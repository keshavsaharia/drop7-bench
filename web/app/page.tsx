import Link from "next/link";
import { Markdown } from "@/components/Markdown";
import { Stat } from "@/components/Board";
import { ShimmerButton } from "@/components/ShimmerButton";
import {
  getExperiments,
  getResults,
  getTheories,
  listApproaches,
  listFamilies,
  readRepoFile,
} from "@/lib/repo";
import { loadLeaderboard } from "@/lib/leaderboard";
import { loadCompetitionLeaderboard } from "@/lib/competition/ledger";
import { COMPETITION_GAME_KEY } from "@/lib/competition/game";
import { getCompetitionGame } from "@/lib/competition/registry";

export const dynamic = "force-dynamic";

function GitHubMark() {
  return (
    <svg viewBox="0 0 16 16" width="18" height="18" aria-hidden="true" fill="currentColor">
      <path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.01 8.01 0 0 0 16 8c0-4.42-3.58-8-8-8z" />
    </svg>
  );
}

export default async function OverviewPage() {
  const theories = getTheories();
  const experiments = getExperiments();
  const results = getResults();
  const families = listFamilies();
  const approachCount = families.reduce(
    (sum, family) => sum + listApproaches(family).length,
    0,
  );
  const leaderboard = loadLeaderboard();
  const competitionGame = getCompetitionGame(COMPETITION_GAME_KEY);
  if (!competitionGame) throw new Error("Current competition is not registered");
  const competitionLeaderboard = await loadCompetitionLeaderboard(
    COMPETITION_GAME_KEY,
  );
  const humanLeader = competitionLeaderboard.entries
    .filter((entry) => entry.kind === "human")
    .sort((a, b) => b.verifiedScore - a.verifiedScore)[0] ?? null;
  const persistedPolicyIds = new Set(
    competitionLeaderboard.entries
      .filter((entry) => entry.kind === "ai" && entry.policyId !== null)
      .map((entry) => entry.policyId),
  );
  const computerLeader = [
    ...competitionLeaderboard.entries
      .filter((entry) => entry.kind === "ai")
      .map((entry) => ({
        name: entry.displayName,
        score: entry.verifiedScore,
        extendedState: entry.publicInformation === false,
      })),
    ...(leaderboard?.games ?? [])
      .filter(
        (game) =>
          game.roundId === competitionGame.manifest.roundId &&
          !persistedPolicyIds.has(game.policyId),
      )
      .map((game) => {
        const policy = leaderboard?.policies.find(
          (candidate) => candidate.id === game.policyId,
        );
        return {
          name: policy?.name ?? game.policyId,
          score: game.score,
          extendedState: policy?.publicInformation === false,
        };
      }),
  ].sort((a, b) => b.score - a.score)[0] ?? null;
  const status = readRepoFile("docs/research/status.md");

  return (
    <div className="space-y-10">
      <section>
        <h1 className="text-3xl font-black text-zinc-50">
          What is the best strategy in a game with chance?
        </h1>
        <p className="mt-2 max-w-3xl text-zinc-400">
          <Link href="https://en.wikipedia.org/wiki/Drop7" target="_blank" className="text-sky-400 hover:text-sky-300">Drop7</Link> 
          {' '}is widely considered one of the great puzzle games of all time.
          The goal of this research is to find an autonomous strategy for playing the game as well as a human can.
        </p>
        <p className="mt-2 max-w-3xl text-zinc-400">
          A decent human player can easily score multiple millions of points in this game with the right long-term strategic thinking. The strongest 
          research reference so far (<Link href="/approaches/fair-expectimax/reference" className="text-sky-400 hover:text-sky-300">fair depth-4 expectimax</Link>) 
          averages about 309k points, so the research problem is very much still open.
        </p>
      </section>

      <section className="grid gap-4 sm:grid-cols-2">
        <Link
          href="/learn/rules"
          className="group rounded-xl border border-sky-900/70 bg-sky-950/30 p-5 hover:border-sky-700"
        >
          <div className="text-xs font-semibold uppercase tracking-wide text-sky-300">
            Learn
          </div>
          <h2 className="mt-1 text-lg font-bold text-zinc-50 group-hover:text-white">
            How Drop7 works
          </h2>
          <p className="mt-1 text-sm text-zinc-400">
            A guide to the mechanics behind one of the greatest puzzle games of all time. Learn the rules, play the game for a while, and see for yourself why this is such a captivating research prize.
          </p>
          <span className="mt-3 inline-block text-sm text-sky-400 group-hover:text-sky-300">
            Click to read how Drop7 works →
          </span>
        </Link>
        <a
          href="https://github.com/keshavsaharia/drop7-bench"
          rel="noopener noreferrer"
          className="group rounded-xl border border-zinc-800 bg-zinc-900/50 p-5 hover:border-zinc-600"
        >
          <div className="text-xs font-semibold uppercase tracking-wide text-zinc-500">
            Open Source
          </div>
          <h2 className="mt-1 flex items-center gap-2 text-lg font-bold text-zinc-50">
            <GitHubMark />
            keshavsaharia/drop7-bench
          </h2>
          <p className="mt-1 text-sm text-zinc-400">
            All of the game engines, approaches, research records, scripted-round benchmarks, and this website are all open-source, and automatically rebuild from GitHub Actions.
          </p>
          <span className="mt-3 inline-block text-sm text-zinc-300 group-hover:text-zinc-100">
            github.com/keshavsaharia/drop7-bench →
          </span>
        </a>
      </section>

      <section className="rounded-xl border border-zinc-800 bg-zinc-900/40 p-5">
        <h2 className="text-sm font-semibold uppercase tracking-wide text-zinc-500">
          How to get involved in the research
        </h2>
        <ol className="mt-3 grid gap-3 sm:grid-cols-2 lg:grid-cols-4">
          {[
            { href: "/learn/rules", step: "1", title: "Learn the game", text: "Learn the rules, and play the game right here in your browser." },
            { href: "/learn/concepts", step: "2", title: "Learn the ideas", text: "Chance, look-ahead, evaluating a board, the sibling trap, and most importantly, what compute can and cannot buy." },
            { href: "/approaches", step: "3", title: "See what was tried", text: "Twelve strategy families have been developed so far, and each has a hierarchy of records into specific approaches that were attempted." },
            { href: "/theories", step: "4", title: "See what is open", text: "Browse the theories and contribute your own, to guide the large-scale direction of the research." },
          ].map((item) => (
            <li key={item.href}>
              <Link
                href={item.href}
                className="block h-full rounded-lg border border-zinc-800 bg-zinc-950/50 p-3 hover:border-sky-800"
              >
                <div className="text-xs text-zinc-500">step {item.step}</div>
                <div className="mt-0.5 font-semibold text-zinc-100">{item.title}</div>
                <div className="mt-1 text-xs text-zinc-400">{item.text}</div>
              </Link>
            </li>
          ))}
        </ol>
      </section>

      <section className="grid grid-cols-2 gap-3 sm:grid-cols-3 lg:grid-cols-6">
        <Stat label="Approaches" value={String(approachCount)} hint={`${families.length} families`} />
        <Stat label="Theories" value={String(theories.length)} hint="registered claims" />
        <Stat label="Experiments" value={String(experiments.length)} hint="preregistered" />
        <Stat label="Results" value={String(results.length)} hint="recorded outcomes" />
        <Stat
          label="Reference mean"
          value="308,296"
          hint="fair D4, 64 games (ledger)"
        />
        <Stat
          label="Target mean"
          value="1,000,000+"
          hint="frozen qualification bar"
        />
      </section>

      <section className="rounded-xl border border-zinc-800 bg-zinc-900/50 p-5">
        <div className="flex flex-wrap items-center justify-between gap-3">
          <h2 className="text-lg font-bold text-zinc-100">
            Global human-computer competition
          </h2>
          <ShimmerButton href="/leaderboard">View leaderboard</ShimmerButton>
        </div>
        <div className="mt-4 grid gap-3 sm:grid-cols-2">
          <CompetitionLeader
            label="Current human leader"
            name={humanLeader?.displayName ?? null}
            score={humanLeader?.verifiedScore ?? null}
            empty={
              competitionLeaderboard.available
                ? "No verified human score yet"
                : "Competition ledger unavailable"
            }
          />
          <CompetitionLeader
            label="Current computer leader"
            name={computerLeader?.name ?? null}
            score={computerLeader?.score ?? null}
            extendedState={computerLeader?.extendedState ?? false}
            empty="No computer score available"
          />
        </div>
        <p className="mt-3 text-xs text-zinc-500">
          Both leaders play {competitionGame.manifest.roundId}, with the same
          visible discs and hidden values.
        </p>
      </section>

      {status && (
        <section>
          <Markdown source={status} fromPath="docs/research/status.md" />
        </section>
      )}
    </div>
  );
}

function CompetitionLeader({
  label,
  name,
  score,
  extendedState = false,
  empty,
}: {
  label: string;
  name: string | null;
  score: number | null;
  extendedState?: boolean;
  empty: string;
}) {
  return (
    <div className="rounded-lg border border-zinc-800 bg-zinc-950/50 px-4 py-3">
      <p className="text-xs font-semibold uppercase tracking-wide text-sky-400">
        {label}
      </p>
      {name !== null && score !== null ? (
        <p className="mt-1 text-sm text-zinc-300">
          <strong className="text-zinc-100">{name}</strong>
          {extendedState && (
            <span
              className="ml-2 rounded bg-amber-900/60 px-1.5 py-0.5 text-[10px] font-medium text-amber-200"
              title="Reads level or move number in addition to the strict public state"
            >
              extended state
            </span>
          )}
          <span className="text-zinc-500"> · </span>
          {score.toLocaleString()} points
        </p>
      ) : (
        <p className="mt-1 text-sm text-zinc-500">{empty}</p>
      )}
    </div>
  );
}
