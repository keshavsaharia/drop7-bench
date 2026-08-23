# Finding 11 — Distilling the *fair* planner: the corpus was right, the teacher was not

**Status:** exploratory. Built and measured in this checkout on 2026-08-20.
**Namespace:** `approaches/lifetime-objective/planner-distill/`, run
`runs/RUN-A526-DISTILL-46d93fb7956e/`, seed lease `SEEDLEASE-A52-DISTILL` =
`0xa5260000`–`0xa526ffff`.
**Nothing in `docs/research/`, `artifacts/`, `research/`, `runs/RUN-20260820T*`,
or any existing approach source was modified by this work.** Files were created
only under `approaches/lifetime-objective/planner-distill/`,
`build/planner-distill/`, `runs/RUN-A526-DISTILL-*/` and `docs/exploratory/`.

**Verdict: run validity `valid`, scientific outcome `fail`, evidence tier
`development`.** The preregistered gate is in
`approaches/lifetime-objective/planner-distill/PREREGISTRATION.md`, written
before any student was trained, and is reproduced verbatim in §4.

## Summary

| Claim | Evidence |
| --- | --- |
| **The teacher barely beats the policy it was meant to teach** | Over **160 paired master tapes**, the legal fair planner at `H = 5, K = 256` scores +16,777 points against fair depth 4 with a 95% lower bound of **−19,143**, and survives +5.18 moves with a lower bound of **−4.56**. Its occupancy slope is **+1.4798 against +1.4813** — the same number to three decimals |
| **finding-07's eight-tape cohort overstated the level and the gap** | Fair D4 lives 117.75 moves on those eight tapes and **93.78** on 160 fresh ones, landing on finding-01's independent 94.06. The planner's clears-per-move edge falls from **+0.1213 to +0.0405** per game (lower bound +0.0006) |
| **Sibling coverage — the dominant historical failure — is genuinely fixed** | **108,462** labelled siblings, **0** missing, **0** illegal columns labelled, action completeness **1.0000**, all under common random numbers inside the planner *and* in the environment |
| **The teacher's own argmax is only 83% reproducible** | Splitting its 256 completions in half, the two halves pick the same column **0.8318** of the time — but the columns they cannot separate are worth **0.0765 discs** apart out of a 2.26-disc spread, and half the teacher's compute already scores a normalised regret of **0.0073**. **The label is sharp about value and blunt about argmax**; nothing here had measured that |
| **The gate fails, in every fold, at both model sizes** | Held-out top-1 **0.4851** against fair D4's **0.6111**, pairwise 0.7364 against 0.7961, normalised regret 0.1915 against 0.1269; **0 of 5** origin folds won. `leaf-h256` has 4× the parameters and lands within 0.002 |
| **Calibration is good; ranking is not** | Held-out Pearson against the teacher residual **0.8742**, mean absolute error 1.699 discs against a 3.958-disc target spread. The repository's oldest lesson, reproduced on a *fair* teacher |
| **The student is not empty — it is being compared as a policy, not as a leaf** | Fair D4 spends 615,090 leaf evaluations per decision; the student spends one per sibling. Blended into D4's ranking at a weight fixed on validation, it cuts normalised regret **0.1269 → 0.1105** and raises pairwise **0.7961 → 0.8117** on held-out roots |
| **No gameplay was run** | The gate failed, so per the preregistration no cohort was opened. The shared evaluation cohort `0xa51d1000`–`0xa51d103f` is untouched |

## 1. Why distillation was worth retrying

[`audit-05`](audit-05-optimistic-curriculum.md) §4 classifies seventeen failed
learned models in this repository by primary failure mode:

| Class | Count | Experiments |
| --- | ---: | --- |
| (iii) sibling coverage / within-root discrimination | **6** | 5, 9, 11, 13, 14, 17 |
| (i) representation / information gap | **3** | 1, 2, 10 |
| (iv) objective mismatch | **3** | 3, 7, 12 |
| (ii) distribution shift | **1** | 6 |
| (v) optimisation / bad basin | **0** | — (actively refuted twice) |

Oracle distillation is failure 1 in that table: train cross-entropy 0.480 →
held-out 3.282, top-1 **0.218**, with distribution shift *controlled*. audit-05
§5.4 assembles four independent measurements that all say the same thing about
why: the oracle's advantage was "overwhelmingly … exploitation of the realised
tape", i.e. a function of information the student can never see.

Something changed. [`finding-07`](finding-07-fair-planning-ceiling.md) built a
**legal** receding-horizon planner —
`approaches/lifetime-objective/flow-ceiling/fair-planner.hpp` arm B. It samples
`K` completions of the hidden board, solves an exact `H`-move window against
each under common random numbers, and plays the argmax of the mean. It never
reads a hidden value or a future disc, and a self-test proves it by replacing
every hidden value *and* the entire future and requiring an identical column.
Its advantage over fair depth 4 is therefore **public by construction**, and it
is expensive enough (hundreds of exact window solves per decision) that
amortising it is exactly what a learned evaluator is for.

That makes it the first distillation target in this repository whose teacher
signal is, in principle, representable by a public student.

> **This is a hypothesis about why the prior distillations failed held-out
> gates, not a proven cause.** None of the seventeen failures was rerun with a
> fair teacher. What can be established here is narrower: whether a public
> student can absorb the advantage of a teacher whose advantage is definitely
> public.

## 2. Method

### 2.1 The teacher, and the budget that set its configuration

The teacher is finding-07's arm B with objective *numbered discs cleared*, one
move committed per decision, 400-move cap, both privileges removed
(`latent_known = false`, `tape_known = false`).

Cost was measured before the configuration was chosen, on this host under the
contention described in §8:

| configuration | seconds per decision |
| --- | ---: |
| `H = 5, K = 64` | 0.146 |
| **`H = 5, K = 256`** (chosen) | **0.567** |
| `H = 7, K = 64` | 4.315 |
| `H = 7, K = 256` (finding-07's headline) | ~17, extrapolated; not run |

Those are single-threaded probe figures on an already-busy machine. The 160-game
corpus run itself averaged **1.5322 s per decision** across eight worker threads
at load averages near 50, and took 3,128 s of wall clock; the ratio to the probe
is contention, not a different amount of work (nodes are exact and
machine-independent).

At finding-07's headline `H = 7, K = 256` a 64-game corpus costs about six hours
of the machine for roughly 12,000 roots. At `H = 5, K = 256` the same six hours
buys an order of magnitude more roots. `K = 256` rather than `K = 64` was kept
deliberately: the *within-root* label noise is what six of the seventeen
documented failures died of, and §3.3 measures that noise directly.

**The student is taught toward the reduced planner's ceiling, and is compared
against that ceiling — never against finding-07's `H = 7` numbers.**

### 2.2 What one corpus row is

One row is one *root*: a 576-byte record holding the public board, the visible
next disc, the moves until the next rise, the legal mask, and then, **for every
legal column**:

- `value[c]` — the mean over the same `K` completions of the exact horizon-`H`
  window optimum that starts by playing `c`;
- `immediate[c]` — the mean over the same completions of the discs the move
  itself clears, so `value − immediate` is the mean value of the *afterstate*;
- `value_lo[c]`, `value_hi[c]` — the same quantity from the first and second
  half of the completions separately, which is what makes §3.2 possible;
- the realised afterstate board, its clears, reveals, occupancy, next visible
  disc and moves-until-rise.

`fairDecision` in `fair-planner.hpp` already computes the per-column mean and
then discards everything except the argmax. This corpus keeps the vector.
Common random numbers hold twice over: inside the planner every column is scored
against the same `K` sampled hidden boards, and in the environment every
column's realised afterstate is resolved against the same true master tape.

### 2.3 What the student is, and what it is not

    s_c  =  immediate_c  +  f(afterstate_c)

`f` is a **state-only afterstate evaluator**, not a policy head over columns.
Every learned ranker in this repository that conditioned on action identity
failed to rank unplayed siblings; a function of the successor state cannot use
action identity as a shortcut because it never sees one. `immediate_c` is
supplied by the search rather than learned, because the discs a move clears are
not a function of the state after it.

The loss is a **within-root** listwise softmax cross-entropy against the
planner's value vector, plus an explicit gap-weighted pairwise margin term, with
absolute regression kept only as a weak scale anchor. That ordering is
deliberate: audit-05's most repeated lesson is that low value error on visited
states never once implied good root-action ranking here (experiment 12 had every
sibling labelled, global Spearman 0.839, and 15.4% within-root top-1).

Three models were trained. Only the first can play:

| model | shape | role |
| --- | --- | --- |
| `leaf-h64` | EmbeddingBag(8902, 64) → 32 → 2 | the deployable size; the leaf budget is ~1 µs per state |
| `leaf-h256` | EmbeddingBag(8902, 256) → 96 → 2 | is the leaf *budget* binding, or the feature space? |
| `cnn-c128b6` | 18-plane residual CNN, warm-started from `runs/RUN-A51D-net/survival-c128-shuffled.pt` (42 tensors adopted) | representation ceiling; ~4.1 ms per state, ~2,900× over the leaf budget. **Interrupted at one epoch** — see §6.3 |
| `cnn-c64b4` | the same, 64 channels and 4 blocks, no warm start | the affordable substitute for the interrupted arm |

The feature space is `learned-leaf/leaf_features.py`, reused unchanged, and the
native inference path reuses that approach's `leafnet.hpp` feature builder. Only
the leaf-shaped models are exportable and only they could ever play; the CNN arm
exists to separate "the target is not learnable from public state" from "the
target is learnable but not inside the leaf budget".

### 2.4 Chance realisations, so the gate measures what the search averages

The corpus stores one realised afterstate per sibling; the label is a mean over
completions. Scoring a student on one draw compares an average against a
sample, and the mismatch can only make the student look worse. The deployed
search averages its leaf over five or seven stratified reveal outcomes, so
`expand` draws seven further independent realisations of the reveal randomness
per sibling from its exact public marginal (i.i.d. uniform 1..7, independent of
everything public — the same draw `sampleWindow` makes), and both training and
the gate use the average.

## 3. Results

### 3.1 The teacher is barely stronger than the policy it was supposed to teach

**This is the result that matters most, and it was not the one being tested.**

[`finding-07`](finding-07-fair-planning-ceiling.md) reports every arm on eight
master tapes, seeds `0xa5230000`–`0xa5230007`. Those eight are easy: fair depth
4 survives 117.75 moves on them against 94.06 on the 64 fresh base-engine seeds
of [`finding-01`](finding-01-score-is-survival.md). This work therefore played
both the teacher and the unchanged fair depth-4 comparator over **160 fresh
master tapes**, paired at the seed, with the same 400-move cap and the same
engine.

| | fair planner, `H = 5, K = 256` | fair depth 4 | paired delta | one-sided 95% bounds |
| --- | ---: | ---: | ---: | ---: |
| games | 160 | 160 | 160 paired | |
| mean score | 337,857 | 321,080 | **+16,777** | **[−19,143, +53,058]** |
| median score | 269,074 | 285,672 | | |
| Q25 score | 171,379 | 193,245 | | |
| min / max score | 69,214 / 1,463,707 | 102,593 / 931,369 | | |
| score sd | 236,935 | 176,532 | | |
| mean moves | 98.96 | 93.78 | **+5.18** | **[−4.56, +15.03]** |
| median moves | 80.0 | 84.5 | | |
| Q25 moves | 51.5 | 59.25 | | |
| censored at the cap | 1 | 0 | | |
| clears / move (pooled) | 2.0471 | 1.9723 | | |
| clears / move (per game) | | | **+0.0405** | **[+0.0006, +0.0785]** |
| reveals / move (pooled) | 1.1444 | 1.0897 | | |
| reveals / move (per game) | | | **+0.0283** | **[−0.0028, +0.0582]** |
| mean occupancy (cells) | 22.78 | 23.95 | | |
| **occupancy slope (cells per five-move cycle)** | **+1.4798** | **+1.4813** | **−0.0015** | |
| wins / ties / losses on score | 85 / 0 / 75 | | | |
| wins / ties / losses on moves | 75 / 21 / 64 | | | |
| score-identity violations | 0 | 0 | | |
| incomplete windows | 0 | — | | |

20,000-resample percentile bootstrap over whole games, resampling seeds.

**Data role.** `lease-map.md` registers `SEEDLEASE-A52-DISTILL` with the
**training** role, and the student was fitted on these same games. The
teacher-versus-comparator comparison above involves **no fitted model** — both
arms are frozen search procedures playing identical master tapes — so it is a
clean 160-game paired measurement, but it is development-tier diagnostic
evidence and is explicitly not a confirmation cohort.

**On score and on lifetime, the teacher's advantage over fair depth 4 is not
distinguishable from zero.** It wins 85 games of 160 on score and 75 of 160 on
moves. Only the flow rate has a lower bound above zero, and it clears zero by
0.0006 clears per move. The occupancy slope — finding-06's cleanest single
diagnostic — is **identical to three decimal places**: +1.4798 against +1.4813.
Both boards fill at the same rate; both policies die of the same thing.

Clears per move conditioned on how full the board was before the move, over all
160 games of each arm (reconstructed from the occupancy trace by disc
conservation, which reproduces the engine's own clear total to within 3%):

| occupied cells | fair planner | fair depth 4 |
| --- | ---: | ---: |
| 5–9 | 0.567 *(n=789)* | 0.465 *(n=727)* |
| 10–14 | 1.601 *(n=1295)* | 1.401 *(n=815)* |
| 15–19 | 1.934 *(n=3445)* | 1.779 *(n=3012)* |
| 20–24 | 2.250 *(n=4402)* | 2.205 *(n=4027)* |
| 25–29 | 2.465 *(n=3316)* | 2.383 *(n=3399)* |
| 30–34 | 2.462 *(n=1560)* | 2.314 *(n=1635)* |
| 35–39 | 2.295 *(n=662)* | 2.130 *(n=820)* |
| 40–44 | 2.326 *(n=282)* | 2.034 *(n=413)* |
| 45–49 | 1.958 *(n=72)* | 2.187 *(n=155)* |
| **required** | **2.400** | **2.400** |

The teacher is uniformly a little better and nowhere decisively better. For
scale, finding-07's clairvoyant arm reaches 3.31 at 20–24 cells and 4.46 at
25–29 against fair D4's 2.10 and 2.59; here the legal planner reaches 2.250 and
2.465 against 2.205 and 2.383.

#### What this corrects in finding-07

finding-07 states its own eight-game cohorts as its dominant limitation, and the
correction is in the direction it warned about. Same policy, same objective,
same engine, same cap, both at `H = 5, K = 256`:

| | finding-07, 8 tapes | this work, 160 tapes |
| --- | ---: | ---: |
| fair D4, mean moves | 117.75 | **93.78** |
| fair D4, clears / move | 2.0467 | **1.9723** |
| fair D4, occupancy slope | +0.990 | **+1.481** |
| arm B `H = 5, K = 256`, mean moves | 128.75 | **98.96** |
| arm B `H = 5, K = 256`, clears / move | 2.1680 | **2.0471** |
| arm B `H = 5, K = 256`, occupancy slope | +0.583 | **+1.480** |
| **arm B minus fair D4, clears / move** | **+0.1213** | **+0.0748 pooled, +0.0405 per game (95% lower bound +0.0006)** |

Two separate effects, and both matter. The eight tapes are easy — fair D4 lives
25% longer on them than on a fresh sample, and this work's 160-game figure of
93.78 moves lands almost exactly on finding-01's independent base-engine figure
of 94.06. And the *gap* is smaller than eight paired games could resolve: on 160
tapes the fair planner's edge in occupancy slope, which finding-07 measured as
+0.583 against +0.990, is **zero**.

**None of this touches finding-07's decomposition**, which is stated at a matched
horizon on paired seeds and is a ratio rather than a level. What it removes is
the *level*: at `H = 5, K = 256` the legal planner is not 93% of the way to flow
balance on a representative sample, it is 85.3%, and it is barely ahead of the
policy it was proposed as a teacher for.

**This is the dominant explanation for §3.4's failed gate, and it is not a
property of the student.** A distillation can only transfer what the teacher
has. At this configuration the teacher has +0.04 clears per move and a lifetime
advantage whose 95% interval contains zero.

### 3.2 The corpus, and its sibling completeness

160 on-policy teacher games, seeds `0xa5260000`–`0xa526009f`, 400-move cap.

| | value |
| --- | ---: |
| roots (decisions) | **15,833** |
| origin games | 160 |
| labelled (root, column) pairs | **108,462** |
| mean legal columns per root | 6.8504 |
| **missing labels on legal columns** | **0** |
| **illegal columns carrying a label** | **0** |
| **action completeness** | **1.0000** |
| completions per decision that solved inside the budget | 256 of 256 (the record clamps the counter at 255) |
| incomplete windows in the whole run | **0** |
| score-identity violations | **0** |
| corpus size | 8.7 MiB, plus 53.0 MiB of 867,696 expanded afterstates |
| split-half argmax agreement over the whole corpus | 0.8290 |

Every legal sibling at every root carries an exact per-column value computed
against the same 256 sampled hidden boards. That is the object the six class
(iii) failures in audit-05 never had.

### 3.3 The ceiling nobody had measured: how well does the teacher agree with itself?

The two independent halves of the 256 completions give two independent
estimates of the same value vector. Their disagreement is the noise floor of
the label, and no student can agree with a label more often than the label
agrees with itself.

On the 1,445 held-out roots:

| | value |
| --- | ---: |
| **argmax agreement, first 128 completions vs second 128** | **0.8318** |
| half agrees with the full 256-completion argmax | 0.9128 |
| mean absolute value gap between the halves | 0.2511 discs |
| mean root value spread (best − worst legal column) | 2.2643 discs |
| mean top-1 minus top-2 margin | 0.5197 discs |
| median top-1 minus top-2 margin | 0.3418 discs |
| **cost of a half-disagreement, judged by the full label** | **0.0765 discs**, i.e. 7.6% of the root spread |

Read the first row against the last. The teacher **disagrees with itself on one
root in six** (agreement 0.8318), but when it does, the two columns it cannot
separate are worth **0.0765 discs** apart out of a **2.2643-disc** root spread.
**The label is sharp about value and blunt about argmax**, which is exactly the
regime in which a top-1 target is a poor summary of what the teacher knows — and
it is the first quantitative statement of that in this repository.

Scored as if it were a student, half the teacher's own compute gives:

| arm | top-1 | top-2 | pairwise | normalised regret |
| --- | ---: | ---: | ---: | ---: |
| teacher, 128 of its own 256 completions | 0.9114 | 0.9882 | 0.9650 | **0.0073** |

Those figures are **optimistic and are not the ceiling**: the half is a subset of
the whole, so its agreement with the full-256 argmax is nested rather than
independent. **The independent number is the split-half agreement, 0.8318**, and
that is what the preregistered gate's ceiling clause uses. The row is quoted for
its regret column, which is the informative part: half the teacher's compute already
picks columns worth **0.0073** of the root spread less than the best, against the
student's 0.1915 and fair depth 4's 0.1269.

### 3.4 The offline gate: FAIL

Held-out **16 origin games**, 1,445 roots, 9,879 labelled siblings, split by
whole origin game. Fair depth 4 completed depth 4 on **100%** of those roots.

Every arm below is scored with the *same* tie rule — the argmax of its own value
vector, ties to the lower column index — which is also the planner's own rule.
That matters: fair D4's *emitted* column, which breaks ties centre-first through
`kColumnOrder`, matches the planner on 0.5903 of these roots rather than 0.6111.
The consistent-tie figure is used throughout so that no arm is judged by a
different rule from any other.

| arm | top-1 | top-2 | pairwise | normalised regret | roots |
| --- | ---: | ---: | ---: | ---: | ---: |
| teacher, half its completions *(nested; see §3.3)* | 0.9114 | 0.9882 | 0.9650 | **0.0073** | 1,445 |
| **fair depth 4** *(the comparator)* | **0.6111** | 0.8201 | **0.7961** | **0.1269** | 1,445 |
| student `leaf-h64`, 8 realisations, recalibrated *(headline)* | 0.4851 | 0.7003 | 0.7364 | 0.1915 | 1,445 |
| student `leaf-h64`, 1 realisation | 0.5024 | 0.7038 | 0.7366 | 0.1901 | 1,445 |
| student `leaf-h256`, 8 realisations, recalibrated | 0.4837 | 0.7017 | 0.7381 | 0.1886 | 1,445 |
| student `leaf-h256`, 1 realisation | 0.4941 | 0.7087 | 0.7416 | 0.1851 | 1,445 |
| student `cnn-c64b4`, 4 epochs, **under-trained** | 0.3571 | 0.5218 | 0.6266 | 0.3127 | 1,445 |
| student `cnn-c64b4`, 1 realisation | 0.4042 | 0.5834 | 0.6424 | 0.2755 | 1,445 |
| reference: immediate clears only | 0.3571 | 0.5149 | 0.4222 | 0.3367 | 1,445 |
| reference: immediate − 0.1 × occupancy | 0.3564 | 0.5128 | 0.4213 | 0.3376 | 1,445 |
| reference: survival + immediate | 0.3578 | 0.5156 | 0.4233 | 0.3360 | 1,445 |

**The student beats every trivial reference by a wide margin and loses to fair
depth 4 on all three headline statistics.** It is not a failure to learn
anything: a constant-leaf ranker gets 0.357 top-1 and 0.422 pairwise, and the
student gets 0.485 and 0.736. It is a failure to clear the preregistered bar.

Quadrupling the model does nothing: `leaf-h256` has 2,304,034 parameters against
`leaf-h64`'s 571,938 and lands within 0.002 of it on every statistic. That
reproduces audit-05's class-(v) verdict — optimisation and capacity are not the
binding constraint here — on a new teacher.

**The CNN row is under-trained and must not be read as a representation
result.** It received 4 epochs against the leaf models' 120, because a single
CNN epoch on this corpus costs 425–1,559 s against the leaf model's 1.2–2.2 s;
its validation top-1 was still rising when the budget closed (0.4148 → 0.4188 →
0.4330 → 0.4369). The interrupted warm-started `c128b6` arm reached validation
top-1 **0.5071 after a single 2,151 s epoch**, which is the closest thing here
to a representation-ceiling measurement and is still below the leaf model's
0.5300. Nothing observed suggests the 18-plane CNN unlocks the target; nothing
observed rules it out either. See §6.3.

**By origin fold, the student loses in 5 of 5:**

| fold | roots | student top-1 | fair D4 top-1 | student pairwise | fair D4 pairwise |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 535 | 0.5009 | 0.6411 | 0.7396 | 0.8027 |
| 1 | 280 | 0.4893 | 0.5964 | 0.7365 | 0.7855 |
| 2 | 205 | 0.5220 | 0.6000 | 0.7357 | 0.8039 |
| 3 | 210 | 0.4762 | 0.5905 | 0.7371 | 0.7741 |
| 4 | 215 | 0.4140 | 0.5860 | 0.7280 | 0.8068 |

**Calibration is good, and is not the problem.** The value head's held-out
Pearson against the teacher residual is **0.8742** with a mean absolute error of
**1.699 discs** against a target standard deviation of 3.958. The reliability
table shows a mild, monotone under-prediction of 5–8% across the whole range
(predicted 4.54 → actual 4.81 in the lowest decile; predicted 16.10 → actual
17.39 in the highest), which the validation-fitted affine map removes. This is
the repository's oldest lesson restated on new data: **a well-calibrated value
head is not a good action ranker.**

Breakouts show the deficit is uniform rather than concentrated:

| rise phase (moves until rise) | roots | student top-1 |
| ---: | ---: | ---: |
| 1 | 289 | 0.5156 |
| 2 | 289 | 0.4844 |
| 3 | 289 | 0.5190 |
| 4 | 289 | 0.4360 |
| 5 | 289 | 0.4706 |

| occupied cells | roots | student top-1 |
| ---: | ---: | ---: |
| 5–9 | 68 | 0.5000 |
| 10–14 | 65 | 0.3385 |
| 15–19 | 280 | 0.4750 |
| 20–24 | 412 | 0.4830 |
| 25–29 | 355 | 0.5070 |
| 30–34 | 179 | 0.4581 |
| 35–39 | 57 | 0.5789 |
| 40–44 | 24 | 0.5417 |

| legal columns | roots | student top-1 |
| ---: | ---: | ---: |
| 4 | 23 | 0.5652 |
| 5 | 42 | 0.6190 |
| 6 | 31 | 0.5806 |
| 7 | 1,339 | 0.4750 |

Action stability across independent scenario halves is high for the student
because a frozen deterministic evaluator has no sampling noise of its own: it
agrees with the teacher's first-half argmax 0.4747 of the time and with the
second half 0.4685, a gap of **0.0062**. The instability in this pipeline is in
the *label*, not the student.

### 3.5 The comparison is unequal, and the unequal part is informative

Fair depth 4 spends **615,090 leaf evaluations per decision**; the student
spends **one per sibling**. The preregistered gate compares them as
stand-alone rankers, and that is the comparison it was fixed to make, but losing
it does not by itself mean the student is uninformative — its intended use is as
the leaf *inside* that search.

The cheap version of the deployment question, on the same held-out roots, with
the two scores standardised within each root:

    blended_c = z(fairD4_c) + lambda * z(student_c)

| λ | top-1 | top-2 | pairwise | normalised regret |
| ---: | ---: | ---: | ---: | ---: |
| 0.00 *(fair D4 alone)* | 0.6111 | 0.8201 | 0.7961 | 0.1269 |
| 0.05 | 0.6104 | 0.8111 | 0.8015 | 0.1190 |
| 0.10 | 0.6180 | 0.8166 | 0.8049 | 0.1164 |
| 0.15 | 0.6208 | 0.8201 | 0.8068 | 0.1137 |
| **0.20** | **0.6263** | 0.8263 | 0.8099 | **0.1102** |
| 0.30 | 0.6256 | **0.8311** | 0.8117 | 0.1105 |
| 0.50 | 0.6180 | 0.8221 | **0.8136** | 0.1154 |
| 1.00 | 0.6021 | 0.8069 | 0.8056 | 0.1216 |
| 2.00 | 0.5682 | 0.7855 | 0.7875 | 0.1392 |
| ∞ *(student alone)* | 0.4775 | 0.7031 | 0.7352 | 0.1958 |

**The curve has an interior optimum**, and the same sweep was run on the
*validation* origins so the weight is not chosen by looking at the held-out
numbers:

| λ | validation top-1 | validation pairwise | validation regret |
| ---: | ---: | ---: | ---: |
| 0.00 *(fair D4 alone)* | **0.6017** | 0.7946 | 0.1202 |
| 0.20 | 0.5994 | 0.8045 | 0.1100 |
| **0.30** | 0.5970 | 0.8073 | **0.1084** |
| 0.50 | 0.5907 | **0.8115** | 0.1101 |
| ∞ *(student alone)* | 0.5000 | 0.7305 | 0.1715 |

Selecting λ on validation by normalised regret gives **λ = 0.3**, and applying
that unchanged to the held-out roots gives, against fair depth 4 alone:

| statistic | fair D4 | fair D4 + 0.3 × student | change |
| --- | ---: | ---: | ---: |
| top-1 | 0.6111 | 0.6256 | +0.0145 |
| top-2 | 0.8201 | 0.8311 | +0.0110 |
| pairwise | 0.7961 | 0.8117 | +0.0156 |
| **normalised regret** | 0.1269 | **0.1105** | **−13%** |

**Stated carefully.** *Regret* and *pairwise accuracy* improve on **both**
splits, so the student does hold ranking signal fair depth 4 does not already
have. *Top-1* improves on the held-out roots but **does not** improve on
validation (0.6017 → 0.5994 at best), so the top-1 gain is not corroborated and
is not claimed. And all of this is a **root-level** statistic on 1,445 roots from
16 games: `docs/methodology.md` is explicit that a root panel cannot supply a
complete-game interval, and none is offered.

What it does support is a localisation. The label is not empty and the student
is not vacuous; what failed is the specific thing the gate fixed — the student
as a **stand-alone** ranker against a depth-4 search that spends six hundred
thousand times more evaluation per decision.

### 3.6 Inference cost, and whether this student could have played at all

Measured on this host, one thread, one state at a time — the way an expectimax
leaf calls it, and under the contention of §8:

| | value |
| --- | ---: |
| parameters | 571,938 |
| **inference** | **4.171 µs and 2.650 µs per state** in two measurements under different contention |
| leaf evaluations per decision, depth 4 / 5 strata | 615,090 |
| leaf evaluations per decision, depth 4 / 7 strata | 2,271,280 |
| **implied cost per decision, 5 strata** | **1.63–2.57 s** |
| **implied cost per decision, 7 strata** | **6.02–9.47 s** |
| frozen reference decision, 5 strata | 0.887 s |
| exported vs PyTorch, 4,096 real afterstates | max absolute difference **8.6e-6** |
| horizontal-reflection asymmetry | mean 0.510 against a prediction sd of 3.365 |
| Pearson with the frozen `fairLeaf` on the same states | 0.374 |
| equal-influence blend scale (sd ratio) | 8,381 |

`learned-leaf` measures **1.33 µs** for exactly this shape on an idle machine;
the 4.171 µs here is the same model under a load average near 50, and the ratio
should be read as a contention artefact rather than as a property of the model.
At 1.33 µs a seven-stratum depth-4 decision would add 3.0 s of model time to a
reference decision that already costs seconds at that width, which is
expensive but affordable — **the reason no gameplay cohort was run is the gate,
not the clock.**

The reflection asymmetry is worth flagging: mirror augmentation was applied per
batch with probability 0.5 rather than enforced, and the residual 15% column
preference means the model has not fully exploited a symmetry the rules
guarantee. It does not create an inconsistency at deployment, because the frozen
search canonicalises the state before the leaf sees it, but it is capacity spent
on nothing.

## 4. The preregistered gate, verbatim

From `approaches/lifetime-objective/planner-distill/PREREGISTRATION.md` §4,
written before any student was trained and before any offline number was
computed:

> Let `T1_s`, `PW_s`, `REG_s` be the student's held-out top-1, pairwise accuracy
> and normalised regret under the eight-realisation score, and `T1_d4`, `PW_d4`,
> `REG_d4` the same statistics for the unmodified fair depth-4 search on exactly
> the same roots.
>
> | Outcome | Criterion |
> | --- | --- |
> | **PASS** | `T1_s > T1_d4` **and** `PW_s > PW_d4` **and** `REG_s < REG_d4`, **and** the student wins on top-1 in at least 4 of the 5 held-out origin folds, **and** `T1_s ≥ 0.60 × (teacher split-half agreement)` |
> | **PARTIAL** | the student beats fair D4 on pairwise accuracy and regret but not top-1, or wins in aggregate but in fewer than 4 of 5 folds |
> | **FAIL** | the student does not beat fair D4 on at least two of the three headline statistics |
>
> **Gameplay is run only on PASS.**

Evaluated mechanically by `report.py`:

| quantity | value |
| --- | ---: |
| `T1_s` | 0.4851 |
| `T1_d4` | 0.6111 |
| `PW_s` | 0.7364 |
| `PW_d4` | 0.7961 |
| `REG_s` | 0.1915 |
| `REG_d4` | 0.1269 |
| headline statistics beaten | **0 of 3** |
| folds won on top-1 | **0 of 5** |
| `0.60 × ceiling` | 0.4991 |
| **outcome** | **FAIL** |

**No gameplay cohort was opened.** The shared evaluation cohort
`0xa51d1000`–`0xa51d103f` was not touched by this work.

## 5. Verdict

**Run validity `valid`. Scientific outcome `fail`. Evidence tier
`development`.**

The preregistered prediction is **refuted on its own terms**: the distilled
student ranks the fair planner's legal siblings worse than the unmodified fair
depth-4 search does, on held-out origin games, on all three headline statistics,
in all five origin folds, at both model sizes. That is the eighteenth recorded
negative in this repository and it is reported as one.

Stated precisely, with the direction of every bound made explicit:

1. **The teacher's advantage over its comparator is not distinguishable from
   zero on the quantity that matters.** Over 160 paired master tapes the fair
   receding-horizon planner at `H = 5, K = 256` scores +16,777 points (95% lower
   bound **−19,143**) and survives +5.18 moves (lower bound **−4.56**) against
   fair depth 4. Its occupancy slope is +1.4798 against +1.4813 — the same
   number. Only clears per move clears zero, by +0.0006.

2. **finding-07's eight-tape cohort overstated both the level and the gap.**
   Fair D4 lives 117.75 moves on those eight tapes and 93.78 on 160 fresh ones;
   the planner's clears-per-move edge falls from +0.1213 to +0.0405 per game.
   finding-07 named small cohorts as its dominant limitation and the correction
   runs in the direction it warned about.

3. **The label is complete and sharp about value, and blunt about argmax.**
   Sibling coverage is 1.0000 with zero missing labels across 108,462 labelled
   siblings — the class-(iii) defect that killed six of seventeen prior models
   is genuinely absent. But the teacher agrees with its own argmax only
   **0.8318** of the time when its 256 completions are split in half, because
   the two columns it cannot separate are worth **0.0765 discs** apart out of a
   2.26-disc root spread. **A top-1 target is a poor summary of what this
   teacher knows**, and nothing in this repository had measured that before.

4. **The student learns the value function and not the ranking.** Held-out
   Pearson against the teacher residual is **0.8742** with a mean absolute error
   of 1.699 discs against a 3.958-disc target spread, and the reliability curve
   is monotone with a 5–8% under-prediction. Top-1 is 0.4851 against fair D4's
   0.6111. This is the repository's oldest lesson reproduced on a *fair*
   teacher: a well-calibrated value head is not a good action ranker.

5. **Capacity and optimisation are not the constraint.** `leaf-h256` has 4.03×
   the parameters of `leaf-h64` and lands within 0.002 of it on every held-out
   statistic. That reproduces audit-05's class-(v) count of zero on new data.

6. **The student is not empty, and the failure is partly one of comparison.**
   Fair D4 spends 615,090 leaf evaluations per decision and the student spends
   one per sibling. Blended into fair D4's ranking at a weight chosen on the
   validation origins, the student **improves** the search's agreement with the
   teacher: normalised regret 0.1269 → 0.1105 and pairwise 0.7961 → 0.8117 on
   held-out roots, with regret and pairwise improving on both splits. The
   top-1 improvement appears on test but not on validation and is not claimed.

### What this does not establish

- It does not test finding-07's headline teacher. `H = 7, K = 256` was
  measured at ~17 s per decision and never run; every statement here is about
  `H = 5, K = 256`. A stronger teacher might have more to transfer, and §3.1
  is a reason to check whether it does *before* distilling it again.
- It does not refute distillation of a fair planner in general. It rejects one
  configuration: this teacher, this target, this loss, these two architectures.
- It makes no gameplay claim of any kind. The gate failed and no cohort was
  opened, so there is no score, no lifetime and no bound to report for any
  student.
- The blend probe in §3.5 is a root-level diagnostic on 1,445 roots from 16
  games. It is not a policy result and no whole-game interval is offered for it.

### What this changes for the research program

1. **Re-measure a teacher before distilling it.** The single cheapest thing
   that would have changed this experiment is §3.1, and it costs one paired
   comparator run. finding-07's arm-B numbers were carried forward as the
   teacher's strength; on a representative sample they are 40% smaller and the
   lifetime advantage is inside the noise. Any future distillation should open
   with the paired teacher-versus-comparator run and only then spend GPU time.

2. **Stop targeting the teacher's argmax.** §3.3 measures, for the first time
   here, that this teacher's argmax is 83% reproducible from its own compute
   while its *values* are stable to 0.25 discs. The information is in the value
   vector, not in the winner, and a gate whose headline is top-1 is scoring the
   student on the least reliable part of the label. `normalised regret` — where
   the teacher's own half scores 0.0073 and the student scores 0.1915 — is the
   better primary metric, and the blend probe of §3.5 improves it while leaving
   top-1 ambiguous.

3. **A leaf evaluator should be compared as a leaf, not as a policy.** The
   preregistered comparison here pitted one model call per sibling against a
   615,090-leaf search and the student lost, which was foreseeable and is why
   §3.5 exists. The comparison that predicts deployment is the blended one, and
   it should be preregistered as the headline next time, with λ fixed on
   validation.

4. **The `K` axis is not the free lunch it looked like.** finding-07 found the
   fair planner still climbing at `K = 256` and recommended reporting `K` as a
   first-class result. This work paid for `K = 256` at `H = 5` and got a policy
   that is statistically tied with fair depth 4 on lifetime. Sampling budget
   without horizon does not buy a teacher.

## 6. Limitations

1. **One teacher configuration.** `H = 5, K = 256`. finding-07's headline
   `H = 7, K = 256` was budgeted at ~17 s per decision and not run, so this work
   cannot say whether a stronger fair teacher would have more to transfer.
2. **The exploration cohort of the preregistration was not generated.**
   PREREGISTRATION §6 lists a 96-game ε = 0.15 cohort for state coverage off the
   teacher's own trajectory. The machine was shared with three other jobs at
   load averages near 50 and the paired comparator of §3.1 was judged
   load-bearing while the exploration cohort was not, so the driver was stopped
   after cohort A. **The corpus is therefore entirely on-policy**, and
   distribution shift — audit-05's class (ii), one failure of seventeen — is
   untested here. This deviation is recorded rather than hidden.
3. **The CNN arm did not converge, and its held-out row is under-trained.** The
   warm-started `c128b6` model adopted 42 tensors from
   `runs/RUN-A51D-net/survival-c128-shuffled.pt` and reached validation top-1
   **0.5071** after one epoch of 2,151 s, then was stopped at the budget. Its
   `c64b4` replacement completed 4 epochs (1,559 / 1,261 / 428 / 425 s) with
   validation top-1 still rising monotonically — 0.4148, 0.4188, 0.4330, 0.4369
   — and its held-out numbers in §3.4 are therefore a floor on that
   architecture, not a measurement of it. A CNN epoch costs 200–1,300× a leaf
   epoch on this corpus, which is why the leaf models got 120 epochs and the CNN
   got 4. **The representation question is answered only weakly**: nothing
   observed suggests the 18-plane CNN unlocks the target, nothing rules it out,
   and the `leaf-h64` versus `leaf-h256` comparison is the stronger evidence
   that capacity is not the binding constraint.
4. **The gate's headline metric is top-1, which §3.3 shows is the least
   reliable part of the label.** That choice was preregistered and is honoured,
   but it is a design weakness identified by this work's own measurement rather
   than a neutral one.
5. **The blend probe is root-level.** 1,445 roots from 16 games. It cannot
   supply a whole-game interval and none is claimed.
6. **The reflection symmetry is not enforced.** Mirror augmentation was applied
   per batch with probability 0.5, and the trained model retains a 15% column
   preference (mean absolute reflection difference 0.510 against a prediction
   standard deviation of 3.365). The frozen search canonicalises before the leaf
   sees a state, so this is wasted capacity rather than an inconsistency.
7. **The latent randomness model is not the base engine's model** (audit-01 M2,
   inherited from finding-06 and finding-07). Scores in §3.1 are not comparable
   with any ledger figure. The two arms play identical master tapes, so the
   paired comparison is unaffected.
8. **The occupancy-band reconstruction has a 3% residual** (1,010 clears of
   32,412 for the teacher, 933 of 29,594 for fair D4), concentrated at the
   terminating move where a rise fails. The bands are descriptive; each policy
   generates its own occupancy distribution and the samples are not matched.
9. **No timing is timing-grade.** Every run shared a 16-core / 32-thread machine
   with three other jobs at load averages of 44–54 throughout, using at most 10
   threads. The 4.171 µs per state of §3.6 is 3.1× `learned-leaf`'s 1.33 µs for
   the identical shape on an idle machine. No performance claim is made.
10. **A model contribution record under `research/contributions/` is owed and
    was not written**, because this work was scoped to create files only under
    `approaches/lifetime-objective/planner-distill/` and `docs/exploratory/`.
    The coordinator should add one before this is promoted. The same debt is
    open for finding-02, finding-06 and finding-07.
11. **160 games is not a qualification cohort.** §3.1 is development evidence on
    a fresh exploratory lease. The score standard deviations are 70% and 55% of
    their means.

## 7. Validation, all before any gameplay

| Gate | Scale | Result |
| --- | --- | --- |
| INFORMATION BOUNDARY: the whole per-column value vector ignores hidden state — every hidden value replaced *and* a completely different disc tape and rise sequence substituted, given the same sampler stream | 20+ consecutive real decisions, all legal columns, values and counts compared exactly | ok |
| the logged argmax equals the frozen `fairDecision`'s, so the corpus records the measured planner and not a drifted re-implementation | 20+ consecutive decisions | ok |
| every legal sibling carries a finite value, every illegal one carries the sentinel, and `immediate <= value` | whole game | ok |
| the two half-`K` values average back to the full-`K` value | whole game | ok |
| the logged afterstate is the state the game really enters | whole game, move by move | ok |
| the parameterised search selects exactly the frozen reference column at default parameters | 120 moves (`d4-rank --parity`) | **0 mismatches** |
| the same, through the gameplay binary | 120 moves (`play --parity`) | **0 mismatches** |
| the redrawn chance realisations reproduce the true realisation's marginal | 8,249 true vs 57,743 redrawn afterstates | clears 1.2515 vs 1.2708, reveals 0.5424 vs 0.5535, survival 0.9868 vs 0.9867, occupancy 25.32 vs 25.30 |
| exported `D7PDST` weights vs the PyTorch checkpoint | 2,048 real corpus afterstates | max absolute difference 7.2e-7 |
| the occupancy-band reconstruction used in §3 reproduces a published number | finding-06's fair-D4 arm, 8 games | occupancy slope +0.9900 against finding-06's +0.990 |

`--parity` matters more than it looks: `w = 0` short-circuits to the frozen leaf
*before* the model is touched, so the comparator arm of any gameplay 2x2 is the
reference bit for bit and costs exactly what the reference costs.

## 8. Environment and machine profile

AMD clang 23.0.0git, `-O3 -std=c++20 -pthread -Wall -Wextra`. 16 physical
cores / 32 logical, 125 GiB unified memory, integrated Radeon 8060S (gfx1151).
Training in bf16 with GroupNorm — the bundled MIOpen in torch 2.13.0+rocm7.1
emits GFX9-only assembly for the BatchNorm *training* kernel on this device and
fails only in training mode — with `OPENBLAS_NUM_THREADS=1` (that OpenBLAS
build silently corrupts float32 matmul at four or more threads on this host) and
`LD_PRELOAD=/opt/rocm/lib/libhsa-runtime64.so.1` (without it every `.cuda()`
call segfaults). See `docs/exploratory/gpu-01`, `gpu-02` and `gpu-03`.

The machine was shared with three other jobs throughout at one-minute load
averages of **44–54**; at most 10 threads were used by this work at any time.
Frozen sources were consumed unmodified and their hashes recorded at build time
in `build/planner-distill/sources.sha256`; the shared `flow-ceiling/` and
`scenario/` headers were **snapshotted into the build tree** before compiling,
because another contributor was editing them concurrently and a multi-hour
corpus run must not be silently invalidated halfway through.

## 9. Seed lease

`SEEDLEASE-A52-DISTILL` = `0xa5260000`–`0xa526ffff`, assigned to this work by
the coordinator. Game seeds come from the low half and sampler streams from the
high half, and every binary refuses to start outside it.

| range | use |
| --- | --- |
| `0xa5260000`–`0xa526009f` | the 160 teacher master tapes, replayed by the fair-D4 comparator |
| `0xa5268000`–`0xa526809f` | the planner's hidden-board sampler, one sub-stream per game |
| `0xa526c000` | the `expand` chance-realisation stream |
| `0xa5267000`–`0xa526701f`, `0xa526f000`+ | CHECK-tier smoke data only, never evidence |

`lease-map.md` registers this range with the **training** role. Once read these
seeds are training/development data permanently, and no result here is offered
as confirmation evidence. **The shared evaluation cohort
`0xa51d1000`–`0xa51d103f` was not opened by this work**, except that
`d4-rank --parity` and `play --parity` replay its first three games against the
frozen reference as a legality gate, which reads no result.

## 10. Reproduce

```sh
# build (clang++ explicitly; the Makefile's CXX ?= clang++ loses to make's
# builtin CXX=g++, which trips a false -Werror=array-bounds in
# src/core/native/public-behavior.hpp)
./approaches/lifetime-objective/scenario/build.sh
./approaches/lifetime-objective/planner-distill/build.sh

RID=RUN-A526-DISTILL-46d93fb7956e

# CHECK gates
./build/planner-distill/corpus-gen --self-test
./build/planner-distill/d4-rank --parity
./build/planner-distill/play --parity

# 1. the teacher: 160 games, every legal sibling labelled
./build/planner-distill/corpus-gen --games 160 --seed-start 0xa5260000 \
    --sampler-seed 0xa5268000 --horizon 5 --samples 256 --max-moves 400 \
    --threads 8 --out runs/$RID/corpus/teacher-h5k256-onpolicy.bin \
    --jsonl runs/$RID/corpus/teacher-h5k256-onpolicy.jsonl
./build/planner-distill/expand --corpus runs/$RID/corpus/teacher-h5k256-onpolicy.bin \
    --out runs/$RID/corpus/onpolicy.after.bin --draws 8 \
    --sampler-seed 0xa526c000 --threads 4

# 2. the paired comparator on the same master tapes
./build/planner-distill/baseline --games 160 --seed-start 0xa5260000 \
    --depth 4 --chance-samples 5 --max-work 3200000 --max-moves 400 \
    --threads 8 --jsonl runs/$RID/corpus/fair-d4-s5-mastertapes.jsonl

# 3. the students
source approaches/lifetime-objective/gpu/activate.sh
S=approaches/lifetime-objective/planner-distill
python $S/train_student.py --corpus runs/$RID/corpus/teacher-h5k256-onpolicy.bin \
    --after runs/$RID/corpus/onpolicy.after.bin --out runs/$RID/model/leaf-h64 \
    --arch leaf --hidden 64 --mid 32 --epochs 120 --batch-roots 256 --lr 3e-3
python $S/train_student.py --corpus runs/$RID/corpus/teacher-h5k256-onpolicy.bin \
    --after runs/$RID/corpus/onpolicy.after.bin --out runs/$RID/model/leaf-h256 \
    --arch leaf --hidden 256 --mid 96 --epochs 120 --batch-roots 256 --lr 2e-3

# 4. the comparator's own ranking of the held-out roots, then the gate
python - <<'PY'
import sys, numpy as np; sys.path.insert(0, 'approaches/lifetime-objective/planner-distill')
import dataset as pd, os
rid = os.environ['RID']
r = pd.load(f'runs/{rid}/corpus/teacher-h5k256-onpolicy.bin')
tr, va, te = pd.split_by_origin(r)
np.savetxt(f'runs/{rid}/eval/test-rows.txt', np.flatnonzero(te), fmt='%d')
np.savetxt(f'runs/{rid}/eval/val-rows.txt', np.flatnonzero(va), fmt='%d')
PY
for SPLIT in test val; do
  ./build/planner-distill/d4-rank --corpus runs/$RID/corpus/teacher-h5k256-onpolicy.bin \
      --out runs/$RID/eval/d4-rank-$SPLIT.bin --depth 4 --chance-samples 5 \
      --max-work 3200000 --rows runs/$RID/eval/$SPLIT-rows.txt --threads 4
done
for M in leaf-h64 leaf-h256; do
  python $S/offline_gate.py --corpus runs/$RID/corpus/teacher-h5k256-onpolicy.bin \
      --after runs/$RID/corpus/onpolicy.after.bin \
      --checkpoint runs/$RID/model/$M.pt --d4-rank runs/$RID/eval/d4-rank-test.bin \
      --split test --out runs/$RID/eval/gate-$M.json
done
python $S/report.py runs/$RID/eval/gate-leaf-h64.json runs/$RID/eval/gate-leaf-h256.json

# 5. export, parity, cost, and the blend diagnostic
python $S/export_student.py --checkpoint runs/$RID/model/leaf-h64.pt \
    --out runs/$RID/model/leaf-h64.d7pdst
python $S/parity_student.py --corpus runs/$RID/corpus/teacher-h5k256-onpolicy.bin \
    --checkpoint runs/$RID/model/leaf-h64.pt --exported runs/$RID/model/leaf-h64.d7pdst
./build/planner-distill/play --leaf-stats --model runs/$RID/model/leaf-h64.d7pdst \
    --corpus runs/$RID/corpus/teacher-h5k256-onpolicy.bin
for SPLIT in val test; do
  python $S/blend_probe.py --corpus runs/$RID/corpus/teacher-h5k256-onpolicy.bin \
      --after runs/$RID/corpus/onpolicy.after.bin \
      --checkpoint runs/$RID/model/leaf-h64.pt \
      --d4-rank runs/$RID/eval/d4-rank-$SPLIT.bin --split $SPLIT \
      --out runs/$RID/eval/blend-$SPLIT-leaf-h64.json
done

# 6. the paired whole-game tables
python $S/analyze.py flow "teacher=runs/$RID/corpus/teacher-h5k256-onpolicy.jsonl" \
    "fairD4=runs/$RID/corpus/fair-d4-s5-mastertapes.jsonl"
python $S/analyze.py bands "teacher=runs/$RID/corpus/teacher-h5k256-onpolicy.jsonl"
python $S/analyze.py bands "fairD4=runs/$RID/corpus/fair-d4-s5-mastertapes.jsonl"
```

**No gameplay stage was run.** `runs/$RID/run-stage5.sh` exists and is the
script that would have been used had the gate passed; it is retained unrun.
