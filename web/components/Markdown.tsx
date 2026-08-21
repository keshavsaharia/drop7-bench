import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";

/** Renders a plain Markdown document (existing repo docs) with GFM tables. */
export function Markdown({ source }: { source: string }) {
  return (
    <div className="prose-drop7">
      <ReactMarkdown remarkPlugins={[remarkGfm]}>{source}</ReactMarkdown>
    </div>
  );
}
