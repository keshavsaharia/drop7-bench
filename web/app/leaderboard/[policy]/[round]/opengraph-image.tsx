import { loadLeaderboard, loadReplay } from "@/lib/leaderboard";
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

export const dynamic = "force-dynamic";
export const alt = cardAlt({ title: "Scripted-round replay" });
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

type Params = Promise<{ policy: string; round: string }>;

export default async function Image({ params }: { params: Params }) {
  const { policy: policyId, round: roundId } = await params;
  const game = loadReplay(policyId, roundId);
  const leaderboard = loadLeaderboard();
  const policy = leaderboard?.policies.find((entry) => entry.id === policyId);
  const round = leaderboard?.rounds.find((entry) => entry.id === roundId);
  return renderPageCard({
    eyebrow: "Scripted-round replay",
    title: `${policy?.name ?? policyId} on ${round?.name ?? roundId}`,
    summary: policy?.description,
    labels: game ? [`${game.score.toLocaleString()} points`, `${game.moves} moves`] : [],
    path: `/leaderboard/${policyId}/${roundId}`,
  });
}
