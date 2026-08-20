# Archived Protocols

These files record data boundaries and fixed gates used by historical
experiments:

- [`million-point-validation.json`](million-point-validation.json) defines the
  public-information qualification target, seed lifecycle, censoring rule, and
  required confidence bounds.
- [`optimistic-phase-ntuple/protocol.json`](optimistic-phase-ntuple/protocol.json)
  defines the staged n-tuple training and evaluation gates. Its adjacent text
  files are the exact archived lane manifests.

The JSON `format` values and internal old filenames are serialized
compatibility data. Do not edit v1 hashes to match relocated sources. Create a
new versioned protocol before resuming a source-locked run; see
[`docs/provenance.md`](../../docs/provenance.md).
