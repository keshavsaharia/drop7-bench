/**
 * The link preview for one engine, from the engine catalogue. `role` is the
 * catalogue's one-sentence field; an unknown slug keeps the slug as the title.
 */
import { getEngine } from "@/lib/engines";
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

export const dynamic = "force-dynamic";

export const alt = cardAlt({ title: "Engine" });
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

type Props = { params: Promise<{ slug: string }> };

export default async function Image({ params }: Props) {
  const { slug } = await params;
  const entry = getEngine(slug);
  return renderPageCard({
    eyebrow: "Engine",
    title: entry ? entry.title : slug,
    summary: entry?.role,
    path: `/engines/${slug}`,
  });
}
