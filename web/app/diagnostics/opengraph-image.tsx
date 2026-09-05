/**
 * The link preview for the diagnostics index. The words are the page's own
 * title and description.
 */
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

const CARD = {
  eyebrow: "Research",
  title: "Diagnostics",
  summary:
    "The measuring instruments, harnesses and model probes of the Drop7 research program. None of these is a way to play.",
  path: "/diagnostics",
};

export const alt = cardAlt(CARD);
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

export default function Image() {
  return renderPageCard(CARD);
}
