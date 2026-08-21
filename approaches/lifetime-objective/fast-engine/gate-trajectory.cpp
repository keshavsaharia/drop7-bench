// Trajectory equivalence gate.
//
// Plays the same games twice, move for move, through the unmodified
// drop7::playHeadlessMove and through drop7::fast::playHeadlessMoveFast, and
// compares everything observable: board, next disc, score, score delta, the
// full wave list (depth, cleared, revealed, points), level advance, board
// clear, moves remaining, moves played and the terminal flag.
//
// Several policies are used, because a deterministic policy and a searching
// policy visit different parts of the state space:
//   --policy center   drop7::centerFirstMove
//   --policy d4       the frozen drop7::fair_only_depth4::chooseDepth4Action
//   --policy d3fast   depth-3 five-strata FastSearch, used only as a source of
//                     realistic long-game column sequences when the frozen
//                     depth-4 search is too expensive for the seed count.  The
//                     column is still chosen once and handed to both engines,
//                     so the gate remains a test of engine semantics only.
//
// The column played is always chosen from the reference state and handed to
// both engines, so the gate isolates engine semantics from policy semantics.
// Any nonzero mismatch count fails.

#include "slow-search.hpp"
#include "fast-engine.hpp"
#include "fast-search.hpp"
#include "corpus.hpp"

#include <atomic>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace drop7;
using namespace drop7::fast;

struct Report {
  std::uint64_t games = 0;
  std::uint64_t moves = 0;
  std::uint64_t waves = 0;
  std::uint64_t mismatches = 0;
  std::string first_failure;
};

bool sameWaves(const std::vector<Wave>& reference, const FullWaveSink& sink) {
  if (reference.size() != static_cast<std::size_t>(sink.count)) return false;
  for (std::size_t index = 0; index < reference.size(); ++index) {
    const Wave& left = reference[index];
    const Wave& right = sink.waves[index];
    if (left.depth != right.depth || left.cleared != right.cleared ||
        left.revealed != right.revealed || left.points != right.points) {
      return false;
    }
  }
  return true;
}

enum class Policy { kCenter, kDepth4, kDepth3Fast };

void compareGame(std::uint32_t seed, int maximum_moves, Policy policy,
                 FastSearch& helper, Report& report) {
  requireLease(seed);
  State reference = initialHeadlessState(seed);
  State fast = initialHeadlessState(seed);
  if (reference.board != fast.board || reference.next_disc != fast.next_disc) {
    ++report.mismatches;
    if (report.first_failure.empty()) {
      report.first_failure = "initial state differs";
    }
    return;
  }
  ++report.games;
  while (!reference.game_over && reference.moves_played < maximum_moves) {
    int column = 0;
    switch (policy) {
      case Policy::kCenter:
        column = centerFirstMove(reference.board);
        break;
      case Policy::kDepth4:
        column = ref::chooseDepth4Action(reference).action;
        break;
      case Policy::kDepth3Fast: {
        FastSearchMetrics metrics;
        column = helper.chooseAction(reference, metrics);
        break;
      }
    }
    if (column < 0 || !isLegal(reference.board, column)) {
      column = centerFirstMove(reference.board);
      if (column < 0) break;
    }
    MoveResult reference_move;
    FullWaveSink sink;
    FastMoveResult fast_move;
    const bool reference_ok =
        playHeadlessMove(reference, seed, column, reference_move);
    const bool fast_ok =
        playHeadlessMoveFast(fast, seed, column, sink, fast_move);
    ++report.moves;
    report.waves += reference_move.waves.size();

    const bool identical =
        reference_ok == fast_ok && reference.board == fast.board &&
        reference.next_disc == fast.next_disc &&
        reference.score == fast.score && reference.level == fast.level &&
        reference.moves_remaining == fast.moves_remaining &&
        reference.moves_played == fast.moves_played &&
        reference.game_over == fast.game_over &&
        reference_move.score_delta == fast_move.score_delta &&
        reference_move.cleared_board == fast_move.cleared_board &&
        reference_move.level_advanced == fast_move.level_advanced &&
        sameWaves(reference_move.waves, sink);
    if (!identical) {
      ++report.mismatches;
      if (report.first_failure.empty()) {
        report.first_failure = "seed 0x" + std::to_string(seed) + " move " +
                               std::to_string(reference.moves_played);
      }
      return;
    }
    if (!reference_ok) break;
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::string policy = "center";
  int games = 4096;
  int maximum_moves = 2000;
  int threads = 8;
  std::uint32_t seed_start = 0;
  for (int index = 1; index + 1 < argc; index += 2) {
    const std::string key = argv[index];
    const std::string value = argv[index + 1];
    if (key == "--policy") policy = value;
    else if (key == "--games") games = std::stoi(value);
    else if (key == "--max-moves") maximum_moves = std::stoi(value);
    else if (key == "--threads") threads = std::stoi(value);
    else if (key == "--seed-start")
      seed_start = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
  }
  Policy selected = Policy::kCenter;
  if (policy == "d4") selected = Policy::kDepth4;
  else if (policy == "d3fast") selected = Policy::kDepth3Fast;
  if (seed_start == 0) {
    seed_start = selected == Policy::kCenter ? kTrajectoryDeterministicSeeds
                                             : kTrajectorySearchSeeds;
  }
  requireLease(seed_start);
  requireLease(seed_start + static_cast<std::uint32_t>(games) - 1u);

  std::vector<Report> reports(static_cast<std::size_t>(threads));
  std::atomic<int> cursor{0};
  std::vector<std::thread> workers;
  for (int worker = 0; worker < threads; ++worker) {
    workers.emplace_back([&, worker] {
      Report& report = reports[static_cast<std::size_t>(worker)];
      FastSearchParameters helper_parameters;
      helper_parameters.depth = 3;
      helper_parameters.chance_samples = 5;
      helper_parameters.maximum_work = 3'200'000;
      helper_parameters.maximum_cache_entries = 60'000;
      FastSearch helper{helper_parameters};
      while (true) {
        const int game = cursor.fetch_add(1);
        if (game >= games) break;
        compareGame(seed_start + static_cast<std::uint32_t>(game),
                    maximum_moves, selected, helper, report);
      }
    });
  }
  for (auto& worker : workers) worker.join();

  Report total;
  for (const Report& report : reports) {
    total.games += report.games;
    total.moves += report.moves;
    total.waves += report.waves;
    total.mismatches += report.mismatches;
    if (total.first_failure.empty()) total.first_failure = report.first_failure;
  }
  std::cout << "policy: " << policy << "  seeds 0x" << std::hex << seed_start
            << "-0x" << (seed_start + static_cast<std::uint32_t>(games) - 1u)
            << std::dec << '\n';
  std::cout << "games compared: " << total.games << '\n';
  std::cout << "moves compared: " << total.moves << '\n';
  std::cout << "waves compared: " << total.waves << '\n';
  std::cout << "mismatches: " << total.mismatches << '\n';
  if (total.mismatches > 0) {
    std::cout << "first failure: " << total.first_failure << '\n';
  }
  const bool ok = total.mismatches == 0 && total.games > 0;
  std::cout << (ok ? "TRAJECTORY GATE PASSED\n" : "TRAJECTORY GATE FAILED\n");
  return ok ? 0 : 1;
}
