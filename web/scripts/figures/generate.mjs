#!/usr/bin/env node
/**
 * Renders one figure spec (web/content/figures/<name>.json) to a self-contained
 * SVG next to it (web/content/figures/<name>.svg).
 *
 *   node web/scripts/figures/generate.mjs web/content/figures/score-vs-depth.json
 *
 * The generator draws exactly the numbers in the spec. It computes axis ranges
 * and pixel positions, nothing else: no means, no fits, no interpolation. Every
 * point must name the research record or finding document it was copied from
 * (`sourceRecord`), or the generator refuses to render it. Output is
 * deterministic: no dates, no random ids.
 *
 * Kinds:
 *   line  numeric x; one polyline per series; optional lo/hi whiskers
 *   bar   categorical x; one bar per point around a zero line; lo/hi whiskers
 *   dot   categorical x; one marker per point, series side by side; lo/hi whiskers
 */
import { readFileSync, writeFileSync } from "node:fs";
import { basename, dirname, join } from "node:path";

const W = 720;
let H = 400; // grows when the legend needs more than one row
const LEGEND_ROW = 16;
const PAD = { top: 44, right: 24, bottom: 84, left: 84 };
const CHAR = 0.6; // heuristic text width, em per character
const TITLE_CHARS = Math.floor((W - PAD.left - PAD.right) / (14 * CHAR));
const PALETTE = ["#3987e5", "#f59e0b", "#22c55e", "#e879f9", "#f87171", "#a3e635"];
const SOURCE_ID = /^(RS|RUN|EX|TH)-[A-Za-z0-9-]+$|^docs\/.+\.md$|^web\/content\/log\/\d{4}-\d{2}-\d{2}\.mdx$/;

export function loadSpec(path) {
  const spec = JSON.parse(readFileSync(path, "utf8"));
  validate(spec, path);
  return spec;
}

function validate(spec, path) {
  const fail = (msg) => {
    throw new Error(`${path}: ${msg}`);
  };
  if (!spec.title) fail("missing title");
  if (!["line", "bar", "dot"].includes(spec.kind)) fail(`unknown kind ${spec.kind}`);
  if (!Array.isArray(spec.series) || spec.series.length === 0) fail("series[] is empty");
  for (const s of spec.series) {
    if (!s.name) fail("every series needs a name");
    if (!Array.isArray(s.points) || s.points.length === 0) fail(`series ${s.name} has no points`);
    for (const p of s.points) {
      const where = `series ${s.name}, point x=${p.x}`;
      if (typeof p.sourceRecord !== "string" || !SOURCE_ID.test(p.sourceRecord)) {
        fail(`${where}: sourceRecord must be a research record ID (RS-/RUN-/EX-/TH-) or a docs/*.md path; refusing to render a point without provenance`);
      }
      if (typeof p.y !== "number" || !Number.isFinite(p.y)) fail(`${where}: y must be a finite number`);
      if (spec.kind === "line" && typeof p.x !== "number") fail(`${where}: line charts need numeric x`);
      if (spec.kind !== "line" && typeof p.x !== "string") fail(`${where}: ${spec.kind} charts need a string x category`);
      for (const k of ["lo", "hi", "n"]) {
        if (p[k] !== undefined && (typeof p[k] !== "number" || !Number.isFinite(p[k]))) fail(`${where}: ${k} must be a number`);
      }
    }
  }
}

/* ---------------------------------------------------------------- helpers */

const esc = (s) => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
const fmt = (v, unit) => {
  const abs = Math.abs(v);
  const text = abs >= 1000 ? v.toLocaleString("en-US", { maximumFractionDigits: 0 }) : v.toLocaleString("en-US", { maximumFractionDigits: 2 });
  return unit ? `${text} ${unit}` : text;
};
const signed = (v, unit) => (v > 0 ? `+${fmt(v, unit)}` : fmt(v, unit));
const round2 = (v) => Math.round(v * 100) / 100;

/** "Nice" tick steps for a numeric range. */
function ticks(min, max, count = 5) {
  if (min === max) {
    min -= 1;
    max += 1;
  }
  const raw = (max - min) / count;
  const mag = 10 ** Math.floor(Math.log10(raw));
  const step = [1, 2, 2.5, 5, 10].map((m) => m * mag).find((s) => s >= raw);
  const lo = Math.floor(min / step) * step;
  const hi = Math.ceil(max / step) * step;
  const out = [];
  for (let v = lo; v <= hi + step / 2; v += step) out.push(round2(v));
  return { lo, hi, values: out };
}

function axisLabel(axis) {
  if (!axis) return "";
  return axis.unit ? `${axis.label} (${axis.unit})` : axis.label;
}

/** Greedy word wrap to at most `max` characters per line. */
function wrap(text, max) {
  const lines = [];
  let line = "";
  for (const word of String(text).split(" ")) {
    if (line && (line + " " + word).length > max) {
      lines.push(line);
      line = word;
    } else line = line ? `${line} ${word}` : word;
  }
  if (line) lines.push(line);
  return lines;
}

const POP_CHARS = 64;

/** Popover lines for one point: heading, label, value, bounds, n, source. */
function popLines(spec, series, p) {
  const yUnit = spec.y?.unit;
  const heading = spec.kind === "line" ? `${series.name}, ${spec.x?.label ?? "x"} ${p.x}` : `${series.name} — ${p.x}`;
  const lines = [heading];
  if (p.label) lines.push(...wrap(p.label, POP_CHARS));
  lines.push(`${spec.y?.label ?? "value"}: ${spec.kind === "bar" ? signed(p.y, yUnit) : fmt(p.y, yUnit)}`);
  if (p.lo !== undefined && p.hi !== undefined) lines.push(`bounds: ${fmt(p.lo, yUnit)} to ${fmt(p.hi, yUnit)}`);
  else if (p.lo !== undefined) lines.push(`95% lower bound: ${fmt(p.lo, yUnit)}`);
  else if (p.hi !== undefined) lines.push(`95% upper bound: ${fmt(p.hi, yUnit)}`);
  if (p.n !== undefined) lines.push(`n = ${fmt(p.n)} games`);
  lines.push(...wrap(`source: ${p.sourceRecord}${p.sourceField ? ` · ${p.sourceField}` : ""}`, POP_CHARS));
  return lines;
}

/** A hidden popover group anchored at (cx, cy), clamped inside the viewBox. */
function popover(lines, cx, cy) {
  const fs = 11;
  const lh = 15;
  const padX = 8;
  const width = Math.max(...lines.map((l) => l.length)) * fs * CHAR + padX * 2;
  const height = lines.length * lh + 8;
  let x = cx + 10;
  let y = cy - height - 8;
  if (x + width > W - 4) x = cx - width - 10;
  if (x < 4) x = 4;
  if (y < 4) y = cy + 12;
  if (y + height > H - 4) y = H - 4 - height;
  const text = lines
    .map((l, i) => `<text x="${x + padX}" y="${y + 4 + (i + 1) * lh - 4}" font-size="${fs}"${i === 0 ? ' font-weight="600"' : ""}>${esc(l)}</text>`)
    .join("");
  return `<g class="fig-pop"><rect x="${x}" y="${y}" width="${round2(width)}" height="${height}" rx="4"/>${text}</g>`;
}

function pointGroup(spec, series, p, cx, cy, marker) {
  const lines = popLines(spec, series, p);
  return `<g class="fig-pt" tabindex="0"><title>${esc(lines.join(" | "))}</title>${marker}${popover(lines, cx, cy)}</g>`;
}

function whisker(x, yLo, yHi, color) {
  return `<g class="fig-whisker" stroke="${color}"><line x1="${x}" y1="${yLo}" x2="${x}" y2="${yHi}"/><line x1="${x - 4}" y1="${yLo}" x2="${x + 4}" y2="${yLo}"/><line x1="${x - 4}" y1="${yHi}" x2="${x + 4}" y2="${yHi}"/></g>`;
}

/* ---------------------------------------------------------------- layout */

function frame(spec, yTicks, yScale, xAxisSvg) {
  const x0 = PAD.left;
  const x1 = W - PAD.right;
  const grid = yTicks.values
    .map((v) => {
      const y = round2(yScale(v));
      return `<line class="fig-grid" x1="${x0}" y1="${y}" x2="${x1}" y2="${y}"/><text class="fig-tick" x="${x0 - 8}" y="${y + 4}" text-anchor="end">${esc(fmt(v))}</text>`;
    })
    .join("");
  const title = titleLines(spec.title)
    .map((l, i) => `<text class="fig-title" x="${x0}" y="${24 + i * 18}">${esc(l)}</text>`)
    .join("");
  return `${title}${grid}${xAxisSvg}<text class="fig-axis" transform="translate(16 ${(PAD.top + H - PAD.bottom) / 2}) rotate(-90)" text-anchor="middle">${esc(axisLabel(spec.y))}</text><text class="fig-axis" x="${(x0 + x1) / 2}" y="${H - PAD.bottom + 48}" text-anchor="middle">${esc(axisLabel(spec.x))}</text>`;
}

/** Legend items laid out left to right, wrapping into rows that fit the width. */
function legendRows(spec) {
  const rows = [[]];
  let x = PAD.left;
  spec.series.forEach((s, i) => {
    const width = 15 + s.name.length * 11 * CHAR + 18;
    if (x + width > W - PAD.right && rows[rows.length - 1].length > 0) {
      rows.push([]);
      x = PAD.left;
    }
    rows[rows.length - 1].push({ x, name: s.name, color: PALETTE[i % PALETTE.length] });
    x += width;
  });
  return rows;
}

/** Greedy word wrap of the title into lines that fit the plot width. */
function titleLines(title) {
  const lines = [];
  let line = "";
  for (const word of title.split(" ")) {
    if (line && (line + " " + word).length > TITLE_CHARS) {
      lines.push(line);
      line = word;
    } else line = line ? `${line} ${word}` : word;
  }
  if (line) lines.push(line);
  return lines;
}

function legend(spec) {
  const rows = legendRows(spec);
  const y0 = H - 12 - (rows.length - 1) * LEGEND_ROW;
  return rows
    .map((row, r) =>
      row
        .map((item) => {
          const y = y0 + r * LEGEND_ROW;
          return `<g class="fig-legend"><rect x="${item.x}" y="${y - 9}" width="10" height="10" rx="2" fill="${item.color}"/><text x="${item.x + 15}" y="${y}">${esc(item.name)}</text></g>`;
        })
        .join(""),
    )
    .join("");
}

function yRange(spec, includeZero) {
  const vals = [];
  for (const s of spec.series) for (const p of s.points) vals.push(p.y, p.lo ?? p.y, p.hi ?? p.y);
  if (includeZero) vals.push(0);
  return ticks(Math.min(...vals), Math.max(...vals));
}

function makeYScale(yTicks) {
  const top = PAD.top + 8;
  const bottom = H - PAD.bottom;
  return (v) => bottom - ((v - yTicks.lo) / (yTicks.hi - yTicks.lo)) * (bottom - top);
}

function renderLine(spec) {
  const yTicks = yRange(spec, false);
  const yScale = makeYScale(yTicks);
  const xs = spec.series.flatMap((s) => s.points.map((p) => p.x));
  const xMin = Math.min(...xs);
  const xMax = Math.max(...xs);
  const xScale = (v) => PAD.left + 30 + ((v - xMin) / Math.max(xMax - xMin, 1)) * (W - PAD.right - PAD.left - 60);
  const xVals = [...new Set(xs)].sort((a, b) => a - b);
  const xAxis = xVals.map((v) => `<text class="fig-tick" x="${round2(xScale(v))}" y="${H - PAD.bottom + 16}" text-anchor="middle">${esc(fmt(v))}</text>`).join("");
  let body = "";
  spec.series.forEach((s, i) => {
    const color = PALETTE[i % PALETTE.length];
    const pts = [...s.points].sort((a, b) => a.x - b.x);
    if (pts.length > 1) {
      const d = pts.map((p) => `${round2(xScale(p.x))},${round2(yScale(p.y))}`).join(" ");
      body += `<polyline class="fig-line" points="${d}" stroke="${color}"${s.dashed ? ' stroke-dasharray="5 4"' : ""}/>`;
    }
    for (const p of pts) {
      const cx = round2(xScale(p.x));
      const cy = round2(yScale(p.y));
      let marker = "";
      if (p.lo !== undefined || p.hi !== undefined) marker += whisker(cx, round2(yScale(p.lo ?? p.y)), round2(yScale(p.hi ?? p.y)), color);
      marker += `<circle class="fig-marker" cx="${cx}" cy="${cy}" r="5" fill="${color}"/>`;
      body += pointGroup(spec, s, p, cx, cy, marker);
    }
  });
  return frame(spec, yTicks, yScale, xAxis) + body + legend(spec);
}

function categories(spec) {
  const cats = [];
  for (const s of spec.series) for (const p of s.points) if (!cats.includes(p.x)) cats.push(p.x);
  return cats;
}

function renderCategorical(spec) {
  const isBar = spec.kind === "bar";
  const yTicks = yRange(spec, isBar);
  const yScale = makeYScale(yTicks);
  const cats = categories(spec);
  const slot = (W - PAD.right - PAD.left) / cats.length;
  const xAxis = cats
    .map((c, i) => {
      const cx = round2(PAD.left + slot * (i + 0.5));
      // Wrap long category labels onto two lines at the last space before the slot width.
      const maxChars = Math.max(6, Math.floor(slot / (11 * CHAR)));
      let first = c;
      let second = "";
      if (c.length > maxChars) {
        const cut = c.lastIndexOf(" ", maxChars);
        if (cut > 0) {
          first = c.slice(0, cut);
          second = c.slice(cut + 1);
        }
      }
      return `<text class="fig-tick" x="${cx}" y="${H - PAD.bottom + 16}" text-anchor="middle">${esc(first)}${second ? `<tspan x="${cx}" dy="13">${esc(second)}</tspan>` : ""}</text>`;
    })
    .join("");
  let body = "";
  if (isBar) {
    const y0 = round2(yScale(0));
    body += `<line class="fig-zero" x1="${PAD.left}" y1="${y0}" x2="${W - PAD.right}" y2="${y0}"/>`;
  }
  spec.series.forEach((s, si) => {
    const color = PALETTE[si % PALETTE.length];
    for (const p of s.points) {
      const ci = cats.indexOf(p.x);
      // Series that actually have a point in this category share its slot evenly.
      const present = spec.series.filter((t) => t.points.some((q) => q.x === p.x));
      const cx = round2(PAD.left + slot * ci + (slot / (present.length + 1)) * (present.indexOf(s) + 1));
      const cy = round2(yScale(p.y));
      let marker = "";
      if (isBar) {
        const bw = Math.min(36, (slot / (present.length + 1)) * 0.8);
        const y0 = round2(yScale(0));
        marker += `<rect class="fig-bar" x="${round2(cx - bw / 2)}" y="${Math.min(cy, y0)}" width="${round2(bw)}" height="${round2(Math.abs(y0 - cy))}" fill="${color}"/>`;
      }
      if (p.lo !== undefined || p.hi !== undefined) marker += whisker(cx, round2(yScale(p.lo ?? p.y)), round2(yScale(p.hi ?? p.y)), color);
      if (!isBar) marker += `<circle class="fig-marker" cx="${cx}" cy="${cy}" r="6" fill="${color}"/>`;
      else marker += `<circle class="fig-marker" cx="${cx}" cy="${cy}" r="3" fill="${color}"/>`;
      if (p.label && !isBar) marker += `<text class="fig-tick" x="${cx}" y="${cy - 12}" text-anchor="middle">${esc(p.label)}</text>`;
      body += pointGroup(spec, s, p, cx, cy, marker);
    }
  });
  return frame(spec, yTicks, yScale, xAxis) + body + legend(spec);
}

/* ---------------------------------------------------------------- entry */

export function renderSpec(spec, slug = "figure") {
  const extraTitle = (titleLines(spec.title).length - 1) * 18;
  PAD.top = 44 + extraTitle;
  H = 400 + extraTitle + (legendRows(spec).length - 1) * LEGEND_ROW;
  const inner = spec.kind === "line" ? renderLine(spec) : renderCategorical(spec);
  const sources = [...new Set(spec.series.flatMap((s) => s.points.map((p) => p.sourceRecord)))];
  const desc = [`${spec.title}.`, spec.notes ?? "", `Sources: ${sources.join(", ")}.`].filter(Boolean).join(" ");
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${W} ${H}" width="100%" role="img" aria-labelledby="fig-${slug}-title fig-${slug}-desc" class="research-fig-svg" font-family="ui-sans-serif, system-ui, sans-serif">
<title id="fig-${slug}-title">${esc(spec.title)}</title>
<desc id="fig-${slug}-desc">${esc(desc)}</desc>
<style>
.research-fig-svg text { fill: var(--fig-fg, #e4e4e7); }
.research-fig-svg .fig-title { font-size: 14px; font-weight: 600; }
.research-fig-svg .fig-axis { font-size: 12px; fill: var(--fig-muted, #a1a1aa); }
.research-fig-svg .fig-tick { font-size: 11px; fill: var(--fig-muted, #a1a1aa); }
.research-fig-svg .fig-grid { stroke: var(--fig-grid, #27272a); stroke-width: 1; }
.research-fig-svg .fig-zero { stroke: var(--fig-muted, #a1a1aa); stroke-width: 1; }
.research-fig-svg .fig-line { fill: none; stroke-width: 2; }
.research-fig-svg .fig-whisker { stroke-width: 1.5; opacity: 0.8; }
.research-fig-svg .fig-bar { opacity: 0.75; }
.research-fig-svg .fig-legend text { font-size: 11px; }
.research-fig-svg .fig-pt { cursor: pointer; outline: none; }
.research-fig-svg .fig-pop { display: none; pointer-events: none; }
.research-fig-svg .fig-pop rect { fill: var(--fig-pop-bg, #18181b); stroke: var(--fig-pop-border, #3f3f46); }
.research-fig-svg .fig-pop text { fill: var(--fig-fg, #e4e4e7); }
.research-fig-svg .fig-pt:hover .fig-pop, .research-fig-svg .fig-pt:focus .fig-pop { display: block; }
.research-fig-svg .fig-pt:hover .fig-marker, .research-fig-svg .fig-pt:focus .fig-marker { stroke: var(--fig-fg, #e4e4e7); stroke-width: 2; }
</style>
${inner}
</svg>
`;
}

export function generate(specPath) {
  const spec = loadSpec(specPath);
  const out = join(dirname(specPath), basename(specPath, ".json") + ".svg");
  writeFileSync(out, renderSpec(spec, basename(specPath, ".json")));
  return out;
}

if (process.argv[1] && import.meta.url.endsWith(basename(process.argv[1]))) {
  const target = process.argv[2];
  if (!target) {
    console.error("usage: node web/scripts/figures/generate.mjs <spec.json>");
    process.exit(2);
  }
  console.log(`wrote ${generate(target)}`);
}
