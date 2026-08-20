# Benchmark procedure

Read `docs/benchmarks.md` before starting a gameplay or performance run.
Use `research/benchmarks/profiles-v1.json` to distinguish fixed-work strength,
machine-max throughput, and the broader resource frontier.

1. Classify the run as `CHECK`, `PILOT`, `SCREEN`, `STANDARD`, `QUALIFY`,
   `PROTECTED`, or `FINAL`.
2. Verify that the experiment protocol names the exact cohort/data role and
   that a valid lease exists before process start.
3. Freeze source, model, command, algorithmic work, comparator, move cap, gate,
   stop conditions, and output locations.
4. Capture a system profile. For strength claims, prefer fixed algorithmic work;
   report fixed-time results separately as a resource frontier.
5. Use the same ordered games for candidate and fair D4. Preserve common random
   numbers across sibling estimates when the method allows it.
6. Retain deterministic per-game rows plus summary statistics. Execution may be
   parallel, but stored results must be sorted into canonical cohort order.
7. Record illegal moves, incomplete searches, process failures, censoring,
   logical work, wall/CPU time, peak host memory, and GPU observations.
8. Evaluate the gate once. A post-hoc diagnostic uses already-read development
   data and cannot upgrade the evidence tier.

Never equate faster wall time on a different machine with a stronger policy.
Never count shared APU memory twice as host RAM and VRAM.
