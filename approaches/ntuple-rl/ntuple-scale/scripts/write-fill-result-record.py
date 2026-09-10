#!/usr/bin/env python3
"""Draft the result record for the fill-conditioned leaf experiment
(EX-20260906-ntuple-fill-conditioned-*) from its analysis.json.

Every metric is copied from analysis.json (computed from the run's artifacts
by analyze.py); the gate checks are the preregistered criteria as analyze.py
evaluated them, and the six readings beside the gate carry the fixed
three-way verdicts.  The summary paragraph is assembled from those numbers
with the outcome wording the experiment record prescribes.  Nothing here
opens a seed or changes a gate.

The per-game artifact is cited by its public archive reference, resolved from
the run record or the publisher manifest and checked against the local file's
digest (artifact_refs.py); publish the artifact before writing the record, or
pass --allow-local-path for an explicitly unfinalized draft.

Usage: write-fill-result-record.py --run RUN_ID --result-id RS-... --root REPO
                                   --experiment EX-... --theory TH-... --machine research/system-profiles/MACH-....json
                                   --contribution CT-... [--per-game-ref URL | --allow-local-path]
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time

from artifact_refs import UnresolvedArtifact, artifact_manifest_ref, citation_limitation, resolve_public_ref, sha256_file

PRIOR_SHA256 = "0ade9d4e4080ebdd52a1474b1a13410dc8dfb77f5eba24b078aa7703c92ace0b"


def fmt(x):
    return f"{x:,.0f}"


def contrast_sentence(label, c):
    return (
        f"{label}: paired {c['meanDelta']:+,.0f} (bootstrap 95% lower bound {c['bootstrapLower95']:+,.0f}, Student-t lower bound "
        f"{c['studentTLower95']:+,.0f}, upper bound {c['bootstrapUpper95']:+,.0f}, detection floor {fmt(c['detectionFloor'])}), "
        f"W-T-L {c['wins']}-{c['ties']}-{c['losses']}, halves {c['firstHalfMeanDelta']:+,.0f} / {c['secondHalfMeanDelta']:+,.0f}"
    )


def read_hash(out, name):
    path = os.path.join(out, "main", f"{name}-weights.sha256")
    return open(path, encoding="utf-8").read().split()[0] if os.path.exists(path) else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", required=True)
    parser.add_argument("--result-id", required=True)
    parser.add_argument("--root", default=".")
    parser.add_argument("--experiment", required=True)
    parser.add_argument("--theory", required=True)
    parser.add_argument("--machine", required=True)
    parser.add_argument("--contribution", required=True)
    parser.add_argument("--per-game-ref", default=None)
    parser.add_argument("--allow-local-path", action="store_true")
    args = parser.parse_args()
    out = os.path.join(args.root, "runs", args.run, "ntuple-scale")
    analysis = json.load(open(os.path.join(out, "analysis.json"), encoding="utf-8"))
    screen = analysis["screen"]
    fill = screen["fill"]
    games = screen.get("games") or next(iter(screen["arms"].values()))["games"]
    contrasts = {name: c["score"] for name, c in screen["contrasts"].items()}
    moves = {name: c["moves"] for name, c in screen["contrasts"].items()}
    arms = screen["arms"]
    gate = fill["gate"]
    passed = bool(gate and gate["passed"])
    integrity_ok = all(a["illegalDecisions"] == 0 and a["incompleteDecisions"] == 0 for a in arms.values())
    heldout = os.path.join(out, "screen", "heldout.json")
    heldout_relative = os.path.relpath(heldout, args.root)
    heldout_sha256 = sha256_file(heldout)
    try:
        per_game_path = resolve_public_ref(args.root, args.run, heldout_relative, heldout_sha256, args.per_game_ref)
    except UnresolvedArtifact as error:
        if not args.allow_local_path:
            raise SystemExit(f"error: {error}\n(--allow-local-path writes an unfinalized draft that cites {heldout_relative} instead)")
        print(f"warning: {error}; the draft cites {heldout_relative} and is not finalized", file=sys.stderr)
        per_game_path = heldout_relative
    hashes = {name: read_hash(out, name) for name in ("prior", "candidate", "control", "zeroed", "classmean")}
    selection = (analysis.get("pilot") or {}).get("selection") or {}
    pilot_arms = (analysis.get("pilot") or {}).get("arms") or {}
    edits = analysis.get("edits") or {}
    primary = contrasts["fill-d3s7-vs-prior-d3s7"]
    f3, p3 = arms["fill-d3s7"], arms["prior-d3s7"]
    falsifiers = fill["theory"]
    conditioning = fill.get("conditioning")
    continuation = fill.get("continuation")
    if passed and conditioning and conditioning["verdict"] == "supported":
        assessment = "supported-as-tested"
    elif falsifiers["primaryFalsifierUpperBoundBelowZero"]:
        assessment = "not-supported-as-tested"
    elif passed:
        assessment = "mixed"
    elif primary["bootstrapUpper95"] > 0:
        assessment = "mixed" if any(r and r["verdict"] == "supported" for r in (fill.get("zeroed"), fill.get("classmean"), continuation)) else "not-supported-as-tested"
    else:
        assessment = "not-supported-as-tested"

    arm_names = list(arms)
    summary = (
        f"Held-out screen, {games} never-read paired public-development games ({screen['seedStartHex']}+), {len(arm_names)} arms on identical seeds. "
        f"Candidate arm {selection.get('candidateArm')} (best validation margin {fmt(selection.get('candidateBestMargin', 0))} at {fmt(selection.get('candidateBestMoves', 0))} moves against the control arm's best {fmt(selection.get('controlBestMargin', 0))}; training-signal check passed {selection.get('trainingSignal', {}).get('passed')}). "
        f"The fill-conditioned tables (SHA-256 {hashes['candidate']}) as the depth-3 leaf averaged {fmt(f3['meanScore'])} points and {f3['meanMoves']:.2f} moves against "
        f"{fmt(p3['meanScore'])} and {p3['meanMoves']:.2f} for the unchanged frozen tables (SHA-256 {hashes['prior']}) in the same search: "
        f"{contrast_sentence('fill-d3s7 minus prior-d3s7', primary)}, lower quartile {fmt(primary['candidateQ25'])} vs {fmt(primary['referenceQ25'])}, "
        f"moves {moves['fill-d3s7-vs-prior-d3s7']['meanDelta']:+.2f}. The preregistered gate {'PASSES' if passed else 'FAILS'}. "
    )
    for key, label in (("conditioning", "Conditioning, fill-d3s7 minus control-d3s7"), ("continuation", "Continuation, control-d3s7 minus prior-d3s7"), ("zeroed", "Zeroed edit, zeroed-d3s7 minus prior-d3s7"), ("classmean", "Class-mean edit, classmean-d3s7 minus prior-d3s7"), ("fillDepth4", "At depth 4, fill-d4s7 minus prior-d4s7"), ("zeroedDepth4", "At depth 4, zeroed-d4s7 minus prior-d4s7")):
        r = fill.get(key)
        if r:
            summary += f"{contrast_sentence(label, r)}: verdict '{r['verdict']}'" + (f" (four criteria {'PASS' if r['passed'] else 'FAIL'})" if "passed" in r else "") + ". "
    for name in ("fill-d3s7", "control-d3s7", "zeroed-d3s7", "classmean-d3s7", "prior-d3s7", "prior-d4s7", "fill-d4s7", "zeroed-d4s7", "fill-1ply", "prior-1ply", "fair-d3s7"):
        if name in arms:
            summary += f"{name} {fmt(arms[name]['meanScore'])} / {arms[name]['meanMoves']:.2f} moves; "
    for name in ("fill-d4s7-vs-fill-d3s7", "prior-d4s7-vs-prior-d3s7", "zeroed-d4s7-vs-zeroed-d3s7", "prior-d3s7-vs-fair-d3s7", "fill-d3s7-vs-fair-d3s7", "fill-1ply-vs-prior-1ply"):
        if name in contrasts:
            c = contrasts[name]
            summary += f"{name}: {c['meanDelta']:+,.0f} (LB {c['bootstrapLower95']:+,.0f}, UB {c['bootstrapUpper95']:+,.0f}). "
    if edits:
        summary += (
            f"Edits: {fmt(edits['untouchedEntries'])} of {fmt(edits['entries'])} entries of the frozen file sit at the starting value {edits['optimisticStart']:.5f} rise units; "
            f"the zeroed edit changed {fmt(edits['outputs']['zeroed']['entriesChanged'])} entries and the class-mean edit {fmt(edits['outputs']['classmean']['entriesChanged'])}, none of them touched entries. "
        )
    for name in ("control", "occ5", "hgt5"):
        a = pilot_arms.get(name)
        if a:
            summary += f"Arm {name}: {fmt(a['movesTotal'])} moves, {len(a['validations'])} validation points, best margin {fmt(a['bestMargin'])} at {fmt(a['best']['moves']) if a.get('best') else 'n/a'} moves, final {fmt(a['finalMargin'])}, stop {a['stop']['reason'] if a.get('stop') else 'n/a'}, {fmt(a['meanMovesPerSecond'])} moves/s. "
    summary += (
        "Logical work per game: " + ", ".join(f"{n} {fmt(arms[n]['meanWork'])}" for n in ("fill-d3s7", "prior-d3s7", "fill-d4s7", "prior-d4s7") if n in arms) + ". "
        f"Theory falsifiers: {json.dumps(falsifiers)}."
    )

    checks = [
        {"criterion": "All CHECK gates passed on the probe block for both fill layouts before any leased seed was read", "passed": bool(analysis["gates"] and analysis["gates"]["passed"] and analysis.get("gatesHgt5") and analysis["gatesHgt5"]["passed"]), "observed": f"{len(analysis['gates']['gates'])} + {len(analysis['gatesHgt5']['gates']) if analysis.get('gatesHgt5') else 0} gate lines"},
        {"criterion": "gate --weights passed on each of the four new table files before the screen lease opened", "passed": bool(analysis.get("frozenGates")) and all(g["passed"] for g in analysis["frozenGates"].values()) and set(analysis["frozenGates"]) == {"candidate", "control", "zeroed", "classmean"}, "observed": json.dumps({k: v["passed"] for k, v in (analysis.get("frozenGates") or {}).items()})},
    ]
    for c in gate["checks"]:
        checks.append({"criterion": c["criterion"], "passed": bool(c["passed"]), "observed": json.dumps(c.get("observed"))})
    checks.append({"criterion": "The prior arms are the exact frozen best-weights.bin of RUN-20260905T193006Z-4fbeb4e5 (SHA-256 verified before any stage read it); every other table file's SHA-256 was written before the screen lease opened", "passed": hashes["prior"] == PRIOR_SHA256 and all(hashes[n] for n in ("candidate", "control", "zeroed", "classmean")), "observed": json.dumps(hashes)})
    for key, label in (("conditioning", "conditioning verdict (fill-d3s7 vs control-d3s7)"), ("continuation", "continuation reading (control-d3s7 vs prior-d3s7)"), ("zeroed", "zeroed edit verdict (vs prior-d3s7)"), ("classmean", "class-mean edit verdict (vs prior-d3s7)"), ("fillDepth4", "fill at depth 4 verdict (vs prior-d4s7)"), ("zeroedDepth4", "zeroed at depth 4 verdict (vs prior-d4s7)")):
        r = fill.get(key)
        if r:
            checks.append({"criterion": f"Beside the gate: {label}: supported / refuted / inconclusive", "passed": None, "observed": json.dumps({"verdict": r["verdict"], "meanDelta": r["meanDelta"], "bootstrapLower95": r["bootstrapLower95"], "bootstrapUpper95": r["bootstrapUpper95"]})})
    checks.append({"criterion": "Beside the gate: training-signal check (a fill arm's best validation margin exceeds the control arm's)", "passed": None, "observed": json.dumps(selection.get("trainingSignal"))})

    metrics = {
        "screen": {
            "games": games,
            "seedStartHex": screen["seedStartHex"],
            "arms": {n: {k: a[k] for k in ("meanScore", "medianScore", "q25Score", "minScore", "maxScore", "sdScore", "meanMoves", "q25Moves", "numberedClearsPerMove", "coverRevealsPerMove", "meanOccupiedCells", "censoredGames", "illegalDecisions", "incompleteDecisions", "meanWork", "meanWallSecondsPerGame", "games") if k in a} for n, a in arms.items()},
            "contrasts": screen["contrasts"],
            "fill": fill,
        },
        "selection": selection,
        "trainingArms": {n: {k: a[k] for k in ("layout", "movesTotal", "gamesTotal", "wallSeconds", "meanMovesPerSecond", "finalMargin", "bestMargin", "best", "stop") if k in a} | {"validationPoints": [{"movesTrained": v["movesTrained"], "pairedDeltaD3": v["pairedD3VsFair"]["meanDelta"], "bootstrapLower95": v["pairedD3VsFair"]["bootstrapLower95"], "ntupleD3Mean": v["arms"]["ntuple-d3s7"]["meanScore"], "directMean": v["arms"].get("ntuple-1ply", {}).get("meanScore")} for v in a["validations"]]} for n, a in pilot_arms.items()},
        "edits": {k: v for k, v in edits.items() if k != "families"} if edits else None,
        "tablesSha256": hashes,
    }
    limitations = [
        f"Public-development SCREEN tier, {games} paired games opened once; nothing here is a qualification claim, and protected and final cohorts stay sealed.",
        "The candidate and the control are each the best of their arm's validation points, and the candidate arm is the better of two, all chosen on the 256-game training-role validation block; the screen inherits that selection and measures what survives it.",
        "The training arms are Hogwild (32 lock-free workers) and are not bit-reproducible; a re-run trains different tables from the same warm start and seeds.",
        "Five buckets with fixed edges are the only conditioning tested; a different bucket count, different edges, or a bucket variable other than occupied cells and tallest column is a different configuration.",
        "The depth-4 arms use the standing reference configuration (1M-entry table, seven strata, terminal utility -1,000,000); the fair-d3s7 arm is context, and the fair leaf at depth 4 was not played on this block.",
        "Wall times were measured on a shared workstation with every arm run in sequence at 32 threads; logical work and the ratios between arms on the same seeds are the trustworthy cost quantities.",
    ]
    limitations.append(citation_limitation(per_game_path, heldout_relative))
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
        "perGameArtifact": {"path": per_game_path, "sha256": heldout_sha256, "recordCount": sum(a["games"] for a in arms.values())},
        "machineProfileRefs": [args.machine],
        "artifactManifestRef": artifact_manifest_ref(args.root, args.experiment, args.run),
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
