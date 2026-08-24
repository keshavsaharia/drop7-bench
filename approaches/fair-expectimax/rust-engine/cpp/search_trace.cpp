// Search-gate emitter: for real root states, prints the fair expectimax
// evaluation in two forms.
//
//   --mode values   per legal column, the reference evaluateAction value at
//                   a fixed depth/strata (uint64 bit pattern), on the
//                   canonical state, plus the chosen (unmirrored) column.
//                   Values are cache-independent (a cache hit returns exactly
//                   what recomputation would produce), so a fresh context per
//                   column is exact.
//   --mode metrics  the C++ FastSearch's full chooseAction metrics: action,
//                   work, nodes, cache hits, completed depth.
//
// The Rust gate (src/bin/gate_search.rs) runs the Rust search on the same
// roots and requires bit-identical values and identical metrics.
//
// Seed discipline: the Rust gate sub-block 0xa5276000-0xa5276fff of the
// already-opened SEEDLEASE-A52-FAST development lease only.
//
// Additive file; modifies no existing repository source.

#include "src/core/native/engine.hpp"
#include "src/core/native/public-behavior.hpp"
#include "approaches/lifetime-objective/fast-engine/fast-engine.hpp"
#include "approaches/lifetime-objective/fast-engine/fast-leaf.hpp"
#include "approaches/lifetime-objective/fast-engine/fast-search.hpp"
#include "approaches/lifetime-objective/fast-engine/corpus.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using namespace drop7;
using namespace drop7::fast;

constexpr std::uint32_t kRustGateSeeds = 0xa527'6000u;

void printState(const State& state) {
  std::cout << "s " << serializeBoard(state.board) << ' '
            << static_cast<int>(state.next_disc) << ' ' << state.moves_remaining
            << ' ' << state.level << ' ' << (state.game_over ? 1 : 0) << '\n';
}

// Fixed-depth value evaluation built from the public fast-engine primitives
// with the fair leaf.  Values are cache-independent (a hit returns exactly
// what recomputation would produce), so the unbounded memo here yields the
// identical values to FastSearch's bounded LRU table; the metrics mode below
// separately confirms action/work/node/hit identity with FastSearch itself.
struct ValueSearch {
  int depth_ = 4;
  int strata_ = 7;
  double terminal_utility = -1'000'000.0;
  std::uint32_t policy_seed = 0xd707'5eedu;
  LeafScratch scratch;
  // The fast engine's own packed-key table at a large capacity.  Values are
  // cache-independent, so this yields the identical values to FastSearch's
  // bounded LRU table at FastSearch's speed.
  TranspositionTable table{200'000};

  double evaluateAction(const State& state, int column, int depth) {
    const std::uint32_t state_seed = cfpi::detail::scenarioSeedForState(
        state, policy_seed, depth);
    double value = 0.0;
    for (int sample = 0; sample < strata_; ++sample) {
      FastStratifiedRandom random{state_seed, sample, strata_, 0};
      MinimalWaveSink sink;
      FastMoveResult move;
      const bool played = playMoveFast(state, column, random, sink, move);
      if (!played) {
        value += terminal_utility;
        continue;
      }
      const double score_delta = static_cast<double>(move.score_delta);
      if (move.state.game_over) {
        value += score_delta + terminal_utility;
        continue;
      }
      move.state.score = 0;
      move.state.next_disc = fastSampledNextDisc(state_seed, sample, strata_);
      bool ignored = false;
      const State next = canonicalStateFast(move.state, ignored);
      value += score_delta + bestFutureValue(next, depth - 1);
    }
    return value / static_cast<double>(strata_);
  }

  double bestFutureValue(const State& state, int depth) {
    if (state.game_over) return terminal_utility;
    if (depth == 0) return fastFairLeaf(state, scratch);
    const PackedKey key = packKey(state, depth);
    const std::uint64_t hash = hashKey(key);
    if (const double* cached = table.lookup(key, hash)) return *cached;
    double best = -std::numeric_limits<double>::infinity();
    for (const int column : cfpi::detail::kColumnOrder) {
      if (!isLegal(state.board, column)) continue;
      best = std::max(best, evaluateAction(state, column, depth));
    }
    if (!std::isfinite(best)) best = terminal_utility;
    table.store(key, hash, best);
    return best;
  }
};

}  // namespace

int main(int argc, char** argv) {
  std::string mode = "values";
  int games = 8;
  int maximum_moves = 120;
  int depth = 4;
  int strata = 7;
  std::size_t limit = 500;
  std::uint32_t seed_start = kRustGateSeeds + 0x900u;
  for (int index = 1; index + 1 < argc; index += 2) {
    const std::string key = argv[index];
    const std::string value = argv[index + 1];
    if (key == "--mode") mode = value;
    else if (key == "--games") games = std::stoi(value);
    else if (key == "--max-moves") maximum_moves = std::stoi(value);
    else if (key == "--depth") depth = std::stoi(value);
    else if (key == "--strata") strata = std::stoi(value);
    else if (key == "--limit") limit = std::stoull(value);
    else if (key == "--seed-start")
      seed_start = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
  }
  if (seed_start < 0xa527'6000u || seed_start > 0xa527'6fffu) {
    std::cerr << "seed range outside the Rust gate sub-block" << '\n';
    return 2;
  }

  // Harvest realistic roots with a depth-3 five-strata helper.
  FastSearchParameters helper_parameters;
  helper_parameters.depth = 3;
  helper_parameters.chance_samples = 5;
  FastSearch helper{helper_parameters};
  auto decide = [&helper](const State& state) {
    FastSearchMetrics metrics;
    return helper.chooseAction(state, metrics);
  };

  std::vector<State> roots;
  for (int game = 0; game < games && roots.size() < limit; ++game) {
    harvestRootStates(seed_start + static_cast<std::uint32_t>(game),
                      maximum_moves, decide, roots);
  }

  if (mode == "values") {
    for (const State& root : roots) {
      if (root.game_over) continue;
      printState(root);
      bool mirrored = false;
      const State canonical = canonicalStateFast(root, mirrored);
      ValueSearch search;
      search.depth_ = depth;
      search.strata_ = strata;
      int chosen = -1;
      double best = -std::numeric_limits<double>::infinity();
      for (const int column : cfpi::detail::kColumnOrder) {
        if (!isLegal(canonical.board, column)) continue;
        const double value = search.evaluateAction(canonical, column, depth);
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof bits);
        std::cout << "c " << column << ' ' << std::hex << bits << std::dec
                  << '\n';
        if (value > best) {
          best = value;
          chosen = column;
        }
      }
      if (mirrored && chosen >= 0) chosen = kBoardSize - 1 - chosen;
      std::cout << "a " << chosen << '\n';
    }
  } else {
    FastSearchParameters parameters;
    parameters.depth = depth;
    parameters.chance_samples = strata;
    parameters.maximum_work = 1ull << 62;
    parameters.maximum_cache_entries = 200'000;
    FastSearch search{parameters};
    for (const State& root : roots) {
      if (root.game_over) continue;
      printState(root);
      FastSearchMetrics metrics;
      search.chooseAction(root, metrics);
      std::cout << "m " << metrics.action << ' ' << metrics.work << ' '
                << metrics.nodes << ' ' << metrics.cache_hits << ' '
                << metrics.completed_depth << '\n';
    }
  }
  std::cerr << "emitted " << roots.size() << " roots" << '\n';
  return 0;
}
