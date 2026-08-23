/**
 * <Diagram name="search-pipeline" caption="…" />
 *
 * Inlines a hand-authored or script-generated diagram from
 * web/content/figures/diagrams/<name>.svg. If a sibling <name>.source.md
 * exists it is rendered (plain Markdown with GFM tables) under a collapsible
 * "Source" block so a reader can see where the diagram's content came from.
 * Like Figure, this component draws nothing of its own: it shows the file as
 * written, and a missing SVG renders a visible "not generated" notice.
 *
 * Server component. Shares the `.research-fig` styles in globals.css.
 */
import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";
import { readRepoFile } from "@/lib/repo";

export const DIAGRAMS_DIR = "web/content/figures/diagrams";
const NAME = /^[a-z0-9-]+$/;

export function Diagram({ name, caption }: { name: string; caption?: string }) {
  if (!NAME.test(name)) {
    return (
      <figure className="research-fig research-fig-missing">
        <p>Invalid diagram name.</p>
      </figure>
    );
  }
  const svg = readRepoFile(`${DIAGRAMS_DIR}/${name}.svg`);
  const source = readRepoFile(`${DIAGRAMS_DIR}/${name}.source.md`);

  if (!svg) {
    return (
      <figure className="research-fig research-fig-missing">
        <p>
          Diagram <code>{name}</code> has not been generated. No file was found at{" "}
          <code>
            {DIAGRAMS_DIR}/{name}.svg
          </code>
          .
        </p>
        {caption && <figcaption>{caption}</figcaption>}
      </figure>
    );
  }

  return (
    <figure className="research-fig research-fig-diagram">
      <div className="research-fig-svg-wrap" dangerouslySetInnerHTML={{ __html: svg }} />
      {caption && <figcaption>{caption}</figcaption>}
      {source && (
        <details className="research-fig-data">
          <summary>Source</summary>
          <div className="research-fig-source-md">
            <ReactMarkdown remarkPlugins={[remarkGfm]}>{source}</ReactMarkdown>
          </div>
          <p className="research-fig-spec">
            Source: <code>
              {DIAGRAMS_DIR}/{name}.source.md
            </code>
          </p>
        </details>
      )}
    </figure>
  );
}
