# Preregistration — distilling the *fair* receding-horizon planner (A526-DISTILL)

**Written before any student was trained and before any offline ranking number
was computed.** Namespace `approaches/lifetime-objective/planner-distill/`, run
directory `runs/RUN-A526-DISTILL-46d93fb7956e/`, seed lease
`SEEDLEASE-A52-DISTILL` = `0xa5260000`–`0xa526ffff` (game seeds in the low half,
sampler streams in the high half). Every binary refuses to start outside it,
except on the one fixed evaluation cohort named in §6.

Nothing outside this directory, `build/planner-distill/`,
`runs/RUN-A526-DISTILL-*/` and `docs/exploratory/` is created or modified.

## 1. Theory and mechanism

This repository contains roughly seventeen failed learned models.
[`audit-05`](../../../docs/exploratory/audit-05-optimistic-curriculum.md) §4
classifies their primary failure modes: sibling coverage / within-root
discrimination **6**, representation / information gap **3**, objective mismatch
**3**, distribution shift **1**, optimisation **0**. Oracle distillation
specifically failed held-out gates repeatedly, and audit-05 §5.4 assembles four
independent measurements which all say the oracle's advantage was
"overwhelmingly … exploitation of the realised tape".

[`finding-07`](../../../docs/exploratory/finding-07-fair-planning-ceiling.md)
changes the picture in one specific way. It measures a **legal**
receding-horizon planner — arm B of `flow-ceiling/fair-planner.hpp` — that
samples `K` completions of the hidden board, solves an exact `H`-move window
against each and plays the argmax of the mean. It never reads hidden values or
the future disc tape, and a self-test proves it: swapping every hidden value and
the entire future leaves the chosen column unchanged. On eight master tapes at
`H = 7, K = 256` it sustains 2.2309 clears and 1.2782 reveals per move with a
mean lifetime of 200.88 moves, against fair D4's 2.0467 / 1.1423 / 117.75 on the
identical futures.

**The mechanism claim.** Prior teachers were clairvoyant, and finding-07's
decomposition bounds the share of a clairvoyant teacher's margin that is hidden
information at up to 41%. Chasing that share is chasing something a public
student cannot represent. The fair planner's entire advantage is, by
construction, a function of public state. It is therefore the first well-posed
distillation target in this repository.

**This is a hypothesis about why the prior attempts failed, not a proven cause.**
None of the seventeen failures was rerun with a fair teacher, so "the teacher's
privilege was the problem" remains an untested explanation. What this experiment
can establish is narrower and still worth having: whether a public student can
absorb the advantage of a teacher whose advantage is definitely public.

## 2. The falsifiable prediction

> A state-only afterstate evaluator, trained with a within-root listwise loss
> against the fair planner's all-sibling values, will rank the planner's legal
> siblings on **held-out origin games** better than the fair depth-4 search
> ranks the same siblings.

The comparison is against fair D4 because fair D4 is both the repository's
reference policy and the search the student would be inserted into. A student
that ranks worse than the search it is joining cannot improve it.

## 3. What is measured, on which data

Split by **whole origin game**, 80/10/10, never by root row. The gate is read on
the 10% test origins, which are not used for model selection; the 10% validation
origins select the epoch and the architecture.

Per held-out root, with `s_c = immediate_c + f(afterstate_c)`:

| Metric | Definition |
| --- | --- |
| action completeness | fraction of legal columns carrying a teacher value; missing-label rate |
| top-1 | student argmax equals the teacher's argmax |
| top-2 | teacher's argmax is in the student's top two |
| pairwise | over within-root pairs with teacher value gap > 0.02 discs, fraction ordered correctly |
| normalised regret | `(V_max − V_chosen) / (V_max − V_min)` averaged over roots with a non-zero spread |
| calibration | Pearson and MAE of `f` against the teacher residual `value − immediate`, plus a ten-bin reliability table |
| stability | agreement of the student's argmax with the teacher's argmax computed from the first and from the second half of the K completions, separately |
| breakouts | all of the above by rise phase (moves until rise 1..5), occupancy band, and legal-action count |

`immediate_c` is supplied to both the student and the comparator, because the
search observes it directly at its chance node and no state-only evaluator can
represent it. Two scoring variants are reported: **one realisation** (the
afterstate the game actually entered) and **eight realisations** (independent
draws of the reveal randomness from its exact public marginal, averaged), the
latter because the deployed search averages its leaf over five or seven
stratified chance outcomes and a one-draw score can only understate the student.

**The teacher's own split-half agreement is reported first and is the ceiling.**
No student can agree with a label more often than the label agrees with itself.

## 4. Pass / fail, fixed before the numbers were read

Let `T1_s`, `PW_s`, `REG_s` be the student's held-out top-1, pairwise accuracy
and normalised regret under the eight-realisation score, and `T1_d4`, `PW_d4`,
`REG_d4` the same statistics for the unmodified fair depth-4 search on exactly
the same roots.

| Outcome | Criterion |
| --- | --- |
| **PASS** | `T1_s > T1_d4` **and** `PW_s > PW_d4` **and** `REG_s < REG_d4`, **and** the student wins on top-1 in at least 4 of the 5 held-out origin folds, **and** `T1_s ≥ 0.60 × (teacher split-half agreement)` |
| **PARTIAL** | the student beats fair D4 on pairwise accuracy and regret but not on top-1, or wins in aggregate but in fewer than 4 of 5 folds |
| **FAIL** | the student does not beat fair D4 on at least two of the three headline statistics |

The `0.60 ×` ceiling-relative floor exists so that a student cannot "pass" by
beating a comparator that is itself far from the label. The fold rule exists
because `docs/benchmarks.md` forbids promoting an aggregate win when a
preregistered split fails.

**Gameplay is run only on PASS.** On PARTIAL or FAIL the result is reported as a
negative and no cohort is opened, because the repository's most common recorded
process error is building on a model before its label was shown to carry usable
signal.

## 5. Teacher configuration and its measured strength

The teacher is arm B at **`H = 5`, `K = 256`**, objective *numbered discs
cleared*, one move committed per decision, 400-move cap. This is a **reduced**
configuration relative to finding-07's headline `H = 7, K = 256`; the reduction
is a budget decision, measured before it was made:

| configuration | seconds per decision (this host, contended) |
| --- | ---: |
| `H = 5, K = 64` | 0.146 |
| **`H = 5, K = 256`** | **0.567** |
| `H = 7, K = 64` | 4.315 |
| `H = 7, K = 256` | ~17 (extrapolated; not run) |

At `H = 7, K = 256` a 64-game corpus would cost about six hours of the machine
for roughly 12,000 roots. At `H = 5, K = 256` the same six hours buys an order
of magnitude more roots, and `K = 256` rather than `K = 64` is chosen
deliberately because the label's *within-root* noise is what six of the
seventeen documented failures died of.

**The student is therefore taught toward the reduced planner's ceiling, not the
headline planner's, and it is compared against that ceiling.** The reduced
planner's own strength is measured on this work's own master tapes, in the same
run that produces the corpus, together with fair depth 4 on the identical tapes.
finding-07's eight-seed figures are not used as the teacher's strength here:
those eight tapes are visibly easier than average (fair D4 survives 117.75 moves
on them against 94.06 on `finding-01`'s 64 fresh base-engine seeds).

## 6. Cohorts and roles

| Cohort | Seeds | Games | Role |
| --- | --- | ---: | --- |
| corpus A, on-policy | `0xa5260000`–`0xa526009f` | 160 | teacher strength **and** training corpus |
| corpus B, ε = 0.15 explored | `0xa5260200`–`0xa526025f` | 96 | state coverage off the teacher's own trajectory |
| D4 comparator | the same tapes as A | 160 | paired teacher-strength reference |
| smoke | `0xa5267000`–`0xa5267003` | 4 | CHECK-tier plumbing only, never evidence |
| **EVAL** | **`0xa51d1000`–`0xa51d103f`** | **64** | the shared evaluation cohort, opened only on PASS |

The EVAL cohort is already development data — it is `finding-05`'s confirmation
cohort — and is never tuned on. Its published reference figures are
d4s5 = 297,327 mean / 87.16 moves / 1.9489 clears per move and
d4s7 = 398,498 / 114.66 / 2.0571.

## 7. Work bound for any gameplay arm

`finding-05` measured an interaction: at depth 4, exact seven-strata chance
handling is worth +101,171 points (95% lower bound +47,457) while at depth 3 it
is worth nothing. Worst-case depth-4 work is 3,134,950 at five strata and
11,892,398 at seven, so the frozen 3,200,000 bound silently degrades a
seven-stratum depth-4 search to a completed depth 3. **Every seven-stratum arm
runs with `--max-work 16000000`**, and the completed depth is recorded and
checked before any number is read.

## 8. Budget, stop conditions, artifacts

At most 10 threads for CPU work; pause if the one-minute load average from other
jobs exceeds 24. GPU training in bf16 with GroupNorm (the bundled MIOpen emits
GFX9-only assembly for the BatchNorm *training* kernel on gfx1151).

Stop conditions: any score-identity violation; any information-boundary gate
failure; any incomplete-window rate above 1% in the teacher run; wall clock
beyond eight hours for the whole programme, in which case whatever completed is
reported with an explicit interrupted status.

Artifacts under `runs/RUN-A526-DISTILL-46d93fb7956e/`: `corpus/`, `model/`,
`eval/`, plus `sources.sha256`. Report:
`docs/exploratory/finding-11-planner-distillation.md`.

**A clean negative is a complete result.** If a student cannot absorb even a
*fair* teacher's advantage, that localises the difficulty to representation or
search integration rather than to the teacher's privilege, and it is the
eighteenth honest negative rather than a strained positive.
