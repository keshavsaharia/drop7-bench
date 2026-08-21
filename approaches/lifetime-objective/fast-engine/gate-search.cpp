// Search equivalence gate.
//
// Three checks, in dependency order.
//
// A. ANCHOR.  drop7::fast::SlowSearch, the parameterized search built only from
//    unmodified frozen primitives, must reproduce
//    drop7::fair_only_depth4::chooseDepth4Action exactly at its default
//    parameters: same action, same logical work, same completed depth, same
//    node count, same cache-hit count, same cache size.  This is what licenses
//    using SlowSearch as the semantic anchor at configurations the frozen
//    binary cannot run (depth 5, seven strata).
//
// B. PARITY.  drop7::fast::FastSearch must match SlowSearch on every move of
//    every probe game, across a grid of (depth, chance strata), on action AND
//    logical work AND completed depth.  Work identity is not optional: work is
//    the currency every comparison in this repository is denominated in, so a
//    version that does less logical work is a different algorithm.
//
// C. DETERMINISM AND REFLECTION.  FastSearch must be repeatable and must map a
//    mirrored position to the mirrored action with the identical work count,
//    matching the frozen self-test's reflection assertion.
//
// Any nonzero mismatch count fails.

#include "slow-search.hpp"
#include "fast-search.hpp"
#include "corpus.hpp"

#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace drop7;
using namespace drop7::fast;

struct GridResult {
  int depth = 0;
  int strata = 0;
  std::uint64_t moves = 0;
  std::uint64_t action_mismatches = 0;
  std::uint64_t work_mismatches = 0;
  std::uint64_t depth_mismatches = 0;
  std::uint64_t slow_work = 0;
  std::uint64_t fast_work = 0;
  std::string first_failure;
};

std::uint64_t workBoundFor(int depth, int strata) {
  const std::uint64_t branches =
      static_cast<std::uint64_t>(kBoardSize) * static_cast<std::uint64_t>(strata);
  std::uint64_t total = 0;
  for (int level = 1; level <= depth; ++level) {
    std::uint64_t power = 1;
    for (int step = 0; step < level; ++step) power *= branches;
    for (int inner = 1; inner <= level; ++inner) {
      std::uint64_t inner_power = 1;
      for (int step = 0; step < inner; ++step) inner_power *= branches;
      total += inner_power;
    }
    total += power;
  }
  return total;
}

bool anchorCheck(std::uint32_t seed_start, int games, int maximum_moves,
                 std::ostream& out) {
  SlowSearch slow{SlowSearchParameters{}};
  std::uint64_t moves = 0;
  std::uint64_t mismatches = 0;
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed = seed_start + static_cast<std::uint32_t>(game);
    requireLease(seed);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < maximum_moves) {
      const ref::SearchDecision reference = ref::chooseDepth4Action(state);
      SlowSearchMetrics metrics;
      slow.chooseAction(state, metrics);
      ++moves;
      if (metrics.action != reference.action ||
          metrics.work != reference.work ||
          metrics.completed_depth != reference.completed_depth ||
          metrics.nodes != reference.nodes ||
          metrics.cache_hits != reference.cache_hits ||
          metrics.cache_entries != reference.cache_entries) {
        ++mismatches;
        if (mismatches == 1) {
          out << "  anchor mismatch seed 0x" << std::hex << seed << std::dec
              << " move " << state.moves_played << ": reference action "
              << reference.action << " work " << reference.work << " depth "
              << reference.completed_depth << " nodes " << reference.nodes
              << " hits " << reference.cache_hits << " entries "
              << reference.cache_entries << " | slow action " << metrics.action
              << " work " << metrics.work << " depth "
              << metrics.completed_depth << " nodes " << metrics.nodes
              << " hits " << metrics.cache_hits << " entries "
              << metrics.cache_entries << '\n';
        }
      }
      MoveResult move;
      if (!playHeadlessMove(state, seed, reference.action, move)) break;
    }
  }
  out << "A. anchor  SlowSearch(default) vs frozen chooseDepth4Action: " << moves
      << " moves compared, " << mismatches
      << " mismatches (action, work, completed depth, nodes, cache hits, cache "
         "entries)\n";
  return mismatches == 0;
}

GridResult parityCheck(int depth, int strata, std::uint32_t seed_start,
                       int games, int maximum_moves) {
  GridResult result;
  result.depth = depth;
  result.strata = strata;
  const std::uint64_t bound = workBoundFor(depth, strata) + 1;
  SlowSearchParameters slow_parameters;
  slow_parameters.depth = depth;
  slow_parameters.chance_samples = strata;
  slow_parameters.maximum_work = bound;
  slow_parameters.maximum_cache_entries = 200'000;
  FastSearchParameters fast_parameters;
  fast_parameters.depth = depth;
  fast_parameters.chance_samples = strata;
  fast_parameters.maximum_work = bound;
  fast_parameters.maximum_cache_entries = 200'000;
  SlowSearch slow{slow_parameters};
  FastSearch fast{fast_parameters};
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed = seed_start + static_cast<std::uint32_t>(game);
    requireLease(seed);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < maximum_moves) {
      SlowSearchMetrics slow_metrics;
      FastSearchMetrics fast_metrics;
      slow.chooseAction(state, slow_metrics);
      fast.chooseAction(state, fast_metrics);
      ++result.moves;
      result.slow_work += slow_metrics.work;
      result.fast_work += fast_metrics.work;
      bool failed = false;
      if (slow_metrics.action != fast_metrics.action) {
        ++result.action_mismatches;
        failed = true;
      }
      if (slow_metrics.work != fast_metrics.work) {
        ++result.work_mismatches;
        failed = true;
      }
      if (slow_metrics.completed_depth != fast_metrics.completed_depth) {
        ++result.depth_mismatches;
        failed = true;
      }
      if (failed && result.first_failure.empty()) {
        result.first_failure =
            "seed 0x" + std::to_string(seed) + " move " +
            std::to_string(state.moves_played) + ": slow(action " +
            std::to_string(slow_metrics.action) + ", work " +
            std::to_string(slow_metrics.work) + ", depth " +
            std::to_string(slow_metrics.completed_depth) + ") fast(action " +
            std::to_string(fast_metrics.action) + ", work " +
            std::to_string(fast_metrics.work) + ", depth " +
            std::to_string(fast_metrics.completed_depth) + ")";
      }
      MoveResult move;
      if (!playHeadlessMove(state, seed, slow_metrics.action, move)) break;
    }
  }
  return result;
}

bool isHorizontallySymmetric(const Board& board) {
  return board == cfpi::detail::mirrorBoard(board);
}

bool reflectionAndDeterminism(std::uint32_t seed_start, int games,
                              int maximum_moves, std::ostream& out) {
  FastSearchParameters parameters;
  FastSearch first{parameters};
  FastSearch second{parameters};
  FastSearch mirrored_search{parameters};
  SlowSearch slow_mirrored{SlowSearchParameters{}};
  std::uint64_t moves = 0;
  std::uint64_t repeat_mismatches = 0;
  std::uint64_t mirror_equivalence_mismatches = 0;
  std::uint64_t asymmetric = 0;
  std::uint64_t reflection_mismatches = 0;
  std::uint64_t symmetric = 0;
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed = seed_start + static_cast<std::uint32_t>(game);
    requireLease(seed);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < maximum_moves) {
      FastSearchMetrics one;
      FastSearchMetrics two;
      first.chooseAction(state, one);
      second.chooseAction(state, two);
      if (one.action != two.action || one.work != two.work ||
          one.nodes != two.nodes || one.cache_hits != two.cache_hits) {
        ++repeat_mismatches;
      }
      State mirrored = state;
      mirrored.board = cfpi::detail::mirrorBoard(state.board);
      FastSearchMetrics reflected;
      mirrored_search.chooseAction(mirrored, reflected);
      // Equivalence question: does the fast search answer the mirrored
      // position exactly as the frozen-primitive search does?
      SlowSearchMetrics slow_reflected;
      slow_mirrored.chooseAction(mirrored, slow_reflected);
      if (reflected.action != slow_reflected.action ||
          reflected.work != slow_reflected.work ||
          reflected.completed_depth != slow_reflected.completed_depth) {
        ++mirror_equivalence_mismatches;
      }
      // Policy property, reported separately: on a horizontally symmetric
      // board the mirror IS the board, so the deterministic tie-break returns
      // the same column rather than its reflection.  The frozen reference
      // behaves identically; its self-test uses an asymmetric fixture.
      if (isHorizontallySymmetric(state.board)) {
        ++symmetric;
      } else {
        ++asymmetric;
        if (reflected.action != kBoardSize - 1 - one.action ||
            reflected.work != one.work) {
          ++reflection_mismatches;
        }
      }
      ++moves;
      MoveResult move;
      if (!playHeadlessMove(state, seed, one.action, move)) break;
    }
  }
  out << "C. determinism: " << moves << " moves, " << repeat_mismatches
      << " repeat mismatches\n";
  out << "   mirror equivalence (fast vs slow on the mirrored position): "
      << mirror_equivalence_mismatches << " mismatches\n";
  out << "   reflection property on " << asymmetric
      << " asymmetric boards (action == 6 - action AND identical work): "
      << reflection_mismatches << " mismatches; " << symmetric
      << " symmetric boards excluded, where the frozen reference also returns "
         "the unreflected column\n";
  return repeat_mismatches == 0 && mirror_equivalence_mismatches == 0 &&
         reflection_mismatches == 0;
}

}  // namespace

int main(int argc, char** argv) {
  int anchor_games = 4;
  int anchor_moves = 60;
  int parity_games = 3;
  int parity_moves = 40;
  int reflect_games = 2;
  int reflect_moves = 30;
  std::string grid = "3x5,3x7,4x5,4x7,5x5";
  for (int index = 1; index + 1 < argc; index += 2) {
    const std::string key = argv[index];
    const std::string value = argv[index + 1];
    if (key == "--anchor-games") anchor_games = std::stoi(value);
    else if (key == "--anchor-moves") anchor_moves = std::stoi(value);
    else if (key == "--parity-games") parity_games = std::stoi(value);
    else if (key == "--parity-moves") parity_moves = std::stoi(value);
    else if (key == "--reflect-games") reflect_games = std::stoi(value);
    else if (key == "--reflect-moves") reflect_moves = std::stoi(value);
    else if (key == "--grid") grid = value;
  }

  bool ok = true;
  ok &= anchorCheck(kSearchParitySeeds, anchor_games, anchor_moves, std::cout);

  std::cout << "\nB. parity  FastSearch vs SlowSearch across the (depth, "
               "strata) grid\n";
  std::cout << "  depth strata      moves   action  work  depth  "
               "slow work/move  fast work/move\n";
  std::size_t cursor = 0;
  while (cursor < grid.size()) {
    const std::size_t comma = grid.find(',', cursor);
    const std::string item =
        grid.substr(cursor, comma == std::string::npos ? std::string::npos
                                                       : comma - cursor);
    cursor = comma == std::string::npos ? grid.size() : comma + 1;
    const std::size_t split = item.find('x');
    if (split == std::string::npos) continue;
    const int depth = std::stoi(item.substr(0, split));
    const int strata = std::stoi(item.substr(split + 1));
    const GridResult result = parityCheck(
        depth, strata, kSearchParitySeeds + 0x100u, parity_games, parity_moves);
    std::cout << "  " << std::setw(5) << result.depth << std::setw(7)
              << result.strata << std::setw(11) << result.moves << std::setw(9)
              << result.action_mismatches << std::setw(6)
              << result.work_mismatches << std::setw(7)
              << result.depth_mismatches << std::setw(16)
              << (result.moves ? result.slow_work / result.moves : 0)
              << std::setw(16)
              << (result.moves ? result.fast_work / result.moves : 0) << '\n';
    if (!result.first_failure.empty()) {
      std::cout << "    first failure: " << result.first_failure << '\n';
    }
    ok &= result.action_mismatches == 0 && result.work_mismatches == 0 &&
          result.depth_mismatches == 0 && result.moves > 0;
  }

  std::cout << '\n';
  ok &= reflectionAndDeterminism(kSearchParitySeeds + 0x200u, reflect_games,
                                 reflect_moves, std::cout);

  std::cout << (ok ? "SEARCH GATE PASSED\n" : "SEARCH GATE FAILED\n");
  return ok ? 0 : 1;
}
