#include "ob/Bench.hpp"
#include "ob/Feed.hpp"
#include "ob/StubBook.hpp"

#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>

#if defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
#endif

#ifndef OB_GIT_COMMIT
#define OB_GIT_COMMIT "unknown"
#endif
#ifndef OB_CXX_FLAGS
#define OB_CXX_FLAGS "unknown"
#endif

namespace {

struct Args {
    std::string feed;
    std::string book = "stub";
    std::string name;
    std::string block = "auto";
    std::string summary = "results/summary.csv";
    std::string hist;
    std::string raw;
    bool allowNonInvariant = false;
    ob::u64 limit = 0;
    double bucketNs = 1.0;
};

void Usage() {
    std::cerr
        << "usage: ob_bench --feed PATH [--book stub] [--block "
           "auto|N]\n"
           "                [--summary PATH] [--hist PATH] [--raw "
           "PATH]\n"
           "                [--name LABEL] [--limit N] "
           "[--bucket-ns F]\n"
           "                [--allow-noninvariant]\n";
}

std::string NowUtc() {
    std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ",
                  std::gmtime(&t));
    return buf;
}

ob::u64 ChooseBlock(const Args& a, const ob::CounterInfo& ci,
                    double pilotNsPerUpdate) {
    if (a.block != "auto")
        return std::stoull(a.block);
    if (!ci.coarse)
        return 1;
    double target = 200.0 * ci.resolutionNs;
    ob::u64 n = 1;
    while (static_cast<double>(n) * pilotNsPerUpdate < target)
        n <<= 1;
    return n;
}

template<class Book>
double PilotNsPerUpdate(const ob::Update* updates, ob::u64 count,
                        double ticksPerNs) {
    Book book;
    ob::RunConfig cfg;
    cfg.blockSize = 1024;
    cfg.warmupFraction = 0.0;
    cfg.emptyLoopSamples = 0;
    ob::u64 n = std::min<ob::u64>(count, 1'000'000);
    n -= n % cfg.blockSize;
    if (n == 0)
        return 1.0;
    ob::RunResult r = ob::Replay(book, updates, n, cfg);
    std::vector<double> ns = ob::TicksToNsPerUpdate(r, ticksPerNs);
    ob::Summary s = ob::Summarize(ns);
    return s.median > 0 ? s.median : 1.0;
}

template<class Book>
int RunBook(const Args& a, const ob::MappedFeed& feed,
            const ob::CounterInfo& ci) {
    ob::u64 count = feed.count();
    if (a.limit > 0 && a.limit < count)
        count = a.limit;

    double pilot =
        PilotNsPerUpdate<Book>(feed.data(), count, ci.ticksPerNs);
    ob::RunConfig cfg;
    cfg.blockSize = ChooseBlock(a, ci, pilot);

    Book book;
    ob::RunResult r = ob::Replay(book, feed.data(), count, cfg);
    std::vector<double> ns = ob::TicksToNsPerUpdate(r, ci.ticksPerNs);
    std::span<const double> measured(ns.data() + r.warmupSamples,
                                     ns.size() - r.warmupSamples);
    ob::Summary s = ob::Summarize(measured);
    ob::Histogram h = ob::BuildHistogram(measured, a.bucketNs);

    ob::RunMeta m;
    m.book = a.name.empty() ? std::string(Book::kName) : a.name;
    m.seed = feed.config().seed;
    m.updateCount = r.updatesReplayed;
    m.newLevelRate = feed.config().newLevelRate;
    m.compiler = __VERSION__;
    m.flags = OB_CXX_FLAGS;
    m.cpuModel = ci.cpuModel;
    m.ticksPerNs = ci.ticksPerNs;
    m.resolutionNs = ci.resolutionNs;
    m.blockSize = r.blockSize;
    m.emptyLoopNs = r.emptyLoopMedianTicks / ci.ticksPerNs;
    m.warmupExcluded = r.warmupSamples;
    m.allocsInTimedRegion = r.allocsEnd - r.allocsAfterWarmup;
    m.gitCommit = OB_GIT_COMMIT;
    m.timestamp = NowUtc();

    std::cout << "book:              " << m.book << '\n'
              << "feed:              " << a.feed << '\n'
              << "feed config:       "
              << ob::DescribeConfig(feed.config()) << '\n'
              << "updates replayed:  " << r.updatesReplayed << '\n'
              << "compiler:          " << m.compiler << '\n'
              << "flags:             " << m.flags << '\n'
              << "cpu:               " << m.cpuModel << '\n'
              << "counter:           " << ci.source << ", "
              << ci.ticksPerNs << " ticks/ns, observed resolution "
              << ci.resolutionNs << " ns, "
              << (ci.coarse ? "coarse" : "fine") << '\n'
              << "invariant:         " << ci.invariantNote << '\n'
              << "block size:        " << r.blockSize
              << " updates per sample\n"
              << "samples:           " << r.ticks.size() << " total, "
              << r.warmupSamples << " warmup excluded (1%)\n"
              << "empty loop:        " << m.emptyLoopNs
              << " ns per block median, not subtracted\n"
              << "allocations:       "
              << (r.allocsAfterWarmup - r.allocsStart)
              << " during warmup, " << m.allocsInTimedRegion
              << " after warmup\n"
              << "git commit:        " << m.gitCommit << '\n'
              << "timestamp:         " << m.timestamp << '\n'
              << "ns per update:     count=" << s.count
              << " min=" << s.min << " median=" << s.median
              << " p75=" << s.p75 << " p90=" << s.p90
              << " p99=" << s.p99 << " p99.9=" << s.p999
              << " p99.99=" << s.p9999 << " max=" << s.max
              << " mean=" << s.mean << " stddev=" << s.stddev << '\n';

    int rc = 0;
    if (!Book::kIsStub && s.median < 5.0)
        std::cout << "WARN: median below 5 ns, the work is probably "
                     "being optimized away\n";
    if (s.modeFraction > 0.5)
        std::cout << "WARN: " << s.modeFraction * 100
                  << "% of samples share the value " << s.modeValue
                  << " ns, hard floor from a coarse counter or bad "
                     "calibration\n";
    if (s.median > 0 && s.p9999 > 100.0 * s.median)
        std::cout << "WARN: p99.99 exceeds 100x the median, look for "
                     "page faults or allocations\n";
    if (Book::kNoAllocations && m.allocsInTimedRegion > 0) {
        std::cout
            << "FAIL: " << m.allocsInTimedRegion
            << " allocations inside the timed region for a book "
               "that promises none\n";
        rc = 2;
    }

    if (!a.summary.empty()) {
        ob::AppendSummaryCsv(a.summary, m, s);
        std::cout << "summary appended: " << a.summary << '\n';
    }
    std::string hist =
        a.hist.empty() ? "results/hist_" + m.book + ".csv" : a.hist;
    ob::WriteHistogramCsv(hist, h);
    std::cout << "histogram written: " << hist << " (" << h.bucketNs
              << " ns buckets)\n";
    if (!a.raw.empty()) {
        ob::WriteRawCsv(a.raw, ns);
        std::cout << "raw samples written: " << a.raw << '\n';
    }
    return rc;
}

} // namespace

int main(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << name << " needs a value\n";
                std::exit(1);
            }
            return argv[++i];
        };
        if (s == "--feed")
            a.feed = next("--feed");
        else if (s == "--book")
            a.book = next("--book");
        else if (s == "--name")
            a.name = next("--name");
        else if (s == "--block")
            a.block = next("--block");
        else if (s == "--summary")
            a.summary = next("--summary");
        else if (s == "--hist")
            a.hist = next("--hist");
        else if (s == "--raw")
            a.raw = next("--raw");
        else if (s == "--limit")
            a.limit = std::stoull(next("--limit"));
        else if (s == "--bucket-ns")
            a.bucketNs = std::stod(next("--bucket-ns"));
        else if (s == "--allow-noninvariant")
            a.allowNonInvariant = true;
        else if (s == "--help" || s == "-h") {
            Usage();
            return 0;
        } else {
            std::cerr << "unknown argument " << s << '\n';
            Usage();
            return 1;
        }
    }
    if (a.feed.empty()) {
        Usage();
        return 1;
    }
#if defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    try {
        ob::CounterInfo ci = ob::CalibrateCounter();
        if (!ci.invariant && !a.allowNonInvariant) {
            std::cerr << "refusing to run: " << ci.invariantNote
                      << " (pass --allow-noninvariant to override)\n";
            return 3;
        }
        ob::MappedFeed feed(a.feed);
        if (a.book == "stub")
            return RunBook<ob::StubBook>(a, feed, ci);
        std::cerr << "unknown book " << a.book << " (known: stub)\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
