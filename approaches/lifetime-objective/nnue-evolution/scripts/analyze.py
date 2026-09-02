#!/usr/bin/env python3
"""Summarise every retained artifact of one EX-20260825-nnue-evolution-d3 run.

Reads runs/<RUN_ID>/nnue-evolution/ and writes analysis.json plus a
human-readable analysis.md next to it.  Handles partial runs: each stage is
reported only if its artifacts exist.  Nothing here changes a gate or
extrapolates; the preregistered pass criteria are evaluated from the
compare.py reports exactly as written in the experiment record.

Usage: analyze.py --run RUN_ID [--root REPO_ROOT]
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import statistics as st


def load_json(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def q(values, p):
    if not values:
        return None
    ordered = sorted(values)
    k = (len(ordered) - 1) * p
    lo, hi = int(k), min(int(k) + 1, len(ordered) - 1)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (k - lo)


def dist(values):
    if not values:
        return None
    return {
        "n": len(values),
        "mean": st.fmean(values),
        "sd": st.stdev(values) if len(values) > 1 else 0.0,
        "min": min(values),
        "q25": q(values, 0.25),
        "median": q(values, 0.5),
        "q75": q(values, 0.75),
        "max": max(values),
    }


def corpus_summary(out):
    parts = sorted(glob.glob(os.path.join(out, "corpus", "parts", "*.jsonl")))
    if not parts:
        return None
    games, roots = [], 0
    legal_counts, moves_remaining, best_values, spreads = [], [], [], []
    root_moves = []
    for part in parts:
        with open(part, encoding="utf-8") as handle:
            for line in handle:
                row = json.loads(line)
                if row["type"] == "game":
                    games.append(row)
                elif row["type"] == "root":
                    roots += 1
                    cols = row["columns"]
                    values = [v for _, v in cols]
                    legal_counts.append(len(cols))
                    moves_remaining.append(row["movesRemaining"])
                    best_values.append(max(values))
                    spreads.append(max(values) - min(values))
                    root_moves.append(row["move"])
    summary_path = os.path.join(out, "corpus", "corpus-summary.json")
    return {
        "partFiles": len(parts),
        "assembledSummary": load_json(summary_path) if os.path.exists(summary_path) else None,
        "games": len(games),
        "roots": roots,
        "censoredGames": sum(1 for g in games if g["censored"]),
        "score": dist([g["score"] for g in games]),
        "moves": dist([g["moves"] for g in games]),
        "wallSecondsPerGame": dist([g["wallSeconds"] for g in games]),
        "secondsPerRoot": (sum(g["wallSeconds"] for g in games) / roots) if roots else None,
        "rootsPerGame": (roots / len(games)) if games else None,
        "legalColumnsPerRoot": dist(legal_counts),
        "movesRemainingHistogram": {str(k): moves_remaining.count(k) for k in range(1, 6)},
        "teacherRootValuePoints": dist(best_values),
        "teacherSiblingSpreadPoints": dist(spreads),
        "rootMoveIndex": dist(root_moves),
        "seedsPlayed": [g["seed"] for g in games][:3] + ["..."] + [g["seed"] for g in games][-2:],
    }


def pretrain_summary(out):
    report = os.path.join(out, "pretrain", "report.json")
    if not os.path.exists(report):
        return None
    epochs = []
    log_path = os.path.join(out, "pretrain.log")
    if os.path.exists(log_path):
        for line in open(log_path, encoding="utf-8"):
            if line.startswith("epoch "):
                head, rest = line.split(":", 1)
                fields = rest.split()
                epochs.append({
                    "epoch": int(head.split()[1]),
                    "trainHuber": float(fields[1]),
                    "valHuber": float(fields[3]),
                    "valPearson": float(fields[5]),
                })
    probe = os.path.join(out, "pretrain", "probe.json")
    return {
        "report": load_json(report),
        "epochs": epochs,
        "probe": load_json(probe) if os.path.exists(probe) else None,
    }


def evolve_summary(out):
    progress = os.path.join(out, "evolve", "progress.jsonl")
    if not os.path.exists(progress):
        return None
    rows = [json.loads(line) for line in open(progress, encoding="utf-8") if line.strip()]
    gens = []
    for row in rows:
        fitness = sorted(row["fitness"], reverse=True)
        gens.append({
            "generation": row["generation"],
            "blockStart": row["blockStart"],
            "best": row["best"],
            "mean": row["mean"],
            "top4Mean": st.fmean(fitness[:4]),
            "median": q(row["fitness"], 0.5),
            "controlFair": row["controlFair"],
            "controlInit": row["controlInit"],
            "meanMinusFair": row["mean"] - row["controlFair"],
            "bestMinusFair": row["best"] - row["controlFair"],
            "top4MinusFair": st.fmean(fitness[:4]) - row["controlFair"],
            "initMinusFair": row["controlInit"] - row["controlFair"],
        })
    last10 = gens[-10:]
    # Per-generation artifact integrity (gate criterion: zero illegal and
    # zero incomplete decisions in every generation artifact).
    illegal = incomplete = censored = 0
    artifacts = 0
    for path in sorted(glob.glob(os.path.join(out, "evolve", "gen-*.json"))):
        artifacts += 1
        art = load_json(path)
        for ind in art["individuals"]:
            illegal += ind["illegalDecisions"]
            incomplete += ind["incompleteDecisions"]
            censored += ind["censoredGames"]
    config = os.path.join(out, "evolve", "config.json")
    return {
        "config": load_json(config) if os.path.exists(config) else None,
        "generationsCompleted": len(gens),
        "generations": gens,
        "trainingSignalCheck": {
            "rule": "population mean fitness > paired fair-d3s7 control in the majority of the final 10 completed generations (theory falsifier 2)",
            "window": [g["generation"] for g in last10],
            "generationsMeanAboveFair": sum(1 for g in last10 if g["meanMinusFair"] > 0),
            "generationsTop4AboveFair": sum(1 for g in last10 if g["top4MinusFair"] > 0),
            "generationsBestAboveFair": sum(1 for g in last10 if g["bestMinusFair"] > 0),
            "generationsInitAboveFair": sum(1 for g in last10 if g["initMinusFair"] > 0),
            "meanMarginLast10": st.fmean(g["meanMinusFair"] for g in last10) if last10 else None,
            "top4MarginLast10": st.fmean(g["top4MinusFair"] for g in last10) if last10 else None,
            "passed": (sum(1 for g in last10 if g["meanMinusFair"] > 0) > len(last10) / 2) if last10 else None,
        },
        "artifactIntegrity": {
            "generationArtifacts": artifacts,
            "illegalDecisions": illegal,
            "incompleteDecisions": incomplete,
            "censoredGames": censored,
        },
    }


def select_summary(out):
    path = os.path.join(out, "evolve", "selection.json")
    if not os.path.exists(path):
        return None
    lines = []
    log_path = os.path.join(out, "select.log")
    if os.path.exists(log_path):
        lines = [l.strip() for l in open(log_path, encoding="utf-8") if l.startswith("candidate-")]
    sha = os.path.join(out, "evolve", "candidate-weights.sha256")
    return {
        "selection": load_json(path),
        "finalistMeans": lines,
        "candidateSha256": open(sha, encoding="utf-8").read().split()[0] if os.path.exists(sha) else None,
    }


def screen_summary(out):
    heldout = os.path.join(out, "screen", "heldout.json")
    if not os.path.exists(heldout):
        return None
    art = load_json(heldout)
    arms = {ind["name"]: ind for ind in art["individuals"]}
    reports = {}
    for name in ("candidate-vs-fair-d3s7", "init-vs-fair-d3s7", "candidate-vs-init", "fair-d4s7-vs-fair-d3s7"):
        path = os.path.join(out, "screen", f"compare-{name}.json")
        if os.path.exists(path):
            reports[name] = load_json(path)
    gate = None
    primary = reports.get("candidate-vs-fair-d3s7")
    if primary:
        ps = primary["pairedScore"]
        cand, ref = primary["candidate"], primary["reference"]
        integrity_ok = all(
            arms[a]["illegalDecisions"] == 0 and arms[a]["incompleteDecisions"] == 0
            for a in arms
        )
        checks = [
            {"criterion": "screen artifact: illegalDecisions 0 and incompleteDecisions 0 in every arm", "passed": integrity_ok,
             "observed": {a: {"illegal": arms[a]["illegalDecisions"], "incomplete": arms[a]["incompleteDecisions"], "censored": arms[a]["censoredGames"]} for a in arms}},
            {"criterion": "bootstrap 95% lower bound of paired score delta > 0", "passed": ps["bootstrapLower95"] > 0, "observed": ps["bootstrapLower95"]},
            {"criterion": "Student-t 95% lower bound > 0", "passed": ps["studentTLower95"] > 0, "observed": ps["studentTLower95"]},
            {"criterion": "paired mean delta > 0 in both halves", "passed": ps["firstHalfMeanDelta"] > 0 and ps["secondHalfMeanDelta"] > 0,
             "observed": [ps["firstHalfMeanDelta"], ps["secondHalfMeanDelta"]]},
            {"criterion": "candidate Q25 >= fair-d3s7 Q25", "passed": cand["score"]["q25"] >= ref["score"]["q25"],
             "observed": [cand["score"]["q25"], ref["score"]["q25"]]},
        ]
        gate = {"checks": checks, "allPassed": all(c["passed"] for c in checks),
                "meanDelta": ps["meanDelta"], "pairedSd": ps["pairedSd"], "detectionFloor": ps["detectionFloor"],
                "wtl": [ps["wins"], ps["ties"], ps["losses"]]}
    return {
        "config": art["config"],
        "seedStartHex": art["seedStartHex"],
        "arms": {name: {"games": len(ind["games"]), "mean": st.fmean(g["score"] for g in ind["games"]),
                        "median": q([g["score"] for g in ind["games"]], 0.5),
                        "q25": q([g["score"] for g in ind["games"]], 0.25),
                        "max": max(g["score"] for g in ind["games"]),
                        "movesMean": st.fmean(g["moves"] for g in ind["games"]),
                        "clearsPerMove": ind["numberedClearsPerMove"], "revealsPerMove": ind["coverRevealsPerMove"],
                        "censored": ind["censoredGames"], "illegal": ind["illegalDecisions"], "incomplete": ind["incompleteDecisions"],
                        "wallSeconds": sum(g["wallSeconds"] for g in ind["games"])}
                 for name, ind in arms.items()},
        "paired": {name: {"meanDelta": r["pairedScore"]["meanDelta"], "bootstrapLower95": r["pairedScore"]["bootstrapLower95"],
                          "bootstrapUpper95": r["pairedScore"]["bootstrapUpper95"], "studentTLower95": r["pairedScore"]["studentTLower95"],
                          "pairedSd": r["pairedScore"]["pairedSd"], "detectionFloor": r["pairedScore"]["detectionFloor"],
                          "wtl": [r["pairedScore"]["wins"], r["pairedScore"]["ties"], r["pairedScore"]["losses"]],
                          "halves": [r["pairedScore"]["firstHalfMeanDelta"], r["pairedScore"]["secondHalfMeanDelta"]],
                          "q25Delta": r["pairedScore"]["q25Delta"], "movesDelta": r["pairedMoves"]["meanDelta"]}
                   for name, r in reports.items()},
        "gate": gate,
    }


def rusage_summary(out):
    path = os.path.join(out, "rusage.jsonl")
    if not os.path.exists(path):
        return None
    return [json.loads(l) for l in open(path, encoding="utf-8") if l.strip()]


def fmt(v, digits=0):
    if v is None:
        return "n/a"
    if isinstance(v, float):
        return f"{v:,.{digits}f}"
    return str(v)


def markdown(run_id, a):
    lines = [f"# Analysis of {run_id}", ""]
    c = a.get("corpus")
    if c:
        lines += ["## Stage A: teacher corpus (d5s7 fair leaf)", "",
                  f"- games {c['games']} (censored at the 500-move cap: {c['censoredGames']}), roots {c['roots']}, roots/game {fmt(c['rootsPerGame'],1)}",
                  f"- teacher score mean {fmt(c['score']['mean'])} median {fmt(c['score']['median'])} q25 {fmt(c['score']['q25'])} max {fmt(c['score']['max'])}",
                  f"- moves mean {fmt(c['moves']['mean'],1)} median {fmt(c['moves']['median'],1)} max {fmt(c['moves']['max'])}",
                  f"- wall per game mean {fmt(c['wallSecondsPerGame']['mean'],1)} s; per root {fmt(c['secondsPerRoot'],2)} s (thread-local wall)",
                  f"- legal columns per root mean {fmt(c['legalColumnsPerRoot']['mean'],2)}; teacher sibling spread mean {fmt(c['teacherSiblingSpreadPoints']['mean'])} points", ""]
    p = a.get("pretrain")
    if p:
        r = p["report"]
        lines += ["## Stage B: supervised warm start", "",
                  f"- {r['roots']} roots from {r['games']} games; whole-origin split {r['trainRoots']} train / {r['valRoots']} val; best epoch {r['bestEpoch']} (val Huber {r['bestValHuber']:.4f})"]
        for e in p["epochs"]:
            lines.append(f"  - epoch {e['epoch']}: train {e['trainHuber']:.4f} val {e['valHuber']:.4f} Pearson {e['valPearson']:.4f}")
        if p["probe"]:
            lines.append(f"- deployment-faithful ordering probe: {p['probe']['probeRoots']} held-out roots, top-1 {p['probe']['top1']:.4f}, mean teacher-value regret {p['probe']['meanTeacherValueRegret']:.0f} points")
        lines.append("")
    e = a.get("evolve")
    if e:
        lines += ["## Stage C: evolution", "",
                  f"- generations completed {e['generationsCompleted']}; artifacts {e['artifactIntegrity']['generationArtifacts']}, illegal {e['artifactIntegrity']['illegalDecisions']}, incomplete {e['artifactIntegrity']['incompleteDecisions']}, censored {e['artifactIntegrity']['censoredGames']}",
                  "", "| gen | best | top-4 mean | pop mean | fair ctrl | init ctrl | mean-fair | top4-fair |", "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
        for g in e["generations"]:
            lines.append(f"| {g['generation']} | {fmt(g['best'])} | {fmt(g['top4Mean'])} | {fmt(g['mean'])} | {fmt(g['controlFair'])} | {fmt(g['controlInit'])} | {fmt(g['meanMinusFair'])} | {fmt(g['top4MinusFair'])} |")
        t = e["trainingSignalCheck"]
        lines += ["", f"- training-signal check (last {len(t['window'])} generations): population mean above fair in {t['generationsMeanAboveFair']}, top-4 above fair in {t['generationsTop4AboveFair']}, best above fair in {t['generationsBestAboveFair']}, init above fair in {t['generationsInitAboveFair']}; mean margin {fmt(t['meanMarginLast10'])}, top-4 margin {fmt(t['top4MarginLast10'])}; passed: {t['passed']}", ""]
    s = a.get("select")
    if s:
        lines += ["## Stage C: elite re-selection", "", f"- {s['selection']}"]
        lines += [f"  - {l}" for l in s["finalistMeans"]]
        lines += [f"- candidate-weights.bin sha256 {s['candidateSha256']}", ""]
    sc = a.get("screen")
    if sc:
        lines += ["## Stage D: held-out screen", "", f"- seeds from {sc['seedStartHex']}, {sc['config']}", "",
                  "| arm | mean | median | q25 | max | moves | clears/move | reveals/move | censored | illegal | incomplete |", "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
        for name, arm in sc["arms"].items():
            lines.append(f"| {name} | {fmt(arm['mean'])} | {fmt(arm['median'])} | {fmt(arm['q25'])} | {fmt(arm['max'])} | {fmt(arm['movesMean'],1)} | {arm['clearsPerMove']:.3f} | {arm['revealsPerMove']:.3f} | {arm['censored']} | {arm['illegal']} | {arm['incomplete']} |")
        lines += ["", "| contrast | delta | boot LB95 | boot UB95 | t LB95 | paired sd | floor | W-T-L | halves | q25 delta |", "| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | ---: |"]
        for name, r in sc["paired"].items():
            lines.append(f"| {name} | {fmt(r['meanDelta'])} | {fmt(r['bootstrapLower95'])} | {fmt(r['bootstrapUpper95'])} | {fmt(r['studentTLower95'])} | {fmt(r['pairedSd'])} | {fmt(r['detectionFloor'])} | {'-'.join(map(str, r['wtl']))} | {fmt(r['halves'][0])} / {fmt(r['halves'][1])} | {fmt(r['q25Delta'])} |")
        if sc["gate"]:
            lines += ["", "### Preregistered gate", ""]
            for chk in sc["gate"]["checks"]:
                lines.append(f"- [{'x' if chk['passed'] else ' '}] {chk['criterion']}: {chk['observed']}")
            lines.append(f"- **all criteria passed: {sc['gate']['allPassed']}**")
        lines.append("")
    ru = a.get("rusage")
    if ru:
        lines += ["## Resource usage (kernel rusage per stage)", "", "| stage | wall s | user s | sys s | peak RSS MB | exit |", "| --- | ---: | ---: | ---: | ---: | ---: |"]
        for r in ru:
            lines.append(f"| {r['label']} | {r['wallSeconds']:.0f} | {r['userSeconds']:.0f} | {r['systemSeconds']:.0f} | {r['peakRssBytes']/1e6:.0f} | {r['exitCode']} |")
        lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", required=True)
    parser.add_argument("--root", default=os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
    args = parser.parse_args()
    out = os.path.join(os.path.abspath(args.root), "runs", args.run, "nnue-evolution")
    analysis = {
        "runId": args.run,
        "corpus": corpus_summary(out),
        "pretrain": pretrain_summary(out),
        "evolve": evolve_summary(out),
        "select": select_summary(out),
        "screen": screen_summary(out),
        "rusage": rusage_summary(out),
    }
    with open(os.path.join(out, "analysis.json"), "w", encoding="utf-8") as handle:
        json.dump(analysis, handle, indent=2)
        handle.write("\n")
    text = markdown(args.run, analysis)
    with open(os.path.join(out, "analysis.md"), "w", encoding="utf-8") as handle:
        handle.write(text + "\n")
    print(text)


if __name__ == "__main__":
    main()
