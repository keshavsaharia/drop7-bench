import Link from "next/link";
import { notFound } from "next/navigation";
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
      <div>
        <Link
          href="/leaderboard"
          className="text-sm text-sky-400 hover:text-sky-300"
        >
          ← Leaderboard
        </Link>
        <h1 className="mt-1 text-2xl font-black text-zinc-50">
          {policy?.name ?? policyId} on {round?.name ?? roundId}
        </h1>
        <p className="mt-1 text-sm text-zinc-400">
          Final score{" "}
          <strong className="text-zinc-100">{game.score.toLocaleString()}</strong>{" "}
          over {game.moves} moves
          {game.censored ? " (censored at the move cap)" : ""}. Every disc and
          every gray-disc value is predetermined by the round script; replaying
          the same policy always produces this exact game. Checksum{" "}
          <code className="rounded bg-zinc-800 px-1 text-xs">{game.checksum}</code>
        </p>
      </div>
      <ReplayPlayer game={game} />
    </div>
  );
}
