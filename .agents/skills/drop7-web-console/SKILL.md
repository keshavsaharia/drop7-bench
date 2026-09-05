---
name: drop7-web-console
description: Extend the Next.js research console under web/ by adding routes, record types, approach, technique or documentation pages, engines pages, leaderboards, replays, navigation, or styling. Use for any request to show research output in the browser, build a dashboard or visualization, or add to the Drop7 web app.
---

# Drop7 web console

`web/` is a Next.js App Router site that renders this repository as a browsable
research console. It reads the repo directly at build/request time. It has **no
database or API server**, and it must stay that way.

Follow the repository contract in `AGENTS.md`. Every sentence a visitor reads
follows `.agents/skills/drop7-writing-style/SKILL.md`; read it before writing
page copy, captions, or MDX. Two references sit beside this file and this
skill: `references/card-art.md` for the animated card art, and
`.agents/skills/drop7-social-cards/SKILL.md` for a page's link preview, its
OpenGraph metadata, the sitemap and any raster image asset.

## Run it

```sh
npm run web                   # from the repo root; proxies to web/
cd web && npm run dev         # equivalent; port 7777
```

Dependencies live in `web/node_modules` and are gitignored, so run
`npm --prefix web install` on a fresh checkout. `web/data/` is gitignored too —
it holds regenerable benchmark output, not source. A checkout with no
`web/data/` is expected; the leaderboard route must degrade gracefully rather
than crash.

## How it reads the repository

Everything goes through `web/lib/`. **Use it rather than reaching for `fs` in a
page.**

- `repo.ts`: `REPO_ROOT` (from `DROP7_REPO_ROOT` or walking up from `web/`),
  `readRepoFile(relativePath)` (returns `null` when absent; always handle it),
  `listJsonRecords<T>(subdir)` with `getTheories()`, `getExperiments()`,
  `getResults()`, and the approach walkers `listFamilies()`,
  `listApproaches(family)`, `listAllApproaches()`,
  `listApproachesByTechnique(slug)`, `listApproachesByKind(kind)`,
  `readApproachFrontmatter(family, slug)`. An `ApproachEntry` carries
  `kind` (`strategy | engine | diagnostic | unknown`), `technique`,
  `featured`, `status`, `evidence`, `reads`, `draft`.
- `techniques.ts`: the technique catalogue as data (`TECHNIQUE_ORDER`,
  `listTechniques()`, `getTechnique(slug)`): fourteen slugs, each with a plain
  title, a one-line description and its primer slug. It is the closed
  vocabulary for the `technique` frontmatter key.
- `records.ts`: links research records to approach directories by scanning
  record JSON for `approaches/<family>/<slug>` paths:
  `recordsForApproach(family, slug)`, `approachForRecord(id)`, `listResults()`.
  Linkage only; nothing is computed.
- `learn.ts`: `listLearnPages()`, `listConceptPages()`, `loadConceptPage()`,
  `listTechniquePages()`, `loadTechniquePage()`, `techniquePageForTechnique()`
  over `web/content/learn/`.
- `headings.ts`: `extractHeadings(source, {minDepth, maxDepth})` returns the
  ids rehype-slug writes, so a page builds its table of contents without
  rendering twice.
- `labels.ts`: the one map from record vocabulary to reader text (`badgeText`,
  `readsSentence`, `tierSentence`). Colour for the same vocabulary lives once
  in `globals.css` (`.badge[data-kind][data-value]`).
- `search-index.ts`: `buildSearchIndex()` over approaches, techniques,
  concepts, glossary terms, docs, records and log entries (server only).
  `app/api/search/route.ts` serves it, and `components/SiteSearch.tsx` (the
  header button, or Command-K) fetches it once on first open and filters in
  the browser. Adding a page means adding it to the index, or it cannot be
  found.
- `engines.ts`: the engine catalogue and the comparison-table rows, every
  numeric cell paired with its record or finding, `null` meaning
  "not recorded".
- `leaderboard.ts`: `loadLeaderboard()` and `loadReplay()`, both `null`
  without `web/data/`. `docs.ts`: the curated and the grouped document index.

## Routes

| Route | Source |
| --- | --- |
| `/` | hero, stat strip, technique tiles, the first sections of `docs/research/status.md`, latest log entries, competition leaders |
| `/approaches` | every `kind: strategy` directory, grouped by technique (or by family with `?view=family`), filter chips as links (`?technique=&status=&reads=`), a client search field |
| `/approaches/technique/[technique]` | one technique group: art, primer summary, cards, directory list |
| `/approaches/[family]` | the family's strategy cards, its engine and diagnostic members, then the family essay |
| `/approaches/[family]/[approach]` | the approach template: header with badges, technique strip, the README MDX with a table of contents, Records and Source-files accordions, prev/next. Engine and diagnostic directories render here too with their own breadcrumb |
| `/approaches/[family]/[approach]/[...path]` | a source file inside the directory |
| `/engines`, `/engines/[slug]` | `web/lib/engines.ts` plus the engine READMEs read by path; the comparison table prints only recorded cells |
| `/diagnostics` | the `kind: diagnostic` directories in three groups |
| `/research` | `docs/research/status.md` and the six record cards |
| `/theories`, `/theories/[id]`, `/experiments`, `/experiments/[id]`, `/results`, `/results/[id]` | `research/theories`, `research/experiments`, `research/results`; overlays at `web/content/research/<id>.mdx`; the registered record sits in a `TechnicalRecord` accordion |
| `/log`, `/log/[date]` | `web/content/log/YYYY-MM-DD.mdx` |
| `/learn` | rules, concepts (ordered by `order`, `hidden: true` skipped), techniques, glossary, leaderboard guides |
| `/learn/[slug]`, `/learn/concepts/[slug]` | `web/content/learn/*.mdx`, `web/content/learn/concepts/*.mdx` |
| `/learn/techniques`, `/learn/techniques/[slug]` | the fourteen primers in `web/content/learn/techniques/*.mdx` |
| `/leaderboard`, `/leaderboard/[policy]/[round]`, `/leaderboard/human/[submission]`, `/compete`, `/play` | unchanged |
| `/docs`, `/docs/[...slug]`, `/src/[[...path]]` | `docs/**` markdown, the source browser |
| `/api/search` | the site search index, fetched by the header dialog |

Every route must render on a checkout with no `web/data/`, empty `research/`
directories and no `web/content/log/`.

## The design system

`web/app/globals.css` opens with one `@theme static` block: it is the **only**
place a literal colour may appear. Everything else, including inline SVG
attributes and chart code, reads a token: `var(--color-bg | surface | raised |
hover | cell)`, `var(--color-ink | ink-1 | ink-2 | ink-3 | ink-4)`,
`var(--color-rule | rule-strong)`, `var(--color-accent | accent-strong |
accent-soft)`, `var(--color-disc-1..7)`, `var(--color-status-*)`,
`var(--color-reads-*)`, `var(--color-series-1..8)` (the validated chart
palette), `var(--color-highlight)`, fonts `var(--font-display | sans | mono)`,
the `--text-*`, `--radius-*`, `--duration-*`, `--ease-*` and `--container-*`
scales. `node web/scripts/check-tokens.mjs` enforces this: it scans every
stylesheet, card art and figure kit and fails on a literal colour, with the
handful of deliberate exceptions (the chart ramps, the code ground, the Satori
renderers) recorded in the script itself. Chart-only tokens live in
`web/components/charts/charts.css`, mirrored in `web/lib/charts/palette.ts`.

Fonts are vendored under `web/app/fonts/` (Schibsted Grotesk for display,
Inter for body, JetBrains Mono for labels; OFL licences alongside) and loaded
with `next/font/local` in `web/app/fonts.ts`, so the site never fetches a font
at runtime. Removing a file falls back to the system stack.

Shared components, all server components unless noted:

- `PageHeader` (`crumbs`, `title`, `lead`, children = badge row) opens every
  route. `ArticleLayout` (`toc`, `aside`) is the reading frame: a prose column
  with a sticky aside at wide widths and a collapsed "On this page" above it
  otherwise; `Toc` renders the headings; `OpenOnHash` (client) opens the
  accordion a `#fragment` link points into.
- `Badge kind="status | evidence | reads | outcome | validity | tier | plain"
  value="…"` renders the recorded token; colour comes from CSS by kind and
  value, and `rejected`, `fail`, `not-supported-as-tested` are neutral, never
  red. The old `label`/`className` props still work.
- `Reveal` and its aliases `AgentContext` (bot icon, dashed border) and
  `TechnicalRecord` (ledger icon; `meta` for a record id) are the only place
  agent-facing detail appears. `TechnicalDetails` remains an alias.
- `Button variant="primary | secondary | ghost"`, `Card` (`href`, `art`,
  `eyebrow`, `title`, `summary`, `foot`, `playing`), `ApproachCard`,
  `TechniqueCard`, `FilterSearch` (client), `ApproachRecords`, `AsideRecords`
  (the aside's record cards, split by kind, five to a kind behind a "Show all"
  toggle), `EngineCard`, `RecordTable`. An approach card's art loops
  continuously with no JavaScript: `.card-wrap .tart [data-anim]` runs in
  `art.css`, the `:nth-child` block in `approaches.css` gives each card a
  different starting phase as a share of its own cycle, and
  `content-visibility: auto` on the wrapper keeps a page of ninety cards cheap
  by not rendering the ones off screen.
- `TechniqueArt name="q-learning" approach={{ family, slug }} mode="hover |
  loop | once | static"` draws the animated SVG for a technique from
  `web/components/technique-art/registry.ts` (fourteen techniques, three
  engines, a fallback), or the approach's own art from
  `technique-art/approach/registry.ts` when that directory has one.
  `technique-art/board.tsx` is the board kit an art draws a real position
  with, including `ArtScore`, which prints the game's own wave score from the
  engine's `scoreForWave`. A cycle is `--tart-motion` plus `--tart-read`, and
  an art that rests on a label it did not start with sets `--tart-read` so the
  words can be read before the loop repeats. **The full contract for writing
  one is `references/card-art.md` beside this file; read it before drawing
  art.**
- Primer figures live in `web/components/primers/<Technique>.tsx`, are
  exported through `web/components/primers/index.ts` and registered for MDX.
- `Drop7Board`, `Board`, `BoardCompare`, `Disc`, `Stat`, `Callout`, the
  figure kits (`Rules.tsx`, `Engine.tsx`, `Concepts*.tsx`, `Evolution.tsx`,
  `Engines.tsx`), `Figure`, `Diagram`, `StatTile`, `StatRow`,
  `EvidenceStrip`, the log components and the record summaries keep their
  MDX contracts.

CSS placement: `.prose-drop7` element rules in `globals.css` are **unlayered**,
and Tailwind v4 utilities live in a cascade layer, so those prose rules beat
every Tailwind class inside MDX regardless of specificity. Style MDX-rendered
components with a scoped class block, never with utility classes on inner
elements. A section's own styles go in a CSS file next to its pages
(`app/approaches/approaches.css`, `app/engines/engines.css`,
`app/learn/learn.css`, `app/research/research.css`, `app/home.css`) imported
by the page; component CSS (`technique-art/*.css`, `primers/*.css`,
`charts/charts.css`) is imported by the component. Every decorative motion
stops under `prefers-reduced-motion: reduce` and leaves a frame that still
explains the figure.

## Approach pages and the taxonomy

Frontmatter on every `approaches/**/README.mdx` carries `kind` (`strategy |
engine | diagnostic`; family READMEs `family`), `technique` (a catalogue slug,
strategies only) and optionally `featured: true`, alongside `title`,
`summary`, `status`, `evidence`, `reads` from the closed vocabulary in the
writing skill. `node scripts/check-approach-frontmatter.mjs` (repository root)
validates all of it. The template `docs/agents/approach-page-template.md`
describes the page skeleton: the body carries the five plain sections and the
agent-context accordions; the route renders the deck, the badges, the
technique strip and the Records and Source accordions itself, so a body never
repeats the summary, never carries an `EvidenceLabel`, and never lists its own
source files.

Machine-generated approach pages carry `draft: true` and are produced by
`node scripts/generate-approach-docs.mjs`, which never overwrites a
hand-written page. Check MDX with `node web/scripts/check-mdx.mjs <paths>`
(it discovers registered components from `web/components/primers`,
`Reveal.tsx`, `TechniqueArt.tsx` and the chart components, so a new export
there needs no edit) and sweep every route against a running server with
`web/scripts/check-routes.sh http://localhost:<port>`.

## Showing a position: `Drop7Board`

`Drop7Board` is the one component to reach for when a theory, experiment or
concept page needs to show a board and explain a decision on it. It draws the
position and optional annotations without a score panel or controls, so it fits
a sentence of prose:

```mdx
<Drop7Board
  cells="0000000000000000000000000000000400000020000005000"
  nextDisc={4}
  dropColumn={3}
  highlight={[31, 38]}
  columns={[null, null, { label: "c3", value: 1240 }, { label: "best", value: 2210, best: true }, { label: "c5", value: 980 }, null, { muted: true }]}
  caption="Engine output for this position: the 4 completes the column and the 2 follows."
/>
```

- `cells` uses the engine's `serializeBoard` encoding (row-major from the top;
  0 empty, 1–7 numbered, 8 solid gray, 9 cracked gray).
- `nextDisc` and `dropColumn` show the incoming disc over its column.
  `dropColumn` keeps the engine's zero-based index; rendered labels use columns
  1–7. `highlight` rings cells, and `dim` fades them.
- `columns` is a seven-entry per-column readout (label, value, `best`,
  `muted`); the bar under each value is a relative scale of the values you
  passed, not a computed number.

Values shown on a board must come from a record or from engine output
generated by a script under `web/scripts/`, exactly like every other figure.

## The playable game: `Drop7Game` and the browser solver

`/play` mounts `Drop7Game` (a client component) with a mode switch: `play`,
`evaluate` (the solver recommends), `auto` (the solver plays). It is also
available in MDX as `<Drop7Game mode="evaluate" maxDepth={4} />`. Rules come
from `src/core/typescript/engine.ts` directly; the console imports `src/`
through the webpack build. The solver runs in a Web Worker
(`web/lib/play/solver.worker.ts`) as `fastEvaluateMoves`
(`web/lib/play/fast-search.ts`) and must stay value-identical to the
reference `evaluateMoves`; `cd web && npm test` checks exactly that. Run it
after touching anything under `web/lib/play/`. The browser solver is a
demonstration of one policy; it is never tier evidence, and pages must not
describe it as the C++ fair-D4 reference whose cohort numbers the docs quote.

`Mdx.tsx` sets `blockJS: false` on next-mdx-remote because repository-authored
MDX needs JSX expression props; do not re-enable the default, which silently
strips every `{...}` attribute. It also runs `rehype-slug` so headings get the
ids `extractHeadings` predicts.

## Charts

Research figures are JSON specs under `web/content/figures/<name>.json`
rendered by `<Figure name caption />` through the kit in
`web/components/charts/` (`ResearchChart` dispatches on `spec.kind`). The spec
contract is `web/lib/charts/spec.ts`: every point carries or inherits a
`sourceRecord`, a histogram carries pre-binned counts, a strip carries
recorded markers, and the chart computes only pixel positions, axis ranges and
tick values. Series colours are `--color-series-1..8` in fixed order, never
cycled; tooltips are value-first; each chart is one tab stop with arrow keys;
every chart has a table view. `node web/scripts/check-figures.mjs` validates
specs and provenance across `approaches/`, `web/content` and `web/app`;
`node --experimental-strip-types --test web/lib/charts/*.test.ts` runs the
library tests.

## The daily research log

`web/content/log/YYYY-MM-DD.mdx` is one entry per calendar day, rendered at
`/log` and `/log/<date>`. **The filename stem is the canonical date**; routing
never reads the frontmatter `date`. Everything except `date:` and `title:` in
the frontmatter is optional.

```yaml
---
date: 2026-08-21
title: The search plateau
summary: "One or two direct sentences shown on the index."
contributors: [claude-opus-5 (Claude Code), kimi-k3 (OpenCode)]
tags: [fair-planner, depth]
outcomes: { negative: 5, positive: 1, open: 2 }
---
```

**Always quote `summary:` and `title:`.** A colon-space anywhere in an unquoted
scalar makes YAML read it as a nested mapping and the build fails with
`incomplete explicit mapping pair`.

Six components are registered for log MDX (`web/components/ResearchLog.tsx`):

| Component | Props |
| --- | --- |
| `Finding` | `title`, `kind?` = positive/negative/neutral/open, `metric?` |
| `DeadEnd` | `title`, `cost?`, `verdict?` = `closed` \| `configuration-rejected` |
| `Direction` | `title`, `status?` = proposed/running/blocked/closed, `owner?` |
| `ArmTable` | `columns={[{key,label?,numeric?,delta?}]}`, `rows={[{…,highlight?}]}`, `caption?` |
| `Timeline` | `entries={[{time,text,kind?}]}`, `caption?` |
| `LogQuote` | `who?` |

`ArmTable` cells are **pre-formatted strings**; the component parses no
quantity. The log reports; it does not measure. Record what failed as
prominently as what worked, say who did what, and write corrections to the
project's own earlier claims explicitly. Only the coordinator merges a day that
several contributors worked on.

## Adding a record type

1. Add the interface and a `listJsonRecords<T>("<subdir>")` helper to
   `web/lib/repo.ts`, mirroring `TheoryRecord`.
2. Add `web/app/<name>/page.tsx` as a server component that calls it, using
   `PageHeader`, `RecordTable` and `ArticleLayout`.
3. Add a nav or footer entry in `web/components/Header.tsx` / `Footer.tsx`.
4. Render a **missing or empty** state. Records are optional on any checkout.

Keep pages server components; the only client islands are the game, the
chart frame, the search field, `PlayOnView`, `OpenOnHash` and the header menu.
If a page needs something the repo does not contain, the fix is to write that
artifact to the repo, not to add a fetch.

## Rules that matter here

**Never invent or interpolate research numbers.** The console's only job is to
display what the repository actually records. Do not compute a mean, fill a gap,
extrapolate a trend, or default a missing value to zero. Counts of pages and
files are fine; a comparison-table cell without a record reads "not recorded".

**Carry evidence qualifiers into the UI.** Records distinguish run validity
(`valid`/`partial`/`invalid`), scientific outcome (`pass`/`fail`/`inconclusive`/
`not-applicable`), and evidence tier. A result that is `valid + fail` is a
completed contribution and must not be styled as an error. Use `Badge
kind/value` and keep the repository's vocabulary. Never relabel a tier or
promote a number to a stronger claim than its record supports.

**Agent context goes behind the accordion.** Record IDs, seed ranges,
commands, paths, gate tables, protocol excerpts and "what an agent needs to
extend this" text live inside `AgentContext` or `TechnicalRecord`, with a
summary line that says in plain words what is inside.

**Scripted-round results are not research evidence.** `/leaderboard` shows the
`src/bench/` playground: 8 fixed rounds, no seed lease, heavy-tailed scores.
See `drop7-benchmark-playground`.

**The information boundary is user-visible.** Policies flagged
`publicInformation: false` read state a legal policy may not. Keep the "extended
state" badge visible wherever such a policy appears; approach pages carry the
`reads` badge.

**Do not commit generated output.** `web/data/` and `web/.next/` are gitignored.

**Do not add analytics beyond the existing first-party page-view tracker,
telemetry, or any external network call.** This is a local research console; it
must work fully offline.

## Before you finish

```sh
cd web && npx tsc --noEmit -p tsconfig.json
cd web && npm run lint
cd web && npm run build       # must pass; catches server/client boundary errors
cd web && npm test            # browser-solver parity gates and lib tests
node scripts/check-approach-frontmatter.mjs          # from the repo root
node web/scripts/check-mdx.mjs approaches web/content
node web/scripts/check-figures.mjs
web/scripts/check-routes.sh http://localhost:7777    # against a running server
npm test                      # repo tests, from the root
```

Then run the writing-skill checklist on any prose you wrote, and check the
console renders on a checkout with **no** `web/data/`, an empty
`research/results/`, and **no** `web/content/log/`.
