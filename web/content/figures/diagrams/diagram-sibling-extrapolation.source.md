# diagram-sibling-extrapolation.svg — source and reading guide

Mechanism diagram **D1** of `runs/RUN-20260823T191900Z-b9f8f80d/kimi-k3-figure-plan.md`
("Diagrams (mechanisms, not charts)"). Hand-written, self-contained SVG; no measured
numbers are drawn.

## What it explains

The repeated failure mode named in `docs/research/status.md` §4 ("Learning
public-state values"): a model is trained on the outcome of the action that was
actually played, then deployment asks it to *rank* all seven legal columns — six
of which it never observed equally. This is failure class **(iii) sibling
coverage / within-root discrimination** of
`docs/exploratory/audit-05-optimistic-curriculum.md` §4, the largest class in the
census: 6 of 17 learned-policy experiments (experiments 5, 9, 11, 13, 14, 17).

## Element-by-element

- **Root board (both panels):** a small invented 7×7 position, drawn in engine
  orientation (row 0 at top, columns 1–7 left to right). Blue circles are
  numbered discs; solid gray circles are covered discs; the gray disc with a
  crack mark is cracked (one hit taken). "next disc: 2 · 3 moves to rise" is the
  rest of the public state a legal policy may use (`docs/methodology.md`,
  information boundary).
- **Seven arrows:** one per legal column, fanning from the root to the seven
  successor afterstates.
- **Solid accent arrow + "label: H40 return" tag (column 3):** the action that
  was played during data collection. Its successor is the only one carrying a
  training label — an H40 return under D1 continuation, the afterstate label
  panel of the 2026-08-20/21 experiments (`docs/research/status.md` §7).
- **Dashed arrows + "?" tags:** the six unplayed columns. No label exists for
  these afterstates; any value the model assigns them is extrapolation.
- **Deployment panel:** the same fan, but now the model must produce a ranking
  of all seven. The six never-observed successors carry dashed red outlines and
  "rank ?" tags; the observed one is marked "observed".
- **Caption strip:** the failure in one line. The second line quotes the
  status.md §4 conclusion that low value error on visited states did not
  guarantee good root-action ranking.
- **Hover/focus popovers** (the `fig-pt`/`fig-pop` convention of the chart
  generator): drill-downs on the root board (public-state definition and board
  orientation), the labelled sibling, the unlabelled siblings, the deployment
  fan, and the caption.

## Simplifications (stated explicitly)

1. **Successors are drawn as root + dropped disc.** Cascades, reveals and
   gravity after the drop are not simulated; the diagram is about label
   coverage, not mechanics. The landing cell of the dropped disc is the accent
   square in each mini-board.
2. **The root position is invented**, not a recorded board; no recorded board
   image exists in the repository for this purpose, and the mechanism does not
   depend on the position.
3. **"H40 return"** names the label family of the afterstate experiments
   (status.md §7); the diagram does not assert that every class-(iii) failure
   used that exact label — the class spans played-action-only labels, noisy
   labels, and labels that cannot separate siblings (audit-05 §4 definition).
4. The 2026-08-21 result quoted in the deployment popover (successor-closed
   coverage, every legal sibling, exact search-value labels, still worse than
   one-ply exact search) is from `docs/research/status.md` "Directions closed",
   included because it relocates the obstacle from coverage to capacity.

## Sources

- `docs/research/status.md` §4 (sibling extrapolation paragraph) and §7;
  "Directions closed" table (the successor-closed coverage row).
- `docs/exploratory/audit-05-optimistic-curriculum.md` §4 — class (iii)
  definition and the counts table (6 of 17; experiments 5, 9, 11, 13, 14, 17).
- `docs/methodology.md` — the public-state definition.
- Figure spec: `runs/RUN-20260823T191900Z-b9f8f80d/kimi-k3-figure-plan.md`, D1.

## Conventions

CSS variables with light-theme fallbacks (`var(--fig-fg, #222)`,
`var(--fig-muted, #888)`, `var(--fig-accent, #2563eb)`, `--fig-grid`,
`--fig-pop-bg`, `--fig-pop-border`, `--fig-cover`, `--fig-danger`) so the diagram
renders standalone and adopts the console's dark theme inside `.research-fig`
(`web/app/globals.css`). Popovers are pure SVG/CSS (`fig-pt` + `fig-pop`), the
same convention as `web/content/figures/score-vs-depth.svg`; they need no
JavaScript.
