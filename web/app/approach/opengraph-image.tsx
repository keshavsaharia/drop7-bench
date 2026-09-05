/**
 * The link preview for the approaches index. The words are the page's own
 * title and description.
 */
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";
import { TechniqueArt } from "@/components/technique-art/TechniqueArt";

const CARD = {
  title: "Approaches",
  summary: "Every theory of how to choose a column, grouped by the technique it uses.",
  path: "/approach",
};

export const alt = cardAlt(CARD);
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

export default function Image() {
  return renderPageCard({ ...CARD, art: <TechniqueArt name="expectimax" mode="static" /> });
}
