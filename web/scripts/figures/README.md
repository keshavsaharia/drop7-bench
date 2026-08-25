# Research figures

A small, dependency-free toolchain that turns a JSON spec of recorded numbers
into a self-contained SVG the console can inline with `<Figure name="…" />`.
The point is reproducibility: a figure is regenerated from its spec, every
number on it names the record it was copied from, and nothing is computed.

```
web/content/figures/<name>.json   the spec (hand-written, numbers copied verbatim)
web/content/figures/<name>.svg    the rendered figure (generated, committed)
web/scripts/figures/generate.mjs  render one spec
web/scripts/figures/generate-all.mjs  render every spec
web/components/Figure.tsx         the MDX component
```

## Regenerate

```sh
node web/scripts/figures/generate-all.mjs                      # every spec
node web/scripts/figures/generate.mjs web/content/figures/score-vs-depth.json
```

Output is deterministic (no dates, no random ids), so regenerating an unchanged
spec produces a byte-identical SVG. Commit the SVG with the spec.

## The provenance rule

**Every point must carry `sourceRecord`.** It is a research record ID
(`RS-…`, `RUN-…`, `EX-…`, `TH-…`) or a finding document path under `docs/`
(for example `docs/exploratory/finding-05-chance-strata.md`). The generator
refuses to render a spec with a point that lacks one. `sourceField` is optional
but strongly encouraged: name the metric key or table row the number came from
so a reader can check it in seconds.

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
labelled as a one-sided 95% lower bound in the popover), optional `n` (games),
optional `label` (shown in the popover; for `dot` also drawn next to the
marker, useful for W-T-L), `sourceRecord` (required), `sourceField` (optional).

Kinds:

- `line` — metric against a numeric parameter, one polyline per series.
- `bar` — paired deltas around a zero line, one bar per point, whiskers for
  bounds. Use for candidate-minus-comparator contrasts.
- `dot` — per-arm comparisons on categorical x; series sit side by side
  within each category.
- `forest` — the same categorical points as `dot`, laid out as rows with the
  metric on the horizontal axis. Use when category names or point labels are
  too long to share a vertical x-axis. Row names and the title are truncated
  with an ellipsis; the full string is on hover. Long `label` text stays in
  the popover.

## What the SVG contains

- `viewBox` with `width="100%"`, so it is responsive.
- Colours through CSS variables (`--fig-fg`, `--fig-muted`, `--fig-grid`,
  `--fig-pop-bg`, `--fig-pop-border`) with fallbacks, set by the
  `.research-fig` block in `web/app/globals.css`.
- Each point is `<g class="fig-pt" tabindex="0">` holding the marker and a
  hidden `<g class="fig-pop">` popover (label, value, bounds, n, source) shown
  on hover or keyboard focus; a `<title>` carries the same text for assistive
  technology. Popovers are clamped inside the viewBox.
- A `<desc>` with the title, notes and the de-duplicated list of sources.

Text width is estimated at 0.6 em per character; keep titles, series names and
`sourceField` values short enough to read.

## Using a figure in MDX

```mdx
<Figure name="score-vs-depth" caption="One sentence on what the reader should see." />
```

`Figure` reads the SVG and the spec through `web/lib/repo.ts`, inlines the
SVG, renders the caption, and lists every point and its source under a
collapsible "Source data" table (experiment, theory and docs sources link to
their pages; result records link to their experiment's results section). If
the SVG is missing it renders a visible "not generated" notice rather than
failing, so a checkout without generated figures still builds.
