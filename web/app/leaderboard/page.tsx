import Link from "next/link";
import { loadLeaderboard } from "@/lib/leaderboard";

export const dynamic = "force-dynamic";

const fmt = (value: number | null) =>
  value === null ? "—" : Math.round(value).toLocaleString();

export default function LeaderboardPage() {
  const data = loadLeaderboard();

  if (!data) {
    return (
      <div className="max-w-2xl space-y-4">
        <h1 className="text-2xl font-black text-zinc-50">Leaderboard</h1>
        <div className="rounded-xl border border-amber-800 bg-amber-950/40 p-5 text-sm text-amber-100">
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
