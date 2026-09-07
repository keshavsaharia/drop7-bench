#!/usr/bin/env python3
"""Draft the result record for the search-target (TreeStrap) experiment
(EX-20260907-ntuple-treestrap-*) from its analysis.json.

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

Usage: write-tree-result-record.py --run RUN_ID --result-id RS-... --root REPO
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
CONTROL_SHA256 = "92dd1cb2d2a74b026270606c18c5f0d6e4f4ccc74e64c3c4e0f0d2043cdddd90"


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
    fill = screen["tree"]
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
    hashes = {name: read_hash(out, name) for name in ("prior", "candidate", "searchtd", "control")}
    selection = (analysis.get("pilot") or {}).get("selection") or {}
    pilot_arms = (analysis.get("pilot") or {}).get("arms") or {}
    primary = contrasts["treestrap-d3s7-vs-prior-d3s7"]
    f3, p3 = arms["treestrap-d3s7"], arms["prior-d3s7"]
    falsifiers = fill["theory"]
    offpath = fill.get("offPathBoards")
    ablation = fill.get("ablation")
    if passed and offpath and offpath["verdict"] == "supported":
        assessment = "supported-as-tested"
    elif falsifiers["primaryFalsifierUpperBoundBelowZero"]:
        assessment = "not-supported-as-tested"
    elif passed:
        assessment = "mixed"
    elif primary["bootstrapUpper95"] > 0:
        assessment = "mixed" if any(r and r["verdict"] == "supported" for r in (ablation, fill.get("vsOnePlyContinuation"))) else "not-supported-as-tested"
    else:
        assessment = "not-supported-as-tested"

    arm_names = list(arms)
    summary = (
        f"Held-out screen, {games} never-read paired public-development games ({screen['seedStartHex']}+), {len(arm_names)} arms on identical seeds. "
        f"Candidate arm {selection.get('candidateArm')} by protocol (best validation margin {fmt(selection.get('candidateBestMargin', 0))} at {fmt(selection.get('candidateBestMoves', 0))} searched moves; the ablation arm {selection.get('ablationArm')} best {fmt(selection.get('ablationBestMargin', 0))} at {fmt(selection.get('ablationBestMoves', 0))}; the warm start's own margin on the validation block {json.dumps({n: a.get('warmStartMargin') for n, a in selection.get('arms', {}).items()})}; training-signal check passed {selection.get('trainingSignal', {}).get('passed')}). "
        f"The TreeStrap-trained tables (SHA-256 {hashes['candidate']}) as the depth-3 leaf averaged {fmt(f3['meanScore'])} points and {f3['meanMoves']:.2f} moves against "
        f"{fmt(p3['meanScore'])} and {p3['meanMoves']:.2f} for the unchanged frozen tables (SHA-256 {hashes['prior']}) in the same search: "
        f"{contrast_sentence('treestrap-d3s7 minus prior-d3s7', primary)}, lower quartile {fmt(primary['candidateQ25'])} vs {fmt(primary['referenceQ25'])}, "
        f"moves {moves['treestrap-d3s7-vs-prior-d3s7']['meanDelta']:+.2f}. The preregistered gate {'PASSES' if passed else 'FAILS'}. "
    )
    for key, label in (("offPathBoards", "Off-path boards, treestrap-d3s7 minus searchtd-d3s7"), ("ablation", "Search targets at visited states only, searchtd-d3s7 minus prior-d3s7"), ("vsOnePlyContinuation", "Against the one-ply continuation, treestrap-d3s7 minus control-d3s7"), ("treestrapDepth4", "At depth 4, treestrap-d4s7 minus prior-d4s7"), ("ablationDepth4", "At depth 4, searchtd-d4s7 minus prior-d4s7")):
        r = fill.get(key)
        if r:
            summary += f"{contrast_sentence(label, r)}: verdict '{r['verdict']}'" + (f" (four criteria {'PASS' if r['passed'] else 'FAIL'})" if "passed" in r else "") + ". "
    for name in ("treestrap-d3s7", "searchtd-d3s7", "control-d3s7", "prior-d3s7", "prior-d4s7", "treestrap-d4s7", "searchtd-d4s7", "treestrap-1ply", "prior-1ply", "fair-d3s7"):
        if name in arms:
            summary += f"{name} {fmt(arms[name]['meanScore'])} / {arms[name]['meanMoves']:.2f} moves; "
    for name in ("treestrap-d4s7-vs-treestrap-d3s7", "searchtd-d4s7-vs-searchtd-d3s7", "prior-d4s7-vs-prior-d3s7", "control-d3s7-vs-prior-d3s7", "prior-d3s7-vs-fair-d3s7", "treestrap-d3s7-vs-fair-d3s7", "treestrap-1ply-vs-prior-1ply"):
        if name in contrasts:
            c = contrasts[name]
            summary += f"{name}: {c['meanDelta']:+,.0f} (LB {c['bootstrapLower95']:+,.0f}, UB {c['bootstrapUpper95']:+,.0f}). "
    for name in ("searchtd", "treestrap"):
        a = pilot_arms.get(name)
        if a:
            internal = sum(c.get("internalUpdates") or 0 for c in a["curve"])
            summary += f"Arm {name}: {fmt(a['movesTotal'])} searched moves in {fmt(a['gamesTotal'])} games, {len(a['validations'])} validation points (point 0 = warm start {fmt(a['start']['pairedDeltaD3']) if a.get('start') else 'n/a'}), best margin {fmt(a['bestMargin'])} at {fmt(a['best']['moves']) if a.get('best') else 'n/a'} moves, final {fmt(a['finalMargin'])}, stop {a['stop']['reason'] if a.get('stop') else 'n/a'}, {fmt(a['meanMovesPerSecond'])} moves/s, {fmt(internal)} internal-node updates. "
    summary += (
        "Logical work per game: " + ", ".join(f"{n} {fmt(arms[n]['meanWork'])}" for n in ("treestrap-d3s7", "prior-d3s7", "treestrap-d4s7", "prior-d4s7") if n in arms) + ". "
        f"Theory falsifiers: {json.dumps(falsifiers)}."
    )

    checks = [
        {"criterion": "All CHECK gates passed on the frozen tables on the probe block before any leased seed was read, including train-search-vs-engine", "passed": bool(analysis["gates"] and analysis["gates"]["passed"] and any("train-search-vs-engine" in g and g.startswith("PASS") for g in analysis["gates"]["gates"])), "observed": f"{len(analysis['gates']['gates'])} gate lines"},
        {"criterion": "gate --weights passed on each of the two new table files before the screen lease opened", "passed": bool(analysis.get("frozenGates")) and all(g["passed"] for g in analysis["frozenGates"].values()) and set(analysis["frozenGates"]) == {"candidate", "searchtd"}, "observed": json.dumps({k: v["passed"] for k, v in (analysis.get("frozenGates") or {}).items()})},
    ]
    for c in gate["checks"]:
        checks.append({"criterion": c["criterion"], "passed": bool(c["passed"]), "observed": json.dumps(c.get("observed"))})
    checks.append({"criterion": "The prior arms are the exact frozen best-weights.bin of RUN-20260905T193006Z-4fbeb4e5 and the control arm the fill experiment's control file (SHA-256 verified before any stage read them); the candidate and searchtd files' SHA-256 were written before the screen lease opened", "passed": hashes["prior"] == PRIOR_SHA256 and hashes["control"] == CONTROL_SHA256 and all(hashes[n] for n in ("candidate", "searchtd")), "observed": json.dumps(hashes)})
    for key, label in (("offPathBoards", "off-path verdict (treestrap-d3s7 vs searchtd-d3s7)"), ("ablation", "ablation reading (searchtd-d3s7 vs prior-d3s7)"), ("vsOnePlyContinuation", "one-ply reference (treestrap-d3s7 vs control-d3s7)"), ("treestrapDepth4", "treestrap at depth 4 verdict (vs prior-d4s7)"), ("ablationDepth4", "searchtd at depth 4 verdict (vs prior-d4s7)")):
        r = fill.get(key)
        if r:
            checks.append({"criterion": f"Beside the gate: {label}: supported / refuted / inconclusive", "passed": None, "observed": json.dumps({"verdict": r["verdict"], "meanDelta": r["meanDelta"], "bootstrapLower95": r["bootstrapLower95"], "bootstrapUpper95": r["bootstrapUpper95"]})})
    checks.append({"criterion": "Beside the gate: training-signal check (the treestrap arm's best validation margin exceeds the warm start's own margin on the same block)", "passed": None, "observed": json.dumps(selection.get("trainingSignal"))})

    metrics = {
        "screen": {
            "games": games,
            "seedStartHex": screen["seedStartHex"],
            "arms": {n: {k: a[k] for k in ("meanScore", "medianScore", "q25Score", "minScore", "maxScore", "sdScore", "meanMoves", "q25Moves", "numberedClearsPerMove", "coverRevealsPerMove", "meanOccupiedCells", "censoredGames", "illegalDecisions", "incompleteDecisions", "meanWork", "meanWallSecondsPerGame", "games") if k in a} for n, a in arms.items()},
            "contrasts": screen["contrasts"],
            "tree": fill,
        },
        "selection": selection,
        "trainingArms": {n: {k: a[k] for k in ("layout", "movesTotal", "gamesTotal", "wallSeconds", "meanMovesPerSecond", "finalMargin", "bestMargin", "best", "start", "stop") if k in a} | {"internalUpdates": sum(c.get("internalUpdates") or 0 for c in a["curve"]), "rootUpdates": sum(c.get("rootUpdates") or 0 for c in a["curve"]), "leafCalls": sum(c.get("leafCalls") or 0 for c in a["curve"]), "validationPoints": [{"movesTrained": v["movesTrained"], "pairedDeltaD3": v["pairedD3VsFair"]["meanDelta"], "bootstrapLower95": v["pairedD3VsFair"]["bootstrapLower95"], "ntupleD3Mean": v["arms"]["ntuple-d3s7"]["meanScore"], "directMean": v["arms"].get("ntuple-1ply", {}).get("meanScore")} for v in a["validations"]]} for n, a in pilot_arms.items()},
        "smoke": {k: analysis["smoke"][k] for k in ("movesTotal", "gamesTotal", "wallSeconds", "meanMovesPerSecond") if analysis.get("smoke") and k in analysis["smoke"]} if analysis.get("smoke") else None,
        "tablesSha256": hashes,
    }
    limitations = [
        f"Public-development SCREEN tier, {games} paired games opened once; nothing here is a qualification claim, and protected and final cohorts stay sealed.",
        "The candidate is the treestrap arm's best validation point and the ablation the searchtd arm's, each chosen on the 256-game training-role validation block; the screen inherits that selection and measures what survives it.",
        "The training arms are Hogwild (32 lock-free workers) and are not bit-reproducible; a re-run trains different tables from the same warm start and seeds.",
        "Only one step-size configuration was tested (alpha 1.0 with fresh coherence accumulators on a warm start, the fill experiment's recipe); a gentler step, or carrying the frozen tables' own accumulators, is a different configuration and is the successor this record names.",
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
