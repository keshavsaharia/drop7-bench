# Research Status

## August 2026

The simulator and reference searches are mature enough to support reproducible
research, but the strategy problem is unsolved. Corrected-score [fair depth-4
expectimax](/approaches/fair-expectimax) is the strongest dependable reference found so far. 
Its average score across 64 games is **308,296 points**, far below the goal of a
one-million-point average.

Exploratory work on 2026-08-20/21 reproduced that reference on fresh seeds and
completed the depth-by-chance-resolution factorial. Two results from it strongly
suggest what to do next:

- **The fourth ply is worth +86,172 points, but only with an exact chance
  model** (95% lower bound +26,605, 40-0-24 over 64 paired games). With an
  approximate five-stratum model the same fourth ply is worth −7,723. Chance
  resolution does not merely add points; it changes the sign of the depth
  gradient.
- **The fifth ply is not measurable by this design.** At five strata it is
  −8,624 over 64 paired games; at seven strata it is +23,367 over 32. Both are
  far inside their own detection floors (see below), and the seven-stratum
  estimate **changed sign** when the cohort grew from 16 games to 32. The
  supported statement is that any depth-5 effect is smaller than a 64-game paired
  cohort can resolve, not that it is zero. What can be said is economic: even
  the optimistic estimate is +23,367 for 35.6x the work per move.
- **A 64-game paired cohort in this game cannot see an effect below roughly
  50,000 points.** Paired whole-game deltas have a standard deviation of
  228,000 to 371,000 depending on the contrast, so the one-sided 95% detection
  floor runs from 47,052 to 107,988. Every null result in the depth factorial
  is below its own floor and is therefore a non-measurement rather than
  evidence of no effect. Resolving the observed +23,367 would take about 684
  paired games, about 13 wall-days at that arm's measured throughput.
  **Choose experiments whose predicted effect exceeds the floor, or find a
  lower-variance estimator than complete games.**

No candidate has qualified for the protected validation protocol. The frozen
record marks both the protected and one-shot final cohorts as unopened. See the
[research status evidence](/docs/research/status/evidence) for details.

## How the research progressed

### 1. Establishing a trustworthy simulator

The work first aligned TypeScript and native Hardcore rules, chain scoring,
gray-disc reveals, row rises, and deterministic seed behavior. A scoring audit
corrected the five-move mode's level award from 7,000 to 17,000. Results made
with 7,000-point scoring remain historical Sequence-scored evidence and are not
used for the Hardcore million-point claim.

### 2. Hand-written policies and shallow lookahead

Feature-weighted heuristics, open-loop beams, risk penalties, tunneling,
ignition, evolutionary search, and shallow fair lookahead provided fast
baselines. They showed that immediate score alone is a poor guide: a policy
must keep revealing covered numbers and preserve future chain structure.

### 3. Fair expectimax as the reference

Completed fair D4 consistently improved on D3 in the corrected-score small
cohorts and became the reference for later experiments. Going deeper was not
automatically better. Selective D5, full D5, and cycle-boundary variants often
spent much more work, sampled chance outcomes too noisily, or overrode good D4
actions on unstable estimates.

### 4. Learning public-state values

N-tuples, Monte Carlo values, NNUE-style networks, DQN variants, phase students,
PPO, and fitted-policy methods explored longer horizons. A repeated problem was
**sibling extrapolation**: a model learned the outcome of the action that was
played, then deployment asked it to choose among several actions it had not
observed equally well. Low value error on visited states did not guarantee good
root-action ranking.

### 5. Oracle and long-outcome teachers

Privileged future-aware planners, D4 distillation, 25-move outcomes, rollout
vetoes, and curriculum data were used to build better labels. They found
predictive signal, but students usually lacked enough diverse successor data,
failed held-out sibling ranking, or were too slow to improve complete games.
Oracle strength is an upper-bound teaching signal, not a legal policy result.

### 6. Constructive cycles and explicit reservoirs

Reservoir, viability-controller, constructive-spectrum, and tail-survival
experiments tried to build chain structures deliberately across row rises. A
12-move constructive horizon improved over a 7-move version in one development
comparison, but longer horizons were not monotonic and the policies did not
displace D4. The idea still appears useful as a feature or option, but not yet
as a standalone controller.

### 7. Offline policy improvement around D4

The latest completed work evaluated every legal sibling on a locked panel of
477 public roots. A martingale-dual H12 ranker underperformed D4: 28.93% vs
38.16% top-1 accuracy, 59.85% vs 66.82% pairwise accuracy, and higher normalized
regret. A regenerative policy-iteration variant was nearly identical to D4 but
overrode only 11 roots; six overrides helped, confidence bounds were negative,
and only five of eight origins did not regress. Neither justified a gameplay
run.

## Most useful conclusions so far

- **Fair chance handling matters.** Optimistic, worst-case, or tiny reused
  reveal samples can rank moves incorrectly.
- **More depth is not automatically more strength.** Horizon, continuation
  quality, chance variance, and work budget interact. The 2026-08-21 factorial
  makes the interaction concrete: with an approximate chance model the best
  depth is three and deeper search actively loses; with an exact chance model
  the best depth is four. Depth and chance resolution *substitute* rather than
  compound: at equal work, depth 3 with six-fold reveal sampling (4.24M per
  move, 376,442) and depth 4 with single-sample reveals (4.96M, 398,498) are
  statistically indistinguishable, and doubling reveal samples on top of ply 4
  buys nothing measurable for 4.07x the work. Refining the reveal distribution
  is itself worth +64,116 at depth 3 (95% lower bound +7,475) and saturates
  before full joint coverage.
- **Flow matters before spectacular chains.** The task record repeatedly used
  roughly 2.4 numbered clears and 1.4 reveals per move as the region associated
  with stable long games. Treat these as diagnostic targets from limited runs,
  not proven universal thresholds.
- **Static board potential is insufficient.** Similar-looking boards can have
  very different futures depending on how reachable triggers and covered discs
  evolve across rises.
- **State-value accuracy is not action-ranking accuracy.** Training data must
  cover legal siblings or use an objective designed for relative action value.
  As of 2026-08-21 this is necessary but demonstrably not sufficient: a student
  given successor-closed coverage, every legal sibling, and exact search-value
  labels still ranked worse than a one-ply exact search.
- **Score is heavy-tailed, and this sets a hard measurement floor.** One
  million-point game can coexist with a much lower average; paired whole-game
  cohorts and confidence bounds are essential. But pairing does not rescue
  small effects: single-seed paired deltas in the depth factorial reach
  −1,002,862 and +958,985, more than twice the cohort mean, so a 64-game
  cohort resolves nothing below roughly 50,000 points. Before running a cohort,
  state the effect size the mechanism predicts and check it against the floor.
- **D4 is useful but not the answer.** It is a strong tactical fallback and
  teacher, yet its average is not close enough to qualify.
- **The leaf evaluation prices what search cannot see.** Refitting it toward a
  quantity the search already computes exactly — achievable clears over the next
  few moves — loses monotonically, and loses by destroying the upper tail rather
  than the lower one. A leaf that correlates weakly with short-horizon clear
  rate is behaving correctly, not failing.
- **Cheap proxies for this game invert.** Three independent times a short-horizon
  screen has ranked configurations in the opposite order from complete games: the
  `suite-h9-v1` scenario suite, a depth-3 screening run, and an eight-move
  achievable-clear label. Screen at the depth you intend to deploy, or do not
  screen.

## Directions closed by exploratory evidence, 2026-08-20/21

Each entry rejects the exact configuration tested, at *development* or *pilot*
tier. None of them is a proof that the underlying idea is impossible, and each
names what would reopen it.

| Direction | Result | Reopens if |
| --- | --- | --- |
| Search depth beyond four plies | Not measurable: +23,367 (7 strata, n=32) and −8,624 (5 strata, n=64), both inside detection floors of 107,988 and 47,052, at 23-36x work | A design that can resolve sub-50k effects, or a mechanism predicting an effect above the floor |
| Stacking reveal sampling on top of the fourth ply | −41,950 at M=2 (95% upper bound +17,541), 4.07x work | A wider dose is tested; only 28.6% joint coverage was affordable at depth 4, and the dose that worked at depth 3 was 85.7% |
| Harsher terminal (death) utility | Parameter saturated; play is identical past the current value | Never, for this parameterization |
| 25-move rollout veto | −21,887 points per veto | A lower-variance rollout estimator |
| Refitting the leaf toward achievable clear rate | Monotone loss across six arms; fully-fitted vector −237,182 | A refit against remaining lifetime rather than achievable clears |
| Direct action override by the compact afterstate model | Override gate failed; harmful in one half-fold | A materially larger student, or a stronger teacher than D1-continuation |
| Compact afterstate model reproducing D4's ordering | Top-1 0.375 against a 0.60 gate; exact D1 scores 0.486 | A materially larger model; the claim is only established for compact evaluators |
| The `suite-h9-v1` scenario benchmark as a strength measure | Ranked policies backwards, Spearman −0.257 | A validated longer horizon; it remains usable as a diagnostic |

The two afterstate rows matter more than their tier suggests. The program's
standing explanation for every failed learned policy was insufficient sibling
coverage. The 2026-08-21 experiment supplied successor-closed coverage, every
legal sibling, exact search-value labels, and completeness 1.0 — and the student
still ranked worse than a one-ply exact search. That relocates the obstacle from
the training data to the capacity of a compact board evaluator, and it makes
"train a bigger student" a falsifiable next step rather than a hopeful one.

## Open work

The next research step should be evidence-driven rather than another broad
architecture sweep:

1. Re-register any resumed SHA-locked experiment against the reorganized source
   tree and rerun cross-engine parity.
2. Refit the search leaf against remaining lifetime, the quantity that
   correlates with score at r = 0.9995, rather than against achievable clears.
3. Test whether student capacity is the binding constraint, since that is now
   the explicit claim on the table and the cheapest one to falsify.
4. Test long-cycle features as bounded corrections to D4 before allowing them
   to control an entire game.
5. Resolve the three recorded divergences between this simulator and the
   shipped game, which is an owner decision and not an agent decision.

Items 1, 4 and 5 are unchanged. Items 2 and 3 replace the AFBR-40 closure
sequence, whose data-feasibility question was answered directly: closure was
achieved and did not rescue the model.

AFBR-40 as originally proposed has no implementation, protocol, model, or
measurements in this repository and must not appear in a list of attempted
results. The action-free afterstate models that *were* built and measured on
2026-08-20/21 are separate, separately recorded, and are not AFBR-40.

For configurations and every retained entry point, use the
[experiment index](experiment-index.md). For the unabridged chronological
record, use the [experiment history](history.md). For alternatives and research
priority, use the [strategy landscape](../strategies.md) and the staged
[research roadmap](roadmap.md). Future agents must use the standard
[benchmark contract](../benchmarks.md) and machine-readable records under
[`research/`](../../research/README.md); these do not retroactively upgrade the
historical evidence above.
