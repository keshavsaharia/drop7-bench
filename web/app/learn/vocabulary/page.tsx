import "../learn.css";
import { VocabularyIndex, VOCABULARY_DESCRIPTION } from "@/components/VocabularyIndex";
import { pageMetadata } from "@/lib/metadata";

export const dynamic = "force-dynamic";
export const metadata = pageMetadata({ title: "Vocabulary", description: VOCABULARY_DESCRIPTION, path: "/learn/vocabulary" });
export default VocabularyIndex;
