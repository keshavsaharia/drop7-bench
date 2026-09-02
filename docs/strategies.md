# Drop7 strategy catalog

This guide explains the strategy ideas in this repository, the evidence for
each one, and the most useful open directions. It begins with the game itself,
then introduces research terms as they become useful.

## The problem

In Drop7, you choose one of seven columns for the next numbered disc. A 3, for
example, breaks when it sits in an unbroken horizontal or vertical group of
exactly three occupied cells. One break can make other discs fall and break,
creating a chain. Breaks beside covered discs also crack or reveal them.

In the fast five-drop mode studied here, a new covered row rises after every
five drops. This creates three competing jobs:

1. **Stay alive.** Keep stacks away from the top before the next row rises.
2. **Open the board.** Break and reveal covered discs, especially dangerous
   ones that are already high.
3. **Prepare useful chains.** Leave numbered discs in arrangements that can
   clear several waves later instead of making only the easiest break now.

A move can help one job and hurt another. Clearing one disc immediately may
destroy a future chain. Building a chain may leave the board too tall to
survive the next rise. Random future discs and random covered-disc reveals make
the tradeoff harder.

The easiest way to understand the computer strategies is to imagine three
kinds of player:

- A **rule-following player** gives points to board features such as low
  height, open edges, revealed covers, and chain readiness.
- A **look-ahead player** tries each legal column in a simulator, imagines
  several possible random outcomes, and chooses the move with the best future.
- A **learning player** plays or observes many games and learns which board
  patterns tend to lead to long survival or high scores.

The strongest established baseline in this repository combines the first two.
It looks four decision steps ahead and uses a carefully designed board
evaluation at the end of each simulated branch. It is called **fair D4**.
“Fair” means that it uses only information a real player could know; it does
not peek at the hidden random seed or future disc sequence.

Fair D4 is a baseline, not a solution to Drop7. On the corrected Hardcore
scoring replay, its eight-game confirmation mean was about 401,000 points. It
did not establish a million-point policy, and no search performed here proves
that its moves are optimal.

## What counts as evidence

### Rules and mode

The research target is the numbered-disc-only, five-drops-per-rise mode called
Hardcore in the original app and Blitz in some later descriptions. It awards
17,000 points at a level rise. Normal and Sequence modes begin with much longer
levels and use a 7,000-point level award. Their score distributions and
strategic constraints are different.

Some early experiments accidentally used the 7,000-point Sequence-style level
award. Those runs can still show that one policy beat another on the same
games, or that an idea was too slow, but their absolute scores are not evidence
for the Hardcore target. See the [mode correction and full experiment
record](research/history.md#mode-specific-scoring-correction).

### Information boundary

A deployable policy may use the visible board, the next visible numbered disc,
and the number of drops until the next row rise. It may not use the game seed,
hidden covered numbers, future discs, future reveals, score history, or an
offline scenario identity. Oracle experiments that use hidden futures are
diagnostics, not playable strategies.

### Status words used here

- **Baseline** means the implementation is verified and has survived the
  repository's comparison gates well enough to serve as a reference.
- **Rejected** means the tested version failed its frozen gate. It does not
  prove that every possible version of the broad idea is bad.
- **Signal only** means a diagnostic or small pilot was interesting but did
  not provide enough independent evidence or meet the runtime requirement.
- **Proposed** means there is not yet an implementation and result.

Small samples are especially dangerous in Drop7. A policy can win because one
random game becomes unusually long. The strongest claims therefore require
paired games, disjoint fitting and evaluation seeds, frozen thresholds, whole-
game uncertainty estimates, censoring counts, and no tuning after a protected
cohort is opened. The experiment record describes the repository's [seed
discipline](research/history.md#seed-discipline).

## Current reference: fair D4

The reference implementation is
[`fair-only-depth4.cpp`](../approaches/fair-expectimax/reference/fair-only-depth4.cpp).
It considers every legal root move, completes four decision plies, and uses
five deterministic, stratified samples at chance nodes. Its leaf evaluation
rewards build readiness and latent chain potential while penalizing height,
covered-disc altitude, low-number clogs, danger near a rise, and other public
risks. The search has fixed work and cache bounds and completes the promised
depth instead of silently returning a partial iteration.

The most useful result is the corrected-score replay on an already-consumed
eight-game confirmation cohort:

| Policy | Mean score | Mean moves | Paired wins |
| --- | ---: | ---: | ---: |
| Fair D3 | 235,071 | 71.000 | 1 of 8 |
| Fair D4 | **400,675** | **116.375** | **7 of 8** |

D4 changed about 34% of the decisions that D3 would have made. It used about
1.35 million logical work units per move and ran at roughly 0.74 moves per
aggregate game-second in that replay. These data establish a strong local
baseline and show that the fourth ply matters with this leaf. They do not
establish optimality, a stable population mean, or a million-point result. The
protected validation and final seed banks were not opened. Full context is in
the [D4 experiment](research/history.md#completed-depth-4-with-the-fair-only-leaf)
and [corrected Hardcore replay](research/history.md#corrected-hardcore-scoring-replay).

## Strategy families already studied

### Hand-built heuristics and shallow search

**Idea.** Describe a good board with understandable features, then choose the
move that produces the best immediate or short-horizon evaluation.

The repository includes phase-aware scoring, tunneling, gray-disc throughput,
virtual ignition, risk-sensitive planning, recursive potential, edge priority,
cycle abstraction, rollouts, sparse expectimax, and evolved feature weights.
The TypeScript implementations are organized under
[`approaches/heuristic-search`](../approaches/heuristic-search), with reusable
evaluators in [`src/core/typescript`](../src/core/typescript).

**Evidence.** These policies helped identify useful concepts — height danger,
reveal flow, build readiness, rise timing — but as complete policies they were
brittle, and no tested hand-built policy displaced fair D4. Per-experiment
outcomes are in the [experiment index](research/experiment-index.md) and the
evidence map below.

**Conclusion.** Keep interpretable features as diagnostics and leaf inputs.
Do not treat a higher fitting score from another weight sweep as a new result
without a fresh, paired gate.

### Fair expectimax and deeper tactical search

**Idea.** Explicitly branch over legal moves and representative chance
outcomes. This is the most direct way to value a move without knowing the real
future.

Code for the reference searches and their extensions is under
[`approaches/fair-expectimax`](../approaches/fair-expectimax). The major
variants tested different leaf terms, transition rewards, seven rather than
five chance strata, root downside/CVaR, CEM-tuned weights, selective extra
depth, vertical-ladder energy, and full-width D5.

**Evidence.** Completed fair D4 was the clear positive result; most additions
made the policy worse on held-out or gameplay gates, and “deeper” was not
assumed to mean “better.” The 2026-08-21 depth-by-chance-resolution factorial
below is the measured form of that lesson; per-variant rows are in the
[experiment index](research/experiment-index.md).

```figure
depth-chance-factorial
caption: Mean score by search depth and chance resolution. With an approximate chance model the best depth is three; with an exact chance model it is four.
```

**Conclusion.** Fair D4 remains the strongest known baseline. Exact or
completed D5 is still scientifically interesting only if its cost can be
reduced enough for a properly powered comparison.

### N-tuple value learning and Q-learning

**Idea.** Learn values for overlapping board patterns. N-tuples can represent
local shapes with far fewer parameters than a table containing every complete
board.

Implementations under [`approaches/ntuple-rl`](../approaches/ntuple-rl)
include episodic and chance-state values, hierarchical position residuals,
temporal-coherence updates, phase-conditioned values, Rainbow-style Q-learning,
native PPO experiments, and a bounded two-rise rollout driven by an optimistic
phase-conditioned n-tuple.

**Evidence.** Corrected and extended n-tuple systems repeatedly improved at an
early stage and then failed their larger gates, and the optimistic
phase-conditioned system's direct policy and two-boundary rollout both fell far
below fair D4 on the mandatory 64-game gate. The retained numbers are in the
[experiment index](research/experiment-index.md) rows for this family.

**Conclusion.** Pattern learning is computationally attractive, but the tested
TD and Q-learning targets did not learn a reliable long-horizon action ranking.
Future work needs better coverage of legal siblings and less extrapolation from
the single action chosen by the data-collection policy.

### Learned values, policies, and neural evaluators

**Idea.** Train a compact model to predict survival, score, or the advantage of
one legal move, then use it directly or as a search leaf.

The experiments in
[`approaches/value-policy-learning`](../approaches/value-policy-learning) and
[`approaches/d4-long-outcome`](../approaches/d4-long-outcome) cover Monte Carlo
state values, structured and denoised NNUE-style models, DQN, policy cloning,
sibling-advantage learning, D4 distillation, and long-outcome classifiers.

**Evidence.** Direct value policies, structured models, counterfactual-
successor leaves, D4-Q clones, and learned sibling rankers usually improved
fitting metrics and then regressed on whole-game-held-out roots. The held-out
ranking metrics for every learned evaluator are charted below; per-experiment
rows are in the [experiment index](research/experiment-index.md).

```figure
learned-ranking-metrics
caption: Learned evaluators against exact search on held-out roots: top-1, pairwise accuracy and normalized regret. Every student sits below its panel's fair-D4 comparator.
```

The repeated failure pattern is important: learning the played action or a
sparse sample of siblings does not provide trustworthy values for every action
the learned policy will later consider. This is a coverage and distribution-
shift problem, not merely a need for a larger network.

**Conclusion.** A learned leaf remains plausible, but a new attempt should
begin with a data-coverage proof, whole-origin splits, calibrated uncertainty,
and exact fallback parity, not with additional model capacity.

### Policy-gradient and actor-critic methods

**Idea.** Learn the column choice directly from complete interaction rather
than fitting a separate value for every candidate move.

Native and PyTorch experiments include PPO, direct PPO, curriculum options,
oracle-manifold features, and a primal-dual actor-critic. See
[`approaches/ntuple-rl/torch-ppo`](../approaches/ntuple-rl/torch-ppo),
[`approaches/ntuple-rl/native-ppo`](../approaches/ntuple-rl/native-ppo), and
[`approaches/ntuple-rl/primal-dual-actor-critic`](../approaches/ntuple-rl/primal-dual-actor-critic).

**Evidence.** The tested PPO and actor-critic pipelines failed their agreement,
memory, performance, or coverage gates, and none opened a gameplay screen. The
retained numbers are in the [experiment index](research/experiment-index.md).

**Conclusion.** The tested policy-gradient pipelines were neither competitive
nor sample-efficient enough. Reconsider them only with a demonstrably better
state representation, training curriculum, and resource plan.

### MCTS, PUCT, and open-loop planning

**Idea.** Grow a search tree around the current board, sampling chance events
instead of enumerating the entire stochastic future.

Implementations live in [`approaches/tree-search`](../approaches/tree-search)
and the open-loop experiments in
[`approaches/heuristic-search/open-loop`](../approaches/heuristic-search/open-loop).

**Evidence.** Observable-state UCT and confidence-gated MCTS overrides failed
their held-out ranking gates, and scaling the tree budget improved short-horizon
D4 imitation while worsening 25-move outcome ranking; the audit traced the
problem to the weak public D1 rollout and repeated empirical chance reservoir
rather than simply too few simulations. See the
[experiment index](research/experiment-index.md).

**Conclusion.** Increasing the tree budget alone is not a promising next
experiment. MCTS needs a stronger continuing policy or long-value model and a
less biased treatment of fresh chance events.

### Long rollouts and terminal policy improvement

**Idea.** Keep fair D4 as the default, but compare close alternatives by
playing them forward for several complete five-drop rise cycles.

The most direct implementation is
[`d4-d2-rollout-veto.cpp`](../approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto.cpp).
Related compression, learned-ranker, and policy-iteration experiments live
under [`approaches/d4-long-outcome`](../approaches/d4-long-outcome) and
[`approaches/terminal-policy-iteration`](../approaches/terminal-policy-iteration).

**Evidence.** A public 25-move D4/D2 continuation produced one striking single
pilot — **signal only**, far beyond its runtime limit — and compression,
fitting completion, compact rankers, and regenerative panels did not reproduce
the signal on disjoint data. The rollout veto's whole-game delta appears in the
screen chart below; retained numbers are in the
[experiment index](research/experiment-index.md).

```figure
screen-deltas-with-bounds
caption: Every screen and confirmation paired delta with its recorded 95% bound, including the 25-move rollout veto's whole-game loss.
```

**Conclusion.** Multi-cycle comparison is one of the more informative open
directions, but the existing implementation is neither fast nor robust enough.
A successor must be cheaper by design and must not turn the one positive pilot
into a tuning target.

### Oracle and hindsight curricula

**Idea.** Let an offline teacher see the future random tape, learn what makes
its actions successful, and distill that knowledge into a public policy that
cannot see the future.

These diagnostics are under
[`approaches/oracle-curriculum`](../approaches/oracle-curriculum), including a
perfect-information oracle, topology and accessible-energy residuals, DAgger,
state curricula, and hindsight planning.

**Evidence.** The perfect-future oracle averaged more than one million points
even under the historical 7,000-point score, proving valuable long-horizon
structure exists in the simulator; every attempt to transfer that advantage to
a public policy failed held-out label, ranking, or gameplay gates, and several
apparent wins reversed on confirmation. See the
[experiment index](research/experiment-index.md).

**Conclusion.** The oracle is useful for measuring information and
representation gaps. Future distillation should first show that its public
features predict oracle preferences across whole held-out games before it is
allowed to control moves.

### Constructive reservoirs and complete-cycle planning

**Idea.** Deliberately store high numbered discs in useful ladders or
reservoirs, then release them in a long chain while continuing to expose
covered discs.

The experiments are collected in
[`approaches/constructive-reservoir`](../approaches/constructive-reservoir).
They include vertical reservoirs, discrete charge/dig/release/repair options,
seven- to 27-move constructive spectra, terminal vetoes, and tail-focused CEM.

**Evidence.** A constructive complete-cycle planner was a real improvement over
D1, but inserting its signal into D4 made frequent unstable switches and
reduced the fitting mean, longer horizons were not monotonically better, and
the 12-move variants still failed their robustness gates. Retained numbers are
in the [experiment index](research/experiment-index.md).

**Conclusion.** Planning across a whole rise cycle contains information that
one-ply heuristics miss. A visible reservoir shape by itself is not a reliable
policy, and longer sampled rollouts are not automatically more accurate.

### Conservative overrides and tail-risk objectives

**Idea.** Preserve a trusted search action unless a learned or simulated
alternative clears a strict confidence, survival, and flow threshold.

This pattern appears in conservative fitted policy iteration, denoised-value
vetoes, D4 long-outcome vetoes, structural terminal vetoes, regenerative
panels, root CVaR, and tail-focused evolution.

**Evidence.** Conservative gates successfully prevented many weak policies from
reaching protected tests, but the admitted switches were too rare, poorly
calibrated, or unstable, mean-only gains often hid lower-tail damage, and the
public regenerative B0 panel was statistically indistinguishable from retaining
D4. See the [experiment index](research/experiment-index.md).

**Conclusion.** Confidence gates are a safety mechanism, not a source of new
strategic information. They become useful only after the underlying challenger
can rank legal siblings on disjoint data.

## Compact evidence map

| Family | Best-supported finding | Current status |
| --- | --- | --- |
| Fair expectimax | Completed D4 materially beat D3 on the recorded comparison | **Strongest known baseline; not optimal** |
| Hand-built features | Height, phase, reveal flow, and build readiness are useful signals | Useful components; standalone policies rejected |
| N-tuple / Q-learning | Compact pattern values learn something, but tested long-horizon action rankings remained weak | Rejected versions |
| Neural values and policies | Fitting gains repeatedly failed under whole-game or sibling distribution shift | Rejected versions |
| MCTS / open loop | More search improved D4 imitation, not long-outcome ranking | Architecture needs a new continuation model |
| Long D4/D2 rollout | One large pilot gain showed possible multi-cycle signal | Signal only; runtime and robustness failed |
| Oracle / hindsight | Future information has very large value, but distillation did not transfer it | Diagnostic only |
| Constructive reservoir | Complete-cycle planning beat D1, but destabilized D4 | Signal below the current baseline |
| Conservative override | Safe fallback logic works mechanically; challenger evidence is weak | Keep as protocol, not as strategy |

## Strategies still worth considering

The following are research directions, not claims that an untested policy will
work. Each should be preregistered and compared against unchanged fair D4.

### 1. Resolve AFBR-40's data closure before implementing it

**AFBR-40** is a working-name proposal for a state-only, action-free public
afterstate model. The intended target spans roughly 40 moves, or eight row-rise
cycles. Training data would be successor-closed: every training decision would
need the afterstates for all legal actions rather than only the action that the
behavior policy selected. Fair D4 would remain the fallback.

This is **only a proposal**. Work stopped before implementation, and there is
no source file, frozen protocol, checkpoint, or result. There is also a known
feasibility problem: the available logged panels may not contain every
transition and successor needed to construct truly successor-closed training
and held-out examples without opening new seeds. That must be audited first.
Calling a sparse sibling panel “successor-closed” would repeat the coverage
problem seen in earlier value models.

A valid next step is therefore a read-only closure inventory: define the exact
afterstate key, enumerate the required legal successors for each candidate
root, and report the percentage whose outcomes can be reconstructed from
existing artifacts. Only a complete or explicitly bounded construction should
advance to a source and protocol.

### 2. Make multi-cycle sibling comparison affordable

The 25-move D4/D2 rollout and the constructive H12 experiments both suggest
that decisions across complete rise cycles contain useful information. A new
method could use a cheaper verified continuation, batched transitions,
incremental D2 evaluation, admissible early elimination, or a model trained on
complete sibling panels. It should preserve common random numbers across
siblings and keep the current one-game pilot out of parameter selection.

### 3. Accelerate completed D5 without changing its semantics

D5 has not received a sufficiently powered, runtime-admissible comparison.
Incremental board evaluation, compact transposition keys, reuse across chance
strata, and measured cache locality are possible engineering directions. The
first milestone should be bit-equivalence with the existing D5 decision on
fixtures, followed by a prospective runtime projection. A faster D5 still has
to beat D4; deeper search is not assumed superior.

### 4. Learn a leaf from complete legal-sibling data

Many failures came from training on the behavior action and evaluating unseen
alternatives later. A better corpus would record every legal action at a root,
use aligned public chance samples, attach multi-cycle outcome distributions,
and split by whole origin game. Before gameplay, the model should beat D4 or
exact D2 in top-action accuracy, pairwise accuracy, normalized regret, and
calibration on every preregistered held-out half. AFBR-40 is one possible
formulation, but not the only one.

### 5. Replace MCTS's weak continuation, not just its budget

A continuing public policy near D4 strength, an independently validated
long-value cutoff, and progressive widening or fresh event-keyed chance samples
could address the diagnosed MCTS bias. The key offline test is long-outcome
ranking, not agreement with D4's short-horizon Q values.

### 6. Treat human strategy as testable feature hypotheses

High covered discs, weak edge connectivity, and simple 3–4-wave chains are
plausible human ideas. They can be converted into reflection-safe public
features and tested first on preserved sibling panels. This is more informative
than copying a human rule directly into the policy. A feature should advance
only if it adds stable held-out action-ranking information beyond D4/D2.

### 7. Study objectives after establishing action quality

Survival probability, lower-tail return, reveal throughput, and catastrophic
rise risk remain valuable evaluation measures. Past CVaR and tail-CEM failures
show that changing the objective cannot repair a weak action model. Once a
challenger ranks siblings reliably, distributional gates and conservative D4
fallback can test whether it improves both the mean and the bad-game tail.

## External prior work

These sources are useful context, but none supplies a benchmark directly
comparable with this repository's public-information Hardcore protocol.

### Approximate Q-learning report

Erez Klein and Ben Friedmann's [Drop7 Q-learning
report](https://ekreate.github.io/projects/drop7_q_learning.pdf) formulates the
game as an MDP and uses linear feature approximation because exact state tables
are infeasible. Its reward and reported units emphasize surviving moves, and
its environment and evaluation protocol are not interchangeable with this
repository's corrected Hardcore score benchmark. It is useful prior work for
feature-based Q-learning and for explaining the state-space problem, not a
numeric baseline here. On 2026-09-02 the report was reproduced from its own
code and its six features were ported onto the Rust engine
([approach page](../approaches/value-policy-learning/klein-friedmann-linear-q/README.mdx)):
the numbers reproduce, the training is front-loaded by its per-move 1/t step
size, the regularisation claim does not hold for the shipped code
([RS-20260902T082726Z-75606ce7](../research/results/RS-20260902T082726Z-75606ce7.json)),
and on corrected Hardcore rules the features are a survival heuristic worth
about ten moves over random at pilot tier
([RS-20260902T084356Z-2488ecc7](../research/results/RS-20260902T084356Z-2488ecc7.json)).

### David Walton's Sequence-mode solver

David Walton's [solver source](https://github.com/dwalton76/Drop7-Sequence-Mode)
and [project write-up](https://programmablebrick.blogspot.com/2013/03/drop7-with-lego-mindstorms-nxt.html)
search fixed blocks of a known Sequence-mode disc stream. The reported play
reached more than five million points, including long chains and repeated
board clears. It demonstrates the value of macro-scale chain planning when
the future sequence is known. Sequence mode exposes a fixed future and has
different rise/scoring rules, so the method and score are not evidence for a
public stochastic Hardcore policy. Its block-search idea is still relevant to
bounded cycle planning if future knowledge is replaced by fair chance models.

### Experienced-player strategy: anecdotal

The [Drop7 strategy notes by an experienced
player](https://blog.adamatomic.com/post/46685122887/drop7-strats) recommend
prioritizing high covered discs and watching the less-connected edge columns
in Hardcore mode. The author explicitly warns that the ideas may be wrong or
luck-dependent. Treat them as **anecdotal hypotheses**, not experimental
evidence. They are valuable because they suggest small, interpretable features
that can be tested without giving the policy private information.

## Recommended decision standard for the next candidate

A serious successor should satisfy all of the following before any
million-point claim:

1. Use the verified rules in [`engine.hpp`](../src/core/native/engine.hpp) and
   pass native/TypeScript parity tests.
2. Make decisions from public state only and prove deterministic reflection
   behavior, legal fallback, and resource bounds.
3. Freeze the source, data manifest, model, search budget, thresholds, and seed
   ranges before evaluation.
4. Compare against unchanged fair D4 on paired, disjoint whole games; report
   score, moves, clear/reveal flow, lower-tail outcomes, uncertainty, and
   censoring.
5. Require stable improvement across preregistered halves or folds, not only a
   higher aggregate mean.
6. Keep protected validation and final cohorts sealed until the candidate and
   its qualification rule are fixed.

The complete chronological record, including exact commands, gates, artifacts,
and negative results, remains in
[`docs/research/history.md`](research/history.md). This catalog is the map; the
history is the audit trail. The ordered, hardware-aware next program is in the
[`research roadmap`](research/roadmap.md).
