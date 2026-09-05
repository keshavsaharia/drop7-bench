/**
 * Turn the resting frame of registered card art into an SVG data URL for a
 * Satori social image. The card-art contract makes the SVG's own attributes
 * the final frame, so this always follows the latest checked-in animation
 * without a generated image that can go stale.
 */
import {
  Fragment,
  isValidElement,
  type CSSProperties,
  type ReactElement,
  type ReactNode,
} from "react";

/** Literal copies of the design tokens used by card art. */
const SOCIAL_ART_TOKENS: Readonly<Record<string, string>> = {
  "--color-bg": "#0a0a0c",
  "--color-surface": "#111114",
  "--color-raised": "#18181c",
  "--color-hover": "#1f1f24",
  "--color-cell": "#0d0d10",
  "--color-ink": "#f4f4f5",
  "--color-ink-1": "#d4d4d8",
  "--color-ink-2": "#a1a1aa",
  "--color-ink-3": "#7d7d86",
  "--color-ink-4": "#52525b",
  "--color-rule": "#26262b",
  "--color-rule-strong": "#3a3a41",
  "--color-accent": "#8fb0ff",
  "--color-accent-strong": "#405db0",
  "--color-accent-soft": "rgba(64,93,176,0.18)",
  "--color-accent-fg": "#ffffff",
  "--color-disc-1": "#218a57",
  "--color-disc-1-fg": "#ffffff",
  "--color-disc-2": "#d7b33f",
  "--color-disc-2-fg": "#17130a",
  "--color-disc-3": "#d7742e",
  "--color-disc-3-fg": "#ffffff",
  "--color-disc-4": "#c4443e",
  "--color-disc-4-fg": "#ffffff",
  "--color-disc-5": "#9e4c8b",
  "--color-disc-5-fg": "#ffffff",
  "--color-disc-6": "#238391",
  "--color-disc-6-fg": "#ffffff",
  "--color-disc-7": "#405db0",
  "--color-disc-7-fg": "#ffffff",
  "--color-disc-gray": "#aeb2af",
  "--color-disc-gray-core": "#111412",
  "--color-status-completed": "#6fd39f",
  "--color-reads-public": "#5fc4d3",
  "--color-reads-oracle": "#f0a06a",
  "--color-reads-teacher": "#f0a06a",
  "--color-series-1": "#3987e5",
  "--color-series-2": "#d95926",
  "--color-series-3": "#199e70",
  "--color-series-4": "#c98500",
  "--color-series-5": "#d55181",
  "--color-series-6": "#008300",
  "--color-series-7": "#9085e9",
  "--color-series-8": "#e66767",
  "--color-highlight": "#e6c25a",
  "--color-danger": "#e66767",
  "--font-mono": "Courier New, monospace",
  "--font-sans": "Arial, Helvetica, sans-serif",
};

const CASE_SENSITIVE_ATTRIBUTES = new Set([
  "attributeName",
  "baseFrequency",
  "calcMode",
  "keyPoints",
  "keySplines",
  "keyTimes",
  "lengthAdjust",
  "pathLength",
  "preserveAlpha",
  "preserveAspectRatio",
  "repeatCount",
  "repeatDur",
  "requiredExtensions",
  "requiredFeatures",
  "specularConstant",
  "specularExponent",
  "spreadMethod",
  "startOffset",
  "stdDeviation",
  "stitchTiles",
  "surfaceScale",
  "systemLanguage",
  "tableValues",
  "targetX",
  "targetY",
  "textLength",
  "viewBox",
  "viewTarget",
  "xChannelSelector",
  "yChannelSelector",
  "zoomAndPan",
]);

function escapeText(value: string): string {
  return value.replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;");
}

function escapeAttribute(value: string): string {
  return escapeText(value).replaceAll('"', "&quot;");
}

function resolveTokens(value: string): string {
  return value.replace(
    /var\((--[a-z0-9-]+)(?:,\s*([^)]+))?\)/gi,
    (_match, token: string, fallback: string | undefined) => {
      const resolved = SOCIAL_ART_TOKENS[token];
      if (resolved !== undefined) return resolved;
      if (fallback !== undefined) return fallback.trim();
      throw new Error(`Social card art uses an unmapped design token: ${token}`);
    },
  );
}

function attributeName(name: string): string {
  if (name === "className") return "class";
  if (CASE_SENSITIVE_ATTRIBUTES.has(name) || name.startsWith("aria-") || name.startsWith("data-")) {
    return name;
  }
  return name.replace(/[A-Z]/g, (letter) => `-${letter.toLowerCase()}`);
}

function styleValue(style: CSSProperties): string {
  return Object.entries(style)
    .filter(([, value]) => value !== null && value !== undefined)
    .map(([name, value]) => `${attributeName(name)}:${resolveTokens(String(value))}`)
    .join(";");
}

function renderComponent(element: ReactElement<Record<string, unknown>>): ReactNode {
  const Component = element.type;
  if (typeof Component !== "function") return null;
  const rendered = (Component as (props: Record<string, unknown>) => ReactNode)(element.props);
  if (rendered && typeof rendered === "object" && "then" in rendered) {
    throw new Error("Social card art components must render synchronously");
  }
  return rendered as ReactNode;
}

function serialize(node: ReactNode): string {
  if (node === null || node === undefined || typeof node === "boolean") return "";
  if (typeof node === "string" || typeof node === "number" || typeof node === "bigint") {
    return escapeText(String(node));
  }
  if (Array.isArray(node)) return node.map(serialize).join("");
  if (!isValidElement<Record<string, unknown>>(node)) return "";

  if (node.type === Fragment) return serialize(node.props.children as ReactNode);
  if (typeof node.type === "function") return serialize(renderComponent(node));
  if (typeof node.type !== "string") {
    throw new Error("Social card art contains an unsupported React element type");
  }

  const attributes: string[] = [];
  for (const [name, raw] of Object.entries(node.props)) {
    if (name === "children" || name === "className" || name === "dangerouslySetInnerHTML") continue;
    if (name.startsWith("data-") || name === "key" || raw === false || raw === null || raw === undefined) continue;
    const value = name === "style" && typeof raw === "object"
      ? styleValue(raw as CSSProperties)
      : resolveTokens(String(raw));
    attributes.push(`${attributeName(name)}="${escapeAttribute(value)}"`);
  }
  if (node.type === "svg" && !("xmlns" in node.props)) {
    attributes.unshift('xmlns="http://www.w3.org/2000/svg"');
  }

  return `<${node.type}${attributes.length > 0 ? ` ${attributes.join(" ")}` : ""}>${serialize(
    node.props.children as ReactNode,
  )}</${node.type}>`;
}

/**
 * Serialize the checked-in final frame of an art. Class names and animation
 * data attributes are deliberately removed, leaving only the SVG's resting
 * attributes and the final-only group visible.
 */
export function socialArtSvg(art: ReactNode): string {
  const svg = serialize(art);
  if (!svg.startsWith("<svg ")) {
    throw new Error("Social card art must resolve to one SVG root");
  }
  return svg;
}

/** A self-contained image source accepted by `next/og`. */
export function socialArtDataUrl(art: ReactNode): string {
  return `data:image/svg+xml;base64,${Buffer.from(socialArtSvg(art), "utf8").toString("base64")}`;
}
