# Research figures

A figure is a JSON spec of recorded numbers under `web/content/figures/`,
rendered in the console by `<Figure name="…" />` through the visx chart kit in
`web/components/charts/`. The point is reproducibility: every number on a
figure names the record it was copied from, and the chart computes nothing but
pixel positions and axis ranges.

```
web/content/figures/<name>.json      the spec (hand-written, numbers copied verbatim)
web/lib/charts/spec.ts               the spec type, validator and number formatters
web/components/charts/ResearchChart.tsx   the renderer (client component)
web/components/Figure.tsx            the MDX / Markdown-fence component (server)
web/scripts/check-figures.mjs        resolves every embed and validates every spec
```

The old string-templated SVG generator that lived here (`generate.mjs`) is
gone. It estimated text widths at 0.6 em per character inside a fixed
720 x 400 box, which is why legends ran into axis labels, crowded category
labels overlapped, and popovers were squeezed inside the SVG. The chart kit
lays text out from measured widths after hydration, keeps the title and legend
in HTML so they wrap, turns crowded categorical figures into horizontal rows
with a measured label gutter, and shows tooltips in a portal clamped to the
viewport. Figures render on the server too; the client only re-measures.

## Check

```sh
node web/scripts/check-figures.mjs          # every doc embed + every spec
node web/scripts/check-figures.mjs docs/research/status.md
```

## The provenance rule

**Every point must carry `sourceRecord`.** It is a research record ID
(`RS-…`, `RUN-…`, `EX-…`, `TH-…`), a finding document path under `docs/`
(for example `docs/exploratory/finding-05-chance-strata.md`), or a research
log entry path. The validator refuses a spec with a point that lacks one, and
`<Figure/>` renders a visible notice instead of the chart. `sourceField` is
optional but strongly encouraged: name the metric key or table row the number
came from so a reader can check it in seconds.

The rules of `AGENTS.md` and the web-console skill apply with no exceptions:
copy numbers verbatim, never average or interpolate, never merge cohorts into
one series silently. If two depths were measured on different cohorts, put
them in different series and say so in `notes`. If a number is not recorded
anywhere, leave the point out. The spec's `notes` field is shown under the
figure's "Source data" disclosure, so use it to state cohort, sample size,
scoring rule, evidence tier and any partial or stopped arm.

## Spec format

```json
{
  "title": "Mean score against search depth",
  "kind": "line",
  "x": { "label": "Search depth", "unit": "plies" },
  "y": { "label": "Mean score", "unit": "points" },
  "notes": "Cohort, n, scoring, evidence tier, caveats.",
  "series": [
    {
      "name": "7 strata",
      "dashed": false,
      "points": [
        { "x": 4, "y": 398498, "n": 64, "label": "depth 4, 7 strata",
          "sourceRecord": "RS-20260821T181917Z-9a34ba02",
          "sourceField": "metrics.d4s7ControlMeanScore" }
      ]
    }
  ]
}
```

Point fields: `x` (number for `line`, string category for `bar`, `dot` and
`forest`), `y`, optional `lo` / `hi` (drawn as whiskers; a lone `lo` is
labelled as a one-sided 95% lower bound in the tooltip), optional `n` (games),
optional `label` (shown in the tooltip; for `dot` also drawn beside the
marker, useful for W-T-L), `sourceRecord` (required), `sourceField` (optional).

Axis fields: `label`, optional `unit`, and for a numeric x axis an optional
`scale: "log"` (positive values only), which is a presentation choice, not a
change to any number.

Kinds:

- `line` — metric against a numeric parameter, one polyline per series. A
  series with one point draws a marker only. `dashed: true` gives a dashed
  line and hollow markers; use it for a stopped, partial or reference arm.
- `bar` — paired deltas around a zero line, one bar per point, whiskers for
  bounds. Use for candidate-minus-comparator contrasts.
- `dot` — per-arm comparisons on categorical x; series sit side by side
  within each category.
- `forest` — the same categorical points as `dot`, laid out as rows with the
  metric on the horizontal axis. Dashed series draw as hollow diamonds.

`bar` and `dot` figures with more than six categories, or whose labels do not
fit their slot at the current width, are drawn as rows automatically, so a
category name never needs to be shortened to fit.

## Using a figure

In MDX:

```mdx
<Figure name="score-vs-depth" caption="One sentence on what the reader should see." />
```

In plain Markdown under `docs/`:

    ```figure score-vs-depth
    caption: One sentence on what the reader should see.
    ```

`Figure` reads the spec through `web/lib/repo.ts`, validates it, renders the
chart and the caption, and lists every point and its source under a
collapsible "Source data" table (experiment, theory and docs sources link to
their pages; result records link to their experiment's results section). If
the spec is missing or invalid it renders a visible notice rather than
failing, so a broken spec cannot take a page down.

## Other charts in the kit

`web/components/charts/EvolutionCharts.tsx` draws the nnue-evolution run
snapshot (`web/content/figures/nnue-evolution/<run-id>.json`, produced by
`web/scripts/extract-nnue-evolution.ts` from the run's artifacts). Those
charts share the primitives, tooltip and layout rules above; they take the
snapshot's rows as props and draw them as written.
