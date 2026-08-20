// Mints a labelled scenario benchmark.
//
// For every candidate it records:
//   * the exact clairvoyant optimum over the horizon, its principal variation,
//     whether that line clears the board, and its maximum chain depth;
//   * a privileged one-ply greedy that maximizes the actual immediate score
//     delta on this exact tape (it can see the reveals, so it is a diagnostic,
//     not a policy);
//   * the frozen fair search at completed depths one, two, and four, which are
//     genuine public-information policies playing the same scenario; and
//   * gap = optimum - best_shallow.
//
// Every candidate is written out, not only the ones a later selection step
// would keep, so selection can be redone without regenerating anything.
//
// IMPORTANT about gap: a large gap does NOT mean a fair policy could have done
// better.  It may mean the position rewards knowing the future.  Separating
// "fairly recoverable" from "luck-only" requires evaluating a fair policy over
// many independent tapes from the same start position, which is deliberately
// not done here; the record is designed so that the coordinator can reuse
// `board` + `latent` + `movesRemaining` with a different `discTape`.

// The generated copy has its entry point renamed at build time; see build.sh.
#include "fair-only-depth4-noentry.cpp"

#include "generate.hpp"
#include "scenario-io.hpp"
#include "solver.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
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
namespace d4 = drop7::fair_only_depth4;

constexpr std::uint32_t kLeaseStart = 0xa51d'c000u;
constexpr std::uint32_t kLeaseEnd = 0xa51d'ffffu;
// Offset map inside the lease, so concurrent tools never collide:
//   0x0000-0x0fff  scenario-parity game seeds
//   0x1000-0x10ff  scenario-parity latent invariant games
//   0x1100-0x11ff  scenario-parity reveal marginal draws
//   0x1200-0x2fff  mint (this program)
//   0x3000-0x3fff  solve --self-test
constexpr std::uint32_t kMintOffsetEnd = 0x2fffu;

std::uint32_t leaseSeed(std::uint32_t offset) {
  const std::uint32_t seed = kLeaseStart + offset;
  if (offset > kMintOffsetEnd || seed > kLeaseEnd) {
    std::cerr << "seed lease SEEDLEASE-A51D-SCEN exhausted at offset "
              << offset << "\n";
    std::exit(2);
  }
  return seed;
}

// A completed full-width fair search at a fixed depth, from the frozen
// reference source.  Depth four reproduces the repository comparator exactly
// through `chooseDepth4Action`; depths one and two use the same root routine at
// a shallower completed depth.
int fairAction(const State& state, int depth) {
  if (state.game_over) return -1;
  if (depth >= 4) {
    const d4::SearchDecision decision = d4::chooseDepth4Action(state);
    return decision.action;
  }
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(state, mirrored);
  d4::SearchContext context;
  d4::RootEvaluation evaluation;
  try {
    evaluation = d4::rootDecision(canonical, depth, context);
  } catch (const d4::WorkLimitReached&) {
  }
  int action = evaluation.action;
  if (action < 0) action = centerFirstMove(canonical.board);
  if (action < 0) return -1;
  return mirrored ? kBoardSize - 1 - action : action;
}

struct PlayOutcome {
  std::int64_t points = 0;
  int moves = 0;
  bool cleared_board = false;
  int max_chain_depth = 0;
  bool died = false;
};

PlayOutcome playFair(const Scenario& scenario, int depth) {
  PlayOutcome outcome;
  auto engine = makeScenarioEngine(scenario);
  for (int move = 0; move < scenario.horizon; ++move) {
    if (engine.state().game_over) break;
    const int column = fairAction(engine.state(), depth);
    if (column < 0 || !isLegal(engine.state().board, column)) break;
    MoveResult result;
    if (!engine.play(column, result)) break;
    outcome.points += result.score_delta;
    ++outcome.moves;
    if (result.cleared_board) outcome.cleared_board = true;
    for (const Wave& wave : result.waves) {
      outcome.max_chain_depth = std::max(outcome.max_chain_depth, wave.depth);
    }
    if (engine.state().game_over) {
      outcome.died = true;
      break;
    }
  }
  return outcome;
}

// Privileged: maximizes the true immediate score delta, which requires seeing
// the reveals.  Diagnostic only.
PlayOutcome playClairvoyantGreedy(const Scenario& scenario) {
  PlayOutcome outcome;
  SearchNode node;
  node.board = scenario.board;
  node.latent = scenario.latent;
  MoveResult scratch;
  for (int depth = 0; depth < scenario.horizon; ++depth) {
    std::int64_t best = -1;
    SearchNode best_child;
    bool best_over = false;
    int best_max_depth = 0;
    bool best_cleared = false;
    for (int column = 0; column < kBoardSize; ++column) {
      if (!isLegal(node.board, column)) continue;
      SearchNode child;
      std::int64_t delta = 0;
      bool over = false;
      if (!applyScenarioMove(scenario, node, depth, column, child, delta, over,
                             scratch)) {
        continue;
      }
      if (delta > best) {
        best = delta;
        best_child = child;
        best_over = over;
        best_cleared = scratch.cleared_board;
        best_max_depth = 0;
        for (const Wave& wave : scratch.waves) {
          best_max_depth = std::max(best_max_depth, wave.depth);
        }
      }
    }
    if (best < 0) break;
    outcome.points += best;
    ++outcome.moves;
    if (best_cleared) outcome.cleared_board = true;
    outcome.max_chain_depth =
        std::max(outcome.max_chain_depth, best_max_depth);
    node = best_child;
    if (best_over) {
      outcome.died = true;
      break;
    }
  }
  return outcome;
}

struct Candidate {
  Scenario scenario;
  bool fair_labels = true;
  std::string origin;
  std::string profile;
  int occupied = 0;
  int covered = 0;
  std::uint32_t label_seed = 0;
  std::uint32_t game_seed = 0;

  SolveResult solved;
  PlayOutcome greedy;
  PlayOutcome fair1;
  PlayOutcome fair2;
  PlayOutcome fair4;
  std::int64_t best_shallow = 0;
  std::int64_t gap = 0;
};

std::string serializeCandidate(const Candidate& candidate) {
  std::ostringstream out;
  out << "{" << scenarioFields(candidate.scenario);
  out << ",\"origin\":\"" << candidate.origin << "\"";
  out << ",\"numberProfile\":\"" << candidate.profile << "\"";
  out << ",\"occupiedCells\":" << candidate.occupied;
  out << ",\"coveredCells\":" << candidate.covered;
  out << ",\"labelSeed\":\"" << std::hex << candidate.label_seed << std::dec
      << "\"";
  out << ",\"originGameSeed\":\"" << std::hex << candidate.game_seed
      << std::dec << "\"";
  out << ",\"optimum\":" << candidate.solved.optimum;
  out << ",\"solveComplete\":"
      << (candidate.solved.complete ? "true" : "false");
  out << ",\"principalVariation\":[";
  for (std::size_t index = 0;
       index < candidate.solved.principal_variation.size(); ++index) {
    if (index != 0) out << ',';
    out << candidate.solved.principal_variation[index];
  }
  out << "]";
  out << ",\"optimalClearsBoard\":"
      << (candidate.solved.optimal_clears_board ? "true" : "false");
  out << ",\"optimalClearCount\":" << candidate.solved.optimal_clear_count;
  out << ",\"optimalMaxChainDepth\":"
      << candidate.solved.optimal_max_chain_depth;
  out << ",\"optimalMovesSurvived\":"
      << candidate.solved.optimal_moves_survived;
  out << ",\"solveNodes\":" << candidate.solved.nodes;
  out << ",\"solveSeconds\":" << candidate.solved.wall_seconds;
  const auto emit = [&out](const char* name, const PlayOutcome& outcome) {
    out << ",\"" << name << "\":{\"points\":" << outcome.points
        << ",\"moves\":" << outcome.moves
        << ",\"clearedBoard\":" << (outcome.cleared_board ? "true" : "false")
        << ",\"maxChainDepth\":" << outcome.max_chain_depth
        << ",\"died\":" << (outcome.died ? "true" : "false") << "}";
  };
  emit("greedyClairvoyant", candidate.greedy);
  out << ",\"fairLabelsPresent\":"
      << (candidate.fair_labels ? "true" : "false");
  if (candidate.fair_labels) {
    emit("fairDepth1", candidate.fair1);
    emit("fairDepth2", candidate.fair2);
    emit("fairDepth4", candidate.fair4);
    out << ",\"bestShallow\":" << candidate.best_shallow;
    out << ",\"gap\":" << candidate.gap;
  }
  out << "}";
  return out.str();
}

struct Options {
  int harvested = 64;
  int synthetic = 64;
  int horizon = 8;
  int threads = 8;
  int solve_threads = 1;
  double time_limit = 0.0;
  bool skip_fair = false;
  int synthetic_min_cells = 6;
  int synthetic_max_cells = 39;
  double cover_min = 0.10;
  double cover_max = 0.66;
  std::string output;
  std::uint32_t seed_offset = 0x1200u;
};

}  // namespace

int main(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string flag = argv[index];
    if (flag == "--harvested" && index + 1 < argc) {
      options.harvested = std::atoi(argv[++index]);
    } else if (flag == "--synthetic" && index + 1 < argc) {
      options.synthetic = std::atoi(argv[++index]);
    } else if (flag == "--horizon" && index + 1 < argc) {
      options.horizon = std::atoi(argv[++index]);
    } else if (flag == "--threads" && index + 1 < argc) {
      options.threads = std::atoi(argv[++index]);
    } else if (flag == "--solve-threads" && index + 1 < argc) {
      options.solve_threads = std::atoi(argv[++index]);
    } else if (flag == "--time-limit" && index + 1 < argc) {
      options.time_limit = std::atof(argv[++index]);
    } else if (flag == "--seed-offset" && index + 1 < argc) {
      options.seed_offset = static_cast<std::uint32_t>(
          std::strtoul(argv[++index], nullptr, 0));
    } else if (flag == "--min-cells" && index + 1 < argc) {
      options.synthetic_min_cells = std::atoi(argv[++index]);
    } else if (flag == "--max-cells" && index + 1 < argc) {
      options.synthetic_max_cells = std::atoi(argv[++index]);
    } else if (flag == "--cover-min" && index + 1 < argc) {
      options.cover_min = std::atof(argv[++index]);
    } else if (flag == "--cover-max" && index + 1 < argc) {
      options.cover_max = std::atof(argv[++index]);
    } else if (flag == "--skip-fair") {
      options.skip_fair = true;
    } else if (flag == "--output" && index + 1 < argc) {
      options.output = argv[++index];
    } else {
      std::cerr << "unknown option " << flag << "\n";
      return 2;
    }
  }
  if (options.output.empty()) {
    std::cerr << "--output <suite.jsonl> is required\n";
    return 2;
  }

  // Build every candidate position first, single-threaded, so that seed use is
  // deterministic and reproducible regardless of thread count.
  std::vector<Candidate> candidates;
  std::uint32_t cursor = options.seed_offset;
  const std::array<NumberProfile, 3> profiles{{NumberProfile::kUniform,
                                               NumberProfile::kLowHeavy,
                                               NumberProfile::kHighHeavy}};
  int harvest_attempts = 0;
  while (static_cast<int>(candidates.size()) < options.harvested &&
         harvest_attempts < options.harvested * 8) {
    ++harvest_attempts;
    HarvestOptions harvest;
    harvest.horizon = options.horizon;
    const std::uint32_t game_seed = leaseSeed(cursor++);
    const std::uint32_t label_seed = leaseSeed(cursor++);
    Candidate candidate;
    if (!harvestScenario(game_seed, label_seed, harvest, candidate.scenario)) {
      continue;
    }
    candidate.origin = "harvested";
    candidate.profile = "engine";
    candidate.game_seed = game_seed;
    candidate.label_seed = label_seed;
    candidate.occupied = occupiedCells(candidate.scenario.board);
    candidate.covered = coveredCells(candidate.scenario.board);
    candidates.push_back(std::move(candidate));
  }
  const int harvested_count = static_cast<int>(candidates.size());

  int synthetic_attempts = 0;
  while (static_cast<int>(candidates.size()) - harvested_count <
             options.synthetic &&
         synthetic_attempts < options.synthetic * 8) {
    const int index = synthetic_attempts++;
    SyntheticOptions synthetic;
    synthetic.horizon = options.horizon;
    // Span easy-open through near-death-crowded.
    const int cell_span =
        std::max(1, options.synthetic_max_cells - options.synthetic_min_cells + 1);
    synthetic.target_cells = options.synthetic_min_cells + (index % cell_span);
    synthetic.cover_fraction =
        options.cover_min + (options.cover_max - options.cover_min) *
                                (static_cast<double>(index % 8) / 7.0);
    synthetic.profile = profiles[static_cast<std::size_t>(index % 3)];
    const std::uint32_t label_seed = leaseSeed(cursor++);
    Candidate candidate;
    if (!syntheticScenario(label_seed, synthetic, candidate.scenario)) continue;
    candidate.origin = "synthetic";
    candidate.profile = numberProfileName(synthetic.profile);
    candidate.label_seed = label_seed;
    candidate.occupied = occupiedCells(candidate.scenario.board);
    candidate.covered = coveredCells(candidate.scenario.board);
    candidates.push_back(std::move(candidate));
  }

  std::printf("minting %zu candidates (harvested %d, synthetic %zu) H=%d\n",
              candidates.size(), harvested_count,
              candidates.size() - static_cast<std::size_t>(harvested_count),
              options.horizon);
  std::printf("seed lease SEEDLEASE-A51D-SCEN, offsets 0x%x..0x%x of 0x%08x\n",
              options.seed_offset, cursor - 1, kLeaseStart);

  std::atomic<std::size_t> next{0};
  std::atomic<int> done{0};
  const auto worker = [&]() {
    for (;;) {
      const std::size_t index = next.fetch_add(1);
      if (index >= candidates.size()) return;
      Candidate& candidate = candidates[index];
      SolveOptions solve_options;
      solve_options.threads = options.solve_threads;
      solve_options.time_limit_seconds = options.time_limit;
      candidate.solved = solveScenario(candidate.scenario, solve_options);
      candidate.greedy = playClairvoyantGreedy(candidate.scenario);
      candidate.fair_labels = !options.skip_fair;
      if (!options.skip_fair) {
        candidate.fair1 = playFair(candidate.scenario, 1);
        candidate.fair2 = playFair(candidate.scenario, 2);
        candidate.fair4 = playFair(candidate.scenario, 4);
      }
      candidate.best_shallow =
          std::max({candidate.fair1.points, candidate.fair2.points,
                    candidate.fair4.points});
      candidate.gap = candidate.solved.optimum - candidate.best_shallow;
      const int completed = ++done;
      if (completed % 16 == 0) {
        std::printf("  %d/%zu labelled\n", completed, candidates.size());
        std::fflush(stdout);
      }
    }
  };

  const auto started = std::chrono::steady_clock::now();
  std::vector<std::thread> pool;
  for (int slot = 0; slot < std::max(1, options.threads); ++slot) {
    pool.emplace_back(worker);
  }
  for (std::thread& thread : pool) thread.join();
  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();

  std::ofstream output(options.output);
  if (!output) {
    std::cerr << "cannot write " << options.output << "\n";
    return 2;
  }
  for (const Candidate& candidate : candidates) {
    output << serializeCandidate(candidate) << "\n";
  }
  output.close();

  int incomplete = 0;
  int optimal_clears = 0;
  int fair_clears = 0;
  int greedy_clears = 0;
  std::vector<std::int64_t> gaps;
  std::int64_t total_optimum = 0;
  std::int64_t total_shallow = 0;
  for (const Candidate& candidate : candidates) {
    if (!candidate.solved.complete) {
      ++incomplete;
      continue;
    }
    if (candidate.solved.optimal_clears_board) ++optimal_clears;
    if (candidate.fair1.cleared_board || candidate.fair2.cleared_board ||
        candidate.fair4.cleared_board) {
      ++fair_clears;
    }
    if (candidate.greedy.cleared_board) ++greedy_clears;
    gaps.push_back(candidate.gap);
    total_optimum += candidate.solved.optimum;
    total_shallow += candidate.best_shallow;
  }
  std::sort(gaps.begin(), gaps.end());
  const auto quantile = [&gaps](double q) -> std::int64_t {
    if (gaps.empty()) return 0;
    const std::size_t index = static_cast<std::size_t>(
        q * static_cast<double>(gaps.size() - 1) + 0.5);
    return gaps[index];
  };
  int positive = 0;
  int zero = 0;
  int negative = 0;
  for (std::int64_t gap : gaps) {
    if (gap > 0) {
      ++positive;
    } else if (gap == 0) {
      ++zero;
    } else {
      ++negative;
    }
  }
  std::printf("labelled %zu candidates in %.1f s (%d incomplete)\n",
              candidates.size(), elapsed, incomplete);
  std::printf(
      "board clears in the clairvoyant optimum: %d/%zu (%.1f%%); by any fair "
      "policy: %d; by privileged greedy: %d\n",
      optimal_clears, gaps.size(),
      gaps.empty() ? 0.0
                   : 100.0 * optimal_clears / static_cast<double>(gaps.size()),
      fair_clears, greedy_clears);
  if (options.skip_fair) {
    std::printf(
        "fair labels skipped: bestShallow and gap are not defined for this "
        "run\n");
    std::printf("wrote %s\n", options.output.c_str());
    return incomplete == 0 ? 0 : 1;
  }
  std::printf(
      "gap distribution: min %lld q25 %lld median %lld q75 %lld q90 %lld max "
      "%lld; positive %d zero %d negative %d\n",
      static_cast<long long>(gaps.empty() ? 0 : gaps.front()),
      static_cast<long long>(quantile(0.25)),
      static_cast<long long>(quantile(0.50)),
      static_cast<long long>(quantile(0.75)),
      static_cast<long long>(quantile(0.90)),
      static_cast<long long>(gaps.empty() ? 0 : gaps.back()), positive, zero,
      negative);
  std::printf("mean optimum %.1f, mean best-shallow %.1f, mean gap %.1f\n",
              gaps.empty() ? 0.0
                           : static_cast<double>(total_optimum) /
                                 static_cast<double>(gaps.size()),
              gaps.empty() ? 0.0
                           : static_cast<double>(total_shallow) /
                                 static_cast<double>(gaps.size()),
              gaps.empty() ? 0.0
                           : static_cast<double>(total_optimum - total_shallow) /
                                 static_cast<double>(gaps.size()));
  std::printf("wrote %s\n", options.output.c_str());
  return incomplete == 0 ? 0 : 1;
}
