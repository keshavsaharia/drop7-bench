# planner-distill

Distilling the **fair** (legal, non-clairvoyant) receding-horizon planner into a
state-only afterstate evaluator, with every legal sibling labelled under common
random numbers.

    value[c] = immediate[c] + f(afterstate[c])

`f` is the student; `immediate[c]` is supplied by the search at its chance node,
because the discs a move clears are not a function of the state after it.

## Why this target and not the previous ones

`docs/exploratory/audit-05-optimistic-curriculum.md` classifies seventeen failed
learned models here. The largest primary class is **sibling coverage /
within-root discrimination (6 of 17)**, and the oracle-distillation failures are
**representation / information gap (3 of 17)**: the teacher's advantage was
largely a function of the realised tape, which no public student can represent.

`docs/exploratory/finding-07-fair-planning-ceiling.md` supplies a teacher with
neither defect. Its arm B never reads a hidden value or a future disc, and a
self-test proves it by swapping the entire hidden board and future and requiring
an identical column. Its advantage over fair D4 is therefore public by
construction. This approach keeps the per-column value vector that
`fair-planner.hpp::fairDecision` computes and throws away.

**This is a hypothesis about why the prior attempts failed, not a proven cause.**

## Contents

| File | Purpose |
| --- | --- |
| `PREREGISTRATION.md` | theory, prediction, metrics, pass/fail rule, cohorts and budget, written before any student existed |
| `build.sh` | builds every target with `clang++` into `build/planner-distill/`, snapshotting the shared headers it compiles against |
| `corpus.hpp`, `corpus-gen.cpp` | the 576-byte root record and the teacher run that writes it; also measures the teacher's own strength on the same games |
| `expand.cpp` | independent chance realisations of every logged sibling afterstate, so the offline gate measures what the search averages |
| `fair-search.hpp` | the frozen fair expectimax with depth, strata, work bound and *leaf* as parameters, plus a root-value accessor |
| `baseline.cpp` | fair depth 4 on the same master tapes, paired at the seed |
| `d4-rank.cpp` | fair depth 4's own value vector for the same roots, and the parity gate against the frozen reference |
| `dataset.py` | the corpus reader, whole-origin splits, and the sibling panel |
| `train_student.py` | within-root listwise + pairwise training of the afterstate evaluator, in both a leaf-affordable and a CNN shape |
| `offline_gate.py` | the `docs/benchmarks.md` sibling-ranking gate, including the teacher's own split-half ceiling |
| `export_student.py`, `student.hpp` | the versioned `D7PDST` weight file and its dependency-free C++ reader |
| `play.cpp` | the student blended into the leaf of the parameterised fair search, plus `--parity` and `--leaf-stats` |
| `analyze.py` | paired whole-game bootstrap bounds and the occupancy-band flow table |
| `blend_probe.py` | does the student carry ranking signal fair depth 4 does not already have? |
| `report.py` | formats the gate JSON and evaluates the preregistered pass/fail rule mechanically |
| `parity_student.py`, `student-probe.cpp` | native/PyTorch parity and the reflection check |

## CHECK gates, all recorded before any gameplay

| Gate | Command | Result |
| --- | --- | --- |
| the whole value vector ignores hidden state | `corpus-gen --self-test` | ok |
| the logged argmax equals the frozen fair planner's | `corpus-gen --self-test` | ok |
| every legal sibling carries a value | `corpus-gen --self-test` | ok |
| the logged afterstate is the state the game really enters | `corpus-gen --self-test` | ok |
| the two half-K values average back to the full-K value | `corpus-gen --self-test` | ok |
| the parameterised search reproduces the frozen reference column | `d4-rank --parity` | 120 moves, 0 mismatches |
| the same, through the gameplay binary | `play --parity` | 120 moves, 0 mismatches |
| the exported weights match the PyTorch checkpoint | `parity_student.py`, 4,096 real afterstates | max absolute difference 8.6e-6 |
| redrawn chance realisations reproduce the true realisation's marginal | `expand`, 8,249 true vs 57,743 redrawn | clears 1.2515 vs 1.2708, survival 0.9868 vs 0.9867 |

## Outcome

**Run validity `valid`, scientific outcome `fail`, evidence tier `development`.**
The offline gate failed, so no gameplay cohort was opened and
`runs/RUN-A526-DISTILL-46d93fb7956e/run-stage5.sh` is retained unrun.

The result that matters is upstream of the student. On **160 paired master
tapes** the teacher (the legal fair planner at `H = 5, K = 256`) beats fair
depth 4 by +16,777 points (95% lower bound **−19,143**) and +5.18 moves (lower
bound **−4.56**), with an occupancy slope of **+1.4798 against +1.4813**.
`finding-07`'s eight-tape cohort put that gap at +0.1213 clears per move; on 160
tapes it is +0.0405. **A distillation can only transfer what the teacher has.**

Secondary, and useful: the teacher agrees with its own argmax only **0.8318** of
the time when its 256 completions are split in half, while the columns it cannot
separate are worth 0.0765 discs apart out of a 2.26-disc spread, so a top-1
target is a poor summary of what this teacher knows, and `normalised regret` is
the better primary metric. Nothing in this repository had measured that.

Result: [`docs/exploratory/finding-11-planner-distillation.md`](../../../docs/exploratory/finding-11-planner-distillation.md).
