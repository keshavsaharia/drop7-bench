# Provenance and Evidence Boundaries

## Sources used to reconstruct the project

This repository was organized from four evidence sources:

1. the copied TypeScript, C++, Python, model, manifest, and protocol files;
2. the long native experiment notebook, now retained as
   `docs/research/history.md`;
3. the referenced research task, read as a live task record; and
4. deterministic tests and builds run in this checkout.

The current directory did not contain Git metadata when the audit began, so
there is no local commit history establishing when each copied experiment was
created. The research task is not vendored here. Claims found only there are
explicitly labeled **task-record only**.

## Engine provenance

The copied TypeScript engine bytes initially matched the historical
`typescriptSha256` value in the frozen million-point protocol exactly. The
native and TypeScript implementations also expose deterministic trace parity
checks.

The reorganization changed paths, include statements, and purpose comments.
These changes alter byte hashes even where executable behavior is unchanged.
Current source files must therefore be tested directly and must not be presented
as byte-identical to the historical snapshot.

## Frozen protocols

Files under `artifacts/protocols/` are archival records. Their serialized
format identifiers retain the original `drop7-...-v1` names because those are
compatibility identifiers, not repository filenames.

The protocols contain source and manifest SHA-256 values from the earlier tree.
Those hashes were not silently replaced during relocation. In particular, the
optimistic phase n-tuple executable verifies transitive source bytes, and its
historical lock is expected to reject the reorganized files. A resumed study
needs a new protocol version and new current hashes; editing the v1 artifact
would erase the distinction between the historical run and a new one.

## Result artifacts

Many recorded experiments wrote checkpoints, corpora, and JSON summaries to
temporary storage. Most of those artifacts are absent from this checkout. The
detailed ledger records their paths and historical hashes, which helps identify
the experiment, but a hash without the corresponding bytes is not independently
verifiable here.

For that reason:

- local tests and parity are described as **reproduced**;
- expensive metrics in the ledger are **ledger-recorded**;
- immutable state in a retained protocol is **protocol-recorded**; and
- a statement present only in the task is **task-record only**.

## Third-party prior work

Five copied page images were identified as rasterized pages from Erez Klein and
Ben Friedmann's [Drop7 Q-learning final report](https://ekreate.github.io/projects/drop7_q_learning.pdf).
The images had no bundled license or provenance notice and exposed personal
contact details, so they were removed rather than redistributed. The original
report remains cited as prior work in the strategy landscape.

David Walton's
[Sequence-mode solver write-up](https://programmablebrick.blogspot.com/2013/03/drop7-with-lego-mindstorms-nxt.html)
and [source repository](https://github.com/dwalton76/Drop7-Sequence-Mode) are
also relevant prior work, but their known-future-disc setting is not comparable
to this project's public-information Hardcore target.

## Using this repository in research

When citing a result, include its evidence label, rules/scoring variant, cohort
size, seed role, move cap, and whether the run was censored. Cite the source and
protocol version separately. For a new experiment, retain its complete result
artifact and transitive-source manifest inside a durable artifact store rather
than relying only on a temporary path.
