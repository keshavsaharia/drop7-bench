# Research Status

## August 2026

The best dependable policy found so far is [fair depth-4
expectimax](/approaches/fair-expectimax) — a four-move look-ahead that treats
the game's luck honestly. Even with its best chance model, its measured
means sit under half of that target. This page shows where the gap is, what has
been measured, and which directions the evidence has closed.

How score responds to search depth, under each of the two chance models:

```figure
score-vs-depth
caption: Mean score against search depth under the five-stratum and seven-stratum chance models. Hover or focus a point for its value, bounds, cohort size and source record.
```

The same factorial as paired contrasts, which is the form the conclusions rest on:

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

- **The fourth ply is worth a large, significant gain — but only with an exact
  chance model.** With the approximate five-stratum model the same fourth ply
  is worth slightly less than nothing. Chance resolution does not merely add
  points; it changes the sign of the depth gradient.
- **The fifth ply is not measurable by this design.** Both depth-5 contrasts
  sit far inside their own detection floors, and the seven-stratum estimate
  **changed sign** when the cohort grew from 16 games to 32, so the supported
  statement is that any depth-5 effect is smaller than a 64-game paired cohort
  can resolve — not that it is zero. What can be said is economic: even the
  optimistic estimate buys its points at 35.6x the work per move.
- **A 64-game paired cohort in this game cannot see an effect below roughly
  50,000 points.** Every null result in the depth factorial is below its own
  detection floor and is therefore a non-measurement rather than evidence of no
  effect
  ([`RS-20260821T205102Z-d89df4b5`](../../research/results/RS-20260821T205102Z-d89df4b5.json)).
  **Choose experiments whose predicted effect exceeds the floor, or find a
  lower-variance estimator than complete games.**

A fourth day, 2026-08-23, went to learned models and label economics — the
reveal-construction probe in live play, the optimistic-state D0 gate, the
leaf-cost NNUE student C0, and the P-SOL G0 label-semantics guardrail — and
closed each of those four directions as tested; the [research log for that
day](../../web/content/log/2026-08-23.mdx) tells the story, and the
closed-directions table below carries the headline numbers.

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

The failure mode that recurs through eras 4-7 is easier drawn than described:

```diagram
diagram-sibling-extrapolation
caption: The era-4 failure mode: training labels the played action's successor, deployment asks the model to rank all seven siblings — six of which it never observed equally.
```

And every learned evaluator's ranking quality, against the exact search it tried to reproduce:

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

The substitution is visible as a crossed pair of lines:

```figure
depth-chance-factorial
caption: Mean score by search depth and chance resolution on the shared cohort. The sign of the depth gradient flips with the stratum count.
```

And the budget view of the same arms shows why neither axis wins on its own:

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

Each contrast of the depth factorial, drawn against the smallest effect its cohort could have seen:

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

The screen and confirmation deltas behind these closures, with their recorded bounds:

```figure
screen-deltas-with-bounds
caption: The score-valued closures at a glance: every screen and confirmation paired delta with its recorded 95% bound. Whiskers are absent, not zero, where a record carries no upper bound.
```

The closures themselves, one dot per direction, each drawn against the detection floor its record states — the geometric difference between "rejected" and "not measurable":

```figure
closed-directions-map
caption: Every closed direction whose headline is a points number, one row each, with its recorded bound and, where the record states one, its detection floor. A dot whose magnitude sits inside its floor is a non-measurement, not a rejection. Hover a marker for the recorded note; the non-score closures are quoted verbatim in the figure's notes.
```

| Direction | Result | Reopens if |
| --- | --- | --- |
| Search depth beyond four plies | Not measurable: +23,367 (7 strata, n=32) and −8,624 (5 strata, n=64), both inside their detection floors ([RS-20260821T205102Z-d89df4b5](../../research/results/RS-20260821T205102Z-d89df4b5.json)) | A design that can resolve sub-50k effects, or a mechanism predicting an effect above the floor |
| Stacking reveal sampling on top of the fourth ply | −41,950 at M=2 ([RS-20260821T192140Z-189fe392](../../research/results/RS-20260821T192140Z-189fe392.json)) | A wider dose is tested; only 28.6% joint coverage was affordable at depth 4, and the dose that worked at depth 3 was 85.7% |
| Pricing the same-wave double hit on a solid gray (reveal construction) as a leaf term | Corpus: partial r −0.044 with 60% of live setups uncollected; in play, +3,204 over 256 fresh paired games, inside its floor ([RS-20260823T131226Z-16564ed9](../../research/results/RS-20260823T131226Z-16564ed9.json)) | A term that raises reveals per move at all; or a per-root counterfactual showing the uncollected setups were worth collecting |
| Harsher terminal (death) utility | Saturated: byte-identical play at every magnitude past the current value ([finding-04](../exploratory/finding-04-terminal-utility-saturated.md)) | Never, for this parameterization |
| A leaf-cost NNUE student reproducing D4's within-root ordering from successor-closed D4 values | Top-1 0.296/0.301 by half-fold against the 0.60 gate ([RS-20260823T194142Z-946e3cd1](../../research/results/RS-20260823T194142Z-946e3cd1.json)) | A different architecture class at leaf cost, e.g. a cross-sibling set ranker; the self-play loop's leaf form stays blocked and its redesign is root-prior shaped |
| Optimistic states with fair labels (H-pool, the salvageable core of the oracle curriculum) | Stage D0: tau = −0.959 [−3.069, −0.390], with a degenerate denominator — 63/64 oracle games hit the 500-move cap ([RS-20260823T205143Z-ead14c9d](../../research/results/RS-20260823T205143Z-ead14c9d.json)) | A D0 rerun with an uncapped horizon shows tau ≥ 0.25 and the oracle's action at least matching D4's under fair futures |
| Cheap M=1 continuation labels standing in for D3 N7M6 label semantics (P-SOL G0) | Guardrail kill: within-root orderings agree at only mean tau 0.370 (LB95 0.283) on 6 CRN-matched roots; the divergence is the reveal quadrature, not the engine ([RS-20260823T225753Z-0fbd48c3](../../research/results/RS-20260823T225753Z-0fbd48c3.json)) | **Reopened 2026-08-24**: E-FAST-M6 passed every equivalence gate (RS-20260824T010000Z-8f3e9b4f) at a realised 5.6x speedup (0.177 CPU-s/move in continuation duty), so a powered M=6 guardrail is now affordable; a full M=6 label corpus still costs thousands of CPU-hours and needs a P-SOL-3 design or a scale-out lease |
| 25-move rollout veto | −21,887 points per veto ([finding-03](../exploratory/finding-03-rollout-veto-17k.md)) | A lower-variance rollout estimator |
| Refitting the leaf toward achievable clear rate | Monotone loss across six arms; fully-fitted vector −237,182 ([finding-14](../exploratory/finding-14-leaf-reweight.md)) | A refit against remaining lifetime rather than achievable clears |
| Direct action override by the compact afterstate model | Override gate failed narrowly, and full training was harmful in one half-fold ([RS-20260820T184500Z-63c0a8e2](../../research/results/RS-20260820T184500Z-63c0a8e2.json), [RS-20260821T094500Z-1a7e3c55](../../research/results/RS-20260821T094500Z-1a7e3c55.json)) | A materially larger student, or a stronger teacher than D1-continuation |
| Compact afterstate model reproducing D4's ordering | Top-1 0.375 against a 0.60 gate; exact D1 scores 0.486 ([RS-20260821T104500Z-77d21e90](../../research/results/RS-20260821T104500Z-77d21e90.json)) | A materially larger model; the claim is only established for compact evaluators |
| The `suite-h9-v1` scenario benchmark as a strength measure | Ranked policies backwards, Spearman −0.257 ([finding-10](../exploratory/finding-10-suite-validation.md)) | A validated longer horizon; it remains usable as a diagnostic |
| A depth-5-distilled NNUE leaf refined by whole-game evolution inside the depth-3 search | Rejected at this budget: −106,964 paired on 64 held-out games (LB −146,580, floor 38,357), 14-0-50; the training-signal falsifier failed (0 of the last 10 generations above the fair control). Evolution did beat its own warm start by +35,375 (LB +16,899) ([RS-20260903T025751Z-6577b33e](../../research/results/RS-20260903T025751Z-6577b33e.json)) | A warm start that holds the teacher's ordering (the 0.441 top-1 probe is near the zero-leaf level), a larger teacher corpus than the 177 games the slow depth-5 teacher produced, or a continuation from the generation-60 population to see where the still-rising curve plateaus |
| The same leaf evolved 150 generations further with annealed mutation, until a preregistered plateau rule stopped it | Rejected at this budget: −68,441 paired on 64 fresh held-out games (LB −112,090, floor 43,193), 25-0-39; the continuation did beat the first run's candidate out of sample (+36,278, LB +9,085) and the curve levelled about 90,000 paired points below the fair control ([RS-20260903T163321Z-733076b5](../../research/results/RS-20260903T163321Z-733076b5.json)) | A constant-sigma continuation from the same population to separate the plateau from the annealing; a warm start that holds the teacher's ordering; more games per candidate as sigma shrinks, so selection is not steered by noise |

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
