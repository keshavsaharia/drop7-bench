/**
 * The link preview for one registered theory: the claim, and the lifecycle,
 * assessment and evidence tier the record carries.
 */
import { getTheories } from "@/lib/repo";
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

export const dynamic = "force-dynamic";

export const alt = cardAlt({ title: "Theory" });
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

type Props = { params: Promise<{ id: string }> };

export default async function Image({ params }: Props) {
  const { id } = await params;
  const theory = getTheories().find((record) => record.theoryId === id) ?? null;
  const labels = [theory?.lifecycle, theory?.assessment, theory?.evidenceTier].filter(
    (label): label is string => typeof label === "string" && label.length > 0,
  );

  return renderPageCard({
    eyebrow: "Theory",
    title: theory?.title || id,
    summary: theory?.claim || undefined,
    labels,
    path: `/theories/${id}`,
  });
}
