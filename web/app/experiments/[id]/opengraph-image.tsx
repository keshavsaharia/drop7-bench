/**
 * The link preview for one preregistered experiment: its hypothesis, and the
 * tier and classification the record carries.
 */
import { getExperiments } from "@/lib/repo";
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

export const dynamic = "force-dynamic";

export const alt = cardAlt({ title: "Experiment" });
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

type Props = { params: Promise<{ id: string }> };

export default async function Image({ params }: Props) {
  const { id } = await params;
  const experiment = getExperiments().find((record) => record.experimentId === id) ?? null;
  const labels = [experiment?.benchmarkTier, experiment?.classification].filter(
    (label): label is string => typeof label === "string" && label.length > 0,
  );

  return renderPageCard({
    eyebrow: "Experiment",
    title: experiment?.title || id,
    summary: experiment?.hypothesis || undefined,
    labels,
    path: `/experiments/${id}`,
  });
}
