/**
 * One engine as a card on the Engines index: the language as eyebrow, the
 * title, and a label/value list (role, board, latent mode, parity) in place
 * of a summary. `compact` drops the art and keeps a shorter list, for the
 * entries that are not engines in their own right. Styled by `.card*` in
 * globals.css and `.engine-card dl` in web/app/engines/engines.css.
 */
import { Card } from "@/components/Card";
import { TechniqueArt } from "@/components/technique-art/TechniqueArt";
import type { EngineEntry } from "@/lib/engines";

export function latentModeText(mode: EngineEntry["latentMode"]): string {
  if (mode === "n/a") return "n/a";
  return mode ? "yes" : "no";
}

export function EngineCard({ entry, compact = false }: { entry: EngineEntry; compact?: boolean }) {
  return (
    <Card
      href={`/engines/${entry.slug}`}
      art={compact ? undefined : <TechniqueArt name={entry.art} title={entry.title} />}
      eyebrow={entry.language}
      title={entry.title}
      summary={compact ? entry.role : undefined}
      className="engine-card"
      foot={<span className="label">read the explainer</span>}
    >
      <dl>
        <dt>Role</dt>
        <dd>{entry.roleLabel}</dd>
        {!compact && (
          <>
            <dt>Board</dt>
            <dd>{entry.boardShort}</dd>
            <dt>Latent mode</dt>
            <dd>{latentModeText(entry.latentMode)}</dd>
          </>
        )}
        <dt>Parity</dt>
        <dd>{entry.parityShort}</dd>
      </dl>
    </Card>
  );
}
