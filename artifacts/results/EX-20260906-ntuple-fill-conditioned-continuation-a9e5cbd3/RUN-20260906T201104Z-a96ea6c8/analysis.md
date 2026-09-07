# Analysis of RUN-20260906T201104Z-a96ea6c8

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
- PASS leaf-in-search-determinism: untrained tables, 4 games x (1, 4, 4 workers): identical true, illegal/incomplete-free true; mean 124834 over 80-move caps
- PASS leaf-in-d4-search-determinism: untrained tables, 2 games x (1, 2, 2 workers) over 40-move caps: identical true, illegal/incomplete-free true, more work than depth 3 on every game true; work 352025645 vs 12237891 at depth 3; 74 s
- PASS serial-training-determinism: two 12-game single-thread runs: fingerprints b5c5f9335b6b26dc / b5c5f9335b6b26dc, 435 updates, 3177 touched entries; fresh initial-board value 20.0000 (declared 20)
- PASS finite-and-profile: values finite true; 5000000000 entries = 20.00 GB frozen, 60.00 GB trainable; per state: features 45 ns, value 105 ns (untrained tables, hot); direct decision 6.3 us (14.8 simulations) [sink 551729919120400 2211999.8]

## Training arms (training-role validation block, 256 paired games)

| arm | layout | alpha | entries | moves | train mean | final margin d3s7 vs fair | best margin | best at moves | points | stop | 1-ply final |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| control | rows,cols,win23,win32,phase=all | 1 | 1,000,000,000 | 1,200,228,265 | 307,841 | 197,769 | 210,990 | 600,112,060 | 6 | plateau | -9,800 |
| hgt5 | rows,cols,win23,win32,phase=all,fill=hgt5 | 1 | 5,000,000,000 | 2,000,004,221 | 307,336 | 157,846 | 179,760 | 1,600,283,262 | 10 | plateau | 18,028 |
| occ5 | rows,cols,win23,win32,phase=all,fill=occ5 | 1 | 5,000,000,000 | 1,600,289,700 | 306,087 | 150,272 | 203,193 | 1,400,252,568 | 8 | plateau | -17,997 |

Selected fill candidate: arm occ5 (best margin 203,193 at 1,400,252,568 moves); control best margin 210,990 at 600,112,060 moves; training-signal check passed: False; rule: the fill arm (occ5 or hgt5) whose best validation point has the larger paired mean margin of ntuple-d3s7 over fair-d3s7 on the 256-game training-role block, ties to occ5; the control arm's best point is the control candidate

Validation curve of arm control:

| point | moves | ntuple-d3s7 | fair-d3s7 | paired delta | LB95 | W-L | 1-ply | touched entries | plateau (last / previous window) |
| ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: | --- |
| 1 | 200,037,439 | 490,933 | 312,767 | 178,167 | 137,938 | 172-84 | 294,995 | 56,868,429 |  |
| 2 | 400,073,347 | 489,114 | 312,767 | 176,348 | 135,424 | 166-90 | 300,748 | 69,834,683 |  |
| 3 | 600,112,060 | 523,757 | 312,767 | 210,990 | 165,418 | 176-80 | 305,466 | 77,712,858 |  |
| 4 | 800,150,452 | 484,146 | 312,767 | 171,380 | 133,017 | 162-94 | 302,770 | 83,407,539 |  |
| 5 | 1,000,189,400 | 472,869 | 312,767 | 160,102 | 126,793 | 166-90 | 302,340 | 87,863,373 |  |
| 6 | 1,200,228,265 | 510,536 | 312,767 | 197,769 | 158,765 | 174-82 | 302,966 | 91,509,124 | 176,417 / 188,502 STOP |

Validation curve of arm hgt5:

| point | moves | ntuple-d3s7 | fair-d3s7 | paired delta | LB95 | W-L | 1-ply | touched entries | plateau (last / previous window) |
| ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: | --- |
| 1 | 200,028,295 | 437,476 | 312,767 | 124,709 | 91,009 | 164-92 | 261,278 | 94,891,681 |  |
| 2 | 400,060,509 | 450,029 | 312,767 | 137,262 | 103,554 | 162-94 | 280,088 | 122,728,350 |  |
| 3 | 600,096,150 | 469,664 | 312,767 | 156,898 | 117,186 | 162-94 | 279,606 | 140,981,096 |  |
| 4 | 800,131,115 | 448,493 | 312,767 | 135,727 | 101,619 | 164-92 | 286,336 | 154,891,231 |  |
| 5 | 1,000,167,008 | 472,983 | 312,767 | 160,216 | 121,482 | 161-95 | 303,431 | 165,884,743 |  |
| 6 | 1,200,208,525 | 492,355 | 312,767 | 179,588 | 142,390 | 173-83 | 291,790 | 175,090,459 | 158,510 / 139,623 |
| 7 | 1,400,244,137 | 482,293 | 312,767 | 169,526 | 130,143 | 159-97 | 312,632 | 183,033,149 | 169,777 / 143,296 |
| 8 | 1,600,283,262 | 492,526 | 312,767 | 179,760 | 139,912 | 170-86 | 285,266 | 189,967,354 | 176,291 / 150,947 |
| 9 | 1,800,319,931 | 471,103 | 312,767 | 158,336 | 120,473 | 158-98 | 297,039 | 196,122,852 | 169,207 / 158,510 |
| 10 | 2,000,004,221 | 470,613 | 312,767 | 157,846 | 118,845 | 160-96 | 330,795 | 201,697,051 | 165,314 / 169,777 STOP |

Validation curve of arm occ5:

| point | moves | ntuple-d3s7 | fair-d3s7 | paired delta | LB95 | W-L | 1-ply | touched entries | plateau (last / previous window) |
| ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: | --- |
| 1 | 200,030,322 | 454,648 | 312,767 | 141,882 | 103,289 | 149-107 | 270,047 | 84,949,833 |  |
| 2 | 400,061,818 | 490,763 | 312,767 | 177,997 | 134,200 | 167-89 | 283,788 | 108,837,432 |  |
| 3 | 600,097,906 | 490,502 | 312,767 | 177,736 | 139,164 | 175-81 | 291,741 | 124,165,020 |  |
| 4 | 800,135,553 | 493,410 | 312,767 | 180,644 | 136,012 | 168-88 | 306,541 | 135,637,387 |  |
| 5 | 1,000,173,026 | 493,430 | 312,767 | 180,663 | 142,053 | 177-79 | 300,276 | 144,810,868 |  |
| 6 | 1,200,212,497 | 482,638 | 312,767 | 169,871 | 130,476 | 159-97 | 311,856 | 152,395,576 | 177,059 / 165,871 |
| 7 | 1,400,252,568 | 515,960 | 312,767 | 203,193 | 157,108 | 167-89 | 316,705 | 158,865,823 | 184,576 / 178,792 |
| 8 | 1,600,289,700 | 463,039 | 312,767 | 150,272 | 114,342 | 160-96 | 294,770 | 164,510,583 | 174,446 / 179,681 STOP |

## Held-out screen (512 paired games, one-shot)

| arm | mean | median | Q25 | max | moves | clears/move | reveals/move |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| prior-d3s7 | 485,455 | 358,256 | 230,374 | 3,329,202 | 140.38 | 2.1097 | 1.1941 |
| prior-1ply | 293,390 | 238,459 | 157,932 | 1,706,990 | 87.53 | 1.9310 | 1.0638 |
| fill-d3s7 | 506,494 | 388,587 | 229,450 | 2,751,523 | 146.33 | 2.1216 | 1.2010 |
| fill-1ply | 298,199 | 262,235 | 190,084 | 1,165,434 | 88.90 | 1.9392 | 1.0653 |
| control-d3s7 | 491,401 | 389,192 | 229,197 | 2,662,558 | 141.92 | 2.1128 | 1.1945 |
| zeroed-d3s7 | 514,897 | 370,697 | 229,884 | 3,088,440 | 148.50 | 2.1262 | 1.2060 |
| classmean-d3s7 | 486,386 | 358,256 | 230,374 | 3,329,202 | 140.64 | 2.1098 | 1.1940 |
| prior-d4s7 | 521,956 | 393,322 | 244,332 | 2,469,625 | 150.20 | 2.1274 | 1.2047 |
| fill-d4s7 | 499,324 | 391,662 | 242,349 | 2,934,902 | 144.08 | 2.1172 | 1.1964 |
| zeroed-d4s7 | 511,364 | 391,800 | 230,407 | 3,016,304 | 147.36 | 2.1239 | 1.2033 |
| fair-d3s7 | 329,895 | 268,113 | 176,683 | 1,540,436 | 96.85 | 2.0017 | 1.1140 |

| contrast | delta | LB95 boot | LB95 t | UB95 | W-T-L | halves | floor |
| --- | ---: | ---: | ---: | ---: | --- | --- | ---: |
| prior-d3s7-vs-fair-d3s7 | 155,561 | 123,422 | 123,162 | 187,991 | 329-0-183 | 175,956 / 135,166 | 32,343 |
| prior-1ply-vs-fair-d3s7 | -36,504 | -56,474 | -56,674 | -16,523 | 225-0-287 | -43,043 / -29,966 | 20,135 |
| prior-d4s7-vs-prior-d3s7 | 36,500 | -249 | -262 | 72,651 | 287-0-225 | -13,181 / 86,181 | 36,699 |
| prior-d4s7-vs-fair-d3s7 | 192,061 | 158,820 | 158,928 | 225,169 | 340-0-172 | 162,775 / 221,347 | 33,076 |
| fill-d3s7-vs-prior-d3s7 | 21,039 | -16,864 | -16,982 | 59,230 | 271-0-241 | 6,466 / 35,613 | 37,956 |
| fill-d3s7-vs-control-d3s7 | 15,093 | -22,431 | -22,479 | 52,817 | 265-2-245 | 21,802 / 8,385 | 37,507 |
| control-d3s7-vs-prior-d3s7 | 5,946 | -29,891 | -29,937 | 41,989 | 255-6-251 | -15,336 / 27,228 | 35,821 |
| zeroed-d3s7-vs-prior-d3s7 | 29,442 | 5,813 | 5,246 | 54,179 | 102-318-92 | 12,168 / 46,716 | 24,154 |
| classmean-d3s7-vs-prior-d3s7 | 931 | -530 | -869 | 2,954 | 4-506-2 | -690 / 2,551 | 1,797 |
| fill-d4s7-vs-prior-d4s7 | -22,631 | -57,779 | -58,306 | 12,806 | 236-0-276 | 37,523 / -82,786 | 35,613 |
| zeroed-d4s7-vs-prior-d4s7 | -10,592 | -32,045 | -32,160 | 11,133 | 93-320-99 | -16,117 / -5,066 | 21,531 |
| fill-d4s7-vs-fill-d3s7 | -7,170 | -45,566 | -45,471 | 31,331 | 263-0-249 | 17,876 / -32,217 | 38,234 |
| zeroed-d4s7-vs-zeroed-d3s7 | -3,534 | -43,880 | -43,900 | 36,834 | 270-0-242 | -41,466 / 34,399 | 40,296 |
| fill-d3s7-vs-fair-d3s7 | 176,600 | 143,625 | 143,267 | 210,258 | 325-0-187 | 182,422 / 170,778 | 33,276 |
| control-d3s7-vs-fair-d3s7 | 161,507 | 132,301 | 131,913 | 192,037 | 324-0-188 | 160,620 / 162,393 | 29,542 |
| zeroed-d3s7-vs-fair-d3s7 | 185,003 | 149,752 | 148,936 | 221,463 | 334-0-178 | 188,124 / 181,882 | 36,005 |
| classmean-d3s7-vs-fair-d3s7 | 156,491 | 124,334 | 124,023 | 188,987 | 329-0-183 | 175,266 / 137,717 | 32,413 |
| fill-1ply-vs-prior-1ply | 4,809 | -13,189 | -13,115 | 22,386 | 289-0-223 | 21,419 / -11,801 | 17,893 |
| fill-1ply-vs-fair-d3s7 | -31,695 | -50,766 | -51,006 | -12,547 | 249-0-263 | -21,624 / -41,767 | 19,278 |
| fill-d4s7-vs-fair-d3s7 | 169,430 | 140,100 | 139,843 | 199,458 | 339-0-173 | 200,298 / 138,561 | 29,535 |

Gate (fill-d3s7 vs prior-d3s7) passed: False

- PASS screen artifact: illegalDecisions 0 and incompleteDecisions 0 in every arm: 
- FAIL bootstrap 95% lower bound of fill-d3s7 minus prior-d3s7 > 0: -16863.925097656247
- FAIL Student-t 95% lower bound > 0: -16982.313672978897
- PASS paired mean delta > 0 in both halves: [6465.9375, 35612.62109375]
- FAIL fill-d3s7 Q25 >= prior-d3s7 Q25: [229449.5, 230374.0]

Fill-conditioned experiment readings (three-way verdicts: supported / refuted / inconclusive):

- conditioning (fill-d3s7 vs control-d3s7): inconclusive: delta 15,093, LB95 boot -22,431, LB95 t -22,479, UB95 52,817, W-T-L 265-2-245, halves 21,802 / 8,385, floor 37,507
- continuation (control-d3s7 vs prior-d3s7): inconclusive: delta 5,946, LB95 boot -29,891, LB95 t -29,937, UB95 41,989, W-T-L 255-6-251, halves -15,336 / 27,228, floor 35,821; four criteria passed: False
- zeroed edit (zeroed-d3s7 vs prior-d3s7): supported: delta 29,442, LB95 boot 5,813, LB95 t 5,246, UB95 54,179, W-T-L 102-318-92, halves 12,168 / 46,716, floor 24,154
- class-mean edit (classmean-d3s7 vs prior-d3s7): inconclusive: delta 931, LB95 boot -530, LB95 t -869, UB95 2,954, W-T-L 4-506-2, halves -690 / 2,551, floor 1,797
- fill at depth 4 (fill-d4s7 vs prior-d4s7): inconclusive: delta -22,631, LB95 boot -57,779, LB95 t -58,306, UB95 12,806, W-T-L 236-0-276, halves 37,523 / -82,786, floor 35,613
- zeroed at depth 4 (zeroed-d4s7 vs prior-d4s7): inconclusive: delta -10,592, LB95 boot -32,045, LB95 t -32,160, UB95 11,133, W-T-L 93-320-99, halves -16,117 / -5,066, floor 21,531
- depthSteps fill-d4s7-vs-fill-d3s7: inconclusive: delta -7,170, LB95 boot -45,566, UB95 31,331, W-T-L 263-0-249
- depthSteps prior-d4s7-vs-prior-d3s7: inconclusive: delta 36,500, LB95 boot -249, UB95 72,651, W-T-L 287-0-225
- depthSteps zeroed-d4s7-vs-zeroed-d3s7: inconclusive: delta -3,534, LB95 boot -43,880, UB95 36,834, W-T-L 270-0-242
- direct fill-1ply-vs-prior-1ply: inconclusive: delta 4,809, LB95 boot -13,189, UB95 22,386, W-T-L 289-0-223
- direct fill-1ply-vs-fair-d3s7: refuted: delta -31,695, LB95 boot -50,766, UB95 -12,547, W-T-L 249-0-263
- direct prior-1ply-vs-fair-d3s7: refuted: delta -36,504, LB95 boot -56,474, UB95 -16,523, W-T-L 225-0-287
Theory falsifiers: {"primaryFalsifierUpperBoundBelowZero": false, "conditioningUpperBoundBelowZero": false, "gainIsContinuationNotConditioning": false, "optimismLegRefuted": false, "depthCompounding": false}

Replication (prior-d3s7 vs fair-d3s7, the first experiment's frozen tables on this fresh block) passed: True

- PASS screen artifact: illegalDecisions 0 and incompleteDecisions 0 in every arm: 
- PASS bootstrap 95% lower bound of prior-d3s7 minus fair-d3s7 > 0: 123421.78505859376
- PASS Student-t 95% lower bound > 0: 123162.21097680496
- PASS paired mean delta > 0 in both halves: [175955.66015625, 135165.58203125]
- PASS prior-d3s7 Q25 >= fair-d3s7 Q25: [230374.0, 176683.0]
