# Benchmark manifests

`tiers-v1.json` defines scientific evidence tiers without assigning seed ranges.
`profiles-v1.json` defines resource views. `baselines-v1.json` pins the current
transitive source closure and algorithmic constants for fair D4 while keeping
its complete-game performance explicitly ledger-recorded. Future cohort
manifests must be added only after the historical seed audit is complete.

The `machine-max-throughput` profile means the best stable point found by a
recorded scaling preflight under an explicit safety margin. It never means
blindly allocating every CPU, byte of RAM, or GPU queue.
