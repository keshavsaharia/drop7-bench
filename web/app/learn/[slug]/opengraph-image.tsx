/**
 * The link preview for a Learn page, from that page's own frontmatter. A slug
 * with no file in this checkout keeps the slug as the title.
 */
import { listLearnPages } from "@/lib/learn";
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

export const dynamic = "force-dynamic";

export const alt = cardAlt({ title: "Learn" });
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

type Props = { params: Promise<{ slug: string }> };

export default async function Image({ params }: Props) {
  const { slug } = await params;
  const page = listLearnPages().find((entry) => entry.slug === slug) ?? null;
  return renderPageCard({
    eyebrow: "Learn",
    title: page?.title || slug,
    summary: page?.summary || undefined,
    path: `/learn/${slug}`,
  });
}
