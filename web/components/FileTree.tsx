"use client";

import Link from "next/link";
import * as React from "react";

export interface FileTreeNode {
  name: string;
  path: string;
  href: string;
  children?: FileTreeNode[];
}

interface FileTreeProps {
  tree: FileTreeNode[];
  /** Expand all folders, none, or specific repository paths. */
  defaultExpanded?: boolean | string[];
  iconStyle?: "minimal" | "colored";
  /** Full repository paths to mark as selected. */
  highlight?: string[];
  className?: string;
}

function classes(...values: Array<string | false | null | undefined>) {
  return values.filter(Boolean).join(" ");
}

export function FileTree({
  tree,
  defaultExpanded = true,
  iconStyle = "minimal",
  highlight,
  className,
}: FileTreeProps) {
  const highlightSet = React.useMemo(() => new Set(highlight ?? []), [highlight]);
  const initialExpanded = React.useMemo(() => {
    if (defaultExpanded === true) return collectAllFolderPaths(tree);
    if (defaultExpanded === false) return new Set<string>();
    return new Set(defaultExpanded);
  }, [defaultExpanded, tree]);
  const [expanded, setExpanded] = React.useState(initialExpanded);

  const toggle = React.useCallback((path: string) => {
    setExpanded((previous) => {
      const next = new Set(previous);
      if (next.has(path)) next.delete(path);
      else next.add(path);
      return next;
    });
  }, []);

  return (
    <div
      className={classes(
        "rounded-xl border border-zinc-800 bg-zinc-950/70 font-mono text-sm shadow-2xl shadow-black/10",
        className,
      )}
      role="tree"
      aria-label="Repository file tree"
    >
      <div className="p-2">
        {tree.map((node) => (
          <TreeEntry
            key={node.path}
            node={node}
            depth={0}
            expanded={expanded}
            toggle={toggle}
            highlightSet={highlightSet}
            iconStyle={iconStyle}
          />
        ))}
      </div>
    </div>
  );
}

interface TreeEntryProps {
  node: FileTreeNode;
  depth: number;
  expanded: Set<string>;
  toggle: (path: string) => void;
  highlightSet: Set<string>;
  iconStyle: "minimal" | "colored";
}

function TreeEntry({
  node,
  depth,
  expanded,
  toggle,
  highlightSet,
  iconStyle,
}: TreeEntryProps) {
  const isFolder = node.children !== undefined;
  const isOpen = isFolder && expanded.has(node.path);
  const isHighlighted = highlightSet.has(node.path);
  const rowClass = classes(
    "group flex w-full items-center gap-2 rounded-md px-1.5 py-1.5 text-left text-[13px] leading-tight transition-colors",
    isHighlighted
      ? "bg-sky-500/10 font-medium text-sky-300 ring-1 ring-inset ring-sky-500/20"
      : "text-zinc-400 hover:bg-zinc-800/70 hover:text-zinc-100",
  );
  const rowStyle = { paddingLeft: `${depth * 18 + 6}px` };

  return (
    <div
      role="treeitem"
      aria-expanded={isFolder ? isOpen : undefined}
      aria-selected={isHighlighted}
    >
      {isFolder ? (
        <button
          type="button"
          className={rowClass}
          style={rowStyle}
          onClick={() => toggle(node.path)}
          aria-label={`${isOpen ? "Collapse" : "Expand"} ${node.name}`}
        >
          <ChevronIcon open={isOpen} />
          <FolderIcon open={isOpen} iconStyle={iconStyle} />
          <span className="truncate">{node.name}</span>
        </button>
      ) : (
        <Link
          href={node.href}
          className={rowClass}
          style={rowStyle}
          aria-current={isHighlighted ? "page" : undefined}
          title={node.path}
        >
          <span className="w-3.5 shrink-0" aria-hidden="true" />
          <FileIcon name={node.name} iconStyle={iconStyle} />
          <span className="truncate">{node.name}</span>
        </Link>
      )}

      {isFolder && isOpen && node.children && (
        <div role="group">
          {node.children.map((child) => (
            <TreeEntry
              key={child.path}
              node={child}
              depth={depth + 1}
              expanded={expanded}
              toggle={toggle}
              highlightSet={highlightSet}
              iconStyle={iconStyle}
            />
          ))}
        </div>
      )}
    </div>
  );
}

function collectAllFolderPaths(nodes: FileTreeNode[]): Set<string> {
  const paths = new Set<string>();
  for (const node of nodes) {
    if (node.children) {
      paths.add(node.path);
      for (const nested of collectAllFolderPaths(node.children)) paths.add(nested);
    }
  }
  return paths;
}

function ChevronIcon({ open }: { open: boolean }) {
  return (
    <svg
      width="14"
      height="14"
      viewBox="0 0 14 14"
      fill="none"
      className={classes(
        "shrink-0 text-zinc-600 transition-transform duration-150",
        open && "rotate-90",
      )}
      aria-hidden="true"
    >
      <path
        d="M5.25 3.5 8.75 7l-3.5 3.5"
        stroke="currentColor"
        strokeWidth="1.5"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
    </svg>
  );
}

function FolderIcon({
  open,
  iconStyle,
}: {
  open: boolean;
  iconStyle: "minimal" | "colored";
}) {
  const color = iconStyle === "colored" ? "#54aeff" : "currentColor";
  return (
    <svg
      width="16"
      height="16"
      viewBox="0 0 16 16"
      fill="none"
      className="shrink-0"
      style={{ color }}
      aria-hidden="true"
    >
      <path
        d="M1.5 3.5c0-.55.45-1 1-1h3.29L7.29 4h6.21c.55 0 1 .45 1 1v7c0 .55-.45 1-1 1h-11c-.55 0-1-.45-1-1V3.5Z"
        fill="currentColor"
        opacity={open ? "0.2" : "0.12"}
      />
      <path
        d="M1.5 3.5c0-.55.45-1 1-1h3.29L7.29 4h6.21c.55 0 1 .45 1 1v7c0 .55-.45 1-1 1h-11c-.55 0-1-.45-1-1V3.5Z"
        stroke="currentColor"
        strokeWidth="1.2"
        strokeLinejoin="round"
      />
      {open && <path d="M1.5 5.5h13" stroke="currentColor" strokeWidth="1.2" />}
    </svg>
  );
}

const FILE_ICON_MAP: Record<string, { color: string; label: string }> = {
  ts: { color: "#4f9bd8", label: "TS" },
  tsx: { color: "#61dafb", label: "TX" },
  js: { color: "#f7df1e", label: "JS" },
  jsx: { color: "#f7df1e", label: "JX" },
  c: { color: "#8ea9db", label: "C" },
  cc: { color: "#f34b7d", label: "C+" },
  cpp: { color: "#f34b7d", label: "C+" },
  cxx: { color: "#f34b7d", label: "C+" },
  h: { color: "#a074c4", label: "H" },
  hh: { color: "#a074c4", label: "H+" },
  hpp: { color: "#a074c4", label: "H+" },
  rs: { color: "#dea584", label: "Rs" },
  py: { color: "#4b8bbe", label: "Py" },
  json: { color: "#c4c4c4", label: "{}" },
  jsonl: { color: "#c4c4c4", label: "{}" },
  md: { color: "#519aba", label: "M" },
  mdx: { color: "#519aba", label: "MX" },
  css: { color: "#a074c4", label: "#" },
  html: { color: "#e34c26", label: "<>" },
  yaml: { color: "#cb171e", label: "Y" },
  yml: { color: "#cb171e", label: "Y" },
  toml: { color: "#9c4121", label: "T" },
  sh: { color: "#89e051", label: "$" },
  bash: { color: "#89e051", label: "$" },
  go: { color: "#00add8", label: "Go" },
};

function getFileExtension(name: string): string {
  if (name === "Makefile") return "makefile";
  if (name === "Dockerfile") return "dockerfile";
  if (name.startsWith(".") && !name.slice(1).includes(".")) return name.slice(1);
  const parts = name.split(".");
  return parts.length > 1 ? parts.at(-1) ?? "" : "";
}

function FileIcon({
  name,
  iconStyle,
}: {
  name: string;
  iconStyle: "minimal" | "colored";
}) {
  if (iconStyle === "colored") {
    const info = FILE_ICON_MAP[getFileExtension(name)];
    if (info) {
      return (
        <span
          className="flex size-4 shrink-0 items-center justify-center rounded-[3px] text-[8px] font-bold leading-none"
          style={{ backgroundColor: `${info.color}20`, color: info.color }}
          aria-hidden="true"
        >
          {info.label}
        </span>
      );
    }
  }

  return (
    <svg
      width="16"
      height="16"
      viewBox="0 0 16 16"
      fill="none"
      className="shrink-0 text-zinc-600"
      aria-hidden="true"
    >
      <path
        d="M4 1.5h5.5l3 3v9c0 .55-.45 1-1 1H4c-.55 0-1-.45-1-1v-11c0-.55.45-1 1-1Z"
        fill="currentColor"
        opacity="0.1"
      />
      <path
        d="M4 1.5h5.5l3 3v9c0 .55-.45 1-1 1H4c-.55 0-1-.45-1-1v-11c0-.55.45-1 1-1Z"
        stroke="currentColor"
        strokeWidth="1.2"
        strokeLinejoin="round"
      />
      <path d="M9.5 1.5v3h3" stroke="currentColor" strokeWidth="1.2" />
    </svg>
  );
}
