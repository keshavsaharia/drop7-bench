#pragma once
// Fast factored-chance fair search: the E-FAST-M6 port
// (EX-20260823-fast-m6-reveal-sampling-port-be23e203).
//
// This header generalizes the gated fast search
// (approaches/lifetime-objective/fast-engine/fast-search.hpp, finding-13,
// audit-06) from one chance knob to the factored N x M chance node of the
// native FactoredSearch (approaches/lifetime-objective/reveal-sampling/
// search.cpp, the C0 arms' engine).  It modifies no existing file: it reuses
// the fast engine's mechanics, transposition table and stratified draws
// unchanged, and adds only the factored chance-node loop and the leaf-memo
// call site.
//
// EQUIVALENCE CONTRACT -- the native traversal order, stated precisely:
//
//   * chooseAction canonicalizes the state (mirror to the lexicographically
//     smaller top-row representation), then runs iterative deepening
//     depth = 1..D over ONE shared context (one transposition table, one work
//     counter); a WorkLimitReached at depth d keeps the completed depth d-1
//     decision.
//   * Every action node (root or interior) tries the legal columns in
//     cfpi::detail::kColumnOrder {3, 2, 4, 1, 5, 0, 6}; strictly greater
//     value replaces the incumbent (first-in-order wins ties).
//   * Every chance node enumerates its T = N*M scenarios DISC-MAJOR:
//     the outer loop runs disc stratum d = 0..N-1, the inner loop reveal
//     sample r = 0..M-1, and the scenario index is s = r*N + d.  Iteration
//     (d, r) draws its cascade reveal values from
//     StratifiedRandom{stateSeed, s, T, event = 0, 1, ...} -- with
//     stateSeed = scenarioSeedForState(state, kPolicySeed 0xd7075eed, depth)
//     and the reveal events consumed in cascade order -- and, when the move
//     survives, sets the successor's next visible disc from
//     sampledNextDisc(stateSeed, d, N): the disc draw is stratified over the
//     N disc strata only and never depends on r.  Work (++work_) is counted
//     once per scenario immediately after the move resolves and before the
//     terminal checks; the budget is checked before each scenario, at every
//     bestFutureValue entry and before every leaf; the surviving successor
//     is canonicalized and recursed at depth-1; the action value is the sum
//     over all T scenario values divided by T.
//   * At M = 1 the indexing collapses to s = d and T = N, so every draw is
//     byte-identical to the single-knob fast search at chance_samples = N.
//     The M == 1 branch below is the fast-search.hpp loop VERBATIM (only the
//     leaf call site differs, per the memo contract), so the M = 1 path is
//     the untouched gated code path; gate.cpp additionally proves metric
//     bit-identity of this class at M = 1 against fast::FastSearch.
//
// Leaf memo under M > 1 (audit-06 executive item 1): the one-entry memo keys
// on the FULL board (memcmp) plus moves_remaining and recomputes only the
// next-disc term, returning a bit-identical double on every call.  Sampled
// reveals that change the board therefore MISS (never alias) and reveal
// samples that leave the board unchanged HIT, exactly as at M = 1.  The memo
// sits below the search's ++work_ line, so it cannot change logical work,
// completed depth or any chosen column; gate.cpp proves memo-on/off trace
// identity across the whole grid.  The memo stays enabled for M > 1.

#include "../fast-engine/fast-search.hpp"
#include "../fast-engine-memo/memo-leaf.hpp"

#include <cstdint>
#include <exception>
#include <limits>
#include <stdexcept>

namespace drop7::fastr {

using drop7::State;
using fast::FastMoveResult;
using fast::FastStratifiedRandom;
using fast::FastSearchMetrics;
using fast::LeafScratch;
using fast::MinimalWaveSink;
using fast::PackedKey;
using fast::TranspositionTable;
using fast::canonicalStateFast;
using fast::fastFairLeaf;
using fast::fastSampledNextDisc;
using fast::hashKey;
using fast::packKey;
using fast::playMoveFast;

struct FastFactoredParameters {
  int depth = 4;
  // N: disc strata (the fast search's single knob, same name so the M == 1
  // branch below is byte-identical to fast-search.hpp).
  int chance_samples = 5;
  // M: reveal samples per disc stratum (native --reveal-samples).
  int reveal_samples = 1;
  double terminal_utility = -1'000'000.0;
  std::uint64_t maximum_work = 3'200'000;
  std::size_t maximum_cache_entries = 60'000;
  std::uint32_t policy_seed = 0xd707'5eedu;
  // The one-entry leaf memo is value-bit-identical either way; off exists
  // only for the memo-on/off identity gate and for cost accounting.
  bool use_leaf_memo = true;
};

class FastFactoredWorkLimitReached : public std::exception {};

class FastFactoredSearch {
 public:
  explicit FastFactoredSearch(FastFactoredParameters parameters)
      : parameters_(parameters), table_(parameters.maximum_cache_entries) {
    if (parameters_.chance_samples < 1 || parameters_.reveal_samples < 1) {
      throw std::invalid_argument(
          "chance_samples and reveal_samples must be >= 1");
    }
  }

  int chooseAction(const State& source, FastSearchMetrics& metrics) {
    metrics = FastSearchMetrics{};
    if (source.game_over) return -1;
    bool mirrored = false;
    const State canonical = canonicalStateFast(source, mirrored);
    table_.clear();
    nodes_ = 0;
    work_ = 0;
    cache_hits_ = 0;
    int action = -1;
    int completed_depth = 0;
    for (int depth = 1; depth <= parameters_.depth; ++depth) {
      try {
        const int candidate = rootDecision(canonical, depth);
        if (candidate < 0) break;
        action = candidate;
        completed_depth = depth;
      } catch (const FastFactoredWorkLimitReached&) {
        break;
      }
    }
    if (action < 0) action = centerFirstMove(canonical.board);
    metrics.completed_depth = completed_depth;
    metrics.nodes = nodes_;
    metrics.work = work_;
    metrics.cache_hits = cache_hits_;
    metrics.cache_entries = table_.size();
    metrics.action = mirrored && action >= 0 ? kBoardSize - 1 - action : action;
    return metrics.action;
  }

  int chooseAction(const State& source, std::uint64_t& work) {
    FastSearchMetrics metrics;
    const int action = chooseAction(source, metrics);
    work += metrics.work;
    return action;
  }

  std::size_t tableBytes() const { return table_.slotBytes(); }

  int requestedDepth() const { return parameters_.depth; }

 private:
  void checkBudget() const {
    if (work_ >= parameters_.maximum_work) throw FastFactoredWorkLimitReached{};
  }

  double evaluateAction(const State& state, int column, int depth) {
    const std::uint32_t state_seed = cfpi::detail::scenarioSeedForState(
        state, parameters_.policy_seed, depth);
    if (parameters_.reveal_samples == 1) {
      // M == 1 short circuit: the fast-search.hpp chance loop verbatim
      // (chance_samples = N; scenario index s collapses to the disc index).
      double value = 0.0;
      for (int sample = 0; sample < parameters_.chance_samples; ++sample) {
        checkBudget();
        FastStratifiedRandom random{state_seed, sample,
                                    parameters_.chance_samples, 0};
        MinimalWaveSink sink;
        FastMoveResult move;
        const bool played = playMoveFast(state, column, random, sink, move);
        ++work_;
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
        move.state.next_disc = fastSampledNextDisc(state_seed, sample,
                                                   parameters_.chance_samples);
        bool ignored = false;
        const State next = canonicalStateFast(move.state, ignored);
        value += score_delta + bestFutureValue(next, depth - 1);
      }
      return value / parameters_.chance_samples;
    }
    // M > 1: the native FactoredSearch chance node, disc-major, s = r*N + d
    // over T = N*M, reveals stratified over T, the disc over N only.
    const int disc_samples = parameters_.chance_samples;
    const int reveal_samples = parameters_.reveal_samples;
    const int total = disc_samples * reveal_samples;
    double value = 0.0;
    for (int disc = 0; disc < disc_samples; ++disc) {
      for (int rev = 0; rev < reveal_samples; ++rev) {
        checkBudget();
        const int scenario = rev * disc_samples + disc;
        FastStratifiedRandom random{state_seed, scenario, total, 0};
        MinimalWaveSink sink;
        FastMoveResult move;
        const bool played = playMoveFast(state, column, random, sink, move);
        ++work_;
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
        move.state.next_disc =
            fastSampledNextDisc(state_seed, disc, disc_samples);
        bool ignored = false;
        const State next = canonicalStateFast(move.state, ignored);
        value += score_delta + bestFutureValue(next, depth - 1);
      }
    }
    return value / static_cast<double>(total);
  }

  double evaluateLeaf(const State& state) {
    checkBudget();
    ++work_;
    const double value = parameters_.use_leaf_memo
                             ? fastm::fastFairLeafMemo(state, scratch_, memo_)
                             : fastFairLeaf(state, scratch_);
    if (!std::isfinite(value)) {
      throw std::runtime_error("fast leaf returned a non-finite value");
    }
    return value;
  }

  double bestFutureValue(const State& state, int depth) {
    ++nodes_;
    checkBudget();
    if (state.game_over) return parameters_.terminal_utility;
    if (depth == 0) return evaluateLeaf(state);
    const PackedKey key = packKey(state, depth);
    const std::uint64_t hash = hashKey(key);
    if (const double* cached = table_.lookup(key, hash)) {
      ++cache_hits_;
      return *cached;
    }
    double best = -std::numeric_limits<double>::infinity();
    for (const int column : cfpi::detail::kColumnOrder) {
      if (state.board[static_cast<std::size_t>(column)] != kEmpty) continue;
      const double value = evaluateAction(state, column, depth);
      if (value > best) best = value;
    }
    if (!std::isfinite(best)) best = parameters_.terminal_utility;
    table_.store(key, hash, best);
    return best;
  }

  int rootDecision(const State& canonical, int depth) {
    int action = -1;
    double best_value = -std::numeric_limits<double>::infinity();
    for (const int column : cfpi::detail::kColumnOrder) {
      if (canonical.board[static_cast<std::size_t>(column)] != kEmpty) continue;
      const double value = evaluateAction(canonical, column, depth);
      if (value > best_value) {
        best_value = value;
        action = column;
      }
    }
    return action;
  }

  FastFactoredParameters parameters_;
  TranspositionTable table_;
  LeafScratch scratch_{};
  fastm::LeafMemo memo_{};
  std::uint64_t nodes_ = 0;
  std::uint64_t work_ = 0;
  std::uint64_t cache_hits_ = 0;
};

}  // namespace drop7::fastr
