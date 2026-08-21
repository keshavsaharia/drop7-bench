import Link from "next/link";
import { Markdown } from "@/components/Markdown";
import { Stat } from "@/components/Board";
import {
  getExperiments,
  getResults,
  getTheories,
  listApproaches,
  listFamilies,
  readRepoFile,
} from "@/lib/repo";
import { loadLeaderboard } from "@/lib/leaderboard";

export const dynamic = "force-dynamic";

export default function OverviewPage() {
  const theories = getTheories();
  const experiments = getExperiments();
  const results = getResults();
  const families = listFamilies();
  const approachCount = families.reduce(
    (sum, family) => sum + listApproaches(family).length,
    0,
  );
  const leaderboard = loadLeaderboard();
  const leader = leaderboard
    ? [...leaderboard.summaries].sort(
        (a, b) => (b.meanScore ?? 0) - (a.meanScore ?? 0),
      )[0]
    : null;
  const leaderPolicy = leaderboard?.policies.find(
    (policy) => policy.id === leader?.policyId,
  );
  const status = readRepoFile("docs/research/status.md");

  return (
    <div className="space-y-10">
      <section>
        <h1 className="text-3xl font-black text-zinc-50">
          The million-point Drop7 program
        </h1>
        <p className="mt-2 max-w-3xl text-zinc-400">
          The goal is a public-information policy whose{" "}
          <strong className="text-zinc-200">mean</strong> score exceeds one
          million points in corrected five-move Hardcore mode. The strongest
          dependable reference, fair depth-4 expectimax, averages about 309k —
          the problem is open. This console tracks every theory, experiment,
          and benchmark in the repository.
        </p>
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

      {leaderboard && leader && (
        <section className="rounded-xl border border-zinc-800 bg-zinc-900/50 p-5">
          <div className="flex flex-wrap items-baseline justify-between gap-2">
            <h2 className="text-lg font-bold text-zinc-100">
              Scripted-round leaderboard
            </h2>
            <Link href="/leaderboard" className="text-sm text-sky-400 hover:text-sky-300">
              View full leaderboard →
            </Link>
          </div>
          <p className="mt-1 text-sm text-zinc-400">
            {leaderboard.policies.length} policies × {leaderboard.rounds.length}{" "}
            deterministic gauntlet rounds. Current leader:{" "}
            <strong className="text-zinc-100">{leaderPolicy?.name ?? leader.policyId}</strong>{" "}
            with a mean of{" "}
            <strong className="text-zinc-100">
              {Math.round(leader.meanScore ?? 0).toLocaleString()}
            </strong>{" "}
            across {leader.games} games.
          </p>
        </section>
      )}

      {status && (
        <section>
          <Markdown source={status} />
        </section>
      )}
    </div>
  );
}
