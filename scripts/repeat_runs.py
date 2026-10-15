#!/usr/bin/env python3
import argparse
import os
import re
import statistics
import subprocess
import sys


def spread(values):
    mid = statistics.median(values)
    return (max(values) - min(values)) / mid * 100 if mid > 0 else float("inf")


def main():
    ap = argparse.ArgumentParser(
        description="launch ob_bench several times and report the spread")
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--max-spread", type=float, default=5.0)
    ap.add_argument("--env", action="append", default=[],
                    help="KEY=VALUE set for every launch, repeatable")
    ap.add_argument("--repeat", type=int, default=5,
                    help="passes per launch, appended unless the command sets --repeat")
    ap.add_argument("--prefault", type=int, default=2000000,
                    help="heap blocks to prefault, appended unless the command sets --prefault")
    ap.add_argument("bench", nargs=argparse.REMAINDER,
                    help="ob_bench binary followed by its arguments")
    args = ap.parse_args()
    if not args.bench:
        ap.error("bench command required")
    cmd = [a for a in args.bench if a != "--"]
    if "--repeat" not in cmd:
        cmd += ["--repeat", str(args.repeat)]
    if "--prefault" not in cmd:
        cmd += ["--prefault", str(args.prefault)]
    print("command:", " ".join(cmd))
    env = dict(os.environ)
    for kv in args.env:
        key, _, value = kv.partition("=")
        env[key] = value

    stats = {"median": [], "mean": [], "p99": []}
    rc = 0
    for i in range(args.runs):
        out = subprocess.run(cmd, capture_output=True, text=True, env=env)
        if out.returncode != 0:
            rc = out.returncode
        line = next((l for l in out.stdout.splitlines()
                     if l.startswith("ns per update:")), "")
        found = {k: re.search(rf"\b{k}=([0-9.]+)", line) for k in stats}
        if not found["median"]:
            print(out.stdout)
            print(out.stderr, file=sys.stderr)
            print(f"run {i + 1}: no median found", file=sys.stderr)
            return 1
        for k, m in found.items():
            stats[k].append(float(m.group(1)) if m else 0.0)
        print(f"run {i + 1}: median={stats['median'][-1]:.3f} ns "
              f"mean={stats['mean'][-1]:.3f} ns p99={stats['p99'][-1]:.3f} ns "
              f"rc={out.returncode}")

    for k in ("median", "mean", "p99"):
        v = stats[k]
        print(f"{k:>6}: median of runs {statistics.median(v):.3f} ns, "
              f"min {min(v):.3f} max {max(v):.3f}, spread {spread(v):.2f}%")
    ok = spread(stats["median"]) <= args.max_spread
    print("spread ok" if ok else f"median spread exceeds {args.max_spread}%")
    return rc if rc else (0 if ok else 4)


if __name__ == "__main__":
    sys.exit(main())
