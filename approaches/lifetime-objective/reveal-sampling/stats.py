#!/usr/bin/env python3
"""Full cohort rows and paired whole-game deltas for the finding-09 arms.

compare.py already prints a summary table; this prints the superset the
benchmark contract asks for (adds Q25 moves and the bound diagnostics to the
arm row, and takes an explicit comparator per comparison rather than always
using the first arm).

Usage:
  stats.py rows    label=path ...
  stats.py delta   candidate=path comparator=path ...   (pairs, in order)
  stats.py partial candidate=path comparator=path ...   (pairs, in order)

`delta` requires the two cohorts to hold exactly the same games and refuses
anything else, which is the right rule for two finished arms.  `partial` pairs
on the intersection of the two seed sets instead, so an arm that is still
running can be compared honestly against a finished comparator; it prints the
paired n and both subset means so the reader can never mistake the subset for a
full cohort.
"""
import json
import random
import sys


def load(path):
    with open(path) as handle:
        return json.load(handle)


def bootstrap_lower(values, alpha=0.05, draws=20000, seed=0xB0075EED):
    rng = random.Random(seed)
    n = len(values)
    means = []
    for _ in range(draws):
        means.append(sum(values[rng.randrange(n)] for _ in range(n)) / n)
    means.sort()
    pos = alpha * (len(means) - 1)
    low = int(pos)
    high = min(low + 1, len(means) - 1)
    w = pos - low
    return means[low] * (1 - w) + means[high] * w


def rows(specs):
    print("| arm | depth | N | M | games | mean | median | Q25 | min | max | sd | "
          "moves mean | moves Q25 | cens | clears/mv | reveals/mv | occupied | work/mv | >=1M |")
    print("| --- |" + " ---: |" * 18)
    for spec in specs:
        label, path = spec.split("=", 1)
        d = load(path)
        c = d["config"]
        g = d["gamesDetail"]
        print(f"| {label} | {c['depth']} | {c.get('discSamples', c.get('chanceSamples'))} | "
              f"{c.get('revealSamples', 1)} | {d['games']} | {d['score']['mean']:,.0f} | "
              f"{d['score']['median']:,.0f} | {d['score']['q25']:,.0f} | {d['score']['min']:,.0f} | "
              f"{d['score']['max']:,.0f} | {d['score']['sd']:,.0f} | {d['moves']['mean']:.2f} | "
              f"{d['moves']['q25']:.2f} | {d['censoredGames']} | {d['numberedClearsPerMove']:.4f} | "
              f"{d['coverRevealsPerMove']:.4f} | {d['meanOccupiedCells']:.2f} | {d['workPerMove']:,.0f} | "
              f"{sum(1 for x in g if x['score'] >= 1_000_000)} |")
    print()
    print("| arm | decisions | below target depth | work-limit events | min completed depth | "
          "max work in one decision | bound | headroom | identity failures |")
    print("| --- |" + " ---: |" * 8)
    for spec in specs:
        label, path = spec.split("=", 1)
        d = load(path)
        c = d["config"]
        if "decisionsBelowTargetDepth" not in c:
            print(f"| {label} | (legacy artifact, no bound diagnostics) | | | | | "
                  f"{c.get('maximumWork', 0):,} | | {d['scoreIdentityFailures']} |")
            continue
        print(f"| {label} | {c['decisions']:,} | {c['decisionsBelowTargetDepth']} | "
              f"{c['workLimitEvents']} | {c['minCompletedDepth']} | {c['maxDecisionWork']:,} | "
              f"{c['maximumWork']:,} | {c['maximumWork']/max(1,c['maxDecisionWork']):.2f}x | "
              f"{d['scoreIdentityFailures']} |")


def delta(specs):
    print("| comparison | d score | 95% lower bound | d moves | d clears/move | "
          "d reveals/move | d occupied | W-T-L |")
    print("| --- |" + " ---: |" * 6 + " :---: |")
    for i in range(0, len(specs), 2):
        alabel, apath = specs[i].split("=", 1)
        blabel, bpath = specs[i + 1].split("=", 1)
        a, b = load(apath), load(bpath)
        sa = {x["seedHex"]: x for x in a["gamesDetail"]}
        sb = {x["seedHex"]: x for x in b["gamesDetail"]}
        seeds = sorted(set(sa) & set(sb))
        assert len(seeds) == len(sa) == len(sb), f"cohorts differ: {len(seeds)}"
        diffs = [sa[s]["score"] - sb[s]["score"] for s in seeds]
        mdiffs = [sa[s]["moves"] - sb[s]["moves"] for s in seeds]
        w = sum(1 for x in diffs if x > 0)
        t = sum(1 for x in diffs if x == 0)
        l = sum(1 for x in diffs if x < 0)
        print(f"| {alabel} - {blabel} (n={len(seeds)}) | {sum(diffs)/len(diffs):+,.0f} | "
              f"{bootstrap_lower(diffs):+,.0f} | {sum(mdiffs)/len(mdiffs):+.2f} | "
              f"{a['numberedClearsPerMove']-b['numberedClearsPerMove']:+.4f} | "
              f"{a['coverRevealsPerMove']-b['coverRevealsPerMove']:+.4f} | "
              f"{a['meanOccupiedCells']-b['meanOccupiedCells']:+.2f} | {w}-{t}-{l} |")


def partial(specs):
    """Paired delta over the seeds the two cohorts share.

    Used only when one arm is incomplete.  A partial arm's finished games are
    not a random subset of its cohort -- chunked runners finish whole seed
    blocks in order and, within a block, short games first -- so the subset mean
    of the candidate is not an estimate of its 64-game mean.  The paired delta
    on the shared seeds is still a fair paired comparison over those seeds.
    """
    print("| comparison | paired n | d score | 95% lower bound | d moves | W-T-L | "
          "candidate subset mean | comparator subset mean |")
    print("| --- |" + " ---: |" * 4 + " :---: |" + " ---: |" * 2)
    for i in range(0, len(specs), 2):
        alabel, apath = specs[i].split("=", 1)
        blabel, bpath = specs[i + 1].split("=", 1)
        a, b = load(apath), load(bpath)
        sa = {x["seedHex"]: x for x in a["gamesDetail"]}
        sb = {x["seedHex"]: x for x in b["gamesDetail"]}
        seeds = sorted(set(sa) & set(sb))
        if not seeds:
            raise SystemExit(f"{alabel} and {blabel} share no seed")
        diffs = [sa[s]["score"] - sb[s]["score"] for s in seeds]
        mdiffs = [sa[s]["moves"] - sb[s]["moves"] for s in seeds]
        w = sum(1 for x in diffs if x > 0)
        t = sum(1 for x in diffs if x == 0)
        l = sum(1 for x in diffs if x < 0)
        ma = sum(sa[s]["score"] for s in seeds) / len(seeds)
        mb = sum(sb[s]["score"] for s in seeds) / len(seeds)
        print(f"| {alabel} - {blabel} | {len(seeds)} of {max(len(sa), len(sb))} | "
              f"{sum(diffs)/len(diffs):+,.0f} | {bootstrap_lower(diffs):+,.0f} | "
              f"{sum(mdiffs)/len(mdiffs):+.2f} | {w}-{t}-{l} | {ma:,.0f} | {mb:,.0f} |")


if __name__ == "__main__":
    if sys.argv[1] == "rows":
        rows(sys.argv[2:])
    elif sys.argv[1] == "delta":
        delta(sys.argv[2:])
    elif sys.argv[1] == "partial":
        partial(sys.argv[2:])
    else:
        raise SystemExit(__doc__)
