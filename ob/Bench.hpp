#ifndef OB_BENCH_HPP
#define OB_BENCH_HPP

#include "ob/Types.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <fstream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <time.h>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
#endif
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace ob {

template<class T>
inline void DoNotOptimize(const T& value) {
    asm volatile("" : : "r,m"(value) : "memory");
}

template<class Book>
inline void ObserveBook(Book& book) {
    Book* p = &book;
    asm volatile("" : : "r"(p) : "memory");
}

template<class Book>
inline void ApplyUpdate(Book& book, const Update& u) {
    switch (u.type) {
    case MsgType::Add:
        book.OnAdd(u.id, u.side, u.price, u.volume);
        break;
    case MsgType::Modify:
        book.OnModify(u.id, u.volume);
        break;
    case MsgType::Delete:
        book.OnDelete(u.id);
        break;
    }
}

inline u64 ReadCounter() {
#if defined(__x86_64__) || defined(_M_X64)
    return __rdtsc();
#elif defined(__aarch64__)
    u64 v;
    asm volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
#else
#error "unsupported architecture"
#endif
}

inline u64 MonotonicNs() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<u64>(ts.tv_sec) * 1000000000ull +
           static_cast<u64>(ts.tv_nsec);
}

struct CounterInfo {
    f64 ticksPerNs = 0.0;
    f64 resolutionNs = 0.0;
    std::string source;
    bool invariant = false;
    bool coarse = false;
    std::string invariantNote;
    std::string cpuModel;
};

namespace detail {

inline std::string ReadCpuModel() {
#if defined(__APPLE__)
    char buf[256];
    size_t len = sizeof buf;
    if (sysctlbyname("machdep.cpu.brand_string", buf, &len, nullptr,
                     0) == 0)
        return std::string(buf, len > 0 ? len - 1 : 0);
    return "unknown";
#else
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("model name", 0) == 0) {
            auto pos = line.find(':');
            if (pos != std::string::npos)
                return line.substr(pos + 2);
        }
    }
    return "unknown";
#endif
}

inline void CheckInvariant(CounterInfo& info) {
#if defined(__x86_64__) && defined(__linux__)
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    bool constant = false;
    bool nonstop = false;
    while (std::getline(in, line)) {
        if (line.rfind("flags", 0) == 0) {
            constant = line.find("constant_tsc") != std::string::npos;
            nonstop = line.find("nonstop_tsc") != std::string::npos;
            break;
        }
    }
    info.invariant = constant && nonstop;
    info.invariantNote = info.invariant
                             ? "constant_tsc and nonstop_tsc present"
                             : "constant_tsc or nonstop_tsc missing";
    info.source = "rdtsc";
#elif defined(__x86_64__)
    info.invariant = true;
    info.invariantNote = "non linux x86: invariant tsc assumed";
    info.source = "rdtsc";
#elif defined(__aarch64__)
    info.invariant = true;
    info.invariantNote = "arm64 generic timer is fixed frequency";
    info.source = "cntvct_el0";
#endif
}

inline f64 Percentile(const std::vector<f64>& sorted, f64 p) {
    if (sorted.empty())
        return 0;
    f64 pos = p * static_cast<f64>(sorted.size() - 1);
    auto idx = static_cast<std::size_t>(pos);
    if (idx + 1 >= sorted.size())
        return sorted.back();
    f64 frac = pos - static_cast<f64>(idx);
    return sorted[idx] + frac * (sorted[idx + 1] - sorted[idx]);
}

inline std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"')
            out += '"';
        out += c;
    }
    out += '"';
    return out;
}

} // namespace detail

inline CounterInfo CalibrateCounter() {
    CounterInfo info;
    detail::CheckInvariant(info);
    info.cpuModel = detail::ReadCpuModel();

    const u64 spinNs = 100'000'000ull;
    u64 t0 = MonotonicNs();
    u64 c0 = ReadCounter();
    u64 t1 = t0;
    while (t1 - t0 < spinNs)
        t1 = MonotonicNs();
    u64 c1 = ReadCounter();
    info.ticksPerNs =
        static_cast<f64>(c1 - c0) / static_cast<f64>(t1 - t0);

    const u64 probes = 1'000'000;
    std::vector<u64> deltas;
    deltas.reserve(probes);
    u64 zeros = 0;
    u64 prev = ReadCounter();
    for (u64 i = 0; i < probes; ++i) {
        u64 now = ReadCounter();
        u64 d = now - prev;
        prev = now;
        if (d == 0)
            ++zeros;
        else
            deltas.push_back(d);
    }
    u64 granularity = 1;
    if (!deltas.empty()) {
        std::sort(deltas.begin(), deltas.end());
        u64 bestRun = 0;
        u64 run = 1;
        for (std::size_t i = 1; i <= deltas.size(); ++i) {
            if (i < deltas.size() && deltas[i] == deltas[i - 1]) {
                ++run;
                continue;
            }
            if (run > bestRun) {
                bestRun = run;
                granularity = deltas[i - 1];
            }
            run = 1;
        }
    }
    info.resolutionNs =
        static_cast<f64>(granularity) / info.ticksPerNs;
    info.coarse = zeros > probes / 2;
    return info;
}

u64 AllocationCount();
u64 DeallocationCount();
u64 AllocatedBytes();

struct Summary {
    u64 count = 0;
    f64 min = 0;
    f64 median = 0;
    f64 p75 = 0;
    f64 p90 = 0;
    f64 p99 = 0;
    f64 p999 = 0;
    f64 p9999 = 0;
    f64 max = 0;
    f64 mean = 0;
    f64 stddev = 0;
    f64 modeValue = 0;
    f64 modeFraction = 0;
};

inline Summary Summarize(std::span<const f64> samples) {
    Summary s;
    s.count = samples.size();
    if (samples.empty())
        return s;
    std::vector<f64> sorted(samples.begin(), samples.end());
    std::sort(sorted.begin(), sorted.end());
    s.min = sorted.front();
    s.max = sorted.back();
    s.median = detail::Percentile(sorted, 0.5);
    s.p75 = detail::Percentile(sorted, 0.75);
    s.p90 = detail::Percentile(sorted, 0.90);
    s.p99 = detail::Percentile(sorted, 0.99);
    s.p999 = detail::Percentile(sorted, 0.999);
    s.p9999 = detail::Percentile(sorted, 0.9999);
    f64 sum = 0;
    for (f64 v : sorted)
        sum += v;
    s.mean = sum / static_cast<f64>(sorted.size());
    f64 sq = 0;
    for (f64 v : sorted)
        sq += (v - s.mean) * (v - s.mean);
    s.stddev = std::sqrt(sq / static_cast<f64>(sorted.size()));

    std::size_t bestRun = 0;
    f64 bestVal = sorted.front();
    std::size_t run = 1;
    for (std::size_t i = 1; i < sorted.size(); ++i) {
        if (sorted[i] == sorted[i - 1]) {
            ++run;
        } else {
            if (run > bestRun) {
                bestRun = run;
                bestVal = sorted[i - 1];
            }
            run = 1;
        }
    }
    if (run > bestRun) {
        bestRun = run;
        bestVal = sorted.back();
    }
    s.modeValue = bestVal;
    s.modeFraction =
        static_cast<f64>(bestRun) / static_cast<f64>(sorted.size());
    return s;
}

constexpr u32 kHistogramBuckets = 500;

struct Histogram {
    std::array<u64, kHistogramBuckets + 1> buckets{};
    u64 total = 0;
    f64 bucketNs = 1.0;

    f64 Median() const {
        if (total == 0)
            return 0;
        u64 half = (total + 1) / 2;
        u64 acc = 0;
        for (u32 i = 0; i <= kHistogramBuckets; ++i) {
            acc += buckets[i];
            if (acc >= half)
                return static_cast<f64>(i) * bucketNs;
        }
        return static_cast<f64>(kHistogramBuckets) * bucketNs;
    }
};

inline Histogram BuildHistogram(std::span<const f64> samples,
                                f64 bucketNs = 1.0) {
    Histogram h;
    h.bucketNs = bucketNs > 0 ? bucketNs : 1.0;
    for (f64 v : samples) {
        if (v < 0)
            v = 0;
        f64 scaled = v / h.bucketNs;
        u64 b = scaled >= static_cast<f64>(kHistogramBuckets)
                    ? kHistogramBuckets
                    : static_cast<u64>(scaled);
        ++h.buckets[b];
    }
    h.total = samples.size();
    return h;
}

struct RunMeta {
    std::string book;
    u64 seed = 0;
    u64 updateCount = 0;
    f64 newLevelRate = 0;
    std::string compiler;
    std::string flags;
    std::string cpuModel;
    f64 ticksPerNs = 0;
    f64 resolutionNs = 0;
    u64 blockSize = 1;
    f64 emptyLoopNs = 0;
    u64 warmupExcluded = 0;
    u64 allocsInTimedRegion = 0;
    std::string gitCommit;
    std::string timestamp;
};

inline std::string SummaryCsvHeader() {
    return "book,seed,update_count,new_level_rate,compiler,flags,"
           "cpu_model,ticks_per_ns,resolution_ns,block_size,"
           "empty_loop_ns,warmup_excluded,allocs_in_timed_region,"
           "git_commit,timestamp,count,min,median,p75,p90,p99,p999,"
           "p9999,max,mean,stddev";
}

inline std::string SummaryCsvRow(const RunMeta& m, const Summary& s) {
    std::ostringstream os;
    os.precision(6);
    os << std::fixed;
    os << detail::Quote(m.book) << ',' << m.seed << ','
       << m.updateCount << ',' << m.newLevelRate << ','
       << detail::Quote(m.compiler) << ',' << detail::Quote(m.flags)
       << ',' << detail::Quote(m.cpuModel) << ',' << m.ticksPerNs
       << ',' << m.resolutionNs << ',' << m.blockSize << ','
       << m.emptyLoopNs << ',' << m.warmupExcluded << ','
       << m.allocsInTimedRegion << ',' << detail::Quote(m.gitCommit)
       << ',' << detail::Quote(m.timestamp) << ',' << s.count << ','
       << s.min << ',' << s.median << ',' << s.p75 << ',' << s.p90
       << ',' << s.p99 << ',' << s.p999 << ',' << s.p9999 << ','
       << s.max << ',' << s.mean << ',' << s.stddev;
    return os.str();
}

inline void AppendSummaryCsv(const std::string& path,
                             const RunMeta& meta, const Summary& s) {
    bool fresh = true;
    {
        std::ifstream probe(path);
        fresh = !probe.good() ||
                probe.peek() == std::ifstream::traits_type::eof();
    }
    std::ofstream out(path, std::ios::app);
    if (!out)
        throw std::runtime_error("cannot open " + path);
    if (fresh)
        out << SummaryCsvHeader() << '\n';
    out << SummaryCsvRow(meta, s) << '\n';
}

inline void WriteHistogramCsv(const std::string& path,
                              const Histogram& h) {
    std::ofstream out(path);
    if (!out)
        throw std::runtime_error("cannot open " + path);
    out << "ns,count\n";
    for (u32 i = 0; i <= kHistogramBuckets; ++i)
        out << static_cast<f64>(i) * h.bucketNs << ',' << h.buckets[i]
            << '\n';
}

inline void WriteRawCsv(const std::string& path,
                        std::span<const f64> samples) {
    std::ofstream out(path);
    if (!out)
        throw std::runtime_error("cannot open " + path);
    out << "index,ns\n";
    for (std::size_t i = 0; i < samples.size(); ++i)
        out << i << ',' << samples[i] << '\n';
}

struct RunConfig {
    u64 blockSize = 1;
    f64 warmupFraction = 0.01;
    u64 emptyLoopSamples = 1'000'000;
};

struct RunResult {
    std::vector<u32> ticks;
    u64 blockSize = 1;
    u64 updatesReplayed = 0;
    u64 warmupSamples = 0;
    f64 emptyLoopMedianTicks = 0;
    u64 allocsStart = 0;
    u64 allocsAfterWarmup = 0;
    u64 allocsEnd = 0;
};

template<class Book>
inline void CrossCheck(Book& book, const Update& u) {
#ifndef NDEBUG
    if constexpr (requires { book.DebugLookup(u.id); }) {
        if (u.type != MsgType::Add) {
            auto rec = book.DebugLookup(u.id);
            assert(rec.side == u.side);
            assert(rec.price == u.price);
        }
    }
#else
    (void)book;
    (void)u;
#endif
}

template<class Book>
inline void ReplayRange(Book& book, const Update* updates, u64 block,
                        u64 fromSample, u64 toSample, u32* out) {
    for (u64 s = fromSample; s < toSample; ++s) {
        const Update* u = updates + s * block;
        const Update* end = u + block;
#ifndef NDEBUG
        for (const Update* c = u; c < end; ++c)
            CrossCheck(book, *c);
#endif
        u64 t0 = ReadCounter();
        for (; u < end; ++u) {
            ApplyUpdate(book, *u);
            ObserveBook(book);
        }
        u64 t1 = ReadCounter();
        out[s] = static_cast<u32>(t1 - t0);
    }
}

inline f64 EmptyLoopMedianTicks(const Update* updates, u64 block,
                                u64 samples) {
    std::vector<u32> ticks(samples);
    u64 sink = 0;
    for (u64 s = 0; s < samples; ++s) {
        const Update* u = updates + s * block;
        const Update* end = u + block;
        u64 t0 = ReadCounter();
        for (; u < end; ++u) {
            sink += u->id;
            DoNotOptimize(sink);
        }
        u64 t1 = ReadCounter();
        ticks[s] = static_cast<u32>(t1 - t0);
    }
    DoNotOptimize(sink);
    if (ticks.empty())
        return 0;
    std::nth_element(ticks.begin(), ticks.begin() + ticks.size() / 2,
                     ticks.end());
    return static_cast<f64>(ticks[ticks.size() / 2]);
}

template<class Book>
RunResult Replay(Book& book, const Update* updates, u64 count,
                 const RunConfig& cfg) {
    RunResult r;
    r.blockSize = cfg.blockSize == 0 ? 1 : cfg.blockSize;
    u64 samples = count / r.blockSize;
    r.updatesReplayed = samples * r.blockSize;
    r.ticks.assign(samples, 0);

    u64 touch = 0;
    for (u64 i = 0; i < count; ++i)
        touch += updates[i].id ^ updates[i].timestamp;
    DoNotOptimize(touch);

    u64 emptySamples = std::min(samples, cfg.emptyLoopSamples);
    r.emptyLoopMedianTicks =
        EmptyLoopMedianTicks(updates, r.blockSize, emptySamples);

    r.warmupSamples = static_cast<u64>(static_cast<f64>(samples) *
                                       cfg.warmupFraction);
    if (r.warmupSamples > samples)
        r.warmupSamples = samples;

    r.allocsStart = AllocationCount();
    ReplayRange(book, updates, r.blockSize, 0, r.warmupSamples,
                r.ticks.data());
    r.allocsAfterWarmup = AllocationCount();
    ReplayRange(book, updates, r.blockSize, r.warmupSamples, samples,
                r.ticks.data());
    r.allocsEnd = AllocationCount();
    return r;
}

inline std::vector<f64> TicksToNsPerUpdate(const RunResult& r,
                                           f64 ticksPerNs) {
    std::vector<f64> ns(r.ticks.size());
    f64 scale = 1.0 / (ticksPerNs * static_cast<f64>(r.blockSize));
    for (std::size_t i = 0; i < ns.size(); ++i)
        ns[i] = static_cast<f64>(r.ticks[i]) * scale;
    return ns;
}

} // namespace ob

#endif
