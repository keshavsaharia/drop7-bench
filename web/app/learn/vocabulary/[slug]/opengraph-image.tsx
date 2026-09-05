import { vocabularyTopic } from "@/lib/vocabulary";
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";
export const dynamic = "force-dynamic";
export const alt = cardAlt({ title: "Vocabulary" });
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;
export default async function Image({ params }: { params: Promise<{ slug: string }> }) {
  const { slug } = await params;
  const topic = vocabularyTopic(slug);
  return renderPageCard({ eyebrow: "Vocabulary", title: topic?.title ?? "Vocabulary", summary: topic?.summary, path: `/learn/vocabulary/${slug}` });
}
