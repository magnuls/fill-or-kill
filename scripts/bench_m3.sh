#!/bin/sh
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
RUNS=${RUNS:-5}
SUMMARY=${SUMMARY:-results/summary.csv}
./scripts/bench_env.sh || true
for feed in results/feed_itch_AAPL.bin results/feed_s1.bin; do
    tag=$(basename "$feed" .bin | sed 's/^feed_//')
    for book in table reverse_branchless reverse_linear map; do
        for table in std identity mix; do
            name="${book}_${table}"
            echo "=== $name on $tag ==="
            python3 scripts/repeat_runs.py --runs "$RUNS" ./build/ob_bench --feed "$feed" --book "$book" --table "$table" --name "${name}_${tag}" --summary "" --hist /dev/null | grep -E "^ *median|spread"
            ./build/ob_bench --feed "$feed" --book "$book" --table "$table" --name "${name}_${tag}" --summary "$SUMMARY" --hist "results/hist_${name}_${tag}.csv" --repeat 5 --prefault 2000000 | grep -E "ns per update|allocations|^table|WARN|FAIL" | cut -c1-200
        done
    done
done
feed=results/feed_itch_AAPL.bin
for table in identity mix; do
    name="table_${table}_stride65536"
    echo "=== $name on itch_AAPL ==="
    ./build/ob_bench --feed "$feed" --book table --table "$table" --id-stride 65536 --name "${name}_itch_AAPL" --summary "$SUMMARY" --hist /dev/null --repeat 5 --prefault 2000000 | grep -E "ns per update|allocations|^table|WARN|FAIL" | cut -c1-200
done
for table in identity mix; do
    name="reverse_branchless_${table}_prefetch"
    echo "=== $name on itch_AAPL ==="
    python3 scripts/repeat_runs.py --runs "$RUNS" ./build/ob_bench --feed "$feed" --book reverse_branchless --table "$table" --prefetch --name "${name}_itch_AAPL" --summary "" --hist /dev/null | grep -E "^ *median|spread"
    ./build/ob_bench --feed "$feed" --book reverse_branchless --table "$table" --prefetch --name "${name}_itch_AAPL" --summary "$SUMMARY" --hist "results/hist_${name}_itch_AAPL.csv" --repeat 5 --prefault 2000000 | grep -E "ns per update|allocations|^table|WARN|FAIL" | cut -c1-200
done
python3 scripts/plot_hist.py "std::unordered_map=results/hist_table_std_itch_AAPL.csv" "flat, identity hash=results/hist_table_identity_itch_AAPL.csv" "flat, mix hash=results/hist_table_mix_itch_AAPL.csv" --out results/m3_lookup.png --title "Order table alone, AAPL 2019-12-30"
python3 scripts/plot_hist.py "reverse branchless, std::unordered_map=results/hist_reverse_branchless_std_itch_AAPL.csv" "reverse branchless, flat identity=results/hist_reverse_branchless_identity_itch_AAPL.csv" "reverse linear, std::unordered_map=results/hist_reverse_linear_std_itch_AAPL.csv" "reverse linear, flat identity=results/hist_reverse_linear_identity_itch_AAPL.csv" --out results/m3_e2e_real.png --title "OrderBook Latency Distribution by order table, AAPL 2019-12-30"
python3 scripts/plot_hist.py "reverse branchless, std::unordered_map=results/hist_reverse_branchless_std_s1.csv" "reverse branchless, flat identity=results/hist_reverse_branchless_identity_s1.csv" "reverse branchless, flat mix=results/hist_reverse_branchless_mix_s1.csv" --out results/m3_e2e_synthetic.png --title "OrderBook Latency Distribution by order table, synthetic seed 1"
python3 scripts/plot_hist.py "flat identity=results/hist_reverse_branchless_identity_itch_AAPL.csv" "flat identity, prefetch next=results/hist_reverse_branchless_identity_prefetch_itch_AAPL.csv" --out results/m3_prefetch.png --title "reverse branchless, flat identity table, prefetch of the next update"
