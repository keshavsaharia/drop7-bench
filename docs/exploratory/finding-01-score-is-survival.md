# Finding 01 — Hardcore score is survival time, and survival is a flow-balance problem

**Status:** exploratory, evidence tier `development` (64 paired whole games on a
fresh exploratory lease). Reproduced in this checkout on 2026-08-20.
**Namespace:** `approaches/lifetime-objective/score-decomposition`, run
`runs/RUN-A51D-d4/`, seed lease `SEEDLEASE-A51D`.
**Nothing in `docs/research/` was modified by this work.**

## Summary

Two facts, both measured rather than assumed:

1. **Score is lifetime.** Across 64 fair-D4 games the correlation between final
   score and moves survived is **r = 0.9995**. 94.3% of all points came from the
   flat 17,000-point row-rise bonus, 5.7% from chain waves, and **0.0% from
   board clears — there were none in 64 games.** Points per move was 3,423,
   against a theoretical ceiling of 17,000 / 5 = 3,400 from rises alone.

2. **Lifetime is a conservation law.** Every five-move cycle inserts
   5 placed discs + 7 risen covered discs = 12 discs onto a 49-cell board. A
   policy therefore survives indefinitely only if it sustains

   - **≥ 2.400 numbered clears per move** (12 / 5), and
   - **≥ 1.400 covered reveals per move** (7 / 5).

   Fair D4 sustains **1.973 clears** and **1.090 reveals** per move. It is
   running a structural deficit of 18% and 22%. That deficit, not chain design,
   is what ends its games.

The million-point target is consequently a lifetime target: at 3,400 points per
move a 1,000,000 mean requires a **mean lifetime near 294 moves**, versus the
94.06 measured here. That is a 3.1× survival problem.

## Measured reference cohort

Fair D4, unmodified frozen source, 64 games, seeds `0xa51d0000`–`0xa51d003f`,
2,000-move cap, 32 threads, 408 s wall.

| Metric | Value |
| --- | ---: |
| Mean score | 321,991.7 |
| Median score | 266,282 |
| Q25 score | 188,701 |
| Min / max score | 104,731 / 1,017,234 |
| Score standard deviation | 187,502 |
| Mean moves | 94.06 |
| Median moves | 80.0 |
| Min / max moves | 35 / 280 |
| Censored games | 0 |
| Rises per game | 17.86 |
| Board clears per game | 0.000 |
| Points per move | 3,423.2 |
| Numbered clears per move | 1.973 |
| Covered reveals per move | 1.090 |
| Score-identity violations | 0 / 64 |

This is consistent with, and independently reproduces, the ledger-recorded
figure of 308,295.578 points at 90.031 moves over 64 games. It is a *fresh-seed*
reproduction, not a replay of the historical cohort.

### Score decomposition

The engine's score is exactly

```
score = 17,000 x rises + 70,000 x boardClears + Σ_waves popperCount x floor(7 x depth^2.5)
```

The runner asserts this identity on every game; it held for 64/64.

| Source | Share of all points |
| --- | ---: |
| Row-rise level bonus | **94.29%** |
| Chain waves | 5.71% |
| Board clears | 0.00% |

The same decomposition next to the policies later measured in
[`finding-06`](finding-06-flow-ceiling.md) §4 puts this cohort in context:

```figure
score-composition
caption: Where each policy's points come from. Every survival-oriented policy earns roughly nine-tenths of its score from the flat row-rise bonus; the one planner that escapes that split — the clairvoyant score-maximizer — pays for its chains with its lifetime.
```

Chain-wave points are small by construction: a wave awards 7 points per disc at
depth 1, 39 at depth 2, 109 at depth 3, 224 at depth 4, 391 at depth 5. A
five-deep, seven-disc wave — a spectacular play — is worth 2,737 points, or
**0.16 of one row rise**. The 70,000-point board clear is worth 20.6 moves of
survival but was never once achieved in 64 games of the strongest known policy.

## Flow balance predicts lifetime

Sorting the 64 games by lifetime and grouping into quartiles:

| Group | n | Mean moves | Mean score | Clears/move | Reveals/move |
| --- | ---: | ---: | ---: | ---: | ---: |
| Shortest 16 | 16 | 44.7 | 138,762 | 1.471 | 0.732 |
| Second 16 | 16 | 70.0 | 232,515 | 1.812 | 0.975 |
| Third 16 | 16 | 99.5 | 341,310 | 1.989 | 1.099 |
| Longest 16 | 16 | 162.1 | 575,380 | 2.147 | 1.216 |
| **Steady-state requirement** | | **∞** | | **2.400** | **1.400** |

Correlations over whole games: clears/move vs. moves **r = 0.804**, reveals/move
vs. moves **r = 0.770**, score vs. moves **r = 0.9995**.

The single million-point game in the cohort (1,017,234 points, 280 moves,
seed `0xa51d0033`) reached 2.229 clears and 1.254 reveals per move — closer to
the balance point than any other game, and still below it. Its 82,234 chain
points are 8.1% of its score; the other 91.9% is survival.

## Why this matters for the research program

**It reframes the objective.** The program's documents describe three competing
jobs — stay alive, open the board, prepare chains — and much of the historical
work went into chain structure and score prediction. The measurement says the
third job is worth 5.7% of the score and only matters insofar as clearing discs
*is* how you stay alive. There is one job with two names.

**It explains a documented failure pattern.** `docs/research/status.md` records
that learned value models repeatedly fit visited states and then failed to rank
unplayed siblings. Part of that difficulty is target conditioning: raw score is
a 17,000-quantized, heavy-tailed variable with a standard deviation of 187,502
in this cohort — 58% of its own mean. Remaining lifetime is the same quantity
divided by 3,400, with no heavy tail and no quantization. Per-move flow
(clears and reveals) is denser still: it yields one labelled observation per
move rather than one per game, with a bounded range.

**It gives the 2.4 / 1.4 diagnostic a derivation.** `status.md` describes
"roughly 2.4 numbered clears and 1.4 reveals per move" as an empirical region
"associated with stable long games" and warns they are "diagnostic targets from
limited runs, not proven universal thresholds." They are neither empirical nor
approximate: they are 12/5 and 7/5, the exact disc-conservation requirement of
the five-move rise cadence on a 49-cell board. They are necessary conditions for
unbounded survival, and this cohort confirms the engine obeys them.

**It bounds what search can do.** Rises occur every five moves; a depth-4 search
sees at most one rise boundary and cannot observe the accumulation of a flow
deficit over the eight-plus cycles that separate a 90-move game from a 294-move
one. The deficit is a slow variable. This is a direct, quantitative argument for
a learned long-horizon evaluator rather than deeper exact search, and it says
what that evaluator should predict.

## Trivial-policy control cohort

To separate "value of search" from "value of merely being alive", three
deliberately weak policies were run on the same 64 seeds:

| Policy | Mean score | Mean moves | Level share | Board clears |
| --- | ---: | ---: | ---: | ---: |
| Center-first | 57,233 | 21.64 | 98.9% | 0 |
| Random legal | 80,778 | 28.44 | 98.7% | 0 |
| Lowest column | 100,050 | 33.28 | 98.5% | 0 |
| **Fair D4** | **321,992** | **94.06** | **94.3%** | **0** |

Fair D4 buys 3.2× the lifetime of uniformly random legal play. Reaching the
target requires buying a further 3.1× on top of that.

## Limitations

- 64 games on one exploratory lease. The score standard deviation is 58% of the
  mean, so the mean is known to roughly ±46,000 at one standard error.
- The flow-balance requirement is a necessary condition for indefinite survival,
  not a sufficient one; a policy can meet it locally and still die by stacking a
  single column into the top row.
- Board clears were absent here, so this cohort says nothing about whether a
  clear-seeking policy could change the economics. At 70,000 points a clear is
  worth 20.6 moves, so a policy achieving one clear per 20 moves would double
  the points-per-move rate. Nothing in this repository has measured whether that
  is reachable; that is a separate open question, not a refuted one.
- These figures describe the repository's simulator. See
  [`audit-01-engine-fidelity.md`](audit-01-engine-fidelity.md) for two
  identified rise-boundary scoring discrepancies against the cited reference
  implementation, both of which act at rise boundaries and therefore act
  directly on the quantity measured here.

## Reproduce

```sh
./approaches/lifetime-objective/score-decomposition/build.sh
./build/lifetime/decompose --policy fair-d4 --seed-start 0xa51d0000 \
    --games 64 --max-moves 2000 --threads 32 --output runs/RUN-A51D-d4/d4-standard64.json
```
