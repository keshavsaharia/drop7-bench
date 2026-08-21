// flow-run - receding-horizon clairvoyant play, and what it does to disc flow.
//
//   flow-run --self-test
//   flow-run --cross-check <suite.jsonl> [--limit N]
//   flow-run --policy <rh-points|rh-clears|fair-d4|lowest-column>
//            --games N --seed-start 0xa5230000 [--horizon 9] [--commit 1]
//            [--max-moves 300] [--threads 8] [--time-limit S] [--node-limit N]
//            [--jsonl out.jsonl]
//
// The question
// ------------
// `docs/exploratory/finding-01-score-is-survival.md` derives an exact
// conservation requirement: twelve discs (five placed, seven risen) enter a
// 49-cell board every five moves, so indefinite survival needs at least 2.400
// numbered clears and 1.400 covered reveals per move.  Fair depth-4 sustains
// 1.9489 / 1.0697 and dies.  Is 2.400 reachable by *any* line of play?
//
// What this measures, and what it does not
// ----------------------------------------
// A nine-move exact optimum cannot answer that, because nine moves is under two
// rise cycles: a nine-move line can build height it never has to pay for, so
// its flow rate is an over-estimate of anything sustainable.  This tool instead
// plays a *whole game* under receding-horizon clairvoyant control: at every
// move it cuts an exactly-solvable window out of a fixed future, solves it
// exactly, plays the first `--commit` moves of the optimal line, and repeats.
//
// The future is fixed once, at the start, as a `MasterTape`.  It is never
// redrawn when the policy deviates, so the planner is genuinely clairvoyant
// over its window and never gets a second draw.
//
//   * This is a LOWER bound on sustainable optimal flow.  A globally optimal
//     player is at least as good as one that re-plans every move with a
//     nine-move exact lookahead.
//   * It is NOT an upper bound, and it is NOT globally optimal.  A flow rate
//     measured here that falls short of 2.400 does not by itself prove 2.400 is
//     unreachable; it proves that nine-move exact lookahead does not reach it.
//   * `--horizon` sweeps the lookahead so the reader can see whether the
//     measured rate is still climbing with H or has flattened.
//
// Two objectives are provided because the score objective is not the flow
// objective.  `rh-points` maximizes score over the window, exactly what
// `scenario/solver.hpp` optimizes.  `rh-clears` maximizes numbered discs
// cleared over the window, which is the quantity the conservation law is about
// and is therefore the more direct probe of the ceiling.
//
// Public-information boundary: `rh-points` and `rh-clears` READ THE HIDDEN
// BOARD.  They are diagnostics and teachers, never deployable policies.
// `fair-d4` and `lowest-column` read only public state and are the controls.

#include "fair-only-depth4-noentry.cpp"

#include "fair-planner.hpp"
#include "flow-common.hpp"
#include "flow-solver.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
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
namespace d4 = drop7::fair_only_depth4;

// The exploratory leases assigned to this work.  Nothing outside them is
// opened.  Game seeds come from FLOW, so that every cohort in finding-06 and
// finding-07 plays the *same* master tapes and every comparison is paired.
// The fair planner's hidden-board sampler is a separate stream and draws from
// FLOW2, so sampling randomness can never collide with game randomness.
constexpr std::uint32_t kLeaseStart = 0xa523'0000u;      // SEEDLEASE-A52-FLOW
constexpr std::uint32_t kLeaseEnd = 0xa523'3fffu;
// SEEDLEASE-A52-FLOW3 is partitioned so a master-tape seed and a sampler seed
// can never collide: game tapes take 0xa5239000-0xa5239fff, samplers take
// 0xa523a000-0xa523bfff.
constexpr std::uint32_t kLease3TapeStart = 0xa523'9000u;
constexpr std::uint32_t kLease3TapeEnd = 0xa523'9fffu;
constexpr std::uint32_t kSamplerLeaseStart = 0xa523'4000u;  // ...-FLOW2
constexpr std::uint32_t kSamplerLeaseEnd = 0xa523'bfffu;    // ...-FLOW3 end
// FLOW2 (0xa5234000-0xa5237fff) carries the original K series; FLOW3
// (0xa5238000-0xa523bfff) carries everything introduced after it.  A new K in
// an existing series deliberately keeps its series' sampler base so the series
// stays a series.

// ---------------------------------------------------------------------------
// Choosers
// ---------------------------------------------------------------------------

// Frozen fair depth-4, public state only.
struct FairDepth4Chooser {
  int operator()(const State& state, const LatentBoard&, int, int) const {
    if (state.game_over) return -1;
    const d4::SearchDecision decision = d4::chooseDepth4Action(state);
    return decision.action;
  }
};

// Trivial public control from `generate.hpp`.
struct LowestColumnChooser {
  int operator()(const State& state, const LatentBoard&, int, int) const {
    return lowestColumnPolicyLocal(state.board);
  }
  static int lowestColumnPolicyLocal(const Board& board) {
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
};

// Receding-horizon clairvoyant control.  Privileged: reads `latent`.
struct RecedingChooser {
  const MasterTape* tape = nullptr;
  Objective objective = Objective::kPoints;
  int horizon = 9;
  int commit = 1;
  int min_horizon = 5;
  WindowLimits limits{};
  GameStat* stats = nullptr;

  std::vector<int> pending;
  std::size_t pending_at = 0;

  int operator()(const State& state, const LatentBoard& latent, int move,
                 int rises) {
    if (pending_at < pending.size()) return pending[pending_at++];
    pending.clear();
    pending_at = 0;

    for (int attempt_horizon = horizon; attempt_horizon >= min_horizon;
         attempt_horizon -= 2) {
      Scenario window;
      std::string reason;
      if (!cutWindow(*tape, state, latent, move, rises, attempt_horizon, window,
                     reason)) {
        std::fprintf(stderr, "window cut failed at move %d: %s\n", move,
                     reason.c_str());
        return -1;
      }
      const WindowResult solved = solveWindow(window, objective, limits);
      stats->solver_nodes += solved.nodes;
      stats->solve_seconds += solved.wall_seconds;
      if (!solved.complete) {
        ++stats->incomplete_windows;
        continue;
      }
      // Replay the whole optimal line through the engine and require that it
      // really earns the reported value.  Cheap, and it turns a solver bug into
      // a reported number rather than a silent wrong answer.
      if (!verifyLine(window, solved)) ++stats->pv_mismatches;
      if (solved.pv.empty()) return -1;
      const int take =
          std::min(static_cast<int>(solved.pv.size()), std::max(1, commit));
      pending.assign(solved.pv.begin(), solved.pv.begin() + take);
      return pending[pending_at++];
    }
    // Every horizon exhausted its budget.  Reported, not papered over.
    return -1;
  }

  bool verifyLine(const Scenario& window, const WindowResult& solved) const {
    auto engine = makeScenarioEngine(window);
    std::int64_t total = 0;
    for (int column : solved.pv) {
      if (engine.state().game_over) break;
      MoveResult result;
      if (!engine.play(column, result)) return false;
      total += moveValue(objective, result);
      if (engine.state().game_over) break;
    }
    return total == solved.value;
  }
};

// Receding-horizon planner that does not read the hidden board.  See
// `fair-planner.hpp` for what each configuration is and is not allowed to know.
struct FairRecedingChooser {
  const MasterTape* tape = nullptr;
  FairPlannerConfig config{};
  // Optional warm-up configuration.  Moves before `warm_moves` are decided with
  // `warm_config`; from `warm_moves` onward `config` takes over and only those
  // moves are measured.  Two arms sharing a warm-up reach a bit-identical state
  // before they diverge, which is what makes a matched-horizon comparison at a
  // realistic mid-game occupancy affordable.
  FairPlannerConfig warm_config{};
  int warm_moves = 0;
  std::uint32_t sampler_seed = kSamplerLeaseStart;
  GameStat* stats = nullptr;
  Mulberry32 random{kSamplerLeaseStart};

  void reset() { random = Mulberry32(sampler_seed); }

  int operator()(const State& state, const LatentBoard& hidden, int move,
                 int rises) {
    const FairPlannerConfig& active =
        move < warm_moves ? warm_config : config;
    const FairDecision decision = fairDecision(active, *tape, state, hidden,
                                               move, rises, random);
    stats->solver_nodes += decision.nodes;
    stats->solve_seconds += decision.wall_seconds;
    stats->incomplete_windows += decision.incomplete;
    return decision.column;
  }
};

// ---------------------------------------------------------------------------
// Aggregation and reporting
// ---------------------------------------------------------------------------

double median(std::vector<double> values) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const std::size_t half = values.size() / 2;
  if (values.size() % 2 == 1) return values[half];
  return 0.5 * (values[half - 1] + values[half]);
}

double meanOf(const std::vector<double>& values) {
  if (values.empty()) return 0.0;
  double total = 0.0;
  for (double value : values) total += value;
  return total / static_cast<double>(values.size());
}

void report(const std::string& label, const std::vector<GameStat>& games) {
  std::printf("\n=== %s ===\n", label.c_str());
  if (games.empty()) {
    std::printf("no games\n");
    return;
  }
  std::int64_t moves = 0;
  std::int64_t cleared = 0;
  std::int64_t revealed = 0;
  std::int64_t score = 0;
  std::int64_t chain = 0;
  std::int64_t rise = 0;
  std::int64_t clear_points = 0;
  int rises = 0;
  int clear_moves = 0;
  int clear_awards = 0;
  int double_awards = 0;
  int fifth_drop = 0;
  int censored = 0;
  int identity = 0;
  int incomplete = 0;
  int mismatches = 0;
  int deepest = 0;
  double solve_seconds = 0.0;
  std::int64_t nodes = 0;
  std::array<std::int64_t, kMaxWaveDepth> depth_count{};
  std::array<std::int64_t, kMaxWaveDepth> depth_cleared{};
  std::vector<double> move_list;
  std::vector<double> score_list;
  std::vector<double> clears_list;
  std::vector<double> reveals_list;
  std::vector<double> slope_list;
  std::size_t max_cycles = 0;

  for (const GameStat& game : games) {
    moves += game.moves;
    cleared += game.cleared;
    revealed += game.revealed;
    score += game.score;
    chain += game.chain_points;
    rise += game.rise_points;
    clear_points += game.clear_points;
    rises += game.rises;
    clear_moves += game.clear_moves;
    clear_awards += game.clear_awards;
    double_awards += game.double_clear_awards;
    fifth_drop += game.fifth_drop_clears;
    if (game.censored) ++censored;
    identity += game.identity_violations;
    incomplete += game.incomplete_windows;
    mismatches += game.pv_mismatches;
    deepest = std::max(deepest, game.max_wave_depth);
    solve_seconds += game.solve_seconds;
    nodes += game.solver_nodes;
    for (int depth = 0; depth < kMaxWaveDepth; ++depth) {
      depth_count[static_cast<std::size_t>(depth)] +=
          game.wave_depth_count[static_cast<std::size_t>(depth)];
      depth_cleared[static_cast<std::size_t>(depth)] +=
          game.wave_depth_cleared[static_cast<std::size_t>(depth)];
    }
    move_list.push_back(game.moves);
    score_list.push_back(static_cast<double>(game.score));
    clears_list.push_back(game.clearsPerMove());
    reveals_list.push_back(game.revealsPerMove());
    slope_list.push_back(occupancySlope(game.cycle_occupancy, 1));
    max_cycles = std::max(max_cycles, game.cycle_occupancy.size());
  }

  std::printf("games %zu, censored (hit the move cap alive) %d\n", games.size(),
              censored);
  std::printf("moves: mean %.2f  median %.1f  min %.0f  max %.0f\n",
              meanOf(move_list), median(move_list),
              *std::min_element(move_list.begin(), move_list.end()),
              *std::max_element(move_list.begin(), move_list.end()));
  std::printf("score: mean %.1f  median %.1f\n", meanOf(score_list),
              median(score_list));
  const double move_total = static_cast<double>(moves);
  std::printf(
      "flow (pooled): clears/move %.4f of 2.4000 required (%.1f%%), "
      "reveals/move %.4f of 1.4000 required (%.1f%%)\n",
      cleared / move_total, 100.0 * (cleared / move_total) / 2.4,
      revealed / move_total, 100.0 * (revealed / move_total) / 1.4);
  std::printf("flow (per-game mean): clears/move %.4f  reveals/move %.4f\n",
              meanOf(clears_list), meanOf(reveals_list));
  std::printf("score composition: rise %.2f%%  boardClear %.2f%%  chain %.2f%%\n",
              score > 0 ? 100.0 * static_cast<double>(rise) / score : 0.0,
              score > 0 ? 100.0 * static_cast<double>(clear_points) / score : 0.0,
              score > 0 ? 100.0 * static_cast<double>(chain) / score : 0.0);
  std::printf(
      "rises %d, board-clear moves %d, 70k awards %d, double awards %d, "
      "fifth-drop clears %d\n",
      rises, clear_moves, clear_awards, double_awards, fifth_drop);
  std::printf(
      "checks: score-identity violations %d, incomplete windows %d, pv "
      "mismatches %d\n",
      identity, incomplete, mismatches);
  std::printf("solver: %lld nodes, %.1f s of window solving\n",
              static_cast<long long>(nodes), solve_seconds);

  std::int64_t waves = 0;
  for (std::int64_t count : depth_count) waves += count;
  std::printf("waves %lld, deepest %d\n", static_cast<long long>(waves),
              deepest);
  std::printf("depth  waves    share    discs\n");
  for (int depth = 1; depth < kMaxWaveDepth; ++depth) {
    const std::int64_t count = depth_count[static_cast<std::size_t>(depth)];
    if (count == 0) continue;
    std::printf("%5d  %6lld  %6.2f%%  %7lld\n", depth,
                static_cast<long long>(count),
                waves > 0 ? 100.0 * static_cast<double>(count) / waves : 0.0,
                static_cast<long long>(
                    depth_cleared[static_cast<std::size_t>(depth)]));
  }

  std::printf("occupancy after each rise (cells occupied of 49):\n");
  std::printf("cycle  games  mean occupied  mean covered\n");
  for (std::size_t cycle = 0; cycle < max_cycles; ++cycle) {
    double occupied = 0.0;
    double covered = 0.0;
    int count = 0;
    for (const GameStat& game : games) {
      if (cycle >= game.cycle_occupancy.size()) continue;
      occupied += game.cycle_occupancy[cycle];
      covered += game.cycle_covered[cycle];
      ++count;
    }
    if (count == 0) continue;
    std::printf("%5zu  %5d  %13.2f  %12.2f\n", cycle + 1, count,
                occupied / count, covered / count);
  }
  std::printf(
      "occupancy slope (cells per five-move cycle, from cycle 2): mean %.4f  "
      "median %.4f\n",
      meanOf(slope_list), median(slope_list));
}

// ---------------------------------------------------------------------------
// Run modes
// ---------------------------------------------------------------------------

struct Options {
  std::string policy = "rh-points";
  std::string jsonl_output;
  std::string cross_check;
  int games = 8;
  std::uint32_t seed_start = kLeaseStart;
  int horizon = 9;
  int commit = 1;
  int min_horizon = 5;
  int max_moves = 300;
  int threads = 8;
  int limit = 0;
  double time_limit = 60.0;
  std::int64_t node_limit = 0;
  int samples = 8;
  int sample_threads = 1;
  int warm_horizon = 0;
  int warm_samples = 0;
  int warm_moves = 0;
  bool latent_known = false;
  bool tape_known = false;
  std::uint32_t sampler_seed = kSamplerLeaseStart;
  bool self_test = false;
};

GameStat playOneGame(const Options& options, std::uint32_t seed) {
  const MasterTape tape = makeMasterTape(seed, options.max_moves);
  GameStat game;
  game.seed = seed;
  game.policy = options.policy;
  if (options.policy == "fair-d4") {
    FairDepth4Chooser chooser;
    playLongGame(tape, chooser, options.max_moves, game, nullptr);
  } else if (options.policy == "lowest-column") {
    LowestColumnChooser chooser;
    playLongGame(tape, chooser, options.max_moves, game, nullptr);
  } else if (options.policy == "fair-rh") {
    FairRecedingChooser chooser;
    chooser.tape = &tape;
    chooser.config.objective = Objective::kClears;
    chooser.config.horizon = options.horizon;
    chooser.config.samples = options.samples;
    chooser.config.latent_known = options.latent_known;
    chooser.config.tape_known = options.tape_known;
    chooser.config.sample_threads = options.sample_threads;
    chooser.config.limits.max_seconds = options.time_limit;
    chooser.config.limits.max_nodes = options.node_limit;
    chooser.warm_config = chooser.config;
    chooser.warm_moves = options.warm_moves;
    if (options.warm_horizon > 0) chooser.warm_config.horizon = options.warm_horizon;
    if (options.warm_samples > 0) chooser.warm_config.samples = options.warm_samples;
    // One independent sampler stream per game, inside SEEDLEASE-A52-FLOW2.
    chooser.sampler_seed = options.sampler_seed + (seed - options.seed_start);
    chooser.stats = &game;
    chooser.reset();
    game.measure_from = options.warm_moves;
    playLongGame(tape, chooser, options.max_moves, game, nullptr,
                 options.warm_moves);
  } else {
    RecedingChooser chooser;
    chooser.tape = &tape;
    chooser.objective = options.policy == "rh-clears" ? Objective::kClears
                                                      : Objective::kPoints;
    chooser.horizon = options.horizon;
    chooser.commit = options.commit;
    chooser.min_horizon = std::min(options.min_horizon, options.horizon);
    chooser.limits.max_seconds = options.time_limit;
    chooser.limits.max_nodes = options.node_limit;
    chooser.stats = &game;
    playLongGame(tape, chooser, options.max_moves, game, nullptr);
  }
  return game;
}

int runGames(const Options& options) {
  const std::uint32_t seed_end =
      options.seed_start + static_cast<std::uint32_t>(options.games) - 1;
  const bool in_flow =
      options.seed_start >= kLeaseStart && seed_end <= kLeaseEnd;
  const bool in_flow3_tapes =
      options.seed_start >= kLease3TapeStart && seed_end <= kLease3TapeEnd;
  if (!in_flow && !in_flow3_tapes) {
    std::cerr << "game seed range outside SEEDLEASE-A52-FLOW "
              << "(0xa5230000-0xa5233fff) and the FLOW3 tape partition "
              << "(0xa5239000-0xa5239fff)\n";
    return 2;
  }
  if (options.policy == "fair-rh" &&
      (options.sampler_seed < kSamplerLeaseStart ||
       options.sampler_seed + static_cast<std::uint32_t>(options.games) - 1 >
           kSamplerLeaseEnd)) {
    std::cerr << "sampler seed range outside SEEDLEASE-A52-FLOW2/FLOW3 ("
              << "0xa5234000-0xa523bfff)\n";
    return 2;
  }
  std::vector<GameStat> games(static_cast<std::size_t>(options.games));
  std::atomic<int> next{0};
  std::mutex log;
  const int threads = std::max(1, std::min(options.threads, options.games));
  std::vector<std::thread> pool;
  for (int worker = 0; worker < threads; ++worker) {
    pool.emplace_back([&] {
      for (;;) {
        const int index = next.fetch_add(1);
        if (index >= options.games) return;
        const std::uint32_t seed =
            options.seed_start + static_cast<std::uint32_t>(index);
        GameStat game = playOneGame(options, seed);
        games[static_cast<std::size_t>(index)] = game;
        std::lock_guard<std::mutex> guard(log);
        std::printf(
            "seed %08x moves %4d score %10lld clears/move %.4f reveals/move "
            "%.4f occ_end %2d %s\n",
            seed, game.moves, static_cast<long long>(game.score),
            game.clearsPerMove(), game.revealsPerMove(),
            game.move_occupancy.empty() ? 0 : game.move_occupancy.back(),
            game.censored ? "CENSORED" : "died");
        std::fflush(stdout);
      }
    });
  }
  for (std::thread& thread : pool) thread.join();

  if (!options.jsonl_output.empty()) {
    std::ofstream jsonl(options.jsonl_output);
    if (!jsonl) {
      std::cerr << "cannot write " << options.jsonl_output << "\n";
      return 2;
    }
    for (const GameStat& game : games) jsonl << gameStatJson(game) << "\n";
  }

  char label[256];
  if (options.policy == "fair-rh") {
    std::snprintf(label, sizeof(label),
                  "fair-rh  H=%d K=%d latentKnown=%d tapeKnown=%d cap=%d "
                  "warm=%d(H%d,K%d)  %d games",
                  options.horizon, options.samples, options.latent_known ? 1 : 0,
                  options.tape_known ? 1 : 0, options.max_moves,
                  options.warm_moves,
                  options.warm_horizon > 0 ? options.warm_horizon
                                           : options.horizon,
                  options.warm_samples > 0 ? options.warm_samples
                                           : options.samples,
                  options.games);
  } else {
    std::snprintf(label, sizeof(label), "%s  H=%d commit=%d cap=%d  %d games",
                  options.policy.c_str(), options.horizon, options.commit,
                  options.max_moves, options.games);
  }
  report(label, games);
  return 0;
}

// The correctness gate for `flow-solver.hpp`: with the points objective it must
// reproduce the frozen solver's optimum on every scenario of a suite.
int runCrossCheck(const Options& options) {
  std::vector<Scenario> scenarios;
  std::string error;
  if (!loadScenarioFile(options.cross_check, scenarios, error)) {
    std::cerr << error << "\n";
    return 2;
  }
  if (options.limit > 0 &&
      static_cast<int>(scenarios.size()) > options.limit) {
    scenarios.resize(static_cast<std::size_t>(options.limit));
  }
  std::atomic<int> next{0};
  std::atomic<int> mismatches{0};
  std::atomic<int> incomplete{0};
  std::mutex log;
  const int threads = std::max(1, options.threads);
  std::vector<std::thread> pool;
  for (int worker = 0; worker < threads; ++worker) {
    pool.emplace_back([&] {
      for (;;) {
        const int index = next.fetch_add(1);
        if (index >= static_cast<int>(scenarios.size())) return;
        const Scenario& scenario = scenarios[static_cast<std::size_t>(index)];
        SolveOptions frozen_options;
        frozen_options.threads = 1;
        const SolveResult frozen = solveScenario(scenario, frozen_options);
        WindowLimits limits;
        limits.max_seconds = 0.0;
        const WindowResult mine =
            solveWindow(scenario, Objective::kPoints, limits);
        if (!frozen.complete || !mine.complete) {
          incomplete.fetch_add(1);
          continue;
        }
        if (frozen.optimum != mine.value) {
          mismatches.fetch_add(1);
          std::lock_guard<std::mutex> guard(log);
          std::printf("MISMATCH %s frozen=%lld flow=%lld\n", scenario.id,
                      static_cast<long long>(frozen.optimum),
                      static_cast<long long>(mine.value));
        }
      }
    });
  }
  for (std::thread& thread : pool) thread.join();
  std::printf("cross-check: %zu scenarios, %d mismatches, %d incomplete\n",
              scenarios.size(), mismatches.load(), incomplete.load());
  return mismatches.load() == 0 ? 0 : 1;
}

bool checkMasterTapeDeterminism() {
  const MasterTape left = makeMasterTape(kLeaseStart, 40);
  const MasterTape right = makeMasterTape(kLeaseStart, 40);
  if (left.discs != right.discs) return false;
  if (left.start_latent != right.start_latent) return false;
  for (std::size_t index = 0; index < left.rises.size(); ++index) {
    if (left.rises[index] != right.rises[index]) return false;
  }
  return true;
}

// A window cut out of the long game must reproduce the long game exactly when
// played with the same columns: same boards, same score deltas, same waves.
bool checkWindowMatchesLongGame() {
  const MasterTape tape = makeMasterTape(kLeaseStart + 1u, 60);
  State state = masterStartState(tape);
  LatentBoard latent = tape.start_latent;
  int rises = 0;
  for (int move = 0; move < 24; ++move) {
    if (state.game_over) return true;
    Scenario window;
    std::string reason;
    if (!cutWindow(tape, state, latent, move, rises, 6, window, reason)) {
      std::printf("self-test: window cut failed: %s\n", reason.c_str());
      return false;
    }
    auto engine = makeScenarioEngine(window);
    State long_state = state;
    LatentBoard long_latent = latent;
    int long_rises = rises;
    for (int step = 0; step < 6; ++step) {
      if (long_state.game_over) break;
      const int column =
          LowestColumnChooser::lowestColumnPolicyLocal(long_state.board);
      if (column < 0) break;
      MoveResult window_result;
      if (!engine.play(column, window_result)) return false;
      MoveResult long_result;
      LatentBoard next_latent{};
      if (!playMasterMove(tape, long_state, long_latent, move + step,
                          long_rises, column, long_result, next_latent)) {
        return false;
      }
      if (window_result.state.board != long_result.state.board) return false;
      if (window_result.score_delta != long_result.score_delta) return false;
      if (window_result.waves.size() != long_result.waves.size()) return false;
      if (engine.latent() != next_latent) return false;
      if (long_result.level_advanced) ++long_rises;
      long_state = long_result.state;
      long_latent = next_latent;
    }
    const int column = LowestColumnChooser::lowestColumnPolicyLocal(state.board);
    if (column < 0) return true;
    MoveResult result;
    LatentBoard next_latent{};
    if (!playMasterMove(tape, state, latent, move, rises, column, result,
                        next_latent)) {
      return false;
    }
    if (result.level_advanced) ++rises;
    state = result.state;
    latent = next_latent;
  }
  return true;
}

// The score identity of finding-01, checked move by move on a real game:
// delta = 17,000 x rise + 70,000 x clears + sum of wave points.
bool checkScoreIdentity() {
  Options options;
  options.policy = "lowest-column";
  options.max_moves = 200;
  const GameStat game = playOneGame(options, kLeaseStart + 2u);
  if (game.moves < 10) return false;
  if (game.identity_violations != 0) return false;
  return game.score ==
         game.rise_points + game.clear_points + game.chain_points;
}

// The clears objective must dominate the points objective on clears, and the
// points objective must dominate the clears objective on points.  Both are
// exact optima of their own objective, so anything else is a solver bug.
bool checkObjectiveDominance() {
  const MasterTape tape = makeMasterTape(kLeaseStart + 3u, 40);
  State state = masterStartState(tape);
  LatentBoard latent = tape.start_latent;
  int rises = 0;
  for (int move = 0; move < 8; ++move) {
    Scenario window;
    std::string reason;
    if (!cutWindow(tape, state, latent, move, rises, 6, window, reason)) {
      return false;
    }
    WindowLimits limits;
    const WindowResult points = solveWindow(window, Objective::kPoints, limits);
    const WindowResult clears = solveWindow(window, Objective::kClears, limits);
    if (!points.complete || !clears.complete) return false;

    const auto replay = [&window](const std::vector<int>& path,
                                  std::int64_t& score, std::int64_t& cleared) {
      auto engine = makeScenarioEngine(window);
      score = 0;
      cleared = 0;
      for (int column : path) {
        if (engine.state().game_over) break;
        MoveResult result;
        if (!engine.play(column, result)) break;
        score += result.score_delta;
        for (const Wave& wave : result.waves) cleared += wave.cleared;
      }
    };
    std::int64_t points_score = 0;
    std::int64_t points_cleared = 0;
    std::int64_t clears_score = 0;
    std::int64_t clears_cleared = 0;
    replay(points.pv, points_score, points_cleared);
    replay(clears.pv, clears_score, clears_cleared);
    if (points_score != points.value) return false;
    if (clears_cleared != clears.value) return false;
    if (clears_cleared < points_cleared) return false;
    if (points_score < clears_score) return false;

    const int column = LowestColumnChooser::lowestColumnPolicyLocal(state.board);
    if (column < 0) return true;
    MoveResult result;
    LatentBoard next_latent{};
    if (!playMasterMove(tape, state, latent, move, rises, column, result,
                        next_latent)) {
      return false;
    }
    if (result.level_advanced) ++rises;
    state = result.state;
    latent = next_latent;
    if (state.game_over) return true;
  }
  return true;
}

// runRoot must agree with run(): the best root value is the window optimum.
// This validates the machinery the fair planner is built on against the same
// solver that `--cross-check` proves equal to the frozen exact solver.
bool checkRootValuesMatchOptimum() {
  const MasterTape tape = makeMasterTape(kLeaseStart + 4u, 60);
  State state = masterStartState(tape);
  LatentBoard latent = tape.start_latent;
  int rises = 0;
  for (int move = 0; move < 20; ++move) {
    if (state.game_over) return true;
    Scenario window;
    std::string reason;
    if (!cutWindow(tape, state, latent, move, rises, 6, window, reason)) {
      return false;
    }
    WindowLimits limits;
    for (Objective objective : {Objective::kPoints, Objective::kClears}) {
      const WindowResult whole = solveWindow(window, objective, limits);
      const RootResult root = solveWindowRoot(window, objective, limits);
      if (!whole.complete || !root.complete) return false;
      std::int64_t best = 0;
      bool any = false;
      for (int column = 0; column < kBoardSize; ++column) {
        if (!root.legal[static_cast<std::size_t>(column)]) continue;
        const std::int64_t value = root.value[static_cast<std::size_t>(column)];
        if (!any || value > best) {
          best = value;
          any = true;
        }
      }
      if (!any || best != whole.value) return false;
    }
    const int column = LowestColumnChooser::lowestColumnPolicyLocal(state.board);
    if (column < 0) return true;
    MoveResult result;
    LatentBoard next_latent{};
    if (!playMasterMove(tape, state, latent, move, rises, column, result,
                        next_latent)) {
      return false;
    }
    if (result.level_advanced) ++rises;
    state = result.state;
    latent = next_latent;
  }
  return true;
}

// THE INFORMATION-BOUNDARY GATE.  With `latent_known = false` the decision must
// be a function of public state alone, so replacing every hidden value under
// every covered cell with a different legal value must not change the chosen
// column.  With `tape_known = false` the same must hold when the entire future
// disc tape and every future risen row are replaced.
bool checkFairPlannerIgnoresHiddenState() {
  const MasterTape tape = makeMasterTape(kLeaseStart + 5u, 80);
  // A second tape with the same start position but different hidden values and
  // a completely different future.
  MasterTape other = makeMasterTape(kLeaseStart + 6u, 80);
  other.start_board = tape.start_board;

  FairPlannerConfig config;
  config.objective = Objective::kClears;
  config.horizon = 5;
  config.samples = 3;
  config.latent_known = false;
  config.tape_known = false;

  State state = masterStartState(tape);
  LatentBoard latent = tape.start_latent;
  int rises = 0;
  int checked = 0;
  for (int move = 0; move < 30; ++move) {
    if (state.game_over) break;

    // Same public state, same sampler stream, two different hidden boards and
    // two different futures.
    LatentBoard fake{};
    for (int index = 0; index < kCellCount; ++index) {
      const std::uint8_t cell = state.board[index];
      if (cell == kSolid || cell == kCracked) {
        fake[index] = static_cast<std::uint8_t>(1 + (latent[index] % kBoardSize));
      }
    }
    Mulberry32 left(kSamplerLeaseStart + 0x20u);
    Mulberry32 right(kSamplerLeaseStart + 0x20u);
    const FairDecision a =
        fairDecision(config, tape, state, latent, move, rises, left);
    const FairDecision b =
        fairDecision(config, other, state, fake, move, rises, right);
    if (a.column != b.column) return false;
    if (a.column < 0) return false;
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

// The `latent_known` arm is allowed to read the hidden board, and must: with
// one sample and both privileges it has to reproduce the clairvoyant window
// optimum exactly.
bool checkPrivilegedArmMatchesClairvoyant() {
  const MasterTape tape = makeMasterTape(kLeaseStart + 7u, 60);
  FairPlannerConfig config;
  config.objective = Objective::kClears;
  config.horizon = 6;
  config.samples = 1;
  config.latent_known = true;
  config.tape_known = true;

  State state = masterStartState(tape);
  LatentBoard latent = tape.start_latent;
  int rises = 0;
  for (int move = 0; move < 20; ++move) {
    if (state.game_over) return true;
    Scenario window;
    std::string reason;
    if (!cutWindow(tape, state, latent, move, rises, config.horizon, window,
                   reason)) {
      return false;
    }
    WindowLimits limits;
    const WindowResult whole =
        solveWindow(window, Objective::kClears, limits);
    if (!whole.complete || whole.pv.empty()) return false;

    Mulberry32 unused(kSamplerLeaseStart + 0x40u);
    const FairDecision decision =
        fairDecision(config, tape, state, latent, move, rises, unused);
    if (decision.column < 0) return false;

    // The privileged arm need not pick the same column as the clairvoyant PV
    // when several columns tie, but the value it picked must equal the optimum.
    const RootResult root = solveWindowRoot(window, Objective::kClears, limits);
    if (!root.complete) return false;
    if (root.value[static_cast<std::size_t>(decision.column)] != whole.value) {
      return false;
    }

    MoveResult result;
    LatentBoard next_latent{};
    if (!playMasterMove(tape, state, latent, move, rises, decision.column,
                        result, next_latent)) {
      return false;
    }
    if (result.level_advanced) ++rises;
    state = result.state;
    latent = next_latent;
  }
  return true;
}

int runSelfTest() {
  struct Check {
    const char* name;
    bool (*run)();
  };
  const Check checks[] = {
      {"master tape is deterministic in its seed", checkMasterTapeDeterminism},
      {"a cut window reproduces the long game move for move",
       checkWindowMatchesLongGame},
      {"per-move score identity holds over a whole game", checkScoreIdentity},
      {"each objective's optimum dominates on its own quantity",
       checkObjectiveDominance},
      {"root-move values agree with the window optimum",
       checkRootValuesMatchOptimum},
      {"INFORMATION BOUNDARY: the fair planner ignores hidden state",
       checkFairPlannerIgnoresHiddenState},
      {"the privileged arm reproduces the clairvoyant optimum",
       checkPrivilegedArmMatchesClairvoyant},
  };
  int failures = 0;
  for (const Check& check : checks) {
    const bool ok = check.run();
    std::printf("%-58s %s\n", check.name, ok ? "ok" : "FAILED");
    if (!ok) ++failures;
  }
  std::printf("self-test: %d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string flag = argv[index];
    if (flag == "--self-test") {
      options.self_test = true;
    } else if (flag == "--cross-check" && index + 1 < argc) {
      options.cross_check = argv[++index];
    } else if (flag == "--policy" && index + 1 < argc) {
      options.policy = argv[++index];
    } else if (flag == "--jsonl" && index + 1 < argc) {
      options.jsonl_output = argv[++index];
    } else if (flag == "--games" && index + 1 < argc) {
      options.games = std::atoi(argv[++index]);
    } else if (flag == "--seed-start" && index + 1 < argc) {
      options.seed_start = static_cast<std::uint32_t>(
          std::strtoul(argv[++index], nullptr, 0));
    } else if (flag == "--horizon" && index + 1 < argc) {
      options.horizon = std::atoi(argv[++index]);
    } else if (flag == "--commit" && index + 1 < argc) {
      options.commit = std::atoi(argv[++index]);
    } else if (flag == "--min-horizon" && index + 1 < argc) {
      options.min_horizon = std::atoi(argv[++index]);
    } else if (flag == "--max-moves" && index + 1 < argc) {
      options.max_moves = std::atoi(argv[++index]);
    } else if (flag == "--threads" && index + 1 < argc) {
      options.threads = std::atoi(argv[++index]);
    } else if (flag == "--limit" && index + 1 < argc) {
      options.limit = std::atoi(argv[++index]);
    } else if (flag == "--time-limit" && index + 1 < argc) {
      options.time_limit = std::atof(argv[++index]);
    } else if (flag == "--node-limit" && index + 1 < argc) {
      options.node_limit = std::atoll(argv[++index]);
    } else if (flag == "--samples" && index + 1 < argc) {
      options.samples = std::atoi(argv[++index]);
    } else if (flag == "--sample-threads" && index + 1 < argc) {
      options.sample_threads = std::atoi(argv[++index]);
    } else if (flag == "--warm-horizon" && index + 1 < argc) {
      options.warm_horizon = std::atoi(argv[++index]);
    } else if (flag == "--warm-samples" && index + 1 < argc) {
      options.warm_samples = std::atoi(argv[++index]);
    } else if (flag == "--warm-moves" && index + 1 < argc) {
      options.warm_moves = std::atoi(argv[++index]);
    } else if (flag == "--latent-known") {
      options.latent_known = true;
    } else if (flag == "--tape-known") {
      options.tape_known = true;
    } else if (flag == "--sampler-seed" && index + 1 < argc) {
      options.sampler_seed = static_cast<std::uint32_t>(
          std::strtoul(argv[++index], nullptr, 0));
    } else {
      std::cerr << "unknown argument " << flag << "\n";
      return 2;
    }
  }
  if (options.self_test) return runSelfTest();
  if (!options.cross_check.empty()) return runCrossCheck(options);
  if (options.policy != "rh-points" && options.policy != "rh-clears" &&
      options.policy != "fair-rh" && options.policy != "fair-d4" &&
      options.policy != "lowest-column") {
    std::cerr << "unknown policy " << options.policy << "\n";
    return 2;
  }
  return runGames(options);
}
