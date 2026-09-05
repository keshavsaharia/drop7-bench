import { Card } from "./Card";
import { LessonArt } from "./technique-art/LessonArt";
import type { LearnPageInfo } from "@/lib/learn";
import { VOCABULARY_TOPICS } from "@/lib/vocabulary";

export function ConceptCards({ pages, heading = "h3" }: { pages: LearnPageInfo[]; heading?: "h2" | "h3" }) {
  return <ol className="learn-card-grid learn-card-grid--concepts">
    {pages.map((page, index) => <li className="learn-card-wrap" key={page.slug}>
      <Card href={`/learn/concepts/${page.slug}`} art={<LessonArt name={page.slug} mode="loop" />} heading={heading}
        eyebrow={`Lesson ${String(index + 1).padStart(2, "0")}`} title={page.title} summary={page.summary}
        foot={<span className="label">Explore the concept</span>} />
    </li>)}
  </ol>;
}

export function VocabularyCards({ heading = "h3" }: { heading?: "h2" | "h3" }) {
  return <ul className="learn-card-grid learn-card-grid--4">
    {VOCABULARY_TOPICS.map((topic) => <li className="learn-card-wrap" id={topic.anchor} key={topic.slug}>
      <Card href={`/learn/vocabulary/${topic.slug}`} heading={heading} art={<LessonArt name={topic.art} mode="loop" />}
        title={topic.title} summary={topic.summary} foot={<span className="label">Browse definitions</span>} />
    </li>)}
  </ul>;
}

export function LessonMotionToggle() {
  return <label className="learn-motion-toggle"><input type="checkbox" />Pause illustrations</label>;
}
