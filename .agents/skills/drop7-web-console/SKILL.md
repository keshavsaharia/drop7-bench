---
name: drop7-web-console
description: Extend the Next.js research console under web/ — add a page or route, surface a new research record type, render an approach or doc page, add a leaderboard or replay view, or change navigation and styling. Use for any request to show research output in the browser, build a dashboard or visualization, or add to the Drop7 web app.
---

# Drop7 web console

`web/` is a Next.js App Router site that renders this repository as a browsable
research console. It reads the repo directly at build/request time — it has **no
database and no API server**, and it must stay that way.

Follow the repository contract in `AGENTS.md`.

## Run it

```sh
npm run web                   # from the repo root; proxies to web/
cd web && npm run dev         # equivalent
```

Dependencies live in `web/node_modules` and are gitignored, so run
`npm --prefix web install` on a fresh checkout. `web/data/` is gitignored too —
it holds regenerable benchmark output, not source. A checkout with no
`web/data/` is expected; the leaderboard route must degrade gracefully rather
than crash.

## How it reads the repository

Everything goes through `web/lib/repo.ts`. **Use it rather than reaching for
`fs` in a page.**

- `REPO_ROOT` — resolves from `DROP7_REPO_ROOT` or walks up from `web/`. Never
  hardcode a path or assume `process.cwd()`.
- `readRepoFile(relativePath)` — returns file contents or `null`. Always handle
  `null`; the console must render on a checkout where an artifact is absent.
- `listJsonRecords<T>(subdir)` — loads `research/<subdir>/*.json` and stamps each
  with `$id`. Existing typed helpers: `getTheories()`, `getExperiments()`,
  `getResults()`.
- `listFamilies()` / `listApproaches(family)` — walk `approaches/`.
- `web/lib/leaderboard.ts` — `loadLeaderboard()` and `loadReplay(policyId, roundId)`,
  both returning `null` when `web/data/` is absent.
- `web/lib/learn.ts` — prose pages from `web/content/learn/*.mdx`.

## Existing routes

| Route | Source |
| --- | --- |
| `/` | overview |
| `/approaches`, `/approaches/[family]`, `/approaches/[family]/[approach]` | `approaches/**` plus each directory's `README.mdx` |
| `/theories`, `/experiments` | `research/theories`, `research/experiments` |
| `/leaderboard`, `/leaderboard/[policy]/[round]` | `web/data/leaderboard.json`, `web/data/replays/` |
| `/learn`, `/learn/[slug]` | `web/content/learn/*.mdx` |
| `/docs/[...slug]` | `docs/**` markdown |

Components: `Board.tsx` renders a 49-cell position, `ReplayPlayer.tsx` steps
through a replay, `Mdx.tsx` and `Markdown.tsx` render prose, `Badge.tsx` is the
status/flag chip.

## Adding a record type

1. Add the interface and a `listJsonRecords<T>("<subdir>")` helper to
   `web/lib/repo.ts`, mirroring `TheoryRecord`.
2. Add `web/app/<name>/page.tsx` as a server component that calls it.
3. Add a nav entry in `web/app/layout.tsx`.
4. Render a **missing or empty** state. Records are optional on any checkout.

Keep pages server components. There is no data layer to add — if a page needs
something the repo does not contain, the fix is to write that artifact to the
repo, not to add a fetch.

## Rules that matter here

**Never invent or interpolate research numbers.** The console's only job is to
display what the repository actually records. Do not compute a mean, fill a gap,
extrapolate a trend, or default a missing value to zero. If a record lacks a
field, render it as absent. A dashboard that silently makes a number up is worse
than one that shows a blank.

**Carry evidence qualifiers into the UI.** Records distinguish run validity
(`valid`/`partial`/`invalid`), scientific outcome (`pass`/`fail`/`inconclusive`/
`not-applicable`), and evidence tier. A result that is `valid + fail` is a
completed contribution and must not be styled as an error. Use `Badge.tsx` and
keep the repository's vocabulary — never relabel a tier or promote a number to a
stronger claim than its record supports.

**Scripted-round results are not research evidence.** `/leaderboard` shows the
`src/bench/` playground: 8 fixed rounds, no seed lease, heavy-tailed scores. It
must never be presented as a policy-strength ranking comparable to a research
cohort, and the page should say so. See
[`drop7-benchmark-playground`](../drop7-benchmark-playground/SKILL.md).

**The information boundary is user-visible.** Policies flagged
`publicInformation: false` read state a legal policy may not. Keep the "extended
state" badge visible wherever such a policy appears.

**Do not commit generated output.** `web/data/` and `web/.next/` are gitignored.
If a page needs data, generate it with `npm run bench` at runtime.

**Do not add analytics, telemetry, or any external network call.** This is a
local research console; it must work fully offline.

## Before you finish

```sh
cd web && npm run build       # must pass; catches server/client boundary errors
npm test                      # repo tests, from the root
```

Check the console renders on a checkout with **no** `web/data/` and with an
empty `research/results/`. Those are the states a collaborator will actually
clone into.
