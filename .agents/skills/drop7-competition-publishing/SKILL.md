---
name: drop7-competition-publishing
description: Publish an existing Drop7 policy result to the AWS competition leaderboard through the validated repository CLI. Use when seeding or importing a policy-score record into the global competition ledger; do not use for research-tier evidence claims.
---

# Drop7 competition publishing

Read `../drop7-benchmark-playground/SKILL.md` first. A competition score is a
scripted-round playground demonstration, never research-tier evidence.

Use the `drop7-research` AWS profile and the repository CLI. Although the
machine policy contains the narrow DynamoDB action needed by the CLI, never call
`dynamodb:PutItem` directly. The CLI independently replays the moves, verifies
the immutable game artifact, and conditionally inserts an immutable record.

## Publish a registered policy

Preview first:

```sh
npm run competition -- seed \
  --stage production \
  --profile drop7-research \
  --policies <policy-id>
```

Review the game key, policy identity, public-information flag, source revision,
score, move count, censor state, and trajectory checksum. Add `--write` only
when the user has authorized the external ledger mutation.

## Import a game played on another machine

```sh
npm run competition -- seed \
  --stage production \
  --profile drop7-research \
  --replay runs/<run-id>/replays/<policy>--<round>.json \
  --source-revision <producing-commit-or-unknown>
```

Again, preview before adding `--write`. The replay must name a registered policy
and the active or selected competition round. State the producing machine's
actual commit; use the literal `unknown` rather than inventing one. A changed
result cannot replace an occupied policy slot—register a versioned policy ID.

After a write, confirm the CLI reports `inserted` or the identical idempotent
state. Do not infer scientific strength from the single scripted game.
