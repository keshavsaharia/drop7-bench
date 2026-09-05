/**
 * The link preview for the play page. The words are the page's own title and
 * description.
 */
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

const CARD = {
  title: "Play Drop7",
  summary:
    "Play five-move Hardcore Drop7 in the browser with the repository's rules engine, or watch the depth-4 expectimax play it.",
  path: "/play",
};

export const alt = cardAlt(CARD);
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

export default function Image() {
  return renderPageCard(CARD);
}
