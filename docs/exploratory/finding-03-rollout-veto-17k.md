# Finding 03 — The 25-move rollout veto under corrected 17,000-point scoring

**Status:** exploratory. Run validity **`valid`**; scientific outcome
**`fail`**; evidence tier **`development`** (SCREEN, 32 paired whole games).
A valid negative result: the mechanism was tested at corrected scoring and
rejected.
**Namespace:** `approaches/lifetime-objective/rollout-veto-17k`, seed lease
`SEEDLEASE-A51D-VETO` = `0xa51e0000`–`0xa51e3fff`.
**Nothing in `docs/research/`, `artifacts/`, `research/`, or any existing
approach source was modified by this work.** The historical source
`approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto.cpp` is byte-for-byte
untouched.

---

## 0. Why this retest exists

[`audit-04-blind-spots.md`](audit-04-blind-spots.md) §A.2 and §C.4 identify the
D4 + 25-move exact-D2 rollout veto as **"the single largest unexploited number
in the repository."** Its history:

- a single pilot on `0x3ded0000` scored **404,047 in 250 moves** against fair
  D4's **159,616 in 105 moves**;
- it was recorded "signal only" and shelved because a projected 15,341 s
  exceeded a 2,700 s runtime ceiling;
- every one of those numbers was produced under the **wrong 7,000-point level
  bonus**.

Audit-04 §C.2's exact paired-rescoring identity is

```
Δscore₁₇ = Δscore₇ + 2000 · Δmoves
```

because a game of `M` moves earns `floor(M/5) − 1` level bonuses under the
engine's terminal rule (the rise that fails pays nothing), and each is worth
17,000 − 7,000 = 10,000 more. Applied to the pilot the rescored figures are
**894,047 vs 359,616**, and to the four-game quality extension **392,160 vs
242,008**.

That is 89% of the qualification bar from a legal public-information policy —
on **one game**. This document is the properly powered retest.

---

## 1. What the original program actually does

Read in full: `approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto.cpp`
(1,736 lines).

### 1.1 Decision procedure

| Stage | Behaviour |
| --- | --- |
| Default policy | Unmodified qualified fair D4 — full-width, completed depth 4, five stratified chance samples. Throws if the search does not complete at depth 4. |
| Routing predicate | `isDanger` = maximum column height ≥ `kDangerHeight` (4). Nothing else gates the rollout: it fires in every rise phase. |
| Candidate set | **Every legal root column**, in `kColumnOrder` = {3,2,4,1,5,0,6}. The D4 column is the incumbent; all other legal columns are challengers. There is no pre-filter on root-Q, no top-k, no width limit. |
| Continuation | For each (action, scenario) pair the state is played forward `kRolloutHorizon` = 25 synthetic moves. Step 0 plays the candidate column; **every later step calls `fairDepthTwoAction`**, a fresh completed full-width fair D2 search on the canonicalized public observable state, with its own `SearchContext` and its own cache. The continuation function's type is `int(*)(const ObservableState&, D2Metrics*)`, statically asserted, so it structurally cannot receive tape or scenario metadata. |
| Scenarios | `kScenarios` = 7. |
| Return | Sum of raw `score_delta` over the rollout, **plus `fair::kTerminalUtility` = −1,000,000** if the branch dies, **or** one unscaled `fair::fairLeaf(state)` if it survives all 25 steps. |
| Veto test | Four conditions, ANDed (`testAlternative`). |
| Selection | Among alternatives that pass all four, the one with the largest return lower bound. Ties resolve to the incumbent because `best_lower` starts at 0.0. |

### 1.2 The four veto conditions

```
survivors_ok :  candidate.surviving_scenarios − D4.surviving_scenarios ≥ 0
clears_ok    :  candidate.mean_numbered_clears − D4.mean_numbered_clears ≥ 0
return_ok    :  mean(Δ) − 2.446912 · sd(Δ)/√7  >  0      over the 7 paired scenarios
root_q_ok    :  D4.rootQ[d4] − D4.rootQ[alt]  ≤  kMaximumRootQLoss
```

`kMaximumRootQLoss` is written as `static_cast<double>(kLevelBonus)` — "one
canonical level bonus" — which evaluated to **7,000** when the file was written.

### 1.3 Common random numbers: yes, and event-ordinal aligned

The tape seed is `seed32(publicHash(canonical root) ^ "D2RT")` — a function of
the **public state only**, never of the game seed. All sibling actions at a root
therefore share one tape seed, and every draw is
`stratifiedUnit(tape_seed, scenario, 7, domain, event)`, so each of the seven
scenarios lands in a **distinct rotated stratum** of [0,1) for every event index.
Reveals (`"D2RV"`) and future visible discs (`"D2VS"`) use separate domains, and
the reveal tape is indexed `step·64 + event_within_step`.

This is genuine CRN across siblings, and it is exactly stratified rather than
merely seeded. Its one limitation: alignment is by **event ordinal within a
step**, so two siblings whose cascades consume different numbers of reveals draw
different values at the same ordinal. The variance reduction is therefore
weakest precisely in the long-chain situations the veto exists to detect.

### 1.4 Reflection, information boundary, and resource proof

`evaluateRollouts` canonicalizes the root, evaluates in canonical coordinates,
and un-mirrors the result. `materialize` zeroes score, level and move counter,
so score/level/absolute move index are structurally unreachable. Fixed worst-case
bounds (`kWorstD2Work` = 2,485 per D2 call; 1,176 D2 calls and 1,225 synthetic
transitions per routed decision at horizon 25) are asserted at runtime and the
program throws rather than degrading. This is a well-built, fail-fast program.

### 1.5 Runtime bound it was shelved on

`kMaximumProjectedWallSeconds` = 2,700 s, applied to
`first_paired_game_wall × kFullProtocolProjectionWaves (18)` at a hard-coded
`kParallelism` = 4, covering the whole 4 + 8 + 8 + 16 paired-game protocol.
The measured projection was 15,341 s, so the program wrote a `paused` artifact
after **one** paired game and exited 3.

### 1.6 Defects and design hazards found

| # | Finding | Severity |
| --- | --- | --- |
| **D-1** | **The `return_ok` test is very nearly unpassable.** It requires `mean(Δ) > 0.9248 · sd(Δ)` over 7 samples. Scenario returns mix ordinary score deltas (10³–10⁵) with a −1,000,000 terminal utility, so `sd(Δ)` is dominated by *whether* a branch died and is routinely 10⁵–10⁶. Measured over the cohort: **12,203 of 12,314 alternatives (99.1%) failed this one test**, and it is the single reason the veto is rare. | **critical** |
| **D-2** | `kPairedT975Df6` = 2.446912 is `t(0.975, df=6)` — a **97.5%** one-sided bound, used and named as `lower95`. The 95% one-sided value is 1.943. The test is 26% stricter than its name claims, worsening D-1. | high |
| **D-3** | `kMaximumRootQLoss = static_cast<double>(kLevelBonus)` **silently tracks the engine constant**. Porting to the corrected engine doubles the band as a side effect of an unrelated change. This is precisely why audit-04 requires it be treated as a parameter. | high |
| **D-4** | Correcting the level bonus scales the *accumulated-score* part of the return by ≈2.43× but leaves `kTerminalUtility` = −1,000,000 and the `fairLeaf` tail **unchanged**. The correction therefore re-weights the veto's own objective in a way nobody chose: score-vs-survival and score-vs-leaf mixtures both move. It is not a cosmetic rescoring. | high |
| **D-5** | The routing predicate `maxHeight ≥ 4` is coarse: it fires on **77.3%** of decisions over the cohort, so the "conservative correction" pays 1,176 completed D2 searches on roughly three of every four moves in order to change 3.2% of them. | medium |
| **D-6** | Hard-coded shared `/tmp` output paths (`/tmp/drop7-d4-d2-rollout-veto.json`, `…-teacher.jsonl`) — a collision hazard the benchmark contract explicitly calls out. | medium |
| **D-7** | `kMaximumMoves` = 1,000, not the contract's 2,000; `kParallelism` = 4 hard-coded, so the runtime projection that killed the experiment was never a fixed-work measure and is not portable across machines. | medium |
| **D-8** | The fitting gate applies a 0.90 lower-half retention rule to a **4-game** cohort, i.e. a two-game statistic. Audit-04 already flags the same problem in the quality extension. | medium |
| **D-9** | It cannot be compiled. See §2.1. | blocking |

Nothing found is a *correctness* bug in the game model, the reflection handling,
or the information boundary. The problems are statistical and operational.

---

## 2. The port

### 2.1 The compile failure, verified

```
$ clang++ -fsyntax-only -std=c++20 \
      approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto.cpp
approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto.cpp:90:15: error:
    static assertion failed due to requirement 'kLevelBonus == 7000'
   90 | static_assert(kLevelBonus == 7'000);
      |               ^~~~~~~~~~~~~~~~~~~~
note: expression evaluates to '17000 == 7000'
1 error generated.
```

Exactly one error, exactly the one audit-04 predicted. `clang++` is AMD clang
23.0.0git. (`clang++` is used explicitly: the Makefile's `CXX ?= clang++` is
inert because GNU make predefines `CXX=g++`, and g++ rejects the reference
sources with a false-positive `-Werror=array-bounds`.)

### 2.2 Complete change list

New file `approaches/lifetime-objective/rollout-veto-17k/veto.cpp`. The original
is **not** included (it would double-include `fair-only-depth4.cpp`); the policy
routines are transcribed, and §2.3 proves the transcription is exact.

**Policy-affecting changes — there are two, and only two.**

| ID | Change | One-line justification |
| --- | --- | --- |
| **P1** | `static_assert(kLevelBonus == 7'000)` → `== 17'000` | The sole compile error; the engine defines 17,000 and the corrected Hardcore standard requires it. |
| **P2** | `constexpr double kMaximumRootQLoss = static_cast<double>(kLevelBonus)` → runtime `--root-q-loss`, default 17,000 | Audit-04 D3: the constant meant "one canonical level bonus" and is now half its intended width; exposing both values makes it a measured parameter instead of a silent edit. |

**Harness-only, policy-neutral changes.**

| ID | Change | One-line justification |
| --- | --- | --- |
| H1 | `kMaximumMoves` 1,000 → `--max-moves` | The contract cap is 2,000; a lower cap must be a declared diagnostic choice, not a constant. |
| H2 | `kParallelism` 4 → `--threads` | Required by the task's 8-thread ceiling and by the contract's thread-count reporting. |
| H3 | Four hard-coded historical cohorts → `--seed-start` / `--games`, validated against `SEEDLEASE-A51D-VETO` | Prevents re-reading historical or protected ranges; the program refuses any seed outside the lease. |
| H4 | `/tmp` default outputs → required `--output`; a `/tmp/` prefix is **refused at parse time** | D-6; other agents share this machine. |
| H5 | `kRolloutHorizon` 25 → `--horizon` (default 25, max 25); the derived worst-case resource bounds are recomputed from the runtime horizon instead of being `constexpr` | Lets the horizon be reduced as an explicitly declared configuration if runtime demands it. Scenario count stays frozen at 7 because `t(0.975, df=6)` is tied to it. |
| H6 | The four-stage fitting/held-out/screen/confirmation protocol, its `Gate` struct, its runtime-pause path and its teacher-replay mode are removed | This experiment is a single preregistered paired cohort with its own gate; reusing a gate designed for 4-game fitting would import D-8. |
| H7 | Bespoke game loop, summary and artifact writer → `approaches/lifetime-objective/common/harness.hpp`, **included unmodified** | Gives the same schema, the same bootstrap, and a per-game assertion of `score = 17,000·rises + 70,000·clears + Σ chain points` as every other exploratory arm. |
| H8 | Added counters: `alternatives`, plus per-game veto opportunities / vetoes taken / per-condition rejections / D4 and rollout seconds | The task requires them; none feeds back into the policy. |
| H9 | Removed unused `sameObservable`, `mirror`, `TeacherRecord` | Dead code in the ported subset. |

Nothing else differs. A normalized (comments and line-wrapping stripped) diff of
the transcribed region against the original yields **only** the entries above.

### 2.3 Differential parity against the frozen source — byte-identical

`build.sh` generates a build-tree copy of the frozen source in which **exactly
one line** is changed (its 7,000 assertion) and refuses to proceed if `diff`
reports anything else. `parity-original.cpp` links that copy and prints a
canonical digest of `evaluateRollouts` — per-column mean return, surviving
scenarios, mean numbered clears, plus legal-action count, synthetic transitions,
D2 calls, D2 work and D2 nodes — over 10 public states reached by a deterministic
fair-D2 walk from `0xa51e3f20`. `veto --parity-dump` prints the same digest.

```
PARITY: BYTE-IDENTICAL over 80 digest lines
sha256  fff708de5126f3456156c11067240234311ea993766d10cb6d1bed8e669846e9
```

This check earned its keep: it caught a real transcription error on the first
run. `mix64`, `publicHash` and `seed32` had been written from memory rather than
copied, which changed the tape seed and therefore every realized scenario. The
digest diverged, the routines were corrected to verbatim, and the digest then
matched byte for byte. **The ported policy is the original policy.**

### 2.4 CHECK-tier results

```
ROLLOUT_VETO_17K_SELFTEST {"scoring": true, "deterministic": true,
  "reflection": true, "metadataBlind": true, "routedAction": 4, "routed": true,
  "alternatives": 6, "passingAlternatives7k": 0, "passingAlternatives17k": 0,
  "bandMonotone": true, "d4Fallback": true, "passed": true}
```

Covers: 17,000/70,000/5-move scoring; repeat determinism of a full rollout;
horizontal-reflection equivalence; metadata blindness (score 987,654,321,
level 42, move 999 change nothing); legality at a routed root; monotonicity of
the root-Q band; exact fair-D4 fallback when routing is disabled; the fixed
worst-case D2 arithmetic; and seed-lease containment. Score-decomposition
identity is additionally asserted by the shared harness on **every** game.

---

## 3. Runtime, measured before any strength claim

Single-thread probe, seed `0xa51e3f00` (probe block, **excluded from the
cohort**), horizon 25, capped at 40 moves.

| Quantity | Value |
| --- | ---: |
| Decisions | 40 |
| Routed (danger) decisions | 25 (**62.5%**) |
| Legal alternatives scored | 150 |
| D2 continuation calls | 24,115 |
| Synthetic transitions | 24,982 |
| **Fair D4 cost per decision** | **1.657 s** |
| **Rollout cost per routed decision** | **2.242 s** |
| **Total cost per decision** | **3.058 s** |

Two things matter here.

**First, the rollout is not the dominant cost — fair D4 is.** Over the probe,
D4 consumed 66.3 s and the entire 7-action × 7-scenario × 25-step rollout
consumed 56.1 s. The veto multiplies total decision cost by roughly **1.85×**,
not the order of magnitude the original's shelving implies. The original's
15,341 s projection was for the complete 36-paired-game four-stage protocol at
a hard-coded parallelism of 4, on a machine whose profile is not recorded.

**Second, these are contended timings and are not benchmark-grade.** Two other
agents were running unbounded jobs on this box throughout (load average 20–70 on
32 logical CPUs). Everything here was run `nice -n 10` on at most 8 threads. The
absolute seconds are upper bounds; the *ratios* are the transferable numbers.

**Projection for a 32-game paired cohort on 8 threads.** At 3.058 s/decision and
a fair-D4-like mean lifetime near 90 moves, the candidate arm is ≈4 waves ×
275 s ≈ 18 min and the comparator arm ≈10 min. Even against the declared 600-move
cap the absolute worst case is 4 × 600 × 3.058 s ≈ 2.0 h for the candidate arm.
**Both are inside the 4-hour budget, so neither the 25-move horizon nor the
7-scenario width was reduced.** The configuration tested is the original one.

### 3.1 Realized cost (from the cohort itself)

The projection was optimistic by about 2× because contention rose during the
run, but it was the right side of the decision.

| Quantity | Realized over 32 paired games |
| --- | ---: |
| comparator arm wall (32 fair-D4 games, 8 threads) | **1,266 s** |
| candidate arm wall (32 veto games, 8 threads) | **2,040 s** (**1.61×**) |
| total cohort wall | **3,306 s (55 min)** |
| candidate decisions | 2,763 |
| routed decisions | 2,137 (**77.3%**) |
| fair-D4 CPU seconds inside the candidate arm | 7,633 |
| rollout CPU seconds inside the candidate arm | 7,029 |
| **CPU seconds per decision** | **5.307** |
| **rollout CPU seconds per routed decision** | **3.289** |
| D2 continuation calls | 1,961,918 |
| synthetic transitions | 2,063,075 |

The rollout adds **1.92×** to total decision CPU (7,633 → 14,662 s) and 1.61× to
arm wall time. That is the measured cost of the mechanism: it is expensive, but it
is roughly a factor of two, not the factor of six the original's 15,341 s-versus-
2,700 s pause suggests. **The runtime gate was never the real obstacle.**

---

## 3A. A preliminary look at the four veto conditions

Before the cohort, a diagnostic (`veto --veto-diagnostic`) dumped the raw
quantities behind all four veto conditions for **every** alternative at **every**
routed decision of one probe game (seed `0xa51e3f30`, 30 moves, horizon 25,
probe block, excluded from the cohort). 20 routed decisions, **120
alternatives**, **zero** vetoes taken.

Each condition evaluated independently on that probe:

| Condition | Alternatives passing |
| --- | ---: |
| `survivors_ok` — surviving scenarios ≥ D4's | 95 / 120 (79%) |
| `clears_ok` — mean numbered clears ≥ D4's | 33 / 120 (28%) |
| `root_q_ok` at the **original 7,000** band | 112 / 120 (93%) |
| `root_q_ok` at the **corrected 17,000** band | 120 / 120 (100%) |
| `return_ok` — paired t lower bound > 0 | **0 / 120 (0%)** |

The binding constraint is `return_ok`, and the reason is signal-to-noise, not
threshold placement:

| Quantity over 120 alternatives | Value |
| --- | ---: |
| median \|mean(Δ)\| | 14,369 |
| sd(Δ): Q25 / median / Q75 / max | 26,539 / 41,711 / 381,275 / 830,777 |
| median mean(Δ)/sd(Δ), all | −0.314 |
| median mean(Δ)/sd(Δ), positive-mean subset (n=31) | **0.288** |
| best mean(Δ)/sd(Δ) observed | 0.638 |
| ratio the 7-scenario test requires (`t(0.975,6)/√7`) | **0.925** |

Only 31 of 120 alternatives even had a positive mean advantage, and none reached
0.925. The −1,000,000 terminal utility is what inflates `sd(Δ)`: one scenario in
which one sibling dies and the other does not moves the difference by a million
points, which is 59 row rises.

Solving `r·√n > t(0.975, n−1)` for the observed ratios gives the sample size the
estimator would need: the **median positive** alternative ≈ **51 scenarios**,
the **single best** observed ≈ **16**, against the 7 it uses. Rollout cost is
linear in scenario count, so making this estimator decisive costs roughly
2.3×–7.3× more work per routed decision.

**Caveat, and why this section is only preliminary.** This is one probe game and
`n = 120`. At cohort scale (§5) the return test still rejects **99.1%** of
alternatives, but it does not reject all of them, so the 7,000 and 17,000 bands
are **not** provably identical policies and the ablation in §5.4 was run rather
than argued.

---

## 4. Preregistered gate

Frozen in
`approaches/lifetime-objective/rollout-veto-17k/PREREGISTRATION.md` **before any
cohort seed was read**. Reproduced here verbatim in substance.

- **Candidate:** rollout-veto-17k, horizon 25, 7 scenarios, danger height 4,
  root-Q band 17,000.
- **Comparator:** unmodified fair D4, same binary with routing disabled, same
  ordered seeds.
- **Cohort:** `SCREEN`, 32 paired games, `0xa51e0000`–`0xa51e001f`, lease
  `SEEDLEASE-A51D-VETO`, role exploratory development diagnostic.
- **Cap:** 600 moves, a declared diagnostic cap applied identically to both arms
  (the longest fair-D4 game in the ledger is 285 moves and the longest
  rollout-veto game is 250, so this is >2× headroom while bounding wall time).
- **Bootstrap:** 20,000 percentile resamples over whole games, one-sided 95%,
  RNG domains `0xa51e5eed` (score) and `0xa51e6eed` (moves).

**PASS requires all five:**

| ID | Condition |
| --- | --- |
| G1 | paired mean score delta > 0 |
| G2 | one-sided 95% whole-game bootstrap lower bound on the paired score delta > 0 |
| G3 | paired mean move delta > 0 |
| G4 | score wins ≥ 20 of 32 |
| G5 | candidate clears/move ≥ D4's **and** candidate reveals/move ≥ D4's |

**FAIL** if G1 or G2 fails. **FAIL-on-design** — a distinct but equally valid
negative — if fewer than 1 veto is taken per 50 veto opportunities, because the
deployed policy is then unmodified fair D4 and what the cohort rejects is the
*architecture*, not the idea of a long rollout. **INCONCLUSIVE** only on runner
failures, score-identity failures, or an incomplete cohort.

**Anti-tuning statement.** The 404,047/250-move pilot on `0x3ded0000` is a
single game under a wrong level bonus. It is the reason this retest exists and
it is **not** a tuning target. No parameter of this candidate was chosen by
looking at any cohort seed, the cohort is fresh within its lease, and the gate
above was written down first.

---

## 5. Results

Cohort completed 2026-08-20. 32 paired games, seeds `0xa51e0000`–`0xa51e001f`,
600-move cap, 8 threads, total wall 3,306 s. **0 runner failures, 0 illegal
moves, 0 incomplete decisions, 0 score-decomposition identity violations, 0
censored games in either arm.** Run validity: `valid`.

### 5.1 Cohort table

| Arm | mean | median | Q25 | min | max | sd | mean moves | censored | clears/move | reveals/move |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| fair D4 (comparator) | **339,290** | 257,270 | 188,045 | 120,279 | 725,934 | 188,407 | **98.66** | 0 | **2.0200** | **1.1308** |
| rollout veto 17k (candidate) | **292,780** | 250,794 | 187,589 | 120,286 | 799,183 | 152,600 | **86.34** | 0 | **1.9515** | **1.0749** |

The comparator arm is a useful sanity check on the whole apparatus: 339,290
points at 98.66 moves, with 94.26% of points from row rises, sits squarely
between the ledger-recorded fair-D4 figure (308,296 / 90.03) and
[`finding-01`](finding-01-score-is-survival.md)'s fresh 64-game reproduction
(321,992 / 94.06, 94.29% level share). Nothing is broken.

### 5.2 Paired whole-game comparison

| Statistic | score | moves |
| --- | ---: | ---: |
| paired mean delta | **−46,510.5** | **−12.31** |
| paired median delta | −9,264.0 | −2.00 |
| one-sided 95% bootstrap lower bound (20,000 resamples) | **−91,924.6** | **−24.34** |
| wins–ties–losses | **9–4–19** | 7–9–16 |
| paired delta standard deviation | 159,175 | — |

An exact one-sided sign test on the 28 non-tied games gives **p = 0.0436**. The
bootstrap lower bound, the mean, the median, the sign test and the move delta
all point the same way.

### 5.3 Veto activity

| Quantity | value |
| --- | ---: |
| decisions | 2,763 |
| routed (danger) decisions = **veto opportunities** | **2,137** (77.3% of decisions) |
| legal alternatives scored | 12,314 |
| alternatives passing all four conditions | 103 |
| **vetoes taken** | **68** (3.18 per 100 opportunities) |
| rejected on `survivors_ok` | 3,732 |
| rejected on `clears_ok` | 7,854 |
| rejected on `return_ok` | **12,203 (99.1%)** |
| rejected on `root_q_ok` (17,000 band) | 720 |
| D2 continuation calls | 1,961,918 |
| synthetic transitions | 2,063,075 |

The rule fires. It is not a null-action experiment, so the preregistered
FAIL-on-design branch does not apply: 68 vetoes is 1.59 per 50 opportunities,
above the 1-per-50 floor.

**Three games drew zero vetoes** (`0xa51e000a`, `0xa51e001b`, `0xa51e001d`) and
in all three the candidate and comparator trajectories are **exactly identical**
— same score, moves, clears, reveals and rises. That is a free determinism and
fallback check: when the rule does not fire, the candidate *is* unmodified fair
D4, bit for bit.

Isolating the 29 games in which the veto did act:

| Statistic over the 29 acting games | value |
| --- | ---: |
| paired mean score delta | **−51,322** |
| paired mean move delta | −13.59 |
| wins–ties–losses | 9–1–19 |
| median \|score delta\| | 93,096 |
| max \|score delta\| | 446,294 |

Attributing the whole cohort's paired total to the vetoes that caused it:

> **68 vetoes moved the cohort by −1,488,335 points and −394 moves — an average
> of −21,887 points and −5.79 moves per veto taken.**

The correlation between vetoes taken and score delta across acting games is
**+0.024**, i.e. none. More vetoes is not worse; **each individual veto is a
near-coin-flip whose mean is strongly negative.** Seed `0xa51e0003` gains
+299,746 from 3 vetoes and seed `0xa51e0006` loses −446,294 from 2. This is
exactly the heavy-tail hazard the methodology warns about, and it is why the
original's one-game pilot was uninformative in either direction.

### 5.4 Root-Q band ablation: 7,000 vs 17,000

Audit-04 §D3 asked for exactly one substantive parameter change beyond the level
bonus: `kMaximumRootQLoss`, written as "one canonical level bonus", should go
from 7,000 to 17,000. The port exposes it as `--root-q-loss`, so both values
were run as full 32-game candidate arms against the **same** comparator arm and
the same seeds.

| | **17,000 band** (corrected) | **7,000 band** (original) |
| --- | ---: | ---: |
| paired mean score delta | **−46,510.5** | **−53,445.4** |
| paired median score delta | −9,264.0 | −528.5 |
| one-sided 95% bootstrap LB | **−91,924.6** | **−96,039.5** |
| score wins–ties–losses | 9–4–19 | 9–5–18 |
| paired mean move delta | −12.31 | −14.22 |
| mean score | 292,780 | 285,845 |
| mean moves | 86.34 | 84.44 |
| clears / move | 1.9515 | 1.9360 |
| reveals / move | 1.0749 | 1.0637 |
| censored / identity failures | 0 / 0 | 0 / 0 |

Mechanism counters:

| | 17,000 | 7,000 |
| --- | ---: | ---: |
| veto opportunities | 2,137 | 2,077 |
| alternatives scored | 12,314 | 11,924 |
| rejected on `root_q_ok` | **720** | **2,266** |
| rejected on `return_ok` | 12,203 | 11,808 |
| alternatives passing all four | 103 | 103 |
| **vetoes taken** | **68** | **67** |
| games identical to the other band | — | **29 of 32** |

**The correction is real, measurable, and does not matter.** Narrowing the band
to 7,000 triples root-Q rejections (720 → 2,266) but changes the number of
vetoes actually taken by **one** (68 → 67) and leaves the number of
fully-passing alternatives unchanged at 103, because almost every alternative
the narrow band would have excluded was already excluded by `return_ok`. **29 of
32 games are identical between the two bands.**

Both bands fail all five gate conditions. The corrected 17,000 band is very
slightly the better of the two (+6,935 points on the paired mean, +1.9 moves),
which is well inside the noise of a 159,175-point paired standard deviation, and
both bootstrap lower bounds sit around −92,000 to −96,000.

So the audit's specified repair was applied, measured on a properly powered
cohort, and **it does not rescue the mechanism.** Reporting only the 17,000 arm
would have understated how little the parameter matters; reporting only the
7,000 arm would have left the audit's recommendation untested. Both are here.

*(Statistical note: the bootstrap lower bound was independently recomputed in
Python from the per-game records, reproducing the C++ harness's
−91,924.6 exactly, which confirms the resampling RNG domain and quantile
definition are as documented.)*

---

## 6. Flow-rate analysis — the mechanism is absent

This is the decisive section, and it is the one the score table alone cannot
supply. [`finding-01`](finding-01-score-is-survival.md) established that
Hardcore score *is* lifetime (r = 0.9995, 94.3% of points from the flat 17,000
row-rise bonus), and that lifetime is a conservation problem: each five-move
cycle inserts 5 placed discs + 7 risen covered discs = 12 discs onto a 49-cell
board, so indefinite survival requires **≥ 2.400 numbered clears per move** and
**≥ 1.400 covered reveals per move**.

| Arm | clears/move | deficit vs 2.400 | reveals/move | deficit vs 1.400 |
| --- | ---: | ---: | ---: | ---: |
| fair D4 (comparator) | 2.0200 | −15.8% | 1.1308 | −19.2% |
| rollout veto 17k | **1.9515** | **−18.7%** | **1.0749** | **−23.2%** |
| delta | **−0.0685** | | **−0.0559** | |

Per game rather than pooled: mean clears/move delta **−0.0569** (median −0.0614,
worse in **18 of 32** games); mean reveals/move delta **−0.0537** (median
−0.0315, worse in **18 of 32**). Rises per game fall from **18.81 to 16.31**.

**The veto moves both flow rates in the wrong direction.** This matters more
than the score number. For the rollout veto to be real, it would have to buy
survival, and survival is bought by clearing and revealing faster. It does the
opposite, and it does so despite `clears_ok` being one of the four conditions an
alternative must satisfy: 7,854 of 12,314 alternatives were rejected *for*
insufficient predicted clears, yet the 68 that were accepted still produced a
policy that clears less per move than plain D4.

That gap is the actual finding. The rollout's estimate of an action's clear
advantage — measured over 7 synthetic scenarios, 25 moves deep, under a D2
continuation — **does not transfer to the realized game under a D4
continuation.** The estimator is not merely noisy; it is measuring a different
policy's future. A veto chosen because it looks better under 25 moves of D2 play
is being executed in a game that will actually be played by D4, and the D4 game
that follows is shorter.

The score decomposition is unchanged in shape (level share 94.26% → 94.72%,
board clears 0.000 per game in both arms, max chain depth 10 in both), which
confirms the candidate is not trading rises for chains or clears. It is simply
dying sooner.

---

## 7. Verdict

| Field | Value |
| --- | --- |
| **Run validity** | **`valid`** — 0 runner failures, 0 illegal moves, 0 identity violations, 0 censored games, byte-identical rollout parity with the frozen source, all CHECK-tier gates passed |
| **Scientific outcome** | **`fail`** — all five preregistered conditions failed |
| **Evidence tier** | **`development`** (SCREEN, 32 paired whole games, exploratory development lease) |
| **Provenance label** | **Reproduced** — executed in this checkout |

| Gate | Condition | Observed | Result |
| --- | --- | ---: | --- |
| G1 | paired mean score delta > 0 | −46,510.5 | **FAIL** |
| G2 | one-sided 95% bootstrap LB > 0 | −91,924.6 | **FAIL** |
| G3 | paired mean move delta > 0 | −12.31 | **FAIL** |
| G4 | score wins ≥ 20 of 32 | 9–4–19 | **FAIL** |
| G5 | flow rates not below D4 | −0.0685 clears, −0.0559 reveals | **FAIL** |
| — | vetoes ≥ 1 per 50 opportunities | 1.59 per 50 | ok (design branch not taken) |
| — | runner / identity failures | 0 / 0 | ok |

**The corrected-scoring 25-move, 7-scenario, completed-fair-D2 rollout veto over
fair D4, with the corrected 17,000-point root-Q band, is rejected on a 32-game
paired development cohort. It is not merely no better than fair D4; on this
cohort it is significantly worse, and it degrades exactly the flow rates that
would have had to improve for it to work.**

### What this does and does not rule out

**Ruled out (this exact configuration):** horizon 25, 7 scenarios, danger
height 4, D2 continuation, `t(0.975,6)` return test, all legal siblings as
challengers, at **both** root-Q bands (17,000 and 7,000), on 32 fresh paired
games each against a common comparator arm.

**Not ruled out:** the family. A valid negative result rejects the tested
configuration, not the idea that a long public rollout can improve on D4. In
particular §3A shows the return estimator is under-sampled by roughly 2×–7×, so
"7 scenarios was not enough" is a live and untested alternative explanation for
part of the failure — though it cannot explain §6, where the *accepted* vetoes
made realized flow worse rather than merely random.

### What the audit's headline number was worth

Audit-04 §C.4 called the rescored 894,047-point pilot "the single largest
unexploited number in the repository." Properly powered, at corrected scoring,
with the correction audit-04 itself specified, **the mechanism does not
reproduce.** The 250-move pilot was a single draw from a distribution whose
per-veto standard deviation is roughly 160,000 points; this cohort contains a
+299,746 game and a −446,294 game side by side. That is what a one-game result
from this policy looks like. Audit-04 was right that the rejection was
confounded and right that it deserved a retest; it was wrong about which way the
retest would go, and now that is measured rather than assumed.

### Cheapest next test, if the family is pursued

Not a wider root-Q band (§5.4) and not a longer horizon. The one intervention
the evidence actually points at is **scenario count**: §3A puts the required
`n` at 16–51 against the 7 in use. A `--scenarios 32` variant at horizon 12
would cost about the same per routed decision as the present horizon-25/7 build
and would test the under-sampling hypothesis directly. It should be preregistered
as a new configuration, not as a repair of this one.

---

## 8. Limitations

1. **Contended timing.** Two other agents ran unbounded jobs on this 32-logical-CPU
   machine throughout (load average 20–70). Everything here ran `nice -n 10` on
   at most 8 threads. Absolute seconds are upper bounds and are **not**
   benchmark-grade; the ratios (candidate arm 1.61× the comparator arm's wall,
   rollout ≈1.85× a D4-only decision) are the transferable numbers. No timing
   claim here should be promoted without an exclusive-resource rerun.
2. **Declared 600-move cap**, below the contract's 2,000. It never bound: 0 games
   were censored in either arm and the longest game was 225 moves.
3. **n = 32.** The paired delta standard deviation is 159,175, so the 95%
   bootstrap lower bound is wide. The result is a `SCREEN`-tier rejection, not a
   precise effect-size estimate. It is sufficient to decline promotion and
   insufficient to quantify *how much* worse the veto is.
4. **One machine, one compiler.** clang++ (AMD clang 23.0.0git), `-O3 -std=c++20
   -pthread`. No cross-engine TypeScript parity sweep was run for this candidate;
   the policy is built from the frozen native reference and is proven
   byte-identical to the original at the rollout level (§2.3), which is a weaker
   claim than full engine parity.
5. **Development data, permanently.** Seeds `0xa51e0000`–`0xa51e001f` and the
   probe seeds `0xa51e3f00`, `0xa51e3f10`, `0xa51e3f20`, `0xa51e3f30` are now
   read and can never serve as confirmation evidence. No protected or final seed
   was touched, and no `STANDARD`, `QUALIFY`, `PROTECTED` or `FINAL` cohort is
   claimed.
6. **The historical pilot was not reproduced.** Seed `0x3ded0000` was
   deliberately **not** re-run. Re-running the exact seed that produced the
   headline number and reporting it would be selection on the outcome. This
   cohort is fresh and its gate was fixed first.
7. **Scope of §3A.** The condition-by-condition breakdown is one probe game
   (n = 120 alternatives). The cohort-scale counters in §5.3 supersede it where
   they disagree.

---

## 9. Reproduction

```bash
bash approaches/lifetime-objective/rollout-veto-17k/build.sh
./build/lifetime/rollout-veto-17k/veto --self-test
./build/lifetime/rollout-veto-17k/veto --parity-dump \
  | diff - <(./build/lifetime/rollout-veto-17k/parity-original)
./build/lifetime/rollout-veto-17k/veto --run \
  --seed-start 0xa51e0000 --games 32 --threads 8 --max-moves 600 \
  --horizon 25 --root-q-loss 17000 \
  --output approaches/lifetime-objective/rollout-veto-17k/runs/screen32-17k.json
python3 approaches/lifetime-objective/rollout-veto-17k/report.py \
  approaches/lifetime-objective/rollout-veto-17k/runs/screen32-17k
python3 approaches/lifetime-objective/rollout-veto-17k/finalize.py \
  approaches/lifetime-objective/rollout-veto-17k/runs/screen32-17k

# 7,000-band ablation arm (reuses the comparator arm above)
./build/lifetime/rollout-veto-17k/veto --run \
  --seed-start 0xa51e0000 --games 32 --threads 8 --max-moves 600 \
  --horizon 25 --root-q-loss 7000 --candidate-only \
  --output approaches/lifetime-objective/rollout-veto-17k/runs/screen32-7kband.json

# veto-condition diagnostic (probe seeds only)
./build/lifetime/rollout-veto-17k/veto --veto-diagnostic 0xa51e3f30 30 25
```

Source hashes of everything the build depends on are written to
`build/lifetime/rollout-veto-17k/sources.sha256` on every build.

### Artifacts

| Path | Contents |
| --- | --- |
| `approaches/lifetime-objective/rollout-veto-17k/veto.cpp` | the ported policy, harness wiring, self-test, parity dump and diagnostic |
| `approaches/lifetime-objective/rollout-veto-17k/parity-original.cpp` | digest program linked against the frozen source (assert patched in the build tree only) |
| `approaches/lifetime-objective/rollout-veto-17k/build.sh` | clang++ build; carries the complete port change list; refuses to build the parity reference if more than one line differs from the frozen source |
| `approaches/lifetime-objective/rollout-veto-17k/PREREGISTRATION.md` | the gate, frozen before any cohort seed was read |
| `approaches/lifetime-objective/rollout-veto-17k/report.py`, `finalize.py` | read-only cohort table and gate evaluation |
| `.../runs/screen32-17k.json` + `-baseline.json` + `-candidate.json` | the 32-game paired SCREEN cohort |
| `.../runs/screen32-7kband.json` + `-candidate.json` | the 7,000-band ablation arm |
| `.../runs/veto-conditions.csv` | per-alternative veto-condition diagnostic (§3A) |
| `.../runs/probe-h25.json`, `probe-d4.json` | runtime probes (§3) |

Seeds consumed by this work, now permanently development data:
`0xa51e0000`–`0xa51e001f` (cohort) and `0xa51e3f00`, `0xa51e3f10`,
`0xa51e3f20`, `0xa51e3f30` (probes). Everything else in the lease
`0xa51e0000`–`0xa51e3fff` is unread.

### Note on the exploratory index

`docs/exploratory/README.md` is an existing repository file and was **not**
modified, so this finding does not yet appear in its contents table. Adding the
row is a one-line change for whoever owns that file.
