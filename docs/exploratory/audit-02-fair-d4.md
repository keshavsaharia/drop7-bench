# Audit 02 — the fair depth-4 reference policy

Independent, read-only audit of the policy every other result in this repository
is measured against. Performed at
`HEAD = ac7f04e3d7d6243c2e2831b433ae9c4d599d7b28`.

## Scope

The completed full-width fair depth-4 expectimax comparator
(`fair-d4-current-source-v1`) and the exact source closure that produces it:

| File | Lines | sha256 | Matches `baselines-v1.json`? |
| --- | ---: | --- | --- |
| `approaches/fair-expectimax/reference/fair-only-depth4.cpp` | 926 | `45e7c223…f97cf` | yes |
| `approaches/fair-expectimax/reference/fair-only-horizon.cpp` | 1038 | `8828379b…fee1a` | yes |
| `src/core/native/public-behavior.hpp` | 1051 | `8b4267af…8c89e` | yes |
| `src/core/native/engine.hpp` | 372 | `b6dcde5f…3090` | yes |
| `research/benchmarks/baselines-v1.json` | — | — | audited as the claim of record |

All four sources are clean at `HEAD`; all four hashes reproduce the manifest's
`sourceClosure` exactly. Working-tree state is disclosed: `git status --porcelain`
shows one untracked directory, `approaches/lifetime-objective/` (plus
`build/lifetime`), belonging to another contributor. It was not read, built, or
modified, and is not part of this audit.

Not in scope: engine fidelity (covered by
[audit-01](audit-01-engine-fidelity.md)), ledger claim arithmetic in general
(covered by [audit-03](audit-03-claim-arithmetic.md)), attribution records. **No
gameplay cohort was run. No protected or final seed was opened. No repository
file other than this report was created or modified.**

## Method

1. Read all four files completely, line by line, plus
   `approaches/ntuple-rl/regenerative-expert-iteration/regenerative-expert-iteration.cpp`
   (lines 1–3, 82, 3170–3280) to establish provenance of the headline 64-game
   figure, and `docs/research/history.md:955–985, 3327–3347, 4234–4245`.
2. Reproduced the build. `make native CXX=clang++` reported nothing to do
   against a pre-existing `build/`, so the binary was rebuilt independently in a
   scratch directory with the Makefile's exact flags
   (`-O3 -std=c++20 -pthread -Wall -Wextra -Werror`). AMD clang 23.0.0git.
   **The independent rebuild is byte-identical to the committed `build/fair-depth4`:
   sha256 `9066026462967a5f0900ad8151bf841fff0966a82aecd67f663bc5541c10f2d6`.**
3. Ran `./build/fair-depth4 --self-test` (verbatim output in §2) and the same
   self-test on the independent rebuild — identical output, exit 0, 4.195 s
   single-threaded.
4. Wrote four disposable probes **outside the repository** that link
   `fair-only-depth4.cpp` as a library and exercise it on synthetic,
   hand-constructed boards only (no game seed, no cohort, no `--run`):
   chance-sample coverage, leaf-value scale, root-value spread, risk behaviour
   on crowded boards, leaf term decomposition, and a leaf micro-benchmark.
5. Recomputed the score decomposition arithmetic from `engine.hpp` constants and
   from the exact rise counts derivable via
   [audit-03's](audit-03-claim-arithmetic.md) 7,000↔17,000 replay identity.

Machine profile for every timing below: AMD RYZEN AI MAX+ 395, 32 logical CPUs,
131,167,724 kB RAM, Linux 6.18.35. Non-exclusive; timings are indicative only.

---

## 1. What the policy actually is

`chooseDepth4Action` (`fair-only-depth4.cpp:243–289`) is iterative-deepening
full-width expectimax over the *canonical* (reflection-normalised) public state:

- **Root:** `rootDecision` (`:210–227`) evaluates **every** legal column in
  `kColumnOrder = {3,2,4,1,5,0,6}` (`public-behavior.hpp:573`). No pruning, no
  move ordering cutoff, no width limit. **Full width at the root: confirmed.**
- **Interior:** `bestFutureValue` (`:178–201`) does the same at every node.
  Full width everywhere, not just the root.
- **Depth:** `evaluateAction(state, c, d)` applies one move and recurses to
  `bestFutureValue(next, d-1)`; `d == 0` returns the leaf. So `d = 4` applies
  decisions at plies 4, 3, 2, 1. **Four decision plies: confirmed.**
- **Chance:** 5 stratified samples per action node (`:137–162`).
- **Iterative deepening:** depths 1→4 in one shared `SearchContext`
  (`:251–260`). The transposition cache key includes the *remaining* depth
  (`public-behavior.hpp:744–754`), so an entry written during the depth-3
  iteration is a true 2-ply value and is validly reused by the depth-4
  iteration. **No stale-depth bug.**
- **Leaf:** `frozen::fairLeaf` (`fair-only-horizon.cpp:135–162`) — *not*
  `cfpi::detail::phaseUtility`. The two evaluators share the feature extractor
  but have different weights and different terms; only `fairLeaf` is the
  comparator's leaf.

### Search value units

This is the single most important structural fact, and it is easy to miss:

```
value(state, d) = E_samples[ score_delta  +  value(child, d-1) ]      (:161)
value(state, 0) = fairLeaf(state)                                     (:171)
value(terminal) = kTerminalUtility = -1,000,000                       (:182, :226)
```

Real game score and leaf heuristic points are **added directly**, with
`kImmediateScoreWeight = 1.0` (`fair-only-horizon.cpp:51`). **One leaf point is
therefore defined to be exactly one point of game score.** Every exchange-rate
claim below follows from that identity, not from an assumption.

---

## 2. Self-test output (verbatim)

`./build/fair-depth4 --self-test`, exit code 0:

```
{"deterministic":true,"legal":true,"greedy_legal":true,"mirror_safe":true,"potential_mirror_safe":true,"telescoping":true,"completed_depth":2,"passed":true}
FAIR_ONLY_HORIZON_SELF_TEST {"passed":true,"typescriptFixtureParity":true,"fixtureCount":3,"maximumLeafError":0,"maximumRootError":1.81898940355e-12,"maximumExpectedScoreError":0,"reflectionSafe":true,"publicStateOnly":true,"terminalSafe":true,"phaseResidualIncluded":false,"levelBonus":17000}
FAIR_ONLY_DEPTH4_SELF_TEST {"passed":true,"frozenLeafTest":true,"typescriptD4Parity":true,"maximumRootError":3.63797880709e-12,"completedDepth":4,"depth3Action":1,"depth4Action":4,"actionSwitched":true,"work":1877470,"cacheEntries":27360,"deterministic":true,"reflectionSafe":true,"publicStateOnly":true,"completionProven":true,"worstCaseWork":3134950,"worstCaseCache":45430,"levelBonus":17000}
```

All three suites pass, on both the committed binary and the independent rebuild.

---

## 3. Leaf evaluation: every term and its weight

`fairLeaf` (`fair-only-horizon.cpp:135–162`) is a linear dot product over 19
features. Weights are `fair-only-horizon.cpp:53–71`; feature semantics are in
`public-behavior.hpp`.

Magnitudes in the last two columns are from probe 4: 30,000 synthetic mid-game
boards (14–36 discs, random legal drops). **Caveat: the generator never creates
cracked (`9`) cells and only ever has the seven initial solid cells, so
`cracked_cells`, `cracked_exposure` and `covered_height_risk` are
under-represented relative to real play.** Treat the shares as indicative
ordering, not calibration.

| # | Feature | Weight | Source (weight / feature) | Semantics | mean contrib | share of Σ&#124;contrib&#124; |
| --: | --- | ---: | --- | --- | ---: | ---: |
| 1 | `open_columns` | **+180** | `:53` / `pb:281` | legal columns | +1,065 | 2.1% |
| 2 | `height_load` | **−20** | `:54` / `pb:293` | Σ elevation² over occupied cells | −7,341 | 14.3% |
| 3 | `solid_cells` | **−620** | `:55` / `pb:296` | count of `8` | −4,340 | 8.5% |
| 4 | `cracked_cells` | **−220** | `:56` / `pb:297` | count of `9` | 0 * | 0% * |
| 5 | `numbered_cells` | **−18** | `:57` / `pb:306` | count of 1–7 | −426 | 0.8% |
| 6 | `high_low_numbers` | **−90** | `:58` / `pb:307` | value ≤ 2 at elevation ≥ 5 | −162 | 0.3% |
| 7 | `direct_potential` | **+1600** | `:59` / `pb:327` | Σ per-disc readiness `2^(1−cost)` to pop by one addition | **+8,868** | **17.3%** |
| 8 | `latent_chain_potential` | **+700** | `:60` / `pb:356` | Σ per-disc readiness to pop by *release* (neighbours popping first) | +2,579 | 5.0% |
| 9 | `cracked_exposure` | **+100** | `:61` / `pb:460` | P(at least one neighbour of a cracked disc pops) | 0 * | 0% * |
| 10 | `solid_exposure` | **+40** | `:62` / `pb:462` | 0.35·best + 0.65·second-best neighbour readiness (two hits needed) | +34 | 0.1% |
| 11 | `adjacent_ones` | **−550** | `:63` / `pb:374` | trapped adjacent `1`s (each blocks the other) | −340 | 0.7% |
| 12 | `triple_twos` | **−750** | `:64` / `pb:407` | excess² of runs of `2` longer than 2 | −38 | 0.1% |
| 13 | `dead_low_numbers` | **−120** | `:65` / `pb:359` | low discs already over-length in both axes with no escape | −547 | 1.1% |
| 14 | `covered_height_risk` | **−95** | `:66` / `foh:116-120` | Σ elevation²·edge(1.65)·(1.0 solid / 0.72 cracked) | −789 * | 1.5% * |
| 15 | `low_number_height_risk` | **−85** | `:67` / `foh:122-123` | Σ (elevation−2)⁺² over `1`/`2` discs | −2,734 | 5.3% |
| 16 | `danger_height_squared` | **−1250** | `:68` / `foh:130-131` | (max column height − 4)⁺² | **−8,352** | **16.3%** |
| 17 | `roughness` | **0.0** | `:69` / `foh:127-129` | Σ &#124;Δ height&#124; — **computed then multiplied by zero** | 0 | 0% |
| 18 | `rise_pressure` | **−35** | `:70` / `foh:102-104` | Σ height³ ÷ `moves_remaining` | **−13,537** | **26.4%** |
| 19 | `next_disc_vertical_options` | **+220** | `:71` / `foh:105-107` | columns where height+1 == next disc | +186 | 0.4% |

Declared but inert:

| Constant | Line | Status |
| --- | --- | --- |
| `kImmediateScoreWeight = 1.0` | `:51` | Not a leaf term. It is the implicit coefficient on `score_delta` in the search (`fod4:161`). |
| `kRevealedCoverWeight = 300.0` | `:52` | Declared, `static_assert`ed at `:75`, **never used**. The comment at `:48–50` says so explicitly. |
| `kFairTerminalUtility = −2,500,000` | `:37`, used `:136` | **Unreachable from the search.** `bestFutureValue` returns `kTerminalUtility` for `game_over` *before* calling the leaf (`fod4:182–183`). Only the self-test at `foh:932` reaches it. |

`fairLeaf` uses 13 of the 24 fields of `PhaseFeatures` (`pb:81–106`). **Eleven
are computed at every leaf and discarded:** `projected_occupancy_debt`,
`residual_cover_debt`, `cover_altitude_debt`, `imminent_cover_altitude_debt`,
`peak_height_risk`, `low_cap_load`, `adjacent_low_cap_load`,
`quiet_build_options`, `quiet_direct_gain`, `trigger_readiness`,
`rise_trigger_readiness`. See M5 and H4 — this is both a performance finding and
a substantive one, because the discarded set is precisely the rise-readiness set.

Measured leaf scale (probe 2, 20,000 random boards):
`min −86,080 · mean −15,393 · max +10,067`. Frozen parity fixtures
(`foh:839–861`): `initial −4,057.5`, `manual −548.87`, `walked −23,504.1`.

---

## 4. Is D4 optimizing the right objective?

### 4.1 The score identity

From `engine.hpp` (independently verified, and consistent with audit-03 §0):

| Award | Line | Value |
| --- | --- | ---: |
| Row rise (`kLevelBonus`) | `:21` | **flat 17,000**, once per successful rise |
| Board clear (`kClearBonus`) | `:22` | 70,000 |
| Chain wave depth `d` | `:202–206` | `popper_count · floor(7·d^2.5)` |

Verified by execution: `7, 39, 109, 224, 391, 617` for `d = 1..6`.

A rise is awarded only when `moves_remaining` reaches 0 **and**
`raiseCoveredRow` succeeds (`engine.hpp:302–324`); a failed rise ends the game
with no award. Therefore `moves/5 − 1 ≤ rises ≤ floor(moves/5)`.

### 4.2 Decomposition of the recorded means

**The 8-game cohort has an *exact* rise count.** Audit-03 shows the corrected
replay changed score by exactly `10,000 × rises` on identical trajectories
(history.md:962/970 vs 3335/3337): confirmation D4 = 223,750 ⇒ **22.375 mean
rises**.

| Cohort | Mean score | Mean moves | Rises | Level bonus | Level share | Chains + clears |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| D4 8-game `0x3e9c0000…07` | 400,675.25 | 116.375 | **22.375 (exact)** | 380,375.0 | **94.93%** | 20,300.2 (5.07%) |
| D4 64-game `0x3da40000…3f` | 308,295.578 | 90.031 | 17.081 (calibrated) | 290,380.4 | **94.19%** | 17,915.2 (5.81%) |
| D4 64-game — upper bound | " | " | 18.006 | 306,105.4 | 99.29% | 2,190.2 (0.71%) |
| D4 64-game — lower bound | " | " | 17.006 | 289,105.4 | 93.78% | 19,190.2 (6.22%) |

**Correction to the brief.** The `18.006 × 17,000 = 306,102` (99.3%) figure
assumes `rises = moves/5`, i.e. that every started level completes its rise.
It does not: the modal termination is a *failed* rise on the fifth move of a
level, which costs exactly one rise. The 8-game cohort proves this empirically
(22.375 actual vs 23.275 for `moves/5`). The defensible statement is:

> **Level bonuses are 94–99% of fair D4's mean score, best estimate ~94–95%.
> Chain waves and board clears together are 1–6%, best estimate ~5%.**

The qualitative conclusion is unchanged and, if anything, sharpened: chains
are a rounding error on the score, but a slightly larger rounding error than
the brief assumed. (From the ledger alone the residual is *chains plus board
clears* and this audit cannot separate them.)

**Independent corroboration.** A concurrently produced exploratory measurement,
[`finding-01-score-is-survival.md`](finding-01-score-is-survival.md), instruments
a live 64-game fair-D4 cohort and reports **94.29% row-rise bonus, 5.71% chain
waves, 0.00% board clears** at 94.06 mean moves. That is within 0.1 pp of the
94.19% this audit derived independently from the ledger and the audit-03 rise
calibration, and it resolves the ambiguity above: the residual is chain waves,
not board clears. It also independently reproduces the ≈294-move requirement in
§4.3. Two independent routes, one from recorded aggregates and one from
instrumented gameplay, agree.

### 4.3 What a one-million-point mean actually requires

| Route | Requirement | Multiple of current |
| --- | --- | --- |
| Survival, level share 1.00 | 58.82 rises ⇒ **≈ 299 mean moves** | **3.32×** the 64-game 90.031 |
| Survival, level share 0.949 | 55.84 rises ⇒ **≈ 284 mean moves** | 3.15× |
| Chains alone at the observed ~175 pts/move | ≈ 5,733 moves | absurd |
| Board clears alone | 14.3 full board clears per game | absurd |

**Mean lifetime is the only lever with the right order of magnitude, and it
must roughly triple.** Any candidate whose mechanism is "deeper chains" is
optimizing a channel that carries ~5% of the score.

### 4.4 So is the leaf aligned?

**Directionally yes; structurally no.**

*Aligned.* The leaf is overwhelmingly a survival/crowding evaluator, which is
what the score decomposition says to optimize. Roughly 76% of measured leaf
magnitude sits in height, rise-pressure, cover and low-disc penalties (#2, 3,
14, 15, 16, 18) and only ~23% in chain-potential rewards (#7–10, 19). Critically,
**there is no chain-depth term anywhere in the leaf.** Given 391 points for a
depth-5 wave against 17,000 for a rise, that omission is *correct*.

*Not aligned — and this is the real critique.* The chain-potential terms are not
mis-weighted score proxies; they are survival instruments (clearing is the only
way mass leaves the board) priced at ~100–1,000× their score value. A single
extra trigger is worth up to +1,600 leaf points (`direct_potential`, #7) while
the disc it pops is worth 7 real points. That pricing is defensible. The genuine
structural problems are:

1. **The true score function has essentially no influence on action ranking.**
   Within a node, all siblings share `moves_remaining`, so the +17,000 rise
   bonus is an identical constant added to every surviving sibling and cancels
   exactly. Chain points (7–391) are 2–4 orders of magnitude below the leaf
   spread. Measured: on all three frozen parity fixtures the chosen action's
   mean immediate score is **exactly 0.00** (probe 3). The search is, in
   practice, a 4-ply minimax of a hand-tuned potential with a death penalty —
   the score function is nearly decorative. This is *fine* only because the
   potential happens to be survival-shaped; nothing enforces that.

2. **The leaf is not an estimate of anything.** It is not calibrated to expected
   remaining lifetime or expected remaining score. Beyond 4 plies, it is the
   *only* carrier of survival information, and `argmax leaf` is not guaranteed
   to be `argmax expected lifetime`. Every strength claim about D4 rests on an
   uncalibrated potential.

3. **The leaf discards exactly the rise-awareness features it needs (H4).**

**Verdict on the objective question:** D4 optimizes a *reasonable proxy for* the
right objective (survival), by accident of its weight vector rather than by
construction. It does not spend meaningful weight on score-worthless
chain-depth building. But it also has no mechanism that can plausibly triple
mean lifetime, and its 4-ply horizon plus uncalibrated potential are the two
places where that mechanism would have to live.

---

## 5. Terminal utility and implicit risk attitude

`terminalUtility = −1,000,000` (`baselines-v1.json` `algorithm.terminalUtility`;
`fair-only-horizon.cpp:36`; `static_assert`ed at `fair-only-depth4.cpp:86`).

Because leaf points *are* score points (§1), the exchange rate is exact:

| Quantity | In score points | Equivalent |
| --- | ---: | --- |
| One row rise | 17,000 | 5 moves of survival |
| `terminalUtility` | −1,000,000 | **58.82 rises = 294.1 moves of survival** |
| " as a fraction of the whole recorded game | — | **3.24× the entire 64-game mean of 308,295.578** |
| One death in one of five samples | −200,000 | **11.76 rises = 58.8 moves** |
| Measured spread across legal siblings, D4 root, `initial` fixture | 217.2 | — |
| " `manual` fixture | 2,133.4 | — |
| " `walked` fixture | 9,745.1 | — |
| " mean over non-death siblings, 60 crowded synthetic roots | 10,924 | — |
| " mean full spread on roots that contain a death sibling | 862,720 | **79× the non-death spread** |

**A single sampled death costs 18×–920× the entire legitimate discrimination
range between actions.** The decision rule is therefore effectively
**lexicographic**: minimise the 5-sample estimate of death within 4 plies first,
and only then maximise the leaf.

**In units of real score the search is strongly risk-averse.** A risk-neutral
expected-score maximizer's terminal utility should equal −(expected remaining
score), which from the start of a 308,296-point game is ≈ −308,000 and less
thereafter. −1,000,000 over-prices death by ≳3.2× at move 0 and by far more
later.

Two qualifications:

- The over-pricing has **little effect on the score-versus-survival trade-off**,
  because the maximum in-horizon score that could be traded against death is one
  rise (17,000) plus negligible chain points. Even a correctly calibrated
  −308,000 would refuse the trade.
- Its real effect is on the **death-probability-versus-structure trade-off**: at
  −1,000,000 the search will accept an arbitrarily worse board (leaf spread
  ~10,000) to remove a 20% modeled 4-ply death (−200,000). *Speculation, flagged
  as such:* combined with the biased 5-of-7 chance estimate (H1), this is a
  plausible mechanism for myopic self-burial — escaping now, worsening
  structure, and dying around move 90 rather than building for a longer game.
  This is the single cheapest high-information ablation available: sweep
  `terminalUtility ∈ {−150k, −308k, −1M, −3M}` at fixed everything else on
  already-consumed development seeds. It is one constant and changes no other
  semantics.

**Secondary finding (M2): three mutually inconsistent terminal constants exist
in the same source closure** — `−250,000` (`public-behavior.hpp:516`, the
`phaseUtility` game-over return), `−1,000,000` (the search), `−2,500,000`
(`fair-only-horizon.cpp:37`). The latter two are unreachable in D4's search
path, but `phasePotential()` (`public-behavior.hpp:904–906`) is a public entry
point that other approaches call and it carries the `−250,000` value. Any
consumer that mixes `phasePotential` with search values is silently using a
death penalty 4× smaller.

**M4: there is no death-depth shaping.** `bestFutureValue` returns the same
`−1,000,000` whether the game ends at ply 1 or ply 4 (`fair-only-depth4.cpp:182`).
The only gradient toward delaying death is the accumulated `score_delta`, at most
≈17,000 = **1.7% of the terminal penalty**. On a root where every sibling dies
within the horizon, the values are near-flat and the choice falls through to
`kColumnOrder` tie-breaking. That ratio is arithmetically *correct* in score
units — but it means the search has almost no gradient for the thing it must
actually maximize.

---

## 6. Chance nodes: what the "5 deterministic stratified samples" are

### 6.1 Mechanism

`stratifiedUnit(seed, sample, count, domain, event)`
(`public-behavior.hpp:579–593`):

```
event_seed = mix32(seed ^ domain ^ (event+1)*0x85ebca6b)
rotation   = event_seed % count                       // fixed per (state, depth, event)
stratum    = (sample + rotation) % count              // bijection over sample=0..count-1
jitter     = mix32(event_seed ^ (sample+1)*0x9e3779b9) / 2^32
return (stratum + jitter) / count
```

Two consumers:

- **Reveal values** — `StratifiedRandom::nextDisc` (`:601–607`), domain
  `kRevealSampleDomain`, with an `event` counter that advances once per revealed
  cover inside the cascade (`:655–657`).
- **The successor's visible next disc** — `sampledNextDisc` (`:736–742`), domain
  `kDiscSampleDomain`, **always `event = 0`**. Note that
  `playMoveSampled` also draws a next disc internally (`:711–712`), but
  `evaluateAction` immediately overwrites it (`fair-only-depth4.cpp:156–157`).
  This is deliberate and correct: it makes the successor's disc independent of
  how many reveals a particular column's cascade happened to consume.

`state_seed = scenarioSeedForState(state, policy_seed, depth)`
(`public-behavior.hpp:721–734`) hashes **board, next_disc, moves_remaining,
policy_seed, depth** — and **not the column**.

### 6.2 Are the samples probability-matched? Are they weighted correctly?

- **Weighting:** each sample contributes `1/count` (`fair-only-depth4.cpp:163`).
  Since `rotation` is fixed per event and `stratum = (sample+rotation) % count`
  is a bijection, the five samples hit strata 0..4 **exactly once each**. On the
  latent uniform variate the quadrature is exactly probability-matched with
  equal weights. That part is right.
- **Sibling reuse (common random numbers):** because `state_seed` is
  column-independent, all siblings at a node see the same unit values for
  event *k*. This is textbook CRN. **It reduces comparison variance and does not
  bias the expectation of the difference.** The brief's worry about sibling
  reuse being the bias source is not borne out — reuse is the good part.

### 6.3 Where the bias actually is (finding H1)

The disc distribution has **7 equiprobable atoms**; the stratification has
**5 strata**. `floor(unit·7)+1` maps 5 strata of width 1/5 onto 7 atoms of width
1/7, and **5 ∤ 7**. Consequences, all measured:

- **Marginal over states is unbiased.** Probe 1, 4,000,000 draws across 200,000
  synthetic states × 4 depths: per-disc frequencies `0.142830, 0.142916,
  0.142844, 0.142771, 0.143003, 0.142846, 0.142790` against 1/7 = 0.142857.
  Error < 1.5e-4. **The estimator is unbiased in aggregate.**
- **Every individual node is conditionally biased.** Distinct disc values among
  a node's five samples: **5 distinct on 62.65% of nodes, 4 on 33.86%, 3 on
  3.49%**. Mean = 4.59, so **on average 2.41 of the 7 possible next discs
  (≈34% of the true probability mass) are assigned probability zero at every
  node**, while the sampled values are inflated from 1/7 to 1/5 or 2/5.
- **This bias does not average out within a decision.** It is deterministic
  given the public state, so re-searching cannot wash it out.
- **Common-mode cancellation is partial, not total.** All siblings see the same
  distorted distribution, so much of the distortion cancels in the ranking. But
  the *consequences* of a missing disc are column-specific: a column that only
  dies on a `2` looks perfectly safe at a node where `2` was not sampled. Given
  §5's lexicographic risk rule, a missed lethal disc is exactly the error mode
  that flips a decision.

**Fix and cost.** Setting `chance_samples = 7` makes each stratum
`[k/7,(k+1)/7)` map to exactly one disc, converting the next-disc chance node
from a 5-point sample into **exact enumeration with the true 1/7 weights**.
`validateOptions` already permits up to 32 (`public-behavior.hpp:779`). Worst-case
work rises from 3,134,950 to ≈11,892,398 (**3.79×**, branches 35→49). A later
experiment in this repository independently reached the same conclusion:
`regenerative-expert-iteration.cpp:50` sets `kChanceStrata = 7` with a comment
about exact permutations of discs 1..7.

### 6.4 Row rise inside the search

Handled correctly. `playMoveSampled` (`public-behavior.hpp:665–719`) mirrors
`playMove` exactly: decrement `moves_remaining`; at 0, attempt
`raiseCoveredRow`; on failure set `game_over`; on success award `kLevelBonus`,
continue the cascade at `waves.back().depth + 1`, and re-check the board clear.
`moves_remaining` is carried in `State`, is part of both the cache key
(`:751`) and the scenario seed (`:731`), and survives canonicalisation
(`:560–571`). **The rise fires at the correct move offsets inside the search.**

---

## 7. Findings by severity

No **Critical** findings. Nothing invalidates the comparator as a legal,
deterministic, bounded, full-width 4-ply policy.

### High

**H1 — Five chance strata cannot represent a seven-atom distribution.**
`public-behavior.hpp:579–607, 736–742`; `fair-only-horizon.cpp:33`;
`fair-only-depth4.cpp:36`. On average 2.41 of 7 next-disc values (≈34% of
probability mass) receive **zero** weight at every node; sampled values are
inflated to 1/5 or 2/5 from 1/7. Unbiased in the marginal over states (measured
to 1.5e-4), conditionally biased at every node, and not removable by re-search.
Interacts multiplicatively with H2: the estimator most likely to be wrong is
the death estimate, which the value function treats lexicographically. Fix:
`chance_samples = 7`, at 3.79× worst-case work. **This is the highest-value
bounded correction to fair D4 identified by this audit.**

**H2 — `terminalUtility = −1,000,000` is uncalibrated and makes the search
lexicographic.** `fair-only-horizon.cpp:36`; used `fair-only-depth4.cpp:182,
220, 226, 272`. Equal to 58.82 row rises, 294.1 moves of survival, and 3.24× the
entire recorded 64-game mean. A single death among five samples (−200,000) is
18×–920× the measured spread across non-death siblings (217–10,924). The search
therefore minimises modeled 4-ply death probability first and maximises the leaf
second. Risk-averse in real-score units by ≳3.2×. See §5 for the recommended
one-constant ablation.

**H3 — The comparator manifest's headline strength evidence omits required
cohort fields, and was not produced by the standalone binary.**
`research/benchmarks/baselines-v1.json` `strengthEvidence` reports
`broadDevelopmentGames: 64, broadMeanScore: 308295.578` with no seed range, no
data role, and no move cap. Traced: it comes from
`docs/research/history.md:4234–4235`, i.e. the D4 bootstrap inside
`approaches/ntuple-rl/regenerative-expert-iteration/regenerative-expert-iteration.cpp`
on lane `kD4InitializationLane{0x3da40000, 0x3da4003f}` (`:82`, `:3254–3277`),
under a **2,000**-move cap (`:160`). The standalone binary's own cohorts are
different seeds under a **1,000**-move cap.
*Mitigating:* that file includes `fair-only-depth4.cpp` as a library (`:1–3`)
and calls the identical `drop7::fair_only_depth4::chooseDepth4Action` (`:3177`)
with `playHeadlessMove`, so **the policy really is the pinned one**. The defect
is manifest completeness against `docs/benchmarks.md` ("exact ordered cohort and
its data role"), not policy identity. The 8-game 400,675.25 figure *is* from the
standalone binary's confirmation cohort (`history.md:3337`) and is correctly
attributed. Note also that the manifest's two strength numbers differ by 30%
(400,675 vs 308,296) with no explanation of cohort difference — a reader
comparing against "the D4 baseline" can pick either.

**H4 — The 4-ply horizon misses the rise on 20% of decisions, and the leaf
discards exactly the features that would compensate.** With
`moves_remaining = 5` (the first move of every level), four plies end at
`moves_remaining = 1` and the search **never sees the rise it is preparing for**.
Demonstrated by the frozen fixtures themselves (`fair-only-horizon.cpp:839–861`):
`initial` and `walked` both have `moves_remaining = 5` and root values of
−791…−906 and −20,260…−26,695; `manual` has `moves_remaining = 3` and root values
of +14,251…+16,008 — a clean +17,000 offset. Meanwhile `fairLeaf` drops all
eleven rise-aware `PhaseFeatures`: `rise_trigger_readiness` (poppers that would
fire on the raised board, `pb:504–511`), `imminent_cover_altitude_debt`,
`projected_occupancy_debt`, `residual_cover_debt`, `peak_height_risk`,
`low_cap_load`, `adjacent_low_cap_load`, `quiet_*`, `trigger_readiness`,
`cover_altitude_debt`. At `moves_remaining = 5` the policy's **entire**
rise-awareness is the single term `−35 · Σheight³ / 5`. Given §4.3 (lifetime
must triple), rise survivability is the objective, and this is where the policy
is blindest.

### Medium

**M1 — The `--run` decision gate has no uncertainty control and the computed
interval is not used.** `fair-only-depth4.cpp:576–579` (`improvesBothMeans`) is a
bare comparison of two means on **n = 4** (screen) then **n = 8**
(confirmation), and the screen gates the confirmation (`:862`). `differences()`
(`:525–544`) does compute a lower bound but with the **normal** quantile 1.96 at
n = 4 and n = 8, where `docs/benchmarks.md` requires bootstrap or Student-t
(t₀.₉₅,₃ = 2.353, t₀.₉₅,₇ = 1.895). It is anti-conservative at n = 4 — and it is
**never consulted by the gate** (`:855`, `:869`). The recorded pass is therefore
a two-mean comparison on eight games.

**M2 — Three inconsistent terminal constants.** −250,000
(`public-behavior.hpp:516`), −1,000,000 (`fair-only-horizon.cpp:36`), −2,500,000
(`fair-only-horizon.cpp:37`, applied `:136`). The last two are unreachable in
D4's search path, but `phasePotential()` (`pb:904`) exports the −250,000 variant
to other approaches. See §5.

**M3 — Non-contract move cap.** `kMaximumMoves = 1'000`
(`fair-only-depth4.cpp:43`), against the 2,000-move cap in
`docs/methodology.md` and `docs/benchmarks.md`. Immaterial for the recorded runs
(neither cohort censored, `history.md:978`), but it means the standalone binary
and the 64-game bootstrap ran under **different censor rules** — which matters
directly at the ~284–299 mean moves a million-point policy would need.

**M4 — No death-depth shaping.** `fair-only-depth4.cpp:182`. Dying at ply 1 and
at ply 4 both cost −1,000,000; the only differentiator is ≤17,000 of accumulated
score = 1.7% of the penalty. Roots where all siblings die are near-value-flat
and fall through to `kColumnOrder` tie-breaking. See §5.

**M5 — 11 of 24 leaf features are computed and thrown away; one weight is
literally zero.** `public-behavior.hpp:270–513` vs `fair-only-horizon.cpp:141–160`.
Measured (probe 3, 2,000 synthetic boards × 400 reps): `fairLeaf` costs
4,193.6 ns/call, of which `extractPhaseFeatures` is 4,056.0 ns (96.7%). Two
isolable dead blocks — `placementInventory` (439.1 ns, `pb:209–268`, feeding only
`trigger_readiness`/`quiet_*`) and `raiseCoveredRow`+`findPoppers`
(116.0 ns, `pb:504–511`, feeding only `rise_trigger_readiness`) — account for
**≥13.2% of leaf time**, and the leaf runs ~700k–1.5M times per decision.
Separately, `kRoughnessWeight = 0.0` (`:69`) multiplies a feature that is
computed every leaf (`:127–129`). Removing the dead work is a pure speedup; note
that under `docs/benchmarks.md` even a pure speedup must be shown not to change
the selected column. (H4 argues the better fix is to *use* these features, not
delete them.)

### Low

**L1 — `make native` fails out of the box; `CXX ?= clang++` is inert.**
`Makefile:1`. GNU make's built-in `CXX = g++` has origin `default`, and `?=`
only assigns when origin is `undefined`. Proved directly:

```
$ make -f <(printf 'CXX ?= clang++\nall:\n\t@echo CXX=$(CXX) origin=$(origin CXX)\n')
CXX=g++ origin=default
```

g++ 14.2.0 then fails the build with
`error: array subscript [8, 576460752303423487] is outside array bounds of 'std::array<double, 4> [1]' [-Werror=array-bounds=]` at
`public-behavior.hpp:442` / `:453`. **This is a compiler false positive** — the
loop at `pb:444–452` increments `count` at most once per each of four directions,
so `count ≤ 4` is provable by inspection. But with `-Werror` the repository is
**not buildable by its own documented command on a default GNU make + GCC
host**, which is a reproducibility defect for a research baseline. Two
independent fixes: `CXX := clang++` (or `override`), and/or hoist the `std::sort`
bound so GCC can see `count ≤ 4`.

**L2 — Shared `/tmp` default output.** `fair-only-depth4.cpp:687` defaults to
`/tmp/drop7-fair-only-depth4.json`, against `AGENTS.md` ("Do not introduce new
shared `/tmp` defaults"). Two concurrent agents would silently collide.

**L3 — Per-game peak RSS is process-wide and non-deterministic.**
`peakRssBytes()` (`:291–299`) uses `RUSAGE_SELF`, but is stored per game
(`:367`, `:405`) while four games run concurrently (`:427`). The reported
`peakRssBytes` per game is a whole-process high-water mark at an arbitrary
moment, not that game's footprint, and depends on scheduling.

**L4 — Hardcoded `kParallelism = 4`** (`:44`) on a 32-logical-CPU host; the
search itself is single-threaded. Already listed in the manifest's
`limitations`.

**L5 — `#define main` + whole-`.cpp` include.** `fair-only-depth4.cpp:1–3`
renames `fair-only-horizon.cpp`'s `main` to
`drop7_fair_only_horizon_frozen_entrypoint` and compiles it in. The D3 program's
cohorts are therefore **linked into `build/fair-depth4` but unreachable**
(§8). Fragile against ODR and against any future edit to the D3 file's
`#ifndef` guards.

**L6 — Domain-constant collision.** `kRevealSampleDomain = 0x5245564c`
(`public-behavior.hpp:574`) is bit-identical to `drop7::kRevealDomain`
(`engine.hpp:24`). **Not a leak** — the search's seeds derive from the board hash
and never from the game seed, so no aliasing can expose real reveals — but it
defeats the purpose of declaring separate domains and should be changed if only
to keep the `docs/benchmarks.md` "RNG domain separation" check meaningful.

### Informational — verified positives

These were checked and **pass**; they are recorded so a future agent does not
re-audit them.

**I1 — Full width is real.** No pruning at the root (`:215–216`) or at any
interior node (`:194–196`). Alpha-beta is correctly absent (chance nodes).

**I2 — Four decision plies are real.** Trace in §1. `completedDepth: 4` in the
self-test; `runDepth4Game` **throws** unless `completed_depth == 4` (`:382–384`).

**I3 — The work and cache bounds provably never bind.** `worstCaseIterativeWork(4)
= 3,134,950 < 3,200,000` and `worstCaseIterativeCacheEntries(4) = 45,430 < 60,000`,
`static_assert`ed at `:79–82`. I re-derived both by hand (branches = 7 columns ×
5 samples = 35; Σ_{d=1..4}[Σ_{l=1..d} 35^l + 35^d] = 70 + 2,485 + 87,010 +
3,045,385 = 3,134,950 ✓). Fewer legal columns and cache hits only reduce work,
so the bound is unreachable. Measured: 0 of 60 crowded synthetic roots failed to
complete depth 4; the self-test fixture uses 1,877,470 of 3,200,000.
**Answering the brief's question directly: if the bound *were* hit, behaviour is
not silent degradation.** `WorkLimitReached` is caught only in
`chooseDepth4Action` (`:257`), which abandons the whole depth-4 iteration and
falls back to the completed depth-3 result — a consistent, not partial, answer —
and then `runDepth4Game` **throws** `"fair depth four did not complete"`
(`:382–384`), aborting the cohort. There is no path by which a work-bound hit
silently degrades play.

**I4 — Information boundary is clean.** `extractFairFeatures` validates and reads
only `board`, `next_disc`, `moves_remaining` (`fair-only-horizon.cpp:91–133`).
`canonicalState` zeroes `score` (`pb:564`, `:569`) and `evaluateAction` zeroes it
again on every successor (`fair-only-depth4.cpp:155`). `level` and `moves_played`
propagate inside `State` but are read by **nothing** in the decision path —
neither the leaf, nor `dynamicStateKey` (`pb:744–754`), nor
`scenarioSeedForState` (`pb:721–734`). The game seed never enters the policy at
all; the search uses `playMoveSampled` + `StratifiedRandom`, never
`playHeadlessMove`. The self-test proves it empirically by setting
`score = 8,000,000, level = 73, moves_played = 412` and asserting identical
action, D3 action, and work (`:766–770`, `:800–802`) — `"publicStateOnly":true`.
**The policy is a function of (visible board, visible next disc, moves-until-rise,
terminal flag) only. Proved.**

**I5 — Determinism and reflection.** Deterministic by construction: no RNG state,
no threading inside a decision, all randomness derived from a hash of the public
state plus a fixed `policy_seed = 0xd7075eed`. Self-test asserts identical
action, work, nodes, cache entries, and cache hits on repeat (`:791–795`).
Reflection: `canonicalState` picks the lexicographically smaller of board and
mirror (`pb:554–571`) at the root **and at every successor** (`:159`), and the
self-test asserts `mirrored.action == 6 - first.action` **and
`mirrored.work == first.work`** (`:796–799`) — i.e. the mirrored search is not
merely equivalent, it is the identical computation. The symmetric `initial`
fixture's root values are exactly palindromic (`foh:843–845`). Ties break by
`kColumnOrder` under strict `>`, so they are deterministic too.

**I6 — Cross-engine parity is asserted, not just claimed.** The self-test pins
TypeScript-derived root values to 1e-8 (`fair-only-depth4.cpp:772–787`) and node,
work, cache-entry, and cache-hit counts **exactly**. Observed
`maximumRootError = 3.638e-12`. Three additional D3 fixtures pin leaf values to
1e-9 with observed error **0** (`foh:887–911`).

**I7 — The build is bit-reproducible** with this toolchain: an independent
rebuild is byte-identical to the committed binary (sha256 `9066…f2d6`).

---

## 8. Hardcoded experiment setup and seed consumption

`fair-only-depth4.cpp:39–44`:

```
kScreenSeedStart       = 0x3e9b0000    kScreenGames       = 4
kConfirmationSeedStart = 0x3e9c0000    kConfirmationGames = 8
kMaximumMoves          = 1000
kParallelism           = 4
```

`fair-only-horizon.cpp:39–44` additionally hardcodes `0x3e950000` (8 games) and
`0x3e960000` (16 games).

**Seeds that `./build/fair-depth4 --run` would consume — already-opened
development data:**

| Range | Count | Role |
| --- | ---: | --- |
| `0x3e9b0000`–`0x3e9b0003` | 4 | D4 screen; opened, `history.md:961–972` |
| `0x3e9c0000`–`0x3e9c0007` | 8 | D4 confirmation; opened, `history.md:974–985`, rescored `:3335–3337` |

Each seed is played **twice** per run (D3 arm then D4 arm, `:434–437`), so a
full `--run` is 24 games over 12 seeds.

**Compiled into the binary but unreachable** (the D3 `main` is renamed away by
`fair-only-depth4.cpp:1`): `0x3e950000`–`0x3e950007` and
`0x3e960000`–`0x3e96000f`. A future agent must still treat these as consumed —
`history.md` records them as opened by the D3 experiment.

**Not in this executable, but consumed by the manifest's headline number:**
`0x3da40000`–`0x3da4003f` (64 seeds, H3).

The guard at `:810–816` asserts the seed prefixes are neither `0x7d` nor `0xd7`,
i.e. the protected/final families are structurally excluded. `--run` cannot
reach them. Good.

---

## 9. Verdict

| Axis | Assessment |
| --- | --- |
| **Run validity** | **valid** for everything reproduced here: build, self-test, mechanics probes, hash verification, and the score-decomposition arithmetic. **partial** for `baselines-v1.json` as a benchmark-contract manifest — its headline strength evidence is missing the cohort identity, data role, and move cap that `docs/benchmarks.md` requires (H3). |
| **Scientific outcome** | **pass** on the claim *"fair D4 is a full-width, 4-ply, deterministic, reflection-safe, public-information-only, provably work-bounded expectimax policy with corrected 17,000-point scoring."* Every element independently verified (I1–I7). **inconclusive** on the claim *"fair D4 is the right reference against which to measure progress toward a one-million-point mean"* — it is the strongest **available** reference, but §4 and H1/H2/H4 show its objective is an uncalibrated potential whose alignment with the true objective is accidental. |
| **Evidence tier** | **proposal/mechanics (CHECK)** for this audit's own reproduced evidence. The strength numbers themselves remain **development**, `ledger-recorded`, on already-opened development cohorts, **not independently replicated** — this audit did not and could not rerun them, and a first-party rerun would not count as replication in any case. |

### Recommended next actions, ranked by information gain per unit of compute

1. **`terminalUtility` sweep** (H2, §5). One constant, no semantic change,
   already-consumed development seeds. Highest information for lowest cost.
2. **`chance_samples = 5 → 7`** (H1, §6.3). Removes a measured 34%-of-mass
   conditional bias and converts the next-disc node to exact enumeration.
   Costs 3.79× work — cheap for the class of correction it represents.
3. **Restore the rise-aware leaf features** (H4). They are already computed at
   every leaf and thrown away; wiring them into `fairLeaf` costs nothing at
   runtime and is the most direct attack on the one lever (lifetime) that can
   reach a million points.
4. **Complete `baselines-v1.json`** (H3): record `0x3da40000`–`0x3da4003f`, the
   data role, the 2,000-move cap, and the producing binary; or re-derive the
   comparator figure from a single named binary and cohort.
5. Fix `Makefile:1` and the GCC `-Werror` failure (L1) before any claim of
   cross-compiler reproducibility.

Note for whoever acts on 1–3: each is a **new algorithmic candidate** under
`docs/benchmarks.md`, not a tuning of the frozen comparator. The frozen fair-D4
manifest must remain unchanged.

---

*Audited read-only. Source, self-test output, probe results, and arithmetic in
this report were produced in this checkout on 2026-08-20 and can be regenerated
from the commands quoted above. Probe programs were written outside the
repository and are not retained.*
