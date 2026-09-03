"""Seed-free exports from the upstream simulator for the Rust port's CHECK
gates (EX-20260902-kf-linear-q-rust-transfer-4328a730, G1 and G2).

  --mode features  one line per (state, action):
      <board49 engine encoding> <next> <action> <mask6> <v0> <v1> <v2> <v3> <v4> <v5>
    States come from complete upstream games, half played uniformly at random
    and half greedily by a briefly trained upstream learner, so tall and
    cluttered boards are covered as well as the opening.
  --mode update    one line per upstream incorporateFeedback call:
      S <t> <reward> <mask> <v0..v5> T <w0..w5>
      S <t> <reward> <mask> <v0..v5> N <7 x (<mask> <v0..v5>)> <w0..w5>
    t is upstream numIters at the call; w are the weights after the update,
    in the fixed key order below, written with repr() so they round-trip.

Encoding swap: upstream 9 (untouched gray) -> engine 8; upstream 8 (cracked)
-> engine 9.  Upstream rows count from the bottom; the engine string is
row-major from the top.  Python's RNG is seeded, so the export is
reproducible and reads no repository seed.
"""
import argparse
import os
import random
import sys

ap = argparse.ArgumentParser()
ap.add_argument("--upstream", required=True)
ap.add_argument("--mode", choices=["features", "update"], required=True)
ap.add_argument("--games", type=int, default=200)
ap.add_argument("--seed", type=int, default=0)
ap.add_argument("--out", required=True)
args = ap.parse_args()

sys.path.insert(0, args.upstream)
import main as upstream_main  # noqa: E402
import util  # noqa: E402

qsrc_path = os.path.join(args.upstream, "Drop7QLearning.py")
definitions = open(qsrc_path).read().split("print('start')")[0]
ns = {}
exec(compile(definitions, qsrc_path, "exec"), ns)
QLearningAlgorithm = ns["QLearningAlgorithm"]
Drop7FeatureExtractor = ns["Drop7FeatureExtractor"]

KEYS = ["min_eq_elem_True", "row_dets", "col_dets", "max_eq_elem", "1_dets", "elem_det"]
ACTIONS = [0, 1, 2, 3, 4, 5, 6]


def mask_values(feature_list):
    d = dict(feature_list)
    assert len(d) == len(feature_list), "duplicate feature key"
    assert set(d) <= set(KEYS), f"unexpected key in {d}"
    mask = "".join("1" if k in d else "0" for k in KEYS)
    values = [int(d.get(k, 0)) for k in KEYS]
    return mask, values


def board_string(state):
    out = []
    for row in range(7):  # engine row 0 is the top
        y = 6 - row
        for x in range(7):
            v = state.field.elements[x][y]
            if v == 9:
                out.append("8")
            elif v == 8:
                out.append("9")
            else:
                assert isinstance(v, int) and 0 <= v <= 7, v
                out.append(str(v))
    return "".join(out)


def new_game():
    field = upstream_main.Field(7)
    state = upstream_main.Drop7()
    state.set_field(field)
    return state


random.seed(args.seed)
lines = []

if args.mode == "features":
    greedy = QLearningAlgorithm(ACTIONS, 1, Drop7FeatureExtractor, explorationProb=0.2)
    util.simulate(greedy, numTrials=300)
    greedy.explorationProb = 0
    pairs = 0
    for g in range(args.games):
        state = new_game()
        use_greedy = g % 2 == 1
        while not state.endGame:
            board = board_string(state)
            next_disc = state.curr_elem
            for a in ACTIONS:
                mask, values = mask_values(Drop7FeatureExtractor(state, a))
                lines.append(f"{board} {next_disc} {a} {mask} {' '.join(map(str, values))}")
                pairs += 1
            action = greedy.getAction(state) if use_greedy else random.randint(0, 6)
            upstream_main.playDrop7(None, state, action)
    print(f"features: games {args.games} pairs {pairs}", file=sys.stderr)
else:
    class Logging(QLearningAlgorithm):
        def incorporateFeedback(self, state, action, reward, newState):
            t = self.numIters
            mask, values = mask_values(self.featureExtractor(state, action))
            parts = ["S", str(t), repr(float(reward)), mask, *map(str, values)]
            if newState:
                parts.append("N")
                for a in self.actions:
                    m2, v2 = mask_values(self.featureExtractor(newState, a))
                    parts.append(m2)
                    parts.extend(map(str, v2))
            else:
                parts.append("T")
            super().incorporateFeedback(state, action, reward, newState)
            parts.extend(repr(float(self.weights.get(k, 0.0))) for k in KEYS)
            lines.append(" ".join(parts))

    learner = Logging(ACTIONS, 1, Drop7FeatureExtractor, explorationProb=0.2)
    util.simulate(learner, numTrials=args.games)
    print(f"update: games {args.games} steps {len(lines)} final weights {dict(learner.weights)}", file=sys.stderr)

with open(args.out, "w") as f:
    f.write("\n".join(lines) + "\n")
