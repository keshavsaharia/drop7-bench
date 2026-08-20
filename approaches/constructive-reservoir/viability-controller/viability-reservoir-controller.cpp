#define DROP7_FAIR_ONLY_HORIZON_LIBRARY
#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#define DROP7_FAIR_ONLY_DEPTH4_NO_MAIN
#include "../../fair-expectimax/reference/fair-only-depth4.cpp"
#undef DROP7_FAIR_ONLY_DEPTH4_NO_MAIN
#undef DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#undef DROP7_FAIR_ONLY_HORIZON_LIBRARY

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <type_traits>
#include <utility>
#include <vector>

// Implements a non-parametric viability-reservoir controller.  The runtime
// decision accepts only the public board, next disc, rise phase, and terminal
// flag.  It constructs an exact conservative cascade
// certificate for all 7 x 7 possible next trigger keys, chooses a public-state
// option, applies a terminal/viability shield, and ranks the surviving actions
// lexicographically.  There is no fitted scalar leaf and no gameplay history.
namespace drop7::viability_reservoir_controller {

namespace fair = drop7::fair_only_horizon;
namespace d4 = drop7::fair_only_depth4;
namespace detail = drop7::cfpi::detail;

using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kStageASeedStart = 0x3d65'c000u;
constexpr std::uint32_t kStageASeedEndExclusive = 0x3d65'c020u;
constexpr int kStageAGames = 32;
constexpr int kMaximumMoves = 1'000;
constexpr int kSuccessorSamples = kBoardSize;
constexpr std::uint32_t kPolicySeed = 0x5652'4331u;  // "VRC1"
constexpr double kWallLimitSeconds = 30.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr std::uint8_t kInertReveal = 10;
constexpr int kRankFields = 18;

constexpr double kGateMeanScore = 700'000.0;
constexpr double kGateMeanMoves = 200.0;
constexpr double kGateClearsPerMove = 2.20;
constexpr double kGateRevealsPerMove = 1.20;
constexpr double kGateBottomQuartileMoves = 120.0;
constexpr int kGateJointWins = 24;

constexpr std::array<int, kBoardSize> kColumnOrder{{3, 2, 4, 1, 5, 0, 6}};

static_assert(kLevelBonus == 17'000);
static_assert(kMovesPerLevel == 5);
static_assert(kSuccessorSamples == 7);
static_assert(kStageASeedEndExclusive - kStageASeedStart == kStageAGames);
static_assert((kStageASeedStart >> 16u) == 0x3d65u);
static_assert((kStageASeedEndExclusive - 1u) >> 16u == 0x3d65u);
static_assert((kStageASeedStart >> 24u) != 0x4du);
static_assert((kStageASeedStart >> 24u) != 0x7du);
static_assert((kStageASeedStart >> 24u) != 0xd7u);

struct PublicState {
  Board board{};
  std::uint8_t next_disc = 1;
  std::uint8_t moves_remaining = kMovesPerLevel;
  bool terminal = false;

  bool operator==(const PublicState&) const = default;
};

PublicState publicState(const State& source) {
  if (source.next_disc < 1 || source.next_disc > kBoardSize ||
      source.moves_remaining < 0 || source.moves_remaining > kMovesPerLevel ||
      (!source.game_over && source.moves_remaining < 1)) {
    throw std::invalid_argument("invalid public viability-reservoir state");
  }
  for (const std::uint8_t cell : source.board) {
    if (cell > kCracked) throw std::invalid_argument("invalid board token");
  }
  return {source.board, source.next_disc,
          static_cast<std::uint8_t>(source.moves_remaining), source.game_over};
}

State materialize(const PublicState& source) {
  State result;
  result.board = source.board;
  result.next_disc = source.next_disc;
  result.moves_remaining = source.moves_remaining;
  result.game_over = source.terminal;
  return result;
}

PublicState mirror(const PublicState& source) {
  PublicState result = source;
  result.board = detail::mirrorBoard(source.board);
  return result;
}

PublicState canonicalState(const PublicState& source, bool& mirrored) {
  mirrored = detail::mirroredRepresentationIsSmaller(source.board);
  return mirrored ? mirror(source) : source;
}

std::uint64_t peakRssBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ull;
#endif
}

void enforceRssLimit() {
  if (peakRssBytes() > kRssLimitBytes) {
    throw std::runtime_error(
        "viability reservoir exceeded the 256 MiB RSS cap");
  }
}

struct Deadline {
  Clock::time_point started = Clock::now();

  double elapsedSeconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }

  void check() const {
    if (elapsedSeconds() > kWallLimitSeconds) {
      throw std::runtime_error(
          "viability reservoir exceeded the 30 minute wall cap");
    }
  }
};

std::array<int, kBoardSize> columnHeights(const Board& board) {
  std::array<int, kBoardSize> result{};
  for (int column = 0; column < kBoardSize; ++column) {
    for (int row = 0; row < kBoardSize; ++row) {
      result[column] += board[indexOf(row, column)] != kEmpty;
    }
  }
  return result;
}

enum class EdgeKind : std::uint8_t {
  kLineNeighbor,
  kVerticalSupport,
  kCoverFrontier,
};

struct CausalEdge {
  std::uint8_t from = 0;
  std::uint8_t to = 0;
  EdgeKind kind = EdgeKind::kLineNeighbor;

  bool operator==(const CausalEdge&) const = default;
};

struct DiscNode {
  std::uint8_t cell = 0;
  std::uint8_t value = 0;
  std::uint8_t horizontal_length = 0;
  std::uint8_t vertical_length = 0;
  std::int8_t horizontal_deficit = 0;
  std::int8_t vertical_deficit = 0;
  std::int8_t horizontal_build_cost = -1;
  std::int8_t support_cell = -1;
  std::uint8_t adjacent_covers = 0;

  bool operator==(const DiscNode&) const = default;
};

struct GraphStats {
  int occupied = 0;
  int maximum_height = 0;
  int top_slack = kBoardSize;
  int open_columns = kBoardSize;
  int solid_cells = 0;
  int cracked_cells = 0;
  int numbered_cells = 0;
  int inert_cells = 0;
  int cover_altitude_debt = 0;
  int edge_cover_debt = 0;
  int frontier_access = 0;
  int stored_mass = 0;
  int release_ready = 0;
  int same_target_pairs = 0;
  int adjacent_ones = 0;
  int triple_twos = 0;
  int dead_low_numbers = 0;
  int capped_low_columns = 0;
  int clog_debt = 0;
  int line_edges = 0;
  int support_edges = 0;
  int frontier_edges = 0;

  bool operator==(const GraphStats&) const = default;
};

struct CertificateGraph {
  std::array<DiscNode, kCellCount> nodes{};
  int node_count = 0;
  std::array<CausalEdge, 512> edges{};
  int edge_count = 0;
  GraphStats stats{};

  bool operator==(const CertificateGraph&) const = default;
};

void addEdge(CertificateGraph& graph, int from, int to, EdgeKind kind) {
  if (graph.edge_count >= static_cast<int>(graph.edges.size())) {
    throw std::runtime_error("causal certificate edge capacity exceeded");
  }
  graph.edges[graph.edge_count++] = {
      static_cast<std::uint8_t>(from), static_cast<std::uint8_t>(to), kind};
  if (kind == EdgeKind::kLineNeighbor) ++graph.stats.line_edges;
  if (kind == EdgeKind::kVerticalSupport) ++graph.stats.support_edges;
  if (kind == EdgeKind::kCoverFrontier) ++graph.stats.frontier_edges;
}

int horizontalBuildCost(const Board& board,
                        const std::array<int, kBoardSize>& heights, int row,
                        int column, int value) {
  const int length = lineLength(board, row, column, false);
  if (value <= length) return -1;
  int start = column;
  int end = column;
  while (start > 0 && board[indexOf(row, start - 1)] != kEmpty) --start;
  while (end + 1 < kBoardSize &&
         board[indexOf(row, end + 1)] != kEmpty) {
    ++end;
  }
  const int additions = value - length;
  const int target_elevation = kBoardSize - row;
  int best = std::numeric_limits<int>::max();
  for (int left = 0; left <= additions; ++left) {
    const int right = additions - left;
    if (start - left < 0 || end + right >= kBoardSize) continue;
    int cost = 0;
    bool valid = true;
    for (int offset = 1; offset <= left; ++offset) {
      const int next_column = start - offset;
      if (board[indexOf(row, next_column)] != kEmpty ||
          heights[next_column] >= target_elevation) {
        valid = false;
        break;
      }
      cost += target_elevation - heights[next_column];
    }
    for (int offset = 1; valid && offset <= right; ++offset) {
      const int next_column = end + offset;
      if (board[indexOf(row, next_column)] != kEmpty ||
          heights[next_column] >= target_elevation) {
        valid = false;
        break;
      }
      cost += target_elevation - heights[next_column];
    }
    if (valid) best = std::min(best, cost);
  }
  return best <= 14 ? best : -1;
}

CertificateGraph buildCertificateGraph(const Board& board) {
  CertificateGraph graph;
  const auto heights = columnHeights(board);
  std::array<std::array<int, kBoardSize + 1>, kBoardSize> vertical_targets{};

  for (int column = 0; column < kBoardSize; ++column) {
    graph.stats.occupied += heights[column];
    graph.stats.maximum_height =
        std::max(graph.stats.maximum_height, heights[column]);
    graph.stats.open_columns -= heights[column] == kBoardSize;
  }
  graph.stats.top_slack = kBoardSize - graph.stats.maximum_height;

  constexpr std::array<std::array<int, 2>, 4> directions{{
      {{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}},
  }};
  for (int row = 0; row < kBoardSize; ++row) {
    const int elevation = kBoardSize - row;
    for (int column = 0; column < kBoardSize; ++column) {
      const int cell_index = indexOf(row, column);
      const std::uint8_t cell = board[cell_index];
      if (cell == kEmpty) continue;
      if (cell == kSolid || cell == kCracked) {
        const int type_weight = cell == kSolid ? 3 : 2;
        graph.stats.solid_cells += cell == kSolid;
        graph.stats.cracked_cells += cell == kCracked;
        graph.stats.cover_altitude_debt +=
            type_weight * elevation * elevation;
        if (column == 0 || column == kBoardSize - 1) {
          graph.stats.edge_cover_debt += type_weight * elevation * elevation;
        }
        for (const int neighbor : {column - 1, column + 1}) {
          if (neighbor < 0 || neighbor >= kBoardSize ||
              heights[neighbor] >= elevation) {
            continue;
          }
          const int distance = elevation - heights[neighbor];
          graph.stats.frontier_access +=
              type_weight * std::max(0, kBoardSize + 1 - distance);
        }
        continue;
      }
      if (cell == kInertReveal) {
        ++graph.stats.inert_cells;
        continue;
      }
      if (!isNumbered(cell)) {
        throw std::invalid_argument("invalid certificate board token");
      }

      ++graph.stats.numbered_cells;
      DiscNode node;
      node.cell = static_cast<std::uint8_t>(cell_index);
      node.value = cell;
      node.horizontal_length = static_cast<std::uint8_t>(
          lineLength(board, row, column, false));
      node.vertical_length = static_cast<std::uint8_t>(
          lineLength(board, row, column, true));
      node.horizontal_deficit = static_cast<std::int8_t>(
          static_cast<int>(cell) - node.horizontal_length);
      node.vertical_deficit = static_cast<std::int8_t>(
          static_cast<int>(cell) - node.vertical_length);
      node.horizontal_build_cost = static_cast<std::int8_t>(
          horizontalBuildCost(board, heights, row, column, cell));
      node.support_cell = static_cast<std::int8_t>(
          row + 1 < kBoardSize ? indexOf(row + 1, column) : -1);

      for (const auto& direction : directions) {
        const int next_row = row + direction[0];
        const int next_column = column + direction[1];
        if (!inside(next_row, next_column)) continue;
        const int next_index = indexOf(next_row, next_column);
        const std::uint8_t neighbor = board[next_index];
        if (neighbor == kSolid || neighbor == kCracked) {
          ++node.adjacent_covers;
          addEdge(graph, cell_index, next_index, EdgeKind::kCoverFrontier);
        } else if (neighbor != kEmpty) {
          const EdgeKind kind = direction[0] != 0
                                    ? EdgeKind::kVerticalSupport
                                    : EdgeKind::kLineNeighbor;
          addEdge(graph, cell_index, next_index, kind);
        }
      }

      const int value = cell;
      if (node.vertical_deficit > 0) {
        const int gap = node.vertical_deficit;
        const int high_multiplier = value >= 5 ? 2 : 1;
        graph.stats.stored_mass +=
            high_multiplier * (value + 1) * std::max(0, 8 - gap);
        graph.stats.release_ready += gap == 1;
        ++vertical_targets[column][value];
      }
      if (node.horizontal_build_cost > 0) {
        const int high_multiplier = value >= 5 ? 2 : 1;
        graph.stats.stored_mass +=
            high_multiplier * (value + 1) *
            std::max(0, 8 - node.horizontal_build_cost);
        graph.stats.release_ready += node.horizontal_build_cost == 1;
      }
      graph.nodes[graph.node_count++] = node;
    }
  }

  for (int column = 0; column < kBoardSize; ++column) {
    for (int value = 1; value <= kBoardSize; ++value) {
      const int count = vertical_targets[column][value];
      if (count >= 2) {
        const int pairs = count * (count - 1) / 2;
        graph.stats.same_target_pairs += pairs;
        graph.stats.stored_mass += pairs * value * 6;
      }
    }
  }

  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      const std::uint8_t cell = board[indexOf(row, column)];
      if (cell == 1) {
        if (column + 1 < kBoardSize &&
            board[indexOf(row, column + 1)] == 1) {
          ++graph.stats.adjacent_ones;
        }
        if (row + 1 < kBoardSize && board[indexOf(row + 1, column)] == 1) {
          ++graph.stats.adjacent_ones;
        }
      }
      if (cell == 1 || cell == 2) {
        const int horizontal = lineLength(board, row, column, false);
        const int vertical = lineLength(board, row, column, true);
        graph.stats.dead_low_numbers +=
            horizontal > cell && vertical > cell;
      }
    }
  }

  const auto countTwoRun = [&](int start_row, int start_column, int row_step,
                               int column_step) {
    int run = 0;
    int triples = 0;
    for (int offset = 0; offset < kBoardSize; ++offset) {
      const int row = start_row + row_step * offset;
      const int column = start_column + column_step * offset;
      if (board[indexOf(row, column)] == 2) {
        ++run;
      } else {
        triples += std::max(0, run - 2);
        run = 0;
      }
    }
    return triples + std::max(0, run - 2);
  };
  for (int row = 0; row < kBoardSize; ++row) {
    graph.stats.triple_twos += countTwoRun(row, 0, 0, 1);
  }
  for (int column = 0; column < kBoardSize; ++column) {
    graph.stats.triple_twos += countTwoRun(0, column, 1, 0);
    if (heights[column] == 0) continue;
    const std::uint8_t cap =
        board[indexOf(kBoardSize - heights[column], column)];
    graph.stats.capped_low_columns +=
        heights[column] >= 4 && (cap == 1 || cap == 2);
  }
  graph.stats.clog_debt = 8 * graph.stats.adjacent_ones +
                          12 * graph.stats.triple_twos +
                          4 * graph.stats.dead_low_numbers +
                          5 * graph.stats.capped_low_columns;
  return graph;
}

struct ConservativeResult {
  Board board{};
  bool played = false;
  bool terminal = false;
  bool level_advanced = false;
  int moves_remaining = 0;
  int clears = 0;
  int reveals = 0;
  int cracks = 0;
  int cover_hits = 0;
  int waves = 0;
  int maximum_depth = 0;

  bool operator==(const ConservativeResult&) const = default;
};

void resolveConservatively(Board& board, int starting_depth,
                           ConservativeResult& result) {
  constexpr std::array<std::array<int, 2>, 4> directions{{
      {{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}},
  }};
  for (int depth = starting_depth;; ++depth) {
    int popper_count = 0;
    const auto poppers = findPoppers(board, popper_count);
    if (popper_count == 0) return;
    std::array<bool, kCellCount> popping{};
    Board cleared = board;
    for (int offset = 0; offset < popper_count; ++offset) {
      const int popper = poppers[offset];
      popping[popper] = true;
      cleared[popper] = kEmpty;
    }
    for (int row = 0; row < kBoardSize; ++row) {
      for (int column = 0; column < kBoardSize; ++column) {
        const int cell_index = indexOf(row, column);
        const std::uint8_t cell = board[cell_index];
        if (cell != kSolid && cell != kCracked) continue;
        int hits = 0;
        for (const auto& direction : directions) {
          const int neighbor_row = row + direction[0];
          const int neighbor_column = column + direction[1];
          if (inside(neighbor_row, neighbor_column) &&
              popping[indexOf(neighbor_row, neighbor_column)]) {
            ++hits;
          }
        }
        result.cover_hits += hits;
        if (hits == 0) continue;
        const int required = cell == kSolid ? 2 : 1;
        if (hits >= required) {
          cleared[cell_index] = kInertReveal;
          ++result.reveals;
        } else if (cell == kSolid) {
          cleared[cell_index] = kCracked;
          ++result.cracks;
        }
      }
    }
    result.clears += popper_count;
    ++result.waves;
    result.maximum_depth = depth;
    board = applyGravity(cleared);
  }
}

ConservativeResult conservativePlay(const PublicState& state, int column,
                                    std::uint8_t disc) {
  ConservativeResult result;
  result.board = state.board;
  if (state.terminal || disc < 1 || disc > kBoardSize ||
      !placeDisc(result.board, column, disc)) {
    return result;
  }
  result.played = true;
  resolveConservatively(result.board, 1, result);
  result.moves_remaining = static_cast<int>(state.moves_remaining) - 1;
  if (result.moves_remaining == 0) {
    Board raised{};
    if (!raiseCoveredRow(result.board, raised)) {
      result.terminal = true;
    } else {
      result.level_advanced = true;
      result.moves_remaining = kMovesPerLevel;
      result.board = raised;
      resolveConservatively(result.board, result.maximum_depth + 1, result);
    }
  }
  int legal_count = 0;
  legalColumns(result.board, legal_count);
  if (!result.terminal && legal_count == 0) result.terminal = true;
  return result;
}

struct KeyCertificate {
  bool legal = false;
  bool survives = false;
  int clears = 0;
  int reveals = 0;
  int cracks = 0;
  int waves = 0;
  int maximum_depth = 0;
  int top_slack = 0;
  int build_gain = 0;
  int frontier_gain = 0;
  int clog_improvement = 0;
  int release_strength = 0;
  int quality = 0;
  bool productive = false;

  bool operator==(const KeyCertificate&) const = default;
};

struct TriggerSummary {
  int worst_safe_columns = kBoardSize;
  int worst_productive_columns = kBoardSize;
  int worst_best_release = std::numeric_limits<int>::max();
  int worst_best_quality = std::numeric_limits<int>::max();
  int total_safe_columns = 0;
  int total_productive_columns = 0;
  int total_best_release = 0;
  int total_best_quality = 0;
  int strong_keys = 0;

  bool operator==(const TriggerSummary&) const = default;
};

struct TriggerMatrix {
  std::array<std::array<KeyCertificate, kBoardSize>, kBoardSize> keys{};
  TriggerSummary summary{};

  bool operator==(const TriggerMatrix&) const = default;
};

TriggerMatrix buildTriggerMatrix(const PublicState& state,
                                 std::uint64_t& simulations) {
  TriggerMatrix matrix;
  if (state.terminal) {
    matrix.summary.worst_safe_columns = 0;
    matrix.summary.worst_productive_columns = 0;
    matrix.summary.worst_best_release = 0;
    matrix.summary.worst_best_quality = 0;
    return matrix;
  }
  const CertificateGraph before = buildCertificateGraph(state.board);
  for (int disc_offset = 0; disc_offset < kBoardSize; ++disc_offset) {
    int safe_columns = 0;
    int productive_columns = 0;
    int best_release = 0;
    int best_quality = std::numeric_limits<int>::min();
    for (int column = 0; column < kBoardSize; ++column) {
      KeyCertificate& key = matrix.keys[disc_offset][column];
      if (!isLegal(state.board, column)) continue;
      key.legal = true;
      const ConservativeResult release = conservativePlay(
          state, column, static_cast<std::uint8_t>(disc_offset + 1));
      ++simulations;
      if (!release.played) {
        throw std::runtime_error("conservative key rejected legal column");
      }
      const CertificateGraph after = buildCertificateGraph(release.board);
      key.survives = !release.terminal;
      key.clears = release.clears;
      key.reveals = release.reveals;
      key.cracks = release.cracks;
      key.waves = release.waves;
      key.maximum_depth = release.maximum_depth;
      key.top_slack = after.stats.top_slack;
      key.build_gain = after.stats.stored_mass - before.stats.stored_mass;
      key.frontier_gain =
          after.stats.frontier_access - before.stats.frontier_access;
      key.clog_improvement =
          before.stats.clog_debt - after.stats.clog_debt;
      key.release_strength = 12 * key.clears + 18 * key.reveals +
                             7 * key.cracks + 3 * key.waves +
                             key.maximum_depth;
      const int constructive = std::clamp(
          key.build_gain + key.frontier_gain + 2 * key.clog_improvement,
          -63, 63);
      key.quality = key.release_strength * 128 + constructive;
      key.productive = key.survives &&
                       (key.release_strength > 0 || key.build_gain >= 8 ||
                        key.clog_improvement >= 4);
      safe_columns += key.survives;
      productive_columns += key.productive;
      if (key.survives) {
        best_release = std::max(best_release, key.release_strength);
        best_quality = std::max(best_quality, key.quality);
      }
      matrix.summary.strong_keys +=
          key.survives && key.release_strength >= 24;
    }
    if (best_quality == std::numeric_limits<int>::min()) best_quality = 0;
    matrix.summary.worst_safe_columns =
        std::min(matrix.summary.worst_safe_columns, safe_columns);
    matrix.summary.worst_productive_columns =
        std::min(matrix.summary.worst_productive_columns,
                 productive_columns);
    matrix.summary.worst_best_release =
        std::min(matrix.summary.worst_best_release, best_release);
    matrix.summary.worst_best_quality =
        std::min(matrix.summary.worst_best_quality, best_quality);
    matrix.summary.total_safe_columns += safe_columns;
    matrix.summary.total_productive_columns += productive_columns;
    matrix.summary.total_best_release += best_release;
    matrix.summary.total_best_quality += best_quality;
  }
  if (matrix.summary.worst_best_release == std::numeric_limits<int>::max()) {
    matrix.summary.worst_best_release = 0;
  }
  if (matrix.summary.worst_best_quality == std::numeric_limits<int>::max()) {
    matrix.summary.worst_best_quality = 0;
  }
  return matrix;
}

enum class OptionMode : std::uint8_t {
  kCharge,
  kDig,
  kRelease,
  kRepair,
  kEmergency,
  kCount,
};

std::string_view optionName(OptionMode option) {
  switch (option) {
    case OptionMode::kCharge:
      return "charge";
    case OptionMode::kDig:
      return "dig";
    case OptionMode::kRelease:
      return "release";
    case OptionMode::kRepair:
      return "repair";
    case OptionMode::kEmergency:
      return "emergency";
    case OptionMode::kCount:
      break;
  }
  throw std::invalid_argument("invalid viability-reservoir option");
}

struct KnownDiscKeys {
  int best_clears = 0;
  int best_damage = 0;
  int best_waves = 0;
  int productive_columns = 0;
};

KnownDiscKeys knownDiscKeys(const TriggerMatrix& matrix, int disc) {
  if (disc < 1 || disc > kBoardSize) {
    throw std::invalid_argument("invalid known disc for trigger matrix");
  }
  KnownDiscKeys result;
  for (const KeyCertificate& key : matrix.keys[disc - 1]) {
    if (!key.legal) continue;
    result.best_clears = std::max(result.best_clears, key.clears);
    result.best_damage =
        std::max(result.best_damage, key.reveals + key.cracks);
    result.best_waves = std::max(result.best_waves, key.waves);
    result.productive_columns += key.productive;
  }
  return result;
}

OptionMode selectOption(const PublicState& state,
                        const CertificateGraph& graph,
                        const TriggerMatrix& triggers) {
  const GraphStats& stats = graph.stats;
  const KnownDiscKeys known = knownDiscKeys(triggers, state.next_disc);
  const int projected_occupancy =
      stats.occupied + state.moves_remaining + kBoardSize;

  // Frozen mechanics-derived thresholds: height six leaves one physical row;
  // 32 projected cells leaves less than 2.5 clears/move of slack through the
  // next rise; 16 clog-debt is two adjacent-one pairs or an equivalent low cap.
  if (stats.maximum_height >= 6 ||
      triggers.summary.worst_safe_columns <= 1 ||
      (state.moves_remaining == 1 && stats.occupied >= 27)) {
    return OptionMode::kEmergency;
  }
  if (stats.clog_debt >= 16 && stats.capped_low_columns > 0) {
    return OptionMode::kRepair;
  }
  if (projected_occupancy >= 32 || stats.occupied >= 30 ||
      (known.best_clears >= 3 && known.best_waves >= 2)) {
    return OptionMode::kRelease;
  }
  if (known.best_damage > 0 ||
      (stats.cracked_cells > 0 && stats.frontier_access > 0)) {
    return OptionMode::kDig;
  }
  return OptionMode::kCharge;
}

struct CandidateEvidence {
  int column = -1;
  bool conservative_survives = false;
  int terminal_samples = 0;
  int minimum_top_slack = kBoardSize;
  int minimum_worst_safe_columns = kBoardSize;
  int minimum_worst_productive_columns = kBoardSize;
  int minimum_worst_best_release = std::numeric_limits<int>::max();
  int minimum_worst_best_quality = std::numeric_limits<int>::max();
  std::int64_t score_sum = 0;
  int clears_sum = 0;
  int reveals_sum = 0;
  int cracks_sum = 0;
  int waves_sum = 0;
  int top_slack_sum = 0;
  int occupied_sum = 0;
  int cover_debt_sum = 0;
  int frontier_sum = 0;
  int stored_mass_sum = 0;
  int clog_debt_sum = 0;
  int total_productive_sum = 0;
  int total_quality_sum = 0;
  int conservative_clears = 0;
  int conservative_reveals = 0;
  int conservative_cracks = 0;
  std::uint64_t sampled_transitions = 0;
  std::uint64_t certificate_simulations = 0;
  std::array<std::int64_t, kRankFields> rank{};

  bool operator==(const CandidateEvidence&) const = default;
};

using Rank = std::array<std::int64_t, kRankFields>;

Rank buildRank(OptionMode option, const GraphStats& before,
               const CandidateEvidence& candidate) {
  Rank rank{};
  const int cover_reduction =
      kSuccessorSamples * before.cover_altitude_debt -
      candidate.cover_debt_sum;
  const int clog_reduction =
      kSuccessorSamples * before.clog_debt - candidate.clog_debt_sum;
  const int reservoir_gain =
      candidate.stored_mass_sum - kSuccessorSamples * before.stored_mass;
  rank[0] = candidate.conservative_survives;
  rank[1] = -candidate.terminal_samples;
  rank[2] = candidate.minimum_worst_safe_columns;

  switch (option) {
    case OptionMode::kEmergency:
      rank[3] = candidate.minimum_top_slack;
      rank[4] = candidate.clears_sum;
      rank[5] = candidate.reveals_sum;
      rank[6] = cover_reduction;
      rank[7] = -candidate.occupied_sum;
      rank[8] = -candidate.clog_debt_sum;
      rank[9] = candidate.waves_sum;
      rank[10] = candidate.score_sum;
      rank[11] = candidate.minimum_worst_best_quality;
      rank[12] = candidate.total_productive_sum;
      break;
    case OptionMode::kRepair:
      rank[3] = clog_reduction;
      rank[4] = -candidate.clog_debt_sum;
      rank[5] = candidate.clears_sum;
      rank[6] = candidate.minimum_worst_productive_columns;
      rank[7] = candidate.reveals_sum;
      rank[8] = candidate.minimum_top_slack;
      rank[9] = reservoir_gain;
      rank[10] = cover_reduction;
      rank[11] = candidate.score_sum;
      break;
    case OptionMode::kRelease:
      rank[3] = candidate.clears_sum;
      rank[4] = candidate.reveals_sum;
      rank[5] = candidate.waves_sum;
      rank[6] = candidate.minimum_top_slack;
      rank[7] = candidate.minimum_worst_best_release;
      rank[8] = cover_reduction;
      rank[9] = -candidate.clog_debt_sum;
      rank[10] = candidate.total_productive_sum;
      rank[11] = candidate.score_sum;
      break;
    case OptionMode::kDig:
      rank[3] = candidate.reveals_sum;
      rank[4] = candidate.conservative_reveals;
      rank[5] = candidate.cracks_sum;
      rank[6] = candidate.conservative_cracks;
      rank[7] = cover_reduction;
      rank[8] = candidate.clears_sum;
      rank[9] = candidate.minimum_worst_productive_columns;
      rank[10] = reservoir_gain;
      rank[11] = -candidate.clog_debt_sum;
      rank[12] = candidate.score_sum;
      break;
    case OptionMode::kCharge:
      rank[3] = candidate.minimum_worst_productive_columns;
      rank[4] = candidate.minimum_worst_best_quality;
      rank[5] = candidate.total_quality_sum;
      rank[6] = reservoir_gain;
      rank[7] = candidate.reveals_sum + candidate.cracks_sum -
                candidate.clears_sum;
      rank[8] = -candidate.clog_debt_sum;
      rank[9] = cover_reduction;
      rank[10] = candidate.minimum_top_slack;
      rank[11] = candidate.total_productive_sum;
      rank[12] = candidate.score_sum;
      break;
    case OptionMode::kCount:
      throw std::invalid_argument("cannot rank sentinel option");
  }
  rank[13] = candidate.top_slack_sum;
  rank[14] = candidate.frontier_sum;
  rank[15] = -candidate.occupied_sum;
  rank[16] = -candidate.column;
  rank[17] = 1;
  return rank;
}

struct Decision {
  int action = -1;
  OptionMode option = OptionMode::kEmergency;
  std::array<CandidateEvidence, kBoardSize> candidates{};
  std::uint64_t sampled_transitions = 0;
  std::uint64_t certificate_simulations = 0;

  bool operator==(const Decision&) const = default;
};

Decision chooseActionCanonical(const PublicState& state) {
  Decision result;
  if (state.terminal) return result;
  const CertificateGraph before = buildCertificateGraph(state.board);
  std::uint64_t matrix_work = 0;
  const TriggerMatrix current = buildTriggerMatrix(state, matrix_work);
  result.certificate_simulations += matrix_work;
  result.option = selectOption(state, before, current);
  const State engine_state = materialize(state);
  const std::uint32_t chance_seed =
      detail::scenarioSeedForState(engine_state, kPolicySeed, 1);

  int selected = -1;
  Rank best_rank{};
  bool have_best = false;
  for (const int column : kColumnOrder) {
    CandidateEvidence candidate;
    candidate.column = column;
    if (!isLegal(state.board, column)) {
      result.candidates[column] = candidate;
      continue;
    }

    const ConservativeResult conservative =
        conservativePlay(state, column, state.next_disc);
    ++candidate.certificate_simulations;
    candidate.conservative_survives =
        conservative.played && !conservative.terminal;
    candidate.conservative_clears = conservative.clears;
    candidate.conservative_reveals = conservative.reveals;
    candidate.conservative_cracks = conservative.cracks;

    for (int sample = 0; sample < kSuccessorSamples; ++sample) {
      detail::StratifiedRandom random{
          chance_seed, sample, kSuccessorSamples, 0};
      MoveResult move;
      if (!detail::playMoveSampled(engine_state, column, random, move)) {
        throw std::runtime_error("sampled root rejected legal action");
      }
      ++candidate.sampled_transitions;
      candidate.score_sum += move.score_delta;
      for (const Wave& wave : move.waves) {
        candidate.clears_sum += wave.cleared;
        candidate.reveals_sum += wave.revealed;
      }
      candidate.waves_sum += static_cast<int>(move.waves.size());
      candidate.terminal_samples += move.state.game_over;

      const CertificateGraph after = buildCertificateGraph(move.state.board);
      candidate.minimum_top_slack =
          std::min(candidate.minimum_top_slack, after.stats.top_slack);
      candidate.top_slack_sum += after.stats.top_slack;
      candidate.occupied_sum += after.stats.occupied;
      candidate.cover_debt_sum += after.stats.cover_altitude_debt;
      candidate.frontier_sum += after.stats.frontier_access;
      candidate.stored_mass_sum += after.stats.stored_mass;
      candidate.clog_debt_sum += after.stats.clog_debt;

      if (move.state.game_over) {
        candidate.minimum_worst_safe_columns = 0;
        candidate.minimum_worst_productive_columns = 0;
        candidate.minimum_worst_best_release = 0;
        candidate.minimum_worst_best_quality = 0;
        continue;
      }
      move.state.score = 0;
      move.state.level = 1;
      move.state.moves_played = 0;
      move.state.next_disc = detail::sampledNextDisc(
          chance_seed, sample, kSuccessorSamples);
      std::uint64_t work = 0;
      const TriggerMatrix future =
          buildTriggerMatrix(publicState(move.state), work);
      candidate.certificate_simulations += work;
      candidate.minimum_worst_safe_columns =
          std::min(candidate.minimum_worst_safe_columns,
                   future.summary.worst_safe_columns);
      candidate.minimum_worst_productive_columns =
          std::min(candidate.minimum_worst_productive_columns,
                   future.summary.worst_productive_columns);
      candidate.minimum_worst_best_release =
          std::min(candidate.minimum_worst_best_release,
                   future.summary.worst_best_release);
      candidate.minimum_worst_best_quality =
          std::min(candidate.minimum_worst_best_quality,
                   future.summary.worst_best_quality);
      candidate.total_productive_sum +=
          future.summary.total_productive_columns;
      candidate.total_quality_sum += future.summary.total_best_quality;
    }
    if (candidate.minimum_worst_best_release ==
        std::numeric_limits<int>::max()) {
      candidate.minimum_worst_best_release = 0;
    }
    if (candidate.minimum_worst_best_quality ==
        std::numeric_limits<int>::max()) {
      candidate.minimum_worst_best_quality = 0;
    }
    candidate.rank = buildRank(result.option, before.stats, candidate);
    result.sampled_transitions += candidate.sampled_transitions;
    result.certificate_simulations += candidate.certificate_simulations;
    result.candidates[column] = candidate;
    if (!have_best || candidate.rank > best_rank) {
      have_best = true;
      best_rank = candidate.rank;
      selected = column;
    }
  }
  if (selected < 0) selected = centerFirstMove(state.board);
  result.action = selected;
  return result;
}

Decision chooseAction(const PublicState& source) {
  if (source.terminal) return {};
  bool mirrored = false;
  const PublicState canonical = canonicalState(source, mirrored);
  Decision result = chooseActionCanonical(canonical);
  if (!mirrored) return result;
  std::array<CandidateEvidence, kBoardSize> source_candidates{};
  for (int column = 0; column < kBoardSize; ++column) {
    source_candidates[kBoardSize - 1 - column] = result.candidates[column];
    if (source_candidates[kBoardSize - 1 - column].column >= 0) {
      source_candidates[kBoardSize - 1 - column].column =
          kBoardSize - 1 - result.candidates[column].column;
    }
  }
  result.candidates = source_candidates;
  result.action = kBoardSize - 1 - result.action;
  return result;
}

using PublicPolicy = Decision (*)(const PublicState&);
static_assert(std::is_same_v<decltype(&chooseAction), PublicPolicy>);
static_assert(!std::is_invocable_v<PublicPolicy, const State&>);

struct BaselineDecision {
  int action = -1;
  std::uint64_t work = 0;
  bool complete = false;
};

BaselineDecision chooseFairDepthOne(const PublicState& source) {
  BaselineDecision result;
  if (source.terminal) return result;
  bool mirrored = false;
  const PublicState canonical_public = canonicalState(source, mirrored);
  const State canonical = materialize(canonical_public);
  fair::SearchContext context;
  const fair::RootEvaluation root = fair::rootDecision(canonical, 1, context);
  int legal = 0;
  int evaluated = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    legal += isLegal(canonical.board, column);
    evaluated += std::isfinite(root.values[column]);
  }
  if (root.action < 0 || legal != evaluated || context.work > 70 ||
      !context.cache.empty()) {
    throw std::runtime_error("fair D1 did not complete exactly");
  }
  result.action = mirrored ? kBoardSize - 1 - root.action : root.action;
  result.work = context.work;
  result.complete = true;
  return result;
}

enum class Policy : std::uint8_t { kViability, kFairD1, kFairD4 };

bool allowedStageASeed(std::uint32_t seed) {
  return seed >= kStageASeedStart && seed < kStageASeedEndExclusive &&
         (seed >> 24u) != 0x4du && (seed >> 24u) != 0x7du &&
         (seed >> 24u) != 0xd7u;
}

void requireStageASeed(std::uint32_t seed) {
  if (!allowedStageASeed(seed)) {
    throw std::invalid_argument("seed is outside the frozen 0x3d65c Stage-A bank");
  }
}

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool natural_terminal = false;
  bool capped = false;
  int clears = 0;
  int reveals = 0;
  int waves = 0;
  int maximum_chain = 0;
  std::array<std::uint64_t, static_cast<std::size_t>(OptionMode::kCount)>
      option_counts{};
  std::uint64_t sampled_transitions = 0;
  std::uint64_t certificate_simulations = 0;
  std::uint64_t search_work = 0;
  std::uint64_t disc_hash = 0xcbf2'9ce4'8422'2325ull;
  std::int64_t graph_stored_sum = 0;
  std::int64_t graph_clog_sum = 0;
  std::int64_t graph_cover_sum = 0;
  std::int64_t graph_frontier_sum = 0;

  bool operator==(const GameResult&) const = default;
};

void observeDisc(GameResult& result, std::uint8_t disc) {
  result.disc_hash ^= disc;
  result.disc_hash *= 0x0000'0100'0000'01b3ull;
}

void observeMove(GameResult& result, const MoveResult& move) {
  for (const Wave& wave : move.waves) {
    result.clears += wave.cleared;
    result.reveals += wave.revealed;
    result.maximum_chain = std::max(result.maximum_chain, wave.depth);
  }
  result.waves += static_cast<int>(move.waves.size());
}

GameResult playGame(std::uint32_t seed, Policy policy,
                    const Deadline& deadline) {
  requireStageASeed(seed);
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    deadline.check();
    enforceRssLimit();
    observeDisc(result, state.next_disc);
    const PublicState public_state = publicState(state);
    const CertificateGraph graph = buildCertificateGraph(state.board);
    result.graph_stored_sum += graph.stats.stored_mass;
    result.graph_clog_sum += graph.stats.clog_debt;
    result.graph_cover_sum += graph.stats.cover_altitude_debt;
    result.graph_frontier_sum += graph.stats.frontier_access;

    int action = -1;
    if (policy == Policy::kViability) {
      const Decision decision = chooseAction(public_state);
      action = decision.action;
      ++result.option_counts[static_cast<std::size_t>(decision.option)];
      result.sampled_transitions += decision.sampled_transitions;
      result.certificate_simulations += decision.certificate_simulations;
    } else if (policy == Policy::kFairD1) {
      const BaselineDecision decision = chooseFairDepthOne(public_state);
      if (!decision.complete) {
        throw std::runtime_error("fair D1 returned incomplete gameplay move");
      }
      action = decision.action;
      result.search_work += decision.work;
    } else {
      State metadata_free = materialize(public_state);
      const d4::SearchDecision decision = d4::chooseDepth4Action(metadata_free);
      if (!decision.complete) {
        throw std::runtime_error("fair D4 returned incomplete gameplay move");
      }
      action = decision.action;
      result.search_work += decision.work;
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("policy selected an illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("headless engine rejected policy action");
    }
    observeMove(result, move);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.natural_terminal = state.game_over;
  result.capped = !state.game_over && state.moves_played == kMaximumMoves;
  return result;
}

struct Summary {
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double bottom_quartile_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double waves_per_move = 0.0;
  double stored_mass_per_state = 0.0;
  double clog_debt_per_state = 0.0;
  double cover_debt_per_state = 0.0;
  double frontier_per_state = 0.0;
  int natural_terminals = 0;
  int capped = 0;
  int maximum_chain = 0;
  std::array<std::uint64_t, static_cast<std::size_t>(OptionMode::kCount)>
      option_counts{};
  std::uint64_t sampled_transitions = 0;
  std::uint64_t certificate_simulations = 0;
  std::uint64_t search_work = 0;
};

Summary summarize(const std::vector<GameResult>& games) {
  if (games.empty()) throw std::invalid_argument("cannot summarize no games");
  Summary result;
  std::vector<int> moves;
  std::int64_t total_score = 0;
  std::int64_t total_moves = 0;
  std::int64_t total_clears = 0;
  std::int64_t total_reveals = 0;
  std::int64_t total_waves = 0;
  std::int64_t stored = 0;
  std::int64_t clog = 0;
  std::int64_t cover = 0;
  std::int64_t frontier = 0;
  moves.reserve(games.size());
  for (const GameResult& game : games) {
    total_score += game.score;
    total_moves += game.moves;
    total_clears += game.clears;
    total_reveals += game.reveals;
    total_waves += game.waves;
    stored += game.graph_stored_sum;
    clog += game.graph_clog_sum;
    cover += game.graph_cover_sum;
    frontier += game.graph_frontier_sum;
    result.natural_terminals += game.natural_terminal;
    result.capped += game.capped;
    result.maximum_chain = std::max(result.maximum_chain, game.maximum_chain);
    for (std::size_t option = 0; option < result.option_counts.size();
         ++option) {
      result.option_counts[option] += game.option_counts[option];
    }
    result.sampled_transitions += game.sampled_transitions;
    result.certificate_simulations += game.certificate_simulations;
    result.search_work += game.search_work;
    moves.push_back(game.moves);
  }
  const double count = static_cast<double>(games.size());
  result.mean_score = static_cast<double>(total_score) / count;
  result.mean_moves = static_cast<double>(total_moves) / count;
  if (total_moves > 0) {
    result.clears_per_move =
        static_cast<double>(total_clears) / static_cast<double>(total_moves);
    result.reveals_per_move =
        static_cast<double>(total_reveals) / static_cast<double>(total_moves);
    result.waves_per_move =
        static_cast<double>(total_waves) / static_cast<double>(total_moves);
    result.stored_mass_per_state =
        static_cast<double>(stored) / static_cast<double>(total_moves);
    result.clog_debt_per_state =
        static_cast<double>(clog) / static_cast<double>(total_moves);
    result.cover_debt_per_state =
        static_cast<double>(cover) / static_cast<double>(total_moves);
    result.frontier_per_state =
        static_cast<double>(frontier) / static_cast<double>(total_moves);
  }
  std::sort(moves.begin(), moves.end());
  const std::size_t bottom_count = std::max<std::size_t>(1, games.size() / 4);
  result.bottom_quartile_moves =
      static_cast<double>(std::accumulate(moves.begin(),
                                          moves.begin() + bottom_count, 0LL)) /
      static_cast<double>(bottom_count);
  return result;
}

std::vector<GameResult> evaluate(Policy policy, int threads,
                                 const Deadline& deadline) {
  if (threads < 1 || threads > 8) {
    throw std::invalid_argument("threads must be in [1, 8]");
  }
  std::vector<GameResult> games(kStageAGames);
  std::atomic<int> next{0};
  const int workers = std::min(threads, kStageAGames);
  std::vector<std::future<void>> futures;
  futures.reserve(workers);
  for (int worker = 0; worker < workers; ++worker) {
    futures.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int index = next.fetch_add(1);
        if (index >= kStageAGames) return;
        games[index] = playGame(
            kStageASeedStart + static_cast<std::uint32_t>(index), policy,
            deadline);
      }
    }));
  }
  for (auto& future : futures) future.get();
  deadline.check();
  enforceRssLimit();
  return games;
}

struct PairedSummary {
  int candidate_score_wins = 0;
  int candidate_move_wins = 0;
  int candidate_joint_wins = 0;
  double mean_score_delta = 0.0;
  double mean_move_delta = 0.0;
};

PairedSummary pair(const std::vector<GameResult>& candidate,
                   const std::vector<GameResult>& baseline) {
  if (candidate.size() != baseline.size() || candidate.empty()) {
    throw std::invalid_argument("paired cohorts do not align");
  }
  PairedSummary result;
  for (std::size_t index = 0; index < candidate.size(); ++index) {
    // The headless next-disc tape is action-independent, but games with
    // different lifetimes necessarily hash different-length prefixes.  Seed
    // identity is therefore the exact pairing invariant; full-stream hashes
    // remain useful only for equal-length determinism checks.
    if (candidate[index].seed != baseline[index].seed) {
      throw std::runtime_error("paired seed mismatch");
    }
    if (candidate[index].moves == baseline[index].moves &&
        candidate[index].disc_hash != baseline[index].disc_hash) {
      throw std::runtime_error("equal-length paired disc-stream mismatch");
    }
    const bool score_win = candidate[index].score > baseline[index].score;
    const bool move_win = candidate[index].moves > baseline[index].moves;
    result.candidate_score_wins += score_win;
    result.candidate_move_wins += move_win;
    result.candidate_joint_wins += score_win && move_win;
    result.mean_score_delta +=
        static_cast<double>(candidate[index].score - baseline[index].score);
    result.mean_move_delta += candidate[index].moves - baseline[index].moves;
  }
  result.mean_score_delta /= static_cast<double>(candidate.size());
  result.mean_move_delta /= static_cast<double>(candidate.size());
  return result;
}

struct Options {
  std::string output = "/tmp/drop7-viability-reservoir-stage-a.json";
  int threads = 4;
};

Options parseOptions(int argc, char** argv, int begin) {
  Options options;
  for (int index = begin; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--output" && index + 1 < argc) {
      options.output = argv[++index];
    } else if (argument == "--threads" && index + 1 < argc) {
      options.threads = std::stoi(argv[++index]);
    } else {
      throw std::invalid_argument("unknown or incomplete option");
    }
  }
  if (options.output.empty()) throw std::invalid_argument("empty output path");
  if (options.threads < 1 || options.threads > 8) {
    throw std::invalid_argument("threads must be in [1, 8]");
  }
  return options;
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"bottomQuartileMoves\":" << summary.bottom_quartile_moves
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"wavesPerMove\":" << summary.waves_per_move
         << ",\"storedMassPerState\":" << summary.stored_mass_per_state
         << ",\"clogDebtPerState\":" << summary.clog_debt_per_state
         << ",\"coverDebtPerState\":" << summary.cover_debt_per_state
         << ",\"frontierPerState\":" << summary.frontier_per_state
         << ",\"naturalTerminals\":" << summary.natural_terminals
         << ",\"capped\":" << summary.capped
         << ",\"maximumChain\":" << summary.maximum_chain
         << ",\"sampledTransitions\":" << summary.sampled_transitions
         << ",\"certificateSimulations\":"
         << summary.certificate_simulations
         << ",\"searchWork\":" << summary.search_work
         << ",\"options\":{";
  for (std::size_t option = 0; option < summary.option_counts.size(); ++option) {
    if (option > 0) output << ',';
    output << '\"'
           << optionName(static_cast<OptionMode>(option)) << "\":"
           << summary.option_counts[option];
  }
  output << "}}";
}

void writeGame(std::ostream& output, const GameResult& game) {
  output << "{\"seed\":\"0x" << std::hex << game.seed << std::dec
         << "\",\"score\":" << game.score << ",\"moves\":" << game.moves
         << ",\"naturalTerminal\":"
         << (game.natural_terminal ? "true" : "false")
         << ",\"capped\":" << (game.capped ? "true" : "false")
         << ",\"clears\":" << game.clears
         << ",\"reveals\":" << game.reveals
         << ",\"waves\":" << game.waves
         << ",\"maximumChain\":" << game.maximum_chain << '}';
}

void writeArtifact(const Options& options,
                   const std::vector<GameResult>& candidate,
                   const Summary& candidate_summary,
                   const std::vector<GameResult>& fair_d1,
                   const Summary& fair_d1_summary,
                   const PairedSummary& d1_paired, bool absolute_gate,
                   const std::vector<GameResult>* fair_d4,
                   const Summary* fair_d4_summary,
                   const PairedSummary* d4_paired, bool passed,
                   double wall_seconds) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open Stage-A artifact");
  output << std::setprecision(12)
         << "{\n  \"format\":\"drop7-viability-reservoir-stage-a-v1\","
         << "\n  \"seedStart\":\"0x3d65c000\","
         << "\n  \"games\":" << kStageAGames
         << ",\n  \"maximumMoves\":" << kMaximumMoves
         << ",\n  \"publicOnly\":true,"
         << "\n  \"conservativeInertReveals\":true,"
         << "\n  \"triggerMatrix\":[7,7],"
         << "\n  \"parameters\":0,"
         << "\n  \"fittingSeedsOpened\":false,"
         << "\n  \"candidate\":";
  writeSummary(output, candidate_summary);
  output << ",\n  \"fairD1\":";
  writeSummary(output, fair_d1_summary);
  output << ",\n  \"d1Paired\":{\"scoreWins\":"
         << d1_paired.candidate_score_wins << ",\"moveWins\":"
         << d1_paired.candidate_move_wins << ",\"jointWins\":"
         << d1_paired.candidate_joint_wins << ",\"meanScoreDelta\":"
         << d1_paired.mean_score_delta << ",\"meanMoveDelta\":"
         << d1_paired.mean_move_delta << "},"
         << "\n  \"absoluteGate\":" << (absolute_gate ? "true" : "false")
         << ",\n  \"d4Opened\":" << (fair_d4 ? "true" : "false");
  if (fair_d4 && fair_d4_summary && d4_paired) {
    output << ",\n  \"fairD4\":";
    writeSummary(output, *fair_d4_summary);
    output << ",\n  \"d4Paired\":{\"scoreWins\":"
           << d4_paired->candidate_score_wins << ",\"moveWins\":"
           << d4_paired->candidate_move_wins << ",\"jointWins\":"
           << d4_paired->candidate_joint_wins << ",\"meanScoreDelta\":"
           << d4_paired->mean_score_delta << ",\"meanMoveDelta\":"
           << d4_paired->mean_move_delta << '}';
  }
  output << ",\n  \"gate\":{\"meanScore\":" << kGateMeanScore
         << ",\"meanMoves\":" << kGateMeanMoves
         << ",\"clearsPerMove\":" << kGateClearsPerMove
         << ",\"revealsPerMove\":" << kGateRevealsPerMove
         << ",\"bottomQuartileMoves\":" << kGateBottomQuartileMoves
         << ",\"jointD4Wins\":" << kGateJointWins << "},"
         << "\n  \"passed\":" << (passed ? "true" : "false")
         << ",\n  \"wallSeconds\":" << wall_seconds
         << ",\n  \"peakRssBytes\":" << peakRssBytes()
         << ",\n  \"candidateGames\":[";
  for (std::size_t index = 0; index < candidate.size(); ++index) {
    if (index > 0) output << ',';
    writeGame(output, candidate[index]);
  }
  output << "],\n  \"fairD1Games\":[";
  for (std::size_t index = 0; index < fair_d1.size(); ++index) {
    if (index > 0) output << ',';
    writeGame(output, fair_d1[index]);
  }
  output << ']';
  if (fair_d4) {
    output << ",\n  \"fairD4Games\":[";
    for (std::size_t index = 0; index < fair_d4->size(); ++index) {
      if (index > 0) output << ',';
      writeGame(output, (*fair_d4)[index]);
    }
    output << ']';
  }
  output << "\n}\n";
  if (!output) throw std::runtime_error("failed writing Stage-A artifact");
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

template <typename Function>
bool throwsInvalid(Function&& function) {
  try {
    function();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

PublicState asymmetricFixture() {
  PublicState fixture;
  fixture.board.fill(kEmpty);
  fixture.board[indexOf(6, 0)] = kSolid;
  fixture.board[indexOf(5, 0)] = 4;
  fixture.board[indexOf(6, 1)] = kSolid;
  fixture.board[indexOf(5, 1)] = 1;
  fixture.board[indexOf(6, 2)] = kSolid;
  fixture.board[indexOf(6, 3)] = kCracked;
  fixture.board[indexOf(6, 4)] = kSolid;
  fixture.board[indexOf(5, 4)] = 6;
  fixture.board[indexOf(4, 4)] = 5;
  fixture.next_disc = 3;
  fixture.moves_remaining = 4;
  return fixture;
}

bool selfTest(std::ostream& output) {
  expect(kLevelBonus == 17'000, "corrected Hardcore scoring regression");
  const PublicState fixture = asymmetricFixture();
  int popper_count = 0;
  findPoppers(fixture.board, popper_count);
  expect(popper_count == 0, "asymmetric certificate fixture is unstable");

  const CertificateGraph graph = buildCertificateGraph(fixture.board);
  const CertificateGraph reflected_graph =
      buildCertificateGraph(mirror(fixture).board);
  expect(graph.stats == reflected_graph.stats && graph.node_count > 0 &&
             graph.edge_count > 0 && graph.stats.stored_mass > 0 &&
             graph.stats.frontier_edges > 0,
         "causal certificate graph/reflection fixture failed");

  std::uint64_t work = 0;
  const TriggerMatrix matrix = buildTriggerMatrix(fixture, work);
  std::uint64_t reflected_work = 0;
  const TriggerMatrix reflected_matrix =
      buildTriggerMatrix(mirror(fixture), reflected_work);
  expect(work == 49 && reflected_work == 49 &&
             matrix.summary == reflected_matrix.summary,
         "7x7 trigger matrix work/reflection summary failed");
  for (int disc = 0; disc < kBoardSize; ++disc) {
    for (int column = 0; column < kBoardSize; ++column) {
      expect(matrix.keys[disc][column] ==
                 reflected_matrix.keys[disc][kBoardSize - 1 - column],
             "7x7 trigger certificate reflection failed");
    }
  }

  PublicState crack;
  crack.board.fill(kEmpty);
  crack.board[indexOf(6, 0)] = kSolid;
  crack.next_disc = 1;
  crack.moves_remaining = 5;
  const ConservativeResult cracked = conservativePlay(crack, 0, 1);
  expect(cracked.played && !cracked.terminal && cracked.clears == 1 &&
             cracked.cracks == 1 && cracked.reveals == 0 &&
             cracked.board[indexOf(6, 0)] == kCracked,
         "single-hit conservative cover certificate failed");
  crack.board[indexOf(6, 0)] = kCracked;
  const ConservativeResult revealed = conservativePlay(crack, 0, 1);
  expect(revealed.reveals == 1 && revealed.cracks == 0 &&
             revealed.board[indexOf(6, 0)] == kInertReveal,
         "inert conservative reveal certificate failed");

  const Decision first = chooseAction(fixture);
  const Decision repeated = chooseAction(fixture);
  const Decision reflected = chooseAction(mirror(fixture));
  expect(first == repeated && isLegal(fixture.board, first.action),
         "controller determinism/legality failed");
  expect(reflected.action == kBoardSize - 1 - first.action &&
             reflected.option == first.option,
         "controller action/option reflection failed");
  for (int column = 0; column < kBoardSize; ++column) {
    expect(first.candidates[column].rank ==
               reflected.candidates[kBoardSize - 1 - column].rank,
           "controller rank reflection failed");
  }

  State metadata = materialize(fixture);
  metadata.score = 9'999'999;
  metadata.level = 777;
  metadata.moves_played = 888;
  expect(publicState(metadata) == fixture &&
             chooseAction(publicState(metadata)) == first,
         "controller used hidden score/level/history metadata");
  PublicState terminal = fixture;
  terminal.terminal = true;
  expect(chooseAction(terminal).action == -1,
         "terminal public state selected an action");

  PublicState emergency = fixture;
  emergency.board[indexOf(3, 4)] = 7;
  emergency.board[indexOf(2, 4)] = 7;
  emergency.board[indexOf(1, 4)] = 7;
  expect(selectOption(emergency, buildCertificateGraph(emergency.board),
                      buildTriggerMatrix(emergency, work)) ==
             OptionMode::kEmergency,
         "emergency option threshold failed");
  PublicState charge;
  charge.board = initialBoard();
  charge.next_disc = 4;
  charge.moves_remaining = 5;
  std::uint64_t charge_work = 0;
  const TriggerMatrix charge_matrix = buildTriggerMatrix(charge, charge_work);
  expect(selectOption(charge, buildCertificateGraph(charge.board),
                      charge_matrix) == OptionMode::kCharge,
         "charge option fixture failed");

  PublicState dig = charge;
  dig.next_disc = 1;
  std::uint64_t dig_work = 0;
  const TriggerMatrix dig_matrix = buildTriggerMatrix(dig, dig_work);
  expect(selectOption(dig, buildCertificateGraph(dig.board), dig_matrix) ==
             OptionMode::kDig,
         "dig option fixture failed");

  PublicState release_state;
  release_state.board.fill(kEmpty);
  for (int row = 2; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      release_state.board[indexOf(row, column)] = kSolid;
    }
  }
  release_state.next_disc = 6;
  release_state.moves_remaining = 5;
  std::uint64_t release_work = 0;
  const TriggerMatrix release_matrix =
      buildTriggerMatrix(release_state, release_work);
  expect(selectOption(release_state,
                      buildCertificateGraph(release_state.board),
                      release_matrix) == OptionMode::kRelease,
         "release option fixture failed");

  PublicState repair_state;
  repair_state.board.fill(kEmpty);
  for (int column = 0; column < 2; ++column) {
    for (int row = 3; row < kBoardSize; ++row) {
      repair_state.board[indexOf(row, column)] = kSolid;
    }
    repair_state.board[indexOf(2, column)] = 1;
  }
  repair_state.next_disc = 4;
  repair_state.moves_remaining = 5;
  std::uint64_t repair_work = 0;
  const TriggerMatrix repair_matrix =
      buildTriggerMatrix(repair_state, repair_work);
  expect(selectOption(repair_state,
                      buildCertificateGraph(repair_state.board),
                      repair_matrix) == OptionMode::kRepair,
         "repair option fixture failed");

  PublicState shield_state;
  shield_state.board = initialBoard();
  for (int row = 1; row < kBoardSize - 1; ++row) {
    shield_state.board[indexOf(row, 0)] = kSolid;
  }
  shield_state.next_disc = 4;
  shield_state.moves_remaining = 1;
  const Decision shield = chooseAction(shield_state);
  const ConservativeResult shield_release = conservativePlay(
      shield_state, shield.action, shield_state.next_disc);
  expect(shield.action != 0 && shield_release.played &&
             !shield_release.terminal,
         "hard conservative terminal shield failed");

  const BaselineDecision d1_first = chooseFairDepthOne(fixture);
  const BaselineDecision d1_repeat = chooseFairDepthOne(fixture);
  const BaselineDecision d1_reflected = chooseFairDepthOne(mirror(fixture));
  expect(d1_first.action == d1_repeat.action && d1_first.complete &&
             d1_reflected.action == kBoardSize - 1 - d1_first.action,
         "fair D1 determinism/reflection failed");

  expect(allowedStageASeed(kStageASeedStart) &&
             allowedStageASeed(kStageASeedEndExclusive - 1u) &&
             !allowedStageASeed(kStageASeedStart - 1u) &&
             !allowedStageASeed(kStageASeedEndExclusive) &&
             throwsInvalid([] { requireStageASeed(0x3d65'0000u); }) &&
             throwsInvalid([] { requireStageASeed(0x3d65'1000u); }) &&
             throwsInvalid([] { requireStageASeed(0x3d65'8000u); }) &&
             throwsInvalid([] { requireStageASeed(0x3d65'e000u); }) &&
             throwsInvalid([] { requireStageASeed(0x4d65'c000u); }) &&
             throwsInvalid([] { requireStageASeed(0x7d65'c000u); }) &&
             throwsInvalid([] { requireStageASeed(0xd765'c000u); }),
         "Stage-A seed guards failed");
  enforceRssLimit();
  output << "VIABILITY_RESERVOIR_SELF_TEST {\"passed\":true,"
         << "\"publicOnly\":true,\"metadataBlind\":true,"
         << "\"reflection\":true,\"deterministic\":true,"
         << "\"legal\":true,\"inertRevealConservative\":true,"
         << "\"triggerMatrix\":[7,7],\"certificateWork\":" << work
         << ",\"fittedParameters\":0,\"seedGuards\":true,"
         << "\"peakRssBytes\":" << peakRssBytes() << "}\n";
  return true;
}

int runStageA(const Options& options, std::ostream& output) {
  const Deadline deadline;
  const std::vector<GameResult> candidate =
      evaluate(Policy::kViability, options.threads, deadline);
  const Summary candidate_summary = summarize(candidate);
  const std::vector<GameResult> fair_d1 =
      evaluate(Policy::kFairD1, options.threads, deadline);
  const Summary fair_d1_summary = summarize(fair_d1);
  const PairedSummary d1_paired = pair(candidate, fair_d1);

  // The candidate's fixed absolute score/move thresholds gate the expensive
  // D4 comparison.  Throughput and tail gates cannot enable D4.
  const bool absolute_gate = candidate_summary.mean_score >= kGateMeanScore &&
                             candidate_summary.mean_moves >= kGateMeanMoves;
  std::vector<GameResult> fair_d4;
  Summary fair_d4_summary;
  PairedSummary d4_paired;
  if (absolute_gate) {
    fair_d4 = evaluate(Policy::kFairD4, options.threads, deadline);
    fair_d4_summary = summarize(fair_d4);
    d4_paired = pair(candidate, fair_d4);
  }
  const bool passed =
      absolute_gate &&
      candidate_summary.clears_per_move >= kGateClearsPerMove &&
      candidate_summary.reveals_per_move >= kGateRevealsPerMove &&
      candidate_summary.bottom_quartile_moves >=
          kGateBottomQuartileMoves &&
      d4_paired.candidate_joint_wins >= kGateJointWins;
  deadline.check();
  enforceRssLimit();
  const double wall_seconds = deadline.elapsedSeconds();
  writeArtifact(options, candidate, candidate_summary, fair_d1,
                fair_d1_summary, d1_paired, absolute_gate,
                absolute_gate ? &fair_d4 : nullptr,
                absolute_gate ? &fair_d4_summary : nullptr,
                absolute_gate ? &d4_paired : nullptr, passed, wall_seconds);
  output << std::fixed << std::setprecision(3)
         << "VIABILITY_RESERVOIR_STAGE_A {\"candidateScore\":"
         << candidate_summary.mean_score << ",\"candidateMoves\":"
         << candidate_summary.mean_moves << ",\"bottomQuartileMoves\":"
         << candidate_summary.bottom_quartile_moves
         << ",\"clearsPerMove\":" << candidate_summary.clears_per_move
         << ",\"revealsPerMove\":" << candidate_summary.reveals_per_move
         << ",\"fairD1Score\":" << fair_d1_summary.mean_score
         << ",\"fairD1Moves\":" << fair_d1_summary.mean_moves
         << ",\"d1JointWins\":" << d1_paired.candidate_joint_wins
         << ",\"absoluteGate\":" << (absolute_gate ? "true" : "false")
         << ",\"d4Opened\":" << (absolute_gate ? "true" : "false")
         << ",\"d4JointWins\":" << d4_paired.candidate_joint_wins
         << ",\"passed\":" << (passed ? "true" : "false")
         << ",\"wallSeconds\":" << wall_seconds
         << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return passed ? EXIT_SUCCESS : 2;
}

}  // namespace drop7::viability_reservoir_controller

#ifndef DROP7_VIABILITY_RESERVOIR_CONTROLLER_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::viability_reservoir_controller::selfTest(std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--stage-a") {
      const auto options =
          drop7::viability_reservoir_controller::parseOptions(argc, argv, 2);
      return drop7::viability_reservoir_controller::runStageA(options,
                                                               std::cout);
    }
    std::cerr << "usage: drop7_viability_reservoir_controller --self-test | "
                 "--stage-a [--output PATH] [--threads N]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_viability_reservoir_controller: " << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
}
#endif
