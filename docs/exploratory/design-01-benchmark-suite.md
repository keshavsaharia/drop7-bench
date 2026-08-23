# Design 01 — A fully-specified scenario benchmark for Drop7 policies

**Status:** design + implementation in progress. Nothing here is a result yet.
**Namespace:** `approaches/lifetime-objective/scenario/`, leases
`SEEDLEASE-A51D-SCEN` (`0xa51dc000`–`0xa51dffff`).

## The problem this solves

Today a policy can only be compared by playing whole games. That is the correct
statistical unit for a qualification claim, but it is a terrible instrument for
iteration:

- Fair D4's score standard deviation over 64 games is 187,502 — **58% of its own
  mean**. Detecting a 5% improvement needs hundreds of paired games.
- One long game dominates a small cohort. The historical record contains at
  least two selection decisions that were made on a mean that was mostly one
  pre-observed game.
- Board clears are worth 70,000 points each — 20.6 moves of survival — and fair
  D4 achieved **zero in 64 games**. Nothing in this repository has ever measured
  whether a board clear is reachable at all. A whole-game cohort cannot answer
  that, because it never produces the event.

A fixed suite of fully-specified positions with known ground truth replaces a
noisy population estimate with a deterministic exam.

## What a scenario is

Everything that the base engine leaves to chance is fixed in advance:

```
Scenario {
  board[49]          0 empty | 1..7 numbered | 8 solid cover | 9 cracked cover
  latent[49]         the hidden value 1..7 of every covered cell
  movesRemaining     1..5 until the next row rise
  horizon H          how many moves the scenario is scored over
  discTape[H]        the exact numbered discs the player will receive
  riseLatent[R][7]   hidden values of every future risen row
  id                 content hash over all of the above
}
```

With the tape fixed, Drop7 becomes a **deterministic single-player puzzle**, so
the optimal play is computable exactly by exhaustive search rather than
estimated.

### One engine change this forces, and why it is worth it

In the current engine a covered disc has **no latent value**: its number is
drawn from the reveal RNG at the moment it is revealed
(`engine.hpp:213-268`). There is therefore no "hidden board" to specify. The
scenario engine adds one by carrying a parallel `latent[49]` array through the
same gravity and rise transforms as the board.

This is a genuine semantic change, and it is being handled as one:

- A **parity gate** proves the scenario engine with a *stream* reveal source is
  trajectory-identical to `playHeadlessMove` over 512+ seeds — same boards, same
  scores, same wave lists. That proves the cascade, damage, gravity, rise,
  scoring and termination logic is the shared, audited logic and not a
  re-derivation.
- The *latent* reveal source then differs only in where a revealed number comes
  from. The marginal distribution is identical (i.i.d. uniform 1..7), so a
  policy's expected performance is unchanged, but scores are not bit-comparable
  to base-engine runs and will never be reported as if they were.

As a side effect this closes an audit finding: without latent values there is no
true hidden board, which blocks reproducible oracle and teacher experiments.

## Two modes, and only one of them can prove improvement

This distinction is the whole validity of the benchmark, so it is stated first.

### Mode 1 — Puzzle mode (clairvoyant reference)

Fix the tape. Solve exactly. Report

```
normalizedRegret = (optimum - policyScore) / (optimum - referenceScore)
```

The optimum is what a player who **knows the future** achieves. A public-
information policy cannot reach it, and is not expected to. Mode 1 is therefore:

- a **difficulty labeller** — how much value is present in this position at all;
- a **golden-example generator** — the optimal principal variation is a
  ground-truth label for every position along it, including positions no fair
  policy would ever reach;
- a **relative comparator** between fair policies, where the optimum only
  supplies a common scale.

Mode 1 is *not* a fair benchmark, and a score on it is not evidence that a
policy is better at Drop7.

### Mode 2 — Position mode (the fair benchmark)

Keep the start position and its latent board. Draw **K independent disc tapes**.
Score a policy by its mean over those K tapes, with the same K tapes given to
every policy (common random numbers).

This *is* a legitimate fair metric. It preserves the information boundary — the
policy never sees the tape — while removing the two largest sources of noise in
whole-game comparison: the starting position varies, and the early game
dominates lifetime. Variance falls because the position is held fixed and the
comparison is paired at the tape level.

**Mode 2 is what proves improvement. Mode 1 labels the exam.**

## The selection trap, and the filter that avoids it

The obvious minting rule — "keep positions where shallow search does much worse
than the clairvoyant optimum" — builds a benchmark that measures clairvoyance.
A public policy *should* fail those positions; there is no legal way to recover
the gap, so improving on them may be unachievable or, worse, achievable only by
overfitting to the suite.

Every minted candidate is therefore labelled with two different gaps:

| Label | Definition | Meaning |
| --- | --- | --- |
| `clairvoyantGap` | optimum − best shallow policy, on the fixed tape | how much value knowing the future is worth here |
| `fairGap` | (deep fair policy − shallow fair policy), averaged over K tapes | how much value *legal thinking* is worth here |
| `luck-only` | large `clairvoyantGap`, `fairGap` ≈ 0 | excluded from the fair suite; kept for oracle research |
| `fairly-hard` | large `fairGap` | **the fair suite** |

The suite that gets used for policy comparison is the `fairly-hard` set. The
`luck-only` set is retained separately because it measures the information gap —
the ceiling that no public policy can pass — which is a useful quantity in its
own right and is exactly what a teacher/oracle experiment needs.

## Required validation before this benchmark is trusted

A fast proxy metric is only useful if it predicts the thing it proxies for. Two
checks must pass, and both are preregistered here:

1. **Rank correlation with whole games.** Take a set of policies whose whole-game
   means are already known on a common cohort (center-first, random legal,
   lowest column, fair D1/D2/D3/D4, and any candidate). Score them on the fair
   suite. Report Spearman correlation between suite rank and whole-game mean
   rank. If the suite cannot reproduce the known ordering of policies that
   differ by 3-4x in whole-game mean, it is not measuring policy strength.
2. **No suite-overfitting channel.** The suite must be split into a public
   development half and a sealed half fixed before any tuning. A candidate tuned
   on the development half must hold its margin on the sealed half.

Until check 1 passes with a reported number, **the suite is a diagnostic and
cannot be cited as evidence of policy strength.**

## Why board clears deserve their own track

At 70,000 points a board clear is worth 20.6 moves of survival — more than four
row rises. Fair D4 produced none in 64 games. Two questions are currently
unanswerable and become answerable here:

- With perfect knowledge of the tape, **how often is a board clear reachable
  from a typical mid-game position?** The exact solver answers this directly.
- If it is frequently reachable but never achieved, that is a large, specific,
  quantified failure of the current policy, and it points at a mechanism rather
  than a vague "play better."

If it turns out clears are essentially unreachable even clairvoyantly, that is
also a valuable result: it would close off a 70,000-point line of hope and
confirm that lifetime is the only lever.

## What this does not replace

The frozen qualification protocol is unchanged. A whole game remains the
statistical unit for any million-point claim, protected and final cohorts stay
sealed, and no suite result can substitute for the 256-game gates. This is a
screening and diagnosis instrument that makes the expensive gates worth
spending.
