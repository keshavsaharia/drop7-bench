import type { Metadata } from "next";
import { notFound } from "next/navigation";
import { SourceBrowser } from "@/components/SourceBrowser";
import { getRepoSource } from "@/lib/repo";

export const dynamic = "force-dynamic";

type Props = { params: Promise<{ path?: string[] }> };

export async function generateMetadata({ params }: Props): Promise<Metadata> {
  const { path = [] } = await params;
  const entry = getRepoSource(["src", ...path].join("/"));
  return {
    title: entry ? `${entry.name} · Source · Drop7 Research` : "Source · Drop7 Research",
  };
}

export default async function SourcePage({ params }: Props) {
  const { path = [] } = await params;
  const entry = getRepoSource(["src", ...path].join("/"));
  if (!entry) notFound();
  return <SourceBrowser entry={entry} treeRoot="src" />;
}
