import "../../../app.css";
import { notFound } from "next/navigation";
import { PageHeader } from "@/components/PageHeader";
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
import { pageMetadata } from "@/lib/metadata";

export const dynamic = "force-dynamic";

type Params = Promise<{ submission: string }>;

export async function generateMetadata({ params }: { params: Params }) {
  const { submission } = await params;
  const record = await getSubmissionRecord(submission);
  return pageMetadata({
    title: record ? `${record.displayName} on the global game` : "Leaderboard replay",
    description: record
      ? `A server-verified ${record.recordType === "policy-score" ? "research policy" : "human"} replay from ${record.roundId}.`
      : undefined,
    path: `/leaderboard/human/${submission}`,
  });
}

export default async function HumanReplayPage({
  params,
}: {
  params: Params;
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
        <PageHeader
          crumbs={[{ href: "/leaderboard", label: "leaderboard" }]}
          title={`${record.displayName} · ${record.verifiedScore.toLocaleString()} points`}
          lead={
            <>
              Verified by replaying {record.moveCount} packed column choices against{" "}
              {record.roundId}. {isPolicy ? "Seeded" : "Submitted"}{" "}
              {new Date(record.submittedAt).toLocaleString()} via{" "}
              {record.sourceApplication === "drop7-mobile" ? "the mobile app" : record.provider}.
            </>
          }
        >
          <span className="label">
            {isPolicy ? "Research policy" : "Human submission"} · {competition.manifest.gameVersion}
          </span>
        </PageHeader>
        {isPolicy && record.researchUrl && (
          <p className="text-small">
            <a href={record.researchUrl} className="text-accent hover:underline">
              Read the underlying research →
            </a>
          </p>
        )}
        {record.scoreMismatch && (
          <p className="mt-2 text-small text-status-paused">
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
