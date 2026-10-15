#!/bin/sh
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
SUMMARY=${SUMMARY:-results/summary.csv}
COUNT=${COUNT:-2000000}
mkdir -p results/sweep
for rate in 0 0.02 0.05 0.10 0.15 0.20 0.30; do
    feed="results/sweep/feed_r${rate}.bin"
    [ -f "$feed" ] || ./build/ob_genfeed --out "$feed" --count "$COUNT" --new-level-rate "$rate" | tail -1
    for book in map vector reverse_lb reverse_branchless reverse_linear; do
        extra=""
        [ "$book" = "map" ] && extra="--scramble 4000000"
        ./build/ob_bench --feed "$feed" --book "$book" --name "${book}@${rate}" --summary "$SUMMARY" --hist /dev/null --repeat 5 --prefault 2000000 $extra | grep -E "ns per update" | sed "s/^/${book}@${rate}: /" | cut -c1-120
    done
done
python3 scripts/plot_sweep.py --summary "$SUMMARY" --out results/m2_sweep.png
