#include "../../../src/core/native/engine.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#endif

namespace drop7::evolution {

using Clock = std::chrono::steady_clock;

// These ranges are deliberately disjoint from calibration and held-out seeds.
constexpr std::uint32_t kTrainingSeedStart = 0x3d70'0000u;
constexpr std::uint32_t kProbeSeedStart = 0x4d70'0000u;
constexpr std::uint32_t kTrainingSeedEnd = kProbeSeedStart;
constexpr std::uint32_t kProbeSeedEnd = 0x5d70'0000u;

enum Feature : std::size_t {
  kBias,
  kImmediateScore,
  kImmediateClears,
  kImmediateReveals,
  kImmediateCrackProgress,
  kChainDepth,
  kBoardClear,
  kDeath,
  kLevelAdvance,
  kOccupancy,
  kProjectedOccupancyDebt,
  kMaximumHeight,
  kSquaredHeightLoad,
  kTopLoad,
  kRoughness,
  kCoverLoad,
  kCrackedLoad,
  kCoverAltitude,
  kCrackedAltitude,
  kDirectReadiness,
  kReleaseReadiness,
  kHighNumberReadiness,
  kCoverExposure,
  kAdjacentOnes,
  kTripleTwos,
  kLowCapLoad,
  kAdjacentLowCaps,
  kAccessibleCracked,
  kNextDropTriggers,
  kQuietBuildOptions,
  kRiseTriggers,
  kRiseHighTriggers,
  kHighNumberTrenches,
  kCoveredCliffAccess,
  kStoredHighNumbers,
  kPhaseHeightRisk,
  kOccupancyReduction,
  kCoverReduction,
  kWorstTopLoad,
  kOutcomeDispersion,
  kFeatureCount,
};

using Features = std::array<double, kFeatureCount>;
using Weights = std::array<double, kFeatureCount>;

constexpr std::array<std::string_view, kFeatureCount> kFeatureNames{{
    "bias",
    "immediateScore",
    "immediateClears",
    "immediateReveals",
    "immediateCrackProgress",
    "chainDepth",
    "boardClear",
    "death",
    "levelAdvance",
    "occupancy",
    "projectedOccupancyDebt",
    "maximumHeight",
    "squaredHeightLoad",
    "topLoad",
    "roughness",
    "coverLoad",
    "crackedLoad",
    "coverAltitude",
    "crackedAltitude",
    "directReadiness",
    "releaseReadiness",
    "highNumberReadiness",
    "coverExposure",
    "adjacentOnes",
    "tripleTwos",
    "lowCapLoad",
    "adjacentLowCaps",
    "accessibleCracked",
    "nextDropTriggers",
    "quietBuildOptions",
    "riseTriggers",
    "riseHighTriggers",
    "highNumberTrenches",
    "coveredCliffAccess",
    "storedHighNumbers",
    "phaseHeightRisk",
    "occupancyReduction",
    "coverReduction",
    "worstTopLoad",
    "outcomeDispersion",
}};

constexpr std::array<std::array<int, 2>, 4> kDirections{{
    {{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}},
}};

struct Bounds {
  double minimum;
  double maximum;
};

constexpr std::array<Bounds, kFeatureCount> kBounds{{
    {0, 0},       {0, 60},       {0, 60},       {0, 80},
    {0, 60},      {0, 50},       {0, 80},       {-240, -10},
    {0, 30},      {-60, 40},     {-140, 10},    {-180, 15},
    {-140, 20},   {-220, -2},    {-50, 40},     {-80, 30},
    {-40, 40},    {-160, -1},    {-120, 15},    {-30, 100},
    {-30, 100},   {-30, 120},    {-20, 100},    {-160, 5},
    {-160, 5},    {-160, 10},    {-180, 5},     {-20, 100},
    {-30, 120},   {-30, 120},    {-30, 160},    {-30, 120},
    {-30, 120},   {-30, 120},    {-30, 120},    {-220, -1},
    {-40, 120},   {-30, 120},    {-180, -1},    {-80, 0},
}};

constexpr Weights kHandWeights{{
    0.0,    // bias
    3.0,    // immediate score
    7.0,    // immediate clears
    14.0,   // immediate reveals
    4.0,    // crack progress
    8.0,    // chain depth
    18.0,   // board clear
    -180.0, // death
    5.0,    // level advance
    -3.0,   // occupancy: building is allowed, altitude is the real debt
    -32.0,  // projected occupancy debt at the next rise
    -62.0,  // maximum height
    -28.0,  // squared height load
    -90.0,  // top two rows
    -5.0,   // roughness
    -4.0,   // covers
    3.0,    // cracked covers
    -32.0,  // covered altitude
    -12.0,  // cracked altitude
    18.0,   // one/two-placement readiness
    12.0,   // latent release readiness
    23.0,   // stored 5/6/7 readiness
    13.0,   // ready discs adjacent to covers
    -55.0,  // adjacent ones
    -45.0,  // locked three-two runs
    -46.0,  // high 1/2 caps
    -60.0,  // adjacent low caps
    18.0,   // cracked covers reachable from a cliff
    20.0,   // next-disc trigger inventory
    12.0,   // quiet build choices
    30.0,   // discs triggered by a public row rise
    34.0,   // high-number rise triggers
    24.0,   // repeated high-number vertical trenches
    18.0,   // covered cliff faces reachable from below
    17.0,   // high numbers stored without firing
    -86.0,  // height risk as a rise approaches
    22.0,   // discs removed by the action
    26.0,   // covers removed by the action
    -72.0,  // worst top-load chance sample
    -8.0,   // chance sensitivity
}};

struct CanonicalState {
  State state;
  bool reflected = false;
  bool symmetric = false;
};

struct BoardAnalysis {
  Features features{};
  int occupied = 0;
  int covers = 0;
  int solids = 0;
  int cracked = 0;
  double top_load = 0;
};

struct PolicyOptions {
  int chance_probes = 7;
};

struct GameStats {
  double mean_score = 0;
  double median_score = 0;
  double p10_score = 0;
  double p90_score = 0;
  double mean_moves = 0;
  double score_stddev = 0;
  std::int64_t minimum_score = 0;
  std::int64_t maximum_score = 0;
  int censored = 0;
  int games = 0;

  double objective() const {
    // Common-random-number CEM ranking should reward long survival without
    // allowing one extraordinary chain to dominate a whole generation.
    return 0.65 * mean_score + 0.35 * median_score;
  }
};

struct TrainingOptions {
  int generations = 12;
  int population = 36;
  int elite = 8;
  int training_games = 48;
  int probe_games = 64;
  int max_moves = 750;
  int chance_probes = 3;
  int final_chance_probes = 7;
  int threads = std::max(1u, std::thread::hardware_concurrency());
  std::uint32_t optimizer_seed = 0xe701'c0deu;
};

Board reflectedBoard(const Board& board) {
  Board reflected{};
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      reflected[indexOf(row, kBoardSize - 1 - column)] =
          board[indexOf(row, column)];
    }
  }
  return reflected;
}

CanonicalState canonicalize(const State& source) {
  CanonicalState result{source, false, false};
  const Board reflected = reflectedBoard(source.board);
  if (reflected < source.board) {
    result.state.board = reflected;
    result.reflected = true;
  } else if (reflected == source.board) {
    result.symmetric = true;
  }
  return result;
}

int canonicalColumn(const CanonicalState& canonical, int original_column) {
  if (canonical.reflected) return kBoardSize - 1 - original_column;
  if (canonical.symmetric) {
    return std::min(original_column, kBoardSize - 1 - original_column);
  }
  return original_column;
}

std::uint32_t observableHash(const State& state, int column) {
  std::uint32_t hash = 0x811c'9dc5u;
  for (std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint32_t>(cell + 1u);
    hash *= 0x0100'0193u;
    hash = mix32(hash);
  }
  hash ^= static_cast<std::uint32_t>(state.next_disc) * 0x9e37'79b9u;
  hash ^= static_cast<std::uint32_t>(state.moves_remaining) * 0x85eb'ca6bu;
  hash ^= static_cast<std::uint32_t>(std::min(state.level, 255)) *
          0xc2b2'ae35u;
  hash ^= static_cast<std::uint32_t>(column + 1) * 0x27d4'eb2fu;
  return mix32(hash);
}

std::uint32_t seedWithFirstDisc(std::uint32_t base, int target_disc) {
  for (std::uint32_t nonce = 0; nonce < 256; ++nonce) {
    const std::uint32_t candidate = mix32(base ^ (nonce * 0x9e37'79b9u));
    Mulberry32 random(candidate);
    if (random.nextDisc() == target_disc) return candidate;
  }
  throw std::runtime_error("failed to construct a stratified chance probe");
}

double readiness(int placements) {
  return placements >= 1 ? std::ldexp(1.0, 1 - placements) : 0.0;
}

std::array<int, kBoardSize> columnHeights(const Board& board) {
  std::array<int, kBoardSize> heights{};
  for (int column = 0; column < kBoardSize; ++column) {
    for (int row = 0; row < kBoardSize; ++row) {
      if (board[indexOf(row, column)] != kEmpty) ++heights[column];
    }
  }
  return heights;
}

int countCell(const Board& board, std::uint8_t target) {
  return static_cast<int>(
      std::count(board.begin(), board.end(), target));
}

int countCovers(const Board& board) {
  int count = 0;
  for (std::uint8_t cell : board) {
    if (cell == kSolid || cell == kCracked) ++count;
  }
  return count;
}

bool isCover(std::uint8_t cell) {
  return cell == kSolid || cell == kCracked;
}

double directReadinessForDisc(int value, int horizontal, int vertical) {
  double result = 0;
  if (horizontal < value) result = readiness(value - horizontal);
  if (vertical < value) {
    const double vertical_readiness = readiness(value - vertical);
    result = 1.0 - (1.0 - result) * (1.0 - vertical_readiness);
  }
  return result;
}

BoardAnalysis analyzeBoard(const State& state) {
  BoardAnalysis analysis;
  Features& features = analysis.features;
  const Board& board = state.board;
  const auto heights = columnHeights(board);
  int maximum_height = 0;
  double squared_height = 0;
  double roughness = 0;
  double cover_altitude = 0;
  double cracked_altitude = 0;
  double direct_readiness = 0;
  double release_readiness = 0;
  double high_readiness = 0;
  double cover_exposure = 0;
  double adjacent_ones = 0;
  double triple_twos = 0;
  double low_cap = 0;
  double adjacent_low_cap = 0;
  double accessible_cracked = 0;
  double trench_cohesion = 0;
  double covered_cliff_access = 0;
  double stored_high = 0;

  for (int column = 0; column < kBoardSize; ++column) {
    maximum_height = std::max(maximum_height, heights[column]);
    squared_height += heights[column] * heights[column];
    if (column > 0) roughness += std::abs(heights[column] - heights[column - 1]);
    if (heights[column] > 0) {
      const int cap_row = kBoardSize - heights[column];
      const std::uint8_t cap = board[indexOf(cap_row, column)];
      if (cap == 1 || cap == 2) {
        const double factor = cap == 1 ? 1.5 : 1.0;
        low_cap += factor * heights[column] * heights[column];
        if (column > 0 && heights[column - 1] > 0) {
          const std::uint8_t previous_cap =
              board[indexOf(kBoardSize - heights[column - 1], column - 1)];
          if (previous_cap == 1 || previous_cap == 2) {
            const int shared = std::min(heights[column], heights[column - 1]);
            adjacent_low_cap += shared * shared;
          }
        }
      }
    }

    std::array<int, 3> high_counts{};
    for (int row = kBoardSize - heights[column]; row < kBoardSize; ++row) {
      if (row < 0) continue;
      const std::uint8_t cell = board[indexOf(row, column)];
      if (cell >= 5 && cell <= 7 && cell > heights[column]) {
        ++high_counts[cell - 5];
      }
    }
    for (int offset = 0; offset < 3; ++offset) {
      const int count = high_counts[offset];
      if (count < 2) continue;
      const int value = offset + 5;
      trench_cohesion +=
          (count * (count - 1) / 2.0) * readiness(value - heights[column]);
    }
  }

  for (int row = 0; row < kBoardSize; ++row) {
    const int elevation = kBoardSize - row;
    for (int column = 0; column < kBoardSize; ++column) {
      const int index = indexOf(row, column);
      const std::uint8_t cell = board[index];
      if (cell == kEmpty) continue;
      ++analysis.occupied;
      if (row <= 1) analysis.top_load += row == 0 ? 1.5 : 0.75;
      if (cell == kSolid) {
        ++analysis.solids;
        ++analysis.covers;
        cover_altitude += elevation * elevation;
      } else if (cell == kCracked) {
        ++analysis.cracked;
        ++analysis.covers;
        cracked_altitude += elevation * elevation;
      }

      if (isNumbered(cell)) {
        const int horizontal = lineLength(board, row, column, false);
        const int vertical = lineLength(board, row, column, true);
        const double ready =
            directReadinessForDisc(cell, horizontal, vertical);
        direct_readiness += ready;
        if (cell >= 5) {
          high_readiness += ready;
          if (ready > 0 && horizontal != cell && vertical != cell) {
            stored_high += ready * (cell - 3) / 4.0;
          }
        }
        if (horizontal > cell) release_readiness += readiness(horizontal - cell);
        if (vertical > cell) release_readiness += readiness(vertical - cell);

        for (const auto& direction : kDirections) {
          const int next_row = row + direction[0];
          const int next_column = column + direction[1];
          if (!inside(next_row, next_column)) continue;
          const std::uint8_t neighbor = board[indexOf(next_row, next_column)];
          if (isCover(neighbor)) cover_exposure += ready;
          if (cell == 1 && neighbor == 1 &&
              (direction[0] > 0 || direction[1] > 0)) {
            adjacent_ones += 1;
          }
        }
      }

      if (!isCover(cell)) continue;
      const double cover_factor = cell == kSolid ? 1.0 : 0.68;
      for (int neighbor_column : {column - 1, column + 1}) {
        if (neighbor_column < 0 || neighbor_column >= kBoardSize) continue;
        if (heights[neighbor_column] >= elevation) continue;
        const int required = elevation - heights[neighbor_column];
        const double access = readiness(required) * elevation * elevation;
        covered_cliff_access += cover_factor * access;
        if (cell == kCracked) accessible_cracked += access;
      }
    }
  }

  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column + 2 < kBoardSize; ++column) {
      if (board[indexOf(row, column)] == 2 &&
          board[indexOf(row, column + 1)] == 2 &&
          board[indexOf(row, column + 2)] == 2) {
        triple_twos += 1;
      }
    }
  }
  for (int column = 0; column < kBoardSize; ++column) {
    for (int row = 0; row + 2 < kBoardSize; ++row) {
      if (board[indexOf(row, column)] == 2 &&
          board[indexOf(row + 1, column)] == 2 &&
          board[indexOf(row + 2, column)] == 2) {
        triple_twos += 1;
      }
    }
  }

  int best_next_trigger = 0;
  int quiet_options = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    Board placed = board;
    if (!placeDisc(placed, column, state.next_disc)) continue;
    int popper_count = 0;
    findPoppers(placed, popper_count);
    best_next_trigger = std::max(best_next_trigger, popper_count);
    if (popper_count == 0) ++quiet_options;
  }

  int rise_trigger_count = 0;
  int rise_high_count = 0;
  Board raised{};
  if (raiseCoveredRow(board, raised)) {
    int popper_count = 0;
    const auto poppers = findPoppers(raised, popper_count);
    rise_trigger_count = popper_count;
    for (int offset = 0; offset < popper_count; ++offset) {
      if (raised[poppers[offset]] >= 5 && raised[poppers[offset]] <= 7) {
        ++rise_high_count;
      }
    }
  }

  const int moves_until_rise = std::clamp(state.moves_remaining, 1, kMovesPerLevel);
  const double rise_urgency =
      static_cast<double>(kMovesPerLevel - moves_until_rise) /
      (kMovesPerLevel - 1);
  const double projected_occupancy =
      analysis.occupied + kBoardSize - 1.4 * moves_until_rise;
  const double occupancy_debt =
      std::max(0.0, projected_occupancy - 14.0) / 14.0;
  const double height_risk =
      std::max(0.0, maximum_height + rise_urgency - 3.0) / 5.0;

  features[kBias] = 1;
  features[kOccupancy] = analysis.occupied / 28.0;
  features[kProjectedOccupancyDebt] = occupancy_debt * occupancy_debt;
  features[kMaximumHeight] = maximum_height / 7.0;
  features[kSquaredHeightLoad] = squared_height / 343.0;
  features[kTopLoad] = analysis.top_load / 14.0;
  features[kRoughness] = roughness / 42.0;
  features[kCoverLoad] = analysis.covers / 21.0;
  features[kCrackedLoad] = analysis.cracked / 14.0;
  features[kCoverAltitude] = cover_altitude / 343.0;
  features[kCrackedAltitude] = cracked_altitude / 343.0;
  features[kDirectReadiness] = direct_readiness / 14.0;
  features[kReleaseReadiness] = release_readiness / 14.0;
  features[kHighNumberReadiness] = high_readiness / 10.0;
  features[kCoverExposure] = cover_exposure / 20.0;
  features[kAdjacentOnes] = adjacent_ones / 6.0;
  features[kTripleTwos] = triple_twos / 4.0;
  features[kLowCapLoad] = low_cap / 343.0;
  features[kAdjacentLowCaps] = adjacent_low_cap / 196.0;
  features[kAccessibleCracked] = accessible_cracked / 343.0;
  features[kNextDropTriggers] = best_next_trigger / 7.0;
  features[kQuietBuildOptions] = quiet_options / 7.0;
  features[kRiseTriggers] = rise_trigger_count / 7.0;
  features[kRiseHighTriggers] = rise_high_count / 4.0;
  features[kHighNumberTrenches] = trench_cohesion / 4.0;
  features[kCoveredCliffAccess] = covered_cliff_access / 343.0;
  features[kStoredHighNumbers] = stored_high / 7.0;
  features[kPhaseHeightRisk] = height_risk * height_risk * (0.5 + rise_urgency);
  return analysis;
}

double dot(const Features& features, const Weights& weights) {
  double result = 0;
  for (std::size_t index = 0; index < kFeatureCount; ++index) {
    result += features[index] * weights[index];
  }
  return result;
}

double evaluateCanonicalAction(const State& canonical_state, int column,
                               const Weights& weights,
                               const PolicyOptions& options) {
  if (!isLegal(canonical_state.board, column)) {
    return -std::numeric_limits<double>::infinity();
  }
  if (options.chance_probes < 1 || options.chance_probes > 49) {
    throw std::invalid_argument("chance probes must be between 1 and 49");
  }

  const BoardAnalysis before = analyzeBoard(canonical_state);
  Features aggregate{};
  double top_sum = 0;
  double top_square_sum = 0;
  double worst_top = 0;
  const std::uint32_t base_hash = observableHash(canonical_state, column);

  for (int probe = 0; probe < options.chance_probes; ++probe) {
    const int stratum = std::min(
        6, static_cast<int>((probe + 0.5) * 7.0 / options.chance_probes));
    const int reveal_target = (stratum + 2 * column) % 7 + 1;
    const int next_target = (3 * stratum + column) % 7 + 1;
    const std::uint32_t chance_seed = seedWithFirstDisc(
        mix32(base_hash ^ (static_cast<std::uint32_t>(stratum + 1) *
                           0x9e37'79b9u)),
        reveal_target);
    Mulberry32 chance(chance_seed);
    MoveResult move;
    if (!playMove(canonical_state, column, chance, move)) {
      throw std::runtime_error("policy attempted an illegal candidate move");
    }
    if (!move.state.game_over) {
      // Headless mode has an independent public next-disc stream. A full set
      // of seven probes therefore uses every possible next disc exactly once.
      move.state.next_disc = static_cast<std::uint8_t>(next_target);
    }

    const BoardAnalysis after = analyzeBoard(move.state);
    Features sample = after.features;
    int clears = 0;
    int reveals = 0;
    int max_depth = 0;
    for (const Wave& wave : move.waves) {
      clears += wave.cleared;
      reveals += wave.revealed;
      max_depth = std::max(max_depth, wave.depth);
    }
    const int raised_covers = move.level_advanced ? kBoardSize : 0;
    const int solid_progress = std::max(
        0, before.solids + raised_covers - after.solids - reveals);
    sample[kImmediateScore] =
        std::min(8.0, move.score_delta / static_cast<double>(kLevelBonus));
    sample[kImmediateClears] = clears / 7.0;
    sample[kImmediateReveals] = reveals / 7.0;
    sample[kImmediateCrackProgress] = solid_progress / 7.0;
    sample[kChainDepth] = max_depth / 8.0;
    sample[kBoardClear] = move.cleared_board ? 1.0 : 0.0;
    sample[kDeath] = move.state.game_over ? 1.0 : 0.0;
    sample[kLevelAdvance] = move.level_advanced ? 1.0 : 0.0;
    sample[kOccupancyReduction] =
        (before.occupied + 1 + raised_covers - after.occupied) / 14.0;
    sample[kCoverReduction] =
        (before.covers + raised_covers - after.covers) / 14.0;

    for (std::size_t index = 0; index < kFeatureCount; ++index) {
      aggregate[index] += sample[index];
    }
    top_sum += after.top_load;
    top_square_sum += after.top_load * after.top_load;
    worst_top = std::max(worst_top, after.top_load);
  }

  const double inverse = 1.0 / options.chance_probes;
  for (double& value : aggregate) value *= inverse;
  const double top_mean = top_sum * inverse;
  const double top_variance =
      std::max(0.0, top_square_sum * inverse - top_mean * top_mean);
  aggregate[kWorstTopLoad] = worst_top / 14.0;
  aggregate[kOutcomeDispersion] = std::sqrt(top_variance) / 14.0;
  return dot(aggregate, weights);
}

double evaluateAction(const State& state, int original_column,
                      const Weights& weights, const PolicyOptions& options) {
  const CanonicalState canonical = canonicalize(state);
  const int column = canonicalColumn(canonical, original_column);
  return evaluateCanonicalAction(canonical.state, column, weights, options);
}

int selectAction(const State& state, const Weights& weights,
                 const PolicyOptions& options) {
  const CanonicalState canonical = canonicalize(state);
  constexpr std::array<int, kBoardSize> order{{3, 2, 4, 1, 5, 0, 6}};
  int best_column = -1;
  double best_value = -std::numeric_limits<double>::infinity();
  for (int column : order) {
    if (!isLegal(canonical.state.board, column)) continue;
    const double value =
        evaluateCanonicalAction(canonical.state, column, weights, options);
    if (value > best_value + 1e-12) {
      best_value = value;
      best_column = column;
    }
  }
  if (best_column < 0) return -1;
  return canonical.reflected ? kBoardSize - 1 - best_column : best_column;
}

GameStats summarize(std::vector<std::int64_t> scores,
                    const std::vector<int>& moves, int censored) {
  if (scores.empty() || scores.size() != moves.size()) {
    throw std::invalid_argument("invalid game summary inputs");
  }
  GameStats stats;
  stats.games = static_cast<int>(scores.size());
  stats.censored = censored;
  const double total_score = std::accumulate(
      scores.begin(), scores.end(), 0.0,
      [](double total, std::int64_t score) { return total + score; });
  stats.mean_score = total_score / scores.size();
  stats.mean_moves = std::accumulate(moves.begin(), moves.end(), 0.0) /
                     static_cast<double>(moves.size());
  stats.minimum_score = *std::min_element(scores.begin(), scores.end());
  stats.maximum_score = *std::max_element(scores.begin(), scores.end());
  double variance = 0;
  for (std::int64_t score : scores) {
    const double difference = score - stats.mean_score;
    variance += difference * difference;
  }
  stats.score_stddev = std::sqrt(variance / scores.size());
  std::sort(scores.begin(), scores.end());
  const auto quantile = [&](double proportion) {
    const double position = proportion * (scores.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(std::floor(position));
    const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
    const double fraction = position - lower;
    return scores[lower] * (1 - fraction) + scores[upper] * fraction;
  };
  stats.p10_score = quantile(0.10);
  stats.median_score = quantile(0.50);
  stats.p90_score = quantile(0.90);
  return stats;
}

GameStats evaluatePolicy(const Weights& weights, std::uint32_t seed_start,
                         int games, int max_moves, int chance_probes) {
  if (games < 1 || max_moves < 1) {
    throw std::invalid_argument("games and max moves must be positive");
  }
  const std::uint64_t end =
      static_cast<std::uint64_t>(seed_start) + games;
  const bool training_range =
      seed_start >= kTrainingSeedStart && end <= kTrainingSeedEnd;
  const bool probe_range = seed_start >= kProbeSeedStart && end <= kProbeSeedEnd;
  if (!training_range && !probe_range) {
    throw std::invalid_argument(
        "evolution experiment may only use 0x3d70 training or 0x4d70 probe seeds");
  }

  const PolicyOptions policy{chance_probes};
  std::vector<std::int64_t> scores;
  std::vector<int> moves;
  scores.reserve(games);
  moves.reserve(games);
  int censored = 0;
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed = seed_start + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < max_moves) {
      const int column = selectAction(state, weights, policy);
      if (column < 0) break;
      MoveResult move;
      if (!playHeadlessMove(state, seed, column, move)) {
        throw std::runtime_error("observable policy selected an illegal move");
      }
    }
    if (!state.game_over && state.moves_played == max_moves) ++censored;
    scores.push_back(state.score);
    moves.push_back(state.moves_played);
  }
  return summarize(std::move(scores), moves, censored);
}

void printStats(std::string_view label, const GameStats& stats, double seconds) {
  std::cout << std::fixed << std::setprecision(3) << label
            << " {\"games\":" << stats.games
            << ",\"meanScore\":" << stats.mean_score
            << ",\"medianScore\":" << stats.median_score
            << ",\"p10\":" << stats.p10_score
            << ",\"p90\":" << stats.p90_score
            << ",\"stddev\":" << stats.score_stddev
            << ",\"min\":" << stats.minimum_score
            << ",\"max\":" << stats.maximum_score
            << ",\"meanMoves\":" << stats.mean_moves
            << ",\"censored\":" << stats.censored
            << ",\"seconds\":" << seconds << "}\n";
}

class NormalRandom {
 public:
  explicit NormalRandom(std::uint32_t seed) : random_(seed) {}

  double next() {
    if (has_spare_) {
      has_spare_ = false;
      return spare_;
    }
    const double first = std::max(1e-12, random_.nextUnit());
    const double second = random_.nextUnit();
    const double radius = std::sqrt(-2.0 * std::log(first));
    const double angle = 2.0 * std::acos(-1.0) * second;
    spare_ = radius * std::sin(angle);
    has_spare_ = true;
    return radius * std::cos(angle);
  }

 private:
  Mulberry32 random_;
  bool has_spare_ = false;
  double spare_ = 0;
};

double clampWeight(std::size_t feature, double value) {
  return std::clamp(value, kBounds[feature].minimum,
                    kBounds[feature].maximum);
}

double maximumResidentMiB() {
#if defined(__APPLE__) || defined(__linux__)
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(__APPLE__)
    return usage.ru_maxrss / (1024.0 * 1024.0);
#else
    return usage.ru_maxrss / 1024.0;
#endif
  }
#endif
  return 0;
}

void printWeights(std::string_view label, const Weights& weights) {
  std::cout << label << " {";
  for (std::size_t index = 0; index < kFeatureCount; ++index) {
    if (index != 0) std::cout << ',';
    std::cout << '\"' << kFeatureNames[index] << "\":" << std::fixed
              << std::setprecision(6) << weights[index];
  }
  std::cout << "}\n";
}

Weights train(const TrainingOptions& options) {
  if (options.generations < 1 || options.population < 4 || options.elite < 2 ||
      options.elite >= options.population || options.training_games < 4 ||
      options.probe_games < 4 || options.threads < 1) {
    throw std::invalid_argument("invalid evolution training options");
  }
  const std::uint64_t last_training =
      static_cast<std::uint64_t>(kTrainingSeedStart) +
      static_cast<std::uint64_t>(options.generations) * options.training_games;
  if (last_training > kTrainingSeedEnd) {
    throw std::invalid_argument("training options leave the 0x3d70 seed range");
  }

  Weights mean = kHandWeights;
  Weights sigma{};
  for (std::size_t index = 0; index < kFeatureCount; ++index) {
    sigma[index] = kBounds[index].minimum == kBounds[index].maximum
                       ? 0
                       : std::max(2.0, std::abs(mean[index]) * 0.18);
  }
  Weights best = mean;
  GameStats best_probe = evaluatePolicy(
      best, kProbeSeedStart, options.probe_games, options.max_moves,
      options.final_chance_probes);
  printStats("INITIAL_PROBE", best_probe, 0);
  NormalRandom normal(options.optimizer_seed);

  const auto training_started = Clock::now();
  for (int generation = 0; generation < options.generations; ++generation) {
    std::vector<Weights> population(options.population);
    population[0] = mean;
    population[1] = best;
    for (int candidate = 2; candidate < options.population; ++candidate) {
      for (std::size_t feature = 0; feature < kFeatureCount; ++feature) {
        population[candidate][feature] = clampWeight(
            feature, mean[feature] + sigma[feature] * normal.next());
      }
    }

    std::vector<GameStats> results(options.population);
    std::atomic<int> next_candidate{0};
    std::exception_ptr worker_error;
    std::atomic<bool> failed{false};
    std::mutex error_mutex;
    const std::uint32_t generation_seed =
        kTrainingSeedStart + generation * options.training_games;
    auto worker = [&]() {
      try {
        while (!failed.load(std::memory_order_relaxed)) {
          const int candidate = next_candidate.fetch_add(1);
          if (candidate >= options.population) return;
          results[candidate] = evaluatePolicy(
              population[candidate], generation_seed, options.training_games,
              options.max_moves, options.chance_probes);
        }
      } catch (...) {
        failed.store(true, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(error_mutex);
        if (!worker_error) worker_error = std::current_exception();
      }
    };
    std::vector<std::thread> workers;
    const int thread_count = std::min(options.threads, options.population);
    workers.reserve(thread_count);
    for (int thread = 0; thread < thread_count; ++thread) {
      workers.emplace_back(worker);
    }
    for (std::thread& thread : workers) thread.join();
    if (worker_error) std::rethrow_exception(worker_error);

    std::vector<int> ranking(options.population);
    std::iota(ranking.begin(), ranking.end(), 0);
    std::sort(ranking.begin(), ranking.end(), [&](int first, int second) {
      return results[first].objective() > results[second].objective();
    });

    Weights elite_mean{};
    Weights elite_variance{};
    double rank_total = 0;
    for (int rank = 0; rank < options.elite; ++rank) {
      const double rank_weight = std::log(options.elite + 0.5) -
                                 std::log(rank + 1.0);
      rank_total += rank_weight;
      const Weights& candidate = population[ranking[rank]];
      for (std::size_t feature = 0; feature < kFeatureCount; ++feature) {
        elite_mean[feature] += rank_weight * candidate[feature];
      }
    }
    for (double& value : elite_mean) value /= rank_total;
    for (int rank = 0; rank < options.elite; ++rank) {
      const double rank_weight = std::log(options.elite + 0.5) -
                                 std::log(rank + 1.0);
      const Weights& candidate = population[ranking[rank]];
      for (std::size_t feature = 0; feature < kFeatureCount; ++feature) {
        const double difference = candidate[feature] - elite_mean[feature];
        elite_variance[feature] += rank_weight * difference * difference;
      }
    }
    for (std::size_t feature = 0; feature < kFeatureCount; ++feature) {
      const double elite_sigma = std::sqrt(elite_variance[feature] / rank_total);
      mean[feature] = clampWeight(
          feature, 0.35 * mean[feature] + 0.65 * elite_mean[feature]);
      if (sigma[feature] > 0) {
        sigma[feature] = std::clamp(
            0.45 * sigma[feature] + 0.55 * elite_sigma, 0.15, 30.0);
      }
    }

    const int winner = ranking.front();
    const auto probe_started = Clock::now();
    const GameStats winner_probe = evaluatePolicy(
        population[winner], kProbeSeedStart, options.probe_games,
        options.max_moves, options.final_chance_probes);
    const double probe_seconds =
        std::chrono::duration<double>(Clock::now() - probe_started).count();
    if (winner_probe.objective() > best_probe.objective()) {
      best = population[winner];
      best_probe = winner_probe;
    }

    std::cout << std::fixed << std::setprecision(3) << "GENERATION {\"index\":"
              << generation + 1 << ",\"trainSeedStart\":" << generation_seed
              << ",\"trainMean\":" << results[winner].mean_score
              << ",\"trainMedian\":" << results[winner].median_score
              << ",\"probeMean\":" << winner_probe.mean_score
              << ",\"probeMedian\":" << winner_probe.median_score
              << ",\"bestProbeMean\":" << best_probe.mean_score
              << ",\"bestProbeMedian\":" << best_probe.median_score
              << ",\"probeSeconds\":" << probe_seconds
              << ",\"maxRssMiB\":" << maximumResidentMiB() << "}\n";
  }

  const double seconds =
      std::chrono::duration<double>(Clock::now() - training_started).count();
  printStats("BEST_PROBE", best_probe, seconds);
  printWeights("BEST_WEIGHTS", best);
  std::cout << std::fixed << std::setprecision(3)
            << "MEMORY {\"populationBytes\":"
            << options.population * sizeof(Weights)
            << ",\"statisticsBytes\":"
            << options.population * sizeof(GameStats)
            << ",\"maxRssMiB\":" << maximumResidentMiB() << "}\n";
  return best;
}

bool selfTest(std::ostream& output) {
  const PolicyOptions options{7};
  State state = initialHeadlessState(kTrainingSeedStart);
  MoveResult move;
  if (!playHeadlessMove(state, kTrainingSeedStart, 1, move) ||
      !playHeadlessMove(state, kTrainingSeedStart, 4, move)) {
    output << "SELF_TEST failed to construct test state\n";
    return false;
  }
  if (reflectedBoard(reflectedBoard(state.board)) != state.board) {
    output << "SELF_TEST reflection is not an involution\n";
    return false;
  }

  const int first = selectAction(state, kHandWeights, options);
  const int second = selectAction(state, kHandWeights, options);
  if (first != second) {
    output << "SELF_TEST observable policy was not deterministic\n";
    return false;
  }

  State mirrored = state;
  mirrored.board = reflectedBoard(state.board);
  const int mirrored_action = selectAction(mirrored, kHandWeights, options);
  if (mirrored_action != kBoardSize - 1 - first) {
    output << "SELF_TEST selected move did not reflect: " << first << " vs "
           << mirrored_action << '\n';
    return false;
  }
  for (int column = 0; column < kBoardSize; ++column) {
    if (!isLegal(state.board, column)) continue;
    const int counterpart = kBoardSize - 1 - column;
    const double original = evaluateAction(state, column, kHandWeights, options);
    const double reflected =
        evaluateAction(mirrored, counterpart, kHandWeights, options);
    if (std::abs(original - reflected) > 1e-10) {
      output << "SELF_TEST mirror score mismatch in column " << column << '\n';
      return false;
    }
  }

  const CanonicalState canonical = canonicalize(state);
  const std::uint32_t first_hash = observableHash(
      canonical.state, canonicalColumn(canonical, first));
  const std::uint32_t second_hash = observableHash(
      canonical.state, canonicalColumn(canonical, first));
  if (first_hash != second_hash) {
    output << "SELF_TEST chance probes depended on hidden state\n";
    return false;
  }
  output << "SELF_TEST {\"deterministic\":true,\"mirrorInvariant\":true,"
            "\"seedBlindInterface\":true}\n";
  return true;
}

std::string valueAfter(int argc, char** argv, std::string_view flag,
                       std::string fallback = {}) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == flag) return argv[index + 1];
  }
  return fallback;
}

bool hasFlag(int argc, char** argv, std::string_view flag) {
  for (int index = 1; index < argc; ++index) {
    if (argv[index] == flag) return true;
  }
  return false;
}

int parsePositive(const std::string& value, std::string_view name) {
  std::size_t consumed = 0;
  const long long parsed = std::stoll(value, &consumed, 10);
  if (consumed != value.size() || parsed < 1 ||
      parsed > std::numeric_limits<int>::max()) {
    throw std::invalid_argument(std::string(name) + " must be positive");
  }
  return static_cast<int>(parsed);
}

std::uint32_t parseUint32(const std::string& value, std::string_view name) {
  std::size_t consumed = 0;
  const unsigned long long parsed = std::stoull(value, &consumed, 0);
  if (consumed != value.size() || parsed > 0xffff'ffffull) {
    throw std::invalid_argument(std::string(name) + " must be a uint32");
  }
  return static_cast<std::uint32_t>(parsed);
}

int runBenchmark(int argc, char** argv) {
  const int games = parsePositive(valueAfter(argc, argv, "--games", "64"),
                                  "--games");
  const int max_moves = parsePositive(
      valueAfter(argc, argv, "--max-moves", "750"), "--max-moves");
  const int chance_probes = parsePositive(
      valueAfter(argc, argv, "--chance-probes", "7"), "--chance-probes");
  const std::uint32_t seed_start = parseUint32(
      valueAfter(argc, argv, "--seed-start", "0x4d700000"), "--seed-start");
  const auto started = Clock::now();
  const GameStats stats = evaluatePolicy(kHandWeights, seed_start, games,
                                         max_moves, chance_probes);
  printStats("HAND_BENCHMARK", stats,
             std::chrono::duration<double>(Clock::now() - started).count());
  printWeights("HAND_WEIGHTS", kHandWeights);
  return 0;
}

int runTrain(int argc, char** argv) {
  TrainingOptions options;
  options.generations = parsePositive(
      valueAfter(argc, argv, "--generations", std::to_string(options.generations)),
      "--generations");
  options.population = parsePositive(
      valueAfter(argc, argv, "--population", std::to_string(options.population)),
      "--population");
  options.elite = parsePositive(
      valueAfter(argc, argv, "--elite", std::to_string(options.elite)),
      "--elite");
  options.training_games = parsePositive(
      valueAfter(argc, argv, "--training-games",
                 std::to_string(options.training_games)),
      "--training-games");
  options.probe_games = parsePositive(
      valueAfter(argc, argv, "--probe-games", std::to_string(options.probe_games)),
      "--probe-games");
  options.max_moves = parsePositive(
      valueAfter(argc, argv, "--max-moves", std::to_string(options.max_moves)),
      "--max-moves");
  options.chance_probes = parsePositive(
      valueAfter(argc, argv, "--chance-probes",
                 std::to_string(options.chance_probes)),
      "--chance-probes");
  options.final_chance_probes = parsePositive(
      valueAfter(argc, argv, "--final-chance-probes",
                 std::to_string(options.final_chance_probes)),
      "--final-chance-probes");
  options.threads = parsePositive(
      valueAfter(argc, argv, "--threads", std::to_string(options.threads)),
      "--threads");
  options.optimizer_seed = parseUint32(
      valueAfter(argc, argv, "--optimizer-seed",
                 std::to_string(options.optimizer_seed)),
      "--optimizer-seed");
  train(options);
  return 0;
}

void printUsage() {
  std::cerr
      << "Usage:\n"
      << "  drop7_evolution --self-test\n"
      << "  drop7_evolution --benchmark-hand [--games N] [--seed-start N] "
         "[--chance-probes N] [--max-moves N]\n"
      << "  drop7_evolution --train [--generations N] [--population N] "
         "[--elite N] [--training-games N] [--probe-games N] "
         "[--threads N]\n";
}

}  // namespace drop7::evolution

int main(int argc, char** argv) {
  using namespace drop7::evolution;
  try {
    if (hasFlag(argc, argv, "--self-test")) return selfTest(std::cout) ? 0 : 1;
    if (hasFlag(argc, argv, "--benchmark-hand")) return runBenchmark(argc, argv);
    if (hasFlag(argc, argv, "--train")) return runTrain(argc, argv);
    printUsage();
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
