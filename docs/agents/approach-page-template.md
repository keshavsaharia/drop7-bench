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

The voice, the banned constructions and the checklist live in
`.agents/skills/drop7-writing-style/SKILL.md`; read it first. This file
keeps the page skeleton and the component list.

Every approach page has these parts, in order:

1. **Frontmatter.** `title` is a plain noun phrase, `summary` is the one
   direct sentence the page renders as its deck. The body never repeats it.
2. **The technique strip** (rendered by the page, not written by you): the
   technique's card figure and a link to its primer under
   `/learn/techniques/`, chosen from the `technique` frontmatter key.
3. **The problem.** What is wrong or unknown before this work, in the game's
   terms. One engine-generated figure if it shows the mechanism.
4. **Proposed solution.** The bet and the mechanism, the information
   boundary in one sentence (the `reads` chip carries the label).
5. **How it works.** Numbered steps naming inputs (what the policy reads),
   the computation, and the output (a column). Link every term to the
   glossary or a concept page the first time it appears; do not re-explain
   expectimax, strata, leaf, sibling, or oracle.
6. **What happened.** A direct result a newcomer can understand, with the
   cohort size and the evidence label, then at most three numbers. A
   negative result is a finished contribution; say what it ruled out and
   what it did not.
7. **What we learned.** Prose, not bullets; ends with the one open question.
8. **Agent-context accordions.** `<AgentContext summary="…">` blocks with
   fixed titles, in this order: Records and provenance; Full results table;
   Validity, gates and limitations; Source files; Scoring mode. Everything
   the visible page leaves out (record IDs, seed leases, commands, paths,
   gate tables, full arm tables) lives here so an agent loses nothing.

Section headings at `##` are exactly "The problem", "Proposed solution",
"How it works", "What happened", "What we learned", each once. Family pages
use the same headings where they apply and otherwise plain noun phrases.

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
status: completed | rejected | runtime-paused | preregistered | support-only | proposal | unknown
evidence: ledger-recorded | task-record only | repository-verified | reproduced | none
reads: public | oracle | teacher | diagnostic   # what the code is allowed to see
kind: strategy | engine | diagnostic            # family pages: kind: family
technique: <catalogue slug>                     # strategies only
featured: true                                  # optional; strategies only
---
```

`kind` says what the directory is. A `strategy` is a deployable player (or a
bounded correction to one) that can be evaluated through the public
interface. An `engine` is infrastructure that plays no game of its own: a
game engine, a data factory, a training harness. A `diagnostic` is a
measurement of something other than a policy's strength: a parity sweep, a
score decomposition, a benchmark audit. A family `README.mdx` carries
`kind: family` and no `technique`.

`technique` groups strategies on the site and is required for every
`kind: strategy` page; engines and diagnostics carry none. It takes one of
the fourteen catalogue slugs, which are also `TECHNIQUE_ORDER` in
`web/lib/techniques.ts`:

| Slug | Group |
| --- | --- |
| `expectimax` | Expectimax search |
| `heuristic-evaluation` | Heuristic evaluation |
| `q-learning` | Q-learning and value learning |
| `n-tuple` | N-tuple networks |
| `nnue` | NNUE evaluators |
| `policy-gradient` | Policy gradients |
| `evolution` | Evolutionary optimisation |
| `mcts` | Monte Carlo tree search |
| `rollout-policy-iteration` | Rollouts and policy iteration |
| `oracle-distillation` | Oracles, teachers and distillation |
| `risk-survival` | Risk and survival objectives |
| `afterstate` | Afterstates |
| `constructive-planning` | Constructive planning |
| `determinization` | Determinized planning |

`featured: true` marks the one or two best-documented pages of a technique
(a real explanation, engine-generated figures, a result with a record behind
it). It is a curation choice made when the group page is designed; do not add
it to your own page.

`draft: true` is reserved for machine-generated pages; remove it when you
hand-write the page.

`node scripts/check-approach-frontmatter.mjs` validates every approach and
family README against this vocabulary (status, evidence, reads, kind,
technique, featured) and exits non-zero on any value outside it. Run it after
editing frontmatter; `make research-validate` does not cover it.

## Components available in MDX

- `Board`, `BoardCompare`, `Disc`, `Stat`, `Callout` (tones: info, warn,
  success) — from the existing kit.
- `AgentContext summary="..."` — the click-to-open accordion with the bot
  icon for agent-facing detail; `TechnicalRecord summary="..." meta="EX-…"`
  is the same accordion with a ledger icon for cohorts, gates and record
  tables. `TechnicalDetails title="..."` is kept as an alias of
  `TechnicalRecord` for existing pages.
- `TechniqueArt name="q-learning" mode="loop"` — a technique's animated card
  figure (names are the catalogue slugs plus `engine-native`,
  `engine-typescript`, `engine-rust`); the approach page renders it in the
  technique strip, so a page rarely needs it inline.
- Primer figures from `web/components/primers/` (for example
  `ExpectimaxTwoDoors`, `QLearningCorridor`, `NnueGather`) are registered for
  MDX and may be reused on an approach page when the technique needs
  re-explaining there.
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

See `.agents/skills/drop7-writing-style/SKILL.md`. In short: plain, concrete,
curious; no em dashes; no "not X but Y" verdicts; no witty titles; only
recorded numbers, each with its cohort and source; the closed label
vocabulary, never softened ("retired") or invented.
