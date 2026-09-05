import "../engines.css";
import Link from "next/link";
import { notFound } from "next/navigation";
import matter from "gray-matter";
import type { ReactNode } from "react";
import { ArticleLayout } from "@/components/ArticleLayout";
import { AsideRecords } from "@/components/AsideRecords";
import { ApproachBadges } from "@/components/ApproachBadges";
import { Badge } from "@/components/Badge";
import { Button } from "@/components/Button";
import { ENGINE_BODY_TOC, EngineBody, hasWrittenEngineBody } from "@/components/EngineBodies";
import { latentModeText } from "@/components/EngineCard";
import { sourceHref } from "@/components/Engines";
import { Mdx } from "@/components/Mdx";
import { PageHeader } from "@/components/PageHeader";
import { RustEngineHero } from "@/components/RustBitboardFigures";
import { TechniqueArt } from "@/components/technique-art/TechniqueArt";
import {
  ENGINES,
  getEngine,
  readmeApproach,
  type EngineEntry,
} from "@/lib/engines";
import { extractHeadings } from "@/lib/headings";
import { pageMetadata } from "@/lib/metadata";
import { recordsForApproach, type ApproachRecords } from "@/lib/records";
import { readRepoFile } from "@/lib/repo";
import type { TocItem } from "@/components/Toc";

export const dynamic = "force-dynamic";

export async function generateMetadata({ params }: { params: Promise<{ slug: string }> }) {
  const { slug } = await params;
  const entry = getEngine(slug);
  const path = `/engine/${slug}`;
  if (!entry) return pageMetadata({ title: "Engine", path });
  return pageMetadata({ title: entry.title, description: entry.role, path });
}

/* ---- aside ---- */

function approachHref(path: string): string | null {
  const match = /^approaches\/([a-z0-9-]+)\/([a-z0-9-]+)\//.exec(path);
  return match ? `/approach/${match[1]}/${match[2]}` : null;
}

function ApproachBlock({ entry }: { entry: EngineEntry }) {
  const approachLink = entry.readme ? approachHref(entry.readme) ?? "/approach" : null;
  if (!approachLink) return null;
  return (
    <div className="aside-block">
      <Button variant="secondary" href={approachLink} className="engine-approach-link">
        <span>Approach page</span>
        <span aria-hidden="true">→</span>
      </Button>
    </div>
  );
}

function SourceBlock({ entry }: { entry: EngineEntry }) {
  const sourceLink = sourceHref(entry.path);
  const sourceName = entry.path.split("/").at(-1) ?? entry.path;
  return (
    <div className="aside-block">
      <span className="label">Source</span>
      <ul className="engine-aside-list">
        <li>
          {sourceLink ? (
            <Link href={sourceLink} title={entry.path} aria-label={`${sourceName}, source file at ${entry.path}`}>
              <code>{sourceName}</code>
            </Link>
          ) : (
            <code title={entry.path}>{sourceName}</code>
          )}
        </li>
      </ul>
    </div>
  );
}


function UsedByBlock({ entry }: { entry: EngineEntry }) {
  return (
    <div className="aside-block">
      <span className="label">Used by</span>
      {entry.usedBy.length === 0 ? (
        <p className="engine-aside-empty">Nothing consumes it yet.</p>
      ) : (
        <ul className="engine-aside-list">
          {entry.usedBy.map((link) => (
            <li key={link.label}>{link.href ? <Link href={link.href}>{link.label}</Link> : link.label}</li>
          ))}
        </ul>
      )}
    </div>
  );
}

/* ---- page ---- */

export default async function EnginePage({ params }: { params: Promise<{ slug: string }> }) {
  const { slug } = await params;
  const entry = getEngine(slug);
  if (!entry) notFound();

  const index = ENGINES.findIndex((candidate) => candidate.slug === entry.slug);
  const previous = index > 0 ? ENGINES[index - 1] : null;
  const next = index < ENGINES.length - 1 ? ENGINES[index + 1] : null;

  let body: ReactNode;
  let toc: readonly TocItem[] = ENGINE_BODY_TOC;
  let lead: string = entry.role;
  let frontmatter: Record<string, unknown> = {};
  let records: ApproachRecords | null = null;
  let readmeContent: string | null = null;

  if (entry.readme) {
    const approach = readmeApproach(entry.readme);
    if (approach) records = recordsForApproach(approach.family, approach.slug);
    const raw = readRepoFile(entry.readme);
    if (raw) {
      const parsed = matter(raw);
      frontmatter = parsed.data as Record<string, unknown>;
      if (typeof frontmatter.summary === "string") lead = frontmatter.summary;
      readmeContent = parsed.content;
    }
  }

  if (hasWrittenEngineBody(entry.slug)) {
    body = (
      <div className="prose-drop7">
        <EngineBody slug={entry.slug} />
      </div>
    );
  } else if (entry.readme) {
    if (readmeContent !== null) {
      toc = extractHeadings(readmeContent, { minDepth: 2, maxDepth: 2 });
      body = <Mdx source={readmeContent} fromPath={entry.readme} />;
    } else {
      toc = [];
      body = (
        <div className="prose-drop7">
          <p>
            This engine is documented in <code>{entry.readme}</code>, which is absent from this checkout.
          </p>
        </div>
      );
    }
  } else {
    toc = [];
    body = <div className="prose-drop7"><p>This engine has no page yet.</p></div>;
  }

  const status = typeof frontmatter.status === "string" ? frontmatter.status : null;
  const evidence = typeof frontmatter.evidence === "string" ? frontmatter.evidence : null;
  const reads = typeof frontmatter.reads === "string" ? frontmatter.reads : null;

  const aside = (
    <>
      <ApproachBlock entry={entry} />
      <SourceBlock entry={entry} />
      {records && <AsideRecords records={records} />}
      <UsedByBlock entry={entry} />
    </>
  );

  return (
    <div className="engine-page">
      <PageHeader crumbs={[{ href: "/engine", label: "engines" }]} title={entry.title} lead={lead}>
        <Badge label={entry.language} />
        <Badge label={`latent mode: ${latentModeText(entry.latentMode)}`} />
        <ApproachBadges status={status} evidence={evidence} reads={reads} />
      </PageHeader>
      {entry.hero &&
        (entry.slug === "rust" ? (
          <div className="engine-rust-hero">
            <RustEngineHero />
          </div>
        ) : (
          <div className="engine-hero fig-frame" aria-hidden="true">
            <TechniqueArt name={entry.art} mode="loop" title={entry.title} />
          </div>
        ))}
      <ArticleLayout toc={toc} aside={aside}>
        {body}
        <nav className="engine-nav" aria-label="Other engines">
          {previous ? (
            <Button variant="ghost" href={`/engine/${previous.slug}`}>
              ← {previous.title}
            </Button>
          ) : (
            <span />
          )}
          {next && (
            <Button variant="ghost" href={`/engine/${next.slug}`}>
              {next.title} →
            </Button>
          )}
        </nav>
      </ArticleLayout>
    </div>
  );
}
