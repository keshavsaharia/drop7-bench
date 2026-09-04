import "../app.css";
import Link from "next/link";
import { Card } from "@/components/Card";
import { CompetitionGame } from "@/components/CompetitionGame";
import { GitHubSignInButton } from "@/components/GitHubSignInButton";
import { PageHeader } from "@/components/PageHeader";
import {
  COMPETITION_GAME,
  COMPETITION_ROUND,
} from "@/lib/competition/game";

export const dynamic = "force-dynamic";

export default function CompetePage() {
  return (
    <div className="space-y-8">
      <PageHeader
        crumbs={[{ href: "/leaderboard", label: "leaderboard" }]}
        title="Compete on the global game"
        lead="You and every computer policy face the same visible discs and the same hidden values under gray discs. Play entirely in your browser, then optionally sign in and submit only your column choices. The server validates your score and puts you on the same leaderboard as all the autonomous strategies."
      >
        <span className="label">Human strategy lab</span>
      </PageHeader>

      <div>
        <GitHubSignInButton />
      </div>

      <CompetitionGame manifest={COMPETITION_GAME} round={COMPETITION_ROUND} />

      <section className="max-w-prose">
        <p className="text-small text-ink-2">
          You can submit as many runs as you want. When the game is over, you will see an explicit request to
          contribute your moves and score. A submission links your GitHub username and numeric account ID to the
          validated game record; research datasets derived from the move stream omit those identifiers. See the{" "}
          <Link href="/privacy" className="text-accent hover:underline">privacy policy</Link> and{" "}
          <Link href="/terms" className="text-accent hover:underline">terms</Link>.
        </p>
      </section>

      <div className="grid gap-4 md:grid-cols-3">
        <Card
          heading="h2"
          title="One global game"
          summary={
            "Game " +
            COMPETITION_GAME.gameVersion +
            " pins " +
            COMPETITION_GAME.roundId +
            " and its SHA-256 digest. It is open-source, and can be used by anyone to determine an optimal strategy using any language or framework, and submit their results to the competition leaderboard."
          }
        />
        <Card
          heading="h2"
          title="Three bits per move"
          summary="Since every turn involves making one of seven choices, your sequence of choices in a game can be packed into a bit stream using three bits per move. These anonymous streams will be publicly released at the end of every competition, to hopefully advance the open research. Submitter information will never be shared."
        />
        <Card
          heading="h2"
          title="Server score wins"
          summary="The submitted columns are replayed in a cloud environment which rejects illegal, incomplete, or trailing moves. Only a server-validated score can appear in rankings."
        />
      </div>
    </div>
  );
}
