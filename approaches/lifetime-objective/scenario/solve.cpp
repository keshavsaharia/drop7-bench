// Exact clairvoyant solver, command line driver and self-test.
//
//   solve --self-test
//   solve --input suite.jsonl [--horizon H] [--threads N] [--limit N]
//         [--no-tt] [--no-bound] [--time-limit S] [--jsonl out.jsonl]
//   solve --sweep --input suite.jsonl --min-h A --max-h B [--threads N]

#include "generate.hpp"
#include "scenario-io.hpp"
#include "solver.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace drop7;
using namespace drop7::scenario;

constexpr std::uint32_t kLeaseStart = 0xa51d'c000u;
constexpr std::uint32_t kLeaseEnd = 0xa51d'ffffu;

std::uint32_t leaseSeed(std::uint32_t offset) {
  const std::uint32_t seed = kLeaseStart + offset;
  if (seed > kLeaseEnd) {
    std::cerr << "seed lease SEEDLEASE-A51D-SCEN exhausted\n";
    std::exit(2);
  }
  return seed;
}

std::string describePath(const std::vector<int>& path) {
  std::string out;
  for (std::size_t index = 0; index < path.size(); ++index) {
    if (index != 0) out.push_back('-');
    out += std::to_string(path[index]);
  }
  return out.empty() ? std::string("(none)") : out;
}

// -------------------------------------------------------------------------
// Self-test
// -------------------------------------------------------------------------

Scenario handScenario(const Board& board, int moves_remaining,
                      const std::vector<std::uint8_t>& tape,
                      std::uint8_t latent_fill) {
  Scenario scenario;
  scenario.board = board;
  scenario.moves_remaining = static_cast<std::uint8_t>(moves_remaining);
  scenario.horizon = static_cast<std::uint8_t>(tape.size());
  scenario.disc_tape = tape;
  scenario.latent.fill(0);
  for (int index = 0; index < kCellCount; ++index) {
    if (board[index] == kSolid || board[index] == kCracked) {
      scenario.latent[index] = latent_fill;
    }
  }
  const int rises =
      riseRowCount(static_cast<int>(tape.size()), moves_remaining);
  RiseRow row{};
  row.fill(latent_fill);
  for (int index = 0; index < rises; ++index) scenario.rise_latent.push_back(row);
  assignScenarioId(scenario);
  return scenario;
}

bool expectOptimum(const char* name, const Scenario& scenario,
                   std::int64_t expected, const std::string& expected_path) {
  std::string reason;
  if (!validateScenario(scenario, reason)) {
    std::printf("  FAIL %s: scenario invalid (%s)\n", name, reason.c_str());
    return false;
  }
  SolveOptions options;
  options.threads = 1;
  const SolveResult result = solveScenario(scenario, options);
  if (!result.complete) {
    std::printf("  FAIL %s: solver reported incomplete\n", name);
    return false;
  }
  if (result.optimum != expected) {
    std::printf("  FAIL %s: optimum %lld, expected %lld (pv %s)\n", name,
                static_cast<long long>(result.optimum),
                static_cast<long long>(expected),
                describePath(result.principal_variation).c_str());
    return false;
  }
  if (!expected_path.empty() &&
      describePath(result.principal_variation) != expected_path) {
    std::printf("  FAIL %s: principal variation %s, expected %s\n", name,
                describePath(result.principal_variation).c_str(),
                expected_path.c_str());
    return false;
  }
  std::printf("  ok   %s: optimum %lld pv %s\n", name,
              static_cast<long long>(result.optimum),
              describePath(result.principal_variation).c_str());
  return true;
}

bool handConstructedTests() {
  bool ok = true;

  // 1. A move that fills the last legal cell ends the game and earns nothing.
  //    Every cell is a covered gray, which can never pop, so the only event is
  //    the loss of the last legal column.
  {
    Board board{};
    board.fill(kSolid);
    board[indexOf(0, 3)] = kEmpty;
    ok &= expectOptimum("forced-death-zero-points",
                        handScenario(board, 5, {1}, 1), 0, "3");
  }

  // 2. A rise on an otherwise inert position pays exactly the level bonus.
  //    A 7 cannot pop: it would need a contiguous run of seven.
  {
    Board board{};
    const Scenario scenario = handScenario(board, 1, {7}, 1);
    ok &= expectOptimum("rise-pays-level-bonus", scenario, kLevelBonus, "");
  }

  // 3. Completing a run of three 3s pops all three and empties the board,
  //    which pays 3 * floor(7 * 1^2.5) plus the 70,000 clear bonus.  Only
  //    column 2 does it.
  {
    Board board{};
    board[indexOf(6, 0)] = 3;
    board[indexOf(6, 1)] = 3;
    ok &= expectOptimum("clear-by-completing-a-run",
                        handScenario(board, 5, {3}, 1),
                        3 * scoreForWave(1) + kClearBonus, "2");
  }

  // 4. Two isolated 2s one column apart: the optimum must find the second-move
  //    pop rather than only the first-move one, and the total is two separate
  //    depth-one waves.
  {
    Board board{};
    board[indexOf(6, 0)] = 2;
    board[indexOf(6, 5)] = 2;
    const Scenario scenario = handScenario(board, 5, {2, 2}, 1);
    SolveOptions options;
    const SolveResult result = solveScenario(scenario, options);
    const std::int64_t expected = 2 * scoreForWave(1) + 2 * scoreForWave(1) +
                                  kClearBonus;
    if (result.optimum != expected) {
      std::printf("  FAIL two-pops-two-moves: optimum %lld expected %lld\n",
                  static_cast<long long>(result.optimum),
                  static_cast<long long>(expected));
      ok = false;
    } else {
      std::printf("  ok   two-pops-two-moves: optimum %lld pv %s\n",
                  static_cast<long long>(result.optimum),
                  describePath(result.principal_variation).c_str());
    }
  }

  return ok;
}

// Cross-checks the real solver against a deliberately naive enumerator with no
// table, no bound, no threads and no ordering.  Any disagreement means the
// optimizations changed the answer.
bool randomCrossCheck(int scenarios, int max_horizon) {
  int checked = 0;
  int with_gap = 0;
  long long naive_nodes = 0;
  long long solver_nodes = 0;
  for (int index = 0; index < scenarios; ++index) {
    Scenario scenario;
    const std::uint32_t label = leaseSeed(0x3000u + static_cast<std::uint32_t>(index));
    const int horizon = 1 + (index % max_horizon);
    bool built = false;
    if (index % 2 == 0) {
      HarvestOptions harvest;
      harvest.horizon = horizon;
      built = harvestScenario(leaseSeed(0x3800u +
                                        static_cast<std::uint32_t>(index)),
                              label, harvest, scenario);
    } else {
      SyntheticOptions synthetic;
      synthetic.horizon = horizon;
      synthetic.target_cells = 8 + (index % 30);
      synthetic.cover_fraction = 0.15 + 0.05 * (index % 8);
      built = syntheticScenario(label, synthetic, scenario);
    }
    if (!built) continue;

    std::int64_t nodes = 0;
    const std::int64_t reference = naiveOptimum(scenario, nodes);
    naive_nodes += nodes;

    for (int variant = 0; variant < 4; ++variant) {
      SolveOptions options;
      options.use_tt = (variant & 1) != 0;
      options.use_bound = (variant & 2) != 0;
      options.threads = variant == 3 ? 4 : 1;
      const SolveResult result = solveScenario(scenario, options);
      if (variant == 3) solver_nodes += result.nodes;
      if (!result.complete) {
        std::printf("  FAIL cross-check %s: incomplete (variant %d)\n",
                    scenario.id, variant);
        return false;
      }
      if (result.optimum != reference) {
        std::printf(
            "  FAIL cross-check %s: solver %lld naive %lld "
            "(tt=%d bound=%d threads=%d)\n",
            scenario.id, static_cast<long long>(result.optimum),
            static_cast<long long>(reference), options.use_tt,
            options.use_bound, options.threads);
        return false;
      }
    }

    // A one-ply greedy on the same tape, for a sanity signal that the solver is
    // doing more than taking the best immediate score.
    {
      SearchNode node;
      node.board = scenario.board;
      node.latent = scenario.latent;
      std::int64_t greedy = 0;
      MoveResult scratch;
      for (int depth = 0; depth < scenario.horizon; ++depth) {
        std::int64_t best = -1;
        SearchNode best_child;
        bool best_over = false;
        for (int column = 0; column < kBoardSize; ++column) {
          if (!isLegal(node.board, column)) continue;
          SearchNode child;
          std::int64_t delta = 0;
          bool over = false;
          if (!applyScenarioMove(scenario, node, depth, column, child, delta,
                                 over, scratch)) {
            continue;
          }
          if (delta > best) {
            best = delta;
            best_child = child;
            best_over = over;
          }
        }
        if (best < 0) break;
        greedy += best;
        node = best_child;
        if (best_over) break;
      }
      if (greedy > reference) {
        std::printf("  FAIL cross-check %s: greedy %lld beats optimum %lld\n",
                    scenario.id, static_cast<long long>(greedy),
                    static_cast<long long>(reference));
        return false;
      }
      if (greedy < reference) ++with_gap;
    }
    ++checked;
  }
  std::printf(
      "  ok   randomized cross-check: %d scenarios x 4 solver variants vs "
      "naive enumeration, %d with greedy<optimum, naive nodes %lld, solver "
      "nodes %lld\n",
      checked, with_gap, naive_nodes, solver_nodes);
  return checked > 0;
}

bool reHorizonTest() {
  Scenario scenario;
  HarvestOptions harvest;
  harvest.horizon = 8;
  if (!harvestScenario(leaseSeed(0x3f00u), leaseSeed(0x3f01u), harvest,
                       scenario)) {
    std::printf("  FAIL re-horizon: could not harvest\n");
    return false;
  }
  for (int horizon = 1; horizon <= 8; ++horizon) {
    Scenario shorter;
    if (!reHorizonScenario(scenario, horizon, shorter)) {
      std::printf("  FAIL re-horizon: cut to %d failed\n", horizon);
      return false;
    }
    std::string reason;
    if (!validateScenario(shorter, reason)) {
      std::printf("  FAIL re-horizon: cut to %d invalid (%s)\n", horizon,
                  reason.c_str());
      return false;
    }
    if (shorter.board != scenario.board || shorter.latent != scenario.latent) {
      std::printf("  FAIL re-horizon: start position changed\n");
      return false;
    }
    if (std::string(shorter.id) == std::string(scenario.id) && horizon != 8) {
      std::printf("  FAIL re-horizon: id did not change with the horizon\n");
      return false;
    }
  }
  // Monotonicity: a longer horizon can never earn less, since score never
  // decreases and the shorter line is a prefix of a legal longer line.
  std::int64_t previous = -1;
  for (int horizon = 1; horizon <= 7; ++horizon) {
    Scenario shorter;
    reHorizonScenario(scenario, horizon, shorter);
    SolveOptions options;
    const SolveResult result = solveScenario(shorter, options);
    if (result.optimum < previous) {
      std::printf("  FAIL re-horizon: optimum fell from %lld to %lld at H=%d\n",
                  static_cast<long long>(previous),
                  static_cast<long long>(result.optimum), horizon);
      return false;
    }
    previous = result.optimum;
  }
  std::printf("  ok   re-horizon: H=1..8 cut, revalidated, monotone optima\n");
  return true;
}

int runSelfTest() {
  std::printf("solver self-test\n");
  bool ok = true;
  ok &= handConstructedTests();
  ok &= reHorizonTest();
  ok &= randomCrossCheck(120, 6);
  std::printf(ok ? "SELF-TEST PASSED\n" : "SELF-TEST FAILED\n");
  return ok ? 0 : 1;
}

// -------------------------------------------------------------------------
// Batch solving and the horizon sweep
// -------------------------------------------------------------------------

struct Options {
  std::string input;
  std::string replay_id;
  std::string jsonl_output;
  int horizon = 0;
  int threads = 1;
  int limit = 0;
  int min_h = 0;
  int max_h = 0;
  bool sweep = false;
  bool use_tt = true;
  bool use_bound = true;
  double time_limit = 0.0;
};

// Replays the optimal line move by move so a surprising optimum can be audited
// by hand: every wave, its depth, how many discs it cleared, how many covered
// cells it revealed, and what it paid.
int runReplay(const Options& options) {
  std::vector<Scenario> scenarios;
  std::string error;
  if (!loadScenarioFile(options.input, scenarios, error)) {
    std::cerr << error << "\n";
    return 2;
  }
  const Scenario* found = nullptr;
  for (const Scenario& scenario : scenarios) {
    if (options.replay_id == scenario.id) found = &scenario;
  }
  if (found == nullptr) {
    std::cerr << "no scenario with id " << options.replay_id << "\n";
    return 2;
  }
  Scenario scenario = *found;
  if (options.horizon > 0 && options.horizon != scenario.horizon) {
    Scenario shorter;
    if (!reHorizonScenario(scenario, options.horizon, shorter)) return 2;
    scenario = shorter;
  }
  SolveOptions solve_options;
  solve_options.threads = options.threads;
  const SolveResult result = solveScenario(scenario, solve_options);
  std::printf("scenario %s H=%d optimum=%lld complete=%d\n", scenario.id,
              static_cast<int>(scenario.horizon),
              static_cast<long long>(result.optimum), result.complete ? 1 : 0);
  std::printf("start board  %s\n", serializeBoard(scenario.board).c_str());
  std::printf("start latent %s\n",
              serializeBoard(scenario.latent).c_str());
  std::printf("disc tape    ");
  for (std::uint8_t disc : scenario.disc_tape) std::printf("%d", disc);
  std::printf("   movesRemaining=%d\n",
              static_cast<int>(scenario.moves_remaining));
  auto engine = makeScenarioEngine(scenario);
  std::int64_t running = 0;
  for (std::size_t index = 0; index < result.principal_variation.size();
       ++index) {
    const int column = result.principal_variation[index];
    MoveResult move;
    if (!engine.play(column, move)) break;
    running += move.score_delta;
    std::printf("  move %zu col %d disc %d delta %8lld total %9lld%s%s\n",
                index + 1, column,
                static_cast<int>(
                    scenario.disc_tape[index]),
                static_cast<long long>(move.score_delta),
                static_cast<long long>(running),
                move.level_advanced ? "  [rise +17000]" : "",
                move.cleared_board ? "  [BOARD CLEAR]" : "");
    for (const Wave& wave : move.waves) {
      std::printf("      wave depth %2d cleared %2d revealed %2d points %8lld\n",
                  wave.depth, wave.cleared, wave.revealed,
                  static_cast<long long>(wave.points));
    }
    std::printf("      board %s\n",
                serializeBoard(engine.state().board).c_str());
    if (engine.state().game_over) {
      std::printf("      game over\n");
      break;
    }
  }
  std::printf("replayed total %lld (optimum %lld)\n",
              static_cast<long long>(running),
              static_cast<long long>(result.optimum));
  return running == result.optimum ? 0 : 1;
}

int runBatch(const Options& options) {
  std::vector<Scenario> scenarios;
  std::string error;
  if (!loadScenarioFile(options.input, scenarios, error)) {
    std::cerr << error << "\n";
    return 2;
  }
  if (options.limit > 0 &&
      static_cast<int>(scenarios.size()) > options.limit) {
    scenarios.resize(static_cast<std::size_t>(options.limit));
  }
  std::ofstream jsonl;
  if (!options.jsonl_output.empty()) {
    jsonl.open(options.jsonl_output);
    if (!jsonl) {
      std::cerr << "cannot write " << options.jsonl_output << "\n";
      return 2;
    }
  }

  long long total_nodes = 0;
  long long total_pv_nodes = 0;
  long long total_tt_hits = 0;
  double total_seconds = 0.0;
  int clears = 0;
  int incomplete = 0;
  for (Scenario& scenario : scenarios) {
    if (options.horizon > 0 &&
        options.horizon != static_cast<int>(scenario.horizon)) {
      Scenario shorter;
      if (!reHorizonScenario(scenario, options.horizon, shorter)) {
        std::cerr << "scenario " << scenario.id << " cannot be cut to horizon "
                  << options.horizon << "\n";
        return 2;
      }
      scenario = shorter;
    }
    SolveOptions solve_options;
    solve_options.threads = options.threads;
    solve_options.use_tt = options.use_tt;
    solve_options.use_bound = options.use_bound;
    solve_options.time_limit_seconds = options.time_limit;
    const SolveResult result = solveScenario(scenario, solve_options);
    total_nodes += result.nodes;
    total_pv_nodes += result.pv_nodes;
    total_tt_hits += result.tt_hits;
    total_seconds += result.wall_seconds;
    if (result.optimal_clears_board) ++clears;
    if (!result.complete) ++incomplete;
    std::printf(
        "%s H=%2d optimum=%9lld pv=%-24s clear=%d maxdepth=%d moves=%d "
        "nodes=%10lld tt_hits=%9lld cuts=%9lld %.3fs%s\n",
        scenario.id, static_cast<int>(scenario.horizon),
        static_cast<long long>(result.optimum),
        describePath(result.principal_variation).c_str(),
        result.optimal_clears_board ? 1 : 0, result.optimal_max_chain_depth,
        result.optimal_moves_survived, static_cast<long long>(result.nodes),
        static_cast<long long>(result.tt_hits),
        static_cast<long long>(result.bound_cutoffs), result.wall_seconds,
        result.complete ? "" : "  INCOMPLETE");
    if (jsonl) {
      jsonl << "{" << scenarioFields(scenario)
            << ",\"optimum\":" << result.optimum
            << ",\"complete\":" << (result.complete ? "true" : "false")
            << ",\"principalVariation\":[";
      for (std::size_t index = 0; index < result.principal_variation.size();
           ++index) {
        if (index != 0) jsonl << ',';
        jsonl << result.principal_variation[index];
      }
      jsonl << "],\"optimalClearsBoard\":"
            << (result.optimal_clears_board ? "true" : "false")
            << ",\"optimalClearCount\":" << result.optimal_clear_count
            << ",\"optimalMaxChainDepth\":" << result.optimal_max_chain_depth
            << ",\"optimalMovesSurvived\":" << result.optimal_moves_survived
            << ",\"nodes\":" << result.nodes
            << ",\"ttHits\":" << result.tt_hits
            << ",\"boundCutoffs\":" << result.bound_cutoffs
            << ",\"wallSeconds\":" << result.wall_seconds << "}\n";
    }
  }
  std::printf(
      "scenarios=%zu clears=%d incomplete=%d search_nodes=%lld pv_nodes=%lld "
      "tt_hits=%lld total_seconds=%.3f nodes_per_second=%.0f tt=%d bound=%d "
      "threads=%d\n",
      scenarios.size(), clears, incomplete, total_nodes, total_pv_nodes,
      total_tt_hits, total_seconds,
      total_seconds > 0 ? static_cast<double>(total_nodes) / total_seconds : 0,
      options.use_tt ? 1 : 0, options.use_bound ? 1 : 0, options.threads);
  return incomplete == 0 ? 0 : 1;
}

int runSweep(const Options& options) {
  std::vector<Scenario> scenarios;
  std::string error;
  if (!loadScenarioFile(options.input, scenarios, error)) {
    std::cerr << error << "\n";
    return 2;
  }
  if (options.limit > 0 &&
      static_cast<int>(scenarios.size()) > options.limit) {
    scenarios.resize(static_cast<std::size_t>(options.limit));
  }
  std::printf(
      "%3s %8s %14s %14s %10s %10s %10s %10s\n", "H", "solved", "nodes/med",
      "nodes/max", "sec/med", "sec/max", "tt_hit%", "cut%");
  for (int horizon = options.min_h; horizon <= options.max_h; ++horizon) {
    std::vector<double> seconds;
    std::vector<double> nodes;
    long long total_nodes = 0;
    long long total_hits = 0;
    long long total_cuts = 0;
    int solved = 0;
    for (const Scenario& source : scenarios) {
      Scenario scenario;
      if (!reHorizonScenario(source, horizon, scenario)) continue;
      SolveOptions solve_options;
      solve_options.threads = options.threads;
      solve_options.use_tt = options.use_tt;
      solve_options.use_bound = options.use_bound;
      solve_options.time_limit_seconds = options.time_limit;
      const SolveResult result = solveScenario(scenario, solve_options);
      if (!result.complete) continue;
      ++solved;
      seconds.push_back(result.wall_seconds);
      nodes.push_back(static_cast<double>(result.nodes));
      total_nodes += result.nodes;
      total_hits += result.tt_hits;
      total_cuts += result.bound_cutoffs;
    }
    if (seconds.empty()) {
      std::printf("%3d %8d %14s %14s %10s %10s %10s %10s\n", horizon, 0, "-",
                  "-", "-", "-", "-", "-");
      continue;
    }
    std::sort(seconds.begin(), seconds.end());
    std::sort(nodes.begin(), nodes.end());
    std::printf("%3d %8d %14.0f %14.0f %10.3f %10.3f %10.2f %10.2f\n", horizon,
                solved, nodes[nodes.size() / 2], nodes.back(),
                seconds[seconds.size() / 2], seconds.back(),
                total_nodes > 0
                    ? 100.0 * static_cast<double>(total_hits) /
                          static_cast<double>(total_nodes + total_hits)
                    : 0.0,
                total_nodes > 0
                    ? 100.0 * static_cast<double>(total_cuts) /
                          static_cast<double>(total_nodes + total_cuts)
                    : 0.0);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  bool self_test = false;
  for (int index = 1; index < argc; ++index) {
    const std::string flag = argv[index];
    if (flag == "--self-test") {
      self_test = true;
    } else if (flag == "--sweep") {
      options.sweep = true;
    } else if (flag == "--no-tt") {
      options.use_tt = false;
    } else if (flag == "--no-bound") {
      options.use_bound = false;
    } else if (flag == "--input" && index + 1 < argc) {
      options.input = argv[++index];
    } else if (flag == "--replay" && index + 1 < argc) {
      options.replay_id = argv[++index];
    } else if (flag == "--jsonl" && index + 1 < argc) {
      options.jsonl_output = argv[++index];
    } else if (flag == "--horizon" && index + 1 < argc) {
      options.horizon = std::atoi(argv[++index]);
    } else if (flag == "--threads" && index + 1 < argc) {
      options.threads = std::atoi(argv[++index]);
    } else if (flag == "--limit" && index + 1 < argc) {
      options.limit = std::atoi(argv[++index]);
    } else if (flag == "--min-h" && index + 1 < argc) {
      options.min_h = std::atoi(argv[++index]);
    } else if (flag == "--max-h" && index + 1 < argc) {
      options.max_h = std::atoi(argv[++index]);
    } else if (flag == "--time-limit" && index + 1 < argc) {
      options.time_limit = std::atof(argv[++index]);
    } else {
      std::cerr << "unknown option " << flag << "\n";
      return 2;
    }
  }
  if (self_test) return runSelfTest();
  if (options.input.empty()) {
    std::cerr << "use --self-test or --input <suite.jsonl>\n";
    return 2;
  }
  if (!options.replay_id.empty()) return runReplay(options);
  if (options.sweep) {
    if (options.min_h < 1 || options.max_h < options.min_h) {
      std::cerr << "--sweep needs --min-h and --max-h\n";
      return 2;
    }
    return runSweep(options);
  }
  return runBatch(options);
}
