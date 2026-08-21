# Finding 16 — The two axes do not compound: the arms finding-09 left open

**Status:** exploratory, **both arms complete at 64/64**. Run validity `valid`,
evidence tier `development`. **Valid negative** on the compounding question, and
a **saturation** result on the reveal ladder.
**Updated 2026-08-21** after the depth-3 twelve-sample arm finished; the sections
below carry the completed figures throughout. The earlier partial-arm numbers
live in the superseded result record named below and should not be quoted.
**Namespace:** `approaches/lifetime-objective/reveal-sampling`, run
`runs/RUN-A525-reveal/`, build `build/reveal-sampling/`.
**Cohort:** the fixed shared evaluation cohort `0xa51d1000`–`0xa51d103f`,
64 games, 2,000-move cap, corrected 17,000-point Hardcore scoring. Already-read
development data, permanently.
**Records:** theory
[`TH-20260821-factored-chance-depth-compounding-aca01725`](../../research/theories/TH-20260821-factored-chance-depth-compounding-aca01725.json),
experiment
[`EX-20260821-reveal-sampling-unfinished-arms-470677b5`](../../research/experiments/EX-20260821-reveal-sampling-unfinished-arms-470677b5.json),
result
[`RS-20260821T192140Z-189fe392`](../../research/results/RS-20260821T192140Z-189fe392.json),
which supersedes the partial-arm record
[`RS-20260821T181918Z-ea7076a3`](../../research/results/RS-20260821T181918Z-ea7076a3.json)
(left unedited as committed history), runs
[`RUN-20260821T035407Z-00483c6c`](../../research/runs/RUN-20260821T035407Z-00483c6c.json)
and
[`RUN-20260821T143541Z-2b35eaaf`](../../research/runs/RUN-20260821T143541Z-2b35eaaf.json).
Machine profile
[`MACH-20260820T080056Z-376ada90`](../../research/system-profiles/MACH-20260820T080056Z-376ada90.json).

## Why this exists

[`finding-09`](finding-09-reveal-sampling.md) found and fixed a real defect: the
fair search drew "which disc comes next" and "what the covered discs turn out to
be" from the same sample counter, so with seven samples it saw seven of the 49
joint outcomes and those seven were perfectly correlated. Factoring the chance
node into `--disc-samples N` × `--reveal-samples M` and raising `M` from one to
six at depth 3 was worth **+64,116 points [95% lower bound +7,475]**.

It closed with two named open arms and one explicit question:

> **Still open:** whether the two axes *compound* when used together. The arm
> that would answer it was killed by the runtime and never produced a game.

Those two arms are what this document reports. Both were relaunched by
`approaches/lifetime-objective/reveal-sampling/run-arms.sh` as four sequential
16-game chunks over consecutive blocks of the shared cohort, precisely so that a
runtime kill costs one chunk instead of an arm.

## The answer: they substitute

**Depth 4 with two reveal samples is not better than depth 4 with one.** It is
worse by a point estimate of 41,950 for 4.07× the logical work, and the 95%
bootstrap lower bound is far below zero.

| arm | depth | N | M | joint coverage | games | mean | median | moves | clears/mv | reveals/mv | occupied | work/move |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| d3 N=7 M=1 | 3 | 7 | 1 | 14.3% | 64 | 312,327 | 267,279 | 92.27 | 1.9849 | 1.1001 | 23.88 | 156,834 |
| d3 N=7 M=3 | 3 | 7 | 3 | 42.9% | 64 | 337,306 | 285,023 | 98.70 | 2.0033 | 1.1111 | 23.81 | 1,045,719 |
| **d3 N=7 M=6** | 3 | 7 | 6 | 85.7% | 64 | **376,442** | 322,859 | 109.45 | 2.0447 | 1.1423 | 23.49 | 4,244,020 |
| d3 N=7 M=12 | 3 | 7 | 12 | 100% | 64 | 349,345 | 258,855 | 101.92 | 2.0231 | 1.1309 | **23.39** | 13,506,434 |
| **d4 N=7 M=1** *(comparator)* | 4 | 7 | 1 | 14.3% | 64 | **398,498** | 344,630 | 114.66 | 2.0571 | 1.1549 | 23.15 | 4,956,614 |
| **d4 N=7 M=2** *(new)* | 4 | 7 | 2 | 28.6% | 64 | 356,548 | 305,167 | 103.64 | 2.0306 | 1.1358 | 23.35 | 20,178,327 |
| d4 N=5 M=1 *(frozen reference)* | 4 | 5 | 1 | 14.3% | 64 | 297,327 | 260,415 | 87.16 | 1.9489 | 1.0697 | 24.29 | 1,296,034 |

Paired whole-game deltas, one-sided 95% percentile bootstrap over whole games,
20,000 resamples, seed `0xb0075eed`:

| comparison | n | Δ score | 95% lower | 95% upper | Δ moves | W-T-L | work ratio |
| --- | ---: | ---: | ---: | ---: | ---: | :---: | ---: |
| **d4 M=2 − d4 M=1** | 64 | **−41,950** | **−100,137** | **+17,541** | −11.02 | 28-0-36 | 4.07× |
| d4 M=2 − d4 N=5 M=1 | 64 | +59,221 | **+9,134** | +111,812 | +16.48 | 37-0-27 | 15.57× |
| d4 M=2 − d3 M=6 | 64 | −19,894 | −76,456 | +36,846 | −5.81 | 37-0-27 | 4.75× |
| d3 M=6 − d4 M=1 | 64 | −22,056 | −89,867 | +46,009 | −5.20 | 30-0-34 | 0.86× |
| d3 M=3 − d3 M=1 | 64 | +24,980 | −23,451 | +73,442 | +6.44 | 32-0-32 | 6.67× |
| d3 M=6 − d3 M=1 | 64 | +64,116 | **+7,475** | +121,776 | +17.19 | 36-0-28 | 27.06× |
| **d3 M=12 − d3 M=6** | 64 | **−27,097** | −83,807 | +31,209 | −7.53 | 28-0-36 | 3.18× |
| d3 M=12 − d3 M=1 | 64 | +37,019 | −25,076 | +102,426 | +9.66 | 30-0-34 | 86.1× |
| d3 M=12 − d4 M=1 | 64 | −49,153 | −125,029 | +27,828 | −12.73 | 22-0-42 | 2.73× |

**Not one row in that table clears zero in the negative direction.** Every
negative result here is "buys nothing measurable", never "harms"; the upper
bound column is what makes that readable at a glance.

Four things deserve to be read carefully.

**1. The compounding hypothesis is rejected at the dose tested, and the dose
matters.** Two reveal samples raises joint (disc, reveal) coverage from 14.3% to
28.6%. That is a *smaller* step than the one that first cleared noise at depth 3
— six samples, 85.7% — and at depth 3 the intermediate 42.9% dose was itself not
significant (+24,980 [−23,451]). So the honest claim is: **a doubling of reveal
samples on top of the fourth ply buys nothing measurable and costs 4.07× the
work.** It is not "reveal sampling fails at depth 4"; the wide depth-4 arm that
would test that was never affordable. What the arm does establish is that the
four-ply search is not *starved* for what the extra samples supply — if it were,
the first increment would have shown something, as the first increment did at
depth 3 in the direction of the effect if not its significance.

**2. The delta is negative but not significantly negative.** The same estimator
puts the one-sided 95% *upper* bound at **+17,541**. Read it as "buys nothing
measurable", not as "harms". That ceiling is the load-bearing number: had the
two axes compounded even at a quarter of the +64,116 the same knob is worth at
depth 3, 64 paired games would have had to show it. The sign is also stable
across both cohort halves — **−11,082** on seeds `0xa51d1000`–`0xa51d101f` and
**−72,818** on `0xa51d1020`–`0xa51d103f` — so this is not one half of the cohort
carrying the result.

**3. The gain that is there comes from the disc samples, not the reveal
samples.** Against the frozen five-stratum reference, d4 M=2 is **+59,221
[+9,134]**, 37-0-27. Every bit of that is the seven disc samples: on this cohort the paired
depth-4 seven-stratum minus depth-4 five-stratum contrast, computed from
[`finding-05`](finding-05-chance-strata.md)'s own retained arms, is **+101,171
[+47,447]**, 41-0-23.
Adding the reveal samples took 41,950 points back off.

**4. The near-equal-work equivalence is the real finding.** Depth 3 with six
reveal samples spends 4,244,020 work per move and scores 376,442. Depth 4 with
one reveal sample spends 4,956,614 and scores 398,498. Their paired delta is
−22,056 [−89,867], 30-0-34 — **indistinguishable**. Two entirely different ways
of spending about the same budget land in the same place. Then paying for both
at once (d4 M=2, 20,178,327 work per move, 4.75× the d3 M=6 arm) buys −19,894
[−76,456] against the cheaper of them. The budget is what is binding, not which
axis it is spent on.

## The reveal axis saturates at M ≈ 6

finding-09's attribution was specific and falsifiable: strength tracks the
fraction of the chance node's *joint* atoms that receive weight, and 14.3% →
42.9% → 85.7% ordered with score, moves, clears, reveals and (downward)
occupancy in lockstep. The obvious test is the arm that takes coverage to 100%.
It is now complete at 64 games, and **the curve turns over one step before full
coverage**:

| depth-3 arm | joint coverage | mean score | moves | clears/mv | reveals/mv | occupied |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| N=7, M=1 | 14.3% | 312,327 | 92.27 | 1.9849 | 1.1001 | 23.88 |
| N=7, M=3 | 42.9% | 337,306 | 98.70 | 2.0033 | 1.1111 | 23.81 |
| **N=7, M=6** | 85.7% | **376,442** | **109.45** | **2.0447** | **1.1423** | 23.49 |
| N=7, M=12 | **100.0%** | 349,345 | 101.92 | 2.0231 | 1.1309 | **23.39** |

**Say this as saturation, not as regression.** The M=6 → M=12 step is −27,097
with an interval of **(−83,807, +31,209)** and 28-0-36: it does not clear zero
in either direction. The data are consistent with the curve being flat from
M = 6 onward, and are *not* consistent with it continuing to climb at the rate
M = 1 → 6 showed. The strongest defensible statement is that **the reveal axis
is exhausted by M ≈ 6, and filling the last of the joint grid buys at most +31k
and plausibly nothing** — for 3.18× the work.

Score, moves, numbered clears and covered reveals all peak at M = 6. **Only mean
occupancy improves monotonically across all four points** (23.88 → 23.81 →
23.49 → 23.39), which makes it the one quantity that tracks coverage the whole
way — an odd survivor, and not enough on its own to rescue the coverage story.

### What this does and does not do to finding-09

finding-09's **headline result stands**: M = 6 beats M = 1 by +64,116 with a
lower bound of +7,475, and that is its own paired test on its own 64 games,
untouched by anything here. Two of its *supporting* arguments are weakened:

1. **The dose-response is no longer monotone.** §5 read the ordering of five
   quantities across three settings as corroboration that the effect was
   mechanical rather than noise. Add the fourth point and **four of those five
   quantities reverse**. The endpoint test still carries the result; the
   monotone-ladder argument no longer adds to it.
2. **The joint-coverage attribution fails at its own best-case endpoint.** §2
   predicted strength should track the (disc, reveal) joint column. It does over
   M ∈ {1, 3, 6} and then stops exactly at 100%, which is where that mechanism
   predicts the *maximum*.

The practical revision: **§5 should be read as "M = 6 is a local optimum", not
"more M is better".** finding-09's own Continuation §16 works through the three
readings that remain consistent with the data — redundant last atoms, variance
reduction rather than coverage as such, or 64 games simply being unable to
resolve steps this size — and this experiment does not separate them. That
analysis is not repeated here; read it there.

It is also consistent with finding-09's own caveat that the *reveal-by-reveal*
joint barely moves with `M` (14.3% → 26.2%): once the disc-by-reveal correlation
is broken, the remaining correlation is one this knob cannot reach.

## What was checked before any of this was believed

- **Pooling validity — the control every cohort in this family rests on.** Each
  arm is four 16-game chunks over consecutive seed blocks, because the artifact
  is only serialized when a whole cohort finishes and the previous attempt at
  d4 (7,2) was killed after an hour with nothing written. Whole games are the
  statistical unit and each game is a deterministic function of its seed and the
  policy, so a pooled 64-game cohort should be the *same object* as a single
  64-game run. That was checked rather than assumed: **depth 3 (N=5, M=1) run as
  four 16-game chunks at 1 thread reproduces the existing single 64-game
  12-thread artifact field-for-field — 0 mismatches, summed logical work
  312,966,881 on both sides, with only per-game `wallSeconds` differing**, and
  the pooled bound diagnostics match exactly (5,750 decisions, 0 below target
  depth, 0 work-limit events, `maxDecisionWork` 85,085). Because the two sides
  used 1 and 12 threads, this is simultaneously a **worker-count independence**
  check. Details in finding-09's Continuation §12.
- **Pooling determinism.** Two independent poolings of the same four chunks —
  the completing agent's and this session's — produce **byte-identical**
  artifacts for both arms.
- **Work bounds never bound.** d4 N=7 M=2: 6,633 decisions, **0 below target
  depth**, 0 work-limit events, minimum completed depth 4, busiest decision at
  44% of its declared bound. d3 N=7 M=12: 6,523 decisions, 0 below target depth,
  0 work-limit events, minimum completed depth 3, busiest at 47%. An arm that
  silently completed a shallower depth would be void, not negative — that is the
  failure mode this check exists for, and it did not happen.
- **0 censored games and 0 score-decomposition identity failures** in every arm.
- The `M = 1` decision-identity gates from finding-09 still hold: the factored
  search reproduces the frozen reference over 50 moves and the single-knob
  parameterized search's column *and logical work* over 30 moves, 0 mismatches.

## Limitations

- **Supersession.** An earlier result record assessed this experiment while the
  M=12 arm held 32 of 64 games, and reported that arm at 356,890 with a −4,495
  delta against M=6. The completed 64-game figures are **349,345** and
  **−27,097**: same direction, about six times the magnitude, still inside
  noise. That record was already committed and is left byte-unchanged as
  history; quote `RS-20260821T192140Z-189fe392`, not its predecessor.
- The depth-4 arm tested **two** reveal samples, not six. See point 1 above.
- No adjacent step in the depth-3 M ladder is individually significant, so the
  whole M ≥ 3 region is consistent with a single plateau. The M=1 → M=6 endpoint
  test is what carries finding-09's positive result.
- 64 paired games, score standard deviation 55–73% of the mean (the M=12 arm's
  is 254,059 on a mean of 349,345). Effects below roughly 60,000 points are
  invisible here.
- Already-read development cohort. `STANDARD` tier, diagnostic only. Never a
  freeze gate, never confirmation evidence, and a fresh-block replication is
  owed before anything here is promoted.
- The new arms auto-size their transposition cache from their branching factor
  (960,695 and 346,921 entries) while the recorded comparators declare 60,000.
  Capacity cannot change play — proven three ways in
  [`finding-15`](finding-15-depth5-exact-estimator.md) §3 — but **work per move
  is not comparable across capacities** and each figure must be read with its
  capacity.
- Not a timing-grade measurement: shared machine, one-minute load 12–63
  throughout, and chunk 1 of the depth-4 arm ran at 8 threads rather than 12
  after a deliberate load back-off. Scores, moves and logical work are
  deterministic and unaffected.
- All artifacts live under `runs/`, which is gitignored. The content manifests
  under the run directories named in the result record are the durable
  reference.
- The experiment record was written **after** the runs. The arms and their
  launch protocol were fixed in prose in finding-09 and in `run-arms.sh` before
  either produced a game, but this is retroactive registration and the
  experiment record's amendment says so.
- Nothing here approaches the target. The best arm on this cohort still sustains
  2.06 clears and 1.15 reveals per move against the 2.400 and 1.400 that
  [indefinite survival](finding-01-score-is-survival.md) requires, and every
  game ended.

## What this changes

- **finding-09's headline stands** and its interpretation gets sharper. "Depth
  and chance quality are exchangeable" was the cautious reading; the depth-4
  arm now says they are exchangeable *and not additive*, which is a stronger and
  more useful statement. Together with
  [`finding-15`](finding-15-depth5-exact-estimator.md) — where a fifth ply buys
  nothing at either stratum count — the picture is a **budget frontier with a
  flat top near the fair-D4 operating point**, reachable from either direction
  and not extended by pushing either axis further.
- **The reveal axis has a measured stopping point, M ≈ 6**, and two of
  finding-09's supporting arguments — the monotone dose-response and the
  joint-coverage attribution — do not survive the fourth ladder point. Its
  headline does. See the saturation section above and finding-09's own
  Continuation §16 and §18.
- **The next experiment is not a wider chance node and not a deeper search.**
  Both axes are measured and both are flat. The two mechanisms named in
  finding-15 §5 — the terminal utility's missing death-depth shaping, and the
  leaf's lack of calibration to lifetime — are one constant and one function,
  and they are what the flat top would look like if the *objective*, rather than
  the estimate of it, were the binding constraint.

## Reproduce

```sh
./approaches/lifetime-objective/reveal-sampling/build.sh

# The arms (chunked, detached, load-aware).  Already run; this is the protocol.
./approaches/lifetime-objective/reveal-sampling/run-arms.sh arm1   # d4 N=7 M=2
./approaches/lifetime-objective/reveal-sampling/run-arms.sh arm2   # d3 N=7 M=12

# Everything in this document, idempotent and read-only apart from re-pooling:
./approaches/lifetime-objective/reveal-sampling/finish.sh
```

`finish.sh` re-pools whatever chunks exist, runs the pooling-validity control,
and prints the cohort rows with their bound diagnostics and every paired delta
with one-sided 95% bounds on both sides. It routes each arm **by completeness**
rather than by hand: a full 64-game cohort joins the paired table, while an arm
still in flight is compared on shared seeds only through `stats.py partial`,
which prints its paired `n` on every row. A partial arm therefore cannot be
misread as a 64-game result — which is exactly the mistake the superseded record
above had to be written around.
