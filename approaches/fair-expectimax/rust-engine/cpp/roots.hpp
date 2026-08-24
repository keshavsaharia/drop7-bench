// Shared reader for the harvested-roots corpus the C++ benchmark arms time
// against.  Both benchmarks read the SAME file the Rust arms read, so a
// silently mis-parsed record would not fail loudly -- it would produce a
// benchmark number computed from a state that never occurred.  Every field is
// therefore validated, and any malformed or empty corpus is a hard error
// rather than a timing.
//
// Record grammar, as emitted by leaf_trace.cpp and search_trace.cpp:
//
//   s <board49> <next> <moves-remaining> <level> <game-over>
//
// Additive file; modifies no existing repository source.
#pragma once

#include "src/core/native/engine.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace drop7 {

// Parses one `s ` record.  Returns false, leaving `out` untouched, if the
// board token is not exactly kCellCount cells, if any cell is outside the
// 0-9 encoding (0 empty, 1-7 numbered, 8 solid, 9 cracked), if any trailing
// field is missing or non-numeric, or if a field is outside its legal range.
inline bool parseRootRecord(const std::string& line, State& out) {
  std::istringstream in(line.substr(2));
  std::string board;
  int next = 0;
  int moves_remaining = 0;
  int level = 0;
  int over = 0;
  // A single extraction chain: a truncated record fails here rather than
  // leaving a field silently at zero.
  if (!(in >> board >> next >> moves_remaining >> level >> over)) return false;
  if (board.size() != static_cast<std::size_t>(kCellCount)) return false;
  if (next < 1 || next > kBoardSize) return false;
  if (moves_remaining < 0 || moves_remaining > kMovesPerLevel) return false;
  if (level < 1) return false;
  if (over != 0 && over != 1) return false;

  State state;
  for (int index = 0; index < kCellCount; ++index) {
    const unsigned digit = static_cast<unsigned>(
        static_cast<unsigned char>(board[static_cast<std::size_t>(index)]) - '0');
    if (digit > kCracked) return false;
    state.board[static_cast<std::size_t>(index)] =
        static_cast<std::uint8_t>(digit);
  }
  state.next_disc = static_cast<std::uint8_t>(next);
  state.moves_remaining = moves_remaining;
  state.level = level;
  state.game_over = over != 0;
  state.score = 0;
  state.moves_played = 0;
  out = state;
  return true;
}

// Reads every `s ` record from `path` into `roots`.  Returns false, after
// naming the problem on stderr, if the file cannot be opened, if any record
// is malformed, or if the corpus holds no roots -- an empty corpus would
// otherwise divide a timing by zero and report an infinite or NaN result
// while exiting successfully.
inline bool readRootsFile(const std::string& path, std::vector<State>& roots) {
  std::ifstream in(path);
  if (!in) {
    std::cerr << "cannot read roots file " << path << '\n';
    return false;
  }
  std::string line;
  int number = 0;
  while (std::getline(in, line)) {
    ++number;
    if (line.rfind("s ", 0) != 0) continue;
    State state;
    if (!parseRootRecord(line, state)) {
      std::cerr << path << ':' << number << ": malformed root record" << '\n';
      return false;
    }
    roots.push_back(state);
  }
  if (roots.empty()) {
    std::cerr << "no roots in " << path << '\n';
    return false;
  }
  return true;
}

}  // namespace drop7
