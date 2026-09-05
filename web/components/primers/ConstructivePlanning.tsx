/**
 * Figures for the constructive-planning primer
 * (web/content/learn/techniques/constructive-planning.mdx).
 *
 * Server components: inline SVG whose motion is CSS keyframes declared in
 * constructive-planning.css on the data-anim elements. The animation runs by
 * default; under prefers-reduced-motion it is removed and each element's base
 * style shows the explanatory final state. Only transform, opacity and
 * stroke-dashoffset animate, and no text moves.
 *
 * The board cells in ConstructiveRiseTrigger are the engine-generated
 * positions from the constructive-reservoir family page
 * (approaches/constructive-reservoir/README.mdx, BoardCompare): four 6s
 * stacked on the gray row one drop before the rise, and the board after the
 * rise fired the stack.
 */
import "./constructive-planning.css";
import { CellGlyph, parseBoard } from "@/components/discs";

const MONO = "var(--font-mono)";

/** Engine output: four 6s in one column above the gray row, one drop before the rise. */
const BEFORE = "0000000000000000060000006000000600000060008888888";
/** Engine output after the rise: the four 6s cleared and the gray disc under them cracked. */
const AFTER = "0000000000000000000000000000000000088898888888888";
const COLS = 7;
const STACK_COL = 3;
const SIX_CELLS = [17, 24, 31, 38];
const GRAY_UNDER_STACK = 45;

/* -------------------------------------------------------------------------
 * 1. The domino toy: five turns that pay nothing, then one bump that pays
 *    for the whole line.
 * ---------------------------------------------------------------------- */

export function ConstructiveDelayedPayoff({ caption }: { caption?: string }) {
  const dominoX = [110, 182, 254, 326, 398];
  const tableY = 150;
  return (
    <figure className="fig fig--cp">
      <div className="fig-frame">
        <svg
          viewBox="0 0 560 200"
          role="img"
          aria-label="Five dominoes stood one per turn for no points, then tipped by a bump on the sixth turn for five points"
        >
          <path d={`M36 ${tableY} H524`} stroke="var(--color-rule-strong)" strokeWidth={2} fill="none" />
          <text x={76} y={92} textAnchor="middle" fontSize={12} fontFamily={MONO} fill="var(--color-ink-2)">
            bump
          </text>
          <path
            data-anim="cp-tip"
            d="M52 127 h34 m0 0 l-8 -7 m8 7 l-8 7"
            stroke="var(--color-accent)"
            strokeWidth={2}
            strokeLinecap="round"
            strokeLinejoin="round"
            fill="none"
          />
          <text x={76} y={172} textAnchor="middle" fontSize={11} fontFamily={MONO} fill="var(--color-ink-3)">
            turn 6
          </text>
          {dominoX.map((x, i) => (
            <g key={x}>
              <text x={x} y={92} textAnchor="middle" fontSize={12} fontFamily={MONO} fill="var(--color-ink-2)">
                +0
              </text>
              <rect
                data-anim={`cp-dom-${i + 1}`}
                x={x - 7}
                y={tableY - 46}
                width={14}
                height={46}
                rx={2}
                fill="var(--color-series-1)"
              />
              <text x={x} y={172} textAnchor="middle" fontSize={11} fontFamily={MONO} fill="var(--color-ink-3)">
                turn {i + 1}
              </text>
            </g>
          ))}
          <rect
            data-anim="cp-pay"
            x={452}
            y={106}
            width={64}
            height={34}
            rx={6}
            fill="var(--color-accent-soft)"
            stroke="var(--color-accent)"
          />
          <text
            x={484}
            y={124}
            textAnchor="middle"
            dominantBaseline="central"
            fontSize={16}
            fontWeight={700}
            fontFamily={MONO}
            fill="var(--color-accent)"
          >
            +5
          </text>
          <text x={484} y={172} textAnchor="middle" fontSize={11} fontFamily={MONO} fill="var(--color-ink-3)">
            paid on the bump
          </text>
        </svg>
      </div>
      <figcaption>
        {caption ??
          "The domino game from the example. Turns one to five each stand a domino and pay nothing; on turn six the table is bumped, the line falls, and the player is paid five points for it. A player who tips every second turn would have three points by now."}
      </figcaption>
    </figure>
  );
}

/* -------------------------------------------------------------------------
 * 2. The rise as trigger, on the engine's own positions.
 * ---------------------------------------------------------------------- */

function EmptyGrid({ x, y, s }: { x: number; y: number; s: number }) {
  return (
    <g>
      {Array.from({ length: 49 }, (_, i) => (
        <CellGlyph key={i} cell={0} x={x + (i % COLS) * s} y={y + Math.floor(i / COLS) * s} s={s} />
      ))}
    </g>
  );
}

function StaticBoard({ cells, x, y, s }: { cells: string; x: number; y: number; s: number }) {
  const board = parseBoard(cells);
  return (
    <g>
      {board.map((cell, i) => (
        <CellGlyph key={i} cell={cell} x={x + (i % COLS) * s} y={y + Math.floor(i / COLS) * s} s={s} />
      ))}
    </g>
  );
}

export function ConstructiveRiseTrigger({ caption }: { caption?: string }) {
  const s = 22;
  const leftX = 28;
  const rightX = 378;
  const top = 40;
  const before = parseBoard(BEFORE);
  const after = parseBoard(AFTER);
  const newRow = after.slice(42);
  const cx = (x: number, i: number) => x + (i % COLS) * s;
  const cy = (i: number) => top + Math.floor(i / COLS) * s;
  const bracketX = rightX + (STACK_COL + 1) * s + 5;
  return (
    <figure className="fig fig--cp">
      <div className="fig-frame">
        <svg
          viewBox="0 0 560 262"
          role="img"
          aria-label="Two Drop7 boards: four 6s stacked on the gray row before the rise, and the same board after the rise cleared all four"
        >
          <text x={leftX} y={26} fontSize={11} fontFamily={MONO} fill="var(--color-ink-3)">
            before the rise: one drop left
          </text>
          <text x={rightX} y={26} fontSize={11} fontFamily={MONO} fill="var(--color-ink-3)">
            the rise, then the clears
          </text>

          {/* left: the engine position one drop before the rise */}
          <StaticBoard cells={BEFORE} x={leftX} y={top} s={s} />
          <path
            d={`M${leftX + (STACK_COL + 1) * s + 5} ${top + 2 * s + 3} h4 V${top + 7 * s - 3} h-4`}
            stroke="var(--color-ink-2)"
            strokeWidth={1.5}
            fill="none"
          />
          <text
            x={leftX + (STACK_COL + 1) * s + 16}
            y={top + 4.5 * s}
            dominantBaseline="central"
            fontSize={12}
            fontFamily={MONO}
            fill="var(--color-ink-2)"
          >
            run 5
          </text>
          <text x={leftX} y={228} fontSize={11} fontFamily={MONO} fill="var(--color-ink-2)">
            four 6s plus the gray beneath: a run of five
          </text>

          {/* middle: the rise */}
          <path
            d="M214 117 H336 m0 0 l-10 -9 m10 9 l-10 9"
            stroke="var(--color-ink-3)"
            strokeWidth={2}
            strokeLinecap="round"
            strokeLinejoin="round"
            fill="none"
          />
          <text x={275} y={104} textAnchor="middle" fontSize={11} fontFamily={MONO} fill="var(--color-ink-3)">
            rise
          </text>
          <text x={275} y={138} textAnchor="middle" fontSize={10} fontFamily={MONO} fill="var(--color-ink-3)">
            +17,000
          </text>

          {/* right: the same position, animated through the rise */}
          <EmptyGrid x={rightX} y={top} s={s} />
          <g data-anim="cp-shift">
            {before.map((cell, i) => {
              if (cell === 0) return null;
              if (SIX_CELLS.includes(i)) {
                return (
                  <g key={i} data-anim="cp-six">
                    <CellGlyph cell={cell} x={cx(rightX, i)} y={cy(i)} s={s} />
                  </g>
                );
              }
              if (i === GRAY_UNDER_STACK) {
                return (
                  <g key={i}>
                    <g data-anim="cp-gray-solid">
                      <CellGlyph cell={8} x={cx(rightX, i)} y={cy(i)} s={s} />
                    </g>
                    <g data-anim="cp-gray-cracked">
                      <CellGlyph cell={9} x={cx(rightX, i)} y={cy(i)} s={s} />
                    </g>
                  </g>
                );
              }
              return <CellGlyph key={i} cell={cell} x={cx(rightX, i)} y={cy(i)} s={s} />;
            })}
          </g>
          <g data-anim="cp-newrow">
            {newRow.map((cell, i) => (
              <CellGlyph key={i} cell={cell} x={rightX + i * s} y={top + 6 * s} s={s} />
            ))}
          </g>
          <rect
            data-anim="cp-flash"
            x={rightX + STACK_COL * s + 1}
            y={top + s + 1}
            width={s - 2}
            height={4 * s - 2}
            rx={5}
            fill="none"
            stroke="var(--color-highlight)"
            strokeWidth={2}
          />
          <path
            data-anim="cp-run6"
            d={`M${bracketX} ${top + s + 3} h4 V${top + 7 * s - 3} h-4`}
            stroke="var(--color-ink-2)"
            strokeWidth={1.5}
            fill="none"
          />
          <text x={rightX} y={228} fontSize={11} fontFamily={MONO} fill="var(--color-ink-2)">
            run six: all four clear, the gray cracks
          </text>
        </svg>
      </div>
      <figcaption>
        {caption ??
          "Engine output from the constructive-reservoir family page. Left: a 6 has just been dropped onto three others above the gray row, making a run of five, so nothing clears. Right: the rise pushes a new gray row in underneath, the run becomes six, all four 6s clear together for 28 chain points on top of the 17,000-point rise bonus, and the gray disc that was under them is cracked. The right board ends on the position the engine records after the rise."}
      </figcaption>
    </figure>
  );
}
