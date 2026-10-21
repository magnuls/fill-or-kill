#!/bin/sh
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
FEED=${FEED:-results/feed_itch_AAPL.bin}
OUT=${OUT:-results/m3_probe.csv}
PEAK=$(./build/ob_bench --feed "$FEED" --book table --table identity --summary "" --hist /dev/null --block 256 --repeat 1 | sed -n 's/.*peak live orders \([0-9]*\).*/\1/p')
echo "table,reserve,capacity,size_at_peak,load,avg_probe,max_probe,median_ns" > "$OUT"
for table in identity mix; do
    for mult in 1 2 4 8 16; do
        reserve=$((PEAK * mult))
        line=$(./build/ob_bench --feed "$FEED" --book table --table "$table" --reserve "$reserve" --summary "" --hist /dev/null --repeat 5 --prefault 2000000)
        stats=$(echo "$line" | grep '^table:' | sed -E 's/.*capacity=([0-9]+), at peak live \(update [0-9]+\): size=([0-9]+) load=([0-9.e-]+) avg_probe=([0-9.e-]+) max_probe=([0-9]+)/\1,\2,\3,\4,\5/')
        median=$(echo "$line" | grep 'ns per update' | sed -E 's/.*median=([0-9.]+).*/\1/')
        echo "$table,$reserve,$stats,$median" | tee -a "$OUT"
    done
done
