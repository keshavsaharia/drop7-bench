# Finding 12 — The eight-tape cohort was unrepresentative: a re-baselined fair-planner ceiling

**Status:** exploratory, evidence tier `development`/`pilot`. Built and measured
in this checkout on 2026-08-20. **Extends
[`finding-07`](finding-07-fair-planning-ceiling.md); nothing in it is rewritten.**
**Namespace:** `approaches/lifetime-objective/flow-ceiling/`, run
`runs/RUN-FLOW-044da902f8e8/`, leases `SEEDLEASE-A52-FLOW` =
`0xa5230000`–`0xa5233fff` (the original eight master tapes, reused so every
cohort stays paired), `SEEDLEASE-A52-FLOW2` = `0xa5234000`–`0xa5237fff` (the
existing K series' sampler stream, continued here so the series stays a series)
and `SEEDLEASE-A52-FLOW3` = `0xa5238000`–`0xa523bfff` (everything new).
**Nothing in `docs/research/`, `artifacts/`, `research/`, `runs/RUN-20260820T*`,
or any existing approach source was modified.** Files were created or edited
only under `approaches/lifetime-objective/flow-ceiling/`, `build/flow-ceiling/`,
`runs/RUN-FLOW-*/` and `docs/exploratory/`.

## Headline

Two findings, in the order they matter.

**1. The eight master tapes used throughout findings 06 and 07 favour long
games, and every absolute figure measured on them is inflated for any policy
that survives a while.** Fair depth 4 — whose configuration never changed —
scores **93.56 mean moves on 128 fresh tapes against 117.75 on the eight**, a
26% inflation. The fresh-tape figure lands on
[`finding-01`](finding-01-score-is-survival.md)'s independent base-engine
measurement of 94.06 moves over 64 games, and on a separate agent's 93.78 over
160 tapes, so the fresh-tape number is the representative one and the eight-tape
number is not. **The bias scales with lifetime**: lowest-column, which dies at
about 32 moves, moves only −3.7% between cohorts, while fair D4 moves −20.5%.
This affects `finding-07`'s central quantitative claim and it is corrected in
§2 below.

**2. The fair planner's K series turns over.** `finding-07` reported the series
"still climbing" at K = 256 and could not identify an asymptote. K = 1024 is
**worse**, not better, on five of six paired tapes. There is an interior optimum
near K = 256, not an asymptote approaching 2.400. Combined with (1), **the
number the distillation agent needs is a ceiling of roughly 2.03 clears per move
and ~95 moves** — not 2.23, and not 2.40. §3 and §9.

Third, and reassuringly: **finding-06's clairvoyant ceiling is robust to the
cohort.** The clairvoyant planner is capped rather than tape-limited, so an easy
tape draw has no room to inflate it. On fresh tapes it measures **2.3663 clears
per move at H = 7 (64 tapes) and 2.3655 at H = 9 (32 tapes), against finding-06's
2.3601 and 2.3663** — agreement to within 0.001 — with 59 of 64 and 31 of 32
games censored alive. §6. The ceiling the programme is oriented around needs no
caveat.

## Why this exists

`finding-07` measured a legal (public-information) receding-horizon planner
reaching **2.2309 clears per move** at H = 7, K = 256, and reported that the K
series **had not saturated**: the last observed gain was +0.091 and the fit
could not identify an asymptote. That asymptote is the number that matters
operationally, because **the fair planner's sustained flow rate is an upper
bound on what any policy distilled from it can achieve.** A separate agent is
distilling this planner now. If the ceiling is 2.25 that student has almost
nowhere to go; if it is 2.40 the survival programme is open.

Four questions, in the priority the coordinator set:

1. what does H = 7, K = 1024 give, and what does the series extrapolate to;
2. is H = 9 reachable at a K where the answer would not be misleading;
3. where does the remaining clairvoyant-minus-fair shortfall live;
4. how large is the reveal-blindness effect diagnosed in `finding-07` §6.

## Method changes

Three additions to `approaches/lifetime-objective/flow-ceiling/`, all gated.

**Sample-level parallelism** (`fair-planner.hpp`). The K sampled windows of one
decision are now solved on a thread pool. The K scenarios are still **drawn
serially** from the sampler stream before any of them is solved, so the sequence
of sampled worlds — and therefore the decision — is bit-identical to a
single-threaded run at the same seed. Verified two ways: `--sample-threads 1`
and `--sample-threads 5` produce identical games, and re-running H = 7, K = 64
on the new binary reproduces `finding-07`'s recorded per-seed lines **exactly**
(seed `a5230000`: 215 moves / 750,636 points / 2.2000 clears per move; seed
`a5230001`: 100 / 342,532 / 2.0400).

**Warm-started measurement** (`--warm-moves M`, `--warm-horizon`,
`--warm-samples`). The first M moves are played with a warm-up configuration and
**excluded from every statistic**; the measured segment then runs with the main
configuration. Two arms sharing a warm-up and a tape reach a bit-identical state
before they diverge, which is what makes an expensive horizon comparison
affordable: it buys mid-game states without paying for the sparse opening.
Verified by setting the warm configuration equal to the main configuration,
which reproduces the plain game exactly with M fewer measured moves.

**Per-move cover accounting.** `moveCovered` and `moveRevealed` are now recorded
per move, because reveals — unlike clears — cannot be reconstructed from the
occupancy trace: opening a cover changes a cell's kind, not the cell count. The
two deterministic cohorts (`fair-d4`, clairvoyant `rh-clears` H = 7) were re-run
on the new binary to obtain them and reproduce their originals line for line.

All seven `--self-test` gates still pass, including the information-boundary
gate, and `--cross-check` over `suite-h9-v1` still reports **0 mismatches
against the frozen exact solver** (128 scenarios, H = 9).

## 2. Re-baselining: what the eight tapes did

A master tape is a whole fixed future: a start position, one numbered disc per
move index, and one hidden row per rise index. Findings 06 and 07 used eight of
them, seeds `0xa5230000`–`0xa5230007`, and disclosed the small cohort as a
limitation. What was not knowable then is the *size* of the error. It is now.

128 fresh tapes were drawn from `SEEDLEASE-A52-FLOW3` (`0xa5239000`+) and every
policy re-run on them at the same 400-move cap.

| policy | cohort | games | mean moves | mean score | clears/move | reveals/move | occupancy slope |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| lowest column | eight original tapes | 8 | 33.12 | 97,110 | 1.1774 | 0.5019 | +4.470 |
| lowest column | **128 fresh tapes** | 128 | **31.89** | **94,536** | **1.0436** | **0.3902** | **+5.554** |
| fair depth 4 | eight original tapes | 8 | 117.75 | 409,985 | 2.0467 | 1.1423 | +0.990 |
| **fair depth 4** | **128 fresh tapes** | 128 | **93.56** | **319,474** | **1.9865** | **1.1050** | **+1.481** |
| clairvoyant `rh-clears` H = 7 | eight original tapes | 8 | 366.25 | 1,422,001 | 2.3601 | 1.3785 | +0.014 |
| **clairvoyant `rh-clears` H = 7** | **64 fresh tapes** | 64 | **386.48** | **1,485,783** | **2.3663** | **1.3832** | **+0.075** |

Three independent measurements now agree on fair depth 4's lifetime: **93.56**
here on 128 fresh tapes, **94.06** in `finding-01` on 64 base-engine games with a
different engine and a different lease, and **93.78** from the distillation
agent on 160 tapes. The eight-tape 117.75 is the outlier.

**The bias is a lifetime bias, and that is diagnostic.** Lowest column loses
3.7% of its mean lifetime moving to the fresh cohort; fair D4 loses 20.5%; the
clairvoyant planner, which is censored at the move cap rather than limited by
the tape, does not lose at all — it *gains* 5.5%, from 366.25 to 386.48 moves,
with 59 of 64 fresh games censored alive against 6 of 8 before.

| policy | mean lifetime, 8 tapes | mean lifetime, fresh | change | clears/move change |
| --- | ---: | ---: | ---: | ---: |
| lowest column (dies ~32) | 33.12 | 31.89 | **−3.7%** | −0.134 |
| fair depth 4 (dies ~94) | 117.75 | 93.56 | **−20.5%** | −0.060 |
| clairvoyant (censored at the cap) | 366.25 | 386.48 | **+5.5%** | **+0.006** |

The eight tapes are a set on which games run long, so they inflate a policy in
proportion to how much room that policy had to run — which inflates a
survival-seeking planner's margin more than it inflates a comparator that dies
early, and does not inflate a planner that was hitting the move cap either way.
That is exactly the mechanism the coordinator hypothesised, and the monotone
ordering across three policies of very different lifetimes confirms it.

**The clairvoyant ceiling is therefore not affected.** 2.3663 clears and 1.3832
reveals per move on 64 fresh tapes against 2.3601 and 1.3785 on the eight is a
shift of +0.006 and +0.005 — an order of magnitude smaller than fair D4's, and
in the opposite direction. `finding-06`'s headline needs no quantitative caveat.
§6 extends this check to H = 9, the horizon its headline was actually measured
at.

**But the gap being decomposed got bigger, not smaller.** On the eight tapes,
clairvoyant minus fair D4 at H = 7 was 2.3601 − 2.0467 = **0.3134**. On fresh
tapes it is 2.3663 − 1.9865 = **0.3798**, 21% larger, because D4 fell and the
clairvoyant planner did not. Any "share of the gap closed" percentage from
`finding-07` is therefore computed against a denominator that was too small as
well as a numerator that was too large.

### The paired re-baselined comparison

Whole games are the statistical unit. Both arms played the same fresh master
tapes at the same move cap; the bootstrap resamples *tapes* and recomputes both
arms from the same resampled multiset, so pairing is never broken. The bound
reported is a **one-sided 95% bootstrap lower bound** on the paired delta.

**Fair planner at H = 5, K = 256 — the configuration the distillation agent is
using as its teacher — against fair depth 4, 128 fresh tapes:**

| metric | fair planner | fair D4 | delta | 95% lower bound | verdict |
| --- | ---: | ---: | ---: | ---: | --- |
| mean moves | 98.29 | 93.56 | +4.73 | **−5.96** | straddles zero |
| mean score | 334,813 | 319,474 | +15,339 | **−23,539** | straddles zero |
| **clears/move (pooled)** | **2.0445** | **1.9865** | **+0.0580** | **+0.0157** | **advantage established** |
| **reveals/move (pooled)** | **1.1419** | **1.1050** | **+0.0368** | **+0.0055** | **advantage established** |
| occupancy slope | +1.3753 | +1.4806 | −0.1054 | −0.3069 | straddles zero |
| censored at the cap | 0 / 128 | 0 / 128 | — | — | — |
| per-tape lifetime record | | | **58 win / 19 tie / 51 loss** | | |

This is an independent replication of the distillation agent's numbers on a
different lease and a different tape count: they measured 2.0471 clears per move
and 98.96 mean moves over 160 tapes, against my 2.0445 and 98.29 over 128, and
both of us find the lifetime delta straddling zero (+5.18 with a lower bound of
−4.56 there, +4.73 with −5.96 here).

**The correct reading is narrower than either "it works" or "it doesn't".** The
flow-rate advantage is real and survives the bound: the planner clears 0.058
more discs per move and reveals 0.037 more covers per move than fair D4, and
both lower bounds are above zero. What does **not** survive is the claim that
this converts into lifetime or score: those deltas straddle zero, and the
planner wins the lifetime race on only 58 of 128 tapes. A flow-rate edge of
0.058 is 2.4% of the requirement, which moves the occupancy drift from +1.481
to +1.375 cells per cycle — still overwhelmingly positive, still a board that
fills, still a game that ends at about the same time.

**Fair planner at H = 7, K = 256 — this work's headline configuration —
against fair depth 4, 32 fresh tapes** (the arm is expensive; 32 tapes was what
the budget allowed, and D4's mean on this particular subset is 78.47 rather than
its 93.56 over all 128, so only the paired deltas should be read across):

| metric | fair planner | fair D4 | delta | 95% lower bound | verdict |
| --- | ---: | ---: | ---: | ---: | --- |
| **mean moves** | **97.28** | **78.47** | **+18.81** | **+0.56** | **advantage established** |
| mean score | 331,176 | 264,811 | +66,365 | −505 | straddles zero, barely |
| **clears/move (pooled)** | **2.0260** | **1.9004** | **+0.1256** | **+0.0473** | **advantage established** |
| **reveals/move (pooled)** | **1.1256** | **1.0339** | **+0.0918** | **+0.0328** | **advantage established** |
| **occupancy slope** *(lower is better)* | **+1.3063** | **+1.7401** | **−0.4338** | upper bound −0.0509 | **advantage established** |
| censored at the cap | 0 / 32 | 0 / 32 | — | — | — |
| per-tape lifetime record | | | **18 win / 5 tie / 9 loss** | | |

So **at H = 7 the fair planner's advantage does survive re-baselining** — flow
rate, reveal rate, occupancy slope and even lifetime all have lower bounds above
zero — but it is far smaller than the eight tapes suggested, and mean score
still straddles zero. At H = 5, the configuration actually being distilled, only
the flow rates survive.

### The corrected gap decomposition

All four arms restricted to the 32 fresh tapes on which all four were run, so
the comparison is fully matched:

| arm | clears/move | mean moves |
| --- | ---: | ---: |
| fair depth 4 | 1.9004 | 78.47 |
| fair planner H = 5, K = 256 | 2.0031 | 91.06 |
| fair planner H = 7, K = 256 | 2.0260 | 97.28 |
| clairvoyant `rh-clears` H = 7 | 2.3637 | 379.69 |

| component | value | share of the gap |
| --- | ---: | ---: |
| total gap, clairvoyant − fair D4 | 0.4633 | 100% |
| closed by fair planning at H = 5, K = 256 | +0.1027 | **22.2%** |
| **closed by fair planning at H = 7, K = 256** | **+0.1256** | **27.1%** |
| **not closed** | **0.3377** | **72.9%** |

`finding-07` reported 40.0% and 58.8% for these same two configurations. **The
corrected figures are roughly half: 22.2% and 27.1%.** The direction of that
document's headline — "at least 59% planning, at most 41% information" — is
reversed by the correction: on a representative cohort **at most about 27% of
the gap is recovered by fair planning of this kind, and at least 73% is not.**

### What must be weakened in finding-07

`finding-07` is not withdrawn: its method, its information-boundary gate, its
arm A result (the future disc tape is worth nothing) and its qualitative
conclusion all stand. Three specific quantitative claims must be weakened, and
they are listed here rather than edited into that document.

| claim in finding-07 | status |
| --- | --- |
| "The best legal planner sustains **2.2309 clears and 1.2782 reveals per move**" | **cohort-inflated.** That is an eight-tape figure at H = 7. On fresh tapes the same configuration measures **2.0260 clears and 1.1256 reveals per move** on 32 fresh tapes, and the H = 5 configuration falls from 2.1680 to 2.0445. |
| "**Fair planning closes 58.8%** of the clairvoyant-minus-D4 gap" (40.0% at H = 5) | **withdrawn as stated.** The numerator and the denominator were both wrong: D4's baseline was inflated and the clairvoyant ceiling was not, so the true gap is 21% larger. At H = 5 on fresh tapes the share closed is 0.0580 / 0.3798 = **15.3%**, not 40.0%. At H = 7 on the matched fresh cohort it is **27.1%**, not 58.8%. |
| "best mean lifetime **200.88 moves** against the clairvoyant's 366.25" | **cohort-inflated on the fair side only.** The clairvoyant figure is robust (386.48 on fresh tapes); the fair figure is not. |
| "at least 59% planning, at most 41% information" | **withdrawn.** With the corrected gap and the corrected fair-planner rate, the fair planner accounts for a *much smaller* share of the gap than finding-07 reported, and the honest statement is now the opposite of that document's headline: on a representative cohort, **most of the clairvoyant advantage is not recovered by fair planning of this kind.** Whether the unrecovered part is information or a better fair planner's headroom remains unresolved, and §5 argues it is mostly information. |
| "Every fair game ended… every fair arm's board filled monotonically" | **stands, and is reinforced.** 0 of 128 and 0 of 32 fresh-tape fair games reached the cap. |

`finding-06`'s clairvoyant results are **not** affected; see §6.

## 3. K = 1024, and what the series extrapolates to

`finding-07` left the K series at K = 256 "still climbing", with a fitted
asymptote that the bootstrap could not identify: 54% of replicates found no
finite asymptote at all. The obvious continuation was K = 1024.

**K = 1024 is worse than K = 256.** The arm was stopped at 6 of 8 games after
four hours when the re-baselining above took priority; the six that completed
are compared paired, against the same six master tapes at K = 256.

| paired on the same 6 master tapes | K = 256 | K = 1024 | delta |
| --- | ---: | ---: | ---: |
| mean moves | 182.00 | **100.33** | **−81.67** |
| mean score | 653,644 | **347,348** | **−306,296** |
| clears/move (pooled) | 2.2125 | **2.0947** | **−0.1178** |
| reveals/move (pooled) | 1.2646 | **1.1910** | **−0.0736** |
| tapes on which K = 1024 clears more | — | **1 of 6** | |

Per tape, clears per move:

| tape | K = 256 | K = 1024 |
| --- | ---: | ---: |
| `a5230001` | 2.0308 | 1.8857 |
| `a5230002` | 2.0571 | 1.9381 |
| `a5230003` | 2.2279 | 2.2083 |
| `a5230005` | 2.2605 | 2.1667 |
| `a5230006` | 2.1692 | **2.2552** |
| `a5230007` | 2.2651 | 1.9250 |

Five of six tapes are worse and the pooled deficit is −0.118 clears per move,
larger than the entire fair-planning advantage `finding-07` reported over fair
depth 4. A five-of-six sign test alone is p ≈ 0.11 and would not carry this on
its own; the magnitude and the consistency of the lifetime collapse
(182 → 100 mean moves) are what make it convincing.

### The series is unimodal, not asymptotic

Pooled clears per move on the original eight tapes, H = 7:

| K | 1 | 4 | 16 | 64 | 256 | 1024 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| clears/move | 1.5370 | 1.7095 | 1.9279 | 2.1403 | **2.2309** | 2.0947 *(6 tapes; 2.2125 at K = 256 on the same 6)* |
| gain over previous | — | +0.1725 | +0.2183 | +0.2125 | +0.0905 | **−0.1178** |

**The requested extrapolation is therefore not an asymptote — the series has an
interior maximum near K = 256.** Fitting a saturating curve to a unimodal series
is meaningless, and
`approaches/lifetime-objective/flow-ceiling/extrapolate.py` is written to say so
rather than produce a number: on the K ≥ 16 tail it reports that the log-linear
model fits at least as well as the saturating one and that a majority of
bootstrap replicates identify no finite asymptote. **The operational answer for
the distillation agent is a peak, and the peak is what §2's re-baselining
re-values: roughly 2.2 on the eight tapes and materially lower on a
representative cohort (§4).** There is no K at which this planner approaches
2.400.

### Why more sampling makes a determinized planner worse

This is the expected behaviour of hindsight optimization, and `finding-07` §2
already named the mechanism without predicting this consequence. The planner
maximizes `max_a E_w[V(a, w)]`, where `V(a, w)` is the value of action `a` in a
world `w` *computed by a planner that knows `w`*. That estimator systematically
over-values actions whose payoff depends on the covers happening to be right,
because inside every sample the cascade is planned with the answer key.

With small `K`, sampling noise partly randomises the choice and accidentally
protects the planner from the most over-valued action. As `K` grows the noise
falls and the planner converges — reliably — on the action the biased objective
most prefers. There is therefore a bias–variance optimum in `K`, and past it
more compute buys a more faithful optimisation of the wrong objective. The
measured optimum sits near K = 256 at H = 7.

**This bounds the whole determinization family, not just this configuration.**
Increasing `K` is the obvious lever, it is embarrassingly parallel, and it does
not work. Improving this planner requires changing the estimator — a belief
state, or an explicit information term — not spending more on the current one.


## 4. Is H = 9 reachable?

**H = 9 was not reached, and this document does not report an H = 9 fair
number.** That is the honest outcome the coordinator asked for in preference to
a misleading one.

The cost arithmetic: a fair decision costs `K` window solves. Measured
single-thread window costs are 0.005 s at H = 5, 0.079 s at H = 7 and 2.52 s at
H = 9, so one H = 9 decision at K = 256 costs about **645 seconds** — over ten
minutes per move. Whole games are out of the question, and a warm-started
40-move measured segment on six tapes would still be some three hours for a
sample of 240 moves, whose standard error on clears per move is about 0.13 —
larger than the entire horizon effect being looked for.

A warm-started matched-horizon probe (identical 25-move warm-up, then measured
segments at H = 5, 7 and 9 with matched K = 64 from bit-identical states) was
built, verified, and launched. It was **stopped after producing two of eight
games** when the re-baselining above took priority. Its machinery is retained
and documented in §Method changes, since it is the right instrument for this
question when compute allows.

Reporting a low-K H = 9 number would have been actively misleading, for a reason
this document now demonstrates rather than merely suspects. `finding-07` showed
the horizon ordering reverses with sampling budget — at K = 16, H = 5 beats
H = 7; by K = 64 it is the other way round — and §3 above shows why: a
determinized planner's bias grows with how much it plans under the assumption of
knowing the answer. **A deeper horizon at fixed K makes that bias worse**, so an
H = 9 arm at an affordable K would very likely measure the bias rather than the
horizon, and would report "H = 9 does not help" for the wrong reason.

What can be said from data already in hand is that **the horizon axis is nearly
exhausted for both planners, and it is an order of magnitude smaller than the K
axis.** At matched K = 256 on the eight tapes, H = 5 → H = 7 was worth +0.063
clears per move whole-game and +0.010 in steady state; on the fresh 32-tape
cohort it is worth +0.023 (2.0031 → 2.0260). The clairvoyant series is flatter
still: 2.3496 at H = 5, 2.3601 at H = 7, 2.3663 at H = 9, increments of +0.011
and +0.006. Against that, moving K from 1 to 256 at H = 7 is worth +0.694. **The
ceiling for this planner family is set by the estimator, not by the horizon.**

## 5. Where the remaining shortfall lives

This is the sharpest result in this document.

Clears per move by occupied cells before the move, for fair depth 4, the fair
planner at three sampling budgets, and the clairvoyant planner at the same
horizon — with the clairvoyant-minus-fair shortfall on the right.

| occupied | fair D4 | fair K=16 | fair K=64 | fair K=256 | clairvoyant | shortfall vs K=16 | vs K=64 | vs K=256 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0–9 | 0.87 | 0.56 | 0.71 | 0.70 | 0.77 | +0.21 | +0.06 | +0.07 |
| 10–14 | 1.38 | 0.82 | 1.36 | 1.46 | 1.60 | +0.77 | +0.24 | **+0.14** |
| 15–19 | 1.91 | 1.30 | 1.90 | 1.84 | 2.19 | +0.89 | +0.29 | **+0.35** |
| 20–24 | 2.10 | 1.69 | 2.41 | 2.49 | 3.31 | +1.62 | +0.90 | **+0.82** |
| 25–29 | 2.59 | 2.55 | 2.48 | 3.00 | 4.46 | +1.91 | +1.98 | **+1.46** |
| 30–34 | 2.26 | 2.17 | 2.48 | 2.37 | 6.33 | +4.16 | +3.85 | **+3.96** |
| **required** | **2.400** | **2.400** | **2.400** | **2.400** | **2.400** | | | |

Moves observed per band, in the same column order:

| occupied | fair D4 | K=16 | K=64 | K=256 | clairvoyant |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0–9 | 38 | 34 | 42 | 46 | 204 |
| 10–14 | 63 | 17 | 98 | 154 | 807 |
| 15–19 | 173 | 66 | 255 | 410 | 1,037 |
| 20–24 | 266 | 149 | 339 | 585 | 689 |
| 25–29 | 239 | 157 | 271 | 289 | 163 |
| 30–34 | 78 | 115 | 142 | 84 | 30 |

**More sampling closes the shortfall in the sparse bands and does essentially
nothing at high occupancy.** Going from K = 16 to K = 256 closes

| band | shortfall closed |
| --- | ---: |
| 10–14 | **82%** (0.77 → 0.14) |
| 15–19 | **61%** (0.89 → 0.35) |
| 20–24 | **49%** (1.62 → 0.82) |
| 25–29 | **24%** (1.91 → 1.46) |
| 30–34 | **5%** (4.16 → 3.96) |

The gradient is monotone and steep. **The residual is localised at high
occupancy**, which is precisely where a public policy structurally cannot
compete, and the reason is countable: mean covered cells available on the board,
by the same bands, measured on the clairvoyant cohort —

| occupied cells | 0–9 | 10–14 | 15–19 | 20–24 | 25–29 | 30–34 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| mean covered cells | 3.9 | 7.0 | 9.9 | 13.5 | 16.7 | 20.4 |

A board at 10–14 occupied hides about **7** unknown values; a board at 30–34
hides about **20**. Sampling 256 completions is a meaningful exploration of the
first and a vanishing one of the second, and the measured shortfall behaves
exactly that way. This is not a search deficiency that a better evaluator can
absorb — it is the value of information about a large hidden state, and it is
concentrated in the region the fair planner is forced to operate in as its board
fills.

Two caveats on this table. The bands are **not matched samples**: each policy
generates its own occupancy distribution, and the clairvoyant planner barely
visits 30–34 (30 moves) because it never needs to, so its 6.33 there rests on
few observations. And every fair arm spends far more of its life above 25 cells
than the clairvoyant does, which is itself a consequence of the shortfall rather
than an independent fact.

### The same table on the fresh cohort, and it is the same shape

The band analysis above was computed on the eight original tapes. Repeating it
on the fresh cohort — where fair D4 contributes 3,284 moves in the 20–24 band
rather than 266 — reproduces the shape with much better power.

| occupied | fair D4 fresh | fair H=7 K=256 fresh | clairvoyant fresh | shortfall |
| --- | ---: | ---: | ---: | ---: |
| 0–9 | 0.55 *(584)* | 0.68 *(164)* | 0.88 *(1,653)* | +0.20 |
| 10–14 | 1.39 *(665)* | 1.53 *(231)* | 1.58 *(6,423)* | **+0.05** |
| 15–19 | 1.79 *(2,471)* | 1.88 *(494)* | 2.29 *(9,407)* | +0.41 |
| 20–24 | 2.20 *(3,284)* | 2.15 *(774)* | 3.22 *(5,714)* | **+1.07** |
| 25–29 | 2.38 *(2,590)* | 2.31 *(770)* | 4.41 *(1,376)* | **+2.10** |
| 30–34 | 2.34 *(1,301)* | 2.39 *(418)* | 5.93 *(150)* | **+3.54** |
| 35–39 | 1.82 *(681)* | 2.05 *(168)* | 3.30 *(10)* | +1.25 |
| 40–44 | 1.27 *(313)* | 1.46 *(67)* | 2.00 *(2)* | — |
| **required** | **2.400** | **2.400** | **2.400** | |

The shortfall is +0.05 at 10–14 occupied cells and **+3.54 at 30–34** — a
seventy-fold spread across the band range. **The residual is not spread across
the state space; it is concentrated on crowded boards.** And the fair planner's
clear rate is essentially *flat* from 20 to 34 cells (2.15, 2.31, 2.39) where
the clairvoyant's climbs steeply (3.22, 4.41, 5.93): as the board fills and the
hidden state grows, the clairvoyant converts the extra material into clears and
the fair planner cannot.

Note also what this does to the fair planner's equilibrium. Its rate crosses
2.400 nowhere in the range it actually occupies — it peaks at 2.39 at 30–34,
just under the requirement, and collapses above 35. It has no attracting fixed
point below the death boundary, which is why 0 of 160 fresh-tape fair games
survived.

## 6. Does finding-06's clairvoyant ceiling need a caveat?

**No.** This is the question with the most riding on it, because the whole
programme is currently oriented around that ceiling, so it was checked directly
rather than argued.

| clairvoyant `rh-clears` | cohort | games | mean moves | censored alive | clears/move | reveals/move | occupancy slope |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| H = 7 | eight original tapes | 8 | 366.25 | 6 / 8 | 2.3601 | 1.3785 | +0.014 |
| **H = 7** | **64 fresh tapes** | 64 | **386.48** | **59 / 64** | **2.3663** | **1.3832** | **+0.075** |
| H = 9 | eight original tapes, cap 400 | 8 | 396.88 | 7 / 8 | 2.3663 | 1.3839 | −0.009 |
| H = 9 | eight original tapes, cap 1,000 | 6 | 1,000.00 | 6 / 6 | 2.3875 | 1.3963 | −0.002 |
| **H = 9** | **32 fresh tapes, cap 400** | 32 | **389.38** | **31 / 32** | **2.3655** | **1.3854** | **+0.099** |

At H = 7 the clairvoyant planner measures **2.3663 clears and 1.3832 reveals per
move on 64 fresh tapes against 2.3601 and 1.3785 on the eight** — a shift of
**+0.006 and +0.005, in the *opposite* direction to fair D4's −0.060** — and 59
of 64 fresh games reached the move cap alive against 6 of 8 before.

At **H = 9, the exact horizon of `finding-06`'s headline**, 32 fresh tapes give
**2.3655 clears and 1.3854 reveals per move against that document's 2.3663 and
1.3839**, with **31 of 32 games censored alive** against 7 of 8. The two agree
to within 0.001 clears per move. This is the check the coordinator asked for and
it comes back clean.

The reason is structural and was predictable once the mechanism was understood.
The eight tapes inflate a policy in proportion to the room it had to run; a
planner that was hitting the move cap on both cohorts has no room to be
inflated. `finding-06`'s headline figures are measurements of a censored policy
against a cap, not of a policy against the tapes, so they are insensitive to the
tape draw in a way that fair D4's are not.

**`finding-06` therefore stands as written, with one wording caveat**: its
comparisons *against fair depth 4* used a D4 baseline that is now known to be
26% too generous on lifetime. Every statement of the form "the clairvoyant
planner reaches 396.88 moves against fair depth 4's 117.75" should be read as
"…against fair depth 4's 93.56", which makes the contrast larger, not smaller.
No clairvoyant number in that document needs revision and no conclusion in it is
weakened.

## 7. The size of the reveal-blindness effect

`finding-07` §6 diagnosed a structural blindness: a planner that averages over
sampled completions is never rewarded for *revealing a cover in order to learn*,
because inside every sample the answer is already known. It recommended an
explicit information term as "the obvious next candidate". This section sizes
that effect, and the answer argues against that recommendation.

The raw reveal rates look like a large effect. Reveals per move, by occupancy,
on the fresh cohort, with the mean number of covered cells actually available:

| occupied | fair D4 | fair H=7 K=256 | clairvoyant | covers available (fair / clair) |
| --- | ---: | ---: | ---: | ---: |
| 15–19 | 1.11 | 1.16 | 1.34 | 10.8 / 9.8 |
| 20–24 | 1.30 | 1.23 | **1.91** | 14.5 / 13.2 |
| 25–29 | 1.28 | 1.30 | **2.67** | 18.8 / 16.6 |
| 30–34 | 1.15 | 1.24 | **3.63** | 22.6 / 21.1 |

The fair planner's reveal rate is **flat** from 20 to 34 occupied cells (1.23,
1.30, 1.24) while the clairvoyant's nearly doubles (1.91 → 3.63), and the two
have essentially the same number of covers available to open. As a fraction of
available covers opened per move, the clairvoyant runs 1.71x, 2.31x and 3.15x
the fair planner across those three bands.

**But almost all of that is a consequence of clearing more, not of seeking
information.** Reveals in Drop7 are produced by cascades cracking adjacent
covers, so a policy that clears more reveals more mechanically. Normalising by
discs cleared removes that channel:

| occupied | reveals per cleared disc — fair D4 | fair H=7 K=256 | clairvoyant | ratio clair/fair |
| --- | ---: | ---: | ---: | ---: |
| 10–14 | 0.613 | 0.618 | 0.564 | **0.91x** |
| 15–19 | 0.622 | 0.616 | 0.584 | **0.95x** |
| 20–24 | 0.591 | 0.566 | 0.594 | **1.05x** |
| 25–29 | 0.535 | 0.560 | 0.605 | **1.08x** |
| 30–34 | 0.469 | 0.496 | 0.612 | **1.23x** |

**Per disc cleared, the clairvoyant planner opens only 5–23% more covers than
the fair one, and on sparse boards it opens fewer.** The reveal-blindness effect
is therefore **second-order**: it is real, it is confined to crowded boards, and
it grows in the right direction (1.05x → 1.08x → 1.23x as the board fills), but
it accounts for a small fraction of the reveal gap and a smaller fraction still
of the clear gap.

**This weakens finding-07's own recommendation.** That document proposed an
information-gathering term as the obvious next candidate on the grounds that
"every fair arm under-delivers on reveals by roughly the same fraction as on
clears". That observation is correct and this measurement explains it: the
reveal shortfall is *the clear shortfall*, arriving through the cascade
mechanism, not an independent failure to gather information. An explicit
information term would be chasing at most a 5–23% effect on a quantity that is
itself downstream of the real problem.

## 8. The K axis turns over while the M axis climbs — and why that is consistent

[`finding-09`](finding-09-reveal-sampling.md), produced concurrently by a
separate agent, factors the frozen expectimax's chance node into disc samples
`N` and reveal samples `M` and finds the reveal axis **pays monotonically**:
1.9849 clears per move at M = 1, 2.0033 at M = 3, **2.0447 at M = 6**, still
climbing, with a 95% lower bound above zero at M = 6.

This document finds the opposite shape on what looks like the same axis: more
samples of the hidden quantity help up to K ≈ 256 and then **hurt**. The
coordinator asked for the disagreement to be called out. It is real, and it
resolves cleanly — the two axes are not the same operation.

| | finding-09's `M` | this document's `K` |
| --- | --- | --- |
| system | frozen expectimax, base engine | exact window solver, latent scenario model |
| what is sampled | reveal outcomes, drawn as the move resolves | completions of the hidden board, fixed before the window is solved |
| does the inner evaluation *condition* on the sample? | **no** — the search below the chance node does not know which reveal occurred | **yes** — each window is solved knowing the sampled values |
| is the estimator unbiased? | **yes**, it is an expectation over a genuine future draw | **no**, it is hindsight optimization |
| effect of more samples | monotone improvement | interior optimum, then degradation |

**The distinguishing property is whether the inner evaluation conditions on the
sample.** finding-09's `M` refines an honest expectation: the base engine draws a
covered disc's number at the moment of reveal (audit-01 M2), so there is no
hidden truth to be clairvoyant about, and adding samples strictly reduces
estimator variance with no bias to sharpen. This document's `K` refines a
*biased* objective, because inside each determinization the cascade is planned
with the answer key, and converging on that objective converges on its bias.

That is a transferable methodological lesson, and it is the useful output of the
disagreement: **averaging over sampled completions of hidden state is safe only
if the evaluation inside the sample does not condition on the sample.** Any
future Drop7 planner that determinizes the covered board inherits this ceiling;
one that samples reveals *without* letting the search see them does not.

**One striking coincidence worth recording rather than over-reading.**
finding-09's best legal arm reaches **2.0447** clears per move and this
document's re-baselined fair planner reaches **2.0445** at H = 5, K = 256 and
2.0260 at H = 7 — two quite different legal methods, in two different engines, on
different cohorts, landing within 0.0002 of each other and both about 0.06 above
fair depth 4. It may be coincidence at this sample size. It may also be that
**≈ 2.04 clears per move is where current legal methods sit**, roughly 85% of the
2.400 requirement, with the clairvoyant ceiling at 2.366 unreached by any of
them. The two agents should compare on a shared cohort before either claims it.

A third agent (`approaches/lifetime-objective/planner-distill`,
[`finding-11`](finding-11-planner-distillation.md)) is distilling this document's
fair planner; §9 is written for them.

## 9. Verdict — the ceiling the distillation agent should design against

**The fair planner's sustained clear rate is about 2.03 per move, not 2.23 and
not 2.40.** That is the number a student distilled from this teacher is bounded
by, and it is only +0.13 above fair depth 4 on a matched cohort.

| quantity | finding-07 reported | corrected here |
| --- | ---: | ---: |
| best fair planner, clears/move | 2.2309 (8 tapes, H = 7, K = 256) | **2.0260** (32 fresh tapes, same configuration) |
| the distilled teacher, clears/move | 2.1680 (8 tapes, H = 5, K = 256) | **2.0445** (128 fresh tapes) |
| fair depth 4, clears/move | 2.0467 | **1.9865** (128 fresh tapes) |
| clairvoyant ceiling, clears/move | 2.3601 (H = 7) | **2.3663** (64 fresh tapes) — unchanged |
| share of the gap closed by fair planning | 58.8% | **27.1%** (H = 7), 22.2% (H = 5) |
| the K asymptote | "still climbing, not identified" | **an interior maximum near K = 256**; K = 1024 is worse |

Four conclusions, in the order they bear on the programme:

1. **More sampling is not the lever, and neither is more horizon.** The K series
   turns over at K ≈ 256 and the horizon axis is worth an order of magnitude
   less than the K axis. Both obvious ways to spend compute on this planner are
   exhausted. The determinization family has been measured to its ceiling.
2. **The fair planner's advantage over D4 is real but small, and mostly does not
   reach lifetime.** At H = 7 on fresh tapes, clears/move, reveals/move,
   occupancy slope and mean lifetime all have lower bounds above zero; mean
   score does not. At H = 5 — the configuration being distilled — only the flow
   rates survive, and the planner wins the lifetime race on 58 of 128 tapes. **A
   student that exactly reproduces this teacher would be, at best, a slightly
   better fair D4.**
3. **The residual is localised on crowded boards and it is information.** The
   clairvoyant shortfall is +0.05 clears per move at 10–14 occupied cells and
   +3.54 at 30–34, where 20 covered cells are hidden. Sampling closes the sparse
   bands and does nothing in the crowded ones. This is the one place a public
   policy structurally cannot compete, and it is exactly the regime a policy
   running a flow deficit is pushed into.
4. **Explicit information-gathering is a smaller lever than finding-07
   suggested.** Per disc cleared, the clairvoyant opens only 5–23% more covers.
   The reveal shortfall is the clear shortfall arriving through the cascade
   mechanism, not an independent failure to gather information.

**What this does not say.** It does not say the survival programme is capped:
`finding-06`'s clairvoyant ceiling is intact and re-confirmed on fresh tapes, so
flow balance remains achievable *in the game*. It says that **determinized fair
planning is not the route to it**, and that the route will have to reason about
the hidden board rather than average over guesses of it.

**Recommendation for the distillation agent.** Distilling this teacher is still
worth doing — it is legal, it is 0.13 clears per move ahead of D4 at H = 7, and
amortising 256 exact window solves per move into a network is exactly what
distillation is for. But the target should be set at roughly **2.03 clears per
move and ~95 moves**, not at 2.23 or 2.40, and a student that matches its
teacher should be counted a success rather than a disappointment. The
policy-improvement headroom above the teacher is small; the headroom is in a
different teacher.

## Limitations

1. **The eight-tape cohort is the subject of this correction, not a limitation
   of it.** findings 06 and 07 disclosed the small cohort; what was missing was
   its size, which is now measured at −20.5% on fair D4's lifetime and −3.7% on
   lowest column's. The bias is a *lifetime* bias, so it is not a constant
   factor that can be divided out of past results — it must be re-measured per
   policy.
2. **The H = 7, K = 256 fresh cohort is 32 tapes, not 128.** That arm costs
   about 28,000 core-seconds per eight tapes; 32 was the budget. Its paired
   bounds are correspondingly wider, and fair D4's mean on that particular
   32-tape subset is 78.47 rather than its 93.56 over all 128, so only the
   paired deltas transfer.
3. **K = 1024 is 6 of 8 games.** The arm was stopped after four hours when
   re-baselining took priority. Five of six tapes are worse than K = 256 and the
   pooled deficit is large, but a six-tape sign test is p ≈ 0.11 on its own; the
   conclusion rests on magnitude and on the mechanism, not on that test.
4. **No H = 9 fair measurement exists.** §4. The horizon claim in this document
   is an extrapolation from H = 5 → H = 7 at matched K plus the clairvoyant
   horizon series, not a direct measurement.
5. **The by-band tables are descriptive, not causal.** Each policy generates its
   own occupancy distribution; the clairvoyant planner contributes only 150
   moves in the 30–34 band because it rarely needs to be there.
6. **Hindsight optimization remains one particular fair planner.** Every
   statement about "the fair ceiling" is a statement about the determinization
   family. A belief-state planner is unmeasured and this document's §9(3) is the
   argument that it is where the remaining value is.
7. **The latent randomness model is not the base engine's model** (audit-01 M2),
   inherited from finding-06. Scores are not comparable to any ledger figure.
   The independent agreement of three fair-D4 lifetime measurements — 93.56
   here, 94.06 in the base engine in finding-01, 93.78 from the distillation
   agent — is the evidence that the models agree on behaviour.
8. **The timings are not timing-grade.** System load averaged 40–60 on a
   32-thread machine throughout, from three other agents running concurrently.
   At most 20 threads were requested by this work.
9. **A model contribution record under `research/contributions/` is owed and was
   not written**, because this work was scoped to create files only under
   `approaches/lifetime-objective/flow-ceiling/` and `docs/exploratory/`. The
   same debt is open for findings 02, 06 and 07.

## Reproduce

```sh
./approaches/lifetime-objective/flow-ceiling/build.sh
./build/flow-ceiling/flow-run --self-test
./build/flow-ceiling/flow-run \
    --cross-check approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl \
    --threads 12

RID=RUN-FLOW-044da902f8e8

# 1. K = 1024, continuing the finding-07 series on its own sampler base
./build/flow-ceiling/flow-run --policy fair-rh --games 8 --seed-start 0xa5230000 \
    --sampler-seed 0xa5234000 --horizon 7 --samples 1024 --max-moves 400 \
    --threads 8 --sample-threads 2 --time-limit 20 \
    --jsonl runs/$RID/fair-rh-h7-k1024.jsonl

# 2. the horizon probe: identical warm-up, then matched-K measured segments
for H in 9 7 5; do
  ./build/flow-ceiling/flow-run --policy fair-rh --games 8 \
      --seed-start 0xa5230000 --sampler-seed 0xa5238100 --horizon $H \
      --samples 64 --warm-moves 25 --warm-horizon 7 --warm-samples 64 \
      --max-moves 65 --threads 4 --sample-threads 1 --time-limit 30 \
      --jsonl runs/$RID/horizon-probe-h$H-k64.jsonl
done

# 3. the re-baseline on fresh master tapes
./build/flow-ceiling/flow-run --policy fair-d4 --games 128 --seed-start 0xa5239000 \
    --max-moves 400 --threads 4 --jsonl runs/$RID/rebase-fair-d4-128.jsonl
./build/flow-ceiling/flow-run --policy lowest-column --games 128 --seed-start 0xa5239000 \
    --max-moves 400 --threads 4 --jsonl runs/$RID/rebase-lowest-128.jsonl
./build/flow-ceiling/flow-run --policy rh-clears --games 64 --seed-start 0xa5239000 \
    --horizon 7 --commit 1 --max-moves 400 --threads 4 --time-limit 12 \
    --jsonl runs/$RID/rebase-clair-h7-64.jsonl
./build/flow-ceiling/flow-run --policy rh-clears --games 32 --seed-start 0xa5239000 \
    --horizon 9 --commit 1 --max-moves 400 --threads 16 --time-limit 12 \
    --jsonl runs/$RID/rebase-clair-h9-32.jsonl
./build/flow-ceiling/flow-run --policy fair-rh --games 128 --seed-start 0xa5239000 \
    --sampler-seed 0xa523a000 --horizon 5 --samples 256 --max-moves 400 \
    --threads 4 --sample-threads 1 --time-limit 20 \
    --jsonl runs/$RID/rebase-fair-h5-k256-128.jsonl
./build/flow-ceiling/flow-run --policy fair-rh --games 32 --seed-start 0xa5239000 \
    --sampler-seed 0xa523a400 --horizon 7 --samples 256 --max-moves 400 \
    --threads 8 --sample-threads 2 --time-limit 20 \
    --jsonl runs/$RID/rebase-fair-h7-k256-32.jsonl

# 4. the paired bootstrap bounds
python3 approaches/lifetime-objective/flow-ceiling/paired.py \
    runs/$RID/rebase-fair-h7-k256-32.jsonl runs/$RID/rebase-fair-d4-128.jsonl \
    --label-a "fairRH H7K256" --label-b "fair D4"

# 5. deterministic cohorts re-run for per-move cover accounting
./build/flow-ceiling/flow-run --policy rh-clears --games 8 --seed-start 0xa5230000 \
    --horizon 7 --commit 1 --max-moves 400 --threads 3 --time-limit 12 \
    --jsonl runs/$RID/rh-clears-h7-c1-pm.jsonl
./build/flow-ceiling/flow-run --policy fair-d4 --games 8 --seed-start 0xa5230000 \
    --max-moves 400 --threads 3 --jsonl runs/$RID/fair-d4-pm.jsonl

# 6. the tables and the extrapolation
python3 approaches/lifetime-objective/flow-ceiling/compare.py "label=runs/$RID/x.jsonl" ...
python3 approaches/lifetime-objective/flow-ceiling/extrapolate.py --steady --min-k 16 \
    16=runs/$RID/fair-rh-h7-k16.jsonl 64=runs/$RID/fair-rh-h7-k64.jsonl \
    256=runs/$RID/fair-rh-h7-k256.jsonl 1024=runs/$RID/fair-rh-h7-k1024.jsonl
```

### Seed lease

Game seeds `0xa5230000`–`0xa5230007` from `SEEDLEASE-A52-FLOW`, reused from
finding-06 and finding-07 so every cohort plays the same master tapes. The
K = 1024 arm continues the existing K series' sampler base `0xa5234000`
(`FLOW2`) deliberately, because a new K in an existing series must share that
series' stream to be a continuation of it. Everything introduced here draws from
`SEEDLEASE-A52-FLOW3`: FLOW3 is partitioned so that a master-tape seed and a sampler seed can never
collide — game tapes take `0xa5239000`–`0xa5239fff` and samplers take
`0xa523a000`–`0xa523bfff`. Used here: `0xa5239000`+ for the 128 fresh master
tapes, `0xa523a000`+ and `0xa523a400`+ for the two fair-planner samplers, and
`0xa5238100`+ for the abandoned horizon probe. `flow-run` refuses to start with a
game seed outside FLOW or the FLOW3 tape partition, or a sampler seed outside
FLOW2/FLOW3.

### Environment

AMD clang 23.0.0git, `-O3 -std=c++20 -pthread -Wall -Wextra`. 16 physical cores
/ 32 logical, 125 GiB RAM. **Heavily shared**: system load averaged 45–55
throughout, against a 32-thread machine, because three other agents were running
concurrently. At most 20 threads were requested by this work and the effective
share was closer to 15. Wall-clock costs quoted here are therefore pessimistic;
node counts are exact and machine-independent.
