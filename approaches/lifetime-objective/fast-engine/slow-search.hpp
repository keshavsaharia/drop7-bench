#pragma once
// The unoptimised comparator: the frozen fair depth-4 search with depth,
// chance-sample count, terminal utility, work bound and cache bound exposed as
// parameters, built exclusively from the unmodified frozen primitives
// (cfpi::detail::playMoveSampled, cfpi::detail::dynamicStateKey,
// std::unordered_map<std::string, ...> + std::list<std::string>,
// frozen::fairLeaf).
//
// It exists for two reasons: it is the semantic anchor the fast search is
// gated against at configurations the frozen binary cannot itself run
// (depth 5, seven strata), and it is the "before" arm of every timing table.
// gate-search proves it reproduces ref::chooseDepth4Action exactly at its
// default parameters, which is what licenses using it as the anchor.
//
// This file is a new file in this approach's own directory.  It copies the
// reference's control flow verbatim; it does not modify it.

#include "fair-only-depth4-noentry.cpp"

#include <exception>
#include <limits>
#include <list>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace drop7::fast {

namespace ref = drop7::fair_only_depth4;
namespace frozen = drop7::fair_only_horizon;

struct SlowSearchParameters {
  int depth = 4;
  int chance_samples = frozen::kChanceSamples;
  double terminal_utility = frozen::kTerminalUtility;
  std::uint64_t maximum_work = 3'200'000;
  std::size_t maximum_cache_entries = 60'000;
  std::uint32_t policy_seed = frozen::kPolicySeed;
};

struct SlowSearchMetrics {
  int action = -1;
  int completed_depth = 0;
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
  std::uint64_t cache_hits = 0;
  std::size_t cache_entries = 0;
};

class SlowWorkLimitReached : public std::exception {};

class SlowSearch {
 public:
  explicit SlowSearch(SlowSearchParameters parameters)
      : parameters_(parameters) {}

  int chooseAction(const State& source, SlowSearchMetrics& metrics) {
    metrics = SlowSearchMetrics{};
    if (source.game_over) return -1;
    bool mirrored = false;
    const State canonical = cfpi::detail::canonicalState(source, mirrored);
    Context context;
    int action = -1;
    int completed_depth = 0;
    for (int depth = 1; depth <= parameters_.depth; ++depth) {
      try {
        const int candidate = rootDecision(canonical, depth, context);
        if (candidate < 0) break;
        action = candidate;
        completed_depth = depth;
      } catch (const SlowWorkLimitReached&) {
        break;
      }
    }
    if (action < 0) action = centerFirstMove(canonical.board);
    metrics.completed_depth = completed_depth;
    metrics.nodes = context.nodes;
    metrics.work = context.work;
    metrics.cache_hits = context.cache_hits;
    metrics.cache_entries = context.cache.size();
    metrics.action = mirrored && action >= 0 ? kBoardSize - 1 - action : action;
    return metrics.action;
  }

  int chooseAction(const State& source, std::uint64_t& work) {
    SlowSearchMetrics metrics;
    const int action = chooseAction(source, metrics);
    work += metrics.work;
    return action;
  }

 private:
  struct Entry {
    double value = 0.0;
    std::list<std::string>::iterator order;
  };

  struct Context {
    std::unordered_map<std::string, Entry> cache;
    std::list<std::string> order;
    std::uint64_t nodes = 0;
    std::uint64_t work = 0;
    std::uint64_t cache_hits = 0;
  };

  void checkBudget(const Context& context) const {
    if (context.work >= parameters_.maximum_work) throw SlowWorkLimitReached{};
  }

  void cacheValue(Context& context, std::string key, double value) const {
    const auto prior = context.cache.find(key);
    if (prior != context.cache.end()) {
      context.order.erase(prior->second.order);
      context.cache.erase(prior);
    }
    while (context.cache.size() >= parameters_.maximum_cache_entries) {
      const std::string& oldest = context.order.front();
      context.cache.erase(oldest);
      context.order.pop_front();
    }
    context.order.push_back(key);
    const auto order = std::prev(context.order.end());
    context.cache.emplace(std::move(key), Entry{value, order});
  }

  double evaluateAction(const State& state, int column, int depth,
                        Context& context) const {
    const std::uint32_t state_seed = cfpi::detail::scenarioSeedForState(
        state, parameters_.policy_seed, depth);
    double value = 0.0;
    for (int sample = 0; sample < parameters_.chance_samples; ++sample) {
      checkBudget(context);
      cfpi::detail::StratifiedRandom random{state_seed, sample,
                                            parameters_.chance_samples, 0};
      MoveResult move;
      const bool played =
          cfpi::detail::playMoveSampled(state, column, random, move);
      ++context.work;
      if (!played) {
        value += parameters_.terminal_utility;
        continue;
      }
      const double score_delta = static_cast<double>(move.score_delta);
      if (move.state.game_over) {
        value += score_delta + parameters_.terminal_utility;
        continue;
      }
      move.state.score = 0;
      move.state.next_disc = cfpi::detail::sampledNextDisc(
          state_seed, sample, parameters_.chance_samples);
      bool ignored = false;
      const State next = cfpi::detail::canonicalState(move.state, ignored);
      value += score_delta + bestFutureValue(next, depth - 1, context);
    }
    return value / parameters_.chance_samples;
  }

  double evaluateLeaf(const State& state, Context& context) const {
    checkBudget(context);
    ++context.work;
    const double value = frozen::fairLeaf(state);
    if (!std::isfinite(value)) {
      throw std::runtime_error("slow leaf returned a non-finite value");
    }
    return value;
  }

  double bestFutureValue(const State& state, int depth,
                         Context& context) const {
    ++context.nodes;
    checkBudget(context);
    if (state.game_over) return parameters_.terminal_utility;
    if (depth == 0) return evaluateLeaf(state, context);
    const std::string key = cfpi::detail::dynamicStateKey(state, depth);
    const auto cached = context.cache.find(key);
    if (cached != context.cache.end()) {
      ++context.cache_hits;
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
    if (!std::isfinite(best)) best = parameters_.terminal_utility;
    cacheValue(context, key, best);
    return best;
  }

  int rootDecision(const State& canonical, int depth, Context& context) const {
    int action = -1;
    double best_value = -std::numeric_limits<double>::infinity();
    for (const int column : cfpi::detail::kColumnOrder) {
      if (!isLegal(canonical.board, column)) continue;
      const double value = evaluateAction(canonical, column, depth, context);
      if (value > best_value) {
        best_value = value;
        action = column;
      }
    }
    return action;
  }

  SlowSearchParameters parameters_;
};

}  // namespace drop7::fast
