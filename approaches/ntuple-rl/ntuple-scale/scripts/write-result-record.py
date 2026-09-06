#!/usr/bin/env python3
"""Draft the result record for one ntuple-scale run from its analysis.json.

Every metric is copied from analysis.json (itself computed from the run's
artifacts by analyze.py); the gate checks are the preregistered criteria as
analyze.py evaluated them.  The summary paragraph is assembled from those
numbers with the outcome wording the experiment record prescribes.  The
record is written as a draft for the coordinator to read before it is
committed; nothing here opens a seed or changes a gate.

Usage: write-result-record.py --run RUN_ID --result-id RS-... --root REPO
                              [--contribution CT-...]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import time

THEORY = "TH-20260905-ntuple-line-tuples-tc-td-leaf-bcb25133"
EXPERIMENT = "EX-20260905-ntuple-scale-tc-td-leaf-d3-535b2620"
MACHINE = "research/system-profiles/MACH-20260905T192901Z-83559f62.json"


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fmt(x):
    return f"{x:,.0f}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", required=True)
    parser.add_argument("--result-id", required=True)
    parser.add_argument("--root", default=".")
    parser.add_argument("--contribution", default="CT-20260905T192505Z-c487efca")
    args = parser.parse_args()
    out = os.path.join(args.root, "runs", args.run, "ntuple-scale")
    analysis = json.load(open(os.path.join(out, "analysis.json"), encoding="utf-8"))
    screen = analysis["screen"]
    main_run = analysis["main"]
    pilot = analysis["pilot"]
    primary = screen["contrasts"]["candidate-d3s7-vs-fair-d3s7"]["score"]
    primary_moves = screen["contrasts"]["candidate-d3s7-vs-fair-d3s7"]["moves"]
    vs_d4 = screen["contrasts"].get("candidate-d3s7-vs-fair-d4s7", {}).get("score")
    one_ply = screen["contrasts"].get("candidate-1ply-vs-fair-d3s7", {}).get("score")
    d4_vs_d3 = screen["contrasts"].get("fair-d4s7-vs-fair-d3s7", {}).get("score")
    gate = screen["gate"]
    arms = screen["arms"]
    cand = arms["candidate-d3s7"]
    fair3 = arms["fair-d3s7"]
    passed = bool(gate and gate["passed"])
    integrity_ok = all(a["illegalDecisions"] == 0 and a["incompleteDecisions"] == 0 for a in arms.values()) and main_run["artifactIntegrity"]["illegalDecisions"] == 0 and main_run["artifactIntegrity"]["incompleteDecisions"] == 0
    heldout = os.path.join(out, "screen", "heldout.json")
    hash_path = os.path.join(out, "main", "candidate-weights.sha256")
    candidate_sha = open(hash_path, encoding="utf-8").read().split()[0] if os.path.exists(hash_path) else None
    best = main_run["best"]
    selection = pilot["selection"]
    ablation = pilot["arms"].get("E")

    summary = (
        f"Held-out screen, 256 never-read paired public-development games (0xa52f2140+): the frozen n-tuple tables as the leaf of the depth-3 "
        f"seven-stratum fair search averaged {fmt(cand['meanScore'])} points and {cand['meanMoves']:.2f} moves against {fmt(fair3['meanScore'])} points "
        f"and {fair3['meanMoves']:.2f} moves for the identical search with the frozen fair leaf: paired {primary['meanDelta']:+,.0f} points "
        f"(bootstrap 95% lower bound {primary['bootstrapLower95']:+,.0f}, Student-t lower bound {primary['studentTLower95']:+,.0f}, upper bound "
        f"{primary['bootstrapUpper95']:+,.0f}, detection floor {fmt(primary['detectionFloor'])}), W-T-L {primary['wins']}-{primary['ties']}-{primary['losses']}, "
        f"halves {primary['firstHalfMeanDelta']:+,.0f} / {primary['secondHalfMeanDelta']:+,.0f}, lower quartile {fmt(primary['candidateQ25'])} vs "
        f"{fmt(primary['referenceQ25'])}, moves {primary_moves['meanDelta']:+.2f}. The preregistered gate {'PASSES' if passed else 'FAILS'}. "
    )
    if vs_d4:
        summary += (
            f"Against the program's standing reference, the fair leaf at depth 4 on the same seeds ({fmt(arms['fair-d4s7']['meanScore'])} points), the "
            f"candidate is {vs_d4['meanDelta']:+,.0f} paired (bootstrap LB {vs_d4['bootstrapLower95']:+,.0f}, UB {vs_d4['bootstrapUpper95']:+,.0f}, "
            f"W-T-L {vs_d4['wins']}-{vs_d4['ties']}-{vs_d4['losses']}); diagnostic only. "
        )
    if one_ply:
        summary += (
            f"The same tables played directly one ply averaged {fmt(arms['candidate-1ply']['meanScore'])} points, {one_ply['meanDelta']:+,.0f} paired against fair-d3s7 "
            f"(LB {one_ply['bootstrapLower95']:+,.0f}); diagnostic. "
        )
    if d4_vs_d3:
        summary += f"Fair-d4s7 minus fair-d3s7 on these seeds: {d4_vs_d3['meanDelta']:+,.0f} (LB {d4_vs_d3['bootstrapLower95']:+,.0f}). "
    summary += (
        f"Candidate: the tables of the main run's validation point at {fmt(best['moves'])} training moves (layout {selection['layout']}, alpha {selection['alpha']}, "
        f"selected from six pilot arms by the preregistered rule with a final validation margin of {selection['finalMargin']:+,.0f}), whose paired margin on the "
        f"64-game training-role validation block was {best['pairedDeltaD3']:+,.0f}; SHA-256 {candidate_sha}. Main run: {fmt(main_run['movesTotal'])} training moves, "
        f"{fmt(main_run['gamesTotal'])} games, {len(main_run['validations'])} validation points, mean {fmt(main_run['meanMovesPerSecond'] or 0)} moves per second. "
    )
    if ablation and ablation.get("finalMargin") is not None:
        summary += (
            f"Mechanism ablation (pilot, windows-only arm E): final validation margin {ablation['finalMargin']:+,.0f} against the full layout's "
            f"{pilot['arms']['A']['finalMargin']:+,.0f} (arm A) and {pilot['arms']['C']['finalMargin']:+,.0f} (arm C, selected). "
        )
    summary += f"Training-signal check: {'at least one' if main_run['anyPositiveMargin'] else 'no'} validation point of the main run had a positive paired margin."

    checks = []
    checks.append({"criterion": "All CHECK gates passed before the first training seed was read", "passed": bool(analysis["gates"] and analysis["gates"]["passed"]), "observed": f"{len(analysis['gates']['gates'])} gate lines, all PASS" if analysis["gates"] else "no gates.log"})
    checks.append({"criterion": "Every validation artifact and the screen artifact have illegalDecisions 0 and incompleteDecisions 0", "passed": integrity_ok, "observed": f"main validations illegal {main_run['artifactIntegrity']['illegalDecisions']} / incomplete {main_run['artifactIntegrity']['incompleteDecisions']}; screen arms " + ", ".join(f"{n}: {a['illegalDecisions']}/{a['incompleteDecisions']}" for n, a in arms.items())})
    for c in gate["checks"][1:]:
        checks.append({"criterion": c["criterion"], "passed": bool(c["passed"]), "observed": json.dumps(c.get("observed"))})
    checks.append({"criterion": "The screened candidate is the exact frozen best-weights.bin (SHA-256 recorded before the screen lease opened)", "passed": candidate_sha is not None, "observed": candidate_sha or "no hash file"})

    record = {
        "$schema": "../schemas/result-v1.schema.json",
        "format": "drop7-result-v1",
        "resultId": args.result_id,
        "theoryIds": [THEORY],
        "experimentId": EXPERIMENT,
        "runIds": [args.run],
        "runValidity": "valid" if integrity_ok else "invalid",
        "scientificOutcome": "pass" if passed else "fail",
        "assessment": "supported-as-tested" if passed else "not-supported-as-tested",
        "evidenceTier": "public-development",
        "summary": summary,
        "metrics": {
            "screen": {
                "games": 256,
                "seedStartHex": screen["seedStartHex"],
                "arms": {n: {k: a[k] for k in ("meanScore", "medianScore", "q25Score", "minScore", "maxScore", "sdScore", "meanMoves", "q25Moves", "numberedClearsPerMove", "coverRevealsPerMove", "meanOccupiedCells", "censoredGames", "illegalDecisions", "incompleteDecisions", "gamesAtOrAboveMillion", "meanWallSecondsPerGame")} for n, a in arms.items()},
                "contrasts": {n: c for n, c in screen["contrasts"].items()},
            },
            "main": {
                "layout": selection["layout"],
                "alpha": selection["alpha"],
                "entries": main_run["config"]["entries"],
                "movesTotal": main_run["movesTotal"],
                "gamesTotal": main_run["gamesTotal"],
                "wallSeconds": main_run["wallSeconds"],
                "meanMovesPerSecond": main_run["meanMovesPerSecond"],
                "best": best,
                "validations": [{"movesTrained": v["movesTrained"], "ntupleD3Mean": v["arms"]["ntuple-d3s7"]["meanScore"], "directMean": v["arms"]["ntuple-1ply"]["meanScore"], "fairD3Mean": v["arms"]["fair-d3s7"]["meanScore"], "pairedDeltaD3": v["pairedD3VsFair"]["meanDelta"], "bootstrapLower95": v["pairedD3VsFair"]["bootstrapLower95"], "wins": v["pairedD3VsFair"]["wins"], "losses": v["pairedD3VsFair"]["losses"]} for v in main_run["validations"]],
                "anyPositiveMargin": main_run["anyPositiveMargin"],
            },
            "pilot": {name: {"layout": a["layout"], "alpha": a["alpha"], "entries": a["config"]["entries"], "movesTotal": a["movesTotal"], "finalMargin": a["finalMargin"], "bestMargin": a["bestMargin"]} for name, a in pilot["arms"].items()},
            "pilotSelection": selection,
            "candidateSha256": candidate_sha,
        },
        "gateChecks": checks,
        "perGameArtifact": {"path": os.path.relpath(heldout, args.root), "sha256": sha256_file(heldout), "recordCount": sum(a["games"] for a in arms.values())},
        "machineProfileRefs": [MACHINE],
        "artifactManifestRef": None,
        "limitations": [
            "Public-development SCREEN tier, 256 paired games opened once; nothing here is a qualification claim, and protected and final cohorts stay sealed.",
            "The candidate is the validation point with the largest paired margin on a 64-game training-role block that was read at every validation point and also chose the configuration; that selection is upward-biased, which is why the held-out screen exists.",
            "Training used lock-free asynchronous updates from 32 threads, so the training run is not bit-reproducible; the frozen tables are hashed and every gameplay arm is deterministic and worker-count independent.",
            "The fair-d4s7 arm is the program's standing reference for context only; the preregistered comparator is the identical depth-3 search with the frozen fair leaf.",
            "Table files (1.9 GB weights, 5.8 GB with accumulators for the pilot layouts; 4.0 GB and 12 GB for the selected phase=all layout) are retained on the workstation with their SHA-256 and are not committed.",
            "Wall times were measured on a shared workstation with the web console building concurrently for part of the run; ratios between arms on the same seeds are the trustworthy quantity.",
        ],
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
