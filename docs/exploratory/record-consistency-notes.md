# Record consistency notes

Observations made while writing plain-English overlays for the machine-readable
records (`web/content/research/*.mdx`). **No record was edited.** These are
reported so a future auditor can decide whether a successor record is owed;
under `AGENTS.md` a frozen or promoted record is immutable, so any correction
creates a successor rather than a rewrite.

Status: observations only, not a claim that any result is wrong.

## 1. Quantile-coverage level disagrees between criterion and observation

`RS-20260820T094500Z-5c1e9a04` states the gate criterion as "80% quantile
interval coverage within [0.70, 0.90]" while its `observed` field reports
"0.631 coverage of the nominal 76.5% outer-quantile interval". The nominal
level named in the criterion (80%) and in the observation (76.5%) differ. The
same mismatch appears in `EX-20260820-afterstate-pilot-h40-29b8588a`, whose
`gate.passCriteria` says 80% / [0.70, 0.90] while its own `metrics.secondary`
for later iterations says 76.5%.

Effect on the reading: the check failed either way, so the pass/fail outcome is
unaffected; only the stated nominal level is ambiguous. The overlay avoids
quoting the coverage number for that iteration.

## 2. `updatedAt` earlier than `createdAt`

`EX-20260820-afterstate-pilot-h40-k64-d7a9faf5` has `createdAt`
2026-08-20T09:50:00Z and `updatedAt` 2026-08-20T09:04:08Z — updated 46 minutes
before it was created. Its amendment timestamp (14:30:00Z) is also later than
`updatedAt`. `EX-20260820-afterstate-pilot-h40-29b8588a` shows the same
amendment-after-`updatedAt` pattern (updated 09:01:20Z, amendments 14:30:00Z).

Effect on the reading: none on the science; it does mean timestamps cannot be
used to order these records reliably.

## 3. "Only change" claim is narrower than the disclosure

`EX-20260820-afterstate-pilot-h40-k64-d7a9faf5`'s hypothesis says raising the
scenario count from K=8 to K=64 is "the only change" from
`EX-20260820-afterstate-pilot-h40-29b8588a`, but its own `reuseDisclosure` also
moves the held-out origins to a fresh seed block and folds iteration 1's
held-out roots into training. The disclosure's closing sentence ("this
experiment changes only the scenario count and the held-out origins") is the
accurate statement; the hypothesis sentence understates it.

Effect on the reading: two variables moved between those iterations, so a
difference between them cannot be attributed to K alone. The overlay says so.

## 4. Root counts differ between summary and gate check

`RS-20260820T184500Z-63c0a8e2` evaluates "2,689 fresh held-out roots" in its
summary while its corpus gate check reports "4,858,880 rows over 2,793 roots".
The difference is plausibly corpus roots versus roots that were actually
evaluable (for example, roots with fewer than two legal actions cannot have a
top-two near-tie), but the record does not say so.

Effect on the reading: the override-gate percentages are computed against 2,689
and are internally consistent with that denominator; the overlay uses the
record's own numbers and does not reconcile the two counts.

---

# Part 2 — discrepancies found while writing the console's approach pages

Twelve writers read the ledger, the experiment index, the findings and the
machine-readable records in order to describe each approach for a general
reader. Doing that end to end surfaced places where two retained sources
disagree, or where a summary document has fallen behind the record it
summarises. **Nothing below was fixed by editing a record**; the console pages
carry both readings where they conflict, and this list exists so a coordinator
can decide what deserves a successor record or a status update.

Several of these were already known: `docs/exploratory/proposed-status-corrections.md`
proposes exact wording for seven of them, and the `audit-01`…`audit-05` notes
raise others. Those are marked *(already proposed)*.

## A. Summary documents that are behind the record

**A1. `status.md` still describes the afterstate line as unimplemented.** Its
evidence snapshot row reads "AFBR-40 afterstate idea | Task-record only |
Proposal; no source, checkpoint, or result", and its closing paragraph says the
approach "has no implementation, protocol, model, or measurements in this
repository and must not appear in a list of attempted results." The repository
now holds a full implementation under
`approaches/afterstate-learning/distributional-afterstate/`, a theory record at
`lifecycle: assessed` / `assessment: not-supported-as-tested`, four experiment
records and four result records. `status.md` §7 also stops before the
top-two override gate, which is the only preregistered held-out test in which a
learned intervention improved on fair D4 at all (in both half-folds; it failed
because the second half's margin was below the preregistered bar).

*This one also propagated into the console.* The concepts primer originally
repeated the "no implementation" claim; it was corrected during this pass and
now describes the four pilots and their outcomes.

**A2. Findings 06–13 appear nowhere in `status.md` or the experiment index.**
`grep -c lifetime-objective docs/research/experiment-index.md` returns 0. A
reader following the console's evidence links from that family reaches only
`docs/exploratory/`. Not a contradiction, but the "current evidence boundary"
is materially behind the retained record.

**A3. The seven-stratum verdict is stated as settled and is not.** The index and
`strategies.md` describe more chance samples as "score-neutral to worse".
`audit-03` C2 rescores the ledger's own paired means to **+4,336** under
corrected scoring, and `finding-05` measures **+101,171 points / +27.5 moves**
on 64 fresh paired games (95% lower bound +47,457), replicated on a second
cohort. *(already proposed — `audit-03` rows D4 and D8.)*

**A4. The rollout veto is listed as runtime-paused; it has since been
rejected on evidence.** `finding-03` retested it at corrected scoring on 32
paired games (paired mean −46,510.5, 95% lower bound −91,924.6, 9–4–19, both
flow rates worse), run validity `valid`, outcome `fail`, tier `development`.
The index still says runtime-paused.

**A5. The 2.4 clears / 1.4 reveals figures are labelled as measurements but are
arithmetic.** `status.md` calls them "diagnostic targets from limited runs, not
proven universal thresholds". `finding-01` derives them exactly — 12 discs onto
49 cells every five moves gives 12/5 = 2.400 and 7/5 = 1.400 — and says so.
Both `finding-01` and `finding-06` raise this against `status.md`. The console
pages present them as arithmetic and cite `finding-01`.

## B. The repository's headline reference number

**B1. The 308,295.578 / 90.031 over-64-games figure has no identified cohort.**
`audit-03` H2: it appears exactly once in the ledger (`history.md:4234`) as an
internal bootstrap comparator inside the regenerative expert-iteration
experiment. Which 64 seeds, what dispersion, and what censoring are not
recorded, so it does not meet `methodology.md`'s own cohort-reporting
requirements — yet it propagates to `status.md`, `README.md`, the experiment
index, and `research/benchmarks/baselines-v1.json`. *(already proposed.)*

Note that a *different* 64-game fair-D4 cohort **is** fully retained, with
per-game rows, on lease `SEEDLEASE-A51D` (`finding-01`): mean 321,991.71875,
median 266,282, max 1,017,234, 0 censored. The console's heavy-tails page uses
that one and states explicitly that the two must not be pooled.

## C. Internal inconsistencies inside individual records

- **C1.** `RS-20260820T094500Z-5c1e9a04` states its criterion as "80% quantile
  interval coverage within [0.70, 0.90]" but observes "0.631 coverage of the
  nominal 76.5% outer-quantile interval". The nominal level disagrees between
  criterion and observation; the same mismatch is in the matching experiment
  record. The check failed either way, so no outcome changes.
- **C2.** `EX-20260820-afterstate-pilot-h40-k64-d7a9faf5` has `updatedAt`
  (09:04:08Z) **earlier** than `createdAt` (09:50:00Z), and amendments dated
  later than `updatedAt`. Its result references a run stamped 09:04:11Z, so
  `createdAt` looks like the wrong field. Timestamps in this family cannot be
  used to order records.
- **C3.** The same record's hypothesis says raising scenarios from K=8 to K=64
  is "the only change" from the previous iteration, while its own
  `reuseDisclosure` also moves the held-out origins and folds the previous
  iteration's held-out roots into training. Two variables moved, so a
  difference between those iterations cannot be attributed to K alone.
- **C4.** `RS-20260820T184500Z-63c0a8e2` evaluates "2,689 fresh held-out roots"
  in its summary while its corpus gate check reports 2,793 roots. Plausibly
  corpus roots versus evaluable roots, but the record never says so.
- **C5.** Two panel measurements of exact fair D4 are quoted interchangeably in
  adjacent prose: pairwise 0.6585 / regret 0.2766 (preflight) versus 66.8191% /
  0.276577 (martingale-dual audit). Different frozen protocols and tie handling;
  they should not be quoted across records.

## D. Scoring-mode and reproducibility hazards

- **D1.** `history.md:57`'s archival disclaimer scopes itself to "experiments
  below that identify `levelBonus: 7000`" — a literal string that appears
  nowhere else in the file, so the disclaimer selects nothing. *(audit-03 C3.)*
  Consequence: several ledger sections are 7,000-point Sequence-scored with no
  in-file label. The console pages state the mode per source and attribute the
  reconstruction to `audit-03` where the ledger is silent.
- **D2.** Sources split into those that pin the old scoring with a
  `static_assert(kLevelBonus == 7'000)` and those that do not. The unpinned
  7,000-era sources (for example the n-tuple temporal-coherence and
  phase-conditioned programs, and the learned-guidance search sources) now build
  against the 17,000-point engine, so re-running them will silently disagree
  with their own recorded numbers.
- **D3.** `artifacts/protocols/optimistic-phase-ntuple/protocol.json` requires a
  corrected-scoring D4 to reproduce a 7,000-point figure (176,925.25 / 116.375),
  which is permanently unsatisfiable. The artifact is frozen and was not
  touched; a superseding protocol is the remedy. *(audit-03 C1.)*

## E. Verdicts that do not match the code they describe

- **E1.** The index's `direct-policy` verdict describes teacher roll-ins drifting
  from their teacher, but that program contains no teacher, no imitation target
  and no roll-in mechanism — it is cross-entropy weight search against complete
  games. Either the verdict belongs to a different configuration or it is
  misfiled.
- **E2.** The index's `denoised-value` row implies score means that were never
  retained; only move means exist (78.75→81.25 screen, 79.5→88.125
  confirmation). *(audit-03 L1 already flags this.)*
- **E3.** "CFPI" names two different things in `history.md` — a depth-3
  comparator in the fair-only horizon screen, and the conservative
  fitted-policy-iteration program — and the ledger never says whether they are
  the same. The console pages do not attribute one's numbers to the other.
- **E4.** The index lists all three evolution programs as "Rejected —
  ledger-recorded", but the linear `evolution.cpp` appears in the ledger only as
  the source of reused features, with no result section of its own.
- **E5.** The PyTorch PPO index row stops at the aborted direct run and omits a
  completed third stage (32 iterations, 16,384 games, then a 64-game
  development cohort read and rejected). `strategies.md` does mention it.
- **E6.** `strategies.md` records the primal-dual actor-critic at "about 176,000
  points and 54 moves"; the ledger records 175,834 / **55.006**.

## F. Engine and interface facts worth surfacing

- **F1. Hidden gray values are drawn at reveal time, not stored.** `audit-01` M2:
  this repository's engine assigns a fresh value at the moment of reveal, in
  row-major order, while the original game fixes the value when the disc is
  created. To a player the two are indistinguishable; for research they are not,
  because there is no persistent latent board unless latent mode is enabled.
  *The console's rules page originally stated the original game's behaviour as
  though it were this engine's; it was corrected during this pass.*
- **F2. The censoring cap is not one number.** The benchmark contract says 2,000
  moves; the engines default to 500, and retained runs use 500, 1,000 or 2,000.
  Every quoted cohort should state its own cap. *(audit-01 L1.)*
- **F3. The gray-throughput policy is registered `publicInformation: false`.**
  Its chance-sample salt hashes level and moves-played, both outside the
  deployable interface. It is a legitimate playground entry, not a legal
  candidate, and the console shows its extended-state badge.
- **F4. "Throughput" means two different things.** In this repository it is
  discs cleared and revealed per move, not games per second. A draft console
  page had adopted the wrong sense; it was corrected.

## G. Documentation defects

- **G1.** `docs/exploratory/finding-15-depth5-exact-estimator.md` carries the H1
  heading "Finding 14", which is also the number of
  `finding-14-leaf-reweight.md`. A citation to "finding 14" is ambiguous.
- **G2.** `docs/exploratory/finding-12-fair-planner-ceiling-extended.md` §6
  contains an unfinished table row — a four-cell placeholder against an
  eight-column table — for an H = 9 clairvoyant arm. No number was quoted from
  it.
- **G3.** `docs/exploratory/README.md`'s contents table is stale: it lists
  through finding-03, marks two notes "in progress", and omits findings 05–08
  and the gpu-02/gpu-03 notes. *(reconciliation-01 gap 4.)*
- **G4.** `approaches/lifetime-objective/flow-ceiling/README.md` (the operational
  note, not the rendered page) still presents figures that `finding-12`
  withdrew: 58.8% gap closure, since corrected to 27.1% on a matched fresh
  cohort; 2.2309 clears per move, cohort-inflated versus 2.0260 on fresh tapes;
  and 117.75 mean moves for fair D4 versus 93.56.
- **G5.** `strategies.md` reports the perfect-information oracle above one
  million points without noting that all twelve of those games were censored at
  the 500-move cap, so the figure is a lower bound on the oracle's own play and
  is not comparable with uncensored cohorts.
- **G6.** The implied move target for a million points is quoted two ways: about
  294 moves in `status.md`/`finding-01` arithmetic, and "roughly 285" in the
  ledger's evolved-public-policy entry.
