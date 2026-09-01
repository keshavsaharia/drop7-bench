/**
 * Bridge from the benchmark playground to a native (C++) decision binary.
 *
 * The binary is `build/leaf-evolution/decide`, built by
 * `approaches/lifetime-objective/leaf-evolution/build.sh decide`. It receives
 * exactly the public state — the serialized board, the visible next disc and
 * the moves until the next rise — and prints `bestmove <column>`. One process
 * per decision keeps the bridge synchronous and deterministic; at depth 4 the
 * search itself costs far more than the process start.
 *
 * Nothing here is research evidence: the playground's scripted rounds are a
 * demonstration, and this bridge only lets the research search play them.
 */
import { execFileSync } from "node:child_process";
import { existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { serializeBoard, type GameState } from "../core/typescript/engine.ts";

const REPO_ROOT = join(dirname(fileURLToPath(import.meta.url)), "..", "..");

export const NATIVE_DECIDE_BINARY = join(REPO_ROOT, "build", "leaf-evolution", "decide");
export const NATIVE_BUILD_HINT =
  "approaches/lifetime-objective/leaf-evolution/build.sh decide";

/** The Rust bitboard engine's one-shot decision binary (cargo release build). */
export const RUST_DECIDE_BINARY = join(
  REPO_ROOT,
  "approaches",
  "fair-expectimax",
  "rust-engine",
  "target",
  "release",
  "decide",
);
export const RUST_BUILD_HINT =
  "cargo build --release --manifest-path approaches/fair-expectimax/rust-engine/Cargo.toml";

export interface NativeDecideOptions {
  /** Path to a leaf weights file ("name value" lines); omitted = frozen leaf. */
  weights?: string;
  depth?: number;
  chanceSamples?: number;
  /** Total cache-entry budget; parallel binaries divide it across workers. */
  cache?: number;
  binary?: string;
  /** Override for the build hint in the not-built error message. */
  buildHint?: string;
  /** Per-decision wall-clock limit; a depth step costs roughly 25-50x. */
  timeoutMs?: number;
}

const DEFAULT_TIMEOUT_MS = 600_000;

export function nativeBinaryAvailable(binary = NATIVE_DECIDE_BINARY): boolean {
  return existsSync(binary);
}

export function nativeDecide(
  state: GameState,
  options: NativeDecideOptions = {},
): number | null {
  if (state.gameOver) return null;
  const binary = options.binary ?? NATIVE_DECIDE_BINARY;
  if (!existsSync(binary)) {
    throw new Error(
      `native policy needs ${binary}; build it with ${options.buildHint ?? NATIVE_BUILD_HINT}`,
    );
  }
  const args = [
    "--board", serializeBoard(state.board),
    "--next", String(state.nextDisc),
    "--rise", String(state.movesRemaining),
    "--depth", String(options.depth ?? 4),
    "--chance-samples", String(options.chanceSamples ?? 7),
    "--cache", String(options.cache ?? 60_000),
  ];
  if (options.weights) {
    const weights = options.weights.startsWith("/") ? options.weights : join(REPO_ROOT, options.weights);
    if (!existsSync(weights)) throw new Error(`native policy weights file is missing: ${weights}`);
    args.push("--weights", weights);
  }
  const timeout = options.timeoutMs ?? DEFAULT_TIMEOUT_MS;
  let output: string;
  try {
    output = execFileSync(binary, args, { encoding: "utf8", timeout });
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code === "ETIMEDOUT") {
      throw new Error(
        `native decision exceeded its ${Math.round(timeout / 1000)}s budget at depth ${
          options.depth ?? 4
        } (board ${serializeBoard(state.board)}); raise timeoutMs in the policy registration if this depth genuinely needs longer`,
      );
    }
    throw error;
  }
  const match = output.match(/^bestmove (\d|none)$/m);
  if (!match) throw new Error(`native decision binary returned no bestmove: ${output.slice(0, 200)}`);
  return match[1] === "none" ? null : Number(match[1]);
}
