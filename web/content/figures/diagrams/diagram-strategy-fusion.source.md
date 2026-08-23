# diagram-strategy-fusion.svg — source and reading guide

Mechanism diagram **D3** of `runs/RUN-20260823T191900Z-b9f8f80d/kimi-k3-figure-plan.md`.
Hand-written, self-contained SVG. All bar heights and tape discs are
**illustrative** — no measured numbers are drawn; recorded numbers appear only in
hover popovers with their citations.

## What it explains

Why the hindsight planner collapsed — `docs/exploratory/audit-05-optimistic-curriculum.md`
§4, experiment 3 (`hindsight-planner.cpp`), primary class **(iv) objective
mismatch** — and the PIMC (determinized / hindsight-optimization) caveat of
`docs/exploratory/finding-07-fair-planning-ceiling.md` §2.

The mechanism: a hindsight planner draws K completions of everything it is not
allowed to know (future disc tape, hidden board), solves each exactly, averages
the per-completion value of each candidate first move, and plays the argmax. When
the per-tape optimal lines are incompatible — each tape's optimum is bad on the
other tape — the average can be maximized by a move that is optimal on **no**
tape. That is strategy fusion.

## Element-by-element

- **Tape strips (A and B):** the next-disc sequences of two sampled futures
  (illustrative values). Each strip stands for one full determinization: disc
  tape, risen rows and hidden board values.
- **Per-tape bar rows:** the exact value of each first move (columns 1–7) under
  that tape alone. The accent bar is the tape's optimal first move: column 2 on
  tape A, column 5 on tape B. Heights are illustrative and carry no axis.
- **Bottom bar row:** the mean of the two per-tape rows. Column 3 — second-best
  on both tapes — wins the average and is marked with a red ✗ and "best on
  neither tape". The heights are consistent with the two panels above: each
  tape's optimum is drawn low on the other tape, so neither survives the
  average.
- **Right-hand dashed arrow:** "average over K tapes → argmax", the planner's
  reduction step.
- **Caption strip:** the plan's sentence — "averaging clairvoyant values across
  tapes fuses incompatible strategies."
- **Popovers:** what a determinization is; the two per-tape optima; the fused
  argmax (with the recorded outcome: 51,500.5 mean vs fair D3's 107,076, the
  ledger's "incompatible and overoptimistic" at history.md:797-799, the corpus's
  single worst result); and finding-07 §2's sharpening note (fixing the tape
  removes sample diversity; arm A scored 2.1361 vs arm B's 2.1403 clears/move at
  K=64 on the eight master tapes — knowing the future bought nothing).

## Simplifications (stated explicitly)

1. **Two tapes are drawn; the planners sampled K up to 1024.** Two is the
   smallest number that exhibits fusion.
2. **Bar heights are invented** to show the mechanism (each tape's optimum
   poor on the other tape, the compromise column winning the mean). No per-tape
   per-column values are recorded in the repository, so none are drawn; the
   visible panels carry an "illustrative" tag.
3. The diagram shows the averaging rule, not the window solver, the rise
   cycle, or the board; those are in D2/D4 and the findings.
4. The recorded numbers in the popovers (51,500.5 vs 107,076; 2.1361 vs 2.1403)
   come from different cohorts and contexts (the historical hindsight-planner
   ledger entry; finding-07's eight master tapes) and are quoted, not compared,
   inside one popover each.

## Sources

- `docs/exploratory/audit-05-optimistic-curriculum.md` §4 — experiment 3 row
  (hindsight planner; class (iv); "incompatible and overoptimistic",
  history.md:797-799) and the counts table.
- `docs/exploratory/finding-07-fair-planning-ceiling.md` — method (the fair
  planner's K completions) and §2 (strategy fusion sharpened by fixing the
  tape; arm A vs arm B at K=64).
- Figure spec: `runs/RUN-20260823T191900Z-b9f8f80d/kimi-k3-figure-plan.md`, D3.

## Conventions

Same as the other diagrams in this directory: theme-aware CSS variables with
light fallbacks, `fig-pt`/`fig-pop` pure-SVG hover/focus popovers matching
`web/content/figures/score-vs-depth.svg` and the `.research-fig` block of
`web/app/globals.css`.
