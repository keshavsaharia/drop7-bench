# Run records

A run is one process attempt. Record it even when interrupted or invalid. Seed
leases become opened at process start, not when the first result is written.
Large stdout, checkpoints, and raw rows stay under ignored `runs/<run-id>/`
until deliberately promoted.

