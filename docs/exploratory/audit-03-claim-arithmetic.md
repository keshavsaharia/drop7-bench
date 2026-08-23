# Audit 03 — arithmetic and claim integrity of the historical evidence ledger

**Scope.** Independent, read-only audit of `docs/research/history.md` (4,607 lines,
read in full), `docs/research/status.md`, `docs/research/experiment-index.md`, and
`docs/strategies.md`, cross-checked against `src/core/native/engine.hpp`,
`research/schemas/game-result-v1.schema.json`,
`research/benchmarks/baselines-v1.json`,
`artifacts/protocols/optimistic-phase-ntuple/protocol.json`, `research/seeds/`,
and the `approaches/` source tree. No file other than this one was modified. No
gameplay, training, or benchmark was run. No cohort was opened.

**Status of this document.** Exploratory audit finding. It changes no frozen
protocol, no result record, and no shared status table. Every recommendation in
section (d) is a proposal for the coordinator, not an applied edit.

---

## 0. Method: the score identity used throughout

From `src/core/native/engine.hpp`:

| Constant | Line | Value |
| --- | --- | ---: |
| `kLevelBonus` (row rise) | 21 | 17,000 |
| `kClearBonus` (board clear) | 22 | 70,000 |
| `scoreForWave(d)` | 204–206 | `popper_count * floor(7 * d^2.5)` |

so a wave of depth `d` pays 7 / 39 / 109 / 224 / 391 / 617 per popped disc for
`d = 1..6`. A rise is awarded only when `moves_remaining` reaches zero **and**
`raiseCoveredRow` succeeds (`playMove`, lines 297–322); a failed rise ends the
game with no award. Therefore, for any complete game,

```text
score = rises * levelBonus + clears * 70,000 + sum(waves)
moves/5 - 1  <=  rises  <=  floor(moves/5)
```

Every reported `(mean score, mean moves)` pair was tested against both candidate
level bonuses. A mode is **feasible** only if the residual
`score - rises*bonus` is non-negative at the upper rise bound and does not imply
an absurd chain rate (> ~900 points/move) at the lower bound.

**The test is fully discriminating.** Because 17,000/5 = 3,400 and 7,000/5 =
1,400 points per move, the two feasible residual bands never overlap for any row
in the ledger. All 203 rows that record a move count classify into exactly one
mode. Two rows record no move count and are unclassifiable.

**Calibration of the rescoring constant.** The "Corrected Hardcore scoring
replay" (history.md:3327–3347) is a deterministic replay of the *same*
already-consumed seeds. Its move counts are byte-identical to the original
7,000-point run, and the score differences are exact multiples of 10,000:

| Cohort | 7k score (h:962/970) | 17k score (h:3335/3337) | Difference | Implied mean rises | moves/5 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Screen D3 (4 games) | 141,028.75 | 318,528.75 | 177,500 | 17.750 | 18.600 |
| Screen D4 (4 games) | 179,951.75 | 399,951.75 | 220,000 | 22.000 | 23.000 |
| Confirmation D3 (8 games) | 102,571.25 | 235,071.25 | 132,500 | 13.250 | 14.200 |
| Confirmation D4 (8 games) | 176,925.25 | 400,675.25 | 223,750 | 22.375 | 23.275 |

This proves two things the ledger never states explicitly:

1. **The level bonus does not change fair-expectimax decisions.** Identical
   trajectories, identical move counts, identical clear/reveal counts. The
   engine's level award is added after the search has chosen.
2. **Any 7,000-point result in the ledger can be exactly rescored** as
   `score_17k = score_7k + 10,000 * rises`, with
   `rises ≈ moves/5 − 0.925` calibrated from the four rows above
   (equivalently `Δ ≈ 2,000·moves − 9,250`).

Point 2 is what makes several of the findings below actionable rather than
rhetorical: three preregistered gate outcomes can be *recomputed*, and two of
them reverse.

---

## (a) Classification of every reported (score, moves, n) triple

205 rows. "Implied non-level pts/move" is the `min to max` range of non-level
points per move (chain waves plus any undisclosed board clears) implied by the
inferred mode. The **minimum** is evaluated at the upper rise bound
`floor(moves/5)` and is routinely slightly negative, because real games almost
always end on a *failed* rise, putting true mean rises near `moves/5 − 0.925`
(calibrated in section 0). The operative figure is therefore the **maximum**,
which is the residual at the lower rise bound. "Mode stated in ledger?" is
whether the surrounding history.md section names the scoring mode at all.

| history.md:line | Result | Mean score | Mean moves | n | Inferred mode | Mode stated in ledger? | Implied non-level pts/move | Verdict |
| --- | --- | ---: | ---: | ---: | :--: | :--: | :--: | --- |
| 144 | n-tuple greedy (64-probe) | 182,057.73 | 56.969 | 64 | **17k** | no | -204 to 94 | consistent; single mode only |
| 145 | n-tuple sparse D2 (64-probe) | 232,107.16 | 70.766 | 64 | **17k** | no | -120 to 120 | consistent; single mode only |
| 146 | n-tuple sparse D3 (64-probe) | 227,975.86 | 69.141 | 64 | **17k** | no | -103 to 143 | consistent; single mode only |
| 226 | CFPI iter2 value policy | 224,621.00 | *not recorded* | 4 | **UNCLASSIFIABLE** | no | — | No move count; scoring mode unrecoverable |
| 226 | CFPI iter2 paired behavior | 257,423.00 | *not recorded* | 4 | **UNCLASSIFIABLE** | no | — | No move count; scoring mode unrecoverable |
| 259 | MC-value heldout behavior | 246,447.88 | 75.000 | 8 | **17k** | no | -114 to 113 | consistent; single mode only |
| 260 | MC-value value policy | 122,100.12 | 40.000 | 8 | **17k** | no | -347 to 78 | consistent; single mode only |
| 296 | nonlinear-evo exact behavior | 307,222.75 | 91.250 | 4 | **17k** | no | -33 to 153 | consistent; single mode only |
| 297 | nonlinear-evo candidate | 148,349.25 | 47.500 | 4 | **17k** | no | -277 to 81 | consistent; single mode only |
| 336 | survival-scale exact behavior | 299,059.75 | 89.328 | 64 | **17k** | no | -52 to 138 | consistent; single mode only |
| 386 | structured-NNUE exact behavior | 247,202.87 | 74.575 | 160 | **17k** | no | -85 to 143 | consistent; single mode only |
| 451 | denoised roll-ins (orig run) | 234,260.75 | 70.906 | 64 | **17k** | yes | -96 to 144 | consistent; single mode only |
| 514 | denoised-veto screen ensemble | 126,146.00 | 85.500 | 4 | **7k** | **yes** | 75 to 157 | consistent; single mode only |
| 515 | denoised-veto screen veto | 184,649.25 | 121.250 | 4 | **7k** | **yes** | 123 to 181 | consistent; single mode only |
| 521 | denoised-veto conf ensemble | 164,266.00 | 110.625 | 8 | **7k** | **yes** | 85 to 148 | consistent; single mode only |
| 522 | denoised-veto conf veto | 161,660.12 | 108.750 | 8 | **7k** | **yes** | 87 to 151 | consistent; single mode only |
| 554 | counterfactual screen D3 | 166,112.25 | 111.500 | 4 | **7k** | **yes** | 90 to 153 | consistent; single mode only |
| 555 | counterfactual screen NNUE-leaf | 102,916.25 | 72.500 | 4 | **7k** | **yes** | 20 to 116 | consistent; single mode only |
| 568 | privileged oracle single seed (500 cap) | 2,079,579.00 | 500.000 | 1 | **17k** | no | 759 to 793 | consistent; single mode only |
| 607 | oracle distill fitting roll-ins (200 cap) | 829,516.75 | 200.000 | None | **17k** | no | 748 to 833 | consistent; single mode only |
| 613 | oracle-distill screen behavior | 261,871.88 | 79.250 | 8 | **17k** | no | -96 to 119 | consistent; single mode only |
| 614 | oracle-distill screen hybrid | 283,850.62 | 84.500 | 8 | **17k** | no | -41 to 160 | consistent; single mode only |
| 616 | oracle-distill conf behavior | 249,816.88 | 75.375 | 16 | **17k** | no | -86 to 140 | consistent; single mode only |
| 617 | oracle-distill conf hybrid | 187,542.62 | 57.250 | 16 | **17k** | no | -124 to 173 | consistent; single mode only |
| 640 | root-quad ensemble screen cand | 128,858.75 | 88.750 | 4 | **7k** | **yes** | 52 to 131 | consistent; single mode only |
| 640 | root-quad ensemble screen base | 96,304.00 | 67.500 | 4 | **7k** | **yes** | 27 to 130 | consistent; single mode only |
| 642 | root-quad ensemble conf cand | 117,066.50 | 80.625 | 8 | **7k** | **yes** | 52 to 139 | consistent; single mode only |
| 642 | root-quad ensemble conf base | 162,492.12 | 109.375 | 8 | **7k** | **yes** | 86 to 150 | consistent; single mode only |
| 658 | nnue-guided screen cand | 82,169.25 | 58.750 | 4 | **7k** | **yes** | -1 to 118 | consistent; single mode only |
| 659 | nnue-guided screen D3 | 87,743.75 | 62.000 | 4 | **7k** | **yes** | 15 to 128 | consistent; single mode only |
| 673 | root quadrature D3 | 205,001.25 | 132.500 | 4 | **7k** | **yes** | 147 to 200 | consistent; single mode only |
| 674 | root quadrature cand | 179,243.00 | 120.000 | 4 | **7k** | **yes** | 94 to 152 | consistent; single mode only |
| 687 | exact-depth4 screen D3 | 149,621.25 | 103.750 | 4 | **7k** | **yes** | 42 to 110 | consistent; single mode only |
| 688 | exact-depth4 screen D4 | 124,172.25 | 85.000 | 4 | **7k** | **yes** | 61 to 143 | consistent; single mode only |
| 707 | phase-weight-evo default | 107,709.75 | 75.125 | 8 | **7k** | **yes** | 34 to 127 | consistent; single mode only |
| 708 | phase-weight-evo evolved | 105,534.88 | 72.500 | 8 | **7k** | **yes** | 56 to 152 | consistent; single mode only |
| 721 | topology audit D3 | 90,273.00 | 63.625 | 16 | **7k** | **yes** | 19 to 129 | consistent; single mode only |
| 722 | topology audit oracle (200 cap) | 429,182.50 | 200.000 | 16 | **7k** | **yes** | 746 to 781 | consistent; single mode only |
| 735 | oracle 500-cap 12 seeds | 1,058,931.50 | 500.000 | 12 | **7k** | **yes** | 718 to 732 | consistent; single mode only |
| 737 | topology paired public D3 | 132,979.00 | 90.500 | 12 | **7k** | **yes** | 69 to 147 | consistent; single mode only |
| 757 | accessible-energy screen D3 | 112,318.50 | 77.125 | 8 | **7k** | **yes** | 56 to 147 | consistent; single mode only |
| 758 | accessible-energy screen cand | 85,768.00 | 60.625 | 8 | **7k** | **yes** | 15 to 130 | consistent; single mode only |
| 773 | energy root-prior D3 | 105,767.88 | 73.125 | 8 | **7k** | **NO** | 46 to 142 | consistent; **7k, but section carries no scoring label** |
| 773 | energy root-prior cand | 90,300.75 | 60.875 | 8 | **7k** | **NO** | 83 to 198 | consistent; **7k, but section carries no scoring label** |
| 793 | hindsight screen D3 | 107,076.00 | 72.500 | 4 | **7k** | **yes** | 77 to 173 | consistent; single mode only |
| 794 | hindsight screen cand | 51,500.50 | 37.500 | 4 | **7k** | **yes** | -27 to 160 | consistent; single mode only |
| 906 | fair-only screen CFPI D3 | 109,336.75 | 75.625 | 8 | **7k** | **yes** | 46 to 138 | consistent; single mode only |
| 907 | fair-only screen fair-only | 142,027.12 | 94.375 | 8 | **7k** | **yes** | 105 to 179 | consistent; single mode only |
| 914 | fair-only conf CFPI | 102,612.81 | 71.562 | 16 | **7k** | **yes** | 34 to 132 | consistent; single mode only |
| 914 | fair-only conf fair-only | 119,270.75 | 80.500 | 16 | **7k** | **yes** | 82 to 169 | consistent; single mode only |
| 962 | D4 screen D3 (7k) | 141,028.75 | 93.000 | 4 | **7k** | **yes** | 116 to 192 | consistent; single mode only |
| 962 | D4 screen D4 (7k) | 179,951.75 | 115.000 | 4 | **7k** | **yes** | 165 to 226 | consistent; single mode only |
| 970 | D4 conf D3 (7k) | 102,571.25 | 71.000 | 8 | **7k** | **yes** | 45 to 143 | consistent; single mode only |
| 970 | D4 conf D4 (7k) | 176,925.25 | 116.375 | 8 | **7k** | **yes** | 120 to 180 | consistent; single mode only |
| 1019 | root-CVaR screen fair D3 | 176,819.75 | 114.125 | 8 | **7k** | **yes** | 149 to 211 | consistent; single mode only |
| 1020 | root-CVaR screen risk | 87,112.00 | 61.500 | 8 | **7k** | **yes** | 16 to 130 | consistent; single mode only |
| 1063 | full-fair screen fair leaf D3 | 134,923.88 | 89.125 | 8 | **7k** | **yes** | 114 to 192 | consistent; single mode only |
| 1064 | full-fair screen full-fair | 94,812.00 | 65.250 | 8 | **7k** | **yes** | 53 to 160 | consistent; single mode only |
| 1091 | transition-reward fair-only | 113,772.50 | 78.125 | 8 | **7k** | **NO** | 56 to 146 | consistent; **7k, but section carries no scoring label** |
| 1092 | transition-reward cand | 96,759.62 | 66.500 | 8 | **7k** | **NO** | 55 to 160 | consistent; **7k, but section carries no scoring label** |
| 1150 | CEM D3 heldout fair | 120,608.00 | 81.250 | 32 | **7k** | **NO** | 84 to 171 | consistent; **7k, but section carries no scoring label** |
| 1151 | CEM D3 heldout candidate | 141,523.75 | 93.969 | 32 | **7k** | **NO** | 106 to 181 | consistent; **7k, but section carries no scoring label** |
| 1159 | CEM D3 screen fair | 149,022.25 | 97.500 | 8 | **7k** | **NO** | 128 to 200 | consistent; **7k, but section carries no scoring label** |
| 1159 | CEM D3 screen candidate | 148,116.88 | 99.375 | 8 | **7k** | **NO** | 90 to 161 | consistent; **7k, but section carries no scoring label** |
| 1220 | CEM-D4 training stock D4 | 178,289.56 | 114.688 | 16 | **7k** | **NO** | 155 to 216 | consistent; **7k, but section carries no scoring label** |
| 1221 | CEM-D4 training composite | 200,972.00 | 131.250 | 16 | **7k** | **NO** | 131 to 185 | consistent; **7k, but section carries no scoring label** |
| 1229 | CEM-D4 heldout stock D4 | 177,487.31 | 114.063 | 16 | **7k** | **NO** | 156 to 217 | consistent; **7k, but section carries no scoring label** |
| 1229 | CEM-D4 heldout composite | 159,652.06 | 103.125 | 16 | **7k** | **NO** | 148 to 216 | consistent; **7k, but section carries no scoring label** |
| 1288 | D4/s7 fitting stock s5 | 118,676.00 | 78.875 | 8 | **7k** | **NO** | 105 to 193 | consistent; **7k, but section carries no scoring label** |
| 1288 | D4/s7 fitting s7 | 118,512.38 | 81.125 | 8 | **7k** | **NO** | 61 to 147 | consistent; **7k, but section carries no scoring label** |
| 1452 | topology-ext screen fair D4 | 85,874.50 | 60.000 | 8 | **7k** | **NO** | 31 to 148 | consistent; **7k, but section carries no scoring label** |
| 1452 | topology-ext screen residual | 95,697.12 | 65.625 | 8 | **7k** | **NO** | 58 to 165 | consistent; **7k, but section carries no scoring label** |
| 1458 | topology-ext conf fair D4 | 196,764.12 | 125.938 | 16 | **7k** | **NO** | 162 to 218 | consistent; **7k, but section carries no scoring label** |
| 1458 | topology-ext conf residual | 154,934.62 | 101.938 | 16 | **7k** | **NO** | 120 to 189 | consistent; **7k, but section carries no scoring label** |
| 1465 | topology-ext single baseline game | 476,511.00 | 285.000 | 1 | **7k** | **NO** | 272 to 297 | consistent; **7k, but section carries no scoring label** |
| 1465 | topology-ext single cand game | 139,399.00 | 90.000 | 1 | **7k** | **NO** | 149 to 227 | consistent; **7k, but section carries no scoring label** |
| 1465 | topology-ext single baseline game 2 | 329,049.00 | 200.000 | 1 | **7k** | **NO** | 245 to 280 | consistent; **7k, but section carries no scoring label** |
| 1465 | topology-ext single cand game 2 | 74,172.00 | 55.000 | 1 | **7k** | **NO** | -51 to 76 | consistent; **7k, but section carries no scoring label** |
| 1563 | fair-D1 rollout-improve base | 72,526.17 | 53.750 | 12 | **7k** | **NO** | -51 to 80 | consistent; **7k, but section carries no scoring label** |
| 1564 | fair-D1 rollout-improve best | 64,304.83 | 45.500 | 12 | **7k** | **NO** | 13 to 167 | consistent; **7k, but section carries no scoring label** |
| 1611 | PPO v1 untrained greedy | 23,936.52 | 21.720 | 64 | **7k** | **NO** | -298 to 24 | consistent; **7k, but section carries no scoring label** |
| 1613 | PPO v1 best greedy probe | 24,503.34 | 22.110 | 64 | **7k** | **NO** | -292 to 25 | consistent; **7k, but section carries no scoring label** |
| 1613 | PPO v1 deterministic random | 31,835.25 | 26.940 | 64 | **7k** | **NO** | -218 to 42 | consistent; **7k, but section carries no scoring label** |
| 1633 | PPO v2 warm-start before | 18,906.03 | 18.280 | 64 | **7k** | **NO** | -366 to 17 | consistent; **7k, but section carries no scoring label** |
| 1633 | PPO v2 warm-start after | 33,539.47 | 28.020 | 64 | **7k** | **NO** | -203 to 47 | consistent; **7k, but section carries no scoring label** |
| 1634 | PPO v2 random | 32,143.69 | 27.030 | 64 | **7k** | **NO** | -211 to 48 | consistent; **7k, but section carries no scoring label** |
| 1634 | PPO v2 fair D1 | 69,274.41 | 51.270 | 64 | **7k** | **NO** | -49 to 88 | consistent; **7k, but section carries no scoring label** |
| 1688 | phase-energy seed1 stock | 185,341.00 | 105.000 | 1 | **7k** | **NO** | 365 to 432 | consistent; **7k, but section carries no scoring label** |
| 1688 | phase-energy seed1 clear-only | 151,969.00 | 100.000 | 1 | **7k** | **NO** | 120 to 190 | consistent; **7k, but section carries no scoring label** |
| 1689 | phase-energy seed1 phase-only | 67,049.00 | 50.000 | 1 | **7k** | **NO** | -59 to 81 | consistent; **7k, but section carries no scoring label** |
| 1689 | phase-energy seed1 moderate | 203,191.00 | 135.000 | 1 | **7k** | **NO** | 105 to 157 | consistent; **7k, but section carries no scoring label** |
| 1690 | phase-energy seed1 aggressive | 193,310.00 | 130.000 | 1 | **7k** | **NO** | 87 to 141 | consistent; **7k, but section carries no scoring label** |
| 1697 | phase-energy 4-game stock | 143,299.50 | 88.750 | 4 | **7k** | **NO** | 215 to 294 | consistent; **7k, but section carries no scoring label** |
| 1698 | phase-energy 4-game clear-only | 244,186.50 | 156.250 | 4 | **7k** | **NO** | 163 to 208 | consistent; **7k, but section carries no scoring label** |
| 1699 | phase-energy 4-game phase-only | 93,784.75 | 65.500 | 4 | **7k** | **NO** | 32 to 139 | consistent; **7k, but section carries no scoring label** |
| 1700 | phase-energy 4-game moderate | 169,493.75 | 112.500 | 4 | **7k** | **NO** | 107 to 169 | consistent; **7k, but section carries no scoring label** |
| 1751 | clear-reward heldout stock | 155,655.38 | 101.250 | 8 | **7k** | **NO** | 137 to 206 | consistent; **7k, but section carries no scoring label** |
| 1752 | clear-reward heldout +600 | 106,701.50 | 72.625 | 8 | **7k** | **NO** | 69 to 166 | consistent; **7k, but section carries no scoring label** |
| 1819 | selective-D5 heldout D4 | 120,580.75 | 82.875 | 8 | **7k** | **NO** | 55 to 139 | consistent; **7k, but section carries no scoring label** |
| 1819 | selective-D5 heldout D5w3 | 133,977.12 | 89.750 | 8 | **7k** | **NO** | 93 to 171 | consistent; **7k, but section carries no scoring label** |
| 1828 | selective-D5 screen D4 | 182,041.12 | 115.875 | 8 | **7k** | **NO** | 171 to 231 | consistent; **7k, but section carries no scoring label** |
| 1829 | selective-D5 screen D5w3 | 174,808.38 | 112.375 | 8 | **7k** | **NO** | 156 to 218 | consistent; **7k, but section carries no scoring label** |
| 1886 | cycle-boundary pilot D4 | 231,290.00 | 150.000 | 1 | **7k** | **NO** | 142 to 189 | consistent; **7k, but section carries no scoring label** |
| 1886 | cycle-boundary pilot D5 | 107,668.00 | 75.000 | 1 | **7k** | **NO** | 36 to 129 | consistent; **7k, but section carries no scoring label** |
| 1949 | D5/s3 pilot stock D4/s5 | 269,141.00 | 170.000 | 1 | **7k** | **NO** | 183 to 224 | consistent; **7k, but section carries no scoring label** |
| 1950 | D5/s3 pilot D4/s3 | 151,153.00 | 95.000 | 1 | **7k** | **NO** | 191 to 265 | consistent; **7k, but section carries no scoring label** |
| 1950 | D5/s3 pilot D5/s3 | 182,622.00 | 120.000 | 1 | **7k** | **NO** | 122 to 180 | consistent; **7k, but section carries no scoring label** |
| 2003 | phase5 veto pilot (identical) | 162,102.00 | 110.000 | 1 | **7k** | **NO** | 74 to 137 | consistent; **7k, but section carries no scoring label** |
| 2103 | D2-only sanity diagnostic | 119,061.25 | 81.250 | 12 | **7k** | **NO** | 65 to 152 | consistent; **7k, but section carries no scoring label** |
| 2104 | D4 comparator (same 12) | 115,073.08 | 77.170 | 12 | **7k** | **NO** | 91 to 182 | consistent; **7k, but section carries no scoring label** |
| 2167 | reveal-reward seed1 stock | 140,681.00 | 90.000 | 1 | **7k** | **NO** | 163 to 241 | consistent; **7k, but section carries no scoring label** |
| 2167 | reveal-reward seed1 reveal-only | 171,147.00 | 108.000 | 1 | **7k** | **NO** | 185 to 250 | consistent; **7k, but section carries no scoring label** |
| 2168 | reveal-reward seed1 balanced | 370,588.00 | 225.000 | 1 | **7k** | **NO** | 247 to 278 | consistent; **7k, but section carries no scoring label** |
| 2176 | reveal-reward fit stock | 110,139.25 | 71.250 | 4 | **7k** | **NO** | 146 to 244 | consistent; **7k, but section carries no scoring label** |
| 2177 | reveal-reward fit reveal-only | 117,299.25 | 77.500 | 4 | **7k** | **NO** | 114 to 204 | consistent; **7k, but section carries no scoring label** |
| 2178 | reveal-reward fit balanced | 152,413.25 | 98.750 | 4 | **7k** | **NO** | 143 to 214 | consistent; **7k, but section carries no scoring label** |
| 2188 | reveal-reward heldout stock | 108,247.50 | 74.375 | 8 | **7k** | **NO** | 55 to 150 | consistent; **7k, but section carries no scoring label** |
| 2189 | reveal-reward heldout balanced | 101,147.88 | 70.125 | 8 | **7k** | **NO** | 42 to 142 | consistent; **7k, but section carries no scoring label** |
| 2255 | rollout-veto pilot stock D4 | 159,616.00 | 105.000 | 1 | **7k** | **NO** | 120 to 187 | consistent; **7k, but section carries no scoring label** |
| 2256 | rollout-veto pilot veto | 404,047.00 | 250.000 | 1 | **7k** | **NO** | 216 to 244 | consistent; **7k, but section carries no scoring label** |
| 2764 | quality-ext 3ded0001 stock | 109,264.00 | 70.000 | 1 | **7k** | **NO** | 161 to 261 | consistent; **7k, but section carries no scoring label** |
| 2764 | quality-ext 3ded0001 veto | 153,925.00 | 100.000 | 1 | **7k** | **NO** | 139 to 209 | consistent; **7k, but section carries no scoring label** |
| 2765 | quality-ext 3ded0002 both | 59,004.00 | 45.000 | 1 | **7k** | **NO** | -89 to 67 | consistent; **7k, but section carries no scoring label** |
| 2766 | quality-ext 3ded0003 stock | 100,147.00 | 70.000 | 1 | **7k** | **NO** | 31 to 131 | consistent; **7k, but section carries no scoring label** |
| 2766 | quality-ext 3ded0003 veto | 81,662.00 | 60.000 | 1 | **7k** | **NO** | -39 to 78 | consistent; **7k, but section carries no scoring label** |
| 2768 | quality-ext mean veto | 174,659.50 | 113.750 | 4 | **7k** | **NO** | 135 to 197 | consistent; **7k, but section carries no scoring label** |
| 2768 | quality-ext mean stock | 107,007.75 | 72.500 | 4 | **7k** | **NO** | 76 to 173 | consistent; **7k, but section carries no scoring label** |
| 2830 | ladder probe D2 base | 98,642.29 | 68.770 | 256 | **7k** | **NO** | 34 to 136 | consistent; **7k, but section carries no scoring label** |
| 2831 | ladder probe D2 w500 | 103,467.46 | 71.590 | 256 | **7k** | **NO** | 45 to 143 | consistent; **7k, but section carries no scoring label** |
| 2845 | ladder D4 stock | 124,934.38 | 83.875 | 8 | **7k** | **NO** | 90 to 173 | consistent; **7k, but section carries no scoring label** |
| 2846 | ladder D4 candidate | 121,848.63 | 82.125 | 8 | **7k** | **NO** | 84 to 169 | consistent; **7k, but section carries no scoring label** |
| 3092 | TC corrected score-TD | 66,625.12 | 49.469 | 64 | **7k** | **NO** | -53 to 88 | consistent; **7k, but section carries no scoring label** |
| 3093 | TC legacy score-TD 10k | 73,480.45 | 53.766 | 64 | **7k** | **NO** | -33 to 97 | consistent; **7k, but section carries no scoring label** |
| 3098 | TC corrected MC | 66,296.95 | 49.312 | 64 | **7k** | **NO** | -56 to 86 | consistent; **7k, but section carries no scoring label** |
| 3099 | TC legacy MC 10k | 66,442.00 | 49.453 | 64 | **7k** | **NO** | -56 to 85 | consistent; **7k, but section carries no scoring label** |
| 3099 | TC legacy MC 100k | 78,194.23 | 57.031 | 64 | **7k** | **NO** | -29 to 94 | consistent; **7k, but section carries no scoring label** |
| 3178 | phase-conditioned n-tuple | 68,463.25 | 50.828 | 64 | **7k** | **NO** | -53 to 85 | consistent; **7k, but section carries no scoring label** |
| 3335 | CORRECTED replay screen D3 | 318,528.75 | 93.000 | 4 | **17k** | yes | 25 to 208 | consistent; single mode only |
| 3335 | CORRECTED replay screen D4 | 399,951.75 | 115.000 | 4 | **17k** | yes | 78 to 226 | consistent; single mode only |
| 3337 | CORRECTED replay conf D3 | 235,071.25 | 71.000 | 8 | **17k** | yes | -89 to 150 | consistent; single mode only |
| 3337 | CORRECTED replay conf D4 | 400,675.25 | 116.375 | 8 | **17k** | yes | 43 to 189 | consistent; single mode only |
| 3491 | rainbow StageA random | 73,670.06 | 26.410 | 32 | **17k** | yes | -611 to 33 | consistent; single mode only |
| 3492 | rainbow StageA learned | 101,324.97 | 33.910 | 32 | **17k** | yes | -412 to 89 | consistent; single mode only |
| 3502 | rainbow StageB fair D1 | 168,072.38 | 52.940 | 32 | **17k** | yes | -225 to 96 | consistent; single mode only |
| 3503 | rainbow StageB learned 1m | 111,092.25 | 36.840 | 32 | **17k** | yes | -384 to 77 | consistent; single mode only |
| 3593 | evo pilot start | 137,725.00 | 44.550 | 96 | **17k** | yes | -309 to 73 | consistent; single mode only |
| 3593 | evo pilot end | 151,923.00 | 48.490 | 96 | **17k** | yes | -267 to 84 | consistent; single mode only |
| 3602 | evo tournament hand-seeded | 136,169.15 | 44.156 | 128 | **17k** | yes | -316 to 69 | consistent; single mode only |
| 3603 | evo tournament evolved | 157,528.50 | 50.234 | 128 | **17k** | yes | -264 to 74 | consistent; single mode only |
| 3619 | evo search direct | 162,932.25 | 51.875 | 8 | **17k** | yes | -259 to 69 | consistent; single mode only |
| 3620 | evo search d2w2s3 | 171,204.50 | 53.625 | 8 | **17k** | yes | -207 to 110 | consistent; single mode only |
| 3621 | evo search d3w2s3 | 235,950.00 | 71.250 | 8 | **17k** | yes | -88 to 150 | consistent; single mode only |
| 3622 | evo search d4w2s3 | 170,558.38 | 53.750 | 8 | **17k** | yes | -227 to 89 | consistent; single mode only |
| 3623 | evo search d3w3s3 | 175,596.25 | 54.875 | 8 | **17k** | yes | -200 to 110 | consistent; single mode only |
| 3624 | evo search d4w3s3 | 247,404.63 | 75.000 | 8 | **17k** | yes | -101 to 125 | consistent; single mode only |
| 3625 | evo search d3w2s5 | 234,309.63 | 71.250 | 8 | **17k** | yes | -111 to 127 | consistent; single mode only |
| 3626 | evo search d4w2s5 | 272,605.13 | 81.750 | 8 | **17k** | yes | -65 to 143 | consistent; single mode only |
| 3627 | evo search d5w2s3 | 253,689.50 | 76.250 | 8 | **17k** | yes | -73 to 150 | consistent; single mode only |
| 3707 | public-rollout-PI fair D1 | 151,909.25 | 48.750 | 4 | **17k** | yes | -284 to 65 | consistent; single mode only |
| 3708 | public-rollout-PI 15-tape | 162,491.50 | 50.000 | 4 | **17k** | yes | -150 to 190 | consistent; single mode only |
| 3798 | torch clone-training D2 corpus | 244,207.95 | 73.547 | 768 | **17k** | yes | -80 to 152 | consistent; single mode only |
| 3799 | torch heldout D2 corpus | 231,063.22 | 70.121 | 256 | **17k** | yes | -105 to 138 | consistent; single mode only |
| 3800 | torch DAgger student corpus | 130,565.02 | 42.293 | 512 | **17k** | yes | -313 to 89 | consistent; single mode only |
| 3806 | torch clone dev | 141,986.94 | 45.125 | 32 | **17k** | yes | -253 to 123 | consistent; single mode only |
| 3807 | torch dev random | 79,307.88 | 27.969 | 32 | **17k** | yes | -564 to 43 | consistent; single mode only |
| 3807 | torch dev fair D1 | 181,846.44 | 56.281 | 32 | **17k** | yes | -169 to 133 | consistent; single mode only |
| 3808 | torch dev fair D2 | 191,189.34 | 58.688 | 32 | **17k** | yes | -142 to 147 | consistent; single mode only |
| 3827 | torch correction dev | 142,364.03 | 45.313 | 32 | **17k** | yes | -258 to 117 | consistent; single mode only |
| 4027 | gradaccum iter1 batch | 112,175.52 | 37.168 | 512 | **17k** | yes | -382 to 75 | consistent; single mode only |
| 4027 | gradaccum iter32 batch | 136,608.05 | 44.037 | 512 | **17k** | yes | -298 to 88 | consistent; single mode only |
| 4033 | gradaccum dev candidate | 142,677.78 | 45.656 | 64 | **17k** | yes | -275 to 97 | consistent; single mode only |
| 4033 | gradaccum dev clone | 130,797.41 | 42.500 | 64 | **17k** | yes | -322 to 78 | consistent; single mode only |
| 4034 | gradaccum dev fair D1 | 180,713.42 | 56.359 | 64 | **17k** | yes | -194 to 108 | consistent; single mode only |
| 4035 | gradaccum dev fair D2 | 241,825.20 | 72.594 | 64 | **17k** | yes | -69 to 165 | consistent; single mode only |
| 4035 | gradaccum dev random | 77,674.41 | 27.484 | 64 | **17k** | yes | -574 to 45 | consistent; single mode only |
| 4143 | manifold root prior | 253,798.88 | 73.938 | 16 | **17k** | no | 33 to 263 | consistent; single mode only |
| 4143 | manifold fair D3 | 301,101.06 | 88.938 | 16 | **17k** | no | -14 to 177 | consistent; single mode only |
| 4196 | primal-dual calibration | 175,834.00 | 55.006 | 512 | **17k** | no | -203 to 106 | consistent; single mode only |
| 4234 | corrected-D4 bootstrap (64) | 308,295.58 | 90.031 | 64 | **17k** | yes | 24 to 213 | consistent; single mode only |
| 4236 | regen round1 roll-ins | 110,294.00 | 36.386 | None | **17k** | yes | -369 to 98 | consistent; single mode only |
| 4236 | regen round2 peak | 138,229.00 | 44.134 | None | **17k** | yes | -268 to 117 | consistent; single mode only |
| 4237 | regen round8 | 116,598.00 | 38.046 | None | **17k** | yes | -335 to 111 | consistent; single mode only |
| 4366 | optimistic n-tuple final chunk | 176,247.00 | 54.811 | None | **17k** | no | -184 to 126 | consistent; single mode only |
| 4372 | optimistic StageA direct | 181,733.42 | 56.359 | 64 | **17k** | no | -175 to 126 | consistent; single mode only |
| 4373 | optimistic StageA 2-boundary | 113,643.97 | 37.375 | 64 | **17k** | no | -359 to 95 | consistent; single mode only |
| 4375 | optimistic half1 direct | 180,667.00 | 56.125 | 32 | **17k** | no | -181 to 122 | consistent; single mode only |
| 4375 | optimistic half1 search | 118,367.00 | 38.719 | 32 | **17k** | no | -343 to 96 | consistent; single mode only |
| 4375 | optimistic half2 direct | 182,800.00 | 56.594 | 32 | **17k** | no | -170 to 130 | consistent; single mode only |
| 4375 | optimistic half2 search | 108,921.00 | 36.031 | 32 | **17k** | no | -377 to 95 | consistent; single mode only |
| 4399 | vertical reservoir candidate | 160,498.09 | 50.891 | 128 | **17k** | yes | -246 to 88 | consistent; single mode only |
| 4399 | vertical reservoir fair D1 | 178,554.44 | 55.750 | 128 | **17k** | yes | -197 to 108 | consistent; single mode only |
| 4409 | viability controller StageA | 132,537.09 | 43.281 | 32 | **17k** | yes | -338 to 55 | consistent; single mode only |
| 4410 | viability fair D1 | 172,697.62 | 53.969 | 32 | **17k** | yes | -200 to 115 | consistent; single mode only |
| 4421 | constructive spectrum StageA | 266,695.50 | 79.500 | 32 | **17k** | yes | -45 to 168 | consistent; single mode only |
| 4422 | constructive fair D1 | 157,198.06 | 49.875 | 32 | **17k** | yes | -248 to 93 | consistent; single mode only |
| 4425 | constructive D4 integration | 283,286.00 | 83.750 | 4 | **17k** | yes | -17 to 186 | consistent; single mode only |
| 4425 | exact D4 fitting quartet | 372,870.50 | 106.250 | 4 | **17k** | yes | 109 to 269 | consistent; single mode only |
| 4437 | H12 horizon | 299,730.56 | 88.344 | 32 | **17k** | yes | -7 to 185 | consistent; single mode only |
| 4438 | H7 horizon | 258,223.94 | 77.219 | 32 | **17k** | yes | -56 to 164 | consistent; single mode only |
| 4440 | H17 horizon | 202,634.00 | 61.969 | 32 | **17k** | yes | -130 to 144 | consistent; single mode only |
| 4440 | H27 horizon | 261,633.00 | 77.938 | 32 | **17k** | yes | -43 to 175 | consistent; single mode only |
| 4441 | H12 pareto gate | 302,114.91 | 89.281 | 32 | **17k** | yes | -16 to 174 | consistent; single mode only |
| 4441 | H7 comparator | 254,541.34 | 76.344 | 32 | **17k** | yes | -66 to 157 | consistent; single mode only |
| 4469 | tail-CEM start policy | 208,940.70 | 64.004 | 256 | **17k** | yes | -136 to 130 | consistent; single mode only |
| 4470 | tail-CEM champion | 214,968.93 | 65.590 | 256 | **17k** | yes | -123 to 137 | consistent; single mode only |

### Summary of section (a)

| Outcome | Rows |
| --- | ---: |
| Consistent with corrected 17,000-point Hardcore scoring | 84 |
| Consistent with historical 7,000-point Sequence scoring | 119 |
| Unclassifiable (no move count recorded) | 2 |
| **Consistent with neither** (transcription error) | **0** |
| **Ambiguous between modes** | **0** |

**Positive finding.** The ledger's numbers are arithmetically sound. Not one
`(score, moves)` pair is impossible under both bonuses, and not one is
compatible with both. There is no detectable transcription error in 205 reported
results. Independent corroboration: 26 approach sources carry a
`kLevelBonus == 7'000` build assertion, and every one of them matches a row this
audit classified as 7k from the arithmetic alone.

**Negative finding.** Of the 119 rows that are 7,000-point Sequence-scored, only
38 sit inside a history.md section that says so. **81 rows across 23 experiment
sections carry 7,000-point scores with no scoring-mode label at all.** The
ledger's own disclaimer (history.md:57, repeated at 3347) scopes itself to
"experiments below that identify `levelBonus: 7000`" — and `grep` finds that
string in exactly two places in the file, both of which are the disclaimer
itself. **The disclaimer's selector matches zero experiment sections.**

The unlabeled 7k sections are:

`accessible-energy-root-prior` (773) · `transition-reward-horizon` (1091–1092) ·
`fair-cem-optimizer` (1150–1159) · `fair-cem-depth4-interaction` (1220–1229) ·
`fair-depth4-s7` (1288) · `oracle-topology-residual-extension` (1452–1465) ·
`fair-d1-rollout-improvement` (1563–1564) · `ppo.hpp` v1 audit (1611–1613) ·
`ppo-v2` (1633–1634) · `fair-phase-energy-release` (1688–1700) ·
`fair-clear-reward-confirmation` (1751–1752) · `fair-selective-depth`
(1819–1829) · `fair-cycle-boundary-depth5` (1886) · `fair-depth5-s3` (1949–1950)
· `d4-phase5-value-veto` (2003) · `scaled-d4-distill` D2 diagnostic (2103–2104) ·
`fair-reveal-reward` (2167–2189) · `d4-d2-rollout-veto` (2255–2256) ·
`d4-d2-rollout-veto-quality-extension` (2764–2768) · `d2-vertical-ladder-probe`
(2830–2831) · `fair-vertical-ladder-depth4` (2845–2846) · `ntuple-tc`
(3092–3099) · `ntuple-phase-conditioned` (3178).

This is not cosmetic. It means the **entire fair-D4 ablation programme** —
seven-stratum chance, CEM-tuned leaf coefficients, selective D5, cycle-boundary
D5, full-width D5/s3, clear reward, reveal reward, vertical-ladder energy,
phase-energy release, the 25-move rollout veto, and the phase-5 value veto —
was conducted, gated, and rejected under the wrong level bonus, and history.md
does not say so at any of those sections.

### Two board-clear observations

The 70,000-point board-clear bonus is **never counted anywhere in the
repository**. `research/schemas/game-result-v1.schema.json` has no `boardClears`
field, no `rowRises` field, and no `scoringMode`/`levelBonus` field; neither does
the "Standard per-game record" list in `docs/benchmarks.md`. The residual column
in section (a) therefore cannot be decomposed into chains versus clears for any
run.

The residuals nevertheless carry a real signal that the ledger never states.
Public fair searches sit at **120–230 non-level points per move**. The
privileged future-aware oracle sits at **730–830** (history.md:568, 607, 722,
735) — a 4–6× higher chain-scoring density, not merely a longer game. That is a
quantitative restatement of the "oracle gap" that is more informative than the
raw score comparison, and it is currently invisible because no board-clear or
per-depth wave counter is retained.

---

## (b) Seed-range import table

`research/seeds/` contains only a README that says "Do not assign a new range
until historical usage has been conservatively imported." `docs/benchmarks.md`
repeats the requirement. **No registry exists.** The table below is the
conservative import, assembled from history.md, the frozen protocols, and
hard-coded constants in `approaches/`.

Legend: **CONSUMED** = the ledger records the range as read; **RESERVED** = the
ledger explicitly records it as declared-but-unopened; **UNDOCUMENTED** = the
constant exists in source with no ledger entry.

### Parity, self-test and calibration families

| Range | Size | Experiment | Citation | Stated role | Audited state |
| --- | ---: | --- | --- | --- | --- |
| `0x2d700000`–`0x2d7000ff` | 256 | native/TypeScript parity sweep | history.md:74 | parity | CONSUMED (reproduced in checkout) |
| `0x2e000000`–`0x2e01ffff` | 131,072 | declared parity/self-test/scale family | history.md:175 | parity/self-test | declared; no consumer recorded |
| `0xa5700000`–`0xa571869f` | 100,000 | native throughput benchmark | history.md:76 | benchmark | CONSUMED |
| `0xa51d0000`–`0xa51dffff` | 65,536 | source constant, no ledger entry | `approaches/` | unknown | UNDOCUMENTED |

### `0x3d1`–`0x3d6` — training and fitting lanes

| Range | Size | Experiment | Citation | Stated role | Audited state |
| --- | ---: | --- | --- | --- | --- |
| `0x3d100000`–`0x3d10270f` | 10,000 | n-tuple TC score-TD; replayed by phase-conditioned | history.md:3091, 3168 | training | CONSUMED (twice) |
| `0x3d200000`–`0x3d20003f` | 64 | burned probe: TC, phase-conditioned, optimistic-phase Stage A | history.md:3092, 3169, 4346 | burned development | CONSUMED (three times) |
| `0x3d210000`… | — | TC/phase-conditioned reserved | history.md:3134, 3217 | reserved | RESERVED |
| `0x3d300000`–`0x3d3fffff` | 1,048,576 | compiled Torch-env allowlist | history.md:3838 | allowlist | mixed (see rows below) |
| `0x3d300040`–`0x3d300043` | 4 | Torch smoke replay | history.md:3915 | smoke | CONSUMED |
| `0x3d310000`–`0x3d3102ff` | 768 | Torch clone-training D2 corpus | history.md:3798 | training | CONSUMED |
| `0x3d320000`–`0x3d3200ff` | 256 | Torch held-out validation corpus | history.md:3799 | validation (dev) | CONSUMED |
| `0x3d330000`–`0x3d3301ff` | 512 | Torch DAgger student corpus | history.md:3800 | training | CONSUMED |
| `0x3d340000`–`0x3d3401ff` | 512 | direct-PPO first collection (run aborted) | history.md:3922 | training | CONSUMED |
| `0x3d340200`–`0x3d340fff` | 3,584 | direct-PPO remainder | history.md:3928 | training | RESERVED |
| `0x3d350000`–`0x3d352fff` | 12,288 | direct-PPO training | history.md:3907 | training | RESERVED |
| `0x3d360000`–`0x3d36003f` | 64 | direct-PPO development | history.md:3908 | development | RESERVED |
| `0x3d390000`–`0x3d393fff` | 16,384 | grad-accum PPO training | history.md:4024 | training | CONSUMED |
| `0x3d3a0000`–`0x3d3a003f` | 64 | grad-accum PPO development | history.md:4032 | development | CONSUMED |
| `0x3d400000`–`~0x3d4087de` | ~34,783 | Rainbow-lite Q training (span **not recorded**; inferred from 34,783 games) | history.md:3476, 3509 | training | CONSUMED — **span must be reserved conservatively to `0x3d40ffff`** |
| `0x3d420000` | — | source constant, no ledger entry | `approaches/` | unknown | UNDOCUMENTED |
| `0x3d500000`–`~0x3d5009ff` | ~2,560 | evolved-public-policy generation batches (span **not recorded**) | history.md:3573 | fitting | CONSUMED — reserve to `0x3d50ffff` |
| `0x3d500000`–`0x3d51ffff` | 131,072 | compiled evo fitting allowlist | history.md:3577 | allowlist | mixed |
| `0x3d510000`–`0x3d51007f` | 128 | evo fitting tournament | history.md:3652 | fitting | CONSUMED |
| `0x3d510900`–`0x3d510907` | 8 | evo selective-search ablation (reuses fitting) | history.md:3614 | fitting reuse | CONSUMED |
| `0x3d520000` | — | source constant, no ledger entry | `approaches/` | unknown | UNDOCUMENTED |
| `0x3d600000`–`0x3d600003` | 4 | public rollout policy iteration fitting | history.md:3700 | fitting | CONSUMED |
| `0x3d6b0000`–`0x3d6b03ff` | 1,024 | oracle-manifold negatives; replayed by root prior | history.md:4079, 4129 | fitting | CONSUMED (twice) |
| `0x3d6b1000`–`0x3d6b6fff` | 24,576 | manifold policy training | history.md:4098 | training | RESERVED |
| `0x3d6c0000`–`0x3d6c001f` | 32 | manifold Stage A | history.md:4099 | screen | RESERVED |
| `0x3d6f0000`–`0x3d6f000f` | 16 | manifold root-prior fitting | history.md:4142 | fitting | CONSUMED |
| `0x3d6f1000`–`0x3d6f101f` | 32 | manifold root-prior screen | history.md:4148 | screen | RESERVED |
| `0x3d610000`, `0x3d630000`, `0x3d640000`, `0x3d650000`–`0x3d65ffff`, `0x3d660000`, `0x3d670000`–`0x3d677fff`, `0x3d680000`–`0x3d68001f`, `0x3d690000`–`0x3d69bfff`, `0x3d6a0000`–`0x3d6a7040`, `0x3d6d0000`, `0x3d6e4000` | — | ~24 further constants in `approaches/` with **no history.md entry** | source only | unknown | **UNDOCUMENTED — must be imported blind** |

### `0x3d7…` — the collision zone

The n-tuple baseline consumes **consecutive** seeds
(`src/core/native/ntuple.hpp:559`, `training_seed_start + game`), so:

| Range | Size | Experiment | Citation | Audited state |
| --- | ---: | --- | --- | --- |
| `0x3d700000`–`0x3d71869f` | 100,000 | n-tuple base training | history.md:99–104 | CONSUMED |
| `0x3d7186a0`–`0x3d77a11f` | 400,000 | hierarchical warm-start training | history.md:111–117 | CONSUMED |

**Twelve later ranges that the ledger or source calls "fresh", "training-only",
or "collection" fall inside the first 100,000-game training block:**

| Overlapping range | Described as | Citation | Offset into training block |
| --- | --- | --- | ---: |
| `0x3d700000` | privileged-oracle upper-bound seed | history.md:568 | game 0 |
| `0x3d700000` | `throughput-probe.cpp` seed start | `approaches/baselines-diagnostics/throughput-probe/throughput-probe.cpp:53` | game 0 |
| `0x3d700000` | `evolution.cpp` `kTrainingSeedStart` | `approaches/heuristic-search/evolution/evolution.cpp:32` | game 0 |
| `0x3d700100` | `phase-benchmark.cpp` default | `approaches/baselines-diagnostics/phase-benchmark/phase-benchmark.cpp:122` | 256 |
| `0x3d700300` / `0x3d700400` | edge-priority screen / confirm | `approaches/heuristic-search/edge-priority/edge-priority-lab.cpp:29–30` | 768 / 1,024 |
| `0x3d704000`… | survival-value-scale collection | history.md:347 | 16,384 |
| `0x3d706000`… | structured-NNUE collection | history.md:385 | 24,576 |
| `0x3d706800`… | denoised-value collection | history.md:447 | 26,624 |
| `0x3d709000` / `0x3d709100` | nnue-selective screen / confirmation | `approaches/tree-search/nnue-guided/nnue-selective-search.cpp:11–12` | 36,864 |
| `0x3d70b000`–`03` | **"fresh"** nnue-guided screen | history.md:658 | 45,056 |
| `0x3d70c000`–`03` | **"fresh"** root-quadrature screen | history.md:673 | 49,152 |
| `0x3d70d…` | **"fresh"** root-ensemble training | history.md:638 | 53,248 |
| `0x3d70e000`–`03` | **"fresh"** exact-depth-4 screen | history.md:687 | 57,344 |
| `0x3d70f000`–`0f` | **"fresh"** topology-audit seeds | history.md:721 | 61,440 |

The `0x3d7a…`–`0x3d7f…` oracle-distillation ranges (history.md:603–616) end
above `0x3d77a11f` and do **not** collide.

| Range | Size | Experiment | Citation | Audited state |
| --- | ---: | --- | --- | --- |
| `0x3d7a0000`…, `0x3d7b0000`… | — | oracle / behavior label fitting | history.md:603–604 | CONSUMED |
| `0x3d7c0000`…, `0x3d7d0000`… | — | whole-game label holdouts | history.md:604–605 | CONSUMED |
| `0x3d7e0000`–`0x3d7e0007` | 8 | oracle-distill policy screen | history.md:612 | CONSUMED |
| `0x3d7f0000`–`0x3d7f000f` | 16 | oracle-distill confirmation | history.md:616 | CONSUMED |

### `0x3d9`–`0x3df` — development and fitting cohorts

| Range | Size | Experiment | Citation | Stated role | Audited state |
| --- | ---: | --- | --- | --- | --- |
| `0x3d950000`–`0x3d95000b` | 12 | oracle 500-move feasibility ceiling | history.md:734 | training-only | CONSUMED |
| `0x3d9a0000`–`0x3d9a0017` | 24 | transition-reward diagnostic | history.md:1089 | training-only | CONSUMED |
| `0x3d9c…` | — | topology-residual fitting | history.md:1332 | fitting | CONSUMED |
| `0x3d9d0000`–`0x3d9d0007` | 8 | topology-residual heldout; replayed by extension | history.md:1332, 1405 | heldout | CONSUMED (twice) |
| `0x3d9d0008`–`0x3d9d000f` | 8 | topology-residual extension | history.md:1407 | prediction-only | CONSUMED |
| `0x3d9e0000`–`0x3d9e00ff` | 256 | vertical-ladder D2 coefficient sweep | history.md:2829 | training | CONSUMED |
| `0x3d9f0000`–`0x3d9f0007` | 8 | vertical-ladder D4 transfer | history.md:2840 | training-only | CONSUMED |
| `0x3da00000`–`0x3da0001f` | 32 | observable-MCTS origin games | history.md:814 | training-only | CONSUMED |
| `0x3da10000`–`0x3da1000f` | 16 | observable-MCTS disjoint roots | history.md:819 | heldout | CONSUMED |
| `0x3da20000`–`0x3da2000f` | 16 | CEM-D4 interaction training | history.md:1213 | training | CONSUMED |
| `0x3da30000`–`0x3da3000f` | 16 | CEM-D4 interaction heldout | history.md:1214 | heldout | CONSUMED |
| `0x3da41000`–`0x3da7ffff` | 258,048 | regenerative expert iteration lane | history.md:4231 | sealed training lane | CONSUMED |
| `0x3dac0000`–`0x3dadffff` | 131,072 | primal-dual actor-critic | history.md:4194 | sealed training lane | CONSUMED |
| `0x3dae…`, `0x3daf…` | — | primal-dual gameplay gates | history.md:4201 | gate | RESERVED |
| `0x3db00000`–`0x3db0001f` | 32 | MCTS-confidence fitting origins | history.md:837 | fitting | CONSUMED |
| `0x3db10000`–`0x3db1000f` | 16 | MCTS-confidence heldout | history.md:848 | heldout | CONSUMED |
| `0x3dc10000`–`0x3dc1001f` | 32 | fair-leaf CEM heldout | history.md:1150 | heldout | CONSUMED |
| `0x3dd00000`–`0x3dd00003` | 4 | selective-depth fitting menu | history.md:1807 | fitting | CONSUMED |
| `0x3dd10000`–`0x3dd10007` | 8 | selective-depth training heldout | history.md:1818 | heldout | CONSUMED |
| `0x3de10000`–`0x3de10007` | 8 | fair D4/s7 fitting | history.md:1287 | fitting | CONSUMED |
| `0x3de30000`–`0x3de3001f` | 32 | d4-q-clone gameplay | history.md:1542 | training-only | RESERVED |
| `0x3de40000` | 1 | D5/s3 pilot triple | history.md:1941–1943 | fitting | CONSUMED |
| `0x3de40001`–`0x3de40007` | 7 | D5/s3 fitting remainder | history.md:1944 | fitting | RESERVED |
| `0x3de50000`–`0x3de50003` | 4 | phase-energy fitting (2 arms incomplete) | history.md:1695 | fitting | CONSUMED (partial) |
| `0x3de60000`–`0x3de60007` | 8 | clear-reward heldout | history.md:1750 | heldout | CONSUMED |
| `0x3de70000` | 1 | cycle-boundary D5 pilot pair | history.md:1881 | fitting | CONSUMED |
| `0x3de70001` | 1 | cycle-boundary fitting remainder | history.md:1893 | fitting | RESERVED |
| `0x3de80000`–`0x3de80007` | 8 | cycle-boundary heldout | history.md:1894 | heldout | RESERVED |
| `0x3de90000` | 1 | phase-5 value-veto pilot pair | history.md:2002 | fitting | CONSUMED |
| `0x3de90001`–`0x3de90003` | 3 | phase-5 veto fitting remainder | history.md:2014 | fitting | RESERVED |
| `0x3dea0000`–`0x3dea0007` | 8 | phase-5 veto heldout | history.md:2016 | heldout | RESERVED |
| `0x3ded0000` | 1 | rollout-veto pilot — executed **at least four times** (pilot, trace replay, exact-compressed replay, cache-free replay) | history.md:2254, 2285, 2462, 2500 | fitting | CONSUMED |
| `0x3ded0001`–`0x3ded0003` | 3 | rollout-veto quality extension | history.md:2750 | fitting | CONSUMED |
| `0x3dee0000`–`0x3dee0007` | 8 | rollout-veto heldout | history.md:2273 | heldout | RESERVED |
| `0x3def0000` | 1 | reveal-reward pre-observed diagnostic | history.md:2166 | fitting | CONSUMED |
| `0x3def0001`–`0x3def0003` | 3 | reveal-reward fitting remainder | history.md:2170 | fitting | CONSUMED |
| `0x3df00000`–`0x3df0000b` | 12 | fair-D1 rollout-improvement fitting | history.md:1561 | fitting | CONSUMED |
| `0x3df10000`–`0x3df1000f` | 16 | fair-D1 rollout heldout | history.md:1572 | one-shot heldout | RESERVED |
| `0x3df20000`–`0x3df20017` | 24 | scaled-D4-distill fitting (`0x3df20000` executed twice) | history.md:2067, 2073 | fitting | CONSUMED |
| `0x3df30000`–`0x3df3000b` | 12 | scaled-D4-distill holdout; later re-read by an inadvertent D2 sanity run | history.md:2075, 2101 | holdout | CONSUMED (twice) |
| `0x3df40000`–`0x3df40007` | 8 | reveal-reward heldout | history.md:2184 | heldout | CONSUMED |

### `0x3e…` — fresh screens and gated confirmations (40 declared subranges)

| Consumed | Reserved and never read |
| --- | --- |
| `0x3e820000…`, `0x3e830000…`, `0x3e840000–003`, `0x3e850000–007`, `0x3e870000–03`, `0x3e890000–07`, `0x3e8b0000–03`, `0x3e930000–07`, `0x3e950000–007`, `0x3e960000–00f`, `0x3e9b0000–003`, `0x3e9c0000–007`, `0x3e9d0000–007`, `0x3ea10000–007`, `0x3ea30000–007`, `0x3ea50000–007`, `0x3ea70000–007`, `0x3ea90000–007`, `0x3eaa0000–00f` | `0x3e7b…`, `0x3e7c…`, `0x3e940000–0f`, `0x3e990000–03`, `0x3e9a0000–07`, `0x3e9e0000–00f`, `0x3e9f0000–07`, `0x3ea00000–0f`, `0x3ea20000–00f`, `0x3ea40000–00f`, `0x3ea60000–00f`, `0x3ea80000–00f`, `0x3eab0000–007`, `0x3eac0000–00f`, `0x3ead0000–007`, `0x3eae0000–00f`, `0x3eb10000–007`, `0x3eb20000–00f`, `0x3eb30000–007`, `0x3eb40000–00f`, `0x3eb50000–007`, `0x3eb60000–00f`, `0x3eb70000–007`, `0x3eb80000–00f`, `0x3ebb0000–007`, `0x3ebc0000–00f`, `0x3ebd…`, `0x3ebe…` |

No two `0x3e…` subranges collide. `0x3ea90000–007` / `0x3eaa0000–00f` were
declared unread by `oracle-topology-residual` (history.md:1375) and later
legitimately consumed by `oracle-topology-residual-extension`
(history.md:1451, 1457); the import table must record them as CONSUMED.

### `0x3f…`, `0x4d…`, `0x5d…`, `0x6d…`, `0x6e…`

| Range | Size | Experiment | Citation | Stated role | Audited state |
| --- | ---: | --- | --- | --- | --- |
| `0x3f000000`–`~0x3f0003ff` | ~1,024 | PPO v1 audit training (8×128 games) | history.md:1609 | training | CONSUMED (span not stated) |
| `0x3f100000`–`0x3f10003f` | 64 | PPO v1/v2 fitting probe | history.md:1610 | fitting probe | CONSUMED |
| `0x3f200000`… | — | PPO v2 heldout | history.md:1640 | heldout | RESERVED |
| `0x3f010000`, `0x3f030000`, `0x3f040000` | — | source constants, no ledger entry | `approaches/` | unknown | UNDOCUMENTED |
| `0x4d400000`–`0x4d40001f` | 32 | Rainbow Stage A random probe | history.md:3477 | development probe | CONSUMED |
| `0x4d400020`–`0x4d40003f` | 32 | Rainbow Stage B fair-D1 probe | history.md:3480 | development probe | CONSUMED |
| `0x4d400040`–`0x4d40007f` | 64 | Rainbow Stage C probe | history.md:3482 | development probe | RESERVED |
| `0x4d500000`–`0x4d50007f` | 128 | evolved-public-policy probe | history.md:3578, 3634 | probe | RESERVED |
| `0x4d600000`–`0x4d600007` | 8 | public rollout PI development | history.md:3715 | development | RESERVED |
| `0x4d700000`–`0x4d70003f` | 64 | n-tuple / phase-blend / evolution development probe | history.md:103, 137, 156 | burned development | CONSUMED (repeatedly) |
| `0x4d410000`, `0x4d610000`, `0x4d630000`, `0x4d65c000`, `0x4d670000`, `0x4d690000`, `0x4d69c000`, `0x4d6a0000`–`0x4d6a7000`, `0x4d6b0000`, `0x4d6b1000`, `0x4d6c1000`, `0x4d6d0000`, `0x4d6e4000`, `0x4d6f0000`, `0x4d710000`, `0x4d730000` | — | ~17 constants in `approaches/` with **no ledger entry** | source only | unknown | **UNDOCUMENTED** |
| `0x5d700000`… | — | phase-blend / phase-fair calibration and tuning | history.md:156, `phase-fair-combination/main.ts:35` | tuning | CONSUMED (span not stated) |
| `0x6d000000`–`0x6dffffff` | 16,777,216 | optimistic-phase n-tuple fit lane (1,057,844 games used) | history.md:4338 | fitting | CONSUMED |
| `0x6e000000`–`0x6e0000ff` | 256 | optimistic-phase development cohort | history.md:4341 | development | RESERVED (capability absent from binary) |

### `0x7d…` protected and `0xd7…` final

| Declared bank | Citation | State |
| --- | --- | --- |
| `0x7d000000`–`0x7d00ffff` | history.md:181 | **UNOPENED** — every experiment section that mentions it records it as unread |
| `0xd7000000`–`0xd70000ff` | history.md:183 | **UNOPENED** — same |

I found no ledger statement, artifact, or gate result anywhere that indicates a
protected or final seed was read. `status.md:11–12` is consistent with the
ledger on this point.

However, **the declared bank boundaries are too narrow for the constants the
sources already use.** `approaches/` hard-codes protected/final constants far
outside the declared banks:

- `0x7d600000`, `0x7d630000`, `0x7d65c000`, `0x7d660000`, `0x7d670000`,
  `0x7d690000`, `0x7d69c000`, `0x7d6a0000`, `0x7d6a1000`, `0x7d6a4000`,
  `0x7d6a5000`, `0x7d6a6000`, `0x7d6a7000`, `0x7d6b0000`, `0x7d6b1000`,
  `0x7d6c1000`, `0x7d6d0000`, `0x7d6e5000`, `0x7d6f0000`, `0x7d700000`,
  `0x7d730000`
- `0xd7600000`, `0xd7630000`, `0xd765c000`, `0xd7660000`, `0xd7670000`,
  `0xd7690000`, `0xd769c000`, `0xd76a0000`, `0xd76a1000`, `0xd76a4000`–
  `0xd76a7000`, `0xd76c0000`, `0xd76c8000`, `0xd76d0000`, `0xd76e5000`,
  `0xd76f0000`, `0xd7738000`

A conservative importer must reserve **the entire `0x7d……` and `0xd7……`
top-byte families**, not the two narrow banks named in history.md:181–183.

### Importer hazards

1. **ASCII domain tags are not seeds.** `0x4d435453` = `"MCTS"`,
   `0x4d504f4c` = `"MPOL"`, `0x4d52564c` = `"MRVL"`, `0x4d444953` = `"MDIS"`,
   `0x6e455301`, `0x6e4c4301`, `0x3d40c0de`, `0x3f00c0de` are RNG-domain
   separators, not game seeds. A naive `grep`-based importer will misclassify
   them.
2. **`0xd7075eed` is the fair-D4 *policy* salt** (`baselines-v1.json`,
   `algorithm.policySeedHex`), not a final-cohort game seed. It nevertheless
   sits inside the `0xd7` final-seed family — a namespace collision worth fixing
   in a successor manifest.
3. **Six ranges have no recorded span** and must be reserved generously:
   Rainbow training (`0x3d4…`), evo generation batches (`0x3d50…`), PPO v1
   training (`0x3f00…`), phase-blend tuning (`0x5d70…`), and the
   `0x3d7a…`/`0x3d7b…`/`0x3d7c…`/`0x3d7d…` oracle label families.
4. **Nine complete-game experiments record no seed range at all** — see
   Finding S3.


---

## (c) Findings by severity

### CRITICAL

**C1 — A frozen protocol embeds a 7,000-point number as the "corrected" D4
reference, making its qualification gate unsatisfiable.**
`artifacts/protocols/optimistic-phase-ntuple/protocol.json:101` requires, at 100
million transitions, that

> `"corrected D4 reproduces frozen means 176925.25 score and 116.375 moves"`

176,925.25 is the **7,000-point Sequence-scored** confirmation mean
(history.md:970). The corrected-17k mean for the identical cohort and identical
trajectories is **400,675.25** (history.md:3337). A corrected-scoring D4 run can
never reproduce 176,925.25, so this gate condition is permanently false and the
protocol's Stage-B qualification path is dead. The same protocol also demands
`"search aggregate score and moves are each >= corrected D4 on identical seeds"`
— which, read together with line 101, is self-contradictory about which "D4" is
meant.

`AGENTS.md` forbids editing frozen protocol artifacts. The correct remedy is a
new versioned protocol that names the scoring mode explicitly and cites
history.md:3337; the v1 record must be retained with this defect documented.

**C2 — Two preregistered gate rejections reverse under corrected scoring.**
Because the corrected replay proves that the level bonus does not change fair
search decisions (section 0), every 7k score-mean gate can be exactly recomputed
as `score + 2,000·moves − 9,250`.

| Experiment | Citation | 7k Δscore / Δmoves | Corrected Δscore | Recorded outcome | Corrected outcome |
| --- | --- | ---: | ---: | --- | --- |
| Fair D4 seven-stratum (`fair-depth4-s7`) | history.md:1287–1294 | −163.63 / +2.250 | **+4,336** | rejected at fitting; "the score-mean failure is decisive" | score mean **improves**; lower-tail-25% score and survival already improved (54,922→63,230; 42.5→47.5) — **the fitting gate would have passed** |
| Fair-leaf CEM (`fair-cem-optimizer`) fresh screen | history.md:1158–1163 | −905.38 / +1.875 | **+2,845** | rejected; "because both means did not improve, `0x3ea40000…00f` was never read" | **both means improve** — the confirmation range would have been opened |

These are not speculative rescorings. They are arithmetic on the ledger's own
paired means, using a rescaling the ledger itself demonstrates. The two
rejections are therefore **scoring-mode artefacts, not scientific results**, and
`docs/strategies.md:157–162` ("more chance samples … did not reliably improve the
whole game") and the corresponding `experiment-index.md` row
("Seven-stratum D4 … score-neutral to worse") overstate the evidence.

For completeness I recomputed the third close call and it does **not** flip: the
25-move rollout-veto quality extension (history.md:2754–2775) fails its
lower-half-score retention gate at 88.38% under 7k and at **89.62% under
corrected scoring** — narrower, but still below the 90% floor, and its
"2 of 3 new pairs" sub-gate is unaffected. Its rejection stands.

I also checked every other paired comparison in the 7k set for a sign change:
these two are the only ones. The rest have Δscore and Δmoves of the same sign,
so rescoring strengthens rather than reverses them.

**C3 — 81 of 119 seven-thousand-point results carry no scoring-mode label, and
the ledger's disclaimer selects nothing.** history.md:57 scopes the archival
warning to "experiments below that identify `levelBonus: 7000`"; that literal
string appears nowhere in the file except in the two disclaimers themselves
(history.md:57, 3347). Twenty-three experiment sections — including the entire
fair-D4 ablation programme — present 7k scores as if they were corrected-mode
results. See section (a) for the enumerated list.

### HIGH

**H1 — The fitting selection that opened the reveal-reward heldout is entirely
produced by one pre-observed game, and reverses on the three genuinely fresh
ones.** history.md:2166–2184. Seed `0x3def0000` was run first as "a separately
persisted diagnostic", frozen byte-for-byte, and then **included in the four-game
fitting mean** that selected the winner. Removing it:

| Arm | 4-game fitting mean (h:2176–2178) | Pre-observed `0x3def0000` (h:2167–2168) | Mean of the 3 fresh games |
| --- | ---: | ---: | ---: |
| Stock | 110,139.25 / 71.25 | 140,681 / 90 | **99,959 / 65.0** |
| Reveal-only | 117,299.25 / 77.50 | 171,147 / 108 | 99,350 / 56.7 |
| Balanced (**selected**) | 152,413.25 / 98.75 | 370,588 / 225 | **79,688 / 56.7** |

The selected arm is **20% worse on score and 13% worse on moves than stock** on
the three fitting games that were not already observed; 60.8% of its total
fitting score comes from the single pre-observed game. The ledger's own lower
quartiles corroborate this (balanced Q25 74,179.50 vs stock 74,049.75 — a
0.2% difference against a 38% mean difference). This exactly predicts the
heldout reversal that followed. history.md discloses the pre-observation
(2166–2172) but never states the dominance, and the gate was not re-evaluated
without the pre-observed seed.

**H2 — `status.md`'s headline 64-game D4 reference has no identified cohort, no
dispersion statistics, and no censoring statement.** The number
308,295.578 / 90.031 appears exactly once in the ledger (history.md:4234), as an
internal bootstrap comparator inside the *regenerative expert iteration*
experiment, whose seed lane is `0x3da41000…0x3da7ffff`. Which 64 seeds were used
is not recorded. `docs/methodology.md:69–77` requires mean, median, lower
quartile, minimum, move Q25, censor count, clears/reveals per move, chain depth,
paired deltas, and confidence bounds for a reported cohort; **none of these are
recorded for the repository's primary reference number.** `status.md:8–9`,
`status.md:22`, `experiment-index.md:41–44`, `README.md:54`, and
`baselines-v1.json.strengthEvidence.broadMeanScore` all propagate it.

**H3 — Twelve later "fresh"/"training-only" seed ranges overlap the n-tuple
training block.** See the `0x3d7…` collision table in section (b). Five of them
are described in history.md with the word "fresh" (658, 673, 638, 687, 721). The
scientific contamination risk is low — the colliding consumers are unrelated
fair-expectimax and oracle programs that never saw the n-tuple model — but the
ledger's freshness claim is factually wrong, and a seed allocator that imports
the ledger at face value will re-issue burned seeds.

**H4 — Fourteen sources whose ledger results are 7k-scored have no
`kLevelBonus == 7'000` build lock.** 26 sources do carry the lock;
`experiment-index.md:35–37` implies the lock protects "several Sequence-scored
sources". The unlocked 7k-era sources are:
`fair-clear-reward-confirmation.cpp`, `fair-reveal-reward.cpp`,
`oracle-topology-residual-extension.cpp`,
`d4-d2-rollout-veto-quality-extension.cpp`, `d2-vertical-ladder-probe.cpp`,
`fair-vertical-ladder-depth4.cpp`, `ntuple-tc.cpp`,
`ntuple-phase-conditioned.cpp`, `scaled-d4-distill.cpp`,
`transition-reward-horizon.cpp`, `d4-d2-rollout-veto-exact-compressed.cpp`,
`d4-d2-rollout-veto-cache-free.cpp`, `d2-rollout-teacher-compression.cpp`,
`d2-long-outcome-ranker.cpp`. Rebuilding any of them against the current 17k
engine will silently produce numbers that disagree with the ledger. (Three of
them assert the 7k pilot totals at replay and would abort loudly, which is the
desired behaviour — the remainder would not.)

**H5 — The 70,000-point board-clear bonus is unaccounted for everywhere.**
Neither `research/schemas/game-result-v1.schema.json` nor the "Standard per-game
record" list in `docs/benchmarks.md` retains a board-clear count or a row-rise
count, and no history.md entry reports one. Consequently the score identity
cannot be verified per game for any existing or future run, and the large
oracle residuals (730–830 points/move, history.md:568, 607, 722, 735) cannot be
attributed between deep chains and board clears.

### MEDIUM

**M1 — `history.md` self-describes as chronological but is not.** Line 3: "the
unabridged chronological record". The scoring-mode sequence disproves it: 17k
(144–617) → 7k (514–555) → 17k (568–617) → 7k (640–3178) → 17k (3335+). Since
the 2026-08-15 scoring error and its correction are dated events, the sections
are ordered by topic. Readers using the file to reconstruct what was known when
a gate was frozen will be misled.

**M2 — Nine complete-game experiments record no seed range at all.** The entire
"Explicit reservoir and constructive-cycle policies" block (history.md:4389–4456)
and "Tail-focused complete-game CEM" (4458–4484) cite SHA-256 values for every
source and artifact but no seed range for: vertical-reservoir 128-game
tournament (4398), viability-controller 32-game Stage A (4409),
constructive-spectrum 32-game Stage A (4421), constructive D4-integration
fitting quartet (4425), horizon-scale 32 fitting games (4437), H12 Pareto risk
gate (4441), tail-survival-CEM 33,792 candidate-games and 256-game tournament
(4465–4466). The 477-root, eight-origin H200 deployment panel that underpins the
four most recent conclusions (4257–4604) likewise records the corpus SHA-256 but
never the origin game seeds. These cannot be imported into a seed registry.

**M3 — `d4-phase5-value-veto` uses the wrong level bonus as a policy
parameter, not merely as a score label.** history.md:1998–1999: a challenger
must have "a completed-D4 root-Q loss no greater than one canonical 7,000-point
level bonus". The same 7,000 band is baked into `d4-d2-rollout-veto`
(2250), its exact-compressed variant (2451), the teacher-compression menu
(2581), and the long-outcome veto classifier (2901, 2914). Under corrected
scoring the analogous band is 17,000, so these are not just mislabeled results —
they are policies whose admission thresholds are calibrated to the wrong
constant. Any port must re-derive the band, which makes it a **new candidate**,
not a rescoring.

**M4 — `status.md:21` presents a rescoring replay as a cohort result.** The row
"Fair D4 vs D3: 400,675.25/116.375 vs 235,071.25/71 over 8 games … Recorded,
small confirmation cohort" does not say that these are the *same eight already
consumed development seeds* (`0x3e9c0000…007`) replayed under corrected scoring,
with byte-identical trajectories. `docs/strategies.md:102–103` gets this right
("corrected-score replay on an already-consumed eight-game confirmation
cohort"); `status.md` and `README.md:51–54` do not.

**M5 — `experiment-index.md` overstates the rollout-veto pilot.** Its row reads
"Runtime-paused — ledger-recorded; **one promising pilot passed quality gates**
but missed the runtime limit by a wide margin." history.md:2263 says the
opposite: "This is only one pair, not statistical evidence." No quality gate was
evaluated on the pilot; the four-game quality gate that was later run
**failed** (history.md:2771–2775). The index also describes the "original
404,000-point pilot" without noting that 404,047 is a single 7,000-point-scored
game.

**M6 — `docs/strategies.md:281–283` quotes the 404,047/250 vs 159,616/105 pilot
with no scoring label**, inside a document that states at 60–64 that
7,000-point absolute scores "are not evidence for the Hardcore target". Same
omission in the "Compact evidence map" row (375).

**M7 — Small-n claims.** Twenty-three reported comparisons rest on n ≤ 4, and
eleven on n = 1. `docs/benchmarks.md` classifies 1–8 games as `PILOT` with "no
strength claim". Single-game rows: history.md 568, 1465 (×4), 1688–1690 (×5),
1886, 1949–1950, 2003, 2167–2168, 2255–2256, 2763–2766. Four-game rows: 226,
296, 514, 554, 640, 658, 673, 687, 793, 962, 1697, 2176, 2768, 3335, 3707, 4425.
The ledger generally states this correctly in prose (e.g. 1823–1825, 1830–1832,
922–923, 2263), but `experiment-index.md` and `status.md` frequently carry the
resulting numbers forward without the caveat.

**M8 — Means dominated by one game, disclosed unevenly.** history.md discloses
the dominance at 1825 (selective-D5 heldout: "removing the single
+201,765-point/+120-move pair made the means negative"), 1831 (screen), 922–923
(fair-only confirmation), and 1162 (CEM screen). It does **not** disclose it at
2768–2771, where the four-game rollout-veto quality mean gain of +67,652 falls
to +8,725 once the frozen pilot game is removed — a 7.8× dependence on one
game — while the text says only "all four leave-one-out subsets were positive"
(technically true, materially misleading). And it does not disclose it at
2176–2178 (Finding H1).

**M9 — The `0x4d…` family has two contradictory declared roles.**
history.md:179–180 declares `0x4d70…` and `0x5d70…` as "historical
development/tuning ranges that have already been observed"; history.md:626 and
4099 instead treat the whole `0x4d` byte as protected ("every protected `0x4d`,
`0x7d`, and `0xd7` cohort remain unopened"). Both cannot be true:
`0x4d700000…3f` (103, 137, 156) and `0x4d400000…3f` (3477, 3480) were read.
`status.md`'s protected/final assertion is unaffected — those are `0x7d`/`0xd7`
— but the seed registry must not inherit the "`0x4d` is protected" wording.

### LOW

**L1 — history.md:462–469 is internally contradictory about the denoised-value
screen.** Line 462 calls it "that legacy-scoring run"; line 467–468 says "its
reported absolute point totals use the verified 17,000-point Hardcore bonus".
No point totals are reported for that screen or confirmation at all — only move
means (78.75/81.25 and 79.5/88.125). The scoring claim is therefore
unverifiable from the ledger, and `experiment-index.md`'s "a small confirmation
improved means" inherits the ambiguity.

**L2 — history.md:891–903 makes a scoring claim about a pilot whose scores were
never recorded.** "The historical 155/160-move pilot … Its 17,000-point bonus is
now verified as correct." Move counts, clears, and reveals are given; no score
is. The section also correctly notes n = 2 non-independent training seeds.

**L3 — `0xd7075eed` (fair-D4 policy salt) sits inside the `0xd7` final-cohort
family.** No leakage: it is a policy-derived salt, never a game seed. But it will
be misclassified by any automated seed importer and should be moved out of the
reserved byte in a successor manifest.

**L4 — `status.md:18` ("122 local tests") was not re-verified by this audit**;
verifying it requires executing the test suite, which is outside a read-only
claim-arithmetic audit.

**L5 — Numbers that do check out.** For the record, I recomputed and confirmed:
the quality-extension four-game means, lower halves, and retention percentages
(history.md:2763–2773, all exact); the corrected-replay paired gains of
+81,423 / +22 and +165,604 / +45.375 (3336, 3338); and the D2/D4 12-game
diagnostic move mean of 77.17 = 926 roots / 12 games (2092, 2104). No
arithmetic error was found in any of them.

---

## (d) Claims that should be downgraded, with proposed wording

The coordinator owns these edits; none has been applied.

| # | Location | Current wording | Proposed wording |
| --- | --- | --- | --- |
| D1 | `status.md:21` | "Fair D4 vs D3: 400,675.25/116.375 vs 235,071.25/71 over 8 games \| Detailed ledger \| Recorded, small confirmation cohort" | "Fair D4 vs D3: 400,675.25/116.375 vs 235,071.25/71 over 8 games \| Corrected-scoring replay of the already-consumed `0x3e9c0000…007` development cohort; trajectories identical to the original 7,000-point run \| Ledger-recorded; burned development seeds, not a fresh cohort" |
| D2 | `status.md:22`, `README.md:54`, `experiment-index.md:41–44`, `baselines-v1.json` | "Fair D4: 308,295.578 points and 90.031 moves over 64 games \| Recorded broader reference cohort" | "Fair D4: 308,295.578 points and 90.031 moves over 64 games \| Ledger-recorded as an internal bootstrap comparator inside the regenerative-expert-iteration run (history.md:4234); **the 64 seeds, dispersion, censoring, and flow statistics required by `methodology.md` were not retained** \| Provisional reference mean pending a re-run under the benchmark contract" |
| D3 | `status.md:23`, `README.md:58` | "One D4 game scored 1,246,684 \| Task-record only \| Anecdote; not an average or qualification" | "One D4 game is reported at 1,246,684 points \| Task-record only; **no move count, seed, cohort, scoring mode, or artifact is recorded anywhere in this repository** \| Unverified anecdote of unknown scoring mode. Under corrected 17k scoring it implies roughly 68–70 rises and ~340–360 moves; under 7k scoring roughly 140–156 rises and ~700–780 moves. It cannot be classified, and it must not be cited as evidence of anything." |
| D4 | `experiment-index.md` (seven-stratum D4 row) | "Rejected — ledger-recorded; it was score-neutral to worse, reduced flow, and cost about 3.8 times more work." | "Rejected under **7,000-point Sequence scoring** — ledger-recorded. Its −163.63-point mean gap **reverses to +4,336 when rescored to the corrected 17,000-point bonus** (identical trajectories), so the score-mean rejection does not hold under Hardcore rules. The flow regression (clears 1.8875→1.8598, reveals 1.0349→0.9923) and the 3.79× work cost are scoring-independent and remain valid grounds for not adopting it. **A corrected-scoring re-test is a live open question.**" |
| D5 | `experiment-index.md` (fair-leaf CEM row) | "Rejected — ledger-recorded; the D3 screen and D4 heldout/tail gates failed." | "Rejected under **7,000-point Sequence scoring** — ledger-recorded. The D3 fresh screen's −905-point mean gap **reverses to +2,845 under corrected scoring**, so that screen no longer rejects the candidate; the independent D4 interaction heldout (−17,835 points, −10,938 moves, worst-quartile regression) fails under both scoring modes and is the surviving ground for rejection." |
| D6 | `experiment-index.md` (25-move rollout-veto row) | "Runtime-paused — ledger-recorded; one promising pilot passed quality gates but missed the runtime limit by a wide margin." | "Runtime-paused — ledger-recorded. A **single 7,000-point-scored game** (404,047/250 vs 159,616/105, history.md:2254–2256) was strikingly positive; **no quality gate was evaluated on it**, and the later four-game quality extension failed its lower-half-score and per-pair gates. The runtime projection exceeded its ceiling by >4×." |
| D7 | `strategies.md:281–283` and `:375` | "A public 25-move, seven-scenario continuation driven by completed D2 produced a striking single pilot: 404,047 points and 250 moves versus 159,616 and 105 for D4." | Insert "**under the historical 7,000-point Sequence level bonus**" after "single pilot", matching the treatment already given to the oracle result at `:307–309`. |
| D8 | `strategies.md:157–162` | "In particular, more chance samples, explicit clear/reveal rewards, risk-sensitive root aggregation, and learned/tuned leaf changes did not reliably improve the whole game." | "…did not reliably improve the whole game **on the 7,000-point Sequence-scored evidence available. The seven-stratum and CEM-leaf score-mean rejections do not survive rescoring to the corrected 17,000-point bonus and should be treated as open, not settled.**" |
| D9 | `experiment-index.md` (fair D3 reference row) | "Completed — ledger-recorded; it passed its mean-improvement screen and confirmation." | "Completed — ledger-recorded; it passed its mean-improvement screen and confirmation **under 7,000-point Sequence scoring** (history.md:905–919). Only the D3-vs-D4 comparison was later replayed at corrected scoring; the D3-vs-CFPI qualification itself has never been re-scored." |
| D10 | `experiment-index.md` (denoised public value row) | "Completed — ledger-recorded; prediction gates passed and a small confirmation improved means…" | "Completed — ledger-recorded; prediction gates passed and a small confirmation **improved mean survival (79.5 → 88.125 moves); no score means were retained for the screen or confirmation, and history.md:462–468 is internally inconsistent about their scoring mode.**" |
| D11 | `history.md:57` and `:3347` | "Experiments below that identify `levelBonus: 7000` are preserved as historical Sequence-scored evidence." | Replace the selector, which currently matches nothing, with an explicit index: add a per-section `Scoring: 7,000-point Sequence (archival)` or `Scoring: 17,000-point Hardcore (corrected)` line to each of the ~40 experiment sections, using the classification in section (a) of this audit. Retain the original disclaimer text alongside it. |
| D12 | `history.md:3` | "This is the unabridged chronological record of the Drop7 strategy experiments." | "This is the unabridged record of the Drop7 strategy experiments, **grouped by strategy family rather than strictly by date**; use the per-section scoring label and the mode-correction note to establish what was known when a gate was frozen." |
| D13 | `history.md:2768–2771` | "All four leave-one-out subsets were positive, and lower-half moves retained 91.30%." | Append: "**The aggregate gain is dominated by the frozen pilot game: excluding `0x3ded0000`, the candidate mean falls from +67,652 to +8,725 points over the three genuinely new pairs.**" |
| D14 | `history.md:2180–2184` | "Balanced was selected by mean score and improved all three preregistered fitting means over stock." | Append: "**60.8% of the balanced arm's fitting score comes from the single pre-observed seed `0x3def0000`. On the three fitting games opened after that observation, balanced averages 79,688 points / 56.7 moves against stock's 99,959 / 65.0 — i.e. the selection reverses without the pre-observed game, which anticipates the heldout failure.**" |
| D15 | `research/seeds/README.md` | (registry not yet created) | Create the registry from section (b) of this audit. Reserve the whole `0x7d……` and `0xd7……` byte families, not the narrow banks at history.md:181–183; mark the six ranges with unrecorded spans as generously over-reserved; and record the nine experiments in Finding M2 as *seed range unknown — do not reuse any `0x3d`/`0x3e` range until reconstructed*. |
| D16 | `research/schemas/game-result-v1.schema.json` | (no scoring or bonus fields) | Add required `scoringMode` (`hardcore-17000` \| `sequence-7000`), `rowRises`, and `boardClears`. This makes `score == rowRises*levelBonus + boardClears*70000 + chainPoints` a per-game machine-checkable invariant and retires the whole class of defects in Findings C1–C3 and H5. |
| D17 | `artifacts/protocols/optimistic-phase-ntuple/protocol.json:101` | "corrected D4 reproduces frozen means 176925.25 score and 116.375 moves" | **Do not edit the frozen artifact.** Register a superseding protocol whose corresponding clause reads "corrected D4 reproduces frozen means 400,675.25 score and 116.375 moves on `0x3e9c0000…0x3e9c0007` (history.md:3337)", and record the v1 defect in the provenance note. |

---

## (e) Verdict

Assessed as an audit of the historical evidence ledger, not of any single
experiment.

- **Run validity of the ledger as a record:** `partial`. The arithmetic is
  sound — 205 of 205 reported results are internally consistent, with zero
  transcription errors and zero mode-ambiguous rows — but the record is
  incomplete in three ways that materially affect interpretation: missing
  scoring-mode labels on 81 of 119 archival results (C3), missing cohort and
  dispersion statistics for the headline reference number (H2), and missing seed
  ranges for nine complete-game experiments plus the 477-root deployment panel
  (M2).

- **Scientific outcome of the audit:** `fail` on claim integrity, `pass` on
  computational integrity. Two preregistered rejections (`fair-depth4-s7`,
  `fair-cem-optimizer`) do not survive correction to the mandated 17,000-point
  bonus and must be reopened as unresolved (C2). One frozen protocol contains an
  unsatisfiable gate (C1). One fitting selection was driven by a pre-observed
  game and reverses without it (H1).

- **Highest justified evidence tier for fair D4 as the reference policy:**
  `development` — and, strictly, *reusable/burned* development. The 8-game
  corrected figure is a rescoring replay of already-consumed development seeds;
  the 64-game corrected figure has no identified cohort and no dispersion
  statistics. Neither reaches `STANDARD` under `docs/benchmarks.md`, and
  `baselines-v1.json` is correct not to assign one.

- **Protected and final cohorts:** I found **no evidence anywhere in the ledger,
  artifacts, or sources that a `0x7d……` or `0xd7……` game seed was ever
  read.** `status.md:11–12` is supported. The declared bank boundaries are,
  however, narrower than the constants the sources already reserve, and must be
  widened before a registry is written.

- **The million-point claim:** unchanged and unsupported. The only recorded
  above-million means are the privileged future-aware oracle
  (history.md:735, 12 games, censored at 500 moves, **7,000-point scored**) and
  its 500-move single-seed run (history.md:568, 17k). No public-information
  policy has produced a mean above 400,675.25 in any scoring mode on any
  cohort. The 1,246,684-point single game (status.md:23, README.md:58) is
  unverifiable and unclassifiable and should be downgraded per D3.

### Audit inputs

`docs/research/history.md` (all 4,607 lines) · `docs/research/status.md` ·
`docs/research/experiment-index.md` · `docs/strategies.md` ·
`docs/methodology.md` · `docs/benchmarks.md` · `README.md` ·
`src/core/native/engine.hpp` · `src/core/native/ntuple.hpp` ·
`research/schemas/game-result-v1.schema.json` ·
`research/benchmarks/baselines-v1.json` · `research/seeds/README.md` ·
`artifacts/protocols/optimistic-phase-ntuple/protocol.json` · seed and
`kLevelBonus` constants across `approaches/`.

No cohort was opened, no gameplay was run, and no file other than this one was
created or modified.
