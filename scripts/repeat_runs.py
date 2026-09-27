#!/usr/bin/env python3
import argparse
import re
import statistics
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser(
        description="launch ob_bench several times and report the spread"
    )
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--max-spread", type=float, default=5.0)
    ap.add_argument(
        "bench",
        nargs=argparse.REMAINDER,
        help="ob_bench binary followed by its arguments",
    )
    args = ap.parse_args()
    if not args.bench:
        ap.error("bench command required")
    cmd = [a for a in args.bench if a != "--"]

    medians = []
    p99s = []
    rc = 0
    for i in range(args.runs):
        out = subprocess.run(cmd, capture_output=True, text=True)
        if out.returncode != 0:
            rc = out.returncode
        m = re.search(r"median=([0-9.]+)", out.stdout)
        p = re.search(r"p99=([0-9.]+)", out.stdout)
        if not m:
            print(out.stdout)
            print(out.stderr, file=sys.stderr)
            print(f"run {i + 1}: no median found", file=sys.stderr)
            return 1
        medians.append(float(m.group(1)))
        p99s.append(float(p.group(1)) if p else 0.0)
        print(
            f"run {i + 1}: median={medians[-1]:.3f} ns p99={p99s[-1]:.3f} ns"
            f" rc={out.returncode}"
        )

    mom = statistics.median(medians)
    lo, hi = min(medians), max(medians)
    spread = (hi - lo) / mom * 100 if mom > 0 else float("inf")
    print(f"median of medians: {mom:.3f} ns")
    print(f"min {lo:.3f} max {hi:.3f} spread {spread:.2f}%")
    ok = spread <= args.max_spread
    print("spread ok" if ok else f"spread exceeds {args.max_spread}%")
    return rc if rc else (0 if ok else 4)


if __name__ == "__main__":
    sys.exit(main())
