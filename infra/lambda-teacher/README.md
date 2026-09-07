# n-tuple teacher Lambda pilot

A viability test for generating deeper-search teacher labels on AWS Lambda
instead of (or alongside) this workstation, for the n-tuple leaf research
under `approaches/ntuple-rl/ntuple-scale`. This is a pilot, not a
production data pipeline: one function, one decision per invocation, no
batching, no S3 output, no retry/backoff. It exists to answer one question
— does horizontal Lambda scaling of `depth-N` search decisions actually
work and cost what the math says it should — before any training run
depends on it.

## What it is

`src/main.rs` is the Rust counterpart of
`approaches/fair-expectimax/rust-engine/src/bin/decide.rs`, repackaged as
an AWS Lambda handler instead of a CLI binary. It takes the public state
(board, next disc, moves until rise, search depth, chance strata) and
returns the search's decision and cost counters. It reads no seed, score,
level, or hidden value — the same information boundary every deployable
policy in this repository follows (`AGENTS.md`, "Non-negotiable scientific
rules").

The crate depends on `approaches/fair-expectimax/rust-engine` by path and
calls the same `choose_action_root_parallel` function `decide.rs` calls,
including a byte-for-byte copy of its per-worker cache-partitioning helper.
It does not modify the engine crate.

## Why a container image, and why not `-C target-cpu=native`

The workstation's own build (`build.sh`) compiles with
`RUSTFLAGS="-C target-cpu=native"`, which is correct for a binary that only
ever runs on this machine and wrong for one that runs on whatever CPU AWS
schedules the function on. This crate's `Cargo.toml` says so explicitly and
does not set that flag.

The `Dockerfile` builds the binary *inside* `public.ecr.aws/lambda/provided:al2023`,
the same base image the function runs on, so the compiled binary's glibc
symbol versions are guaranteed to match the runtime rather than assumed to.
Building on this workstation (a newer glibc) or in a generic Rust image and
copying the binary in would risk a binary that compiles cleanly but fails
to start on Lambda.

## Deploying (from the repository root; build context matters)

```sh
podman build -f infra/lambda-teacher/Dockerfile -t drop7-lambda-teacher:pilot .

# Smoke test locally first, for free, before touching AWS:
podman run -d --name drop7-lambda-pilot-test -p 9000:8080 localhost/drop7-lambda-teacher:pilot
curl -s -X POST http://localhost:9000/2015-03-31/functions/function/invocations \
  -d '{"board":"0000000000000000000000000000000000000000000000","next":4,"rise":5,"depth":5}'
podman stop drop7-lambda-pilot-test && podman rm drop7-lambda-pilot-test

aws ecr get-login-password --profile personal-deploy --region us-east-1 | \
  podman login --username AWS --password-stdin 157405255987.dkr.ecr.us-east-1.amazonaws.com
podman tag localhost/drop7-lambda-teacher:pilot \
  157405255987.dkr.ecr.us-east-1.amazonaws.com/drop7-ntuple-teacher-pilot:pilot
podman push 157405255987.dkr.ecr.us-east-1.amazonaws.com/drop7-ntuple-teacher-pilot:pilot

aws lambda update-function-code --profile personal-deploy --region us-east-1 \
  --function-name drop7-ntuple-teacher-pilot \
  --image-uri 157405255987.dkr.ecr.us-east-1.amazonaws.com/drop7-ntuple-teacher-pilot:pilot
```

## Invoking

```sh
echo '{"board":"0000000000000000000000000000315724624613575273164","next":4,"rise":3,"depth":5}' > /tmp/req.json
aws lambda invoke --profile personal-deploy --region us-east-1 \
  --function-name drop7-ntuple-teacher-pilot \
  --cli-binary-format raw-in-base64-out \
  --cli-read-timeout 900 \
  --payload file:///tmp/req.json \
  --log-type Tail /tmp/resp.json --query 'LogResult' --output text | base64 -d
cat /tmp/resp.json
```

**Always pass `--cli-read-timeout` at or above the function's 900-second
timeout for depth 5+.** Without it, the AWS CLI's own HTTP client gives up
and, worse, silently retries the same invoke while the first attempt is
still running server-side — which happened once during the pilot and cost
a few extra cents of duplicate depth-6 compute for no reason. It is
harmless at this pilot's scale but is exactly the kind of thing that stops
being harmless once invocation volume goes up.

## Real numbers from the pilot (2026-09-07, us-east-1, 2048 MB memory)

Same board for the depth-5 comparison as the workstation's own single-thread
measurement (`decide --threads 1 --scheduler root`), so `work` is directly
comparable — and it matched exactly (34,485,024 in both), which is the
correctness check that matters more than timing: the same board, depth, and
parameters produce the identical search tree size on both a native Ryzen
build and this AL2023 container build.

| depth | workstation, single thread | Lambda, single thread, 2048 MB | ratio |
| --- | ---: | ---: | ---: |
| 4 | 0.59 s (different board, not compared) | 1.04 s | — |
| 5 | 11.46 s | 20.9 s | 1.82x |
| 6 | 194 s | 480.8 s and 412.2 s (two runs, same board) | 2.13x-2.48x |

Lambda ran this workload at roughly 1.8-2.5x the wall-clock time of a
single thread on this workstation's Ryzen AI MAX+ 395 — slower, as expected
for a shared cloud vCPU, and the two depth-6 runs (identical board, depth,
and parameters) landed 480.8 s and 412.2 s apart by about 15%, which is
real noisy-neighbor variance on shared hardware rather than a fixed
constant to plan against. Both are comfortably inside the 900 s function
timeout with 45-55% of the budget to spare; depth 6 is genuinely viable
here, not a tight fit.

The 480.8 s and 412.2 s readings exist because of the CLI-retry incident
below: two of the three concurrent depth-6 invocations it caused completed
and are cited here as real data rather than discarded, since they cost
real money already and are valid same-input measurements regardless of how
they were triggered.

Cost at 2048 MB: about $0.0000333/GB-second-equivalent (`2 GB x
$0.0000166667`). A depth-5 decision (~21 s) costs about $0.0007; a depth-6
decision (~410-480 s measured) about $0.014-0.016. Both numbers are
consistent with the pre-deployment estimate that an $80 compute budget buys
on the order of 90,000 depth-5 examples or 5,000-6,000 depth-6 examples —
cost is not the binding constraint at either depth. Total AWS spend for
every invocation in this pilot, including the accidental triple depth-6
run, was about 3-5 cents.

## What was deployed, and how to find it again

Everything is tagged `Project=drop7-ntuple-teacher-pilot`,
`ManagedBy=claude-code`, `Repo=drop7-bench` for identification and cleanup,
in AWS account `157405255987` (identity `csvhub-deploy`, which also hosts
about twenty other unrelated live services — see the account survey note
below before assuming anything about capacity or naming is free to reuse).

- IAM role `drop7-ntuple-teacher-pilot-exec`: trust policy for
  `lambda.amazonaws.com` only, one attached managed policy
  (`AWSLambdaBasicExecutionRole`, log writes only). No other permission.
  Created with a scoped role rather than the deploying `personal-deploy`
  credential (which has full `AdministratorAccess` on this account) so the
  function itself cannot do anything beyond writing its own logs.
- ECR repository `drop7-ntuple-teacher-pilot` (`us-east-1`), one image tag
  `pilot`.
- Lambda function `drop7-ntuple-teacher-pilot`: 2048 MB memory, 900 s
  timeout, `reserved-concurrent-executions` capped at 10 so this pilot
  cannot draw down the account's shared 1,000-execution concurrency pool
  (823 of which were already unreserved by other functions when this was
  measured) at the expense of the other live services in the account.

Nothing else was created: no S3 bucket, no SQS queue, no Step Functions
state machine, no budget action. This was deliberate — the brief was to
test viability, not stand up a production pipeline, and a driver that
invokes this function many times in parallel is a separate, larger piece
of work that should be scoped and preregistered as its own thing (see
`docs/research/status.md` and the `million-point-research` skill) once the
viability question above is answered to the owner's satisfaction.

## What this pilot does not answer

- Whether many concurrent invocations behave the same as one at a time
  (throttling, cold-start rate under burst, real p99 latency at scale).
- Whether a production driver (batching many boards per invocation, writing
  results to S3 instead of returning them synchronously, retrying failures
  correctly) changes the cost or reliability picture.
- Whether depth-5 or depth-6 teacher labels actually improve the n-tuple
  leaf when used for training — that is a research question for a
  preregistered experiment, not an infrastructure one, and this pilot
  makes no claim about it.
