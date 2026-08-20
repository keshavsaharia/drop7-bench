# Standard benchmark contract

This contract makes policy quality and systems performance comparable across
strategy families and machines. It extends the scientific rules in
[`methodology.md`](methodology.md); the frozen million-point protocol remains
authoritative for protected and final validation.

## What a benchmark must hold fixed

A comparison names and freezes:

- corrected five-move Hardcore rules and a 2,000-move cap unless a lower
  diagnostic cap is part of the tier;
- candidate and unchanged fair-D4 comparator manifests;
- public-information boundary and legal fallback behavior;
- exact ordered cohort and its data role;
- per-decision depth, work, action width, chance samples, random-domain
  derivation, cache semantics, and model bytes;
- compiler/interpreter, flags, command, thread/process counts, and environment;
- wall, CPU, host-memory, GPU-memory, error, and futility stops;
- primary metric, uncertainty method, and pass/fail rule; and
- output paths, schema versions, and expected artifact hashes.

The current comparator definition and transitive source hashes are pinned in
[`baselines-v1.json`](../research/benchmarks/baselines-v1.json). Its score
figures remain ledger-recorded; the manifest does not claim those games were
rerun or assign a new cohort.

The candidate and comparator play the same complete-game seeds. Sibling
estimators use aligned, event-keyed random scenarios when possible. The policy
never receives the game seed or a key from which future events can be inferred.

## Benchmark tiers

| Tier | Games | Data role | Purpose and allowed claim |
| --- | ---: | --- | --- |
| `CHECK` | 0 | No gameplay data | Mechanics, parity, legality, determinism, reflection, public-state, bounds, and resume checks |
| `PILOT` | 1–8 | Previously evaluated or explicitly leased development | Find bugs and project runtime/memory; no strength claim |
| `SCREEN` | 32 paired | Public development | Cheap rejection and diagnostic comparison |
| `STANDARD` | 64 paired | Fixed reusable development | Shared leaderboard and ablations; tunable, never fresh confirmation |
| `QUALIFY` | 256 | Fresh development | Existing mean >1.05M and bootstrap lower bound >1M freeze gate |
| `PROTECTED` | 256 | Next unread aligned protected block | Immutable protected validation after qualification |
| `FINAL` | 256 | One-shot final cohort | Final confirmation of the unchanged candidate |

No active `STANDARD` seed manifest is assigned merely by this document. Before
one is created, the seed allocator must conservatively import every range in the
historical ledger and frozen protocols. An agent may not choose a convenient
unlisted range on its own.

`PROTECTED` and `FINAL` follow
[`million-point-validation.json`](../artifacts/protocols/million-point-validation.json)
exactly. The archived v1 protocol has obsolete relocated-source hashes, so a
current candidate also needs a new source-locked protocol as described in
[`provenance.md`](provenance.md).

## Required correctness gate

Before any gameplay tier, record results for all relevant checks:

- TypeScript rules and policy tests;
- native self-tests and exact native/TypeScript trajectories;
- 17,000-point scoring and five-move rise schedule;
- legal action and terminal handling;
- deterministic repeat and worker-count independence;
- horizontal-reflection behavior;
- metadata blindness and public-state-only inputs;
- chance/RNG domain separation and sibling alignment;
- full-width root evaluation when the protocol promises it;
- fixed work, cache, memory, and timeout behavior; and
- checkpoint/resume equivalence for resumable runs.

An optimization that changes random events, completed depth, logical work,
floating-point action ranking, or the selected column is a new algorithmic
candidate unless a tolerance rule was preregistered. It is not presented as a
pure speedup.

## Standard per-game record

Store one JSON object per candidate game and comparator game in deterministic
cohort order. Completion order may differ. Required fields are represented by
[`game-result-v1.schema.json`](../research/schemas/game-result-v1.schema.json)
and include:

- experiment, run, policy, cohort, and seed-lease IDs;
- seed as a hexadecimal string, or protected ordinal where exposure is barred;
- final score, moves, and censor flag;
- numbered clears, covered reveals, mean and maximum chain depth;
- illegal/incomplete decisions and runner failures;
- logical work, nodes/transitions, and model inferences;
- wall time and available per-decision latency samples; and
- a deterministic trajectory or result checksum.

Use explicit unit suffixes such as `wallSeconds`, `peakHostBytes`, and
`energyJoules`. JSON numbers must be finite; never serialize `NaN` or infinity.
Record the quantile definition and bootstrap RNG domain.

Large raw rows are staged under `runs/<run-id>/`. Promote compact evidence to
`artifacts/results/<experiment-id>/<run-id>/` only with a content manifest.
Hash files other than the manifest first, then hash the manifest separately to
avoid self-hash ambiguity.

## Standard cohort summary

Report at least:

### Policy quality

- score mean, median, standard deviation, Q25, minimum, and maximum;
- moves mean and Q25;
- censor count and move cap;
- numbered clears and covered reveals per move;
- mean and maximum chain depth;
- paired candidate-minus-D4 score and move deltas;
- wins, ties, losses, and non-regression by preregistered half/fold;
- whole-game confidence bounds; and
- illegal moves, incomplete decisions, and runner failures.

### Work and latency

- logical work, transitions/nodes, and model inferences per move;
- p50, p95, and p99 decision time;
- total wall and CPU seconds;
- games, moves, and logical work per second; and
- compile and initialization time separated from steady-state execution.

### Resources

- whole-job-tree peak resident memory and cgroup limit;
- GPU/shared-memory peak, utilization, power, and temperature when available;
- CPU affinity, physical/logical worker counts, NUMA placement, and governor;
- abnormal exits, out-of-memory events, throttling, and page-fault counters; and
- immutable machine-profile reference.

Unavailable measurements are explicitly `null` with a reason. On a unified-
memory APU, GPU allocation and host RAM are overlapping views and are never
summed as separate physical capacity.

## Statistics and heavy tails

The independent unit is a whole game. Move/root rows can diagnose a model but
cannot supply a complete-game confidence interval. Use paired games and common
random numbers to reduce variance without weakening the information boundary.

For qualification, use the exact bootstrap and Student-t rules in the frozen
protocol. For earlier tiers, publish the estimate, resampling method/seed,
interval direction, and number of whole-game samples. Do not promote an
aggregate win if a preregistered split, origin group, or lower-tail gate fails.

A game stopped at the move cap is censored and keeps score already earned. A
process killed by timeout, crash, invalid output, or resource guard is a partial
or invalid run—not a censored game—and remains visible in failure counters.

## Offline sibling-ranking standard

Learned policies must pass an action-ranking gate before complete gameplay.
Split datasets by whole origin game, never by state row. Evaluate every legal
sibling under aligned chance scenarios and report:

- action completeness and missing-label rate;
- top-1 and top-2 accuracy against the declared long-outcome target;
- within-root pairwise accuracy;
- normalized regret and calibration;
- action stability across independent scenario halves;
- results by origin, rise phase, height, and legal-action count; and
- leakage checks for seeds, futures, score, metadata, and duplicate origins.

Played-action value error is not a substitute for sibling ranking. Reusable
historical panels are diagnostic development data; model selection requires a
new whole-origin manifest fixed before labels are inspected.

## Strength and speed are two different views

Publish both when relevant:

1. **Fixed-work strength:** exact algorithmic configuration, independent of
   machine speed. This is the primary policy comparison.
2. **Resource-performance frontier:** score or ranking quality versus latency,
   CPU, host/shared memory, GPU memory, and energy on one machine profile.

Do not compare wall time from two machine fingerprints as policy strength. For
a CPU/GPU port, first prove semantics/action parity at fixed work, then compare
end-to-end speed. A fixed-time search is a distinct candidate because different
hardware can complete different depths.

The machine-readable resource views are in
[`profiles-v1.json`](../research/benchmarks/profiles-v1.json), including a
`machine-max-throughput` profile that requires a scaling preflight, explicit
headroom, exclusive resources, and three repeated measurements.

## Using the operating system well

The agent should measure rather than assume a maximum configuration:

1. record cgroup/container limits, affinity, physical cores, SMT, NUMA, memory,
   swap, GPU visibility, and thermal/power state;
2. run a seed-free scaling preflight over thread/process counts;
3. separate game-level parallelism from within-decision search parallelism;
4. set OpenMP, BLAS, PyTorch, and simulator threads to avoid multiplication;
5. sweep representative cache sizes and GPU batches within a declared memory
   safety margin;
6. choose the best stable throughput point, not the largest allocation; and
7. repeat the frozen performance run at least three times, reporting variation.

Timing-grade runs require exclusive resources or explicit cgroup/affinity
isolation. GPU training and independent game generation may run concurrently
only when the protocol records the contention and the result is not used as a
clean performance baseline.

## Current implementation boundary

The existing fair-D4 executable hardcodes historical cohorts and parallelism,
and many older approaches use shared temporary paths. Until a new standard
adapter and seed registry exist, agents must not pretend those programs already
implement this contract. They may use `CHECK` operations and previously
evaluated diagnostics, or port the method into a new versioned experiment with
parameterized, namespaced output and an explicit data lease.

Use the record templates under [`research/templates`](../research/templates)
and validate them with `make research-validate`.
