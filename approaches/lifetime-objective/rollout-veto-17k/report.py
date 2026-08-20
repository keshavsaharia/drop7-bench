#!/usr/bin/env python3
"""Render the paired cohort table for the corrected-scoring rollout-veto retest.

Reads the three artifacts written by `veto --run`:
  <base>.json            paired summary + per-game veto counters
  <base>-baseline.json   standard drop7-lifetime-cohort-v1 artifact (fair D4)
  <base>-candidate.json  standard drop7-lifetime-cohort-v1 artifact (veto)

Writes a markdown fragment on stdout.  Reads nothing else; changes nothing.
"""
import json
import statistics
import sys


def load(base):
    with open(base + ".json") as handle:
        paired = json.load(handle)
    def optional(suffix):
        try:
            with open(base + suffix) as handle:
                return json.load(handle)
        except FileNotFoundError:
            return None
    return paired, optional("-baseline.json"), optional("-candidate.json")


def arm_row(name, cohort):
    s = cohort["score"]
    m = cohort["moves"]
    return (
        f"| {name} | {s['mean']:,.0f} | {s['median']:,.0f} | {s['q25']:,.0f} | "
        f"{s['min']:,.0f} | {s['max']:,.0f} | {s['sd']:,.0f} | {m['mean']:.2f} | "
        f"{cohort['censoredGames']} | {cohort['numberedClearsPerMove']:.3f} | "
        f"{cohort['coverRevealsPerMove']:.3f} |"
    )


def main():
    base = sys.argv[1]
    paired, baseline, candidate = load(base)

    print("### Cohort table\n")
    print("| Arm | mean | median | Q25 | min | max | sd | mean moves | "
          "censored | clears/move | reveals/move |")
    print("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | "
          "---: | ---: |")
    if baseline:
        print(arm_row("fair D4 (comparator)", baseline))
    if candidate:
        print(arm_row("rollout veto 17k (candidate)", candidate))

    ps = paired.get("pairedScore")
    pm = paired.get("pairedMoves")
    if ps:
        print("\n### Paired whole-game comparison\n")
        print("| Statistic | score | moves |")
        print("| --- | ---: | ---: |")
        print(f"| paired mean delta | {ps['meanDelta']:,.1f} | "
              f"{pm['meanDelta']:.3f} |")
        print(f"| paired median delta | {ps['medianDelta']:,.1f} | "
              f"{pm['medianDelta']:.3f} |")
        print(f"| one-sided 95% bootstrap LB | {ps['bootstrapLower95']:,.1f} | "
              f"{pm['bootstrapLower95']:.3f} |")
        print(f"| wins-ties-losses | {ps['wins']}-{ps['ties']}-{ps['losses']} | "
              f"{pm['wins']}-{pm['ties']}-{pm['losses']} |")

    c = paired["candidateCounters"]
    opp = c["vetoOpportunities"]
    dec = c["decisions"] or 1
    print("\n### Veto activity\n")
    print("| Quantity | value |")
    print("| --- | ---: |")
    print(f"| decisions | {c['decisions']:,} |")
    print(f"| danger (routed) decisions = veto opportunities | {opp:,} "
          f"({100.0 * opp / dec:.1f}% of decisions) |")
    print(f"| legal alternatives scored | {c['alternativesConsidered']:,} |")
    print(f"| alternatives passing all four tests | "
          f"{c['passingAlternatives']:,} |")
    print(f"| **vetoes taken** | **{c['vetoesTaken']:,}** |")
    if opp:
        print(f"| vetoes per 100 opportunities | "
              f"{100.0 * c['vetoesTaken'] / opp:.2f} |")
    print(f"| rejected on survivors | {c['survivorRejections']:,} |")
    print(f"| rejected on clears | {c['clearRejections']:,} |")
    print(f"| rejected on return lower bound | {c['returnRejections']:,} |")
    print(f"| rejected on D4 root-Q band | {c['rootQRejections']:,} |")

    print("\n### Cost\n")
    print("| Quantity | value |")
    print("| --- | ---: |")
    print(f"| candidate wall seconds (arm) | {paired['candidateWallSeconds']:,.1f} |")
    print(f"| baseline wall seconds (arm) | {paired['baselineWallSeconds']:,.1f} |")
    print(f"| candidate D4 CPU seconds | {c['d4Seconds']:,.1f} |")
    print(f"| candidate rollout CPU seconds | {c['rolloutSeconds']:,.1f} |")
    if opp:
        print(f"| CPU seconds per routed decision | "
              f"{c['rolloutSeconds'] / opp:.3f} |")
    print(f"| CPU seconds per decision (all) | "
          f"{(c['d4Seconds'] + c['rolloutSeconds']) / dec:.3f} |")
    print(f"| D2 continuation calls | {c['d2Calls']:,} |")
    print(f"| synthetic transitions | {c['syntheticTransitions']:,} |")
    print(f"| runner failures (baseline / candidate) | "
          f"{paired['baselineFailures']} / {paired['candidateFailures']} |")
    print(f"| score-identity failures (baseline / candidate) | "
          f"{baseline['scoreIdentityFailures'] if baseline else 'n/a'} / "
          f"{candidate['scoreIdentityFailures'] if candidate else 'n/a'} |")

    print("\n### Score decomposition\n")
    for name, cohort in (("fair D4", baseline), ("veto", candidate)):
        if not cohort:
            continue
        d = cohort["decomposition"]
        print(f"- {name}: level {100 * d['levelShare']:.2f}%, "
              f"board-clear {100 * d['clearShare']:.2f}%, "
              f"chain {100 * d['chainShare']:.2f}%; "
              f"rises/game {cohort['risesPerGame']:.2f}, "
              f"board clears/game {cohort['boardClearsPerGame']:.3f}, "
              f"max chain depth {cohort['maxChainDepth']}")

    print("\n### Per-game pairs\n")
    print("| seed | D4 score | D4 moves | veto score | veto moves | "
          "score delta | move delta | vetoes/opps |")
    print("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    for row in paired["perGame"]:
        b = row.get("baseline")
        k = row.get("candidate")
        v = row.get("veto", {})
        if not b or not k:
            continue
        print(f"| {row['seedHex']} | {b['score']:,} | {b['moves']} | "
              f"{k['score']:,} | {k['moves']} | "
              f"{k['score'] - b['score']:+,} | {k['moves'] - b['moves']:+} | "
              f"{v.get('vetoesTaken', 0)}/{v.get('vetoOpportunities', 0)} |")

    deltas = [r["candidate"]["score"] - r["baseline"]["score"]
              for r in paired["perGame"]
              if r.get("candidate") and r.get("baseline")]
    if len(deltas) > 1:
        print(f"\nPaired delta sd = {statistics.stdev(deltas):,.1f}")


if __name__ == "__main__":
    main()
