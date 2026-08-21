#pragma once

// Position mode: the fair scenario metric.
//
// `docs/exploratory/design-01-benchmark-suite.md` defines two modes and states
// that only one of them can prove improvement:
//
//   Mode 1, puzzle mode, fixes the disc tape and scores a policy against the
//   clairvoyant optimum on that tape.  It is a difficulty labeller.  A public
//   policy cannot be graded against it, because the optimum is a function of
//   information the policy is forbidden to have.
//
//   Mode 2, position mode, holds the start position AND its latent board fixed,
//   draws K independent disc tapes, and scores a policy by its mean over those
//   K tapes with common random numbers across policies.  This is the fair
//   metric, and it did not exist before this file.
//
// The three primitives it is built on are the ones `finding-02` deliberately
// exposed for it: `retapeScenario`, `resampleScenarioRandomness`, and
// `reHorizonScenario`.
//
// Two details that matter for fairness:
//
//  * The visible next disc is `discTape[0]`.  It is part of the *position*, not
//    of the hidden future, so a re-taped sibling that changed it would change
//    the decision problem.  `retapeAt` therefore holds `discTape[0]` fixed and
//    redraws only `discTape[1..]` and every future risen row.  The
//    information-boundary gate in `posmode.cpp` uses exactly this property: a
//    fair policy's first chosen column must be identical across all K tapes.
//
//  * Every policy sees the same K tapes for a given position (common random
//    numbers), so a paired difference between two policies removes the
//    position-to-position and tape-to-tape variance that dominates the raw
//    mean.

#include "policies.hpp"

#include "../scenario/scenario-io.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace drop7::suitevalidation {

// Redraws the hidden future of a scenario while holding the start position, the
// latent board, the rise phase, and the visible next disc fixed.
inline bool retapeAt(const Scenario& source, int horizon, Mulberry32& random,
                     Scenario& out) {
  if (source.disc_tape.empty()) return false;
  std::vector<std::uint8_t> tape;
  tape.push_back(source.disc_tape[0]);  // the visible disc is public state
  for (int move = 1; move < horizon; ++move) tape.push_back(random.nextDisc());
  std::vector<RiseRow> rises;
  const int rise_count = riseRowCount(horizon, source.moves_remaining);
  for (int row = 0; row < rise_count; ++row) {
    RiseRow values{};
    for (int column = 0; column < kBoardSize; ++column) {
      values[column] = random.nextDisc();
    }
    rises.push_back(values);
  }
  return retapeScenario(source, tape, rises, out);
}

// Redraws the hidden value under every covered cell.  Used by the structure
// probe, where the label must be an expectation over latent completions so that
// it is a function of the *public* position alone.  Not used by position mode,
// which holds the latent board fixed by design.
inline bool relatentAt(const Scenario& source, Mulberry32& random,
                       Scenario& out) {
  out = source;
  for (int index = 0; index < kCellCount; ++index) {
    const std::uint8_t cell = out.board[index];
    if (cell == kSolid || cell == kCracked) out.latent[index] = random.nextDisc();
  }
  assignScenarioId(out);
  std::string reason;
  return validateScenario(out, reason);
}

// One policy's play of one fully specified scenario.
struct PlayRecord {
  std::int64_t points = 0;
  int moves = 0;
  int clears = 0;    // numbered discs removed by cascades
  int reveals = 0;   // covered cells cracked open
  int rises = 0;
  int max_chain_depth = 0;
  bool died = false;
  bool cleared_board = false;
  int first_column = -1;
  int occupied_end = 0;
  std::uint64_t work = 0;
};

inline PlayRecord playScenario(const Scenario& scenario, Policy& policy,
                               Mulberry32& policy_rng) {
  PlayRecord record;
  auto engine = makeScenarioEngine(scenario);
  for (int move = 0; move < scenario.horizon; ++move) {
    if (engine.state().game_over) break;
    const int column = policy.chooseColumn(engine.state(), policy_rng, record.work);
    if (column < 0 || !isLegal(engine.state().board, column)) break;
    if (record.first_column < 0) record.first_column = column;
    MoveResult result;
    if (!engine.play(column, result)) break;
    record.points += result.score_delta;
    ++record.moves;
    if (result.level_advanced) ++record.rises;
    if (result.cleared_board) record.cleared_board = true;
    for (const Wave& wave : result.waves) {
      record.clears += wave.cleared;
      record.reveals += wave.revealed;
      record.max_chain_depth = std::max(record.max_chain_depth, wave.depth);
    }
    if (engine.state().game_over) {
      record.died = true;
      break;
    }
  }
  record.occupied_end = occupiedCells(engine.state().board);
  return record;
}

}  // namespace drop7::suitevalidation
