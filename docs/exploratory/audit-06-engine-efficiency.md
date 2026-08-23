# Audit 06 — Engine efficiency: where the fast engine can still go, and where it cannot

**Role.** Independent read-only performance audit of the fair-D4 hot path
(`approaches/lifetime-objective/fast-engine/`) and its cohort drivers. No
repository file was modified, no research seed was opened, no cohort was run.
Every timing below was taken while a 30-thread `leaf-evolution/evaluate` job
held the machine at load ~30 of 32 logical CPUs; **only ratios between arms run
back-to-back in one process are findings; no absolute throughput is claimed.**
Draft for coordinator review.

## Executive summary (ranked)

1. **One-entry board memo in the leaf — measured 1.64x (5 strata) / 1.87x (7 strata) per whole decision, bit-exact, zero action/work/hit/depth mismatches.** 66–69 % of leaf calls land on a board identical to the previous stratum's; only `next_disc_vertical_options` differs. Risk: low, but the coordinator must rule whether "one entry, no capacity" is a cache-semantics declaration under `docs/benchmarks.md`.
2. **`-march=native -ffp-contract=off` — measured ~1.08x leaf, ~1.09x move, bit-identical.** `-march=native` alone emits 73 FMAs and changes the leaf's bit pattern; the in-TU gate cannot see it. `learned-leaf/build.sh:39` already ships `-march=native` unpinned (hazard, see D).
3. Items 1+2 together: measured **1.93x** on the same roots. Every other bit-exact idea is worth <1.2x combined.
4. Thread-count/SMT/pinning preflight — unmeasurable today; plausible ±10–25 %; zero semantic risk; required by `profiles-v1.json` anyway.
5. PGO — measured no gain (leaf 1.00x, move 0.93x). LTO — nothing to link. Hugepages — THP already `always`, table fits the L2 TLB.
6. Transposition table: ≤1.01x for anything; a smaller or sharded table changes work at 7 strata/depth 5 and is a new configuration.
7. False sharing on the audit atomics: ~60 contended RMWs per second process-wide, ≈1e-7 of runtime. Leave it.
8. Scheduling is already game-major; live cohorts show 0.94–0.98 thread utilisation, so the straggler tail is 2–6 %.
9. GPU batched leaf (i): bit-exact in principle, needs a breadth-first last ply, capped at 2.4x/thread, and the APU's shader clock halves under the CPU load a cohort creates. Do the CPU memo first — it removes two-thirds of the leaves the GPU would batch.
10. GPU lockstep games (ii): RNG/engine parity is easy, the search is not a GPU workload at this fp64 rate — stays "research prototype only". GPU NNUE leaf (iii): only with batch-invariant reductions and the same restructuring as (i).

---

## A. CPU hot path

**Census** (finding-13 §2.1–2.3): 96.1 % of nodes are leaves; on the fast side
the leaf is 58 % of decision time, move application 41 %, table <1 %. Amdahl:
leaf free → 2.4x; move free → 1.7x; table free → 1.01x. In-leaf stages under
today's load (`leafprofile`, 3,000 leaves, load 30.9): masks/heights 19 %,
per-cell sweep 44 %, release inventory 13 %, twos 6 %, cover exposure 17 % —
the same shape as finding-13 §3.2.

| Idea | Bit-exact? | Why | Ceiling |
| --- | --- | --- | --- |
| Reuse board-only features across the strata of one `(node, column)` | **Yes** | `fast-leaf.hpp` reads `next_disc` only at :239 (validation) and :273–274 (`next_disc_vertical_options`, its own accumulator, column order 0..6); every other feature is a function of `(board, moves_remaining)`. Identical inputs give identical bits; the dot product (:536–561) is re-run in the frozen order. Work is unchanged: `work_` increments before the value exists (`fast-search.hpp:430`). | Distinct boards per strata group: 34.3 % (5 strata), 31.1 % (7). **Measured 1.64x / 1.87x.** |
| Memo across the 7 columns of a node as well | Yes | Same argument. | +1.4 points (34.3 → 32.9 % distinct). A one-entry "last board" memo already captures ~all of it because strata are evaluated consecutively (`fast-search.hpp:397–426`). |
| Incremental features after a drop | **No** for 9 of 18 features | `covered_height_risk` (:311, :315), `rise_pressure` (:271), both potentials, exposures, `adjacent_ones`, `triple_twos`, `dead_low_numbers` are left-to-right double sums; subtract-and-add does not reproduce the rounding. Only the integer-valued features are order-free, and they are the cheap ones. | New candidate, not an optimisation. |
| SIMD / bit-parallel extraction | Integer parts only | Stage-1 masks and the stage-2 prefix sums (:290–295) vectorise; the double accumulators cannot be reordered. | ≈20 % of the decision; 2x on it is ≤1.11x. |
| First-wave restricted popper scan (finding-13 §8.3) | Only given an unenforced invariant | 13 cells instead of 49. | ~15 ns of a ~190 ns move, ≤1.03x; ship only with a debug cross-check. |
| Move application | — | `playMoveFast` (`fast-engine.hpp:401–451`): one 49-byte copy, one scan (:148–172), ~0.9 cascades, two `stratifiedUnit` calls (`public-behavior.hpp:579–593`, one double division each). | Near its floor. |
| Cache locality | — | Leaf working set ≈5 kB (`LeafScratch` 2,432 B, run table 2,688 B, readiness 640 B): L1-resident. | Nothing in the leaf; table in B. |
| SMT / thread count | Yes | Game-level parallelism shares nothing. 16 physical cores, 30 workers; a scalar dependency-chain leaf typically gains 10–30 % from SMT, not 2x. | Unknown; measure on an idle host, fixed seeds, three repeats. |

**The memo, concretely.** A scratch implementation (key `board` +
`moves_remaining`; stores `FastLeafFeatures` and `heights`; recomputes
`next_disc_vertical_options` from the stored heights) against the unchanged
`FastSearch` on 12 synthetic depth-4 roots: 0 mismatches on action, work,
nodes, cache hits, completed depth; 17,045 leaf states bit-identical to
`fastFairLeaf`. Finding-13 §8.4 declined a *hash-table* memo at ≈20 MB/thread;
this costs 2.5 kB and a 49-byte `memcmp`. Before shipping: the repository
gates and a ruling on the cache-semantics question.

## B. Memory

Per search object: table 4,648,576 B at 60,000 entries (56 B entry + 4 B index
+ 4 B stamp, 2x slot over-provisioning; `fast-search.hpp:109–124, 203–209`),
16,194,304 B at 200,000; scratch 2,432 B; `State` 80 B. The live 30-thread
evaluate job has RSS 134 MB — ~4.5 MB/thread, i.e. the table is the footprint.

**L3.** Two CCDs × 32 MB, 15 threads each. A d4/5-strata decision touches
22,743 entries (1.3 MB) plus random slots of the 1 MB index/stamp arrays:
~2.3 MB/thread, ~35 MB per CCD — the tables alone exceed one CCD's L3. But the
table is probed at 3.9 % of nodes; at full DRAM latency that is ~6 ms of a
~350 ms decision (<2 %), and the leaf's working set is not displaced. So 30
parallel searches do not thrash L3 *in a way that matters*, which is what the
table's measured 1.01x (finding-13 §3.3) already says. Shrinking it to fit L3
would not change the arithmetic.

**Why a smaller or sharded table is not an optimisation.** Hits depend on
strict-LRU eviction at the declared capacity (`store`, :163–200). At d4/5
strata the 60,000 cap never binds (worst case 45,430), so any capacity
≥45,430 gives identical work. Below it, or at d4/7 strata (worst case 122,598;
`evaluate.cpp:71–72` defaults to strata 7 with cache 60,000, so it *can*
bind), or at depth 5, hits change, work changes by up to 2.2x (finding-14 §3),
and a work bound can then bind at a different node: different completed
depth, possibly a different column. `docs/benchmarks.md:71–74` makes that a
new algorithmic candidate. A shared sharded table would additionally make
hits depend on thread interleaving — non-deterministic work. Do not.

## C. GPU (Radeon 8060S, gfx1151, 20 CUs, unified 95 GB pool)

Rule (`references/hardware.md` "Workload placement"; `docs/benchmarks.md:180–183`):
search and exact transitions stay CPU-first; a port must pass transition/RNG
parity at fixed work and then beat the CPU end to end.

**(i) Batched leaf evaluation.** Bit-exactness is achievable: `fastFairLeaf`
is pure, uses only `+ - * /`, comparisons, `max` and table lookups in
`double`, no libm, and no cross-leaf reduction — the sums in `evaluateAction`
(:397–426) stay on the CPU in their order. Requirements: `double` (fp32 is a
new candidate), `hipcc -ffp-contract=off` (HIP contracts by default), and
correctly rounded fp64 division for `rise_pressure` (HIP's is). The APU
removes PCIe, not coherence: 765k × 80 B = 61 MB of `State` per decision per
thread must be made visible to the GPU. The search is depth-first with a
per-node LRU store, so batching the last ply means a new driver. *Corrected
after adversarial review:* naive breadth-first expansion is not order-invariant
even with an unbounded budget, because it reorders transposition probe/store
interleaving and changes hits and work; but fair D4's traversal is
value-independent (full width, no pruning), so a record/replay driver — a CPU
pre-pass that fixes control flow, one batch of leaves, then an ordered replay
of the sums — is invariant even under a binding budget. Against
that: Amdahl caps it at 2.4x/thread, 30 CPU threads must feed ~65 M leaves/s,
fp64 runs at a fraction of fp32 rate on RDNA 3.5, the release-inventory and
sort loops diverge per lane, and gpu-01 §5.3 measured the shader clock falling
from 2,084 to 1,059 MHz at host load 41 — the load a cohort creates.
**Verdict: not now.** First experiment if revisited: a HIP micro-benchmark of
`fastFairLeaf` over the 225,183-state `gate-leaf` corpus, `uint64`
bit-compared to the CPU, reporting leaves/s at load ~30.

**(ii) Thousands of games in lockstep.** RNG parity is easy: `mix32`,
`Mulberry32`, `headlessDisc` (`engine.hpp:53–97`) are integer; `stratifiedUnit`
divides by 2^32 (exact) and once by `count` (correctly rounded on both sides);
the real-game reveal stream is counter-based per move
(`playHeadlessMoveFast`, `fast-engine.hpp:457–471`), so lanes are stateless.
The cascade loop (0.9 waves/move, tail to depth 20) diverges but is small. The
search is the problem: each lane would run iterative deepening over a private
4.4 MB strict-LRU table, recursion, a budget exception and 1–7 legal columns
per node — plausibly 20–40 % wavefront efficiency, fp64 leaf on top.
**Verdict: research prototype only, expected to lose to 30 CPU threads.** The
Stage C gate (1 M mixed transitions, exact checksums) is cheap and
prerequisite, but says nothing about the search.

**(iii) NNUE/learned leaf or population evaluation, deterministic but not
CPU-bit-exact.** Viable in principle (Stage B) under two conditions: a
synchronous service at batch 35 is launch-bound (gpu-01 shows wins only at
batch ≥1,024), so it needs the breadth-first last ply of (i); and
"deterministic" must mean batch-shape-invariant — library reduction order
changes with tile selection, so one state in two batch sizes can differ in
ulps and flip a near-tied root; use a per-row fixed-order HIP reduction.
`leaf-evolution` population evaluation is whole-game play, not a matrix
product; the GPU cannot help it. **Verdict: defer until a learned leaf beats the fair
leaf on CPU at fixed work.**

## D. Cheap wins available now

**`-march=native` needs `-ffp-contract=off`, and the in-TU gate cannot tell.**
All three cohort builds (`fast-engine/build.sh:37`,
`leaf-evolution/build.sh:55`, `survival-instinct/build.sh:40`) target baseline
x86-64: no FMA, no `popcnt` instruction (`__builtin_popcount` is emulated),
`bsf` for `ctz`. Clang's default `-ffp-contract=on` fuses `a*b+c` inside one
statement *when the target has FMA*; baseline x86-64 has none, so the current
builds are exact by accident. With `-march=native` (Zen 5) the leaf emits 73
FMAs and its output hash changes (`c15253a0e551014e` → `b2fff45b8381b5fa`);
adding `-ffp-contract=off` restores the baseline hash, and the engine hash is
unchanged in all builds. Contracted sites: `elevation*elevation*edge_multiplier`
accumulations (`fast-leaf.hpp:311, 315`), `1.0 - (1.0-a)*(1.0-b)`
(`fast-engine.hpp:94–96`), `support[0]*0.35 + support[1]*0.65` (:521–522),
the 18-term dot product (:536–561). `gate-leaf` compiles the frozen reference
in the same TU with the same flags, so both sides contract identically and the
gate passes while the binary disagrees with `build/fair-depth4` — finding-13
§8.1 predicted this. **Any flag change needs a cross-build bit-pattern hash.**
Measured, interleaved, three rounds: leaf 534→494 ns (1.08x), move 288→263 ns
(1.09x); `-march=x86-64-v3 -ffp-contract=off` is equivalent. `#pragma STDC
FP_CONTRACT OFF` at the top of `fast-leaf.hpp` would pin it in source.

**Existing hazard.** `learned-leaf/build.sh:39` uses `-march=native` without
the contract flag; `build/lifetime-leaf/{leaf-check,net-check,leaf-probe,search}`
contain 18, 137, 426 and 482 FMA instructions and compile the frozen reference
leaf into the same TU. *Corrected after adversarial review (see
"Reconciliation"):* this is a bit-identity hazard, not a doubt about recorded
numbers — finding-08's frozen-leaf arms reproduced finding-05's cohort to every
digit over 12,916 depth-4 decisions, which is a cross-build check of that
binary's decisions. Pin the flag and record a cross-build leaf hash; re-derive
the learned-arm figures only if they are ever promoted beyond development tier.

**LTO:** one translation unit of `inline` headers per program; nothing to
optimise. **PGO:** `-fprofile-instr-generate/-use` on the leaf probe:
bit-identical, leaf 1.00x, move 0.93x. **Hugepages:** THP `[always]`; the
4.4 MB table fits the L2 DTLB reach anyway. **Pinning:** plausible single-digit
gain from keeping a thread's table in its 1 MB L2; measure, do not assume.
**Atomics:** `Individual` (`evaluate.cpp:52–60`) and `Arm` (`run.cpp:43–48`)
pack 4–8 atomics into shared lines — touched once per ~0.5 s decision, ≈1e-7
of runtime. **Scheduling:** already game-major with a dynamic counter
(`evaluate.cpp:182–186`); live artifacts (gen-018/019/020, 544 tasks, 30
threads) show utilisation 0.94–0.98, longest game 159–329 s against a 45–54 s
mean. The 2–6 % is the final straggler; leave it.

### D.1 Verified

Follow-up requested by the coordinator: does the contraction in
`learned-leaf/build.sh:39` change the **frozen** leaf and its decisions?
`build/lifetime-leaf/fair-only-depth4-noentry.cpp` (learned-leaf's own
generated copy of the reference TU, sha256 identical to
`build/fast-engine/`'s) was compiled three times into the scratchpad with
learned-leaf's flags (`-O3 -std=c++20 -pthread -Wall -Wextra`), then
`-march=native` added, then `-march=native -ffp-contract=off`; nothing in the
repository was modified. Corpus: the same 17,045 synthetic states (0x5eed
playground domain); probes: `ref::chooseDepth4Action` at depth 4 / 5 strata on
13 synthetic roots. Load 27–29.

| Build | FMA insns | `frozen::fairLeaf` hash | leaf bit mismatches vs base | column mismatches |
| --- | ---: | --- | ---: | ---: |
| base (no `-march`) | 0 | `c15253a0e551014e` | — | — |
| `-march=native` (learned-leaf's flags) | 463 | `b2fff45b8381b5fa` | **2,890 / 17,045 (17.0 %)** | 0 / 13 |
| `-march=native -ffp-contract=off` | 0 | `c15253a0e551014e` | 0 / 17,045 | 0 / 13 |

The hazard is real: one frozen leaf value in six is a different double under
learned-leaf's build, and the FMAs are inside the leaf. The contracted frozen
leaf has exactly the hash the contracted *fast* leaf had in D, so an in-TU gate
passes in both builds — blind, as predicted. No column changed on 13 depth-4
probes; that is expected (ulp changes flip only near-tied roots) and is not
safety evidence: one differing root in a few hundred changes a cohort's paired
deltas, and 13 synthetic roots cannot bound that rate. Any
learned-leaf figure that compares a candidate against `frozen::fairLeaf` or
`ref::chooseDepth4Action` from those binaries was measured against a
comparator that is not `build/fair-depth4`; adding `-ffp-contract=off` to
`learned-leaf/build.sh:39` restores bit-identity at no measured cost.

Scratch sources: `fmacheck.cpp`, `fma_{base,native,native_off}.bin` in the
session scratchpad.

## What I measured

Ryzen AI MAX+ 395, 16c/32t, L1d 48 kB, L2 1 MB/core, L3 2 × 32 MB; AMD clang
23; load 29.5–31 throughout (`build/leaf-evolution/evaluate`, PID 226745,
30 threads, RSS 134 MB). Synthetic states: `playMoveFast` driven by a
`Mulberry32` seeded in the `0x5eed****` playground domain, columns from a
depth-2 fast search — no research seed, lease or cohort touched. Scratch
sources live in the session scratchpad, not the repository. Per-round values
are given as best/worst.

```
# flag sweep: 17,045 leaf states; 2,435 nodes x legal columns x 5 strata; reps 20; 3 interleaved rounds
clang++ -O3 -std=c++20 [flags] leafbits.cpp
 base                    fma=0   leaf c15253a0e551014e  move 38bbc417ea9f3d07  leaf 531-536 ns  move 285-290 ns
 -march=native           fma=73  leaf b2fff45b8381b5fa  move 38bbc417ea9f3d07  leaf 497-500     move 264-297
 -march=native -ffp-contract=off   fma=0  hashes = base                        leaf 491-496     move 261-264
 -march=x86-64-v3 -ffp-contract=off fma=0  hashes = base                       leaf 501-513     move 269-271
 PGO (llvm-profdata-19, same probe): hashes = base; leaf 493-539 vs base 498-535; move 287-311 vs 268-293
# sibling census: instrumented copy of FastSearch, depth 4, max-work 20M
 8 decisions, 5 strata: work/decision 1,634,376; ns/work 434; 34.8 leaves per depth-1 node
   strata group: distinct boards 34.3 %, distinct states 93.4 %; whole node: 32.9 % / 87.2 %
 6 decisions, 7 strata: work/decision 5,729,821; ns/work 436; 48.7 leaves per node
   strata group: distinct boards 31.1 %, distinct states 100.0 %; whole node: 30.2 % / 93.5 %
# board memo vs unchanged FastSearch, same roots, interleaved per root
 9 roots, 5 strata, 3 reps: plain 5.104-5.118 s, memo 3.116-3.121 s, ratio 1.635-1.642, mismatches 0, work 12,491,685
 3 roots, 7 strata, 2 reps: plain 3.710-3.758 s, memo 1.982-1.988 s, ratio 1.87-1.90, mismatches 0
 same, built -march=native -ffp-contract=off: plain 4.549-4.578 s, memo 2.651-2.660 s; vs base plain 1.93x
 memo leaf vs fastFairLeaf, 17,045 states: hash c15253a0e551014e both, bit mismatches 0
# in-leaf profile (repository binary on lease seeds it already reads), load 30.93
 build/fast-engine/leafprofile --games 1 --max-moves 8 --leaves 3000 --reps 20
 frozen 1520.9 | s1 61.3 | s2 201.4 | s3 243.6 | s4 246.7 | s5 265.4 | s6 321.4 ns
# live artifacts runs/RUN-20260822T014412Z-a0c63063/gen-0{18,19,20}/population.json
 utilisation 0.94 / 0.96 / 0.98; reveals per move 1.08-1.12; 507-520 ms/move at 30 threads
```

Absolute nanoseconds are 1.5–2.5x above finding-13's low-load figures and
must not be quoted; the ratios are the findings.

## What I did not verify

- The memo against the repository gates, on real lease seeds, or inside the
  generated `weighted-search.hpp` / `filtered-search.hpp` variants. Synthetic
  depth-2 roots are shallower than real D4 roots; the sharing fraction could
  differ on tall, cover-heavy boards.
- Any thread-count, SMT, pinning or governor effect — impossible on a loaded
  host.
- That 1.64x survives 30-way parallel execution (bandwidth is not the limiter
  on paper; not measured).
- Whether any *recorded* learned-leaf number actually moved: D.1 shows the
  frozen leaf's values change under that build, not which published figure
  depends on them.
- Any GPU code: no HIP kernel was written or timed; the gfx1151 fp64 rate is
  from public architecture descriptions.
- `perf` is not installed; stage costs come from the repository's
  `if constexpr` profile, not sampling.

## Reconciliation after adversarial review

An independent review by Kimi K3
(`runs/RUN-20260822T072033Z-510cf15e/kimi-k3-audit-review.md`) accepted the
audit's findings with the following corrections, which the coordinator adopts:

- **Memo placement is a requirement, not an implication.** The one-entry leaf
  memo is work-invariant only if it wraps the `fastFairLeaf` call *below* the
  `checkBudget(); ++work_;` lines (`fast-search.hpp:429-431`). A memo installed
  above `evaluateLeaf` would change logical work and be a new candidate.
- **Ruling:** the memo is an implementation detail of the same class as
  finding-13's no-allocation rewrite, not a cache-semantics declaration — it
  has no capacity and therefore cannot change hits, work, completed depth or
  the selected column. Record it as a one-line manifest note ("one-entry
  last-board leaf memo; work-invariant by construction") when adopted.
- **Ratios downgraded to indicative:** the 1.64x / 1.87x / 1.93x figures rest
  on 12 synthetic roots; the consecutive-hit rate was inferred from the
  distinct-board census, not printed. The port must print the hit rate and be
  confirmed on real lease roots and under 30-way parallel execution. The
  1.08x / 1.09x flag ratios are solid as ratios. PGO's 0.93x is within spread.
- **Hazard reworded** (section D): no recorded number is in doubt; see the
  corrected sentence above. `learned-leaf/build.sh:39` should still be pinned
  with `-ffp-contract=off` and a cross-build leaf hash recorded.
- **GPU (i) premise replaced** with the record/replay framing (above); gpu-01
  §5.2 shows batched-inference wins from batch 256 (solid from 1,024), so
  "only at ≥1,024" was imprecise. All three verdicts stand.
- **Three further bit-exact candidates the audit did not name**, from the
  review: (1) reuse of a move outcome across strata when the cascade consumed
  zero reveal draws — a deterministic function of `(state, column)`, keeping
  `++work_` per stratum; ceiling ≈ 1.15-1.22x, composes with the leaf memo;
  (2) interleaving two independent searches in one thread to hide the leaf's
  scalar dependency-chain latency (zero semantic risk, 1.2-1.5x plausible,
  measure in the thread preflight); (3) a bounded multi-entry leaf memo for
  non-consecutive repeats, capped by finding-13 §8.4's eviction objection.

## D.2 Ported and gated (coordinator, 2026-08-22)

The memo is now `approaches/lifetime-objective/fast-engine-memo/` (`memo-leaf.hpp`,
`MemoSearch` generated from `fast-search.hpp` with a 15-line diff guard and a
build-time check that the memo call sits directly below `++work_`;
`-ffp-contract=off` pinned). Gates on real probe seeds
(`runs/RUN-20260822T073338Z-48e4df54/gates.log`, load ~30):

| gate | result |
| --- | --- |
| leaf bits, d4s5 / d4s7, search feeding order | 0 / 4,260 and 0 / 2,500 mismatches; memo hits 56.5 % / 63.9 % |
| search parity vs `FastSearch`, d4s5 160 moves / d4s7 50 moves | 0 action/work/node/hit/depth mismatches; 117.7 M / 119.6 M leaf calls; hit rate **61.7 % / 68.5 %** |
| determinism, 4 games, 1 vs 4 threads | 0 mismatches |
| interleaved timing, real roots (indicative) | **1.58x** (d4s5, 12 roots × 3 reps), **1.63x** (d4s7, 6 roots × 2 reps) |

The real-root ratios sit below the synthetic 1.64x / 1.87x, as the review
anticipated (consecutive-hit rate, not distinct-board rate, is what the
one-entry memo captures). Not yet adopted by any cohort runner; the in-flight
experiments keep their gated binaries.
