// Feasibility probe, run before any learned leaf is written.
//
// A learned leaf is only affordable if the number of leaf evaluations per
// decision is small enough for the model's per-state cost.  Nothing in the
// repository records that number, so measure it: total leaf evaluations per
// decision, and the number of DISTINCT leaf states per decision (the ceiling
// on what a perfect leaf memo could save).
//
// This program plays the frozen parameterized search unchanged apart from the
// counters; it never blends anything.

#include "fair-only-depth4-noentry.cpp"

#include "../../../approaches/lifetime-objective/common/harness.hpp"

#include <exception>
#include <limits>
#include <list>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace drop7::lifetime::probe {

namespace ref = drop7::fair_only_depth4;
namespace frozen = drop7::fair_only_horizon;

struct Parameters {
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

struct Context {
  std::unordered_map<std::string, CacheEntry> cache;
  std::list<std::string> order;
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
  std::uint64_t leafCalls = 0;
  std::unordered_set<std::string> distinctLeaves;
};

// The public leaf identity: board, visible next disc, moves until the rise.
// This is exactly the information dataset.py encodes, so it is also the key a
// learned-leaf memo would use.
std::string leafKey(const State& state) {
  std::string key;
  key.reserve(kCellCount + 2);
  for (std::uint8_t cell : state.board) key.push_back(static_cast<char>('0' + cell));
  key.push_back(static_cast<char>('0' + state.next_disc));
  key.push_back(static_cast<char>('0' + state.moves_remaining));
  return key;
}

class Search {
 public:
  explicit Search(Parameters parameters) : parameters_(parameters) {}

  int chooseAction(const State& source, Context& context) {
    if (source.game_over) return -1;
    bool mirrored = false;
    const State canonical = cfpi::detail::canonicalState(source, mirrored);
    int action = -1;
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
    return mirrored && action >= 0 ? kBoardSize - 1 - action : action;
  }

 private:
  void checkBudget(const Context& context) const {
    if (context.work >= parameters_.maximumWork) throw WorkLimitReached{};
  }

  void cacheValue(Context& context, std::string key, double value) const {
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
                        Context& context) const {
    const std::uint32_t stateSeed =
        cfpi::detail::scenarioSeedForState(state, frozen::kPolicySeed, depth);
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

  double evaluateLeaf(const State& state, Context& context) const {
    checkBudget(context);
    ++context.work;
    ++context.leafCalls;
    context.distinctLeaves.insert(leafKey(state));
    const double value = frozen::fairLeaf(state);
    if (!std::isfinite(value)) throw std::runtime_error("non-finite leaf");
    return value;
  }

  double bestFutureValue(const State& state, int depth, Context& context) const {
    ++context.nodes;
    checkBudget(context);
    if (state.game_over) return parameters_.terminalUtility;
    if (depth == 0) return evaluateLeaf(state, context);
    const std::string key = cfpi::detail::dynamicStateKey(state, depth);
    const auto cached = context.cache.find(key);
    if (cached != context.cache.end()) {
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

  int rootDecision(const State& canonical, int depth, Context& context) const {
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

  Parameters parameters_;
};

}  // namespace drop7::lifetime::probe

int main(int argc, char** argv) {
  using namespace drop7;
  using namespace drop7::lifetime;
  std::uint32_t seed = 0xa524'0000u;
  int moves = 40;
  int strata = 5;
  std::uint64_t maxWork = 3'200'000;
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string key = argv[i];
    const std::string value = argv[i + 1];
    if (key == "--seed") seed = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
    else if (key == "--moves") moves = std::stoi(value);
    else if (key == "--chance-samples") strata = std::stoi(value);
    else if (key == "--max-work") maxWork = std::stoull(value, nullptr, 0);
  }
  probe::Parameters parameters;
  parameters.chanceSamples = strata;
  parameters.maximumWork = maxWork;
  probe::Search search{parameters};

  State state = initialHeadlessState(seed);
  std::uint64_t leafTotal = 0, distinctTotal = 0, workTotal = 0, played = 0;
  std::uint64_t leafMax = 0, distinctMax = 0;
  const auto started = std::chrono::steady_clock::now();
  while (!state.game_over && played < static_cast<std::uint64_t>(moves)) {
    probe::Context context;
    const int column = search.chooseAction(state, context);
    leafTotal += context.leafCalls;
    distinctTotal += context.distinctLeaves.size();
    workTotal += context.work;
    leafMax = std::max(leafMax, context.leafCalls);
    distinctMax = std::max<std::uint64_t>(distinctMax, context.distinctLeaves.size());
    ++played;
    MoveResult move;
    if (column < 0 || !playHeadlessMove(state, seed, column, move)) break;
  }
  const double wall = std::chrono::duration<double>(
                          std::chrono::steady_clock::now() - started).count();
  const double n = static_cast<double>(played);
  std::cout << std::fixed << std::setprecision(1)
            << "strata " << strata << "  maxWork " << maxWork
            << "  moves " << played << "\n"
            << "  work/move          " << workTotal / n << "\n"
            << "  leafEvals/move     " << leafTotal / n << "  (max " << leafMax << ")\n"
            << "  distinctLeaves/move " << distinctTotal / n << "  (max " << distinctMax << ")\n"
            << "  dedupRatio         " << static_cast<double>(leafTotal) / std::max<double>(1.0, static_cast<double>(distinctTotal)) << "\n"
            << "  wall s/move        " << std::setprecision(4) << wall / n << "\n";
  return 0;
}
