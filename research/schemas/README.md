# Record schemas

These JSON Schemas use draft 2020-12 and versioned `format` identifiers. Add a
new schema version for a breaking field or semantic change; do not silently
reinterpret retained v1 evidence.

The schemas are the interchange contract. `researchctl validate` implements a
dependency-free subset plus project-specific cross-record, hash, and seed-bank
checks. A publication or external ingestion pipeline may additionally use a
full draft-2020-12 JSON Schema validator.

Record validity does not imply scientific validity. Schema validation says the
required facts are present and well formed; the independent audit skill checks
whether the experiment and claim are justified.
