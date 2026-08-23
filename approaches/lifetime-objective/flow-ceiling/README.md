# flow-ceiling

Measures whether the disc-conservation requirement of
[`docs/exploratory/finding-01-score-is-survival.md`](../../../docs/exploratory/finding-01-score-is-survival.md)
— **2.400 numbered clears and 1.400 covered reveals per move** — is reachable
by any line of play, or whether Drop7 Hardcore is a strictly losing battle in
which every game necessarily ends.

**Answer: it is reachable.** A clairvoyant receding-horizon planner with a
nine-move exact lookahead sustains 2.3875 clears and 1.3963 reveals per move
over 6,000 moves — 2.4023 clears per move over the second half of those games,
which is at the requirement — holds board occupancy flat at ~20 of 49 cells for
200 rise cycles, and reached the 1,000-move cap alive in 6 of 6 games. Fair
depth 4 on identical futures sustains 1.98 and dies at a mean of 117.75 moves.

**But that planner is privileged twice over**. It plans exactly over a window
*and* it reads the hidden board. `fair-planner.hpp` removes the two privileges
one at a time. A **legal** receding-horizon planner (arm B: hidden board and
future both sampled, `K` determinizations per decision, exact solve of each)
reaches 2.2309 clears and 1.2782 reveals per move at H = 7, K = 256 — 93.0% of
the requirement — closing 58.8% of the clairvoyant-minus-D4 gap. Knowing the
future disc tape closes none of it. Every fair game still died.

**Both of those numbers are measured on eight master tapes, which are
unrepresentative. They favour long games.** On 128 fresh tapes fair depth 4
drops from 117.75 to **93.56** mean moves while the clairvoyant planner, censored
at the move cap, is unchanged. Re-baselined, the fair planner reaches **2.0260
clears per move at H = 7, K = 256** (32 fresh tapes) and **2.0445 at H = 5,
K = 256** (128 fresh tapes) against fair D4's **1.9865**, closing 27.1% of the
clairvoyant gap rather than the 58.8% first reported. The K series also **turns
over**: K = 1024 is worse than K = 256 on five of six paired tapes, so the fair
ceiling is an interior optimum near 2.03, not an asymptote near 2.40.

The results are written up in
[`docs/exploratory/finding-06-flow-ceiling.md`](../../../docs/exploratory/finding-06-flow-ceiling.md)
,
[`docs/exploratory/finding-07-fair-planning-ceiling.md`](../../../docs/exploratory/finding-07-fair-planning-ceiling.md)
and
[`docs/exploratory/finding-12-fair-planner-ceiling-extended.md`](../../../docs/exploratory/finding-12-fair-planner-ceiling-extended.md),
which carries the re-baselining and supersedes the eight-tape figures in the
first two.

Nothing outside this directory, `build/flow-ceiling/`, `runs/RUN-FLOW-*/` and
`docs/exploratory/` is written by this work.

## Files

| File | What it is |
| --- | --- |
| `flow-common.hpp` | The `MasterTape` (one fixed future for a whole game), the long-game driver, per-move score/flow decomposition, occupancy tracking, and JSON emission |
| `flow-solver.hpp` | A single-threaded exact window solver with a pluggable per-move objective: **points** (the frozen solver's objective) or **clears** (numbered discs removed). `runRoot()` returns the exact value of every legal opening move |
| `fair-planner.hpp` | The same planner with the hidden board and/or the future removed and replaced by `K` sampled determinizations: a legal public-information policy. `--sample-threads` solves the K windows in parallel without changing the decision |
| `analyze.py`, `compare.py` | Per-cohort tables, and side-by-side clears/reveals by occupancy band |
| `paired.py` | Paired whole-game comparison on shared master tapes, with one-sided 95% bootstrap lower bounds |
| `extrapolate.py` | Fits the K series and reports when no finite asymptote is identified |
| `pv-replay.cpp` | Replays a solved principal variation through the scenario engine and reports its flow rates, wave depths, score composition and occupancy |
| `flow-run.cpp` | Receding-horizon clairvoyant long games, the public-policy controls, the self-test and the solver cross-check |
| `build.sh` | `clang++` build |

The game itself is not re-derived. Every move goes through
`approaches/lifetime-objective/scenario/scenario.hpp`, which
`scenario-parity.cpp` proves trajectory-identical to `drop7::playHeadlessMove`
over 218,470 moves, and the search reuses that approach's `applyScenarioMove`
and node identity.

## Gates

```sh
./approaches/lifetime-objective/flow-ceiling/build.sh

# seven gates: tape determinism, window/long-game agreement, the per-move score
# identity, objective dominance, root-move values vs the window optimum, the
# INFORMATION BOUNDARY gate (the fair planner's decisions are unchanged when
# every hidden value and the entire future are replaced), and the privileged
# arm reproducing the clairvoyant optimum
./build/flow-ceiling/flow-run --self-test

# the solver gate: with the points objective this solver must reproduce the
# frozen exact solver's optimum on every scenario of the minted suite
./build/flow-ceiling/flow-run \
    --cross-check approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl \
    --threads 10
```

## Measurements

```sh
# 1. the optimal line's flow on the existing H=9 suite
./build/scenario/solve --input approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl \
    --threads 10 --jsonl runs/<run>/suite-h9-solved.jsonl
./build/flow-ceiling/pv-replay --input runs/<run>/suite-h9-solved.jsonl --also-fair \
    --jsonl runs/<run>/suite-h9-pv-replay.jsonl

# 2. receding-horizon clairvoyant whole games (the sustainability probe)
./build/flow-ceiling/flow-run --policy rh-clears --games 8 --seed-start 0xa5230000 \
    --horizon 9 --commit 1 --max-moves 400 --threads 8 --time-limit 12 \
    --jsonl runs/<run>/rh-clears-h9-c1.jsonl

# the long replication: six fresh futures, 1,000-move cap
./build/flow-ceiling/flow-run --policy rh-clears --games 6 --seed-start 0xa5230000 \
    --horizon 9 --commit 1 --max-moves 1000 --threads 6 --time-limit 12 \
    --jsonl runs/<run>/rh-clears-h9-c1-long.jsonl

# 3. paired controls on the identical master tapes
./build/flow-ceiling/flow-run --policy fair-d4       --games 8 --seed-start 0xa5230000 ...
./build/flow-ceiling/flow-run --policy lowest-column --games 8 --seed-start 0xa5230000 ...

# 4. the legal (fair) planner: sweep the determinization count K
./build/flow-ceiling/flow-run --policy fair-rh --games 8 --seed-start 0xa5230000 \
    --sampler-seed 0xa5234000 --horizon 7 --samples 256 --max-moves 400 \
    --threads 6 --time-limit 20 --jsonl runs/<run>/fair-rh-h7-k256.jsonl
# add --tape-known for arm A (future known, hidden board still unknown)

# 5. the tables
python3 approaches/lifetime-objective/flow-ceiling/analyze.py runs/<run>/*.jsonl
python3 approaches/lifetime-objective/flow-ceiling/compare.py "label=runs/<run>/x.jsonl" ...
```

Note: a master tape's risen rows are drawn after its disc tape, so a tape is a
function of the seed **and** the move cap. Cohorts with different `--max-moves`
share seeds but not futures.

## Information boundary

`rh-points` and `rh-clears` read the hidden latent board. They are diagnostics
and teachers, never deployable policies. `fair-d4`, `lowest-column` and
`fair-rh` (without `--latent-known` or `--tape-known`) read only public state.
The `--self-test` information-boundary gate checks the last of those
mechanically rather than by inspection. `docs/methodology.md`'s public-information
rule is not weakened by anything here: no policy measured in this directory is
proposed for deployment.

## Seed lease

`SEEDLEASE-A52-FLOW` = `0xa5230000`–`0xa5233fff` for the original eight master
tapes, `SEEDLEASE-A52-FLOW2` = `0xa5234000`–`0xa5237fff` for the original K
series' hidden-board sampler, and `SEEDLEASE-A52-FLOW3` =
`0xa5238000`–`0xa523bfff` for everything added later — partitioned so a
master-tape seed and a sampler seed can never collide: fresh game tapes take
`0xa5239000`–`0xa5239fff`, samplers take `0xa523a000`–`0xa523bfff`. All are
exploratory development diagnostics; `flow-run` refuses to start outside them.
Once read these seeds are development data permanently.

**A note for anyone reusing this directory: use a large tape cohort.** Eight
tapes gave fair depth 4 a mean lifetime 26% above its true value. 64 is the
minimum for a paired claim and 128 is cheap for anything that is not a full
window solve.
