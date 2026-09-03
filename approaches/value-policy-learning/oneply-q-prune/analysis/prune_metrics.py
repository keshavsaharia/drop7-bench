#!/usr/bin/env python3
"""Score pruned-search decisions against exact values on the same roots.

  prune_metrics.py --results prune-d4.ndjson --truth-config exact:4:7
                   [--truth-results d5-panel.ndjson --truth-config exact:5:7]
                   --reference-config exact:4:7 --out summary.json

The truth is the exact column values of --truth-config, read from
--truth-results if given (else from --results).  The reference config's work
on each root is the denominator of the work ratio.  Regret is normalised
((best - chosen) / (best - worst), 0 when all legal values are equal) and raw.
Paired differences against a --paired-config are bootstrapped over games.
"""
import argparse
import json
import random
import sys


def load(path):
    rows = {}
    with open(path) as handle:
        for line in handle:
            line = line.strip()
            if line:
                row = json.loads(line)
                rows[(row["game"], row["move"])] = {r["config"]: r for r in row["results"]}
    return rows


def regret_of(truth, action):
    columns, values = truth["columns"], truth["values"]
    best = max(values)
    worst = min(values)
    chosen_value = values[columns.index(action)]
    spread = best - worst
    norm = 0.0 if spread <= 0 else (best - chosen_value) / spread
    agree = 1.0 if chosen_value == best else 0.0
    return norm, best - chosen_value, agree


def percentile(sorted_values, q):
    if not sorted_values:
        return None
    index = min(len(sorted_values) - 1, max(0, int(q * (len(sorted_values) - 1))))
    return sorted_values[index]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--results", required=True)
    parser.add_argument("--truth-results")
    parser.add_argument("--truth-config", required=True)
    parser.add_argument("--reference-config", required=True)
    parser.add_argument("--paired-config", help="config whose per-root regret every other config is paired against")
    parser.add_argument("--out", required=True)
    parser.add_argument("--resamples", type=int, default=10000)
    parser.add_argument("--seed", type=lambda s: int(s, 0), default=0x6B660002)
    args = parser.parse_args()

    results = load(args.results)
    truth_rows = load(args.truth_results) if args.truth_results else results
    keys = sorted(k for k in results if k in truth_rows and args.truth_config in truth_rows[k])
    configs = []
    for k in keys:
        for c in results[k]:
            if c not in configs:
                configs.append(c)
    per_config = {c: [] for c in configs}
    monotone_violations = {c: 0 for c in configs}
    for k in keys:
        truth = truth_rows[k][args.truth_config]
        ref_work = results[k][args.reference_config]["work"] if args.reference_config in results[k] else None
        for c in configs:
            r = results[k].get(c)
            if r is None:
                continue
            norm, raw, agree = regret_of(truth, r["action"])
            if r["columns"] == truth["columns"]:
                for a, b in zip(r["values"], truth["values"]):
                    if a > b:
                        monotone_violations[c] += 1
            per_config[c].append({
                "game": k[0], "move": k[1], "norm": norm, "raw": raw, "agree": agree,
                "work": r["work"], "priorWork": r["priorWork"], "prunedNodes": r["prunedNodes"],
                "refWork": ref_work, "wallMs": r["wallMs"],
            })

    rng = random.Random(args.seed)
    summary = {}
    for c in configs:
        rows = per_config[c]
        n = len(rows)
        if n == 0:
            continue
        work_ratio_rows = [x["work"] / x["refWork"] for x in rows if x["refWork"]]
        total_work = sum(x["work"] for x in rows)
        total_ref = sum(x["refWork"] for x in rows if x["refWork"])
        entry = {
            "roots": n,
            "games": len({x["game"] for x in rows}),
            "top1Agreement": sum(x["agree"] for x in rows) / n,
            "meanNormalisedRegret": sum(x["norm"] for x in rows) / n,
            "meanRawRegret": sum(x["raw"] for x in rows) / n,
            "maxNormalisedRegret": max(x["norm"] for x in rows),
            "meanWork": total_work / n,
            "workRatioOfMeans": (total_work / total_ref) if total_ref else None,
            "meanWorkRatio": (sum(work_ratio_rows) / len(work_ratio_rows)) if work_ratio_rows else None,
            "priorWorkShare": (sum(x["priorWork"] for x in rows) / total_work) if total_work else 0.0,
            "meanPrunedNodes": sum(x["prunedNodes"] for x in rows) / n,
            "meanWallMs": sum(x["wallMs"] for x in rows) / n,
            "monotoneViolations": monotone_violations[c],
        }
        # game-clustered bootstrap of agreement and regret
        by_game = {}
        for x in rows:
            by_game.setdefault(x["game"], []).append(x)
        games = sorted(by_game)

        def boot(stat):
            values = []
            for _ in range(args.resamples):
                pooled = []
                for _ in games:
                    pooled.extend(by_game[games[rng.randrange(len(games))]])
                values.append(stat(pooled))
            values.sort()
            return values

        b_agree = boot(lambda p: sum(x["agree"] for x in p) / len(p))
        b_regret = boot(lambda p: sum(x["norm"] for x in p) / len(p))
        entry["top1AgreementLb95"] = percentile(b_agree, 0.05)
        entry["meanNormalisedRegretUb95"] = percentile(b_regret, 0.95)
        if args.paired_config and args.paired_config in per_config and c != args.paired_config:
            other = {(x["game"], x["move"]): x for x in per_config[args.paired_config]}
            diffs_by_game = {}
            for x in rows:
                o = other.get((x["game"], x["move"]))
                if o is not None:
                    diffs_by_game.setdefault(x["game"], []).append(
                        {"norm": o["norm"] - x["norm"], "raw": o["raw"] - x["raw"]})
            dg = sorted(diffs_by_game)

            def boot_diff(field):
                values = []
                for _ in range(args.resamples):
                    pooled = []
                    for _ in dg:
                        pooled.extend(diffs_by_game[dg[rng.randrange(len(dg))]])
                    values.append(sum(d[field] for d in pooled) / len(pooled))
                values.sort()
                return values

            all_diffs = [d for g in dg for d in diffs_by_game[g]]
            bn = boot_diff("norm")
            br = boot_diff("raw")
            entry["pairedVs" + args.paired_config] = {
                "meanNormalisedRegretReduction": sum(d["norm"] for d in all_diffs) / len(all_diffs),
                "normalisedLb95": percentile(bn, 0.05),
                "meanRawRegretReduction": sum(d["raw"] for d in all_diffs) / len(all_diffs),
                "rawLb95": percentile(br, 0.05),
                "roots": len(all_diffs),
            }
        summary[c] = entry
        print("%-40s n %4d agree %.4f regret %.5f raw %9.1f work %.3f prior %.3f" % (
            c, n, entry["top1Agreement"], entry["meanNormalisedRegret"], entry["meanRawRegret"],
            entry["workRatioOfMeans"] or 0, entry["priorWorkShare"]), file=sys.stderr)
    out = {
        "format": "drop7-oneply-q-prune-metrics-v1",
        "results": args.results,
        "truthResults": args.truth_results or args.results,
        "truthConfig": args.truth_config,
        "referenceConfig": args.reference_config,
        "pairedConfig": args.paired_config,
        "roots": len(keys),
        "bootstrap": {"resamples": args.resamples, "seed": args.seed, "unit": "game"},
        "configs": summary,
    }
    with open(args.out, "w") as handle:
        json.dump(out, handle, indent=2)


if __name__ == "__main__":
    main()
