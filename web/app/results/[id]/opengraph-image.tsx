/**
 * The link preview for one result record: the experiment it reports on, the
 * result's own summary, and the three labels the record carries. A result id
 * that is not in this checkout keeps the id as the title.
 */
import { getExperiments, getResults } from "@/lib/repo";
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

export const dynamic = "force-dynamic";

export const alt = cardAlt({ title: "Result" });
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

type Props = { params: Promise<{ id: string }> };

export default async function Image({ params }: Props) {
  const { id } = await params;
  const result = getResults().find((record) => record.resultId === id) ?? null;
  const experiment = result
    ? (getExperiments().find((record) => record.experimentId === result.experimentId) ?? null)
    : null;
  const labels = [result?.runValidity, result?.scientificOutcome, result?.evidenceTier].filter(
    (label): label is string => typeof label === "string" && label.length > 0,
  );

  return renderPageCard({
    eyebrow: "Result",
    title: experiment?.title || id,
    summary: result?.summary || undefined,
    labels,
    path: `/results/${id}`,
  });
}
