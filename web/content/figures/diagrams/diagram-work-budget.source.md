# diagram-work-budget.svg — source and reading guide

Parameter diagram: what "work" counts in the fair expectimax search, why a
depth-4 seven-strata decision evaluates ~2.3M leaves, where the work cap stops
a search, and why leaf cost multiplies everything. Companion to
`diagram-chance-strata` / `diagram-reveal-sampling` (the N and M parameters);
this one is about the budget the search spends.

## What it explains

- One decision is one tree: a decision node expands **all 7 columns at full
  width** (no pruning — that is what fair D4 *is*, audit-02 I1), each column
  leads to a chance node over the N sampled next-disc strata, and the tree
  repeats per ply down to the frontier leaves.
- The census of one depth-4 five-strata decision (finding-13 §2.1): 796,058
  nodes, of which **764,899 (96.1%) are `fairLeaf` leaf calls** and 31,159
  (3.9%) interior nodes; logical work is 1,560,979 because iterative deepening
  re-expands plies 1–4. At seven strata, finding-08 §2 measured **2,271,280
  leaves per depth-4 decision** (615,090 at five strata).
- The work cap: the search deepens iteratively — work per completed depth at
  five strata is 70 / 2,221 / 60,800 / 1,383,207 (finding-13 §4B) — and a
  declared `--max-work` budget exhausted mid-ply falls back to the deepest
  **completed** ply (finding-13 §8.2). A 3.2M cap sized for five strata
  silently degrades d4 s7 to depth 3 (finding-05's failure mode, recalled in
  finding-13 §8); finding-08's seven-stratum arms run `--max-work 16000000`
  and verify completion by work/move = 4,956,614 (finding-08 §7).
- Leaf cost multiplies everything: leaves are 96.1% of nodes, so
  decision cost ≈ leaves × leaf cost. LeafNet (572,367 params, **1.33 µs** per
  state) × 2,271,280 ≈ 3.0 s per d4 s7 decision; the exported CNN teacher
  (3,006,543 params, **4,122 µs** per state) × 2,271,280 ≈ 9,362 s ≈ 2.6 h —
  gated but never played (finding-08 §2–3, §11).

## Element-by-element

- **Panel 1 (tree):** decision node → all 7 column circles (c1, c2, …, c7) →
  one column's chance node ("?") → the seven possible next discs, of which the
  chance node samples N = 5 or 7 → dashed "× every ply" → the frontier leaf
  row. Annotated: leaves are 96.1% of every decision's nodes; branching is
  7 × N per ply, worst case 49^d at seven strata.
- **Panel 2 (census bar):** one stacked bar per the finding-13 §2.1 census —
  96.1% leaves (accent) vs 3.9% interior (muted) — with the node count
  (796,058), the logical-work count (1,560,979), and the applied-move count
  (796,081) spelled out, plus the seven-strata leaf figure.
- **Panel 3 (work cap):** log10-scaled bars of work per completed depth at
  five strata (d1–d4). The dashed danger outline on the d4 row is the
  seven-strata requirement (4.96M) crossing the dashed 3.2M cap line; the
  curved arrow is the fallback to the deepest completed ply (d3). Notes state
  the finding-05 silent-degradation failure and the 16M s7 budget.
- **Panel 4 (leaf cost):** log10-seconds bars comparing LeafNet (≈3.0 s per
  d4 s7 decision) against the CNN (≈2.6 h), with the "roughly one microsecond"
  leaf budget and the frozen-`fairLeaf` 970 → 279 ns fast-engine figure
  (3.5×, still 58% of decision time, finding-13 §2.3).
- **Caption strip:** "work counts every node; the cap stops the deepening; the
  leaf (96.1% of nodes) multiplies everything", plus the NNUE-vs-CNN caption
  line requested by the owner.
- **Popovers:** what one unit of work is; N strata and the worst-case vs
  measured gap (10.4×, transpositions, finding-13 §6); why the leaf dominates
  (Amdahl ≈5×, table = 0.6% of a decision); work-cap semantics including the
  finding-05 failure; the leaf-cost frontier arithmetic; and a sources/caveats
  popover on the caption.

## Simplifications (stated explicitly)

1. **The tree is schematic.** Three of seven columns and three of seven
   strata arrows are drawn, with ellipses; no real position is implied. No
   board is drawn, so engine board orientation does not apply here.
2. **Bars are log-scaled** (stated under each panel); a linear bar for
   70 vs 1,383,207 (or 3 s vs 9,362 s) would be invisible at one end.
3. **The 3.0 s / 2.6 h figures are products of cited numbers** (2,271,280
   leaves × 1.33 µs / 4,122 µs), not independent measurements; the caption
   popover says so. finding-08's own wall-clock conclusions come from its
   cohort runs, not this product.
4. The d4 five-strata bar (1,383,207) is the search-parity gate's measured
   work/move; the census decision (1,560,979 work) is a different, larger
   sample of 24 real decisions. Both are cited with their own contexts; the
   diagram does not average them.

## Sources

- `docs/exploratory/finding-13-fast-engine.md` — §2.1 census (796,058 nodes,
  764,899 leaves = 96.1%, 31,159 interior = 3.9%, work 1,560,979, applied
  moves 796,081), §2.3 attribution (leaf 79.1% → 58%, Amdahl ≈5×, table 0.6%),
  §4B work per completed depth (70 / 2,221 / 60,800 / 1,383,207 at five
  strata), §5 ("the last ply is always ~96% of the nodes"), §6 (worst case
  582,727,796 vs measured 55.8M at d5 s7, 10.4×), §8 (finding-05's
  silent degradation; completed-depth fallback).
- `docs/exploratory/finding-08-learned-leaf.md` — §2 (615,090 / 2,271,280
  leaves per depth-4 decision at five / seven strata; CNN 4,122 µs per state),
  §3 (LeafNet 572,367 params, 1.33 µs; "a leaf evaluator has roughly one
  microsecond"), §5 (tuning cohort), §7 (`--max-work 3200000` at s5,
  `16000000` at s7; work/move 4,956,614), §11 (CNN never played).
- `docs/exploratory/finding-05-chance-strata.md` — the work-bound degradation
  failure, quoted via finding-13 §8.
- Figure spec: `runs/RUN-20260823T191900Z-b9f8f80d/kimi-k3-figure-plan.md`
  (parameter-diagrams batch, this diagram = "work budget").

## Conventions

Same as the other diagrams in this directory: `viewBox="0 0 760 540"`,
`width="100%"`, theme-aware CSS variables with light fallbacks,
`fig-pt`/`fig-pop` pure-SVG hover/focus popovers with `tabindex="0"`,
`<title>`/`<desc>` with sources, matching `diagram-two-hit-reveal.svg` and the
`.research-fig` block of `web/app/globals.css`. Under 30 KB.
