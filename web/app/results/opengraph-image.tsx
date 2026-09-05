/**
 * The link preview for the results index. The words are the page's own title
 * and description.
 */
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

const CARD = {
  eyebrow: "Research",
  title: "Results",
  summary:
    "Recorded outcomes of the Drop7 million-point program, each with run validity, scientific outcome and evidence tier.",
  path: "/results",
};

export const alt = cardAlt(CARD);
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

export default function Image() {
  return renderPageCard(CARD);
}
