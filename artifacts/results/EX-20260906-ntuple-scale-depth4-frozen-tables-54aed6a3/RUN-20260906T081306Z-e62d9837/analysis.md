# Analysis of RUN-20260906T081306Z-e62d9837

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
- PASS leaf-in-search-determinism: frozen tables, 4 games x (1, 4, 4 workers): identical true, illegal/incomplete-free true; mean 252067 over 80-move caps
- PASS leaf-in-d4-search-determinism: frozen tables, 2 games x (1, 2, 2 workers) over 40-move caps: identical true, illegal/incomplete-free true, more work than depth 3 on every game true; work 422072121 vs 14437927 at depth 3; 82 s
- PASS serial-training-determinism: two 12-game single-thread runs: fingerprints b5c5f9335b6b26dc / b5c5f9335b6b26dc, 435 updates, 3177 touched entries; fresh initial-board value 20.0000 (declared 20)
- PASS finite-and-profile: values finite true; 1000000000 entries = 4.00 GB frozen, 12.00 GB trainable; per state: features 41 ns, value 84 ns (untrained tables, hot); direct decision 5.5 us (14.8 simulations) [sink 110341919120400 2211999.8]

## Held-out screen (512 paired games, one-shot)

| arm | mean | median | Q25 | max | moves | clears/move | reveals/move |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| prior-d3s7 | 465,675 | 352,446 | 212,969 | 2,674,728 | 134.82 | 2.0987 | 1.1851 |
| prior-d4s7 | 516,155 | 392,496 | 233,182 | 2,716,683 | 148.75 | 2.1255 | 1.2033 |
| fair-d3s7 | 332,968 | 262,338 | 174,599 | 1,745,301 | 97.72 | 2.0115 | 1.1225 |
| fair-d4s7 | 382,567 | 303,015 | 208,481 | 2,634,604 | 110.45 | 2.0459 | 1.1460 |

| contrast | delta | LB95 boot | LB95 t | UB95 | W-T-L | halves | floor |
| --- | ---: | ---: | ---: | ---: | --- | --- | ---: |
| fair-d4s7-vs-fair-d3s7 | 49,599 | 25,751 | 25,578 | 73,244 | 289-0-223 | 45,189 / 54,009 | 23,979 |
| prior-d3s7-vs-fair-d3s7 | 132,707 | 102,193 | 101,741 | 163,903 | 309-0-203 | 115,331 / 150,084 | 30,913 |
| prior-d3s7-vs-fair-d4s7 | 83,109 | 51,349 | 51,365 | 114,439 | 282-0-230 | 70,142 / 96,076 | 31,689 |
| prior-d4s7-vs-prior-d3s7 | 50,479 | 14,707 | 14,086 | 86,224 | 284-0-228 | 67,335 / 33,623 | 36,331 |
| prior-d4s7-vs-fair-d4s7 | 133,588 | 102,051 | 101,161 | 165,801 | 314-0-198 | 137,477 / 129,699 | 32,371 |
| prior-d4s7-vs-fair-d3s7 | 183,187 | 150,976 | 150,210 | 216,400 | 345-0-167 | 182,666 / 183,708 | 32,920 |

Gate (prior-d4s7 vs prior-d3s7) passed: True

- PASS screen artifact: illegalDecisions 0 and incompleteDecisions 0 in every arm: 
- PASS bootstrap 95% lower bound of prior-d4s7 minus prior-d3s7 > 0: 14706.909765625
- PASS Student-t 95% lower bound > 0: 14085.866195359107
- PASS paired mean delta > 0 in both halves: [67335.2890625, 33623.49609375]
- PASS prior-d4s7 Q25 >= prior-d3s7 Q25: [233181.75, 212968.75]

Persistence (prior-d4s7 vs fair-d4s7) passed: True

- PASS screen artifact: illegalDecisions 0 and incompleteDecisions 0 in every arm: 
- PASS bootstrap 95% lower bound of prior-d4s7 minus fair-d4s7 > 0: 102050.977734375
- PASS Student-t 95% lower bound > 0: 101161.09894605307
- PASS paired mean delta > 0 in both halves: [137477.09765625, 129699.0859375]
- PASS prior-d4s7 Q25 >= fair-d4s7 Q25: [233181.75, 208481.25]

Depth-step interaction ((tables d4 - tables d3) - (fair d4 - fair d3)): inconclusive: delta 881, LB95 boot -40,932, LB95 t -41,742, UB95 42,867, W-T-L 250-0-262, halves 22,146 / -20,385, floor 42,550
Theory falsifiers: {"primaryFalsifierUpperBoundBelowZero": false, "secondLegUpperBoundBelowZero": false, "persistenceLowerBoundAtOrBelowZero": false}

Replication (prior-d3s7 vs fair-d3s7, the first experiment's frozen tables on this fresh block) passed: True

- PASS screen artifact: illegalDecisions 0 and incompleteDecisions 0 in every arm: 
- PASS bootstrap 95% lower bound of prior-d3s7 minus fair-d3s7 > 0: 102193.1853515625
- PASS Student-t 95% lower bound > 0: 101741.20673084096
- PASS paired mean delta > 0 in both halves: [115330.6640625, 150084.1171875]
- PASS prior-d3s7 Q25 >= fair-d3s7 Q25: [212968.75, 174598.75]
