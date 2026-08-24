/**
 * <Figure name="score-vs-depth" caption="…" />
 *
 * Inlines a generated research figure from web/content/figures/<name>.svg and
 * lists the spec's points with their source records under a collapsible
 * "Source data" block. The SVG is produced by web/scripts/figures/generate.mjs
 * from web/content/figures/<name>.json; every point in that spec carries a
 * `sourceRecord`, and the generator refuses to draw one without it. This
 * component draws nothing of its own and computes no number: it shows the
 * file as written and the spec as written.
 *
 * Server component. Styled by the `.research-fig` block in globals.css.
 */
import Link from "next/link";
import { readRepoFile } from "@/lib/repo";

interface FigurePoint {
  x: number | string;
  y: number;
  lo?: number;
  hi?: number;
  n?: number;
  label?: string;
  sourceRecord: string;
  sourceField?: string;
}

interface FigureSeries {
  name: string;
  points: FigurePoint[];
}

interface FigureSpec {
  title: string;
  kind: string;
  x?: { label: string; unit?: string };
  y?: { label: string; unit?: string };
  notes?: string;
  series: FigureSeries[];
}

const NAME = /^[a-z0-9-]+$/;

function loadSpec(name: string): FigureSpec | null {
  const raw = readRepoFile(`web/content/figures/${name}.json`);
  if (!raw) return null;
  try {
    return JSON.parse(raw) as FigureSpec;
  } catch {
    return null;
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

function formatValue(value: number, unit?: string) {
  const text = Math.abs(value) >= 1000 ? value.toLocaleString("en-US", { maximumFractionDigits: 0 }) : value.toLocaleString("en-US", { maximumFractionDigits: 2 });
  return unit ? `${text} ${unit}` : text;
}

export function Figure({ name, caption }: { name: string; caption?: string }) {
  if (!NAME.test(name)) {
    return (
      <figure className="research-fig research-fig-missing">
        <p>Invalid figure name.</p>
      </figure>
    );
  }
  const svg = readRepoFile(`web/content/figures/${name}.svg`);
  const spec = loadSpec(name);

  if (!svg) {
    return (
      <figure className="research-fig research-fig-missing">
        <p>
          Figure <code>{name}</code> has not been generated.
          {spec ? (
            <>
              {" "}
              Its spec exists; run <code>node web/scripts/figures/generate-all.mjs</code>.
            </>
          ) : (
            <>
              {" "}
              No spec was found at <code>web/content/figures/{name}.json</code>.
            </>
          )}
        </p>
        {caption && <figcaption>{caption}</figcaption>}
      </figure>
    );
  }

  const sources = spec ? [...new Set(spec.series.flatMap((s) => s.points.map((p) => p.sourceRecord)))] : [];

  return (
    <figure className="research-fig">
      <div className="research-fig-svg-wrap" dangerouslySetInnerHTML={{ __html: svg }} />
      {caption && <figcaption>{caption}</figcaption>}
      {spec && (
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
      )}
    </figure>
  );
}
