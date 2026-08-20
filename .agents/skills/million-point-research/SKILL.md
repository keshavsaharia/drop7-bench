---
name: million-point-research
description: Orchestrate rigorous Drop7 strategy research, including hypothesis selection, public-information safety, isolated implementation, hardware-aware benchmarking, evidence grading, contribution attribution, and million-point qualification. Use for any request to invent, implement, train, benchmark, accelerate, compare, or validate a Drop7 policy or to continue the research program autonomously.
---

# Drop7 research

Follow the repository research contract in `AGENTS.md`. Treat a high-level goal
as authority to run a sequence of bounded, reversible research stages, not as
authority to open sealed data or run without a resource limit.

## Start every research task

1. Read `docs/research/status.md`, `docs/methodology.md`, and the relevant part
   of `docs/strategies.md` before selecting an approach.
2. Run `make research-validate` and the cheapest relevant correctness checks.
3. Inspect Git/worktree state and existing research records. Never initialize
   Git, overwrite another contributor's work, or invent missing commit IDs.
4. Capture the machine with `make research-doctor` before performance claims.
5. Classify the work as engineering, diagnostic, algorithmic, or validation.

## Route to the detailed procedure

- Read [orchestration](references/orchestration.md) for autonomous planning,
  concurrency, stopping rules, and the million-point directive.
- Read [benchmarks](references/benchmarks.md) before opening any seed or making
  a performance/strength comparison.
- Read [records](references/records.md) before creating a theory, experiment,
  result, contribution record, or commit.
- Read [hardware](references/hardware.md) before changing thread counts,
  memory limits, GPU code, ROCm, kernel, firmware, or BIOS settings.
- Read [roadmap](references/roadmap.md) when choosing the next strategy family.

When the task is an independent review of an existing claim, also read the
repository's `.agents/skills/audit-drop7-experiment/SKILL.md` and do not combine
the verifier role with candidate repair or a new cohort.

## Required research loop

1. State one falsifiable theory and the reason it is worth testing.
2. Check data, semantic, runtime, and dependency feasibility without new data.
3. Register the theory and a preregistered experiment with a fixed budget,
   comparison, cohort role, gate, and stop conditions.
4. Work in an isolated approach directory and unique run directory. Preserve
   the current fair-D4 reference and frozen protocols.
5. Pass seed-free mechanics, legality, determinism, reflection, information-
   boundary, resource-bound, and engine-parity checks before gameplay.
6. Advance through benchmark tiers in order. Stop a failed configuration; do
   not silently tune it on the same evaluation cohort.
7. Write an immutable result record, including negative or aborted results.
8. Write one contribution record per model/agent. Report exact identity when
   exposed and `unknown` when it is not; never guess.
9. Update summaries only when the evidence level changes.
10. Choose the next experiment by expected information gain per unit of
    compute and repeat while the directive, authority, and budget remain.

## Non-negotiable claims

- The research target is a public-information policy whose **mean** exceeds one
  million under the frozen qualification protocol. One high-scoring game is
  interesting, not success.
- A privileged oracle is a teacher or diagnostic, never a deployable result.
- `completed`, `passed`, and `validated` are distinct. Use only the narrowest
  status and evidence level supported by retained artifacts.
- A faster implementation is an engineering result only when actions, random
  events, work semantics, and traces remain equivalent. Otherwise register it
  as a new algorithmic candidate.
- Protected and final cohorts remain sealed until the versioned protocol says
  they may be opened. If authorization is ambiguous, stop before reading them.
