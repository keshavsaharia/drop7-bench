# diagram-two-hit-reveal.svg — source and reading guide

Mechanism diagram **D2** of `runs/RUN-20260823T191900Z-b9f8f80d/kimi-k3-figure-plan.md`.
Hand-written, self-contained SVG. The only numbers drawn are the disc values of an
invented position; measured numbers from the reveal-construction screen appear only
inside hover popovers, with their record cited.

## What it explains

The same-wave double hit behind the 2026-08-23 reveal-construction term
(`web/content/log/2026-08-23.mdx`, experiment
`EX-20260823-reveal-construction-screen-v2-63d73b6a`), and why a weighted sum of
marginal readiness cannot express it.

## The engine rule

From `approaches/lifetime-objective/fast-engine/fast-engine.hpp`,
`resolveCascadeFast` (statement order copied verbatim from the reference
`cfpi::detail::resolveCascadeSampled`):

1. Each wave: scan the board, mark **all** poppers (a numbered disc pops when its
   value equals the contiguous occupied run length through it, row or column).
2. Covered cells are then scanned in row-major order, and each counts its
   orthogonal neighbours **in the pre-clear popping set** — poppers are removed
   only afterwards.
3. `hits_needed = 2` for a solid gray, `1` for a cracked one. `hits >=
   hits_needed` reveals (a number is drawn); otherwise the cell becomes cracked.
   Hits therefore accumulate across waves only through the cracked state; a
   **solid** gray reveals in one wave only if two hits land in that same scan.
4. Reveals are written in row-major order, then gravity applies, then the next
   wave scans — so a disc revealed this wave can itself pop later in the same
   cascade.

A derived fact stated in one popover: wave-1 poppers are always runs through the
dropped cell (the pre-drop board is stable and the drop changes exactly one
cell), and at most one cell of those runs can be orthogonally adjacent to a given
gray — so a same-wave double hit on a **solid** gray is necessarily a wave-≥2
event, which is why the diagram shows the completing discs as cascade-delivered.

## Element-by-element

- **Board 1 (setup):** bottom row (row 6) of a 7×7 board, row 0 at top, columns
  1–7 left to right. The solid gray at column 4 is flanked by two `2`s (columns
  3 and 5) whose vertical runs are one short (length 1, need 2). Ghosted `2`s
  with dashed fall arrows are being delivered by the ongoing cascade's gravity.
  The flanking `5`s are inert context.
- **Board 2 (actual):** the cascade completes both vertical runs in one scan;
  all four `2`s pop (accent rings). Two hit arrows strike the gray in the same
  wave — the "2 hits" badge — and the gray flips straight to a revealed number
  (the accent `4` with a dashed ring), skipping the cracked state entirely.
- **Board 3 (counterfactual):** the same pops spread across waves. The left pair
  has already popped (hollow crossed cells): one hit, the gray is cracked
  (crack mark). The right run's completing disc never lands inside the horizon
  (struck-through ghost). End state: still covered.
- **Caption strip:** "two hits, one wave = reveal — the leaf priced the
  marginals, not the joint event", plus the engine rule in one line.
- **Popovers:** the cover rule, why the event is a wave-≥2 event, the joint
  event vs `solid_exposure` (a weighted sum of the neighbours' marginal
  readiness), the reveal's chain continuation, the counterfactual, and the
  measured outcome of pricing it (RS-20260823T131226Z-16564ed9: +900 dose,
  2.08% of decisions changed, +3,204 points with 95% lower bound −26,860,
  reveals flat at 1.152 vs 1.154 per move; 60.5% of live setups uncollected —
  all verbatim from `web/content/log/2026-08-23.mdx`).

## Simplifications (stated explicitly)

1. **The boards are invented and minimal.** Only the bottom rows are drawn; the
   wave-1 pops elsewhere in the columns that deliver the completing discs are
   represented by ghost discs, not simulated. The drawn positions are internally
   consistent with the rules (no stable board contains a popper; the revealed
   `4` does not itself pop).
2. **The hit arrows are drawn from the adjacent bottom-row poppers** — the two
   cells that actually hit the gray. The upper poppers (row 5) are part of the
   same runs but are not adjacent to the gray and hit nothing.
3. **"Revealed as a 4"** is illustrative; in the base engine the value is drawn
   at reveal time (audit-01 M2), in the scenario engine it is latent.
4. The counterfactual panel shows the case where the second hit never lands
   in-horizon — the outcome the leaf's marginal pricing cannot distinguish from
   the joint case. In real play a cracked gray can of course still be revealed
   by a later hit; the diagram's claim is about what the horizon prices.

## Sources

- `approaches/lifetime-objective/fast-engine/fast-engine.hpp` —
  `resolveCascadeFast` cover scan (`hits_needed = cell == kSolid ? 2 : 1`).
- `web/content/log/2026-08-23.mdx` — the same-wave double-hit design, the
  `solid_exposure` marginal-sum limitation, and the v2 screen result.
- `research/results/RS-20260823T131226Z-16564ed9.json` (quoted via the log).
- `docs/agents/project-nature.md` — hit / crack / reveal vocabulary.
- Figure spec: `runs/RUN-20260823T191900Z-b9f8f80d/kimi-k3-figure-plan.md`, D2.

## Conventions

Same as the other diagrams in this directory: theme-aware CSS variables with
light fallbacks, `fig-pt`/`fig-pop` pure-SVG hover/focus popovers matching
`web/content/figures/score-vs-depth.svg` and the `.research-fig` block of
`web/app/globals.css`.
