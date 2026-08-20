---
name: audit-drop7-experiment
description: Independently audit a Drop7 theory, protocol, run, benchmark, result, or million-point claim for simulator parity, public-information leakage, seed/data reuse, sibling coverage, statistics, resource accounting, hashes, attribution, and evidence wording. Use when reviewing or validating research rather than designing the candidate.
---

# Audit a Drop7 experiment

Act as a skeptical verifier. Do not modify the candidate, open another cohort,
rerun protected/final data, or repair a failed result during the audit.

1. Read `AGENTS.md`, `docs/methodology.md`, `docs/benchmarks.md`, and
   `docs/agents/contributions-and-commits.md`.
2. Identify theory, experiment, run, result, machine, dataset, contribution, and
   seed-lease records. Missing required records are findings, not invitations to
   invent them.
3. Verify source, protocol, binary/model/data/result hashes and disclose dirty or
   non-Git source state.
4. Recompute result counts and summary arithmetic from canonical per-game rows.
5. Check corrected scoring, legal moves, determinism, reflection, RNG domains,
   public-state inputs, common-random-number alignment, and censor/failure logic.
6. Check that origin-level splits and every legal sibling match the experiment's
   claim. Played-action prediction cannot support an all-action ranking claim.
7. Verify seed role and state. Treat a cohort as opened at process start and
   never reclaim protected/final data after an interruption.
8. Separate run validity, scientific outcome, evidence tier, and theory scope.
9. Check machine/resource measurements; never add shared APU memory to host RAM.
10. Check that each model claims only concrete artifacts and that the coordinator
    does not absorb delegated implementation credit.

Report findings by severity, with exact file/record references, followed by a
verdict: `valid`, `partial`, or `invalid`; `pass`, `fail`, `inconclusive`, or
`not-applicable`; and the highest justified evidence tier. A first-party rerun
does not count as independent replication.
