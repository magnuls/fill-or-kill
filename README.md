# Order Book Latency Benchmark

- Order book implementations, fed Nasdaq ITCH data and measured per update with the cycle counter.

![AAPLE latency distribution](results/m2_real.png)

## Overview

Heavily inspired by [David Gross CppCon talk](https://www.youtube.com/watch?v=sX2nF1fW7kI)

- Feeds
  - `ob_genfeed` writes a synthetic and customizable feed for benchmarking
  - `ob_itch` converts a Nasdaq ITCH 5.0 day file into one feed per symbol
  - `ob_validate` checks that feed makes sense before it is used for benchmarking
- Books (`ob/`)
  - `ReferenceBook`: a slow but correct book used by ob_diff to check against our fast books
  - `MapBook`: `std::map` map<price, volume> for bids and asks and a
     hash table of orders. Each order holds an iterator to its price level, modifying or deleting O(1) on average
  - `VectorBook`: sorted vector of price levels with best price at front of vector. The search algo used for finding a price level is a template parameter (lower_bound, branchless, linear)
  - `ReverseVectorBook`: same as VectorBook but best price is located at the back
  - `StubBook`: a book that does nothing but increment a counter. Benchmarking it measures the benchmark's own overhead and noise.
- Benchmark (`ob_bench`)
  - times blocks of updates with the CPU cycle counter, drops first ~1% of samples, counts total heap allocation calls
  - Pre-grow heap to avoid page faults, optionally scramble which blocks you allocate, ASLR turned off (memory layout is the same every run), run five times, and if the difference between medians > 5% we flag the run as invalid
- Correctness
  - `ob_diff` feeds the same data feed into the ReferenceBook and a fast book at the same time, compares the two after every message, and stops at a difference
  - `ob_tests` unit tests for every book, the ITCH parser, the feed validator, and ob_diff. Includes one broken book to make sure the test works
- Results, 1.6M updates, M4 Max (My Chip), clang 17 `-O2`

  | book | median ns | allocations per add |
  | --- | --- | --- |
  | ref | 81.8 | 2.20 |
  | map | 40.5 | 1.21 |
  | map, randomized heap | 46.1 | 1.21 |
  | forward vector | 106.1 | 0.99 |
  | reverse vector, lower_bound | 51.6 | 0.99 |
  | reverse vector, branchless | 50.8 | 0.99 |
  | reverse vector, linear | 72.9 | 0.99 |

  ![Map with sequential versus randomized allocation](results/m2_map_seq_vs_rand.png)

  ![Median against new level rate](results/m2_sweep.png)

- EXPLANATION:
  - Putting the best price at the back of the vector halves the forward vector's latency cost
  - Linear search is the worst on this data 

The pro-rata matching engine lives in `matching/` and still builds as `lob` and `lob_tests`.

## Build and run

```sh
cmake -S . -B build && cmake --build build
ctest --test-dir build

./scripts/fetch_itch.sh                                   # Nasdaq sample day into data/
./build/ob_itch --in data/12302019.NASDAQ_ITCH50.gz --symbols AAPL --out-dir results
./build/ob_bench --feed results/feed_itch_AAPL.bin --book reverse_branchless --hist results/hist.csv
./scripts/bench_all.sh                                    # the full matrix and the three charts
```

## TODO

custom open-addressing order table, zero allocations in the timed region.
level distribution instrument, measure the real search trip length, retune the synthetic generator from measured distributions.
Micro-optimizations from the talk, each with a before and after histogram.
Re-measure on GCC x86 Linux where branchless search is expected to beat `lower_bound`.
Bring every configuration inside the 5% spread gate.
Matching engine: FIFO price-time book, IOC / FOK / AON order types, order modification, strategies.
