// Leaf-gate emitter: harvests real leaf states the search actually visits
// and prints each state with the uint64 bit pattern of the C++ fast leaf
// value.  The Rust gate (src/bin/gate_leaf.rs) computes its leaf on the same
// states and compares bit patterns exactly.
//
// Two state sources, mirroring how the C++ fast engine was audited:
//   --mode leaves   states harvested at record_depth 0 of a depth-4
//                   five-strata search expansion of real roots (the leaf
//                   distribution the search sees)
//   --mode visited  every root state of real games (broad coverage)
//
// Seed discipline: the Rust gate sub-block 0xa5276000-0xa5276fff of the
// already-opened SEEDLEASE-A52-FAST development lease only.
//
// Additive file; modifies no existing repository source.

#include "src/core/native/engine.hpp"
#include "approaches/lifetime-objective/fast-engine/fast-engine.hpp"
#include "approaches/lifetime-objective/fast-engine/fast-leaf.hpp"
#include "approaches/lifetime-objective/fast-engine/fast-search.hpp"
#include "approaches/lifetime-objective/fast-engine/corpus.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace drop7;
using namespace drop7::fast;

constexpr std::uint32_t kRustGateSeeds = 0xa527'6000u;

void printStateValue(const State& state, LeafScratch& scratch) {
  std::cout << "s " << serializeBoard(state.board) << ' '
            << static_cast<int>(state.next_disc) << ' ' << state.moves_remaining
            << ' ' << state.level << ' ' << (state.game_over ? 1 : 0) << '\n';
  const double value = fastFairLeaf(state, scratch);
  std::uint64_t bits = 0;
  static_assert(sizeof bits == sizeof value);
  std::memcpy(&bits, &value, sizeof bits);
  std::cout << "v " << std::hex << bits << std::dec << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  std::string mode = "leaves";
  int games = 8;
  int maximum_moves = 200;
  std::size_t limit = 200'000;
  std::uint32_t seed_start = kRustGateSeeds + 0x800u;
  for (int index = 1; index + 1 < argc; index += 2) {
    const std::string key = argv[index];
    const std::string value = argv[index + 1];
    if (key == "--mode") mode = value;
    else if (key == "--games") games = std::stoi(value);
    else if (key == "--max-moves") maximum_moves = std::stoi(value);
    else if (key == "--limit") limit = std::stoull(value);
    else if (key == "--seed-start")
      seed_start = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
  }
  if (seed_start < 0xa527'6000u || seed_start > 0xa527'6fffu) {
    std::cerr << "seed range outside the Rust gate sub-block" << '\n';
    return 2;
  }

  LeafScratch scratch;
  std::uint64_t emitted = 0;
  // A depth-3 five-strata FastSearch supplies realistic long-game states for
  // both modes (the same discipline as the fast engine's d3fast gate arm).
  FastSearchParameters helper_parameters;
  helper_parameters.depth = 3;
  helper_parameters.chance_samples = 5;
  FastSearch helper{helper_parameters};
  auto decide = [&helper](const State& state) {
    FastSearchMetrics metrics;
    return helper.chooseAction(state, metrics);
  };
  if (mode == "visited") {
    for (int game = 0; game < games && emitted < limit; ++game) {
      const std::uint32_t seed = seed_start + static_cast<std::uint32_t>(game);
      std::vector<State> roots;
      harvestRootStates(seed, maximum_moves, decide, roots);
      for (const State& state : roots) {
        if (state.game_over) continue;
        printStateValue(state, scratch);
        ++emitted;
      }
    }
  } else {
    for (int game = 0; game < games && emitted < limit; ++game) {
      const std::uint32_t seed = seed_start + static_cast<std::uint32_t>(game);
      std::vector<State> roots;
      harvestRootStates(seed, 60, decide, roots);
      for (const State& root : roots) {
        if (root.game_over) continue;
        std::vector<State> leaves;
        harvestSearchStates(root, 4, 5, 0, 0xd707'5eedu, leaves, 4'000);
        for (const State& state : leaves) {
          if (state.game_over) continue;
          printStateValue(state, scratch);
          ++emitted;
          if (emitted >= limit) break;
        }
        if (emitted >= limit) break;
      }
    }
  }
  std::cerr << "emitted " << emitted << " state/value pairs" << '\n';
  return 0;
}
