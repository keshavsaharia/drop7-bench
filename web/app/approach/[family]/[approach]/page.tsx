import "../../approaches.css";
import { readFileSync } from "node:fs";
import { relative as relativePath } from "node:path";
import matter from "gray-matter";
import Link from "next/link";
import { notFound } from "next/navigation";
import { sortApproaches } from "@/components/ApproachCard";
import { ApproachBadges, hasApproachBadges } from "@/components/ApproachBadges";
import { ApproachRecords } from "@/components/ApproachRecords";
import { ApproachSourceList } from "@/components/ApproachSourceList";
import { AsideRecords } from "@/components/AsideRecords";
import { ArticleLayout } from "@/components/ArticleLayout";
import { Button } from "@/components/Button";
import { Markdown } from "@/components/Markdown";
import { Mdx } from "@/components/Mdx";
import { PageHeader, type Crumb } from "@/components/PageHeader";
import { techniqueHref } from "@/components/TechniqueCard";
import { getApproachArt } from "@/components/technique-art/approach/registry";
import { TechniqueArt } from "@/components/technique-art/TechniqueArt";
import { extractHeadings } from "@/lib/headings";
import { techniquePageForTechnique } from "@/lib/learn";
import { pageMetadata } from "@/lib/metadata";
import { recordsForApproach } from "@/lib/records";
import {
  approachDocPath,
  familyMeta,
  listApproaches,
  listApproachesByTechnique,
  listFamilies,
  readApproachFrontmatter,
  REPO_ROOT,
  type ApproachEntry,
} from "@/lib/repo";
import { getTechnique } from "@/lib/techniques";

export const dynamic = "force-dynamic";

type Props = { params: Promise<{ family: string; approach: string }> };

const APPROACHES_CRUMB: Crumb = { href: "/approach", label: "approaches" };

export async function generateMetadata({ params }: Props) {
  const { family, approach } = await params;
  const front = readApproachFrontmatter(family, approach);
  return pageMetadata({
    title: typeof front?.title === "string" ? front.title : approach,
    description: typeof front?.summary === "string" ? front.summary : undefined,
    path: `/approach/${family}/${approach}`,
  });
}

/**
 * The template for every approach directory. `kind` from the README decides
 * the breadcrumb and eyebrow: a strategy sits under its technique group, an
 * engine under Engines, a diagnostic under Diagnostics, and a directory with
 * no kind (or no README) under its family. A directory without a README gets
 * the same header and accordions with nothing in between.
 */
export default async function ApproachPage({ params }: Props) {
  const { family, approach } = await params;
  if (!listFamilies().includes(family)) notFound();
  const siblings = listApproaches(family);
  const entry = siblings.find((candidate) => candidate.slug === approach);
  if (!entry) notFound();

  const docPath = approachDocPath(family, approach);
  const raw = docPath ? readFileSync(docPath, "utf8") : null;
  const isMdx = docPath?.endsWith(".mdx") ?? false;
  const content = raw !== null && isMdx ? matter(raw).content : (raw ?? "");
  const fromPath = docPath ? relativePath(REPO_ROOT, docPath).replaceAll("\\", "/") : undefined;

  const technique = entry.technique ? getTechnique(entry.technique) : null;
  const primer = technique ? techniquePageForTechnique(technique.slug) : null;
  const family_ = familyMeta(family);
  const records = recordsForApproach(family, approach);
  const toc = docPath ? extractHeadings(content, { minDepth: 2, maxDepth: 2 }) : [];

  let crumbs: Crumb[];
  let eyebrow: string | null = null;
  switch (entry.kind) {
    case "strategy":
      crumbs = technique
        ? [APPROACHES_CRUMB, { href: techniqueHref(technique.slug), label: technique.title }]
        : [APPROACHES_CRUMB, { href: `/approach/${family}`, label: family_.title }];
      break;
    case "engine":
      crumbs = [{ href: "/engine", label: "engines" }];
      eyebrow = "Engine";
      break;
    case "diagnostic":
      crumbs = [
        { href: "/research", label: "research" },
        { href: "/diagnostics", label: "diagnostics" },
      ];
      eyebrow = "Diagnostic";
      break;
    default:
      crumbs = [APPROACHES_CRUMB, { href: `/approach/${family}`, label: family_.title }];
  }

  // Previous and next: within the technique group for a strategy, within the family otherwise.
  const inTechniqueGroup = entry.kind === "strategy" && technique !== null;
  const sequence: ApproachEntry[] = inTechniqueGroup
    ? sortApproaches(listApproachesByTechnique(technique.slug).filter((candidate) => candidate.kind === "strategy"))
    : siblings;
  const position = sequence.findIndex((candidate) => candidate.family === family && candidate.slug === approach);
  const previous = position > 0 ? sequence[position - 1] : null;
  const next = position >= 0 && position < sequence.length - 1 ? sequence[position + 1] : null;
  const sequenceName = inTechniqueGroup ? technique.title : family_.title;

  const hasLabels = Boolean(eyebrow || hasApproachBadges(entry));
  // Directories with an art of their own show it even when no technique strip
  // is drawn, which is how a diagnostic or an undocumented directory gets one.
  const hasOwnArt = getApproachArt(family, approach) !== null;

  const aside = (
    <>
      <AsideRecords records={records} />
      <div className="aside-block">
        <span className="label">Family</span>
        <p className="aside-text">
          <Link href={`/approach/${family}`}>{family_.title}</Link>
        </p>
      </div>
      <div className="aside-block">
        <span className="label">Sources</span>
        {entry.sourceFiles.length > 0 ? (
          <ApproachSourceList family={family} slug={approach} files={entry.sourceFiles} />
        ) : (
          <p className="aside-text">No source files are listed.</p>
        )}
      </div>
    </>
  );

  return (
    <div>
      <PageHeader crumbs={crumbs} title={entry.title} lead={entry.summary || undefined}>
        {hasLabels ? (
          <>
            {eyebrow && <span className="label">{eyebrow}</span>}
            <ApproachBadges status={entry.status} evidence={entry.evidence} reads={entry.reads} />
          </>
        ) : undefined}
      </PageHeader>

      {entry.kind === "strategy" && technique ? (
        <section className="technique-strip" aria-label="Technique">
          <div className="fig-frame">
            <TechniqueArt name={technique.slug} approach={{ family, slug: approach }} mode="loop" />
          </div>
          <div className="technique-strip-text">
            <span className="label">Technique</span>
            <p className="technique-strip-title">
              <Link href={techniqueHref(technique.slug)}>{technique.title}</Link>
            </p>
            {primer ? (
              <>
                <p>{primer.summary}</p>
                <p>
                  <Link className="technique-strip-primer" href={`/learn/techniques/${primer.slug}`}>
                    Read the primer: {primer.title}
                  </Link>
                </p>
              </>
            ) : (
              <p>{technique.oneLine}</p>
            )}
          </div>
        </section>
      ) : hasOwnArt ? (
        <section className="technique-strip" aria-label={entry.title}>
          <div className="fig-frame">
            <TechniqueArt name="fallback" approach={{ family, slug: approach }} mode="loop" />
          </div>
        </section>
      ) : null}

      <ArticleLayout toc={toc} aside={aside}>
        {docPath && raw !== null ? (
          isMdx ? (
            <Mdx source={content} fromPath={fromPath} />
          ) : (
            <Markdown source={raw} fromPath={fromPath} />
          )
        ) : (
          <p className="approaches-empty">
            This directory has no page of its own yet. Its source files, and any records that reference it,
            are listed below.
          </p>
        )}
        <ApproachRecords entry={entry} records={records} noDocs={!docPath} />
      </ArticleLayout>

      {(previous || next) && (
        <nav className="approach-nav" aria-label={`Previous and next in ${sequenceName}`}>
          {previous && (
            <Button variant="ghost" href={`/approach/${previous.family}/${previous.slug}`}>
              ← {previous.title}
            </Button>
          )}
          {next && (
            <Button variant="ghost" href={`/approach/${next.family}/${next.slug}`} className="approach-nav-next">
              {next.title} →
            </Button>
          )}
        </nav>
      )}
    </div>
  );
}
