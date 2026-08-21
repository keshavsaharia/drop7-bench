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

function GitHubMark() {
  return (
    <svg viewBox="0 0 16 16" width="18" height="18" aria-hidden="true" fill="currentColor">
      <path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.01 8.01 0 0 0 16 8c0-4.42-3.58-8-8-8z" />
    </svg>
  );
}

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
            A guide to the mechanics behind one of the greatest puzzle games of all time. Once
            you learn the rules and <Link href="/play">play the game for a while</Link>, you'll
            see why this is such a captivating research prize.
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
            { href: "/learn/rules", step: "1", title: "Learn the game", text: "Every rule, animated by the engine." },
            { href: "/learn/concepts", step: "2", title: "Learn the ideas", text: "Chance, look-ahead, evaluating a board, the sibling trap, what compute can and cannot buy." },
            { href: "/approaches", step: "3", title: "See what was tried", text: "Twelve strategy families, each in plain language first, with the record one click deeper." },
            { href: "/theories", step: "4", title: "See what is open", text: "Registered claims, how they would be proven wrong, and the large-scale direction." },
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
