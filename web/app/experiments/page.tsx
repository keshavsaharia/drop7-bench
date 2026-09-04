import "../research/research.css";
import { PageHeader } from "@/components/PageHeader";
import { RecordTable } from "@/components/RecordTable";
import { getExperiments } from "@/lib/repo";

export const dynamic = "force-dynamic";

export const metadata = {
  title: "Experiments",
  description: "Preregistered experiment protocols of the Drop7 million-point program, with lifecycle and benchmark tier.",
};

export default function ExperimentsPage() {
  const experiments = getExperiments();
  return (
    <div>
      <PageHeader
        crumbs={[{ href: "/research", label: "research" }]}
        title="Experiments"
        lead="Preregistered protocols: the candidate, the comparator, the cohort, the metrics and the gate, fixed before the data is read."
      />
      {experiments.length === 0 ? (
        <p className="record-empty">No experiment records are present in this checkout.</p>
      ) : (
        <RecordTable kind="experiment" records={experiments} />
      )}
    </div>
  );
}
