import Link from "next/link";
import { CompetitionGame } from "@/components/CompetitionGame";
import { GitHubSignInButton } from "@/components/GitHubSignInButton";
import {
  COMPETITION_GAME,
  COMPETITION_ROUND,
} from "@/lib/competition/game";

export const dynamic = "force-dynamic";

export default function CompetePage() {
  return (
    <div className="space-y-8">
      <section className="max-w-3xl">
        <p className="text-xs font-semibold uppercase tracking-[0.18em] text-violet-400">
          Human strategy lab
        </p>
        <h1 className="mt-2 text-3xl font-black text-zinc-50">Compete on the global game</h1>
        <p className="mt-3 leading-relaxed text-zinc-400">
          You and every computer policy face the same visible discs and the same hidden
          values under gray discs. Play entirely in your browser, then optionally sign in
          and submit only your column choices. The server validates your score and puts you
          on the same leaderboard as all the autonomous strategies.
        </p>
        <div className="mt-4">
          <GitHubSignInButton />
        </div>
        <p className="mt-3 text-sm text-zinc-500">
          You can submit as many runs as you want. When the game is over, you will see an explicit request to
          contribute your moves and score. A submission links your GitHub username and numeric account ID to the
          validated game record; research datasets derived from the move stream omit those identifiers. See the{" "}
          <Link href="/privacy" className="text-violet-300 hover:text-violet-200">privacy policy</Link> and{" "}
          <Link href="/terms" className="text-violet-300 hover:text-violet-200">terms</Link>.
        </p>
      </section>

      <CompetitionGame manifest={COMPETITION_GAME} round={COMPETITION_ROUND} />

      <section className="grid gap-4 text-sm md:grid-cols-3">
        <InfoCard
          title="One global game"
          body={
            "Game " +
            COMPETITION_GAME.gameVersion +
            " pins " +
            COMPETITION_GAME.roundId +
            " and its SHA-256 digest. It is open-source, and can be used by anyone to determine an optimal strategy using any language or framework, and submit their results to the competition leaderboard."
          }
        />
        <InfoCard
          title="Three bits per move"
          body="You play columns 1–7. The submission stores them internally as 0–6 and packs them into a bit stream using three bits per move. These anonymous streams will be released after the competition for open research."
        />
        <InfoCard
          title="Server score wins"
          body="The submitted columns are replayed in a cloud environment which rejects illegal, incomplete, or trailing moves. Only a server-validated score can appear in rankings."
        />
      </section>
    </div>
  );
}

function InfoCard({ title, body }: { title: string; body: string }) {
  return (
    <article className="rounded-xl border border-zinc-800 bg-zinc-900/30 p-4">
      <h2 className="font-bold text-zinc-100">{title}</h2>
      <p className="mt-2 leading-relaxed text-zinc-500">{body}</p>
    </article>
  );
}
