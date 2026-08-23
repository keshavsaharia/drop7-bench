import type { ComponentProps, ReactElement } from "react";
import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";
import { Diagram } from "./Diagram";
import { Figure } from "./Figure";
import { RepoDocAnchor } from "./RepoDocAnchor";

/**
 * Figure and diagram fences in plain Markdown.
 *
 *   ```figure score-vs-depth
 *   ```
 *
 *   ```figure
 *   score-vs-depth
 *   caption: One sentence on what to see.
 *   ```
 *
 *   ```diagram
 *   search-pipeline
 *   caption: Optional.
 *   ```
 *
 * The fence language is `figure` (web/content/figures/<name>.svg, rendered by
 * <Figure/>) or `diagram` (web/content/figures/diagrams/<name>.svg, rendered by
 * <Diagram/>). The name comes from the fence's info string (one-line form) or
 * the first non-empty body line; an optional `caption: …` line follows. Other
 * renderers (GitHub) show the fence as a code block containing the name, which
 * is the intended degradation. Anything unparseable falls through to a normal
 * <pre> block so no document is ever broken by the hook.
 */
const EMBED_KINDS = new Set(["figure", "diagram"]);

interface EmbedFence {
  kind: "figure" | "diagram";
  name: string;
  caption?: string;
}

/** Extract the fence kind, name and caption from a hast `code` node, or null. */
export function parseEmbedFence(codeNode: unknown): EmbedFence | null {
  const node = codeNode as {
    tagName?: string;
    properties?: { className?: unknown };
    data?: { meta?: unknown };
    children?: { type: string; value?: string }[];
  } | null;
  if (!node || node.tagName !== "code") return null;
  const classes = Array.isArray(node.properties?.className) ? (node.properties!.className as unknown[]) : [];
  const lang = classes.map(String).find((c) => c.startsWith("language-"))?.slice("language-".length);
  if (!lang || !EMBED_KINDS.has(lang)) return null;
  const body = (node.children ?? [])
    .filter((c) => c.type === "text")
    .map((c) => c.value ?? "")
    .join("");
  const lines = body
    .split("\n")
    .map((l) => l.trim())
    .filter(Boolean);
  const meta = typeof node.data?.meta === "string" ? node.data.meta.trim() : "";
  let name = meta.split(/\s+/)[0] ?? "";
  let rest = lines;
  if (!name) {
    name = lines[0] ?? "";
    rest = lines.slice(1);
  }
  if (!/^[a-z0-9-]+$/.test(name)) return null;
  const captionLine = rest.find((l) => /^caption\s*:/i.test(l));
  const caption = captionLine ? captionLine.replace(/^caption\s*:\s*/i, "").trim() || undefined : undefined;
  return { kind: lang as EmbedFence["kind"], name, caption };
}

type PreProps = ComponentProps<"pre"> & { node?: unknown };

function Pre({ node, children, ...props }: PreProps) {
  const hast = node as { children?: unknown[] } | undefined;
  const fence = parseEmbedFence(hast?.children?.[0]);
  if (fence) {
    return fence.kind === "figure" ? (
      <Figure name={fence.name} caption={fence.caption} />
    ) : (
      <Diagram name={fence.name} caption={fence.caption} />
    );
  }
  return <pre {...props}>{children as ReactElement}</pre>;
}

/** Renders a plain Markdown document (existing repo docs) with GFM tables. */
export function Markdown({ source, fromPath }: { source: string; fromPath?: string }) {
  return (
    <div className="prose-drop7">
      <ReactMarkdown
        remarkPlugins={[remarkGfm]}
        components={{
          a: ({ href, children, ...props }) => (
            <RepoDocAnchor href={href} fromPath={fromPath} {...props}>
              {children}
            </RepoDocAnchor>
          ),
          pre: Pre,
        }}
      >
        {source}
      </ReactMarkdown>
    </div>
  );
}
