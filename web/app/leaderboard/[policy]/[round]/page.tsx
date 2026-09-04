import "../../../app.css";
import { notFound } from "next/navigation";
import { PageHeader } from "@/components/PageHeader";
import { ReplayPlayer } from "@/components/ReplayPlayer";
import { loadLeaderboard, loadReplay } from "@/lib/leaderboard";

export const dynamic = "force-dynamic";

export default async function ReplayPage({
  params,
}: {
  params: Promise<{ policy: string; round: string }>;
}) {
  const { policy: policyId, round: roundId } = await params;
  const game = loadReplay(policyId, roundId);
  const leaderboard = loadLeaderboard();
  if (!game) notFound();
  const policy = leaderboard?.policies.find((p) => p.id === policyId);
  const round = leaderboard?.rounds.find((r) => r.id === roundId);

  return (
    <div className="space-y-6">
      <PageHeader
        crumbs={[{ href: "/leaderboard", label: "leaderboard" }]}
        title={`${policy?.name ?? policyId} on ${round?.name ?? roundId}`}
        lead={
          <>
            Final score <strong className="text-ink">{game.score.toLocaleString()}</strong> over{" "}
            {game.moves} moves
            {game.censored ? " (censored at the move cap)" : ""}. Every disc and every gray-disc
            value is predetermined by the round script; replaying the same policy always produces
            this exact game. Checksum <code className="app-code">{game.checksum}</code>
          </>
        }
      />
      <ReplayPlayer game={game} />
    </div>
  );
}
