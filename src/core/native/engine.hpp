#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace drop7 {

constexpr int kBoardSize = 7;
constexpr int kCellCount = kBoardSize * kBoardSize;
constexpr std::uint8_t kEmpty = 0;
constexpr std::uint8_t kSolid = 8;
constexpr std::uint8_t kCracked = 9;
constexpr int kMovesPerLevel = 5;
// The five-drop numbered-disc Hardcore/Blitz mode awards 17,000. The 7,000
// value used by Normal/Sequence belongs to their longer level cadence.
constexpr std::int64_t kLevelBonus = 17'000;
constexpr std::int64_t kClearBonus = 70'000;
constexpr std::uint32_t kNextDiscDomain = 0x4e45'5854u;
constexpr std::uint32_t kRevealDomain = 0x5245'564cu;

using Board = std::array<std::uint8_t, kCellCount>;

struct State {
  Board board{};
  std::uint8_t next_disc = 1;
  std::int64_t score = 0;
  int level = 1;
  int moves_remaining = kMovesPerLevel;
  int moves_played = 0;
  bool game_over = false;
};

struct Wave {
  int depth = 0;
  int cleared = 0;
  int revealed = 0;
  std::int64_t points = 0;
};

struct MoveResult {
  State state{};
  std::int64_t score_delta = 0;
  std::vector<Wave> waves;
  bool cleared_board = false;
  bool level_advanced = false;
};

inline std::uint32_t mix32(std::uint32_t value) {
  value ^= value >> 16;
  value *= 0x7feb'352du;
  value ^= value >> 15;
  value *= 0x846c'a68bu;
  value ^= value >> 16;
  return value;
}

inline std::uint32_t headlessDiscBits(std::uint32_t seed, int move) {
  return mix32(seed ^
               (static_cast<std::uint32_t>(move + 1) * 0x9e37'79b9u) ^
               kNextDiscDomain);
}

inline std::uint8_t headlessDisc(std::uint32_t seed, int move) {
  return static_cast<std::uint8_t>(
      ((static_cast<std::uint64_t>(headlessDiscBits(seed, move)) * 7u) >> 32) +
      1u);
}

class Mulberry32 {
 public:
  explicit Mulberry32(std::uint32_t seed) : state_(seed) {}

  std::uint32_t nextBits() {
    state_ += 0x6d2b'79f5u;
    std::uint32_t value = state_;
    value = (value ^ (value >> 15)) * (value | 1u);
    value ^= value + (value ^ (value >> 7)) * (value | 61u);
    return value ^ (value >> 14);
  }

  double nextUnit() {
    return static_cast<double>(nextBits()) / 4'294'967'296.0;
  }

  std::uint8_t nextDisc() {
    return static_cast<std::uint8_t>(
        ((static_cast<std::uint64_t>(nextBits()) * 7u) >> 32) + 1u);
  }

 private:
  std::uint32_t state_;
};

inline int indexOf(int row, int column) {
  return row * kBoardSize + column;
}

inline bool inside(int row, int column) {
  return row >= 0 && row < kBoardSize && column >= 0 &&
         column < kBoardSize;
}

inline bool isNumbered(std::uint8_t cell) {
  return cell >= 1 && cell <= 7;
}

inline Board initialBoard() {
  Board board{};
  for (int column = 0; column < kBoardSize; ++column) {
    board[indexOf(kBoardSize - 1, column)] = kSolid;
  }
  return board;
}

inline State initialHeadlessState(std::uint32_t seed) {
  State state;
  state.board = initialBoard();
  state.next_disc = headlessDisc(seed, 0);
  return state;
}

inline std::array<int, kBoardSize> legalColumns(const Board& board,
                                                int& count) {
  std::array<int, kBoardSize> result{};
  count = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    if (board[indexOf(0, column)] == kEmpty) result[count++] = column;
  }
  return result;
}

inline bool isLegal(const Board& board, int column) {
  return column >= 0 && column < kBoardSize &&
         board[indexOf(0, column)] == kEmpty;
}

inline bool placeDisc(Board& board, int column, std::uint8_t disc) {
  if (!isLegal(board, column)) return false;
  for (int row = kBoardSize - 1; row >= 0; --row) {
    const int index = indexOf(row, column);
    if (board[index] == kEmpty) {
      board[index] = disc;
      return true;
    }
  }
  return false;
}

inline int lineLength(const Board& board, int row, int column, bool vertical) {
  if (!inside(row, column) || board[indexOf(row, column)] == kEmpty) return 0;
  const int row_step = vertical ? 1 : 0;
  const int column_step = vertical ? 0 : 1;
  int count = 1;
  for (int direction : {-1, 1}) {
    int next_row = row + row_step * direction;
    int next_column = column + column_step * direction;
    while (inside(next_row, next_column) &&
           board[indexOf(next_row, next_column)] != kEmpty) {
      ++count;
      next_row += row_step * direction;
      next_column += column_step * direction;
    }
  }
  return count;
}

inline std::array<int, kCellCount> findPoppers(const Board& board, int& count) {
  std::array<int, kCellCount> poppers{};
  count = 0;
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      const int index = indexOf(row, column);
      const std::uint8_t cell = board[index];
      if (!isNumbered(cell)) continue;
      if (lineLength(board, row, column, false) == cell ||
          lineLength(board, row, column, true) == cell) {
        poppers[count++] = index;
      }
    }
  }
  return poppers;
}

inline Board applyGravity(const Board& board) {
  Board next{};
  for (int column = 0; column < kBoardSize; ++column) {
    int destination = kBoardSize - 1;
    for (int row = kBoardSize - 1; row >= 0; --row) {
      const std::uint8_t cell = board[indexOf(row, column)];
      if (cell == kEmpty) continue;
      next[indexOf(destination--, column)] = cell;
    }
  }
  return next;
}

inline std::int64_t scoreForWave(int depth) {
  if (depth < 1) throw std::invalid_argument("chain depth must be positive");
  return static_cast<std::int64_t>(
      std::floor(7.0 * std::pow(static_cast<double>(depth), 2.5)));
}

inline bool isBoardEmpty(const Board& board) {
  return std::all_of(board.begin(), board.end(),
                     [](std::uint8_t cell) { return cell == kEmpty; });
}

inline void resolveCascade(Board& board, Mulberry32& random, int starting_depth,
                           std::int64_t& score,
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

    // engine.ts scans the board in row-major order and consumes reveal values
    // before gravity. The ordering is observable through subsequent chains.
    for (int offset = 0; offset < reveal_count; ++offset) {
      cleared[reveals[offset]] = random.nextDisc();
    }
    const std::int64_t points = popper_count * scoreForWave(depth);
    score += points;
    waves.push_back({depth, popper_count, reveal_count, points});
    board = applyGravity(cleared);
  }
}

inline bool raiseCoveredRow(const Board& board, Board& raised) {
  for (int column = 0; column < kBoardSize; ++column) {
    if (board[indexOf(0, column)] != kEmpty) return false;
  }
  raised.fill(kEmpty);
  for (int row = 0; row < kBoardSize - 1; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      raised[indexOf(row, column)] = board[indexOf(row + 1, column)];
    }
  }
  for (int column = 0; column < kBoardSize; ++column) {
    raised[indexOf(kBoardSize - 1, column)] = kSolid;
  }
  return true;
}

inline bool playMove(const State& state, int column, Mulberry32& random,
                     MoveResult& result) {
  if (state.game_over) return false;
  Board board = state.board;
  if (!placeDisc(board, column, state.next_disc)) return false;

  result = MoveResult{};
  std::int64_t first_score = 0;
  resolveCascade(board, random, 1, first_score, result.waves);
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
      // TS starts at firstCascade.waves.length + 1. First-cascade wave depths
      // begin at one, so this is equivalent even when the cascade is empty.
      resolveCascade(board, random, next_depth, level_score, result.waves);
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
  result.state.next_disc = game_over ? state.next_disc : random.nextDisc();
  result.state.score = state.score + result.score_delta;
  result.state.level = level;
  result.state.moves_remaining = moves_remaining;
  result.state.moves_played = state.moves_played + 1;
  result.state.game_over = game_over;
  return true;
}

inline bool playHeadlessMove(State& state, std::uint32_t game_seed, int column,
                             MoveResult& result) {
  const std::uint32_t reveal_seed =
      mix32(game_seed ^
            (static_cast<std::uint32_t>(state.moves_played + 1) *
             0x85eb'ca6bu) ^
            kRevealDomain);
  Mulberry32 random(reveal_seed);
  if (!playMove(state, column, random, result)) return false;
  state = result.state;
  if (!state.game_over) {
    state.next_disc = headlessDisc(game_seed, state.moves_played);
  }
  return true;
}

inline std::string serializeBoard(const Board& board) {
  std::string result;
  result.reserve(kCellCount);
  for (std::uint8_t cell : board) result.push_back(static_cast<char>('0' + cell));
  return result;
}

inline int centerFirstMove(const Board& board) {
  constexpr std::array<int, kBoardSize> order{{3, 2, 4, 1, 5, 0, 6}};
  for (int column : order) {
    if (isLegal(board, column)) return column;
  }
  return -1;
}

}  // namespace drop7
