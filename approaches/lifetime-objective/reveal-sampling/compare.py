#!/usr/bin/env python3
"""Paired whole-game comparison for factored-chance arms.

Usage:
  compare.py identity A.json B.json          exact per-game identity check
  compare.py table  label=path [label=path]  cohort summary + paired deltas
"""
import json
import random
import sys

FIELDS = ["seedHex", "score", "moves", "censored", "rises", "boardClears",
          "levelPoints", "clearPoints", "chainPoints", "numberedCleared",
          "coversRevealed", "maxChainDepth", "meanOccupiedCells"]


def load(path):
    with open(path) as handle:
        return json.load(handle)


def identity(a_path, b_path):
    a, b = load(a_path), load(b_path)
    ga = {g["seedHex"]: g for g in a["gamesDetail"]}
    gb = {g["seedHex"]: g for g in b["gamesDetail"]}
    assert set(ga) == set(gb), "cohorts differ"
    bad = 0
    for seed in sorted(ga):
        for field in FIELDS:
            if ga[seed][field] != gb[seed][field]:
                bad += 1
                print(f"  {seed} {field}: {ga[seed][field]} != {gb[seed][field]}")
    wa = sum(g["work"] for g in a["gamesDetail"])
    wb = sum(g["work"] for g in b["gamesDetail"])
    print(f"identity {a_path} vs {b_path}: {len(ga)} games, {bad} field mismatches, "
          f"logical work {wa} vs {wb} ({'equal' if wa == wb else 'DIFFERENT'})")
    return bad == 0 and wa == wb


def bootstrap_lower(values, alpha=0.05, draws=20000, seed=0xb0075eed):
    rng = random.Random(seed)
    n = len(values)
    means = []
    for _ in range(draws):
        means.append(sum(values[rng.randrange(n)] for _ in range(n)) / n)
    means.sort()
    pos = alpha * (len(means) - 1)
    low, high = int(pos), min(int(pos) + 1, len(means) - 1)
    w = pos - low
    return means[low] * (1 - w) + means[high] * w


def summarize(label, doc):
    g = doc["gamesDetail"]
    moves = sum(x["moves"] for x in g)
    cfg = doc["config"]
    return {
        "label": label,
        "games": len(g),
        "depth": cfg.get("depth"),
        "N": cfg.get("discSamples", cfg.get("chanceSamples")),
        "M": cfg.get("revealSamples", 1),
        "mean": doc["score"]["mean"],
        "median": doc["score"]["median"],
        "q25": doc["score"]["q25"],
        "min": doc["score"]["min"],
        "max": doc["score"]["max"],
        "sd": doc["score"]["sd"],
        "moves_mean": doc["moves"]["mean"],
        "clears": doc["numberedClearsPerMove"],
        "reveals": doc["coverRevealsPerMove"],
        "occ": doc["meanOccupiedCells"],
        "work": doc["workPerMove"],
        "censored": doc["censoredGames"],
        "identity_failures": doc["scoreIdentityFailures"],
        "wall": doc["wallSeconds"],
        "threads": doc["threads"],
        "scores": {x["seedHex"]: x["score"] for x in g},
        "movesmap": {x["seedHex"]: x["moves"] for x in g},
        "millions": sum(1 for x in g if x["score"] >= 1_000_000),
        "total_moves": moves,
        "cfg": cfg,
    }


def table(specs):
    arms = []
    for spec in specs:
        label, path = spec.split("=", 1)
        arms.append(summarize(label, load(path)))
    print("| arm | depth | N | M | mean | median | Q25 | min | max | sd | moves | clears/move | reveals/move | occupied | work/move | cens | >=1M |")
    print("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    for a in arms:
        print(f"| {a['label']} | {a['depth']} | {a['N']} | {a['M']} | {a['mean']:,.0f} | {a['median']:,.0f} | "
              f"{a['q25']:,.0f} | {a['min']:,.0f} | {a['max']:,.0f} | {a['sd']:,.0f} | {a['moves_mean']:.2f} | "
              f"{a['clears']:.4f} | {a['reveals']:.4f} | {a['occ']:.2f} | {a['work']:,.0f} | {a['censored']} | {a['millions']} |")
    print()
    print("| comparison | d score | 95% lower | d moves | d clears/move | d reveals/move | W-T-L |")
    print("| --- | ---: | ---: | ---: | ---: | ---: | :---: |")
    base = arms[0]
    for a in arms[1:]:
        seeds = sorted(set(base["scores"]) & set(a["scores"]))
        diffs = [a["scores"][s] - base["scores"][s] for s in seeds]
        mdiffs = [a["movesmap"][s] - base["movesmap"][s] for s in seeds]
        w = sum(1 for d in diffs if d > 0)
        t = sum(1 for d in diffs if d == 0)
        l = sum(1 for d in diffs if d < 0)
        print(f"| {a['label']} - {base['label']} | {sum(diffs)/len(diffs):+,.0f} | "
              f"{bootstrap_lower(diffs):+,.0f} | {sum(mdiffs)/len(mdiffs):+.2f} | "
              f"{a['clears']-base['clears']:+.4f} | {a['reveals']-base['reveals']:+.4f} | {w}-{t}-{l} |")
    print()
    for a in arms:
        c = a["cfg"]
        if "decisionsBelowTargetDepth" in c:
            print(f"{a['label']}: decisions {c['decisions']}, belowTargetDepth "
                  f"{c['decisionsBelowTargetDepth']}, workLimitEvents {c['workLimitEvents']}, "
                  f"minCompletedDepth {c['minCompletedDepth']}, maxDecisionWork {c['maxDecisionWork']:,}, "
                  f"maxWork {c['maximumWork']:,}, worstCaseWork {c['worstCaseWork']:,}, "
                  f"cache {c['maximumCacheEntries']:,} (worst case {c['worstCaseCacheEntries']:,}), "
                  f"identityFailures {a['identity_failures']}, wall {a['wall']:.0f}s @ {a['threads']}t")
        else:
            print(f"{a['label']}: (legacy artifact) identityFailures {a['identity_failures']}, "
                  f"wall {a['wall']:.0f}s @ {a['threads']}t, maxWork {c.get('maximumWork'):,}")


if __name__ == "__main__":
    if sys.argv[1] == "identity":
        sys.exit(0 if identity(sys.argv[2], sys.argv[3]) else 1)
    table(sys.argv[2:])
