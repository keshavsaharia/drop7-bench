// Leaf micro-benchmark: tight-loop evaluation of the C++ fast leaf over the
// same harvested root states the Rust bench uses, for a like-for-like
// ns/leaf comparison.
//
//   leaf-micro --roots F --repeats R
//
// Additive file; modifies no existing repository source.

#include "src/core/native/engine.hpp"
#include "approaches/lifetime-objective/fast-engine/fast-engine.hpp"
#include "approaches/lifetime-objective/fast-engine/fast-leaf.hpp"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace drop7;
using namespace drop7::fast;
using Clock = std::chrono::steady_clock;

State parseState(const std::string& line) {
  // s <board49> <next> <mr> <level> <over>
  State state;
  state.score = 0;
  state.moves_played = 0;
  std::string board;
  std::string token;
  std::istringstream in(line.substr(2));
  in >> board;
  for (int index = 0; index < kCellCount; ++index) {
    state.board[static_cast<std::size_t>(index)] =
        static_cast<std::uint8_t>(board[static_cast<std::size_t>(index)] - '0');
  }
  int next = 0;
  int mr = 0;
  int level = 0;
  int over = 0;
  in >> next >> mr >> level >> over;
  state.next_disc = static_cast<std::uint8_t>(next);
  state.moves_remaining = mr;
  state.level = level;
  state.game_over = over != 0;
  return state;
}

}  // namespace

int main(int argc, char** argv) {
  std::string roots_path;
  int repeats = 50;
  for (int index = 1; index + 1 < argc; index += 2) {
    const std::string key = argv[index];
    const std::string value = argv[index + 1];
    if (key == "--roots") roots_path = value;
    else if (key == "--repeats") repeats = std::stoi(value);
  }
  std::ifstream in(roots_path);
  std::vector<State> roots;
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("s ", 0) == 0) roots.push_back(parseState(line));
  }
  LeafScratch scratch;
  double best = 1e18;
  for (int repeat = 0; repeat < repeats; ++repeat) {
    const auto start = Clock::now();
    double acc = 0;
    for (const State& state : roots) acc += fastFairLeaf(state, scratch);
    const double seconds =
        std::chrono::duration<double>(Clock::now() - start).count();
    if (acc == 12345.6789) std::cout << acc;  // keep the work live
    if (seconds < best) best = seconds;
  }
  std::cout << "cpp fast leaf: " << roots.size() << " evals, "
            << (best * 1e9 / roots.size()) << " ns/leaf" << '\n';
  return 0;
}
