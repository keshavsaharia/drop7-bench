#pragma once

// Exact clairvoyant solver for a fully specified scenario.
//
// A scenario fixes the board, the hidden value under every covered cell, the
// values that arrive with every future risen row, and the disc tape.  The game
// is therefore a deterministic single-player perfect-information puzzle over
// `horizon` moves, and it has an exact optimum.
//
// Objective: maximize the total points earned inside the horizon.  Dying ends
// the line and earns nothing further, so survival is priced by the points it
// would have bought rather than by an arbitrary penalty.
//
// Search contract, used for branch-and-bound:
//   search(node, depth, alpha) returns either kPruned, which asserts that the
//   node's true value is <= alpha, or the exact true value of the node.
// A single-agent maximization tree admits no "fail high" cut, so a child is
// only ever discarded when a provable upper bound on it cannot beat the best
// line already found.  The bound used here is admissible by construction; see
// `perMoveUpperBound`.

#include "scenario.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace drop7::scenario {

constexpr std::int64_t kPruned = -1;

struct SearchNode {
  Board board{};
  LatentBoard latent{};
};

inline int movesRemainingAt(int start_moves_remaining, int depth) {
  const int cycle = ((start_moves_remaining - 1 - depth) % kMovesPerLevel +
                     kMovesPerLevel) %
                    kMovesPerLevel;
  return cycle + 1;
}

inline bool riseOnMove(int start_moves_remaining, int depth) {
  return movesRemainingAt(start_moves_remaining, depth) == 1;
}

// Applies one move of a scenario at search depth `depth`.  Everything the
// reveal source needs is a function of the depth alone: the tape index, the
// rise index, and the number of moves left before the next rise.  This is what
// makes a scenario node exactly (board, latent, depth).
inline bool applyScenarioMove(const Scenario& scenario, const SearchNode& node,
                              int depth, int column, SearchNode& child,
                              std::int64_t& delta, bool& game_over,
                              MoveResult& scratch) {
  State state;
  state.board = node.board;
  state.next_disc = scenario.disc_tape[static_cast<std::size_t>(depth)];
  state.score = 0;
  state.level = 1;
  state.moves_remaining = movesRemainingAt(scenario.moves_remaining, depth);
  state.moves_played = depth;
  state.game_over = false;

  LatentRevealSource source;
  source.tape = scenario.disc_tape.data();
  source.tape_length = static_cast<int>(scenario.disc_tape.size());
  source.tape_index = depth + 1;
  source.rise_rows = scenario.rise_latent.data();
  source.rise_count = static_cast<int>(scenario.rise_latent.size());
  source.rise_index = risesConsumed(depth, scenario.moves_remaining);

  if (!playScenarioMove(state, node.latent, column, source, scratch,
                        child.latent)) {
    return false;
  }
  child.board = scratch.state.board;
  delta = scratch.score_delta;
  game_over = scratch.state.game_over;
  return true;
}

// ---------------------------------------------------------------------------
// Admissible upper bound
// ---------------------------------------------------------------------------

// Maximum chain points obtainable in one move when at most `cells` numbered
// discs can be cleared during the whole cascade.  Reaching wave depth d costs
// at least one cleared disc in each of the d-1 earlier waves, so the extremal
// allocation puts one disc in each shallow wave and the rest in the deepest.
inline const std::array<std::int64_t, 64>& chainBoundTable() {
  static const std::array<std::int64_t, 64> table = [] {
    std::array<std::int64_t, 64> values{};
    for (int cells = 0; cells < 64; ++cells) {
      std::int64_t best = 0;
      for (int depth = 1; depth <= cells; ++depth) {
        std::int64_t total = 0;
        for (int earlier = 1; earlier < depth; ++earlier) {
          total += scoreForWave(earlier);
        }
        total += static_cast<std::int64_t>(cells - (depth - 1)) *
                 scoreForWave(depth);
        best = std::max(best, total);
      }
      values[cells] = best;
    }
    return values;
  }();
  return table;
}

// Upper bound on the points one move can earn.  At most 49 discs sit on the
// board, the move adds one, and a rise adds seven more; each cell-instance can
// clear at most once during a move.  A rise pays 17,000 and the engine can pay
// the 70,000 clear bonus once before the rise and once after it.
inline std::int64_t perMoveUpperBound(bool rise) {
  const int cells = kCellCount + 1 + (rise ? kBoardSize : 0);
  std::int64_t bound = chainBoundTable()[static_cast<std::size_t>(cells)];
  bound += kClearBonus * (rise ? 2 : 1);
  if (rise) bound += kLevelBonus;
  return bound;
}

// ---------------------------------------------------------------------------
// Transposition table
// ---------------------------------------------------------------------------

struct TTKey {
  std::array<std::uint8_t, 2 * kCellCount + 1> bytes{};
  bool operator==(const TTKey& other) const { return bytes == other.bytes; }
};

struct TTKeyHash {
  std::size_t operator()(const TTKey& key) const {
    std::uint64_t hash = 1469598103934665603ull;
    for (std::uint8_t byte : key.bytes) {
      hash ^= static_cast<std::uint64_t>(byte);
      hash *= 1099511628211ull;
    }
    return static_cast<std::size_t>(hash);
  }
};

inline TTKey makeKey(const SearchNode& node, int depth) {
  TTKey key;
  std::memcpy(key.bytes.data(), node.board.data(), kCellCount);
  std::memcpy(key.bytes.data() + kCellCount, node.latent.data(), kCellCount);
  key.bytes[2 * kCellCount] = static_cast<std::uint8_t>(depth);
  return key;
}

struct TTValue {
  std::int64_t value = 0;
  std::int8_t best_column = -1;
};

class TranspositionTable {
 public:
  static constexpr int kShards = 64;

  explicit TranspositionTable(std::size_t capacity) : capacity_(capacity) {}

  bool lookup(const TTKey& key, TTValue& out) {
    Shard& shard = shardFor(key);
    std::lock_guard<std::mutex> guard(shard.mutex);
    const auto found = shard.map.find(key);
    if (found == shard.map.end()) return false;
    out = found->second;
    return true;
  }

  void store(const TTKey& key, const TTValue& value) {
    Shard& shard = shardFor(key);
    std::lock_guard<std::mutex> guard(shard.mutex);
    if (shard.map.size() >= capacity_ / kShards) return;
    shard.map[key] = value;
  }

  std::size_t size() {
    std::size_t total = 0;
    for (Shard& shard : shards_) {
      std::lock_guard<std::mutex> guard(shard.mutex);
      total += shard.map.size();
    }
    return total;
  }

 private:
  struct alignas(64) Shard {
    std::unordered_map<TTKey, TTValue, TTKeyHash> map;
    std::mutex mutex;
  };

  Shard& shardFor(const TTKey& key) {
    const std::size_t hash = TTKeyHash{}(key);
    return shards_[(hash >> 40) % kShards];
  }

  std::array<Shard, kShards> shards_;
  std::size_t capacity_;
};

// ---------------------------------------------------------------------------
// Solver
// ---------------------------------------------------------------------------

struct SolveOptions {
  bool use_tt = true;
  bool use_bound = true;
  int threads = 1;
  std::size_t tt_capacity = 6'000'000;
  double time_limit_seconds = 0.0;  // 0 disables the limit
};

struct SolveResult {
  bool complete = false;
  std::int64_t optimum = 0;
  std::vector<int> principal_variation;
  bool optimal_clears_board = false;
  int optimal_max_chain_depth = 0;
  int optimal_moves_survived = 0;
  int optimal_clear_count = 0;
  std::int64_t nodes = 0;
  std::int64_t pv_nodes = 0;  // extra nodes spent only on rebuilding the line
  std::int64_t tt_hits = 0;
  std::int64_t tt_stores = 0;
  std::int64_t bound_cutoffs = 0;
  std::size_t tt_entries = 0;
  double wall_seconds = 0.0;
};

class Solver {
 public:
  Solver(const Scenario& scenario, const SolveOptions& options)
      : scenario_(scenario),
        options_(options),
        table_(options.tt_capacity),
        horizon_(scenario.horizon),
        has_time_limit_(options.time_limit_seconds > 0.0) {
    suffix_bound_.assign(static_cast<std::size_t>(horizon_) + 1, 0);
    for (int depth = horizon_ - 1; depth >= 0; --depth) {
      suffix_bound_[static_cast<std::size_t>(depth)] =
          suffix_bound_[static_cast<std::size_t>(depth) + 1] +
          perMoveUpperBound(riseOnMove(scenario.moves_remaining, depth));
    }
  }

  SolveResult run();

 private:
  struct ThreadStats {
    std::int64_t nodes = 0;
    std::int64_t tt_hits = 0;
    std::int64_t tt_stores = 0;
    std::int64_t bound_cutoffs = 0;
  };

  std::int64_t search(const SearchNode& node, int depth, std::int64_t alpha,
                      ThreadStats& stats, MoveResult& scratch);

  bool outOfTime() const {
    if (options_.time_limit_seconds <= 0.0) return false;
    if (timed_out_.load(std::memory_order_relaxed)) return true;
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_)
            .count();
    if (elapsed > options_.time_limit_seconds) {
      timed_out_.store(true, std::memory_order_relaxed);
      return true;
    }
    return false;
  }

  const Scenario& scenario_;
  SolveOptions options_;
  TranspositionTable table_;
  int horizon_;
  bool has_time_limit_ = false;
  std::vector<std::int64_t> suffix_bound_;
  std::chrono::steady_clock::time_point start_{};
  mutable std::atomic<bool> timed_out_{false};
  std::atomic<long long> time_check_{0};
};

inline std::int64_t Solver::search(const SearchNode& node, int depth,
                                   std::int64_t alpha, ThreadStats& stats,
                                   MoveResult& scratch) {
  if (depth >= horizon_) return 0;
  if (options_.use_bound && alpha >= 0 &&
      suffix_bound_[static_cast<std::size_t>(depth)] <= alpha) {
    ++stats.bound_cutoffs;
    return kPruned;
  }
  ++stats.nodes;
  if (has_time_limit_) {
    // The flag must be consulted on every node: sampling it only every N nodes
    // lets the search run essentially to completion after the deadline passes.
    if (timed_out_.load(std::memory_order_relaxed)) return kPruned;
    if ((stats.nodes & 0x3ff) == 0 && outOfTime()) return kPruned;
  }

  TTKey key;
  if (options_.use_tt) {
    key = makeKey(node, depth);
    TTValue cached;
    if (table_.lookup(key, cached)) {
      ++stats.tt_hits;
      return cached.value;
    }
  }

  std::int64_t best = kPruned;
  int best_column = -1;
  bool any_child_pruned = false;
  int legal_count = 0;
  const auto columns = legalColumns(node.board, legal_count);
  for (int offset = 0; offset < legal_count; ++offset) {
    const int column = columns[offset];
    SearchNode child;
    std::int64_t delta = 0;
    bool game_over = false;
    if (!applyScenarioMove(scenario_, node, depth, column, child, delta,
                           game_over, scratch)) {
      continue;
    }
    std::int64_t value = delta;
    if (!game_over && depth + 1 < horizon_) {
      const std::int64_t child_alpha = std::max(alpha, best) - delta;
      const std::int64_t sub =
          search(child, depth + 1, child_alpha, stats, scratch);
      if (sub == kPruned) {
        // The child's contribution is at most delta + child_alpha, that is at
        // most max(alpha, best_at_this_moment).
        any_child_pruned = true;
        continue;
      }
      value = delta + sub;
    }
    if (value > best) {
      best = value;
      best_column = column;
    }
  }

  if (best == kPruned) return kPruned;
  // Every discarded child was bounded by max(alpha, best_at_the_time).  If the
  // final best is at least alpha, every such bound is at most best, so best is
  // the exact maximum.  Otherwise the node's true value is at most alpha, which
  // is exactly what kPruned asserts, and nothing may be cached.
  const bool exact = !any_child_pruned || best >= alpha;
  if (!exact) return kPruned;
  if (options_.use_tt && !timed_out_.load(std::memory_order_relaxed)) {
    ++stats.tt_stores;
    table_.store(key, TTValue{best, static_cast<std::int8_t>(best_column)});
  }
  return best;
}

inline SolveResult Solver::run() {
  SolveResult result;
  start_ = std::chrono::steady_clock::now();

  SearchNode root;
  root.board = scenario_.board;
  root.latent = scenario_.latent;

  // Prefix expansion: unroll the first plies breadth-first so that independent
  // subtrees can be handed to worker threads.  Rises land on fixed move indices,
  // so a prefix node is fully described by (board, latent, depth).
  struct PrefixNode {
    SearchNode node;
    int depth = 0;
    std::int64_t accumulated = 0;
    int column = -1;
    int parent = -1;
    bool terminal = false;
  };
  std::vector<PrefixNode> prefix;
  prefix.push_back(PrefixNode{root, 0, 0, -1, -1, false});
  std::vector<int> frontier{0};
  const int target_tasks =
      std::max(1, options_.threads <= 1 ? 1 : options_.threads * 6);
  MoveResult scratch;
  while (static_cast<int>(frontier.size()) < target_tasks) {
    std::vector<int> next;
    bool expanded = false;
    for (int index : frontier) {
      const PrefixNode current = prefix[static_cast<std::size_t>(index)];
      if (current.terminal || current.depth >= horizon_) {
        next.push_back(index);
        continue;
      }
      int legal_count = 0;
      const auto columns = legalColumns(current.node.board, legal_count);
      bool any = false;
      for (int offset = 0; offset < legal_count; ++offset) {
        SearchNode child;
        std::int64_t delta = 0;
        bool game_over = false;
        if (!applyScenarioMove(scenario_, current.node, current.depth,
                               columns[offset], child, delta, game_over,
                               scratch)) {
          continue;
        }
        PrefixNode node;
        node.node = child;
        node.depth = current.depth + 1;
        node.accumulated = current.accumulated + delta;
        node.column = columns[offset];
        node.parent = index;
        node.terminal = game_over || node.depth >= horizon_;
        prefix.push_back(node);
        next.push_back(static_cast<int>(prefix.size()) - 1);
        any = true;
        expanded = true;
      }
      if (!any) {
        prefix[static_cast<std::size_t>(index)].terminal = true;
        next.push_back(index);
      }
    }
    frontier = next;
    if (!expanded) break;
  }

  std::vector<int> tasks;
  for (int index : frontier) {
    if (!prefix[static_cast<std::size_t>(index)].terminal) tasks.push_back(index);
  }
  // Best immediate accumulation first: a good incumbent early makes the bound
  // useful sooner.
  std::sort(tasks.begin(), tasks.end(), [&prefix](int left, int right) {
    return prefix[static_cast<std::size_t>(left)].accumulated >
           prefix[static_cast<std::size_t>(right)].accumulated;
  });

  std::atomic<long long> incumbent{-1};
  if (options_.use_bound) {
    // A cheap greedy descent gives branch-and-bound a real incumbent to beat
    // from the first node.  It only ever raises a lower bound, so it cannot
    // change the optimum.
    SearchNode node = root;
    std::int64_t total = 0;
    MoveResult greedy_scratch;
    for (int depth = 0; depth < horizon_; ++depth) {
      std::int64_t best = -1;
      SearchNode best_child;
      bool best_over = false;
      int legal_count = 0;
      const auto columns = legalColumns(node.board, legal_count);
      for (int offset = 0; offset < legal_count; ++offset) {
        SearchNode child;
        std::int64_t delta = 0;
        bool over = false;
        if (!applyScenarioMove(scenario_, node, depth, columns[offset], child,
                               delta, over, greedy_scratch)) {
          continue;
        }
        if (delta > best) {
          best = delta;
          best_child = child;
          best_over = over;
        }
      }
      if (best < 0) break;
      total += best;
      node = best_child;
      if (best_over) break;
    }
    incumbent.store(total);
  }
  for (int index : frontier) {
    const PrefixNode& node = prefix[static_cast<std::size_t>(index)];
    if (node.terminal) {
      long long current = incumbent.load();
      while (node.accumulated > current &&
             !incumbent.compare_exchange_weak(current, node.accumulated)) {
      }
    }
  }

  std::vector<std::int64_t> task_values(tasks.size(), kPruned);
  std::atomic<std::size_t> cursor{0};
  std::vector<ThreadStats> stats(
      static_cast<std::size_t>(std::max(1, options_.threads)));

  const auto worker = [&](int slot) {
    MoveResult local_scratch;
    ThreadStats& local = stats[static_cast<std::size_t>(slot)];
    for (;;) {
      const std::size_t index = cursor.fetch_add(1);
      if (index >= tasks.size()) return;
      const PrefixNode& node =
          prefix[static_cast<std::size_t>(tasks[index])];
      const std::int64_t alpha =
          options_.use_bound
              ? static_cast<std::int64_t>(incumbent.load()) - node.accumulated
              : kPruned;
      const std::int64_t value =
          search(node.node, node.depth, alpha, local, local_scratch);
      task_values[index] = value;
      if (value != kPruned) {
        const long long total = node.accumulated + value;
        long long current = incumbent.load();
        while (total > current &&
               !incumbent.compare_exchange_weak(current, total)) {
        }
      }
    }
  };

  const int thread_count = std::max(1, options_.threads);
  if (thread_count == 1 || tasks.size() <= 1) {
    worker(0);
  } else {
    std::vector<std::thread> pool;
    for (int slot = 0; slot < thread_count; ++slot) {
      pool.emplace_back(worker, slot);
    }
    for (std::thread& thread : pool) thread.join();
  }

  for (const ThreadStats& local : stats) {
    result.nodes += local.nodes;
    result.tt_hits += local.tt_hits;
    result.tt_stores += local.tt_stores;
    result.bound_cutoffs += local.bound_cutoffs;
  }
  result.complete = !timed_out_.load();
  if (!result.complete) {
    result.wall_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_)
            .count();
    return result;
  }

  std::int64_t optimum = -1;
  int best_task = -1;
  for (std::size_t index = 0; index < tasks.size(); ++index) {
    if (task_values[index] == kPruned) continue;
    const PrefixNode& node = prefix[static_cast<std::size_t>(tasks[index])];
    const std::int64_t total = node.accumulated + task_values[index];
    if (total > optimum) {
      optimum = total;
      best_task = static_cast<int>(index);
    }
  }
  int best_terminal = -1;
  for (int index : frontier) {
    const PrefixNode& node = prefix[static_cast<std::size_t>(index)];
    if (!node.terminal) continue;
    if (node.accumulated > optimum) {
      optimum = node.accumulated;
      best_task = -1;
      best_terminal = index;
    }
  }
  if (optimum < 0) optimum = 0;
  result.optimum = optimum;

  // Principal variation: the prefix path to the winning subtree, then a walk
  // down the transposition table's stored best columns.  If an entry is absent
  // (table capacity reached), the subtree is re-solved exactly at that point.
  std::vector<int> path;
  int walk = best_task >= 0
                 ? tasks[static_cast<std::size_t>(best_task)]
                 : best_terminal;
  if (walk >= 0) {
    for (int index = walk; index > 0;
         index = prefix[static_cast<std::size_t>(index)].parent) {
      path.push_back(prefix[static_cast<std::size_t>(index)].column);
    }
    std::reverse(path.begin(), path.end());
    SearchNode node = prefix[static_cast<std::size_t>(walk)].node;
    int depth = prefix[static_cast<std::size_t>(walk)].depth;
    ThreadStats pv_stats;
    MoveResult pv_scratch;
    while (depth < horizon_) {
      TTValue cached;
      int column = -1;
      if (options_.use_tt && table_.lookup(makeKey(node, depth), cached)) {
        column = cached.best_column;
      } else {
        std::int64_t best = kPruned;
        int legal_count = 0;
        const auto columns = legalColumns(node.board, legal_count);
        for (int offset = 0; offset < legal_count; ++offset) {
          SearchNode child;
          std::int64_t delta = 0;
          bool game_over = false;
          if (!applyScenarioMove(scenario_, node, depth, columns[offset], child,
                                 delta, game_over, pv_scratch)) {
            continue;
          }
          std::int64_t value = delta;
          if (!game_over && depth + 1 < horizon_) {
            const std::int64_t sub =
                search(child, depth + 1, kPruned, pv_stats, pv_scratch);
            if (sub == kPruned) continue;
            value = delta + sub;
          }
          if (value > best) {
            best = value;
            column = columns[offset];
          }
        }
      }
      if (column < 0) break;
      path.push_back(column);
      SearchNode child;
      std::int64_t delta = 0;
      bool game_over = false;
      if (!applyScenarioMove(scenario_, node, depth, column, child, delta,
                             game_over, pv_scratch)) {
        break;
      }
      node = child;
      ++depth;
      if (game_over) break;
    }
    result.pv_nodes = pv_stats.nodes;
  }
  result.principal_variation = path;

  // Replay the principal variation to characterize it and to check that it
  // really earns the reported optimum.
  {
    auto engine = makeScenarioEngine(scenario_);
    std::int64_t replay = 0;
    for (std::size_t index = 0; index < path.size(); ++index) {
      if (engine.state().game_over) break;
      MoveResult move;
      if (!engine.play(path[index], move)) break;
      replay += move.score_delta;
      if (move.cleared_board) {
        result.optimal_clears_board = true;
        ++result.optimal_clear_count;
      }
      for (const Wave& wave : move.waves) {
        result.optimal_max_chain_depth =
            std::max(result.optimal_max_chain_depth, wave.depth);
      }
      ++result.optimal_moves_survived;
    }
    if (replay != result.optimum) {
      // Never expected; surfaced as an incomplete result rather than silently.
      result.complete = false;
    }
  }

  result.tt_entries = options_.use_tt ? table_.size() : 0;
  result.wall_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start_)
          .count();
  return result;
}

inline SolveResult solveScenario(const Scenario& scenario,
                                 const SolveOptions& options) {
  Solver solver(scenario, options);
  return solver.run();
}

// Deliberately naive exact enumerator used only to cross-check the real solver.
// No table, no bound, no threads, no move ordering.
inline std::int64_t naiveOptimum(const Scenario& scenario,
                                 const SearchNode& node, int depth,
                                 std::int64_t& nodes) {
  if (depth >= scenario.horizon) return 0;
  MoveResult scratch;
  std::int64_t best = 0;
  bool any = false;
  for (int column = 0; column < kBoardSize; ++column) {
    if (!isLegal(node.board, column)) continue;
    SearchNode child;
    std::int64_t delta = 0;
    bool game_over = false;
    if (!applyScenarioMove(scenario, node, depth, column, child, delta,
                           game_over, scratch)) {
      continue;
    }
    ++nodes;
    std::int64_t value = delta;
    if (!game_over) value += naiveOptimum(scenario, child, depth + 1, nodes);
    if (!any || value > best) {
      best = value;
      any = true;
    }
  }
  return any ? best : 0;
}

inline std::int64_t naiveOptimum(const Scenario& scenario,
                                 std::int64_t& nodes) {
  SearchNode root;
  root.board = scenario.board;
  root.latent = scenario.latent;
  return naiveOptimum(scenario, root, 0, nodes);
}

}  // namespace drop7::scenario
