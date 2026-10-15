#include "ob/Bench.hpp"
#include "ob/Books.hpp"
#include "ob/Diff.hpp"
#include "ob/Feed.hpp"
#include "ob/Itch.hpp"
#include "ob/ReferenceBook.hpp"
#include "ob/StubBook.hpp"
#include "ob/Types.hpp"
#include "ob/Validate.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include <zlib.h>

using namespace ob;

static bool near(f64 a, f64 b, f64 eps = 1e-9) {
    return std::fabs(a - b) < eps;
}

static void update_layout() {
    assert(sizeof(Update) == 32);
    assert(offsetof(Update, type) == 0);
    assert(offsetof(Update, side) == 1);
    assert(offsetof(Update, price) == 4);
    assert(offsetof(Update, id) == 8);
    assert(offsetof(Update, volume) == 16);
    assert(offsetof(Update, timestamp) == 24);
}

static void strong_types() {
    Price a{100};
    Price b{101};
    assert(a < b);
    assert(a != b);
    assert(a == Price{100});
    Volume v{5};
    v += Volume{3};
    assert(v == Volume{8});
    v -= Volume{10};
    assert(v == Volume{-2});
    assert(-v == Volume{2});
    assert(Volume{1} + Volume{2} == Volume{3});
    assert(Opposite(Side::Bid) == Side::Ask);
    assert(Opposite(Side::Ask) == Side::Bid);
}

static FeedConfig small_config(u64 seed, u64 count) {
    FeedConfig c;
    c.seed = seed;
    c.update_count = count;
    c.levels_per_side = 200;
    c.orders_per_level_seed = 3;
    return c;
}

static bool same_bytes(const std::vector<Update>& a,
                       const std::vector<Update>& b) {
    return a.size() == b.size() &&
           std::memcmp(a.data(), b.data(),
                       a.size() * sizeof(Update)) == 0;
}

static void deterministic() {
    FeedConfig c = small_config(7, 50'000);
    std::vector<Update> a = GenerateFeed(c);
    std::vector<Update> b = GenerateFeed(c);
    assert(a.size() == c.update_count);
    assert(same_bytes(a, b));
    c.seed = 8;
    std::vector<Update> d = GenerateFeed(c);
    assert(!same_bytes(a, d));
}

struct Checker {
    struct Rec {
        Side side;
        Price price;
        Volume volume;
    };
    std::unordered_map<OrderId, Rec> orders;
    std::map<i32, i64> bids;
    std::map<i32, i64> asks;

    std::map<i32, i64>& side_of(Side s) {
        return s == Side::Bid ? bids : asks;
    }

    void apply(const Update& u) {
        if (u.type == MsgType::Add) {
            assert(!orders.contains(u.id));
            assert(u.volume.lots > 0);
            orders.emplace(u.id, Rec{u.side, u.price, u.volume});
            side_of(u.side)[u.price.ticks] += u.volume.lots;
        } else if (u.type == MsgType::Modify) {
            auto it = orders.find(u.id);
            assert(it != orders.end());
            assert(it->second.side == u.side);
            assert(it->second.price == u.price);
            assert(u.volume.lots > 0);
            auto& lvl = side_of(u.side)[u.price.ticks];
            lvl += u.volume.lots - it->second.volume.lots;
            it->second.volume = u.volume;
        } else {
            auto it = orders.find(u.id);
            assert(it != orders.end());
            assert(it->second.side == u.side);
            assert(it->second.price == u.price);
            assert(it->second.volume == u.volume);
            auto& side = side_of(u.side);
            auto lv = side.find(u.price.ticks);
            lv->second -= u.volume.lots;
            assert(lv->second >= 0);
            if (lv->second == 0)
                side.erase(lv);
            orders.erase(it);
        }
        if (!bids.empty() && !asks.empty())
            assert(bids.rbegin()->first < asks.begin()->first);
    }
};

static void valid_sequence_and_mix() {
    FeedConfig c = small_config(3, 1'000'000);
    std::vector<u32> depth;
    std::vector<Update> feed = GenerateFeed(c, &depth);
    assert(depth.size() == feed.size());
    Checker chk;
    u64 counts[3] = {0, 0, 0};
    u64 top5 = 0;
    u64 deep = 0;
    u64 seed_count = c.SeedMessageCount();
    for (std::size_t i = 0; i < feed.size(); ++i) {
        const Update& u = feed[i];
        assert(u.pad == 0);
        assert(u.id != 0);
        chk.apply(u);
        if (i < seed_count)
            continue;
        assert(!chk.bids.empty());
        assert(!chk.asks.empty());
        ++counts[static_cast<int>(u.type)];
        if (depth[i] < 5)
            ++top5;
        if (depth[i] > 500)
            ++deep;
    }
    f64 n = static_cast<f64>(feed.size() - seed_count);
    f64 add_frac = static_cast<f64>(counts[0]) / n;
    f64 mod_frac = static_cast<f64>(counts[1]) / n;
    f64 del_frac = static_cast<f64>(counts[2]) / n;
    assert(add_frac > 0.38 && add_frac < 0.42);
    assert(mod_frac > 0.28 && mod_frac < 0.32);
    assert(del_frac > 0.28 && del_frac < 0.32);
    f64 top5Frac = static_cast<f64>(top5) / n;
    assert(top5Frac > 0.55 && top5Frac < 0.80);
    assert(deep > 0);
    for (std::size_t i = 1; i < feed.size(); ++i)
        assert(feed[i].timestamp > feed[i - 1].timestamp);
    assert(chk.bids.size() > 100 && chk.asks.size() > 100);
}

static void level_count_stays_near_config() {
    FeedConfig c = small_config(11, 400'000);
    std::vector<Update> feed = GenerateFeed(c);
    Checker chk;
    for (const Update& u : feed)
        chk.apply(u);
    assert(chk.bids.size() > c.levels_per_side / 2);
    assert(chk.asks.size() > c.levels_per_side / 2);
    assert(chk.bids.size() < c.levels_per_side * 3 / 2);
    assert(chk.asks.size() < c.levels_per_side * 3 / 2);
}

static void file_round_trip() {
    FeedConfig c = small_config(5, 20'000);
    std::vector<Update> feed = GenerateFeed(c);
    std::string path = "ob_feed_test.bin";
    WriteFeed(path, c, feed);
    {
        MappedFeed m(path);
        assert(m.count() == feed.size());
        assert(m.config().seed == 5);
        assert(m.config().update_count == 20'000);
        assert(m.bytes() ==
               sizeof(FeedHeader) + feed.size() * sizeof(Update));
        assert(std::memcmp(m.data(), feed.data(),
                           feed.size() * sizeof(Update)) == 0);
    }
    std::remove(path.c_str());
    bool threw = false;
    try {
        MappedFeed missing("ob_feed_missing.bin");
    } catch (const std::exception&) {
        threw = true;
    }
    assert(threw);
}

static void rejects_bad_config() {
    FeedConfig c = small_config(1, 10);
    bool threw = false;
    try {
        GenerateFeed(c);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
    c = small_config(1, 10'000);
    c.add_pct = 50;
    threw = false;
    try {
        GenerateFeed(c);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
}

static void summary_known_values() {
    std::vector<f64> v;
    for (int i = 1; i <= 100; ++i)
        v.push_back(static_cast<f64>(101 - i));
    Summary s = Summarize(v);
    assert(s.count == 100);
    assert(near(s.min, 1));
    assert(near(s.max, 100));
    assert(near(s.median, 50.5));
    assert(near(s.p75, 75.25));
    assert(near(s.p90, 90.1));
    assert(near(s.p99, 99.01));
    assert(near(s.mean, 50.5));
    assert(near(s.stddev, std::sqrt(833.25)));
    assert(near(s.mode_fraction, 0.01));
}

static void summary_mode() {
    std::vector<f64> v(10, 42.0);
    v.push_back(1);
    v.push_back(2);
    Summary s = Summarize(v);
    assert(near(s.mode_value, 42.0));
    assert(s.mode_fraction > 0.8);
    Summary e = Summarize(std::vector<f64>{});
    assert(e.count == 0);
}

static void histogram_buckets() {
    std::vector<f64> v = {0.0,   0.9,   1.0, 1.5,
                          499.9, 500.0, 1e9, -3.0};
    Histogram h = BuildHistogram(v);
    assert(h.total == 8);
    assert(h.buckets[0] == 3);
    assert(h.buckets[1] == 2);
    assert(h.buckets[499] == 1);
    assert(h.buckets[500] == 2);
    assert(near(h.Median(), 1.0));
}

static void histogram_fine_buckets() {
    std::vector<f64> v = {0.334, 0.30,  0.33, 0.33,
                          0.36,  4.999, 5.0,  9.0};
    Histogram h = BuildHistogram(v, 0.01);
    assert(near(h.bucket_ns, 0.01));
    assert(h.total == 8);
    assert(h.buckets[33] == 3);
    assert(h.buckets[30] == 1);
    assert(h.buckets[36] == 1);
    assert(h.buckets[499] == 1);
    assert(h.buckets[500] == 2);
    assert(near(h.Median(), 0.33));
    Histogram bad = BuildHistogram(v, 0.0);
    assert(near(bad.bucket_ns, 1.0));
}

static void csv_writers() {
    RunMeta m;
    m.book = "stub";
    m.flags = "-O2 \"quoted\"";
    Summary s = Summarize(std::vector<f64>{1, 2, 3});
    std::string path = "ob_summary_test.csv";
    std::remove(path.c_str());
    AppendSummaryCsv(path, m, s);
    AppendSummaryCsv(path, m, s);
    std::ifstream in(path);
    std::string line;
    int lines = 0;
    while (std::getline(in, line)) {
        if (lines == 0)
            assert(line == SummaryCsvHeader());
        else
            assert(line.find("\"-O2 \"\"quoted\"\"\"") !=
                   std::string::npos);
        ++lines;
    }
    assert(lines == 3);
    in.close();
    std::remove(path.c_str());

    Histogram h = BuildHistogram(std::vector<f64>{3, 3, 600});
    std::string hp = "ob_hist_test.csv";
    WriteHistogramCsv(hp, h);
    std::ifstream hin(hp);
    lines = 0;
    while (std::getline(hin, line)) {
        if (lines == 0)
            assert(line == "ns,count");
        if (lines == 4)
            assert(line == "3,2");
        if (lines == 501)
            assert(line == "500,1");
        ++lines;
    }
    assert(lines == 502);
    hin.close();
    std::remove(hp.c_str());
}

static void put16(std::vector<u8>& v, u16 x) {
    v.push_back(static_cast<u8>(x >> 8));
    v.push_back(static_cast<u8>(x));
}
static void put32(std::vector<u8>& v, u32 x) {
    put16(v, static_cast<u16>(x >> 16));
    put16(v, static_cast<u16>(x));
}
static void put48(std::vector<u8>& v, u64 x) {
    put16(v, static_cast<u16>(x >> 32));
    put32(v, static_cast<u32>(x));
}
static void put64(std::vector<u8>& v, u64 x) {
    put32(v, static_cast<u32>(x >> 32));
    put32(v, static_cast<u32>(x));
}
static void put_stock(std::vector<u8>& v, const std::string& s) {
    for (std::size_t i = 0; i < 8; ++i)
        v.push_back(i < s.size() ? static_cast<u8>(s[i]) : ' ');
}
static std::vector<u8> hdr(char type, u16 locate, u64 ts) {
    std::vector<u8> v;
    v.push_back(static_cast<u8>(type));
    put16(v, locate);
    put16(v, 0);
    put48(v, ts);
    return v;
}
static std::vector<u8> msg_r(u16 locate, const std::string& stock) {
    std::vector<u8> v = hdr('R', locate, 0);
    put_stock(v, stock);
    while (v.size() < 39)
        v.push_back(0);
    return v;
}
static std::vector<u8> msg_a(u16 locate, u64 ts, u64 ref, char side,
                            u32 shares, const std::string& stock,
                            u32 price, bool attributed = false) {
    std::vector<u8> v = hdr(attributed ? 'F' : 'A', locate, ts);
    put64(v, ref);
    v.push_back(static_cast<u8>(side));
    put32(v, shares);
    put_stock(v, stock);
    put32(v, price);
    if (attributed)
        put_stock(v, "MPID");
    return v;
}
static std::vector<u8> msg_e(u16 locate, u64 ts, u64 ref,
                             u32 executed,
                            bool with_price = false, u32 price = 0) {
    std::vector<u8> v = hdr(with_price ? 'C' : 'E', locate, ts);
    put64(v, ref);
    put32(v, executed);
    put64(v, 12345);
    if (with_price) {
        v.push_back('Y');
        put32(v, price);
    }
    return v;
}
static std::vector<u8> msg_x(u16 locate, u64 ts, u64 ref,
                            u32 cancelled) {
    std::vector<u8> v = hdr('X', locate, ts);
    put64(v, ref);
    put32(v, cancelled);
    return v;
}
static std::vector<u8> msg_d(u16 locate, u64 ts, u64 ref) {
    std::vector<u8> v = hdr('D', locate, ts);
    put64(v, ref);
    return v;
}
static std::vector<u8> msg_u(u16 locate, u64 ts, u64 old_ref,
                            u64 new_ref, u32 shares, u32 price) {
    std::vector<u8> v = hdr('U', locate, ts);
    put64(v, old_ref);
    put64(v, new_ref);
    put32(v, shares);
    put32(v, price);
    return v;
}

static std::vector<std::vector<u8>> itch_script() {
    std::vector<std::vector<u8>> m;
    m.push_back(msg_r(7, "AAPL"));
    m.push_back(msg_r(8, "MSFT"));
    m.push_back(msg_a(7, 1000, 1, 'B', 100, "AAPL", 2900000));
    m.push_back(msg_a(7, 1001, 2, 'S', 50, "AAPL", 2901000, true));
    m.push_back(msg_a(8, 1002, 9, 'B', 10, "MSFT", 1500000));
    m.push_back(msg_e(7, 1003, 1, 40));
    m.push_back(msg_x(7, 1004, 1, 10));
    m.push_back(msg_e(7, 1005, 2, 50, true, 2901000));
    m.push_back(msg_u(7, 1006, 1, 3, 70, 2899000));
    std::vector<u8> unknown = hdr('P', 7, 1007);
    while (unknown.size() < 44)
        unknown.push_back(0);
    m.push_back(unknown);
    m.push_back(msg_e(7, 1008, 777, 5));
    m.push_back(msg_d(7, 1009, 3));
    return m;
}

static void check_itch_updates(const std::vector<Update>& u) {
    assert(u.size() == 8);
    assert(u[0].type == MsgType::Add && u[0].id == 1 &&
           u[0].side == Side::Bid && u[0].price == Price{2900000} &&
           u[0].volume == Volume{100} && u[0].timestamp == 1000 &&
           u[0].pad == itch::REASON_PLAIN);
    assert(u[1].type == MsgType::Add && u[1].id == 2 &&
           u[1].side == Side::Ask && u[1].price == Price{2901000} &&
           u[1].volume == Volume{50});
    assert(u[2].type == MsgType::Modify && u[2].id == 1 &&
           u[2].volume == Volume{60} &&
           u[2].pad == itch::REASON_EXECUTION &&
           u[2].side == Side::Bid && u[2].price == Price{2900000});
    assert(u[3].type == MsgType::Modify && u[3].id == 1 &&
           u[3].volume == Volume{50} &&
           u[3].pad == itch::REASON_CANCEL);
    assert(u[4].type == MsgType::Delete && u[4].id == 2 &&
           u[4].volume == Volume{50} &&
           u[4].pad == itch::REASON_EXECUTION);
    assert(u[5].type == MsgType::Delete && u[5].id == 1 &&
           u[5].volume == Volume{50} &&
           u[5].pad == itch::REASON_REPLACE);
    assert(u[6].type == MsgType::Add && u[6].id == 3 &&
           u[6].side == Side::Bid && u[6].price == Price{2899000} &&
           u[6].volume == Volume{70} &&
           u[6].pad == itch::REASON_REPLACE &&
           u[6].timestamp == 1006);
    assert(u[7].type == MsgType::Delete && u[7].id == 3 &&
           u[7].volume == Volume{70} && u[7].timestamp == 1009);
}

static void itch_messages() {
    itch::Converter c({"AAPL"});
    for (const auto& m : itch_script())
        c.OnMessage(m.data(), m.size());
    assert(c.States().size() == 1);
    itch::SymbolState& st = c.States()[0];
    check_itch_updates(st.updates);
    assert(st.unknown_ids == 1);
    assert(st.dropped == 0);
    assert(st.live_high_water == 2);
    assert(st.orders.empty());
    const itch::Stats& s = c.GetStats();
    assert(s.messages == 12);
    assert(s.by_type['A'] == 2 && s.by_type['F'] == 1 &&
           s.by_type['E'] == 2);
    assert(s.by_type['P'] == 1 && s.by_type['R'] == 2);
    assert(s.symbols_in_directory == 2);
    assert(s.first_ts == 0 && s.last_ts == 1009);
}

static void itch_window_seeds_resting_orders() {
    u64 from = 1004;
    itch::Converter c({"AAPL"}, from, 1006);
    for (const auto& m : itch_script())
        c.OnMessage(m.data(), m.size());
    const std::vector<Update>& u = c.States()[0].updates;
    assert(u.size() == 6);
    assert(u[0].type == MsgType::Add && u[0].id == 1 &&
           u[0].volume == Volume{60} && u[0].timestamp == from);
    assert(u[1].type == MsgType::Add && u[1].id == 2 &&
           u[1].volume == Volume{50} && u[1].timestamp == from);
    assert(u[2].type == MsgType::Modify && u[2].id == 1 &&
           u[2].volume == Volume{50} && u[2].timestamp == 1004);
    assert(u[3].type == MsgType::Delete && u[3].id == 2);
    assert(u[4].type == MsgType::Delete && u[4].id == 1);
    assert(u[5].type == MsgType::Add && u[5].id == 3);
    assert(c.States()[0].orders.empty());
}

static void itch_gz_file_round_trip() {
    std::string path = "ob_itch_test.gz";
    gzFile gz = gzopen(path.c_str(), "wb");
    assert(gz);
    for (const auto& m : itch_script()) {
        u8 len[2] = {static_cast<u8>(m.size() >> 8),
                     static_cast<u8>(m.size())};
        assert(gzwrite(gz, len, 2) == 2);
        assert(
            gzwrite(gz, m.data(), static_cast<unsigned>(m.size())) ==
            static_cast<int>(m.size()));
    }
    gzclose(gz);
    itch::Converter c({"AAPL"});
    std::string err;
    assert(itch::ReadItchFile(path, c, err));
    check_itch_updates(c.States()[0].updates);
    std::remove(path.c_str());
    assert(!itch::ReadItchFile("ob_itch_missing.gz", c, err));
    assert(!err.empty());
    assert(itch::ParseClockNs("09:30") ==
           9 * itch::NS_PER_HOUR + 30 * itch::NS_PER_MINUTE);
}

static void feed_source_and_version() {
    FeedConfig c = small_config(9, 5'000);
    std::vector<Update> feed = GenerateFeed(c);
    std::string path = "ob_feed_source_test.bin";
    WriteFeed(path, c, feed, "itch:test:AAPL");
    {
        MappedFeed m(path);
        assert(m.source() == "itch:test:AAPL");
        assert(m.header().version == FEED_VERSION);
    }
    FeedHeader h{};
    h.magic = FEED_MAGIC;
    h.version = 1;
    h.config = c;
    h.count = 0;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    assert(std::fwrite(&h, sizeof h, 1, f) == 1);
    std::fclose(f);
    bool threw = false;
    try {
        MappedFeed old(path);
    } catch (const std::exception&) {
        threw = true;
    }
    assert(threw);
    std::remove(path.c_str());
}

static void reference_book() {
    ReferenceBook b;
    assert(b.LevelCount(Side::Bid) == 0 && b.BestBid() == Price{0});
    b.OnAdd(1, Side::Bid, Price{100}, Volume{10});
    b.OnAdd(2, Side::Bid, Price{101}, Volume{5});
    b.OnAdd(3, Side::Bid, Price{100}, Volume{7});
    b.OnAdd(4, Side::Ask, Price{103}, Volume{20});
    b.OnAdd(5, Side::Ask, Price{102}, Volume{1});
    assert(b.BestBid() == Price{101});
    assert(b.BestAsk() == Price{102});
    assert(b.LevelCount(Side::Bid) == 2);
    assert(b.LevelCount(Side::Ask) == 2);
    assert(b.VolumeAtLevel(Side::Bid, 0) == Volume{5});
    assert(b.VolumeAtLevel(Side::Bid, 1) == Volume{17});
    assert(b.VolumeAtLevel(Side::Bid, 2) == Volume{0});
    assert(b.VolumeAtLevel(Side::Ask, 1) == Volume{20});
    assert(b.OrderCount() == 5);
    assert(b.OrderCount(Side::Bid) == 3);
    assert(b.TotalVolume(Side::Bid) == Volume{22});
    ReferenceBook::Lookup lk = b.DebugLookup(3);
    assert(lk.side == Side::Bid && lk.price == Price{100} &&
           lk.volume == Volume{7});
    auto snap = b.Snapshot(Side::Bid, 5);
    assert(snap.size() == 2);
    assert(snap[0].first == Price{101} &&
           snap[0].second == Volume{5});
    assert(snap[1].first == Price{100} &&
           snap[1].second == Volume{17});
    b.OnModify(3, Volume{2});
    assert(b.VolumeAtLevel(Side::Bid, 1) == Volume{12});
    b.OnModify(2, Volume{0});
    assert(!b.Contains(2));
    assert(b.BestBid() == Price{100});
    assert(b.LevelCount(Side::Bid) == 1);
    b.OnDelete(5);
    assert(b.BestAsk() == Price{103});
    b.OnDelete(4);
    assert(b.LevelCount(Side::Ask) == 0 && b.BestAsk() == Price{0});
    b.OnDelete(999);
    b.OnModify(999, Volume{3});
    assert(b.OrderCount() == 2);
    assert(b.DebugLookup(999).volume == Volume{0});
}

static void validator_passes_and_fails() {
    FeedConfig c = small_config(4, 60'000);
    std::vector<Update> feed = GenerateFeed(c);
    ValidateOptions opt;
    opt.every = 1000;
    ValidateReport r = ValidateFeed(feed.data(), feed.size(), opt);
    assert(r.ok);
    assert(r.updates == feed.size());
    assert(r.adds + r.modifies + r.deletes == feed.size());
    assert(r.executions == 0);
    assert(r.sum_checks == 60);
    assert(r.max_bid_levels >= 100 && r.median_bid_levels >= 100);
    assert(r.live_high_water > 0 && r.live_at_end > 0);

    std::vector<Update> bad = feed;
    bad.push_back(Update{MsgType::Delete, Side::Bid, 0, Price{1},
                         999'999'999, Volume{1}, 0});
    r = ValidateFeed(bad.data(), bad.size(), opt);
    assert(!r.ok && r.failed_at == feed.size());
    assert(r.failure.find("unknown id") != std::string::npos);

    bad = feed;
    for (std::size_t i = 0; i < bad.size(); ++i) {
        if (bad[i].type == MsgType::Modify) {
            bad[i].price = Price{bad[i].price.ticks + 1};
            r = ValidateFeed(bad.data(), bad.size(), opt);
            assert(!r.ok && r.failed_at == i);
            break;
        }
    }

    std::vector<Update> exec;
    exec.push_back(Update{MsgType::Add, Side::Bid, 0, Price{100}, 1,
                          Volume{10}, 100});
    exec.push_back(Update{MsgType::Add, Side::Bid, 0, Price{99}, 2,
                          Volume{10}, 101});
    exec.push_back(Update{MsgType::Modify, Side::Bid,
                          itch::REASON_EXECUTION, Price{100}, 1,
                          Volume{5}, 102});
    exec.push_back(Update{MsgType::Delete, Side::Bid,
                          itch::REASON_EXECUTION, Price{99}, 2,
                          Volume{10}, 103});
    r = ValidateFeed(exec.data(), exec.size(), opt);
    assert(r.ok && r.executions == 2 && r.executions_at_inside == 1);
}

template<class B>
static std::vector<std::pair<Price, Volume>> levels_of(const B& b,
                                                       Side s) {
    return b.Snapshot(s, b.LevelCount(s));
}

template<class B>
static void edge_cases() {
    B b;
    assert(b.LevelCount(Side::Bid) == 0 &&
           b.LevelCount(Side::Ask) == 0);
    assert(b.BestBid() == Price{0} && b.BestAsk() == Price{0});
    assert(b.VolumeAtLevel(Side::Bid, 0) == Volume{0});
    assert(!b.Contains(1));
    b.OnDelete(1);
    b.OnModify(1, Volume{5});
    assert(b.LevelCount(Side::Bid) == 0 && b.OrderCount() == 0);

    b.OnAdd(1, Side::Bid, Price{100}, Volume{10});
    assert(b.LevelCount(Side::Bid) == 1 &&
           b.LevelCount(Side::Ask) == 0);
    assert(b.BestBid() == Price{100} && b.BestAsk() == Price{0});

    b.OnAdd(2, Side::Bid, Price{102}, Volume{3});
    assert(b.BestBid() == Price{102});
    b.OnAdd(3, Side::Bid, Price{90}, Volume{7});
    assert(b.LevelCount(Side::Bid) == 3);
    assert(b.VolumeAtLevel(Side::Bid, 0) == Volume{3});
    assert(b.VolumeAtLevel(Side::Bid, 1) == Volume{10});
    assert(b.VolumeAtLevel(Side::Bid, 2) == Volume{7});
    b.OnAdd(4, Side::Bid, Price{95}, Volume{1});
    assert(b.VolumeAtLevel(Side::Bid, 2) == Volume{1});
    assert(b.VolumeAtLevel(Side::Bid, 3) == Volume{7});

    b.OnAdd(9, Side::Bid, Price{100}, Volume{5});
    b.OnAdd(8, Side::Bid, Price{100}, Volume{5});
    b.OnAdd(7, Side::Bid, Price{100}, Volume{5});
    assert(b.VolumeAtLevel(Side::Bid, 1) == Volume{25});
    assert(b.LevelCount(Side::Bid) == 4);

    b.OnAdd(1, Side::Ask, Price{200}, Volume{999});
    assert(b.LevelCount(Side::Ask) == 0);
    assert(b.DebugLookup(1).side == Side::Bid);
    assert(b.DebugLookup(1).volume == Volume{10});

    b.OnModify(1, Volume{40});
    assert(b.VolumeAtLevel(Side::Bid, 1) == Volume{55});
    b.OnModify(1, Volume{0});
    assert(!b.Contains(1));
    assert(b.VolumeAtLevel(Side::Bid, 1) == Volume{15});
    b.OnDelete(9);
    b.OnDelete(8);
    b.OnDelete(7);
    assert(b.LevelCount(Side::Bid) == 3);
    assert(b.VolumeAtLevel(Side::Bid, 1) == Volume{1});

    b.OnDelete(2);
    assert(b.BestBid() == Price{95});
    assert(b.LevelCount(Side::Bid) == 2);
    b.OnDelete(3);
    assert(b.LevelCount(Side::Bid) == 1);
    b.OnDelete(4);
    assert(b.LevelCount(Side::Bid) == 0 && b.BestBid() == Price{0});
    assert(b.OrderCount() == 0);

    b.OnAdd(20, Side::Ask, Price{105}, Volume{2});
    b.OnAdd(21, Side::Ask, Price{103}, Volume{4});
    b.OnAdd(22, Side::Ask, Price{110}, Volume{8});
    assert(b.BestAsk() == Price{103});
    assert(b.VolumeAtLevel(Side::Ask, 0) == Volume{4});
    assert(b.VolumeAtLevel(Side::Ask, 2) == Volume{8});
    b.OnAdd(23, Side::Bid, Price{102}, Volume{1});
    assert(b.BestBid() == Price{102} && b.BestAsk() == Price{103});
    b.OnDelete(21);
    assert(b.BestAsk() == Price{105});
    auto snap = levels_of(b, Side::Ask);
    assert(snap.size() == 2 && snap[0].first == Price{105} &&
           snap[1].first == Price{110});
    b.OnDelete(999);
    assert(levels_of(b, Side::Ask) == snap);
}

template<class B>
static void fuzz_against_reference(u64 seed, u64 ops) {
    std::mt19937_64 rng(seed);
    ReferenceBook ref;
    B b;
    std::vector<OrderId> live;
    OrderId next = 1;
    std::string why;
    for (u64 i = 0; i < ops; ++i) {
        u64 roll = rng() % 100;
        if (roll < 45 || live.empty()) {
            Side side = (rng() & 1) ? Side::Ask : Side::Bid;
            i32 base = side == Side::Bid ? 990 : 1010;
            i32 offset = static_cast<i32>(rng() % 12);
            Price p{side == Side::Bid ? base - offset
                                      : base + offset};
            Volume v{static_cast<i64>(1 + rng() % 20)};
            OrderId id = next++;
            ref.OnAdd(id, side, p, v);
            b.OnAdd(id, side, p, v);
            live.push_back(id);
        } else if (roll < 70) {
            std::size_t k = rng() % live.size();
            Volume v{static_cast<i64>(rng() % 25)};
            ref.OnModify(live[k], v);
            b.OnModify(live[k], v);
            if (v.lots == 0) {
                live[k] = live.back();
                live.pop_back();
            }
        } else {
            std::size_t k = rng() % live.size();
            ref.OnDelete(live[k]);
            b.OnDelete(live[k]);
            live[k] = live.back();
            live.pop_back();
        }
        assert(CompareTop(ref, b, 4, why));
        if (i % 100 == 0)
            assert(CompareFull(ref, b, why));
    }
    assert(CompareFull(ref, b, why));
}

template<class Search, class Better>
static void search_matches_lower_bound(u64 seed) {
    std::mt19937_64 rng(seed);
    for (int round = 0; round < 300; ++round) {
        std::size_t n = rng() % 64;
        std::vector<Level> data;
        std::vector<i32> prices;
        for (std::size_t i = 0; i < n; ++i)
            prices.push_back(static_cast<i32>(rng() % 200));
        std::sort(prices.begin(), prices.end());
        prices.erase(std::unique(prices.begin(), prices.end()),
                     prices.end());
        if constexpr (std::is_same_v<Better, AskBetter>)
            std::reverse(prices.begin(), prices.end());
        for (i32 p : prices)
            data.push_back(Level{Price{p}, Volume{1}});
        for (int probe = 0; probe < 40; ++probe) {
            Price x{static_cast<i32>(rng() % 220) - 10};
            std::size_t want = LowerBoundSearch::Find<Better>(
                data.data(), data.size(), x);
            std::size_t got = Search::template Find<Better>(
                data.data(), data.size(), x);
            assert(want == got);
        }
    }
}

template<class Inner>
class BrokenBook {
  public:
    static constexpr const char* NAME = "broken";
    static constexpr bool NO_ALLOCATIONS = false;
    static constexpr bool IS_STUB = false;
    void OnAdd(OrderId id, Side s, Price p, Volume v) {
        inner_.OnAdd(id, s, p, v);
    }
    void OnModify(OrderId id, Volume v) {
        inner_.OnModify(id, v);
    }
    void OnDelete(OrderId id) {
        if (++deletes_ % 1000 != 0)
            inner_.OnDelete(id);
    }
    Price BestBid() const {
        return inner_.BestBid();
    }
    Price BestAsk() const {
        return inner_.BestAsk();
    }
    LevelIdx LevelCount(Side s) const {
        return inner_.LevelCount(s);
    }
    Volume VolumeAtLevel(Side s, LevelIdx l) const {
        return inner_.VolumeAtLevel(s, l);
    }
    std::vector<std::pair<Price, Volume>> Snapshot(Side s,
                                                   LevelIdx d) const {
        return inner_.Snapshot(s, d);
    }
    bool Contains(OrderId id) const {
        return inner_.Contains(id);
    }
    u64 OrderCount() const {
        return inner_.OrderCount();
    }

  private:
    Inner inner_;
    u64 deletes_ = 0;
};

static void books_edge_cases() {
    edge_cases<ReferenceBook>();
    edge_cases<MapBook>();
    edge_cases<VectorBook>();
    edge_cases<ReverseLowerBoundBook>();
    edge_cases<ReverseBranchlessBook>();
    edge_cases<ReverseLinearBook>();
}

static void books_fuzz() {
    const u64 ops = 300'000;
    fuzz_against_reference<MapBook>(1, ops);
    fuzz_against_reference<VectorBook>(2, ops);
    fuzz_against_reference<ReverseLowerBoundBook>(3, ops);
    fuzz_against_reference<ReverseBranchlessBook>(4, ops);
    fuzz_against_reference<ReverseLinearBook>(5, ops);
}

static void search_policies() {
    search_matches_lower_bound<BranchlessSearch, BidBetter>(11);
    search_matches_lower_bound<BranchlessSearch, AskBetter>(12);
    search_matches_lower_bound<LinearSearch, BidBetter>(13);
    search_matches_lower_bound<LinearSearch, AskBetter>(14);
    std::vector<Level> asc = {{Price{1}, Volume{1}},
                              {Price{3}, Volume{1}},
                              {Price{5}, Volume{1}}};
    assert((LinearSearch::Find<BidBetter>(asc.data(), 3, Price{3}) ==
            1));
    assert((LinearSearch::Find<BidBetter>(asc.data(), 3, Price{4}) ==
            2));
    assert((LinearSearch::Find<BidBetter>(asc.data(), 3, Price{9}) ==
            3));
    assert((LinearSearch::Find<BidBetter>(asc.data(), 3, Price{0}) ==
            0));
    assert((BranchlessSearch::Find<BidBetter>(asc.data(), 0,
                                              Price{3}) == 0));
}

static void diff_feed_passes_and_catches() {
    FeedConfig c = small_config(21, 80'000);
    std::vector<Update> feed = GenerateFeed(c);
    DiffReport r;
    for (const char* n : VARIANT_NAMES) {
        bool known = DispatchBook(n, [&](auto tag) {
            using B = typename decltype(tag)::type;
            if constexpr (!B::IS_STUB)
                r = DiffFeed<B>(feed.data(), feed.size(), 500);
        });
        assert(known);
        assert(r.ok && r.updates == feed.size() &&
               r.full_compares == 160);
    }
    r = DiffFeed<BrokenBook<VectorBook>>(feed.data(), feed.size(),
                                         500);
    assert(!r.ok);
    assert(r.failed_at < feed.size());
    bool unknown = DispatchBook("nope", [](auto) {});
    assert(!unknown);
}

static std::vector<Update> small_feed() {
    FeedConfig c;
    c.seed = 2;
    c.update_count = 100'000;
    c.levels_per_side = 100;
    c.orders_per_level_seed = 2;
    return GenerateFeed(c);
}

static void replay_counts_every_update() {
    std::vector<Update> feed = small_feed();
    StubBook book;
    RunConfig cfg;
    cfg.block_size = 1;
    RunResult r = Replay(book, feed.data(), feed.size(), cfg);
    assert(book.Count() == feed.size());
    assert(r.updates_replayed == feed.size());
    assert(r.ticks.size() == feed.size());
    assert(r.warmup_samples == feed.size() / 100);
    assert(r.allocs_end == r.allocs_after_warmup);
    assert(r.allocs_after_warmup == r.allocs_start);
}

static void block_mode() {
    std::vector<Update> feed = small_feed();
    StubBook book;
    RunConfig cfg;
    cfg.block_size = 64;
    RunResult r = Replay(book, feed.data(), feed.size(), cfg);
    u64 samples = feed.size() / 64;
    assert(r.ticks.size() == samples);
    assert(r.updates_replayed == samples * 64);
    assert(book.Count() == samples * 64);
    assert(r.allocs_end == r.allocs_start);
    std::vector<f64> ns = TicksToNsPerUpdate(r, 2.0);
    assert(ns.size() == samples);
    for (std::size_t i = 0; i < ns.size(); ++i)
        assert(ns[i] == static_cast<f64>(r.ticks[i]) / 128.0);
}

static void repeat_passes() {
    std::vector<Update> feed = small_feed();
    RunConfig cfg;
    cfg.block_size = 100;
    cfg.repeat = 3;
    cfg.empty_loop_samples = 10;
    RunResult r =
        ReplayPasses<StubBook>(feed.data(), feed.size(), cfg);
    u64 per_pass = feed.size() / 100;
    assert(r.passes == 3);
    assert(r.ticks.size() == per_pass * 3);
    assert(r.updates_replayed == per_pass * 100 * 3);
    assert(r.warmup_samples == per_pass * 3 / 100);
    assert(r.pass_median_ticks.size() == 3);
    assert(r.allocs_end == r.allocs_start);
    RunResult one =
        ReplayPasses<ReferenceBook>(feed.data(), 5000, cfg);
    assert(one.passes == 3 && one.ticks.size() == 150);
    assert(one.allocs_end > one.allocs_start);
}

static void dither_and_prefault() {
    std::vector<f64> q;
    for (int i = 0; i < 6000; ++i)
        q.push_back(0.65625 * static_cast<f64>(100 + (i % 7)));
    Histogram raw = BuildHistogram(q);
    u64 empty_raw = 0;
    for (u32 i = 65; i < 71; ++i)
        empty_raw += raw.buckets[i] == 0;
    assert(empty_raw > 0);
    std::vector<f64> d = Dither(q, 0.65625, 42);
    assert(d.size() == q.size());
    for (std::size_t i = 0; i < q.size(); ++i)
        assert(d[i] >= q[i] && d[i] < q[i] + 0.65625);
    Histogram smooth = BuildHistogram(d);
    u64 empty_smooth = 0;
    for (u32 i = 66; i < 70; ++i)
        empty_smooth += smooth.buckets[i] == 0;
    assert(empty_smooth == 0);
    assert(std::fabs(Summarize(d).median - Summarize(q).median) <
           0.65625);
    std::vector<f64> same = Dither(q, 0.65625, 42);
    assert(same == d);
    std::vector<f64> zero = Dither(q, 0.0, 1);
    assert(zero == q);
    u64 before = AllocationCount();
    PrefaultHeap(1000);
    assert(AllocationCount() >= before + 1000);
}

static void allocation_counter_moves() {
    u64 before = AllocationCount();
    {
        std::vector<int> v(1000);
        DoNotOptimize(v);
    }
    u64 after = AllocationCount();
    assert(after > before);
    assert(DeallocationCount() > 0);
}

static void counter_calibration() {
    CounterInfo ci = CalibrateCounter();
    assert(ci.ticks_per_ns > 0.001);
    assert(ci.ticks_per_ns < 100.0);
    assert(ci.resolution_ns > 0);
    assert(!ci.source.empty());
    assert(!ci.cpu_model.empty());
    u64 a = ReadCounter();
    u64 b = ReadCounter();
    assert(b >= a);
}

int main() {
    update_layout();
    strong_types();
    std::cout << "types ok\n";
    deterministic();
    valid_sequence_and_mix();
    level_count_stays_near_config();
    file_round_trip();
    rejects_bad_config();
    std::cout << "feed ok\n";
    summary_known_values();
    summary_mode();
    histogram_buckets();
    histogram_fine_buckets();
    csv_writers();
    std::cout << "stats ok\n";
    replay_counts_every_update();
    block_mode();
    repeat_passes();
    dither_and_prefault();
    allocation_counter_moves();
    counter_calibration();
    std::cout << "harness ok\n";
    itch_messages();
    itch_window_seeds_resting_orders();
    itch_gz_file_round_trip();
    feed_source_and_version();
    std::cout << "itch ok\n";
    reference_book();
    validator_passes_and_fails();
    std::cout << "reference and validator ok\n";
    books_edge_cases();
    search_policies();
    books_fuzz();
    diff_feed_passes_and_catches();
    std::cout << "books ok\n";
    std::cout << "all checks passed\n";
    return 0;
}
