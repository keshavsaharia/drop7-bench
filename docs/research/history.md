# Detailed Experiment Ledger

This is the unabridged chronological record of the Drop7 strategy experiments.
It preserves configurations, measured outcomes, rejected gates, seed-use notes,
and historical artifact hashes. Start with the repository README and the
[current status](status.md) for a concise explanation; use this ledger when an
exact historical detail is needed.

Most expensive runs below were not repeated during the project reorganization,
and many generated artifacts were not retained. Their measurements are
ledger-recorded evidence rather than newly reproduced results. Historical
source hashes describe the pre-reorganization tree; see
[provenance](../provenance.md) before resuming a locked experiment.

This repository is an offline laboratory for testing long-horizon Drop7
policies. The C++ engine implements the same rules as the TypeScript engine,
while native executables can play millions of moves per second and save compact
learned value functions.

The engine models **Hardcore mode**: a covered row rises after every five drops
and only numbered discs arrive from the top. Normal and Sequence mode instead
begin with a much longer level countdown (30 drops, then 29, and so on); results
from those easier modes are not comparable to the qualification target here.

The first learned-policy line investigated here combined:

- a hierarchical n-tuple value function trained from complete games, and
- a bounded, sparse depth-2 expectimax search that uses the learned value at its
  leaves.

Four-cell tuples represent latent chain arrangements as well as repeated local
patterns such as adjacent `1`s and runs of `2`s. Training them against full-game
returns lets the data determine whether those shapes are useful or clogging,
instead of assigning an arbitrary one-step penalty. The hierarchy adds absolute
board-position residuals to shared row, column, and 2x2-pattern tables, so it can
distinguish the same shape near the ceiling from one in a safer location.

That model was useful as a baseline experiment, but later, cleaner native
comparisons did not establish it as the selected deployment policy. Drop7 has
chance nodes and a very long horizon, so ordinary alpha-beta pruning does not
directly apply. The sparse search controls work with stratified gray-disc
reveals, exact enumeration of the next visible disc, top-k internal actions,
and a hard work budget.

### Mode-specific scoring correction

On 2026-08-15, an initial comparison with a published Sequence-mode scoring
table incorrectly changed the level award from 17,000 to 7,000. A subsequent
mode-level audit found the error: 7,000 is the bonus for the 30-drop
Classic/Sequence rules, while the five-drop, numbered-disc-only Blitz rules
(the mode called Hardcore in the original app) award **17,000**. The
[open-source Drop7 clone](https://github.com/oddlord/html5-js-drop7) documents
both values and implements the branch directly in `nextLevelPre`.
`src/core/native/engine.hpp` and the TypeScript engine therefore use 17,000 and retain an
explicit regression test.

Experiments below that identify `levelBonus: 7000` are preserved as historical
Sequence-scored evidence. Their board trajectories, move counts, legality,
memory measurements, and within-cohort survival comparisons remain useful,
but their absolute scores and score-sensitive choices are not valid evidence
for the Hardcore million-point qualification target. New candidate selection
and the one-shot final benchmark use the corrected 17,000-point award.

## Build and verify

Run commands from the repository root:

```sh
clang++ -O3 -std=c++20 -pthread -Wall -Wextra -Werror \
  approaches/ntuple-rl/native-suite/native.cpp -o /tmp/drop7_native

node --disable-warning=MODULE_TYPELESS_PACKAGE_JSON \
  --experimental-strip-types approaches/baselines-diagnostics/native-parity/main.ts \
  --native /tmp/drop7_native --seed-start 762314752 --seed-count 256

/tmp/drop7_native --benchmark --games 100000 --max-moves 500 \
  --seed-start 0xa5700000
/tmp/drop7_native --gradient-check
/tmp/drop7_native --ntuple-self-test
/tmp/drop7_native --ntuple-search-self-test
```

The parity sweep compares JSON traces byte-for-byte, including the board, scores,
levels, simultaneous chain waves, and gray-disc reveals. The current sweep covers
256 seeds and 6,852 transitions exactly. On the development machine the native
engine ran 2,673,362 moves in 0.927 seconds (2.88 million moves/second).

The n-tuple tests check deterministic evaluation, reflection-safe values and
actions, exact first-reveal stratification, and enforcement of the sparse-search
work bound.

## Reproduce the n-tuple baseline

First train the translation-sharing base on 100,000 games with an episodic
lambda-return. `U(board, movesRemaining)` is a chance-state value: it intentionally
does not include the unknown next visible disc.

```sh
/tmp/drop7_native --train-ntuple --chance-state \
  --games 100000 --probe-games 64 --report-every 10000 --max-moves 500 \
  --chance-samples 7 --learning-rate 0.005 --epsilon 0 \
  --gamma 1 --lambda 1 \
  --training-seed-start 0x3d700000 --probe-seed-start 0x4d700000 \
  --checkpoint /tmp/drop7-ntuple-chance-l1-e0-a005-100k.bin
```

Then warm-start a hierarchical model and train its shared and absolute-position
features for another 400,000 games on the immediately following seeds:

```sh
/tmp/drop7_native --train-ntuple --chance-state --hierarchical \
  --warm-start-shared /tmp/drop7-ntuple-chance-l1-e0-a005-100k.bin \
  --games 400000 --probe-games 64 --report-every 100000 --max-moves 500 \
  --chance-samples 7 --learning-rate 0.005 --epsilon 0 \
  --gamma 1 --lambda 1 \
  --training-seed-start 0x3d7186a0 --probe-seed-start 0x4d700000 \
  --checkpoint /tmp/drop7-ntuple-chance-hierarchical-l1-500k.bin
```

The hierarchical model contains 5.45 million float weights (21.8 MB; a
21,800,016-byte checkpoint). Its measured peak resident memory was about 23.5 MB.
Checkpoint files live in `/tmp` and are experiment artifacts, not repository
assets.

## Reproduce the n-tuple sparse-search candidate

The root always considers every legal move. At deeper decision nodes, candidates
are ranked by a one-step value and only the best two are expanded. Each action
uses three stratified gray-disc reveal samples; every following visible disc is
enumerated exactly. The search is deterministic for a given observable state and
does not inspect the game seed.

```sh
/tmp/drop7_native --benchmark-ntuple-search \
  --checkpoint /tmp/drop7-ntuple-chance-hierarchical-l1-500k.bin \
  --depth 2 --reveal-samples 3 --internal-action-width 2 --max-work 10000 \
  --games 64 --max-moves 500 --seed-start 0x4d700000
```

On the fixed 64-game development probe this configuration produced:

| Policy | Mean score | Mean moves | Min / max score | Mean work / move |
| --- | ---: | ---: | ---: | ---: |
| Hierarchical n-tuple, greedy | 182,057.734 | 56.969 | 85,592 / 511,078 | - |
| Sparse search, depth 2 | **232,107.156** | **70.766** | 102,565 / 568,667 | 2,896.975 |
| Sparse search, depth 3 | 227,975.859 | 69.141 | 85,669 / 493,564 | 36,376 |

Depth 2 completed in 6.015 seconds with zero incomplete decisions. Depth 3 took
75.3 seconds and scored lower, so the extra expansion was rejected rather than
adopted merely because it searched deeper.

## Run the hand-crafted blend experiment

This lab loads the native hierarchical checkpoint in TypeScript, calibrates its
scale against the phase-horizon heuristic, selects a blend coefficient only on
the `0x5d70...` tuning games, and evaluates the frozen choice on the `0x4d70...`
development probe:

```sh
node --disable-warning=MODULE_TYPELESS_PACKAGE_JSON \
  --experimental-strip-types approaches/ntuple-rl/phase-blend/main.ts \
  --checkpoint /tmp/drop7-ntuple-chance-hierarchical-l1-500k.bin \
  --training-games 8 --probe-games 64 --scale-games 24 \
  --depth 2 --chance-samples 3 --max-work 100000 --max-moves 500
```

The tuning means for learned-value coefficients `0`, `0.25`, `0.5`, and `1` were
173,413.5, 186,019.75, 187,391.375, and 190,087.25. Although coefficient `1` won
that tuning set, it reached only 217,332.672 on the fixed probe, below the native
n-tuple-only depth-2 result. The blend is therefore recorded as a rejected theory,
not part of the selected policy.

## Seed discipline

- `0x2d...` and `0x2e...`: parity, self-test, and scale calibration.
- `0x3d...`: model fitting and historical training experiments.
- `0x3e...`: fresh policy screens and gated confirmations; every experiment gets
  a disjoint, predeclared subrange.
- `0x4d70...` and `0x5d70...`: historical development/tuning ranges that have
  already been observed and are no longer eligible as validation evidence.
- `0x7d000000...0x7d00ffff`: protected validation bank; select the first
  never-used aligned cohort only after a candidate is frozen.
- `0xd7000000...0xd70000ff`: one-shot 256-game final range.

Keep fitting, screens, confirmations, validation, and the final cohort separate.
Do not tune a model or search parameter after observing a sealed range. The
million-point claim requires a frozen candidate, at least 1,000 allowed moves,
explicit censoring statistics, and one run of the final 256-game range.

## Conservative distributional fitted-policy lab

`approaches/value-policy-learning/conservative-fitted-policy-iteration/cfpi.cpp` is a standalone falsification lab for conservative
distributional fitted policy iteration. It uses the exact engine and the frozen
depth-3/five-stratum phase-safety policy in `src/core/native/public-behavior.hpp`.

```sh
clang++ -O3 -std=c++20 -Wall -Wextra -Werror \
  approaches/value-policy-learning/conservative-fitted-policy-iteration/cfpi.cpp -o /tmp/drop7_cfpi
/tmp/drop7_cfpi --self-test

/tmp/drop7_cfpi --pilot --iterations 2 \
  --behavior-games 1 --improved-games 1 --stage-games 4 \
  --max-moves 500 --collection-stride 3 --epochs 3 \
  --maximum-states 500 --minimum-margin 5 \
  --maximum-modeled-transitions 1000000
```

The model has four bootstrap members and 101 lifetime atoms spanning 0–500
moves in five-move increments. Each collected state enumerates every legal
action under seven common, exactly stratified gray-reveal and following-disc
outcomes. Frozen-member rotation separates online action selection from target
evaluation. Auxiliary heads predict next-rise survival, five-move clear and
reveal throughput, top risk, and a potential-shaped return. The shaping reward
is only `Phi(next) - Phi(current)`, so it telescopes; the categorical lifetime
target remains unshaped.

The exact phase-safety action is always retained. An alternative is deployed
only when its four-member lower confidence margin over that action remains
positive after an additional epistemic floor. Five-move continuations provide
only auxiliary labels; they are not presented as a substitute for a terminal
return. Observed phase-safety terminal trajectories anchor the lifetime
distribution with Monte Carlo returns.

The initial staged result rejected aggressive switching. With a five-move
epistemic floor, iteration two scored 224,621 versus 257,423 for paired behavior
games, with a -134,808 score lower confidence bound. A ten-move floor made zero
switches and exactly retained behavior. This consumed only 14,164 modeled
training transitions and about 58.2 MB peak RSS, so the lab stopped well before
the 20-million-transition cap. It did not run the 64-game qualification gate and
did not read development-probe, validation, or final-test seeds.

## Direct Monte Carlo behavior-value pilot

`approaches/value-policy-learning/monte-carlo-value/mc-value-policy.cpp` tests a simpler policy-improvement hypothesis without
any Bellman bootstrap. It runs complete games with the exact depth-3/five-stratum
phase-safety behavior and labels every observable state with its actual remaining
lifetime plus survival-at-25 and survival-at-50 indicators. A reflection-safe
four-member state-value ensemble has no action input.

At deployment the exact phase-safety action remains the baseline. Every legal
action is evaluated from seven common, stratified reveal and following-disc
successors. A challenger must have a positive paired ensemble lower confidence
margin, adequate training-feature support relative to the behavior successor,
and bounded ensemble disagreement.

```sh
clang++ -O3 -std=c++20 -Wall -Wextra -Werror \
  approaches/value-policy-learning/monte-carlo-value/mc-value-policy.cpp -o /tmp/drop7_mc_value_policy
/tmp/drop7_mc_value_policy --self-test

/tmp/drop7_mc_value_policy --pilot \
  --training-games 8 --stage-games 8 --epochs 25 --max-moves 500 \
  --minimum-margin 3 --minimum-support 2 --support-ratio 0.8 \
  --maximum-disagreement 35
```

The first required paired training-only stage decisively rejected the method.
The eight-game heldout behavior mean was 246,447.875 points and 75 moves; the
value policy reached only 122,100.125 and 40 moves. Its paired 95% lower bounds
were -223,447 points and -62.904 moves, with clear/reveal throughput also falling
from 1.888/1.023 to 1.516/0.778. The run used 634 terminal MC labels, 15,029
counterfactual transitions, 5.36 MB peak RSS, and only `0x3d...`/`0x3e...` seeds.
It stopped after this first failure without tuning or reading any probe,
validation, or final-test range.

## Nonlinear engineered-feature evolution

`approaches/heuristic-search/evolution/nonlinear-evolution.cpp` reuses the audited 40 action features from the
linear evolution experiment, including immediate clear/reveal/chain outcomes,
queue and height risk, build/release readiness, cover access, low-number clogs,
and chance dispersion. After standardization, a 12-unit tanh network with 505
parameters scores each action.

The bounded funnel first clones exact depth-3/five-stratum phase-safety actions.
Antithetic NES with rotating common training seeds is implemented, but it may run
only if the cloned network reaches the imitation thresholds, at least 85% of the
paired behavior score, at least 250,000 points, and does not lose mean moves.

```sh
clang++ -O3 -std=c++20 -pthread -Wall -Wextra -Werror \
  approaches/heuristic-search/evolution/nonlinear-evolution.cpp \
  -o /tmp/drop7_nonlinear_evolution
/tmp/drop7_nonlinear_evolution --self-test

/tmp/drop7_nonlinear_evolution --pilot \
  --clone-games 4 --holdout-games 2 --clone-epochs 80 \
  --clone-chance-probes 3 --policy-chance-probes 7 \
  --screen-games 4 --confirmation-games 8 --max-moves 500 \
  --generations 3 --antithetic-pairs 6 --evolution-games 4 \
  --clone-learning-rate 0.003 --sigma 0.05 --nes-step 0.03 \
  --screen-ratio 0.85 --minimum-screen-score 250000
```

The clone fit 96.86% of 255 training actions but only 43.40% of 235 disjoint
teacher actions, showing substantial state-distribution overfit. It then failed
the first paired four-game screen: exact behavior scored 307,222.75 over 91.25
moves, while the nonlinear policy scored 148,349.25 over 47.5 moves. Clear and
reveal throughput fell from 1.956/1.063 to 1.700/0.932, and the paired score and
move lower bounds were -441,770 and -121.882. The funnel therefore stopped before
NES or confirmation. Runtime was 30.4 seconds with 3.81 MiB peak RSS. Only
`0x3d...` cloning/holdout seeds and `0x3e...` screen seeds were used; protected
probe, validation, and final ranges were not read.

## Survival-value scale experiment

`approaches/value-policy-learning/monte-carlo-value/survival-value-scale.cpp` tests whether a compact, reflection-safe value
ensemble can learn useful long-horizon information from substantially more
exact phase-safety play before it is allowed to affect a move. It collects
whole depth-3/five-stratum trajectories in parallel and directly labels each
public state with remaining lifetime and death-within-25/50 outcomes. The split
holds out complete games rather than individual positions.

If, and only if, held-out death AUC is at least 0.75 for both horizons and
held-out lifetime Spearman correlation is at least 0.60, a conservative root
policy may compare all legal actions over five common stratified successors.
It retains exact behavior unless every ensemble member favors the challenger
by eight moves, support is adequate both absolutely and relative to behavior,
and ensemble disagreement is bounded. A four-seed screen precedes an eight-seed
confirmation, and both score and lifetime must improve.

```sh
clang++ -O3 -std=c++20 -pthread -Wall -Wextra -Werror \
  approaches/value-policy-learning/monte-carlo-value/survival-value-scale.cpp \
  -o /tmp/drop7_survival_value_scale
/tmp/drop7_survival_value_scale --self-test

/tmp/drop7_survival_value_scale --run \
  --trajectories 64 --holdout-games 16 --epochs 30 --max-moves 500 \
  --threads 8 --screen-games 4 --confirmation-games 8 \
  --learning-rate 0.03 --switch-margin 8 --minimum-support 8 \
  --support-ratio 0.8 --maximum-disagreement 25
```

The single predeclared run collected 64 uncensored trajectories and 5,717
labels: 4,212 labels from 48 training games and 1,505 from 16 held-out games.
The exact behavior averaged 299,059.75 points and 89.328 moves. Training MAE
was 12.636 moves, death-within-25/50 AUC was 0.990/0.963, and lifetime Spearman
correlation was 0.911. On held-out whole games, however, lifetime MAE rose to
30.773 moves and Spearman correlation fell to 0.557. Held-out death AUC remained
0.920/0.756; Brier scores were 0.099/0.200 and expected calibration errors were
0.047/0.063. Mean held-out feature support was 1,641.65 with a 1,269.29 tenth
percentile.

The held-out lifetime ranking therefore missed the preregistered 0.60 gate,
despite narrowly passing both AUC gates. The experiment stopped at prediction;
it made no policy switches and read no screen or confirmation seeds. Collection
used only `0x3d704000...`, while the unused policy ranges were reserved within
`0x3e...`. Runtime was 41.3 seconds, peak RSS was 10 MiB, and the Werror and
ASan/UBSan self-test builds passed. The experiment was not retuned or repeated.

## Structured multi-head NNUE value experiment

`approaches/value-policy-learning/structured-nnue/structured-value-nnue.cpp` is an architecturally distinct supervised
test of long-horizon state value. Its sparse input activates a separate raw
one-hot embedding for every board position and token, plus next-disc and
moves-until-rise embeddings. Twenty standardized phase metrics describe chain
readiness, quiet build inventory, low-number clogs, cover exposure, and height
risk. These feed a fixed 128/64 leaky-ReLU trunk with normalized remaining
lifetime and survival-at-25/50 heads.

Training presents both orientations of every state, while inference averages
the two orientations exactly. Only the board, next disc, and moves-until-rise
are observable to the model. The fixed corpus contains 128 fitting games and 32
whole-game holdouts; indices divisible by five are held out before fitting. A
training-only affine lifetime calibration and logistic survival calibration are
applied after 24 fixed epochs. The 75,395-parameter model occupies 301,764 bytes
including normalization and calibration metadata.

Before any policy use, both held-out death AUCs must reach 0.80 and held-out
lifetime Spearman correlation must reach 0.65. Had that gate passed, a candidate
action would have needed an eight-move paired lower bound over five common
stratified successors, adequate absolute and relative embedding support, and a
bounded forward/reflected raw prediction gap. A four-game screen would then
have required both score and moves to improve before an eight-game confirmation.

```sh
clang++ -O3 -std=c++20 -pthread -Wall -Wextra -Werror \
  approaches/value-policy-learning/structured-nnue/structured-value-nnue.cpp \
  -o /tmp/drop7_structured_value_nnue
/tmp/drop7_structured_value_nnue --self-test
/tmp/drop7_structured_value_nnue --run
```

The one permitted run collected 160 uncensored exact depth-3/five-stratum
trajectories from `0x3d706000...`: 9,800 fitting labels and 2,132 held-out
labels. Exact behavior averaged 247,202.869 points and 74.575 moves, consuming
637,715,230 teacher-work units. Training MAE was 20.391 moves, death-within-25/50
AUC was 0.999/0.978, and lifetime Spearman correlation was 0.887.

Generalization was substantially weaker. Held-out MAE was 29.524 moves,
death-within-25/50 AUC was 0.855/0.614, and lifetime Spearman correlation was
0.510. Held-out Brier scores were 0.192/0.314 and expected calibration errors
were 0.186/0.286. The held-out mean reflection-orientation gap was 3.384 moves,
so symmetry disagreement was not the primary failure.

Both the 50-move AUC and ranking gates failed. The experiment therefore stopped
at prediction without reading the reserved `0x3e7b...` screen or `0x3e7c...`
confirmation seeds, and it was not retuned or repeated. Runtime was 91.0 seconds
with 9.28 MiB peak RSS. Optimized Werror and ASan/UBSan self-test builds passed,
including public-state isolation, exact reflection invariance, engineered-metric
symmetry, a finite-difference gradient check, and learner wiring.

## Denoised stochastic public-state value

`approaches/value-policy-learning/denoised-value/denoised-stochastic-value.cpp` tests whether the earlier value models
were limited by their single realized terminal target. Exact depth-3/five-stratum
games are used only to collect public states every three moves. Each canonical,
deduplicated state is then reconstructed without score, level, move index, game
seed, or history and evaluated by 32 independent stochastic futures capped at
50 moves. Every future uses the same deterministic public depth-1/one-stratum
phase-greedy continuation policy; only reveal and future-disc randomness varies.

The 64 roll-in games are split by whole game before state deduplication: 48
training games and 16 holdouts. Canonical public states occurring in training
are excluded from holdout, and training and holdout labels use separate random
domains. The resulting targets are expected capped lifetime and the probability
of remaining alive after 25 and 50 moves.

A 64/32 reflection-averaged NNUE consumes raw position/token embeddings,
next-disc and rise-phase embeddings, and 16 standardized phase metrics. It has
35,395 parameters. Its deterministic checkpoint has a 141,732-byte payload
including normalization and training-only calibration metadata, plus a
48-byte versioned architecture/checksum header, for 141,780 bytes total.
Before policy use, held-out lifetime
Spearman correlation must be at least 0.70, both soft survival AUCs at least
0.80, and both expected calibration errors at most 0.10.

If those gates pass, the exact phase-safety action remains the baseline. Every
legal alternative is evaluated over five common stratified successors and must
clear a three-move paired lower bound, absolute and relative training-support
checks, and a forward/reflected prediction-agreement check. A four-game screen
must improve both score and moves before an eight-game confirmation is read.

```sh
clang++ -O3 -std=c++20 -pthread -Wall -Wextra -Werror \
  approaches/value-policy-learning/denoised-value/denoised-stochastic-value.cpp \
  -o /tmp/drop7_denoised_stochastic_value
/tmp/drop7_denoised_stochastic_value --self-test
/tmp/drop7_denoised_stochastic_value --run
/tmp/drop7_denoised_stochastic_value --train-checkpoint \
  artifacts/models/denoised-value/v1.bin
/tmp/drop7_denoised_stochastic_value --verify-checkpoint \
  artifacts/models/denoised-value/v1.bin
```

The single fixed run collected 1,071 training states and 319 held-out disjoint
states from `0x3d706800...`; 17 cross-split duplicates were removed. Across
44,480 label rollouts, the mean lifetime standard error was 1.304 training and
1.192 held out, with 90th percentiles of 1.877 and 1.832 moves. Label generation
simulated 1,156,283 moves. All 64 exact roll-ins terminated naturally and
averaged 70.906 moves. The original run reported 234,260.75 points with the
17,000-point five-drop Hardcore/Blitz level bonus. That scoring constant is
now verified as correct for this mode, but the result remains archival
development evidence rather than an independent qualification result.

The denoised model generalized much better than the terminal-trajectory value
models. Held-out lifetime MAE was 2.744 moves, RMSE was 3.530, and Spearman
correlation was 0.951. Survival-at-25/50 soft AUC was 0.920/0.809, Brier error
was 0.0136/0.0032, and expected calibration error was 0.0210/0.0061. All
predeclared prediction gates passed.

The four-game `0x3e820000...` screen also passed both deployment criteria in
that legacy-scoring run: exact behavior reached 78.75 moves and the conservative
value policy reached 81.25 moves with 0.75 switches per game. The gated
eight-game `0x3e830000...` confirmation improved mean behavior from 79.5 to
88.125 moves, with 2.625 switches per game. Clear/reveal throughput improved
from 1.901/1.033 to 1.996/1.112 per move. Its reported absolute point totals
use the verified 17,000-point Hardcore bonus, but the development design and
small cohort still prevent treating them as final evidence.

The confirmation's paired 95% lower bounds remained negative at -63,414 points
and -17.404 moves, so this is promising training-range evidence rather than a
high-confidence final result. The experiment was not repeated and no protected
seed family was read. Runtime was 121.3 seconds with 5.72 MiB peak RSS.
Optimized Werror and ASan/UBSan builds passed public-state label, independent
random-domain, exact reflection, engineered-symmetry, gradient, and learner
wiring tests.

The saved `artifacts/models/denoised-value/v1.bin` checkpoint is a deterministic retrain on
the same already-used corpus. It contains 35,395 parameters, is 141,780 bytes,
and has payload checksum `1239007257`. Round-trip equality, architecture
metadata, payload corruption rejection, and truncation rejection pass in both
optimized Werror and ASan/UBSan self-tests. The scoring correction does not
alter this public-state lifetime checkpoint, and it was reverified after the
engine change.

## Denoised-value veto of the guided ensemble (rejected)

`approaches/value-policy-learning/denoised-value/denoised-guided-veto.cpp` is the isolated preregistered combination
test. It freezes the three-member depth-5/K3 guided root-Q ensemble as its
baseline and computes a verified full-width exact depth-3/five-stratum fallback
on every move. Only when those two actions disagree does it evaluate their
successors with the saved denoised model over five common strata. The model may
select only the exact fallback, and only when its expected capped-50 lifetime
exceeds the ensemble's by the fixed held-out MAE of 2.744151 moves; it can never
nominate a third action.

```sh
clang++ -O3 -std=c++20 -pthread -Wall -Wextra -Werror \
  approaches/value-policy-learning/denoised-value/denoised-guided-veto.cpp \
  -o /tmp/drop7_denoised_guided_veto
/tmp/drop7_denoised_guided_veto --self-test
/tmp/drop7_denoised_guided_veto --run

clang++ -O1 -g -std=c++20 -pthread -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  approaches/value-policy-learning/denoised-value/denoised-guided-veto.cpp \
  -o /tmp/drop7_denoised_guided_veto_san
/tmp/drop7_denoised_guided_veto_san --self-test
```

The historical 7,000-point Sequence-scored four-game
`0x3e840000...003` screen passed both means:
the pure ensemble averaged 126,146 points and 85.5 moves, while the veto policy
averaged 184,649.25 points and 121.25 moves. The paired deltas were +58,503.25
points and +35.75 moves, but their 95% lower bounds were already negative at
-27,421.531 and -14.210. Seven of 485 moves were vetoes (1.443%), or 8.537% of
the 82 ensemble/exact disagreements.

The unlocked eight-game `0x3e850000...007` confirmation did not replicate.
The ensemble averaged 164,266 points and 110.625 moves; the veto policy averaged
161,660.125 points and 108.75 moves. Paired deltas were -2,605.875 points and
-1.875 moves, with 95% lower bounds of -66,414.237 and -42.023. The candidate
vetoed 12 of 870 moves (1.379%), or 6.452% of 186 disagreements; the mean model
advantage on a veto was 4.077 capped-lifetime moves. Confirmation used
47,291,681 units of exact-search work, 1,860 successor transitions/inferences,
and 23,927,040 value-network multiply-adds. The combined Q/value artifacts
occupy 233,836 bytes. Total screen-plus-confirmation wall time was 1,882.9
seconds and peak RSS was 7,798,784 bytes.

This combination is rejected. The screen was a false positive, no threshold
was swept, `0x3e82...`/`0x3e83...` were not reused, and the separately reserved
absolute comparison against exact depth-3 was not opened. Optimized Werror and
ASan/UBSan self-tests passed checkpoint integrity, public-state isolation,
reflection invariance, common-strata evaluation, exact-fallback completion,
threshold behavior, legal actions, and the no-third-action invariant.

## Counterfactual-successor NNUE (rejected)

`approaches/value-policy-learning/structured-nnue/counterfactual-successor-nnue.cpp` broadens value-model support by taking
exact depth-3 roll-in states, enumerating every legal action through three common
public chance strata, canonicalizing/deduplicating the successors, and assigning
each successor eight independent 75-move public continuation labels. Splits are
by source game and have no canonical state overlap.

The model learned absolute successor lifetime: on 242 held-out successors its
Spearman correlation was 0.839 and MAE was 3.888 moves. But the relevant
within-root signal was much weaker. Direct top-action accuracy was 15.4%; using
the NNUE as a full-width depth-3 leaf raised it only to 30.8%, with pairwise
accuracy 62.0% and 1.90 moves of label regret.

The fresh historical 7,000-point Sequence-scored
`0x3e870000...03` screen therefore failed decisively.
Exact depth 3 averaged 166,112.25 points and 111.5 moves, while the NNUE-leaf
search averaged 102,916.25 and 72.5, a paired loss of 63,196 points and 39 moves.
Confirmation seeds were not read. The result is a useful warning: strong global
state-value correlation can coexist with poor discrimination among the sibling
states that determine an action. Future learned evaluators must train and gate
on grouped, within-position ranking evidence. Optimized `-Werror` and
ASan/UBSan self-tests passed.

## Privileged-future oracle distillation

`approaches/oracle-curriculum/oracle-distillation/oracle-distill.cpp` is a leakage-controlled native follow-up to
`approaches/oracle-curriculum/perfect-information-oracle/main.ts` and `approaches/oracle-curriculum/oracle-dagger/main.ts`. The old
oracle is deliberately unfair: it knows the exact future visible-disc and gray
reveal streams for one game, then runs a receding depth-4/beam-128 search. As an
upper-bound check, the original TypeScript implementation scored 2,079,579
points and reached the 500-move cap on seed `0x3d700000`; it is not a deployable
policy.

The native lab asks whether aggregating those privileged actions across many
games produces a fair student. It labels both oracle roll-ins and exact
depth-3/five-stratum behavior roll-ins. Whole games, not shuffled positions,
form the held-out label split. Before an example reaches the 384,540-byte sparse
614/128/128/7 network, a hard boundary drops the game seed, future tape, reveal
RNG, score, level, and move count. The model sees only the board, visible next
disc, and moves until the next rise. Lexicographic board canonicalization makes
the policy reflection-safe.

The label gates were fixed at held-out top-1 accuracy at least 0.30, top-2 at
least 0.55, and cross-entropy at most 1.75. Deployment was also fixed in advance:
the student could override exact behavior only with probability at least 0.40,
a 0.12 probability advantage, and a candidate no more than 10% of the exact
root-Q range below behavior, with a 5,000-point tolerance floor. An eight-game
screen had to improve both mean score and moves before the 16-game confirmation
range could be read.

```sh
clang++ -O3 -std=c++20 -pthread -Wall -Wextra -Werror \
  approaches/oracle-curriculum/oracle-distillation/oracle-distill.cpp \
  -o /tmp/drop7_oracle_distill
/tmp/drop7_oracle_distill --self-test
/tmp/drop7_oracle_distill --run

clang++ -O1 -g -std=c++20 -pthread -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  approaches/oracle-curriculum/oracle-distillation/oracle-distill.cpp \
  -o /tmp/drop7_oracle_distill_san
/tmp/drop7_oracle_distill_san --self-test
```

The frozen run used oracle/behavior fitting seeds `0x3d7a0000...` and
`0x3d7b0000...`, and whole-game label holdouts `0x3d7c0000...` and
`0x3d7d0000...`. It fit 5,110 labels and evaluated 1,629 held-out labels. The
oracle fitting roll-ins all reached the 200-move label cap and averaged
829,516.75 points. Despite training cross-entropy reaching 0.480, held-out
top-1/top-2 accuracy was only 0.218/0.386 and held-out cross-entropy was 3.282;
oracle and behavior holdouts were similarly weak. The label gate therefore
failed, demonstrating strong seed-tape/state-distribution overfit.

For completeness, the frozen policy screen still ran on `0x3d7e0000...07`.
Exact behavior averaged 261,871.875 points over 79.25 moves; the hybrid averaged
283,850.625 over 84.5 moves, so its +21,978.75 points and +5.25 moves unlocked
the predeclared confirmation. Both paired 95% lower bounds were nevertheless
negative. On `0x3d7f0000...0f`, exact behavior averaged 249,816.875 points and
75.375 moves while the hybrid fell to 187,542.625 and 57.25 moves. Paired deltas
were -62,274.25 points and -18.125 moves, with lower bounds -118,363.971 and
-33.322. The hybrid switched on 18.45% of confirmation moves.

The apparent screen win was a false positive, the confirmation failed both
means, and the oracle-distilled model is rejected. The single run took 296.3
seconds. Optimized Werror and ASan/UBSan self-tests passed, including the public
state boundary, reflection, legal masking, deterministic native oracle, parity
of its initial reference action, and a guard against non-training seed ranges.
No `0x4d`, `0x5d`, `0x7d`, or `0xd7` seed was read, and the experiment was not
retuned after its failure.

## Independent root-quadrature ensemble (rejected)

`approaches/heuristic-search/exact-search/exact-root-ensemble.cpp` isolates chance-estimation noise without any
learned branch pruning. Each member expands every legal action through three
plies and five stratified chance samples; three fixed, independent public-state
sampling salts produce complete root-Q arrays, which are averaged before the
move is selected. Here “exact” means full-width decision nodes—the sampled
gray-reveal chance nodes remain an approximation.

The then-assumed 7,000-point Sequence-scored engine and fresh `0x3d70d...`
training seeds were
used. A four-game screen passed (128,858.75 points and 88.75 moves versus
96,304 and 67.5), but the gated eight-game confirmation decisively reversed:
the ensemble averaged 117,066.5 points and 80.625 moves versus 162,492.125 and
109.375 for the single default quadrature, losing seven of eight paired games.
It also used 157,047 work units per move versus 53,778. The result shows that
reducing sampling variance does not repair a biased horizon evaluator and can
remove accidental regularization from one fixed quadrature. The ensemble is
rejected. Optimized `-Werror` and ASan/UBSan self-tests passed determinism,
reflection, public-state isolation, legality, resource bounds, scoring, and
single-member action parity.

## Learned deeper-search override (rejected)

`approaches/tree-search/nnue-guided/nnue-guided-search.cpp` also tested an “exact first” safeguard: complete
the full-width sampled-chance depth-3 search, then spend the remaining fixed
250,000-work budget on learned-guided depth-4/5 iterations. A deeper result was
allowed to replace the depth-3 action only when that entire iteration completed.
On fresh historical 7,000-point Sequence-scored seeds
`0x3d70b000...03`, the safeguarded policy averaged
82,169.25 points and 58.75 moves versus 87,743.75 and 62 for depth 3. It spent
235,654 work units per move, completed a deeper iteration on 22.1% of moves, and
changed the depth-3 action on only 4.68% of moves. Because even the screen mean
was worse, the reserved confirmation seeds were not read. This rejects the
override rule; a rare learned deeper action can still magnify leaf-value error.

## Root reveal quadrature (rejected)

`approaches/tree-search/nnue-guided/nnue-root-quadrature.cpp` replaced a single fixed five-outcome root sample
with a product quadrature: three first-reveal strata by all seven next visible
disc values, followed by three joint interior strata. Its screen against a weak
learned-guided comparator looked favorable, but the predeclared absolute gate
was the full-width sampled-chance depth-3 policy. On fresh historical
7,000-point Sequence-scored seeds
`0x3d70c000...03`, depth 3 averaged 205,001.25 points and 132.5 moves while the
larger quadrature averaged 179,243 and 120. It used about 239,000 work units per
move versus 58,000 and failed the screen, so confirmation was not run. More root
chance coverage did not overcome the biased horizon value.

## Fully completed depth 4 (rejected)

`approaches/heuristic-search/exact-search/exact-depth4.cpp` removes learned ranking and branch reduction entirely.
It expands every legal decision action with the same five-stratum public chance
sampler at depths 3 and 4. Compile-time worst-case bounds prove that depth 4 fits
within 3,134,950 work units and 45,430 cached nodes, and every played decision
hard-asserts that the requested iteration completed.

On fresh historical 7,000-point Sequence-scored seeds
`0x3d70e000...03`, depth 3 averaged 149,621.25
points and 103.75 moves; depth 4 averaged 124,172.25 and 85. The paired mean
difference was -25,449 points and -18.75 moves, with a 2-2 record. Depth 4 used
1,303,982 work units per move—about 24.1 times depth 3—and achieved only 0.943
moves per second. It failed the screen, so confirmation seeds were not read.
Optimized `-Werror` and ASan/UBSan self-tests passed. A fully completed extra ply
is therefore not the missing ingredient: the phase heuristic at the horizon is
the central source of bias.

## Complete-game phase-weight evolution (rejected)

`approaches/heuristic-search/evolution/phase-weight-evolution.cpp` perturbs seven interpretable groups in the
depth-3 leaf evaluator and ranks them on complete-game survival and lower-half
score. Six antithetic generations on historical training seeds independently
increased the magnitude of cover debt by 18.4% and trigger/rise readiness by
26.4%, while leaving release/exposure almost unchanged. Those directions agree
with the later matched oracle-topology audit.

The frozen vector nevertheless failed its historical 7,000-point
Sequence-scored eight-game screen:
the default evaluator averaged 107,709.75 points and 75.125 moves, while the
evolved vector averaged 105,534.875 and 72.5. Confirmation was not run. Coarse
global rescaling can recover plausible directions but cannot express the
state- and action-conditional interactions needed for reliable improvement.

## Privileged topology audit

`approaches/oracle-curriculum/topology/oracle-topology-audit.cpp` compares public depth-3 play with an
analysis-only future-aware depth-4/beam-128 oracle, then matches their states by
seed, 20-move band, rise phase, occupancy, and maximum height. The oracle input
is never available to a deployed policy; the comparison is only a source of
candidate public-state features.

On fresh historical 7,000-point Sequence-scored seeds
`0x3d70f000...0f`, depth 3 averaged 90,273
points and 63.625 moves. The privileged oracle averaged 429,182.5 and reached
the 200-move cap in all 16 games, winning every pair. Its clear/reveal rates
were 2.354/1.386 per move versus 1.792/0.960. Feature directions were required
to agree independently in both eight-seed halves. The stable signals reward
direct and latent trigger readiness, rise triggers, stored unfired high
numbers, and cracked-cover exposure; they penalize solid-cover count and
altitude, projected occupancy debt, dead low numbers, low caps, and adjacent
ones. Generic cliff access and strong repeated-column cohesion did not pass the
split-half stability rule and are not supported additions.

As a separate feasibility ceiling, the same privileged oracle was run to a
500-move cap on twelve additional training-only seeds
(`0x3d950000...0b`). It reached the cap in all twelve games and averaged
1,058,931.5 points with the historical 7,000-point Sequence level bonus.
Public depth 3
averaged 132,979 points and 90.5 moves on the paired cohort. This establishes
that a million points is mechanically attainable in the simulator, but it is
not evidence for a deployable policy: the oracle sees the realized future disc
and reveal streams, and its mean is censored at 500 moves.

## Accessible-energy residual (rejected)

`approaches/oracle-curriculum/accessible-energy/accessible-energy-lab.cpp` turns the split-stable oracle signals into a
public, reflection-safe feature family. It measures stored unfired numbers,
direct and latent trigger readiness, cracked-cover access, projected occupancy,
dead lows, low caps, and adjacent-low congestion. A ridge residual is fitted
only after calibrating the existing phase evaluator, and source games—not
individual positions—define the held-out split.

On 601 held-out action successors, the residual improved calibrated score
prediction R-squared from 0.580 to 0.615, within-position top-action accuracy
from 22.9% to 31.3%, and pairwise ranking accuracy from 49.2% to 52.2%. Stored
energy was the strongest feature group. These modest diagnostic gains did not
survive the complete-game gate: on fresh historical 7,000-point
Sequence-scored seeds
`0x3e890000...07`, exact depth 3 averaged 112,318.5 points and 77.125 moves,
while the residual-guided search averaged 85,768 and 60.625. It lost seven of
eight pairs, so confirmation seeds were not read. The unrestricted leaf blend
is rejected; energy may only be reconsidered as a conservative root tie-break
among actions that repeated search samples cannot distinguish.

## Accessible-energy confidence-set root prior (rejected)

`approaches/oracle-curriculum/accessible-energy/accessible-energy-root-prior.cpp` keeps the exact depth-3 leaf evaluator
unchanged and uses the frozen accessible-energy model only at the root. Three
fixed chance salts produce full-width root-Q arrays; an alternative is eligible
only when its paired one-sided 95% lower bound does not establish it as worse
than the current default-salt action. Accessible energy ranks only that
admissible set, and a singleton returns the exact baseline action.

The fresh `0x3e930000...07` screen rejected this safeguard. Exact depth 3
averaged 105,767.875 points and 73.125 moves, while the root prior averaged
90,300.75 and 60.875, paired losses of 15,467.125 points and 12.25 moves. The
three-salt confidence set was too permissive: it switched 214 of 487 decisions
(43.94%), admitted 31.90% of non-reference alternatives, and averaged 2.828
admissible actions. Realized clear/reveal/wave throughput fell from
1.885/1.032/1.405 to 1.725/0.910/1.244 per move, while work rose from 53,395 to
158,522 units per move. The reserved `0x3e940000...0f` confirmation was not
read, and the admission threshold was not tuned on the failed screen. Strict
`-Werror`, ASan/UBSan, determinism, reflection, public-state, exact-reference,
resource-bound, confidence-rule, and singleton-parity tests passed.

## Synthetic-tape hindsight optimization (rejected)

`approaches/oracle-curriculum/hindsight-planner/hindsight-planner.cpp` tests a public, bounded determinization planner.
For every legal root action it builds the same seven synthetic future tapes
from a canonical public-state hash, runs an independent depth-8/beam-64
clairvoyant continuation on each tape, then selects one root action using a
mean/lower-quartile blend. The real game seed and realized future are not inputs.

This failed its fresh historical 7,000-point Sequence-scored
`0x3e8b0000...03` screen: depth 3 averaged
107,076 points and 72.5 moves, while hindsight optimization averaged 51,500.5
and 37.5. Confirmation seeds were not read. Tape-specific later decisions make
the per-tape root values incompatible and overoptimistic—the familiar strategy
fusion failure of determinization. Optimized `-Werror` and ASan/UBSan self-tests
passed determinism, metadata blindness, reflection, chance stratification,
legality, and the 153,713-transition decision bound.

## Observable-state stochastic UCT (rejected at held-out gate)

`approaches/tree-search/observable-mcts/observable-mcts-lab.cpp` removes the determinization leak entirely.
Nodes contain only the canonical visible board, next disc, moves to the next
rise, terminal flag, and remaining search horizon. Chance successors are
sampled when a state-action edge is visited and stored in an eight-outcome
progressively widened reservoir. Neither the real game seed nor a future tape
is a key or policy input, so later decisions cannot specialize to unrevealed
outcomes. This is the key distinction from privileged hindsight: a
determinization can look strong by giving the root credit for mutually
incompatible tape-conditioned future policies, while stochastic UCT must use
one policy over observable information.

The training-only audit used 64 roots from 32 `0x3da00000...1f` origin games,
independent 32-scenario common-random labels with 60-move public depth-1
continuations, and all twelve combinations of 256/1,024/4,096/16,384
simulations with horizons 8/16/32. The preregistered minimum-regret selection
froze 16,384 simulations and horizon 32. On 32 roots from 16 disjoint
`0x3da10000...0f` games, it achieved 0.6498 pairwise accuracy and 28,420 mean
regret versus exact depth 3's 0.6418 and 44,142, but top-1 was only 11/32 =
0.34375. That missed the 0.35 gate; the next attainable result was 12/32 =
0.375. The `0x3e990000...03` screen and `0x3e9a0000...07` confirmation were
therefore not read, and nothing was retuned.

No search exhausted its arena. Maximum active storage was 7,876,784 bytes and
the fixed arena reservation was 8,257,960 bytes under the 32 MiB cap, with at
most 15,486 nodes and 16,384 sampled outcomes. Strict `-Werror` and ASan/UBSan
self-tests passed deterministic decisions and Q values, reflection, metadata
blindness, chance-boundary equivalence, exact-depth-3 fallback parity,
depth-1 rollout parity, legality, reservoir bounds, and arena bounds.

## Confidence-gated MCTS over fair depth 3 (rejected)

`approaches/tree-search/observable-mcts/fair-mcts-confidence.cpp` keeps the confirmed fair-only depth-3 policy
as the fallback and permits observable-state MCTS to override it only when both
the MCTS Q advantage and visit-share advantage clear one frozen threshold.
Fitting used 64 move-12/24 roots from 32 fair-policy `0x3db00000...1f` origin
games. Each sibling was evaluated on 64 aligned, independently seeded 80-move
public fair-depth-1 continuations; those offline tapes never entered either
policy. The first fitting-only grid, ending at Q margin 0.20 and visit margin
0.10, produced no nonzero rule with at most half of raw MCTS's switches. Before
held-out evaluation, the same conjunctive rule family was widened once and
then frozen.

The selected Q/visit thresholds were 3.2/0.8. Raw MCTS disagreed with fair on
34 of 64 fitting roots; the gate reduced that to 12, but its mean regret was
still worse than fair (29,783.74 versus 25,848.30). On the one allowed set of
32 roots from 16 origin-game-disjoint `0x3db10000...0f` games, the gate switched
six actions. Tie-aware top-1 improved from 0.28125 to 0.34375, but pairwise
accuracy fell from 0.58742 to 0.54755 and regret rose from 28,165.14 to
32,443.13. Raw MCTS was weaker still at 0.21875 top-1, 0.51994 pairwise, and
41,105.89 regret. The held-out gate therefore rejected the override; the
`0x3e9f0000...07` screen and `0x3ea00000...0f` confirmation were not read.

The observable node boundary and 8,257,960-byte per-decision arena are
unchanged. Optimized strict-warning and ASan/UBSan self-tests cover the embedded
fair and MCTS policies, determinism, reflection, metadata blindness, legal
fallback, confidence evidence, and the 32 MiB arena bound.

## Historical fair-only horizon evaluator

`approaches/fair-expectimax/reference/fair-only-horizon.cpp` recovers the fair-only leaf from
`approaches/fair-expectimax/fair-policy/tune.ts` with the five frozen `FAIR_PHASE_BASELINE_WEIGHTS`
overrides from `approaches/fair-expectimax/phase-fair-combination/main.ts`. It intentionally excludes
the later phase residual. The standalone policy is full-width iterative depth
3 with five stratified chance samples, a one-million-work limit, a 40,000-entry
LRU cache, and the same observable-state policy seed and -1,000,000 terminal
utility as the TypeScript experiment.

```sh
clang++ -O3 -std=c++20 -pthread -Wall -Wextra -Werror \
  approaches/fair-expectimax/reference/fair-only-horizon.cpp \
  -o /tmp/drop7_fair_only_horizon
/tmp/drop7_fair_only_horizon --self-test
/tmp/drop7_fair_only_horizon --run

clang++ -O1 -g -std=c++20 -pthread -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  approaches/fair-expectimax/reference/fair-only-horizon.cpp \
  -o /tmp/drop7_fair_only_horizon_san
/tmp/drop7_fair_only_horizon_san --self-test
```

Three deterministic public-state fixtures were evaluated independently in
TypeScript and frozen into the native self-test. The port matches all three
best actions, every root expected score, node/work/cache counts, and completed
depth exactly; maximum fair-leaf and root-value errors are
`3.64e-12` and `1.82e-12`. Reflection and public-state metadata checks also
pass. Optimized Werror and ASan/UBSan builds pass these tests.

The historical 155/160-move pilot was real but not representative. Those two
already-used training seeds cleared 331/351 numbered discs and revealed
186/201 covers, or 2.14/2.19 clears and 1.20/1.26 reveals per move, with maximum
chains of 7/9. The fair leaf gives direct build readiness a weight of 1,600,
keeps latent potential at 700, removes the roughness penalty, and balances that
aggression with height, covered-altitude, low-number, danger-height, rise, and
clog penalties. This can enter a self-sustaining clear/reveal regime. The
300-point revealed-cover override is a transition feature and is inert when
the fair model is used only as a leaf. Adding any tested phase residual broke
the two historical trajectories, reducing them to 55–110 moves. Because the
pilot used only two non-independent training seeds, it was evidence for a
mechanism, not a reliable performance mean. Its 17,000-point bonus is now
verified as correct for five-drop Hardcore/Blitz.

The historical 7,000-point Sequence-scored `0x3e950000...007` screen used a
1,000-move cap and passed both preregistered means. Current CFPI depth 3 averaged 109,336.75 points
and 75.625 moves; fair-only averaged 142,027.125 and 94.375. Paired deltas were
+32,690.375 points and +18.75 moves, although 95% lower bounds were negative at
-64,411.115 and -39.368. Fair-only improved score on five of eight seeds and
moves on four, tied one move count, and lost three. Its clear/reveal throughput
rose from 1.909/1.045 to 2.041/1.155 per move.

That gate opened the one allowed `0x3e960000...00f` confirmation. CFPI averaged
102,612.8125 points and 71.5625 moves; fair-only averaged 119,270.75 and 80.5,
so it passed the stated two-mean rule at +16,657.9375 points and +8.9375 moves.
Fair-only clear/reveal throughput was 1.921/1.064 per move versus
1.866/1.003. Neither cohort was censored. Work was comparable at 53,637 versus
53,685 units per move, the process used 3,915,776 bytes peak RSS, and the full
screen plus confirmation took 46.0 wall seconds.

This is a qualified mean improvement, not a high-confidence uniform win.
Fair-only won only seven of sixteen confirmation pairs; two long 232- and
155-move outcomes supplied much of the gain, while nine pairs lost. Paired 95%
lower bounds remained negative at -33,341.721 points and -21.634 moves. The
evaluator therefore explains the old long games and improves the requested
fresh means, but its knife-edge chain-building behavior remains high variance.
No `0x7d...` or `0xd7...` game seed was read.

## Completed depth 4 with the fair-only leaf

`approaches/fair-expectimax/reference/fair-only-depth4.cpp` holds the qualified fair-only evaluator, public
chance sampler, terminal utility, and full-width action set fixed, changing
only the completed search depth from 3 to 4. Its 3.2-million-work and
60,000-entry cache ceilings exceed compile-time worst cases of 3,134,950 work
and 45,430 cached nodes, so every played D4 action must complete rather than
falling back to a partial iteration. The candidate also records the completed
D3 action on its own trajectory; a switch means the fourth ply changed that
same-state decision.

```sh
clang++ -O3 -std=c++20 -pthread -Wall -Wextra -Werror \
  approaches/fair-expectimax/reference/fair-only-depth4.cpp \
  -o /tmp/drop7_fair_only_depth4
/tmp/drop7_fair_only_depth4 --self-test
/tmp/drop7_fair_only_depth4 --run

clang++ -O1 -g -std=c++20 -pthread -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  approaches/fair-expectimax/reference/fair-only-depth4.cpp \
  -o /tmp/drop7_fair_only_depth4_san
/tmp/drop7_fair_only_depth4_san --self-test
```

An independent TypeScript depth-4 fixture completed with action 4, 1,877,470
work units, 957,740 nodes, 27,360 cached states, and 10,650 cache hits. Native
results match all of those values and every root value exactly; the fixture's
completed D3 action is column 1. Determinism, reflection, public-state isolation,
legal-action, resource-bound, and completion-proof tests pass under optimized
Werror and ASan/UBSan builds.

The historical 7,000-point Sequence-scored `0x3e9b0000...003` screen passed
both means at a 1,000-move cap. Fair D3 averaged 141,028.75 points and 93 moves; completed fair D4 averaged
179,951.75 and 115, paired gains of 38,923 points and 22 moves. D4 won three of
four score pairs, won three move pairs and tied one. The paired 95% lower bounds
were positive at +5,680.278 points and +4.306 moves. Clear/reveal throughput
rose from 1.930/1.048 to 2.122/1.213 per move. D4 changed its completed D3 action
on 156 of 460 decisions (33.91%).

The one allowed `0x3e9c0000...007` confirmation also passed. D3 averaged
102,571.25 points and 71 moves; D4 averaged 176,925.25 and 116.375, for paired
gains of 74,354 points and 45.375 moves. D4 won seven of eight score and move
pairs. The paired 95% lower bounds remained positive at +3,872.088 points and
+1.561 moves. Clear/reveal throughput improved from 1.861/1.023 to
2.040/1.131; numbered clears also retained a positive paired lower bound, while
the reveal-count lower bound was slightly negative. Neither cohort was
censored.

The additional ply is effective with this leaf but expensive. Confirmation D4
used 1,351,113 work units per move versus 52,781 for D3 (25.6x), retained up to
36,105 cached states versus 1,224, and ran at 0.868 versus 24.786 moves per
aggregate game-second. It switched 317 of 931 same-state D3 decisions (34.05%).
Per-column action counts and every paired trajectory are preserved in the JSON
artifact. Peak RSS was 30,081,024 bytes and total screen-plus-confirmation wall
time was 527.1 seconds with four parallel workers. No weight or threshold was
tuned, and no `0x7d...` or `0xd7...` game seed was read.

## Fair root CVaR ablation (rejected)

`approaches/fair-expectimax/root-risk/fair-root-risk.cpp` is one bounded risk-sensitive test over the confirmed
fair-only evaluator. For every legal action it constructs exactly seven
independent scenarios from the canonical public-state hash. A scenario samples
only the immediate chance transition; after that outcome becomes observable,
the unchanged fair search completes two more action plies with five stratified
samples at every chance node. No later decision sees a scenario tape, the real
game seed, score, level, or moves-played metadata. The single preregistered root
utility is `0.75 * mean + 0.25 * CVaR25`. With seven outcomes, CVaR25 gives full
weight to the worst outcome and 0.75 weight to the second worst, then divides
by the 1.75-outcome tail mass.

```sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/root-risk/fair-root-risk.cpp \
  -o /tmp/drop7_fair_root_risk
/tmp/drop7_fair_root_risk --self-test
/tmp/drop7_fair_root_risk --run

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/root-risk/fair-root-risk.cpp \
  -o /tmp/drop7_fair_root_risk_san
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_fair_root_risk_san --self-test
```

The fresh historical 7,000-point Sequence-scored
`0x3e9d0000...007` screen rejected the ablation.
Fair-only depth 3/five samples averaged 176,819.75 points and 114.125 moves;
root risk averaged 87,112 points and 61.5 moves. Paired deltas were -89,707.75
points and -52.625 moves, with one win and seven losses for both measures.
Neither cohort was censored. Clear/reveal throughput fell from 2.043/1.141 to
1.730/0.945 per move, and mean maximum chain fell from 7.875 to 5.125.

The risk policy switched away from the local fair-only recommendation on
28.25% of its trajectory decisions. Its policy search used 53,423 work units
per move versus fair-only's 55,674; the optional switch audit added another
50,333 units per move but is not part of the deployable policy. Peak RSS was
3,948,544 bytes and the screen took 30.7 wall seconds. Because both admission
means regressed, `0x3e9e0000...00f` confirmation seeds were not read. The
coefficient and scenario count were not retuned. Strict `-Werror`, ASan/UBSan,
frozen fair-search parity, deterministic scenario values, fractional-CVaR,
reflection, metadata blindness, game-seed exclusion, legality, and resource
bound tests passed.

## Full historical fair action terms (rejected)

`approaches/fair-expectimax/full-action-terms/full-fair-horizon.cpp` tests a fidelity gap in the historical fair
recovery. The confirmed depth-3 policy uses only the original state features
at its leaves. The older one-ply policy also scored each chosen placement and
each sampled transition. This ablation restores that complete frozen block at
every search ply: 300 points per revealed cover, 120 times squared continuation
chain depth, and the original landing-height, column-content, build-distance,
overshoot, neighbor-height, center, one-away, and imminent-rise terms. It does
not fit or select a coefficient from gameplay.

```sh
clang++ -O3 -std=c++20 -pthread -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/full-action-terms/full-fair-horizon.cpp \
  -o /tmp/drop7_full_fair_horizon
/tmp/drop7_full_fair_horizon --self-test
/tmp/drop7_full_fair_horizon --run

clang++ -O1 -g -std=c++20 -pthread -Wall -Wextra -Wpedantic -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  approaches/fair-expectimax/full-action-terms/full-fair-horizon.cpp \
  -o /tmp/drop7_full_fair_horizon_san
/tmp/drop7_full_fair_horizon_san --self-test
```

The fresh historical 7,000-point Sequence-scored
`0x3ea10000...007` screen rejected the complete
historical block. Fair-leaf depth 3 averaged 134,923.875 points and 89.125
moves, while full-fair depth 3 averaged 94,812 points and 65.25 moves. Paired
deltas were -40,111.875 points and -23.875 moves. The candidate won only three
of eight point pairs and three move pairs, tying one; neither cohort was censored.
The most damaging failure was seed `0x3ea10002`, where the baseline survived
175 moves and the candidate 80.

This result does not isolate transition rewards from placement priors. It does
show that the one-ply action geometry cannot be transplanted wholesale into a
deeper evaluator: successor-state scoring already represents much of the same
height and build information, so charging both can reverse good decisions.
The `0x3ea20000...00f` confirmation range was not read. Optimized and
ASan/UBSan strict builds passed deterministic completion, reflection,
metadata blindness, legal-action, fixed-seed, and public-state checks. The
screen artifact is `/tmp/drop7-full-fair-horizon.json`.

## Fair transition rewards without placement priors (rejected)

`approaches/fair-expectimax/full-action-terms/transition-reward-horizon.cpp` follows up on the full-action failure by
removing every placement prior and retaining only two dense signals from the
historical policy: 300 utility per revealed cover and 120 times squared chain
continuation depth. This separation was motivated before its gameplay screen:
the placement terms duplicate geometry already visible in the depth-3 leaf,
while cover damage and a multi-wave transition are newly observed outcomes.
The coefficients were copied exactly rather than selected from a menu.

A 24-game training-only diagnostic on `0x3d9a0000...017` improved mean score
by 22,104.625 and mean survival by 12.375 moves, but the fresh result did not
replicate. On `0x3ea50000...007`, fair-only averaged 113,772.5 points and
78.125 moves; transition-reward search averaged 96,759.625 and 66.5 moves.
Paired deltas were -17,012.875 points and -11.625 moves. Only three score pairs
and three move pairs improved, and the candidate discarded a 145-move fair
trajectory after 65 moves. Neither cohort was censored.

The screen therefore failed both admission means and
`0x3ea60000...00f` remained unread. This is another explicit false-positive
record: merely rewarding the outcomes associated with the privileged oracle
does not tell a public search which preparations will produce them reliably.
Optimized and ASan/UBSan strict builds passed deterministic completion,
reflection, metadata blindness, legality, fixed ranges, and the action-prior
exclusion. The artifact is `/tmp/drop7-transition-reward-horizon.json`.

## Complete-game fair-leaf CEM optimization (rejected after screen)

`approaches/fair-expectimax/cem/fair-cem-optimizer.cpp` performs derivative-free coefficient search
around the confirmed fair-only depth-3/five-sample policy. The zero normalized
vector reproduces that policy exactly. Eight bounded coordinates scale direct
trigger, latent release, cover debt, altitude/danger/rise, and low-number clog
feature groups; add next-disc/quiet readiness; and reward revealed covers and
additional waves only after a sampled transition. Historical column and
landing-position action priors remain absent. Five multiplicative coordinates
are log-bounded to `[0.5, 2]`; the other bounds are `[-1, 1]`, `[-600, 600]`,
and `[-1500, 1500]` respectively.

The cross-entropy optimizer used eight rotating common-random three-game
batches, 12 candidates per batch, three elites, antithetic perturbations, and
a robust objective equal to `0.60 * mean + 0.40 * lower-tail-25%` of
`moves + score / 14000`. Unique generation winners, the final mean, and the
exact fair vector were re-ranked on 16 fitting-only tournament seeds. The run
used 432 candidate-games, far below its 3,000-game ceiling. The selected
decoded coefficients were 1.2292835 direct trigger, 1.0686570 latent release,
1.0579061 cover debt, 1.2186482 altitude/danger/rise, 0.8812588 low-number
clog, +0.3584225 readiness, +75.6220 per revealed cover, and -77.9406 per
additional wave. The counterintuitive reduced clog penalty and negative
multiwave reward are reasons to treat this as an empirical candidate, not a
strategy claim.

```sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/cem/fair-cem-optimizer.cpp \
  -o /tmp/drop7_fair_cem_optimizer
/tmp/drop7_fair_cem_optimizer --self-test
/tmp/drop7_fair_cem_optimizer --run \
  --output /tmp/drop7-fair-cem-optimizer.json \
  --checkpoint /tmp/drop7-fair-cem-optimizer.bin

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/cem/fair-cem-optimizer.cpp \
  -o /tmp/drop7_fair_cem_optimizer_san
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_fair_cem_optimizer_san --self-test
```

After the champion was frozen and its binary checkpoint written, the one
32-game `0x3dc10000...01f` heldout passed every preregistered gate. Confirmed
fair averaged 120,608 points and 81.25 moves; the candidate averaged
141,523.75 and 93.969, improvements of 17.34% and 15.65%. Lower-tail-25%
score improved from 50,065 to 56,529.875 and lower-tail survival from 38.75 to
42.75 moves. The paired means nevertheless had negative 95% lower bounds and
the candidate won only 15 of 32 score pairs, so the aggregate gain was
high-variance rather than broadly dominant.

The required fresh `0x3ea30000...007` screen then rejected the candidate.
Fair averaged 149,022.25 points and 97.5 moves; the candidate averaged
148,116.875 and 99.375, paired changes of -905.375 points and +1.875 moves.
Although it won six of eight score pairs, the single 204,516-point loss on
seed `0x3ea30000` erased the small wins. Because both means did not improve,
`0x3ea40000...00f` was never read.

No game was censored at 1,000 moves. On heldout, candidate throughput was
20.64 moves per aggregate game-second versus 21.11 for fair, with 54,883
versus 54,047 work units per move; maximum live cache occupancy was 1,235
entries and peak RSS was 3,784,704 bytes. The whole optimization plus gated
evaluation took 646.0 wall seconds with four workers. Optimized Werror and
ASan/UBSan builds passed inherited engine tests, exact zero-vector leaf/search
parity, reflection, metadata blindness, transition-reward arithmetic,
deterministic antithetic sampling, fractional-tail arithmetic, coefficient
bounds, and seed-protocol checks. The complete JSON artifact and binary
checkpoint are `/tmp/drop7-fair-cem-optimizer.json` and
`/tmp/drop7-fair-cem-optimizer.bin`.

## Frozen CEM coefficients with full-width fair D4 (rejected)

`approaches/fair-expectimax/cem/fair-cem-depth4-interaction.cpp` is a preregistered interaction test,
not a rescue retune. It embeds the exact eight-coordinate champion selected by
the rejected D3 CEM experiment and applies its parameterized leaf and
transition-only cover/wave bonuses at every ply of the separately confirmed
full-width fair D4 search. No coefficient, threshold, chance sample, policy
seed, action prior, or search order changes. The D3 screen failure above
remains the primary result.

The wrapper preserves D4 iterative-deepening semantics and its resource proof:
five stratified chance samples, a 3,200,000-work limit, a 60,000-entry LRU
limit, and worst-case requirements of 3,134,950 work units and 45,430 cached
states. An all-zero normalized vector reproduces stock fair D4 exactly,
including every root value and expected score, the D3 and D4 actions,
1,877,470 work units, 957,740 nodes, 27,360 cached states, and 10,650 cache
hits on the independent TypeScript fixture.

```sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/cem/fair-cem-depth4-interaction.cpp \
  -o /tmp/drop7_fair_cem_depth4_interaction
/tmp/drop7_fair_cem_depth4_interaction --self-test
/tmp/drop7_fair_cem_depth4_interaction --run \
  --output /tmp/drop7-fair-cem-depth4-interaction.json

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/cem/fair-cem-depth4-interaction.cpp \
  -o /tmp/drop7_fair_cem_depth4_interaction_san
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_fair_cem_depth4_interaction_san --self-test
```

The fixed protocol first ran all 16 training-only seeds
`0x3da20000...00f`, then all 16 whole-game heldout seeds
`0x3da30000...00f` without making a choice between cohorts. A cohort passed
only if composite D4 improved both score and survival means while neither
lower-tail-25% measure regressed. Fresh screen and confirmation ranges were
sealed behind both cohort gates.

Training means looked promising: stock fair D4 averaged 178,289.563 points
and 114.688 moves, while frozen-CEM D4 averaged 200,972 and 131.25, paired
gains of 22,682.438 points and 16.563 moves. Those means were driven by a few
large trajectories; each policy won eight pairs. The candidate's worst-quarter
score fell from 89,442.25 to 78,565.75 and worst-quarter survival fell from
61.25 to 56.25 moves, so training failed the preregistered lower-tail gate.
The paired 95% lower bounds were also negative for score and moves.

The independent heldout rejected the interaction more directly. Stock D4
averaged 177,487.313 points and 114.063 moves; frozen-CEM D4 averaged
159,652.063 and 103.125, paired regressions of 17,835.25 points and 10.938
moves. Each policy again won eight pairs, exposing high variance rather than a
consistent improvement. Worst-quarter score declined from 87,585 to
74,431.25 and worst-quarter survival from 60.75 to 51.25 moves. Clear and
reveal throughput also fell from 2.051/1.140 to 2.004/1.103 per move.

Neither cohort had a game censored at 1,000 moves. Heldout composite search
used 1,340,143 work units per move versus stock's 1,369,565 and ran at 0.628
versus 0.618 moves per aggregate game-second. Peak live cache occupancy was
38,320 entries for stock and 38,277 for the composite; process peak RSS was
40,386,560 bytes. The 32 paired games took 3,136.2 wall seconds with four
workers. Because both the training and heldout gates failed,
`0x3eab0000...007` and `0x3eac0000...00f` were never read.

Optimized Werror and ASan/UBSan builds passed inherited engine/D4 tests,
zero-vector leaf and root parity, deterministic repeated decisions,
reflection, metadata blindness, legality, completion/resource proof,
transition-reward arithmetic, exact frozen-coefficient identity,
fractional-tail arithmetic, and seed-protocol checks. The JSON artifact is
`/tmp/drop7-fair-cem-depth4-interaction.json` (SHA-256
`240ee1906c46a68e2ae64d5bb6d32c5e4046cddb6b18b0a5b41acd95f0ef3035`).

## Fair full-width D4 with seven chance samples (rejected at fitting)

`approaches/fair-expectimax/chance-strata/fair-depth4-s7.cpp` isolates one sampling hypothesis: increase the
confirmed fair D4 search from five to seven stratified outcomes at every
chance node. Seven samples cover every possible next visible disc exactly once
at each node. The leaf evaluator, immediate-score semantics, terminal
utilities, full-width action order, state-derived policy salt, iterative
deepening, and game dynamics are unchanged.

The larger chance tree has a no-cache worst case of 11,892,398 work units per
completed D4 decision and 122,598 potentially cacheable nodes. The candidate
uses a 12,000,000-work limit, but deliberately retains only 24,000 LRU entries
and enforces a 64 MiB production-process RSS ceiling. Completion depends only
on the work proof, not cache retention: eviction can repeat computation but
cannot exceed the enumerated full tree. The stock s5 parity path retains its
original 3,200,000-work and 60,000-entry limits.

```sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/chance-strata/fair-depth4-s7.cpp \
  -o /tmp/drop7_fair_depth4_s7
/tmp/drop7_fair_depth4_s7 --self-test
/tmp/drop7_fair_depth4_s7 --run \
  --output /tmp/drop7-fair-depth4-s7.json

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/chance-strata/fair-depth4-s7.cpp \
  -o /tmp/drop7_fair_depth4_s7_san
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_fair_depth4_s7_san --self-test
```

The complete eight-game fitting cohort on `0x3de10000...007` rejected the
hypothesis. Stock D4/s5 averaged 118,676 points and 78.875 moves; D4/s7
averaged 118,512.375 and 81.125, paired changes of -163.625 points and +2.25
moves. S7 won only three of eight score and move pairs. Its lower-tail-25%
score did improve from 54,922 to 63,230 and lower-tail survival from 42.5 to
47.5 moves, but admission required both means to improve as well as the tails.
The score-mean failure is decisive even though it is numerically small; its
paired 95% lower bound was -38,398 points.

The extra samples were expensive. Fitting s7 used 3,140,461,451 work units, or
4,838,924 per move, versus s5's 806,741,660 total and 1,278,513 per move—a
3.79x increase. Numbered-clear throughput slipped from 1.8875 to 1.8598 per
move and reveal throughput from 1.0349 to 0.9923. No fitting game was censored
at 1,000 moves, and every candidate decision remained within the 12-million
work, 24,000-entry LRU, and 64 MiB process constraints.

The initially preregistered whole heldout began before the completed fitting
summary was emitted. Once the fitting failure was computed, the remaining
45–75 minutes of decision-irrelevant s7 work was stopped at a completed-game
boundary. Four completed paired heldout games are preserved in the artifact
as explicitly exploratory and incomplete: they averaged -31,083 points and
-17.5 moves for s7, with one win and three losses. Two additional completed
s5-only games are stored separately; all in-flight work is excluded. These
observations are not a formal heldout result and must not be used to qualify or
compare the policy. Exact wall throughput and peak RSS were not retained when
the process was interrupted; the enforced RSS upper bound was 67,108,864
bytes.

Because fitting failed, `0x3ead0000...007` screen and
`0x3eae0000...00f` confirmation seeds were never read. Optimized Werror and
ASan/UBSan builds passed inherited engine/D4 tests, exact s5 root/action/work/
cache parity, complete s7 next-disc coverage, reveal-event stratification,
determinism, reflection, metadata blindness, legality, fractional-tail math,
and the cache-independent completion/resource proof. The recovered JSON
artifact is `/tmp/drop7-fair-depth4-s7.json` (SHA-256
`e67a28d445da4151c6d35603dfcc88bfc4c89ce2c9fe0cafd4dcf13ec2dc42ca`).
It marks the heldout as non-formal and records why exact timing/RSS fields are
unavailable after the requested stop.

## Privileged-oracle observable-topology residual (underpowered gate failure)

`approaches/oracle-curriculum/topology/oracle-topology-residual.cpp` asks a narrower question than oracle
action distillation: can a deployable evaluator recognize the *observable
board topology* of sustainable privileged-oracle trajectories? The privileged
depth-4/beam-128 policy and public fair-only full-width D4 policy generated
states only on the `0x3d9c...` fitting and `0x3d9d...` whole-seed-heldout
families. Before learning, examples were balanced within rise phase, exact
occupancy, exact maximum height, and 20-move trajectory band. The optimizer
then received only a horizontally reflection-canonicalized 49-cell board.
Game seed, future tape, score, level, move index, history, next disc, rise
phase, and every matching key were excluded from model input.

The model is a compact sparse NNUE with 490 one-hot cell/type inputs, exactly
49 active inputs per board, eight ReLU accumulators, and 3,937 parameters. It
was trained once for 240 deterministic Adam epochs with L2 regularization.
Architecture, training schedule, prediction thresholds, the six-value leaf
coefficient grid, and fresh gameplay ranges were fixed before collection. A
measured D4 cost projection caused the cohort to be reduced *before any model
metric existed* to 16 training and eight heldout seeds, with 160 collected
states per policy/game at most.

```sh
clang++ -O3 -std=c++20 -pthread -Wall -Wextra -Wpedantic -Werror \
  approaches/oracle-curriculum/topology/oracle-topology-residual.cpp \
  -o /tmp/drop7_oracle_topology_residual
/tmp/drop7_oracle_topology_residual --self-test
/tmp/drop7_oracle_topology_residual --run \
  --output /tmp/drop7-oracle-topology-residual.json \
  --model /tmp/drop7-oracle-topology-residual-model.json \
  --labels /tmp/drop7-d4-public-root-labels.jsonl

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/oracle-curriculum/topology/oracle-topology-residual.cpp \
  -o /tmp/drop7_oracle_topology_residual_san
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_oracle_topology_residual_san --self-test
```

The 738 matched training examples (369 pairs) fitted strongly, while the 170
matched heldout examples (85 pairs) still showed a real-looking but weaker
signal: AUC 0.68097 and exact-stratum oracle-over-fair ranking 0.67059. Pair
accuracy remained positive in both whole-seed halves, 0.70270 and 0.64583.
However, the preregistered gate required at least 200 heldout examples and 100
heldout pairs in addition to AUC/ranking thresholds. The runtime reduction
left it 30 examples and 15 pairs short. The gate was not changed after seeing
the favorable metrics; coefficient diagnostics did not run, and fresh
`0x3ea90000...007` screen and `0x3eaa0000...00f` confirmation seeds were not
read. This result therefore establishes a promising observable topology
signal, not a policy improvement.

The frozen model fingerprint is `0x0af6ed6f88895cfe`. SHA-256 hashes are
`9b533353828773fa4a4df8bf5be80891b802ff4b3ea2057db0392ed4b5f8271a`
for the model and
`3b34e69d5786fc74c2e9c30f3d88146565d002d66390b312477418b5751cc6da`
for the experiment artifact. Peak RSS was 38,928,384 bytes and collection,
fit, and evaluation took 844.2 wall seconds. Optimized Werror and ASan/UBSan
self-tests passed reflection invariance, deterministic training/search,
metadata blindness, legal action and completed-depth checks, exact
zero-coefficient root-Q parity with frozen fair D4, sealed seed ranges, and
work/cache/parameter limits.

As a zero-extra-search byproduct, the harness also preserves 1,508 training
and 465 whole-seed-heldout fair-D4 public state/action/root-Q labels in
`/tmp/drop7-d4-public-root-labels.jsonl`. Those records exclude game seed,
score, level, move index, history, and future tape, are explicitly split, and
were never read by this residual experiment. Their SHA-256 is
`f61801abc9eefe86011f7202620a18c1277fcc1b5a24f4bce5947033b791dd89`.

## Frozen oracle-topology prediction extension (replicated, policy rejected)

`approaches/oracle-curriculum/topology/oracle-topology-residual-extension.cpp` is a distinct preregistered
follow-up to the underpowered result above; it does not retroactively merge or
change that failure. Before reading a new seed, the harness verifies the
frozen model's exact SHA-256
`9b533353828773fa4a4df8bf5be80891b802ff4b3ea2057db0392ed4b5f8271a`
and parameter fingerprint `0x0af6ed6f88895cfe`. It then replays
`0x3d9d0000...007` and requires exact reproduction of all original prediction
metrics. Only after that check does it collect the untouched prediction-only
extension `0x3d9d0008...00f`, with the same 160-state cap, oracle depth/beam,
whole-seed split, exact matching strata, and board-only learning boundary.
There is no retraining, recalibration, architecture change, threshold change,
or coefficient change.

```sh
clang++ -O3 -std=c++20 -pthread -Wall -Wextra -Wpedantic -Werror \
  approaches/oracle-curriculum/topology/oracle-topology-residual-extension.cpp \
  -o /tmp/drop7_oracle_topology_residual_extension
/tmp/drop7_oracle_topology_residual_extension --self-test \
  --model /tmp/drop7-oracle-topology-residual-model.json \
  --labels /tmp/drop7-d4-public-root-labels.jsonl
/tmp/drop7_oracle_topology_residual_extension --run \
  --model /tmp/drop7-oracle-topology-residual-model.json \
  --labels /tmp/drop7-d4-public-root-labels.jsonl \
  --output /tmp/drop7-oracle-topology-residual-extension.json

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/oracle-curriculum/topology/oracle-topology-residual-extension.cpp \
  -o /tmp/drop7_oracle_topology_residual_extension_san
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_oracle_topology_residual_extension_san --self-test \
  --model /tmp/drop7-oracle-topology-residual-model.json \
  --labels /tmp/drop7-d4-public-root-labels.jsonl
```

The replay reproduced the original 85 pairs and every metric exactly. The
extension produced 204 matched pairs/408 examples and independently passed:
AUC 0.63829, matched-stratum pair accuracy 0.64216, and four-seed-half
accuracies 0.64103/0.64368. The pooled original-plus-extension cohort had 289
pairs/578 examples, AUC 0.64811, pair accuracy 0.65052, and cohort-half
accuracies 0.67059/0.64216. Thus the frozen model genuinely ranks privileged
observable topology above fair-D4 topology, although its heldout logistic loss
was poorly calibrated (1.94287 on the extension).

Only after both prediction gates passed did the original six-value coefficient
grid run on 24 uniformly sampled *training-split* public states from the frozen
label artifact. Coefficients 250, 500, 1,000, and 2,000 switched 8.33%, 12.5%,
20.83%, and 29.17% of actions; 4,000 and 8,000 exceeded the 35% limit. The
frozen target rule therefore selected coefficient 500. All searches completed
depth four and chose legal actions.

The fresh eight-game `0x3ea90000...007` screen passed both means. Fair D4
averaged 85,874.5 points/60 moves and the residual averaged 95,697.125/65.625,
paired gains of 9,822.625 points and 5.625 moves. But each policy won four
score pairs, and the paired 95% lower bounds were -22,580.31 points and -14.01
moves, so this was visibly high variance rather than a robust win.

The required 16-game `0x3eaa0000...00f` confirmation rejected the candidate.
Fair D4 averaged 196,764.125 points and 125.9375 moves; the residual averaged
154,934.625 and 101.9375, paired changes of -41,829.5 points and -24 moves.
The candidate won only five score pairs and five move pairs (with one move
tie); paired lower bounds were -116,357.90 and -67.68. Clear/reveal throughput
fell from 2.0705/1.1608 to 2.0159/1.1245 per move, and mean maximum chain fell
from 7.6875 to 6.6875. No game was censored at 1,000 moves. The failure mode
was catastrophic loss of several long baseline trajectories, including
476,511/285 to 139,399/90 and 329,049/200 to 74,172/55, despite occasional
large improvements.

The full follow-up took 2,644.0 wall seconds with peak RSS 36,421,632 bytes.
Strict optimized Werror and ASan/UBSan tests passed the inherited engine/model
tests plus SHA-256 known-vector and frozen-file verification, label checksum
and count, reflection, finite inference, exact gate boundaries, and sealed
seed ranges. The extension artifact is
`/tmp/drop7-oracle-topology-residual-extension.json`, SHA-256
`3da488cbc95df48b6770ba65e0c019ed2038dc76da13df5ff2bedc4389340ff8`.
The original artifact, frozen model, and label dataset remain separate and
unchanged at their hashes documented above.

## Fair-D4 root-Q behavior clone (label gate rejected)

`approaches/value-policy-learning/d4-q-clone/d4-q-clone.cpp` tests whether the expensive full-width D4 policy can be
compressed into a fast observable rollout policy. It consumes the preserved
1,508-training/465-heldout root-label file and uses the complete legal root-Q
vector, not just the chosen action. Each root is normalized independently, so
absolute Q scale cannot become a global value label; training combines a
temperature-0.18 listwise target with gap-weighted pairwise ranking and omits
Q-tied pairs. Legal masks and optimal ties are preserved.

The model has 502 sparse inputs (49 board cells, next disc, and rise phase),
51 active inputs per state, 24 ReLU accumulators, seven action outputs, and
12,247 parameters/97,976 parameter bytes. Exact reflection equivariance comes
from averaging a direct pass with the reflected pass in reversed action order.
Game/seed, score, level, move/game index, history, and future tape do not enter
the model. The parser read exactly the header plus 1,508 training records,
trained for a fixed 260 epochs, wrote and round-tripped the checkpoint, and
only then reopened the file to read heldout exactly once. No heldout metric
selected architecture, weights, or hyperparameters.

```sh
clang++ -O3 -std=c++20 -pthread -Wall -Wextra -Wpedantic -Werror \
  approaches/value-policy-learning/d4-q-clone/d4-q-clone.cpp -o /tmp/drop7_d4_q_clone
/tmp/drop7_d4_q_clone --self-test \
  --checkpoint /tmp/drop7-d4-q-clone.bin
/tmp/drop7_d4_q_clone --run \
  --labels /tmp/drop7-d4-public-root-labels.jsonl \
  --checkpoint /tmp/drop7-d4-q-clone.bin \
  --output /tmp/drop7-d4-q-clone.json

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/value-policy-learning/d4-q-clone/d4-q-clone.cpp -o /tmp/drop7_d4_q_clone_san
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_d4_q_clone_san --self-test \
  --checkpoint /tmp/drop7-d4-q-clone.bin
```

The clone strongly overfit trajectories. Training top-1-with-ties/top-2/
pairwise were 0.76459/0.89721/0.75467, but the single heldout result fell to
0.24731/0.45806/0.57350. Whole-seed-half pairwise accuracies were
0.57430/0.57248, below the frozen 0.62 half gate. Normalized regret 0.42125
beat center-first's 0.53023 but was almost twice the public one-ply baseline's
0.21257, failing the required regret-retention gate as well as the 0.35 top-1,
0.55 top-2, and 0.65 pairwise gates.

Error changed rather than disappearing across a trajectory. On moves 0–29,
top-1/pairwise were 0.20417/0.59128; on moves 30+, they were
0.29333/0.55161. Under an explicitly labeled independence proxy—not a real
stochastic rollout—30 decisions imply 22.58 wrong top choices on average and
only a `6.27e-19` chance of matching all 30 D4 top choices. The corresponding
expected pairwise errors were 12.79, with all-30 correctness `5.70e-8`.
Early/late all-top-choice proxies were `1.99e-21`/`1.05e-16`, while late
pairwise ordering degraded further. This checkpoint is therefore unsuitable
as a long rollout policy despite its speed.

Standalone symmetrized inference reached 3,485,535 states/second on 250,000
heldout evaluations. The binary checkpoint is 98,008 bytes with fingerprint
`0x1e9b525281e8b3c5`; its SHA-256 is
`56a497f1f8871ee8bfaf477eb140614c87508f2bf5ad25fc97842f09372e249a`.
The JSON artifact SHA-256 is
`d896e7ddb2ae9000486ef3f358043b57e3dca7eceb4eee497615deb7e47e852a`.
Per the superseding protocol, no fresh gameplay seed—including the reserved
training-only `0x3de30000...01f` range—was read.

Strict optimized Werror and ASan/UBSan tests passed inherited D4 parity plus
deterministic training, a `7.06e-12` finite-difference gradient check,
checkpoint roundtrip/fingerprint, exact reflection, metadata blindness,
legality, masks/ties, parameter/checkpoint bounds, and seed-range checks.

## Public fair-D1 rollout-improvement pilot (fitting gate rejected)

`approaches/fair-expectimax/rollout-improvement/fair-d1-rollout-improvement.cpp` evaluates every legal root action on
the same seven scenario tapes, then uses only the public board, visible next
disc, and rise phase to choose all later actions with fair D1. Reveal draws and
future visible discs use separate event-indexed domains; the first future disc
is exactly stratified over 1–7. The reusable `Decision` retains each legal
action's mean and seven aligned scenario returns. Define
`DROP7_FAIR_D1_ROLLOUT_IMPROVEMENT_LIBRARY` to embed the chooser without its
standalone `main`.

The preregistered fitting grid was horizon 8/16/24 crossed with fair-leaf tail
scale 0/0.25, using mean return (no CVaR), 12 complete `0x3df00000...00b`
games, and at most 9,408 transitions per decision. Fair D1 was benchmarked
first at 72,526.17 mean points, 53.75 moves, and 1.69147 numbered clears per
move. The best rollout was horizon 16/tail 0 at only 64,304.83 points, 45.5
moves, and 1.51282 clears per move. It lost to fair D1 in all 12 leave-one-out
score comparisons and all 12 throughput comparisons. Horizon 16 was selected
in 11/12 leave-one-out fits, so the negative result was stable rather than a
grid-selection accident.

The candidate failed both the frozen 250,000-point fitting threshold and the
1.05x clear-throughput threshold. Consequently the one-shot
`0x3df10000...00f` heldout range was never read. This rejects short
mean-return rollouts whose continuation is fair D1; it does not establish that
rollouts with a stronger learned continuation or longer-lived value model are
unhelpful.

```sh
clang++ -O2 -std=c++20 -pthread -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/rollout-improvement/fair-d1-rollout-improvement.cpp \
  -o /tmp/drop7-fair-d1-rollout-improvement
/tmp/drop7-fair-d1-rollout-improvement --threads 4 \
  --output /tmp/drop7-fair-d1-rollout-improvement.json

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/rollout-improvement/fair-d1-rollout-improvement.cpp \
  -o /tmp/drop7-fair-d1-rollout-improvement-sanitize
/tmp/drop7-fair-d1-rollout-improvement-sanitize --self-test-only
```

Optimized Werror, library-mode Werror, and ASan/UBSan self-tests passed exact
determinism, reflection, metadata blindness, tape alignment/stratification,
reveal/visible-domain independence, legality, and the resource bound. Peak RSS
was 2,162,688 bytes. The JSON artifact SHA-256 is
`82b6fe9c78de486ad550dac795f76ff162799846f53a80a76177114ea0b91428`.

## Native PPO audit and fair-D1 DAgger rescue (warm-start gate rejected)

The unused v1 path in `src/core/native/ppo.hpp` has a locally correct policy/value
gradient, deterministic multithreaded collection, and correct bootstrap logic
for a capped trajectory. Its observation nevertheless includes cumulative
score, level, and move count; horizontal canonicalization does not make the
policy exactly equivariant on symmetric boards; and the unnormalized critic
loss is much larger than the policy loss before both share one global gradient
clip. It also saves but cannot reload optimizer/checkpoint state and never
compares its greedy policy with random play.

An unchanged, reproducible v1 audit used 8 iterations of 128 games from
`0x3f000000...` and a fixed 64-game `0x3f100000...03f` fitting probe. Its
untrained greedy policy averaged 23,936.52 points/21.72 moves. Training's
sampled trajectories stayed near random, but the best greedy probe reached
only 24,503.34/22.11 versus deterministic random's 31,835.25/26.94. Policy
loss and approximate KL remained approximately zero while value loss began at
51.5, consistent with the actor receiving too little useful update rather
than learning a better deterministic strategy. The v1 audit checkpoint is
`/tmp/drop7-native-ppo-v1-audit.json`, SHA-256
`b311335844f5a576875ff82c0437cbce321fa20f9911be6a66c4f64cd676c5a6`.

`approaches/ntuple-rl/native-ppo/ppo-v2.cpp` is the bounded rescue. It observes only board, visible next
disc, rise phase, and board-derived scalars. A shared two-pass reflection
ensemble makes the complete action distribution exactly equivariant and the
critic exactly invariant. Its transition-local reward combines 0.1 survival,
0.02 per numbered clear, 0.015 per cover reveal, true score divided by 700,000,
and a -0.5 terminal penalty. Natural terminals cut GAE bootstrap; capped
trajectories bootstrap from the public state. Binary checkpoints round-trip
float bits and carry a deterministic parameter fingerprint.

The frozen rescue collected 26,832 states from 512 fair-D1 games and added
14,189 student-distribution states over two 256-game DAgger rounds. After
41,021 total examples, final teacher-action agreement was 36.62% versus 14.29%
chance, with cross-entropy 1.5340. On the fitting probe, warm-start gameplay
improved from 18,906.03 points/18.28 moves to 33,539.47/28.02, narrowly above
random's 32,143.69/27.03. Fair D1 remained far ahead at 69,274.41/51.27.
Numbered-clear throughput was 1.0881 for the clone, 0.9751 for random, and
1.6525 for fair D1; reveal throughput was 0.4055, 0.3584, and 0.8470.

The warm start therefore failed the preregistered 1.10x-random and
0.70x-fair-D1 score/move reproduction gates. PPO did not run, and the reserved
`0x3f200000...` heldout range was never read. This rejects the small 8,240-
parameter actor as a faithful fair-D1 imitator under this DAgger schedule; an
on-policy update from its still-fragile state would measure recovery from
imitation error rather than policy improvement.

```sh
clang++ -O2 -std=c++20 -pthread -Wall -Wextra -Wpedantic -Werror \
  approaches/ntuple-rl/native-ppo/ppo-v2.cpp -o /tmp/drop7-ppo-v2
/tmp/drop7-ppo-v2 --threads 4 --checkpoint /tmp/drop7-ppo-v2.bin \
  --output /tmp/drop7-ppo-v2.json

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/ntuple-rl/native-ppo/ppo-v2.cpp -o /tmp/drop7-ppo-v2-sanitize
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7-ppo-v2-sanitize --self-test-only
```

Optimized Werror, library-mode Werror, and ASan/UBSan self-tests passed exact
reflection, metadata blindness, deterministic inference, terminal/truncation
semantics, reward accounting, legality, seed partitioning, and bit-exact
checkpoint roundtrip. A full repeated run reproduced every non-resource metric
and the checkpoint byte-for-byte. The 32,992-byte checkpoint SHA-256 is
`789a4b5b2506a456ac6c0317580c2d740710690466d8c0b9f7e06c4ac01c04cb`;
the JSON artifact SHA-256 is
`06046c0a43fc7489e153c3264499c9ab1c29fdbc291d2422fee587d194e0573c`.

## Fair-D4 phase-energy release pilot (resource-capped diagnostic)

`approaches/fair-expectimax/transition-rewards/fair-phase-energy-release.cpp` preserves stock fair D4's full-width
depth-four search, five stratified chance samples, policy seed, terminal
utility, iterative deepening, and work/cache limits. It tests two changes:
explicit transition utility for each numbered disc cleared, and a leaf-only
schedule on the existing 1,600-point direct-potential plus 700-point latent-
potential terms. The schedule changes stored-energy value by +35%, +20%, 0%,
-25%, and -40% from early-cycle to pre-rise states. The frozen menu contained
clear-only (+600/disc), phase-only, their moderate combination, and one
aggressive combination (+1,200/disc and 1.5x schedule).

Zero coefficients reproduce stock D4's action, complete root-Q/expected-score
vectors, nodes, work, cache hits, and cache size exactly. Candidate searches
are public-state-only and reflection safe. Static worst-case proof remains
3,134,950 work and 45,430 cache entries, below limits of 3,200,000 and 60,000;
optimized self-test peak RSS was 12,386,304 bytes under a 2 GiB sanitizer-safe
bound.

The first seed showed the intended interaction but not the expected ablation:
stock scored 185,341/105 moves/204 clears; clear-only 151,969/100/195;
phase-only 67,049/50/79; moderate combined 203,191/135/293; and aggressive
combined 193,310/130/265. Because both combined candidates were competitive,
the diagnostic first-pair stop did not apply.

The fixed experiment then hit its wall budget before the final two aggressive
fitting games completed. Four policies did finish all four
`0x3de50000...003` games:

- Stock: 143,299.5 points, 88.75 moves, 1.9437 clears/move.
- Clear-only: 244,186.5 points, 156.25 moves, 2.1264 clears/move.
- Phase-only: 93,784.75 points, 65.5 moves, 1.7977 clears/move.
- Moderate combined: 169,493.75 points, 112.5 moves, 2.0489 clears/move.

Clear-only beat stock on score, moves, and clear throughput in all four
leave-one-out folds. Nevertheless, it is not a formal fitting winner because
the preregistered aggressive menu member is incomplete. The process was
interrupted with exit 130 before any `0x3de60000`, `0x3eb30000`, or
`0x3eb40000` game began. Thus no heldout, fresh screen, or confirmation result
exists. The defensible diagnostic is narrower: explicit clear credit is worth
a dedicated cheaper follow-up, while this phase schedule is harmful alone and
substantially dilutes the clear-only improvement when combined.

The source now mechanically enforces the 2,100-second ceiling at stage
boundaries using completed paired decision time and a fixed 1.5x safety
factor; this changes neither policy nor statistical gates. The partial JSON
artifact explicitly marks unavailable reveal/work/cache per-game fields and
`formalGateEvaluated:false` rather than treating the interrupted menu as a
failed statistical test.

```sh
clang++ -O2 -std=c++20 -pthread -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/transition-rewards/fair-phase-energy-release.cpp \
  -o /tmp/drop7-fair-phase-energy-release
/tmp/drop7-fair-phase-energy-release --threads 4 \
  --output /tmp/drop7-fair-phase-energy-release.json

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/transition-rewards/fair-phase-energy-release.cpp \
  -o /tmp/drop7-fair-phase-energy-release-sanitize
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7-fair-phase-energy-release-sanitize --self-test-only
```

Optimized Werror, library-mode Werror, and ASan/UBSan tests passed exact stock
parity at zero, determinism, reflection, metadata blindness, isolated-ablation
semantics, completion, work/cache bounds, and RSS bound. The resource-capped
diagnostic artifact SHA-256 is
`37a9a04cda5901ad6c599dc4c6f79f08935e1d9f798f618865c956d870c5398f`.

## Fixed fair-D4 clear-reward confirmation (heldout rejected)

The complete stock/clear-only selection pair above justified one separate,
fixed follow-up in `approaches/fair-expectimax/transition-rewards/fair-clear-reward-confirmation.cpp`. Selection used
only the four `0x3de50000...003` games: +600 per numbered clear improved mean
score by 100,887, moves by 67.5, and clear throughput by 0.18274, with a triple
win in every leave-one-out fold. The follow-up removed the phase term and menu
entirely before opening any later range.

The frozen first gate paired stock and +600 on eight untouched training-only
`0x3de60000...007` games. It required higher score and move means with no
clear/reveal throughput regression. Stock averaged 155,655.375 points and
101.25 moves; +600 averaged only 106,701.5 and 72.625. The paired changes were
-48,953.875 points and -28.625 moves. The candidate won one score pair and one
move pair, with two move ties.

Throughput confirmed the regression: numbered clears fell from 2.01975 to
1.85886 per move, reveals from 1.11975 to 1.01205, and mean maximum chain from
6.75 to 6.625. All 16 policy-games completed without censoring. The heldout
gate failed, so neither `0x3eb30000...007` nor `0x3eb40000...00f` was read.
The strong four-game selection result was therefore a false positive rather
than evidence that clear reward generalizes.

The run retained full-width D4/s5. Stock/candidate mean work per move was
1.322M/1.262M, peak cache entries 37,144/35,239, and process peak RSS
47,726,592 bytes. The heldout stage took 377.96 seconds. A preregistered
2,100-second stage-boundary projection used eight threads, a 1.5x safety
factor, and an initial eight-CPU-minute-per-game upper bound; later projections
use measured paired decision time and cannot launch a stage whose projected
total exceeds the ceiling.

```sh
clang++ -O2 -std=c++20 -pthread -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/transition-rewards/fair-clear-reward-confirmation.cpp \
  -o /tmp/drop7-fair-clear-reward-confirmation
/tmp/drop7-fair-clear-reward-confirmation --threads 8 \
  --output /tmp/drop7-fair-clear-reward-confirmation.json

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/transition-rewards/fair-clear-reward-confirmation.cpp \
  -o /tmp/drop7-fair-clear-reward-confirmation-sanitize
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7-fair-clear-reward-confirmation-sanitize --self-test-only
```

Optimized Werror, library-mode Werror, and ASan/UBSan tests passed inherited
zero-coefficient stock parity, deterministic/reflection/metadata tests, the
fixed no-phase candidate assertion, all four stored leave-one-out selection
checks, resource projection, work/cache completion, and RSS bounds. The JSON
artifact SHA-256 is
`995c6664e068f330eba009f4aa36b1afcffd8afb8a53e07ecaa64ef2f91bdf96`.

## Public selective deeper fair expectimax (rejected at screen)

`approaches/fair-expectimax/selective-depth/fair-selective-depth.cpp` extends the qualified fair D4 policy without
changing its leaf, terminal value, observable-state sampling seed, or five
stratified chance outcomes. Every legal root action is searched. At internal
decision nodes, an exact fair D1 evaluation deterministically orders all legal
actions and only the configured leading actions receive deeper search. A
3,200,000-work cap and 45,000-entry LRU bound every attempt; an incomplete
attempt returns the separately completed fair D4 action. D5/width-2 has a
compile-time worst-case proof of 2,760,835 work and 38,885 cached nodes, so it
never needs that fallback.

The fitting menu was frozen before reading data: uniform D5/width-2,
D5/width-3, and D6/width-2 on four common `0x3dd00000...003` games capped at
500 moves. Selection maximized an equal-log-score/moves utility averaged
equally over all pairs and the worst half. A phase-aligned D3/D4/D5-width-2
contingency was preregistered but eligible only if every uniform candidate was
weak. D5/width-2 gained 45,225.5 points and 29.5 moves on average but had
negative robust utility (-0.0201). D6/width-2 was effectively D4 (-2,020
points, tied moves) and fell back on 90.17% of decisions. D5/width-3 won the
fit with +55,436 points, +32.5 moves, and +0.1848 robust utility, so the phase
contingency was not run. Its cost was already above the preferred target:
3.891 million work per move with 69.65% fallback.

The one disjoint eight-game `0x3dd10000...007` training-heldout pass met its
frozen gate. D4 averaged 120,580.75 points/82.875 moves and selected D5/width-3
averaged 133,977.125/89.75, paired gains of 13,396.375 points and 6.875 moves.
The candidate's bottom-quartile score and move means were 65,901 and 47.5,
versus D4's 59,153 and 45, above the 0.75 no-collapse floor. The evidence was
nevertheless noisy: both paired 95% lower bounds were negative, wins were 4/8
for score and 3/8 for moves with one tie, and removing the single
+201,765-point/+120-move pair made the means negative.

That gate legitimately opened the eight-game `0x3ea70000...007` screen at a
1,000-move cap, where the candidate failed. D4 averaged 182,041.125 points and
115.875 moves; D5/width-3 averaged 174,808.375 and 112.375, paired changes of
-7,232.75 points and -3.5 moves. It won only three of eight pairs. Removing the
largest +260,980-point/+150-move win changed the screen means to
-45,548.86/-25.43; only two of eight leave-one-pair-out means were positive on
both endpoints. Numbered-clear throughput decreased from 2.0647 to 2.0378 per
move, and reveal throughput decreased from 1.1672 to 1.1324. Confirmation
`0x3ea80000...00f` was therefore never read.

The screen candidate switched 76 of 899 same-state completed-D4 actions
(8.45%) and fell back on 620 decisions (68.97%). Switch rates by root
`movesRemaining` were 7.26%, 5.56%, 3.89%, 12.78%, and 12.78% for phases one
through five: 60.5% of all switches occurred in phases four and five, which
contained 40.0% of decisions. This is diagnostic, not causal evidence, but it
is consistent with horizon/rise alignment mattering. Logical policy work was
3.839 million per move versus D4's 1.387 million (2.77x), the cache reached its
45,000-entry cap, peak RSS was 36,552,704 bytes, no game was censored, and the
full gated run took 6,825.3 seconds. No `0x7d...` or `0xd7...` game seed was
read.

```sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/selective-depth/fair-selective-depth.cpp \
  -o /tmp/drop7_fair_selective_depth
/tmp/drop7_fair_selective_depth --self-test
/tmp/drop7_fair_selective_depth --run \
  --output /tmp/drop7-fair-selective-depth.json

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/selective-depth/fair-selective-depth.cpp \
  -o /tmp/drop7_fair_selective_depth_san
/tmp/drop7_fair_selective_depth_san --self-test
```

Optimized Werror and ASan/UBSan self-tests pass deterministic ordering,
reflection, metadata/game-seed blindness, legal full-width roots, the D5/w2
completion proof, cache/work bounds, and forced completed-D4 fallback. The
complete JSON artifact is `/tmp/drop7-fair-selective-depth.json`, SHA-256
`730caf83ad6dfb9b7f473a0a058bd10a4e29ed9306f9a56daec7b710125a2ab7`.

## One-cycle boundary D5 pilot (rejected by runtime gate)

`approaches/fair-expectimax/selective-depth/fair-cycle-boundary-depth5.cpp` isolates the row-rise alignment
hypothesis from the rejected uniform selective search. The primary policy uses
the proved-complete fair D5/width-2/five-stratum search only when public
`movesRemaining` is 5 and uses qualified full-width D4 in phases one through
four. Its only preregistered contingency uses D5/width-2 in phases four and
five, and is eligible only if the primary fails fitting. No search weight,
leaf, chance rule, or threshold is retuned.

The runner first executes only the `0x3de70000` fitting pair, then multiplies
that measured wall time by a conservative 10.5 full-gate wave estimate. It
must pause before reading another seed when the projection exceeds 40 minutes.
That guard fired: D4 scored 231,290 in 150 moves, while the phase-5 D5 policy
scored 107,668 in 75 moves, a first-pair change of -123,622 points and -75
moves. Numbered-clear throughput fell from 2.1200 to 1.9600 per move and reveal
throughput from 1.1933 to 1.1067. The D5 decisions all completed without
fallback; total logical work was slightly lower only because the candidate
game ended in half as many moves.

The pilot pair took 256.071 seconds and projected the full gated protocol at
2,688.743 seconds (44.81 minutes), beyond the fixed 2,400-second ceiling. The
process exited with its documented paused status before `0x3de70001`, the
phase-{4,5} ablation, heldout `0x3de80000...007`, screen
`0x3eb50000...007`, or confirmation `0x3eb60000...00f` was read. This single
pair is not a statistical rejection, but it supplies no reason to override the
wall guard: both outcomes and both throughput measures moved sharply in the
wrong direction. The artifact marks this as pilot-only with no formal
inference. It explicitly records the fitting remainder, ablation, heldout,
screen, confirmation, and protected game-seed ranges as untouched.

```sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/selective-depth/fair-cycle-boundary-depth5.cpp \
  -o /tmp/drop7_fair_cycle_boundary_depth5
/tmp/drop7_fair_cycle_boundary_depth5 --self-test
/tmp/drop7_fair_cycle_boundary_depth5 --run \
  --output /tmp/drop7-fair-cycle-boundary-depth5.json

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/selective-depth/fair-cycle-boundary-depth5.cpp \
  -o /tmp/drop7_fair_cycle_boundary_depth5_san
/tmp/drop7_fair_cycle_boundary_depth5_san --self-test
```

Optimized Werror and ASan/UBSan self-tests pass deterministic phase routing,
reflection, metadata/game-seed blindness, legal full-width roots, completed
D5/width-2, and cache/work bounds. The partial JSON artifact is
`/tmp/drop7-fair-cycle-boundary-depth5.json`, SHA-256
`925d94ca4f32596a2ea9e907d6a30115c78ad4675071e0a6abd2d41c08de14eb`.
The source SHA-256 is
`b2f2f2987c63fe878e46a6d375f27c61c6c4fafc823909554b91145291d5e680`.

## Full-width fair D5 with three strata (paused at runtime gate)

`approaches/fair-expectimax/selective-depth/fair-depth5-s3.cpp` compares three fixed policies with the qualified,
frozen fair leaf and game semantics: stock D4 with five chance strata, D4 with
three strata as the sampling control, and full-width D5 with three strata. All
legal actions are searched at every decision node. The D5 search has a
compile-time worst-case proof of 8,791,020 logical work per move and completion
does not depend on cache retention. Its deterministic LRU is capped at 24,000
entries, games are capped at 1,000 moves, and cohort parallelism is fixed at
two.

The preregistered fitting gate required D5/s3 to improve both mean score and
mean moves over both controls while retaining at least 98% of each control's
numbered-clear and cover-reveal throughput. Eight fitting seeds were reserved
at `0x3de40000...007`. Before reading a second fitting seed, a mandatory pilot
projected total fitting runtime from the first paired triple. The first triple
took 623.250 seconds, projecting 2,804.627 seconds (46.744 minutes), above the
45-minute limit. The experiment therefore paused before `0x3de40001`; this is
not a completed fitting result and no gate conclusion may be drawn from its
single game. The screen `0x3eb10000...007` and confirmation
`0x3eb20000...00f` ranges were never read.

For that one diagnostic pair only, stock D4/s5 scored 269,141 in 170 moves,
D4/s3 scored 151,153 in 95 moves, and D5/s3 scored 182,622 in 120 moves.
D5/s3 beat the sampling control but trailed stock by 86,519 points and 50
moves. Its clear/reveal throughputs were 2.0750/1.1417 per move, versus
2.2000/1.2824 for stock and 1.9895/1.0842 for D4/s3. Logical work per move was
1.521 million for stock, 0.226 million for D4/s3, and 3.904 million for D5/s3.
No game was censored. Peak RSS was 11,501,568 bytes, safely below the fixed
128 MiB ceiling.

```sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/selective-depth/fair-depth5-s3.cpp \
  -o /tmp/drop7_fair_depth5_s3
/tmp/drop7_fair_depth5_s3 --self-test
/tmp/drop7_fair_depth5_s3 --run \
  --output /tmp/drop7-fair-depth5-s3.json

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/selective-depth/fair-depth5-s3.cpp \
  -o /tmp/drop7_fair_depth5_s3_san
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_fair_depth5_s3_san --self-test
```

Optimized Werror and ASan/UBSan self-tests pass exact stock-D4/s5 parity,
full-width D5 root completion, deterministic chance stratification and replay,
reflection, public-state/metadata blindness, legality, frozen semantics, seed
partitioning, and resource proofs. The paused JSON artifact is
`/tmp/drop7-fair-depth5-s3.json`, SHA-256
`c94a28dffe9d10c3a4a2026f2acdfe14b7e5f71bd4c6c0ef0289b7612848f448`.

## Phase-5 denoised-value veto of fair D4 (paused at runtime gate)

`approaches/value-policy-learning/denoised-value/d4-phase5-value-veto.cpp` tests a conservative way to extend the
qualified full-width fair-D4 policy with the frozen
`artifacts/models/denoised-value/v1.bin` checkpoint. Fair D4 and all of its root-Q values
are completed first on every move and remain the default. The model is routed
only when the public rise phase is exactly `movesRemaining == 5` and the
current maximum column height is at least four; without a clear, the covered
row due after five drops would then produce a height of at least five.

At a routed state, every legal action is evaluated on the model's existing five
common stratified public-state successors. A challenger must have a paired
95% lower bound on predicted lifetime advantage of at least the checkpoint's
frozen held-out MAE, 2.744151 moves, nonnegative mean survival-at-25 advantage,
a forward/reflected orientation gap no greater than the model's existing
five-move limit, and a completed-D4 root-Q loss no greater than one canonical
7,000-point level bonus. Those rules, the `maxHeight >= 4` predicate, and the
stage gates were fixed before any assigned gameplay seed was opened.

The runner opened only the first paired fitting seed, `0x3de90000`. Baseline
and candidate were exactly equal at 162,102 points, 110 moves, 222 numbered
clears, and 120 cover reveals. The candidate routed 21 of 22 phase-5 decisions
and evaluated 725 modeled successors, but no alternative passed all gates and
it made zero switches. Across the alternatives, 124 failed the lifetime gate,
97 failed the survival gate, 13 failed the root-Q band, and none failed the
orientation-gap bound. These rejection counts overlap because one alternative
may fail more than one condition.

The paired pilot took 198.902 seconds. Eighteen four-worker waves are needed
for every possible fitting, heldout, screen, and confirmation policy-game, so
the fixed projection was 3,580.240 seconds, above the 2,700-second limit. The
experiment therefore paused before `0x3de90001...003`. This one identical pair
is pilot-only evidence, not a statistical rejection or qualification. Heldout
`0x3dea0000...007`, fresh screen `0x3eb70000...007`, confirmation
`0x3eb80000...00f`, and the protected `0x7d...` and `0xd7...` families remain
untouched.

```sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/value-policy-learning/denoised-value/d4-phase5-value-veto.cpp \
  -o /tmp/drop7_d4_phase5_value_veto
/tmp/drop7_d4_phase5_value_veto --self-test
/tmp/drop7_d4_phase5_value_veto --run \
  --output /tmp/drop7-d4-phase5-value-veto.json

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/value-policy-learning/denoised-value/d4-phase5-value-veto.cpp \
  -o /tmp/drop7_d4_phase5_value_veto_san
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_d4_phase5_value_veto_san --self-test
```

Optimized standalone and library-mode Werror builds and ASan/UBSan self-tests
pass checkpoint byte-count/checksum verification, inherited exact D4 parity,
determinism, reflection, public metadata blindness, legality, phase/danger
routing, zero-switch D4 parity, and every confidence/survival/root-Q boundary.
The partial JSON artifact SHA-256 is
`d30461c85336e44f43a10b03f9a0484c251f64bb3863072449cbe66a53144eb8`;
the source SHA-256 is
`494205b93447af48615ab0bc2168e56df1569184c48c63d1d0e249f8c02b9c09`.

## Scaled fair-D4 distillation and exact-D2 rollout primitive

`approaches/d4-long-outcome/d4-distillation/scaled-d4-distill.cpp` tests a bounded compression path for the
confirmed public fair-D4 policy. Architecture design used only the already
consumed 1,508/465 root-Q artifact. On that fitting-only split, exact public
fair D1 reproduced D4 at 48.60% heldout top-1 and 69.68% pairwise accuracy,
while full-width public fair D2 reached 56.77% top-1, 76.68% pairwise, and
0.14103 normalized regret. A 1,647-weight action-relative sparse linear NNUE
residual over D2 reached 55.70%/76.11%/0.13461. This fixed the old clone's
severe generalization failure, but it did not beat D2 on every metric.

The resulting experiment froze D2 as an anchor, the action-relative feature
layout, exact two-pass residual reflection, 100 Adam epochs, batch size 64,
learning rate 0.01, L2 0.03, pairwise-loss weight 0.25, and all gates before a
new seed was read. Acceptance required at least 55% top-1, 70% top-2, and 72%
pairwise accuracy overall; at least 50% top-1 and 72% pairwise in each
six-game half; and normalized regret at most 0.18. The learned residual also
had to improve D2 by one percentage point top-1, 0.5 points top-2 and
pairwise, and at least 5% regret in the full cohort and both halves.

The isolated `0x3df20000` pilot ended naturally after 65 moves in 72.153
seconds. Its frozen four-worker projection was 811.723 seconds versus the
2,700-second limit, so collection proceeded. The notification pilot and full
run were separate processes; the full run deterministically replayed that
seed to retain its 65 root-Q records. Thus one unique pilot seed was executed
twice, and the first execution's labels were not persisted. The final corpus
contains 1,885 roots from 24 complete `0x3df20000...017` fitting games and
926 roots from 12 complete, checkpoint-after-freeze
`0x3df30000...00b` holdout games. No game was censored at the 250-move cap.

Exact D2 passed the frozen ranking gate decisively:

| Heldout cohort | Top-1 with ties | Top-2 | Pairwise | Normalized regret |
| --- | ---: | ---: | ---: | ---: |
| All 12 games / 926 roots | **60.475%** | **76.782%** | **76.701%** | 0.13192 |
| First six / 495 roots | 64.444% | 78.788% | 76.936% | 0.11911 |
| Second six / 431 roots | 55.916% | 74.478% | 76.434% | 0.14662 |

The frozen residual reached 59.395% top-1, 76.458% top-2, 76.119% pairwise,
and 0.13021 regret overall. It regressed D2's three accuracy metrics and
reduced regret by only 1.29%, not the required 5%. It also failed the required
improvements within each seed half. The learned residual is therefore
rejected; imitation alone supplies no stronger-policy claim.

The ranking-only audit establishes exact public D2 as a compact rollout and
action-ordering primitive. Across all 926 holdout roots it used 1,949.21
logical work units, 983.49 nodes, and 5.33 cache hits per root; maximum work
was 2,485 with at most 35 cached entries. End-to-end optimized inference ran
at 435.39 roots/second during the final audit. The learned residual needed
both D1 and D2, 2,016.88 work units per root, and ran at 421.11 roots/second.
Its checkpoint is only 13,216 bytes, but the accuracy regression makes those
weights unnecessary. Relative to confirmed D4's roughly 1.35 million logical
work units per move, exact D2 is about 693 times cheaper in this accounting.

An already-running pre-guard binary inadvertently executed a cheap D2-only
sanity trajectory on the same 12 training-heldout seeds immediately after the
residual ranking failure. It averaged 119,061.25 points/81.25 moves versus
D4's 115,073.08/77.17, with no censoring. This used no new seed family and no
residual gameplay ran, but it occurred after the residual's required gate had
failed. It is preserved only as an explicitly excluded diagnostic in the raw
artifact. The canonical audit sets `acceptedGameplayEvidence` to `null`; a
future D2 policy claim still requires its own preregistered fresh comparison.

```sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/d4-long-outcome/d4-distillation/scaled-d4-distill.cpp \
  -o /tmp/drop7_scaled_d4_distill
/tmp/drop7_scaled_d4_distill --self-test \
  --checkpoint /tmp/drop7-scaled-d4-distill-selftest.bin
/tmp/drop7_scaled_d4_distill --pilot-only
/tmp/drop7_scaled_d4_distill --run \
  --output /tmp/drop7-scaled-d4-distill.json \
  --checkpoint /tmp/drop7-scaled-d4-distill.bin \
  --labels /tmp/drop7-scaled-d4-distill-labels.jsonl
/tmp/drop7_scaled_d4_distill --audit-existing \
  --output /tmp/drop7-scaled-d4-distill-audit.json \
  --checkpoint /tmp/drop7-scaled-d4-distill.bin \
  --labels /tmp/drop7-scaled-d4-distill-labels.jsonl

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/d4-long-outcome/d4-distillation/scaled-d4-distill.cpp \
  -o /tmp/drop7_scaled_d4_distill_san
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_scaled_d4_distill_san --self-test \
  --checkpoint /tmp/drop7-scaled-d4-distill-selftest-san.bin
```

The canonical ranking-only audit is
`/tmp/drop7-scaled-d4-distill-audit.json`, SHA-256
`3cf46ac6d4a2f542b8fa1014ce4aec2d069ebf836a65380fdf4a680908dc1dac`.
The 13,216-byte checkpoint SHA-256 is
`d63f76e55b2c3573c328ba0a57ca45a0e61b4c57f4d38f6595a2b16d3956217a`;
the 2,811-record label-file SHA-256 is
`e97f0a00dad76ce0e47bd60d5824e4e921e57b2cb47990b28b5bd4a562dd56bf`.
The raw collection artifact, which preserves the excluded D2 diagnostic, is
`/tmp/drop7-scaled-d4-distill.json`, SHA-256
`a0001ab3daaf4e1274f2ef0056225268af0f5c58a18c30cb71ca9ad28ee6b0b7`.
The source SHA-256 is
`c3e424e74fd296a5b6f58008ef008731e08babff12b45963381b1d4ff7840621`.
No `0x3e...`, `0x7d...`, or `0xd7...` seed was read. Optimized strict-Werror
and ASan/UBSan self-tests cover inherited D4 parity, deterministic training
and checkpoint roundtrip, exact reflection, public-metadata blindness,
legality, the zero-residual D2 anchor, feature bounds, resource limits, and
the fixed seed/gate protocol.

## Independent covered-number reveal reward (rejected at heldout)

`approaches/fair-expectimax/transition-rewards/fair-reveal-reward.cpp` tests whether explicitly rewarding access to
covered discs gives fair full-width D4/s5 a better short-horizon proxy for
future chain potential. The three-policy fitting menu was frozen as stock
(`clear=0`, `reveal=0`), reveal-only (`clear=0`, `reveal=600`), and balanced
(`clear=300`, `reveal=600`). Transition reward sums every wave's exact
`revealed` count, including cascades after a row rise. It uses no phase
schedule, no search-width reduction, no private game metadata, and no
`0x3de5...` or `0x3de6...` result or seed.

The first fitting seed, `0x3def0000`, was run as a separately persisted
diagnostic. Stock scored 140,681 in 90 moves, reveal-only 171,147 in 108, and
balanced 370,588 in 225, so the preregistered adverse-stop rule did not fire.
The continuation froze that artifact byte-for-byte and opened only
`0x3def0001...003`; it did not replay the observed seed. All three arms and
their per-seed resource/throughput records are retained in the canonical
artifact.

| Fitting policy (four seeds) | Mean score | Mean moves | Clears / move | Reveals / move | Score Q25 | Move Q25 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Stock | 110,139.25 | 71.25 | 1.85965 | 1.01404 | 74,049.75 | 52.5 |
| Reveal-only | 117,299.25 | 77.50 | 1.85161 | 0.98387 | 84,789.75 | 60.0 |
| Balanced | **152,413.25** | **98.75** | **2.03797** | **1.14684** | 74,179.50 | 52.5 |

Balanced was selected by mean score and improved all three preregistered
fitting means over stock. It won the score/moves/reveals triple in three of
four leave-one-out folds, exactly meeting the gate; leave-one-out candidate
selection chose balanced three times and reveal-only once. This opened only
the fresh heldout range `0x3df40000...007`.

| Heldout policy (eight seeds) | Mean score | Mean moves | Clears / move | Reveals / move | Score Q25 | Move Q25 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Stock | **108,247.50** | **74.375** | **1.85210** | **0.99832** | **78,035.75** | **57.5** |
| Balanced | 101,147.875 | 70.125 | 1.82353 | 0.99465 | 77,261.00 | 54.5 |

Balanced regressed heldout mean score by 7,099.625 points (6.56%) and mean
survival by 4.25 moves (5.71%). It also regressed reveal throughput, clear
throughput, and both lower quartiles. The strict heldout gate therefore
rejected the hypothesis. No `0x3ebd...` screen, `0x3ebe...` confirmation,
`0x7d...`, or `0xd7...` seed was opened, and this reward must not be shipped
as an improvement.

```sh
clang++ -O2 -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/transition-rewards/fair-reveal-reward.cpp \
  -o /tmp/drop7-fair-reveal-reward
/tmp/drop7-fair-reveal-reward --threads 8 \
  --output /tmp/drop7-fair-reveal-reward.json

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/transition-rewards/fair-reveal-reward.cpp \
  -o /tmp/drop7-fair-reveal-reward-san
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7-fair-reveal-reward-san --self-test-only
```

The resumed run used 474.927 wall seconds; including the prior diagnostic,
the experiment used 860.982 seconds of its fixed 2,100-second limit. Optimized
peak RSS was 89,899,008 bytes. Standalone and library-mode strict-Werror
builds and the ASan/UBSan self-test pass exact stock parity, determinism,
reflection, public-metadata blindness, exact multi-wave/row-rise reveal
accounting, frozen-seed continuation, legality, and work/cache bounds. The
canonical artifact is `/tmp/drop7-fair-reveal-reward.json`, SHA-256
`d18543028169e2aa262d2efea0847e3410886220d0e7bf2d9346533d7a5a92fa`.
The prior diagnostic artifact SHA-256 is
`1e3853efb5c4422fedf1c10db2756410ba1665f7859984f9e4a8897bfefeffac`;
the source SHA-256 is
`53bbb8ab949b8f31974e1514a0f8e476fcd840be2bf84fb65fd79202e83eb8f2`.

## Fair-D4 25-move exact-D2 rollout veto (promising pilot; runtime-paused)

`approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto.cpp` uses the ranking-audited exact public D2 as a
longer-horizon continuation without changing qualified fair D4's ordinary
behavior. Every move completes stock full-width fair D4 first and keeps its
action by default. The rollout is routed at every rise phase only when the
reflection-invariant public maximum column height is at least four.

At a routed root, every legal action receives the same seven deterministic
scenario tapes for 25 moves, spanning five complete rise cycles. Each reveal
event and each later visible disc is exactly stratified across the seven
scenarios in separate event-indexed domains. The tape seed is a canonical
public-state hash. After the fixed root action, every action is selected by a
fresh completed full-width fair D2/s5 search whose API accepts only board,
visible next disc, rise phase, and terminal status; it cannot receive scenario
identity, tape seed, future discs, score, level, or move index.

Rollout return is the unchanged stock search objective: actual score deltas,
one -1,000,000 terminal utility when a scenario dies, or one unscaled fair leaf
at a nonterminal step-25 horizon. There is no clear reward or phase schedule.
An alternative must retain at least as many horizon survivors, have no lower
mean numbered clears, have a strictly positive paired return lower bound using
the fixed `t(6) = 2.446912` cutoff, and remain within 7,000 points of the D4
root-Q. The largest passing lower bound wins; otherwise D4 is exact fallback.
All rules were frozen before a gameplay seed was opened.

The single registered `0x3ded0000` pilot was unusually positive. Stock D4
scored 159,616 in 105 moves, with 1.97143 numbered clears and 1.09524 reveals
per move. The rollout veto scored 404,047 in 250 moves, with 2.276 clears and
1.316 reveals per move: paired changes of +244,431 points and +145 moves. It
switched 12 of 179 routed decisions (4.8% of all 250 moves); 15 alternatives
passed every gate. On switches, mean paired return lower bound was 9,899.72,
mean clear advantage was 5.60714, mean D4 root-Q loss was 2,313.21, and mean
horizon-survivor advantage was 0.3333 scenarios.

This is only one pair, not statistical evidence. It also failed the fixed
runtime gate decisively. The candidate consumed 192,983 D2 calls, 388,480,293
D2 work units, and 201,677 synthetic transitions. Its elapsed time was 852.255
seconds in the original paired process, while the concurrently running stock
D4 game took 191.369 seconds; because the candidate dominated, 852.255 seconds
was also the first-pair wall time. The preregistered projection treats the 72
possible policy-games across fitting, heldout, screen, and confirmation as 18
four-worker waves and conservatively charges that observed pair wall to every
wave. It therefore projected 15,340.599 seconds (4.26 hours), far above the
2,700-second ceiling. The runner paused before `0x3ded0001`. Fitting remainder
`0x3ded0001...003`, heldout `0x3dee0000...007`, fresh screen
`0x3ebb0000...007`, confirmation `0x3ebc0000...00f`, and protected `0x7d...`
and `0xd7...` families remain untouched. The result motivates a separately
preregistered performance optimization or faithful rollout approximation; it
does not authorize relaxing this experiment's gate after seeing the pilot.

The exact resource proof bounds each D2 call at 2,485 work units and 35 cache
entries. A fully live routed decision can make at most 1,176 D2 calls,
2,922,360 D2 work units, and 1,225 synthetic transitions. Every observed D2
root was full width, peak D2 cache occupancy was 35, peak D4 cache occupancy
was 34,686, and peak RSS was 16,416,768 bytes under the 128 MiB cap.

An instrumentation-only replay of the same already-read pilot seed exported
all 179 routed decisions to
`/tmp/drop7-d4-d2-rollout-veto-teacher.jsonl`. Each record contains the exact
public board/next-disc/phase encoding, stock D4 action and all root-Q values,
every legal action's seven survival/clear/return outcomes and summary, every
veto predicate, selected action, and D4/D2 timing, work, cache, and transition
counters. The replay asserted the original 404,047 points, 250 moves, 179
routes, and 12 switches before writing. It is a second execution of the same
unique game seed for instrumentation, not new gameplay evidence.

Without the concurrently running baseline, the replay took 641.689 seconds.
Rollouts consumed 333.585 seconds (51.99% of whole-game time); D4 on the 179
routed states consumed 231.629 seconds (36.10%); the remaining 76.475 seconds
covered D4 on 71 unrouted moves and real transitions. D2 averaged 2,013.03 work
units per call and 1.165 million work units/second. Even an optimistic aggregate
projection using the isolated replay plus the original baseline is 7,497.521
seconds (2.08 hours) across 36 pairs and four workers, still far above the
45-minute limit.

Cost and switches were not isolated to one rise phase: phase-1 through phase-5
routes numbered 32/36/35/37/39 with 1/3/3/3/2 switches. Height four accounted
for 101 routes, 10 switches, and 207.026 rollout seconds; tightening the trigger
to height five would discard most observed changes. Of 1,063 legal alternatives,
120 failed the D4 root-Q band, 214 survival, 598 clears, and 1,048 paired return
(failures overlap). Thus two semantics-preserving future optimizations are
available but insufficient alone: reject the 120 root-Q-ineligible actions
before rollout (9.66% of all 1,242 action roots), and evaluate the D4 action
first so irreversible survivor failures can stop challenger tapes early (360
of 8,694 scenario branches in this replay). A bounded public-state D2 action
cache is also semantics-preserving but needs its own hit-rate audit. Shorter
horizons, fewer scenarios, D2/s3, or a tighter danger trigger would change the
policy; they are hypotheses for a new preregistered experiment, not post-pilot
edits to this one.

```sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto.cpp \
  -o /tmp/drop7_d4_d2_rollout_veto
/tmp/drop7_d4_d2_rollout_veto --self-test
/tmp/drop7_d4_d2_rollout_veto --run \
  --output /tmp/drop7-d4-d2-rollout-veto.json
/tmp/drop7_d4_d2_rollout_veto --trace-pilot \
  --teacher-output /tmp/drop7-d4-d2-rollout-veto-teacher.jsonl

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto.cpp \
  -o /tmp/drop7_d4_d2_rollout_veto_san
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_d4_d2_rollout_veto_san --self-test
```

Optimized standalone and library-mode Werror builds and ASan/UBSan self-tests
pass inherited D4 parity, deterministic and reflection-safe D2 decisions,
public-only continuation typing, full-root and resource proofs, aligned exact
stratification, reveal/visible-domain isolation, fixed-horizon terminal/tail
semantics, metadata blindness, legality, zero-switch parity, and every veto
boundary. The partial artifact is `/tmp/drop7-d4-d2-rollout-veto.json`,
SHA-256
`5841c90412d21c0a42ee6adc7a2233b3b585087bcc04f54fab7ef3274d3a607e`.
The 1,203,731-byte teacher JSONL SHA-256 is
`9dfc244bab8f2fc86b429449008c1d8d0645752dd347849263235f2351f7823e`.
The source SHA-256 is
`2f1018304d9cd2729bdf0c2ac552e2c8e9d0976374675f1be97257c81927f433`.

## Public-D2 25-move outcome ranker (rejected)

`approaches/d4-long-outcome/long-outcome/d2-long-outcome-ranker.cpp` tests a narrower follow-up to the expensive
D4/D2 rollout: can a small NNUE-like action ranker learn the result of those
long continuations and retain exact public D2 as its cheap anchor? This is a
ranking-only experiment. It opens no gameplay seed and makes no score or
million-point claim.

The input is the already-consumed, frozen `0x3df2...` fitting and `0x3df3...`
heldout public-D4 root corpus. Exactly 12 evenly spaced roots are selected from
each whole game: 288 fitting roots from 24 games and 144 heldout roots from 12
games. Roots are stored in their public canonical reflection orientation. For
each legal sibling action, seven common-random-number tapes force that first
action and then play 24 more moves with a fresh, completed, full-width fair-D2
search on every move. Reveal events and later visible discs use independent,
event-indexed domains. Return is accumulated canonical score plus one
-1,000,000 terminal utility if the line dies, or the unchanged fair leaf after
move 25 if it survives.

The candidate reuses the 1,647-weight action-relative sparse residual from the
D4 compression experiment. Its direct and reflected feature streams are
averaged over the normalized exact-D2 root Q. Grouped sibling softmax and
pairwise losses are fit on the 288 fitting roots only. Scoring supports an
exact subtract-old/add-new sparse accumulator; the measured maximum numerical
gap was `1.09e-14`, exact reflection-score gap was zero, and prepared sparse
scoring ran at 1.10 million roots/second. The 13,216-byte checkpoint and all
architecture/gate constants were frozen before any heldout continuation was
generated.

The fitting metrics looked encouraging, but the disjoint result exposed
ordinary overfitting:

| Split / ranker | Top-1 | Top-2 | Pairwise | Normalized regret |
| --- | ---: | ---: | ---: | ---: |
| Fitting exact D2 | 26.04% | 43.75% | 56.90% | 0.33442 |
| Fitting D2 + residual | **34.72%** | **53.82%** | **60.67%** | **0.26712** |
| Heldout exact D2 | **27.78%** | 45.83% | **58.48%** | **0.33050** |
| Heldout D2 + residual | 22.22% | 45.83% | 57.67% | 0.36350 |

The first heldout half regressed especially clearly: top-1 fell from 30.56% to
19.44%, pairwise accuracy from 58.27% to 55.96%, and regret rose from 0.36731
to 0.41521. The second half tied top-1 and improved pairwise accuracy slightly,
but regret still rose. The frozen gate required material overall improvements
in top-1, top-2, pairwise accuracy, and regret, plus top-1 non-regression and
pairwise/regret gains in both six-game halves. It failed eight of ten component
checks. No gameplay screen, `0x3e...`, `0x7d...`, or `0xd7...` family was run;
exact D2 remains the accepted anchor.

The saved outcome JSONL intentionally retains each public root, next disc,
rise phase, all seven sibling Q values, and all seven scenario returns per
action. This supports fitting-only feature audits without regenerating labels.
In particular, an interpretable vertical-ladder-energy feature can simulate
zero through `7 - columnHeight` inert additions and count discounted activation
cost, clears, and waves when numbered discs become equal to the evolving column
height. That explicitly represents stored descending release ladders and is a
better-scoped next hypothesis than increasing the capacity of this rejected
generic residual.

```sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/d4-long-outcome/long-outcome/d2-long-outcome-ranker.cpp \
  -o /tmp/drop7_d2_long_outcome_ranker
/tmp/drop7_d2_long_outcome_ranker --self-test
/tmp/drop7_d2_long_outcome_ranker --run

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/d4-long-outcome/long-outcome/d2-long-outcome-ranker.cpp \
  -o /tmp/drop7_d2_long_outcome_ranker_san
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_d2_long_outcome_ranker_san --self-test
```

The fixed late-root pilot took 0.0277 seconds and passed the 2,700-second
projection gate. The complete four-worker run took 226.112 wall seconds,
processed 777,496,389 bounded D2 work units, peaked at 35 D2 cache entries and
13,713,408 RSS bytes, and passed all implementation/resource checks. Optimized
standalone and library-mode strict-Werror builds and the ASan/UBSan self-test
pass inherited engine parity, deterministic outcome and training behavior,
exact stratification, reveal/visible-domain isolation, terminal semantics,
checkpoint roundtrip, canonical reflection, metadata blindness, the exact-D2
anchor, sparse accumulator equivalence, legality, and resource/protocol bounds.

The canonical artifact is `/tmp/drop7-d2-long-outcome-ranker.json`, SHA-256
`e0976d1ade2d148f6a0d3396e51bd6eaae8c82cf9bdddb031d79129086501734`.
The 432-root outcome JSONL SHA-256 is
`621302a0cd8334fa56e5b77c191beb5529eda0e5413b8e7e20d524c852e7ea7a`;
the checkpoint SHA-256 is
`da7d1992555f2f68ebe2bf63db96edd6b09dbbf223cb7733152a02969c9f6f64`;
the source SHA-256 is
`8fc7501c39ba4290efd06fedd5f9894e364181c0e2cec6b0f3dd110e1f15de97`.

## Exact 25-move rollout-veto compression (still runtime-rejected)

`approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto-exact-compressed.cpp` applies only
outcome-preserving transformations to the promising but expensive policy. It
checks the already-frozen 7,000-point D4 root-Q band before simulating a
challenger, evaluates the D4 baseline first and exactly once, lazily simulates
only root-Q-admissible alternatives, memoizes deterministic D2 actions by
canonical public state within a bounded decision, and reuses an exact
continuation only when observable state, scenario, and tape step all match.
The continuation key contains no score, level, real move count, scenario
future, or other private information.

The optimized fixture suite compares evaluated scenario outcomes and selected
actions bit-for-bit with the original, checks both orientations, verifies that
score/level/move metadata cannot affect a decision, and forces a repeated
continuation to prove the suffix cache. The pilot replay then asserted the
original 404,047 points, 250 moves, 569 numbered clears, 329 reveals, 179
routed decisions, 12 switches, 15 passing alternatives, and 353,804,442 D4
work units before writing its artifact or 179-record trace.

Across the replay's 1,242 legal action roots, the D4-Q prefilter skipped 120
(9.66%) and evaluated 1,122. This reduced the uncompressed 201,677 synthetic
transitions to 187,041 logical transitions. Exact convergent-continuation
reuse then avoided another 26,303, leaving 160,738 actual transitions. The
same two steps reduced 192,983 original D2 calls to 152,884 (20.78%). Once the
suffix cache was active, the decision-local canonical action memo had zero
additional hits; its strict 1,176-entry bound remains useful as a correctness
and memory guarantee, but enlarging it is not a promising speed hypothesis.

| Same-seed isolated replay | Whole game | Rollout phase | Transitions | D2 calls | D2 work | Inner D2 cache | Peak RSS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Original teacher | 641.689 s | 333.585 s | 201,677 | 192,983 | 388,480,293 | 35 | 16,416,768 B |
| Exact compressed | 639.321 s | **295.902 s** | **160,738** | **152,884** | **310,321,943** | 35 | 12,107,776 B |
| Cache-free D2 follow-up | **611.245 s** | 307.445 s | **160,738** | **152,884** | 367,554,502 | **0** | **11,845,632 B** |

The exact compressed rollout phase was 11.29% faster than the isolated
original and used 20.12% less logical D2 work. Whole-game timings include a
large unrelated D4 variation: D4 plus non-rollout time was about 343.4 seconds
in the compressed replay but 303.8 seconds in the cache-free replay. Therefore
the cache-free implementation's 611.245-second whole-game result must not be
read as a superior rollout. Its rollout phase was 3.90% slower than the
generic cached compressed version, despite removing strings and LRU state,
because recomputing the missed depth-one subtrees raised D2 work by 18.44%.
The ordinary cached-D2 exact compressor remains the preferred implementation.

`approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto-cache-free.cpp` nevertheless provides a strong
equivalence proof. On the same already-open seed it compared all 152,884
continuation roots against generic cached D2, including every root Q and
selected action, then repeated all 152,884 comparisons on reflected public
states. Every direct/cached comparison was bit-exact in both orientations.
There were 16 symmetric-board action tie exceptions and seven symmetric-board
root-vector roundoff exceptions to *equivariance*; direct and cached policies
still agreed exactly on each orientation. The proof replay retained the same
404,047/250/12 game result and produced a deterministic audit digest.

Neither exact implementation comes close to the original runtime contract.
The compressed replay projects 11,507.771 seconds across the fixed 18 waves;
the cache-free replay projects 11,002.415 seconds. Both exceed the 2,700-second
limit by more than four times, so `0x3ded0001...003` remains sealed. Heldout
`0x3dee...`, screen `0x3ebb...`, confirmation `0x3ebc...`, protected
`0x7d...`/`0xd7...`, and the separately reserved `0x3d10...`/`0x3d20...`
families were not opened.

The trace evidence freezes, but does not run or tune, a two-item approximation
menu for a separate experiment: `h8-s7-d2` isolates a shorter horizon while
retaining exact D2 and all seven scenarios; `h25-s7-d1` retains the 25-move,
seven-scenario geometry while isolating a cheaper public D1 continuation.
Either requires a new preregistration and fresh fitting discipline before it
can make a policy claim.

```sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto-exact-compressed.cpp \
  -o /tmp/drop7-d4-d2-rollout-veto-exact-compressed
/tmp/drop7-d4-d2-rollout-veto-exact-compressed --self-test
/tmp/drop7-d4-d2-rollout-veto-exact-compressed --replay-pilot \
  --output /tmp/drop7-d4-d2-rollout-veto-exact-compressed.json \
  --trace-output /tmp/drop7-d4-d2-rollout-veto-exact-compressed-trace.jsonl

clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto-cache-free.cpp \
  -o /tmp/drop7-d4-d2-rollout-veto-cache-free
/tmp/drop7-d4-d2-rollout-veto-cache-free --self-test
/tmp/drop7-d4-d2-rollout-veto-cache-free --audit-pilot \
  --audit-output /tmp/drop7-d4-d2-rollout-veto-cache-free-audit.json
/tmp/drop7-d4-d2-rollout-veto-cache-free --replay-pilot \
  --output /tmp/drop7-d4-d2-rollout-veto-cache-free.json \
  --trace-output /tmp/drop7-d4-d2-rollout-veto-cache-free-trace.jsonl

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto-cache-free.cpp \
  -o /tmp/drop7-d4-d2-rollout-veto-cache-free-san
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7-d4-d2-rollout-veto-cache-free-san --self-test
```

Standalone and library-mode strict-Werror builds and ASan/UBSan self-tests
pass inherited engine parity, exact original/compressed action and outcome
parity, D4-Q filter boundaries, canonical public-state memoization, exact
continuation reuse, cache-free full-root values/actions, reflection, metadata
blindness, legality, and resource bounds. Artifact SHA-256 values are:

- Exact compressed JSON: `93873f5b5351311f3834a02dc676df86d74ded3db0166c6e3d719ee80f24fc45`
- Exact compressed trace: `8759601a4fbf2d7234cb84b2efc1adfa03a592511dddab2558f400434e0ca7d7`
- Cache-free audit: `c6aec98bc473e39d5ebe24a55f596172eeac3fd8b60c95c5e53ebdd65727ff8e`
- Cache-free timing JSON: `1e01fbc27ebf81cfa3c6035792cf2e95a0e5d972be075e21f380df7148d4a8ce`
- Cache-free timing trace: `2826467803735d38a2b84ef491180f2b2acd8735fd492671747e60cf547e82a8`

The compressed timing replay used source SHA-256
`ac6bf3d6e88328de21c21add34d7ee4c5ae8e3b8f99edde7e0631744fddd6706`.
The current reusable compressed source, after adding the behavior-neutral D2
evaluator injection point for the follow-up, has SHA-256
`b33058e6a3b07b682d15e38182c41607490ca96c58a6593b1e37291944b056f7`.
The cache-free source SHA-256 is
`61bd35abdf085cfc1d0dcba19ae4dfeed211250cdc76be3414a14a8fa32dcdf0`.

## D2 rollout teacher-compression replay (rejected)

`approaches/d4-long-outcome/rollout-veto/d2-rollout-teacher-compression.cpp` asks whether a materially cheaper
rollout can reproduce the expensive D2/s7/h25 veto teacher. It is a replay-only
lab over the 179 public routed states in the already-exported
`/tmp/drop7-d4-d2-rollout-veto-teacher.jsonl`; it creates no game and reads no
new gameplay seed. The parser locks the 1,203,731-byte export, metadata, index
sequence, 404,047-point/250-move summary, 12-switch label count, root legality,
and every public-state-derived tape seed before evaluation.

The approximation menu and selection rule were frozen before any replay
result. Here `sN` is the number of paired root rollout scenarios; every fair-D1
or fair-D2 continuation retains the stock five-stratum chance model. Every
configuration gives all legal root actions a rollout and preserves the
teacher's survival, mean-numbered-clear, root-Q-loss-at-most-7,000, and positive
one-sided paired-return-lower-bound gates. The t cutoffs correctly change with
scenario count: 4.302653 for s3, 2.776445 for s5, and 2.446912 for s7. No
top-Q action pruning is performed. Eligibility required at least 8 of the 12
teacher switches to reproduce the exact beneficial action and at least 95%
specificity on the 167 teacher fallbacks; eligible configurations would then
maximize balanced agreement, breaking ties on lower continuation work and
finally menu order.

None passed:

| ID / approximation | Exact action | Exact switch recall | Fallback specificity | False switches | Pairwise return-rank agreement | Continuation work | Wall |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| A: D1/s7/h25 | 165/179 | 0/12 | 165/167 (98.80%) | 2 | 56.90% | 12,968,178 | 4.16 s |
| B: D2/s3/h25 | 157/179 | 0/12 | 157/167 (94.01%) | 10 | 53.40% | 166,422,708 | 40.57 s |
| C: D2/s5/h15 | 160/179 | 0/12 | 160/167 (95.81%) | 7 | 57.50% | 170,137,927 | 39.75 s |
| D: D2/s3/h15 | 164/179 | 0/12 | 164/167 (98.20%) | 3 | 54.50% | 102,233,749 | 24.05 s |
| E: D2 only at phase 1, otherwise D1; s7/h25 | 165/179 | 0/12 | 165/167 (98.80%) | 2 | 58.52% | 79,375,085 | 20.23 s |
| F: D2 at phases 1 or 5, otherwise D1; s7/h25 | **166/179** | **2/12** | 164/167 (98.20%) | 3 | **60.07%** | 154,489,125 | 46.66 s |

The high overall agreement is dominated by the 167 fallback labels. The best
hybrid, F, preserved specificity and improved return ordering, but recovered
only two exact beneficial switches—far below the preregistered 8/12 minimum.
The simpler approximations recovered none. Therefore this menu is rejected;
it cannot replace the teacher in a gameplay evaluation, and no remaining
fitting, `0x3e...`, `0x7d...`, or `0xd7...` game was opened.

```sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/d4-long-outcome/rollout-veto/d2-rollout-teacher-compression.cpp \
  -o /tmp/drop7_d2_rollout_teacher_compression
/tmp/drop7_d2_rollout_teacher_compression --self-test
/tmp/drop7_d2_rollout_teacher_compression --run --threads 4

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/d4-long-outcome/rollout-veto/d2-rollout-teacher-compression.cpp \
  -o /tmp/drop7_d2_rollout_teacher_compression_san
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_d2_rollout_teacher_compression_san --self-test
```

The four-worker replay took 175.428 wall seconds and peaked at 3,407,872 RSS
bytes. Optimized standalone and library-mode strict-Werror builds pass. The
ASan/UBSan self-test peaked at 104,136,704 bytes under the 128 MiB cap and
passes byte-locked parsing, exact teacher-kernel parity, deterministic D1/D2,
reflection, metadata blindness and public-only typing, full-width/resource
bounds, 3/5/7-stratum tapes, statistical cutoffs, gate boundaries, and the
lexicographic selector. The 7,129-byte artifact is
`/tmp/drop7-d2-rollout-teacher-compression.json`, SHA-256
`1a47593654ac45581db63e60a1c69a1e3bc4a2cf4f2292cd9342366c663aa1cc`;
the source SHA-256 is
`da1d81df363149588cbc1867b65c4b028c6ba892f05b92d97990d9abec7f5872`.

## Long-outcome ladder and multi-head audit (rejected)

approaches/d4-long-outcome/long-outcome/d2-long-outcome-feature-audit.cpp is an architecture-development-only
follow-up over the already-preserved 288 fitting and 144 old-heldout
long-outcome roots. It opens no new root, tape domain, game, or gameplay seed.
The old heldout was already opened by the preceding scalar experiment, so its
result here is explicitly burned development evidence and cannot be reused as
a formal gate.

The original outcome JSONL retained every seven-scenario return but omitted
clear/survival counters. The audit therefore performs an instrumentation replay
of the identical public roots and deterministic LONG tapes, asserting every
scenario return against the persisted value. Maximum replay error was exactly
zero. It also joins each root's original frozen public-D4 action and complete
root-Q vector by whole-game/move identity. The derived JSONL now retains D4
action/Q, root and seven post-first-step ladder energies, seven returns, seven
survival flags, seven numbered-clear counts, and the aggregate targets. This is
enough for a future separately preregistered out-of-fold emulator of the exact
conservative veto pass/fallback rule; no such classifier was added after seeing
this audit.

The first hypothesis is the exact conservative vertical-ladder feature from
approaches/fair-expectimax/vertical-ladder/d2-vertical-ladder-probe.cpp. For each column and each possible inert
addition count, it repeatedly removes visible numbered cells equal to the
current column height and discounts activation cost, clears, and extra waves.
For each sibling action the audit measures expected post-first-step ladder
energy over the same seven CRN scenarios. Subtracting pre-root energy gives the
delta; because the pre value is constant among siblings, post level and delta
have identical within-root rankings.

| Development metric | Exact D2 | Ladder alone | D2 + fitted ladder |
| --- | ---: | ---: | ---: |
| Fitting nested-CV top-1 | 26.04% | 15.63% | 26.04% |
| Fitting nested-CV pairwise | 56.90% | 50.95% | 56.83% |
| Fitting nested-CV regret | 0.33442 | 0.48291 | 0.33150 |
| Old-heldout top-1 | 27.78% | 20.14% | 27.78% |
| Old-heldout pairwise | 58.48% | 50.54% | 58.63% |
| Old-heldout regret | 0.33050 | 0.44851 | 0.33050 |

The fitting-only least-squares coefficient was -0.02315; all six fold
coefficients were negative. Ladder pair-difference Pearson correlation was
only 0.0133 in fitting and 0.0053 on old heldout. Adding the coefficient changed
almost nothing, was stable on only two of six fitting folds, and failed every
material-improvement component. Literal stored vertical ladders therefore do
exist, but this conservative scalar is not a useful 25-move action ranker.

The second hypothesis is a reflection-exact NNUE-like network with 1,650 inputs,
12 ReLU hidden units, and five heads: normalized mean-return residual over
exact D2, scenario survival, numbered clears, downside, and variance. Direct
and reflected sparse accumulators share weights and are averaged exactly.
Epoch count (40, 80, or 120) is selected in nested whole-game fitting splits:
six outer folds, with the next fold used only for inner selection, followed by
outer retraining. The lower median selected 40 epochs for the final fitting
model. Its 86,140-byte checkpoint is well below the fixed 256 KiB limit.

| Ranker | Fitting nested-CV top-1 | Pairwise | Regret | Old-heldout top-1 | Pairwise | Regret |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Exact D2 | **26.04%** | **56.90%** | **0.33442** | **27.78%** | **58.48%** | **0.33050** |
| Multi-head NNUE | 24.31% | 54.84% | 0.35884 | 22.22% | 56.26% | 0.41081 |

The auxiliary tasks were not all hopeless: old-heldout Pearson correlation was
0.854 for survival and 0.427 for numbered clears. But downside was 0.325,
variance 0.139, and the return residual 0.350; shared representation did not
produce a better action policy. It beat D2 on all three ranking measures in
zero of six outer folds and regressed both old-heldout halves. The frozen
consistency gate rejected it. No new disjoint corpus was preregistered or
collected; exact D2 remains the anchor.

~~~sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/d4-long-outcome/long-outcome/d2-long-outcome-feature-audit.cpp \
  -o /tmp/drop7_d2_long_outcome_feature_audit
/tmp/drop7_d2_long_outcome_feature_audit --self-test
/tmp/drop7_d2_long_outcome_feature_audit --run

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/d4-long-outcome/long-outcome/d2-long-outcome-feature-audit.cpp \
  -o /tmp/drop7_d2_long_outcome_feature_audit_san
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_d2_long_outcome_feature_audit_san --self-test
~~~

The final deterministic replay took 186.290 seconds and the complete audit
188.492 seconds. Peak RSS was 15,351,808 bytes; exact direct/reflected
accumulator-swap gap was zero; prepared NNUE inference ran at 2.57 million
action evaluations/second. Optimized standalone and library-mode
strict-Werror builds and ASan/UBSan self-tests pass inherited parity, the exact
ladder fixture, persisted-return replay, deterministic training, checkpoint
roundtrip, reflection, metadata blindness, legality, resource bounds, and the
fixed split/protocol.

The canonical artifact is
/tmp/drop7-d2-long-outcome-feature-audit.json, SHA-256
4b6f78dd2c765ff2ca3db529cc6cba47c069ed279a8d0e97a88ea72b807726e5.
The joined 432-root derived JSONL SHA-256 is
b75363c1071fb2eb93401dda899b944f93c31b3172c9168899a307d978135c6c;
the multi-head checkpoint SHA-256 is
40425af16b8ed6577545579e2e004e8a909974ed67a86488aaa906155623a93b;
the source SHA-256 is
d0fab5c4eee45ebbf07785fa130ac574199f7a7ca5b9d19e20030a4963516416.

## Original D4 + D2/s7/h25 quality extension (rejected)

`approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto-quality-extension.cpp` extends the original
runtime-rejected teacher solely to measure fitting quality. It includes the
unchanged D4 + D2/s7/h25 implementation and changes no policy constant. The
old 45-minute deployment gate remains failed and is ignored only for this
explicitly non-deployable audit. The existing `0x3ded0000` pair was loaded from
the byte-locked pilot artifact, not replayed. The only new game tapes opened
were the previously untouched fitting seeds `0x3ded0001...003`, with all three
baselines concurrent and then all three candidates concurrent. No `0x3dee`,
other `0x3e`, `0x7d`, or `0xd7` seed was opened.

Before running, the four-game quality gate required higher candidate mean
score and moves; at least two of the three new pairs to improve both score and
moves; higher aggregate numbered-clears/move and covers-revealed/move; at
least 90% retention of lower-half score and moves; and at least three of four
leave-one-out subsets with positive mean score and move differences. The wall
cap was 5,400 seconds.

| Seed | Stock D4 score / moves | Veto score / moves | Switches / routes |
| --- | ---: | ---: | ---: |
| `0x3ded0000` (frozen) | 159,616 / 105 | 404,047 / 250 | 12 / 179 |
| `0x3ded0001` | 109,264 / 70 | 153,925 / 100 | 1 / 87 |
| `0x3ded0002` | 59,004 / 45 | 59,004 / 45 | 0 / 35 |
| `0x3ded0003` | 100,147 / 70 | 81,662 / 60 | 1 / 50 |

The candidate mean was 174,659.5 points and 113.75 moves versus 107,007.75
and 72.5 for stock D4. Clears/move improved from 1.84483 to 2.05055 and
reveals/move from 0.98966 to 1.12967. All four leave-one-out subsets were
positive, and lower-half moves retained 91.30%. The frozen gate nevertheless
failed: only one of three new pairs was a joint score-and-move win, and
lower-half score was 70,333 versus 79,575.5, or 88.38% retention. The quality
audit is therefore rejected; even a quality pass could not have reversed the
separate runtime rejection.

The three new baselines took 86.100 wall seconds and the three concurrent
candidates 240.054 seconds. The complete audit took 333.120 seconds, peak
production RSS was 26,394,624 bytes under the 128 MiB cap, all games terminated
uncensored, and every continuation root remained full width. The artifact
retains every game's score, moves, clear/reveal counts, switch predicates,
D4/D2 work, nodes, caches, transitions, and elapsed time.

```sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto-quality-extension.cpp \
  -o /tmp/drop7_d4_d2_rollout_veto_quality_extension
/tmp/drop7_d4_d2_rollout_veto_quality_extension --self-test
/tmp/drop7_d4_d2_rollout_veto_quality_extension --run

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto-quality-extension.cpp \
  -o /tmp/drop7_d4_d2_rollout_veto_quality_extension_san
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_d4_d2_rollout_veto_quality_extension_san --self-test
```

Optimized standalone and library-mode strict-Werror builds and the ASan/UBSan
self-test pass inherited engine/TypeScript parity, deterministic and reflected
D4/D2 behavior, public-only/metadata-blind boundaries, full-root work/cache
proofs, tape-domain separation and stratification, terminal semantics, the
exact three-seed allowlist, frozen-pilot reconstruction, wall-stop wiring, and
all quality-gate boundaries. Sanitizer shadow/quarantine RSS is excluded from
the production cap; production games enforce it directly.

The 11,739-byte artifact is
`/tmp/drop7-d4-d2-rollout-veto-quality-extension.json`, SHA-256
`a91a86313e861bdbc22ef5930b8e87869a013722b531e5cb3b3300523416f0a4`.
The source SHA-256 is
`832911c4ba019ba1cf66ccd34bc4fe3a656d7ed01b5bb74a0f64175755edb4ce`.

## Literal vertical-ladder energy (rejected at D4 transfer)

`approaches/fair-expectimax/vertical-ladder/d2-vertical-ladder-probe.cpp`
tests a literal reward for numbers deliberately stacked for a later chain
without looking at hidden
discs or a future random tape.  For each visible column and each possible
number of inert additions, the feature repeatedly removes numbered discs equal
to the resulting column height.  Multi-wave clears receive a quadratic wave
bonus and are discounted by the number of additions needed to activate them.
Horizontal help and covered-disc reveals are deliberately ignored, making this
a conservative measure of energy already stored in a visible vertical ladder.

The only coefficient sweep used 256 training-family D2 games at
`0x3d9e0000...00ff`, capped at 1,000 moves.  Weight 500 was the unique fixed
choice for transfer: it raised mean score from 98,642.29 to 103,467.46 and
mean lifetime from 68.77 to 71.59 moves.  Clears/reveals per move improved from
1.84857/1.00034 to 1.87227/1.01855, and both ordered halves improved.  The
paired lower bounds were still negative (-3,032 points and -1.995 moves), the
lower-quartile score slipped from 61,074 to 60,739, and weights 1,000 through
4,000 were harmful.  The small D2 gain was therefore only a coefficient-selection
signal, not qualification evidence.

`approaches/fair-expectimax/vertical-ladder/fair-vertical-ladder-depth4.cpp` then froze weight 500 and transferred it
once to the qualified full-width public D4 search on the disjoint training-only
range `0x3d9f0000...007`.  The leaf result is exact and fast: every gravity-valid
base-9 column state has a shared atomic cache slot, so all 5,380,840 entries use
21,523,360 bytes and avoid an unbounded board transposition table.  One thousand
fixtures proved bit-exact equality with the direct feature.

The D4 transfer rejected the feature.  Stock averaged 124,934.38 points and
83.875 moves; the candidate averaged 121,848.63 and 82.125.  The paired changes
were -3,085.75 points and -1.75 moves, with 95% lower bounds -56,964.47 and
-35.259.  It won 4/8 score pairs and 4/8 move pairs, and its lower quartiles
fell from 90,673/65 moves to 82,277/58.75.  Although clears/reveals per move
rose to 1.93303/1.06088, that throughput did not translate into longer games.
No policy seed outside these two `0x3d...` ranges was opened, and no model was
retuned after transfer.

```sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/vertical-ladder/d2-vertical-ladder-probe.cpp \
  -o /tmp/drop7_d2_vertical_ladder_probe
/tmp/drop7_d2_vertical_ladder_probe --self-test
/tmp/drop7_d2_vertical_ladder_probe \
  --seed-start 0x3d9e0000 --games 256 --threads 8 --max-moves 1000

clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/vertical-ladder/fair-vertical-ladder-depth4.cpp \
  -o /tmp/drop7_fair_vertical_ladder_depth4
/tmp/drop7_fair_vertical_ladder_depth4 --self-test
/tmp/drop7_fair_vertical_ladder_depth4 --run \
  /tmp/drop7-fair-vertical-ladder-depth4.json

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/fair-expectimax/vertical-ladder/fair-vertical-ladder-depth4.cpp \
  -o /tmp/drop7_fair_vertical_ladder_depth4_san
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_fair_vertical_ladder_depth4_san --self-test
```

The repeated D2 audit took 84.230 seconds and the D4 cohort 844.028 seconds.
All games were uncensored.  D4 used 2.709 million logical work units per
candidate move and peaked at 37,011,456 RSS bytes under the 128 MiB cap.
Optimized strict-Werror builds and the ASan/UBSan self-test pass inherited
engine parity, deterministic/reflection-safe actions, metadata blindness,
full-width completion, cache equality, legality, seed bounds, and resource
proofs.  The D4 artifact is
`/tmp/drop7-fair-vertical-ladder-depth4.json`, SHA-256
`db6b6b6bb2214317200f4ead1e0243e88da797b0444fac479a79099d21326e93`.
The D2 source SHA-256 is
`a41c6562e1f360a8fde29af2ff74f34b50498f4fec5806bd57e8f539ee4dac8c`;
the D4 source SHA-256 is
`098f95201d8e8ba3daebe2c75eb4de0d00ff10f120dc1f9ad924efc94dbe15b7`.

## Long-outcome D4 veto classifier (rejected)

`approaches/d4-long-outcome/long-outcome/d4-long-outcome-veto-classifier.cpp` asks a deliberately narrower
question than the failed global long-outcome ranker: can the useful survival
and numbered-clear heads identify a small set of alternatives that are safe
enough to override exact public D4?  D4 is the immutable default.  An action is
a positive training target only when its D4 root-Q loss is at most 7,000, its
seven common-random-number 25-move continuations improve by at least 10,000 on
average with a positive paired t lower bound, and neither survival nor mean
numbered clears regress.

The nine public features are D4-Q loss, predicted long-return, survival,
numbered-clear, downside and inverse-variance deltas, exact D2 and immediate
score deltas, and maximum public column height.  A balanced L2 logistic model
may switch only at probability 0.90 and only after predicted survival and clear
prefilters.  Its stacked audit is nested by whole game: every outer game's
head and classifier training excludes that game, while inner cross-fitted head
predictions train the classifier.  The frozen gate required at least 80%
precision, 20% coverage of roots with an eligible alternative, a 10,000 mean
gain, paired-scenario q10 of at least -7,000, at least 85% D4 fallback, 90%
survival and clear retention, 12 switches, four active folds, and four stable
folds.  The previously burned 144-root split is reported only as additional
architecture-development evidence.

The veto failed decisively.  The 288-root nested fitting audit contained only
seven roots with an eligible alternative.  It made 19 switches, but zero were
true positives: precision and coverage were both zero, paired-scenario q10 was
-39,674.24, survival retention was 84.21%, clear retention was 57.89%, and
zero of six folds was stable.  Mean return happened to rise 14,165.41, which
illustrates why mean-only selection is unsafe here.  On old heldout data it
made three switches against six eligible roots, again with zero true
positives; q10 was -39,470.31 and clear retention 33.33%.  Both frozen gates
rejected the classifier.  No new game, root, tape domain, or reserved seed was
opened, no disjoint protocol was proposed, and exact D4 remains unchanged.

~~~sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/d4-long-outcome/long-outcome/d4-long-outcome-veto-classifier.cpp \
  -o /tmp/drop7_d4_long_outcome_veto_classifier
/tmp/drop7_d4_long_outcome_veto_classifier --self-test
/tmp/drop7_d4_long_outcome_veto_classifier --run

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/d4-long-outcome/long-outcome/d4-long-outcome-veto-classifier.cpp \
  -o /tmp/drop7_d4_long_outcome_veto_classifier_san
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_d4_long_outcome_veto_classifier_san --self-test
~~~

The final audit took 3.692 seconds and peaked at 14,630,912 RSS bytes.  The
86,412-byte combined head/classifier checkpoint is below the frozen 256 KiB
cap; veto selection after prepared head predictions ran at 30.48 million
roots/second.  Optimized standalone and library-mode strict-Werror builds and
the ASan/UBSan self-test pass inherited engine/TypeScript parity, exact D4
fallback, deterministic training and checkpoint round trips, outcome-label and
metadata blindness at deployment, the synthetic high-confidence switch, and
explicit zero-switch gate rejection.

The 7,954-byte artifact is
`/tmp/drop7-d4-long-outcome-veto-classifier.json`, SHA-256
`ed39d9b4d1df7f88b16913f4a32b7047f646018575cb925048ba8666f3fe00a3`.
The 272-byte classifier checkpoint SHA-256 is
`ac4e13de594169cf22f917a2fcd73b92a6f8764083edbcbe41a552fd5b8c96d3`;
the source SHA-256 is
`038a1ccaf8b8f9564428d8225e8b29e0d87f4715c4f2e8710e1cc8649cf64857`.

## Relaxed chain-potential feature audit (rejected)

`approaches/d4-long-outcome/long-outcome/relaxed-chain-potential-audit.cpp` broadens the rejected literal
vertical ladder without opening any game, seed, root, or future tape. For every
legal action at each of the preserved 288 fitting and 144 already-burned
old-heldout roots, it replays only the seven existing LONG first-step CRN
outcomes. From each public successor it exhaustively tries one or two chosen
visible numbered drops across all values 1–7 and all legal columns. A separate
build-then-release feature requires the first hypothetical drop to be quiet
and the second to trigger.

The simulator follows numbered pops, gravity, level rises, exact wave-score
multipliers, clears, and covers hit/revealed. It never samples a hypothetical
future value. A cover that would reveal becomes an occupied inert-unknown token
which affects line lengths but can never pop or reveal again. Energy is exact
wave score plus 14 per clear and 28 per revealed cover, divided by hypothetical
drop count and by `1 + 0.25 * max(0, height - 4)^2`; terminal paths receive an
additional risk divisor. Eleven fixed summaries include best one-drop,
two-drop, relaxed-any, and quiet-release energy plus wave score, clears,
reveals, waves, activation cost, height risk, and quiet-release opportunity.

A fixed 11-feature ridge residual augments exact D2. Six outer whole-game folds
measure fitting performance; the next whole-game fold selects ridge strength
from 0.0001/0.001/0.01/0.1, with all remaining folds used for inner training.
The lower median selected strength trains the final fitting model. Before any
features were generated, acceptance required raw pair-difference Pearson of
at least 0.10 fitting and 0.08 old heldout, positive correlation in both
heldout halves, nested-CV and heldout gains of 2 points top-1, 1 point top-2,
0.5 points pairwise, at least 5% lower regret, five of six fully stable folds,
and non-regression on all four ranking metrics in both heldout halves.

The relaxed feature was real but too weak and unstable:

| Split / ranker | Top-1 | Top-2 | Pairwise | Regret | Pair Pearson |
| --- | ---: | ---: | ---: | ---: | ---: |
| Fitting exact D2 | **26.04%** | 43.75% | **56.90%** | **0.33442** | 0.2164 |
| Fitting raw relaxed | 21.18% | 37.85% | 54.64% | 0.42690 | 0.0505 |
| Fitting nested-CV D2 + relaxed | 25.00% | **44.10%** | 56.24% | 0.33914 | 0.2003 |
| Old-heldout exact D2 | **27.78%** | **45.83%** | **58.48%** | **0.33050** | 0.2614 |
| Old-heldout raw relaxed | **27.78%** | 44.44% | 55.99% | 0.35445 | 0.0517 |
| Old-heldout D2 + relaxed | 25.69% | 41.67% | 57.40% | 0.33887 | 0.2309 |

Raw quiet build-then-release Pearson was 0.0838 fitting and 0.0333 heldout.
Only one of six outer folds non-regressed on all four metrics. Both heldout
halves regressed under the combined model, although raw relaxed correlation
remained positive in each (0.0925 and 0.0200). Every material gate therefore
failed, so the feature is rejected and no transfer/gameplay proposal is made.

~~~sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/d4-long-outcome/long-outcome/relaxed-chain-potential-audit.cpp \
  -o /tmp/drop7_relaxed_chain_potential_audit
/tmp/drop7_relaxed_chain_potential_audit --self-test
/tmp/drop7_relaxed_chain_potential_audit --run

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/d4-long-outcome/long-outcome/relaxed-chain-potential-audit.cpp \
  -o /tmp/drop7_relaxed_chain_potential_audit_san
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_relaxed_chain_potential_audit_san --self-test
~~~

The four-worker audit evaluated 20,643 preserved first-step outcomes and
47,175,065 bounded hypothetical moves in 2.365 seconds, with zero stored-mean
error and 14,221,312 peak RSS. Optimized standalone and library-mode
strict-Werror builds and the ASan/UBSan self-test pass conservative reveal and
inert-token semantics, quiet release, reflection, deterministic exhaustive
bounds, activation/height normalization, ridge fitting, corpus byte/count
locks, and frozen-gate wiring.

The 8,638-byte artifact is
`/tmp/drop7-relaxed-chain-potential-audit.json`, SHA-256
`38fb8ffb8ef54dbb7d645f05330dbbc19382e6d7daf4f0e94b6ee31d402872b5`.
The 572,818-byte derived feature JSONL SHA-256 is
`a3985f2cdaf192a2aee30e6bc8d519f44b84abcb9d22d1204f4d969199fe8cb1`;
the source SHA-256 is
`ba612c13ead6ea23618957cc6770a63cecd14e2a67652a771a29570dd6c2d42e`.

## Shared-parameter temporal-coherence correction (valid bug, rejected policy)

The afterstate equations in Szubert and Jaśkowski's
[original 2048 study](https://www.cs.put.poznan.pl/wjaskowski/pub/papers/Szubert2014_2048.pdf)
and the later
[delayed-TC paper](https://arxiv.org/abs/1604.05085) clarify which parts of
`approaches/ntuple-rl/temporal-coherence/ntuple-tc.cpp` transfer to Drop7.  Let `x = (board, moves-to-rise)` be
the public state just before the next numbered disc is sampled, let `d` be the
disc visible at decision time, and let `w` contain only stochastic covered-disc
reveals.  The correct equations are

~~~text
Q(x, d, a) = E_w [ r(x, d, a, w) + gamma U(x') ]
U(x)       = E_d [ max_a Q(x, d, a) ]
delta      = r + gamma U(x') - U(x)
~~~

with a zero continuation at terminal.  The implementation's disc-independent
chance value, sampled TD target, and inclusion of the current move reward in
action evaluation are therefore conceptually sound.  A direct copy of 2048's
deterministic afterstate is not: Drop7 can reveal random numbered discs while
resolving the action's cascade, so a true pre-chance afterstate would also need
to encode the unresolved cascade, reveal sites, depth, phase, and accrued
reward.  Delayed TC is only an access optimization at lambda zero; it cannot
repair the value target.

The audit did find a concrete shared-parameter gradient bug.  The translated
shared tables can activate the same weight many times, but the old update
visited every occurrence independently and divided by the constant 338.  On
the initial board, 338 occurrences collapse to 220 unique parameters, one
weight occurs 25 times, and the squared feature norm is 2,294.  Consequently,
the requested update changed the current prediction by 2,294 / 338 = 6.787
times the intended step.  It also added the current error to TC's `E` and `A`
before calculating the adaptive rate; Algorithms 2 and 4 calculate the rate
from prior history and update `E`/`A` afterward.

The corrected semi-gradient aggregates each shared parameter's multiplicity
`c`, normalizes by `sum(c^2)`, applies `c * delta` once, and then updates the TC
history.  A deterministic self-test proves an exact `alpha * delta` prediction
change, including the paper's one-sample-delayed response to an error-sign
reversal.  This is a correctness fix, but it did not improve the policy.

Before training, the 10,000-game stop gate was frozen at 100,000 points and 70
moves against the already-known qualified D4 reference of 176,925.25 and
116.375.  Score-TD used only `0x3d100000...0x3d10270f` and the previously
burned `0x3d200000...0x3d20003f` probe.  It reached 66,625.125 points and
49.469 moves, below both the gate and the legacy 10k score-TD result of
73,480.453 / 53.766.  Training stopped; the remaining 90,000 games were not
run.

One preregistered terminal-MC replay then reused exactly those already-opened
games and probes, with no new seed.  Corrected MC reached 66,296.953 / 49.312,
slightly below legacy MC's 66,442 / 49.453 at 10k (and 78,194.234 / 57.031 at
100k).  It also failed the same gate, so no further run is justified.  The
remaining representation concern is structural: phase currently enters as
one additive scalar, so tuple values cannot interact with proximity to a
covered-row rise.  Phase-conditioned tables or stages would be a separate
architecture experiment, not evidence rescued from this failed correction.

~~~sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/ntuple-rl/temporal-coherence/ntuple-tc.cpp \
  -o /tmp/drop7_ntuple_tc_gradient_corrected
/tmp/drop7_ntuple_tc_gradient_corrected --self-test

/tmp/drop7_ntuple_tc_gradient_corrected --train \
  --games 10000 --training-seed-start 0x3d100000 \
  --probe-seed-start 0x3d200000 --probe-games 64 \
  --max-moves 1000 --chance-samples 7 --report-every 10000 \
  --update-target td0 --reward score-delta --score-scale 1000 \
  --learning-rate 0.1 --optimistic-value 60 --epsilon 0.01 \
  --checkpoint /tmp/drop7-ntuple-scoretd-gradient-corrected-10k.bin

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/ntuple-rl/temporal-coherence/ntuple-tc.cpp \
  -o /tmp/drop7_ntuple_tc_gradient_corrected_san
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_ntuple_tc_gradient_corrected_san --self-test
~~~

Standalone and library-mode strict-Werror builds and the ASan/UBSan self-test
pass exhaustive indices, reflection, deterministic public sampling, reward
modes, shared-gradient aggregation, normalization, TC ordering, legality, and
the 256 MiB model bound.  No `0x3d210000`, `0x3e`, `0x7d`, or `0xd7` seed was
opened.  The 4,351-byte audit artifact is
`/tmp/drop7-ntuple-tc-gradient-audit.json`, SHA-256
`a8f5a2eaeb6402cae8712901b10f22824b60ff32830072c0fe12fdfe7145cec7`.
The score-TD and MC checkpoints are 111,853,296 bytes with SHA-256
`2aab40fae71a5aea0546b34485f073d6328dead5001be4438f1b10350537d4ef`
and `945b876136d14fe813907a81b38dd49f6c290bebd1e655cb8435340069baa924`,
respectively.  The source SHA-256 is
`b8c51c5da1891df764a22f4eaf143cc0a151ac7a62c5bb1d45c21eecf4eb2dee`.

## Phase-conditioned n-tuple residual (rejected)

The next bounded hypothesis tested the structural concern above without
altering the corrected Bellman target or temporal-coherence update.  Inspired
by multi-stage 2048 value functions, `approaches/ntuple-rl/temporal-coherence/ntuple-phase-conditioned.cpp`
retains all 9,321,107 general-model nodes and adds a residual bank keyed by
`moves_remaining`.  Each of the five rise phases has 92 absolute local tables:
all 28 horizontal four-tuples, 28 vertical four-tuples, and 36 2x2 tuples.
Drop7's phase cycles from five to one and back to five, so this is a cyclic
interaction rather than a monotone stage switch; no 2048-style weight
promotion is performed.

The architecture and gate were frozen before replay.  The 4,600,000 new nodes
start at exactly zero, while the general nodes retain the corrected baseline's
`60 / 338` optimistic initialization.  This isolates learned phase residuals
instead of giving the larger model an extra prior.  The combined model has
13,921,107 twelve-byte TC nodes (167,053,284 parameter bytes, 159.314 MiB) and
430 active occurrences.  On the initial state these reduce to 312 unique
parameters with squared gradient norm 2,386 and maximum multiplicity 25; all
92 phase IDs are distinct and disjoint from the general tables.  The update
continues to aggregate shared multiplicities, divide by the exact squared
gradient norm, and compute TC's adaptive rate from history preceding the
current sample.

The only run replayed the already-opened 10,000 training games
`0x3d100000...0x3d10270f` and 64 development games
`0x3d200000...0x3d20003f`.  It used score-delta TD(0), score scale 1,000,
`alpha = 0.1`, optimistic value 60, epsilon 0.01, gamma 1, seven stratified
reveal samples, and a 1,000-move cap.  The preregistered pass condition was at
least 100,000 mean points and 70 mean moves, plus gains of at least 20,000
points and 12 moves over corrected TD.  That closes at least 30% of both gaps
from corrected TD (66,625.125 / 49.469) to qualified fair D4
(176,925.25 / 116.375).

After 452,693 training transitions, the phase model reached **68,463.250
points and 50.828 moves**.  It gained only 1,838.125 points and 1.359 moves
over corrected TD, closing 1.667% and 2.031% of the respective D4 gaps.  The
64-game range was 28,615...224,920 points and 25...155 moves, with no censored
games.  It therefore failed every material gate and is rejected; no 100k
continuation, phase sweep, or alternate seed was run.  The result suggests
that access to rise phase is real but not the principal missing representation
at this training scale.

~~~sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/ntuple-rl/temporal-coherence/ntuple-phase-conditioned.cpp \
  -o /tmp/drop7_ntuple_phase_conditioned
/tmp/drop7_ntuple_phase_conditioned --self-test
/tmp/drop7_ntuple_phase_conditioned --run

clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  -DDROP7_NTUPLE_PHASE_CONDITIONED_LIBRARY -c \
  approaches/ntuple-rl/temporal-coherence/ntuple-phase-conditioned.cpp \
  -o /tmp/drop7_ntuple_phase_conditioned.o

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/ntuple-rl/temporal-coherence/ntuple-phase-conditioned.cpp \
  -o /tmp/drop7_ntuple_phase_conditioned_san
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_ntuple_phase_conditioned_san --self-test
~~~

Standalone and library-mode strict-Werror builds and the ASan/UBSan self-test
pass the inherited Bellman/TC checks plus exhaustive phase indices,
zero-residual initialization, reflection-safe action mapping, deterministic
public sampling, disc independence, terminal continuation, exact normalized
combined gradients, active/collision audits, memory bounds, seed locks, and
gate-boundary wiring.  Peak production RSS was 168,722,432 bytes (160.828
MiB).  No `0x3d210000`, `0x3e`, `0x7d`, or `0xd7` seed was opened.

The 3,021-byte artifact is
`/tmp/drop7-ntuple-phase-conditioned-audit.json`, SHA-256
`a561e1e88584cd5cb4e2453e3ddd2329ec2c8a8977e000e9ca75213196fc28cb`.
The 167,053,296-byte rejected checkpoint SHA-256 is
`e46107f2a1cc9161563e8a1c552c03f438fc3b387cebedb81f999a42666e7c7f`.
The source SHA-256 is
`7e933a7807ec3e0da7be38f981d2bbc0077f75e835cfcbfb5e5815e805f82049`.

## Scaled long-outcome NNUE sample-bottleneck audit (rejected)

`approaches/d4-long-outcome/long-outcome/scaled-long-outcome-nnue.cpp` tests whether the earlier 288-root
long-outcome NNUE failed merely because it was sample-starved. It opens no new
game or gameplay seed: the only input is the locked 527,391-byte public-D4
root corpus (SHA-256
`f61801abc9eefe86011f7202620a18c1277fcc1b5a24f4bce5947033b791dd89`).
This supplies 1,508 fitting roots from 16 whole games, 5.236 times the earlier
sample count, plus 465 roots from eight already-burned old-heldout games.

Each legal sibling receives seven common-random-number continuations of 25
moves. The first action is forced and every subsequent action is selected by
a fresh, full-width exact public-D2 search. New deterministic domains `SCLE`,
`SRVL`, and `SVIS` keep the tapes separate from earlier experiments. The
target records mean return, horizon survival, normalized clears, downside,
variance, and expected post-first-step vertical-ladder energy. The one-root
projection gate passed; four workers then generated all 1,973 roots in
779.731 seconds, covering 2,004,494 synthetic transitions and 1,910,547 exact
D2 decisions.

The fixed comparison used the same 1,650 public/action-relative inputs, five
heads, 40 epochs, losses, optimizer, and exact direct/reflected accumulator
mean for both networks. The earlier capacity has 12 hidden ReLUs; the larger
candidate has 48. Four outer folds split entire fitting games by game index
modulo four. No metric selected a hyperparameter, and the old heldout was
opened only after fitting the final models.

The larger model learned some scalar targets more accurately but made move
ordering worse:

| Split / ranker | Top-1 | Top-2 | Pairwise | Regret | Survival r | Downside r |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Whole-game CV exact D2 | **27.79%** | **46.02%** | **57.44%** | **0.34828** | 0.0209 | 0.0800 |
| Whole-game CV 12 hidden | 25.99% | 41.98% | 55.70% | 0.36251 | **0.8547** | **0.3829** |
| Whole-game CV 48 hidden | 23.94% | 40.52% | 55.15% | 0.37018 | 0.8494 | 0.3550 |
| Old-heldout exact D2 | **30.75%** | **48.39%** | **59.61%** | **0.33356** | 0.0403 | 0.1107 |
| Old-heldout 12 hidden | 26.02% | 41.94% | 57.80% | 0.35371 | 0.8185 | 0.3758 |
| Old-heldout 48 hidden | 24.73% | 42.58% | 56.88% | 0.38022 | **0.8291** | **0.3972** |

The 48-hidden model had zero of four fully non-regressing folds. It failed
both CV ranking comparisons, both burned-heldout ranking comparisons, both
heldout ranking halves, both CV head-correlation gains, and the heldout
survival gain. Only its heldout downside gain and per-half head non-regression
passed. The frozen conjunction therefore rejects the sample-bottleneck
hypothesis and does not propose a gameplay transfer.

This experiment predates the scoring correction: its engine and all return
labels use the historical **7,000-point Sequence-style level bonus**, not the
confirmed **17,000-point five-drop Hardcore/Blitz bonus**. The JSONL metadata
and audit artifact state that boundary explicitly. This is useful
architecture evidence, not score-calibrated Hardcore/Blitz evidence, and no
fresh score-sensitive test was started. The source now deliberately asserts
the historical 7k dependency, so it cannot silently rerun against the
corrected 17k shared engine.

~~~sh
# These commands describe the validated historical-7k build. The source lock
# intentionally stops them against the corrected 17k shared engine.
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/d4-long-outcome/long-outcome/scaled-long-outcome-nnue.cpp \
  -o /tmp/drop7_scaled_long_outcome_nnue
/tmp/drop7_scaled_long_outcome_nnue --self-test
/tmp/drop7_scaled_long_outcome_nnue --run

clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  -DDROP7_SCALED_LONG_OUTCOME_NNUE_LIBRARY -c \
  approaches/d4-long-outcome/long-outcome/scaled-long-outcome-nnue.cpp \
  -o /tmp/drop7_scaled_long_outcome_nnue.o

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/d4-long-outcome/long-outcome/scaled-long-outcome-nnue.cpp \
  -o /tmp/drop7_scaled_long_outcome_nnue_san
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_scaled_long_outcome_nnue_san --self-test
~~~

Before the scoring correction, standalone and library-mode strict-Werror
builds and the ASan/UBSan self-test passed inherited engine/TypeScript parity,
SHA and corpus locks, deterministic training, exact reflection averaging,
checkpoint round trips, model bounds, and positive/negative gate wiring. The
full run took 790.961 seconds, peaked at 91,734,016 RSS bytes, and stayed below
the 256 MiB cap. The 12-hidden and 48-hidden checkpoints are 86,140 and
324,604 bytes, both below 512 KiB.

The 16,305-byte audit artifact is
`/tmp/drop7-scaled-long-outcome-nnue.json`, SHA-256
`044f6f9f56cc3d23302bd2f0b0d9d1bbe7c79b52dd6e4bb1e79654649068710f`.
The 8,610,104-byte labeled corpus SHA-256 is
`9504d067bf88d1d21659a4c02fef48bb11c47a07de33a4f30b6950fc7051ef05`.
The small and large checkpoint SHA-256 values are
`34dd5d269661e960dfc77d18cd734b157e3d85a40f1fa185a59b92065a5001a0`
and `f065fb98057e07bd220e9e0d857f6803a42d15ab7cc12d4129f9a1ef5adae708`.
The annotated historical source SHA-256 is
`11c1af35d5181d1256b9e380364dbea5ce1b62023e525b0038d6ea97e62ccb66`.

## Corrected Hardcore scoring replay

After the mode audit restored the five-drop Hardcore/Blitz level award to
17,000, the TypeScript suite and a 256-seed native/TypeScript parity sweep
passed exactly across 6,852 transitions. The qualified full-width fair-D4
policy was then rebuilt and deterministically replayed on its already-consumed
screen and confirmation seeds; no new gameplay seed was opened.

On the four-game screen, D3 averaged 318,528.75 points/93 moves and D4 averaged
399,951.75/115, a paired gain of 81,423 points and 22 moves. On the eight-game
confirmation, D3 averaged 235,071.25/71 and D4 averaged 400,675.25/116.375. D4
won seven of eight confirmation pairs, with paired gains of 165,604 points and
45.375 moves; neither arm was censored at 1,000 moves. D4 used 1,351,112.595
logical work units per move, retained at most 36,105 cache entries, peaked at
30,441,472 RSS bytes, and ran at 0.744 moves per aggregate game-second.

This establishes the corrected-score baseline, not the qualification target: its
confirmation mean is still about 40% of one million. The replay artifact is
`/tmp/drop7-fair-only-depth4-hardcore17-replay.json`. The production score
constant is regression-tested as 17,000, while experiments explicitly marked
`levelBonus: 7000` above remain historical Sequence-scored evidence only.

## Scaled public-state observable MCTS (rejected before gameplay)

`approaches/tree-search/observable-mcts/observable-mcts-scaled-audit.cpp` is a ranking-only architecture audit
of `approaches/tree-search/observable-mcts/observable-mcts-lab.cpp`; the original source is unchanged. The old
search's three important limitations are explicit: each action edge draws at
most eight chance outcomes and then replays only that small empirical
reservoir, rollout actions come from a myopic public phase-D1 policy, and a
nonterminal horizon returns exactly zero. The candidate was frozen before any
ranking at 65,536 simulations, horizon 64, a 16-outcome reservoir, the same D1
rollout, and one public fair leaf at a surviving cutoff. Its fixed arena is
47,710,856 bytes per decision, below the 128 MiB cap.

The audit opens no gameplay seed and reconstructs no origin game. It selects
the fixed zero-based middle root (ordinal five of twelve) from each game in
the already-preserved public long-outcome corpus: 24 fitting games and 12
disjoint old-heldout games. The stored roots and synthetic tape identities are
valid, but their numeric Q values predate the scoring correction. Therefore,
on only those 36 persisted public states, the audit deterministically replays
the same public 25-move/seven-scenario tapes and recomputes fair D3/D4 under
the corrected 17,000-point Hardcore bonus. Historical 7,000-point labels and
the original MCTS artifact are retained only as explicitly non-comparable
provenance.

The fitting-only 2x2 ablation used identical roots throughout:

| Predictor against corrected 25-move labels | Top-1 | Pairwise | Raw regret | Normalized regret |
| --- | ---: | ---: | ---: | ---: |
| Old 16,384 / h32 / r8 / zero tail | 0.00% | 48.16% | 115,411 | 0.47946 |
| Scale only: 65,536 / h64 / r16 | 12.50% | **54.92%** | 79,300 | 0.37345 |
| Fair leaf only at old scale | 4.17% | 48.77% | 117,517 | 0.46225 |
| Full scaled candidate + fair leaf | **16.67%** | 52.46% | **78,944** | **0.35470** |
| Exact public fair D3 | 12.50% | 56.35% | 74,945 | 0.33939 |
| Exact public fair D4 | 8.33% | 53.07% | 79,648 | 0.35738 |

Scale supplied nearly all of the apparent fitting gain. The fair leaf improved
the full candidate's fitting top-1 and regret relative to scale alone, but
reduced pairwise accuracy; by itself it was essentially neutral. Whole-game
folds showed the full candidate improving normalized regret in four folds,
tying one, and regressing one, so the disjoint result was required.

On the 12 corrected-score old-heldout roots, the candidate failed clearly:

| Predictor against corrected 25-move labels | Top-1 | Pairwise | Raw regret | Normalized regret |
| --- | ---: | ---: | ---: | ---: |
| Old observable MCTS | **25.00%** | **61.02%** | **97,414** | **0.29450** |
| Scaled candidate | **25.00%** | 56.78% | 120,769 | 0.37251 |
| Exact public fair D3 | **25.00%** | 57.63% | 108,055 | 0.35712 |
| Exact public fair D4 | 33.33% | 55.93% | 120,612 | 0.36314 |

Both six-game heldout halves lost pairwise accuracy; the second half also
more than doubled normalized regret from 0.13592 to 0.29193. The frozen gate
required top-1 non-regression, strict pairwise and regret gains overall, and
no half regression. It failed, so no fresh gameplay, `0x3e...`, protected
`0x7d...`/`0xd7...`, or unused root seed was opened.

The diagnostic contrast is useful: against corrected fair-D4 root Q on the
same heldout states, scaling looked excellent—top-1 rose from 66.67% to
91.67%, pairwise from 66.10% to 74.58%, and normalized regret fell from
0.03896 to 0.00356. More simulations, depth, chance samples, and a terminal
leaf therefore made MCTS a much better short-horizon D4 imitator while making
it a worse ranker of the 25-move continuation. The remaining failure is not
search quantity; the public D1 rollout and bounded replay reservoir optimize
the wrong continuation distribution. This candidate should not receive a
gameplay screen. A future architecture would need a separately frozen
long-value/rollout replacement or unbiased continuing chance sampling, not
another budget increase.

No search exhausted its arena. Candidate maxima were 60,986 nodes, 65,536
stored outcomes, and 44,761,712 active bytes; the fixed reservation was
47,710,856 bytes. Four concurrent offline workers produced a 179,486,720-byte
process peak. The audit took 550.296 seconds. Standalone and library-mode
strict-Werror builds pass; the ASan/UBSan self-test passes exact old-config
parity against the embedded historical implementation, determinism,
reflection, public-metadata blindness, legality, fair-tail activation,
reservoir bounds, and arena bounds.

~~~sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/tree-search/observable-mcts/observable-mcts-scaled-audit.cpp \
  -o /tmp/drop7_observable_mcts_scaled_audit
/tmp/drop7_observable_mcts_scaled_audit --self-test
/tmp/drop7_observable_mcts_scaled_audit --audit --threads 4 \
  --output /tmp/drop7-observable-mcts-scaled-audit.json

clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  -DDROP7_OBSERVABLE_MCTS_SCALED_AUDIT_LIBRARY -c \
  approaches/tree-search/observable-mcts/observable-mcts-scaled-audit.cpp \
  -o /tmp/drop7_observable_mcts_scaled_audit_library.o

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/tree-search/observable-mcts/observable-mcts-scaled-audit.cpp \
  -o /tmp/drop7_observable_mcts_scaled_audit_san
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_observable_mcts_scaled_audit_san --self-test
~~~

The canonical artifact is
`/tmp/drop7-observable-mcts-scaled-audit.json`, SHA-256
`60d08880ccd0ec42cc6d79b4b996837b37444bff0f287ddd77891fda381c417c`.
The corrected 17k source SHA-256 is
`53ec45745ec58701ad58770aa32c56463a0fae9c8c52ec0d879fbdc5d1d5f606`.

## Corrected-17k Rainbow-lite n-tuple Q learner (stopped at fair-D1)

`approaches/ntuple-rl/rainbow-q/rainbow-ntuple-q.cpp` is an isolated, pure-C++ off-policy action-value
experiment for the corrected five-drop numbered-only Hardcore/Blitz engine.
Its observation is strictly the public board, visible next disc, and
five-move rise phase. Score, level, moves played, game seed, and covered-disc
values are absent from the model type. Illegal columns are masked, and every
deployed Q value is the exact mean of the direct evaluation for action `a`
and the mirrored-board evaluation for action `6-a`.

The high-throughput model hashes 28 horizontal length-four tuples, 28 vertical
length-four tuples, and 36 2x2 tuples in both action-only and
action-plus-visible-disc contexts. Public cell, height, row, count, next-disc,
and phase factors bring each orientation/action evaluation to 259 active
features. The 8,388,608 float parameters occupy 32 MiB, well below the frozen
128 MiB deployed limit. Training uses Double-DQN selection/evaluation, five-
step returns, `gamma = 0.997`, unclipped score deltas divided by 17,000,
proportional prioritized replay, annealed importance weights, Huber loss,
normalized sparse updates, and hard target copies.

The seed and promotion protocol was frozen before training. Stage A trained
to 250,000 transitions from `0x3d400000...` and then opened only the 32-game
random probe `0x4d400000...0x4d40001f`. It required 1.10x mean score, 1.05x
mean moves, and positive/nonnegative paired lower-95 bounds. Only a pass could
open Stage B, which extended to one million transitions and compared with
exact corrected fair-D1 on `0x4d400020...0x4d40003f`. Only another pass could
open four-million-transition Stage C and the disjoint
`0x4d400040...0x4d40007f` probe. Every observed visible disc was asserted
equal to `headlessDisc(gameSeed, moveIndex)`, paired stream hashes had to
match, and random-policy randomness used a separate policy domain rather than
the game seed.

Stage A passed materially:

| 32-game probe | Mean score | Mean moves | Natural | Censored |
| --- | ---: | ---: | ---: | ---: |
| Random | 73,670.06 | 26.41 | 32 | 0 |
| Learned Q at 250k | **101,324.97** | **33.91** | 32 | 0 |

The paired learned-minus-random differences were +27,654.91 points and +7.50
moves; their lower-95 bounds were +18,726.26 and +5.01. The corresponding
250,025-transition checkpoint was therefore preserved before scaling.

Stage B failed decisively and stopped the run:

| 32-game probe | Mean score | Mean moves | Natural | Censored |
| --- | ---: | ---: | ---: | ---: |
| Exact corrected fair-D1 | **168,072.38** | **52.94** | 32 | 0 |
| Learned Q at 1m | 111,092.25 | 36.84 | 32 | 0 |

The paired differences were -56,980.13 points and -16.09 moves, with
lower-95 bounds of -84,700.99 and -23.80. Every performance gate failed.
Stage C training and its entire development seed range remained unopened, and
the failed one-million-transition model was not checkpointed. Across the
deterministic restart's 34,783 training games, all ended naturally before the
300-move cap; there were zero censored training games. No `0x7d` or `0xd7`
gameplay seed was opened.

An initial pre-development attempt stopped after its last report at 47,146
training transitions because float accumulation in the replay sum tree could
select an unwritten zero-priority tail slot. It opened no development seed.
The tree was changed to double precision, a 60,000-entry partially filled
stress test was added, strict and sanitizer tests were rerun, and training was
restarted deterministically from the same approved training start. This
implementation failure and restart are recorded in the artifact.

~~~sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/ntuple-rl/rainbow-q/rainbow-ntuple-q.cpp \
  -o /tmp/drop7_rainbow_ntuple_q
/tmp/drop7_rainbow_ntuple_q --self-test \
  --checkpoint /tmp/drop7-rainbow-ntuple-q-selftest.bin
/tmp/drop7_rainbow_ntuple_q --run

clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  -DDROP7_RAINBOW_NTUPLE_Q_LIBRARY -c \
  approaches/ntuple-rl/rainbow-q/rainbow-ntuple-q.cpp \
  -o /tmp/drop7_rainbow_ntuple_q_library.o

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/ntuple-rl/rainbow-q/rainbow-ntuple-q.cpp \
  -o /tmp/drop7_rainbow_ntuple_q_san
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_rainbow_ntuple_q_san --self-test \
  --checkpoint /tmp/drop7-rainbow-ntuple-q-selftest-san.bin
~~~

Standalone and library-mode strict-Werror builds and the ASan/UBSan self-test
pass corrected 17k scoring, inherited engine parity, exhaustive feature
bounds, public-metadata blindness, exact reflection, action masking, terminal
and truncated n-step returns, partial prioritized replay, Double-DQN target
selection, checkpoint round trips, resource bounds, paired streams, and
positive/negative gate wiring.

The completed staged run processed 1,000,021 environment transitions in
108.234 training seconds (9,239 transitions/second) and 108.805 seconds total.
The headless inference benchmark measured 99,061 public states/second and
693,430 legal action values/second. Peak RSS was 119,488,512 bytes, below the
256 MiB runtime cap. The 5,941-byte audit artifact is
`/tmp/drop7-rainbow-ntuple-q.json`, SHA-256
`f510ee0bb96029d2e3580fcaeb6262dd29e25a4367d995c39978f596de6f1d2d`.
The 33,554,472-byte last-passing Stage A checkpoint SHA-256 is
`68083e2f2f4f921fb9f8815ba832d5f7e9bec0507edd23c228a9824fb614de9d`.
The source SHA-256 is
`4bd88e0cd65ed318c98fc712fb6c110d369a3f9831c7c5690581837ed8e26ead`.

## Phase-conditioned public heuristic evolution (rejected before probe)

`approaches/heuristic-search/evolved-public-policy/evo-public-policy.cpp` is an independent corrected-17k test of direct
complete-game derivative-free optimization.  It does not fit a proxy label.
Every candidate is played to a natural terminal (or an explicit cap) on the
same common-random-number game seeds, and a diagonal cross-entropy update ranks
`0.60 * mean + 0.40 * lower-quartile` of `moves + score / 17000`.  Generation
batches rotate through `0x3d500000...`; the generation-disjoint fitting
tournament starts at `0x3d510000`.  That tournament remained fitting evidence
after its first use in the phase-independent pilot; it is not described as a
heldout result.  The executable mechanically rejects every game range outside
its assigned `0x3d500000...0x3d51ffff` fitting and
`0x4d500000...0x4d50007f` probe ranges.

The policy sees only the board, visible next disc, and five-drop rise phase.
For every legal root column it averages seven public, exactly stratified
immediate successors.  Thirty-three normalized features cover the existing
direct/latent potential, cover exposure and altitude, low-number clogs,
quiet-build and rise readiness, height risk, and observed transition clears,
reveals, waves, and score.  Each rise phase has an independent weight block,
for 165 doubles (1,320 parameter bytes).  The versioned checkpoint is 1,352
bytes.  Canonical orientation, public-state-derived chance salts, and fixed
center-first tie order make mirrored nonsymmetric positions select mirrored
actions.  Score, level, moves played, game seed, history, future discs, and
future reveals never enter the policy.

An initial 33-weight phase-independent pilot improved its 96-game fitting
tournament from 137,725 points / 44.55 moves to 151,923 / 48.49, but this was
too small to justify a probe.  The larger fixed run used 80 generations, 65
antithetic-plus-mean candidates, 32 CRN games per generation, 12 elites, and a
128-game generation-disjoint fitting tournament.  It evaluated 166,400 candidate-games
before tournament reranking.  All games ended naturally before the 500-move
cap.

| 128-game fitting tournament | Mean score | Mean moves | Lower-quartile score | Lower-quartile moves | Clears / move | Reveals / move |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Hand-seeded phase policy | 136,169.15 | 44.156 | 95,855.97 | 32.938 | 1.5400 | 0.7619 |
| Evolved phase policy | **157,528.50** | **50.234** | **96,130.41** | **32.938** | **1.6516** | **0.8566** |

The candidate improved fitting mean score by 15.7% and mean survival by 13.8%,
but did not improve lower-quartile survival at all.  It also remained far below
the roughly 285-move trajectory implied by a million points.  A direct-policy
decision used 47.54 sampled transitions per played move and the complete
128-game replay took 0.472 seconds with a 1,720,320-byte peak RSS.

A bounded selective-expectimax follow-up kept all root actions legal and
ranked internal actions with the evolved public evaluator.  No model parameter
was changed.  The following configuration selection deliberately reused the
same eight fitting-only games `0x3d510900...07`; these results are therefore a
mechanism ablation, not independent evidence.

| Search (depth / internal width / strata) | Mean score | Mean moves | Wall time, 8 games |
| --- | ---: | ---: | ---: |
| Direct / - / 3 | 162,932.25 | 51.875 | 0.014 s |
| 2 / 2 / 3 | 171,204.50 | 53.625 | 0.364 s |
| 3 / 2 / 3 | 235,950.00 | 71.250 | 3.345 s |
| 4 / 2 / 3 | 170,558.38 | 53.750 | 13.628 s |
| 3 / 3 / 3 | 175,596.25 | 54.875 | 4.498 s |
| 4 / 3 / 3 | 247,404.63 | 75.000 | 49.621 s |
| 3 / 2 / 5 | 234,309.63 | 71.250 | 15.902 s |
| 4 / 2 / 5 | **272,605.13** | **81.750** | **162.543 s** |
| 5 / 2 / 3 | 253,689.50 | 76.250 | 134.173 s |

Depth 3 / width 2 / three strata consumed 3,694.64 sampled transitions per
move.  Deeper variants were both unstable and one to two orders of magnitude
slower; even the best selected eight-game mean remained below the corrected
fair-D4 baseline and below one third of the qualification trajectory.  The
phase-conditioned linear successor family and this selective-search wrapper
are therefore rejected.  The entire `0x4d500000...7f` probe remains sealed, as
do all `0x7d...` validation and `0xd7...` final seeds.

~~~sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/heuristic-search/evolved-public-policy/evo-public-policy.cpp \
  -o /tmp/drop7-evo-public-phase-policy
/tmp/drop7-evo-public-phase-policy --self-test

/tmp/drop7-evo-public-phase-policy --train \
  --generations 80 --population 65 --batch-games 32 \
  --tournament-games 128 --elite 12 --max-moves 500 \
  --samples 7 --threads 8 \
  --checkpoint /tmp/drop7-evo-public-phase-v2.bin

/tmp/drop7-evo-public-phase-policy --evaluate \
  --checkpoint /tmp/drop7-evo-public-phase-v2.bin \
  --games 128 --seed-start 0x3d510000 --max-moves 500 --summary-only

/tmp/drop7-evo-public-phase-policy --evaluate \
  --checkpoint /tmp/drop7-evo-public-phase-v2.bin --samples 3 \
  --games 8 --seed-start 0x3d510900 --max-moves 500 --summary-only \
  --search-depth 3 --search-width 2

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/heuristic-search/evolved-public-policy/evo-public-policy.cpp \
  -o /tmp/drop7-evo-public-phase-policy-san
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7-evo-public-phase-policy-san --self-test
~~~

Optimized strict-Werror and ASan/UBSan self-tests pass deterministic and legal
direct decisions, selective-search determinism, reflection, public-metadata
blindness, exact 17,000-point scoring, headless-disc reproducibility, and
bit-exact checkpoint round trips.  Training took 105.784 wall seconds with a
3,719,168-byte peak RSS.  The rejected checkpoint SHA-256 is
`d3de888162a02b658e8ebcbb10091022a7539a4d6917ac9218b45c53be1ae10a`;
the final source SHA-256 is
`939298e1616f2ebbf76297439006db19f8e2c7d95cd2ab26c53cd30969bce1c3`.

## Corrected-17k public rollout policy iteration (rejected at fitting gate)

`approaches/terminal-policy-iteration/public-rollout-policy-iteration/public-rollout-policy-iteration.cpp` is a frozen one-step public
policy-improvement test.  At each real decision it canonicalizes only the
board, visible next disc, five-drop phase, and terminal flag, then evaluates
every legal action on 15 common synthetic continuations of exactly 50 moves.
Each reveal and future-visible-disc event occupies a separate domain and an
independent `(step,event)` slice; for every event the 15 scenarios cover all
15 strata exactly once.  The tape seed is a hash of the canonical observable
state.  It contains no origin game seed, real future, score, level, move index,
or history.

Only the first rollout action is fixed.  Every later action is selected by a
fresh, completed, full-width fair-D1 search with five chance strata.  This
continuation function accepts only the observable-state type and cannot
receive a scenario or tape.  A rollout accumulates the engine's real score
deltas, adds -1,000,000 if it terminates before the 50-move leaf, and otherwise
adds the unchanged public fair leaf.  The action with the greatest arithmetic
mean over the 15 tapes is selected, with the fixed center-first order for exact
ties.  Paired scenario intervals against the fair-D1 action and runner-up are
exportable audit measurements only; they never affect selection.

The preregistered first gate paired four candidate games with exact fair-D1 on
`0x3d600000...0x3d600003`, capped at 500 moves.  It required at least 1.20x
both mean score and mean moves, candidate covers revealed per move at least as
large as fair-D1, and at least three of four paired games to win jointly on
score and moves.  The gate failed:

| Four-game fitting screen | Mean score | Mean moves | Reveals/move | Natural | Censored |
| --- | ---: | ---: | ---: | ---: | ---: |
| Exact public fair-D1 / five strata | 151,909.25 | 48.75 | **0.86154** | 4 | 0 |
| 15-tape h50 rollout improvement | **162,491.50** | **50.00** | 0.69500 | 4 | 0 |

The candidate improved every paired score, for a mean gain of 10,582.25, but
reached only 1.0697x fair-D1 score and 1.0256x moves.  It had two joint
score-and-move wins rather than three and reduced reveal throughput.  The
fitting-only paired one-sided 95% lower bounds were -2,137.01 points and -4.38
moves.  All four performance gates therefore failed.  The program stopped
without opening any `0x4d600000...0x4d600007` development game and did not
create the conditional distillation JSONL.  It also rejects the neighboring
`0x3d3...`, `0x3d4...`, and `0x3d5...` game ranges and all `0x7d...` and
`0xd7...` gameplay seeds.

The candidate processed 540,185 synthetic transitions and 520,435 fresh
fair-D1 calls (34,274,756 fair work units) over 200 real roots.  Every public
fair root completed full-width with no cache allocation.  Mean candidate
decision time was 166.97 ms; the four-thread screen took 14.137 seconds and
peaked at 3,375,104 bytes RSS, below the fixed 30-minute and 256 MiB caps.  The
per-root static maxima are 5,250 synthetic transitions, 5,146 D1 calls, and
360,220 fair work units.

~~~sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/terminal-policy-iteration/public-rollout-policy-iteration/public-rollout-policy-iteration.cpp \
  -o /tmp/drop7_public_rollout_policy_iteration
/tmp/drop7_public_rollout_policy_iteration --self-test
/tmp/drop7_public_rollout_policy_iteration --run --threads 4 \
  --output /tmp/drop7-public-rollout-policy-iteration.json \
  --teacher-output /tmp/drop7-public-rollout-policy-iteration.jsonl

clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  -DDROP7_PUBLIC_ROLLOUT_POLICY_ITERATION_LIBRARY -c \
  approaches/terminal-policy-iteration/public-rollout-policy-iteration/public-rollout-policy-iteration.cpp \
  -o /tmp/drop7_public_rollout_policy_iteration_library.o

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/terminal-policy-iteration/public-rollout-policy-iteration/public-rollout-policy-iteration.cpp \
  -o /tmp/drop7_public_rollout_policy_iteration_san
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/drop7_public_rollout_policy_iteration_san --self-test
~~~

The standalone and library-mode strict-Werror builds pass.  The optimized and
ASan/UBSan self-tests cover corrected scoring, exact continuation completion,
public-metadata blindness, determinism, reflection, legality, exact event
stratification, tape-domain isolation, terminal/tail semantics, mean-only tie
breaking, resource bounds, and seed guards.  The canonical 7,519-byte artifact
is `/tmp/drop7-public-rollout-policy-iteration.json`, SHA-256
`2538ccd1b0792990645b4904d378fd7b02f243fb0d52b75f1fa39d6252216cac`.
The source SHA-256 is
`93f821bc38a60f7461e8ce38ef7810bb730f99f3b531c18428cc466fe33bbe95`.

## Public-state PyTorch behavior-cloning and PPO lab (rejected before PPO)

`approaches/ntuple-rl/torch-ppo/torch-env.cpp` and `../approaches/ntuple-rl/torch-ppo/train.py` form a high-throughput neural
policy lab.  The C++ vector environment owns the exact Hardcore engine and
exposes only the 49-cell board, visible next disc, five-drop phase, legal mask,
and active mask.  The Python policy cannot inspect a game seed, future tape,
score, level, move index, or history.  Completed environments are not silently
autoreset: `step()` returns an unambiguous terminal/truncation transition and
`reset_done()` advances only those slots whose final episode record has been
collected.

The environment also provides parallel, exact full-width fair-D1 and fair-D2
teachers with five reveal strata.  The D2 fixture uses 2,485 work units versus
70 for D1.  During corpus generation it sustained roughly 3.3--5.0 million D2
work units per second.  Its original episode reporting mistakenly returned
only the last transition's clear/reveal counts; those counters now accumulate
the entire episode and are checked against the native game object over 60 exact
transitions.

The actor-critic is deliberately small: 263,720 parameters and 1,054,880 bytes
of Float32 weights.  It one-hot encodes ten board values, seven visible-disc
values, and five phase values, then uses 32 channels, three residual blocks,
and a 128-unit hidden layer.  The public policy averages a state and its
horizontally reflected inference before applying the legal mask, so action
reflection is exact by construction.  A 49-state adapter test covers seven
legal root actions across seven synthetic h25 scenarios.  Export produces a
Torch checkpoint, TorchScript, a JSON architecture manifest, raw
little-endian Float32 tensors, and a golden inference batch.  Python versus raw
inference is bit exact; Python versus TorchScript is checked to `1e-6`.

The frozen first pilot generated whole-game-disjoint D2 corpora:

| Corpus | Games | States | Mean exact-D2 score | Mean moves | Censored |
| --- | ---: | ---: | ---: | ---: | ---: |
| Clone training, `0x3d310000...0x3d3102ff` | 768 | 56,484 | 244,207.953 | 73.547 | 0 |
| Held-out validation, `0x3d320000...0x3d3200ff` | 256 | 17,951 | 231,063.223 | 70.121 | 0 |
| Student-policy DAgger, `0x3d330000...0x3d3301ff` | 512 | 21,654 | 130,565.021 | 42.293 | 0 |

The initial clone used 70% hard teacher-action cross-entropy and 30% per-root,
full-range-normalized soft Q matching for 12 epochs, followed by six DAgger
epochs.  Its held-out agreement was 0.471840, top-two accuracy 0.697788, and
cross-entropy 1.367013, below the preregistered 0.55 agreement gate.  On the
fixed 32-game development cohort it averaged 141,986.938 points and 45.125
moves, versus 79,307.875/27.969 for random, 181,846.438/56.281 for fair-D1,
and 191,189.344/58.688 for fair-D2; no game was censored.  The clone passed its
random and D1 gameplay-ratio floors but failed the agreement gate, so PPO did
not run.

One concrete defect was diagnosed before the single allowed correction.  A
catastrophic approximately -1,000,000 terminal alternative can dominate a
root's complete Q range, flattening the soft probabilities among all viable
actions.  Across the frozen training corpus the chosen soft-target probability
averaged 0.621020, target entropy 0.992563, median top-two margin divided by the
full range 0.202572, and terminal-outlier frequency 0.053980.  The correction
resumed the rejected checkpoint, replayed only the identical 56,484 training
and 17,951 validation states, and performed exactly 16 epochs of pure hard
teacher-action cross-entropy at learning rate `3e-4`, batch size 512.  It did
not replay DAgger, open a new game seed, sweep a parameter, or change a gate.

The correction was also rejected.  Training cross-entropy fell from 1.272471
to 1.021483, while final held-out agreement was only 0.471784, top-two accuracy
0.694836, and cross-entropy worsened to 1.426484: the small policy overfit and
the soft-target defect was not the sole limitation.  Its development result
was 142,364.031 points and 45.313 moves with zero censored games.  Because the
unchanged 0.55 agreement gate failed, the frozen PPO pilot, continuation, and
confirmation cohorts remain unopened, no second correction is permitted, and
this experiment makes no million-point or selected-policy claim.

There was one interrupted process before the pilot was recorded.  It completed
the registered `0x3d31...` clone-training cohort and advanced every one of the
256 registered validation games through its first 25 decisions.  It observed
no validation result or gate and was stopped when seed ownership was narrowed;
the complete registered cohorts were then replayed deterministically.  It did
not open a conflicting seed.  The compiled environment permits only
`0x3d300000...0x3d3fffff`, explicitly rejects starts at `0x3d400000`,
`0x3d500000`, and `0x3d600000`, and does not expose any seed-named Python
attribute.  Neither run opened any `0x3e...`, `0x7d...`, or `0xd7...` gameplay
seed.  Peak RSS was 468,451,328 bytes for the first pilot and 482,066,432 bytes
for the correction, both below the fixed 512 MiB ceiling.

Build and test from the repository root with the Homebrew PyTorch interpreter:

~~~sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  -DDROP7_TORCH_ENV_CORE_ONLY -DDROP7_TORCH_ENV_STANDALONE \
  approaches/ntuple-rl/torch-ppo/torch-env.cpp -o /tmp/drop7_torch_env
/tmp/drop7_torch_env

clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  -DDROP7_TORCH_ENV_CORE_ONLY -c \
  approaches/ntuple-rl/torch-ppo/torch-env.cpp -o /tmp/drop7_torch_env_library.o

clang++ -O1 -g -std=c++20 -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Werror \
  -DDROP7_TORCH_ENV_CORE_ONLY -DDROP7_TORCH_ENV_STANDALONE \
  approaches/ntuple-rl/torch-ppo/torch-env.cpp -o /tmp/drop7_torch_env_san
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 /tmp/drop7_torch_env_san

/opt/homebrew/opt/pytorch/libexec/bin/python3 \
  approaches/ntuple-rl/torch-ppo/train.py --self-test
~~~

`--pilot` reproduces the initial frozen path and `--corrected` requires its
rejected checkpoint and artifact before reproducing the sole correction.  The
optimized, library, sanitizer, and Python self-tests pass exact-engine parity,
cumulative accounting, legal action and reset semantics, deterministic
parallel teaching, public-metadata blindness, reflection, gradients, raw and
TorchScript export, the h25 adapter, corrected 17,000-point scoring, and seed
guards.  The correction-run source hashes are
`84d6134be727f5bf4ec29508620abbb2fc8b37f21d1670b92e0973e8cc7fd065`
for the Python trainer and
`e095fec1777b21e9b4e30adda12603c24e508a440563e2bb2d67a10d7a64f5da`
for the C++ environment.

The canonical initial artifact is the 11,098-byte
`/tmp/drop7-torch-rl-pilot.json`, SHA-256
`b571337d23811706755e99e674429a1903bc18c695410a8f78c20d3e00a679b0`;
its rejected checkpoint SHA-256 is
`20c2dd906b6e7d58bac2da64b1267a8032222d937cdd02fe98aaeaedd339ecf8`.
The canonical correction artifact is the 10,823-byte
`/tmp/drop7-torch-rl-corrected.json`, SHA-256
`b10eaffca3af590fa2ff6e58cd23642868636c3cad0628b04285f02a75368358`;
its rejected checkpoint SHA-256 is
`fbf392ada61e01c1615161cede71ce136a2495e24497f2d77efedd6e977ae93a`.
The correction's raw 1,054,880-byte export has SHA-256
`77672539704d0d760f5e2ea29ad15040ef6c1ea605b7f593eb90d349b82838a0`
and its 1,075,121-byte TorchScript export has SHA-256
`7d8ed4bb7767014782fd67f83fa64cbe27005d3d285bd7d3a7a7a92b9490a324`.

### Separately authorized direct-PPO run (resource-limit abort)

After the clone experiments stopped, a distinct on-policy hypothesis was
preregistered.  It started from the original clone (not the overfit
correction), removed the imitation anchor, and fixed 32 iterations of 512
complete games.  The reward, AdamW optimizer, four PPO epochs, logical
minibatch 1,024, learning rate `1e-4`, clip 0.15, gamma 0.997, GAE lambda
0.95, entropy coefficient 0.005, value coefficient 0.25, and gradient clip
0.5 were frozen before any new seed opened.  The only permitted training
ranges were `0x3d340000...0x3d340fff` and
`0x3d350000...0x3d352fff`.  A single 64-game paired development cohort at
`0x3d360000...0x3d36003f` was reserved but could be read only after all 32
iterations and a frozen checkpoint.

The added self-test checks a hand-computed, interleaved-lane GAE fixture to
`1e-6`, exact clipped policy and per-sample clipped-value losses, finite actor
and critic gradients, deterministic rollout sampling, and bit-identical twin
optimizer updates.  Its only game smoke replays the already-opened
`0x3d300040...0x3d300043` range: four games, 95 transitions, zero censored.
Synthetic 64-game fixtures prove both acceptance and rejection branches of
the frozen paired development gate.

The production run respected its hard stop.  It peaked at 551,567,360 bytes,
above the fixed 536,870,912-byte (512 MiB) ceiling, and stopped 2.755 seconds
after direct training began.  The first 512-game collection on
`0x3d340000...0x3d3401ff` had completed, and exactly one optimizer update had
run, but no full PPO iteration completed.  This is recoverable from control
flow rather than guesswork: collection returns only after all episodes finish,
the resource check occurs immediately before the first minibatch and after
every optimizer step, and the partial model differs from its origin in all 20
parameter tensors (261,535 elements, maximum absolute change
`9.997934e-5`).  The remainder of `0x3d34...`, all of `0x3d35...`, and the
entire reserved `0x3d36...` development cohort remain unopened.  No candidate
was frozen, no score gate was read, and the partial checkpoint is explicitly
non-deployable.  There was no retry or post-failure tuning.

The canonical post-run-audited artifact is the 5,256-byte
`/tmp/drop7-torch-direct-ppo.json`, SHA-256
`488343d7eedad038097682e64780218dde4ec2dfd607717b75aa643bfe1cbcc1`.
The 1,063,106-byte partial checkpoint has SHA-256
`ab9d5e41a86a1562f1aad048c991c437736a53a5e80c78241c7641ea74d8929b`
and must not be deployed or used to initialize another experiment.  The
trainer source used for the run has SHA-256
`e81091f4697448f636570fdefb2cf849e053aa454ec5ea678aa9ed35dfae5da6`;
the C++ environment remains
`e095fec1777b21e9b4e30adda12603c24e508a440563e2bb2d67a10d7a64f5da`.

A no-gameplay audit isolated the likely memory defect.  The direct process ran
the allocator-heavy full neural self-test first and had already peaked at
340,213,760 bytes.  A fresh process that only loaded the same original model
and constructed the same optimizer peaked at 286,539,776 bytes.  Removing that
53,673,984-byte retained allocation projects the unchanged 1,024-sample update
at 497,893,376 bytes, 38,977,536 bytes below the cap.

That process-isolation hypothesis was separately authorized with no PPO change.
The full self-test ran first and exited; it wrote a source-bound pass marker.
A new process then loaded only the bit-exact original clone, constructed an
empty fresh AdamW optimizer, replayed four already-opened self-test games, and
tiled their 130 public transitions to exactly 1,024.  It used a physical and
logical minibatch of 1,024, no gradient accumulation, and all four unchanged
PPO epochs.  Rejected direct/overfit artifacts were hashed for the audit but
never loaded.  A hard preflight gate required peak RSS at most 480 MiB before
any fresh game could begin.

The empirical preflight invalidated the projection: it peaked at 528,449,536
bytes (503.97 MiB).  Although this remained below the absolute 512 MiB process
ceiling, it exceeded the preregistered 503,316,480-byte admission threshold and
stopped with status `failed-memory-threshold`.  Consequently the production
entrypoint did not run.  Every proposed training seed
`0x3d390000...0x3d393fff` and every proposed development seed
`0x3d3a0000...0x3d3a003f` remains unopened.  There was no microbatch change,
retry, parameter adjustment, production checkpoint, or development read.

The isolated-test marker is the 3,232-byte
`/tmp/drop7-torch-selftest-passed.json`, SHA-256
`a978ac893df5e60b7154af16a5bec96fad12d4b1406afd29ed38f674b2843b84`.
The canonical 3,313-byte preflight artifact is
`/tmp/drop7-torch-fresh-process-preflight.json`, SHA-256
`871ec3903a30f9c4683bf5ec738305879f39d12eeab00ee17dfc8f6e46224966`.
The final trainer source SHA-256 is
`cd13ba54ba31865b98b050e700a43390e6b010fc9c5703d1ae22cdeeb42275ec`.
The two isolated commands are intentionally separate processes:

~~~sh
/opt/homebrew/opt/pytorch/libexec/bin/python3 \
  approaches/ntuple-rl/torch-ppo/train.py --self-test
/opt/homebrew/opt/pytorch/libexec/bin/python3 \
  approaches/ntuple-rl/torch-ppo/train.py --fresh-process-preflight
~~~

`--direct-ppo-fresh-process` requires both source-bound artifacts to pass, so
with this failed preflight it refuses before opening a fresh seed.

### Ordered physical-256 gradient accumulation

A final, separately authorized experiment changed only the memory schedule of
the rejected physical-1,024 proposal.  Each unchanged logical PPO minibatch of
1,024 is traversed in its original permutation order as at most four physical
chunks of 256.  Every chunk mean is weighted by its share of the logical
minibatch, gradients accumulate, global-norm clipping occurs once, and AdamW
steps exactly once after the complete logical minibatch.  The final short
logical minibatch receives the same sample-count weighting.  The reward,
rollout-wide advantage normalization, sample order, generator consumption,
four PPO epochs, optimizer, and every learning hyperparameter are unchanged.

Synthetic equivalence tests compare one physical-1,024 update with four
physical-256 chunks.  For a full 1,024-sample logical minibatch the maximum
loss, gradient, gradient-norm, parameter, and Adam-state errors are respectively
`8.9407e-8`, `4.6566e-9`, `3.7253e-9`, `3.7253e-9`, and `4.6566e-10`, with one
optimizer step on both paths.  A 777-sample final logical minibatch split as
256 + 256 + 256 + 9 has corresponding maxima `1.8748e-7`, `5.1223e-9`,
`1.8626e-9`, `3.7253e-9`, and `5.2387e-10`, also with one optimizer step.

The source-bound full self-test ran in a separate process and peaked at
332,873,728 bytes.  The production-style preflight used the real model and
optimizer for all four epochs over one logical 1,024-sample minibatch tiled
only from already-opened self-test games.  It peaked at 366,755,840 bytes and
passed the fixed 419,430,400-byte (400 MiB) admission gate.  No fresh training
or development seed was opened by either check.  The 4,050-byte self-test
marker `/tmp/drop7-torch-selftest-passed.json` has SHA-256
`2ca66297c9361a0682a0c5e954af341d78158b1556ffd55cbf7018999b5c96d3`;
the 4,268-byte preflight artifact
`/tmp/drop7-torch-gradaccum256-preflight.json` has SHA-256
`7d2f7249037a151f49675c0066c247d81e099b341449f7c60cad21bbbae5a103`.

The sealed production run then started bit-equal to the original D2 clone with
an empty optimizer.  It completed all 32 iterations and exactly 16,384 games
on `0x3d390000...0x3d393fff` in 1,597.281 seconds.  Peak RSS remained
389,955,584 bytes (371.89 MiB), below the fixed 512 MiB hard limit, and all
training games terminated naturally.  Mean batch performance rose from
112,175.525 points and 37.168 moves at iteration 1 to 136,608.055 points and
44.037 moves at iteration 32.  There was no intermediate evaluation or
checkpoint selection.

Only after freezing the checkpoint and exports did the process read the single
64-game paired development cohort `0x3d3a0000...0x3d3a003f`.  The greedy
candidate averaged 142,677.781 points and 45.656 moves, versus 130,797.406 and
42.500 for the original clone, 180,713.422 and 56.359 for fair D1,
241,825.203 and 72.594 for fair D2, and 77,674.406 and 27.484 for random.  No
policy was censored.  Candidate/clone score and move ratios were 1.090830 and
1.074265, below the registered 1.15 floors; candidate/D2 ratios were 0.590004
and 0.628928, below 1.0.  The paired candidate-minus-clone score difference
was +11,880.375 with standard error 7,201.038, but its one-sided 95% Student-t
lower bound was -141.054.  The candidate therefore failed every performance
gate except zero censoring and is rejected.  No continuation, tuning, or seed
beyond that development cohort was opened.

The canonical 36,446-byte result artifact is
`/tmp/drop7-torch-gradaccum256-ppo.json`, SHA-256
`1ae834d5844b3dbc96a6cde8410baf0501244ef173d77527a6b2b0fe103ebccc`.
The rejected 1,063,058-byte checkpoint has SHA-256
`8f6f43f5655a16a069a0c903cf000b3a1167a59c1f050aaaea30fa3e5a964ed9`.
Its 4,058-byte manifest, 1,054,880-byte raw float export, and 1,075,367-byte
TorchScript export have SHA-256 hashes
`b03dca8df2d7afcc7cc4a7058a1aa433cd44c67b12321bfc66e2d35c76f2c7db`,
`f030672583489fc896dd61f90379de397ace4cfb4d6f2da76f8d67e6af362304`,
and `70c979750497eb24830e78589dd1f8dd50721c42242c499a7cfeaa29087a452b`.
The 3,844-byte golden fixture has SHA-256
`f84cf6e0056ce24758d3d45d1c9bb79d469e8543f6b3e56a87c098e91e88200b`;
Python, raw, and TorchScript logits and values agree exactly on it.  The run's
trainer and environment source hashes are
`47eeed3c576dc2011656555f5c07f42f070b81540b511faef7a010e64f9d5abc`
and `e095fec1777b21e9b4e30adda12603c24e508a440563e2bb2d67a10d7a64f5da`.

Reproduce the isolated validation and the now-complete production entrypoint
as separate processes:

~~~sh
/opt/homebrew/opt/pytorch/libexec/bin/python3 \
  approaches/ntuple-rl/torch-ppo/train.py --self-test
/opt/homebrew/opt/pytorch/libexec/bin/python3 \
  approaches/ntuple-rl/torch-ppo/train.py --gradaccum256-preflight
/opt/homebrew/opt/pytorch/libexec/bin/python3 \
  approaches/ntuple-rl/torch-ppo/train.py --direct-ppo-gradaccum256
~~~

### Public oracle-manifold discriminator (coverage-gate stop)

A distinct experiment tested whether a fixed public-state classifier could
provide the missing long-horizon "fertile topology" signal.  Its positive
examples were the checksum-locked 4,096-state oracle curriculum
(`0x8657ac0dc83c6041`); its negatives were fair-D1 roll-in states from exactly
1,024 fresh games at `0x3d6b0000...0x3d6b03ff`.  The classifier can see only
board, visible next disc, rise phase, causal-graph, and trigger-certificate
features.  Canonicalization followed by invariant aggregates makes its scalar
exactly reflection invariant.  No seed, future draw, score, level, move count,
or history is an input.

To prevent classification by board load, pairs were matched exactly on rise
phase, occupied-cell count, and maximum column height.  Positive states were
split by canonical public hash; every negative state inherited the split of
its whole origin game.  Two complementary 295-24-1 MLPs (7,129 parameters)
were trained on one fold and evaluated only on the other.  Fold 0 held-out AUC
and paired ranking were 0.924806 and 0.930140; fold 1 values were 0.915419 and
0.912361.  These comfortably exceeded the frozen 0.62/0.58 label thresholds.

The preregistered admission gate also required at least 80% exact-match
coverage.  Only 3,032 of 4,096 positives matched without negative reuse
(74.0234%), so the gate failed and execution stopped.  PPO was never started,
no final discriminator was fit or serialized, and no deployable public-state
scalar exists from this run.  All proposed policy-training seeds
`0x3d6b1000...0x3d6b6fff`, all Stage-A seeds
`0x3d6c0000...0x3d6c001f`, and every protected `0x4d`, `0x7d`, and `0xd7`
cohort remain unopened.  There was no threshold adjustment, negative reuse,
rematching, or retry after observing the gate.

The stopped run took 12.574 seconds and peaked at 9,846,784 bytes RSS.  Its
862-byte artifact `/tmp/drop7-oracle-manifold-discriminator.json` has SHA-256
`47434c51ab6c00d2e89e141ece694caf3f7506f585bef74f1882ac71705210ce`.
The experiment source `approaches/ntuple-rl/manifold-ppo/oracle-manifold-ppo.cpp` has
SHA-256
`5afd52091de9931761449f34015f09f46abcd6fcd37331e96a033ccde30a5e2f`.
Strict optimized and AddressSanitizer/UndefinedBehaviorSanitizer self-tests
pass curriculum checksum, exact reflection, metadata blindness, reward/GAE
math, checkpoint inheritance, matching, seed guards, and both gate branches.

~~~sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/ntuple-rl/manifold-ppo/oracle-manifold-ppo.cpp \
  -o /tmp/drop7_oracle_manifold_ppo
/tmp/drop7_oracle_manifold_ppo --self-test
/tmp/drop7_oracle_manifold_ppo --discriminator \
  --output /tmp/drop7-oracle-manifold-discriminator.json
~~~

### Manifold scalar as a close-D3 root prior

A downstream experiment fit one final public scalar on all 3,032 exact
rise-phase/occupancy/maximum-height matched pairs from the stopped manifold
experiment.  This was a new hypothesis, not a retroactive pass of that
experiment's 80% coverage gate.  Negative construction only replayed the
already-opened `0x3d6b0000...0x3d6b03ff` games.  The deterministic 295-24-1
MLP has 7,129 parameters, is exactly reflection invariant, and exposes a
metadata-blind `PublicState -> logit` interface.  Its whole-fit AUC was
0.945853, matched-pair ranking 0.941953, and logistic loss 0.291224; its model
fingerprint is `0x6f8b8157b0b9553b`.

The scalar was allowed only as a root tie-break around an exact fair-D3
anchor.  At most two actions with D3 Q within 2,500 points of the anchor were
admitted.  Each received the mean scalar logit of seven public-state-derived,
stratified immediate successors; exact ties returned the D3 action.  There was
no scalar coefficient, leaf modification, private metadata, parameter sweep,
or D4 search.

Exactly 16 fitting games at `0x3d6f0000...0x3d6f000f` were opened.  The root
prior averaged 253,798.875 points and 73.938 moves versus fair D3's
301,101.062 and 88.938.  It cleared/revealed 1.826/0.986 discs per move versus
D3's 1.936/1.063 and won both score and moves in only 5 of 16 games.  The
candidate changed 31.53% of D3 moves; two candidates were eligible on 74.81%
of its moves.  It therefore failed every preregistered improvement gate.  The
sole 32-game screen `0x3d6f1000...0x3d6f101f` remained unopened, as did every
`0x4d`, `0x7d`, and `0xd7` cohort.  This result shows that strong matched-state
classification does not by itself rank close root actions correctly.

The run took 61.349 seconds, including dataset replay and fitting, and peaked
at 11,550,720 bytes RSS.  The 1,760-byte artifact
`/tmp/drop7-manifold-root-prior.json`, 28,540-byte rejected checkpoint
`/tmp/drop7-manifold-root-prior.bin`, and source
`approaches/ntuple-rl/manifold-ppo/manifold-root-prior.cpp` have SHA-256 hashes
`87bc1d6be6c0be6e51437880d050de5a1ad2a371067d357bc2f2795d7c6736b4`,
`cd3fd27e3f2749778ea653483c63df4f2eab0f5c57272205fad1048951a7c144`,
and `8d87b4d7a056c743edd029fd0967f5f90dd01b673acecfbd265de40bc110353d`.
Strict `-Werror` and ASan/UBSan self-tests pass checksum-stable dataset replay,
same-build deterministic fitting, exact policy/scalar reflection, metadata
blindness, checkpoint round-trip, root admission, both gates, and seed guards.

~~~sh
clang++ -O3 -DNDEBUG -std=c++20 -pthread \
  -Wall -Wextra -Wpedantic -Werror \
  approaches/ntuple-rl/manifold-ppo/manifold-root-prior.cpp \
  -o /tmp/drop7_manifold_root_prior
/tmp/drop7_manifold_root_prior --self-test
/tmp/drop7_manifold_root_prior --run
~~~

## Primal-dual actor-critic (rejected at final calibration)

`approaches/ntuple-rl/primal-dual-actor-critic/primal-dual-actor-critic.cpp` tested a five-move macro actor-critic
instead of another short-horizon leaf residual.  A reflection-exact sparse NNUE
learned a public policy residual over the fixed fair-D1 action, while primal-dual
constraints targeted absolute occupancy drift, covered-disc drift, and death in
the same undiscounted five-move cycle.  Per-game calibration prevented a few
long trajectories from dominating confidence estimates.  The final greedy
checkpoint alone—not an iteration ensemble—had to pass support, drift-upper-
confidence, terminal-risk, and entropy gates before any gameplay screen could
open.

The source and protocol were frozen before production.  Strict Clang/GCC
executable and library builds, ASan/UBSan, 32 numerical gradients (maximum
relative error `9.91e-7`), exact reflection, censoring/terminal alignment,
checkpoint/resume, transactional optimizer, injective restart streams, and
seed guards passed.  A burned-only preflight projected 1,565.85 seconds of
training (4,982.25 seconds at its conservative maximum) and 333,348,864 bytes
peak RSS, below the fixed 12-hour/512-MiB limits.

The one authorized run consumed exactly 131,072 training games in the sealed
`0x3dac0000...0x3dadffff` lane and completed 128 atomic iterations in 339.663
seconds.  Peak RSS was 94,846,976 bytes.  On the mandatory final 512-game
initial-board calibration, the greedy policy averaged only 175,834 points and
55.006 moves.  Its four five-move drift upper-95 bounds were
3.1767/1.8588/3.8765/1.6098, all above the required zero, and its terminal-risk
upper bound was 0.10496 against the fixed 0.02 limit.  The checkpoint was
therefore sealed untrusted and the executable refused to open Stage A.  Every
`0x3dae...`/`0x3daf...` gameplay gate and every protected/final seed remains
unopened by this experiment.

The 768,297-byte rejected checkpoint
`/tmp/drop7-primal-dual-production.bin` has SHA-256
`427e2741d7215065e95aee064ee7d9f5f425c31def263c25e3e88c0109b7068f`.
The 1,213-byte result `/tmp/drop7-primal-dual-training.json`, preregistration,
and preflight have SHA-256 values
`a618ea425382715df5cd28004a78bc0cadccc3ce3f9dec8e82dce6b9b2c74d26`,
`afc4eb2e3e70f1a66f8071d16d9a3a8fc943f71f9718886b76123d3324561dd7`,
and `4e4e329111be86fe80203dbb19326d611d8350c75c751751f4f4c4811e779176`.
The frozen source SHA-256 is
`48262c101ac2f103f7616ed89ee09427a3a7f15088ef457f431eb3a719df195e`.

~~~sh
/tmp/drop7_primal_dual_actor_critic --train \
  EXECUTE_FROZEN_PRIMAL_DUAL_3DAC_PROTOCOL \
  48262c101ac2f103f7616ed89ee09427a3a7f15088ef457f431eb3a719df195e \
  /tmp/drop7-primal-dual-production.bin \
  /tmp/drop7-primal-dual-training.json NEW
~~~

## Regenerative expert iteration (rejected before deployment)

`approaches/ntuple-rl/regenerative-expert-iteration/regenerative-expert-iteration.cpp` tested a D4-initialized recurrent
public-state evaluator with eight rounds of fresh roll-in, replay
reanalysis, and explicit calibration of lifetime, regeneration, and flow
heads.  The source was frozen at SHA-256
`3ac3c9d481e8638c62e0cc94fd25bdae86801f297be9a471de485a8550533028`.
The sole production run consumed 160,000 new roots and reanalysed 40,000
older roots from its sealed `0x3da41000...0x3da7ffff` lane.  No checkpoint
selection or intermediate gameplay evaluation was allowed.

The fixed corrected-D4 bootstrap averaged 308,295.578 points and 90.031
moves over its 64 games.  In contrast, learned-policy roll-ins averaged
110,294 / 36.386 in round 1, peaked at 138,229 / 44.134 in round 2, and ended
at only 116,598 / 38.046 in round 8.  The regeneration head calibrated on
both halves in every later round, but lifetime calibration was unstable and
failed again in the final round; the flow head never calibrated.  Final
policy-top-1 agreement was only 0.362/0.353 across the two halves, and
score-top-1 agreement was 0.093/0.095.

The experiment therefore exported zero policy checkpoints and exited 2 with
`deploymentQualified=false`.  Its central failure is informative: targets
were observed for the played action while deployment maximized predictions
over unplayed siblings, so even regenerating on-policy trajectories did not
remove the sibling extrapolation error.  No gameplay gate, protected cohort,
or final seed was opened.

The 7,126-byte result artifact
`/tmp/drop7-regenerative-prod-iteration.json`, 23,538,236-byte final replay,
and 25,430,000-byte resume ledger have SHA-256 values
`a9d2aa1188fa62d2eebcced4c4bee06440dac57a733c21fa80c6c68863b1dcdf`,
`2777a9320aaabafbbd2486fb02d0b803f258fd8b7c5edca7efa3f5b3d3931fff`,
and `d9913a8ce2d83667a1243901aa6d756c2ee21bdab7c0906ad6f2400e8fa2c940`.

## Full-panel conservative-policy preflight (preregistered)

`approaches/terminal-policy-iteration/deployment-panel/full-panel-cpi-preflight.cpp` is a seed-free B0 falsification test for
the proposed conservative policy-iteration architecture.  It is restricted to
the already-consumed 477-root, eight-origin deployment-panel corpus at
`/tmp/drop7-terminal-policy-deployment-panels.jsonl`, whose required SHA-256 is
`bfda8ae32fa0be3577c6b27f6413aba28f2854930e2f91a0dcf7674808f04196`.
The executable cannot start or replay an origin game: all stochastic work
begins at the stored public roots with their public-state-derived tapes.

Before the one allowed corpus run, the source was frozen at SHA-256
`5bee89dca06c4926f7cdf5b0a5d001acdc4b314f7ba88c1a1e3d4a6c9ef5ae16`.
Two independent audits plus strict Clang/GCC, library, and ASan/UBSan builds
verified exact public D1 continuation, complete D4 comparison, joint
state/action reflection, whole-origin cross-validation with exact canonical
public-state duplicate purging, and truly nested/disjoint scenario samples.
No corpus record was read during those audits.

The frozen K=63 conjunction requires at least 70% independent-half top-action
stability, 80% override precision, 25% override recall, pairwise accuracy at
least 0.02 above exact D4, normalized regret at most 0.90 times exact D4, at
least six of eight non-regressing whole-origin folds, and non-regression on
both ordered four-game halves for ranking and hybrid stored return.  Pairwise
accuracy uses every unordered legal sibling pair; a target or prediction tie
receives one-half credit.  Failure stops the architecture before any new
training or gameplay lane is opened.

After the regenerative experiment released its workers, the one authorized
corpus run completed all 477 roots in 324.699 seconds with 25,706,496 bytes
peak RSS.  It used 13,730,063 synthetic transitions and replayed zero origin
transitions, comfortably within every resource bound.

The K=63 actor failed every performance gate.  Independent-half top-action
stability was 0.5241; override precision and recall were 0.3478 and 0.0955.
Its pairwise accuracy was 0.5425 versus exact D4's 0.6585, while normalized
regret was worse at 0.4159 versus 0.2766.  Zero of eight whole-origin folds
non-regressed, and both ordered halves regressed in both ranking and hybrid
stored return.  The B0 artifact therefore records `status=falsified` and the
architecture stops before a training or gameplay lane is opened.

The 20,510-byte artifact
`/tmp/drop7-full-panel-cpi-preflight.json` has SHA-256
`43b90e9976dc266a0a7571bea23dc702b41ee8d693d76e7cb7f591c26ef6f1c0`.
It binds the expected corpus hash, all per-fold exact-state purge counts, the
complete K=7/21/35/63 metrics, thresholds, resource accounting, and the fact
that `newGameplaySeeds=0` and `originTransitions=0`.

## Optimistic phase-conditioned n-tuple (preregistered)

`approaches/ntuple-rl/optimistic-phase/optimistic-phase-ntuple.cpp` is a separately gated attempt to learn the
long-horizon public chance-state value directly rather than regress one played
action and extrapolate across unsupervised siblings.  Its 184 active length-4
n-tuple occurrences are reflection-canonical and initially pool all five
moves-to-rise phases.  At 20 million transitions the weights are copied into
separate phase heads.  On-policy TD uses an undiscounted three-delta forward
view (`lambda=0.5`), fixed-rate stages, and temporal coherence only for the
last ten million of the frozen 100-million-transition budget.

The deployment policy uses all seven coordinate-stratified chance strata for
each legal root action.  It iteratively deepens over one and two row-rise
boundaries, keeps the full root width, and admits two internal actions.  The
future principal variation follows the sampled outcome closest to each
bundle's mean backed-up value.  This makes the search a deterministic,
bounded representative-outcome rollout—not exact expectiminimax.  Every
evaluation decision must finish both boundaries within 100,000 transition
calls; partial iterations and fallback decisions are fatal.

The source was frozen after two independent semantic reviews at SHA-256
`7906961c43012f76076c22efa156150423beedc3cd0fbdd43b7a50a57d63044c`.
Strict Clang 21 and GCC 14 optimized executable/library builds, no-main checks,
capability-enabled checks, and ASan/UBSan all pass without opening a seed.
Self-tests cover the conventional forward view, reflection, coordinate
stratification, bounded one/two-boundary work, schedule boundaries,
bit-equivalent transition checkpoint/resume, corruption/provenance rejection,
and every staged gate.  The phase model with temporal-coherence accumulators
uses 65.4 MB of parameter storage.

The immutable preregistration is
`artifacts/protocols/optimistic-phase-ntuple/protocol.json`, SHA-256
`3f5af46853e57073f928dd2bf04ab752a11d90d0a61783c1f83e34c07c58e810`.
Its compile-time-locked fit lane is the previously unused full seed family
`0x6d000000...0x6dffffff`; the canonical manifest has SHA-256
`01cc9d694a0e8422cb4b63951c52748dbe08d2f0c2e154af8854f2f4bc5d583c`.
The adjacent 256-game development cohort
`0x6e000000...0x6e0000ff` has manifest SHA-256
`2862af3c129741735fe9d30165806a5620849b0b8c65dcb26879256ccc4afbde`,
but that capability remains absent from the training binary.

Training must stop exactly at 50 million transitions.  The fixed burned
64-game cohort `0x3d200000...0x3d20003f` can authorize continuation only if
the two-boundary policy averages at least 300,000 points and 90 moves and
strictly beats its direct n-tuple policy on both score and moves in each
ordered 32-game half.  A continued 100-million checkpoint must repeat those
conditions and also be non-inferior to corrected D4 in aggregate score/moves
and in both paired one-sided 95% lower bounds.  Only then may it emit an atomic
qualification artifact whose exact SHA-256 can be compiled into a new binary
to open the 256-game development cohort.  Development requires mean score
above 1.05 million and a whole-game bootstrap lower-95 above one million
before any protected seed can be touched.  At this preregistration point no
training, Stage-A, development, protected, or final seed has been opened by
this experiment.

The exact training binary was then built with Apple Clang 21.0.0 and the fit
manifest as a compile-time capability.  Its SHA-256 is
`6c0a54bbd6f906383293cbd4e9515e3a9657baa2a0ef8f17978f0c71543c538f`;
the source-bound self-test passed and printed the canonical manifest byte for
byte.  Training used 5-million-transition-or-smaller resumable chunks without
evaluating or selecting an intermediate checkpoint.  It reached exactly 50
million transitions across 1,057,844 completed games in 1,504.404 seconds.
The final chunk averaged 176,247 points and 54.811 moves; the checkpoint had
no censored completed game, retained one active game and two pending forward
views, and was frozen at SHA-256
`2c4e2fba4157066c09dd8cde215a27cbf45f5757973b40c30d93e7487165b5dc`.

The mandatory burned Stage-A gate rejected the family decisively.  On all 64
games, the direct n-tuple policy averaged 181,733.422 points and 56.359 moves.
The fixed two-boundary rollout made it substantially worse: 113,643.969
points and 37.375 moves.  Ordered-half direct/search results were
180,667/118,367 points and 56.125/38.719 moves, then 182,800/108,921 and
56.594/36.031.  Thus the search failed both the 300,000/90 absolute gate and
both score/move half comparisons.  It completed every decision, made no
illegal move, and censored no game, so this is an algorithmic failure rather
than a resource fallback.

The derived 50-million-transition summary artifact has SHA-256
`e29db3c0e2e4d12f06b463817558e547ba132d69fc7bb5959f0054f24e35620b`.
Its ordered-result SHA-256 is
`96752e808ca935d3e6b5ff40e001b2deeeba4809ab43b1cf7136388b24125dbf`.
No qualification artifact was written; training stops before 100 million,
and the `0x6e` development cohort plus all protected/final seeds remain
unopened.

## Explicit reservoir and constructive-cycle policies (rejected)

Several corrected-score experiments tested a more literal version of the
quiet-build strategy. They are recorded together because the
important result is architectural: making the stored chain explicit did not
make it a reliable replacement for the tactical search.

`approaches/constructive-reservoir/vertical-reservoir/vertical-reservoir-policy.cpp` used nonlinear vertical-viability,
release, escape-well, low-cap, cover-access, and immediate-transition features
and optimized complete games directly with CEM.  On its 128-game tournament,
the frozen candidate averaged 160,498.094 points and 50.891 moves versus fair
D1's 178,554.438 and 55.750.  Score, lifetime, lower-quartile score, and reveal
flow all failed admission, so the probe remained unopened.  The source and
artifact SHA-256 values are
`0f5d0f967c55f72ea48d45331832727a76857f042478135e4c22370821aa5aed`
and
`fcd6112a7ce176cb6cbe69694f4ea2a1c0b9c15a1c0f12f950d6ac8de92b3c21`.

`approaches/constructive-reservoir/viability-controller/viability-reservoir-controller.cpp` made the strategy discrete: charge,
dig, release, repair, and emergency options were selected from public
viability and trigger certificates.  Its 32-game Stage A reached only
132,537.094 points and 43.281 moves versus fair D1's 172,697.625 and 53.969;
clear/reveal flow also fell from 1.741/0.936 to 1.511/0.729 per move.  The
source and artifact SHA-256 values are
`ce0c1188e798985e7268e6caa03a8c32989ac387544b111654ef86f7d625c4e8`
and
`5b558b17284d4cf3d0cf4f6f2d7a8fcc641b70a1a36664df13cdaac9daecd6fa`.

`approaches/constructive-reservoir/constructive-spectrum/constructive-spectrum.cpp` then planned across the remainder of the
current five-drop rise cycle plus one build cycle.  A D3 tactical shortlist
fed a seven-scenario public rollout whose terminal target rewarded reachable
high-number reservoirs, overlapping trigger keys, and exposed edge covers.
This was a real improvement over fair D1 on its 32-game Stage A: 266,695.500
points/79.500 moves versus 157,198.063/49.875, with 25 of 32 joint wins and
higher clear/reveal flow.  It still did not displace D4.  The frozen D4
integration admitted only D4's top two near-tied actions, yet averaged
283,286/83.750 on its fitting quartet versus exact D4's 372,870.500/106.250;
it switched 37.0% of moves and won only one pair.  The constructive and D4
integration source SHA-256 values are
`18158adb588160a9969b0c2b0ef52da4446be9998b22d5ea6f6d3b1118805caf`
and
`69adacb4cbfb7810f299f6c4d11df6be3bfbba762a03bdc6107ba868fbb37121`;
the Stage-A and D4-fitting artifacts have SHA-256 values
`0b726154029f197a169b5c34cf7351b5acbe52c5ce8e8acfb16d68979889e348`
and
`63d77903beed9457ca1644286e2e0c94fd661ff7a3226aba7febc2b21222267a`.

The fixed horizon ablation in `approaches/constructive-reservoir/constructive-spectrum/constructive-horizon-scale.cpp` compared
H7, H12, H17, and H27 on the same 32 fitting games.  H12 was best at
299,730.563 points/88.344 moves, versus H7's 258,223.938/77.219, but achieved
only 16 joint wins and failed the frozen robustness gate.  Longer was not
monotonic: H17 fell to 202,634/61.969 and H27 to 261,633/77.938.  A separate
coefficient-free H12 Pareto risk gate reached 302,114.906/89.281 versus H7's
254,541.344/76.344, but switched only 286 of 918 disagreements and produced
17 rather than the required 20 joint wins.  Neither experiment opened a
screen.  Their source/artifact SHA-256 pairs are
`a1e88f8155f83d3f6a2035a1f218ccacd648e8f9dbfed818eca79253216f7834` /
`964b8aaecb170e71f5c22526162dfa031cfcdeb73252fb58ab8fa30a48a6f191`
and
`dd3b218f872451bedabc6b2f2e744c3be5f28399097e5a10a99339703b2bd4b7` /
`a63f8b3f3d1340065bfaa53ee2f9db5c9ae8a23f6eeb90e080296a3da067a6fe`.

These results falsify two tempting shortcuts.  An explicit reservoir motif is
not enough by itself, and a single sampled continuation policy does not become
trustworthy merely by extending it from seven to twelve or twenty-seven
moves.  The H12 gain is nevertheless useful evidence that complete-cycle
planning carries information absent from D1; a future method must retain that
signal without overriding D4 on unstable sibling comparisons.

## Tail-focused complete-game CEM (rejected before Stage A)

`approaches/constructive-reservoir/tail-survival-cem/tail-survival-cem.cpp` tested whether the failure of the evolved public
policy was mainly its mean-score objective.  It retained the frozen 165-weight
phase evaluator and completed D3/internal-width-two/three-stratum search, but
optimized a preregistered robust survival curve at 75/100/150/225/300 moves.
The 32-generation run evaluated 33,792 fitting candidate-games and then
reranked the starting policy plus 64 archived means/champions on the same
fresh 256-game tournament, for 50,432 candidate-games in all.  It finished in
3,334.006 seconds with 2,998,272 bytes peak RSS.

The starting policy averaged 208,940.695 points and 64.004 moves.  The frozen
champion improved only to 214,968.934 and 65.590; lower-quartile lifetime was
38.250 moves, clear/reveal flow was 1.800/0.967, only two of 256 games reached
150 moves, and none reached 225.  It failed every admission check, including
the 500,000-point/150-move tournament floor, so Stage A was not opened.  This
is useful negative evidence against the idea that a different complete-game
tail scalar can rescue the existing linear phase policy.

Strict Clang/GCC builds and ASan/UBSan self-tests pass, and the burned-only
preflight opened zero fitting, tournament, Stage-A, protected, or final seeds.
The frozen source, result, checkpoint, and golden artifact SHA-256 values are
`47832bd8dfeed37975e7ad808e489d3a7762937ffae63142026de729a9672a83`,
`c7c6a04d696ceb15e588caad88313277026b6f8f87cbc2778864aad6332517c3`,
`e83e1f0788be00e4902a3ae7316edf2bcd83fc068a5624e3621564acfe6998a5`,
and
`eddae1fdf269581b690046cebf0abdacfd222af8c42bb9ff41256019c99019d7`.

## Martingale-dual B0 ranking audit (rejected)

`approaches/terminal-policy-iteration/deployment-panel/martingale-dual-b0.cpp` tested a leakage-controlled
information-relaxation ranker on the locked, already-consumed H200 sibling
corpus: 477 public roots from eight origins, with no new gameplay seeds.  The
exact frozen protocol was
`drop7-martingale-dual-b0/h12/b8/shared-kernel7/evaluation7/fair-d4-leaf/v2`;
the corpus SHA-256 was
`bfda8ae32fa0be3577c6b27f6413aba28f2854930e2f91a0dcf7674808f04196`.

The candidate searches 12 plies, keeps an eight-state beam, and averages seven
planner scenarios.  Its cutoff value `V` is the frozen public leaf used by
fair-D4.  At every canonical public state and action, planner realizations and
penalty expectations use the same exactly uniform seven-member empirical
transition support.  Each transition is charged

```text
z = reward + V(next) - E_support[reward + V(next) | public state, action]
```

and the beam accumulates `reward - z`.  Thus `z` has exact mean zero under that
empirical one-step law for every fixed public state/action.  Reflection
canonicalization and public-state-only APIs prevent score, origin, history,
scenario, or tape identity from entering a decision.  A separate
seven-scenario evaluation lane uses independent event-keyed reveal and
visible-disc domains and a public fair-D1 continuation.  Root ranking was
compared with the stored H200 `meanScoreReturn` teacher, with exact fair-D4
root Q values as the baseline.

This construction is **not a certified information-relaxation upper bound**.
Beam width eight prunes the anticipative action tree instead of taking the
exact supremum over all relaxed policies, the 12-ply search terminates at a
heuristic fair-D4 leaf rather than terminal return, and the seven-outcome
kernel only approximates the game's full chance law.  The reported
dual-adjusted values are therefore heuristic sibling-ranking scores, not
bounds on the true game value or even guaranteed exact finite-horizon relaxed
optima.

| Ranker | Top-1 accuracy | Pairwise accuracy | Normalized regret |
| --- | ---: | ---: | ---: |
| Martingale-dual B0 | 28.9308% | 59.8460% | 0.350157 |
| Frozen fair-D4 | **38.1551%** | **66.8191%** | **0.276577** |

The gate was frozen before reading the corpus.  It required complete values
for every legal sibling, top-1 no worse than fair-D4, at least a
one-percentage-point pairwise gain, normalized regret no more than 95% of
fair-D4, non-regression in at least six of eight whole origins and in both
ordered four-origin halves, and at least 70% agreement between the two planner
scenario splits.  Completeness passed, but the candidate passed 0/8 origins,
regressed in both halves, and had only 22.6415% split stability.  The aggregate
pairwise score was 6.9731 points lower, and normalized regret was about 26.6%
higher than fair-D4.  The gate therefore failed.

The completed run took 1,511.812 seconds and peaked at 14,647,296 bytes RSS.
It reported 11,387,953 planner transitions, 79,715,671 penalty transitions,
249,591 independent evaluation transitions, 15,314,013 units of fair-D1 work,
and 572,887,018 units of fair-D4 work.  This is decisive negative evidence for
the bounded B0 ranker: retain fair-D4 and do not advance this candidate to
gameplay, protected validation, or the final cohort.

The source, result, and checkpoint SHA-256 values are
`5e673ae4ce765dc3cee96f844b9342f1616fc665c6f0cd6fd63185333cef251b`,
`68be2904af1574bb87b5c08f1d30483b4c67518a80e85c780057fb8f06c9a6f5`,
and
`22a49fab5334bc630367519b6611d912589089787b06677cfe8a86217ecd9698`.

## Public regenerative policy-iteration B0 (rejected)

`approaches/terminal-policy-iteration/public-regenerative-b0/public-regenerative-policy-iteration-b0.cpp` tested whether complete
legal-sibling evaluation could fix the extrapolation failure seen in the
regenerative expert and learned action rankers.  It opened only the locked,
already-consumed corpus of 477 public roots from eight whole-game origins;
the stored H200 outcomes were not used for selection, and no origin game was
replayed.  Policy inputs contained only the board, visible next disc, and
moves-to-rise phase.  Origin, score, history, scenario, future-disc, and
future-reveal metadata were excluded.

Every root began with all legal actions under seven common H25 scenarios.
The three surviving challengers plus exact fair-D4 were re-evaluated under 21
nested H50 scenarios.  An independent 21-scenario H75 panel could admit a
challenger only when its paired return lower bound, clear count, and reveal
count all beat D4.  A final independent 35-scenario H75 panel compared the
admitted action with D4.  All continuations used a completed, full-width
public D2/s5 policy, while exact public D4/s5 remained the fallback.  Chance
streams were common across siblings, nested only between the two elimination
rounds, and domain-separated for the confirmation and evaluation panels.

The frozen gate required at least 70% split stability, 5% override coverage,
75% beneficial-override precision, 1.10 times D4's raw score, 1.05 times its
restricted mean survival, positive one-sided 95% whole-origin lower bounds
for utility and moves, nonregressing clear/reveal flow, at least six of eight
nonregressing origins, and nonregression in both ordered halves.

The candidate was statistically indistinguishable from simply retaining D4.
It overrode only 11 of 477 roots (2.3061%) and only six overrides were
beneficial (54.5455%).  Its score and survival ratios were 1.000718 and
1.000621; mean utility and move deltas were positive at 695.637 and 0.02660,
but their lower-95 bounds were negative at -169.897 and -0.01365.  Candidate
clear/reveal flow also trailed D4 slightly at 1.966311/1.053280 versus
1.966329/1.053383.  A1/A2 stability was 49.6855%, five of eight origins
nonregressed, and both ordered halves passed.  Every gate except the ordered
halves therefore failed.

The complete fresh run used 2,509,342 synthetic transitions, 2,436,507 D2
calls, 4,625,296,860 D2 work units, and 572,887,018 D4 work units.  It took
1,867.000 effective four-thread root seconds, peaked at 35,831,808 bytes RSS,
and opened zero gameplay, protected, or final seeds.  This remains an offline
root-panel proxy rather than a complete-game result: only the root action was
improved, and subsequent actions reverted to public D2.  It nevertheless
rejects the current sequential-halving/H75 correction and does not justify
either a gameplay gate or a production persistent-option run: the missing
signal is score, survival, and flow, not merely commitment stability.

The source, result, checkpoint, and preflight SHA-256 values are
`2c55a8c87302d6b9835b513fee64fe4b833513ef6e5c8e0fa7717779d8f29d74`,
`c5c7a1a899ddae7b1aca69f9cf0bd5871a4437b197eb9103af844b06bf317145`,
`f779e29b02ae8b6232a367bafa5b2d848b51a56f9e15a5441756eac5b6a9a453`,
and
`632fd7b3b8d00e1e405204774ce455e373820342e8955b3e0411487a4684850c`.
The run was uninterrupted and its textual dependencies were unchanged.  A
future resumable successor must bind a combined transitive-source hash rather
than only the top-level source hash before a passing result can be trusted.
