/**
 * Lookup tables shared by the browser solver's fast move generator and leaf.
 *
 * A Drop7 row or column holds seven cells, so its occupancy is one of 128
 * patterns, and every position's run length, run start and run end follow from
 * that pattern alone (finding-13, O3 and L5). Computing them once here replaces
 * the per-cell rescans in `engine.ts` and `heuristic.ts` with indexed loads.
 *
 * Every table is filled with the same expression the reference uses, so a
 * table lookup returns the identical bit pattern. `fast-search.test.ts` checks
 * that claim against the reference on real positions.
 */

export const BOARD_SIZE = 7;
export const CELL_COUNT = BOARD_SIZE * BOARD_SIZE;

/** `RUN_LENGTH[mask * 7 + position]`: contiguous occupied run containing `position`, 0 when empty. */
export const RUN_LENGTH = new Uint8Array(128 * BOARD_SIZE);
/** First position of that run, -1 when `position` is empty. */
export const RUN_START = new Int8Array(128 * BOARD_SIZE).fill(-1);
/** Last position of that run, -1 when `position` is empty. */
export const RUN_END = new Int8Array(128 * BOARD_SIZE).fill(-1);
/** Number of occupied cells in a seven-cell mask. */
export const POPCOUNT = new Uint8Array(128);
/**
 * 1 when a column occupancy mask (bit r = row r, row 0 at the top) is already
 * settled, i.e. its occupied rows are contiguous from the bottom. Gravity is
 * the identity on such a column.
 */
export const SETTLED_COLUMN = new Uint8Array(128);

for (let mask = 0; mask < 128; mask += 1) {
  let count = 0;
  for (let bit = 0; bit < BOARD_SIZE; bit += 1) {
    if ((mask >> bit) & 1) count += 1;
  }
  POPCOUNT[mask] = count;
  SETTLED_COLUMN[mask] = mask === ((0x7f << (BOARD_SIZE - count)) & 0x7f) ? 1 : 0;

  let position = 0;
  while (position < BOARD_SIZE) {
    if (!((mask >> position) & 1)) {
      position += 1;
      continue;
    }
    const start = position;
    while (position < BOARD_SIZE && (mask >> position) & 1) position += 1;
    const end = position - 1;
    for (let cell = start; cell <= end; cell += 1) {
      RUN_LENGTH[mask * BOARD_SIZE + cell] = end - start + 1;
      RUN_START[mask * BOARD_SIZE + cell] = start;
      RUN_END[mask * BOARD_SIZE + cell] = end;
    }
  }
}

/** Points per disc for chain wave `depth`; identical to `scoreForWave`. */
const WAVE_SCORE_TABLE_SIZE = 64;
const WAVE_SCORE = new Float64Array(WAVE_SCORE_TABLE_SIZE + 1);
for (let depth = 1; depth <= WAVE_SCORE_TABLE_SIZE; depth += 1) {
  WAVE_SCORE[depth] = Math.floor(7 * depth ** 2.5);
}
export function waveScore(depth: number): number {
  return depth <= WAVE_SCORE_TABLE_SIZE
    ? WAVE_SCORE[depth]
    : Math.floor(7 * depth ** 2.5);
}

/** `2 ** (1 - cost)` for integer costs 1..49; identical to the heuristic's `readiness`. */
const READINESS = new Float64Array(CELL_COUNT + 1);
for (let cost = 1; cost <= CELL_COUNT; cost += 1) READINESS[cost] = 2 ** (1 - cost);
export function readiness(cost: number): number {
  if (cost < 1) return 0;
  return cost <= CELL_COUNT && Number.isInteger(cost)
    ? READINESS[cost]
    : 2 ** (1 - cost);
}
