/**
 * Generates web/content/learn/data/leaf-scenarios.json, the data behind the
 * figures on /learn/concepts/evaluating-a-board.
 *
 * Two things are produced, both from boards the repository's TypeScript engine
 * actually reached in toy games (web/scripts/toy-policy.ts) — nothing here is
 * hand-drawn:
 *
 *  1. `xray` — one real position, annotated with the *kinds* of thing the fair
 *     leaf evaluator measures: column heights, the danger height, the rise
 *     clock, which cells are covered, which numbered discs are one drop from
 *     clearing (and with which disc, in which column), and what the covered row
 *     underneath would set off if it rose right now. Every annotation is
 *     recomputed with the engine's own `placeDisc`, `findPoppers` and
 *     `raiseCoveredRow`; none of it uses the evaluator's weights.
 *
 *  2. `pair` — two positions with the *same* column heights, the same number of
 *     covered cells and the same number of numbered discs, whose futures differ.
 *     "Future" is measured by replaying each board forward with the toy policy
 *     under the same fixed set of disc tapes and reporting how long it survived.
 *     That is a demonstration of the status summary's "static board potential is
 *     insufficient", not a research measurement: the policy is a toy and the
 *     sample is small.
 *
 *   cd web && node --experimental-strip-types scripts/generate-leaf-scenarios.ts
 */
import { writeFileSync } from "node:fs";
import { join } from "node:path";
import {
  BOARD_SIZE,
  contiguousLineLength,
  findPoppers,
  legalColumns,
  placeDisc,
  raiseCoveredRow,
  type Board,
  type DiscValue,
} from "../../src/core/typescript/engine.ts";
import { columnHeights, occupiedCount, playToyGame } from "./toy-policy.ts";

const DISCS: DiscValue[] = [1, 2, 3, 4, 5, 6, 7];
const HARVEST_GAMES = 24;
const ROLLOUT_TAPES = 16;
const MAX_PAIR_BOARDS = 80;

function boardOf(cells: string): Board {
  return cells.split("").map(Number) as unknown as Board;
}

interface Annotated {
  board: string;
  heights: number[];
  maxHeight: number;
  /** How far the tallest column is above the evaluator's danger line of four. */
  dangerHeight: number;
  occupied: number;
  solid: number[];
  cracked: number[];
  numbered: number[];
  /** Discs already on the board that some legal single drop would clear now. */
  oneDrop: number[];
  /** Columns where a drop clears the dropped disc itself. */
  selfClearColumns: number[];
  /** For each column, the disc values whose drop there clears something. */
  triggers: { column: number; discs: number[] }[];
  /** Numbered discs already sitting in a run longer than their own number. */
  overRun: number[];
  /** Discs the covered row underneath would set off if the board rose now. */
  risePoppers: number;
  riseBlocked: boolean;
}

function annotate(cells: string): Annotated {
  const board = boardOf(cells);
  const heights = columnHeights(board);
  const maxHeight = Math.max(...heights);
  const solid: number[] = [];
  const cracked: number[] = [];
  const numbered: number[] = [];
  for (let index = 0; index < board.length; index += 1) {
    const cell = board[index];
    if (cell === 8) solid.push(index);
    else if (cell === 9) cracked.push(index);
    else if (cell !== 0) numbered.push(index);
  }

  const oneDrop = new Set<number>();
  const selfClearColumns = new Set<number>();
  const triggers: { column: number; discs: number[] }[] = [];
  for (const column of legalColumns(board)) {
    const discs: number[] = [];
    for (const disc of DISCS) {
      const placed = placeDisc(board, column, disc);
      if (!placed) continue;
      const poppers = findPoppers(placed);
      if (poppers.length === 0) continue;
      discs.push(disc);
      for (const index of poppers) {
        // A popper on a cell that was empty before the drop is the dropped disc
        // itself clearing; everything else is a disc already on the board.
        if (board[index] === 0) selfClearColumns.add(column);
        else oneDrop.add(index);
      }
    }
    if (discs.length > 0) triggers.push({ column, discs });
  }

  // A numbered disc whose row run and column run are both already longer than
  // its number can never clear where it stands; the evaluator's "dead low
  // number" and "runs of twos" terms are about exactly this shape.
  const overRun: number[] = [];
  for (const index of numbered) {
    const row = Math.floor(index / BOARD_SIZE);
    const column = index % BOARD_SIZE;
    const value = board[index] as number;
    const horizontal = contiguousLineLength(board, row, column, "row");
    const vertical = contiguousLineLength(board, row, column, "column");
    if (horizontal > value && vertical > value) overRun.push(index);
  }

  const raised = raiseCoveredRow(board);
  return {
    board: cells,
    heights,
    maxHeight,
    dangerHeight: Math.max(0, maxHeight - 4),
    occupied: occupiedCount(board),
    solid,
    cracked,
    numbered,
    oneDrop: [...oneDrop].sort((a, b) => a - b),
    selfClearColumns: [...selfClearColumns].sort((a, b) => a - b),
    triggers,
    overRun,
    risePoppers: raised ? findPoppers(raised).length : 0,
    riseBlocked: raised === null,
  };
}

/* ------------------------------------------------------------------ */
/* Harvest real positions                                              */
/* ------------------------------------------------------------------ */

interface Harvested { board: string; nextDisc: number; movesUntilRise: number; game: number; move: number }

const harvested: Harvested[] = [];
for (let game = 0; game < HARVEST_GAMES; game += 1) {
  const record = playToyGame({
    seed: 0x5eed_7000 + game,
    latentSeed: 0x5eed_7800 + game,
    maxMoves: 200,
  });
  for (let i = 0; i + 1 < record.history.length; i += 1) {
    const here = record.history[i];
    if (here.gameOver) break;
    harvested.push({
      board: here.board,
      nextDisc: record.history[i + 1].disc,
      movesUntilRise: here.movesUntilRise,
      game,
      move: here.move,
    });
  }
}

/* ------------------------------------------------------------------ */
/* 1. The x-rayed position                                             */
/* ------------------------------------------------------------------ */

function interest(position: Harvested, a: Annotated): number {
  if (a.riseBlocked) return -1;
  if (a.maxHeight < 4 || a.maxHeight > 6) return -1;
  if (a.solid.length + a.cracked.length < 6) return -1;
  if (a.numbered.length < 6) return -1;
  if (a.oneDrop.length < 2) return -1;
  const spread = Math.max(...a.heights) - Math.min(...a.heights);
  return (
    a.oneDrop.length * 3 +
    a.cracked.length * 4 +
    a.overRun.length * 2 +
    spread * 2 +
    (position.movesUntilRise <= 2 ? 6 : 0)
  );
}

let xray: (Annotated & { nextDisc: number; movesUntilRise: number; origin: string }) | null = null;
let bestInterest = 0;
for (const position of harvested) {
  const a = annotate(position.board);
  const score = interest(position, a);
  if (score > bestInterest) {
    bestInterest = score;
    xray = {
      ...a,
      nextDisc: position.nextDisc,
      movesUntilRise: position.movesUntilRise,
      origin: `toy game ${position.game}, after move ${position.move}`,
    };
  }
}
if (!xray) throw new Error("no suitable x-ray position was harvested");

/* ------------------------------------------------------------------ */
/* 2. Two look-alike boards with different futures                     */
/* ------------------------------------------------------------------ */

/**
 * Two boards share a signature when a summary of the board — its column
 * heights, how many covers are solid, how many are cracked, and the multiset of
 * numbered discs on it — is identical. Boards that match here differ only in
 * *where* the same discs sit.
 */
function signature(a: Annotated, cells: string): string {
  const values = a.numbered.map((index) => cells[index]).sort().join("");
  return `${a.heights.join(",")}|${a.solid.length}|${a.cracked.length}|${values}`;
}

const groups = new Map<string, { cells: string; annotated: Annotated }[]>();
for (const position of harvested) {
  const a = annotate(position.board);
  if (a.maxHeight < 3 || a.riseBlocked) continue;
  const key = signature(a, position.board);
  const group = groups.get(key) ?? [];
  if (!group.some((entry) => entry.cells === position.board)) {
    group.push({ cells: position.board, annotated: a });
  }
  groups.set(key, group);
}

const candidates: { cells: string; annotated: Annotated }[] = [];
for (const group of groups.values()) {
  if (group.length < 2) continue;
  for (const entry of group.slice(0, 4)) candidates.push(entry);
  if (candidates.length >= MAX_PAIR_BOARDS) break;
}

/** Replays a board forward with the toy policy under a fixed set of tapes. */
function future(cells: string) {
  const board = boardOf(cells);
  const moves: number[] = [];
  const scores: number[] = [];
  for (let tape = 0; tape < ROLLOUT_TAPES; tape += 1) {
    const record = playToyGame({
      seed: 0x5eed_9000 + tape,
      latentSeed: 0x5eed_9800 + tape,
      maxMoves: 200,
      board,
      history: false,
    });
    moves.push(record.moves);
    scores.push(record.score);
  }
  const mean = (values: number[]) => values.reduce((a, b) => a + b, 0) / values.length;
  return {
    tapes: ROLLOUT_TAPES,
    meanMoves: mean(moves),
    meanScore: mean(scores),
    minMoves: Math.min(...moves),
    maxMoves: Math.max(...moves),
    /** Per-tape lifetimes, so the two boards can be compared tape by tape. */
    moves,
    scores,
  };
}

const measured = candidates.map((entry) => ({ ...entry, future: future(entry.cells) }));
const byKey = new Map<string, typeof measured>();
for (const entry of measured) {
  const key = signature(entry.annotated, entry.cells);
  byKey.set(key, [...(byKey.get(key) ?? []), entry]);
}

/** Tape-by-tape comparison of two boards replayed under the same tapes. */
function paired(a: number[], b: number[]) {
  let wins = 0;
  let ties = 0;
  let losses = 0;
  for (let i = 0; i < a.length; i += 1) {
    if (a[i] > b[i]) wins += 1;
    else if (a[i] === b[i]) ties += 1;
    else losses += 1;
  }
  return { wins, ties, losses };
}

let pair: {
  a: typeof measured[number];
  b: typeof measured[number];
  paired: { wins: number; ties: number; losses: number };
} | null = null;
let bestRank = -Infinity;
for (const group of byKey.values()) {
  for (let i = 0; i < group.length; i += 1) {
    for (let j = i + 1; j < group.length; j += 1) {
      const [first, second] =
        group[i].future.meanMoves >= group[j].future.meanMoves
          ? [group[i], group[j]]
          : [group[j], group[i]];
      if (first.annotated.oneDrop.length !== second.annotated.oneDrop.length) continue;
      const comparison = paired(first.future.moves, second.future.moves);
      if (comparison.wins <= comparison.losses) continue;
      // Prefer a difference that holds tape by tape, not one long lucky game.
      const rank =
        (comparison.wins - comparison.losses) * 4 +
        (first.future.meanMoves - second.future.meanMoves);
      if (rank > bestRank) {
        bestRank = rank;
        pair = { a: first, b: second, paired: comparison };
      }
    }
  }
}
if (!pair) throw new Error("no look-alike pair was found");

/* ------------------------------------------------------------------ */

const document = {
  generatedBy: "web/scripts/generate-leaf-scenarios.ts",
  engine: "src/core/typescript/engine.ts (latent mode)",
  note:
    "Positions harvested from toy-policy games; annotations recomputed with the engine. " +
    "The rollouts use the toy policy and are a demonstration, not a research measurement.",
  harvest: { games: HARVEST_GAMES, positions: harvested.length, seedDomain: "0x5eed****" },
  xray,
  pair: {
    rollout: {
      policy: "toy-greedy (web/scripts/toy-policy.ts)",
      tapes: ROLLOUT_TAPES,
      moveCap: 200,
      seedDomain: "0x5eed****",
    },
    paired: pair.paired,
    better: { board: pair.a.cells, ...pair.a.annotated, future: pair.a.future },
    worse: { board: pair.b.cells, ...pair.b.annotated, future: pair.b.future },
  },
};

const target = join(import.meta.dirname, "..", "content", "learn", "data", "leaf-scenarios.json");
writeFileSync(target, JSON.stringify(document, null, 1) + "\n");
console.log(
  `xray: ${xray.origin}, heights ${xray.heights.join("")}, one-drop ${xray.oneDrop.length}, ` +
    `covered ${xray.solid.length + xray.cracked.length}, rise poppers ${xray.risePoppers}`,
);
console.log(
  `pair: heights ${pair.a.annotated.heights.join("")} — ` +
    `${pair.a.future.meanMoves.toFixed(1)} vs ${pair.b.future.meanMoves.toFixed(1)} mean moves ` +
    `over ${ROLLOUT_TAPES} tapes (${pair.paired.wins}W ${pair.paired.ties}T ${pair.paired.losses}L)`,
);
console.log("wrote", target);
