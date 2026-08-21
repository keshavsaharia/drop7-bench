import Link from "next/link";
import {
  loadHumanLeaderboard,
  type HumanLeaderboardEntry,
} from "@/lib/competition/ledger";
import { COMPETITION_GAME } from "@/lib/competition/game";
import { loadLeaderboard, type LeaderboardData } from "@/lib/leaderboard";

export const dynamic = "force-dynamic";

const fmt = (value: number | null) =>
  value === null ? "—" : Math.round(value).toLocaleString();

export default async function LeaderboardPage({
  searchParams,
}: {
  searchParams: Promise<{ players?: string }>;
}) {
  const data = loadLeaderboard();
  const human = await loadHumanLeaderboard();
  const requestedFilter = (await searchParams).players;
  const playerFilter =
    requestedFilter === "human" || requestedFilter === "ai"
      ? requestedFilter
      : "all";
  const competition = (
    <CompetitionLeaderboard
      data={data}
      humanEntries={human.entries}
      humanAvailable={human.available}
      filter={playerFilter}
    />
  );

  if (!data) {
    return (
      <div className="space-y-8">
        {competition}
        <div className="max-w-2xl rounded-xl border border-amber-800 bg-amber-950/40 p-5 text-sm text-amber-100">
          <p className="font-semibold">No benchmark data yet.</p>
          <p className="mt-2">Run the scripted-round benchmark from the repository root:</p>
          <pre className="mt-2 rounded-lg bg-zinc-950 p-3 text-zinc-200">
            <code>npm run bench</code>
          </pre>
          <p className="mt-2">
            Then refresh this page. Use{" "}
            <code>npm run bench -- --all</code> to include the slow reference
            policies (expectimax D3/D4).
          </p>
        </div>
      </div>
    );
  }

  const rank = [...data.summaries].sort(
    (a, b) => (b.meanScore ?? 0) - (a.meanScore ?? 0),
  );
  const bestPerRound = new Map<string, number>();
  for (const round of data.rounds) {
    bestPerRound.set(
      round.id,
      Math.max(
        ...data.games
          .filter((game) => game.roundId === round.id)
          .map((game) => game.score),
      ),
    );
  }

  return (
    <div className="space-y-8">
      {competition}
      <section>
        <h1 className="text-2xl font-black text-zinc-50">
          Scripted-round leaderboard
        </h1>
        <p className="mt-2 max-w-3xl text-sm text-zinc-400">
          Every policy plays the exact same predetermined rounds: the visible
          disc sequence is fixed by move number, and every gray disc hides a
          fixed value that takes its place when revealed. Two policies therefore
          face identical randomness, move for move. Generated{" "}
          {new Date(data.generatedAt).toLocaleString()}.{" "}
          <Link href="/learn/benchmarking" className="text-sky-400 hover:text-sky-300">
            How the benchmark works →
          </Link>
        </p>
        <p className="mt-1 text-xs text-zinc-600">
          Playground evidence only — scripted rounds are not a research tier and
          support no qualification claim (see docs/benchmarks.md).
        </p>
      </section>

      <section className="overflow-x-auto rounded-xl border border-zinc-800">
        <table className="w-full border-collapse text-sm">
          <thead>
            <tr className="bg-zinc-900 text-left text-xs uppercase tracking-wide text-zinc-500">
              <th className="px-3 py-2">#</th>
              <th className="px-3 py-2">Policy</th>
              <th className="px-3 py-2 text-right">Mean</th>
              <th className="px-3 py-2 text-right">Median</th>
              <th className="px-3 py-2 text-right">Min</th>
              <th className="px-3 py-2 text-right">Max</th>
              <th className="px-3 py-2 text-right">Moves</th>
              {data.rounds.map((round) => (
                <th key={round.id} className="px-3 py-2 text-right" title={round.name}>
                  {round.id.replace("gauntlet-", "G")}
                </th>
              ))}
            </tr>
          </thead>
          <tbody>
            {rank.map((summary, index) => {
              const policy = data.policies.find((p) => p.id === summary.policyId);
              return (
                <tr
                  key={summary.policyId}
                  className="border-t border-zinc-800 hover:bg-zinc-900/60"
                >
                  <td className="px-3 py-2 text-zinc-500">{index + 1}</td>
                  <td className="px-3 py-2">
                    <div className="font-semibold text-zinc-100">
                      {policy?.name ?? summary.policyId}
                      {policy && !policy.publicInformation && (
                        <span
                          className="ml-2 rounded bg-amber-900/60 px-1.5 py-0.5 text-[10px] font-medium text-amber-200"
                          title="Reads level/move number in addition to the strict public state"
                        >
                          extended state
                        </span>
                      )}
                    </div>
                    <div className="text-xs text-zinc-500">{policy?.family}</div>
                  </td>
                  <td className="px-3 py-2 text-right font-bold text-zinc-50">
                    {fmt(summary.meanScore)}
                  </td>
                  <td className="px-3 py-2 text-right text-zinc-300">
                    {fmt(summary.medianScore)}
                  </td>
                  <td className="px-3 py-2 text-right text-zinc-400">
                    {fmt(summary.minimumScore)}
                  </td>
                  <td className="px-3 py-2 text-right text-zinc-400">
                    {fmt(summary.maximumScore)}
                  </td>
                  <td className="px-3 py-2 text-right text-zinc-400">
                    {summary.meanMoves === null ? "—" : summary.meanMoves.toFixed(1)}
                  </td>
                  {data.rounds.map((round) => {
                    const game = data.games.find(
                      (g) => g.policyId === summary.policyId && g.roundId === round.id,
                    );
                    if (!game) {
                      return (
                        <td key={round.id} className="px-3 py-2 text-right text-zinc-700">
                          —
                        </td>
                      );
                    }
                    const isBest = game.score === bestPerRound.get(round.id);
                    return (
                      <td key={round.id} className="px-3 py-2 text-right">
                        <Link
                          href={`/leaderboard/${summary.policyId}/${round.id}`}
                          className={`hover:underline ${
                            isBest
                              ? "font-bold text-emerald-400"
                              : "text-zinc-300"
                          }`}
                          title={`Replay ${policy?.name} on ${round.name} (${game.moves} moves)`}
                        >
                          {(game.score / 1000).toFixed(0)}k
                        </Link>
                      </td>
                    );
                  })}
                </tr>
              );
            })}
          </tbody>
        </table>
      </section>

      <section>
        <h2 className="mb-3 text-lg font-bold text-zinc-100">Flow diagnostics</h2>
        <div className="overflow-x-auto rounded-xl border border-zinc-800">
          <table className="w-full border-collapse text-sm">
            <thead>
              <tr className="bg-zinc-900 text-left text-xs uppercase tracking-wide text-zinc-500">
                <th className="px-3 py-2">Policy</th>
                <th className="px-3 py-2 text-right">Clears / move</th>
                <th className="px-3 py-2 text-right">Reveals / move</th>
                <th className="px-3 py-2 text-right">Max chain</th>
                <th className="px-3 py-2 text-right">Censored</th>
                <th className="px-3 py-2 text-right">Illegal</th>
                <th className="px-3 py-2 text-right">Compute</th>
              </tr>
            </thead>
            <tbody>
              {rank.map((summary) => {
                const policy = data.policies.find((p) => p.id === summary.policyId);
                return (
                  <tr key={summary.policyId} className="border-t border-zinc-800">
                    <td className="px-3 py-2 text-zinc-200">{policy?.name ?? summary.policyId}</td>
                    <td className="px-3 py-2 text-right text-zinc-300">
                      {summary.meanClearsPerMove.toFixed(2)}
                      <span className="ml-1 text-xs text-zinc-600">/ 2.40 target</span>
                    </td>
                    <td className="px-3 py-2 text-right text-zinc-300">
                      {summary.meanRevealsPerMove.toFixed(2)}
                      <span className="ml-1 text-xs text-zinc-600">/ 1.40 target</span>
                    </td>
                    <td className="px-3 py-2 text-right text-zinc-300">{summary.maxChain}</td>
                    <td className="px-3 py-2 text-right text-zinc-400">{summary.censoredGames}</td>
                    <td className="px-3 py-2 text-right text-zinc-400">{summary.illegalMoves}</td>
                    <td className="px-3 py-2 text-right text-zinc-400">
                      {(summary.elapsedMs / 1000).toFixed(1)}s
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
        <p className="mt-2 text-xs text-zinc-600">
          The 2.4 clears / 1.4 reveals per move figures are diagnostic targets
          from limited task-record runs, not proven thresholds. Click any score
          above to replay that game move by move.
        </p>
      </section>
    </div>
  );
}

type PlayerFilter = "all" | "human" | "ai";

interface CompetitionEntry {
  id: string;
  name: string;
  kind: "human" | "ai";
  score: number;
  moves: number;
  href: string;
  detail: string;
  scoreMismatch: boolean;
}

function CompetitionLeaderboard({
  data,
  humanEntries,
  humanAvailable,
  filter,
}: {
  data: LeaderboardData | null;
  humanEntries: HumanLeaderboardEntry[];
  humanAvailable: boolean;
  filter: PlayerFilter;
}) {
  const policies = new Map(data?.policies.map((policy) => [policy.id, policy]) ?? []);
  const aiEntries: CompetitionEntry[] = (data?.games ?? [])
    .filter((game) => game.roundId === COMPETITION_GAME.roundId)
    .map((game) => {
      const policy = policies.get(game.policyId);
      return {
        id: "ai:" + game.policyId,
        name: policy?.name ?? game.policyId,
        kind: "ai",
        score: game.score,
        moves: game.moves,
        href: "/leaderboard/" + game.policyId + "/" + game.roundId,
        detail: policy?.publicInformation === false ? "extended-state policy" : "public policy",
        scoreMismatch: false,
      };
    });
  const humans: CompetitionEntry[] = humanEntries.map((entry) => ({
    id: "human:" + entry.submissionId,
    name: entry.displayName,
    kind: "human",
    score: entry.verifiedScore,
    moves: entry.moveCount,
    href: "/leaderboard/human/" + entry.submissionId,
    detail: entry.provider + " · " + new Date(entry.submittedAt).toLocaleDateString(),
    scoreMismatch: entry.scoreMismatch,
  }));
  const entries = [...humans, ...aiEntries]
    .filter((entry) => filter === "all" || entry.kind === filter)
    .sort((a, b) => b.score - a.score || a.name.localeCompare(b.name));

  return (
    <section className="space-y-4">
      <div>
        <p className="text-xs font-semibold uppercase tracking-[0.18em] text-violet-400">
          Global competition · {COMPETITION_GAME.gameVersion}
        </p>
        <div className="mt-2 flex flex-wrap items-end justify-between gap-4">
          <div>
            <h1 className="text-2xl font-black text-zinc-50">Human + AI leaderboard</h1>
            <p className="mt-2 max-w-3xl text-sm text-zinc-400">
              Every entry is scored on {COMPETITION_GAME.roundId}. Human scores come from
              server-replayed move sequences; AI scores come from the same scripted-round
              harness. This is a reproducible playground, not research-tier evidence.
            </p>
          </div>
          <Link
            href="/compete"
            className="rounded-md border border-violet-500/60 px-3 py-2 text-sm font-semibold text-violet-300 hover:bg-violet-500/10"
          >
            Play this game →
          </Link>
        </div>
      </div>

      <div className="flex flex-wrap gap-2" aria-label="Leaderboard player filter">
        {(["all", "human", "ai"] as const).map((option) => (
          <Link
            key={option}
            href={option === "all" ? "/leaderboard" : "/leaderboard?players=" + option}
            aria-current={filter === option ? "page" : undefined}
            className={
              "rounded-full border px-3 py-1 text-xs font-semibold uppercase tracking-wide " +
              (filter === option
                ? "border-violet-400 bg-violet-500/15 text-violet-200"
                : "border-zinc-700 text-zinc-500 hover:border-zinc-500 hover:text-zinc-300")
            }
          >
            {option === "all" ? "Humans + AI" : option === "human" ? "Humans only" : "AI only"}
          </Link>
        ))}
      </div>

      <div className="overflow-x-auto rounded-xl border border-zinc-800">
        <table className="w-full border-collapse text-sm">
          <thead>
            <tr className="bg-zinc-900 text-left text-xs uppercase tracking-wide text-zinc-500">
              <th className="px-3 py-2">#</th>
              <th className="px-3 py-2">Player</th>
              <th className="px-3 py-2">Type</th>
              <th className="px-3 py-2 text-right">Verified score</th>
              <th className="px-3 py-2 text-right">Moves</th>
            </tr>
          </thead>
          <tbody>
            {entries.map((entry, index) => (
              <tr key={entry.id} className="border-t border-zinc-800 hover:bg-zinc-900/60">
                <td className="px-3 py-2 text-zinc-500">{index + 1}</td>
                <td className="px-3 py-2">
                  <Link href={entry.href} className="font-semibold text-zinc-100 hover:text-sky-300">
                    {entry.name}
                  </Link>
                  <div className="text-xs text-zinc-600">{entry.detail}</div>
                </td>
                <td className="px-3 py-2">
                  <span
                    className={
                      "rounded px-1.5 py-0.5 text-[10px] font-semibold uppercase tracking-wide " +
                      (entry.kind === "human"
                        ? "bg-violet-950 text-violet-300"
                        : "bg-sky-950 text-sky-300")
                    }
                  >
                    {entry.kind}
                  </span>
                  {entry.scoreMismatch && (
                    <span className="ml-2 rounded bg-amber-950 px-1.5 py-0.5 text-[10px] text-amber-300">
                      client mismatch
                    </span>
                  )}
                </td>
                <td className="px-3 py-2 text-right font-bold text-zinc-50">
                  {entry.score.toLocaleString()}
                </td>
                <td className="px-3 py-2 text-right text-zinc-400">{entry.moves}</td>
              </tr>
            ))}
            {entries.length === 0 && (
              <tr className="border-t border-zinc-800">
                <td colSpan={5} className="px-3 py-8 text-center text-zinc-500">
                  {filter === "human" && !humanAvailable
                    ? "The human ledger is unavailable in this local checkout."
                    : "No entries in this filter yet."}
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>
    </section>
  );
}
