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
          title="One public future"
          body={
            "Game " +
            COMPETITION_GAME.gameVersion +
            " pins " +
            COMPETITION_GAME.roundId +
            " and its SHA-256 digest. It is reproducible and inspectable, so it is a competition playground—not research-tier evidence."
          }
        />
        <InfoCard
          title="Three bits per move"
          body="Submitted columns 0–6 are packed into a dense 3-bit stream in DynamoDB. The original JSON move array is not retained."
        />
        <InfoCard
          title="Server score wins"
          body="The Lambda replay rejects illegal, incomplete, or trailing moves. A client/server score mismatch is logged and stored, and only the server score appears in rankings."
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
