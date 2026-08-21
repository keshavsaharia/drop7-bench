// Factored chance node: separate the next-visible-disc expectation from the
// covered-disc reveal expectation.
//
// The frozen fair search has one knob, `chance_samples`, that governs two
// distinct random quantities at every chance node:
//
//   * the next visible disc, uniform on 7 values, drawn by
//     `cfpi::detail::sampledNextDisc(state_seed, sample, chance_samples)`
//     (src/core/native/public-behavior.hpp:736), and
//   * the covered-disc reveals produced by a cascade, drawn by
//     `cfpi::detail::StratifiedRandom{state_seed, sample, chance_samples, 0}`
//     (src/core/native/public-behavior.hpp:595) inside
//     `resolveCascadeSampled` (:655), where a wave exposes a variable number of
//     cells and each takes an independent uniform 1..7 value.
//
// Both are indexed by the *same* `sample` counter and stratified over the
// *same* `count`, so `chance_samples` cannot be raised for one without raising
// it for the other, and sample i's disc value is welded to sample i's reveal
// values (public-behavior.hpp:824-846; the frozen driver's copy is at
// approaches/fair-expectimax/reference/fair-only-horizon.cpp:209-238 and
// fair-only-depth4.cpp:137-163).
//
// At seven strata the next-disc expectation is exact (7 strata over 7 uniform
// atoms), so all remaining chance-node error is reveal sampling.  This program
// factors the chance node into `--disc-samples N` x `--reveal-samples M`,
// giving N*M scenarios per action:
//
//   scenario index s = r * N + d   (d in [0,N), r in [0,M), total T = N*M)
//   reveals: StratifiedRandom{state_seed, s, T, 0}
//   disc:    sampledNextDisc(state_seed, d, N)
//
// Two properties make this a strict generalization rather than a new policy:
//
//   1. At M = 1 the indexing collapses to s = d and T = N, so every draw is
//      byte-identical to the single-knob search at chance_samples = N.  N = 5,
//      M = 1 is therefore the frozen reference and N = 7, M = 1 the existing
//      seven-strata arm.  Both are checked move-by-move below.
//   2. For M > 1 the reveal marginal is stratified over all T strata while the
//      disc keeps exactly N equally weighted strata, and within one disc value
//      the M reveal draws are spaced 1/M apart in the unit interval (s jumps by
//      N in a cyclic group of size N*M), so each disc branch also sees a
//      properly stratified reveal sample.
//
// This program changes no existing source.  The frozen reference and the
// single-knob parameterized search are consumed unmodified, via generated
// copies that differ only in their entry-point line (see build.sh).

#include "oracle/risk-calibration-noentry.cpp"

#include <atomic>
#include <exception>
#include <limits>
#include <list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace drop7::lifetime::reveal {

namespace ref = drop7::fair_only_depth4;
namespace frozen = drop7::fair_only_horizon;
namespace single = drop7::lifetime::risk;

// ---------------------------------------------------------------------------
// Work bounds.  Identical arithmetic to fair-only-depth4.cpp:52-62, with the
// branching factor generalized from 7 * chance_samples to 7 * N * M.
// ---------------------------------------------------------------------------

inline std::uint64_t power(std::uint64_t base, int exponent) {
  std::uint64_t result = 1;
  for (int count = 0; count < exponent; ++count) result *= base;
  return result;
}

inline std::uint64_t worstCaseIterativeWork(std::uint64_t branches,
                                            int maximumDepth) {
  std::uint64_t result = 0;
  for (int depth = 1; depth <= maximumDepth; ++depth) {
    for (int level = 1; level <= depth; ++level) result += power(branches, level);
    result += power(branches, depth);
  }
  return result;
}

inline std::uint64_t worstCaseIterativeCacheEntries(std::uint64_t branches,
                                                    int maximumDepth) {
  std::uint64_t result = 0;
  for (int depth = 2; depth <= maximumDepth; ++depth) {
    for (int level = 1; level < depth; ++level) result += power(branches, level);
  }
  return result;
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

struct SearchParameters {
  int depth = 4;
  int discSamples = frozen::kChanceSamples;
  int revealSamples = 1;
  double terminalUtility = frozen::kTerminalUtility;
  std::uint64_t maximumWork = 3'200'000;
  std::size_t maximumCacheEntries = 60'000;
};

class WorkLimitReached : public std::exception {};

struct CacheEntry {
  double value = 0.0;
  std::list<std::string>::iterator order;
};

struct SearchContext {
  std::unordered_map<std::string, CacheEntry> cache;
  std::list<std::string> order;
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
  std::uint64_t cacheHits = 0;
};

// Bound diagnostics, shared across worker threads.  These exist so that "the
// work bound never bound" is an empirical statement and not a projection: a
// decision whose completed depth is below the target depth is a silently
// degraded decision, which is exactly the failure that turns a chance-sampling
// experiment into an accidental depth experiment.
inline std::atomic<std::uint64_t> gDecisions{0};
inline std::atomic<std::uint64_t> gDecisionsBelowTarget{0};
inline std::atomic<std::uint64_t> gWorkLimitEvents{0};
inline std::atomic<int> gMinCompletedDepth{1 << 30};
inline std::atomic<std::uint64_t> gMaxDecisionWork{0};

inline void resetDiagnostics() {
  gDecisions = 0;
  gDecisionsBelowTarget = 0;
  gWorkLimitEvents = 0;
  gMinCompletedDepth = 1 << 30;
  gMaxDecisionWork = 0;
}

inline void recordMaximum(std::atomic<std::uint64_t>& slot, std::uint64_t value) {
  std::uint64_t seen = slot.load();
  while (value > seen && !slot.compare_exchange_weak(seen, value)) {
  }
}

inline void recordMinimum(std::atomic<int>& slot, int value) {
  int seen = slot.load();
  while (value < seen && !slot.compare_exchange_weak(seen, value)) {
  }
}

class FactoredSearch {
 public:
  explicit FactoredSearch(SearchParameters parameters)
      : parameters_(parameters) {}

  int chooseAction(const State& source, std::uint64_t& work) {
    if (source.game_over) return -1;
    bool mirrored = false;
    const State canonical = cfpi::detail::canonicalState(source, mirrored);
    SearchContext context;
    int action = -1;
    int completedDepth = 0;
    // Iterative deepening exactly as the reference does, so a work-limited
    // decision degrades to the deepest completed ply rather than a partial one.
    for (int depth = 1; depth <= parameters_.depth; ++depth) {
      try {
        const int candidate = rootDecision(canonical, depth, context);
        if (candidate < 0) break;
        action = candidate;
        completedDepth = depth;
      } catch (const WorkLimitReached&) {
        gWorkLimitEvents.fetch_add(1);
        break;
      }
    }
    if (action < 0) action = centerFirstMove(canonical.board);
    gDecisions.fetch_add(1);
    if (completedDepth < parameters_.depth) gDecisionsBelowTarget.fetch_add(1);
    recordMinimum(gMinCompletedDepth, completedDepth);
    recordMaximum(gMaxDecisionWork, context.work);
    work += context.work;
    return mirrored && action >= 0 ? kBoardSize - 1 - action : action;
  }

 private:
  void checkBudget(const SearchContext& context) const {
    if (context.work >= parameters_.maximumWork) throw WorkLimitReached{};
  }

  void cacheValue(SearchContext& context, std::string key, double value) const {
    const auto prior = context.cache.find(key);
    if (prior != context.cache.end()) {
      context.order.erase(prior->second.order);
      context.cache.erase(prior);
    }
    while (context.cache.size() >= parameters_.maximumCacheEntries) {
      const std::string& oldest = context.order.front();
      context.cache.erase(oldest);
      context.order.pop_front();
    }
    context.order.push_back(std::move(key));
    const auto order = std::prev(context.order.end());
    context.cache.emplace(*order, CacheEntry{value, order});
  }

  double evaluateAction(const State& state, int column, int depth,
                        SearchContext& context) const {
    const std::uint32_t stateSeed = cfpi::detail::scenarioSeedForState(
        state, frozen::kPolicySeed, depth);
    const int discSamples = parameters_.discSamples;
    const int revealSamples = parameters_.revealSamples;
    const int total = discSamples * revealSamples;
    double value = 0.0;
    for (int disc = 0; disc < discSamples; ++disc) {
      for (int rev = 0; rev < revealSamples; ++rev) {
        checkBudget(context);
        // s = rev * N + disc collapses to s = disc when M == 1, which is what
        // makes the M == 1 configurations bit-identical to the single-knob
        // search; for M > 1 it spreads one disc branch's reveal draws evenly
        // across the T = N*M reveal strata instead of giving it a contiguous
        // block.
        const int scenario = rev * discSamples + disc;
        cfpi::detail::StratifiedRandom random{stateSeed, scenario, total, 0};
        MoveResult move;
        const bool played =
            cfpi::detail::playMoveSampled(state, column, random, move);
        ++context.work;
        if (!played) {
          value += parameters_.terminalUtility;
          continue;
        }
        const double scoreDelta = static_cast<double>(move.score_delta);
        if (move.state.game_over) {
          value += scoreDelta + parameters_.terminalUtility;
          continue;
        }
        move.state.score = 0;
        move.state.next_disc =
            cfpi::detail::sampledNextDisc(stateSeed, disc, discSamples);
        bool ignored = false;
        const State next = cfpi::detail::canonicalState(move.state, ignored);
        value += scoreDelta + bestFutureValue(next, depth - 1, context);
      }
    }
    return value / static_cast<double>(total);
  }

  double evaluateLeaf(const State& state, SearchContext& context) const {
    checkBudget(context);
    ++context.work;
    const double value = frozen::fairLeaf(state);
    if (!std::isfinite(value)) {
      throw std::runtime_error("leaf returned a non-finite value");
    }
    return value;
  }

  double bestFutureValue(const State& state, int depth,
                         SearchContext& context) const {
    ++context.nodes;
    checkBudget(context);
    if (state.game_over) return parameters_.terminalUtility;
    if (depth == 0) return evaluateLeaf(state, context);
    const std::string key = cfpi::detail::dynamicStateKey(state, depth);
    const auto cached = context.cache.find(key);
    if (cached != context.cache.end()) {
      ++context.cacheHits;
      const double value = cached->second.value;
      context.order.splice(context.order.end(), context.order,
                           cached->second.order);
      return value;
    }
    double best = -std::numeric_limits<double>::infinity();
    for (const int column : cfpi::detail::kColumnOrder) {
      if (!isLegal(state.board, column)) continue;
      best = std::max(best, evaluateAction(state, column, depth, context));
    }
    if (!std::isfinite(best)) best = parameters_.terminalUtility;
    cacheValue(context, key, best);
    return best;
  }

  int rootDecision(const State& canonical, int depth,
                   SearchContext& context) const {
    int action = -1;
    double bestValue = -std::numeric_limits<double>::infinity();
    for (const int column : cfpi::detail::kColumnOrder) {
      if (!isLegal(canonical.board, column)) continue;
      const double value = evaluateAction(canonical, column, depth, context);
      if (value > bestValue) {
        bestValue = value;
        action = column;
      }
    }
    return action;
  }

  SearchParameters parameters_;
};

// ---------------------------------------------------------------------------
// CHECK gate
// ---------------------------------------------------------------------------

// Gate A: N = 5, M = 1, depth 4, frozen work bound must select exactly the
// column that the unmodified frozen reference selects, on every move.
bool parityAgainstFrozen(std::uint32_t seedStart, int games, int maximumMoves,
                         std::ostream& out) {
  SearchParameters parameters;  // depth 4, N = 5, M = 1, 3'200'000 work
  FactoredSearch mine{parameters};
  std::uint64_t mismatches = 0;
  std::uint64_t comparedMoves = 0;
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed = seedStart + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < maximumMoves) {
      const ref::SearchDecision reference = ref::chooseDepth4Action(state);
      std::uint64_t work = 0;
      const int candidate = mine.chooseAction(state, work);
      ++comparedMoves;
      if (candidate != reference.action) {
        ++mismatches;
        out << "  mismatch seed 0x" << std::hex << seed << std::dec << " move "
            << state.moves_played << ": frozen " << reference.action
            << " factored " << candidate << '\n';
      }
      MoveResult move;
      if (!playHeadlessMove(state, seed, reference.action, move)) break;
    }
  }
  out << "gate A (N=5, M=1, depth 4) vs frozen fair-only-depth4: "
      << comparedMoves << " moves compared, " << mismatches << " mismatches\n";
  return mismatches == 0;
}

// Gate B: at M = 1 the factored search must select exactly the column that the
// single-knob parameterized search selects at chance_samples = N, on every move,
// at the same depth and the same work and cache bounds.  The oracle here is the
// actual source of the existing arm, consumed unmodified.
bool parityAgainstSingleKnob(const SearchParameters& parameters,
                             std::uint32_t seedStart, int games,
                             int maximumMoves, std::ostream& out) {
  if (parameters.revealSamples != 1) {
    out << "gate B requires --reveal-samples 1\n";
    return false;
  }
  single::SearchParameters legacy;
  legacy.depth = parameters.depth;
  legacy.chanceSamples = parameters.discSamples;
  legacy.terminalUtility = parameters.terminalUtility;
  legacy.maximumWork = parameters.maximumWork;
  legacy.maximumCacheEntries = parameters.maximumCacheEntries;
  single::ParameterizedSearch oracle{legacy};
  FactoredSearch mine{parameters};

  std::uint64_t mismatches = 0;
  std::uint64_t comparedMoves = 0;
  std::uint64_t workMismatches = 0;
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed = seedStart + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < maximumMoves) {
      std::uint64_t oracleWork = 0;
      const int expected = oracle.chooseAction(state, oracleWork);
      std::uint64_t mineWork = 0;
      const int candidate = mine.chooseAction(state, mineWork);
      ++comparedMoves;
      if (candidate != expected) {
        ++mismatches;
        out << "  mismatch seed 0x" << std::hex << seed << std::dec << " move "
            << state.moves_played << ": single-knob " << expected
            << " factored " << candidate << '\n';
      }
      if (mineWork != oracleWork) ++workMismatches;
      MoveResult move;
      if (expected < 0) break;
      if (!playHeadlessMove(state, seed, expected, move)) break;
    }
  }
  out << "gate B (N=" << parameters.discSamples << ", M=1, depth "
      << parameters.depth << ") vs single-knob chance_samples="
      << parameters.discSamples << ": " << comparedMoves
      << " moves compared, " << mismatches << " action mismatches, "
      << workMismatches << " logical-work mismatches\n";
  return mismatches == 0 && workMismatches == 0;
}

// ---------------------------------------------------------------------------
// Command line
// ---------------------------------------------------------------------------

struct Options {
  CohortOptions cohort;
  SearchParameters parameters;
  std::string output;
  bool parityFrozen = false;
  bool parityLegacy = false;
  bool workBoundOnly = false;
  bool autoCache = false;
  int parityGames = 3;
  int parityMoves = 40;
};

Options parseOptions(int argc, char** argv) {
  Options options;
  bool workSet = false;
  for (int index = 1; index < argc;) {
    const std::string key = argv[index];
    if (key == "--parity") {
      options.parityFrozen = true;
      index += 1;
      continue;
    }
    if (key == "--gate") {
      options.parityLegacy = true;
      index += 1;
      continue;
    }
    if (key == "--work-bound") {
      options.workBoundOnly = true;
      index += 1;
      continue;
    }
    if (key == "--auto-cache") {
      options.autoCache = true;
      index += 1;
      continue;
    }
    if (key == "--quiet") {
      options.cohort.quiet = true;
      index += 1;
      continue;
    }
    if (index + 1 >= argc) throw std::invalid_argument("missing value for " + key);
    const std::string value = argv[index + 1];
    if (key == "--seed-start") {
      options.cohort.seedStart =
          static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
    } else if (key == "--games") {
      options.cohort.games = std::stoi(value);
    } else if (key == "--max-moves") {
      options.cohort.maximumMoves = std::stoi(value);
    } else if (key == "--threads") {
      options.cohort.threads = std::stoi(value);
    } else if (key == "--depth") {
      options.parameters.depth = std::stoi(value);
    } else if (key == "--disc-samples") {
      options.parameters.discSamples = std::stoi(value);
    } else if (key == "--reveal-samples") {
      options.parameters.revealSamples = std::stoi(value);
    } else if (key == "--terminal-utility") {
      options.parameters.terminalUtility = std::stod(value);
    } else if (key == "--max-work") {
      options.parameters.maximumWork = std::stoull(value);
      workSet = true;
    } else if (key == "--max-cache") {
      options.parameters.maximumCacheEntries =
          static_cast<std::size_t>(std::stoull(value));
    } else if (key == "--parity-games") {
      options.parityGames = std::stoi(value);
    } else if (key == "--parity-moves") {
      options.parityMoves = std::stoi(value);
    } else if (key == "--output") {
      options.output = value;
    } else {
      throw std::invalid_argument("unknown option " + key);
    }
    index += 2;
  }
  if (options.parameters.discSamples < 1 || options.parameters.revealSamples < 1) {
    throw std::invalid_argument("--disc-samples and --reveal-samples must be >= 1");
  }
  // The work bound is load-bearing: a bound sized for a smaller branching
  // factor silently degrades the search to a shallower completed depth and the
  // run then reports a depth result as a chance-sampling result.  Refuse to
  // guess it.
  if (!options.workBoundOnly && !options.parityFrozen && !options.parityLegacy &&
      !workSet) {
    throw std::invalid_argument(
        "--max-work must be stated explicitly; run --work-bound to compute it");
  }
  if (options.autoCache) {
    const std::uint64_t branches =
        static_cast<std::uint64_t>(kBoardSize) *
        static_cast<std::uint64_t>(options.parameters.discSamples) *
        static_cast<std::uint64_t>(options.parameters.revealSamples);
    const std::uint64_t needed =
        worstCaseIterativeCacheEntries(branches, options.parameters.depth);
    options.parameters.maximumCacheEntries = static_cast<std::size_t>(
        std::max<std::uint64_t>(60'000, needed + 1));
  }
  return options;
}

void printWorkBound(const SearchParameters& parameters, std::ostream& out) {
  const std::uint64_t branches = static_cast<std::uint64_t>(kBoardSize) *
                                 static_cast<std::uint64_t>(parameters.discSamples) *
                                 static_cast<std::uint64_t>(parameters.revealSamples);
  out << "depth=" << parameters.depth << " N=" << parameters.discSamples
      << " M=" << parameters.revealSamples << " branching=" << branches
      << " worstCaseWork=" << worstCaseIterativeWork(branches, parameters.depth)
      << " worstCaseCacheEntries="
      << worstCaseIterativeCacheEntries(branches, parameters.depth) << '\n';
}

}  // namespace drop7::lifetime::reveal

int main(int argc, char** argv) {
  using namespace drop7;
  using namespace drop7::lifetime;
  namespace rs = drop7::lifetime::reveal;
  try {
    const auto options = rs::parseOptions(argc, argv);
    if (options.workBoundOnly) {
      rs::printWorkBound(options.parameters, std::cout);
      return 0;
    }
    if (options.parityFrozen || options.parityLegacy) {
      bool ok = true;
      if (options.parityFrozen) {
        ok = rs::parityAgainstFrozen(options.cohort.seedStart,
                                     options.parityGames, options.parityMoves,
                                     std::cout) && ok;
      }
      if (options.parityLegacy) {
        ok = rs::parityAgainstSingleKnob(options.parameters,
                                         options.cohort.seedStart,
                                         options.parityGames,
                                         options.parityMoves, std::cout) && ok;
      }
      std::cout << (ok ? "CHECK OK\n" : "CHECK FAILED\n");
      return ok ? 0 : 1;
    }

    rs::printWorkBound(options.parameters, std::cerr);
    rs::resetDiagnostics();
    const auto started = std::chrono::steady_clock::now();
    auto records = runCohort(options.cohort, [&]() {
      return [search = rs::FactoredSearch{options.parameters}](
                 const State& state, std::uint64_t& work) mutable {
        return search.chooseAction(state, work);
      };
    });
    const double wall = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started).count();

    const std::uint64_t branches =
        static_cast<std::uint64_t>(kBoardSize) *
        static_cast<std::uint64_t>(options.parameters.discSamples) *
        static_cast<std::uint64_t>(options.parameters.revealSamples);
    std::ostringstream config;
    config << std::setprecision(12) << "{\"depth\": " << options.parameters.depth
           << ", \"discSamples\": " << options.parameters.discSamples
           << ", \"revealSamples\": " << options.parameters.revealSamples
           << ", \"scenariosPerAction\": "
           << options.parameters.discSamples * options.parameters.revealSamples
           << ", \"branchingFactor\": " << branches
           << ", \"terminalUtility\": " << options.parameters.terminalUtility
           << ", \"maximumWork\": " << options.parameters.maximumWork
           << ", \"maximumCacheEntries\": "
           << options.parameters.maximumCacheEntries
           << ", \"worstCaseWork\": "
           << rs::worstCaseIterativeWork(branches, options.parameters.depth)
           << ", \"worstCaseCacheEntries\": "
           << rs::worstCaseIterativeCacheEntries(branches,
                                                 options.parameters.depth)
           << ", \"decisions\": " << rs::gDecisions.load()
           << ", \"decisionsBelowTargetDepth\": "
           << rs::gDecisionsBelowTarget.load()
           << ", \"workLimitEvents\": " << rs::gWorkLimitEvents.load()
           << ", \"minCompletedDepth\": " << rs::gMinCompletedDepth.load()
           << ", \"maxDecisionWork\": " << rs::gMaxDecisionWork.load() << "}";
    if (options.output.empty()) {
      writeArtifact(std::cout, "factored-chance-fair-search", config.str(),
                    options.cohort, records, wall);
    } else {
      std::ofstream file(options.output);
      if (!file) throw std::runtime_error("cannot open " + options.output);
      writeArtifact(file, "factored-chance-fair-search", config.str(),
                    options.cohort, records, wall);
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "reveal-sampling failed: " << error.what() << '\n';
    return 1;
  }
}
