// Bit-exact leaf gate.
//
// Compares drop7::fair_only_horizon::fairLeaf against drop7::fast::fastFairLeaf
// as raw uint64 bit patterns -- not as approximately equal doubles -- over the
// distribution of states the search actually evaluates, harvested from real
// games on the SEEDLEASE-A52-FAST block.  Also exhaustively verifies the two
// lookup tables that replaced libm calls.
//
// Any nonzero mismatch count means the fast leaf is a different evaluator and
// therefore a new algorithmic candidate, not an optimisation.

#include "slow-search.hpp"
#include "fast-leaf.hpp"
#include "corpus.hpp"

#include <cstring>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <vector>

namespace {

using namespace drop7;
using namespace drop7::fast;

std::uint64_t bits(double value) {
  std::uint64_t pattern = 0;
  std::memcpy(&pattern, &value, sizeof(pattern));
  return pattern;
}

bool checkWaveTable(std::ostream& out) {
  int mismatches = 0;
  for (int depth = 1; depth < kWaveTableSize; ++depth) {
    const std::int64_t original = drop7::scoreForWave(depth);
    const std::int64_t table = scoreForWaveFast(depth);
    if (original != table) ++mismatches;
  }
  // And beyond the table, where the fallback must still agree.
  for (int depth = kWaveTableSize; depth < kWaveTableSize + 64; ++depth) {
    if (drop7::scoreForWave(depth) != scoreForWaveFast(depth)) ++mismatches;
  }
  out << "wave-score table: depths 1.." << (kWaveTableSize + 63)
      << " checked against floor(7*pow(d,2.5)), " << mismatches
      << " mismatches\n";
  out << "  spot values d=1..6: ";
  for (int depth = 1; depth <= 6; ++depth) out << scoreForWaveFast(depth) << ' ';
  out << " d=20: " << scoreForWaveFast(20) << '\n';
  return mismatches == 0;
}

bool checkReadinessTable(std::ostream& out) {
  int mismatches = 0;
  for (int cost = -64; cost < kReadinessTableSize + 64; ++cost) {
    const double original = cfpi::detail::readiness(cost);
    const double fast = readinessFast(cost);
    if (bits(original) != bits(fast)) ++mismatches;
  }
  out << "readiness table: costs -64.." << (kReadinessTableSize + 63)
      << " bit-compared against ldexp(1.0, 1-cost), " << mismatches
      << " mismatches\n";
  return mismatches == 0;
}

}  // namespace

int main(int argc, char** argv) {
  int games = 24;
  int maximum_moves = 400;
  std::size_t leaf_target = 3'000'000;
  std::size_t per_root = 400;
  int strata = 5;
  for (int index = 1; index + 1 < argc; index += 2) {
    const std::string key = argv[index];
    if (key == "--games") games = std::stoi(argv[index + 1]);
    else if (key == "--max-moves") maximum_moves = std::stoi(argv[index + 1]);
    else if (key == "--leaves") leaf_target = std::stoull(argv[index + 1]);
    else if (key == "--per-root") per_root = std::stoull(argv[index + 1]);
    else if (key == "--strata") strata = std::stoi(argv[index + 1]);
  }

  static_assert(leafweights::kOpenColumnsWeight ==
                drop7::fair_only_horizon::kOpenColumnsWeight);
  static_assert(leafweights::kRisePressureWeight ==
                drop7::fair_only_horizon::kRisePressureWeight);
  static_assert(leafweights::kDangerHeightSquaredWeight ==
                drop7::fair_only_horizon::kDangerHeightSquaredWeight);
  static_assert(leafweights::kFairTerminalUtility ==
                drop7::fair_only_horizon::kFairTerminalUtility);

  std::cout << std::setprecision(17);
  bool ok = true;
  ok &= checkWaveTable(std::cout);
  ok &= checkReadinessTable(std::cout);

  // Real leaf states: play a real game with the frozen depth-4 policy, then
  // expand each root the way the search does and record the depth-0 states.
  std::vector<State> leaves;
  leaves.reserve(leaf_target);
  std::uint64_t roots = 0;
  for (int game = 0; game < games && leaves.size() < leaf_target; ++game) {
    const std::uint32_t seed =
        kLeafCorpusSeeds + static_cast<std::uint32_t>(game);
    requireLease(seed);
    std::vector<State> root_states;
    auto decide = [](const State& state) {
      return ref::chooseDepth4Action(state).action;
    };
    harvestRootStates(seed, maximum_moves, decide, root_states);
    for (const State& root : root_states) {
      if (leaves.size() >= leaf_target) break;
      ++roots;
      bool ignored = false;
      const State canonical = cfpi::detail::canonicalState(root, ignored);
      // Leaves occur at every iterative-deepening ply, so sample plies 1..4,
      // and cap each root so the corpus spans many real positions instead of
      // being one root's subtree.
      for (int ply = 1; ply <= 4; ++ply) {
        const std::size_t cap =
            std::min(leaf_target, leaves.size() + per_root);
        harvestSearchStates(canonical, ply, strata, 0,
                            drop7::fair_only_horizon::kPolicySeed, leaves, cap);
      }
    }
  }

  LeafScratch scratch;
  std::uint64_t mismatches = 0;
  std::uint64_t first_mismatch_index = 0;
  double first_original = 0.0;
  double first_fast = 0.0;
  std::uint64_t domain_violations = 0;
  for (std::size_t index = 0; index < leaves.size(); ++index) {
    const State& state = leaves[index];
    for (std::uint8_t cell : state.board) {
      if (cell > 15) ++domain_violations;
    }
    const double original = drop7::fair_only_horizon::fairLeaf(state);
    const double fast = fastFairLeaf(state, scratch);
    if (bits(original) != bits(fast)) {
      if (mismatches == 0) {
        first_mismatch_index = index;
        first_original = original;
        first_fast = fast;
      }
      ++mismatches;
    }
  }

  // Game-over states take the early return; check that path too.
  State terminal;
  terminal.board = initialBoard();
  terminal.game_over = true;
  const bool terminal_ok =
      bits(drop7::fair_only_horizon::fairLeaf(terminal)) ==
      bits(fastFairLeaf(terminal, scratch));

  std::cout << "leaf corpus: " << roots << " real roots expanded, "
            << leaves.size() << " leaf states compared\n";
  std::cout << "leaf bit-pattern mismatches: " << mismatches << '\n';
  std::cout << "cell-domain violations (cell > 15): " << domain_violations
            << '\n';
  std::cout << "terminal path identical: " << (terminal_ok ? "yes" : "no")
            << '\n';
  if (mismatches > 0) {
    std::cout << "  first mismatch at index " << first_mismatch_index
              << ": frozen " << first_original << " (0x" << std::hex
              << bits(first_original) << std::dec << ") fast " << first_fast
              << " (0x" << std::hex << bits(first_fast) << std::dec << ")\n";
  }
  ok &= mismatches == 0 && terminal_ok && domain_violations == 0;
  std::cout << (ok ? "LEAF GATE PASSED\n" : "LEAF GATE FAILED\n");
  return ok ? 0 : 1;
}
