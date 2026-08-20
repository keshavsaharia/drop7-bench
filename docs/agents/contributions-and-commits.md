# Contributions, validity, and commits

This repository attributes work without confusing effort, authorship, and
scientific evidence. Each model or human records its own concrete contribution;
result validity is evaluated separately.

## Four independent dimensions

| Dimension | Examples | What it answers |
| --- | --- | --- |
| Lifecycle | draft, preregistered, running, completed, paused, superseded | Where is the work? |
| Run validity | valid, partial, invalid | Did the frozen procedure execute correctly? |
| Scientific outcome | pass, fail, inconclusive, not-applicable | Did the gate support this exact claim? |
| Evidence tier | proposal through final confirmation | How strong and independent is the evidence? |

A correct run that falsifies its theory is `valid` and `fail`. An implementation
can be `completed` while its validity remains `untested`. “Validated” without a
tier and artifact is not an acceptable status.

## Theory assessments

Use one of these scoped labels:

- `untested`;
- `supported-as-tested`;
- `not-supported-as-tested`;
- `mixed`;
- `superseded`; or
- `invalidated-by-methodological-error`.

Every assessment cites immutable experiment/run IDs and describes what remains
outside its scope. Never write “neural networks do not work” when one network,
dataset, and gate failed.

## Contribution levels

Contribution level is a compact scope label, not a percentage or a ranking of
intelligence:

| Level | Meaning |
| --- | --- |
| `L0` | Executed or observed an existing workflow without a material change |
| `L1` | Review, documentation, mechanical support, or a small diagnostic |
| `L2` | Substantive bounded implementation, analysis, or validation |
| `L3` | Primary author of a named theory, protocol, implementation, or result |
| `L4` | Coordinated and integrated multiple independently attributed contributions |

`L4` does not carry stronger scientific evidence than `L2`; only experiment
design and results do. A coordinator that delegated implementation records
orchestration/integration, while the implementing model keeps its own record.

## Contribution roles

Use any applicable CRediT-inspired roles:

- `conceptualization`, `methodology`, `software`, `validation`, `investigation`;
- `formal-analysis`, `data-curation`, `compute`, `visualization`;
- `writing`, `review`, and `orchestration`.

For each role, record `supporting`, `substantial`, or `lead`. Include concrete
theory, experiment, run, file, artifact, test, and commit references. Also state
limitations and whether the record is self-reported or reviewer-confirmed.

Record the exact platform/model string exposed by the host. If either is not
available, use `unknown`; do not infer it from style or capabilities. Do not
store private chain-of-thought. A concise decision summary, input references,
and tested outputs are the auditable contribution.

## Commit convention

Git may not yet be initialized in this checkout. Do not invent commit hashes or
initialize a repository automatically. Once Git is available and the active
user/platform workflow permits commits, use one coherent change per commit.

Recommended subjects:

```text
theory(afterstate): define successor-closure claim
experiment(afterstate): freeze H40 protocol
benchmark(d4): record machine scaling profile
result(afterstate): record standard-tier failure
infra(research): add deterministic result validator
fix(engine): preserve reveal parity in packed transition
docs(roadmap): reprioritize GPU leaf evaluation
```

Use the configured human or bot as the Git author. Attribute models through
contribution records and trailers:

```text
Theory-ID: TH-20260820-afterstate-a1b2c3d4
Experiment-ID: EX-20260820-h40-83e712aa
Run-ID: RUN-20260820T184215Z-91b02c33
Result-ID: RS-20260820T190501Z-1e7a4c02
Contribution-ID: CT-20260820T184300Z-f482ab19
Evidence-Change: implemented-to-smoke-tested
Result-SHA256: 0123456789abcdef...
```

Omit inapplicable trailers or write `none`; never manufacture an ID. A result
commit adds evidence instead of rewriting the earlier implementation commit's
historical status. Multi-model work lists each contribution ID. The root
`.gitmessage` is a local template; using it does not change global Git config.

Validate a prepared message before committing:

```sh
python3 .agents/skills/million-point-research/scripts/researchctl.py \
  commit-lint .git/COMMIT_EDITMSG
```

The linter requires type-specific theory/experiment/run/result trailers and at
least one contribution ID. It does not create a commit or change Git settings.

## Review responsibilities

Before a result is promoted, a contributor other than the primary runner should
verify:

- the source, binary, model, data, protocol, and result hashes;
- that the run did not read a disallowed cohort;
- that all games and failures are present in canonical order;
- the metric and confidence-bound arithmetic;
- that `partial`, `invalid`, and censored are not conflated;
- that model attribution matches actual artifacts; and
- that the final prose uses the narrowest supported evidence label.

The machine-readable schema and templates are under [`research/`](../../research/).
