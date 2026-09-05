import "../research/research.css";
import { PageHeader } from "@/components/PageHeader";
import { RecordTable } from "@/components/RecordTable";
import { pageMetadata } from "@/lib/metadata";
import { getExperiments, getResults } from "@/lib/repo";

export const dynamic = "force-dynamic";

export const metadata = pageMetadata({
  title: "Results",
  description: "Recorded outcomes of the Drop7 million-point program, each with run validity, scientific outcome and evidence tier.",
  path: "/results",
});

export default function ResultsPage() {
  const results = getResults();
  const experiments = getExperiments();
  return (
    <div>
      <PageHeader
        crumbs={[{ href: "/research", label: "research" }]}
        title="Results"
        lead="Recorded outcomes, each carrying its run validity, scientific outcome and evidence tier."
      />
      {results.length === 0 ? (
        <p className="record-empty">No result records are present in this checkout.</p>
      ) : (
        <RecordTable kind="result" records={results} experiments={experiments} />
      )}
    </div>
  );
}
