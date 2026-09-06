#!/usr/bin/env python3
"""Draft the result record for the depth-4 screen of the frozen tables
(EX-20260906-ntuple-scale-depth4-frozen-tables-*) from its analysis.json.

Every metric is copied from analysis.json (computed from the run's artifacts
by analyze.py); the gate checks are the preregistered criteria as analyze.py
evaluated them.  The summary paragraph is assembled from those numbers with
the outcome wording the experiment record prescribes.  Nothing here opens a
seed or changes a gate.

Usage: write-depth-result-record.py --run RUN_ID --result-id RS-... --root REPO
                                    --experiment EX-... --theory TH-... --machine research/system-profiles/MACH-....json
                                    --contribution CT-...
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import time

PRIOR_SHA256 = "0ade9d4e4080ebdd52a1474b1a13410dc8dfb77f5eba24b078aa7703c92ace0b"


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fmt(x):
    return f"{x:,.0f}"


def contrast_sentence(label, c):
    return (
        f"{label}: paired {c['meanDelta']:+,.0f} (bootstrap 95% lower bound {c['bootstrapLower95']:+,.0f}, Student-t lower bound "
        f"{c['studentTLower95']:+,.0f}, upper bound {c['bootstrapUpper95']:+,.0f}, detection floor {fmt(c['detectionFloor'])}), "
        f"W-T-L {c['wins']}-{c['ties']}-{c['losses']}, halves {c['firstHalfMeanDelta']:+,.0f} / {c['secondHalfMeanDelta']:+,.0f}. "
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", required=True)
    parser.add_argument("--result-id", required=True)
    parser.add_argument("--root", default=".")
    parser.add_argument("--experiment", required=True)
    parser.add_argument("--theory", required=True)
    parser.add_argument("--machine", required=True)
    parser.add_argument("--contribution", required=True)
    args = parser.parse_args()
    out = os.path.join(args.root, "runs", args.run, "ntuple-scale")
    analysis = json.load(open(os.path.join(out, "analysis.json"), encoding="utf-8"))
    screen = analysis["screen"]
    depth = screen["depth"]
    games = screen.get("games") or next(iter(screen["arms"].values()))["games"]
    contrasts = {name: c["score"] for name, c in screen["contrasts"].items()}
    moves = {name: c["moves"] for name, c in screen["contrasts"].items()}
    arms = screen["arms"]
    gate = screen["gate"]
    passed = bool(gate and gate["passed"])
    integrity_ok = all(a["illegalDecisions"] == 0 and a["incompleteDecisions"] == 0 for a in arms.values())
    heldout = os.path.join(out, "screen", "heldout.json")
    hash_path = os.path.join(out, "main", "prior-weights.sha256")
    tables_sha = open(hash_path, encoding="utf-8").read().split()[0] if os.path.exists(hash_path) else None
    step = contrasts["prior-d4s7-vs-prior-d3s7"]
    keep = contrasts.get("prior-d4s7-vs-fair-d4s7")
    fair_step = contrasts.get("fair-d4s7-vs-fair-d3s7")
    inter = depth["interaction"]
    p4, p3, f4, f3 = arms["prior-d4s7"], arms["prior-d3s7"], arms["fair-d4s7"], arms["fair-d3s7"]
    falsifiers = depth["theory"]
    if passed and not falsifiers["secondLegUpperBoundBelowZero"] and not falsifiers["persistenceLowerBoundAtOrBelowZero"]:
        assessment = "supported-as-tested"
    elif falsifiers["primaryFalsifierUpperBoundBelowZero"]:
        assessment = "not-supported-as-tested"
    elif passed:
        assessment = "mixed"
    else:
        assessment = "mixed" if step["bootstrapUpper95"] > 0 else "not-supported-as-tested"

    summary = (
        f"Held-out screen, {games} never-read paired public-development games ({screen['seedStartHex']}+), four arms on identical seeds, no training. "
        f"The frozen tables (SHA-256 {tables_sha}) as the leaf of the depth-4 seven-stratum fair search averaged {fmt(p4['meanScore'])} points and "
        f"{p4['meanMoves']:.2f} moves against {fmt(p3['meanScore'])} points and {p3['meanMoves']:.2f} moves for the same tables as the depth-3 leaf: "
        f"paired {step['meanDelta']:+,.0f} points (bootstrap 95% lower bound {step['bootstrapLower95']:+,.0f}, Student-t lower bound {step['studentTLower95']:+,.0f}, "
        f"upper bound {step['bootstrapUpper95']:+,.0f}, detection floor {fmt(step['detectionFloor'])}), W-T-L {step['wins']}-{step['ties']}-{step['losses']}, "
        f"halves {step['firstHalfMeanDelta']:+,.0f} / {step['secondHalfMeanDelta']:+,.0f}, lower quartile {fmt(step['candidateQ25'])} vs {fmt(step['referenceQ25'])}, "
        f"moves {moves['prior-d4s7-vs-prior-d3s7']['meanDelta']:+.2f}. The preregistered gate {'PASSES' if passed else 'FAILS'}. "
    )
    if fair_step:
        summary += (
            f"The fair leaf's own fourth ply on these seeds: fair-d4s7 {fmt(f4['meanScore'])} against fair-d3s7 {fmt(f3['meanScore'])}, "
            + contrast_sentence("fair-d4s7 minus fair-d3s7", fair_step)
        )
    summary += (
        f"Depth-step interaction, per game (tables d4 minus tables d3) minus (fair d4 minus fair d3): {inter['meanDelta']:+,.0f} "
        f"(bootstrap LB {inter['bootstrapLower95']:+,.0f}, t LB {inter['studentTLower95']:+,.0f}, UB {inter['bootstrapUpper95']:+,.0f}, floor {fmt(inter['detectionFloor'])}, "
        f"W-T-L {inter['wins']}-{inter['ties']}-{inter['losses']}): the preregistered verdict is '{inter['verdict']}'. "
    )
    if keep:
        summary += (
            "Persistence: " + contrast_sentence("prior-d4s7 minus fair-d4s7", keep)
            + f"The persistence criteria {'PASS' if depth['persistence']['passed'] else 'FAIL'}. "
        )
    if "prior-d3s7-vs-fair-d3s7" in contrasts:
        summary += f"The tables' depth-3 margin on these seeds: {contrast_sentence('prior-d3s7 minus fair-d3s7', contrasts['prior-d3s7-vs-fair-d3s7'])}"
    if "prior-d4s7-vs-fair-d3s7" in contrasts:
        c = contrasts["prior-d4s7-vs-fair-d3s7"]
        summary += f"prior-d4s7 minus fair-d3s7: {c['meanDelta']:+,.0f} (LB {c['bootstrapLower95']:+,.0f}). "
    summary += (
        f"Logical work per game: prior-d4s7 {fmt(p4['meanWork'])}, prior-d3s7 {fmt(p3['meanWork'])}, fair-d4s7 {fmt(f4['meanWork'])}, fair-d3s7 {fmt(f3['meanWork'])}; "
        f"mean wall seconds per game on the shared 32-thread workstation: {p4['meanWallSecondsPerGame']:.1f}, {p3['meanWallSecondsPerGame']:.1f}, {f4['meanWallSecondsPerGame']:.1f}, {f3['meanWallSecondsPerGame']:.1f}. "
        f"Theory falsifiers: primary upper bound below zero {falsifiers['primaryFalsifierUpperBoundBelowZero']}; second leg upper bound below zero {falsifiers['secondLegUpperBoundBelowZero']}; "
        f"persistence lower bound at or below zero {falsifiers['persistenceLowerBoundAtOrBelowZero']}."
    )

    checks = [
        {"criterion": "All CHECK gates passed on the frozen tables before the leased seed was read (including leaf-in-d4-search-determinism)", "passed": bool(analysis["gates"] and analysis["gates"]["passed"]), "observed": f"{len(analysis['gates']['gates'])} gate lines" if analysis["gates"] else "no gates.log"},
    ]
    for c in gate["checks"]:
        checks.append({"criterion": c["criterion"], "passed": bool(c["passed"]), "observed": json.dumps(c.get("observed"))})
    checks.append({"criterion": "Every table arm is the exact frozen best-weights.bin of RUN-20260905T193006Z-4fbeb4e5 (SHA-256 verified before the screen lease opened)", "passed": tables_sha == PRIOR_SHA256, "observed": tables_sha or "no hash file"})
    if depth.get("persistence"):
        for c in depth["persistence"]["checks"][1:]:
            checks.append({"criterion": "Persistence (beside the gate): " + c["criterion"], "passed": bool(c["passed"]), "observed": json.dumps(c.get("observed"))})
    checks.append({"criterion": "Depth-step interaction verdict (beside the gate): larger / smaller / inconclusive", "passed": None, "observed": json.dumps({"verdict": inter["verdict"], "meanDelta": inter["meanDelta"], "bootstrapLower95": inter["bootstrapLower95"], "bootstrapUpper95": inter["bootstrapUpper95"]})})

    metrics = {
        "screen": {
            "games": games,
            "seedStartHex": screen["seedStartHex"],
            "arms": {n: {k: a[k] for k in ("meanScore", "medianScore", "q25Score", "minScore", "maxScore", "sdScore", "meanMoves", "q25Moves", "numberedClearsPerMove", "coverRevealsPerMove", "meanOccupiedCells", "censoredGames", "illegalDecisions", "incompleteDecisions", "meanWork", "meanWallSecondsPerGame", "games") if k in a} for n, a in arms.items()},
            "contrasts": screen["contrasts"],
            "depth": depth,
        },
        "tablesSha256": tables_sha,
    }
    limitations = [
        f"Public-development SCREEN tier, {games} paired games opened once; nothing here is a qualification claim, and protected and final cohorts stay sealed.",
        "No table was trained or changed: both table arms are the first experiment's frozen candidate, selected there at the best of twenty training-role validation points; this screen inherits that selection but adds none of its own.",
        "The depth-4 search is the standing reference configuration (1M-entry table, seven strata, terminal utility -1,000,000); a different terminal utility, table size or stratum count is a different configuration and is not tested here.",
        "The fair-d3s7 and fair-d4s7 arms are context for the interaction reading; the preregistered comparator is the identical tables at depth 3.",
        "Wall times were measured on a shared workstation with all four arms run in sequence at 32 threads; logical work and the ratios between arms on the same seeds are the trustworthy cost quantities.",
    ]
    record = {
        "$schema": "../schemas/result-v1.schema.json",
        "format": "drop7-result-v1",
        "resultId": args.result_id,
        "theoryIds": [args.theory],
        "experimentId": args.experiment,
        "runIds": [args.run],
        "runValidity": "valid" if integrity_ok else "invalid",
        "scientificOutcome": "pass" if passed else "fail",
        "assessment": assessment,
        "evidenceTier": "public-development",
        "summary": summary,
        "metrics": metrics,
        "gateChecks": checks,
        "perGameArtifact": {"path": os.path.relpath(heldout, args.root), "sha256": sha256_file(heldout), "recordCount": sum(a["games"] for a in arms.values())},
        "machineProfileRefs": [args.machine],
        "artifactManifestRef": None,
        "limitations": limitations,
        "contributionIds": [args.contribution],
        "recordedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }
    path = os.path.join(args.root, "research", "results", f"{args.result_id}.json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(record, handle, indent=2, default=float)
        handle.write("\n")
    print(path)
    print(summary)


if __name__ == "__main__":
    main()
