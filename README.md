# Drop7 Strategy Research

This repository studies a simple question with a difficult answer: **given the
board that a Drop7 player can see, which column should receive the next disc?**
The long-term goal is a public-information policy that averages more than one
million points in Hardcore mode. No policy in this repository has met that
standard yet.

## Drop7 in one minute

Drop7 is played on a 7-by-7 board. Each falling disc has a number from 1 to 7.
That number is a rule: for example, a 3 disappears when it sits in an unbroken
horizontal or vertical group of exactly three occupied cells. The discs above
it fall, which can make more numbers disappear and start a chain reaction.

Gray discs hide numbers and need two neighboring hits to open. After every five
moves, a new covered row rises from the bottom. The game ends when no column
can accept another disc.

The basic choice is easy to state: pick one of seven columns. A move can change
what happens many row rises later, and it must account for numbers that have not
been revealed yet. That combination makes the strategy problem much harder than
it first appears.

## What the research is allowed to know

A deployable policy may use only:

- the 49 visible board cells;
- the next numbered disc;
- the number of moves before the next row rises; and
- whether the game is over.

It may not use the random seed, future discs, future gray-disc reveals, the
score, the level, the absolute move number, or hidden game history. Some
experiments deliberately use future information as teachers, but those are
training tools, not legal playing policies.

## Current answer

Completed fair depth-4 expectimax, called **fair D4** in the research notes, is
the strongest dependable reference policy found so far. It looks four decision
layers ahead and averages representative outcomes for hidden reveals instead
of pretending that the best or worst reveal is guaranteed.

The expensive historical runs have not been rerun as part of this repository
cleanup. The figures below are therefore ledger-recorded results, while the
engine tests and parity checks are directly reproducible here.

| Result | Games | Mean score | Mean moves | Interpretation |
| --- | ---: | ---: | ---: | --- |
| Fair D3 | 8 | 235,071.25 | 71.000 | Comparison policy |
| Fair D4 | 8 | 400,675.25 | 116.375 | Won 7 of 8 paired games |
| Fair D4 reference cohort | 64 | 308,295.578 | 90.031 | Broader development baseline |

These results are well below the one-million-point average target. The frozen
record says that no candidate qualified for protected validation and that the
protected and final seed cohorts remain unopened. A single 1,246,684-point D4
game appears in the referenced task record, but one unusually good game is not
evidence of a million-point average.

See [research status](docs/research/status.md) for the evidence trail and
[methodology](docs/methodology.md) for the evaluation rules.

## Strategy families

- **Heuristics:** score visible board features such as height, holes, exposed
  numbers, and immediate chains. They are fast, but a hand-written score can
  miss consequences far in the future.
- **Expectimax:** look ahead through both player choices and random reveals.
  This is the strongest current reference, but its cost grows quickly with
  depth.
- **Rollouts and tree search:** play many short imaginary futures and compare
  their outcomes. These methods need careful chance sampling and strong
  continuation play.
- **Learned value and policy models:** learn which public states or actions are
  promising from examples. Many models predicted played states reasonably but
  ranked the unplayed sibling actions poorly.
- **Constructive and reservoir policies:** deliberately build reusable chain
  structures over several row-rise cycles. The tested versions found useful
  long-horizon signals but did not beat fair D4 reliably.
- **Oracle-guided methods:** use hidden future information only to create
  training labels, then train a public-state student. The student must still
  prove that it works without privileged inputs.

The full comparison, including ideas that remain worth trying, is in
[strategy landscape](docs/strategies.md). Every executable is indexed in the
[experiment index](docs/research/experiment-index.md).

## Continue the research with a coding agent

The repository includes one platform-neutral research contract and reusable
skills for autonomous work. Codex and OpenCode read [`AGENTS.md`](AGENTS.md)
directly; Claude Code loads the same file through [`CLAUDE.md`](CLAUDE.md).
For a goal such as “work toward the million-point policy,” the agent is required
to use the [`million-point-research` skill](.agents/skills/million-point-research/SKILL.md),
register falsifiable work, advance through bounded benchmark tiers, and keep
protected/final data sealed until the protocol permits it.
Independent claims are checked with the separate
[`audit-drop7-experiment` skill](.agents/skills/audit-drop7-experiment/SKILL.md)
so verification is not mixed with candidate repair.

Start a session from the repository root and describe the goal in your own words.
The agent's first safe checks are:

```sh
make research-validate
make research-doctor
make test
```

The [agent handbook](docs/agents/README.md) explains orchestration across Codex,
Claude Code, and OpenCode. The [research roadmap](docs/research/roadmap.md)
prioritizes action-complete multi-cycle learning, exact-search acceleration,
human-style rise-cycle structure, and GPU-batched neural evaluation. The
[benchmark contract](docs/benchmarks.md) standardizes strength, runtime, memory,
GPU, and evidence reporting.

## Research console (web app)

A local Next.js console renders the research program: theories, experiments,
approach documentation (MDX), and a deterministic scripted-round leaderboard.

```sh
npm run bench   # run policies over the scripted Gauntlet rounds (deterministic)
npm run web     # serve the console at http://localhost:3000
```

The benchmark uses scripted rounds (`drop7-scripted-round-v1`): the visible
disc sequence is fixed by move number and every gray disc carries a
predetermined hidden value, so policies face identical randomness. Policies
are wrapped through the D7P policy protocol — see
[docs/d7p-protocol.md](docs/d7p-protocol.md). Scripted rounds are a public
playground, not a research tier; they consume no seed lease and support no
qualification claim.

## Reproduce the core checks

Requirements are Node.js 22.6 or newer, Python 3.10 or newer, a C++20 compiler,
and `make`.

```sh
npm test
make test-native
make parity
```

Or run all three groups together:

```sh
make test
```

To compile a current, corrected-score standalone C++ experiment:

```sh
make experiment SOURCE=approaches/tree-search/puct/puct.cpp
```

The experiments are intentionally standalone translation units, so a long
training run is never hidden inside the normal test command. Some archived
7,000-point experiments intentionally refuse to compile against the corrected
17,000-point engine. See [reproducibility](docs/reproducibility.md) before
interpreting or resuming one.

## Project map

```text
src/core/                 Shared TypeScript and C++ game engines
src/bench/                Scripted-round benchmark, D7P policy protocol, registry
approaches/               One directory per strategy or diagnostic approach
web/                      Next.js research console (MDX docs, leaderboard, replays)
artifacts/models/         Small, retained model artifacts
artifacts/protocols/      Frozen validation and experiment records
artifacts/results/        Promoted compact run evidence
docs/research/            Current status, experiment index, roadmap, and ledger
docs/strategies.md        Strategy comparison and future research avenues
docs/methodology.md       Rules for fair, leakage-resistant evaluation
docs/benchmarks.md        Policy-quality and systems benchmark contract
docs/agents/              Portable autonomous-research handbook
docs/hardware/            Machine-specific acceleration guidance
docs/reproducibility.md   Build, test, and experiment instructions
docs/provenance.md        Evidence sources and historical hash limitations
research/                 Schemas, templates, and machine-readable records
.agents/skills/           Portable research and independent-audit skills
```

The detailed [experiment history](docs/research/history.md) is an archival lab
ledger. Start with this README, the status report, and the strategy landscape;
use the ledger when exact configurations, seed ranges, or recorded metrics are
needed.

## Research integrity

This project separates three kinds of evidence:

1. **Reproduced here:** tests, builds, and simulator parity executed in this
   checkout.
2. **Recorded:** results preserved in the detailed experiment ledger and frozen
   protocols, but not rerun during this reorganization.
3. **Task-record only:** observations found in the referenced research task
   without a retained result artifact; these are labeled as anecdotal.

Source files were relocated and given purpose-focused names and comments.
Historical byte hashes were not rewritten to make them appear current. Read
[provenance](docs/provenance.md) before using a SHA-locked experiment in new
research.

Contribution and experiment-recording conventions are in
[CONTRIBUTING.md](CONTRIBUTING.md).
