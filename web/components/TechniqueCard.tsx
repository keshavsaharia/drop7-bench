/**
 * One technique as a card, for the home page and the technique index: the
 * technique's art, its title, its one-line description, and how many approach
 * pages carry it. The count is a count of pages, never a research number.
 * The card links to the technique group under /approach/technique/<slug>.
 */
import { Card } from "./Card";
import { TechniqueArt } from "./technique-art/TechniqueArt";
import type { Technique } from "@/lib/techniques";

export interface TechniqueCardProps {
  technique: Technique;
  /** Number of approach pages that carry this technique. */
  count: number;
  /** Heading element for the card title. */
  heading?: "h2" | "h3" | "h4";
  /** Play the art once on mount (touch screens) or loop it; default plays on hover. */
  playing?: "once" | "loop";
}

export function techniqueHref(slug: string): string {
  return `/approach/technique/${slug}`;
}

export function approachCountLabel(count: number): string {
  return count === 1 ? "1 approach" : `${count} approaches`;
}

export function TechniqueCard({ technique, count, heading = "h3", playing }: TechniqueCardProps) {
  return (
    <Card
      href={techniqueHref(technique.slug)}
      art={<TechniqueArt name={technique.slug} />}
      heading={heading}
      title={technique.title}
      summary={technique.oneLine}
      foot={<span className="label">{approachCountLabel(count)}</span>}
      playing={playing}
    />
  );
}
