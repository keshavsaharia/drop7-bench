# Agent research handbook

This handbook turns a broad instruction such as “work toward the million-point
Drop7 policy” into a reproducible research program. It is designed for Codex,
Claude Code, OpenCode, and agents that can only read repository files.

## One contract, three discovery paths

The authoritative always-on rules are in [`AGENTS.md`](../../AGENTS.md). The
portable research workflow is in
[`million-point-research/SKILL.md`](../../.agents/skills/million-point-research/SKILL.md).

| Host | Rules loaded | Research skill |
| --- | --- | --- |
| Codex | Root `AGENTS.md` | Discovers `.agents/skills/million-point-research` |
| Claude Code | `CLAUDE.md` imports `AGENTS.md` | Root rule tells Claude when to read the canonical skill |
| OpenCode | Root `AGENTS.md` takes precedence | Discovers `.agents/skills/million-point-research` |
| Other agent | Read `AGENTS.md` manually | Read the skill when its description matches |

This layout follows the current official documentation for
[Codex project instructions](https://developers.openai.com/codex/guides/agents-md/),
[Codex skills](https://developers.openai.com/codex/skills/),
[Claude Code project memory](https://code.claude.com/docs/en/memory), and
[OpenCode rules](https://opencode.ai/docs/rules/) and
[skills](https://opencode.ai/docs/skills). The skill uses the portable
[Agent Skills specification](https://agentskills.io/specification) and avoids
host-specific frontmatter or tool names.

Some hosts discover project files by walking to a Git worktree. This checkout
may not contain `.git` metadata. Until it does, launch the agent from the
repository root. Do not let an agent initialize Git merely to satisfy discovery;
repository creation is an owner decision.

Agent hosts may cache their available-skill catalog for the lifetime of a
session. After adding, removing, or renaming a skill, start a fresh session
before testing discovery; an already-running agent can retain the old path even
when every current-tree reference is correct.

## What a high-level directive authorizes

A research directive authorizes agents to inspect, plan, implement, run bounded
checks on data they are allowed to use, and record results. It does not
implicitly authorize:

- opening protected or final seed cohorts;
- changing a frozen protocol after seeing its controlled data;
- installing system drivers or changing BIOS, firmware, kernel, GTT, or TTM;
- purchasing compute, publishing externally, or changing remote services;
- destructive Git or filesystem operations; or
- an unbounded job with no memory, time, or failure stop.

The coordinator should continue through a sequence of small, falsifiable stages
until the target is met, a stated budget expires, or one of those authority
boundaries is reached.

## The documents an agent uses

| Need | Read |
| --- | --- |
| Current best policy and unresolved claims | [`research/status.md`](../research/status.md) |
| Full catalog of tried strategies | [`strategies.md`](../strategies.md) |
| Prioritized future program | [`research/roadmap.md`](../research/roadmap.md) |
| Game, information, seed, and statistical rules | [`methodology.md`](../methodology.md) |
| Standard comparison and performance tiers | [`benchmarks.md`](../benchmarks.md) |
| Campaign state machine and parallel ownership | [`orchestration.md`](orchestration.md) |
| Model attribution and commit labels | [`contributions-and-commits.md`](contributions-and-commits.md) |
| AMD Ryzen Halo and ROCm plan | [`hardware/amd-ryzen-halo.md`](../hardware/amd-ryzen-halo.md) |
| Machine-readable records | [`research/README.md`](../../research/README.md) |
| Exact historical configurations | [`research/history.md`](../research/history.md) |

## Quick start for an orchestrator

```sh
make research-validate
make research-doctor
make test
python3 .agents/skills/million-point-research/scripts/researchctl.py new theory \
  --slug action-complete-afterstates \
  --title "Action-complete afterstates improve long-horizon sibling ranking"
```

`research-doctor` prints a machine profile and does not alter the system.
`new` writes a draft record with a cryptographically generated ID; the agent
must complete and validate it before opening any new gameplay data.

## What “done” means

Software may be complete while its theory remains untested. A research handoff
must separately report:

- what was implemented;
- what was mechanically verified;
- which data was read and what role it had;
- whether the run itself was valid, invalid, or partial;
- whether the frozen scientific gate passed, failed, or was inconclusive;
- what evidence tier the claim reached;
- what remains unknown; and
- which model/human contribution records and commits cover the work.

That separation lets a future agent resume the program without mistaking code,
a pilot, or an anecdote for a validated strategy.
