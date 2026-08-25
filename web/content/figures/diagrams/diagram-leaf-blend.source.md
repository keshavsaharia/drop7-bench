# diagram-leaf-blend.svg — source and reading guide

Parameter diagram: how the blended leaf of
[`finding-08`](../../../../docs/exploratory/finding-08-learned-leaf.md) works —
`(1−w)·handLeaf + w·scale·learnedLifetime` at the search frontier — and what
`w = 0` (bit-identical fallback) and `w = 0.5` (the frozen choice) mean.

## What it explains

- **Where the blend sits:** it is the leaf evaluator of the fair expectimax
  search, run at every frontier leaf — leaves are 96.1% of all nodes
  (finding-13 §2.1), i.e. 2,271,280 evaluations per depth-4 seven-strata
  decision (finding-08 §2). That is why the leaf budget is "roughly one
  microsecond" and the deployed model is the NNUE-shaped LeafNet (1.33 µs per
  state), not the 4,122 µs CNN teacher (finding-08 §2–3).
- **The formula:** `blended = (1−w)·handLeaf + w·scale·learnedLifetime`.
  `handLeaf` is the frozen `fairLeaf` — nineteen hand-weighted structural
  terms, already in points. `learnedLifetime` is LeafNet's predicted remaining
  lifetime in moves; `scale = 3400` maps moves to leaf points.
- **What w = 0 means:** the code short-circuits to `frozen::fairLeaf` before
  the model is touched, so the reference arms are the frozen policy
  **bit-for-bit** — they reproduced finding-05's published cohort to every
  digit, with 0 score-identity violations in 26,934 decisions (finding-08 §7).
- **What w = 0.5 means:** half structural leaf, half learned lifetime × 3400.
  Frozen with the lifetime head on a **separate** 32-game tuning cohort
  (seeds `0xa5241000`–`0xa524101f`) before the 2x2; the evaluation cohort was
  never touched by tuning (finding-08 §5).
- **The inverted U:** paired Δ vs `w = 0` on the tuning cohort: +8,174 at
  w = 0.25, **+33,681 at w = 0.5**, −30,947 at w = 1.0. The pure learned leaf
  is worse than the frozen one (occupancy climbs to 25.53 cells): a good
  lifetime predictor is still a worse *ranker of sibling leaves* than the 19
  structural terms. The two are complements; the blend is not a formality.
- **The 2x2 outcome (popover):** d5 = +39,105 (95% lower +1,138, significant);
  d7 = +17,281 (not significant); DiD = −21,824 → the preregistered
  chance-bias cap prediction is refuted; the value of the exact chance
  estimator falls 22% once the leaf carries a learned survival estimate
  (finding-08 §7).

## Element-by-element

- **Left panel:** a compact decision tree (decision node → 7 columns → chance
  node → 7 discs → dashed "× every ply" → frontier leaf row). One leaf is
  ringed in danger red; the arrow from it to the blend panel reads "every
  leaf". Notes give the 96.1% / 2,271,280 leaf counts.
- **Center panel:** the two blend inputs as boxes — `handLeaf` (accent border,
  frozen `fairLeaf`, 19 structural terms) and `learnedLifetime` (LeafNet,
  1.33 µs/state, with the `scale = 3400` note) — feeding the Σ mixer through
  arrows labelled `(1 − w)` and `w · 3400`, with the frozen `w = 0.50` badge,
  down to the blended leaf value in points that is backed up the tree.
- **Bottom panel:** the tuning response in w as a small plot — dashed zero
  line (the w = 0 reference), the four measured points joined by an inverted-U
  curve, the w = 0.5 point ringed and annotated "frozen for the 2x2", w = 0
  annotated "bit-identical fallback", and w = 1.0 (danger red) annotated "pure
  learned is worse: the blend is not a formality". A note states the
  tuning/evaluation cohort separation.
- **Caption strip:** "w = 0 is the frozen policy bit-for-bit; w = 0.5 blends
  half structural leaf with half learned lifetime × 3400", plus the
  tuning-cohort / 2x2 outcome line.
- **Popovers:** where the blend sits (leaf counts, the 1 µs budget); the hand
  leaf (19 terms, bit-exactness gate); the learned leaf (LeafNet vs CNN
  accuracy and cost, lifetime vs hazard head); the formula and the w = 0
  short-circuit validity gate; the inverted U with the tuning numbers; and
  the 2x2 outcome on the caption.

## Simplifications (stated explicitly)

1. **The tree is schematic** (three of seven columns and strata arrows drawn,
   ellipses for the rest); no real position is implied. No board is drawn, so
   engine board orientation does not apply here.
2. **The w-response plot draws the four tuning-cohort paired deltas as
   points** joined by a smooth curve for readability; the curve between
   measured points is illustrative, not interpolated data. None of the
   32-game bounds excludes zero — the plot's own note and popover say tuning
   chooses, evaluation tests.
3. **"× 3400" in the caption** is the frozen `scale` value; the diagram does
   not derive it (it was chosen on the tuning cohort, finding-08 §5).
4. The hazard-head row of the tuning table (−3,276) appears only in a popover,
   to keep the plot to the lifetime blend that was frozen.

## Sources

- `docs/exploratory/finding-08-learned-leaf.md` — §2 (leaves per decision;
  CNN 4,122 µs/state), §3 (LeafNet 572,367 params, 0.8564 vs 0.8646 Pearson,
  1.33 µs; the ≈1 µs leaf budget), §5 (blend parameters, tuning cohort, the
  inverted-U table, hazard head, "the blend is not a formality", the frozen
  `w = 0.50, scale = 3400, --leaf-value lifetime`), §7 (w = 0 short-circuit
  and the bit-for-bit validity gate; the 2x2 and DiD), §11 (CNN never played).
- `docs/exploratory/finding-13-fast-engine.md` — §2.1 (96.1% leaves), §4E
  (bit-exact leaf gate, 225,183 leaves).
- `docs/exploratory/finding-05-chance-strata.md` — the published cohort the
  w = 0 arms reproduced, quoted via finding-08 §7.
- Figure spec: `runs/RUN-20260823T191900Z-b9f8f80d/kimi-k3-figure-plan.md`
  (parameter-diagrams batch, this diagram = "leaf blend").

## Conventions

Same as the other diagrams in this directory: `viewBox="0 0 760 540"`,
`width="100%"`, theme-aware CSS variables with light fallbacks,
`fig-pt`/`fig-pop` pure-SVG hover/focus popovers with `tabindex="0"`,
`<title>`/`<desc>` with sources, matching `diagram-two-hit-reveal.svg` and the
`.research-fig` block of `web/app/globals.css`. Under 30 KB.
