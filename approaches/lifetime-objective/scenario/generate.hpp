#pragma once

// Scenario generation.
//
// Two sampling routes, on purpose:
//
//   * harvested - snapshots of positions a real game actually visits, taken by
//     playing the base engine with the lowest-column policy and then assigning
//     latent values to the covered cells from the lease RNG.  These have the
//     joint structure of real play (column profile, cover geometry, number
//     histogram) that no synthetic sampler reproduces.
//   * synthetic - positions drawn from a controlled occupancy, cover fraction,
//     and number profile, so the suite spans easy-open boards and near-death
//     crowded boards rather than only whatever the harvesting policy produces.
//
// Both routes end in the same `Scenario` record, so the solver and the labels
// cannot tell them apart except through the recorded origin field.

#include "scenario.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace drop7::scenario {

enum class NumberProfile { kUniform, kLowHeavy, kHighHeavy };

inline const char* numberProfileName(NumberProfile profile) {
  switch (profile) {
    case NumberProfile::kLowHeavy:
      return "low-heavy";
    case NumberProfile::kHighHeavy:
      return "high-heavy";
    default:
      return "uniform";
  }
}

inline std::uint8_t sampleNumber(Mulberry32& random, NumberProfile profile) {
  const std::uint8_t base = random.nextDisc();
  if (profile == NumberProfile::kUniform) return base;
  const std::uint8_t second = random.nextDisc();
  if (profile == NumberProfile::kLowHeavy) return std::min(base, second);
  return std::max(base, second);
}

inline int lowestColumnPolicy(const Board& board) {
  int best = -1;
  int best_height = kBoardSize + 1;
  for (int column = 0; column < kBoardSize; ++column) {
    if (!isLegal(board, column)) continue;
    int height = 0;
    for (int row = 0; row < kBoardSize; ++row) {
      if (board[indexOf(row, column)] != kEmpty) ++height;
    }
    if (height < best_height) {
      best_height = height;
      best = column;
    }
  }
  return best;
}

inline int occupiedCells(const Board& board) {
  int count = 0;
  for (std::uint8_t cell : board) {
    if (cell != kEmpty) ++count;
  }
  return count;
}

inline int coveredCells(const Board& board) {
  int count = 0;
  for (std::uint8_t cell : board) {
    if (cell == kSolid || cell == kCracked) ++count;
  }
  return count;
}

// Fills latent values, disc tape, and future risen rows, then stamps the id.
inline bool completeScenario(Scenario& scenario, int horizon,
                             Mulberry32& random) {
  scenario.horizon = static_cast<std::uint8_t>(horizon);
  scenario.latent.fill(0);
  for (int index = 0; index < kCellCount; ++index) {
    const std::uint8_t cell = scenario.board[index];
    if (cell == kSolid || cell == kCracked) {
      scenario.latent[index] = random.nextDisc();
    }
  }
  scenario.disc_tape.clear();
  for (int move = 0; move < horizon; ++move) {
    scenario.disc_tape.push_back(random.nextDisc());
  }
  scenario.rise_latent.clear();
  const int rises = riseRowCount(horizon, scenario.moves_remaining);
  for (int row = 0; row < rises; ++row) {
    RiseRow values{};
    for (int column = 0; column < kBoardSize; ++column) {
      values[column] = random.nextDisc();
    }
    scenario.rise_latent.push_back(values);
  }
  assignScenarioId(scenario);
  std::string reason;
  return validateScenario(scenario, reason);
}

struct HarvestOptions {
  int horizon = 8;
  int warmup_moves = 4;   // never snapshot the opening position
  int max_moves = 400;
};

// Plays one base-engine game with the lowest-column policy and returns a
// uniformly chosen mid-game snapshot as a scenario.  The base engine has no
// latent board (audit-01 M2), so the latent values are drawn here; the visible
// position is exactly one a real game reached.
inline bool harvestScenario(std::uint32_t game_seed, std::uint32_t label_seed,
                            const HarvestOptions& options, Scenario& out) {
  State state = initialHeadlessState(game_seed);
  std::vector<State> visited;
  for (int move = 0; move < options.max_moves; ++move) {
    if (state.game_over) break;
    const int column = lowestColumnPolicy(state.board);
    if (column < 0) break;
    MoveResult result;
    if (!playHeadlessMove(state, game_seed, column, result)) break;
    if (state.game_over) break;
    visited.push_back(state);
  }
  if (static_cast<int>(visited.size()) <= options.warmup_moves) return false;
  Mulberry32 random(label_seed);
  const std::size_t span = visited.size() -
                           static_cast<std::size_t>(options.warmup_moves);
  const std::size_t pick =
      static_cast<std::size_t>(options.warmup_moves) +
      static_cast<std::size_t>(random.nextBits() % span);
  const State& snapshot = visited[pick];

  out = Scenario{};
  out.board = snapshot.board;
  out.moves_remaining = static_cast<std::uint8_t>(snapshot.moves_remaining);
  return completeScenario(out, options.horizon, random);
}

struct SyntheticOptions {
  int horizon = 8;
  int target_cells = 24;
  double cover_fraction = 0.35;
  double cracked_share = 0.25;  // of the covered cells
  NumberProfile profile = NumberProfile::kUniform;
  int moves_remaining = 0;  // 0 draws uniformly from 1..5
  int attempts = 400;
};

// Draws a gravity-settled, popper-free position with a controlled occupancy and
// cover fraction.  Columns are filled bottom-up, which makes settling automatic;
// a draw that lands on an already-poppable position is rejected and redrawn.
inline bool syntheticScenario(std::uint32_t label_seed,
                              const SyntheticOptions& options, Scenario& out) {
  Mulberry32 random(label_seed);
  for (int attempt = 0; attempt < options.attempts; ++attempt) {
    std::array<int, kBoardSize> heights{};
    int remaining = std::min(options.target_cells, kBoardSize * (kBoardSize - 1));
    // Spread the requested cells over columns, capped at 6 so at least one
    // legal column always exists.
    while (remaining > 0) {
      bool placed = false;
      for (int column = 0; column < kBoardSize && remaining > 0; ++column) {
        if (heights[column] >= kBoardSize - 1) continue;
        if (random.nextBits() % 2u == 0u) {
          ++heights[column];
          --remaining;
          placed = true;
        }
      }
      if (!placed && remaining > 0) {
        bool any_room = false;
        for (int column = 0; column < kBoardSize; ++column) {
          if (heights[column] < kBoardSize - 1) any_room = true;
        }
        if (!any_room) break;
      }
    }
    Board board{};
    for (int column = 0; column < kBoardSize; ++column) {
      for (int step = 0; step < heights[column]; ++step) {
        const int row = kBoardSize - 1 - step;
        const double roll = random.nextUnit();
        std::uint8_t cell;
        if (roll < options.cover_fraction) {
          cell = random.nextUnit() < options.cracked_share ? kCracked : kSolid;
        } else {
          cell = sampleNumber(random, options.profile);
        }
        board[indexOf(row, column)] = cell;
      }
    }
    int popper_count = 0;
    findPoppers(board, popper_count);
    if (popper_count != 0) continue;
    int legal_count = 0;
    legalColumns(board, legal_count);
    if (legal_count == 0) continue;

    out = Scenario{};
    out.board = board;
    out.moves_remaining = static_cast<std::uint8_t>(
        options.moves_remaining > 0
            ? options.moves_remaining
            : 1 + static_cast<int>(random.nextBits() % kMovesPerLevel));
    if (completeScenario(out, options.horizon, random)) return true;
  }
  return false;
}

}  // namespace drop7::scenario
