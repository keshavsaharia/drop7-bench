# Analysis of RUN-20260907T041853Z-70c90dc9

## Training arms (training-role validation block, 256 paired games)

| arm | layout | alpha | entries | moves | train mean | final margin d3s7 vs fair | best margin | best at moves | points | stop | 1-ply final |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| searchtd05 | rows,cols,win23,win32,phase=all | 0.05 | 1,000,000,000 | 1,821,204 | 394,643 | 40,720 | 83,875 | 1,521,557 | 7 | plateau | -104,734 |
| treestrap05 | rows,cols,win23,win32,phase=all | 0.05 | 1,000,000,000 | 2,120,050 | 474,111 | 137,068 | 166,414 | 1,802,815 | 8 | plateau | -58,786 |
| treestrap20 | rows,cols,win23,win32,phase=all | 0.2 | 1,000,000,000 | 1,807,768 | 475,738 | 132,604 | 168,523 | 900,377 | 7 | plateau | -57,137 |

Candidate: arm treestrap20 (best margin 168,523 at 900,377 moves); ablation arm searchtd05 best margin 83,875 at 1,521,557 moves; warm-start margins (point 0) {"treestrap05": 188854.44140625, "treestrap20": 188854.44140625, "searchtd05": 188854.44140625}; training-signal check passed: False; rule: the TreeStrap arm (treestrap05 or treestrap20) whose best validation point has the larger paired mean margin of ntuple-d3s7 over fair-d3s7 on the 256-game training-role block is the candidate, ties to treestrap05; the other is screened at depth 3 as treestrapalt; searchtd05 is the ablation; each arm's point 0 is the warm start's own margin and is never a candidate

Validation curve of arm searchtd05:

| point | moves | ntuple-d3s7 | fair-d3s7 | paired delta | LB95 | W-L | 1-ply | touched entries | plateau (last / previous window) |
| ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: | --- |
| 1 | 0 | 506,782 | 317,928 | 188,854 | 147,691 | 172-84 | 283,902 | n/a |  |
| 2 | 318,689 | 383,577 | 317,928 | 65,650 | 34,083 | 141-115 | 191,489 | 2,096,850 |  |
| 3 | 616,338 | 392,271 | 317,928 | 74,343 | 42,018 | 147-109 | 193,990 | 3,115,580 |  |
| 4 | 916,453 | 358,481 | 317,928 | 40,553 | 10,685 | 134-122 | 196,199 | 3,929,959 |  |
| 5 | 1,217,197 | 364,498 | 317,928 | 46,570 | 15,664 | 142-114 | 207,280 | 4,646,468 |  |
| 6 | 1,521,557 | 401,803 | 317,928 | 83,875 | 50,158 | 157-99 | 213,023 | 5,288,775 |  |
| 7 | 1,821,204 | 358,648 | 317,928 | 40,720 | 10,573 | 140-116 | 213,194 | 5,875,876 | 57,055 / 60,182 STOP |

Validation curve of arm treestrap05:

| point | moves | ntuple-d3s7 | fair-d3s7 | paired delta | LB95 | W-L | 1-ply | touched entries | plateau (last / previous window) |
| ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: | --- |
| 1 | 0 | 506,782 | 317,928 | 188,854 | 147,691 | 172-84 | 283,902 | n/a |  |
| 2 | 310,563 | 464,090 | 317,928 | 146,163 | 108,728 | 166-90 | 239,466 | 36,866,510 |  |
| 3 | 601,865 | 466,113 | 317,928 | 148,186 | 111,778 | 172-84 | 252,110 | 48,852,491 |  |
| 4 | 918,864 | 472,032 | 317,928 | 154,105 | 114,027 | 159-97 | 246,025 | 57,548,097 |  |
| 5 | 1,211,102 | 466,411 | 317,928 | 148,483 | 112,124 | 169-87 | 267,825 | 63,551,772 |  |
| 6 | 1,506,197 | 455,812 | 317,928 | 137,885 | 101,915 | 171-85 | 253,098 | 68,556,367 |  |
| 7 | 1,802,815 | 484,342 | 317,928 | 166,414 | 123,918 | 158-98 | 249,108 | 72,719,513 | 150,927 / 149,484 |
| 8 | 2,120,050 | 454,995 | 317,928 | 137,068 | 95,199 | 159-97 | 259,142 | 76,532,655 | 147,122 / 150,258 STOP |

Validation curve of arm treestrap20:

| point | moves | ntuple-d3s7 | fair-d3s7 | paired delta | LB95 | W-L | 1-ply | touched entries | plateau (last / previous window) |
| ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: | --- |
| 1 | 0 | 506,782 | 317,928 | 188,854 | 147,691 | 172-84 | 283,902 | n/a |  |
| 2 | 313,890 | 461,890 | 317,928 | 143,962 | 104,114 | 156-100 | 246,381 | 37,905,975 |  |
| 3 | 609,024 | 440,442 | 317,928 | 122,515 | 85,079 | 152-104 | 270,042 | 50,000,592 |  |
| 4 | 900,377 | 486,450 | 317,928 | 168,523 | 125,800 | 166-90 | 266,441 | 58,066,182 |  |
| 5 | 1,218,351 | 437,735 | 317,928 | 119,807 | 83,292 | 152-104 | 260,876 | 64,691,442 |  |
| 6 | 1,510,853 | 478,511 | 317,928 | 160,583 | 119,176 | 165-91 | 278,818 | 69,578,902 |  |
| 7 | 1,807,768 | 450,532 | 317,928 | 132,604 | 90,165 | 158-98 | 260,791 | 73,755,622 | 137,665 / 145,000 STOP |

## Held-out screen (2048 paired games, one-shot)

| arm | mean | median | Q25 | max | moves | clears/move | reveals/move |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| prior-d3s7 | 489,321 | 375,632 | 226,353 | 2,777,773 | 141.46 | 2.1110 | 1.1940 |
| prior-1ply | 293,770 | 247,815 | 173,712 | 1,253,450 | 87.74 | 1.9319 | 1.0652 |
| treestrap-d3s7 | 467,899 | 355,830 | 215,806 | 2,627,197 | 135.61 | 2.1065 | 1.1905 |
| treestrap-1ply | 262,406 | 227,100 | 157,349 | 1,179,524 | 78.75 | 1.8903 | 1.0371 |
| treestrapalt-d3s7 | 485,343 | 373,428 | 216,952 | 3,160,928 | 140.32 | 2.1149 | 1.1962 |
| searchtd-d3s7 | 382,505 | 315,728 | 209,268 | 2,389,633 | 111.99 | 2.0451 | 1.1434 |
| control-d3s7 | 493,481 | 377,567 | 229,388 | 3,400,586 | 142.65 | 2.1130 | 1.1955 |
| prior-d4s7 | 535,153 | 404,587 | 231,010 | 2,965,639 | 153.99 | 2.1368 | 1.2134 |
| treestrap-d4s7 | 509,810 | 406,638 | 247,844 | 2,263,528 | 147.11 | 2.1282 | 1.2055 |
| fair-d3s7 | 325,904 | 263,182 | 175,461 | 2,329,707 | 95.65 | 1.9961 | 1.1101 |

| contrast | delta | LB95 boot | LB95 t | UB95 | W-T-L | halves | floor |
| --- | ---: | ---: | ---: | ---: | --- | --- | ---: |
| prior-d3s7-vs-fair-d3s7 | 163,417 | 148,769 | 148,591 | 178,125 | 1345-0-703 | 160,869 / 165,966 | 14,821 |
| prior-1ply-vs-fair-d3s7 | -32,134 | -41,613 | -41,450 | -22,871 | 946-0-1102 | -36,927 / -27,341 | 9,313 |
| prior-d4s7-vs-prior-d3s7 | 14,325 | -27,057 | -27,031 | 54,764 | 273-0-239 | 25,071 / 3,579 | 41,285 |
| prior-d4s7-vs-fair-d3s7 | 204,007 | 170,409 | 169,832 | 238,129 | 343-0-169 | 212,331 / 195,684 | 34,116 |
| control-d3s7-vs-prior-d3s7 | 4,160 | -13,667 | -13,764 | 21,862 | 1064-15-969 | -3,916 / 12,236 | 17,918 |
| control-d3s7-vs-fair-d3s7 | 167,577 | 152,675 | 152,743 | 182,553 | 1354-0-694 | 156,952 / 178,202 | 14,829 |
| treestrap-d3s7-vs-prior-d3s7 | -21,422 | -38,785 | -38,718 | -4,294 | 1005-0-1043 | -24,311 / -18,534 | 17,290 |
| treestrap-d3s7-vs-searchtd-d3s7 | 85,394 | 70,489 | 70,433 | 100,318 | 1169-0-879 | 82,519 / 88,268 | 14,955 |
| searchtd-d3s7-vs-prior-d3s7 | -106,816 | -122,276 | -122,162 | -91,479 | 851-0-1197 | -106,829 / -106,802 | 15,341 |
| treestrap-d3s7-vs-control-d3s7 | -25,582 | -43,488 | -43,299 | -7,786 | 975-0-1073 | -20,394 / -30,770 | 17,711 |
| treestrap-d4s7-vs-prior-d4s7 | -25,344 | -64,719 | -64,903 | 14,253 | 253-0-259 | 8,011 / -58,698 | 39,492 |
| treestrap-d4s7-vs-treestrap-d3s7 | 39,537 | 4,671 | 3,949 | 75,253 | 277-0-235 | 89,460 / -10,385 | 35,527 |
| treestrap-d3s7-vs-fair-d3s7 | 141,995 | 127,552 | 127,595 | 156,337 | 1282-0-766 | 136,558 / 147,432 | 14,395 |
| searchtd-d3s7-vs-fair-d3s7 | 56,601 | 45,299 | 45,155 | 67,944 | 1180-0-868 | 54,039 / 59,164 | 11,443 |
| treestrap-d4s7-vs-fair-d3s7 | 178,664 | 146,782 | 146,426 | 211,433 | 341-0-171 | 220,342 / 136,986 | 32,182 |
| treestrap-1ply-vs-prior-1ply | -31,364 | -38,943 | -39,003 | -23,819 | 919-0-1129 | -32,074 / -30,653 | 7,636 |
| treestrap-1ply-vs-fair-d3s7 | -63,497 | -72,661 | -72,708 | -54,464 | 832-0-1216 | -69,001 / -57,994 | 9,208 |
| treestrapalt-d3s7-vs-prior-d3s7 | -3,978 | -22,126 | -21,894 | 13,794 | 1019-0-1029 | -1,805 / -6,152 | 17,909 |
| treestrap-d3s7-vs-treestrapalt-d3s7 | -17,444 | -35,160 | -34,977 | 56 | 1014-0-1034 | -22,506 / -12,382 | 17,527 |
| treestrapalt-d3s7-vs-fair-d3s7 | 159,439 | 144,462 | 144,510 | 174,591 | 1300-0-748 | 159,064 / 159,814 | 14,923 |

Gate (treestrap-d3s7 vs prior-d3s7) passed: False

- PASS screen artifact: illegalDecisions 0 and incompleteDecisions 0 in every arm: 
- FAIL bootstrap 95% lower bound of treestrap-d3s7 minus prior-d3s7 > 0: -38784.96091308594
- FAIL Student-t 95% lower bound > 0: -38718.345861692054
- FAIL paired mean delta > 0 in both halves: [-24310.58203125, -18533.8076171875]
- FAIL treestrap-d3s7 Q25 >= prior-d3s7 Q25: [215805.5, 226352.75]

Search-target experiment readings (three-way verdicts: supported / refuted / inconclusive):

- off-path boards (treestrap-d3s7 vs searchtd-d3s7): supported: delta 85,394, LB95 boot 70,489, LB95 t 70,433, UB95 100,318, W-T-L 1169-0-879, halves 82,519 / 88,268, floor 14,955
- search targets at visited states only (searchtd-d3s7 vs prior-d3s7): refuted: delta -106,816, LB95 boot -122,276, LB95 t -122,162, UB95 -91,479, W-T-L 851-0-1197, halves -106,829 / -106,802, floor 15,341; four criteria passed: False
- vs the one-ply continuation (treestrap-d3s7 vs control-d3s7): refuted: delta -25,582, LB95 boot -43,488, LB95 t -43,299, UB95 -7,786, W-T-L 975-0-1073, halves -20,394 / -30,770, floor 17,711
- the other TreeStrap step size (treestrapalt-d3s7 vs prior-d3s7): inconclusive: delta -3,978, LB95 boot -22,126, LB95 t -21,894, UB95 13,794, W-T-L 1019-0-1029, halves -1,805 / -6,152, floor 17,909
- candidate vs the other step size (treestrap-d3s7 vs treestrapalt-d3s7): inconclusive: delta -17,444, LB95 boot -35,160, LB95 t -34,977, UB95 56, W-T-L 1014-0-1034, halves -22,506 / -12,382, floor 17,527
- treestrap at depth 4 (treestrap-d4s7 vs prior-d4s7): inconclusive: delta -25,344, LB95 boot -64,719, LB95 t -64,903, UB95 14,253, W-T-L 253-0-259, halves 8,011 / -58,698, floor 39,492
- depthSteps treestrap-d4s7-vs-treestrap-d3s7: supported: delta 39,537, LB95 boot 4,671, UB95 75,253, W-T-L 277-0-235
- depthSteps prior-d4s7-vs-prior-d3s7: inconclusive: delta 14,325, LB95 boot -27,057, UB95 54,764, W-T-L 273-0-239
- direct treestrap-1ply-vs-prior-1ply: refuted: delta -31,364, LB95 boot -38,943, UB95 -23,819, W-T-L 919-0-1129
- direct treestrap-1ply-vs-fair-d3s7: refuted: delta -63,497, LB95 boot -72,661, UB95 -54,464, W-T-L 832-0-1216
- direct prior-1ply-vs-fair-d3s7: refuted: delta -32,134, LB95 boot -41,613, UB95 -22,871, W-T-L 946-0-1102
Theory falsifiers: {"primaryFalsifierUpperBoundBelowZero": true, "offPathUpperBoundBelowZero": false, "gainIsSearchTargetNotOffPath": false, "depthCompounding": false}

Replication (prior-d3s7 vs fair-d3s7, the first experiment's frozen tables on this fresh block) passed: True

- PASS screen artifact: illegalDecisions 0 and incompleteDecisions 0 in every arm: 
- PASS bootstrap 95% lower bound of prior-d3s7 minus fair-d3s7 > 0: 148768.58845214843
- PASS Student-t 95% lower bound > 0: 148590.5926487934
- PASS paired mean delta > 0 in both halves: [160868.51171875, 165965.884765625]
- PASS prior-d3s7 Q25 >= fair-d3s7 Q25: [226352.75, 175461.25]
