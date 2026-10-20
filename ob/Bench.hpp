#ifndef OB_BENCH_HPP
#define OB_BENCH_HPP

#include "ob/OrderTable.hpp"
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
    f64 ticks_per_ns = 0.0;
    f64 resolution_ns = 0.0;
    std::string source;
    bool invariant = false;
    bool coarse = false;
    std::string invariant_note;
    std::string cpu_model;
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
    info.invariant_note = info.invariant
                              ? "constant_tsc and nonstop_tsc present"
                              : "constant_tsc or nonstop_tsc missing";
    info.source = "rdtsc";
#elif defined(__x86_64__)
    info.invariant = true;
    info.invariant_note = "non linux x86: invariant tsc assumed";
    info.source = "rdtsc";
#elif defined(__aarch64__)
    info.invariant = true;
    info.invariant_note = "arm64 generic timer is fixed frequency";
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

}

inline CounterInfo CalibrateCounter() {
    CounterInfo info;
    detail::CheckInvariant(info);
    info.cpu_model = detail::ReadCpuModel();

    const u64 spin_ns = 100'000'000ull;
    u64 t0 = MonotonicNs();
    u64 c0 = ReadCounter();
    u64 t1 = t0;
    while (t1 - t0 < spin_ns)
        t1 = MonotonicNs();
    u64 c1 = ReadCounter();
    info.ticks_per_ns =
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
        u64 best_run = 0;
        u64 run = 1;
        for (std::size_t i = 1; i <= deltas.size(); ++i) {
            if (i < deltas.size() && deltas[i] == deltas[i - 1]) {
                ++run;
                continue;
            }
            if (run > best_run) {
                best_run = run;
                granularity = deltas[i - 1];
            }
            run = 1;
        }
    }
    info.resolution_ns =
        static_cast<f64>(granularity) / info.ticks_per_ns;
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
    f64 mode_value = 0;
    f64 mode_fraction = 0;
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

    std::size_t best_run = 0;
    f64 best_val = sorted.front();
    std::size_t run = 1;
    for (std::size_t i = 1; i < sorted.size(); ++i) {
        if (sorted[i] == sorted[i - 1]) {
            ++run;
        } else {
            if (run > best_run) {
                best_run = run;
                best_val = sorted[i - 1];
            }
            run = 1;
        }
    }
    if (run > best_run) {
        best_run = run;
        best_val = sorted.back();
    }
    s.mode_value = best_val;
    s.mode_fraction =
        static_cast<f64>(best_run) / static_cast<f64>(sorted.size());
    return s;
}

constexpr u32 HISTOGRAM_BUCKETS = 500;

struct Histogram {
    std::array<u64, HISTOGRAM_BUCKETS + 1> buckets{};
    u64 total = 0;
    f64 bucket_ns = 1.0;

    f64 Median() const {
        if (total == 0)
            return 0;
        u64 half = (total + 1) / 2;
        u64 acc = 0;
        for (u32 i = 0; i <= HISTOGRAM_BUCKETS; ++i) {
            acc += buckets[i];
            if (acc >= half)
                return static_cast<f64>(i) * bucket_ns;
        }
        return static_cast<f64>(HISTOGRAM_BUCKETS) * bucket_ns;
    }
};

inline Histogram BuildHistogram(std::span<const f64> samples,
                                f64 bucket_ns = 1.0) {
    Histogram h;
    h.bucket_ns = bucket_ns > 0 ? bucket_ns : 1.0;
    for (f64 v : samples) {
        if (v < 0)
            v = 0;
        f64 scaled = v / h.bucket_ns;
        u64 b = scaled >= static_cast<f64>(HISTOGRAM_BUCKETS)
                    ? HISTOGRAM_BUCKETS
                    : static_cast<u64>(scaled);
        ++h.buckets[b];
    }
    h.total = samples.size();
    return h;
}

struct RunMeta {
    std::string book;
    std::string source;
    u64 seed = 0;
    u64 update_count = 0;
    f64 new_level_rate = 0;
    std::string compiler;
    std::string flags;
    std::string cpu_model;
    f64 ticks_per_ns = 0;
    f64 resolution_ns = 0;
    u64 block_size = 1;
    u64 repeat = 1;
    u64 scramble = 0;
    f64 empty_loop_ns = 0;
    u64 warmup_excluded = 0;
    u64 allocs_in_timed_region = 0;
    std::string git_commit;
    std::string timestamp;
};

inline std::string SummaryCsvHeader() {
    return "book,source,seed,update_count,new_level_rate,compiler,"
           "flags,"
           "cpu_model,ticks_per_ns,resolution_ns,block_size,repeat,"
           "scramble,empty_loop_ns,warmup_excluded,allocs_in_timed_"
           "region,"
           "git_commit,timestamp,count,min,median,p75,p90,p99,p999,"
           "p9999,max,mean,stddev";
}

inline std::string SummaryCsvRow(const RunMeta& m, const Summary& s) {
    std::ostringstream os;
    os.precision(6);
    os << std::fixed;
    os << detail::Quote(m.book) << ',' << detail::Quote(m.source)
       << ',' << m.seed << ',' << m.update_count << ','
       << m.new_level_rate << ',' << detail::Quote(m.compiler) << ','
       << detail::Quote(m.flags) << ',' << detail::Quote(m.cpu_model)
       << ',' << m.ticks_per_ns << ',' << m.resolution_ns << ','
       << m.block_size << ',' << m.repeat << ',' << m.scramble << ','
       << m.empty_loop_ns << ',' << m.warmup_excluded << ','
       << m.allocs_in_timed_region << ','
       << detail::Quote(m.git_commit) << ','
       << detail::Quote(m.timestamp) << ',' << s.count << ',' << s.min
       << ',' << s.median << ',' << s.p75 << ',' << s.p90 << ','
       << s.p99 << ',' << s.p999 << ',' << s.p9999 << ',' << s.max
       << ',' << s.mean << ',' << s.stddev;
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
    for (u32 i = 0; i <= HISTOGRAM_BUCKETS; ++i)
        out << static_cast<f64>(i) * h.bucket_ns << ','
            << h.buckets[i] << '\n';
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
    u64 block_size = 1;
    f64 warmup_fraction = 0.01;
    u64 empty_loop_samples = 1'000'000;
    u64 repeat = 1;
    u64 order_reserve = DEFAULT_ORDER_RESERVE;
    bool prefetch = false;
};

struct RunResult {
    std::vector<u32> ticks;
    u64 block_size = 1;
    u64 updates_replayed = 0;
    u64 warmup_samples = 0;
    f64 empty_loop_median_ticks = 0;
    u64 allocs_start = 0;
    u64 allocs_after_warmup = 0;
    u64 allocs_end = 0;
    u64 passes = 1;
    std::vector<f64> pass_median_ticks;
    u64 allocs_constructing_books = 0;
};

struct LivePeak {
    u64 orders = 0;
    u64 index = 0;
};

inline LivePeak FindLivePeak(const Update* updates, u64 count) {
    i64 live = 0;
    i64 peak = 0;
    LivePeak out;
    for (u64 i = 0; i < count; ++i) {
        const Update& u = updates[i];
        if (u.type == MsgType::Add)
            ++live;
        else if (u.type == MsgType::Delete || u.volume.lots <= 0)
            --live;
        if (live > peak) {
            peak = live;
            out.index = i;
        }
    }
    out.orders = static_cast<u64>(peak);
    return out;
}

inline u64 PeakLiveOrders(const Update* updates, u64 count) {
    return FindLivePeak(updates, count).orders;
}

inline void PrefaultHeap(u64 blocks) {
    std::vector<void*> held;
    held.reserve(blocks);
    for (u64 i = 0; i < blocks; ++i)
        held.push_back(::operator new(32 + (i % 4) * 16));
    for (void* p : held)
        ::operator delete(p);
}

inline void ScrambleHeap(u64 blocks, u64 seed = 7) {
    static std::vector<void*> kept;
    std::vector<void*> all;
    all.reserve(blocks);
    u64 state = seed ^ 0x9e3779b97f4a7c15ull;
    for (u64 i = 0; i < blocks; ++i) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        all.push_back(::operator new(48 + (state % 3) * 16));
    }
    for (u64 i = blocks; i > 1; --i) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        std::swap(all[i - 1], all[state % i]);
    }
    for (u64 i = 0; i < blocks; ++i) {
        if (i % 2 == 0)
            ::operator delete(all[i]);
        else
            kept.push_back(all[i]);
    }
}

inline f64 MedianOf(std::vector<u32> v) {
    if (v.empty())
        return 0;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return static_cast<f64>(v[v.size() / 2]);
}

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

template<bool PREFETCH, class Book>
inline void ReplayRangeImpl(Book& book, const Update* updates,
                            u64 block, u64 from_sample, u64 to_sample,
                            u32* out) {
    for (u64 s = from_sample; s < to_sample; ++s) {
        const Update* u = updates + s * block;
        const Update* end = u + block;
        u64 t0 = ReadCounter();
        for (; u < end; ++u) {
#ifndef NDEBUG
            CrossCheck(book, *u);
#endif
            if constexpr (PREFETCH) {
                if (u + 1 != end)
                    book.Prefetch(u[1].id);
            }
            ApplyUpdate(book, *u);
            ObserveBook(book);
        }
        u64 t1 = ReadCounter();
        out[s] = static_cast<u32>(t1 - t0);
    }
}

template<class Book>
inline void ReplayRange(Book& book, const Update* updates, u64 block,
                        u64 from_sample, u64 to_sample, u32* out,
                        bool prefetch = false) {
    if (prefetch)
        ReplayRangeImpl<true>(book, updates, block, from_sample,
                              to_sample, out);
    else
        ReplayRangeImpl<false>(book, updates, block, from_sample,
                               to_sample, out);
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
    r.block_size = cfg.block_size == 0 ? 1 : cfg.block_size;
    u64 samples = count / r.block_size;
    r.updates_replayed = samples * r.block_size;
    r.ticks.assign(samples, 0);

    u64 touch = 0;
    for (u64 i = 0; i < count; ++i)
        touch += updates[i].id ^ updates[i].timestamp;
    DoNotOptimize(touch);

    u64 empty_samples = std::min(samples, cfg.empty_loop_samples);
    r.empty_loop_median_ticks =
        EmptyLoopMedianTicks(updates, r.block_size, empty_samples);

    r.warmup_samples = static_cast<u64>(static_cast<f64>(samples) *
                                        cfg.warmup_fraction);
    if (r.warmup_samples > samples)
        r.warmup_samples = samples;

    r.allocs_start = AllocationCount();
    ReplayRange(book, updates, r.block_size, 0, r.warmup_samples,
                r.ticks.data(), cfg.prefetch);
    r.allocs_after_warmup = AllocationCount();
    ReplayRange(book, updates, r.block_size, r.warmup_samples,
                samples, r.ticks.data(), cfg.prefetch);
    r.allocs_end = AllocationCount();
    return r;
}

template<class Book>
RunResult ReplayPasses(const Update* updates, u64 count,
                       const RunConfig& cfg) {
    u64 passes = cfg.repeat == 0 ? 1 : cfg.repeat;
    u64 block = cfg.block_size == 0 ? 1 : cfg.block_size;
    u64 per_pass = count / block;
    RunResult r;
    r.block_size = block;
    r.passes = passes;
    r.updates_replayed = per_pass * block * passes;
    r.ticks.assign(per_pass * passes, 0);
    r.warmup_samples = static_cast<u64>(
        static_cast<f64>(per_pass * passes) * cfg.warmup_fraction);
    if (r.warmup_samples > per_pass)
        r.warmup_samples = per_pass;

    u64 touch = 0;
    for (u64 i = 0; i < count; ++i)
        touch += updates[i].id ^ updates[i].timestamp;
    DoNotOptimize(touch);

    u64 empty_samples = std::min(per_pass, cfg.empty_loop_samples);
    r.empty_loop_median_ticks =
        EmptyLoopMedianTicks(updates, block, empty_samples);

    r.pass_median_ticks.reserve(passes);
    std::vector<u32> scratch(per_pass);
    r.allocs_start = AllocationCount();
    for (u64 p = 0; p < passes; ++p) {
        u64 before_book = AllocationCount();
        Book book(cfg.order_reserve);
        if (p > 0)
            r.allocs_constructing_books +=
                AllocationCount() - before_book;
        u32* out = r.ticks.data() + p * per_pass;
        if (p == 0) {
            ReplayRange(book, updates, block, 0, r.warmup_samples,
                        out, cfg.prefetch);
            r.allocs_after_warmup = AllocationCount();
            ReplayRange(book, updates, block, r.warmup_samples,
                        per_pass, out, cfg.prefetch);
        } else {
            ReplayRange(book, updates, block, 0, per_pass, out,
                        cfg.prefetch);
        }
    }
    r.allocs_end = AllocationCount() - r.allocs_constructing_books;
    for (u64 p = 0; p < passes; ++p) {
        const u32* out = r.ticks.data() + p * per_pass;
        std::copy(out, out + per_pass, scratch.begin());
        r.pass_median_ticks.push_back(MedianOf(scratch));
    }
    return r;
}

inline std::vector<f64> Dither(std::span<const f64> samples,
                               f64 quantum, u64 seed) {
    std::vector<f64> out(samples.begin(), samples.end());
    if (quantum <= 0)
        return out;
    u64 state = seed ^ 0x9e3779b97f4a7c15ull;
    for (f64& v : out) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        f64 unit = static_cast<f64>(state >> 11) / 9007199254740992.0;
        v += unit * quantum;
    }
    return out;
}

inline std::vector<f64> TicksToNsPerUpdate(const RunResult& r,
                                           f64 ticks_per_ns) {
    std::vector<f64> ns(r.ticks.size());
    f64 scale = 1.0 / (ticks_per_ns * static_cast<f64>(r.block_size));
    for (std::size_t i = 0; i < ns.size(); ++i)
        ns[i] = static_cast<f64>(r.ticks[i]) * scale;
    return ns;
}

}

#endif
