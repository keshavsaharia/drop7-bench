/**
 * The link preview for a concept page, from that page's own frontmatter.
 */
import { listConceptPages } from "@/lib/learn";
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

export const dynamic = "force-dynamic";

export const alt = cardAlt({ title: "Concept" });
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

type Props = { params: Promise<{ slug: string }> };

export default async function Image({ params }: Props) {
  const { slug } = await params;
  const page = listConceptPages().find((entry) => entry.slug === slug) ?? null;
  return renderPageCard({
    eyebrow: "Concept",
    title: page?.title || slug,
    summary: page?.summary || undefined,
    path: `/learn/concepts/${slug}`,
  });
}
