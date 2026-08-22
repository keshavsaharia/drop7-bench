# `./do-research` — autonomous research loop

`./do-research` turns the repository's research contract (`AGENTS.md`) into a
loop a machine can run unattended:

```
plan → critique → register → implement → review → assess → (repeat)
```

| Stage | Who runs it | What it produces |
| --- | --- | --- |
| plan | the planner model (see backends) | one preregisterable proposal as JSON, or an abstention |
| critique | `opencode run` with a second model family (Kimi K3 by default) | `critique.md`; the planner revises once |
| register | the orchestrator, through `researchctl.py` | a theory record, an experiment record, seed leases from the allocator |
| implement | `claude -p` headless, hard-capped | code under `approaches/<family>/<slug>/`, CHECK gates, pilot, the run, run/result/contribution records, a research-log entry |
| review | `opencode run`, following `.agents/skills/audit-drop7-experiment` | `review.md` |
| assess | the planner | continue / follow-up / stop |

Everything for one session lives under `runs/orchestrator/ORCH-…/` (brief,
proposals, logs, reports, reviews, `state.json`). Nothing is committed unless
`--commit` is given; nothing is pushed unless `--push` is given too.

## Run it

```sh
./do-research                                   # 3 iterations, 10 wall-hours each, plan with the claude CLI
./do-research --focus "neural leaf evaluators with tunable hyperparameters"
./do-research --max-iterations 1 --dry-run      # plan + critique only, no implementation
./do-research --commit --push                   # commit each validated iteration and push the current branch
./do-research --resume ORCH-20260822T030000Z-1a2b3c4d
```

Options: `--max-iterations N` (3), `--wall-hours H` per iteration (10),
`--cpu-threads T` (30), `--max-usd U` implementer dollar cap per iteration
(40), `--planner-usd` (3), `--max-turns` (400), `--implementer-model`,
`--reviewer-model` (`baseten/moonshotai/Kimi-K3`), `--focus "<direction>"`,
`--skip-critique`, `--skip-review`, `--dry-run`, `--commit`, `--push`,
`--resume <session>`.

## Planner backends

| `DO_RESEARCH_PLANNER` | Needs | Model (`DO_RESEARCH_MODEL`) |
| --- | --- | --- |
| `claude-cli` (default) | a logged-in `claude` CLI | `claude-opus-5` |
| `anthropic` | `ANTHROPIC_API_KEY` or an `ant auth login` profile; `npm install` (the SDK is a devDependency) | `claude-opus-5` |
| `openai` | `OPENAI_API_KEY`, optional `OPENAI_BASE_URL`, `OPENAI_MODEL` | any OpenAI-compatible chat model |

The implementer is always the `claude` CLI; the critic/reviewer is always
`opencode` so that a different model family checks the work.

## What the loop can never do

- Open a protected (`0x7d……`) or final (`0xd7……`) seed: the allocator refuses
  those prefixes, and every grant is checked against every lease record and
  every eight-hex-digit constant in the tree before it is written to
  `research/seeds/leases/` and `docs/exploratory/lease-map.md`.
  The pool it draws from is `DO_RESEARCH_LEASE_POOL_START`–`…_END`
  (default `0xa52c0000`–`0xa5300000`, inside the reserve the lease map lists
  as unallocated).
- Skip preregistration: the implementer must freeze the experiment record
  before reading a leased seed, and `make research-validate` must pass before
  an iteration is committed.
- Tune on an already-read cohort, edit frozen protocols, or promote a tier:
  these are written into the implementer's instructions and are checked by
  the independent review.
- Spend without limit: turns, dollars and wall-clock are capped per
  iteration; the loop stops after two consecutive failed iterations, when the
  planner abstains, when the assessor says stop, or when a `STOP` file
  appears in the session directory.

The loop is a coordinator, not an oracle: a `fail` result is a finished
contribution and is recorded as prominently as a pass.
