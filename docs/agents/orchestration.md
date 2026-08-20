# Autonomous research orchestration

This is the operating procedure for a coordinator given an outcome instead of
a ticket—for example, “make it your goal to find the million-point game.”

## Translate the outcome correctly

The official target is not to observe one lucky score above one million. It is
to produce one frozen, public-information policy that satisfies the mean and
confidence requirements in
[`million-point-validation.json`](../../artifacts/protocols/million-point-validation.json).
The first individual million-point game can be logged as a milestone, but it
does not complete the campaign.

The coordinator should be optimistic about finding a path and skeptical about
every individual claim.

## Campaign state machine

```text
DIRECTIVE
   |
   v
CURRENT-STATE AUDIT --> THEORY --> FEASIBILITY --> PREREGISTERED EXPERIMENT
                                                |
                                                v
IMPLEMENT --> CHECK --> PILOT --> SCREEN --> STANDARD --> QUALIFY
     ^          |         |         |            |          |
     |          +---------+---------+------------+----------+
     |                    valid result + assessment
     +---------------- choose next highest-information theory

QUALIFY pass --> freeze candidate --> PROTECTED pass --> FINAL pass --> CLAIM
```

A failed gate returns evidence to the theory portfolio; it never quietly turns
into another tuning run on the same cohort. Protected and final transitions are
manual protocol events, not automatic continuations.

## Phase 1: recover the current state

1. Read current status, methodology, strategy catalog, and the relevant index
   entries. Consult the historical ledger only for exact provenance.
2. Inspect source control and local changes. If Git is absent, report that and
   use file-based contribution records; do not fabricate commits or initialize
   a repository automatically.
3. Run `make research-validate`, then the lowest-cost relevant correctness
   checks. Do not start by rerunning a historical multi-hour command.
4. Run `make research-doctor` and determine available CPU, memory, GPU, runtime,
   and cgroup limits without changing the machine.
5. Inventory required datasets and artifacts. Distinguish retained files from
   ledger references to files that no longer exist.
6. Check active experiment, run, contribution, and seed-lease records before
   assigning work.

## Phase 2: choose a theory portfolio

Rank candidate theories with a short decision table:

| Factor | Question |
| --- | --- |
| Failure addressed | Does it attack a documented bottleneck such as sibling extrapolation? |
| Information gain | Will a negative result eliminate a useful design choice? |
| Data feasibility | Can it be tested without mislabeled or already-selected evidence? |
| Semantic risk | Could it change rules, chance handling, or the public boundary? |
| Compute path | Is there a cheap seed-free or offline gate before gameplay? |
| Hardware fit | Can CPU, memory, or GPU improve throughput end to end? |
| Reversibility | Can it be isolated without changing a frozen baseline? |

Select one primary theory. Optionally register one fallback diagnostic whose
trigger and scope are fixed in advance. Avoid launching a wide architecture
sweep whose failures would be impossible to interpret.

## Phase 3: preregister before data

Create separate theory and experiment records. The experiment must fix:

- candidate, unchanged comparator, information class, and source entry point;
- data manifest or seed role and the precise rule for assigning it;
- primary metric, secondary diagnostics, comparison method, and gate;
- algorithmic work, chance semantics, maximum moves, and censor policy;
- wall, CPU, host-memory, GPU-memory, error, and futility stops;
- expected artifacts and checksums;
- what is allowed to happen after pass, failure, interruption, or invalidity;
- the model/human responsible for each work package.

Hash the completed protocol before its controlled data is opened. A pre-data
amendment is explicit and versioned. A post-data change becomes a new experiment.

## Phase 4: divide work without interference

Useful independent roles are:

- **coordinator:** IDs, leases, budgets, integration, and final claim language;
- **theory designer:** mechanism, alternatives, and falsification criteria;
- **data auditor:** origin splits, sibling closure, leakage, and manifests;
- **simulator verifier:** rules, RNG, parity, legal actions, and invariants;
- **systems engineer:** profiling and semantics-preserving acceleration;
- **implementer:** candidate in an isolated approach directory;
- **benchmark runner:** frozen command and canonical result bundle;
- **skeptical verifier:** independent arithmetic, hashes, data roles, and scope.

A single agent may fill several roles when parallel workers are unavailable,
but it should still perform them as explicit passes. When workers are available:

- give each one a bounded deliverable and disjoint files;
- use a separate experiment branch/worktree if Git and the host support it;
- use `build/<experiment-id>/<run-id>/` and `runs/<run-id>/`;
- allocate CPU, memory, and GPU explicitly; and
- let only the coordinator assign data or edit shared summaries.

Do not use the many historical shared `/tmp` defaults for new work. Copy or
parameterize an approach under a new experiment instead of letting concurrent
jobs overwrite one another.

## Phase 5: gate from cheapest to strongest

Run in this order:

1. static and schema validation;
2. focused unit/self-tests;
3. native/TypeScript transition and RNG parity when mechanics are touched;
4. public-information, metadata-blindness, reflection, legality, determinism,
   resource-bound, and resume-equivalence tests;
5. seed-free/offline sibling diagnostics;
6. a bounded runtime pilot on leased development data;
7. paired gameplay tiers from `docs/benchmarks.md`.

Never compensate for a failed offline ranking gate by opening a larger gameplay
cohort unless the preregistered protocol explicitly made that the next step.

## Phase 6: close every run

Every execution ends with a run record. Distinguish:

- `valid`: the frozen method executed as specified;
- `partial`: some interpretable output exists, but the run hit a declared stop;
- `invalid`: a mechanics, data, corruption, or protocol error prevents the
  output from testing the hypothesis.

Then assess the scientific outcome separately as `pass`, `fail`,
`inconclusive`, or `not-applicable`. Record negative and interrupted work,
including unopened data and unused follow-on authority.

The skeptical verifier checks source/model/protocol hashes, per-game counts,
seed roles, arithmetic, censoring, failure counters, machine profile, and claim
scope. Replication requires a distinct run and contributor; an author cannot
declare its own first run independently replicated.

## Phase 7: continue intelligently

After each assessment:

1. update the theory state without generalizing beyond the tested configuration;
2. update the current status only if evidence materially changed;
3. keep a rejected mechanism available when a clearly different configuration
   remains plausible;
4. choose the next step by information gain per expected compute; and
5. continue while the directive and authority remain active.

Ask the owner only when progress requires new authority or a choice that would
materially alter the research objective. Slow or difficult work should first be
reduced to a smaller diagnostic, profiled, or transferred to the preregistered
fallback.

## Recovery after interruption

On resume, do not assume that a process which produced no final file read no
data. Check process-start records, partial output, checkpoints, seed leases, and
artifact hashes. A seed is opened at environment process start. Protected/final
leases are never automatically reclaimed after a crash. Resource locks may be
reclaimed only after host/PID verification.

Resume only from an exact checkpoint with recorded RNG cursors and input hashes.
Otherwise close the old run as partial/invalid and create a new run ID.
