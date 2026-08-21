# Preregistration — validating the scenario suite as a strength proxy

**Written before any position-mode gameplay row was produced or inspected.**
Namespace `approaches/lifetime-objective/suite-validation/`, seed lease
`SEEDLEASE-A52-SUITE` = `0xa5258000`–`0xa525bfff`.

## Theory

`docs/exploratory/design-01-benchmark-suite.md` preregisters two checks that must
pass before any scenario-suite number may be cited as evidence of policy
strength, and states that until check 1 passes with a reported number **"the
suite is a diagnostic and cannot be cited as evidence of policy strength."**
Neither check has been run. `finding-02` §7 states explicitly that the fair
multi-tape evaluation "is deliberately **not** implemented here".

**Theory T-SUITE-1.** The fair position-mode metric of `design-01` Mode 2 — mean
score over K independent disc tapes drawn from a fixed start position and fixed
latent board, with common random numbers across policies — orders policies the
same way their whole-game means order them.

**Mechanism.** Score is 94% flat row-rise bonus and correlates with lifetime at
r = 0.9995 (`finding-01`). Over a horizon that spans at least one rise, a
policy's scenario score is dominated by how many rises it survives to collect,
which is a short-horizon image of the same survival quantity that whole-game
score measures. If the image is faithful, ranks agree.

**Falsification.** Stated as a signed, numeric rule below. If the suite cannot
reproduce the ordering of policies that differ by 3–4× in whole-game mean, the
theory is rejected and the suite is not a strength instrument.

## Position mode — the only fair mode

Mode 1 (puzzle mode) fixes the disc tape and grades against the clairvoyant
optimum. A public-information policy cannot be graded against it: the optimum is
a function of information the policy is forbidden to have, so the ranking it
induces is a ranking of luck as much as of skill.

Mode 2 is implemented here for the first time:

1. hold `board`, `latent`, and `movesRemaining` fixed — that is the *position*;
2. hold `discTape[0]` fixed — the visible next disc is public state, part of the
   decision problem, not of the hidden future;
3. redraw `discTape[1..H-1]` and every future risen row independently, K times;
4. give **every policy the same K tapes** (common random numbers);
5. score a policy on the position by its mean over the K tapes.

## Frozen configuration

| Item | Value |
| --- | --- |
| Suite | `approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl`, all 128 positions |
| Horizon | H = 9, the horizon the suite was minted at |
| Tapes | K = 4 (amended, see below) |
| Tape seeds | one lease seed per tape index, `0xa5258100`+k, spread over positions by splitmix64 |
| Threads | 8 |
| **Primary metric** | mean scenario **points** per (position, tape), pooled over all 128 × K cells |
| Secondary metrics | numbered clears per move; survival rate within H; mean moves |
| Comparator set | the nine policies below |

### Policies and their known whole-game means

Six fair arms on the **shared** 64-game cohort `0xa51d1000`–`0xa51d103f`
(`finding-05`, plus the coordinator-supplied d2s5 figure):

| policy | depth | strata | work bound | whole-game mean |
| --- | ---: | ---: | ---: | ---: |
| `d2s5` | 2 | 5 | 3,200,000 | 249,641 |
| `d2s7` | 2 | 7 | 16,000,000 | 265,294 |
| `d3s5` | 3 | 5 | 3,200,000 | 305,051 |
| `d3s7` | 3 | 7 | 16,000,000 | 312,327 |
| `d4s5` | 4 | 5 | 3,200,000 | 297,327 |
| `d4s7` | 4 | 7 | 16,000,000 | 398,498 |

Three weak baselines whose whole-game means come from a **different** 64-seed
cohort (`finding-01`): `center-first` 57,233, `random-legal` 80,778,
`lowest-column` 100,050.

The two cohorts are not the same seeds, so the nine-policy comparison is
cross-cohort and is labelled as such wherever it is reported.

### Two Spearman correlations, both declared primary-facing

- **S9** — all nine policies. Whole-game means span 57,233 to 398,498, a 7.0×
  range, which is the regime `design-01`'s criterion names ("policies that
  differ by 3-4x in whole-game mean"). This is the check `design-01` asks for.
- **S6** — the six fair arms on the shared cohort. A 1.6× range, no cross-cohort
  splice. This measures *fine* discrimination and is strictly harder.

## Decision rule, fixed in advance

| Verdict | Rule |
| --- | --- |
| **Usable as a strength proxy** | S9 ≥ 0.70 **and** S6 ≥ 0.60 |
| **Usable only as a coarse screen** | S9 ≥ 0.70 **and** S6 < 0.60 |
| **Not a strength instrument — Task 2 does not proceed** | S9 < 0.70 |

S9 ≥ 0.70 at n = 9 is p ≈ 0.018 one-sided under the null of no association.
The verdict is read off the **primary** metric (points). The secondary metrics
are reported for every arm and are explicitly labelled exploratory; a good
correlation on a secondary metric does not rescue a failed primary.

## Discriminating power, defined before it is measured

Reported in **logical work units**, not wall time, because
`docs/benchmarks.md` requires fixed-work comparison for strength and this
machine is shared.

- σ²_between: variance of the position-mode metric across the nine policies.
- σ²_within: variance of the same policy's metric across independent
  re-evaluations, obtained by splitting the K tapes into disjoint halves.
- For the named pair **d3s7 vs d4s7**: the paired mean difference across
  positions with CRN, its standard error over the 128 positions treated as the
  independent unit, the resulting t, and the logical work spent.
- The same pair on 64 whole games costs `Σ work/move × mean moves × 64` and, per
  `finding-05`, resolves that pair at t ≈ 2.37. The suite is *cheaper* only if it
  reaches t ≥ 2.37 for less logical work.

## Check 2 — the split

Deterministic, content-addressed, stratified by `origin`, fixed and hashed
before any tuning: a scenario is assigned to the sealed half iff the low bit of
the FNV-1a 64 hash of `"suite-split-v1:" + id` is 1, within each origin stratum
balanced to 32/32. The manifest records every id, its content hash, its half,
and a SHA-256 over each half's ordered id list.

## Resource budget and stop conditions

- clang++ only; at most 8 threads; the machine is shared with another session.
- Position mode stops at 90 minutes of wall clock. If it has not finished, K is
  reported at whatever value completed and the run is labelled partial.
- The structure probe (Task 2) stops at 45 minutes.
- No protected or final seed is opened. Only `SEEDLEASE-A52-SUITE` is drawn from.

## Amendment 1 — K reduced from 16 to 4, on measured cost alone

Recorded **before any scenario points row was produced or inspected**. A runtime
feasibility probe (8 positions x 2 tapes, the only gameplay run before this
amendment) measured the per-cell cost of one (position, tape) cell at H = 9:

| policy | logical work per cell | core-seconds per cell |
| --- | ---: | ---: |
| `d2s5` | 16,996 | 0.05 |
| `d2s7` | 34,415 | 0.10 |
| `d3s5` | 414,360 | 1.4 |
| `d3s7` | 1,155,752 | 3.1 |
| `d4s5` | 9,341,013 | 26 |
| `d4s7` | 35,252,025 | 101 |
| three baselines | ~8 | <0.001 |

K = 16 over 128 positions is 2,048 cells, which is 57 core-hours for `d4s7`
alone — 7.2 hours on the eight threads this work is allowed. K = 4 is 512 cells
and about 2.3 hours for the full nine-policy set, which fits the budget.

The probe read `work` and `moves`; it did not read `points`, the primary metric,
and no verdict-bearing quantity was inspected. The reduction is a cost decision,
not a result-dependent one. Consequence to be stated in the finding: the
within-policy variance estimate is a two-tape versus two-tape split rather than
an eight versus eight split, so it is noisier, and the discriminating-power
figures are correspondingly wider.

## Amendment 2 — recorded after the run, describing exactly what was done

Two things happened that a reader must be able to check against this document.

1. **The 90-minute stop fired.** The H = 9, K = 4 run was stopped at its
   preregistered budget with **95 of 128 positions complete**. Because positions
   are processed in an order determined only by the FNV-1a-64 hash of their id,
   the completed set is an unbiased subsample: 48 harvested / 47 synthetic,
   45 development / 50 sealed. The verdict statistic was stable from n = 13
   through n = 95.
2. **A post-hoc horizon arm was added after the verdict was read.** Re-scoring
   the same positions and the same tape streams at H = 25 with the seven cheaper
   policies (the two depth-4 arms were unaffordable at that horizon) is **not**
   part of check 1 and does not change its verdict. It is a diagnosis of *why*
   the preregistered configuration failed, and its direction — that a longer
   horizon should help — was predicted by the score decomposition before it was
   run. It is reported as post hoc everywhere it appears.

Nothing else deviated. The primary metric, the comparator set, the decision rule,
the split rule, and the discriminating-power definitions are as written above.

## What a failure means

If S9 < 0.70 the correct outcome is a recorded negative result: the suite is not
a strength instrument at H = 9 with K = 16 on the points metric, and no number
computed on it may be cited as evidence about policy strength. That rejects this
exact configuration, not the idea of a scenario benchmark.
