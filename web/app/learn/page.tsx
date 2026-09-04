import "./learn.css";
import Link from "next/link";
import { Button } from "@/components/Button";
import { Card } from "@/components/Card";
import { PageHeader } from "@/components/PageHeader";
import { TechniqueArt } from "@/components/technique-art/TechniqueArt";
import {
  listConceptPages,
  listLearnPages,
  listTechniquePages,
  loadConceptPage,
  type LearnPageInfo,
} from "@/lib/learn";
import { listApproachesByTechnique } from "@/lib/repo";
import { listTechniques } from "@/lib/techniques";

export const dynamic = "force-dynamic";

export const metadata = {
  title: "Learn",
  description: "The game, the ideas behind every strategy on this site, and how to read a result.",
};

/** Concept pages in reading order, leaving out any whose frontmatter says `hidden: true`. */
function visibleConceptPages(): LearnPageInfo[] {
  return listConceptPages().filter(
    (page) => loadConceptPage(page.slug)?.data.hidden !== true,
  );
}

const GUIDE_SLUGS = ["benchmarking", "protocol"] as const;
const TECHNIQUE_TILES = 4;

export default function LearnPage() {
  const pages = listLearnPages();
  const rules = pages.find((page) => page.slug === "rules") ?? null;
  const glossary = pages.find((page) => page.slug === "glossary") ?? null;
  const guides = GUIDE_SLUGS.map((slug) => pages.find((page) => page.slug === slug)).filter(
    (page): page is LearnPageInfo => page !== undefined,
  );
  const concepts = visibleConceptPages();
  const primers = new Map(listTechniquePages().map((page) => [page.technique, page]));
  const techniques = listTechniques();
  const tiles = techniques.slice(0, TECHNIQUE_TILES);

  return (
    <div className="learn-hub">
      <PageHeader
        title="Learn"
        lead="The game, the ideas behind every strategy on this site, and how to read a result. Start with the rules if you have not played; the concepts follow in reading order."
      />

      <section className="learn-section" aria-labelledby="learn-game">
        <div className="learn-section-head">
          <div>
            <span className="label">The game</span>
            <h2 id="learn-game" className="learn-h2">
              The rules of Drop7
            </h2>
          </div>
          <Button variant="secondary" href="/play">
            Play
          </Button>
        </div>
        {rules ? (
          <Card
            href="/learn/rules"
            className="learn-card-wide"
            title={rules.title}
            summary={rules.summary}
            foot={<span className="label">Read the rules</span>}
          />
        ) : (
          <p className="learn-empty">
            The rules page is not present in this checkout. It lives at{" "}
            <code>web/content/learn/rules.mdx</code>.
          </p>
        )}
      </section>

      <section className="learn-section" aria-labelledby="learn-ideas">
        <div className="learn-section-head">
          <div>
            <span className="label">The ideas</span>
            <h2 id="learn-ideas" className="learn-h2">
              Concepts in reading order
            </h2>
            <p className="learn-section-lead">
              Each page leans on the ones before it, so the order matters more than it looks.
            </p>
          </div>
          <Link href="/learn/concepts" className="learn-more">
            all concepts →
          </Link>
        </div>
        {concepts.length > 0 ? (
          <ol className="concept-list">
            {concepts.map((page, index) => (
              <li key={page.slug}>
                <Card
                  href={`/learn/concepts/${page.slug}`}
                  eyebrow={String(index + 1).padStart(2, "0")}
                  title={page.title}
                  summary={page.summary}
                />
              </li>
            ))}
          </ol>
        ) : (
          <p className="learn-empty">
            No concept pages are present in this checkout. They live in{" "}
            <code>web/content/learn/concepts/</code>.
          </p>
        )}
      </section>

      <section className="learn-section" aria-labelledby="learn-techniques">
        <div className="learn-section-head">
          <div>
            <span className="label">Techniques</span>
            <h2 id="learn-techniques" className="learn-h2">
              The techniques behind the strategies
            </h2>
            <p className="learn-section-lead">
              Each primer explains one technique with a small example before it meets Drop7,
              then links to the strategies that used it. These are the first four of{" "}
              {techniques.length}.
            </p>
          </div>
          <Link href="/learn/techniques" className="learn-more">
            all {techniques.length} techniques →
          </Link>
        </div>
        <ol className="learn-card-grid learn-card-grid--4">
          {tiles.map((technique) => {
            const primer = primers.get(technique.slug);
            const count = listApproachesByTechnique(technique.slug).length;
            const href = primer
              ? `/learn/techniques/${primer.slug}`
              : `/approaches/technique/${technique.slug}`;
            return (
              <li key={technique.slug}>
                <Card
                  href={href}
                  art={<TechniqueArt name={technique.slug} title={technique.title} />}
                  title={technique.title}
                  summary={primer?.summary || technique.oneLine}
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

      <section className="learn-section" aria-labelledby="learn-vocabulary">
        <div className="learn-section-head">
          <div>
            <span className="label">Vocabulary</span>
            <h2 id="learn-vocabulary" className="learn-h2">
              The words the site uses
            </h2>
          </div>
        </div>
        {glossary ? (
          <Card
            href="/learn/glossary"
            className="learn-card-wide"
            title={glossary.title}
            summary={glossary.summary}
            foot={<span className="label">Grouped by topic</span>}
          />
        ) : (
          <p className="learn-empty">
            The glossary is not present in this checkout. It lives at{" "}
            <code>web/content/learn/glossary.mdx</code>.
          </p>
        )}
      </section>

      <section className="learn-section" aria-labelledby="learn-leaderboard">
        <div className="learn-section-head">
          <div>
            <span className="label">How the leaderboard works</span>
            <h2 id="learn-leaderboard" className="learn-h2">
              The scripted rounds and the wire protocol
            </h2>
            <p className="learn-section-lead">
              The leaderboard runs every policy over the same eight scripted rounds. It is a
              playground and a demonstration; its totals are never research evidence.
            </p>
          </div>
        </div>
        {guides.length > 0 ? (
          <div className="learn-card-grid">
            {guides.map((page) => (
              <Card
                key={page.slug}
                href={`/learn/${page.slug}`}
                title={page.title}
                summary={page.summary}
              />
            ))}
          </div>
        ) : (
          <p className="learn-empty">
            The benchmark and protocol guides are not present in this checkout.
          </p>
        )}
      </section>

      <p className="learn-closing">
        The documents the research is bound to (the methodology, the benchmark contract, the
        status page and the ledger) are under <Link href="/docs">Docs</Link>.
      </p>
    </div>
  );
}
