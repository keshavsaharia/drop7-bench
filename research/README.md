# Machine-readable research records

This directory is the operational layer for new research. Historical evidence
remains in `docs/research/history.md` and frozen files under
`artifacts/protocols/`; do not backfill model attribution or validity that those
sources cannot establish.

## Record layout

```text
research/
  schemas/          Versioned JSON Schemas
  templates/        Human-copyable draft records
  theories/         One file per falsifiable theory
  experiments/      One frozen protocol per experiment
  runs/             One metadata record per execution attempt
  results/          One scientific assessment per result
  contributions/    One self-contained record per human/model contribution
  datasets/         Content-addressed dataset manifests
  seeds/leases/     Data-use leases; no ad hoc ranges
  system-profiles/  Machine and toolchain manifests
  benchmarks/       Tier definitions and future cohort/baseline manifests
  generated/        Rebuildable indexes; never edit by hand
```

Individual files avoid a shared append-only ledger that would create merge
conflicts among concurrent agents. Drafts can change. A preregistered protocol,
opened seed lease, promoted dataset/result, or content-addressed artifact is
immutable; corrections create a successor record.

`researchctl freeze` computes an experiment's `protocolSha256` over UTF-8 JSON
with keys sorted, compact separators, one final newline, and
`protocolSha256: null`. This self-excluding canonical form avoids an impossible
self-hash while detecting every other protocol change.

## Record IDs

Use the helper instead of inventing IDs:

```sh
python3 .agents/skills/million-point-research/scripts/researchctl.py new theory \
  --slug concise-slug --title "Falsifiable title"
```

IDs include a UTC time/date component and a cryptographically random suffix:

```text
TH-20260820-afterstate-value-a1b2c3d4
EX-20260820-h40-closure-83e712aa
RUN-20260820T184215Z-91b02c33
RS-20260820T190501Z-1e7a4c02
CT-20260820T184300Z-f482ab19
```

Paths and IDs are separate: moving a draft file does not change its identity.

## Validate

```sh
make research-validate
```

The dependency-free validator checks JSON syntax, formats, IDs, status enums,
local references, seed-range overlap, sealed-bank rules, and consistency such
as “a preregistered experiment has a protocol hash.” The checked-in JSON Schemas
are the full interchange contract; the helper deliberately implements only the
high-value invariants without adding a package dependency.

Templates are excluded until copied into a live record directory. Replace every
`REPLACE_*` value before validation.

## Machine profiles

```sh
make research-doctor
python3 .agents/skills/million-point-research/scripts/researchctl.py doctor \
  --output research/system-profiles
```

The doctor is read-only. It records available facts and `null` when a tool is
missing. Passing the existing directory writes a new file named with the
embedded machine-profile ID. It never installs ROCm, changes power settings, or
exposes the hostname.

## Seed allocation is deliberately closed at bootstrap

The historical ledger mentions many ranges that have not yet been conservatively
imported into lease records. Therefore the helper validates seed leases but does
not allocate them. Before enabling an allocator:

1. import every ledger/protocol range and mark any possibly read range as used;
2. preserve `0x7d000000...0x7d00ffff` as protected and
   `0xd7000000...0xd70000ff` as final;
3. add an atomic, serialized overlap check and cross-language RNG test vectors;
4. mark a lease opened at environment process start; and
5. require manual audit before any protected/final transition.

Until then, a new experiment uses no gameplay, a documented previously evaluated
development cohort, or an owner/coordinator-assigned lease created after the
conservative import. An agent must not choose a range because it appears unused.

## Scratch and promoted evidence

Local run output belongs in ignored `runs/<run-id>/`. Compact evidence is
promoted to `artifacts/results/<experiment-id>/<run-id>/` with hashes and a
result record. Never write a new experiment to a historical shared `/tmp` path.

See [`docs/agents/contributions-and-commits.md`](../docs/agents/contributions-and-commits.md)
for attribution and [`docs/benchmarks.md`](../docs/benchmarks.md) for the result
contract.
