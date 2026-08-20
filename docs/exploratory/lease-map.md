# Seed lease map for the exploratory workspace

This is the authoritative allocation for exploratory work in this session. Every
range here is **development or training data**: once read, it can never become
confirmation evidence. No protected (`0x7d……`) or final (`0xd7……`) seed has been
opened.

## Why these prefixes

Candidate prefixes were chosen by extracting every eight-hex-digit constant from
`docs/research/history.md`, `approaches/`, `src/`, `research/` and `artifacts/`
and selecting prefixes that appear nowhere. Historical gameplay seeds cluster
densely in `0x3d000000`–`0x3ea00000`, with further blocks at `0x2d`, `0x2e`,
`0x3f`, `0x4d`, `0x5d700000`, `0x6d`, `0x6e` and `0xa5700000`. The only `0xa5`
constants anywhere in the repository are `0xa511e9b3`, `0xa54ff53a` and
`0xa5700000`, none of which fall in the ranges below.

## Allocation

| Lease | Range | Games | Role | Consumer | State |
| --- | --- | ---: | --- | --- | --- |
| `SEEDLEASE-A51D` eval | `0xa51d0000`–`0xa51d003f` | 64 | development, paired evaluation | fair-D4 reference cohort, terminal-utility sweep (6 arms), chance-strata arms (3 and 7) | opened |
| `SEEDLEASE-A51D` parity | `0xa51d0100`–`0xa51d0101` | 2 | development, CHECK probes | parameterized-search parity gate | opened |
| `SEEDLEASE-A51D` smoke | `0xa51d8000`–`0xa51d800f` | 16 | development, smoke test | corpus-generator smoke run | opened |
| `SEEDLEASE-A51D` withdrawn | `0xa51d9000`–`0xa51dcfff` | 16,384 | **withdrawn — see incident below** | first training-corpus attempt | opened, output destroyed |
| `SEEDLEASE-A51D-SCEN` | `0xa51dc000`–`0xa51dffff` | 16,384 | development, scenario minting | scenario engine, exact solver, minted suites | opened |
| `SEEDLEASE-A51D-VETO` | `0xa51e0000`–`0xa51e3fff` | 16,384 | development, paired SCREEN | rollout-veto retest | opened |
| `SEEDLEASE-A52` d2 | `0xa5200000`–`0xa520ffff` | 65,536 | **training** | corpus, depth-2 behaviour, ε=0.08 | opened |
| `SEEDLEASE-A52` d1 | `0xa5210000`–`0xa5213fff` | 16,384 | **training** | corpus, depth-1 behaviour, ε=0.15 | opened |
| `SEEDLEASE-A52` d3 | `0xa5214000`–`0xa5214fff` | 4,096 | **training** | corpus, depth-3 behaviour, ε=0.05 | opened |
| `SEEDLEASE-A52` d4 | `0xa5215000`–`0xa52152ff` | 768 | **training** | corpus, depth-4 behaviour, ε=0.03 | opened |
| `SEEDLEASE-A52` reserve | `0xa5216000`–`0xa52fffff` | — | training, unallocated | — | reserved |
| `SL-…-5da70000` | `0x5da70000`–`0x5da70fff` | 4,096 | public-development | a concurrent agent's afterstate track | **state disputed — under review** |

Training and evaluation ranges are disjoint by construction: evaluation lives
under `0xa51d`, training under `0xa52`. No model trained on `SEEDLEASE-A52` has
ever seen an evaluation seed.

## Incident: lease overlap, 2026-08-20

**What happened.** The first training-corpus run was launched at `0xa51d9000`
for 16,384 games. `0xa51d9000 + 16384 = 0xa51dd000`, so it consumed
`0xa51d9000`–`0xa51dcfff` and overlapped the scenario-minting sub-lease
`0xa51dc000`–`0xa51dffff` by **4,096 seeds** (`0xa51dc000`–`0xa51dcfff`). The
range was sized in games and the end address was not checked against the
allocation table before launch.

**Why it mattered.** Both roles are development data, so no protected or final
seed was touched and no qualification claim is affected. The real hazard was
contamination of a benchmark: scenario positions minted from those 4,096 seeds
could have appeared in the training corpus, so a model trained on that corpus
would have been evaluated partly on positions harvested from its own training
games. That is precisely the leak
[`design-01-benchmark-suite.md`](design-01-benchmark-suite.md) exists to prevent.

**Resolution.** The contaminated corpus (`d2eps08.states`, 1,029,206 records)
was **destroyed, not filtered**, and the corpus was regenerated from
`SEEDLEASE-A52`, a range with no overlap with any evaluation or scenario lease.
Regeneration cost 103 seconds. The 4,096 overlapped seeds remain validly
allocated to scenario minting; they are recorded above as withdrawn from
training so that no future run reuses them for that purpose.

**Process change.** Every block in `SEEDLEASE-A52` starts exactly where the
previous one ends, the arithmetic is printed and checked before launch, and the
generation script records the consumed end address for each block.
