// One-shot native decision for the benchmark playground and the competition.
//
//   decide --board <49 digits> --next <1-7> --rise <1-5>
//          [--weights file] [--depth 4] [--chance-samples 7] [--cache 60000]
//
// prints "bestmove <column>" (0-6) or "bestmove none" on a terminal board, and
// exits 0.  The board is the engine's serializeBoard encoding, row-major from
// the top: 0 empty, 1-7 numbered, 8 solid gray, 9 cracked gray -- the same
// string the D7P protocol carries (docs/d7p-protocol.md).
//
// The policy reads exactly the public state: visible board, visible next disc,
// moves until the next rise.  There is no seed, score, level or move number on
// the command line, so there is nothing else it could read.  Decisions are
// deterministic for a given board, weights and configuration, which the
// benchmark harness requires.
//
// With no --weights this is the frozen fast fair-D4 leaf; with a weights file
// it is the same search with that leaf.  The search is the gated
// WeightedFastSearch (see build.sh), so a column chosen here is the column the
// research evaluator would choose on the same public state.

#include "weighted-search.hpp"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace drop7;

std::uint64_t worstCaseWork(int maximumDepth, int strata) {
  const auto branches =
      static_cast<std::uint64_t>(kBoardSize) * static_cast<std::uint64_t>(strata);
  std::uint64_t total = 0;
  for (int depth = 1; depth <= maximumDepth; ++depth) {
    std::uint64_t power = 1;
    for (int level = 1; level <= depth; ++level) {
      power *= branches;
      total += power;
    }
    total += power;
  }
  return total;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    std::string board;
    std::string weightsPath;
    int next = 0;
    int rise = 0;
    fastw::FastSearchParameters parameters;
    parameters.depth = 4;
    parameters.chance_samples = 7;
    parameters.maximum_cache_entries = 60'000;
    for (int i = 1; i < argc; ++i) {
      const std::string key = argv[i];
      if (i + 1 >= argc) throw std::invalid_argument("missing value for " + key);
      const std::string value = argv[++i];
      if (key == "--board") board = value;
      else if (key == "--next") next = std::stoi(value);
      else if (key == "--rise") rise = std::stoi(value);
      else if (key == "--weights") weightsPath = value;
      else if (key == "--depth") parameters.depth = std::stoi(value);
      else if (key == "--chance-samples") parameters.chance_samples = std::stoi(value);
      else if (key == "--cache") parameters.maximum_cache_entries = std::stoull(value);
      else throw std::invalid_argument("unknown option " + key);
    }
    if (board.size() != static_cast<std::size_t>(kCellCount)) {
      throw std::invalid_argument("--board must be 49 characters");
    }
    if (next < 1 || next > kBoardSize) throw std::invalid_argument("--next must be 1-7");
    if (rise < 1 || rise > kMovesPerLevel) throw std::invalid_argument("--rise must be 1-5");
    parameters.maximum_work = worstCaseWork(parameters.depth, parameters.chance_samples) + 1;

    State state;
    for (int cell = 0; cell < kCellCount; ++cell) {
      const char c = board[static_cast<std::size_t>(cell)];
      if (c < '0' || c > '9') throw std::invalid_argument("--board cells must be digits 0-9");
      state.board[static_cast<std::size_t>(cell)] = static_cast<std::uint8_t>(c - '0');
    }
    state.next_disc = static_cast<std::uint8_t>(next);
    state.moves_remaining = rise;
    state.game_over = false;

    fastw::LeafWeights weights;
    if (!weightsPath.empty()) weights = fastw::readWeightsFile(weightsPath);

    fastw::WeightedFastSearch search{parameters, weights};
    fastw::FastSearchMetrics metrics;
    const int action = search.chooseAction(state, metrics);
    if (action < 0 || !isLegal(state.board, action)) {
      std::cout << "bestmove none\n";
    } else {
      std::cout << "bestmove " << action << "\n";
    }
    std::cout << "info depth " << metrics.completed_depth << " work " << metrics.work
              << " nodes " << metrics.nodes << " frozen " << (weights.isFrozen() ? 1 : 0) << "\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "decide failed: " << error.what() << '\n';
    return 2;
  }
}
