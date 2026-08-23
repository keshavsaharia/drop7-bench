# Finding 08 — A learned leaf helps, and the chance-bias cap hypothesis is wrong

**Status:** exploratory. Evidence tier `development` for the gameplay arms (64
paired whole games per arm on a previously read reusable development cohort),
`CHECK` for the export, parity and feasibility gates. Built and measured in this
checkout on 2026-08-20.
**Namespace:** `approaches/lifetime-objective/learned-leaf/`, run
`runs/RUN-A52-LEAF/`, seed lease `SEEDLEASE-A52-LEAF` = `0xa5240000`–`0xa5247fff`.
**Preregistration:** [`PREREGISTRATION.md`](../../approaches/lifetime-objective/learned-leaf/PREREGISTRATION.md),
written before any gameplay cohort was run.
**Nothing in `docs/research/`, `artifacts/`, `research/`, or any existing
approach source was modified.** The only files created are under
`approaches/lifetime-objective/learned-leaf/`, `build/lifetime-leaf/`,
`runs/RUN-A52-LEAF/` and `docs/exploratory/`.

## Summary

Four results, in descending order of how much they should change what the
program does next.

1. **The preregistered prediction is refuted.** A learned survival evaluator
   blended into the leaf of fair depth 4 is worth **+39,105 points (95% lower
   bound +1,138)** with the biased five-stratum chance estimator and
   **+17,281 (95% lower bound −55,892)** with the exact seven-stratum one. The
   difference-in-differences is **−21,824 (95% lower bound −105,254)**: the
   effect is *smaller* where the prediction said it would be larger. Chance-node
   bias does **not** cap leaf-injected foresight the way it caps extra
   lookahead. Depth and estimator quality are complements
   ([`finding-05`](finding-05-chance-strata.md)); a learned leaf and estimator
   quality behave like **substitutes**.

2. **The learned leaf is nonetheless a real, positive, significant result on the
   axis where it was tested.** +39,105 points and +11.13 moves at five strata,
   37–0–27 on paired games, with a one-sided 95% bootstrap lower bound above
   zero, and every quantile from Q25 up improving. Against the pattern
   `status.md` §4 records — learned public-state values that fitted the played
   action and then failed to rank unplayed siblings — this one is state-only and
   sibling-blind by construction, and it improved complete games.

3. **The model that was supposed to play could not.** A depth-4 fair expectimax
   evaluates **615,090 leaves per decision at five chance strata and 2,271,280
   at seven** — measured, not estimated. The trained 3,006,543-parameter CNN
   costs 4,122 µs per state in the exported C++ path, so at the leaf it is
   **~2,860x over budget**: 2,535 seconds per decision against a 0.887 s
   reference decision, or 18 days for one 64-game arm. The experiment ran on an
   NNUE-shaped student, trained on the same corpus and the same whole-origin
   split, that reaches **0.8564 held-out lifetime Pearson against the CNN's
   0.8646 at 1/3,109th the cost**. Model capacity was not the binding
   constraint; leaf arity was.

4. **`torch.nn.Conv2d` on this host's CPU is nondeterministic**, which silently
   broke the parity gate before it was caught. Separate write-up:
   [`gpu-03`](gpu-03-onednn-conv-nondeterminism.md).

The mechanism behind (1) is visible in the flow rates. Both the exact chance
estimator and the learned leaf are ways of knowing what happens *after* the
current move. Having bought that information once, buying it again returns less:
seven strata is worth +101,171 over five with the frozen leaf and only +79,347
with the learned one; the learned leaf is worth +39,105 at five strata and
+17,281 at seven. Read either way round, it is the same substitution.
## 1. The export, and the wall it ran into

### 1.1 A dependency-free C++ inference path

`export_net.py` writes the trained checkpoint to a versioned, self-describing
binary (`D7NET`, format version 1: a magic, the architecture scalars, a
name-keyed float32 tensor table in PyTorch's own contiguous order, and an
FNV-1a digest of everything above it). `net.hpp` reads it and runs the model
with no libtorch, no BLAS, no dynamic library and no allocation on the hot
path. Everything is NHWC — 49 pixels, channels minor — so a 3x3 convolution is
49 x 9 rank-1 updates of a `C_out`-wide accumulator and the innermost loop is a
unit-stride FMA over channels. Weights are repacked once at load into
`[tap][in][out]` order.

Exported artifact: `runs/RUN-A52-LEAF/model/survival-c128-shuffled.d7net`,
54 tensors, 3,006,543 floats, 12,027,890 bytes, `fnv1a=0x187dcaafbf6479bd`.

### 1.2 The parity gate, with the tolerance stated

4,096 real board states drawn from `runs/RUN-A51D-corpus/mix-d3.states` by a
deterministic stride that both the C++ and the Python side compute
independently. Declared tolerance before the comparison: **2e-3 absolute or
2e-3 relative on every head**, relative difference measured as
`|a-b| / max(|a|, |b|, 1.0)` with the floor stated so a near-zero output cannot
manufacture an infinite ratio.

| head | max absolute | max relative | reference range |
| --- | ---: | ---: | --- |
| hazard logits (12) | **4.900e-05** | 4.031e-05 | [−22.19, 17.57] |
| lifetime, log1p moves | **4.649e-06** | 2.333e-06 | [0.574, 4.353] |
| flow, clears | **3.767e-05** | 3.103e-05 | [−0.246, 11.46] |
| flow, reveals | **1.812e-05** | 1.252e-05 | [−0.201, 6.693] |
| hazard probabilities (derived) | 8.643e-06 | 8.643e-06 | [2.3e-10, 1.0] |
| lifetime in **moves** (derived) | **1.357e-04** | 5.383e-06 | [0.776, 76.73] |

**PASS**, by a factor of about 40 on the worst head. Pearson between the two
lifetime predictions is 1 − 4e-13. Ranking 4,096 states by predicted lifetime
gives **one** adjacent inversion out of 4,095, and it occurs at an exact tie in
the reference (gap 0.000), so it is a sort-stability artifact rather than a
disagreement.

The same gate for the student that actually plays (§3), against a reference
proved bit-repeatable over three trials:

| head | max absolute | max relative |
| --- | ---: | ---: |
| hazard logits | 3.815e-06 | 2.034e-06 |
| lifetime, log1p moves | 1.192e-06 | 4.493e-07 |
| flow, clears | 2.265e-06 | 2.265e-06 |
| flow, reveals | 1.669e-06 | 1.179e-06 |
| lifetime in moves | 6.515e-05 | — |

In the unit the search actually consumes — `expm1(lifetime) x 3400`, points —
the largest disagreement anywhere in the 4,096 states is **0.22 points**,
against a 17,000-point row-rise bonus.

### 1.3 The gate initially failed for a reason that was not the code

The first CPU comparison passed at 1.1e-5. The identical command, re-run,
reported failures on every head and a maximum relative error of 3.5e-2. Neither
run was wrong about the C++ path: **`torch.nn.Conv2d` on this host's CPU is
nondeterministic.** Two identical forward passes over the same tensor, in
`eval()`, single-threaded, with every thread environment variable pinned to 1,
differ in 6.25% of output elements by up to 0.15. `GroupNorm`, `Linear` and
`torch.mm` are bit-exact; disabling the oneDNN path makes `Conv2d` bit-exact.
Through GroupNorm's group statistics one perturbed stem activation moves every
downstream output, so repeated whole-model passes differ in **100%** of outputs
by 0.18–0.35 per logit.

This is written up separately in
[`gpu-03-onednn-conv-nondeterminism.md`](gpu-03-onednn-conv-nondeterminism.md),
because it is not specific to this experiment: any gate in this repository that
compares against a CPU PyTorch convolution has been measuring that comparison
plus an unknown perturbation. The parity scripts here now disable oneDNN and
**prove the reference repeats bit-for-bit before comparing anything to it**,
exiting non-zero on drift.

## 2. The affordability wall, measured rather than assumed

Nothing in the repository recorded how many leaves a depth-4 fair expectimax
evaluates, so `leaf-probe` measured it over 30 whole-game decisions from
`0xa5240000`, counting both total leaf calls and distinct leaf states (the
ceiling on what a perfect leaf memo could save).

| | depth 4, 5 strata | depth 4, 7 strata |
| --- | ---: | ---: |
| logical work per decision | 1,271,785 | 4,631,941 |
| **leaf evaluations per decision** | **615,090** (max 1,110,435) | **2,271,280** (max 3,405,402) |
| distinct leaf states per decision | 380,326 | 1,118,405 |
| dedup ratio available to a perfect memo | 1.6x | 2.0x |
| reference decision, one thread | 0.887 s | 3.675 s |

Against that, the exported CNN costs **4,122 µs per state** on one thread
(42 GFLOP/s for its ~174 MFLOP), and 2,218 states/s across 12 threads.

**615,090 x 4,122 µs = 2,535 seconds per decision**, against a 0.887 s reference
decision. That is a factor of **2,860**. One 87-move game would take 6.7 hours
of the entire machine; the 64-game arm would take 18 days. A 1.6x memo, 4x
int8 quantisation and a 2x better kernel together move that to ~220x, which is
still not a slower experiment but an impossible one.

**The premise that a ~3M-parameter CNN can be the leaf of a depth-4 expectimax
in this game is false, and it is false by three orders of magnitude.** That is
the first result in this document and it is not close enough to argue about.

## 3. What can afford to play: LeafNet

A leaf evaluator has roughly one microsecond. That is an NNUE-shaped budget, so
the student is NNUE-shaped: binary features that are gathered and summed rather
than multiplied through a dense matrix.

8,902 features, exactly 135 active per state — 49 (cell, value) features,
1 next disc, 1 moves-until-rise, and 42 horizontal plus 42 vertical adjacent
**pair** features `(position, value_a, value_b)`. The pairs are the point: a
Drop7 disc clears when its number equals the length of the run it lands in, so
run structure is the mechanism, and a per-cell bag of features cannot see a run
at all. Then `EmbeddingBag(8902, 64, sum) + bias -> ReLU -> Linear(64, 32) ->
ReLU -> Linear(32, 15)`, the 15 outputs being the same 12 hazard logits, log
lifetime and 2 flow scalars the CNN predicts.

Trained on the same corpus, the same targets, the same loss weights and — this
matters — **the same whole-origin split seed**, so the two models' held-out
numbers are computed on the same 486,819 rows from the same games.

| model | parameters | held-out lifetime Pearson | held-out MAE (moves) | µs per state |
| --- | ---: | ---: | ---: | ---: |
| SurvivalNet CNN (teacher) | 3,006,543 | **0.8646** | 14.25 | 4,122 |
| **LeafNet (deployed)** | 572,367 | **0.8564** | 14.44 | **1.33** |

The compression costs **0.008 of Pearson and 0.2 moves of MAE for a 3,109x
speedup.** On 4,096 held-out states the student's lifetime prediction agrees
with the teacher's at Pearson 0.9815 and a mean absolute difference of 2.48
moves (teacher mean 35.81 moves, student 35.52). Mirroring the board changes the
student's held-out Pearson from 0.85640 to 0.85644, so it carries no column
preference — which matters because the frozen search hands the leaf a
*canonicalised* state while the corpus stores boards in play orientation.

Training took 10 epochs at 3.4 s per epoch on the gfx1151 GPU. The student
contains no convolution, so its PyTorch reference repeats bit-for-bit with no
flags at all (§1.3).

## 4. The leaf, and why this leaf value

    leafValue = (1 - w) * frozen::fairLeaf(state) + w * scale * learnedValue(state)

`w = 0` short-circuits to `frozen::fairLeaf` **before the model is touched**, so
the reference arms are the frozen search bit-for-bit and cost exactly what the
reference costs. CHECK gate: `--parity --seed-start 0xa5240100 --parity-games 2
--parity-moves 25` → **50 moves compared, 0 mismatches**. Everything else in the
search — depth, chance stratification, canonicalisation, cache keying, column
order, work accounting, terminal utility — is the unmodified frozen code
reached through the same driver as
`approaches/lifetime-objective/risk-calibration/search.cpp`.

Two learned values are implemented, and which one was expected to win was
written down before the runs:

- **`lifetime`: `expm1(lifetimeHead) * 3400`.** Score is 94.29% flat
  17,000-point row-rise bonus, correlates with lifetime at r = 0.9995, and runs
  at ~3,400 points per move ([`finding-01`](finding-01-score-is-survival.md)).
  Expected remaining lifetime x 3,400 is therefore *already in real score
  units*, the same unit the search's immediate-score term carries at weight 1.0.
  The blend mixes two estimates of one quantity rather than a score and an
  arbitrary index.
- **`hazard`: `sum_k P(survive k more rises) * 17000`, k = 1..12.** Expected to
  be weaker, and the reason given in advance was **saturation**: 12 rises is 60
  moves, while the lifetime a 1,000,000-point mean needs is ~294 moves, so every
  merely-healthy position scores the same 12 and the head stops discriminating
  exactly where the objective lives.

The two terms are only comparable if their scales are known, so `--leaf-stats`
measured them on 20,000 corpus states before `w` was chosen:

| term | mean | sd | Pearson with `fairLeaf` |
| --- | ---: | ---: | ---: |
| `frozen::fairLeaf` | −27,773 | 28,715 | — |
| `lifetime x 3400` | 120,148 | 61,300 | 0.860 |
| `hazard x 17000` | 108,651 | 52,829 | 0.892 |

`fairLeaf` is not in score units despite being summed with real points: it
carries a −27,773 offset and a 28,715 spread. Only the spread matters for action
ranking, and the learned term has 2.13x of it, so equal influence sits near
w = 0.32.

## 5. Tuning, on a separate cohort

32 games, seeds `0xa5241000`–`0xa524101f`, depth 4, five strata,
`--max-work 3200000`. **The evaluation cohort was never touched by tuning.**

| arm | mean score | median | moves | clears/move | reveals/move | cells | paired Δ vs `w=0` | 95% lower | W–T–L |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :---: |
| `w = 0` (reference) | 308,959 | 243,981 | 90.59 | 1.9541 | 1.0790 | 24.07 | — | — | — |
| `w = 0.25`, lifetime | 317,134 | 219,304 | 93.03 | 1.9631 | 1.0880 | 24.17 | +8,174 | −66,567 | 15–0–17 |
| **`w = 0.50`, lifetime** | **342,640** | **286,050** | **100.50** | **2.0190** | **1.1253** | 24.49 | **+33,681** | −38,095 | 14–0–18 |
| `w = 1.00`, lifetime | 278,012 | 230,349 | 82.59 | 1.9039 | 1.0325 | 25.53 | −30,947 | −96,768 | 15–0–17 |
| `w = 0.50`, hazard | 305,683 | 248,672 | 90.06 | 1.9483 | 1.0649 | 24.40 | −3,276 | −70,385 | 16–0–16 |

Two things are worth keeping even though none of these 32-game bounds excludes
zero (32 games at sd ≈ 150,000 resolves nothing smaller than about ±50,000; the
tuning cohort is for choosing, the evaluation cohort is for testing):

1. **The response in `w` is an inverted U, and the pure learned leaf is worse
   than the frozen one.** At `w = 1.00` the hand-weighted leaf is switched off
   entirely and the policy loses 31,000 points and 8 moves relative to the
   reference, with occupancy climbing to 25.53 cells. A state-value model that
   predicts remaining lifetime well in the aggregate is still a worse *ranker of
   sibling leaves* than nineteen hand-tuned structural terms. The two are
   complements, and the blend is not a formality.
2. **The saturation argument for the hazard head was right.** Priced at one
   level bonus per surviving rise it is worth −3,276 points, i.e. nothing, while
   the unbounded lifetime head at the same `w` is worth +33,681.

**Frozen for the 2x2: `w = 0.50`, `scale = 3400`, `--leaf-value lifetime`.**
## 6. The preregistered prediction, verbatim

Copied unaltered from
[`PREREGISTRATION.md`](../../approaches/lifetime-objective/learned-leaf/PREREGISTRATION.md)
§2, written before any cohort was run:

> **The learned leaf will help materially more at 7 chance strata than at 5.**
>
> Concretely, with `d5 = mean paired score(learned leaf, depth 4, 5 strata)
> − mean paired score(reference leaf, depth 4, 5 strata)` and `d7` the same
> quantity at 7 strata, both over the same 64 seeds:
>
> **`d7 > d5`, and the difference-in-differences `d7 − d5` is positive with a
> one-sided 95% bootstrap lower bound above zero.**

with the pass/fail rule:

> | Outcome | Criterion |
> | --- | --- |
> | **PASS** — prediction supported | `d7` one-sided 95% lower bound > 0 **and** `(d7 − d5)` one-sided 95% lower bound > 0 |
> | **FAIL** — prediction refuted, hypothesis wrong | `d5` one-sided 95% lower bound > 0 **and** `(d7 − d5)` one-sided 95% lower bound ≤ 0. The learned leaf helps, and it helps as much (or more) with the biased estimator. The cap hypothesis is then wrong and this document will say so plainly. |
> | **INCONCLUSIVE about the interaction** | neither `d5` nor `d7` has a lower bound above zero. |

and the falsification condition:

> **What would falsify the prediction:** the learned leaf producing an equal or
> larger gain at 5 strata than at 7. That is a real possible outcome — the
> learned leaf could substitute for lookahead precisely where lookahead is
> useless — and it will be reported as a refutation if observed.

**That is what was observed.** `d5`'s lower bound is +1,138 (> 0) and the
difference-in-differences lower bound is −105,254 (≤ 0). By the rule written
before the data existed, this is a **FAIL: the prediction is refuted and the
cap hypothesis is wrong for leaf-injected foresight.**

## 7. The 2x2

64 paired whole games per arm, seeds `0xa51d1000`–`0xa51d103f`, depth 4,
2,000-move cap, 12 threads. Five-stratum arms at `--max-work 3200000`,
**seven-stratum arms at `--max-work 16000000`**. The learned arms use
`w = 0.50`, `scale = 3400`, `--leaf-value lifetime`, frozen on the separate
tuning cohort of §5.

| arm | mean | median | Q25 | min | max | sd | moves | ≥1M games |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| d4 s5, reference leaf | 297,327 | 260,415 | 192,352 | 86,935 | 836,427 | 150,550 | 87.16 | 0 |
| d4 s5, learned leaf | **336,432** | **313,730** | **209,914** | **104,322** | 948,586 | 171,583 | **98.28** | 0 |
| d4 s7, reference leaf | 398,498 | 344,630 | **212,864** | 103,331 | 1,341,287 | 254,414 | 114.66 | 2 |
| d4 s7, learned leaf | **415,779** | **349,034** | 194,040 | 86,033 | **1,344,732** | 276,636 | **120.75** | **3** |

### Paired deltas, one-sided 95% bootstrap lower bounds over whole games

20,000 percentile-bootstrap resamples, RNG seed `0xa52a1eaf`, resampling
**seeds** so a game's four arm outcomes stay together.

| comparison | Δ score | 95% lower | Δ moves | W–T–L | reading |
| --- | ---: | ---: | ---: | :---: | --- |
| **`d5` = learned − reference, 5 strata** | **+39,105** | **+1,138** | +11.13 | 37–0–27 | **significant** |
| **`d7` = learned − reference, 7 strata** | +17,281 | −55,892 | +6.09 | 34–0–30 | not significant |
| **DiD = `d7` − `d5`** | **−21,824** | **−105,254** | −5.03 | 29 of 64 games positive | **prediction refuted** |
| 7 strata − 5 strata, reference leaf | +101,171 | +46,730 | +27.50 | 41–0–23 | significant (reproduces finding-05) |
| 7 strata − 5 strata, learned leaf | +79,347 | +17,053 | +22.47 | 38–0–26 | significant, but **21,824 smaller** |

The last two rows are the same substitution seen from the other axis, and they
are the more useful form of it: the value of making the chance estimator exact
**falls by 22%** once the leaf already carries a learned survival estimate.

### The validity gate that makes this comparable

Because `w = 0` short-circuits to `frozen::fairLeaf` before the model is
touched, the two reference arms are the frozen policy bit-for-bit and had to
reproduce [`finding-05`](finding-05-chance-strata.md)'s published confirmation
cohort. They did, to every digit reported there:

| | mean | median | Q25 | min | max | sd | moves | med moves | clears/mv | reveals/mv | work/mv |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| d4s5 here | 297,327 | 260,415 | 192,352 | 86,935 | 836,427 | 150,550 | 87.16 | 77.5 | 1.9489 | 1.0697 | 1,296,034 |
| d4s5 published | 297,327 | 260,415 | 192,352 | 86,935 | 836,427 | 150,550 | 87.16 | 77.5 | 1.9489 | 1.0697 | 1,296,034 |
| d4s7 here | 398,498 | 344,630 | 212,864 | 103,331 | 1,341,287 | 254,414 | 114.66 | 102.5 | 2.0571 | 1.1549 | 4,956,614 |
| d4s7 published | 398,498 | 344,630 | 212,864 | 103,331 | 1,341,287 | 254,414 | 114.66 | 102.5 | 2.0571 | 1.1549 | 4,956,614 |

The 4,956,614 work per move also confirms the seven-stratum arms completed
depth 4 rather than silently degrading to depth 3 at the frozen 3,200,000 bound
— the failure mode `finding-05` identified. **Score-identity violations: 0 in
all four arms, 5,578 + 6,290 + 7,338 + 7,728 = 26,934 decisions. Censored games:
0 of 256.**

## 8. Flow rates and occupancy — is the gain mechanical or lucky?

Steady-state survival requires ≥ 2.400 numbered clears and ≥ 1.400 covered
reveals per move ([`finding-01`](finding-01-score-is-survival.md)); a
clairvoyant planner sustains 2.3875 / 1.3963 at 19–20 occupied cells
([`finding-06`](finding-06-flow-ceiling.md)).

| arm | clears/move | reveals/move | mean occupied cells | points/move | level share | chain share |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| d4 s5, reference | 1.9489 | 1.0697 | 24.29 | 3,411 | 94.61% | 5.39% |
| d4 s5, learned | **1.9831** | **1.0909** | 24.68 | 3,423 | 94.90% | 5.10% |
| d4 s7, reference | 2.0571 | 1.1549 | 23.15 | 3,476 | 93.79% | 6.21% |
| d4 s7, learned | **2.0714** | **1.1595** | 23.87 | 3,443 | 94.81% | 5.19% |
| requirement / clairvoyant | 2.400 / 2.3875 | 1.400 / 1.3963 | 19–20 | — | — | — |

**At five strata the mechanism is present.** Clears rise by +0.0342 and reveals
by +0.0212 per move, closing 7.6% and 6.4% of the remaining gap to the
conservation requirement, and the lifetime gain (+11.13 moves) is what that flow
gain predicts. The score gain is not a tail artifact: the cohort median rises by
+53,315, Q25 by +17,562 and the minimum by +17,387 — every reported quantile
improves — and the median *paired* per-game delta is +54,319.

**At seven strata the mechanism is absent.** Pooled clears move +0.0143 and
reveals +0.0046, and the *paired per-game* versions are slightly **negative**
(−0.0139 and −0.0157, 95% lower bounds −0.0834 and −0.0682). The +17,281 score
delta at seven strata is therefore not supported by a flow-rate movement, which
is an independent reason — beyond its own bootstrap bound — to treat it as
unresolved rather than as a smaller real effect. It also strengthens the
refutation: the prediction required the learned leaf to work *better* here, and
by the mechanistic test it does not work here at all.

**Occupancy moves the wrong way in both cases.** The learned leaf pushes the
board from 24.29 to 24.68 cells at five strata and from 23.15 to 23.87 at
seven, away from the 19–20 cell equilibrium where `finding-06` measured the
achievable clear rate crossing 2.400. The tuning sweep shows the same thing more
sharply: the pure learned leaf at `w = 1.00` runs at **25.53** cells and loses
31,000 points. **The learned leaf buys lifetime without moving the board toward
the operating point that makes lifetime free.** It is trading inside fair D4's
regime rather than relocating the policy to the clairvoyant one, and that is a
concrete, measurable statement about its ceiling.

## 9. Inference cost, stated as a frontier point

`docs/benchmarks.md` requires a learned candidate to be presented as a point on
the resource-performance frontier, not as a strength number alone.

| arm | leaf evals / decision | **model evals / decision** | wall s / decision | CPU s / decision | games / hour | mean score |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| d4 s5, reference | 628,609 | 0 | 0.111 | 1.327 | **373.5** | 297,327 |
| d4 s5, learned | 660,481 | **660,481** | 0.215 | 2.575 (**1.94x**) | 170.7 (0.46x) | 336,432 |
| d4 s7, reference | 2,423,502 | 0 | 0.378 | 4.538 | 83.0 | 398,498 |
| d4 s7, learned | 2,496,922 | **2,496,922** | 0.798 | 9.570 (**2.11x**) | 37.4 (0.45x) | 415,779 |

The learned leaf calls the model at **every** leaf — 660,481 and 2,496,922
inferences per decision — and roughly doubles the cost of a decision. Read as a
frontier, the +39,105 at five strata costs 1.94x, while spending the same
compute on the chance estimator instead (`d4s7` reference, 3.42x the CPU per
decision) buys +101,171. **On this machine, at equal compute, fixing the chance
estimator is the better purchase, and it is not close.**

For the same reason the CNN is not on the frontier at all. At the s5 learned
arm's 660,481 inferences per decision it would need 2,723 s per decision on one
thread, 298 s using all 12, and would complete **0.12 games per hour against the
student arm's 170.7 — 1,388x slower** for at most the 0.008 of held-out Pearson
the student gives up.

Model bytes, for the record: CNN 12,027,890 (`fnv1a=0x187dcaafbf6479bd`),
LeafNet 2,289,654 (`fnv1a=0x051ae690acf7a212`).
## 10. Verdict

| Field | Value |
| --- | --- |
| **Validity** | **valid** |
| **Outcome, preregistered prediction** | **fail — the prediction is refuted** |
| **Outcome, learned leaf at 5 strata** | **pass** (+39,105, 95% lower +1,138, 37–0–27) |
| **Outcome, learned leaf at 7 strata** | **inconclusive** (+17,281, 95% lower −55,892; no supporting flow-rate movement) |
| **Evidence tier** | `development` — 64 paired whole games per arm on reusable development seeds; not fresh confirmation, not a qualification claim |

Validity rests on things that were checked rather than assumed: both reference
arms reproduce `finding-05`'s published cohort to every digit; `--parity` at
`w = 0` matches the frozen reference on 50 of 50 compared moves; both exported
models pass a stated-tolerance parity gate against a reference **proved
bit-repeatable**; 0 score-identity violations in 26,934 decisions; 0 censored
games in 256; the seven-stratum arms ran at `--max-work 16000000` and their
4,956,614 work per move confirms a completed depth 4; and tuning used a disjoint
leased cohort.

**Conclusion required by the preregistration:** the hypothesis that
chance-node bias caps what leaf-injected foresight can contribute **is wrong**.
It caps extra lookahead — `finding-05` measured that, and this document
reproduces the underlying arm exactly — but a learned leaf is not extra
lookahead. Extra plies are *processed through* the chance expectation, so a
biased expectation corrupts them; a leaf value is *terminal*, so it inherits the
bias of the path that reached it but does not compound it. The measured
relationship is substitution, not amplification.

### What this changes for the research program

- **Do not spend the next experiment on a bigger or better survival model.** The
  binding constraint is arity, not capacity: 615,090 leaves per decision. A
  model 5x more accurate than LeafNet, if it costs 5x more, is unplayable, and
  the 0.008 Pearson between LeafNet and a 5x-larger CNN says the accuracy is not
  there to be had anyway.
- **The learned-leaf and exact-chance axes should not both be bought.** The
  cheapest strong configuration on this evidence is the frozen leaf with seven
  strata; adding the learned leaf to it costs 2.11x for a gain that does not
  clear its bound.
- **The occupancy result is the more actionable one.** Every learned arm here
  moved the board *away* from the 19–20 cell equilibrium. A leaf that predicts
  remaining lifetime accurately still does not steer the policy to the operating
  point where lifetime becomes cheap, because remaining lifetime under *the
  current policy* is not the same target as remaining lifetime under a policy
  that holds flow balance. Targeting occupancy or flow directly — the quantity
  `finding-06` identified — is a different and untested objective.
- **This is a positive result for the state-only, sibling-blind design.**
  `status.md` §4 records sibling extrapolation as the repeated failure mode of
  learned public-state values: the model learns the outcome of the action that
  was played, then deployment asks it to choose among actions it never observed
  equally well. This evaluator never sees an action identity at all — the same
  function scores every legal successor inside the audited chance-averaging
  search — and it cleared a paired 64-game lower bound. That is a design worth
  keeping even though its magnitude is modest.

## 11. Limitations

1. **64 paired games per arm.** Score sd is 51–67% of the mean, so a 64-game
   mean carries roughly ±19,000 to ±35,000 at one standard error. `d5`'s lower
   bound of +1,138 clears zero by very little; this is a result that wants
   replication on a fresh cohort before it is leaned on.
2. **The difference-in-differences interval is wide** (point −21,824, lower
   bound −105,254). The data refute the preregistered *positive* interaction at
   the stated criterion. They do **not** establish that the true interaction is
   negative — a small positive interaction is still inside the interval. The
   claim made here is the refutation, not its mirror image.
3. **The evaluation cohort is previously read development data.** It is
   `finding-05`'s confirmation cohort, chosen deliberately so these arms are
   directly comparable to the published d4s5 and d4s7 figures. It is now doubly
   read and cannot serve as fresh confirmation for anything.
4. **The deployed model is not the model the brief specified**, for the reason
   measured in §2. The CNN is exported and gated but never played a move. The
   student's foresight is close (0.9815 agreement, 0.008 Pearson behind) but it
   is not identical, and a result about LeafNet is not literally a result about
   the CNN.
5. **`w`, `scale` and the head were tuned only at five strata**, on 32 games
   whose bounds all include zero. The direction of that bias is worth stating:
   it optimises the blend where the prediction said the leaf should help
   *least*, so it is conservative with respect to the prediction and cannot have
   manufactured the refutation. A seven-stratum tuning confirmation was
   preregistered as TUNE-B and **was not run** — the compute went to the 2x2
   instead. If a different `w` is optimal at seven strata, `d7` here is an
   underestimate.
6. **No fixed-time comparison and no timing-grade measurement.** Every arm ran
   on a machine shared with other jobs at load averages of 11–19, using at most
   12 threads. `docs/benchmarks.md` requires exclusive resources for a
   performance claim; none is made. Node, leaf and model-evaluation counts are
   exact and machine-independent; the seconds are not.
7. **The 5-strata and 7-strata arms are not equal-compute comparisons.** The
   frontier table in §9 reports the cost so the reader can do the trade
   themselves, but no equal-compute variant of the learned leaf (e.g. shallower
   search plus learned leaf) was run.
8. **Mean 415,779 is far below the 1,050,000 the frozen protocol requires**
   before a candidate may be frozen. Three games of 256 exceeded 1,000,000
   points; that is a tail of one 64-game arm, not a qualification. **No protected or final seed was
   opened, and none is justified by this result.**
9. **This is the repository's simulator**, including the two rise-boundary
   scoring discrepancies in [`audit-01`](audit-01-engine-fidelity.md). Zero
   board clears occurred in 26,934 decisions across all four arms, so
   `audit-01` H2's fifth-drop bonus inflates nothing here.
10. **The oneDNN defect in [`gpu-03`](gpu-03-onednn-conv-nondeterminism.md) was
    found here, not audited generally.** Other measurements in this repository
    that compared against a CPU PyTorch convolution may be affected; that has
    not been checked.
11. **A model contribution record under `research/contributions/` is owed and
    was not written**, because this work was scoped to create files only under
    `approaches/lifetime-objective/learned-leaf/` and `docs/exploratory/` while
    another contributor held `research/`. The coordinator should add one (level
    `L3` for the export path, the affordability measurement, LeafNet, the
    learned-leaf search, the preregistered experiment and this result, plus the
    `gpu-03` host defect).

## 12. Reproduce

```sh
# build (clang++ explicitly; the Makefile's CXX ?= clang++ loses to make's
# builtin CXX=g++, which trips a false -Werror=array-bounds in
# src/core/native/public-behavior.hpp)
./approaches/lifetime-objective/learned-leaf/build.sh

# 0. feasibility: how many leaves does a depth-4 decision actually have?
./build/lifetime-leaf/leaf-probe --seed 0xa5240000 --moves 30 --chance-samples 5 --max-work 3200000
./build/lifetime-leaf/leaf-probe --seed 0xa5240000 --moves 30 --chance-samples 7 --max-work 16000000

# 1. export the CNN and gate it (oneDNN off; reference proved repeatable)
V=.venv-rocm/bin/python
OPENBLAS_NUM_THREADS=1 $V approaches/lifetime-objective/learned-leaf/export_net.py \
    --checkpoint runs/RUN-A51D-net/survival-c128-shuffled.pt \
    --out runs/RUN-A52-LEAF/model/survival-c128-shuffled.d7net
./build/lifetime-leaf/net-check --model runs/RUN-A52-LEAF/model/survival-c128-shuffled.d7net \
    --states runs/RUN-A51D-corpus/mix-d3.states --count 4096 \
    --out runs/RUN-A52-LEAF/parity/cpp-4096.bin --bench 400 --threads 12
OPENBLAS_NUM_THREADS=1 $V approaches/lifetime-objective/learned-leaf/parity_net.py \
    --checkpoint runs/RUN-A51D-net/survival-c128-shuffled.pt \
    --states runs/RUN-A51D-corpus/mix-d3.states \
    --cpp runs/RUN-A52-LEAF/parity/cpp-4096.bin --count 4096 --device cpu \
    --torch-threads 1 --determinism-trials 2 \
    --json runs/RUN-A52-LEAF/parity/parity-cnn.json

# 2. train, export and gate the leaf-affordable student
source approaches/lifetime-objective/gpu/activate.sh
python approaches/lifetime-objective/learned-leaf/train_leaf.py \
    --states runs/RUN-A51D-corpus/all.states \
    --out runs/RUN-A52-LEAF/model/leafnet-h64 --epochs 10 --batch 8192
OPENBLAS_NUM_THREADS=1 $V approaches/lifetime-objective/learned-leaf/export_leaf.py \
    --checkpoint runs/RUN-A52-LEAF/model/leafnet-h64.pt \
    --out runs/RUN-A52-LEAF/model/leafnet-h64.d7leaf
./build/lifetime-leaf/leaf-check --model runs/RUN-A52-LEAF/model/leafnet-h64.d7leaf \
    --states runs/RUN-A51D-corpus/mix-d3.states --count 4096 \
    --out runs/RUN-A52-LEAF/parity/leaf-cpp-4096.bin --bench 400000 --threads 1
OPENBLAS_NUM_THREADS=1 $V approaches/lifetime-objective/learned-leaf/parity_leaf.py \
    --checkpoint runs/RUN-A52-LEAF/model/leafnet-h64.pt \
    --states runs/RUN-A51D-corpus/mix-d3.states \
    --cpp runs/RUN-A52-LEAF/parity/leaf-cpp-4096.bin \
    --teacher-cpp runs/RUN-A52-LEAF/parity/cpp-4096.bin --count 4096 \
    --json runs/RUN-A52-LEAF/parity/parity-leaf.json

# 3. CHECK gate and leaf-scale diagnostic
./build/lifetime-leaf/search --parity --seed-start 0xa5240100 --parity-games 2 --parity-moves 25
./build/lifetime-leaf/search --model runs/RUN-A52-LEAF/model/leafnet-h64.d7leaf \
    --states runs/RUN-A51D-corpus/mix-d3.states --leaf-stats 20000 --leaf-value lifetime

# 4. the cohorts (reference arms + tuning, then the two learned arms)
THREADS=12 ./approaches/lifetime-objective/learned-leaf/run-stage1.sh
THREADS=12 W=0.50 SCALE=3400 KIND=lifetime \
    ./approaches/lifetime-objective/learned-leaf/run-stage2.sh

# 5. the tables
OPENBLAS_NUM_THREADS=1 $V approaches/lifetime-objective/learned-leaf/analyze.py \
    --label s5ref=runs/RUN-A52-LEAF/eval/s5-w000.json \
    --label s5learned=runs/RUN-A52-LEAF/eval/s5-learned.json \
    --label s7ref=runs/RUN-A52-LEAF/eval/s7-w000.json \
    --label s7learned=runs/RUN-A52-LEAF/eval/s7-learned.json \
    --pair s5ref:s5learned --pair s7ref:s7learned \
    --pair s5ref:s7ref --pair s5learned:s7learned \
    --did s5ref:s5learned:s7ref:s7learned \
    --json runs/RUN-A52-LEAF/eval/twobytwo.json
```

### Seed lease

`SEEDLEASE-A52-LEAF` = `0xa5240000`–`0xa5247fff`. Opened:
`0xa5240100`–`0xa5240101` (parity probe), `0xa5240200`–`0xa5240207` (pilot),
`0xa5241000`–`0xa524101f` (tuning). `0xa5242000`–`0xa524201f` was reserved for
the seven-stratum tuning confirmation and **was not opened**. The binary refuses
to start outside the lease except on the declared fixed evaluation cohort
`0xa51d1000`–`0xa51d103f`, which is `finding-05`'s already-read confirmation
cohort and was chosen precisely so these arms are comparable to it.

### Environment

AMD clang 23.0.0git, `-O3 -std=c++20 -pthread -Wall -Wextra -march=native`.
AMD Ryzen AI MAX+ 395 (Zen 5), 16 physical cores / 32 logical, 125 GiB unified
memory, Radeon 8060S (gfx1151); `torch 2.13.0+rocm7.1` for training and the
parity references. Frozen source hashes are recorded at build time in
`build/lifetime-leaf/reference-sources.sha256`, and the build refuses to proceed
if the generated depth-4 reference copy differs from the original by more than
its single entry-point line. Every cohort shared the machine with other jobs at
load averages of roughly 11–19 and used at most 12 threads; no timing-grade
claim is made.
