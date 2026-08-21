import { MDXRemote } from "next-mdx-remote/rsc";
import { Board, BoardCompare, Callout, Disc, Stat } from "./Board";

const components = { Board, BoardCompare, Callout, Disc, Stat };

/** Renders an MDX document (already stripped of frontmatter) with the Drop7 visual components available. */
export function Mdx({ source }: { source: string }) {
  return (
    <div className="prose-drop7">
      <MDXRemote source={source} components={components} />
    </div>
  );
}
