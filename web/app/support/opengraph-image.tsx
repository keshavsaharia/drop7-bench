import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

const CARD = {
  title: "App support",
  summary: "Help with Drop7 Research, saved games, replays, competitions, and privacy.",
  path: "/support",
};

export const alt = cardAlt(CARD);
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

export default function Image() {
  return renderPageCard(CARD);
}
