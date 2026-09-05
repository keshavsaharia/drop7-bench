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
  RecordList,
  RepoRef,
  asRecord,
  recordHref,
  scalarText,
  stringList,
  stringOrNull,
} from "@/components/RecordAside";
import { firstSentence } from "@/components/RecordTable";
import { ResultNarrative } from "@/components/Research";
import { AgentContext, TechnicalRecord } from "@/components/Reveal";
import { extractHeadings } from "@/lib/headings";
import { contentDescription, pageMetadata } from "@/lib/metadata";
import { loadRecordOverlay } from "@/lib/research";
import { getExperiments, getResults } from "@/lib/repo";

export const dynamic = "force-dynamic";

type Params = Promise<{ id: string }>;

export async function generateMetadata({ params }: { params: Params }) {
  const { id } = await params;
  const result = getResults().find((r) => r.resultId === id);
  const path = `/results/${id}`;
  if (!result) return pageMetadata({ title: "Result", path });
  const experiment = getExperiments().find((e) => e.experimentId === result.experimentId);
  return pageMetadata({
    title: `Result of ${experiment ? experiment.title : result.experimentId}`,
    description: contentDescription(result.summary),
    path,
  });
}

export default async function ResultPage({ params }: { params: Params }) {
  const { id } = await params;
  const result = getResults().find((r) => r.resultId === id);
  if (!result) notFound();
  const experiment = getExperiments().find((e) => e.experimentId === result.experimentId);
  const overlay = loadRecordOverlay(id);
  const toc = overlay ? extractHeadings(overlay.content, { minDepth: 2, maxDepth: 3 }) : [];
  const recordFile = `research/${RECORD_DIR.result}/${result.$id}.json`;

  // Provenance fields the schema records that the typed interface does not yet name.
  const raw = result as unknown as Record<string, unknown>;
  const perGameArtifact = asRecord(raw.perGameArtifact);
  const artifactManifestRef = stringOrNull(raw.artifactManifestRef);
  const machineProfileRefs = stringList(raw.machineProfileRefs);

  return (
    <div>
      <PageHeader
        crumbs={[
          { href: "/research", label: "research" },
          { href: "/results", label: "results" },
        ]}
        title={
          <>
            <span className="label record-eyebrow">Result</span>
            {experiment ? experiment.title : result.experimentId}
          </>
        }
        lead={firstSentence(result.summary)}
      >
        <Badge kind="validity" value={result.runValidity} />
        <Badge kind="outcome" value={result.scientificOutcome} />
        <Badge kind="outcome" value={result.assessment} />
        <Badge kind="tier" value={result.evidenceTier} />
        <Badge label={result.resultId} className="record-id" />
      </PageHeader>

      <ArticleLayout
        toc={toc}
        aside={
          <RecordAside
            id={result.resultId}
            kind="result"
            theoryIds={result.theoryIds ?? []}
            experimentIds={result.experimentId ? [result.experimentId] : []}
            dates={[{ label: "Recorded", value: result.recordedAt }]}
          />
        }
      >
        {overlay ? (
          <Mdx source={overlay.content} />
        ) : (
          <p className="record-empty">No explanation has been written for this record yet.</p>
        )}

        <TechnicalRecord summary="Metrics, gate checks and limitations" meta={result.resultId}>
          <ResultNarrative result={result} />
          {experiment && (
            <p>
              Recorded against{" "}
              <Link href={recordHref("experiment", experiment.experimentId)}>{experiment.title}</Link>.
            </p>
          )}
        </TechnicalRecord>

        <AgentContext summary="How to extend this record" className="record-agent">
          <p>
            To add a reader-facing explanation, write <code>web/content/research/{result.resultId}.mdx</code>; it renders
            above this record on the next request. The recorded metrics, gate checks and limitations are in the
            technical record above.
          </p>
          <p>
            Record file: <code>{recordFile}</code>, validated against <code>research/schemas/result-v1.schema.json</code>.
          </p>
          <dl className="record-dl">
            <div>
              <dt className="label">Run ids</dt>
              <dd>
                <RecordList items={result.runIds ?? []} render={(runId) => <code>{runId}</code>} />
              </dd>
            </div>
            <div>
              <dt className="label">Contribution ids</dt>
              <dd>
                <RecordList items={result.contributionIds ?? []} render={(cid) => <code>{cid}</code>} />
              </dd>
            </div>
            {perGameArtifact && (
              <div>
                <dt className="label">Per-game artifact</dt>
                <dd>
                  <code>{scalarText(perGameArtifact.path)}</code>
                  {typeof perGameArtifact.sha256 === "string" && (
                    <>
                      {" "}
                      (sha256 <code>{perGameArtifact.sha256}</code>
                      {perGameArtifact.recordCount !== undefined
                        ? `, ${scalarText(perGameArtifact.recordCount)} records)`
                        : ")"}
                    </>
                  )}
                </dd>
              </div>
            )}
            {artifactManifestRef && (
              <div>
                <dt className="label">Artifact manifest</dt>
                <dd>
                  <code>{artifactManifestRef}</code>
                </dd>
              </div>
            )}
            {machineProfileRefs.length > 0 && (
              <div>
                <dt className="label">Machine profiles</dt>
                <dd>
                  <RecordList items={machineProfileRefs} render={(ref) => <RepoRef path={ref} />} />
                </dd>
              </div>
            )}
          </dl>
        </AgentContext>
      </ArticleLayout>
    </div>
  );
}
