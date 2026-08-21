---
name: drop7-scale-out
description: Plan, pilot, and run the search-guided self-play loop (every-sibling labels → public evaluator → search leaf → repeat) at any scale, from this workstation to a GPU cluster. Use for any request to prepare, configure, launch, or scale a large training or simulation run for Drop7, or to set the software up on SLURM, Kubernetes, Ray, a cloud GPU pool, or a single multi-GPU machine.
---

# Drop7 scale-out

This skill turns "we have a lot of compute" into a sequence of bounded,
gated stages. Read `AGENTS.md` and `.agents/skills/million-point-research/SKILL.md`
first; this skill does not relax any rule there. The scientific direction is
registered as theory `TH-20260821-search-guided-self-play-at-scale-299ed02f`
and explained for readers at `web/content/learn/concepts/scale-out-direction.mdx`.

## What the loop is

1. Actors run a fair public-information search at every visited position, for
   **every legal column**, with chance scenarios shared across siblings, and emit
   one record per position (board, next disc, rise clock, seven sibling values).
2. A learner trains a small public board evaluator on those successor-closed
   records.
3. The frozen evaluator is gated against fair D4 on a fixed paired cohort and,
   only on a preregistered win, becomes the search leaf for the next iteration.

Compute scales the actors (more positions, wider/deeper search per label) and
the learner (more GPUs). Nothing about the rules, the information boundary, or
the evidence standard scales with it.

## Stages and gates — do them in order

| Stage | Where | Done when |
| --- | --- | --- |
| 0 Data-closure audit | workstation | written answer: can the existing every-sibling panel seed iteration 0 without opening new seeds? |
| 1 Batched simulator | workstation | differential gates vs `src/core/native/engine.hpp` and the TypeScript engine: zero mismatches over leased seeds, crack/reveal/rise/cascade included |
| 2 One iteration, tiny | workstation (iGPU) | evaluator trains; held-out sibling ranking measured on the locked every-sibling panel |
| 3 Three iterations, pilot | workstation | ranking improves across iterations or the theory is failing and the record says so |
| 4 Compute-response | workstation → small cluster | double actor compute, everything else fixed; paired 32-game SCREEN gain beyond bootstrap noise, or not |
| 5 Scale-out | cluster | same gates; larger cohorts; whole-game bootstrap bounds on the paired difference vs fair D4 |

Never skip to stage 5. If stage 3 cannot improve sibling ranking on one
machine, more machines will not change that, and a cheap negative result is the
correct outcome.

## Before any run, at any scale

- Register the experiment (`researchctl.py new experiment`) with candidate,
  comparator (fair D4), cohort role, seed lease, metrics, gate, budget, stop
  conditions, and artifact paths. Take a seed lease; record it in
  `docs/exploratory/lease-map.md` and `research/seeds/leases/`.
- Capture a machine profile (`make research-doctor`) for every node type.
- Training seeds, development seeds, and evaluation seeds are disjoint by
  construction; shards carry their lease ID and role in the manifest.
- Every candidate evaluator is frozen (bytes hashed) before it is evaluated,
  and evaluated only through the public interface.

## Route to the details

- [pipeline](references/pipeline.md): component contracts, record schema,
  shard layout, gating, checkpoint/resume, and the single configuration file.
- [clusters](references/clusters.md): mapping the configuration onto a single
  multi-GPU box, SLURM, Kubernetes, Ray, and cloud spot pools; what to record
  for each; what must not be assumed.

## Non-negotiables

- The simulator used by actors must be proven trajectory-identical to the
  reference before it produces a single training record.
- A student that reads seed, hidden values, future discs, score, level, or move
  number is disqualified regardless of score.
- Report whole-game means on paired fresh cohorts with bootstrap bounds. A
  single high-scoring game is an anecdote.
- Record partial, invalid, and negative results. A run stopped by a budget is
  `partial`, not a failure of the theory.
