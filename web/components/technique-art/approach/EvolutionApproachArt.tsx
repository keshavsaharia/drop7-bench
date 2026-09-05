/**
 * Card art for `heuristic-search/evolution`: the generational loop itself,
 * with nothing standing in for it. A band of six weight vectors — each drawn
 * as its own stack of four coloured bands, so two candidates are told apart at
 * a glance — is culled to three elites, and those three are copied into a new
 * band of six in which one stripe of every copy has changed. Population,
 * selection, mutation, next generation, read top to bottom.
 *
 * Server component. Motion lives in evolution.css in this directory (transform
 * and opacity only); the SVG's own attributes are the resting frame.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./evolution.css";

/** Four distinguishable stripe colours; a genome is four indices into these. */
const STRIPES = [
  "var(--color-series-1)",
  "var(--color-series-3)",
  "var(--color-series-4)",
  "var(--color-series-7)",
];

const SLOT_X = [76, 116, 156, 196, 236, 276];
const CHIP_W = 16;
const CELL_H = 9;
const BAND_Y = [30, 80, 130];

/** The starting population: six four-stripe weight vectors. */
const POPULATION: readonly (readonly number[])[] = [
  [0, 2, 1, 3],
  [1, 0, 3, 2],
  [2, 3, 0, 1],
  [0, 1, 2, 0],
  [3, 2, 1, 0],
  [1, 3, 2, 1],
];

/** Which three survived the ranking, in their own slots. */
const ELITES = [1, 3, 4];

/** Six children: each copies an elite and changes exactly one stripe. */
const CHILDREN: readonly { parent: number; cell: number; stripe: number }[] = [
  { parent: 1, cell: 2, stripe: 1 },
  { parent: 1, cell: 0, stripe: 3 },
  { parent: 3, cell: 3, stripe: 3 },
  { parent: 3, cell: 1, stripe: 3 },
  { parent: 4, cell: 2, stripe: 0 },
  { parent: 4, cell: 0, stripe: 1 },
];

function Chip({ genome, x, y }: { genome: readonly number[]; x: number; y: number }) {
  return (
    <g>
      {genome.map((stripe, row) => (
        <rect
          key={row}
          x={x}
          y={y + row * CELL_H}
          width={CHIP_W}
          height={CELL_H - 1}
          rx={1.5}
          fill={STRIPES[stripe]}
        />
      ))}
    </g>
  );
}

export function EvolutionApproachArt(props: ArtProps) {
  return (
    <svg
      {...artSvgProps(
        "approach-evolution",
        "A population of six weight vectors is culled to three elites, which are copied into a new generation with one stripe changed in each copy",
        props,
      )}
    >
      <g data-anim="cull" opacity={0.3}>
        {POPULATION.map((genome, i) =>
          ELITES.includes(i) ? null : <Chip key={i} genome={genome} x={SLOT_X[i]} y={BAND_Y[0]} />,
        )}
      </g>
      <g>
        {ELITES.map((i) => (
          <Chip key={i} genome={POPULATION[i]} x={SLOT_X[i]} y={BAND_Y[0]} />
        ))}
      </g>
      <g data-anim="rings" fill="none" stroke="var(--color-accent)" strokeWidth={1.4}>
        {ELITES.map((i) => (
          <rect
            key={i}
            x={SLOT_X[i] - 3}
            y={BAND_Y[0] - 3}
            width={CHIP_W + 6}
            height={4 * CELL_H + 5}
            rx={3}
          />
        ))}
      </g>

      <g data-anim="keep">
        {ELITES.map((i) => (
          <Chip key={i} genome={POPULATION[i]} x={SLOT_X[i]} y={BAND_Y[1]} />
        ))}
      </g>

      <g data-anim="breed">
        {CHILDREN.map(({ parent }, i) => (
          <Chip key={i} genome={POPULATION[parent]} x={SLOT_X[i]} y={BAND_Y[2]} />
        ))}
      </g>
      <g data-anim="mutate">
        {CHILDREN.map(({ cell, stripe }, i) => (
          <g key={i}>
            <rect
              x={SLOT_X[i]}
              y={BAND_Y[2] + cell * CELL_H}
              width={CHIP_W}
              height={CELL_H - 1}
              rx={1.5}
              fill={STRIPES[stripe]}
            />
            <rect
              x={SLOT_X[i] - 1.5}
              y={BAND_Y[2] + cell * CELL_H - 1.5}
              width={CHIP_W + 3}
              height={CELL_H + 2}
              rx={2.5}
              fill="none"
              stroke="var(--color-highlight)"
              strokeWidth={1.3}
            />
          </g>
        ))}
      </g>

      <g fontFamily={ART_MONO} fontSize={9} fill="var(--color-ink-2)">
        <text x={10} y={BAND_Y[0] + 21}>
          population
        </text>
        <text x={10} y={BAND_Y[1] + 21}>
          elites
        </text>
        <text x={10} y={BAND_Y[2] + 21}>
          mutate
        </text>
      </g>
    </svg>
  );
}
