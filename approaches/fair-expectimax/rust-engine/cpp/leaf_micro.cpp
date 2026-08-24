// Leaf micro-benchmark: tight-loop evaluation of the C++ fast leaf over the
// same harvested root states the Rust bench uses, for a like-for-like
// ns/leaf comparison.
//
//   leaf-micro --roots F --repeats R
//
// Additive file; modifies no existing repository source.

#include "roots.hpp"

#include "src/core/native/engine.hpp"
#include "approaches/lifetime-objective/fast-engine/fast-engine.hpp"
#include "approaches/lifetime-objective/fast-engine/fast-leaf.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace drop7;
using namespace drop7::fast;
using Clock = std::chrono::steady_clock;

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
  std::vector<State> roots;
  if (!readRootsFile(roots_path, roots)) return 2;
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
