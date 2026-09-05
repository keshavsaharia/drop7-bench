/** Recorded presentation data. The player never runs the engine or invents scores. */
export type BoardFrameKind = "ready" | "drop" | "match" | "burst" | "impact" | "settle" | "rise" | "done";

export interface BoardRun { start: number; end: number; length: number }

export interface BoardFrame {
  kind: BoardFrameKind;
  board: string;
  indexes: number[];
  /** Vertical travel in cells, keyed by destination cell. */
  travel: Record<number, number>;
  score: number;
  points: number;
  depth?: number;
  label: string;
  runs?: BoardRun[];
}

export interface BoardClip {
  initial: string;
  nextDisc: number;
  column: number;
  points: number;
  waves: { depth: number; cleared: number; points: number }[];
  frames: BoardFrame[];
}

export interface ChoiceLessonData {
  source: string;
  board: string;
  nextDisc: number;
  columns: {
    column: number;
    move: BoardClip;
    replies: { disc: number; move: BoardClip }[];
    replyAverage: number;
    fair: number;
    optimistic: number;
    pessimistic: number;
  }[];
}

/** Preserve each disc's order when gravity closes holes, including duplicate values. */
export function gravityTravel(before: string, after: string): Record<number, number> {
  const travel: Record<number, number> = {};
  for (let column = 0; column < 7; column += 1) {
    const source: number[] = [];
    const destination: number[] = [];
    for (let row = 6; row >= 0; row -= 1) {
      if (before[row * 7 + column] !== "0") source.push(row);
      if (after[row * 7 + column] !== "0") destination.push(row);
    }
    if (source.length !== destination.length) throw new Error("Gravity changed the disc count");
    destination.forEach((row, i) => {
      if (before[source[i] * 7 + column] !== after[row * 7 + column]) {
        throw new Error("Gravity changed the order of the discs");
      }
      if (row !== source[i]) travel[row * 7 + column] = source[i] - row;
    });
  }
  return travel;
}

export function lessonNumber(value: number): string {
  return Number.isInteger(value) ? value.toLocaleString("en-US") : value.toFixed(1);
}
