---
name: drop7-writing-style
description: The one voice for every page under web/content, every approaches/**/README.mdx, the research log, and site copy in web/app. Read before writing or editing any prose, title, caption, or log entry that a visitor will see.
---

# Drop7 site voice

The reader has never played Drop7 and has no background in search or machine
learning. Every page is written for that reader first. The technical record
stays on the page, one click down, inside an agent-context accordion.

Assume a human will read the result out loud. Content and ideas matter more
than polish; a plain sentence that says what happened beats a clever one.

## Voice

- Plain, concrete, curious. Say what happened and let the numbers carry the
  weight. Describe the thing; if it matters, the facts will show it.
- Write like a person explaining something they find interesting to a
  friend. Contractions are fine. Vary sentence length. A short sentence after
  a long one reads like a person.
- Prefer the specific to the abstract: "clears 1.973 discs per move where
  2.4 are needed" over "runs a structural deficit".
- Repeating a word is fine. Don't cycle through synonyms.
- Never brag or inflate. "The evolved leaf beat its warm start by 35,375
  points on 64 games and lost to the hand-written leaf by 106,964" is the
  whole sentence. No "remarkably", no "decisively".
- Use plain copulas. "Is" and "has" beat "serves as", "stands as",
  "features", "functions as".
- A negative result is written as a completed contribution: what was ruled
  out (the exact configuration) and what was not.

## Banned

- Em dashes. Use a comma, a period, a colon, or parentheses. Do not fix an
  em dash with search-and-replace; rewrite the sentence.
- "It is not X, it is Y", "not X but Y", "X, not Y" used as a verdict, and
  every cousin ("a direction, not a magnitude", "a ceiling is not a
  curriculum"). Say what it is.
- Status-quo framing ("most policies do X; this one does Y") unless the page
  is explicitly a comparison.
- Meta-commentary about the prose: "the whole point", "the whole story",
  "in one picture", "in one sentence", "worth understanding", "worth
  quoting", "the interesting part", "this is the useful part".
- One-line verdict paragraphs ("It lost.", "Nothing was retained.") as the
  opener of every "What happened". Once on the whole site is fine.
- Intensifiers and stance words: simply, genuinely, precisely, emphatically,
  decisively, strikingly, dramatic, textbook, famously.
- Sentences ending in a significance clause ("which is why this matters",
  "highlighting the importance of", "reflecting its enduring relevance").
- "Additionally", "Furthermore", "Moreover", "It is worth noting", "In
  conclusion", "Overall".
- Stock vocabulary: delve, dive into, tapestry, landscape (abstract),
  vibrant, seamless, robust (except inside a quoted protocol name),
  leverage, foster, showcase, meticulous, pivotal, crucial, testament,
  underscore (verb), intricate, comprehensive, game-changer, journey.
- Rule-of-three lists for rhythm ("no protocol, no artifact, no number").
- False ranges ("from hobbyists to professionals") and vague attribution
  ("many experts say", "research shows"). Name the source or drop the claim.
- Bold for emphasis mid-prose, bold-led paragraphs, and "**Term:**
  definition" bullet stacks. Bullets are for a genuine list only.
- Witty, punning, or question titles. Titles and section headings are plain
  noun phrases.
- Talking about "the repository" in visitor-facing prose. Write "this site",
  "the record", "the ledger", or name the document. Inside an agent-context
  accordion, repository paths are fine.

## Titles and headings

- A page title is a plain noun phrase that names the idea: "Separate reveal
  and next-disc sampling", not "One dial was controlling two different
  pieces of luck". No question marks, no colon-explainers.
- Section headings on an approach page are exactly these, in this order,
  each once: The problem; Proposed solution; How it works; What happened;
  What we learned.
- Primer and concept pages use the same plain register: The idea; A small
  example; How it works; In Drop7; What it cannot do.
- Index-page section eyebrows are short labels ("The ideas", "Where the
  evidence stands"), never slogans.

## Numbers and evidence

- A number appears on a page only if it already exists in a retained
  record: `research/results/*.json`, `docs/research/history.md`,
  `docs/research/experiment-index.md`, `docs/exploratory/*.md`, or a
  `PREREGISTRATION.md` or finding in the approach directory. Quote it as
  recorded. Never compute a new mean, round into a stronger claim, pool two
  cohorts, or fill a gap.
- Every number travels with its cohort size and its source: "+39,105 points
  on 64 paired development games (finding-08)". A number without a cohort
  does not appear in visible prose.
- At most three numbers in a visible "What happened". Everything else goes
  in a table inside the agent-context accordion.
- Use the label vocabulary exactly and only.
  status: completed | rejected | runtime-paused | preregistered |
  support-only | proposal | unknown.
  evidence: ledger-recorded | task-record only | repository-verified |
  reproduced | none.
  reads: public | oracle | teacher | diagnostic.
  tier: CHECK | PILOT | SCREEN | STANDARD | QUALIFY | PROTECTED | FINAL,
  plus the finding tiers exactly as the finding states them.
  run validity: valid | partial | invalid.
  outcome: pass | fail | inconclusive | not-applicable.
  Never invent a value ("machine-readable records", "exploratory ·
  engineering result") and never soften one ("retired" for rejected). If a
  page needs to say more, it says it in a sentence, not in the label.
- Say "task-record only" every time such a number appears.
- Historical 7,000-point scoring is labelled "historical 7,000-point
  scoring, archival" on first appearance, and those numbers are never
  compared with corrected 17,000-point results in the same sentence or
  table.
- An oracle's or teacher's score never sits in the same table as a
  policy's.
- Cohort-size caveats are stated, not smoothed: "8 games is a pilot", "the
  lower bound clears zero by 1,138 points".
- "Mean" and "one game" are never confused. A single game is an anecdote.

## Structure of an approach page

1. Title (plain), the one-sentence deck from frontmatter `summary`
   (rendered once by the page, never repeated in the body), and the three
   chips (status, evidence, reads).
2. The technique strip: one paragraph and the technique's card figure,
   linking to the primer under `/learn/techniques/`.
3. The problem: what is wrong or unknown before this work, in the game's
   terms.
4. Proposed solution: the bet, the mechanism in one engine-generated
   figure, the information boundary in one sentence.
5. How it works: numbered steps naming inputs, computation, output.
6. What happened: the result sentence with cohort and label, then up to
   three numbers.
7. What we learned: prose, not bullets; ends with the one open question.
8. Agent-context accordions (`<AgentContext>`, bot icon), fixed titles in
   this order: Records and provenance; Full results table; Validity, gates
   and limitations; Source files; Scoring mode. They carry everything the
   page removed from view, so nothing is lost for an agent.

## Terms and links

- Define a term on first use in plain words, then link it to the glossary
  or a concept page: "a chance node (where the game deals a disc; the
  search averages)". After that, use the term without re-explaining.
- Familiar words before abbreviations: "the depth-4 search with seven
  chance samples", not "d4s7". D4, NNUE, PPO, MCTS, CEM appear only after
  their long form on that page.
- Every number that comes from a record links to the record page.

## Figures and captions

- A figure earns its place only if it shows the mechanism. No decorative
  diagrams.
- Boards are engine output (a script under `web/scripts/` using the
  TypeScript engine, or a recorded position). Never hand-draw a board.
- Captions are complete sentences that say what the reader is looking at
  and where it came from. The rules page captions are the model.

## The research log

The log follows the same voice. It reports what was tried and what failed
as plainly as what worked, names who did the work, and quotes only numbers
that already sit in a result record or run artifact. A withdrawn claim is
written down as a withdrawn claim.

## Checklist (run against a page before it ships)

- [ ] `grep -c '—' <file>` is 0.
- [ ] No "not X, it is Y" verdicts: `grep -iE 'is not [^.]*[,;:] it is'` finds nothing.
- [ ] No banned words: `grep -iwE 'simply|genuinely|precisely|emphatically|decisively|robust|leverage|delve|pivotal|crucial|testament|whole point|whole story|worth noting|in this repository|the repository'` finds nothing outside accordions and quoted protocol names.
- [ ] `##` headings are only the fixed titles, in order, each once.
- [ ] No line starts `**` and no bullet starts `- **`.
- [ ] Title is a plain noun phrase; no question mark, no colon-explainer.
- [ ] Frontmatter status, evidence, and reads come from the closed lists.
- [ ] Every number has a cohort size and a source link; at most three are visible in "What happened".
- [ ] Every abbreviation follows its long form on this page.
- [ ] The body does not repeat the summary sentence.
- [ ] The information boundary is stated once, in one sentence, with the chip.
- [ ] Task-record-only, historical-scoring, and oracle numbers are labelled at every appearance.
- [ ] Figures are engine output or record data; captions are sentences.
- [ ] Agent-context accordions use the fixed titles and hold everything the page removed from view.
- [ ] `node web/scripts/check-mdx.mjs <file>` passes.
