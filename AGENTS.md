# Drop7 research agent contract

This repository is a research program for a public-information Drop7 policy.
The target is the frozen qualification standard: a policy whose **mean** score
is above one million points in corrected five-move Hardcore mode. A single
million-point game is a milestone, not proof that the goal is met.

If any term here reads oddly out of context (cracking, exploding, oracle,
clairvoyant, hooks), read `docs/agents/project-nature.md`: it is a glossary
of this puzzle game's vocabulary and a plain statement of what the work is.

These rules are platform-neutral. Codex and OpenCode read this file directly.
Claude Code reads it through `CLAUDE.md`.

## Mandatory research routing

For any request to invent, implement, train, benchmark, accelerate, compare,
validate, or autonomously improve a Drop7 strategy, read
`.agents/skills/million-point-research/SKILL.md` completely before acting and follow
the references it selects. This includes broad directives such as “find the
million-point game” or “keep researching until a better policy is found.”

For an independent review of an existing theory, protocol, run, result, or
million-point claim, also read
`.agents/skills/audit-drop7-experiment/SKILL.md` completely and keep the audit
separate from candidate repair or new data collection.

To run the scripted-round benchmark, register a policy on the leaderboard,
drive a policy over the D7P wire protocol, or generate scripted rounds, read
`.agents/skills/drop7-benchmark-playground/SKILL.md`. Read it before quoting any
leaderboard number: scripted rounds are a playground and are never tier
evidence.

To prepare, configure, launch, or scale a large training or simulation run
(a multi-GPU machine, SLURM, Kubernetes, Ray, or a cloud pool), read
`.agents/skills/drop7-scale-out/SKILL.md`. It sequences the work into gated
stages that start on one workstation; a cluster never skips those stages.

To add to or change the Next.js research console under `web/`, read
`.agents/skills/drop7-web-console/SKILL.md`.

For a narrow documentation or code-maintenance task, follow the constraints
below without opening gameplay data or starting a research run.

## Ground truth and starting order

Before proposing new work, read:

1. `docs/research/status.md` for the current evidence boundary;
2. `docs/methodology.md` for game, information, seed, and statistical rules;
3. the relevant sections of `docs/strategies.md` and
   `docs/research/experiment-index.md`; and
4. `docs/research/history.md` only when exact prior configurations or outcomes
   are needed.

Run `make research-validate` before registering work. Run the cheapest relevant
tests first and `make test` before changing a scientific conclusion.

## Non-negotiable scientific rules

- A deployable policy may use only the visible board, visible next disc, moves
  until the next rise, and terminal state. It may not use seed identity, future
  randomness, hidden values, score, level, move number, or unreconstructable
  history.
- Privileged information may create a teacher or diagnostic label only. The
  final student must be frozen and evaluated through the public interface.
- Preserve corrected 17,000-point Hardcore scoring. Historical sources locked
  to 7,000 points are archival; port an approach into a new experiment instead
  of removing its lock.
- Use complete games as the statistical unit. Preserve paired per-game output,
  censor flags, and heavy-tail statistics.
- Keep training, reusable development, fresh development, protected, and final
  data roles distinct. Reading a cohort permanently changes its data status.
- Never open protected or final seeds unless a current, frozen protocol has met
  every prerequisite. A high-level goal is not that authorization.
- Do not edit frozen protocol artifacts or rewrite historical hashes. Create a
  new versioned protocol and retain the old record.
- A failed experiment rejects only the exact tested configuration. A valid
  negative result is still a completed scientific contribution.

## Work as a falsifiable sequence

1. Register one theory with a mechanism and explicit falsification criteria.
2. Perform seed-free data, semantic, dependency, and runtime feasibility checks.
3. Preregister an experiment: candidate, comparator, cohort role, metrics,
   gate, resource budget, stop conditions, and artifact paths.
4. Implement it in `approaches/<family>/<approach>/`. Put reusable, policy-
   independent mechanics in `src/core/` only when necessary.
5. Pass mechanics, legality, determinism, reflection, information-boundary,
   resource-bound, and native/TypeScript parity checks before gameplay.
6. Advance through the benchmark tiers in `docs/benchmarks.md`; do not tune on
   the cohort whose gate was just read.
7. Record valid, invalid, partial, negative, and interrupted outcomes. Never
   omit a run because it was disappointing.
8. Update current-status docs only when retained evidence changes them.

Choose the next experiment by expected information gain per unit of compute,
not novelty alone. Prefer bounded corrections around fair D4 until a candidate
proves it can rank all legal siblings on disjoint whole-origin data.

## Concurrency and filesystem safety

- One coordinator owns experiment IDs, data leases, and integration decisions.
  Delegated agents own bounded work packages and report their own contribution.
- If Git and platform-managed worktrees are available, use one branch/worktree
  per experiment. Do not initialize Git automatically when metadata is absent.
- Inspect the working tree before editing. Preserve unrelated work and never
  reset, overwrite, amend, or delete another contributor's changes.
- Use cryptographically generated record IDs. Never let two agents choose an
  ad hoc seed range or shared output name independently.
- Write local work to `runs/<run-id>/` and builds to a namespaced build path.
  Do not introduce new shared `/tmp` defaults.
- Only the coordinator edits shared status tables after merging evidence.
  Frozen protocols and promoted results are immutable.
- Expensive timing runs need an exclusive or explicitly isolated resource
  lease. Avoid nested CPU, OpenMP, BLAS, and GPU oversubscription.

## Attribution and commits

Every model or human that materially contributes writes a separate record under
`research/contributions/`. Record the exact platform and model identifier when
the runtime exposes them; otherwise write `unknown`. Do not infer a model name,
claim a percentage, or claim delegated implementation as the coordinator's own.

Contribution level describes scope, not truth or credit rank:

- `L0`: executed or observed an existing workflow;
- `L1`: review or mechanical support;
- `L2`: substantive bounded implementation or analysis;
- `L3`: primary author of a named theory, protocol, implementation, or result;
- `L4`: coordinated and integrated multiple separately attributed contributions.

When the user/platform workflow authorizes commits, use the repository commit
template and validated trailers. Use the configured human/bot Git author; model
attribution belongs in contribution records and trailers. Never invent a SHA
when Git is absent or work is uncommitted.

## Tooling: benchmark playground and web console

`src/bench/` implements scripted deterministic rounds (fixed disc tape plus
predetermined latent gray-disc values, via the engine's optional latent mode)
and the D7P policy protocol (`docs/d7p-protocol.md`). `web/` is a Next.js
console rendering research records, approach MDX docs, and the leaderboard.
Scripted rounds are a public playground: they consume no seed lease, and their
results are never tier evidence. The engine's latent mode is optional and must
leave default random-reveal behavior byte-identical; `npm test` covers both.

Eight fixed rounds cannot separate policies whose scores are heavy-tailed, so a
leaderboard total is a smoke test and a demonstration, not a measurement. Never
tune a research candidate against the gauntlet rounds; they are visible in the
repository and are an overfitting target. Scripted-round generators use the
dedicated `0x5eed****` domain, which overlaps no research range.

The native engine has no latent mode and draws a covered disc's value at reveal
time, so latent-mode behavior is not cross-engine verified. For research that
needs both a hidden board and a native solver, use the C++ scenario engine under
`approaches/lifetime-objective/scenario/`, which is proven trajectory-identical
to the native engine in stream mode.

`web/` reads the repository directly and has no database or API server. It must
render on a checkout with no `web/data/` and no research results, must never
compute or infer a number that is not recorded, and must carry run validity,
scientific outcome, and evidence tier through to the interface unchanged.

## Definition of done

A research change is not done until its source, protocol or theory record,
result or explicit no-run status, validation commands, limitations, and model
contribution record agree. A performance claim also requires a machine profile.
