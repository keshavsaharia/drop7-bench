# Audit 01 — Simulator and engine fidelity

Independent, read-only audit of the Drop7 engine that every score in this
repository is produced by. Performed at `HEAD = ac7f04e3d7d6243c2e2831b433ae9c4d599d7b28`.

## Scope

Correctness and fidelity of the game engine only:

- `src/core/native/engine.hpp` (372 lines, sha256 `b6dcde5f40dc39c6931b9a88e42bb351acd6fadaddd1e07691c41a82e44f3090`)
- `src/core/typescript/engine.ts` (769 lines, sha256 `9df63d0be3c6a30e1c6339c6dcfb7187ececdc0885c3a2ca9ad8799f269f6475`)
- `src/core/typescript/engine.test.ts` (387 lines, sha256 `10b4801a73adf51bd840e5270c2feafbb04ad420d543b98f64d38b6dd1706753`)
- `approaches/baselines-diagnostics/native-parity/main.ts` (185 lines, sha256 `4d6e2aad08de422ea05ebd269c132f1036d3d4c11d4d69b3629f1896eeb7f765`)
- supporting reads of `src/core/typescript/headless.ts`,
  `src/core/native/public-behavior.hpp`,
  `approaches/ntuple-rl/native-suite/native.cpp`

Not in scope: policy quality, search correctness, statistics of any recorded
run, attribution records. No protected or final seed data was opened. No
repository file other than this report was created or modified.

## Method

1. Read both engines in full, line by line, plus the TypeScript engine test
   suite, the parity harness, and the native `--trace` driver.
2. Reproduced the repository's own checks:
   - `npm test` → **122 tests, 122 pass, 0 fail**, 549 ms.
   - `make test-native CXX=clang++` → gradient check, n-tuple self-test,
     n-tuple search self-test, and `fair-depth4 --self-test` all
     `"passed":true`.
   - `npm run parity` → `PARITY {"seeds":256,"moves":6852,"exact":true}`.
3. Regenerated the 6,852 parity traces into a scratch directory and computed
   coverage statistics (wave-depth histogram, board clears, level advances,
   termination modes, score composition). This used only the documented parity
   seed range `0x2d700000 + 0..255` (`docs/research/history.md:74`), which is a
   determinism range, not a research cohort.
4. Wrote two disposable probes outside the repository: a TypeScript probe that
   drives `playMove` through the two disputed scoring edge cases, and a C++
   probe that exhaustively searches the 2^32 game-seed space.
5. Compared behaviour against the reference implementation the repository
   itself cites as its authority for the 17,000-point award
   (`docs/research/history.md:51-54`): `oddlord/html5-js-drop7`, files
   `js/drop7.es6`, `js/animations.es6`, `js/pieces.es6`, `js/settings.es6`,
   `js/utils.es6`, fetched at audit time. Reference line numbers below are from
   those files.

Where the original 2009 Area/Code application's behaviour could not be checked
against a primary source, this report says so explicitly rather than asserting
it.

## Findings

### Critical

None. No defect was found that makes the engine internally inconsistent, that
breaks native/TypeScript agreement, or that invalidates every recorded score.
The three High findings below are systematic biases of bounded, computed size,
not wholesale invalidation.

### High

#### H1 — The level bonus is forfeited on the terminating rise; the cited reference pays it

`src/core/native/engine.hpp:302-310` and `src/core/typescript/engine.ts:549-556`:
when the fifth drop of a level completes and `raiseCoveredRow` returns
`null`/`false`, the engine sets `gameOver = true` and leaves `scoreDelta`
untouched. `raiseCoveredRow` refuses the rise whenever any cell of row 0 is
occupied (`engine.hpp:270-273`, `engine.ts:497-501`).

The reference does the opposite. `nextLevelPre` (`drop7.es6:155-164`) selects
17,000 for `mode === 'blitz'`; `nextLevelPost` (`drop7.es6:166-189`) adds it to
the score *first*, then shifts the grid up, then runs `checkMatches`. The
overflow game-over test only runs later, from `dropSequenceDone` →
`checkGameover` (`drop7.es6:212-218`, `drop7.es6:127-138`). The reference grid
is `createMatrix(8, 8, null)` (`drop7.es6:339`) — an 8th buffer row `j = 0`
exists precisely so the rise always succeeds and the overflow is *detected*
afterwards, not *prevented*.

Measured in the repository engine, with internal column index `0` (displayed as
column 1) occupied in rows 1-6, `nextDisc = 5`, and `movesRemaining = 1`:

```
scoreDelta 0   levelAdvanced false   gameOver true   movesRemaining 0
```

Under reference ordering the same move scores 17,000 and then ends.

Why it matters: 254 of the 256 parity games (99.2%) terminate exactly this way
(the other 2 end with no legal column). The engine therefore under-reports
every game by one level bonus. That is 17,000 points — 1.7% of the 1,000,000
qualification target, and 22.7% of the 74,994-point mean observed under
uniform-random play. The bias is one-sided and conservative, so it cannot
manufacture a false million-point claim, but every mean, lower bound, and
paired delta in the repository carries it.

#### H2 — The 70,000 board-clear bonus is tested before the rise, so a fifth-drop clear is overpaid

`engine.ts:540` computes `clearedBoard = isBoardEmpty(board)` immediately after
the placement cascade and `engine.ts:547` adds `CLEAR_BONUS`; the rise then
runs at `engine.ts:549-580`. Identical structure at `engine.hpp:296-297`
followed by `engine.hpp:302-324`.

The reference tests board-clear exactly once per move, and only after the level
rise has already inserted the new gray row: `dropSequenceDone`
(`drop7.es6:212-218`) calls `checkBoardClearPre` (`drop7.es6:191-205`), and
`dropSequenceDone` is reached after `nextLevelPost` has written
`grid[i][7] = SolidPiece.getRandomSolidPiece()` for all seven columns
(`drop7.es6:175-184`). A board cleared on the fifth drop is therefore *not*
empty when the reference checks it.

Measured, repository engine, empty board, `nextDisc = 1`, column 3:

| case | `movesRemaining` | repo `scoreDelta` | reference ordering |
| --- | --- | --- | --- |
| mid-level clear | 2 | 70,007 | 70,007 (agrees) |
| fifth-drop clear | 1 | **87,007** | 17,007 |

Why it matters: the divergence is +70,000 and it lands on precisely the event a
million-point policy is built to engineer. One board clear in five falls on a
fifth drop by position alone, and a search-based candidate that can see the
rise coming has a direct incentive to schedule clears there — harvesting a
bonus the cited reference does not award. Unlike H1 this bias is upward and
policy-controllable, which is the dangerous direction for a qualification claim.

The symmetric check after the level cascade (`engine.ts:575-578`,
`engine.hpp:320-323`) *does* match the reference and is correct; it is simply
almost unreachable, since it requires all seven newly risen grays to be revealed
and popped within the same move.

#### H3 — The opening position is not the reference's Blitz opening

`engine.hpp:112-118` and `engine.ts:136-142` seed the board with seven `kSolid`
gray discs filling the bottom row, and `engine.test.ts:236-245` freezes that as
"the Hardcore game starts above a solid row".

The reference's `gridReset` (`drop7.es6:277-308`) sets
`onlyNumbers = mode === 'blitz'` (`drop7.es6:285`) and drops
`randomIntFromInterval(minStartingPieces, maxStartingPieces)` pieces, with
`minStartingPieces = 11`, `maxStartingPieces = 21` (`settings.es6:33-34`),
rejecting any placement that would score. Blitz therefore opens with 11 to 21
**numbered** discs and **no** gray discs at all.

I could not verify which opening the original application used; that remains
open. What is verified is the inconsistency of the repository's own sourcing:
`docs/research/history.md:51-54` names this clone as the authority that settled
the 17,000 constant, and the engine follows it on scoring while contradicting it
on setup. A grep of `docs/` found no statement of the initial board and no
rationale for the deviation.

Why it matters: occupancy at move 0 is 7 cells of pure gray versus 11-21 cells
of pure number. Those are materially different opening problems — one is a
gray-throughput problem from the first drop, the other is a numbered-clear
problem with no covers to open. Every trajectory in the repository begins from
the first of these.

#### H4 — The environment's entire random tape is a 32-bit key that the public state exposes

The upcoming disc is a pure function of `(seed, move)`:
`engine.hpp:62-72` (`headlessDiscBits` / `headlessDisc`) and
`headless.ts:121-126`. Reveal values come from a Mulberry32 re-seeded each move
from the same game seed: `engine.hpp:341-348`, `headless.ts:183-187`. The move
index needed to key that function, `movesPlayed`, is a member of the state
object handed to policies (`engine.hpp:34`, `engine.ts:23`).

Measured, exhaustive search of all 2^32 seeds (single-threaded, `-O3`):

| observed next-discs | surviving seeds | wall clock |
| --- | --- | --- |
| 11 | 4 | 9.8 s |
| 12 | 1 (exact) | 9.8 s |
| 14 | 1 (exact) | 9.8 s |

Twelve public observations uniquely determine the game seed in under ten
seconds. From the recovered seed, every future disc *and* every future reveal
value is directly computable, because both are keyed by the same 32-bit value.

`docs/benchmarks.md:31` states the policy "never receives the game seed or a key
from which future events can be inferred". The construction violates that
literally: the public observation stream *is* such a key.

No exploitation was found. The native public-policy path derives its scenario
seeds from board, `next_disc`, and `moves_remaining` only
(`public-behavior.hpp:725-733`, `745-753`), and `policy_seed` is a fixed
constant `0xd7075eed` (`public-behavior.hpp:28`) that never touches the game
seed. Labelled speculation: a gradient-trained evaluator is very unlikely to
discover a 32-bit brute force; the realistic exposure is a hand-written or
search-based candidate, or an oracle experiment whose student is not fully
re-derived. The finding is a protocol hazard, not an observed breach.

### Medium

#### M1 — Cross-engine parity covers 6,852 transitions of random play and misses the paths that decide the target

The harness itself is stronger than a move-choice comparison. `main.ts:19-33`
and `main.ts:69-83` build one JSON record per move containing `scoreDelta`,
cumulative `score`, `level`, `movesRemaining`, `gameOver`, `clearedBoard`,
`levelAdvanced`, every wave as `{depth, cleared, revealed, points}`, and the
full 49-cell board string; `native.cpp:78-101` emits the identical shape; and
`main.ts:105-115` compares the two line by line, byte for byte. Actions are
driven by a shared uniform-random stream (`main.ts:45,51` vs
`native.cpp:109,115-118`), so both engines play the same games.

Measured coverage of `npm run parity`
(`--seed-start 762314752 --seed-count 256`, i.e. `0x2d700000 + 0..255`):

| quantity | value |
| --- | --- |
| games / transitions | 256 / 6,852 |
| moves per game | mean 26.8, min 20, max 45 |
| waves per move | 0: 3,466 · 1: 2,370 · 2: 605 · 3: 263 · 4: 85 · 5: 43 · 6: 15 · 7: 2 · 8: 2 · 9: 1 |
| maximum chain depth reached | 9 |
| level advances | 1,115 |
| gray reveals | 2,389 |
| **board clears** | **0** |
| **games reaching the 500-move cap** | **0** |
| termination | 254 blocked rise, 2 no legal column |

What parity proves: on 6,852 transitions of uniform-random play the two
implementations are bit-identical in board, score, level, and chain structure.
That is a real and useful determinism/portability result.

What parity does **not** prove: (a) nothing about fidelity to Drop7 — two
engines can agree exactly on the wrong rules, and H1-H3 are exactly that case;
(b) the `CLEAR_BONUS` branches (`engine.hpp:296-297`, `engine.hpp:320-323`,
`engine.ts:547`, `engine.ts:575-578`) have **zero** cross-engine coverage, since
no parity game ever cleared the board — they are covered only by the one-sided
TypeScript unit test at `engine.test.ts:247-257`; (c) the censoring path has
zero coverage; (d) chains beyond depth 9 have zero coverage, and depth ≥ 7
appears in 5 of 6,852 moves (0.07%); (e) nothing about the positions a strong
policy visits, since 26.8-move random games never reach them.

#### M2 — Hidden gray values are sampled at reveal time, not fixed when the row is created

`engine.hpp:260-262` and `engine.ts:366-368` assign `random.nextDisc()` to each
revealed cell, in row-major scan order, at the moment of reveal. The comment at
`engine.hpp:258-259` acknowledges that the ordering is observable through
subsequent chains.

The reference fixes the value when the gray is created:
`SolidPiece.getRandomSolidPiece()` builds a `NumberedPiece` up front
(`pieces.es6:77-81`), the row rise stores those objects
(`drop7.es6:182`), and `crack()` twice returns exactly that stored piece
(`pieces.es6:93-96`, `pieces.es6:105-107`).

Three consequences:

1. The same physical gray disc can hold different numbers depending on when and
   in what order it is opened. There is no persistent latent board.
2. `docs/methodology.md:41-43` permits "privileged planners" to inspect future
   randomness to generate labels. Under this engine there is no true hidden
   value to inspect — only a counterfactual that changes the moment the student
   deviates. Any teacher/oracle experiment built on "knowing what is under the
   grays" is resting on a quantity the engine does not define.
3. Reveal order is row-major, a scan artifact with no physical meaning, and it
   determines which value lands in which cell.

The design is deliberate and documented (`headless.ts:114-119`), and the
marginal distribution is correct — measured uniform over 1..7 with
chi-square 3.59 on 6 degrees of freedom across 400,000 reveal draws. The
finding is about semantics, not about the marginal.

#### M3 — A second, untested copy of the move loop exists in the native policy layer

`public-behavior.hpp:610-663` (`resolveCascadeSampled`) and
`public-behavior.hpp:665-719` (`playMoveSampled`) reproduce
`engine.hpp:213-268` and `engine.hpp:286-339` statement for statement,
differing only in the `Random` template parameter. Read side by side they are
semantically identical today.

Nothing tests that equivalence, and parity cannot catch a regression: the
harness drives `engine.hpp` through `native.cpp:103-124` and never touches
`playMoveSampled`. Every native rollout and sampled-search policy runs on the
copy. A future edit to `engine.hpp` alone would silently desynchronise the
policy layer from the environment while `npm run parity` still reported
`"exact":true`.

#### M4 — The state object has no public/privileged split, and two TypeScript policies read forbidden fields

`engine.hpp:28-36` and `engine.ts:17-25` expose `score`, `level`, and
`movesPlayed` on the same object a policy receives. `docs/methodology.md:36-38`
forbids all three as policy inputs. There is no separate `PublicState` type and
no mechanical barrier.

Two consumers currently cross the line:

- `src/core/typescript/mc-return-policy.ts:102-103` pushes
  `Math.min(state.level, 100) / 100` and `Math.min(state.movesPlayed, 500) / 500`
  directly into the policy's feature vector. That is a literal read of level and
  absolute move number.
- `src/core/typescript/gray-throughput-policy.ts:471-478` folds `state.level` and
  `state.movesPlayed` into `hashObservable`, a chance-estimator salt. This leaks
  no future information, but it does mean the policy is not a function of the
  public state alone: the same public position sampled at two different move
  numbers gets different scenarios.

Both are outside the engine's own boundary but are enabled by the engine's
undifferentiated state type, so they are recorded here for the coordinator
rather than acted on. `canonicalizeState` at
`gray-throughput-policy.ts:456-465` and `mcts-solver.ts:483` do correctly zero
`score` before evaluation, which shows the discipline exists but is applied
per-policy rather than enforced by the interface.

### Low

#### L1 — Move-cap defaults disagree with the documented censor rule

`headless.ts:110` sets `DEFAULT_MAX_MOVES = 500`; `native.cpp:107` and
`native.cpp:132` default `--moves`/`--max-moves` to 500;
`docs/methodology.md:80` describes "a game stopped at the 2,000-move cap".
Runs taken on defaults censor four times earlier than the documented rule.
Nothing observed here is affected (the longest parity game was 45 moves), but a
candidate approaching the target would need roughly 294 moves per game on the
level-bonus arithmetic below, which is within a factor of two of the 500 default.

#### L2 — `BOARD_SIZE` is overloaded as the disc-value cardinality

`engine.ts:146` (`Math.floor(sample * BOARD_SIZE) + 1`), `engine.ts:485`, and
`engine.ts:706` all use the board dimension where the disc alphabet size is
meant. The native engine writes the literal `7` in the same places
(`engine.hpp:70`, `engine.hpp:92`). Numerically identical in Drop7; the two
files simply express the same constant differently, and a hypothetical
board-size change would silently alter the disc alphabet in TypeScript only.

#### L3 — `make` uses g++ by default and one target does not build with it

`Makefile:1` writes `CXX ?= clang++`, which GNU make's built-in `CXX = g++`
defeats — a bare `make` uses g++. Verified on this machine with g++ 14.2.0:
`approaches/ntuple-rl/native-suite/native.cpp` compiles cleanly, but
`approaches/fair-expectimax/reference/fair-only-depth4.cpp` fails with
`-Werror=array-bounds` raised inside an inlined `std::sort` originating at
`src/core/native/public-behavior.hpp:453`.

This is a compiler diagnostic, not a defect: at
`public-behavior.hpp:442-454`, `support` is `std::array<double, 4>` and `count`
is incremented at most once per iteration of a four-element `directions` loop,
so `std::sort(support.begin(), support.begin() + count, ...)` is in bounds by
construction. GCC 14 cannot prove the bound after introsort inlining. No
out-of-bounds access exists. `make ... CXX=clang++` is required; using
`CXX := clang++` in the Makefile would fix the default.

### Informational

- **Scoring constants are corroborated.** `floor(7 * depth^2.5)`
  (`engine.hpp:202-206`, `engine.ts:268-273`) matches the reference's
  `Math.floor(7 * (Math.pow(chain, 2.5)))` (`drop7.es6:114`), and the
  per-wave multiplication `popper_count * scoreForWave(depth)`
  (`engine.hpp:263`, `engine.ts:369`) matches
  `score += points * matchedPieces.length` (`animations.es6:150`). The
  17,000 Blitz / 7,000 Classic-Sequence split (`engine.hpp:19-21`,
  `engine.ts:7-8`) matches `drop7.es6:155-161`, the five-drop cadence
  (`engine.hpp:18`) matches `blitzDrops = 5` (`settings.es6:37`) against
  `classicSequenceDrops = 30` (`settings.es6:36`), and the 70,000 clear bonus
  (`engine.hpp:22`) matches `drop7.es6:208`. An independent third-party
  description of Blitz also reports 17,000 per level with numbered-only drops.
- **Score composition under random play.** Across the 256 parity games,
  1,115 level advances contributed 18,955,000 points and all chain waves
  together contributed 243,544 — the level bonus is **98.7%** of all points
  scored. On that arithmetic a 1,000,000-point mean needs roughly 59 level
  advances, i.e. about 294 surviving moves per game against the 26.8 observed
  for uniform-random play. Measured under uniform-random play only; a strong
  policy shifts the mix toward chain points, but the observation makes later
  scoring claims rest on the 17,000 constant and the H1/H2 rise-boundary edge
  cases rather than treating them as incidental.
- **Working tree state.** `git status` at audit time showed one untracked
  directory, `approaches/lifetime-objective/`, belonging to another
  contributor. It was not read, modified, or included in any hash above.
- **Machine profile.** AMD Ryzen AI MAX+ 395 (32 logical CPUs), 125 GiB total
  RAM as reported by `free -g` (host RAM only; no shared APU memory added),
  Debian, AMD clang 23.0.0git, g++ 14.2.0, Node v26.5.0. The 9.8 s seed-recovery
  figure in H4 is single-threaded on this machine.

## What the engine provably does correctly

Verified by reading both implementations and, where noted, by execution:

- **Both clear axes are checked.** `engine.hpp:180-181` and `engine.ts:257-260`
  pop a numbered disc when its value equals the contiguous run through it in its
  row **or** its column. Matches `isNumberedPieceAMatch` (`drop7.es6:54-89`).
- **Covered discs count as occupied.** `engine.hpp:163` and `engine.ts:239` test
  `!= EMPTY`, so `kSolid`/`kCracked` extend runs. Matches the reference's
  `!== null` test. Regression-tested at `engine.test.ts:82-97`.
- **Simultaneous clears are resolved correctly.** `engine.hpp:218-227` and
  `engine.ts:281-284` identify every popper on the pre-removal board and only
  then blank them, so a wave cannot cascade into itself. Gray damage is likewise
  evaluated against the pre-removal board (`engine.hpp:237`, `engine.ts:290`).
  Tested at `engine.test.ts:99-115`.
- **Chain depth starts at 1 and increments once per wave**, and gravity is
  applied at the end of each wave before the next popper scan
  (`engine.hpp:216`, `engine.hpp:266`; `engine.ts:348`, `engine.ts:390-399`).
  A revealed disc therefore cannot pop in the wave that revealed it, but can pop
  in the next. Tested at `engine.test.ts:188-209`.
- **Chain depth carries across the level rise.** `engine.hpp:313-315` uses
  `waves.back().depth + 1` and `engine.ts:568` uses
  `firstCascade.waves.length + 1`; both equal 1 when the placement cascade was
  empty, and the two expressions are equivalent because wave depths are
  contiguous from 1. This matches the reference exactly: `chain` is reset only
  in `dropSequenceDone` (`drop7.es6:221`), while `nextLevelPost` calls
  `checkMatches` directly (`drop7.es6:188`), which increments the surviving
  `chain`. Frozen at `engine.test.ts:285-306`.
- **Gray damage is 4-neighbour, and two hits in one wave do fully reveal.**
  `engine.hpp:231-255` and `engine.ts:293-312` count adjacent poppers and
  compare against `hits_needed` (2 for solid, 1 for cracked). This is **not** a
  one-crack-per-wave rule, and the reference agrees: `matchPointsAnimStart`
  calls `breakNeighbours(i, j)` once per matched piece
  (`animations.es6:90-101`), and `breakNeighbours` calls `crack()`
  unconditionally on each of the four neighbours (`drop7.es6:32-52`), so a solid
  between two poppers is cracked twice in one wave. Frozen at
  `engine.test.ts:171-186`. The engine is correct here relative to its cited
  reference; whether the original application behaved this way is unverified.
- **Row rise mechanics.** `engine.hpp:270-284` and `engine.ts:497-513` shift
  every row up by one and fill all seven bottom cells with `kSolid`, matching
  `drop7.es6:175-184`. The risen row can trigger a cascade
  (`engine.hpp:318`, `engine.ts:565`), matching `drop7.es6:188`.
- **No off-by-one on board height.** The board is 7 rows indexed 0 (top) to 6
  (bottom) throughout (`engine.hpp:13`, `engine.hpp:99-106`). Legality is
  `board[row 0][column] == EMPTY` (`engine.hpp:137-140`, `engine.ts:192`),
  matching the reference's `grid[col][1] !== null` guard on its 1-based grid
  (`drop7.es6:234`). Gravity keeps every column bottom-packed
  (`engine.hpp:189-200`, `engine.ts:208-221`), and the rise preserves that
  packing, so `placeDisc`'s bottom-up scan (`engine.hpp:144-151`,
  `engine.ts:198-204`) can never drop a disc into a buried hole. The
  gravity-packing invariant is asserted every move for 12 seeded games at
  `engine.test.ts:361-386`.
- **Game over is evaluated in the documented order**: the rise is attempted
  first and only then is "no legal column" tested (`engine.hpp:302-329`,
  `engine.ts:549-582`). The game ends *before* the rise bonus is awarded — that
  ordering is internally consistent and matches `docs/methodology.md:18`; it is
  the reference that differs, which is finding H1.
- **Disc distribution is uniform on 1..7 in both engines, with matching
  arithmetic.** Native computes `(bits * 7) >> 32 + 1` (`engine.hpp:69-71`,
  `engine.hpp:90-93`); TypeScript computes `floor(bits / 2^32 * 7) + 1`
  (`engine.ts:144-147`). These are exactly equal in IEEE-754 double arithmetic
  because `bits * 7 < 2^35` is exactly representable. Measured over 10,000,000
  next-disc draws (200,000 seeds × 50 moves): per-value frequencies 0.14272 to
  0.14303, chi-square 7.48 on 6 df (5% critical value 12.59); lag-1 pair
  chi-square 46.07 on 48 df (5% critical value 65.2). Revealed grays draw from
  the same uniform 1..7 alphabet, measured chi-square 3.59 on 6 df. Both engines
  place next-disc and reveal draws in the same order on the same stream, which
  is why parity holds.
- **RNG domains are separated by seed provenance.** The environment uses
  `kNextDiscDomain = 0x4e455854` and `kRevealDomain = 0x5245564c`
  (`engine.hpp:23-24`) keyed by the game seed. Policy chance estimators use
  `scenarioSeedForState` keyed by board, next disc, `moves_remaining`, and a
  constant `policy_seed = 0xd7075eed` (`public-behavior.hpp:28`,
  `public-behavior.hpp:725-733`) that is never derived from the game seed;
  a grep of all `policy_seed` assignments found only fixed constants. Note that
  `kRevealSampleDomain` (`public-behavior.hpp:574`) reuses the identical tag
  value `0x5245564c` as the environment's `kRevealDomain`; the streams stay
  independent only because their seeds have disjoint provenance, not because the
  tags differ. That is fragile but currently correct.
- **The parity harness compares full trajectories, not just move choices** —
  see M1 for exactly what that does and does not establish.

## Verdict

**Run validity:** `valid` — every command in the Method section executed to
completion on the current checkout, and all quantitative claims above are
reproduced measurements, not estimates.

**Scientific outcome:** `fail` — against the hypothesis "the simulator is a
faithful model of five-drop Hardcore/Blitz Drop7 as defined by the reference
this repository cites". Three fidelity divergences were demonstrated with
executable evidence (H1 terminal level bonus, H2 fifth-drop board-clear bonus,
H3 opening position), plus one protocol violation of `docs/benchmarks.md:31`
(H4). The engine is internally consistent, deterministic, and native/TypeScript
exact; it is not a verified model of the target game.

**Evidence tier:** `proposal/mechanics`. This is a mechanics-level verification
on public sources and the documented parity range. No development, protected, or
final cohort was opened, and nothing here revises the status of any recorded run.

**Bearing on recorded scores:** H1 biases every recorded mean *down* by 17,000
points per game and H2 biases it *up* by 70,000 per fifth-drop board clear. Both
are one-sided and computable, so existing results are correctable rather than
void — but until H1-H3 are resolved, an absolute score from this engine is a
score in this engine's variant of Drop7, and the 1,000,000 qualification
threshold is defined against that variant rather than against the game.
