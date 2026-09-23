#include "ob/Bench.hpp"
#include "ob/Feed.hpp"
#include "ob/StubBook.hpp"
#include "ob/Types.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

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
    c.updateCount = count;
    c.levelsPerSide = 200;
    c.ordersPerLevelSeed = 3;
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
    assert(a.size() == c.updateCount);
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

    std::map<i32, i64>& sideOf(Side s) {
        return s == Side::Bid ? bids : asks;
    }

    void apply(const Update& u) {
        if (u.type == MsgType::Add) {
            assert(!orders.contains(u.id));
            assert(u.volume.lots > 0);
            orders.emplace(u.id, Rec{u.side, u.price, u.volume});
            sideOf(u.side)[u.price.ticks] += u.volume.lots;
        } else if (u.type == MsgType::Modify) {
            auto it = orders.find(u.id);
            assert(it != orders.end());
            assert(it->second.side == u.side);
            assert(it->second.price == u.price);
            assert(u.volume.lots > 0);
            auto& lvl = sideOf(u.side)[u.price.ticks];
            lvl += u.volume.lots - it->second.volume.lots;
            it->second.volume = u.volume;
        } else {
            auto it = orders.find(u.id);
            assert(it != orders.end());
            assert(it->second.side == u.side);
            assert(it->second.price == u.price);
            assert(it->second.volume == u.volume);
            auto& side = sideOf(u.side);
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
    u64 seedCount = c.SeedMessageCount();
    for (std::size_t i = 0; i < feed.size(); ++i) {
        const Update& u = feed[i];
        assert(u.pad == 0);
        assert(u.id != 0);
        chk.apply(u);
        if (i < seedCount)
            continue;
        assert(!chk.bids.empty());
        assert(!chk.asks.empty());
        ++counts[static_cast<int>(u.type)];
        if (depth[i] < 5)
            ++top5;
        if (depth[i] > 500)
            ++deep;
    }
    f64 n = static_cast<f64>(feed.size() - seedCount);
    f64 addFrac = static_cast<f64>(counts[0]) / n;
    f64 modFrac = static_cast<f64>(counts[1]) / n;
    f64 delFrac = static_cast<f64>(counts[2]) / n;
    assert(addFrac > 0.38 && addFrac < 0.42);
    assert(modFrac > 0.28 && modFrac < 0.32);
    assert(delFrac > 0.28 && delFrac < 0.32);
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
    assert(chk.bids.size() > c.levelsPerSide / 2);
    assert(chk.asks.size() > c.levelsPerSide / 2);
    assert(chk.bids.size() < c.levelsPerSide * 3 / 2);
    assert(chk.asks.size() < c.levelsPerSide * 3 / 2);
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
        assert(m.config().updateCount == 20'000);
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
    c.addPct = 50;
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
    assert(near(s.modeFraction, 0.01));
}

static void summary_mode() {
    std::vector<f64> v(10, 42.0);
    v.push_back(1);
    v.push_back(2);
    Summary s = Summarize(v);
    assert(near(s.modeValue, 42.0));
    assert(s.modeFraction > 0.8);
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
    assert(near(h.bucketNs, 0.01));
    assert(h.total == 8);
    assert(h.buckets[33] == 3);
    assert(h.buckets[30] == 1);
    assert(h.buckets[36] == 1);
    assert(h.buckets[499] == 1);
    assert(h.buckets[500] == 2);
    assert(near(h.Median(), 0.33));
    Histogram bad = BuildHistogram(v, 0.0);
    assert(near(bad.bucketNs, 1.0));
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

static std::vector<Update> small_feed() {
    FeedConfig c;
    c.seed = 2;
    c.updateCount = 100'000;
    c.levelsPerSide = 100;
    c.ordersPerLevelSeed = 2;
    return GenerateFeed(c);
}

static void replay_counts_every_update() {
    std::vector<Update> feed = small_feed();
    StubBook book;
    RunConfig cfg;
    cfg.blockSize = 1;
    RunResult r = Replay(book, feed.data(), feed.size(), cfg);
    assert(book.Count() == feed.size());
    assert(r.updatesReplayed == feed.size());
    assert(r.ticks.size() == feed.size());
    assert(r.warmupSamples == feed.size() / 100);
    assert(r.allocsEnd == r.allocsAfterWarmup);
    assert(r.allocsAfterWarmup == r.allocsStart);
}

static void block_mode() {
    std::vector<Update> feed = small_feed();
    StubBook book;
    RunConfig cfg;
    cfg.blockSize = 64;
    RunResult r = Replay(book, feed.data(), feed.size(), cfg);
    u64 samples = feed.size() / 64;
    assert(r.ticks.size() == samples);
    assert(r.updatesReplayed == samples * 64);
    assert(book.Count() == samples * 64);
    assert(r.allocsEnd == r.allocsStart);
    std::vector<f64> ns = TicksToNsPerUpdate(r, 2.0);
    assert(ns.size() == samples);
    for (std::size_t i = 0; i < ns.size(); ++i)
        assert(ns[i] == static_cast<f64>(r.ticks[i]) / 128.0);
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
    assert(ci.ticksPerNs > 0.001);
    assert(ci.ticksPerNs < 100.0);
    assert(ci.resolutionNs > 0);
    assert(!ci.source.empty());
    assert(!ci.cpuModel.empty());
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
    allocation_counter_moves();
    counter_calibration();
    std::cout << "harness ok\n";
    std::cout << "all checks passed\n";
    return 0;
}
