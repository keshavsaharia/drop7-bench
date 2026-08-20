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
    else throw std::invalid_argument("unknown option " + key);
  }
  return options;
}

}  // namespace drop7::corpus

int main(int argc, char** argv) {
  using namespace drop7;
  using namespace drop7::corpus;
  try {
    const Options options = parseOptions(argc, argv);
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
