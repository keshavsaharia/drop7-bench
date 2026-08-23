# learned-leaf

A learned survival evaluator blended into the leaf of the frozen fair depth-4
expectimax, and a preregistered test of whether chance-node bias caps what leaf
foresight can contribute.

    leafValue = (1 - w) * frozen::fairLeaf(state) + w * scale * learnedValue(state)

`w = 0` short-circuits to the frozen leaf before the model is touched, so it is
the reference bit-for-bit and costs exactly what the reference costs. That is
the correctness anchor (`--parity`, 0 mismatches over 50 compared moves).

Nothing outside this directory, `build/lifetime-leaf/`, `runs/RUN-A52-LEAF/`
and `docs/exploratory/` was created or modified.

## Contents

| File | Purpose |
| --- | --- |
| `PREREGISTRATION.md` | the prediction, pass/fail rule, cohorts and budget, written before any gameplay |
| `build.sh` | builds every target with `clang++` into `build/lifetime-leaf/` |
| `leaf-probe.cpp` | measures leaf evaluations and distinct leaf states per decision: the feasibility gate that decides which model can play |
| `export_net.py`, `net.hpp`, `net-check.cpp` | export the residual CNN to a versioned binary and run it with no libtorch, no BLAS, no dynamic library |
| `parity_net.py` | numerical parity gate for the CNN, with a determinism guard (see `docs/exploratory/gpu-03-onednn-conv-nondeterminism.md`) |
| `leaf_features.py`, `train_leaf.py`, `export_leaf.py`, `leafnet.hpp`, `leaf-check.cpp`, `parity_leaf.py` | the leaf-affordable NNUE-shaped student: features, training, export, C++ inference, parity |
| `search.cpp` | the blended search, the `--parity` gate, and the `--leaf-stats` scale diagnostic |
| `analyze.py` | paired whole-game deltas, bootstrap bounds and the difference-in-differences |
| `run-stage1.sh`, `run-stage2.sh` | the exact cohort commands that were run |

## The one number that shaped every design choice

A depth-4 fair expectimax evaluates **615,090 leaves per decision at five chance
strata and 2,271,280 at seven** (`leaf-probe`). The trained CNN costs 4,122 µs
per state in the exported C++ path, so it is ~2,860x over budget at the leaf and
cannot play. The student costs 1.33 µs and can. Both are exported and both pass
their parity gates; only the student plays.

Result: [`docs/exploratory/finding-08-learned-leaf.md`](../../../docs/exploratory/finding-08-learned-leaf.md).

## A note on `runs/RUN-A52-LEAF/parity/`

`parity-cnn.json` is the gate. `parity-cnn-FIRST-ATTEMPT-nondeterministic-reference.json`
is retained deliberately: it is the same comparison run against a PyTorch
reference that had not been pinned, and it *fails* on every head. It is the
evidence for `docs/exploratory/gpu-03-onednn-conv-nondeterminism.md`, not a
result about the C++ code, and it is kept rather than deleted because a
disappearing failed gate is exactly the thing this repository's contract says
must not happen.
