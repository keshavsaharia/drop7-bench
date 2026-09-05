import Link from "next/link";
import { codeToHtml, type BundledLanguage } from "shiki";
import { FileTree } from "./FileTree";
import styles from "./SourceBrowser.module.css";
import {
  getRepoSourceTree,
  sourceLanguage,
  sourceLanguageLabel,
  type RepoSourceEntry,
  type RepoSourceTreeNode,
} from "@/lib/repo";

interface SourceBrowserProps {
  entry: RepoSourceEntry;
  treeRoot: string;
}

function parentDirectories(treeRoot: string, entry: RepoSourceEntry): string[] {
  const parts = entry.path.split("/");
  const rootDepth = treeRoot.split("/").length;
  const folderDepth = entry.kind === "directory" ? parts.length : parts.length - 1;
  const paths: string[] = [];
  for (let length = rootDepth; length <= folderDepth; length += 1) {
    paths.push(parts.slice(0, length).join("/"));
  }
  return paths;
}

function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

function DirectoryContents({ entries }: { entries: RepoSourceTreeNode[] }) {
  return (
    <div className="grid gap-2 sm:grid-cols-2">
      {entries.map((child) => {
        const directory = child.children !== undefined;
        const language = directory ? null : sourceLanguage(child.path);
        return (
          <Link
            key={child.path}
            href={child.href}
            className="group flex min-w-0 items-center gap-3 rounded-lg border border-rule bg-surface p-3 transition-colors hover:border-rule-strong hover:bg-hover"
          >
            <span
              className={
                directory
                  ? "flex size-9 shrink-0 items-center justify-center rounded-md bg-accent-soft text-accent"
                  : "flex size-9 shrink-0 items-center justify-center rounded-md bg-raised font-mono text-[10px] font-bold text-ink-2"
              }
              aria-hidden="true"
            >
              {directory ? "▰" : language === "cpp" ? "C++" : language === "rust" ? "RS" : language === "typescript" || language === "tsx" ? "TS" : "·/·"}
            </span>
            <span className="min-w-0">
              <span className="block truncate font-mono text-small text-ink-1 group-hover:text-accent">
                {child.name}
              </span>
              <span className="mt-0.5 block text-caption text-ink-3">
                {directory ? `${child.children?.length ?? 0} entries` : sourceLanguageLabel(language ?? "text")}
              </span>
            </span>
          </Link>
        );
      })}
    </div>
  );
}

async function HighlightedCode({ entry }: { entry: Extract<RepoSourceEntry, { kind: "file" }> }) {
  const html = await codeToHtml(entry.source, {
    lang: entry.language as BundledLanguage,
    // Paired with `--code-ground` in SourceBrowser.module.css, which is this
    // theme's own background; change both together.
    theme: "github-dark-default",
    transformers: [
      {
        line(node, line) {
          node.properties.id = `L${line}`;
          node.properties["data-line"] = line;
        },
      },
    ],
  });

  return (
    <div className={styles.codeFrame}>
      <div className={styles.codeToolbar}>
        <span className={styles.windowDots} aria-hidden="true">
          <span />
          <span />
          <span />
        </span>
        <span className="min-w-0 truncate font-mono text-caption text-ink-2">{entry.path}</span>
        <span className="shrink-0 rounded-xs border border-rule bg-bg px-1.5 py-0.5 text-[10px] text-ink-3">
          {sourceLanguageLabel(entry.language)}
        </span>
        <span className="ml-auto shrink-0 text-caption text-ink-3">
          {entry.lines.toLocaleString()} lines · {formatBytes(entry.bytes)}
        </span>
      </div>
      <div className={styles.codeScroll} tabIndex={0} aria-label={`Source code for ${entry.path}`}>
        <div className={styles.code} dangerouslySetInnerHTML={{ __html: html }} />
      </div>
    </div>
  );
}

/** Shared server-rendered repository directory and source-file viewer. */
export async function SourceBrowser({ entry, treeRoot }: SourceBrowserProps) {
  const tree = getRepoSourceTree(treeRoot);
  if (!tree) return null;

  return (
    <div className={`source-workspace ${styles.workspace}`}>
      <aside className={styles.sidebar} aria-label="Source navigation">
        <div className={styles.treeToolbar}>
          <span className="label">Explorer</span>
          <span className="min-w-0 truncate font-mono text-[10px] text-ink-3">{treeRoot}</span>
        </div>
        <FileTree
          tree={[tree]}
          defaultExpanded={parentDirectories(treeRoot, entry)}
          iconStyle="colored"
          highlight={[entry.path]}
        />
      </aside>

      <section className={styles.sourcePane} aria-label="Source viewer">
        {entry.kind === "file" ? (
          <HighlightedCode entry={entry} />
        ) : (
          <div className={styles.directoryPane}>
            <div className="mb-4 flex items-center justify-between gap-3">
              <h1 className="min-w-0 truncate font-mono text-small font-semibold text-ink">{entry.path}</h1>
              <span className="shrink-0 text-caption text-ink-3">{entry.children.length} entries</span>
            </div>
            <DirectoryContents entries={entry.children} />
          </div>
        )}
      </section>
    </div>
  );
}
