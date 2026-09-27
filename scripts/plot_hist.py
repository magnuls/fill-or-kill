#!/usr/bin/env python3
import argparse
import csv
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

PALETTE = ["#8fc3ea", "#3fa27a", "#f3e35a", "#e07b7b", "#a58cd6"]


def read_hist(path):
    xs, ys = [], []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            xs.append(float(row["ns"]))
            ys.append(int(row["count"]))
    width = xs[1] - xs[0] if len(xs) > 1 else 1.0
    return xs[:-1], ys[:-1], ys[-1], width


def median_from_hist(xs, ys, overflow):
    half = (sum(ys) + overflow + 1) // 2
    acc = 0
    for x, y in zip(xs, ys):
        acc += y
        if acc >= half:
            return x
    return xs[-1] if xs else 0.0


def main():
    ap = argparse.ArgumentParser(description="overlay latency histograms")
    ap.add_argument("series", nargs="+", help="label=path/to/hist.csv")
    ap.add_argument("--out", default="results/hist.png")
    ap.add_argument("--xmin", type=float, default=None)
    ap.add_argument("--xmax", type=float, default=None)
    ap.add_argument("--title", default="OrderBook Latency Distribution")
    ap.add_argument("--log", action="store_true", help="log scale on y")
    args = ap.parse_args()

    fig, ax = plt.subplots(figsize=(9, 5.2))
    handles, lo, hi = [], None, 0.0
    for i, spec in enumerate(args.series):
        label, path = spec.split("=", 1) if "=" in spec else (spec, spec)
        xs, ys, overflow, width = read_hist(path)
        color = PALETTE[i % len(PALETTE)]
        med = median_from_hist(xs, ys, overflow)
        digits = 1 if width >= 1 else 3
        ax.bar(xs, ys, width=width, align="edge", alpha=0.55, color=color)
        line = ax.axvline(
            med,
            color=color,
            linestyle="--",
            linewidth=1.2,
            label=f"Median: {med:.{digits}f} ns",
        )
        handles += [Patch(facecolor=color, alpha=0.55, label=label), line]
        nonzero = [x for x, y in zip(xs, ys) if y > 0]
        if nonzero:
            lo = nonzero[0] if lo is None else min(lo, nonzero[0])
            hi = max(hi, nonzero[-1] + width)
        print(f"{label}: median {med:.{digits}f} ns, overflow {overflow}")
    lo = lo or 0.0
    xmin = args.xmin if args.xmin is not None else max(0.0, lo - 0.15 * (hi - lo))
    ax.set_xlim(xmin, args.xmax if args.xmax else hi + 0.05 * (hi - lo))
    if args.log:
        ax.set_yscale("log")
    ax.set_xlabel("Latency (ns)")
    ax.set_ylabel("Frequency")
    ax.set_title(args.title)
    ax.legend(handles=handles, loc="upper right", framealpha=0.95)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    fig.tight_layout()
    fig.savefig(args.out, dpi=120, facecolor="white")
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
