#!/bin/sh
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
RUNS=${RUNS:-5}
SUMMARY=${SUMMARY:-results/summary.csv}
./scripts/bench_env.sh || true
for feed in results/feed_itch_AAPL.bin results/feed_s1.bin; do
    tag=$(basename "$feed" .bin | sed 's/^feed_//')
    for spec in "ref:ref:" "map_seq:map:" "map_rand:map:--scramble 4000000" "vector:vector:" "reverse_lb:reverse_lb:" "reverse_branchless:reverse_branchless:" "reverse_linear:reverse_linear:"; do
        name=${spec%%:*}
        rest=${spec#*:}
        book=${rest%%:*}
        extra=${rest#*:}
        echo "=== $name on $tag ==="
        python3 scripts/repeat_runs.py --runs "$RUNS" ./build/ob_bench --feed "$feed" --book "$book" --name "${name}_${tag}" --summary "" --hist /dev/null $extra | grep -E "^ *median|spread"
        ./build/ob_bench --feed "$feed" --book "$book" --name "${name}_${tag}" --summary "$SUMMARY" --hist "results/hist_${name}_${tag}.csv" --repeat 5 --prefault 2000000 $extra | grep -E "ns per update|allocations|WARN|FAIL" | cut -c1-200
    done
done
python3 scripts/plot_hist.py "std::map (randomized allocs)=results/hist_map_rand_itch_AAPL.csv" "std::vector=results/hist_vector_itch_AAPL.csv" "reverse vector=results/hist_reverse_lb_itch_AAPL.csv" "reverse branchless=results/hist_reverse_branchless_itch_AAPL.csv" "reverse linear=results/hist_reverse_linear_itch_AAPL.csv" --out results/m2_real.png --title "OrderBook Latency Distribution, AAPL 2019-12-30"
python3 scripts/plot_hist.py "std::map (randomized allocs)=results/hist_map_rand_s1.csv" "std::vector=results/hist_vector_s1.csv" "reverse vector=results/hist_reverse_lb_s1.csv" "reverse branchless=results/hist_reverse_branchless_s1.csv" "reverse linear=results/hist_reverse_linear_s1.csv" --out results/m2_synthetic.png --title "OrderBook Latency Distribution, synthetic seed 1"
python3 scripts/plot_hist.py "std::map=results/hist_map_seq_itch_AAPL.csv" "std::map (randomized allocs)=results/hist_map_rand_itch_AAPL.csv" --out results/m2_map_seq_vs_rand.png --title "OrderBookMap latencies, sequential vs randomized allocation"
