# Finding 07 — Separating planning from information: at most 41% of the clairvoyant advantage is hidden knowledge

**Status:** exploratory, evidence tier `development`/`pilot`. Built and measured
in this checkout on 2026-08-20.
**Namespace:** `approaches/lifetime-objective/flow-ceiling/`, run
`runs/RUN-FLOW-044da902f8e8/`, leases `SEEDLEASE-A52-FLOW` =
`0xa5230000`–`0xa5233fff` (game seeds, deliberately reused so every cohort is
paired with [`finding-06`](finding-06-flow-ceiling.md)) and
`SEEDLEASE-A52-FLOW2` = `0xa5234000`–`0xa5237fff` (the hidden-board sampler).
**Nothing in `docs/research/`, `artifacts/`, `research/`, `runs/RUN-20260820T*`,
or any existing approach source was modified by this work.** Files were created
or edited only under `approaches/lifetime-objective/flow-ceiling/`,
`build/flow-ceiling/`, `runs/RUN-FLOW-*/` and `docs/exploratory/`.

> **CORRECTION NOTICE (added after publication).** The eight master tapes this
> document is measured on are **unrepresentative**. They favour long games, and
> the bias scales with how long a policy survives.
> [`finding-12`](finding-12-fair-planner-ceiling-extended.md) re-ran every arm on
> 128 fresh tapes: fair depth 4, whose configuration never changed, drops from
> **117.75 to 93.56 mean moves** (the fresh figure matching finding-01's
> independent 94.06 and a separate agent's 93.78), while the clairvoyant planner
> — censored at the move cap on both cohorts — is unaffected.
>
> Consequently the **central quantitative claim of this document is withdrawn as
> stated**: "fair planning closes 58.8% of the clairvoyant-minus-D4 gap" becomes
> **27.1%** at H = 7 on a matched fresh cohort (22.2% at H = 5), because D4's
> baseline was inflated while the clairvoyant ceiling was not, making the true
> gap 21% larger and the fair planner's share of it roughly half what is
> reported below. The summary line "at least 59% planning, at most 41%
> information" is **withdrawn**; the corrected reading is that at most about 27%
> is recovered by fair planning of this kind.
>
> Also superseded: §1's reading that the K series was "still climbing" at
> K = 256. K = 1024 is **worse** on five of six paired tapes — the series has an
> interior maximum near K = 256, not an asymptote.
>
> What stands unchanged: the method, the information-boundary gate, the arm A
> result that the future disc tape is worth nothing, the finding that every fair
> game ends (reinforced — 0 of 160 fresh-tape fair games survived), and the
> qualitative conclusion that the residual is concentrated where the hidden state
> is large. The original text below is left exactly as written.

## What this corrects in finding-06

[`finding-06`](finding-06-flow-ceiling.md) §3 ends with this sentence, which is
left in place there and is **not supported by that document's evidence**:

> The gap between the two is a *control* gap.

finding-06's planner is privileged twice over. It plans exactly over a nine-move
window **and** it reads `latent[]`, so it knows which covered disc holds which
number and therefore knows exactly which covers are worth cracking. finding-06
never separated those two privileges, so it could not attribute the gap.

This document separates them, and the sentence turns out to be **directionally
right but unproven, and the correct statement is narrower**: at a matched
horizon a *legal* planner closes **58.8%** of the clairvoyant-minus-D4 gap, so
**at most 41.2% of it is hidden information** — and that 41.2% is an upper
bound that was still falling when the compute budget ran out. Knowing the future
disc tape is worth nothing.

What is genuinely weakened is a different finding-06 claim: that its ceiling is
a *target*. **No legal policy tested here reached flow balance.** All 87
fair-planner games ended, none reached the move cap, and every fair arm's board
filled monotonically. finding-06's other conclusions — the conservation arithmetic, the
clairvoyant ceiling, the occupancy-equilibrium mechanism, the score composition
and the wave-depth results — stand as measured.

## The question

Two research programs follow, and they are incompatible:

- if the gap is mostly **planning**, a learned evaluator plus search can chase
  it and finding-06's ~2.40 clears per move is a reachable target;
- if the gap is mostly **hidden information**, the ceiling for any legal policy
  is far below 2.400 and the survival program is capped no matter how good the
  search gets.

## Method

### The fair planner

`approaches/lifetime-objective/flow-ceiling/fair-planner.hpp` is finding-06's
`rh-clears` with exactly one thing removed: access to hidden state. At each move
it draws `K` independent completions of everything it is not allowed to know,
solves the horizon-`H` window **exactly** against each completion, and plays the
column with the best mean value. Common random numbers: every candidate column
inside one decision is scored against the same `K` completions, via
`solveWindowRoot`, which returns the exact value of every legal root move under
one completion.

Three arms, so the privileges come off one at a time:

| arm | hidden board `latent[]` | future disc tape and risen rows | legal policy? |
| --- | --- | --- | --- |
| `rh-clears` (finding-06) | known | known | no |
| **arm A** | **sampled** | known | no |
| **arm B** | **sampled** | **sampled** | **yes** |

Arm B reads exactly what `docs/methodology.md` permits a deployable policy to
read: the visible board, the visible next disc, and the moves remaining before
the next rise. Structurally it is never handed the future — `sampleWindow` only
touches the master tape when `tape_known` is set.

Everything else is held fixed against finding-06: same objective (maximize
numbered discs cleared), same exact window solver, one move committed per
decision, and the same master tapes, so every comparison below is paired at the
seed.

### What this bounds, and what it does not

The fair planner is a **determinized / hindsight-optimization (PIMC)**
controller. Inside a sample it assumes the drawn hidden values will turn out to
be true, which makes its value estimates optimistically biased and exposes it to
the two classical PIMC pathologies, strategy fusion and non-locality. It does
**not** compute the true POMDP optimum and is not claimed to. Consequently:

- its flow rate is a **lower bound** on what a legal policy can sustain;
- the share of the gap it closes is a **lower bound on the planning share**, and
  the residual is an **upper bound on the information share**;
- sampling variance is a first-order effect, not a nuisance. `finding-05` found
  that at depth 4 moving from five to seven chance strata is worth +101,171
  points while at depth 3 it is worth nothing measurable — chance-estimator
  exactness and planning depth are complements. `K` is the same axis, so it is
  swept over three orders of magnitude and the saturation point is reported
  rather than assumed. That precaution mattered: see §1.

### Validation

`flow-run --self-test` grew three gates for this work on top of finding-06's
four. All seven pass.

| gate | what it proves |
| --- | --- |
| root-move values agree with the window optimum | `solveWindowRoot`, the primitive the fair planner is built on, returns exact per-column values whose maximum equals the window optimum, under both objectives |
| **INFORMATION BOUNDARY: the fair planner ignores hidden state** | over 20+ consecutive real decisions, replacing **every** hidden value under **every** covered cell *and* substituting a completely different future disc tape and rise sequence leaves the chosen column **identical**, given the same sampler stream |
| the privileged arm reproduces the clairvoyant optimum | with `K = 1` and both privileges restored, the planner's chosen column carries exactly the clairvoyant window optimum, so the two code paths are one planner |

`flow-run --cross-check` over `suite-h9-v1` (128 scenarios, H = 9) still reports
**0 mismatches against the frozen exact solver** after the solver changes.

## 1. The K sweep — and why a small-K answer would have been wrong

Arm B, eight master tapes, seeds `0xa5230000`–`0xa5230007`, 400-move cap. Pooled
clears per move over all moves of all games.

| H | K = 1 | K = 4 | K = 16 | K = 64 | K = 256 | K = 1024 | clairvoyant at this H |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 5 | — | — | 2.0413 | 2.0373 | **2.1680** | 2.1415 | 2.3496 |
| 7 | 1.5370 | 1.7095 | 1.9279 | 2.1403 | **2.2309** | — | 2.3601 |

Three things this table settles.

**A small-K answer would have inverted the finding.** At K = 8 in a pilot, and at
K = 16 here, the fair planner is *worse than fair depth 4* (1.9279 against
2.0467) and the natural conclusion would have been "the gap is information". By
K = 256 it is 2.2309, comfortably better than D4 and 93.0% of the requirement.
Between K = 1 and K = 256 at H = 7 the planner moves 0.694 clears per move —
more than twice the entire clairvoyant-minus-D4 gap being decomposed.

**H = 5 saturates; H = 7 has not.** The H = 5 row is flat from K = 256 to
K = 1024 (2.1680 vs 2.1415, eight games each, difference well inside noise), so
that horizon is planning-limited rather than sample-limited above K ≈ 256. The
H = 7 row is still climbing at K = 256 (+0.091 over K = 64, against +0.212 from
16 to 64 — decelerating, not flat). **The fair planner's true ceiling was not
reached.** H = 7 at K = 1024 would have cost roughly six hours on the available
threads and was not run; H = 9 at any non-artifactual K was far out of budget
(§Limitations 3).

**Chance-estimator exactness and planning depth are complements**, exactly as
`finding-05` found on the strata axis. At K = 16, H = 5 (2.0413) *beats* H = 7
(1.9279): with coarse sampling, more lookahead makes strategy fusion worse. By
K = 64 the ordering reverses (2.1403 against 2.0373) and by K = 256 H = 7 leads
by 0.063. Deeper fair planning is only worth having once the chance estimate can
support it.

## 2. The arms, and the decomposition

All at H = 7 on the same eight master tapes unless noted. `steady` excludes the
first 25 moves, the sparse opening no steady state contains (finding-06 §3).

| cohort | mean moves | censored | mean score | clears/move | reveals/move | steady clears/move | occupancy slope | mean occupancy |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| lowest column | 33.12 | 0 / 8 | 97,110 | 1.1774 | 0.5019 | — | +4.47 | 27.13 |
| fair depth 4 | 117.75 | 0 / 8 | 409,985 | 2.0467 | 1.1423 | 2.1213 | +0.990 | 23.91 |
| arm A, tape known, K = 64 | 119.38 | 0 / 8 | 411,365 | 2.1361 | 1.2157 | 2.2437 | +0.749 | 21.92 |
| arm B, fully fair, K = 64 | 155.00 | 0 / 8 | 542,357 | 2.1403 | 1.2145 | 2.2135 | +0.833 | 23.45 |
| arm B, H = 5, K = 256 | 128.75 | 0 / 8 | 446,805 | 2.1680 | 1.2398 | 2.2855 | +0.583 | 22.12 |
| arm A, tape known, K = 256 *(7 of 8 games; see §2)* | 112.57 | 0 / 7 | 395,474 | 2.0761 | 1.1764 | — | — | — |
| **arm B, fully fair, K = 256** | **200.88** | 0 / 8 | 723,384 | **2.2309** | **1.2782** | **2.2950** | **+0.311** | 21.38 |
| `rh-clears`, clairvoyant | 366.25 | **6 / 8** | 1,422,001 | **2.3601** | **1.3785** | **2.3963** | **+0.014** | 16.89 |
| **requirement** | ∞ | 8 / 8 | | **2.400** | **1.400** | **2.400** | **0.000** | |

### The decomposition

Against fair depth 4 as baseline and the clairvoyant planner **at the same
horizon** as ceiling, in whole-game clears per move:

| component | value | share of the total gap |
| --- | ---: | ---: |
| total gap, clairvoyant H = 7 − fair D4 | 0.3134 | 100% |
| **closed by fair planning alone** (arm B H = 7 K = 256 − D4) | **0.1842** | **58.8%** |
| closed by knowing the future disc tape | ≈ 0 (see below) | **≈ 0%** |
| **not closed: hidden board, plus PIMC's own suboptimality** | **0.1292** | **41.2%** |

The same decomposition at H = 5, where *both* planners are saturated in their
respective compute axes, gives fair planning 0.1213 of a 0.3029 gap — **40.0%
closed, 60.0% unattributed.** The two horizons bracket the answer: the
information share is **at most 41% at the horizon where fair planning was pushed
hardest, and the number falls as the fair planner is given more compute.**

**Knowing the future is worth nothing.** Arm A is handed the exact future disc
tape and the exact hidden values of every future risen row. At K = 64 it
sustains 2.1361 against arm B's 2.1403 — a difference of −0.004, i.e. nothing.
At K = 256 arm A is *worse*: 2.0761 against arm B's 2.2393 on the same seven
seeds (arm A's eighth game was still running after 1 h 51 m of wall
clock when the resource budget for this work closed and was stopped; it is
excluded from both sides of that paired comparison. The excluded game is the
slowest-running one, so the arm A figure may be pessimistic).
This is not a claim that future knowledge is *harmful*; it is the well-known
result that a non-Bayesian determinized planner is not monotone in information.
Fixing the tape removes diversity from the K completions, which sharpens
strategy fusion: every sample plans against the same future, so the planner
commits to a specific line instead of choosing a move that is good on average.
The safe reading is that **the future disc tape is not the source of the
clairvoyant planner's advantage; the board under the covers is.**

**What is *not* attributable, stated plainly.** The 41.2% residual is an upper
bound on the value of hidden information, not a measurement of it. It contains
two things this experiment cannot separate:

1. genuine hidden information — value no legal policy can ever recover; and
2. the suboptimality of hindsight optimization itself. A PIMC planner is
   structurally blind to the value of *revealing a cover in order to learn*,
   because averaging over determinizations assumes the answer is already known.
   In a game whose second conservation requirement is 1.400 covered reveals per
   move, that is likely a material part of the residual.

Since the fair planner had not saturated at H = 7, item 2 is known to be
non-zero and the honest statement is **"at least 59% planning, at most 41%
information, and the planning share can only go up with better fair methods."**

## 3. The occupancy trend — no fair planner found a fixed point

finding-06 §2's decisive diagnostic, applied to the fair arms.

| policy | occupancy slope, cells per five-move cycle | outcome |
| --- | ---: | --- |
| lowest column | +4.47 | dead in 7 cycles |
| fair depth 4 | +0.990 | 0 / 8 survive, mean 117.75 moves |
| arm B, H = 7, K = 16 | +1.269 | 0 / 8 survive, mean 76.25 moves |
| arm B, H = 7, K = 64 | +0.833 | 0 / 8 survive, mean 155.00 moves |
| arm B, H = 5, K = 256 | +0.583 | 0 / 8 survive, mean 128.75 moves |
| **arm B, H = 7, K = 256** | **+0.311** | **0 / 8 survive, mean 200.88 moves** |
| `rh-clears`, clairvoyant H = 7 | **+0.014** | **6 / 8 reach the 400-move cap alive** |

**Every fair arm has a strictly positive occupancy slope and every fair game
ended.** Across **87 fair-planner games** — 72 arm-B games at nine (H, K)
settings plus 15 arm-A games — not one reached the move cap alive. The longest
single fair game was 332 moves; the clairvoyant planner ran 6 of 8 games into a
400-move cap and, in finding-06, 6 of 6 into a 1,000-move cap.

But the slope is falling fast and monotonically with sampling: +1.269 → +0.833 →
+0.311 as K goes 16 → 64 → 256 at H = 7, a factor of four over the range, with
mean lifetime rising 76 → 155 → 201 moves. **The trend has not flattened.** This
document therefore does **not** establish a flow-balance ceiling for legal
policies; it establishes a measured floor of 2.2309 clears per move (93.0% of
the requirement) and a trajectory that was still improving when the budget ran
out.

## 4. Clears per move by occupancy — the requested column

finding-06 §3's table with the fair arms added. `n` is the number of moves
observed in that band; bands are not matched samples across policies.

| occupied cells | fair depth 4 | arm B H=7 K=64 | **arm B H=7 K=256** | arm A H=7 K=64 | arm B H=5 K=256 | clairvoyant H=7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 0–9 | 0.87 *(38)* | 0.71 *(42)* | 0.70 *(46)* | 0.74 *(38)* | 0.90 *(39)* | 0.77 *(204)* |
| 10–14 | 1.38 *(63)* | 1.36 *(98)* | 1.46 *(154)* | 1.62 *(103)* | 1.35 *(93)* | 1.60 *(807)* |
| 15–19 | 1.91 *(173)* | 1.90 *(255)* | 1.84 *(410)* | 2.04 *(230)* | 1.87 *(234)* | 2.19 *(1037)* |
| 20–24 | 2.10 *(266)* | 2.41 *(339)* | **2.49** *(585)* | 2.29 *(276)* | 2.44 *(308)* | **3.31** *(689)* |
| 25–29 | 2.59 *(239)* | 2.48 *(271)* | **3.00** *(289)* | 2.59 *(177)* | 2.50 *(218)* | **4.46** *(163)* |
| 30–34 | 2.26 *(78)* | 2.48 *(142)* | 2.37 *(84)* | 2.26 *(94)* | 2.35 *(104)* | **6.33** *(30)* |
| 35–39 | 1.58 *(45)* | 1.96 *(57)* | 1.21 *(24)* | 1.89 *(27)* | 2.82 *(34)* | — |
| 40–44 | 1.40 *(30)* | 1.76 *(25)* | 2.25 *(8)* | 2.00 *(10)* | — | — |
| 45–49 | 1.00 *(10)* | 0.82 *(11)* | 0.71 *(7)* | — | — | — |
| **required** | **2.400** | **2.400** | **2.400** | **2.400** | **2.400** | **2.400** |

This is the clearest statement of the result in the document.

**Fair planning buys a real restoring force, in exactly the band that matters.**
At 20–24 cells arm B at K = 256 clears 2.49 against D4's 2.10; at 25–29 it
clears 3.00 against 2.59. Both are above the 2.400 requirement, so a legal
planner does have a negative-feedback region around its 21.4-cell operating
point. That is the mechanism behind its 200.88-move mean lifetime.

**But the restoring force is an order of magnitude weaker than the clairvoyant's,
and the shortfall grows with occupancy.** Clairvoyant minus best-fair, by band:
+0.35 at 15–19, +0.82 at 20–24, +1.46 at 25–29, +3.96 at 30–34. **A fuller board
holds more covered discs, so knowing their values is worth more — the advantage
scales with exactly the quantity that hidden information should scale with.** It
is the one qualitative signature in this data that points at information rather
than search.

**And the fair planner's basin is too shallow to hold.** Its margin over the
requirement is 0.09–0.60 clears per move in the bands it occupies, against the
clairvoyant's 0.91–3.93. Above 35 cells it collapses to 1.21 and cannot get
back. A bad sequence walks it out of the basin and it does not walk back — which
is why every fair game ends and no clairvoyant game did.

## 5. For the record: fair D4 is not too height-averse

The coordinator asked for this, because it closes a hypothesis cheaply. Mean
occupied cells over every move of every game, identical master tapes:

| policy | mean occupancy, all moves | mean occupancy, moves 26+ |
| --- | ---: | ---: |
| lowest column | 27.13 | 38.03 |
| `rh-points`, clairvoyant score-seeking | 25.47 | 26.29 |
| **fair depth 4** | **23.91** | **25.63** |
| arm B, H = 7, K = 64 | 23.45 | — |
| arm B, H = 7, K = 256 | 21.38 | — |
| arm B, H = 5, K = 256 | 22.12 | — |
| **`rh-clears`, clairvoyant, H = 9** | **17.93** | **17.98** |
| `rh-clears`, clairvoyant, H = 9, cap 1,000 | 17.81 | 17.87 |

**Fair D4 runs a board six cells fuller than the clairvoyant planner's
equilibrium and still extracts less at every occupancy band.** It is not dying
from excessive height aversion — it is not conservative, it is ineffective. Any
candidate built on "make D4 keep the board emptier" is addressing the wrong
variable: D4 would have to clear more *at the height it already plays at*. Note
also that better fair planning moves occupancy in the right direction on its
own — 23.45 at K = 64 down to 21.38 at K = 256 — without any height term in the
objective.

## 6. Verdict

**Cannot yet determine whether a legal policy can reach flow balance. The
evidence moved substantially toward "yes", and the information share of the gap
is bounded above by 41%.**

- The best legal planner measured sustains **2.2309 clears and 1.2782 reveals
  per move** — 93.0% and 91.3% of the 2.400 / 1.400 requirement — against fair
  depth 4's 2.0467 / 1.1423 and the clairvoyant planner's 2.3601 / 1.3785 at a
  matched horizon.
- **Fair planning closes 58.8% of the clairvoyant-minus-D4 gap** at H = 7 and
  40.0% at H = 5, where both sides are saturated. Knowing the future disc tape
  closes none of it. The residual — **at most 41.2%** — is hidden board *plus*
  the known suboptimality of hindsight optimization, and this experiment cannot
  split those two.
- **Every fair game ended.** 87 games across nine arm-B settings and two arm-A
  settings, none reached the move cap, best mean lifetime 200.88 moves against
  the clairvoyant's 366.25 with 6 of 8 censored alive.
- **Every fair arm's board filled**, but the slope falls fast with sampling:
  +1.269 → +0.833 → +0.311 cells per cycle as K goes 16 → 64 → 256 at H = 7,
  against the clairvoyant's +0.014. The trend had not flattened when compute ran
  out.

So finding-06's ceiling is not obviously out of reach for a legal policy, but it
was not reached here, and the honest position is that the fair-planner frontier
is **still moving** and its limit is unmeasured.

### What this changes for the research program

1. **Report `K` — or any chance-estimator budget — as a first-class result.**
   At H = 7 the difference between K = 1 and K = 256 is 1.5370 vs 2.2309 clears
   per move, more than twice the entire gap under study. A fair sampling planner
   reported at small K is reporting an artifact, and this experiment would have
   reached the opposite conclusion had it stopped at K = 16.
2. **finding-06's distillation plan is weakened but not dead.** Up to 41% of the
   teacher's margin is not transferable in principle, so a student fitted to
   clairvoyant labels is partly being asked to learn a function of hidden state —
   the `sibling extrapolation` failure `docs/research/status.md` records, by a
   new route. Distilling the *fair* planner instead has no such defect and is
   now the better-posed target: it is legal by construction, it already beats D4
   by 0.18 clears per move, and it costs 256 exact window solves per move, which
   is exactly the kind of expense a learned evaluator exists to amortise.
3. **The residual has a name, and it is actionable.** The one thing a PIMC
   planner structurally cannot do is act to *reduce its own uncertainty*. Drop7
   demands 1.400 covered reveals per move by conservation, and every fair arm
   here under-delivers on reveals by about the same fraction as on clears. A
   fair planner that values *cracking covers in order to learn* — an explicit
   information term, or a belief-state search — is a bounded change to the
   objective rather than a new family, and it targets precisely the part of the
   residual that is not genuine hidden information.
4. **Do not spend compute on making D4 keep a lower board.** §5.

## Limitations

1. **Hindsight optimization is not the optimal fair planner**, and this is the
   dominant limitation. Every "information share" number is an upper bound and
   every "planning share" a lower bound.
2. **The K axis is not saturated at H = 7.** The headline 2.2309 and 58.8% were
   still improving at the budget boundary. H = 5 is saturated and gives 40.0%;
   the truth for a fully-resourced fair planner is at or above the larger of the
   two.
3. **The fair planner could not be run at H = 9**, the horizon of finding-06's
   headline clairvoyant number. A fair decision costs K window solves and an
   H = 9 window costs 2.92 s against H = 7's 0.079 s, so H = 9 at a
   non-artifactual K was far outside budget. All decompositions are stated at a
   matched horizon and never against finding-06's H = 9 figure.
4. **Eight games per arm, 87 fair games in total.** Small cohorts. Paired master
   tapes remove most between-game variance from the comparisons, but per-arm
   means carry wide intervals — visible in the K = 16 vs K = 64 pairing, where
   seven of eight seeds improved and one collapsed from 75 moves to 35.
5. **Arm A is a diagnostic, not a clean measurement of tape value.** A
   determinized planner is not monotone in information, so arm A's result
   supports "the tape is not the source of the advantage" and not a signed
   valuation of tape knowledge. Its K = 256 cohort is also 7 of 8 games: the
   eighth was stopped, still running, when the resource budget closed, and the
   arm A vs arm B comparison at that K is therefore reported paired over the
   seven seeds both arms completed.
6. **Objective mismatch with fair D4.** The comparator maximizes its frozen
   terminal utility over a depth-4 expectimax; the fair arms maximize numbered
   discs cleared over an H-move exact window. They differ in more than
   information, so "fair planning closes 58.8%" describes *these* planners.
7. **The latent randomness model is not the base engine's model** (audit-01 M2),
   inherited from finding-06. Scores are not comparable to any ledger figure.
8. **The occupancy-band table is descriptive, not causal.** Counts are given for
   that reason.
9. **The timings are not timing-grade.** Shared machine, load averages 20–40, at
   most 12 threads. No performance claim is made.
10. **A model contribution record under `research/contributions/` is owed and was
    not written**, because this work was scoped to create files only under
    `approaches/lifetime-objective/flow-ceiling/` and `docs/exploratory/`. The
    same debt is open for finding-02 and finding-06.

## Reproduce

```sh
./approaches/lifetime-objective/flow-ceiling/build.sh

# gates, including the information-boundary gate
./build/flow-ceiling/flow-run --self-test
./build/flow-ceiling/flow-run \
    --cross-check approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl \
    --threads 12

RID=RUN-FLOW-044da902f8e8

# arm B, fully fair: the K sweep at H = 7 and H = 5
for K in 1 4 16 64 256; do
  ./build/flow-ceiling/flow-run --policy fair-rh --games 8 \
      --seed-start 0xa5230000 --sampler-seed 0xa5234000 --horizon 7 \
      --samples $K --max-moves 400 --threads 6 --time-limit 20 \
      --jsonl runs/$RID/fair-rh-h7-k$K.jsonl
done
for K in 16 64 256 1024; do
  ./build/flow-ceiling/flow-run --policy fair-rh --games 8 \
      --seed-start 0xa5230000 --sampler-seed 0xa5234100 --horizon 5 \
      --samples $K --max-moves 400 --threads 6 --time-limit 20 \
      --jsonl runs/$RID/fair-rh-h5-k$K.jsonl
done

# arm A: the future disc tape is known, the hidden board is not
for K in 64 256; do
  ./build/flow-ceiling/flow-run --policy fair-rh --games 8 \
      --seed-start 0xa5230000 --sampler-seed 0xa5234200 --horizon 7 \
      --samples $K --tape-known --max-moves 400 --threads 6 --time-limit 20 \
      --jsonl runs/$RID/fair-rh-armA-h7-k$K.jsonl
done

# the tables
python3 approaches/lifetime-objective/flow-ceiling/compare.py \
    "fair-d4=runs/$RID/fair-d4.jsonl" \
    "armB H7 K64=runs/$RID/fair-rh-h7-k64.jsonl" \
    "armB H7 K256=runs/$RID/fair-rh-h7-k256.jsonl" \
    "armA H7 K64=runs/$RID/fair-rh-armA-h7-k64.jsonl" \
    "armB H5 K256=runs/$RID/fair-rh-h5-k256.jsonl" \
    "clairvoyant H7=runs/$RID/rh-clears-h7-c1.jsonl"
```

### Seed lease

Game seeds are `0xa5230000`–`0xa5230007` from `SEEDLEASE-A52-FLOW`, deliberately
**reused** from finding-06 so both documents' cohorts play the same master tapes
and every comparison is paired. Those seeds were already opened by finding-06 and
are development data permanently.

The hidden-board sampler is a separate stream from `SEEDLEASE-A52-FLOW2` =
`0xa5234000`–`0xa5237fff`, one sub-stream per game, partitioned by arm:
`0xa5234000`+ arm B at H = 7, `0xa5234100`+ arm B at H = 5, `0xa5234200`+ arm A.
`flow-run` refuses to start with a sampler seed outside FLOW2 or a game seed
outside FLOW.

### Environment

AMD clang 23.0.0git, `-O3 -std=c++20 -pthread -Wall -Wextra`. 16 physical cores
/ 32 logical, 125 GiB RAM, shared with other jobs throughout; at most 12 threads
were used. Frozen sources consumed unmodified, hashes recorded at build time in
`build/flow-ceiling/sources.sha256`.
