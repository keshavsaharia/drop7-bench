import "./home.css";
import Link from "next/link";
import { Badge } from "@/components/Badge";
import { Stat } from "@/components/Board";
import { Button } from "@/components/Button";
import { Card } from "@/components/Card";
import { Drop7Intro } from "@/components/Drop7Intro";
import { Markdown } from "@/components/Markdown";
import { TechniqueArt } from "@/components/technique-art/TechniqueArt";
import { COMPETITION_GAME_KEY } from "@/lib/competition/game";
import { loadCompetitionLeaderboard } from "@/lib/competition/ledger";
import { getCompetitionGame } from "@/lib/competition/registry";
import { loadLeaderboard } from "@/lib/leaderboard";
import { listLogEntries } from "@/lib/log";
import {
  getExperiments,
  getResults,
  getTheories,
  listApproaches,
  listApproachesByTechnique,
  listFamilies,
  readRepoFile,
} from "@/lib/repo";
import { listTechniques } from "@/lib/techniques";

export const dynamic = "force-dynamic";

const STATUS_PATH = "docs/research/status.md";

/**
 * The first `count` second-level sections of a Markdown document, without its
 * document title. Fenced blocks are skipped so a `##` inside a fence is not
 * counted as a heading. Nothing is rewritten or summarised: the text is the
 * document's own, cut at the next `## ` heading.
 */
function leadingSections(markdown: string, count: number): string {
  const lines = markdown.replace(/\r\n?/g, "\n").split("\n");
  const out: string[] = [];
  let fence: string | null = null;
  let seen = 0;
  for (const line of lines) {
    const fenceMatch = /^ {0,3}(`{3,}|~{3,})/.exec(line);
    if (fence) {
      if (fenceMatch && fenceMatch[1][0] === fence[0] && fenceMatch[1].length >= fence.length) {
        fence = null;
      }
    } else if (fenceMatch) {
      fence = fenceMatch[1];
    } else if (/^# /.test(line) && seen === 0) {
      continue;
    } else if (/^## /.test(line)) {
      seen += 1;
      if (seen > count) break;
    }
    out.push(line);
  }
  return out.join("\n").trim();
}

export default async function HomePage() {
  const theories = getTheories();
  const experiments = getExperiments();
  const results = getResults();
  const families = listFamilies();
  const approachCount = families.reduce(
    (sum, family) => sum + listApproaches(family).length,
    0,
  );
  const techniques = listTechniques();
  const logEntries = listLogEntries().slice(0, 3);
  const status = readRepoFile(STATUS_PATH);
  const statusExcerpt = status ? leadingSections(status, 2) : null;

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

  return (
    <div className="home">
      <section className="home-hero" aria-labelledby="home-title">
        <div>
          <h1 id="home-title" className="home-hero-title text-display">
            What is the best strategy in a game with chance?
          </h1>
          <p className="home-hero-lead">
            Drop7 is widely considered the greatest puzzle game of all time<sup className="home-reference">
              <Link
                href="https://en.wikipedia.org/wiki/Drop7"
                target="_blank"
                rel="noopener noreferrer"
                aria-label="Drop7 on Wikipedia, reference 1"
              >
                [1]
              </Link>
            </sup>.{" "}
            The goal of this research is to find an autonomous strategy for playing the game as well as a human can.
          </p>
          <p className="home-hero-lead">
            An experienced human player can score a million points in this game with the right long-term strategic thinking. The strongest research reference so far (<Link href="/approach/fair-expectimax/reference">depth-4 expectimax</Link>) averages about 300k points, so the research problem is very much still open.
          </p>
          <div className="home-hero-actions">
            <Button href="/play">Play the game</Button>
            <Button variant="ghost" href="/learn/rules">
              Read the rules
            </Button>
          </div>
          <p className="home-hero-steps">
            New here? <Link href="/learn/rules">Learn the game</Link>, then{" "}
            <Link href="/learn/concepts">the ideas behind the strategies</Link>, then see{" "}
            <Link href="/approach">what has been tried</Link> and{" "}
            <Link href="/theories">what is still open</Link>.
          </p>
        </div>
        <Drop7Intro />
      </section>

      <section className="home-stats" aria-label="Where things stand">
        <Stat label="Reference mean" value="308,296" hint="fair D4, 64 games (ledger)" />
        <Stat label="Target mean" value="1,000,000+" hint="frozen qualification bar" />
        <Stat label="Approaches" value={String(approachCount)} hint={`${families.length} families`} />
        <Stat label="Theories" value={String(theories.length)} hint="registered claims" />
        <Stat label="Experiments" value={String(experiments.length)} hint="preregistered" />
        <Stat label="Results" value={String(results.length)} hint="recorded outcomes" />
      </section>

      <section className="home-ideas" aria-labelledby="home-ideas-title">
        <div className="home-section-head">
          <div>
            <span className="label">The ideas</span>
            <h2 id="home-ideas-title" className="home-h2">
              Fourteen ways to build a player
            </h2>
            <p className="home-section-lead">
              Each technique is explained from the ground up before it meets the game, and
              each links to the strategies that tried it.
            </p>
          </div>
          <Link href="/approach" className="home-more">
            all approaches →
          </Link>
        </div>
        <ol className="home-technique-grid">
          {techniques.map((technique) => {
            const count = listApproachesByTechnique(technique.slug).length;
            return (
              <li key={technique.slug}>
                <Card
                  href={`/approach/technique/${technique.slug}`}
                  art={<TechniqueArt name={technique.slug} title={technique.title} />}
                  title={technique.title}
                  summary={technique.oneLine}
                  foot={
                    <span className="label">
                      {count === 1 ? "1 approach" : `${count} approaches`}
                    </span>
                  }
                />
              </li>
            );
          })}
        </ol>
      </section>

      <section className="home-evidence">
        <div className="home-evidence-status">
          <h2 className="label">Where the evidence stands</h2>
          {statusExcerpt ? (
            <>
              <Markdown source={statusExcerpt} fromPath={STATUS_PATH} />
              <p className="home-read-more">
                <Link href="/docs/research/status">Read the full status →</Link>
              </p>
            </>
          ) : (
            <p className="home-empty">
              The status document is not present in this checkout.
            </p>
          )}
        </div>
        <aside className="home-evidence-log">
          <h2 className="label">Latest from the log</h2>
          {logEntries.length > 0 ? (
            <ol className="home-log">
              {logEntries.map((entry) => (
                <li key={entry.date}>
                  <Link href={`/log/${entry.date}`}>
                    <time dateTime={entry.date}>{entry.date}</time>
                    <span className="home-log-title">{entry.title}</span>
                    {entry.summary && <p className="home-log-summary">{entry.summary}</p>}
                  </Link>
                </li>
              ))}
            </ol>
          ) : (
            <p className="home-empty">No log entries are present in this checkout.</p>
          )}
          <Link href="/log" className="home-log-all">
            all entries →
          </Link>
        </aside>
      </section>

      <section className="home-compete" aria-labelledby="home-compete-title">
        <div className="home-section-head">
          <div>
            <span className="label">Compete</span>
            <h2 id="home-compete-title" className="home-h2">
              Play the same game as the computer strategies
            </h2>
            <p className="home-section-lead">
              You and every computer policy face the same visible discs and the same hidden
              values. Play in your browser, submit your column choices, and your verified score
              goes on the same leaderboard as the autonomous strategies.
            </p>
          </div>
          <div className="home-actions">
            <Button variant="secondary" href="/leaderboard">
              View the leaderboard
            </Button>
            <Button variant="ghost" href="/compete">
              Compete →
            </Button>
          </div>
        </div>
        <div className="home-leaders">
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
        <p className="home-compete-note">
          Both leaders play {competitionGame.manifest.roundId}, with the same visible discs
          and hidden values.
        </p>
      </section>
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
    <div className="home-leader">
      <span className="label">{label}</span>
      {name !== null && score !== null ? (
        <p className="home-leader-line">
          <strong>{name}</strong>
          {extendedState && (
            <Badge
              label="extended state"
              title="Reads level or move number in addition to the strict public state"
            />
          )}
          <span className="home-leader-score">{score.toLocaleString()} points</span>
        </p>
      ) : (
        <p className="home-leader-empty">{empty}</p>
      )}
    </div>
  );
}
