/**
 * The link preview for the engines index. The words are the page's own title
 * and description.
 */
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";
import { TechniqueArt } from "@/components/technique-art/TechniqueArt";

const CARD = {
  eyebrow: "Research",
  title: "Engines",
  summary:
    "Several implementations of the Drop7 rules, each proven to play the same game as the C++ reference, with every recorded number and its source.",
  path: "/engine",
};

export const alt = cardAlt(CARD);
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

export default function Image() {
  return renderPageCard({ ...CARD, art: <TechniqueArt name="engine-native" mode="static" /> });
}
