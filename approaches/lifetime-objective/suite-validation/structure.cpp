// The structure probe: what distinguishes a board where three numbered clears
// per move are available from one where only two are?
//
// `finding-06` §3 and `finding-07` §4 condition the measured clear rate on board
// occupancy and find that occupancy does not explain it: at 25-29 occupied cells
// the clairvoyant planner extracts 4.46 clears per move, the best legal sampling
// planner 3.00, and fair D4 2.59, and past 30 cells fair D4's rate *falls* while
// the clairvoyant's keeps rising.  That difference is the whole distance between
// dying at 117 moves and surviving past 1,000.
//
// Method
//   1. Draw a large sample of positions spanning the occupancy range, from fair
//      play, from weak play, and from a controlled synthetic sampler.
//   2. Label each position with its **achievable** clear rate: the exact optimum
//      of the clear-counting objective over a short horizon, averaged over J
//      independent completions of everything the position leaves open (the
//      hidden value under every cover, the disc tape, and every future risen
//      row).  Averaging over latent completions is what makes the label a
//      function of the PUBLIC position, which is the only kind of label a leaf
//      feature could ever learn.
//   3. Record, for the same position and the same completions, what two cheap
//      fair policies actually extract, so the achievable-minus-achieved gap can
//      be modelled directly.
//   4. Record the 19 features of the frozen leaf, unmodified, alongside ~45
//      candidate structural quantities, so that "is this already in the leaf?"
//      is answered by a variance decomposition rather than by reading weights.
//
// The exact solver is privileged and is used here only to create a diagnostic
// label.  Nothing in this program is a policy.

#include "features.hpp"
#include "policies.hpp"
#include "posmode.hpp"

#include "../flow-ceiling/flow-solver.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

namespace {

using namespace drop7;
using namespace drop7::scenario;
using namespace drop7::suitevalidation;
namespace flow = drop7::flowceiling;
namespace foh = drop7::fair_only_horizon;

struct Options {
  int fair_positions = 384;
  int weak_positions = 192;
  int synthetic_positions = 448;
  int horizon = 8;
  int completions = 4;
  int threads = 8;
  double solve_time_limit = 20.0;
  std::string output;
  bool self_test = false;
};

struct Candidate {
  Scenario base;
  std::string origin;
  std::uint32_t label_seed = 0;

  int completions_solved = 0;
  double achievable_clears = 0.0;   // per move, mean over completions
  double achievable_points = 0.0;   // per move, mean over completions
  double achievable_reveals = 0.0;
  double d2_clears = 0.0;
  double d3_clears = 0.0;
  double d2_deaths = 0.0;
  double d3_deaths = 0.0;
};

// ---------------------------------------------------------------------------
// Position sources
// ---------------------------------------------------------------------------

// A mid-game snapshot of a base-engine game played by a public policy.  The
// base engine has no latent board (audit-01 M2), so latent values are drawn
// here from the lease; the *visible* position is one a real game reached.
bool harvestWithPolicy(std::uint32_t game_seed, std::uint32_t label_seed,
                       Policy& policy, int horizon, int warmup, Scenario& out) {
  State state = initialHeadlessState(game_seed);
  Mulberry32 policy_rng(label_seed);
  std::vector<State> visited;
  std::uint64_t work = 0;
  for (int move = 0; move < 400; ++move) {
    if (state.game_over) break;
    const int column = policy.chooseColumn(state, policy_rng, work);
    if (column < 0) break;
    MoveResult result;
    if (!playHeadlessMove(state, game_seed, column, result)) break;
    if (state.game_over) break;
    visited.push_back(state);
  }
  if (static_cast<int>(visited.size()) <= warmup) return false;
  Mulberry32 random(label_seed ^ 0x5bf0'3635u);
  const std::size_t span = visited.size() - static_cast<std::size_t>(warmup);
  const std::size_t pick = static_cast<std::size_t>(warmup) +
                           static_cast<std::size_t>(random.nextBits() % span);
  const State& snapshot = visited[pick];
  out = Scenario{};
  out.board = snapshot.board;
  out.moves_remaining = static_cast<std::uint8_t>(snapshot.moves_remaining);
  return completeScenario(out, horizon, random);
}

// ---------------------------------------------------------------------------
// Labelling
// ---------------------------------------------------------------------------

void labelCandidate(Candidate& candidate, const Options& options,
                    Policy& d2, Policy& d3) {
  flow::WindowLimits limits;
  limits.max_seconds = options.solve_time_limit;
  limits.tt_capacity = 2'000'000;

  double clears = 0.0;
  double points = 0.0;
  double reveals = 0.0;
  double d2_clears = 0.0;
  double d3_clears = 0.0;
  double d2_deaths = 0.0;
  double d3_deaths = 0.0;
  int solved = 0;

  for (int completion = 0; completion < options.completions; ++completion) {
    Mulberry32 random(derivedSeed(
        leaseSeed(0x2000u + static_cast<std::uint32_t>(completion)),
        candidate.label_seed));
    // Redraw the hidden board, then the future.  Both are things the position
    // leaves open, so the label must average over both.
    Scenario relatented;
    if (!relatentAt(candidate.base, random, relatented)) continue;
    Scenario completed;
    if (!resampleScenarioRandomness(relatented, options.horizon, random,
                                    completed)) {
      continue;
    }

    const flow::WindowResult clear_optimum =
        flow::solveWindow(completed, flow::Objective::kClears, limits);
    if (!clear_optimum.complete) continue;
    const flow::WindowResult point_optimum =
        flow::solveWindow(completed, flow::Objective::kPoints, limits);
    if (!point_optimum.complete) continue;

    ++solved;
    clears += static_cast<double>(clear_optimum.value);
    points += static_cast<double>(point_optimum.value);

    // Replay the clear-optimal principal variation to count covered reveals.
    {
      SearchNode node;
      node.board = completed.board;
      node.latent = completed.latent;
      MoveResult scratch;
      int depth = 0;
      for (int column : clear_optimum.pv) {
        SearchNode child;
        std::int64_t delta = 0;
        bool over = false;
        if (!applyScenarioMove(completed, node, depth, column, child, delta,
                               over, scratch)) {
          break;
        }
        for (const Wave& wave : scratch.waves) reveals += wave.revealed;
        node = child;
        ++depth;
        if (over) break;
      }
    }

    Mulberry32 unused(1u);
    const PlayRecord r2 = playScenario(completed, d2, unused);
    const PlayRecord r3 = playScenario(completed, d3, unused);
    d2_clears += r2.clears;
    d3_clears += r3.clears;
    d2_deaths += r2.died ? 1.0 : 0.0;
    d3_deaths += r3.died ? 1.0 : 0.0;
  }

  candidate.completions_solved = solved;
  if (solved == 0) return;
  const double denominator = solved * static_cast<double>(options.horizon);
  candidate.achievable_clears = clears / denominator;
  candidate.achievable_points = points / denominator;
  candidate.achievable_reveals = reveals / denominator;
  candidate.d2_clears = d2_clears / denominator;
  candidate.d3_clears = d3_clears / denominator;
  candidate.d2_deaths = d2_deaths / solved;
  candidate.d3_deaths = d3_deaths / solved;
}

// ---------------------------------------------------------------------------
// Leaf features, unmodified
// ---------------------------------------------------------------------------

NamedFeatures extractLeaf(const State& state) {
  NamedFeatures f;
  const foh::FairFeatures features = foh::extractFairFeatures(state);
  const auto& h = features.heuristic;
  f.add("leaf_open_columns", h.open_columns);
  f.add("leaf_height_load", h.height_load);
  f.add("leaf_solid_cells", h.solid_cells);
  f.add("leaf_cracked_cells", h.cracked_cells);
  f.add("leaf_numbered_cells", h.numbered_cells);
  f.add("leaf_high_low_numbers", h.high_low_numbers);
  f.add("leaf_direct_potential", h.direct_potential);
  f.add("leaf_latent_chain_potential", h.latent_chain_potential);
  f.add("leaf_cracked_exposure", h.cracked_exposure);
  f.add("leaf_solid_exposure", h.solid_exposure);
  f.add("leaf_adjacent_ones", h.adjacent_ones);
  f.add("leaf_triple_twos", h.triple_twos);
  f.add("leaf_dead_low_numbers", h.dead_low_numbers);
  f.add("leaf_covered_height_risk", features.covered_height_risk);
  f.add("leaf_low_number_height_risk", features.low_number_height_risk);
  f.add("leaf_danger_height_squared", features.danger_height_squared);
  f.add("leaf_roughness", features.roughness);
  f.add("leaf_rise_pressure", features.rise_pressure);
  f.add("leaf_next_disc_vertical_options", features.next_disc_vertical_options);
  f.add("leaf_value", foh::fairLeaf(state));
  return f;
}

// ---------------------------------------------------------------------------
// Gates
// ---------------------------------------------------------------------------

Board mirrorBoard(const Board& board) {
  Board out{};
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      out[indexOf(row, kBoardSize - 1 - column)] = board[indexOf(row, column)];
    }
  }
  return out;
}

// Gate: every candidate structural feature must be invariant under horizontal
// reflection.  A feature that is not is a feature that encodes an accidental
// orientation, and the frozen search canonicalizes by mirror, so such a feature
// could not be used by it consistently.
bool gateReflection() {
  std::cout << "gate: horizontal-reflection invariance of the feature vector\n";
  int checked = 0;
  int violations = 0;
  std::vector<std::string> offenders;
  for (int trial = 0; trial < 512; ++trial) {
    SyntheticOptions synthetic;
    synthetic.horizon = 6;
    synthetic.target_cells = 6 + (trial % 38);
    synthetic.cover_fraction = 0.05 + 0.6 * ((trial % 11) / 10.0);
    Scenario scenario;
    if (!syntheticScenario(leaseSeed(0x0020u + static_cast<std::uint32_t>(trial % 64)) +
                               static_cast<std::uint32_t>(trial),
                           synthetic, scenario)) {
      continue;
    }
    const int disc = scenario.disc_tape[0];
    const NamedFeatures a =
        extractStructure(scenario.board, disc, scenario.moves_remaining);
    const NamedFeatures b = extractStructure(mirrorBoard(scenario.board), disc,
                                             scenario.moves_remaining);
    for (std::size_t index = 0; index < a.values.size(); ++index) {
      ++checked;
      if (std::abs(a.values[index] - b.values[index]) > 1e-9) {
        ++violations;
        if (offenders.size() < 8) offenders.push_back(a.names[index]);
      }
    }
  }
  std::cout << "  " << checked << " feature values compared, " << violations
            << " violations\n";
  for (const std::string& name : offenders) {
    std::cout << "    offender: " << name << "\n";
  }
  return violations == 0;
}

// Gate: the clear-objective window solver must agree with the frozen exact
// solver on the *points* objective, which is the cross-check `flow-run` runs and
// the reason this program may use `flow-solver.hpp` at all.
bool gateSolverAgreement() {
  std::cout << "gate: flow window solver (points) == frozen exact solver\n";
  int compared = 0;
  int mismatches = 0;
  for (int trial = 0; trial < 24; ++trial) {
    SyntheticOptions synthetic;
    synthetic.horizon = 6;
    synthetic.target_cells = 8 + (trial % 30);
    synthetic.cover_fraction = 0.1 + 0.5 * ((trial % 7) / 6.0);
    Scenario scenario;
    if (!syntheticScenario(leaseSeed(0x0060u) + static_cast<std::uint32_t>(trial),
                           synthetic, scenario)) {
      continue;
    }
    SolveOptions frozen;
    frozen.threads = 1;
    frozen.time_limit_seconds = 30.0;
    const SolveResult exact = solveScenario(scenario, frozen);
    flow::WindowLimits limits;
    limits.max_seconds = 30.0;
    const flow::WindowResult window =
        flow::solveWindow(scenario, flow::Objective::kPoints, limits);
    if (!exact.complete || !window.complete) continue;
    ++compared;
    if (exact.optimum != window.value) ++mismatches;
  }
  std::cout << "  " << compared << " scenarios compared, " << mismatches
            << " mismatches\n";
  return compared > 0 && mismatches == 0;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string flag = argv[index];
    const auto value = [&]() -> std::string {
      if (index + 1 >= argc) {
        std::cerr << "missing value for " << flag << "\n";
        std::exit(2);
      }
      return argv[++index];
    };
    if (flag == "--fair") options.fair_positions = std::stoi(value());
    else if (flag == "--weak") options.weak_positions = std::stoi(value());
    else if (flag == "--synthetic") options.synthetic_positions = std::stoi(value());
    else if (flag == "--horizon") options.horizon = std::stoi(value());
    else if (flag == "--completions") options.completions = std::stoi(value());
    else if (flag == "--threads") options.threads = std::stoi(value());
    else if (flag == "--time-limit") options.solve_time_limit = std::stod(value());
    else if (flag == "--csv") options.output = value();
    else if (flag == "--self-test") options.self_test = true;
    else {
      std::cerr << "unknown option " << flag << "\n";
      return 2;
    }
  }
  if (options.threads > 8) {
    std::cerr << "refusing to use more than 8 threads (shared machine)\n";
    return 2;
  }
  if (options.self_test) {
    bool ok = gateReflection();
    ok = gateSolverAgreement() && ok;
    std::cout << (ok ? "SELF-TEST OK\n" : "SELF-TEST FAILED\n");
    return ok ? 0 : 1;
  }
  if (options.output.empty()) {
    std::cerr << "--csv <out.csv> is required\n";
    return 2;
  }

  // Positions are drawn single-threaded and up front so seed consumption is
  // deterministic and independent of the thread count.
  std::vector<Candidate> candidates;
  std::uint32_t cursor = 0x1000u;
  {
    Policy harvest_fair(*findPolicy("d2s5"));
    Policy harvest_weak(*findPolicy("lowest-column"));
    int attempts = 0;
    while (static_cast<int>(candidates.size()) < options.fair_positions &&
           attempts < options.fair_positions * 4) {
      ++attempts;
      const std::uint32_t seed = leaseSeed(cursor++);
      Candidate candidate;
      if (!harvestWithPolicy(seed, seed ^ 0x1357'9bdfu, harvest_fair,
                             options.horizon, 6, candidate.base)) {
        continue;
      }
      candidate.origin = "fair-d2";
      candidate.label_seed = seed;
      candidates.push_back(std::move(candidate));
    }
    const int fair_count = static_cast<int>(candidates.size());
    attempts = 0;
    while (static_cast<int>(candidates.size()) - fair_count <
               options.weak_positions &&
           attempts < options.weak_positions * 4) {
      ++attempts;
      const std::uint32_t seed = leaseSeed(cursor++);
      Candidate candidate;
      if (!harvestWithPolicy(seed, seed ^ 0x2468'ace0u, harvest_weak,
                             options.horizon, 6, candidate.base)) {
        continue;
      }
      candidate.origin = "lowest-column";
      candidate.label_seed = seed;
      candidates.push_back(std::move(candidate));
    }
    const int harvested = static_cast<int>(candidates.size());
    const std::array<NumberProfile, 3> profiles{{NumberProfile::kUniform,
                                                 NumberProfile::kLowHeavy,
                                                 NumberProfile::kHighHeavy}};
    attempts = 0;
    while (static_cast<int>(candidates.size()) - harvested <
               options.synthetic_positions &&
           attempts < options.synthetic_positions * 6) {
      const int index = attempts++;
      SyntheticOptions synthetic;
      synthetic.horizon = options.horizon;
      synthetic.target_cells = 8 + (index % 38);
      synthetic.cover_fraction = 0.05 + 0.60 * ((index / 38) % 8) / 7.0;
      synthetic.profile = profiles[static_cast<std::size_t>(index % 3)];
      const std::uint32_t seed = leaseSeed(cursor++);
      Candidate candidate;
      if (!syntheticScenario(seed, synthetic, candidate.base)) continue;
      candidate.origin = "synthetic";
      candidate.label_seed = seed;
      candidates.push_back(std::move(candidate));
    }
  }
  std::printf("structure probe: %zu positions, horizon %d, %d completions\n",
              candidates.size(), options.horizon, options.completions);
  std::printf("seed lease SEEDLEASE-A52-SUITE, offsets 0x1000..0x%x\n",
              cursor - 1);

  std::atomic<std::size_t> next{0};
  std::atomic<int> done{0};
  const auto started = std::chrono::steady_clock::now();
  const auto worker = [&]() {
    Policy d2(*findPolicy("d2s5"));
    Policy d3(*findPolicy("d3s5"));
    for (;;) {
      const std::size_t index = next.fetch_add(1);
      if (index >= candidates.size()) return;
      labelCandidate(candidates[index], options, d2, d3);
      const int completed = ++done;
      if (completed % 64 == 0) {
        std::printf("  %d/%zu labelled\n", completed, candidates.size());
        std::fflush(stdout);
      }
    }
  };
  std::vector<std::thread> pool;
  for (int slot = 0; slot < std::max(1, options.threads); ++slot) {
    pool.emplace_back(worker);
  }
  for (std::thread& thread : pool) thread.join();
  const double wall =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();

  std::ofstream out(options.output);
  if (!out) {
    std::cerr << "cannot write " << options.output << "\n";
    return 2;
  }
  bool header_written = false;
  int usable = 0;
  for (const Candidate& candidate : candidates) {
    if (candidate.completions_solved == 0) continue;
    ++usable;
    const State state = scenarioStartState(candidate.base);
    const NamedFeatures structure = extractStructure(
        candidate.base.board, state.next_disc, candidate.base.moves_remaining);
    const NamedFeatures leaf = extractLeaf(state);
    if (!header_written) {
      out << "id,origin,completionsSolved,achievableClears,achievablePoints,"
             "achievableReveals,d2Clears,d3Clears,d2Deaths,d3Deaths";
      for (const std::string& name : structure.names) out << ',' << name;
      for (const std::string& name : leaf.names) out << ',' << name;
      out << "\n";
      header_written = true;
    }
    out << candidate.base.id << ',' << candidate.origin << ','
        << candidate.completions_solved << ',' << candidate.achievable_clears
        << ',' << candidate.achievable_points << ','
        << candidate.achievable_reveals << ',' << candidate.d2_clears << ','
        << candidate.d3_clears << ',' << candidate.d2_deaths << ','
        << candidate.d3_deaths;
    for (double v : structure.values) out << ',' << v;
    for (double v : leaf.values) out << ',' << v;
    out << "\n";
  }
  std::printf("labelled %d/%zu positions in %.1f s on %d threads\n", usable,
              candidates.size(), wall, options.threads);
  std::printf("wrote %s\n", options.output.c_str());
  return usable > 0 ? 0 : 1;
}
