/**
 * The link preview for the theories index. The words are the page's own title
 * and description.
 */
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

const CARD = {
  eyebrow: "Research",
  title: "Theories",
  summary:
    "Registered falsifiable claims of the Drop7 million-point program, with their assessment and evidence tier.",
  path: "/theories",
};

export const alt = cardAlt(CARD);
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

export default function Image() {
  return renderPageCard(CARD);
}
