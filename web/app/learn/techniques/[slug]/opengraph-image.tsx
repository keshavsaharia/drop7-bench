/**
 * The link preview for a technique primer, from that page's own frontmatter.
 */
import { listTechniquePages } from "@/lib/learn";
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";
import { TechniqueArt } from "@/components/technique-art/TechniqueArt";

export const dynamic = "force-dynamic";

export const alt = cardAlt({ title: "Technique primer" });
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

type Props = { params: Promise<{ slug: string }> };

export default async function Image({ params }: Props) {
  const { slug } = await params;
  const page = listTechniquePages().find((entry) => entry.slug === slug) ?? null;
  return renderPageCard({
    eyebrow: "Primer",
    title: page?.title || slug,
    summary: page?.summary || undefined,
    path: `/learn/techniques/${slug}`,
    art: <TechniqueArt name={page?.technique ?? "fallback"} mode="static" />,
  });
}
