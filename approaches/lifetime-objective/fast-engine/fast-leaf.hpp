#pragma once
// Bit-exact fast reimplementation of drop7::fair_only_horizon::fairLeaf.
//
// EQUIVALENCE CONTRACT.  Every floating-point expression below is copied
// character-for-character from src/core/native/public-behavior.hpp
// (extractPhaseFeatures) and approaches/fair-expectimax/reference/
// fair-only-horizon.cpp (extractFairFeatures, fairLeaf), in the same order,
// with the same accumulators and the same intermediate types.  Nothing is
// re-associated, vectorised or promoted.  The differences are exclusively:
//
//   L1  the eleven PhaseFeatures that fairLeaf never reads are not computed
//       (audit-02 M5).  Their producers are removed wholesale, which is safe
//       because each writes only into its own accumulator:
//         placementInventory      -> trigger_readiness, quiet_build_options,
//                                    quiet_direct_gain
//         raiseCoveredRow+findPoppers -> rise_trigger_readiness
//         the low-cap column loop -> low_cap_load, adjacent_low_cap_load
//         cover_altitude_debt     -> cover_altitude_debt,
//                                    imminent_cover_altitude_debt
//         the rise-urgency block  -> projected_occupancy_debt,
//                                    residual_cover_debt, peak_height_risk
//       Together these are three std::pow calls and one full popper scan per
//       leaf.  The `occupied`, `covers` and interior `maximum_height` counters
//       exist only to feed them and go with them.
//   L2  readiness()'s std::ldexp becomes an exact power-of-two table lookup.
//   L3  the two std::vector<double> support buffers, allocated and freed once
//       per numbered disc per leaf, become stack arrays.  releaseReadiness
//       reads one order statistic of the sorted values, which is a property of
//       the multiset and not of the sort, so this is value-identical.
//   L4  DiscAnalysis becomes a struct of arrays and LineAnalysis' six 49-entry
//       int arrays become a 128-entry run-length table, cutting the per-leaf
//       zeroing from ~5.5 kB to nothing.
//   L6  the fair-only horizon's own column loop and board sweep have the same
//       bounds and order as this evaluator's, and disjoint accumulators, so
//       they are folded in; two whole board passes disappear.
//
//   L7  kRoughnessWeight is 0.0, so `roughness` was computed on every leaf and
//       multiplied by zero.  Both the feature and the term are removed; the
//       argument that this is bit-exact is written out at the removal site and
//       checked empirically by gate-leaf.
//
// gate-leaf compares the uint64 bit pattern of this function against the
// frozen one over millions of real boards.

#include "fast-engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>

namespace drop7::fast {

// Frozen leaf constants, re-declared so this header does not depend on the
// reference translation unit.  gate-leaf static_asserts them against the
// originals.
namespace leafweights {
inline constexpr double kFairTerminalUtility = -2'500'000.0;
inline constexpr double kOpenColumnsWeight = 180.0;
inline constexpr double kHeightLoadWeight = -20.0;
inline constexpr double kSolidCellsWeight = -620.0;
inline constexpr double kCrackedCellsWeight = -220.0;
inline constexpr double kNumberedCellsWeight = -18.0;
inline constexpr double kHighLowNumbersWeight = -90.0;
inline constexpr double kDirectPotentialWeight = 1'600.0;
inline constexpr double kLatentChainPotentialWeight = 700.0;
inline constexpr double kCrackedExposureWeight = 100.0;
inline constexpr double kSolidExposureWeight = 40.0;
inline constexpr double kAdjacentOnesWeight = -550.0;
inline constexpr double kTripleTwosWeight = -750.0;
inline constexpr double kDeadLowNumbersWeight = -120.0;
inline constexpr double kCoveredHeightRiskWeight = -95.0;
inline constexpr double kLowNumberHeightRiskWeight = -85.0;
inline constexpr double kDangerHeightSquaredWeight = -1'250.0;
// kRoughnessWeight = 0.0 is intentionally absent: see L7 above.
inline constexpr double kRisePressureWeight = -35.0;
inline constexpr double kNextDiscVerticalOptionsWeight = 220.0;
}  // namespace leafweights

// ---------------------------------------------------------------------------
// L5  Run-length table.
//
// A row (or column) of the board is seven cells, so its occupancy is a 7-bit
// pattern with only 128 possibilities.  The run length, run start and run end
// of every position follow from that pattern alone, so analyzeLines' two
// scanning sweeps and its six 49-entry int arrays collapse into two 7-entry
// mask arrays and a 2,688-byte lookup table.
// ---------------------------------------------------------------------------

struct RunInfo {
  std::int8_t length[kBoardSize];
  std::int8_t start[kBoardSize];
  std::int8_t end[kBoardSize];
};

inline const std::array<RunInfo, 128> kRunTable = [] {
  std::array<RunInfo, 128> table{};
  for (int mask = 0; mask < 128; ++mask) {
    RunInfo& info = table[static_cast<std::size_t>(mask)];
    for (int position = 0; position < kBoardSize; ++position) {
      info.length[position] = 0;
      info.start[position] = -1;
      info.end[position] = -1;
    }
    int cursor = 0;
    while (cursor < kBoardSize) {
      if (((mask >> cursor) & 1) == 0) {
        ++cursor;
        continue;
      }
      const int run_start = cursor;
      while (cursor < kBoardSize && ((mask >> cursor) & 1) != 0) ++cursor;
      const int run_end = cursor - 1;
      for (int position = run_start; position <= run_end; ++position) {
        info.length[position] = static_cast<std::int8_t>(run_end - run_start + 1);
        info.start[position] = static_cast<std::int8_t>(run_start);
        info.end[position] = static_cast<std::int8_t>(run_end);
      }
    }
  }
  return table;
}();

struct LeafScratch {
  std::array<int, kBoardSize> heights{};
  std::array<std::uint8_t, kBoardSize> row_mask{};     // occupancy per row
  std::array<std::uint8_t, kBoardSize> column_mask{};  // occupancy per column
  std::array<std::uint8_t, kBoardSize> twos_row{};
  std::array<std::uint8_t, kBoardSize> twos_column{};
  std::uint64_t present_bits = 0;   // numbered 1..7
  std::uint64_t cover_bits = 0;     // solid or cracked
  std::uint64_t ones_bits = 0;
  std::array<double, kCellCount> addition{};
  std::array<double, kCellCount> release{};
  std::array<double, kCellCount> horizontal_addition{};
  std::array<double, kCellCount> vertical_addition{};
  std::array<double, kCellCount> horizontal_release{};
  std::array<double, kCellCount> vertical_release{};

  const RunInfo& horizontal(int row) const {
    return kRunTable[row_mask[static_cast<std::size_t>(row)]];
  }
  const RunInfo& vertical(int column) const {
    return kRunTable[column_mask[static_cast<std::size_t>(column)]];
  }
  bool present(std::size_t index) const {
    return (present_bits >> index) & 1u;
  }
};

// Same search as minimumHorizontalAdditionCost, with the inner accumulation
// replaced by a difference of the row's integer prefix sums of
// max(0, elevation - height).  Integer arithmetic only, so the returned value
// is identical.
inline double minimumHorizontalAdditionCostFast(
    int value, int segment_start, int segment_end, int segment_length,
    const std::array<int, kBoardSize>& heights, int elevation,
    const std::array<int, kBoardSize + 1>& prefix) {
  if (segment_start < 0 || value <= segment_length) return -1.0;
  int best = std::numeric_limits<int>::max();
  const int lowest = std::max(0, segment_end - value + 1);
  const int highest = std::min(segment_start, kBoardSize - value);
  for (int start = lowest; start <= highest; ++start) {
    const int end = start + value - 1;
    if (start > 0 && heights[static_cast<std::size_t>(start - 1)] >= elevation) {
      continue;
    }
    if (end + 1 < kBoardSize &&
        heights[static_cast<std::size_t>(end + 1)] >= elevation) {
      continue;
    }
    const int cost = prefix[static_cast<std::size_t>(end + 1)] -
                     prefix[static_cast<std::size_t>(start)];
    if (cost > 0) best = std::min(best, cost);
  }
  return best == std::numeric_limits<int>::max() ? -1.0
                                                 : static_cast<double>(best);
}

// Descending insertion sort over at most seven doubles.  std::sort orders by
// value, so any correct sort yields the identical sequence and the identical
// order statistic; this one avoids the generic sort's setup cost.
inline void sortDescending(double* values, int count) {
  for (int index = 1; index < count; ++index) {
    const double key = values[index];
    int position = index - 1;
    while (position >= 0 && values[position] < key) {
      values[position + 1] = values[position];
      --position;
    }
    values[position + 1] = key;
  }
}

inline double releaseReadinessFast(int excess, double* support, int count) {
  if (excess <= 0 || count < excess) return 0;
  sortDescending(support, count);
  return support[excess - 1] * readinessFast(excess);
}

// The thirteen PhaseFeatures fairLeaf actually reads, plus the six FairFeatures
// terms.  Field order matches the report order in audit-02 section 3.
struct FastLeafFeatures {
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
  double covered_height_risk = 0;
  double low_number_height_risk = 0;
  double danger_height_squared = 0;
  double rise_pressure = 0;
  double next_disc_vertical_options = 0;
};

// kStopAfter exists only for the in-leaf profile in leafprofile.cpp: it is an
// `if constexpr` early return, so the default instantiation compiles to exactly
// the code it would without the parameter.  Stages:
//   1 occupancy masks, heights, rise pressure, danger height
//   2 per-cell sweep: direct potential, height load, covered height risk,
//     low-number height risk
//   3 release inventory      4 adjacent ones      5 runs of twos
//   6 cover exposure (the fair-only horizon terms are folded into 1 and 2)
template <int kStopAfter = 6>
inline void extractFastLeafFeatures(const State& state, LeafScratch& scratch,
                                    FastLeafFeatures& features) {
  if (state.moves_remaining < 1 || state.moves_remaining > kMovesPerLevel ||
      state.next_disc < 1 || state.next_disc > kBoardSize) {
    throw std::invalid_argument("invalid public state for fair evaluator");
  }
  const Board& board = state.board;

  // --- stage 1: occupancy masks, heights, open columns ---------------------
  std::uint8_t column_mask[kBoardSize] = {0, 0, 0, 0, 0, 0, 0};
  for (int row = 0; row < kBoardSize; ++row) {
    const std::uint8_t* cells = board.data() + row * kBoardSize;
    unsigned mask = 0;
    for (int column = 0; column < kBoardSize; ++column) {
      const unsigned occupied = cells[column] != kEmpty ? 1u : 0u;
      mask |= occupied << column;
      column_mask[column] =
          static_cast<std::uint8_t>(column_mask[column] | (occupied << row));
    }
    scratch.row_mask[static_cast<std::size_t>(row)] =
        static_cast<std::uint8_t>(mask);
  }
  auto& heights = scratch.heights;
  int maximum_height = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    scratch.column_mask[static_cast<std::size_t>(column)] = column_mask[column];
    const int height = __builtin_popcount(column_mask[column]);
    heights[static_cast<std::size_t>(column)] = height;
    if (board[static_cast<std::size_t>(column)] == kEmpty) {
      ++features.open_columns;
    }
    // L6: the fair-only horizon's own column loop had identical bounds and
    // order, so folding it here removes a whole pass without touching the
    // order of any accumulation.
    maximum_height = std::max(maximum_height, height);
    features.rise_pressure +=
        static_cast<double>(height * height * height) / state.moves_remaining;
    if (height < kBoardSize && height + 1 == state.next_disc) {
      features.next_disc_vertical_options += 1.0;
    }
  }
  const int danger = std::max(0, maximum_height - 4);
  features.danger_height_squared = danger * danger;
  scratch.present_bits = 0;
  scratch.cover_bits = 0;
  scratch.ones_bits = 0;
  scratch.twos_row.fill(0);
  scratch.twos_column.fill(0);

  if constexpr (kStopAfter < 2) return;

  // --- stage 2: per-cell sweep ---------------------------------------------
  for (int row = 0; row < kBoardSize; ++row) {
    const int elevation = kBoardSize - row;
    std::array<int, kBoardSize + 1> prefix{};
    for (int column = 0; column < kBoardSize; ++column) {
      prefix[static_cast<std::size_t>(column + 1)] =
          prefix[static_cast<std::size_t>(column)] +
          std::max(0, elevation - heights[static_cast<std::size_t>(column)]);
    }
    const RunInfo& horizontal = scratch.horizontal(row);
    for (int column = 0; column < kBoardSize; ++column) {
      const auto index = static_cast<std::size_t>(row * kBoardSize + column);
      const std::uint8_t cell = board[index];
      if (cell == kEmpty) continue;
      features.height_load += elevation * elevation;
      // L6: the fair-only horizon's second board sweep had the identical
      // row-major bounds, and covered_height_risk / low_number_height_risk are
      // separate accumulators from everything here, so folding them into this
      // sweep removes a pass and preserves the order within each sum exactly.
      const double edge_multiplier =
          column == 0 || column == kBoardSize - 1 ? 1.65 : 1.0;
      if (cell == kSolid || cell == kCracked) {
        if (cell == kSolid) {
          ++features.solid_cells;
          features.covered_height_risk += elevation * elevation * edge_multiplier;
        } else {
          ++features.cracked_cells;
          features.covered_height_risk +=
              elevation * elevation * edge_multiplier * 0.72;
        }
        scratch.cover_bits |= 1ull << index;
        continue;
      }
      if (!isNumbered(cell)) continue;
      ++features.numbered_cells;
      if (cell <= 2) {
        const int height_risk = std::max(0, elevation - 2);
        features.low_number_height_risk += height_risk * height_risk;
        if (elevation >= 5) ++features.high_low_numbers;
      }
      scratch.present_bits |= 1ull << index;
      if (cell == 1) {
        scratch.ones_bits |= 1ull << index;
      } else if (cell == 2) {
        scratch.twos_row[static_cast<std::size_t>(row)] =
            static_cast<std::uint8_t>(scratch.twos_row[static_cast<std::size_t>(row)] |
                                      (1u << column));
        scratch.twos_column[static_cast<std::size_t>(column)] =
            static_cast<std::uint8_t>(
                scratch.twos_column[static_cast<std::size_t>(column)] |
                (1u << row));
      }
      const int height = heights[static_cast<std::size_t>(column)];
      const double vertical_addition =
          cell > height ? readinessFast(static_cast<int>(cell) - height) : 0.0;
      const double horizontal_cost = minimumHorizontalAdditionCostFast(
          cell, horizontal.start[column], horizontal.end[column],
          horizontal.length[column], heights, elevation, prefix);
      const double horizontal_addition =
          horizontal_cost < 0 ? 0.0
                              : readinessFast(static_cast<int>(horizontal_cost));
      const double addition =
          unionReadinessFast(horizontal_addition, vertical_addition);
      scratch.vertical_addition[index] = vertical_addition;
      scratch.horizontal_addition[index] = horizontal_addition;
      scratch.addition[index] = addition;
      features.direct_potential += addition;
    }
  }

  if constexpr (kStopAfter < 3) return;

  // --- stage 3: release inventory ------------------------------------------
  for (std::uint64_t remaining = scratch.present_bits; remaining != 0;
       remaining &= remaining - 1) {
    const int index = __builtin_ctzll(remaining);
    const auto slot = static_cast<std::size_t>(index);
    const int row = index / kBoardSize;
    const int column = index % kBoardSize;
    const RunInfo& horizontal = scratch.horizontal(row);
    const RunInfo& vertical = scratch.vertical(column);
    double horizontal_support[kBoardSize];
    double vertical_support[kBoardSize];
    int horizontal_count = 0;
    int vertical_count = 0;
    for (int scan = horizontal.start[column]; scan <= horizontal.end[column];
         ++scan) {
      const auto supporter = static_cast<std::size_t>(row * kBoardSize + scan);
      if (supporter != slot && scratch.present(supporter)) {
        horizontal_support[horizontal_count++] = scratch.addition[supporter];
      }
    }
    for (int scan = vertical.start[row]; scan <= vertical.end[row]; ++scan) {
      const auto supporter = static_cast<std::size_t>(scan * kBoardSize + column);
      if (supporter != slot && scratch.present(supporter)) {
        vertical_support[vertical_count++] = scratch.addition[supporter];
      }
    }
    const int value = static_cast<int>(board[slot]);
    const double horizontal_release = releaseReadinessFast(
        horizontal.length[column] - value, horizontal_support,
        horizontal_count);
    const double vertical_release = releaseReadinessFast(
        vertical.length[row] - value, vertical_support, vertical_count);
    const double release =
        unionReadinessFast(horizontal_release, vertical_release);
    scratch.horizontal_release[slot] = horizontal_release;
    scratch.vertical_release[slot] = vertical_release;
    scratch.release[slot] = release;
    features.latent_chain_potential += release;
    if (value <= 2 && horizontal.length[column] > value &&
        vertical.length[row] > value) {
      features.dead_low_numbers +=
          1.0 - unionReadinessFast(scratch.addition[slot], release);
    }
  }

  if constexpr (kStopAfter < 4) return;

  // --- stage 4: adjacent ones ----------------------------------------------
  for (std::uint64_t remaining = scratch.ones_bits; remaining != 0;
       remaining &= remaining - 1) {
    const int index = __builtin_ctzll(remaining);
    const auto slot = static_cast<std::size_t>(index);
    const int row = index / kBoardSize;
    const int column = index % kBoardSize;
    if (column + 1 < kBoardSize && board[slot + 1] == 1) {
      const double escape =
          std::max(unionReadinessFast(scratch.vertical_addition[slot],
                                      scratch.vertical_release[slot]),
                   unionReadinessFast(scratch.vertical_addition[slot + 1],
                                      scratch.vertical_release[slot + 1]));
      features.adjacent_ones += 1.0 - escape;
    }
    if (row + 1 < kBoardSize && board[slot + kBoardSize] == 1) {
      const double escape = std::max(
          unionReadinessFast(scratch.horizontal_addition[slot],
                             scratch.horizontal_release[slot]),
          unionReadinessFast(scratch.horizontal_addition[slot + kBoardSize],
                             scratch.horizontal_release[slot + kBoardSize]));
      features.adjacent_ones += 1.0 - escape;
    }
  }

  if constexpr (kStopAfter < 5) return;

  // --- stage 5: runs of twos ------------------------------------------------
  for (int row = 0; row < kBoardSize; ++row) {
    unsigned mask = scratch.twos_row[static_cast<std::size_t>(row)];
    while (mask != 0) {
      const int start = __builtin_ctz(mask);
      unsigned run = mask >> start;
      int length = 0;
      while ((run & 1u) != 0) {
        ++length;
        run >>= 1;
      }
      const int excess = length - 2;
      if (excess > 0) {
        double escape = 0;
        for (int column = start; column < start + length; ++column) {
          const auto slot = static_cast<std::size_t>(row * kBoardSize + column);
          escape = std::max(escape,
                            unionReadinessFast(scratch.vertical_addition[slot],
                                               scratch.vertical_release[slot]));
        }
        features.triple_twos += excess * excess * (1.0 - escape);
      }
      mask &= ~(((1u << length) - 1u) << start);
    }
  }
  for (int column = 0; column < kBoardSize; ++column) {
    unsigned mask = scratch.twos_column[static_cast<std::size_t>(column)];
    while (mask != 0) {
      const int start = __builtin_ctz(mask);
      unsigned run = mask >> start;
      int length = 0;
      while ((run & 1u) != 0) {
        ++length;
        run >>= 1;
      }
      const int excess = length - 2;
      if (excess > 0) {
        double escape = 0;
        for (int row = start; row < start + length; ++row) {
          const auto slot = static_cast<std::size_t>(row * kBoardSize + column);
          escape = std::max(
              escape, unionReadinessFast(scratch.horizontal_addition[slot],
                                         scratch.horizontal_release[slot]));
        }
        features.triple_twos += excess * excess * (1.0 - escape);
      }
      mask &= ~(((1u << length) - 1u) << start);
    }
  }

  if constexpr (kStopAfter < 6) return;

  // --- stage 6: cover exposure ---------------------------------------------
  for (std::uint64_t remaining = scratch.cover_bits; remaining != 0;
       remaining &= remaining - 1) {
    const int index = __builtin_ctzll(remaining);
    const auto slot = static_cast<std::size_t>(index);
    const int row = index / kBoardSize;
    const int column = index % kBoardSize;
    double support[4];
    int count = 0;
    // Neighbour order {-1,0},{1,0},{0,-1},{0,1} as in the reference; the values
    // are sorted immediately afterwards, so only the multiset matters, but the
    // order is preserved anyway.
    if (row > 0 && scratch.present(slot - kBoardSize)) {
      support[count++] = unionReadinessFast(scratch.addition[slot - kBoardSize],
                                            scratch.release[slot - kBoardSize]);
    }
    if (row + 1 < kBoardSize && scratch.present(slot + kBoardSize)) {
      support[count++] = unionReadinessFast(scratch.addition[slot + kBoardSize],
                                            scratch.release[slot + kBoardSize]);
    }
    if (column > 0 && scratch.present(slot - 1)) {
      support[count++] = unionReadinessFast(scratch.addition[slot - 1],
                                            scratch.release[slot - 1]);
    }
    if (column + 1 < kBoardSize && scratch.present(slot + 1)) {
      support[count++] = unionReadinessFast(scratch.addition[slot + 1],
                                            scratch.release[slot + 1]);
    }
    sortDescending(support, count);
    if (board[slot] == kCracked) {
      double inverse = 1;
      for (int offset = 0; offset < count; ++offset) {
        inverse *= 1.0 - support[offset];
      }
      features.cracked_exposure += 1.0 - inverse;
    } else {
      features.solid_exposure += (count > 0 ? support[0] * 0.35 : 0.0) +
                                 (count > 1 ? support[1] * 0.65 : 0.0);
    }
  }

}

template <int kStopAfter = 6>
inline double fastFairLeaf(const State& state, LeafScratch& scratch) {
  using namespace leafweights;
  if (state.game_over) return kFairTerminalUtility;
  FastLeafFeatures f;
  extractFastLeafFeatures<kStopAfter>(state, scratch, f);
  // Preserve the TypeScript dot-product order for parity.
  double result = 0.0;
  result += kOpenColumnsWeight * f.open_columns;
  result += kHeightLoadWeight * f.height_load;
  result += kSolidCellsWeight * f.solid_cells;
  result += kCrackedCellsWeight * f.cracked_cells;
  result += kNumberedCellsWeight * f.numbered_cells;
  result += kHighLowNumbersWeight * f.high_low_numbers;
  result += kDirectPotentialWeight * f.direct_potential;
  result += kLatentChainPotentialWeight * f.latent_chain_potential;
  result += kCrackedExposureWeight * f.cracked_exposure;
  result += kSolidExposureWeight * f.solid_exposure;
  result += kAdjacentOnesWeight * f.adjacent_ones;
  result += kTripleTwosWeight * f.triple_twos;
  result += kDeadLowNumbersWeight * f.dead_low_numbers;
  result += kCoveredHeightRiskWeight * f.covered_height_risk;
  result += kLowNumberHeightRiskWeight * f.low_number_height_risk;
  result += kDangerHeightSquaredWeight * f.danger_height_squared;
  // L7: kRoughnessWeight is 0.0 and `roughness` is a sum of six absolute
  // integer height differences, so it is finite on every board and
  // 0.0 * roughness is exactly +0.0.  `result` starts at +0.0 and can never
  // become -0.0 (round-to-nearest returns +0.0 for any exact cancellation, and
  // +0.0 + -0.0 is +0.0), so `result += +0.0` is the identity and the whole
  // term -- feature and multiply -- is removed.  gate-leaf checks the
  // consequence directly by comparing bit patterns.
  // result += kRoughnessWeight * f.roughness;
  result += kRisePressureWeight * f.rise_pressure;
  result += kNextDiscVerticalOptionsWeight * f.next_disc_vertical_options;
  return result;
}

}  // namespace drop7::fast
