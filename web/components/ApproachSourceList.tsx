/** A compact source list rooted at the smallest enclosing approach folder. */
import Link from "next/link";

export interface ApproachSourceListProps {
  family: string;
  slug: string;
  files: readonly string[];
}

function fileHref(family: string, slug: string, file: string): string {
  return `/approaches/${encodeURIComponent(family)}/${encodeURIComponent(slug)}/${file
    .split("/")
    .map(encodeURIComponent)
    .join("/")}`;
}

function SourceFile({ family, slug, file }: { family: string; slug: string; file: string }) {
  const fullPath = `approaches/${family}/${slug}/${file}`;
  const name = file.split("/").at(-1) ?? file;
  return (
    <Link href={fileHref(family, slug, file)} title={fullPath}>
      <span className="approach-source-file-icon" aria-hidden="true" />
      <span>{name}</span>
    </Link>
  );
}

export function ApproachSourceList({ family, slug, files }: ApproachSourceListProps) {
  if (files.length === 0) return <p className="aside-text">No source files are listed.</p>;

  if (files.length === 1) {
    return (
      <ul className="approach-source-list approach-source-list--single">
        <li>
          <SourceFile family={family} slug={slug} file={files[0]} />
        </li>
      </ul>
    );
  }

  return (
    <div className="approach-source-tree" role="tree" aria-label={`Source files in ${slug}`}>
      <div className="approach-source-folder" role="treeitem" aria-expanded="true">
        <span className="approach-source-folder-icon" aria-hidden="true" />
        <span title={`approaches/${family}/${slug}`}>{slug}</span>
      </div>
      <ul role="group" className="approach-source-list">
        {files.map((file) => (
          <li role="treeitem" key={file}>
            <SourceFile family={family} slug={slug} file={file} />
          </li>
        ))}
      </ul>
    </div>
  );
}
