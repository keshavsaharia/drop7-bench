# Preregistration — learned leaf x chance-estimator bias (A52-LEAF)

**Written before any gameplay cohort was run.** Namespace
`approaches/lifetime-objective/learned-leaf/`, run directory
`runs/RUN-A52-LEAF/`, seed lease `SEEDLEASE-A52-LEAF` =
`0xa5240000`–`0xa5247fff`. The binary refuses to start outside that lease
except on the one fixed evaluation cohort named in §5.

## 1. Theory and mechanism

[`finding-05`](../../../docs/exploratory/finding-05-chance-strata.md) measured
an interaction rather than a main effect: the fourth ply of fair expectimax is
worth **+86,172 points (95% lower bound +26,468)** when the chance estimator
uses seven strata and is exact on the next disc, and **nothing measurable
(+7,723, bound −42,743)** when it uses five and leaves an average 2.41 of the
seven disc values at zero weight. The proposed mechanism is that a systematic
chance-node bias is propagated and amplified by every additional ply, so extra
lookahead mostly buys a better estimate of a wrong quantity.

If that mechanism is right it is not specific to lookahead. **Any** additional
foresight has to travel back to the root through the same biased chance
expectation. Foresight injected at the leaf — a learned survival evaluator in
place of the frozen hand-weighted one — should therefore be capped the same way.

## 2. The prediction, stated before the cohorts were run

> **The learned leaf will help materially more at 7 chance strata than at 5.**
>
> Concretely, with `d5 = mean paired score(learned leaf, depth 4, 5 strata)
> − mean paired score(reference leaf, depth 4, 5 strata)` and `d7` the same
> quantity at 7 strata, both over the same 64 seeds:
>
> **`d7 > d5`, and the difference-in-differences `d7 − d5` is positive with a
> one-sided 95% bootstrap lower bound above zero.**

### Pass / fail

| Outcome | Criterion |
| --- | --- |
| **PASS** — prediction supported | `d7` one-sided 95% lower bound > 0 **and** `(d7 − d5)` one-sided 95% lower bound > 0 |
| **FAIL** — prediction refuted, hypothesis wrong | `d5` one-sided 95% lower bound > 0 **and** `(d7 − d5)` one-sided 95% lower bound ≤ 0. The learned leaf helps, and it helps as much (or more) with the biased estimator. The cap hypothesis is then wrong and this document will say so plainly. |
| **INCONCLUSIVE about the interaction** | neither `d5` nor `d7` has a lower bound above zero. The learned leaf is then a clean negative in its own right and the interaction is untested, not refuted. |

`d7 − d5` is bootstrapped **paired over whole games**: every seed contributes a
quadruple (ref-s5, learned-s5, ref-s7, learned-s7) and the resample draws
seeds, not arms. 20,000 resamples, percentile method, RNG domain recorded.

**What would falsify the prediction:** the learned leaf producing an equal or
larger gain at 5 strata than at 7. That is a real possible outcome — the
learned leaf could substitute for lookahead precisely where lookahead is
useless — and it will be reported as a refutation if observed.

## 3. Deviation from the original design, and why it was forced

The intended candidate was the trained residual CNN
(`runs/RUN-A51D-net/survival-c128-shuffled.pt`, 3,006,543 parameters) at the
leaf. **That is not implementable at depth 4 on this machine, and the number is
not close.** Measured before any gameplay:

| Quantity | Measurement | Source |
| --- | ---: | --- |
| leaf evaluations per decision, depth 4 / 5 strata | **615,090** (max 1,110,435) | `build/lifetime-leaf/leaf-probe`, 30 moves |
| leaf evaluations per decision, depth 4 / 7 strata | **2,271,280** (max 3,405,402) | same |
| distinct leaf states per decision (ceiling on a perfect memo) | 380,326 / 1,118,405 — dedup only 1.6x / 2.0x | same |
| exported CNN, one state, one thread | **4,122 µs** | `build/lifetime-leaf/net-check` |
| reference decision cost, depth 4 / 5 strata, one thread | 887 ms | `leaf-probe` |

615,090 x 4,122 µs = **2,535 seconds per decision** against a 0.887 s reference
decision: a factor of ~2,860. At 12 threads the CNN reaches 2,218 states/s, so
one 87-move game would take 6.7 hours of the whole machine. No caching (1.6x),
quantisation (~4x) or kernel work closes a 2,860x gap.

The experiment therefore runs with **LeafNet**, a student trained on the same
corpus, the same targets and the **same whole-origin split** as the CNN, sized
to the leaf budget: an NNUE-shaped model whose first layer is 135 gathered rows
rather than a dense matrix (572,367 parameters, 1.33 µs per state — 3,100x
faster). It is not a weaker signal by much:

| model | held-out lifetime Pearson | held-out MAE (moves) | µs/state | test rows |
| --- | ---: | ---: | ---: | ---: |
| SurvivalNet CNN (teacher) | 0.8646 | 14.25 | 4,122 | 486,819 |
| **LeafNet (deployed)** | **0.8564** | **14.44** | **1.33** | 486,819 |

Student-vs-teacher lifetime agreement on 4,096 held-out corpus states:
Pearson 0.9815, mean absolute difference 2.48 moves.

This deviation is recorded here, before the cohorts, rather than presented
afterwards as a design choice.

## 4. Candidate, comparator, and the leaf value

Candidate and comparator are the **same binary**,
`build/lifetime-leaf/search`, differing only in `--w`:

    leafValue = (1 - w) * frozen::fairLeaf(state) + w * scale * learnedValue(state)

`w = 0` short-circuits to `frozen::fairLeaf` before the model is touched, so the
comparator arms are the frozen reference bit-for-bit and cost exactly what the
reference costs. CHECK gate already recorded: `--parity`, 50 moves compared,
**0 mismatches**.

Two learned values are implemented; the choice is `--leaf-value`.

- **`lifetime` (expected to be better, and this is stated before the runs):**
  `expm1(lifetimeHead) * 3400`. Score is 94.29% flat 17,000-point row-rise
  bonus and correlates with lifetime at r = 0.9995, and the steady-state rate is
  3,400 points per move ([`finding-01`](../../../docs/exploratory/finding-01-score-is-survival.md)),
  so this is expected remaining score in real points.
- **`hazard`:** `sum_k P(survive k more rises) * 17000`, k = 1..12. Expected to
  be weaker because it **saturates**: 12 rises is 60 moves, while the lifetime a
  1,000,000-point mean needs is ~294 moves, so every position that is merely
  healthy scores the same 12 and the head stops discriminating exactly in the
  region the objective cares about.

Measured leaf-term scales on 20,000 corpus states, so `w` can be read as a
mixing weight between comparable spreads rather than an arbitrary constant:
`fairLeaf` mean −27,773 sd 28,715; `lifetime x 3400` mean 120,148 sd 61,300
(Pearson with fairLeaf 0.860); `hazard x 17000` mean 108,651 sd 52,829
(Pearson 0.892). Equal influence on action ranking is near `w = 0.32`.

## 5. Cohorts and their roles

| Cohort | Seeds | Games | Role |
| --- | --- | ---: | --- |
| PILOT | `0xa5240200`–`0xa5240207` | 8 | runtime projection only, no strength claim |
| TUNE-A | `0xa5241000`–`0xa524101f` | 32 | choose `w`, `scale`, `--leaf-value` at 5 strata |
| TUNE-B | `0xa5242000`–`0xa524201f` | 32 | confirm the choice at 7 strata |
| **EVAL** | **`0xa51d1000`–`0xa51d103f`** | **64** | the reported 2x2; the fixed paired cohort every other arm in this session used |

**The EVAL cohort is never tuned on.** It is already development data: it is
`finding-05`'s confirmation cohort, whose published figures are d4s5 = 297,327
mean / 87.16 moves / 1.9489 clears per move and d4s7 = 398,498 / 114.66 /
2.0571. Because `w = 0` is the frozen leaf bit-for-bit, **the two reference arms
of the 2x2 must reproduce those numbers exactly**; any deviation invalidates the
run and is reported as such rather than explained.

## 6. Work bound

Worst-case depth-4 work is 3,134,950 at 5 strata and 11,892,398 at 7. The frozen
3,200,000 bound silently degrades a 7-stratum depth-4 search to a completed
depth 3. **Every 7-stratum arm runs with `--max-work 16000000`** and every
5-stratum arm with `--max-work 3200000`; the value is recorded in each artifact's
`config` block and checked before the numbers are read.

## 7. Metrics, budget and stop conditions

Primary: mean final score over whole games, paired. Secondary and mandatory:
mean moves, numbered clears per move, covered reveals per move (against the
2.400 / 1.400 conservation requirement), mean occupied cells (fair D4 operates
at 23–24 of 49, the clairvoyant equilibrium is 19–20), censor count, score-
identity violations, leaf and model evaluations per decision, wall time per
decision, games per hour.

Budget: at most 12 threads, ceasing if the 1-minute load average from other jobs
exceeds 24. Stop conditions: any score-identity violation; any reference arm
failing to reproduce `finding-05`; any 7-stratum arm found running at the
3,200,000 bound; wall clock beyond 10 hours for the whole programme, in which
case whatever completed is reported with an explicit interrupted status.

Artifacts: `runs/RUN-A52-LEAF/` (`model/`, `parity/`, `pilot/`, `tune/`,
`eval/`), one `drop7-lifetime-cohort-v1` JSON per arm plus a
`drop7-learned-leaf-cost-v1` sidecar. Report:
`docs/exploratory/finding-08-learned-leaf.md`.

**A clean negative is a complete result.** The repository already contains
roughly 17 failed learned models; an honest eighteenth is worth more than a
strained positive.
