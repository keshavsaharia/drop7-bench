# Finding 02 — A fully specified Drop7 scenario, its exact optimum, and what perfect knowledge is actually worth

**Status:** exploratory, evidence tier `pilot`/`development`. Built and measured
in this checkout on 2026-08-20.
**Namespace:** `approaches/lifetime-objective/scenario/`, data in
`approaches/lifetime-objective/scenario/data/`, seed lease
`SEEDLEASE-A51D-SCEN` = `0xa51dc000`–`0xa51dffff`.
**Nothing in `docs/research/`, `artifacts/`, `research/`, `runs/`, or any
existing approach source was modified by this work.**

## Why this exists

[`audit-01-engine-fidelity.md`](audit-01-engine-fidelity.md) finding **M2**
records that this engine draws a covered disc's number from the reveal RNG *at
the moment of reveal*, not when the row was created
(`engine.hpp:260-262`, `engine.ts:366-368`). Its second consequence is the one
that blocks research:

> `docs/methodology.md:41-43` permits "privileged planners" to inspect future
> randomness to generate labels. Under this engine there is no true hidden
> value to inspect — only a counterfactual that changes the moment the student
> deviates.

So the repository has never had a well-posed clairvoyant baseline. You cannot
ask "what was the best possible play here?" when the answer changes depending on
which cells you happen to open, in which order.

This work builds that missing object: a **scenario**, a Drop7 position in which
every future random quantity is fixed in advance, so the game becomes a
deterministic single-player perfect-information puzzle with an exact optimum.

## What was built

`approaches/lifetime-objective/scenario/`

| File | What it is |
| --- | --- |
| `scenario.hpp` | The repository move loop with the reveal source factored out, plus the `Scenario` record, its content hash, and its validator |
| `scenario-io.hpp` | JSONL serialization and a self-verifying loader |
| `solver.hpp` | Exact depth-first clairvoyant solver: transposition table, admissible branch-and-bound, thread pool, principal-variation reconstruction |
| `generate.hpp` | Two position samplers: harvested from real play, and synthetic with controlled occupancy/cover/number profile |
| `scenario-parity.cpp` | **The parity gate** |
| `solve.cpp` | Solver CLI, self-test, horizon sweep, and an auditable optimal-line replay |
| `mint.cpp` | Candidate generation and difficulty labelling |
| `build.sh` | clang++ build; renames the frozen depth-4 entry point in the build tree only |

The move loop is not re-derived. It calls the shared `drop7::` primitives —
`findPoppers`, `applyGravity`, `raiseCoveredRow`, `scoreForWave`,
`isBoardEmpty`, `placeDisc`, `legalColumns`, `isLegal` — and reproduces the
damage/reveal scan and the rise-boundary scoring of `drop7::playMove`
statement for statement. The only parameterized point is where a revealed
cell's number comes from.

Two reveal sources:

- **`StreamRevealSource`** pulls from a `drop7::Mulberry32`, in the engine's
  consumption order. This is the base game.
- **`LatentRevealSource`** consumes a value that was already sitting under that
  specific covered cell. Cells introduced by a row rise take their values from a
  pre-specified per-rise array. A parallel `latent[49]` array is permuted by
  exactly the same gravity and rise transforms as the board.

## 1. The parity gate — zero mismatches

`ScenarioEngine<StreamRevealSource>` must be trajectory-identical to
`drop7::playHeadlessMove`, or nothing downstream means anything.

```sh
./approaches/lifetime-objective/scenario/build.sh
./build/scenario/scenario-parity --seeds 4096
```

| Check | Scale | Result |
| --- | ---: | --- |
| Paired gravity transform vs `drop7::applyGravity` | 20,000 random boards | OK |
| Paired rise transform vs `drop7::raiseCoveredRow` | 20,000 random boards | OK |
| Trajectory parity, center-first policy | 4,096 seeds, 86,946 moves | **0 mismatches** |
| Trajectory parity, lowest-column policy | 4,096 seeds, 131,524 moves | **0 mismatches** |
| Latent-source invariants + JSONL round trip | 64 games | OK |
| **Total** | **8,192 game-plays, 218,470 moves** | **0 mismatches** |

Compared on **every move**: all 49 board cells, next disc, score, score delta,
moves remaining, level, moves played, game-over flag, cleared-board flag,
level-advanced flag, and the complete wave list `(depth, cleared, revealed,
points)` element by element. A single differing field aborts that seed and is
counted.

Aggregate scores are bit-identical: center-first 228,382,336 points across
4,096 games under both engines; lowest-column 390,070,924 under both.

As an external sanity check, those give 55,758 points / 21.23 moves for
center-first and 95,233 / 32.11 for lowest-column, against
[`finding-01`](finding-01-score-is-survival.md)'s 57,233 / 21.64 and
100,050 / 33.28 measured on a different, disjoint lease.

The gate is the first thing `build.sh` produces and the first thing to run.
**If it is not zero, stop** — a mismatch means the scenario engine is not the
same game, and every optimum below would be an optimum of a different game.

## 2. `LatentRevealSource` is a *different game*, on purpose

This must not be glossed over.

| | Base engine (`StreamRevealSource`) | `LatentRevealSource` |
| --- | --- | --- |
| When a gray's number is decided | at reveal | when the gray was created |
| Marginal distribution | i.i.d. uniform 1..7 | i.i.d. uniform 1..7 |
| Depends on reveal *order* | yes (row-major scan order) | no |
| Same physical gray, different play order | can hold a different number | always the same number |
| Hidden state a teacher could inspect | none exists | the `latent[49]` board |

The marginal is measured, not assumed: 700,000 `Mulberry32::nextDisc()` draws
give chi-square 2.36 on 6 degrees of freedom, consistent with
audit-01's 3.59 over 400,000 draws.

Two consequences follow:

1. **This is strictly more faithful to real Drop7.** audit-01 M2 documents that
   the cited reference implementation fixes the value when the gray is created
   (`pieces.es6:77-81`, `drop7.es6:182`, `pieces.es6:93-107`). The base engine
   does not. `LatentRevealSource` does.
2. **Scores under it are not bit-comparable to base-engine runs.** A scenario
   score is not a base-engine score. Nothing in this document may be compared
   directly against a ledger figure produced by `playHeadlessMove`. The parity
   gate proves the *rules* are shared; it does not make the two randomness
   models interchangeable.

The one thing the latent model buys is decisive: because the hidden values do
not move when the student deviates, a clairvoyant optimum is well defined, and
the objection audit-01 raised against every oracle/teacher experiment in this
repository ("resting on a quantity the engine does not define") does not apply
inside a scenario.

## 3. The `Scenario` record

```text
Scenario {
  id[17]          16 hex characters of a content hash over every field below
  board[49]       0 empty, 1-7 numbered, 8 solid (2 hits), 9 cracked (1 hit)
  latent[49]      1..7 under every covered cell, 0 everywhere else
  movesRemaining  1..5, moves until the next rise
  horizon         H, the number of moves the scenario is scored over
  discTape[H]     the numbered discs the player receives, in order
  riseLatent[R][7] latent values for each future risen row,
                  R = ceil((H + movesRemaining)/5) + 1
}
```

Serialized one object per line as JSONL so a suite is diffable, streamable, and
language-neutral. `deserializeScenario` recomputes the hash and refuses any line
whose content does not match its `id`, so a suite is self-verifying. Unknown
keys are ignored, which lets `mint` write difficulty labels into the same
object while `solve` still loads the scenario alone.

The hash is FNV-1a 64 over an explicitly documented byte order, so any language
can recompute it.

`validateScenario` rejects a record unless the board is gravity-settled, every
covered cell has a latent value in 1..7, no visible cell carries one, the start
position contains **no already-poppable disc**, `movesRemaining` is in 1..5, the
tape length equals the horizon, the rise-row count matches, and at least one
column is legal.

The reason a scenario node is exactly `(board, latent, depth)` is that rises land
on fixed move indices no matter which columns are chosen: `movesRemainingAt` and
`risesConsumed` are functions of the depth alone, and so are the tape index and
the rise index.

### Designed for tape reuse — read this before selecting scenarios

`retapeScenario` and `resampleScenarioRandomness` hold `board`, `latent`, and
`movesRemaining` fixed and replace `discTape` and `riseLatent` with a fresh
draw. `reHorizonScenario` re-cuts a scenario to a shorter horizon.

This is the operation the fair multi-tape evaluation needs, and it is why the
record separates the start position from the randomness. That evaluation is
deliberately **not** implemented here — see §7.

## 4. The exact solver

Depth-first search over every legal move sequence to horizon `H`. Objective:
**maximize total points earned within `H` moves**, where dying terminates the
line and earns nothing further. Survival is therefore priced by the points it
would have bought, not by an invented penalty.

Exactness machinery:

- **Transposition table**, keyed on the full `(board[49], latent[49], depth)` —
  99 bytes, no lossy hashing, sharded 64 ways with per-shard mutexes. It stores
  a value only when that node was searched exhaustively.
- **Branch and bound.** A single-agent maximization tree admits no alpha-beta
  "fail high" cut, so the search contract is: `search(node, depth, alpha)`
  returns either `kPruned`, which asserts the node's true value is at most
  `alpha`, or the exact value. A child is discarded only when a provable upper
  bound cannot beat the best line already found, and a node is cached only when
  it is provably exact.
- The bound is admissible by construction: at most 49 discs sit on the board, the
  move adds one, a rise adds seven, and each cell-instance can clear at most
  once; reaching wave depth `d` costs at least one cleared disc in each of the
  `d-1` earlier waves. A greedy descent seeds the incumbent so the bound has
  something to beat from the first node.
- Parallelism expands a breadth-first prefix into independent subtrees and hands
  them to a thread pool with a shared table and a shared atomic incumbent.
- The principal variation is rebuilt from the table's stored best columns and
  then **replayed through the scenario engine**; if the replay does not earn
  exactly the reported optimum the result is marked incomplete rather than
  published. Across every run reported here that check never fired.

No sampling. No beam. When `H` is too large the run is reported `INCOMPLETE`; it
is never approximated.

### Self-test

```sh
./build/scenario/solve --self-test
```

| Test | Expected | Result |
| --- | --- | --- |
| Filling the last legal cell with an all-gray board | optimum 0, pv `3` | ok |
| A rise on an inert position | optimum 17,000 | ok |
| Completing a run of three 3s and emptying the board | 3·7 + 70,000 = 70,021, pv `2` | ok |
| Two isolated 2s, two moves | 2·7 + 2·7 + 70,000 = 70,028, pv `0-4` | ok |
| Re-horizon H=1..8: revalidate, id changes, optima monotone | monotone | ok |
| **Randomized cross-check vs a naive enumerator** | identical | ok |

The cross-check is the important one: 107 scenarios (harvested and synthetic,
H=1..6) are solved by a deliberately naive enumerator with no table, no bound,
no threads and no move ordering, and compared against **four** solver variants
(table on/off × bound on/off, 1 and 4 threads). All 428 comparisons agree, and
the privileged greedy never beats the optimum. 77 of the 107 have
greedy < optimum, so the solver is demonstrably doing more than taking the best
immediate score.

### Largest exactly solvable horizon

12 scenarios, 8 threads, 10-second limit per scenario:

| H | solved | nodes median | nodes max | sec median | sec max | TT hit % | bound cut % |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 6 | 12/12 | 4,784 | 15,750 | 0.005 | 0.013 | 28.4 | **0.00** |
| 7 | 12/12 | 21,960 | 70,734 | 0.019 | 0.073 | 25.3 | **0.00** |
| 8 | 12/12 | 109,640 | 379,284 | 0.098 | 0.310 | 25.6 | **0.00** |
| **9** | **12/12** | **563,874** | **2,155,597** | **0.336** | **2.371** | 26.3 | **0.00** |
| 10 | 10/12 | 1,063,335 | 3,703,247 | 0.788 | 4.788 | 31.4 | **0.00** |
| 11 | 8/12 | 1,164,574 | 14,775,762 | 0.953 | 9.643 | 37.4 | **0.00** |
| 12 | 5/12 | — | — | — | — | 31.8 | **0.00** |
| 13 | 4/12 | — | — | — | — | 3.8 | **0.00** |

Rows near the limit depend on machine load: a repeat of the sweep under
different contention solved 7/12 rather than 8/12 at H = 11 and moved the
H >= 11 medians, while H <= 10 reproduced to within a few percent.

**H = 9 is the largest horizon solved exactly for every scenario inside about
ten seconds on eight threads.** At H = 10 two of twelve time out; from H = 12
the only scenarios that finish are the ones whose lines die early. This was
confirmed at scale: the 128-scenario suite below solved **128/128 exactly** at
H = 9 with zero incomplete results.

### Pruning: the table is the entire speedup, the bound does nothing

12 scenarios at H = 9, 8 threads:

| Configuration | Search nodes | Wall seconds |
| --- | ---: | ---: |
| Table + bound | 6,887,055 | 6.96 |
| Table only | 6,722,783 | 6.78 |
| Bound only | 52,198,097 | 34.16 |
| Neither | 52,198,097 | 34.16 |

The transposition table is worth **7.6× in nodes and 4.9× in wall time**. The
admissible bound prunes **exactly zero nodes** — the last two rows are identical
to the node, and `bound cut %` is 0.00 at every horizon.

The no-table node counts are exactly deterministic and reproduced to the node
(52,198,097) across repeat runs; the with-table counts move by a few percent
with thread interleaving, which is the only reason row 1 differs from row 2.

This is a negative result worth recording rather than hiding. The bound we can
*prove* allows roughly 3.2 million points per move (a hypothetical 57-disc
cascade plus two clear bonuses plus a rise), while real optima are in the tens
of thousands. Any bound tight enough to prune would have to be a claim about
achievable chain structure, which is exactly the thing being measured. The small
difference between the first two rows is thread-scheduling nondeterminism in
table stores, not pruning.

## 5. The minted suite

```sh
./build/scenario/mint --harvested 64 --synthetic 64 --horizon 9 --threads 8 \
    --time-limit 60 --seed-offset 0x1800 \
    --output approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl
```

`approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl` — 128
candidates, H = 9, every one solved exactly, 390 s wall on 8 threads.

Positions come from two routes:

- **harvested (64):** snapshots of positions real games actually visit, taken by
  playing the base engine with the lowest-column policy and picking a uniform
  mid-game state, then assigning latent values from the lease RNG.
- **synthetic (64):** controlled occupancy (6–39 cells), cover fraction
  (0.10–0.66), and number profile (uniform / low-heavy / high-heavy), so the
  suite spans easy-open through near-death-crowded rather than only what the
  harvesting policy produces.

Every candidate is written out with its labels, not only the ones a selection
step would keep, so selection can be redone without regenerating anything.

Shallow comparators are the frozen fair search from
`approaches/fair-expectimax/reference/fair-only-depth4.cpp`, used unmodified
through a build-tree copy whose only changed line is the entry point (the same
technique as `score-decomposition/build.sh`, with the one-line diff enforced).
Fair depth 4 is `chooseDepth4Action` itself, the repository comparator. A
privileged one-ply greedy that maximizes the *true* immediate delta is recorded
separately as a diagnostic; it is not a policy, because it reads the reveals.

`bestShallow = max(fairD1, fairD2, fairD4)` and `gap = optimum - bestShallow`.

| | all 128 | harvested 64 | synthetic 64 |
| --- | ---: | ---: | ---: |
| Occupied cells (min / median / max) | 6 / 25 / 46 | 14 / 30 / 46 | 6 / 18 / 39 |
| Covered cells (min / median / max) | 0 / 16 / 36 | 9 / 21 / 36 | 0 / 8 / 28 |
| Mean clairvoyant optimum | 53,109 | 41,984 | 64,234 |
| Mean fair D1 / D2 / D4 | 29,385 / 29,983 / 30,402 | 28,001 / 27,988 / 27,686 | 30,770 / 31,977 / 33,119 |
| Mean privileged greedy | 28,295 | 26,487 | 30,103 |
| Fair D4 died inside H | 15 | 10 | 5 |
| Optimal line died inside H | 8 | 6 | 2 |
| Max chain depth in the optimal line | 20 | 19 | 20 |
| Max chain depth reached by fair D4 | 14 | 11 | 14 |

### Gap distribution (128 candidates, H = 9)

| statistic | gap |
| --- | ---: |
| min | 0 |
| q25 | 6,293 |
| median | **15,445** |
| q75 | 27,392 |
| q90 | 51,152 |
| max | 119,391 |
| mean | 21,653 |

`gap > 0` in 123/128, `gap = 0` in 5/128, never negative (as it must be — the
optimum dominates by construction, and this is an implicit correctness check
that held 128/128).

Two secondary observations:

- Deeper fair search is only mildly better here: D4 attains `bestShallow` in
  67/128 and D2 in 45/128, and on the *harvested* subset D4 is actually the
  worst of the three on the mean (27,686 vs 28,001 for D1). With one tape per
  position this is well inside noise and must not be read as "D4 is worse".
- The privileged greedy reaches `bestShallow` in only 21/128. Knowing the
  future is worth much less to a one-ply agent than to a nine-ply planner, which
  is the first quantitative sign that the gap is a *planning* gap and not simply
  an information gap.

## 6. Is a board clear reachable at all? — the headline number

[`finding-01`](finding-01-score-is-survival.md) recorded **zero board clears in
64 fair-D4 games** and flagged the open question explicitly:

> Board clears were absent here, so this cohort says nothing about whether a
> clear-seeking policy could change the economics. […] Nothing in this
> repository has measured whether that is reachable; that is a separate open
> question, not a refuted one.

It is now measured, with perfect knowledge, on an exact solver.

| Cohort | n | Occupied cells | Clears in the clairvoyant optimum |
| --- | ---: | --- | ---: |
| `suite-h9-v1` harvested | 64 | 14–46 | **0 (0.0%)** |
| `suite-h9-v1` synthetic | 64 | 6–39 | **0 (0.0%)** |
| `sparse-probe-h9-v1` | 256 | 3–14 | **12 (4.7%)** |

Broken down by occupancy inside the sparse probe:

| Occupied cells | n | scenarios whose optimum clears | rate |
| --- | ---: | ---: | ---: |
| 3–5 | 66 | 4 | 6.1% |
| 6–8 | 64 | 4 | 6.2% |
| 9–11 | 63 | 2 | 3.2% |
| 12–14 | 63 | 2 | 3.2% |

**A board clear is reachable — but only from board states real play does not
reach.** Two of the twelve clearing scenarios clear *twice* inside nine moves.
Clearing scenarios average an optimum of 116,855 against 47,010 for
non-clearing ones, so a clear is worth roughly 2.5× the whole nine-move budget.

How far outside real play is that regime? Over 256 uniform mid-game snapshots of
lowest-column games:

| occupied cells | min | q05 | q25 | median | q75 | max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| harvested snapshots | 11 | 16 | 22 | 29 | 36 | 47 |

Only 3.1% of real snapshots sit at 14 cells or fewer, and **none** at 8 or
fewer — which is where the clairvoyant clear rate is highest. Combined with
finding-01's conservation law (12 discs enter every five moves), this is a
coherent explanation of the zero-clear observation: a fair policy running a
flow deficit never gets the board sparse enough for a clear to be reachable even
*with* the answer key.

The correct reading is **not** "clears are impossible". It is:

- at H = 9 a clear is unreachable from any of 128 realistic-to-crowded
  positions even with perfect knowledge;
- at H = 9 a clear is reachable from about 1 sparse position in 21;
- H = 9 is itself a binding constraint. Emptying a 25-cell board needs roughly
  32 numbered clears across 9 moves against 7 more discs arriving on the rise,
  about 3.6 clears per move, far above the 2.4 per move that finding-01 derives
  as the steady-state requirement. **The 0% on realistic boards is therefore
  partly a statement about the horizon, not only about the positions.** A larger
  H would be the valid way to settle it, and H > 11 is not exactly solvable
  with this solver.

### A verified 20-deep cascade, and a confirmed audit-01 divergence

`solve --replay <id>` prints the optimal line move by move. The suite's best
scenario, `de5ad5aedc4a27e3` (optimum 165,300), spends moves 5–8 scoring 21
points in total and then collects **148,124 points in a single move** from a
cascade running to **depth 20**, with individual waves paying up to 30,495. For
comparison, audit-01 measured chain depth >= 7 in only 5 of 6,852 moves of its
parity cohort, and fair D4 across the whole suite here never exceeds depth 14.

So the chain machinery of this engine is far richer than the 5.7% of score that
finding-01 attributes to it in fair play. The 5.7% is a statement about what
fair D4 can *find*, not about what the engine can *pay*.

The replay also independently reproduces
[audit-01 finding **M1**](audit-01-engine-fidelity.md): in scenario
`2683eb6ba6a0ad48` the optimal line clears the board on a fifth drop and is paid
**87,007** (70,000 clear + 17,000 rise + 7 chain) where the cited reference
would pay 17,007. The clairvoyant solver walks straight into it. This is direct
evidence for M1's warning that "a search-based candidate that can see the rise
coming has a direct incentive to schedule clears there — harvesting a bonus the
cited reference does not award." Any future use of this suite for clear-seeking
work should treat the double-award as a known upward bias.

## 7. The selection-bias problem — read before using `gap` to select

Selecting scenarios where shallow search underperforms the clairvoyant optimum
**biases the suite toward positions that require knowing the future**. A public
policy cannot know the future. A large `gap` therefore does not by itself mean a
fair policy could have done better on that position.

The suite must distinguish two cases:

- **fairly recoverable** — a deeper or better *fair* policy, averaged over many
  independent tapes drawn from the same start position and latent board, beats
  the shallow one. The gap points at a real planning deficiency.
- **luck-only** — the optimum is high only because this particular tape and
  these particular latent values happened to line up. No fair policy could have
  systematically found it, and selecting the scenario teaches a student to
  memorize a coincidence.

**This work does not implement that separation, by design** — the coordinator is
building the multi-tape fair evaluation. What this work does is make it
mechanically available:

- `board`, `latent`, and `movesRemaining` are separate fields, so the start
  position is reusable verbatim;
- `retapeScenario(source, discTape, riseLatent, out)` and
  `resampleScenarioRandomness(source, horizon, rng, out)` produce a family of
  scenarios sharing one start position with independent randomness;
- the `id` is a content hash, so every member of the family has a distinct,
  self-verifying identity while the position is byte-identical across them;
- the same start position can be given a *different horizon* with
  `reHorizonScenario` without regenerating anything.

Until that evaluation exists, the numbers in §5 support this statement:
**the mean gap of 21,653 points over nine moves is an upper bound on what
better fair play could recover, and an unknown fraction of it is unrecoverable
luck.** The one piece of evidence pointing at "a real planning gap" is that the
privileged one-ply greedy — which sees everything the solver sees — only matches
`bestShallow` in 21/128, so most of the gap is created by *planning* several
moves ahead rather than by *information* alone. That is suggestive, not
conclusive.

## Limitations

1. **The latent model is not the base engine's model.** Scenario scores are not
   comparable to any ledger figure. §2.
2. **H = 9 is short.** It is one and a bit rise cycles. Everything about board
   clears, deep chains, and gap magnitude is conditioned on nine moves, and the
   zero-clear result on realistic boards is partly a horizon artifact. §6.
3. **`gap` is a selection hazard, not a policy verdict.** §7.
4. **One tape per position.** Every per-scenario number in §5 is a single draw.
   The comparison between fair D1, D2, and D4 is inside noise and must not be
   used to rank them.
5. **Synthetic positions are not real positions.** They are gravity-settled and
   popper-free, but their column profiles, cover geometry, and number
   correlations are not those of played games. The harvested half exists to keep
   the suite anchored; the synthetic half exists to reach regimes real play never
   visits, and the two are labelled `origin` so they can be analysed apart.
6. **Harvested positions come from lowest-column play**, a weak policy. They are
   representative of *a* real game, not of a strong policy's trajectory. Positions
   from fair-D4 play would be a better anchor and were not harvested here because
   generating them is expensive.
7. **The timings are not timing-grade.** Every run in this document shared a
   16-core / 32-thread machine with other jobs at load averages of roughly 35–55.
   `docs/benchmarks.md` requires exclusive or explicitly isolated resources for a
   performance claim. The node counts are exact and machine-independent; the wall
   times are pessimistic and should be re-measured under a resource lease before
   being quoted.
8. **The engine's own divergences are inherited.** The scenario engine
   deliberately reproduces the repository's rise-boundary scoring, including the
   double clear bonus audit-01 flags as M1, which the clairvoyant solver actively
   exploits. §6.
9. **The transposition table has a capacity cap** (6M entries by default) and
   stops inserting when full rather than evicting. This never changes an answer —
   entries are only ever exact values — but it can slow a large search and force
   the principal-variation walk to re-solve a subtree.
10. **A model contribution record under `research/contributions/` is owed and
    was not written**, because this work was scoped to create files only under
    `approaches/lifetime-objective/scenario/` and `docs/exploratory/`. The
    coordinator should add one (level `L2`/`L3` for the scenario engine, solver,
    and suite) before this is promoted.

## Reproduce

```sh
# build (clang++ explicitly; the Makefile's CXX ?= clang++ loses to make's
# builtin CXX=g++, and g++ trips a false -Werror=array-bounds in
# src/core/native/public-behavior.hpp)
./approaches/lifetime-objective/scenario/build.sh

# 1. the gate
./build/scenario/scenario-parity --seeds 4096

# 2. solver correctness
./build/scenario/solve --self-test

# 3. horizon sweep and pruning measurement.  The sweep source only needs
#    positions with a long enough tape, so fair labelling is skipped and the
#    per-scenario solve is capped; --sweep re-cuts each scenario per horizon.
SWEEP=build/scenario/sweep-source.jsonl
./build/scenario/mint --harvested 32 --synthetic 32 --horizon 16 --threads 8 \
    --skip-fair --time-limit 0.05 --seed-offset 0x1200 --output "$SWEEP"
./build/scenario/solve --sweep --input "$SWEEP" \
    --limit 12 --min-h 6 --max-h 13 --threads 8 --time-limit 10
for flags in "" "--no-bound" "--no-tt" "--no-tt --no-bound"; do
  ./build/scenario/solve --input "$SWEEP" --limit 12 --horizon 9 \
      --threads 8 $flags | tail -1
done

# 4. the suite
./build/scenario/mint --harvested 64 --synthetic 64 --horizon 9 --threads 8 \
    --time-limit 60 --seed-offset 0x1800 \
    --output approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl

# 5. the board-clear reachability probe
./build/scenario/mint --harvested 0 --synthetic 256 --horizon 9 --threads 8 \
    --skip-fair --time-limit 60 --min-cells 3 --max-cells 14 \
    --cover-min 0.0 --cover-max 0.5 --seed-offset 0x2000 \
    --output approaches/lifetime-objective/scenario/data/sparse-probe-h9-v1.jsonl

# 6. occupancy distribution of real play (256 uniform mid-game snapshots of
#    lowest-column games); only the occupiedCells/coveredCells fields are used
./build/scenario/mint --harvested 256 --synthetic 0 --horizon 9 --threads 8 \
    --skip-fair --time-limit 0.002 --seed-offset 0x2400 \
    --output build/scenario/harvest-occupancy.jsonl

# 7. audit any optimum by hand
./build/scenario/solve --input approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl \
    --replay de5ad5aedc4a27e3 --threads 8
```

### Seed lease map

`SEEDLEASE-A51D-SCEN` = `0xa51dc000`–`0xa51dffff`, a sub-range of the
exploratory lease `SEEDLEASE-A51D`. Role: **exploratory development
diagnostic**; once read these seeds are development data permanently. Offsets
are partitioned so concurrent tools never collide:

| Offset | User |
| --- | --- |
| `0x0000`–`0x0fff` | `scenario-parity` game seeds |
| `0x1000`–`0x10ff` | `scenario-parity` latent-invariant games |
| `0x1100`–`0x11ff` | `scenario-parity` reveal-marginal draws |
| `0x1200`–`0x2fff` | `mint` |
| `0x3000`–`0x3fff` | `solve --self-test` |

`suite-h9-v1` used offsets `0x1800`+, `sparse-probe-h9-v1` used `0x2000`+.

### Environment

AMD clang 23.0.0git, `-O3 -std=c++20 -pthread -Wall -Wextra`. 16 physical cores
/ 32 logical, 125 GiB RAM, shared with other jobs throughout. Frozen sources
consumed unmodified; their hashes are recorded at build time in
`build/scenario/sources.sha256`.
