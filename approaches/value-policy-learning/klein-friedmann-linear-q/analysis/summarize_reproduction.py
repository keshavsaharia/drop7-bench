"""Gate checks for EX-20260902-kf-report-reproduction-b7f61bf1.

Usage: summarize_reproduction.py RUN_DIR   (the kf-reproduction directory)
Reads seed10.json, seed11.json, seed12.json, seed10-noreg.json, seed10-300.json
and writes summary.json next to them.  Bands are the preregistered ones:
random mean in [30.2, 32.2]; agent test mean in [47.6, 51.6]; 300-game arm
within 2.0 moves of the 50,000-game arm; lambda = 0 arm compared with random.
"""
import json
import os
import sys

run_dir = sys.argv[1]


def load(name):
    p = os.path.join(run_dir, name)
    return json.load(open(p)) if os.path.exists(p) else None


arms = {n: load(n + ".json") for n in ("seed10", "seed11", "seed12", "seed10-noreg", "seed10-300")}
summary = {"arms": {}, "gateChecks": []}
for name, d in arms.items():
    if d is None:
        summary["arms"][name] = None
        continue
    summary["arms"][name] = {
        "args": d["args"],
        "random": d.get("random"),
        "train": d["train"],
        "test": d["test"],
        "numItersAfterTrain": d["numItersAfterTrain"],
        "weights": d["weights"],
        "trainBlockMeans1000": d["trainBlockMeans1000"],
        "wallSeconds": d["wallSeconds"],
        "upstreamCommit": d["upstreamCommit"],
        "upstreamFileSha256": d["upstreamFileSha256"],
    }

def check(criterion, passed, observed):
    summary["gateChecks"].append({"criterion": criterion, "passed": passed, "observed": observed})

main = [arms[n] for n in ("seed10", "seed11", "seed12") if arms[n]]
if len(main) == 3:
    rm = [d["random"]["mean"] for d in main]
    check("Random: 5,000-game uniform-random mean within [30.2, 32.2] for each of the three seeds",
          all(30.2 <= m <= 32.2 for m in rm), f"means {['%.3f' % m for m in rm]}")
    tm = [d["test"]["mean"] for d in main]
    check("Agent: 10,000-game test mean within [47.6, 51.6] for each of the three lambda = 0.1 seeds",
          all(47.6 <= m <= 51.6 for m in tm), f"means {['%.3f' % m for m in tm]} (report 49.61)")
else:
    check("Random band", None, "arms missing")
    check("Agent band", None, "arms missing")
if arms["seed10"] and arms["seed10-300"]:
    a, b = arms["seed10"]["test"]["mean"], arms["seed10-300"]["test"]["mean"]
    check("Clause (ii-a): the 300-game arm's test mean is within 2.0 moves of the seed-10 50,000-game arm on the same test seeds",
          abs(a - b) <= 2.0, f"50,000-game {a:.3f}; 300-game {b:.3f}; difference {a - b:+.3f}")
else:
    check("Clause (ii-a)", None, "arms missing")
if arms["seed10"] and arms["seed10-noreg"]:
    r, nr, reg = arms["seed10"]["random"]["mean"], arms["seed10-noreg"]["test"]["mean"], arms["seed10"]["test"]["mean"]
    check("Clause (ii-b): the lambda = 0 arm's test mean compared with the random mean (report's Figure 5 predicts below random)",
          nr < r, f"lambda = 0 test mean {nr:.3f}; random {r:.3f}; lambda = 0.1 test mean {reg:.3f}; 'passed' here means the report's prediction held")
else:
    check("Clause (ii-b)", None, "arms missing")
json.dump(summary, open(os.path.join(run_dir, "summary.json"), "w"), indent=2)
for c in summary["gateChecks"]:
    print(("PASS" if c["passed"] else "FAIL" if c["passed"] is False else "----"), c["criterion"], "|", c["observed"])
