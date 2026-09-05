import type { ComponentProps } from "react";
import { MDXRemote } from "next-mdx-remote/rsc";
import { Board, BoardCompare, Callout, Disc, Stat } from "./Board";
import { Drop7Board } from "./Drop7Board";
import { Drop7Game } from "./Drop7Game";
import { Drop7Intro } from "./Drop7Intro";
import { ChoiceLesson } from "./lessons/ChoiceLesson";
import { Diagram } from "./Diagram";
import { CodeSnippet } from "./CodeSnippet";
import { Figure } from "./Figure";
import { GameTreeFigure } from "./GameTree";
import * as Engine from "./Engine";
import * as Rules from "./Rules";
import * as Concepts from "./Concepts";
import * as ConceptsB from "./ConceptsB";
import * as ConceptsC from "./ConceptsC";
import { EvidenceLabel, ExperimentSummary, ResultSummary, TheorySummary } from "./Research";
import { AgentContext, Reveal, TechnicalDetails, TechnicalRecord } from "./Reveal";
import { ArmTable, DeadEnd, Direction, Finding, LogQuote, Timeline } from "./ResearchLog";
import { RepoDocAnchor } from "./RepoDocAnchor";
import * as Evolution from "./Evolution";
import * as Primers from "./primers";
import {
  RustExplosionBitplanesFigure,
  RustPackedBoardFigure,
  RustPextGravityFigure,
} from "./RustBitboardFigures";
import { BitboardColumns, GravityWave, ParityReplay, TranspositionTable } from "./Engines";
import remarkGfm from "remark-gfm";
import rehypeSlug from "rehype-slug";

const components = {
  Board,
  BoardCompare,
  Callout,
  CodeSnippet,
  Diagram,
  Disc,
  Drop7Board,
  Drop7Game,
  Drop7Intro,
  ChoiceLesson,
  Figure,
  GameTreeFigure,
  Stat,
  ...Engine,
  ...Rules,
  ...Concepts,
  ...ConceptsB,
  ...ConceptsC,
  ...Evolution,
  ...Primers,
  RustExplosionBitplanesFigure,
  RustPackedBoardFigure,
  RustPextGravityFigure,
  BitboardColumns,
  GravityWave,
  ParityReplay,
  TranspositionTable,
  EvidenceLabel,
  ExperimentSummary,
  ResultSummary,
  TheorySummary,
  Reveal,
  AgentContext,
  TechnicalRecord,
  TechnicalDetails,
  ArmTable,
  DeadEnd,
  Direction,
  Finding,
  LogQuote,
  Timeline,
};

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
          // rehype-slug gives every heading the id that lib/headings.ts
          // predicts, so a page's table of contents links resolve.
          mdxOptions: { remarkPlugins: [remarkGfm], rehypePlugins: [rehypeSlug] },
        }}
      />
    </div>
  );
}
