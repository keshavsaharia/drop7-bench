import "../../research/research.css";
import Link from "next/link";
import { notFound } from "next/navigation";
import { ArticleLayout } from "@/components/ArticleLayout";
import { Badge } from "@/components/Badge";
import { Mdx } from "@/components/Mdx";
import { PageHeader } from "@/components/PageHeader";
import {
  RECORD_DIR,
  RecordAside,
  RecordField,
  RecordList,
  RepoRef,
  recordHref,
} from "@/components/RecordAside";
import { firstSentence } from "@/components/RecordTable";
import { AgentContext, TechnicalRecord } from "@/components/Reveal";
import { extractHeadings } from "@/lib/headings";
import { pageMetadata } from "@/lib/metadata";
import { loadRecordOverlay } from "@/lib/research";
import { getExperiments, getResults, getTheories } from "@/lib/repo";

export const dynamic = "force-dynamic";

type Params = Promise<{ id: string }>;

export async function generateMetadata({ params }: { params: Params }) {
  const { id } = await params;
  const theory = getTheories().find((t) => t.theoryId === id);
  return pageMetadata({
    title: theory ? theory.title : "Theory",
    path: `/theories/${id}`,
  });
}

export default async function TheoryPage({ params }: { params: Params }) {
  const { id } = await params;
  const theory = getTheories().find((t) => t.theoryId === id);
  if (!theory) notFound();
  const overlay = loadRecordOverlay(id);
  const toc = overlay ? extractHeadings(overlay.content, { minDepth: 2, maxDepth: 3 }) : [];
  const experimentIds = getExperiments()
    .filter((e) => e.theoryIds.includes(id))
    .map((e) => e.experimentId);
  const resultIds = getResults()
    .filter((r) => r.theoryIds.includes(id))
    .map((r) => r.resultId);
  const dependencies = theory.dependencies ?? [];
  const evidenceRefs = theory.evidenceRefs ?? [];
  const recordFile = `research/${RECORD_DIR.theory}/${theory.$id}.json`;

  return (
    <div>
      <PageHeader
        crumbs={[
          { href: "/research", label: "research" },
          { href: "/theories", label: "theories" },
        ]}
        title={theory.title}
        lead={firstSentence(theory.claim)}
      >
        <Badge kind="outcome" value={theory.assessment} />
        <Badge kind="status" value={theory.lifecycle} />
        <Badge kind="tier" value={theory.evidenceTier} />
        <Badge value={theory.informationClass} />
        <Badge label={theory.theoryId} className="record-id" />
      </PageHeader>

      <ArticleLayout
        toc={toc}
        aside={
          <RecordAside
            id={theory.theoryId}
            kind="theory"
            dependencyIds={dependencies}
            experimentIds={experimentIds}
            resultIds={resultIds}
            dates={[
              { label: "Created", value: theory.createdAt },
              { label: "Updated", value: theory.updatedAt },
            ]}
          />
        }
      >
        {overlay ? (
          <Mdx source={overlay.content} />
        ) : (
          <p className="record-empty">No explanation has been written for this record yet.</p>
        )}

        <TechnicalRecord summary="The registered claim, mechanism and falsification criteria" meta={theory.theoryId}>
          <dl className="record-dl">
            <RecordField label="Claim">{theory.claim}</RecordField>
            <RecordField label="Mechanism">{theory.mechanism}</RecordField>
            <RecordField label="Falsification criteria">
              <RecordList items={theory.falsificationCriteria ?? []} ordered />
            </RecordField>
            <RecordField label="Information class">{theory.informationClass}</RecordField>
            <RecordField label="Lifecycle">{theory.lifecycle}</RecordField>
            <RecordField label="Assessment">{theory.assessment}</RecordField>
            <RecordField label="Evidence tier">{theory.evidenceTier}</RecordField>
            <RecordField label="Dependencies">
              <RecordList
                items={dependencies}
                render={(dependency) => (
                  <Link href={recordHref("theory", dependency)}>
                    <code>{dependency}</code>
                  </Link>
                )}
              />
            </RecordField>
            <RecordField label="Evidence references">
              <RecordList items={evidenceRefs} render={(ref) => <RepoRef path={ref} />} />
            </RecordField>
          </dl>
        </TechnicalRecord>

        <AgentContext summary="How to extend this record" className="record-agent">
          <p>
            To add a reader-facing explanation, write <code>web/content/research/{theory.theoryId}.mdx</code>; it renders
            above this record on the next request. The registered record itself is in the technical record above.
          </p>
          <p>
            Record file: <code>{recordFile}</code>, validated against <code>research/schemas/theory-v1.schema.json</code>.
          </p>
          <p>
            Registered by {theory.createdBy.platform} / {theory.createdBy.model}
            {theory.createdBy.agentId ? ` (${theory.createdBy.agentId})` : ""}.
          </p>
        </AgentContext>
      </ArticleLayout>
    </div>
  );
}
