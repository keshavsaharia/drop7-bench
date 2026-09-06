---
name: drop7-artifact-publishing
description: Publish large, public-safe Drop7 run artifacts to the immutable data.drop7.dev archive with the drop7-research AWS profile. Use for generated corpora, checkpoints, traces, replay bundles, per-game rows, or other run files that should not enter Git.
---

# Drop7 artifact publishing

Follow `AGENTS.md`, especially **Remote research artifacts**. The destination is
world-readable; publishing is a release decision, not temporary storage.

## Decide what belongs there

Keep compact source, protocols, manifests, summaries, hashes, and evidence
records in Git. Publish generated data and any artifact over 10 MiB under the
canonical run that produced it.

Never upload credentials, secrets, protected or final seed material, sealed
cohort identities, privileged hidden-state data, or any file that has not been
classified for public release. Athena source rows and raw mobile game tapes are
private by default.

## Publish

Confirm the profile before the first upload on a machine:

```sh
aws sts get-caller-identity --profile drop7-research
```

The account must be `157405255987`. Then use the checked-in publisher:

```sh
npm run artifact:publish -- \
  --run-id RUN-YYYYMMDDTHHMMSSZ-0123abcd \
  --file runs/RUN-YYYYMMDDTHHMMSSZ-0123abcd/per-game.jsonl \
  --public
```

Use `--key relative/path` only to preserve a meaningful layout below the run.
The helper defaults to `--profile drop7-research`, hashes the file, refuses to
replace a conflicting object, verifies its S3 metadata and byte count, and
prints the canonical reference:

```text
https://data.drop7.dev/runs/<run-id>/<path>#sha256=<digest>
```

Copy that exact reference into the run, result, dataset, or contribution record.
Do not finalize a record before the upload verifies. Do not use an unscoped key,
overwrite an existing key, or commit a duplicate copy of the large artifact.
