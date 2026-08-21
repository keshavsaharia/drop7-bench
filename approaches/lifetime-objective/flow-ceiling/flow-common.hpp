#pragma once

// Long-game machinery for the flow-ceiling measurement.
//
// The unit of analysis here is a whole game played inside the scenario
// (latent) randomness model, so that a clairvoyant planner and a public
// policy can be given *the same fixed future* and compared move for move.
//
// A `MasterTape` is that fixed future: one start position, one numbered disc
// per absolute move index, and one hidden row per absolute rise index.  Rises
// land on fixed move indices regardless of which columns are chosen
// (`movesRemainingAt` and `risesConsumed` are functions of the move index
// alone), so a master tape defines the same future for every policy.  Nothing
// is redrawn when a policy deviates, which is exactly the property the base
// engine lacks (audit-01 finding M2) and the reason this work runs inside the
// scenario engine at all.
//
// Everything about scoring, cascades, gravity, rises and termination comes from
// `scenario.hpp`, which `scenario-parity.cpp` proves trajectory-identical to
// `drop7::playHeadlessMove`.

#include "../scenario/scenario.hpp"
#include "../scenario/scenario-io.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace drop7::flowceiling {

using namespace drop7;
using namespace drop7::scenario;

constexpr int kMaxWaveDepth = 64;

// The disc-conservation requirement derived in finding-01: five placed discs
// plus seven risen covered discs enter a 49-cell board every five moves.
constexpr double kRequiredClearsPerMove = 12.0 / 5.0;   // 2.400
constexpr double kRequiredRevealsPerMove = 7.0 / 5.0;   // 1.400

// ---------------------------------------------------------------------------
// Master tape
// ---------------------------------------------------------------------------

struct MasterTape {
  std::uint32_t seed = 0;
  Board start_board{};
  LatentBoard start_latent{};
  int start_moves_remaining = kMovesPerLevel;
  std::vector<std::uint8_t> discs;
  std::vector<RiseRow> rises;
};

// A real game start: `drop7::initialBoard()` is an empty board with a solid
// covered bottom row, and a fresh game has five moves before the first rise.
// The seven covered cells need hidden values, which the base engine does not
// define, so they are drawn here from the lease RNG.
inline MasterTape makeMasterTape(std::uint32_t seed, int max_moves) {
  MasterTape tape;
  tape.seed = seed;
  tape.start_board = initialBoard();
  tape.start_moves_remaining = kMovesPerLevel;
  Mulberry32 random(seed);
  tape.start_latent.fill(0);
  for (int index = 0; index < kCellCount; ++index) {
    const std::uint8_t cell = tape.start_board[index];
    if (cell == kSolid || cell == kCracked) {
      tape.start_latent[index] = random.nextDisc();
    }
  }
  // Slack past `max_moves` so the last window can always be cut.
  const int disc_count = max_moves + 2 * kMovesPerLevel + 64;
  for (int move = 0; move < disc_count; ++move) {
    tape.discs.push_back(random.nextDisc());
  }
  const int rise_count = disc_count / kMovesPerLevel + 8;
  for (int row = 0; row < rise_count; ++row) {
    RiseRow values{};
    for (int column = 0; column < kBoardSize; ++column) {
      values[column] = random.nextDisc();
    }
    tape.rises.push_back(values);
  }
  return tape;
}

inline State masterStartState(const MasterTape& tape) {
  State state;
  state.board = tape.start_board;
  state.next_disc = tape.discs.empty() ? 1 : tape.discs[0];
  state.score = 0;
  state.level = 1;
  state.moves_remaining = tape.start_moves_remaining;
  state.moves_played = 0;
  state.game_over = false;
  return state;
}

// Plays one move of the long game.  `move_index` and `rises_consumed` are
// absolute counters into the master tape; everything else is the ordinary
// scenario move loop.
inline bool playMasterMove(const MasterTape& tape, const State& state,
                           const LatentBoard& latent, int move_index,
                           int rises_consumed, int column, MoveResult& result,
                           LatentBoard& latent_out) {
  LatentRevealSource source;
  source.tape = tape.discs.data();
  source.tape_length = static_cast<int>(tape.discs.size());
  source.tape_index = move_index + 1;
  source.rise_rows = tape.rises.data();
  source.rise_count = static_cast<int>(tape.rises.size());
  source.rise_index = rises_consumed;
  return playScenarioMove(state, latent, column, source, result, latent_out);
}

// Cuts a horizon-H scenario out of the long game at the current position.  This
// is the object the exact solver consumes; it is a self-verifying record with a
// content-hash id, so a window can be dumped and re-solved independently.
inline bool cutWindow(const MasterTape& tape, const State& state,
                      const LatentBoard& latent, int move_index,
                      int rises_consumed, int horizon, Scenario& out,
                      std::string& reason) {
  const int available = static_cast<int>(tape.discs.size()) - move_index;
  if (horizon > available) {
    reason = "master tape too short for the window";
    return false;
  }
  const int rows = riseRowCount(horizon, state.moves_remaining);
  if (rises_consumed + rows > static_cast<int>(tape.rises.size())) {
    reason = "master rise rows exhausted";
    return false;
  }
  out = Scenario{};
  out.board = state.board;
  out.latent = latent;
  out.moves_remaining = static_cast<std::uint8_t>(state.moves_remaining);
  out.horizon = static_cast<std::uint8_t>(horizon);
  out.disc_tape.assign(tape.discs.begin() + move_index,
                       tape.discs.begin() + move_index + horizon);
  out.rise_latent.assign(tape.rises.begin() + rises_consumed,
                         tape.rises.begin() + rises_consumed + rows);
  assignScenarioId(out);
  return validateScenario(out, reason);
}

// ---------------------------------------------------------------------------
// Per-move accounting
// ---------------------------------------------------------------------------

inline int occupiedCellCount(const Board& board) {
  int count = 0;
  for (std::uint8_t cell : board) {
    if (cell != kEmpty) ++count;
  }
  return count;
}

inline int coveredCellCount(const Board& board) {
  int count = 0;
  for (std::uint8_t cell : board) {
    if (cell == kSolid || cell == kCracked) ++count;
  }
  return count;
}

struct MoveStat {
  int move_index = 0;        // 1-based within the game
  int column = 0;
  int disc = 0;
  int cleared = 0;           // numbered discs removed by this move's cascades
  int revealed = 0;          // covered cells opened by this move's cascades
  int wave_count = 0;
  int max_wave_depth = 0;
  std::int64_t delta = 0;
  std::int64_t chain_points = 0;
  std::int64_t rise_points = 0;
  std::int64_t clear_points = 0;
  int clear_awards = 0;      // 70,000 bonuses paid by this move: 0, 1 or 2
  bool rise = false;
  bool double_clear_award = false;  // audit-01 M1: two bonuses in one move
  bool fifth_drop_clear = false;    // a clear on the drop that also rises
  int occupied_after = 0;
  int covered_after = 0;
  bool game_over = false;
  bool identity_ok = true;
};

// Splits the move's score delta into the three sources of
// `score = 17,000 x rises + 70,000 x boardClears + sum_waves points`.
inline MoveStat describeMove(int move_index, int column, int disc,
                             const MoveResult& result) {
  MoveStat stat;
  stat.move_index = move_index;
  stat.column = column;
  stat.disc = disc;
  stat.rise = result.level_advanced;
  stat.delta = result.score_delta;
  for (const Wave& wave : result.waves) {
    stat.cleared += wave.cleared;
    stat.revealed += wave.revealed;
    stat.chain_points += wave.points;
    stat.max_wave_depth = std::max(stat.max_wave_depth, wave.depth);
  }
  stat.wave_count = static_cast<int>(result.waves.size());
  stat.rise_points = result.level_advanced ? kLevelBonus : 0;
  stat.clear_points = stat.delta - stat.chain_points - stat.rise_points;
  stat.clear_awards =
      static_cast<int>(stat.clear_points / kClearBonus);
  stat.identity_ok = stat.clear_points >= 0 &&
                     stat.clear_points % kClearBonus == 0 &&
                     stat.clear_awards <= 2;
  stat.double_clear_award = stat.clear_awards >= 2;
  stat.fifth_drop_clear = result.level_advanced && stat.clear_awards > 0;
  stat.occupied_after = occupiedCellCount(result.state.board);
  stat.covered_after = coveredCellCount(result.state.board);
  stat.game_over = result.state.game_over;
  return stat;
}

// ---------------------------------------------------------------------------
// Whole-game accumulation
// ---------------------------------------------------------------------------

struct GameStat {
  std::uint32_t seed = 0;
  std::string policy;
  int moves = 0;
  std::int64_t score = 0;
  std::int64_t cleared = 0;
  std::int64_t revealed = 0;
  std::int64_t chain_points = 0;
  std::int64_t rise_points = 0;
  std::int64_t clear_points = 0;
  int rises = 0;
  int clear_moves = 0;        // moves on which the board was empty at least once
  int clear_awards = 0;       // 70,000 bonuses paid
  int double_clear_awards = 0;
  int fifth_drop_clears = 0;
  int max_wave_depth = 0;
  bool died = false;
  bool censored = false;      // hit the move cap alive
  int incomplete_windows = 0;
  int identity_violations = 0;
  std::int64_t solver_nodes = 0;
  double solve_seconds = 0.0;
  int measure_from = 0;      // warm-up moves played but excluded from stats
  int pv_mismatches = 0;
  std::array<std::int64_t, kMaxWaveDepth> wave_depth_count{};
  std::array<std::int64_t, kMaxWaveDepth> wave_depth_cleared{};
  std::vector<int> cycle_occupancy;   // occupied cells after each rise
  std::vector<int> cycle_covered;
  std::vector<int> move_occupancy;    // occupied cells after each move
  std::vector<int> move_covered;      // covered cells after each move
  std::vector<int> move_revealed;     // covers opened by each move

  void absorb(const MoveStat& stat, const MoveResult& result) {
    ++moves;
    score += stat.delta;
    cleared += stat.cleared;
    revealed += stat.revealed;
    chain_points += stat.chain_points;
    rise_points += stat.rise_points;
    clear_points += stat.clear_points;
    if (stat.rise) ++rises;
    if (stat.clear_awards > 0) ++clear_moves;
    clear_awards += stat.clear_awards;
    if (stat.double_clear_award) ++double_clear_awards;
    if (stat.fifth_drop_clear) ++fifth_drop_clears;
    if (!stat.identity_ok) ++identity_violations;
    max_wave_depth = std::max(max_wave_depth, stat.max_wave_depth);
    for (const Wave& wave : result.waves) {
      const int slot = std::min(wave.depth, kMaxWaveDepth - 1);
      ++wave_depth_count[static_cast<std::size_t>(slot)];
      wave_depth_cleared[static_cast<std::size_t>(slot)] += wave.cleared;
    }
    move_occupancy.push_back(stat.occupied_after);
    move_covered.push_back(stat.covered_after);
    move_revealed.push_back(stat.revealed);
    if (stat.rise) {
      cycle_occupancy.push_back(stat.occupied_after);
      cycle_covered.push_back(stat.covered_after);
    }
  }

  double clearsPerMove() const {
    return moves > 0 ? static_cast<double>(cleared) / moves : 0.0;
  }
  double revealsPerMove() const {
    return moves > 0 ? static_cast<double>(revealed) / moves : 0.0;
  }
};

// Least-squares slope of occupancy against cycle index, in cells per five-move
// cycle.  Positive means the board is filling faster than it is being emptied
// even under the policy being measured.
inline double occupancySlope(const std::vector<int>& occupancy, int skip) {
  const int n = static_cast<int>(occupancy.size()) - skip;
  if (n < 3) return 0.0;
  double sum_x = 0.0;
  double sum_y = 0.0;
  double sum_xy = 0.0;
  double sum_xx = 0.0;
  for (int index = 0; index < n; ++index) {
    const double x = index;
    const double y = occupancy[static_cast<std::size_t>(index + skip)];
    sum_x += x;
    sum_y += y;
    sum_xy += x * y;
    sum_xx += x * x;
  }
  const double denominator = n * sum_xx - sum_x * sum_x;
  if (denominator == 0.0) return 0.0;
  return (n * sum_xy - sum_x * sum_y) / denominator;
}

// ---------------------------------------------------------------------------
// Long-game driver
// ---------------------------------------------------------------------------

// `chooser(state, latent, move_index, rises_consumed)` returns a column, or a
// negative value to resign.  A clairvoyant chooser may read `latent`; a public
// policy must not, and `flow-run.cpp` keeps the two in separate functions so
// the boundary is visible rather than conditional.
// `measure_from` plays the first `measure_from` moves normally but excludes
// them from every statistic.  This is how a horizon or sample-count arm can be
// measured *from a warm mid-game state* rather than from the sparse opening:
// give two arms the same warm-up settings and the same tape and they reach a
// bit-identical state, after which only the measured segment differs.
template <typename Chooser>
inline void playLongGame(const MasterTape& tape, Chooser& chooser,
                         int max_moves, GameStat& out,
                         std::vector<MoveStat>* per_move,
                         int measure_from = 0) {
  State state = masterStartState(tape);
  LatentBoard latent = tape.start_latent;
  int rises_consumed = 0;
  for (int move = 0; move < max_moves; ++move) {
    if (state.game_over) break;
    const int column = chooser(state, latent, move, rises_consumed);
    if (column < 0 || !isLegal(state.board, column)) {
      out.died = true;
      return;
    }
    MoveResult result;
    LatentBoard next_latent{};
    if (!playMasterMove(tape, state, latent, move, rises_consumed, column,
                        result, next_latent)) {
      out.died = true;
      return;
    }
    const MoveStat stat =
        describeMove(move + 1 - measure_from, column, state.next_disc, result);
    if (move >= measure_from) {
      out.absorb(stat, result);
      if (per_move != nullptr) per_move->push_back(stat);
    }
    if (result.level_advanced) ++rises_consumed;
    state = result.state;
    latent = next_latent;
    if (state.game_over) {
      out.died = true;
      return;
    }
  }
  out.censored = true;
}

// ---------------------------------------------------------------------------
// JSON emission
// ---------------------------------------------------------------------------

inline std::string joinInts(const std::vector<int>& values) {
  std::ostringstream out;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) out << ',';
    out << values[index];
  }
  return out.str();
}

inline std::string waveHistogramJson(
    const std::array<std::int64_t, kMaxWaveDepth>& counts) {
  std::ostringstream out;
  out << '{';
  bool first = true;
  for (int depth = 0; depth < kMaxWaveDepth; ++depth) {
    if (counts[static_cast<std::size_t>(depth)] == 0) continue;
    if (!first) out << ',';
    first = false;
    out << '"' << depth << "\":" << counts[static_cast<std::size_t>(depth)];
  }
  out << '}';
  return out.str();
}

inline std::string gameStatJson(const GameStat& game) {
  std::ostringstream out;
  out << "{\"schema\":\"drop7-flow-game-v1\""
      << ",\"seed\":" << game.seed
      << ",\"policy\":\"" << game.policy << "\""
      << ",\"moves\":" << game.moves
      << ",\"score\":" << game.score
      << ",\"cleared\":" << game.cleared
      << ",\"revealed\":" << game.revealed
      << ",\"clearsPerMove\":" << game.clearsPerMove()
      << ",\"revealsPerMove\":" << game.revealsPerMove()
      << ",\"chainPoints\":" << game.chain_points
      << ",\"risePoints\":" << game.rise_points
      << ",\"clearPoints\":" << game.clear_points
      << ",\"rises\":" << game.rises
      << ",\"clearMoves\":" << game.clear_moves
      << ",\"clearAwards\":" << game.clear_awards
      << ",\"doubleClearAwards\":" << game.double_clear_awards
      << ",\"fifthDropClears\":" << game.fifth_drop_clears
      << ",\"maxWaveDepth\":" << game.max_wave_depth
      << ",\"died\":" << (game.died ? "true" : "false")
      << ",\"censored\":" << (game.censored ? "true" : "false")
      << ",\"incompleteWindows\":" << game.incomplete_windows
      << ",\"identityViolations\":" << game.identity_violations
      << ",\"pvMismatches\":" << game.pv_mismatches
      << ",\"solverNodes\":" << game.solver_nodes
      << ",\"solveSeconds\":" << game.solve_seconds
      << ",\"measureFrom\":" << game.measure_from
      << ",\"waveDepthCount\":" << waveHistogramJson(game.wave_depth_count)
      << ",\"waveDepthCleared\":" << waveHistogramJson(game.wave_depth_cleared)
      << ",\"cycleOccupancy\":[" << joinInts(game.cycle_occupancy) << "]"
      << ",\"cycleCovered\":[" << joinInts(game.cycle_covered) << "]"
      << ",\"moveOccupancy\":[" << joinInts(game.move_occupancy) << "]"
      << ",\"moveCovered\":[" << joinInts(game.move_covered) << "]"
      << ",\"moveRevealed\":[" << joinInts(game.move_revealed) << "]"
      << "}";
  return out.str();
}

}  // namespace drop7::flowceiling
