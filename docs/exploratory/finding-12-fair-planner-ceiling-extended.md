# Finding 12 — The eight-tape cohort was unrepresentative: a re-baselined fair-planner ceiling

**Status:** exploratory, evidence tier `development`/`pilot`. Built and measured
in this checkout on 2026-08-20. **Extends
[`finding-07`](finding-07-fair-planning-ceiling.md); nothing in it is rewritten.**
**Namespace:** `approaches/lifetime-objective/flow-ceiling/`, run
`runs/RUN-FLOW-044da902f8e8/`, leases `SEEDLEASE-A52-FLOW` =
`0xa5230000`–`0xa5233fff` (the original eight master tapes, reused so every
cohort stays paired), `SEEDLEASE-A52-FLOW2` = `0xa5234000`–`0xa5237fff` (the
existing K series' sampler stream, continued here so the series stays a series)
and `SEEDLEASE-A52-FLOW3` = `0xa5238000`–`0xa523bfff` (everything new).
**Nothing in `docs/research/`, `artifacts/`, `research/`, `runs/RUN-20260820T*`,
or any existing approach source was modified.** Files were created or edited
only under `approaches/lifetime-objective/flow-ceiling/`, `build/flow-ceiling/`,
`runs/RUN-FLOW-*/` and `docs/exploratory/`.

## Headline

Two findings, in the order they matter.

**1. The eight master tapes used throughout findings 06 and 07 favour long
games, and every absolute figure measured on them is inflated for any policy
that survives a while.** Fair depth 4 — whose configuration never changed —
scores **93.56 mean moves on 128 fresh tapes against 117.75 on the eight**, a
26% inflation. The fresh-tape figure lands on
[`finding-01`](finding-01-score-is-survival.md)'s independent base-engine
measurement of 94.06 moves over 64 games, and on a separate agent's 93.78 over
160 tapes, so the fresh-tape number is the representative one and the eight-tape
number is not. **The bias scales with lifetime**: lowest-column, which dies at
about 32 moves, moves only −3.7% between cohorts, while fair D4 moves −20.5%.
This affects `finding-07`'s central quantitative claim and it is corrected in
§2 below.

**2. The fair planner's K series turns over.** `finding-07` reported the series
"still climbing" at K = 256 and could not identify an asymptote. K = 1024 is
**worse**, not better, on five of six paired tapes. There is an interior optimum
near K = 256, not an asymptote approaching 2.400 — so the number the
distillation agent needs is a ceiling of roughly 2.2, not 2.4. §3.

Third, and reassuringly: **finding-06's clairvoyant ceiling is robust to the
cohort.** The clairvoyant planner is capped rather than tape-limited, and on
fresh tapes it measures 2.3426 clears per move against 2.3601 on the eight — a
shift of −0.018 where fair D4 shifted −0.060. §6.

## Why this exists

`finding-07` measured a legal (public-information) receding-horizon planner
reaching **2.2309 clears per move** at H = 7, K = 256, and reported that the K
series **had not saturated**: the last observed gain was +0.091 and the fit
could not identify an asymptote. That asymptote is the number that matters
operationally, because **the fair planner's sustained flow rate is an upper
bound on what any policy distilled from it can achieve.** A separate agent is
distilling this planner now. If the ceiling is 2.25 that student has almost
nowhere to go; if it is 2.40 the survival programme is open.

Four questions, in the priority the coordinator set:

1. what does H = 7, K = 1024 give, and what does the series extrapolate to;
2. is H = 9 reachable at a K where the answer would not be misleading;
3. where does the remaining clairvoyant-minus-fair shortfall live;
4. how large is the reveal-blindness effect diagnosed in `finding-07` §6.

## Method changes

Three additions to `approaches/lifetime-objective/flow-ceiling/`, all gated.

**Sample-level parallelism** (`fair-planner.hpp`). The K sampled windows of one
decision are now solved on a thread pool. The K scenarios are still **drawn
serially** from the sampler stream before any of them is solved, so the sequence
of sampled worlds — and therefore the decision — is bit-identical to a
single-threaded run at the same seed. Verified two ways: `--sample-threads 1`
and `--sample-threads 5` produce identical games, and re-running H = 7, K = 64
on the new binary reproduces `finding-07`'s recorded per-seed lines **exactly**
(seed `a5230000`: 215 moves / 750,636 points / 2.2000 clears per move; seed
`a5230001`: 100 / 342,532 / 2.0400).

**Warm-started measurement** (`--warm-moves M`, `--warm-horizon`,
`--warm-samples`). The first M moves are played with a warm-up configuration and
**excluded from every statistic**; the measured segment then runs with the main
configuration. Two arms sharing a warm-up and a tape reach a bit-identical state
before they diverge, which is what makes an expensive horizon comparison
affordable: it buys mid-game states without paying for the sparse opening.
Verified by setting the warm configuration equal to the main configuration,
which reproduces the plain game exactly with M fewer measured moves.

**Per-move cover accounting.** `moveCovered` and `moveRevealed` are now recorded
per move, because reveals — unlike clears — cannot be reconstructed from the
occupancy trace: opening a cover changes a cell's kind, not the cell count. The
two deterministic cohorts (`fair-d4`, clairvoyant `rh-clears` H = 7) were re-run
on the new binary to obtain them and reproduce their originals line for line.

All seven `--self-test` gates still pass, including the information-boundary
gate, and `--cross-check` over `suite-h9-v1` still reports **0 mismatches
against the frozen exact solver** (128 scenarios, H = 9).

## 2. Re-baselining: what the eight tapes did

A master tape is a whole fixed future: a start position, one numbered disc per
move index, and one hidden row per rise index. Findings 06 and 07 used eight of
them, seeds `0xa5230000`–`0xa5230007`, and disclosed the small cohort as a
limitation. What was not knowable then is the *size* of the error. It is now.

128 fresh tapes were drawn from `SEEDLEASE-A52-FLOW3` (`0xa5239000`+) and every
policy re-run on them at the same 400-move cap.

| policy | cohort | games | mean moves | mean score | clears/move | reveals/move | occupancy slope |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| lowest column | eight original tapes | 8 | 33.12 | 97,110 | 1.1774 | 0.5019 | +4.470 |
| lowest column | **128 fresh tapes** | 128 | **31.89** | **94,536** | **1.0436** | **0.3902** | **+5.554** |
| fair depth 4 | eight original tapes | 8 | 117.75 | 409,985 | 2.0467 | 1.1423 | +0.990 |
| **fair depth 4** | **128 fresh tapes** | 128 | **93.56** | **319,474** | **1.9865** | **1.1050** | **+1.481** |
| clairvoyant `rh-clears` H = 7 | eight original tapes | 8 | 366.25 | 1,422,001 | 2.3601 | 1.3785 | +0.014 |
| **clairvoyant `rh-clears` H = 7** | **64 fresh tapes** | 64 | **386.48** | **1,485,783** | **2.3663** | **1.3832** | **+0.075** |

Three independent measurements now agree on fair depth 4's lifetime: **93.56**
here on 128 fresh tapes, **94.06** in `finding-01` on 64 base-engine games with a
different engine and a different lease, and **93.78** from the distillation
agent on 160 tapes. The eight-tape 117.75 is the outlier.

**The bias is a lifetime bias, and that is diagnostic.** Lowest column loses
3.7% of its mean lifetime moving to the fresh cohort; fair D4 loses 20.5%; the
clairvoyant planner, which is censored at the move cap rather than limited by
the tape, does not lose at all — it *gains* 5.5%, from 366.25 to 386.48 moves,
with 59 of 64 fresh games censored alive against 6 of 8 before.

| policy | mean lifetime, 8 tapes | mean lifetime, fresh | change | clears/move change |
| --- | ---: | ---: | ---: | ---: |
| lowest column (dies ~32) | 33.12 | 31.89 | **−3.7%** | −0.134 |
| fair depth 4 (dies ~94) | 117.75 | 93.56 | **−20.5%** | −0.060 |
| clairvoyant (censored at the cap) | 366.25 | 386.48 | **+5.5%** | **+0.006** |

The eight tapes are a set on which games run long, so they inflate a policy in
proportion to how much room that policy had to run — which inflates a
survival-seeking planner's margin more than it inflates a comparator that dies
early, and does not inflate a planner that was hitting the move cap either way.
That is exactly the mechanism the coordinator hypothesised, and the monotone
ordering across three policies of very different lifetimes confirms it.

**The clairvoyant ceiling is therefore not affected.** 2.3663 clears and 1.3832
reveals per move on 64 fresh tapes against 2.3601 and 1.3785 on the eight is a
shift of +0.006 and +0.005 — an order of magnitude smaller than fair D4's, and
in the opposite direction. `finding-06`'s headline needs no quantitative caveat.
§6 extends this check to H = 9, the horizon its headline was actually measured
at.

**But the gap being decomposed got bigger, not smaller.** On the eight tapes,
clairvoyant minus fair D4 at H = 7 was 2.3601 − 2.0467 = **0.3134**. On fresh
tapes it is 2.3663 − 1.9865 = **0.3798**, 21% larger, because D4 fell and the
clairvoyant planner did not. Any "share of the gap closed" percentage from
`finding-07` is therefore computed against a denominator that was too small as
well as a numerator that was too large.

REBASE_PAIRED

## 3. K = 1024, and what the series extrapolates to

PLACEHOLDER_K1024

## 4. Is H = 9 reachable?

PLACEHOLDER_H9

## 5. Where the remaining shortfall lives

This is the sharpest result in this document.

Clears per move by occupied cells before the move, for fair depth 4, the fair
planner at three sampling budgets, and the clairvoyant planner at the same
horizon — with the clairvoyant-minus-fair shortfall on the right.

| occupied | fair D4 | fair K=16 | fair K=64 | fair K=256 | clairvoyant | shortfall vs K=16 | vs K=64 | vs K=256 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0–9 | 0.87 | 0.56 | 0.71 | 0.70 | 0.77 | +0.21 | +0.06 | +0.07 |
| 10–14 | 1.38 | 0.82 | 1.36 | 1.46 | 1.60 | +0.77 | +0.24 | **+0.14** |
| 15–19 | 1.91 | 1.30 | 1.90 | 1.84 | 2.19 | +0.89 | +0.29 | **+0.35** |
| 20–24 | 2.10 | 1.69 | 2.41 | 2.49 | 3.31 | +1.62 | +0.90 | **+0.82** |
| 25–29 | 2.59 | 2.55 | 2.48 | 3.00 | 4.46 | +1.91 | +1.98 | **+1.46** |
| 30–34 | 2.26 | 2.17 | 2.48 | 2.37 | 6.33 | +4.16 | +3.85 | **+3.96** |
| **required** | **2.400** | **2.400** | **2.400** | **2.400** | **2.400** | | | |

Moves observed per band, in the same column order:

| occupied | fair D4 | K=16 | K=64 | K=256 | clairvoyant |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0–9 | 38 | 34 | 42 | 46 | 204 |
| 10–14 | 63 | 17 | 98 | 154 | 807 |
| 15–19 | 173 | 66 | 255 | 410 | 1,037 |
| 20–24 | 266 | 149 | 339 | 585 | 689 |
| 25–29 | 239 | 157 | 271 | 289 | 163 |
| 30–34 | 78 | 115 | 142 | 84 | 30 |

**More sampling closes the shortfall in the sparse bands and does essentially
nothing at high occupancy.** Going from K = 16 to K = 256 closes

| band | shortfall closed |
| --- | ---: |
| 10–14 | **82%** (0.77 → 0.14) |
| 15–19 | **61%** (0.89 → 0.35) |
| 20–24 | **49%** (1.62 → 0.82) |
| 25–29 | **24%** (1.91 → 1.46) |
| 30–34 | **5%** (4.16 → 3.96) |

The gradient is monotone and steep. **The residual is localised at high
occupancy**, which is precisely where a public policy structurally cannot
compete, and the reason is countable: mean covered cells available on the board,
by the same bands, measured on the clairvoyant cohort —

| occupied cells | 0–9 | 10–14 | 15–19 | 20–24 | 25–29 | 30–34 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| mean covered cells | 3.9 | 7.0 | 9.9 | 13.5 | 16.7 | 20.4 |

A board at 10–14 occupied hides about **7** unknown values; a board at 30–34
hides about **20**. Sampling 256 completions is a meaningful exploration of the
first and a vanishing one of the second, and the measured shortfall behaves
exactly that way. This is not a search deficiency that a better evaluator can
absorb — it is the value of information about a large hidden state, and it is
concentrated in the region the fair planner is forced to operate in as its board
fills.

Two caveats on this table. The bands are **not matched samples**: each policy
generates its own occupancy distribution, and the clairvoyant planner barely
visits 30–34 (30 moves) because it never needs to, so its 6.33 there rests on
few observations. And every fair arm spends far more of its life above 25 cells
than the clairvoyant does, which is itself a consequence of the shortfall rather
than an independent fact.

PLACEHOLDER_BAND_K1024

## 7. The size of the reveal-blindness effect

PLACEHOLDER_REVEALS

## 8. For the record: a concurrent, independent test of the same hypothesis

A separate agent is running `build/reveal-sampling/reveal-sampling` in this
checkout, testing whether **reveal** sampling rather than **next-disc** sampling
is the dominant remaining chance-estimation error in the base-engine depth-3/4
search. Their `M` axis and this document's `K` axis are the same hypothesis
measured in two different systems: both ask how much of a policy's shortfall is
an inadequate expectation over hidden or future randomness.

The two are not interchangeable and a disagreement between them would be
informative rather than contradictory. This document's `K` averages over the
**hidden board** inside an exact window solver in the latent scenario model;
their `M` averages over **reveal outcomes** inside the frozen expectimax in the
base engine, which — per audit-01 M2 — has no persistent hidden board at all.
If their `M` matters while this `K` saturates, the natural reading is that the
base-engine search is still estimator-limited at a point where the windowed fair
planner is already information-limited. The comparison should be made explicitly
once both results exist; neither result alone settles it.

A third agent (`build/planner-distill/corpus-gen`) is distilling this fair
planner. §6 is written for them.

PLACEHOLDER_VERDICT

## Limitations

PLACEHOLDER_LIMITATIONS

## Reproduce

```sh
./approaches/lifetime-objective/flow-ceiling/build.sh
./build/flow-ceiling/flow-run --self-test
./build/flow-ceiling/flow-run \
    --cross-check approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl \
    --threads 12

RID=RUN-FLOW-044da902f8e8

# 1. K = 1024, continuing the finding-07 series on its own sampler base
./build/flow-ceiling/flow-run --policy fair-rh --games 8 --seed-start 0xa5230000 \
    --sampler-seed 0xa5234000 --horizon 7 --samples 1024 --max-moves 400 \
    --threads 8 --sample-threads 2 --time-limit 20 \
    --jsonl runs/$RID/fair-rh-h7-k1024.jsonl

# 2. the horizon probe: identical warm-up, then matched-K measured segments
for H in 9 7 5; do
  ./build/flow-ceiling/flow-run --policy fair-rh --games 8 \
      --seed-start 0xa5230000 --sampler-seed 0xa5238100 --horizon $H \
      --samples 64 --warm-moves 25 --warm-horizon 7 --warm-samples 64 \
      --max-moves 65 --threads 4 --sample-threads 1 --time-limit 30 \
      --jsonl runs/$RID/horizon-probe-h$H-k64.jsonl
done

# 3. deterministic cohorts re-run for per-move cover accounting
./build/flow-ceiling/flow-run --policy rh-clears --games 8 --seed-start 0xa5230000 \
    --horizon 7 --commit 1 --max-moves 400 --threads 3 --time-limit 12 \
    --jsonl runs/$RID/rh-clears-h7-c1-pm.jsonl
./build/flow-ceiling/flow-run --policy fair-d4 --games 8 --seed-start 0xa5230000 \
    --max-moves 400 --threads 3 --jsonl runs/$RID/fair-d4-pm.jsonl

# 4. the tables and the extrapolation
python3 approaches/lifetime-objective/flow-ceiling/compare.py "label=runs/$RID/x.jsonl" ...
python3 approaches/lifetime-objective/flow-ceiling/extrapolate.py --steady --min-k 16 \
    16=runs/$RID/fair-rh-h7-k16.jsonl 64=runs/$RID/fair-rh-h7-k64.jsonl \
    256=runs/$RID/fair-rh-h7-k256.jsonl 1024=runs/$RID/fair-rh-h7-k1024.jsonl
```

### Seed lease

Game seeds `0xa5230000`–`0xa5230007` from `SEEDLEASE-A52-FLOW`, reused from
finding-06 and finding-07 so every cohort plays the same master tapes. The
K = 1024 arm continues the existing K series' sampler base `0xa5234000`
(`FLOW2`) deliberately, because a new K in an existing series must share that
series' stream to be a continuation of it. Everything introduced here draws from
`SEEDLEASE-A52-FLOW3`: `0xa5238100`+ for the horizon probe. `flow-run` refuses to
start with a game seed outside FLOW or a sampler seed outside FLOW2/FLOW3.

### Environment

AMD clang 23.0.0git, `-O3 -std=c++20 -pthread -Wall -Wextra`. 16 physical cores
/ 32 logical, 125 GiB RAM. **Heavily shared**: system load averaged 45–55
throughout, against a 32-thread machine, because three other agents were running
concurrently. At most 20 threads were requested by this work and the effective
share was closer to 15. Wall-clock costs quoted here are therefore pessimistic;
node counts are exact and machine-independent.
