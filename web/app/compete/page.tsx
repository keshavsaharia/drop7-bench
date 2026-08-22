import { CompetitionGame } from "@/components/CompetitionGame";
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
          and submit only your column choices. The server replays those choices from the
          immutable game artifact and computes the score independently.
        </p>
        <p className="mt-3 text-sm text-zinc-500">
          Signing in is an explicit choice to contribute. A submitted run stores your OAuth
          provider identity, public handle, game version, compact move sequence, client score,
          server-verified score, and timestamps. Anonymous games never leave this browser.
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
          body="Your column sequence of 0-6 is packed into a bit stream (3 bits per move). These streams are completely anonymous, and will be publicly released at the end of the competition for anyone to use them in research."
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
