# Audit 05 — Optimistic-teacher pretraining and curriculum ordering for an NNUE evaluator

**Role of this document.** Independent read-only second opinion. No existing file
was modified, no gameplay cohort was run, no protected or final seed was touched,
and no new experiment, theory, or contribution record was registered. Every claim
below is traced to source lines in this checkout or to `docs/research/history.md`.
Where the corpus records a result only as *task-record only*, that label is
carried through rather than upgraded.

---

## 1. The owner's hypothesis, restated precisely

In the owner's words:

> training a model based on a large number of games where I provide an
> "optimistic long-horizon strategy" — something where I think of the best
> possible outcome and simulate it for an ML model, so it is unfairly biased
> toward the possibility of something working out favorably. I then want to
> balance that favorable bias with training on a lot of normal games where the
> odds are evenly placed. I think this "order of training input" might be
> relevant to placing the NNUE model weights in the right place, or perhaps
> another ordering function is better.

Formalised, this is two separable claims. They must be separated because the
evidence treats them very differently.

**H-order (the stated hypothesis, an optimization/initialization claim).**
Hold architecture, total example count, optimizer, learning-rate schedule, and
total gradient steps fixed. Vary *only the order in which examples are
presented*. Then there exists an ordering — specifically optimistic-first then
fair — that reaches a better parameter basin, measured by held-out legal-sibling
ranking, than (a) fair-only, (b) optimistic-only, or (c) the same examples in
shuffled order.

**H-pool (a distribution claim, implied but not identical).**
Adding optimistic/hindsight-generated *states* to the training pool improves the
evaluator relative to a fair-only pool of the same size, regardless of ordering.

**H-label (a target claim, implicit in "unfairly biased toward the possibility
of something working out favorably").**
The optimistic teacher's *labels* — outcomes realised on a favourable future —
are a useful regression target for a public network.

Section 6 argues H-label is not merely untested but mechanically harmful under
this game's objective, and that H-pool is the salvageable core of the idea.
Section 4 establishes that H-order has never been tested here. Section 5
establishes that the diagnosed failures of prior work are not of a kind that
ordering can repair.

---

## 2. Inventory

Read from source, not from doc summaries. "Shuffle" column answers: does the
trainer reshuffle the whole example pool every epoch (which erases presentation
order by construction)?

### 2.1 Oracle and hindsight sources

| Artifact | What it trained on / computed | Target | Schedule | Shuffle | Recorded outcome |
| --- | --- | --- | --- | --- | --- |
| `approaches/oracle-curriculum/perfect-information-oracle/main.ts` (`:155-227`) | Not a learner. Receding-horizon beam over the **realised** future disc and reveal tape; default depth 12, beam 512, 500-move cap (`:75-77`) | Accumulated score, ties broken by score (`:267`) | n/a | n/a | 2,079,579 pts / 500-move cap on `0x3d700000` (history.md:568); 1,058,931.5 mean over 12 games, all capped at 500 moves, under **historical 7k** scoring (history.md:735). Diagnostic only |
| `approaches/oracle-curriculum/oracle-dagger/main.ts` (`:737-790`) | Stage 1: oracle roll-in states, oracle action labels. Stage 2: **student**-visited states, oracle labels, aggregated into the same pool | 7-way action cross-entropy, label smoothing 0.05 (`:46`) | **Two-stage**: `initialEpochs` on oracle pool, then DAgger collection, then `daggerEpochs` on the union at **half learning rate** (`:786`) | Yes, per epoch (`:628-635`) | Rejected — task-record only (experiment-index.md:200); student drifted, oracle choices not recoverable from public state |
| `approaches/oracle-curriculum/oracle-distillation/oracle-distill.cpp` | 12 oracle roll-in games + 32 **fair-D3/s5 behavior** roll-in games (`:50-53`). **Both** carry the oracle's action as label (`:308`); only the roll-in action differs (`:309-311`). 200-move label cap | Soft action distribution, 0.92 mass on the oracle column (`:35`, `:272-276`) | Single pooled fit, 30 epochs, batch 64 (`:57-58`, `:648`) | Yes — `phase_student::train` reshuffles every epoch (`phase-student.cpp:592-601`) | Rejected — ledger (history.md:562-628). Train CE 0.480; **held-out top-1 0.218 / top-2 0.386, CE 3.282**, and *"oracle and behavior holdouts were similarly weak"* (history.md:607-609). Screen passed (+21,979/+5.25), confirmation reversed (−62,274/−18.1) |
| `approaches/oracle-curriculum/state-curriculum/generate.ts` | Emits `D7CURR1` records tagged `source ∈ {1 oracle, 2 combined, 3 phase-safety}` (`:31`, `:216-233`) | `remainingMoves`, `remainingScore` per record (`:33-40`) | Data generator only | n/a | Support-only |
| `approaches/oracle-curriculum/state-curriculum/oracle-curriculum.cpp` | Converts 64 oracle games (`:26-27`) into ≤4,096 canonical **public** restart states; privilege terminates at the `PublicState` constructor (`:12-16`). **Re-scores each state under 7 independent public-derived futures at H25 with fair D1** (`:30-31`, `:445-528`) | `mean_moves`, `survival_rate`, `clears/reveals per move`, `FlowBand` (`:482-537`) | n/a | n/a | Completed — task-record only support. 4,096 states generated, privilege isolation passed. **The aggregate fair-relabelled survival distribution was never reported.** See §7 |
| `approaches/oracle-curriculum/topology/oracle-topology-audit.cpp` | Matches oracle-D4/beam-128 states to fair-D3 states on seed, 20-move band, rise phase, occupancy, max height | Split-half-stable feature directions | n/a | n/a | Completed — ledger diagnostic (history.md:712-741). Oracle 429,182.5 / 200-move cap in 16/16 vs D3 90,273 / 63.6 |
| `.../topology/oracle-topology-residual.cpp` | 738 matched training examples, board-only 490→8 sparse NNUE, 3,937 params | Oracle-vs-fair topology logit | **240 fixed Adam epochs**, architecture and schedule frozen before collection (`history.md:1341-1345`) | Yes | Rejected — **underpowered**, not falsified: 170 held-out examples vs a 200-example gate (history.md:1372-1379). AUC 0.681 |
| `.../topology/oracle-topology-residual-extension.cpp` | Frozen model, no retraining, replay + fresh prediction extension | same | none (frozen) | n/a | **Prediction replicated** (pooled AUC 0.648, pair 0.651) then **policy rejected**: 16-game confirmation −41,829.5 pts / −24 moves, "catastrophic loss of several long baseline trajectories" (history.md:1462-1470) |
| `.../accessible-energy/accessible-energy-lab.cpp` | Public feature family distilled from split-stable oracle signals; ridge residual on fair-D3 successors, split by source game | Calibrated score prediction | Single ridge fit | n/a | Rejected — ledger. Within-position top-1 22.9%→31.3%, pairwise 49.2%→52.2%; whole-game screen lost 7 of 8 pairs (history.md:748-761) |
| `.../accessible-energy/accessible-energy-root-prior.cpp` | Frozen energy model restricted to a confidence-admissible root set | same | none | n/a | Rejected — ledger. Switched 43.94% of decisions; −15,467 pts / −12.25 moves (history.md:772-780) |
| `.../hindsight-planner/hindsight-planner.cpp` | **Public** determinization: 7 synthetic tapes from a public-state hash, depth-8/beam-64 clairvoyant continuation per tape, mean/lower-quartile blend | Best-case-per-tape value | none | n/a | Rejected — ledger. 51,500.5 / 37.5 moves vs D3's 107,076 / 72.5. Explicit diagnosis: **strategy fusion**, "tape-specific later decisions make the per-tape root values incompatible and overoptimistic" (history.md:784-800) |

### 2.2 "Optimistic" sources — a naming correction

| Artifact | What "optimistic" actually means |
| --- | --- |
| `approaches/ntuple-rl/optimistic-phase/optimistic-phase-ntuple.cpp` | **Optimistic weight initialization**, not optimistic data. `kOptimisticValue = 60.0f` (`:66`) spread across active features at construction (`:751-753`), asserted by a self-test (`:3270-3272`). Training data is ordinary on-policy TD. It *does* contain a genuine schedule — pooled phase weights are copied into separate phase heads at 20M transitions (history.md:4307-4312) — but that is a parameter-sharing schedule with a single arm, never ablated |
| `approaches/ntuple-rl/temporal-coherence/ntuple-tc.cpp:427-429`, `ntuple-phase-conditioned.cpp:37,147-151`, `ntuple-rl/bellman-ntuple/bellman-ntuple.cpp:88,329-335` | Same: optimistic scalar initialization as an exploration device |

**Finding.** There is no "optimistic-data" teacher anywhere in the repository
despite the directory name. The only best-case-future *data generators* are the
perfect-information oracle and the hindsight planner. Recorded outcome for
`optimistic-phase-ntuple`: after exactly 50M transitions, direct n-tuple play
averaged 181,733.4 / 56.36 and the two-boundary representative-outcome search
made it **worse**, 113,644.0 / 37.38, failing both Stage-A halves
(history.md:4371-4382).

### 2.3 NNUE-family work

| Artifact | Trained on | Target | Schedule | Shuffle | Outcome |
| --- | --- | --- | --- | --- | --- |
| `value-policy-learning/structured-nnue/structured-value-nnue.cpp` | 160 exact-D3/s5 trajectories, 9,800 fitting / 2,132 held-out labels, whole-game holdout, both orientations presented | Normalized remaining lifetime + survival-at-25/50 heads; 75,395 params | `kEpochs = 24` fixed (`:47`), LR 1e-3 (`:50`) | Yes, fresh RNG per epoch (`:556-563`) | Rejected — ledger. Train AUC 0.999/0.978, Spearman 0.887 → held-out AUC 0.855/**0.614**, Spearman 0.510, ECE 0.186/0.286. Both gates failed; screen never opened (history.md:388-401) |
| `.../structured-nnue/counterfactual-successor-nnue.cpp` | D3 roll-in roots, **every legal action** through 3 common strata, 8 independent 75-move continuations per successor, whole-game split | Absolute successor lifetime | fixed | n/a (no epoch loop in this file) | Rejected — ledger. Global Spearman 0.839, MAE 3.888 → **within-root top-1 15.4%**, 30.8% as a D3 leaf, pairwise 62.0%. Screen −63,196 pts / −39 moves (history.md:538-561) |
| `.../denoised-value/denoised-stochastic-value.cpp` | 64 exact-D3 roll-ins, states sampled every 3 moves, canonicalized/deduplicated, **32 independent stochastic futures per state** capped at 50 moves, 48/16 whole-game split | Expected capped lifetime + P(alive at 25/50) | `kEpochs = 30` (`:49`) | Yes (`:751-758`) | **Passed every prediction gate**: held-out Spearman 0.951, soft AUC 0.920/0.809, ECE 0.021/0.006 (history.md:455-461). Checkpoint retained: `artifacts/models/denoised-value/v1.bin`, 35,395 params, 141,780 bytes |
| `.../denoised-value/denoised-guided-veto.cpp` | Frozen `v1.bin` used only to veto a guided ensemble | n/a | n/a | n/a | Rejected — ledger. 4-game screen +58,503; 8-game confirmation −2,606 pts / −1.875 moves. "The screen was a false positive" (history.md:487-537) |
| `.../chance-state-nnue/nnue-value.cpp` | **Closest existing analogue to the owner's idea.** Consumes the `D7CURR1` curriculum: oracle records (`source==1`) as positives, combined/phase records as negatives, **bucketed by rise phase** (`:1048-1052`) | `remaining_moves` (Huber, threshold 20) **plus a margin ranking loss** on oracle-vs-fair pairs with lifetime gap > 25 (`:1079-1097`) | `curriculum_epochs = 120` (`:479`) | Yes (`:1036-1038`) | Rejected — task-record only (experiment-index.md:155); "the learner plateaued well below fair search". **No ledger entry exists** |
| `.../phase-distillation/phase-student.cpp` | Exact phase-D3/s5 action policy, public tokens | Action cross-entropy | Two-stage: `epochs = 40` then `dagger_epochs = 20` on the aggregated pool (`:662-665`, `:799-823`) | Yes (`:592-601`) | Rejected — task-record only |
| `.../phase-distillation/phase-q-student.cpp` | Phase features + common one-ply successor summaries | Scalar teacher Q | fixed | Yes | Rejected — task-record only |
| `.../d4-q-clone/d4-q-clone.cpp` | The preserved 1,508-train / 465-heldout D4 root-label file; **full legal root-Q vector**, per-root normalized; listwise (T=0.18) + gap-weighted pairwise | Within-root action ranking | 260 fixed epochs | Yes | Rejected — ledger. Train top-1 0.765 / pairwise 0.755 → **held-out 0.247 / 0.574**; regret 0.421 vs a public one-ply baseline's 0.213 (history.md:1518-1526) |
| `d4-long-outcome/long-outcome/curriculum-long-outcome-nnue.cpp` | 3,072 train / 1,024 held-out states from the **frozen public oracle curriculum** (corpus fingerprint `0xc649f123fc0cc4b9`, `:76`); every action, 7 scenarios, H100 | 5 heads: balanced return residual, survival, cumulative score, clears, reveals (`:41-61`) | `kEpochs = 36` (`:43`), cosine-ish LR decay to 0.15× (`:46`, `:1319-1321`) | Yes, per epoch (`:1355-1358`, `:1387-1388`) | Rejected — task-record only; survival prediction useful, **action ordering regressed vs exact search** |
| `d4-long-outcome/long-outcome/scaled-long-outcome-nnue.cpp` | 1,508 roots (5.24× the earlier corpus), all siblings, 7 CRN continuations at H25 | Same 5 heads | 40 epochs, 4 whole-game outer folds; **12 vs 48 hidden units as the only varied factor** | Yes | Rejected — ledger. **Explicitly refutes the sample/capacity bottleneck**: survival head r = 0.855/0.849 but top-1 fell 27.79% (D2) → 25.99% (12h) → **23.94% (48h)**; 0 of 4 non-regressing folds (history.md:3227-3282). Historical **7k** scoring |
| `d4-long-outcome/h200-sibling-nnue/d4-h200-sibling-nnue.cpp` | Locked 477-root / 8-origin H200 sibling corpus; 5 heads | Head 0 = **residual over D4**, initialized to **exact zero** so "the untrained model is exactly always-D4" (`:1199-1206`); aux heads = mean moves, clears, reveals, 99% material downside (`:1066-1069`) | `kEpochs = 48`, batch 32 roots, LR 1.2e-3, weight decay 1e-5, grad-norm 3.0; losses pair 1.0 / list 0.75 / point 0.25 / aux 0.12; comment at `:504-505`: *"One architecture and training schedule fixed before evaluation. There is no validation-driven epoch, seed, feature, width, or loss selection."* | Yes, `kShuffleDomain` per epoch (`:1480-1486`) | Rejected — task-record only; regressed D4 top-1, pairwise, and regret **in every origin fold** |
| `constructive-reservoir/panel-value/panel-value-nnue.cpp` | Every legal sibling's common H100 continuations | Public state value | `kEpochs = 16` (`:62`) | Yes (`:1152-1158`) | Rejected — task-record only; weak top-action accuracy and worse regret on untouched holdout |

### 2.4 Mixed oracle/fair RL curricula

| Artifact | Mixing rule | Ablated? | Outcome |
| --- | --- | --- | --- |
| `ntuple-rl/rainbow-q/flow-curriculum-rainbow.cpp` | Warm-starts from a frozen Q checkpoint, then **strictly alternates**: "half [of] episodes begin at an ordinary initial board; half begin at a canonical public state from the fixed oracle curriculum" (`:11-12`), implemented at `:698`. Final counts 206,899 initial / 206,898 restart (`:1385-1386`) over a fixed 16M-transition Stage A | **No** — single arm | Rejected — task-record only; failed all absolute Stage-A floors (400,000 pts / 120 moves / 70 Q25 moves / 2.10 clears / 1.15 reveals, `:60-64`) |
| `ntuple-rl/curriculum-option-ppo/curriculum-option-ppo.cpp` | `kInitialEpisodesPerIteration = kEpisodesPerIteration / 2` (`:48`) — fixed 50/50 initial vs mature-restart per iteration | **No** — single arm | Rejected — task-record only; modestly improved D1, far below D4 |
| `ntuple-rl/manifold-ppo/oracle-manifold-ppo.cpp` | Discriminator: oracle-curriculum states as positives, 1,024 fair-D1 games as negatives, matched on rise phase / occupancy / max height | n/a | **Stopped at coverage gate**: held-out AUC 0.925/0.915, pair ranking 0.930/0.912 (far above the 0.62/0.58 thresholds) but only 74.02% exact-match coverage vs an 80% requirement (history.md:4085-4095) |
| `ntuple-rl/manifold-ppo/manifold-root-prior.cpp` | Final scalar on all 3,032 matched pairs, used only as a near-tie root tie-break around exact D3 | n/a | Rejected — ledger. Whole-fit AUC **0.9459**, matched-pair ranking 0.9420, yet the policy scored 253,798.9 / 73.9 vs D3's 301,101.1 / 88.9. Ledger conclusion: *"strong matched-state classification does not by itself rank close root actions correctly"* (history.md:4123-4160) |
| `ntuple-rl/regenerative-expert-iteration/regenerative-expert-iteration.cpp` | **D4-initialized** recurrent evaluator, 8 rounds of fresh roll-in + replay reanalysis, 160,000 new + 40,000 reanalysed roots | Round count is a schedule, but there is one arm | Rejected — ledger. Roll-ins 110,294/36.4 → peak 138,229/44.1 → 116,598/38.0 vs D4's 308,295.578/90.031. Ledger diagnosis: *"targets were observed for the played action while deployment maximized predictions over unplayed siblings, so even regenerating on-policy trajectories did not remove the sibling extrapolation error"* (history.md:4243-4248) |
| `terminal-policy-iteration/deployment-panel/martingale-dual-b0.cpp` | Information-relaxation ranker: a 12-ply / beam-8 hindsight beam **charged a mean-zero penalty** `z = reward + V(next) − E_support[·]` for its future-information advantage | n/a | Rejected — ledger. Top-1 **28.93%** vs fair-D4 **38.16%**; pairwise 59.85% vs 66.82%; regret 0.350 vs 0.277; 0 of 8 origins passed; split stability 22.6% (history.md:4486-4551) |

### 2.5 Retained assets

- `artifacts/models/denoised-value/v1.bin` — 141,780 bytes, SHA-256
  `c8090759f3719fea8eb350dc1adf59e8578e6a36c1e380001a74e2c9470ef2fc`. The only
  retained model checkpoint in the repository.
- `research/datasets/` contains **only a README**. Every corpus referenced above
  (the 477-root H200 panel `bfda8ae3…`, the 4,096-state oracle curriculum
  `0x8657ac0dc83c6041` / `0xc649f123fc0cc4b9`, the D4 root-label file
  `f61801ab…`) lived under `/tmp` and is **gone from this machine** (verified:
  `ls /tmp/drop7*` returns nothing).
- Consequence: **any experiment proposed here must regenerate its own data from
  a leased training range.** No shortcut through retained corpora exists.
- `roadmap.md:127` additionally states the 477-root panel is *"reusable
  diagnosis, not fresh model-selection evidence"* — so even if recovered it
  could not gate a new model.

---

## 3. Verdict on the key question: was training ORDER ever ablated?

Using the requested three-way distinction:

| Level | Definition | Did the corpus do it? |
| --- | --- | --- |
| **(a)** Use oracle/hindsight data at all | — | **Yes, extensively.** ≥11 distinct programs (§2.1, §2.4) |
| **(b)** Mix oracle and fair data at a fixed ratio | — | **Yes, repeatedly.** `oracle-distill.cpp` pools 12 oracle + 32 fair-roll-in games (`:50-53`); `flow-curriculum-rainbow.cpp` alternates exactly 50/50 (`:11-12`, `:698`, `:1385-1386`); `curriculum-option-ppo.cpp` fixes 50/50 per iteration (`:48`); `nnue-value.cpp` pairs one oracle positive with one phase-matched fair negative in **every batch item** (`:1048-1052`) |
| **(c)** Ablate the ORDER / anneal schedule as an independent factor — oracle-first-then-fair vs shuffled vs fair-only vs reverse, with everything else matched | — | **No. Never. Not once.** |

### Evidence for the "no" on (c)

1. **Every supervised trainer in the repository reshuffles the entire example
   pool at the start of every epoch.** This erases presentation order by
   construction, and it is uniform across the corpus:
   `structured-value-nnue.cpp:556-563`, `denoised-stochastic-value.cpp:751-758`,
   `nnue-value.cpp:1036-1038`, `d4-h200-sibling-nnue.cpp:1480-1486`,
   `curriculum-long-outcome-nnue.cpp:1355-1358` + `:1387-1388`,
   `panel-value-nnue.cpp:1152-1158`, `phase-student.cpp:592-601`,
   `oracle-dagger/main.ts:628-635`. There is no code path in any of them that
   presents a subset first.
2. **The two multi-stage schedules that exist are DAgger data aggregation, not
   ordering arms, and each has exactly one arm.**
   `oracle-dagger/main.ts:760-790` runs oracle imitation, then collects
   student-visited states, then retrains **on the union** at half LR.
   `phase-student.cpp:799-823` does the same shape. Neither has a comparator
   that shuffles the same union from the start, and neither varies which pool
   comes first. This is the closest structural precedent for H-order and it was
   never treated as a factor.
3. **The one RL schedule that exists is a parameter-sharing switch, not a data
   order.** `optimistic-phase-ntuple.cpp` copies pooled tuple weights into
   per-phase heads at 20M of 100M transitions (history.md:4307-4312) — one arm,
   frozen in a preregistered protocol
   (`artifacts/protocols/optimistic-phase-ntuple/protocol.json`).
4. **Where a factor *was* deliberately ablated, it was capacity or samples, not
   order.** `scaled-long-outcome-nnue.cpp` is the corpus's only clean
   single-factor learning ablation: 12 vs 48 hidden units at identical epochs,
   losses, optimizer, folds (history.md:3243-3251).
5. **No trainer in the corpus runs multiple initialization seeds.** Every one
   fixes a single network seed (e.g. `d4-h200-sibling-nnue.cpp:516`
   `kNetworkSeed`, `curriculum-long-outcome-nnue.cpp:80` `kInitializationDomain`).
   So even if an ordering effect had appeared, the corpus has no machinery to
   separate it from initialization noise. Any future ordering experiment must
   add this.

**Conclusion.** H-order is **genuinely untested in this repository**. It has not
been rejected. A properly matched ordering ablation would be the first of its
kind here, and its negative result would be a real, durable contribution. That
said — see §5 and §8 — "untested" is not the same as "promising", and the reason
it is untested is that no prior failure ever pointed at ordering.

---

## 4. Why prior oracle distillation failed — failure-mode classification

Classification scheme as requested:

- **(i) representation / information gap** — public features cannot express what
  the teacher used.
- **(ii) distribution shift** — the student visits states the teacher's data did
  not cover.
- **(iii) sibling coverage / within-root discrimination** — only the played (or
  too few) actions were labelled, or labels are too noisy to separate siblings
  at one root.
- **(iv) objective mismatch** — the fitted quantity is not the quantity that
  should govern the decision.
- **(v) optimization / underfitting / bad basin** — the model could express and
  the data could support the answer, but training failed to find it.

| # | Experiment | Primary | Secondary | Decisive evidence |
| --: | --- | --- | --- | --- |
| 1 | Oracle distillation (`oracle-distill.cpp`) | **(i)** | (iii) | Train CE 0.480 → **held-out CE 3.282, top-1 0.218**. Crucially, `(ii)` was *controlled*: fair-D3 roll-in states were labelled with oracle actions (`:308-311`) and *"oracle and behavior holdouts were similarly weak"* (history.md:607-609). Fitting was excellent, so **not (v)** |
| 2 | Oracle DAgger (`oracle-dagger/main.ts`) | **(i)** | (ii) | DAgger exists precisely to fix (ii); it still failed. Index: "the student drifted and **oracle choices were not recoverable from public state**" (experiment-index.md:200) |
| 3 | Hindsight planner (`hindsight-planner.cpp`) | **(iv)** | (i) | 51,500.5 vs D3 107,076. Ledger names the mechanism: **strategy fusion** — per-tape root values are "incompatible and overoptimistic" (history.md:797-799). This is *exactly* the owner's "best possible outcome" generator, made public and fair, and it is the single worst result in the corpus |
| 4 | Oracle topology residual | **(0) underpowered** | — | 170 held-out examples vs a 200 gate; AUC 0.681 was favourable. Not a mechanism failure (history.md:1372-1379) |
| 5 | Topology residual extension | **(iii)** | (iv) | Prediction **replicated** (pooled AUC 0.648, pair 0.651) then policy lost 41,829.5 pts / 24 moves. State-level classification did not become action ranking (history.md:1450-1470) |
| 6 | Accessible-energy residual | **(ii)** | (iii) | Within-position top-1 22.9→31.3% on D3 successors, then lost 7/8 whole games (history.md:751-760) |
| 7 | Accessible-energy root prior | **(iv)** | — | Confidence set admitted 31.9% of alternatives and switched 43.94% of decisions; calibration, not learning, failed (history.md:772-780) |
| 8 | Oracle-manifold discriminator | **(0) coverage stop** | (iii) | AUC 0.925/0.915 — the *label task was easy*. Stopped at 74.02% match coverage vs 80% (history.md:4085-4095) |
| 9 | Manifold root prior | **(iii)** | — | AUC 0.9459, matched-pair ranking 0.9420, policy 253,799 vs D3 301,101. Ledger: "strong matched-state classification does not by itself rank close root actions correctly" (history.md:4157-4159) |
| 10 | Martingale-dual B0 | **(i)** | (iv) | The corpus's explicit attempt to *price out* the future-information advantage. Result: **worse than plain fair D4** on top-1 (28.93 vs 38.16), pairwise, regret, all 8 origins (history.md:4522-4533) |
| 11 | Structured multi-head NNUE | **(iii)** | (v-overfit) | Train AUC 0.999/0.978 → held-out 0.855/**0.614**; no sibling data at all (history.md:388-397) |
| 12 | Counterfactual-successor NNUE | **(iv)** | (iii) | **(iii) was controlled** — every legal sibling was labelled with 8 independent continuations — and it still failed: global Spearman 0.839 vs **within-root top-1 15.4%** (history.md:544-550). The absolute-lifetime target does not resolve within-root differences |
| 13 | Denoised-value veto | **(iii)** | — | Every prediction gate passed (Spearman 0.951, ECE 0.021); the *action* use reversed on confirmation (history.md:517-527) |
| 14 | D4-Q clone | **(iii)** | (v-overfit) | Full root-Q vectors, listwise+pairwise loss, and still train 0.765 → held-out 0.247 on 1,508 roots |
| 15 | Scaled long-outcome NNUE | **not (v)** | (iii)+(iv) | The corpus's direct test of (v): 5.24× samples and 4× capacity made **ranking worse** (top-1 27.79 → 25.99 → 23.94), while the survival head stayed at r ≈ 0.85 (history.md:3253-3266) |
| 16 | H200 sibling NNUE | **not (v)** | (iii) | Initialized as an **exact zero residual over D4** — literally the best known policy as the starting basin (`:1199-1206`) — and training moved away from it, regressing in **every** origin fold |
| 17 | Regenerative expert iteration | **(iii)** | (ii) | D4-initialized, on-policy regenerating; ledger names sibling extrapolation as "its central failure" (history.md:4243-4248) |

### Counts

| Class | Count (primary) | Experiments |
| --- | ---: | --- |
| (i) representation / information gap | **3** | 1, 2, 10 |
| (ii) distribution shift | **1** | 6 |
| (iii) sibling coverage / within-root discrimination | **6** | 5, 9, 11, 13, 14, 17 |
| (iv) objective mismatch | **3** | 3, 7, 12 |
| (v) optimization / underfitting / bad basin | **0** | — (and **actively refuted twice**, by 15 and 16) |
| (0) underpowered or gate-stopped, mechanism untested | **2** | 4, 8 |

### What this means for the owner's hypothesis — stated plainly

Curriculum ordering is an intervention on class **(v)**. It changes which basin
the optimizer lands in. It cannot create sibling labels that were never
collected, it cannot add public information that the state does not contain, and
it cannot change what quantity the loss is fitting.

**Zero of the seventeen failures are class (v).** Worse for the hypothesis, the
two experiments that came closest to *testing* the initialization/basin idea both
falsify it in the strongest available way:

- `d4-h200-sibling-nnue.cpp:1199-1206` starts training at **exactly fair D4** —
  the best known public policy, the ideal "right place" for the weights — and
  gradient descent walked away from it into a worse held-out ranking in every
  origin fold. If a better basin adjacent to D4 existed and were reachable, this
  arm would have found it.
- `scaled-long-outcome-nnue.cpp` (history.md:3227-3282) shows that the
  optimization is not starved: more data and more capacity made ranking *worse*,
  which is the signature of a **label/objective** limit, not a search-over-weights
  limit.

So: **as literally stated, H-order does not address the diagnosed failure mode.**
A fair second opinion has to say that directly. The bottleneck in this corpus is
that at a single root, the legal siblings' true long-horizon values are close
together relative to the noise in any affordable label, and no amount of
reordering the same examples changes that gap-to-noise ratio.

Two honest qualifications in the owner's favour:

- Class (i) failures 1, 2, and 10 are all *oracle-target* failures, and §6
  argues they share a single removable cause — optimism in the **label**. Fixing
  that is a data-generator change the corpus has never made.
- The corpus has never trained *any* model on a pool whose **state
  distribution** is optimistic while its **labels** are fair. That is H-pool, it
  is untested, and item 8 of the oracle-manifold result (AUC 0.925 separating
  oracle from fair states on public features alone) says the two pools really do
  occupy different, publicly-visible regions.

---

## 5. Objective-alignment analysis — verified arithmetic

### 5.1 Verification against `src/core/native/engine.hpp`

Confirmed by reading the engine, not the docs:

| Claim | Verified at | Value |
| --- | --- | --- |
| A row rise awards a **flat** amount | `engine.hpp:21`, added unconditionally at `:310` when `raiseCoveredRow` succeeds | `kLevelBonus = 17'000` |
| Board clear | `engine.hpp:22`, applied at `:297` and again at `:321` | `kClearBonus = 70'000` |
| Chain wave of depth *d* | `engine.hpp:202-206`, applied at `:263` as `popper_count * scoreForWave(depth)` | `floor(7·d^2.5)` = **7, 39, 109, 224, 391, 617** for d = 1…6 |
| Rise cadence | `engine.hpp:18`, tested at `:302` | `kMovesPerLevel = 5` |
| A failed rise ends the game with **no** bonus | `engine.hpp:302-306` | confirmed |
| The post-rise cascade **continues the depth counter** rather than restarting at 1 | `engine.hpp:312-319` (`next_depth = waves.back().depth + 1`) | confirmed — this is where deep, high-value waves come from |

So `score = 17,000·(successful rises) + 70,000·(clears) + Σ_waves popper_count·floor(7·d^2.5)`.

### 5.2 What fair D4's 308,295.578 mean is actually made of

At 90.031 mean moves, the number of successful rises per game is between 17 and
18 (a game usually dies *at* a rise attempt, which forfeits that bonus):

| Component | Value | Share of the 308,295.578 mean |
| --- | ---: | ---: |
| 17 rises | 289,000 | 93.7% |
| 18 rises | 306,000 | 99.3% |
| Everything else (chains + clears) | 2,300 – 19,300 | **0.7% – 6.3%** |

Even the generous bracket puts **at least 93.7%** of D4's score in a term that is
a pure step function of how long the game lasted. Chain scoring is nearly
irrelevant: at D4's recorded ~2.04 numbered clears per move, a residual of
~19,300 over 90 moves is ~214 points/move, consistent with typical waves at
depth 2–4 plus occasional rise-continued deep waves.

**Score in this game is, to first order, a relabelling of survival.** To reach a
1,000,000 mean you need ≈ 58.8 successful rises, i.e. **≈ 294 mean moves** —
about **3.3× D4's lifetime**. Board clears are the only alternative currency
(70,000 each ≈ 4.1 rises), and the corpus contains **no measurement of D4's
board-clear rate at all**; the residual above bounds it at well under one clear
per two games. That absence is itself a cheap, unclaimed measurement.

### 5.3 Consequence for the NNUE target — argue for hazard, not score

1. **Raw score is a heavy-tailed affine encoding of a bounded quantity.**
   `status.md:106-107` already warns "Score is heavy-tailed. One million-point
   game can coexist with a much lower average." Regressing raw score spends
   capacity on a 70,000-point jackpot that contributes < 7% of the mean and
   carries almost all of the variance.
2. **The corpus's own head-by-head results say survival is learnable and score
   is not.** In `scaled-long-outcome-nnue` the survival head reached r = 0.855 /
   0.849 while the return-residual head was r = 0.350, downside 0.325, variance
   **0.139** (history.md:2702-2704, 3260-3266). In `denoised-stochastic-value`
   the survival heads hit AUC 0.920/0.809 with ECE 0.021/0.006
   (history.md:455-461). Every strong head in this repository is a survival head.
3. **Therefore the target should be a per-rise hazard vector**, not a scalar:
   `h_j = P(survive rise j | public state, action)` for j = 1…8 (≈40 moves), with
   expected rises `Σ_j Π_{k≤j} h_k` as the derived decision scalar and clear
   probability as a separate low-weight head. This is a strictly better-posed
   version of what `curriculum-long-outcome-nnue.cpp:56-61` already blends into a
   single scalar (`kBalancedSurvivalWeight = 0.45`, `kBalancedScoreWeight = 0.30`,
   `kScoreScale = 350,000`) — the score component there is mostly re-encoding the
   survival component with extra variance.

### 5.4 Is the oracle's advantage (a) better choices, or (b) unavailable information?

This is the question that decides whether H-label can work at all. The corpus
does not contain the clean decomposition, **but it contains four independent
measurements that all point the same way**, and none points the other way.

| Measurement | What it isolates | Result |
| --- | --- | --- |
| Hindsight planner (history.md:784-800) | The oracle's *algorithm* (clairvoyant beam) with the **real tape removed** and replaced by public-hash-derived synthetic tapes | Collapses to **51,500.5 / 37.5 moves** vs fair D3's 107,076 / 72.5. Not merely "advantage shrinks" — it goes **far below** the public baseline. Diagnosed as strategy fusion |
| Martingale-dual B0 (history.md:4486-4551) | A hindsight beam **explicitly charged a mean-zero penalty** for its information advantage — the corpus's deliberate attempt to price out the leak | Top-1 **28.93%** vs fair-D4 **38.16%**; 0 of 8 origins pass. After removing the information advantage there is **nothing left** that beats plain fair search |
| Oracle-manifold discriminator (history.md:4085-4095) and root prior (history.md:4123-4160) | Whether oracle-visited **states** are publicly distinguishable, and whether that scalar ranks actions | States: **yes, decisively** — held-out AUC 0.925/0.915, whole-fit 0.9459. Actions: **no** — as a near-tie root prior it lost 47,302 points to fair D3 |
| Oracle distillation behavior holdout (history.md:607-609) | Whether the oracle's *action* is predictable from public state on **fair-policy** states (distribution shift removed) | held-out top-1 **0.218**, and oracle/behavior holdouts "similarly weak" |

**Read together: the oracle's advantage is overwhelmingly type (b) — exploitation
of the realised tape — with a genuine but non-actionable type-(a) residue.** The
type-(a) residue is real and shows up as *state* quality (AUC 0.93+), not as
*action* preference (top-1 0.218, root-prior loss).

Which is exactly the split the owner needs to know about:

- Optimism in the **label** (regress what happened on a lucky tape) is a type-(b)
  target. It is not a function of the public state, so a public network cannot fit
  it; the best it can do is fit `E[lucky outcome | public state]`, which is a
  **systematically upward-biased** hazard estimate. And the bias is
  state-dependent — largest exactly on cluttered boards where a favourable
  reveal is what saves you. Pretraining on it installs survival overconfidence
  precisely where overconfidence is fatal. The corpus has already seen what that
  looks like: the topology-residual extension's confirmation failed by
  "catastrophic loss of several long baseline trajectories, including 476,511/285
  → 139,399/90 and 329,049/200 → 74,172/55" (history.md:1466-1470).
- Optimism in the **state distribution** (train on boards the oracle reached, but
  label them fairly) is a type-(a) target and is legitimate.

**The single missing measurement.** Nobody has ever reported: *what is the fair
value of an oracle-visited state?* The machinery to answer it already exists and
is unused. `oracle-curriculum.cpp` re-scores each oracle state under 7
independent public-derived futures with fair D1 (`:352-528`), computes
`mean_moves`, `survival_rate`, `clears_per_move`, `reveals_per_move`
(`:482-528`), and even bands them (`FlowBand::kBlocked` when survival < 0.25,
`:531-537`). It generated 4,096 states. **The aggregate band distribution and
mean fair-relabelled survival were never recorded anywhere** — the ledger has no
entry, and `experiment-index.md:203` records only that "4,096 restart states were
generated and privilege-isolation checks passed."

That number is the crux of the owner's whole idea and it costs about an hour to
produce. If oracle states re-labelled fairly are barely better than
phase/occupancy-matched fair-D4 states, the optimistic pool has nothing to teach
and H-pool dies with H-label. If they are markedly better, the pool is worth
pretraining on and the program is alive.

---

## 6. Proposed decisive experiment

Two stages. Stage D0 is cheap and can kill or license the whole program. Stage E1
is the first matched ordering ablation in this repository. **Do not run E1 before
D0 passes** — that would repeat the corpus's most common process error (building
a model before establishing that its label carries signal).

### Stage D0 — Oracle advantage decomposition (tier `CHECK`/`PILOT`, ≈1–2 CPU-hours)

**Question.** Of the oracle's lifetime advantage over fair D4, how much survives
when the realised tape is replaced by independent public randomness?

**Method.** Reuse, do not rewrite:
`oracle-topology-audit.cpp` for exact matching on rise phase / occupancy / max
height / 20-move band; `oracle-curriculum.cpp:352-528` for public restart
re-labelling with domain-separated streams (`kRestartRevealDomain` "CRRV",
`kRestartVisibleDomain` "CRVS"); `fair-only-horizon.cpp` for the D1/D2
continuation.

Collect on a freshly leased **training** range (a new `0x3d…` lease; the
allocator must first import every historical range per `benchmarks.md:47-50`):

- pool **O**: states from oracle (depth 4 / beam 128) roll-ins, sampled from move
  50 onward as `oracle-curriculum.cpp:28` already does;
- pool **F**: fair-D4 states matched to each O state on rise phase, occupancy,
  and max height (this matching is what made the manifold discriminator honest).

Report four numbers plus split halves:

1. `R_tape(O)` — oracle's realised remaining moves on its own tape.
2. `R_fair(O)` — mean remaining moves of O states over K = 32 independent public
   futures under a fixed public policy (D1 for cost, D2 if budget allows).
3. `R_fair(F)` — same for matched F states.
4. `R_real(F)` — fair D4's realised remaining moves from F states.

Derived: **transferable fraction** `τ = (R_fair(O) − R_fair(F)) / (R_tape(O) − R_real(F))`.
Also report the `FlowBand` histogram over O (`oracle-curriculum.cpp:531-537`) —
the fraction of oracle states that are `kBlocked` under fair randomness is the
single most interpretable output of this stage.

Also compute, at oracle-visited roots, an **oracle-action sibling regret**:
evaluate *all* legal siblings under common independent futures and ask how often
the oracle's chosen column is the fair-best sibling, versus how often D4's is.
This directly tests whether the oracle's *actions* are a legitimate label at all,
independently of any representation question.

**Falsification.** H-pool is refused if `τ < 0.25`, or if the split halves of the
O pool disagree in sign, or if the oracle's action is fair-top-1 less often than
D4's. Any of those means the optimistic pool teaches tape luck, not strategy, and
E1 should not be run.

**Cost.** Oracle planning is cheap relative to D4: a depth-4 / beam-128 receding
beam expands ≈ 7·128·4 ≈ 3.6k nodes per move against D4's recorded 1,351,113 work
units per move (history.md:975-977). 2,000 O states × 32 futures × 25 moves ≈ 1.6M
public-D1 transitions — the same order as `oracle-curriculum.cpp`'s original run,
which fit inside a 30-minute wall cap and 256 MiB (`:35-36`).

### Stage E1 — Matched curriculum-ordering ablation (tier `CHECK` for gates, ≈1 CPU-day)

Run only if D0 passes.

**Dataset (one generator, one manifest, shared by every arm).**
Roots split by **whole origin game** into 8 origin folds. At each root, label
**every legal sibling** under **7 common event-keyed independent futures** at
H = 100, with the continuation policy fixed at completed public D2/s5 (the same
choice `scaled-long-outcome-nnue` and `public-regenerative-b0` made). Record per
sibling:

- survival indicator at each of the next 8 rises → hazard vector target;
- mean remaining moves;
- clears and reveals per move;
- board-clear event count (rare; separate low-weight head);
- realised score (recorded for reporting, **not** used as the primary target — §5.3).

Two state pools of **equal size**:

- **O**: oracle-visited public states, **fairly labelled** — optimism lives in
  the state distribution only. This is the corrected form of the owner's idea.
- **F**: fair-D4-visited public states, matched on rise phase / occupancy / max
  height.

Target ≈ 10,000 roots (≈ 5,000 per pool). At ~5 legal siblings × 7 scenarios ×
100 moves that is ≈ 35M synthetic transitions; `scaled-long-outcome-nnue`
generated 2.0M in 779.7 s on 4 workers (history.md:3238-3241), which scales to
roughly 1.5–2 hours on 32 threads. Halve the horizon to H = 50 if the projection
gate says otherwise.

**Model (frozen, identical in every arm).**
Reuse the training kernel in `d4-h200-sibling-nnue.cpp` — it already provides
exact direct/reflected accumulator averaging, sparse categorical inputs
(`kCategoryCount = 509`, `:497`), listwise + pairwise + pointwise + auxiliary
losses (`:511-515`), whole-origin folds, checkpoint round-trip, and a frozen
schedule comment at `:504-505`. **The only new code is a data-order module** plus
the hazard head replacing the score residual head. Retain the exact-zero primary
head initialization (`:1199-1206`) so that an untrained arm is exactly D4.

**Arms.** Identical dataset, identical total gradient steps, identical LR
schedule shape, identical batch composition size. Only presentation order varies.

| Arm | Order |
| --- | --- |
| A0 | **Non-learned comparator**: exact fair-D4 root-Q ranking (and exact D2, which beat every learned ranker in history.md:3253-3266) |
| A1 | **fair-only** — F pool only, N examples |
| A2 | **optimistic-only** — O pool only, N examples |
| A3 | **shuffled mixture** — O ∪ F, reshuffled every epoch (the corpus's universal default; this is the null against which H-order must win) |
| A4 | **O → F anneal** — the owner's hypothesis. Mixing weight on O ramps 1.0 → 0.0 across training (linear over the middle half, so early epochs are pure O and late epochs pure F) |
| A5 | **F → O anneal** — the reverse. If A4 beats A3 but A5 also beats A3, the effect is "annealing" rather than "optimism-first", which is a different and weaker claim |
| A6 | **difficulty-ordered** — the "other ordering function". Sort the mixture easy→hard by fair-relabelled outcome dispersion across the 7 scenarios (low-variance roots first), independent of pool identity |

**Seeds.** ≥ 5 network-initialization seeds per arm — 35 training runs total.
This is non-negotiable and is the piece the corpus has never had: without it, an
ordering difference cannot be distinguished from initialization noise. At the
model sizes in play (12k–75k parameters, 16–48 epochs, 90–800 s single-core per
run in the ledger) all 35 runs fit comfortably in a few hours on 32 threads.

**Offline gate (whole-origin, all legal siblings, no gameplay).**
Per origin fold and per ordered origin half, against the fair-relabelled
best-sibling target:

- top-1 accuracy (ties credited as in `d4-h200-sibling-nnue.cpp`);
- pairwise accuracy over every unordered legal sibling pair, ties at half credit;
- normalized regret;
- hazard-head calibration: ECE and Brier per rise index;
- scenario-half action stability (independent scenario splits must agree ≥ 70%,
  matching the `martingale-dual-b0` and `full-panel-cpi-preflight` convention).

**Falsification criterion for H-order (freeze before any metric is read).**
H-order is **rejected** unless the best ordered arm (A4, A5, or A6) beats the
shuffled arm A3 by **all** of:

- ≥ +0.02 absolute pairwise accuracy, and
- ≥ 5% relative reduction in normalized regret, and
- a between-arm gap ≥ 2× the across-seed standard deviation within A3, and
- non-regression in ≥ 6 of 8 origin folds and in both ordered origin halves.

Additional named sub-conclusions, all recordable as valid results:

- If A4 > A3 but A5 ≈ A4, the effect is *annealing*, not *optimism-first*.
- If A2 ≈ A1 and A3 ≈ A1, then H-pool fails: the optimistic states add nothing,
  and ordering the same nothing cannot help.
- If every arm loses to A0 (fair D4 / exact D2), report that plainly. Given that
  **no learned ranker in this corpus has ever beaten exact D2 or D4 on held-out
  sibling ranking**, this is the modal outcome and it is still a publishable
  negative that closes the ordering question.

### Hardware: does this need PyTorch on ROCm?

Verified on this machine: `python3 -c "import torch"` → `ModuleNotFoundError`.
ROCm is present (`/opt/rocm/bin/hipcc`, `/opt/rocm/bin/rocminfo`,
`/opt/rocm/llvm/bin/clang++`), plus system `g++` and Node.

**Recommendation: native C++, no PyTorch, no HIP.**

1. **The models are tiny.** Every NNUE in this corpus is 3,937–75,395 parameters
   with 49–84 active sparse inputs per example. 35 training runs of that size are
   minutes of CPU, not GPU work. `structured-value-nnue` trained in 91.0 s at
   9.28 MiB RSS (history.md:401-402).
2. **The cost is data generation, and it is CPU-bound and irregular.** Cascades,
   gravity, and variable legal-action counts are precisely what
   `docs/hardware/amd-ryzen-halo.md` assigns to the CPU ("Exact transitions and
   chain cascades → CPU: irregular control flow, tiny 7×7 state, exact integer
   semantics"). 16 cores / 32 threads is the right machine for this and the GPU
   would not help.
3. **Installing PyTorch-on-ROCm would add unvalidated risk to a study whose whole
   point is a small controlled difference.** gfx1151 support is stack-dependent;
   the hardware doc explicitly warns that "a detected GPU, a working display
   driver, and a supported ROCm compute stack" are not equivalent. Non-deterministic
   reductions would also break the corpus's determinism, exact-reflection, and
   bit-equal-checkpoint self-test conventions — and `benchmarks.md:74-77` treats
   any change to floating-point action ranking as a **new algorithmic candidate**,
   not a speedup.
4. Note also that `approaches/ntuple-rl/torch-ppo/` exists and its history entry
   (history.md:3764-3896, 3897-3989) is a chronicle of memory-ceiling aborts. The
   corpus's own experience with the PyTorch lane is poor.

Revisit GPU only for roadmap step 5's much larger distributional afterstate
ranker, and then as the batched-leaf **service** prototype described in the
hardware doc's Stage B — not for E1.

### Process obligations (not performed by this audit)

Before E1: register one theory record with mechanism and falsification criteria,
run `make research-validate`, obtain a seed lease that conservatively imports
every historical range, preregister the experiment with cohort role and stop
conditions, and write a machine profile via `make research-doctor`. Contribution
records under `research/contributions/` are required for anyone who materially
contributes. This document is an audit and registers none of those.

---

## 7. Honest assessment

**Is H-order, as literally stated, likely to work? No — perhaps 10%.**

The reasoning is not "somebody tried it and it failed". Nobody tried it, and
§3 says so plainly. The reasoning is that the mechanism does not connect to the
diagnosed disease:

- 0 of 17 documented transfer failures are optimization/basin failures.
- The best conceivable initialization — the weights of fair D4 itself — was
  already the starting point of `d4-h200-sibling-nnue` (`:1199-1206`), and
  training moved *away* from it into a worse held-out ranking in every origin
  fold. "Placing the weights in the right place" was tested at its theoretical
  optimum and the optimizer left.
- The sample/capacity bottleneck was explicitly tested and refuted:
  `scaled-long-outcome-nnue` gave the model 5.24× the data and 4× the width and
  ranking got *worse* (history.md:3253-3266). Models that are already
  over-fitting a noisy within-root signal do not benefit from a better basin.
- Every trainer in the repository reshuffles every epoch over 16–260 epochs. In
  that regime, initial presentation order is largely washed out by construction.
  If the owner wants ordering to matter *mechanically*, the regimes where it
  plausibly could are (i) single-pass or very-low-epoch supervised training, and
  (ii) bootstrapped TD/RL where the target is non-stationary — which is exactly
  where the corpus's one real schedule lives (`optimistic-phase-ntuple`'s
  pooled→phase-head switch). A many-epoch Adam fit on a fixed corpus is the least
  favourable possible setting for an ordering effect.

**Is H-label likely to work? No — and it is worse than neutral.**

Optimistic labels are a function of the realised tape, not of the public state.
Fitting them yields `E[lucky outcome | public state]`, an upward-biased hazard
estimate whose bias is largest on cluttered boards. Under this game's objective —
where §5 shows **≥ 93.7%** of score is a step function of survival — a
survival-overconfident evaluator is the specific failure that destroys long
games, and the corpus has already photographed that failure
(history.md:1466-1470). The corpus also contains the strongest possible warning
against best-case-future planning as a public procedure: the hindsight planner,
which is precisely "think of the best possible outcome and act on it" made fair,
scored **less than half** of fair D3 (51,500.5 vs 107,076) and was diagnosed as
strategy fusion (history.md:784-800).

**Is H-pool worth testing? Yes — 25–35%, and it is the salvageable core.**

Separate optimism-in-states from optimism-in-labels. The corpus supports each
half of that split independently:

- Oracle-visited states are a *publicly recognisable, genuinely distinct* region
  of state space: held-out AUC 0.925/0.915, whole-fit 0.9459
  (history.md:4085-4095, 4132). Whatever else is true, the pool is not
  degenerate.
- Fair re-labelling machinery already exists, already isolates privilege, and
  already passed its independence checks (`oracle-curriculum.cpp:12-16`,
  `:352-528`, `:584-604`).
- Survival/hazard targets are the only ones this corpus has ever fitted well
  (r ≈ 0.85, AUC 0.92, ECE 0.02), and §5 shows they are the *right* targets
  because score is 94–99% survival.
- `roadmap.md` step 9 already calls for "mature public restart states with
  independent future randomness" — H-pool is that idea with a proper control arm.

**What I would actually do, in order.**

1. **Run D0.** One to two CPU-hours. It produces the number the entire program
   turns on — the fair-relabelled value of an oracle state — which has never been
   reported despite the code to compute it having been written, run, and
   self-tested. Whatever it returns, it is a durable result. If `τ < 0.25`, the
   optimistic-teacher program ends here on solid evidence and the owner has saved
   a CPU-day and, more importantly, a wrong conclusion.
2. **Only then run E1**, and run it as designed: optimism in states, fairness in
   labels, hazard targets, all legal siblings, whole-origin folds, ≥5 seeds per
   arm, A3-shuffled as the null. Its most likely outcome is that ordering does
   not matter and that no arm beats exact D2/D4 — and that is a genuinely useful
   negative, because it will be the **first matched-arm ordering ablation in this
   repository** and it closes a question that would otherwise keep resurfacing.
3. **Do not** put optimistic labels into any training target, at any position in
   any schedule.

The most valuable thing in the owner's idea is not the ordering. It is the
instinct that *where the training states come from* matters — and that instinct
is correct, is supported by the AUC-0.93 manifold result, and is testable this
week. The ordering is the part of the hypothesis the evidence does not support;
the state distribution is the part it does.
