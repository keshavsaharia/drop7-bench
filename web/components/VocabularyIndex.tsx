import { VocabularyCards } from "./LearnCards";
import { PageHeader } from "./PageHeader";

export const VOCABULARY_DESCRIPTION = "Definitions for the game, search, learning, and research results, grouped by topic.";

export function VocabularyIndex() {
  return <div className="vocabulary-index">
    <PageHeader crumbs={[{ href: "/learn", label: "learn the game" }]} title="Vocabulary" lead={VOCABULARY_DESCRIPTION} />
    <VocabularyCards heading="h2" />
    <p className="learn-note">Choose a topic to browse its terms. Each definition has a direct link, and you can find terms with the site search.</p>
  </div>;
}
