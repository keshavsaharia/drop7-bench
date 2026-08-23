# Finding 13 — a semantics-preserving fast engine for the fair expectimax search

**Status:** exploratory, `CHECK` tier. **Engineering result, not a policy
result.** No score, strength or ranking claim is made anywhere in this document.
**Namespace:** `approaches/lifetime-objective/fast-engine/`, build output
`build/fast-engine/`.
**Seed lease:** `SEEDLEASE-A52-FAST`, `0xa5270000`–`0xa5277fff`. No seed outside
that block was opened; the sub-blocks are fixed as constants in `corpus.hpp` so
two programs here can never overlap.
**No existing file was modified.** The frozen reference is compiled as a library
from a copy generated into this approach's own build directory with exactly one
line — the entry point — renamed; `build.sh` refuses to build if the diff is
anything other than that one line. The four untouched reference sources hash to
exactly the values [`audit-02`](audit-02-fair-d4.md) pinned:

```
45e7c223…f97cf  approaches/fair-expectimax/reference/fair-only-depth4.cpp
8828379b…ee1a   approaches/fair-expectimax/reference/fair-only-horizon.cpp
8b4267af…8c89e  src/core/native/public-behavior.hpp
b6dcde5f…3090   src/core/native/engine.hpp
```

---

## 0. Headline

| | |
| --- | --- |
| Whole-decision speedup, depth 4 / 5 strata, one thread, interleaved A/B | **3.08x** |
| Whole real games, depth 3 / 5 strata | 3.01x |
| Whole real games, depth 3 / 7 strata | 3.09x |
| Whole real games, depth 4 / 5 strata | 2.88x |
| Whole real games, depth 4 / 7 strata | 2.93x |
| Per decision, depth 5 / 7 strata | 3.23x |
| Selected column identical | yes, 306 gated search moves + 438,020 gated engine moves |
| Logical work identical | yes, at every one of nine (depth, strata) configurations |
| Completed depth identical | yes |
| `fairLeaf` return value | identical **bit pattern**, 225,183 real leaf states |
| **Whole-game outcomes on a real 64-game cohort** | **identical**, 704 field comparisons, 0 mismatches ([`finding-14`](finding-15-depth5-exact-estimator.md) §1) |

**The measured speedup is about 3x, not the 5–10x the brief hoped for, and the
reason is structural rather than a failure of effort.** 96.1 % of the search's
nodes are leaves; after the leaf is made 3.5–3.8x faster it is still 58 % of the
remaining time, and the other 41 % is the move application, which is already
close to the cost of copying a board and hashing two 32-bit words. Amdahl's
bound on this decomposition, with an infinitely fast leaf, is about 5x.

**The brief's ranked expectation was wrong about which lead mattered, and the
profile says so with numbers.** The transposition table — expected to be "very
likely the single largest win" — *is* 5.8x faster per operation, and is worth
**0.6 % of a depth-4 decision** (1.01x measured on its own), because only 3.9 %
of nodes ever touch it. Every one of the four remaining leads was real but
small; the win came from the fifth, the leaf.

**The strongest equivalence evidence is not in this document.**
[`finding-14`](finding-15-depth5-exact-estimator.md) §1 re-runs the depth-4
seven-stratum arm on the fast engine over the shared 64-game evaluation cohort
and reproduces the unoptimised binary's recorded result exactly — mean score,
mean moves, clears per move, reveals per move, occupancy and work per move all
identical to every printed digit, and 704 per-game field comparisons with zero
mismatches. That is the end-to-end proof; everything in section 4 below is the
mechanism behind it.

**Depth 5 with seven strata is affordable now, and most of the reason is not
this work.** See section 6: the 75-hour figure is a worst-case-work projection,
and measured work at that configuration is **10.4x lower** than worst case. One
64-game cohort costs 19.1 CPU-hours with the fast engine, 38 minutes of wall
time on 30 threads.

---

## 1. The machine and the limits of these timing numbers

```
AMD RYZEN AI MAX+ 395 w/ Radeon 8060S, 16 cores / 32 threads, 64 MiB L3,
131,167,724 kB RAM, Linux 6.18.35, AMD clang 23.0.0git.
Flags: -O3 -std=c++20 -pthread -Wall -Wextra -Werror.
clang++ only: `make native` still fails on a default GNU-make + GCC host
(`CXX ?= clang++` is inert, and GCC raises a false-positive
-Werror=array-bounds in public-behavior.hpp:442) — audit-02 L1, unfixed here
because fixing it would mean editing an existing file.
```

**This machine was never exclusive.** Other agents' jobs (`flow-run`,
`corpus-gen`, `posmode`, `reveal-sampling`, `leaf-reweight`, `d4-rank`, `play`)
held the one-minute load average between **14 and 57** throughout, on 32 logical
CPUs. The load average is printed next to every measurement.

Consequences:

- **Absolute nanosecond figures are inflated by roughly 3x at load 50 relative
  to load 15.** The same unoptimised depth-4 decision measured 3,076 ms at load
  51 and 1,082 ms at load 22 — same binary, same 24 positions.
- **Ratios between two arms measured back-to-back in one process are the
  trustworthy quantity**, and every speedup here is such a ratio.
- **A drifting load can fake a speedup, and did.** An earlier ablation run
  measured variants sequentially — all repeats of arm A, then all of arm B —
  while the background load fell from 51 to 28 over the run. It reported
  **5.51x** for the full stack. Re-run with the variants **interleaved inside
  each repeat**, at a steady load of ~22, the same code measures **3.08x**. The
  5.51x was an artefact of measurement order. This is recorded because it is
  exactly the kind of number that would otherwise have been published.
- Even interleaved, the worst/best spread within a variant is 1.35x–1.74x.
  Nothing below that resolution is claimed.
- Under `docs/benchmarks.md` these are **not** a clean performance baseline and
  must not enter `research/benchmarks/profiles-v1.json`.

`perf` is not installed on this host, so section 2 instruments instead of
sampling.

---

## 2. Profile of the ORIGINAL hot path

### 2.1 Census — what a depth-4, five-strata decision actually does

A counting instantiation of the same search driver (`CensusSearch`; the counters
are behind `if constexpr` and compile away in every other instantiation) over
**24 real decisions** from real games on lease seeds `0xa5274000+`:

| Per decision | Count | Share of nodes |
| --- | ---: | ---: |
| logical work | 1,560,979 | |
| `bestFutureValue` nodes | 796,058 | 100 % |
| **`fairLeaf` calls** | **764,899** | **96.1 %** |
| **`playMoveSampled` calls** | **796,081** | |
| `canonicalState` calls | 796,058 | |
| `dynamicStateKey` builds (interior nodes) | 31,159 | **3.9 %** |
| cache inserts | 22,743 | |
| cache hits | 8,416 | |
| `findPoppers` scans per applied move | 1.899 | |

The shape of the tree is the whole story. Any optimisation of the transposition
table is capped at a few percent before it starts.

### 2.2 Microbenchmarks on the real input distribution

Each primitive is timed on exactly the inputs the census says it sees: leaf
states harvested by expanding real roots the way the search expands them,
interior states likewise, cascade boards likewise. ns per call, **load 14.6**:

| Primitive | original | fast | ratio |
| --- | ---: | ---: | ---: |
| `fairLeaf` | 970.3 | 278.8 | **3.48** |
| `playMoveSampled` (one applied move) | 238.2 | 188.6 | 1.26 |
| `findPoppers` | 52.2 | 34.7 | 1.50 |
| `isBoardEmpty` | 4.6 | 1.0 | 4.65 |
| board scan + popper extraction *vs* scan alone | 33.8 | 20.2 | — † |
| `applyGravity` | 14.3 | 28.8 | 0.50 ‡ |
| `canonicalState` | 10.6 | 1.8 | 5.78 |
| `scoreForWave` (`pow(d,2.5)`) | below timer | below timer | — |
| transposition probe + insert | 187.9 | 32.5 | **5.79** |

† This row is not an A/B of two implementations; it decomposes the fast path,
showing that the one-pass board scan is 20.2 ns and extracting the popper list
from it costs a further 13.6 ns. Compare the 52.2 ns of `drop7::findPoppers`.
‡ **Not a fair comparison, and reported as such.** The original returns a
`Board` by value and the harness discards it, so the compiler elides the stores;
the fast arm must copy a board into a mutable buffer first because it works in
place. In the real cascade the fast version has no copy. Read 0.50 as "this
microbenchmark cannot measure this", not as a regression.

### 2.3 Attribution, with the residual shown, not hidden

Census counts × microbenchmark cost, for one depth-4 five-strata decision at
load ~15–22:

| Component | calls | ns/call | ms | share of attributed |
| --- | ---: | ---: | ---: | ---: |
| `fairLeaf` | 764,899 | 970.3 | 742.2 | **79.1 %** |
| `playMoveSampled` | 796,081 | 238.2 | 189.6 | 20.2 % |
| transposition probe + insert | 31,159 | 187.9 | 5.9 | **0.6 %** |
| attributed total | | | 937.7 | |
| **measured whole decision** | | | **1,081.8** | |
| unattributed residual | | | 144.1 (13.3 %) | |

The residual is the driver itself — recursion, budget checks, `State` copies,
the iterative-deepening loop — plus the gap between a microbenchmark's streaming
access pattern and the search's. It is reported rather than distributed.

The same arithmetic on the fast side: leaf 213.3 ms + move 150.1 ms + table
1.0 ms = 364.4 ms attributed, against 351.8 ms measured. The leaf is now 58 % of
the remaining time and the move application 41 %.

### 2.4 The allocation census nobody had counted

Three of the four confirmed leads are really the same defect — the hot path
calls `malloc`. Per depth-4 five-strata decision, the original performs:

| Site | allocations per decision |
| --- | ---: |
| `dynamicStateKey` (52-byte string, exceeds the 15-byte SSO buffer) | 31,159 |
| `cacheValue` — the `list<string>` node and its copy of the key | 45,486 |
| `MoveResult::waves` vector, on applied moves that produce a wave | ≈ 358,000 |
| **`releaseReadiness`'s two `std::vector<double>` per numbered disc per leaf** | **≈ 15–30 million** |

The last row is the one that matters and it is not in the brief's list. It is
in `public-behavior.hpp`'s release-inventory loop, two vectors built by
`push_back` for every numbered disc on the board, on every one of 764,899 leaf
evaluations. The fast engine performs **zero** allocations inside a decision.

---

## 3. The optimisations, individually and cumulatively

Each is switched independently in one shared search driver
(`variant-search.hpp`, `ConfigurableSearch<kFastTable, kFastEngine, kFastLeaf>`),
so an A/B difference is attributable to the switched storage and nothing else.

### 3.1 What each switch contains

**O1 — transposition table** (`fast-search.hpp`).
The reference keys an `std::unordered_map<std::string, CacheEntry>` with a
52-byte string rebuilt at every interior node — one `malloc`/`free` per interior
node — and keeps LRU order in a parallel `std::list<std::string>`, a second
allocation plus a second 52-byte copy per insert. Replaced by a 32-byte packed
key (49 cells × 4 bits, plus next disc, moves remaining and depth), an
open-addressed table with linear probing and backward-shift deletion, and an
intrusive doubly linked LRU over slot indices. A decision starts with an empty
cache — the reference gets that by constructing a fresh `SearchContext`; here it
is an epoch bump, O(1), so there is no multi-megabyte memset per decision.
*Equivalence:* the packing is injective on the reachable domain (cells 0–9, next
disc 1–7, moves remaining 1–5, depth 1–8; `gate-leaf` checks the cell bound on
every state it sees), so two states collide here exactly when their strings
collide there, and eviction is still strict LRU at the same declared capacity.
Hit, miss, insert and eviction sequences are therefore identical — which is why
work counts are identical, including at depth 5 where the cache does evict.

**O2 — `scoreForWave` and `readiness` tables** (`fast-engine.hpp`).
`drop7::scoreForWave` calls `std::pow(double, 2.5)` once per cascade wave;
`cfpi::detail::readiness` calls `std::ldexp` several times per numbered disc per
leaf. Both become table lookups at namespace scope, so the hot path is an
indexed load with no thread-safe-static guard. The wave table has 1,024 entries
with the original expression retained as the fallback beyond it — far above the
depth-20 cascade observed in real play, and above anything reachable at all,
since every wave clears at least one of 49 discs. Every entry is verified
against `floor(7.0 * pow(d, 2.5))` and every readiness value bit-compared
against `ldexp`.

**O3 — popper detection** (`fast-engine.hpp`).
`drop7::findPoppers` calls `lineLength` twice per numbered cell, and
`lineLength` rescans up to seven cells in each of two directions — on the order
of 1,300 board reads per wave, recomputed from scratch every wave. Replaced by
one board pass producing seven row occupancy masks, seven column occupancy masks
and a bitboard of numbered cells, after which each cell's run length is a
128-entry table lookup. The popper list comes out in the identical row-major
order.

**O4 — gravity and cover resolution.**
`applyGravity` returned a 49-byte `Board` by value on every wave; it is now in
place, and only on the columns that actually lost a disc — reveals overwrite a
cover without creating a hole, so a column with no popper is provably unchanged
by compaction. The cover-resolution scan iterates a cover bitboard instead of
all 49 cells, and that bitboard is built lazily, because most applied moves
resolve with no wave at all (1.899 popper scans per move means ~0.9 waves) and
never need it.

**O5 — wave sink.**
`MoveResult::waves` is a `std::vector<Wave>` allocated per applied move for data
the search never reads; `playMove` itself consults only `empty()` and
`back().depth`. The search now passes `MinimalWaveSink`, which records exactly
those two facts. Trajectory consumers get the complete list through
`FullWaveSink`, which stores it inline and throws rather than truncating.

**O7 — the leaf** (`fast-leaf.hpp`), seven changes:

- **L1, dead computation.** `fairLeaf` reads 13 of `PhaseFeatures`' 24 fields;
  audit-02 M5 measured the discarded set at ≥13.2 % of leaf time. Removed
  wholesale, because each discarded feature has its own accumulator and nothing
  retained depends on it: `placementInventory` (a full seven-column placement
  simulation), `raiseCoveredRow` + `findPoppers` (an entire extra popper scan
  per leaf), the low-cap column loop, `cover_altitude_debt`, and the rise-urgency
  block — which between them contain **three `std::pow` calls per leaf** whose
  results are multiplied by nothing.
- **L2, libm.** `readiness`'s `ldexp` becomes the O2 table.
- **L3, allocation.** The two `std::vector<double>` support buffers per numbered
  disc become stack arrays (see 2.4). `releaseReadiness` reads one order
  statistic of the sorted values, a property of the multiset and not of the
  sort, so a descending insertion sort is value-identical to `std::sort`.
- **L4, layout.** `std::array<DiscAnalysis, 49>` was ~4.3 kB zero-initialised
  per leaf; `LineAnalysis`'s six 49-entry `int` arrays another 1.2 kB with four
  `fill(-1)` sweeps. Both gone; the leaf's scratch is one reused ~2.4 kB member.
- **L5, run-length table.** A board row is seven cells, so its occupancy is one
  of 128 patterns and every position's run length, start and end follow from
  that pattern alone. `analyzeLines`' two scanning sweeps and its six arrays
  collapse into two 7-entry mask arrays plus a 2,688-byte lookup table.
- **L6, pass fusion.** Six of the leaf's passes iterated all 49 cells. The
  fair-only horizon's own column loop has the same bounds and order as the
  occupancy/heights loop, and its board sweep the same bounds and order as the
  main per-cell sweep, and their accumulators are disjoint from everything else,
  so both are folded in — two whole board passes disappear while the order
  *within each sum* is preserved exactly. The remaining passes (`adjacent_ones`,
  runs of twos, cover exposure) iterate bitboards of the relevant cells —
  typically 5, 3 and 13 cells — instead of 49 each.
- **L7, the zero-weighted term.** `kRoughnessWeight` is `0.0` and the leaf
  computed column roughness on every evaluation and multiplied it by zero. Both
  the feature and the term are removed. The argument: `roughness` is a sum of
  six absolute integer height differences, so it is finite on every board and
  `0.0 * roughness` is exactly `+0.0`; `result` starts at `+0.0` and can never
  become `-0.0` (round-to-nearest returns `+0.0` for any exact cancellation, and
  `+0.0 + -0.0` is `+0.0`), so `result += +0.0` is the identity. Verified
  empirically by re-running the bit-exact gate on all 225,183 corpus leaves after
  the removal: 0 mismatches. *(This lead was contributed by the coordinator
  mid-task; it had been deliberately kept in until the finiteness argument was
  checked. Its measured cost after L6 was six integer subtractions inside a loop
  that runs anyway — below this machine's resolution.)*

### 3.2 In-leaf profile

Cumulative cost of each leaf stage, measured with an `if constexpr` early return
that compiles away in the shipped leaf, on 8,000 real leaf states, load 21.4:

| Stage | cumulative ns | marginal ns |
| --- | ---: | ---: |
| frozen `fairLeaf`, for scale | **824.1** | |
| 1 occupancy masks, heights, rise pressure, danger height | 34.2 | 34.2 |
| 2 + per-cell sweep: direct potential, height load, covered/low-number height risk | 134.4 | 100.2 |
| 3 + release inventory | 171.6 | 37.2 |
| 4 + adjacent ones | 171.6 | 0.1 |
| 5 + runs of twos | 177.6 | 5.9 |
| 6 + cover exposure — **full leaf** | **218.9** | 41.4 |
| 7 (identical code to 6 after the L6 fold) | 217.8 | −1.1 |

Row 7 is the same code as row 6; its −1.1 ns marginal is a direct read of this
machine's noise floor, ±0.5 % at this stage size and much worse at high load. An
earlier run of the same table at load 54 produced marginals of −196 ns and
−40 ns, i.e. it was not resolvable at all; that is why the stage table is
reported only from the low-load run.

### 3.3 Ablation: whole depth-4 / 5-strata decision

24 real decisions, 3 repeats, **variants interleaved within each repeat**, load
~22:

| Variant | best ms | worst ms | spread | speedup |
| --- | ---: | ---: | ---: | ---: |
| baseline (all original) | 1,081.83 | 1,877.53 | 1.74x | 1.00 |
| O1 fast table only | 1,069.47 | 1,448.69 | 1.35x | **1.01** |
| O3–O5 fast engine only | 1,003.21 | 1,730.87 | 1.73x | **1.08** |
| O2/O7 fast leaf only | 414.64 | 699.12 | 1.69x | **2.61** |
| O1 + engine | 1,012.50 | 1,596.04 | 1.58x | 1.07 |
| **O1 + engine + leaf (all)** | **351.81** | 526.75 | 1.50x | **3.08** |

This table shows:

- The leaf carries essentially the whole result: 2.61 of the 3.08.
- The table on its own is 1.01x — indistinguishable from nothing, and inside the
  spread. Its 5.79x per-operation improvement lands on 0.6 % of the runtime, and
  its 4.4 MB of arrays displace hot leaf data from cache, which is why it does
  not even show its arithmetic share. It is kept because at depth 5 the cache
  actually evicts and the reference's `list<string>` churn grows with it, and
  because it removes the last per-node allocation.
- The engine on its own is 1.08x, which is what a 1.26x improvement on 20 % of
  the runtime predicts.
- 2.61 × 1.08 × 1.01 = 2.85, against 3.08 measured; the composition is slightly
  super-additive because removing the leaf's allocations also removes allocator
  contention with the engine's.

---

## 4. The equivalence gates

All gates exit non-zero on any mismatch.

### 4A. Anchor — the comparator really is the comparator

`SlowSearch` (`slow-search.hpp`) is a literal transcription of
`fair-only-depth4.cpp`'s driver with depth, strata, terminal utility, work bound
and cache bound exposed as parameters, built exclusively from unmodified frozen
primitives. This is what licenses using it as the semantic anchor at
configurations the frozen binary cannot itself run.

> `SlowSearch(default)` vs frozen `chooseDepth4Action`: **60 moves compared, 0
> mismatches** on action, logical work, completed depth, node count, cache-hit
> count and cache size.

### 4B. Search parity — action AND work AND completed depth

`FastSearch` vs `SlowSearch`, every move of every probe game:

| depth | strata | moves | action mismatches | work mismatches | depth mismatches | work/move (both arms) |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 5 | 50 | 0 | 0 | 0 | 70 |
| 2 | 5 | 50 | 0 | 0 | 0 | 2,221 |
| 2 | 7 | 50 | 0 | 0 | 0 | 4,615 |
| 3 | 5 | 50 | 0 | 0 | 0 | 60,800 |
| 3 | 7 | 50 | 0 | 0 | 0 | 174,013 |
| 4 | 5 | 10 | 0 | 0 | 0 | 1,383,207 |
| 4 | 7 | 10 | 0 | 0 | 0 | 4,377,375 |
| 5 | 5 | 3 | 0 | 0 | 0 | 19,025,731 |
| 5 | 7 | 3 | 0 | 0 | 0 | 52,398,525 |
| **total** | | **306** | **0** | **0** | **0** | |

Work-count identity is not optional. Work is the fixed-cost currency every
comparison in this repository is denominated in; a version that reached the same
column having done less logical work would be a different algorithm, not a
faster one. Note the depth-5 rows in particular: there the declared cache
capacity (200,000) is far below the worst case (6,007,498 entries at seven
strata), so eviction happens on essentially every store — and the two arms still
agree exactly, which is the strongest available evidence that the open-addressed
LRU reproduces the `list<string>` LRU's eviction order.

### 4C. Determinism and reflection

> 43 moves; **0** repeat mismatches (identical action, work, nodes and cache
> hits on a second call); **0** mirror-equivalence mismatches (fast and
> frozen-primitive searches give the same action, work and completed depth on
> the mirrored position); **0** reflection mismatches on 38 asymmetric boards
> (`action == 6 - action` **and** identical work); 5 symmetric boards excluded.

One caveat applies to the reflection property. On a **horizontally symmetric**
board the mirror *is* the board, so canonicalisation returns the same canonical
state and the deterministic `kColumnOrder` tie-break returns the same column
rather than its reflection; `action == 6 - action` then holds only for column 3.
**The frozen reference behaves identically** — its self-test uses an asymmetric
fixture — so this is a property of the policy, not a defect introduced here. The
gate excludes symmetric boards from the `6 - action` check, counts them, and
separately requires fast and slow to agree on the mirrored position, which is
the actual equivalence question.

### 4D. Trajectory — engines identical move for move

Both engines are driven through the same game with the same column, chosen once
from the reference state and handed to both, so the gate isolates engine
semantics from policy semantics. Compared every move: board, next disc, score,
score delta, level, moves remaining, moves played, terminal flag, board-clear
flag, level-advance flag, and the complete wave list (depth, cleared, revealed,
points) entry by entry.

| Policy | Seeds | Games | Moves | Waves | Mismatches |
| --- | --- | ---: | ---: | ---: | ---: |
| `centerFirstMove`, full games | `0xa5270000`–`0xa5270fff` | 4,096 | 87,212 | 60,632 | **0** |
| depth-3 five-strata search, full games | `0xa5271000`–`0xa5271fff` | 4,096 | 346,594 | 482,016 | **0** |
| frozen fair depth-4, 45-move cap | `0xa5276000`–`0xa527605f` | 96 | 4,214 | 5,615 | **0** |
| **total** | | **8,288** | **438,020** | **548,263** | **0** |

The depth-4 arm is 96 seeds rather than 4,096 because a 4,096-seed frozen
depth-4 arm is several CPU-days on this contended host; the depth-3 arm supplies
the 4,096-seed breadth with search-quality (long, tall-board) states. The column
is handed to both engines either way, so every arm is a valid engine test; what
the smaller depth-4 arm buys less of is *state distribution*, not validity.

### 4E. Bit-exact leaf

`frozen::fairLeaf` and `fast::fastFairLeaf` compared as raw `uint64` bit
patterns — not as approximately-equal doubles — over states harvested by
expanding real roots at every iterative-deepening ply exactly the way the search
expands them:

> wave-score table: depths 1..1,087 checked against `floor(7*pow(d,2.5))`, **0
> mismatches** (spot values 7, 39, 109, 224, 391, 617; d=20 → 12,521)
> readiness table: costs −64..143 bit-compared against `ldexp(1.0, 1-cost)`, **0
> mismatches**
> leaf corpus: 125 real roots expanded, **225,183 leaf states compared**
> leaf bit-pattern mismatches: **0**
> cell-domain violations (cell > 15): **0**
> terminal path identical: **yes**

This gate was re-run after every change to the leaf — after the bitboard
rewrite, after the L6 pass fusion, and after the L7 roughness removal — and
reported zero mismatches each time. That is a claim about the leaf, not about
the process: the one real defect found during this work (section 8.5) was caught
by reading the code, not by the gate, and the only gate that ever failed was the
reflection check in 4C, which turned out to be the gate being wrong about
symmetric boards rather than the engine.

---

## 5. End-to-end on real games

Paired whole games, seeds `0xa5275000+`, both arms in one process, three
repeats, best of. The benchmark asserts on every configuration that the two arms
end with the **same score, same move count and same total logical work**, and
aborts if they do not — a silent divergence cannot be reported as a speedup.

| Config | arm | best s | ms/move | moves | work/move | speedup | identical | load |
| --- | --- | ---: | ---: | ---: | ---: | ---: | :---: | ---: |
| d3, 5 strata | baseline | 28.310 | 65.08 | 435 | 54,826 | 1.00 | yes | 31.9 |
| | **fast** | **9.414** | **21.64** | 435 | 54,826 | **3.01** | yes | 31.9 |
| d3, 7 strata | baseline | 65.669 | 190.35 | 345 | 153,759 | 1.00 | yes | 31.7 |
| | **fast** | **21.221** | **61.51** | 345 | 153,759 | **3.09** | yes | 31.7 |
| d4, 5 strata | baseline | 67.055 | 1,117.59 | 60 | 1,487,630 | 1.00 | yes | 28.8 |
| | **fast** | **23.243** | **387.39** | 60 | 1,487,630 | **2.88** | yes | 28.8 |
| d4, 7 strata | baseline | 47.847 | 3,987.26 | 12 | 6,142,430 | 1.00 | yes | 29.6 |
| | **fast** | **16.309** | **1,359.09** | 12 | 6,142,430 | **2.93** | yes | 29.6 |

Cache capacity 60,000 entries at five strata (the frozen value, worst case
45,430, never binds) and 200,000 at depth-4 seven strata (worst case 122,598,
never binds). Game counts differ per configuration because a depth-4
seven-stratum game is ~4 s per move on this host; the move counts are the
reported unit.

Measured work per move agrees closely with
[`finding-05`](finding-05-chance-strata.md)'s independent figures — 54,826 vs
54,429 at d3s5 and 153,759 vs 156,834 at d3s7 — which is a useful third-party
check that this driver really is the same algorithm.

Per-decision probes on three fixed real roots, three repeats, both arms:

| Config | work/decision | baseline ms | fast ms | baseline ns/work | fast ns/work | speedup | load |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| d4, 5 strata | 1,315,860 | 697.9 | 225.1 | 530.4 | 171.1 | **3.10** | 28.9 |
| d4, 7 strata | 3,295,315 | 2,903.9 | 910.9 | 881.2 | 276.4 | **3.19** | 26.9 |
| d5, 5 strata | 21,358,808 | 11,864.0 | 3,765.1 | 555.5 | 176.3 | **3.15** | 24.3 |
| d5, 7 strata | 55,765,609 | 30,340.2 | 9,379.2 | 544.1 | 168.2 | **3.23** | 22.0 |

The speedup is flat to slightly increasing with depth and strata, which is what
the decomposition predicts: the leaf fraction does not fall as the tree grows,
because the last ply is always ~96 % of the nodes.

---

## 6. Does depth 5 with seven strata become affordable?

**Yes — and the arithmetic says most of the reason is a cost-model correction,
not this engine.**

### 6.1 Reproducing the brief's 75 hours

The brief states worst-case work of **582,727,796 per move** at depth 5 with
seven strata, projecting to "roughly 75 hours for one 64-game cohort". That
figure is reproducible exactly:

```
worst-case iterative work, branches b = 7 columns x 7 strata = 49:
  sum_{d=1..5} [ sum_{l=1..d} 49^l + 49^d ]
  = 98 + 4,851 + 237,748 + 11,649,701 + 570,835,398
  = 582,727,796                                        (matches the brief)

cohort = 64 games x 114.66 mean moves               (finding-05's d4s7 mean)
       = 7,338.24 decisions
total  = 582,727,796 x 7,338.24 = 4.2761e12 work units

cost per work unit, ORIGINAL engine, at the heavy contention that prevailed
earlier in this session:  3,076.199 ms / 1,560,979 work = 1,970.7 ns

4.2761e12 x 1,970.7e-9 s = 8.428e6 s = 2,341 CPU-hours
                                     / 30 threads = 78.0 wall-hours
```

So "roughly 75 hours" is **worst-case work × the original engine's contended
rate ÷ 30 threads**. Both inputs are pessimistic.

### 6.2 Measured work is 10.4x below worst case

Depth-5 seven-stratum work was measured directly, twice, on disjoint real roots:
**52,398,525** per move (search-parity gate, 3 moves) and **55,765,609** per
decision (probe, 3 roots). Take the larger, 55,765,609.

```
582,727,796 / 55,765,609 = 10.45x
```

The transposition table is why: the deeper the tree, the more of it is
transpositions. The same effect is visible at depth 4 (worst case 11,892,398,
measured 3.3–6.1 million). **This correction is not a contribution of this work
— it applies equally to the unoptimised engine — but it is the larger of the two
factors and the projection is wrong without it.**

### 6.3 The measured cost of one 64-game depth-5 seven-stratum cohort

```
decisions = 64 games x 114.66 mean moves = 7,338.24

unoptimised engine:  7,338.24 x 30.3402 s = 222,640 s = 61.8 CPU-hours
                                                      = 2.06 h on 30 threads
fast engine:         7,338.24 x  9.3792 s =  68,825 s = 19.1 CPU-hours
                                                      = 0.64 h on 30 threads
                                                      = 38 minutes
```

| Model | work/move | CPU-hours | wall hours, 30 threads |
| --- | ---: | ---: | ---: |
| brief's projection (worst-case work, contended original engine) | 582,727,796 | 2,341 | 78.0 |
| unoptimised engine, measured work and rate | 55,765,609 | 61.8 | **2.06** |
| **fast engine, measured work and rate** | 55,765,609 | **19.1** | **0.64** |

**The experiment the evidence most wants next is reachable either way.** The
engineering contribution is the 3.23x — it turns a two-hour cohort into a
38-minute one, which is the difference between "an afternoon per configuration"
and "sweep the configuration space".

### 6.4 What this projection assumes, stated so it can be attacked

- **114.66 mean moves** is finding-05's measured depth-4 seven-stratum figure,
  used as the closest available anchor. A depth-5 policy that is genuinely
  stronger will survive *longer* and cost proportionally more: at 150 mean moves
  the fast cohort is 25.0 CPU-hours / 50 minutes; at 200 it is 33.3 CPU-hours /
  67 minutes. The projection is therefore optimistic in exactly the case where
  the experiment succeeds.
- **Three real roots.** Work per decision varies by ±40 % with board state; the
  two independent depth-5 seven-stratum measurements differed by 6 %.
- **Cache capacity 200,000 entries**, against a worst case of 6,007,498 at this
  configuration. The cache therefore evicts, and measured work is a function of
  that declared cap. A different cap is a different configuration with different
  work — this must be frozen and declared in the experiment's manifest, exactly
  as `docs/benchmarks.md` requires. Both arms used the same cap and agreed
  exactly.
- **Contended machine**, load ~22. On an idle machine both arms speed up
  together; the 3.23x ratio is the contention-robust part.
- 30 threads is assumed by analogy with finding-05's runs, not measured here. No
  parallel scaling preflight was performed.

---

## 7. Memory

| | reference | fast |
| --- | --- | --- |
| Transposition storage, 60,000-entry capacity | ~15.4 MB at capacity, ~5.8 MB at the 22,743 entries actually used (estimated: ~256 B/entry across a map node, a heap string, a list node and a second heap string) | **4,648,576 B**, fixed, preallocated (77.5 B/slot: 56 B entry + 4 B index + 4 B epoch stamp, at 2x slot over-provisioning) |
| Transposition storage, 200,000-entry capacity | ~51 MB at capacity | **16,194,304 B**, fixed |
| Cache occupancy at d4s5 | 22,743 of 60,000 = 37.9 % | identical, by construction |
| Allocations inside one decision | ~15–30 million (section 2.4) | **0** (every buffer is preallocated at construction; the only allocation the fast search can make is the exception object if the work bound is hit) |
| Leaf scratch | ~5.5 kB zero-initialised **per leaf call** (≈4.2 GB of writes per depth-4 decision) | one reused ~2.4 kB member per search object |
| Process peak resident, both arms in one process, d4s5 | 18,096,128 B | |
| Process peak resident, d4s7 (200,000-entry cache) | 50,892,800 B | |
| Process peak resident, d5s7 probe (200,000-entry cache) | 77,742,080 B | |
| Process peak resident, profile (six search objects + corpora) | 62,476,288 B | |

The reference figures marked "estimated" are derived from the libstdc++ node
layouts, not measured; the fast figures are reported by the table itself. At 30
threads the fast engine's 16.2 MB table at depth 5 is 486 MB of resident
transposition storage, comfortable on a 131 GB host but *not* comfortable in a
64 MB L3, which is one reason the table's measured benefit is small.

---

## 8. Rejected, and why — the part that matters most

Every idea below was considered and **not shipped**, because each would have
turned an engineering result into an untested policy change. This repository has
already been bitten once by an unstated configuration assumption — a work bound
sized for five strata silently degraded a seven-stratum depth-4 search to depth
3 and produced a wrong scientific conclusion
([`finding-05`](finding-05-chance-strata.md)). The list is long on purpose.

### 8.1 Rejected because they change floating-point results

| Idea | Why rejected |
| --- | --- |
| Vectorise or re-associate `fairLeaf`'s 19-term dot product | Changes the summation order, changes the returned double, changes action ranking. The order is pinned to the TypeScript reference for parity. |
| Replace `x / moves_remaining` in `rise_pressure` with `x * (1.0/moves_remaining)` | Reciprocal-multiply is not division. Seven different doubles per leaf. |
| Multiple partial accumulators in the leaf's per-cell sums, to break the dependency chain | Classic, effective, and changes every sum. |
| `float` for the `addition` / `release` / support arrays | Halves the leaf's memory traffic and changes every value. |
| `-march=native`, `-ffast-math`, `-ffp-contract=fast` | Would change FP codegen for the **frozen reference too**, since it is compiled into the same translation unit. That is worse than useless: it would make the comparator disagree with the repository's own `build/fair-depth4` while this document's gate still passed. |
| Constant-fold `elevation*elevation*edge_multiplier` into a 49-entry table | Not attempted. `acc += e*e*edge` is one statement, so clang may contract it to an FMA, and a table lookup cannot reproduce an FMA's single rounding. The gate would catch it, but after L6 the upside is small and the failure mode is silent-if-untested. |

### 8.2 Rejected because they change work, depth, or the action

| Idea | Why rejected |
| --- | --- |
| Alpha-beta, move ordering, or root width limits | Full width at every node is part of what fair D4 *is* (audit-02 I1). Any pruning changes work and can change the column. |
| Share chance subtrees between sibling columns | Changes work and the sampled scenarios. New candidate. |
| Skip the depth-1 and depth-2 iterative-deepening passes when the work bound provably cannot bind | Changes work, and removes the completed-depth fallback that makes a work-limited decision consistent rather than partial. |
| Raise the transposition capacity so depth 5 never evicts | Changes cache semantics **and therefore work**. It is a legitimate configuration choice that must be declared per configuration, not smuggled in as an optimisation. Both arms in sections 5 and 6 use the same declared cap. |

### 8.3 Rejected because they rest on an unchecked invariant

| Idea | Why rejected |
| --- | --- |
| **First-wave restricted popper scan.** A board handed to `playMove` provably has no poppers (the previous cascade ran to completion), so after placing a disc only that disc's row and column can pop — 13 candidate cells instead of 49. | Exactly equivalent *given the invariant*, and the invariant is nowhere enforced. It would be correct today and silently wrong the first time any caller hands the engine an unresolved board. If wanted, it must ship with a debug-build cross-check against the full scan, exercised by the gates. |
| `isBoardEmpty` as "the bottom row is empty", valid because gravity keeps every board settled | Same objection, for ~4 ns. |
| Deriving vertical run length from column height, valid for the same reason | Same objection. |

### 8.4 Measured, provably value-preserving, and deliberately not taken

**Leaf memoisation.** The census counts how often the same leaf state is
evaluated more than once inside one decision: **264,655 of 764,899 leaf calls,
34.6 %**. `fairLeaf` is a pure function of `(board, next disc, moves remaining)`,
so — unlike the search's own transposition table — a leaf memo cannot change its
value *at any capacity*; hit rate would affect speed only. Work counts would
also be unchanged, because `work` is incremented in `evaluateLeaf` before the
value is produced. By the letter of `docs/benchmarks.md` it is a pure
optimisation: it changes no random event, no completed depth, no logical work,
no floating-point ranking and no selected column.

It is **not shipped** anyway, for two reasons.

1. It is a *cache-semantics* change, and `docs/benchmarks.md` requires a
   comparison to freeze and declare "per-decision depth, work, action width,
   chance samples, random-domain derivation, **cache semantics**, and model
   bytes". It needs its own declaration, its own memory accounting and its own
   gate — not a footnote in an engineering finding.
2. Its payoff is unmeasured and not obviously positive. A table large enough for
   ~500,000 distinct leaf states per decision is ~20 MB per thread; probing it
   765,000 times per decision is 765,000 random accesses that would evict the
   leaf's own working set from a 64 MB L3 shared with 30 sibling threads. This
   document already shows the transposition table's 4.4 MB doing exactly that
   (section 3.3). **34.6 % is the headroom, not the gain.**

Recorded here so a follow-up can decide with the number in hand.

### 8.5 A bug caught by writing the gate first

The first `isBoardEmptyFast` read seven `uint64` words out of a 49-byte
`std::array` — a seven-byte out-of-bounds read — and masked the wrong end of the
final word. It was replaced before it ever produced a number. Recorded because
it is precisely the defect that a "it's faster and the answers still look right"
workflow does not catch.

---

## 9. Limitations

- **Not a clean performance baseline.** Section 1. Nothing here may be promoted
  into `research/benchmarks/profiles-v1.json` or compared with a timing from
  another machine or another load.
- The trajectory gate's frozen depth-4 arm is 96 seeds × 45 moves, not 4,096
  seeds; the 4,096-seed breadth comes from the deterministic arm and a depth-3
  search arm. Section 4D explains why this is still a valid engine test and what
  it buys less of.
- The search-parity grid is deep but not wide: 50 moves per configuration at
  depths 1–3, 10 at depth 4, 3 at depth 5. Depth-5 parity rests on three
  decisions — each of which is tens of millions of work units, so it is strong
  evidence per decision and thin evidence across positions.
- No `PILOT`/`SCREEN`/`STANDARD` cohort was run and **no strength claim is
  made**. The fast engine has not produced a single point of score evidence.
- Two of the three trajectory arms (`center`, `d3fast`) were run on the build
  immediately preceding the L7 roughness removal. That change is confined to
  `fastFairLeaf` and cannot affect engine mechanics; the leaf's bit-exactness
  after it is separately gated on all 225,183 corpus leaves, and the depth-4
  arm ran on the final build.
- Section 6's 55,765,609 work/move for depth 5 at seven strata was measured at a
  declared cache capacity of 200,000 entries. Capacity changes work by up to
  2.2x at that configuration without changing any decision; see
  [`finding-14`](finding-15-depth5-exact-estimator.md) §3. Never compare work
  across capacities.
- The 30-thread figures in section 6 are arithmetic, not measurement. No
  parallel scaling preflight was run, as `docs/benchmarks.md` §"Using the
  operating system well" requires before a throughput claim.
- **A contribution record under `research/contributions/` is owed and not
  written.** This work package was constrained to create files only under
  `approaches/lifetime-objective/fast-engine/` and `docs/exploratory/`, and
  `research/` belongs to a concurrent contributor. The coordinator must add it
  before this is done under `AGENTS.md`.

---

## 10. Reproduce

```sh
./approaches/lifetime-objective/fast-engine/build.sh          # clang++ only
B=./build/fast-engine

# Gates
$B/gate-leaf       --games 3 --max-moves 45 --leaves 500000 --per-root 600
$B/gate-search     --anchor-games 2 --anchor-moves 30 --parity-games 2 \
                   --parity-moves 25 --reflect-games 2 --reflect-moves 20 \
                   --grid 1x5,2x5,2x7,3x5,3x7
$B/gate-search     --anchor-games 1 --anchor-moves 1 --parity-games 1 \
                   --parity-moves 10 --reflect-games 1 --reflect-moves 1 \
                   --grid 4x5,4x7
$B/gate-search     --anchor-games 1 --anchor-moves 1 --parity-games 1 \
                   --parity-moves 3 --reflect-games 1 --reflect-moves 1 \
                   --grid 5x5,5x7
$B/gate-trajectory --policy center --games 4096 --threads 8
$B/gate-trajectory --policy d3fast --games 4096 --threads 8 --seed-start 0xa5271000
$B/gate-trajectory --policy d4     --games 96   --threads 8 --max-moves 45 \
                   --seed-start 0xa5276000

# Profile, in-leaf profile and ablation
$B/profile      --games 2 --max-moves 30 --decisions 24 --repeats 3
$B/leafprofile  --games 1 --max-moves 25 --leaves 8000 --reps 60

# End to end and the depth-5 price
$B/bench --mode ab --grid 3x5,3x7 --games 4 --max-moves 2000 --repeats 3 --cache 60000
$B/bench --mode ab --grid 4x5     --games 2 --max-moves 30   --repeats 3 --cache 60000
$B/bench --mode ab --grid 4x7     --games 1 --max-moves 12   --repeats 3 --cache 200000
for d in "4 5" "4 7" "5 5" "5 7"; do set -- $d;
  $B/bench --mode probe --probe-depth $1 --probe-strata $2 --decisions 3 \
           --repeats 3 --probe-baseline 1 --cache 200000; done
```

Every one of these exits non-zero on any mismatch. Repeat the timing commands on
an exclusive machine before quoting any absolute number from this document.
