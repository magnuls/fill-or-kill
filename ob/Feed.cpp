#include "ob/Feed.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <random>
#include <sstream>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>

namespace ob {

namespace {

struct ShadowOrder {
    Side side;
    Price price;
    Volume volume;
    u32 slot;
};

struct ShadowSide {
    Side side;
    std::vector<Price> levels;
    std::unordered_map<i32, std::vector<OrderId>> orders;

    bool Better(Price a, Price b) const {
        return side == Side::Bid ? a > b : a < b;
    }
    Price TowardTail(Price p, i32 n = 1) const {
        return Price{side == Side::Bid ? p.ticks - n : p.ticks + n};
    }
    Price TowardTop(Price p, i32 n = 1) const {
        return Price{side == Side::Bid ? p.ticks + n : p.ticks - n};
    }
    bool Has(Price p) const {
        return orders.contains(p.ticks);
    }
    void InsertLevel(Price p) {
        auto pos = std::lower_bound(
            levels.begin(), levels.end(), p,
            [this](Price a, Price b) { return Better(a, b); });
        levels.insert(pos, p);
        orders.emplace(p.ticks, std::vector<OrderId>{});
    }
    void EraseLevel(Price p) {
        auto pos = std::find(levels.begin(), levels.end(), p);
        levels.erase(pos);
        orders.erase(p.ticks);
    }
    u64 LiveOrders() const {
        u64 n = 0;
        for (const auto& [k, v] : orders)
            n += v.size();
        return n;
    }
};

class Generator {
  public:
    Generator(const FeedConfig& cfg, std::vector<u32>* depthOut)
        : cfg_(cfg), rng_(cfg.seed), depthOut_(depthOut) {
        bids_.side = Side::Bid;
        asks_.side = Side::Ask;
        BuildDepthCdf();
    }

    std::vector<Update> Run() {
        if (cfg_.addPct + cfg_.modifyPct + cfg_.deletePct != 100)
            throw std::invalid_argument(
                "message type percentages must sum to 100");
        if (cfg_.levelsPerSide == 0 || cfg_.ordersPerLevelSeed == 0)
            throw std::invalid_argument(
                "levelsPerSide and ordersPerLevelSeed must be "
                "positive");
        if (cfg_.updateCount < cfg_.SeedMessageCount())
            throw std::invalid_argument(
                "updateCount smaller than the seed message count");
        out_.reserve(cfg_.updateCount);
        if (depthOut_)
            depthOut_->reserve(cfg_.updateCount);
        SeedBook();
        while (out_.size() < cfg_.updateCount)
            Step();
        return std::move(out_);
    }

  private:
    void BuildDepthCdf() {
        cdf_.resize(cfg_.maxDepth);
        f64 acc = 0;
        for (u32 k = 0; k < cfg_.maxDepth; ++k) {
            acc += std::pow(static_cast<f64>(k + 1),
                            -cfg_.depthExponent);
            cdf_[k] = acc;
        }
        for (f64& c : cdf_)
            c /= acc;
    }

    u32 SampleDepth() {
        f64 u = uniform_(rng_);
        auto it = std::lower_bound(cdf_.begin(), cdf_.end(), u);
        if (it == cdf_.end())
            return cfg_.maxDepth - 1;
        return static_cast<u32>(it - cdf_.begin());
    }

    ShadowSide& SideRef(Side s) {
        return s == Side::Bid ? bids_ : asks_;
    }

    Side CoinSide() {
        return (rng_() & 1) ? Side::Ask : Side::Bid;
    }

    Volume RandomVolume() {
        return Volume{
            static_cast<i64>(std::uniform_int_distribution<u32>(
                1, cfg_.maxVolume)(rng_))};
    }

    u64 NextTimestamp() {
        ts_ += std::uniform_int_distribution<u64>(1, 1000)(rng_);
        return ts_;
    }

    void Emit(MsgType type, Side side, Price price, OrderId id,
              Volume volume, u32 depth) {
        out_.push_back(Update{type, side, 0, price, id, volume,
                              NextTimestamp()});
        if (depthOut_)
            depthOut_->push_back(depth);
    }

    void EmitAdd(ShadowSide& sd, Price price, u32 depth) {
        OrderId id = nextId_++;
        Volume v = RandomVolume();
        auto& ids = sd.orders[price.ticks];
        u32 slot = static_cast<u32>(ids.size());
        ids.push_back(id);
        table_.emplace(id, ShadowOrder{sd.side, price, v, slot});
        Emit(MsgType::Add, sd.side, price, id, v, depth);
    }

    void SeedBook() {
        Price bid{cfg_.midPrice - 1};
        Price ask{cfg_.midPrice + 1};
        std::uniform_int_distribution<i32> gap(1, 3);
        for (u32 k = 0; k < cfg_.levelsPerSide; ++k) {
            bids_.InsertLevel(bid);
            asks_.InsertLevel(ask);
            for (u32 j = 0; j < cfg_.ordersPerLevelSeed; ++j) {
                EmitAdd(bids_, bid, k);
                EmitAdd(asks_, ask, k);
            }
            bid = bids_.TowardTail(bid, gap(rng_));
            ask = asks_.TowardTail(ask, gap(rng_));
        }
    }

    bool TryCreateLevel(ShadowSide& sd, u32 k) {
        ShadowSide& opp = SideRef(Opposite(sd.side));
        Price candidate{0};
        if (k == 0) {
            candidate = sd.TowardTop(sd.levels.front());
            bool crosses = sd.side == Side::Bid
                               ? candidate >= opp.levels.front()
                               : candidate <= opp.levels.front();
            if (crosses) {
                if (sd.levels.size() < 2)
                    return false;
                k = 1;
            }
        }
        if (k > 0) {
            u32 base =
                std::min<u32>(k, static_cast<u32>(sd.levels.size())) -
                1;
            candidate = sd.TowardTail(sd.levels[base]);
            while (sd.Has(candidate))
                candidate = sd.TowardTail(candidate);
        }
        sd.InsertLevel(candidate);
        EmitAdd(sd, candidate, k);
        return true;
    }

    void DoAdd() {
        Side side = CoinSide();
        ShadowSide& sd = SideRef(side);
        u32 k = SampleDepth();
        if (uniform_(rng_) < cfg_.newLevelRate &&
            TryCreateLevel(sd, k))
            return;
        u32 idx =
            std::min<u32>(k, static_cast<u32>(sd.levels.size()) - 1);
        EmitAdd(sd, sd.levels[idx], idx);
    }

    struct Pick {
        ShadowSide* sd;
        u32 idx;
        Price price;
        OrderId id;
    };

    Pick PickOrder(bool allowPrune) {
        Side side = CoinSide();
        ShadowSide& sd = SideRef(side);
        u32 size = static_cast<u32>(sd.levels.size());
        u32 idx;
        if (allowPrune && size > cfg_.levelsPerSide) {
            idx = std::uniform_int_distribution<u32>(size - size / 4,
                                                     size - 1)(rng_);
        } else {
            idx = std::min<u32>(SampleDepth(), size - 1);
        }
        Price price = sd.levels[idx];
        auto& ids = sd.orders[price.ticks];
        u32 j = std::uniform_int_distribution<u32>(
            0, static_cast<u32>(ids.size()) - 1)(rng_);
        return Pick{&sd, idx, price, ids[j]};
    }

    void DoModify() {
        Pick p = PickOrder(false);
        ShadowOrder& o = table_[p.id];
        i64 hi = std::min<i64>(2 * o.volume.lots,
                               4 * static_cast<i64>(cfg_.maxVolume));
        Volume nv{std::uniform_int_distribution<i64>(1, hi)(rng_)};
        o.volume = nv;
        Emit(MsgType::Modify, o.side, o.price, p.id, nv, p.idx);
    }

    void DoDelete() {
        Pick p = PickOrder(true);
        ShadowSide& sd = *p.sd;
        auto& ids = sd.orders[p.price.ticks];
        if (sd.levels.size() == 1 && ids.size() == 1) {
            DoAdd();
            return;
        }
        ShadowOrder o = table_[p.id];
        table_.erase(p.id);
        OrderId moved = ids.back();
        ids[o.slot] = moved;
        table_[moved].slot = o.slot;
        ids.pop_back();
        if (ids.empty())
            sd.EraseLevel(p.price);
        Emit(MsgType::Delete, o.side, o.price, p.id, o.volume, p.idx);
    }

    void Step() {
        u32 roll = std::uniform_int_distribution<u32>(0, 99)(rng_);
        if (roll < cfg_.addPct)
            DoAdd();
        else if (roll < cfg_.addPct + cfg_.modifyPct)
            DoModify();
        else
            DoDelete();
    }

    FeedConfig cfg_;
    std::mt19937_64 rng_;
    std::uniform_real_distribution<f64> uniform_{0.0, 1.0};
    std::vector<u32>* depthOut_;
    std::vector<f64> cdf_;
    ShadowSide bids_;
    ShadowSide asks_;
    std::unordered_map<OrderId, ShadowOrder> table_;
    std::vector<Update> out_;
    OrderId nextId_ = 1;
    u64 ts_ = 0;
};

} // namespace

std::vector<Update> GenerateFeed(const FeedConfig& cfg,
                                 std::vector<u32>* depthOut) {
    Generator g(cfg, depthOut);
    return g.Run();
}

void WriteFeed(const std::string& path, const FeedConfig& cfg,
               const std::vector<Update>& updates) {
    FeedHeader h{};
    h.magic = kFeedMagic;
    h.version = kFeedVersion;
    h.pad = 0;
    h.config = cfg;
    h.count = updates.size();
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        throw std::runtime_error("cannot open " + path + ": " +
                                 std::strerror(errno));
    bool ok = std::fwrite(&h, sizeof h, 1, f) == 1;
    if (ok && !updates.empty())
        ok = std::fwrite(updates.data(), sizeof(Update),
                         updates.size(), f) == updates.size();
    ok = (std::fclose(f) == 0) && ok;
    if (!ok)
        throw std::runtime_error("short write to " + path);
}

MappedFeed::MappedFeed(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0)
        throw std::runtime_error("cannot open " + path + ": " +
                                 std::strerror(errno));
    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        throw std::runtime_error("fstat failed on " + path);
    }
    bytes_ = static_cast<u64>(st.st_size);
    if (bytes_ < sizeof(FeedHeader)) {
        ::close(fd);
        throw std::runtime_error("feed file too small: " + path);
    }
    base_ = ::mmap(nullptr, bytes_, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (base_ == MAP_FAILED) {
        base_ = nullptr;
        throw std::runtime_error("mmap failed on " + path);
    }
    header_ = static_cast<const FeedHeader*>(base_);
    if (header_->magic != kFeedMagic ||
        header_->version != kFeedVersion) {
        ::munmap(base_, bytes_);
        base_ = nullptr;
        throw std::runtime_error("bad feed header in " + path);
    }
    if (bytes_ !=
        sizeof(FeedHeader) + header_->count * sizeof(Update)) {
        ::munmap(base_, bytes_);
        base_ = nullptr;
        throw std::runtime_error("feed size mismatch in " + path);
    }
    data_ = reinterpret_cast<const Update*>(
        static_cast<const char*>(base_) + sizeof(FeedHeader));
}

MappedFeed::~MappedFeed() {
    if (base_)
        ::munmap(base_, bytes_);
}

std::string DescribeConfig(const FeedConfig& cfg) {
    std::ostringstream os;
    os << "seed=" << cfg.seed << " updateCount=" << cfg.updateCount
       << " levelsPerSide=" << cfg.levelsPerSide
       << " maxDepth=" << cfg.maxDepth << " midPrice=" << cfg.midPrice
       << " ordersPerLevelSeed=" << cfg.ordersPerLevelSeed
       << " depthExponent=" << cfg.depthExponent
       << " newLevelRate=" << cfg.newLevelRate
       << " mix=" << cfg.addPct << "/" << cfg.modifyPct << "/"
       << cfg.deletePct << " maxVolume=" << cfg.maxVolume;
    return os.str();
}

} // namespace ob
