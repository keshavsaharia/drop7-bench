#include "../../../src/core/native/engine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using drop7::Board;
using drop7::MoveResult;
using drop7::State;

constexpr std::uint32_t kTrainingSeedStart = 0x3d70'0000u;
constexpr std::uint32_t kProbeSeedStart = 0x4d70'0000u;
constexpr std::uint32_t kPlannerDomain = 0x524f'4c4cu;  // "ROLL"
constexpr std::uint32_t kOneStepDomain = 0x4f4e'4553u;  // "ONES"
constexpr int kMaximumHorizon = 100;
constexpr int kMaximumScenarios = 64;
constexpr int kMaximumContinuationSamples = 7;

enum class RiskMode { Mean, LowerHalf, Cvar25, Blend };

struct PlannerOptions {
  int horizon = 25;
  int scenarios = 7;
  int continuation_samples = 1;
  RiskMode risk = RiskMode::Blend;
  double leaf_scale = 1.0;
  double death_penalty = 250'000.0;
  double remaining_death_penalty = 15'000.0;
};

struct RunOptions {
  int games = 4;
  int max_moves = 1000;
  std::uint32_t seed_start = kTrainingSeedStart;
  std::string range = "train";
  PlannerOptions planner;
};

struct PlannerStats {
  std::uint64_t simulated_moves = 0;
  std::uint64_t root_scenarios = 0;
  std::uint64_t one_step_moves = 0;
};

struct CandidateEvaluation {
  int column = -1;
  double utility = -std::numeric_limits<double>::infinity();
  double mean = -std::numeric_limits<double>::infinity();
  double lower_half = -std::numeric_limits<double>::infinity();
  double cvar25 = -std::numeric_limits<double>::infinity();
  std::vector<double> returns;
};

struct Decision {
  int column = -1;
  std::array<CandidateEvaluation, drop7::kBoardSize> candidates{};
  int candidate_count = 0;
};

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  int level = 1;
  bool censored = false;
  std::uint64_t simulated_moves = 0;
};

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
  if (value.empty()) throw std::invalid_argument(std::string(name) + " is required");
  std::size_t consumed = 0;
  const long long parsed = std::stoll(value, &consumed, 10);
  if (consumed != value.size() || parsed < 1 ||
      parsed > std::numeric_limits<int>::max()) {
    throw std::invalid_argument(std::string(name) + " must be positive");
  }
  return static_cast<int>(parsed);
}

double parsePositiveDouble(const std::string& value, std::string_view name) {
  if (value.empty()) throw std::invalid_argument(std::string(name) + " is required");
  std::size_t consumed = 0;
  const double parsed = std::stod(value, &consumed);
  if (consumed != value.size() || !std::isfinite(parsed) || parsed <= 0) {
    throw std::invalid_argument(std::string(name) + " must be positive");
  }
  return parsed;
}

RiskMode parseRisk(const std::string& value) {
  if (value == "mean") return RiskMode::Mean;
  if (value == "lower") return RiskMode::LowerHalf;
  if (value == "cvar25") return RiskMode::Cvar25;
  if (value == "blend") return RiskMode::Blend;
  throw std::invalid_argument("--risk must be mean, lower, cvar25, or blend");
}

std::string_view riskName(RiskMode risk) {
  switch (risk) {
    case RiskMode::Mean:
      return "mean";
    case RiskMode::LowerHalf:
      return "lower";
    case RiskMode::Cvar25:
      return "cvar25";
    case RiskMode::Blend:
      return "blend";
  }
  return "unknown";
}

Board mirrorBoard(const Board& board) {
  Board mirrored{};
  for (int row = 0; row < drop7::kBoardSize; ++row) {
    for (int column = 0; column < drop7::kBoardSize; ++column) {
      mirrored[drop7::indexOf(row, drop7::kBoardSize - 1 - column)] =
          board[drop7::indexOf(row, column)];
    }
  }
  return mirrored;
}

bool boardLess(const Board& first, const Board& second) {
  return std::lexicographical_compare(first.begin(), first.end(),
                                      second.begin(), second.end());
}

State canonicalState(const State& state, bool& was_mirrored) {
  const Board reflected = mirrorBoard(state.board);
  was_mirrored = boardLess(reflected, state.board);
  if (!was_mirrored) return state;
  State canonical = state;
  canonical.board = reflected;
  return canonical;
}

std::uint32_t observableHash(const State& canonical) {
  // Deliberately excludes score, level, and moves_played. They are observable,
  // but do not change future mechanics. Excluding them gives a stronger
  // seed-blind test: equal public decision states share exactly one chance set.
  std::uint32_t hash = 0x811c'9dc5u;
  for (std::uint8_t cell : canonical.board) {
    hash ^= static_cast<std::uint32_t>(cell + 1u);
    hash *= 0x0100'0193u;
  }
  hash ^= static_cast<std::uint32_t>(canonical.next_disc) * 0x9e37'79b9u;
  hash ^= static_cast<std::uint32_t>(canonical.moves_remaining) * 0x85eb'ca6bu;
  return drop7::mix32(hash ^ kPlannerDomain);
}

std::uint32_t stratifiedSeed(std::uint32_t base, int stratum) {
  // Rejection-select a deterministic Mulberry stream whose first seven-way
  // draw is in the requested stratum. Across each block of seven scenarios,
  // the first chance variate therefore covers all seven disc buckets exactly.
  const std::uint8_t target = static_cast<std::uint8_t>(stratum % 7 + 1);
  for (std::uint32_t attempt = 0; attempt < 128; ++attempt) {
    const std::uint32_t seed = drop7::mix32(
        base + attempt * 0x9e37'79b9u + 0x6d2b'79f5u);
    drop7::Mulberry32 random(seed);
    if (random.nextDisc() == target) return seed;
  }
  throw std::runtime_error("failed to construct stratified chance stream");
}

std::uint32_t rolloutSeed(std::uint32_t root_hash, int scenario, int step) {
  const int batch = scenario / 7;
  const int rotation = static_cast<int>((root_hash >> 28) % 7u);
  const int stratum = (scenario + step + rotation) % 7;
  const std::uint32_t base = drop7::mix32(
      root_hash ^ kPlannerDomain ^
      (static_cast<std::uint32_t>(batch + 1) * 0x27d4'eb2du) ^
      (static_cast<std::uint32_t>(step + 1) * 0x1656'67b1u));
  return stratifiedSeed(base, stratum);
}

std::uint32_t oneStepSeed(const State& state, int sample) {
  bool ignored = false;
  const State canonical = canonicalState(state, ignored);
  const std::uint32_t hash = observableHash(canonical);
  const int rotation = static_cast<int>((hash >> 24) % 7u);
  const std::uint32_t base = drop7::mix32(
      hash ^ kOneStepDomain ^
      (static_cast<std::uint32_t>(sample / 7 + 1) * 0x94d0'49bbu));
  return stratifiedSeed(base, (sample + rotation) % 7);
}

double readiness(int cost) {
  return cost >= 1 ? std::ldexp(1.0, 1 - cost) : 0.0;
}

double unionReadiness(double first, double second) {
  return 1.0 - (1.0 - first) * (1.0 - second);
}

std::array<int, drop7::kBoardSize> columnHeights(const Board& board) {
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

LineAnalysis analyzeLines(const Board& board) {
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

double minimumHorizontalAdditionCost(
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

double releaseReadiness(int excess, std::vector<double> support) {
  if (excess <= 0 || static_cast<int>(support.size()) < excess) return 0;
  std::sort(support.begin(), support.end(), std::greater<double>());
  return support[excess - 1] * readiness(excess);
}

void placementInventory(const State& state,
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

PhaseFeatures extractPhaseFeatures(const State& state) {
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

double phaseUtility(const State& state) {
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

int canonicalTieRank(int column) {
  constexpr std::array<int, drop7::kBoardSize> ranks{{5, 3, 1, 0, 2, 4, 6}};
  return ranks[column];
}

int oneStepMoveCanonical(const State& canonical,
                         const PlannerOptions& options,
                         PlannerStats& stats) {
  int legal_count = 0;
  const auto legal = drop7::legalColumns(canonical.board, legal_count);
  if (legal_count == 0) return -1;
  int best_column = legal[0];
  double best_value = -std::numeric_limits<double>::infinity();
  for (int offset = 0; offset < legal_count; ++offset) {
    const int column = legal[offset];
    double value = 0;
    for (int sample = 0; sample < options.continuation_samples; ++sample) {
      drop7::Mulberry32 random(oneStepSeed(canonical, sample));
      MoveResult move;
      if (!drop7::playMove(canonical, column, random, move)) {
        throw std::runtime_error("one-step policy selected illegal candidate");
      }
      ++stats.simulated_moves;
      ++stats.one_step_moves;
      value += static_cast<double>(move.score_delta) +
               options.leaf_scale * phaseUtility(move.state);
    }
    value /= options.continuation_samples;
    if (value > best_value + 1e-9 ||
        (std::abs(value - best_value) <= 1e-9 &&
         canonicalTieRank(column) < canonicalTieRank(best_column))) {
      best_value = value;
      best_column = column;
    }
  }
  return best_column;
}

int oneStepMove(const State& state, const PlannerOptions& options,
                PlannerStats& stats) {
  bool mirrored = false;
  const State canonical = canonicalState(state, mirrored);
  const int column = oneStepMoveCanonical(canonical, options, stats);
  return mirrored && column >= 0 ? drop7::kBoardSize - 1 - column : column;
}

double tailMean(const std::vector<double>& sorted, double fraction) {
  const int count = std::max(
      1, static_cast<int>(std::ceil(sorted.size() * fraction - 1e-12)));
  return std::accumulate(sorted.begin(), sorted.begin() + count, 0.0) /
         count;
}

void finalizeCandidate(CandidateEvaluation& candidate, RiskMode risk) {
  std::vector<double> sorted = candidate.returns;
  std::sort(sorted.begin(), sorted.end());
  candidate.mean =
      std::accumulate(sorted.begin(), sorted.end(), 0.0) / sorted.size();
  candidate.lower_half = tailMean(sorted, 0.5);
  candidate.cvar25 = tailMean(sorted, 0.25);
  switch (risk) {
    case RiskMode::Mean:
      candidate.utility = candidate.mean;
      break;
    case RiskMode::LowerHalf:
      candidate.utility = candidate.lower_half;
      break;
    case RiskMode::Cvar25:
      candidate.utility = candidate.cvar25;
      break;
    case RiskMode::Blend:
      candidate.utility = 0.65 * candidate.mean + 0.35 * candidate.cvar25;
      break;
  }
}

Decision chooseMoveCanonical(const State& canonical,
                             const PlannerOptions& options,
                             PlannerStats& stats) {
  Decision decision;
  int legal_count = 0;
  const auto legal = drop7::legalColumns(canonical.board, legal_count);
  if (legal_count == 0) return decision;
  const std::uint32_t root_hash = observableHash(canonical);

  for (int offset = 0; offset < legal_count; ++offset) {
    CandidateEvaluation& candidate = decision.candidates[decision.candidate_count++];
    candidate.column = legal[offset];
    candidate.returns.reserve(options.scenarios);
    for (int scenario = 0; scenario < options.scenarios; ++scenario) {
      State simulated = canonical;
      double total = 0;
      bool died = false;
      for (int step = 0; step < options.horizon; ++step) {
        const int column = step == 0
                               ? candidate.column
                               : oneStepMove(simulated, options, stats);
        if (column < 0) {
          died = true;
          total -= options.death_penalty +
                   options.remaining_death_penalty *
                       (options.horizon - step);
          break;
        }
        drop7::Mulberry32 random(rolloutSeed(root_hash, scenario, step));
        MoveResult move;
        if (!drop7::playMove(simulated, column, random, move)) {
          throw std::runtime_error("rollout policy selected an illegal move");
        }
        ++stats.simulated_moves;
        ++stats.root_scenarios;
        total += static_cast<double>(move.score_delta);
        simulated = move.state;
        if (simulated.game_over) {
          died = true;
          total -= options.death_penalty +
                   options.remaining_death_penalty *
                       (options.horizon - step - 1);
          break;
        }
      }
      if (!died) total += options.leaf_scale * phaseUtility(simulated);
      candidate.returns.push_back(total);
    }
    finalizeCandidate(candidate, options.risk);
  }

  const CandidateEvaluation* best = &decision.candidates[0];
  for (int index = 1; index < decision.candidate_count; ++index) {
    const CandidateEvaluation& candidate = decision.candidates[index];
    if (candidate.utility > best->utility + 1e-9 ||
        (std::abs(candidate.utility - best->utility) <= 1e-9 &&
         canonicalTieRank(candidate.column) < canonicalTieRank(best->column))) {
      best = &candidate;
    }
  }
  decision.column = best->column;
  return decision;
}

Decision chooseMove(const State& public_state, const PlannerOptions& options,
                    PlannerStats& stats) {
  // This is the planner's entire interface. There is intentionally no game
  // seed, random tape, callback, or environment RNG parameter.
  bool mirrored = false;
  const State canonical = canonicalState(public_state, mirrored);
  Decision decision = chooseMoveCanonical(canonical, options, stats);
  if (!mirrored || decision.column < 0) return decision;
  decision.column = drop7::kBoardSize - 1 - decision.column;
  for (int index = 0; index < decision.candidate_count; ++index) {
    decision.candidates[index].column =
        drop7::kBoardSize - 1 - decision.candidates[index].column;
  }
  return decision;
}

void validateOptions(const PlannerOptions& options) {
  if (options.horizon < 1 || options.horizon > kMaximumHorizon) {
    throw std::invalid_argument("--horizon must be from 1 to 100");
  }
  if (options.scenarios < 1 || options.scenarios > kMaximumScenarios) {
    throw std::invalid_argument("--scenarios must be from 1 to 64");
  }
  if (options.continuation_samples < 1 ||
      options.continuation_samples > kMaximumContinuationSamples) {
    throw std::invalid_argument("--continuation-samples must be from 1 to 7");
  }
  if (!std::isfinite(options.leaf_scale) || options.leaf_scale <= 0 ||
      !std::isfinite(options.death_penalty) || options.death_penalty <= 0 ||
      !std::isfinite(options.remaining_death_penalty) ||
      options.remaining_death_penalty <= 0) {
    throw std::invalid_argument("planner utility scales must be positive");
  }
}

PlannerOptions parsePlannerOptions(int argc, char** argv) {
  PlannerOptions options;
  options.horizon = parsePositive(
      valueAfter(argc, argv, "--horizon", std::to_string(options.horizon)),
      "--horizon");
  options.scenarios = parsePositive(
      valueAfter(argc, argv, "--scenarios", std::to_string(options.scenarios)),
      "--scenarios");
  options.continuation_samples = parsePositive(
      valueAfter(argc, argv, "--continuation-samples",
                 std::to_string(options.continuation_samples)),
      "--continuation-samples");
  options.risk = parseRisk(valueAfter(argc, argv, "--risk", "blend"));
  options.leaf_scale = parsePositiveDouble(
      valueAfter(argc, argv, "--leaf-scale", std::to_string(options.leaf_scale)),
      "--leaf-scale");
  options.death_penalty = parsePositiveDouble(
      valueAfter(argc, argv, "--death-penalty",
                 std::to_string(options.death_penalty)),
      "--death-penalty");
  options.remaining_death_penalty = parsePositiveDouble(
      valueAfter(argc, argv, "--remaining-death-penalty",
                 std::to_string(options.remaining_death_penalty)),
      "--remaining-death-penalty");
  validateOptions(options);
  return options;
}

RunOptions parseRunOptions(int argc, char** argv) {
  RunOptions options;
  options.games = parsePositive(
      valueAfter(argc, argv, "--games", std::to_string(options.games)),
      "--games");
  options.max_moves = parsePositive(
      valueAfter(argc, argv, "--max-moves", std::to_string(options.max_moves)),
      "--max-moves");
  options.range = valueAfter(argc, argv, "--range", options.range);
  if (options.range == "train") options.seed_start = kTrainingSeedStart;
  else if (options.range == "probe") options.seed_start = kProbeSeedStart;
  else throw std::invalid_argument("--range must be train or probe");
  if (options.games > 256) {
    throw std::invalid_argument("--games is bounded at 256");
  }
  if (options.max_moves > 5000) {
    throw std::invalid_argument("--max-moves is bounded at 5000");
  }
  options.planner = parsePlannerOptions(argc, argv);
  return options;
}

std::uint64_t maximumResidentBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
#endif
}

GameResult runGame(std::uint32_t environment_seed, const RunOptions& options) {
  State state = drop7::initialHeadlessState(environment_seed);
  PlannerStats stats;
  while (!state.game_over && state.moves_played < options.max_moves) {
    // Commit to the move before giving the environment seed to the engine.
    const Decision decision = chooseMove(state, options.planner, stats);
    if (decision.column < 0) {
      throw std::runtime_error("planner found no move in a live game");
    }
    MoveResult move;
    if (!drop7::playHeadlessMove(state, environment_seed, decision.column,
                                 move)) {
      throw std::runtime_error("planner committed an illegal move");
    }
  }
  return {environment_seed, state.score, state.moves_played, state.level,
          !state.game_over, stats.simulated_moves};
}

double percentile(std::vector<std::int64_t> values, double quantile) {
  std::sort(values.begin(), values.end());
  if (values.empty()) return 0;
  const double position = quantile * (values.size() - 1);
  const std::size_t lower = static_cast<std::size_t>(std::floor(position));
  const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
  const double fraction = position - lower;
  return values[lower] * (1.0 - fraction) + values[upper] * fraction;
}

void printIntegerArray(const std::vector<GameResult>& results, bool scores) {
  std::cout << '[';
  for (std::size_t index = 0; index < results.size(); ++index) {
    if (index != 0) std::cout << ',';
    std::cout << (scores ? results[index].score : results[index].moves);
  }
  std::cout << ']';
}

int runBenchmark(int argc, char** argv) {
  const RunOptions options = parseRunOptions(argc, argv);
  std::vector<GameResult> results;
  results.reserve(options.games);
  const auto started = Clock::now();
  for (int game = 0; game < options.games; ++game) {
    const std::uint32_t seed =
        options.seed_start + static_cast<std::uint32_t>(game);
    const auto game_started = Clock::now();
    GameResult result = runGame(seed, options);
    results.push_back(result);
    const double seconds =
        std::chrono::duration<double>(Clock::now() - game_started).count();
    std::cout << "GAME {\"seed\":\"0x" << std::hex << std::setw(8)
              << std::setfill('0') << seed << std::dec << std::setfill(' ')
              << "\",\"score\":" << result.score << ",\"moves\":"
              << result.moves << ",\"level\":" << result.level
              << ",\"censored\":" << (result.censored ? "true" : "false")
              << ",\"simulatedMoves\":" << result.simulated_moves
              << ",\"seconds\":" << std::fixed << std::setprecision(6)
              << seconds << "}\n";
  }
  const double seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  std::vector<std::int64_t> scores;
  scores.reserve(results.size());
  std::int64_t score_sum = 0;
  std::int64_t move_sum = 0;
  std::uint64_t work_sum = 0;
  int censored = 0;
  for (const GameResult& result : results) {
    scores.push_back(result.score);
    score_sum += result.score;
    move_sum += result.moves;
    work_sum += result.simulated_moves;
    if (result.censored) ++censored;
  }
  std::cout << "RESULT {\"range\":\"" << options.range
            << "\",\"seedStart\":\"0x" << std::hex << std::setw(8)
            << std::setfill('0') << options.seed_start << std::dec
            << std::setfill(' ') << "\",\"games\":" << options.games
            << ",\"horizon\":" << options.planner.horizon
            << ",\"scenarios\":" << options.planner.scenarios
            << ",\"continuationSamples\":"
            << options.planner.continuation_samples << ",\"risk\":\""
            << riskName(options.planner.risk) << "\",\"meanScore\":"
            << std::fixed << std::setprecision(3)
            << static_cast<double>(score_sum) / results.size()
            << ",\"medianScore\":" << percentile(scores, 0.5)
            << ",\"p25Score\":" << percentile(scores, 0.25)
            << ",\"minimumScore\":"
            << *std::min_element(scores.begin(), scores.end())
            << ",\"maximumScore\":"
            << *std::max_element(scores.begin(), scores.end())
            << ",\"meanMoves\":"
            << static_cast<double>(move_sum) / results.size()
            << ",\"censored\":" << censored << ",\"seconds\":"
            << seconds << ",\"simulatedMoves\":" << work_sum
            << ",\"simulatedMovesPerSecond\":" << work_sum / seconds
            << ",\"maxRssBytes\":" << maximumResidentBytes()
            << ",\"scores\":";
  printIntegerArray(results, true);
  std::cout << ",\"moves\":";
  printIntegerArray(results, false);
  std::cout << "}\n";
  return 0;
}

State syntheticTestState() {
  State state;
  state.board.fill(drop7::kEmpty);
  state.board[drop7::indexOf(6, 0)] = drop7::kSolid;
  state.board[drop7::indexOf(6, 1)] = drop7::kCracked;
  state.board[drop7::indexOf(5, 1)] = 5;
  state.board[drop7::indexOf(6, 2)] = 6;
  state.board[drop7::indexOf(5, 2)] = 2;
  state.board[drop7::indexOf(6, 3)] = 7;
  state.board[drop7::indexOf(6, 4)] = 4;
  state.next_disc = 3;
  state.moves_remaining = 2;
  return state;
}

bool runSelfTest() {
  PlannerOptions options;
  options.horizon = 3;
  options.scenarios = 3;
  options.continuation_samples = 1;
  validateOptions(options);
  const State state = syntheticTestState();

  PlannerStats first_stats;
  const Decision first = chooseMove(state, options, first_stats);
  PlannerStats repeat_stats;
  const Decision repeat = chooseMove(state, options, repeat_stats);
  if (first.column < 0 || first.column != repeat.column ||
      first_stats.simulated_moves != repeat_stats.simulated_moves) {
    std::cerr << "determinism test failed\n";
    return false;
  }
  for (int index = 0; index < first.candidate_count; ++index) {
    if (first.candidates[index].utility != repeat.candidates[index].utility) {
      std::cerr << "deterministic candidate utility test failed\n";
      return false;
    }
  }

  State altered = state;
  altered.score = 987'654;
  altered.level = 73;
  altered.moves_played = 359;
  PlannerStats altered_stats;
  const Decision seed_blind = chooseMove(altered, options, altered_stats);
  if (seed_blind.column != first.column ||
      altered_stats.simulated_moves != first_stats.simulated_moves) {
    std::cerr << "seed-blind public-state test failed\n";
    return false;
  }
  for (int index = 0; index < first.candidate_count; ++index) {
    if (seed_blind.candidates[index].utility !=
        first.candidates[index].utility) {
      std::cerr << "seed-blind utility test failed\n";
      return false;
    }
  }

  State mirrored = state;
  mirrored.board = mirrorBoard(state.board);
  PlannerStats mirror_stats;
  const Decision mirror_decision = chooseMove(mirrored, options, mirror_stats);
  if (mirror_decision.column != drop7::kBoardSize - 1 - first.column) {
    std::cerr << "mirror-equivariance test failed: " << first.column << " vs "
              << mirror_decision.column << '\n';
    return false;
  }

  bool hash_was_mirrored = false;
  const std::uint32_t hash =
      observableHash(canonicalState(state, hash_was_mirrored));
  for (int step = 0; step < 3; ++step) {
    std::array<bool, 8> strata{};
    for (int scenario = 0; scenario < 7; ++scenario) {
      drop7::Mulberry32 random(rolloutSeed(hash, scenario, step));
      strata[random.nextDisc()] = true;
    }
    for (int disc = 1; disc <= 7; ++disc) {
      if (!strata[disc]) {
        std::cerr << "chance stratification test failed\n";
        return false;
      }
    }
  }

  const std::uint64_t loose_bound =
      static_cast<std::uint64_t>(drop7::kBoardSize) * options.scenarios *
      options.horizon *
      (1u + static_cast<std::uint64_t>(drop7::kBoardSize) *
                options.continuation_samples);
  if (first_stats.simulated_moves == 0 ||
      first_stats.simulated_moves > loose_bound) {
    std::cerr << "bounded-work test failed\n";
    return false;
  }
  if (maximumResidentBytes() == 0) {
    std::cerr << "RSS accounting test failed\n";
    return false;
  }
  std::cout << "SELF_TEST {\"deterministic\":true,\"seedBlind\":true,"
               "\"mirrorEquivariant\":true,\"stratified\":true,"
               "\"boundedWork\":true,\"selectedColumn\":"
            << first.column << ",\"simulatedMoves\":"
            << first_stats.simulated_moves << ",\"maxRssBytes\":"
            << maximumResidentBytes() << "}\n";
  return true;
}

void printUsage() {
  std::cerr
      << "Usage:\n"
      << "  drop7_rollout --self-test\n"
      << "  drop7_rollout --benchmark [--range train|probe] [--games N] "
         "[--max-moves N]\n"
      << "      [--horizon 1..100] [--scenarios 1..64] "
         "[--continuation-samples 1..7]\n"
      << "      [--risk mean|lower|cvar25|blend] [--leaf-scale X] "
         "[--death-penalty X]\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (hasFlag(argc, argv, "--self-test")) return runSelfTest() ? 0 : 1;
    if (hasFlag(argc, argv, "--benchmark")) return runBenchmark(argc, argv);
    printUsage();
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
