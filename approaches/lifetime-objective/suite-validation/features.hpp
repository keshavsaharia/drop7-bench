#pragma once

// Candidate structural descriptions of a Drop7 position.
//
// Every quantity here is computed from **public state only**: the visible board,
// the visible next disc, and the moves remaining before the next rise.  None of
// them reads `latent[]`, the disc tape, the score, the level, the move number,
// or the seed.  Every quantity is also invariant under horizontal reflection —
// they are counts, histograms, extrema, and variances over columns — which
// `structure.cpp` asserts with a mirror gate rather than asserting it here in a
// comment.
//
// The question these exist to answer: `finding-06` §3 and `finding-07` §4 show
// that at *matched* board occupancy a clairvoyant planner extracts up to four
// more numbered clears per move than fair D4.  Occupancy therefore does not
// explain the achievable clear rate.  What does?
//
// The 19 features of the frozen leaf are extracted alongside these, unmodified,
// from `drop7::fair_only_horizon::extractFairFeatures`, so that "is this
// property already in the leaf?" is answered by a variance decomposition
// against the leaf's own span rather than by reading the weight table.

#include "../scenario/scenario.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace drop7::suitevalidation {

using namespace drop7;

// ---------------------------------------------------------------------------
// Small board helpers
// ---------------------------------------------------------------------------

inline bool isNumbered(std::uint8_t cell) { return cell >= 1 && cell <= kBoardSize; }
inline bool isCover(std::uint8_t cell) { return cell == kSolid || cell == kCracked; }

inline std::array<int, kBoardSize> heightsOf(const Board& board) {
  std::array<int, kBoardSize> heights{};
  for (int column = 0; column < kBoardSize; ++column) {
    int height = 0;
    for (int row = 0; row < kBoardSize; ++row) {
      if (board[indexOf(row, column)] != kEmpty) ++height;
    }
    heights[column] = height;
  }
  return heights;
}

// The row a disc dropped into `column` would come to rest in, or -1 if the
// column is full.
inline int landingRow(const Board& board, int column) {
  for (int row = kBoardSize - 1; row >= 0; --row) {
    if (board[indexOf(row, column)] == kEmpty) return row;
  }
  return -1;
}

// The run a disc is compared against.  `drop7::lineLength` counts **every
// non-empty cell**, so a gray cover is part of a run exactly like a number is;
// a 3 in the pattern [3][gray][3] pops.  Getting this wrong would invent a
// different game, so the horizontal run is taken from the engine primitive
// itself.
inline int runHorizontal(const Board& board, int row, int column) {
  return lineLength(board, row, column, false);
}

// Gravity keeps every column contiguous from the bottom, so the vertical run
// through any occupied cell is exactly the column height.  This is not an
// approximation: it is forced by `applyGravity`.
inline int runVertical(const std::array<int, kBoardSize>& heights, int column) {
  return heights[column];
}

// The set of discs the first cascade wave would remove after dropping `disc`
// into `column`.  This depends only on the visible board: the first wave is
// fully determined before any cover is revealed, so it is legal for a public
// policy to compute it.
inline int firstWavePoppers(const Board& board, int column, std::uint8_t disc) {
  if (!isLegal(board, column)) return -1;
  Board next = board;
  if (!placeDisc(next, column, disc)) return -1;
  int count = 0;
  findPoppers(next, count);
  return count;
}

// ---------------------------------------------------------------------------
// The candidate feature vector
// ---------------------------------------------------------------------------

struct NamedFeatures {
  std::vector<std::string> names;
  std::vector<double> values;

  void add(const std::string& name, double value) {
    names.push_back(name);
    values.push_back(value);
  }
};

inline NamedFeatures extractStructure(const Board& board, int next_disc,
                                      int moves_remaining) {
  NamedFeatures f;
  const auto heights = heightsOf(board);

  // --- occupancy controls -------------------------------------------------
  int occupied = 0;
  int solid = 0;
  int cracked = 0;
  int numbered = 0;
  std::array<int, kBoardSize + 1> value_count{};
  for (int index = 0; index < kCellCount; ++index) {
    const std::uint8_t cell = board[index];
    if (cell == kEmpty) continue;
    ++occupied;
    if (cell == kSolid) ++solid;
    else if (cell == kCracked) ++cracked;
    else {
      ++numbered;
      ++value_count[cell];
    }
  }
  f.add("occupied", occupied);
  f.add("covered", solid + cracked);
  f.add("solid", solid);
  f.add("cracked", cracked);
  f.add("numbered", numbered);
  f.add("movesRemaining", moves_remaining);
  f.add("nextDisc", next_disc);

  // --- column profile -----------------------------------------------------
  double height_sum = 0.0;
  double height_sq = 0.0;
  int max_height = 0;
  int min_height = kBoardSize;
  double roughness = 0.0;
  int open_columns = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    const int h = heights[column];
    height_sum += h;
    height_sq += static_cast<double>(h) * h;
    max_height = std::max(max_height, h);
    min_height = std::min(min_height, h);
    if (h < kBoardSize) ++open_columns;
    if (column > 0) roughness += std::abs(h - heights[column - 1]);
  }
  const double mean_height = height_sum / kBoardSize;
  f.add("meanHeight", mean_height);
  f.add("heightVariance", height_sq / kBoardSize - mean_height * mean_height);
  f.add("heightRange", max_height - min_height);
  f.add("maxHeight", max_height);
  f.add("roughness", roughness);
  f.add("openColumns", open_columns);

  // --- run-length structure ----------------------------------------------
  // Maximal runs of NON-EMPTY cells, which is what the engine compares a disc's
  // value against.  Vertical runs are column heights by gravity.
  int h_runs = 0;
  double h_run_len_sum = 0.0;
  int max_run = 0;
  std::array<int, kBoardSize + 2> run_hist{};
  for (int row = 0; row < kBoardSize; ++row) {
    int length = 0;
    for (int column = 0; column <= kBoardSize; ++column) {
      const bool filled =
          column < kBoardSize && board[indexOf(row, column)] != kEmpty;
      if (filled) {
        ++length;
      } else if (length > 0) {
        ++h_runs;
        h_run_len_sum += length;
        max_run = std::max(max_run, length);
        ++run_hist[std::min(length, kBoardSize + 1)];
        length = 0;
      }
    }
  }
  int v_runs = 0;
  double v_run_len_sum = 0.0;
  for (int column = 0; column < kBoardSize; ++column) {
    const int h = heights[column];
    if (h == 0) continue;
    ++v_runs;
    v_run_len_sum += h;
    max_run = std::max(max_run, h);
    ++run_hist[std::min(h, kBoardSize + 1)];
  }
  f.add("runsHorizontal", h_runs);
  f.add("runsVertical", v_runs);
  f.add("meanRunHorizontal", h_runs == 0 ? 0.0 : h_run_len_sum / h_runs);
  f.add("meanRunVertical", v_runs == 0 ? 0.0 : v_run_len_sum / v_runs);
  f.add("maxRun", max_run);
  f.add("runsOfOne", run_hist[1]);
  f.add("runsOfTwo", run_hist[2]);
  f.add("runsThreePlus",
        run_hist[3] + run_hist[4] + run_hist[5] + run_hist[6] + run_hist[7]);

  // --- how far is each disc from popping, and in which direction? ---------
  // The start position of a scenario contains no poppable disc by construction,
  // so every disc is either short of its run (it needs discs to ARRIVE) or over
  // it (it needs neighbours to LEAVE).  The two are completely different
  // problems and the frozen leaf prices the second one only for values <= 2
  // (`dead_low_numbers`).
  int one_away = 0;        // (disc, axis) pairs needing exactly one arrival
  int two_away = 0;        // ... exactly two
  int over_length = 0;     // value < run on both axes: needs neighbours to leave
  int over_length_low = 0;    // the same, value <= 2 - the leaf's own domain
  int over_length_high = 0;   // the same, value >= 3 - not in the leaf
  int under_length = 0;    // value > run on both axes: needs arrivals
  double growth_needed = 0.0;   // total arrivals the board's numbers wait for
  double min_distance_sum = 0.0;  // sum over discs of min |value - run|
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      const std::uint8_t cell = board[indexOf(row, column)];
      if (!isNumbered(cell)) continue;
      const int value = cell;
      const int rh = runHorizontal(board, row, column);
      const int rv = runVertical(heights, column);
      if (value == rh + 1) ++one_away;
      if (value == rv + 1) ++one_away;
      if (value == rh + 2) ++two_away;
      if (value == rv + 2) ++two_away;
      if (value < rh && value < rv) {
        ++over_length;
        if (value <= 2) ++over_length_low; else ++over_length_high;
      }
      if (value > rh && value > rv) {
        ++under_length;
        growth_needed += std::min(value - rh, value - rv);
      }
      min_distance_sum += std::min(std::abs(value - rh), std::abs(value - rv));
    }
  }
  f.add("oneAwayPairs", one_away);
  f.add("twoAwayPairs", two_away);
  f.add("overLengthDiscs", over_length);
  f.add("overLengthLow", over_length_low);
  f.add("overLengthHigh", over_length_high);
  f.add("underLengthDiscs", under_length);
  f.add("growthNeeded", growth_needed);
  f.add("minDistanceSum", min_distance_sum);
  f.add("minDistancePerDisc",
        numbered == 0 ? 0.0 : min_distance_sum / numbered);

  // --- value histogram -----------------------------------------------------
  double mean_value = 0.0;
  int low_discs = 0;
  int high_discs = 0;
  for (int index = 0; index < kCellCount; ++index) {
    const std::uint8_t cell = board[index];
    if (!isNumbered(cell)) continue;
    mean_value += cell;
    if (cell <= 2) ++low_discs;
    if (cell >= 5) ++high_discs;
  }
  f.add("meanValue", numbered == 0 ? 0.0 : mean_value / numbered);
  f.add("lowDiscs", low_discs);
  f.add("highDiscs", high_discs);
  // Demand for each value against the room the board has for it: a value v can
  // only ever pop into a run of exactly v, so a board full of 6s and 7s with
  // short runs is structurally different from a board full of 2s and 3s at the
  // same occupancy.
  double value_room_mismatch = 0.0;
  for (int value = 1; value <= kBoardSize; ++value) {
    int slots = 0;
    for (int column = 0; column < kBoardSize; ++column) {
      if (heights[column] <= kBoardSize && value >= heights[column]) ++slots;
    }
    value_room_mismatch += std::abs(value_count[value] - slots);
  }
  f.add("valueRoomMismatch", value_room_mismatch);

  // --- the trigger map: what a uniformly drawn disc could do --------------
  // For each of the seven possible next discs and each legal column, the number
  // of discs the first cascade wave would remove.  Public, deterministic.
  double exp_best_first_wave = 0.0;
  double exp_trigger_columns = 0.0;
  double exp_any_trigger = 0.0;
  int trigger_pairs = 0;
  int trigger_values = 0;
  int best_first_wave_now = 0;
  int trigger_columns_now = 0;
  std::array<bool, kCellCount> ever_pops{};
  for (int value = 1; value <= kBoardSize; ++value) {
    int best = 0;
    int columns_with_trigger = 0;
    for (int column = 0; column < kBoardSize; ++column) {
      const int poppers = firstWavePoppers(board, column, static_cast<std::uint8_t>(value));
      if (poppers <= 0) continue;
      ++trigger_pairs;
      ++columns_with_trigger;
      best = std::max(best, poppers);
      Board next = board;
      placeDisc(next, column, static_cast<std::uint8_t>(value));
      int count = 0;
      const auto list = findPoppers(next, count);
      for (int offset = 0; offset < count; ++offset) ever_pops[list[offset]] = true;
    }
    exp_best_first_wave += best;
    exp_trigger_columns += columns_with_trigger;
    if (columns_with_trigger > 0) {
      ++trigger_values;
      exp_any_trigger += 1.0;
    }
    if (value == next_disc) {
      best_first_wave_now = best;
      trigger_columns_now = columns_with_trigger;
    }
  }
  f.add("expBestFirstWave", exp_best_first_wave / kBoardSize);
  f.add("expTriggerColumns", exp_trigger_columns / kBoardSize);
  f.add("probAnyTrigger", exp_any_trigger / kBoardSize);
  f.add("triggerPairs", trigger_pairs);
  f.add("triggerValues", trigger_values);
  f.add("bestFirstWaveNow", best_first_wave_now);
  f.add("triggerColumnsNow", trigger_columns_now);

  // --- cover adjacency and crackability ------------------------------------
  constexpr std::array<std::array<int, 2>, 4> directions{{
      {{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}},
  }};
  int cover_frontier = 0;       // covers with >= 1 numbered neighbour
  int cover_two_sided = 0;      // covers with >= 2 numbered neighbours
  int cover_buried = 0;         // covers with 0 numbered neighbours
  int covers_in_wave = 0;       // covers adjacent to a cell some drop can pop
  int cracked_in_wave = 0;      // the same, restricted to cracked covers
  double cover_neighbour_sum = 0.0;
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      const int index = indexOf(row, column);
      if (!isCover(board[index])) continue;
      int numbered_neighbours = 0;
      bool in_wave = false;
      for (const auto& direction : directions) {
        const int r = row + direction[0];
        const int c = column + direction[1];
        if (!inside(r, c)) continue;
        const int neighbour = indexOf(r, c);
        if (isNumbered(board[neighbour])) ++numbered_neighbours;
        if (ever_pops[neighbour]) in_wave = true;
      }
      cover_neighbour_sum += numbered_neighbours;
      if (numbered_neighbours >= 1) ++cover_frontier;
      if (numbered_neighbours >= 2) ++cover_two_sided;
      if (numbered_neighbours == 0) ++cover_buried;
      if (in_wave) {
        ++covers_in_wave;
        if (board[index] == kCracked) ++cracked_in_wave;
      }
    }
  }
  const int covers = solid + cracked;
  f.add("coverFrontier", cover_frontier);
  f.add("coverTwoSided", cover_two_sided);
  f.add("coverBuried", cover_buried);
  f.add("coversInWave", covers_in_wave);
  f.add("crackedInWave", cracked_in_wave);
  f.add("coverNeighbourMean", covers == 0 ? 0.0 : cover_neighbour_sum / covers);
  f.add("coversInWaveShare", covers == 0 ? 0.0 : static_cast<double>(covers_in_wave) / covers);

  // --- empty-region connectivity ------------------------------------------
  std::array<int, kCellCount> component{};
  component.fill(-1);
  int components = 0;
  int largest = 0;
  std::vector<int> stack;
  for (int index = 0; index < kCellCount; ++index) {
    if (board[index] != kEmpty || component[index] >= 0) continue;
    const int id = components++;
    int size = 0;
    stack.clear();
    stack.push_back(index);
    component[index] = id;
    while (!stack.empty()) {
      const int current = stack.back();
      stack.pop_back();
      ++size;
      const int row = current / kBoardSize;
      const int column = current % kBoardSize;
      for (const auto& direction : directions) {
        const int r = row + direction[0];
        const int c = column + direction[1];
        if (!inside(r, c)) continue;
        const int neighbour = indexOf(r, c);
        if (board[neighbour] != kEmpty || component[neighbour] >= 0) continue;
        component[neighbour] = id;
        stack.push_back(neighbour);
      }
    }
    largest = std::max(largest, size);
  }
  f.add("emptyComponents", components);
  f.add("largestEmptyComponent", largest);
  f.add("emptyFragmentation",
        components == 0 ? 0.0
                        : static_cast<double>(kCellCount - occupied) / components);

  // --- landing geometry ----------------------------------------------------
  // Where a dropped disc would land relative to the structure it must match.
  int landing_next_to_number = 0;
  int landing_on_cover = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    const int row = landingRow(board, column);
    if (row < 0) continue;
    bool number_neighbour = false;
    for (const auto& direction : directions) {
      const int r = row + direction[0];
      const int c = column + direction[1];
      if (inside(r, c) && isNumbered(board[indexOf(r, c)])) number_neighbour = true;
    }
    if (number_neighbour) ++landing_next_to_number;
    if (row + 1 < kBoardSize && isCover(board[indexOf(row + 1, column)])) {
      ++landing_on_cover;
    }
  }
  f.add("landingNextToNumber", landing_next_to_number);
  f.add("landingOnCover", landing_on_cover);

  return f;
}

}  // namespace drop7::suitevalidation
