// Reference trace emitter for the Rust engine parity gate.
//
// Plays complete games through the FROZEN reference engine
// (src/core/native/engine.hpp, drop7::playHeadlessMove) and prints one text
// record per move.  The Rust gate (src/bin/gate_trajectory.rs) replays the
// same columns through the Rust engine and prints the identical format; the
// two outputs are diffed byte-for-byte.
//
// Columns are chosen from the reference state and recorded in the trace, so
// the gate isolates engine semantics from policy semantics (the same
// discipline as the C++ fast engine's gate-trajectory.cpp):
//   --policy center   drop7::centerFirstMove
//   --policy d3fast   depth-3 five-strata FastSearch (realistic long games)
//
// Seed discipline: only the Rust gate sub-block 0xa5276000-0xa5276fff of the
// already-opened SEEDLEASE-A52-FAST development lease is used here.
//
// This file is additive; it modifies no existing repository source.

#include "src/core/native/engine.hpp"
#include "approaches/lifetime-objective/fast-engine/fast-engine.hpp"
#include "approaches/lifetime-objective/fast-engine/fast-search.hpp"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

using namespace drop7;

constexpr std::uint32_t kRustGateSeeds = 0xa527'6000u;

void emitGame(std::uint32_t seed, int maximum_moves, bool use_search,
              drop7::fast::FastSearch& helper) {
  State state = initialHeadlessState(seed);
  std::cout << "game 0x" << std::hex << seed << std::dec << " next "
            << static_cast<int>(state.next_disc) << '\n';
  while (!state.game_over && state.moves_played < maximum_moves) {
    int column = -1;
    if (use_search) {
      drop7::fast::FastSearchMetrics metrics;
      column = helper.chooseAction(state, metrics);
    }
    if (column < 0 || !isLegal(state.board, column)) {
      column = centerFirstMove(state.board);
      if (column < 0) break;
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, column, move)) break;
    std::cout << "m " << state.moves_played << " col " << column << " sd "
              << move.score_delta << " b " << serializeBoard(state.board)
              << " next " << static_cast<int>(state.next_disc) << " score "
              << state.score << " level " << state.level << " mr "
              << state.moves_remaining << " over " << (state.game_over ? 1 : 0)
              << " cleared " << (move.cleared_board ? 1 : 0) << " advanced "
              << (move.level_advanced ? 1 : 0) << " waves";
    for (const Wave& wave : move.waves) {
      std::cout << ' ' << wave.depth << ':' << wave.cleared << ':'
                << wave.revealed << ':' << wave.points;
    }
    std::cout << '\n';
  }
  std::cout << "end score " << state.score << " moves " << state.moves_played
            << " over " << (state.game_over ? 1 : 0) << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  std::string policy = "center";
  int games = 4096;
  int maximum_moves = 2000;
  std::uint32_t seed_start = kRustGateSeeds;
  for (int index = 1; index + 1 < argc; index += 2) {
    const std::string key = argv[index];
    const std::string value = argv[index + 1];
    if (key == "--policy") policy = value;
    else if (key == "--games") games = std::stoi(value);
    else if (key == "--max-moves") maximum_moves = std::stoi(value);
    else if (key == "--seed-start")
      seed_start = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
  }
  if (seed_start < 0xa527'6000u ||
      seed_start + static_cast<std::uint32_t>(games) - 1u > 0xa527'6fffu) {
    std::cerr << "seed range outside the Rust gate sub-block" << '\n';
    return 2;
  }
  drop7::fast::FastSearchParameters helper_parameters;
  helper_parameters.depth = 3;
  helper_parameters.chance_samples = 5;
  helper_parameters.maximum_work = 3'200'000;
  helper_parameters.maximum_cache_entries = 60'000;
  drop7::fast::FastSearch helper{helper_parameters};
  for (int game = 0; game < games; ++game) {
    emitGame(seed_start + static_cast<std::uint32_t>(game), maximum_moves,
             policy == "d3fast", helper);
  }
  return 0;
}
