/**
 * The link preview for the experiments index. The words are the page's own
 * title and description.
 */
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

const CARD = {
  eyebrow: "Research",
  title: "Experiments",
  summary:
    "Preregistered experiment protocols of the Drop7 million-point program, with lifecycle and benchmark tier.",
  path: "/experiments",
};

export const alt = cardAlt(CARD);
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

export default function Image() {
  return renderPageCard(CARD);
}
