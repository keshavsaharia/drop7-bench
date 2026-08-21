#pragma once
// Fast, semantics-preserving reimplementation of the Drop7 mechanics used by
// the fair expectimax search.
//
// EQUIVALENCE CONTRACT.  Everything in this header is integer, memory-layout
// or allocation work.  No floating-point expression is reordered, re-associated
// or re-typed.  The only floating-point values produced here are
//   * scoreForWaveFast, which returns a table entry that is required to be the
//     bit-identical result of floor(7.0 * pow(depth, 2.5)); and
//   * StratifiedRandom-compatible disc draws, whose double arithmetic is copied
//     verbatim from src/core/native/public-behavior.hpp.
// Wave lists, reveal ordering, gravity results, popper order (row major),
// score deltas, level advances and terminal flags are all bit-for-bit the same
// as src/core/native/engine.hpp.
//
// Modifies no existing file; this is an additive header.

#include "../../../src/core/native/public-behavior.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace drop7::fast {

using drop7::Board;
using drop7::MoveResult;
using drop7::State;
using drop7::Wave;

// ---------------------------------------------------------------------------
// O2.  Chain-wave score table.
//
// drop7::scoreForWave calls std::pow(double, 2.5) once per cascade wave.  The
// table below must reproduce floor(7.0 * pow(d, 2.5)) exactly.  gate-leaf
// verifies every entry against the original expression, and any depth beyond
// the table falls back to the original expression.  A real cascade has been
// observed at depth 20, so the table is sized far above anything reachable:
// a single move can produce at most 49 discs' worth of waves plus the rise
// continuation, so depth can never approach 1024.
// ---------------------------------------------------------------------------

inline constexpr int kWaveTableSize = 1024;

// A namespace-scope object, not a function-local static: the hot path is then
// a plain indexed load with no thread-safe-static guard and no libm call.
inline const std::array<std::int64_t, kWaveTableSize> kWaveScores = [] {
  std::array<std::int64_t, kWaveTableSize> values{};
  for (int depth = 1; depth < kWaveTableSize; ++depth) {
    values[static_cast<std::size_t>(depth)] = static_cast<std::int64_t>(
        std::floor(7.0 * std::pow(static_cast<double>(depth), 2.5)));
  }
  return values;
}();

inline std::int64_t scoreForWaveFast(int depth) {
  if (depth < 1) throw std::invalid_argument("chain depth must be positive");
  if (depth < kWaveTableSize) {
    return kWaveScores[static_cast<std::size_t>(depth)];
  }
  return static_cast<std::int64_t>(
      std::floor(7.0 * std::pow(static_cast<double>(depth), 2.5)));
}

// ---------------------------------------------------------------------------
// O2b.  readiness() table.
//
// cfpi::detail::readiness(cost) is ldexp(1.0, 1 - cost) for cost >= 1 and 0
// otherwise.  Every value is an exact power of two, so a table is bit-identical
// by construction; gate-leaf verifies it against std::ldexp over the whole
// reachable range.
// ---------------------------------------------------------------------------

inline constexpr int kReadinessTableSize = 80;

inline const std::array<double, kReadinessTableSize> kReadiness = [] {
  std::array<double, kReadinessTableSize> values{};
  for (int cost = 1; cost < kReadinessTableSize; ++cost) {
    values[static_cast<std::size_t>(cost)] = std::ldexp(1.0, 1 - cost);
  }
  return values;
}();

inline double readinessFast(int cost) {
  if (cost < 1) return 0.0;
  if (cost < kReadinessTableSize) {
    return kReadiness[static_cast<std::size_t>(cost)];
  }
  return std::ldexp(1.0, 1 - cost);
}

inline double unionReadinessFast(double first, double second) {
  return 1.0 - (1.0 - first) * (1.0 - second);
}

// ---------------------------------------------------------------------------
// O3.  Popper detection by run-length extraction.
//
// drop7::findPoppers calls lineLength twice per numbered cell and lineLength
// rescans up to seven cells in each of two directions, so one wave costs on the
// order of 1,300 board reads.  Two run-length sweeps cost 98 reads and produce
// the identical row-major popper list.
// ---------------------------------------------------------------------------

// A board scan produces everything the cascade needs in one pass: the 7-bit
// occupancy mask of each row and column, the bitboard of numbered cells and the
// bitboard of covers.  Run lengths then come from a 128-entry table instead of
// lineLength's directional rescans.
struct BoardScan {
  std::uint8_t row_mask[kBoardSize];
  std::uint8_t column_mask[kBoardSize];
  std::uint64_t numbered = 0;
};

struct RunInfo8 {
  std::uint8_t length[kBoardSize];
};

inline const std::array<RunInfo8, 128> kRunLengthTable = [] {
  std::array<RunInfo8, 128> table{};
  for (int mask = 0; mask < 128; ++mask) {
    RunInfo8& info = table[static_cast<std::size_t>(mask)];
    for (int position = 0; position < kBoardSize; ++position) {
      info.length[position] = 0;
    }
    int cursor = 0;
    while (cursor < kBoardSize) {
      if (((mask >> cursor) & 1) == 0) {
        ++cursor;
        continue;
      }
      const int start = cursor;
      while (cursor < kBoardSize && ((mask >> cursor) & 1) != 0) ++cursor;
      for (int position = start; position < cursor; ++position) {
        info.length[position] = static_cast<std::uint8_t>(cursor - start);
      }
    }
  }
  return table;
}();

// One pass over the board produces the seven row occupancy masks, the seven
// column occupancy masks and the bitboard of numbered cells.  The cover
// bitboard is deliberately NOT produced here: better than half of all move
// applications resolve with no wave at all, and those never need it.
inline void scanBoard(const Board& board, BoardScan& scan) {
  unsigned rows[kBoardSize];
  std::uint64_t numbered = 0;
  for (int row = 0; row < kBoardSize; ++row) {
    const std::uint8_t* cells = board.data() + row * kBoardSize;
    unsigned occupied = 0;
    unsigned numbers = 0;
    for (int column = 0; column < kBoardSize; ++column) {
      const unsigned cell = cells[column];
      occupied |= static_cast<unsigned>(cell != 0u) << column;
      numbers |= static_cast<unsigned>(cell - 1u < 7u) << column;
    }
    rows[row] = occupied;
    scan.row_mask[row] = static_cast<std::uint8_t>(occupied);
    numbered |= static_cast<std::uint64_t>(numbers) << (row * kBoardSize);
  }
  for (int column = 0; column < kBoardSize; ++column) {
    unsigned mask = 0;
    for (int row = 0; row < kBoardSize; ++row) {
      mask |= ((rows[row] >> column) & 1u) << row;
    }
    scan.column_mask[column] = static_cast<std::uint8_t>(mask);
  }
  scan.numbered = numbered;
}

inline std::uint64_t coverBits(const Board& board) {
  std::uint64_t covers = 0;
  for (int row = 0; row < kBoardSize; ++row) {
    const std::uint8_t* cells = board.data() + row * kBoardSize;
    unsigned mask = 0;
    for (int column = 0; column < kBoardSize; ++column) {
      const unsigned cell = cells[column];
      mask |= static_cast<unsigned>(cell == kSolid || cell == kCracked) << column;
    }
    covers |= static_cast<std::uint64_t>(mask) << (row * kBoardSize);
  }
  return covers;
}

// Returns the popper count and fills `poppers` in the identical row-major order
// that drop7::findPoppers produces.
inline int findPoppersScanned(const Board& board, const BoardScan& scan,
                              std::array<int, kCellCount>& poppers) {
  int count = 0;
  for (std::uint64_t remaining = scan.numbered; remaining != 0;
       remaining &= remaining - 1) {
    const int index = __builtin_ctzll(remaining);
    const int row = index / kBoardSize;
    const int column = index - row * kBoardSize;
    const std::uint8_t cell = board[static_cast<std::size_t>(index)];
    if (kRunLengthTable[scan.row_mask[row]].length[column] == cell ||
        kRunLengthTable[scan.column_mask[column]].length[row] == cell) {
      poppers[static_cast<std::size_t>(count++)] = index;
    }
  }
  return count;
}

inline int findPoppersFast(const Board& board,
                           std::array<int, kCellCount>& poppers) {
  BoardScan scan;
  scanBoard(board, scan);
  return findPoppersScanned(board, scan, poppers);
}

// ---------------------------------------------------------------------------
// O4.  Gravity in place.  drop7::applyGravity returns a 49-byte Board by value
// on every wave; the destination index never trails the source index, so the
// compaction is safe to do in the same buffer.
// ---------------------------------------------------------------------------

// Only a column that lost a disc can have a hole; reveals overwrite a cover in
// place and leave the column contiguous.  Compacting just the affected columns
// is therefore identical to compacting all seven.
inline void applyGravityInPlace(Board& board, unsigned columns) {
  while (columns != 0) {
    const int column = __builtin_ctz(columns);
    columns &= columns - 1;
    int destination = kBoardSize - 1;
    for (int row = kBoardSize - 1; row >= 0; --row) {
      const std::uint8_t cell =
          board[static_cast<std::size_t>(row * kBoardSize + column)];
      if (cell == kEmpty) continue;
      board[static_cast<std::size_t>(destination * kBoardSize + column)] = cell;
      --destination;
    }
    for (int row = destination; row >= 0; --row) {
      board[static_cast<std::size_t>(row * kBoardSize + column)] = kEmpty;
    }
  }
}

inline void applyGravityInPlace(Board& board) {
  applyGravityInPlace(board, (1u << kBoardSize) - 1u);
}

inline bool isBoardEmptyFast(const Board& board) {
  std::uint8_t combined = 0;
  for (std::uint8_t cell : board) combined = static_cast<std::uint8_t>(combined | cell);
  return combined == 0;
}

inline int legalColumnCount(const Board& board) {
  int count = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    if (board[static_cast<std::size_t>(column)] == kEmpty) ++count;
  }
  return count;
}

// ---------------------------------------------------------------------------
// O5.  Wave sinks.  The search discards MoveResult::waves entirely; only
// `empty()` and `back().depth` are consulted inside playMove.  A vector is
// therefore a per-node heap allocation for data nobody reads.  Trajectory
// consumers get the full list through FullWaveSink, which stores it inline.
// ---------------------------------------------------------------------------

struct MinimalWaveSink {
  int count = 0;
  int last_depth = 0;

  void push(const Wave& wave) {
    ++count;
    last_depth = wave.depth;
  }
  bool empty() const { return count == 0; }
  int backDepth() const { return last_depth; }
};

// A move can pop at most 49 discs, and every wave clears at least one disc, so
// a single move produces at most 49 waves before the rise and at most 49 after
// it.  128 is a hard upper bound with margin; overflow throws rather than
// silently truncating.
inline constexpr int kMaximumWavesPerMove = 128;

struct FullWaveSink {
  std::array<Wave, kMaximumWavesPerMove> waves;
  int count = 0;

  void push(const Wave& wave) {
    if (count >= kMaximumWavesPerMove) {
      throw std::runtime_error("fast engine: wave capacity exceeded");
    }
    waves[static_cast<std::size_t>(count++)] = wave;
  }
  bool empty() const { return count == 0; }
  int backDepth() const { return waves[static_cast<std::size_t>(count - 1)].depth; }
};

struct FastMoveResult {
  State state{};
  std::int64_t score_delta = 0;
  bool cleared_board = false;
  bool level_advanced = false;
};

// ---------------------------------------------------------------------------
// Cascade and move, templated on the random source and the wave sink.  The
// statement order, the reveal order and the arithmetic are copied verbatim
// from cfpi::detail::resolveCascadeSampled / playMoveSampled.
// ---------------------------------------------------------------------------

template <typename Random, typename Sink>
inline void resolveCascadeFast(Board& board, Random& random, int starting_depth,
                               std::int64_t& score, Sink& sink) {
  std::array<int, kCellCount> poppers;
  BoardScan scan;
  for (int depth = starting_depth;; ++depth) {
    scanBoard(board, scan);
    const int popper_count = findPoppersScanned(board, scan, poppers);
    if (popper_count == 0) return;

    std::uint64_t popping = 0;
    unsigned popped_columns = 0;
    for (int offset = 0; offset < popper_count; ++offset) {
      const int index = poppers[static_cast<std::size_t>(offset)];
      popping |= 1ull << index;
      popped_columns |= 1u << (index % kBoardSize);
    }

    std::array<int, kCellCount> reveals;
    int reveal_count = 0;
    // Cover resolution reads the pre-clear board, so poppers are removed only
    // afterwards; this mirrors engine.hpp's separate `cleared` copy.  Only
    // cover cells can react, so the scan visits exactly those, in the same
    // row-major order.
    for (std::uint64_t remaining = coverBits(board); remaining != 0;
         remaining &= remaining - 1) {
      const int index = __builtin_ctzll(remaining);
      const int row = index / kBoardSize;
      const int column = index - row * kBoardSize;
      int hits = 0;
      if (row > 0) hits += static_cast<int>((popping >> (index - kBoardSize)) & 1ull);
      if (row + 1 < kBoardSize) {
        hits += static_cast<int>((popping >> (index + kBoardSize)) & 1ull);
      }
      if (column > 0) hits += static_cast<int>((popping >> (index - 1)) & 1ull);
      if (column + 1 < kBoardSize) {
        hits += static_cast<int>((popping >> (index + 1)) & 1ull);
      }
      if (hits == 0) continue;
      const std::uint8_t cell = board[static_cast<std::size_t>(index)];
      const int hits_needed = cell == kSolid ? 2 : 1;
      if (hits >= hits_needed) {
        reveals[static_cast<std::size_t>(reveal_count++)] = index;
      } else {
        board[static_cast<std::size_t>(index)] = kCracked;
      }
    }

    for (int offset = 0; offset < popper_count; ++offset) {
      board[static_cast<std::size_t>(poppers[static_cast<std::size_t>(offset)])] =
          kEmpty;
    }
    for (int offset = 0; offset < reveal_count; ++offset) {
      board[static_cast<std::size_t>(reveals[static_cast<std::size_t>(offset)])] =
          random.nextDisc();
    }
    const std::int64_t points =
        static_cast<std::int64_t>(popper_count) * scoreForWaveFast(depth);
    score += points;
    sink.push(Wave{depth, popper_count, reveal_count, points});
    applyGravityInPlace(board, popped_columns);
  }
}

inline bool raiseCoveredRowFast(Board& board) {
  for (int column = 0; column < kBoardSize; ++column) {
    if (board[static_cast<std::size_t>(column)] != kEmpty) return false;
  }
  std::memmove(board.data(), board.data() + kBoardSize,
               static_cast<std::size_t>((kBoardSize - 1) * kBoardSize));
  for (int column = 0; column < kBoardSize; ++column) {
    board[static_cast<std::size_t>((kBoardSize - 1) * kBoardSize + column)] =
        kSolid;
  }
  return true;
}

inline bool placeDiscFast(Board& board, int column, std::uint8_t disc) {
  if (column < 0 || column >= kBoardSize) return false;
  if (board[static_cast<std::size_t>(column)] != kEmpty) return false;
  for (int row = kBoardSize - 1; row >= 0; --row) {
    const std::size_t index = static_cast<std::size_t>(row * kBoardSize + column);
    if (board[index] == kEmpty) {
      board[index] = disc;
      return true;
    }
  }
  return false;
}

template <typename Random, typename Sink>
inline bool playMoveFast(const State& state, int column, Random& random,
                         Sink& sink, FastMoveResult& result) {
  if (state.game_over) return false;
  Board board = state.board;
  if (!placeDiscFast(board, column, state.next_disc)) return false;

  result.score_delta = 0;
  result.cleared_board = false;
  result.level_advanced = false;
  std::int64_t first_score = 0;
  resolveCascadeFast(board, random, 1, first_score, sink);
  result.score_delta = first_score;
  result.cleared_board = isBoardEmptyFast(board);
  if (result.cleared_board) result.score_delta += kClearBonus;

  int level = state.level;
  int moves_remaining = state.moves_remaining - 1;
  bool game_over = false;
  if (moves_remaining == 0) {
    Board raised = board;
    if (!raiseCoveredRowFast(raised)) {
      game_over = true;
    } else {
      result.level_advanced = true;
      ++level;
      moves_remaining = kMovesPerLevel;
      result.score_delta += kLevelBonus;
      board = raised;
      std::int64_t level_score = 0;
      const int next_depth = sink.empty() ? 1 : sink.backDepth() + 1;
      resolveCascadeFast(board, random, next_depth, level_score, sink);
      result.score_delta += level_score;
      if (isBoardEmptyFast(board)) {
        result.score_delta += kClearBonus;
        result.cleared_board = true;
      }
    }
  }

  if (!game_over && legalColumnCount(board) == 0) game_over = true;

  result.state.board = board;
  result.state.next_disc = game_over ? state.next_disc : random.nextDisc();
  result.state.score = state.score + result.score_delta;
  result.state.level = level;
  result.state.moves_remaining = moves_remaining;
  result.state.moves_played = state.moves_played + 1;
  result.state.game_over = game_over;
  return true;
}

// ---------------------------------------------------------------------------
// Real-game driver, bit-identical to drop7::playHeadlessMove.
// ---------------------------------------------------------------------------

inline bool playHeadlessMoveFast(State& state, std::uint32_t game_seed,
                                 int column, FullWaveSink& sink,
                                 FastMoveResult& result) {
  const std::uint32_t reveal_seed =
      mix32(game_seed ^
            (static_cast<std::uint32_t>(state.moves_played + 1) * 0x85eb'ca6bu) ^
            kRevealDomain);
  Mulberry32 random(reveal_seed);
  if (!playMoveFast(state, column, random, sink, result)) return false;
  state = result.state;
  if (!state.game_over) {
    state.next_disc = headlessDisc(game_seed, state.moves_played);
  }
  return true;
}

// ---------------------------------------------------------------------------
// O8.  Canonicalisation without materialising the mirror twice.
// ---------------------------------------------------------------------------

inline int mirrorIndex(int index) {
  const int row = index / kBoardSize;
  const int column = index % kBoardSize;
  return row * kBoardSize + (kBoardSize - 1 - column);
}

inline void mirrorBoardInto(const Board& board, Board& out) {
  for (int row = 0; row < kBoardSize; ++row) {
    const int base = row * kBoardSize;
    for (int column = 0; column < kBoardSize; ++column) {
      out[static_cast<std::size_t>(base + kBoardSize - 1 - column)] =
          board[static_cast<std::size_t>(base + column)];
    }
  }
}

// lexicographical_compare(mirror(board), board) without building mirror(board)
// and without a division per cell.  Column 3 mirrors to itself, and if columns
// 0..2 of a row all match their mirrors then columns 4..6 do too, so only the
// first three columns of each row can ever hold the first difference.
inline bool mirroredIsSmallerFast(const Board& board) {
  for (int row = 0; row < kBoardSize; ++row) {
    const std::uint8_t* cells = board.data() + row * kBoardSize;
    for (int column = 0; column < 3; ++column) {
      const std::uint8_t mirrored = cells[kBoardSize - 1 - column];
      const std::uint8_t original = cells[column];
      if (mirrored != original) return mirrored < original;
    }
  }
  return false;
}

inline State canonicalStateFast(const State& state, bool& mirrored) {
  mirrored = mirroredIsSmallerFast(state.board);
  State result = state;
  result.score = 0;
  if (mirrored) mirrorBoardInto(state.board, result.board);
  return result;
}

}  // namespace drop7::fast
