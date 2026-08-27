# Rust search matrices on a large x86 EC2 instance

This directory runs the same fixed-work Rust command locally and remotely. It
does not make a search-strength claim and the scripts never open a seed range.
Freeze the roots, leaf files, source archive, matrix config, budget, and output
prefix before launch; a later teacher corpus still needs its own registered
data lease and protocol.

The default example uses `hpc7a.96xlarge`, whose live AWS description must show
at least 192 x86 physical cores. `c7i.48xlarge` has 192 vCPUs but only 96
physical cores with SMT, so use it only by lowering `MIN_PHYSICAL_CORES` to 96
and changing `AWS_VCPU_QUOTA_NAME` to the account's On-Demand Standard quota.
New accounts commonly start with a zero-vCPU adjustable On-Demand HPC quota,
so request the increase before the run; plan mode fails closed unless the live
regional quota covers the instance. No instance type is trusted from its name:
`provision-ec2.sh` queries architecture, vCPUs, cores, threads per core,
applicable vCPU quota, Availability Zone offering, and security-group ingress
before launch.

## One-command EC2 run

From the repository root, the normal entry point is:

```sh
AWS_PROFILE=personal-deploy just run-matrix gauntlet-01 --budget 10000
```

`--budget` is an all-in operator ceiling in integer US cents. The orchestrator
queries the current On-Demand Linux price through the AWS Price List API,
reserves the larger of 10% or $5 for EBS, S3, and the finite Capacity
Reservation tail, and converts the rest into a boot-to-shutdown wall bound. It
fails before launch if the account's live HPC quota, x86 core count, source
hash, S3 bucket, region, AZ offering, subnet, security group, instance profile,
or projected instance charge does not match the request.

The default uses `drop7-bench-data` in `us-east-2` and
`hpc7a.96xlarge`. On its first real launch it resolves a public subnet in the
default VPC and creates or reuses two free control-plane resources: an
egress-only `drop7-bench-ec2-egress` security group and a
`drop7-bench-ec2-runner` IAM role/instance profile restricted to
`s3://drop7-bench-data/ec2/*` plus SSM. Custom accounts can set
`DROP7_SUBNET_ID`, `DROP7_SECURITY_GROUP_ID`, or
`DROP7_IAM_INSTANCE_PROFILE` instead.

The command creates a cryptographically suffixed run ID, derives the scripted
round's initial public position, writes and hashes the matrix config, performs
the allocation-free resource plan, packages the exact dirty checkout, uploads
it below `s3://drop7-bench-data/ec2/<run-id>/source/` together with the request,
input, and resource-plan metadata, executes the live preflight, creates a
finite targeted Capacity Reservation, and launches one instance. The local
orchestrator then waits, prints the latest ten-second
host-utilization sample, downloads the final artifacts, cancels the reservation,
and verifies that the instance reached `terminated`.

For a no-launch rehearsal:

```sh
AWS_PROFILE=personal-deploy just run-matrix gauntlet-01 --budget 10000 --plan
```

Plan mode makes read-only AWS calls and writes a local package under `runs/`;
it creates no S3 object, IAM role, security group, Capacity Reservation, EBS
volume, or EC2 instance.

Useful overrides are `--depths 4,5,6,7`, `--strata 7`, `--roots FILE`,
`--root-limit N`, `--threads N`, `--instance-type TYPE`, `--region REGION`,
`--bucket BUCKET`, and `--no-capacity-reservation`. The default generated root
is only the named round's **initial public state**. A frozen roots file can
probe later positions; playing a complete scripted trajectory at each depth is
a separate online policy run because the depths can choose different moves.

The instance records `/proc` CPU, load, and memory counters every ten seconds,
uploads a live CSV about once a minute, and retains both
`utilization.csv` and `utilization-summary.json`. Search-specific occupancy is
also present in each analytics row as `workerBusyFraction` and per-worker task,
time, and work counts. After download, the orchestrator writes and re-uploads
`matrix-summary.json`, including every completed depth and the deepest action,
wall time, and busy fraction for each root/leaf/strata group. Basic sampling is
used instead of paid detailed CloudWatch monitoring.

If the local orchestrator exits or receives a signal during or after launch,
both launcher cleanup layers recover resources from the unique EC2 client token
and atomic `Project`/`RunId` tags, retry through the EC2 eventual-consistency
window, request instance termination, and cancel the reservation. This closes
the interval between AWS accepting a launch and the instance ID reaching the
local log. If the local machine disappears without running either trap, the
boot-level watchdog still shuts the instance down and the reservation has a
finite end. The encrypted root volume is delete-on-termination. Run-owned
source lists, IAM documents, and user data stay under `runs/<run-id>/` and are
removed after use. The S3 source and result artifacts are deliberately
retained; the reusable IAM role and security group do not consume compute
capacity or accrue hourly instance charges.

## Local smoke run

```sh
approaches/fair-expectimax/rust-engine/cluster/run-matrix.sh \
  approaches/fair-expectimax/rust-engine/experiments/example-matrix.env
```

The analyzer writes one manifest and one JSON object for every
root × leaf × strata × depth cell. Decision rows retain every legal column's
decimal value and exact f64 bits, selected action, whether the action changed
from the preceding depth, work/node/cache counts, planner/task counts, phase
timings, memory projections, overall busy fraction, and per-worker load.

## What the depth ladder can establish

The position matrix answers a narrow question: on the same public roots and
leaf, how do sibling values and selected actions change as completed depth and
chance coverage increase, and what compute, memory, and utilization does each
cell cost? It does **not** by itself establish a million-point strategy. Drop7
scores are heavy-tailed, and the qualification target is a mean above one
million over complete corrected-scoring Hardcore games, not an action agreement
rate, a single game, or a deeper evaluator value.

Use the remote workflow as a falsifiable sequence:

1. **Mechanics and scaling.** On the target x86 host, compare `root` and
   `frontier` schedulers at D4/S7 and D5/S7 on the frozen roots. Require exact
   sibling-value/action parity, exactly-once tasks, bounded memory, and retained
   per-worker occupancy before spending the rest of the reservation.
2. **Depth ladder.** Run the same ordered roots with `MATRIX_DEPTHS=4,5,6,7`,
   fixed leaf weights, and preregistered strata. Inspect `previousAction`,
   `actionChangedFromPreviousDepth`, every sibling's `valueBits`, logical work,
   and wall time. Set the action-stability/value-gap gate before reading the
   deeper rows; do not select a depth after looking at the cohort it must pass.
3. **Teacher transfer, if requested.** Emit every legal sibling label, preserve
   the whole root/origin grouping, and train the NNUE only on assigned training
   origins. A deeper solver may be a public-information teacher; any
   future-randomness or hidden-value teacher must be marked privileged, and its
   frozen student still gets only public inputs.
4. **Strategy evidence.** Freeze either the exact-depth policy or the trained
   student and evaluate complete games through the public interface on disjoint
   development, protected, and final roles. Only the recorded complete-game
   mean and uncertainty can answer whether the policy exceeds one million.

The first cloud run should stop after steps 1–2. It establishes whether deeper
search is computationally practical and changes rankings coherently; it does
not silently open a teacher-data or gameplay cohort.

## No-launch preparation

These commands allocate no AWS resource:

```sh
cargo build --release \
  --manifest-path approaches/fair-expectimax/rust-engine/Cargo.toml \
  --bin plan --bin analyze --bin bench

approaches/fair-expectimax/rust-engine/target/release/plan \
  --depth 7 --strata 7 --threads 192 --cache 262144 \
  --split-plies auto --max-host-bytes 8589934592

approaches/fair-expectimax/rust-engine/cluster/package-source.sh \
  --local runs/<run-id>/package
```

`provision-ec2.sh --plan <ec2-run.env>` is also non-mutating, but it does make
live read-only AWS calls to verify instance shape, regional quota, Availability
Zone offering, security-group ingress, and the current public AMI. This work's
CHECK run used a local fake CLI instead; no live AWS API was called.

## Package, inspect, and launch

1. Copy `ec2-deep-matrix.env.example` plus its frozen roots/weights into
   `approaches/fair-expectimax/rust-engine/cluster-input/<run-id>/`; this input
   directory is included in the source archive while generated `runs/` output
   is excluded. Copy `ec2-run.env.example` into the unique local run directory.
   Replace every placeholder and record the caller-verified current hourly
   price and total instance-cost cap.
2. Package the exact dirty checkout (including uncommitted source) and upload
   it to an operator-owned S3 prefix:

   ```sh
   approaches/fair-expectimax/rust-engine/cluster/package-source.sh \
     runs/<run-id>/package s3://<bucket>/<run-id>/source
   ```

   Use `package-source.sh --local runs/<run-id>/package` first when the archive
   and SHA-256 should be inspected without an AWS call. Packaging follows
   `git ls-files`: all tracked and current non-ignored untracked source is
   included, while ignored runs, build trees, dependencies, and web output are
   excluded.

3. Inspect the live AWS preflight without creating resources:

   ```sh
   approaches/fair-expectimax/rust-engine/cluster/provision-ec2.sh \
     --plan runs/<run-id>/ec2-run.env
   ```

4. After registering the run and its machine/resource budget, execute the same
   command without `--plan`. The launcher uses the latest regional Amazon Linux
   2023 x86_64 AMI from AWS's public SSM parameter, requires an egress-only
   security group and SSM/S3 instance profile, enables IMDSv2 and encrypted
   delete-on-termination EBS, and sets instance-initiated shutdown to
   `terminate`. A boot-level watchdog starts before package installation, while
   the instance runner also shuts down after success, failure, or its timeout;
   a failed preflight therefore cannot leave the instance running indefinitely.
   If requested, its targeted Capacity Reservation has a finite end time just
   beyond the wall bound.

The instance uploads the result directory to the frozen S3 result prefix on
success, error, signal, or timeout, then shuts itself down. The user-supplied
hourly price assertion is a fail-closed budget input, not a live AWS price
quote; verify it immediately before launch.

There is deliberately no automatic launch in `run-matrix.sh`, `analyze`, or
the Rust `plan` binary. `just run-matrix ...` is the complete operator entry
point and is intentionally mutating unless given `--plan`; internally, the
only command that creates a Capacity Reservation or EC2 instance remains
`provision-ec2.sh` **without** `--plan`.
