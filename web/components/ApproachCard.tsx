/**
 * One approach as a card: its own art when the directory has one, otherwise
 * the technique's art (or the generic board when the approach carries no
 * technique), a mono eyebrow, the title, the one-sentence summary from
 * frontmatter and the compact retained-label row. Public-information access
 * is the default and is omitted; privileged access remains visible.
 *
 * The card is wrapped in `div.card-wrap[data-search]` so the index's
 * FilterSearch island can hide it by title, summary or slug without the Card
 * shell knowing about filtering.
 */
import { ApproachBadges } from "./ApproachBadges";
import { Card } from "./Card";
import { TechniqueArt } from "./technique-art/TechniqueArt";
import type { ApproachEntry } from "@/lib/repo";

export interface ApproachCardProps {
  entry: ApproachEntry;
  /** Mono label above the title: the technique title, or the family title in family view. */
  eyebrow?: string;
  /** Heading element for the card title. */
  heading?: "h2" | "h3" | "h4";
}

export function approachHref(entry: Pick<ApproachEntry, "family" | "slug">): string {
  return `/approaches/${entry.family}/${entry.slug}`;
}

/** Featured pages first, then alphabetical by title. */
export function sortApproaches(entries: readonly ApproachEntry[]): ApproachEntry[] {
  return [...entries].sort((a, b) => {
    if (a.featured !== b.featured) return a.featured ? -1 : 1;
    return a.title.localeCompare(b.title) || a.slug.localeCompare(b.slug);
  });
}

export function ApproachCard({ entry, eyebrow, heading = "h3" }: ApproachCardProps) {
  const searchText = [entry.title, entry.summary, entry.slug, entry.family].join(" ").toLowerCase();
  const showEyebrow = eyebrow !== undefined || entry.featured;
  return (
    <div className="card-wrap" data-search={searchText}>
      <Card
        href={approachHref(entry)}
        art={
          <TechniqueArt
            name={entry.technique ?? "fallback"}
            approach={{ family: entry.family, slug: entry.slug }}
          />
        }
        heading={heading}
        eyebrow={
          showEyebrow ? (
            <>
              {eyebrow}
              {entry.featured && <span className="card-featured">featured</span>}
            </>
          ) : undefined
        }
        title={entry.title}
        summary={entry.summary || undefined}
      >
        <ApproachBadges
          className="card-labels"
          status={entry.status}
          evidence={entry.evidence}
          reads={entry.reads}
        />
      </Card>
    </div>
  );
}
