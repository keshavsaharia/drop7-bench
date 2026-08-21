// corpus-gen - play whole games with the LEGAL receding-horizon planner and log
// the planner's value for EVERY legal sibling at every decision.
//
//   corpus-gen --budget [--horizon 7] [--samples 256] [--decisions 12]
//   corpus-gen --self-test
//   corpus-gen --games N --seed-start 0xa5260000 --sampler-seed 0xa5268000
//              [--horizon 7] [--samples 64] [--max-moves 400] [--epsilon 0]
//              [--threads 10] [--out runs/RID/corpus.bin] [--jsonl games.jsonl]
//
// WHY
// ---
// `docs/exploratory/finding-06-flow-ceiling.md` measured a *clairvoyant*
// receding-horizon planner sustaining disc-conservation balance, and proposed
// distilling it.  `docs/exploratory/finding-07-fair-planning-ceiling.md`
// weakened that plan: up to 41% of the clairvoyant teacher's margin is hidden
// information a public student can never represent, so a student fitted to
// clairvoyant labels is partly being asked to learn a function of hidden state.
// finding-07 also supplied the repair: the *fair* planner (arm B) is legal by
// construction, already 0.18 clears per move ahead of fair depth 4, and
// expensive enough that amortising it is exactly what a learned evaluator is
// for.  This program generates that teacher's corpus.
//
// This is a hypothesis about why prior distillations failed held-out gates, not
// a proven cause.  What is measured here is only that the teacher's advantage
// is representable in principle; whether a student can absorb it is the
// experiment.
//
// WHAT IS LOGGED AND WHY
// ----------------------
// Six of the seventeen documented learned-model failures are class (iii),
// sibling coverage: only the played action was labelled, or the sibling labels
// were too noisy to separate.  `fairDecision` in fair-planner.hpp already
// computes an exact per-column mean over K shared completions and then throws
// away everything except the argmax.  This program keeps the whole vector.
//
// Common random numbers hold in two places at once:
//   * inside the planner, every column is scored against the SAME K sampled
//     hidden boards (that is what `solveWindowRoot` is for); and
//   * in the environment, every column's realised afterstate is resolved
//     against the SAME true master tape.
//
// INFORMATION BOUNDARY
// --------------------
// The planner runs with `latent_known = false` and `tape_known = false`.  Its
// chosen column is a function of the visible board, the visible next disc and
// the moves remaining before the next rise; `flow-run --self-test` gate 6
// proves that by re-deciding the same public position against a completely
// different hidden board and future.  `--self-test` here re-runs the same
// property against this program's own decision path.
//
// The realised afterstate boards written to the corpus are PUBLIC states: a
// revealed cover is a visible number.  They are inputs to a student, never
// hidden values.

#include "pinned/flow-ceiling/fair-planner.hpp"
#include "pinned/flow-ceiling/flow-common.hpp"
#include "pinned/flow-ceiling/flow-solver.hpp"
#include "corpus.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace drop7;
using namespace drop7::scenario;
using namespace drop7::flowceiling;
using drop7::distill::RootRecord;

// SEEDLEASE-A52-DISTILL, assigned to this work by the coordinator.  Game seeds
// come from the low half and the hidden-board sampler from the high half, so
// sampling randomness can never collide with game randomness.
constexpr std::uint32_t kLeaseStart = 0xa526'0000u;
constexpr std::uint32_t kLeaseEnd = 0xa526'ffffu;
constexpr std::uint32_t kGameSeedEnd = 0xa526'7fffu;
constexpr std::uint32_t kSamplerSeedStart = 0xa526'8000u;

struct Options {
  int games = 8;
  std::uint32_t seed_start = kLeaseStart;
  std::uint32_t sampler_seed = kSamplerSeedStart;
  int horizon = 7;
  int samples = 64;
  int max_moves = 400;
  int threads = 8;
  int decisions = 12;
  double epsilon = 0.0;
  double time_limit = 20.0;
  std::string out;
  std::string jsonl;
  bool budget = false;
  bool self_test = false;
};

// ---------------------------------------------------------------------------
// One game, with full sibling logging
// ---------------------------------------------------------------------------

struct GameOutput {
  GameStat stat;
  std::vector<RootRecord> records;
  double decision_seconds = 0.0;
  int decisions = 0;
};

// Everything the planner needs, plus the per-column bookkeeping the corpus
// needs.  This mirrors `fairDecision` rather than calling it, because the
// argmax is all `fairDecision` returns and the whole point here is the vector.
struct LoggedDecision {
  std::array<double, kBoardSize> total{};
  std::array<double, kBoardSize> immediate{};
  // Independent halves of the K completions.  Their disagreement is the noise
  // floor of the label itself, and therefore the ceiling on any student.
  std::array<double, kBoardSize> total_lo{};
  std::array<double, kBoardSize> total_hi{};
  std::array<int, kBoardSize> counted_lo{};
  std::array<int, kBoardSize> counted_hi{};
  std::array<int, kBoardSize> counted{};
  std::array<bool, kBoardSize> legal{};
  int column = -1;
  int samples_used = 0;
  int incomplete = 0;
  std::int64_t nodes = 0;
  double wall_seconds = 0.0;
};

LoggedDecision loggedDecision(const FairPlannerConfig& config,
                              const MasterTape& tape, const State& state,
                              const LatentBoard& truth, int move_index,
                              int rises_consumed, Mulberry32& random) {
  LoggedDecision out;
  MoveResult scratch;
  for (int sample = 0; sample < config.samples; ++sample) {
    Scenario window;
    std::string reason;
    if (!sampleWindow(config, tape, state, truth, move_index, rises_consumed,
                      random, window, reason)) {
      break;
    }
    const RootResult root =
        solveWindowRoot(window, config.objective, config.limits);
    out.nodes += root.nodes;
    out.wall_seconds += root.wall_seconds;
    if (!root.complete) {
      ++out.incomplete;
      continue;
    }
    ++out.samples_used;

    // The immediate term under this same completion.  One extra move
    // application per column per sample, against a search that already spent
    // thousands of nodes on the same column: a rounding error in cost, and it
    // is what separates the afterstate's value from the move's own reward.
    SearchNode root_node;
    root_node.board = window.board;
    root_node.latent = window.latent;
    for (int column = 0; column < kBoardSize; ++column) {
      if (!root.legal[static_cast<std::size_t>(column)]) continue;
      out.legal[static_cast<std::size_t>(column)] = true;
      const double sibling_value =
          static_cast<double>(root.value[static_cast<std::size_t>(column)]);
      out.total[static_cast<std::size_t>(column)] += sibling_value;
      if (sample * 2 < config.samples) {
        out.total_lo[static_cast<std::size_t>(column)] += sibling_value;
        ++out.counted_lo[static_cast<std::size_t>(column)];
      } else {
        out.total_hi[static_cast<std::size_t>(column)] += sibling_value;
        ++out.counted_hi[static_cast<std::size_t>(column)];
      }
      SearchNode child;
      std::int64_t delta = 0;
      bool game_over = false;
      double immediate = 0.0;
      if (applyScenarioMove(window, root_node, 0, column, child, delta,
                            game_over, scratch)) {
        for (const Wave& wave : scratch.waves) immediate += wave.cleared;
      }
      out.immediate[static_cast<std::size_t>(column)] += immediate;
      ++out.counted[static_cast<std::size_t>(column)];
    }
  }

  double best = 0.0;
  for (int column = 0; column < kBoardSize; ++column) {
    const std::size_t slot = static_cast<std::size_t>(column);
    if (!out.legal[slot] || out.counted[slot] == 0) continue;
    const double mean = out.total[slot] / out.counted[slot];
    if (out.column < 0 || mean > best) {
      best = mean;
      out.column = column;
    }
  }
  return out;
}

// A deterministic per-game exploration stream, separate from the planner's
// sampler stream so that turning exploration on cannot change the planner's
// completions for a given seed.
struct Explorer {
  Mulberry32 random;
  double epsilon = 0.0;

  bool deviate() { return epsilon > 0.0 && random.nextUnit() < epsilon; }
};

GameOutput playCorpusGame(const Options& options, std::uint32_t seed,
                          std::uint32_t sampler_seed) {
  GameOutput out;
  out.stat.seed = seed;
  out.stat.policy = "fair-rh";

  const MasterTape tape = makeMasterTape(seed, options.max_moves);

  FairPlannerConfig config;
  config.objective = Objective::kClears;
  config.horizon = options.horizon;
  config.samples = options.samples;
  config.latent_known = false;
  config.tape_known = false;
  config.limits.max_seconds = options.time_limit;

  Mulberry32 sampler(sampler_seed);
  Explorer explorer{Mulberry32(sampler_seed ^ 0x5bf0'3a91u), options.epsilon};

  State state = masterStartState(tape);
  LatentBoard latent = tape.start_latent;
  int rises_consumed = 0;

  for (int move = 0; move < options.max_moves; ++move) {
    if (state.game_over) break;

    const auto started = std::chrono::steady_clock::now();
    const LoggedDecision decision = loggedDecision(
        config, tape, state, latent, move, rises_consumed, sampler);
    out.decision_seconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
            .count();
    ++out.decisions;
    out.stat.solver_nodes += decision.nodes;
    out.stat.solve_seconds += decision.wall_seconds;
    out.stat.incomplete_windows += decision.incomplete;
    if (decision.column < 0) {
      out.stat.died = true;
      break;
    }

    RootRecord record{};
    std::memcpy(record.board, state.board.data(), drop7::distill::kCells);
    record.next_disc = state.next_disc;
    record.moves_remaining = static_cast<std::uint8_t>(state.moves_remaining);
    record.occupied = static_cast<std::uint8_t>(occupiedCellCount(state.board));
    record.chosen_column = static_cast<std::uint8_t>(decision.column);
    record.samples_used = static_cast<std::uint8_t>(
        std::min(decision.samples_used, 255));
    record.incomplete = static_cast<std::uint8_t>(
        std::min(decision.incomplete, 255));
    record.move_index = static_cast<std::uint16_t>(move);
    record.horizon = static_cast<std::uint8_t>(options.horizon);
    record.samples_configured = static_cast<std::uint16_t>(options.samples);
    record.game_seed = seed;

    std::uint8_t mask = 0;
    for (int column = 0; column < kBoardSize; ++column) {
      const std::size_t slot = static_cast<std::size_t>(column);
      record.value[slot] = -1.0f;
      record.immediate[slot] = -1.0f;
      record.value_lo[slot] = -1.0f;
      record.value_hi[slot] = -1.0f;
      if (!isLegal(state.board, column)) continue;
      mask |= static_cast<std::uint8_t>(1u << column);
      if (decision.counted[slot] > 0) {
        record.value[slot] = static_cast<float>(decision.total[slot] /
                                                decision.counted[slot]);
        record.immediate[slot] = static_cast<float>(decision.immediate[slot] /
                                                    decision.counted[slot]);
        if (decision.counted_lo[slot] > 0) {
          record.value_lo[slot] = static_cast<float>(
              decision.total_lo[slot] / decision.counted_lo[slot]);
        }
        if (decision.counted_hi[slot] > 0) {
          record.value_hi[slot] = static_cast<float>(
              decision.total_hi[slot] / decision.counted_hi[slot]);
        }
      }
      // The realised afterstate under the true environment.  This is the state
      // the game would actually be in, and the state a search's chance node
      // would hand a leaf evaluator.
      MoveResult sibling;
      LatentBoard sibling_latent{};
      if (playMasterMove(tape, state, latent, move, rises_consumed, column,
                         sibling, sibling_latent)) {
        std::memcpy(record.after_board[slot], sibling.state.board.data(),
                    drop7::distill::kCells);
        record.after_survived[slot] = sibling.state.game_over ? 0 : 1;
        int cleared = 0;
        int revealed = 0;
        for (const Wave& wave : sibling.waves) {
          cleared += wave.cleared;
          revealed += wave.revealed;
        }
        record.after_clears[slot] = static_cast<std::uint8_t>(std::min(cleared, 255));
        record.after_reveals[slot] = static_cast<std::uint8_t>(std::min(revealed, 255));
        record.after_occupied[slot] =
            static_cast<std::uint8_t>(occupiedCellCount(sibling.state.board));
        record.after_next_disc[slot] = sibling.state.next_disc;
        record.after_moves_remaining[slot] =
            static_cast<std::uint8_t>(sibling.state.moves_remaining);
      }
    }
    record.legal_mask = mask;

    int played = decision.column;
    if (explorer.deviate()) {
      int legal_count = 0;
      const auto columns = legalColumns(state.board, legal_count);
      if (legal_count > 1) {
        const int pick = static_cast<int>(explorer.random.nextUnit() *
                                          static_cast<double>(legal_count));
        played = columns[static_cast<std::size_t>(
            std::min(pick, legal_count - 1))];
      }
    }
    record.played_column = static_cast<std::uint8_t>(played);
    record.explored = played == decision.column ? 0 : 1;
    out.records.push_back(record);

    MoveResult result;
    LatentBoard next_latent{};
    if (!playMasterMove(tape, state, latent, move, rises_consumed, played,
                        result, next_latent)) {
      out.stat.died = true;
      break;
    }
    const MoveStat stat = describeMove(move + 1, played, state.next_disc, result);
    out.stat.absorb(stat, result);
    if (result.level_advanced) ++rises_consumed;
    state = result.state;
    latent = next_latent;
    if (state.game_over) {
      out.stat.died = true;
      break;
    }
    if (move + 1 == options.max_moves) out.stat.censored = true;
  }
  if (!out.stat.died) out.stat.censored = true;

  // Remaining-lifetime labels are filled in once the game's length is known.
  const int total_moves = static_cast<int>(out.records.size());
  for (int index = 0; index < total_moves; ++index) {
    out.records[static_cast<std::size_t>(index)].moves_to_end =
        static_cast<std::uint16_t>(total_moves - index);
    out.records[static_cast<std::size_t>(index)].censored_game =
        out.stat.censored ? 1 : 0;
  }
  return out;
}

// ---------------------------------------------------------------------------
// Budget probe
// ---------------------------------------------------------------------------

int runBudget(const Options& options) {
  const MasterTape tape = makeMasterTape(options.seed_start, 200);
  FairPlannerConfig config;
  config.objective = Objective::kClears;
  config.horizon = options.horizon;
  config.samples = options.samples;
  config.limits.max_seconds = options.time_limit;

  Mulberry32 sampler(options.sampler_seed);
  State state = masterStartState(tape);
  LatentBoard latent = tape.start_latent;
  int rises = 0;
  double total = 0.0;
  std::int64_t nodes = 0;
  int measured = 0;

  for (int move = 0; move < options.decisions; ++move) {
    if (state.game_over) break;
    const auto started = std::chrono::steady_clock::now();
    const LoggedDecision decision =
        loggedDecision(config, tape, state, latent, move, rises, sampler);
    const double elapsed = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
    if (decision.column < 0) break;
    total += elapsed;
    nodes += decision.nodes;
    ++measured;
    std::printf("  move %2d  occupied %2d  %.4f s  %lld nodes\n", move,
                occupiedCellCount(state.board), elapsed,
                static_cast<long long>(decision.nodes));
    MoveResult result;
    LatentBoard next_latent{};
    if (!playMasterMove(tape, state, latent, move, rises, decision.column,
                        result, next_latent)) {
      break;
    }
    if (result.level_advanced) ++rises;
    state = result.state;
    latent = next_latent;
  }
  if (measured == 0) {
    std::printf("no decisions measured\n");
    return 1;
  }
  const double per_decision = total / measured;
  std::printf(
      "\nH=%d K=%d: %.4f s per decision (%d decisions, %lld nodes)\n"
      "  one 200-move game: %.1f s single-threaded\n"
      "  32 games on 10 threads: %.1f min (at 200 moves each)\n",
      options.horizon, options.samples, per_decision, measured,
      static_cast<long long>(nodes), per_decision * 200.0,
      per_decision * 200.0 * 32.0 / 10.0 / 60.0);
  return 0;
}

// ---------------------------------------------------------------------------
// Self-test: the information boundary, on THIS program's decision path
// ---------------------------------------------------------------------------

// Replacing every hidden value under every covered cell AND the entire future
// disc tape and rise sequence must leave the whole logged value vector
// identical, given the same sampler stream.  This is stronger than
// flow-run's gate, which only checks the argmax.
bool checkVectorIgnoresHiddenState() {
  const MasterTape tape = makeMasterTape(kLeaseStart + 0x100u, 80);
  MasterTape other = makeMasterTape(kLeaseStart + 0x101u, 80);
  other.start_board = tape.start_board;

  FairPlannerConfig config;
  config.objective = Objective::kClears;
  config.horizon = 5;
  config.samples = 4;

  State state = masterStartState(tape);
  LatentBoard latent = tape.start_latent;
  int rises = 0;
  int checked = 0;
  for (int move = 0; move < 30; ++move) {
    if (state.game_over) break;
    LatentBoard fake{};
    for (int index = 0; index < kCellCount; ++index) {
      const std::uint8_t cell = state.board[index];
      if (cell == kSolid || cell == kCracked) {
        fake[index] = static_cast<std::uint8_t>(1 + (latent[index] % kBoardSize));
      }
    }
    Mulberry32 left(kSamplerSeedStart + 0x33u);
    Mulberry32 right(kSamplerSeedStart + 0x33u);
    const LoggedDecision a =
        loggedDecision(config, tape, state, latent, move, rises, left);
    const LoggedDecision b =
        loggedDecision(config, other, state, fake, move, rises, right);
    if (a.column != b.column || a.column < 0) return false;
    for (int column = 0; column < kBoardSize; ++column) {
      const std::size_t slot = static_cast<std::size_t>(column);
      if (a.legal[slot] != b.legal[slot]) return false;
      if (a.counted[slot] != b.counted[slot]) return false;
      if (a.total[slot] != b.total[slot]) return false;
      if (a.immediate[slot] != b.immediate[slot]) return false;
    }
    ++checked;

    MoveResult result;
    LatentBoard next_latent{};
    if (!playMasterMove(tape, state, latent, move, rises, a.column, result,
                        next_latent)) {
      return false;
    }
    if (result.level_advanced) ++rises;
    state = result.state;
    latent = next_latent;
  }
  return checked >= 20;
}

// The logged argmax must be exactly what the frozen `fairDecision` returns, so
// the corpus is a record of the measured planner and not of a re-implementation
// that drifted from it.
bool checkMatchesFrozenPlanner() {
  const MasterTape tape = makeMasterTape(kLeaseStart + 0x102u, 80);
  FairPlannerConfig config;
  config.objective = Objective::kClears;
  config.horizon = 5;
  config.samples = 8;

  State state = masterStartState(tape);
  LatentBoard latent = tape.start_latent;
  int rises = 0;
  int checked = 0;
  for (int move = 0; move < 25; ++move) {
    if (state.game_over) break;
    Mulberry32 left(kSamplerSeedStart + 0x44u);
    Mulberry32 right(kSamplerSeedStart + 0x44u);
    const LoggedDecision mine =
        loggedDecision(config, tape, state, latent, move, rises, left);
    const FairDecision theirs =
        fairDecision(config, tape, state, latent, move, rises, right);
    if (mine.column != theirs.column) return false;
    if (mine.samples_used != theirs.samples_used) return false;
    ++checked;
    MoveResult result;
    LatentBoard next_latent{};
    if (!playMasterMove(tape, state, latent, move, rises, mine.column, result,
                        next_latent)) {
      return false;
    }
    if (result.level_advanced) ++rises;
    state = result.state;
    latent = next_latent;
  }
  return checked >= 20;
}

// The value vector must be complete: every legal column carries a finite value
// and every illegal column carries the -1 sentinel.  Completeness is the whole
// reason this corpus exists.
bool checkSiblingCompleteness() {
  Options options;
  options.horizon = 5;
  options.samples = 4;
  options.max_moves = 30;
  options.seed_start = kLeaseStart + 0x103u;
  const GameOutput game =
      playCorpusGame(options, options.seed_start, kSamplerSeedStart + 0x55u);
  if (game.records.size() < 10) return false;
  for (const RootRecord& record : game.records) {
    int legal = 0;
    for (int column = 0; column < kBoardSize; ++column) {
      const bool is_legal = (record.legal_mask >> column) & 1u;
      const float value = record.value[column];
      if (is_legal) {
        ++legal;
        if (!(value >= 0.0f)) return false;
        if (!(record.immediate[column] >= 0.0f)) return false;
        if (record.immediate[column] > value + 1e-6f) return false;
        const float lo = record.value_lo[column];
        const float hi = record.value_hi[column];
        if (!(lo >= 0.0f) || !(hi >= 0.0f)) return false;
        if (std::fabs(0.5f * (lo + hi) - value) > 1e-3f) return false;
      } else if (value != -1.0f) {
        return false;
      }
    }
    if (legal == 0) return false;
    if (((record.legal_mask >> record.chosen_column) & 1u) == 0) return false;
  }
  return true;
}

// The realised afterstate must be the state the game actually enters when that
// column is the one played.
bool checkAfterstatesAreReal() {
  Options options;
  options.horizon = 5;
  options.samples = 2;
  options.max_moves = 25;
  options.seed_start = kLeaseStart + 0x104u;
  const GameOutput game =
      playCorpusGame(options, options.seed_start, kSamplerSeedStart + 0x66u);
  if (game.records.size() < 8) return false;
  const MasterTape tape = makeMasterTape(options.seed_start, options.max_moves);
  State state = masterStartState(tape);
  LatentBoard latent = tape.start_latent;
  int rises = 0;
  for (const RootRecord& record : game.records) {
    for (int index = 0; index < drop7::distill::kCells; ++index) {
      if (record.board[index] != state.board[index]) return false;
    }
    if (record.next_disc != state.next_disc) return false;
    const int played = record.played_column;
    MoveResult result;
    LatentBoard next_latent{};
    if (!playMasterMove(tape, state, latent, record.move_index, rises, played,
                        result, next_latent)) {
      return false;
    }
    for (int index = 0; index < drop7::distill::kCells; ++index) {
      if (record.after_board[played][index] != result.state.board[index]) {
        return false;
      }
    }
    if (result.level_advanced) ++rises;
    state = result.state;
    latent = next_latent;
    if (state.game_over) break;
  }
  return true;
}

int runSelfTest() {
  struct Check {
    const char* name;
    bool (*run)();
  };
  const Check checks[] = {
      {"INFORMATION BOUNDARY: the whole value vector ignores hidden state",
       checkVectorIgnoresHiddenState},
      {"the logged argmax equals the frozen fair planner's decision",
       checkMatchesFrozenPlanner},
      {"every legal sibling carries a value, illegal ones carry the sentinel",
       checkSiblingCompleteness},
      {"the logged afterstate is the state the game really enters",
       checkAfterstatesAreReal},
  };
  int failures = 0;
  for (const Check& check : checks) {
    const bool ok = check.run();
    std::printf("%-62s %s\n", check.name, ok ? "ok" : "FAILED");
    if (!ok) ++failures;
  }
  std::printf("self-test: %d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Cohort driver
// ---------------------------------------------------------------------------

int runCohort(const Options& options) {
  if (options.seed_start < kLeaseStart ||
      options.seed_start + static_cast<std::uint32_t>(options.games) - 1u >
          kGameSeedEnd) {
    std::cerr << "game seeds outside SEEDLEASE-A52-DISTILL game half\n";
    return 2;
  }
  if (options.sampler_seed < kSamplerSeedStart ||
      options.sampler_seed > kLeaseEnd) {
    std::cerr << "sampler seed outside SEEDLEASE-A52-DISTILL sampler half\n";
    return 2;
  }

  std::vector<GameOutput> results(static_cast<std::size_t>(options.games));
  std::atomic<int> next_game{0};
  std::mutex log;
  const auto started = std::chrono::steady_clock::now();

  const int threads = std::max(1, std::min(options.threads, options.games));
  std::vector<std::thread> pool;
  for (int worker = 0; worker < threads; ++worker) {
    pool.emplace_back([&] {
      for (;;) {
        const int index = next_game.fetch_add(1);
        if (index >= options.games) return;
        const std::uint32_t seed =
            options.seed_start + static_cast<std::uint32_t>(index);
        const std::uint32_t sampler =
            options.sampler_seed + static_cast<std::uint32_t>(index);
        GameOutput game = playCorpusGame(options, seed, sampler);
        {
          std::lock_guard<std::mutex> guard(log);
          std::printf(
              "game 0x%08x  moves %4d  score %9lld  clears/move %.4f  "
              "reveals/move %.4f  roots %zu  %.1f s\n",
              seed, game.stat.moves, static_cast<long long>(game.stat.score),
              game.stat.clearsPerMove(), game.stat.revealsPerMove(),
              game.records.size(), game.decision_seconds);
          std::fflush(stdout);
        }
        results[static_cast<std::size_t>(index)] = std::move(game);
      }
    });
  }
  for (std::thread& thread : pool) thread.join();
  const double wall =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();

  // ---- corpus ------------------------------------------------------------
  std::size_t rows = 0;
  if (!options.out.empty()) {
    std::ofstream file(options.out, std::ios::binary);
    if (!file) {
      std::cerr << "cannot open " << options.out << "\n";
      return 1;
    }
    for (const GameOutput& game : results) {
      file.write(reinterpret_cast<const char*>(game.records.data()),
                 static_cast<std::streamsize>(game.records.size() *
                                              sizeof(RootRecord)));
      rows += game.records.size();
    }
  } else {
    for (const GameOutput& game : results) rows += game.records.size();
  }

  if (!options.jsonl.empty()) {
    std::ofstream file(options.jsonl);
    if (!file) {
      std::cerr << "cannot open " << options.jsonl << "\n";
      return 1;
    }
    for (const GameOutput& game : results) {
      file << gameStatJson(game.stat) << "\n";
    }
  }

  // ---- teacher strength, measured on the run that produced the corpus -----
  std::int64_t moves = 0;
  std::int64_t cleared = 0;
  std::int64_t revealed = 0;
  std::int64_t score = 0;
  std::int64_t occupancy_sum = 0;
  int censored = 0;
  int identity = 0;
  int incomplete = 0;
  double slope_sum = 0.0;
  int slope_games = 0;
  double decision_seconds = 0.0;
  int decisions = 0;
  std::int64_t steady_moves = 0;
  std::int64_t steady_cleared = 0;
  std::vector<int> lifetimes;

  for (const GameOutput& game : results) {
    moves += game.stat.moves;
    cleared += game.stat.cleared;
    revealed += game.stat.revealed;
    score += game.stat.score;
    if (game.stat.censored) ++censored;
    identity += game.stat.identity_violations;
    incomplete += game.stat.incomplete_windows;
    decision_seconds += game.decision_seconds;
    decisions += game.decisions;
    lifetimes.push_back(game.stat.moves);
    for (int value : game.stat.move_occupancy) occupancy_sum += value;
    if (game.stat.cycle_occupancy.size() >= 4) {
      slope_sum += occupancySlope(game.stat.cycle_occupancy, 1);
      ++slope_games;
    }
    // Steady state excludes the sparse opening (finding-06 section 3): the
    // conservation law is about the operating point, not the first five cycles.
    for (std::size_t index = 25; index < game.records.size(); ++index) {
      ++steady_moves;
      steady_cleared += game.records[index].after_clears[
          game.records[index].played_column];
    }
  }

  std::sort(lifetimes.begin(), lifetimes.end());
  const double move_total = static_cast<double>(std::max<std::int64_t>(moves, 1));
  std::printf(
      "\n=== teacher: fair receding-horizon planner, H=%d K=%d, %d games ===\n",
      options.horizon, options.samples, options.games);
  std::printf("moves: mean %.2f  median %d  min %d  max %d  censored %d\n",
              moves / static_cast<double>(options.games),
              lifetimes[lifetimes.size() / 2], lifetimes.front(),
              lifetimes.back(), censored);
  std::printf("score: mean %.1f\n", score / static_cast<double>(options.games));
  std::printf(
      "flow (pooled): clears/move %.4f of 2.4000 (%.1f%%), reveals/move %.4f "
      "of 1.4000 (%.1f%%)\n",
      cleared / move_total, 100.0 * (cleared / move_total) / 2.4,
      revealed / move_total, 100.0 * (revealed / move_total) / 1.4);
  if (steady_moves > 0) {
    std::printf("flow (steady, moves 26+): clears/move %.4f\n",
                static_cast<double>(steady_cleared) / steady_moves);
  }
  std::printf("occupancy: mean %.2f cells, slope %.3f cells per cycle (%d games)\n",
              occupancy_sum / move_total,
              slope_games > 0 ? slope_sum / slope_games : 0.0, slope_games);
  std::printf("checks: identity violations %d, incomplete windows %d\n",
              identity, incomplete);
  std::printf("corpus: %zu roots, %.1f MiB\n", rows,
              rows * sizeof(RootRecord) / 1048576.0);
  std::printf("cost: %.4f s per decision, %.1f s wall on %d threads\n",
              decisions > 0 ? decision_seconds / decisions : 0.0, wall, threads);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string flag = argv[index];
    auto next = [&]() { return std::string(argv[++index]); };
    if (flag == "--self-test") {
      options.self_test = true;
    } else if (flag == "--budget") {
      options.budget = true;
    } else if (flag == "--games" && index + 1 < argc) {
      options.games = std::atoi(next().c_str());
    } else if (flag == "--seed-start" && index + 1 < argc) {
      options.seed_start =
          static_cast<std::uint32_t>(std::strtoul(next().c_str(), nullptr, 0));
    } else if (flag == "--sampler-seed" && index + 1 < argc) {
      options.sampler_seed =
          static_cast<std::uint32_t>(std::strtoul(next().c_str(), nullptr, 0));
    } else if (flag == "--horizon" && index + 1 < argc) {
      options.horizon = std::atoi(next().c_str());
    } else if (flag == "--samples" && index + 1 < argc) {
      options.samples = std::atoi(next().c_str());
    } else if (flag == "--max-moves" && index + 1 < argc) {
      options.max_moves = std::atoi(next().c_str());
    } else if (flag == "--threads" && index + 1 < argc) {
      options.threads = std::atoi(next().c_str());
    } else if (flag == "--decisions" && index + 1 < argc) {
      options.decisions = std::atoi(next().c_str());
    } else if (flag == "--epsilon" && index + 1 < argc) {
      options.epsilon = std::atof(next().c_str());
    } else if (flag == "--time-limit" && index + 1 < argc) {
      options.time_limit = std::atof(next().c_str());
    } else if (flag == "--out" && index + 1 < argc) {
      options.out = next();
    } else if (flag == "--jsonl" && index + 1 < argc) {
      options.jsonl = next();
    } else {
      std::cerr << "unknown argument " << flag << "\n";
      return 2;
    }
  }
  if (options.self_test) return runSelfTest();
  if (options.budget) return runBudget(options);
  return runCohort(options);
}
