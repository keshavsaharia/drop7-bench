# Analysis of RUN-20260907T012358Z-a00ecdf0

## CHECK gates

All passed: True

- PASS codec-vs-horner: 2000000 random line words, 0 mismatches; max index 9999999
- PASS row-words-vs-accessor: 553 states, 0 mismatches
- PASS features-vs-reference[rows]: 553 states x 5 phases: 0 mismatches, 0 duplicate/short sets, 0 out of range; 70000000 entries
- PASS features-vs-reference[cols]: 553 states x 5 phases: 0 mismatches, 0 duplicate/short sets, 0 out of range; 70000000 entries
- PASS features-vs-reference[win23]: 553 states x 5 phases: 0 mismatches, 0 duplicate/short sets, 0 out of range; 30000000 entries
- PASS features-vs-reference[win32]: 553 states x 5 phases: 0 mismatches, 0 duplicate/short sets, 0 out of range; 30000000 entries
- PASS features-vs-reference[win24]: 553 states x 5 phases: 0 mismatches, 0 duplicate/short sets, 0 out of range; 2400000000 entries
- PASS features-vs-reference[win42]: 553 states x 5 phases: 0 mismatches, 0 duplicate/short sets, 0 out of range; 2400000000 entries
- PASS features-vs-reference[rows,cols,win23,win32,phase=none]: 553 states x 5 phases: 0 mismatches, 0 duplicate/short sets, 0 out of range; 200000000 entries
- PASS features-vs-reference[rows,cols,win23,win32,phase=cols]: 553 states x 5 phases: 0 mismatches, 0 duplicate/short sets, 0 out of range; 480000000 entries
- PASS features-vs-reference[rows,cols,win23,win32,phase=all]: 553 states x 5 phases: 0 mismatches, 0 duplicate/short sets, 0 out of range; 1000000000 entries
- PASS features-vs-reference[rows,cols,win23,win32,win24,win42,phase=all]: 553 states x 5 phases: 0 mismatches, 0 duplicate/short sets, 0 out of range; 5800000000 entries
- PASS features-vs-reference[cols,fill=occ5]: 553 states x 5 phases: 0 mismatches, 0 duplicate/short sets, 0 out of range; 350000000 entries
- PASS features-vs-reference[rows,win23,phase=none,fill=hgt5]: 553 states x 5 phases: 0 mismatches, 0 duplicate/short sets, 0 out of range; 500000000 entries
- PASS features-vs-reference[rows,cols,win23,win32,phase=all,fill=occ5]: 553 states x 5 phases: 0 mismatches, 0 duplicate/short sets, 0 out of range; 5000000000 entries
- PASS features-vs-reference[rows,cols,win23,win32,phase=all,fill=hgt5]: 553 states x 5 phases: 0 mismatches, 0 duplicate/short sets, 0 out of range; 5000000000 entries
- PASS fill-bucket-vs-reference[occ5]: 553 states: 0 mismatches, 0 mirror mismatches; states per bucket [81, 107, 110, 147, 108] (all reached true)
- PASS fill-bucket-vs-reference[hgt5]: 553 states: 0 mismatches, 0 mirror mismatches; states per bucket [127, 92, 134, 110, 90] (all reached true)
- PASS promotion-preserves-value[occ5]: 553 states x 5 phases: 0 value mismatches after promotion; entries x5 true; fresh accumulators true; buckets separate under one update true
- PASS promotion-preserves-value[hgt5]: 553 states x 5 phases: 0 value mismatches after promotion; entries x5 true; fresh accumulators true; buckets separate under one update true
- PASS information-boundary: 553 states x 4 hidden-field perturbations: 0 leaf-value changes, 0 decision changes
- PASS reflection: 553 states: 0 value mismatches; 534 live states, 0 decision mismatches under mirroring
- PASS direct-policy-legality: 553 states (19 terminal), 0 illegal or missing decisions
- PASS leaf-in-search-determinism: frozen tables, 4 games x (1, 4, 4 workers): identical true, illegal/incomplete-free true; mean 252067 over 80-move caps
- PASS leaf-in-d4-search-determinism: frozen tables, 2 games x (1, 2, 2 workers) over 40-move caps: identical true, illegal/incomplete-free true, more work than depth 3 on every game true; work 422072121 vs 14437927 at depth 3; 83 s
- PASS train-search-vs-engine: frozen tables, 120 live states at depth 3: column sets 0, values (bit-for-bit) 0, column_values actions 0, choose_action decisions 0, repeat 0 mismatches; internal targets mean 2226 per decision, max 2450, malformed 0; 10 s
- PASS serial-training-determinism: two 12-game single-thread runs: fingerprints b5c5f9335b6b26dc / b5c5f9335b6b26dc, 435 updates, 3177 touched entries; fresh initial-board value 20.0000 (declared 20)
- PASS finite-and-profile: values finite true; 1000000000 entries = 4.00 GB frozen, 12.00 GB trainable; per state: features 42 ns, value 86 ns (untrained tables, hot); direct decision 5.4 us (14.8 simulations) [sink 110341919120400 2211999.8]

## Training arms (training-role validation block, 256 paired games)

| arm | layout | alpha | entries | moves | train mean | final margin d3s7 vs fair | best margin | best at moves | points | stop | 1-ply final |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| searchtd | rows,cols,win23,win32,phase=all | 1 | 1,000,000,000 | 1,812,910 | 442,517 | 112,357 | 140,529 | 907,918 | 7 | plateau | -110,412 |
| treestrap | rows,cols,win23,win32,phase=all | 1 | 1,000,000,000 | 3,126,439 | 384,492 | 75,708 | 93,767 | 2,108,358 | 11 | wall | -107,245 |

Candidate: arm treestrap (best margin 93,767 at 2,108,358 moves); ablation arm searchtd best margin 140,529 at 907,918 moves; warm-start margins (point 0) {"searchtd": 174466.23046875, "treestrap": 174466.23046875}; training-signal check passed: False; rule: the candidate is the treestrap arm's best validation point by protocol (no selection between arms); the searchtd arm's best point is the ablation; each arm's point 0 is the warm start's own margin on the same block and is never a candidate

Validation curve of arm searchtd:

| point | moves | ntuple-d3s7 | fair-d3s7 | paired delta | LB95 | W-L | 1-ply | touched entries | plateau (last / previous window) |
| ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: | --- |
| 1 | 0 | 492,151 | 317,684 | 174,466 | 132,378 | 170-86 | 284,964 | n/a |  |
| 2 | 322,336 | 419,604 | 317,684 | 101,920 | 68,384 | 153-103 | 201,777 | 2,236,173 |  |
| 3 | 601,818 | 413,660 | 317,684 | 95,976 | 60,710 | 164-92 | 195,731 | 3,336,265 |  |
| 4 | 907,918 | 458,214 | 317,684 | 140,529 | 104,857 | 166-90 | 215,810 | 4,315,950 |  |
| 5 | 1,216,725 | 416,948 | 317,684 | 99,263 | 62,718 | 157-99 | 212,896 | 5,159,688 |  |
| 6 | 1,502,032 | 389,737 | 317,684 | 72,053 | 40,941 | 154-102 | 212,356 | 5,859,483 |  |
| 7 | 1,812,910 | 430,042 | 317,684 | 112,357 | 76,627 | 153-103 | 207,272 | 6,554,101 | 94,558 / 112,808 STOP |

Validation curve of arm treestrap:

| point | moves | ntuple-d3s7 | fair-d3s7 | paired delta | LB95 | W-L | 1-ply | touched entries | plateau (last / previous window) |
| ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: | --- |
| 1 | 0 | 492,151 | 317,684 | 174,466 | 132,378 | 170-86 | 284,964 | n/a |  |
| 2 | 306,312 | 327,744 | 317,684 | 10,059 | -19,599 | 135-121 | 182,700 | 37,912,852 |  |
| 3 | 600,263 | 382,938 | 317,684 | 65,254 | 33,087 | 152-104 | 197,662 | 52,260,783 |  |
| 4 | 921,421 | 340,382 | 317,684 | 22,698 | -7,758 | 139-117 | 192,343 | 62,557,249 |  |
| 5 | 1,217,550 | 352,777 | 317,684 | 35,092 | 5,089 | 146-110 | 197,532 | 69,881,519 |  |
| 6 | 1,515,106 | 367,301 | 317,684 | 49,617 | 16,801 | 140-116 | 196,368 | 75,789,370 |  |
| 7 | 1,812,847 | 346,749 | 317,684 | 29,065 | 2,548 | 147-109 | 204,989 | 80,663,348 | 37,925 / 32,670 |
| 8 | 2,108,358 | 411,452 | 317,684 | 93,767 | 58,987 | 154-102 | 212,020 | 84,769,943 | 57,483 / 41,015 |
| 9 | 2,405,105 | 365,367 | 317,684 | 47,683 | 14,529 | 145-111 | 216,394 | 88,476,469 | 56,838 / 35,802 |
| 10 | 2,701,947 | 375,358 | 317,684 | 57,674 | 25,154 | 149-107 | 209,233 | 91,797,749 | 66,375 / 37,925 |
| 11 | 3,002,242 | 393,392 | 317,684 | 75,708 | 42,554 | 147-109 | 210,440 | 94,854,652 | 60,355 / 57,483 |

## Throughput smoke run (probe block, tables discarded)

Layout rows,cols,win23,win32,phase=all, 1,000,000,000 entries, moves 64,059, wall 112 s, mean 588 moves/s, last chunk train mean 294,654 / 87.9 moves

## Held-out screen (512 paired games, one-shot)

| arm | mean | median | Q25 | max | moves | clears/move | reveals/move |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| prior-d3s7 | 484,342 | 376,502 | 215,414 | 2,172,909 | 140.16 | 2.1109 | 1.1951 |
| prior-1ply | 292,614 | 249,004 | 172,892 | 1,143,379 | 87.42 | 1.9261 | 1.0615 |
| treestrap-d3s7 | 375,372 | 301,128 | 192,729 | 1,826,954 | 109.85 | 2.0361 | 1.1411 |
| treestrap-1ply | 212,834 | 182,473 | 139,614 | 1,067,474 | 64.91 | 1.7898 | 0.9673 |
| searchtd-d3s7 | 411,191 | 303,296 | 196,307 | 2,032,455 | 119.84 | 2.0662 | 1.1605 |
| control-d3s7 | 466,475 | 372,346 | 228,490 | 2,392,977 | 135.13 | 2.1002 | 1.1869 |
| prior-d4s7 | 532,273 | 419,196 | 244,637 | 3,266,088 | 153.16 | 2.1348 | 1.2110 |
| treestrap-d4s7 | 431,020 | 336,510 | 194,989 | 2,584,387 | 125.30 | 2.0842 | 1.1749 |
| searchtd-d4s7 | 454,730 | 334,314 | 214,485 | 2,698,321 | 132.08 | 2.0960 | 1.1805 |
| fair-d3s7 | 320,843 | 264,820 | 174,268 | 1,464,784 | 94.22 | 1.9888 | 1.1041 |

| contrast | delta | LB95 boot | LB95 t | UB95 | W-T-L | halves | floor |
| --- | ---: | ---: | ---: | ---: | --- | --- | ---: |
| prior-d3s7-vs-fair-d3s7 | 163,499 | 133,843 | 133,453 | 193,865 | 322-0-190 | 198,133 / 128,864 | 29,994 |
| prior-1ply-vs-fair-d3s7 | -28,230 | -46,834 | -47,108 | -9,596 | 226-0-286 | -20,328 / -36,131 | 18,846 |
| prior-d4s7-vs-prior-d3s7 | 47,932 | 13,648 | 12,994 | 81,845 | 289-0-223 | 33,480 / 62,384 | 34,878 |
| prior-d4s7-vs-fair-d3s7 | 211,430 | 180,063 | 179,472 | 243,552 | 343-0-169 | 231,613 / 191,247 | 31,903 |
| control-d3s7-vs-prior-d3s7 | -17,867 | -51,858 | -51,518 | 15,511 | 245-5-262 | -38,333 / 2,599 | 33,593 |
| control-d3s7-vs-fair-d3s7 | 145,632 | 118,182 | 117,921 | 173,192 | 329-0-183 | 159,800 / 131,463 | 27,663 |
| treestrap-d3s7-vs-prior-d3s7 | -108,970 | -139,543 | -139,254 | -78,715 | 206-0-306 | -126,504 / -91,436 | 30,232 |
| treestrap-d3s7-vs-searchtd-d3s7 | -35,819 | -64,276 | -64,106 | -7,292 | 242-0-270 | -36,530 / -35,109 | 28,237 |
| searchtd-d3s7-vs-prior-d3s7 | -73,150 | -105,383 | -105,578 | -41,675 | 224-0-288 | -89,974 / -56,327 | 32,371 |
| treestrap-d3s7-vs-control-d3s7 | -91,103 | -120,954 | -120,979 | -60,596 | 223-0-289 | -88,171 / -94,035 | 29,825 |
| treestrap-d4s7-vs-prior-d4s7 | -101,253 | -135,587 | -136,261 | -66,683 | 208-0-304 | -117,855 / -84,652 | 34,947 |
| searchtd-d4s7-vs-prior-d4s7 | -77,543 | -113,433 | -113,187 | -42,565 | 215-0-297 | -102,221 / -52,865 | 35,582 |
| treestrap-d4s7-vs-treestrap-d3s7 | 55,648 | 27,132 | 27,074 | 84,408 | 287-0-225 | 42,129 / 69,167 | 28,525 |
| searchtd-d4s7-vs-searchtd-d3s7 | 43,539 | 12,093 | 11,408 | 75,317 | 272-0-240 | 21,233 / 65,845 | 32,076 |
| treestrap-d3s7-vs-fair-d3s7 | 54,529 | 31,785 | 31,548 | 77,846 | 283-0-229 | 71,629 / 37,428 | 22,941 |
| searchtd-d3s7-vs-fair-d3s7 | 90,348 | 64,778 | 64,275 | 116,130 | 307-0-205 | 108,159 / 72,537 | 26,028 |
| treestrap-d4s7-vs-fair-d3s7 | 110,177 | 84,707 | 84,088 | 136,774 | 310-0-202 | 113,758 / 106,595 | 26,044 |
| treestrap-1ply-vs-prior-1ply | -79,779 | -93,803 | -93,697 | -66,238 | 168-0-344 | -82,968 / -76,590 | 13,894 |
| treestrap-1ply-vs-fair-d3s7 | -108,009 | -124,633 | -124,654 | -91,790 | 157-0-355 | -103,295 / -112,722 | 16,616 |

Gate (treestrap-d3s7 vs prior-d3s7) passed: False

- PASS screen artifact: illegalDecisions 0 and incompleteDecisions 0 in every arm: 
- FAIL bootstrap 95% lower bound of treestrap-d3s7 minus prior-d3s7 > 0: -139543.174609375
- FAIL Student-t 95% lower bound > 0: -139253.97993843877
- FAIL paired mean delta > 0 in both halves: [-126504.26171875, -91435.6953125]
- FAIL treestrap-d3s7 Q25 >= prior-d3s7 Q25: [192728.75, 215414.25]

Search-target experiment readings (three-way verdicts: supported / refuted / inconclusive):

- off-path boards (treestrap-d3s7 vs searchtd-d3s7): refuted: delta -35,819, LB95 boot -64,276, LB95 t -64,106, UB95 -7,292, W-T-L 242-0-270, halves -36,530 / -35,109, floor 28,237
- search targets at visited states only (searchtd-d3s7 vs prior-d3s7): refuted: delta -73,150, LB95 boot -105,383, LB95 t -105,578, UB95 -41,675, W-T-L 224-0-288, halves -89,974 / -56,327, floor 32,371; four criteria passed: False
- vs the one-ply continuation (treestrap-d3s7 vs control-d3s7): refuted: delta -91,103, LB95 boot -120,954, LB95 t -120,979, UB95 -60,596, W-T-L 223-0-289, halves -88,171 / -94,035, floor 29,825
- treestrap at depth 4 (treestrap-d4s7 vs prior-d4s7): refuted: delta -101,253, LB95 boot -135,587, LB95 t -136,261, UB95 -66,683, W-T-L 208-0-304, halves -117,855 / -84,652, floor 34,947
- searchtd at depth 4 (searchtd-d4s7 vs prior-d4s7): refuted: delta -77,543, LB95 boot -113,433, LB95 t -113,187, UB95 -42,565, W-T-L 215-0-297, halves -102,221 / -52,865, floor 35,582
- depthSteps treestrap-d4s7-vs-treestrap-d3s7: supported: delta 55,648, LB95 boot 27,132, UB95 84,408, W-T-L 287-0-225
- depthSteps searchtd-d4s7-vs-searchtd-d3s7: supported: delta 43,539, LB95 boot 12,093, UB95 75,317, W-T-L 272-0-240
- depthSteps prior-d4s7-vs-prior-d3s7: supported: delta 47,932, LB95 boot 13,648, UB95 81,845, W-T-L 289-0-223
- direct treestrap-1ply-vs-prior-1ply: refuted: delta -79,779, LB95 boot -93,803, UB95 -66,238, W-T-L 168-0-344
- direct treestrap-1ply-vs-fair-d3s7: refuted: delta -108,009, LB95 boot -124,633, UB95 -91,790, W-T-L 157-0-355
- direct prior-1ply-vs-fair-d3s7: refuted: delta -28,230, LB95 boot -46,834, UB95 -9,596, W-T-L 226-0-286
Theory falsifiers: {"primaryFalsifierUpperBoundBelowZero": true, "offPathUpperBoundBelowZero": true, "gainIsSearchTargetNotOffPath": false, "depthCompounding": true}

Replication (prior-d3s7 vs fair-d3s7, the first experiment's frozen tables on this fresh block) passed: True

- PASS screen artifact: illegalDecisions 0 and incompleteDecisions 0 in every arm: 
- PASS bootstrap 95% lower bound of prior-d3s7 minus fair-d3s7 > 0: 133842.91982421876
- PASS Student-t 95% lower bound > 0: 133453.0650176403
- PASS paired mean delta > 0 in both halves: [198133.28125, 128863.90234375]
- PASS prior-d3s7 Q25 >= fair-d3s7 Q25: [215414.25, 174267.75]
