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

## Publish a whole run directory

A run's small artifacts (per-game rows, compare reports, validation line-ups,
progress logs, gate logs, the analysis) belong together under the same run
namespace. `--dir` walks a directory, keeps its layout below an optional
`--key` prefix, skips `--exclude` globs (relative to the directory), and
`--manifest` appends one JSON line per object so the record can cite every
reference without retyping it:

```sh
npm run artifact:publish -- \
  --run-id RUN-YYYYMMDDTHHMMSSZ-0123abcd \
  --dir runs/RUN-YYYYMMDDTHHMMSSZ-0123abcd/ntuple-scale \
  --key ntuple-scale \
  --exclude '*.bin' --exclude '*.nohup' \
  --manifest runs/RUN-YYYYMMDDTHHMMSSZ-0123abcd/published.jsonl \
  --dry-run --public
```

Run it with `--dry-run` first and read the list: every path it prints will be
world-readable. A dry run uploads nothing and writes nothing to the manifest;
only an object verified in S3 reaches it, as `uploaded` or
`already-published`. Then run it again without `--dry-run`. An object that
already holds identical bytes is reported as `already-published`, so the
command is safe to repeat after an interruption; a conflicting object still
stops it.

Publish large table or checkpoint files one at a time with `--file`, compressed
with `zstd` when the file is sparse (a 4 GB lookup table with a tenth of its
entries touched compresses about ten to one). The reference's digest is the
digest of the uploaded object (the `.zst`); record the digest of the
decompressed file beside it in the record's notes so a consumer can verify
both. Keep `--key` under the same layout the run used locally
(`ntuple-scale/main/best-weights.bin.zst`), so a reader who knows the run
directory knows the object key.

## Cite what was published

- A run record lists each object's reference in `artifactRefs`; the validator
  accepts `https://data.drop7.dev/runs/<run-id>/...#sha256=<digest>` there and
  rejects any other remote form.
- A result record's `perGameArtifact.path` may be the remote reference of the
  per-game artifact; its `sha256` must equal the fragment digest.
- A frozen model or corpus another agent should load gets a dataset record
  (`research/datasets/`, `researchctl.py new` does not mint these yet; copy
  `research/templates/dataset.json`) whose `artifact.path` is the remote
  reference and whose notes say how to decompress and verify it.
- Consumers download with plain HTTPS (`curl -fL -o file
  https://data.drop7.dev/runs/<run-id>/<path>`), strip the fragment, and
  check `sha256sum` against the digest before reading the file.
