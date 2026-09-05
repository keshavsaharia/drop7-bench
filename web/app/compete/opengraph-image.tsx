import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

const CARD = {
  eyebrow: "Human strategy lab",
  title: "Compete on the global game",
  summary:
    "Play the same visible discs and hidden gray-disc values as every human and computer policy, then submit your column choices for server replay.",
  path: "/compete",
};

export const alt = cardAlt(CARD);
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

export default function Image() {
  return renderPageCard(CARD);
}
