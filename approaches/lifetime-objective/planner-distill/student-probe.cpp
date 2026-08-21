// student-probe - evaluates the exported student on states read from stdin.
//
//   student-probe model.d7pdst < states.txt
//
// One state per line: 49 cell values, then the visible next disc, then the
// moves until the next rise.  One float per line on stdout: the residual head.
//
// This exists so `parity_student.py` can compare the native path against the
// PyTorch checkpoint without linking anything from PyTorch into the search
// binary.  The native path is the one that plays.

#include "corpus.hpp"
#include "student.hpp"

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: student-probe model.d7pdst < states\n";
    return 2;
  }
  drop7::distill::Student model(argv[1]);
  std::vector<float> scratch(
      static_cast<std::size_t>(model.hidden() + model.mid() + 16));
  std::uint8_t board[drop7::distill::kCells];
  int next_disc = 0;
  int moves_remaining = 0;
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty()) continue;
    std::size_t cursor = 0;
    const auto number = [&]() {
      while (cursor < line.size() && line[cursor] == ' ') ++cursor;
      int value = 0;
      while (cursor < line.size() && line[cursor] >= '0' && line[cursor] <= '9') {
        value = value * 10 + (line[cursor++] - '0');
      }
      return value;
    };
    for (int index = 0; index < drop7::distill::kCells; ++index) {
      board[index] = static_cast<std::uint8_t>(number());
    }
    next_disc = number();
    moves_remaining = number();
    float residual = 0.0f;
    float lifetime = 0.0f;
    model.evaluate(board, next_disc, moves_remaining, scratch.data(), residual,
                   lifetime);
    std::printf("%.7g\n", static_cast<double>(residual));
  }
  return 0;
}
