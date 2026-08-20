# Research roadmap toward a million-point policy

This roadmap turns the opportunities in [`strategies.md`](../strategies.md)
into an ordered, falsifiable program. It is not a claim that the proposed model
will work. It prioritizes the failure most often seen in the existing research:
models that predict visited states but cannot reliably rank all legal sibling
actions over several row-rise cycles.

## Research question

Can a policy using only public state rank every legal action by its long-horizon
stochastic outcome, across disjoint whole-game origins, well enough to improve
complete games over unchanged fair D4?

Fair D4 is the strongest retained reference, with a ledger-recorded broader
mean near 308,296, but it is far below the target. Privileged planners and long
rollouts show that valuable multi-cycle structure exists; previous students,
partial sibling panels, and expensive search variants did not transfer or
estimate it reliably. See [`status.md`](status.md) for the evidence boundary.

## Safe bootstrap work package

In a checkout with no target Linux machine profile and no authorized retained
dataset manifest, the first autonomous task is a `CHECK`-tier AFBR-40 closure
preflight—not gameplay or model training. Register a no-gameplay diagnostic that:

- freezes the public state/afterstate key and required sibling/label/provenance
  fields;
- inventories repository-local filenames and manifests without reading seed
  contents;
- advances only if the coordinator names authorized, previously evaluated
  development artifacts; and
- otherwise records that closure is unestablished.

A suitable initial bound is 15 minutes, one CPU, 2 GiB of host memory, and an
immediate stop on seed-content access or ambiguous provenance. This preflight
can falsify data availability, but it cannot establish policy strength or claim
that AFBR-40 has been implemented.

## Priority map

| Priority | Work package | Why now | First falsification gate | Hardware fit |
| ---: | --- | --- | --- | --- |
| 1 | Reproduce/profile corrected fair D4 | Establish trustworthy speed, memory, and decision baseline | Exact actions/traces; three stable timing repeats | CPU, cache, many games |
| 2 | Semantics-preserving D4/D5 acceleration | More reliable lookahead/data per day | ≥2× end-to-end target chosen in protocol with exact parity | CPU, 128 GB table sweeps |
| 3 | AFBR-40 closure audit | Prevent another played-action/sibling coverage failure | Quantified successor completeness without new gameplay | CPU/RAM, read-only data audit |
| 4 | Action-complete multi-cycle corpus | Give every legal action comparable targets | 10k-root pilot has complete labels and stable scenario halves | CPU simulation, large RAM, parallel I/O |
| 5 | Distributional afterstate ranker | Learn long survival/score potential, not only behavior imitation | Beats fixed D4 ranking metrics on disjoint origins | ROCm training, batched GPU |
| 6 | Conservative neural-guided D4 | Limit harm while testing useful overrides | Calibrated top-two overrides pass offline and gameplay gates | CPU search + GPU leaf batches |
| 7 | Human-style rise-cycle options | Represent build, protect, crack, release, stabilize | Ablations improve origin-level ranking and complete games | GPU training, CPU option search |
| 8 | Controlled D5/tree-search factorial | Separate depth, chance variance, leaf bias, and cache | Fixed-work and fixed-time comparisons both reported | CPU/RAM plus GPU leaves |
| 9 | All-sibling policy iteration | Improve on states the new policy actually visits | Every round passes unchanged origin folds | Parallel CPU generation + GPU training |
| 10 | Formal qualification | Establish the actual claim | Frozen 256/256/256 protocol gates | Exclusive machine profile |

## 1. Re-establish the reference on the target machine

Before changing strategy, create a current source/binary/machine manifest and
measure fair D4 under corrected scoring. Preserve fixed algorithmic work and
selected actions. Benchmark raw transitions, complete games, D1/D2/D4 work,
thread scaling, cache scaling, RSS, and thermal stability using permitted
development diagnostics.

This step is engineering evidence. It does not re-open old confirmation data or
upgrade ledger-recorded score means.

## 2. Accelerate exact search without changing it

Test incremental board/features, compact canonical keys, sharded transposition
tables, parallel complete games, cache-size sweeps, and profile-guided changes.
A candidate speedup must match transition traces, chance identities, root values
within the frozen numeric contract, work counts, and chosen actions. Otherwise
it becomes a new algorithmic experiment.

The 128 GB machine makes large tables possible, but table size is an independent
factor. Measure hit rate and end-to-end speed rather than treating memory use as
progress.

## 3. Resolve AFBR-40 before implementation

AFBR-40 is only a proposed action-free public afterstate model with an intended
multi-cycle target. There is no implementation, protocol, checkpoint, or result.
Its first task is a read-only closure audit:

1. define the exact public state and afterstate keys;
2. enumerate every legal action at each candidate root;
3. enumerate required successors and long-outcome labels;
4. determine what existing retained artifacts can reconstruct;
5. report completeness by origin, phase, height, and legal-action count; and
6. list missing data without filling it from new seeds.

If closure is incomplete, close the audit as a valid negative result and design
a new corpus. Do not call a sparse or played-action panel successor-closed.

## 4. Build action-complete multi-cycle data

Start with about 10,000 public roots, not a full production corpus. For each root:

- evaluate every legal sibling;
- use common event-keyed random scenarios across siblings;
- attach H40, H100, and H200 outcome distributions where budgets permit;
- record score, survival by rise, terminal hazard, clear/reveal flow, chain
  statistics, and logical work;
- store natural-game roots separately from curriculum/restart roots; and
- split training, calibration, and held-out data by whole origin game.

Measure label stability between independent scenario halves. Scale toward
100,000+ roots only if completeness, stability, throughput, and held-out
ranking justify the storage and compute.

## 5. Train a distributional afterstate ranker

Use one shared evaluator on each legal afterstate so action identity cannot act
as a shortcut. Start simpler than a transformer:

- categorical 7×7 public board encoding;
- next visible disc, rise phase, and legal mask;
- reflection-equivariant residual CNN or compact axial model;
- return quantiles or distribution bins;
- survival hazard by future rise;
- clear/reveal flow and terminal-probability auxiliary heads; and
- within-root pairwise/listwise loss in addition to scalar prediction.

Train on ROCm in large batches, but validate inference at every legal sibling.
The first offline gate should compare top-1, top-2, pairwise accuracy, normalized
regret, calibration, origin non-regression, and scenario-half action stability
against fair D4. Freeze exact thresholds in the experiment record. The existing
477-root panel is reusable diagnosis, not fresh model-selection evidence.

## 6. Add the model above fair D4 conservatively

First compare only fair D4's top two near-tied actions. Override when a calibrated
lower-confidence advantage exceeds a frozen threshold. Record coverage as well
as accuracy; a policy that almost never acts has not solved the problem.

After a successful offline gate:

1. test the confidence-gated override on paired development games;
2. batch leaves across roots/games for GPU utilization;
3. test full-width D4 with the multi-cycle leaf; and
4. only then consider PUCT or progressive widening.

Do not begin with learned action pruning. Every legal root action remains
eligible until the model has independently demonstrated safe sibling ranking.

## 7. Turn human strategy into testable structure

Humans describe long-term play in patterns: reservoirs, ladders, keeping a high
covered disc reachable, protecting overlapping triggers, and releasing a chain
at the right rise phase. Encode these as reflection-safe public features or
five-move options:

- build a reachable reservoir;
- expose or crack covered discs;
- preserve trigger overlap;
- release stored chain energy; and
- stabilize before a row rise.

Use auxiliary labels for trigger reachability, occupancy debt, cover access,
terminal hazard, and build/stabilize/release phase. Keep a feature or option only
when a preregistered ablation improves disjoint-origin action ranking. Saliency
or a visually convincing board is not evidence.

Compare an eight-cycle option search against a primitive H40 search at equal
logical work. This directly tests whether human-like temporal abstraction makes
long-horizon planning easier.

## 8. Revisit depth and stochastic tree search as a factorial

After acceleration and a validated leaf exist, vary one declared factor at a
time:

- D4 versus D5;
- chance strata 3, 5, and 7;
- reference leaf versus validated learned leaf;
- transposition-table size; and
- fixed logical work versus separately reported fixed wall time.

This distinguishes depth value from chance noise, leaf bias, and cache effects.
Past work shows that deeper search is not monotonically stronger, so D5 advances
only through the same ranking and whole-game gates.

## 9. Iterate with every sibling, not only the played action

If the neural-guided policy passes:

1. play on new training-only origins;
2. evaluate every legal sibling at encountered roots;
3. add mature public restart states with independent future randomness;
4. freeze a new data manifest;
5. retrain for a preregistered number of rounds; and
6. keep origin-level validation folds unchanged across rounds.

This is action-complete dataset aggregation. It is intended to prevent the
distribution shift that hurt behavior cloning and played-state value learning.

## 10. Qualification sequence

Use [`benchmarks.md`](../benchmarks.md): `CHECK`, bounded `PILOT`, paired
`SCREEN`, reusable `STANDARD`, and fresh 256-game `QUALIFY`. Only a qualifying
candidate is frozen with current transitive source, binary, model, corpus,
compiler, command, and work hashes.

Then open the next permitted protected block and the one-shot final cohort only
as the versioned protocol allows. Report the complete distribution and failures,
not only the mean. The same unchanged policy must pass every gate.

## Parallel tracks that do not consume strategy data

While the main candidate is training or running, agents can independently:

- audit data closure and manifests;
- add seed-free policy-boundary and reflection tests;
- improve result schemas, checksums, and resumption verification;
- profile compiler flags and simulator hot paths on synthetic fixtures;
- reproduce external algorithmic ideas in toy/no-seed domains; and
- review experiment arithmetic and claim scope.

These tracks increase confidence without competing for the same cohort. Their
owners still write separate contribution records.

## Decision rule

Prefer the next experiment that most reduces uncertainty about long-horizon
sibling ranking per expected CPU/GPU hour. A larger model, deeper search, wider
tree, or bigger table is justified only after its cheaper predecessor shows the
specific signal it is meant to scale.
