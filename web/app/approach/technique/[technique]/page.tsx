import "../../approaches.css";
import Link from "next/link";
import { notFound } from "next/navigation";
import { ApproachCard, sortApproaches } from "@/components/ApproachCard";
import { Button } from "@/components/Button";
import { PageHeader } from "@/components/PageHeader";
import { Reveal } from "@/components/Reveal";
import { approachCountLabel } from "@/components/TechniqueCard";
import { TechniqueArt } from "@/components/technique-art/TechniqueArt";
import { techniquePageForTechnique } from "@/lib/learn";
import { pageMetadata } from "@/lib/metadata";
import { listApproachesByTechnique, type ApproachEntry } from "@/lib/repo";
import { getTechnique } from "@/lib/techniques";

export const dynamic = "force-dynamic";

type Props = { params: Promise<{ technique: string }> };

export async function generateMetadata({ params }: Props) {
  const { technique: slug } = await params;
  const technique = getTechnique(slug);
  return pageMetadata({
    title: technique ? technique.title : "Technique",
    description: technique?.oneLine,
    path: `/approach/technique/${slug}`,
  });
}

/** The strategies in this group, by family directory, for the agent accordion. */
function byFamily(entries: readonly ApproachEntry[]): [string, ApproachEntry[]][] {
  const families = new Map<string, ApproachEntry[]>();
  for (const entry of entries) {
    const list = families.get(entry.family) ?? [];
    list.push(entry);
    families.set(entry.family, list);
  }
  return [...families.entries()]
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([family, list]) => [family, [...list].sort((a, b) => a.slug.localeCompare(b.slug))]);
}

export default async function TechniquePage({ params }: Props) {
  const { technique: slug } = await params;
  const technique = getTechnique(slug);
  if (!technique) notFound();

  const approaches = sortApproaches(listApproachesByTechnique(slug).filter((entry) => entry.kind === "strategy"));
  const primer = techniquePageForTechnique(slug);

  return (
    <div>
      <PageHeader
        crumbs={[{ href: "/approach", label: "approaches" }]}
        title={technique.title}
        lead={technique.oneLine}
      />

      <div className="fig-frame technique-hero">
        <TechniqueArt name={technique.slug} mode="loop" />
      </div>

      {primer && (
        <section className="technique-about" aria-labelledby="technique-about-label">
          <span className="label" id="technique-about-label">
            The technique
          </span>
          <p>{primer.summary}</p>
          <Button variant="secondary" href={`/learn/techniques/${primer.slug}`}>
            Read the primer
          </Button>
        </section>
      )}

      <section className="approach-section" aria-labelledby="technique-approaches">
        <div className="approach-section-head">
          <h2 id="technique-approaches">Approaches using this technique</h2>
          <p>
            {approachCountLabel(approaches.length)}, featured pages first. Each card is one theory of how to
            choose a column and what happened when it was tried.
          </p>
        </div>
        {approaches.length === 0 ? (
          <p className="approaches-empty">No approach page carries this technique yet.</p>
        ) : (
          <div className="approach-grid">
            {approaches.map((entry) => (
              <ApproachCard key={`${entry.family}/${entry.slug}`} entry={entry} eyebrow={technique.title} />
            ))}
          </div>
        )}
      </section>

      <div className="approach-records">
        <Reveal variant="agent" summary="Approach directories in this group, by family">
          {approaches.length === 0 ? (
            <p>
              No README under <code>approaches/</code> carries <code>technique: {technique.slug}</code>.
            </p>
          ) : (
            byFamily(approaches).map(([family, entries]) => (
              <div key={family} className="aside-block">
                <span className="label">{family}</span>
                <ul className="aside-list">
                  {entries.map((entry) => (
                    <li key={entry.slug}>
                      <Link href={`/approach/${entry.family}/${entry.slug}`}>
                        approaches/{entry.family}/{entry.slug}
                      </Link>
                    </li>
                  ))}
                </ul>
              </div>
            ))
          )}
        </Reveal>
      </div>
    </div>
  );
}
