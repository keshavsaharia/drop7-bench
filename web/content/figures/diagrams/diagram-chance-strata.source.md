# diagram-chance-strata.svg — source and reading guide

Parameter diagram for the chance-node sample count **N**, written for the
research console. Hand-written, self-contained SVG. The only numbers drawn in
the panels are the disc values 1–7, the weights 1/5, 1/7 and 0, and the frozen
work bound 3,200,000 with its declared 16,000,000 replacement (a configuration
fact, not a measurement). All measured outcomes — the 2.41 zero-weighted
values, the paired deltas, the work ratios — appear only inside hover popovers,
with their records cited.

## What it explains

What a **disc stratum** is, and why the exact expectation over the seven-point
uniform is what unlocks depth:

1. **The chance node has seven atoms.** After a drop resolves, the engine deals
   the next disc uniformly from {1,…,7}. The exact next-disc expectation
   averages the continuation value over all seven with weight 1/7 each.
2. **N = 5 strata cannot cover seven atoms.** The frozen fair-D4 reference
   draws five stratified samples per chance node, so at least two of the seven
   values receive zero weight at every node. Audit-02 (H1, quoted in
   finding-05) measured **2.41 of the 7 values zero-weighted on average**, and
   because which values are missed is fixed by the node's deterministic stream,
   the error never averages out.
3. **N = 7 is exact.** Seven strata put the true 1/7 weight on every atom.
   Measured at depth 4 on 64 previously unread seeds (finding-05 confirmation
   cohort): **+101,171 mean points (+34%), +27.5 moves, 95% lower bound
   +47,457**.
4. **The interaction is the point.** With 5 strata the fourth ply buys nothing
   (+7,723 [−42,743], n.s.); with 7 strata it is worth +86,172 [+26,468]. Depth
   and estimator quality are complements, not independent knobs — a deeper ply
   propagates whatever the chance estimator gives it, bias or signal.

## Load-bearing facts

From `docs/exploratory/finding-05-chance-strata.md`:

- Frozen reference: depth 4, 5 strata; candidate: 7 strata, same everything
  else (CHECK gate: reproduces the reference at default settings).
- Interaction table (64 paired games per arm): d4 7−5 strata +101,171
  [LB +47,457], 41-0-23; d3 7−5 +7,276 [−45,961] n.s.; 7 strata d4−d3 +86,172
  [+26,468]; 5 strata d4−d3 +7,723 [−42,743] n.s.
- Work bound: worst-case depth-4 work with branching b = 7 columns × N strata
  is 3,134,950 at N=5 (fits the frozen 3,200,000 bound with 2.1% headroom) and
  11,892,398 at N=7 (exceeds it 3.7×). A 7-strata run left at the frozen bound
  silently falls back to a completed depth 3; the 7-strata arms declared
  16,000,000. Measured work ratio 3.82× against a predicted 3.79×.
- Caveat carried in the N=7 popover: seven strata makes the *next-disc*
  expectation exact; covered-disc reveals are still sampled, so the reveal
  expectation is not exact. That is the separate M knob (finding-09/16), drawn
  in `diagram-reveal-sampling.svg`.

## Element-by-element

- **Top row:** the seven atoms as accent discs 1–7, each labelled 1/7; the
  exact-expectation formula to the right.
- **Left panel (N = 5, danger header):** five filled discs (values 1, 2, 4, 5,
  7) weighted 1/5; values 3 and 6 dashed and zero-weighted. The estimator line
  and the "deterministic, never averages out" note.
- **Right panel (N = 7, accent header):** all seven discs filled, each 1/7;
  "estimate = (1/7)·Σ over all seven = E exactly".
- **Bottom rows:** the interaction as two arrows — flat/muted for "5 strata:
  depth 3 → 4" (no measurable gain), accent for "7 strata: depth 3 → 4" (the
  fourth ply pays).
- **Caption strip:** the one-sentence claim, the zero-weight rule, and the
  work-bound caveat.
- **Popovers:** what a stratum is; the audit-02 H1 measurement; exactness and
  the confirmation-cohort result; the full interaction table; the work-bound
  arithmetic.

## Simplifications (stated explicitly)

1. **Which two values are missed is illustrative.** The panel shows values 3
   and 6 unsampled; in the real search the missed set is determined per node by
   the deterministic stratified stream and varies from node to node. The
   measured average is 2.41 of 7 (worse than the minimum 2, because draws can
   repeat), stated in the popover.
2. **Weights are shown as the estimator's weights** (1/5 per sampled value, 0
   for unsampled), which is exactly how the node average is computed; no claim
   is made about the internal partition the stratified sampler uses.
3. **The interaction arrows are qualitative.** The measured deltas and their
   intervals live in the popover, per the directory convention that measured
   numbers carry their record with them.

## Sources

- `docs/exploratory/finding-05-chance-strata.md` — strata, exactness, the
  interaction table, the work-bound analysis, both cohorts.
- `docs/exploratory/audit-02-fair-d4.md` (H1), quoted via finding-05 — the
  2.41-of-7 zero-weight measurement.
- Companion diagram: `diagram-reveal-sampling.svg` (the M knob).

## Conventions

Same as the other diagrams in this directory: `viewBox="0 0 760 540"`,
`width="100%"`, theme-aware CSS variables with light fallbacks, the shared
`<style>` block, `fig-pt`/`fig-pop` pure-SVG hover/focus popovers with
`tabindex="0"`, `<title>`/`<desc>` with sources, marker IDs namespaced `cs-`.
No board position is drawn, so the engine's board-orientation convention does
not apply. 16.6 KB, under the 30 KB budget.
