import "../app.css";
import Link from "next/link";
import { Card } from "@/components/Card";
import { Drop7Game } from "@/components/Drop7Game";
import { PageHeader } from "@/components/PageHeader";
import { pageMetadata } from "@/lib/metadata";

export const metadata = pageMetadata({
  title: "Play Drop7",
  description:
    "Play five-move Hardcore Drop7 in the browser with the repository's rules engine, or watch the depth-4 expectimax play it.",
  path: "/play",
});

/** `?seed=0x1234abcd` or `?seed=305419896` replays a specific game. */
function parseSeed(raw: string | undefined): number | undefined {
  if (!raw) return undefined;
  const trimmed = raw.trim();
  const value = /^0x[0-9a-f]{1,8}$/i.test(trimmed)
    ? Number.parseInt(trimmed.slice(2), 16)
    : /^\d{1,10}$/.test(trimmed)
      ? Number(trimmed)
      : Number.NaN;
  return Number.isInteger(value) && value >= 0 && value <= 0xffff_ffff ? value : undefined;
}

export default async function PlayPage({
  searchParams,
}: {
  searchParams: Promise<{ seed?: string }>;
}) {
  const { seed } = await searchParams;
  const initialSeed = parseSeed(seed);

  return (
    <div className="space-y-8">
      <PageHeader
        title="Play Drop7"
        lead={
          <>
            Everything runs locally in your browser. If you don&apos;t know the rules of the game, you can{" "}
            <Link href="/learn/rules" className="text-accent hover:underline">
              learn them here →
            </Link>
          </>
        }
      />

      <Drop7Game
        mode="play"
        modeSwitcher
        eager
        seed={initialSeed}
        bestScoreKey="drop7-console-best-score"
        caption={
          <>
            Switch to <strong className="text-ink">evaluate</strong> to see which column the
            depth-4 expectimax would choose before you move, or <strong className="text-ink">auto</strong> to
            watch it play. Each game has a seed; <em>replay</em> restarts the same sequence of
            discs and reveals, and <code className="app-code">/play?seed=…</code> links to it.
          </>
        }
      />

      <div className="grid gap-4 md:grid-cols-3">
        <Card
          heading="h2"
          title="How points add up"
          summary={
            <>
              A disc cleared in wave <em>d</em> of a chain scores ⌊7·d<sup>2.5</sup>⌋ — 7, 39, 109,
              224, 391 … — so the later waves of a long chain are worth far more than the first.
              Surviving a row rise pays 17,000 points in this five-move Hardcore mode, and
              emptying the board pays 70,000. Long games are mostly rise bonuses; big games are
              chains.
            </>
          }
        />
        <Card
          heading="h2"
          title="What a strong score looks like"
          summary={
            <>
              The research target is a policy whose <strong className="text-ink">mean</strong> score
              exceeds one million. The strongest dependable reference, the C++{" "}
              <Link href="/approach/fair-expectimax/reference" className="text-accent hover:underline">
                fair depth-4 expectimax
              </Link>
              , averaged 308,296 points over a 64-game cohort and 400,675 over its eight-game
              confirmation; its single best recorded game reached 1,246,684. Your best here is
              kept only in this browser, and one game — yours or the solver&apos;s — is never a
              measurement: scores are heavy-tailed.
            </>
          }
        />
        <Card
          heading="h2"
          title="About the solver"
          summary={
            <>
              Evaluate and auto modes run the repository&apos;s TypeScript expectimax
              (<code className="app-code">src/core/typescript/solver.ts</code>, the leaderboard&apos;s
              &ldquo;Expectimax D4&rdquo;) in a Web Worker, through a faster move generator and
              leaf that are parity-tested to give identical values and decisions
              (<code className="app-code">web/lib/play/fast-search.test.ts</code>). It is a
              demonstration of one policy, not tier evidence, and it is a different
              implementation from the C++ reference whose cohort numbers are quoted here.{" "}
              <Link href="/learn/concepts/chance-vs-choice" className="text-accent hover:underline">
                Why it averages over chance →
              </Link>
            </>
          }
        />
      </div>
    </div>
  );
}
