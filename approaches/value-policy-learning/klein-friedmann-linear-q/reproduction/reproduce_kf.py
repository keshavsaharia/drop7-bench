"""Reproduce Klein & Friedmann, "Final Report - Drop7" (Stanford CS221), from
their own code at github.com/ekreate/cs221-final-project commit 8cc8a0e.

The upstream simulator (main.py), homework scaffold (util.py), feature extractor
and Q-learning class (Drop7QLearning.py) run unchanged. Two things differ, both
disclosed in EX-20260902-kf-report-reproduction-b7f61bf1: the per-step weight
history list is not kept (it grows to millions of floats), and the ridge
constant hard-coded as 0.1 on upstream line 79 is exposed as --lam so the
lambda = 0 arm can be run. Python's global RNG is reseeded at each phase so the
test games of every arm with the same --seed are identical.

Usage: reproduce_kf.py --upstream DIR --seed N --train 50000 --test 10000 --random 5000 --lam 0.1 --out FILE
"""
import argparse
import hashlib
import json
import os
import random
import statistics
import sys
import time

ap = argparse.ArgumentParser()
ap.add_argument("--upstream", required=True, help="directory holding the fetched upstream files")
ap.add_argument("--seed", type=int, default=0)
ap.add_argument("--train", type=int, default=50000)
ap.add_argument("--test", type=int, default=10000)
ap.add_argument("--random", type=int, default=5000)
ap.add_argument("--lam", type=float, default=0.1)
ap.add_argument("--out", required=True)
args = ap.parse_args()

sys.path.insert(0, args.upstream)
import main as upstream_main  # noqa: E402
import util  # noqa: E402

qsrc_path = os.path.join(args.upstream, "Drop7QLearning.py")
qsrc = open(qsrc_path).read()
# The module body runs a 500-game experiment on import; execute definitions only.
definitions = qsrc.split("print('start')")[0]
ns = {}
exec(compile(definitions, qsrc_path, "exec"), ns)
QLearningAlgorithm = ns["QLearningAlgorithm"]
Drop7FeatureExtractor = ns["Drop7FeatureExtractor"]

hashes = {
    f: hashlib.sha256(open(os.path.join(args.upstream, f), "rb").read()).hexdigest()
    for f in ("main.py", "util.py", "Drop7QLearning.py")
}
KEYS = ["min_eq_elem_True", "row_dets", "col_dets", "max_eq_elem", "1_dets", "elem_det"]


class QLearningRidge(QLearningAlgorithm):
    """Upstream incorporateFeedback (line 64-80) verbatim, with the ridge
    constant as a parameter and without the weight_plot history."""

    def __init__(self, *a, lam=0.1, **k):
        super().__init__(*a, **k)
        self.lam = lam

    def incorporateFeedback(self, state, action, reward, newState):
        w = self.weights
        phi = self.featureExtractor(state, action)
        eta = self.getStepSize()
        Q = self.getQ(state, action)
        v_opt = -1
        if newState:
            for cur_action in self.actions:
                v = self.getQ(newState, cur_action)
                v_opt = max(v, v_opt)
        else:
            v_opt = float(0)
        for feature in phi:
            key, val = feature
            self.weights[key] = w[key] - eta * ((Q - (reward + self.discount * v_opt)) * val + self.lam * w[key])


def summary(xs):
    if not xs:
        return None
    s = sorted(xs)
    return {
        "n": len(s),
        "mean": statistics.mean(s),
        "sd": statistics.stdev(s) if len(s) > 1 else 0.0,
        "median": s[len(s) // 2],
        "min": s[0],
        "max": s[-1],
        "capped200": sum(1 for x in s if x >= 200),
    }


out = {"args": vars(args), "upstreamCommit": "8cc8a0e", "upstreamFileSha256": hashes}
t0 = time.time()

if args.random > 0:
    random.seed(args.seed)
    scores = []
    for _ in range(args.random):
        field = upstream_main.Field(7)
        state = upstream_main.Drop7()
        state.set_field(field)
        while not state.endGame:
            upstream_main.playDrop7(None, state, random.randint(0, 6))
        scores.append(state.iteration)
    out["random"] = summary(scores)
    out["randomScores"] = scores
    print("random", out["random"], f"{time.time() - t0:.0f}s", flush=True)

random.seed(args.seed + 1000)
learner = QLearningRidge([0, 1, 2, 3, 4, 5, 6], 1, Drop7FeatureExtractor, explorationProb=0.2, lam=args.lam)
train_scores = util.simulate(learner, numTrials=args.train)
out["train"] = summary(train_scores)
out["trainScores"] = train_scores
out["trainBlockMeans1000"] = [statistics.mean(train_scores[i:i + 1000]) for i in range(0, len(train_scores), 1000)]
out["numItersAfterTrain"] = learner.numIters
out["weights"] = {k: learner.weights.get(k, 0.0) for k in KEYS}
print("train", out["train"], "numIters", learner.numIters, f"{time.time() - t0:.0f}s", flush=True)
print("weights", {k: round(v, 3) for k, v in out["weights"].items()}, flush=True)

random.seed(args.seed + 2000)
learner.explorationProb = 0
test_scores = util.simulate(learner, numTrials=args.test)
out["test"] = summary(test_scores)
out["testScores"] = test_scores
out["wallSeconds"] = time.time() - t0
print("test", out["test"], f"{time.time() - t0:.0f}s", flush=True)

json.dump(out, open(args.out, "w"))
with open(args.out + ".weights.txt", "w") as f:
    for k in KEYS:
        f.write(f"{k} {out['weights'][k]!r}\n")
