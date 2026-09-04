import Link from "next/link";
import type { ReactNode } from "react";
import { CellGlyph, parseBoard } from "./discs";
import styles from "./RustBitboardFigures.module.css";

/**
 * The recorded cascade already used by the fast-engine page. It was produced
 * by the TypeScript rules engine with a fixed latent board. The Rust trajectory
 * gate proves that the packed engine emits the same boards and wave record.
 */
const TRACE = {
  placed: "0000000000000000000000000000000000000890000333080",
  written: "0000000000000000000000000000000000000910000000080",
  compacted: "0000000000000000000000000000000000000000000091080",
  settled: "0000000000000000000000000000000000000000000040080",
} as const;

const POPPING = [43, 44, 45] as const;
const COVERED = [37, 38, 47] as const;
const CRACKED = [38] as const;

const SHIFT_MASKS = [
  { label: "up = popping >> 7", hex: "0x7000000000", set: [36, 37, 38] },
  { label: "down = (popping << 7) & BOARD_MASK", hex: "0x0", set: [] },
  { label: "left = (popping & NOT_COL0) >> 1", hex: "0x1c0000000000", set: [42, 43, 44] },
  { label: "right = (popping & NOT_COL6) << 1", hex: "0x700000000000", set: [44, 45, 46] },
] as const;

const HIT_PLANES = [
  { label: "ones", hex: "0x6c7000000000", set: [36, 37, 38, 42, 43, 45, 46] },
  { label: "twos", hex: "0x100000000000", set: [44] },
  { label: "fours", hex: "0x0", set: [] },
] as const;

const TEST_GRAVITY = {
  before: [0, 3, 0, 5, 0, 0, 1],
  flags: [0, 1, 0, 1, 0, 0, 1],
  mask: [0, "F", 0, "F", 0, 0, "F"],
  after: [0, 0, 0, 0, 3, 5, 1],
  raised: [0, 0, 0, 3, 5, 1, 8],
} as const;

const BOARD_SOURCE = "/approaches/fair-expectimax/rust-engine/src/board.rs";
const CASCADE_SOURCE = "/approaches/fair-expectimax/rust-engine/src/engine.rs";

function columnWords(cells: string): number[] {
  const board = parseBoard(cells);
  return Array.from({ length: 7 }, (_, column) => {
    let word = 0;
    for (let row = 0; row < 7; row += 1) {
      word = (word | (board[row * 7 + column] << (4 * (6 - row)))) >>> 0;
    }
    return word;
  });
}

function hex32(value: number): string {
  return `0x${value.toString(16).padStart(8, "0")}`;
}

function wordValues(value: number): number[] {
  return Array.from({ length: 7 }, (_, row) => (value >>> (4 * (6 - row))) & 0xf);
}

function FigureShell({
  eyebrow,
  title,
  label,
  caption,
  className,
  children,
}: {
  eyebrow: string;
  title: string;
  label: string;
  caption: ReactNode;
  className?: string;
  children: ReactNode;
}) {
  return (
    <figure className={[styles.figure, className].filter(Boolean).join(" ")}>
      <div className={styles.frame}>
        <header className={styles.header}>
          <span className={styles.eyebrow}>{eyebrow}</span>
          <span className={styles.title}>{title}</span>
        </header>
        <div className={styles.visual} role="img" aria-label={label}>
          <div aria-hidden="true">{children}</div>
        </div>
      </div>
      <figcaption className={styles.caption}>{caption}</figcaption>
    </figure>
  );
}

function NibbleRail({
  values,
  hex,
  label,
  kind = "cell",
  compact = false,
}: {
  values: readonly (number | string)[];
  hex?: string;
  label?: string;
  kind?: "cell" | "flag" | "mask";
  compact?: boolean;
}) {
  return (
    <div className={compact ? `${styles.railRow} ${styles.railRowCompact}` : styles.railRow}>
      <span className={styles.railLabel}>{label ?? ""}</span>
      <div className={styles.nibbleRail} data-kind={kind}>
        <span className={`${styles.nibble} ${styles.unused}`} title="unused bits 31 through 28">
          ·
        </span>
        {values.map((value, index) => {
          const empty = value === 0 || value === "0";
          return (
            <span
              className={styles.nibble}
              data-empty={empty ? "true" : "false"}
              data-value={String(value)}
              key={`${index}-${value}`}
              title={`nibble ${6 - index}: ${value}`}
            >
              {value}
            </span>
          );
        })}
      </div>
      {hex && <code className={styles.wordHex}>{hex}</code>}
    </div>
  );
}

function BoardView({ cells, litColumns = [] }: { cells: string; litColumns?: readonly number[] }) {
  const board = parseBoard(cells);
  const size = 28;
  return (
    <svg className={styles.board} viewBox={`0 0 ${7 * size} ${7 * size}`} focusable="false">
      <rect width={7 * size} height={7 * size} rx="8" fill="var(--color-cell)" />
      {board.map((cell, index) => (
        <CellGlyph cell={cell} x={(index % 7) * size} y={Math.floor(index / 7) * size} s={size} key={index} />
      ))}
      {litColumns.map((column) => (
        <rect
          key={column}
          x={column * size + 1.5}
          y="1.5"
          width={size - 3}
          height={7 * size - 3}
          rx="5"
          fill="none"
          stroke="var(--color-accent)"
          strokeWidth="2"
        />
      ))}
    </svg>
  );
}

function BitGrid({
  set,
  tone,
  marks = {},
}: {
  set: readonly number[];
  tone: "pop" | "shift" | "plane" | "cover";
  marks?: Partial<Record<number, "solid" | "cracked">>;
}) {
  const active = new Set(set);
  return (
    <span className={styles.bitGrid} data-tone={tone}>
      {Array.from({ length: 49 }, (_, index) => (
        <span
          className={styles.bit}
          data-set={active.has(index) ? "true" : "false"}
          data-mark={marks[index] ?? "none"}
          key={index}
        />
      ))}
    </span>
  );
}

function MaskCard({
  label,
  hex,
  set,
  tone,
  stage,
}: {
  label: string;
  hex: string;
  set: readonly number[];
  tone: "pop" | "shift" | "plane" | "cover";
  stage?: "one" | "two" | "three";
}) {
  return (
    <div className={styles.maskCard} data-stage={stage}>
      <div className={styles.maskHeading}>
        <span>{label}</span>
        <code>{hex}</code>
      </div>
      <BitGrid set={set} tone={tone} />
    </div>
  );
}

function DiscGlyph({ value }: { value: number }) {
  return (
    <svg className={styles.discGlyph} viewBox="0 0 40 40" focusable="false">
      <CellGlyph cell={value} x={0} y={0} s={40} />
    </svg>
  );
}

/**
 * A compact page hero: the recorded wave moves from the seven column words to
 * row-major masks, then writes crack/reveal results back into the packed words.
 */
export function RustEngineHero({ caption }: { caption?: ReactNode }) {
  const words = columnWords(TRACE.placed);
  return (
    <FigureShell
      eyebrow="Packed cascade"
      title="One wave in three machine views"
      label="Seven packed column registers become row-major masks, then crack and reveal writes return to the packed columns before gravity"
      className={styles.hero}
      caption={
        caption ?? (
          <>
            The recorded wave begins in seven packed column words, derives row-major masks for the explosion, and
            writes the crack and reveal back before gravity. The loop that repeats these steps is in{" "}
            <Link href={`${CASCADE_SOURCE}#L131`}>engine.rs</Link>.
          </>
        )
      }
    >
      <div className={styles.heroFlow}>
        <section className={styles.heroStage}>
          <span className={styles.stageNumber}>01</span>
          <span className={styles.stageTitle}>seven column registers</span>
          <div className={styles.wordChips}>
            {words.map((word, column) => (
              <code
                className={styles.wordChip}
                data-active={column >= 1 && column <= 3 ? "true" : "false"}
                key={column}
              >
                c{column + 1} {hex32(word).slice(2)}
              </code>
            ))}
          </div>
        </section>

        <span className={styles.flowArrow}>→</span>

        <section className={styles.heroStage}>
          <span className={styles.stageNumber}>02</span>
          <span className={styles.stageTitle}>row-major masks</span>
          <div className={styles.heroMasks}>
            <span>
              <BitGrid set={POPPING} tone="pop" />
              <small>popping</small>
            </span>
            <span>
              <BitGrid set={COVERED} tone="cover" />
              <small>covered</small>
            </span>
            <span>
              <BitGrid set={CRACKED} tone="plane" />
              <small>cracked</small>
            </span>
          </div>
        </section>

        <span className={styles.flowArrow}>→</span>

        <section className={styles.heroStage}>
          <span className={styles.stageNumber}>03</span>
          <span className={styles.stageTitle}>write back, then gather</span>
          <div className={styles.heroTrace}>
            <code>c3&nbsp; 00000083 → 00000090 → 00000009</code>
            <code>c4&nbsp; 00000093 → 00000010 → 00000001</code>
          </div>
          <span className={styles.heroLegend}>8 → 9 crack&nbsp;&nbsp; 9 → 1 reveal</span>
        </section>
      </div>
    </FigureShell>
  );
}

/** The full recorded board beside all seven exact u32 column words. */
export function RustPackedBoardFigure({ caption }: { caption?: ReactNode }) {
  const words = columnWords(TRACE.placed);
  return (
    <FigureShell
      eyebrow="Board representation"
      title="Forty-nine cells in seven registers"
      label="The recorded seven by seven board beside its seven packed 32-bit column words, one four-bit nibble per cell"
      caption={
        caption ?? (
          <>
            The same recorded position appears as a board and as the seven <code>u32</code> words used by{" "}
            <Link href={`${BOARD_SOURCE}#L133`}>Board</Link>. The leftmost slot in each word is unused; the top board
            row comes next, and the bottom board row is the least-significant nibble at the right.
          </>
        )
      }
    >
      <div className={styles.registerLayout}>
        <div className={styles.boardPanel}>
          <BoardView cells={TRACE.placed} litColumns={[1, 2, 3]} />
          <span className={styles.boardLabel}>recorded position</span>
          <span className={styles.boardKey}>columns 2 through 4 contain the popping run</span>
        </div>
        <div className={styles.registerPanel}>
          <div className={styles.registerAxis}>
            <span />
            <span>unused</span>
            <span>top row</span>
            <span>bottom row</span>
            <span>word</span>
          </div>
          {words.map((word, column) => (
            <NibbleRail
              key={column}
              label={`c${column + 1}`}
              values={wordValues(word)}
              hex={hex32(word)}
              compact
            />
          ))}
          <div className={styles.encodingKey}>
            <span><code>0</code> empty</span>
            <span><code>1..7</code> numbered</span>
            <span><code>8</code> solid gray</span>
            <span><code>9</code> cracked gray</span>
          </div>
        </div>
      </div>
    </FigureShell>
  );
}

/**
 * The first wave's exact 49-bit masks and the two observable gray-disc writes.
 * It follows board.rs's four-input parallel counter without simplifying away
 * the empty down plane or the twos plane that lands on a numbered popper.
 */
export function RustExplosionBitplanesFigure({ caption }: { caption?: ReactNode }) {
  return (
    <FigureShell
      eyebrow="Explosion wave"
      title="Four shifts count every adjacent hit"
      label="The three-popper mask shifted up, down, left, and right, summed into ones, twos, and fours bit planes, then intersected with the solid and cracked cover masks"
      caption={
        caption ?? (
          <>
            This is the first wave of the recorded move. The three bottom-row 3s form the popper mask. Four shifts
            feed the bitwise counter in <Link href={`${BOARD_SOURCE}#L385`}>clear_wave</Link>; its one-hit plane
            cracks the solid 8, while any hit reveals the cracked 9 as the next recorded value, 1.
          </>
        )
      }
    >
      <div className={styles.explosionFlow}>
        <div className={styles.maskGroup}>
          <span className={styles.groupLabel}>popper bitboard</span>
          <MaskCard label="popping" hex="0x380000000000" set={POPPING} tone="pop" stage="one" />
        </div>

        <span className={styles.downArrow}>↓ four edge-safe shifts</span>

        <div className={styles.maskGroup}>
          <span className={styles.groupLabel}>one neighbour mask per direction</span>
          <div className={styles.shiftGrid}>
            {SHIFT_MASKS.map((mask) => (
              <MaskCard key={mask.label} {...mask} tone="shift" stage="two" />
            ))}
          </div>
        </div>

        <div className={styles.logicBand}>
          <code>xor</code>
          <span>adds each cell without carrying into its neighbour</span>
          <code>and</code>
          <span>moves carries into the twos and fours planes</span>
        </div>

        <div className={styles.maskGroup}>
          <span className={styles.groupLabel}>bit-sliced hit count</span>
          <div className={styles.planeGrid}>
            {HIT_PLANES.map((plane) => (
              <MaskCard key={plane.label} {...plane} tone="plane" stage="three" />
            ))}
          </div>
        </div>

        <div className={styles.formulaBand}>
          <code>any_hit = ones | twos | fours</code>
          <code>multi_hit = twos | fours</code>
        </div>

        <div className={styles.writeGrid}>
          <div className={styles.writeCard}>
            <span className={styles.writeLabel}>solid ∩ exactly one hit</span>
            <div className={styles.discTransition}>
              <DiscGlyph value={8} />
              <span>→</span>
              <DiscGlyph value={9} />
            </div>
            <code>c3: 0x83 → 0x90</code>
          </div>
          <div className={styles.writeCard}>
            <span className={styles.writeLabel}>cracked ∩ any hit</span>
            <div className={styles.discTransition}>
              <DiscGlyph value={9} />
              <span>→</span>
              <DiscGlyph value={1} />
            </div>
            <code>c4: 0x93 → 0x10</code>
          </div>
        </div>
      </div>
    </FigureShell>
  );
}

/** PEXT as a stable nibble gather, plus the separate row-rise shift. */
export function RustPextGravityFigure({ caption }: { caption?: ReactNode }) {
  return (
    <FigureShell
      eyebrow="Gravity and rise"
      title="The mask moves whole nibbles"
      label="A packed column becomes nonzero flags, the flags expand into a nibble mask, PEXT gathers the three surviving nibbles toward the bottom, and a separate shift inserts a solid gray row"
      caption={
        caption ?? (
          <>
            The gravity example is the exact three-disc case in the{" "}
            <Link href={`${BOARD_SOURCE}#L487`}>board test</Link>. <code>nonzero_flags</code> marks one bit per
            occupied nibble, multiplying by <code>0xF</code> selects all four bits, and <code>PEXT</code> gathers the
            selected nibbles in order. The recorded cascade uses the same operation for <code>0x90 → 0x09</code> and{" "}
            <code>0x10 → 0x01</code>.
          </>
        )
      }
    >
      <div className={styles.gravityLayout}>
        <div className={styles.gravityPipeline}>
          <NibbleRail label="word" values={TEST_GRAVITY.before} hex="0x00305001" />
          <span className={styles.operation}>↓ <code>nonzero_flags(word)</code></span>
          <NibbleRail label="flags" values={TEST_GRAVITY.flags} hex="0x00101001" kind="flag" />
          <span className={styles.operation}>↓ <code>flags × 0xF</code></span>
          <NibbleRail label="mask" values={TEST_GRAVITY.mask} hex="0x00f0f00f" kind="mask" />
          <span className={styles.operation}>↓ <code>pext32(word, mask)</code></span>
          <div className={styles.gravityResult}>
            <NibbleRail label="result" values={TEST_GRAVITY.after} hex="0x00000351" />
          </div>
          <span className={styles.orderNote}>The 3, 5, and 1 keep their order as the holes disappear.</span>
        </div>

        <aside className={styles.risePanel}>
          <span className={styles.groupLabel}>row rise</span>
          <NibbleRail values={TEST_GRAVITY.after} hex="0x00000351" compact />
          <span className={styles.operation}>↓ <code>((word &lt;&lt; 4) &amp; COL_USED) | 8</code></span>
          <NibbleRail values={TEST_GRAVITY.raised} hex="0x00003518" compact />
          <div className={styles.riseSummary}>
            <span>every cell moves up one nibble</span>
            <span>a solid 8 enters at the bottom</span>
          </div>
        </aside>
      </div>

      <div className={styles.recordedGravity}>
        <span>same gather in the recorded wave</span>
        <code>c3&nbsp; 0x00000090 → 0x00000009</code>
        <code>c4&nbsp; 0x00000010 → 0x00000001</code>
      </div>
    </FigureShell>
  );
}

/** The exact serialized boards are exported for tests or adjacent page copy. */
export const RUST_BITBOARD_RECORDED_TRACE = TRACE;
