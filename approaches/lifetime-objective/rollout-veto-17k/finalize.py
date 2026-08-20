#!/usr/bin/env python3
"""Evaluate the preregistered gate and print the numbers §5-§8 of
docs/exploratory/finding-03-rollout-veto-17k.md needs.

Pure read-and-compute: it opens only the three run artifacts and prints
markdown.  It writes nothing and decides nothing that the preregistration did
not already fix.
"""
import json
import sys

D4_REFERENCE_CLEARS = 2.400   # 12 discs per 5-move cycle / 5
D4_REFERENCE_REVEALS = 1.400  # 7 covered discs per 5-move cycle / 5


def main():
    base = sys.argv[1]
    paired = json.load(open(base + ".json"))
    baseline = json.load(open(base + "-baseline.json"))
    candidate = json.load(open(base + "-candidate.json"))

    ps, pm = paired["pairedScore"], paired["pairedMoves"]
    c = paired["candidateCounters"]
    n = paired["games"]

    g1 = ps["meanDelta"] > 0
    g2 = ps["bootstrapLower95"] > 0
    g3 = pm["meanDelta"] > 0
    g4 = ps["wins"] >= 20
    g5 = (candidate["numberedClearsPerMove"] >= baseline["numberedClearsPerMove"]
          and candidate["coverRevealsPerMove"] >= baseline["coverRevealsPerMove"])
    opp = c["vetoOpportunities"]
    veto_rate = c["vetoesTaken"] / opp if opp else 0.0
    design_fail = veto_rate < 1.0 / 50.0
    failures = paired["baselineFailures"] + paired["candidateFailures"]
    identity = baseline["scoreIdentityFailures"] + candidate["scoreIdentityFailures"]

    print("### Preregistered gate\n")
    print("| ID | Condition | Observed | Verdict |")
    print("| --- | --- | ---: | --- |")
    print(f"| G1 | paired mean score delta > 0 | {ps['meanDelta']:+,.1f} | "
          f"{'PASS' if g1 else 'FAIL'} |")
    print(f"| G2 | one-sided 95% bootstrap LB > 0 | {ps['bootstrapLower95']:+,.1f} | "
          f"{'PASS' if g2 else 'FAIL'} |")
    print(f"| G3 | paired mean move delta > 0 | {pm['meanDelta']:+.3f} | "
          f"{'PASS' if g3 else 'FAIL'} |")
    print(f"| G4 | score wins >= 20 of {n} | {ps['wins']} wins, {ps['ties']} ties, "
          f"{ps['losses']} losses | {'PASS' if g4 else 'FAIL'} |")
    print(f"| G5 | flow rates not below D4 | clears "
          f"{candidate['numberedClearsPerMove']:.4f} vs "
          f"{baseline['numberedClearsPerMove']:.4f}, reveals "
          f"{candidate['coverRevealsPerMove']:.4f} vs "
          f"{baseline['coverRevealsPerMove']:.4f} | "
          f"{'PASS' if g5 else 'FAIL'} |")
    print(f"| — | vetoes >= 1 per 50 opportunities | {c['vetoesTaken']}/{opp} "
          f"= {veto_rate * 50:.3f} per 50 | "
          f"{'FAIL-on-design' if design_fail else 'ok'} |")
    print(f"| — | runner / identity failures | {failures} / {identity} | "
          f"{'ok' if failures == 0 and identity == 0 else 'INVALID'} |")

    if failures or identity:
        outcome = "inconclusive (run validity compromised)"
    elif design_fail:
        outcome = "fail (FAIL-on-design: the veto rule cannot fire)"
    elif g1 and g2 and g3 and g4 and g5:
        outcome = "pass"
    else:
        outcome = "fail"
    print(f"\n**Scientific outcome: {outcome}**")

    print("\n### Flow rates against the steady-state requirement\n")
    print("| Arm | clears/move | deficit vs 2.400 | reveals/move | deficit vs 1.400 |")
    print("| --- | ---: | ---: | ---: | ---: |")
    for name, arm in (("fair D4 (comparator)", baseline),
                      ("rollout veto 17k", candidate)):
        cl, rv = arm["numberedClearsPerMove"], arm["coverRevealsPerMove"]
        print(f"| {name} | {cl:.4f} | "
              f"{100 * (cl / D4_REFERENCE_CLEARS - 1):+.1f}% | {rv:.4f} | "
              f"{100 * (rv / D4_REFERENCE_REVEALS - 1):+.1f}% |")
    print(f"\nclears/move delta = "
          f"{candidate['numberedClearsPerMove'] - baseline['numberedClearsPerMove']:+.5f}, "
          f"reveals/move delta = "
          f"{candidate['coverRevealsPerMove'] - baseline['coverRevealsPerMove']:+.5f}")

    # Trajectory identity: with zero vetoes the two arms must be the same policy.
    identical = all(
        r["baseline"]["score"] == r["candidate"]["score"]
        and r["baseline"]["moves"] == r["candidate"]["moves"]
        and r["baseline"]["numberedCleared"] == r["candidate"]["numberedCleared"]
        and r["baseline"]["coversRevealed"] == r["candidate"]["coversRevealed"]
        and r["baseline"]["rises"] == r["candidate"]["rises"]
        for r in paired["perGame"])
    print(f"\nAll {n} pairs identical in score, moves, clears, reveals and rises: "
          f"**{identical}**")

    print("\n### Cost accounting\n")
    dec = c["decisions"]
    print(f"- candidate decisions {dec:,}; veto opportunities {opp:,} "
          f"({100 * opp / dec:.1f}%)")
    print(f"- alternatives scored {c['alternativesConsidered']:,}; "
          f"passing all four tests {c['passingAlternatives']:,}")
    print(f"- rejected on return LB {c['returnRejections']:,} "
          f"({100 * c['returnRejections'] / c['alternativesConsidered']:.1f}% of alternatives)")
    print(f"- rejected on survivors {c['survivorRejections']:,}, "
          f"on clears {c['clearRejections']:,}, "
          f"on root-Q band {c['rootQRejections']:,}")
    print(f"- D2 continuation calls {c['d2Calls']:,}; "
          f"synthetic transitions {c['syntheticTransitions']:,}")
    print(f"- candidate arm wall {paired['candidateWallSeconds']:,.0f} s vs "
          f"comparator arm wall {paired['baselineWallSeconds']:,.0f} s "
          f"(x{paired['candidateWallSeconds'] / paired['baselineWallSeconds']:.2f})")
    print(f"- D4 seconds {c['d4Seconds']:,.0f}, rollout seconds "
          f"{c['rolloutSeconds']:,.0f}; per decision "
          f"{(c['d4Seconds'] + c['rolloutSeconds']) / dec:.3f} s, "
          f"per routed decision (rollout only) "
          f"{c['rolloutSeconds'] / opp:.3f} s")

    # Root-Q band equivalence proof.
    proof = c["returnRejections"] == c["alternativesConsidered"]
    print(f"\n### Root-Q band (7,000 vs 17,000)\n")
    print(f"Alternatives scored: {c['alternativesConsidered']:,}. "
          f"Failed the return lower-bound test: {c['returnRejections']:,}. "
          f"Equal: **{proof}**.")
    if proof:
        print("\nBecause the four veto conditions are ANDed and condition 3 "
              "(`return_ok`) failed for **every** alternative in the cohort, no "
              "value of `--root-q-loss` can change a single decision. The 7,000 "
              "and 17,000 bands are therefore *provably* the same policy on this "
              "cohort, and the audit-04 correction to `kMaximumRootQLoss` is "
              "measurable but inert. Rejections on the root-Q band "
              f"({c['rootQRejections']:,}) are all redundant with a return-test "
              "rejection.")


if __name__ == "__main__":
    main()
