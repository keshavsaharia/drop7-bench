"use client";

import { useEffect, useRef, useState, type CSSProperties, type ReactNode } from "react";
import "./choice-tree.css";

interface Branch { id: string; content: ReactNode }

/** One parent and its alternatives. Wrapped rows share a trunk, never each other. */
export function ChoiceTree({ root, branches, label, columns = 4, compactColumns = 2 }: {
  root: ReactNode;
  branches: Branch[];
  label: string;
  columns?: number;
  compactColumns?: number;
}) {
  const host = useRef<HTMLDivElement>(null);
  const parentRef = useRef<HTMLDivElement>(null);
  const branchesRef = useRef<HTMLOListElement>(null);
  const [paths, setPaths] = useState<string[]>([]);
  const [active, setActive] = useState<number | null>(null);

  useEffect(() => {
    const tree = host.current!;
    const parent = parentRef.current!;
    const list = branchesRef.current!;
    const measure = () => {
      const bounds = tree.getBoundingClientRect();
      const origin = parent.getBoundingClientRect();
      const children = Array.from(list.children, (child) => child.getBoundingClientRect());
      const x = origin.left + origin.width / 2 - bounds.left;
      const y = origin.bottom - bounds.top;
      // Read the actual responsive gutter; no assumed card widths or row heights.
      const gutter = parseFloat(getComputedStyle(list).rowGap);
      const trunk = list.getBoundingClientRect().left - bounds.left;
      const fork = children[0]?.top - bounds.top - gutter / 2;
      const next = children.map((child) => {
        const targetX = child.left + child.width / 2 - bounds.left;
        const targetY = child.top - bounds.top;
        const rail = targetY - gutter / 2;
        // The first row fans out directly. Later rows return to the same parent
        // through the outside trunk, not through any previous move's board.
        return rail === fork
          ? `M ${x} ${y} V ${fork} H ${targetX} V ${targetY}`
          : `M ${x} ${y} V ${fork} H ${trunk} V ${rail} H ${targetX} V ${targetY}`;
      });
      setPaths((previous) => previous.join() === next.join() ? previous : next);
    };
    const observer = new ResizeObserver(measure);
    [tree, parent, list, ...list.children].forEach((node) => observer.observe(node));
    return () => observer.disconnect();
  }, [branches.length, columns, compactColumns]);

  return (
    <div ref={host} className="d7-choice-tree" style={{ "--d7-branch-columns": columns, "--d7-compact-columns": compactColumns } as CSSProperties}>
      <svg className="d7-choice-connections" aria-hidden="true">
        {paths.map((path, index) => <path key={index} d={path} data-active={active === index} />)}
        {active !== null && paths[active] && <path d={paths[active]} data-active="true" />}
      </svg>
      <div ref={parentRef} className="d7-choice-root">{root}</div>
      <div className="d7-choice-label"><span>{label}</span></div>
      <ol ref={branchesRef} className="d7-choice-branches" aria-label={label}>
        {branches.map((branch, index) => <li key={branch.id} onPointerEnter={() => setActive(index)} onPointerLeave={() => setActive(null)} onFocus={() => setActive(index)} onBlur={() => setActive(null)}>{branch.content}</li>)}
      </ol>
    </div>
  );
}
