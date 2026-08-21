# Finding 14 — reweighting the fair leaf toward achievable clears

**Status:** DRAFT — the gameplay arms are still running. Sections 1–3 are final.

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

Four gates, all passed, all on the tuning lease:

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

## 5. Results — depth 4, seven strata, the shared evaluation cohort

TBD
