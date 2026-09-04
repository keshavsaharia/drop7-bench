/** Rewrite repository Markdown hrefs so docs pages resolve on the console. */

const MARKDOWN_EXT = /\.mdx?$/i;

function splitHref(href: string): { path: string; suffix: string } {
  const hash = href.indexOf("#");
  const query = href.indexOf("?");
  let end = href.length;
  if (hash >= 0) end = hash;
  if (query >= 0 && query < end) end = query;
  return { path: href.slice(0, end), suffix: href.slice(end) };
}

function stripMarkdownExt(path: string): string {
  return path.replace(MARKDOWN_EXT, "");
}

export function resolveRepoRelative(fromFile: string, href: string): string {
  const fromDir = fromFile.includes("/")
    ? fromFile.slice(0, fromFile.lastIndexOf("/"))
    : "";
  const parts = [...(fromDir ? fromDir.split("/") : []), ...href.split("/")];
  const out: string[] = [];
  for (const part of parts) {
    if (part === "" || part === ".") continue;
    if (part === "..") {
      out.pop();
      continue;
    }
    out.push(part);
  }
  return out.join("/");
}

function isExternal(path: string): boolean {
  return (
    path.startsWith("mailto:") ||
    path.startsWith("tel:") ||
    /^(https?:)?\/\//i.test(path)
  );
}

function toDocsHref(repoPath: string, suffix: string): string | null {
  if (!repoPath.startsWith("docs/") || !MARKDOWN_EXT.test(repoPath)) return null;
  return `/docs/${stripMarkdownExt(repoPath.slice("docs/".length))}${suffix}`;
}

function toApproachHref(repoPath: string, suffix: string): string | null {
  const trimmed = repoPath.replace(/\/+$/, "");
  const withoutReadme = trimmed.replace(/\/README\.mdx?$/i, "");
  const match = /^approaches\/([^/]+)(?:\/([^/]+))?$/.exec(withoutReadme);
  if (!match) return null;
  const family = match[1];
  const approach = match[2];
  if (approach?.includes(".")) return null;
  return approach ? `/approaches/${family}/${approach}${suffix}` : `/approaches/${family}${suffix}`;
}

function toRecordHref(repoPath: string, suffix: string): string | null {
  const trimmed = repoPath.replace(/\/+$/, "");
  if (trimmed === "research" || /^research\/README\.mdx?$/i.test(trimmed)) {
    return `/research${suffix}`;
  }
  const theory = /^research\/theories\/([^/]+)\.json$/i.exec(repoPath);
  if (theory) return `/theories/${theory[1]}${suffix}`;
  const experiment = /^research\/experiments\/([^/]+)\.json$/i.exec(repoPath);
  if (experiment) return `/experiments/${experiment[1]}${suffix}`;
  const result = /^research\/results\/([^/]+)\.json$/i.exec(repoPath);
  if (result) return `/results/${result[1]}${suffix}`;
  return null;
}

function toLogHref(repoPath: string, suffix: string): string | null {
  const entry = /^web\/content\/log\/(\d{4}-\d{2}-\d{2})\.mdx?$/i.exec(repoPath);
  return entry ? `/log/${entry[1]}${suffix}` : null;
}

function toSourceHref(repoPath: string, suffix: string): string | null {
  if (repoPath === "src" || repoPath.startsWith("src/")) {
    return `/${repoPath}${suffix}`;
  }
  if (/^approaches\/[^/]+\/[^/]+\/.+/.test(repoPath)) {
    return `/${repoPath}${suffix}`;
  }
  return null;
}

function toConsoleHref(repoPath: string, suffix: string): string | null {
  return toDocsHref(repoPath, suffix) ?? toApproachHref(repoPath, suffix) ?? toRecordHref(repoPath, suffix) ?? toLogHref(repoPath, suffix) ?? toSourceHref(repoPath, suffix);
}

/**
 * Map a repository Markdown href onto a console route when one exists:
 * `docs/*.md` → `/docs/<slug>`, approach directories → `/approaches/…`,
 * source files → their repository path, and theory/experiment JSON →
 * `/theories/<id>` or `/experiments/<id>`.
 */
export function rewriteRepoDocHref(href: string, fromRepoPath?: string): string {
  if (!href) return href;

  const { path, suffix } = splitHref(href);
  if (path === "" || isExternal(path)) return href;

  if (path.startsWith("/docs/")) {
    return `/docs/${stripMarkdownExt(path.slice("/docs/".length))}${suffix}`;
  }

  if (path.startsWith("/")) return href;

  const rooted = toConsoleHref(path, suffix);
  if (rooted) return rooted;

  if (fromRepoPath) {
    const resolved = resolveRepoRelative(fromRepoPath, path);
    const mapped = toConsoleHref(resolved, suffix);
    if (mapped) return mapped;
  }

  return href;
}
