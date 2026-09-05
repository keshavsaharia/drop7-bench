import { notFound } from "next/navigation";
import { SourceBrowser } from "@/components/SourceBrowser";
import { pageMetadata } from "@/lib/metadata";
import { getRepoSource } from "@/lib/repo";

export const dynamic = "force-dynamic";

type Props = { params: Promise<{ path?: string[] }> };

export async function generateMetadata({ params }: Props) {
  const { path = [] } = await params;
  const repoPath = ["src", ...path].join("/");
  const entry = getRepoSource(repoPath);
  return pageMetadata({
    title: entry ? `${entry.name} · Source` : "Source",
    description: entry?.path,
    path: entry?.href ?? `/${repoPath}`,
    image: `/share/source/${repoPath}`,
    imageAlt: entry ? `${entry.name}, source on Drop7 Research` : "Source on Drop7 Research",
  });
}

export default async function SourcePage({ params }: Props) {
  const { path = [] } = await params;
  const entry = getRepoSource(["src", ...path].join("/"));
  if (!entry) notFound();
  return <SourceBrowser entry={entry} treeRoot="src" />;
}
