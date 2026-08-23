# diagram-occupancy-fixed-point.svg — source and reading guide

Mechanism diagram **D4** of `runs/RUN-20260823T191900Z-b9f8f80d/kimi-k3-figure-plan.md`.
Hand-written, self-contained SVG. This is a **schematic** phase diagram: the two
curves are smoothed shapes that follow the recorded occupancy-band means of
finding-06 §3, but no data points are drawn and the caption says so. The only
numbers on the canvas are the axis scales, the recorded constant 2.400 = 12/5,
and the two documented crossing locations (≈20 and 25–29 cells).

## What it explains

The mechanism behind `docs/exploratory/finding-06-flow-ceiling.md` §3 (and the
measured companion figure `occupancy-clear-rate-equilibrium`, P7 of the plan):
why ~20 occupied cells is an attracting equilibrium for the clairvoyant
clear-seeker, and why fair depth 4 death-spirals.

## Element-by-element

- **Axes:** x = occupied cells before the move (0–49 of the 7×7 board); y =
  numbered clears per move (0–6).
- **Dashed accent line at 2.400:** the exact disc-conservation requirement —
  5 placed discs + 7 risen covered discs enter per five-move cycle, so survival
  needs 12/5 clears per move (finding-01; the companion 7/5 = 1.400 reveals per
  move is not shown).
- **Accent S-curve (clairvoyant `rh-clears`, schematic):** the achievable clear
  rate rises with occupancy and crosses 2.400 at ≈20 cells. Shape follows the
  recorded band means 1.08 / 1.48 / 2.16 / 2.97 / 4.21 / 5.70 (finding-06 §3,
  cap-1000 cohort).
- **Restoring arrows around the crossing:** the negative feedback — below the
  crossing the board fills and the clear rate rises; above it the board empties
  and the rate falls. Measured counterpart: occupancy stationary at 19–20 of 49
  cells for 200 consecutive cycles, slope −0.0015 cells/cycle, and 2.4023
  clears/move over moves 501–1000 (finding-06 §2.1).
- **Dark hump (fair depth 4, schematic):** peaks at 2.59 in the 25–29-cell band
  — barely above the requirement — then falls (2.26, 1.58, 1.40, 1.00 across
  the higher bands, finding-06 §3). It has no stable fixed point.
- **Danger dashed arrow past the hump:** "deficit compounds — the death
  spiral". Once the board is pushed past ~30 cells the rate drops under the
  requirement and the shortfall feeds itself; occupancy slope +0.99 cells per
  cycle on the eight tapes (+1.481 on 128 fresh tapes, finding-12 §2). This is
  finding-06's explanation of heavy-tailed lifetimes.
- **Caption strip:** the required disclaimer that this figure is schematic and
  the measured curves live in `occupancy-clear-rate-equilibrium`.
- **Popovers:** the requirement's derivation; the measured fixed point; the
  feedback mechanism; D4's hump with its recorded band values; the death spiral
  with its recorded slopes.

## Simplifications (stated explicitly)

1. **Schematic, not measured.** The curves are smooth interpolations through
   the shapes of the finding-06 §3 band table; the plan mandates this ("a
   schematic phase diagram (not measured data)") because the measured version
   is the companion chart P7. Crossing locations are marked with ≈.
2. **Bands are not matched samples across policies** (finding-06 limitation 9);
   the schematic hides that caveat, and the popovers point back to the table.
3. The reveals-per-move requirement (1.400) is omitted to keep one axis; it is
   named in the requirement popover.
4. The clairvoyant curve is drawn from the cap-1000 cohort's band means; the
   two `rh-clears` cohorts agree band for band (finding-06 §3).

## Sources

- `docs/exploratory/finding-06-flow-ceiling.md` — §2 whole-game table (slopes),
  §2.1 (the 1,000-move cohort), §3 (the occupancy-conditional table and the
  fixed-point / death-spiral mechanism).
- `docs/exploratory/finding-01-score-is-survival.md` — the 12/5 and 7/5
  conservation constants.
- `docs/exploratory/finding-12-fair-planner-ceiling-extended.md` §2 — the
  fresh-tape D4 occupancy slope (+1.481).
- Figure spec: `runs/RUN-20260823T191900Z-b9f8f80d/kimi-k3-figure-plan.md`, D4.

## Conventions

Same as the other diagrams in this directory: theme-aware CSS variables with
light fallbacks, `fig-pt`/`fig-pop` pure-SVG hover/focus popovers matching
`web/content/figures/score-vs-depth.svg` and the `.research-fig` block of
`web/app/globals.css`.
