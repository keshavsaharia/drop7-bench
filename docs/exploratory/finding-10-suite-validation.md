# Finding 10 — The scenario suite does not rank fair policies, and the reason is the horizon

**Status:** exploratory, evidence tier `development`/`pilot`. **Negative result on
the preregistered check**, with a mechanism and a concrete repair.
Built and measured in this checkout on 2026-08-20.
**Namespace:** `approaches/lifetime-objective/suite-validation/`, run
`runs/RUN-SUITE-9c41ab7e2d10/`, seed lease `SEEDLEASE-A52-SUITE` =
`0xa5258000`–`0xa525bfff`.
**Nothing in `docs/research/`, `artifacts/`, `research/`, `runs/RUN-20260820T*`,
`approaches/afterstate-learning/`, or any existing approach source was created
or modified by this work.** Files were created only under
`approaches/lifetime-objective/suite-validation/`, `build/suite-validation/`,
`runs/RUN-SUITE-*/` and `docs/exploratory/`.
Preregistration:
[`PREREGISTRATION.md`](../../approaches/lifetime-objective/suite-validation/PREREGISTRATION.md),
written and amended before any verdict-bearing row was inspected.

## Why this exists

[`design-01`](design-01-benchmark-suite.md) ends with a gate on its own product:

> Until check 1 passes with a reported number, **the suite is a diagnostic and
> cannot be cited as evidence of policy strength.**

Neither of its two checks had been run.
[`finding-02`](finding-02-scenario-benchmark.md) §7 says so in as many words —
the fair multi-tape evaluation "is deliberately **not** implemented here" — and
every policy number in `finding-02` §5 is a single tape per position, which that
document's own limitation 4 says "must not be used to rank" policies.

So the repository owned a 128-position benchmark, a proven exact solver, and no
evidence that any of it predicted whole-game strength. This document builds the
missing fair evaluation, runs both checks, and reports what it found.

## Summary

| Claim | Evidence |
| --- | --- |
| **Position mode now exists and is provably fair** | Redrawing every future disc and every hidden value of every future risen row, while holding the visible state fixed, never changed a fair policy's chosen column: **768 decisions, 0 violations** across 8 policies |
| **Check 1 FAILS as preregistered** | On the primary metric, Spearman with whole-game means is **S9 = +0.633** over nine policies (preregistered pass threshold 0.70) and **S6 = −0.257** over the six fair arms that share one cohort. The suite ranks the fair arms *slightly backwards* |
| It separates strong families from weak ones, and nothing finer | Every fair arm beats `lowest-column` at t = 3.6–4.9 and `center-first` at t ≈ 8.5. Inside the fair block every paired t is \|t\| ≤ 2.61 and the signs are frequently wrong: `d4s7` − `d2s7` = **−4 points (t = −0.01)** where whole games differ by **+133,204** |
| **The cause is measured, not guessed** | The exact score identity: across the six fair arms the row-rise term — **92% of the scenario score** — spans only **492 points** (28,005–28,497) and the chain term 243. At H = 9 the rise count is fixed by `movesRemaining` unless a policy dies, so the metric has no room to express a 149,000-point whole-game spread |
| **The suite is precise about the wrong thing** | Between-policy variance is **624×** the within-policy re-evaluation variance. It resolves its own metric to t = 1.905 on `d3s7` vs `d4s7` for **43%** of the logical work of 64 paired whole games — a 1.50× better t² per unit work. Precision without validity |
| **The failure is a horizon artifact, and the repair is concrete** | On the **same 95 positions and the same tape streams**, changing only the horizon from 9 to 25 moves the fair-block Spearman from **−0.600 to +0.800** and the seven-policy Spearman from +0.714 to +0.964; on all 128 positions at H = 25 both reach **+1.000**, consistently on both halves of the split and both position origins. Post hoc, and missing the two depth-4 arms |
| Check 2 is recorded, and immediately earned its keep | A content-hashed 64/64 development/sealed split. At H = 9 the two halves disagree on the fair block (S6 = +0.03 vs −0.37) — exactly the overfitting channel the split exists to catch |

Two addenda were added after the coordinator read the above:

| Claim | Evidence |
| --- | --- |
| **[A] The frozen leaf is not missing features — it is pointing the wrong way** | Over 1,024 exactly-labelled positions: any linear model over 53 public structural properties tops out at held-out R² = **0.753**; the leaf's own 19 features **freely reweighted** reach **0.734**; the leaf as frozen reaches **0.396**. Reweighting recovers 95% of all available signal. Cosine between the frozen weight direction and the direction that predicts achievable clears: **+0.141** |
| **[A] The cheapest candidates are constants, not features** | `roughness` is computed at every node and weighted **exactly 0** (univariate ρ +0.401); `solid_exposure` is 0.2% of the leaf's direction and has the **strongest** univariate correlation of all 19 (+0.627); `cracked_exposure` is 0.5% (+0.453). Meanwhile `covered_height_risk` is **67.7%** of the leaf's direction and the fit wants +0.005 |
| **[A] What is genuinely absent is all one thing: cover-cracking geometry** | `coversInWave` and `landingOnCover` add +0.036 held-out R² on the achievable rate and +0.043 on the achievable-minus-achieved gap — the only absent properties that add anything. This is `finding-07` §6's named residual reached by an independent route |
| **[B] Suite v2: do not build it** | At H = 25 the suite/whole-game work ratio to reach t = 2 has median **1.50** and spans **0.02 to 5,103** across six fair pairs, and its signal transfer varies by **283×**. A validity-preserving suite plays **1.5–2.8× more decisions** than the 64-game cohort it would replace. The horizon that makes it valid is the horizon that makes it expensive |

**§6 recorded that Task 2 was not run under the preregistered gate. The
coordinator subsequently authorized it; the result is Addendum A. §6 is left as
recorded.**

## 1. Position mode — the design, and why it is the only fair mode

`design-01` defines two modes and states that only one can prove improvement.
The distinction is the entire validity of the benchmark.

**Puzzle mode fixes the disc tape and grades a policy against the clairvoyant
optimum on that tape.** A public-information policy cannot be graded against it:
the optimum is a function of information `docs/methodology.md` forbids the policy
to have. `finding-02` §7 names the failure mode — selecting positions by that gap
"teaches a student to memorize a coincidence".

**Position mode holds the position fixed and averages over futures.** It did not
exist before this work. It is implemented as:

1. hold `board`, `latent[]`, and `movesRemaining` fixed — that is the *position*,
   including the hidden board underneath it;
2. hold `discTape[0]` fixed — **the visible next disc is public state**, part of
   the decision problem the position poses, not part of the hidden future. A
   sibling that redrew it would be a different position;
3. redraw `discTape[1..H-1]` and every future risen row independently, K times;
4. give **every policy the same K tapes** — common random numbers;
5. score a policy on the position by its mean over those K tapes.

The primitives are exactly the ones `finding-02` exposed for this and never used:
`retapeScenario`, `resampleScenarioRandomness`, `reHorizonScenario`. `retapeAt`
in `posmode.hpp` is the tape-0-fixing variant; `relatentAt` is its hidden-board
counterpart, used only by the (unrun) structure probe and deliberately **not** by
position mode, because redrawing the latent board would change the position
rather than the future.

Point 2 is not cosmetic — it is what makes the fairness gate below possible.

### The comparator set

Six fair arms on the **shared** 64-game cohort `0xa51d1000`–`0xa51d103f`
([`finding-05`](finding-05-chance-strata.md), plus the coordinator-supplied
`d2s5` figure), and three weak baselines whose whole-game means come from a
**different** 64-seed cohort ([`finding-01`](finding-01-score-is-survival.md)).
Every table that mixes the two says so.

| policy | depth | strata | work bound | whole-game mean |
| --- | ---: | ---: | ---: | ---: |
| `d2s5` | 2 | 5 | 3,200,000 | 249,641 |
| `d2s7` | 2 | 7 | 16,000,000 | 265,294 |
| `d3s5` | 3 | 5 | 3,200,000 | 305,051 |
| `d3s7` | 3 | 7 | 16,000,000 | 312,327 |
| `d4s5` *(the frozen comparator)* | 4 | 5 | 3,200,000 | 297,327 |
| `d4s7` | 4 | 7 | 16,000,000 | 398,498 |
| `center-first` | — | — | — | 57,233 |
| `random-legal` | — | — | — | 80,778 |
| `lowest-column` | — | — | — | 100,050 |

The six fair arms are `ParameterizedSearch` from
`approaches/lifetime-objective/risk-calibration/search.cpp`, consumed unmodified
through a build-tree copy whose only changed line is the entry point. That class
is the driver `finding-05` measured. The work bounds are `finding-05`'s own, and
they matter: at seven strata the frozen 3,200,000 bound silently degrades depth 4
to a completed depth 3.

**These are literally the whole-game policies.** `scenarioSeedForState` and
`dynamicStateKey` (`public-behavior.hpp:721-754`) hash only `board`, `next_disc`,
and `moves_remaining` — no score, level, or move number — so a decision taken
inside a scenario is byte-identical to the decision the same policy would take
from the same public state in a whole game. Nothing about the failure below is an
artifact of re-implementation.

### CHECK gates

All ran before any measurement.

| Gate | Scale | Result |
| --- | --- | --- |
| **Comparator parity**: this work's `d4s5` arm selects the column the frozen `chooseDepth4Action` selects | 3 games x 40 moves, seeds `0xa5258000`+ | **120 moves compared, 0 mismatches** |
| **Scenario engine parity** (`finding-02`'s gate, re-run here): `ScenarioEngine<StreamRevealSource>` vs `drop7::playHeadlessMove` | 4,096 seeds x 2 policies = **8,192 game-plays, 218,470 moves** | **0 mismatches**; reveal-marginal chi-square 2.356 on 6 df |
| **Information boundary**: a fair policy's first chosen column must be identical across all K tapes of a position | 8 policies x 24 positions x 4 tapes = **768 decisions** | **0 violations** |
| **Determinism and thread-count independence** of the whole evaluation | 24 positions x 4 tapes x 3 policies, 1 thread vs 8 threads | **byte-identical**, hash `e0b34b13692470a0` |
| **Reflection invariance** of every candidate structural feature (§6) | 20,034 feature values over 378 synthetic boards (512 draws attempted) | **0 violations** |
| **Window solver agreement**: the clear-objective solver in points mode vs the frozen exact solver | 20 scenarios | **0 mismatches** |
| **Rebuild reproduction**: binaries rebuilt from the final sources replay recorded rows of the reported run | 4 positions x 4 tapes x 4 policies = 64 rows, all nine recorded fields | **0 differences** |

The information-boundary gate deserves a sentence. Position mode redraws the
entire future — every remaining disc and every hidden value of every future risen
row — while holding the visible state fixed. A policy that leaked any of it would
change its first move on at least one tape. None of the eight deterministic
policies ever did, across 768 decisions. That is a structural proof of fairness
for this metric, not an assertion about it.

## 2. Check 1 — the suite does not rank fair policies

**Configuration as preregistered:** all 128 positions of `suite-h9-v1`, H = 9,
K = 4 tapes, common random numbers, primary metric = mean scenario **points**.
The run was stopped by its preregistered 90-minute budget with **95 of 128
positions complete** (74%). Positions are processed in an order determined only
by the FNV-1a-64 hash of their id, so the completed set is an unbiased subsample
of the suite, not "all the harvested ones": it is 48 harvested and 47 synthetic,
45 development and 50 sealed.

### Position-mode results — 95 positions x 4 tapes = 380 paired cells

| policy | whole-game mean | **points** | clears/move | moves (of 9) | survival | reveals/move | −occupancy drift/move | logical work per cell |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `d2s5` | 249,641 | 30,765 | 2.1248 | 8.705 | 0.9079 | 1.1419 | −0.1896 | 17,751 |
| `d2s7` | 265,294 | **31,012** | 2.1401 | 8.705 | 0.9105 | 1.1509 | −0.1765 | 36,670 |
| `d3s5` | 305,051 | 30,415 | 2.0783 | 8.634 | 0.8921 | 1.1062 | −0.2201 | 464,025 |
| `d3s7` | 312,327 | 30,484 | 2.1426 | 8.618 | 0.8868 | 1.1534 | −0.1560 | 1,340,809 |
| `d4s5` | 297,327 | 30,600 | 2.0488 | 8.658 | 0.8921 | 1.0798 | −0.2539 | 11,144,902 |
| **`d4s7`** | **398,498** | 31,008 | 2.1109 | 8.687 | 0.9079 | 1.1360 | −0.2039 | 40,980,426 |
| `center-first` | 57,233 | 22,018 | 1.0820 | 7.663 | 0.5789 | 0.3450 | −0.9526 | 8 |
| `random-legal` | 80,778 | 23,236 | 1.1514 | 7.824 | 0.6368 | 0.4547 | −0.9293 | 8 |
| `lowest-column` | 100,050 | 28,155 | 1.3970 | 8.561 | 0.8289 | 0.5760 | −0.8850 | 9 |

The six fair arms occupy a band 597 points wide — **1.9% of their own mean** —
while their whole-game means span 148,857 points, 60% of theirs.

### The Spearman numbers

`p` is a one-sided permutation p-value against the null of no association;
`n` is 9 or 6, so the permutation distribution is the honest null and no
large-sample approximation is used.

| metric | S9 (all nine, cross-cohort) | p | S6 (six fair arms, shared cohort) | p |
| --- | ---: | ---: | ---: | ---: |
| **`points` (primary)** | **+0.6333** | 0.038 | **−0.2571** | 0.717 |
| `clearsPerMove` | +0.7000 | 0.022 | −0.0286 | 0.541 |
| `moves` | +0.5272 | 0.075 | −0.6377 | 0.920 |
| `survived` | +0.5883 | 0.051 | −0.4414 | 0.828 |
| `revealsPerMove` | +0.7000 | 0.022 | −0.0286 | 0.541 |
| `negOccupancyDrift` *(post hoc)* | +0.7000 | 0.022 | −0.0286 | 0.541 |

> ### Preregistered verdict
>
> **S9 = +0.6333 < 0.70. The suite as minted, scored in position mode at H = 9,
> is NOT a strength instrument.** No number computed on it may be cited as
> evidence about policy strength, exactly as `design-01` said in advance.

Note that **no metric rescues it.** The preregistered secondaries and the one
post-hoc metric all give S6 between −0.64 and −0.03: on the six policies that
share a cohort and whose ranking actually matters, the suite carries **no
information at all, and if anything a slightly inverted signal.**

### What the suite *can* do — and it is not nothing

Paired t on `points` under common random numbers, 95 positions as the
independent unit:

| comparison | suite delta | SE | t | whole-game delta |
| --- | ---: | ---: | ---: | ---: |
| any fair arm − `center-first` | +8,397 … +8,994 | ~1,000 | **+8.4 … +8.7** | +192,408 … +341,265 |
| any fair arm − `random-legal` | +7,179 … +7,776 | ~870 | **+8.4 … +8.7** | +168,863 … +317,720 |
| any fair arm − `lowest-column` | +2,260 … +2,857 | ~600 | **+3.6 … +4.9** | +149,591 … +298,448 |
| `lowest-column` − `random-legal` | +4,919 | 720 | **+6.83** | +19,272 |
| `lowest-column` − `center-first` | +6,137 | 833 | **+7.37** | +42,817 |
| `random-legal` − `center-first` | +1,218 | 752 | +1.62 | +23,545 |
| **`d4s7` − `d2s7`** | **−4** | 363 | **−0.01** | **+133,204** |
| `d4s7` − `d3s7` | +523 | 275 | +1.91 | +86,171 |
| `d3s5` − `d2s5` | −350 | 326 | −1.07 | +55,410 |
| `d3s7` − `d2s7` | −527 | 397 | −1.33 | +47,033 |
| `d4s5` − `d2s5` | −164 | 347 | −0.47 | +47,686 |

It ranks the *families* correctly — every fair search beats every weak baseline
with large t, and the three weak baselines come out in the right order. It has
**no ability to order the fair arms**: four of the six within-block comparisons
above have the wrong sign, and the largest whole-game gap in the set
(`d4s7` over `d2s7`, +133,204 points, a 50% improvement) registers as
**minus four points**.

That is the honest description of the instrument: a family detector, not a
strength meter.

### The mechanism, from the exact score identity

`audit-02` §4.1 gives the identity, and `finding-06` verified it over 19,610
moves with zero violations:

```
score delta = 17,000 x rises + 70,000 x board clears + sum of wave points
```

Decomposing the position-mode score:

| policy | mean points | rises x 17,000 | board clears x 70,000 | chain wave points | rise share |
| --- | ---: | ---: | ---: | ---: | ---: |
| `d2s5` | 30,765 | 28,453 | 0 | 2,312 | 92.48% |
| `d2s7` | 31,012 | 28,497 | 0 | 2,514 | 91.89% |
| `d3s5` | 30,415 | 28,050 | 0 | 2,365 | 92.22% |
| `d3s7` | 30,484 | 28,005 | 0 | 2,479 | 91.87% |
| `d4s5` | 30,600 | 28,139 | 0 | 2,461 | 91.96% |
| `d4s7` | 31,008 | 28,453 | 0 | 2,555 | 91.76% |
| `center-first` | 22,018 | 21,384 | 0 | 634 | 97.12% |
| `random-legal` | 23,236 | 22,547 | 0 | 689 | 97.04% |
| `lowest-column` | 28,155 | 27,245 | 0 | 910 | 96.77% |

**Across the six fair arms the rise term spans 492 points and the chain term
243.** Zero board clears, consistent with `finding-01`, `finding-02` §6 and
`finding-06` §5.

This is the whole failure in one number. A nine-move window starting with
`movesRemaining` in 1..5 contains **one or two** row rises, and which of the two
is a function of `movesRemaining` alone — not of the policy — unless the policy
dies inside the window. The fair arms die inside nine moves at rates of
8.9%–11.3%, and those rates differ by less than the tape-to-tape noise. So the
92% of the metric that carries the signal in whole games is, at H = 9, very
nearly a constant that every policy collects equally.

There is a second contributing cause, and the data separates it. `finding-02`
limitation 6 records that the suite's harvested positions come from
**lowest-column play**, a weak policy that runs a board six to fourteen cells
fuller than any fair arm's operating point (`finding-07` §5). Splitting by
origin, post hoc:

| origin | positions | S9 | S6 |
| --- | ---: | ---: | ---: |
| harvested (from `lowest-column` games) | 48 | +0.617 | **−0.314** |
| synthetic (controlled occupancy/cover) | 47 | +0.883 | **+0.600** |

The half of the suite that was supposed to keep it anchored to real play is the
half that predicts worst, because it is anchored to the *wrong* policy's real
play. This is a subgroup analysis at n≈48 and is reported as a lead, not a
result.

## 3. Discriminating power — precise about the wrong thing

Between-policy variance of the position-mode metric, against the variance of
re-evaluating the same policy on an independent set of tapes (the K = 4 tapes
split into two disjoint halves, then scaled to a full-K evaluation):

| metric | between-policy variance | within-policy re-evaluation variance | ratio |
| --- | ---: | ---: | ---: |
| `points` | 1.243e+07 | 19,926 | **624** |
| `clearsPerMove` | 0.2091 | 2.538e−04 | **824** |
| `negOccupancyDrift` | 0.1315 | 3.441e−04 | **382** |

The instrument is **extremely precise**. In standard-deviation terms its own
re-evaluation noise is 4.0% of the spread it reports (points: 141 against 3,526).
That is precisely why it is dangerous: a 624:1 variance ratio on a quantity whose
Spearman with the target is −0.26 will produce confident, reproducible, wrong
rankings, and will reproduce them on demand.

### Against 64 whole games, in logical work

Machine-independent logical work, as `docs/benchmarks.md` requires for a strength
comparison, because this machine was shared throughout.

`finding-05` resolves `d4s7` − `d3s7` on 64 paired whole games at
Δ = +86,172 with a 95% lower bound of +26,468, i.e. **t ≈ 2.37**, at a cost of
`Σ (work/move × mean moves × 64)` = **37.30e9 logical work**.

The suite, on the same pair:

| | value |
| --- | ---: |
| cells measured | 380 (95 positions x 4 tapes) |
| logical work | **16.08e9** (42.3e6 per cell) |
| paired t on `points` | **+1.905** |
| t² per unit work, suite | 2.257e−10 |
| t² per unit work, 64 whole games | 1.506e−10 |
| **suite advantage in resolving power per unit work** | **1.50×** |

**How many scenarios and how many tapes are needed to match the resolving power
of 64 whole games?** t scales as the square root of the cell count, so matching
t = 2.37 needs 380 × (2.37/1.905)² = **588 cells**. The suite has 128 positions,
so:

| configuration | cells | projected t | logical work | fraction of 64 whole games |
| --- | ---: | ---: | ---: | ---: |
| 128 positions x K = 4 | 512 | 2.21 | 21.67e9 | 0.58 |
| **128 positions x K = 5** | **640** | **2.47** | **27.09e9** | **0.73** |
| 128 positions x K = 6 | 768 | 2.71 | 32.50e9 | 0.87 |

**Plainly: 128 scenarios at 5 tapes each — 640 paired evaluations — matches the
resolving power of a 64-game paired cohort on this comparison, for about 73% of
the logical work. It is cheaper, by roughly 1.4×. It is also not worth using,
because the quantity it resolves has Spearman −0.26 with whole-game strength
across the fair arms.** A 1.4× saving on a measurement that does not predict the
target is not a saving.

## 4. Check 2 — the development / sealed split

Fixed and content-hashed **before** anything was measured on it; this document
tunes nothing on either half. Manifest:
`approaches/lifetime-objective/suite-validation/data/suite-h9-v1-split-v1.json`.

Rule, recorded verbatim inside the manifest: inside each `origin` stratum, order
positions by FNV-1a-64(`"suite-split-v1:" + id`); the first half of that order is
`development`, the second is `sealed`. The `id` is already a content hash over
every field of the scenario, so the assignment is a pure function of the suite's
bytes and cannot be quietly reshuffled after a result.

| | value |
| --- | --- |
| suite | `approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl` |
| suite SHA-256 | `1eaa2c74199298521889bd57eba68ef24d4b27d3ea1e8ed3217c3345c3ff9692` |
| positions | 128 (64 harvested, 64 synthetic) |
| **development half** | 64 positions, SHA-256 over ordered ids `0aca1768371ae19552ff0ee3d7fcfc42e1e48a3e9bf7dd2545902dab82d4dd21` |
| **sealed half** | 64 positions, SHA-256 over ordered ids `a8a56f7061ce7b00c6452cb94dc09909fb555f75cc3fefe282c8780abf661ebe` |
| manifest SHA-256 (over the body, excluding the field itself) | `b1b59dabcb98c50494eb138ea38d52d556ed8514807e3dfd3442633c0415dce4` |
| harvested positions sharing an origin game seed across the halves | **0** |

| half | positions | harvested / synthetic | occupied cells min / median / max / mean | covered mean | clairvoyant optimum mean |
| --- | ---: | --- | --- | ---: | ---: |
| development | 64 | 32 / 32 | 9 / 26 / 46 / 24.84 | 16.09 | 55,897 |
| sealed | 64 | 32 / 32 | 6 / 25 / 40 / 23.63 | 15.13 | 50,321 |

The halves are comparable on every recorded axis. The sealed half is slightly
easier by clairvoyant optimum (50,321 vs 55,897), a property of a content-hash
split, recorded here so a future cross-half comparison is read against it rather
than surprised by it.

### The split immediately earned its keep

Applying §2's analysis to each half separately, at H = 9:

| half | positions | `d2s5` | `d2s7` | `d3s5` | `d3s7` | `d4s5` | `d4s7` | S9 | S6 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| development | 45 | 32,331 | 32,712 | 32,283 | 32,076 | 32,696 | 32,886 | +0.717 | **+0.029** |
| sealed | 50 | 29,355 | 29,482 | 28,733 | 29,052 | 28,715 | 29,317 | +0.600 | **−0.371** |

A candidate tuned to the development half's ordering would have lost its margin
on the sealed half. That is exactly the channel `design-01` check 2 exists to
close, observed on the first use.

## 5. The failure is a horizon artifact — a post-hoc diagnostic with a repair

**Everything in this section is post hoc.** It was designed after §2's result was
read, and it is a diagnosis, not a re-test of the verdict. It is reported because
it identifies a specific, preregisterable repair.

The mechanism in §2 predicts a specific fix: if the metric fails because the
17,000-point rise term cannot vary over nine moves, then lengthening the horizon
until the rise count *can* vary should restore the ranking. Position mode does not
need the exact solver, so its horizon is not capped at the H = 9 that
`finding-02` established as the solver's practical limit — this is the one axis
where position mode is strictly freer than puzzle mode.

Re-scoring the **same 128 positions with the same tape streams at H = 25** — five
rise cycles instead of one and a bit. The two depth-4 arms were omitted: at
H = 25 they cost an estimated 3.7 hours of the eight threads this work was
allowed, against the 11 minutes the seven cheaper arms took.

| policy | whole-game mean | points at H = 25 | moves (of 25) | survival | clears/move |
| --- | ---: | ---: | ---: | ---: | ---: |
| `d2s5` | 249,641 | 72,301 | 20.848 | 0.6602 | 1.9553 |
| `d2s7` | 265,294 | 72,756 | 20.932 | 0.6523 | 1.9993 |
| `d3s5` | 305,051 | 72,773 | 20.879 | 0.6504 | 1.9714 |
| `d3s7` | 312,327 | **73,663** | 21.004 | 0.6699 | 2.0187 |
| `center-first` | 57,233 | 31,399 | 11.713 | 0.0684 | 1.0019 |
| `random-legal` | 80,778 | 38,283 | 13.434 | 0.1445 | 1.0946 |
| `lowest-column` | 100,050 | 54,431 | 17.266 | 0.3242 | 1.2603 |

| | S over all seven | p | S over the four fair arms | p |
| --- | ---: | ---: | ---: | ---: |
| **`points` at H = 25** | **+1.0000** | 0.0002 | **+1.0000** | 0.042 |
| `clearsPerMove` | +0.9643 | 0.0015 | +0.8000 | 0.166 |
| `moves` | +0.9643 | 0.0015 | +0.8000 | 0.166 |
| `revealsPerMove` | +0.9643 | 0.0015 | +0.8000 | 0.166 |
| `negOccupancyDrift` | +0.9286 | 0.0033 | +0.8000 | 0.166 |
| *the same seven policies at H = 9* | *+0.7143* | *0.020* | *−0.6000* | *0.892* |

The controlled version of that comparison — the **same 95 positions** that the
H = 9 arm completed, the same tape streams, the same seven policies, the only
difference being the horizon:

| horizon | positions | `d2s5` | `d2s7` | `d3s5` | `d3s7` | S over seven | S over the four fair arms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| **H = 9** | 95 | 30,765 | 31,012 | 30,415 | 30,484 | **+0.714** | **−0.600** |
| **H = 25** | 95 | 73,448 | 73,764 | 73,760 | 74,426 | **+0.964** | **+0.800** |

**Changing only the horizon moves the fair-block Spearman from −0.600 to +0.800.**
On the full 128-position H = 25 arm both statistics reach +1.000. And unlike
H = 9 the H = 25 result is stable across every slice:

| slice | positions | S over seven | S over the four fair arms |
| --- | ---: | ---: | ---: |
| development half | 64 | +0.964 | +0.800 |
| sealed half | 64 | +0.964 | +0.800 |
| harvested origin | 64 | +0.964 | +0.800 |
| synthetic origin | 64 | +1.000 | +1.000 |

Four slices, four consistent answers. At H = 9 the same four slices gave S6 of
+0.03, −0.37, −0.31 and +0.60 — the instrument disagreed with itself depending on
which half of its own positions it was asked about.

(The +1.000 on all 128 positions and the +0.800 on the matched 95 differ for one
reason: `d2s7` and `d3s5` land 4 points apart on the 95-position subset and swap
rank. Their whole-game means differ by 39,757. This is caution 2 below, made
concrete.)

**Three cautions, all of which matter.**

1. **The two depth-4 arms are missing**, and they are the hard case: whole-game
   means are *not* monotone in depth (`d4s5` 297,327 sits below `d3s5` 305,051
   and `d3s7` 312,327), so the four arms tested here happen to be monotone in
   both depth and strata. A perfect Spearman on a monotone n = 4 set is a much
   weaker statement than it looks.
2. **The fine resolution is still poor.** Within the fair block at H = 25 the
   paired t values are `d3s7` − `d2s5` +1,362 (t = +2.13), `d3s7` − `d3s5`
   +890 (t = +2.06), `d3s7` − `d2s7` +907 (t = +1.33), `d3s5` − `d2s7`
   **+17 (t = +0.02)**. The *means* order correctly; individual pairs are mostly
   unresolved. The suite gets the ranking right by getting six of six signs
   nearly right, not by resolving any one of them.
3. **It is post hoc.** S = +1.000 was not predicted in advance at this horizon,
   only the *direction* of the effect was.

### The preregisterable repair

> **Weighed and rejected in [Addendum B](#addendum-b--suite-v2-build-or-retire).**
> The configuration below is what a v2 *could* be. Addendum B prices it at 66
> core-hours, shows that its cost advantage over 64 paired whole games has a
> median of 1.50 **against** it and a spread of 0.02 to 5,103, and recommends not
> building it. This subsection is kept because it was written before that
> analysis existed and because B.5 refers to it.

A `suite-h*-v2` position-mode configuration that could plausibly pass check 1:

- **H = 25 or longer**, so the row-rise term — 92–94% of the score — can vary;
- **positions harvested from fair-policy play, not `lowest-column` play**,
  closing `finding-02` limitation 6, which §2 shows costs 0.9 of Spearman on the
  fair block;
- **all six fair arms including both depth-4 configurations**, since the
  non-monotone `d4s5` is the discriminating case;
- **K ≥ 5**, from §3's resolving-power arithmetic;
- the same split manifest rule, regenerated for the new position set.

The cost driver is `d4s7` at H = 25: about 280 core-seconds per (position, tape)
cell, so 128 x 5 cells is roughly 50 core-hours for that arm alone. That is the
number a coordinator has to authorize, and it is why it was not run here.

## 6. Task 2 was not run

> **SUPERSEDED BY [Addendum A](#addendum-a--task-2-what-predicts-an-achievable-clear-and-what-the-frozen-leaf-does-with-it).**
> After this section was written the coordinator accepted the objection at the
> end of it and authorized Task 2, on the ground that the structure probe does
> not consume the suite. The section is left exactly as recorded, because the
> reasoning that led to holding is part of the record. The result is Addendum A.

The coordinator's instruction gates the structural analysis of the achievable
clear rate on check 1 passing, and the preregistered rule reads the verdict off
the primary metric at the preregistered configuration. **Check 1 failed. Task 2
was therefore not run**, and no structural result is claimed.

What exists and is ready:

- `approaches/lifetime-objective/suite-validation/features.hpp` — **53** candidate
  structural properties covering every quantity the brief names: run-length
  histograms by row and column under the **engine's own run semantics**
  (`drop7::lineLength` counts every non-empty cell, so a gray cover is part of a
  run — and gravity forces the vertical run through any occupied cell to equal
  the column height); one-away and two-away counts; over-length "clog" counts
  split at the value-2 boundary where the frozen leaf's `dead_low_numbers` term
  stops; cover adjacency and the count of covers reachable by a cracking wave;
  column height variance and range; the value histogram against the room
  available to each value; the connectivity of the empty region; and the full
  **trigger map** — for each of the seven possible next discs and each legal
  column, the size of the first cascade wave, which is public and deterministic
  because the first wave is fully determined before any cover is revealed.
  All 53 pass a horizontal-reflection invariance gate (20,034 values over
  378 boards, 0 violations), which matters because the frozen search
  canonicalizes by mirror and could not use an orientation-dependent feature
  consistently.
- `approaches/lifetime-objective/suite-validation/structure.cpp` — labels a
  position with its exactly-achievable clear rate, averaged over J independent
  completions of **both** the hidden board and the future, so that the label is a
  function of the public position and is therefore the kind of thing a leaf
  feature could learn. It emits the frozen leaf's 19 features alongside, so "is
  this property already in the leaf?" is answerable by variance decomposition
  against the leaf's own span rather than by arguing about weights. Its window
  solver agrees with the frozen exact solver on 20/20 scenarios.
- `approaches/lifetime-objective/suite-validation/analyze_structure.py` — the
  whole-position held-out analysis, including a hand-checked map of each
  candidate property to its status in the frozen leaf (`present`, `related`,
  `computed but weight 0`, `absent`).

**One point for the coordinator.** The gate's stated rationale is that using a
benchmark which does not predict whole-game strength "would be worse than not
having it". That rationale does not obviously bind Task 2: the structure probe
**does not consume the suite**. It draws fresh positions from this work's own
lease, labels them with the exact solver, and never reads `suite-h9-v1`. The
gating is therefore a sequencing decision that may be worth revisiting; a probe
of ~1,000 positions at H = 8 with J = 4 completions costs roughly 20 minutes on
eight threads. It was not run because the instruction was explicit.

One thing the probe's construction already establishes, at no cost, and it is
worth recording because it is a fact about the frozen leaf rather than a result:
`kRoughnessWeight = 0.0` (`fair-only-horizon.cpp:69`). The leaf **computes**
column roughness at every node and multiplies it by zero. Any analysis that asks
"is this property in the leaf?" by looking at the extracted feature vector will
answer "yes" for roughness while the frozen search is in fact blind to it. The
analysis script therefore reports incremental fit over the leaf's scalar value
`fairLeaf(state)` as well as over its 19-feature span — the first is what the
search actually sees, the second is what a reweighting could recover.

## 7. Verdict

**Check 1 fails. The scenario suite, scored in fair position mode at H = 9,
does not measure policy strength and no number computed on it may be cited as
evidence of policy strength.** S9 = +0.633 against a preregistered threshold of
0.70, and S6 = −0.257 across the six policies whose ranking matters.

Stated with the direction of each claim explicit:

- **What the suite does measure** is the difference between a searching policy
  and a non-searching one. It resolves that at t = 8.5 and gets the three weak
  baselines in the right order. As a smoke test — "is this candidate doing
  anything at all?" — it is excellent and cheap.
- **What it does not measure** is anything finer. The largest whole-game gap in
  the comparator set, `d4s7` over `d2s7` at +133,204 points, registers as
  **minus four points at t = −0.01**.
- **The cause is arithmetic, not noise.** At H = 9 the row-rise bonus is 92% of
  the score and varies by 492 points across the six fair arms because the number
  of rises in a nine-move window is a function of `movesRemaining`, not of skill.
  The instrument's precision (624:1) makes this worse, not better: it would have
  produced a confident, reproducible, wrong ranking.
- **It is cheaper than whole games and that does not help.** 128 scenarios x 5
  tapes matches the resolving power of 64 paired whole games for 73% of the
  logical work. Resolving power on the wrong quantity is not a saving.
- **The failure is repairable and the repair is specific.** The same suite at
  H = 25 orders seven policies perfectly and does so consistently on both halves
  of the split and both position origins, where H = 9 disagreed with itself
  across all four slices. §5 lists the configuration a v2 check should
  preregister, and the compute it needs.

### What this changes for the research program

1. **`design-01`'s gate is now discharged with a number, and the number is a
   failure.** `finding-02` §5's policy comparisons — including its observation
   that "on the *harvested* subset D4 is actually the worst of the three" — are
   confirmed to be uninformative about policy strength, for a reason stronger
   than its own limitation 4 stated: not merely that one tape is noisy, but that
   the metric itself does not track the target at this horizon.
2. **Horizon is the first-class parameter of a scenario benchmark, and it was
   inherited from a constraint that does not apply.** H = 9 is the largest
   horizon `finding-02`'s *exact solver* can handle. Position mode does not use
   the solver. Carrying H = 9 into the fair benchmark imported the solver's
   limit into a place it had no business being, and that single choice is what
   broke check 1.
3. **Harvest positions from the policy family you intend to rank.** The
   `lowest-column`-harvested half of the suite gives S6 = −0.31; the synthetic
   half gives +0.60. `finding-02` limitation 6 anticipated this and it is now
   quantified.
4. **Check 2 should be run on every suite before any tuning, not after.** Its
   first application immediately showed the two halves disagreeing by 0.40 of
   Spearman at H = 9. Had a candidate been tuned on the development half, that
   disagreement would have been discovered as a failed replication instead of as
   a property of the instrument.

## Addendum A — Task 2: what predicts an achievable clear, and what the frozen leaf does with it

**Authorized by the coordinator after §6 was written**, on the ground that the
structure probe does not consume the suite: it draws its own positions from
`SEEDLEASE-A52-SUITE` offsets `0x1000`–`0x14a1`, labels them with the exact
solver, and never reads `suite-h9-v1`. §6's reasoning stands as recorded; this
addendum reports the result of running it.

Run: `runs/RUN-SUITE-9c41ab7e2d10/structure-h8-j4.csv`, **1,024 positions,
1,024/1,024 labelled, 1,198 s on 8 threads.**

### A.1 The label, and why it is a function of the public position

For each position the probe draws **J = 4 independent completions of everything
the position leaves open** — the hidden value under every covered cell *and* the
disc tape *and* every future risen row — and solves each completion **exactly**
for the maximum numbered discs clearable over an 8-move window
(`flow-solver.hpp`, clears objective, whose points mode agrees with the frozen
exact solver on 20/20 scenarios). The label is the mean over completions,
divided by the horizon:

> `achievableClears` = E over hidden boards and futures of (exact 8-move clear
> optimum) / 8

Averaging over the latent board is the step that matters. A label conditioned on
one particular hidden board is a function of information no leaf can see; the
expectation over hidden boards is a function of the **public** position, and is
therefore the kind of quantity a public leaf feature could in principle learn.
The solver is privileged; the label is a diagnostic; nothing here is a policy.

Positions span the occupancy range by construction: 384 harvested from `d2s5`
fair play, 192 from `lowest-column` play, 448 synthetic with controlled
occupancy, cover fraction and number profile. Analysis is on a **whole-position**
three-way split by content hash of the scenario id — 628 train / 193 validation /
203 held-out. Coefficients are fitted on train only; the greedy block is selected
on validation and reported on held-out, so no held-out number chose a term.

### A.2 External consistency, before anything is concluded

The two fair policies the probe plays on the same completions reproduce
`finding-07` §4's measured shape for fair D4, which is the check that the
labelling machinery is measuring the same game:

| occupied cells | n | **achievable** | `d2s5` achieved | `d3s5` achieved | fair D4, `finding-07` §4 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0–9 | 34 | 2.626 | 1.498 | 1.511 | 0.87 |
| 10–14 | 94 | 2.976 | 1.723 | 1.783 | 1.38 |
| 15–19 | 162 | 3.292 | 2.028 | 2.076 | 1.91 |
| 20–24 | 202 | **3.465** | 2.227 | 2.222 | 2.10 |
| 25–29 | 185 | 3.406 | 2.284 | 2.301 | 2.59 |
| 30–34 | 130 | 2.973 | 2.014 | 2.026 | 2.26 |
| 35–39 | 112 | 2.484 | 1.762 | 1.758 | 1.58 |
| 40–49 | 105 | 0.665 | 0.468 | 0.472 | 1.40 / 1.00 |

The fair arms rise to a peak near 25–29 cells and fall away past 30, which is
`finding-07`'s shape for D4. They sit slightly below D4 in the 25–34 bands, as
two shallower searches should. The bands are sampled positions rather than a
policy's own trajectory, so the columns are not matched samples and the 40–49
band is dominated by near-terminal boards.

### A.3 Occupancy does not explain it — the size of what is left

| model | held-out R² on `achievableClears` |
| --- | ---: |
| intercept only | −0.001 |
| **occupancy only** | **0.187** |
| occupancy + covered + numbered + movesRemaining | 0.233 |
| **the frozen leaf's scalar value `fairLeaf(state)`** | **0.396** |
| **the frozen leaf's 19 features, freely reweighted** | **0.734** |
| leaf 19 + the occupancy block | 0.736 |
| **all 53 candidate structural properties** | **0.753** |
| leaf 19 + the eight properties selected on validation | **0.770** |

Occupancy explains **18.7%** of the variance in the achievable clear rate. The
residual is not noise: within a single occupancy band the standard deviation of
the achievable rate runs from 0.33 clears per move at 0–9 cells to **1.58 at
35–39 cells** — larger than the entire 0.35–0.85 gap between fair D4 and the
requirement that `finding-06` and `finding-07` are chasing. Two boards with the
same number of occupied cells routinely differ by more than the whole research
target.

### A.4 The headline: the leaf is not missing features, it is pointing the wrong way

Read the table above again in the order that matters.

- Every linear model over **any** of the 53 public structural properties tops out
  at **0.753**.
- The frozen leaf's own 19 features, freely reweighted, reach **0.734** — 98% of
  that ceiling.
- The frozen leaf as it actually is — the scalar `fairLeaf(state)` the search
  sees at every leaf — reaches **0.396**.

**Reweighting the existing 19 features recovers +0.338 of held-out R², which is
95% of all the signal any of the 53 candidate properties can supply. Adding new
features on top of a reweighted leaf buys +0.036.** The frozen leaf's basis is
very nearly sufficient for predicting where clears are available. Its weight
vector is not using it.

Quantifying the misalignment directly. Each weight is scaled by its feature's
standard deviation over the corpus — leaf units per one sigma, the only sense in
which +180-per-open-column and −1250-per-danger-unit are comparable — and each
vector normalised to unit length, so what is compared is the **direction** the
leaf points in, not its arbitrary scale:

> **Cosine similarity between the frozen leaf's weight direction and the
> direction that best predicts the achievable clear rate: +0.141.** Against the
> achievable-minus-achieved gap it is **+0.007**.

Nearly orthogonal. Term by term, ordered by how much the fit wants each one:

| # | leaf feature | frozen weight | frozen, per σ (normalised) | fitted, per σ (normalised) | sign | univariate ρ |
| --: | --- | ---: | ---: | ---: | :---: | ---: |
| 5 | `numbered_cells` | −18 | −0.006 | **+0.601** | **FLIP** | +0.173 |
| 2 | `height_load` | −20 | −0.219 | **−0.600** | ok | −0.306 |
| 3 | `solid_cells` | −620 | −0.279 | +0.377 | **FLIP** | −0.429 |
| 10 | `solid_exposure` | +40 | +0.002 | **+0.184** | ok | **+0.627** |
| 18 | `rise_pressure` | −35 | **−0.572** | −0.162 | ok | −0.240 |
| 4 | `cracked_cells` | −220 | −0.023 | +0.150 | **FLIP** | −0.017 |
| 9 | `cracked_exposure` | +100 | +0.005 | +0.132 | ok | +0.453 |
| 17 | `roughness` | **0** | 0.000 | +0.097 | **zero** | **+0.401** |
| 13 | `dead_low_numbers` | −120 | −0.027 | −0.079 | ok | −0.299 |
| 11 | `adjacent_ones` | −550 | −0.043 | −0.075 | ok | −0.267 |
| 7 | `direct_potential` | +1,600 | +0.180 | +0.067 | ok | **+0.687** |
| 15 | `low_number_height_risk` | −85 | −0.132 | −0.061 | ok | −0.435 |
| 6 | `high_low_numbers` | −90 | −0.010 | −0.052 | ok | −0.487 |
| 12 | `triple_twos` | −750 | −0.012 | −0.045 | ok | −0.113 |
| 8 | `latent_chain_potential` | +700 | +0.053 | +0.041 | ok | +0.581 |
| 19 | `next_disc_vertical_options` | +220 | +0.018 | −0.024 | **FLIP** | +0.115 |
| 16 | `danger_height_squared` | −1,250 | −0.182 | −0.015 | ok | −0.266 |
| 1 | `open_columns` | +180 | +0.006 | +0.006 | ok | +0.397 |
| 14 | `covered_height_risk` | −95 | **−0.677** | +0.005 | **FLIP** | −0.335 |

**Read the two normalised columns as a pair.** The frozen leaf spends 78% of its
direction on `covered_height_risk` (−0.677) and `rise_pressure` (−0.572). The
fitted direction puts **+0.005 and −0.162** on those two. Meanwhile the fit's two
largest terms, `numbered_cells` (+0.601) and `height_load` (−0.600), carry
−0.006 and −0.219 of the frozen direction.

Three of the five sign disagreements — `solid_cells`, `cracked_cells`,
`covered_height_risk` — are **conditional only**: their univariate correlations
(−0.429, −0.017, −0.335) agree with the frozen sign, and the flip appears only
after partialling out the other eighteen terms. Those are suppression effects and
should not be read as "the leaf has the sign wrong". `numbered_cells` is
different: it is positive both univariately (+0.173) and conditionally (+0.601,
the largest fitted term), and the leaf charges it −18. Mechanically that is not
surprising — numbered discs are the only things that can clear — but the frozen
leaf treats them purely as crowding.

None of this says the leaf is a bad *survival* evaluator; `audit-02` §4.4 already
established it is a crowding evaluator and that this is directionally right. What
it says is narrower and sharper: **the quantity `finding-06` and `finding-07`
identify as the binding constraint — how many clears a board can actually yield —
is very nearly orthogonal to what the frozen leaf scores.**

### A.5 Ranked candidates, cheapest first

Ranked by held-out R² added, with the two targets reported separately:
`achievableClears` (what is available) and `clearGap` = achievable − `d3s5`
achieved (what a fair policy leaves on the table). The two agree on which family
matters, which is the strongest internal check available here.

#### Tier 1 — present in the leaf, weighted at or near zero. One constant each.

| candidate | frozen weight | share of frozen direction | fitted, per σ | univariate ρ | why it is cheap |
| --- | ---: | ---: | ---: | ---: | --- |
| **`roughness`** | **0.0** | **0.0%** | +0.097 | **+0.401** | `kRoughnessWeight = 0.0` (`fair-only-horizon.cpp:69`). The leaf computes Σ\|Δheight\| at every node and multiplies it by zero. A rough board offers more distinct run lengths for an arriving disc to match. **Changing one constant is the entire experiment** |
| **`solid_exposure`** | +40 | 0.2% | **+0.184** | **+0.627 — the strongest of all 19** | Present but effectively inert. It is the readiness of a solid cover's neighbours to pop, i.e. how close the board is to *opening* a cover, and the conservation law demands 1.400 reveals per move |
| **`cracked_exposure`** | +100 | 0.5% | +0.132 | +0.453 | Same family, one hit from opening rather than two |
| `numbered_cells` | −18 | 0.6% | **+0.601** | +0.173 | Sign is contradicted both univariately and conditionally |

#### Tier 2 — present and over-weighted relative to clear availability

| candidate | frozen weight | share of frozen direction | fitted, per σ | implication |
| --- | ---: | ---: | ---: | --- |
| `covered_height_risk` | −95 | **67.7%** | +0.005 | Two thirds of the leaf's direction is spent on a quantity carrying no conditional information about clear availability |
| `rise_pressure` | −35 | **57.2%** | −0.162 | Right sign, roughly 3.5× over-weighted relative to the fit |
| `danger_height_squared` | −1,250 | 18.2% | −0.015 | Right sign, roughly 12× over-weighted |
| `direct_potential` | +1,600 | 18.0% | +0.067 | Right sign, roughly 2.7× over-weighted — and the single strongest univariate term after `solid_exposure`, so the issue is its weight relative to the height block, not the term itself |

#### Tier 3 — genuinely absent, and all of one kind

| candidate | +R² over the leaf's 19, `achievableClears` | +R² over the leaf's 19, `clearGap` | univariate ρ (gap) | what it is |
| --- | ---: | ---: | ---: | --- |
| **`coversInWave`** | **+0.0149** | **+0.0271** | +0.445 | covers adjacent to a cell that **some legal drop of some disc value can pop** — covers a cracking wave can actually reach. Greedy step 1 on the gap target |
| **`landingOnCover`** | **+0.0203** | **+0.0307** | +0.312 | columns whose landing cell sits directly on top of a cover |
| `landingNextToNumber` | +0.0139 | +0.0202 | −0.071 | columns whose landing cell has a numbered neighbour |
| `coversInWaveShare` | +0.0021 | +0.0170 | **+0.573** | the same as a fraction of all covers |
| `crackedInWave` | +0.0048 | +0.0103 | +0.210 | the one-hit subset |

**Every genuinely absent property that predicts is cover-cracking geometry.**
Together the selected block adds **+0.036** on the achievable rate (0.734 → 0.770)
and **+0.043** on the gap (0.589 → 0.631). The leaf carries `solid_exposure` and
`cracked_exposure`, which are *readiness* quantities weighted at 0.2% and 0.5% of
its direction; it carries nothing that counts **how many covers a wave can
actually reach**, and nothing about where a dropped disc would land relative to
the cover structure.

This lands exactly where [`finding-07`](finding-07-fair-planning-ceiling.md) §6
point 3 predicted it would. That document's residual — the part of the
clairvoyant advantage a determinized planner structurally cannot capture — was
named as the inability "to act to *reduce its own uncertainty*", in a game whose
second conservation requirement is 1.400 covered reveals per move. Two
independent routes, one from a planner's behaviour and one from a regression on
board structure, arrive at the same missing quantity.

#### Not candidates, despite looking like the strongest

These have the largest univariate correlations of anything measured and are
absent from the leaf by name, yet add **nothing** over the leaf's 19-feature
span. The leaf's basis already contains them linearly. They are worth listing so
that nobody spends a week implementing one:

| property | univariate ρ | univariate R² | +R² over the leaf's 19 |
| --- | ---: | ---: | ---: |
| `highDiscs` (count of discs with value ≥ 5) | +0.582 | 0.320 | +0.0001 |
| `triggerPairs` / `expTriggerColumns` (the trigger map) | +0.568 | 0.318 | −0.0027 |
| `expBestFirstWave` (expected best first-wave size over a uniform next disc) | +0.542 | 0.232 | −0.0208 |
| `oneAwayPairs` | +0.542 | 0.255 | +0.0002 |
| `coverBuried` | −0.452 | 0.301 | −0.0000 |
| `meanValue` | +0.462 | 0.206 | −0.0002 |
| `minDistancePerDisc` | −0.420 | 0.297 | −0.0018 |

The trigger map is the interesting casualty. It is public, deterministic, and is
the most direct possible statement of "how many clears are available right now",
and it is entirely spanned by `direct_potential` plus the crowding terms. That is
a point in the frozen leaf's favour and is recorded as such.

### A.6 Robustness by position origin

Held-out R² of the leaf's 19 against the achievable rate, and of the leaf plus
the selected block, split by where the position came from:

| origin | n | leaf 19 only | leaf 19 + selected | gain |
| --- | ---: | ---: | ---: | ---: |
| harvested from `d2s5` fair play | 384 | 0.780 | 0.785 | +0.005 |
| harvested from `lowest-column` play | 192 | 0.760 | 0.761 | +0.001 |
| synthetic, controlled occupancy | 448 | 0.697 | 0.751 | **+0.054** |

The added cover-geometry block earns most of its keep **off** the fair-play
manifold. On positions a fair policy actually visits it adds +0.005. That is a
real caution: the fair-play manifold may simply not vary enough in cover
geometry for the term to matter where it would be used. The `clearGap` target
gives the same warning in stronger form — the block gains +0.029 on fair-play
positions but **−0.015** on `lowest-column` positions.

### A.7 What to test first

Ranked by expected information gain per unit of compute, which is what
`AGENTS.md` asks for:

1. **Set `kRoughnessWeight` to a non-zero value and sweep it.** One constant in a
   new parameterized arm, no new feature computation, no new search cost. The
   leaf already computes the number. If a term the frozen search has been
   multiplying by zero since inception is worth anything, this is the cheapest
   experiment in the repository.
2. **Sweep `solid_exposure` and `cracked_exposure` upward.** Both are present,
   both are at 0.2–0.5% of the leaf's direction, and `solid_exposure` has the
   strongest univariate correlation with achievable clears of any of the 19.
   Again: constants only.
3. **Re-fit the whole 19-vector against a survival-calibrated target and test the
   refit as a candidate.** The cosine similarity of +0.141 says the largest
   single available improvement is a weight vector, not a feature. This is
   `learned-leaf`'s existing machinery pointed at a target it has not used.
4. **Only then add `coversInWave` and `landingOnCover`** as new leaf terms.
   +0.036 held-out R² and +0.043 on the gap, but +0.005 on fair-play positions —
   worth testing, not worth assuming.

Every one of these is a bounded correction around fair D4 evaluated through the
public interface, which is the class `AGENTS.md` says to prefer. **None of them
is established as an improvement**: this addendum shows what predicts a
diagnostic label, not what raises a mean score. The distance between those two
is exactly what a preregistered SCREEN-tier experiment is for, and — per
Addendum B — that experiment should be paired whole games, not a scenario suite.

## Addendum B — Suite v2: build or retire?

**Verdict: do not build suite v2 as a screening or ranking instrument. Retire
that role. Keep the scenario machinery for the three jobs whole games cannot do
at all.**

The instruction was to weigh this properly and be willing to say it is not worth
building. It is not, and the reason is not the marginal 1.4x — it is two
measurements that only became available once the H = 25 arm existed.

### B.1 Does the 1.4x improve or worsen at H = 25? It worsens.

The 1.4x in §3 was a **single-pair** figure (`d3s7` vs `d4s7`) and it was
flattering, because that is a pair 64 whole games already resolve well. Doing it
for every fair pair, in logical work needed to reach t = 2 on each instrument:

`finding-05` publishes a paired 95% lower bound for five comparisons over 64
paired games, which pins the whole-game paired standard error:

| pair | delta | 95% lower bound | implied paired SE | t over 64 games |
| --- | ---: | ---: | ---: | ---: |
| `d4s7` − `d4s5` | +101,171 | +47,457 | 32,653 | +3.098 |
| `d3s7` − `d3s5` | +7,276 | −45,961 | 32,363 | +0.225 |
| `d4s7` − `d3s7` | +86,172 | +26,468 | 36,294 | +2.374 |
| `d3s5` − `d4s5` | +7,723 | −42,743 | 30,678 | +0.252 |
| `d3s7` − `d4s5` | +14,999 | −31,029 | 27,981 | +0.536 |

**The whole-game paired SE is 27,981–36,294, mean 31,994 — essentially
independent of which pair is compared.** That is the useful property of the
direct instrument: its precision is a constant, so the t it delivers is just the
effect size divided by 32,000.

The suite is not like that. At H = 25, over the four fair arms it could afford:

| pair | whole-game Δ | t over 64 games | suite Δ | suite t (128 x 4) | sign | work for t = 2: 64-game route | suite route | **suite / games** |
| --- | ---: | ---: | ---: | ---: | :---: | ---: | ---: | ---: |
| `d3s7` − `d3s5` | +7,276 | +0.227 | +890 | +2.058 | yes | 9.58e10 | 2.09e9 | **0.02** |
| `d2s7` − `d2s5` | +15,653 | +0.489 | +455 | +0.814 | yes | 5.92e8 | 3.97e8 | **0.67** |
| `d3s7` − `d2s5` | +62,686 | +1.959 | +1,362 | +2.134 | yes | 9.80e8 | 1.47e9 | **1.50** |
| `d3s7` − `d2s7` | +47,033 | +1.470 | +907 | +1.328 | yes | 1.75e9 | 3.85e9 | **2.20** |
| `d3s5` − `d2s5` | +55,410 | +1.732 | +472 | +0.745 | yes | 4.37e8 | 4.20e9 | **9.61** |
| `d3s5` − `d2s7` | +39,757 | +1.243 | +17 | +0.023 | yes | 8.65e8 | 4.42e12 | **5,103** |

All six signs are right — that is the H = 25 validity result from §5, and it
holds. But **the cost ratio has a median of 1.50 and spans 0.02 to 5,103.** The
suite is 50x cheaper on one pair and 5,000x more expensive on another, and
nothing observable in advance says which. That is not a screening instrument; it
is a lottery over screening instruments.

The reason is visible directly. How much of a whole-game gap does the suite
metric reproduce?

| pair | whole-game Δ | suite Δ at H = 25 | transfer ratio |
| --- | ---: | ---: | ---: |
| `d3s7` − `d3s5` | +7,276 | +890 | 0.1223 |
| `d2s7` − `d2s5` | +15,653 | +455 | 0.0291 |
| `d3s7` − `d2s5` | +62,686 | +1,362 | 0.0217 |
| `d3s7` − `d2s7` | +47,033 | +907 | 0.0193 |
| `d3s5` − `d2s5` | +55,410 | +472 | 0.0085 |
| `d3s5` − `d2s7` | +39,757 | +17 | 0.0004 |

**The suite-to-whole-game signal transfer varies by 283x across six pairs.** A
proxy whose slope against the target moves by a factor of 283 cannot size an
effect. It got the *ranking* right (§5) by ordering six numbers correctly, on a
four-policy validation. A rank correlation of +1.000 on n = 4 does not license
"this candidate is 3% better on the suite, therefore it is better in whole
games", and sizing effects is what a screening instrument is for.

### B.2 The arithmetic that makes cheapness impossible

Strip away the logical-work accounting and count **decisions taken**, which is
what any search-based policy actually pays for:

| policy | 64 paired games | suite, 128 positions x K = 4 at H = 25 | x K = 5 | ratio at K = 4 | ratio at K = 5 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `d2s5` | 4,864 | 10,701 | 13,376 | 2.20x | 2.75x |
| `d2s7` | 5,087 | 10,701 | 13,376 | 2.10x | 2.63x |
| `d3s5` | 5,750 | 10,701 | 13,376 | 1.86x | 2.33x |
| `d3s7` | 5,905 | 10,701 | 13,376 | 1.81x | 2.27x |
| `d4s5` | 5,578 | 10,701 | 13,376 | 1.92x | 2.40x |
| `d4s7` | 7,338 | 10,701 | 13,376 | 1.46x | 1.82x |

**A validity-preserving suite plays 1.5–2.8x more decisions than the 64-game
cohort it is meant to replace.** A 64-game fair-D4 cohort is only 5,600–7,300
moves; a fair policy's whole game is not long. A 25-move scenario is nearly a
quarter of one, and §3's resolving-power arithmetic says you need 512–640 of
them. The two requirements collide:

> **the horizon that makes the suite valid is the horizon that makes it
> expensive.** At H = 9 it is 0.58x the cost of the cohort and it ranks the fair
> arms backwards (S6 = −0.257). At H = 25 it ranks them correctly and costs
> 1.5–2.8x. There is no setting where it is both cheap and valid.

This is not a fixable engineering detail. It follows from §2's mechanism: the
metric only carries survival information once the horizon is long enough for the
17,000-point rise term to vary, and "long enough for lifetime differences to
show" is a large fraction of the lifetime itself.

### B.3 The non-cost reasons, weighed honestly

The instruction asked whether reusability, a sealed half, low variance on small
effects, or measuring things whole games cannot might justify it at parity cost.
Taking them one at a time:

| claimed advantage | verdict |
| --- | --- |
| **Reusable across many future candidates** | **No advantage.** A fixed `STANDARD` 64-game development cohort is equally reusable and measures the target directly. `docs/benchmarks.md` already defines the tier |
| **A fixed sealed half that resists overfitting** | **No advantage.** The tier ladder already provides this through fresh development and protected cohorts, and it provides it on the real quantity. The split manifest is good hygiene, not a reason for a second instrument |
| **Lower variance for small effects** | **Real but unbankable.** On `d3s7` vs `d3s5` — an effect 64 whole games cannot see at all (t = 0.225) — the suite reaches t = 2.058 for 2% of the work. That is a genuine 50x. But on `d3s5` vs `d2s7` it is 5,103x worse, and B.1 shows there is no way to predict which case you are in |
| **Measuring what whole games cannot** | **Real, and the only surviving justification — but it is diagnosis, not screening** |

That last row is where the value actually is, and it is not the value the suite
was pitched on. Four things need a fully-specified scenario and cannot be got
from whole games at any price:

1. **A well-posed clairvoyant optimum.** The exact solver needs every future
   quantity fixed. This is what closed audit-01 M2 and it is the foundation of
   `finding-06` and `finding-07`.
2. **Controlled occupancy and cover fraction**, including regimes real play never
   reaches. `finding-02` §6's sparse probe answered "is a board clear reachable
   at all?" — a question a whole-game cohort could not answer because it never
   produces the event.
3. **A hidden board a teacher can inspect**, which is what makes any oracle or
   distillation experiment well posed at all.
4. **Paired position families** — one start position, many futures — which is
   exactly what `finding-07`'s fair planner and this work's structure probe
   (Addendum A) are built on.

None of these require the suite to rank policies. All of them survive retiring
that claim.

### B.4 What to do instead, and what happens to the artifacts

**For cheap screening, use paired whole games and score them on flow, not on
score.** `docs/benchmarks.md`'s `SCREEN` tier (32 paired games, common random
numbers) already exists and measures the target. The improvement worth making is
free and needs no new instrument: **report numbered clears per move and the
occupancy slope as the early statistic, with score as the confirming one.**
`finding-01` derives that score is 94% row-rise bonus and correlates with
lifetime at r = 0.9995; `finding-06` §2 calls the occupancy slope "the cleanest
single diagnostic in this document" and shows it is the conservation law
integrated. The statistical point is that score's independent unit is the whole
game — n = 64, SE 32,000 on a mean of 300,000 — while a flow rate is pooled over
every move of every game, n ≈ 5,600 for a 64-game depth-4 cohort. In this work's
own position-mode data the between-policy to within-policy variance ratio is 824
for clears per move against 624 for points, the only direct comparison available
here. **This is a recommendation with a mechanism, not a measured result**, and
it is cheap to test: it requires re-analysing per-game records that already
exist, opening no new seed.

**The artifacts:**

| artifact | disposition |
| --- | --- |
| `suite-h9-v1.jsonl` (128 positions, exact optima, PVs, difficulty labels) | **Keep, reclassified as a diagnostic corpus.** It is already load-bearing: `flow-run --cross-check` validates against it and `finding-06` §1 replays its principal variations. Its policy-comparison columns (`fairDepth1/2/4`, `bestShallow`, `gap`) should carry a pointer to §2 of this document |
| `sparse-probe-h9-v1.jsonl` | **Keep unchanged.** It answers a question whole games cannot, and `finding-06` §5 already uses it correctly |
| `data/suite-h9-v1-split-v1.json` | **Keep.** It cost nothing, it is content-hashed, and any future corpus — including the structure probe's train/held-out split — should follow the same rule. Retarget it as the split for diagnostic reuse rather than for tuning a ranked candidate |
| `posmode.{hpp,cpp}` (position mode) | **Keep the instrument, retire the claim.** It is the repository's only fair multi-tape evaluator and the only thing that can measure a public policy's behaviour at a controlled occupancy. It should not be used to rank candidates |
| The idea of a v2 ranking suite | **Retire.** B.1 and B.2 |

### B.5 If you build it anyway

Should a coordinator overrule this, the configuration below is what a v2 check 1
must preregister, and this is its honest price. It is included so the decision is
made against a number rather than an impression.

- H = 25 or longer; K >= 5; 128 positions;
- positions harvested from **fair-policy** play, not `lowest-column` play
  (§2 measures that choice at 0.91 of Spearman on the fair block);
- all six fair arms, including both depth-4 configurations — `d4s5` is the
  discriminating case because whole-game means are not monotone in depth;
- the same content-hashed split rule, regenerated for the new position set;
- a preregistered pass rule on **S6**, not S9. §2 shows S9 can reach +0.7 purely
  by separating search from no-search.

| arm | logical work per cell | total at 128 x 5 | core-hours |
| --- | ---: | ---: | ---: |
| `d2s5` | 41,814 | 2.68e7 | 0.02 |
| `d2s7` | 86,654 | 5.55e7 | 0.04 |
| `d3s5` | 1,094,867 | 7.01e8 | 0.6 |
| `d3s7` | 3,231,471 | 2.07e9 | 1.6 |
| `d4s5` | 26,903,263 *(extrapolated)* | 1.72e10 | 13.7 |
| `d4s7` | 98,594,555 *(extrapolated)* | 6.31e10 | 50.1 |
| **total** | | **8.3e10** | **66** |

**66 core-hours** at the 350,000 logical-work-per-core-second this machine
delivers — 8.3 hours on eight threads at full efficiency, about 12 hours at the
5.3-of-8 throughput actually observed under contention. Plus the fair-play
position harvest. The depth-4 arms are 97% of it, and they are not optional,
because they are the only arms whose whole-game ordering is non-monotone.

For comparison, running the same six arms as a fresh 64-game paired cohort costs
4.5e10 logical work — **54% of the v2 suite** — and measures the qualification
target directly.

## Limitations

1. **The H = 9 arm is 95 of 128 positions**, stopped by its preregistered
   90-minute budget. The completed set is an unbiased hash-ordered subsample
   (48 harvested / 47 synthetic, 45 development / 50 sealed), and the verdict was
   stable from n = 13 through n = 95, but the last 33 positions were not played.
2. **K = 4 tapes**, reduced from a preregistered 16 on measured cost before any
   points row was read (Amendment 1). The within-policy variance in §3 is
   therefore a two-tape versus two-tape split, which is a noisy estimate of a
   noise term.
3. **Nine policies is a small n for a Spearman.** S9 = 0.70 was chosen in advance
   as the threshold precisely because n = 9 admits no finer statement. The p
   values are exact permutation p values, so they are honest, but they cannot
   distinguish "the suite is uninformative" from "the suite is weakly
   informative" — only the S6 = −0.26 with four wrong signs does that.
4. **The nine-policy comparison is cross-cohort.** Six arms share
   `0xa51d1000`–`0xa51d103f`; the three baselines come from a different 64-seed
   cohort. S6 is the clean within-cohort statement and it is the worse of the two.
5. **The `d2s5` whole-game mean (249,641) was supplied by the coordinator** and
   does not appear in `docs/` or `research/` in this checkout. Every other
   whole-game figure is traceable to `finding-05` or `finding-01`. If that figure
   is wrong, S6 changes; S9's verdict does not, because `d2s5` and `d2s7` are
   adjacent in both orderings.
6. **§5 is post hoc**, omits both depth-4 arms, and rests on n = 4 for the fair
   block. It is a diagnosis and a design proposal, not a passing check 1.
7. **The H = 25 tapes are not nested inside the H = 9 tapes.** `retapeAt` draws
   the disc tape first and the risen rows afterwards, so a longer tape shifts the
   rise draws. The two arms share positions and tape *streams* but are
   independent futures, which makes them independent samples rather than nested
   ones.
8. **The latent randomness model is not the base engine's model** (audit-01 M2,
   `finding-02` §2), inherited. Scenario scores are not comparable to any ledger
   figure. What is compared here is a *ranking* produced inside the scenario model
   against a *ranking* produced in the base engine, which is exactly the
   comparison check 1 asks for, but the two score scales are not interchangeable.
9. **Absolute scenario scores inherit audit-01 H1 and H2** (the forfeited level
   bonus on a terminating rise, and the double board-clear award). H2 is inert
   here: **zero board clears occurred in any of 7,004 scenario plays**, matching
   `finding-01`, `finding-02` §6 and `finding-06` §5.
10. **No timing claim is made.** Every wall time was measured on a machine shared
    with another contributor's session at load averages of 45–50, using at most 8
    threads. All cost comparisons in §3 are in logical work, which is exact and
    machine-independent.
11. **§6 records Task 2 as not run under the preregistered gate.** It was
    subsequently authorized and is reported in Addendum A. §6 is left unedited
    because the reasoning that led to holding is part of the record.
12. **Addendum A's target is a clairvoyant label.** The exact 8-move clear
    optimum, averaged over hidden boards and futures, is what is *available*, not
    what a legal policy can take. A feature that predicts availability is a
    hypothesis about a better leaf, not a demonstration of one. Nothing in
    Addendum A is evidence that any mean score would rise.
13. **Addendum A is a linear analysis.** Held-out R² of a linear model bounds
    what a *linear* leaf can extract. A property that adds nothing linearly could
    still matter non-linearly, and the frozen leaf is linear only by choice.
14. **H = 8 and J = 4 in the structure probe.** The label is an 8-move window,
    which `finding-06` §1 shows over-states sustainable flow because a line
    ending at the horizon need not pay for the structure it spent. Four
    completions per position is a noisy expectation; the per-position label
    standard error is not separated from the between-position variance anywhere
    in Addendum A.
15. **The added cover block earns most of its keep off the fair-play manifold**
    (+0.054 on synthetic positions, +0.005 on `d2s5`-harvested ones, and −0.015
    on `lowest-column` positions for the gap target). That is the one result in
    Addendum A that argues against its own conclusion, and it is A.6.
16. **Addendum A's per-feature increments are 53 comparisons on one held-out
    split.** Each feature is pre-specified rather than selected, and the greedy
    block is chosen on validation and reported on held-out, but the ranked table
    should be read as a screen, not as 53 independent tests.
17. **Addendum B's depth-4 costs are extrapolated**, not measured: the two
    depth-4 arms were never run at H = 25, and their per-cell cost is their
    measured H = 9 work per move times 20.9 moves. The `d2s5` whole-game work
    per move is also an estimate (5/7 of `d2s7`'s, which `finding-05` reports).
18. **Addendum B's whole-game standard error is one number for all pairs.**
    `finding-05` publishes it for five comparisons (27,981–36,294) and B.1 uses
    the mean, 31,994, for pairs it does not publish.
19. **A model contribution record under `research/contributions/` is owed and was
    not written**, because this work was scoped to create files only under
    `approaches/lifetime-objective/suite-validation/` and `docs/exploratory/`.
    The coordinator should add one (level `L3` for position mode, the validation
    protocol and this result). The same debt is open for `finding-02`,
    `finding-06` and `finding-07`.

## Reproduce

```sh
# build (clang++ explicitly; the Makefile's CXX ?= clang++ loses to make's
# builtin CXX=g++, which trips a false -Werror=array-bounds in
# src/core/native/public-behavior.hpp)
./approaches/lifetime-objective/scenario/build.sh
./approaches/lifetime-objective/suite-validation/build.sh

# gates
./build/scenario/scenario-parity --seeds 4096
./build/suite-validation/posmode --self-test \
    --suite approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl --tapes 4
./build/suite-validation/structure --self-test

RID=RUN-SUITE-9c41ab7e2d10

# Check 2 first: the split is fixed before anything is measured on it
python3 approaches/lifetime-objective/suite-validation/split.py \
    approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl \
    approaches/lifetime-objective/suite-validation/data/suite-h9-v1-split-v1.json

# Check 1, as preregistered.  Rows stream as each position finishes and
# positions are processed in content-hash order, so a run stopped by the
# resource budget leaves an unbiased subsample.  This one was stopped at 90
# minutes with 95 of 128 positions complete.
./build/suite-validation/posmode \
    --suite approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl \
    --horizon 9 --tapes 4 --threads 8 --jsonl runs/$RID/posmode-h9-k4.jsonl

# the post-hoc horizon diagnostic (11 minutes; omits the two depth-4 arms)
./build/suite-validation/posmode \
    --suite approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl \
    --horizon 25 --tapes 4 --threads 8 \
    --policies d2s5,d2s7,d3s5,d3s7,center-first,random-legal,lowest-column \
    --jsonl runs/$RID/posmode-h25-k4.jsonl

for F in posmode-h9-k4 posmode-h25-k4; do
  python3 approaches/lifetime-objective/suite-validation/analyze.py \
      runs/$RID/$F.jsonl \
      --split approaches/lifetime-objective/suite-validation/data/suite-h9-v1-split-v1.json
done

# Addendum A - the structure probe (1,024 positions, 1,198 s on 8 threads).
# It draws its own positions and never reads the suite.
./build/suite-validation/structure --fair 384 --weak 192 --synthetic 448 \
    --horizon 8 --completions 4 --threads 8 --time-limit 20 \
    --csv runs/$RID/structure-h8-j4.csv
python3 approaches/lifetime-objective/suite-validation/analyze_structure.py \
    runs/$RID/structure-h8-j4.csv
python3 approaches/lifetime-objective/suite-validation/leaf_weights.py \
    runs/$RID/structure-h8-j4.csv

# the achievable-minus-achieved gap target: add the derived column, then rerun
python3 - <<'EOF'
import csv
src = "runs/RUN-SUITE-9c41ab7e2d10/structure-h8-j4.csv"
dst = "runs/RUN-SUITE-9c41ab7e2d10/structure-h8-j4-gap.csv"
rows = list(csv.DictReader(open(src)))
names = list(rows[0]); names.insert(names.index("d3Clears") + 1, "clearGap")
out = csv.DictWriter(open(dst, "w", newline=""), fieldnames=names); out.writeheader()
for r in rows:
    r["clearGap"] = round(float(r["achievableClears"]) - float(r["d3Clears"]), 6)
    out.writerow(r)
EOF
python3 approaches/lifetime-objective/suite-validation/analyze_structure.py \
    runs/$RID/structure-h8-j4-gap.csv --target clearGap
python3 approaches/lifetime-objective/suite-validation/leaf_weights.py \
    runs/$RID/structure-h8-j4-gap.csv --target clearGap
```

### Artifacts

Under `runs/RUN-SUITE-9c41ab7e2d10/`, hashed in `MANIFEST.sha256`:

| file | rows | SHA-256 |
| --- | ---: | --- |
| `posmode-h9-k4.jsonl` | 3,420 | `b9ab8563b0caf8341adf8ff0e6896f77b8801f1caaaa54609b016c76d2395d9d` |
| `posmode-h25-k4.jsonl` | 3,584 | `764b1cdd6ac08bbbed82f31e49d912a5ea0a9ff43852f3079644fa9d3f08d737` |
| `analysis-h9-k4.txt` | — | `1b2b2390ef20f4b191b3fb75fc83313ab99298b7ccd0d7332b148d5d4debd52f` |
| `analysis-h25-k4.txt` | — | `c4b467d0b62f09879545375a82072802ca3ad99b69d1daeb5c2e2fd63dffae02` |
| `structure-h8-j4.csv` (Addendum A) | 1,024 | `b939a3d7868aca05ea9ff594a4535bbb55736b10f7c1fce99e5b41059a6bb92e` |
| `structure-h8-j4-gap.csv` | 1,024 | `0dd553ebc20b2d08a8595123a33c3b1ac7ece12e2dce5de167f2eb08a73df777` |
| `analysis-structure-clears.txt` | — | `fab653e4732dee2cec9bb7f3ef4cecca0c2a5d3dce03a4c4b09e024fad7d42a0` |
| `analysis-structure-gap.txt` | — | `c19268ae3cb88526025da911e41b0c6cb561116988a7abebe3f36d1912b5c6ae` |
| `analysis-leaf-weights-clears.txt` | — | `fbed0132797c819e741260da16bb6b8d5bea32cbfca85f95bdcf175cffaf58e2` |
| `analysis-leaf-weights-gap.txt` | — | `cf46ceed290af52bd7f628d716eb46d19e45c21f7aa6cb7dd0c1209f7ce13a76` |

Each `structure-h8-j4.csv` row is one position: its id, origin, the exact
achievable clear/point/reveal labels, what `d2s5` and `d3s5` achieved on the same
completions, 53 candidate structural properties, and the frozen leaf's 19
features plus its scalar value. Each `posmode` JSONL row is one (position, tape,
policy) evaluation carrying the scenario
id, the re-taped scenario's own content-hash id, the position's origin, occupancy
before and after, points, moves, numbered clears, covered reveals, rises, maximum
chain depth, death and board-clear flags, the first column chosen, and the
logical work spent. Every table in this document is derived from those two files
by `analyze.py`.

### Seed lease

`SEEDLEASE-A52-SUITE` = `0xa5258000`–`0xa525bfff`, a sub-range of the
`SEEDLEASE-A52` reserve recorded in [`lease-map.md`](lease-map.md). Role:
**exploratory development diagnostic**; once read these seeds are development
data permanently. Both programs refuse to draw outside the lease.
**`lease-map.md` was not edited by this work** — it is an existing shared file
and another contributor's session was writing to it concurrently. The
coordinator should add this row when merging.

| Offset | User | Opened |
| --- | --- | --- |
| `0x0000`–`0x007f` | CHECK gates (comparator parity, reflection, solver agreement) | `0x0000`–`0x0002`, `0x0020`–`0x005f`, `0x0060`–`0x0077` |
| `0x0080` | policy randomization stream for `random-legal` | yes |
| `0x0100`–`0x01ff` | position-mode tape streams, one lease seed per tape index | `0x0100`–`0x0103` |
| `0x1000`–`0x1fff` | structure-probe position generation | `0x1000`–`0x14a1` (Addendum A) |
| `0x2000`–`0x2fff` | structure-probe completion sampling | `0x2000`–`0x2003` (Addendum A) |
| `0x3000`–`0x3fff` | reserve | not opened |

`suite-h9-v1` was consumed as already-minted data from `SEEDLEASE-A51D-SCEN`; no
new seed was drawn from that lease. **No protected or final seed was opened or is
justified by anything here.**

### Environment

AMD clang 23.0.0git, `-O3 -std=c++20 -pthread -Wall -Wextra`. 16 physical cores /
32 logical, shared with another contributor's session throughout at load averages
of roughly 45–50; **at most 8 threads were used by this work**, and the run was
observed taking about 5.3 of the 8 it asked for. Frozen sources were consumed
unmodified and their hashes are recorded at build time in
`build/suite-validation/sources.sha256`. Two of them end in a real `int main` and
are compiled from build-tree copies whose only changed line is the entry point;
`build.sh` refuses to proceed if more than that one line differs, and the copies
live exactly three directories below the repository root so every `../../../`
relative include inside them still resolves to the untouched original tree.
