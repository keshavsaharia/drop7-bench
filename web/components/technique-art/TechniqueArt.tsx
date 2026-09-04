/**
 * Renders the card art for a technique by name, or — when the caller names an
 * approach directory that has an art of its own — that approach's art
 * instead. Unknown names get the generic board art, so a card built from
 * frontmatter never throws.
 * Server component: the art is plain SVG and every motion is CSS.
 */
import "./art.css";
import { getApproachArt } from "./approach/registry";
import { FALLBACK, TECHNIQUES, isTechniqueName, type ArtProps } from "./registry";

export function TechniqueArt({
  name,
  approach,
  ...props
}: { name: string; approach?: { family: string; slug: string } } & ArtProps) {
  const own = approach ? getApproachArt(approach.family, approach.slug) : null;
  const entry = own ? { Art: own } : isTechniqueName(name) ? TECHNIQUES[name] : FALLBACK;
  return <entry.Art {...props} />;
}
