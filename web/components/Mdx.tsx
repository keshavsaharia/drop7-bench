import { MDXRemote } from "next-mdx-remote/rsc";
import { Board, BoardCompare, Callout, Disc, Stat } from "./Board";
import * as Engine from "./Engine";
import * as Rules from "./Rules";
import * as Concepts from "./Concepts";
import * as ConceptsB from "./ConceptsB";
import * as ConceptsC from "./ConceptsC";
import { EvidenceLabel, ExperimentSummary, ResultSummary, TechnicalDetails, TheorySummary } from "./Research";
import remarkGfm from "remark-gfm";

const components = { Board, BoardCompare, Callout, Disc, Stat, ...Engine, ...Rules, ...Concepts, ...ConceptsB, ...ConceptsC, EvidenceLabel, ExperimentSummary, ResultSummary, TechnicalDetails, TheorySummary };

/** Renders an MDX document (already stripped of frontmatter) with the Drop7 visual components available. */
export function Mdx({ source }: { source: string }) {
  return (
    <div className="prose-drop7">
      <MDXRemote
        source={source}
        components={components}
        options={{
          // next-mdx-remote 6 defaults blockJS to true, which strips every JSX
          // expression ({3}, arrays, objects) from the source as an injection
          // guard for untrusted MDX. Our MDX is authored in this repository, so
          // expression props must survive; blockDangerousJS stays on.
          blockJS: false,
          mdxOptions: { remarkPlugins: [remarkGfm] },
        }}
      />
    </div>
  );
}
