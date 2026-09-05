import { basename } from "node:path";
import Link from "next/link";
import type { ReactNode } from "react";
import { codeToHtml, type BundledLanguage } from "shiki";
import { readRepoFile, sourceLanguage, sourceLanguageLabel } from "@/lib/repo";
import styles from "./CodeSnippet.module.css";

export interface CodeSnippetProps {
  /** Repository-relative path under `src/`, `approaches/` or `web/`. */
  path: string;
  /** First source line to render, inclusive and one-based. Defaults to 1. */
  startLine?: number;
  /** Last source line to render, inclusive and one-based. Defaults to EOF. */
  endLine?: number;
  /** Optional description shown beside the source filename. */
  title?: ReactNode;
  /** Optional explanation rendered as the figure caption. */
  caption?: ReactNode;
}

interface SourceExcerpt {
  end: number;
  language: string;
  source: string;
  start: number;
}

function isSnippetSourcePath(path: string): boolean {
  if (!path || path.startsWith("/") || path.includes("\\")) return false;
  const parts = path.split("/");
  return (
    (parts[0] === "src" || parts[0] === "approaches" || parts[0] === "web") &&
    parts.every((part) => part.length > 0 && part !== "." && part !== "..")
  );
}

function sourceViewerHref(path: string, line: number): string | null {
  // The source browser publishes src/ and approaches/. Web excerpts still come
  // from the live checkout, but stay unlinked until that tree has its own route.
  if (path.startsWith("web/")) return null;
  const pathname = path
    .split("/")
    .map((part) => encodeURIComponent(part))
    .join("/");
  return `/${pathname}#L${line}`;
}

function excerptFor(
  path: string,
  startLine: number | undefined,
  endLine: number | undefined,
): SourceExcerpt | null {
  if (!isSnippetSourcePath(path)) return null;

  let raw: string | null;
  try {
    raw = readRepoFile(path);
  } catch {
    return null;
  }
  if (raw === null || raw.length === 0) return null;

  const language = sourceLanguage(path);
  if (!language) return null;

  const normalized = raw.replace(/\r\n?/g, "\n");
  const lines = normalized.endsWith("\n")
    ? normalized.slice(0, -1).split("\n")
    : normalized.split("\n");
  if (lines.length === 0) return null;

  const start = startLine ?? 1;
  const end = endLine ?? lines.length;
  if (
    !Number.isSafeInteger(start) ||
    !Number.isSafeInteger(end) ||
    start < 1 ||
    end < start ||
    end > lines.length
  ) {
    return null;
  }

  return {
    start,
    end,
    language,
    source: lines.slice(start - 1, end).join("\n"),
  };
}

function Header({
  path,
  excerpt,
  title,
}: {
  path: string;
  excerpt: SourceExcerpt | null;
  title?: ReactNode;
}) {
  const name = basename(path) || "source";
  const href = excerpt ? sourceViewerHref(path, excerpt.start) : null;
  const lineLabel = excerpt
    ? excerpt.start === excerpt.end
      ? `line ${excerpt.start}`
      : `lines ${excerpt.start}–${excerpt.end}`
    : null;

  return (
    <header className={styles.header}>
      <div className={styles.identity}>
        {excerpt && href ? (
          <Link
            href={href}
            className={styles.fileLink}
            title={`${path} — open at line ${excerpt.start}`}
            aria-label={`Open ${path} at line ${excerpt.start}`}
          >
            {name}
          </Link>
        ) : (
          <span className={styles.fileName} title={path} aria-label={path}>
            {name}
          </span>
        )}
        {title !== undefined && <span className={styles.title}>{title}</span>}
      </div>
      {excerpt && (
        <div className={styles.meta} aria-label={`${sourceLanguageLabel(excerpt.language)}, ${lineLabel}`}>
          <span>{sourceLanguageLabel(excerpt.language)}</span>
          <span aria-hidden="true">·</span>
          <span>{lineLabel}</span>
        </div>
      )}
    </header>
  );
}

function Unavailable({ path, title, caption }: Pick<CodeSnippetProps, "path" | "title" | "caption">) {
  return (
    <figure className={styles.frame}>
      <Header path={path} excerpt={null} title={title} />
      <p className={styles.unavailable} role="note">
        This source excerpt is unavailable in this checkout.
      </p>
      {caption !== undefined && <figcaption className={styles.caption}>{caption}</figcaption>}
    </figure>
  );
}

/**
 * A server-rendered, syntax-highlighted excerpt of a retained repository file.
 * The source viewer remains the full-file view; the filename links to the
 * excerpt's first original line there.
 */
export async function CodeSnippet({
  path,
  startLine,
  endLine,
  title,
  caption,
}: CodeSnippetProps) {
  const excerpt = excerptFor(path, startLine, endLine);
  if (!excerpt) return <Unavailable path={path} title={title} caption={caption} />;

  let html: string;
  try {
    html = await codeToHtml(excerpt.source, {
      lang: excerpt.language as BundledLanguage,
      // This is paired with `--code-ground` in CodeSnippet.module.css.
      theme: "github-dark-default",
      transformers: [
        {
          line(node, line) {
            node.properties["data-line"] = excerpt.start + line - 1;
          },
        },
      ],
    });
  } catch {
    return <Unavailable path={path} title={title} caption={caption} />;
  }

  const lineLabel =
    excerpt.start === excerpt.end
      ? `line ${excerpt.start}`
      : `lines ${excerpt.start} through ${excerpt.end}`;

  return (
    <figure className={styles.frame}>
      <Header path={path} excerpt={excerpt} title={title} />
      <div
        className={styles.scroll}
        tabIndex={0}
        role="region"
        aria-label={`Source excerpt from ${path}, ${lineLabel}`}
      >
        <div className={styles.code} dangerouslySetInnerHTML={{ __html: html }} />
      </div>
      {caption !== undefined && <figcaption className={styles.caption}>{caption}</figcaption>}
    </figure>
  );
}
