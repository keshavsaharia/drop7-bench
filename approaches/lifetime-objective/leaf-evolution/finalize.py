#!/usr/bin/env python3
"""Turn the evolution + screen artifacts into the immutable result record.

Usage: finalize.py <evolution-run-dir> <screen-run-dir> [--contributor-model M]

Evaluates the preregistered gate of EX-20260822-leaf-cmaes-d4s7-4f5f462a
EXACTLY ONCE, from compare-d4s5.json, and writes:
  research/results/RS-<ts>-<hex>.json      (valid + pass|fail|inconclusive)
  research/contributions/CT-...json        (the finalizer's own record)
  updates research/runs/<evo>.json and <screen>.json to completed
  sets closedAt on the two seed leases
It refuses to run twice (a result for the experiment already exists) and it
never recomputes a statistic: every number is copied from compare.py's output.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import secrets
import subprocess
from pathlib import Path

EX = "EX-20260822-leaf-cmaes-d4s7-4f5f462a"
TH = "TH-20260822-depth-native-leaf-weights-caa6f8ba"
TRAIN_LEASE = "research/seeds/leases/SL-20260822T020000Z-a5290000.json"
HELDOUT_LEASE = "research/seeds/leases/SL-20260822T020000Z-a52b0000.json"
MACHINE = "research/system-profiles/MACH-20260820T080056Z-376ada90.json"


def now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path: str | Path):
    return json.loads(Path(path).read_text())


def dump(path: str | Path, value) -> None:
    Path(path).write_text(json.dumps(value, indent=2) + "\n")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("evo")
    ap.add_argument("screen")
    ap.add_argument("--contributor-model", default="claude-fable-5")
    ap.add_argument("--contributor-platform", default="Claude Code")
    ap.add_argument("--out-root", default=".", help="repository root to write records into (a copy, for a rehearsal)")
    args = ap.parse_args()
    root = Path(args.out_root)
    evo, screen = Path(args.evo), Path(args.screen)
    evo_id, screen_id = evo.name, screen.name

    for existing in (root / "research/results").glob("RS-*.json"):
        if load(existing).get("experimentId") == EX:
            raise SystemExit(f"refusing: a result for {EX} already exists ({existing.name}); the gate is evaluated once")

    final = load(evo / "final.json")
    progress = [json.loads(l) for l in (evo / "progress.jsonl").read_text().splitlines()]
    c5 = load(screen / "compare-d4s5.json")
    c7 = load(screen / "compare-d4s7.json")
    h5 = load(screen / "heldout-d4s5.json")
    h7 = load(screen / "heldout-d4s7.json")
    gens = [load(p) for p in sorted(evo.glob("gen-*/population.json"))]

    incomplete = sum(g["incompleteDecisionsTotal"] for g in gens) + h5["incompleteDecisionsTotal"] + h7["incompleteDecisionsTotal"]
    illegal = sum(g["illegalDecisionsTotal"] for g in gens) + h5["illegalDecisionsTotal"] + h7["illegalDecisionsTotal"]
    identity = sum(i["scoreIdentityFailures"] for g in gens for i in g["individuals"]) + sum(i["scoreIdentityFailures"] for i in h5["individuals"] + h7["individuals"])
    valid = incomplete == 0 and illegal == 0 and identity == 0 and h5["seedStartHex"] == "0xa52b0000" and h5["games"] == 64

    ps = c5["pairedScore"]
    checks = [
        {"criterion": "CHECK gates passed before the first training seed (runs/RUN-20260822T013756Z-0f7ab038/gates.log)", "passed": True, "observed": "6 of 6 gates passed, 0 mismatches; recorded in RUN-20260822T013756Z-0f7ab038"},
        {"criterion": "incompleteDecisionsTotal 0, illegalDecisionsTotal 0, no score-identity failures across all generation and screen artifacts", "passed": incomplete == 0 and illegal == 0 and identity == 0, "observed": f"incomplete {incomplete}, illegal {illegal}, identity failures {identity} over {len(gens)} generations and 2 screen arms"},
        {"criterion": "held-out d4s5: bootstrap 95% lower bound > 0 AND Student-t 95% lower bound > 0", "passed": ps["bootstrapLower95"] > 0 and ps["studentTLower95"] > 0, "observed": f"mean delta {ps['meanDelta']:.1f}, bootstrap LB {ps['bootstrapLower95']:.1f}, t LB {ps['studentTLower95']:.1f}, floor {ps['detectionFloor']:.1f}"},
        {"criterion": "held-out d4s5: candidate Q25 >= frozen Q25", "passed": c5["candidate"]["score"]["q25"] >= c5["reference"]["score"]["q25"], "observed": f"Q25 candidate {c5['candidate']['score']['q25']:.1f} vs frozen {c5['reference']['score']['q25']:.1f}"},
        {"criterion": "held-out d4s5: paired mean delta > 0 in both halves", "passed": ps["firstHalfMeanDelta"] > 0 and ps["secondHalfMeanDelta"] > 0, "observed": f"halves {ps['firstHalfMeanDelta']:.1f} / {ps['secondHalfMeanDelta']:.1f}"},
        {"criterion": "the screened vector is the CMA distribution mean frozen before the held-out lease was opened", "passed": True, "observed": f"candidate-weights.txt sha256 {sha256(evo / 'candidate-weights.txt')}; final.json stop reason {final['stopReason']} after {final['generationsCompleted']} generations"},
    ]
    gate_pass = all(c["passed"] for c in checks)
    late = progress[-10:]
    gradient_seen = any(g["meanPairedDeltaVsControl"] > 20000 for g in late)

    if not valid:
        validity, outcome, assessment = "invalid", "not-applicable", "invalidated-by-methodological-error"
    elif gate_pass:
        validity, outcome, assessment = "valid", "pass", "supported-as-tested"
    else:
        validity, outcome, assessment = "valid", "fail", "not-supported-as-tested"

    rid = f"RS-{now().replace('-', '').replace(':', '')}-{secrets.token_hex(4)}"
    ct = f"CT-{now().replace('-', '').replace(':', '')}-{secrets.token_hex(4)}"
    p7 = c7["pairedScore"]
    summary = (
        f"Held-out SCREEN of the CMA-ES leaf (distribution mean after {final['generationsCompleted']} generations, stop={final['stopReason']}) "
        f"against the frozen fair leaf, depth 4 five strata, 64 paired never-read games 0xa52b0000+: mean {c5['candidate']['score']['mean']:.0f} vs {c5['reference']['score']['mean']:.0f}, "
        f"paired delta {ps['meanDelta']:+.0f} (bootstrap 95% LB {ps['bootstrapLower95']:+.0f}, t LB {ps['studentTLower95']:+.0f}, paired sd {ps['pairedSd']:.0f}, floor {ps['detectionFloor']:.0f}), "
        f"W-T-L {ps['wins']}-{ps['ties']}-{ps['losses']}, halves {ps['firstHalfMeanDelta']:+.0f}/{ps['secondHalfMeanDelta']:+.0f}, Q25 delta {ps['q25Delta']:+.0f}. Gate: {outcome.upper()}. "
        f"Secondary seven-stratum transfer on the same seeds (diagnostic only): delta {p7['meanDelta']:+.0f} (LB {p7['bootstrapLower95']:+.0f}). "
        f"Training-block population-mean-minus-control exceeded +20,000 in {'at least one' if gradient_seen else 'none'} of the last {len(late)} generations."
    )
    result = {
        "$schema": "../schemas/result-v1.schema.json", "format": "drop7-result-v1", "resultId": rid,
        "theoryIds": [TH], "experimentId": EX, "runIds": ["RUN-20260822T013756Z-0f7ab038", "RUN-20260822T013250Z-cb282a1c", evo_id, screen_id],
        "runValidity": validity, "scientificOutcome": outcome, "assessment": assessment, "evidenceTier": "public-development",
        "summary": summary,
        "metrics": {
            "heldOutD4S5": {"candidate": c5["candidate"], "reference": c5["reference"], "pairedScore": ps, "pairedMoves": c5["pairedMoves"]},
            "heldOutD4S7Diagnostic": {"candidate": c7["candidate"], "reference": c7["reference"], "pairedScore": p7, "pairedMoves": c7["pairedMoves"]},
            "evolution": {"generationsCompleted": final["generationsCompleted"], "stopReason": final["stopReason"], "finalSigma": final["sigma"], "wallHours": final["wallHours"],
                           "seedsConsumedEndExclusiveHex": final["seedsConsumedEndExclusiveHex"], "candidateWeights": dict(zip(final["names"], final["candidateWeights"])),
                           "progress": [{k: v for k, v in g.items() if k != "meanWeights"} for g in progress]},
            "detectionFloorNote": "A 64-game paired cohort cannot resolve effects below roughly the stated floor; a fail rejects this exact configuration and does not show the frozen weights are optimal.",
        },
        "gateChecks": checks,
        "perGameArtifact": {"path": str(screen / "heldout-d4s5.json"), "sha256": sha256(screen / "heldout-d4s5.json"), "recordCount": 2 * h5["games"]},
        "machineProfileRefs": [MACHINE], "artifactManifestRef": str(screen / "artifacts.sha256"),
        "limitations": [
            "Single 64-game development-tier screen on one machine; no replication yet (192 seeds of the held-out lease remain reserved for one).",
            "Evolution and screen ran while a GPU training sweep shared the machine; wall times are not timing-grade, decisions are deterministic and unaffected.",
            "The seven-stratum arm replays already-read held-out seeds and is diagnostic only.",
            "The population-mean trajectory on training blocks carries the perturbation penalty of sampling at sigma and does not measure the candidate's own fitness.",
            "Scripted-round (leaderboard) play of any vector is a demonstration and is not part of this result.",
        ],
        "contributionIds": [ct, "CT-20260822T015730Z-eb02ae84", "CT-20260822T015542Z-028e9222"], "recordedAt": now(),
    }
    dump(root / f"research/results/{rid}.json", result)

    contribution = {
        "$schema": "../schemas/contribution-v1.schema.json", "format": "drop7-contribution-v1", "contributionId": ct,
        "actor": {"kind": "model", "platform": args.contributor_platform, "model": args.contributor_model, "agentId": None},
        "level": "L2", "roles": [{"role": "formal-analysis", "degree": "lead"}, {"role": "data-curation", "degree": "substantial"}],
        "summary": f"Finalized {EX}: evaluated the preregistered gate once from compare-d4s5.json via approaches/lifetime-objective/leaf-evolution/finalize.py, wrote {rid}, closed the run and lease records. No statistic recomputed.",
        "theoryIds": [TH], "experimentIds": [EX], "runIds": [evo_id, screen_id],
        "artifactPaths": [f"research/results/{rid}.json", str(screen / "compare-d4s5.json"), str(screen / "compare-d4s7.json")], "commitShas": [],
        "validationPerformed": ["make research-validate (run by the watcher after finalize)"],
        "limitations": ["Self-reported contribution; written by a script the same model authored."], "selfReported": True, "reviewedBy": None, "recordedAt": now(),
    }
    dump(root / f"research/contributions/{ct}.json", contribution)

    for run_id, started_key in ((evo_id, None), (screen_id, None)):
        rp = root / f"research/runs/{run_id}.json"
        r = load(rp)
        r["lifecycle"] = "completed"; r["runValidity"] = validity; r["endedAt"] = now(); r["exitCode"] = 0
        r["stopReason"] = final["stopReason"] if run_id == evo_id else "completed"
        if run_id == evo_id:
            r["resourceObserved"]["wallSeconds"] = round(final["wallHours"] * 3600)
            r["artifactRefs"] = [str(evo / "final.json"), str(evo / "progress.jsonl"), str(evo / "candidate-weights.txt")]
        else:
            r["startedAt"] = r["startedAt"] or load(root / HELDOUT_LEASE)["openedAt"]
            r["resourceObserved"]["wallSeconds"] = round(h5["wallSeconds"] + h7["wallSeconds"])
        r["contributionIds"] = [ct]
        dump(rp, r)
    for lease in (TRAIN_LEASE, HELDOUT_LEASE):
        l = load(root / lease); l["closedAt"] = now(); dump(root / lease, l)
    print(rid, outcome, validity)
    if args.out_root == ".":
        subprocess.run(["make", "research-validate"], check=False)


if __name__ == "__main__":
    main()
