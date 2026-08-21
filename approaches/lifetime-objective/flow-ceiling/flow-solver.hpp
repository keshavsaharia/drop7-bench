#pragma once

// A single-threaded exact window solver with a pluggable per-move objective.
//
// Why this exists rather than reusing `scenario/solver.hpp` unchanged
// -------------------------------------------------------------------
// The frozen solver maximizes *points*.  The flow-ceiling question is about
// *discs*: can any line of play sustain 2.400 numbered clears and 1.400 covered
// reveals per move, the disc-conservation requirement that
// `docs/exploratory/finding-01-score-is-survival.md` derives from 12 discs
// entering a 49-cell board every five moves?  Answering it needs an optimum
// under a clear-counting objective as well as under the score objective.
//
// Everything that defines the game is reused, not re-derived:
//   * `applyScenarioMove`  - one move of a scenario at a search depth
//   * `TTKey` / `makeKey`  - the exact (board, latent, depth) node identity
//   * `movesRemainingAt`, `risesConsumed`, `riseRowCount`
// from `approaches/lifetime-objective/scenario/`, whose engine is proven
// trajectory-identical to `drop7::playHeadlessMove` over 218,470 moves.
//
// The frozen solver's admissible bound is deliberately not reproduced.
// `docs/exploratory/finding-02-scenario-benchmark.md` measured it pruning
// exactly zero nodes at every horizon, so this solver is plain depth-first
// search plus a transposition table, which is where all of the measured 7.6x
// speedup came from.  Every node this solver stores was searched exhaustively,
// so every stored value is exact and the stored best column is a valid
// principal-variation step.
//
// `--cross-check` in `flow-run.cpp` runs this solver with the points objective
// against `drop7::scenario::solveScenario` over a whole suite and requires the
// optima to be identical.

#include "../scenario/solver.hpp"

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace drop7::flowceiling {

using namespace drop7;
using namespace drop7::scenario;

enum class Objective {
  kPoints,  // score delta of the move, exactly the frozen solver's objective
  kClears,  // numbered discs cleared by the move's cascades
};

inline const char* objectiveName(Objective objective) {
  return objective == Objective::kPoints ? "points" : "clears";
}

// The per-move value under an objective.  `result` is the completed move.
inline std::int64_t moveValue(Objective objective, const MoveResult& result) {
  if (objective == Objective::kPoints) return result.score_delta;
  std::int64_t cleared = 0;
  for (const Wave& wave : result.waves) {
    cleared += wave.cleared;
  }
  return cleared;
}

struct WindowResult {
  bool complete = false;
  std::int64_t value = 0;
  std::vector<int> pv;
  std::int64_t nodes = 0;
  std::size_t tt_entries = 0;
  double wall_seconds = 0.0;
};

// Exact value of every legal root move, under one fully specified window.
struct RootResult {
  bool complete = false;
  std::array<std::int64_t, kBoardSize> value{};
  std::array<bool, kBoardSize> legal{};
  std::int64_t nodes = 0;
  double wall_seconds = 0.0;
};

struct WindowLimits {
  std::int64_t max_nodes = 0;        // 0 disables
  double max_seconds = 0.0;          // 0 disables
  std::size_t tt_capacity = 4'000'000;
};

// Exact depth-first search of one scenario window.  Single-threaded on purpose:
// the receding-horizon driver runs whole games in parallel, one game per
// thread, which keeps every solver's transposition table private and avoids the
// nested oversubscription `AGENTS.md` forbids.
class WindowSolver {
 public:
  WindowSolver(const Scenario& scenario, Objective objective,
               const WindowLimits& limits)
      : scenario_(scenario),
        objective_(objective),
        limits_(limits),
        horizon_(scenario.horizon) {}

  // Per-root-column exact values under this window's objective.  The fair
  // planner needs the value of *every* legal opening move under one sampled
  // hidden board, not only the best one, so that it can average each column's
  // value across samples before choosing.  Everything below the root is the
  // same exact search, and the transposition table is shared across the root's
  // children, which is why this costs about what one `run()` costs.
  RootResult runRoot() {
    RootResult result;
    start_ = std::chrono::steady_clock::now();
    aborted_ = false;
    nodes_ = 0;
    table_.clear();

    SearchNode root;
    root.board = scenario_.board;
    root.latent = scenario_.latent;
    MoveResult scratch;

    int legal_count = 0;
    const auto columns = legalColumns(root.board, legal_count);
    for (int offset = 0; offset < legal_count; ++offset) {
      SearchNode child;
      std::int64_t delta = 0;
      bool game_over = false;
      if (!applyScenarioMove(scenario_, root, 0, columns[offset], child, delta,
                             game_over, scratch)) {
        continue;
      }
      ++nodes_;
      std::int64_t total = moveValue(objective_, scratch);
      if (!game_over && horizon_ > 1) total += search(child, 1, scratch);
      const std::size_t slot = static_cast<std::size_t>(columns[offset]);
      result.legal[slot] = true;
      result.value[slot] = total;
      if (aborted_) break;
    }
    result.complete = !aborted_;
    result.nodes = nodes_;
    result.wall_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_)
            .count();
    return result;
  }

  WindowResult run() {
    WindowResult result;
    start_ = std::chrono::steady_clock::now();
    aborted_ = false;
    nodes_ = 0;
    table_.clear();

    SearchNode root;
    root.board = scenario_.board;
    root.latent = scenario_.latent;

    MoveResult scratch;
    result.value = search(root, 0, scratch);
    result.complete = !aborted_;
    result.nodes = nodes_;
    result.tt_entries = table_.size();

    if (result.complete) result.pv = extractPv(root, scratch);
    result.wall_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_)
            .count();
    return result;
  }

 private:
  bool outOfBudget() {
    if (aborted_) return true;
    if (limits_.max_nodes > 0 && nodes_ > limits_.max_nodes) {
      aborted_ = true;
      return true;
    }
    if (limits_.max_seconds > 0.0 && (nodes_ & 0x3fff) == 0) {
      const double elapsed = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - start_)
                                 .count();
      if (elapsed > limits_.max_seconds) {
        aborted_ = true;
        return true;
      }
    }
    return false;
  }

  // Exact maximum of the summed per-move value over the remaining window.
  // Dying ends the line: no further value is earned, which is what prices
  // survival under both objectives without inventing a terminal penalty.
  std::int64_t search(const SearchNode& node, int depth, MoveResult& scratch) {
    if (depth >= horizon_) return 0;
    if (outOfBudget()) return 0;

    const TTKey key = makeKey(node, depth);
    const auto found = table_.find(key);
    if (found != table_.end()) return found->second.value;

    int legal_count = 0;
    const auto columns = legalColumns(node.board, legal_count);
    std::int64_t best = 0;
    int best_column = -1;
    for (int offset = 0; offset < legal_count; ++offset) {
      SearchNode child;
      std::int64_t delta = 0;
      bool game_over = false;
      if (!applyScenarioMove(scenario_, node, depth, columns[offset], child,
                             delta, game_over, scratch)) {
        continue;
      }
      ++nodes_;
      // `moveValue` is read before recursing, so the child may reuse `scratch`.
      // That matters: the solver visits millions of nodes and a fresh
      // `MoveResult` per node would allocate a wave vector per node.
      std::int64_t total = moveValue(objective_, scratch);
      if (!game_over) total += search(child, depth + 1, scratch);
      if (best_column < 0 || total > best) {
        best = total;
        best_column = columns[offset];
      }
      if (aborted_) return best_column < 0 ? 0 : best;
    }
    if (best_column < 0) return 0;
    if (!aborted_ && table_.size() < limits_.tt_capacity) {
      TTValue stored;
      stored.value = best;
      stored.best_column = static_cast<std::int8_t>(best_column);
      table_.emplace(key, stored);
    }
    return best;
  }

  // Walks the stored best columns.  Every stored node was searched
  // exhaustively, so this is a genuine optimal line, and `flow-common.hpp`
  // replays it through the engine and checks that it earns the reported value.
  std::vector<int> extractPv(const SearchNode& root, MoveResult& scratch) {
    std::vector<int> path;
    SearchNode node = root;
    for (int depth = 0; depth < horizon_; ++depth) {
      const auto found = table_.find(makeKey(node, depth));
      if (found == table_.end() || found->second.best_column < 0) break;
      const int column = found->second.best_column;
      SearchNode child;
      std::int64_t delta = 0;
      bool game_over = false;
      if (!applyScenarioMove(scenario_, node, depth, column, child, delta,
                             game_over, scratch)) {
        break;
      }
      path.push_back(column);
      node = child;
      if (game_over) break;
    }
    return path;
  }

  const Scenario& scenario_;
  Objective objective_;
  WindowLimits limits_;
  int horizon_;
  std::unordered_map<TTKey, TTValue, TTKeyHash> table_;
  std::int64_t nodes_ = 0;
  bool aborted_ = false;
  std::chrono::steady_clock::time_point start_{};
};

inline WindowResult solveWindow(const Scenario& scenario, Objective objective,
                                const WindowLimits& limits) {
  WindowSolver solver(scenario, objective, limits);
  return solver.run();
}

inline RootResult solveWindowRoot(const Scenario& scenario, Objective objective,
                                  const WindowLimits& limits) {
  WindowSolver solver(scenario, objective, limits);
  return solver.runRoot();
}

}  // namespace drop7::flowceiling
