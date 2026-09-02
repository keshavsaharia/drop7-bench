/**
 * <Figure name="score-vs-depth" caption="…" />
 *
 * Renders a research figure from its spec, web/content/figures/<name>.json,
 * through the visx chart kit (web/components/charts/ResearchChart.tsx), and
 * lists the spec's points with their source records under a collapsible
 * "Source data" block. Every point in the spec carries a `sourceRecord`, and
 * the spec is refused without it. This component computes no number: the
 * chart draws the values as written and the table lists them as written.
 *
 * Server component; the chart itself is a client component that receives the
 * parsed spec as a prop. Styled by the `.research-fig` and `.rchart` blocks
 * in globals.css.
 */
import Link from "next/link";
import { readRepoFile } from "@/lib/repo";
import { formatValue, validateFigureSpec, type FigureSpec } from "@/lib/charts/spec";
import { ResearchChart } from "./charts/ResearchChart";

const NAME = /^[a-z0-9-]+$/;

function loadSpec(name: string): { spec: FigureSpec | null; error: string | null; exists: boolean } {
  const raw = readRepoFile(`web/content/figures/${name}.json`);
  if (!raw) return { spec: null, error: null, exists: false };
  try {
    return { spec: validateFigureSpec(JSON.parse(raw), name), error: null, exists: true };
  } catch (error) {
    return { spec: null, error: error instanceof Error ? error.message : String(error), exists: true };
  }
}

/** Where a source record can be opened in the console, if anywhere. */
function sourceHref(source: string): string | null {
  if (source.startsWith("EX-")) return `/experiments/${source}`;
  if (source.startsWith("TH-")) return `/theories/${source}`;
  if (/^docs\/.+\.md$/.test(source)) return `/${source.replace(/\.md$/, "")}`;
  // RS-/RUN- records have no route of their own; results render under their experiment.
  return null;
}

function SourceRef({ source }: { source: string }) {
  const href = sourceHref(source);
  return href ? (
    <Link href={href} className="research-fig-source">
      {source}
    </Link>
  ) : (
    <code className="research-fig-source">{source}</code>
  );
}

/** Result records render on their experiment's page; resolve that link when the record exists. */
function resultHref(source: string): string | null {
  if (!source.startsWith("RS-")) return null;
  const raw = readRepoFile(`research/results/${source}.json`);
  if (!raw) return null;
  try {
    const record = JSON.parse(raw) as { experimentId?: string };
    return record.experimentId ? `/experiments/${record.experimentId}#results` : null;
  } catch {
    return null;
  }
}

export function Figure({ name, caption }: { name: string; caption?: string }) {
  if (!NAME.test(name)) {
    return (
      <figure className="research-fig research-fig-missing">
        <p>Invalid figure name.</p>
      </figure>
    );
  }
  const { spec, error, exists } = loadSpec(name);

  if (!spec) {
    return (
      <figure className="research-fig research-fig-missing">
        <p>
          {exists ? (
            <>
              Figure <code>{name}</code> has a spec that cannot be rendered: {error}
            </>
          ) : (
            <>
              Figure <code>{name}</code> has no spec at <code>web/content/figures/{name}.json</code>.
            </>
          )}
        </p>
        {caption && <figcaption>{caption}</figcaption>}
      </figure>
    );
  }

  const sources = [...new Set(spec.series.flatMap((s) => s.points.map((p) => p.sourceRecord)))];

  return (
    <figure className="research-fig">
      <ResearchChart spec={spec} id={`fig-${name}`} />
      {caption && <figcaption>{caption}</figcaption>}
      <details className="research-fig-data">
        <summary>Source data</summary>
        {spec.notes && <p className="research-fig-notes">{spec.notes}</p>}
        <div className="research-fig-scroll">
          <table>
            <thead>
              <tr>
                <th>Series</th>
                <th>{spec.x?.label ?? "x"}</th>
                <th className="num">{spec.y?.label ?? "y"}</th>
                <th className="num">Bounds</th>
                <th className="num">n</th>
                <th>Source</th>
              </tr>
            </thead>
            <tbody>
              {spec.series.flatMap((s) =>
                s.points.map((p, i) => {
                  const rs = resultHref(p.sourceRecord);
                  return (
                    <tr key={`${s.name}-${i}`}>
                      <td>{s.name}</td>
                      <td>
                        {String(p.x)}
                        {p.label && (
                          <span className="research-fig-label" title={p.label}>
                            {" "}
                            {p.label}
                          </span>
                        )}
                      </td>
                      <td className="num">{formatValue(p.y, spec.y?.unit)}</td>
                      <td className="num">
                        {p.lo !== undefined && p.hi !== undefined
                          ? `${formatValue(p.lo)} to ${formatValue(p.hi)}`
                          : p.lo !== undefined
                            ? `lower ${formatValue(p.lo)}`
                            : p.hi !== undefined
                              ? `upper ${formatValue(p.hi)}`
                              : "—"}
                      </td>
                      <td className="num">{p.n !== undefined ? p.n.toLocaleString("en-US") : "—"}</td>
                      <td>
                        {rs ? (
                          <Link href={rs} className="research-fig-source">
                            {p.sourceRecord}
                          </Link>
                        ) : (
                          <SourceRef source={p.sourceRecord} />
                        )}
                        {p.sourceField && (
                          <span className="research-fig-field" title={p.sourceField}>
                            {" "}
                            {p.sourceField}
                          </span>
                        )}
                      </td>
                    </tr>
                  );
                }),
              )}
            </tbody>
          </table>
        </div>
        <p className="research-fig-spec">
          Spec: <code>web/content/figures/{name}.json</code>
          {sources.length > 0 && <> · {sources.length} source record{sources.length === 1 ? "" : "s"}</>}
        </p>
      </details>
    </figure>
  );
}
