#pragma once

// Shared corpus format and labeling logic for the distributional afterstate
// ranker pilot (experiment EX-20260820-afterstate-pilot-h40-29b8588a).
//
// Information boundary: a corpus afterstate record contains only the public
// state (board, next visible disc, moves until rise) plus continuation labels
// produced by a fixed public policy under synthetic, event-keyed chance
// scenarios. Origin seeds are recorded only for provenance, fold assignment,
// and common-random-number auditing; they are never part of a model input.

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "../../../src/core/native/engine.hpp"
#include "../../../src/core/native/public-behavior.hpp"

namespace drop7::afterstate {

constexpr std::uint32_t kCorpusDomain = 0x4153'4654u;  // "ASFT"
constexpr int kScenarios = 8;
constexpr int kHorizon = 40;
constexpr int kMaxHarvestGames = 256;
constexpr int kMaxRoots = 16'000;

// Fold boundaries on mix32(origin_seed) / 2^32.
constexpr double kTrainFraction = 0.70;
constexpr double kCalibrationFraction = 0.15;  // held-out is the remainder

struct RootRecord {
  std::uint32_t origin_seed = 0;
  int move_index = 0;
  Board board{};  // canonical public board
  int next_disc = 1;
  int moves_remaining = kMovesPerLevel;
  bool mirrored = false;  // canonicalization flag relative to the played game
  std::vector<int> legal_actions;  // canonical columns, ascending
  std::string fold;  // "train" | "calibration" | "heldout"
};

struct SiblingLabel {
  int action = -1;    // canonical column
  int scenario = -1;  // aligned scenario index
  Board afterstate{}; // canonical resolved public afterstate
  int afterstate_next_disc = 1;
  int afterstate_moves_remaining = kMovesPerLevel;
  bool terminal = false;
  double score_gained = 0.0;  // placement + continuation score over horizon
  int moves_survived = 0;     // continuation moves completed, 0..kHorizon
  int clears = 0;             // numbered discs cleared over placement+continuation
  int reveals = 0;            // covered discs revealed over placement+continuation
  int max_chain = 0;          // deepest chain wave observed
};

inline std::uint32_t publicStateHash(const Board& board, int next_disc,
                                     int moves_remaining) {
  std::uint32_t hash = 0x811c'9dc5u;
  for (const std::uint8_t cell : board) {
    hash ^= static_cast<std::uint32_t>(cell + 1u);
    hash *= 0x0100'0193u;
  }
  hash ^= static_cast<std::uint32_t>(next_disc);
  hash *= 0x0100'0193u;
  hash ^= static_cast<std::uint32_t>(moves_remaining);
  hash *= 0x0100'0193u;
  return mix32(hash);
}

// One scenario stream per (root, scenario), keyed only on the canonical public
// root state. Every sibling of a root reuses the same per-scenario stream, so
// chance outcomes are aligned across actions (common random numbers), and a
// reflected root yields an identical stream (reflection-safe).
inline Mulberry32 scenarioRandom(const RootRecord& root, int scenario) {
  const std::uint32_t key =
      publicStateHash(root.board, root.next_disc, root.moves_remaining);
  return Mulberry32(mix32(kCorpusDomain ^ key ^
                          (static_cast<std::uint32_t>(scenario + 1) *
                           0x9e37'79b9u)));
}

inline std::string foldForSeed(std::uint32_t seed) {
  const double unit =
      static_cast<double>(mix32(seed ^ kCorpusDomain)) / 4'294'967'296.0;
  if (unit < kTrainFraction) return "train";
  if (unit < kTrainFraction + kCalibrationFraction) return "calibration";
  return "heldout";
}

// Labels one legal sibling of a root under one aligned scenario: resolve the
// placement, then continue with the fixed public phase-greedy D1 policy for
// up to kHorizon moves, all randomness drawn from the scenario stream.
inline SiblingLabel labelSibling(const RootRecord& root, int action,
                                 int scenario) {
  SiblingLabel label;
  label.action = action;
  label.scenario = scenario;
  Mulberry32 random = scenarioRandom(root, scenario);

  State state;
  state.board = root.board;
  state.next_disc = static_cast<std::uint8_t>(root.next_disc);
  state.moves_remaining = root.moves_remaining;

  MoveResult move;
  if (!cfpi::detail::playMoveSampled(state, action, random, move)) {
    // Legal-action invariant: callers must pass only legal columns. A failure
    // here is a mechanics bug, so make it loud rather than a silent label.
    throw std::runtime_error("labelSibling called with an illegal action");
  }

  auto accumulate = [&label](const MoveResult& result) {
    for (const Wave& wave : result.waves) {
      label.clears += wave.cleared;
      label.reveals += wave.revealed;
      label.max_chain = std::max(label.max_chain, wave.depth);
    }
  };

  label.score_gained = static_cast<double>(move.score_delta);
  accumulate(move);

  // The afterstate is the resolved public state immediately after the root
  // placement, before any continuation move.
  bool ignored_mirror = false;
  const State canonical = cfpi::detail::canonicalState(move.state, ignored_mirror);
  label.afterstate = canonical.board;
  label.afterstate_next_disc = canonical.next_disc;
  label.afterstate_moves_remaining = canonical.moves_remaining;

  state = move.state;
  label.terminal = state.game_over;

  for (int step = 0; step < kHorizon && !state.game_over; ++step) {
    const int continuation = cfpi::choosePhaseGreedyAction(state, 1);
    if (continuation < 0) break;
    MoveResult follow;
    if (!cfpi::detail::playMoveSampled(state, continuation, random, follow)) break;
    label.score_gained += static_cast<double>(follow.score_delta);
    accumulate(follow);
    state = follow.state;
    ++label.moves_survived;
  }
  label.terminal = label.terminal || state.game_over;
  return label;
}

// Labels every legal sibling of a root under every aligned scenario.
// Successor closure is structural: the loop covers root.legal_actions
// completely, so a missing label indicates an engine failure, not a gap.
inline std::vector<SiblingLabel> labelRoot(const RootRecord& root,
                                           int scenario_count = kScenarios) {
  std::vector<SiblingLabel> labels;
  labels.reserve(root.legal_actions.size() * scenario_count);
  for (const int action : root.legal_actions) {
    for (int scenario = 0; scenario < scenario_count; ++scenario) {
      labels.push_back(labelSibling(root, action, scenario));
    }
  }
  return labels;
}

// Extracts a canonical root record from a played-game public state, or returns
// false when the state is terminal or offers no real decision.
inline bool makeRoot(const State& played, std::uint32_t origin_seed,
                     RootRecord& root) {
  if (played.game_over) return false;
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(played, mirrored);
  int legal_count = 0;
  const auto columns = legalColumns(canonical.board, legal_count);
  if (legal_count < 2) return false;

  root = RootRecord{};
  root.origin_seed = origin_seed;
  root.move_index = played.moves_played;
  root.board = canonical.board;
  root.next_disc = canonical.next_disc;
  root.moves_remaining = canonical.moves_remaining;
  root.mirrored = mirrored;
  root.legal_actions.assign(columns.begin(), columns.begin() + legal_count);
  std::sort(root.legal_actions.begin(), root.legal_actions.end());
  root.fold = foldForSeed(origin_seed);
  return true;
}

}  // namespace drop7::afterstate
