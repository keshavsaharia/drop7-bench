/**
 * The engine catalogue for the Engines section, as data.
 *
 * Every string here is a fact about the code or a value quoted from a
 * retained record, and every number travels with its source. Nothing is
 * computed. The catalogue mirrors the engines audit (scratchpad
 * redesign/audit-engines.md, section 1.1) and the comparison skeleton
 * (section 4.2). A `null` cell in COMPARISON_ROWS means the measurement was
 * never retained; NOT_APPLICABLE means the engine has no such component.
 */

export type EngineSlug =
  | "typescript"
  | "browser"
  | "native"
  | "fast"
  | "scenario"
  | "rust"
  | "rust-classic"
  | "gpu";

export type EngineArt = "engine-native" | "engine-typescript" | "engine-rust";

export interface EngineLink {
  label: string;
  href?: string;
}

export interface EngineEntry {
  slug: EngineSlug;
  title: string;
  /** Column head in the comparison table. */
  shortTitle: string;
  language: string;
  /** One sentence: what this engine is for. */
  role: string;
  /** Two or three words for a card's ROLE line. */
  roleLabel: string;
  /** Source entry point, as a repository path. */
  path: string;
  boardRepresentation: string;
  /** A few words for a card's BOARD line. */
  boardShort: string;
  latentMode: boolean | "n/a";
  /** Who it is proven against, at what scale, with the source. */
  parity: string;
  /** A few words for a card's PARITY line. */
  parityShort: string;
  usedBy: EngineLink[];
  /** The approach README that documents it, when one exists. */
  readme?: `approaches/${string}/${string}/README.mdx`;
  art: EngineArt;
  /** False when the art would misdescribe the entry (the GPU work is not a game engine). */
  hero: boolean;
}

export const RS_RUST = "RS-20260824T075451Z-e89ea128";
export const RS_MEMO = "RS-20260822T074305Z-bd1697c8";
export const RS_M6 = "RS-20260824T010000Z-8f3e9b4f";
export const RS_FRONTIER = "RS-20260825T052959Z-1b3ed9a5";
export const FINDING_13 = "docs/exploratory/finding-13-fast-engine.md";
export const FINDING_15 = "docs/exploratory/finding-15-depth5-exact-estimator.md";
export const FINDING_02 = "docs/exploratory/finding-02-scenario-benchmark.md";
export const AUDIT_01 = "docs/exploratory/audit-01-engine-fidelity.md";
export const AUDIT_06 = "docs/exploratory/audit-06-engine-efficiency.md";
export const REPRODUCIBILITY = "docs/reproducibility.md";
export const HISTORY = "docs/research/history.md";
export const BENCHMARKS = "docs/benchmarks.md";

export const ENGINES: readonly EngineEntry[] = [
  {
    slug: "typescript",
    title: "TypeScript engine",
    shortTitle: "TypeScript",
    language: "TypeScript",
    role: "The readable statement of the rules: the engine behind the browser game, every board drawn on this site, the scripted rounds and the parity harness.",
    roleLabel: "reference rulebook",
    path: "src/core/typescript/engine.ts",
    boardRepresentation:
      "A 49-cell array, row-major from the top (0 empty, 1 to 7 a numbered disc, 8 a solid gray disc, 9 a cracked gray disc); every step copies the array.",
    boardShort: "49-cell array, copied per step",
    latentMode: true,
    parity: `Exact against the C++ engine on 256 seeded games and 6,852 moves (${REPRODUCIBILITY}, current verification snapshot). The Rust engine's TypeScript-driver arm replays 256 games against it (${RS_RUST}).`,
    parityShort: "exact on 256 seeds, 6,852 moves",
    usedBy: [
      { label: "Every TypeScript policy under src/core/typescript", href: "/src/core/typescript" },
      { label: "The scripted-round playground and the D7P server", href: "/leaderboard" },
      { label: "The browser game and every board on this site", href: "/play" },
      { label: "Mobile submission validation (web/lib/submissions)" },
    ],
    art: "engine-typescript",
    hero: true,
  },
  {
    slug: "browser",
    title: "Browser fast search",
    shortTitle: "Browser",
    language: "TypeScript",
    role: "The solver that runs in a visitor's browser: the reference search with a faster move generator, an allocation-free leaf and a packed transposition table, kept value-identical to the reference.",
    roleLabel: "console solver",
    path: "web/lib/play/fast-search.ts",
    boardRepresentation:
      "A reused 49-cell array plus typed-array row and column masks and a pop-flag array; the transposition key is the position packed into seven 32-bit words.",
    boardShort: "49 cells + typed-array masks",
    latentMode: false,
    parity:
      "Test-gated against the reference solver on real positions (web/lib/play/fast-search.test.ts, 12 tests); no research record, and no counts were retained.",
    parityShort: "test-gated, counts not retained",
    usedBy: [
      { label: "The browser game in evaluate and auto modes", href: "/play" },
      { label: "Drop7Game in evaluate mode on pages that want a live example" },
    ],
    art: "engine-typescript",
    hero: true,
  },
  {
    slug: "native",
    title: "C++ engine",
    shortTitle: "C++",
    language: "C++20",
    role: "The reference engine: it plays the fair depth-3 and depth-4 reference games and nearly every large research run, and every other engine is proven against it.",
    roleLabel: "research reference",
    path: "src/core/native/engine.hpp",
    boardRepresentation:
      "A std::array of 49 bytes, row-major from the top; the cascade keeps a std::vector of waves.",
    boardShort: "49-byte array",
    latentMode: false,
    parity: `The reference the others are proven against. Exact against the TypeScript engine on 256 seeds and 6,852 moves (${REPRODUCIBILITY}). The scenario engine (218,470 moves, ${FINDING_02}), the fast engine (438,020 moves, ${FINDING_13}) and the Rust engine (512 center and 256 search-policy games, ${RS_RUST}) each replay it with zero mismatches.`,
    parityShort: "the reference",
    usedBy: [
      { label: "The fair D3/D4 reference policy", href: "/approaches/fair-expectimax/reference" },
      { label: "The native suite", href: "/approaches/ntuple-rl/native-suite" },
      { label: "PPO and the torch environment", href: "/approaches/ntuple-rl/torch-ppo" },
      { label: "Nearly every C++ entry point under approaches/", href: "/approaches" },
    ],
    art: "engine-native",
    hero: true,
  },
  {
    slug: "fast",
    title: "C++ fast engine",
    shortTitle: "C++ fast",
    language: "C++20",
    role: "A reimplementation of the move engine and the fair leaf, proven bit-identical to the frozen reference and measured at about 3x end to end.",
    roleLabel: "fast research solver",
    path: "approaches/lifetime-objective/fast-engine/fast-engine.hpp",
    boardRepresentation:
      "The same 49-byte array, scanned once into seven row masks, seven column masks and a 49-bit numbered bitboard; gravity in place on the columns that changed; a 32-byte packed transposition key.",
    boardShort: "49 bytes + 14 masks + bitboard",
    latentMode: false,
    parity: `Against the frozen C++ reference: 438,020 trajectory moves and 548,263 waves with zero mismatches, 306 search moves at nine depth and strata configurations, and 225,183 leaf states bit-exact (${FINDING_13}). Whole-game identity on 64 games, 704 field comparisons (${FINDING_15}).`,
    parityShort: "bit-identical on 438,020 moves",
    usedBy: [
      { label: "Fair-D4 cohorts in lifetime-objective", href: "/approaches/lifetime-objective" },
      { label: "Leaf evolution", href: "/approaches/lifetime-objective/leaf-evolution" },
      { label: "The learned leaf", href: "/approaches/lifetime-objective/learned-leaf" },
      { label: "The playground policy native-fair-d4-s7", href: "/leaderboard" },
    ],
    readme: "approaches/lifetime-objective/fast-engine/README.mdx",
    art: "engine-native",
    hero: true,
  },
  {
    slug: "scenario",
    title: "Scenario engine and exact solver",
    shortTitle: "Scenario",
    language: "C++20",
    role: "The rules with a pluggable reveal source, so a position can carry a fixed hidden board and be solved exactly; an oracle instrument, never a policy.",
    roleLabel: "oracle instrument",
    path: "approaches/lifetime-objective/scenario/scenario.hpp",
    boardRepresentation:
      "A 49-byte board paired with a 49-byte latent board that the same gravity and rise transforms permute.",
    boardShort: "49 bytes + 49 latent bytes",
    latentMode: true,
    parity: `Against the C++ engine in stream mode: 8,192 game-plays, 218,470 moves, zero mismatches; the solver agrees with a naive enumerator on 428 comparisons (${FINDING_02}).`,
    parityShort: "identical on 218,470 moves",
    usedBy: [
      { label: "The scenario benchmark (finding-02)", href: `/${FINDING_02.replace(/\.md$/, "")}` },
      { label: "Suite validation", href: "/approaches/lifetime-objective/suite-validation" },
    ],
    readme: "approaches/lifetime-objective/scenario/README.mdx",
    art: "engine-native",
    hero: true,
  },
  {
    slug: "rust",
    title: "Rust bitboard engine",
    shortTitle: "Rust",
    language: "Rust",
    role: "A packed port of the rules, the fair search and the fair leaf, replayed move for move against the C++ engines, with a within-decision parallel scheduler.",
    roleLabel: "packed port",
    path: "approaches/fair-expectimax/rust-engine/src/lib.rs",
    boardRepresentation:
      "Seven 32-bit column words, four bits per cell, the bottom row in the least-significant nibble (28 bytes); row-major bitboards are derived per wave.",
    boardShort: "seven 32-bit words (28 bytes)",
    latentMode: false,
    parity: `Against the C++ reference, the C++ fast engine and the TypeScript engine: three trajectory arms, 36,427 moves and 40,286 waves with zero mismatches; 150,854 leaf states bit-identical; 105 d4s7 and 10 d5s7 roots with bit-identical per-column values (${RS_RUST}).`,
    parityShort: "identical on 36,427 moves",
    usedBy: [
      { label: "Playground policies rust-fair-d6-s7 and rust-fair-d7-s7", href: "/leaderboard" },
      { label: "The analyzer and cluster workflow (RS-20260825T052959Z-57698687)" },
      {
        label: "The KF linear-Q Rust transfer experiment",
        href: "/experiments/EX-20260902-kf-linear-q-rust-transfer-4328a730",
      },
      {
        label: "The pruned-search gameplay pilot",
        href: "/experiments/EX-20260902-pruned-search-gameplay-pilot-bf465b1d",
      },
    ],
    readme: "approaches/fair-expectimax/rust-engine/README.mdx",
    art: "engine-rust",
    hero: true,
  },
  {
    slug: "rust-classic",
    title: "Rust Classic-mode engine",
    shortTitle: "Rust Classic",
    language: "Rust",
    role: "A second Rust crate for the Classic ruleset, written for the mobile game-collection API; nothing consumes it yet.",
    roleLabel: "classic ruleset",
    path: "src/core/rust/classic-engine/src/lib.rs",
    boardRepresentation: "A 49-byte board plus a 49-byte latent array, plain loops, no bit packing.",
    boardShort: "49 bytes + 49 latent bytes",
    latentMode: true,
    parity:
      "One shared conformance transition with the TypeScript Classic engine (src/core/typescript/classic-engine.test.ts) and four cargo tests. Not yet in the native parity suite or any whole-trajectory harness (CT-20260901T070609Z-3028349f).",
    parityShort: "one shared transition",
    usedBy: [],
    art: "engine-rust",
    hero: true,
  },
  {
    slug: "gpu",
    title: "GPU and batching",
    shortTitle: "GPU",
    language: "Python / PyTorch on ROCm",
    role: "Not a game engine: the work that made PyTorch run on the workstation's GPU and measured training throughput; a batched GPU simulator is a proposal only.",
    roleLabel: "training only",
    path: "approaches/lifetime-objective/gpu/bench.py",
    boardRepresentation: "None; no game is played on the GPU.",
    boardShort: "none",
    latentMode: "n/a",
    parity: "None; no game is played on the GPU, and the work reads as diagnostic.",
    parityShort: "no game played",
    usedBy: [{ label: "Neural training only; the hardware plan keeps exact transitions and chain cascades on the CPU" }],
    readme: "approaches/lifetime-objective/gpu/README.mdx",
    art: "engine-native",
    hero: false,
  },
];

export const ENGINE_SLUGS: readonly EngineSlug[] = ENGINES.map((entry) => entry.slug);

export function getEngine(slug: string): EngineEntry | null {
  return ENGINES.find((entry) => entry.slug === slug) ?? null;
}

/** The engines that carry card art on the index. */
export const FEATURED_SLUGS: readonly EngineSlug[] = ["typescript", "native", "fast", "scenario", "rust"];

/** The engines shown as smaller cards without art. */
export const COMPACT_SLUGS: readonly EngineSlug[] = ["browser", "rust-classic", "gpu"];

/** The family and slug of an approach README path. */
export function readmeApproach(readme: string): { family: string; slug: string } | null {
  const match = /^approaches\/([a-z0-9-]+)\/([a-z0-9-]+)\/README\.mdx$/.exec(readme);
  return match ? { family: match[1], slug: match[2] } : null;
}

/* ---- Side-by-side comparison (audit section 4.2) ---- */

/** The engine has no such component, so the quantity does not apply to it. */
export const NOT_APPLICABLE = "not applicable";

export interface ComparisonCell {
  /** The recorded string, exactly as the audit gives it; null when the measurement was not retained. */
  value: string | null;
  /** Record id(s) (RS-, MACH-, CT-), doc path(s) under docs/, or the source file the fact comes from. */
  source: string | readonly string[] | null;
}

export type ComparisonSlug = Exclude<EngineSlug, "gpu">;

export const COMPARISON_SLUGS: readonly ComparisonSlug[] = [
  "typescript",
  "browser",
  "native",
  "fast",
  "scenario",
  "rust",
  "rust-classic",
];

export interface ComparisonRow {
  quantity: string;
  cells: Record<ComparisonSlug, ComparisonCell>;
}

const none: ComparisonCell = { value: null, source: null };
const na: ComparisonCell = { value: NOT_APPLICABLE, source: null };

const TS_ENGINE = "src/core/typescript/engine.ts";
const TS_SOLVER = "src/core/typescript/solver.ts";
const BROWSER_SEARCH = "web/lib/play/fast-search.ts";
const BROWSER_TEST = "web/lib/play/fast-search.test.ts";
const NATIVE_ENGINE = "src/core/native/engine.hpp";
const FAST_ENGINE = "approaches/lifetime-objective/fast-engine/fast-engine.hpp";
const SCENARIO_ENGINE = "approaches/lifetime-objective/scenario/scenario.hpp";
const CLASSIC_RS = "src/core/rust/classic-engine/src/lib.rs";
const CLASSIC_TEST = "src/core/typescript/classic-engine.test.ts";

export const COMPARISON_ROWS: readonly ComparisonRow[] = [
  {
    quantity: "Board bytes",
    cells: {
      typescript: { value: "49 cells; bytes not recorded", source: TS_ENGINE },
      browser: none,
      native: { value: "49", source: FINDING_13 },
      fast: { value: "49 + scan", source: FAST_ENGINE },
      scenario: { value: "49 + 49 latent", source: SCENARIO_ENGINE },
      rust: { value: "28", source: RS_RUST },
      "rust-classic": none,
    },
  },
  {
    quantity: "Rules parity partner and scale",
    cells: {
      typescript: {
        value: "C++: 256 seeds / 6,852 moves; Rust: 256 games",
        source: [REPRODUCIBILITY, RS_RUST],
      },
      browser: { value: "TypeScript: test-gated, counts not retained", source: BROWSER_TEST },
      native: {
        value:
          "TypeScript: 256 seeds / 6,852 moves; scenario: 218,470 moves; C++ fast: 438,020 moves; Rust: 512 center + 256 search-policy games",
        source: [REPRODUCIBILITY, FINDING_02, FINDING_13, RS_RUST],
      },
      fast: { value: "C++: 438,020 moves; whole-game 704 comparisons", source: [FINDING_13, FINDING_15] },
      scenario: { value: "C++: 218,470 moves", source: FINDING_02 },
      rust: { value: "C++, C++ fast, TypeScript: 36,427 moves / 40,286 waves", source: RS_RUST },
      "rust-classic": { value: "TypeScript Classic: one shared transition; 4 cargo tests", source: CLASSIC_TEST },
    },
  },
  {
    quantity: "Leaf parity",
    cells: {
      typescript: na,
      browser: { value: "bit-identical vs evaluateHeuristic, all profiles", source: BROWSER_TEST },
      native: na,
      fast: { value: "225,183 states bit-exact", source: FINDING_13 },
      scenario: na,
      rust: { value: "150,854 states", source: RS_RUST },
      "rust-classic": na,
    },
  },
  {
    quantity: "Search parity",
    cells: {
      typescript: na,
      browser: { value: "depth 2/3/4 exact incl. eviction", source: BROWSER_TEST },
      native: na,
      fast: { value: "306 moves, 9 configs", source: FINDING_13 },
      scenario: { value: "428 solver comparisons vs naive", source: FINDING_02 },
      rust: { value: "105 d4s7 + 10 d5s7 roots", source: RS_RUST },
      "rust-classic": na,
    },
  },
  {
    quantity: "Single-core moves/s",
    cells: {
      typescript: { value: "649,471", source: RS_RUST },
      browser: none,
      native: { value: "6,799,180", source: RS_RUST },
      fast: { value: "6,511,760", source: RS_RUST },
      scenario: none,
      rust: { value: "12,838,933", source: RS_RUST },
      "rust-classic": none,
    },
  },
  {
    quantity: "Leaf ns/eval",
    cells: {
      typescript: none,
      browser: none,
      native: { value: "970.3 at load 14.6", source: FINDING_13 },
      fast: { value: "278.8 at load 14.6; 187.5", source: [FINDING_13, RS_RUST] },
      scenario: na,
      rust: { value: "155.6", source: RS_RUST },
      "rust-classic": na,
    },
  },
  {
    quantity: "d4s7 ms/decision",
    cells: {
      typescript: none,
      browser: none,
      native: { value: "3,247.8; 2,903.9 at load 26.9", source: [RS_RUST, FINDING_13] },
      fast: { value: "1,071.5; 910.9", source: [RS_RUST, FINDING_13] },
      scenario: na,
      rust: { value: "907.6 with 64k table; 1,633.4 no table", source: RS_RUST },
      "rust-classic": na,
    },
  },
  {
    quantity: "d5s7 ms/decision",
    cells: {
      typescript: none,
      browser: none,
      native: { value: "23,992.6; 30,340.2", source: [RS_RUST, FINDING_13] },
      fast: { value: "7,817.3; 9,379.2", source: [RS_RUST, FINDING_13] },
      scenario: na,
      rust: { value: "7,787.9 / 7,047.1 / 6,748.4 at 256k / 1M / 4M; 63,325.4 no table", source: RS_RUST },
      "rust-classic": na,
    },
  },
  {
    quantity: "Speedup vs its own reference",
    cells: {
      typescript: na,
      browser: none,
      native: { value: "1.00 (the reference)", source: FINDING_13 },
      fast: {
        value:
          "3.08x d4s5 decision; 2.88x to 3.23x games and probes; memo 1.581x / 1.634x indicative; M6 5.56x play duty",
        source: [FINDING_13, RS_MEMO, RS_M6],
      },
      scenario: { value: "table 7.6x nodes / 4.9x wall", source: FINDING_02 },
      rust: {
        value:
          "1.97x vs C++ fast engine, 19.8x vs TypeScript; frontier 1.2612x d4s7, 1.1352x d5s7 on arm64 12 CPUs",
        source: [RS_RUST, RS_FRONTIER],
      },
      "rust-classic": none,
    },
  },
  {
    quantity: "Table memory",
    cells: {
      typescript: { value: "40,000 entries; bytes not recorded", source: TS_SOLVER },
      browser: none,
      native: { value: "~15.4 MB / ~51 MB at 60k / 200k, estimated", source: FINDING_13 },
      fast: { value: "4,648,576 B at 60k; 16,194,304 B at 200k", source: [FINDING_13, RS_RUST] },
      scenario: none,
      rust: { value: "3,145,728 B at 64k; searcher 2,496 B", source: RS_RUST },
      "rust-classic": na,
    },
  },
  {
    quantity: "16-thread moves/s",
    cells: {
      typescript: none,
      browser: na,
      native: none,
      fast: { value: "82,364,000", source: RS_RUST },
      scenario: none,
      rust: { value: "129,483,860", source: RS_RUST },
      "rust-classic": na,
    },
  },
  {
    quantity: "Latent / hidden-board mode",
    cells: {
      typescript: { value: "yes (LatentBoardOptions)", source: TS_ENGINE },
      browser: { value: "no", source: BROWSER_SEARCH },
      native: { value: "no", source: NATIVE_ENGINE },
      fast: { value: "no", source: FAST_ENGINE },
      scenario: { value: "yes (LatentRevealSource)", source: SCENARIO_ENGINE },
      rust: { value: "no", source: RS_RUST },
      "rust-classic": { value: "yes (explicit inputs)", source: CLASSIC_RS },
    },
  },
  {
    quantity: "Idle-host measurement",
    cells: {
      typescript: { value: "none", source: null },
      browser: { value: "none", source: null },
      native: { value: "none", source: null },
      fast: { value: "none", source: null },
      scenario: { value: "none", source: null },
      rust: { value: "none", source: null },
      "rust-classic": { value: "none", source: null },
    },
  },
];

/** Verbatim from the limitations of RS-20260824T075451Z-e89ea128. */
export const MACHINE_CAVEAT =
  "Timing measured on a shared workstation (load average 1.1-1.7); ratios between back-to-back arms are the trustworthy quantity, absolute nanoseconds are not.";

/** Every distinct source named in a row, in first-appearance order. */
export function rowSources(row: ComparisonRow): string[] {
  const seen = new Set<string>();
  for (const slug of COMPARISON_SLUGS) {
    const source = row.cells[slug].source;
    if (source === null) continue;
    for (const item of Array.isArray(source) ? source : [source]) seen.add(item);
  }
  return [...seen];
}

/** A short tag for a source, used inside a cell when a row has several sources. */
export function shortSource(source: string): string {
  if (/^(RS|RUN|CT|MACH)-/.test(source)) return `${source.slice(0, 2)}-…${source.slice(-8)}`;
  if (source.startsWith("docs/")) return source.replace(/^docs\/(exploratory\/|research\/)?/, "").replace(/\.md$/, "");
  return source.split("/").at(-1) ?? source;
}

/* ---- Gates and open questions (audit sections 1.3 and 5), for the agent accordion ---- */

export interface GateGroup {
  file: string;
  tests: readonly string[];
}

export const ENGINE_GATES: readonly GateGroup[] = [
  {
    file: "src/core/typescript/engine.test.ts",
    tests: [
      "scoring constants match the original game",
      "gravity preserves the order of discs in every column",
      "line counts stop at gaps and include covered discs",
      "a wave clears all matching discs simultaneously",
      "two hits in one wave fully reveal a solid disc",
      "gravity, cracks, reveals, and scoring compose across chain waves",
      "exact gray-disc outcomes retain their full probability mass",
      "the Hardcore game starts above a solid row and cracks it on a 1",
      "clearing the board awards the original screen-clear bonus",
      "every fifth move raises a solid row and awards the level bonus",
      "a level-up explosion continues the fifth move's chain depth",
      "a rising row ends the game instead of discarding an occupied top cell",
      "the exact move model includes all seven next discs",
      "streamed move outcomes preserve exact probability and expected score",
      "seeded games remain settled and gravity-packed through game over",
    ],
  },
  {
    file: "src/core/typescript/engine-latent.test.ts",
    tests: [
      "a reveal uses the covered cell's predetermined latent value",
      "a reveal without a latent value is an error, not a draw",
      "latent values follow their covered cell through gravity",
      "a row rise shifts latent values up and draws the new row from the source",
      "scripted latent games are exactly reproducible",
      "moves without a latent board keep random reveals and report no latent state",
    ],
  },
  {
    file: "src/core/typescript/classic-engine.test.ts",
    tests: [
      "Classic TypeScript matches the shared native conformance transition",
      "Classic uses a decreasing 30-drop clock and 7,000-point rise bonus",
      "Classic incoming discs contain seven numbers and one gray outcome",
      "Classic accepts latent values for dropped gray discs",
      "an explicit Classic tape is independently replayed to completion",
    ],
  },
  {
    file: "web/lib/play/fast-search.test.ts",
    tests: [
      "fast leaf is bit-identical to evaluateHeuristic for every profile",
      "fast leaf sees every chance outcome identically, not just root boards",
      "fast move generator streams the reference outcomes in order with exact probabilities",
      "fast move generator settles a caller-supplied unsettled board like the reference",
      "fast move generator ignores illegal columns and terminal states like the reference",
      "completed depth-2 searches match the reference exactly",
      "completed depth-3 searches match the reference exactly, per profile",
      "a depth-4 search matches the reference exactly, including cache eviction",
      "work-limited searches abort at the same point and report the same partial result",
      "depth-complete callbacks fire with identical intermediate results",
      "timing: fast search against reference on the same decisions (informational)",
    ],
  },
  {
    file: "make test-native; make parity",
    tests: [
      "native-suite --gradient-check",
      "native-suite --ntuple-self-test",
      "native-suite --ntuple-search-self-test",
      "fair-depth4 --self-test",
      "approaches/baselines-diagnostics/native-parity/main.ts over seeds 0x2d700000 + 0..255",
    ],
  },
  {
    file: "build/fast-engine (finding-13 section 10)",
    tests: ["gate-leaf", "gate-search", "gate-trajectory", "fast-engine-memo/gate.cpp", "fast-reveal-sampling/gate.cpp"],
  },
  {
    file: "build/scenario",
    tests: ["scenario-parity --seeds 4096"],
  },
  {
    file: "approaches/fair-expectimax/rust-engine (target/release)",
    tests: [
      "gate_trajectory against rust-engine/cpp/trace.cpp and rust-engine/ts/trace.ts",
      "gate_leaf against rust-engine/cpp/leaf_trace.cpp",
      "gate_search against rust-engine/cpp/search_trace.cpp",
    ],
  },
  {
    file: "src/core/rust/classic-engine (cargo test)",
    tests: [
      "clock_and_bonus_match_classic",
      "gray_drop_keeps_its_hidden_value_through_gravity",
      "level_boundary_uses_seven_thousand_and_a_decreasing_clock",
      "shared_typescript_conformance_transition",
    ],
  },
];

export const OPEN_QUESTIONS: readonly string[] = [
  "Gitignored run artifacts: benchmark.json, bench.log and gates.log for the Rust run, the memo gates.log, the M6 equivalence and timing JSON, and the arm64 machine profile MACH-20260825T045112Z-2d4c14b8 are all under runs/ and absent from this checkout. README figures that exist only there cannot be checked against their source here.",
  "\"14.1x in a clean run\" (RS-20260824T075451Z-e89ea128 summary): no run record, artifact reference or limitation describes that clean run. It is not printed on this site until the owner confirms what it refers to.",
  "Which C++ fast arm the Rust benchmark compared against: rust-engine/build.sh compiles search_bench.cpp against the variant-search driver; whether that arm carried the one-entry leaf memo is not stated in the record. The 1.18x and 1.11x search ratios should be read as \"vs the C++ fast search as built by rust-engine/build.sh\".",
  "Machine and load for audit-02 section M5's 4,193.6 ns leaf figure are not stated in that section.",
  "The M6 README's artifact path runs/RUN-20260823T215500Z-sol/fastm6/ matches neither run ID (RUN-20260824T005500Z-4cde9171 and RUN-20260823T215500Z-fc74e0b4 exist); the result record RS-20260824T010000Z-8f3e9b4f is the citation.",
  "The Rust Classic engine's intended consumer: infra/README.md mentions a multi-hour Rust policy game in the submission context and the contribution record says \"for future research use\", but nothing in the tree calls the crate today.",
  "Hashes: src/core/native/engine.hpp still hashes to the value pinned in research/benchmarks/baselines-v1.json (b6dcde5f…3090). src/core/typescript/engine.ts has grown to 874 lines since audit-01, so audit-01's TypeScript hash is quoted only as \"at the time of the audit\".",
  "The policy-layer move loop (audit-01 M3): finding-13's anchor gate and the Rust engine exercise playMoveSampled indirectly through search-parity gates, but no gate compares playMoveSampled to engine.hpp::playMove directly. This site keeps audit-01's wording.",
  "Browser solver environment: no Node, V8 or browser version is recorded for the browser port anywhere; even an informational figure would lack a machine profile.",
  "The KF linear-Q Rust transfer and pruned-search pilot experiments (EX-20260902-…-4328a730, EX-20260902-…-bf465b1d) reference the Rust engine run and were outside the engines audit's scope; they may add Rust-side policies or leaf variants.",
  "\"98 reads\" and \"about 70 bit operations\" per wave appear only in README captions (fast-engine and rust-engine); finding-13 records only \"on the order of 1,300 board reads per wave\" for the reference.",
];
