import type { ComponentProps } from "react";
import { MDXRemote } from "next-mdx-remote/rsc";
import { Board, BoardCompare, Callout, Disc, Stat } from "./Board";
import { Drop7Board } from "./Drop7Board";
import { Drop7Game } from "./Drop7Game";
import { Drop7Intro } from "./Drop7Intro";
import { GameTreeFigure } from "./GameTree";
import * as Engine from "./Engine";
import * as Rules from "./Rules";
import * as Concepts from "./Concepts";
import * as ConceptsB from "./ConceptsB";
import * as ConceptsC from "./ConceptsC";
import { EvidenceLabel, ExperimentSummary, ResultSummary, TechnicalDetails, TheorySummary } from "./Research";
import { ArmTable, DeadEnd, Direction, Finding, LogQuote, Timeline } from "./ResearchLog";
import { RepoDocAnchor } from "./RepoDocAnchor";
import remarkGfm from "remark-gfm";

const components = { Board, BoardCompare, Callout, Disc, Drop7Board, Drop7Game, Drop7Intro, GameTreeFigure, Stat, ...Engine, ...Rules, ...Concepts, ...ConceptsB, ...ConceptsC, EvidenceLabel, ExperimentSummary, ResultSummary, TechnicalDetails, TheorySummary, ArmTable, DeadEnd, Direction, Finding, LogQuote, Timeline };

/** Renders an MDX document (already stripped of frontmatter) with the Drop7 visual components available. */
export function Mdx({ source, fromPath }: { source: string; fromPath?: string }) {
  return (
    <div className="prose-drop7">
      <MDXRemote
        source={source}
        components={{
          ...components,
          a: ({ href, children, ...props }: ComponentProps<"a">) => (
            <RepoDocAnchor href={href} fromPath={fromPath} {...props}>
              {children}
            </RepoDocAnchor>
          ),
        }}
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
