// Exploratory bounded correction around fair D4: expose the frozen search's
// terminal utility, depth, and chance-sample count as parameters, prove that
// the default parameters reproduce the reference decisions exactly, and then
// sweep the one constant that governs how the search trades death risk against
// leaf quality.
//
// Motivation.  Measured decomposition (see
// docs/exploratory/finding-01-score-is-survival.md) shows that Hardcore score
// is dominated by the flat 17,000-point row-rise bonus, so expected score is
// very nearly 3,400 x expected lifetime.  The reference search encodes death as
// a flat -1,000,000 leaf-unit penalty that was never swept, and that constant
// is the search's entire risk calibration.  If it is mis-set relative to the
// leaf scale, the reference is either too timid or too reckless in exactly the
// dimension that produces 94% of the score.
//
// This program changes no existing source.  It re-implements only the
// depth-limited driver of fair-only-depth4.cpp so the constants become runtime
// parameters; the leaf, chance stratification, canonicalization, cache keying,
// column order, and work accounting all come from the unmodified frozen code.

#include "fair-only-depth4-noentry.cpp"

#include "../../../approaches/lifetime-objective/common/harness.hpp"

#include <exception>
#include <limits>
#include <list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace drop7::lifetime::risk {

namespace ref = drop7::fair_only_depth4;
namespace frozen = drop7::fair_only_horizon;

struct SearchParameters {
  int depth = 4;
  int chanceSamples = frozen::kChanceSamples;
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

class ParameterizedSearch {
 public:
  explicit ParameterizedSearch(SearchParameters parameters)
      : parameters_(parameters) {}

  int chooseAction(const State& source, std::uint64_t& work) {
    if (source.game_over) return -1;
    bool mirrored = false;
    const State canonical = cfpi::detail::canonicalState(source, mirrored);
    SearchContext context;
    int action = -1;
    // Iterative deepening exactly as the reference does, so a work-limited
    // decision degrades to the deepest completed ply rather than a partial one.
    for (int depth = 1; depth <= parameters_.depth; ++depth) {
      try {
        const int candidate = rootDecision(canonical, depth, context);
        if (candidate < 0) break;
        action = candidate;
      } catch (const WorkLimitReached&) {
        break;
      }
    }
    if (action < 0) action = centerFirstMove(canonical.board);
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
    double value = 0.0;
    for (int sample = 0; sample < parameters_.chanceSamples; ++sample) {
      checkBudget(context);
      cfpi::detail::StratifiedRandom random{stateSeed, sample,
                                            parameters_.chanceSamples, 0};
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
      move.state.next_disc = cfpi::detail::sampledNextDisc(
          stateSeed, sample, parameters_.chanceSamples);
      bool ignored = false;
      const State next = cfpi::detail::canonicalState(move.state, ignored);
      value += scoreDelta + bestFutureValue(next, depth - 1, context);
    }
    return value / parameters_.chanceSamples;
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

// CHECK-tier gate: at default parameters this driver must select exactly the
// same column as the unmodified reference on every move of every probe game.
bool parityCheck(std::uint32_t seedStart, int games, int maximumMoves,
                 std::ostream& out) {
  ParameterizedSearch mine{SearchParameters{}};
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
        out << "  mismatch seed 0x" << std::hex << seed << std::dec
            << " move " << state.moves_played << ": reference "
            << reference.action << " parameterized " << candidate << '\n';
      }
      MoveResult move;
      if (!playHeadlessMove(state, seed, reference.action, move)) break;
    }
  }
  out << "parity: " << comparedMoves << " moves compared, " << mismatches
      << " mismatches\n";
  return mismatches == 0;
}

struct Options {
  CohortOptions cohort;
  SearchParameters parameters;
  std::string output;
  bool parity = false;
  int parityGames = 3;
  int parityMoves = 40;
};

Options parseOptions(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc;) {
    const std::string key = argv[index];
    if (key == "--parity") {
      options.parity = true;
      index += 1;
      continue;
    }
    if (index + 1 >= argc) throw std::invalid_argument("missing value for " + key);
    const std::string value = argv[index + 1];
    if (key == "--seed-start") {
      options.cohort.seedStart = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
    } else if (key == "--games") {
      options.cohort.games = std::stoi(value);
    } else if (key == "--max-moves") {
      options.cohort.maximumMoves = std::stoi(value);
    } else if (key == "--threads") {
      options.cohort.threads = std::stoi(value);
    } else if (key == "--depth") {
      options.parameters.depth = std::stoi(value);
    } else if (key == "--chance-samples") {
      options.parameters.chanceSamples = std::stoi(value);
    } else if (key == "--terminal-utility") {
      options.parameters.terminalUtility = std::stod(value);
    } else if (key == "--max-work") {
      options.parameters.maximumWork = std::stoull(value);
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
  return options;
}

}  // namespace drop7::lifetime::risk

int main(int argc, char** argv) {
  using namespace drop7;
  using namespace drop7::lifetime;
  try {
    const auto options = risk::parseOptions(argc, argv);
    if (options.parity) {
      const bool ok = risk::parityCheck(options.cohort.seedStart,
                                        options.parityGames,
                                        options.parityMoves, std::cout);
      std::cout << (ok ? "PARITY OK\n" : "PARITY FAILED\n");
      return ok ? 0 : 1;
    }
    const auto started = std::chrono::steady_clock::now();
    auto records = runCohort(options.cohort, [&]() {
      return [search = risk::ParameterizedSearch{options.parameters}](
                 const State& state, std::uint64_t& work) mutable {
        return search.chooseAction(state, work);
      };
    });
    const double wall = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started).count();
    std::ostringstream config;
    config << std::setprecision(12) << "{\"depth\": " << options.parameters.depth
           << ", \"chanceSamples\": " << options.parameters.chanceSamples
           << ", \"terminalUtility\": " << options.parameters.terminalUtility
           << ", \"maximumWork\": " << options.parameters.maximumWork << "}";
    if (options.output.empty()) {
      writeArtifact(std::cout, "parameterized-fair-search", config.str(),
                    options.cohort, records, wall);
    } else {
      std::ofstream file(options.output);
      if (!file) throw std::runtime_error("cannot open " + options.output);
      writeArtifact(file, "parameterized-fair-search", config.str(),
                    options.cohort, records, wall);
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "risk-calibration failed: " << error.what() << '\n';
    return 1;
  }
}
