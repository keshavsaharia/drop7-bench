import "./learn.css";
import Link from "next/link";
import { Card } from "@/components/Card";
import { LessonMotionToggle, ConceptCards, VocabularyCards } from "@/components/LearnCards";
import { PageHeader } from "@/components/PageHeader";
import { LessonArt } from "@/components/technique-art/LessonArt";
import { TechniqueArt } from "@/components/technique-art/TechniqueArt";
import { listConceptPages, listLearnPages, listTechniquePages } from "@/lib/learn";
import { LEARN_DESCRIPTION } from "@/lib/lesson-guides";
import { pageMetadata } from "@/lib/metadata";
import { listTechniques } from "@/lib/techniques";

export const dynamic = "force-dynamic";
export const metadata = pageMetadata({ title: "Learn the game", description: LEARN_DESCRIPTION, path: "/learn" });

export default function LearnPage() {
  const pages = listLearnPages();
  const concepts = listConceptPages().filter((page) => !page.hidden);
  const primers = new Map(listTechniquePages().map((page) => [page.technique, page]));
  const techniques = listTechniques();
  const guides = pages.filter((page) => ["benchmarking", "protocol"].includes(page.slug));
  return (
    <div className="learn-hub learn-motion-scope">
      <PageHeader title="Learn the game" lead={LEARN_DESCRIPTION} />
      <div className="learn-tools"><LessonMotionToggle /></div>
      <section className="learn-section learn-start" aria-label="Start playing Drop7">
        <div className="learn-card-grid learn-card-grid--start">
          <Card href="/learn/rules" className="learn-featured" heading="h2" art={<LessonArt name="rules" mode="loop" />}
            eyebrow="Start here" title="How to play Drop7"
            summary="Drop a disc, match its number, and make room for the next row. See the rules in motion."
            foot={<span className="label">Learn the rules</span>} />
          <Card href="/play" className="learn-play" heading="h2" art={<LessonArt name="play" mode="loop" />}
            eyebrow="Try it yourself" title="Play the game"
            summary="Play a few rounds to get a feel for the drops, chains, and rising board. Then come back to explore the ideas."
            foot={<span className="label">Start playing</span>} />
        </div>
      </section>
      <nav className="learn-jump-nav" aria-label="Learning topics">
        <a href="#concepts">Concepts</a><a href="#vocabulary">Vocabulary</a><a href="#techniques">Techniques</a><a href="#guides">Guides</a>
      </nav>
      <section className="learn-section" aria-labelledby="concepts">
        <div className="learn-section-head">
          <div><h2 id="concepts" className="learn-h2">Concepts</h2>
            <p className="learn-section-lead">Visual lessons on choosing moves, making room on the board, and understanding how strategies learn. Explore any topic that interests you.</p></div>
          <Link href="/learn/concepts" className="learn-more">All concepts →</Link>
        </div>
        {concepts.length ? <ConceptCards pages={concepts} /> : <p className="learn-empty">Concept lessons will appear here when available.</p>}
      </section>
      <section className="learn-section" aria-labelledby="vocabulary">
        <div className="learn-section-head">
          <div><h2 id="vocabulary" className="learn-h2">Vocabulary</h2>
            <p className="learn-section-lead">Short definitions grouped by topic. Each term has its own link, so you can return to it as you read.</p></div>
          <Link href="/learn/vocabulary" className="learn-more">All vocabulary →</Link>
        </div>
        <VocabularyCards />
      </section>
      <section className="learn-section" aria-labelledby="techniques">
        <div className="learn-section-head">
          <div><h2 id="techniques" className="learn-h2">Techniques</h2>
            <p className="learn-section-lead">Explore the methods used to build a player, with worked examples and links to the strategies that use them.</p></div>
          <Link href="/learn/techniques" className="learn-more">All {techniques.length} techniques →</Link>
        </div>
        <ul className="learn-card-grid learn-card-grid--4">
          {techniques.slice(0, 4).map((technique) => {
            const primer = primers.get(technique.slug);
            return <li className="learn-card-wrap" key={technique.slug}><Card
              href={primer ? `/learn/techniques/${primer.slug}` : `/approach/technique/${technique.slug}`}
              art={<TechniqueArt name={technique.slug} mode="loop" />} title={technique.title}
              summary={primer?.summary || technique.oneLine} foot={<span className="label">Read the primer</span>} /></li>;
          })}
        </ul>
      </section>
      <section className="learn-section" aria-labelledby="guides">
        <div className="learn-section-head"><div><h2 id="guides" className="learn-h2">Leaderboard guides</h2>
          <p className="learn-section-lead">How scripted rounds work and how a program sends its moves. The leaderboard is a playground; its totals are not research evidence.</p></div></div>
        <div className="learn-card-grid">
          {guides.map((page) => <Card key={page.slug} href={`/learn/${page.slug}`} className="learn-guide-card"
            art={<LessonArt name={page.slug} mode="loop" />} title={page.title} summary={page.summary} foot={<span className="label">Read the guide</span>} />)}
        </div>
      </section>
      <p className="learn-closing">The methodology and research records are in <Link href="/docs">Docs</Link>. You can also follow the work in the <Link href="/log">research log</Link>.</p>
    </div>
  );
}
