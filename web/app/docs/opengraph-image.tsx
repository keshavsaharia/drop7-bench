/**
 * The link preview for the documents index. The words are the page's own
 * title and description.
 */
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

const CARD = {
  eyebrow: "Research",
  title: "Documents",
  summary:
    "The documents the Drop7 research is bound to: methodology, benchmark contract, ledger, findings, hardware profiles and agent contracts.",
  path: "/docs",
};

export const alt = cardAlt(CARD);
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

export default function Image() {
  return renderPageCard(CARD);
}
