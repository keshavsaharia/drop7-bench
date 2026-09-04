import "../research/research.css";
import Link from "next/link";
import { PageHeader } from "@/components/PageHeader";
import { AgentMark } from "@/components/RecordAside";
import { listDocCatalogue } from "@/lib/docs";

export const dynamic = "force-dynamic";

export const metadata = {
  title: "Documents",
  description:
    "The documents the Drop7 research is bound to: methodology, benchmark contract, ledger, findings, hardware profiles and agent contracts.",
};

export default function DocsIndexPage() {
  const groups = listDocCatalogue();
  return (
    <div>
      <PageHeader
        crumbs={[{ href: "/research", label: "research" }]}
        title="Documents"
        lead="The documents this research is bound to: how a game is scored, how a claim is tested, what has been recorded, and what an agent working on the program must follow."
      />
      {groups.length === 0 && <p className="record-empty">No documents are present in this checkout.</p>}
      {groups.map((group) => (
        <section key={group.id} className="docs-group" aria-labelledby={`docs-group-${group.id}`}>
          <h2 id={`docs-group-${group.id}`} className="docs-group-head">
            {group.agentFacing && (
              <span className="docs-agent-mark" title="Written for agents">
                <AgentMark />
              </span>
            )}
            {group.title}
          </h2>
          <p className="docs-group-summary">
            {group.summary}
            {group.agentFacing && " Agent-facing."}
          </p>
          <ul className="docs-list">
            {group.entries.map((entry) => (
              <li key={entry.slug}>
                <Link href={entry.href}>{entry.title}</Link>
                <span className="docs-path">{entry.path}</span>
              </li>
            ))}
          </ul>
        </section>
      ))}
    </div>
  );
}
