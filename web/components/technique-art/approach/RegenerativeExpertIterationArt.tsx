/**
 * Card art for `ntuple-rl/regenerative-expert-iteration`: the teaching loop
 * closed on itself. The search labels positions, the network learns from
 * them, the network plays games, and those games come back as the next
 * round's data. On play three markers carry the round clockwise around the
 * ring, a fresh row of the student's own games slides into the pile, and the
 * search lights up ready to label it.
 *
 * Server component. Motion lives in regenerative-expert-iteration.css
 * (transform and opacity only); the markup is the resting frame, with the new
 * row already in the pile.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./regenerative-expert-iteration.css";

const CX = 160;
const CY = 86;
const R = 52;

function ringPoint(degrees: number): [number, number] {
  const radians = (degrees * Math.PI) / 180;
  return [CX + R * Math.cos(radians), CY + R * Math.sin(radians)];
}

/** Where the round is between stations, and which way it travels. */
const BEADS = [-30, 90, 210];
/** The three stations, clockwise: label, learn, play. */
const STATION_W = 36;
const STATION_H = 22;
const SEARCH: [number, number] = [CX, CY - R];
const NET = ringPoint(30);
const GAMES = ringPoint(150);
const PILE_Y = [104, 109, 114];

function stationBox(centre: readonly [number, number]) {
  return { x: centre[0] - STATION_W / 2, y: centre[1] - STATION_H / 2 };
}

const SEARCH_BOX = stationBox(SEARCH);
const NET_BOX = stationBox(NET);
const GAMES_BOX = stationBox(GAMES);
const NET_EDGES = [106, 112, 118].map((y) => `M195,${y}L214,112`).join("");

export function RegenerativeExpertIterationArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-regenerative-expert-iteration",
        "A closed loop: the search labels, the network learns, the network plays, and its own games return as the next round's data",
        props,
      )}
    >
      <circle cx={CX} cy={CY} r={R} fill="none" stroke="var(--color-rule-strong)" strokeWidth="1.4" />
      <g data-anim="orbit" fill="var(--color-accent)">
        {BEADS.map((angle) => {
          const [x, y] = ringPoint(angle);
          return (
            <g key={angle} transform={`translate(${x.toFixed(1)} ${y.toFixed(1)}) rotate(${angle + 90})`}>
              <path
                d="M-15,0L-5,0"
                fill="none"
                stroke="var(--color-accent)"
                strokeWidth="2"
                strokeLinecap="round"
              />
              <path d="M-5,-3.6L7,0L-5,3.6z" />
            </g>
          );
        })}
      </g>
      <g fill="var(--color-raised)" stroke="var(--color-ink-3)" strokeWidth="1">
        <rect x={SEARCH_BOX.x} y={SEARCH_BOX.y} width={STATION_W} height={STATION_H} rx="4" />
        <rect x={NET_BOX.x} y={NET_BOX.y} width={STATION_W} height={STATION_H} rx="4" />
        <rect x={GAMES_BOX.x} y={GAMES_BOX.y} width={STATION_W} height={STATION_H} rx="4" />
      </g>
      <path d="M160,31L152,41M160,31L168,41" fill="none" stroke="var(--color-ink-4)" strokeWidth="1" />
      <g fill="var(--color-ink-2)">
        <circle cx="160" cy="30" r="2.2" />
        <circle cx="152" cy="42" r="2.2" />
        <circle cx="168" cy="42" r="2.2" />
      </g>
      <path d={NET_EDGES} fill="none" stroke="var(--color-ink-4)" strokeWidth="0.8" />
      <g fill="var(--color-cell)" stroke="var(--color-ink-3)" strokeWidth="1">
        <circle cx="195" cy="106" r="2.2" />
        <circle cx="195" cy="112" r="2.2" />
        <circle cx="195" cy="118" r="2.2" />
        <circle cx="214" cy="112" r="2.6" />
      </g>
      <g fill="var(--color-ink-4)">
        {PILE_Y.map((y) => (
          <rect key={y} x="103" y={y} width="24" height="3" rx="1.5" />
        ))}
      </g>
      <rect data-anim="fresh" x="103" y="119" width="24" height="3" rx="1.5" fill="var(--color-accent)" />
      <rect
        data-anim="relabel"
        x={SEARCH_BOX.x - 3}
        y={SEARCH_BOX.y - 3}
        width={STATION_W + 6}
        height={STATION_H + 6}
        rx="6"
        fill="none"
        stroke="var(--color-highlight)"
        strokeWidth="1.4"
      />
      <g fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-3)">
        <text x={CX} y="15" textAnchor="middle">
          search
        </text>
        <text x="230" y="116">
          net
        </text>
        <text x="90" y="116" textAnchor="end">
          games
        </text>
      </g>
      <g className="tart-final" data-anim="caption">
        <text x="160" y="166" textAnchor="middle" fontFamily={ART_MONO} fontSize="9" fill="var(--color-ink-2)">
          the student&rsquo;s own games become the next round&rsquo;s data
        </text>
      </g>
    </svg>
  );
}
