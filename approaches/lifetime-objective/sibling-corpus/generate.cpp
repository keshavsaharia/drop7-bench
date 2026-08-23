// Generates a training corpus for a public-state afterstate evaluator whose
// target is SURVIVAL, not score.
//
// Rationale (see docs/exploratory/finding-01-score-is-survival.md): measured
// over 64 fair-D4 games, score correlates with lifetime at r = 0.9995 and 94.3%
// of all points are the flat row-rise bonus.  Predicting remaining lifetime is
// therefore predicting score, but with no heavy tail, no 17,000-point
// quantization, and one label per move instead of one per game.
//
// Two products are written:
//
//   *.states  fixed-width records for every visited public state, labelled with
//             the exact remaining lifetime of that game.  Cheap: one label per
//             move, no branching, no rollout-policy bias.
//
//   *.panel   for a sampled subset of roots, the resolved afterstate of EVERY
//             legal column under the same environment tape (common random
//             numbers are automatic here because the engine derives reveal
//             randomness from (gameSeed, movesPlayed), which is identical
//             across siblings).  This is the offline all-sibling ranking panel
//             the benchmark contract requires, and it is what previous learned
//             leaves in this repository lacked.
//
// Behaviour diversity, not per-root branching, is what gives the evaluator
// coverage of actions the reference would never choose: the generator mixes
// search depths and injects epsilon-random legal deviations, recording which
// moves were deviations so they can be included or excluded by arm.

#include "fair-only-depth4-noentry.cpp"

#include "../../../approaches/lifetime-objective/common/harness.hpp"
#include "../fast-engine/fast-search.hpp"

#include <sys/resource.h>

#include <memory>

#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace drop7::corpus {

namespace frozen = drop7::fair_only_horizon;
using drop7::lifetime::occupiedCells;
using drop7::lifetime::topOccupiedRow;

#pragma pack(push, 1)
struct StateRecord {
  std::uint8_t board[kCellCount];  // pre-move public board, values 0..9
  std::uint8_t nextDisc;           // 1..7
  std::uint8_t movesRemaining;     // 1..5 until the next rise
  std::uint8_t legalMask;          // bit c set when column c is legal
  std::uint8_t chosenColumn;
  std::uint16_t movesToDeath;      // label: moves this game still had left
  std::uint16_t risesToDeath;      // label: rises this game still had left
  std::uint8_t clearsThisMove;
  std::uint8_t revealsThisMove;
  std::uint8_t behaviorDepth;
  std::uint8_t explored;           // 1 when the move was an epsilon deviation
  std::uint8_t censoredGame;       // 1 when the source game hit the move cap
  std::uint8_t occupiedCells;
  std::uint16_t moveIndex;
  std::uint32_t gameSeed;
  std::uint8_t padding[3];
};

struct PanelRecord {
  std::uint8_t board[kCellCount];   // pre-move public board
  std::uint8_t nextDisc;
  std::uint8_t movesRemaining;
  std::uint8_t legalMask;
  std::uint8_t chosenColumn;        // what the behaviour policy actually played
  std::uint8_t referenceColumn;     // what unmodified fair D4 would play
  // Per column: the resolved afterstate and its immediate effects.  Illegal
  // columns are written with survived = 0 and a zeroed board.
  std::uint8_t afterBoard[kBoardSize][kCellCount];
  std::uint8_t survived[kBoardSize];        // 0 when the move ends the game
  std::uint8_t afterClears[kBoardSize];
  std::uint8_t afterReveals[kBoardSize];
  std::uint8_t afterMaxDepth[kBoardSize];
  std::uint16_t afterScoreDelta[kBoardSize];
  std::uint16_t moveIndex;
  std::uint32_t gameSeed;
};
#pragma pack(pop)

static_assert(sizeof(StateRecord) == 72, "StateRecord must stay 72 bytes");

// ---------------------------------------------------------------------------
// Parameterized fair search, identical in semantics to the frozen reference at
// its default parameters (proved by the --parity gate in
// approaches/lifetime-objective/risk-calibration).
// ---------------------------------------------------------------------------
class WorkLimitReached : public std::exception {};

struct CacheEntry {
  double value = 0.0;
  std::list<std::string>::iterator order;
};

struct SearchContext {
  std::unordered_map<std::string, CacheEntry> cache;
  std::list<std::string> order;
  std::uint64_t work = 0;
};

class Search {
 public:
  Search(int depth, double terminalUtility, std::uint64_t maximumWork)
      : depth_(depth), terminalUtility_(terminalUtility), maximumWork_(maximumWork) {}

  int chooseAction(const State& source, std::uint64_t& work) {
    if (source.game_over) return -1;
    bool mirrored = false;
    const State canonical = cfpi::detail::canonicalState(source, mirrored);
    SearchContext context;
    int action = -1;
    for (int depth = 1; depth <= depth_; ++depth) {
      try {
        const int candidate = rootDecision(canonical, depth, context);
        if (candidate < 0) break;
        action = candidate;
      } catch (const WorkLimitReached&) {
        break;
      }
    }
    if (action < 0) action = centerFirstMove(canonical.board);
    work += context.work;
    return mirrored && action >= 0 ? kBoardSize - 1 - action : action;
  }

 private:
  void checkBudget(const SearchContext& context) const {
    if (context.work >= maximumWork_) throw WorkLimitReached{};
  }

  void cacheValue(SearchContext& context, std::string key, double value) const {
    const auto prior = context.cache.find(key);
    if (prior != context.cache.end()) {
      context.order.erase(prior->second.order);
      context.cache.erase(prior);
    }
    while (context.cache.size() >= 60'000) {
      const std::string& oldest = context.order.front();
      context.cache.erase(oldest);
      context.order.pop_front();
    }
    context.order.push_back(std::move(key));
    const auto order = std::prev(context.order.end());
    context.cache.emplace(*order, CacheEntry{value, order});
  }

  double evaluateAction(const State& state, int column, int depth,
                        SearchContext& context) const {
    const std::uint32_t stateSeed =
        cfpi::detail::scenarioSeedForState(state, frozen::kPolicySeed, depth);
    double value = 0.0;
    for (int sample = 0; sample < frozen::kChanceSamples; ++sample) {
      checkBudget(context);
      cfpi::detail::StratifiedRandom random{stateSeed, sample,
                                            frozen::kChanceSamples, 0};
      MoveResult move;
      const bool played =
          cfpi::detail::playMoveSampled(state, column, random, move);
      ++context.work;
      if (!played) {
        value += terminalUtility_;
        continue;
      }
      const double scoreDelta = static_cast<double>(move.score_delta);
      if (move.state.game_over) {
        value += scoreDelta + terminalUtility_;
        continue;
      }
      move.state.score = 0;
      move.state.next_disc = cfpi::detail::sampledNextDisc(
          stateSeed, sample, frozen::kChanceSamples);
      bool ignored = false;
      const State next = cfpi::detail::canonicalState(move.state, ignored);
      value += scoreDelta + bestFutureValue(next, depth - 1, context);
    }
    return value / frozen::kChanceSamples;
  }

  double bestFutureValue(const State& state, int depth,
                         SearchContext& context) const {
    checkBudget(context);
    if (state.game_over) return terminalUtility_;
    if (depth == 0) {
      ++context.work;
      return frozen::fairLeaf(state);
    }
    const std::string key = cfpi::detail::dynamicStateKey(state, depth);
    const auto cached = context.cache.find(key);
    if (cached != context.cache.end()) {
      const double value = cached->second.value;
      context.order.splice(context.order.end(), context.order,
                           cached->second.order);
      return value;
    }
    double best = -std::numeric_limits<double>::infinity();
    for (const int column : cfpi::detail::kColumnOrder) {
      if (!isLegal(state.board, column)) continue;
      best = std::max(best, evaluateAction(state, column, depth, context));
    }
    if (!std::isfinite(best)) best = terminalUtility_;
    cacheValue(context, key, best);
    return best;
  }

  int rootDecision(const State& canonical, int depth,
                   SearchContext& context) const {
    int action = -1;
    double bestValue = -std::numeric_limits<double>::infinity();
    for (const int column : cfpi::detail::kColumnOrder) {
      if (!isLegal(canonical.board, column)) continue;
      const double value = evaluateAction(canonical, column, depth, context);
      if (value > bestValue) {
        bestValue = value;
        action = column;
      }
    }
    return action;
  }

  int depth_;
  double terminalUtility_;
  std::uint64_t maximumWork_;
};

// ---------------------------------------------------------------------------

struct Options {
  std::uint32_t seedStart = 0xa51d'4000u;
  int games = 256;
  int maximumMoves = 2000;
  int threads = 32;
  int depth = 2;
  double terminalUtility = frozen::kTerminalUtility;
  double epsilon = 0.05;         // probability of a uniform legal deviation
  int panelStride = 0;           // 0 disables the all-sibling panel
  std::string statesPath;
  std::string panelPath;
  std::string summaryPath;
  // --- PanelRecordV2 mode (P-SOL-v1 section 3; EX-20260823-sol-corpus-...).
  // Every field below is inert unless --panel2 or --panel2-mirror-check is
  // given; the v1 output paths above are byte-identical with these defaults.
  std::string panel2Prefix;            // enables panel2 mode; one file/engine
  std::string contEngines = "d1";      // comma list of d1|d2|d3n7m6
  int panel2K = 6;                     // continuations per sibling (1..30)
  int panel2Horizon = 48;              // continuation horizon H in moves
  int panel2Roots = 1;                 // roots kept per game (0 = all grid)
  int panel2Start = 8;                 // first grid move index
  int behaviourDiscSamples = 5;        // behaviour engine N (FactoredSearch)
  int behaviourRevealSamples = 1;      // behaviour engine M
  std::uint64_t behaviourMaxWork = 3'200'000;
  std::size_t behaviourMaxCache = 60'000;
  int mirrorCheck = 0;                 // >0: mirror-invariance gate, N roots
  std::string leaseLabel = "unspecified";
  std::string dataRole = "unspecified";
  // --- G0 ladder modes (EX-...-v2).  Inert unless one of these is given.
  int makeSyntheticRoots = 0;   // >0: write N seed-free synthetic roots
  std::string rootsOut;         // output path for a roots file
  std::string replaySpec;       // "seedHex:move:move,..." C0 replay harvest
  std::string ladderRoots;      // roots file to run the continuation plan on
  int ladderLimit = 0;          // 0 = all roots in the file
  int crnParity = 0;            // >0: H=1 tape-parity gate over N roots
  int fastParity = 0;           // >0: fast-vs-native decision parity gate
};

std::uint8_t legalMaskOf(const Board& board) {
  std::uint8_t mask = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    if (isLegal(board, column)) mask |= static_cast<std::uint8_t>(1u << column);
  }
  return mask;
}

struct GameOutput {
  std::vector<StateRecord> states;
  std::vector<PanelRecord> panels;
  int moves = 0;
  std::int64_t score = 0;
  bool censored = false;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
};

GameOutput generateGame(std::uint32_t seed, const Options& options,
                        Search& search) {
  GameOutput output;
  State state = initialHeadlessState(seed);
  // Exploration randomness is a policy-side domain derived only from the policy
  // seed and the game index, never from environment randomness.
  Mulberry32 explore(mix32(seed ^ 0x6f75'7421u) ^ frozen::kPolicySeed);
  std::vector<int> riseIndexOfMove;

  while (!state.game_over && state.moves_played < options.maximumMoves) {
    const Board preBoard = state.board;
    const std::uint8_t mask = legalMaskOf(preBoard);
    if (mask == 0) break;

    std::uint64_t work = 0;
    int column = search.chooseAction(state, work);
    bool explored = false;
    if (options.epsilon > 0.0 && explore.nextUnit() < options.epsilon) {
      int legalCount = 0;
      const auto legal = legalColumns(preBoard, legalCount);
      if (legalCount > 0) {
        const auto pick = static_cast<std::size_t>(
            (static_cast<std::uint64_t>(explore.nextBits()) *
             static_cast<std::uint64_t>(legalCount)) >> 32);
        const int deviation = legal[pick];
        if (deviation != column) {
          column = deviation;
          explored = true;
        }
      }
    }
    if (column < 0 || !isLegal(preBoard, column)) column = centerFirstMove(preBoard);
    if (column < 0) break;

    // All-sibling panel: resolve every legal column under the same tape.
    if (options.panelStride > 0 && state.moves_played % options.panelStride == 0) {
      PanelRecord panel{};
      std::memcpy(panel.board, preBoard.data(), kCellCount);
      panel.nextDisc = state.next_disc;
      panel.movesRemaining = static_cast<std::uint8_t>(state.moves_remaining);
      panel.legalMask = mask;
      panel.chosenColumn = static_cast<std::uint8_t>(column);
      panel.referenceColumn = static_cast<std::uint8_t>(
          drop7::fair_only_depth4::chooseDepth4Action(state).action);
      panel.moveIndex = static_cast<std::uint16_t>(state.moves_played);
      panel.gameSeed = seed;
      for (int sibling = 0; sibling < kBoardSize; ++sibling) {
        if (!isLegal(preBoard, sibling)) continue;
        State probe = state;
        MoveResult move;
        if (!playHeadlessMove(probe, seed, sibling, move)) continue;
        std::memcpy(panel.afterBoard[sibling], probe.board.data(), kCellCount);
        panel.survived[sibling] = probe.game_over ? 0 : 1;
        int clears = 0, reveals = 0, maxDepth = 0;
        for (const Wave& wave : move.waves) {
          clears += wave.cleared;
          reveals += wave.revealed;
          maxDepth = std::max(maxDepth, wave.depth);
        }
        panel.afterClears[sibling] = static_cast<std::uint8_t>(std::min(clears, 255));
        panel.afterReveals[sibling] = static_cast<std::uint8_t>(std::min(reveals, 255));
        panel.afterMaxDepth[sibling] = static_cast<std::uint8_t>(std::min(maxDepth, 255));
        panel.afterScoreDelta[sibling] = static_cast<std::uint16_t>(
            std::min<std::int64_t>(move.score_delta, 65535));
      }
      output.panels.push_back(panel);
    }

    StateRecord record{};
    std::memcpy(record.board, preBoard.data(), kCellCount);
    record.nextDisc = state.next_disc;
    record.movesRemaining = static_cast<std::uint8_t>(state.moves_remaining);
    record.legalMask = mask;
    record.chosenColumn = static_cast<std::uint8_t>(column);
    record.behaviorDepth = static_cast<std::uint8_t>(options.depth);
    record.explored = explored ? 1 : 0;
    record.occupiedCells = static_cast<std::uint8_t>(occupiedCells(preBoard));
    record.moveIndex = static_cast<std::uint16_t>(state.moves_played);
    record.gameSeed = seed;

    MoveResult move;
    if (!playHeadlessMove(state, seed, column, move)) break;

    int clears = 0, reveals = 0;
    for (const Wave& wave : move.waves) {
      clears += wave.cleared;
      reveals += wave.revealed;
    }
    record.clearsThisMove = static_cast<std::uint8_t>(std::min(clears, 255));
    record.revealsThisMove = static_cast<std::uint8_t>(std::min(reveals, 255));
    output.clears += static_cast<std::uint64_t>(clears);
    output.reveals += static_cast<std::uint64_t>(reveals);
    output.states.push_back(record);
    riseIndexOfMove.push_back(move.level_advanced ? 1 : 0);
    output.moves += 1;
  }

  output.score = state.score;
  output.censored = !state.game_over && output.moves >= options.maximumMoves;

  // Backfill the survival labels now that the game's true length is known.
  int risesAfter = 0;
  for (int index = output.moves - 1; index >= 0; --index) {
    StateRecord& record = output.states[static_cast<std::size_t>(index)];
    record.movesToDeath =
        static_cast<std::uint16_t>(output.moves - index);
    record.risesToDeath = static_cast<std::uint16_t>(risesAfter + riseIndexOfMove[static_cast<std::size_t>(index)]);
    risesAfter = record.risesToDeath;
    record.censoredGame = output.censored ? 1 : 0;
  }
  return output;
}

// ===========================================================================
// PanelRecordV2 (--panel2): sibling panels with K CRN continuation outcomes.
//
// Protocol: EX-20260823-sol-corpus-and-offline-gate-4d3d86e4 (P-SOL-v1,
// runs/RUN-20260823T191900Z-b9f8f80d/kimi-k3-main-design.md section 3).
// Everything in namespace panel2 is reachable only through --panel2 /
// --panel2-mirror-check; the v1 record layouts and code paths above are
// untouched.
//
// Layout (992 bytes per root, fixed stride; little-endian scalars):
//   header, 96 B:
//     u32 version = 0x0200; u32 recordId (sequential in seed order);
//     u32 originSeed (split bookkeeping only, never a feature);
//     u16 moveIndex; u8 rootNextDisc; u8 rootMovesToRise; u8 legalMask;
//     u8 chosenColumn; u8 referenceColumn (fair-D4 argmax, computed only when
//     recordId % 16 == 0, else 255); u8 engineId (0=D1, 1=D2, 2=D3 N7M6);
//     u8 K; u8 H; u8 panelFlags (bit0 reference computed, bit1 epsilon-root);
//     u8 rootBoard[49]; 24 B zero pad.
//   per sibling, 128 B x 7 (illegal columns keep their slot, fully zeroed):
//     u8 afterBoard[49]; u8 afterNextDisc; u8 afterMovesToRise; u8 survived;
//     u8 legal; u8 afterClears; u8 afterReveals; u8 afterMaxDepth;
//     i32 afterScoreDelta; u8 contLifetime[K]; u8 contDeathRise[K];
//     u32 contClearsTotal; u32 contRevealsTotal; zero pad to 128.
//   The two u32 continuation totals (summed numbered clears / cover reveals
//   over all K continuations, sibling move included) are an implementation
//   completion of the design's per-sibling table, which left the tail as pad;
//   they are not a protocol change.  K <= 30 as a consequence.
//
// Continuation semantics:
//   * One-step fields (afterBoard .. afterScoreDelta, survived) are resolved
//     under the true environment tape via playHeadlessMove, exactly as the v1
//     panel does (CRN across siblings is automatic there).
//   * Continuations are computed in the CANONICAL orientation of the public
//     root.  The CRN tape seed for continuation j is a pure function of the
//     canonical public root (board, next disc, moves-to-rise) and j, under
//     dedicated domain constants -- never of the origin seed.  Mirror
//     invariance of the labels is therefore exact by construction, and a
//     recordId regenerates byte-identical labels.
//   * Tape T_{r,j} is shared by all 7 siblings of root r and by every
//     continuation engine; alignment is by continuation move ordinal (each
//     ordinal reseeds its own Mulberry32 stream, so cascade length cannot
//     desynchronize siblings).
//   * contLifetime = moves survived from the root, the sibling placement
//     counting as the first move; 0 if that placement is immediately
//     terminal; capped at H.  contDeathRise = 0 when alive at H (censored),
//     else min(12, rises completed at death + 1).
// ===========================================================================

namespace panel2 {

constexpr std::uint32_t kVersion = 0x0200;
constexpr int kHeaderBytes = 96;
constexpr int kSiblingBytes = 128;
constexpr int kRecordBytes = kHeaderBytes + kBoardSize * kSiblingBytes;
static_assert(kRecordBytes == 992, "PanelRecordV2 must stay 992 bytes");
constexpr int kMaxK = 30;
constexpr std::uint32_t kTapeDomain = 0x50534f4cu;  // "PSOL"
constexpr std::uint32_t kMoveDomain = 0x434f4e54u;  // "CONT"

struct EngineSpec {
  std::uint8_t id = 0;  // 0=D1, 1=D2, 2=D3 N7M6
  int depth = 1;
  int discSamples = 5;
  int revealSamples = 1;
  std::uint64_t maximumWork = 3'200'000;
  std::size_t maximumCacheEntries = 60'000;
  std::string name;
};

// Continuation-engine identities are frozen to the configurations already on
// record: D1/D2 are the parameterized fair search at the frozen bounds
// (identical to FactoredSearch at N=5, M=1 -- gate B of the reveal-sampling
// CHECK), and D3 N7M6 is the C0 arm exactly as retained in
// runs/RUN-A525-reveal/d3-n7-m6.json (depth 3, N=7, M=6,
// maximumWork 51,084,852, maximumCacheEntries 87,025).
// worstCaseWork(3,7)+1 with b = 7 columns x 7 strata = 49 (the run.cpp
// iterative-deepening bound formula; at (4,7) it reproduces the recorded
// 11,892,399): the fast-d3s7 search always completes depth 3.
constexpr std::uint64_t kFastD3S7MaxWork = 242'698;

inline EngineSpec engineSpecByName(const std::string& name) {
  if (name == "d1") return {0, 1, 5, 1, 3'200'000, 60'000, "d1"};
  if (name == "d2") return {1, 2, 5, 1, 3'200'000, 60'000, "d2"};
  if (name == "d3n7m6") return {2, 3, 7, 6, 51'084'852, 87'025, "d3n7m6"};
  // EX-20260823-sol-corpus-and-offline-gate-v2: the fast memo-free engine
  // (finding-13, proven action/work-identical to the parameterized fair
  // search) at depth 3, seven disc strata, M = 1.
  if (name == "fastd3s7") return {3, 3, 7, 1, kFastD3S7MaxWork, 60'000, "fastd3s7"};
  // The native single-knob d3 s7 M1 search at the same bounds, for the
  // fast-vs-native parity gate only (never a corpus engine).
  if (name == "d3s7native") return {4, 3, 7, 1, kFastD3S7MaxWork, 60'000, "d3s7native"};
  throw std::invalid_argument("unknown continuation engine " + name);
}

inline std::vector<EngineSpec> parseEngineList(const std::string& list) {
  std::vector<EngineSpec> engines;
  std::string current;
  std::stringstream stream(list);
  while (std::getline(stream, current, ',')) {
    if (!current.empty()) engines.push_back(engineSpecByName(current));
  }
  if (engines.empty()) throw std::invalid_argument("--cont-engines is empty");
  return engines;
}

// ---------------------------------------------------------------------------
// Factored-chance fair search.  Faithful copy of
// approaches/lifetime-objective/reveal-sampling/search.cpp
// (drop7::lifetime::reveal::FactoredSearch, the C0 arms' engine), with the
// global bound diagnostics removed; the search arithmetic, iterative
// deepening, cache policy, and scenario indexing are line-identical.  At
// M = 1 the scenario indexing collapses to the single-knob parameterized
// search (proved by that program's gate B), so d1/d2 below are the frozen
// fair search at depths 1 and 2.
// ---------------------------------------------------------------------------

struct FactoredParameters {
  int depth = 4;
  int discSamples = frozen::kChanceSamples;
  int revealSamples = 1;
  double terminalUtility = frozen::kTerminalUtility;
  std::uint64_t maximumWork = 3'200'000;
  std::size_t maximumCacheEntries = 60'000;
};

inline FactoredParameters parametersFor(const EngineSpec& spec) {
  FactoredParameters parameters;
  parameters.depth = spec.depth;
  parameters.discSamples = spec.discSamples;
  parameters.revealSamples = spec.revealSamples;
  parameters.maximumWork = spec.maximumWork;
  parameters.maximumCacheEntries = spec.maximumCacheEntries;
  return parameters;
}

class FactoredSearch {
 public:
  explicit FactoredSearch(FactoredParameters parameters)
      : parameters_(parameters) {}

  int chooseAction(const State& source, std::uint64_t& work) {
    if (source.game_over) return -1;
    bool mirrored = false;
    const State canonical = cfpi::detail::canonicalState(source, mirrored);
    SearchContext context;
    int action = -1;
    for (int depth = 1; depth <= parameters_.depth; ++depth) {
      try {
        const int candidate = rootDecision(canonical, depth, context);
        if (candidate < 0) break;
        action = candidate;
      } catch (const WorkLimitReached&) {
        break;
      }
    }
    if (action < 0) action = centerFirstMove(canonical.board);
    work += context.work;
    return mirrored && action >= 0 ? kBoardSize - 1 - action : action;
  }

 private:
  void checkBudget(const SearchContext& context) const {
    if (context.work >= parameters_.maximumWork) throw WorkLimitReached{};
  }

  void cacheValue(SearchContext& context, std::string key, double value) const {
    const auto prior = context.cache.find(key);
    if (prior != context.cache.end()) {
      context.order.erase(prior->second.order);
      context.cache.erase(prior);
    }
    while (context.cache.size() >= parameters_.maximumCacheEntries) {
      const std::string& oldest = context.order.front();
      context.cache.erase(oldest);
      context.order.pop_front();
    }
    context.order.push_back(std::move(key));
    const auto order = std::prev(context.order.end());
    context.cache.emplace(*order, CacheEntry{value, order});
  }

  double evaluateAction(const State& state, int column, int depth,
                        SearchContext& context) const {
    const std::uint32_t stateSeed = cfpi::detail::scenarioSeedForState(
        state, frozen::kPolicySeed, depth);
    const int discSamples = parameters_.discSamples;
    const int revealSamples = parameters_.revealSamples;
    const int total = discSamples * revealSamples;
    double value = 0.0;
    for (int disc = 0; disc < discSamples; ++disc) {
      for (int rev = 0; rev < revealSamples; ++rev) {
        checkBudget(context);
        const int scenario = rev * discSamples + disc;
        cfpi::detail::StratifiedRandom random{stateSeed, scenario, total, 0};
        MoveResult move;
        const bool played =
            cfpi::detail::playMoveSampled(state, column, random, move);
        ++context.work;
        if (!played) {
          value += parameters_.terminalUtility;
          continue;
        }
        const double scoreDelta = static_cast<double>(move.score_delta);
        if (move.state.game_over) {
          value += scoreDelta + parameters_.terminalUtility;
          continue;
        }
        move.state.score = 0;
        move.state.next_disc =
            cfpi::detail::sampledNextDisc(stateSeed, disc, discSamples);
        bool ignored = false;
        const State next = cfpi::detail::canonicalState(move.state, ignored);
        value += scoreDelta + bestFutureValue(next, depth - 1, context);
      }
    }
    return value / static_cast<double>(total);
  }

  double bestFutureValue(const State& state, int depth,
                         SearchContext& context) const {
    checkBudget(context);
    if (state.game_over) return parameters_.terminalUtility;
    if (depth == 0) {
      ++context.work;
      return frozen::fairLeaf(state);
    }
    const std::string key = cfpi::detail::dynamicStateKey(state, depth);
    const auto cached = context.cache.find(key);
    if (cached != context.cache.end()) {
      const double value = cached->second.value;
      context.order.splice(context.order.end(), context.order,
                           cached->second.order);
      return value;
    }
    double best = -std::numeric_limits<double>::infinity();
    for (const int column : cfpi::detail::kColumnOrder) {
      if (!isLegal(state.board, column)) continue;
      best = std::max(best, evaluateAction(state, column, depth, context));
    }
    if (!std::isfinite(best)) best = parameters_.terminalUtility;
    cacheValue(context, key, best);
    return best;
  }

  int rootDecision(const State& canonical, int depth,
                   SearchContext& context) const {
    int action = -1;
    double bestValue = -std::numeric_limits<double>::infinity();
    for (const int column : cfpi::detail::kColumnOrder) {
      if (!isLegal(canonical.board, column)) continue;
      const double value = evaluateAction(canonical, column, depth, context);
      if (value > bestValue) {
        bestValue = value;
        action = column;
      }
    }
    return action;
  }

  FactoredParameters parameters_;
};

// ---------------------------------------------------------------------------
// CRN continuation tapes.  Seeds are pure functions of the canonical public
// root and the continuation index, under dedicated domains; the origin seed
// never enters (SL-20260823T215000Z-a5216000, rngAlgorithm note).
// ---------------------------------------------------------------------------

inline std::uint32_t publicRootHash(const State& canonicalRoot) {
  std::uint32_t hash = 0x811c'9dc5u;
  for (std::uint8_t cell : canonicalRoot.board) {
    hash ^= static_cast<std::uint32_t>(cell + 1u);
    hash *= 0x0100'0193u;
  }
  hash ^= static_cast<std::uint32_t>(canonicalRoot.next_disc);
  hash *= 0x0100'0193u;
  hash ^= static_cast<std::uint32_t>(canonicalRoot.moves_remaining);
  hash *= 0x0100'0193u;
  return hash;
}

inline std::uint32_t tapeSeedFor(std::uint32_t rootHash, int continuation) {
  return mix32(rootHash ^ kTapeDomain ^
               ((static_cast<std::uint32_t>(continuation) + 1u) *
                0x9e37'79b9u));
}

inline std::uint32_t moveSeedFor(std::uint32_t tapeSeed, int ordinal) {
  return mix32(tapeSeed ^ kMoveDomain ^
               ((static_cast<std::uint32_t>(ordinal) + 1u) * 0x85eb'ca6bu));
}

struct ContinuationOutcome {
  std::uint8_t lifetime = 0;   // moves survived, sibling move first, cap H
  std::uint8_t deathRise = 0;  // 0 = censored at H; else rise bin 1..12
  std::uint32_t clears = 0;    // numbered clears over the continuation
  std::uint32_t reveals = 0;   // cover reveals over the continuation
  std::uint64_t movesPlayed = 0;
};

// Continuation mover: FactoredSearch for the native engines, fast::FastSearch
// for engineId 3.  Both expose chooseAction(state, work&); the environment
// stepping below is engine-independent (CRN tape playMoveSampled), so engines
// differ only in the columns they choose from move 2 onward.
class ContinuationMover {
 public:
  explicit ContinuationMover(const EngineSpec& spec) {
    if (spec.id == 3) {
      fast::FastSearchParameters parameters;
      parameters.depth = spec.depth;
      parameters.chance_samples = spec.discSamples;
      parameters.maximum_work = spec.maximumWork;
      parameters.maximum_cache_entries = spec.maximumCacheEntries;
      fast_ = std::make_unique<fast::FastSearch>(parameters);
    } else {
      factored_ = std::make_unique<FactoredSearch>(parametersFor(spec));
    }
  }
  int chooseAction(const State& state, std::uint64_t& work) {
    return fast_ ? fast_->chooseAction(state, work)
                 : factored_->chooseAction(state, work);
  }

 private:
  std::unique_ptr<FactoredSearch> factored_;
  std::unique_ptr<fast::FastSearch> fast_;
};

inline ContinuationOutcome runContinuation(const State& canonicalRoot,
                                           int canonicalColumn,
                                           std::uint32_t tapeSeed, int horizon,
                                           const EngineSpec& spec) {
  ContinuationMover engine{spec};
  ContinuationOutcome outcome;
  State state = canonicalRoot;
  int column = canonicalColumn;
  int survivedMoves = 0;
  int rises = 0;
  for (int ordinal = 0;; ++ordinal) {
    Mulberry32 tape(moveSeedFor(tapeSeed, ordinal));
    MoveResult move;
    if (!cfpi::detail::playMoveSampled(state, column, tape, move)) break;
    ++outcome.movesPlayed;
    for (const Wave& wave : move.waves) {
      outcome.clears += static_cast<std::uint32_t>(wave.cleared);
      outcome.reveals += static_cast<std::uint32_t>(wave.revealed);
    }
    if (move.level_advanced) ++rises;
    state = move.state;
    if (state.game_over) break;
    ++survivedMoves;
    if (survivedMoves >= horizon) {
      outcome.lifetime = static_cast<std::uint8_t>(horizon);
      outcome.deathRise = 0;  // censored
      return outcome;
    }
    std::uint64_t work = 0;
    column = engine.chooseAction(state, work);
    if (column < 0 || !isLegal(state.board, column)) {
      column = centerFirstMove(state.board);
    }
    if (column < 0) break;
  }
  outcome.lifetime = static_cast<std::uint8_t>(std::min(survivedMoves, horizon));
  outcome.deathRise = static_cast<std::uint8_t>(std::min(rises + 1, 12));
  return outcome;
}

// ---------------------------------------------------------------------------
// Behaviour game with root staging.  Mirrors generateGame (same epsilon
// stream, same StateRecord labels) but plays through a FactoredSearch
// behaviour engine and stages full pre-move roots on the panel grid
// (moveIndex >= panel2Start, every panelStride-th move) for continuation
// labelling.
// ---------------------------------------------------------------------------

struct RootStaging {
  State rootState{};       // pre-move behaviour state, original orientation
  State canonicalRoot{};   // canonical public root used for continuations
  std::uint32_t rootHash = 0;
  bool mirroredRoot = false;
  std::uint8_t legalMask = 0;
  std::uint8_t chosenColumn = 0;
  std::uint8_t explored = 0;
  std::uint16_t moveIndex = 0;
  // One-step sibling resolution under the true environment tape (v1
  // semantics); illegal siblings stay zeroed.
  std::uint8_t afterBoard[kBoardSize][kCellCount] = {};
  std::uint8_t afterNextDisc[kBoardSize] = {};
  std::uint8_t afterMovesToRise[kBoardSize] = {};
  std::uint8_t survived[kBoardSize] = {};
  std::uint8_t afterClears[kBoardSize] = {};
  std::uint8_t afterReveals[kBoardSize] = {};
  std::uint8_t afterMaxDepth[kBoardSize] = {};
  std::int32_t afterScoreDelta[kBoardSize] = {};
};

struct GameStaging {
  std::vector<StateRecord> states;
  std::vector<RootStaging> roots;
  int moves = 0;
  std::int64_t score = 0;
  bool censored = false;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
};

inline State publicOnlyCanonicalRoot(const State& rootState, bool& mirrored) {
  State canonical = cfpi::detail::canonicalState(rootState, mirrored);
  // Continuation labels must be a pure function of the public root: the
  // board, the next disc, and the moves until the next rise.  Score is
  // already zeroed by canonicalState; level and move count are bookkeeping
  // with no effect on dynamics, and are normalized so no privileged history
  // can reach the tape or the engines.
  canonical.score = 0;
  canonical.level = 1;
  canonical.moves_played = 0;
  canonical.game_over = false;
  return canonical;
}

inline GameStaging generateGamePanel2(std::uint32_t seed,
                                      const Options& options,
                                      FactoredSearch& behaviour) {
  GameStaging output;
  State state = initialHeadlessState(seed);
  Mulberry32 explore(mix32(seed ^ 0x6f75'7421u) ^ frozen::kPolicySeed);
  std::vector<int> riseIndexOfMove;

  while (!state.game_over && state.moves_played < options.maximumMoves) {
    const Board preBoard = state.board;
    const std::uint8_t mask = legalMaskOf(preBoard);
    if (mask == 0) break;

    std::uint64_t work = 0;
    int column = behaviour.chooseAction(state, work);
    bool explored = false;
    if (options.epsilon > 0.0 && explore.nextUnit() < options.epsilon) {
      int legalCount = 0;
      const auto legal = legalColumns(preBoard, legalCount);
      if (legalCount > 0) {
        const auto pick = static_cast<std::size_t>(
            (static_cast<std::uint64_t>(explore.nextBits()) *
             static_cast<std::uint64_t>(legalCount)) >> 32);
        const int deviation = legal[pick];
        if (deviation != column) {
          column = deviation;
          explored = true;
        }
      }
    }
    if (column < 0 || !isLegal(preBoard, column)) column = centerFirstMove(preBoard);
    if (column < 0) break;

    const bool onGrid =
        state.moves_played >= options.panel2Start &&
        (state.moves_played - options.panel2Start) % options.panelStride == 0;
    if (onGrid) {
      RootStaging root;
      root.rootState = state;
      root.legalMask = mask;
      root.chosenColumn = static_cast<std::uint8_t>(column);
      root.explored = explored ? 1 : 0;
      root.moveIndex = static_cast<std::uint16_t>(state.moves_played);
      root.canonicalRoot = publicOnlyCanonicalRoot(state, root.mirroredRoot);
      root.rootHash = publicRootHash(root.canonicalRoot);
      for (int sibling = 0; sibling < kBoardSize; ++sibling) {
        if (!isLegal(preBoard, sibling)) continue;
        State probe = state;
        MoveResult move;
        if (!playHeadlessMove(probe, seed, sibling, move)) continue;
        std::memcpy(root.afterBoard[sibling], probe.board.data(), kCellCount);
        root.afterNextDisc[sibling] = probe.next_disc;
        root.afterMovesToRise[sibling] =
            static_cast<std::uint8_t>(probe.moves_remaining);
        root.survived[sibling] = probe.game_over ? 0 : 1;
        int clears = 0, reveals = 0, maxDepth = 0;
        for (const Wave& wave : move.waves) {
          clears += wave.cleared;
          reveals += wave.revealed;
          maxDepth = std::max(maxDepth, wave.depth);
        }
        root.afterClears[sibling] =
            static_cast<std::uint8_t>(std::min(clears, 255));
        root.afterReveals[sibling] =
            static_cast<std::uint8_t>(std::min(reveals, 255));
        root.afterMaxDepth[sibling] =
            static_cast<std::uint8_t>(std::min(maxDepth, 255));
        root.afterScoreDelta[sibling] = static_cast<std::int32_t>(
            std::min<std::int64_t>(move.score_delta,
                                   std::numeric_limits<std::int32_t>::max()));
      }
      output.roots.push_back(std::move(root));
    }

    StateRecord record{};
    std::memcpy(record.board, preBoard.data(), kCellCount);
    record.nextDisc = state.next_disc;
    record.movesRemaining = static_cast<std::uint8_t>(state.moves_remaining);
    record.legalMask = mask;
    record.chosenColumn = static_cast<std::uint8_t>(column);
    record.behaviorDepth = static_cast<std::uint8_t>(options.depth);
    record.explored = explored ? 1 : 0;
    record.occupiedCells = static_cast<std::uint8_t>(occupiedCells(preBoard));
    record.moveIndex = static_cast<std::uint16_t>(state.moves_played);
    record.gameSeed = seed;

    MoveResult move;
    if (!playHeadlessMove(state, seed, column, move)) break;

    int clears = 0, reveals = 0;
    for (const Wave& wave : move.waves) {
      clears += wave.cleared;
      reveals += wave.revealed;
    }
    record.clearsThisMove = static_cast<std::uint8_t>(std::min(clears, 255));
    record.revealsThisMove = static_cast<std::uint8_t>(std::min(reveals, 255));
    output.clears += static_cast<std::uint64_t>(clears);
    output.reveals += static_cast<std::uint64_t>(reveals);
    output.states.push_back(record);
    riseIndexOfMove.push_back(move.level_advanced ? 1 : 0);
    output.moves += 1;
  }

  output.score = state.score;
  output.censored = !state.game_over && output.moves >= options.maximumMoves;

  int risesAfter = 0;
  for (int index = output.moves - 1; index >= 0; --index) {
    StateRecord& record = output.states[static_cast<std::size_t>(index)];
    record.movesToDeath = static_cast<std::uint16_t>(output.moves - index);
    record.risesToDeath = static_cast<std::uint16_t>(
        risesAfter + riseIndexOfMove[static_cast<std::size_t>(index)]);
    risesAfter = record.risesToDeath;
    record.censoredGame = output.censored ? 1 : 0;
  }

  // Disclosed root-selection rule: keep panel2Roots roots per game, evenly
  // spaced over the game's grid roots (index floor((i + 0.5) * n / R)); at
  // R = 1 this is the median grid root.  0 keeps every grid root.
  if (options.panel2Roots > 0 &&
      output.roots.size() > static_cast<std::size_t>(options.panel2Roots)) {
    const std::size_t n = output.roots.size();
    const std::size_t r = static_cast<std::size_t>(options.panel2Roots);
    std::vector<RootStaging> kept;
    kept.reserve(r);
    for (std::size_t i = 0; i < r; ++i) {
      kept.push_back(std::move(output.roots[(2 * i + 1) * n / (2 * r)]));
    }
    output.roots = std::move(kept);
  }
  return output;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

template <typename Scalar>
inline void putScalar(std::uint8_t* out, Scalar value) {
  std::memcpy(out, &value, sizeof(Scalar));  // little-endian host assumed
}

inline void serializeRecord(std::uint8_t* out, const RootStaging& root,
                            std::uint32_t recordId, std::uint32_t originSeed,
                            const EngineSpec& spec, int k, int horizon,
                            std::uint8_t referenceColumn,
                            std::uint8_t panelFlags,
                            const ContinuationOutcome* outcomes /* [7][K] */) {
  std::memset(out, 0, kRecordBytes);
  putScalar<std::uint32_t>(out + 0, kVersion);
  putScalar<std::uint32_t>(out + 4, recordId);
  putScalar<std::uint32_t>(out + 8, originSeed);
  putScalar<std::uint16_t>(out + 12, root.moveIndex);
  out[14] = root.rootState.next_disc;
  out[15] = static_cast<std::uint8_t>(root.rootState.moves_remaining);
  out[16] = root.legalMask;
  out[17] = root.chosenColumn;
  out[18] = referenceColumn;
  out[19] = spec.id;
  out[20] = static_cast<std::uint8_t>(k);
  out[21] = static_cast<std::uint8_t>(horizon);
  out[22] = panelFlags;
  std::memcpy(out + 23, root.rootState.board.data(), kCellCount);
  for (int sibling = 0; sibling < kBoardSize; ++sibling) {
    std::uint8_t* slot = out + kHeaderBytes + sibling * kSiblingBytes;
    const bool legal = (root.legalMask >> sibling) & 1u;
    if (!legal) continue;  // illegal columns keep their slot, zeroed
    std::memcpy(slot, root.afterBoard[sibling], kCellCount);
    slot[49] = root.afterNextDisc[sibling];
    slot[50] = root.afterMovesToRise[sibling];
    slot[51] = root.survived[sibling];
    slot[52] = 1;  // legal
    slot[53] = root.afterClears[sibling];
    slot[54] = root.afterReveals[sibling];
    slot[55] = root.afterMaxDepth[sibling];
    putScalar<std::int32_t>(slot + 56, root.afterScoreDelta[sibling]);
    std::uint32_t clearsTotal = 0, revealsTotal = 0;
    for (int j = 0; j < k; ++j) {
      const ContinuationOutcome& outcome = outcomes[sibling * k + j];
      slot[60 + j] = outcome.lifetime;
      slot[60 + k + j] = outcome.deathRise;
      clearsTotal += outcome.clears;
      revealsTotal += outcome.reveals;
    }
    putScalar<std::uint32_t>(slot + 60 + 2 * k, clearsTotal);
    putScalar<std::uint32_t>(slot + 64 + 2 * k, revealsTotal);
  }
}

// ---------------------------------------------------------------------------
// Continuation task pool, shared by the corpus writer and the mirror gate.
// Outcomes are stored by (root, engine, sibling, continuation) index, so the
// result is deterministic for any thread count.
// ---------------------------------------------------------------------------

struct ContinuationPlan {
  const std::vector<const RootStaging*>* roots = nullptr;
  const std::vector<EngineSpec>* engines = nullptr;
  int k = 0;
  int horizon = 0;
  std::vector<ContinuationOutcome> outcomes;  // [root][engine][7][K]
  std::vector<std::uint64_t> movesPerEngine;

  std::size_t indexOf(std::size_t rootIndex, std::size_t engineIndex,
                      int sibling, int continuation) const {
    return ((rootIndex * engines->size() + engineIndex) * kBoardSize +
            static_cast<std::size_t>(sibling)) * static_cast<std::size_t>(k) +
           static_cast<std::size_t>(continuation);
  }
};

inline void runContinuations(ContinuationPlan& plan, int threads) {
  struct Task {
    std::uint32_t rootIndex;
    std::uint16_t engineIndex;
    std::uint8_t sibling;
    std::uint8_t continuation;
  };
  std::vector<Task> tasks;
  for (std::size_t rootIndex = 0; rootIndex < plan.roots->size(); ++rootIndex) {
    const RootStaging& root = *(*plan.roots)[rootIndex];
    for (std::size_t engineIndex = 0; engineIndex < plan.engines->size();
         ++engineIndex) {
      for (int sibling = 0; sibling < kBoardSize; ++sibling) {
        if (!((root.legalMask >> sibling) & 1u)) continue;
        for (int j = 0; j < plan.k; ++j) {
          tasks.push_back({static_cast<std::uint32_t>(rootIndex),
                           static_cast<std::uint16_t>(engineIndex),
                           static_cast<std::uint8_t>(sibling),
                           static_cast<std::uint8_t>(j)});
        }
      }
    }
  }
  plan.outcomes.assign(
      plan.roots->size() * plan.engines->size() * kBoardSize *
          static_cast<std::size_t>(plan.k),
      ContinuationOutcome{});
  std::vector<std::atomic<std::uint64_t>> engineMoves(plan.engines->size());
  for (auto& counter : engineMoves) counter.store(0);

  std::atomic<std::size_t> nextTask{0};
  const int workerCount =
      std::max(1, std::min<int>(threads, static_cast<int>(tasks.size())));
  std::vector<std::thread> pool;
  for (int worker = 0; worker < workerCount; ++worker) {
    pool.emplace_back([&]() {
      for (;;) {
        const std::size_t taskIndex = nextTask.fetch_add(1);
        if (taskIndex >= tasks.size()) return;
        const Task& task = tasks[taskIndex];
        const RootStaging& root = *(*plan.roots)[task.rootIndex];
        const EngineSpec& spec = (*plan.engines)[task.engineIndex];
        const int canonicalColumn =
            root.mirroredRoot ? kBoardSize - 1 - task.sibling : task.sibling;
        const std::uint32_t tapeSeed =
            tapeSeedFor(root.rootHash, task.continuation);
        const ContinuationOutcome outcome =
            runContinuation(root.canonicalRoot, canonicalColumn, tapeSeed,
                            plan.horizon, spec);
        plan.outcomes[plan.indexOf(task.rootIndex, task.engineIndex,
                                   task.sibling, task.continuation)] = outcome;
        engineMoves[task.engineIndex].fetch_add(outcome.movesPlayed);
      }
    });
  }
  for (std::thread& thread : pool) thread.join();
  plan.movesPerEngine.clear();
  for (auto& counter : engineMoves) plan.movesPerEngine.push_back(counter.load());
}

}  // namespace panel2

// ===========================================================================
// G0 ladder harness (EX-20260823-sol-corpus-and-offline-gate-v2).
//
// Everything in namespace ladder is reachable only through the --make-*,
// --ladder-roots, --fast-parity and --crn-parity options; the v1 and panel2
// code paths above are untouched.
//
// Root pools are stored as JSON lines:
//   {"tag": 17, "kind": "synthetic", "moveIndex": 42,
//    "board": "<49 digits 0-9>", "nextDisc": 4, "movesRemaining": 5}
//
// Synthetic pool rule (disclosed): for pool index i, the generation tape is
// tapeSeed = mix32(kPoolTapeDomain ^ (i+1)*0x9e3779b9); the game starts from
// the empty board with next_disc drawn from Mulberry32(mix32(tapeSeed ^
// kPoolDiscDomain)) and moves_remaining 5, is played forward by the
// fast-d3s7 reference with environment randomness
// Mulberry32(mix32(tapeSeed ^ kPoolMoveDomain ^ (ordinal+1)*0x85ebca6b))
// per move, and the root is the pre-move state at move
// 8 + mix32(tapeSeed ^ kPoolDepthDomain) % 53 (8..60).  If the game dies
// first, the last pre-move state with >= 2 legal columns and >= 8 moves
// played is kept; if none exists the index is skipped and counted.  No
// engine seed is consumed anywhere on this path.
// ===========================================================================

double processCpuSeconds();

namespace ladder {

constexpr std::uint32_t kPoolTapeDomain = 0x524f'4f54u;   // "ROOT"
constexpr std::uint32_t kPoolMoveDomain = 0x504f'4f4cu;   // "POOL"
constexpr std::uint32_t kPoolDiscDomain = 0x4449'5343u;   // "DISC"
constexpr std::uint32_t kPoolDepthDomain = 0x4445'5054u;  // "DEPT"

struct PoolRoot {
  std::uint32_t tag = 0;
  std::string kind;
  std::uint16_t moveIndex = 0;
  State state{};
};

inline int popcount8(std::uint8_t mask) {
  int bits = 0;
  for (int i = 0; i < 8; ++i) bits += (mask >> i) & 1;
  return bits;
}

inline void writeRoots(const std::string& path,
                       const std::vector<PoolRoot>& roots) {
  std::ofstream file(path);
  if (!file) throw std::runtime_error("cannot open " + path);
  for (const PoolRoot& root : roots) {
    file << "{\"tag\": " << root.tag << ", \"kind\": \"" << root.kind
         << "\", \"moveIndex\": " << root.moveIndex << ", \"board\": \"";
    for (std::uint8_t cell : root.state.board)
      file << static_cast<char>('0' + cell);
    file << "\", \"nextDisc\": " << static_cast<int>(root.state.next_disc)
         << ", \"movesRemaining\": " << root.state.moves_remaining << "}\n";
  }
}

inline std::string jsonField(const std::string& line, const std::string& key) {
  const std::string needle = "\"" + key + "\": ";
  const std::size_t at = line.find(needle);
  if (at == std::string::npos)
    throw std::runtime_error("roots file missing field " + key);
  std::size_t start = at + needle.size();
  bool quoted = line[start] == '"';
  if (quoted) ++start;
  std::size_t end = start;
  while (end < line.size() &&
         (quoted ? line[end] != '"'
                 : (line[end] != ',' && line[end] != '}')))
    ++end;
  return line.substr(start, end - start);
}

inline std::vector<PoolRoot> readRoots(const std::string& path, int limit) {
  std::ifstream file(path);
  if (!file) throw std::runtime_error("cannot open " + path);
  std::vector<PoolRoot> roots;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty()) continue;
    PoolRoot root;
    root.tag = static_cast<std::uint32_t>(std::stoul(jsonField(line, "tag")));
    root.kind = jsonField(line, "kind");
    root.moveIndex =
        static_cast<std::uint16_t>(std::stoul(jsonField(line, "moveIndex")));
    const std::string board = jsonField(line, "board");
    if (board.size() != static_cast<std::size_t>(kCellCount))
      throw std::runtime_error("bad board length in roots file");
    for (int cell = 0; cell < kCellCount; ++cell) {
      root.state.board[static_cast<std::size_t>(cell)] =
          static_cast<std::uint8_t>(board[static_cast<std::size_t>(cell)] - '0');
    }
    root.state.next_disc =
        static_cast<std::uint8_t>(std::stoi(jsonField(line, "nextDisc")));
    root.state.moves_remaining = std::stoi(jsonField(line, "movesRemaining"));
    roots.push_back(std::move(root));
    if (limit > 0 && static_cast<int>(roots.size()) >= limit) break;
  }
  return roots;
}

// Play one synthetic pool game; returns true and fills the root when a state
// with >= 2 legal columns at >= 8 moves played exists on the trajectory.
inline bool makeSyntheticRoot(std::uint32_t index, PoolRoot& out) {
  const std::uint32_t tapeSeed =
      mix32(kPoolTapeDomain ^ ((index + 1u) * 0x9e37'79b9u));
  const int targetDepth =
      8 + static_cast<int>(mix32(tapeSeed ^ kPoolDepthDomain) % 53u);
  State state{};
  {
    Mulberry32 disc(mix32(tapeSeed ^ kPoolDiscDomain));
    state.next_disc = disc.nextDisc();
  }
  panel2::ContinuationMover mover{panel2::engineSpecByName("fastd3s7")};
  State lastGood{};
  bool haveGood = false;
  for (int ordinal = 0; ordinal <= targetDepth; ++ordinal) {
    if (state.game_over) break;
    const std::uint8_t mask = legalMaskOf(state.board);
    if (mask == 0) break;
    if (popcount8(mask) >= 2 && state.moves_played >= 8) {
      lastGood = state;
      haveGood = true;
      if (state.moves_played == targetDepth) break;
    }
    std::uint64_t work = 0;
    int column = mover.chooseAction(state, work);
    if (column < 0 || !isLegal(state.board, column))
      column = centerFirstMove(state.board);
    if (column < 0) break;
    Mulberry32 tape(mix32(tapeSeed ^ kPoolMoveDomain ^
                          ((static_cast<std::uint32_t>(ordinal) + 1u) *
                           0x85eb'ca6bu)));
    MoveResult move;
    if (!cfpi::detail::playMoveSampled(state, column, tape, move)) break;
    state = move.state;
  }
  if (!haveGood) return false;
  out.tag = index;
  out.kind = "synthetic";
  out.moveIndex = static_cast<std::uint16_t>(lastGood.moves_played);
  lastGood.score = 0;
  out.state = lastGood;
  return true;
}

inline int runMakeSyntheticRoots(const Options& options) {
  const int wanted = options.makeSyntheticRoots;
  const std::uint32_t budget = static_cast<std::uint32_t>(wanted) * 2u;
  std::vector<PoolRoot> candidates(budget);
  std::vector<std::uint8_t> valid(budget, 0);
  std::atomic<std::uint32_t> nextIndex{0};
  const int threads = std::max(1, options.threads);
  std::vector<std::thread> pool;
  for (int worker = 0; worker < threads; ++worker) {
    pool.emplace_back([&]() {
      for (;;) {
        const std::uint32_t index = nextIndex.fetch_add(1);
        if (index >= budget) return;
        valid[index] = makeSyntheticRoot(index, candidates[index]) ? 1 : 0;
      }
    });
  }
  for (std::thread& thread : pool) thread.join();
  std::vector<PoolRoot> roots;
  int skipped = 0;
  for (std::uint32_t index = 0;
       index < budget && static_cast<int>(roots.size()) < wanted; ++index) {
    if (valid[index]) roots.push_back(candidates[index]);
    else ++skipped;
  }
  if (static_cast<int>(roots.size()) < wanted)
    throw std::runtime_error("synthetic pool exhausted its 2x index budget");
  writeRoots(options.rootsOut, roots);
  std::cout << "{\"mode\": \"make-synthetic-roots\", \"roots\": "
            << roots.size() << ", \"skippedIndices\": " << skipped
            << ", \"out\": \"" << options.rootsOut << "\"}\n";
  return 0;
}

// Replay retained C0 games (already-read cohort; data status unchanged) with
// the exact C0 engine to harvest realistic roots.  --replay-spec is
// "seedHex:move:move[,...]"; every game is replayed to its end and the final
// (moves, score) printed so the caller can assert identity with the retained
// artifact.
inline int runReplayRoots(const Options& options) {
  struct Item {
    std::uint32_t seed;
    std::vector<int> captures;
  };
  std::vector<Item> items;
  {
    std::stringstream stream(options.replaySpec);
    std::string part;
    while (std::getline(stream, part, ',')) {
      if (part.empty()) continue;
      std::stringstream inner(part);
      std::string token;
      Item item{};
      int field = 0;
      while (std::getline(inner, token, ':')) {
        if (field == 0)
          item.seed = static_cast<std::uint32_t>(std::stoul(token, nullptr, 0));
        else
          item.captures.push_back(std::stoi(token));
        ++field;
      }
      items.push_back(std::move(item));
    }
  }
  std::vector<std::vector<PoolRoot>> captured(items.size());
  std::vector<std::string> finals(items.size());
  std::atomic<std::size_t> nextItem{0};
  const int threads =
      std::max(1, std::min<int>(options.threads, static_cast<int>(items.size())));
  std::vector<std::thread> pool;
  for (int worker = 0; worker < threads; ++worker) {
    pool.emplace_back([&]() {
      for (;;) {
        const std::size_t itemIndex = nextItem.fetch_add(1);
        if (itemIndex >= items.size()) return;
        const Item& item = items[itemIndex];
        panel2::ContinuationMover mover{panel2::engineSpecByName("d3n7m6")};
        State state = initialHeadlessState(item.seed);
        while (!state.game_over && state.moves_played < options.maximumMoves) {
          if (legalMaskOf(state.board) == 0) break;
          for (const int capture : item.captures) {
            if (state.moves_played == capture) {
              PoolRoot root;
              root.tag = item.seed;
              root.kind = "c0";
              root.moveIndex = static_cast<std::uint16_t>(capture);
              root.state = state;
              root.state.score = 0;
              captured[itemIndex].push_back(std::move(root));
            }
          }
          std::uint64_t work = 0;
          int column = mover.chooseAction(state, work);
          if (column < 0 || !isLegal(state.board, column))
            column = centerFirstMove(state.board);
          if (column < 0) break;
          MoveResult move;
          if (!playHeadlessMove(state, item.seed, column, move)) break;
        }
        std::ostringstream summary;
        summary << "{\"seedHex\": \"0x" << std::hex << item.seed << std::dec
                << "\", \"moves\": " << state.moves_played
                << ", \"score\": " << state.score << "}";
        finals[itemIndex] = summary.str();
      }
    });
  }
  for (std::thread& thread : pool) thread.join();
  std::vector<PoolRoot> roots;
  for (const auto& perItem : captured)
    for (const PoolRoot& root : perItem) roots.push_back(root);
  writeRoots(options.rootsOut, roots);
  std::cout << "{\"mode\": \"replay-roots\", \"roots\": " << roots.size()
            << ", \"finals\": [";
  for (std::size_t index = 0; index < finals.size(); ++index)
    std::cout << (index == 0 ? "" : ", ") << finals[index];
  std::cout << "], \"out\": \"" << options.rootsOut << "\"}\n";
  return 0;
}

inline panel2::RootStaging stagingFor(const PoolRoot& root) {
  panel2::RootStaging staging;
  staging.rootState = root.state;
  staging.legalMask = legalMaskOf(root.state.board);
  staging.chosenColumn = 255;
  staging.moveIndex = root.moveIndex;
  staging.canonicalRoot =
      panel2::publicOnlyCanonicalRoot(staging.rootState, staging.mirroredRoot);
  staging.rootHash = panel2::publicRootHash(staging.canonicalRoot);
  return staging;
}

// Run the continuation plan over a roots file and write one panel2 file per
// engine (ladder.py consumes them).  With --crn-parity N > 0 this instead
// runs every engine at horizon 1 over the first N roots: no engine decision
// is ever taken before the horizon, so the outcome arrays must be
// byte-identical across engines -- the CRN tape-parity gate.
inline int runLadder(const Options& options) {
  const auto engines = panel2::parseEngineList(options.contEngines);
  const auto poolRoots = readRoots(options.ladderRoots, options.ladderLimit);
  std::vector<panel2::RootStaging> staged;
  staged.reserve(poolRoots.size());
  for (const PoolRoot& root : poolRoots) staged.push_back(stagingFor(root));
  std::vector<const panel2::RootStaging*> roots;
  for (const auto& staging : staged) roots.push_back(&staging);

  const bool parityMode = options.crnParity > 0;
  if (parityMode && static_cast<int>(roots.size()) > options.crnParity)
    roots.resize(static_cast<std::size_t>(options.crnParity));

  panel2::ContinuationPlan plan;
  plan.roots = &roots;
  plan.engines = &engines;
  plan.k = options.panel2K;
  plan.horizon = parityMode ? 1 : options.panel2Horizon;
  const auto started = std::chrono::steady_clock::now();
  const double cpuAtStart = processCpuSeconds();
  panel2::runContinuations(plan, options.threads);
  const double wall =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();
  const double cpu = processCpuSeconds() - cpuAtStart;

  if (parityMode) {
    std::uint64_t comparisons = 0, mismatches = 0;
    for (std::size_t rootIndex = 0; rootIndex < roots.size(); ++rootIndex) {
      for (int sibling = 0; sibling < kBoardSize; ++sibling) {
        if (!((roots[rootIndex]->legalMask >> sibling) & 1u)) continue;
        for (int j = 0; j < plan.k; ++j) {
          const auto& reference = plan.outcomes[plan.indexOf(rootIndex, 0, sibling, j)];
          for (std::size_t engineIndex = 1; engineIndex < engines.size();
               ++engineIndex) {
            const auto& other =
                plan.outcomes[plan.indexOf(rootIndex, engineIndex, sibling, j)];
            ++comparisons;
            if (reference.lifetime != other.lifetime ||
                reference.deathRise != other.deathRise ||
                reference.clears != other.clears ||
                reference.reveals != other.reveals)
              ++mismatches;
          }
        }
      }
    }
    std::cout << "{\"gate\": \"panel2-crn-tape-parity\", \"roots\": "
              << roots.size() << ", \"engines\": \"" << options.contEngines
              << "\", \"k\": " << plan.k << ", \"comparisons\": " << comparisons
              << ", \"mismatches\": " << mismatches
              << ", \"pass\": " << (mismatches == 0 ? "true" : "false")
              << "}\n";
    return mismatches == 0 ? 0 : 1;
  }

  std::vector<std::uint8_t> buffer(panel2::kRecordBytes);
  for (std::size_t engineIndex = 0; engineIndex < engines.size();
       ++engineIndex) {
    const std::string path = options.panel2Prefix + "." +
                             engines[engineIndex].name + ".panel2";
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) throw std::runtime_error("cannot open " + path);
    for (std::size_t rootIndex = 0; rootIndex < roots.size(); ++rootIndex) {
      const std::size_t base = plan.indexOf(rootIndex, engineIndex, 0, 0);
      panel2::serializeRecord(buffer.data(), *roots[rootIndex],
                              static_cast<std::uint32_t>(rootIndex),
                              poolRoots[rootIndex].tag, engines[engineIndex],
                              plan.k, plan.horizon, 255, 0,
                              plan.outcomes.data() + base);
      std::fwrite(buffer.data(), 1, buffer.size(), file);
    }
    std::fclose(file);
  }
  std::cout << std::setprecision(10)
            << "{\"mode\": \"ladder\", \"roots\": " << roots.size()
            << ", \"engines\": \"" << options.contEngines
            << "\", \"k\": " << plan.k << ", \"horizon\": " << plan.horizon
            << ", \"movesPerEngine\": {";
  for (std::size_t engineIndex = 0; engineIndex < engines.size();
       ++engineIndex) {
    std::cout << (engineIndex == 0 ? "" : ", ") << "\""
              << engines[engineIndex].name
              << "\": " << plan.movesPerEngine[engineIndex];
  }
  std::cout << "}, \"wallSeconds\": " << wall << ", \"cpuSeconds\": " << cpu
            << ", \"threads\": " << options.threads << "}\n";
  return 0;
}

// Fast-vs-native parity gate: identical decisions from fast::FastSearch and
// the native single-knob d3 s7 M1 search on live probe games (smoke seeds
// only).  Work counts are compared and reported; the gate criterion is
// decision identity.
inline int runFastParityGate(const Options& options) {
  panel2::ContinuationMover nativeMover{panel2::engineSpecByName("d3s7native")};
  panel2::ContinuationMover fastMover{panel2::engineSpecByName("fastd3s7")};
  std::uint64_t decisions = 0, actionMismatches = 0, workMismatches = 0;
  std::uint64_t nativeWorkTotal = 0, fastWorkTotal = 0;
  for (std::uint32_t seed = options.seedStart;
       decisions < static_cast<std::uint64_t>(options.fastParity); ++seed) {
    State state = initialHeadlessState(seed);
    while (!state.game_over &&
           decisions < static_cast<std::uint64_t>(options.fastParity)) {
      if (legalMaskOf(state.board) == 0) break;
      std::uint64_t nativeWork = 0, fastWork = 0;
      const int nativeAction = nativeMover.chooseAction(state, nativeWork);
      const int fastAction = fastMover.chooseAction(state, fastWork);
      ++decisions;
      nativeWorkTotal += nativeWork;
      fastWorkTotal += fastWork;
      if (nativeAction != fastAction) ++actionMismatches;
      if (nativeWork != fastWork) ++workMismatches;
      int column = nativeAction;
      if (column < 0 || !isLegal(state.board, column))
        column = centerFirstMove(state.board);
      if (column < 0) break;
      MoveResult move;
      if (!playHeadlessMove(state, seed, column, move)) break;
    }
  }
  std::cout << "{\"gate\": \"fast-d3s7-vs-native-parity\", \"decisions\": "
            << decisions << ", \"actionMismatches\": " << actionMismatches
            << ", \"workMismatches\": " << workMismatches
            << ", \"nativeWorkTotal\": " << nativeWorkTotal
            << ", \"fastWorkTotal\": " << fastWorkTotal
            << ", \"seedStartHex\": \"0x" << std::hex << options.seedStart
            << std::dec << "\", \"pass\": "
            << (actionMismatches == 0 ? "true" : "false") << "}\n";
  return actionMismatches == 0 ? 0 : 1;
}

}  // namespace ladder

Options parseOptions(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing value");
    const std::string key = argv[index];
    const std::string value = argv[index + 1];
    if (key == "--seed-start") options.seedStart = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
    else if (key == "--games") options.games = std::stoi(value);
    else if (key == "--max-moves") options.maximumMoves = std::stoi(value);
    else if (key == "--threads") options.threads = std::stoi(value);
    else if (key == "--depth") options.depth = std::stoi(value);
    else if (key == "--terminal-utility") options.terminalUtility = std::stod(value);
    else if (key == "--epsilon") options.epsilon = std::stod(value);
    else if (key == "--panel-stride") options.panelStride = std::stoi(value);
    else if (key == "--states") options.statesPath = value;
    else if (key == "--panel") options.panelPath = value;
    else if (key == "--summary") options.summaryPath = value;
    else if (key == "--panel2") options.panel2Prefix = value;
    else if (key == "--cont-engines") options.contEngines = value;
    else if (key == "--panel2-k") options.panel2K = std::stoi(value);
    else if (key == "--panel2-horizon") options.panel2Horizon = std::stoi(value);
    else if (key == "--panel2-roots") options.panel2Roots = std::stoi(value);
    else if (key == "--panel2-start") options.panel2Start = std::stoi(value);
    else if (key == "--behaviour-disc-samples") options.behaviourDiscSamples = std::stoi(value);
    else if (key == "--behaviour-reveal-samples") options.behaviourRevealSamples = std::stoi(value);
    else if (key == "--behaviour-max-work") options.behaviourMaxWork = std::stoull(value);
    else if (key == "--behaviour-max-cache") options.behaviourMaxCache = static_cast<std::size_t>(std::stoull(value));
    else if (key == "--panel2-mirror-check") options.mirrorCheck = std::stoi(value);
    else if (key == "--lease-label") options.leaseLabel = value;
    else if (key == "--data-role") options.dataRole = value;
    else if (key == "--make-synthetic-roots") options.makeSyntheticRoots = std::stoi(value);
    else if (key == "--roots-out") options.rootsOut = value;
    else if (key == "--replay-spec") options.replaySpec = value;
    else if (key == "--ladder-roots") options.ladderRoots = value;
    else if (key == "--ladder-limit") options.ladderLimit = std::stoi(value);
    else if (key == "--crn-parity") options.crnParity = std::stoi(value);
    else if (key == "--fast-parity") options.fastParity = std::stoi(value);
    else throw std::invalid_argument("unknown option " + key);
  }
  return options;
}

// ---------------------------------------------------------------------------
// panel2 drivers (reachable only through --panel2 / --panel2-mirror-check).
// ---------------------------------------------------------------------------

double processCpuSeconds() {
  struct rusage usage {};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0.0;
  const auto seconds = [](const timeval& tv) {
    return static_cast<double>(tv.tv_sec) +
           static_cast<double>(tv.tv_usec) / 1e6;
  };
  return seconds(usage.ru_utime) + seconds(usage.ru_stime);
}

panel2::FactoredParameters behaviourParameters(const Options& options) {
  panel2::FactoredParameters parameters;
  parameters.depth = options.depth;
  parameters.discSamples = options.behaviourDiscSamples;
  parameters.revealSamples = options.behaviourRevealSamples;
  parameters.terminalUtility = options.terminalUtility;
  parameters.maximumWork = options.behaviourMaxWork;
  parameters.maximumCacheEntries = options.behaviourMaxCache;
  return parameters;
}

std::vector<panel2::GameStaging> playBehaviourGames(const Options& options) {
  std::vector<panel2::GameStaging> games(
      static_cast<std::size_t>(options.games));
  std::atomic<int> nextIndex{0};
  const int threads = std::max(1, std::min(options.threads, options.games));
  std::vector<std::thread> pool;
  for (int worker = 0; worker < threads; ++worker) {
    pool.emplace_back([&]() {
      panel2::FactoredSearch behaviour{behaviourParameters(options)};
      for (;;) {
        const int index = nextIndex.fetch_add(1);
        if (index >= options.games) return;
        const std::uint32_t seed =
            options.seedStart + static_cast<std::uint32_t>(index);
        games[static_cast<std::size_t>(index)] =
            panel2::generateGamePanel2(seed, options, behaviour);
      }
    });
  }
  for (std::thread& thread : pool) thread.join();
  return games;
}

int runMirrorCheck(const Options& options) {
  const auto engines = panel2::parseEngineList(options.contEngines);
  const auto games = playBehaviourGames(options);

  // First N staged roots in seed order.
  std::vector<const panel2::RootStaging*> roots;
  std::vector<panel2::RootStaging> mirroredRoots;
  for (const auto& game : games) {
    for (const auto& root : game.roots) {
      if (static_cast<int>(roots.size()) >= options.mirrorCheck) break;
      roots.push_back(&root);
    }
  }
  mirroredRoots.reserve(roots.size());
  for (const panel2::RootStaging* original : roots) {
    panel2::RootStaging mirrored;
    mirrored.rootState = original->rootState;
    mirrored.rootState.board =
        cfpi::detail::mirrorBoard(original->rootState.board);
    std::uint8_t mask = 0;
    for (int column = 0; column < kBoardSize; ++column) {
      if ((original->legalMask >> column) & 1u) {
        mask |= static_cast<std::uint8_t>(1u << (kBoardSize - 1 - column));
      }
    }
    mirrored.legalMask = mask;
    mirrored.moveIndex = original->moveIndex;
    mirrored.canonicalRoot =
        panel2::publicOnlyCanonicalRoot(mirrored.rootState,
                                        mirrored.mirroredRoot);
    mirrored.rootHash = panel2::publicRootHash(mirrored.canonicalRoot);
    mirroredRoots.push_back(mirrored);
  }
  std::vector<const panel2::RootStaging*> mirroredPointers;
  for (const auto& root : mirroredRoots) mirroredPointers.push_back(&root);

  panel2::ContinuationPlan originalPlan;
  originalPlan.roots = &roots;
  originalPlan.engines = &engines;
  originalPlan.k = options.panel2K;
  originalPlan.horizon = options.panel2Horizon;
  panel2::runContinuations(originalPlan, options.threads);

  panel2::ContinuationPlan mirroredPlan;
  mirroredPlan.roots = &mirroredPointers;
  mirroredPlan.engines = &engines;
  mirroredPlan.k = options.panel2K;
  mirroredPlan.horizon = options.panel2Horizon;
  panel2::runContinuations(mirroredPlan, options.threads);

  std::uint64_t comparisons = 0, mismatches = 0;
  int maxLifetimeDiff = 0, maxDeathRiseDiff = 0;
  for (std::size_t rootIndex = 0; rootIndex < roots.size(); ++rootIndex) {
    for (std::size_t engineIndex = 0; engineIndex < engines.size();
         ++engineIndex) {
      for (int sibling = 0; sibling < kBoardSize; ++sibling) {
        if (!((roots[rootIndex]->legalMask >> sibling) & 1u)) continue;
        const int mirroredSibling = kBoardSize - 1 - sibling;
        for (int j = 0; j < options.panel2K; ++j) {
          const auto& a = originalPlan.outcomes[originalPlan.indexOf(
              rootIndex, engineIndex, sibling, j)];
          const auto& b = mirroredPlan.outcomes[mirroredPlan.indexOf(
              rootIndex, engineIndex, mirroredSibling, j)];
          ++comparisons;
          const int lifetimeDiff =
              std::abs(static_cast<int>(a.lifetime) - static_cast<int>(b.lifetime));
          const int deathRiseDiff =
              std::abs(static_cast<int>(a.deathRise) - static_cast<int>(b.deathRise));
          maxLifetimeDiff = std::max(maxLifetimeDiff, lifetimeDiff);
          maxDeathRiseDiff = std::max(maxDeathRiseDiff, deathRiseDiff);
          if (lifetimeDiff != 0 || deathRiseDiff != 0 ||
              a.clears != b.clears || a.reveals != b.reveals) {
            ++mismatches;
          }
        }
      }
    }
  }
  std::cout << "{\"gate\": \"panel2-mirror-invariance\", \"roots\": "
            << roots.size() << ", \"engines\": \"" << options.contEngines
            << "\", \"k\": " << options.panel2K << ", \"horizon\": "
            << options.panel2Horizon << ", \"comparisons\": " << comparisons
            << ", \"mismatches\": " << mismatches
            << ", \"maxLifetimeDiff\": " << maxLifetimeDiff
            << ", \"maxDeathRiseDiff\": " << maxDeathRiseDiff
            << ", \"pass\": " << (mismatches == 0 ? "true" : "false")
            << "}\n";
  return mismatches == 0 ? 0 : 1;
}

int runPanel2(const Options& options) {
  const double cpuAtStart = processCpuSeconds();
  const auto startedBehaviour = std::chrono::steady_clock::now();
  const auto engines = panel2::parseEngineList(options.contEngines);
  const auto games = playBehaviourGames(options);
  const auto startedContinuations = std::chrono::steady_clock::now();

  std::vector<const panel2::RootStaging*> roots;
  std::vector<std::uint32_t> rootSeeds;
  for (std::size_t gameIndex = 0; gameIndex < games.size(); ++gameIndex) {
    for (const auto& root : games[gameIndex].roots) {
      roots.push_back(&root);
      rootSeeds.push_back(options.seedStart +
                          static_cast<std::uint32_t>(gameIndex));
    }
  }

  panel2::ContinuationPlan plan;
  plan.roots = &roots;
  plan.engines = &engines;
  plan.k = options.panel2K;
  plan.horizon = options.panel2Horizon;
  panel2::runContinuations(plan, options.threads);
  const auto startedReference = std::chrono::steady_clock::now();

  // referenceColumn: fair-D4 argmax on every 16th record (recordId is the
  // root's index in seed order, so this is deterministic for any thread
  // count); 255 elsewhere.
  std::vector<std::uint8_t> referenceColumn(roots.size(), 255);
  {
    std::vector<std::size_t> targets;
    for (std::size_t rootIndex = 0; rootIndex < roots.size(); rootIndex += 16) {
      targets.push_back(rootIndex);
    }
    std::atomic<std::size_t> nextTarget{0};
    const int threads = std::max(
        1, std::min<int>(options.threads, static_cast<int>(targets.size())));
    std::vector<std::thread> pool;
    for (int worker = 0; worker < threads; ++worker) {
      pool.emplace_back([&]() {
        for (;;) {
          const std::size_t index = nextTarget.fetch_add(1);
          if (index >= targets.size()) return;
          const std::size_t rootIndex = targets[index];
          referenceColumn[rootIndex] = static_cast<std::uint8_t>(
              drop7::fair_only_depth4::chooseDepth4Action(
                  roots[rootIndex]->rootState).action);
        }
      });
    }
    for (std::thread& thread : pool) thread.join();
  }
  const auto startedWrite = std::chrono::steady_clock::now();

  // States file, seed order (deterministic for any thread count).
  std::FILE* statesFile = std::fopen(options.statesPath.c_str(), "wb");
  if (statesFile == nullptr) {
    throw std::runtime_error("cannot open " + options.statesPath);
  }
  std::uint64_t totalStates = 0, totalMoves = 0;
  std::uint64_t totalClears = 0, totalReveals = 0;
  long long totalScore = 0;
  int censoredGames = 0;
  for (const auto& game : games) {
    if (!game.states.empty()) {
      std::fwrite(game.states.data(), sizeof(StateRecord), game.states.size(),
                  statesFile);
    }
    totalStates += game.states.size();
    totalMoves += static_cast<std::uint64_t>(game.moves);
    totalClears += game.clears;
    totalReveals += game.reveals;
    totalScore += game.score;
    if (game.censored) ++censoredGames;
  }
  std::fclose(statesFile);

  // One panel2 file per continuation engine; identical roots, identical CRN
  // tapes, engine-specific labels.
  std::vector<std::string> panelPaths;
  std::vector<std::uint8_t> buffer(panel2::kRecordBytes);
  for (std::size_t engineIndex = 0; engineIndex < engines.size();
       ++engineIndex) {
    const std::string path =
        options.panel2Prefix + "." + engines[engineIndex].name + ".panel2";
    std::FILE* panelFile = std::fopen(path.c_str(), "wb");
    if (panelFile == nullptr) throw std::runtime_error("cannot open " + path);
    for (std::size_t rootIndex = 0; rootIndex < roots.size(); ++rootIndex) {
      const panel2::RootStaging& root = *roots[rootIndex];
      std::uint8_t panelFlags = 0;
      if (referenceColumn[rootIndex] != 255) panelFlags |= 1u;
      if (root.explored) panelFlags |= 2u;
      const std::size_t base =
          plan.indexOf(rootIndex, engineIndex, 0, 0);
      panel2::serializeRecord(buffer.data(), root,
                              static_cast<std::uint32_t>(rootIndex),
                              rootSeeds[rootIndex], engines[engineIndex],
                              options.panel2K, options.panel2Horizon,
                              referenceColumn[rootIndex], panelFlags,
                              plan.outcomes.data() + base);
      std::fwrite(buffer.data(), 1, buffer.size(), panelFile);
    }
    std::fclose(panelFile);
    panelPaths.push_back(path);
  }

  const auto finished = std::chrono::steady_clock::now();
  const auto wallOf = [](auto from, auto to) {
    return std::chrono::duration<double>(to - from).count();
  };
  std::ostringstream summary;
  summary << std::setprecision(10) << "{\n"
          << "  \"format\": \"drop7-panel2-summary-v1\",\n"
          << "  \"seedLease\": \"" << options.leaseLabel << "\",\n"
          << "  \"dataRole\": \"" << options.dataRole << "\",\n"
          << "  \"seedStartHex\": \"0x" << std::hex << options.seedStart
          << std::dec << "\",\n"
          << "  \"games\": " << options.games << ",\n"
          << "  \"behaviour\": {\"depth\": " << options.depth
          << ", \"discSamples\": " << options.behaviourDiscSamples
          << ", \"revealSamples\": " << options.behaviourRevealSamples
          << ", \"maximumWork\": " << options.behaviourMaxWork
          << ", \"epsilon\": " << options.epsilon << "},\n"
          << "  \"contEngines\": \"" << options.contEngines << "\",\n"
          << "  \"k\": " << options.panel2K << ",\n"
          << "  \"horizon\": " << options.panel2Horizon << ",\n"
          << "  \"panelStride\": " << options.panelStride << ",\n"
          << "  \"panel2Start\": " << options.panel2Start << ",\n"
          << "  \"panel2RootsPerGame\": " << options.panel2Roots << ",\n"
          << "  \"panel2RecordBytes\": " << panel2::kRecordBytes << ",\n"
          << "  \"panel2Records\": " << roots.size() << ",\n"
          << "  \"stateRecords\": " << totalStates << ",\n"
          << "  \"meanMoves\": "
          << static_cast<double>(totalMoves) /
                 static_cast<double>(std::max(1, options.games)) << ",\n"
          << "  \"meanScore\": "
          << static_cast<double>(totalScore) /
                 static_cast<double>(std::max(1, options.games)) << ",\n"
          << "  \"censoredGames\": " << censoredGames << ",\n"
          << "  \"clearsPerMove\": "
          << static_cast<double>(totalClears) /
                 static_cast<double>(std::max<std::uint64_t>(1, totalMoves))
          << ",\n"
          << "  \"revealsPerMove\": "
          << static_cast<double>(totalReveals) /
                 static_cast<double>(std::max<std::uint64_t>(1, totalMoves))
          << ",\n"
          << "  \"continuationMovesPerEngine\": {";
  for (std::size_t engineIndex = 0; engineIndex < engines.size();
       ++engineIndex) {
    summary << (engineIndex == 0 ? "" : ", ") << "\""
            << engines[engineIndex].name << "\": "
            << plan.movesPerEngine[engineIndex];
  }
  summary << "},\n"
          << "  \"behaviourWallSeconds\": "
          << wallOf(startedBehaviour, startedContinuations) << ",\n"
          << "  \"continuationWallSeconds\": "
          << wallOf(startedContinuations, startedReference) << ",\n"
          << "  \"referenceWallSeconds\": "
          << wallOf(startedReference, startedWrite) << ",\n"
          << "  \"totalWallSeconds\": "
          << wallOf(startedBehaviour, finished) << ",\n"
          << "  \"cpuSeconds\": " << processCpuSeconds() - cpuAtStart << ",\n"
          << "  \"threads\": " << options.threads << "\n}\n";
  std::cout << summary.str();
  if (!options.summaryPath.empty()) {
    std::ofstream file(options.summaryPath);
    file << summary.str();
  }
  return 0;
}

}  // namespace drop7::corpus

int main(int argc, char** argv) {
  using namespace drop7;
  using namespace drop7::corpus;
  try {
    const Options options = parseOptions(argc, argv);
    // G0 ladder modes (EX-20260823-sol-corpus-and-offline-gate-v2).
    if (options.makeSyntheticRoots > 0) {
      if (options.rootsOut.empty()) throw std::invalid_argument("--roots-out is required");
      return ladder::runMakeSyntheticRoots(options);
    }
    if (!options.replaySpec.empty()) {
      if (options.rootsOut.empty()) throw std::invalid_argument("--roots-out is required");
      return ladder::runReplayRoots(options);
    }
    if (options.fastParity > 0) return ladder::runFastParityGate(options);
    if (!options.ladderRoots.empty()) {
      if (options.panel2K < 1 || options.panel2K > panel2::kMaxK)
        throw std::invalid_argument("--panel2-k must be 1..30");
      if (options.crnParity == 0 && options.panel2Prefix.empty())
        throw std::invalid_argument("--panel2 prefix is required for ladder output");
      return ladder::runLadder(options);
    }
    if (!options.panel2Prefix.empty() || options.mirrorCheck > 0) {
      // PanelRecordV2 mode (P-SOL-v1).  The v1 body below is untouched.
      if (options.panel2K < 1 || options.panel2K > panel2::kMaxK) {
        throw std::invalid_argument("--panel2-k must be 1..30");
      }
      if (options.panel2Horizon < 1 || options.panel2Horizon > 255) {
        throw std::invalid_argument("--panel2-horizon must be 1..255");
      }
      if (options.panelStride <= 0) {
        throw std::invalid_argument("--panel-stride must be > 0 with --panel2");
      }
      if (options.mirrorCheck > 0) return runMirrorCheck(options);
      if (options.statesPath.empty()) {
        throw std::invalid_argument("--states is required");
      }
      return runPanel2(options);
    }
    if (options.statesPath.empty()) throw std::invalid_argument("--states is required");

    std::FILE* statesFile = std::fopen(options.statesPath.c_str(), "wb");
    if (statesFile == nullptr) throw std::runtime_error("cannot open " + options.statesPath);
    std::FILE* panelFile = nullptr;
    if (!options.panelPath.empty()) {
      panelFile = std::fopen(options.panelPath.c_str(), "wb");
      if (panelFile == nullptr) throw std::runtime_error("cannot open " + options.panelPath);
    }

    std::mutex writeMutex;
    std::atomic<int> nextIndex{0};
    std::atomic<std::uint64_t> totalStates{0}, totalPanels{0}, totalMoves{0};
    std::atomic<std::uint64_t> totalClears{0}, totalReveals{0};
    std::atomic<long long> totalScore{0};
    std::atomic<int> censoredGames{0}, finished{0};
    const auto started = std::chrono::steady_clock::now();

    const int threads = std::max(1, std::min(options.threads, options.games));
    std::vector<std::thread> pool;
    for (int worker = 0; worker < threads; ++worker) {
      pool.emplace_back([&]() {
        Search search(options.depth, options.terminalUtility, 3'200'000);
        for (;;) {
          const int index = nextIndex.fetch_add(1);
          if (index >= options.games) return;
          const std::uint32_t seed = options.seedStart + static_cast<std::uint32_t>(index);
          GameOutput output = generateGame(seed, options, search);
          {
            const std::lock_guard<std::mutex> lock(writeMutex);
            if (!output.states.empty()) {
              std::fwrite(output.states.data(), sizeof(StateRecord),
                          output.states.size(), statesFile);
            }
            if (panelFile != nullptr && !output.panels.empty()) {
              std::fwrite(output.panels.data(), sizeof(PanelRecord),
                          output.panels.size(), panelFile);
            }
          }
          totalStates += output.states.size();
          totalPanels += output.panels.size();
          totalMoves += static_cast<std::uint64_t>(output.moves);
          totalClears += output.clears;
          totalReveals += output.reveals;
          totalScore += output.score;
          if (output.censored) censoredGames.fetch_add(1);
          const int done = finished.fetch_add(1) + 1;
          if (done % 64 == 0 || done == options.games) {
            std::cerr << "[" << done << "/" << options.games << "] states "
                      << totalStates.load() << " moves " << totalMoves.load() << "\n";
          }
        }
      });
    }
    for (std::thread& thread : pool) thread.join();
    std::fclose(statesFile);
    if (panelFile != nullptr) std::fclose(panelFile);

    const double wall = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started).count();
    std::ostringstream summary;
    summary << std::setprecision(10) << "{\n"
            << "  \"format\": \"drop7-corpus-v1\",\n"
            << "  \"seedLease\": \"SEEDLEASE-A51D\",\n"
            << "  \"dataRole\": \"exploratory-training\",\n"
            << "  \"seedStartHex\": \"0x" << std::hex << options.seedStart << std::dec << "\",\n"
            << "  \"games\": " << options.games << ",\n"
            << "  \"behaviorDepth\": " << options.depth << ",\n"
            << "  \"epsilon\": " << options.epsilon << ",\n"
            << "  \"panelStride\": " << options.panelStride << ",\n"
            << "  \"stateRecordBytes\": " << sizeof(StateRecord) << ",\n"
            << "  \"panelRecordBytes\": " << sizeof(PanelRecord) << ",\n"
            << "  \"stateRecords\": " << totalStates.load() << ",\n"
            << "  \"panelRecords\": " << totalPanels.load() << ",\n"
            << "  \"meanMoves\": " << static_cast<double>(totalMoves.load()) / options.games << ",\n"
            << "  \"meanScore\": " << static_cast<double>(totalScore.load()) / options.games << ",\n"
            << "  \"clearsPerMove\": " << static_cast<double>(totalClears.load()) / static_cast<double>(totalMoves.load()) << ",\n"
            << "  \"revealsPerMove\": " << static_cast<double>(totalReveals.load()) / static_cast<double>(totalMoves.load()) << ",\n"
            << "  \"censoredGames\": " << censoredGames.load() << ",\n"
            << "  \"wallSeconds\": " << wall << ",\n"
            << "  \"movesPerSecond\": " << static_cast<double>(totalMoves.load()) / wall << "\n}\n";
    std::cout << summary.str();
    if (!options.summaryPath.empty()) {
      std::ofstream file(options.summaryPath);
      file << summary.str();
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "generate failed: " << error.what() << '\n';
    return 1;
  }
}
