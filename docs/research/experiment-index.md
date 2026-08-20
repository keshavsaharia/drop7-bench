# Experiment index

This index maps the current research source tree to the question each program
asks and to the strongest evidence presently available for it. It covers all
139 approach sources and all 41 shared core and test sources in the repository.
It is an inventory, not a claim that every executable has a durable result.

## How to read the evidence

- **Repository-verified** means the implementation or test is present and its
  purpose can be established from the current source. It does not prove that a
  complete experimental run occurred.
- **Ledger-recorded** means [history.md](history.md) records a protocol and
  result. These are the most durable experiment claims available here.
- **Task-record only** means the referenced research conversation reports a
  result that has not yet been promoted into `history.md`. Treat the number as
  provisional until an artifact or ledger entry is added.

Status labels are deliberately narrow:

- **Completed**: the run or diagnostic finished and produced usable evidence.
- **Rejected**: the tested candidate failed its frozen gate or was explicitly
  retired. This rejects that configuration, not every future method in its
  family.
- **Runtime-paused**: cost or resource limits stopped the experiment before a
  decisive policy comparison.
- **Preregistered**: a bounded implementation or protocol exists, but no result
  is established.
- **Support-only**: infrastructure, a benchmark, or a data-producing diagnostic;
  it is not itself a candidate policy.
- **Unknown**: the source exists, but neither the ledger nor the task record
  establishes a trustworthy outcome.

Buildability is a separate property. Several Sequence-scored sources retain a
`kLevelBonus == 7'000` assertion so they cannot be run accidentally with the
corrected 17,000-point Hardcore engine. The optional PyTorch environment also
requires pybind11 include flags. These intentional boundaries are described in
the [reproducibility guide](../reproducibility.md#historical-7000-point-source-locks).

The current strongest public-information reference remains completed full-width
fair depth 4. The ledger reports about 308,000 points over a broad 64-game
already-used cohort and about 401,000 over a smaller eight-game corrected-score
confirmation. Those are development results, not protected validation, and the
one-million-point average remains open.

## Shared core

These files implement reusable mechanics and policy components. Their status is
about software support, not playing strength.

| Component and sources | Purpose | Status and evidence |
| --- | --- | --- |
| Native engine: [engine.hpp](../../src/core/native/engine.hpp) | Exact headless Hardcore transitions, scoring, rises, reveals, and seeded play. | **Support-only — repository-verified;** current parity reproduced 6,852 matching transitions. |
| Native public policy support: [public-behavior.hpp](../../src/core/native/public-behavior.hpp) | Shared public-state phase evaluator and behavior-policy primitives used by many native experiments. | **Support-only — repository-verified.** |
| Native n-tuple support: [ntuple.hpp](../../src/core/native/ntuple.hpp), [ntuple-search.hpp](../../src/core/native/ntuple-search.hpp) | Reflection-safe n-tuple storage, training, and bounded learned-value search. | **Support-only — repository-verified;** self-test results are ledger-recorded. |
| Native PPO support: [ppo.hpp](../../src/core/native/ppo.hpp) | Common policy/value-network and PPO training utilities. | **Support-only — repository-verified.** |
| TypeScript engine: [engine.ts](../../src/core/typescript/engine.ts), [engine.test.ts](../../src/core/typescript/engine.test.ts) | Runtime-neutral game rules and deterministic transition tests. | **Support-only — repository-verified;** 122 current tests pass and native parity is exact. |
| Baseline expectimax: [solver.ts](../../src/core/typescript/solver.ts), [solver.test.ts](../../src/core/typescript/solver.test.ts) | Iterative-deepening max/chance search with bounded, mirror-canonical caching. | **Support-only — repository-verified.** |
| Hand evaluator: [heuristic.ts](../../src/core/typescript/heuristic.ts), [heuristic.test.ts](../../src/core/typescript/heuristic.test.ts) | Immediate score, chain readiness, cover access, height risk, and low-number congestion features. | **Support-only — repository-verified.** |
| Headless tournaments: [headless.ts](../../src/core/typescript/headless.ts), [headless.test.ts](../../src/core/typescript/headless.test.ts) | Reproducible complete games, paired seeds, censoring, and summary statistics. | **Support-only — repository-verified.** |
| Phase-horizon evaluator: [phase-horizon-evaluator.ts](../../src/core/typescript/phase-horizon-evaluator.ts), [phase-horizon-evaluator.test.ts](../../src/core/typescript/phase-horizon-evaluator.test.ts) | Phase-aware leaf terms keyed to the five-move rise clock. | **Support-only — repository-verified.** |
| Tunneling evaluator: [tunneling-heuristic.ts](../../src/core/typescript/tunneling-heuristic.ts), [tunneling-heuristic.test.ts](../../src/core/typescript/tunneling-heuristic.test.ts) | Rewards moves that create reachable damage paths through covered discs. | **Support-only — repository-verified.** |
| Recursive potential: [recursive-potential.ts](../../src/core/typescript/recursive-potential.ts), [recursive-potential.test.ts](../../src/core/typescript/recursive-potential.test.ts) | Recursively values delayed trigger chains and excludes already-overshot clogs. | **Support-only — repository-verified.** |
| Virtual ignition: [virtual-ignition.ts](../../src/core/typescript/virtual-ignition.ts), [virtual-ignition.test.ts](../../src/core/typescript/virtual-ignition.test.ts) | Simulates hypothetical trigger placements to estimate stored chain energy. | **Support-only — repository-verified.** |
| Rollout solver: [rollout-solver.ts](../../src/core/typescript/rollout-solver.ts), [rollout-solver.test.ts](../../src/core/typescript/rollout-solver.test.ts) | Bounded Monte Carlo continuation search over public information. | **Support-only — repository-verified.** |
| MCTS solver: [mcts-solver.ts](../../src/core/typescript/mcts-solver.ts), [mcts-solver.test.ts](../../src/core/typescript/mcts-solver.test.ts) | Observable-state Monte Carlo tree search component. | **Support-only — repository-verified.** |
| Sparse expectimax: [sparse-expectimax.ts](../../src/core/typescript/sparse-expectimax.ts), [sparse-expectimax.test.ts](../../src/core/typescript/sparse-expectimax.test.ts) | Full-width root search with sampled chance branches and reduced internal action width. | **Support-only — repository-verified.** |
| Risk-sensitive planning: [risk-sensitive-planner.ts](../../src/core/typescript/risk-sensitive-planner.ts), [risk-sensitive-planner.test.ts](../../src/core/typescript/risk-sensitive-planner.test.ts) | Mean/lower-tail aggregation for public stochastic rollouts. | **Support-only — repository-verified.** |
| Open-loop beam: [robust-open-loop-beam.ts](../../src/core/typescript/robust-open-loop-beam.ts), [robust-open-loop-beam.test.ts](../../src/core/typescript/robust-open-loop-beam.test.ts) | Common-root open-loop planning across synthetic future scenarios. | **Support-only — repository-verified.** |
| Sampled beam solver: [sampled-beam-solver.ts](../../src/core/typescript/sampled-beam-solver.ts), [sampled-beam-solver.test.ts](../../src/core/typescript/sampled-beam-solver.test.ts) | Fixed-width, fixed-memory beam search over sampled public outcomes. | **Support-only — repository-verified.** |
| Learned evaluator: [learned-evaluator.ts](../../src/core/typescript/learned-evaluator.ts), [learned-evaluator.test.ts](../../src/core/typescript/learned-evaluator.test.ts) | Sparse NNUE-style board and policy inference used by TypeScript training labs. | **Support-only — repository-verified.** |
| Monte Carlo return policy: [mc-return-policy.ts](../../src/core/typescript/mc-return-policy.ts), [mc-return-policy.test.ts](../../src/core/typescript/mc-return-policy.test.ts) | Action features and network inference for episode-return learning. | **Support-only — repository-verified.** |
| Gray-throughput policy: [gray-throughput-policy.ts](../../src/core/typescript/gray-throughput-policy.ts), [gray-throughput-policy.test.ts](../../src/core/typescript/gray-throughput-policy.test.ts) | Rule policy emphasizing cracks, reveals, cover altitude, and occupancy. | **Support-only — repository-verified.** |
| Gray-throughput rollout: [gray-throughput-rollout.ts](../../src/core/typescript/gray-throughput-rollout.ts), [gray-throughput-rollout.test.ts](../../src/core/typescript/gray-throughput-rollout.test.ts) | Monte Carlo continuation around the gray-throughput rule policy. | **Support-only — repository-verified.** |

## Baselines and diagnostics

| Approach and sources | Purpose | Status and evidence |
| --- | --- | --- |
| Heuristic benchmark: [main.ts](../../approaches/baselines-diagnostics/heuristic-benchmark/main.ts) | Runs paired complete-game comparisons among named TypeScript heuristic profiles. | **Support-only — repository-verified.** |
| Native parity: [main.ts](../../approaches/baselines-diagnostics/native-parity/main.ts) | Compares native and TypeScript traces byte for byte. | **Completed — reproduced;** 256 seeds and 6,852 transitions matched in this checkout. |
| Tie-breaking: [main.ts](../../approaches/baselines-diagnostics/tie-breaking/main.ts) | Measures sensitivity to fixed legal-column ordering. | **Support-only — repository-verified.** |
| Phase benchmark: [phase-benchmark.cpp](../../approaches/baselines-diagnostics/phase-benchmark/phase-benchmark.cpp) | Reports score, survival, clear/reveal flow, height, work, and censoring for the shared phase policy. | **Support-only — repository-verified;** no durable standalone result was located. |
| D4 flow audit: [d4-flow-audit.cpp](../../approaches/baselines-diagnostics/d4-flow/d4-flow-audit.cpp) | Emits per-move D4 geometry, flow, and root-value traces on already-consumed seeds. | **Support-only — repository-verified.** |
| Trajectory throughput: [main.ts](../../approaches/baselines-diagnostics/trajectory-throughput/main.ts) | Compares matched windows from public policies and a privileged ceiling to locate regenerative clear/reveal regimes. | **Completed — task-record only;** this analysis motivated the approximate 2.4-clear and 1.4-reveal sustainability targets. |
| Cover-throughput probe: [throughput-probe.cpp](../../approaches/baselines-diagnostics/throughput-probe/throughput-probe.cpp) | Tests large immediate crack/reveal weights on historical seeds. | **Rejected — task-record only;** the strongest profile was nearly flat, so larger local cover weights were not promoted. |

## Heuristic and rollout search

| Approach and sources | Purpose | Status and evidence |
| --- | --- | --- |
| Sparse expectimax lab: [main.ts](../../approaches/heuristic-search/sparse-expectimax/main.ts) | Benchmarks the sampled, reduced-width expectimax component over complete games. | **Completed — task-record only;** it established a useful bounded-search baseline, still well below the target. |
| Open-loop lab: [main.ts](../../approaches/heuristic-search/open-loop/main.ts) | Evaluates public open-loop synthetic-future planning. | **Support-only — repository-verified;** it primarily serves as a comparator. |
| Phase-horizon policy: [main.ts](../../approaches/heuristic-search/phase-horizon/main.ts) | Tests phase-conditioned safety and rise-readiness weights. | **Rejected — task-record only;** the frozen policy improved some games but failed its 400,000-point gate. |
| Tunneling policy: [main.ts](../../approaches/heuristic-search/tunneling/main.ts) | Adds transition credit for damaging high or edge covered discs through reachable paths. | **Completed — task-record only;** a 64-game comparison improved the open-loop baseline, but did not approach qualification. |
| Virtual ignition lab: [main.ts](../../approaches/heuristic-search/virtual-ignition/main.ts) | Tests explicit stored-chain energy under hypothetical triggers. | **Rejected — task-record only;** the small gain did not justify its substantial search cost. |
| Risk-sensitive lab: [main.ts](../../approaches/heuristic-search/risk-sensitive/main.ts) | Applies lower-tail/CVaR weighting to sampled rollouts. | **Rejected — task-record only;** the best mild-risk pilot trailed the phase baseline. |
| Policy comparison: [main.ts](../../approaches/heuristic-search/policy-comparison/main.ts) | Common harness for comparing heuristic and planning policies. | **Support-only — repository-verified.** |
| Gray-throughput tuning: [tune.ts](../../approaches/heuristic-search/gray-throughput/tune.ts), [benchmark.ts](../../approaches/heuristic-search/gray-throughput/benchmark.ts) | Tunes and evaluates policies that prioritize covered-disc damage and removal flow. | **Rejected — task-record only;** stronger gray weights improved exposure but not sustainable cracks/reveals or score. |
| Native rollout planner: [rollout.cpp](../../approaches/heuristic-search/rollout/rollout.cpp) | Evaluates root actions with bounded public continuations and configurable horizons/scenarios. | **Rejected — task-record only;** the tested 25-move control reproduced the weak continuation policy's mistakes. |
| Native rollout teacher: [teacher.cpp](../../approaches/heuristic-search/rollout/teacher.cpp) | Produces higher-cost common-tape action labels for rollout and distillation studies. | **Completed — task-record only;** useful as a teacher/benchmark, not a deployable policy. |
| Five-move cycle abstraction: [cycle-abstraction.cpp](../../approaches/heuristic-search/cycle-abstraction/cycle-abstraction.cpp) | Tests whether 41 cycle-level features can rank five-move options. | **Rejected — task-record only;** survival prediction was strong but option ranking was weak. |
| Edge-cover priority: [edge-priority-lab.cpp](../../approaches/heuristic-search/edge-priority/edge-priority-lab.cpp) | Screens nonlinear cover-altitude and edge multipliers with paired confirmation gates. | **Unknown — repository-verified;** no ledger or task-record outcome was located. |
| Critical-state risk: [critical-risk-lab.cpp](../../approaches/heuristic-search/critical-risk/critical-risk-lab.cpp) | Applies lower-tail aggregation only at high-load critical states. | **Unknown — repository-verified;** no durable outcome was located. |
| Engineered-feature evolution: [evolution.cpp](../../approaches/heuristic-search/evolution/evolution.cpp), [nonlinear-evolution.cpp](../../approaches/heuristic-search/evolution/nonlinear-evolution.cpp), [phase-weight-evolution.cpp](../../approaches/heuristic-search/evolution/phase-weight-evolution.cpp) | Evolves linear, small nonlinear, and grouped phase-aware one-step action scorers. | **Rejected — ledger-recorded;** each family failed its whole-game gate despite inexpensive fitting or plausible feature directions. |
| Evolved public policy: [evo-public-policy.cpp](../../approaches/heuristic-search/evolved-public-policy/evo-public-policy.cpp) | Evolves a larger public phase evaluator used inside selective expectimax. | **Rejected — ledger-recorded;** it failed before the protected probe. |
| Exact root quadrature and historical D4: [exact-root-ensemble.cpp](../../approaches/heuristic-search/exact-search/exact-root-ensemble.cpp), [exact-depth4.cpp](../../approaches/heuristic-search/exact-search/exact-depth4.cpp) | Tests independent root quadratures and a fully completed extra ply with the older leaf. | **Rejected — ledger-recorded;** both were slower and worse, showing that depth alone cannot repair a biased leaf. |
| Three-member exact policy ensemble: [policy-ensemble.cpp](../../approaches/heuristic-search/exact-search/policy-ensemble.cpp) | Votes or aggregates independent complete root searches, falling back when they split. | **Completed — task-record only;** a small replicated gain was reported against its comparator, but it was not qualified against the strongest D3/D4 reference. |

## Fair expectimax

| Approach and sources | Purpose | Status and evidence |
| --- | --- | --- |
| Fair policy tuning: [tune.ts](../../approaches/fair-expectimax/fair-policy/tune.ts), [weight-sweep.ts](../../approaches/fair-expectimax/fair-policy/weight-sweep.ts) | Fits the public fair leaf and performs bounded coefficient sweeps. | **Completed — ledger-recorded;** these runs produced the fair reference weights. |
| Phase/fair combination: [main.ts](../../approaches/fair-expectimax/phase-fair-combination/main.ts) | Combines the fair leaf with frozen phase overrides. | **Completed — ledger-recorded;** it is part of the recovered D3 baseline lineage. |
| Fair D3 reference: [fair-only-horizon.cpp](../../approaches/fair-expectimax/reference/fair-only-horizon.cpp) | Full-width depth-3, five-stratum public expectimax with the fair leaf. | **Completed — ledger-recorded;** it passed its mean-improvement screen and confirmation. |
| Fair D4 reference: [fair-only-depth4.cpp](../../approaches/fair-expectimax/reference/fair-only-depth4.cpp) | Holds the fair evaluator fixed and completes all four search plies. | **Completed — ledger-recorded;** this is the strongest retained public baseline, although still only development evidence. |
| Root CVaR: [fair-root-risk.cpp](../../approaches/fair-expectimax/root-risk/fair-root-risk.cpp) | Replaces mean root utility with a fixed mean/lower-tail mixture. | **Rejected — ledger-recorded;** score, survival, and flow all regressed. |
| Full action terms: [full-fair-horizon.cpp](../../approaches/fair-expectimax/full-action-terms/full-fair-horizon.cpp), [transition-reward-horizon.cpp](../../approaches/fair-expectimax/full-action-terms/transition-reward-horizon.cpp) | Restores historical placement terms, then isolates reveal/chain transition rewards. | **Rejected — ledger-recorded;** neither transfer survived its fresh screen. |
| D2 reward landscape: [d2-reward-probe.cpp](../../approaches/fair-expectimax/transition-rewards/d2-reward-probe.cpp) | Training-range-only sweep of clear and reveal transition rewards at depth 2. | **Completed — task-record only diagnostic;** it generated hypotheses but cannot qualify a policy. |
| Phase-energy release: [fair-phase-energy-release.cpp](../../approaches/fair-expectimax/transition-rewards/fair-phase-energy-release.cpp) | Tests phase-dependent stored-energy release terms inside fair D4. | **Runtime-paused — ledger-recorded;** useful diagnostics were retained after the resource cap. |
| Clear reward confirmation: [fair-clear-reward-confirmation.cpp](../../approaches/fair-expectimax/transition-rewards/fair-clear-reward-confirmation.cpp) | Re-tests the fitting winner that pays a fixed reward per numbered clear. | **Rejected — ledger-recorded;** the held-out cohort reversed the fitting gain. |
| Reveal reward: [fair-reveal-reward.cpp](../../approaches/fair-expectimax/transition-rewards/fair-reveal-reward.cpp) | Isolates direct reward for newly exposed covered numbers. | **Rejected — ledger-recorded;** the held-out comparison failed. |
| Fair-leaf CEM: [fair-cem-optimizer.cpp](../../approaches/fair-expectimax/cem/fair-cem-optimizer.cpp), [fair-cem-depth4-interaction.cpp](../../approaches/fair-expectimax/cem/fair-cem-depth4-interaction.cpp) | Optimizes eight fair-leaf/action coefficients on complete games and transfers the frozen vector to D4. | **Rejected — ledger-recorded;** the D3 screen and D4 heldout/tail gates failed. |
| Seven-stratum D4: [fair-depth4-s7.cpp](../../approaches/fair-expectimax/chance-strata/fair-depth4-s7.cpp) | Covers all seven next-disc values at each D4 chance node. | **Rejected — ledger-recorded;** it was score-neutral to worse, reduced flow, and cost about 3.8 times more work. |
| Selective D5: [fair-selective-depth.cpp](../../approaches/fair-expectimax/selective-depth/fair-selective-depth.cpp) | Extends only selected roots beyond D4. | **Rejected — ledger-recorded;** the screen lost score, survival, and flow. |
| Cycle-boundary D5: [fair-cycle-boundary-depth5.cpp](../../approaches/fair-expectimax/selective-depth/fair-cycle-boundary-depth5.cpp) | Searches one extra ply only where D4 cannot see through the next rise. | **Rejected — ledger-recorded;** the pilot was adverse and also missed its runtime gate. |
| Full-width D5/s3: [fair-depth5-s3.cpp](../../approaches/fair-expectimax/selective-depth/fair-depth5-s3.cpp) | Trades chance quality for a completed fifth ply. | **Runtime-paused — ledger-recorded;** the runtime gate stopped the full study and the available pilot was unfavorable. |
| Vertical-ladder probe: [d2-vertical-ladder-probe.cpp](../../approaches/fair-expectimax/vertical-ladder/d2-vertical-ladder-probe.cpp) | Screens a literal 7/6/5 vertical-reservoir energy term cheaply at D2. | **Completed — ledger-recorded diagnostic;** the shallow signal motivated a D4 transfer. |
| Vertical-ladder D4: [fair-vertical-ladder-depth4.cpp](../../approaches/fair-expectimax/vertical-ladder/fair-vertical-ladder-depth4.cpp) | Applies the frozen ladder feature to fair D4. | **Rejected — ledger-recorded;** the shallow benefit did not transfer. |
| Fair-D1 rollout improvement: [fair-d1-rollout-improvement.cpp](../../approaches/fair-expectimax/rollout-improvement/fair-d1-rollout-improvement.cpp) | Uses public continuations to improve fair D1 one root at a time. | **Rejected — ledger-recorded;** it failed the fitting gate. |

## Tree search

| Approach and sources | Purpose | Status and evidence |
| --- | --- | --- |
| TypeScript MCTS lab: [typescript.ts](../../approaches/tree-search/mcts/typescript.ts) | Benchmarks the reusable observable-state MCTS solver. | **Rejected — task-record only;** ordinary MCTS did not establish a whole-game improvement. |
| Observable stochastic UCT: [observable-mcts-lab.cpp](../../approaches/tree-search/observable-mcts/observable-mcts-lab.cpp) | Samples chance at observable-state edges, avoiding determinization and strategy fusion. | **Rejected — ledger-recorded;** it missed the frozen held-out top-action gate by one root. |
| Confidence-gated MCTS: [fair-mcts-confidence.cpp](../../approaches/tree-search/observable-mcts/fair-mcts-confidence.cpp) | Allows MCTS to override fair D3 only with Q and visit-share confidence. | **Rejected — ledger-recorded;** held-out pairwise accuracy and regret worsened. |
| Scaled observable MCTS: [observable-mcts-scaled-audit.cpp](../../approaches/tree-search/observable-mcts/observable-mcts-scaled-audit.cpp) | Re-runs observable MCTS at larger simulation/horizon scale on locked root artifacts. | **Rejected — ledger-recorded;** larger search worsened long-outcome ranking. |
| PUCT: [puct.cpp](../../approaches/tree-search/puct/puct.cpp) | Bounded public PUCT with phase-policy priors and screen/confirmation lanes. | **Unknown — repository-verified;** no durable result was located. |
| NNUE-guided deeper search: [nnue-guided-search.cpp](../../approaches/tree-search/nnue-guided/nnue-guided-search.cpp) | Completes D3 first, then permits fully completed learned-guided deeper iterations. | **Rejected — ledger-recorded;** the safeguarded screen still regressed. |
| Root quadrature: [nnue-root-quadrature.cpp](../../approaches/tree-search/nnue-guided/nnue-root-quadrature.cpp) | Expands a larger first-reveal/next-disc product quadrature. | **Rejected — ledger-recorded;** extra chance coverage did not beat full-width D3. |
| Selective NNUE search: [nnue-selective-search.cpp](../../approaches/tree-search/nnue-guided/nnue-selective-search.cpp) | Re-searches only model-ranked promising actions while retaining the frozen exact fallback. | **Rejected — task-record only;** the learned selective extension failed its screen. |

## Value and policy learning

| Approach and sources | Purpose | Status and evidence |
| --- | --- | --- |
| Direct public policy: [main.ts](../../approaches/value-policy-learning/direct-policy/main.ts) | Evolves a seed-blind policy directly on complete games. | **Rejected — task-record only;** learned-policy roll-ins drifted away from their teacher and did not clear the whole-game gate. |
| General TypeScript value model: [train.ts](../../approaches/value-policy-learning/value-model/train.ts) | Trains sparse board/policy models from shape, oracle, fitted, or contrastive teachers with disjoint seed lanes. | **Rejected — task-record only for the tested configurations;** early learned leaves helped one ply but hurt the stronger rollout policy. |
| Double-DQN and continuation: [train.ts](../../approaches/value-policy-learning/dqn/train.ts), [continuation-benchmark.ts](../../approaches/value-policy-learning/dqn/continuation-benchmark.ts) | Trains a public Double-DQN and evaluates it with bounded six-move continuations. | **Completed — task-record only;** continuation improved the frozen DQN over 64 games, but remained far below the research target. |
| DQN v2: [train-v2.ts](../../approaches/value-policy-learning/dqn/train-v2.ts) | Adds five-step credit, prioritized trajectory replay, and optional privileged demonstrations. | **Rejected — task-record only;** the first fixed five-step/prioritized ablation regressed. |
| Monte Carlo return: [train.ts](../../approaches/value-policy-learning/monte-carlo-return/train.ts), [benchmark.ts](../../approaches/value-policy-learning/monte-carlo-return/benchmark.ts) | Learns action values from complete realized returns and benchmarks a frozen artifact. | **Rejected — task-record only;** the independent audit trailed the existing DQN. |
| Chance-state NNUE: [nnue-value.cpp](../../approaches/value-policy-learning/chance-state-nnue/nnue-value.cpp) | Learns `U(board, rise phase)` without splitting future value by the next disc. | **Rejected — task-record only;** the learner plateaued well below fair search. |
| Policy distillation: [phase-student.cpp](../../approaches/value-policy-learning/phase-distillation/phase-student.cpp) | Clones the exact phase D3/s5 action policy from public board tokens. | **Rejected — task-record only;** held-out top-action agreement and whole-game play were poor. |
| Q distillation: [phase-q-student.cpp](../../approaches/value-policy-learning/phase-distillation/phase-q-student.cpp) | Regresses scalar teacher Q from phase features and common one-ply successor summaries. | **Rejected — task-record only;** training fit did not translate to a viable standalone policy. |
| Conservative fitted policy iteration: [cfpi.cpp](../../approaches/value-policy-learning/conservative-fitted-policy-iteration/cfpi.cpp) | Fits an ensemble distribution over remaining lifetime and permits only confidence-supported deviations from phase safety. | **Rejected — ledger-recorded;** aggressive switching regressed and the conservative setting made no change. |
| Direct Monte Carlo state value: [mc-value-policy.cpp](../../approaches/value-policy-learning/monte-carlo-value/mc-value-policy.cpp) | Labels public states with terminal behavior returns, then compares sibling successors conservatively. | **Rejected — ledger-recorded;** the first held-out stage collapsed. |
| Scaled survival value: [survival-value-scale.cpp](../../approaches/value-policy-learning/monte-carlo-value/survival-value-scale.cpp) | Tests more terminal behavior data with whole-game holdouts and strict AUC/rank gates. | **Rejected — ledger-recorded;** held-out lifetime ranking missed its gate. |
| Structured NNUE: [structured-value-nnue.cpp](../../approaches/value-policy-learning/structured-nnue/structured-value-nnue.cpp) | Uses position-specific token embeddings and phase metrics for lifetime/survival prediction. | **Rejected — ledger-recorded;** held-out 50-move AUC and ranking failed. |
| Counterfactual-successor NNUE: [counterfactual-successor-nnue.cpp](../../approaches/value-policy-learning/structured-nnue/counterfactual-successor-nnue.cpp) | Labels all legal sibling successors with independent continuations. | **Rejected — ledger-recorded;** good global prediction coexisted with poor sibling ranking and worse games. |
| Sibling-advantage ranker: [sibling-advantage-ranker.cpp](../../approaches/value-policy-learning/sibling-advantage/sibling-advantage-ranker.cpp) | Learns relative returns for every legal sibling under aligned continuation tapes. | **Rejected — task-record only;** the learned ordering did not beat exact search on unseen roots. |
| Scaled sibling-advantage study: [scaled-sibling-advantage-lab.cpp](../../approaches/value-policy-learning/sibling-advantage/scaled-sibling-advantage-lab.cpp) | Increases data and capacity while retaining grouped, whole-origin splits. | **Rejected — task-record only;** more data still increased regret relative to exact search. |
| D4 root-Q clone: [d4-q-clone.cpp](../../approaches/value-policy-learning/d4-q-clone/d4-q-clone.cpp) | Compresses normalized within-root D4 action ordering into a small public model. | **Rejected — ledger-recorded;** the label/ranking gate failed. |
| Denoised public value: [denoised-stochastic-value.cpp](../../approaches/value-policy-learning/denoised-value/denoised-stochastic-value.cpp) | Labels each public state with many independent futures before fitting lifetime and survival heads. | **Completed — ledger-recorded;** prediction gates passed and a small confirmation improved means, but confidence remained weak and no protected cohort was opened. |
| Denoised guided veto: [denoised-guided-veto.cpp](../../approaches/value-policy-learning/denoised-value/denoised-guided-veto.cpp) | Lets the denoised model veto a guided ensemble only in favor of an exact fallback. | **Rejected — ledger-recorded;** a positive screen failed to replicate. |
| Phase-5 D4 value veto: [d4-phase5-value-veto.cpp](../../approaches/value-policy-learning/denoised-value/d4-phase5-value-veto.cpp) | Allows a calibrated value model to act only where D4 cannot see through the next rise. | **Runtime-paused — ledger-recorded;** the first pair made no switches and exceeded the runtime projection. |

## N-tuple and reinforcement learning

| Approach and sources | Purpose | Status and evidence |
| --- | --- | --- |
| Native suite and hierarchical n-tuples: [native.cpp](../../approaches/ntuple-rl/native-suite/native.cpp) | Houses the native engine benchmark, n-tuple training, sparse learned-value search, and self-tests. | **Completed — ledger-recorded;** depth-2 sparse search improved the learned greedy baseline, but it was not selected over later fair D4. |
| Hand/learned phase blend: [main.ts](../../approaches/ntuple-rl/phase-blend/main.ts) | Calibrates and combines the hierarchical n-tuple value with the phase heuristic. | **Rejected — ledger-recorded;** the frozen blend trailed learned-value search alone. |
| Bellman n-tuple: [bellman-ntuple.cpp](../../approaches/ntuple-rl/bellman-ntuple/bellman-ntuple.cpp) | Performs off-policy fitted Bellman updates over all legal actions with common chance samples. | **Rejected — task-record only;** the run plateaued below fair search. |
| Temporal-coherence n-tuples: [ntuple-tc.cpp](../../approaches/ntuple-rl/temporal-coherence/ntuple-tc.cpp), [ntuple-phase-conditioned.cpp](../../approaches/ntuple-rl/temporal-coherence/ntuple-phase-conditioned.cpp) | Corrects shared-parameter TD updates and then adds separate rise-phase residuals. | **Rejected — ledger-recorded;** the update bug was fixed, but neither policy gate passed. |
| Optimistic phase n-tuple: [optimistic-phase-ntuple.cpp](../../approaches/ntuple-rl/optimistic-phase/optimistic-phase-ntuple.cpp) | Trains pooled then phase-specific chance-state tuples and searches across two rise boundaries. | **Rejected — ledger-recorded;** after 50 million transitions the representative-outcome search was worse than direct n-tuple play. |
| Rainbow-lite n-tuple Q: [rainbow-ntuple-q.cpp](../../approaches/ntuple-rl/rainbow-q/rainbow-ntuple-q.cpp) | Uses replay, multi-step off-policy targets, and a compact n-tuple Q function under corrected scoring. | **Rejected — ledger-recorded;** it passed random play but failed the fair-D1 gate at one million transitions. |
| Flow-curriculum Rainbow: [flow-curriculum-rainbow.cpp](../../approaches/ntuple-rl/rainbow-q/flow-curriculum-rainbow.cpp) | Continues the frozen Q checkpoint with balanced initial and sustainable-oracle restart states. | **Rejected — task-record only;** the fixed 16-million-transition Stage A failed all absolute floors. |
| Native PPO v2: [ppo-v2.cpp](../../approaches/ntuple-rl/native-ppo/ppo-v2.cpp) | Public actor/critic with behavior cloning and PPO fine-tuning in the native environment. | **Rejected — ledger-recorded;** the corrected PPO policy remained far below fair search. |
| PyTorch PPO: [torch-env.cpp](../../approaches/ntuple-rl/torch-ppo/torch-env.cpp), [train.py](../../approaches/ntuple-rl/torch-ppo/train.py) | Batched native environment plus PyTorch behavior cloning and PPO. | **Rejected — ledger-recorded;** the warm-start gate failed; a separately authorized direct run later aborted at its resource limit. |
| Oracle-manifold discriminator: [oracle-manifold-ppo.cpp](../../approaches/ntuple-rl/manifold-ppo/oracle-manifold-ppo.cpp) | Learns a public classifier for states resembling privileged long-lived trajectories. | **Rejected — ledger-recorded;** the original coverage gate stopped the policy experiment. |
| Manifold root prior: [manifold-root-prior.cpp](../../approaches/ntuple-rl/manifold-ppo/manifold-root-prior.cpp) | Uses the frozen manifold scalar only as a near-tied D3 root prior. | **Rejected — ledger-recorded;** it reduced score, survival, and flow. |
| Manifold GAIL development: [manifold-gail-development.cpp](../../approaches/ntuple-rl/manifold-ppo/manifold-gail-development.cpp) | Shapes a fixed PPO run with the already-matched public oracle-manifold discriminator. | **Rejected — task-record only;** modest training improvement still missed every Stage-A floor. |
| Scaled manifold GAIL: [manifold-gail-scaled.cpp](../../approaches/ntuple-rl/manifold-ppo/manifold-gail-scaled.cpp) | Increases only the frozen manifold shaping coefficients on a disjoint lane. | **Rejected — task-record only;** stronger shaping materially regressed Stage A. |
| Curriculum option PPO: [curriculum-option-ppo.cpp](../../approaches/ntuple-rl/curriculum-option-ppo/curriculum-option-ppo.cpp) | Adds a reflection-equivariant residual to exact fair-D1 logits and trains on initial plus mature restart states. | **Rejected — task-record only;** it modestly improved D1 but remained far below sustainable flow and D4. |
| Primal-dual actor-critic: [primal-dual-actor-critic.cpp](../../approaches/ntuple-rl/primal-dual-actor-critic/primal-dual-actor-critic.cpp) | Constrains five-move occupancy drift, cover drift, and terminal risk while learning a policy residual. | **Rejected — ledger-recorded;** final calibration failed and Stage A stayed closed. |
| Regenerative expert iteration: [regenerative-expert-iteration.cpp](../../approaches/ntuple-rl/regenerative-expert-iteration/regenerative-expert-iteration.cpp) | Reanalyses D4-initialized roll-ins with lifetime, regeneration, and flow heads over eight fixed rounds. | **Rejected — ledger-recorded;** all rounds remained far below the D4 bootstrap and exported no deployable checkpoint. |

## Oracle and curriculum research

Oracle programs are analysis tools or teachers unless explicitly converted to a
public-state policy. Their scores must not be compared directly with deployable
public-information policies.

| Approach and sources | Purpose | Status and evidence |
| --- | --- | --- |
| Perfect-information oracle: [main.ts](../../approaches/oracle-curriculum/perfect-information-oracle/main.ts) | Uses a realized future tape to estimate a mechanical performance ceiling. | **Completed — ledger-recorded diagnostic;** it demonstrates feasibility but is not deployable. |
| Oracle DAgger: [main.ts](../../approaches/oracle-curriculum/oracle-dagger/main.ts) | Iteratively labels public student states with a privileged oracle. | **Rejected — task-record only;** the student drifted and oracle choices were not recoverable from public state. |
| Native oracle distillation: [oracle-distill.cpp](../../approaches/oracle-curriculum/oracle-distillation/oracle-distill.cpp) | Aggregates privileged actions over whole-game splits and gates a public policy hybrid. | **Rejected — ledger-recorded;** held-out labels overfit and the apparent screen gain reversed in confirmation. |
| TypeScript restart generator: [generate.ts](../../approaches/oracle-curriculum/state-curriculum/generate.ts) | Extracts public restart states from long oracle trajectories for curriculum use. | **Support-only — repository-verified.** |
| Native restart curriculum: [oracle-curriculum.cpp](../../approaches/oracle-curriculum/state-curriculum/oracle-curriculum.cpp) | Converts privileged trajectories into canonical public-only restart artifacts with independent future streams. | **Completed — task-record only support;** 4,096 restart states were generated and privilege-isolation checks passed. |
| Oracle topology audit: [oracle-topology-audit.cpp](../../approaches/oracle-curriculum/topology/oracle-topology-audit.cpp) | Matches public and privileged states to identify split-stable observable geometry and flow signals. | **Completed — ledger-recorded diagnostic.** |
| Oracle topology residual: [oracle-topology-residual.cpp](../../approaches/oracle-curriculum/topology/oracle-topology-residual.cpp) | Fits a compact public classifier to matched oracle/fair board topology. | **Rejected — ledger-recorded;** favorable metrics were underpowered and did not open a policy screen. |
| Topology residual extension: [oracle-topology-residual-extension.cpp](../../approaches/oracle-curriculum/topology/oracle-topology-residual-extension.cpp) | Rebuilds the frozen topology labels at larger scale and tests the scalar as a policy residual. | **Rejected — ledger-recorded;** prediction replicated, but policy confirmation regressed. |
| Accessible-energy residual: [accessible-energy-lab.cpp](../../approaches/oracle-curriculum/accessible-energy/accessible-energy-lab.cpp) | Encodes stored high numbers, trigger reachability, cover access, and low-number debt. | **Rejected — ledger-recorded;** local prediction improved, but complete-game search worsened. |
| Accessible-energy root prior: [accessible-energy-root-prior.cpp](../../approaches/oracle-curriculum/accessible-energy/accessible-energy-root-prior.cpp) | Restricts the learned signal to a confidence-admissible root action set. | **Rejected — ledger-recorded;** the confidence set switched too often and reduced flow. |
| Hindsight planner: [hindsight-planner.cpp](../../approaches/oracle-curriculum/hindsight-planner/hindsight-planner.cpp) | Averages clairvoyant beam values over synthetic future tapes. | **Rejected — ledger-recorded;** strategy fusion made the public root ranking worse. |

## D4 and long-outcome research

| Approach and sources | Purpose | Status and evidence |
| --- | --- | --- |
| Scaled D4 distillation: [scaled-d4-distill.cpp](../../approaches/d4-long-outcome/d4-distillation/scaled-d4-distill.cpp) | Compresses normalized D4 root-Q labels and compares the model with an exact D2 primitive. | **Rejected — ledger-recorded for the clone;** exact D2 itself completed a strong ranking diagnostic and remains useful support. |
| H200 sibling NNUE: [d4-h200-sibling-nnue.cpp](../../approaches/d4-long-outcome/h200-sibling-nnue/d4-h200-sibling-nnue.cpp) | Fits an offline residual over the locked 477-root, 200-move sibling corpus with whole-origin folds. | **Rejected — task-record only;** it regressed D4 top-1, pairwise ranking, and regret in every origin fold. |
| D2 long-outcome ranker: [d2-long-outcome-ranker.cpp](../../approaches/d4-long-outcome/long-outcome/d2-long-outcome-ranker.cpp) | Learns 25-move public-D2 sibling returns from grouped root panels. | **Rejected — ledger-recorded;** held-out ranking and regret did not beat exact D2/D4 comparators. |
| Long-outcome feature audit: [d2-long-outcome-feature-audit.cpp](../../approaches/d4-long-outcome/long-outcome/d2-long-outcome-feature-audit.cpp) | Measures survival, flow, downside, and ladder features against the same long-outcome panels. | **Rejected — ledger-recorded;** the feature conjunction did not pass. |
| Curriculum long-outcome NNUE: [curriculum-long-outcome-nnue.cpp](../../approaches/d4-long-outcome/long-outcome/curriculum-long-outcome-nnue.cpp) | Artifact-only H100 multi-head model over frozen public curriculum states. | **Rejected — task-record only;** survival prediction was useful, but action ordering regressed versus exact search. |
| Scaled long-outcome NNUE: [scaled-long-outcome-nnue.cpp](../../approaches/d4-long-outcome/long-outcome/scaled-long-outcome-nnue.cpp) | Tests whether more labels and capacity fix the earlier long-outcome sample bottleneck. | **Rejected — ledger-recorded;** no whole-origin fold fully non-regressed. |
| D4 veto classifier: [d4-long-outcome-veto-classifier.cpp](../../approaches/d4-long-outcome/long-outcome/d4-long-outcome-veto-classifier.cpp) | Uses long-outcome heads only to veto fragile D4 actions. | **Rejected — ledger-recorded;** override precision and downside gates failed. |
| Relaxed chain-potential audit: [relaxed-chain-potential-audit.cpp](../../approaches/d4-long-outcome/long-outcome/relaxed-chain-potential-audit.cpp) | Broadens the literal ladder feature into relaxed reachable chain potential. | **Rejected — ledger-recorded;** every material transfer gate failed. |
| Exact D4 + 25-move D2 rollout veto: [d4-d2-rollout-veto.cpp](../../approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto.cpp) | Lets a costly common-tape H25/D2 comparison veto fair D4. | **Runtime-paused — ledger-recorded;** one promising pilot passed quality gates but missed the runtime limit by a wide margin. |
| Exact compression variants: [d4-d2-rollout-veto-exact-compressed.cpp](../../approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto-exact-compressed.cpp), [d4-d2-rollout-veto-cache-free.cpp](../../approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto-cache-free.cpp) | Preserve decisions while deduplicating suffixes or removing low-value D2 caches. | **Runtime-paused — ledger-recorded;** parity held, but wall-time savings were immaterial. |
| Rollout quality extension: [d4-d2-rollout-veto-quality-extension.cpp](../../approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto-quality-extension.cpp) | Runs more complete games to test whether the original 404,000-point pilot replicates. | **Rejected — ledger-recorded;** the frozen replication and robustness gates failed. |
| Teacher compression: [d2-rollout-teacher-compression.cpp](../../approaches/d4-long-outcome/rollout-veto/d2-rollout-teacher-compression.cpp) | Tests cheaper continuation teachers against the exact rollout's beneficial interventions. | **Rejected — ledger-recorded;** none recovered enough good vetoes. |

## Constructive and reservoir policies

| Approach and sources | Purpose | Status and evidence |
| --- | --- | --- |
| Vertical reservoir: [vertical-reservoir-policy.cpp](../../approaches/constructive-reservoir/vertical-reservoir/vertical-reservoir-policy.cpp) | Fits nonlinear vertical release, escape, cover-access, and low-cap features by complete-game CEM. | **Rejected — ledger-recorded;** the 128-game tournament trailed fair D1. |
| Viability controller: [viability-reservoir-controller.cpp](../../approaches/constructive-reservoir/viability-controller/viability-reservoir-controller.cpp) | Selects charge, dig, release, repair, and emergency options from exact public certificates. | **Rejected — ledger-recorded;** Stage A reduced score, survival, and flow. |
| Constructive spectrum: [constructive-spectrum.cpp](../../approaches/constructive-reservoir/constructive-spectrum/constructive-spectrum.cpp) | Plans across a rise cycle toward reachable high-number reservoirs and overlapping trigger keys. | **Completed — ledger-recorded;** it materially beat fair D1 and is a useful fast continuation, but did not beat D4. |
| Constructive D4 shield: [constructive-spectrum-depth4.cpp](../../approaches/constructive-reservoir/constructive-spectrum/constructive-spectrum-depth4.cpp) | Applies the frozen constructive rollout only to D4's top near-tied actions. | **Rejected — ledger-recorded in the grouped constructive result;** frequent overrides reduced score, survival, and flow. |
| Constructive horizon scale: [constructive-horizon-scale.cpp](../../approaches/constructive-reservoir/constructive-spectrum/constructive-horizon-scale.cpp) | Compares frozen H7, H12, H17, and H27 continuation horizons. | **Rejected — ledger-recorded;** H12 improved means but missed the robustness gate, and longer horizons were not monotonic. |
| H12 risk gate: [constructive-h12-risk-gate.cpp](../../approaches/constructive-reservoir/constructive-spectrum/constructive-h12-risk-gate.cpp) | Accepts H12 over H7 only when two actions satisfy a common 21-scenario Pareto panel. | **Rejected — ledger-recorded in the grouped constructive result;** it improved means but missed the paired-win gate. |
| Panel-value NNUE: [panel-value-nnue.cpp](../../approaches/constructive-reservoir/panel-value/panel-value-nnue.cpp) | Learns public state value from every legal sibling's common H100 continuations. | **Rejected — task-record only;** the untouched holdout had weak top-action accuracy and worse regret, so gameplay stayed sealed. |
| Direct sibling ranker: [direct-sibling-ranker.cpp](../../approaches/constructive-reservoir/panel-value/direct-sibling-ranker.cpp) | Trains directly on complete within-root action panels rather than a successor state-value reservoir. | **Rejected — task-record only;** it slightly beat D1 but remained materially behind D4. |
| Structural terminal veto: [d4-structural-terminal-veto.cpp](../../approaches/constructive-reservoir/structural-terminal-veto/d4-structural-terminal-veto.cpp) | Compares D4's top two actions on two independent 127-stream constructive terminal panels. | **Rejected — task-record only;** the aggregate fitting result trailed plain D4 and could not reach its gate. |
| Rise-boundary option QD: [rise-option-qd.cpp](../../approaches/constructive-reservoir/rise-option-qd/rise-option-qd.cpp) | Seed-free prototype of persistent rise-cycle options and a deterministic MAP-Elites archive. | **Support-only — repository-verified;** the source intentionally has no gameplay or production-training lane. |
| Tail-survival CEM: [tail-survival-cem.cpp](../../approaches/constructive-reservoir/tail-survival-cem/tail-survival-cem.cpp) | Optimizes complete-game 75/100/150/225/300-move survival milestones around a fixed selective policy. | **Rejected — ledger-recorded;** 50,432 candidate-games produced only a small gain and missed every admission floor. |

## Terminal policy iteration and deployment panels

| Approach and sources | Purpose | Status and evidence |
| --- | --- | --- |
| Terminal-rollout feasibility: [terminal-rollout.cpp](../../approaches/terminal-policy-iteration/terminal-rollout/terminal-rollout.cpp) | Screens faithful full-terminal comparisons around the shared phase behavior. | **Runtime-paused — task-record only;** a faithful design projected roughly hundreds of seconds per move. |
| Public survival rollout: [public-survival-rollout.cpp](../../approaches/terminal-policy-iteration/public-survival-rollout/public-survival-rollout.cpp) | Evaluates each root action with 31 aligned H100 public fair-D1 continuations. | **Rejected — task-record only;** the longer rollout was worse than fair D1 on fitting games. |
| Terminal policy iteration: [terminal-policy-iteration.cpp](../../approaches/terminal-policy-iteration/terminal-policy-iteration/terminal-policy-iteration.cpp) | Uses 255 aligned H200 continuations and strict confidence bounds to override fair D1. | **Rejected — task-record only;** the gain was real but missed the frozen score/survival and whole-origin gates. |
| Public rollout policy iteration: [public-rollout-policy-iteration.cpp](../../approaches/terminal-policy-iteration/public-rollout-policy-iteration/public-rollout-policy-iteration.cpp) | Runs lower-cost complete-sibling public rollout improvement with conservative fallbacks. | **Rejected — ledger-recorded;** it failed at the fitting gate. |
| H200 D1/D4 signal audit: [terminal-panel-d4-signal-audit.cpp](../../approaches/terminal-policy-iteration/deployment-panel/terminal-panel-d4-signal-audit.cpp) | Recomputes exact D1 and D4 rankings on the locked 477-root H200 corpus. | **Completed — task-record only diagnostic;** D4 materially beat D1 on top-action, pairwise ranking, regret, and every origin. |
| Full-panel conservative preflight: [full-panel-cpi-preflight.cpp](../../approaches/terminal-policy-iteration/deployment-panel/full-panel-cpi-preflight.cpp) | Tests nested 7/21/35/63-scenario sibling labels with whole-origin cross-validation before any gameplay. | **Rejected — ledger-recorded;** stability, precision, recall, ranking, regret, and origin gates all failed. |
| Martingale-dual ranker: [martingale-dual-b0.cpp](../../approaches/terminal-policy-iteration/deployment-panel/martingale-dual-b0.cpp) | Charges a bounded hindsight beam for future-information advantage on the locked H200 panel. | **Rejected — ledger-recorded;** it ranked siblings materially worse than fair D4. |
| Public regenerative B0: [public-regenerative-policy-iteration-b0.cpp](../../approaches/terminal-policy-iteration/public-regenerative-b0/public-regenerative-policy-iteration-b0.cpp) | Evaluates every sibling through staged H25/H50/H75 panels with exact D4 fallback. | **Rejected — ledger-recorded;** it was effectively tied in mean proxy return but lacked coverage, precision, and stability. |

## Afterstate learning

| Approach and sources | Purpose | Status and evidence |
| --- | --- | --- |
| Distributional afterstate ranker: [common.hpp](../../approaches/afterstate-learning/distributional-afterstate/common.hpp), [generate-corpus.cpp](../../approaches/afterstate-learning/distributional-afterstate/generate-corpus.cpp), [label-d4.cpp](../../approaches/afterstate-learning/distributional-afterstate/label-d4.cpp), [self-test.cpp](../../approaches/afterstate-learning/distributional-afterstate/self-test.cpp), [train.py](../../approaches/afterstate-learning/distributional-afterstate/train.py) | Trains one action-free distributional evaluator of the public afterstate on a successor-closed corpus (every legal sibling, K aligned chance scenarios, H40 phase-greedy-D1 continuation), then gates held-out whole-origin sibling ranking against exact fair D4. | **Rejected — machine-readable records;** three preregistered pilots on lease [SL-20260820T083000Z-5da70000](../../research/seeds/leases/SL-20260820T083000Z-5da70000.json): K=8 and K=64 were inconclusive on label stability (split-half Spearman 0.246/0.446 vs the 0.5 floor); K=256 passed stability (0.818 decisive) and produced a valid negative — the calibrated model (0.86 quantile coverage) beat its D1 teacher (top-1 0.424 vs 0.319) but trailed D4 (top-1 0.424 vs 0.499, regret 0.241 vs 0.178) in both fresh held-out half-folds. See [theory](../../research/theories/TH-20260820-distributional-afterstate-ranker-7aba7fb3.json) and results [K=8](../../research/results/RS-20260820T094500Z-5c1e9a04.json), [K=64](../../research/results/RS-20260820T114500Z-2b7c9e31.json), [K=256](../../research/results/RS-20260820T142500Z-8f4a2d17.json). |

## What remains genuinely open

The source inventory shows that the research has already tested more depth,
more chance samples, static potential bonuses, learned global state values,
action cloning, n-tuple TD/Q learning, PPO-style policies, public MCTS,
determinized hindsight, long rollouts, conservative vetoes, and explicit
reservoir controllers. The recurring failure is not a lack of code paths. It
is reliable **within-root action ranking over long stochastic horizons**.

The most defensible unclosed directions are therefore:

1. A successor-closed, action-free public afterstate evaluator in which every
   legal sibling receives supported labels and no learned maximization occurs
   outside that support.
2. A stronger full-sibling teacher that preserves D4 as the fallback while
   learning only rare, independently confirmed overrides.
3. A computationally cheaper version of the exact H25/D2 veto that preserves
   the original intervention decisions; the tested exact cache changes were
   not enough, so this requires a different representation rather than more
   bookkeeping.
4. A persistent option policy only after a one-step evaluator demonstrates
   robust score, survival, and clear/reveal signal. The current seed-free
   option prototype is infrastructure, not evidence that commitment helps.
5. Porting the corrected fair-D4 native reference into a TypeScript benchmark
   entry point so both implementations expose the same research policy. This is
   an engineering comparison step, not a new strategy claim.

Any new candidate should keep whole-game origin splits, common random futures
for sibling comparisons, explicit censoring, bounded work and memory, exact D4
fallback, and the still-unopened protected/final cohorts.
