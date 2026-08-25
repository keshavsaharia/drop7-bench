#pragma once
// Extra leaf terms for the chain-reveal-leaf experiment
// (EX-20260823-reveal-construction-screen-371fd638), implementing
// runs/RUN-20260823T091530Z-cbe65468/kimi-k3-theory-design.md section 2
// (i)-(iii).  The definitions below are FROZEN by that experiment; change them
// only under a new experiment record.
//
// CONTRACT FOR EVERY TERM (the memo in augmented-leaf.hpp depends on it):
//   * public information only: the board and moves_remaining.  The signature
//     takes exactly those (no State), so score, level, moves_played and
//     next_disc reads are unrepresentable.  A term that needs the next disc must be
//     recomputed on a memo hit exactly as next_disc_vertical_options is in
//     fast-engine-memo/memo-leaf.hpp; the memo stores ExtraFeatures per
//     (board, moves_remaining) and would otherwise serve a stale value.
//     gate.cpp --leaf-bits checks memoised == fresh on every visited state.
//   * mirror-invariant: v on a board and on its horizontal mirror must be
//     equal.  gate.cpp --leaf-bits checks this on every visited state.
//   * finite on every reachable board.
//   * `scratch` is the LeafScratch that extractFastLeafFeatures<6> has just
//     filled for this board.  Read it; do not write it.  The per-cell arrays
//     addition[] / release[] are written only for NUMBERED cells (stages 2 and
//     3 of fast-leaf.hpp) and are read here only for numbered cells.
//
// QUANTITIES (all taken from the scratch the frozen leaf computed):
//   r[x]   = unionReadinessFast(scratch.addition[x], scratch.release[x])
//            -- the per-disc "about to pop" readiness the frozen leaf uses for
//            cover exposure (fast-leaf.hpp stage 6).
//   rel[x] = scratch.release[x]
//            -- the RELEASE readiness alone (the per-disc summand of
//            latent_chain_potential, fast-leaf.hpp stage 3): the disc pops
//            because run-mates leave, i.e. the hit arrives as a wave.
//   heights, run table: scratch.heights / scratch.horizontal(row).
//
// SUPPORT-DISJOINT RULE for a pair {a,b} of orthogonal numbered neighbours of
// a cover g (used by aligned_double_hit and chain_to_crack_solid).  A pair is
// SKIPPED when either of these holds:
//   R1 (opposite sides: a and b are both in g's column or both in g's row, so
//       they lie in one contiguous occupied run through g): if a's readiness
//       is release-dominated (release[a] > addition[a]) then a's completion
//       needs a run-mate -- possibly b -- to leave first, which is a different
//       wave; skip.  Symmetrically for b.
//   R2 (adjacent sides: one of a,b is in g's column and the other in g's
//       row): skip if the completion paths P(a) and P(b) share a cell, where
//       P(x) = V(x) | H(x):
//         V(x) = the empty cells of x's column that vertical completion would
//                fill: rows [7 - value, 7 - height - 1] when value > height,
//                else empty;
//         H(x) = the union, over every minimal-cost horizontal window for x
//                (the same windows minimumHorizontalAdditionCostFast scans,
//                with the same edge-blocking and the same cost), of the empty
//                cells each window column must fill to reach x's row; empty
//                when no window exists.
//       Taking the union over all minimal windows (instead of the first) is
//       what makes the rule mirror-invariant.
// The rule is deterministic, reads only the board, and is symmetric in a,b.
//
// danger_gate = 1.0 if max column height <= 4, 0.5 if it is 5, 0.0 if >= 6
// (Kimi's danger = max(0, max_height - 4): 0 -> 1, 1 -> 0.5, >= 2 -> 0).

#include "../fast-engine/fast-leaf.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace drop7::fastx {

constexpr int kExtraTerms = 7;
constexpr const char* kExtraNames[kExtraTerms] = {
    "aligned_double_hit",          // (i)  sum over kSolid g of max pair r[a]*r[b]
    "chain_to_crack_cracked",      // (ii) sum over kCracked g of noisy-OR rel[d]
    "chain_to_crack_solid",        // (ii) sum over kSolid g of max pair rel[a]*rel[b]
    "entombed_high",               // (iii) entombed 3..7 weighted by (1 - r)
    "aligned_double_hit_gated",    // (i)  x danger_gate
    "chain_to_crack_cracked_gated",// (ii) x danger_gate
    "chain_to_crack_solid_gated",  // (ii) x danger_gate
};

struct ExtraFeatures {
  double v[kExtraTerms] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
};

namespace detail {

inline std::uint64_t bit(int row, int column) {
  return 1ull << (row * kBoardSize + column);
}

// Completion path P(x) of the numbered cell at `index` (see header).
inline std::uint64_t completionPath(const Board& board,
                                    const fast::LeafScratch& scratch,
                                    int index) {
  const int row = index / kBoardSize;
  const int column = index % kBoardSize;
  const int value = static_cast<int>(board[static_cast<std::size_t>(index)]);
  const auto& heights = scratch.heights;
  std::uint64_t path = 0;
  // V(x)
  const int height = heights[static_cast<std::size_t>(column)];
  if (value > height) {
    for (int r = kBoardSize - value; r <= kBoardSize - height - 1; ++r) {
      path |= bit(r, column);
    }
  }
  // H(x): same window enumeration as minimumHorizontalAdditionCostFast.
  const int elevation = kBoardSize - row;
  const fast::RunInfo& horizontal = scratch.horizontal(row);
  const int segment_start = horizontal.start[column];
  const int segment_end = horizontal.end[column];
  const int segment_length = horizontal.length[column];
  if (segment_start < 0 || value <= segment_length) return path;
  int best = std::numeric_limits<int>::max();
  std::uint64_t union_of_best = 0;
  const int lowest = std::max(0, segment_end - value + 1);
  const int highest = std::min(segment_start, kBoardSize - value);
  for (int start = lowest; start <= highest; ++start) {
    const int end = start + value - 1;
    if (start > 0 && heights[static_cast<std::size_t>(start - 1)] >= elevation) continue;
    if (end + 1 < kBoardSize && heights[static_cast<std::size_t>(end + 1)] >= elevation) continue;
    int cost = 0;
    std::uint64_t cells = 0;
    for (int c = start; c <= end; ++c) {
      const int h = heights[static_cast<std::size_t>(c)];
      if (h >= elevation) continue;
      cost += elevation - h;
      for (int r = row; r <= kBoardSize - h - 1; ++r) cells |= bit(r, c);
    }
    if (cost <= 0) continue;
    if (cost < best) {
      best = cost;
      union_of_best = cells;
    } else if (cost == best) {
      union_of_best |= cells;
    }
  }
  return path | union_of_best;
}

// Neighbour slots of g in the fixed order up, down, left, right; -1 if absent
// or not numbered.  Sides 0,1 are g's column; 2,3 are g's row.
inline void numberedNeighbours(const fast::LeafScratch& scratch, int index,
                               int out[4]) {
  const int row = index / kBoardSize;
  const int column = index % kBoardSize;
  const auto slot = static_cast<std::size_t>(index);
  out[0] = row > 0 && scratch.present(slot - kBoardSize) ? index - kBoardSize : -1;
  out[1] = row + 1 < kBoardSize && scratch.present(slot + kBoardSize) ? index + kBoardSize : -1;
  out[2] = column > 0 && scratch.present(slot - 1) ? index - 1 : -1;
  out[3] = column + 1 < kBoardSize && scratch.present(slot + 1) ? index + 1 : -1;
}

// True when the pair (side i, side j) of g is support-disjoint (see header).
inline bool supportDisjoint(const Board& board, const fast::LeafScratch& scratch,
                            int i, int j, int a, int b, std::uint64_t path[4]) {
  const bool sameAxis = (i < 2) == (j < 2);
  if (sameAxis) {  // R1
    const auto sa = static_cast<std::size_t>(a), sb = static_cast<std::size_t>(b);
    if (scratch.release[sa] > scratch.addition[sa]) return false;
    if (scratch.release[sb] > scratch.addition[sb]) return false;
    return true;
  }
  // R2
  if (path[i] == ~0ull) path[i] = completionPath(board, scratch, a);
  if (path[j] == ~0ull) path[j] = completionPath(board, scratch, b);
  return (path[i] & path[j]) == 0;
}

// max over support-disjoint neighbour pairs of q[a]*q[b], q = r or rel.
template <typename Quantity>
inline double bestPair(const Board& board, const fast::LeafScratch& scratch,
                       int index, Quantity q) {
  int n[4];
  numberedNeighbours(scratch, index, n);
  std::uint64_t path[4] = {~0ull, ~0ull, ~0ull, ~0ull};
  double best = 0.0;
  for (int i = 0; i < 4; ++i) {
    if (n[i] < 0) continue;
    for (int j = i + 1; j < 4; ++j) {
      if (n[j] < 0) continue;
      if (!supportDisjoint(board, scratch, i, j, n[i], n[j], path)) continue;
      best = std::max(best, q(n[i]) * q(n[j]));
    }
  }
  return best;
}

}  // namespace detail

inline void extractExtraFeatures(const Board& board,
                                 [[maybe_unused]] int moves_remaining,
                                 const fast::LeafScratch& scratch,
                                 ExtraFeatures& out) {
  auto r = [&](int x) {
    const auto s = static_cast<std::size_t>(x);
    return fast::unionReadinessFast(scratch.addition[s], scratch.release[s]);
  };
  auto rel = [&](int x) { return scratch.release[static_cast<std::size_t>(x)]; };

  double aligned = 0.0, crackedChain = 0.0, solidChain = 0.0, entombed = 0.0;
  for (std::uint64_t remaining = scratch.cover_bits; remaining != 0;
       remaining &= remaining - 1) {
    const int index = __builtin_ctzll(remaining);
    if (board[static_cast<std::size_t>(index)] == kSolid) {
      aligned += detail::bestPair(board, scratch, index, r);
      solidChain += detail::bestPair(board, scratch, index, rel);
    } else {
      int n[4];
      detail::numberedNeighbours(scratch, index, n);
      double inverse = 1.0;
      for (int k = 0; k < 4; ++k) {
        if (n[k] >= 0) inverse *= 1.0 - rel(n[k]);
      }
      crackedChain += 1.0 - inverse;
    }
  }
  for (std::uint64_t remaining = scratch.present_bits; remaining != 0;
       remaining &= remaining - 1) {
    const int index = __builtin_ctzll(remaining);
    const int value = static_cast<int>(board[static_cast<std::size_t>(index)]);
    if (value < 3) continue;
    const int row = index / kBoardSize;
    const int column = index % kBoardSize;
    if (scratch.heights[static_cast<std::size_t>(column)] > value &&
        scratch.horizontal(row).length[column] > value) {
      entombed += 1.0 - r(index);
    }
  }
  int maximumHeight = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    maximumHeight = std::max(maximumHeight, scratch.heights[static_cast<std::size_t>(column)]);
  }
  const double gate = maximumHeight <= 4 ? 1.0 : maximumHeight == 5 ? 0.5 : 0.0;

  out.v[0] = aligned;
  out.v[1] = crackedChain;
  out.v[2] = solidChain;
  out.v[3] = entombed;
  out.v[4] = aligned * gate;
  out.v[5] = crackedChain * gate;
  out.v[6] = solidChain * gate;
}

}  // namespace drop7::fastx
