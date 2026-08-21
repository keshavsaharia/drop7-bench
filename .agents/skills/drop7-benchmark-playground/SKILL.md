---
name: drop7-benchmark-playground
description: Run the scripted-round benchmark, add or register a Drop7 policy, drive a policy over the D7P wire protocol, and generate new scripted rounds. Use for any request to bench a policy, add it to the leaderboard, compare policies on the gauntlet rounds, or expose a strategy through D7P. Also read this before quoting any scripted-round number, because those results are a playground and are never research-tier evidence.
---

# Drop7 benchmark playground

`src/bench/` is a fast, deterministic, seed-lease-free way to run a Drop7 policy
and see it on the leaderboard. It is deliberately separate from the research
tiers in [`docs/benchmarks.md`](../../../docs/benchmarks.md).

Follow the repository contract in `AGENTS.md`. The protocol reference is
[`docs/d7p-protocol.md`](../../../docs/d7p-protocol.md); read it before changing
anything about the interface.

## The boundary — read this first

Scripted rounds fix the entire future in advance: the disc at every move and the
hidden value of every covered disc. That makes them reproducible and cheap, and
it makes them **unsuitable as evidence about policy strength**.

- Scripted-round results **consume no seed lease** and **are never tier
  evidence**. Do not cite a leaderboard number in a theory, experiment, result,
  or status document, and never compare one against a `SCREEN`/`STANDARD`/
  `QUALIFY` cohort figure.
- There are **8 rounds**. Drop7 score is heavy-tailed — a 64-game research
  cohort has a score standard deviation near half its mean — so an 8-round total
  cannot separate policies that differ by less than a large factor.
- A related instrument was measured and **failed** its validation gate for
  exactly this reason: scoring positions over a short fixed horizon ranked
  search configurations *backwards*, because over a short window the score is
  dominated by the rise cadence rather than by skill. See
  [`docs/exploratory/finding-10-suite-validation.md`](../../../docs/exploratory/finding-10-suite-validation.md).
  Treat the gauntlet the same way: it is a smoke test, a demo, and a regression
  check, not a measurement.

What the playground **is** good for: proving a policy is legal and terminates,
catching crashes and illegal moves, eyeballing behaviour in the replay player,
regression-checking that a refactor did not change play, and demonstrating the
work to a human.

## Run it

```sh
npm run bench                       # default policies over all rounds
npm run bench -- --all              # include policies marked slow
npm run bench -- --policies greedy,expectimax-d2 --rounds gauntlet-01,gauntlet-02
npm run bench -- --out web/data     # default output location
```

Output is `web/data/leaderboard.json` plus one replay per game under
`web/data/replays/`, which the console renders at `/leaderboard`. Both are
gitignored: they are regenerable artifacts, not source.

## Add a policy

Register it in `src/bench/policies.ts`:

```ts
{
  id: "my-policy",            // stable kebab-case; also the replay path
  name: "My policy",
  family: "heuristic-search", // must match an approaches/<family> directory
  description: "One line shown on the leaderboard.",
  publicInformation: true,    // false if it reads level or move number
  slow: false,                // true excludes it from the default suite
  chooseColumn(state) { return 3; },
}
```

Rules that are enforced or checked:

- **Determinism.** Same public state must give the same column. If you need
  randomness, derive it from `policySeed(id)`, never from a game seed.
- **The information boundary.** A strict policy receives a sanitized state whose
  score, level and move counter are fixed constants. If your policy reads level
  or absolute move number it must set `publicInformation: false`, and it will be
  badged "extended state" on the leaderboard. Do not set the flag `true` to make
  a badge go away.
- **Legality.** Returning `null` or an illegal column is recorded as an illegal
  decision and the harness falls back to the first legal column. Illegal
  decisions are a defect, not a strategy.
- Add a test in `src/bench/bench.test.ts`. `npm test` covers `src/core/typescript`
  and `src/bench` together.

## Drive a policy over the wire

D7P is line-oriented UTF-8 over stdin/stdout, in the spirit of UCI. Any registry
policy can be served:

```sh
node --experimental-strip-types src/bench/d7p-server.ts --policy expectimax-d2
```

```
-> d7p                                            -> isready
<- id name Fair expectimax depth 2                <- readyok
<- id family fair-expectimax                      -> position startpos next 3 rise 5
<- id public-information true                     -> go
<- d7pok                                          <- bestmove 4
```

`board` is the 49-character `serializeBoard` form, row-major from the top:
`0` empty, `1`-`7` numbered, `8` solid gray, `9` cracked gray. Harnesses must
ignore unrecognized lines, so `info ...` is always safe to emit. Use this layer
to bench a policy written in another language without porting it.

## Generate new rounds

```sh
npm run bench:rounds
```

Rounds live in `src/bench/rounds/` in the `drop7-scripted-round-v1` format:
`discs[m]` is the visible disc at absolute move `m`; `latentRows[r][c]` is the
hidden value of the covered disc in column `c` of covered-row generation `r`,
with generation 0 being the opening bottom row.

Generators use the dedicated `0x5eed****` domain, which overlaps no historical
or reserved research range. **Keep new rounds in that domain.** If you need a
different domain, allocate it the way research leases are allocated — check every
eight-hex-digit constant already present in the repository first, and record the
range. A lease overlap has already happened here once; see
[`docs/exploratory/lease-map.md`](../../../docs/exploratory/lease-map.md).

## The engine's latent mode

Scripted rounds rely on `PlayMoveOptions.latent` in
`src/core/typescript/engine.ts`, which carries each hidden value with its
physical disc through gravity and row rises so a reveal can only produce the
predetermined value.

**Latent mode is optional and must leave default behaviour byte-identical.**
Two checks protect this and both must pass after any engine change:

```sh
npm test          # includes engine-latent.test.ts
npm run parity    # native/TypeScript exact-trajectory sweep, 256 seeds
```

The native engine (`src/core/native/engine.hpp`) has **no** latent mode; it
draws a covered disc's number at reveal time. That asymmetry is deliberate but
it means latent-mode behaviour is not cross-engine verified. A separate C++
scenario engine with latent values exists for research use under
`approaches/lifetime-objective/scenario/`, proven trajectory-identical to the
native engine in stream mode; prefer it for anything that needs a hidden board
and a native solver.

## Do not

- Quote a leaderboard score as evidence that one policy is stronger than another.
- Tune a research candidate against the gauntlet rounds. Eight fixed futures are
  an overfitting target, and they are visible in the repository.
- Add a round to make a policy look better.
- Change `drop7-scripted-round-v1` or the D7P wire format without versioning it —
  existing rounds and any external policy implementation depend on both.
