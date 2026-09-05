/**
 * The shared social card: the 1200x630 image a link preview shows.
 *
 * One template serves every page. It takes the page's own eyebrow, title,
 * summary and labels and draws them beside a Drop7 board, so a shared link
 * carries what the page is about rather than only what the site is about.
 * `app/opengraph-image.tsx` keeps its own hand-built home card; everything
 * else calls `renderPageCard`.
 *
 * Satori (the renderer behind next/og) cannot resolve a CSS variable, so the
 * palette below repeats the `@theme static` tokens from app/globals.css as
 * literals. Keep the two in step; `scripts/check-tokens.mjs` records this
 * file as one of the few places a literal colour is allowed.
 *
 * Every value drawn here comes from the caller, which reads it from the page
 * it is describing. Nothing is computed, counted or inferred.
 */
import { ImageResponse } from "next/og";
import type { ReactElement, ReactNode } from "react";
import { createGame, playMove, seededRandom } from "../../src/core/typescript/engine.ts";
import { CRACKED_CELL, DISC_STYLES, EMPTY_CELL, SOLID_CELL } from "@/components/discs";
import { socialArtDataUrl } from "@/lib/social-art";

export const SOCIAL_SIZE = { width: 1200, height: 630 };
export const SOCIAL_CONTENT_TYPE = "image/png";

/** The token palette, as literals Satori can read. */
const INK = {
  bg: "#0a0a0c",
  surface: "#111114",
  raised: "#18181c",
  cell: "#0d0d10",
  rule: "#26262b",
  ruleStrong: "#3a3a41",
  ink: "#f4f4f5",
  ink1: "#d4d4d8",
  ink2: "#a1a1aa",
  ink3: "#7d7d86",
  accent: "#8fb0ff",
  accentStrong: "#405db0",
  gray: "#aeb2af",
  grayCore: "#111412",
};

/* ------------------------------------------------------------------ board */

/**
 * A board reached by playing real moves through the engine, so the position
 * on a card is one the game can actually produce. The seed comes from the
 * page's own path, so each page keeps the same board every time it renders.
 */
export function socialPosition(seed: number, moveCount = 18) {
  const random = seededRandom(seed >>> 0);
  let state = createGame(random);
  for (let move = 0; move < moveCount; move += 1) {
    // Walk the columns from a seed-dependent offset and take the first legal one.
    let played = false;
    for (let step = 0; step < 7 && !played; step += 1) {
      const column = (move * 3 + step + (seed % 7)) % 7;
      const result = playMove(state, column, random);
      if (result) {
        state = result.state;
        played = true;
      }
    }
    if (!played) break;
  }
  return state;
}

/** A stable 32-bit seed for a page, from its path. */
export function seedFromPath(path: string): number {
  let hash = 0x811c9dc5;
  for (let index = 0; index < path.length; index += 1) {
    hash ^= path.charCodeAt(index);
    hash = Math.imul(hash, 0x01000193);
  }
  return hash >>> 0;
}

export function SocialDisc({ cell, size = 38 }: { cell: number; size?: number }): ReactElement | null {
  if (cell === EMPTY_CELL) return null;

  if (cell === SOLID_CELL) {
    return (
      <div
        style={{
          width: size,
          height: size,
          borderRadius: 999,
          background: INK.gray,
          display: "flex",
          alignItems: "center",
          justifyContent: "center",
        }}
      >
        <div
          style={{
            width: size * 0.72,
            height: size * 0.72,
            borderRadius: 999,
            background: INK.grayCore,
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
          }}
        >
          <div style={{ width: size * 0.58, height: size * 0.58, borderRadius: 999, background: INK.gray, display: "flex" }} />
        </div>
      </div>
    );
  }

  if (cell === CRACKED_CELL) {
    return (
      <div
        style={{
          width: size,
          height: size,
          borderRadius: 999,
          border: `${Math.max(3, size * 0.13)}px dashed ${INK.gray}`,
          background: INK.grayCore,
          display: "flex",
          alignItems: "center",
          justifyContent: "center",
        }}
      >
        <div style={{ width: size * 0.5, height: size * 0.5, borderRadius: 999, background: INK.gray, display: "flex" }} />
      </div>
    );
  }

  const palette = DISC_STYLES[cell] ?? { bg: INK.ruleStrong, fg: INK.ink };
  return (
    <div
      style={{
        width: size,
        height: size,
        borderRadius: 999,
        background: palette.bg,
        color: palette.fg,
        display: "flex",
        alignItems: "center",
        justifyContent: "center",
        fontSize: size * 0.52,
        fontWeight: 800,
        lineHeight: 1,
      }}
    >
      {cell}
    </div>
  );
}

/** The board motif on the right of a page card. */
function CardBoard({ seed }: { seed: number }) {
  const state = socialPosition(seed);
  const rows = Array.from({ length: 7 }, (_, row) => state.board.slice(row * 7, row * 7 + 7));
  const cell = 46;
  return (
    <div
      style={{
        display: "flex",
        flexDirection: "column",
        border: `1px solid ${INK.rule}`,
        borderRadius: 20,
        background: INK.surface,
        padding: 18,
      }}
    >
      <div style={{ display: "flex", height: 40, alignItems: "center", justifyContent: "center" }}>
        <SocialDisc cell={state.nextDisc} size={34} />
      </div>
      <div
        style={{
          display: "flex",
          flexDirection: "column",
          border: `4px solid ${INK.raised}`,
          borderRadius: 12,
          background: INK.rule,
          padding: 4,
        }}
      >
        {rows.map((row, rowIndex) => (
          <div key={rowIndex} style={{ display: "flex" }}>
            {row.map((value, columnIndex) => (
              <div
                key={columnIndex}
                style={{
                  width: cell,
                  height: cell,
                  margin: 1,
                  background: INK.cell,
                  display: "flex",
                  alignItems: "center",
                  justifyContent: "center",
                }}
              >
                <SocialDisc cell={value} size={36} />
              </div>
            ))}
          </div>
        ))}
      </div>
    </div>
  );
}

/* ------------------------------------------------------------------- card */

export interface PageCardProps {
  /** The section the page belongs to, shown as a mono label. */
  eyebrow?: string;
  /** The page's own title. */
  title: string;
  /** The page's own summary or lead, trimmed to fit. */
  summary?: string;
  /** Short chips along the foot: a status, an evidence tier, a record id. */
  labels?: readonly string[];
  /** The path shown in the foot, and the seed for the board motif. */
  path?: string;
  /** The registered card animation. Its checked-in resting frame is used. */
  art?: ReactNode;
}

/** Longest title that still sets at the large size. */
function titleSize(title: string): number {
  if (title.length <= 34) return 62;
  if (title.length <= 58) return 52;
  if (title.length <= 84) return 44;
  return 38;
}

/**
 * The foot line: the site and the route, with a long path's middle elided so
 * it stays on one line beside the label chips.
 */
function shortPath(path: string): string {
  const full = `drop7.dev${path}`;
  if (full.length <= 44) return full;
  const parts = path.split("/").filter(Boolean);
  if (parts.length < 3) return `${full.slice(0, 43)}…`;
  const short = `drop7.dev/${parts[0]}/…/${parts[parts.length - 1]}`;
  return short.length <= 52 ? short : `${short.slice(0, 51)}…`;
}

function trim(text: string, limit: number): string {
  if (text.length <= limit) return text;
  const cut = text.slice(0, limit);
  const stop = cut.lastIndexOf(" ");
  return `${cut.slice(0, stop > limit * 0.6 ? stop : limit).trimEnd()}…`;
}

export function renderPageCard({ eyebrow, title, summary, labels = [], path = "", art }: PageCardProps) {
  const seed = seedFromPath(path || title);
  const artSource = art ? socialArtDataUrl(art) : null;
  return new ImageResponse(
    (
      <div
        style={{
          width: "100%",
          height: "100%",
          padding: "52px 60px",
          backgroundColor: INK.bg,
          backgroundImage: `radial-gradient(circle at 86% 22%, rgba(64,93,176,0.18), transparent 34%), linear-gradient(150deg, ${INK.bg} 0%, #0c0c10 60%, #10131f 100%)`,
          color: INK.ink,
          display: "flex",
          alignItems: "stretch",
          justifyContent: "space-between",
          fontFamily: "Arial, Helvetica, sans-serif",
        }}
      >
        <div
          style={{
            width: 680,
            display: "flex",
            flexDirection: "column",
            justifyContent: "space-between",
          }}
        >
          <div style={{ display: "flex", alignItems: "center" }}>
            <div
              style={{
                width: 40,
                height: 40,
                marginRight: 13,
                borderRadius: 999,
                background: INK.accentStrong,
                color: "#ffffff",
                display: "flex",
                alignItems: "center",
                justifyContent: "center",
                fontSize: 21,
                fontWeight: 800,
              }}
            >
              7
            </div>
            <span style={{ fontSize: 23, fontWeight: 700 }}>Drop7 Research</span>
          </div>

          <div style={{ display: "flex", flexDirection: "column" }}>
            {eyebrow && (
              <span
                style={{
                  marginBottom: 18,
                  color: INK.accent,
                  fontSize: 17,
                  fontWeight: 800,
                  letterSpacing: "0.16em",
                  textTransform: "uppercase",
                }}
              >
                {eyebrow}
              </span>
            )}
            <h1
              style={{
                margin: 0,
                fontSize: titleSize(title),
                fontWeight: 800,
                lineHeight: 1.06,
                letterSpacing: "-0.03em",
              }}
            >
              {trim(title, 110)}
            </h1>
            {summary && (
              <p style={{ margin: "22px 0 0", color: INK.ink2, fontSize: 22, lineHeight: 1.4 }}>
                {trim(summary, 190)}
              </p>
            )}
          </div>

          <div style={{ display: "flex", alignItems: "center" }}>
            {labels.slice(0, 3).map((label) => (
              <span
                key={label}
                style={{
                  marginRight: 10,
                  padding: "7px 13px",
                  border: `1px solid ${INK.ruleStrong}`,
                  borderRadius: 999,
                  background: INK.raised,
                  color: INK.ink1,
                  display: "flex",
                  flexShrink: 0,
                  whiteSpace: "nowrap",
                  fontSize: 15,
                  fontWeight: 700,
                  letterSpacing: "0.06em",
                  textTransform: "uppercase",
                }}
              >
                {label}
              </span>
            ))}
            <span
              style={{
                marginLeft: "auto",
                paddingLeft: 16,
                color: INK.ink3,
                fontSize: 17,
                fontWeight: 600,
                whiteSpace: "nowrap",
              }}
            >
              {shortPath(path)}
            </span>
          </div>
        </div>

        <div style={{ display: "flex", alignItems: "center" }}>
          {artSource ? (
            <div
              style={{
                width: 400,
                height: 250,
                border: `1px solid ${INK.rule}`,
                borderRadius: 20,
                background: INK.surface,
                display: "flex",
                alignItems: "center",
                justifyContent: "center",
                overflow: "hidden",
              }}
            >
              {/* The source is an inline SVG final frame; next/image cannot optimize data URLs in next/og. */}
              {/* eslint-disable-next-line @next/next/no-img-element */}
              <img src={artSource} alt="" width={400} height={225} style={{ objectFit: "contain" }} />
            </div>
          ) : (
            <CardBoard seed={seed} />
          )}
        </div>
      </div>
    ),
    SOCIAL_SIZE,
  );
}

/**
 * The alt text a link preview reads out. A route's `alt` is a module-level
 * export, so it cannot name a page the request has not resolved yet; a
 * dynamic segment passes the section name and gets an honest general
 * sentence rather than a wrong specific one.
 */
export function cardAlt({ eyebrow, title }: Pick<PageCardProps, "eyebrow" | "title">): string {
  return `${eyebrow ? `${eyebrow}: ${title}` : title}, on Drop7 Research`;
}
