#!/usr/bin/env python3
"""Summarise every retained artifact of one ntuple-scale run.

Reads runs/<RUN_ID>/ntuple-scale/ and writes analysis.json plus analysis.md
next to it.  Handles partial runs: each stage is reported only if its
artifacts exist.  Paired statistics use the same bootstrap as the
leaf-evolution compare.py (20,000 resamples, seed 0xb0071eaf).  The
preregistered pass criteria are evaluated from the screen artifact exactly as
written in the experiment record; nothing here extrapolates.

Four run shapes are understood: the first experiment's (gates, six pilot
arms, main run, screen with four arms), the replication's (gates, a
throughput smoke run on the probe block, a main run with a plateau rule, a
screen that also carries the first experiment's frozen tables as the prior-*
arms), the depth-4 screen's (no training) and the fill-conditioned
experiment's (gates, two no-training edits under main/, three warm-started
arms under pilot/{control,occ5,hgt5}, a screen whose gate reads fill-d3s7
against prior-d3s7).  A stage that is absent is reported as absent.

Usage: analyze.py --run RUN_ID [--root REPO_ROOT] [--select-arm | --select-fill-arm]

With --select-arm the script prints (to stdout) the pilot selection JSON:
the arm whose final validation point has the largest paired mean margin of
ntuple-d3s7 over fair-d3s7, ties by fewer table entries.  With
--select-fill-arm it prints the fill-conditioned experiment's selection: the
fill arm (occ5 or hgt5) whose best validation point has the larger paired
margin, ties to occ5, beside the control arm's best point.
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
    """Paired statistics on the seeds both arms played, in cohort order (a
    depth-4 arm may have played only the first --games-d4 seeds of the
    block; every other arm the whole block).  The seed lists must agree on
    the shared prefix."""
    cand_games = cand["games"]
    ref_games = ref["games"]
    if len(cand_games) != len(ref_games):
        n = min(len(cand_games), len(ref_games))
        cand_games = cand_games[:n]
        ref_games = ref_games[:n]
    assert [g["seedHex"] for g in cand_games] == [g["seedHex"] for g in ref_games]
    a = np.array([g[key] for g in cand_games], dtype=float)
    b = np.array([g[key] for g in ref_games], dtype=float)
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
        "meanWork": float(np.mean([g["work"] for g in ind["games"]])),
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
            "rootUpdates": row.get("rootUpdates"),
            "internalUpdates": row.get("internalUpdates"),
            "leafCalls": row.get("leafCalls"),
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
    stop = load_json_if_complete(os.path.join(directory, "stop.json"))
    start = load_json_if_complete(os.path.join(directory, "start.json"))
    plateau_by_moves = {}
    for row in rows:
        v = row.get("validation")
        if v and v.get("plateau"):
            plateau_by_moves[v["moves"]] = v["plateau"]
    for entry in validations:
        entry["plateau"] = plateau_by_moves.get(entry["movesTrained"])
    return {
        "config": config,
        "validateGames": config.get("validateGames") if config else None,
        "stop": stop,
        "chunks": len(rows),
        "movesTotal": rows[-1]["movesTotal"] if rows else 0,
        "gamesTotal": rows[-1]["gamesTotal"] if rows else 0,
        "wallSeconds": rows[-1]["wallSeconds"] if rows else 0,
        "meanMovesPerSecond": st.fmean(r["movesPerSecond"] for r in rows) if rows else None,
        "done": os.path.exists(os.path.join(directory, "DONE")),
        "curve": curve,
        "validations": validations,
        "best": best,
        "start": start,
        "artifactIntegrity": {"illegalDecisions": illegal, "incompleteDecisions": incomplete},
        "finalMargin": validations[-1]["pairedD3VsFair"]["meanDelta"] if validations and "pairedD3VsFair" in validations[-1] else None,
        "bestMargin": max((v["pairedD3VsFair"]["meanDelta"] for v in validations if "pairedD3VsFair" in v and v["movesTrained"] > 0), default=None),
        "anyPositiveMargin": any(v["pairedD3VsFair"]["meanDelta"] > 0 for v in validations if "pairedD3VsFair" in v),
    }


FILL_ARMS = ("occ5", "hgt5")
TREE_ARMS = ("searchtd", "treestrap")
TREE_RULE = "the candidate is the treestrap arm's best validation point by protocol (no selection between arms); the searchtd arm's best point is the ablation; each arm's point 0 is the warm start's own margin on the same block and is never a candidate"
FILL_RULE = "the fill arm (occ5 or hgt5) whose best validation point has the larger paired mean margin of ntuple-d3s7 over fair-d3s7 on the 256-game training-role block, ties to occ5; the control arm's best point is the control candidate"


def pilot_summary(out):
    """Every training arm under pilot/: the first experiment's six named
    arms, and any other directory that holds a config.json (the
    fill-conditioned experiment's control, occ5 and hgt5), whose layout and
    alpha are read from the config."""
    arms = {}
    pilot_dir = os.path.join(out, "pilot")
    names = sorted(PILOT_ARMS)
    if os.path.isdir(pilot_dir):
        names += sorted(n for n in os.listdir(pilot_dir) if n not in PILOT_ARMS and os.path.exists(os.path.join(pilot_dir, n, "config.json")))
    for name in names:
        summary = training_summary(os.path.join(pilot_dir, name))
        if summary is not None:
            if name in PILOT_ARMS:
                summary["layout"] = PILOT_ARMS[name][0]
                summary["alpha"] = PILOT_ARMS[name][1]
            else:
                summary["layout"] = summary["config"]["layout"] if summary["config"] else ""
                summary["alpha"] = summary["config"]["alpha"] if summary["config"] else None
                summary["initFrom"] = summary["config"].get("initFrom") if summary["config"] else None
            arms[name] = summary
    if not arms:
        return None
    selection_path = os.path.join(out, "pilot", "selection.json")
    fill_shaped = any(name in FILL_ARMS or name == "control" for name in arms)
    tree_shaped = any(name in TREE_ARMS for name in arms)
    gentle_shaped = any(name in ("treestrap05", "treestrap20", "searchtd05") for name in arms)
    return {
        "arms": arms,
        "selection": load_json_if_complete(selection_path),
        "rule": GENTLE_RULE if gentle_shaped else TREE_RULE if tree_shaped else FILL_RULE if fill_shaped else "the arm whose final validation point has the largest paired mean margin of ntuple-d3s7 over fair-d3s7; ties by fewer table entries",
    }


GENTLE_RULE = "the TreeStrap arm (treestrap05 or treestrap20) whose best validation point has the larger paired mean margin of ntuple-d3s7 over fair-d3s7 on the 256-game training-role block is the candidate, ties to treestrap05; the other is screened at depth 3 as treestrapalt; searchtd05 is the ablation; each arm's point 0 is the warm start's own margin and is never a candidate"


def select_gentle_arm(out):
    """The gentle-step TreeStrap experiment's selection."""
    pilot = pilot_summary(out)
    if pilot is None:
        raise SystemExit("no training arms found")
    arms = pilot["arms"]
    for name in ("treestrap05", "treestrap20", "searchtd05"):
        if name not in arms or not arms[name]["done"] or arms[name]["bestMargin"] is None:
            raise SystemExit(f"arm {name} is not complete with a validation point")
    def start_margin(name):
        start = load_json_if_complete(os.path.join(out, "pilot", name, "start.json"))
        return start["pairedDeltaD3"] if start else None
    ranked = sorted(("treestrap05", "treestrap20"), key=lambda n: (-arms[n]["bestMargin"], ("treestrap05", "treestrap20").index(n)))
    candidate, alternate = ranked
    return {
        "candidateArm": candidate,
        "candidateBestMargin": arms[candidate]["bestMargin"],
        "candidateBestMoves": arms[candidate]["best"]["moves"] if arms[candidate]["best"] else None,
        "alternateArm": alternate,
        "ablationArm": "searchtd05",
        "ablationBestMargin": arms["searchtd05"]["bestMargin"],
        "ablationBestMoves": arms["searchtd05"]["best"]["moves"] if arms["searchtd05"]["best"] else None,
        "arms": {n: {"bestMargin": arms[n]["bestMargin"], "finalMargin": arms[n]["finalMargin"], "bestMoves": arms[n]["best"]["moves"] if arms[n]["best"] else None, "movesTotal": arms[n]["movesTotal"], "validationPoints": len(arms[n]["validations"]), "stop": arms[n]["stop"]["reason"] if arms[n]["stop"] else None, "warmStartMargin": start_margin(n), "alpha": arms[n].get("alpha")} for n in ("treestrap05", "treestrap20", "searchtd05")},
        "trainingSignal": {"criterion": "the candidate TreeStrap arm's best validation margin exceeds the warm start's own margin on the same block (point 0)", "passed": (arms[candidate]["bestMargin"] > start_margin(candidate)) if start_margin(candidate) is not None else None},
        "rule": GENTLE_RULE,
    }


def select_tree_arm(out):
    """The search-target experiment's fixed candidate, with both arms' best points and the warm start's point 0."""
    pilot = pilot_summary(out)
    if pilot is None:
        raise SystemExit("no training arms found")
    arms = pilot["arms"]
    for name in TREE_ARMS:
        if name not in arms or not arms[name]["done"] or arms[name]["bestMargin"] is None:
            raise SystemExit(f"arm {name} is not complete with a validation point")
    def start_margin(name):
        start = load_json_if_complete(os.path.join(out, "pilot", name, "start.json"))
        return start["pairedDeltaD3"] if start else None
    return {
        "candidateArm": "treestrap",
        "candidateBestMargin": arms["treestrap"]["bestMargin"],
        "candidateBestMoves": arms["treestrap"]["best"]["moves"] if arms["treestrap"]["best"] else None,
        "ablationArm": "searchtd",
        "ablationBestMargin": arms["searchtd"]["bestMargin"],
        "ablationBestMoves": arms["searchtd"]["best"]["moves"] if arms["searchtd"]["best"] else None,
        "arms": {n: {"bestMargin": arms[n]["bestMargin"], "finalMargin": arms[n]["finalMargin"], "bestMoves": arms[n]["best"]["moves"] if arms[n]["best"] else None, "movesTotal": arms[n]["movesTotal"], "validationPoints": len(arms[n]["validations"]), "stop": arms[n]["stop"]["reason"] if arms[n]["stop"] else None, "warmStartMargin": start_margin(n)} for n in TREE_ARMS},
        "trainingSignal": {"criterion": "the treestrap arm's best validation margin exceeds the warm start's own margin on the same block (point 0)", "passed": (arms["treestrap"]["bestMargin"] > start_margin("treestrap")) if start_margin("treestrap") is not None else None},
        "rule": TREE_RULE,
    }


def select_fill_arm(out):
    """The fill-conditioned experiment's fixed selection rule."""
    pilot = pilot_summary(out)
    if pilot is None:
        raise SystemExit("no training arms found")
    arms = pilot["arms"]
    for name in FILL_ARMS + ("control",):
        if name not in arms or not arms[name]["done"] or arms[name]["bestMargin"] is None:
            raise SystemExit(f"arm {name} is not complete with a validation point")
    ranked = sorted(FILL_ARMS, key=lambda n: (-arms[n]["bestMargin"], FILL_ARMS.index(n)))
    candidate = ranked[0]
    control_best = arms["control"]["bestMargin"]
    return {
        "candidateArm": candidate,
        "candidateBestMargin": arms[candidate]["bestMargin"],
        "candidateBestMoves": arms[candidate]["best"]["moves"] if arms[candidate]["best"] else None,
        "controlBestMargin": control_best,
        "controlBestMoves": arms["control"]["best"]["moves"] if arms["control"]["best"] else None,
        "arms": {n: {"bestMargin": arms[n]["bestMargin"], "finalMargin": arms[n]["finalMargin"], "bestMoves": arms[n]["best"]["moves"] if arms[n]["best"] else None, "movesTotal": arms[n]["movesTotal"], "validationPoints": len(arms[n]["validations"]), "stop": arms[n]["stop"]["reason"] if arms[n]["stop"] else None} for n in FILL_ARMS + ("control",)},
        "trainingSignal": {"criterion": "a fill arm's best validation margin exceeds the control arm's best validation margin", "passed": any(arms[n]["bestMargin"] > control_best for n in FILL_ARMS)},
        "rule": FILL_RULE,
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
    pairs = [
        ("candidate-d3s7", "fair-d3s7"), ("candidate-1ply", "fair-d3s7"), ("candidate-d3s7", "fair-d4s7"),
        ("fair-d4s7", "fair-d3s7"), ("candidate-d3s7", "candidate-1ply"),
        ("prior-d3s7", "fair-d3s7"), ("candidate-d3s7", "prior-d3s7"), ("prior-d3s7", "fair-d4s7"), ("prior-1ply", "fair-d3s7"),
        # The depth-4 experiment: the same frozen tables one ply deeper.
        ("prior-d4s7", "prior-d3s7"), ("prior-d4s7", "fair-d4s7"), ("prior-d4s7", "fair-d3s7"),
        # The fill-conditioned experiment.
        ("fill-d3s7", "prior-d3s7"), ("fill-d3s7", "control-d3s7"), ("control-d3s7", "prior-d3s7"),
        ("zeroed-d3s7", "prior-d3s7"), ("classmean-d3s7", "prior-d3s7"),
        ("fill-d4s7", "prior-d4s7"), ("zeroed-d4s7", "prior-d4s7"), ("fill-d4s7", "fill-d3s7"), ("zeroed-d4s7", "zeroed-d3s7"),
        ("fill-d3s7", "fair-d3s7"), ("control-d3s7", "fair-d3s7"), ("zeroed-d3s7", "fair-d3s7"), ("classmean-d3s7", "fair-d3s7"),
        ("fill-1ply", "prior-1ply"), ("fill-1ply", "fair-d3s7"), ("fill-d4s7", "fair-d3s7"),
        # The search-target (TreeStrap) experiment.
        ("treestrap-d3s7", "prior-d3s7"), ("treestrap-d3s7", "searchtd-d3s7"), ("searchtd-d3s7", "prior-d3s7"),
        ("treestrap-d3s7", "control-d3s7"), ("treestrap-d4s7", "prior-d4s7"), ("searchtd-d4s7", "prior-d4s7"),
        ("treestrap-d4s7", "treestrap-d3s7"), ("searchtd-d4s7", "searchtd-d3s7"),
        ("treestrap-d3s7", "fair-d3s7"), ("searchtd-d3s7", "fair-d3s7"), ("treestrap-d4s7", "fair-d3s7"),
        ("treestrap-1ply", "prior-1ply"), ("treestrap-1ply", "fair-d3s7"),
        ("treestrapalt-d3s7", "prior-d3s7"), ("treestrap-d3s7", "treestrapalt-d3s7"), ("treestrapalt-d3s7", "fair-d3s7"),
    ]
    for cand, ref in pairs:
        if cand in by and ref in by:
            contrasts[f"{cand}-vs-{ref}"] = {"score": paired(by[cand], by[ref]), "moves": paired(by[cand], by[ref], "moves")}
    integrity_ok = all(i["illegalDecisions"] == 0 and i["incompleteDecisions"] == 0 for i in by.values())

    def gate_checks(label, contrast):
        return [
            {"criterion": "screen artifact: illegalDecisions 0 and incompleteDecisions 0 in every arm", "passed": integrity_ok},
            {"criterion": f"bootstrap 95% lower bound of {label} minus fair-d3s7 > 0", "passed": contrast["bootstrapLower95"] > 0, "observed": contrast["bootstrapLower95"]},
            {"criterion": "Student-t 95% lower bound > 0", "passed": contrast["studentTLower95"] > 0, "observed": contrast["studentTLower95"]},
            {"criterion": "paired mean delta > 0 in both halves", "passed": contrast["firstHalfMeanDelta"] > 0 and contrast["secondHalfMeanDelta"] > 0, "observed": [contrast["firstHalfMeanDelta"], contrast["secondHalfMeanDelta"]]},
            {"criterion": f"{label} Q25 >= fair-d3s7 Q25", "passed": contrast["candidateQ25"] >= contrast["referenceQ25"], "observed": [contrast["candidateQ25"], contrast["referenceQ25"]]},
        ]

    primary = contrasts.get("candidate-d3s7-vs-fair-d3s7", {}).get("score")
    gate = None
    if primary:
        checks = gate_checks("candidate-d3s7", primary)
        gate = {"checks": checks, "passed": all(c["passed"] for c in checks)}
    # Replication of the first experiment's frozen tables on this fresh block.
    replication = None
    prior = contrasts.get("prior-d3s7-vs-fair-d3s7", {}).get("score")
    if prior:
        checks = gate_checks("prior-d3s7", prior)
        replication = {"checks": checks, "passed": all(c["passed"] for c in checks)}
    # The scale question: the wider, longer-trained tables against the first
    # candidate on the same seeds.  Three verdicts, fixed in the protocol.
    scale = None
    versus = contrasts.get("candidate-d3s7-vs-prior-d3s7", {}).get("score")
    if versus:
        if versus["bootstrapLower95"] > 0 and versus["studentTLower95"] > 0:
            verdict = "supported"
        elif versus["bootstrapUpper95"] < 0:
            verdict = "refuted"
        else:
            verdict = "inconclusive"
        scale = {"verdict": verdict, "meanDelta": versus["meanDelta"], "bootstrapLower95": versus["bootstrapLower95"], "bootstrapUpper95": versus["bootstrapUpper95"], "studentTLower95": versus["studentTLower95"], "detectionFloor": versus["detectionFloor"], "wins": versus["wins"], "ties": versus["ties"], "losses": versus["losses"]}
    # The depth-4 experiment (EX-20260906-ntuple-scale-depth4-frozen-tables-*):
    # the frozen tables at reference d4s7 against the same tables at d3s7, the
    # persistence of their margin over the fair leaf at depth 4, and the
    # interaction: the fourth ply's per-game gain on the tables minus its gain
    # on the fair leaf, on the same seeds.  Verdicts fixed in the protocol.
    depth = None
    primary_contrast = "candidate-d3s7-vs-fair-d3s7"
    step = contrasts.get("prior-d4s7-vs-prior-d3s7", {}).get("score")
    if step and all(name in by for name in ("prior-d4s7", "prior-d3s7", "fair-d4s7", "fair-d3s7")):
        primary_contrast = "prior-d4s7-vs-prior-d3s7"

        def depth_checks(label, reference, contrast):
            return [
                {"criterion": "screen artifact: illegalDecisions 0 and incompleteDecisions 0 in every arm", "passed": integrity_ok},
                {"criterion": f"bootstrap 95% lower bound of {label} minus {reference} > 0", "passed": contrast["bootstrapLower95"] > 0, "observed": contrast["bootstrapLower95"]},
                {"criterion": "Student-t 95% lower bound > 0", "passed": contrast["studentTLower95"] > 0, "observed": contrast["studentTLower95"]},
                {"criterion": "paired mean delta > 0 in both halves", "passed": contrast["firstHalfMeanDelta"] > 0 and contrast["secondHalfMeanDelta"] > 0, "observed": [contrast["firstHalfMeanDelta"], contrast["secondHalfMeanDelta"]]},
                {"criterion": f"{label} Q25 >= {reference} Q25", "passed": contrast["candidateQ25"] >= contrast["referenceQ25"], "observed": [contrast["candidateQ25"], contrast["referenceQ25"]]},
            ]

        checks = depth_checks("prior-d4s7", "prior-d3s7", step)
        gate = {"checks": checks, "passed": all(c["passed"] for c in checks)}
        persistence = None
        keep = contrasts.get("prior-d4s7-vs-fair-d4s7", {}).get("score")
        if keep:
            checks = depth_checks("prior-d4s7", "fair-d4s7", keep)
            persistence = {"checks": checks, "passed": all(c["passed"] for c in checks)}
        # Interaction series: per game, (tables d4 - tables d3) - (fair d4 - fair d3).
        seeds = [g["seedHex"] for g in by["prior-d4s7"]["games"]]
        assert all([g["seedHex"] for g in by[n]["games"]] == seeds for n in ("prior-d3s7", "fair-d4s7", "fair-d3s7"))
        series = {"games": [
            {"seedHex": seed, "score": (a["score"] - b["score"]) - (c["score"] - d["score"])}
            for seed, a, b, c, d in zip(seeds, by["prior-d4s7"]["games"], by["prior-d3s7"]["games"], by["fair-d4s7"]["games"], by["fair-d3s7"]["games"])
        ]}
        zero = {"games": [{"seedHex": seed, "score": 0.0} for seed in seeds]}
        inter = paired(series, zero)
        if inter["bootstrapLower95"] > 0 and inter["studentTLower95"] > 0:
            verdict = "larger"
        elif inter["bootstrapUpper95"] < 0:
            verdict = "smaller"
        else:
            verdict = "inconclusive"
        fair_step = contrasts.get("fair-d4s7-vs-fair-d3s7", {}).get("score")
        depth = {
            "primary": "prior-d4s7-vs-prior-d3s7",
            "tablesStep": {k: step[k] for k in ("meanDelta", "bootstrapLower95", "bootstrapUpper95", "studentTLower95", "detectionFloor", "wins", "ties", "losses", "firstHalfMeanDelta", "secondHalfMeanDelta", "candidateQ25", "referenceQ25")},
            "fairStep": {k: fair_step[k] for k in ("meanDelta", "bootstrapLower95", "bootstrapUpper95", "studentTLower95", "detectionFloor", "wins", "ties", "losses", "firstHalfMeanDelta", "secondHalfMeanDelta")} if fair_step else None,
            "persistence": persistence,
            "interaction": {"verdict": verdict, **{k: inter[k] for k in ("n", "meanDelta", "pairedSd", "bootstrapLower95", "bootstrapUpper95", "studentTLower95", "detectionFloor", "wins", "ties", "losses", "firstHalfMeanDelta", "secondHalfMeanDelta")}},
            "theory": {
                "primaryFalsifierUpperBoundBelowZero": step["bootstrapUpper95"] < 0,
                "secondLegUpperBoundBelowZero": inter["bootstrapUpper95"] < 0,
                "persistenceLowerBoundAtOrBelowZero": (keep["bootstrapLower95"] <= 0) if keep else None,
            },
        }
    # The fill-conditioned experiment (EX-20260906-ntuple-fill-conditioned-*):
    # the gate reads the selected fill candidate against the frozen tables
    # at depth 3; six readings beside it carry the fixed three-way verdict.
    fill = None
    primary_fill = contrasts.get("fill-d3s7-vs-prior-d3s7", {}).get("score")
    if primary_fill:
        primary_contrast = "fill-d3s7-vs-prior-d3s7"

        def criteria(label, reference, contrast):
            return [
                {"criterion": "screen artifact: illegalDecisions 0 and incompleteDecisions 0 in every arm", "passed": integrity_ok},
                {"criterion": f"bootstrap 95% lower bound of {label} minus {reference} > 0", "passed": contrast["bootstrapLower95"] > 0, "observed": contrast["bootstrapLower95"]},
                {"criterion": "Student-t 95% lower bound > 0", "passed": contrast["studentTLower95"] > 0, "observed": contrast["studentTLower95"]},
                {"criterion": "paired mean delta > 0 in both halves", "passed": contrast["firstHalfMeanDelta"] > 0 and contrast["secondHalfMeanDelta"] > 0, "observed": [contrast["firstHalfMeanDelta"], contrast["secondHalfMeanDelta"]]},
                {"criterion": f"{label} Q25 >= {reference} Q25", "passed": contrast["candidateQ25"] >= contrast["referenceQ25"], "observed": [contrast["candidateQ25"], contrast["referenceQ25"]]},
            ]

        def verdict(contrast):
            if contrast["bootstrapLower95"] > 0 and contrast["studentTLower95"] > 0:
                return "supported"
            if contrast["bootstrapUpper95"] < 0:
                return "refuted"
            return "inconclusive"

        def reading(name):
            c = contrasts.get(name, {}).get("score")
            if not c:
                return None
            return {"contrast": name, "verdict": verdict(c), **{k: c[k] for k in ("meanDelta", "bootstrapLower95", "bootstrapUpper95", "studentTLower95", "detectionFloor", "wins", "ties", "losses", "firstHalfMeanDelta", "secondHalfMeanDelta", "candidateQ25", "referenceQ25")}}

        checks = criteria("fill-d3s7", "prior-d3s7", primary_fill)
        gate = {"checks": checks, "passed": all(c["passed"] for c in checks)}
        continuation_c = contrasts.get("control-d3s7-vs-prior-d3s7", {}).get("score")
        continuation = None
        if continuation_c:
            cchecks = criteria("control-d3s7", "prior-d3s7", continuation_c)
            continuation = {"checks": cchecks, "passed": all(c["passed"] for c in cchecks), **reading("control-d3s7-vs-prior-d3s7")}
        conditioning = reading("fill-d3s7-vs-control-d3s7")
        zeroed = reading("zeroed-d3s7-vs-prior-d3s7")
        classmean = reading("classmean-d3s7-vs-prior-d3s7")
        fill_depth = reading("fill-d4s7-vs-prior-d4s7")
        zeroed_depth = reading("zeroed-d4s7-vs-prior-d4s7")
        fill = {
            "primary": "fill-d3s7-vs-prior-d3s7",
            "gate": gate,
            "conditioning": conditioning,
            "continuation": continuation,
            "zeroed": zeroed,
            "classmean": classmean,
            "fillDepth4": fill_depth,
            "zeroedDepth4": zeroed_depth,
            "depthSteps": {n: reading(n) for n in ("fill-d4s7-vs-fill-d3s7", "prior-d4s7-vs-prior-d3s7", "zeroed-d4s7-vs-zeroed-d3s7") if n in contrasts},
            "direct": {n: reading(n) for n in ("fill-1ply-vs-prior-1ply", "fill-1ply-vs-fair-d3s7", "prior-1ply-vs-fair-d3s7") if n in contrasts},
            "replicationOfPrior": reading("prior-d3s7-vs-fair-d3s7"),
            "theory": {
                "primaryFalsifierUpperBoundBelowZero": primary_fill["bootstrapUpper95"] < 0,
                "conditioningUpperBoundBelowZero": (conditioning["bootstrapUpper95"] < 0) if conditioning else None,
                "gainIsContinuationNotConditioning": bool(gate["passed"] and conditioning and conditioning["verdict"] == "inconclusive" and continuation and continuation["passed"]) if conditioning and continuation else None,
                "optimismLegRefuted": bool(zeroed and zeroed["verdict"] == "refuted" and classmean and classmean["verdict"] != "supported") if zeroed and classmean else None,
                "depthCompounding": (fill_depth["verdict"] == "refuted") if fill_depth else None,
            },
        }
    # The search-target experiment (EX-20260907-ntuple-treestrap-*): the gate
    # reads the treestrap candidate against the frozen tables at depth 3;
    # the readings beside it carry the fixed three-way verdict.
    tree = None
    primary_tree = contrasts.get("treestrap-d3s7-vs-prior-d3s7", {}).get("score")
    if primary_tree:
        primary_contrast = "treestrap-d3s7-vs-prior-d3s7"

        def criteria(label, reference, contrast):
            return [
                {"criterion": "screen artifact: illegalDecisions 0 and incompleteDecisions 0 in every arm", "passed": integrity_ok},
                {"criterion": f"bootstrap 95% lower bound of {label} minus {reference} > 0", "passed": contrast["bootstrapLower95"] > 0, "observed": contrast["bootstrapLower95"]},
                {"criterion": "Student-t 95% lower bound > 0", "passed": contrast["studentTLower95"] > 0, "observed": contrast["studentTLower95"]},
                {"criterion": "paired mean delta > 0 in both halves", "passed": contrast["firstHalfMeanDelta"] > 0 and contrast["secondHalfMeanDelta"] > 0, "observed": [contrast["firstHalfMeanDelta"], contrast["secondHalfMeanDelta"]]},
                {"criterion": f"{label} Q25 >= {reference} Q25", "passed": contrast["candidateQ25"] >= contrast["referenceQ25"], "observed": [contrast["candidateQ25"], contrast["referenceQ25"]]},
            ]

        def verdict(contrast):
            if contrast["bootstrapLower95"] > 0 and contrast["studentTLower95"] > 0:
                return "supported"
            if contrast["bootstrapUpper95"] < 0:
                return "refuted"
            return "inconclusive"

        def reading(name):
            c = contrasts.get(name, {}).get("score")
            if not c:
                return None
            return {"contrast": name, "verdict": verdict(c), **{k: c[k] for k in ("meanDelta", "bootstrapLower95", "bootstrapUpper95", "studentTLower95", "detectionFloor", "wins", "ties", "losses", "firstHalfMeanDelta", "secondHalfMeanDelta", "candidateQ25", "referenceQ25")}}

        checks = criteria("treestrap-d3s7", "prior-d3s7", primary_tree)
        gate = {"checks": checks, "passed": all(c["passed"] for c in checks)}
        ablation_c = contrasts.get("searchtd-d3s7-vs-prior-d3s7", {}).get("score")
        ablation = None
        if ablation_c:
            achecks = criteria("searchtd-d3s7", "prior-d3s7", ablation_c)
            ablation = {"checks": achecks, "passed": all(c["passed"] for c in achecks), **reading("searchtd-d3s7-vs-prior-d3s7")}
        offpath = reading("treestrap-d3s7-vs-searchtd-d3s7")
        vs_control = reading("treestrap-d3s7-vs-control-d3s7")
        tree_depth = reading("treestrap-d4s7-vs-prior-d4s7")
        ablation_depth = reading("searchtd-d4s7-vs-prior-d4s7")
        tree = {
            "primary": "treestrap-d3s7-vs-prior-d3s7",
            "gate": gate,
            "offPathBoards": offpath,
            "ablation": ablation,
            "vsOnePlyContinuation": vs_control,
            "treestrapDepth4": tree_depth,
            "ablationDepth4": ablation_depth,
            "alternate": reading("treestrapalt-d3s7-vs-prior-d3s7"),
            "candidateVsAlternate": reading("treestrap-d3s7-vs-treestrapalt-d3s7"),
            "depthSteps": {n: reading(n) for n in ("treestrap-d4s7-vs-treestrap-d3s7", "searchtd-d4s7-vs-searchtd-d3s7", "prior-d4s7-vs-prior-d3s7") if n in contrasts},
            "direct": {n: reading(n) for n in ("treestrap-1ply-vs-prior-1ply", "treestrap-1ply-vs-fair-d3s7", "prior-1ply-vs-fair-d3s7") if n in contrasts},
            "replicationOfPrior": reading("prior-d3s7-vs-fair-d3s7"),
            "theory": {
                "primaryFalsifierUpperBoundBelowZero": primary_tree["bootstrapUpper95"] < 0,
                "offPathUpperBoundBelowZero": (offpath["bootstrapUpper95"] < 0) if offpath else None,
                "gainIsSearchTargetNotOffPath": bool(gate["passed"] and offpath and offpath["verdict"] == "inconclusive" and ablation and ablation["passed"]) if offpath and ablation else None,
                "depthCompounding": (tree_depth["verdict"] == "refuted") if tree_depth else None,
            },
        }
    return {"config": art["config"], "seedStartHex": art["seedStartHex"], "games": art["config"].get("games"), "arms": arms, "contrasts": contrasts, "primaryContrast": primary_contrast, "gate": gate, "replication": replication, "scale": scale, "depth": depth, "fill": fill, "tree": tree}


def gates_summary(out, name="gates.log"):
    path = os.path.join(out, name)
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
        games = next((a.get("validateGames") for a in p["arms"].values() if a.get("validateGames")), 64)
        lines += [f"## Training arms (training-role validation block, {games} paired games)", "", "| arm | layout | alpha | entries | moves | train mean | final margin d3s7 vs fair | best margin | best at moves | points | stop | 1-ply final |", "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: |"]
        for name, arm in p["arms"].items():
            last = arm["validations"][-1] if arm["validations"] else None
            lines.append(
                f"| {name} | {arm['layout']} | {arm['alpha']} | {fmt(arm['config']['entries'] if arm['config'] else None)} | {fmt(arm['movesTotal'])} | {fmt(arm['curve'][-1]['trainMeanScore']) if arm['curve'] else 'n/a'} | {fmt(arm['finalMargin'])} | {fmt(arm['bestMargin'])} | {fmt(arm['best']['moves']) if arm.get('best') else 'n/a'} | {len(arm['validations'])} | {arm['stop']['reason'] if arm.get('stop') else ''} | {fmt(last['paired1plyVsFair']['meanDelta']) if last and 'paired1plyVsFair' in last else 'n/a'} |"
            )
        sel = p.get("selection")
        if sel and "arm" in sel:
            lines += ["", f"Selected: arm {sel['arm']} ({sel['layout']}, alpha {sel['alpha']}), rule: {p['rule']}"]
        elif sel and "ablationArm" in sel:
            lines += ["", f"Candidate: arm {sel['candidateArm']} (best margin {fmt(sel['candidateBestMargin'])} at {fmt(sel['candidateBestMoves'])} moves); ablation arm {sel['ablationArm']} best margin {fmt(sel['ablationBestMargin'])} at {fmt(sel['ablationBestMoves'])} moves; warm-start margins (point 0) {json.dumps({n: a['warmStartMargin'] for n, a in sel['arms'].items()})}; training-signal check passed: {sel['trainingSignal']['passed']}; rule: {p['rule']}"]
        elif sel and "candidateArm" in sel:
            lines += ["", f"Selected fill candidate: arm {sel['candidateArm']} (best margin {fmt(sel['candidateBestMargin'])} at {fmt(sel['candidateBestMoves'])} moves); control best margin {fmt(sel['controlBestMargin'])} at {fmt(sel['controlBestMoves'])} moves; training-signal check passed: {sel['trainingSignal']['passed']}; rule: {p['rule']}"]
        for name, arm in p["arms"].items():
            if arm["validations"] and name in ("control", "occ5", "hgt5", "searchtd", "treestrap", "treestrap05", "treestrap20", "searchtd05"):
                lines += ["", f"Validation curve of arm {name}:", "", "| point | moves | ntuple-d3s7 | fair-d3s7 | paired delta | LB95 | W-L | 1-ply | touched entries | plateau (last / previous window) |", "| ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: | --- |"]
                for index, v in enumerate(arm["validations"], start=1):
                    d = v.get("pairedD3VsFair", {})
                    touched = next((c["validation"]["touchedEntries"] for c in arm["curve"] if c.get("validation") and c["validation"]["moves"] == v["movesTrained"]), None)
                    pl = v.get("plateau")
                    pl_text = f"{fmt(pl['recentMean'])} / {fmt(pl['previousMean'])}{' STOP' if pl.get('stop') else ''}" if pl else ""
                    lines.append(f"| {index} | {fmt(v['movesTrained'])} | {fmt(v['arms']['ntuple-d3s7']['meanScore'])} | {fmt(v['arms']['fair-d3s7']['meanScore'])} | {fmt(d.get('meanDelta'))} | {fmt(d.get('bootstrapLower95'))} | {d.get('wins')}-{d.get('losses')} | {fmt(v['arms']['ntuple-1ply']['meanScore']) if 'ntuple-1ply' in v['arms'] else 'n/a'} | {fmt(touched)} | {pl_text} |")
        lines.append("")
    sm = analysis.get("smoke")
    if sm:
        last = sm["curve"][-1] if sm["curve"] else None
        lines += ["## Throughput smoke run (probe block, tables discarded)", "", f"Layout {sm['config']['layout'] if sm['config'] else 'n/a'}, {fmt(sm['config']['entries'] if sm['config'] else None)} entries, moves {fmt(sm['movesTotal'])}, wall {fmt(sm['wallSeconds'])} s, mean {fmt(sm['meanMovesPerSecond'])} moves/s" + (f", last chunk train mean {fmt(last['trainMeanScore'])} / {last['trainMeanMoves']:.1f} moves" if last else ""), ""]
    m = analysis.get("main")
    if m:
        games = m.get("validateGames") or 64
        lines += ["## Main run", "", f"Moves {fmt(m['movesTotal'])}, games {fmt(m['gamesTotal'])}, wall {fmt(m['wallSeconds'])} s, mean {fmt(m['meanMovesPerSecond'])} moves/s, done {m['done']}" + (f", stop reason {m['stop']['reason']}" if m.get("stop") else ""), "", f"Validation line-up on the {games}-game training-role block:", "", "| point | moves | ntuple-d3s7 | fair-d3s7 | paired delta | LB95 | W-L | 1-ply | touched entries | plateau (last / previous window) |", "| ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: | --- |"]
        for index, v in enumerate(m["validations"], start=1):
            d = v.get("pairedD3VsFair", {})
            touched = next((c["validation"]["touchedEntries"] for c in m["curve"] if c.get("validation") and c["validation"]["moves"] == v["movesTrained"]), None)
            pl = v.get("plateau")
            pl_text = f"{fmt(pl['recentMean'])} / {fmt(pl['previousMean'])}{' STOP' if pl.get('stop') else ''}" if pl else ""
            lines.append(f"| {index} | {fmt(v['movesTrained'])} | {fmt(v['arms']['ntuple-d3s7']['meanScore'])} | {fmt(v['arms']['fair-d3s7']['meanScore'])} | {fmt(d.get('meanDelta'))} | {fmt(d.get('bootstrapLower95'))} | {d.get('wins')}-{d.get('losses')} | {fmt(v['arms']['ntuple-1ply']['meanScore']) if 'ntuple-1ply' in v['arms'] else 'n/a'} | {fmt(touched)} | {pl_text} |")
        lines += ["", f"Best validation point: {json.dumps(m['best'])}", f"Any positive validation margin (theory training-signal check): {m['anyPositiveMargin']}"]
        if m.get("stop"):
            lines.append(f"Stop: {json.dumps(m['stop'])}")
        lines.append("")
    s = analysis.get("screen")
    if s:
        lines += [f"## Held-out screen ({s.get('games') or 'n/a'} paired games, one-shot)", "", "| arm | mean | median | Q25 | max | moves | clears/move | reveals/move |", "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
        for name, a in s["arms"].items():
            lines.append(f"| {name} | {fmt(a['meanScore'])} | {fmt(a['medianScore'])} | {fmt(a['q25Score'])} | {fmt(a['maxScore'])} | {fmt(a['meanMoves'],2)} | {a['numberedClearsPerMove']:.4f} | {a['coverRevealsPerMove']:.4f} |")
        lines += ["", "| contrast | delta | LB95 boot | LB95 t | UB95 | W-T-L | halves | floor |", "| --- | ---: | ---: | ---: | ---: | --- | --- | ---: |"]
        for name, c in s["contrasts"].items():
            x = c["score"]
            lines.append(f"| {name} | {fmt(x['meanDelta'])} | {fmt(x['bootstrapLower95'])} | {fmt(x['studentTLower95'])} | {fmt(x['bootstrapUpper95'])} | {x['wins']}-{x['ties']}-{x['losses']} | {fmt(x['firstHalfMeanDelta'])} / {fmt(x['secondHalfMeanDelta'])} | {fmt(x['detectionFloor'])} |")
        if s["gate"]:
            lines += ["", f"Gate ({s.get('primaryContrast', 'candidate-d3s7-vs-fair-d3s7').replace('-vs-', ' vs ')}) passed: {s['gate']['passed']}", ""] + [f"- {'PASS' if c['passed'] else 'FAIL'} {c['criterion']}: {c.get('observed', '')}" for c in s["gate"]["checks"]]
        if s.get("depth"):
            d = s["depth"]
            if d.get("persistence"):
                lines += ["", f"Persistence (prior-d4s7 vs fair-d4s7) passed: {d['persistence']['passed']}", ""] + [f"- {'PASS' if c['passed'] else 'FAIL'} {c['criterion']}: {c.get('observed', '')}" for c in d["persistence"]["checks"]]
            it = d["interaction"]
            lines += ["", f"Depth-step interaction ((tables d4 - tables d3) - (fair d4 - fair d3)): {it['verdict']}: delta {fmt(it['meanDelta'])}, LB95 boot {fmt(it['bootstrapLower95'])}, LB95 t {fmt(it['studentTLower95'])}, UB95 {fmt(it['bootstrapUpper95'])}, W-T-L {it['wins']}-{it['ties']}-{it['losses']}, halves {fmt(it['firstHalfMeanDelta'])} / {fmt(it['secondHalfMeanDelta'])}, floor {fmt(it['detectionFloor'])}", f"Theory falsifiers: {json.dumps(d['theory'])}"]
        if s.get("fill"):
            f = s["fill"]
            lines += ["", "Fill-conditioned experiment readings (three-way verdicts: supported / refuted / inconclusive):", ""]
            for key, label in (("conditioning", "conditioning (fill-d3s7 vs control-d3s7)"), ("continuation", "continuation (control-d3s7 vs prior-d3s7)"), ("zeroed", "zeroed edit (zeroed-d3s7 vs prior-d3s7)"), ("classmean", "class-mean edit (classmean-d3s7 vs prior-d3s7)"), ("fillDepth4", "fill at depth 4 (fill-d4s7 vs prior-d4s7)"), ("zeroedDepth4", "zeroed at depth 4 (zeroed-d4s7 vs prior-d4s7)")):
                r = f.get(key)
                if r:
                    extra = f"; four criteria passed: {r['passed']}" if "passed" in r else ""
                    lines.append(f"- {label}: {r['verdict']}: delta {fmt(r['meanDelta'])}, LB95 boot {fmt(r['bootstrapLower95'])}, LB95 t {fmt(r['studentTLower95'])}, UB95 {fmt(r['bootstrapUpper95'])}, W-T-L {r['wins']}-{r['ties']}-{r['losses']}, halves {fmt(r['firstHalfMeanDelta'])} / {fmt(r['secondHalfMeanDelta'])}, floor {fmt(r['detectionFloor'])}{extra}")
            for group in ("depthSteps", "direct"):
                for name, r in (f.get(group) or {}).items():
                    lines.append(f"- {group} {name}: {r['verdict']}: delta {fmt(r['meanDelta'])}, LB95 boot {fmt(r['bootstrapLower95'])}, UB95 {fmt(r['bootstrapUpper95'])}, W-T-L {r['wins']}-{r['ties']}-{r['losses']}")
            lines.append(f"Theory falsifiers: {json.dumps(f['theory'])}")
        if s.get("tree"):
            f = s["tree"]
            lines += ["", "Search-target experiment readings (three-way verdicts: supported / refuted / inconclusive):", ""]
            for key, label in (("offPathBoards", "off-path boards (treestrap-d3s7 vs searchtd-d3s7)"), ("ablation", "search targets at visited states only (searchtd-d3s7 vs prior-d3s7)"), ("vsOnePlyContinuation", "vs the one-ply continuation (treestrap-d3s7 vs control-d3s7)"), ("alternate", "the other TreeStrap step size (treestrapalt-d3s7 vs prior-d3s7)"), ("candidateVsAlternate", "candidate vs the other step size (treestrap-d3s7 vs treestrapalt-d3s7)"), ("treestrapDepth4", "treestrap at depth 4 (treestrap-d4s7 vs prior-d4s7)"), ("ablationDepth4", "searchtd at depth 4 (searchtd-d4s7 vs prior-d4s7)")):
                r = f.get(key)
                if r:
                    extra = f"; four criteria passed: {r['passed']}" if "passed" in r else ""
                    lines.append(f"- {label}: {r['verdict']}: delta {fmt(r['meanDelta'])}, LB95 boot {fmt(r['bootstrapLower95'])}, LB95 t {fmt(r['studentTLower95'])}, UB95 {fmt(r['bootstrapUpper95'])}, W-T-L {r['wins']}-{r['ties']}-{r['losses']}, halves {fmt(r['firstHalfMeanDelta'])} / {fmt(r['secondHalfMeanDelta'])}, floor {fmt(r['detectionFloor'])}{extra}")
            for group in ("depthSteps", "direct"):
                for name, r in (f.get(group) or {}).items():
                    lines.append(f"- {group} {name}: {r['verdict']}: delta {fmt(r['meanDelta'])}, LB95 boot {fmt(r['bootstrapLower95'])}, UB95 {fmt(r['bootstrapUpper95'])}, W-T-L {r['wins']}-{r['ties']}-{r['losses']}")
            lines.append(f"Theory falsifiers: {json.dumps(f['theory'])}")
        if s.get("replication"):
            lines += ["", f"Replication (prior-d3s7 vs fair-d3s7, the first experiment's frozen tables on this fresh block) passed: {s['replication']['passed']}", ""] + [f"- {'PASS' if c['passed'] else 'FAIL'} {c['criterion']}: {c.get('observed', '')}" for c in s["replication"]["checks"]]
        if s.get("scale"):
            sc = s["scale"]
            lines += ["", f"Scale (candidate-d3s7 vs prior-d3s7): {sc['verdict']}: delta {fmt(sc['meanDelta'])}, LB95 boot {fmt(sc['bootstrapLower95'])}, LB95 t {fmt(sc['studentTLower95'])}, UB95 {fmt(sc['bootstrapUpper95'])}, W-T-L {sc['wins']}-{sc['ties']}-{sc['losses']}, floor {fmt(sc['detectionFloor'])}"]
        lines.append("")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", required=True)
    parser.add_argument("--root", default=os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
    parser.add_argument("--select-arm", action="store_true")
    parser.add_argument("--select-fill-arm", action="store_true")
    parser.add_argument("--select-tree-arm", action="store_true")
    parser.add_argument("--select-gentle-arm", action="store_true")
    args = parser.parse_args()
    out = os.path.join(args.root, "runs", args.run, "ntuple-scale")
    if args.select_arm:
        print(json.dumps(select_arm(out), indent=2))
        return
    if args.select_fill_arm:
        print(json.dumps(select_fill_arm(out), indent=2))
        return
    if args.select_tree_arm:
        print(json.dumps(select_tree_arm(out), indent=2))
        return
    if args.select_gentle_arm:
        print(json.dumps(select_gentle_arm(out), indent=2))
        return
    analysis = {
        "format": "drop7-ntuple-scale-analysis-v1",
        "runId": args.run,
        "gates": gates_summary(out),
        "gatesHgt5": gates_summary(out, "gates-hgt5.log"),
        "frozenGates": {name: gates_summary(os.path.join(out, "main"), f"gates-{name}.log") for name in ("candidate", "control", "zeroed", "classmean") if os.path.exists(os.path.join(out, "main", f"gates-{name}.log"))} or None,
        "edits": load_json_if_complete(os.path.join(out, "main", "edits.json")),
        "smoke": training_summary(os.path.join(out, "smoke")),
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
