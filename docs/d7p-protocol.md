# Drop7 Policy Protocol (D7P)

D7P is the standard interface between a Drop7 policy and a benchmark harness,
similar to UCI for chess engines. It lets any strategy, in any language, run
against the same scripted rounds and report results the same way.

D7P has two equivalent layers:

1. a **TypeScript interface** used by the in-repository registry
   (`src/bench/policies.ts`), and
2. a **text wire protocol** for standalone processes
   (`src/bench/d7p-server.ts`).

Both layers express the same idea: a policy is a deterministic function of the
public state.

## The public state

A policy receives exactly four things, matching the information boundary in
[methodology.md](methodology.md):

- the 49 visible board cells;
- the visible next disc;
- the number of moves until the next row rise; and
- whether the game is over (equivalently, whether any column is legal).

A policy must not receive or infer the round id, generator seed, future discs,
hidden gray-disc values, score, level, absolute move number, or any history it
cannot reconstruct from the public state. In the TypeScript registry, strict
policies are handed a sanitized state whose score, level, and move counter are
fixed constants; policies that deliberately read level or move number are
flagged `publicInformation: false` and badged "extended state" on the
leaderboard.

## TypeScript layer

```ts
interface BenchPolicy {
  id: string;                    // stable kebab-case id
  name: string;
  family: string;                // approaches/<family> grouping
  description: string;
  publicInformation: boolean;    // strict public-state only?
  slow?: boolean;                // excluded from the default bench suite
  chooseColumn(state: GameState): number | null;
}
```

`chooseColumn` returns an internal column index from `0` to `6`. Interfaces for
players display the same columns as 1–7. Returning `null` or an illegal column
is recorded as an illegal decision and the harness plays the first
legal column as a fallback. Policies must be deterministic: same public state,
same column. Solver seeds, when needed, are derived from the policy id and are
never game seeds.

## Wire protocol

The wire protocol is line-oriented UTF-8 over stdin/stdout. The harness sends
commands; the policy answers. All tokens are space-separated.

### Handshake

```
-> d7p
<- id name Greedy 1-ply
<- id family heuristic-search
<- id public-information true
<- d7pok
```

### Readiness

```
-> isready
<- readyok
```

### Setting the position

```
-> position startpos next 3 rise 5
-> position board 00000000000000000000000000000000000000008888888 next 3 rise 5
```

- `board` is the 49-character serialization from the engine
  (`serializeBoard`): row-major from the top, `0` empty, `1`-`7` numbered,
  `8` solid gray, `9` cracked gray.
- `startpos` is the standard opening (empty board, one covered bottom row).
- `next` is the visible next disc, `1`-`7`.
- `rise` is the moves remaining before the next row rise, `1`-`5`.

### Asking for a move

```
-> go
<- bestmove 4
```

`bestmove` carries the internal column index from `0` to `6`, or `none` when no
column is legal. User-facing tools add one when they display it.
The policy may emit `info ...` lines at any time; harnesses must ignore lines
they do not understand. `quit` ends the session.

### Reference server

Any registry policy can be driven over the wire protocol:

```sh
node --experimental-strip-types src/bench/d7p-server.ts --policy expectimax-d2
```

## Scripted rounds

Benchmark randomness is predetermined by the `drop7-scripted-round-v1` format
(`src/bench/rounds.ts`):

- `discs[m]`: the visible disc at move `m`, indexed by absolute move number;
- `latentRows[r][c]`: the hidden value of the covered disc in column `c` of
  covered-row generation `r` (generation 0 is the opening bottom row).

The engine's latent-board mode (`PlayMoveOptions.latent` in
`src/core/typescript/engine.ts`) carries each hidden value with its physical
disc through gravity and rises, and a reveal can only produce that
predetermined value. The standard suite is Gauntlet 01–08
(`src/bench/rounds/`), generated from the dedicated `0x5eed****` domain, which
overlaps no historical or reserved research seed range.

Scripted rounds are a public playground: they consume no research seed lease
and produce no tier evidence. Research-tier claims follow
[benchmarks.md](benchmarks.md).

## Running the benchmark

```sh
npm run bench                  # default policies x all rounds
npm run bench -- --all         # include slow reference policies
```

Results are written to `web/data/leaderboard.json` with one replay file per
game in `web/data/replays/`, and render in the web console at `/leaderboard`.
