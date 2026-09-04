import "../research/research.css";
import { PageHeader } from "@/components/PageHeader";
import { RecordTable } from "@/components/RecordTable";
import { getTheories } from "@/lib/repo";

export const dynamic = "force-dynamic";

export const metadata = {
  title: "Theories",
  description: "Registered falsifiable claims of the Drop7 million-point program, with their assessment and evidence tier.",
};

export default function TheoriesPage() {
  const theories = getTheories();
  return (
    <div>
      <PageHeader
        crumbs={[{ href: "/research", label: "research" }]}
        title="Theories"
        lead="Registered falsifiable claims: each names a mechanism and the criteria that would refute it."
      />
      {theories.length === 0 ? (
        <p className="record-empty">No theory records are present in this checkout.</p>
      ) : (
        <RecordTable kind="theory" records={theories} />
      )}
    </div>
  );
}
