#!/usr/bin/env python3
import argparse
import csv
import sys
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

PALETTE = ["#8fc3ea", "#3fa27a", "#f3e35a", "#e07b7b", "#a58cd6", "#7a7a7a"]


def main():
    ap = argparse.ArgumentParser(description="median latency against new level rate")
    ap.add_argument("--summary", default="results/summary.csv")
    ap.add_argument("--out", default="results/m2_sweep.png")
    ap.add_argument("--title", default="Median latency vs new level rate")
    args = ap.parse_args()

    series = defaultdict(dict)
    with open(args.summary, newline="") as f:
        for row in csv.DictReader(f):
            name = row["book"]
            if "@" not in name:
                continue
            book, rate = name.split("@", 1)
            series[book][float(rate)] = float(row["median"])
    if not series:
        print("no sweep rows (book names of the form name@rate) in", args.summary)
        return 1

    fig, ax = plt.subplots(figsize=(9, 5.2))
    for i, (book, pts) in enumerate(sorted(series.items())):
        xs = sorted(pts)
        ax.plot([x * 100 for x in xs], [pts[x] for x in xs], marker="o",
                color=PALETTE[i % len(PALETTE)], label=book)
    ax.set_xlabel("new level rate (% of adds)")
    ax.set_ylabel("median ns per update")
    ax.set_title(args.title)
    ax.legend(loc="upper left")
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)

    if "map" in series:
        vectors = {b: p for b, p in series.items() if b != "map" and b != "ref"}
        rates = sorted(set.intersection(*(set(p) for p in vectors.values())) & set(series["map"]))
        crossover = None
        for r in rates:
            best_vec = min(p[r] for p in vectors.values())
            if series["map"][r] < best_vec:
                crossover = r
                break
        if crossover is None:
            print("no crossover in range: the fastest vector variant beats the map at every rate")
        else:
            print(f"crossover: map beats every vector variant from new level rate {crossover * 100:.0f}%")
            ax.axvline(crossover * 100, color="black", linestyle=":", linewidth=1)
    fig.tight_layout()
    fig.savefig(args.out, dpi=120, facecolor="white")
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
