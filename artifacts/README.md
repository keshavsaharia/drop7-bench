# Retained Research Artifacts

This directory contains small artifacts that are necessary for understanding or
reproducing a retained approach. Generated training corpora, checkpoints, and
run results do not belong here unless their provenance, license, and role are
documented.

- `models/denoised-value/v1.bin` is the retained deterministic value model used
  by the denoised-value experiments.
- `protocols/million-point-validation.json` is the archived qualification
  protocol.
- `protocols/optimistic-phase-ntuple/` contains an archived experiment protocol
  and its training/development lane manifests.

The protocol contents are frozen historical records. Internal format names and
old path fields are preserved for compatibility; current source relocation has
not been disguised by replacing their SHA-256 values. See
[`docs/provenance.md`](../docs/provenance.md).
