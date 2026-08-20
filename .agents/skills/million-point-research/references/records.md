# Records and attribution

The schemas and templates live under `research/`; the normative explanation is
`docs/agents/contributions-and-commits.md`.

## Required separation

- A **theory** states a falsifiable claim and mechanism.
- An **experiment** freezes how one configuration tests that claim.
- A **run** is one execution attempt; it may be valid, invalid, or partial.
- A **result** assesses a valid/partial run as pass, fail, or inconclusive.
- A **contribution** states what one human/model produced and verified.

Do not collapse these into a narrative status field. In particular, a valid
run with a failing gate is not an invalid run.

Use the repository helper:

```sh
python3 .agents/skills/million-point-research/scripts/researchctl.py --help
python3 .agents/skills/million-point-research/scripts/researchctl.py new theory \
  --slug descriptive-name --title "Falsifiable title"
make research-validate
```

IDs contain a cryptographically random suffix. Never hand-reuse an ID. Once a
protocol or promoted result is frozen, changes create a successor record rather
than modifying the evidence in place.

Use `unknown` for an unexposed model or platform identifier. Never guess, store
private reasoning, or assign contribution percentages. Link concrete files,
commands, artifacts, tests, and commits instead.

Before any authorized commit, run `researchctl.py commit-lint` on the prepared
message. A multi-model commit lists every separately recorded contribution ID.
