import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";
import { RepoDocAnchor } from "./RepoDocAnchor";

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
        }}
      >
        {source}
      </ReactMarkdown>
    </div>
  );
}
