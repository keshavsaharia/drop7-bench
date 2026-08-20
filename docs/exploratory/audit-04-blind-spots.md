# Audit 04 — Blind spots in the existing Drop7 corpus

**Scope.** An independent, read-only re-reading of every rejection already on the
books. No gameplay cohort was run, no seed was opened, and no existing file was
modified. The question is not "what should we try next"; it is **"which of the
things we already tried were rejected for a reason that has nothing to do with
the idea being tested?"**

**Status of this document.** Exploratory audit. It is *not* a protocol, a
result, or a promotion. Nothing here upgrades any historical evidence label.
Every rescoring below is arithmetic performed on numbers already printed in
`docs/research/history.md`; none of it is a new measurement.

---

## Method

1. Read `AGENTS.md`, `docs/research/status.md`, `docs/methodology.md`,
   `docs/strategies.md`, `docs/research/roadmap.md`,
   `docs/research/experiment-index.md`, and all 4,607 lines of
   `docs/research/history.md`.
2. Read the scoring and terminal logic in `src/core/native/engine.hpp`
   (lines 21–22, 202–206, 285–333) directly rather than relying on prose.
3. Mechanically cross-referenced all 139 sources under `approaches/` against
   both `experiment-index.md` and `history.md`.
4. Compiled (`clang++ -fsyntax-only -std=c++20`) every source carrying a
   `kLevelBonus` assertion, to separate "recorded under old scoring" from
   "cannot be rebuilt at all".
5. Derived and verified an exact paired-rescoring identity (Section C.2) that
   converts any historical 7,000-point paired result into corrected 17,000-point
   Hardcore units, and applied it to every 7,000-scored rejection.

**Confound codes used in Section A**

| Code | Meaning |
| --- | --- |
| `C1` | Decided under 7,000-point Sequence scoring — wrong by 2.43× on the term that is >93% of a Hardcore score |
| `C2` | Deciding cohort had n ≤ 8 whole games |
| `C3` | Selection and evaluation touched the same cohort |
| `C4` | Runtime/resource budget ended the run before the decisive comparison |
| `C5` | Two or more changes bundled into one test |
| `C6` | Gated on a mis-specified objective (score return where the mechanism targets lifetime) |
| `C7` | Comparator was weaker than fair D4 |
| `C8` | Gate demanded statistical power the design could not deliver |

---

## Section A — Was each rejection clean or confounded?

`Δ₇` and `Δmoves` are the paired deltas exactly as printed in `history.md`.
`Δ₁₇` is the same games rescored to corrected Hardcore units using the identity
proved in C.2. **A sign change in `Δ₁₇` means the recorded rejection is an
artifact of the scoring bug.**

### A.1 The fair-expectimax family (the ablations around the current reference)

| Approach / source | Stated rejection reason | Δ₇ / Δmoves | Δ₁₇ (rescored) | Confound | What a clean retest needs |
| --- | --- | --- | --- | --- | --- |
| **Seven-stratum D4** `chance-strata/fair-depth4-s7.cpp` | "score-neutral to worse, reduced flow, ~3.8× work"; failed fitting on a **−163.6-point** mean | −163.6 / **+2.25** | **+4,336 → SIGN FLIP** | `C1`,`C2` | Port s7 into a new 17k experiment (the source is hard-locked at 7,000 and will not compile). n≥32 paired. Falsify if Δ₁₇ mean ≤ 0 at n=32. Note the rejected run *already* improved lower-tail score (54,922→63,230) and lower-tail survival (42.5→47.5). |
| **Fair-leaf CEM** `cem/fair-cem-optimizer.cpp` | Fresh screen "rejected the candidate": −905.4 points, won 6/8 pairs, one 204,516-point loss erased the wins | −905.4 / **+1.875** | **+2,845 → SIGN FLIP** | `C1`,`C2` | Same: locked at 7,000. Also note its own fitness was `moves + score/14000`, i.e. it was *already* ~a survival objective with the wrong divisor (should be 17,000). Retest at n≥32 with the corrected divisor. |
| **Frozen CEM into D4** `cem/fair-cem-depth4-interaction.cpp` | Heldout regressed 17,835 points / 10.9 moves | −17,835 / −10.9 | −39,711 | `C1` only (no flip) | Clean **given** the CEM vector; but the vector itself was selected under `C1`. Retest is downstream of the CEM retest. |
| **Root CVaR** `root-risk/fair-root-risk.cpp` | Score, survival and flow all regressed hard | −89,708 / −52.6 | −194,958 | `C1`,`C2` | **Clean in direction.** Rescoring makes it worse, not better. One fixed 0.75/0.25 mixture at one scenario count is still a single point in a 2-D space, but the idea is not rescued by the scoring fix. |
| **Full historical action terms** `full-action-terms/full-fair-horizon.cpp` | Placement priors double-count leaf geometry | −40,112 / −23.9 | −87,862 | `C1`,`C5` (whole block restored at once) | **Clean in direction.** The follow-up (`transition-reward-horizon.cpp`) correctly isolated the two dense terms and also failed. |
| **Selective D5 / width 3** `selective-depth/fair-selective-depth.cpp` | Screen lost score, survival and flow | −7,233 / −3.5 | −14,233 | `C1`,`C2`,`C3` (menu fit on 4 games, heldout n=8) | Direction survives rescoring, but the *training-heldout* pass was +13,396/+6.875 → **+27,146 rescored**, and removing one pair flipped it. This is a coin flip at n=8 either way. A clean retest needs n≥32 at 17k. |
| **Cycle-boundary D5** `selective-depth/fair-cycle-boundary-depth5.cpp` | Pilot adverse **and** missed runtime gate | −123,622 / −75 (n=**1**) | −273,622 | `C1`,`C2`(n=1),`C4` | Not a rejection at all — one game. The runtime gate is the real finding. |
| **Full-width D5/s3** `selective-depth/fair-depth5-s3.cpp` | Paused at runtime gate; single pilot unfavourable | −86,519 / −50 (n=**1**) | −186,519 | `C1`,`C2`(n=1),`C4` | Same: one game. D5 has still never had a powered comparison. |
| **Fully completed D4 (old leaf)** `exact-search/exact-depth4.cpp` | "depth alone cannot repair a biased leaf" | −25,449 / −18.75 (n=4) | −62,949 | `C1`,`C2`,`C7` | **Superseded and effectively confirmed** — the later fair-leaf D4 won decisively. The conclusion drawn from it (leaf bias, not depth) is the right one. |
| **Fixed clear reward (+600/clear)** `transition-rewards/fair-clear-reward-confirmation.cpp` | Heldout reversed the fitting gain: −48,954 / −28.6 | (17k engine) | n/a | `C2`,`C3` | The **selection** was made on n=4 games from an *interrupted* run (`fair-phase-energy-release.cpp`, `C4`), where clear-only scored 244,187/156.25 vs stock 143,300/88.75. One coefficient (+600) was then confirmed at n=8 and failed. A clean retest sweeps the coefficient at n≥32; a single point in a 1-D space was rejected, not the hypothesis. |
| **Reveal reward** `transition-rewards/fair-reveal-reward.cpp` | Heldout regressed 7,100 points / 4.25 moves | (17k engine) | n/a | `C2`,`C3` | Selected on n=4, rejected on n=8, both with negative lower bounds. Same structure as above. |
| **Vertical ladder → D4** `vertical-ladder/fair-vertical-ladder-depth4.cpp` | D4 transfer regressed 3,086 points / 1.75 moves at n=8 | (17k engine) | n/a | `C2` | The delta is ~2% of the mean with a −56,964 lower bound: this is noise, recorded as a rejection. Clean retest = same frozen weight 500, n≥32. |
| **Fair D1 rollout improvement** `rollout-improvement/fair-d1-rollout-improvement.cpp` | Lost to fair D1 on all 12 leave-one-out folds | −8,221 / −8.25 | −24,721 | `C1`,`C7` | **Clean in direction.** Also correctly scoped by its own ledger entry ("does not establish that rollouts with a stronger continuation are unhelpful"). |

### A.2 The long-outcome / rollout-veto family (the strongest signal in the corpus)

| Approach / source | Stated rejection reason | Δ₇ / Δmoves | Δ₁₇ (rescored) | Confound | What a clean retest needs |
| --- | --- | --- | --- | --- | --- |
| **D4 + 25-move exact-D2 rollout veto** `rollout-veto/d4-d2-rollout-veto.cpp` | Runtime gate: projected 15,341 s vs a 2,700 s ceiling | pilot **+244,431 / +145** (n=1) | **candidate 894,047 vs D4 359,616 on that game** | `C1`,`C2`(n=1),`C4` | See Section C.4. This is the single largest unexploited number in the repository. The source **cannot compile today** — it `#include`s `fair-only-depth4.cpp` (locked at 17,000) while asserting `kLevelBonus == 7'000` itself. Retest = port to a new 17k experiment, then measure runtime honestly. |
| **Quality extension** `rollout-veto/d4-d2-rollout-veto-quality-extension.cpp` | Quality gate failed: only 1 of 3 new pairs was a joint win; lower-half score retention 88.4% vs 90% required | mean **+67,652 / +41.25** (n=4) | **candidate 392,160 vs D4 242,008** | `C1`,`C2`,`C8` (a 90%-lower-half gate at n=4 is 1 game) | The gate failed by 1.6 percentage points of a lower-half statistic computed from **two** games. Not a defensible rejection of the mechanism. |
| **Exact compression** `rollout-veto/d4-d2-rollout-veto-exact-compressed.cpp`, `-cache-free.cpp` | Parity held, savings immaterial (11.3% rollout-phase) | n/a | n/a | `C4` | **Clean.** Genuinely negative engineering result; correctly recorded. |
| **Teacher compression menu** `rollout-veto/d2-rollout-teacher-compression.cpp` | Best hybrid recovered 2 of 12 beneficial switches (needed 8) | n/a | n/a | `C6` partially | Clean *for the menu tested*. But the ground truth being reproduced (12 switches from **one** game) is itself n=1, and the 12 switch labels were selected by a score-return criterion. |
| **D2 long-outcome ranker** `long-outcome/d2-long-outcome-ranker.cpp` | Heldout ranking/regret did not beat exact D2 | n/a | n/a | `C6` | Target was 25-move **score return**; the same panel could be scored against survival. Source builds today but was run pre-correction and is **not** locked (see C.5). |
| **Scaled long-outcome NNUE** `long-outcome/scaled-long-outcome-nnue.cpp` | Larger model ranked worse; sample-bottleneck hypothesis rejected | n/a | n/a | `C1`,`C6` | Its **survival head reached r = 0.85 held-out** while its score-ranking head lost to exact D2. The rejection is of score ranking, not of the model. Locked at 7,000 → port required. |
| **Long-outcome feature audit** `long-outcome/d2-long-outcome-feature-audit.cpp` | Ladder scalar and multi-head NNUE both failed ranking | n/a | n/a | `C6` | Same: survival Pearson 0.854 old-heldout, clears 0.427; ranking done on return. |
| **D4 long-outcome veto classifier** `long-outcome/d4-long-outcome-veto-classifier.cpp` | 19 switches, **zero** true positives, precision 0 | n/a | n/a | `C8` | The fitting audit contained **7 roots with an eligible alternative** out of 288. A gate requiring 20% coverage and 80% precision cannot be evaluated on 7 positives. This is an underpowered design, not a falsification. |
| **H200 sibling NNUE** `h200-sibling-nnue/d4-h200-sibling-nnue.cpp` | Regressed D4 top-1/pairwise/regret in every origin fold | n/a | n/a | `C6` | `targets[0]` is the normalized `meanScoreReturn` residual and drives the ranking; `meanSurvivedMoves` is present in the corpus and used only as auxiliary head #1. Never ranked on survival. |

### A.3 Learned value / policy / RL family

| Approach / source | Stated rejection reason | Confound | What a clean retest needs |
| --- | --- | --- | --- |
| **Denoised-value guided veto** `denoised-value/denoised-guided-veto.cpp` | Screen passed, 8-game confirmation did not replicate (−2,606 pts, −1.9 moves) | `C1`,`C2`,`C7` | Rescored: −2,606 + 2000·(−1.875) = **−6,356** — direction holds. But the comparator was a *guided D5/K3 ensemble*, not fair D4, and 12 of 870 moves were vetoed. Effectively a null-action experiment. |
| **Phase-5 denoised veto of D4** `denoised-value/d4-phase5-value-veto.cpp` | Paused at runtime gate; first pair made **zero switches** | `C1`,`C4`,`C8` | **The most confounded entry in the corpus.** The best-predicting model in the repository (held-out lifetime ρ = 0.951, MAE 2.744 moves) was placed on top of D4, allowed to act only at `movesRemaining==5 && maxHeight>=4`, required a lifetime LCB ≥ 2.744 moves *and* non-negative survival-25 *and* a D4 root-Q loss within **"one canonical 7,000-point level bonus"** — a band that is now 17,000. It made 0 switches in 22 opportunities and then hit a runtime cap. **Zero information about the hypothesis was obtained.** Source is locked at 7,000. |
| **Denoised stochastic value** `denoised-value/denoised-stochastic-value.cpp` | *Completed, not rejected* — all prediction gates passed; confirmation improved behaviour from 79.5 → 88.125 moves (+10.8%) with 2.625 switches/game | `C2`,`C7` | This is the **only positive learned-model gameplay result in the corpus**, and it is the only one whose deployed decision rule ranked actions by predicted **lifetime**. It was never re-tested on top of fair D4 with a rule that could actually fire. |
| **Counterfactual-successor NNUE** `structured-nnue/counterfactual-successor-nnue.cpp` | Global lifetime ρ = 0.839 but top-1 15.4%; screen lost 63,196 pts / 39 moves | `C1`,`C2` | Rescored −141,196 — direction holds. Clean rejection of *that* leaf. The lesson recorded ("global value ≠ sibling discrimination") is correct. |
| **Structured multi-head NNUE** `structured-nnue/structured-value-nnue.cpp` | Held-out 50-move AUC 0.614 and lifetime ρ 0.510 missed the 0.80/0.65 gates | none material | **Clean.** Stopped at prediction, never touched policy seeds. Superseded by the denoised design that fixed exactly this. |
| **Survival-value scale** `monte-carlo-value/survival-value-scale.cpp` | Held-out lifetime ρ 0.557 missed a 0.60 gate | `C8` marginal | **Clean but a hair's breadth.** Missed by 0.043. Superseded by denoised (ρ 0.951). |
| **Direct MC behaviour value** `monte-carlo-value/mc-value-policy.cpp` | First paired stage collapsed (122,100 vs 246,448) | `C2`,`C7` | Clean in direction; a genuinely bad model. |
| **Sibling-advantage ranker / scaled** `sibling-advantage/*.cpp` | Learned ordering did not beat exact search; more data increased regret | `C1`,`C6` | Both hard-locked at 7,000. Target is relative **score** advantage. |
| **D4 root-Q clone** `d4-q-clone/d4-q-clone.cpp` | Held-out top-1 fell to 0.247; regret 0.421 vs one-ply 0.213 | `C1`,`C6` | Clean *as imitation*. Locked at 7,000. Note it imitates a score-maximizing teacher's ordering. |
| **Rainbow-lite n-tuple Q** `rainbow-q/rainbow-ntuple-q.cpp` | Stage B lost to exact fair-D1 by 56,980 pts / 16.1 moves | `C6` (reward design) | 17k engine, so no `C1`. Reward is literally `"unclipped score delta divided by 17000"` (line 1213): a rise is `+1.0`, a depth-1 disc clear is `+0.00041`, fed to proportional prioritized replay. Retest with a dense survival reward is a one-constant change. |
| **Temporal-coherence n-tuple** `temporal-coherence/ntuple-tc.cpp` | Corrected gradient still reached only 66,625 pts / 49.5 moves | `C6` | **The CLI default is `--reward one-per-move`** (line 1091); the recorded run explicitly overrode it to `score-delta --score-scale 1000` (rise `+17`, clear `+0.007`). Its successor `ntuple-phase-conditioned.cpp:279` hard-codes the override. The shipped survival reward has no recorded run. |
| **Optimistic phase n-tuple** `optimistic-phase/optimistic-phase-ntuple.cpp` | Two-boundary search (113,644/37.4) far worse than direct play (181,733/56.4) | `C6` | `rewardForMove = score_delta / kScoreScale`. Same. Also the one preregistered protocol that survived a full 50 M-transition budget — a valuable negative on the *search*, weaker on the *reward*. |
| **Native PPO v2** `native-ppo/ppo-v2.cpp` | Warm-start clone failed 1.10×-random / 0.70×-fair-D1 gates | `C1` | Locked at 7,000. Its reward weights survival first-class (`0.10 + 0.020·cleared + 0.015·revealed + score/700000 − 0.5·terminal`, line 563 — the score term is ≈0.005/move against a 0.10 survival term), and it was rejected at the *imitation* stage before that reward ever mattered. Compare `src/core/native/ppo.hpp:587`, `reward = 1.0 + score_delta/100'000`. |
| **Regenerative expert iteration** `regenerative-expert-iteration/*.cpp` | 8 rounds all far below the D4 bootstrap; exported no checkpoint | none material | **Clean, and the most useful negative in the corpus.** Its own diagnosis ("targets observed for the played action while deployment maximized over unplayed siblings") is the correct one. |
| **Martingale-dual B0** `deployment-panel/martingale-dual-b0.cpp` | Top-1 28.9% vs D4 38.2%; 0/8 origins; split stability 22.6% | `C6` | Line 954: `result.target[action] = numberAfter(object, "\"meanScoreReturn\":")`. The same corpus record also contains `meanSurvivedMoves`, which was never read. |
| **Full-panel CPI preflight** `deployment-panel/full-panel-cpi-preflight.cpp` | Every gate failed; 0/8 folds | `C6` | Same corpus, same score-return target. |
| **Public regenerative B0** `public-regenerative-b0/*.cpp` | Overrode 11/477 roots; indistinguishable from keeping D4 | `C6`,`C8` | With 11 overrides, a 75%-precision gate is decided by 3 roots. Underpowered by construction. |
| **Oracle distillation / DAgger / topology residual** `oracle-curriculum/*` | Label gates failed; screen wins reversed in confirmation | `C1`,`C2`,`C8` | `oracle-topology-residual` missed its gate by **30 examples and 15 pairs** after the cohort was cut for *runtime* reasons — a textbook `C4`→`C8` cascade. Its extension then *replicated* the prediction signal and still failed the policy confirmation (rescored −41,830 + 2000·(−24) = −89,830). |
| **Constructive spectrum → D4** `constructive-spectrum/constructive-spectrum-depth4.cpp` | Overrode 37% of moves; 283,286/83.75 vs D4 372,871/106.25 | `C2`(n=4),`C5` | 17k engine. Clean in direction, but n=4 and the override rate (37%) means this tests "replace D4", not "correct D4". |
| **H12 horizon / H12 risk gate** `constructive-spectrum/*.cpp` | H12 improved means (299,731/88.3 vs H7 258,224/77.2) but missed robustness / paired-win gates | `C8` | Both *improved* score and survival and were rejected on win-count gates at n=32. The signal is real; the gate was the binding constraint. |
| **Tail-survival CEM** `tail-survival-cem/*.cpp` | 50,432 candidate-games for +6,028 points; failed every floor | none material | **Clean, and important.** A pure survival-milestone objective did **not** rescue a weak linear phase evaluator. This is the strongest existing counter-evidence to a naive "just optimize survival" prescription. |

### A.4 Summary of Section A

- **26 of 139 sources are hard-locked to `kLevelBonus == 7'000`** and, verified by
  compilation, **none of them builds against the current engine**. Every
  rejection they carry was decided in Sequence units.
- **2 rejections invert under corrected scoring**: seven-stratum D4 and the
  fair-leaf CEM. Both were rejected by a score-mean delta smaller in magnitude
  than the value of the survival they gained.
- **1 pilot becomes an 894,047-point legal game** (Section C.4).
- **12 rejections are clean in direction** — rescoring makes them worse, or they
  stopped at a prediction gate that a later design legitimately superseded.
- **At least 9 rejections are underpowered by construction** (`C8`): a precision
  or coverage gate evaluated on 3–7 positive cases, or a lower-half statistic
  computed from 2 games.
- **`C6` (mis-specified objective) applies to the entire sibling-ranking
  sub-family and to every recorded n-tuple/Rainbow run** — 13+ ranking
  experiments and 6 RL experiments — see Section C.5.

---

## Section B — Sources with no recorded outcome

All 139 approach sources appear in `experiment-index.md`. **58 of 139 have no
entry in `history.md` at all** — their result exists only as a "task record",
which `experiment-index.md` itself instructs the reader to "treat as provisional".

### B.1 Complete labs with a preregistered protocol and *no* result (`Unknown`)

All three compile cleanly against the corrected 17,000-point engine (verified).

| Source | Question it asks | Preregistered seed lanes | Notes |
| --- | --- | --- | --- |
| `approaches/heuristic-search/edge-priority/edge-priority-lab.cpp` (661 lines) | Does raising the covered-disc **altitude exponent** (2.0) or the **edge-column multiplier** (1.3 → 2/3/4) improve complete games? Six profiles, paired screen + confirmation. | `0x3d700300` screen, `0x3d700400` confirm | This is the direct operationalization of the only external human-strategy source cited in `strategies.md` (Adam Saltsman's "high covers, watch the edges"). The comment block says so explicitly. It has never been run. |
| `approaches/heuristic-search/critical-risk/critical-risk-lab.cpp` (726 lines) | Does lower-tail (CVaR-0.4) aggregation applied **only at high-load states** (height ≥ 5/6, occupancy ≥ 20/24, cover backlog ≥ 4/6) help, where global root CVaR failed? Five profiles incl. a control. | `0x3d700500` screen, `0x3d700600` confirm | Directly addresses the `fair-root-risk.cpp` failure mode (risk aggregation applied everywhere). Never run. |
| `approaches/tree-search/puct/puct.cpp` (975 lines) | Does PUCT with phase-policy priors beat the plain UCT that failed its held-out gate? Separate simulation/reveal/disc random domains, screen + confirmation lanes. | `0x3d701200` screen, `0x3d701300` confirm | The roadmap explicitly defers PUCT ("do not begin with learned action pruning"); the lab nevertheless exists, complete, unrun. |

### B.2 Approach sources marked support-only — real questions with no lane

| Source | Question it could answer | Why it has no result |
| --- | --- | --- |
| `constructive-reservoir/rise-option-qd/rise-option-qd.cpp` | Do persistent rise-cycle **options** (build / crack / release / stabilize) with a MAP-Elites archive cover useful behaviour space? | "the source intentionally has no gameplay or production-training lane" |
| `baselines-diagnostics/phase-benchmark/phase-benchmark.cpp` | Score, survival, flow, height, work and censoring for the shared phase policy | "no durable standalone result was located" |
| `baselines-diagnostics/d4-flow/d4-flow-audit.cpp` | Per-move D4 geometry, flow and root-value traces | Diagnostic only; no retained output |
| `baselines-diagnostics/trajectory-throughput/main.ts` | Where the regenerative clear/reveal regime lives — **and it is the only file in the repo that counts `boardClears`** (lines 148, 537, 565, 1330) | Task-record only; motivated the 2.4-clear / 1.4-reveal targets |
| `baselines-diagnostics/tie-breaking/main.ts` | Sensitivity to fixed legal-column ordering | Support-only |
| `baselines-diagnostics/heuristic-benchmark/main.ts` | Paired comparisons among named TypeScript heuristic profiles | Support-only |
| `heuristic-search/open-loop/main.ts` | Public open-loop synthetic-future planning | "primarily serves as a comparator" |
| `heuristic-search/policy-comparison/main.ts` | Common comparison harness | Support-only |
| `oracle-curriculum/state-curriculum/generate.ts` | Public restart states from oracle trajectories | Support-only |
| `fair-expectimax/fair-policy/weight-sweep.ts` | Bounded coefficient sweeps on the fair leaf | No ledger entry |

### B.3 The larger provenance gap

**30 sources carry a `Rejected` verdict that exists only as a task record**, with
no ledger protocol, no artifact hash, and no retained per-game data. These
include several conclusions that the current `status.md` narrative leans on:

`throughput-probe.cpp`, `phase-horizon/main.ts`, `virtual-ignition/main.ts`,
`risk-sensitive/main.ts`, `gray-throughput/{tune,benchmark}.ts`,
`rollout/rollout.cpp`, `cycle-abstraction.cpp`, `mcts/typescript.ts`,
`nnue-selective-search.cpp`, `direct-policy/main.ts`, `value-model/train.ts`,
`dqn/train-v2.ts`, `monte-carlo-return/*.ts`, `chance-state-nnue/nnue-value.cpp`,
`phase-distillation/{phase-student,phase-q-student}.cpp`,
`sibling-advantage/*.cpp`, `bellman-ntuple.cpp`, `flow-curriculum-rainbow.cpp`,
`manifold-gail-{development,scaled}.cpp`, `curriculum-option-ppo.cpp`,
`oracle-dagger/main.ts`, `d4-h200-sibling-nnue.cpp`,
`curriculum-long-outcome-nnue.cpp`, `panel-value-nnue.cpp`,
`direct-sibling-ranker.cpp`, `d4-structural-terminal-veto.cpp`,
`public-survival-rollout.cpp`, `terminal-policy-iteration.cpp`.

For an audit this matters twice over: these rejections cannot be re-derived, and
several of them (`cycle-abstraction`, `curriculum-long-outcome-nnue`,
`public-survival-rollout`, `terminal-policy-iteration`) are precisely the
survival-flavoured experiments whose numbers Section C needs.

---

## Section C — The central arithmetic

### C.1 Verified from `src/core/native/engine.hpp`

| Constant / rule | Source | Value |
| --- | --- | --- |
| Row-rise award | line 21, applied at line 310 | flat **17,000**, once per successful rise |
| Board clear | line 22, applied at lines 297 and 321 | flat **70,000** |
| Chain wave depth *d* | `scoreForWave`, lines 202–206 | `popper_count × floor(7·d^2.5)` |
| Rise cadence | `kMovesPerLevel`, line 18 | every **5** placed discs |
| Terminal | lines 306–310, 325–327 | if `raiseCoveredRow` fails at the rise, `game_over` and **no bonus is awarded** |

`floor(7·d^2.5)` for d = 1…8: **7, 39, 109, 224, 391, 617, 907, 1267** per disc.

The terminal rule is load-bearing and is *not* stated in `methodology.md`:
because the failed rise pays nothing, a game of exactly `M` moves that dies at a
rise earns `M/5 − 1` level bonuses, not `floor(M/5)`.

### C.2 An exact paired-rescoring identity

For any game that ends on a failed rise (`M` a multiple of 5):

```
score      = 17000·(M/5 − 1) + 70000·clears + chainPoints
score_7k   =  7000·(M/5 − 1) + 70000·clears + chainPoints
⟹ score_17k = score_7k + 2000·M − 10000
⟹ Δscore_17k = Δscore_7k + 2000·Δmoves      (paired; the −10000 cancels)
```

I verified this identity against per-game rows produced by a concurrent
diagnostic (see "what I could not determine"): e.g. `0xa51d0001`, 25 moves,
4 rises, 68,000 level + 770 chain = 68,770; and
`28,770 + 2000·25 − 10,000 = 68,770`. Exact.

**Every 7,000-point paired result in `history.md` can therefore be converted to
corrected Hardcore units with no new computation.** No one has done this.

### C.3 Fair D4's own decomposition

Recorded: 308,295.578 points over 90.031 moves, 64 games (`regenerative-expert-iteration` bootstrap).

| Quantity | Value |
| --- | --- |
| Total score | 19,730,917 |
| Total moves | 5,762 |
| Rises (at `M/5 − 1`) | ≈ 1,088 → **17.0 per game** |
| Level points | **18,502,800 = 93.8 %** |
| Chain + board-clear points | 1,228,117 = 6.2 % → **19,189 per game, 213 per move** |
| Implied score slope | **3,613 points per move of survival** |
| Moves needed for a 1,000,000 mean | **281.5 — 3.13× current lifetime** |
| One board clear, in survival-equivalent | **19.4 moves** |
| Board clears needed at current lifetime | **9.9 per game** |

The prompt's figure of 99.3% used `floor(90.031/5) = 18` rises; the engine's
terminal rule gives 17. The corrected figure is **93.8%**, and it is the more
defensible one. Either way the conclusion is unchanged and stark.

The cleanest single statement of the blind spot:

> Fair D4 scores 3,424 points per move. A **random legal policy** scores 2,841
> points per move. D4's entire 3.82× score advantage over random decomposes as
> **3.17× lifetime × 1.21× points-per-move**. Roughly 89% of the log-improvement
> that four plies of expectimax buy is survival time, not scoring skill.

### C.4 The unexploited number

Applying C.2 to `d4-d2-rollout-veto.cpp`'s single pilot on `0x3ded0000`:

| | recorded (7k) | rescored (17k) |
| --- | ---: | ---: |
| Fair D4, 105 moves | 159,616 | **359,616** |
| D2/s7/h25 rollout veto, 250 moves | 404,047 | **894,047** |

and to its four-game quality extension:

| | recorded (7k) | rescored (17k) |
| --- | ---: | ---: |
| Fair D4 (mean of 4) | 107,008 / 72.5 mv | **242,008** |
| Rollout veto (mean of 4) | 174,660 / 113.75 mv | **392,160** |

Two further consequences of the same identity, neither stated anywhere in the
documentation:

- In the `oracle-topology-residual-extension` confirmation cohort, an ordinary
  **fair-D4 baseline game of 285 moves** scored 476,511 at 7,000 → **1,036,511
  corrected**. A legal public policy has already exceeded one million points on
  a single game more than once.
- The privileged perfect-information oracle's 1,058,931.5 mean at the 500-move
  cap rescoreS to **≈ 2,058,931 and is censored**. The mechanical ceiling is at
  least twice the qualification bar.

None of this changes a *mean*. It changes what the corpus's own best signal is
worth, and it means the rollout-veto family was shelved on a runtime gate while
its rescored pilot sat at 89% of the target.

### C.5 What objective did each experiment actually regress on?

I commissioned an independent, read-only classification pass over all 139
sources and then spot-verified seven of its claims by reading the cited lines
directly; all seven were exact. **Its counts corrected my own first-pass
estimate, and the corrected answer is more interesting than the naive one.**

**Counts, by file (139 total)**

| Class | Files |
| --- | ---: |
| **S** — score or score-derived return | **26** |
| **L** — lifetime / survival / moves / hazard | **16** |
| **B** — both heads or an explicit `moves + score/K` composite | **17** |
| **F** — flow or another proxy | **3** |
| **N** — no learning (pure search / harness / diagnostic) | **77** |

**Counts, by experiment directory (41 of 84 contain learning)**

| | Dirs |
| --- | ---: |
| Pure score target | **13** |
| Pure lifetime / survival target | **12** |
| Both | **11** |
| Flow / proxy | **2** |
| Mixed within the directory | **3** |

Counting any presence: **27 experiment directories regress on score, 24 on
lifetime or survival.** So the blunt claim "the corpus predominantly regressed
on raw score" is **false**, and I will not make it. The corpus is roughly
balanced. The blind spot is real but it is *structural*, not aggregate:

#### (a) The split is almost perfectly along sub-family lines

| Sub-family | Target | Outcome |
| --- | --- | --- |
| **Deployment panel + long-outcome sibling ranking** (`martingale-dual-b0`, `full-panel-cpi-preflight`, `terminal-panel-d4-signal-audit`, `d4-h200-sibling-nnue`, `scaled-long-outcome-nnue`, `d2-long-outcome-ranker`, `d2-long-outcome-feature-audit`, `relaxed-chain-potential-audit`, `d4-long-outcome-veto-classifier`, `d4-q-clone`, `scaled-d4-distill`, `phase-student`, `phase-q-student`) | **score return / score root-Q**, uniformly | **every one rejected on ranking** |
| **Public state-value models** (`dqn/train.ts`, `dqn/train-v2.ts`, `monte-carlo-return/train.ts`, `monte-carlo-value/*`, `structured-nnue/*`, `denoised-value/denoised-stochastic-value`, `cfpi`, `chance-state-nnue`) | **lifetime / survival**, uniformly | **the corpus's best held-out prediction and its only positive learned gameplay result** |
| **PPO / GAIL** (`curriculum-option-ppo`, `oracle-manifold-ppo`, `manifold-gail-*`, `torch-ppo`) | `score_delta/17'000 + 0.05·survival` — score-dominant | all rejected |
| **n-tuple / Q** | **survival is the default and was overridden** — see (c) | all rejected |

The roadmap names within-root sibling ranking as *the* bottleneck. That is
exactly the sub-family that never once used a survival target.

#### (b) `d4-h200-sibling-nnue.cpp` states the problem in its own artifact

```
"top1":"selected action reaches the maximum stored meanScoreReturn within 1e-9"
```
(line 1993). The very same corpus record it parses contains
`"meanSurvivedMoves"` (line 261), which the model consumes only as auxiliary
head #1 and which no ranking metric in the repository has ever scored against.
Four separate experiments were decided on the 477-root H200 panel using the
score field; the survival field sat unused in every one.

#### (c) The n-tuple family's survival reward is the *default*, and the recorded runs turned it off

```cpp
// src/core/native/ntuple.hpp:311
inline float transitionReward(const MoveResult& move, const Options& options) {
  return options.direct_score_reward
             ? static_cast<float>(move.score_delta) / 3'400.0f
             : 1.0f;                                  // <- default: +1 per move
}
```

```cpp
// approaches/ntuple-rl/temporal-coherence/ntuple-tc.cpp:1091
valueAfter(argc, argv, "--reward", "one-per-move");   // <- CLI default
```

The recorded temporal-coherence run in `history.md` passes
`--reward score-delta --score-scale 1000`, explicitly overriding that default.
Its successor `ntuple-phase-conditioned.cpp:279` **hard-codes**
`options.score_reward = true`, removing the survival path entirely. Under
`score-delta --score-scale 1000` a rise is `+17.0` and a depth-1 disc clear is
`+0.007` — a **2,429 : 1** dynamic range, fed to TD(0).

Meanwhile the n-tuple *search* primitive it feeds counts moves:

```cpp
// src/core/native/ntuple-search.hpp:77
total += 1.0 + context.model.value(move.state);       // +1 per surviving move
```

so a score-reward model is being backed up inside a lifetime-unit search. I did
not trace every call site and do not claim this is a bug — but the units
disagree by construction and nothing in the ledger records that anyone checked.
**There is no ledger entry anywhere for an n-tuple run using the shipped
`one-per-move` default under corrected scoring.**

The same 2,429 : 1 encoding appears verbatim in the Rainbow family:
`rainbow-ntuple-q.cpp:1213` declares
`"reward":"unclipped score delta divided by 17000"`, and
`flow-curriculum-rainbow.cpp:776` uses `score_delta / kLevelBonus` — both fed
to **proportional prioritized replay on TD error**.

#### (d) The corpus already knows score ≈ 3,400 × moves, in a comment

```cpp
// approaches/heuristic-search/evolved-public-policy/evo-public-policy.cpp:682
// The level award makes score and lifetime nearly collinear.  Retaining both
// rewards unusually productive chains without permitting one lucky game to
// dominate: forty percent of fitness is the lower quartile.
const double mean_utility = evaluation.mean_moves +
                            evaluation.mean_score / 17'000.0;
```

**Six independent sources converged on the same `moves + score/K` composite** —
`evo-public-policy.cpp` (K=17,000), `fair-cem-optimizer.cpp` (K=14,000),
`panel-value-nnue.cpp` and `direct-sibling-ranker.cpp` (K=17,000, line 415/677),
`sibling-advantage-ranker.cpp`, and `vertical-reservoir-policy.cpp` (K=6,800).
Several authors independently rediscovered that score alone is an inadequate
fitness. That knowledge never propagated to (i) the H200 panel targets,
(ii) the RL rewards, (iii) `methodology.md`'s reporting standard, or (iv) the
roadmap. It is folklore in the code, absent from the documentation.

One irony worth recording: `evolution.cpp`'s objective carries the comment
"*should reward long survival without allowing one extraordinary chain to
dominate*" — and then computes `0.65·mean_score + 0.35·median_score`, with no
survival term at all (lines 223–226).

#### (e) The variance argument, stated honestly

Score and lifetime are **0.88–0.9997 correlated per game** on 64-game cohorts of
three weak policies, and switching the *reported* metric buys only 15–30%
variance reduction:

| Policy | CV(score) | CV(moves) | CV(chainPoints) | corr(score, moves) |
| --- | ---: | ---: | ---: | ---: |
| center-first | 0.476 | 0.361 | 1.472 | 0.9997 |
| random-legal | 0.151 | 0.120 | 0.945 | 0.9975 |
| lowest-column | 0.108 | 0.094 | 0.803 | 0.8776 |

The heavy tail lives entirely in `chainPoints` (CV up to 1.47, max/median up to
24×) — 6.2% of the mean carrying most of the variance. And
`tail-survival-cem.cpp` is direct counter-evidence to a naive prescription:
50,432 candidate-games optimizing a 75/100/150/225/300-move survival curve
bought +6,028 points and failed every floor. **Changing the objective does not
rescue a weak evaluator.**

#### (f) The defensible version of the finding

> The blind spot is not "everyone used the wrong metric" — roughly half the
> corpus already regresses lifetime. The blind spot is that the two halves were
> **never crossed**. The sub-family that learned lifetime well
> (`denoised-stochastic-value`: held-out ρ = 0.951, MAE 2.744 moves) was never
> allowed to rank siblings on top of fair D4; its one attempt made **zero**
> switches. The sub-family that ranks siblings
> (`martingale-dual-b0`, `full-panel-cpi-preflight`, `d4-h200-sibling-nnue`,
> `scaled-long-outcome-nnue`) was uniformly scored against `meanScoreReturn`
> while `meanSurvivedMoves` sat unread in the same records. And the n-tuple
> family's shipped survival reward was overridden in every recorded run and
> then hard-coded away.

### C.6 How often do board clears actually happen?

**No board-clear statistic appears anywhere in `docs/`.** Verified:

- `methodology.md` line 16 defines the 70,000-point award, and its mandatory
  reporting list (lines 71–79) requires censoring, clears/reveals per move, and
  chain depth — **but not board clears**.
- `history.md`: zero occurrences of any board-clear count across 4,607 lines.
- `experiment-index.md`, `status.md`, `strategies.md`, `roadmap.md`: the only
  mention of board clears is in `strategies.md` line 480, describing *David
  Walton's Sequence-mode solver*, not this repository's own play.
- Only **8 of 139** approach sources even instantiate a counter
  (`fair-d1-rollout-improvement`, `fair-phase-energy-release`,
  `fair-reveal-reward`, `edge-priority-lab`, `public-rollout-policy-iteration`,
  `trajectory-throughput/main.ts`, `vertical-reservoir-policy`,
  `evolution.cpp`), and **none of their counts was ever promoted into a document**.

What can be bounded from the recorded fair-D4 cohort: total non-level score
across 64 games is 1,228,117, so **board clears in that cohort number at most 17
and plausibly zero**, but the data to resolve it no longer exists. This is a
finding in its own right — a 70,000-point event worth **19.4 moves of survival**,
i.e. **22.7% of an entire average D4 game**, is completely unmeasured. A policy
achieving one board clear per game would add 70,000 to its mean for free; ten per
game would close the entire gap to one million at today's lifetime.

---

## Section D — Ranked retests, by expected information gain per CPU-hour

Every item reuses an existing source. None invents an architecture. Estimated
costs come from the wall-clock figures recorded for each source in `history.md`.

### D1 — Rescore the entire 7,000-point ledger (≈ 0 CPU-hours) ★★★★★

**Reuse:** nothing but `history.md` and the identity in C.2.
**Change:** publish `Δ₁₇ = Δ₇ + 2000·Δmoves` for every 7,000-scored paired
result, as an exploratory table.
**Falsification:** if the recorded per-game move counts are not multiples of five
(i.e. some games ended from "no legal column" rather than a failed rise), the
identity is approximate and the sign flips must be re-derived per game.
**Why first:** it costs nothing, it has already changed two verdicts and produced
an 894,047-point legal game, and every retest below is prioritized by its output.

### D2 — Re-rank the sibling panel on `meanSurvivedMoves` (≈ 0.5 CPU-hours) ★★★★★

**Reuse:** `approaches/d4-long-outcome/long-outcome/d2-long-outcome-feature-audit.cpp`
(builds today against the 17,000 engine; recorded runtime 188 s, four workers)
together with `d2-long-outcome-ranker.cpp` (226 s) to regenerate the 432-root,
7-scenario sibling panel with **both** `meanScoreReturn` and `meanSurvivedMoves`
recorded per sibling.
**Change:** compute the *identical* frozen ranking metrics (top-1, top-2,
pairwise, normalized regret) for exact D2 and exact D4 against **both** targets.
No model is trained. No new policy is proposed.
**Falsification:** if D4's top-1 and pairwise accuracy against the survival
target are **not** materially higher than against the score target (say < 2
percentage points), then score-return label noise is not what is limiting
sibling ranking, and the entire objective hypothesis dies for ≈ 30 CPU-minutes.
**Why:** thirteen ranking experiments were decided on a target nobody ever
validated as the right one. This is the cheapest possible test of that premise,
and the second target is already in the data format.

### D3 — Port the 25-move rollout veto to corrected scoring (≈ 4–8 CPU-hours) ★★★★☆

**Reuse:** `approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto-exact-compressed.cpp`
(the outcome-preserving version, with the D4-Q prefilter and suffix cache).
**Change:** create a **new** experiment directory (do not edit the locked
sources). Exactly two constants change: the `kLevelBonus == 7'000` assertion
becomes 17,000, and `kMaximumRootQLoss` — currently "one canonical 7,000-point
level bonus" — becomes 17,000. Re-run the frozen `0x3ded0000…0003` quartet.
**Falsification:** if the corrected-scoring candidate does not reproduce a mean
above D4 with at least 3 of 4 joint score-and-move wins, the mechanism is dead
regardless of runtime.
**Why:** the rescored pilot is 894,047 points and the rescored quartet is
392,160 vs 242,008. Note that the correction is **not** cosmetic — the rollout
return *is* accumulated score, so a 17,000 rise makes the veto's own objective
weight surviving-to-the-next-rise 2.43× more heavily. The runtime gate remains
the real obstacle and should be measured, not waived.

### D4 — Re-run the two sign-flipped fair-expectimax ablations (≈ 6–20 CPU-hours) ★★★★☆

**Reuse:** `chance-strata/fair-depth4-s7.cpp` and `cem/fair-cem-optimizer.cpp`,
ported (not edited) into new 17k experiments.
**Change:** level bonus 17,000; for the CEM, the fitness divisor `score/14000`
becomes `score/17000`. Cohort n ≥ 32 paired, not 8.
**Falsification:** if s7's corrected paired score-mean delta is ≤ 0 at n = 32,
the seven-stratum hypothesis is rejected cleanly for the first time.
**Cost note:** s7 cost 4.84 M work units/move vs 1.28 M for s5 — this is the most
expensive item in the top five and is ranked below D2/D3 for that reason.

### D5 — Run the three `Unknown` labs (≈ 1–3 CPU-hours each) ★★★☆☆

**Reuse, unmodified:** `edge-priority-lab.cpp`, `critical-risk-lab.cpp`,
`puct.cpp`. All three compile clean, carry preregistered screen/confirmation
lanes, and expose `--self-test --screen-games --confirm-games --max-moves`.
**Falsification:** each has its own frozen paired gate already in the source.
**Why:** these are finished experiments with zero recorded outcome. The
edge-priority lab in particular is the only implementation of the one external
human-strategy hypothesis the repository cites. Running a completed lab is
strictly cheaper per bit than writing a new one.
**Caveat:** confirm with the coordinator that `0x3d7003xx`–`0x3d7013xx` are
unconsumed before opening them.

### D6 — Give the denoised lifetime model a rule that can actually fire (≈ 3–6 CPU-hours) ★★★☆☆

**Reuse:** `approaches/value-policy-learning/denoised-value/d4-phase5-value-veto.cpp`
plus the retained checkpoint `artifacts/models/denoised-value/v1.bin`
(35,395 parameters, 141,780 bytes, checksum 1239007257 — the only surviving
model artifact in the repository).
**Change:** port to 17,000; widen the root-Q band from 7,000 to 17,000 (one
corrected level bonus); route on all five phases rather than `movesRemaining==5`
only; report **coverage as a first-class metric**.
**Falsification:** if the corrected rule still makes < 1 switch per 50 routed
decisions, the conservative-veto architecture is falsified as a *design*, which
is itself a durable negative result the corpus does not yet have.
**Why:** this model has the best held-out prediction in the corpus
(ρ = 0.951, MAE 2.744 moves) and produced the corpus's only positive learned
gameplay result (+10.8% survival). It has never been allowed to act on top of
fair D4 — its one attempt made zero switches under a 7,000-point band and then
hit a runtime cap.

### D7 — Instrument board clears everywhere (≈ 0.1 CPU-hours + a doc change) ★★★☆☆

**Reuse:** the counter already present in
`baselines-diagnostics/trajectory-throughput/main.ts` (lines 148/537/565/1330)
and in `fair-d1-rollout-improvement.cpp` (`clear_boards`).
**Change:** add `boardClears` and the `level / clear / chain` split to
`methodology.md`'s mandatory reporting list and to `research/schemas/game-result-v1.schema.json`.
**Falsification:** none — this is measurement, not a hypothesis.
**Why:** a 70,000-point event worth 19.4 moves is currently invisible. Until it
is counted, "how far is D4 from a million" cannot be answered from either side.

### D8 — Run the n-tuple family with its own shipped default reward (≈ 0.5 CPU-hours) ★★★☆☆

**Reuse:** `approaches/ntuple-rl/temporal-coherence/ntuple-tc.cpp`, unmodified.
**Change:** *drop* the `--reward score-delta --score-scale 1000` override that
the recorded run used, so the CLI default `--reward one-per-move`
(`ntuple-tc.cpp:1091`, backed by `src/core/native/ntuple.hpp:311`) takes effect.
Everything else — corrected temporal-coherence gradient, 10,000 games, the
already-burned `0x3d100000` training and `0x3d200000` probe lanes, the frozen
100,000-point / 70-move stop gate — is untouched. **Zero new seeds. Zero code
edits. One removed flag.**
**Falsification:** if `one-per-move` does not beat the recorded score-delta
result (66,625 points / 49.5 moves) on the identical burned probe, reward
encoding is not the n-tuple family's binding constraint and the objective
hypothesis loses its cheapest supporting case.
**Why it matters beyond the n-tuples:** the survival reward is the *shipped
default* of the shared `ntuple.hpp` primitive, the search primitive it feeds
already accumulates `+1.0` per surviving move
(`ntuple-search.hpp:77`), and **no ledger entry anywhere records a run that used
it.** A secondary check worth folding in: confirm whether a `score-delta`-trained
model is unit-compatible with that `+1.0`-per-move search accumulator, since
`ntuple-phase-conditioned.cpp:279` hard-codes the score reward while using the
same search.
**Companion (≈ 0.5 CPU-hours):** the same one-constant swap in
`rainbow-ntuple-q.cpp` (reward `scoreDelta/17000` → `1.0 + 0.02·cleared +
0.015·revealed`, borrowed from `ppo-v2.cpp:563`), falsified against its own
frozen Stage A checkpoint (101,325 points / 33.91 moves).

### What I would explicitly *not* do next

- Increase depth, width, chance strata, model capacity, or table size. Every
  such experiment in the corpus failed, and the two that arguably should not
  have (D5-selective, s7) failed for reasons D1/D4 address more cheaply.
- Implement AFBR-40. Its data-closure preflight has not been run, `status.md`
  is explicit that it must not appear in a list of attempted results, and the
  corpus's ranking premise (D2) has not been validated.
- Open any `0x7d…` or `0xd7…` seed. Nothing here comes close to authorizing it.

---

## What I could not determine

1. **Whether the corrected-scoring sign flips survive contact with reality.**
   The C.2 identity rescores *fixed trajectories*. Under a 17,000 level bonus
   the searches would also **play differently**, because the level award is part
   of the backed-up utility. I expect the effect to point the same way (more
   survival incentive), but I cannot prove it without running the ports in D3/D4.
   The two sign flips are a reason to retest, not a claim that s7 and the CEM
   vector work.

2. **The exact board-clear rate of any policy.** No retained artifact contains
   it. My bound for the fair-D4 64-game cohort (≤ 17 clears in 64 games) is
   derived from a residual, not observed. Every `/tmp/drop7-*.json` artifact
   referenced throughout `history.md` — including the 477-root H200 deployment
   panel, the 1,508/465 root-Q corpus, and every per-game ledger — **is gone**.
   `artifacts/results/` contains only a README.

3. **Whether D4's games all end on a failed rise.** 64 × 90.031 = 5,762 is not a
   multiple of 5, so at least one or two games ended from "no legal column"
   instead. This perturbs the 93.8% figure by well under a percentage point but
   means my rise count is 1,088 ± 2, not exact.

4. **The per-experiment cost of the D2 retest at scale.** I used recorded wall
   times from a different (Apple) machine profile; `docs/hardware/` and
   `research/system-profiles/` contain no profile for the current host, so every
   CPU-hour estimate in Section D is an order-of-magnitude guide, not a budget.

5. **Whether the `0x3d7003xx`/`0x3d7005xx`/`0x3d7012xx` lanes reserved by the
   three `Unknown` labs are actually unconsumed.** They are inside the burned
   `0x3d…` fitting family. The seed ledger that would settle this does not exist
   in the repository.

6. **Residual uncertainty in the objective classification.** The counts in C.5
   come from an independent read-only pass over all 139 sources; I spot-verified
   seven of its citations (`ntuple.hpp:311`, `ntuple-search.hpp:77`,
   `public-behavior.hpp:844`, `evo-public-policy.cpp:682`,
   `panel-value-nnue.cpp:415`, `d4-h200-sibling-nnue.cpp:1993`,
   `dqn/train.ts:1291`) and all seven were exact. Two classifications rest on
   inference rather than a located reward expression —
   `manifold-root-prior.cpp` and `manifold-gail-development.cpp`, both assigned
   `S` from their frozen reward `static_assert`s and `meanScoreDelta` gate. My
   own first-pass counts were wrong (I had `dqn/train.ts` and
   `monte-carlo-return/train.ts` as score-targeted; both are survival-targeted),
   which is a reason to treat any *single* row here as provisional even though
   the aggregate is solid. Treat each class count as ±2.

8. **Whether the units mismatch in C.5(c) is real.** `ntuple-search.hpp:77`
   accumulates `1.0` per surviving move while `ntuple-phase-conditioned.cpp:279`
   trains the value it consumes on `score_delta/1000`. I did not trace every
   call site and am not claiming a bug — only that the two primitives are in
   different units and nothing in the ledger records that anyone checked.

7. **Concurrent work in this tree.** While I was auditing, another contributor
   created `approaches/lifetime-objective/score-decomposition/{decompose.cpp,build.sh}`,
   built `build/lifetime/decompose`, and produced
   `runs/RUN-A51D-cheap/{center-first,lowest-column,random-legal}.json` under a
   seed lease `SEEDLEASE-A51D` (`0xa51d0000…`). I did not create, modify, or
   delete any of it. I *read* those three JSON files and used them in two places:
   to verify the C.2 identity against real per-game rows, and for the CV table in
   C.5. They independently corroborate this audit — level share 98.5–98.9% for
   weak policies, `boardClearsPerGame = 0` across all 192 games,
   `scoreIdentityFailures: 0` — but they are **not** registered results, I did
   not verify their generator, and no conclusion here depends on them alone.
   Section D1/D7 may already be partly in flight; the coordinator should
   deduplicate before assigning D-items.
