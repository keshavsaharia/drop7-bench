import "../app.css";
import Link from "next/link";
import { Badge } from "@/components/Badge";
import { Callout } from "@/components/Board";
import { Button } from "@/components/Button";
import { PageHeader } from "@/components/PageHeader";
import {
  loadCompetitionLeaderboard,
  type CompetitionLeaderboardEntry,
} from "@/lib/competition/ledger";
import {
  COMPETITION_GAME_KEY,
  type CompetitionGameDefinition,
} from "@/lib/competition/game";
import {
  getCompetitionGame,
  listCompetitionGames,
} from "@/lib/competition/registry";
import { loadLeaderboard, type LeaderboardData } from "@/lib/leaderboard";
import { pageMetadata } from "@/lib/metadata";

export const dynamic = "force-dynamic";

export const metadata = pageMetadata({
  title: "Human and AI leaderboard",
  description:
    "Human scores come from server-replayed move sequences. Computer scores come from the same scripted-round harness, which is a reproducible playground rather than research-tier evidence.",
  path: "/leaderboard",
});

const fmt = (value: number | null) =>
  value === null ? "—" : Math.round(value).toLocaleString();

export default async function LeaderboardPage({
  searchParams,
}: {
  searchParams: Promise<{ players?: string; game?: string }>;
}) {
  const data = loadLeaderboard();
  const requested = await searchParams;
  const selectedGame =
    (requested.game && getCompetitionGame(requested.game)) ||
    getCompetitionGame(COMPETITION_GAME_KEY);
  if (!selectedGame) throw new Error("Current competition is not registered");
  const ledger = await loadCompetitionLeaderboard(selectedGame.gameKey);
  const requestedFilter = requested.players;
  const playerFilter =
    requestedFilter === "human" || requestedFilter === "ai"
      ? requestedFilter
      : "all";
  const competition = (
    <CompetitionLeaderboard
      data={data}
      ledgerEntries={ledger.entries}
      ledgerAvailable={ledger.available}
      filter={playerFilter}
      selectedGame={selectedGame}
      games={listCompetitionGames()}
    />
  );

  if (!data) {
    return (
      <div className="space-y-8">
        {competition}
        <div className="max-w-prose">
          <Callout title="No benchmark data yet" tone="warn">
            <p>Run the scripted-round benchmark from the repository root:</p>
            <pre className="mt-2 rounded-md bg-raised p-3 text-ink-1">
              <code>npm run bench</code>
            </pre>
            <p className="mt-2">
              Then refresh this page. Use <code>npm run bench -- --all</code> to include the slow
              reference policies (expectimax D3/D4).
            </p>
          </Callout>
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
        <h2 className="text-h2 font-display font-semibold text-ink">
          Computer policy leaderboard
        </h2>
        <p className="mt-2 max-w-prose text-small text-ink-2">
          Every autonomous policy plays the exact same predetermined rounds. The visible
          disc sequence is fixed by move number, and every gray disc hides a
          fixed value that takes its place when revealed. Two policies therefore
          face identical randomness on every move.{" "}
          <Link href="/learn/benchmarking" className="text-accent hover:underline">
            How the benchmark works →
          </Link>
        </p>
        <p className="mt-1 text-caption text-ink-3">
          Playground evidence only. These competitions do not support qualification claims as per the{" "}
          <Link href="/docs/benchmarks" className="underline underline-offset-2 hover:text-ink-1">
            benchmark guidelines
          </Link>.
        </p>
      </section>

      <section className="overflow-x-auto rounded-lg border border-rule">
        <table className="w-full border-collapse text-small">
          <thead>
            <tr className="bg-raised text-left">
              <th className="label px-3 py-2">#</th>
              <th className="label px-3 py-2">Policy</th>
              <th className="label px-3 py-2 text-right">Mean</th>
              <th className="label px-3 py-2 text-right">Median</th>
              <th className="label px-3 py-2 text-right">Min</th>
              <th className="label px-3 py-2 text-right">Max</th>
              <th className="label px-3 py-2 text-right">Moves</th>
              {data.rounds.map((round) => (
                <th key={round.id} className="label px-3 py-2 text-right" title={round.name}>
                  {round.id.replace("gauntlet-", "G")}
                </th>
              ))}
            </tr>
          </thead>
          <tbody className="tabular">
            {rank.map((summary, index) => {
              const policy = data.policies.find((p) => p.id === summary.policyId);
              return (
                <tr
                  key={summary.policyId}
                  className="border-t border-rule transition-colors hover:bg-hover"
                >
                  <td className="px-3 py-2 text-ink-3">{index + 1}</td>
                  <td className="px-3 py-2">
                    <div className="flex flex-wrap items-center gap-2 font-semibold text-ink">
                      {policy?.name ?? summary.policyId}
                      {policy && !policy.publicInformation && (
                        <Badge
                          label="extended state"
                          title="Reads level/move number in addition to the strict public state"
                        />
                      )}
                    </div>
                    <div className="text-caption text-ink-3">{policy?.family}</div>
                    {policy?.researchPath && (
                      <Link
                        href={policy.researchPath}
                        className="text-caption text-accent hover:underline"
                      >
                        Research →
                      </Link>
                    )}
                  </td>
                  <td className="px-3 py-2 text-right font-semibold text-ink">
                    {fmt(summary.meanScore)}
                  </td>
                  <td className="px-3 py-2 text-right text-ink-1">
                    {fmt(summary.medianScore)}
                  </td>
                  <td className="px-3 py-2 text-right text-ink-2">
                    {fmt(summary.minimumScore)}
                  </td>
                  <td className="px-3 py-2 text-right text-ink-2">
                    {fmt(summary.maximumScore)}
                  </td>
                  <td className="px-3 py-2 text-right text-ink-2">
                    {summary.meanMoves === null ? "—" : summary.meanMoves.toFixed(1)}
                  </td>
                  {data.rounds.map((round) => {
                    const game = data.games.find(
                      (g) => g.policyId === summary.policyId && g.roundId === round.id,
                    );
                    if (!game) {
                      return (
                        <td key={round.id} className="px-3 py-2 text-right text-ink-4">
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
                              ? "font-semibold text-status-completed"
                              : "text-ink-1"
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
        <h2 className="mb-3 text-h3 font-display font-semibold text-ink">Flow diagnostics</h2>
        <div className="overflow-x-auto rounded-lg border border-rule">
          <table className="w-full border-collapse text-small">
            <thead>
              <tr className="bg-raised text-left">
                <th className="label px-3 py-2">Policy</th>
                <th className="label px-3 py-2 text-right">Clears / move</th>
                <th className="label px-3 py-2 text-right">Reveals / move</th>
                <th className="label px-3 py-2 text-right">Max chain</th>
                <th className="label px-3 py-2 text-right">Censored</th>
                <th className="label px-3 py-2 text-right">Illegal</th>
                <th className="label px-3 py-2 text-right">Compute</th>
              </tr>
            </thead>
            <tbody className="tabular">
              {rank.map((summary) => {
                const policy = data.policies.find((p) => p.id === summary.policyId);
                return (
                  <tr key={summary.policyId} className="border-t border-rule">
                    <td className="px-3 py-2 text-ink">{policy?.name ?? summary.policyId}</td>
                    <td className="px-3 py-2 text-right text-ink-1">
                      {summary.meanClearsPerMove.toFixed(2)}
                      <span className="ml-1 text-caption text-ink-3">/ 2.40 target</span>
                    </td>
                    <td className="px-3 py-2 text-right text-ink-1">
                      {summary.meanRevealsPerMove.toFixed(2)}
                      <span className="ml-1 text-caption text-ink-3">/ 1.40 target</span>
                    </td>
                    <td className="px-3 py-2 text-right text-ink-1">{summary.maxChain}</td>
                    <td className="px-3 py-2 text-right text-ink-2">{summary.censoredGames}</td>
                    <td className="px-3 py-2 text-right text-ink-2">{summary.illegalMoves}</td>
                    <td className="px-3 py-2 text-right text-ink-2">
                      {(summary.elapsedMs / 1000).toFixed(1)}s
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
        <p className="mt-2 max-w-prose text-caption text-ink-3">
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
  researchUrl: string | null;
}

function CompetitionLeaderboard({
  data,
  ledgerEntries,
  ledgerAvailable,
  filter,
  selectedGame,
  games,
}: {
  data: LeaderboardData | null;
  ledgerEntries: CompetitionLeaderboardEntry[];
  ledgerAvailable: boolean;
  filter: PlayerFilter;
  selectedGame: CompetitionGameDefinition;
  games: CompetitionGameDefinition[];
}) {
  const policies = new Map(data?.policies.map((policy) => [policy.id, policy]) ?? []);
  const persistedPolicyIds = new Set(
    ledgerEntries
      .filter((entry) => entry.kind === "ai")
      .map((entry) => entry.policyId)
      .filter((policyId): policyId is string => policyId !== null),
  );
  const localAiEntries: CompetitionEntry[] = (data?.games ?? [])
    .filter(
      (game) =>
        game.roundId === selectedGame.manifest.roundId &&
        !persistedPolicyIds.has(game.policyId),
    )
    .map((game) => {
      const policy = policies.get(game.policyId);
      return {
        id: "local-ai:" + game.policyId,
        name: policy?.name ?? game.policyId,
        kind: "ai",
        score: game.score,
        moves: game.moves,
        href: "/leaderboard/" + game.policyId + "/" + game.roundId,
        detail:
          (policy?.publicInformation === false
            ? "extended-state policy"
            : "public policy") + " · local artifact",
        scoreMismatch: false,
        researchUrl: policy?.researchPath ?? null,
      };
    });
  const persistedEntries: CompetitionEntry[] = ledgerEntries.map((entry) => ({
    id: entry.kind + ":" + entry.submissionId,
    name: entry.displayName,
    kind: entry.kind,
    score: entry.verifiedScore,
    moves: entry.moveCount,
    href: "/leaderboard/human/" + entry.submissionId,
    detail:
      (entry.kind === "ai"
        ? `${entry.policyFamily ?? "research"} · ${entry.publicInformation === false ? "extended state" : "public policy"}`
        : entry.sourceApplication === "drop7-mobile"
          ? `mobile app${entry.sourcePlatform ? ` · ${entry.sourcePlatform}` : ""}`
          : entry.provider) +
      " · " +
      new Date(entry.submittedAt).toLocaleDateString(),
    scoreMismatch: entry.scoreMismatch,
    researchUrl: entry.researchUrl,
  }));
  const entries = [...persistedEntries, ...localAiEntries]
    .filter((entry) => filter === "all" || entry.kind === filter)
    .sort((a, b) => b.score - a.score || a.name.localeCompare(b.name));

  return (
    <section className="space-y-4">
      <PageHeader
        title="Human + AI leaderboard"
        lead={`Every entry is scored on ${selectedGame.manifest.roundId}. Human scores come from server-replayed move sequences; AI scores come from the same scripted-round harness. This is a reproducible playground, not research-tier evidence.`}
      >
        <span className="label">
          {selectedGame.manifest.name} · {selectedGame.manifest.gameVersion}
        </span>
        <Button
          href={
            selectedGame.gameKey === COMPETITION_GAME_KEY
              ? "/compete"
              : "/leaderboard"
          }
        >
          {selectedGame.gameKey === COMPETITION_GAME_KEY
            ? "Play this game →"
            : "Current competition →"}
        </Button>
      </PageHeader>

      {games.length > 1 && (
        <div className="flex flex-wrap gap-2" aria-label="Competition game">
          {games.map((game) => (
            <Link
              key={game.gameKey}
              href={leaderboardHref(filter, game.gameKey)}
              aria-current={game.gameKey === selectedGame.gameKey ? "page" : undefined}
              className="app-chip"
            >
              {game.manifest.name}
              {game.gameKey !== COMPETITION_GAME_KEY ? " · archived" : " · current"}
            </Link>
          ))}
        </div>
      )}

      <div className="flex flex-wrap gap-2" aria-label="Leaderboard player filter">
        {(["all", "human", "ai"] as const).map((option) => (
          <Link
            key={option}
            href={leaderboardHref(option, selectedGame.gameKey)}
            aria-current={filter === option ? "page" : undefined}
            className="app-chip"
          >
            {option === "all" ? "Humans + AI" : option === "human" ? "Humans only" : "AI only"}
          </Link>
        ))}
      </div>

      <div className="overflow-x-auto rounded-lg border border-rule">
        <table className="w-full border-collapse text-small">
          <thead>
            <tr className="bg-raised text-left">
              <th className="label px-3 py-2">#</th>
              <th className="label px-3 py-2">Player</th>
              <th className="label px-3 py-2">Type</th>
              <th className="label px-3 py-2 text-right">Verified score</th>
              <th className="label px-3 py-2 text-right">Moves</th>
            </tr>
          </thead>
          <tbody className="tabular">
            {entries.map((entry, index) => (
              <tr
                key={entry.id}
                className="border-t border-rule transition-colors hover:bg-hover"
              >
                <td className="px-3 py-2 text-ink-3">{index + 1}</td>
                <td className="px-3 py-2">
                  <Link href={entry.href} className="font-semibold text-ink hover:text-accent">
                    {entry.name}
                  </Link>
                  <div className="text-caption text-ink-3">{entry.detail}</div>
                  {entry.researchUrl && (
                    <a href={entry.researchUrl} className="text-caption text-accent hover:underline">
                      Research →
                    </a>
                  )}
                </td>
                <td className="px-3 py-2">
                  <span className="app-kind" data-kind={entry.kind}>
                    {entry.kind}
                  </span>
                  {entry.scoreMismatch && (
                    <span className="ml-2">
                      <Badge
                        label="client mismatch"
                        title="The client-reported score differed from the replayed score"
                      />
                    </span>
                  )}
                </td>
                <td className="px-3 py-2 text-right font-semibold text-ink">
                  {entry.score.toLocaleString()}
                </td>
                <td className="px-3 py-2 text-right text-ink-2">{entry.moves}</td>
              </tr>
            ))}
            {entries.length === 0 && (
              <tr className="border-t border-rule">
                <td colSpan={5} className="px-3 py-8 text-center text-ink-3">
                  {!ledgerAvailable
                    ? "The competition ledger is unavailable in this local checkout."
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

function leaderboardHref(filter: PlayerFilter, gameKey: string) {
  const params = new URLSearchParams();
  if (filter !== "all") params.set("players", filter);
  if (gameKey !== COMPETITION_GAME_KEY) params.set("game", gameKey);
  const query = params.toString();
  return query ? `/leaderboard?${query}` : "/leaderboard";
}
