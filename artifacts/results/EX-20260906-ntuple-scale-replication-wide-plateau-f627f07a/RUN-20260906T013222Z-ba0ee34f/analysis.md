# Analysis of RUN-20260906T013222Z-ba0ee34f

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
- PASS information-boundary: 553 states x 4 hidden-field perturbations: 0 leaf-value changes, 0 decision changes
- PASS reflection: 553 states: 0 value mismatches; 534 live states, 0 decision mismatches under mirroring
- PASS direct-policy-legality: 553 states (19 terminal), 0 illegal or missing decisions
- PASS leaf-in-search-determinism: 4 games x (1, 4, 4 workers): identical true, illegal/incomplete-free true; mean 124834 over 80-move caps
- PASS serial-training-determinism: two 12-game single-thread runs: fingerprints b5c5f9335b6b26dc / b5c5f9335b6b26dc, 435 updates, 3177 touched entries; fresh initial-board value 20.0000 (declared 20)
- PASS finite-and-profile: values finite true; 5800000000 entries = 23.20 GB frozen, 69.60 GB trainable; per state: features 65 ns, value 149 ns (untrained tables, hot); direct decision 9.1 us (14.8 simulations) [sink 639171920617200 2212000.2]

## Throughput smoke run (probe block, tables discarded)

Layout rows,cols,win23,win32,win24,win42,phase=all, 5,800,000,000 entries, moves 40,002,639, wall 34 s, mean 1,240,481 moves/s, last chunk train mean 220,342 / 67.2 moves

## Main run

Moves 4,500,370,590, games 47,477,538, wall 4,358 s, mean 1,184,973 moves/s, done True, stop reason plateau

Validation line-up on the 256-game training-role block:

| point | moves | ntuple-d3s7 | fair-d3s7 | paired delta | LB95 | W-L | 1-ply | touched entries | plateau (last / previous window) |
| ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: | --- |
| 1 | 500,034,572 | 492,111 | 337,539 | 154,572 | 118,307 | 173-83 | 323,748 | 159,839,861 |  |
| 2 | 1,000,072,627 | 483,528 | 337,539 | 145,989 | 106,062 | 160-96 | 310,862 | 192,963,782 |  |
| 3 | 1,500,111,255 | 515,399 | 337,539 | 177,860 | 135,891 | 171-85 | 316,634 | 214,981,762 |  |
| 4 | 2,000,153,332 | 525,039 | 337,539 | 187,500 | 144,166 | 168-88 | 335,959 | 231,088,813 |  |
| 5 | 2,500,198,069 | 521,324 | 337,539 | 183,785 | 138,929 | 171-85 | 336,160 | 243,871,781 |  |
| 6 | 3,000,240,102 | 524,170 | 337,539 | 186,631 | 142,913 | 169-87 | 338,270 | 254,558,199 |  |
| 7 | 3,500,281,390 | 509,646 | 337,539 | 172,106 | 130,276 | 167-89 | 348,955 | 263,791,048 |  |
| 8 | 4,000,326,496 | 480,053 | 337,539 | 142,514 | 106,047 | 164-92 | 363,825 | 271,899,767 | 171,259 / 166,480 |
| 9 | 4,500,370,590 | 498,951 | 337,539 | 161,412 | 120,449 | 159-97 | 362,132 | 279,110,607 | 165,666 / 173,783 STOP |

Best validation point: {"moves": 2000153332, "artifact": "val-002000153332.json", "pairedDeltaD3": 187500.078125, "ntupleD3Mean": 525039.16015625, "fairD3Mean": 337539.08203125}
Any positive validation margin (theory training-signal check): True
Stop: {"reason": "plateau", "movesTotal": 4500370590, "gamesTotal": 47477538, "wallSeconds": 4462.4, "validationPoints": 9, "plateauWindow": 4, "recentWindowMean": 165665.8408203125, "previousWindowMean": 173783.3173828125, "bestMargin": 187500.078125}

## Held-out screen (512 paired games, one-shot)

| arm | mean | median | Q25 | max | moves | clears/move | reveals/move |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| candidate-d3s7 | 481,869 | 393,375 | 241,610 | 2,487,485 | 139.39 | 2.1088 | 1.1927 |
| candidate-1ply | 328,039 | 282,096 | 178,990 | 1,224,664 | 97.27 | 1.9762 | 1.0946 |
| prior-d3s7 | 487,066 | 385,960 | 232,825 | 3,236,265 | 140.83 | 2.1123 | 1.1949 |
| prior-1ply | 294,323 | 247,261 | 173,121 | 1,270,003 | 87.91 | 1.9353 | 1.0665 |
| fair-d3s7 | 326,717 | 269,648 | 192,040 | 1,714,793 | 95.87 | 2.0033 | 1.1138 |
| fair-d4s7 | 397,154 | 320,815 | 202,360 | 1,707,841 | 114.55 | 2.0598 | 1.1550 |

| contrast | delta | LB95 boot | LB95 t | UB95 | W-T-L | halves | floor |
| --- | ---: | ---: | ---: | ---: | --- | --- | ---: |
| candidate-d3s7-vs-fair-d3s7 | 155,153 | 126,819 | 126,919 | 183,307 | 333-0-179 | 159,105 / 151,201 | 28,185 |
| candidate-1ply-vs-fair-d3s7 | 1,323 | -17,545 | -17,456 | 20,096 | 268-0-244 | -21,521 / 24,166 | 18,746 |
| candidate-d3s7-vs-fair-d4s7 | 84,716 | 54,794 | 54,672 | 114,355 | 298-0-214 | 92,759 / 76,672 | 29,992 |
| fair-d4s7-vs-fair-d3s7 | 70,437 | 46,917 | 47,196 | 93,486 | 302-0-210 | 66,346 / 74,528 | 23,201 |
| candidate-d3s7-vs-candidate-1ply | 153,830 | 127,665 | 127,508 | 180,357 | 333-0-179 | 180,625 / 127,035 | 26,277 |
| prior-d3s7-vs-fair-d3s7 | 160,349 | 129,753 | 129,626 | 191,264 | 330-0-182 | 133,349 / 187,349 | 30,670 |
| candidate-d3s7-vs-prior-d3s7 | -5,196 | -40,535 | -40,060 | 29,158 | 267-1-244 | 25,756 / -36,148 | 34,803 |
| prior-d3s7-vs-fair-d4s7 | 89,912 | 58,243 | 58,255 | 121,567 | 290-0-222 | 67,003 / 112,821 | 31,602 |
| prior-1ply-vs-fair-d3s7 | -32,394 | -51,190 | -51,084 | -13,948 | 219-0-293 | -50,576 / -14,211 | 18,658 |

Gate (candidate-d3s7 vs fair-d3s7) passed: True

- PASS screen artifact: illegalDecisions 0 and incompleteDecisions 0 in every arm: 
- PASS bootstrap 95% lower bound of candidate-d3s7 minus fair-d3s7 > 0: 126818.78720703124
- PASS Student-t 95% lower bound > 0: 126919.162189214
- PASS paired mean delta > 0 in both halves: [159104.70703125, 151200.68359375]
- PASS candidate-d3s7 Q25 >= fair-d3s7 Q25: [241609.5, 192039.5]

Replication (prior-d3s7 vs fair-d3s7, the first experiment's frozen tables on this fresh block) passed: True

- PASS screen artifact: illegalDecisions 0 and incompleteDecisions 0 in every arm: 
- PASS bootstrap 95% lower bound of prior-d3s7 minus fair-d3s7 > 0: 129753.3208984375
- PASS Student-t 95% lower bound > 0: 129625.57751269455
- PASS paired mean delta > 0 in both halves: [133348.80078125, 187349.16796875]
- PASS prior-d3s7 Q25 >= fair-d3s7 Q25: [232825.0, 192039.5]

Scale (candidate-d3s7 vs prior-d3s7): inconclusive: delta -5,196, LB95 boot -40,535, LB95 t -40,060, UB95 29,158, W-T-L 267-1-244, floor 34,803
