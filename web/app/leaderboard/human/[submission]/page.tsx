import Link from "next/link";
import { notFound } from "next/navigation";
import { ReplayPlayer } from "@/components/ReplayPlayer";
import {
  columnsFromRecord,
  getSubmissionRecord,
} from "@/lib/competition/ledger";
import {
  getCompetitionGame,
} from "@/lib/competition/registry";
import { replayCompetitionColumns } from "@/lib/competition/replay";
import type { ReplayData } from "@/lib/leaderboard";

export const dynamic = "force-dynamic";

export default async function HumanReplayPage({
  params,
}: {
  params: Promise<{ submission: string }>;
}) {
  const { submission } = await params;
  const record = await getSubmissionRecord(submission);
  if (!record) notFound();
  const isPolicy = record.recordType === "policy-score";
  const competition = getCompetitionGame(record.gameKey);
  if (!competition) {
    throw new Error("This submission's immutable game is not in the registry");
  }
  const columns = columnsFromRecord(record);
  const replay = replayCompetitionColumns(competition.round, columns, {
    captureAnimation: true,
  });
  if (!replay.valid || replay.score !== record.verifiedScore) {
    throw new Error("Stored competition submission failed its integrity replay");
  }

  const cleared = replay.frames.reduce((sum, frame) => sum + frame.cleared, 0);
  const revealed = replay.frames.reduce((sum, frame) => sum + frame.revealed, 0);
  const maxChain = replay.frames.reduce(
    (maximum, frame) => Math.max(maximum, frame.chainDepth),
    0,
  );
  const replayData: ReplayData = {
    policyId: record.policyId ?? "human-" + record.submissionId.slice(0, 12),
    roundId: record.roundId,
    score: record.verifiedScore,
    moves: record.moveCount,
    censored: record.censored,
    maxChain,
    discsCleared: cleared,
    coveredRevealed: revealed,
    illegalMoves: 0,
    elapsedMs: 0,
    checksum: record.submissionId.slice(0, 16),
    frames: replay.frames,
  };

  return (
    <div className="space-y-7">
      <section>
        <Link href="/leaderboard" className="text-sm text-sky-400 hover:text-sky-300">
          ← Leaderboard
        </Link>
        <p className="mt-5 text-xs font-semibold uppercase tracking-[0.18em] text-sky-400">
          {isPolicy ? "Research policy" : "Human submission"} ·{" "}
          {competition.manifest.gameVersion}
        </p>
        <h1 className="mt-2 text-2xl font-black text-zinc-50">
          {record.displayName} · {record.verifiedScore.toLocaleString()} points
        </h1>
        <p className="mt-2 text-sm text-zinc-500">
          Verified by replaying {record.moveCount} packed column choices against{" "}
          {record.roundId}. {isPolicy ? "Seeded" : "Submitted"}{" "}
          {new Date(record.submittedAt).toLocaleString()} via {record.provider}.
        </p>
        {isPolicy && record.researchUrl && (
          <p className="mt-2 text-sm">
            <a
              href={record.researchUrl}
              className="text-sky-400 hover:text-sky-300"
            >
              Read the underlying research →
            </a>
          </p>
        )}
        {record.scoreMismatch && (
          <p className="mt-2 text-sm text-amber-300">
            Client reported {record.clientScore.toLocaleString()}; the independently replayed{" "}
            {record.verifiedScore.toLocaleString()} score is the leaderboard value. The mismatch
            is retained in the ledger and CloudWatch logs.
          </p>
        )}
      </section>
      <ReplayPlayer game={replayData} />
    </div>
  );
}
