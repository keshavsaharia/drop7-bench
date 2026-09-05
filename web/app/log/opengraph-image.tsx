/**
 * The link preview for the research log index. The words are the page's own
 * title and description.
 */
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

const CARD = {
  eyebrow: "Research",
  title: "Research log",
  summary:
    "A dated account of what was tried each day in the Drop7 million-point research program, including the things that did not work.",
  path: "/log",
};

export const alt = cardAlt(CARD);
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

export default function Image() {
  return renderPageCard(CARD);
}
