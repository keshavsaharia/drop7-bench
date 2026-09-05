import { getSubmissionRecord } from "@/lib/competition/ledger";
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

export const dynamic = "force-dynamic";
export const alt = cardAlt({ title: "Leaderboard replay" });
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

type Params = Promise<{ submission: string }>;

export default async function Image({ params }: { params: Params }) {
  const { submission } = await params;
  const record = await getSubmissionRecord(submission);
  return renderPageCard({
    eyebrow: record?.recordType === "policy-score" ? "Research policy" : "Human submission",
    title: record ? `${record.displayName} on the global game` : "Leaderboard replay",
    summary: record ? `A server-verified replay from ${record.roundId}.` : undefined,
    labels: record ? [`${record.verifiedScore.toLocaleString()} points`, `${record.moveCount} moves`] : [],
    path: `/leaderboard/human/${submission}`,
  });
}
