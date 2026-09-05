#!/usr/bin/env python3
"""Summarise every retained artifact of one ntuple-scale run.

Reads runs/<RUN_ID>/ntuple-scale/ and writes analysis.json plus analysis.md
next to it.  Handles partial runs: each stage is reported only if its
artifacts exist.  Paired statistics use the same bootstrap as the
leaf-evolution compare.py (20,000 resamples, seed 0xb0071eaf).  The
preregistered pass criteria are evaluated from the screen artifact exactly as
written in the experiment record; nothing here extrapolates.

Usage: analyze.py --run RUN_ID [--root REPO_ROOT] [--select-arm]

With --select-arm the script prints (to stdout) the pilot selection JSON:
the arm whose final validation point has the largest paired mean margin of
ntuple-d3s7 over fair-d3s7, ties by fewer table entries.
"""
from __future__ import annotations

import argparse
import glob
import json
import math
import os
import statistics as st

import numpy as np

BOOTSTRAP_SEED = 0xB007_1EAF
PILOT_ARMS = {
    "A": ("rows,cols,win23,win32,phase=cols", 1.0),
    "B": ("rows,cols,win23,win32,phase=none", 1.0),
    "C": ("rows,cols,win23,win32,phase=all", 1.0),
    "D": ("rows,cols,phase=cols", 1.0),
    "E": ("win23,win32,phase=none", 1.0),
    "F": ("rows,cols,win23,win32,phase=cols", 0.25),
}


def load_json(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def load_json_if_complete(path):
    """A JSON artifact, or None when the file is absent or still empty (a
    shell redirection creates the target before its producer writes)."""
    if not os.path.exists(path) or os.path.getsize(path) == 0:
        return None
    return load_json(path)


def load_jsonl(path):
    rows = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if line.strip():
                rows.append(json.loads(line))
    return rows


def t_quantile_95(df: int) -> float:
    z = 1.6448536269514722
    g1 = (z ** 3 + z) / 4
    g2 = (5 * z ** 5 + 16 * z ** 3 + 3 * z) / 96
    g3 = (3 * z ** 7 + 19 * z ** 5 + 17 * z ** 3 - 15 * z) / 384
    g4 = (79 * z ** 9 + 776 * z ** 7 + 1482 * z ** 5 - 1920 * z ** 3 - 945 * z) / 92160
    return z + g1 / df + g2 / df ** 2 + g3 / df ** 3 + g4 / df ** 4


def paired(cand, ref, key="score"):
    a = np.array([g[key] for g in cand["games"]], dtype=float)
    b = np.array([g[key] for g in ref["games"]], dtype=float)
    assert [g["seedHex"] for g in cand["games"]] == [g["seedHex"] for g in ref["games"]]
    d = a - b
    n = len(d)
    rng = np.random.default_rng(BOOTSTRAP_SEED)
    boots = d[rng.integers(0, n, size=(20000, n))].mean(axis=1)
    sd = float(d.std(ddof=1)) if n > 1 else 0.0
    half = n // 2
    return {
        "n": n,
        "meanDelta": float(d.mean()),
        "pairedSd": sd,
        "bootstrapLower95": float(np.quantile(boots, 0.05)),
        "bootstrapUpper95": float(np.quantile(boots, 0.95)),
        "studentTLower95": float(d.mean() - t_quantile_95(n - 1) * sd / math.sqrt(n)) if n > 1 else float(d.mean()),
        "detectionFloor": 1.645 * sd / math.sqrt(n) if n > 1 else 0.0,
        "wins": int((d > 0).sum()),
        "ties": int((d == 0).sum()),
        "losses": int((d < 0).sum()),
        "firstHalfMeanDelta": float(d[:half].mean()),
        "secondHalfMeanDelta": float(d[half:].mean()),
        "q25Delta": float(np.quantile(a, 0.25) - np.quantile(b, 0.25)),
        "candidateQ25": float(np.quantile(a, 0.25)),
        "referenceQ25": float(np.quantile(b, 0.25)),
    }


def arm_summary(ind):
    s = np.array([g["score"] for g in ind["games"]], dtype=float)
    m = np.array([g["moves"] for g in ind["games"]], dtype=float)
    return {
        "name": ind["name"],
        "games": len(s),
        "meanScore": float(s.mean()),
        "medianScore": float(np.median(s)),
        "sdScore": float(s.std(ddof=1)) if len(s) > 1 else 0.0,
        "q25Score": float(np.quantile(s, 0.25)),
        "minScore": float(s.min()),
        "maxScore": float(s.max()),
        "meanMoves": float(m.mean()),
        "q25Moves": float(np.quantile(m, 0.25)),
        "numberedClearsPerMove": ind["numberedClearsPerMove"],
        "coverRevealsPerMove": ind["coverRevealsPerMove"],
        "meanOccupiedCells": float(np.mean([g["meanOccupiedCells"] for g in ind["games"]])),
        "censoredGames": ind["censoredGames"],
        "incompleteDecisions": ind["incompleteDecisions"],
        "illegalDecisions": ind["illegalDecisions"],
        "gamesAtOrAboveMillion": int((s >= 1_000_000).sum()),
        "meanWallSecondsPerGame": float(np.mean([g["wallSeconds"] for g in ind["games"]])),
    }


def training_summary(directory):
    """One training directory (a pilot arm or the main run)."""
    progress_path = os.path.join(directory, "progress.jsonl")
    if not os.path.exists(progress_path):
        return None
    config = load_json_if_complete(os.path.join(directory, "config.json"))
    rows = load_jsonl(progress_path)
    curve = []
    for row in rows:
        curve.append({
            "chunk": row["chunk"],
            "movesTotal": row["movesTotal"],
            "gamesTotal": row["gamesTotal"],
            "wallSeconds": row["wallSeconds"],
            "movesPerSecond": row["movesPerSecond"],
            "trainMeanScore": row["trainMeanScore"],
            "trainMeanMoves": row["trainMeanMoves"],
            "trainMaxScore": row["trainMaxScore"],
            "trainClearsPerMove": row["trainClearsPerMove"],
            "trainRevealsPerMove": row["trainRevealsPerMove"],
            "meanAbsDelta": row["meanAbsDelta"],
            "meanBeta": row["meanBeta"],
            "seedWraps": row.get("seedWraps"),
            "quick": row.get("quick"),
            "validation": row.get("validation"),
        })
    validations = []
    for path in sorted(glob.glob(os.path.join(directory, "val-*.json"))):
        art = load_json(path)
        by = {i["name"]: i for i in art["individuals"]}
        entry = {
            "artifact": os.path.basename(path),
            "movesTrained": art["config"]["movesTrained"],
            "arms": {name: arm_summary(ind) for name, ind in by.items()},
        }
        if "ntuple-d3s7" in by and "fair-d3s7" in by:
            entry["pairedD3VsFair"] = paired(by["ntuple-d3s7"], by["fair-d3s7"])
            entry["pairedD3VsFairMoves"] = paired(by["ntuple-d3s7"], by["fair-d3s7"], "moves")
        if "ntuple-1ply" in by and "fair-d3s7" in by:
            entry["paired1plyVsFair"] = paired(by["ntuple-1ply"], by["fair-d3s7"])
        validations.append(entry)
    illegal = sum(sum(i["illegalDecisions"] for i in load_json(p)["individuals"]) for p in glob.glob(os.path.join(directory, "val-*.json")))
    incomplete = sum(sum(i["incompleteDecisions"] for i in load_json(p)["individuals"]) for p in glob.glob(os.path.join(directory, "val-*.json")))
    best = load_json_if_complete(os.path.join(directory, "best.json"))
    return {
        "config": config,
        "chunks": len(rows),
        "movesTotal": rows[-1]["movesTotal"] if rows else 0,
        "gamesTotal": rows[-1]["gamesTotal"] if rows else 0,
        "wallSeconds": rows[-1]["wallSeconds"] if rows else 0,
        "meanMovesPerSecond": st.fmean(r["movesPerSecond"] for r in rows) if rows else None,
        "done": os.path.exists(os.path.join(directory, "DONE")),
        "curve": curve,
        "validations": validations,
        "best": best,
        "artifactIntegrity": {"illegalDecisions": illegal, "incompleteDecisions": incomplete},
        "finalMargin": validations[-1]["pairedD3VsFair"]["meanDelta"] if validations and "pairedD3VsFair" in validations[-1] else None,
        "bestMargin": max((v["pairedD3VsFair"]["meanDelta"] for v in validations if "pairedD3VsFair" in v), default=None),
        "anyPositiveMargin": any(v["pairedD3VsFair"]["meanDelta"] > 0 for v in validations if "pairedD3VsFair" in v),
    }


def pilot_summary(out):
    arms = {}
    for name in sorted(PILOT_ARMS):
        summary = training_summary(os.path.join(out, "pilot", name))
        if summary is not None:
            summary["layout"] = PILOT_ARMS[name][0]
            summary["alpha"] = PILOT_ARMS[name][1]
            arms[name] = summary
    if not arms:
        return None
    selection_path = os.path.join(out, "pilot", "selection.json")
    return {
        "arms": arms,
        "selection": load_json_if_complete(selection_path),
        "rule": "the arm whose final validation point has the largest paired mean margin of ntuple-d3s7 over fair-d3s7; ties by fewer table entries",
    }


def select_arm(out):
    pilot = pilot_summary(out)
    if pilot is None:
        raise SystemExit("no pilot arms found")
    ranked = []
    for name, arm in pilot["arms"].items():
        if not arm["done"] or arm["finalMargin"] is None:
            continue
        entries = arm["config"]["entries"] if arm["config"] else 0
        ranked.append((-arm["finalMargin"], entries, name))
    if not ranked:
        raise SystemExit("no completed pilot arm with a final validation point")
    ranked.sort()
    best = ranked[0][2]
    return {
        "arm": best,
        "layout": PILOT_ARMS[best][0],
        "alpha": PILOT_ARMS[best][1],
        "finalMargin": pilot["arms"][best]["finalMargin"],
        "ranking": [{"arm": n, "finalMargin": -m, "entries": e} for m, e, n in ranked],
        "rule": pilot["rule"],
    }


def screen_summary(out):
    path = os.path.join(out, "screen", "heldout.json")
    if not os.path.exists(path):
        return None
    art = load_json(path)
    by = {i["name"]: i for i in art["individuals"]}
    arms = {name: arm_summary(ind) for name, ind in by.items()}
    contrasts = {}
    for cand, ref in [("candidate-d3s7", "fair-d3s7"), ("candidate-1ply", "fair-d3s7"), ("candidate-d3s7", "fair-d4s7"), ("fair-d4s7", "fair-d3s7"), ("candidate-d3s7", "candidate-1ply")]:
        if cand in by and ref in by:
            contrasts[f"{cand}-vs-{ref}"] = {"score": paired(by[cand], by[ref]), "moves": paired(by[cand], by[ref], "moves")}
    primary = contrasts.get("candidate-d3s7-vs-fair-d3s7", {}).get("score")
    gate = None
    if primary:
        integrity_ok = all(i["illegalDecisions"] == 0 and i["incompleteDecisions"] == 0 for i in by.values())
        checks = [
            {"criterion": "screen artifact: illegalDecisions 0 and incompleteDecisions 0 in every arm", "passed": integrity_ok},
            {"criterion": "bootstrap 95% lower bound of candidate-d3s7 minus fair-d3s7 > 0", "passed": primary["bootstrapLower95"] > 0, "observed": primary["bootstrapLower95"]},
            {"criterion": "Student-t 95% lower bound > 0", "passed": primary["studentTLower95"] > 0, "observed": primary["studentTLower95"]},
            {"criterion": "paired mean delta > 0 in both halves", "passed": primary["firstHalfMeanDelta"] > 0 and primary["secondHalfMeanDelta"] > 0, "observed": [primary["firstHalfMeanDelta"], primary["secondHalfMeanDelta"]]},
            {"criterion": "candidate-d3s7 Q25 >= fair-d3s7 Q25", "passed": primary["candidateQ25"] >= primary["referenceQ25"], "observed": [primary["candidateQ25"], primary["referenceQ25"]]},
        ]
        gate = {"checks": checks, "passed": all(c["passed"] for c in checks)}
    return {"config": art["config"], "seedStartHex": art["seedStartHex"], "arms": arms, "contrasts": contrasts, "gate": gate}


def gates_summary(out):
    path = os.path.join(out, "gates.log")
    if not os.path.exists(path):
        return None
    lines = open(path, encoding="utf-8").read().splitlines()
    return {
        "passed": any("ALL GATES PASSED" in l for l in lines),
        "gates": [l for l in lines if l.startswith("PASS") or l.startswith("FAIL")],
    }


def rusage_summary(out):
    path = os.path.join(out, "rusage.jsonl")
    if not os.path.exists(path):
        return None
    return load_jsonl(path)


def fmt(x, digits=0):
    if x is None:
        return "n/a"
    return f"{x:,.{digits}f}"


def write_markdown(analysis, path):
    lines = [f"# Analysis of {analysis['runId']}", ""]
    g = analysis.get("gates")
    if g:
        lines += ["## CHECK gates", "", f"All passed: {g['passed']}", ""] + [f"- {l}" for l in g["gates"]] + [""]
    p = analysis.get("pilot")
    if p:
        lines += ["## Pilot arms (training-role validation block, 64 paired games)", "", "| arm | layout | alpha | entries | moves | train mean | final margin d3s7 vs fair | best margin | 1-ply final |", "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
        for name, arm in p["arms"].items():
            last = arm["validations"][-1] if arm["validations"] else None
            lines.append(
                f"| {name} | {arm['layout']} | {arm['alpha']} | {fmt(arm['config']['entries'] if arm['config'] else None)} | {fmt(arm['movesTotal'])} | {fmt(arm['curve'][-1]['trainMeanScore']) if arm['curve'] else 'n/a'} | {fmt(arm['finalMargin'])} | {fmt(arm['bestMargin'])} | {fmt(last['paired1plyVsFair']['meanDelta']) if last and 'paired1plyVsFair' in last else 'n/a'} |"
            )
        if p.get("selection"):
            lines += ["", f"Selected: arm {p['selection']['arm']} ({p['selection']['layout']}, alpha {p['selection']['alpha']}), rule: {p['rule']}"]
        lines.append("")
    m = analysis.get("main")
    if m:
        lines += ["## Main run", "", f"Moves {fmt(m['movesTotal'])}, games {fmt(m['gamesTotal'])}, wall {fmt(m['wallSeconds'])} s, mean {fmt(m['meanMovesPerSecond'])} moves/s, done {m['done']}", "", "| moves | ntuple-d3s7 | fair-d3s7 | paired delta | LB95 | W-L | 1-ply | touched entries |", "| ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: |"]
        for v in m["validations"]:
            d = v.get("pairedD3VsFair", {})
            one = v.get("paired1plyVsFair", {})
            touched = next((c["validation"]["touchedEntries"] for c in m["curve"] if c.get("validation") and c["validation"]["moves"] == v["movesTrained"]), None)
            lines.append(f"| {fmt(v['movesTrained'])} | {fmt(v['arms']['ntuple-d3s7']['meanScore'])} | {fmt(v['arms']['fair-d3s7']['meanScore'])} | {fmt(d.get('meanDelta'))} | {fmt(d.get('bootstrapLower95'))} | {d.get('wins')}-{d.get('losses')} | {fmt(v['arms']['ntuple-1ply']['meanScore']) if 'ntuple-1ply' in v['arms'] else 'n/a'} | {fmt(touched)} |")
        lines += ["", f"Best validation point: {json.dumps(m['best'])}", f"Any positive validation margin (theory training-signal check): {m['anyPositiveMargin']}", ""]
    s = analysis.get("screen")
    if s:
        lines += ["## Held-out screen (256 paired games, one-shot)", "", "| arm | mean | median | Q25 | max | moves | clears/move | reveals/move |", "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
        for name, a in s["arms"].items():
            lines.append(f"| {name} | {fmt(a['meanScore'])} | {fmt(a['medianScore'])} | {fmt(a['q25Score'])} | {fmt(a['maxScore'])} | {fmt(a['meanMoves'],2)} | {a['numberedClearsPerMove']:.4f} | {a['coverRevealsPerMove']:.4f} |")
        lines += ["", "| contrast | delta | LB95 boot | LB95 t | UB95 | W-T-L | halves | floor |", "| --- | ---: | ---: | ---: | ---: | --- | --- | ---: |"]
        for name, c in s["contrasts"].items():
            x = c["score"]
            lines.append(f"| {name} | {fmt(x['meanDelta'])} | {fmt(x['bootstrapLower95'])} | {fmt(x['studentTLower95'])} | {fmt(x['bootstrapUpper95'])} | {x['wins']}-{x['ties']}-{x['losses']} | {fmt(x['firstHalfMeanDelta'])} / {fmt(x['secondHalfMeanDelta'])} | {fmt(x['detectionFloor'])} |")
        if s["gate"]:
            lines += ["", f"Gate passed: {s['gate']['passed']}", ""] + [f"- {'PASS' if c['passed'] else 'FAIL'} {c['criterion']}: {c.get('observed', '')}" for c in s["gate"]["checks"]]
        lines.append("")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", required=True)
    parser.add_argument("--root", default=os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
    parser.add_argument("--select-arm", action="store_true")
    args = parser.parse_args()
    out = os.path.join(args.root, "runs", args.run, "ntuple-scale")
    if args.select_arm:
        print(json.dumps(select_arm(out), indent=2))
        return
    analysis = {
        "format": "drop7-ntuple-scale-analysis-v1",
        "runId": args.run,
        "gates": gates_summary(out),
        "pilot": pilot_summary(out),
        "main": training_summary(os.path.join(out, "main")),
        "screen": screen_summary(out),
        "rusage": rusage_summary(out),
    }
    with open(os.path.join(out, "analysis.json"), "w", encoding="utf-8") as handle:
        json.dump(analysis, handle, indent=2, default=float)
    write_markdown(analysis, os.path.join(out, "analysis.md"))
    print(os.path.join(out, "analysis.md"))


if __name__ == "__main__":
    main()
