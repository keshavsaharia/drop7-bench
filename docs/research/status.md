# Research Status

## August 2026

```figure
score-vs-depth
caption: Mean score against search depth under the five-stratum and seven-stratum chance models. Hover or focus a point for its value, bounds, cohort size and source record.
```

```figure
strata-5-vs-7
caption: Paired contrasts from the same factorial: seven strata minus five strata, and the fifth ply minus the fourth, with one-sided 95% lower bounds. The source table under each figure names the record every point was copied from.
```

The simulator and reference searches are mature enough to support reproducible
research, but the strategy problem is unsolved. Corrected-score [fair depth-4
expectimax](/approaches/fair-expectimax) is the strongest dependable reference found so far. 
Its average score across 64 games is **308,296 points**, far below the goal of a
one-million-point average.

Exploratory work on 2026-08-20/21 reproduced that reference on fresh seeds and
completed the depth-by-chance-resolution factorial. Two results from it strongly
suggest what to do next:

- **The fourth ply is worth +86,172 points, but only with an exact chance
  model** (95% lower bound +26,468, 40-0-24 over 64 paired games). With an
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
[research status evidence](status/evidence.md) for details.

## How the research progressed

Seven eras, one sentence each; every configuration and outcome below is tabled
row by row in the [experiment index](experiment-index.md).

1. **A trustworthy simulator.** TypeScript and native Hardcore rules were
   aligned and a scoring audit corrected the five-move level award from 7,000
   to 17,000; results made with 7,000-point scoring remain historical
   Sequence-scored evidence.
2. **Hand-written policies and shallow lookahead** provided fast baselines and
   showed that immediate score alone is a poor guide.
3. **Fair expectimax became the reference**: completed fair D4 consistently
   improved on D3 in the corrected-score small cohorts, while deeper variants
   were not automatically better.
4. **Learning public-state values** repeatedly hit **sibling extrapolation**: a
   model learned the outcome of the action that was played, then deployment
   asked it to rank actions it had not observed equally well.
5. **Oracle and long-outcome teachers** found predictive signal, but students
   failed held-out sibling ranking or were too slow; oracle strength is an
   upper-bound teaching signal, not a legal policy result.
6. **Constructive cycles and explicit reservoirs** look useful as features or
   options, but no tested controller displaced D4.
7. **Offline policy improvement around D4** on a locked panel of 477 public
   roots underperformed or barely overrode D4; neither ranker justified a
   gameplay run.

```diagram
diagram-sibling-extrapolation
caption: The era-4 failure mode: training labels the played action's successor, deployment asks the model to rank all seven siblings — six of which it never observed equally.
```

```figure
learned-ranking-metrics
caption: Every learned evaluator against its panel's exact-search comparator on held-out roots: top-1, pairwise accuracy and normalized regret. Panels differ across points and are named per category; compare within a panel, not across.
```

## Most useful conclusions so far

- **Fair chance handling matters.** Optimistic, worst-case, or tiny reused
  reveal samples can rank moves incorrectly.
- **More depth is not automatically more strength.** Depth and chance
  resolution *substitute* rather than compound: with an approximate chance
  model the best depth is three, with an exact chance model it is four, and
  the budget frontier has a flat top at the fair-D4 operating point, reachable
  from either axis
  ([`RS-20260821T192140Z-189fe392`](../../research/results/RS-20260821T192140Z-189fe392.json),
  [finding-16](../exploratory/finding-16-factored-reveal-sampling.md)).

```figure
depth-chance-factorial
caption: Mean score by search depth and chance resolution on the shared cohort. The sign of the depth gradient flips with the stratum count.
```

```figure
score-vs-work-frontier
caption: Mean score against logical work per move. Two different ways of spending the same budget — depth or reveal sampling — land in the same place, and the frontier is flat at the fair-D4 operating point.
```

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
  million-point game can coexist with a much lower average, and pairing does
  not rescue small effects: before running a cohort, state the effect size the
  mechanism predicts and check it against the detection floor
  ([`RS-20260821T205102Z-d89df4b5`](../../research/results/RS-20260821T205102Z-d89df4b5.json)).

```figure
detection-floor-map
caption: The six paired contrasts of the depth factorial against their detection floors. Every significant result clears its floor; every null sits below it and is a non-measurement, not evidence of no effect.
```

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

## Directions closed by exploratory evidence, 2026-08-20/23

Each entry rejects the exact configuration tested, at *development* or *pilot*
tier. None of them is a proof that the underlying idea is impossible, and each
names what would reopen it.

```figure
screen-deltas-with-bounds
caption: The score-valued closures at a glance: every screen and confirmation paired delta with its recorded 95% bound. Whiskers are absent, not zero, where a record carries no upper bound.
```

| Direction | Result | Reopens if |
| --- | --- | --- |
| Search depth beyond four plies | Not measurable: +23,367 (7 strata, n=32) and −8,624 (5 strata, n=64), both inside detection floors of 107,988 and 47,052, at 23-36x work | A design that can resolve sub-50k effects, or a mechanism predicting an effect above the floor |
| Stacking reveal sampling on top of the fourth ply | −41,950 at M=2 (95% upper bound +17,541), 4.07x work | A wider dose is tested; only 28.6% joint coverage was affordable at depth 4, and the dose that worked at depth 3 was 85.7% |
| Pricing the same-wave double hit on a solid gray (reveal construction) as a leaf term | Corpus: partial r −0.044 with remaining life, yet 60% of live setups uncollected; in play, +300 changed <1% of decisions (rarity null), +900 changed 2.08% and scored +3,204 over 256 fresh paired games (LB −26,860, opposite halves) with reveals/move flat at 1.152 vs 1.154 | A term that raises reveals per move at all; or a per-root counterfactual showing the uncollected setups were worth collecting |
| Harsher terminal (death) utility | Parameter saturated; play is identical past the current value | Never, for this parameterization |
| A leaf-cost NNUE student reproducing D4's within-root ordering from successor-closed D4 values | Top-1 0.296/0.301 by half-fold (5 init seeds, best 0.31), below the 3.4M CNN's 0.375 and exact D1's 0.486; pairwise 0.63, regret 0.34 against gates 0.60/0.78/0.13 (RS-20260823T194142Z-946e3cd1) | A different architecture class at leaf cost, e.g. a cross-sibling set ranker; the self-play loop's leaf form stays blocked and its redesign is root-prior shaped |
| Optimistic states with fair labels (H-pool, the salvageable core of the oracle curriculum) | Stage D0: tau = −0.959 [−3.069, −0.390] with a degenerate denominator (63/64 oracle games hit the 500-move cap; ~95% of remainders censored at horizon 25); fair-value numerator only +0.263 moves; at oracle-visited roots the oracle's column is fair-top-1 76.6% vs fair D4's 81.4% (RS-20260823T205143Z-ead14c9d) | A D0 rerun with an uncapped horizon shows tau ≥ 0.25 and the oracle's action at least matching D4's under fair futures |
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
