# Exploratory workspace (separate namespace)

This directory holds **exploratory, second-opinion work that is deliberately
kept out of the main research record** until it has earned promotion. Nothing
here modifies `docs/research/`, `artifacts/`, frozen protocols, or any existing
approach source. It is a parallel namespace, not a replacement.

## Why this exists

The repository owner asked for two things at once:

1. progress toward the frozen million-point qualification standard, and
2. an independent second set of eyes on work that has already been attempted,
   preferring a re-examination of an existing approach over a new direction.

Audit output and new exploratory evidence are therefore written here, with
explicit provenance, so that the existing record stays intact and any
correction to it is a deliberate, reviewable act rather than a side effect.

## Namespace and isolation rules used by this work

| Concern | This workspace uses | Never touches |
| --- | --- | --- |
| Documentation | `docs/exploratory/*.md` | `docs/research/*`, `docs/*.md` |
| Source | `approaches/lifetime-objective/*` | any existing approach directory |
| Build output | `build/lifetime/` | `build/native-suite`, `build/fair-depth4` |
| Run output | `runs/RUN-A51D-*/` | `artifacts/` |
| Seeds | lease `SEEDLEASE-A51D` = `0xa51d0000`–`0xa51dffff` | every historical range |

### Seed lease SEEDLEASE-A51D

Role: **exploratory development diagnostic**. Once read, these seeds are
development data forever and can never become confirmation evidence.

The range was chosen by extracting every 8-hex-digit constant appearing in
`docs/research/history.md`, `approaches/`, `src/`, `research/`, and
`artifacts/`, and selecting a prefix that appears nowhere. Historical gameplay
seeds cluster densely in `0x3d000000`–`0x3ea00000`; the `0xa51d` prefix is
unused. This is a conservative choice, not an authorization to allocate
`STANDARD`, `QUALIFY`, `PROTECTED`, or `FINAL` cohorts — none of those are
claimed here, and protected and final seeds remain unopened.

## Contents

| File | What it is |
| --- | --- |
| [`audit-01-engine-fidelity.md`](audit-01-engine-fidelity.md) | Independent audit of the native and TypeScript rules engines |
| [`audit-02-fair-d4.md`](audit-02-fair-d4.md) | Independent audit of the fair-D4 reference policy |
| [`audit-03-claim-arithmetic.md`](audit-03-claim-arithmetic.md) | Recomputation of every numeric claim in the historical ledger |
| [`audit-04-blind-spots.md`](audit-04-blind-spots.md) | Which rejections were clean and which were confounded |
| [`audit-05-optimistic-curriculum.md`](audit-05-optimistic-curriculum.md) | Whether optimistic-teacher curriculum *ordering* was ever ablated |
| [`finding-01-score-is-survival.md`](finding-01-score-is-survival.md) | Measured decomposition of where Drop7 Hardcore score actually comes from |
| [`finding-04-terminal-utility-saturated.md`](finding-04-terminal-utility-saturated.md) | Valid negative: D4's death penalty is at its stop and is not a lever |
| [`finding-05-chance-strata.md`](finding-05-chance-strata.md) | Positive, replicated: exact chance enumeration unlocks the fourth ply |
| [`lease-map.md`](lease-map.md) | Seed allocation table and the lease-overlap incident |
| [`finding-06-flow-ceiling.md`](finding-06-flow-ceiling.md) | Flow balance is achievable clairvoyantly; the occupancy equilibrium at ~20 cells |
| [`finding-07-fair-planning-ceiling.md`](finding-07-fair-planning-ceiling.md) | Splitting the clairvoyant advantage into planning vs hidden information |
| [`finding-08-learned-leaf.md`](finding-08-learned-leaf.md) | Learned survival leaf inside the search; a preregistered prediction refuted |
| [`finding-09-reveal-sampling.md`](finding-09-reveal-sampling.md) | Chance-node decorrelation is exchangeable with search depth |
| [`finding-10-suite-validation.md`](finding-10-suite-validation.md) | The scenario suite fails its own validation gate; leaf weights point the wrong way |
| [`finding-11-planner-distillation.md`](finding-11-planner-distillation.md) | Distilling a legal planner; negative, and the 8-tape cohort correction |
| [`finding-12-fair-planner-ceiling-extended.md`](finding-12-fair-planner-ceiling-extended.md) | Pushing the legal planner's ceiling, and re-baselining it |
| [`finding-13-fast-engine.md`](finding-13-fast-engine.md) | 3.08x bit-exact engine speedup, and a 10.4x error in the cost model |
| `finding-14-leaf-reweight.md` *(in progress)* | Refitting the frozen leaf's weights toward achievable clear rate |
| [`finding-15-depth5-exact-estimator.md`](finding-15-depth5-exact-estimator.md) | Depth 5 buys nothing at either stratum count; the interim "reversal" is withdrawn as completion-order bias (see its §8) |
| [`finding-16-factored-reveal-sampling.md`](finding-16-factored-reveal-sampling.md) | Depth and chance resolution substitute rather than compound; the budget frontier is flat on top near fair D4 |
| [`gpu-02-openblas-sgemm-race.md`](gpu-02-openblas-sgemm-race.md) | numpy float32 matmul is silently corrupt on this host |
| [`gpu-03-onednn-conv-nondeterminism.md`](gpu-03-onednn-conv-nondeterminism.md) | torch Conv2d is nondeterministic on this host's CPU |
| [`reconciliation-01.md`](reconciliation-01.md) | Which contributor owns what, and every seed lease |
| [`proposed-status-corrections.md`](proposed-status-corrections.md) | Proposed edits to the main record, split safe-now vs needs-replication |
| [`design-01-benchmark-suite.md`](design-01-benchmark-suite.md) | Design of the fully-specified scenario benchmark and its validity gates |
| `gpu-01-rocm-enablement.md` *(in progress)* | Whether the gfx1151 iGPU is usable for training, with benchmarks |
| [`finding-02-scenario-benchmark.md`](finding-02-scenario-benchmark.md) | Scenario engine parity, exact solver, and board-clear reachability |
| [`finding-03-rollout-veto-17k.md`](finding-03-rollout-veto-17k.md) | Retest of the 25-move rollout veto under corrected scoring |


## Status of everything in this directory

Exploratory. Evidence tier is at most **pilot** or **development**. No result
here is a qualification claim, and no result here upgrades the historical
ledger. Promotion into `docs/research/` requires the coordinator to review the
underlying run records first.
