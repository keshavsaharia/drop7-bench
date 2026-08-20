# Contributing to the Research

Contributions should make an experiment easier to understand, reproduce, or
falsify. A higher score on an already inspected seed range is not sufficient
evidence by itself.

## Source layout

- Put shared, policy-independent rules in `src/core/`.
- Put an experiment in `approaches/<family>/<approach>/`.
- Use short purpose names; repository filenames do not repeat the project name.
- Keep generated checkpoints and large corpora out of source directories.
- Retain only small durable artifacts under `artifacts/`, with provenance in the
  corresponding documentation.

Comments should explain what a function, constant, constraint, or executable
does. Put measured outcomes, rejected hypotheses, and follow-up ideas in the
research documents rather than source commentary.

## Before a change

Read [methodology](docs/methodology.md) and
[provenance](docs/provenance.md). If a source is covered by a frozen protocol,
create a new protocol version instead of editing old hashes or presenting the
new bytes as the frozen candidate.

Research tasks also follow [`AGENTS.md`](AGENTS.md) and the
[`million-point-research` skill](.agents/skills/million-point-research/SKILL.md). Create a
machine-readable theory and experiment record before opening controlled data.
Use one namespaced `runs/<run-id>/` directory; do not add new shared `/tmp`
defaults.

## Verification

Run:

```sh
make research-validate
make test
```

For a C++ experiment, also compile the exact translation unit with strict
warnings. For a stochastic policy change, preserve per-game paired output and
record the seed role, work limits, censoring, runtime, and memory use.

## Documenting an experiment

Add or update:

1. a theory record in `research/theories/`;
2. a preregistered experiment in `research/experiments/`;
3. a run and result record, including negative or interrupted outcomes;
4. one contribution record per human/model in `research/contributions/`;
5. the entry in `docs/research/experiment-index.md` and chronological ledger;
6. the current-status or strategy summary only if the conclusion changed; and
7. a versioned protocol before opening data whose role is protected by a gate.

State clearly whether a number was reproduced, ledger-recorded,
protocol-recorded, task-record only, or proposed.

Use [`docs/benchmarks.md`](docs/benchmarks.md) for cohort tiers and required
metrics. Use
[`docs/agents/contributions-and-commits.md`](docs/agents/contributions-and-commits.md)
for contribution levels and commit trailers. If Git metadata is absent, record
that fact and do not invent a commit or initialize a repository automatically.
