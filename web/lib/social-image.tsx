import { ImageResponse } from "next/og";
import {
  createGame,
  playMove,
  seededRandom,
} from "../../src/core/typescript/engine.ts";
import {
  CRACKED_CELL,
  DISC_STYLES,
  EMPTY_CELL,
  SOLID_CELL,
} from "@/components/discs";

export const socialImageAlt =
  "Drop7 Research — an open-source search for the best strategy in a game of chance, illustrated with a Drop7 board.";

export const socialImageSize = {
  width: 1200,
  height: 630,
};

export const socialImageContentType = "image/png";

const SOCIAL_POSITION_SEED = 0xd7070017;
const SOCIAL_POSITION_MOVES = [
  3, 2, 4, 3, 1, 5, 0, 6, 2, 4, 3, 1, 5, 0, 6, 2, 4, 3,
] as const;

/** Build the card's board through the real engine so the position stays honest. */
function createSocialPosition() {
  const random = seededRandom(SOCIAL_POSITION_SEED);
  let state = createGame(random);

  for (const column of SOCIAL_POSITION_MOVES) {
    const result = playMove(state, column, random);
    if (!result) throw new Error(`Social-card move ${column + 1} became illegal`);
    state = result.state;
  }

  return state;
}

function SocialDisc({ cell, size = 38 }: { cell: number; size?: number }) {
  if (cell === EMPTY_CELL) return null;

  if (cell === SOLID_CELL) {
    return (
      <div
        style={{
          width: size,
          height: size,
          borderRadius: 999,
          background: "#aeb2af",
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
            background: "#111412",
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
          }}
        >
          <div
            style={{
              width: size * 0.58,
              height: size * 0.58,
              borderRadius: 999,
              background: "#aeb2af",
              display: "flex",
            }}
          />
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
          border: `${Math.max(4, size * 0.13)}px dashed #aeb2af`,
          background: "#111412",
          display: "flex",
          alignItems: "center",
          justifyContent: "center",
        }}
      >
        <div
          style={{
            width: size * 0.55,
            height: size * 0.55,
            borderRadius: 999,
            background: "#aeb2af",
            display: "flex",
          }}
        />
      </div>
    );
  }

  const palette = DISC_STYLES[cell] ?? { bg: "#52525b", fg: "#ffffff" };
  return (
    <div
      style={{
        width: size,
        height: size,
        borderRadius: 999,
        border: "1px solid rgba(0,0,0,0.22)",
        background: palette.bg,
        color: palette.fg,
        display: "flex",
        alignItems: "center",
        justifyContent: "center",
        fontSize: size * 0.52,
        fontWeight: 800,
        lineHeight: 1,
        boxShadow: `inset 0 -${Math.max(3, size * 0.11)}px 0 rgba(0,0,0,0.22)`,
      }}
    >
      {cell}
    </div>
  );
}

function SocialBoard() {
  const state = createSocialPosition();
  const rows = Array.from({ length: 7 }, (_, row) =>
    state.board.slice(row * 7, row * 7 + 7),
  );

  return (
    <div
      style={{
        width: 392,
        height: 518,
        border: "1px solid #27272a",
        borderRadius: 22,
        background: "rgba(24,24,27,0.9)",
        padding: 24,
        display: "flex",
        flexDirection: "column",
      }}
    >
      <div
        style={{
          height: 46,
          display: "flex",
          alignItems: "center",
          justifyContent: "space-between",
          color: "#71717a",
          fontSize: 12,
          fontWeight: 700,
          letterSpacing: "0.18em",
          textTransform: "uppercase",
        }}
      >
        <span>live game</span>
        <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
          <span>rise</span>
          {Array.from({ length: 5 }, (_, index) => (
            <span
              key={index}
              style={{
                width: 9,
                height: 9,
                borderRadius: 999,
                background: index < 3 ? "#38bdf8" : "#3f3f46",
                display: "flex",
              }}
            />
          ))}
        </div>
      </div>

      <div
        style={{
          width: 334,
          height: 44,
          display: "flex",
          gap: 2,
        }}
      >
        {Array.from({ length: 7 }, (_, column) => (
          <div
            key={column}
            style={{
              width: 46,
              height: 44,
              display: "flex",
              alignItems: "center",
              justifyContent: "center",
            }}
          >
            {column === 3 ? <SocialDisc cell={state.nextDisc} size={36} /> : null}
          </div>
        ))}
      </div>

      <div
        style={{
          width: 344,
          height: 344,
          marginLeft: -5,
          border: "5px solid #18181b",
          borderRadius: 13,
          background: "#27272a",
          padding: 5,
          display: "flex",
          flexDirection: "column",
          gap: 2,
        }}
      >
        {rows.map((row, rowIndex) => (
          <div key={rowIndex} style={{ display: "flex", gap: 2 }}>
            {row.map((cell, columnIndex) => (
              <div
                key={columnIndex}
                style={{
                  width: 46,
                  height: 46,
                  background: "#09090b",
                  display: "flex",
                  alignItems: "center",
                  justifyContent: "center",
                }}
              >
                <SocialDisc cell={cell} />
              </div>
            ))}
          </div>
        ))}
      </div>

      <div
        style={{
          flex: 1,
          display: "flex",
          alignItems: "flex-end",
          justifyContent: "space-between",
          color: "#71717a",
          fontSize: 13,
        }}
      >
        <span>play · evaluate · auto</span>
        <span style={{ color: "#38bdf8" }}>drop7.dev/play</span>
      </div>
    </div>
  );
}

export function renderSocialImage() {
  return new ImageResponse(
    (
      <div
        style={{
          width: "100%",
          height: "100%",
          padding: "56px 64px",
          backgroundColor: "#09090b",
          backgroundImage:
            "radial-gradient(circle at 88% 24%, rgba(64,93,176,0.2), transparent 32%), linear-gradient(145deg, #09090b 0%, #0c0c10 58%, #10131f 100%)",
          color: "#fafafa",
          display: "flex",
          alignItems: "center",
          justifyContent: "space-between",
          fontFamily: "Arial, Helvetica, sans-serif",
        }}
      >
        <div
          style={{
            width: 626,
            height: 518,
            display: "flex",
            flexDirection: "column",
            justifyContent: "space-between",
          }}
        >
          <div style={{ display: "flex", alignItems: "center", gap: 14 }}>
            <div
              style={{
                width: 44,
                height: 44,
                borderRadius: 999,
                background: "#405db0",
                color: "white",
                display: "flex",
                alignItems: "center",
                justifyContent: "center",
                fontSize: 23,
                fontWeight: 800,
                boxShadow: "inset 0 -5px 0 rgba(0,0,0,0.2)",
              }}
            >
              7
            </div>
            <span style={{ fontSize: 26, fontWeight: 750 }}>Drop7 Research</span>
          </div>

          <div style={{ display: "flex", flexDirection: "column", gap: 24 }}>
            <div
              style={{
                alignSelf: "flex-start",
                border: "1px solid #1e3a5f",
                borderRadius: 999,
                background: "rgba(8,47,73,0.45)",
                color: "#7dd3fc",
                padding: "9px 15px",
                display: "flex",
                fontSize: 13,
                fontWeight: 800,
                letterSpacing: "0.13em",
                textTransform: "uppercase",
              }}
            >
              Open-source strategy research
            </div>
            <h1
              style={{
                margin: 0,
                maxWidth: 620,
                fontSize: 57,
                fontWeight: 900,
                lineHeight: 1.02,
                letterSpacing: "-0.04em",
              }}
            >
              What is the best strategy in a game with chance?
            </h1>
            <p
              style={{
                margin: 0,
                maxWidth: 575,
                color: "#a1a1aa",
                fontSize: 23,
                lineHeight: 1.35,
              }}
            >
              Play Drop7 in your browser and follow the search for a million-point strategy.
            </p>
          </div>

          <div
            style={{
              display: "flex",
              alignItems: "center",
              gap: 18,
              color: "#71717a",
              fontSize: 15,
              fontWeight: 700,
              letterSpacing: "0.08em",
              textTransform: "uppercase",
            }}
          >
            <span style={{ color: "#e4e4e7" }}>drop7.dev</span>
            <span>Play</span>
            <span>Learn</span>
            <span>Research</span>
          </div>
        </div>

        <SocialBoard />
      </div>
    ),
    socialImageSize,
  );
}
