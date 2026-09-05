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
  asRecord,
  boolOrNull,
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
import { approachRefsInText } from "@/lib/records";
import { loadRecordOverlay } from "@/lib/research";
import { getExperiments, getResults, getTheories, type ExperimentRecord } from "@/lib/repo";

export const dynamic = "force-dynamic";

type Params = Promise<{ id: string }>;

export async function generateMetadata({ params }: { params: Params }) {
  const { id } = await params;
  const experiment = getExperiments().find((e) => e.experimentId === id);
  return pageMetadata({
    title: experiment ? experiment.title : "Experiment",
    description: contentDescription(experiment?.hypothesis),
    path: `/experiments/${id}`,
  });
}

interface Amendment {
  timestamp: string;
  beforeControlledData: boolean | null;
  reason: string;
}

/** The amendments array as recorded; entries missing a field keep it absent. */
function amendmentsOf(value: unknown): Amendment[] {
  if (!Array.isArray(value)) return [];
  return value.flatMap((item) => {
    const record = asRecord(item);
    if (!record) return [];
    return [
      {
        timestamp: stringOrNull(record.timestamp) ?? "",
        beforeControlledData: boolOrNull(record.beforeControlledData),
        reason: stringOrNull(record.reason) ?? "",
      },
    ];
  });
}

/** An arm's entry point, linked to its approach page when the path names one. */
function EntryPoint({ entryPoint }: { entryPoint: string | null }) {
  if (!entryPoint) return <span className="record-none">none recorded</span>;
  const [ref] = approachRefsInText(entryPoint);
  if (!ref) return <code>{entryPoint}</code>;
  return (
    <Link href={`/approach/${ref.family}/${ref.slug}`}>
      <code>{entryPoint}</code>
    </Link>
  );
}

function ArmsTable({ experiment }: { experiment: ExperimentRecord }) {
  const arms = [
    { role: "Candidate", arm: experiment.candidate },
    { role: "Comparator", arm: experiment.comparator },
  ];
  return (
    <table className="data-table">
      <thead>
        <tr>
          <th scope="col">Arm</th>
          <th scope="col">Name</th>
          <th scope="col">Entry point</th>
          <th scope="col">Manifest</th>
        </tr>
      </thead>
      <tbody>
        {arms.map(({ role, arm }) => (
          <tr key={role}>
            <th scope="row">{role}</th>
            <td>{arm.name}</td>
            <td>
              <EntryPoint entryPoint={arm.entryPoint} />
            </td>
            <td>{arm.manifestRef ? <code>{arm.manifestRef}</code> : <span className="record-dash">–</span>}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}

const RESOURCE_ROWS: readonly { key: string; label: string }[] = [
  { key: "wallSeconds", label: "Wall seconds" },
  { key: "cpuThreads", label: "CPU threads" },
  { key: "maxHostBytes", label: "Max host bytes" },
  { key: "maxGpuBytes", label: "Max GPU bytes" },
  { key: "gpuDevices", label: "GPU devices" },
];

function ResourcesTable({ resources }: { resources: Record<string, unknown> }) {
  return (
    <table className="data-table">
      <tbody>
        {RESOURCE_ROWS.map(({ key, label }) => {
          const value = resources[key];
          const text = Array.isArray(value) ? (value.length > 0 ? value.map(String).join(", ") : "–") : scalarText(value);
          return (
            <tr key={key}>
              <th scope="row">{label}</th>
              <td className="num mono">{text}</td>
            </tr>
          );
        })}
      </tbody>
    </table>
  );
}

function AmendmentsTable({ amendments }: { amendments: Amendment[] }) {
  return (
    <table className="data-table">
      <thead>
        <tr>
          <th scope="col">Timestamp</th>
          <th scope="col">Before controlled data</th>
          <th scope="col">Reason</th>
        </tr>
      </thead>
      <tbody>
        {amendments.map((amendment, index) => (
          <tr key={index}>
            <td>
              <span className="mono">{amendment.timestamp}</span>
            </td>
            <td>
              {amendment.beforeControlledData === null ? "–" : amendment.beforeControlledData ? "yes" : "no"}
            </td>
            <td>{amendment.reason}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}

export default async function ExperimentPage({ params }: { params: Params }) {
  const { id } = await params;
  const experiment = getExperiments().find((e) => e.experimentId === id);
  if (!experiment) notFound();
  const overlay = loadRecordOverlay(id);
  const toc = overlay ? extractHeadings(overlay.content, { minDepth: 2, maxDepth: 3 }) : [];
  const results = getResults().filter((r) => r.experimentId === id);
  const theories = getTheories();
  const theoryIds = experiment.theoryIds ?? [];
  const recordFile = `research/${RECORD_DIR.experiment}/${experiment.$id}.json`;

  // Fields the schema records that the typed interface does not yet name.
  const raw = experiment as unknown as Record<string, unknown>;
  const rawData = asRecord(experiment.data) ?? {};
  const rawMetrics = asRecord(experiment.metrics) ?? {};
  const rawGate = asRecord(experiment.gate) ?? {};
  const resources = asRecord(raw.resources);
  const stopConditions = stringList(raw.stopConditions);
  const expectedArtifacts = stringList(raw.expectedArtifacts);
  const amendments = amendmentsOf(raw.amendments);
  const createdBy = asRecord(raw.createdBy);
  const protocolSha256 = stringOrNull(raw.protocolSha256);
  const datasetRefs = stringList(rawData.datasetRefs);
  const wholeOriginSplit = boolOrNull(rawData.wholeOriginSplit);
  const uncertaintyMethod = stringOrNull(rawMetrics.uncertaintyMethod);
  const fixedBeforeControlledData = boolOrNull(rawGate.fixedBeforeControlledData);

  return (
    <div>
      <PageHeader
        crumbs={[
          { href: "/research", label: "research" },
          { href: "/experiments", label: "experiments" },
        ]}
        title={experiment.title}
        lead={firstSentence(experiment.hypothesis)}
      >
        <Badge kind="status" value={experiment.lifecycle} />
        <Badge kind="tier" value={experiment.benchmarkTier} />
        <Badge value={experiment.classification} />
        <Badge value={experiment.data.role} />
        <Badge value={experiment.informationBoundary} />
        <Badge label={experiment.experimentId} className="record-id" />
      </PageHeader>

      <ArticleLayout
        toc={toc}
        aside={
          <RecordAside
            id={experiment.experimentId}
            kind="experiment"
            theoryIds={theoryIds}
            resultIds={results.map((r) => r.resultId)}
            dates={[
              { label: "Created", value: experiment.createdAt },
              { label: "Updated", value: experiment.updatedAt },
            ]}
          />
        }
      >
        {overlay ? (
          <Mdx source={overlay.content} />
        ) : (
          <p className="record-empty">No explanation has been written for this record yet.</p>
        )}

        <TechnicalRecord summary="The registered protocol" meta={experiment.experimentId}>
          <dl className="record-dl record-dl--two">
            <RecordField label="Hypothesis" wide>
              {experiment.hypothesis}
            </RecordField>
            <RecordField label="Arms" wide>
              <ArmsTable experiment={experiment} />
            </RecordField>
            <RecordField label="Classification">{experiment.classification}</RecordField>
            <RecordField label="Information boundary">{experiment.informationBoundary}</RecordField>
            <RecordField label="Benchmark tier">{experiment.benchmarkTier}</RecordField>
            <RecordField label="Lifecycle">{experiment.lifecycle}</RecordField>
            <RecordField label="Theories tested" wide>
              <RecordList
                items={theoryIds}
                render={(theoryId) => {
                  const theory = theories.find((t) => t.theoryId === theoryId);
                  return (
                    <Link href={recordHref("theory", theoryId)}>
                      {theory ? theory.title : <code>{theoryId}</code>}
                    </Link>
                  );
                }}
              />
            </RecordField>

            <RecordField label="Primary metric" wide>
              {experiment.metrics.primary}
            </RecordField>
            <RecordField label="Secondary metrics" wide>
              <RecordList items={experiment.metrics.secondary ?? []} />
            </RecordField>
            <RecordField label="Statistical unit">{experiment.metrics.statisticalUnit}</RecordField>
            {uncertaintyMethod && <RecordField label="Uncertainty method">{uncertaintyMethod}</RecordField>}

            <RecordField label="Data role">{experiment.data.role}</RecordField>
            <RecordField label="Seed leases">
              <RecordList items={experiment.data.seedLeaseRefs ?? []} render={(ref) => <code>{ref}</code>} />
            </RecordField>
            {datasetRefs.length > 0 && (
              <RecordField label="Dataset references">
                <RecordList items={datasetRefs} render={(ref) => <code>{ref}</code>} />
              </RecordField>
            )}
            {wholeOriginSplit !== null && (
              <RecordField label="Whole-origin split">{wholeOriginSplit ? "yes" : "no"}</RecordField>
            )}
            <RecordField label="Reuse disclosure" wide>
              {experiment.data.reuseDisclosure}
            </RecordField>

            <RecordField label="Pass criteria" wide>
              <RecordList items={experiment.gate.passCriteria ?? []} ordered />
            </RecordField>
            <RecordField label="On pass">{experiment.gate.passAction}</RecordField>
            <RecordField label="On fail">{experiment.gate.failureAction}</RecordField>
            {fixedBeforeControlledData !== null && (
              <RecordField label="Gate fixed before controlled data">
                {fixedBeforeControlledData ? "yes" : "no"}
              </RecordField>
            )}

            <RecordField label="Resources" wide>
              {resources ? <ResourcesTable resources={resources} /> : <span className="record-none">none recorded</span>}
            </RecordField>
            <RecordField label="Stop conditions" wide>
              <RecordList items={stopConditions} ordered />
            </RecordField>
            <RecordField label="Expected artifacts" wide>
              <RecordList items={expectedArtifacts} render={(artifact) => <RepoRef path={artifact} />} />
            </RecordField>
            <RecordField label="Amendments" wide>
              {amendments.length > 0 ? (
                <AmendmentsTable amendments={amendments} />
              ) : (
                <span className="record-none">none recorded</span>
              )}
            </RecordField>
          </dl>
        </TechnicalRecord>

        {results.length > 0 && (
          <TechnicalRecord
            summary="Results recorded against this protocol"
            meta={`${results.length} ${results.length === 1 ? "record" : "records"}`}
          >
            {results.map((result) => (
              <div key={result.resultId}>
                <ResultNarrative result={result} />
                <p>
                  <Link href={recordHref("result", result.resultId)}>Open the result record</Link>
                </p>
              </div>
            ))}
          </TechnicalRecord>
        )}

        <AgentContext summary="How to extend this record" className="record-agent">
          <p>
            To add a reader-facing explanation, write <code>web/content/research/{experiment.experimentId}.mdx</code>;
            it renders above this record on the next request. The registered protocol itself is in the technical record
            above.
          </p>
          <p>
            Record file: <code>{recordFile}</code>, validated against{" "}
            <code>research/schemas/experiment-v1.schema.json</code>.
            {protocolSha256 && (
              <>
                {" "}
                Protocol hash: <code>{protocolSha256}</code>.
              </>
            )}
          </p>
          {createdBy && (
            <p>
              Registered by {stringOrNull(createdBy.platform) ?? "unknown"} / {stringOrNull(createdBy.model) ?? "unknown"}
              {stringOrNull(createdBy.agentId) ? ` (${stringOrNull(createdBy.agentId)})` : ""}.
            </p>
          )}
        </AgentContext>
      </ArticleLayout>
    </div>
  );
}
