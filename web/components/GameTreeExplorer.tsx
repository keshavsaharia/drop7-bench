"use client";

/**
 * Interactive expectimax game tree on React Flow.
 *
 * Root = the position (a MAX node). One node per legal column = the choice
 * (its value is the exact expectation over every chance outcome of that
 * drop). Below the expanded choice: the most probable chance outcomes, each a
 * real board with the leaf's opinion of it. Clicking an outcome plays the
 * engine's own animation frames for that transition on the root board and
 * descends into it, so a reader can walk a game through the tree it is
 * searching.
 *
 * Every number comes from the browser solver (web/lib/play/game-tree.ts):
 * a demonstration of the mechanics, never research evidence.
 */

import "@xyflow/react/dist/style.css";
import {
  Background,
  Controls,
  Handle,
  Position,
  ReactFlow,
  ReactFlowProvider,
  useReactFlow,
  type Edge,
  type Node,
  type NodeProps,
} from "@xyflow/react";
import { useCallback, useEffect, useMemo, useRef, useState, type CSSProperties } from "react";
import { BOARD_SIZE, serializeBoard, type GameState, type MoveAnimationFrame } from "../../src/core/typescript/engine.ts";
import { formatProbability, realizeTransition, type GameTree, type TreeChoice, type TreeOutcome } from "../lib/play/game-tree.ts";
import type { GameTreeRequest, GameTreeResponse } from "../lib/play/game-tree.protocol.ts";
import { Drop7Board } from "./Drop7Board";
import styles from "./Drop7Game.module.css";

// ---------------------------------------------------------------------------
// Node data
// ---------------------------------------------------------------------------

interface StateNodeData extends Record<string, unknown> {
  cells: string;
  nextDisc: number;
  title: string;
  subtitle: string;
  role: "root" | "outcome";
  value?: number;
  gameOver: boolean;
  selected: boolean;
  animationKind?: MoveAnimationFrame["kind"];
  animatedIndexes?: readonly number[];
  clickable: boolean;
}

interface ChoiceNodeData extends Record<string, unknown> {
  column: number;
  disc: number;
  value: number;
  expectedScore: number;
  best: boolean;
  expanded: boolean;
  mergedOutcomes: number;
  streamedOutcomes: number;
  unlisted: boolean;
  depthUsed: number;
  leafDepth: number;
}

interface MoreNodeData extends Record<string, unknown> {
  text: string;
}

type TreeNode = Node<StateNodeData, "state"> | Node<ChoiceNodeData, "choice"> | Node<MoreNodeData, "more">;

const NUMBER = new Intl.NumberFormat("en-US", { maximumFractionDigits: 0 });
const FRAME_MS: Record<MoveAnimationFrame["kind"], number> = { drop: 260, burst: 110, impact: 90, settle: 150, rise: 200 };
const PRE_DROP_HOLD_MS = 120;

function formatValue(value: number): string {
  if (!Number.isFinite(value)) return "—";
  return (value > 0 ? "+" : "") + NUMBER.format(Math.round(value));
}

// ---------------------------------------------------------------------------
// Node components (module scope, as React Flow and the compiler rules require)
// ---------------------------------------------------------------------------

function StateNode({ data }: NodeProps<Node<StateNodeData, "state">>) {
  const animated = new Set(data.animatedIndexes ?? []);
  const kind = data.animationKind;
  const cellMotion = (index: number) => (kind && animated.has(index) ? styles[kind] : undefined);
  const cellStyle = (index: number) => ({ "--drop7-rows": Math.floor(index / BOARD_SIZE) + 1, "--drop7-motion-duration": `${kind ? FRAME_MS[kind] : 0}ms` }) as CSSProperties;
  return (
    <div className={`tree-node tree-node--${data.role}${data.selected ? " tree-node--selected" : ""}${data.clickable ? " tree-node--clickable" : ""}`}>
      {data.role === "outcome" ? <Handle type="target" position={Position.Top} className="tree-handle" /> : null}
      <div className="tree-node__title">{data.title}</div>
      <Drop7Board cells={data.cells} nextDisc={data.nextDisc} size={data.role === "root" ? 132 : 96} cellClassName={cellMotion} cellStyle={cellStyle} label={data.title} />
      <div className="tree-node__subtitle">{data.subtitle}</div>
      {data.value !== undefined ? <div className={`tree-node__value${data.gameOver ? " tree-node__value--over" : ""}`}>{data.gameOver ? "game over" : `leaf ${formatValue(data.value)}`}</div> : null}
      {data.role === "root" ? <Handle type="source" position={Position.Bottom} className="tree-handle" /> : null}
    </div>
  );
}

function ChoiceNode({ data }: NodeProps<Node<ChoiceNodeData, "choice">>) {
  const depthNote = data.leafDepth > 0 ? (data.depthUsed === data.leafDepth ? `+${data.depthUsed} ply` : "leaf only (heavy)") : "leaf";
  return (
    <div className={`tree-node tree-node--choice${data.best ? " tree-node--best" : ""}${data.expanded ? " tree-node--expanded" : ""}`}>
      <Handle type="target" position={Position.Top} className="tree-handle" />
      <div className="tree-node__title">column {data.column}</div>
      <div className="tree-node__big">{formatValue(data.value)}</div>
      <div className="tree-node__subtitle">expected value · {depthNote}</div>
      <div className="tree-node__meta">
        score now {formatValue(data.expectedScore)} · {data.unlisted ? `${NUMBER.format(data.streamedOutcomes)} outcomes, too many to list` : `${NUMBER.format(data.mergedOutcomes)} outcome${data.mergedOutcomes === 1 ? "" : "s"}`}
      </div>
      {data.best ? <div className="tree-node__tag">MAX picks this</div> : null}
      <Handle type="source" position={Position.Bottom} className="tree-handle" />
    </div>
  );
}

function MoreNode({ data }: NodeProps<Node<MoreNodeData, "more">>) {
  return (
    <div className="tree-node tree-node--more">
      <Handle type="target" position={Position.Top} className="tree-handle" />
      {data.text}
    </div>
  );
}

const nodeTypes = { state: StateNode, choice: ChoiceNode, more: MoreNode };

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

const CHOICE_Y = 330;
const OUTCOME_Y = 620;
const CHOICE_GAP = 196;
const OUTCOME_GAP = 156;

interface Presentation {
  board: string;
  kind?: MoveAnimationFrame["kind"];
  indexes?: readonly number[];
}

function layout(tree: GameTree, expandedColumn: number | null, presentation: Presentation | null, animating: boolean): { nodes: TreeNode[]; edges: Edge[] } {
  const nodes: TreeNode[] = [];
  const edges: Edge[] = [];
  const root = tree.root;
  nodes.push({
    id: "root", type: "state", position: { x: -66, y: 0 }, draggable: false, selectable: false,
    data: {
      cells: presentation?.board ?? serializeBoard(root.board), nextDisc: root.nextDisc,
      title: "position (MAX node)", subtitle: `next disc ${root.nextDisc} · ${root.movesRemaining} drop${root.movesRemaining === 1 ? "" : "s"} until the rise`,
      role: "root", gameOver: root.gameOver, selected: false, animationKind: presentation?.kind, animatedIndexes: presentation?.indexes, clickable: false,
    },
  });
  const legal = tree.choices.filter((choice) => choice.legal);
  legal.forEach((choice, index) => {
    const x = (index - (legal.length - 1) / 2) * CHOICE_GAP - 80;
    const expanded = expandedColumn === choice.column;
    nodes.push({
      id: `choice-${choice.column}`, type: "choice", position: { x, y: CHOICE_Y }, draggable: false, selectable: false,
      data: { column: choice.column, disc: root.nextDisc, value: choice.value, expectedScore: choice.expectedScore, best: tree.bestColumn === choice.column, expanded, mergedOutcomes: choice.mergedOutcomes, streamedOutcomes: choice.streamedOutcomes, unlisted: choice.unlisted, depthUsed: choice.depthUsed, leafDepth: tree.leafDepth },
    });
    edges.push({ id: `e-root-${choice.column}`, source: "root", target: `choice-${choice.column}`, label: `drop the ${root.nextDisc} in column ${choice.column}`, animated: tree.bestColumn === choice.column, className: tree.bestColumn === choice.column ? "tree-edge tree-edge--best" : "tree-edge", labelBgPadding: [4, 2], labelBgBorderRadius: 4 });
    if (!expanded) return;
    const listed = choice.outcomes;
    const slots = listed.length + (choice.hiddenProbability > 0 ? 1 : 0);
    listed.forEach((outcome, slot) => {
      const ox = x + 80 - 48 + (slot - (slots - 1) / 2) * OUTCOME_GAP;
      nodes.push({
        id: `outcome-${outcome.id}`, type: "state", position: { x: ox, y: OUTCOME_Y }, draggable: false, selectable: false,
        data: {
          cells: serializeBoard(outcome.state.board), nextDisc: outcome.state.nextDisc,
          title: `p = ${formatProbability(outcome.probability)}`,
          subtitle: `${outcome.scoreDelta > 0 ? `+${NUMBER.format(outcome.scoreDelta)} points` : "no points"}${outcome.revealedCells > 0 ? ` · ${outcome.revealedCells} reveal${outcome.revealedCells === 1 ? "" : "s"}` : ""}`,
          role: "outcome", value: outcome.leafValue, gameOver: outcome.state.gameOver, selected: false, clickable: !animating,
        },
      });
      edges.push({ id: `e-${outcome.id}`, source: `choice-${choice.column}`, target: `outcome-${outcome.id}`, label: formatProbability(outcome.probability), className: "tree-edge tree-edge--chance", labelBgPadding: [4, 2], labelBgBorderRadius: 4 });
    });
    if (choice.hiddenProbability > 0) {
      const ox = x + 80 - 48 + (listed.length - (slots - 1) / 2) * OUTCOME_GAP;
      nodes.push({ id: `more-${choice.column}`, type: "more", position: { x: ox, y: OUTCOME_Y + 40 }, draggable: false, selectable: false, data: { text: choice.unlisted ? `${NUMBER.format(choice.streamedOutcomes)} exact outcomes were averaged; too many to draw` : `${NUMBER.format(choice.mergedOutcomes - listed.length)} more outcomes, together p = ${formatProbability(choice.hiddenProbability)}` } });
      edges.push({ id: `e-more-${choice.column}`, source: `choice-${choice.column}`, target: `more-${choice.column}`, className: "tree-edge tree-edge--chance tree-edge--more" });
    }
  });
  return { nodes, edges };
}

function FitOnChange({ layoutKey }: { layoutKey: string }) {
  const { fitView } = useReactFlow();
  useEffect(() => {
    const timer = window.setTimeout(() => { void fitView({ padding: 0.12, duration: 320 }); }, 30);
    return () => window.clearTimeout(timer);
  }, [fitView, layoutKey]);
  return null;
}

function wait(milliseconds: number, signal: AbortSignal) {
  if (signal.aborted) return Promise.resolve(false);
  return new Promise<boolean>((resolve) => {
    const timer = window.setTimeout(() => { signal.removeEventListener("abort", cancel); resolve(true); }, milliseconds);
    const cancel = () => { window.clearTimeout(timer); signal.removeEventListener("abort", cancel); resolve(false); };
    signal.addEventListener("abort", cancel, { once: true });
  });
}

function hexSeed(seed: number) {
  return `0x${(seed >>> 0).toString(16).padStart(8, "0")}`;
}

function parseSeed(text: string): number | null {
  const trimmed = text.trim();
  if (!/^(0x)?[0-9a-fA-F]{1,8}$/.test(trimmed)) return null;
  return Number.parseInt(trimmed, 16) >>> 0;
}

// ---------------------------------------------------------------------------
// The explorer
// ---------------------------------------------------------------------------

export interface GameTreeExplorerProps {
  seed: number;
  moves: number;
  leafDepth?: 0 | 1 | 2;
  maxOutcomes?: number;
  height?: number;
  controls?: boolean;
}

export function GameTreeExplorer({ seed: initialSeed, moves: initialMoves, leafDepth: initialLeafDepth = 0, maxOutcomes = 7, height = 640, controls = true }: GameTreeExplorerProps) {
  const [seed, setSeed] = useState(initialSeed >>> 0);
  const [seedText, setSeedText] = useState(hexSeed(initialSeed));
  const [moves, setMoves] = useState(Math.max(0, Math.min(60, Math.trunc(initialMoves))));
  const [leafDepth, setLeafDepth] = useState<0 | 1 | 2>(initialLeafDepth);
  const [rootOverride, setRootOverride] = useState<GameState | null>(null);
  const [history, setHistory] = useState<GameState[]>([]);
  const [tree, setTree] = useState<GameTree | null>(null);
  const [pendingRequest, setPendingRequest] = useState(0);
  const [error, setError] = useState<string | null>(null);
  const [elapsedMs, setElapsedMs] = useState<number | null>(null);
  const [expandedColumn, setExpandedColumn] = useState<number | null>(null);
  const [presentation, setPresentation] = useState<Presentation | null>(null);
  const [animating, setAnimating] = useState(false);
  const [note, setNote] = useState<string | null>(null);
  const workerRef = useRef<Worker | null>(null);
  const requestRef = useRef(0);
  const animationRef = useRef<AbortController | null>(null);

  // The worker is created once; requests are posted whenever the inputs change.
  useEffect(() => {
    const worker = new Worker(new URL("../lib/play/game-tree.worker.ts", import.meta.url), { type: "module", name: "drop7-game-tree" });
    worker.addEventListener("message", (event: MessageEvent<GameTreeResponse>) => {
      const response = event.data;
      if (response.requestId !== requestRef.current) return;
      if (response.type === "tree") {
        setTree(response.tree);
        setElapsedMs(response.elapsedMs);
        setError(null);
      } else {
        setError(response.message);
      }
      setPendingRequest(0);
    });
    workerRef.current = worker;
    return () => { worker.terminate(); workerRef.current = null; };
  }, []);

  useEffect(() => {
    const worker = workerRef.current;
    if (!worker) return;
    requestRef.current += 1;
    const request: GameTreeRequest = { requestId: requestRef.current, seed, moves, leafDepth, maxOutcomes, root: rootOverride ?? undefined };
    const timer = window.setTimeout(() => { worker.postMessage(request); }, 0);
    return () => window.clearTimeout(timer);
  }, [seed, moves, leafDepth, maxOutcomes, rootOverride]);

  const shownColumn = expandedColumn ?? tree?.bestColumn ?? null;
  const graph = useMemo(() => (tree ? layout(tree, shownColumn, presentation, animating) : { nodes: [], edges: [] }), [tree, shownColumn, presentation, animating]);
  const layoutKey = tree ? `${serializeBoard(tree.root.board)}:${shownColumn}:${tree.leafDepth}` : "empty";

  const requestTree = useCallback(() => { setTree(null); setPendingRequest((n) => n + 1); setExpandedColumn(null); }, []);

  const descend = useCallback(async (choice: TreeChoice, outcome: TreeOutcome) => {
    if (!tree || animating) return;
    animationRef.current?.abort();
    const controller = new AbortController();
    animationRef.current = controller;
    setAnimating(true);
    const reduced = typeof window !== "undefined" && window.matchMedia?.("(prefers-reduced-motion: reduce)").matches;
    const realized = realizeTransition(tree.root, choice.column, outcome);
    setNote(realized.matched ? `engine transition for column ${choice.column} (${realized.attempts} realisation${realized.attempts === 1 ? "" : "s"} tried)` : "the engine's random realisation differed from the listed outcome; showing the listed board");
    if (!reduced && realized.frames.length > 0) {
      setPresentation({ board: serializeBoard(tree.root.board) });
      if (await wait(PRE_DROP_HOLD_MS, controller.signal)) {
        for (const frame of realized.frames) {
          if (controller.signal.aborted) break;
          setPresentation({ board: serializeBoard(frame.board), kind: frame.kind, indexes: frame.indexes });
          if (!(await wait(FRAME_MS[frame.kind], controller.signal))) break;
        }
      }
    }
    if (controller.signal.aborted) return;
    setPresentation(null);
    setAnimating(false);
    setHistory((previous) => [...previous, tree.root]);
    setRootOverride(realized.matched ? realized.state : outcome.state);
    setTree(null);
    setExpandedColumn(null);
  }, [tree, animating]);

  const onNodeClick = useCallback((_event: unknown, node: Node) => {
    if (!tree) return;
    if (node.id.startsWith("choice-")) {
      setExpandedColumn(Number(node.id.slice("choice-".length)));
      return;
    }
    if (node.id.startsWith("outcome-")) {
      const [column, index] = node.id.slice("outcome-".length).split(":").map(Number);
      const choice = tree.choices[column];
      const outcome = choice?.outcomes[index];
      if (choice && outcome) void descend(choice, outcome);
    }
  }, [tree, descend]);

  const back = () => {
    animationRef.current?.abort();
    setAnimating(false);
    setPresentation(null);
    setHistory((previous) => {
      const next = previous.slice(0, -1);
      const last = previous[previous.length - 1];
      setRootOverride(previous.length > 1 ? last : null);
      return next;
    });
    setTree(null);
    setExpandedColumn(null);
  };

  const reset = () => {
    animationRef.current?.abort();
    setAnimating(false);
    setPresentation(null);
    setHistory([]);
    setRootOverride(null);
    setNote(null);
    requestTree();
  };

  const applySeed = () => {
    const parsed = parseSeed(seedText);
    if (parsed === null) return;
    setSeed(parsed);
    setHistory([]);
    setRootOverride(null);
    setNote(null);
    requestTree();
  };

  const randomSeed = () => {
    const words = new Uint32Array(1);
    crypto.getRandomValues(words);
    const next = words[0] >>> 0;
    setSeed(next);
    setSeedText(hexSeed(next));
    setHistory([]);
    setRootOverride(null);
    setNote(null);
    requestTree();
  };

  const busy = !tree || pendingRequest > 0;
  const status = error
    ? `could not build the tree: ${error}`
    : busy
      ? "building the tree in the browser…"
      : `${tree.choices.filter((c) => c.legal).length} legal columns · ${NUMBER.format(tree.choices.reduce((s, c) => s + c.streamedOutcomes, 0))} exact chance outcomes averaged${elapsedMs !== null ? ` in ${elapsedMs} ms` : ""}${history.length ? ` · ${history.length} move${history.length === 1 ? "" : "s"} below the seeded position` : ""}`;

  return (
    <div className="tree-explorer">
      {controls ? (
        <div className="tree-controls">
          <label className="tree-control">
            <span>seed</span>
            <input className="tree-input" value={seedText} onChange={(event) => setSeedText(event.target.value)} onKeyDown={(event) => { if (event.key === "Enter") applySeed(); }} spellCheck={false} aria-label="game seed (hex)" />
          </label>
          <button type="button" className="tree-button" onClick={applySeed}>load</button>
          <button type="button" className="tree-button" onClick={randomSeed}>random</button>
          <label className="tree-control">
            <span>moves into the game</span>
            <input type="range" min={0} max={60} value={moves} onChange={(event) => { setMoves(Number(event.target.value)); setHistory([]); setRootOverride(null); requestTree(); }} aria-label="moves played before the root" />
            <span className="tree-control__value">{moves}</span>
          </label>
          <label className="tree-control">
            <span>below each outcome</span>
            <select className="tree-input" value={leafDepth} onChange={(event) => { setLeafDepth(Number(event.target.value) as 0 | 1 | 2); requestTree(); }} aria-label="search depth below each outcome">
              <option value={0}>leaf only</option>
              <option value={1}>+1 ply</option>
              <option value={2}>+2 plies</option>
            </select>
          </label>
          <button type="button" className="tree-button" onClick={back} disabled={history.length === 0 || animating}>back</button>
          <button type="button" className="tree-button" onClick={reset} disabled={animating}>reset</button>
        </div>
      ) : null}
      <div className="tree-canvas" style={{ height }}>
        <ReactFlowProvider>
          <ReactFlow nodes={graph.nodes} edges={graph.edges} nodeTypes={nodeTypes} onNodeClick={onNodeClick} nodesDraggable={false} nodesConnectable={false} elementsSelectable={false} zoomOnDoubleClick={false} minZoom={0.15} maxZoom={2} fitView proOptions={{ hideAttribution: true }} colorMode="dark">
            <Background gap={24} size={1} color="#27272a" />
            <Controls showInteractive={false} position="bottom-right" />
            <FitOnChange layoutKey={layoutKey} />
          </ReactFlow>
        </ReactFlowProvider>
        {busy ? <div className="tree-overlay">building the tree…</div> : null}
      </div>
      <div className="tree-status" aria-live="polite">
        <span>{status}</span>
        {note ? <span className="tree-status__note"> · {note}</span> : null}
      </div>
      <div className="tree-legend">
        <span><b>MAX node</b> the position; the search picks the column with the highest expected value.</span>
        <span><b>chance branches</b> every exact outcome of a drop — the next disc, and what any cracked gray disc turns out to be — weighted by its probability.</span>
        <span><b>leaf</b> the opinion of the board scorer where the look-ahead stops. Click a column to expand it; click an outcome to play that transition and continue from it.</span>
        <span className="tree-legend__muted">Columns are numbered 0–6 from the left. Values come from the browser solver and are a demonstration, never research evidence; seed {hexSeed(seed)} is the same game as <a href={`/play?seed=${hexSeed(seed)}`}>/play</a>.</span>
      </div>
    </div>
  );
}
