# Pipeline contracts

The loop is five programs and one configuration file. All paths below are
proposals for the approach directory that will hold the implementation
(`approaches/<family>/self-play-at-scale/`); nothing here exists yet.

## Configuration (one file, every scale)

```yaml
run_id: RUN-<crypto-random>             # never hand-chosen
seed_lease: SEEDLEASE-<id>              # registered before launch
roles: { train: [..], dev: [..], eval: [..] }   # disjoint ranges
simulator: { impl: batch-cpu, gate_report: runs/<id>/gates.json }
actors:
  count: 32                              # processes or pods
  games_per_actor: 256                   # concurrent boards per process
  search: { depth: 3, strata: 5, leaf: fair | evaluator:<hash> }
  label: every_sibling                   # never played_only
  behaviour: { best: 0.9, explore: 0.1 } # how often a non-best sibling is played
  chance_alignment: shared               # same scenarios across siblings
learner:
  devices: 1                             # GPUs; DDP when > 1
  model: { planes: 12, hidden: 256 }     # small; must be cheap at leaves
  batch: 4096
  shards: runs/<id>/shards/train/
evaluator:
  cohort: { lease: SEEDLEASE-<id>, role: dev, games: 64, paired: true }
  comparator: fair-d4                    # frozen reference, never modified
  gate: { metric: mean_score, bound: bootstrap_95_lower_gt_0, split_rule: ... }
budget: { wall_hours: .., cpu_hours: .., gpu_hours: .., stop_on: [..] }
```

The same file with `actors.count: 2`, `learner.devices: 1` is the workstation
pilot. Cluster launchers only translate `actors.count` and `learner.devices`
into jobs; they never change search, data, or gate fields.

## Record schema (one per visited position)

```
board: 49 cells (0 empty, 1-7 numbered, 8 solid, 9 cracked)
next_disc: 1-7
moves_until_rise: 1-5
sibling_values: 7 floats (NaN for illegal columns)
sibling_work: 7 ints           # logical work spent per sibling
played: column                 # which sibling the actor actually played
scenario_key: hash             # the shared chance scenarios used
iteration, leaf_hash, lease_id, role, seed, move_index
```

No score, level, or absolute move number is ever an *input* to the evaluator;
they are kept in the record for diagnostics only.

## Shards

Append-only files (Parquet or NPZ), one writer per actor, rotated by size, each
with a manifest `{ lease_id, role, seeds_consumed: [from, to], records, sha256 }`.
A shard whose seeds overlap another role's range is rejected at the manifest
check, not filtered later (see the lease incident in
`docs/exploratory/lease-map.md`).

## Learner

Standard data-parallel training; deterministic seeds; checkpoints carry the
config hash and the shard manifests they consumed. The output of an iteration
is a frozen evaluator file plus its hash. Mixed precision, fused kernels, and
reduction order are learner-internal; they do not touch simulation.

## Gate

The frozen evaluator is placed at the leaves of the existing fair search and
played through the public interface against unmodified fair D4 on the paired
development cohort. Report the standard cohort summary from
`docs/benchmarks.md`. Promotion to "current leaf" requires the preregistered
bound; otherwise the iteration is recorded as `valid + fail` and the loop
continues from the previous leaf.

## Checkpoint and resume

Actors are stateless beyond their seed cursor; the learner resumes from its last
checkpoint; the orchestrator records `(iteration, leaf_hash, shard manifests)`
after every gate. A resumed run must reproduce the same shard manifests for the
same seeds — that is the resume-equivalence check the benchmark contract asks
for.
