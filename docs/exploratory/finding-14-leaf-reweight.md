# Finding 14 — reweighting the fair leaf toward achievable clears

**Status:** exploratory, evidence tier `development`. **Negative result**, with a
measured mechanism and a dose-response across six arms. Built and measured in
this checkout on 2026-08-20/21.

**Namespace:** `approaches/lifetime-objective/leaf-reweight/`, run
`runs/RUN-A527-LEAFW/`, seed lease `SEEDLEASE-A52-LEAFW` =
`0xa5278000`–`0xa527ffff` for every tuning, CHECK and screening game.
Headline results are on the shared evaluation cohort
`0xa51d1000`–`0xa51d103f` (64 paired games), which was never tuned on.

**No existing file was created or modified by this work.** New files appear only
under `approaches/lifetime-objective/leaf-reweight/`, `docs/exploratory/`,
`runs/RUN-A527-LEAFW/` and `build/lifetime/`.

## Why this exists

[`finding-10` Addendum A](finding-10-suite-validation.md#addendum-a--task-2-what-predicts-an-achievable-clear-and-what-the-frozen-leaf-does-with-it)
labelled 1,024 public positions with their exact 8-move achievable clear
optimum, averaged over independent completions of both the hidden board and the
future, and asked what predicts that label:

| model | held-out R² |
| --- | ---: |
| occupancy only | 0.187 |
| the frozen `fairLeaf(state)` scalar | 0.396 |
| the leaf's own 19 features, freely reweighted | 0.734 |
| all 53 candidate structural properties | 0.753 |

Reweighting recovers 95% of all the signal any structural property can supply,
and the cosine between the frozen weight direction and the predictive direction
is **+0.141**. The conclusion drawn there was that the leaf is not missing
information — its weights point almost orthogonally to it.

That is a statement about a diagnostic label. This document tests the different
and harder claim the programme actually needs: that a leaf which predicts
achievable clears better is a better leaf *to search with*.

**It is not.** Over 64 paired whole games at depth 4 with seven chance strata,
rotating the leaf toward the fitted direction degrades score, lifetime, numbered
clears per move, covered reveals per move and mean occupancy **monotonically**.
The full refit loses **237,182 points** (one-sided 95% bound −290,406, 7-0-57).
The best arm — setting `kRoughnessWeight` from 0 to +560, Addendum A's
first-ranked and cheapest candidate — is an exact tie at **+627** (95% bound
−73,325, 31-0-33). The frozen weight vector is the best of the six.

| claim | evidence |
| --- | --- |
| **The weights become data and nothing else changes** | 936,612 real boards, **zero differing bits** against `frozen::fairLeaf`; 481 decisions, zero differing columns and **zero differing work counts**; the comparator reproduced game-for-game from a different binary |
| **The reweighting fails, monotonically** | cosine to the fitted direction +0.176 → +0.181 → +0.231 → +0.255 → +0.767 → +1.000; mean score 398,498 → 399,125 → 330,779 → 323,284 → 291,391 → 161,316. Five metrics, six arms, one direction |
| **The mechanism is the flow balance, and it is measured** | clears per move fall 2.0571 → 1.6019 against the 2.400 disc conservation requires, and mean occupancy rises 23.15 → 28.34 cells. **A leaf that predicts where clears are available makes a policy that achieves fewer of them** |
| **Risk 2 did not happen; the tail got better, the head got worse** | every arm improves most of the comparator's eight worst games and three improve the cohort minimum, while the maximum falls 1,341,287 → 342,164 and the standard deviation 254,414 → 54,703 |
| **Risk 3 was real and was corrected for first** | the synthetic-position fit and the fair-play fit are only **+0.310** apart in direction, and Addendum A's headline 0.396 for the frozen scalar is **0.618** on fair-play positions and **0.007** on synthetic ones. Every arm here was built from the fair-play-only refit — the strongest form of the hypothesis — and it still lost |
| **The cheap proxy failed again, as warned** | at depth 3 `roughness` +560 measured **−58,570** (95% bound −109,286); at depth 4 the same arm measures **+627**. The depth-3 map inverted the sign |

## 1. CHECK gate — the weights become data and nothing else changes

The candidate is `approaches/lifetime-objective/leaf-reweight/search.cpp`. Its
search driver is a copy of
`approaches/lifetime-objective/risk-calibration/search.cpp`, which
[`finding-05`](finding-05-chance-strata.md) already proved decision-identical to
the frozen reference. The only change is that `frozen::fairLeaf(state)` is
replaced by a local dot product over `frozen::extractFairFeatures(state)` with
the nineteen weights held in a struct. The feature extractor itself is the
unmodified frozen one, called by name, so the *only* degree of freedom this
program introduces is the weight vector. The accumulation order of the dot
product is preserved term for term, because floating-point addition is not
associative and bit-identity depends on it.

Five gates, all passed, all on the tuning lease:

| gate | what it proves | sample | result |
| --- | --- | ---: | --- |
| `--leaf-check --depth 3 --chance-samples 7` | raw `uint64_t` bit patterns of the parameterized leaf against `frozen::fairLeaf` on real boards | **883,564 boards** from 24 fair-play games and their one- and two-ply chance expansions | **0 bit mismatches**; leaf range [−201,610.37, +12,181.75], mean −25,003.86 |
| `--leaf-check --depth 4 --chance-samples 7` | the same, on boards drawn from the actual evaluation configuration | **53,048 boards** | **0 bit mismatches**; leaf range [−58,997.37, +7,985.63], mean −9,308.94 |
| `--reference-parity` | the driver at default parameters selects the same column as the unmodified `ref::chooseDepth4Action` | 4 games, **240 moves** | **0 mismatches** |
| `--self-parity --depth 4 --chance-samples 5 --max-work 3200000` | weights-as-data against weights-as-constants inside one process: same columns **and** same cumulative work per decision | 3 games, **175 moves**, 235,894,998 total work | **0 column mismatches, 0 work mismatches** |
| `--self-parity --depth 4 --chance-samples 7 --max-work 16000000` | the same at the exact evaluation configuration | 3 games, **66 moves**, 322,575,988 total work | **0 column mismatches, 0 work mismatches** |

**936,612 real boards compared, zero differing bits.** 481 decisions compared,
zero differing columns and zero differing work counts.

A fifth, cross-binary gate closes the loop on the comparator. The frozen-weight
arm at the evaluation configuration is
`runs/RUN-A51D-s7confirm/fresh-s7.json`, produced by a **different binary**
(`approaches/lifetime-objective/risk-calibration`) on the same 64 ordered seeds
at depth 4, seven strata, 16,000,000 work. Re-playing the first eight of those
games with this binary at frozen weights reproduces them exactly:

> **8 games × 11 recorded fields — score, moves, work, numbered clears, covered
> reveals, rises, level points, chain points, maximum chain depth, mean occupied
> cells, censor flag — 0 mismatches.**

Games are deterministic given a seed and a policy, so `fresh-s7.json` is used as
the paired comparator for all 64 games rather than spending another 3.8 hours
recomputing a file this binary provably produces. That file's headline figures
are the ones the coordinator supplied as reference points and they match
exactly: 398,498 mean, 114.66 mean moves, 2.0571 clears per move, 1.1549 reveals
per move, 23.15 mean occupied cells.

The work-count identity is the part that matters beyond column identity: it
shows the cache, the iterative-deepening ladder and the work budget all behave
identically, so a work-limited decision degrades the same way. Nothing past this
gate was run before it passed.

## 2. What the fitted direction is, once it is an actual leaf

Addendum A reports the fitted direction in *leaf units per one sigma of each
feature, normalised to unit length*. That is a direction, not a leaf. A search
needs raw weights whose scale is commensurate with the −1,000,000 terminal
utility and with the 1.0 coefficient on immediate score, because — per
[`audit-02`](audit-02-fair-d4.md) §5 — the ranking is effectively lexicographic:
one sampled death costs 18×–920× the entire legitimate discrimination range
between actions, so the search minimises modelled four-ply death first and
maximises the leaf second. Shrinking or growing the leaf therefore changes how
much modelled death risk the search will accept. That is a **different**
experiment from changing the direction the leaf points in.

`refit.py` holds that second axis fixed:

1. fit standardised OLS of the target on the leaf's own 19 features, on the same
   60% content-hash training split Addendum A used;
2. normalise the fitted per-sigma vector to unit length → `u`, and the frozen
   vector expressed per sigma to unit length → `f`;
3. interpolate, `d(α) = unit((1 − α)·f + α·u)`;
4. convert back to raw weights `w_i = d_i · S / σ_i`, choosing `S` so the
   resulting leaf has the **same standard deviation** over the corpus as the
   frozen `leaf_value`; and
5. add a bias so it has the **same mean**.

Every Tier-2 arm therefore differs from the frozen leaf in direction only. The
bias is a nineteen-term dot product's twentieth parameter; it defaults to exactly
zero, so the CHECK gate is unaffected.

The refit reproduces Addendum A exactly, which is the check that this pipeline is
reading the same thing: held-out R² **0.7342** for the reweighted 19 features,
**0.3962** for the frozen scalar, cosine **+0.1406**.

Tier 1's anchors come from the same rescaling applied to a single term: the raw
weight a same-scale fitted leaf would use for that feature alone.

| term | frozen | fitted per σ (unit) | implied raw weight, `all` fit | implied raw weight, fair-play fit |
| --- | ---: | ---: | ---: | ---: |
| `roughness` | **0** | +0.0965 | +561 | +562 |
| `solid_exposure` | +40 | +0.1836 | +3,744 | +4,934 |
| `cracked_exposure` | +100 | +0.1324 | +2,436 | +2,868 |
| `numbered_cells` | −18 | +0.6013 | +1,717 | +1,048 |

Substituting all four at once moves the leaf's standard deviation from 32,762 to
34,526 on fair-play positions — a 5% change in scale, so the Tier-1 arms are
close to pure direction changes as well.

## 3. Risk 3 first: how much of the fitted direction is a synthetic artifact

Addendum A A.6 warns that its added cover-geometry block earns its keep off the
fair-play manifold: +0.054 R² on synthetic positions but +0.005 on fair-play
positions, and −0.015 on `lowest-column` positions for the gap target. The same
question applies to the reweighting itself, and it has to be answered before any
gameplay number is interpreted. `tier1.py` refits separately by position origin
and scores every fit on every origin's held-out positions.

**Held-out R² on `achievableClears`, fit on the row origin, scored on the column
origin.** No position both trains and scores.

| fit on \ score on | all | fair-play | played | synthetic |
| --- | ---: | ---: | ---: | ---: |
| **all** (Addendum A's fit) | +0.734 | +0.741 | +0.723 | +0.650 |
| **fair-play only** | +0.679 | **+0.786** | +0.780 | +0.431 |
| **played only** (fair + `lowest-column`) | +0.667 | +0.783 | +0.780 | +0.400 |
| **synthetic only** | +0.476 | **+0.214** | +0.201 | +0.629 |
| **frozen `fairLeaf` scalar** | **+0.396** | **+0.618** | +0.616 | **+0.007** |

Two things in that table change how Addendum A's headline should be read.

**First, the headline 0.396 is a mixture number dominated by the synthetic
block.** On synthetic positions the frozen leaf scalar explains **0.7% of the
variance**; on the positions a fair policy actually visits it explains **61.8%**.
The gap the reweighting has to close on the fair-play manifold is +0.168
(0.618 → 0.786), not +0.338. The leaf is a much better predictor of achievable
clears than 0.396 suggests, *where it is actually used*.

**Second, the fitted directions genuinely disagree with each other by origin.**

| cosine | all | fair-play | played | synthetic | frozen |
| --- | ---: | ---: | ---: | ---: | ---: |
| **all** | +1.000 | +0.758 | +0.831 | +0.820 | +0.141 |
| **fair-play** | +0.758 | +1.000 | +0.970 | **+0.310** | +0.176 |
| **played** | +0.831 | +0.970 | +1.000 | +0.456 | +0.220 |
| **synthetic** | +0.820 | +0.310 | +0.456 | +1.000 | +0.248 |

The fair-play fit and the synthetic fit point in directions only **+0.310** apart
— barely more aligned with each other than either is with the frozen leaf. The
`all` fit sits between them at +0.758 and +0.820, so it is a blend of two
different answers, weighted 448/1024 toward synthetic positions. The synthetic
fit transfers to fair-play held-out positions at R² **+0.214**, worse than the
frozen leaf's own +0.618.

Consequences carried into the arm design:

- the **fair-play-only refit is the primary Tier-2 vector**, not Addendum A's
  `all` fit, because it is the one whose training distribution matches where the
  leaf is evaluated;
- the fair-play fit is also more aligned with the frozen leaf (+0.176 vs +0.141),
  so "almost orthogonal" is slightly overstated on the manifold that matters; and
- `numbered_cells` — Addendum A's most striking sign flip, +0.601 per σ and "the
  largest fitted term" — is **+0.147 per σ on fair-play positions**. Most of its
  size is a synthetic-generation artifact: the synthetic generator sweeps
  occupancy deliberately, giving `numbered_cells` a corpus σ of 6.04 against 2.30
  on fair-play positions. The sign is still positive both univariately and
  conditionally on fair-play positions, so the *direction* of the diagnosis
  survives; its *magnitude* does not.

Risk 3 is therefore not hypothetical, and it is not fully avoidable either: even
the fair-play block is 384 positions harvested from `d2s5` play, not `d4s7` play.

## 4. The screening proxy, and why it is not used as a gate

Depth 3 at seven strata costs about 1/32 the work of depth 4 at seven strata, so
it is tempting for screening. It is also exactly the "cheap proxy" gamble that
[`finding-10`](finding-10-suite-validation.md) §1–§7 just lost: a scenario
benchmark scored over nine moves ranked search configurations *backwards*,
because over a short window the score is dominated by a term that is a function
of the rise phase rather than of skill.

**The proxy cannot be validated here, and one known ordering already refutes it.**
`finding-05` measures the chance-strata effect at +101,171 (95% bound +47,457) at
depth 4 and +7,276 (95% bound −45,961, not significant) at depth 3. Depth 3
demonstrably fails to reproduce a depth-4 ordering for that factor. There is no
*leaf-weight* ordering at depth 4 already established in this repository against
which depth 3 could be validated, so no validation was possible in advance.

Depth 3 was therefore used only as a **response-surface map on the tuning lease**,
to choose magnitudes within a family, and every verdict-bearing number in §5 is a
direct depth-4 run. This is the "screen a small number of arms directly at depth 4
and say that you did" branch.

The map, on `SEEDLEASE-A52-LEAFW` seeds `0xa5279000`–`0xa527903f`, 64 paired
games, depth 3, seven strata, `--max-work 16000000`. **The run was stopped after
8 of 25 planned arms** to release the machine for the depth-4 evaluation; arms
execute in a fixed order, so what survives is a prefix — the entire `roughness`
family and the first two `solid_exposure` points.

| arm | mean | median | Q25 | min | sd | moves | clears/mv | reveals/mv | cells | Δ vs frozen | 95% lo | W-T-L |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `solid_exposure` +1,250 | 377,160 | 311,343 | 207,282 | 103,438 | 227,522 | 109.94 | 2.0542 | 1.1497 | 23.48 | **+35,988** | −25,099 | 32-1-31 |
| `solid_exposure` +2,500 | 345,750 | 283,422 | 190,728 | 104,560 | 217,775 | 101.36 | 2.0248 | 1.1300 | 24.16 | +4,577 | −53,987 | 31-0-33 |
| **frozen** | 341,172 | 281,499 | 193,363 | 120,756 | 206,054 | 99.94 | 2.0042 | 1.1119 | 23.66 | — | — | — |
| `roughness` +280 | 289,111 | 222,069 | 175,386 | 103,635 | 176,405 | 85.94 | 1.9662 | 1.0984 | 23.77 | −52,061 | −104,928 | 23-0-41 |
| `roughness` +560 | 282,602 | 251,700 | 175,514 | 88,524 | 140,514 | 83.80 | 1.9413 | 1.0705 | 24.11 | −58,570 | −109,286 | 26-0-38 |
| `roughness` +1,120 | 258,820 | 211,048 | 155,912 | 85,915 | 174,822 | 77.41 | 1.9116 | 1.0549 | 24.70 | −82,352 | −133,259 | 23-0-41 |
| `roughness` +140 | 258,724 | 230,304 | 161,646 | 104,877 | 131,033 | 77.16 | 1.9131 | 1.0531 | 24.02 | −82,449 | −128,265 | 27-2-35 |
| `roughness` +2,240 | 211,618 | 176,720 | 140,520 | 85,794 | 112,384 | 64.59 | 1.8384 | 1.0077 | 25.33 | **−129,554** | −177,323 | 15-0-49 |

Two things are worth reading off it before the depth-4 arms are seen.

**`roughness` is monotone in the wrong direction, and the mechanism is visible.**
The largest weight tested loses 129,554 points and 35 moves. Clears per move fall
monotonically 2.0042 → 1.8384, reveals per move 1.1119 → 1.0077, and mean
occupancy *rises* 23.66 → 25.33. Rewarding a rough board makes the policy worse
at the two conservation quantities that
[`finding-01`](finding-01-score-is-survival.md) shows are what ends games. This is
the opposite of the mechanism Addendum A proposed — that a rough board offers more
distinct run lengths for an arriving disc to match. Every one of the five
roughness points is negative, so this is a dose-response relationship, not a
single unlucky draw.

**`solid_exposure` is the one Tier-1 term that might be real, and its optimum is
below the fitted value.** +1,250 gains +35,988 with clears and reveals both up,
but the 95% lower bound is −25,099 and the split is 32-1-31, so this is a
direction to test at depth 4, not a result. +2,500 is already back to a tie, and
the fitted anchor is +4,934 — the fit wants roughly four times more weight than
the best point measured here.

### The proxy inverted, and §5 measures it

Both readings above are stated as depth-3 observations because §5 then falsifies
one of them outright:

| arm | depth 3, 64 games (tuning lease) | depth 4, 64 games (evaluation cohort) |
| --- | ---: | ---: |
| `roughness` +560 | **−58,570** (95% bound −109,286, 26-0-38) | **+627** (95% bound −73,325, 31-0-33) |
| frozen | reference | reference |

At depth 3, setting `kRoughnessWeight` to +560 is one of the largest losses in
the whole document — a confident, dose-responsive, mechanistically coherent
loss, with the flow rates and occupancy all moving the way a real effect would
move them. At depth 4 the same one-line change is an exact tie that slightly
*improves* both flow rates and raises the cohort minimum by 16,898.

**Nothing about the depth-3 evidence looked unreliable, and it was wrong
anyway.** This is a second, independent instance of the failure
`finding-10` §1–§7 documented — one more reason to treat a cheap surrogate for a
whole-game ranking as a hypothesis generator only, no matter how internally
consistent it looks. It also retrospectively justifies not using depth 3 as a
selection gate: had the arms been screened at depth 3, `roughness` would have
been discarded as the worst candidate rather than reported as the best of five.

It leaves `solid_exposure` +1,250 — the depth-3 best — genuinely unknown at
depth 4. It was queued and not run (§8), and it is the recommended next arm
precisely because the depth-3 estimate for it cannot be trusted in either
direction.

## 5. Results — depth 4, seven strata, the shared evaluation cohort

Cohort `0xa51d1000`–`0xa51d103f`, 64 paired whole games, 2,000-move cap, depth 4,
seven chance strata, `--max-work 16000000`, 12 threads. Comparator as in §1.

| arm | mean | median | Q25 | min | max | sd | mean moves | Q25 moves | censored | clears/mv | reveals/mv | cells | work/move |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| **frozen (comparator)** | **398,498** | 344,630 | 212,864 | 103,331 | 1,341,287 | 254,414 | 114.66 | 65.00 | 0 | 2.0571 | 1.1549 | 23.15 | 4,956,614 |
| `t1-rough-560` | 399,125 | 290,044 | 211,440 | **120,229** | **1,503,379** | 284,871 | 115.30 | 65.00 | 0 | 2.0667 | 1.1611 | 23.63 | 5,053,120 |
| `t1-exposures` | 330,779 | 279,654 | 212,309 | 103,155 | 989,981 | 174,448 | 97.39 | 65.00 | 0 | 1.9939 | 1.1025 | 24.49 | 4,861,279 |
| `t1-combined` | 323,284 | 283,871 | 205,980 | 103,382 | 977,457 | 190,684 | 94.83 | 63.75 | 0 | 1.9792 | 1.0949 | 25.42 | 4,789,546 |
| `t2-fair-a05` | 291,391 | 245,675 | 193,426 | 103,453 | 903,444 | 159,133 | 85.88 | 60.00 | 0 | 1.9370 | 1.0617 | 25.97 | 4,926,403 |
| `t2-fair-a1` | 161,316 | 155,936 | 121,614 | 102,896 | 342,164 | 54,703 | 50.75 | 40.00 | 0 | 1.6019 | 0.7919 | 28.34 | 4,576,771 |

| arm | paired Δ score | one-sided 95% lower bound | Δ moves | W-T-L | Δ Q25 | Δ min |
| --- | ---: | ---: | ---: | :---: | ---: | ---: |
| `t1-rough-560` | **+627** | −73,325 | +0.64 | 31-0-33 | −1,424 | **+16,898** |
| `t1-exposures` | −67,719 | −133,082 | −17.27 | 29-0-35 | −555 | −176 |
| `t1-combined` | −75,214 | −139,902 | −19.83 | 25-0-39 | −6,884 | +51 |
| `t2-fair-a05` | −107,107 | −169,589 | −28.78 | 22-0-42 | −19,438 | +122 |
| `t2-fair-a1` | **−237,182** | −290,406 | −63.91 | **7-0-57** | −91,250 | −435 |

Bounds are one-sided 95% percentile bootstrap over 20,000 resamples of whole
games, paired by seed, Mulberry32 seeded `0xb0075eed`. No arm was censored, no
arm recorded a score-identity violation, and no arm achieved a board clear.

**Four of five arms lose, three of them by more than their own 95% bound. The
fifth — `roughness` alone — is an exact tie: +627 points on a 254,414-point
standard deviation, 31-0-33.**

### The dose-response, which is the actual result

Express each arm's weight vector in leaf-units-per-sigma and normalise, exactly
as §2 does, then measure how far each arm has travelled from the frozen
direction toward the fitted one:

| arm | cos to frozen | cos to fitted | mean score | mean moves | clears/mv | reveals/mv | mean cells |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| frozen | +1.000 | +0.176 | 398,498 | 114.66 | 2.0571 | 1.1549 | **23.15** |
| `t1-rough-560` | +0.997 | +0.181 | **399,125** | **115.30** | **2.0667** | **1.1611** | 23.63 |
| `t1-exposures` | +0.971 | +0.231 | 330,779 | 97.39 | 1.9939 | 1.1025 | 24.49 |
| `t1-combined` | +0.958 | +0.255 | 323,284 | 94.83 | 1.9792 | 1.0949 | 25.42 |
| `t2-fair-a05` | +0.767 | +0.767 | 291,391 | 85.88 | 1.9370 | 1.0617 | 25.97 |
| `t2-fair-a1` | +0.176 | +1.000 | 161,316 | 50.75 | 1.6019 | 0.7919 | 28.34 |
| *steady-state requirement* | | | | ∞ | *2.400* | *1.400* | |
| *clairvoyant planner, `finding-07`* | | | | | *2.3601* | *1.3785* | *~18* |

The score column of that table against the rotation itself is the whole result
in one curve:

```figure
leaf-reweight-monotone
caption: Mean score against how far the leaf's weight vector has been rotated toward the fitted achievable-clear direction. The loss is monotone across all six arms: every degree of rotation toward the direction that predicts clears makes the policy worse at sustaining them.
```

**Score, lifetime, numbered clears per move, covered reveals per move and mean
occupancy are monotone in how far the leaf has been rotated toward the fitted
direction — six arms, five columns, one exception.** The exception is
`t1-rough-560`, and it is the exception that confirms the shape: setting
`kRoughnessWeight` to +560 moves the leaf's direction by 0.005 of cosine, which
is the smallest dose in the table, and it produces the smallest effect in the
table. Roughness is the only feature the frozen leaf multiplies by exactly zero,
but in σ-scaled direction terms it is a small term, and giving it the weight the
fit wants barely rotates the leaf at all.

This is the sharpest available statement of the result, and it is not a null.
The fitted direction predicts the exactly-computed 8-move achievable clear
optimum far better than the frozen leaf does — 0.786 against 0.618 held-out R²
on fair-play positions, and 0.734 against 0.396 on Addendum A's mixed corpus.
Rotating the search's leaf toward it makes the policy **worse at actually
clearing discs**: 2.0571 → 1.6019 clears per move, a 22% fall, moving *away* from
the 2.400 that disc conservation requires, and moving mean occupancy from 23.15
to 28.34 cells and away from the ~18 at which `finding-07`'s clairvoyant planner
operates. Predicting where clears are available and being able to sustain them
are close to opposite objectives for a search leaf.

Work per move stays within 8% of the comparator across every arm (4.58M–5.05M
against 4.96M), so no arm silently degraded to a shallower completed depth and
none of this is a work-limit artifact.

### Tier 1, term by term

- **`roughness` 0 → +560: a tie, +627 (95% bound −73,325), 31-0-33.** Addendum A
  A.7 ranked this first — "changing one constant is the entire experiment", "the
  cheapest experiment in the repository". It is not an improvement, but it is
  also the only arm that is not a loss, and it is the only arm that improves both
  flow rates (2.0667 clears, 1.1611 reveals) and the minimum (+16,898) and the
  maximum (1,503,379, the largest single game anywhere in this document) at once.
  Its variance is higher than the comparator's in both directions. A term the
  frozen search has multiplied by zero since inception turns out to be worth
  approximately zero.
- **`solid_exposure` +40 → +4,934 with `cracked_exposure` +100 → +2,868:
  −67,719 (95% bound −133,082).** These are the two terms the fit wants most
  after the height block, and `solid_exposure` has the strongest univariate
  correlation with achievable clears of all nineteen (+0.627). At the weight the
  fit wants, they cost 17.3 moves of life. The depth-3 map (§4) found the best
  point at roughly a quarter of this weight, so the failure here is one of
  magnitude as well as of direction.
- **All four together: −75,214 (95% bound −139,902).** The bundle is slightly
  worse than `t1-exposures` alone and slightly better than the sum of its parts
  would suggest.
- **`numbered_cells` −18 → +1,048 was not run alone** (§8).

### Tier 2, the full refit

- **Half way, `t2-fair-a05`: −107,107 (95% bound −169,589), −28.78 moves.**
- **All the way, `t2-fair-a1`: −237,182 (95% bound −290,406), −63.91 moves,
  7-0-57.** The mean falls below `lowest-column`-plus-a-bit territory: at 161,316
  it is nearer the 100,050 of the trivial `lowest-column` control in
  [`finding-01`](finding-01-score-is-survival.md) than to the comparator.

Interpolation was the right precaution and it did not rescue anything. There is
no interior maximum: the response is monotone from the frozen vector to the
fitted one, so no blend of the two beats the frozen vector alone.

### Lower tail — and it is not what risk 2 predicted

The eight worst games of the comparator, paired:

| seed | frozen | `t1-rough-560` | `t1-exposures` | `t1-combined` | `t2-fair-a05` | `t2-fair-a1` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `0xa51d103e` | 103,331 | 322,439 | 410,788 | 103,517 | 104,060 | 103,509 |
| `0xa51d1010` | 121,585 | 380,958 | 444,458 | 700,285 | 211,120 | 194,860 |
| `0xa51d103b` | 122,174 | 267,674 | 430,102 | 121,231 | 215,251 | 122,238 |
| `0xa51d101a` | 123,795 | 452,874 | 443,620 | 174,380 | 394,550 | 159,053 |
| `0xa51d100b` | 141,619 | 413,861 | 466,867 | 262,021 | 463,582 | 193,731 |
| `0xa51d102d` | 142,477 | 175,370 | 211,318 | 138,574 | 103,453 | 137,285 |
| `0xa51d1013` | 156,261 | 289,048 | 191,176 | 474,164 | 735,941 | 138,126 |
| `0xa51d1005` | 156,615 | 176,505 | 347,177 | 121,987 | 320,587 | 105,333 |

**Every arm improves most of the comparator's worst eight games**, several of
them by three or four times, and `t1-rough-560`, `t1-combined` and `t2-fair-a05`
all improve the cohort minimum. What the losing arms destroy is the *upper* half:
the maximum falls 1,341,287 → 989,981 → 977,457 → 903,444 → 342,164 down the
ladder, and the standard deviation falls 254,414 → 54,703. Q25 is flat for the
Tier-1 arms (−1,424, −555, −6,884) and only collapses at Tier 2 (−19,438,
−91,250).

The reweighted leaves are not dying earlier in their bad games. They are failing
to build the long ones. Given that mean score is 3,400 × mean lifetime
(`finding-01`), and that the comparator's mean is carried by a heavy right tail —
two games over 1,000,000 in 64 — losing the tail is losing the mean.

## 6. The three risks, answered


### Risk 1 — the fitted target is a proxy, over a short horizon

**Verdict: confirmed, and it is the whole story.** `finding-10` §1–§7 was bitten
by exactly this once already, and the warning was correct a second time. The
label is the exact 8-move achievable clear optimum. Eight moves is one rise cycle
and a bit. A leaf that maximises it is a leaf that maximises what is reachable
*now*, and the measured consequence is a policy that clears less per move over a
whole game and carries more discs while doing it.

The mechanism is legible in the flow rates rather than inferred. Rotating toward
the fitted direction raises the weight on `numbered_cells` from −18 to +1,957 and
on `solid_exposure` from +40 to +9,216. Both say "a board with more numbered discs
and more nearly-poppable covers is a good board" — which is true of *this move's*
opportunity and false of the next fifty, because those discs are also the
occupancy that ends the game. Mean occupied cells rises monotonically 23.15 →
25.42 → 25.97 → 28.34 as the rotation proceeds. R² against an 8-move label
carries no information about that trade, and it is the trade that decides the
game.

The correct reading of Addendum A is therefore the one it gave itself: **a
hypothesis generator.** It generated four; all four were tested; all four were
rejected at depth 4. That is what a hypothesis generator is for.

### Risk 2 — the leaf does not act alone

**Verdict: real, but not the failure mode here, and the evidence says so
specifically.** `audit-02` §5 predicts that a reweighting could destroy the
death-avoidance keeping games alive, because one sampled death costs 18×–920× the
legitimate discrimination range between actions and the ranking is effectively
lexicographic. If that were what happened, the arms would show a collapsing lower
tail with the upper tail intact.

The opposite is observed at moderate strength. `t1-combined` and `t2-fair-a05`
both **improve the minimum** (+51, +122) and improve most of the comparator's
eight worst games, while the maximum falls from 1,341,287 to 977,457 and 903,444
and the standard deviation falls from 254,414 to 190,684 and 159,133. The
reweighted policies die *less* badly and win *less* well.

Two design choices are what let this be said rather than guessed. Every Tier-2
arm is sigma- and mean-matched to the frozen leaf over the corpus (§2), so the
leaf-to-terminal-utility exchange rate is held fixed and the arms vary direction
only; and the CHECK gate proves identical work accounting, so no arm is quietly
searching a different depth. Without the scale match, a wholesale weight swap
would have changed the risk attitude and the direction at the same time and this
question could not have been separated.

At `t2-fair-a1` the tail does degrade (minimum −435, Q25 −91,250). But the
distribution has collapsed to sd 54,703, a fifth of the comparator's: this is a
policy that reliably reaches about 50 moves and reliably stops, not one that dies
unpredictably early. Death-avoidance survived the reweighting; long-game
construction did not.

### Risk 3 — the fitted direction may be a synthetic-position artifact

**Verdict: confirmed as a genuine defect in the diagnostic, and corrected for
before the arms were built — which makes the negative result stronger, not
weaker.** §3 measures it directly: the synthetic-position fit and the fair-play
fit point in directions only **+0.310** apart, and the synthetic fit transfers to
fair-play held-out positions at R² **+0.214**, worse than the frozen leaf's own
+0.618. Addendum A's headline numbers — 0.396 for the frozen scalar, cosine
+0.141 — are mixture statistics over a corpus that is 44% synthetic, and on
synthetic positions the frozen leaf explains 0.7% of the variance while on
fair-play positions it explains 61.8%.

Two specific claims in Addendum A do not survive the restriction to fair-play
positions:

- **the leaf is "nearly orthogonal" to the predictive direction.** On the
  manifold where it is evaluated the cosine is +0.176, and the R² gap the
  reweighting has to close is +0.168 rather than +0.338;
- **`numbered_cells` is "the largest fitted term" at +0.601 per σ.** On fair-play
  positions it is +0.147 per σ. Most of its apparent size comes from the
  synthetic generator deliberately sweeping occupancy, which gives the feature a
  corpus σ of 6.04 against 2.30 on fair-play positions.

Every Tier-1 anchor and both Tier-2 vectors tested here were built from the
**fair-play-only** refit, which is the strongest version of the hypothesis
available. It still lost, monotonically, at every dose. The result is not "the
fit was contaminated so it failed"; the fit was decontaminated first and failed
anyway.

The residual caution belongs to this document too: the fair-play block is 384
positions harvested from `d2s5` play, not from the `d4s7` policy actually being
modified. A refit on `d4s7`-harvested positions is not tested here.

## 7. Verdict

> **Validity: valid.** All five CHECK gates passed before any gameplay; the
> comparator is a proven bit-identical reproduction; every arm played the same 64
> ordered public-information games at a declared fixed work bound with identical
> work accounting; no censoring, no identity violation, no illegal move.
>
> **Outcome: fail — a clean negative, with a mechanism.** Reweighting the frozen
> leaf toward the direction that predicts the exact 8-move achievable clear
> optimum does not improve whole-game play. It monotonically harms it. The best
> arm is a tie (+627, 95% bound −73,325); the full refit loses 237,182 points
> (95% bound −290,406, 7-0-57).
>
> **Evidence tier: `development` / `STANDARD`-shaped.** 64 paired whole games per
> arm on the shared evaluation cohort, which is reusable development data. This
> rejects the exact tested configurations; it is not a qualification claim, and
> **no protected or final seed was opened or is justified by anything here.**

### What was actually learned, stated as a positive

The interesting result is not "the reweighting failed". It is what the failure
says about what a search leaf is for.

`audit-02` §4.4 concluded that the frozen leaf is "a *reasonable proxy for* the
right objective (survival), by accident of its weight vector rather than by
construction". Addendum A then measured that this weight vector is nearly
orthogonal to the direction that predicts where clears are available, and read
that as a defect. This document tested the repair and found that **the frozen
weight vector is well tuned for play precisely because it does not predict
achievable clears.**

The mechanism is visible in the flow rates rather than inferred. The frozen leaf
spends 78% of its σ-scaled direction on `covered_height_risk` and
`rise_pressure` — two quantities that carry almost no conditional information
about how many clears are available *now*, and that are exactly the crowding
pressure which decides whether the board is still playable in thirty moves. The
fitted direction moves that weight onto `numbered_cells`, `solid_exposure` and
`cracked_exposure`, which say "a board with more numbered discs and more
nearly-poppable covers is a good board". That is true of this move's opportunity
and false of the next fifty, because those same discs are the occupancy that ends
the game. Measured consequence: occupancy rises monotonically 23.15 → 28.34 cells
and clears per move fall 2.0571 → 1.6019.

So the accident in `audit-02`'s phrasing may not be an accident. A leaf that is
"a crowding evaluator" is a leaf that estimates *future capacity to clear*, and
that is a different and more useful quantity than *present availability of
clears*, which four plies of exact search can already see for itself. The leaf's
job is to price what the search cannot see. Addendum A's regression measured how
well the leaf duplicates what the search already sees, and the answer — badly —
turns out to be a description of a well-designed leaf, not a defect in one.

This also sharpens the standing recommendation in `finding-01` and `finding-07`:
the binding constraint is a **slow** variable, the accumulation of a flow deficit
over eight-plus rise cycles. Any diagnostic label defined over an 8-move window
is measuring the fast variable. Two independent attempts in this repository have
now been misled by a short-horizon proxy — `finding-10`'s H = 9 scenario suite,
which ranked fair policies backwards, and this one.

### What this does not say

- It does not say the leaf's weights are optimal. It says the gradient does not
  point toward the achievable-clear direction. A refit against **remaining
  lifetime** — the target `finding-01` argues for, dense and untailed — is a
  different experiment and is untouched by this result.
- It does not refute Addendum A's regression, which is reproduced here exactly
  (held-out R² 0.7342 reweighted vs 0.3962 frozen, cosine +0.1406). It refutes
  the inference from that regression to a better search leaf.
- It does not test Addendum A's Tier 3, the genuinely absent cover-cracking
  geometry (`coversInWave`, `landingOnCover`). Those are new features, not
  reweightings. Given that §3 shows their measured gain is +0.005 R² on
  fair-play positions against +0.054 on synthetic ones, and given that
  reweighting the existing basis toward the same target loses, the prior on them
  should now be lower than A.7 implies.

### What to do next, ranked by information gain per unit of compute

1. **Refit against remaining lifetime, not achievable clears.** Same machinery,
   same CHECK gate, one different label. This is the experiment `finding-01` §
   "Why this matters" actually argues for, and it is the one that would
   distinguish "the leaf's weights are right" from "this target is wrong".
2. **Sweep `solid_exposure` alone at depth 4 near the depth-3 optimum** (+1,250,
   not the fitted +4,934). It is the one point in this document with a positive
   estimate at any depth, it was not run at depth 4 (§8), and it is one constant.
3. **Do not spend a week on `coversInWave` / `landingOnCover`** until 1 or 2
   produces a positive.

## 8. Limitations

- **64 paired games per arm.** Score standard deviation is 34–64% of the mean,
  so a 64-game mean carries roughly ±7,000 to ±32,000 at one standard error. The
  three significant losses are 2–7× that; the `t1-rough-560` tie is not resolved
  and its 95% interval spans roughly ±74,000.
- **`t1-numbered` and `t1-solidexp-1250` were not run.** Both were queued at
  depth 4; both were dropped when the shared machine's load average reached 68
  against a back-off threshold of 26 and the remaining arms would have taken a
  further five hours. This is recorded as a no-run, not as a null result. The
  `numbered_cells` sign flip is exercised inside `t1-combined` and both Tier-2
  arms, all of which lost; `solid_exposure` at +1,250 is genuinely untested at
  depth 4 and is the recommended next arm.
- **The depth-3 sweep was stopped at 8 of 25 arms** for the same reason. What
  survives is a prefix in a fixed execution order, so it is an unbiased subset of
  the planned arms in the sense that no arm was dropped for its result — but it
  covers only the `roughness` family and two `solid_exposure` points.
- **The refit corpus is not the policy's own manifold.** The fair-play block is
  384 positions harvested from `d2s5` play. The policy being modified is `d4s7`.
  §3 shows the fitted direction is sensitive to position origin at exactly this
  scale (cosine +0.310 between the synthetic and fair-play fits), so a refit on
  `d4s7`-harvested positions could differ again.
- **One interpolation path.** `t2-fair-a05` is the only interior point tested
  between the frozen and fitted directions, and the path is a straight line in
  σ-scaled space. A different parameterisation could in principle contain an
  interior maximum that this one misses, though the monotonicity across six arms
  makes that unlikely.
- **Scale and level were held fixed by construction** (§2). This is what allows
  the risk-2 question to be answered, but it means the experiment says nothing
  about whether a *rescaled* version of the fitted direction — which would also
  change the search's risk attitude — behaves differently.
- **These are this repository's simulator semantics**, including the two
  rise-boundary scoring discrepancies in
  [`audit-01`](audit-01-engine-fidelity.md).
- **The machine was shared throughout** with other contributors' sessions at load
  averages of 22–68 against 16 physical cores. Wall times below are therefore not
  a clean performance baseline; work per move is the comparable quantity and it
  is reported per arm.

## 9. Reproduce

```sh
# build (clang++ explicitly; the Makefile's CXX ?= clang++ loses to make's
# builtin CXX=g++, which trips a false -Werror=array-bounds in
# src/core/native/public-behavior.hpp)
./approaches/lifetime-objective/leaf-reweight/build.sh
B=./build/lifetime/leaf-reweight
RID=runs/RUN-A527-LEAFW

# CHECK gates - all five, before any gameplay
$B --leaf-check --depth 3 --chance-samples 7 --max-work 16000000 \
   --seed-start 0xa5278000 --check-games 24 --check-moves 400
$B --leaf-check --depth 4 --chance-samples 7 --max-work 16000000 \
   --seed-start 0xa5278100 --check-games 3 --check-moves 45
$B --reference-parity --seed-start 0xa5278200 --check-games 4 --check-moves 60
$B --self-parity --depth 4 --chance-samples 5 --max-work  3200000 \
   --seed-start 0xa5278400 --check-games 3 --check-moves 60
$B --self-parity --depth 4 --chance-samples 7 --max-work 16000000 \
   --seed-start 0xa5278300 --check-games 3 --check-moves 22

# the refit, and the cross-origin transfer that answers risk 3
python3 approaches/lifetime-objective/leaf-reweight/refit.py \
    runs/RUN-SUITE-9c41ab7e2d10/structure-h8-j4.csv --origin fair \
    --alphas 0.25,0.5,1.0 --out-dir approaches/lifetime-objective/leaf-reweight
python3 approaches/lifetime-objective/leaf-reweight/tier1.py \
    runs/RUN-SUITE-9c41ab7e2d10/structure-h8-j4.csv

# depth-3 response-surface map on the tuning lease (NOT a selection gate)
./approaches/lifetime-objective/leaf-reweight/sweep.sh $RID/sweep-d3s7 3 64 8 0xa5279000
python3 approaches/lifetime-objective/leaf-reweight/sweeptable.py $RID/sweep-d3s7 --sort

# depth-4 evaluation on the shared cohort
./approaches/lifetime-objective/leaf-reweight/evaluate.sh $RID/eval-d4s7 12 0xa51d1000
python3 approaches/lifetime-objective/leaf-reweight/compare.py \
    runs/RUN-A51D-s7confirm/fresh-s7.json \
    $RID/eval-d4s7/t1-rough-560.json $RID/eval-d4s7/t1-exposures.json \
    $RID/eval-d4s7/t1-combined.json $RID/eval-d4s7/t2-fair-a05.json \
    $RID/eval-d4s7/t2-fair-a1.json
```

### Artifacts

Under `runs/RUN-A527-LEAFW/`:

| file | contents |
| --- | --- |
| `check-leaf-d3s7.log`, `check-leaf-d4s7.log` | leaf bit-pattern gates, 936,612 boards |
| `check-reference-parity.log` | 240 moves against the unmodified reference driver |
| `check-self-parity-d4s5.log`, `check-self-parity-d4s7.log` | weights-as-data vs weights-as-constants, columns and work |
| `eval-d4s7/frozen8.json` | the cross-binary comparator check, 8 games |
| `eval-d4s7/{t1-rough-560,t1-exposures,t1-combined,t2-fair-a05,t2-fair-a1}.json` | one cohort artifact per arm, 64 games each, with per-game rows |
| `eval-d4s7-table.md` | the §5 tables |
| `sweep-d3s7/*.json`, `sweep-d3s7-table.md` | the depth-3 map, 8 of 25 planned arms |
| `refit-fair.txt`, `refit-played.txt`, `tier1-anchors.txt` | the regression output |

Each cohort artifact is `drop7-lifetime-cohort-v1`: configuration including the
full nineteen-weight vector and the bias, cohort summary, score decomposition,
flow rates, and one row per game with seed, score, moves, censor flag, rises,
level/clear/chain points, numbered clears, covered reveals, maximum chain depth,
mean occupied cells, wall time and logical work.

Frozen source hashes are recorded at build time in
`build/lifetime/reference-sources.sha256`. `fair-only-depth4.cpp` ends in a real
`int main` and is compiled from a build-tree copy whose only changed line is the
entry point; the build refuses to proceed if more than that one line differs.

### Seed lease

`SEEDLEASE-A52-LEAFW` = `0xa5278000`–`0xa527ffff`, a sub-range of the
`SEEDLEASE-A52` reserve in [`lease-map.md`](lease-map.md). Role: **exploratory
development**; once read these seeds are development data permanently.
**`lease-map.md` was not edited by this work** — it is a shared file and other
sessions were writing to it concurrently. The coordinator should add these rows
when merging.

| Offset | Use | Opened |
| --- | --- | --- |
| `0x8000`–`0x80ff` | CHECK gates: leaf bit patterns, reference parity, self parity | `0x8000`–`0x8017`, `0x8100`–`0x8102`, `0x8200`–`0x8203`, `0x8300`–`0x8302`, `0x8400`–`0x8402`, `0x8500`–`0x8501` |
| `0x9000`–`0x903f` | depth-3 response-surface map, 64 games per arm | yes |
| rest | reserve | not opened |

The shared evaluation cohort `0xa51d1000`–`0xa51d103f` was read for the reported
arms only. **No weight value, magnitude, arm or interpolation coefficient in §5
was chosen by looking at evaluation-cohort output**; the arm list and the weight
values were fixed from the tuning lease and the regression before the first
evaluation game was played, and every arm that ran is reported.

### Environment

AMD Ryzen AI MAX+ 395, 16 physical / 32 logical cores, single NUMA node, 125 GB
RAM, Linux 6.18.35. AMD clang 23.0.0git, `-O3 -std=c++20 -pthread -Wall -Wextra`.
**At most 12 threads were used by this work**, on a machine shared with other
contributors' sessions at load averages of 22–68 throughout; the depth-3 sweep
and two depth-4 arms were cancelled when load exceeded the back-off threshold.

| arm | games | threads | wall |
| --- | ---: | ---: | ---: |
| `frozen8` | 8 | 12 | 1,691 s |
| `t1-combined` | 64 | 12 | 4,281 s |
| `t2-fair-a05` | 64 | 12 | 4,965 s |
| `t2-fair-a1` | 64 | 12 | 2,220 s |
| `t1-exposures` | 64 | 12 | 5,994 s |
| `t1-rough-560` | 64 | 12 | 10,647 s |

Wall time is **not** comparable across arms as a performance measurement: the
arms ran at different times against different background load, and a stronger
arm plays longer games. Work per move (§5) is the comparable quantity.
