# Research Methodology

This document defines what a fair Drop7 strategy experiment means in this
repository. It separates a legal public-information policy from a privileged
teacher and separates development evidence from a final claim.

## Game and score model

The simulator models the five-move Hardcore rules on a 7-by-7 board:

- a numbered disc clears when its value equals the contiguous occupied length
  through that disc in either its row or its column;
- clearing discs damage adjacent covered discs, and two hits reveal a number;
- gravity resolves between simultaneous chain waves;
- a covered row rises after every five placed discs;
- a row rise awards 17,000 points, clearing the board awards 70,000 points, and
  each cleared numbered disc in chain wave `d` awards `floor(7 * d^2.5)`;
- a game ends when a row cannot rise or no legal column remains.

The 17,000-point level bonus matters. Early runs marked with a 7,000-point level
bonus used Sequence-style scoring and cannot support the Hardcore score target.
Their trajectories and within-cohort survival comparisons may still be useful
when clearly labeled.

## Information boundary

The deployable policy is a deterministic function of the public state:

```text
(visible board, visible next disc, moves until rise, terminal flag) -> column
```

It must not inspect:

- the game or origin seed;
- future numbered discs or covered-disc reveals;
- a future random tape or oracle action;
- accumulated score, level, or absolute move number; or
- history that cannot be reconstructed from the public state.

Privileged planners may inspect future randomness to generate labels. Such an
experiment is an oracle or teacher experiment until a separate public student
is frozen and evaluated without those inputs.

```diagram
diagram-information-ladder
caption: The information classes as a ladder, from the legal public policy up to the clairvoyant planner, with the measured clears-per-move rate at each rung and the recorded shares of the gap between them.
```

## Chance nodes and fair comparison

Drop7 is stochastic, so a search must evaluate player choices and chance
outcomes. Exact enumeration is preferred when it is tractable. Otherwise the
experiment records its reveal strata, visible-disc samples, action widths,
horizon, cache, and work limit.

Candidate and reference policies should play the same ordered game seeds.
Within a decision, sibling actions should use common random numbers when the
estimator supports it. This reduces comparison noise without exposing the game
seed to the policy.

Alpha-beta pruning is not a direct solution here: ordinary alpha-beta assumes
alternating maximizing and minimizing choices, while Drop7 contains chance
nodes whose outcomes must be averaged.

## Evaluation unit

The independent statistical unit is a complete game, not a move, root, search
scenario, or transition. Move-level and root-level measurements are useful for
diagnosis, but confidence intervals for final performance are computed over
whole games.

Score has a heavy right tail: an occasional long chain can dominate a small
mean. Report at least:

- arithmetic mean, median, lower quartile, and minimum score;
- mean and lower-quartile lifetime in moves;
- censor count and move cap;
- numbered clears and covered reveals per move;
- mean and maximum chain depth;
- per-game paired results against the reference; and
- whole-game confidence bounds when making a qualification claim.

A game stopped at the 2,000-move cap keeps only score already earned. Because
score never decreases, this is a conservative lower bound; it must be reported
as censored and must not be extrapolated.

## Seed lifecycle

Seed ranges have roles, not interchangeable pools:

1. **Training/fitting:** may influence model parameters and implementation
   choices.
2. **Development/screening:** may decide whether an approach advances; once
   read, it remains development data.
3. **Protected validation:** may be opened only by a frozen candidate that met
   the development gate.
4. **Final confirmation:** a one-shot cohort opened only after protected
   validation and cross-engine parity.

Reusing an evaluated development cohort is acceptable for a clearly labeled
diagnostic, but it cannot become fresh confirmation evidence. A failed
protected block cannot be recycled for model selection.

## Million-point qualification

The archived
[validation protocol](../artifacts/protocols/million-point-validation.json)
sets a deliberately demanding standard.

A candidate first needs 256 development games with:

- observed mean score above 1,050,000; and
- a one-sided 95% whole-game bootstrap lower bound above 1,000,000.

The policy, model bytes, source hashes, compiler command, and work
configuration are then frozen. A 256-game protected run must have a mean above
1,000,000, both bootstrap and Student-t one-sided lower bounds above 1,000,000,
no illegal moves, and no runner failures. The unchanged candidate must repeat
those conditions on the one-shot 256-game final cohort.

The frozen status says no candidate had qualified and neither protected nor
final seeds had been opened. The relocation of source files means the archived
hashes do not authenticate current paths; [provenance](provenance.md) explains
what must happen before a new validation run.

## Decision gates

A gate should be fixed before reading the cohort it controls. It should state:

- the candidate and reference policy;
- allowed data and exact seed range;
- sample size and censor rule;
- score, lifetime, flow, stability, and runtime thresholds;
- what artifacts are written on pass or failure; and
- what later data becomes available after a pass.

Negative results remain useful when the gate, implementation, and cohort are
clear. They rule out a specific configuration; they do not prove that an entire
algorithm family can never work.

## Evidence labels used in the documentation

- **Reproduced:** executed in the reorganized checkout.
- **Ledger-recorded:** preserved in `docs/research/history.md`; expensive run not
  repeated during cleanup.
- **Protocol-recorded:** stated by an immutable historical protocol or manifest.
- **Task-record only:** found in the referenced research conversation without a
  retained repository artifact.
- **Proposed:** no completed implementation and no result.

Use the narrowest label that the available evidence supports.

For new machine-readable work, the provenance labels above are complemented by
three separate fields:

- **Run validity:** `valid`, `partial`, or `invalid`;
- **Scientific outcome:** `pass`, `fail`, `inconclusive`, or `not-applicable`;
- **Evidence tier:** proposal/mechanics, pilot, development, independently
  replicated development, protected validation, or final confirmation.

A valid negative result is `valid + fail`; an invalid run cannot support or
reject its hypothesis. The full operational definitions and standardized cohort
sizes are in [the benchmark contract](benchmarks.md).
