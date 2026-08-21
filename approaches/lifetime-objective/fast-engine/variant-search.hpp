#pragma once
// One search driver, three independently switchable storage back ends, so that
// each optimisation can be attributed a measured speedup rather than an
// asserted one.
//
//   kFastTable   O1  packed-key open-addressed LRU transposition table
//                    instead of unordered_map<string> + list<string>
//   kFastEngine  O3/O4/O5  run-length popper detection, in-place gravity,
//                    allocation-free wave sink
//   kFastLeaf    O7 (+O2)  dead-computation-free fairLeaf with table lookups
//                    in place of ldexp
//
// ConfigurableSearch<false,false,false> is the unoptimised reference path and
// is verified in profile.cpp to agree with the literal transcription in
// slow-search.hpp; ConfigurableSearch<true,true,true> is verified to agree with
// FastSearch.  Every combination shares this one driver, so an A/B timing
// difference is attributable to the switched storage and nothing else.

#include "slow-search.hpp"
#include "fast-search.hpp"

#include <limits>
#include <list>
#include <string>
#include <unordered_map>

namespace drop7::fast {

template <bool kFastTable, bool kFastEngine, bool kFastLeaf,
          bool kCensus = false>
class ConfigurableSearch {
 public:
  struct Census {
    std::uint64_t leaf_calls = 0;
    std::uint64_t move_calls = 0;
    std::uint64_t action_nodes = 0;
    std::uint64_t key_builds = 0;
    std::uint64_t key_inserts = 0;
    std::uint64_t canonical_calls = 0;
    std::uint64_t leaf_repeats = 0;   // leaf states seen more than once
  };
  Census census;
  // Diagnostic only (kCensus): how often the same leaf state is evaluated more
  // than once inside one decision.  Measures the headroom a leaf memo would
  // have; see finding-13 for why that memo is reported and not adopted.
  std::unordered_map<std::uint64_t, std::uint32_t> leaf_seen;

  explicit ConfigurableSearch(FastSearchParameters parameters)
      : parameters_(parameters), table_(parameters.maximum_cache_entries) {}

  int chooseAction(const State& source, FastSearchMetrics& metrics) {
    metrics = FastSearchMetrics{};
    if (source.game_over) return -1;
    bool mirrored = false;
    const State canonical = kFastEngine
                                ? canonicalStateFast(source, mirrored)
                                : cfpi::detail::canonicalState(source, mirrored);
    if constexpr (kFastTable) {
      table_.clear();
    } else {
      slow_cache_.clear();
      slow_order_.clear();
    }
    nodes_ = 0;
    work_ = 0;
    cache_hits_ = 0;
    if constexpr (kCensus) {
      census = Census{};
      leaf_seen.clear();
    }
    int action = -1;
    int completed_depth = 0;
    for (int depth = 1; depth <= parameters_.depth; ++depth) {
      try {
        const int candidate = rootDecision(canonical, depth);
        if (candidate < 0) break;
        action = candidate;
        completed_depth = depth;
      } catch (const FastWorkLimitReached&) {
        break;
      }
    }
    if (action < 0) action = centerFirstMove(canonical.board);
    metrics.completed_depth = completed_depth;
    metrics.nodes = nodes_;
    metrics.work = work_;
    metrics.cache_hits = cache_hits_;
    metrics.cache_entries =
        kFastTable ? table_.size() : slow_cache_.size();
    metrics.action = mirrored && action >= 0 ? kBoardSize - 1 - action : action;
    return metrics.action;
  }

  int chooseAction(const State& source, std::uint64_t& work) {
    FastSearchMetrics metrics;
    const int action = chooseAction(source, metrics);
    work += metrics.work;
    return action;
  }

  std::size_t tableBytes() const {
    return kFastTable ? table_.slotBytes() : 0;
  }

 private:
  struct SlowEntry {
    double value = 0.0;
    std::list<std::string>::iterator order;
  };

  void checkBudget() const {
    if (work_ >= parameters_.maximum_work) throw FastWorkLimitReached{};
  }

  double evaluateAction(const State& state, int column, int depth) {
    const std::uint32_t state_seed = cfpi::detail::scenarioSeedForState(
        state, parameters_.policy_seed, depth);
    double value = 0.0;
    if constexpr (kCensus) ++census.action_nodes;
    for (int sample = 0; sample < parameters_.chance_samples; ++sample) {
      checkBudget();
      if constexpr (kCensus) ++census.move_calls;
      State next_state;
      std::int64_t score_delta_raw = 0;
      bool played = false;
      bool over = false;
      if constexpr (kFastEngine) {
        FastStratifiedRandom random{state_seed, sample,
                                    parameters_.chance_samples, 0};
        MinimalWaveSink sink;
        FastMoveResult move;
        played = playMoveFast(state, column, random, sink, move);
        ++work_;
        if (played) {
          next_state = move.state;
          score_delta_raw = move.score_delta;
          over = move.state.game_over;
        }
      } else {
        cfpi::detail::StratifiedRandom random{state_seed, sample,
                                              parameters_.chance_samples, 0};
        MoveResult move;
        played = cfpi::detail::playMoveSampled(state, column, random, move);
        ++work_;
        if (played) {
          next_state = move.state;
          score_delta_raw = move.score_delta;
          over = move.state.game_over;
        }
      }
      if (!played) {
        value += parameters_.terminal_utility;
        continue;
      }
      const double score_delta = static_cast<double>(score_delta_raw);
      if (over) {
        value += score_delta + parameters_.terminal_utility;
        continue;
      }
      next_state.score = 0;
      next_state.next_disc =
          kFastEngine ? fastSampledNextDisc(state_seed, sample,
                                            parameters_.chance_samples)
                      : cfpi::detail::sampledNextDisc(
                            state_seed, sample, parameters_.chance_samples);
      if constexpr (kCensus) ++census.canonical_calls;
      bool ignored = false;
      const State next = kFastEngine
                             ? canonicalStateFast(next_state, ignored)
                             : cfpi::detail::canonicalState(next_state, ignored);
      value += score_delta + bestFutureValue(next, depth - 1);
    }
    return value / parameters_.chance_samples;
  }

  double evaluateLeaf(const State& state) {
    checkBudget();
    ++work_;
    if constexpr (kCensus) {
      ++census.leaf_calls;
      const PackedKey key = packKey(state, 0);
      if (++leaf_seen[hashKey(key)] > 1) ++census.leaf_repeats;
    }
    const double value = kFastLeaf ? fastFairLeaf(state, scratch_)
                                   : frozen::fairLeaf(state);
    if (!std::isfinite(value)) {
      throw std::runtime_error("leaf returned a non-finite value");
    }
    return value;
  }

  double bestFutureValue(const State& state, int depth) {
    ++nodes_;
    checkBudget();
    if (state.game_over) return parameters_.terminal_utility;
    if (depth == 0) return evaluateLeaf(state);

    if constexpr (kFastTable) {
      const PackedKey key = packKey(state, depth);
      const std::uint64_t hash = hashKey(key);
      if (const double* cached = table_.lookup(key, hash)) {
        ++cache_hits_;
        return *cached;
      }
      const double best = expand(state, depth);
      table_.store(key, hash, best);
      return best;
    } else {
      if constexpr (kCensus) ++census.key_builds;
      const std::string key = cfpi::detail::dynamicStateKey(state, depth);
      const auto cached = slow_cache_.find(key);
      if (cached != slow_cache_.end()) {
        ++cache_hits_;
        const double value = cached->second.value;
        slow_order_.splice(slow_order_.end(), slow_order_,
                           cached->second.order);
        return value;
      }
      const double best = expand(state, depth);
      if constexpr (kCensus) ++census.key_inserts;
      storeSlow(key, best);
      return best;
    }
  }

  double expand(const State& state, int depth) {
    double best = -std::numeric_limits<double>::infinity();
    for (const int column : cfpi::detail::kColumnOrder) {
      if (!isLegal(state.board, column)) continue;
      const double value = evaluateAction(state, column, depth);
      if (value > best) best = value;
    }
    if (!std::isfinite(best)) best = parameters_.terminal_utility;
    return best;
  }

  void storeSlow(std::string key, double value) {
    const auto prior = slow_cache_.find(key);
    if (prior != slow_cache_.end()) {
      slow_order_.erase(prior->second.order);
      slow_cache_.erase(prior);
    }
    while (slow_cache_.size() >= parameters_.maximum_cache_entries) {
      const std::string& oldest = slow_order_.front();
      slow_cache_.erase(oldest);
      slow_order_.pop_front();
    }
    slow_order_.push_back(key);
    const auto order = std::prev(slow_order_.end());
    slow_cache_.emplace(std::move(key), SlowEntry{value, order});
  }

  int rootDecision(const State& canonical, int depth) {
    int action = -1;
    double best_value = -std::numeric_limits<double>::infinity();
    for (const int column : cfpi::detail::kColumnOrder) {
      if (!isLegal(canonical.board, column)) continue;
      const double value = evaluateAction(canonical, column, depth);
      if (value > best_value) {
        best_value = value;
        action = column;
      }
    }
    return action;
  }

  FastSearchParameters parameters_;
  TranspositionTable table_;
  std::unordered_map<std::string, SlowEntry> slow_cache_;
  std::list<std::string> slow_order_;
  LeafScratch scratch_{};
  std::uint64_t nodes_ = 0;
  std::uint64_t work_ = 0;
  std::uint64_t cache_hits_ = 0;
};

using BaselineSearch = ConfigurableSearch<false, false, false>;
using CensusSearch = ConfigurableSearch<false, false, false, true>;
using TableOnlySearch = ConfigurableSearch<true, false, false>;
using EngineOnlySearch = ConfigurableSearch<false, true, false>;
using LeafOnlySearch = ConfigurableSearch<false, false, true>;
using TableEngineSearch = ConfigurableSearch<true, true, false>;
using AllFastSearch = ConfigurableSearch<true, true, true>;

}  // namespace drop7::fast
