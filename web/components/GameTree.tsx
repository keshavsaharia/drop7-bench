import type { ReactNode } from "react";
import { GameTreeExplorer } from "./GameTreeExplorer";

/**
 * MDX figure: an interactive expectimax game tree on a seeded position.
 *
 * The explorer is a client component; this server wrapper keeps the props
 * serialisable and adds the caption and the standing disclaimer. Seeds from
 * the `0x5eed****` playground domain are the convention for illustrations,
 * exactly as the scripted rounds use; no research seed is ever embedded.
 * The figure breaks out to the wide measure where the page allows it.
 */
export function GameTreeFigure({
  seed = 0x5eed1001,
  moves = 8,
  leafDepth = 0,
  maxOutcomes = 7,
  height = 640,
  controls = true,
  caption,
}: {
  seed?: number;
  moves?: number;
  leafDepth?: 0 | 1 | 2;
  maxOutcomes?: number;
  height?: number;
  controls?: boolean;
  caption?: ReactNode;
}) {
  return (
    <figure className="fig fig--wide tree-fig">
      <GameTreeExplorer seed={seed} moves={moves} leafDepth={leafDepth} maxOutcomes={maxOutcomes} height={height} controls={controls} />
      {caption ? <figcaption>{caption}</figcaption> : null}
    </figure>
  );
}
