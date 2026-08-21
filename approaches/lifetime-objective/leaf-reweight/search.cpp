// Exploratory bounded correction around fair D4: make the frozen leaf's
// nineteen weights runtime data instead of compile-time constants, prove that
// the frozen weight vector reproduces the reference bit-for-bit, and then test
// the reweightings that docs/exploratory/finding-10-suite-validation.md
// Addendum A says predict achievable clears far better than the frozen vector.
//
// Motivation.  Addendum A labelled 1,024 public positions with their exact
// 8-move achievable clear optimum averaged over independent completions of the
// hidden board and the future.  Predicting that label, occupancy alone reaches
// held-out R^2 0.187, the frozen scalar fairLeaf(state) reaches 0.396, the
// leaf's own nineteen features freely reweighted reach 0.734, and every linear
// model over all 53 candidate structural properties tops out at 0.753.
// Reweighting recovers 95% of all the signal any structural property can
// supply, and the cosine between the frozen weight direction and the
// predictive direction is +0.141.  The leaf is not missing information; its
// weights point almost orthogonally to it.
//
// This program changes no existing source.  The search driver is a copy of
// approaches/lifetime-objective/risk-calibration/search.cpp, which is already
// proved decision-identical to the frozen reference; the only change is that
// frozen::fairLeaf is replaced by a local dot product over
// frozen::extractFairFeatures with runtime weights.  The feature extractor,
// chance stratification, canonicalization, cache keying, column order and work
// accounting all come from the unmodified frozen code, so the ONLY degree of
// freedom exposed here is the weight vector.
//
// CHECK gate (--leaf-check, --parity, --self-parity):
//   1. with the frozen weight vector the local leaf must return values whose
//      raw uint64 bit patterns are identical to frozen::fairLeaf on a large
//      sample of real boards drawn from fair play;
//   2. at default parameters the driver must select the same column as the
//      unmodified reference on every move of every probe game; and
//   3. at any parameterization the driver with frozen weights must produce
//      identical actions, scores, moves and work counts to the same driver
//      compiled against frozen::fairLeaf directly.

#include "fair-only-depth4-noentry.cpp"

#include "../../../approaches/lifetime-objective/common/harness.hpp"

#include <cstring>
#include <exception>
#include <limits>
#include <list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace drop7::lifetime::leafw {

namespace ref = drop7::fair_only_depth4;
namespace frozen = drop7::fair_only_horizon;

// ---------------------------------------------------------------------------
// The leaf, with its weights as data.
// ---------------------------------------------------------------------------

// fair-only-horizon.cpp:53-71, in the exact order fairLeaf accumulates them.
// The order is load-bearing: floating point addition is not associative, so
// bit-identity with the frozen leaf requires the same accumulation order.
constexpr int kLeafTerms = 19;

constexpr const char* kLeafNames[kLeafTerms] = {
    "open_columns",         "height_load",
    "solid_cells",          "cracked_cells",
    "numbered_cells",       "high_low_numbers",
    "direct_potential",     "latent_chain_potential",
    "cracked_exposure",     "solid_exposure",
    "adjacent_ones",        "triple_twos",
    "dead_low_numbers",     "covered_height_risk",
    "low_number_height_risk", "danger_height_squared",
    "roughness",            "rise_pressure",
    "next_disc_vertical_options",
};

constexpr double kFrozenWeights[kLeafTerms] = {
    frozen::kOpenColumnsWeight,
    frozen::kHeightLoadWeight,
    frozen::kSolidCellsWeight,
    frozen::kCrackedCellsWeight,
    frozen::kNumberedCellsWeight,
    frozen::kHighLowNumbersWeight,
    frozen::kDirectPotentialWeight,
    frozen::kLatentChainPotentialWeight,
    frozen::kCrackedExposureWeight,
    frozen::kSolidExposureWeight,
    frozen::kAdjacentOnesWeight,
    frozen::kTripleTwosWeight,
    frozen::kDeadLowNumbersWeight,
    frozen::kCoveredHeightRiskWeight,
    frozen::kLowNumberHeightRiskWeight,
    frozen::kDangerHeightSquaredWeight,
    frozen::kRoughnessWeight,
    frozen::kRisePressureWeight,
    frozen::kNextDiscVerticalOptionsWeight,
};

// Guard against the frozen constants moving underneath this experiment.
static_assert(kFrozenWeights[0] == 180.0);
static_assert(kFrozenWeights[4] == -18.0);
static_assert(kFrozenWeights[9] == 40.0);
static_assert(kFrozenWeights[8] == 100.0);
static_assert(kFrozenWeights[13] == -95.0);
static_assert(kFrozenWeights[16] == 0.0);
static_assert(kFrozenWeights[17] == -35.0);

struct LeafWeights {
  double w[kLeafTerms] = {
      kFrozenWeights[0],  kFrozenWeights[1],  kFrozenWeights[2],
      kFrozenWeights[3],  kFrozenWeights[4],  kFrozenWeights[5],
      kFrozenWeights[6],  kFrozenWeights[7],  kFrozenWeights[8],
      kFrozenWeights[9],  kFrozenWeights[10], kFrozenWeights[11],
      kFrozenWeights[12], kFrozenWeights[13], kFrozenWeights[14],
      kFrozenWeights[15], kFrozenWeights[16], kFrozenWeights[17],
      kFrozenWeights[18]};
  // A constant offset.  It is not part of the frozen leaf and defaults to
  // exactly zero, but a refit vector is only comparable to the frozen one if
  // its level as well as its scale can be matched, and the level matters
  // because leaf points are compared against a -1,000,000 terminal utility.
  double bias = 0.0;

  bool isFrozen() const {
    if (bias != 0.0) return false;
    for (int i = 0; i < kLeafTerms; ++i) {
      if (w[i] != kFrozenWeights[i]) return false;
    }
    return true;
  }
};

// Identical in structure to frozen::fairLeaf (fair-only-horizon.cpp:135-162);
// the constants are replaced by weights.w[] in the same positions and the
// accumulation order is preserved exactly.
inline double parameterizedLeaf(const State& state, const LeafWeights& weights) {
  if (state.game_over) return frozen::kFairTerminalUtility;
  const frozen::FairFeatures features = frozen::extractFairFeatures(state);
  const auto& f = features.heuristic;
  double result = 0.0;
  result += weights.w[0] * f.open_columns;
  result += weights.w[1] * f.height_load;
  result += weights.w[2] * f.solid_cells;
  result += weights.w[3] * f.cracked_cells;
  result += weights.w[4] * f.numbered_cells;
  result += weights.w[5] * f.high_low_numbers;
  result += weights.w[6] * f.direct_potential;
  result += weights.w[7] * f.latent_chain_potential;
  result += weights.w[8] * f.cracked_exposure;
  result += weights.w[9] * f.solid_exposure;
  result += weights.w[10] * f.adjacent_ones;
  result += weights.w[11] * f.triple_twos;
  result += weights.w[12] * f.dead_low_numbers;
  result += weights.w[13] * features.covered_height_risk;
  result += weights.w[14] * features.low_number_height_risk;
  result += weights.w[15] * features.danger_height_squared;
  result += weights.w[16] * features.roughness;
  result += weights.w[17] * features.rise_pressure;
  result += weights.w[18] * features.next_disc_vertical_options;
  if (weights.bias != 0.0) result += weights.bias;
  return result;
}

inline std::uint64_t bits(double value) {
  std::uint64_t raw = 0;
  std::memcpy(&raw, &value, sizeof raw);
  return raw;
}

// ---------------------------------------------------------------------------
// The search.  Copied verbatim from
// approaches/lifetime-objective/risk-calibration/search.cpp except that
// evaluateLeaf calls parameterizedLeaf instead of frozen::fairLeaf.
// ---------------------------------------------------------------------------

struct SearchParameters {
  int depth = 4;
  int chanceSamples = frozen::kChanceSamples;
  double terminalUtility = frozen::kTerminalUtility;
  std::uint64_t maximumWork = 3'200'000;
  std::size_t maximumCacheEntries = 60'000;
  LeafWeights weights{};
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
    const double value = parameterizedLeaf(state, parameters_.weights);
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

// The same driver, hard-wired to frozen::fairLeaf.  It exists only so that the
// CHECK gate can compare "weights as data" against "weights as constants"
// inside one process at an arbitrary parameterization, including the d4/s7
// configuration the reference driver cannot be asked for.
class FrozenLeafSearch {
 public:
  explicit FrozenLeafSearch(SearchParameters parameters)
      : parameters_(parameters) {}

  int chooseAction(const State& source, std::uint64_t& work) {
    if (source.game_over) return -1;
    bool mirrored = false;
    const State canonical = cfpi::detail::canonicalState(source, mirrored);
    SearchContext context;
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

  double bestFutureValue(const State& state, int depth,
                         SearchContext& context) const {
    ++context.nodes;
    checkBudget(context);
    if (state.game_over) return parameters_.terminalUtility;
    if (depth == 0) {
      checkBudget(context);
      ++context.work;
      return frozen::fairLeaf(state);
    }
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
// CHECK gate 1 - raw bit-pattern identity of the leaf on real boards.
// ---------------------------------------------------------------------------

struct LeafCheckResult {
  std::uint64_t boards = 0;
  std::uint64_t mismatches = 0;
  std::uint64_t terminalBoards = 0;
  double minimumLeaf = 0.0;
  double maximumLeaf = 0.0;
  double meanLeaf = 0.0;
};

// Walks real fair-play trajectories and, at every visited state, compares the
// parameterized leaf against frozen::fairLeaf on that state and on every state
// reachable by one legal drop under every stratified chance sample - i.e. on
// exactly the population of boards the search's leaf actually sees.
LeafCheckResult leafBitCheck(const SearchParameters& parameters,
                             std::uint32_t seedStart, int games,
                             int maximumMoves, std::ostream& out) {
  LeafCheckResult result;
  const LeafWeights frozenWeights{};
  if (!frozenWeights.isFrozen()) throw std::runtime_error("default weights moved");
  double total = 0.0;
  bool first = true;
  ParameterizedSearch search{parameters};
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed = seedStart + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    int moves = 0;
    while (!state.game_over && moves < maximumMoves) {
      auto compare = [&](const State& probe) {
        const double mine = parameterizedLeaf(probe, frozenWeights);
        const double theirs = frozen::fairLeaf(probe);
        ++result.boards;
        if (probe.game_over) ++result.terminalBoards;
        if (bits(mine) != bits(theirs)) {
          if (result.mismatches < 5) {
            out << "  leaf mismatch seed 0x" << std::hex << seed << " mine "
                << bits(mine) << " frozen " << bits(theirs) << std::dec << '\n';
          }
          ++result.mismatches;
        }
        total += mine;
        if (first || mine < result.minimumLeaf) result.minimumLeaf = mine;
        if (first || mine > result.maximumLeaf) result.maximumLeaf = mine;
        first = false;
      };
      compare(state);
      for (int column = 0; column < kBoardSize; ++column) {
        if (!isLegal(state.board, column)) continue;
        const std::uint32_t stateSeed = cfpi::detail::scenarioSeedForState(
            state, frozen::kPolicySeed, parameters.depth);
        for (int sample = 0; sample < parameters.chanceSamples; ++sample) {
          cfpi::detail::StratifiedRandom random{
              stateSeed, sample, parameters.chanceSamples, 0};
          MoveResult move;
          if (!cfpi::detail::playMoveSampled(state, column, random, move)) continue;
          move.state.score = 0;
          move.state.next_disc = cfpi::detail::sampledNextDisc(
              stateSeed, sample, parameters.chanceSamples);
          bool ignored = false;
          const State next = cfpi::detail::canonicalState(move.state, ignored);
          if (next.game_over) continue;
          compare(next);
          // one more ply, so the sample includes deeper, more crowded boards
          for (int column2 = 0; column2 < kBoardSize; ++column2) {
            if (!isLegal(next.board, column2)) continue;
            cfpi::detail::StratifiedRandom random2{
                stateSeed, sample, parameters.chanceSamples, 1};
            MoveResult move2;
            if (!cfpi::detail::playMoveSampled(next, column2, random2, move2)) continue;
            move2.state.score = 0;
            move2.state.next_disc = cfpi::detail::sampledNextDisc(
                stateSeed, sample, parameters.chanceSamples);
            bool ignored2 = false;
            const State next2 = cfpi::detail::canonicalState(move2.state, ignored2);
            if (next2.game_over) continue;
            compare(next2);
          }
        }
      }
      std::uint64_t work = 0;
      int column = search.chooseAction(state, work);
      if (column < 0 || !isLegal(state.board, column)) {
        column = centerFirstMove(state.board);
        if (column < 0) break;
      }
      MoveResult move;
      if (!playHeadlessMove(state, seed, column, move)) break;
      ++moves;
    }
  }
  result.meanLeaf = result.boards > 0
                        ? total / static_cast<double>(result.boards)
                        : 0.0;
  return result;
}

// ---------------------------------------------------------------------------
// CHECK gate 2 - the frozen reference driver, at its own configuration.
// ---------------------------------------------------------------------------

bool referenceParityCheck(std::uint32_t seedStart, int games, int maximumMoves,
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
        out << "  mismatch seed 0x" << std::hex << seed << std::dec << " move "
            << state.moves_played << ": reference " << reference.action
            << " parameterized " << candidate << '\n';
      }
      MoveResult move;
      if (!playHeadlessMove(state, seed, reference.action, move)) break;
    }
  }
  out << "reference-parity: " << comparedMoves << " moves compared, "
      << mismatches << " mismatches\n";
  return mismatches == 0;
}

// ---------------------------------------------------------------------------
// CHECK gate 3 - weights-as-data against weights-as-constants at the actual
// evaluation configuration, comparing columns AND cumulative work.
// ---------------------------------------------------------------------------

bool selfParityCheck(const SearchParameters& parameters,
                     std::uint32_t seedStart, int games, int maximumMoves,
                     std::ostream& out) {
  ParameterizedSearch mine{parameters};
  FrozenLeafSearch theirs{parameters};
  std::uint64_t mismatches = 0;
  std::uint64_t workMismatches = 0;
  std::uint64_t comparedMoves = 0;
  std::uint64_t totalWork = 0;
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed = seedStart + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    int moves = 0;
    std::uint64_t mineWork = 0, theirWork = 0;
    while (!state.game_over && moves < maximumMoves) {
      const std::uint64_t beforeMine = mineWork, beforeTheirs = theirWork;
      const int a = mine.chooseAction(state, mineWork);
      const int b = theirs.chooseAction(state, theirWork);
      ++comparedMoves;
      if (a != b) {
        ++mismatches;
        if (mismatches < 5) {
          out << "  self mismatch seed 0x" << std::hex << seed << std::dec
              << " move " << moves << ": data " << a << " constants " << b
              << '\n';
        }
      }
      if (mineWork - beforeMine != theirWork - beforeTheirs) {
        ++workMismatches;
        if (workMismatches < 5) {
          out << "  work mismatch seed 0x" << std::hex << seed << std::dec
              << " move " << moves << ": data " << (mineWork - beforeMine)
              << " constants " << (theirWork - beforeTheirs) << '\n';
        }
      }
      int column = a;
      if (column < 0 || !isLegal(state.board, column)) {
        column = centerFirstMove(state.board);
        if (column < 0) break;
      }
      MoveResult move;
      if (!playHeadlessMove(state, seed, column, move)) break;
      ++moves;
    }
    totalWork += mineWork;
  }
  out << "self-parity: " << comparedMoves << " moves compared, " << mismatches
      << " column mismatches, " << workMismatches << " work mismatches, "
      << totalWork << " total work\n";
  return mismatches == 0 && workMismatches == 0;
}

// ---------------------------------------------------------------------------
// Options.
// ---------------------------------------------------------------------------

int leafIndexOf(const std::string& name) {
  for (int i = 0; i < kLeafTerms; ++i) {
    if (name == kLeafNames[i]) return i;
  }
  return -1;
}

struct Options {
  CohortOptions cohort;
  SearchParameters parameters;
  std::string output;
  std::string arm = "frozen";
  bool referenceParity = false;
  bool selfParity = false;
  bool leafCheck = false;
  int checkGames = 4;
  int checkMoves = 40;
};

void applyWeightsFile(SearchParameters& parameters, const std::string& path) {
  std::ifstream file(path);
  if (!file) throw std::runtime_error("cannot open weights file " + path);
  std::string name;
  double value = 0.0;
  int applied = 0;
  while (file >> name >> value) {
    if (name.size() && name[0] == '#') {
      std::string rest;
      std::getline(file, rest);
      continue;
    }
    if (name == "bias") {
      parameters.weights.bias = value;
      ++applied;
      continue;
    }
    const int index = leafIndexOf(name);
    if (index < 0) throw std::runtime_error("unknown leaf term " + name);
    parameters.weights.w[index] = value;
    ++applied;
  }
  if (applied == 0) throw std::runtime_error("weights file applied nothing: " + path);
}

Options parseOptions(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc;) {
    const std::string key = argv[index];
    if (key == "--reference-parity") {
      options.referenceParity = true;
      index += 1;
      continue;
    }
    if (key == "--self-parity") {
      options.selfParity = true;
      index += 1;
      continue;
    }
    if (key == "--leaf-check") {
      options.leafCheck = true;
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
    } else if (key == "--chance-samples") {
      options.parameters.chanceSamples = std::stoi(value);
    } else if (key == "--terminal-utility") {
      options.parameters.terminalUtility = std::stod(value);
    } else if (key == "--max-work") {
      options.parameters.maximumWork = std::stoull(value);
    } else if (key == "--weights") {
      applyWeightsFile(options.parameters, value);
    } else if (key == "--weight") {
      const auto split = value.find('=');
      if (split == std::string::npos) {
        throw std::invalid_argument("--weight expects name=value");
      }
      const std::string name = value.substr(0, split);
      const double amount = std::stod(value.substr(split + 1));
      if (name == "bias") {
        options.parameters.weights.bias = amount;
      } else {
        const int i = leafIndexOf(name);
        if (i < 0) throw std::invalid_argument("unknown leaf term " + name);
        options.parameters.weights.w[i] = amount;
      }
    } else if (key == "--arm") {
      options.arm = value;
    } else if (key == "--check-games") {
      options.checkGames = std::stoi(value);
    } else if (key == "--check-moves") {
      options.checkMoves = std::stoi(value);
    } else if (key == "--output") {
      options.output = value;
    } else {
      throw std::invalid_argument("unknown option " + key);
    }
    index += 2;
  }
  return options;
}

std::string configJson(const Options& options) {
  std::ostringstream config;
  config << std::setprecision(12) << "{\"arm\": \"" << options.arm
         << "\", \"depth\": " << options.parameters.depth
         << ", \"chanceSamples\": " << options.parameters.chanceSamples
         << ", \"terminalUtility\": " << options.parameters.terminalUtility
         << ", \"maximumWork\": " << options.parameters.maximumWork
         << ", \"frozenWeights\": "
         << (options.parameters.weights.isFrozen() ? "true" : "false")
         << ", \"weights\": {";
  for (int i = 0; i < kLeafTerms; ++i) {
    if (i) config << ", ";
    config << '"' << kLeafNames[i] << "\": " << options.parameters.weights.w[i];
  }
  config << ", \"bias\": " << options.parameters.weights.bias << "}}";
  return config.str();
}

}  // namespace drop7::lifetime::leafw

int main(int argc, char** argv) {
  using namespace drop7;
  using namespace drop7::lifetime;
  try {
    const auto options = leafw::parseOptions(argc, argv);

    if (options.leafCheck) {
      const auto result = leafw::leafBitCheck(
          options.parameters, options.cohort.seedStart, options.checkGames,
          options.checkMoves, std::cout);
      std::cout << "leaf-check: " << result.boards << " boards compared, "
                << result.mismatches << " bit mismatches, leaf range ["
                << std::setprecision(12) << result.minimumLeaf << ", "
                << result.maximumLeaf << "], mean " << result.meanLeaf << '\n';
      std::cout << (result.mismatches == 0 ? "LEAF BITS IDENTICAL\n"
                                           : "LEAF BITS DIFFER\n");
      return result.mismatches == 0 ? 0 : 1;
    }
    if (options.referenceParity) {
      const bool ok = leafw::referenceParityCheck(
          options.cohort.seedStart, options.checkGames, options.checkMoves,
          std::cout);
      std::cout << (ok ? "REFERENCE PARITY OK\n" : "REFERENCE PARITY FAILED\n");
      return ok ? 0 : 1;
    }
    if (options.selfParity) {
      const bool ok = leafw::selfParityCheck(
          options.parameters, options.cohort.seedStart, options.checkGames,
          options.checkMoves, std::cout);
      std::cout << (ok ? "SELF PARITY OK\n" : "SELF PARITY FAILED\n");
      return ok ? 0 : 1;
    }

    const auto started = std::chrono::steady_clock::now();
    auto records = runCohort(options.cohort, [&]() {
      return [search = leafw::ParameterizedSearch{options.parameters}](
                 const State& state, std::uint64_t& work) mutable {
        return search.chooseAction(state, work);
      };
    });
    const double wall = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started)
                            .count();
    const std::string config = leafw::configJson(options);
    if (options.output.empty()) {
      writeArtifact(std::cout, "leaf-reweight-fair-search", config,
                    options.cohort, records, wall);
    } else {
      std::ofstream file(options.output);
      if (!file) throw std::runtime_error("cannot open " + options.output);
      writeArtifact(file, "leaf-reweight-fair-search", config, options.cohort,
                    records, wall);
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "leaf-reweight failed: " << error.what() << '\n';
    return 1;
  }
}
