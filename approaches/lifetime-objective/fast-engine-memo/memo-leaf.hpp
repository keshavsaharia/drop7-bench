#pragma once
// One-entry board memo for the fast fair leaf (audit-06, executive item 1).
//
// Inside one chance node the search evaluates the same move under five or
// seven sampled futures.  Most of those futures leave the board identical
// (a cascade consumed no reveal draw) and differ only in the sampled next
// disc, which the leaf reads in exactly one term, next_disc_vertical_options
// (fast-leaf.hpp:273-274).  Every other feature is a function of
// (board, moves_remaining).  So: remember the last board's features and
// column heights; on a key match recompute that one term from the stored
// heights (same column order, its own accumulator) and re-run the dot product
// in the frozen order.  Identical inputs give identical doubles, so the value
// is bit-identical to fastFairLeaf on every call, hit or miss.
//
// EQUIVALENCE CONTRACT.  This header adds no state the search reads and
// changes no random event, no logical work (the caller increments work_
// before calling the leaf; the memo must stay below that line), no completed
// depth and no selected column.  It is an implementation detail of the same
// class as finding-13's allocation-free rewrite; it has no capacity and
// therefore no eviction policy that could influence any recorded observable.
// gate.cpp checks bit identity, search parity and determinism.

#include "../fast-engine/fast-leaf.hpp"

#include <cstdint>
#include <cstring>

namespace drop7::fastm {

struct LeafMemo {
  bool valid = false;
  Board board{};
  int moves_remaining = 0;
  std::array<int, kBoardSize> heights{};
  fast::FastLeafFeatures features;
  std::uint64_t calls = 0;
  std::uint64_t hits = 0;
};

inline double fastFairLeafMemo(const State& state, fast::LeafScratch& scratch,
                               LeafMemo& memo) {
  using namespace fast::leafweights;
  if (state.game_over) return kFairTerminalUtility;
  if (state.moves_remaining < 1 || state.moves_remaining > kMovesPerLevel ||
      state.next_disc < 1 || state.next_disc > kBoardSize) {
    throw std::invalid_argument("invalid public state for fair evaluator");
  }
  ++memo.calls;
  if (!memo.valid || memo.moves_remaining != state.moves_remaining ||
      std::memcmp(memo.board.data(), state.board.data(), kCellCount) != 0) {
    fast::FastLeafFeatures fresh;
    fast::extractFastLeafFeatures<6>(state, scratch, fresh);
    memo.features = fresh;
    memo.board = state.board;
    memo.moves_remaining = state.moves_remaining;
    memo.heights = scratch.heights;
    memo.valid = true;
  } else {
    ++memo.hits;
  }
  fast::FastLeafFeatures f = memo.features;
  // fast-leaf.hpp:271-275, same column order and its own accumulator.
  f.next_disc_vertical_options = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    const int height = memo.heights[static_cast<std::size_t>(column)];
    if (height < kBoardSize && height + 1 == state.next_disc) {
      f.next_disc_vertical_options += 1.0;
    }
  }
  // fast-leaf.hpp:536-561, the frozen dot-product order.
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
  result += kRisePressureWeight * f.rise_pressure;
  result += kNextDiscVerticalOptionsWeight * f.next_disc_vertical_options;
  return result;
}

}  // namespace drop7::fastm
