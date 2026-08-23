# diagram-information-ladder.svg — source and reading guide

Mechanism diagram **D5** of `runs/RUN-20260823T191900Z-b9f8f80d/kimi-k3-figure-plan.md`.
Hand-written, self-contained SVG. Unlike the other diagrams in this set, this one
**does** draw measured numbers: rung heights are recorded clears-per-move rates,
and the inter-rung arrows carry the recorded gap shares. Every number is quoted
verbatim from a finding document; nothing is recomputed.

## What it explains

The privilege separation performed by findings 06 → 07 → 12: how much of the
clairvoyant advantage over fair D4 is planning and how much is hidden
information — and the direction of finding-12's fresh-tape correction.

## Element-by-element

- **Rung height** = measured numbered clears per move (axis at left). **Rung
  order** = privilege (what the evaluator may see), bottom to top.
- **Fair depth 4 (bottom, solid):** no sampling, public state only. 1.9865 on
  128 fresh tapes (finding-12); its dashed ghost at 2.0467 is the eight-tape
  reading (finding-06 §2).
- **Arm B (solid):** the legal fair planner — hidden board and future both
  sampled, H=7, K=256. 2.0260 on 32 fresh tapes (finding-12 §2). Its dashed
  ghost at 2.2309 is the withdrawn eight-tape reading (finding-07 §1), with the
  dashed downward arrow between them labelled "fresh-tape correction
  (finding-12)" — the correction made visible.
- **Arm A (dashed):** future tape known, hidden board sampled, K=64 — 2.1361 on
  the eight master tapes (finding-07 §2). Never re-run on fresh tapes, so it
  keeps its eight-tape dash.
- **Clairvoyant (top, accent):** `rh-clears` H=7 reads the hidden board and the
  future tape — 2.3663 on 64 fresh tapes (finding-12 §6; 2.3637 on the matched
  32-tape cohort, finding-12 §2). A ceiling and teacher, never a policy.
- **Dashed accent line at 2.400:** the exact survival requirement 12/5
  (finding-01). The clairvoyant's whole-game rate sits just under it because of
  the sparse opening; its steady-state second half reaches 2.4023
  (finding-06 §2.1).
- **Inter-rung arrows (left), recorded shares:**
  - D4 → arm B: **+0.1256 = 27.1% of the gap** (finding-12 §2, matched 32-tape
    cohort: D4 1.9004, arm B 2.0260, clairvoyant 2.3637) — annotated
    "(corrected; was 58.8%)" because finding-07's print of 58.8% is withdrawn.
  - arm B ↔ arm A: **knowing the future tape ≈ 0%** of the gap (2.1361 vs
    2.1403 at K=64, finding-07 §2).
  - arm B → clairvoyant: **not closed: 72.9%** — hidden board plus
    hindsight-optimization (PIMC) suboptimality; an upper bound on the
    information share.
- **Caption strip:** the solid/dashed cohort key.

## Simplifications (stated explicitly)

1. **Rung heights mix cohorts.** Each rung uses its best available measurement:
   fresh tapes where they exist (clairvoyant, arm B, D4), the eight master tapes
   where they do not (arm A). The *shares* on the arrows come only from the
   matched 32-fresh-tape cohort, so the decomposition is internally consistent
   even though the rung heights are not all from one cohort. This mixing is
   inherited from the plan's rung list and is flagged in the `<desc>`.
2. **Arm A sits above arm B's solid rung** despite arm B at matched K=64
   (2.1403) edging arm A (2.1361): arm A's rung is its eight-tape number and
   arm B's solid rung is its fresh number at K=256. The ≈0% arrow and its
   popover carry the matched-K comparison; the visual ordering is a cohort
   artifact, stated here rather than hidden.
3. **Four rungs, not five:** arm B and fair D4 see exactly the same information;
   they differ in planning, which is what the 27.1% arrow measures.
4. Reveals-per-move rates (1.3832 / 1.1256 / 1.1050 / 1.0339) are quoted only in
   popovers, to keep one axis.

## Sources

- `docs/exploratory/finding-07-fair-planning-ceiling.md` — §1 (K sweep; 2.2309),
  §2 (arms table: 2.1361, 2.1403, 2.0467; the ≈0% tape result; the PIMC
  residual), correction notice (0 of 160 fresh-tape fair games survived).
- `docs/exploratory/finding-12-fair-planner-ceiling-extended.md` — §2 (matched
  cohort 1.9004 / 2.0031 / 2.0260 / 2.3637; gap 0.4633; closed +0.1256 = 27.1%;
  not closed 0.3377 = 72.9%; 22.2% at H=5; 15.3% note), §6 (2.3663 / 1.3832 on
  64 fresh tapes, 59/64 censored), D4 1.9865 on 128 fresh tapes.
- `docs/exploratory/finding-06-flow-ceiling.md` §2 — the eight-tape D4 baseline
  2.0467.
- `docs/exploratory/finding-01-score-is-survival.md` — 2.400 = 12/5.
- `docs/methodology.md` — the information boundary defining "legal".
- Figure spec: `runs/RUN-20260823T191900Z-b9f8f80d/kimi-k3-figure-plan.md`, D5.

## Conventions

Same as the other diagrams in this directory: theme-aware CSS variables with
light fallbacks, `fig-pt`/`fig-pop` pure-SVG hover/focus popovers matching
`web/content/figures/score-vs-depth.svg` and the `.research-fig` block of
`web/app/globals.css`.
