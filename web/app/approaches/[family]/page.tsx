import "../approaches.css";
import { readFileSync } from "node:fs";
import matter from "gray-matter";
import Link from "next/link";
import { notFound } from "next/navigation";
import { ApproachCard, sortApproaches } from "@/components/ApproachCard";
import { ArticleLayout } from "@/components/ArticleLayout";
import { Badge } from "@/components/Badge";
import { Mdx } from "@/components/Mdx";
import { PageHeader } from "@/components/PageHeader";
import { approachCountLabel } from "@/components/TechniqueCard";
import { extractHeadings } from "@/lib/headings";
import { pageMetadata } from "@/lib/metadata";
import { familyDocPath, familyMeta, listApproaches, listFamilies, type ApproachEntry } from "@/lib/repo";
import { getTechnique } from "@/lib/techniques";

export const dynamic = "force-dynamic";

type Props = { params: Promise<{ family: string }> };

export async function generateMetadata({ params }: Props) {
  const { family } = await params;
  const path = `/approaches/${family}`;
  if (!listFamilies().includes(family)) return pageMetadata({ title: "Approaches", path });
  const meta = familyMeta(family);
  return pageMetadata({ title: meta.title, description: meta.summary || undefined, path });
}

/** The technique title for a card's eyebrow; the group name when the README names none. */
function eyebrowFor(entry: ApproachEntry): string {
  const technique = entry.technique ? getTechnique(entry.technique) : null;
  return technique ? technique.title : "Other";
}

export default async function FamilyPage({ params }: Props) {
  const { family } = await params;
  if (!listFamilies().includes(family)) notFound();

  const meta = familyMeta(family);
  const members = listApproaches(family);
  const strategies = sortApproaches(members.filter((entry) => entry.kind === "strategy"));
  const instruments = members.filter((entry) => entry.kind === "engine" || entry.kind === "diagnostic");
  const others = members.filter((entry) => entry.kind === "unknown");

  const docPath = familyDocPath(family);
  const doc = docPath ? matter(readFileSync(docPath, "utf8")) : null;
  const toc = doc ? extractHeadings(doc.content, { minDepth: 2, maxDepth: 2 }) : [];

  return (
    <div>
      <PageHeader
        crumbs={[{ href: "/approaches", label: "approaches" }]}
        title={meta.title}
        lead={meta.summary || undefined}
      />

      <section className="approach-section" aria-labelledby="family-approaches">
        <div className="approach-section-head">
          <h2 id="family-approaches">Approaches in this family</h2>
          <p>
            {approachCountLabel(strategies.length)}, featured pages first. The label above each title is the
            technique the approach uses; the same pages appear under that technique on the{" "}
            <Link href="/approaches">approaches index</Link>.
          </p>
        </div>
        {strategies.length === 0 ? (
          <p className="approaches-empty">This directory holds no strategy page.</p>
        ) : (
          <div className="approach-grid">
            {strategies.map((entry) => (
              <ApproachCard key={entry.slug} entry={entry} eyebrow={eyebrowFor(entry)} />
            ))}
          </div>
        )}
      </section>

      {instruments.length > 0 && (
        <section className="approach-section instruments" aria-labelledby="family-instruments">
          <div className="approach-section-head">
            <h2 id="family-instruments">Instruments in this directory</h2>
            <p>
              These play no game of their own: engines run the games and diagnostics measure them. They are
              listed here because they live alongside the approaches above; the full sets are under{" "}
              <Link href="/engines">Engines</Link> and <Link href="/diagnostics">Diagnostics</Link>.
            </p>
          </div>
          <ul className="instruments-list">
            {instruments.map((entry) => (
              <li key={entry.slug}>
                <Link href={`/approaches/${family}/${entry.slug}`}>{entry.title}</Link>
                <Badge label={entry.kind} />
                {entry.summary && <p className="instruments-summary">{entry.summary}</p>}
              </li>
            ))}
          </ul>
        </section>
      )}

      {others.length > 0 && (
        <section className="approach-section instruments" aria-labelledby="family-others">
          <div className="approach-section-head">
            <h2 id="family-others">Other directories</h2>
            <p>These directories have no page of their own yet. Each link lists the source files inside.</p>
          </div>
          <ul className="instruments-list">
            {others.map((entry) => (
              <li key={entry.slug}>
                <Link href={`/approaches/${family}/${entry.slug}`}>{entry.title}</Link>
                <Badge label="no page yet" />
              </li>
            ))}
          </ul>
        </section>
      )}

      {doc && (
        <section className="family-essay" aria-label="About this family">
          <ArticleLayout toc={toc}>
            <Mdx source={doc.content} fromPath={`approaches/${family}/README.mdx`} />
          </ArticleLayout>
        </section>
      )}
    </div>
  );
}
