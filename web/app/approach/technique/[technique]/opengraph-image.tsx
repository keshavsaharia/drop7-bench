/**
 * The link preview for a technique group, from the catalogue in
 * `lib/techniques.ts`. An unknown slug keeps the slug as the title.
 */
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";
import { TechniqueArt } from "@/components/technique-art/TechniqueArt";
import { getTechnique } from "@/lib/techniques";

export const dynamic = "force-dynamic";

export const alt = cardAlt({ title: "Technique" });
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

type Props = { params: Promise<{ technique: string }> };

export default async function Image({ params }: Props) {
  const { technique: slug } = await params;
  const technique = getTechnique(slug);
  return renderPageCard({
    eyebrow: "Technique",
    title: technique ? technique.title : slug,
    summary: technique?.oneLine,
    path: `/approach/technique/${slug}`,
    art: <TechniqueArt name={technique?.slug ?? "fallback"} mode="static" />,
  });
}
