#pragma once

#include "engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <list>
#include <ostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace drop7::cfpi {

struct BehaviorOptions {
  int max_depth = 3;
  int chance_samples = 5;
  std::uint64_t max_work = 1'000'000;
  std::size_t max_cache_entries = 40'000;
  double terminal_utility = -1'000'000.0;
  std::uint32_t policy_seed = 0xd707'5eedu;
};

struct BehaviorMetrics {
  int requested_depth = 0;
  int completed_depth = 0;
  bool complete = false;
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
  std::uint64_t cache_hits = 0;
  std::size_t cache_entries = 0;
  double value = -std::numeric_limits<double>::infinity();
};

struct PhaseMetrics {
  double potential = 0;
  int occupied = 0;
  int covers = 0;
  int maximum_height = 0;
  int legal_columns = 0;
  int moves_until_rise = kMovesPerLevel;
};

namespace detail {

using drop7::Board;
using drop7::MoveResult;
using drop7::State;

struct LineAnalysis {
  std::array<int, drop7::kCellCount> horizontal_lengths{};
  std::array<int, drop7::kCellCount> horizontal_starts{};
  std::array<int, drop7::kCellCount> horizontal_ends{};
  std::array<int, drop7::kCellCount> vertical_lengths{};
  std::array<int, drop7::kCellCount> vertical_starts{};
  std::array<int, drop7::kCellCount> vertical_ends{};
};

struct DiscAnalysis {
  bool present = false;
  int value = 0;
  int row = 0;
  int column = 0;
  int horizontal_length = 0;
  int vertical_length = 0;
  double horizontal_addition = 0;
  double vertical_addition = 0;
  double addition = 0;
  double horizontal_release = 0;
  double vertical_release = 0;
  double release = 0;
};

struct PhaseFeatures {
  int open_columns = 0;
  double height_load = 0;
  int solid_cells = 0;
  int cracked_cells = 0;
  int numbered_cells = 0;
  int high_low_numbers = 0;
  double direct_potential = 0;
  double latent_chain_potential = 0;
  double cracked_exposure = 0;
  double solid_exposure = 0;
  double adjacent_ones = 0;
  double triple_twos = 0;
  double dead_low_numbers = 0;
  double projected_occupancy_debt = 0;
  double residual_cover_debt = 0;
  double cover_altitude_debt = 0;
  double imminent_cover_altitude_debt = 0;
  double peak_height_risk = 0;
  double low_cap_load = 0;
  double adjacent_low_cap_load = 0;
  double quiet_build_options = 0;
  double quiet_direct_gain = 0;
  double trigger_readiness = 0;
  double rise_trigger_readiness = 0;
};

inline double readiness(int cost) {
  return cost >= 1 ? std::ldexp(1.0, 1 - cost) : 0.0;
}

inline double unionReadiness(double first, double second) {
  return 1.0 - (1.0 - first) * (1.0 - second);
}

inline std::array<int, drop7::kBoardSize> columnHeights(const Board& board) {
  std::array<int, drop7::kBoardSize> heights{};
  for (int column = 0; column < drop7::kBoardSize; ++column) {
    for (int row = 0; row < drop7::kBoardSize; ++row) {
      if (board[drop7::indexOf(row, column)] != drop7::kEmpty) {
        ++heights[column];
      }
    }
  }
  return heights;
}

inline LineAnalysis analyzeLines(const Board& board) {
  LineAnalysis lines;
  lines.horizontal_starts.fill(-1);
  lines.horizontal_ends.fill(-1);
  lines.vertical_starts.fill(-1);
  lines.vertical_ends.fill(-1);
  for (int row = 0; row < drop7::kBoardSize; ++row) {
    int cursor = 0;
    while (cursor < drop7::kBoardSize) {
      if (board[drop7::indexOf(row, cursor)] == drop7::kEmpty) {
        ++cursor;
        continue;
      }
      const int start = cursor;
      while (cursor < drop7::kBoardSize &&
             board[drop7::indexOf(row, cursor)] != drop7::kEmpty) {
        ++cursor;
      }
      const int end = cursor - 1;
      const int length = end - start + 1;
      for (int column = start; column <= end; ++column) {
        const int index = drop7::indexOf(row, column);
        lines.horizontal_lengths[index] = length;
        lines.horizontal_starts[index] = start;
        lines.horizontal_ends[index] = end;
      }
    }
  }
  for (int column = 0; column < drop7::kBoardSize; ++column) {
    int cursor = 0;
    while (cursor < drop7::kBoardSize) {
      if (board[drop7::indexOf(cursor, column)] == drop7::kEmpty) {
        ++cursor;
        continue;
      }
      const int start = cursor;
      while (cursor < drop7::kBoardSize &&
             board[drop7::indexOf(cursor, column)] != drop7::kEmpty) {
        ++cursor;
      }
      const int end = cursor - 1;
      const int length = end - start + 1;
      for (int row = start; row <= end; ++row) {
        const int index = drop7::indexOf(row, column);
        lines.vertical_lengths[index] = length;
        lines.vertical_starts[index] = start;
        lines.vertical_ends[index] = end;
      }
    }
  }
  return lines;
}

inline double minimumHorizontalAdditionCost(
    int row, int value, int segment_start, int segment_end,
    int segment_length,
    const std::array<int, drop7::kBoardSize>& heights) {
  if (segment_start < 0 || value <= segment_length) return -1.0;
  const int elevation = drop7::kBoardSize - row;
  int best = std::numeric_limits<int>::max();
  for (int start = 0; start + value <= drop7::kBoardSize; ++start) {
    const int end = start + value - 1;
    if (start > segment_start || end < segment_end) continue;
    if (start > 0 && heights[start - 1] >= elevation) continue;
    if (end + 1 < drop7::kBoardSize && heights[end + 1] >= elevation) continue;
    int cost = 0;
    for (int column = start; column <= end; ++column) {
      cost += std::max(0, elevation - heights[column]);
    }
    if (cost > 0) best = std::min(best, cost);
  }
  return best == std::numeric_limits<int>::max() ? -1.0
                                                  : static_cast<double>(best);
}

inline double releaseReadiness(int excess, std::vector<double> support) {
  if (excess <= 0 || static_cast<int>(support.size()) < excess) return 0;
  std::sort(support.begin(), support.end(), std::greater<double>());
  return support[excess - 1] * readiness(excess);
}

inline void placementInventory(const State& state,
                        const std::array<int, drop7::kBoardSize>& heights,
                        PhaseFeatures& features) {
  for (int column = 0; column < drop7::kBoardSize; ++column) {
    const int old_vertical = heights[column];
    if (old_vertical >= drop7::kBoardSize) continue;
    const int new_vertical = old_vertical + 1;
    const int landing_row = drop7::kBoardSize - new_vertical;
    int left = 0;
    for (int target = column - 1; target >= 0; --target) {
      if (state.board[drop7::indexOf(landing_row, target)] == drop7::kEmpty) break;
      ++left;
    }
    int right = 0;
    for (int target = column + 1; target < drop7::kBoardSize; ++target) {
      if (state.board[drop7::indexOf(landing_row, target)] == drop7::kEmpty) break;
      ++right;
    }
    const int new_horizontal = left + 1 + right;
    int triggers = (state.next_disc == new_horizontal ||
                    state.next_disc == new_vertical)
                       ? 1
                       : 0;
    double direct_gain = unionReadiness(
        readiness(static_cast<int>(state.next_disc) - new_horizontal),
        readiness(static_cast<int>(state.next_disc) - new_vertical));

    for (int target = column - left; target <= column + right; ++target) {
      if (target == column) continue;
      const std::uint8_t cell =
          state.board[drop7::indexOf(landing_row, target)];
      if (!drop7::isNumbered(cell)) continue;
      const int old_horizontal = target < column ? left : right;
      const double old_ready = unionReadiness(
          readiness(static_cast<int>(cell) - old_horizontal),
          readiness(static_cast<int>(cell) - heights[target]));
      const double new_ready = unionReadiness(
          readiness(static_cast<int>(cell) - new_horizontal),
          readiness(static_cast<int>(cell) - heights[target]));
      direct_gain += std::max(0.0, new_ready - old_ready);
      if (cell == new_horizontal) ++triggers;
    }
    for (int row = drop7::kBoardSize - old_vertical;
         row < drop7::kBoardSize; ++row) {
      const std::uint8_t cell = state.board[drop7::indexOf(row, column)];
      if (!drop7::isNumbered(cell)) continue;
      direct_gain += std::max(
          0.0, readiness(static_cast<int>(cell) - new_vertical) -
                   readiness(static_cast<int>(cell) - old_vertical));
      if (cell == new_vertical) ++triggers;
    }
    if (triggers > 0) {
      features.trigger_readiness += 0.5 + triggers;
    } else {
      features.quiet_build_options += 1;
      features.quiet_direct_gain =
          std::max(features.quiet_direct_gain, direct_gain);
    }
  }
}

inline PhaseFeatures extractPhaseFeatures(const State& state) {
  PhaseFeatures features;
  const Board& board = state.board;
  const auto heights = columnHeights(board);
  const LineAnalysis lines = analyzeLines(board);
  std::array<DiscAnalysis, drop7::kCellCount> discs{};
  int occupied = 0;
  int covers = 0;
  int maximum_height = 0;

  for (int column = 0; column < drop7::kBoardSize; ++column) {
    if (board[column] == drop7::kEmpty) ++features.open_columns;
    maximum_height = std::max(maximum_height, heights[column]);

  }

  for (int row = 0; row < drop7::kBoardSize; ++row) {
    const int elevation = drop7::kBoardSize - row;
    for (int column = 0; column < drop7::kBoardSize; ++column) {
      const int index = drop7::indexOf(row, column);
      const std::uint8_t cell = board[index];
      if (cell == drop7::kEmpty) continue;
      ++occupied;
      features.height_load += elevation * elevation;
      if (cell == drop7::kSolid || cell == drop7::kCracked) {
        ++covers;
        if (cell == drop7::kSolid) ++features.solid_cells;
        else ++features.cracked_cells;
        const double cover_factor = cell == drop7::kSolid ? 1.0 : 0.65;
        const double edge_factor =
            column == 0 || column == drop7::kBoardSize - 1 ? 1.3 : 1.0;
        features.cover_altitude_debt +=
            elevation * elevation * cover_factor * edge_factor;
        continue;
      }
      if (!drop7::isNumbered(cell)) continue;
      ++features.numbered_cells;
      if (cell <= 2 && elevation >= 5) ++features.high_low_numbers;
      DiscAnalysis& disc = discs[index];
      disc.present = true;
      disc.value = cell;
      disc.row = row;
      disc.column = column;
      disc.horizontal_length = lines.horizontal_lengths[index];
      disc.vertical_length = lines.vertical_lengths[index];
      disc.vertical_addition =
          cell > heights[column]
              ? readiness(static_cast<int>(cell) - heights[column])
              : 0.0;
      const double horizontal_cost = minimumHorizontalAdditionCost(
          row, cell, lines.horizontal_starts[index],
          lines.horizontal_ends[index], lines.horizontal_lengths[index],
          heights);
      disc.horizontal_addition =
          horizontal_cost < 0 ? 0.0 : readiness(static_cast<int>(horizontal_cost));
      disc.addition = unionReadiness(disc.horizontal_addition,
                                     disc.vertical_addition);
      features.direct_potential += disc.addition;
    }
  }

  for (int index = 0; index < drop7::kCellCount; ++index) {
    DiscAnalysis& disc = discs[index];
    if (!disc.present) continue;
    std::vector<double> horizontal_support;
    std::vector<double> vertical_support;
    for (int column = lines.horizontal_starts[index];
         column <= lines.horizontal_ends[index]; ++column) {
      const int supporter = drop7::indexOf(disc.row, column);
      if (supporter != index && discs[supporter].present) {
        horizontal_support.push_back(discs[supporter].addition);
      }
    }
    for (int row = lines.vertical_starts[index];
         row <= lines.vertical_ends[index]; ++row) {
      const int supporter = drop7::indexOf(row, disc.column);
      if (supporter != index && discs[supporter].present) {
        vertical_support.push_back(discs[supporter].addition);
      }
    }
    disc.horizontal_release = releaseReadiness(
        disc.horizontal_length - disc.value, std::move(horizontal_support));
    disc.vertical_release = releaseReadiness(
        disc.vertical_length - disc.value, std::move(vertical_support));
    disc.release = unionReadiness(disc.horizontal_release,
                                  disc.vertical_release);
    features.latent_chain_potential += disc.release;
    if (disc.value <= 2 && disc.horizontal_length > disc.value &&
        disc.vertical_length > disc.value) {
      features.dead_low_numbers +=
          1.0 - unionReadiness(disc.addition, disc.release);
    }
  }

  for (int row = 0; row < drop7::kBoardSize; ++row) {
    for (int column = 0; column < drop7::kBoardSize; ++column) {
      const int index = drop7::indexOf(row, column);
      if (board[index] == 1) {
        if (column + 1 < drop7::kBoardSize && board[index + 1] == 1) {
          const double escape = std::max(
              unionReadiness(discs[index].vertical_addition,
                             discs[index].vertical_release),
              unionReadiness(discs[index + 1].vertical_addition,
                             discs[index + 1].vertical_release));
          features.adjacent_ones += 1.0 - escape;
        }
        if (row + 1 < drop7::kBoardSize &&
            board[index + drop7::kBoardSize] == 1) {
          const double escape = std::max(
              unionReadiness(discs[index].horizontal_addition,
                             discs[index].horizontal_release),
              unionReadiness(discs[index + drop7::kBoardSize].horizontal_addition,
                             discs[index + drop7::kBoardSize].horizontal_release));
          features.adjacent_ones += 1.0 - escape;
        }
      }
    }
  }

  for (int row = 0; row < drop7::kBoardSize; ++row) {
    int cursor = 0;
    while (cursor < drop7::kBoardSize) {
      if (board[drop7::indexOf(row, cursor)] != 2) {
        ++cursor;
        continue;
      }
      const int start = cursor;
      while (cursor < drop7::kBoardSize &&
             board[drop7::indexOf(row, cursor)] == 2) ++cursor;
      const int excess = cursor - start - 2;
      if (excess > 0) {
        double escape = 0;
        for (int column = start; column < cursor; ++column) {
          const DiscAnalysis& disc = discs[drop7::indexOf(row, column)];
          escape = std::max(escape, unionReadiness(
              disc.vertical_addition, disc.vertical_release));
        }
        features.triple_twos += excess * excess * (1.0 - escape);
      }
    }
  }
  for (int column = 0; column < drop7::kBoardSize; ++column) {
    int cursor = 0;
    while (cursor < drop7::kBoardSize) {
      if (board[drop7::indexOf(cursor, column)] != 2) {
        ++cursor;
        continue;
      }
      const int start = cursor;
      while (cursor < drop7::kBoardSize &&
             board[drop7::indexOf(cursor, column)] == 2) ++cursor;
      const int excess = cursor - start - 2;
      if (excess > 0) {
        double escape = 0;
        for (int row = start; row < cursor; ++row) {
          const DiscAnalysis& disc = discs[drop7::indexOf(row, column)];
          escape = std::max(escape, unionReadiness(
              disc.horizontal_addition, disc.horizontal_release));
        }
        features.triple_twos += excess * excess * (1.0 - escape);
      }
    }
  }

  constexpr std::array<std::array<int, 2>, 4> directions{{
      {{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}},
  }};
  for (int row = 0; row < drop7::kBoardSize; ++row) {
    for (int column = 0; column < drop7::kBoardSize; ++column) {
      const int index = drop7::indexOf(row, column);
      const std::uint8_t cell = board[index];
      if (cell != drop7::kSolid && cell != drop7::kCracked) continue;
      std::array<double, 4> support{};
      int count = 0;
      for (const auto& direction : directions) {
        const int next_row = row + direction[0];
        const int next_column = column + direction[1];
        if (!drop7::inside(next_row, next_column)) continue;
        const DiscAnalysis& disc =
            discs[drop7::indexOf(next_row, next_column)];
        if (disc.present) support[count++] = unionReadiness(
            disc.addition, disc.release);
      }
      std::sort(support.begin(), support.begin() + count,
                std::greater<double>());
      if (cell == drop7::kCracked) {
        double inverse = 1;
        for (int offset = 0; offset < count; ++offset) {
          inverse *= 1.0 - support[offset];
        }
        features.cracked_exposure += 1.0 - inverse;
      } else {
        features.solid_exposure +=
            (count > 0 ? support[0] * 0.35 : 0.0) +
            (count > 1 ? support[1] * 0.65 : 0.0);
      }
    }
  }

  const int moves_until_rise =
      std::max(1, std::min(drop7::kMovesPerLevel, state.moves_remaining));
  const double rise_urgency =
      static_cast<double>(drop7::kMovesPerLevel - moves_until_rise) /
      static_cast<double>(drop7::kMovesPerLevel - 1);
  const double projected_occupancy =
      occupied + drop7::kBoardSize - 1.4 * moves_until_rise;
  features.projected_occupancy_debt =
      std::pow(std::max(0.0, projected_occupancy - 14.0), 2.0);
  const double residual_covers =
      std::max(0.0, covers - 1.4 * moves_until_rise);
  features.residual_cover_debt = residual_covers * residual_covers;
  features.imminent_cover_altitude_debt =
      features.cover_altitude_debt * rise_urgency;
  features.peak_height_risk = std::pow(
      std::max(0.0, maximum_height + rise_urgency - 3.0), 3.0);

  std::array<bool, drop7::kBoardSize> low_caps{};
  for (int column = 0; column < drop7::kBoardSize; ++column) {
    const int height = heights[column];
    if (height == 0) continue;
    const std::uint8_t cap =
        board[drop7::indexOf(drop7::kBoardSize - height, column)];
    if (cap != 1 && cap != 2) continue;
    low_caps[column] = true;
    features.low_cap_load +=
        height * height * (cap == 1 ? 1.5 : 1.0);
    if (column > 0 && low_caps[column - 1]) {
      features.adjacent_low_cap_load +=
          std::pow(std::min(heights[column - 1], height), 2.0);
    }
  }

  placementInventory(state, heights, features);

  Board raised{};
  if (drop7::raiseCoveredRow(board, raised)) {
    int popper_count = 0;
    drop7::findPoppers(raised, popper_count);
    const double immediate_rise_weight =
        moves_until_rise == 1 ? 1.0 : readiness(moves_until_rise - 1);
    features.rise_trigger_readiness = popper_count * immediate_rise_weight;
  }
  return features;
}

inline double phaseUtility(const State& state) {
  if (state.game_over) return -250'000.0;
  const PhaseFeatures f = extractPhaseFeatures(state);
  // This is the release2 + queue2 + altitude2 phase-safety profile. The first
  // block mirrors the existing combined observable evaluator; the second
  // doubles queue and altitude debt, and the release inventory is doubled.
  return
      180.0 * f.open_columns - 10.0 * f.height_load -
      620.0 * f.solid_cells - 220.0 * f.cracked_cells -
      18.0 * f.numbered_cells - 90.0 * f.high_low_numbers +
      140.0 * f.direct_potential + 360.0 * f.latent_chain_potential +
      100.0 * f.cracked_exposure + 40.0 * f.solid_exposure -
      550.0 * f.adjacent_ones - 750.0 * f.triple_twos -
      120.0 * f.dead_low_numbers -
      240.0 * f.projected_occupancy_debt -
      200.0 * f.residual_cover_debt -
      50.0 * f.cover_altitude_debt -
      70.0 * f.imminent_cover_altitude_debt -
      1800.0 * f.peak_height_risk - 120.0 * f.low_cap_load -
      180.0 * f.adjacent_low_cap_load +
      220.0 * f.direct_potential + 300.0 * f.quiet_build_options +
      600.0 * f.quiet_direct_gain +
      600.0 * f.trigger_readiness +
      440.0 * (f.latent_chain_potential + f.cracked_exposure +
               0.35 * f.solid_exposure) +
      1200.0 * f.rise_trigger_readiness;
}

inline Board mirrorBoard(const Board& board) {
  Board mirrored{};
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      mirrored[indexOf(row, kBoardSize - 1 - column)] =
          board[indexOf(row, column)];
    }
  }
  return mirrored;
}

inline bool mirroredRepresentationIsSmaller(const Board& board) {
  const Board mirrored = mirrorBoard(board);
  return std::lexicographical_compare(mirrored.begin(), mirrored.end(),
                                      board.begin(), board.end());
}

inline State canonicalState(const State& state, bool& mirrored) {
  mirrored = mirroredRepresentationIsSmaller(state.board);
  if (!mirrored) {
    State result = state;
    result.score = 0;
    return result;
  }
  State result = state;
  result.board = mirrorBoard(state.board);
  result.score = 0;
  return result;
}

constexpr std::array<int, kBoardSize> kColumnOrder{{3, 2, 4, 1, 5, 0, 6}};
constexpr std::uint32_t kRevealSampleDomain = 0x5245'564cu;
constexpr std::uint32_t kDiscSampleDomain = 0x4449'5343u;
constexpr std::uint32_t kSampleMultiplier = 0x9e37'79b9u;
constexpr std::uint32_t kDepthMultiplier = 0x85eb'ca6bu;

inline double stratifiedUnit(std::uint32_t seed, int sample, int count,
                             std::uint32_t domain, int event) {
  const std::uint32_t event_seed = mix32(
      seed ^ domain ^
      (static_cast<std::uint32_t>(event + 1) * kDepthMultiplier));
  const int rotation = static_cast<int>(event_seed %
                                        static_cast<std::uint32_t>(count));
  const int stratum = (sample + rotation) % count;
  const double jitter = static_cast<double>(
      mix32(event_seed ^
            (static_cast<std::uint32_t>(sample + 1) * kSampleMultiplier))) /
      4'294'967'296.0;
  return (static_cast<double>(stratum) + jitter) /
         static_cast<double>(count);
}

struct StratifiedRandom {
  std::uint32_t seed = 0;
  int sample = 0;
  int count = 1;
  int event = 0;

  std::uint8_t nextDisc() {
    const double unit = stratifiedUnit(seed, sample, count,
                                       kRevealSampleDomain, event++);
    return static_cast<std::uint8_t>(
        std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
  }
};

template <typename Random>
inline void resolveCascadeSampled(Board& board, Random& random,
                                  int starting_depth, std::int64_t& score,
                                  std::vector<Wave>& waves) {
  for (int depth = starting_depth;; ++depth) {
    int popper_count = 0;
    const auto poppers = findPoppers(board, popper_count);
    if (popper_count == 0) return;

    std::array<bool, kCellCount> popping{};
    Board cleared = board;
    for (int offset = 0; offset < popper_count; ++offset) {
      const int index = poppers[offset];
      popping[index] = true;
      cleared[index] = kEmpty;
    }

    std::array<int, kCellCount> reveals{};
    int reveal_count = 0;
    constexpr std::array<std::array<int, 2>, 4> directions{{
        {{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}},
    }};
    for (int row = 0; row < kBoardSize; ++row) {
      for (int column = 0; column < kBoardSize; ++column) {
        const int index = indexOf(row, column);
        const std::uint8_t cell = board[index];
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
        if (hits == 0) continue;
        const int hits_needed = cell == kSolid ? 2 : 1;
        if (hits >= hits_needed) {
          reveals[reveal_count++] = index;
        } else {
          cleared[index] = kCracked;
        }
      }
    }

    for (int offset = 0; offset < reveal_count; ++offset) {
      cleared[reveals[offset]] = random.nextDisc();
    }
    const std::int64_t points = popper_count * scoreForWave(depth);
    score += points;
    waves.push_back({depth, popper_count, reveal_count, points});
    board = applyGravity(cleared);
  }
}

template <typename Random>
inline bool playMoveSampled(const State& state, int column, Random& random,
                            MoveResult& result) {
  if (state.game_over) return false;
  Board board = state.board;
  if (!placeDisc(board, column, state.next_disc)) return false;

  result = MoveResult{};
  std::int64_t first_score = 0;
  resolveCascadeSampled(board, random, 1, first_score, result.waves);
  result.score_delta = first_score;
  result.cleared_board = isBoardEmpty(board);
  if (result.cleared_board) result.score_delta += kClearBonus;

  int level = state.level;
  int moves_remaining = state.moves_remaining - 1;
  bool game_over = false;
  if (moves_remaining == 0) {
    Board raised{};
    if (!raiseCoveredRow(board, raised)) {
      game_over = true;
    } else {
      result.level_advanced = true;
      ++level;
      moves_remaining = kMovesPerLevel;
      result.score_delta += kLevelBonus;
      board = raised;
      std::int64_t level_score = 0;
      const int next_depth = result.waves.empty()
                                 ? 1
                                 : result.waves.back().depth + 1;
      resolveCascadeSampled(board, random, next_depth, level_score,
                            result.waves);
      result.score_delta += level_score;
      if (isBoardEmpty(board)) {
        result.score_delta += kClearBonus;
        result.cleared_board = true;
      }
    }
  }

  int legal_count = 0;
  legalColumns(board, legal_count);
  if (!game_over && legal_count == 0) game_over = true;

  result.state.board = board;
  result.state.next_disc =
      game_over ? state.next_disc : random.nextDisc();
  result.state.score = state.score + result.score_delta;
  result.state.level = level;
  result.state.moves_remaining = moves_remaining;
  result.state.moves_played = state.moves_played + 1;
  result.state.game_over = game_over;
  return true;
}

inline std::uint32_t scenarioSeedForState(const State& state,
                                          std::uint32_t policy_seed,
                                          int depth) {
  std::uint32_t hash = 0x811c'9dc5u;
  for (std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint32_t>(cell + 1u);
    hash *= 0x0100'0193u;
  }
  hash ^= static_cast<std::uint32_t>(state.next_disc);
  hash *= 0x0100'0193u;
  hash ^= static_cast<std::uint32_t>(state.moves_remaining);
  return mix32(hash ^ policy_seed ^
               (static_cast<std::uint32_t>(depth + 1) * kDepthMultiplier));
}

inline std::uint8_t sampledNextDisc(std::uint32_t seed, int sample,
                                    int count) {
  const double unit =
      stratifiedUnit(seed, sample, count, kDiscSampleDomain, 0);
  return static_cast<std::uint8_t>(
      std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
}

inline std::string dynamicStateKey(const State& state, int depth) {
  std::string key;
  key.reserve(kCellCount + 3);
  for (std::uint8_t cell : state.board) {
    key.push_back(static_cast<char>(cell));
  }
  key.push_back(static_cast<char>(state.next_disc));
  key.push_back(static_cast<char>(state.moves_remaining));
  key.push_back(static_cast<char>(depth));
  return key;
}

class WorkLimitReached : public std::exception {};

struct CacheEntry {
  double value = 0;
  std::list<std::string>::iterator order;
};

struct SearchContext {
  explicit SearchContext(const BehaviorOptions& behavior_options)
      : options(behavior_options) {}

  const BehaviorOptions& options;
  std::unordered_map<std::string, CacheEntry> cache;
  std::list<std::string> order;
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
  std::uint64_t cache_hits = 0;
};

inline void validateOptions(const BehaviorOptions& options) {
  if (options.max_depth < 1 || options.max_depth > 8) {
    throw std::invalid_argument("behavior max_depth must be from 1 to 8");
  }
  if (options.chance_samples < 1 || options.chance_samples > 32) {
    throw std::invalid_argument("behavior chance_samples must be from 1 to 32");
  }
  if (options.max_work < 1) {
    throw std::invalid_argument("behavior max_work must be positive");
  }
  if (options.max_cache_entries < 1 ||
      options.max_cache_entries > 200'000) {
    throw std::invalid_argument(
        "behavior max_cache_entries must be from 1 to 200000");
  }
  if (!std::isfinite(options.terminal_utility)) {
    throw std::invalid_argument("behavior terminal_utility must be finite");
  }
}

inline void checkBudget(const SearchContext& context) {
  if (context.work >= context.options.max_work) throw WorkLimitReached{};
}

inline void setCachedValue(SearchContext& context, std::string key,
                           double value) {
  const auto prior = context.cache.find(key);
  if (prior != context.cache.end()) {
    context.order.erase(prior->second.order);
    context.cache.erase(prior);
  }
  while (context.cache.size() >= context.options.max_cache_entries) {
    const std::string& oldest = context.order.front();
    context.cache.erase(oldest);
    context.order.pop_front();
  }
  context.order.push_back(key);
  auto order = std::prev(context.order.end());
  context.cache.emplace(std::move(key), CacheEntry{value, order});
}

inline double bestFutureValue(const State& state, int depth,
                              SearchContext& context);

inline double evaluateAction(const State& state, int column, int depth,
                             SearchContext& context) {
  const std::uint32_t state_seed =
      scenarioSeedForState(state, context.options.policy_seed, depth);
  double value = 0;
  for (int sample = 0; sample < context.options.chance_samples; ++sample) {
    checkBudget(context);
    StratifiedRandom random{state_seed, sample,
                            context.options.chance_samples, 0};
    MoveResult move;
    if (!playMoveSampled(state, column, random, move)) {
      value += context.options.terminal_utility;
      continue;
    }
    ++context.work;
    const double score_delta = static_cast<double>(move.score_delta);
    if (move.state.game_over) {
      value += score_delta + context.options.terminal_utility;
      continue;
    }
    move.state.score = 0;
    move.state.next_disc = sampledNextDisc(
        state_seed, sample, context.options.chance_samples);
    bool ignored = false;
    const State next = canonicalState(move.state, ignored);
    value += score_delta + bestFutureValue(next, depth - 1, context);
  }
  return value / static_cast<double>(context.options.chance_samples);
}

inline double evaluateLeaf(const State& state, SearchContext& context) {
  checkBudget(context);
  ++context.work;
  const double value = phaseUtility(state);
  if (!std::isfinite(value)) {
    throw std::runtime_error("phase behavior evaluator returned non-finite");
  }
  return value;
}

inline double bestFutureValue(const State& state, int depth,
                              SearchContext& context) {
  ++context.nodes;
  checkBudget(context);
  if (state.game_over) return context.options.terminal_utility;
  if (depth == 0) return evaluateLeaf(state, context);

  const std::string key = dynamicStateKey(state, depth);
  const auto cached = context.cache.find(key);
  if (cached != context.cache.end()) {
    ++context.cache_hits;
    const double value = cached->second.value;
    context.order.splice(context.order.end(), context.order,
                         cached->second.order);
    return value;
  }

  double best = -std::numeric_limits<double>::infinity();
  for (int column : kColumnOrder) {
    if (!isLegal(state.board, column)) continue;
    best = std::max(best, evaluateAction(state, column, depth, context));
  }
  if (!std::isfinite(best)) best = context.options.terminal_utility;
  setCachedValue(context, key, best);
  return best;
}

inline std::pair<int, double> bestRootAction(const State& canonical,
                                             int depth,
                                             SearchContext& context) {
  int best_column = -1;
  double best_value = -std::numeric_limits<double>::infinity();
  for (int column : kColumnOrder) {
    if (!isLegal(canonical.board, column)) continue;
    const double value = evaluateAction(canonical, column, depth, context);
    if (value > best_value) {
      best_value = value;
      best_column = column;
    }
  }
  return {best_column, best_value};
}

}  // namespace detail

inline double phasePotential(const State& state) {
  return detail::phaseUtility(state);
}

inline PhaseMetrics evaluatePhaseMetrics(const State& state) {
  PhaseMetrics result;
  result.potential = phasePotential(state);
  result.moves_until_rise =
      std::max(1, std::min(kMovesPerLevel, state.moves_remaining));
  std::array<int, kBoardSize> heights{};
  for (int column = 0; column < kBoardSize; ++column) {
    for (int row = 0; row < kBoardSize; ++row) {
      const std::uint8_t cell = state.board[indexOf(row, column)];
      if (cell == kEmpty) continue;
      ++result.occupied;
      ++heights[column];
      if (cell == kSolid || cell == kCracked) ++result.covers;
    }
    result.maximum_height = std::max(result.maximum_height, heights[column]);
    if (state.board[column] == kEmpty) ++result.legal_columns;
  }
  return result;
}

inline int chooseBehaviorAction(const State& input,
                                const BehaviorOptions& options = {},
                                BehaviorMetrics* metrics = nullptr) {
  detail::validateOptions(options);
  BehaviorMetrics local;
  local.requested_depth = options.max_depth;
  if (input.game_over) {
    if (metrics != nullptr) *metrics = local;
    return -1;
  }

  bool mirrored = false;
  const State canonical = detail::canonicalState(input, mirrored);
  detail::SearchContext context(options);
  int completed_column = -1;
  double completed_value = -std::numeric_limits<double>::infinity();
  for (int depth = 1; depth <= options.max_depth; ++depth) {
    try {
      const auto [column, value] =
          detail::bestRootAction(canonical, depth, context);
      if (column < 0) break;
      completed_column = column;
      completed_value = value;
      local.completed_depth = depth;
    } catch (const detail::WorkLimitReached&) {
      break;
    }
  }

  if (completed_column < 0) completed_column = centerFirstMove(canonical.board);
  local.complete = local.completed_depth == options.max_depth;
  local.nodes = context.nodes;
  local.work = context.work;
  local.cache_hits = context.cache_hits;
  local.cache_entries = context.cache.size();
  local.value = completed_value;
  if (metrics != nullptr) *metrics = local;
  return mirrored && completed_column >= 0
             ? kBoardSize - 1 - completed_column
             : completed_column;
}

/**
 * Cheap, deterministic phase-greedy continuation for auxiliary rollouts.
 *
 * This observable, reflection-safe reference behavior policy performs a single
 * expectimax ply. Callers may use one sample for maximum
 * throughput or five samples to retain the behavior policy's root quadrature.
 */
inline int choosePhaseGreedyAction(const State& state,
                                   int chance_samples = 1,
                                   BehaviorMetrics* metrics = nullptr) {
  BehaviorOptions options;
  options.max_depth = 1;
  options.chance_samples = chance_samples;
  options.max_work = 20'000;
  options.max_cache_entries = 512;
  return chooseBehaviorAction(state, options, metrics);
}

inline bool selfTest(std::ostream& output) {
  BehaviorOptions quick;
  quick.max_depth = 2;
  quick.chance_samples = 3;
  quick.max_work = 100'000;
  quick.max_cache_entries = 4'000;

  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;

  BehaviorMetrics first_metrics;
  BehaviorMetrics second_metrics;
  const int first = chooseBehaviorAction(state, quick, &first_metrics);
  const int second = chooseBehaviorAction(state, quick, &second_metrics);
  const bool deterministic =
      first == second && first_metrics.completed_depth ==
                             second_metrics.completed_depth;
  const bool legal = isLegal(state.board, first);
  const int greedy = choosePhaseGreedyAction(state);
  const bool greedy_legal = isLegal(state.board, greedy);

  State mirrored = state;
  mirrored.board = detail::mirrorBoard(state.board);
  const int reflected = chooseBehaviorAction(mirrored, quick);
  const bool mirror_safe = reflected == kBoardSize - 1 - first;
  const bool potential_mirror_safe =
      std::abs(phasePotential(state) - phasePotential(mirrored)) <= 1e-9;

  Mulberry32 random(0x51f7'7e57u);
  State walked = state;
  const double initial = phasePotential(walked);
  double telescoped = 0;
  for (int step = 0; step < 3 && !walked.game_over; ++step) {
    const int action = chooseBehaviorAction(walked, quick);
    const double before = phasePotential(walked);
    MoveResult move;
    if (!playMove(walked, action, random, move)) break;
    walked = move.state;
    const double after = phasePotential(walked);
    telescoped += after - before;
  }
  const bool telescoping =
      std::abs(telescoped - (phasePotential(walked) - initial)) <= 1e-7;

  const bool passed = deterministic && legal && greedy_legal && mirror_safe &&
                      potential_mirror_safe && telescoping;
  output << "{\"deterministic\":" << (deterministic ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false")
         << ",\"greedy_legal\":" << (greedy_legal ? "true" : "false")
         << ",\"mirror_safe\":" << (mirror_safe ? "true" : "false")
         << ",\"potential_mirror_safe\":"
         << (potential_mirror_safe ? "true" : "false")
         << ",\"telescoping\":" << (telescoping ? "true" : "false")
         << ",\"completed_depth\":" << first_metrics.completed_depth
         << ",\"passed\":" << (passed ? "true" : "false") << "}\n";
  return passed;
}

}  // namespace drop7::cfpi
