/**
 * Content-backed social images for catch-all pages. Next cannot place an
 * `opengraph-image` route beside another catch-all, so documents and source
 * pages point here explicitly from their metadata.
 */
import { listDocs } from "@/lib/docs";
import { TechniqueArt } from "@/components/technique-art/TechniqueArt";
import { getRepoSource, sourceLanguageLabel } from "@/lib/repo";
import { readApproachFrontmatter } from "@/lib/repo";
import { getEngine } from "@/lib/engines";
import { socialArtSvg } from "@/lib/social-art";
import { renderPageCard } from "@/lib/social-card";

export const dynamic = "force-dynamic";

type Params = Promise<{ kind: string; path: string[] }>;

export async function GET(_request: Request, { params }: { params: Params }) {
  const { kind, path: parts } = await params;
  const target = parts.join("/");

  if (kind === "art") {
    const [scope, first, second] = parts;
    let art;
    if (scope === "approach" && first && second) {
      const frontmatter = readApproachFrontmatter(first, second);
      art = (
        <TechniqueArt
          name={typeof frontmatter?.technique === "string" ? frontmatter.technique : "fallback"}
          approach={{ family: first, slug: second }}
          mode="static"
        />
      );
    } else if (scope === "engine" && first) {
      art = <TechniqueArt name={getEngine(first)?.art ?? "fallback"} mode="static" />;
    } else {
      art = <TechniqueArt name={first ?? "fallback"} mode="static" />;
    }
    return new Response(socialArtSvg(art), {
      headers: { "content-type": "image/svg+xml; charset=utf-8" },
    });
  }

  if (kind === "docs") {
    const doc = listDocs().find((entry) => entry.slug === target) ?? null;
    return renderPageCard({
      eyebrow: "Document",
      title: doc?.title ?? parts.at(-1) ?? "Document",
      summary: doc?.summary || undefined,
      path: `/docs/${target}`,
    });
  }

  if (kind === "source") {
    const entry = getRepoSource(target);
    const canonicalPath = entry?.href ?? `/${target}`;
    return renderPageCard({
      eyebrow: entry?.kind === "file" ? sourceLanguageLabel(entry.language) : "Source",
      title: entry?.name ?? parts.at(-1) ?? "Source",
      summary: entry ? entry.path : undefined,
      path: canonicalPath,
    });
  }

  return renderPageCard({ title: "Drop7 Research", path: `/${target}` });
}
