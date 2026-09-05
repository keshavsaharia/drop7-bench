/**
 * The link preview for the leaderboard. The page sets no metadata of its own,
 * so the card takes its heading and the part of its lead that does not name
 * the round currently selected.
 */
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

const CARD = {
  title: "Human + AI leaderboard",
  summary:
    "Human scores come from server-replayed move sequences; AI scores come from the same scripted-round harness. This is a reproducible playground, not research-tier evidence.",
  path: "/leaderboard",
};

export const alt = cardAlt(CARD);
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

export default function Image() {
  return renderPageCard(CARD);
}
