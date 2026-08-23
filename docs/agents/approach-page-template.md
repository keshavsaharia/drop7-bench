# Writing strategy and concept pages for the console

The console's reader is **someone who has never played Drop7 and has no
background in search or machine learning**, and who might become a researcher
on this problem if the pages make the ideas clear. Every page is written for
that reader first; technical detail is kept, but one click deeper.

Pages are MDX under `approaches/<family>/README.mdx` (family) and
`approaches/<family>/<approach>/README.mdx` (approach). Concept pages live in
`web/content/learn/concepts/`. Narrative summaries of theory and experiment
records live in `web/content/research/<record-id>.mdx`.

## Page structure

Every approach and family page has these four levels, in order:

1. **One direct sentence.** Say what the idea tries to do without unexplained
   jargon. Use the same sentence in the frontmatter `summary` and body opening.
2. **The intuition, visually.** Why someone thought it would work, on a board.
   Use the figure components (below) or a `BoardCompare` from real positions.
   A figure earns its place only if it shows the mechanism; no decorative
   diagrams. If no figure fits, a concrete worked example in prose.
3. **How it works.** Use numbered steps. Name the inputs (what the
   policy reads), the computation, and the output (a column). Link every term
   to the glossary or a concept page the first time it appears; do not
   re-explain expectimax, strata, leaf, sibling, or oracle. Link to
   `/learn/concepts/...` and `/learn/glossary`.
4. **What happened.** Begin with a direct result a newcomer can understand:
   "On the same 64 games, it scored about a third less than the reference and
   was retired." Then put the
   technical record inside `<TechnicalDetails>`: cohort, tier, numbers, IDs,
   links to `/docs/research/history`, `/docs/research/experiment-index`,
   result records. A negative result is a finished contribution, not an
   embarrassment; say what it ruled out and what it did not.

Then a short **"What this taught us / what is still open"** section.

## Ground rules

- **Only numbers that exist in a retained record.** Sources, in order of
  strength: `research/results/*.json`, `docs/research/history.md`,
  `docs/research/experiment-index.md`, `docs/exploratory/*.md`, a
  `PREREGISTRATION.md` or finding in the approach directory. Quote the number
  as recorded and say where it came from. Never compute a new mean, never
  round a recorded figure into a stronger claim, never fill a gap.
- **Carry the evidence label.** Use the repository's own words: completed,
  rejected, runtime-paused, preregistered, support-only; ledger-recorded,
  task-record only, repository-verified, reproduced; run validity and
  scientific outcome for machine-readable results. A "task-record only" number
  is labelled as such every time it appears.
- **Use familiar terms before abbreviations.** Do not write "DX beat DY at
  d4s7" before explaining the terms. Write "the depth-4 search with seven
  chance samples". Depth,
  strata, leaf, oracle, afterstate, n-tuple, NNUE, PPO, MCTS all exist in the
  glossary. Link the first use, then keep the wording consistent.
- **Do not smooth over limitations.** If the cohort was 8 games, say 8 games
  and that it is a small confirmation cohort. If a run was stopped, say so and
  why. If the only evidence is a conversation record, say the number is
  provisional.
- **Do not invent history.** If the source files exist but the index has no
  row and the ledger no entry, the page says "no retained result" and
  describes what the code does, from the code.
- **Information boundary is always visible.** If an approach reads hidden
  values or the future, the page says so in altitude 1 and labels it an oracle
  or teacher, never a policy result.
- **Never edit research records, frozen protocols, or source code** while
  writing pages. Pages describe; they do not change what they describe.

## Frontmatter

```yaml
---
title: Readable title (not the slug)
family: <family-slug>            # approach pages only
summary: The one-sentence, no-jargon description.
status: completed | rejected | runtime-paused | preregistered | support-only | proposal
evidence: ledger-recorded | task-record only | repository-verified | reproduced | none
reads: public | oracle | teacher | diagnostic   # what the code is allowed to see
---
```

`draft: true` is reserved for machine-generated pages; remove it when you
hand-write the page.

## Components available in MDX

- `Board`, `BoardCompare`, `Disc`, `Stat`, `Callout` (tones: info, warn,
  success) — from the existing kit.
- `TechnicalDetails title="..."` — collapsible technical block.
- `EvidenceLabel status="rejected" evidence="ledger-recorded" reads="public"` —
  the coloured label row; put it under the opening sentence.
- `ResultSummary id="RS-..."`, `ExperimentSummary id="EX-..."`,
  `TheorySummary id="TH-..."` — render a machine-readable record in sentences
  built only from its fields, with a link to the full record page.
- `RulesScenario`, `RunCounter`, `DropPhysics` (rules figures);
  `RootAndChoices`, `ChanceNode`, `ChanceStyles`, `TreeGrowth`, `SiblingTrap`
  (concept figures); the fast-engine figures in `web/components/Engine.tsx`.
- For a new figure, add a server component to `web/components/` and, if it
  needs data, generate that data with a script under `web/scripts/` that uses
  the TypeScript engine, writing JSON under `web/content/`. Never hand-draw a
  board that the engine could have produced.

Check every MDX file compiles before finishing:
`node web/scripts/check-mdx.mjs <file or directory>`.

## Tone

Be curious, concrete, and precise. Use short paragraphs and complete sentences.
Lead with the idea, not the author or the writing process. Prefer "the search
looks four moves ahead" to "the D4 policy". Prefer "it was retired because…"
to "rejected". Use em dashes sparingly. A reader should finish the page able to
explain the idea and judge the strength of its evidence.
