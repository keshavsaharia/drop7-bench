#pragma once
// The survival-instinct root filter.
//
// A Drop7 column is packed by gravity, so the vertical run through a disc is
// the column's height: a disc of value n that lands as the (n+1)-th disc or
// higher can never clear vertically while the column stays that tall.  The
// owner's rule: such a placement is a danger and the disc must then be cleared
// horizontally, so the search should refuse to make it when it has a choice.
//
// The filter reads only public state (board, next disc) and is evaluated on
// the pre-cascade placement, which is deterministic: the landing row and the
// horizontal run through the landed disc are known before any chance event.
//
//   kNone    every legal column allowed (the unchanged search)
//   kStrict  refuse a column when the disc would be vertically dead AND its
//            landing row run already exceeds its value (entombed on arrival)
//   kLiteral refuse every vertically dead landing unless it clears on arrival
//            (row run exactly equal to the value)
//
// Values 1 and 2 are exempt: the frozen leaf already prices dead low numbers,
// and a literal rule for 1s would refuse almost every column.  When the rule
// leaves no legal column the caller falls back to the unfiltered search.

#include "fast-engine.hpp"

#include <array>
#include <string>

namespace drop7::survival {

enum class Filter { kNone, kStrict, kLiteral };

inline Filter parseFilter(const std::string& name) {
  if (name == "none") return Filter::kNone;
  if (name == "strict") return Filter::kStrict;
  if (name == "literal") return Filter::kLiteral;
  throw std::invalid_argument("unknown filter " + name);
}

inline const char* filterName(Filter f) {
  switch (f) {
    case Filter::kNone: return "none";
    case Filter::kStrict: return "strict";
    case Filter::kLiteral: return "literal";
  }
  return "?";
}

struct MaskResult {
  std::array<bool, kBoardSize> allowed{};
  int legal = 0;
  int refused = 0;
};

// Row run through (row, column) once a disc occupies that cell, pre-cascade.
inline int landingRowRun(const Board& board, int row, int column) {
  int run = 1;
  for (int c = column - 1; c >= 0 && board[static_cast<std::size_t>(indexOf(row, c))] != kEmpty; --c) ++run;
  for (int c = column + 1; c < kBoardSize && board[static_cast<std::size_t>(indexOf(row, c))] != kEmpty; ++c) ++run;
  return run;
}

inline MaskResult rootMask(const State& state, Filter filter) {
  MaskResult result;
  result.allowed.fill(true);
  const auto heights = cfpi::detail::columnHeights(state.board);
  const int value = state.next_disc;
  for (int column = 0; column < kBoardSize; ++column) {
    if (!isLegal(state.board, column)) continue;
    ++result.legal;
    if (filter == Filter::kNone || value <= 2) continue;
    const int height = heights[static_cast<std::size_t>(column)];
    if (height + 1 <= value) continue;  // can still clear vertically later
    const int row = kBoardSize - 1 - height;  // landing row, 0 = top
    const int run = landingRowRun(state.board, row, column);
    if (run == value) continue;  // clears on arrival
    const bool refuse = filter == Filter::kLiteral || run > value;
    if (refuse) {
      result.allowed[static_cast<std::size_t>(column)] = false;
      ++result.refused;
    }
  }
  return result;
}

}  // namespace drop7::survival
