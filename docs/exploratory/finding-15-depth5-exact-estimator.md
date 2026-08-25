# Finding 15 — depth 5 with an exact chance estimator

**Status:** exploratory, in progress at time of writing. Evidence tier
`development`. Arms run on the shared evaluation cohort
`0xa51d1000`–`0xa51d103f`, 64 games, 2,000-move cap.
**Namespace:** `approaches/lifetime-objective/fast-engine/`, artifacts in
`approaches/lifetime-objective/fast-engine/cohorts/`.
**No existing file was modified.**

> **Read section 10 first — this document has been wrong twice, in opposite
> directions, and the sequence matters more than any one of its readings.**
>
> | written at | reading | status |
> | --- | --- | --- |
> | §2.2, §5 — n=8, arm in flight | depth 5 *reverses*, −268,611 | **withdrawn in §8**: completion-order bias, exactly as §2.2 warned |
> | §8 — n=16 at seven strata | depth 5 is a *wash*, −1,581, "does not separate" | **withdrawn in §10**: a non-measurement reported as a null |
> | §10 — n=32, arm stopped, final | the contrast was **never measured**; it sits at 22% of its detection floor | current |
>
> Sections 2.2, 5 and 8 are left exactly as written. The only edit to the
> original text is the heading, which said "Finding 14" while the file is
> `finding-15`; no content was changed.

> **Deviation on file placement, disclosed.** `AGENTS.md` says local run output
> belongs in `runs/<run-id>/`. This work package was constrained to write only
> under `approaches/lifetime-objective/fast-engine/` and `docs/exploratory/`
> because `runs/` and `research/` belong to concurrent contributors, so the
> cohort artifacts live under the approach directory instead. They are standard
> `drop7-lifetime-cohort-v1` documents and can be relocated unchanged.

## Why this experiment exists, and why it was nearly not run

[`finding-05`](finding-05-chance-strata.md) established that **depth pays only
when the chance estimator is exact**: at five strata the depth gradient dies
after depth 3 (d3→d4 is −7,723, not significant), while at seven strata d3→d4
is +86,172 with a 95% lower bound of +26,468. The obvious next question is
whether the gradient keeps climbing at depth 5 once the estimator is exact.

It was blocked on a cost estimate that was wrong by an order of magnitude. The
projection used the **worst-case** iterative-deepening work bound for depth 5 at
seven strata — 582,727,796 per move — and concluded ~75 hours for one 64-game
cohort. Measured work is far lower, because transpositions collapse most of the
tree. See section 4: this is a general defect in how this programme has been
pricing experiments, not a detail of one arm.

---

## 1. The control: does the fast engine reproduce the unoptimised binary?

Before any new arm is believed, the optimised search must reproduce a known one
**end to end on whole games**, not merely on leaves. The depth-4 seven-stratum
arm was re-run on the fast engine, same cohort, same parameters, same declared
cache capacity of 60,000 entries.

| | recorded (unoptimised, `runs/RUN-A51D-s7confirm/fresh-s7.json`) | fast engine |
| --- | ---: | ---: |
| mean score | 398,498.234375 | **398,498.234375** |
| mean moves | 114.65625 | **114.65625** |
| numbered clears / move | 2.05710002726 | **2.05710002726** |
| cover reveals / move | 1.154946852 | **1.154946852** |
| mean occupied cells | 23.146975056 | **23.146975056** |
| work / move | 4,956,614.26519 | **4,956,614.26519** |
| censored | 0 | 0 |
| score-identity failures | 0 | 0 |

Per-game reproduction check: **64 paired games × 11 fields = 704 comparisons, 0
mismatches** (score, moves, censored, rises, board clears, level points, clear
points, chain points, numbered cleared, covers revealed, max chain depth).

Completed-depth audit over all **7,338 decisions**: `incompleteDecisions = 0`,
`minimumCompletedDepth = 4`, maximum work in any single decision 10,639,860
against the declared bound of 11,892,399. The bound never bound.

**This is the end-to-end proof that finding-13's optimisation is
semantics-preserving in the only way that matters for science: identical whole-
game outcomes on a real cohort, not merely identical leaf values.** It also
holds under cache eviction — this configuration's worst case is 122,598 entries
against a declared 60,000, so the cache evicted on essentially every store and
the arms still agreed exactly.

A second, independent reproduction fell out of the depth-5 five-stratum arm: see
section 3.

---

## 2. Results

**The depth-5 arms are still running at the time of writing.** They are detached,
their artifacts are rewritten after every 16-game chunk, and every finished game
is in the arm's `.log`, so they complete without supervision. Run
`approaches/lifetime-objective/fast-engine/finish.sh` for the current state; it
is idempotent and prints progress, the completed-depth audit, the reproduction
checks, the summaries and every paired comparison.

### 2.1 Status

| Arm | depth | strata | cache | games done | status |
| --- | ---: | ---: | ---: | ---: | --- |
| d4s7 control | 4 | 7 | 60,000 | **64 / 64** | complete, reproduces exactly (section 1) |
| d5s7 target | 5 | 7 | 200,000 | in progress | see 2.2 |
| d5s5 | 5 | 5 | 200,000 | in progress | first 16 reproduce the recorded 32-game arm exactly |

### 2.2 Interim depth-5 seven-stratum result — READ THE CAVEAT

**This is a partial cohort and it is biased against d5s7.** Games complete in
order of how long they take, and a d5s7 game that ends early is a *short* game,
so the games that have finished first are systematically the ones where d5s7
died soonest. The paired comparator values are whole recorded games, unbiased.
The interim mean is therefore **more negative than the final result will be**.
It is reported only because the direction is already unambiguous.

| seed | d5s7 score | d5s7 moves | d4s7 score | d4s7 moves | delta score | delta moves |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `0xa51d1002` | 120791 | 38 | 192966 | 60 | -72175 | -22 |
| `0xa51d1004` | 236559 | 70 | 813628 | 225 | -577069 | -155 |
| `0xa51d1006` | 212580 | 60 | 1215442 | 330 | -1002862 | -270 |
| `0xa51d1007` | 124059 | 40 | 194864 | 59 | -70805 | -19 |
| `0xa51d100a` | 176869 | 55 | 230645 | 70 | -53776 | -15 |
| `0xa51d100b` | 169377 | 50 | 141619 | 45 | +27758 | +5 |
| `0xa51d100d` | 120690 | 40 | 263545 | 80 | -142855 | -40 |
| `0xa51d100f` | 121621 | 40 | 378723 | 110 | -257102 | -70 |

At n=8, **W-T-L 1-0-7**, mean paired delta −268,611 points and −73.25 moves
against the depth-4 seven-stratum comparator on the same seeds. No bootstrap
bound is quoted at n=8; it would not be meaningful.

The five-stratum arm at n=16 (which is *not* completion-order-biased, because
those 16 games reproduce a recorded arm and can be compared against its recorded
peers seed-for-seed):

| Arm | n | mean score | mean moves | clears/move | reveals/move | occupied |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| d5s5 (first 16 seeds) | 16 | 269,051 | 79.44 | 1.8906 | 1.0307 | 24.204 |
| d4s5 (same 16 seeds) | 16 | 318,650 | 92.56 | 1.9568 | 1.0689 | 24.613 |

d5s5 minus d4s5 on those 16 paired games: mean **−49,599** points, **−13.12**
moves, W-T-L 8-0-8, 95% lower bound −108,474 — not significant, but negative,
and consistent with the recorded 32-game d5s5 figure of 288,261 / 84.44 moves
sitting *below* d4s5's 297,327 / 87.16.

### 2.3 Flow rates, which is what the coordinator asked to be watched

A real gain has to move numbered clears per move toward the 2.400 that
[`finding-01`](finding-01-score-is-survival.md) says indefinite survival
requires. It is moving the wrong way:

| Arm | clears/move | reveals/move | mean occupied cells | mean moves |
| --- | ---: | ---: | ---: | ---: |
| d4s5 (recorded) | 1.9489 | 1.0697 | 24.288 | 87.16 |
| d3s7 (recorded) | 1.9849 | 1.1001 | 23.883 | 92.27 |
| **d4s7 (recorded = control)** | **2.0571** | **1.1549** | **23.147** | **114.66** |
| d5s5 (interim, 16 games) | 1.8906 | 1.0307 | 24.204 | 79.44 |
| required for indefinite survival | 2.400 | 1.400 | — | — |

d4s7 is the only arm that moves clears, reveals **and** occupancy in the right
direction together. Depth 5 at five strata moves all three back — fewer clears,
fewer reveals, a *more* crowded board and shorter games. That is the signature
of a policy that is surviving worse, not of noise in the score.

---

## 3. Transposition capacity cannot change a decision — only its cost

The transposition cache stores the exact value `expand(state, depth)` would
recompute. The search is deterministic and every chance seed derives from the
state and the remaining depth, so a hit and a miss return the identical double.
**Capacity therefore moves `work`, `nodes` and `cacheHits`, and nothing else.**

That is a strong claim about cost, so it was checked rather than asserted. Two
`FastSearch` instances differing only in declared capacity must select the same
column at every move of a real game (`bench --mode cacheinv`):

Depth 4, seven strata, 2 games x 10 moves:

| capacity | action mismatches | work/move | cache hits/move | relative work |
| ---: | ---: | ---: | ---: | ---: |
| 4,000 | 0 | 7,040,030 | 35,240 | 1.000 |
| 60,000 | 0 | 5,086,819 | 37,146 | 0.723 |
| 200,000 | 0 | 5,080,614 | 37,173 | 0.722 |
| 1,000,000 | 0 | 5,080,614 | 37,173 | 0.722 |

Depth 5, seven strata, 1 game x 2 moves:

| capacity | action mismatches | work/move | cache hits/move | relative work |
| ---: | ---: | ---: | ---: | ---: |
| 4,000 | 0 | 289,451,575 | 2,400,235 | 1.000 |
| 60,000 | 0 | 125,572,226 | 2,285,079 | 0.434 |
| 200,000 | 0 | 57,212,522 | 1,385,376 | 0.198 |
| 1,000,000 | 0 | 52,586,775 | 1,324,520 | 0.182 |

Confirmed a third time at whole-game level, across two different binaries:

- the recorded 32-game d5s5 arm was produced by the **unoptimised** binary at
  capacity **60,000**; this session's d5s5 arm runs the **fast** engine at
  capacity **200,000**, and its first 16 games match it on all 11 recorded
  fields — 176 comparisons, 0 mismatches;
- d5s7 seed `0xa51d1002` scored 120,791 in 38 moves at capacity 60,000 and
  120,791 in 38 moves at capacity 200,000.

**Consequence for this programme.** The frozen 60,000-entry capacity was sized
against depth 4 at five strata, whose worst case is 45,430 entries. At depth 5
with seven strata the worst case is 6,007,498, and 60,000 costs **2.19x more
work than 200,000 for identical play**. Raising the capacity is free strength-
wise and can be done at any time. It is still a *declared* configuration field
under `docs/benchmarks.md` — work per move is not comparable across capacities,
and the arms here declare theirs — but it is not a scientific decision, it is a
budget decision.

---

## 4. The cost model, and why an experiment was cancelled that should not have been

This is the part to carry forward past this experiment.

### 4.1 Worst-case bounds are for sizing `--max-work`, not for scheduling

The depth-5 seven-stratum projection that cancelled this experiment was:

```
worst-case iterative work, branches b = 7 columns x 7 strata = 49:
  sum_{d=1..5} [ sum_{l=1..d} 49^l + 49^d ]  =  582,727,796 per move
cohort  = 64 games x 114.66 mean moves = 7,338 decisions
total   = 4.276e12 work units
at the unoptimised engine's contended cost of ~1,971 ns per work unit
        = 8.43e6 s = 2,341 CPU-hours = 78 wall-hours on 30 threads
```

Measured work per move on real positions, at the capacity these arms use, is
**about 57 million** — roughly **10x below** the worst-case bound, because
transpositions collapse most of the tree. The same gap exists at depth 4:
worst case 11,892,398, measured 4,956,614 (2.4x); and at depth 3: worst case
significantly above the measured 153,759. The gap **widens with depth**, which
is exactly backwards from how a safety bound should be used for scheduling.

The bound is still the right thing to pass to `--max-work` — it must never bind,
and this runner refuses to start if `--max-work` does not exceed it, then audits
every single decision's completed depth. But it is the wrong thing to divide by
a machine's throughput.

### 4.2 The two corrections are independent and multiply

| Correction | factor | whose |
| --- | ---: | --- |
| worst-case work -> measured work | ~10x | cost model; applies to any engine |
| unoptimised -> fast engine ([`finding-13`](finding-13-fast-engine.md)) | 3.2x | this session's engineering |
| 60,000 -> 200,000 cache at depth 5 / seven strata | 2.2x | configuration, free |

Applied to the 78-hour figure, a 64-game depth-5 seven-stratum cohort is
**hours, not days**. At least one experiment was cancelled on the uncorrected
number.

### 4.3 What to do instead

Price an experiment by **measuring** work per decision on a handful of real
roots — `bench --mode probe --probe-depth D --probe-strata S --decisions 3`
does this in minutes — and multiply by the cohort's expected decision count.
Use the worst-case bound only to set `--max-work` and to size the memory. Note
that measured work is itself a function of the declared cache capacity, so
report both together.

---

## 5. Interpretation

**Interim verdict, to be confirmed when the arms finish: no. The depth gradient
does not keep climbing once the estimator is exact. It reverses.**

The framing this experiment was set up to test was that depth and estimator
quality are complements — [`finding-05`](finding-05-chance-strata.md) showed
d3->d4 is worth nothing at five strata (−7,723) and +86,172 at seven. The
natural extrapolation is that d4->d5 should also pay at seven strata. The data
so far says the opposite, and says it at **both** stratum counts:

- at five strata, the recorded 32-game d5s5 (288,261 / 84.44 moves) is *below*
  d4s5 (297,327 / 87.16), and this session's 16-game paired slice is −49,599
  and −13.12 moves, W-T-L 8-0-8;
- at seven strata, the interim eight paired games are −268,611 and −73.25 moves,
  W-T-L 1-0-7.

If depth 5 were merely failing to add value, one would expect a wash. It is not
a wash — games get markedly *shorter*, and the flow rates move away from the
survival threshold. **Depth 5 appears to be actively worse than depth 4, at
both estimator qualities.** That makes it unlikely to be an estimator-quality
effect at all, which is what makes it interesting.

### Two candidate mechanisms, both testable, both already documented

**M4 from [`audit-02`](audit-02-fair-d4.md): there is no death-depth shaping.**
`bestFutureValue` returns the same −1,000,000 whether the game ends at ply 1 or
ply 5; the only differentiator is at most ~17,000 of accumulated score, which is
1.7% of the penalty. A deeper horizon finds *more* within-horizon deaths. Once
several siblings each contain a sampled death, their values are dominated by
−1,000,000 x (deaths / samples), the leaf's legitimate discrimination range
(measured at ~10,000, and 217–9,745 on the frozen fixtures) is swamped, and the
root becomes near-value-flat — at which point the choice falls through to
`kColumnOrder` tie-breaking. Depth 5 should hit this regime far more often than
depth 4. **Prediction: the fraction of roots whose sibling values span less than
the leaf spread rises sharply from depth 4 to depth 5.** That is measurable
without playing a single extra game.

**H2/§4.4 from audit-02: the leaf is an uncalibrated potential, not an
estimate.** It is not calibrated to expected remaining lifetime or score, and
beyond the horizon it is the only carrier of survival information. Searching
five plies optimises that potential harder, at a more distant and less reliable
horizon. If `argmax leaf` is only accidentally `argmax expected lifetime` —
which audit-02 argues — then optimising it harder can move away from the true
objective. **Prediction: depth 5 should recover, or at least stop losing, if the
leaf is replaced by something calibrated to lifetime.**

These are not alternatives to each other and they interact: a flat, death-
dominated value surface plus an uncalibrated tie-breaker is exactly the
"myopic self-burial" mechanism audit-02 flagged as speculation in §5.

### What this means for the programme

[`finding-05`](finding-05-chance-strata.md)'s conclusion survives and should be
restated more precisely. It is **not** "depth pays when the estimator is exact".
It is "**the fourth ply** pays when the estimator is exact". The depth gradient
at seven strata appears to be +86,172 from ply 3 to ply 4 and negative from ply
4 to ply 5. Fair D4 may be sitting at a local optimum in depth, and the binding
constraint is not the horizon — it is the terminal utility's lack of depth
shaping and the leaf's lack of calibration.

Both of those are one-constant and one-function changes respectively, and both
are already on audit-02's ranked list of recommended next actions (H2 first,
H4 third). **The cheapest next experiment is not depth 6.** It is the
terminal-utility sweep at depth 5 with seven strata, which would distinguish
mechanism M4 from mechanism H2 directly: if death-penalty flattening is the
cause, a smaller `terminalUtility` should restore depth 5's gradient, and if
leaf mis-calibration is the cause, it should not.

---

## 6. Limitations

- **The depth-5 arms are incomplete at the time of writing.** Every number in
  section 2.2 is interim, `n` is stated on each, and the d5s7 slice is
  completion-order biased against d5s7 as explained there. No bootstrap bound is
  quoted below n=16. Nothing here is a promotable result until
  `finish.sh` reports 64/64 on both arms.
- 64 paired games is a `STANDARD`-tier cohort on **previously read development
  seeds** (`0xa51d1000`–`0xa51d103f` were opened by finding-05). It is a
  diagnostic comparison and a shared leaderboard entry, never a freeze gate.
- Score standard deviation is 38–64% of the mean on this cohort, so 64-game
  means carry roughly +/-19,000 to +/-32,000 at one standard error.
- The d5 arms declare a 200,000-entry cache and the d4 comparators 60,000.
  **Strength is unaffected** (section 3, verified three ways) but work per move
  is not comparable across capacities and must be read with its capacity.
- The depth-5 mean-moves anchor used for pricing (114.66) came from d4s7. The
  measured d5 games are much shorter, so the arms are cheaper than projected —
  for a reason that is itself the negative result.
- Machine was not exclusive: one-minute load average 33–63 throughout, from five
  other agents' jobs. This does not affect any score or move number, which are
  deterministic functions of the seed and the policy; it affects only wall time.
- The two candidate mechanisms in section 5 are **hypotheses with predictions**,
  not findings. Neither was tested here.
- A contribution record under `research/contributions/` is owed and could not be
  written from this work package, which was scoped out of `research/`.

---

## 7. Reproduce

```sh
./approaches/lifetime-objective/fast-engine/build.sh
B=./build/fast-engine
C=./approaches/lifetime-objective/fast-engine/cohorts

# The control.  --max-work is the worst-case bound, never measured work.
$B/cohort --seed-start 0xa51d1000 --games 64 --threads 6  --depth 4 --chance-samples 7 \
          --cache 60000  --chunk 16 --max-moves 2000 --label d4s7-control \
          --output $C/d4s7-control.json
$B/cohort --seed-start 0xa51d1000 --games 64 --threads 14 --depth 5 --chance-samples 7 \
          --cache 200000 --chunk 16 --max-moves 2000 --label d5s7 --output $C/d5s7.json
$B/cohort --seed-start 0xa51d1000 --games 64 --threads 6  --depth 5 --chance-samples 5 \
          --cache 200000 --chunk 16 --max-moves 2000 --label d5s5 --output $C/d5s5.json

# Capacity invariance
$B/bench --mode cacheinv --probe-depth 4 --probe-strata 7 --games 2 --max-moves 10
$B/bench --mode cacheinv --probe-depth 5 --probe-strata 7 --games 1 --max-moves 2

# Analysis
A=./approaches/lifetime-objective/fast-engine/analyze.py
python3 $A identical $C/d4s7-control.json runs/RUN-A51D-s7confirm/fresh-s7.json
python3 $A summary $C/d5s7.json $C/d5s5.json $C/d4s7-control.json
python3 $A paired $C/d5s7.json $C/d4s7-control.json \
                  $C/d5s7.json $C/d5s5.json \
                  $C/d5s5.json runs/RUN-A51D-s7confirm/fresh-s5.json
```


---

## 8. Completion, 2026-08-21 — the factorial, and the withdrawal of section 5

Run `approaches/lifetime-objective/fast-engine/finish.sh` for the live state.
The numbers below are that script's output at the time this section was written,
retained verbatim in `runs/RUN-20260821T045349Z-73f29417/finish-report.txt`.

### 8.1 Final arm status

| Arm | depth | strata | cache | games | status |
| --- | ---: | ---: | ---: | ---: | --- |
| d4s7 control | 4 | 7 | 60,000 | **64 / 64** | complete; reproduces the unoptimised binary exactly |
| d5s5 | 5 | 5 | 200,000 | **64 / 64** | complete; its first 32 games reproduce the recorded 32-game arm exactly |
| d5s7 | 5 | 7 | 200,000 | **16 / 64** | **still running** (24 games finished in its log, 16 written to its artifact). Partial evidence only. |

The d5s7 arm was neither killed nor waited on. The frozen 16-game snapshot that
the result record assesses is
`runs/RUN-20260821T060358Z-895d0a79/d5s7-partial-16games.json`; the live
artifact will be rewritten at the next chunk boundary and will stop matching it.

### 8.2 The engine control, twice over

| control | comparisons | mismatches |
| --- | ---: | ---: |
| fast-engine d4s7 vs recorded `fresh-s7.json` | 64 paired games × 11 fields = **704** | **0** |
| fast-engine d5s5 vs recorded `s5d5.json` | 32 paired games × 11 fields = **352** | **0** |

The second one is worth more than the first: it holds across **two different
binaries and two different declared cache capacities** (unoptimised at 60,000,
fast at 200,000). Completed-depth audits: d4s7 7,338 decisions, d5s5 5,420
decisions, d5s7 1,760 decisions, **0 incomplete decisions and minimum completed
depth equal to the requested depth in all three**, busiest single decision at
89%, 72% and 76% of its declared bound. 0 censored games, 0 score-decomposition
identity failures.

### 8.3 The completed factorial

Cohort means (64 games unless marked):

| arm | mean | median | moves | clears/mv | reveals/mv | occupied | work/move |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| d3 s7 | 312,327 | 267,279 | 92.27 | 1.9849 | 1.1001 | 23.88 | 156,834 |
| d4 s5 | 297,327 | 260,415 | 87.16 | 1.9489 | 1.0697 | 24.29 | 1,296,034 |
| **d4 s7** | **398,498** | 344,630 | 114.66 | 2.0571 | 1.1549 | 23.15 | 4,956,614 |
| d5 s5 | 288,704 | 248,419 | 84.69 | 1.9387 | 1.0651 | 24.19 | 30,183,227 |
| d5 s7 *(16 games)* | 383,691 | 264,137 | 110.00 | 2.0403 | 1.1330 | 23.89 | 170,131,134 |

Paired whole-game deltas, one-sided 95% percentile bootstrap, 20,000 resamples,
Mulberry32 domain `0xb0075eed`:

| contrast | n | Δ score | 95% lower bound | Δ moves | W-T-L | work ratio |
| --- | ---: | ---: | ---: | ---: | :---: | ---: |
| **d5 s5 − d4 s5** *(depth, five strata)* | 64 | **−8,624** | −55,134 | −2.47 | 33-0-31 | 23.29× |
| **d5 s7 − d4 s7** *(depth, seven strata)* | 16 | **−1,581** | −173,154 | −0.88 | 7-0-9 | 34.32× |
| d5 s7 − d3 s7 *(two plies, seven strata)* | 16 | +16,622 | −130,027 | +3.25 | 8-0-8 | 1,084.78× |
| d5 s7 − d5 s5 *(strata, depth five)* | 16 | +114,640 | −7,280 | +30.56 | 8-0-8 | 5.64× |
| **d4 s7 − d4 s5** *(strata, depth four)* | 64 | **+101,171** | **+47,447** | +27.50 | 41-0-23 | 3.82× |

**One row in that table is significant and it is not a depth row.**

### 8.4 The misreading this section exists to prevent

The eye goes to 398,498 for d4s7 against 288,704 for d5s5 and reads a
110,000-point collapse from the fifth ply. **Those two arms differ in both
factors.** The depth contrasts at *fixed* chance resolution are −8,624 and
−1,581, both indistinguishable from zero; the stratum contrast at *fixed* depth
is +101,171 with a lower bound of +47,447. The gap is a chance-samples effect.
Quote the paired rows, never the difference of two means from different cells of
the factorial.

### 8.5 What section 5 got wrong, and why it is left standing

Section 2.2 reported −268,611 over eight paired games and section 5 concluded
that depth 5 "is actively worse than depth 4, at both estimator qualities". At
16 games the seven-stratum delta is **−1,581**, and at the full 64 games the
five-stratum delta is **−8,624**. The reversal was an artifact of exactly the
bias section 2.2 named in bold and then reasoned through anyway: games finish in
length order, so an in-progress arm's finished games are its short ones, while
the comparator contributes whole recorded games. The lesson is not "be careful
with partial arms" — the document already said that — it is that **a partial arm
should not be given an interpretation section at all.**

What survives from section 5 is the *question*, not the answer. Depth 3 → 4 → 5
does not separate at a fixed stratum count: two extra plies for +16,622 (n.s.)
at 1,085× the work. The fourth ply's +86,172 at seven strata reported in
[`finding-05`](finding-05-chance-strata.md) is not the start of a gradient that
continues; the depth axis is flat above it. The two mechanisms named in section
5 remain untested hypotheses, and both would predict a flat top rather than a
reversal, so this outcome does not choose between them either.

### 8.6 Corrections to section 6's limitations

- The arms are no longer "incomplete at the time of writing" except d5s7, which
  is 16 of 64 and is labelled partial everywhere it appears.
- The n=8 interim slice in section 2.2 is superseded by the n=16 row above and
  should not be quoted.
- The claim in section 2.3 that depth 5 "moves all three back" holds at five
  strata (clears 1.9387 vs 1.9489, reveals 1.0651 vs 1.0697, occupancy 24.19 vs
  24.29) but is much weaker than the interim text implies, and at seven strata
  the 16-game slice shows clears 2.0403 vs 2.0571 and occupancy *up* 23.89 vs
  23.15 — small movements, not a signature.
- A contribution record is no longer owed: see section 9.

### 8.7 The other half of the same picture

[`finding-16`](finding-16-factored-reveal-sampling.md) ran the complementary
experiment — more chance resolution at fixed depth 4 — and found the same
flatness: two reveal samples on top of the fourth ply is −41,950 [−100,137] for
4.07× the work, while depth 3 with six reveal samples and depth 4 with one are
statistically indistinguishable at near-equal work. **The frontier has a flat
top near the fair-D4 operating point, and it is reachable from either axis.**

---

## 9. Records

| record | ID |
| --- | --- |
| theory | [`TH-20260821-depth-gradient-beyond-fair-d4-034314fc`](../../research/theories/TH-20260821-depth-gradient-beyond-fair-d4-034314fc.json) |
| experiment | [`EX-20260821-depth5-chance-exactness-factorial-a6a604fd`](../../research/experiments/EX-20260821-depth5-chance-exactness-factorial-a6a604fd.json) |
| runs | [`RUN-20260821T045349Z-73f29417`](../../research/runs/RUN-20260821T045349Z-73f29417.json) (the two complete arms), [`RUN-20260821T060358Z-93cb9bfc`](../../research/runs/RUN-20260821T060358Z-93cb9bfc.json) (d5s7 as finally stopped, 32 games), superseding the in-flight snapshot [`RUN-20260821T060358Z-895d0a79`](../../research/runs/RUN-20260821T060358Z-895d0a79.json) |
| result *(current)* | [`RS-20260821T205102Z-d89df4b5`](../../research/results/RS-20260821T205102Z-d89df4b5.json) — run validity `partial`, outcome `inconclusive`, assessment `mixed`, evidence tier `public-development` |
| result *(superseded, left as committed history)* | [`RS-20260821T181917Z-9a34ba02`](../../research/results/RS-20260821T181917Z-9a34ba02.json) — outcome `fail`; written at n=16, see §10 |
| machine profile | [`MACH-20260820T080056Z-376ada90`](../../research/system-profiles/MACH-20260820T080056Z-376ada90.json) |

The experiment record was registered **after** the runs, by a different agent
than the one that executed them; its amendment says so. The comparison rule is
the repository's standing paired whole-game bootstrap and predates the arms.

---

## 10. Final, 2026-08-21 — the arm was stopped, and the second withdrawal

The depth-5 seven-stratum arm reached 32 of its planned 64 games and was
**stopped by the repository owner's decision** at a clean 16-game chunk
boundary: the chunk artifact was rewritten first and the process signalled
afterwards, so `gamesComplete` is 32, all 32 are whole games, and there are 0
censored games, 0 score-decomposition identity failures and 0 incomplete
decisions at minimum completed depth 5. The cohort **will not be resumed**, so
the arm is *partial* but its analysis is **final**. Those are different things
and this section keeps them apart.

### 10.1 The sign flipped when the sample doubled

| | n=16 (§8) | n=32 (final) |
| --- | ---: | ---: |
| d5s7 mean score | 383,691 | **411,874** |
| d5s7 mean moves | 110.00 | 117.97 |
| **d5s7 − d4s7 paired mean** | **−1,581** | **+23,367** |
| 95% lower bound | −173,154 | −83,046 |
| median paired delta | −39,660 | **+18,820** |
| W-T-L | 7-0-9 | 17-0-15 |
| work ratio | 34.32× | 35.62× |

By chunk, the paired mean is **−1,581** on seeds `0xa51d1000`–`0xa51d100f` and
**+48,315** on `0xa51d1010`–`0xa51d101f`. Adding sixteen games moved the point
estimate by 25,000 points and reversed its sign and its median.

```figure
depth5-reading-history
caption: The three recorded readings of the d5s7 minus d4s7 contrast as the cohort grew, against the detection floor at each size. The estimate crossed zero twice while staying far inside the floor: the contrast was never measured.
```

### 10.2 So §8's reading is withdrawn too — for a different reason

§8 replaced "depth 5 reverses" with "depth 5 is a wash — depth 3 → 4 → 5 does
not separate". **That was a non-measurement reported as a null.** The paired
standard deviation of the d5s7 − d4s7 contrast is **371,351 points**. At n=32
the smallest true effect whose one-sided 95% bound would clear zero is
**107,988**; the observed +23,367 is 22% of that. At n=16 the paired sd was
431,420, the floor was **177,421**, and the observed −1,581 was **1%** of it. Neither number was ever evidence about
the fifth ply. **The correct statement is that the fourth-to-fifth ply contrast
at seven strata was never measured.**

And it must not now be flipped into the opposite claim. **+23,367 is exactly as
unsupported as −1,581 was.** Nothing about this arm licenses "depth 5 helps".

### 10.3 The power analysis, which is the most useful thing here

Detection floor = `1.645 · sd / √n`, the smallest true mean difference whose
one-sided 95% bound would clear zero:

| contrast | n | mean | paired sd | detection floor | above floor? |
| --- | ---: | ---: | ---: | ---: | :---: |
| d4s7 − d4s5 *(strata @ d4)* | 64 | **+101,171** | 268,413 | 55,192 | **yes** |
| d4s7 − d3s7 *(depth @ s7)* | 64 | **+86,172** | 298,877 | 61,457 | **yes** |
| d5s7 − d5s5 *(strata @ d5)* | 32 | **+123,613** | 327,399 | 95,207 | **yes** |
| d5s7 − d3s7 *(two plies @ s7)* | 32 | +86,397 | 334,291 | 97,211 | no |
| d5s5 − d4s5 *(depth @ s5)* | 64 | −8,624 | 228,827 | 47,052 | no |
| d5s7 − d4s7 *(depth @ s7)* | 32 | +23,367 | 371,351 | 107,988 | no |

**Every significant result in this factorial is above its floor and every null
is below it.** The factorial separated exactly the contrasts it had the power to
separate, and nothing else. That is not a coincidence to be admired; it is the
warning that the "nulls" carry no information.

Resolving the observed +23,367 at one-sided 95% needs **≈684 paired games** —
about 13 wall-days at this arm's own observed 1,647 s per game at 14 threads
(the two chunks differed 2.3× in throughput under other agents' load, so call it
8–18 days). Finishing to the planned 64 would have left a standard error near
**46,400** against a 23,367 estimate: still a non-measurement. **The contrast is
not answerable at any affordable cohort size, and that — not impatience — is the
justification for stopping the arm.** The marginal machine-day bought no
information.

The variance is structural. The five largest single-seed paired deltas in this
contrast are **−1,002,862, +958,985, −678,455, +592,546 and −577,069**:
individual games swing by more than twice the cohort mean. No amount of tidier
running fixes that; only a different estimator or a variance-reduction scheme
would.

**Bootstrap versus normal approximation.** The floors above are
`1.645·sd/√n`; the tooling reports a one-sided percentile bootstrap (20,000
resamples, Mulberry32 domain `0xb0075eed`). The two **agree on the significance
call for all six contrasts**. The bootstrap bound is systematically
0.5k–4.5k *higher* — less conservative — than `mean − 1.645·SE`, i.e. 1–5% of
the half-width (d4s7−d4s5 +47,447 vs +45,979; d5s7−d5s5 +32,575 vs +28,406;
d5s7−d4s7 −83,046 vs −84,621). Paired-delta skewness runs +0.55 to +0.92 on
four contrasts and −0.30 on d5s7−d4s7, so the two are close but not
interchangeable at the third digit. No conclusion here depends on the choice.

### 10.4 What *is* measured, and it is the same lesson from the other side

| contrast | n | delta | 95% lower | W-T-L | work |
| --- | ---: | ---: | ---: | :---: | ---: |
| **d5s7 − d5s5** *(strata, at depth 5)* | 32 | **+123,613** | **+32,575** | 19-0-13 | 5.85× |
| **d4s7 − d4s5** *(strata, at depth 4)* | 64 | **+101,171** | **+47,447** | 41-0-23 | 3.82× |
| d5s7 − d4s7 *(depth, at 7 strata)* | 32 | +23,367 | −83,046 | 17-0-15 | 35.62× |
| d5s5 − d4s5 *(depth, at 5 strata)* | 64 | −8,624 | −55,134 | 33-0-31 | 23.29× |

**Both stratum contrasts are significant. Neither depth contrast is.** Chance
exactness pays at depth 5 just as it pays at depth 4, for a 4–6× work premium;
depth costs 23–36× and buys nothing this cohort can see. §8.4's warning
therefore survives and is strengthened: the gap between d4s7's 398,498 and
d5s5's 288,704 is a **chance-samples** effect, and the factorial now says so
from both rows.

The one genuinely useful *depth* statement the factorial supports is the
five-stratum bounded null: on a complete 64-game cohort, any true d4→d5 effect
at five strata is **smaller than about 47,000 points**. That is a bound, not a
zero, and it is the only depth claim in this document that rests on adequate
power.

### 10.5 Corrections to §8

- §8.1's arm table: d5s7 is **32 / 64, stopped by decision, final**, not "still
  running".
- §8.3's d5s7 row and every paired row involving it are superseded by §10.1 and
  §10.4 above.
- §8.5's "Depth 3 → 4 → 5 does not separate at a fixed stratum count" is
  **withdrawn**. Substitute: *the depth axis was never measured at seven strata,
  and at five strata it is bounded below 47,000 points.*
- §8.5's "+16,622 (n.s.) at 1,085× the work" for d5s7 − d3s7 becomes **+86,397
  [−6,303] at 1,126× the work** — still below its 97,211 floor, so still not a
  measurement, but no longer a small number.
- §8.7's conclusion that the frontier "has a flat top near the fair-D4
  operating point" should be read with §10.3 attached: the top is flat *as far
  as this cohort can resolve*, which for depth contrasts is not very far.
  [`finding-16`](finding-16-factored-reveal-sampling.md)'s reveal-axis arms are
  better powered and are what carry that claim.

### 10.6 The general lesson, which outlives this experiment

Three readings of the same arm, in order: a reversal, a wash, an unmeasurable.
The first two were both produced by taking a point estimate seriously without
first asking what the smallest resolvable effect was. **Compute the detection
floor before running the arm, not after reading it** — `1.645·sd/√n` from a
pilot's paired deltas costs nothing and would have said, before any of these
machine-days were spent, that a 64-game cohort could not answer this question.
The repository has now paid for that lesson twice in one document.
