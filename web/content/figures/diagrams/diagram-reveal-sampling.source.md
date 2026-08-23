# diagram-reveal-sampling.svg — source and reading guide

Parameter diagram for the reveal-sample count **M**, written for the research
console. Hand-written, self-contained SVG. The panels are schematic: no board
position is drawn, and the bar heights in panel 3 are illustrative. All
measured numbers (the +64,116 M=6 result, the M=12 saturation interval, the
rung-2 Kendall τ and top-1 agreement) appear only inside hover popovers, with
their records cited — including `RS-20260823T225753Z-0fbd48c3`, cited again in
the caption strip and below.

## What it explains

What a **reveal sample** is, and why averaging several of them can change which
column looks best:

1. **The reveal event.** During a cascade, hits land on covered grays. A solid
   gray needs 2 hits in one wave-scan to reveal; a first hit only cracks it,
   and a cracked gray needs 1 more hit. When a gray reveals, the engine draws
   its value **at reveal time** — so a search that continues through a reveal
   must average over that draw.
2. **The factored chance node.** Finding-09 factored the chance node into
   `--disc-samples N` × `--reveal-samples M`, after finding that the unfactored
   search drew the next disc and the reveal from the same sample counter (with
   seven samples it saw 7 of the 49 joint (disc, reveal) atoms, perfectly
   correlated). **M = 1** continues each disc stratum with a single drawn
   reveal value; **M = 6** averages six continuations drawn with **common
   random numbers** — the same six draws reused for every sibling column, so
   columns are compared on identical luck.
3. **Why the ranking can flip.** Under M = 1, one lucky or unlucky draw is the
   whole estimate of a column's continuation, so the wrong column can look
   best. The six-draw average prices the reveal rather than the draw, and the
   ordering can change. This is measured, not hypothetical — see below.

## Load-bearing facts

- Engine rule (`approaches/lifetime-objective/fast-engine/fast-engine.hpp`,
  `resolveCascadeFast`, statement order per the reference
  `cfpi::detail::resolveCascadeSampled`): poppers are marked first, then every
  covered cell counts its orthogonal neighbours in the pre-clear popping set;
  `hits_needed` is 2 for a solid gray and 1 for a cracked one. The revealed
  value is drawn at reveal time (audit-01 M2).
- `docs/exploratory/finding-16-factored-reveal-sampling.md`, quoting
  finding-09: factoring the chance node and raising M from 1 to 6 at depth 3
  was worth **+64,116 points [95% lower bound +7,475]**. Joint (disc, reveal)
  coverage runs 14.3% (M=1) → 42.9% (M=3) → 85.7% (M=6) → 100% (M=12), and the
  axis **saturates at M ≈ 6**: M=12 buys nothing measurable (−27,097
  [−83,807, +31,209]). Read M = 6 as a local optimum, not "more is better".
- `research/results/RS-20260823T225753Z-0fbd48c3.json` (P-SOL-2 stage G0,
  rung-2 guardrail): on **6 CRN-matched roots with exact replay verified**, the
  M = 1 search (fast-d3s7) and the M = 6 search (native D3 N7M6) agree on
  within-root move orderings (KM lifetime) at only **mean Kendall τ 0.370
  (LB95 0.283, min 0.053), top-1 4/6**, worst at late-game roots. The record's
  own consequence: "the M=6 reveal quadrature genuinely changes within-root
  orderings, so an M=1 continuation corpus cannot carry D3 N7M6 label
  semantics." That is the direct evidence that M changes which column looks
  best.

## Element-by-element

- **Panel 1 (the reveal event):** a popping numbered disc hits a solid gray
  (danger arrow); the gray cracks (crack mark); a later hit reveals it, and the
  value fans out into the seven possible draws. Engine-rule footnotes below.
- **Panel 2 (the factored chance node):** one disc stratum shown (next disc =
  4, candidate column c, its cascade reveals a gray). M = 1: one dashed "?"
  draw v₁ → one continuation → estimate s(v₁). M = 6: six draws v₁…v₆ → six
  continuations → estimate (1/6)·Σ s(vᵢ). The CRN note states the
  identical-luck property.
- **Panel 3 (the ranking can flip):** two illustrative bar pairs. Under M = 1
  column 3's single draw scores above column 5's; under the M = 6 average
  column 5 scores above column 3. Bars are labelled illustrative; the measured
  ordering change is in the popover.
- **Caption strip:** the one-sentence claim, the factoring/saturation pointers,
  and the RS-20260823T225753Z-0fbd48c3 citation.
- **Popovers:** the engine cover rule; M = 1 and the finding-09 correlation
  defect; M = 6 with the +64,116 result and the M = 12 saturation interval; the
  rung-2 τ / top-1 measurement; why CRN matters.

## Simplifications (stated explicitly)

1. **Panel 1 is a schematic flow, not a board position** — no grid is drawn, so
   the engine's board-orientation convention does not apply. The crack-mark and
   cover-fill glyphs match `diagram-two-hit-reveal.svg`.
2. **The seven tiny discs** represent the draw's support (uniform on 1–7 in
   this simulator — the 49 joint atoms of finding-09 are 7 disc values × 7
   reveal values); the diagram does not assert where in the engine the
   distribution is defined beyond "drawn at reveal time".
3. **Panel 3's bars are illustrative.** They show the mechanism (one draw vs an
   average), not measured scores; the measured ordering change is the rung-2
   guardrail quoted in the popover.
4. **CRN is described by its property** (same tapes across siblings, exact
   replay), matching how RS-20260823T225753Z-0fbd48c3 uses the term; the
   diagram does not specify the tape layout.

## Sources

- `docs/exploratory/finding-16-factored-reveal-sampling.md` — the factored
  chance node, the M ladder, saturation at M ≈ 6.
- `docs/exploratory/finding-09-reveal-sampling.md` (quoted via finding-16) —
  the shared-counter defect and the +64,116 M=6 result.
- `research/results/RS-20260823T225753Z-0fbd48c3.json` — the M=6 quadrature
  genuinely changes within-root orderings (mean τ 0.370, top-1 4/6, 6
  CRN-matched roots, exact replay verified).
- `approaches/lifetime-objective/fast-engine/fast-engine.hpp` —
  `resolveCascadeFast` cover scan; `docs/exploratory/audit-01-engine-fidelity.md`
  M2 for draw-at-reveal-time.
- Companion diagram: `diagram-chance-strata.svg` (the N knob).

## Conventions

Same as the other diagrams in this directory: `viewBox="0 0 760 540"`,
`width="100%"`, theme-aware CSS variables with light fallbacks, the shared
`<style>` block, `fig-pt`/`fig-pop` pure-SVG hover/focus popovers with
`tabindex="0"`, `<title>`/`<desc>` with sources, marker IDs namespaced `rs-`.
18.0 KB, under the 30 KB budget.
