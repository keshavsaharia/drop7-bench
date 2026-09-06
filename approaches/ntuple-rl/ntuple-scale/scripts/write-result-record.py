#!/usr/bin/env python3
"""Draft the result record for one ntuple-scale run from its analysis.json.

Every metric is copied from analysis.json (itself computed from the run's
artifacts by analyze.py); the gate checks are the preregistered criteria as
analyze.py evaluated them.  The summary paragraph is assembled from those
numbers with the outcome wording the experiment record prescribes.  The
record is written as a draft for the coordinator to read before it is
committed; nothing here opens a seed or changes a gate.

Understands both run shapes analyze.py understands: the first experiment
(pilot arms, one candidate) and the replication (smoke run, plateau rule,
the first experiment's frozen tables as the prior-* arms, a replication
check and a scale verdict).

Usage: write-result-record.py --run RUN_ID --result-id RS-... --root REPO
                              --experiment EX-... --theory TH-... --machine research/system-profiles/MACH-....json
                              [--contribution CT-...]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import time

DEFAULT_THEORY = "TH-20260905-ntuple-line-tuples-tc-td-leaf-bcb25133"
DEFAULT_EXPERIMENT = "EX-20260905-ntuple-scale-tc-td-leaf-d3-535b2620"
DEFAULT_MACHINE = "research/system-profiles/MACH-20260905T192901Z-83559f62.json"


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
    parser.add_argument("--experiment", default=DEFAULT_EXPERIMENT)
    parser.add_argument("--theory", default=DEFAULT_THEORY)
    parser.add_argument("--machine", default=DEFAULT_MACHINE)
    parser.add_argument("--contribution", default="CT-20260905T192505Z-c487efca")
    args = parser.parse_args()
    out = os.path.join(args.root, "runs", args.run, "ntuple-scale")
    analysis = json.load(open(os.path.join(out, "analysis.json"), encoding="utf-8"))
    screen = analysis["screen"]
    main_run = analysis["main"]
    pilot = analysis.get("pilot")
    smoke = analysis.get("smoke")
    games = screen.get("games") or next(iter(screen["arms"].values()))["games"]
    contrasts = {name: c["score"] for name, c in screen["contrasts"].items()}
    primary = contrasts["candidate-d3s7-vs-fair-d3s7"]
    primary_moves = screen["contrasts"]["candidate-d3s7-vs-fair-d3s7"]["moves"]
    gate = screen["gate"]
    replication = screen.get("replication")
    scale = screen.get("scale")
    arms = screen["arms"]
    cand = arms["candidate-d3s7"]
    fair3 = arms["fair-d3s7"]
    passed = bool(gate and gate["passed"])
    integrity_ok = all(a["illegalDecisions"] == 0 and a["incompleteDecisions"] == 0 for a in arms.values()) and main_run["artifactIntegrity"]["illegalDecisions"] == 0 and main_run["artifactIntegrity"]["incompleteDecisions"] == 0
    heldout = os.path.join(out, "screen", "heldout.json")
    hash_path = os.path.join(out, "main", "candidate-weights.sha256")
    candidate_sha = open(hash_path, encoding="utf-8").read().split()[0] if os.path.exists(hash_path) else None
    prior_hash_path = os.path.join(out, "main", "prior-weights.sha256")
    prior_sha = open(prior_hash_path, encoding="utf-8").read().split()[0] if os.path.exists(prior_hash_path) else None
    best = main_run["best"]
    config = main_run["config"]
    validate_games = main_run.get("validateGames") or 64
    stop = main_run.get("stop")

    summary = (
        f"Held-out screen, {games} never-read paired public-development games ({screen['seedStartHex']}+): the frozen n-tuple tables as the leaf of the depth-3 "
        f"seven-stratum fair search averaged {fmt(cand['meanScore'])} points and {cand['meanMoves']:.2f} moves against {fmt(fair3['meanScore'])} points "
        f"and {fair3['meanMoves']:.2f} moves for the identical search with the frozen fair leaf: paired {primary['meanDelta']:+,.0f} points "
        f"(bootstrap 95% lower bound {primary['bootstrapLower95']:+,.0f}, Student-t lower bound {primary['studentTLower95']:+,.0f}, upper bound "
        f"{primary['bootstrapUpper95']:+,.0f}, detection floor {fmt(primary['detectionFloor'])}), W-T-L {primary['wins']}-{primary['ties']}-{primary['losses']}, "
        f"halves {primary['firstHalfMeanDelta']:+,.0f} / {primary['secondHalfMeanDelta']:+,.0f}, lower quartile {fmt(primary['candidateQ25'])} vs "
        f"{fmt(primary['referenceQ25'])}, moves {primary_moves['meanDelta']:+.2f}. The preregistered gate {'PASSES' if passed else 'FAILS'}. "
    )
    if replication and "prior-d3s7-vs-fair-d3s7" in contrasts:
        prior = arms["prior-d3s7"]
        summary += (
            f"Replication: the first experiment's frozen tables (SHA-256 {prior_sha}) as the same leaf on these fresh seeds averaged {fmt(prior['meanScore'])} points and "
            f"{prior['meanMoves']:.2f} moves; " + contrast_sentence("prior-d3s7 minus fair-d3s7", contrasts["prior-d3s7-vs-fair-d3s7"]) +
            f"The replication criteria {'PASS' if replication['passed'] else 'FAIL'}. "
        )
    if scale:
        summary += (
            f"Scale: the wider, longer-trained candidate against the first candidate on the same seeds is {scale['meanDelta']:+,.0f} paired "
            f"(bootstrap LB {scale['bootstrapLower95']:+,.0f}, t LB {scale['studentTLower95']:+,.0f}, UB {scale['bootstrapUpper95']:+,.0f}, floor {fmt(scale['detectionFloor'])}, "
            f"W-T-L {scale['wins']}-{scale['ties']}-{scale['losses']}): the preregistered scale verdict is {scale['verdict']}. "
        )
    if "candidate-d3s7-vs-fair-d4s7" in contrasts:
        c = contrasts["candidate-d3s7-vs-fair-d4s7"]
        summary += (
            f"Against the program's standing reference, the fair leaf at depth 4 on the same seeds ({fmt(arms['fair-d4s7']['meanScore'])} points), the "
            f"candidate is {c['meanDelta']:+,.0f} paired (bootstrap LB {c['bootstrapLower95']:+,.0f}, UB {c['bootstrapUpper95']:+,.0f}, "
            f"W-T-L {c['wins']}-{c['ties']}-{c['losses']}); diagnostic only. "
        )
    if "prior-d3s7-vs-fair-d4s7" in contrasts:
        c = contrasts["prior-d3s7-vs-fair-d4s7"]
        summary += f"The first candidate against fair-d4s7 on these seeds: {c['meanDelta']:+,.0f} (LB {c['bootstrapLower95']:+,.0f}); diagnostic. "
    if "candidate-1ply-vs-fair-d3s7" in contrasts:
        c = contrasts["candidate-1ply-vs-fair-d3s7"]
        summary += (
            f"The same tables played directly one ply averaged {fmt(arms['candidate-1ply']['meanScore'])} points, {c['meanDelta']:+,.0f} paired against fair-d3s7 "
            f"(LB {c['bootstrapLower95']:+,.0f}); diagnostic. "
        )
    if "fair-d4s7-vs-fair-d3s7" in contrasts:
        c = contrasts["fair-d4s7-vs-fair-d3s7"]
        summary += f"Fair-d4s7 minus fair-d3s7 on these seeds: {c['meanDelta']:+,.0f} (LB {c['bootstrapLower95']:+,.0f}). "
    summary += (
        f"Candidate: the tables of the main run's validation point at {fmt(best['moves'])} training moves (layout {config['layout']}, alpha {config['alpha']}, "
        f"{fmt(config['entries'])} entries), whose paired margin on the {validate_games}-game training-role validation block was {best['pairedDeltaD3']:+,.0f}; "
        f"SHA-256 {candidate_sha}. Main run: {fmt(main_run['movesTotal'])} training moves, {fmt(main_run['gamesTotal'])} games, "
        f"{len(main_run['validations'])} validation points, mean {fmt(main_run['meanMovesPerSecond'] or 0)} moves per second"
    )
    if stop:
        summary += f", stopped by the {stop['reason']} rule"
        if stop.get("recentWindowMean") is not None:
            summary += f" (last {stop['plateauWindow']} validation points mean {stop['recentWindowMean']:+,.0f}, the {stop['plateauWindow']} before them {stop['previousWindowMean']:+,.0f})"
    summary += ". "
    if pilot:
        selection = pilot["selection"]
        ablation = pilot["arms"].get("E")
        summary += f"Configuration selected from six pilot arms by the preregistered rule (arm {selection['arm']}, final validation margin {selection['finalMargin']:+,.0f}). "
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
    if replication:
        for c in replication["checks"][1:]:
            checks.append({"criterion": "Replication: " + c["criterion"], "passed": bool(c["passed"]), "observed": json.dumps(c.get("observed"))})
        checks.append({"criterion": "Replication: the prior arm is the first experiment's exact frozen tables (SHA-256 verified before the screen lease opened)", "passed": prior_sha == "0ade9d4e4080ebdd52a1474b1a13410dc8dfb77f5eba24b078aa7703c92ace0b", "observed": prior_sha or "no hash file"})

    metrics = {
        "screen": {
            "games": games,
            "seedStartHex": screen["seedStartHex"],
            "arms": {n: {k: a[k] for k in ("meanScore", "medianScore", "q25Score", "minScore", "maxScore", "sdScore", "meanMoves", "q25Moves", "numberedClearsPerMove", "coverRevealsPerMove", "meanOccupiedCells", "censoredGames", "illegalDecisions", "incompleteDecisions", "gamesAtOrAboveMillion", "meanWallSecondsPerGame")} for n, a in arms.items()},
            "contrasts": {n: c for n, c in screen["contrasts"].items()},
            "replication": replication,
            "scale": scale,
        },
        "main": {
            "layout": config["layout"],
            "alpha": config["alpha"],
            "entries": config["entries"],
            "activePerState": config.get("activePerState"),
            "validateGames": validate_games,
            "movesTotal": main_run["movesTotal"],
            "gamesTotal": main_run["gamesTotal"],
            "wallSeconds": main_run["wallSeconds"],
            "meanMovesPerSecond": main_run["meanMovesPerSecond"],
            "stop": stop,
            "best": best,
            "validations": [{"point": i + 1, "movesTrained": v["movesTrained"], "ntupleD3Mean": v["arms"]["ntuple-d3s7"]["meanScore"], "directMean": v["arms"]["ntuple-1ply"]["meanScore"], "fairD3Mean": v["arms"]["fair-d3s7"]["meanScore"], "pairedDeltaD3": v["pairedD3VsFair"]["meanDelta"], "bootstrapLower95": v["pairedD3VsFair"]["bootstrapLower95"], "wins": v["pairedD3VsFair"]["wins"], "losses": v["pairedD3VsFair"]["losses"], "plateau": v.get("plateau")} for i, v in enumerate(main_run["validations"])],
            "anyPositiveMargin": main_run["anyPositiveMargin"],
        },
        "candidateSha256": candidate_sha,
        "priorSha256": prior_sha,
    }
    if smoke:
        metrics["smoke"] = {"layout": smoke["config"]["layout"] if smoke["config"] else None, "entries": smoke["config"]["entries"] if smoke["config"] else None, "movesTotal": smoke["movesTotal"], "wallSeconds": smoke["wallSeconds"], "meanMovesPerSecond": smoke["meanMovesPerSecond"]}
    if pilot:
        metrics["pilot"] = {name: {"layout": a["layout"], "alpha": a["alpha"], "entries": a["config"]["entries"], "movesTotal": a["movesTotal"], "finalMargin": a["finalMargin"], "bestMargin": a["bestMargin"]} for name, a in pilot["arms"].items()}
        metrics["pilotSelection"] = pilot["selection"]

    weights_gb = config["entries"] * 4 / 1e9
    limitations = [
        f"Public-development SCREEN tier, {games} paired games opened once; nothing here is a qualification claim, and protected and final cohorts stay sealed.",
        f"The candidate is the validation point with the largest paired margin on a {validate_games}-game training-role block that was read at every validation point" + (" and also chose the configuration" if pilot else " and drove the plateau stop rule") + "; that selection is upward-biased, which is why the held-out screen exists.",
        "Training used lock-free asynchronous updates from 32 threads, so the training run is not bit-reproducible; the frozen tables are hashed and every gameplay arm is deterministic and worker-count independent.",
        "The fair-d4s7 arm is the program's standing reference for context only; the preregistered comparator is the identical depth-3 search with the frozen fair leaf.",
        f"Table files ({weights_gb:.1f} GB weights, {weights_gb * 3:.1f} GB with accumulators) are retained on the workstation with their SHA-256 and are not committed.",
        "Wall times were measured on a shared workstation; ratios between arms on the same seeds are the trustworthy quantity.",
    ]
    if replication:
        limitations.append("The replication arm re-screens tables frozen by the first experiment on a block that experiment never read; it is a fresh-block replication by the same runner on the same machine, not by an independent runner.")

    record = {
        "$schema": "../schemas/result-v1.schema.json",
        "format": "drop7-result-v1",
        "resultId": args.result_id,
        "theoryIds": [args.theory],
        "experimentId": args.experiment,
        "runIds": [args.run],
        "runValidity": "valid" if integrity_ok else "invalid",
        "scientificOutcome": "pass" if passed else "fail",
        "assessment": "supported-as-tested" if passed else "not-supported-as-tested",
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
