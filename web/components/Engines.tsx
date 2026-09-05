/**
 * Diagrams for the Engines section (web/app/engine). Server components:
 * plain SVG drawn from tokens, with CSS motion declared in
 * web/app/engine/engines.css. Every animated element carries `data-anim`,
 * and the SVG attributes describe the resting frame, so the picture is
 * complete when motion is off (prefers-reduced-motion).
 *
 * Every board here is engine output. The cascade frames come from the
 * fast-engine page (approaches/lifetime-objective/fast-engine/README.mdx),
 * produced by the TypeScript engine with a latent board; the transposition
 * boards and the disc tape were produced with src/core/typescript/engine.ts
 * (`playMove` from `createInitialBoard`) and `headlessDisc` from
 * src/core/typescript/headless.ts, and are quoted here as strings.
 *
 * No component computes or infers a research number. Numbers appear only in
 * captions, each with its source.
 */
import Link from "next/link";
import type { ReactNode } from "react";
import { CellGlyph, parseBoard } from "@/components/discs";
import { readRepoFile } from "@/lib/repo";
import { FINDING_02, FINDING_13, REPRODUCIBILITY, RS_RUST } from "@/lib/engines";

const MONO = "var(--font-mono)";
const INK = "var(--color-ink)";
const INK_2 = "var(--color-ink-2)";
const INK_3 = "var(--color-ink-3)";
const RULE = "var(--color-rule)";
const RULE_STRONG = "var(--color-rule-strong)";
const SURFACE = "var(--color-surface)";
const RAISED = "var(--color-raised)";
const BG = "var(--color-bg)";
const ACCENT = "var(--color-accent)";
const ACCENT_SOFT = "var(--color-accent-soft)";
const SERIES_1 = "var(--color-series-1)";
const SERIES_3 = "var(--color-series-3)";
const HIGHLIGHT = "var(--color-highlight)";

/* ---- shared pieces ---- */

function Frame({
  className,
  label,
  caption,
  children,
}: {
  className?: string;
  label: string;
  caption: ReactNode;
  children: ReactNode;
}) {
  return (
    <figure className={["fig", "engine-diagram", className].filter(Boolean).join(" ")}>
      <div className="fig-frame" role="img" aria-label={label}>
        {children}
      </div>
      <figcaption>{caption}</figcaption>
    </figure>
  );
}

/** A 7 by 7 board of CellGlyphs with optional column highlights. */
function Board7({
  cells,
  x,
  y,
  s,
  litColumns = [],
  skip = [],
}: {
  cells: string;
  x: number;
  y: number;
  s: number;
  litColumns?: readonly number[];
  /** Cell indexes drawn elsewhere (for example, animated separately). */
  skip?: readonly number[];
}) {
  const board = parseBoard(cells);
  return (
    <g transform={`translate(${x} ${y})`}>
      <rect width={7 * s} height={7 * s} rx={s * 0.15} fill={BG} stroke={RULE} />
      {litColumns.map((column) => (
        <rect key={column} x={column * s} y={0} width={s} height={7 * s} fill={ACCENT_SOFT} data-anim="lit" />
      ))}
      {board.map((cell, index) =>
        skip.includes(index) ? null : (
          <CellGlyph key={index} cell={cell} x={(index % 7) * s} y={Math.floor(index / 7) * s} s={s} />
        ),
      )}
    </g>
  );
}

function Label({
  x,
  y,
  children,
  size = 10.5,
  fill = INK_3,
  anchor = "start",
  weight,
}: {
  x: number;
  y: number;
  children: ReactNode;
  size?: number;
  fill?: string;
  anchor?: "start" | "middle" | "end";
  weight?: number;
}) {
  return (
    <text x={x} y={y} fontFamily={MONO} fontSize={size} fill={fill} textAnchor={anchor} fontWeight={weight}>
      {children}
    </text>
  );
}

function Arrow({ d }: { d: string }) {
  return (
    <path d={d} fill="none" stroke={INK_3} strokeWidth={1.2} strokeLinecap="round" strokeLinejoin="round" />
  );
}

/* =========================================================================
 * 1. One position, two memories: the board and the Rust engine's seven words.
 * ========================================================================= */

/** Frame 1 of the recorded cascade on the fast-engine page (TypeScript engine output). */
const SCAN_POSITION = "0000000000000000000000000000000000000890000333080";
const LIT_COLUMN = 2;

export function BitboardColumns({ caption }: { caption?: ReactNode }) {
  const s = 28;
  const boardX = 8;
  const boardY = 26;
  const board = parseBoard(SCAN_POSITION);
  const wordX = 262;
  const wordY = 30;
  const nibW = 34;
  const nibH = 22;
  const gap = 2;
  const rowStep = 26;
  const hexDigit = (cell: number) => cell.toString(16).toUpperCase();
  return (
    <Frame
      label="The same board stored as a 7 by 7 grid and as seven 32-bit column words"
      caption={
        caption ?? (
          <>
            The same recorded position twice: as the 7 by 7 board every engine agrees on, and as the Rust engine
            stores it, seven 32-bit words with four bits per cell and the bottom row in the lowest nibble, 28 bytes in
            all (<SourceRef source={RS_RUST} />, memoryBytes.rustBoard). Column 3 is lit in both; its two occupied
            cells are the two low nibbles of its word.
          </>
        )
      }
    >
      <svg viewBox="0 0 560 232">
        <Label x={boardX} y={14}>
          the board, row-major from the top
        </Label>
        <Board7 cells={SCAN_POSITION} x={boardX} y={boardY} s={s} litColumns={[LIT_COLUMN]} />
        {Array.from({ length: 7 }, (_, c) => (
          <Label key={c} x={boardX + c * s + s / 2} y={boardY + 7 * s + 14} anchor="middle" size={9}>
            c{c + 1}
          </Label>
        ))}

        <Label x={wordX - 30} y={14}>
          seven u32 column words, four bits a cell
        </Label>
        {Array.from({ length: 7 }, (_, c) => {
          const y = wordY + c * rowStep;
          const lit = c === LIT_COLUMN;
          const nibbles = Array.from({ length: 7 }, (_, r) => board[r * 7 + c]);
          return (
            <g key={c}>
              {lit && (
                <rect
                  x={wordX - 6}
                  y={y - 3}
                  width={8 * (nibW + gap) + 8}
                  height={nibH + 6}
                  rx={4}
                  fill={ACCENT_SOFT}
                  data-anim="lit"
                />
              )}
              <Label x={wordX - 12} y={y + nibH / 2 + 3.5} anchor="end" size={9} fill={lit ? INK : INK_3}>
                c{c + 1}
              </Label>
              {/* bits 31..28: unused */}
              <rect
                x={wordX}
                y={y}
                width={nibW}
                height={nibH}
                rx={3}
                fill="none"
                stroke={RULE_STRONG}
                strokeDasharray="3 3"
              />
              {nibbles.map((cell, r) => {
                const x = wordX + (r + 1) * (nibW + gap);
                const occupied = cell !== 0;
                return (
                  <g key={r}>
                    <rect
                      x={x}
                      y={y}
                      width={nibW}
                      height={nibH}
                      rx={3}
                      fill={occupied ? RAISED : SURFACE}
                      stroke={occupied && lit ? SERIES_1 : RULE_STRONG}
                      strokeWidth={occupied && lit ? 1.5 : 1}
                    />
                    <text
                      x={x + nibW / 2}
                      y={y + nibH / 2 + 3.5}
                      textAnchor="middle"
                      fontFamily={MONO}
                      fontSize={11}
                      fontWeight={occupied ? 600 : 400}
                      fill={occupied ? INK : INK_3}
                    >
                      {hexDigit(cell)}
                    </text>
                  </g>
                );
              })}
            </g>
          );
        })}
        <Label x={wordX + nibW / 2} y={wordY + 7 * rowStep + 6} anchor="middle" size={9}>
          31..28
        </Label>
        <Label x={wordX + (nibW + gap) + nibW / 2} y={wordY + 7 * rowStep + 6} anchor="middle" size={9}>
          top row
        </Label>
        <Label x={wordX + 7 * (nibW + gap) + nibW / 2} y={wordY + 7 * rowStep + 6} anchor="middle" size={9}>
          bottom row
        </Label>
        <Label x={boardX} y={224}>
          0 empty · 1..7 numbered · 8 solid gray · 9 cracked gray
        </Label>
      </svg>
    </Frame>
  );
}

/* =========================================================================
 * 2. Gravity on the columns that changed, in place, in one step.
 * ========================================================================= */

/** Frames 3 and 4 of the recorded cascade on the fast-engine page. */
const BEFORE_GRAVITY = "0000000000000000000000000000000000000910000000080";
const AFTER_GRAVITY = "0000000000000000000000000000000000000000000091080";
/** Cells that move in the step: the 9 and the 1 fall from row 6 to row 7 (indexes 44 and 45 after). */
const FALLING_AFTER = [44, 45] as const;
const CHANGED_COLUMNS = [2, 3] as const;
const COLUMN_MASK = [0, 0, 1, 1, 0, 0, 0] as const;

export function GravityWave({ caption }: { caption?: ReactNode }) {
  const s = 26;
  const leftX = 8;
  const rightX = 370;
  const boardY = 30;
  const after = parseBoard(AFTER_GRAVITY);
  const maskX = 214;
  const maskY = 96;
  const maskW = 18;
  return (
    <Frame
      label="A column before and after in-place gravity, with the mask of columns that lost a disc"
      caption={
        caption ?? (
          <>
            Frames 3 and 4 of the recorded cascade on the{" "}
            <Link href="/engine/fast">fast-engine page</Link>. After wave 1 cleared the three 3s, only columns 3
            and 4 lost a disc, so the fast engine compacts those two columns in place and in one step; the other five
            are provably unchanged and are never read (fast-engine.hpp, gravity on popped columns only). The
            reference engine rebuilds all seven columns into a new array every time.
          </>
        )
      }
    >
      <svg viewBox="0 0 560 250">
        <Label x={leftX} y={16}>
          before: wave 1 cleared the three 3s
        </Label>
        <Board7 cells={BEFORE_GRAVITY} x={leftX} y={boardY} s={s} litColumns={CHANGED_COLUMNS} />

        <Label x={rightX} y={16}>
          after: gravity on columns 3 and 4 only
        </Label>
        <Board7 cells={AFTER_GRAVITY} x={rightX} y={boardY} s={s} litColumns={CHANGED_COLUMNS} skip={FALLING_AFTER} />
        <g transform={`translate(${rightX} ${boardY})`}>
          {FALLING_AFTER.map((index) => (
            <g key={index} data-anim="fall">
              <CellGlyph cell={after[index]} x={(index % 7) * s} y={Math.floor(index / 7) * s} s={s} />
            </g>
          ))}
        </g>

        <Label x={maskX + 3.5 * (maskW + 2)} y={maskY - 10} anchor="middle" size={9.5}>
          columns that lost a disc
        </Label>
        {COLUMN_MASK.map((bit, i) => (
          <g key={i}>
            <rect
              x={maskX + i * (maskW + 2)}
              y={maskY}
              width={maskW}
              height={maskW}
              rx={3}
              fill={bit ? SERIES_1 : SURFACE}
              stroke={bit ? SERIES_1 : RULE_STRONG}
            />
            <text
              x={maskX + i * (maskW + 2) + maskW / 2}
              y={maskY + maskW / 2 + 3.5}
              textAnchor="middle"
              fontFamily={MONO}
              fontSize={10}
              fontWeight={600}
              fill={bit ? "var(--color-accent-fg)" : INK_3}
            >
              {bit}
            </text>
          </g>
        ))}
        <Arrow d={`M${maskX + 20},${maskY + 48} h${7 * (maskW + 2) - 44} m-5,-4 l5,4 l-5,4`} />
        <Label x={maskX + 3.5 * (maskW + 2)} y={maskY + 68} anchor="middle" size={9.5}>
          compact in place
        </Label>
        <Label x={maskX + 3.5 * (maskW + 2)} y={maskY + 82} anchor="middle" size={9.5}>
          two columns, one pass each
        </Label>

        <Label x={leftX} y={242}>
          the other five columns are not touched
        </Label>
      </svg>
    </Frame>
  );
}

/* =========================================================================
 * 3. Two move orders, one board, one table slot.
 * ========================================================================= */

/**
 * Produced with the TypeScript engine from the Hardcore opening board:
 * lane A plays a 4 into column 2 then a 5 into column 5; lane B plays the
 * same two drops the other way round. Neither drop pops, so both lanes reach
 * the same board with the same moves left.
 */
const TT_START = "0000000000000000000000000000000000000000008888888";
const TT_A_MID = "0000000000000000000000000000000000004000008888888";
const TT_B_MID = "0000000000000000000000000000000000000005008888888";
const TT_END = "0000000000000000000000000000000000004005008888888";

export function TranspositionTable({ caption }: { caption?: ReactNode }) {
  const s = 12;
  const w = 7 * s;
  const laneY = [24, 140] as const;
  const boardX = [8, 128, 248] as const;
  const slotX = 400;
  const slotY = 86;
  const slotW = 150;
  const slotH = 90;
  const lane = (
    y: number,
    mid: string,
    firstMove: string,
    secondMove: string,
    name: string,
  ) => (
    <g>
      <Label x={boardX[0]} y={y - 8} size={9.5} fill={INK_2}>
        {name}
      </Label>
      <Board7 cells={TT_START} x={boardX[0]} y={y} s={s} />
      <Arrow d={`M${boardX[0] + w + 4},${y + w / 2} h${boardX[1] - boardX[0] - w - 8} m-5,-4 l5,4 l-5,4`} />
      <Label x={(boardX[0] + w + boardX[1]) / 2} y={y + w / 2 - 6} anchor="middle" size={9}>
        {firstMove}
      </Label>
      <Board7 cells={mid} x={boardX[1]} y={y} s={s} />
      <Arrow d={`M${boardX[1] + w + 4},${y + w / 2} h${boardX[2] - boardX[1] - w - 8} m-5,-4 l5,4 l-5,4`} />
      <Label x={(boardX[1] + w + boardX[2]) / 2} y={y + w / 2 - 6} anchor="middle" size={9}>
        {secondMove}
      </Label>
      <Board7 cells={TT_END} x={boardX[2]} y={y} s={s} />
    </g>
  );
  return (
    <Frame
      label="Two move orders reaching the same board, stored once in a transposition table"
      caption={
        caption ?? (
          <>
            Two move orders produced by the TypeScript engine from the opening board: a 4 into column 2 then a 5 into
            column 5, and the same two drops the other way round. They reach the same board with the same moves
            left, so the search stores the position once and the second arrival is a hit whose whole subtree is
            skipped. In the Rust engine a node hit rate of 0.013 at d4s7 cut the work from 11.9M to 6.3M units
            (<SourceRef source={RS_RUST} />, transpositionWorkReduction).
          </>
        )
      }
    >
      <svg viewBox="0 0 560 240">
        {lane(laneY[0], TT_A_MID, "4 into column 2", "5 into column 5", "order A")}
        {lane(laneY[1], TT_B_MID, "5 into column 5", "4 into column 2", "order B")}

        {/* both end boards point at one slot */}
        <Arrow d={`M${boardX[2] + w + 4},${laneY[0] + w / 2} L${slotX - 6},${slotY + 24} m-6,-1 l6,1 l-3,-5`} />
        <g data-anim="hit">
          <Arrow d={`M${boardX[2] + w + 4},${laneY[1] + w / 2} L${slotX - 6},${slotY + slotH - 24} m-6,1 l6,-1 l-3,5`} />
        </g>

        <rect x={slotX} y={slotY} width={slotW} height={slotH} rx={6} fill={SURFACE} stroke={RULE_STRONG} />
        <rect x={slotX} y={slotY} width={slotW} height={slotH} rx={6} fill={ACCENT_SOFT} data-anim="hit" />
        <Label x={slotX + 10} y={slotY + 18} size={9.5} fill={INK_2}>
          one table slot
        </Label>
        <Label x={slotX + 10} y={slotY + 36} size={9} fill={INK_3}>
          key: board · next disc
        </Label>
        <Label x={slotX + 10} y={slotY + 48} size={9} fill={INK_3}>
          · moves left · depth
        </Label>
        <Label x={slotX + 10} y={slotY + 68} size={9.5} fill={INK}>
          A: store the value
        </Label>
        <g data-anim="hit">
          <Label x={slotX + 10} y={slotY + 82} size={9.5} fill={ACCENT} weight={600}>
            B: hit, subtree skipped
          </Label>
        </g>

        <Label x={boardX[0]} y={232}>
          same cells, same moves left, same next disc: one key
        </Label>
      </svg>
    </Frame>
  );
}

/* =========================================================================
 * 4. The parity replay: one tape, two engines, a check per move.
 * ========================================================================= */

/** `headlessDisc(0x2d700000, move)` for moves 0..7, the first seed of the parity sweep. */
const TAPE = [5, 7, 2, 3, 1, 3, 7, 7] as const;
const TAPE_STEP = 34;

export function ParityReplay({ caption }: { caption?: ReactNode }) {
  const s = 28;
  const tapeX = 176;
  const tapeY = 18;
  const laneX = tapeX;
  const laneY = [86, 172] as const;
  const recW = 28;
  const recH = 30;
  const checkY = 140;
  const counterX = 452;
  const counts = Array.from({ length: TAPE.length + 1 }, (_, k) => k);
  const record = (x: number, y: number) => (
    <g>
      <rect x={x} y={y} width={recW} height={recH} rx={3} fill={RAISED} stroke={RULE_STRONG} />
      <path d={`M${x + 5},${y + 8} h18 M${x + 5},${y + 14} h12 M${x + 5},${y + 20} h16 M${x + 5},${y + 26} h9`} stroke={INK_3} strokeWidth={1.2} strokeLinecap="round" />
    </g>
  );
  return (
    <Frame
      className="engine-diagram--parity"
      label="A disc tape feeding two engines, a check after every move, and a count of matched moves"
      caption={
        caption ?? (
          <>
            The method behind every bit-identical claim on this site. One seed fixes the disc tape (here the first
            eight discs of seed 0x2d700000, the first seed of the parity sweep), both engines play the same column
            each move, and after every move each writes one record: points, score, level, moves left, game over,
            the waves and the 49-cell board. The records must match byte for byte, and the first difference stops
            the run. The TypeScript and C++ engines agreed on 256 seeded games and 6,852 moves (<SourceRef source={REPRODUCIBILITY} />); the scenario engine replayed 218,470 moves (<SourceRef source={FINDING_02} />), the fast engine 438,020 (<SourceRef source={FINDING_13} />) and the
            Rust engine 36,427 (<SourceRef source={RS_RUST} />) against the C++ engine with zero mismatches.
          </>
        )
      }
    >
      <svg viewBox="0 0 560 236">
        <Label x={8} y={tapeY + s / 2 + 4} size={9.5} fill={INK_2}>
          disc tape from one seed
        </Label>
        <rect x={tapeX + (TAPE.length - 1) * TAPE_STEP - 3} y={tapeY - 3} width={s + 6} height={s + 6} rx={6} fill="none" stroke={HIGHLIGHT} strokeWidth={1.5} data-anim="cursor" />
        {TAPE.map((disc, i) => (
          <CellGlyph key={i} cell={disc} x={tapeX + i * TAPE_STEP} y={tapeY} s={s} />
        ))}
        {TAPE.map((_, i) => (
          <Arrow key={i} d={`M${tapeX + i * TAPE_STEP + s / 2},${tapeY + s + 4} v14`} />
        ))}

        <Label x={8} y={laneY[0] + recH / 2 + 4} size={9.5} fill={INK_2}>
          TypeScript engine
        </Label>
        {TAPE.map((_, i) => (
          <g key={i}>{record(laneX + i * TAPE_STEP, laneY[0])}</g>
        ))}
        <Label x={8} y={laneY[1] + recH / 2 + 4} size={9.5} fill={INK_2}>
          C++ engine
        </Label>
        {TAPE.map((_, i) => (
          <g key={i}>{record(laneX + i * TAPE_STEP, laneY[1])}</g>
        ))}

        <Label x={8} y={checkY + 4} size={9.5} fill={INK_2}>
          record equal?
        </Label>
        {TAPE.map((_, i) => {
          const cx = laneX + i * TAPE_STEP + recW / 2;
          return (
            <g key={i} data-anim={`check-${i + 1}`}>
              <circle cx={cx} cy={checkY} r={8} fill="var(--color-status-completed-bg)" stroke={SERIES_3} />
              <path d={`M${cx - 4},${checkY} l3,3 l5,-6`} fill="none" stroke={SERIES_3} strokeWidth={1.6} strokeLinecap="round" strokeLinejoin="round" />
            </g>
          );
        })}

        <rect x={counterX - 8} y={checkY - 16} width={104} height={32} rx={4} fill={SURFACE} stroke={RULE_STRONG} />
        {counts.map((k) => (
          <text
            key={k}
            x={counterX + 44}
            y={checkY + 4}
            textAnchor="middle"
            fontFamily={MONO}
            fontSize={10.5}
            fontWeight={600}
            fill={INK}
            opacity={k === TAPE.length ? 1 : 0}
            data-anim={`count-${k}`}
          >
            {k} of {TAPE.length} matched
          </text>
        ))}

        <Label x={8} y={226} size={9.5}>
          one record per move: points · score · level · moves left · game over · waves · board
        </Label>
      </svg>
    </Frame>
  );
}

/* =========================================================================
 * Source references: where a record or document opens in the console.
 * ========================================================================= */

/** The console route for a source id or path, or null when it has no page of its own. */
export function sourceHref(source: string): string | null {
  if (source.startsWith("EX-")) return `/experiments/${source}`;
  if (source.startsWith("TH-")) return `/theories/${source}`;
  if (source.startsWith("RS-")) {
    const raw = readRepoFile(`research/results/${source}.json`);
    if (!raw) return null;
    try {
      const record = JSON.parse(raw) as { experimentId?: string };
      return record.experimentId ? `/experiments/${record.experimentId}#results` : null;
    } catch {
      return null;
    }
  }
  if (/^docs\/.+\.md$/.test(source)) return `/${source.replace(/\.md$/, "")}`;
  if (/^src\/[^\s]+$/.test(source)) return `/${source}`;
  if (/^approaches\/[a-z0-9-]+\/[a-z0-9-]+\/[^\s]+$/.test(source)) return `/${source}`;
  return null;
}

/** A source id or path, linked when the console has a page for it, otherwise as mono text. */
export function SourceRef({ source, label }: { source: string; label?: string }) {
  const href = sourceHref(source);
  return href ? <Link href={href}>{label ?? source}</Link> : <code>{label ?? source}</code>;
}
