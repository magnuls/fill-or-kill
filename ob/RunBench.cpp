#include "ob/Bench.hpp"
#include "ob/Books.hpp"
#include "ob/Feed.hpp"

#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <pthread.h>
#include <spawn.h>
#include <sys/qos.h>
#ifndef _POSIX_SPAWN_DISABLE_ASLR
#define _POSIX_SPAWN_DISABLE_ASLR 0x0100
#endif
extern char** environ;
#elif defined(__linux__)
#include <sys/personality.h>
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
    std::string table = "std";
    std::string name;
    std::string block = "auto";
    std::string summary = "results/summary.csv";
    std::string hist;
    std::string raw;
    bool allow_non_invariant = false;
    ob::u64 limit = 0;
    double bucket_ns = 1.0;
    ob::u64 repeat = 1;
    ob::u64 prefault = 0;
    ob::u64 scramble = 0;
    bool no_aslr = false;
    bool no_dither = false;
    ob::u64 reserve = 0;
    ob::u64 id_stride = 1;
    bool prefetch = false;
};

int g_anchor = 0;

int ReexecWithoutAslr(char** argv) {
    if (std::getenv("OB_ASLR_DISABLED"))
        return -1;
    setenv("OB_ASLR_DISABLED", "1", 1);
#if defined(__APPLE__)
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, _POSIX_SPAWN_DISABLE_ASLR);
    pid_t pid = 0;
    int rc =
        posix_spawn(&pid, argv[0], nullptr, &attr, argv, environ);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) {
        std::cerr << "warning: could not re-exec without ASLR (" << rc
                  << "), continuing with ASLR\n";
        return -1;
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#elif defined(__linux__)
    if (personality(ADDR_NO_RANDOMIZE) == -1)
        return -1;
    execv("/proc/self/exe", argv);
    return -1;
#else
    (void)argv;
    return -1;
#endif
}

void Usage() {
    std::cerr
        << "usage: ob_bench --feed PATH [--book NAME] [--table NAME] "
           "[--block auto|N]\n"
           "                [--summary PATH] [--hist PATH] [--raw "
           "PATH]\n"
           "                [--name LABEL] [--limit N] "
           "[--bucket-ns F]\n"
           "                [--repeat N] [--prefault N] "
           "[--scramble N] [--no-aslr] [--no-dither]\n"
           "                [--reserve N] [--id-stride N] "
           "[--prefetch] [--allow-noninvariant]\n";
}

std::string NowUtc() {
    std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ",
                  std::gmtime(&t));
    return buf;
}

ob::u64 ChooseBlock(const Args& a, const ob::CounterInfo& ci,
                    double pilot_ns_per_update) {
    if (a.block != "auto")
        return std::stoull(a.block);
    if (!ci.coarse)
        return 1;
    double target = 200.0 * ci.resolution_ns;
    ob::u64 n = 1;
    while (static_cast<double>(n) * pilot_ns_per_update < target)
        n <<= 1;
    return n;
}

template<class Book>
double PilotNsPerUpdate(const ob::Update* updates, ob::u64 count,
                        double ticks_per_ns, ob::u64 reserve,
                        bool prefetch) {
    Book book(reserve);
    ob::RunConfig cfg;
    cfg.block_size = 1024;
    cfg.order_reserve = reserve;
    cfg.prefetch = prefetch;
    cfg.warmup_fraction = 0.0;
    cfg.empty_loop_samples = 0;
    ob::u64 n = std::min<ob::u64>(count, 1'000'000);
    n -= n % cfg.block_size;
    if (n == 0)
        return 1.0;
    ob::RunResult r = ob::Replay(book, updates, n, cfg);
    std::vector<double> ns = ob::TicksToNsPerUpdate(r, ticks_per_ns);
    ob::Summary s = ob::Summarize(ns);
    return s.median > 0 ? s.median : 1.0;
}

template<class Book>
int RunBook(const Args& a, const ob::MappedFeed& feed,
            const ob::Update* updates, const ob::CounterInfo& ci) {
    ob::u64 count = feed.count();
    if (a.limit > 0 && a.limit < count)
        count = a.limit;

    ob::LivePeak live = ob::FindLivePeak(updates, count);
    ob::u64 peak = live.orders;
    ob::u64 reserve = a.reserve > 0 ? a.reserve : peak;
    double pilot = PilotNsPerUpdate<Book>(
        updates, count, ci.ticks_per_ns, reserve, a.prefetch);
    ob::RunConfig cfg;
    cfg.block_size = ChooseBlock(a, ci, pilot);
    cfg.order_reserve = reserve;
    cfg.prefetch = a.prefetch;

    cfg.repeat = a.repeat;
    if (a.prefault > 0)
        ob::PrefaultHeap(a.prefault);
    if (a.scramble > 0)
        ob::ScrambleHeap(a.scramble);
    ob::u64 adds_per_pass = 0;
    for (ob::u64 i = 0; i < count; ++i)
        adds_per_pass += updates[i].type == ob::MsgType::Add;

    ob::RunResult r = ob::ReplayPasses<Book>(updates, count, cfg);
    std::vector<double> ns =
        ob::TicksToNsPerUpdate(r, ci.ticks_per_ns);
    std::span<const double> measured(ns.data() + r.warmup_samples,
                                     ns.size() - r.warmup_samples);
    ob::Summary s = ob::Summarize(measured);
    double quantum =
        ci.coarse && r.block_size > 1
            ? ci.resolution_ns / static_cast<double>(r.block_size)
            : 0.0;
    bool dithered = quantum > 0 && !a.no_dither;
    std::vector<double> for_hist =
        dithered
            ? ob::Dither(measured, quantum, feed.config().seed + 1)
            : std::vector<double>(measured.begin(), measured.end());
    ob::Histogram h = ob::BuildHistogram(for_hist, a.bucket_ns);

    std::string table_name = Book::TABLE_NAME;
    std::string default_name = Book::NAME;
    if (!table_name.empty() && table_name != "std")
        default_name += "_" + table_name;
    if (a.id_stride > 1)
        default_name += "_stride" + std::to_string(a.id_stride);
    if (a.prefetch)
        default_name += "_prefetch";
    ob::RunMeta m;
    m.book = a.name.empty() ? default_name : a.name;
    m.source = feed.source();
    m.seed = feed.config().seed;
    m.update_count = r.updates_replayed;
    m.new_level_rate = feed.config().new_level_rate;
    m.compiler = __VERSION__;
    m.flags = OB_CXX_FLAGS;
    m.cpu_model = ci.cpu_model;
    m.ticks_per_ns = ci.ticks_per_ns;
    m.resolution_ns = ci.resolution_ns;
    m.block_size = r.block_size;
    m.repeat = r.passes;
    m.scramble = a.scramble;
    m.empty_loop_ns = r.empty_loop_median_ticks / ci.ticks_per_ns;
    m.warmup_excluded = r.warmup_samples;
    m.allocs_in_timed_region = r.allocs_end - r.allocs_after_warmup;
    m.git_commit = OB_GIT_COMMIT;
    m.timestamp = NowUtc();

    std::cout
        << "book:              " << m.book << '\n'
        << "feed:              " << a.feed << " (" << m.source
        << ")\n"
        << "feed config:       " << ob::DescribeConfig(feed.config())
        << '\n'
        << "updates replayed:  " << r.updates_replayed << '\n'
        << "compiler:          " << m.compiler << '\n'
        << "flags:             " << m.flags << '\n'
        << "cpu:               " << m.cpu_model << '\n'
        << "counter:           " << ci.source << ", "
        << ci.ticks_per_ns << " ticks/ns, observed resolution "
        << ci.resolution_ns << " ns, "
        << (ci.coarse ? "coarse" : "fine") << '\n'
        << "invariant:         " << ci.invariant_note << '\n'
        << "block size:        " << r.block_size
        << " updates per sample\n"
        << "passes:            " << r.passes
        << " over the feed, fresh book per pass\n"
        << "prefault:          " << a.prefault
        << " heap blocks allocated and freed before timing\n"
        << "scramble:          " << a.scramble
        << " heap blocks, half freed in shuffled order, half kept\n"
        << "order table:       "
        << (table_name.empty() ? std::string("none") : table_name)
        << ", reserve " << reserve << " (peak live orders " << peak
        << (a.reserve > 0 ? ", overridden)" : ")") << '\n'
        << "id stride:         " << a.id_stride
        << (a.id_stride > 1 ? " (ids multiplied at load, adversarial)"
                            : "")
        << '\n'
        << "prefetch:          "
        << (a.prefetch ? "next update's table slot, batched feed "
                         "only, not a live feed technique"
                       : "off")
        << '\n'
        << "aslr:              "
        << (std::getenv("OB_ASLR_DISABLED") ? "disabled" : "enabled")
        << ", anchor address " << static_cast<void*>(&g_anchor)
        << '\n'
        << "histogram:         "
        << (dithered ? "dithered by up to "
                     : "not dithered, quantum ")
        << quantum << " ns\n"
        << "samples:           " << r.ticks.size() << " total, "
        << r.warmup_samples << " warmup excluded (1%)\n"
        << "empty loop:        " << m.empty_loop_ns
        << " ns per block median, not subtracted\n"
        << "allocations:       "
        << (r.allocs_after_warmup - r.allocs_start)
        << " during warmup, " << m.allocs_in_timed_region
        << " after warmup, "
        << (adds_per_pass > 0
                ? static_cast<double>(m.allocs_in_timed_region) /
                      static_cast<double>(adds_per_pass * r.passes)
                : 0.0)
        << " per add, " << r.allocs_constructing_books
        << " constructing books between passes excluded\n"
        << "git commit:        " << m.git_commit << '\n'
        << "timestamp:         " << m.timestamp << '\n'
        << "ns per update:     count=" << s.count << " min=" << s.min
        << " median=" << s.median << " p75=" << s.p75
        << " p90=" << s.p90 << " p99=" << s.p99 << " p99.9=" << s.p999
        << " p99.99=" << s.p9999 << " max=" << s.max
        << " mean=" << s.mean << " stddev=" << s.stddev << '\n';
    if (r.passes > 1) {
        std::cout << "pass medians:     ";
        for (double t : r.pass_median_ticks)
            std::cout << ' '
                      << t / (ci.ticks_per_ns *
                              static_cast<double>(r.block_size));
        std::cout << " ns\n";
    }
    if constexpr (requires(const Book& b) { b.OrderTableStats(); }) {
        Book probe(reserve);
        ob::u64 i = 0;
        for (; i <= live.index && i < count; ++i)
            ob::ApplyUpdate(probe, updates[i]);
        ob::TableStats t = probe.OrderTableStats();
        std::cout << "table:             " << table_name
                  << " capacity=" << t.capacity << ", at peak live "
                  << "(update " << live.index << "): size=" << t.size
                  << " load=" << t.load
                  << " avg_probe=" << t.avg_probe
                  << " max_probe=" << t.max_probe << '\n';
        Book half(reserve);
        for (ob::u64 j = 0; j < count / 2; ++j)
            ob::ApplyUpdate(half, updates[j]);
        t = half.OrderTableStats();
        std::cout << "table at midpoint: size=" << t.size
                  << " load=" << t.load
                  << " avg_probe=" << t.avg_probe
                  << " max_probe=" << t.max_probe << '\n';
    }

    int rc = 0;
    if (!Book::IS_STUB && s.median < 5.0)
        std::cout << "WARN: median below 5 ns, the work is probably "
                     "being optimized away\n";
    if (s.mode_fraction > 0.5)
        std::cout << "WARN: " << s.mode_fraction * 100
                  << "% of samples share the value " << s.mode_value
                  << " ns, hard floor from a coarse counter or bad "
                     "calibration\n";
    if (s.median > 0 && s.p9999 > 100.0 * s.median)
        std::cout << "WARN: p99.99 exceeds 100x the median, look for "
                     "page faults or allocations\n";
    if (Book::NO_ALLOCATIONS && m.allocs_in_timed_region > 0) {
        std::cout
            << "FAIL: " << m.allocs_in_timed_region
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
    std::cout << "histogram written: " << hist << " (" << h.bucket_ns
              << " ns buckets)\n";
    if (!a.raw.empty()) {
        ob::WriteRawCsv(a.raw, ns);
        std::cout << "raw samples written: " << a.raw << '\n';
    }
    return rc;
}

}

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
        else if (s == "--table")
            a.table = next("--table");
        else if (s == "--reserve")
            a.reserve = std::stoull(next("--reserve"));
        else if (s == "--id-stride")
            a.id_stride = std::stoull(next("--id-stride"));
        else if (s == "--prefetch")
            a.prefetch = true;
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
            a.bucket_ns = std::stod(next("--bucket-ns"));
        else if (s == "--repeat")
            a.repeat = std::stoull(next("--repeat"));
        else if (s == "--prefault")
            a.prefault = std::stoull(next("--prefault"));
        else if (s == "--scramble")
            a.scramble = std::stoull(next("--scramble"));
        else if (s == "--no-aslr")
            a.no_aslr = true;
        else if (s == "--no-dither")
            a.no_dither = true;
        else if (s == "--allow-noninvariant")
            a.allow_non_invariant = true;
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
    if (a.no_aslr) {
        int rc = ReexecWithoutAslr(argv);
        if (rc >= 0)
            return rc;
    }
#if defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    try {
        ob::CounterInfo ci = ob::CalibrateCounter();
        if (!ci.invariant && !a.allow_non_invariant) {
            std::cerr << "refusing to run: " << ci.invariant_note
                      << " (pass --allow-noninvariant to override)\n";
            return 3;
        }
        ob::MappedFeed feed(a.feed);
        std::vector<ob::Update> strided;
        const ob::Update* updates = feed.data();
        if (a.id_stride > 1) {
            strided.assign(feed.data(), feed.data() + feed.count());
            for (ob::Update& u : strided)
                u.id *= a.id_stride;
            updates = strided.data();
        }
        int rc = 0;
        bool known = ob::DispatchBook(a.book, a.table, [&](auto tag) {
            using B = typename decltype(tag)::type;
            rc = RunBook<B>(a, feed, updates, ci);
        });
        if (!known) {
            std::cerr << "unknown book or table " << a.book << " / "
                      << a.table << " (books: " << ob::KnownBooks()
                      << "; tables: " << ob::KnownTables() << ")\n";
            return 1;
        }
        return rc;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
