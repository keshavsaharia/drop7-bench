# Finding 06 — Flow balance is achievable: Drop7 Hardcore is a control problem, not a losing battle

**Status:** exploratory, evidence tier `development`/`pilot`. Built and measured
in this checkout on 2026-08-20.
**Namespace:** `approaches/lifetime-objective/flow-ceiling/`, run
`runs/RUN-FLOW-044da902f8e8/`, seed lease `SEEDLEASE-A52-FLOW` =
`0xa5230000`–`0xa5233fff`.
**Nothing in `docs/research/`, `artifacts/`, `research/`, or any existing
approach source was modified by this work.** The only files created are under
`approaches/lifetime-objective/flow-ceiling/`, `build/flow-ceiling/`,
`runs/RUN-FLOW-*/` and `docs/exploratory/`.

> **CORRECTION NOTICE (added after publication).** One sentence in §3 of this
> document — "The gap between the two is a *control* gap" — is **not supported
> by the evidence here** and is superseded by
> [`finding-07`](finding-07-fair-planning-ceiling.md). The planner measured
> below is privileged twice over: it plans exactly over a window *and* it reads
> the hidden board. This document never separated those two privileges.
> finding-07 does. It finds the sentence **directionally right but unproven**:
> a *legal* receding-horizon planner with the same objective, the same horizon
> and the same master tapes closes **58.8%** of the clairvoyant-minus-D4 gap, so
> **at most 41.2% of it is hidden information** — an upper bound that was still
> falling when compute ran out. Knowing the future disc tape is worth nothing.
> What is genuinely weakened is a *different* claim: that the ceiling below is a
> reachable **target**. **No legal policy tested reached flow balance — all 87
> fair-planner games ended and every fair arm's board filled monotonically**,
> although the best legal arm reached 2.2309 clears per move (93.0% of the
> requirement) with an occupancy slope still falling with more sampling. The original text is left in place unaltered below.
> Everything else in this document — the conservation arithmetic, the
> clairvoyant flow ceiling, the occupancy-equilibrium mechanism, the score
> composition and the wave-depth results — stands as measured. What is weakened
> is the *reachability* of that ceiling by a legal policy, which this document
> did not measure.

> **SECOND CORRECTION NOTICE (added after publication).** The eight master
> tapes used below are **unrepresentative — they favour long games** — and every
> absolute figure measured on them is inflated in proportion to how long the
> policy survives.
> [`finding-12`](finding-12-fair-planner-ceiling-extended.md) §2 measures the
> size: fair depth 4 drops from **117.75 to 93.56 mean moves** on 128 fresh
> tapes, lowest column drops only 3.7%.
>
> **The clairvoyant results in this document are not affected.** Re-run on 64
> fresh tapes, the clairvoyant planner at H = 7 measures **2.3663 clears and
> 1.3832 reveals per move against 2.3601 and 1.3785 here** — a shift of +0.006,
> in the opposite direction to fair D4's −0.060 — with 59 of 64 fresh games
> reaching the move cap alive against 6 of 8. A policy that is censored at the
> cap on both cohorts has no room to be inflated by an easy tape draw, which is
> why the ceiling is robust and the comparator is not.
>
> The one thing to re-read: **every comparison against fair depth 4 below uses a
> D4 baseline that is 26% too generous on lifetime.** "396.88 moves against fair
> depth 4's 117.75" should be read as "against 93.56", which makes the contrast
> larger, not smaller. No clairvoyant figure needs revision.

## The question

[`finding-01`](finding-01-score-is-survival.md) establishes two measured facts.
Hardcore score is 94.29% flat row-rise bonus and correlates with lifetime at
r = 0.9995, and lifetime obeys an exact conservation law: five placed discs plus
seven risen covered discs enter a 49-cell board every five moves, so surviving
indefinitely requires sustaining

- **≥ 2.400 numbered clears per move** (12 / 5), and
- **≥ 1.400 covered reveals per move** (7 / 5).

Fair depth 4 at five chance strata sustains 1.9489 and 1.0697.
[`finding-05`](finding-05-chance-strata.md)'s best configuration, depth 4 at
seven strata, sustains 2.0571 and 1.1549. Both are below the requirement and
both die.

That leaves the research program facing two incompatible worlds:

- **World A — strictly losing.** If no line of play can sustain 2.400, every
  game necessarily ends, mean lifetime is bounded, and a 1,000,000-point mean
  cannot come from survival. It would have to come from chain scoring, which is
  convex in wave depth (`popperCount x floor(7 x d^2.5)`: 7 points per disc at
  depth 1, 391 at depth 5, 2,213 at depth 10, 12,521 at depth 20).
- **World B — a control problem.** If flow balance is sustainable, survival
  remains the lever and the whole problem is finding a public policy that holds
  the balance.

**The evidence in this document supports World B, and does so with a large
margin.**

## Summary

| Claim | Evidence |
| --- | --- |
| Flow balance is achievable | A clairvoyant receding-horizon planner holds **2.3875 clears and 1.3963 reveals per move over 6,000 moves**, and **2.4023 clears per move over the second half of those games — above the 2.400 requirement**. Board occupancy is flat at ~19–20 of 49 cells for 200 consecutive five-move cycles |
| Games stop ending | **All 6 games reached the 1,000-move cap alive**, mean score 3,865,157. On a separate 8-game cohort capped at 400 moves, 7 of 8 survived. Fair depth 4 on identical futures died at a mean of 117.75 moves |
| The mechanism is a stable equilibrium | The achievable clear rate **rises with occupancy** and crosses 2.400 near 20 occupied cells, so ~20 cells is an attracting fixed point. Fair D4's clear rate peaks at 2.59 near 27 cells and then **falls** as the board crowds, so its equilibrium is unstable and it dies |
| Survival dominates chains, even for a chain-seeking optimum | Maximizing *score* over the same window earns 1.75x the points per move (6,807 vs 3,891) and reaches wave depth 22, but **cuts lifetime by 58% and loses on the mean** (165.0 vs 396.9 moves; 1,123,130 vs 1,544,461 points) |
| Deep cascades are real but not the lever | Clairvoyant score-seeking play puts **21.5% of its waves at depth ≥ 11 and 7.8% at depth ≥ 15**, against 0.22% and 0.00% for fair D4. The engine pays far more than fair play collects — and collecting it still loses |
| The 70,000 board clear stayed unreachable | **Zero board clears in 19,610 measured moves** across every policy on a realistic board, including two clairvoyant ones. audit-01's H2 fifth-drop bonus bias therefore inflates nothing in this document — though on deliberately sparse boards a clairvoyant solver schedules **9 of its 14 clears onto a fifth drop**, exactly as H2 predicted |

The headline number must be read carefully: the receding-horizon planner is
**privileged**. It reads the hidden board. It is a teacher and a ceiling
measurement, never a deployable policy. What it establishes is that the ceiling
is above the requirement — that the target is a control problem with a solution,
not an arithmetic impossibility.

## Method

### Why this cannot be measured in the base engine

`audit-01` finding M2 records that this engine draws a covered disc's number
from the reveal RNG at the moment of reveal, so there is no persistent hidden
board and no well-posed clairvoyant optimum. All work here therefore runs inside
the scenario engine of [`finding-02`](finding-02-scenario-benchmark.md), whose
`LatentRevealSource` fixes every hidden value in advance and whose
`StreamRevealSource` is proven trajectory-identical to `drop7::playHeadlessMove`
over 8,192 game-plays and 218,470 moves. **Scores measured here are not
bit-comparable with base-engine ledger figures**; flow rates are comparable in
expectation because the reveal marginal is identical.

### The master tape: one fixed future per game

A `MasterTape` fixes a whole game's randomness once: the start position (the
repository's real game start — an empty board with a solid covered bottom row,
five moves before the first rise), one numbered disc per absolute move index,
and one hidden row per absolute rise index. Rises land on fixed move indices
whatever columns are chosen, so a master tape defines *the same future for every
policy*. Nothing is redrawn when a policy deviates, and every policy in the
tables below plays the same eight futures.

### Receding-horizon clairvoyant play, and exactly what it bounds

A single nine-move optimum cannot answer the sustainability question. Nine moves
is under two rise cycles, so a nine-move line can build height it never has to
pay for; **its flow rate is an over-estimate of anything sustainable.** §1 below
measures that over-estimate explicitly so the size of the effect is on the
record.

The measurement that answers the question plays whole games: at every move, cut
an exactly-solvable horizon-H window out of the fixed future, solve it exactly,
play the first move of the optimal line, and re-solve from the new position.

- This is a **lower bound** on sustainable optimal flow. A globally optimal
  player is at least as good as one that re-plans every move with an H-move
  exact lookahead.
- It is **not an upper bound and not globally optimal**. A rate measured here
  that falls short of 2.400 would not prove 2.400 unreachable.
- Because the bound is a lower bound, a rate that *reaches* balance is
  conclusive in the direction that matters. That is what happened.
- `--horizon` is swept over 5, 7 and 9 so the reader can see whether the
  measured rate is still climbing with lookahead or has flattened.

### Two objectives, because score is not flow

`scenario/solver.hpp` maximizes points. The conservation law is about discs.
`flow-solver.hpp` therefore provides a pluggable per-move objective:

- **`rh-points`** maximizes the window's score delta — exactly the frozen
  solver's objective;
- **`rh-clears`** maximizes numbered discs cleared in the window, the quantity
  the conservation law is about, and therefore the more direct probe of the
  ceiling.

Both read the hidden board and are diagnostics only. `fair-d4` (the frozen
comparator, unmodified, through `chooseDepth4Action`) and `lowest-column` read
only public state and are the controls.

### Validation before any number was taken

| Gate | Scale | Result |
| --- | --- | --- |
| `flow-run --self-test`: master tape determinism | 2 tapes | ok |
| a cut window reproduces the long game move for move (board, latent, score delta, wave list) | 24 windows x 6 moves | ok |
| per-move score identity `delta = 17,000 x rise + 70,000 x clears + sum wave points` | a whole game, and every move of every game below | **0 violations in 19,610 moves** |
| each objective's optimum dominates on its own quantity | 8 windows | ok |
| **`flow-run --cross-check`: `flow-solver.hpp` with the points objective vs the frozen exact solver** | **128 scenarios of `suite-h9-v1`, H = 9** | **0 mismatches, 0 incomplete** |
| every solved window's principal variation replayed through the engine must earn the reported value | 9,175 windows across the two primary cohorts | **0 mismatches** |
| `lowest-column` whole-game control vs the base-engine figure of `finding-01` | 8 games | 33.12 moves / 97,110 points here vs 33.28 / 100,050 there |
| `fair-d4` whole-game control vs `finding-01` | 8 games | 1.9808 clears and 1.0896 reveals per move here vs 1.973 / 1.090 there |

The last two matter: they show the latent randomness model and this long-game
driver reproduce known public-policy behaviour, so the clairvoyant numbers are
being measured on the same game.

## 1. The nine-move optimum, and how much it over-states flow

`pv-replay` replays the stored principal variation of every scenario in
`approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl` (128 positions,
H = 9, all solved exactly) and measures it move by move. Fair depth 4 is played
over the identical positions, tape and latent board.

| Over 128 scenarios, H = 9 | clairvoyant optimum | fair depth 4 |
| --- | ---: | ---: |
| moves played | 1,115 | 1,103 |
| lines that died inside the horizon | 9 | 15 |
| **numbered clears / move** | **2.7444** | 2.0762 |
| **covered reveals / move** | **1.4565** | 1.0879 |
| per-line mean clears / move | 2.6951 | 2.0444 |
| occupied cells, mean start → end | 24.23 → **20.80** | 24.23 → **26.34** |
| covered cells, mean start → end | 15.61 → 14.68 | 15.61 → 17.61 |
| score share: rise / board clear / chain | 53.77% / 0% / 46.23% | 90.86% / 0% / 9.14% |
| waves, deepest | 2,249, depth 20 | 1,615, depth 14 |
| board clears | 0 | 0 |
| replayed score equal to the stored optimum | 128 / 128 | — |

Two things are visible immediately. Over nine moves the optimal line **empties**
the board (24.23 → 20.80 occupied) while fair D4 **fills** it (24.23 → 26.34),
which is the flow deficit made concrete on a single instrument. And the optimum
clears 2.7444 per move, comfortably above 2.400.

**That 2.7444 is not a sustainable rate.** It is a nine-move number and the
horizon is doing part of the work: a line ending at move 9 is free to spend
structure it would otherwise need later. The evidence shows that it is an
upper bracket. The receding-horizon measurement below is the lower bracket, and
the truth lies between them.

## 2. Whole games under receding-horizon clairvoyant control

Eight master tapes, seeds `0xa5230000`–`0xa5230007`, identical for every policy.
400-move cap. Commit one move per solve.

A note on the cohorts before the tables. A master tape draws its disc sequence
first and its risen rows afterwards, so its length — and therefore the hidden
rows — depends on the move cap. The 400-move cohort below and the 1,000-move
cohort in §2.1 share seeds but are **independent futures**, not a continuation.
That makes §2.1 an independent replication rather than a longer look at the same
games, which is the stronger of the two readings.

| policy | mean moves | censored at the cap | mean score | clears / move | reveals / move | occupancy slope |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| lowest column | 33.12 | 0 / 8 | 97,110 | 1.1774 | 0.5019 | **+4.47** |
| fair depth 4 | 117.75 | 0 / 8 | 409,985 | 2.0467 | 1.1423 | **+0.99** |
| `rh-points` H = 9 | 165.00 | 0 / 8 | 1,123,130 | 2.2000 | 1.2561 | **+0.44** |
| `rh-clears` H = 5 | 345.00 | 5 / 8 | 1,296,404 | 2.3496 | 1.3641 | **+0.053** |
| `rh-clears` H = 7 | 366.25 | 6 / 8 | 1,422,001 | 2.3601 | 1.3785 | **+0.014** |
| **`rh-clears` H = 9** | **396.88** | **7 / 8** | **1,544,461** | **2.3663** | **1.3839** | **−0.009** |
| **requirement** | ∞ | | | **2.400** | **1.400** | **0.000** |

Clears per move is pooled over all moves of all games. The per-game means are
1.1537 / 1.9808 / 2.0812 / 2.3423 / 2.3553 / 2.3662 in the same order, so the
pooled figures are not a survivorship artifact. Occupancy slope is the mean over
games of the least-squares slope of occupied cells against cycle index from
cycle 2 onward, in cells per five-move cycle.

The slope column is the cleanest single diagnostic in this document, and it is
not an independent measurement — it is the conservation law integrated. Over any
stretch of play, twelve discs enter per five-move cycle and five times the clear
rate leave, so occupancy drifts by `12 − 5 x clears-per-move` cells per cycle. A
policy whose occupancy rises monotonically is losing the battle regardless of
what its instantaneous clear rate looks like in any one position.

The whole-game clear rate above is dragged down by the opening, where the board
is too sparse to clear anything (§3). Excluding the first five cycles — measuring
only moves 26 and later, once the board has reached its operating point — gives
the **steady-state** rate, which is what the conservation law is really about:

| policy | clears / move, moves 26+ | implied drift, cells / cycle |
| --- | ---: | ---: |
| fair depth 4 | 2.1213 | +1.394 |
| `rh-points` H = 9 | 2.2643 | +0.679 |
| `rh-clears` H = 5 | 2.3855 | +0.072 |
| `rh-clears` H = 7 | 2.3963 | +0.018 |
| **`rh-clears` H = 9** | **2.3966** | **+0.017** |
| **requirement** | **2.400** | **0.000** |

**In steady state the clairvoyant clear-seeker sustains 99.86% of the exact
requirement.** The residual drift of 0.017 cells per five-move cycle would take
roughly 1,700 cycles — 8,500 moves — to fill a board from its 20-cell operating
point, which is well past the point where the measurement itself is the limit
rather than the game.

- Lowest column loses 4.47 cells per cycle and is dead in seven cycles.
- Fair depth 4 loses 0.99 cells per cycle. It is not collapsing; it is bleeding,
  and 49 cells of board buys it about 24 cycles.
- **`rh-clears` at H = 9 has a slope of −0.009 cells per cycle, which is flat.**
  Mean occupancy at cycle 2 is 21.0 and at cycle 80 is 19.7, over eighty
  cycles. The board is in a steady state.

### The occupancy trend, cycle by cycle (400-move cohort)

Mean occupied cells (of 49) after each five-move cycle; the count of surviving
games at that cycle is in parentheses.

| policy | c1 | c2 | c5 | c10 | c20 | c30 | c40 | c60 | c80 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| lowest column | 16.5 (8) | 24.8 (8) | 37.9 (8) | — | — | — | — | — | — |
| fair depth 4 | 15.8 (8) | 19.4 (8) | 22.8 (8) | 25.9 (8) | 30.0 (4) | 25.0 (1) | 28.0 (1) | — | — |
| `rh-points` H = 9 | 16.8 (8) | 24.6 (8) | 21.0 (8) | 26.0 (7) | 32.8 (4) | 23.0 (3) | 24.0 (3) | 33.0 (2) | — |
| `rh-clears` H = 9 | 16.0 (8) | 21.0 (8) | 19.1 (8) | 17.8 (8) | 18.2 (8) | 20.1 (8) | 19.8 (8) | 21.9 (8) | 19.7 (7) |

Read the surviving-game counts as well as the means: fair D4 and `rh-points`
appear to stabilise only because their crowded games have already died. All
eight `rh-clears` games are still in the average at cycle 60.

### 2.1 The same policy on six fresh futures, run four times as long

The 400-move cap was itself the binding constraint on seven of eight games, so
the primary cohort was re-run on six fresh futures with a 1,000-move cap.

| `rh-clears` H = 9, 6 games, cap 1,000 | value |
| --- | ---: |
| games reaching the cap **alive** | **6 / 6** |
| mean moves | 1,000.00 (censored) |
| mean score | **3,865,157** |
| clears / move, whole game | **2.3875** (99.5% of 2.400) |
| reveals / move, whole game | **1.3963** (99.7% of 1.400) |
| clears / move, moves 101+ | 2.3993 |
| **clears / move, moves 501–1000** | **2.4023** — *above the requirement* |
| implied occupancy drift, moves 501–1000 | **−0.012 cells per cycle** |
| occupancy slope, cycles 2–200 | −0.0015 cells per cycle |
| mean occupied cells at cycle 2 / 100 / 200 | 20.67 / 20.67 / 19.50 |
| final occupied cells, per game | 19, 23, 25, 18, 19, 13 |
| board clears | 0 |
| score-identity violations / PV mismatches | 0 / 0 |
| windows that exceeded their solve budget | 1 of 6,000 |

**Over the second half of these games the clairvoyant clear-seeker sustains
2.4023 numbered clears per move, which is the conservation requirement, and its
board drifts *downward* by 0.012 cells per cycle.** Mean occupancy at cycle 200
is 19.50 against 20.67 at cycle 2. This is not a policy that is slowly losing; it
is a policy in equilibrium, measured over 200 consecutive rise cycles and 6,000
moves with no death.

The residual whole-game deficit (2.3875 rather than 2.400) is entirely the
opening: the first hundred moves are played on a board too sparse to clear (§3),
and that debt is never repaid because it does not need to be — the board
stabilises at 20 cells, not at 7.

## 3. The mechanism — a stable equilibrium at ~20 cells

Disc conservation is exact, so the number of discs a move clears can be
reconstructed from the recorded occupancy trace without further instrumentation:
`cleared_i = occupied_before + 1 + 7 x rise_i − occupied_after`. Every game below
passed the check that this reconstruction reproduces the engine's own clear
total.

Clears per move, conditioned on how full the board was **before** the move:

| occupied cells before the move | `rh-clears` cap 1,000 | `rh-clears` cap 400 | fair depth 4 | `rh-points` H = 9 |
| --- | ---: | ---: | ---: | ---: |
| 0–9 | 1.08 *(n=274)* | 0.92 *(n=154)* | 0.87 *(n=38)* | 0.27 *(n=33)* |
| 10–14 | 1.48 *(n=1296)* | 1.44 *(n=667)* | 1.38 *(n=63)* | 0.60 *(n=73)* |
| 15–19 | 2.16 *(n=2279)* | 2.08 *(n=1183)* | 1.91 *(n=173)* | 0.66 *(n=205)* |
| 20–24 | **2.97** *(n=1568)* | **2.89** *(n=862)* | 2.10 *(n=266)* | 0.97 *(n=288)* |
| 25–29 | **4.21** *(n=533)* | **4.10** *(n=269)* | **2.59** *(n=239)* | 1.99 *(n=314)* |
| 30–34 | **5.70** *(n=50)* | **8.58** *(n=38)* | 2.26 *(n=78)* | **4.66** *(n=258)* |
| 35–39 | — | 13.00 *(n=2)* | 1.58 *(n=45)* | **4.20** *(n=123)* |
| 40–44 | — | — | 1.40 *(n=30)* | 3.58 *(n=26)* |
| 45–49 | — | — | 1.00 *(n=10)* | — |
| **required** | **2.400** | **2.400** | **2.400** | **2.400** |

`n` is the number of moves observed in that band. Each policy generates its own
distribution over occupancies, so the bands are not matched samples and the
sparsely populated cells (`rh-clears` above 30 cells, which it visits 50 times in
6,000 moves) should not be over-read. The two `rh-clears` cohorts are independent
futures and agree band for band, which is the strongest internal check available
on this table.

This is the whole finding in one table.

**A near-empty board cannot clear.** Drop7 clears a disc when its number equals
the length of the row or column run it lands in, so a sparse board offers almost
nothing to match against: at 0–9 occupied cells even a clairvoyant clear-seeker
manages 0.92 per move. The clear rate is an increasing function of occupancy.

**`rh-clears` therefore sits on a fixed point.** Below about 20 cells it clears
less than 2.400 and the board fills; above 20 it clears more and the board
empties. Negative feedback, an attracting equilibrium at roughly 19–20 occupied
cells, and a measured steady state that held for 200 cycles. Flow balance is not
merely reachable in principle — it is the stable operating point of a policy
that aims at it.

For the record, and closing a cheap hypothesis: **fair D4 is not dying from
excessive height aversion.** Its mean occupancy is 23.91 cells over all moves
and 25.63 after the opening, against the clairvoyant clear-seeker's 17.93 and
17.98. D4 runs a board about six cells *fuller* than the clairvoyant
equilibrium and still extracts less at every band below. It is not
conservative; it is ineffective at the height it already plays at.
See [`finding-07`](finding-07-fair-planning-ceiling.md) §5.

**Fair depth 4 has no such fixed point.** Its clear rate peaks at 2.59 in the
25–29 band, only barely above the requirement, and then *falls* — 2.26 at 30–34,
1.58 at 35–39, 1.40 at 40–44, 1.00 at 45–49. Once its board is pushed past about
30 cells the rate drops below the requirement and the deficit compounds. That is
a death spiral, and it explains why lifetime is heavy-tailed rather than
concentrated: a fair-D4 game lives as long as it happens to stay under the
threshold and then goes quickly.

The gap between the two is a *control* gap. At 20–24 occupied cells the
clairvoyant clear-seeker extracts 2.89 clears per move and fair D4 extracts 2.10
from the same regime.

> **CORRECTED.** The sentence above is left as originally written and is wrong
> as stated. This document did not separate the planner's two privileges —
> exact windowed planning, and reading the hidden board — so it could not
> attribute the gap to either.
> [`finding-07`](finding-07-fair-planning-ceiling.md) removes them one at a
> time and measures the split: a legal receding-horizon planner with the same
> objective, the same horizon and the same master tapes closes **58.8%** of this
> gap at H = 7 (40.0% at H = 5, where both planners are saturated), knowing the
> future disc tape closes **none** of it, and the remaining **at most 41.2%** is
> hidden board plus the known suboptimality of hindsight optimization. So the
> gap is majority planning — but the legal planner still died in every game.

## 4. Score composition, and what chains are actually worth

| policy | rise bonus | board clear | chain waves | points / move |
| --- | ---: | ---: | ---: | ---: |
| `finding-01` fair D4, base engine, 64 games | 94.29% | 0.00% | 5.71% | 3,423 |
| lowest column | 98.47% | 0.00% | 1.53% | 2,932 |
| fair depth 4 | 93.81% | 0.00% | 6.19% | 3,482 |
| `rh-clears` H = 5 | 89.99% | 0.00% | 10.01% | 3,757 |
| `rh-clears` H = 7 | 87.27% | 0.00% | 12.73% | 3,882 |
| `rh-clears` H = 9 | 87.23% | 0.00% | 12.77% | 3,891 |
| `rh-clears` H = 9, cap 1,000 | 87.97% | 0.00% | 12.03% | 3,865 |
| `rh-points` H = 9 | 48.44% | 0.00% | **51.56%** | **6,807** |
| clairvoyant optimum, H = 9 windows | 53.77% | 0.00% | 46.23% | 6,097 |

The `rh-points` row is the direct test of the World A hypothesis. Given the same
futures and the same exact nine-move lookahead, a planner told to maximize score
finds a completely different game: **more than half its points come from chain
waves**, and it earns 6,807 points per move against `rh-clears`'s 3,891 — a 1.75x
better rate. It still loses, badly, on the only statistic that matters:

- `rh-points`: 165.00 mean moves, **1,123,130** mean score, 0 / 8 alive at the cap.
- `rh-clears`: 396.88 mean moves, **1,544,461** mean score, 7 / 8 alive at the cap.

Chain construction is real, large, and currently unexploited — and it is not the
lever. Maximizing it over a nine-move window buys 1.75x the scoring rate and
pays 2.4x the lifetime for it.

### Wave-depth distribution

Share of all waves at each depth. `finding-01` measured 44.0% at depth 1 and a
deepest-ever depth of 11 across 8,444 waves in 64 fair-D4 games.

| | fair D4 (this run) | `rh-clears` H = 9 | `rh-clears` cap 1,000 | `rh-points` H = 9 | H = 9 optimum |
| --- | ---: | ---: | ---: | ---: | ---: |
| waves observed | 1,374 | 5,018 | 9,501 | 2,179 | 2,249 |
| depth 1 | 42.36% | 36.09% | 36.50% | 24.78% | 24.68% |
| depth 2 | 24.38% | 21.32% | 21.49% | 13.17% | 13.38% |
| depth 3 | 15.87% | 14.63% | 14.64% | 8.08% | 9.96% |
| depth 4 | 8.59% | 9.92% | 10.20% | 6.10% | 8.05% |
| depth 5–10 | 8.66% | 17.32% | 16.50% | 26.34% | 31.58% |
| **depth ≥ 11** | **0.22%** | 0.72% | 0.67% | **21.53%** | 12.36% |
| **depth ≥ 15** | **0.00%** | 0.00% | 0.06% | **7.76%** | 2.67% |
| deepest | 12 | 14 | 19 | **22** | 20 |

So the answer to "does clairvoyant play routinely reach depth 15+?" is: **only
when it is told to maximize points.** A clairvoyant *survival* player lives at
depth ≤ 14 and puts 0.72% of its waves past depth 10 — barely more than fair D4.
Deep cascades are not lottery tickets; they are reliably constructible with
perfect knowledge, at a price in lifetime that exceeds what they pay.

## 5. Board clears and the known upward bias

[`audit-01`](audit-01-engine-fidelity.md) finding **H2** (cited as "M1" in
[`finding-02`](finding-02-scenario-benchmark.md) §6 and in the brief for this
work; the audit's own label is H2) records that this engine tests for an empty
board *before* the row rise as well as after it, so a board clear that lands on a
fifth drop is paid 70,000 points that the cited reference does not award at all:
87,007 here against 17,007 there. The audit's warning is specific — "a
search-based candidate that can see the rise coming has a direct incentive to
schedule clears there" — and a clairvoyant solver is exactly such a candidate.

Every cohort in this document is therefore reported with its board-clear
accounting split out. `clear awards` counts 70,000 bonuses paid; `fifth-drop`
counts awards that landed on a rise move and are therefore H2-affected;
`double award` counts the literal both-checks-fired-in-one-move case.

| cohort | moves | board-clear moves | 70,000 awards | fifth-drop (H2-affected) | double awards |
| --- | ---: | ---: | ---: | ---: | ---: |
| `suite-h9-v1` clairvoyant optimal lines | 1,115 | 0 | 0 | 0 | 0 |
| `suite-h9-v1` fair depth 4 | 1,103 | 0 | 0 | 0 | 0 |
| lowest column, 8 whole games | 265 | 0 | 0 | 0 | 0 |
| fair depth 4, 8 whole games | 942 | 0 | 0 | 0 | 0 |
| `rh-points` H = 9, 8 whole games | 1,320 | 0 | 0 | 0 | 0 |
| `rh-clears` H = 5 / 7 / 9, 24 whole games (cap 400) | 8,865 | 0 | 0 | 0 | 0 |
| `rh-clears` H = 9, 6 whole games (cap 1,000) | 6,000 | 0 | 0 | 0 | 0 |
| **`sparse-probe-h9-v1` clairvoyant optimal lines** | **2,304** | **14** | **14** | **9** | **0** |

**The H2 bias inflates nothing in the flow measurement.** No policy measured on
a realistic board — including two privileged clairvoyant ones with exact
nine-move lookahead — ever emptied the board, in 19,610 measured moves. The
verdict of this document does not touch the bonus.

The one cohort where clears do occur is `sparse-probe-h9-v1`, 256 deliberately
sparse positions of 3–14 occupied cells, re-solved here (256/256 exact, 12
scenarios whose optimum clears, two of them twice — reproducing
[`finding-02`](finding-02-scenario-benchmark.md) §6 exactly). There the audit's
prediction is confirmed quantitatively:

- **9 of the 14 clears (64%) landed on a fifth drop.** H2 estimates "one board
  clear in five falls on a fifth drop by position alone"; a clairvoyant planner
  puts nearly two thirds of them there.
- Those 9 awards are **630,000 points of the cohort's 980,000 board-clear points
  and 4.9% of its total score of 12,872,636** — points the cited reference would
  not pay.
- The literal double award never fired, consistent with H2's own note that the
  post-rise check "is almost unreachable, since it requires all seven newly
  risen grays to be revealed and popped within the same move".

Anyone using board clears as a scoring lever must subtract that. The sparse
cohort's flow is also worth recording, because it is the counter-example that
makes §3's mechanism concrete: with a board of 3–14 cells, even the exact
optimum manages only **2.2739 clears and 1.1037 reveals per move**, and mean
occupancy *rises* over the nine moves (8.44 → 9.28). An empty board is not a
safe board; it is a board that cannot clear.

## 6. Horizon sensitivity

The receding-horizon estimate is a lower bound whose tightness depends on the
lookahead. Sweeping it:

| lookahead | clears / move | reveals / move | mean moves | alive at 400 | occupancy slope |
| ---: | ---: | ---: | ---: | ---: | ---: |
| H = 5 | 2.3496 | 1.3641 | 345.00 | 5 / 8 | +0.053 |
| H = 7 | 2.3601 | 1.3785 | 366.25 | 6 / 8 | +0.014 |
| H = 9 | 2.3663 | 1.3839 | 396.88 | 7 / 8 | −0.009 |
| requirement | 2.400 | 1.400 | ∞ | 8 / 8 | 0.000 |

The rate is monotone in the lookahead and nearly flat: four extra plies of exact
clairvoyant search buy 0.0167 clears per move, 0.7% of the requirement. Even
H = 5 — a lookahead barely longer than one rise cycle — already reaches 99.4% of
the steady-state requirement and keeps five of eight games alive to 400 moves.

The horizon axis is close to exhausted: `finding-02` measured H = 9 as the
largest horizon solvable exactly in seconds, H = 11 as the practical limit, and
H = 12 as out of reach. Whole games at H = 11 were not affordable under this
work's resource budget and were not attempted, so the trend above stops at nine.
The direction of the missing points is not in doubt — the sequence is
monotonically increasing and a longer lookahead can only weakly improve an exact
receding-horizon controller — but the exact limit is not measured.

For the strategic question this does not matter. §2.1 measures the steady-state
rate at H = 9 directly and finds 2.4023 over 3,000 moves, so the requirement is
already met at the horizon that is affordable. A larger horizon would raise the
margin, not decide the verdict.

## 7. Verdict

**Flow balance is achievable. The evidence supports World B: Drop7 Hardcore is a
control problem, not a strictly losing battle.**

Stated precisely, and with the bound direction made explicit:

- A **lower bound** construction — receding-horizon clairvoyant play with a
  nine-move exact lookahead, committing one move at a time against a fixed
  future — sustains **2.3875 numbered clears and 1.3963 covered reveals per move
  over 6,000 moves**, and **2.4023 clears per move over the second half of those
  games, which is the conservation requirement rather than an approach to it.**
  The whole-game shortfall is the sparse opening, and the opening is not part of
  any steady state.
- Under it, board occupancy is **stationary at ~19–20 of 49 cells for 200
  consecutive five-move cycles**, with a fitted trend of −0.0015 cells per
  cycle and a mean of 20.67 cells at cycle 2 against 19.50 at cycle 200.
  Occupancy does not rise monotonically. The battle is not being lost.
- **Every game reached the move cap alive: 6 / 6 at 1,000 moves and 7 / 8 at
  400 moves on a separate cohort.** Mean lifetime is a censored lower bound of
  1,000 moves, against fair depth 4's 117.75 on identical futures. `finding-01`
  put the lifetime a 1,000,000-point mean needs at ~294 moves; the ceiling is at
  least three times that.
- The mechanism is an **attracting equilibrium**: the achievable clear rate is
  increasing in occupancy and crosses 2.400 near 20 cells, so the operating
  point is self-correcting. Fair D4's rate crosses 2.400 only in a narrow band
  around 27 cells and *falls* beyond it, so its operating point is not — which
  is why its games end and why its lifetime is heavy-tailed.
- The alternative world is **positively excluded**, not merely unsupported.
  Chain-maximizing clairvoyant play on the same futures produced 1.75x the
  points per move, 21.5% of waves past depth 10 and cascades to depth 22 — and a
  mean score 27% *lower*, because it lost 2.4x the lifetime.

What this does **not** establish:

- It does not produce a policy. The planner reads the hidden board. Every
  measurement here is a ceiling and a teacher signal, not a candidate.
- It does not prove balance holds *forever*. It holds over 6,000 measured moves
  and 200 cycles with a slightly negative drift; nothing here excludes a slow
  failure on a scale ten times larger. §6.
- It does not make a qualification claim. The censored mean of 3,865,157 points
  is a privileged policy's score in the latent randomness model on six
  development seeds. It is not comparable with a ledger figure and it is not
  evidence about any public policy.

### What this changes for the research program

The program's target is a mean above 1,000,000 in corrected five-move Hardcore.
`finding-01` reduced that to a lifetime target of roughly 294 moves at 3,400
points per move. This document shows a lifetime of at least 1,000 moves is *achievable in the
game* and identifies precisely what a policy must do to get it: hold board
occupancy near 20 cells, where the achievable clear rate exceeds 2.400.

That is a far more specific and far more learnable objective than "play better".
It also supplies the target variable. The gap between fair D4 and the ceiling is
concentrated in one measurable place — the 20–29 occupied-cell band, where the
clairvoyant planner extracts 2.89 and 4.10 clears per move and fair D4 extracts
2.10 and 2.59 — and a receding-horizon clairvoyant player generates as many
labelled (position, best-column) pairs as anyone wants, all of them on the
trajectory a strong policy would actually visit rather than on positions a weak
policy stumbles into.

> **QUALIFIED by [`finding-07`](finding-07-fair-planning-ceiling.md).** Up to
> 41% of this teacher's margin comes from information the student will never
> have, so a student fitted to clairvoyant labels is partly being asked to learn
> a function of hidden state. finding-07 §6 argues the better-posed target is to
> distil the *fair* planner instead — legal by construction, already 0.18
> clears per move ahead of D4, and expensive enough (256 exact window solves per
> move) that amortising it is exactly what a learned evaluator is for.

Conversely, the chain-construction line of hope should be **de-prioritised as a
primary lever**. It is real: the engine pays it, and chain-seeking clairvoyant play collects
3,510 chain points per move against fair D4's 216, a factor of 16. It is also, measured directly and on paired
futures, a net loss when pursued.

## Limitations

1. **Six to eight games per policy, on one exploratory lease.** Every
   whole-game figure here is a small cohort. The paired design (identical master
   tapes for every policy within a cohort) removes most of the between-game
   variance from the comparisons, but the absolute means carry wide intervals.
   The flow rates are far better determined than the scores: they average
   thousands of moves rather than a handful of games.
2. **Every primary game is censored at the move cap** — 6/6 at 1,000 moves and
   7/8 at 400. Mean lifetime and mean score are lower bounds, and the occupancy
   trend is measured over 200 cycles, not forever. Nothing here excludes a slow
   failure on a much longer scale.
3. **The planner is privileged.** `rh-clears` and `rh-points` read the latent
   board. Nothing here is a deployable policy or a step toward one on its own.
4. **The latent randomness model is not the base engine's model** (audit-01 M2,
   finding-02 §2). Scores are not bit-comparable with any ledger figure. The two
   public controls reproduce their base-engine flow rates and lifetimes closely,
   which is the evidence offered that the games are the same game. Absolute
   scores also inherit audit-01 **H1** (the level bonus is forfeited on the
   terminating rise, a −17,000 bias per death), which is negligible here because
   almost every game is censored rather than terminated.
5. **A master tape is a function of the seed *and* the move cap**, because its
   risen rows are drawn after its disc tape. The 400-move and 1,000-move cohorts
   therefore share seeds but not futures. This makes them independent samples
   rather than nested ones, which is the more useful arrangement here, but the
   two cohorts must not be described as the same games run for longer.
6. **Receding-horizon play is not globally optimal.** It is a lower bound. A
   globally optimal player would do at least as well, so the direction of the
   conclusion is safe, but the exact ceiling is unknown.
7. **The horizon is nearly exhausted.** H = 9 is the largest horizon
   `finding-02` solves exactly within seconds; 37 of 3,175 windows (1.2%) in the
   400-move run (and 1 of 6,000 in the 1,000-move run) exceeded their
   12-second budget at H = 9 and were re-solved exactly at H = 7, which if
   anything biases the measured rate *down*.
8. **`rh-clears` is not the flow-optimal policy.** Maximizing clears over a
   nine-move window is a heuristic proxy for maximizing sustained flow; it is
   myopic about where it leaves the board. The true clairvoyant ceiling is at
   least what it achieved.
9. **The occupancy-conditional table is descriptive, not causal.** Each policy
   generates its own distribution over occupancies, so the bands are not matched
   samples; a policy that rarely visits 30–34 cells is measured there on few
   moves. The counts are reported for that reason.
10. **The opening position is this engine's, not the reference's.** audit-01
   finding **H3** records that the repository opens Hardcore above a solid gray
   row while the cited reference's Blitz opens with 11–21 numbered discs and no
   grays. Every whole game here starts from the repository's opening, so the
   first cycle's very low clear rate (§3) is partly an artifact of that choice.
   It affects the opening, not the steady state, which is why §2 reports both.
11. **The timings are not timing-grade.** Every run shared a 16-core / 32-thread
   machine with other jobs at load averages of roughly 36–40, using at most 10
   threads. `docs/benchmarks.md` requires exclusive resources for a performance
   claim; none is made here. Node counts are exact and machine-independent.
12. **A model contribution record under `research/contributions/` is owed and
    was not written**, because this work was scoped to create files only under
    `approaches/lifetime-objective/flow-ceiling/` and `docs/exploratory/`. The
    coordinator should add one (level `L3` for the flow-ceiling theory, the
    window solver, the receding-horizon measurement and this result) before it
    is promoted. The same debt is open for `finding-02`.
13. **`docs/research/status.md` still describes 2.4 / 1.4 as "diagnostic targets
    from limited runs, not proven universal thresholds."** `finding-01` derived
    them exactly; this document measures a line of play that meets them. Only
    the coordinator edits that table.

## Reproduce

```sh
# build (clang++ explicitly; the Makefile's CXX ?= clang++ loses to make's
# builtin CXX=g++, which trips a false -Werror=array-bounds in
# src/core/native/public-behavior.hpp)
./approaches/lifetime-objective/scenario/build.sh
./approaches/lifetime-objective/flow-ceiling/build.sh

# gates
./build/flow-ceiling/flow-run --self-test
./build/flow-ceiling/flow-run \
    --cross-check approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl \
    --threads 10

# 1. the nine-move optimum's flow, with a paired fair-D4 control
RID=RUN-FLOW-044da902f8e8
./build/scenario/solve \
    --input approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl \
    --threads 10 --jsonl runs/$RID/suite-h9-solved.jsonl
./build/flow-ceiling/pv-replay --input runs/$RID/suite-h9-solved.jsonl \
    --also-fair --jsonl runs/$RID/suite-h9-pv-replay.jsonl

# 2. whole games: the clairvoyant flow ceiling and its controls
for H in 5 7 9; do
  ./build/flow-ceiling/flow-run --policy rh-clears --games 8 \
      --seed-start 0xa5230000 --horizon $H --commit 1 --max-moves 400 \
      --threads 8 --time-limit 12 --jsonl runs/$RID/rh-clears-h$H-c1.jsonl
done
./build/flow-ceiling/flow-run --policy rh-clears --games 6 \
    --seed-start 0xa5230000 --horizon 9 --commit 1 --max-moves 1000 \
    --threads 6 --time-limit 12 --jsonl runs/$RID/rh-clears-h9-c1-long.jsonl
./build/flow-ceiling/flow-run --policy rh-points --games 8 \
    --seed-start 0xa5230000 --horizon 9 --commit 1 --max-moves 400 \
    --threads 8 --time-limit 12 --jsonl runs/$RID/rh-points-h9-c1.jsonl
./build/flow-ceiling/flow-run --policy fair-d4 --games 8 \
    --seed-start 0xa5230000 --max-moves 400 --threads 2 \
    --jsonl runs/$RID/fair-d4.jsonl
./build/flow-ceiling/flow-run --policy lowest-column --games 8 \
    --seed-start 0xa5230000 --max-moves 400 --threads 1 \
    --jsonl runs/$RID/lowest-column.jsonl

# 3. board clears and the audit-01 H2 bias, on the sparse probe
./build/scenario/solve \
    --input approaches/lifetime-objective/scenario/data/sparse-probe-h9-v1.jsonl \
    --threads 4 --time-limit 120 --jsonl runs/$RID/sparse-h9-solved.jsonl
./build/flow-ceiling/pv-replay --input runs/$RID/sparse-h9-solved.jsonl \
    --jsonl runs/$RID/sparse-h9-pv-replay.jsonl

# 4. the tables
python3 approaches/lifetime-objective/flow-ceiling/analyze.py runs/$RID/*.jsonl
```

### Seed lease

`SEEDLEASE-A52-FLOW` = `0xa5230000`–`0xa5233fff`, assigned to this work by the
coordinator, a sub-range of the `SEEDLEASE-A52` reserve recorded in
[`lease-map.md`](lease-map.md). Role: **exploratory development diagnostic**;
once read these seeds are development data permanently. Only `0xa5230000`–
`0xa5230007` were opened. `flow-run` refuses to start outside the lease.
`suite-h9-v1` and `sparse-probe-h9-v1` were consumed as already-minted data from
`SEEDLEASE-A51D-SCEN`; no new seed from that lease was drawn.

### Environment

AMD clang 23.0.0git, `-O3 -std=c++20 -pthread -Wall -Wextra`. 16 physical cores
/ 32 logical, 125 GiB RAM, shared with other jobs throughout at load averages of
roughly 36–40; at most 10 threads were used. Frozen sources were consumed
unmodified and their hashes are recorded at build time in
`build/flow-ceiling/sources.sha256`. The frozen fair depth-4 comparator is
compiled from a generated build-tree copy whose only changed line is the entry
point, and the build refuses to proceed if more than that one line differs.
