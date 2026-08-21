// In-leaf profile: cumulative cost of each stage of the leaf evaluator, on the
// real leaf-state distribution.  Diagnostic only; the stage cut-off is an
// `if constexpr` early return, so the shipped leaf is unaffected.

#include "slow-search.hpp"
#include "fast-leaf.hpp"
#include "corpus.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace drop7;
using namespace drop7::fast;
using Clock = std::chrono::steady_clock;

namespace {
std::vector<State> leaves;
volatile double sink = 0;

template <int kStage>
double timeStage(int reps) {
  LeafScratch scratch;
  const auto start = Clock::now();
  double total = 0;
  for (int rep = 0; rep < reps; ++rep) {
    for (const State& state : leaves) total += fastFairLeaf<kStage>(state, scratch);
  }
  sink = total;
  const double seconds =
      std::chrono::duration<double>(Clock::now() - start).count();
  return seconds * 1e9 / (static_cast<double>(leaves.size()) * reps);
}
}  // namespace

int main(int argc, char** argv) {
  int games = 2;
  int maximum_moves = 40;
  std::size_t target = 20'000;
  int reps = 40;
  for (int index = 1; index + 1 < argc; index += 2) {
    const std::string key = argv[index];
    const std::string value = argv[index + 1];
    if (key == "--games") games = std::stoi(value);
    else if (key == "--max-moves") maximum_moves = std::stoi(value);
    else if (key == "--leaves") target = std::stoull(value);
    else if (key == "--reps") reps = std::stoi(value);
  }
  std::vector<State> roots;
  for (int game = 0; game < games; ++game) {
    auto decide = [](const State& state) {
      return ref::chooseDepth4Action(state).action;
    };
    harvestRootStates(kProfileCorpusSeeds + 0x200u + static_cast<std::uint32_t>(game),
                      maximum_moves, decide, roots);
  }
  for (const State& root : roots) {
    if (leaves.size() >= target) break;
    bool ignored = false;
    const State canonical = cfpi::detail::canonicalState(root, ignored);
    for (int ply = 1; ply <= 4; ++ply) {
      harvestSearchStates(canonical, ply, 5, 0, frozen::kPolicySeed, leaves,
                          std::min(target, leaves.size() + 200));
    }
  }
  std::cout << "leaf corpus " << leaves.size() << " states, load "
            << std::fixed << std::setprecision(2) << loadAverage() << "\n\n";

  // Frozen leaf, for scale.
  {

    const auto start = Clock::now();
    double total = 0;
    for (int rep = 0; rep < reps; ++rep) {
      for (const State& state : leaves) total += frozen::fairLeaf(state);
    }
    sink = total;
    const double ns = std::chrono::duration<double>(Clock::now() - start).count() *
                      1e9 / (static_cast<double>(leaves.size()) * reps);
    std::cout << std::setprecision(1);
    std::cout << "frozen fairLeaf                        " << std::setw(9) << ns
              << " ns\n";
  }
  const double s1 = timeStage<1>(reps);
  const double s2 = timeStage<2>(reps);
  const double s3 = timeStage<3>(reps);
  const double s4 = timeStage<4>(reps);
  const double s5 = timeStage<5>(reps);
  const double s6 = timeStage<6>(reps);
  auto line = [](const char* name, double cumulative, double marginal) {
    std::cout << std::left << std::setw(38) << name << std::right
              << std::setw(9) << cumulative << " ns   (+" << std::setw(7)
              << marginal << ")\n";
  };
  std::cout << std::setprecision(1);
  line("1 masks, heights, rise pressure, danger", s1, s1);
  line("2 + per-cell sweep (+height risks)", s2, s2 - s1);
  line("3 + release inventory", s3, s3 - s2);
  line("4 + adjacent ones", s4, s4 - s3);
  line("5 + runs of twos", s5, s5 - s4);
  line("6 + cover exposure (full leaf)", s6, s6 - s5);
  std::cout << "\n(sink " << sink << ")\n";
  return 0;
}
