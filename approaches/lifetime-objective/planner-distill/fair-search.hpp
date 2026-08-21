#pragma once

// The frozen fair expectimax with its depth, chance-strata count, terminal
// utility, work bound and *leaf* exposed as parameters, plus a root-value
// accessor.
//
// This is the same construction as
// `approaches/lifetime-objective/risk-calibration/search.cpp`, whose `--parity`
// gate proves the driver selects exactly the reference column on every move at
// default parameters.  It is re-derived here rather than included because that
// file ends in a real `int main`, and because two things are needed that it
// does not expose:
//
//   1. `rootValues` - the value of EVERY legal root column in play
//      orientation, so a learned ranker can be scored against the search's own
//      ranking of the same siblings rather than only against its argmax; and
//   2. a pluggable leaf, so a student evaluator can be blended into the leaf
//      the way `approaches/lifetime-objective/learned-leaf/search.cpp` does:
//
//          leafValue = (1 - w) * frozen::fairLeaf(s) + w * scale * student(s)
//
//      with `w = 0` short-circuiting to the frozen leaf *before* the model is
//      touched, so the comparator arm is the reference bit-for-bit and costs
//      exactly what the reference costs.
//
// Everything that defines the search - the leaf, chance stratification,
// canonicalisation, cache keying, column order, iterative deepening and work
// accounting - comes from the unmodified frozen code in
// `approaches/fair-expectimax/reference/`.

#include "fair-only-depth4-noentry.cpp"

#include <cmath>
#include <exception>
#include <limits>
#include <list>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace drop7::distill {

namespace ref = drop7::fair_only_depth4;
namespace frozen = drop7::fair_only_horizon;

using drop7::State;
using drop7::MoveResult;

// A leaf evaluator returning a value on the frozen leaf's own scale.
struct LeafModel {
  virtual ~LeafModel() = default;
  virtual double value(const State& state) const = 0;
};

struct SearchParameters {
  int depth = 4;
  int chanceSamples = frozen::kChanceSamples;
  double terminalUtility = frozen::kTerminalUtility;
  std::uint64_t maximumWork = 3'200'000;
  std::size_t maximumCacheEntries = 60'000;
  double leafWeight = 0.0;   // 0 => the frozen leaf, bit for bit
  double leafScale = 1.0;
  const LeafModel* leaf = nullptr;
};

class WorkLimitReached : public std::exception {};

struct RootValues {
  int action = -1;
  int completedDepth = 0;
  std::array<double, kBoardSize> value{};   // play orientation, -inf if illegal
  std::array<bool, kBoardSize> legal{};
  std::uint64_t work = 0;
  std::uint64_t leafEvaluations = 0;
};

class ParameterizedSearch {
 public:
  explicit ParameterizedSearch(SearchParameters parameters)
      : parameters_(parameters) {}

  int chooseAction(const State& source, std::uint64_t& work) {
    const RootValues values = evaluateRoot(source);
    work += values.work;
    return values.action;
  }

  // Iterative deepening exactly as the reference does, so a work-limited
  // decision degrades to the deepest completed ply rather than a partial one.
  RootValues evaluateRoot(const State& source) {
    RootValues out;
    out.value.fill(-std::numeric_limits<double>::infinity());
    if (source.game_over) return out;
    bool mirrored = false;
    const State canonical = cfpi::detail::canonicalState(source, mirrored);
    Context context;
    std::array<double, kBoardSize> canonicalValues{};
    canonicalValues.fill(-std::numeric_limits<double>::infinity());
    int action = -1;
    for (int depth = 1; depth <= parameters_.depth; ++depth) {
      try {
        std::array<double, kBoardSize> attempt{};
        attempt.fill(-std::numeric_limits<double>::infinity());
        const int candidate = rootDecision(canonical, depth, context, attempt);
        if (candidate < 0) break;
        action = candidate;
        canonicalValues = attempt;
        out.completedDepth = depth;
      } catch (const WorkLimitReached&) {
        break;
      }
    }
    if (action < 0) action = centerFirstMove(canonical.board);
    out.action = mirrored && action >= 0 ? kBoardSize - 1 - action : action;
    for (int column = 0; column < kBoardSize; ++column) {
      const int source_column = mirrored ? kBoardSize - 1 - column : column;
      out.value[static_cast<std::size_t>(source_column)] =
          canonicalValues[static_cast<std::size_t>(column)];
      out.legal[static_cast<std::size_t>(source_column)] =
          isLegal(canonical.board, column);
    }
    out.work = context.work;
    out.leafEvaluations = context.leafEvaluations;
    return out;
  }

 private:
  struct CacheEntry {
    double value = 0.0;
    std::list<std::string>::iterator order;
  };

  struct Context {
    std::unordered_map<std::string, CacheEntry> cache;
    std::list<std::string> order;
    std::uint64_t work = 0;
    std::uint64_t leafEvaluations = 0;
  };

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

  double evaluateLeaf(const State& state, Context& context) const {
    checkBudget(context);
    ++context.work;
    const double base = frozen::fairLeaf(state);
    if (!std::isfinite(base)) {
      throw std::runtime_error("leaf returned a non-finite value");
    }
    // `w == 0` short-circuits before the model is touched, so the comparator
    // arm is the frozen reference bit for bit.
    if (parameters_.leafWeight == 0.0 || parameters_.leaf == nullptr) {
      return base;
    }
    ++context.leafEvaluations;
    const double learned = parameters_.leaf->value(state);
    if (!std::isfinite(learned)) {
      throw std::runtime_error("student leaf returned a non-finite value");
    }
    return (1.0 - parameters_.leafWeight) * base +
           parameters_.leafWeight * parameters_.leafScale * learned;
  }

  double bestFutureValue(const State& state, int depth, Context& context) const {
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

  int rootDecision(const State& canonical, int depth, Context& context,
                   std::array<double, kBoardSize>& values) const {
    int action = -1;
    double bestValue = -std::numeric_limits<double>::infinity();
    for (const int column : cfpi::detail::kColumnOrder) {
      if (!isLegal(canonical.board, column)) continue;
      const double value = evaluateAction(canonical, column, depth, context);
      values[static_cast<std::size_t>(column)] = value;
      if (value > bestValue) {
        bestValue = value;
        action = column;
      }
    }
    return action;
  }

  SearchParameters parameters_;
};

}  // namespace drop7::distill
