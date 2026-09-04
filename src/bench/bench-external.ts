/**
 * Plays an external, synchronous decision-query command through one or more
 * scripted rounds and prints the result. This is the benchmark PLAYGROUND
 * (see .agents/skills/drop7-benchmark-playground/SKILL.md): it consumes no
 * seed lease and the result is never research-tier evidence, only a
 * demonstration or a regression check.
 *
 * It exists for policies that live outside this TypeScript registry — most
 * often a frozen candidate from a Rust research crate — without porting the
 * search or the leaf evaluator to TypeScript. Reuses the repository's own
 * authoritative scripted-round loop (playScriptedGame) unmodified: the only
 * new code here is a BenchPolicy whose chooseColumn shells out to the given
 * command for each move.
 *
 * The external command must, given `--board <49ch> --next <1-7> --rise
 * <1-5>` appended to the base command, print exactly one integer 0-6 (the
 * chosen column) to stdout and exit. `approaches/lifetime-objective/
 * nnue-evolution/src/bin/query_move.rs` implements this convention for any
 * NNUE candidate that crate produces; any other command in any language that
 * follows it works the same way.
 *
 * Usage:
 *   node --experimental-strip-types src/bench/bench-external.ts \
 *     --command "approaches/lifetime-objective/nnue-evolution/target/release/query_move --weights <path>" \
 *     --rounds gauntlet-01[,gauntlet-02,...] \
 *     [--id my-candidate] [--name "My candidate"] [--out path.json]
 *
 * `--command` is split on whitespace into a program and its fixed leading
 * arguments (no shell, no quoting support); `--board/--next/--rise` are
 * appended for every move.
 */
import { execFileSync } from "node:child_process";
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { validateScriptedRound, type ScriptedRound } from "./rounds.ts";
import { playScriptedGame, type BenchGameResult } from "./runner.ts";
import type { BenchPolicy } from "./policies.ts";
import type { GameState } from "../core/typescript/engine.ts";

const HERE = dirname(fileURLToPath(import.meta.url));
const ROUNDS_DIR = join(HERE, "rounds");

interface CliOptions {
  program: string;
  baseArgs: string[];
  roundIds: string[];
  id: string;
  name: string;
  family: string;
  outPath: string | null;
}

function parseArgs(argv: readonly string[]): CliOptions {
  const values = new Map<string, string>();
  for (let i = 0; i < argv.length; i += 1) {
    const token = argv[i];
    if (!token.startsWith("--")) throw new Error(`unexpected argument ${token}`);
    const value = argv[++i];
    if (value === undefined) throw new Error(`${token} requires a value`);
    values.set(token, value);
  }
  const command = values.get("--command");
  if (!command) throw new Error("--command is required");
  const [program, ...baseArgs] = command.trim().split(/\s+/);
  const roundIds = (values.get("--rounds") ?? "gauntlet-01").split(",").map((id) => id.trim());
  return {
    program,
    baseArgs,
    roundIds,
    id: values.get("--id") ?? "external",
    name: values.get("--name") ?? "External candidate (demo)",
    family: values.get("--family") ?? "external",
    outPath: values.get("--out") ?? null,
  };
}

function loadRound(id: string): ScriptedRound {
  const path = join(ROUNDS_DIR, `${id}.json`);
  return validateScriptedRound(JSON.parse(readFileSync(path, "utf8")));
}

/**
 * A policy backed by an external process. `chooseColumn` runs synchronously
 * (playScriptedGame's loop is synchronous), so each move spawns the command
 * fresh rather than holding an interactive session open; for a single
 * scripted-round game (at most a few hundred moves) this costs a few seconds
 * at most, not minutes.
 */
function externalPolicy(options: CliOptions): BenchPolicy {
  return {
    id: options.id,
    name: options.name,
    family: options.family,
    description: "Demo-only bridge to an external decision-query command; not a registry policy.",
    researchPath: "/approaches/lifetime-objective/nnue-evolution",
    publicInformation: true, // enforced structurally: only board/next/rise ever cross the process boundary
    chooseColumn(state: GameState): number | null {
      const board = state.board.join("");
      const output = execFileSync(options.program, [
        ...options.baseArgs,
        "--board", board,
        "--next", String(state.nextDisc),
        "--rise", String(state.movesRemaining),
      ]).toString().trim();
      const column = Number.parseInt(output, 10);
      return Number.isInteger(column) && column >= 0 && column <= 6 ? column : null;
    },
  };
}

function summarize(result: BenchGameResult, wallSeconds: number) {
  return {
    policyId: result.policyId,
    roundId: result.roundId,
    score: result.score,
    moves: result.moves,
    censored: result.censored,
    maxChain: result.maxChain,
    discsCleared: result.discsCleared,
    coveredRevealed: result.coveredRevealed,
    illegalMoves: result.illegalMoves,
    checksum: result.checksum,
    wallSeconds,
  };
}

function main() {
  const options = parseArgs(process.argv.slice(2));
  const policy = externalPolicy(options);
  const results = options.roundIds.map((roundId) => {
    const round = loadRound(roundId);
    const started = Date.now();
    const result = playScriptedGame(policy, round);
    return summarize(result, (Date.now() - started) / 1000);
  });
  const output = results.length === 1 ? results[0] : results;
  const text = JSON.stringify(output, null, 2);
  if (options.outPath) {
    writeFileSync(options.outPath, `${text}\n`);
  }
  console.log(text);
}

// Run only when executed directly, not when imported.
if (process.argv[1] && import.meta.url.endsWith(process.argv[1].replace(/\\/g, "/"))) {
  main();
}
