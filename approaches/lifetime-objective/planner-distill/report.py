"""Formats the offline-gate JSON into the markdown tables the finding uses, and
evaluates the preregistered pass/fail rule mechanically.

The rule is read from `PREREGISTRATION.md` section 4 and implemented here rather
than judged by eye:

    PASS      T1_s > T1_d4 and PW_s > PW_d4 and REG_s < REG_d4,
              and the student wins on top-1 in >= 4 of 5 held-out origin folds,
              and T1_s >= 0.60 * (teacher split-half agreement)
    PARTIAL   beats fair D4 on pairwise and regret but not top-1, or wins in
              aggregate but in fewer than 4 of 5 folds
    FAIL      does not beat fair D4 on at least two of the three headline
              statistics
"""

from __future__ import annotations

import os

os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")

import json
import sys


def verdict(gate: dict) -> dict:
    # The headline arm is fixed in advance: the eight-realisation score with
    # the validation-fitted affine map, restricted to the comparator's roots
    # whenever the comparator covers them, so both are read on one denominator.
    student = (gate.get("studentOnComparatorRoots")
               or gate.get("studentMultiDrawRecalibrated")
               or gate["studentMultiDraw"])
    d4 = gate.get("fairD4")
    ceiling = gate.get("teacherSplitHalfAgreementOnComparatorRoots",
                       gate["teacherSplitHalfAgreement"])
    folds = gate.get("folds", [])
    fold_wins = sum(1 for f in folds
                    if "fairD4Top1" in f and
                    f.get("studentTop1SameRoots", -1) > f["fairD4Top1"])
    fold_total = sum(1 for f in folds if "fairD4Top1" in f)
    out = {
        "studentTop1": student["top1"],
        "studentPairwise": student["pairwise"],
        "studentRegret": student["normalisedRegret"],
        "teacherSplitHalfCeiling": ceiling,
        "ceilingFloor60pct": 0.60 * ceiling,
        "foldWins": fold_wins,
        "foldTotal": fold_total,
    }
    if d4 is None:
        out["outcome"] = "inconclusive (no comparator)"
        return out
    out.update({"fairD4Top1": d4["top1"], "fairD4Pairwise": d4["pairwise"],
                "fairD4Regret": d4["normalisedRegret"]})
    beats = [student["top1"] > d4["top1"],
             student["pairwise"] > d4["pairwise"],
             student["normalisedRegret"] < d4["normalisedRegret"]]
    out["headlineStatisticsBeaten"] = sum(beats)
    passes = (all(beats) and fold_wins >= 4 and
              student["top1"] >= 0.60 * ceiling)
    if passes:
        out["outcome"] = "pass"
    elif sum(beats) >= 2:
        out["outcome"] = "partial"
    else:
        out["outcome"] = "fail"
    return out


def table(rows: list, headers: list) -> str:
    line = "| " + " | ".join(headers) + " |"
    rule = "| " + " | ".join("---" for _ in headers) + " |"
    body = ["| " + " | ".join(str(cell) for cell in row) + " |" for row in rows]
    return "\n".join([line, rule] + body)


def fmt(value, digits=4):
    if value is None:
        return "—"
    if isinstance(value, float):
        return f"{value:.{digits}f}"
    return str(value)


def main() -> None:
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    for path in sys.argv[1:]:
        with open(path) as handle:
            gate = json.load(handle)
        name = os.path.basename(path).replace("gate-", "").replace(".json", "")
        print(f"\n### {name}\n")
        print(json.dumps(verdict(gate), indent=2))
        rows = []
        for label, key in (
            ("teacher, half the completions", "teacherHalfAsStudent"),
            ("student, 1 realisation", "student1Draw"),
            ("student, 1 realisation, recalibrated", "student1DrawRecalibrated"),
            ("student, 8 realisations", "studentMultiDraw"),
            ("student, 8 realisations, recalibrated", "studentMultiDrawRecalibrated"),
            ("student, comparator roots only", "studentOnComparatorRoots"),
            ("fair depth 4", "fairD4"),
        ):
            block = gate.get(key)
            if not block:
                continue
            rows.append([label, fmt(block["top1"]), fmt(block["top2"]),
                         fmt(block["pairwise"]), fmt(block["normalisedRegret"]),
                         block["roots"]])
        for label, key in gate.get("references", {}).items():
            rows.append([f"reference: {label}", fmt(key["top1"]),
                         fmt(key["top2"]), fmt(key["pairwise"]),
                         fmt(key["normalisedRegret"]), key["roots"]])
        print()
        print(table(rows, ["arm", "top-1", "top-2", "pairwise", "norm. regret",
                           "roots"]))
        print()
        for name_, block in gate.get("breakouts", {}).items():
            rows = [[key, value["roots"], fmt(value["top1"])]
                    for key, value in sorted(block.items(),
                                             key=lambda kv: float(kv[0]))]
            print(f"\n**{name_}**\n")
            print(table(rows, [name_, "roots", "student top-1"]))
        if gate.get("folds"):
            rows = [[f["fold"], f["roots"], fmt(f.get("studentTop1SameRoots")),
                     fmt(f.get("fairD4Top1")), fmt(f["studentPairwise"]),
                     fmt(f.get("fairD4Pairwise"))]
                    for f in gate["folds"]]
            print("\n**origin folds**\n")
            print(table(rows, ["fold", "roots", "student top-1",
                               "fair D4 top-1", "student pairwise",
                               "fair D4 pairwise"]))
        print("\n**calibration**\n")
        cal = gate.get("calibration", {})
        print(json.dumps({k: v for k, v in cal.items() if k != "reliability"},
                         indent=2))


if __name__ == "__main__":
    main()
