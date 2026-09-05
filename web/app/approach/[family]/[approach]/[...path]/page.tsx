import { notFound } from "next/navigation";
import { SourceBrowser } from "@/components/SourceBrowser";
import { pageMetadata } from "@/lib/metadata";
import { getRepoSource } from "@/lib/repo";

export const dynamic = "force-dynamic";

type Props = {
  params: Promise<{ family: string; approach: string; path: string[] }>;
};

function paths({ family, approach, path }: Awaited<Props["params"]>) {
  return {
    repoPath: ["approaches", family, approach, ...path].join("/"),
    treeRoot: ["approaches", family, approach].join("/"),
  };
}

export async function generateMetadata({ params }: Props) {
  const resolved = paths(await params);
  const entry = getRepoSource(resolved.repoPath);
  return pageMetadata({
    title: entry ? `${entry.name} · Source` : "Source",
    description: entry?.path,
    path: entry?.href ?? `/approach/${resolved.repoPath.slice("approaches/".length)}`,
    image: `/share/source/${resolved.repoPath}`,
    imageAlt: entry ? `${entry.name}, source on Drop7 Research` : "Source on Drop7 Research",
  });
}

export default async function ApproachSourcePage({ params }: Props) {
  const resolved = paths(await params);
  const entry = getRepoSource(resolved.repoPath);
  if (!entry) notFound();
  return <SourceBrowser entry={entry} treeRoot={resolved.treeRoot} />;
}
