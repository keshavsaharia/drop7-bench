import { VOCABULARY_DESCRIPTION } from "@/components/VocabularyIndex";
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";
const CARD = { title: "Vocabulary", summary: VOCABULARY_DESCRIPTION, path: "/learn/vocabulary" };
export const alt = cardAlt(CARD);
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;
export default function Image() { return renderPageCard(CARD); }
