#pragma once
// The frozen fair leaf plus K weighted extra terms, behind the one-entry
// board memo of approaches/lifetime-objective/fast-engine-memo/memo-leaf.hpp.
//
// EQUIVALENCE CONTRACT.  augmentedFairLeaf computes `result` exactly as
// fastm::fastFairLeafMemo does -- the same feature extraction, the same
// memo key (board, moves_remaining), the same next_disc_vertical_options
// recomputation from stored heights, the same dot product in the frozen
// order with the frozen constants -- and only THEN adds `w[i] * v[i]` for
// every extra term whose weight is not exactly zero.  A zero-weight term is
// skipped, not multiplied: `result + 0.0 * v` is the identity on finite
// inputs, but the contract does not rely on that argument, it relies on the
// instruction never being emitted.  At all-zero weights the returned double is
// therefore the same uint64 bit pattern as fast::fastFairLeaf on every board,
// which gate.cpp --leaf-bits checks on the boards a real search visits.
//
// The memo adds no state the search reads; it sits below the search's ++work_
// (build.sh checks that), so logical work, completed depth and cache
// behaviour are unchanged whatever the weights are.
//
// This header modifies no existing repository file.

#include "../fast-engine/fast-leaf.hpp"
#include "extra-terms.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>

namespace drop7::fastx {

struct ExtraWeights {
  double w[kExtraTerms] = {};  // all zero: the frozen arm

  bool isFrozen() const {
    for (int i = 0; i < kExtraTerms; ++i) {
      if (w[i] != 0.0) return false;
    }
    return true;
  }

  // "name=value,name=value"; unnamed terms stay zero; an empty string is the
  // frozen arm.  Unknown names and non-finite values are errors so that a typo
  // cannot silently mean "frozen".
  static ExtraWeights parse(const std::string& text) {
    ExtraWeights weights;
    std::size_t cursor = 0;
    while (cursor < text.size()) {
      const std::size_t comma = text.find(',', cursor);
      const std::string item =
          text.substr(cursor, comma == std::string::npos ? std::string::npos
                                                         : comma - cursor);
      cursor = comma == std::string::npos ? text.size() : comma + 1;
      if (item.empty()) continue;
      const std::size_t equals = item.find('=');
      if (equals == std::string::npos) {
        throw std::invalid_argument("extra weight without '=': " + item);
      }
      const std::string name = item.substr(0, equals);
      const int index = indexOf(name);
      if (index < 0) throw std::invalid_argument("unknown extra term " + name);
      std::size_t consumed = 0;
      const double value = std::stod(item.substr(equals + 1), &consumed);
      if (consumed != item.size() - equals - 1 || !std::isfinite(value)) {
        throw std::invalid_argument("bad value for extra term " + name);
      }
      weights.w[index] = value;
    }
    return weights;
  }

  static int indexOf(const std::string& name) {
    for (int i = 0; i < kExtraTerms; ++i) {
      if (name == kExtraNames[i]) return i;
    }
    return -1;
  }

  // Canonical "name=value,..." over every term, in term order, for artifacts.
  std::string describe() const {
    std::ostringstream out;
    out.precision(17);
    for (int i = 0; i < kExtraTerms; ++i) {
      if (i) out << ',';
      out << kExtraNames[i] << '=' << w[i];
    }
    return out.str();
  }
};

// memo-leaf.hpp's LeafMemo plus the extra features of the same board.
struct AugmentedMemo {
  bool valid = false;
  Board board{};
  int moves_remaining = 0;
  std::array<int, kBoardSize> heights{};
  fast::FastLeafFeatures features;
  ExtraFeatures extra;
  std::uint64_t calls = 0;
  std::uint64_t hits = 0;
};

inline double augmentedFairLeaf(const State& state, fast::LeafScratch& scratch,
                                AugmentedMemo& memo,
                                const ExtraWeights& weights) {
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
    extractExtraFeatures(state.board, state.moves_remaining, scratch, memo.extra);
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
  // Extra terms, after the frozen sum and only when the weight is non-zero,
  // so the zero-weight arm emits no floating-point instruction past this
  // line.  Term order is the kExtraNames order.
  for (int i = 0; i < kExtraTerms; ++i) {
    if (weights.w[i] != 0.0) result += weights.w[i] * memo.extra.v[i];
  }
  return result;
}

}  // namespace drop7::fastx
