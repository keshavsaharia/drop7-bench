import "./engines.css";
import Link from "next/link";
import { Badge } from "@/components/Badge";
import { Card } from "@/components/Card";
import { EngineCard } from "@/components/EngineCard";
import { ParityReplay, SourceRef } from "@/components/Engines";
import { Figure } from "@/components/Figure";
import { PageHeader } from "@/components/PageHeader";
import { Reveal } from "@/components/Reveal";
import {
  AUDIT_01,
  COMPACT_SLUGS,
  COMPARISON_ROWS,
  COMPARISON_SLUGS,
  ENGINE_GATES,
  ENGINES,
  FEATURED_SLUGS,
  MACHINE_CAVEAT,
  NOT_APPLICABLE,
  OPEN_QUESTIONS,
  RS_RUST,
  getEngine,
  rowSources,
  shortSource,
  type ComparisonCell,
} from "@/lib/engines";
import { listApproachesByKind } from "@/lib/repo";

export const dynamic = "force-dynamic";

export const metadata = {
  title: "Engines",
  description:
    "Several implementations of the Drop7 rules, each proven to play the same game as the C++ reference, with every recorded number and its source.",
};

function CompareCell({ cell, multi }: { cell: ComparisonCell; multi: boolean }) {
  if (cell.value === null) return <span className="muted">not recorded</span>;
  if (cell.value === NOT_APPLICABLE) return <span className="muted">{cell.value}</span>;
  const sources = cell.source === null ? [] : Array.isArray(cell.source) ? cell.source : [cell.source];
  return (
    <>
      {cell.value}
      {multi && sources.length > 0 && (
        <span className="engine-compare-src">{sources.map(shortSource).join(", ")}</span>
      )}
    </>
  );
}

export default function EnginesPage() {
  const featured = FEATURED_SLUGS.map((slug) => getEngine(slug)).filter((entry) => entry !== null);
  const compact = COMPACT_SLUGS.map((slug) => getEngine(slug)).filter((entry) => entry !== null);
  const efficiency = listApproachesByKind("engine");

  return (
    <div className="engines-index">
      <PageHeader
        crumbs={[{ href: "/research", label: "research" }]}
        title="Engines"
        lead="Several implementations of the same rules, each proven to play the same game as the C++ reference."
      />

      <div className="prose-drop7">
        <p>
          An engine here is the code that plays the game: it places a disc, finds the discs that pop, resolves the
          chain, applies gravity, raises the gray row every fifth move and deals the next disc (the rules are on{" "}
          <Link href="/learn/rules">the rules page</Link>). Every strategy on this site runs on one of them, and every
          board drawn on this site comes out of one. There are several because the work asks different things of the
          same rules: one is written to be read and tested, one plays hundreds of thousands of games, one holds a
          fixed hidden board so a position can be solved exactly, and one packs the board into seven integers. The one
          rule for admitting a new engine is that it passes its own gates first and then replays whole games against
          the C++ reference, and any difference at all is a failure.
        </p>
        <p>
          Two facts frame every number on this page. Every timing was taken on one shared workstation under
          background load, so the trustworthy quantity is the ratio between arms measured back to back; an absolute
          nanosecond figure depends on the load at the time. And the run artifacts behind the benchmarks are kept
          outside version control, so what survives is the result record that summarises each run, and every figure
          here cites that record.
        </p>
      </div>

      <section className="engine-section" aria-labelledby="engines-list">
        <span className="label" id="engines-list">
          The engines
        </span>
        <ul className="engine-grid">
          {featured.map((entry) => (
            <li key={entry.slug}>
              <EngineCard entry={entry} />
            </li>
          ))}
        </ul>
        <ul className="engine-grid engine-grid--compact">
          {compact.map((entry) => (
            <li key={entry.slug}>
              <EngineCard entry={entry} compact />
            </li>
          ))}
        </ul>
      </section>

      <section className="engine-section" aria-labelledby="side-by-side">
        <h2 id="side-by-side">Side by side</h2>
        <p className="engine-note">
          One table for everything the records say about the engines. Rows about the code (board bytes, latent mode)
          hold facts; rows about speed hold values quoted from the result record or finding named in the last column.
        </p>
        <div className="engine-compare">
          <table className="data-table" aria-describedby="engine-compare-caption">
            <thead>
              <tr>
                <th scope="col">Quantity</th>
                {COMPARISON_SLUGS.map((slug) => (
                  <th key={slug} scope="col">
                    {getEngine(slug)?.shortTitle ?? slug}
                  </th>
                ))}
                <th scope="col">Source</th>
              </tr>
            </thead>
            <tbody>
              {COMPARISON_ROWS.map((row) => {
                const sources = rowSources(row);
                const multi = sources.length > 1;
                return (
                  <tr key={row.quantity}>
                    <td className="engine-compare-quantity">{row.quantity}</td>
                    {COMPARISON_SLUGS.map((slug) => (
                      <td key={slug}>
                        <CompareCell cell={row.cells[slug]} multi={multi} />
                      </td>
                    ))}
                    <td>
                      <div className="engine-compare-sources">
                        {sources.length === 0 ? (
                          <span className="muted">no record</span>
                        ) : (
                          sources.map((source) => <SourceRef key={source} source={source} />)
                        )}
                      </div>
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
        <p className="engine-caveat" id="engine-compare-caption">
          Every cell is a recorded value or a fact about the code, and a cell reading &ldquo;not recorded&rdquo; means
          the measurement was not retained. The record behind the speed columns says of itself: &ldquo;
          {MACHINE_CAVEAT}&rdquo; (<SourceRef source={RS_RUST} />).
        </p>
        <p className="engine-caveat">
          No engine number has ever been taken on an idle host, and nothing in this table qualifies as a clean
          performance baseline under <Link href="/docs/benchmarks">the benchmark rules</Link>. A clean measurement
          would need exclusive resources, three repeats and a scaling preflight.
        </p>
        <Figure
          name="engine-speedup"
          caption="Fast-engine speedups over the frozen C++ reference, from the headline table of finding-13 and the one-entry leaf-memo record. Every bar is a ratio measured back to back on the shared machine; the memo bars are flagged indicative by their own record."
        />
      </section>

      <section className="engine-section" aria-labelledby="proven">
        <h2 id="proven">Cross-engine parity</h2>
        <div className="prose-drop7">
          <p>
            Two implementations mean two chances to be wrong. If the fast engine dropped a chain wave one move
            earlier, or awarded the rise bonus in a different order, every score from the C++ experiments would mean
            something slightly different from every score from the TypeScript ones, and nobody would notice by
            comparing means. So the engines are compared move by move instead.
          </p>
          <p>
            Pick a seed. The seed fixes the sequence of discs the game will deal, in both engines, through the same
            arithmetic. Play a complete game in each engine, choosing columns from a shared random stream rather than
            from a policy, so both engines make exactly the same moves and any difference in the result is a
            difference in the rules. After every move each engine writes one record: the points that move scored, the
            running score, the level, the moves left before the next rise, whether the game ended, whether the board
            was cleared, whether the level advanced, every chain wave, and the full 49-cell board as a string. Compare
            the two streams line by line, byte for byte. Any difference at all is a failure.
          </p>
          <p>
            The same method sits behind every &ldquo;bit-identical&rdquo; claim on this site: the fast engine&apos;s
            leaf, search and trajectory gates, the scenario engine&apos;s parity run, and the Rust engine&apos;s three
            gate binaries all replay the C++ engine and stop at the first differing byte. The method has a limit the
            fidelity audit states plainly: two engines can agree exactly on the wrong rules, and the 70,000-point
            board-clear branches have zero cross-engine coverage in the parity sweep (<SourceRef source={AUDIT_01} />).
          </p>
        </div>
        <ParityReplay />
      </section>

      <section className="engine-section" aria-labelledby="efficiency">
        <h2 id="efficiency">Efficiency work</h2>
        <p className="engine-note">
          The approach directories that build, speed up or check an engine rather than play the game. Each keeps its
          own page with its records and gates.
        </p>
        {efficiency.length === 0 ? (
          <p className="engine-note">No engine approach directories are present in this checkout.</p>
        ) : (
          <ul className="engine-grid engine-grid--small">
            {efficiency.map((entry) => (
              <li key={`${entry.family}/${entry.slug}`}>
                <Card
                  href={`/approaches/${entry.family}/${entry.slug}`}
                  eyebrow={entry.family}
                  title={entry.title}
                  summary={entry.summary || undefined}
                  heading="h3"
                >
                  {entry.status && (
                    <div className="card-labels">
                      <Badge kind="status" value={entry.status} />
                    </div>
                  )}
                </Card>
              </li>
            ))}
          </ul>
        )}
      </section>

      <Reveal variant="agent" summary="Engine source entry points, gates and open questions" id="agent-context">
        <h3>Source entry points</h3>
        <ul>
          {ENGINES.map((entry) => (
            <li key={entry.slug}>
              <Link href={`/engines/${entry.slug}`}>{entry.title}</Link>: <SourceRef source={entry.path} />
              {entry.readme && (
                <>
                  {" "}
                  (documented in <code>{entry.readme}</code>)
                </>
              )}
            </li>
          ))}
        </ul>
        <h3>Gates</h3>
        <p>
          The tests and gate binaries that must pass before an engine&apos;s output counts, grouped by the file or
          command that runs them.
        </p>
        {ENGINE_GATES.map((group) => (
          <div key={group.file}>
            <p>
              <code>{group.file}</code>
            </p>
            <ul>
              {group.tests.map((test) => (
                <li key={test}>{test}</li>
              ))}
            </ul>
          </div>
        ))}
        <h3>Open questions</h3>
        <ol>
          {OPEN_QUESTIONS.map((question) => (
            <li key={question}>{question}</li>
          ))}
        </ol>
      </Reveal>
    </div>
  );
}
