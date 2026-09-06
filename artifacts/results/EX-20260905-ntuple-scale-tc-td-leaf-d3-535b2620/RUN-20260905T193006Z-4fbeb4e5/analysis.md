# Analysis of RUN-20260905T193006Z-4fbeb4e5

## CHECK gates

All passed: True

- PASS codec-vs-horner: 2000000 random line words, 0 mismatches; max index 9999999
- PASS row-words-vs-accessor: 553 states, 0 mismatches
- PASS features-vs-reference[rows]: 553 states x 5 phases: 0 mismatches, 0 duplicate/short sets, 0 out of range; 70000000 entries
- PASS features-vs-reference[cols]: 553 states x 5 phases: 0 mismatches, 0 duplicate/short sets, 0 out of range; 70000000 entries
- PASS features-vs-reference[win23]: 553 states x 5 phases: 0 mismatches, 0 duplicate/short sets, 0 out of range; 30000000 entries
- PASS features-vs-reference[win32]: 553 states x 5 phases: 0 mismatches, 0 duplicate/short sets, 0 out of range; 30000000 entries
- PASS features-vs-reference[rows,cols,win23,win32,phase=none]: 553 states x 5 phases: 0 mismatches, 0 duplicate/short sets, 0 out of range; 200000000 entries
- PASS features-vs-reference[rows,cols,win23,win32,phase=cols]: 553 states x 5 phases: 0 mismatches, 0 duplicate/short sets, 0 out of range; 480000000 entries
- PASS features-vs-reference[rows,cols,win23,win32,phase=all]: 553 states x 5 phases: 0 mismatches, 0 duplicate/short sets, 0 out of range; 1000000000 entries
- PASS information-boundary: 553 states x 4 hidden-field perturbations: 0 leaf-value changes, 0 decision changes
- PASS reflection: 553 states: 0 value mismatches; 534 live states, 0 decision mismatches under mirroring
- PASS direct-policy-legality: 553 states (19 terminal), 0 illegal or missing decisions
- PASS leaf-in-search-determinism: 4 games x (1, 4, 4 workers): identical true, illegal/incomplete-free true; mean 124834 over 80-move caps
- PASS serial-training-determinism: two 12-game single-thread runs: fingerprints b5c5f9335b6b26dc / b5c5f9335b6b26dc, 435 updates, 3177 touched entries; fresh initial-board value 20.0000 (declared 20)
- PASS finite-and-profile: values finite true; 480000000 entries = 1.92 GB frozen, 5.76 GB trainable; per state: features 44 ns, value 76 ns (untrained tables, hot); direct decision 5.7 us (14.8 simulations) [sink 53064919120400 2211999.8]

## Pilot arms (training-role validation block, 64 paired games)

| arm | layout | alpha | entries | moves | train mean | final margin d3s7 vs fair | best margin | 1-ply final |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| A | rows,cols,win23,win32,phase=cols | 1.0 | 480,000,000 | 200,003,947 | 282,920 | 95,051 | 170,874 | -25,206 |
| B | rows,cols,win23,win32,phase=none | 1.0 | 200,000,000 | 200,002,183 | 187,495 | -70,587 | -35,091 | -154,693 |
| C | rows,cols,win23,win32,phase=all | 1.0 | 1,000,000,000 | 200,002,957 | 245,345 | 123,950 | 123,950 | -93,103 |
| D | rows,cols,phase=cols | 1.0 | 420,000,000 | 200,002,392 | 208,142 | 26,951 | 94,015 | -141,033 |
| E | win23,win32,phase=none | 1.0 | 60,000,000 | 200,001,766 | 173,066 | -83,261 | -80,696 | -158,333 |
| F | rows,cols,win23,win32,phase=cols | 0.25 | 480,000,000 | 200,003,528 | 259,132 | 73,116 | 112,886 | -77,162 |

Selected: arm C (rows,cols,win23,win32,phase=all, alpha 1.0), rule: the arm whose final validation point has the largest paired mean margin of ntuple-d3s7 over fair-d3s7; ties by fewer table entries

## Main run

Moves 4,000,004,271, games 45,852,198, wall 2,469 s, mean 1,956,636 moves/s, done True

| moves | ntuple-d3s7 | fair-d3s7 | paired delta | LB95 | W-L | 1-ply | touched entries |
| ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: |
| 200,027,819 | 453,536 | 337,611 | 115,925 | 46,103 | 41-23 | 248,686 | 63,553,722 |
| 400,060,957 | 473,807 | 337,611 | 136,196 | 60,912 | 39-25 | 281,009 | 75,356,191 |
| 600,095,322 | 481,648 | 337,611 | 144,037 | 70,140 | 40-24 | 281,904 | 82,708,478 |
| 800,131,078 | 491,213 | 337,611 | 153,602 | 69,106 | 39-25 | 255,970 | 88,088,875 |
| 1,000,167,066 | 604,502 | 337,611 | 266,891 | 158,787 | 44-20 | 302,303 | 92,305,210 |
| 1,200,203,606 | 474,550 | 337,611 | 136,939 | 61,273 | 41-23 | 265,704 | 95,779,599 |
| 1,400,240,319 | 607,633 | 337,611 | 270,023 | 165,079 | 47-17 | 323,225 | 98,740,776 |
| 1,600,277,123 | 509,888 | 337,611 | 172,277 | 84,741 | 43-21 | 276,702 | 101,321,085 |
| 1,800,315,771 | 531,422 | 337,611 | 193,811 | 107,982 | 41-23 | 319,334 | 103,609,602 |
| 2,000,352,773 | 593,477 | 337,611 | 255,866 | 160,477 | 39-25 | 318,354 | 105,650,235 |
| 2,200,391,703 | 453,485 | 337,611 | 115,874 | 38,562 | 41-23 | 327,690 | 107,491,617 |
| 2,400,429,413 | 552,724 | 337,611 | 215,113 | 116,274 | 43-21 | 304,750 | 109,179,385 |
| 2,600,469,503 | 482,609 | 337,611 | 144,998 | 70,310 | 44-20 | 285,458 | 110,725,705 |
| 2,800,506,807 | 496,446 | 337,611 | 158,835 | 67,963 | 37-27 | 302,054 | 112,158,575 |
| 3,000,545,916 | 517,928 | 337,611 | 180,318 | 102,679 | 43-21 | 295,605 | 113,492,347 |
| 3,200,585,530 | 547,107 | 337,611 | 209,497 | 133,543 | 44-20 | 298,386 | 114,732,761 |
| 3,400,627,531 | 506,536 | 337,611 | 168,926 | 75,605 | 41-23 | 317,634 | 115,899,162 |
| 3,600,668,274 | 470,056 | 337,611 | 132,446 | 58,262 | 41-23 | 303,216 | 116,997,017 |
| 3,800,707,333 | 507,071 | 337,611 | 169,460 | 90,402 | 45-19 | 296,536 | 118,032,027 |
| 4,000,004,271 | 530,088 | 337,611 | 192,478 | 113,016 | 43-21 | 320,977 | 119,007,526 |

Best validation point: {"moves": 1400240319, "artifact": "val-001400240319.json", "pairedDeltaD3": 270022.78125, "ntupleD3Mean": 607633.375, "fairD3Mean": 337610.59375}
Any positive validation margin (theory training-signal check): True

## Held-out screen (256 paired games, one-shot)

| arm | mean | median | Q25 | max | moves | clears/move | reveals/move |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| candidate-d3s7 | 484,577 | 407,474 | 212,820 | 2,295,869 | 140.21 | 2.1074 | 1.1899 |
| candidate-1ply | 281,441 | 230,850 | 157,750 | 815,564 | 84.30 | 1.9217 | 1.0608 |
| fair-d3s7 | 314,438 | 250,821 | 175,832 | 1,221,000 | 92.58 | 1.9848 | 1.1026 |
| fair-d4s7 | 377,803 | 282,076 | 192,562 | 1,794,652 | 109.34 | 2.0492 | 1.1499 |

| contrast | delta | LB95 boot | LB95 t | UB95 | W-T-L | halves | floor |
| --- | ---: | ---: | ---: | ---: | --- | --- | ---: |
| candidate-d3s7-vs-fair-d3s7 | 170,139 | 130,499 | 130,560 | 209,881 | 167-0-89 | 198,575 / 141,704 | 39,440 |
| candidate-1ply-vs-fair-d3s7 | -32,997 | -58,536 | -58,451 | -7,597 | 114-0-142 | -11,749 / -54,246 | 25,363 |
| candidate-d3s7-vs-fair-d4s7 | 106,775 | 62,574 | 61,891 | 151,706 | 160-0-96 | 109,211 / 104,338 | 44,724 |
| fair-d4s7-vs-fair-d3s7 | 63,365 | 31,538 | 31,238 | 95,449 | 138-0-118 | 89,363 / 37,366 | 32,013 |
| candidate-d3s7-vs-candidate-1ply | 203,137 | 167,199 | 166,028 | 240,598 | 185-0-71 | 210,323 / 195,950 | 36,977 |

Gate passed: True

- PASS screen artifact: illegalDecisions 0 and incompleteDecisions 0 in every arm: 
- PASS bootstrap 95% lower bound of candidate-d3s7 minus fair-d3s7 > 0: 130499.0115234375
- PASS Student-t 95% lower bound > 0: 130559.5594985295
- PASS paired mean delta > 0 in both halves: [198574.609375, 141704.171875]
- PASS candidate-d3s7 Q25 >= fair-d3s7 Q25: [212819.75, 175831.5]
