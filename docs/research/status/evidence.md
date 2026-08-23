
## Research status evidence

| Finding | Evidence | Status |
| --- | --- | --- |
| TypeScript rules engine | 122 local tests | Reproduced in this checkout |
| Native engine and n-tuple checks | Gradient and self-tests | Reproduced in this checkout |
| Native/TypeScript trajectory agreement | Deterministic parity sweep | Reproduced; see reproducibility notes |
| Fair D4 vs D3: 400,675.25/116.375 vs 235,071.25/71 over 8 games | Detailed ledger | Recorded, small confirmation cohort |
| Fair D4: 308,295.578 points and 90.031 moves over 64 games | Detailed ledger; the 64 seeds, dispersion, censoring, and flow statistics required by `methodology.md` were not retained | Provisional reference mean pending a re-run under the benchmark contract |
| Fair D4 reproduced: 321,992 points and 94.06 moves over 64 games | Fresh exploratory seeds `0xa51d0000`+, unmodified frozen source | Development tier; single cohort, consistent with the ledger figure |
| Depth x chance-resolution factorial, depths 2-5 x 5/7 strata | 64 paired games per cell on `0xa51d1000`+, bit-exact accelerated engine | Development tier; the depth-5 cells are partial (32 and 16 games) |
| Score is 94.29% row-rise bonus; r = 0.9995 with game length | 64-game decomposition with a per-game score identity check | Development tier; reframes the objective as survival |
| One D4 game scored 1,246,684 | Task-record only | Anecdote; not an average or qualification |
| Million-point candidate exists | Frozen validation protocol | No |
| AFBR-40 afterstate idea | Task-record only | Proposal; no source, checkpoint, or result |

The cleanup reproduced engine behavior and buildability, not the long and
expensive training/evaluation runs in the historical ledger.
