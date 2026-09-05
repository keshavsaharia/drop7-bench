import "./techniques.css";
import Link from "next/link";
import { Card } from "@/components/Card";
import { PageHeader } from "@/components/PageHeader";
import { TechniqueArt } from "@/components/technique-art/TechniqueArt";
import { listTechniquePages } from "@/lib/learn";
import { pageMetadata } from "@/lib/metadata";
import { listApproachesByTechnique } from "@/lib/repo";
import { listTechniques } from "@/lib/techniques";

export const dynamic = "force-dynamic";

export const metadata = pageMetadata({
  title: "Techniques",
  description:
    "The ideas behind every Drop7 strategy on this site, each explained from the ground up before it meets the game.",
  path: "/learn/techniques",
});

export default function TechniquesIndexPage() {
  const pages = listTechniquePages();
  const bySlug = new Map(pages.map((page) => [page.technique, page]));
  const techniques = listTechniques();

  return (
    <div className="techniques-index">
      <PageHeader
        crumbs={[{ href: "/learn", label: "learn" }]}
        title="Techniques"
        lead="Fourteen ways to build a player, each explained with a small example before it meets Drop7. Read one, then follow the link to the approaches that tried it."
      />
      {pages.length === 0 && (
        <p className="empty-note">
          No technique primers are present in this checkout. They live in{" "}
          <code>web/content/learn/techniques/</code>.
        </p>
      )}
      <ol className="technique-grid">
        {techniques.map((technique) => {
          const page = bySlug.get(technique.slug);
          const count = listApproachesByTechnique(technique.slug).length;
          const href = page ? `/learn/techniques/${page.slug}` : `/approach/technique/${technique.slug}`;
          return (
            <li key={technique.slug}>
              <Card
                href={href}
                art={<TechniqueArt name={technique.slug} title={technique.title} />}
                title={technique.title}
                summary={page?.summary || technique.oneLine}
                foot={
                  <span className="label">
                    {count === 1 ? "1 approach" : `${count} approaches`}
                    {!page && " · primer not written yet"}
                  </span>
                }
              />
            </li>
          );
        })}
      </ol>
      <p className="techniques-footnote">
        Every technique group also has a page under{" "}
        <Link href="/approach">Approaches</Link> listing the strategies that used it and how
        each one fared.
      </p>
    </div>
  );
}
