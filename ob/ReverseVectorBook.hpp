#ifndef OB_REVERSEVECTORBOOK_HPP
#define OB_REVERSEVECTORBOOK_HPP

#include "ob/OrderTable.hpp"
#include "ob/Types.hpp"
#include "ob/VectorBook.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace ob {

struct LowerBoundSearch {
    static constexpr const char* NAME = "reverse_lb";

    template<class Better>
    static std::size_t Find(const Level* data, std::size_t n,
                            Price p) {
        const Level* it = std::lower_bound(
            data, data + n, p, [](const Level& l, Price x) {
                return Better{}(x, l.price);
            });
        return static_cast<std::size_t>(it - data);
    }
};

struct BranchlessSearch {
    static constexpr const char* NAME = "reverse_branchless";

    template<class Better>
    static std::size_t Find(const Level* data, std::size_t n,
                            Price p) {
        if (n == 0)
            return 0;
        const Level* base = data;
        std::size_t len = n;
        while (len > 1) {
            std::size_t half = len / 2;
            base += static_cast<std::size_t>(
                        Better{}(p, base[half].price)) *
                    half;
            len -= half;
        }
        return static_cast<std::size_t>(base - data) +
               static_cast<std::size_t>(Better{}(p, base->price));
    }
};

struct LinearSearch {
    static constexpr const char* NAME = "reverse_linear";

    template<class Better>
    static std::size_t Find(const Level* data, std::size_t n,
                            Price p) {
        std::size_t i = n;
        while (i > 0 && !Better{}(p, data[i - 1].price))
            --i;
        return i;
    }
};

template<class Better, class Search>
class ReverseSide {
  public:
    ReverseSide() {
        levels_.reserve(LEVEL_RESERVE);
    }

    std::size_t Find(Price p) const {
        return Search::template Find<Better>(levels_.data(),
                                             levels_.size(), p);
    }

    void Add(Price p, Volume v) {
        std::size_t i = Find(p);
        if (i < levels_.size() && levels_[i].price == p) {
            levels_[i].volume += v;
            return;
        }
        levels_.insert(levels_.begin() +
                           static_cast<std::ptrdiff_t>(i),
                       Level{p, v});
    }

    void Adjust(Price p, Volume delta) {
        std::size_t i = Find(p);
        levels_[i].volume += delta;
        if (levels_[i].volume.lots <= 0)
            levels_.erase(levels_.begin() +
                          static_cast<std::ptrdiff_t>(i));
    }

    Price Best() const {
        return levels_.empty() ? Price{0} : levels_.back().price;
    }

    LevelIdx Count() const {
        return static_cast<LevelIdx>(levels_.size());
    }

    Volume At(LevelIdx level) const {
        if (level >= levels_.size())
            return Volume{0};
        return levels_[levels_.size() - 1 - level].volume;
    }

    std::vector<std::pair<Price, Volume>>
    Snapshot(LevelIdx depth) const {
        std::vector<std::pair<Price, Volume>> out;
        for (std::size_t k = 0; k < levels_.size() && k < depth;
             ++k) {
            const Level& l = levels_[levels_.size() - 1 - k];
            out.emplace_back(l.price, l.volume);
        }
        return out;
    }

  private:
    std::vector<Level> levels_;
};

template<class Search, template<class> class Table>
class ReverseVectorBook {
  public:
    static constexpr const char* NAME = Search::NAME;
    static constexpr bool IS_STUB = false;

    struct Rec {
        Side side;
        Price price;
        Volume volume;
    };

    static constexpr const char* TABLE_NAME = Table<Rec>::NAME;
    static constexpr bool NO_ALLOCATIONS = Table<Rec>::NO_ALLOCATIONS;

    explicit ReverseVectorBook(
        u64 order_reserve = DEFAULT_ORDER_RESERVE)
        : orders_(order_reserve) {}

    void OnAdd(OrderId id, Side side, Price price, Volume volume) {
        auto [rec, inserted] = orders_.TryInsert(id);
        if (!inserted)
            return;
        rec->side = side;
        rec->price = price;
        rec->volume = volume;
        if (side == Side::Bid)
            bids_.Add(price, volume);
        else
            asks_.Add(price, volume);
    }

    void OnModify(OrderId id, Volume new_volume) {
        Rec* rec = orders_.Find(id);
        if (!rec)
            return;
        if (new_volume.lots <= 0) {
            OnDelete(id);
            return;
        }
        Volume delta = new_volume - rec->volume;
        if (rec->side == Side::Bid)
            bids_.Adjust(rec->price, delta);
        else
            asks_.Adjust(rec->price, delta);
        rec->volume = new_volume;
    }

    void OnDelete(OrderId id) {
        Rec* rec = orders_.Find(id);
        if (!rec)
            return;
        if (rec->side == Side::Bid)
            bids_.Adjust(rec->price, -rec->volume);
        else
            asks_.Adjust(rec->price, -rec->volume);
        orders_.Erase(id);
    }

    Price BestBid() const {
        return bids_.Best();
    }
    Price BestAsk() const {
        return asks_.Best();
    }

    LevelIdx LevelCount(Side side) const {
        return side == Side::Bid ? bids_.Count() : asks_.Count();
    }

    Volume VolumeAtLevel(Side side, LevelIdx level) const {
        return side == Side::Bid ? bids_.At(level) : asks_.At(level);
    }

    std::vector<std::pair<Price, Volume>>
    Snapshot(Side side, LevelIdx depth) const {
        return side == Side::Bid ? bids_.Snapshot(depth)
                                 : asks_.Snapshot(depth);
    }

    bool Contains(OrderId id) const {
        return orders_.Contains(id);
    }

    Lookup DebugLookup(OrderId id) const {
        const Rec* rec = orders_.Find(id);
        if (!rec)
            return Lookup{Side::Bid, Price{0}, Volume{0}};
        return Lookup{rec->side, rec->price, rec->volume};
    }

    u64 OrderCount() const {
        return orders_.Size();
    }

    void Prefetch(OrderId id) const {
        orders_.Prefetch(id);
    }

    TableStats OrderTableStats() const {
        return orders_.Stats();
    }

  private:
    ReverseSide<BidBetter, Search> bids_;
    ReverseSide<AskBetter, Search> asks_;
    Table<Rec> orders_;
};

using ReverseLowerBoundBook =
    ReverseVectorBook<LowerBoundSearch, StdOrderTable>;
using ReverseBranchlessBook =
    ReverseVectorBook<BranchlessSearch, StdOrderTable>;
using ReverseLinearBook =
    ReverseVectorBook<LinearSearch, StdOrderTable>;

}

#endif
