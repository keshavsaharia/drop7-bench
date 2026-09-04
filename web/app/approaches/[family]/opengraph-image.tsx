/**
 * The link preview for a family directory, from the family README's own
 * frontmatter. `familyMeta` falls back to the directory name, so a family
 * that is not in this checkout still renders a card.
 */
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";
import { familyMeta } from "@/lib/repo";

export const dynamic = "force-dynamic";

export const alt = cardAlt({ title: "Approach family" });
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

type Props = { params: Promise<{ family: string }> };

export default async function Image({ params }: Props) {
  const { family } = await params;
  const meta = familyMeta(family);
  return renderPageCard({
    eyebrow: "Family",
    title: meta.title,
    summary: meta.summary || undefined,
    path: `/approaches/${family}`,
  });
}
