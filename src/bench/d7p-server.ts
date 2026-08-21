/**
 * A stdin/stdout server speaking the Drop7 Policy Protocol (D7P), the
 * UCI-style text interface documented in docs/d7p-protocol.md. It wraps one
 * registry policy so any benchmark harness — in any language — can drive it:
 *
 *   node --experimental-strip-types src/bench/d7p-server.ts --policy greedy
 *
 * Protocol summary:
 *   -> d7p                                  <- id name / id family / d7pok
 *   -> isready                              <- readyok
 *   -> position board <49ch> next <1-7> rise <1-5>   (or "position startpos ...")
 *   -> go                                   <- bestmove <0-6>
 *   -> quit
 */
import { createInterface } from "node:readline";
import {
  BOARD_SIZE,
  MOVES_PER_LEVEL,
  createInitialBoard,
  legalColumns,
  type Board,
  type Cell,
  type DiscValue,
  type GameState,
} from "../core/typescript/engine.ts";
import { getPolicy } from "./policies.ts";

export interface D7pPosition {
  board: Board;
  nextDisc: DiscValue;
  movesRemaining: number;
}

export function parsePosition(tokens: readonly string[]): D7pPosition {
  let board: Board | null = null;
  let nextDisc: DiscValue | null = null;
  let movesRemaining: number | null = null;
  const rest = [...tokens];
  if (rest[0] === "startpos") {
    board = createInitialBoard();
    rest.shift();
  }
  while (rest.length > 0) {
    const token = rest.shift();
    if (token === "board") {
      const encoded = rest.shift() ?? "";
      if (encoded.length !== BOARD_SIZE * BOARD_SIZE) {
        throw new Error("board must be 49 characters");
      }
      const cells = [...encoded].map((char) => Number(char) as Cell);
      if (
        cells.some(
          (cell) =>
            !Number.isInteger(cell) || cell < 0 || cell > 9,
        )
      ) {
        throw new Error("board cells must be digits 0-9");
      }
      board = cells;
    } else if (token === "next") {
      const value = Number(rest.shift());
      if (!Number.isInteger(value) || value < 1 || value > BOARD_SIZE) {
        throw new Error("next must be a disc value 1-7");
      }
      nextDisc = value as DiscValue;
    } else if (token === "rise") {
      const value = Number(rest.shift());
      if (!Number.isInteger(value) || value < 1 || value > MOVES_PER_LEVEL) {
        throw new Error(`rise must be 1-${MOVES_PER_LEVEL}`);
      }
      movesRemaining = value;
    } else {
      throw new Error(`unknown position token: ${token}`);
    }
  }
  if (!board || nextDisc === null || movesRemaining === null) {
    throw new Error("position needs board (or startpos), next, and rise");
  }
  return { board, nextDisc, movesRemaining };
}

export function stateFromPosition(position: D7pPosition): GameState {
  return {
    board: position.board,
    nextDisc: position.nextDisc,
    score: 0,
    level: 1,
    movesRemaining: position.movesRemaining,
    movesPlayed: 0,
    gameOver: legalColumns(position.board).length === 0,
  };
}

function parsePolicyArg(argv: readonly string[]): string {
  const index = argv.indexOf("--policy");
  if (index === -1 || !argv[index + 1]) {
    throw new Error("usage: d7p-server.ts --policy <id>");
  }
  return argv[index + 1];
}

function main() {
  const policy = getPolicy(parsePolicyArg(process.argv.slice(2)));
  let position: D7pPosition | null = null;
  const send = (line: string) => process.stdout.write(`${line}\n`);

  const rl = createInterface({ input: process.stdin, terminal: false });
  rl.on("line", (line) => {
    const tokens = line.trim().split(/\s+/).filter(Boolean);
    const command = tokens[0];
    try {
      if (command === "d7p") {
        send(`id name ${policy.name}`);
        send(`id family ${policy.family}`);
        send(`id public-information ${policy.publicInformation ? "true" : "false"}`);
        send("d7pok");
      } else if (command === "isready") {
        send("readyok");
      } else if (command === "position") {
        position = parsePosition(tokens.slice(1));
      } else if (command === "go") {
        if (!position) throw new Error("no position");
        const column = policy.chooseColumn(stateFromPosition(position));
        send(`bestmove ${column ?? "none"}`);
      } else if (command === "quit") {
        rl.close();
      } else if (command) {
        send(`info unknown command ${command}`);
      }
    } catch (error) {
      send(`info error ${(error as Error).message}`);
    }
  });
}

// Run only when executed directly, not when imported by tests.
if (process.argv[1] && import.meta.url.endsWith(process.argv[1].replace(/\\/g, "/"))) {
  main();
}
