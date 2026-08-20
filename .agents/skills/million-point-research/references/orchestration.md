# Autonomous orchestration

Use this procedure when the user gives a goal rather than a specific edit.

## Convert the directive into a program

Interpret “find the million-point game” as “work toward a public policy that
passes `artifacts/protocols/million-point-validation.json`.” Track a first
million-point individual game separately, but do not stop or claim success on
that basis.

1. Establish the current evidence boundary from `docs/research/status.md`.
2. Run the record validator, core tests, and machine doctor.
3. Inventory active work, available artifacts, data roles, and resource leases.
4. Rank candidate experiments by expected information gain, feasibility,
   runtime, and likelihood of addressing a documented failure.
5. Select one primary theory and one cheaper fallback diagnostic.
6. Register both before implementation, but run the fallback only if its stated
   trigger occurs.
7. Delegate independent, non-overlapping work: literature/data audit, simulator
   integrity, systems profiling, implementation, and adversarial evaluation.
8. Integrate through seed-free gates, then progress through benchmark tiers.
9. Record the result and choose the next theory. Continue while the directive,
   compute budget, and data authority remain active.

## Stop and escalation rules

Stop before an action when it requires protected/final data, a kernel/firmware
change, an unbounded compute purchase, external publication, or destructive
worktree changes not already authorized. Runtime difficulty alone is not a
reason to abandon the research loop: reduce to a preregistered pilot, profile,
or switch to the registered fallback.

Terminate a run at its preregistered wall, memory, error, or futility bound.
Record `partial` or `invalid` separately from game censoring. Never change a
gate after observing the data it controls.

## Parallel work ownership

- The coordinator owns IDs, seed/data leases, resource allocation, and merges.
- Implementers own isolated approach directories.
- Evaluators must be able to reproduce the protocol without private context.
- A red-team reviewer checks leakage, sibling coverage, seed reuse, hashes,
  result arithmetic, and claim language before promotion.
- Each participant writes its own contribution record; the coordinator records
  orchestration and integration, not others' source authorship.

See `docs/agents/orchestration.md` for the full state machine.
