/**
 * The link preview for one approach directory: what it tried, and the three
 * labels its README already carries. A slug with no directory in this
 * checkout still renders — the slug becomes the title.
 */
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";
import { TechniqueArt } from "@/components/technique-art/TechniqueArt";
import { listApproaches } from "@/lib/repo";
import { getTechnique } from "@/lib/techniques";

export const dynamic = "force-dynamic";

export const alt = cardAlt({ title: "Approach" });
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

type Props = { params: Promise<{ family: string; approach: string }> };

export default async function Image({ params }: Props) {
  const { family, approach } = await params;
  const entry = listApproaches(family).find((item) => item.slug === approach) ?? null;
  const technique = entry?.technique ? getTechnique(entry.technique) : null;
  const labels = [entry?.status, entry?.evidence, entry?.reads].filter(
    (label): label is string => typeof label === "string" && label.length > 0,
  );

  return renderPageCard({
    eyebrow: technique ? technique.title : "Approach",
    title: entry?.title || approach,
    summary: entry?.summary || undefined,
    labels,
    path: `/approach/${family}/${approach}`,
    art: (
      <TechniqueArt
        name={entry?.technique ?? "fallback"}
        approach={{ family, slug: approach }}
        mode="static"
      />
    ),
  });
}
