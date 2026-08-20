#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#define DROP7_FAIR_ONLY_DEPTH4_NO_MAIN
#include "../../fair-expectimax/reference/fair-only-depth4.cpp"
#undef DROP7_FAIR_ONLY_DEPTH4_NO_MAIN
#undef DROP7_FAIR_ONLY_DEPTH4_LIBRARY

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
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <type_traits>
#include <utility>
#include <vector>

// Integrates the fixed constructive-spectrum tie-break with the reference
// full-width fair-D4/s5 root.  D4 is a hard tactical shield: only
// its top two legal actions may enter the constructive rollout, and the second
// enters only within the unchanged 2,500-utility margin.  A singleton returns
// exact D4 without executing the structural rollout.  Every structural
// feature, weight, scenario, reward, horizon, and policy seed below matches the
// fixed configuration in constructive-spectrum.cpp.
namespace drop7::constructive_spectrum_depth4 {

namespace d4 = drop7::fair_only_depth4;
namespace detail = drop7::cfpi::detail;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kFittingSeedStart = 0x3d6a'0000u;
constexpr std::uint32_t kFittingSeedEndExclusive = 0x3d6a'0004u;
constexpr int kFittingGames = 4;
constexpr std::uint32_t kScreenSeedStart = 0x3d6a'1000u;
constexpr std::uint32_t kScreenSeedEndExclusive = 0x3d6a'1008u;
constexpr int kScreenGames = 8;
constexpr int kMaximumMoves = 1'000;
constexpr int kChanceSamples = 7;
constexpr int kMinimumHorizon = 3;
constexpr int kMaximumHorizon = 7;
constexpr int kTacticalShortlist = 2;
constexpr double kTacticalNearTie = 2'500.0;
constexpr int kDefaultThreads = 4;
constexpr std::uint32_t kPolicySeed = 0x4353'5031u;  // "CSP1"
constexpr double kTerminalValue = -1.0e9;
constexpr double kWallLimitSeconds = 60.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr std::array<int, kBoardSize> kColumnOrder{{3, 2, 4, 1, 5, 0, 6}};

constexpr double kFitScoreRatio = 1.10;
constexpr double kFitMoveRatio = 1.10;
constexpr int kFitJointWins = 3;
constexpr double kScreenScoreRatio = 1.05;
constexpr double kScreenMoveRatio = 1.05;
constexpr int kScreenJointWins = 5;

static_assert(kLevelBonus == 17'000);
static_assert(kMovesPerLevel == 5);
static_assert(d4::kCandidateDepth == 4 && d4::kChanceSamples == 5);
static_assert(d4::kMaximumWork > d4::kWorstCaseD4Work);
static_assert(kFittingSeedEndExclusive - kFittingSeedStart == kFittingGames);
static_assert(kScreenSeedEndExclusive - kScreenSeedStart == kScreenGames);
static_assert(kFittingSeedEndExclusive <= kScreenSeedStart);
static_assert((kFittingSeedStart >> 16u) == 0x3d6au &&
              ((kFittingSeedEndExclusive - 1u) >> 16u) == 0x3d6au &&
              (kScreenSeedStart >> 16u) == 0x3d6au &&
              ((kScreenSeedEndExclusive - 1u) >> 16u) == 0x3d6au);
static_assert((kFittingSeedStart >> 24u) != 0x4du &&
              (kFittingSeedStart >> 24u) != 0x7du &&
              (kFittingSeedStart >> 24u) != 0xd7u &&
              (kScreenSeedStart >> 24u) != 0x4du &&
              (kScreenSeedStart >> 24u) != 0x7du &&
              (kScreenSeedStart >> 24u) != 0xd7u);

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
    throw std::invalid_argument("invalid public D4 constructive state");
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

PublicState canonicalPublic(const PublicState& source, bool& mirrored) {
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
    throw std::runtime_error("D4 constructive integration exceeded 256 MiB");
  }
}

struct Deadline {
  Clock::time_point started = Clock::now();

  double seconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }

  void check() const {
    if (seconds() > kWallLimitSeconds) {
      throw std::runtime_error("D4 constructive integration exceeded 60 min");
    }
  }
};

std::array<int, kBoardSize> columnHeights(const Board& board) {
  std::array<int, kBoardSize> heights{};
  for (int column = 0; column < kBoardSize; ++column) {
    for (int row = 0; row < kBoardSize; ++row) {
      heights[column] += board[indexOf(row, column)] != kEmpty;
    }
  }
  return heights;
}

int topRow(const Board& board, int column) {
  for (int row = 0; row < kBoardSize; ++row) {
    if (board[indexOf(row, column)] != kEmpty) return row;
  }
  return kBoardSize;
}

struct TriggerKeys {
  int legal = 0;
  int any = 0;
  int multiple = 0;
  int placed = 0;
  int high = 0;
  int cover_contact = 0;
  int distinct_discs = 0;
  int distinct_columns = 0;
};

TriggerKeys exactTriggerKeys(const Board& source) {
  TriggerKeys result;
  std::array<bool, kBoardSize + 1> discs{};
  std::array<bool, kBoardSize> columns{};
  for (int disc = 1; disc <= kBoardSize; ++disc) {
    for (int column = 0; column < kBoardSize; ++column) {
      Board board = source;
      if (!placeDisc(board, column, static_cast<std::uint8_t>(disc))) continue;
      ++result.legal;
      int placed_index = -1;
      for (int row = 0; row < kBoardSize; ++row) {
        if (source[indexOf(row, column)] == kEmpty &&
            board[indexOf(row, column)] != kEmpty) {
          placed_index = indexOf(row, column);
          break;
        }
      }
      int count = 0;
      const auto poppers = findPoppers(board, count);
      if (count == 0) continue;
      ++result.any;
      result.multiple += count >= 2;
      discs[disc] = true;
      columns[column] = true;
      bool has_high = false;
      bool touches_cover = false;
      for (int offset = 0; offset < count; ++offset) {
        const int cell_index = poppers[offset];
        result.placed += cell_index == placed_index;
        has_high = has_high || board[cell_index] >= 5;
        const int row = cell_index / kBoardSize;
        const int pop_column = cell_index % kBoardSize;
        for (const auto [dr, dc] :
             std::array<std::array<int, 2>, 4>{{
                 {{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}},
             }}) {
          const int nr = row + dr;
          const int nc = pop_column + dc;
          if (!inside(nr, nc)) continue;
          const auto neighbor = board[indexOf(nr, nc)];
          touches_cover = touches_cover || neighbor == kSolid ||
                          neighbor == kCracked;
        }
      }
      result.high += has_high;
      result.cover_contact += touches_cover;
    }
  }
  result.distinct_discs = std::accumulate(discs.begin(), discs.end(), 0);
  result.distinct_columns =
      std::accumulate(columns.begin(), columns.end(), 0);
  return result;
}

enum Metric : int {
  kOccupancy,
  kCovers,
  kMaximumHeight,
  kHeightMean,
  kHeightStddev,
  kHeightRange,
  kDistinctHeights,
  kAdjacentHeightSteps,
  kUnitHeightSteps,
  kRoughness,
  kInteriorWells,
  kEdgeHeight,
  kSurfaceNumbered,
  kSurfaceHigh,
  kSurfaceLow,
  kSurfaceCover,
  kHighReservoir,
  kHighReady,
  kSameTargetHighPairs,
  kEdgeCovers,
  kEdgeCoverFrontier,
  kCoverNumberContacts,
  kTriggerAny,
  kTriggerMultiple,
  kTriggerPlaced,
  kTriggerHigh,
  kTriggerCover,
  kTriggerDiscBreadth,
  kTriggerColumnBreadth,
  kMetricCount,
};

using Metrics = std::array<double, kMetricCount>;

Metrics extractMetrics(const PublicState& state) {
  Metrics result{};
  const auto heights = columnHeights(state.board);
  const double mean = std::accumulate(heights.begin(), heights.end(), 0.0) /
                      static_cast<double>(kBoardSize);
  std::array<bool, kBoardSize + 1> seen_heights{};
  int minimum = kBoardSize;
  int maximum = 0;
  std::array<int, kBoardSize + 1> ready_high_by_target{};
  for (int column = 0; column < kBoardSize; ++column) {
    minimum = std::min(minimum, heights[column]);
    maximum = std::max(maximum, heights[column]);
    seen_heights[heights[column]] = true;
    result[kOccupancy] += heights[column];
    result[kHeightStddev] +=
        (static_cast<double>(heights[column]) - mean) *
        (static_cast<double>(heights[column]) - mean);
    if (column > 0) {
      const int difference = std::abs(heights[column] - heights[column - 1]);
      result[kRoughness] += difference;
      result[kAdjacentHeightSteps] += difference > 0;
      result[kUnitHeightSteps] += difference == 1;
    }
    if (column > 0 && column + 1 < kBoardSize &&
        heights[column] < heights[column - 1] &&
        heights[column] < heights[column + 1]) {
      ++result[kInteriorWells];
    }
    if (column == 0 || column == kBoardSize - 1) {
      result[kEdgeHeight] += heights[column];
    }
    const int surface_row = topRow(state.board, column);
    if (surface_row < kBoardSize) {
      const std::uint8_t cap = state.board[indexOf(surface_row, column)];
      result[kSurfaceNumbered] += isNumbered(cap);
      result[kSurfaceHigh] += cap >= 5 && cap <= 7;
      result[kSurfaceLow] += cap == 1 || cap == 2;
      result[kSurfaceCover] += cap == kSolid || cap == kCracked;
    }
  }
  result[kHeightMean] = mean;
  result[kHeightStddev] = std::sqrt(result[kHeightStddev] / kBoardSize);
  result[kMaximumHeight] = maximum;
  result[kHeightRange] = maximum - minimum;
  result[kDistinctHeights] =
      std::accumulate(seen_heights.begin(), seen_heights.end(), 0);

  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      const std::uint8_t cell = state.board[indexOf(row, column)];
      if (cell == kSolid || cell == kCracked) {
        ++result[kCovers];
        if (column == 0 || column == kBoardSize - 1) {
          ++result[kEdgeCovers];
          if (row == topRow(state.board, column)) {
            ++result[kEdgeCoverFrontier];
          }
        }
        for (const auto [dr, dc] :
             std::array<std::array<int, 2>, 4>{{
                 {{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}},
             }}) {
          const int nr = row + dr;
          const int nc = column + dc;
          if (inside(nr, nc) &&
              isNumbered(state.board[indexOf(nr, nc)])) {
            ++result[kCoverNumberContacts];
          }
        }
      }
      if (cell < 5 || cell > 7) continue;
      const int horizontal = lineLength(state.board, row, column, false);
      const int vertical = lineLength(state.board, row, column, true);
      const int deficit =
          static_cast<int>(cell) - std::max(horizontal, vertical);
      if (deficit > 0) ++result[kHighReservoir];
      if (deficit == 1 || deficit == 2) {
        ++result[kHighReady];
        ++ready_high_by_target[cell];
      }
    }
  }
  for (int target = 5; target <= 7; ++target) {
    result[kSameTargetHighPairs] +=
        ready_high_by_target[target] * (ready_high_by_target[target] - 1) / 2;
  }
  const TriggerKeys triggers = exactTriggerKeys(state.board);
  result[kTriggerAny] = triggers.any;
  result[kTriggerMultiple] = triggers.multiple;
  result[kTriggerPlaced] = triggers.placed;
  result[kTriggerHigh] = triggers.high;
  result[kTriggerCover] = triggers.cover_contact;
  result[kTriggerDiscBreadth] = triggers.distinct_discs;
  result[kTriggerColumnBreadth] = triggers.distinct_columns;
  return result;
}

double structuralValue(const PublicState& state) {
  if (state.terminal) return kTerminalValue;
  const Metrics m = extractMetrics(state);
  const auto excess = [](double value, double target) {
    return std::max(0.0, value - target);
  };
  const auto capped = [](double value, double target) {
    return std::min(value, target);
  };
  double value = 0.0;
  value -= 2'400.0 * excess(m[kOccupancy], 15.0);
  value -= 3'000.0 * excess(m[kCovers], 8.0);
  value -= 13'000.0 * std::pow(excess(m[kMaximumHeight], 4.0), 2.0);
  value -= 2'000.0 * excess(m[kEdgeCovers], 3.0);
  value -= 6'500.0 * excess(m[kSurfaceLow], 1.0);
  value -= 1'300.0 * excess(m[kRoughness], 7.0);
  value += 3'600.0 * capped(m[kHighReservoir], 4.0);
  value += 2'000.0 * capped(m[kSurfaceHigh], 3.0);
  value += 1'200.0 * capped(m[kSameTargetHighPairs], 2.0);
  value += 550.0 * capped(m[kTriggerCover], 13.0);
  value += 900.0 * capped(m[kTriggerMultiple], 5.0);
  value += 500.0 * capped(m[kTriggerDiscBreadth], 7.0);
  value += 350.0 * capped(m[kTriggerColumnBreadth], 7.0);
  value += 700.0 * capped(m[kDistinctHeights], 4.0);
  value += 350.0 * capped(m[kUnitHeightSteps], 3.0);
  value += 1'500.0 * capped(m[kEdgeCoverFrontier], 2.0);
  const double urgency =
      static_cast<double>(kMovesPerLevel - state.moves_remaining) /
      static_cast<double>(kMovesPerLevel - 1);
  value -= 1'500.0 * urgency * excess(m[kOccupancy] + 7.0, 19.0);
  return value;
}

struct SampledStep {
  State state{};
  std::int64_t score_delta = 0;
  int clears = 0;
  int reveals = 0;
  int waves = 0;
  bool played = false;
};

SampledStep sampledStep(const State& source, int source_column, int sample,
                        int depth_tag) {
  bool mirrored = false;
  const State canonical = detail::canonicalState(source, mirrored);
  const int column = mirrored ? kBoardSize - 1 - source_column : source_column;
  SampledStep result;
  if (!isLegal(canonical.board, column)) return result;
  const std::uint32_t seed =
      detail::scenarioSeedForState(canonical, kPolicySeed, depth_tag);
  detail::StratifiedRandom random{seed, sample, kChanceSamples, 0};
  MoveResult move;
  if (!detail::playMoveSampled(canonical, column, random, move)) return result;
  result.played = true;
  result.score_delta = move.score_delta;
  result.waves = static_cast<int>(move.waves.size());
  for (const Wave& wave : move.waves) {
    result.clears += wave.cleared;
    result.reveals += wave.revealed;
  }
  if (!move.state.game_over) {
    move.state.next_disc =
        detail::sampledNextDisc(seed, sample, kChanceSamples);
  }
  bool ignored = false;
  result.state = detail::canonicalState(move.state, ignored);
  return result;
}

struct OneStepDecision {
  int action = -1;
  double value = -std::numeric_limits<double>::infinity();
  std::uint64_t work = 0;
};

OneStepDecision constructiveContinuation(const State& source,
                                         int depth_tag) {
  OneStepDecision result;
  bool ignored = false;
  const State canonical = detail::canonicalState(source, ignored);
  for (const int column : kColumnOrder) {
    if (!isLegal(canonical.board, column)) continue;
    double total = 0.0;
    for (int sample = 0; sample < kChanceSamples; ++sample) {
      const SampledStep step = sampledStep(canonical, column, sample, depth_tag);
      ++result.work;
      if (!step.played || step.state.game_over) {
        total += kTerminalValue;
        continue;
      }
      total += static_cast<double>(step.score_delta) +
               5'000.0 * step.clears + 8'000.0 * step.reveals +
               500.0 * step.waves + structuralValue(publicState(step.state));
    }
    total /= kChanceSamples;
    if (total > result.value) {
      result.value = total;
      result.action = column;
    }
  }
  if (result.action < 0) result.action = centerFirstMove(canonical.board);
  return result;
}

struct Decision {
  int action = -1;
  int d4_action = -1;
  int shortlist = 0;
  int horizon = 0;
  bool complete = false;
  bool singleton_parity = false;
  std::uint64_t d4_work = 0;
  std::uint64_t rollout_work = 0;
  std::size_t d4_cache_entries = 0;
  std::array<double, kBoardSize> rollout_values{};
  std::array<double, kBoardSize> d4_values{};

  bool operator==(const Decision&) const = default;
};

Decision chooseActionCanonical(const PublicState& source) {
  Decision result;
  result.rollout_values.fill(-std::numeric_limits<double>::infinity());
  result.d4_values.fill(-std::numeric_limits<double>::infinity());
  if (source.terminal) return result;
  const State root = materialize(source);
  const d4::SearchDecision tactical = d4::chooseDepth4Action(root);
  if (!tactical.complete || tactical.completed_depth != d4::kCandidateDepth) {
    throw std::runtime_error("qualified D4 did not complete");
  }
  result.complete = true;
  result.d4_action = tactical.action;
  result.d4_work = tactical.work;
  result.d4_cache_entries = tactical.cache_entries;
  result.d4_values = tactical.root_values;

  std::array<int, kBoardSize> ranked_columns{};
  int ranked_count = 0;
  for (const int column : kColumnOrder) {
    if (isLegal(root.board, column)) ranked_columns[ranked_count++] = column;
  }
  std::stable_sort(ranked_columns.begin(), ranked_columns.begin() + ranked_count,
                   [&](int left, int right) {
                     return tactical.root_values[left] >
                            tactical.root_values[right];
                   });
  std::array<bool, kBoardSize> admitted{};
  for (int rank = 0; rank < std::min(ranked_count, kTacticalShortlist);
       ++rank) {
    const int column = ranked_columns[rank];
    if (tactical.root_values[column] <
        tactical.root_values[tactical.action] - kTacticalNearTie) {
      continue;
    }
    admitted[column] = true;
    ++result.shortlist;
  }
  if (result.shortlist < 1 || !admitted[tactical.action]) {
    throw std::runtime_error("D4 shortlist lost its tactical optimum");
  }
  if (result.shortlist == 1) {
    result.action = tactical.action;
    result.singleton_parity = true;
    return result;
  }

  result.horizon = std::clamp(static_cast<int>(source.moves_remaining) +
                                  kMovesPerLevel,
                              kMinimumHorizon, kMaximumHorizon);
  for (const int root_column : kColumnOrder) {
    if (!admitted[root_column]) continue;
    double root_total = 0.0;
    for (int root_sample = 0; root_sample < kChanceSamples; ++root_sample) {
      const SampledStep first =
          sampledStep(root, root_column, root_sample, result.horizon);
      ++result.rollout_work;
      if (!first.played || first.state.game_over) {
        root_total += kTerminalValue;
        continue;
      }
      State state = first.state;
      double trajectory = static_cast<double>(first.score_delta) +
                          5'000.0 * first.clears +
                          8'000.0 * first.reveals + 500.0 * first.waves;
      bool terminal = false;
      for (int step_index = 1; step_index < result.horizon; ++step_index) {
        const int depth_tag = result.horizon - step_index;
        const OneStepDecision continuation =
            constructiveContinuation(state, depth_tag);
        result.rollout_work += continuation.work;
        if (continuation.action < 0) {
          terminal = true;
          break;
        }
        const int sample =
            (root_sample + 2 * step_index) % kChanceSamples;
        const SampledStep next =
            sampledStep(state, continuation.action, sample, depth_tag);
        ++result.rollout_work;
        if (!next.played || next.state.game_over) {
          terminal = true;
          break;
        }
        trajectory += static_cast<double>(next.score_delta) +
                      5'000.0 * next.clears + 8'000.0 * next.reveals +
                      500.0 * next.waves;
        state = next.state;
      }
      root_total += terminal
                        ? kTerminalValue
                        : trajectory + structuralValue(publicState(state));
    }
    result.rollout_values[root_column] = root_total / kChanceSamples;
    if (result.action < 0 ||
        result.rollout_values[root_column] >
            result.rollout_values[result.action]) {
      result.action = root_column;
    }
  }
  if (!admitted[result.action]) {
    throw std::runtime_error("constructive choice escaped D4 shortlist");
  }
  return result;
}

Decision chooseAction(const PublicState& source) {
  if (source.terminal) return {};
  bool mirrored = false;
  const PublicState canonical = canonicalPublic(source, mirrored);
  Decision result = chooseActionCanonical(canonical);
  if (!mirrored) return result;
  result.action = kBoardSize - 1 - result.action;
  result.d4_action = kBoardSize - 1 - result.d4_action;
  std::array<double, kBoardSize> rollout_values{};
  std::array<double, kBoardSize> d4_values{};
  for (int column = 0; column < kBoardSize; ++column) {
    rollout_values[column] =
        result.rollout_values[kBoardSize - 1 - column];
    d4_values[column] = result.d4_values[kBoardSize - 1 - column];
  }
  result.rollout_values = rollout_values;
  result.d4_values = d4_values;
  return result;
}

using PublicPolicy = Decision (*)(const PublicState&);
static_assert(std::is_same_v<decltype(&chooseAction), PublicPolicy>);
static_assert(!std::is_invocable_v<PublicPolicy, const State&>);

enum class Policy : std::uint8_t { kConstructive, kFairD4 };

bool allowedFittingSeed(std::uint32_t seed) {
  return seed >= kFittingSeedStart && seed < kFittingSeedEndExclusive;
}

bool allowedScreenSeed(std::uint32_t seed) {
  return seed >= kScreenSeedStart && seed < kScreenSeedEndExclusive;
}

void requireSeed(std::uint32_t seed, bool screen) {
  if (screen ? !allowedScreenSeed(seed) : !allowedFittingSeed(seed)) {
    throw std::invalid_argument(
        screen ? "seed outside exact 0x3d6a1000 screen bank"
               : "seed outside exact 0x3d6a0000 fitting bank");
  }
}

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  int clears = 0;
  int reveals = 0;
  int waves = 0;
  int maximum_chain = 0;
  bool natural_terminal = false;
  bool capped = false;
  std::uint64_t d4_work = 0;
  std::uint64_t rollout_work = 0;
  std::size_t maximum_cache_entries = 0;
  int singleton_roots = 0;
  int near_tie_roots = 0;
  int switches_from_d4 = 0;
  std::uint64_t disc_hash = 0xcbf2'9ce4'8422'2325ull;
};

void observeDisc(GameResult& result, std::uint8_t disc) {
  result.disc_hash ^= disc;
  result.disc_hash *= 0x0000'0100'0000'01b3ull;
}

void observeMove(GameResult& result, const MoveResult& move) {
  result.waves += static_cast<int>(move.waves.size());
  for (const Wave& wave : move.waves) {
    result.clears += wave.cleared;
    result.reveals += wave.revealed;
    result.maximum_chain = std::max(result.maximum_chain, wave.depth);
  }
}

GameResult playGame(std::uint32_t seed, Policy policy,
                    const Deadline& deadline, bool screen) {
  requireSeed(seed, screen);
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    deadline.check();
    enforceRssLimit();
    if (state.next_disc != headlessDisc(seed, state.moves_played)) {
      throw std::runtime_error("headless disc stream guard failed");
    }
    observeDisc(result, state.next_disc);
    int action = -1;
    if (policy == Policy::kConstructive) {
      const Decision decision = chooseAction(publicState(state));
      if (!decision.complete) {
        throw std::runtime_error("constructive D4 decision incomplete");
      }
      action = decision.action;
      result.d4_work += decision.d4_work;
      result.rollout_work += decision.rollout_work;
      result.maximum_cache_entries =
          std::max(result.maximum_cache_entries, decision.d4_cache_entries);
      result.singleton_roots += decision.shortlist == 1;
      result.near_tie_roots += decision.shortlist == 2;
      result.switches_from_d4 += decision.action != decision.d4_action;
      if (decision.shortlist == 1 && decision.action != decision.d4_action) {
        throw std::runtime_error("singleton violated exact D4 parity");
      }
    } else {
      const d4::SearchDecision decision = d4::chooseDepth4Action(state);
      if (!decision.complete ||
          decision.completed_depth != d4::kCandidateDepth) {
        throw std::runtime_error("baseline D4 decision incomplete");
      }
      action = decision.action;
      result.d4_work += decision.work;
      result.maximum_cache_entries =
          std::max(result.maximum_cache_entries, decision.cache_entries);
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("gameplay policy selected illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("headless transition failed");
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
  double switch_rate = 0.0;
  double near_tie_rate = 0.0;
  int natural_terminals = 0;
  int capped = 0;
  int maximum_chain = 0;
  std::uint64_t d4_work = 0;
  std::uint64_t rollout_work = 0;
  std::size_t maximum_cache_entries = 0;
};

Summary summarize(const std::vector<GameResult>& games) {
  if (games.empty()) throw std::invalid_argument("cannot summarize no games");
  Summary result;
  std::vector<int> ordered_moves;
  std::int64_t scores = 0;
  std::int64_t moves = 0;
  std::int64_t clears = 0;
  std::int64_t reveals = 0;
  std::int64_t waves = 0;
  std::int64_t roots = 0;
  std::int64_t near_ties = 0;
  std::int64_t switches = 0;
  for (const GameResult& game : games) {
    scores += game.score;
    moves += game.moves;
    clears += game.clears;
    reveals += game.reveals;
    waves += game.waves;
    roots += game.moves;
    near_ties += game.near_tie_roots;
    switches += game.switches_from_d4;
    ordered_moves.push_back(game.moves);
    result.natural_terminals += game.natural_terminal;
    result.capped += game.capped;
    result.maximum_chain = std::max(result.maximum_chain, game.maximum_chain);
    result.d4_work += game.d4_work;
    result.rollout_work += game.rollout_work;
    result.maximum_cache_entries =
        std::max(result.maximum_cache_entries, game.maximum_cache_entries);
  }
  std::sort(ordered_moves.begin(), ordered_moves.end());
  const std::size_t quartile =
      std::max<std::size_t>(1, ordered_moves.size() / 4);
  result.bottom_quartile_moves = std::accumulate(
      ordered_moves.begin(), ordered_moves.begin() + quartile, 0.0) /
      quartile;
  result.mean_score = static_cast<double>(scores) / games.size();
  result.mean_moves = static_cast<double>(moves) / games.size();
  result.clears_per_move = static_cast<double>(clears) / moves;
  result.reveals_per_move = static_cast<double>(reveals) / moves;
  result.waves_per_move = static_cast<double>(waves) / moves;
  result.switch_rate = static_cast<double>(switches) / roots;
  result.near_tie_rate = static_cast<double>(near_ties) / roots;
  return result;
}

struct Paired {
  int score_wins = 0;
  int move_wins = 0;
  int joint_wins = 0;
  double mean_score_delta = 0.0;
  double mean_move_delta = 0.0;
};

Paired pair(const std::vector<GameResult>& candidate,
            const std::vector<GameResult>& baseline) {
  if (candidate.size() != baseline.size()) {
    throw std::invalid_argument("paired cohorts differ in size");
  }
  Paired result;
  for (std::size_t index = 0; index < candidate.size(); ++index) {
    if (candidate[index].seed != baseline[index].seed) {
      throw std::runtime_error("paired seed mismatch");
    }
    const bool score_win = candidate[index].score > baseline[index].score;
    const bool move_win = candidate[index].moves > baseline[index].moves;
    result.score_wins += score_win;
    result.move_wins += move_win;
    result.joint_wins += score_win && move_win;
    result.mean_score_delta += candidate[index].score - baseline[index].score;
    result.mean_move_delta += candidate[index].moves - baseline[index].moves;
  }
  result.mean_score_delta /= candidate.size();
  result.mean_move_delta /= candidate.size();
  return result;
}

std::vector<GameResult> evaluate(std::uint32_t seed_start, int games,
                                 Policy policy, int threads,
                                 const Deadline& deadline, bool screen) {
  std::vector<GameResult> result(games);
  std::atomic<int> next{0};
  std::mutex progress;
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(threads, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int index = next.fetch_add(1);
        if (index >= games) return;
        const std::uint32_t seed = seed_start + index;
        result[index] = playGame(seed, policy, deadline, screen);
        const std::lock_guard<std::mutex> lock(progress);
        std::cerr << (policy == Policy::kConstructive ? "d4-constructive"
                                                       : "fair-d4")
                  << " seed 0x" << std::hex << seed << std::dec << ' '
                  << result[index].score << " (" << result[index].moves
                  << " moves, near ties " << result[index].near_tie_roots
                  << ", switches " << result[index].switches_from_d4
                  << ", work "
                  << result[index].d4_work + result[index].rollout_work
                  << ")\n";
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

struct Options {
  std::string output;
  std::string readme =
      "/tmp/drop7-constructive-spectrum-depth4-README.md";
  std::string qualification;
  std::string source_sha256;
  int threads = kDefaultThreads;
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string argument = argv[index];
    if (argument == "--output") {
      result.output = argv[index + 1];
    } else if (argument == "--readme") {
      result.readme = argv[index + 1];
    } else if (argument == "--qualification") {
      result.qualification = argv[index + 1];
    } else if (argument == "--source-sha256") {
      result.source_sha256 = argv[index + 1];
    } else if (argument == "--threads") {
      result.threads = std::stoi(argv[index + 1]);
      if (result.threads < 1 || result.threads > 4) {
        throw std::invalid_argument("threads must be in [1,4]");
      }
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  if (result.source_sha256.size() != 64) {
    throw std::invalid_argument("exact 64-character source SHA-256 required");
  }
  return result;
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"bottomQuartileMoves\":" << summary.bottom_quartile_moves
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"wavesPerMove\":" << summary.waves_per_move
         << ",\"switchRate\":" << summary.switch_rate
         << ",\"nearTieRate\":" << summary.near_tie_rate
         << ",\"naturalTerminals\":" << summary.natural_terminals
         << ",\"capped\":" << summary.capped
         << ",\"maximumChain\":" << summary.maximum_chain
         << ",\"d4Work\":" << summary.d4_work
         << ",\"rolloutWork\":" << summary.rollout_work
         << ",\"maximumCacheEntries\":"
         << summary.maximum_cache_entries << '}';
}

void writeGame(std::ostream& output, const GameResult& game) {
  output << "{\"seed\":\"0x" << std::hex << std::setw(8)
         << std::setfill('0') << game.seed << std::dec << std::setfill(' ')
         << "\",\"score\":" << game.score << ",\"moves\":" << game.moves
         << ",\"clears\":" << game.clears
         << ",\"reveals\":" << game.reveals << ",\"waves\":" << game.waves
         << ",\"maximumChain\":" << game.maximum_chain
         << ",\"naturalTerminal\":"
         << (game.natural_terminal ? "true" : "false")
         << ",\"capped\":" << (game.capped ? "true" : "false")
         << ",\"d4Work\":" << game.d4_work
         << ",\"rolloutWork\":" << game.rollout_work
         << ",\"maximumCacheEntries\":" << game.maximum_cache_entries
         << ",\"singletonRoots\":" << game.singleton_roots
         << ",\"nearTieRoots\":" << game.near_tie_roots
         << ",\"switchesFromD4\":" << game.switches_from_d4
         << ",\"discHash\":\"0x" << std::hex << game.disc_hash << std::dec
         << "\"}";
}

bool qualificationAllowsScreen(const Options& options) {
  if (options.qualification.empty()) return false;
  std::ifstream input(options.qualification);
  if (!input) return false;
  const std::string contents((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
  return contents.find("\"phase\":\"fitting\"") != std::string::npos &&
         contents.find("\"passed\":true") != std::string::npos &&
         contents.find("\"sourceSha256\":\"" +
                           options.source_sha256 + "\"") !=
             std::string::npos;
}

void writeReadme(const Options& options, bool screen,
                 const Summary& candidate, const Summary& baseline,
                 const Paired& paired, bool passed, double wall_seconds,
                 double projected_seconds) {
  std::ofstream output(options.readme);
  if (!output) throw std::runtime_error("cannot write D4 lab README");
  output << "# Drop7 constructive-spectrum D4 integration\n\n"
         << "This is a frozen, public-only experiment. Qualified full-width "
            "fair D4/s5 admits at most its two highest-valued root actions; "
            "the second must be within 2,500 utility. The unchanged "
            "seven-scenario constructive rollout breaks only that near tie. "
            "Singletons return exact D4.\n\n"
         << "- Phase: " << (screen ? "screen" : "fitting") << "\n"
         << "- Seeds: `0x" << std::hex
         << (screen ? kScreenSeedStart : kFittingSeedStart) << "..0x"
         << ((screen ? kScreenSeedEndExclusive : kFittingSeedEndExclusive) -
             1u)
         << std::dec << "`\n"
         << "- Maximum moves: " << kMaximumMoves << "\n"
         << "- Source SHA-256: `" << options.source_sha256 << "`\n"
         << "- Candidate mean score/moves: " << candidate.mean_score << " / "
         << candidate.mean_moves << "\n"
         << "- Fair D4 mean score/moves: " << baseline.mean_score << " / "
         << baseline.mean_moves << "\n"
         << "- Candidate clear/reveal flow: " << candidate.clears_per_move
         << " / " << candidate.reveals_per_move << "\n"
         << "- Fair D4 clear/reveal flow: " << baseline.clears_per_move << " / "
         << baseline.reveals_per_move << "\n"
         << "- Paired joint wins: " << paired.joint_wins << '\n'
         << "- Near-tie/switch rate: " << candidate.near_tie_rate << " / "
         << candidate.switch_rate << "\n"
         << "- Wall seconds: " << wall_seconds << "\n";
  if (!screen) {
    output << "- Projected eight-game screen seconds: " << projected_seconds
           << "\n";
  }
  output << "- Passed: " << (passed ? "yes" : "no") << "\n\n"
         << "No `0x4d`, `0x7d`, or `0xd7` seeds are permitted by the "
            "executable's guards.\n";
}

int runPhase(const Options& options, bool screen, std::ostream& output) {
  if (screen && !qualificationAllowsScreen(options)) {
    throw std::invalid_argument(
        "screen requires matching passed fitting qualification artifact");
  }
  const Deadline deadline;
  const std::uint32_t seed_start =
      screen ? kScreenSeedStart : kFittingSeedStart;
  const int games = screen ? kScreenGames : kFittingGames;
  const auto candidate = evaluate(seed_start, games, Policy::kConstructive,
                                  options.threads, deadline, screen);
  const auto baseline = evaluate(seed_start, games, Policy::kFairD4,
                                 options.threads, deadline, screen);
  const Summary candidate_summary = summarize(candidate);
  const Summary baseline_summary = summarize(baseline);
  const Paired paired = pair(candidate, baseline);
  const double projected_seconds =
      screen ? deadline.seconds()
             : deadline.seconds() * kScreenGames / kFittingGames;
  const double score_ratio = screen ? kScreenScoreRatio : kFitScoreRatio;
  const double move_ratio = screen ? kScreenMoveRatio : kFitMoveRatio;
  const int required_joint = screen ? kScreenJointWins : kFitJointWins;
  const bool result_gate =
      candidate_summary.mean_score >=
          score_ratio * baseline_summary.mean_score &&
      candidate_summary.mean_moves >=
          move_ratio * baseline_summary.mean_moves &&
      candidate_summary.clears_per_move + 1.0e-12 >=
          baseline_summary.clears_per_move &&
      candidate_summary.reveals_per_move + 1.0e-12 >=
          baseline_summary.reveals_per_move &&
      paired.joint_wins >= required_joint;
  const bool resource_gate = deadline.seconds() <= kWallLimitSeconds &&
                             peakRssBytes() <= kRssLimitBytes &&
                             (screen || projected_seconds <= kWallLimitSeconds);
  const bool passed = result_gate && resource_gate;
  const std::string output_path =
      options.output.empty()
          ? (screen
                 ? "/tmp/drop7-constructive-spectrum-depth4-screen.json"
                 : "/tmp/drop7-constructive-spectrum-depth4-fit.json")
          : options.output;
  std::ofstream artifact(output_path);
  if (!artifact) throw std::runtime_error("cannot write D4 cohort artifact");
  artifact << std::fixed << std::setprecision(9)
           << "{\n  \"format\":\"drop7-constructive-spectrum-depth4-v1\","
           << "\n  \"phase\":\"" << (screen ? "screen" : "fitting")
           << "\",\n  \"sourceSha256\":\"" << options.source_sha256
           << "\",\n  \"publicOnly\":true,\n  \"causal\":true,"
           << "\n  \"rootShield\":{\"depth\":4,\"chanceSamples\":5,"
              "\"fullWidth\":true,\"maximumActions\":2,"
              "\"nearTieMargin\":2500},"
           << "\n  \"rollout\":{\"chanceSamples\":7,\"horizon\":[3,7],"
              "\"frozen\":true},"
           << "\n  \"seedBank\":{\"start\":\"0x" << std::hex
           << seed_start << "\",\"endExclusive\":\"0x"
           << seed_start + games << std::dec << "\",\"games\":" << games
           << ",\"maximumMoves\":" << kMaximumMoves << "},"
           << "\n  \"candidate\":";
  writeSummary(artifact, candidate_summary);
  artifact << ",\n  \"fairD4\":";
  writeSummary(artifact, baseline_summary);
  artifact << ",\n  \"paired\":{\"scoreWins\":" << paired.score_wins
           << ",\"moveWins\":" << paired.move_wins
           << ",\"jointWins\":" << paired.joint_wins
           << ",\"meanScoreDelta\":" << paired.mean_score_delta
           << ",\"meanMoveDelta\":" << paired.mean_move_delta << "},"
           << "\n  \"gate\":{\"scoreRatio\":" << score_ratio
           << ",\"moveRatio\":" << move_ratio
           << ",\"clearNonregression\":true,"
              "\"revealNonregression\":true,\"jointWins\":"
           << required_joint << "},"
           << "\n  \"resultGate\":" << (result_gate ? "true" : "false")
           << ",\n  \"resourceGate\":"
           << (resource_gate ? "true" : "false")
           << ",\n  \"projectedScreenSeconds\":" << projected_seconds
           << ",\n  \"passed\":" << (passed ? "true" : "false")
           << ",\n  \"wallSeconds\":" << deadline.seconds()
           << ",\n  \"peakRssBytes\":" << peakRssBytes()
           << ",\n  \"candidateGames\":[";
  for (std::size_t index = 0; index < candidate.size(); ++index) {
    if (index) artifact << ',';
    writeGame(artifact, candidate[index]);
  }
  artifact << "],\n  \"fairD4Games\":[";
  for (std::size_t index = 0; index < baseline.size(); ++index) {
    if (index) artifact << ',';
    writeGame(artifact, baseline[index]);
  }
  artifact << "]\n}\n";
  if (!artifact) throw std::runtime_error("failed writing D4 artifact");
  writeReadme(options, screen, candidate_summary, baseline_summary, paired,
              passed, deadline.seconds(), projected_seconds);
  output << std::fixed << std::setprecision(3)
         << "CONSTRUCTIVE_SPECTRUM_DEPTH4_"
         << (screen ? "SCREEN" : "FIT") << " {\"candidateScore\":"
         << candidate_summary.mean_score << ",\"candidateMoves\":"
         << candidate_summary.mean_moves << ",\"candidateClears\":"
         << candidate_summary.clears_per_move
         << ",\"candidateReveals\":"
         << candidate_summary.reveals_per_move << ",\"fairD4Score\":"
         << baseline_summary.mean_score << ",\"fairD4Moves\":"
         << baseline_summary.mean_moves << ",\"fairD4Clears\":"
         << baseline_summary.clears_per_move << ",\"fairD4Reveals\":"
         << baseline_summary.reveals_per_move << ",\"jointWins\":"
         << paired.joint_wins << ",\"nearTieRate\":"
         << candidate_summary.near_tie_rate << ",\"switchRate\":"
         << candidate_summary.switch_rate << ",\"projectedScreenSeconds\":"
         << projected_seconds << ",\"passed\":"
         << (passed ? "true" : "false") << ",\"wallSeconds\":"
         << deadline.seconds() << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"artifact\":\"" << output_path << "\"}\n";
  return passed ? EXIT_SUCCESS : 2;
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
  fixture.board[indexOf(5, 0)] = 6;
  fixture.board[indexOf(6, 1)] = kCracked;
  fixture.board[indexOf(6, 2)] = 5;
  fixture.board[indexOf(5, 2)] = 4;
  fixture.board[indexOf(6, 3)] = kSolid;
  fixture.board[indexOf(6, 4)] = 7;
  fixture.next_disc = 3;
  fixture.moves_remaining = 4;
  return fixture;
}

PublicState singletonFixture() {
  PublicState fixture;
  fixture.board.fill(kSolid);
  fixture.board[indexOf(0, 0)] = kEmpty;
  fixture.next_disc = 4;
  fixture.moves_remaining = 2;
  return fixture;
}

bool selfTest(std::ostream& output) {
  expect(kLevelBonus == 17'000 && d4::kCandidateDepth == 4 &&
             d4::kChanceSamples == 5 && kChanceSamples == 7 &&
             kTacticalNearTie == 2'500.0,
         "frozen protocol constants changed");
  const PublicState fixture = asymmetricFixture();
  const Metrics metrics = extractMetrics(fixture);
  const Metrics reflected_metrics = extractMetrics(mirror(fixture));
  expect(metrics == reflected_metrics && structuralValue(fixture) ==
                                             structuralValue(mirror(fixture)),
         "structural metrics/value reflection failed");
  const Decision first = chooseAction(fixture);
  const Decision repeat = chooseAction(fixture);
  const Decision reflected = chooseAction(mirror(fixture));
  expect(first == repeat && first.complete &&
             isLegal(fixture.board, first.action) &&
             (first.shortlist == 1 || first.shortlist == 2),
         "D4 constructive determinism/legality failed");
  expect(reflected.action == kBoardSize - 1 - first.action &&
             reflected.d4_action == kBoardSize - 1 - first.d4_action &&
             reflected.shortlist == first.shortlist &&
             reflected.d4_work == first.d4_work &&
             reflected.rollout_work == first.rollout_work,
         "D4 constructive reflection failed");
  for (int column = 0; column < kBoardSize; ++column) {
    expect(first.d4_values[column] ==
               reflected.d4_values[kBoardSize - 1 - column] &&
               first.rollout_values[column] ==
                   reflected.rollout_values[kBoardSize - 1 - column],
           "D4/rollout root values failed reflection");
  }
  State metadata = materialize(fixture);
  metadata.score = 9'999'999;
  metadata.level = 777;
  metadata.moves_played = 888;
  expect(publicState(metadata) == fixture &&
             chooseAction(publicState(metadata)) == first,
         "D4 constructive policy used hidden metadata");
  PublicState terminal = fixture;
  terminal.terminal = true;
  expect(chooseAction(terminal).action == -1,
         "terminal policy selected an action");

  const PublicState singleton = singletonFixture();
  const Decision singleton_candidate = chooseAction(singleton);
  const d4::SearchDecision singleton_d4 =
      d4::chooseDepth4Action(materialize(singleton));
  expect(singleton_candidate.complete && singleton_d4.complete &&
             singleton_candidate.shortlist == 1 &&
             singleton_candidate.singleton_parity &&
             singleton_candidate.action == singleton_d4.action &&
             singleton_candidate.d4_action == singleton_d4.action &&
             singleton_candidate.rollout_work == 0 &&
             singleton_candidate.d4_work == singleton_d4.work &&
             singleton_candidate.d4_values == singleton_d4.root_values,
         "exact D4 singleton parity failed");
  expect(first.d4_work <= d4::kMaximumWork &&
             first.d4_cache_entries <= d4::kMaximumCacheEntries,
         "D4 resource-bound fixture failed");
  expect(allowedFittingSeed(kFittingSeedStart) &&
             allowedFittingSeed(kFittingSeedEndExclusive - 1u) &&
             !allowedFittingSeed(kFittingSeedStart - 1u) &&
             !allowedFittingSeed(kFittingSeedEndExclusive) &&
             allowedScreenSeed(kScreenSeedStart) &&
             allowedScreenSeed(kScreenSeedEndExclusive - 1u) &&
             !allowedScreenSeed(kScreenSeedStart - 1u) &&
             !allowedScreenSeed(kScreenSeedEndExclusive) &&
             throwsInvalid([] { requireSeed(0x4d6a'0000u, false); }) &&
             throwsInvalid([] { requireSeed(0x7d6a'0000u, false); }) &&
             throwsInvalid([] { requireSeed(0xd76a'0000u, false); }) &&
             throwsInvalid([] { requireSeed(0x4d6a'1000u, true); }) &&
             throwsInvalid([] { requireSeed(0x7d6a'1000u, true); }) &&
             throwsInvalid([] { requireSeed(0xd76a'1000u, true); }),
         "seed guards failed");
  enforceRssLimit();
  output << "CONSTRUCTIVE_SPECTRUM_DEPTH4_SELF_TEST {\"passed\":true,"
         << "\"publicOnly\":true,\"metadataBlind\":true,"
         << "\"deterministic\":true,\"reflection\":true,"
         << "\"legal\":true,\"d4Depth\":4,\"d4ChanceSamples\":5,"
         << "\"rolloutChanceSamples\":7,\"maximumHorizon\":7,"
         << "\"singletonParity\":true,\"seedGuards\":true,"
         << "\"fixtureShortlist\":" << first.shortlist
         << ",\"fixtureD4Work\":" << first.d4_work
         << ",\"fixtureRolloutWork\":" << first.rollout_work
         << ",\"peakRssBytes\":" << peakRssBytes() << "}\n";
  return true;
}

}  // namespace drop7::constructive_spectrum_depth4

int main(int argc, char** argv) {
  try {
    using namespace drop7::constructive_spectrum_depth4;
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return selfTest(std::cout) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--fit") {
      return runPhase(parseOptions(argc, argv, 2), false, std::cout);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--screen") {
      return runPhase(parseOptions(argc, argv, 2), true, std::cout);
    }
    std::cerr << "usage: drop7_constructive_spectrum_depth4 --self-test | "
                 "--fit --source-sha256 HASH [--output PATH] [--readme PATH] "
                 "[--threads N] | --screen --source-sha256 HASH "
                 "--qualification FIT_JSON [--output PATH] [--readme PATH] "
                 "[--threads N]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_constructive_spectrum_depth4: " << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
}
