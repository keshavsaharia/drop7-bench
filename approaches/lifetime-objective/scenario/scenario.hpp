#pragma once

// Scenario engine: the repository move loop with a pluggable reveal source.
//
// Why this exists
// ---------------
// `src/core/native/engine.hpp` draws a covered disc's number from the reveal
// RNG at the moment the disc is revealed, not when the row was created.
// `docs/exploratory/audit-01-engine-fidelity.md` finding M2 records that this
// means the engine defines no persistent hidden board: the same physical gray
// can hold different numbers depending on when and in what order it is opened.
// A fully specified, exactly solvable puzzle instance therefore cannot be
// expressed in the base engine at all.
//
// This header re-expresses the same move loop with the reveal source factored
// out, using the shared `drop7::` primitives (`findPoppers`, `applyGravity`,
// `raiseCoveredRow`, `scoreForWave`, `isBoardEmpty`, `placeDisc`,
// `legalColumns`, `isLegal`) so the rules are shared rather than re-derived.
// Two sources are provided:
//
//   * `StreamRevealSource`   - pulls from a `drop7::Mulberry32`, reproducing
//                              the base engine bit for bit.  Proven by
//                              `scenario-parity.cpp`.
//   * `LatentRevealSource`   - every covered cell carries a fixed hidden value
//                              assigned in advance.  Revealing a cell consumes
//                              that cell's value.  This is what makes a
//                              scenario a deterministic perfect-information
//                              puzzle with an exact optimum.
//
// The two sources have the same marginal distribution (i.i.d. uniform 1..7)
// but genuinely different dynamics.  Scores under `LatentRevealSource` are not
// bit-comparable with base-engine runs.  See the finding document.

#include "../../../src/core/native/engine.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace drop7::scenario {

using LatentBoard = std::array<std::uint8_t, kCellCount>;
using RiseRow = std::array<std::uint8_t, kBoardSize>;

// ---------------------------------------------------------------------------
// Paired transforms: board and latent array move by exactly the same permutation
// ---------------------------------------------------------------------------

// The board result is produced by the shared primitive `drop7::applyGravity`;
// only the latent permutation is computed here, with the identical loop shape,
// so the two arrays can never drift apart.  `scenario-parity.cpp` additionally
// asserts the permutation agrees with `drop7::applyGravity` on random boards.
inline void applyGravityPaired(const Board& board, const LatentBoard& latent,
                               Board& out_board, LatentBoard& out_latent) {
  out_latent.fill(0);
  for (int column = 0; column < kBoardSize; ++column) {
    int destination = kBoardSize - 1;
    for (int row = kBoardSize - 1; row >= 0; --row) {
      const int index = indexOf(row, column);
      if (board[index] == kEmpty) continue;
      out_latent[indexOf(destination--, column)] = latent[index];
    }
  }
  out_board = applyGravity(board);
}

// Board legality and the row shift come from `drop7::raiseCoveredRow`.
inline bool raiseCoveredRowPaired(const Board& board, const LatentBoard& latent,
                                  const RiseRow& rise_latent, Board& out_board,
                                  LatentBoard& out_latent) {
  Board raised{};
  if (!raiseCoveredRow(board, raised)) return false;
  out_latent.fill(0);
  for (int row = 0; row < kBoardSize - 1; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      out_latent[indexOf(row, column)] = latent[indexOf(row + 1, column)];
    }
  }
  for (int column = 0; column < kBoardSize; ++column) {
    out_latent[indexOf(kBoardSize - 1, column)] = rise_latent[column];
  }
  out_board = raised;
  return true;
}

// ---------------------------------------------------------------------------
// Reveal sources
// ---------------------------------------------------------------------------

// Reproduces `drop7::playMove` exactly: reveal values and the next visible disc
// both come from one Mulberry32 stream, in the engine's consumption order.
struct StreamRevealSource {
  Mulberry32* random = nullptr;

  std::uint8_t reveal(int /*index*/, const LatentBoard& /*latent*/) {
    return random->nextDisc();
  }
  std::uint8_t nextVisibleDisc() { return random->nextDisc(); }
  RiseRow riseValues() { return RiseRow{}; }  // latent unused in stream mode
  bool exhausted() const { return false; }
};

// Consumes the fixed hidden value that already sits under each covered cell.
// Cells introduced by a row rise take their values from `rise_rows`, and the
// visible disc sequence is read from `tape`.
struct LatentRevealSource {
  const std::uint8_t* tape = nullptr;
  int tape_length = 0;
  int tape_index = 0;
  const RiseRow* rise_rows = nullptr;
  int rise_count = 0;
  int rise_index = 0;
  bool tape_exhausted = false;
  bool rise_exhausted = false;
  bool invalid_latent = false;

  std::uint8_t reveal(int index, const LatentBoard& latent) {
    const std::uint8_t value = latent[index];
    if (value < 1 || value > kBoardSize) {
      invalid_latent = true;
      return 1;
    }
    return value;
  }
  // Beyond the tape the scenario is undefined; callers stop at the horizon, so
  // a 0 here marks "no further disc is specified" rather than a legal disc.
  std::uint8_t nextVisibleDisc() {
    if (tape_index >= tape_length) {
      tape_exhausted = true;
      return 0;
    }
    return tape[tape_index++];
  }
  RiseRow riseValues() {
    if (rise_index >= rise_count) {
      rise_exhausted = true;
      return RiseRow{};
    }
    return rise_rows[rise_index++];
  }
  bool exhausted() const { return tape_exhausted || rise_exhausted; }
};

// ---------------------------------------------------------------------------
// Move loop
// ---------------------------------------------------------------------------

// Statement-for-statement image of `drop7::resolveCascade`, differing only in
// where a revealed cell's number comes from and in carrying the latent array
// through the same gravity transform.
template <typename Reveal>
inline void resolveCascadeScenario(Board& board, LatentBoard& latent,
                                   Reveal& source, int starting_depth,
                                   std::int64_t& score,
                                   std::vector<Wave>& waves) {
  for (int depth = starting_depth;; ++depth) {
    int popper_count = 0;
    const auto poppers = findPoppers(board, popper_count);
    if (popper_count == 0) return;

    std::array<bool, kCellCount> popping{};
    Board cleared = board;
    LatentBoard cleared_latent = latent;
    for (int offset = 0; offset < popper_count; ++offset) {
      const int index = poppers[offset];
      popping[index] = true;
      cleared[index] = kEmpty;
      cleared_latent[index] = 0;
    }

    std::array<int, kCellCount> reveals{};
    int reveal_count = 0;
    constexpr std::array<std::array<int, 2>, 4> directions{{
        {{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}},
    }};
    for (int row = 0; row < kBoardSize; ++row) {
      for (int column = 0; column < kBoardSize; ++column) {
        const int index = indexOf(row, column);
        const std::uint8_t cell = board[index];
        if (cell != kSolid && cell != kCracked) continue;
        int hits = 0;
        for (const auto& direction : directions) {
          const int neighbor_row = row + direction[0];
          const int neighbor_column = column + direction[1];
          if (inside(neighbor_row, neighbor_column) &&
              popping[indexOf(neighbor_row, neighbor_column)]) {
            ++hits;
          }
        }
        if (hits == 0) continue;
        const int hits_needed = cell == kSolid ? 2 : 1;
        if (hits >= hits_needed) {
          reveals[reveal_count++] = index;
        } else {
          cleared[index] = kCracked;
        }
      }
    }

    // Row-major reveal order is observable through subsequent chains, so it is
    // preserved exactly; only the value source changes.
    for (int offset = 0; offset < reveal_count; ++offset) {
      const int index = reveals[offset];
      cleared[index] = source.reveal(index, latent);
      cleared_latent[index] = 0;
    }
    const std::int64_t points = popper_count * scoreForWave(depth);
    score += points;
    waves.push_back({depth, popper_count, reveal_count, points});
    Board next_board{};
    LatentBoard next_latent{};
    applyGravityPaired(cleared, cleared_latent, next_board, next_latent);
    board = next_board;
    latent = next_latent;
  }
}

// Statement-for-statement image of `drop7::playMove`.
template <typename Reveal>
inline bool playScenarioMove(const State& state, const LatentBoard& latent_in,
                             int column, Reveal& source, MoveResult& result,
                             LatentBoard& latent_out) {
  if (state.game_over) return false;
  Board board = state.board;
  LatentBoard latent = latent_in;
  if (!placeDisc(board, column, state.next_disc)) return false;
  // The placed disc is a visible number; its cell carries no latent value.
  for (int row = kBoardSize - 1; row >= 0; --row) {
    const int index = indexOf(row, column);
    if (board[index] != kEmpty && state.board[index] == kEmpty) {
      latent[index] = 0;
      break;
    }
  }

  // Equivalent to `result = MoveResult{}` but keeps the wave vector's capacity,
  // which matters because the solver calls this millions of times per scenario.
  result.state = State{};
  result.score_delta = 0;
  result.waves.clear();
  result.cleared_board = false;
  result.level_advanced = false;
  std::int64_t first_score = 0;
  resolveCascadeScenario(board, latent, source, 1, first_score, result.waves);
  result.score_delta = first_score;
  result.cleared_board = isBoardEmpty(board);
  if (result.cleared_board) result.score_delta += kClearBonus;

  int level = state.level;
  int moves_remaining = state.moves_remaining - 1;
  bool game_over = false;
  if (moves_remaining == 0) {
    Board raised{};
    LatentBoard raised_latent{};
    const RiseRow rise_values = source.riseValues();
    if (!raiseCoveredRowPaired(board, latent, rise_values, raised,
                               raised_latent)) {
      game_over = true;
    } else {
      result.level_advanced = true;
      ++level;
      moves_remaining = kMovesPerLevel;
      result.score_delta += kLevelBonus;
      board = raised;
      latent = raised_latent;
      std::int64_t level_score = 0;
      const int next_depth =
          result.waves.empty() ? 1 : result.waves.back().depth + 1;
      resolveCascadeScenario(board, latent, source, next_depth, level_score,
                             result.waves);
      result.score_delta += level_score;
      if (isBoardEmpty(board)) {
        // Note: the base engine can award kClearBonus twice in one move (once
        // before the rise and once after).  audit-01 finding M1 flags this as a
        // divergence from the cited reference.  It is reproduced here on
        // purpose: the scenario engine must be the repository's game.
        result.score_delta += kClearBonus;
        result.cleared_board = true;
      }
    }
  }

  int legal_count = 0;
  legalColumns(board, legal_count);
  if (!game_over && legal_count == 0) game_over = true;

  result.state.board = board;
  result.state.next_disc =
      game_over ? state.next_disc : source.nextVisibleDisc();
  result.state.score = state.score + result.score_delta;
  result.state.level = level;
  result.state.moves_remaining = moves_remaining;
  result.state.moves_played = state.moves_played + 1;
  result.state.game_over = game_over;
  latent_out = latent;
  return true;
}

template <typename Reveal>
class ScenarioEngine {
 public:
  ScenarioEngine() = default;
  ScenarioEngine(const State& state, const LatentBoard& latent,
                 const Reveal& source)
      : state_(state), latent_(latent), source_(source) {}

  bool play(int column, MoveResult& result) {
    LatentBoard next_latent{};
    if (!playScenarioMove(state_, latent_, column, source_, result,
                          next_latent)) {
      return false;
    }
    state_ = result.state;
    latent_ = next_latent;
    return true;
  }

  const State& state() const { return state_; }
  State& mutableState() { return state_; }
  const LatentBoard& latent() const { return latent_; }
  Reveal& source() { return source_; }
  const Reveal& source() const { return source_; }

 private:
  State state_{};
  LatentBoard latent_{};
  Reveal source_{};
};

// ---------------------------------------------------------------------------
// Scenario record
// ---------------------------------------------------------------------------

// Number of future risen rows a scenario must specify to cover `horizon` moves
// starting with `moves_remaining` moves left in the current level.  One extra
// row is kept so that a solver never reads past the end.
inline int riseRowCount(int horizon, int moves_remaining) {
  return (horizon + moves_remaining + kMovesPerLevel - 1) / kMovesPerLevel + 1;
}

// Rises land on fixed move indices regardless of which columns are chosen, so
// the number of rises consumed after `moves` moves is a function of depth only.
inline int risesConsumed(int moves, int moves_remaining) {
  if (moves < moves_remaining) return 0;
  return (moves - moves_remaining) / kMovesPerLevel + 1;
}

struct Scenario {
  char id[17] = {};
  std::array<std::uint8_t, kCellCount> board{};
  std::array<std::uint8_t, kCellCount> latent{};
  std::uint8_t moves_remaining = kMovesPerLevel;
  std::uint8_t horizon = 0;
  std::vector<std::uint8_t> disc_tape;
  std::vector<RiseRow> rise_latent;
};

// FNV-1a 64 over a canonical byte image of every field except the id itself.
// Documented here so any language can recompute it: append board[0..48],
// latent[0..48], movesRemaining, horizon, |discTape| as one byte, discTape,
// |riseLatent| as one byte, then each rise row's seven bytes in order.
inline std::uint64_t scenarioContentHash(const Scenario& scenario) {
  std::uint64_t hash = 1469598103934665603ull;
  const auto absorb = [&hash](std::uint8_t byte) {
    hash ^= static_cast<std::uint64_t>(byte);
    hash *= 1099511628211ull;
  };
  for (std::uint8_t cell : scenario.board) absorb(cell);
  for (std::uint8_t cell : scenario.latent) absorb(cell);
  absorb(scenario.moves_remaining);
  absorb(scenario.horizon);
  absorb(static_cast<std::uint8_t>(scenario.disc_tape.size()));
  for (std::uint8_t disc : scenario.disc_tape) absorb(disc);
  absorb(static_cast<std::uint8_t>(scenario.rise_latent.size()));
  for (const RiseRow& row : scenario.rise_latent) {
    for (std::uint8_t value : row) absorb(value);
  }
  return hash;
}

inline void assignScenarioId(Scenario& scenario) {
  std::snprintf(scenario.id, sizeof(scenario.id), "%016llx",
                static_cast<unsigned long long>(scenarioContentHash(scenario)));
}

inline bool scenarioIdMatches(const Scenario& scenario) {
  char expected[17] = {};
  std::snprintf(expected, sizeof(expected), "%016llx",
                static_cast<unsigned long long>(scenarioContentHash(scenario)));
  return std::string(expected) == std::string(scenario.id);
}

// A scenario is well formed when the board is gravity-settled, every covered
// cell carries a latent value in 1..7, no visible cell carries one, the board
// has no already-poppable disc, and the tapes are long enough for the horizon.
inline bool validateScenario(const Scenario& scenario, std::string& reason) {
  const Board settled = applyGravity(scenario.board);
  if (settled != scenario.board) {
    reason = "board is not gravity-settled";
    return false;
  }
  for (int index = 0; index < kCellCount; ++index) {
    const std::uint8_t cell = scenario.board[index];
    const std::uint8_t value = scenario.latent[index];
    const bool covered = cell == kSolid || cell == kCracked;
    if (covered && (value < 1 || value > kBoardSize)) {
      reason = "covered cell without a latent value in 1..7";
      return false;
    }
    if (!covered && value != 0) {
      reason = "visible or empty cell carries a latent value";
      return false;
    }
    if (cell > kCracked) {
      reason = "cell value out of range";
      return false;
    }
  }
  int popper_count = 0;
  findPoppers(scenario.board, popper_count);
  if (popper_count != 0) {
    reason = "start position already contains a poppable disc";
    return false;
  }
  if (scenario.moves_remaining < 1 ||
      scenario.moves_remaining > kMovesPerLevel) {
    reason = "movesRemaining outside 1..5";
    return false;
  }
  if (scenario.horizon < 1) {
    reason = "horizon must be positive";
    return false;
  }
  if (scenario.disc_tape.size() != static_cast<std::size_t>(scenario.horizon)) {
    reason = "discTape length must equal horizon";
    return false;
  }
  for (std::uint8_t disc : scenario.disc_tape) {
    if (disc < 1 || disc > kBoardSize) {
      reason = "discTape entry outside 1..7";
      return false;
    }
  }
  const std::size_t required = static_cast<std::size_t>(
      riseRowCount(scenario.horizon, scenario.moves_remaining));
  if (scenario.rise_latent.size() != required) {
    reason = "riseLatent row count does not match the horizon";
    return false;
  }
  for (const RiseRow& row : scenario.rise_latent) {
    for (std::uint8_t value : row) {
      if (value < 1 || value > kBoardSize) {
        reason = "riseLatent entry outside 1..7";
        return false;
      }
    }
  }
  int legal_count = 0;
  legalColumns(scenario.board, legal_count);
  if (legal_count == 0) {
    reason = "start position has no legal column";
    return false;
  }
  if (!scenarioIdMatches(scenario)) {
    reason = "id does not match the content hash";
    return false;
  }
  return true;
}

// Replaces the randomness of a scenario while keeping the start position, the
// latent board, and the rise phase.  This is the operation a fair multi-tape
// evaluation needs: hold `board`, `latent`, and `movesRemaining` fixed and draw
// many independent tapes, so that "the shallow policy was unlucky" can be
// separated from "the shallow policy is worse here".  The id changes because
// the content changed; the start position is unchanged and can be compared
// directly across the family.
inline bool retapeScenario(const Scenario& source,
                           const std::vector<std::uint8_t>& disc_tape,
                           const std::vector<RiseRow>& rise_latent,
                           Scenario& out) {
  if (disc_tape.empty() || disc_tape.size() > 255) return false;
  const int horizon = static_cast<int>(disc_tape.size());
  if (static_cast<int>(rise_latent.size()) !=
      riseRowCount(horizon, source.moves_remaining)) {
    return false;
  }
  out = source;
  out.horizon = static_cast<std::uint8_t>(horizon);
  out.disc_tape = disc_tape;
  out.rise_latent = rise_latent;
  assignScenarioId(out);
  return true;
}

// Draws a fresh tape and fresh risen rows for the same start position.
inline bool resampleScenarioRandomness(const Scenario& source, int horizon,
                                       Mulberry32& random, Scenario& out) {
  std::vector<std::uint8_t> tape;
  for (int move = 0; move < horizon; ++move) tape.push_back(random.nextDisc());
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

// Re-cuts a scenario to a shorter horizon, keeping the same start position,
// latent board, and tape prefix.  Used to find the largest exactly solvable
// horizon without regenerating anything.
inline bool reHorizonScenario(const Scenario& source, int horizon,
                              Scenario& out) {
  if (horizon < 1 || horizon > static_cast<int>(source.disc_tape.size())) {
    return false;
  }
  const int rises = riseRowCount(horizon, source.moves_remaining);
  if (rises > static_cast<int>(source.rise_latent.size())) return false;
  out = source;
  out.horizon = static_cast<std::uint8_t>(horizon);
  out.disc_tape.assign(source.disc_tape.begin(),
                       source.disc_tape.begin() + horizon);
  out.rise_latent.assign(source.rise_latent.begin(),
                         source.rise_latent.begin() + rises);
  assignScenarioId(out);
  return true;
}

inline State scenarioStartState(const Scenario& scenario) {
  State state;
  state.board = scenario.board;
  state.next_disc = scenario.disc_tape.empty() ? 1 : scenario.disc_tape[0];
  state.score = 0;
  state.level = 1;
  state.moves_remaining = scenario.moves_remaining;
  state.moves_played = 0;
  state.game_over = false;
  return state;
}

inline LatentRevealSource scenarioSource(const Scenario& scenario) {
  LatentRevealSource source;
  source.tape = scenario.disc_tape.data();
  source.tape_length = static_cast<int>(scenario.disc_tape.size());
  source.tape_index = 1;  // entry 0 is the disc already in hand
  source.rise_rows = scenario.rise_latent.data();
  source.rise_count = static_cast<int>(scenario.rise_latent.size());
  source.rise_index = 0;
  return source;
}

inline ScenarioEngine<LatentRevealSource> makeScenarioEngine(
    const Scenario& scenario) {
  return ScenarioEngine<LatentRevealSource>(
      scenarioStartState(scenario), scenario.latent, scenarioSource(scenario));
}

}  // namespace drop7::scenario
