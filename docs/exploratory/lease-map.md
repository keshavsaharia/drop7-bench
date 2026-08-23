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
| `SEEDLEASE-A51D` confirm | `0xa51d1000`–`0xa51d103f` | 64 | development, paired evaluation | **shared evaluation cohort** — fresh-seed 7-strata confirmation, depth x strata factorial, learned-leaf 2x2 | opened |
| `SEEDLEASE-A52-FLOW` | `0xa5230000`–`0xa5233fff` | 16,384 | development, diagnostic | flow-ceiling clairvoyant planner | opened (8 seeds read) |
| `SEEDLEASE-A52-FLOW2` | `0xa5234000`–`0xa5237fff` | 16,384 | development, diagnostic | fair planner, information-vs-planning decomposition | opened |
| `SEEDLEASE-A52-FLOW3` | `0xa5238000`–`0xa523bfff` | 16,384 | development, diagnostic | fair-planner ceiling extension (K=1024) | opened |
| `SEEDLEASE-A52-LEAF` | `0xa5240000`–`0xa5247fff` | 32,768 | development, tuning | learned-leaf blend tuning (disjoint from evaluation) | opened |
| `SEEDLEASE-A52-REVEAL` | `0xa5250000`–`0xa5257fff` | 32,768 | development, tuning | reveal-vs-next-disc sampling split | opened |
| `SEEDLEASE-A52-SUITE` | `0xa5258000`–`0xa525bfff` | 16,384 | development, diagnostic | scenario-suite validation, position mode | opened |
| `SEEDLEASE-A52-DISTILL` | `0xa5260000`–`0xa526ffff` | 65,536 | **training** | fair-planner distillation corpus | opened |
| `SEEDLEASE-A52-FAST` | `0xa5270000`–`0xa5277fff` | 32,768 | development, equivalence gates | semantics-preserving engine optimisation | opened |
| `SEEDLEASE-A52` reserve | `0xa5216000`–`0xa522ffff` and `0xa5278000`–`0xa52fffff` | — | unallocated | — | reserved |
| `SL-…-5da70000` | `0x5da70000`–`0x5da70fff` | 4,096 | public-development | a concurrent agent's afterstate track | **state disputed — under review** |
| `SL-20260822T020000Z-a5290000` | `0xa5290000`–`0xa529ffff` | 65,536 | **training** | leaf-evolution CMA-ES fitness blocks, 32 seeds per generation from `0xa5290000` (at most 40 generations authorised: `0xa5290000`–`0xa52904ff`) | opened |
| `SL-20260822T020000Z-a52b0000` | `0xa52b0000`–`0xa52b00ff` | 256 | public-development, held-out SCREEN | leaf-evolution held-out paired screen (first 64 seeds, read once after the candidate is frozen); remainder reserved for a replication | reserved |
| `SL-20260822T060000Z-a52c0000` | `0xa52c0000`–`0xa52c00ff` | 256 | public-development, SCREEN | survival-instinct root-filter screen (first 128 seeds, read once after CHECK gates); remainder reserved for replication | reserved |
| `SL-20260823T100000Z-a52d0000` | `0xa52d0000`–`0xa52d01ff` | 512 | public-development, SCREEN | reveal-construction leaf screen — corpus gate failed, never opened | cancelled-unopened |
| `SL-20260823T110000Z-a52d0200` | `0xa52d0200`–`0xa52d03ff` | 512 | public-development, SCREEN | reveal-construction leaf successor screen (first 256 seeds read once: frozen, A900 full; A300, B stopped at 39/36 games); remainder reserved | opened |
| `SL-20260823T200000Z-a52e0000` | `0xa52e0000`–`0xa52e01ff` | 512 | **training**, CHECK diagnostic | H-pool stage D0: 64 oracle games (0xa52e0000–0xa52e003f) and fair-D4 matched pool games (0xa52e0100–0xa52e01ff); public states exported, privileged generator only | reserved |
| `SL-20260823T215000Z-a5216000` | `0xa5216000`–`0xa52191ff` | 2,816 used of 12,800 | **training**, CHECK | P-SOL-1 sibling-outcome corpus: G0 ladder 0xa5216000–0xa52160ff, main corpus 0xa5217000–0xa52177ff, gate set 0xa5219000–0xa52191ff (gate origins development-read on use) | reserved |

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

## Incident 2: sub-leases declared outside this file, 2026-08-21

**What happened.** Nine sub-leases were issued to delegated agents in their task
briefs and recorded only in those agents' own READMEs and finding headers. They
were absent from this table and from `research/seeds/leases/`, so for several
hours the authoritative allocation map was incomplete. An independent
reconciliation pass found four of them; the remainder were added at the same
time.

**Why it mattered.** No collision resulted — the ranges were issued sequentially
from a single coordinator and are disjoint, and a check against the concurrent
OpenCode contributor's ranges (`0x5da70000`–`0x5da70fff` and
`0x5eed0001`–`0x5eed0008`) confirms no overlap in either direction. But the
protection was luck plus sequential issuance, not a record. The first incident in
this file was caused by exactly that failure mode: arithmetic done at launch time
rather than checked against a table.

**Resolution.** Every range is now listed above. Machine-readable
`research/seeds/leases/` records remain outstanding for the `0xa5……` family and
are owed before any of this work is promoted beyond the exploratory namespace.

**Process change.** A sub-lease is not issued until it appears in this table.
