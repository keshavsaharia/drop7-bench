import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

const CARD = {
  title: "Privacy policy",
  summary:
    "How Drop7 Research handles anonymous server metrics, local game data, GitHub sign-in, and competition submissions.",
  path: "/privacy",
};

export const alt = cardAlt(CARD);
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

export default function Image() {
  return renderPageCard(CARD);
}
