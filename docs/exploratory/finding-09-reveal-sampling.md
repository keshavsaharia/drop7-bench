# Finding 09 — The chance node's two random quantities are welded together, and unwelding them is worth a whole ply

**Status:** exploratory. Evidence tier `development` for the depth-3 arms,
`pilot` for the runtime projections. **Positive result, with a monotone
dose-response and one arm interrupted.**
**Namespace:** `approaches/lifetime-objective/reveal-sampling`, run
`runs/RUN-A525-reveal/`, build `build/reveal-sampling/`.
**No existing file was modified.** The frozen reference and the single-knob
parameterized search are consumed unmodified through generated copies that
differ from the originals by exactly one entry-point line, verified at build
time. Files were created only under
`approaches/lifetime-objective/reveal-sampling/`, `build/reveal-sampling/`,
`runs/RUN-A525-reveal/` and `docs/exploratory/`.
**Seed lease:** `SEEDLEASE-A52-REVEAL` = `0xa5250000`–`0xa5257fff` for CHECK
probes and pilots. The headline arms play the **fixed shared evaluation cohort**
`0xa51d1000`–`0xa51d103f` (64 games) that every arm in
[`finding-05`](finding-05-chance-strata.md) used, so the comparison is direct.

## Summary

The frozen fair search has one knob, `chance_samples`, that governs two
different random quantities — the next visible disc and the covered-disc
reveals — and indexes both with the same sample counter, so they are perfectly
rank-correlated. This document builds a search that factors the chance node into
`--disc-samples N` × `--reveal-samples M`, proves it decision-identical to both
existing arms at M = 1, and measures the reveal axis.

**The reveal axis pays, and it pays monotonically.** Holding the disc exact at
seven strata and holding depth at three:

| depth-3 arm | (disc, reveal) joint coverage | mean score | Δ vs 7-strata arm | 95% lower bound | clears/move |
| --- | ---: | ---: | ---: | ---: | ---: |
| N=7, M=1 *(= the existing 7-strata arm)* | 14.3% | 312,327 | — | — | 1.9849 |
| N=7, M=3 | 42.9% | 337,306 | +24,980 | −23,451 | 2.0033 |
| **N=7, M=6** | **85.7%** | **376,442** | **+64,116** | **+7,475** | **2.0447** |

Score, moves, numbered clears, covered reveals and (downward) occupancy all move
monotonically with M, and the ordering tracks the fraction of the chance node's
joint atoms that receive non-zero weight. M = 3 alone is not significant; M = 6
is, at 36–0–28.

**The headline comparison is against the frozen reference.** Depth 3 with six
reveal samples beats frozen depth-4 fair D4 by **+79,115 points (95% lower bound
+30,242, 35–0–29)** — and it is **statistically indistinguishable from
finding-05's depth-4 seven-strata arm** (−22,056, 95% bound −89,867, 30–0–34, a
tie) at **4,244,020 logical work per move against that arm's 4,956,614**.

So the corrected statement of finding-05 is not "depth needs an exact chance
estimator". It is symmetric: **at fixed work, a fourth ply and a better reveal
estimate buy roughly the same thing, and the reveal estimate is slightly
cheaper.** finding-05 found one half of that; this finding supplies the other.

The mechanism check passes. Flow rates move in the required direction and by an
amount comparable to finding-05's positive result: +0.0598 numbered clears and
+0.0422 covered reveals per move from M = 1 to M = 6, with mean occupancy
falling from 23.88 to 23.49 of 49 cells.

## 1. The coupling, in the frozen code

The frozen chance node draws two different random quantities from one knob.

**Reveal values.** `cfpi::detail::StratifiedRandom`
(`src/core/native/public-behavior.hpp:595–607`) is constructed as
`StratifiedRandom{state_seed, sample, chance_samples, 0}` and its `nextDisc()`
is called once per revealed cover inside the cascade
(`public-behavior.hpp:655–657`), advancing an `event` counter each time. A wave
can expose a variable number of cells and each takes an independent uniform
1..7 value.

**The successor's visible next disc.** `cfpi::detail::sampledNextDisc`
(`public-behavior.hpp:736–742`), domain `kDiscSampleDomain`, always `event = 0`.

Both go through the same quadrature, `stratifiedUnit`
(`public-behavior.hpp:579–593`):

```
event_seed = mix32(seed ^ domain ^ (event+1)*kDepthMultiplier)
rotation   = event_seed % count
stratum    = (sample + rotation) % count
jitter     = mix32(event_seed ^ (sample+1)*kSampleMultiplier) / 2^32
return (stratum + jitter) / count
```

The coupling is that **both consumers are passed the same `sample` index and the
same `count`**. In the frozen driver this is
`approaches/fair-expectimax/reference/fair-only-horizon.cpp:211–237`
(`for (int sample = 0; sample < kChanceSamples; ++sample)`, then
`StratifiedRandom{state_seed, sample, kChanceSamples, 0}` at `:214` and
`sampledNextDisc(state_seed, sample, kChanceSamples)` at `:231`), repeated for
the depth-4 driver at
`approaches/fair-expectimax/reference/fair-only-depth4.cpp:137–163`
(`:140` and `:157`) and in the shared library search at
`public-behavior.hpp:824–846` (`:826` and `:840`). The parameterized single-knob
search inherits it verbatim at
`approaches/lifetime-objective/risk-calibration/search.cpp:114–133`
(`:116` and `:132`).

Two consequences follow, and the second is the one that turned out to matter:

1. `chance_samples` cannot be raised for one quantity without raising it for the
   other, so finding-05's 5 → 7 change moved both.
2. Because sample *i*'s disc value and sample *i*'s reveal values are both
   deterministic functions of *i*, they are **perfectly rank-correlated**. Across
   the seven samples the pair (first reveal value, next disc) takes only 7 of its
   49 joint outcomes.

Two things in the frozen code are *not* defects and were checked before
changing anything. `state_seed` is column-independent
(`scenarioSeedForState`, `public-behavior.hpp:721–734`, hashes board, next disc
and moves remaining but not the column), so all siblings at a node see the same
unit values — textbook common random numbers, which reduces comparison variance
without biasing the difference. And `playMoveSampled` draws its own next disc
internally (`public-behavior.hpp:711–712`) which `evaluateAction` immediately
overwrites; that is deliberate, and makes the successor's disc independent of
how many reveals a column's cascade happened to consume.

## 2. The factorization

`approaches/lifetime-objective/reveal-sampling/search.cpp` replaces the single
loop with N·M scenarios indexed by a disc index `d ∈ [0,N)` and a reveal index
`r ∈ [0,M)`, total `T = N·M`:

```
scenario s = r * N + d
reveals:   StratifiedRandom{state_seed, s, T, 0}
next disc: sampledNextDisc(state_seed, d, N)
```

Two properties make this a strict generalization rather than a new policy.

**At M = 1 it collapses to the single-knob search exactly.** `s = d` and `T = N`,
so every draw, the iteration order, the work accounting and the divisor are
bit-identical to `chance_samples = N`. That is what makes N=5, M=1 the frozen
reference and N=7, M=1 the existing seven-strata arm, rather than something that
merely resembles them.

**For M > 1 the disc keeps exactly N equally weighted strata while the reveal
marginal gets all T.** The stride `s = r·N + d` means one disc branch's M reveal
draws are spaced 1/M apart in the unit interval rather than occupying a
contiguous block, so each disc branch also sees a properly stratified reveal
sample.

### What raising M actually fixes

`approaches/lifetime-objective/reveal-sampling/chance-coverage.py` reimplements
`mix32` and `stratifiedUnit` in Python and measures, over 20,000 synthetic chance
nodes, the fraction of joint atoms that receive non-zero weight:

| N | M | scenarios | next disc | 1 reveal | 2 reveals joint | 3 reveals joint | (disc, reveal) joint |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 5 | 1 | 5 | 65.7% | 65.5% | 10.1% | 1.5% | 10.1% |
| 7 | 1 | 7 | **100.0%** | **100.0%** | 14.3% | 2.0% | 14.3% |
| 5 | 3 | 15 | 65.7% | 100.0% | 21.9% | 3.7% | 29.3% |
| 7 | 2 | 14 | 100.0% | 100.0% | 21.4% | 3.6% | 28.6% |
| 7 | 3 | 21 | 100.0% | 100.0% | 23.8% | 4.3% | 42.9% |
| 7 | 6 | 42 | 100.0% | 100.0% | 26.2% | 5.2% | 85.7% |
| 7 | 12 | 84 | 100.0% | 100.0% | 27.4% | 5.6% | **100.0%** |

This sharpens the premise of the experiment and partly corrects it. **At seven
strata each individual reveal event's marginal is already exact**, not just the
next disc's — seven strata over seven atoms is exact for any single
`stratifiedUnit` consumer. The residual error is not in any marginal. It is in
the **joint**: 7 samples cover 14.3% of the two-reveal grid, 2.0% of the
three-reveal grid, and 14.3% of the (disc, reveal) grid.

Raising M attacks the (disc, reveal) joint efficiently — 14.3% → 42.9% → 85.7% →
100% at M = 3, 6, 12 — because it decouples the disc index from the reveal
index. It attacks the reveal-by-reveal joint only weakly — 14.3% → 23.8% →
26.2% → 27.4% — because all reveal events within one scenario still share the
sample index `s` and therefore stay rank-correlated with each other. **M is
mostly a disc/reveal decoupling knob, and only secondarily a reveal-joint knob.**

**The measured strength ordering tracks the (disc, reveal) column and not the
reveal-by-reveal column.** Mean score goes 312,327 → 337,306 → 376,442 as that
coverage goes 14.3% → 42.9% → 85.7%, while two-reveal joint coverage barely
moves. That is a specific, falsifiable attribution of *which* part of the chance
node was broken: it was the correlation between the disc and the reveals, not
the correlation among the reveals.

## 3. CHECK gate

No gameplay arm was started before these passed. All are move-by-move
comparisons on whole games from `SEEDLEASE-A52-REVEAL`.

| gate | oracle | configuration | moves compared | action mismatches | logical-work mismatches |
| --- | --- | --- | ---: | ---: | ---: |
| A | frozen `ref::chooseDepth4Action` (`fair-only-depth4.cpp`) | N=5, M=1, depth 4, work 3,200,000 | 50 | **0** | n/a |
| B1 | single-knob `risk::ParameterizedSearch`, `chance_samples`=5 | N=5, M=1, depth 3 | 222 | **0** | **0** |
| B2 | single-knob, `chance_samples`=7 | N=7, M=1, depth 3 | 220 | **0** | **0** |
| B3 | single-knob, `chance_samples`=5 | N=5, M=1, depth 4 | 90 | **0** | **0** |
| B4 | single-knob, `chance_samples`=7 | N=7, M=1, depth 4 | 30 | **0** | **0** |

The gate-B oracle is not a copy of the existing arm's logic — it is the existing
arm's own source, `approaches/lifetime-objective/risk-calibration/search.cpp`,
compiled into the same binary through a generated copy whose only difference from
the original is the entry-point line (`build.sh` refuses to build if more than
one line differs). Logical work is compared as well as the chosen column, which
is a much stronger equality than action agreement alone.

**End-to-end identity, 64 whole games each.** The two M = 1 depth-3 arms were run
on the full shared cohort and compared field-by-field against the artifacts the
*existing* `risk-calibration` binary produced in
[`finding-05`](finding-05-chance-strata.md):

| new arm | compared against | games | field mismatches | total logical work |
| --- | --- | ---: | ---: | --- |
| depth 3, N=7, M=1 | `runs/RUN-A51D-s7confirm/s7d3.json` | 64 | **0** | 926,105,555 = 926,105,555 |
| depth 3, N=5, M=1 | `runs/RUN-A51D-s7confirm/s5d3.json` | 64 | **0** | 312,966,881 = 312,966,881 |

Score, moves, censor flag, rises, board clears, level/clear/chain points,
numbered clears, covers revealed, maximum chain depth and mean occupancy agree
exactly on all 64 seeds, and the summed logical work is equal to the unit. The
factored search at M = 1 is the same policy, not an approximation of it.

## 4. The work bound

Worst-case iterative-deepening work is the frozen arithmetic
(`fair-only-depth4.cpp:52–62`) with branching generalized from
`7 × chance_samples` to `b = 7 × N × M`:

`Σ_{i≤d} [ Σ_{l≤i} b^l + b^i ]`

The frozen bound of 3,200,000 was sized to sit just above five-stratum depth 4
(3,134,950, a 2.1% margin). **A larger branching factor left at that bound does
not error — it silently completes a shallower depth and reports it as a
chance-sampling result.** Every arm here therefore declares `--max-work`
explicitly, and the binary refuses to run a gameplay cohort without it.

| arm | b = 7·N·M | worst-case work | `--max-work` used | worst-case cache entries | cache used |
| --- | ---: | ---: | ---: | ---: | ---: |
| d3, N=5, M=1 | 35 | 89,565 | 3,200,000 | 1,295 | 60,000 |
| d3, N=7, M=1 | 49 | 242,697 | 16,000,000 | 2,499 | 60,000 |
| d3, N=5, M=3 | 105 | 2,348,745 | 2,348,745 | 11,235 | 60,000 |
| d3, N=7, M=3 | 147 | 6,418,461 | 6,418,461 | 21,903 | 60,000 |
| d3, N=7, M=6 | 294 | 51,084,852 | 51,084,852 | 87,024 | 87,025 |
| d3, N=7, M=12 | 588 | 407,634,528 | *not run* | 346,920 | — |
| d4, N=5, M=1 *(frozen)* | 35 | 3,134,950 | 3,200,000 | 45,430 | 60,000 |
| d4, N=7, M=1 | 49 | 11,892,398 | 16,000,000 | 122,598 | 60,000 |
| d4, N=7, M=2 | 98 | 187,336,114 | 187,336,114 | 960,694 | 960,695 |

The two M = 1 rows keep the bounds finding-05 used, because reproducing those
arms bit-for-bit is the CHECK gate. The others are set to their own worst case.
Cache size is a pure performance knob here and cannot change a decision: the
declared bound assumes *no* cache at all, so cache eviction can only make the
search do more of the work the bound already allows, never less depth.

**Evidence that the bound never bound.** Every gameplay decision records its
completed depth; the binary counts decisions that finished below the target
depth and work-limit events, and reports the maximum work any single decision
consumed.

| arm | decisions | below target depth | work-limit events | min completed depth | max work in one decision | bound | headroom |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| d3, N=5, M=1 | 5,750 | **0** | **0** | 3 | 85,085 | 3,200,000 | 37× |
| d3, N=7, M=1 | 5,905 | **0** | **0** | 3 | 239,071 | 16,000,000 | 67× |
| d3, N=5, M=3 | 5,888 | **0** | **0** | 3 | 1,805,370 | 2,348,745 | 1.30× |
| d3, N=7, M=3 | 6,317 | **0** | **0** | 3 | 4,739,805 | 6,418,461 | 1.35× |
| d3, N=7, M=6 | 7,005 | **0** | **0** | 3 | 34,706,994 | 51,084,852 | 1.47× |

Not one decision in 30,865 completed below its target depth, and the busiest
single decision in the whole study used 77% of its bound (the N=5, M=3 arm;
the M=6 arm's busiest used 68%). Every arm below is a
genuine depth-3 result.

## 5. The arms

Shared evaluation cohort `0xa51d1000`–`0xa51d103f`, 64 paired games, 2,000-move
cap, corrected 17,000-point Hardcore scoring. Score-decomposition identity
violations: **0** in every arm. Censored games: **0** in every arm.

| arm | depth | N | M | mean | median | Q25 | min | max | sd | moves | clears/move | reveals/move | occupied | work/move | ≥1M |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| frozen strata | 3 | 5 | 1 | 305,051 | 259,100 | 157,442 | 87,955 | 1,009,561 | 187,244 | 89.84 | 1.9638 | 1.0861 | 23.88 | 54,429 | 1 |
| control | 3 | 5 | 3 | 312,556 | 281,704 | 189,585 | 103,749 | 1,054,752 | 172,760 | 92.00 | 1.9868 | 1.1111 | 23.54 | 368,518 | 1 |
| 7-strata arm | 3 | 7 | 1 | 312,327 | 267,279 | 175,287 | 85,589 | 846,544 | 177,026 | 92.27 | 1.9849 | 1.1001 | 23.88 | 156,834 | 0 |
| reveal ×3 | 3 | 7 | 3 | 337,306 | 285,023 | 196,045 | 103,180 | 931,090 | 208,038 | 98.70 | 2.0033 | 1.1111 | 23.81 | 1,045,719 | 0 |
| **reveal ×6** | 3 | 7 | 6 | **376,442** | 322,859 | 227,224 | 103,015 | 1,002,557 | 223,366 | **109.45** | **2.0447** | **1.1423** | 23.49 | 4,244,020 | 1 |
| *(reference)* | 4 | 5 | 1 | 297,327 | 260,415 | 192,352 | 86,935 | 836,427 | 150,550 | 87.16 | 1.9489 | 1.0697 | 24.29 | 1,296,034 | 0 |
| *(reference)* | 4 | 7 | 1 | 398,498 | 344,630 | 212,864 | 103,331 | 1,341,287 | 254,414 | 114.66 | 2.0571 | 1.1549 | 23.15 | 4,956,614 | 2 |

The two depth-4 rows are finding-05's arms on the same cohort, reproduced here
for comparison; they were not re-run.

### Paired deltas over whole games

One-sided 95% lower bounds by percentile bootstrap, 20,000 resamples, RNG seed
`0xb0075eed`, resampling whole games.

| comparison | Δ score | 95% lower bound | Δ moves | Δ clears/move | Δ reveals/move | W–T–L | verdict |
| --- | ---: | ---: | ---: | ---: | ---: | :---: | --- |
| **d3: (7,6) − (7,1) — the reveal axis** | **+64,116** | **+7,475** | +17.19 | +0.0598 | +0.0422 | 36–0–28 | **significant** |
| d3: (7,3) − (7,1) | +24,980 | −23,451 | +6.44 | +0.0184 | +0.0110 | 32–0–32 | not significant |
| d3: (7,6) − (7,3) — the M=3→6 step | +39,136 | −20,714 | +10.75 | +0.0414 | +0.0312 | 37–1–26 | not significant |
| **d3: (7,6) − (5,1) — vs frozen strata** | **+71,391** | **+13,603** | +19.61 | +0.0809 | +0.0562 | 39–0–25 | **significant** |
| d3: (5,3) − (5,1) — control, reveal axis at biased disc | +7,506 | −42,146 | +2.16 | +0.0229 | +0.0250 | 37–0–27 | not significant |
| d3: (7,1) − (5,1) — disc exactness *(finding-05)* | +7,276 | −45,804 | +2.42 | +0.0211 | +0.0140 | 34–0–30 | not significant |
| **d3 (7,6) − d4 (5,1) — vs the frozen reference policy** | **+79,115** | **+30,242** | +22.30 | +0.0958 | +0.0726 | 35–0–29 | **significant** |
| d3 (7,6) − d4 (7,1) — vs finding-05's best | −22,056 | −89,867 | −5.20 | −0.0124 | −0.0126 | 30–0–34 | **tie** |

**The dose-response is the result.** Each individual step (M=1→3, M=3→6) is
inside noise on 64 games; the endpoints are not. Score, moves, clears, reveals
and occupancy all order correctly across three settings of one knob, which is a
much stronger signal than any single pairwise test, and the ordering matches the
mechanism table in §2.

**The control does its job and its answer is now informative.** If the reveal
axis were simply substituting for disc exactness, (5,3) would recover most of
what (7,1) gains over (5,1). It gains +7,506 against (7,1)'s +7,276 — nominally
identical, both inside noise. But the large effect at (7,6) is measured with the
disc *already exact*, so it cannot be disc exactness in disguise. The two axes
are not substitutes at small M; the reveal axis simply needs M ≥ 6 to show.

**Reveal sampling substitutes for the fourth ply, slightly more cheaply.** Depth
3 at M = 6 ties finding-05's depth-4 seven-strata arm (−22,056, 95% bound
−89,867, 30–0–34) using 4,244,020 work per move against 4,956,614 — 86% of the
cost. That reframes finding-05: the fourth ply and a decorrelated reveal
estimate are, at this budget, two ways of buying the same thing.

## 6. Flow rates — the mechanism that had to move, and did

[`finding-01`](finding-01-score-is-survival.md) establishes that Hardcore score
is very nearly 3,400 × lifetime, and [`finding-06`](finding-06-flow-ceiling.md)
that indefinite survival requires **≥ 2.400 numbered clears and ≥ 1.400 covered
reveals per move**. If reveal sampling improved the policy, these are the
quantities that would have to move.

| policy | clears/move | reveals/move | mean occupied cells (of 49) | mean moves | source |
| --- | ---: | ---: | ---: | ---: | --- |
| **requirement for steady state** | **2.400** | **1.400** | — | ∞ | finding-06 |
| d3, N=5, M=1 *(frozen strata)* | 1.9638 | 1.0861 | 23.88 | 89.84 | this run |
| d3, N=5, M=3 *(control)* | 1.9868 | 1.1111 | 23.54 | 92.00 | this run |
| d3, N=7, M=1 | 1.9849 | 1.1001 | 23.88 | 92.27 | this run |
| d3, N=7, M=3 | 2.0033 | 1.1111 | 23.81 | 98.70 | this run |
| **d3, N=7, M=6** | **2.0447** | **1.1423** | **23.49** | **109.45** | this run |
| d4, N=5, M=1 | 1.9489 | 1.0697 | 24.29 | 87.16 | finding-05 |
| d4, N=7, M=1 | 2.0571 | 1.1549 | 23.15 | 114.66 | finding-05 |
| fair D4 (latent-mode engine) | 2.0467 | 1.1423 | 23.91 | 117.75 | finding-07 |
| legal sampling planner, H=7, K=256 | 2.2309 | 1.2782 | 21.38 | 200.88 | finding-07 |
| clairvoyant planner, H=7 | 2.3601 | 1.3785 | 16.89 | 366.25 | finding-07 |

**The mechanism moved, monotonically, and by a material amount.** From M = 1 to
M = 6 the clear rate rises 0.0598 and the reveal rate 0.0422 — respectively 14%
and 14% of the distance from the 7-strata arm to the requirement, and about a
third of the distance from fair D4 to finding-07's legal sampling planner. Mean
occupancy falls 0.39 cells, in the direction finding-07 associates with longer
life. Mean lifetime rises 17.19 moves.

Two calibrations worth stating plainly. The M = 6 arm reaches **exactly fair
D4's reveal rate (1.1423)** while running a *shallower* search; and its clear
rate, 2.0447, is within 0.002 of fair D4's 2.0467. **A depth-3 search with a
decorrelated chance node reproduces the flow profile of a depth-4 search with a
correlated one.**

None of this reaches the 2.400 / 1.400 requirement. Every arm here still fills
its board and dies; no arm was censored at the move cap.

## 7. Work per move, and whether the gain is bought with compute

| arm | work/move | relative to frozen d4 | mean score | score per unit work | wall (this machine) |
| --- | ---: | ---: | ---: | ---: | --- |
| d3, N=5, M=1 | 54,429 | 0.042× | 305,051 | 5.60 | 21 s @ 12 threads |
| d3, N=7, M=1 | 156,834 | 0.121× | 312,327 | 1.99 | 55 s @ 12 threads |
| d3, N=5, M=3 | 368,518 | 0.284× | 312,556 | 0.85 | 1,664 s @ 2 threads |
| d3, N=7, M=3 | 1,045,719 | 0.807× | 337,306 | 0.32 | 1,057 s @ 8 threads |
| d3, N=7, M=6 | 4,244,020 | 3.274× | **376,442** | 0.089 | 4,442 s @ 12 threads |
| d4, N=5, M=1 *(frozen)* | 1,296,034 | 1.000× | 297,327 | 0.23 | finding-05 |
| d4, N=7, M=1 | 4,956,614 | 3.824× | 398,498 | 0.080 | finding-05 |

**This buys strength with compute at very nearly the same
exchange rate as the fourth ply, and no better.** M = 1 → 6 at depth 3 costs
27.1× the logical work per move for +64,116 points. Score per unit work falls
monotonically with M, as it does with depth. Under
[`benchmarks.md`](../benchmarks.md) this is a fixed-depth comparison that owes a
fixed-work account, and the account says the cheapest arm in the table is still
the best value per unit work, exactly as finding-05 found.

What is new is the **substitution**: at a matched work budget of roughly 4.2–5.0
million per move, depth 3 with M = 6 and depth 4 with M = 1 are
indistinguishable in strength (30–0–34), and the depth-3 arm is 14% cheaper.
That is a genuine second axis on the frontier, not merely a second way to spend
compute — and it is available at depth 3, where a decision costs a third of a
depth-4 decision's worst case.

Measured work per move stays far below the worst-case bound because the
transposition cache collapses the many reveal scenarios that produce identical
successor boards — most moves cause no cascade at all, so their M reveal
variants are the same state. Predicted worst-case ratio for M = 1 → 6 at depth 3
is (294/49)³ = 216×; the measured ratio is 27.1×. That is why the depth-3 M
sweep was affordable at all.

## 8. Verdict

- **Validity: valid.** The candidate is decision-identical to both existing arms
  at M = 1 by explicit move-by-move comparison (562 decisions, 0 action
  mismatches, 0 logical-work mismatches) and by exact 64-game artifact identity
  against the existing binary's own output. Work bounds were computed per
  configuration, passed explicitly, and verified never to bind: 0 of 30,865
  decisions completed below target depth, busiest decision at 77% of bound.
  Score-decomposition identity holds on every game; 0 censored games.
- **Outcome: pass, with the caveat that the effect needs M ≥ 6 to clear noise on
  64 games.** The hypothesis was that at seven strata the residual chance-node
  error is reveal sampling and that this axis is under-sampled. Both are
  supported: the residual is specifically the **(disc, reveal) joint**, and
  decorrelating it is worth +64,116 over the 7-strata arm (95% bound +7,475) and
  +79,115 over the frozen reference policy (95% bound +30,242), with flow rates
  moving monotonically in the required direction.
- **Evidence tier: `development`.** 64 paired games per arm on a
  previously-read fixed evaluation cohort. No protected or final seed was opened,
  and none is justified. Mean 376,442 is far below the 1,050,000 the frozen
  protocol requires before any candidate may be frozen.

**What this changes for the research program.**

1. **finding-05's conclusion is half of a symmetric statement.** It found that
   the fourth ply is worthless with a biased chance estimator and worth 86,000
   points with an exact one. The mirror is now measured: at fixed depth 3, a
   decorrelated chance estimator is worth as much as the fourth ply, slightly
   cheaper. Depth and chance-node quality are two exchangeable ways of spending
   the same budget, not a hierarchy.
2. **The defect has a precise name.** It is not "too few samples" and not "the
   disc marginal is biased" — at seven strata every marginal is exact. It is that
   the disc and the reveals are the same random variable in disguise, and a
   column whose value depends on the *pair* cannot be evaluated. That is a
   two-line change to how one index is computed.
3. **This partially answers the question finding-07 left open.** finding-07
   bounded the fair planner's advantage over D4 at 0.1842 clears per move and
   attributed it to planning structure. Chance estimation alone now recovers
   0.0598 of that at depth 3 — roughly a third — so the residual attributable to
   planning structure is smaller than finding-07's framing implied.
4. **The obvious next arms are the two this session could not finish** (§10),
   and the higher-value one is depth 4 at M = 2, because the whole point of §5 is
   that the two axes trade against each other and nobody has measured them
   together.

## 9. Limitations

1. **Depth 3 only, for the completed arms.** The depth-4 arm that would test
   whether the two axes compound rather than substitute was launched and killed
   (§10). Everything here is measured at one depth.
2. **The individual M steps are not separately significant.** (7,1)→(7,3) is
   +24,980 with a 32–0–32 split and (7,3)→(7,6) is +39,136 at 37–1–26; only the
   endpoints and the comparisons against the frozen configurations clear zero.
   The claim rests on the monotone ordering of five quantities across three
   settings plus the significant endpoint tests, not on any one pairwise result.
3. **M is mostly a disc/reveal decoupling knob** (§2). It raises (disc, reveal)
   joint coverage from 14.3% to 85.7% at M = 6, but the reveal-by-reveal joint
   only from 14.3% to 26.2%, because all reveal events in one scenario share the
   sample index. A design giving each scenario an independent reveal event stream
   would attack the reveal joint directly and is **not** tested here. The
   attribution in §2 is therefore that the *(disc, reveal)* correlation was the
   binding defect; the reveal-by-reveal correlation remains largely untouched and
   unmeasured.
4. **64 paired games.** Score standard deviation is 55–62% of the mean, so a
   64-game mean carries roughly ±22,000 to ±28,000 at one standard error.
5. **One cohort, already read.** These seeds were opened by finding-05 and are
   development data permanently. Nothing here can be confirmation evidence, and
   the result should be replicated on a fresh block before it is promoted.
6. **No fixed-time comparison.** Arms ran at 2–12 threads on a machine whose load
   average was 20–55 throughout, driven mostly by another contributor's jobs.
   Only work per move is comparable; the wall-clock column is provenance, not a
   performance claim.
7. **`--auto-cache` raises the LRU cache above the frozen 60,000 entries** for
   arms whose worst-case entry count exceeds it (d3 M=6 at 87,025). This cannot
   change a decision — the work bound assumes no cache — but it is a declared
   deviation and is recorded per arm in §4.
8. **These are this repository's simulator semantics**, including the two
   rise-boundary scoring discrepancies in
   [`audit-01`](audit-01-engine-fidelity.md).
9. **A model contribution record under `research/contributions/` is owed and was
   not written**, because this work was scoped to create files only under
   `approaches/lifetime-objective/reveal-sampling/` and `docs/exploratory/`. The
   same debt is open for finding-02, finding-06 and finding-07.

## 10. Arms not completed in this session

Recorded rather than omitted, as the repository contract requires.

- **depth 4, N=7, M=2 — INTERRUPTED, no result.** Launched on the shared cohort
  with `--max-work 187336114 --auto-cache --threads 8` and **killed by the
  runtime after about one hour** with no games written. This is a partial run,
  not a censored result, and it appears in no table above. It was authorized by
  a matched pilot rather than guessed: 2 games × 15 opening moves against an
  identically-run (N=7, M=1) pilot gave 21,943,224 work/move against 6,107,746 —
  a **3.59× logical-work ratio and a 3.29× wall ratio**, far below the 15.75×
  worst-case bound ratio, because the cache collapses duplicate reveal outcomes.
  Max work in one decision was 53,154,318 (28% of bound) with 0 decisions below
  target depth. Whole-game work/move projects to about 17.8M, i.e. about 11.6
  core-seconds per move, so 64 games project to **1.7–3.7 hours on 14 threads
  idle** and perhaps 5–7 hours at the load average of 45–55 this machine carried
  — within the 12-hour budget, which is why it was started.
  **This is now the single highest-value open arm**, because §5 shows the two
  axes are exchangeable at fixed work and nobody has measured whether they
  compound.
- **depth 3, N=7, M=12** (b = 588, worst-case work 407,634,528) — **not run.**
  This is the configuration that takes (disc, reveal) joint coverage to 100%, so
  it is the natural endpoint of the dose-response in §5, but it projects to
  roughly 5.7 hours on 14 threads and the compute budget closed first. A
  deliberate no-run, and now clearly worth doing.

## Reproduce

```sh
# build (clang++ explicitly; the Makefile's CXX ?= clang++ loses to make's
# builtin CXX=g++, which trips a false -Werror=array-bounds in
# src/core/native/public-behavior.hpp)
./approaches/lifetime-objective/reveal-sampling/build.sh
B=./build/reveal-sampling/reveal-sampling

# CHECK gate, before any gameplay
$B --parity --seed-start 0xa5250000 --parity-games 2 --parity-moves 25
$B --gate --depth 3 --disc-samples 5 --max-work  3200000 --seed-start 0xa5250010 --parity-games 4 --parity-moves 60
$B --gate --depth 3 --disc-samples 7 --max-work 16000000 --seed-start 0xa5250010 --parity-games 4 --parity-moves 60
$B --gate --depth 4 --disc-samples 5 --max-work  3200000 --seed-start 0xa5250010 --parity-games 3 --parity-moves 30
$B --gate --depth 4 --disc-samples 7 --max-work 16000000 --seed-start 0xa5250010 --parity-games 2 --parity-moves 15

# work bounds; --max-work is mandatory for a gameplay cohort
for M in 1 3 6 12; do $B --work-bound --depth 3 --disc-samples 7 --reveal-samples $M; done
$B --work-bound --depth 4 --disc-samples 7 --reveal-samples 2

# arms, shared evaluation cohort
R=runs/RUN-A525-reveal
$B --depth 3 --disc-samples 7 --reveal-samples 1 --max-work 16000000 --seed-start 0xa51d1000 --games 64 --threads 12 --quiet --output $R/d3-n7-m1.json
$B --depth 3 --disc-samples 5 --reveal-samples 1 --max-work  3200000 --seed-start 0xa51d1000 --games 64 --threads 12 --quiet --output $R/d3-n5-m1.json
$B --depth 3 --disc-samples 7 --reveal-samples 3 --max-work  6418461 --auto-cache --seed-start 0xa51d1000 --games 64 --threads 8 --quiet --output $R/d3-n7-m3.json
$B --depth 3 --disc-samples 5 --reveal-samples 3 --max-work  2348745 --auto-cache --seed-start 0xa51d1000 --games 64 --threads 2 --quiet --output $R/d3-n5-m3.json
$B --depth 3 --disc-samples 7 --reveal-samples 6 --max-work 51084852 --auto-cache --seed-start 0xa51d1000 --games 64 --threads 12 --quiet --output $R/d3-n7-m6.json

# the M = 1 arms must be identical to the existing single-knob artifacts
C=approaches/lifetime-objective/reveal-sampling/compare.py
python3 $C identity $R/d3-n7-m1.json runs/RUN-A51D-s7confirm/s7d3.json
python3 $C identity $R/d3-n5-m1.json runs/RUN-A51D-s7confirm/s5d3.json

# tables
python3 $C table \
  "d3 N5 M1=$R/d3-n5-m1.json" "d3 N5 M3=$R/d3-n5-m3.json" \
  "d3 N7 M1=$R/d3-n7-m1.json" "d3 N7 M3=$R/d3-n7-m3.json" \
  "d3 N7 M6=$R/d3-n7-m6.json"
python3 $C table "d4 N7 M1=runs/RUN-A51D-s7confirm/fresh-s7.json" "d3 N7 M6=$R/d3-n7-m6.json"
python3 $C table "d4 N5 M1=runs/RUN-A51D-s7confirm/fresh-s5.json" "d3 N7 M6=$R/d3-n7-m6.json"

# joint chance-atom coverage
python3 approaches/lifetime-objective/reveal-sampling/chance-coverage.py
```

### Environment

AMD Ryzen AI Max+ 395 (16 physical / 32 logical cores), 125 GiB RAM, AMD clang
23.0.0git, `-O3 -std=c++20 -pthread -Wall -Wextra`. Shared with another
contributor's jobs throughout; load average 20–55, at most 14 threads used by
this work. Frozen source hashes recorded at build time in
`build/reveal-sampling/sources.sha256`:

```
45e7c223…  approaches/fair-expectimax/reference/fair-only-depth4.cpp
8828379b…  approaches/fair-expectimax/reference/fair-only-horizon.cpp
d2c74210…  approaches/lifetime-objective/risk-calibration/search.cpp
14de83dc…  approaches/lifetime-objective/common/harness.hpp
8b4267af…  src/core/native/public-behavior.hpp
b6dcde5f…  src/core/native/engine.hpp
```

---

# Continuation — the two arms §10 left open

**Added 2026-08-21 by a later session.** Nothing above this line was edited.
This section completes `depth 4, N=7, M=2` (the interrupted arm) and
`depth 3, N=7, M=12` (the deliberate no-run), both on the same fixed shared
evaluation cohort `0xa51d1000`–`0xa51d103f`, 64 paired games. New files were
created only under `approaches/lifetime-objective/reveal-sampling/`,
`build/reveal-sampling/` and `runs/RUN-A525-reveal/`.

**Headline: the two axes substitute, they do not compound, and the reveal
dose-response does not continue to the 100%-coverage endpoint.**

| arm | mean | Δ vs its comparator | 95% lower | 95% upper | W–T–L | work/move |
| --- | ---: | ---: | ---: | ---: | :---: | ---: |
| **d4 (7,2)** vs d4 (7,1) | **356,548** | **−41,950** | −100,137 | +17,541 | 28–0–36 | 20,178,327 |
| **d3 (7,12)** vs d3 (7,6) | **349,345** | **−27,097** | −83,807 | +31,209 | 28–0–36 | 13,506,434 |

Neither difference clears zero, so neither is a demonstrated *loss*. But both
point estimates are negative, both cost 3.2–4.1× the work of their comparator,
and in both arms score, moves, numbered clears and covered reveals all move
together in the negative direction — the same four-quantity co-movement that
§5 used as evidence *for* the positive result, here pointing the other way.

## 11. CHECK gate, re-run before any gameplay

The binary was rebuilt with `clang++` (the Makefile's `CXX ?= clang++` loses to
make's builtin `CXX=g++`, which trips a false `-Werror=array-bounds`) and the
whole §3 gate was re-run. The frozen source hashes written to
`build/reveal-sampling/sources.sha256` are byte-identical to those recorded at
the end of this document, so the oracles are the same code the original session
compared against.

| gate | oracle | configuration | moves compared | action mismatches | logical-work mismatches |
| --- | --- | --- | ---: | ---: | ---: |
| A | frozen `ref::chooseDepth4Action` | N=5, M=1, depth 4, work 3,200,000 | 50 | **0** | n/a |
| B1 | single-knob, `chance_samples`=5 | N=5, M=1, depth 3 | 222 | **0** | **0** |
| B2 | single-knob, `chance_samples`=7 | N=7, M=1, depth 3 | 220 | **0** | **0** |
| B3 | single-knob, `chance_samples`=5 | N=5, M=1, depth 4 | 90 | **0** | **0** |
| B4 | single-knob, `chance_samples`=7 | N=7, M=1, depth 4 | 30 | **0** | **0** |

Identical counts to §3, all five `CHECK OK`. Logs:
`runs/RUN-A525-reveal/cont-gate*.log`.

## 12. Chunked execution, and why pooling is not a new statistical object

The previous attempt at d4 (7,2) was killed after about an hour with no games
written, because the artifact is only serialized when the whole cohort
finishes. Both arms here were instead run as **four sequential 16-game chunks**
at seed starts `0xa51d1000`, `+0x10`, `+0x20`, `+0x30`, launched detached with
`setsid nohup` so they survive the controlling session, with per-game progress
to a log. `runCohort` maps game index *i* to seed `seedStart + i`, so the four
chunks play exactly the 64 cohort seeds, once each.

Whole games are the statistical unit and each game is a deterministic function
of its seed and the policy, so a pooled 64-game cohort is the same object as a
single 64-game run. That is asserted rather than assumed, and it was checked:

| check | result |
| --- | --- |
| d3 (5,1) run as 4×16 chunks at 1 thread, pooled, vs the existing 64-game 12-thread artifact | **64 games, 0 field mismatches**, total logical work 312,966,881 = 312,966,881 |
| recursive numeric comparison of all 17 aggregate and per-game field groups (tolerance 1e-11 relative) | **only per-game `wallSeconds` differs** — 64 of 64, every other value equal |
| pooled bound diagnostics vs single-run | `decisions` 5,750 = 5,750, `belowTargetDepth` 0 = 0, `workLimitEvents` 0 = 0, `minCompletedDepth` 3 = 3, `maxDecisionWork` 85,085 = 85,085 |

`pool.py` recomputes every aggregate with the same formulas as
`harness.hpp:writeArtifact`, including a Python reimplementation of the
`Mulberry32` percentile bootstrap, and combines chunk diagnostics
conservatively (counts add, `minCompletedDepth` takes the minimum,
`maxDecisionWork` the maximum). This also re-confirms worker-count
independence: 1-thread and 12-thread runs produce identical per-game records.

## 13. The work bound, computed per configuration and verified never to bind

Both arms passed `--max-work` explicitly, taken from the binary's own
`--work-bound` (the frozen arithmetic with `b = 7·N·M`). Leaving a bound sized
for a smaller branching factor does not error — it silently completes a
shallower depth — so this is load-bearing.

| arm | b = 7·N·M | worst-case work | `--max-work` used | worst-case cache entries | cache used |
| --- | ---: | ---: | ---: | ---: | ---: |
| d4, N=7, M=2 | 98 | 187,336,114 | 187,336,114 | 960,694 | 960,695 |
| d3, N=7, M=12 | 588 | 407,634,528 | 407,634,528 | 346,920 | 346,921 |

**Evidence the bound never bound.** Every decision records its completed depth;
the binary counts decisions finishing below the target depth and work-limit
events, and reports the busiest single decision.

| arm | decisions | below target depth | work-limit events | min completed depth | max work in one decision | bound | headroom | score-identity failures | censored |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| d4, N=7, M=2 | 6,633 | **0** | **0** | **4** | 81,686,570 | 187,336,114 | 2.29× | 0 | 0 |
| d3, N=7, M=12 | 6,523 | **0** | **0** | **3** | 190,214,472 | 407,634,528 | 2.14× | 0 | 0 |

Per chunk the minimum completed depth is 4, 4, 4, 4 for the depth-4 arm and
3, 3, 3, 3 for the depth-3 arm, and the busiest decision in each chunk is
73.1M / 76.7M / 81.7M / 71.4M and 97.3M / 128.4M / 190.2M / 135.7M. No decision
in 13,156 completed below its target depth; the busiest used 44% and 47% of its
bound. Both arms are genuine depth-4 and depth-3 results.

## 14. The arms

Shared evaluation cohort `0xa51d1000`–`0xa51d103f`, 64 paired games, 2,000-move
cap, corrected 17,000-point Hardcore scoring.

| arm | depth | N | M | mean | median | Q25 | min | max | sd | moves | moves Q25 | cens | clears/mv | reveals/mv | occupied | work/mv | ≥1M |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| d3 (7,1) | 3 | 7 | 1 | 312,327 | 267,279 | 175,287 | 85,589 | 846,544 | 177,026 | 92.27 | 55.00 | 0 | 1.9849 | 1.1001 | 23.88 | 156,834 | 0 |
| d3 (7,3) | 3 | 7 | 3 | 337,306 | 285,023 | 196,045 | 103,180 | 931,090 | 208,038 | 98.70 | 60.00 | 0 | 2.0033 | 1.1111 | 23.81 | 1,045,719 | 0 |
| **d3 (7,6)** | 3 | 7 | 6 | **376,442** | 322,859 | 227,224 | 103,015 | 1,002,557 | 223,366 | **109.45** | 70.00 | 0 | **2.0447** | **1.1423** | 23.49 | 4,244,020 | 1 |
| **d3 (7,12)** *(new)* | 3 | 7 | 12 | **349,345** | 258,855 | 186,866 | 103,319 | 1,216,867 | 254,059 | 101.92 | 55.75 | 0 | 2.0231 | 1.1309 | **23.39** | 13,506,434 | 3 |
| d4 (5,1) frozen | 4 | 5 | 1 | 297,327 | 260,415 | 192,352 | 86,935 | 836,427 | 150,550 | 87.16 | 58.75 | 0 | 1.9489 | 1.0697 | 24.29 | 1,296,034 | 0 |
| **d4 (7,1)** | 4 | 7 | 1 | **398,498** | 344,630 | 212,864 | 103,331 | 1,341,287 | 254,414 | **114.66** | 65.00 | 0 | **2.0571** | **1.1549** | 23.15 | 4,956,614 | 2 |
| **d4 (7,2)** *(new)* | 4 | 7 | 2 | **356,548** | 305,167 | 192,347 | 103,348 | 1,064,716 | 219,723 | 103.64 | 60.00 | 0 | 2.0306 | 1.1358 | 23.35 | 20,178,327 | 2 |

### Paired deltas over whole games

One-sided 95% bounds by percentile bootstrap, 20,000 resamples, RNG seed
`0xb0075eed`, resampling whole games. For a null or negative result the
**upper** bound is the informative direction, so both are given.

| comparison | Δ score | 95% lower | 95% upper | Δ moves | Δ clears/mv | Δ reveals/mv | Δ occupied | W–T–L |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :---: |
| **d4 (7,2) − d4 (7,1) — does M compound with the 4th ply?** | **−41,950** | −100,137 | **+17,541** | −11.02 | −0.0265 | −0.0191 | +0.20 | 28–0–36 |
| **d3 (7,12) − d3 (7,6) — the M=6→12 step** | **−27,097** | −83,807 | **+31,209** | −7.53 | −0.0215 | −0.0114 | −0.09 | 28–0–36 |
| d3 (7,12) − d3 (7,3) | +12,039 | −50,119 | +74,688 | +3.22 | +0.0198 | +0.0198 | −0.42 | 30–0–34 |
| d3 (7,12) − d3 (7,1) | +37,019 | −25,076 | +102,426 | +9.66 | +0.0382 | +0.0308 | −0.49 | 30–0–34 |
| d3 (7,12) − d4 (5,1) frozen | +52,018 | −4,998 | +112,368 | +14.77 | +0.0742 | +0.0612 | −0.89 | 32–0–32 |
| **d4 (7,2) − d4 (5,1) frozen** | **+59,221** | **+9,134** | +111,812 | +16.48 | +0.0817 | +0.0661 | −0.94 | 37–0–27 |
| d3 (7,12) − d4 (7,1) | −49,153 | −125,029 | +27,828 | −12.73 | −0.0340 | −0.0240 | +0.25 | 22–0–42 |
| d4 (7,2) − d3 (7,6) | −19,894 | −76,456 | +36,846 | −5.81 | −0.0141 | −0.0065 | −0.14 | 37–0–27 |
| d4 (7,2) − d3 (7,12) | +7,203 | −56,004 | +69,691 | +1.72 | +0.0075 | +0.0049 | −0.04 | 40–0–24 |

**Stability across cohort halves.** No fold was preregistered, so this is a
post-hoc diagnostic, not a gate. Both headline deltas keep their sign and their
losing win–loss split in both halves; the magnitudes differ substantially,
which is what a 64-game cohort with sd ≈ 60% of the mean predicts.

| comparison | half | n | Δ score | 95% lower | W–T–L |
| --- | --- | ---: | ---: | ---: | :---: |
| d4 (7,2) − d4 (7,1) | first 32 | 32 | −11,082 | −94,711 | 14–0–18 |
| d4 (7,2) − d4 (7,1) | last 32 | 32 | −72,818 | −156,151 | 14–0–18 |
| d3 (7,12) − d3 (7,6) | first 32 | 32 | −4,495 | −91,425 | 15–0–17 |
| d3 (7,12) − d3 (7,6) | last 32 | 32 | −49,698 | −120,167 | 13–0–19 |

## 15. Arm 1 — the axes substitute, they do not compound

The question §8 left open was whether the fourth ply and chance-node
decorrelation are *exchangeable* (§5's finding) or *additive*. Adding the
cheapest useful reveal decorrelation on top of the fourth ply gives
**−41,950 points at 4.07× the work**, with a 95% upper bound of **+17,541**.

That upper bound is the useful number. Had the axes compounded even weakly —
say by half of the +64,116 the same knob is worth at depth 3 — the data would
have had to show it; instead the 95% one-sided ceiling on any compounding gain
is about +17.5k, roughly a quarter of the depth-3 effect, at 4× the cost. **The
axes substitute in the strong sense.** The remaining question §8 posed, the
exchange rate, is answered by §7 and unchanged: at matched work d3 (7,6) and
d4 (7,1) are a tie and the depth-3 arm is 14% cheaper.

Two things make this more than a null. First, every flow quantity moves with
the score: −11.02 moves, −0.0265 clears/move, −0.0191 reveals/move, and mean
occupancy *rises* 0.20 cells — the exact reverse of the co-movement §6 used to
argue the depth-3 effect was mechanical. Second, the sign is stable across both
cohort halves. The straightforward reading is that once the disc marginal is
exact and a fourth ply is present, the (disc, reveal) joint is no longer the
binding error, and paying 4× for it buys nothing.

The pilot in §10 projected the cost well: 3.59× predicted logical work against
4.07× measured, versus a 15.75× worst-case bound ratio.

## 16. Arm 2 — coverage saturation and flow-rate saturation do not coincide

d3 (7,12) is the configuration at which the (disc, reveal) joint reaches
**100% coverage**. If the §5 dose-response were driven by that coverage, this
is where the curve should top out. It does not top out here — it turns over
one step earlier:

| depth-3 arm | (disc, reveal) joint coverage | mean score | mean moves | clears/mv | reveals/mv | occupied |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| N=7, M=1 | 14.3% | 312,327 | 92.27 | 1.9849 | 1.1001 | 23.88 |
| N=7, M=3 | 42.9% | 337,306 | 98.70 | 2.0033 | 1.1111 | 23.81 |
| **N=7, M=6** | 85.7% | **376,442** | **109.45** | **2.0447** | **1.1423** | 23.49 |
| N=7, M=12 | **100.0%** | 349,345 | 101.92 | 2.0231 | 1.1309 | **23.39** |

**Answer: no, coverage saturation does not match flow-rate saturation.** Score,
moves, numbered clears and covered reveals all peak at M = 6 with 85.7%
coverage and fall back when the remaining 14.3% of joint atoms are filled in.
Only **mean occupancy** keeps improving monotonically all the way to M = 12
(23.88 → 23.81 → 23.49 → 23.39), which is the one quantity that tracks coverage
across all four points.

The M=6→12 step is −27,097 with a 95% interval of (−83,807, +31,209) and
28–0–36, so this is a **saturation result, not a demonstrated regression**: the
data are consistent with the curve being flat from M = 6 onward, and are not
consistent with it continuing to climb at the rate M = 1→6 showed. The
strongest defensible statement is that **the reveal axis is exhausted by
M ≈ 6**, and that filling the last of the joint grid buys at most +31k and
plausibly nothing.

This weakens, without refuting, §2's attribution. The mechanism table predicted
strength should track the (disc, reveal) column; it does over M ∈ {1, 3, 6} and
then stops. Three readings are consistent with the data and this experiment
does not separate them: (a) the effect saturates once *most* of the joint has
weight and the last atoms are redundant; (b) the gain over M ∈ {1,3,6} was
partly the extra scenario count acting as variance reduction rather than
coverage per se, and that too saturates; (c) 64 games cannot resolve steps this
size and the whole M ≥ 3 region is one plateau. Reading (c) deserves weight:
the four M values span 312k–376k, and *no adjacent step in the sweep is
individually significant*.

## 17. Work, and the shape of the frontier now

| arm | work/move | relative to frozen d4 | mean score | score per unit work |
| --- | ---: | ---: | ---: | ---: |
| d3 (7,1) | 156,834 | 0.121× | 312,327 | 1.991 |
| d3 (7,3) | 1,045,719 | 0.807× | 337,306 | 0.323 |
| d4 (5,1) *(frozen)* | 1,296,034 | 1.000× | 297,327 | 0.229 |
| d3 (7,6) | 4,244,020 | 3.275× | **376,442** | 0.089 |
| d4 (7,1) | 4,956,614 | 3.824× | **398,498** | 0.080 |
| d3 (7,12) | 13,506,434 | 10.421× | 349,345 | 0.026 |
| d4 (7,2) | 20,178,327 | 15.569× | 356,548 | 0.018 |

Measured work ratios again land far below their bounds because the
transposition cache collapses duplicate reveal scenarios: 3.18× measured for
M=6→12 against a 7.98× bound ratio, 4.07× for d4 M=1→2 against 15.75×.

**The frontier has a knee and both new arms are past it.** Everything above
roughly 4–5 million work per move is indistinguishable from d4 (7,1) or worse,
while costing up to 4× more. The two best-value points in this study remain
d3 (7,1) at 156,834 work per move and, if strength is the objective,
d4 (7,1) / d3 (7,6) at ~4–5 million. Nothing here moves the ceiling: the best
arm on this cohort is still 398,498 against the 1,050,000 the frozen protocol
requires.

## 18. Verdict, and what it revises above

- **Validity: valid.** Both arms passed the full CHECK gate on the rebuilt
  binary with identical counts to §3, on unmodified frozen sources with matching
  hashes. Work bounds were computed per configuration, passed explicitly, and
  verified never to bind (0 of 13,156 decisions below target depth, 0 work-limit
  events, busiest decision at 44–47% of bound). 0 censored games and 0
  score-decomposition identity violations in both arms. The chunk-and-pool
  procedure was validated against an existing single-process 64-game artifact
  before being used.
- **Outcome: two informative negatives.** Neither arm produced a significant
  loss and neither produced a gain. Both were run to answer a question whose
  either-way answer was worth having, and both answered it.
- **Evidence tier: `development`,** on the same previously-read cohort as
  everything above. No protected or final seed was opened and none is justified.

**What this revises in §8.**

1. **§8's open question is closed: the axes substitute, they do not compound.**
   The 95% ceiling on any compounding gain from M = 2 on top of depth 4 is
   +17,541 points at 4.07× the work. The symmetric statement in §8 — that depth
   and chance-node decorrelation are two ways of buying the same thing — is
   supported and now has its complement: **you cannot buy it twice.**
2. **The dose-response in §5 is not monotone beyond M = 6; it peaks there.**
   §5's claim rested explicitly on "the monotone ordering of five quantities
   across three settings". Extending it to a fourth setting breaks the
   monotonicity for four of those five (occupancy is the exception). The
   positive result at M = 6 stands on its own paired test against M = 1
   (+64,116, lower bound +7,475) and against the frozen reference (+79,115,
   lower bound +30,242), both unchanged — but **the dose-response argument that
   was offered as corroboration no longer extends**, and §5's framing should be
   read as "M = 6 is a local optimum" rather than "more M is better".
3. **§2's mechanism attribution is weakened.** Joint coverage was offered as the
   quantity strength tracks. It reaches 100% at M = 12 and strength is lower
   there than at 85.7%. Coverage may still be the right variable with a
   saturating rather than linear response, but this experiment cannot separate
   that from the alternative that the M ≥ 3 region is one noise-limited
   plateau.
4. **§10's remaining agenda is now empty.** Both arms it recorded as
   unfinished are complete and recorded here.

## 19. Limitations of this continuation

1. **Neither headline delta is significant.** Both are negative point estimates
   with intervals spanning zero. The claims made are the bounded ones — an upper
   bound on compounding, and saturation rather than regression — and nothing
   stronger is supported.
2. **64 paired games, sd 62–73% of the mean in the new arms.** A separate
   session has shown a small cohort badly misstating a different quantity here
   (an 8-tape comparison put fair D4 at 117.75 mean moves against 93.78 on 160
   tapes). These comparisons are paired on a fixed 64-seed cohort, so they are
   not exposed to that particular failure, but they are exposed to ordinary
   64-game noise and the half-split shows magnitudes moving by 4–6× between
   halves.
3. **One cohort, already read.** The same permanently-development seeds as
   everything above. Nothing here can be confirmation evidence.
4. **Only M = 2 was tested at depth 4.** A larger M at depth 4 is not ruled out
   by this arm, only made unattractive: M = 2 already costs 4× and its ceiling
   is +17.5k. The d3 sweep needed M ≥ 6 to show any effect, and depth-4 M = 6
   would cost roughly 4,500,000,000 worst-case work per decision.
5. **The reveal-by-reveal joint remains untouched.** As §2 notes, all reveal
   events within one scenario still share the sample index. A design giving each
   scenario an independent reveal stream is still untested, and the saturation
   in §16 is a fact about the *(disc, reveal)* axis only.
6. **Wall times are provenance, not performance.** Arm 1 ran across a machine
   whose load average moved between 5 and 72 driven by other contributors, at
   8 threads for one chunk and 12 for the rest; only work per move is comparable.
7. **A model contribution record under `research/contributions/` is owed and was
   not written,** the same open debt §9 records, because this work was scoped to
   the reveal-sampling namespace and `docs/exploratory/`.

## Reproduce (continuation)

```sh
./approaches/lifetime-objective/reveal-sampling/build.sh   # clang++
B=./build/reveal-sampling/reveal-sampling
R=runs/RUN-A525-reveal

# CHECK gate, as §3
$B --parity --seed-start 0xa5250000 --parity-games 2 --parity-moves 25
$B --gate --depth 3 --disc-samples 5 --max-work  3200000 --seed-start 0xa5250010 --parity-games 4 --parity-moves 60
$B --gate --depth 3 --disc-samples 7 --max-work 16000000 --seed-start 0xa5250010 --parity-games 4 --parity-moves 60
$B --gate --depth 4 --disc-samples 5 --max-work  3200000 --seed-start 0xa5250010 --parity-games 3 --parity-moves 30
$B --gate --depth 4 --disc-samples 7 --max-work 16000000 --seed-start 0xa5250010 --parity-games 2 --parity-moves 15

# work bounds; --max-work is mandatory for a gameplay cohort
$B --work-bound --depth 4 --disc-samples 7 --reveal-samples 2    # 187,336,114
$B --work-bound --depth 3 --disc-samples 7 --reveal-samples 12   # 407,634,528

# the chunk-and-pool procedure, validated against an existing 64-game artifact
for c in 0 1 2 3; do
  printf -v s '0x%x' $((0xa51d1000 + c*16))
  $B --depth 3 --disc-samples 5 --reveal-samples 1 --max-work 3200000 \
     --seed-start $s --games 16 --threads 1 --quiet --output $R/cont-pooltest-c$c.json
done
python3 approaches/lifetime-objective/reveal-sampling/pool.py \
  $R/cont-pooltest-pooled.json $R/cont-pooltest-c{0,1,2,3}.json
python3 approaches/lifetime-objective/reveal-sampling/compare.py identity \
  $R/cont-pooltest-pooled.json $R/d3-n5-m1.json

# the arms, detached and chunked (CROWDED_THREADS bounds the shared-machine
# backoff: wait up to 15 min for load <= 26, then run at that many threads)
setsid nohup ./approaches/lifetime-objective/reveal-sampling/run-arms.sh arm1 \
  >> $R/cont-arm1-driver.log 2>&1 &      # d4 N=7 M=2
CROWDED_THREADS=12 setsid nohup ./approaches/lifetime-objective/reveal-sampling/run-arms.sh arm2 \
  >> $R/cont-arm2-driver.log 2>&1 &      # d3 N=7 M=12

python3 approaches/lifetime-objective/reveal-sampling/pool.py $R/d4-n7-m2.json  $R/d4-n7-m2-c{0,1,2,3}.json
python3 approaches/lifetime-objective/reveal-sampling/pool.py $R/d3-n7-m12.json $R/d3-n7-m12-c{0,1,2,3}.json

# tables
S=approaches/lifetime-objective/reveal-sampling/stats.py
python3 $S rows "d3 (7,1)=$R/d3-n7-m1.json" "d3 (7,3)=$R/d3-n7-m3.json" \
  "d3 (7,6)=$R/d3-n7-m6.json" "d3 (7,12)=$R/d3-n7-m12.json" \
  "d4 (5,1)=runs/RUN-A51D-s7confirm/fresh-s5.json" \
  "d4 (7,1)=runs/RUN-A51D-s7confirm/fresh-s7.json" "d4 (7,2)=$R/d4-n7-m2.json"
python3 $S delta  "d4 (7,2)=$R/d4-n7-m2.json" "d4 (7,1)=runs/RUN-A51D-s7confirm/fresh-s7.json" \
                  "d3 (7,12)=$R/d3-n7-m12.json" "d3 (7,6)=$R/d3-n7-m6.json"
python3 $S halves "d4 (7,2)=$R/d4-n7-m2.json" "d4 (7,1)=runs/RUN-A51D-s7confirm/fresh-s7.json" \
                  "d3 (7,12)=$R/d3-n7-m12.json" "d3 (7,6)=$R/d3-n7-m6.json"
```

### Environment (continuation)

Same host as above: AMD Ryzen AI Max+ 395 (16 physical / 32 logical cores),
125 GiB RAM, AMD clang 23.0.0git, `-O3 -std=c++20 -pthread -Wall -Wextra`.
Shared with other contributors' jobs throughout; 1-minute load average ranged
from 5 to 72, at most 12 threads used by this work. Arm 1 took 36,813 s of
wall across its four chunks (one chunk at 8 threads during a load-50 period,
the rest at 12); arm 2 took 16,470 s at 12 threads. Frozen source hashes in
`build/reveal-sampling/sources.sha256` are unchanged from the list above.
