/**
 * The link preview for the concepts index. The words are the page's own title
 * and description.
 */
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

const CARD = {
  eyebrow: "Learn",
  title: "Concepts",
  summary:
    "The ideas behind every Drop7 strategy on this site, in reading order, shown on positions the rules engine produced.",
  path: "/learn/concepts",
};

export const alt = cardAlt(CARD);
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

export default function Image() {
  return renderPageCard(CARD);
}
