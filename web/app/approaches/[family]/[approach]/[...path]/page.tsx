import type { Metadata } from "next";
import { notFound } from "next/navigation";
import { SourceBrowser } from "@/components/SourceBrowser";
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

export async function generateMetadata({ params }: Props): Promise<Metadata> {
  const resolved = paths(await params);
  const entry = getRepoSource(resolved.repoPath);
  return {
    title: entry ? `${entry.name} · Source · Drop7 Research` : "Source · Drop7 Research",
  };
}

export default async function ApproachSourcePage({ params }: Props) {
  const resolved = paths(await params);
  const entry = getRepoSource(resolved.repoPath);
  if (!entry) notFound();
  return <SourceBrowser entry={entry} treeRoot={resolved.treeRoot} />;
}
