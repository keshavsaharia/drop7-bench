# Finding 05 — Search depth pays only when the chance estimator is unbiased

**Status:** exploratory. Evidence tier `development`, **replicated across two
independent cohorts** for the headline arm. Positive result.
**Namespace:** `approaches/lifetime-objective/risk-calibration`, runs
`runs/RUN-A51D-risk/` and `runs/RUN-A51D-s7confirm/`.
**No existing file was modified.** The frozen reference is untouched; this is a
new parameterized candidate that reproduces it exactly at default settings
(CHECK gate: 50 moves compared, 0 mismatches).

## Summary

The frozen fair-D4 reference draws **five** stratified samples at each chance
node. The next visible disc is uniform on **seven** values. Five strata cannot
represent seven atoms: an independent audit measured that on average **2.41 of
the 7 disc values receive zero weight at every node**, deterministically, so the
error never averages out ([`audit-02-fair-d4.md`](audit-02-fair-d4.md), H1).

Setting the sample count to seven makes the next-disc expectation **exact**.
Doing so is worth **+101,171 mean points (+34%) and +27.5 moves (+32%)** over
the frozen reference on previously unread seeds.

But the effect is not "more samples is better." It is an **interaction**:

| Comparison | Δ score | 95% lower bound | Δ moves | W–T–L | Verdict |
| --- | ---: | ---: | ---: | :---: | --- |
| depth 4: 7 strata − 5 strata | **+101,171** | **+47,457** | +27.50 | 41–0–23 | significant |
| depth 3: 7 strata − 5 strata | +7,276 | −45,961 | +2.42 | 34–0–30 | not significant |
| 7 strata: depth 4 − depth 3 | **+86,172** | **+26,468** | +22.39 | 40–0–24 | significant |
| 5 strata: depth 4 − depth 3 | +7,723 | −42,743 | +2.69 | 29–0–35 | not significant |

**With a biased chance estimator, the fourth ply buys nothing. With an exact
one, it is worth 86,000 points.** Depth and estimator quality are complements,
not independent knobs.

This offers a mechanism for one of the repository's most-repeated conclusions —
that "deeper search is not automatically stronger"
([`strategies.md`](../strategies.md)). Every depth experiment in the historical
record was run on top of the same five-stratum estimator. If the extra ply
mainly propagates and amplifies a systematic chance-node bias, that is exactly
what one would observe.

## Two cohorts

| Cohort | Seeds | Data status when read | Δ score (d4s7 − d4s5) | 95% lower bound | Δ moves | W–T–L |
| --- | --- | --- | ---: | ---: | ---: | :---: |
| First | `0xa51d0000`–`0xa51d003f` | already read by the reference cohort and the terminal-utility sweep | +71,138 | +5,826 | +19.53 | 38–0–26 |
| **Confirmation** | `0xa51d1000`–`0xa51d103f` | **previously unread** | **+101,171** | **+47,457** | **+27.50** | 41–0–23 |

The confirmation cohort is the evidential one; the first cohort cannot be
confirmation because those seeds had already influenced other decisions.

### Confirmation cohort detail

| Metric | 5 strata | 7 strata |
| --- | ---: | ---: |
| Mean score | 297,327 | **398,498** |
| Median score | 260,415 | 344,630 |
| Q25 score | 192,352 | 212,864 |
| Min score | 86,935 | 103,331 |
| Max score | 836,427 | 1,341,287 |
| Standard deviation | 150,550 | 254,414 |
| Mean moves | 87.16 | **114.66** |
| Median moves | 77.50 | 102.50 |
| Numbered clears/move | 1.9489 | **2.0571** |
| Covered reveals/move | 1.0697 | **1.1549** |
| Censored games | 0 | 0 |
| Score-identity violations | 0 | 0 |
| Games ≥ 1,000,000 | 0 | 2 |

Median, Q25 and minimum all improve, so the gain is not one lucky tail game.
The flow rates move in the predicted direction: the gap to the 2.400 clears and
1.400 reveals per move required for indefinite survival
([`finding-01`](finding-01-score-is-survival.md)) narrows by about 19% and 22%.
That is the mechanism that has to be true for the effect to be real.

## The work bound is part of the result

Worst-case depth-4 work is `Σ_{d≤4} [Σ_{l≤d} b^l + b^d]` for branching
`b = 7 columns × strata`:

| Strata | Branching | Worst-case D4 work | Frozen bound 3,200,000 |
| ---: | ---: | ---: | --- |
| 5 | 35 | 3,134,950 | fits, with 2.1% headroom |
| 7 | 49 | 11,892,398 | **exceeded by 3.7×** |

The frozen bound was sized to sit just above five-stratum depth 4. A seven-
stratum run left at that bound silently hits the work limit and falls back to a
completed **depth 3** — so a naive comparison measures depth-3-with-7-strata
against depth-4-with-5-strata and reports it as a chance-sampling result. Every
arm here declares its bound explicitly; the seven-stratum arms ran at 16,000,000.

Measured work per move confirms the projection: 1,296,034 at five strata versus
4,956,614 at seven, a ratio of **3.82×** against a predicted 3.79×.

**This is a candidate for what happened historically.** The ledger's seven-
stratum experiment was rejected on a small score loss with a *move gain* — which
is also the signature of a shallower search. Combined with
[`audit-03`](audit-03-claim-arithmetic.md)'s finding that the same rejection
reverses sign under corrected scoring (−164 → +4,336), there are now two
independent reasons that rejection is unsafe. This is a hypothesis about the
historical run, not a claim about it: the original configuration was not re-run.

## Cost, stated plainly

This buys strength with compute. At fixed depth 4 it costs 3.82× the work per
move. Under [`benchmarks.md`](../benchmarks.md) that makes it a fixed-depth
comparison owing a fixed-work account:

| Arm | Work/move | Relative | Mean score | Mean moves | Score per unit work |
| --- | ---: | ---: | ---: | ---: | ---: |
| depth 2, 7 strata | 4,139 | 0.003× | 265,294 | 79.48 | 64.10 |
| depth 3, 5 strata | 54,429 | 0.042× | 305,051 | 89.84 | 5.60 |
| depth 3, 7 strata | 156,834 | 0.121× | 312,327 | 92.27 | 1.99 |
| depth 4, 5 strata *(frozen)* | 1,296,034 | 1.000× | 297,327 | 87.16 | 0.23 |
| depth 4, 7 strata | 4,956,614 | 3.824× | **398,498** | **114.66** | 0.08 |

Two things follow, and the second is uncomfortable for the frozen reference:

1. **Depth 3 with 7 strata matches depth 4 with 5 strata at one-eighth the
   work** (+14,999, 95% bound −31,029, 32–0–32 — a tie, not a win). If decision
   cost matters, that configuration is strictly better value.
2. **On this cohort the frozen depth-4 reference is statistically
   indistinguishable from depth 3** at five strata (+7,723 for *depth 3*, 95%
   bound −42,743, 29–0–35). The ledger's D4-over-D3 result rests on an eight-game
   cohort. This does not refute it — different seeds, and 64 games is still small
   — but it is a reason to re-examine it.

## Limitations

- 64 paired games per arm. The headline d4s7-vs-d4s5 arm is replicated on two
  cohorts; **every other row in the interaction table rests on one cohort** and
  should be treated as a single observation.
- Score standard deviation is 38–64% of the mean, so 64-game means carry roughly
  ±19,000 to ±32,000 at one standard error.
- Seven strata makes the *next-disc* expectation exact. Covered-disc reveals are
  still sampled, and a reveal wave can expose a variable number of cells, so the
  reveal expectation is **not** exact at seven strata. Whether reveal sampling is
  now the binding bias is untested; 14 strata (two samples per disc value) would
  distinguish "exactness" from "more samples", at roughly 7.8× the work of seven.
- No fixed-*time* comparison is reported; arms ran at different thread counts on
  a contended machine, so only work-per-move is comparable.
- These are this repository's simulator semantics, including the two rise-boundary
  scoring discrepancies in [`audit-01`](audit-01-engine-fidelity.md).
- Mean 398,498 is far below the 1,050,000 the frozen protocol requires before a
  candidate may be frozen. **No protected or final seed was opened or is
  justified by this result.**

## Reproduce

```sh
./approaches/lifetime-objective/risk-calibration/build.sh
./build/lifetime/risk-calibration --parity --seed-start 0xa51d0100 --parity-games 2 --parity-moves 25
B=./build/lifetime/risk-calibration
$B --depth 4 --chance-samples 5 --max-work  3200000 --seed-start 0xa51d1000 --games 64 --threads 30 --output fresh-s5.json
$B --depth 4 --chance-samples 7 --max-work 16000000 --seed-start 0xa51d1000 --games 64 --threads 30 --output fresh-s7.json
$B --depth 3 --chance-samples 7 --max-work 16000000 --seed-start 0xa51d1000 --games 64 --threads 30 --output s7d3.json
$B --depth 3 --chance-samples 5 --max-work  3200000 --seed-start 0xa51d1000 --games 64 --threads 30 --output s5d3.json
```
