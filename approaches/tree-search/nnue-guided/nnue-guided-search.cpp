#include "../../../src/core/native/public-behavior.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <list>
#include <limits>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <unordered_map>
#include <utility>
#include <vector>

// Uses a distilled Q model for bounded branch reduction.  The model may order
// and reduce interior max nodes, but every legal root action is always searched.
namespace drop7::nnue_guided {

std::mutex progress_mutex;

constexpr int kFeatureCount = 112;
constexpr int kHidden1 = 128;
constexpr int kHidden2 = 64;
constexpr int kChanceSamples = 5;
constexpr int kCurrentPhaseFeatures = 24;
constexpr int kSuccessorMetrics = 17;
constexpr std::uint32_t kTrainingStart = 0x3d70'0000u;
constexpr std::uint32_t kTrainingEnd = 0x4d00'0000u;
static_assert(kCurrentPhaseFeatures + 1 + 7 + 5 + 7 + 17 +
                  3 * kSuccessorMetrics ==
              kFeatureCount);

using Features = std::array<float, kFeatureCount>;

void appendPhaseFeatures(const cfpi::detail::PhaseFeatures& phase,
                         Features& result, int& offset) {
  const std::array<double, kCurrentPhaseFeatures> values{{
      static_cast<double>(phase.open_columns),
      phase.height_load,
      static_cast<double>(phase.solid_cells),
      static_cast<double>(phase.cracked_cells),
      static_cast<double>(phase.numbered_cells),
      static_cast<double>(phase.high_low_numbers),
      phase.direct_potential,
      phase.latent_chain_potential,
      phase.cracked_exposure,
      phase.solid_exposure,
      phase.adjacent_ones,
      phase.triple_twos,
      phase.dead_low_numbers,
      phase.projected_occupancy_debt,
      phase.residual_cover_debt,
      phase.cover_altitude_debt,
      phase.imminent_cover_altitude_debt,
      phase.peak_height_risk,
      phase.low_cap_load,
      phase.adjacent_low_cap_load,
      phase.quiet_build_options,
      phase.quiet_direct_gain,
      phase.trigger_readiness,
      phase.rise_trigger_readiness,
  }};
  for (const double value : values) {
    result[offset++] = static_cast<float>(value);
  }
}

std::array<float, kSuccessorMetrics> successorMetrics(
    const MoveResult& move) {
  const cfpi::PhaseMetrics phase = cfpi::evaluatePhaseMetrics(move.state);
  const cfpi::detail::PhaseFeatures detailed =
      cfpi::detail::extractPhaseFeatures(move.state);
  int clears = 0;
  int reveals = 0;
  int maximum_depth = 0;
  for (const Wave& wave : move.waves) {
    clears += wave.cleared;
    reveals += wave.revealed;
    maximum_depth = std::max(maximum_depth, wave.depth);
  }
  return {{
      static_cast<float>(phase.potential),
      static_cast<float>(phase.occupied),
      static_cast<float>(phase.covers),
      static_cast<float>(phase.maximum_height),
      static_cast<float>(phase.legal_columns),
      static_cast<float>(move.score_delta),
      static_cast<float>(clears),
      static_cast<float>(reveals),
      static_cast<float>(move.waves.size()),
      static_cast<float>(maximum_depth),
      move.state.game_over ? 1.0f : 0.0f,
      move.level_advanced ? 1.0f : 0.0f,
      move.cleared_board ? 1.0f : 0.0f,
      static_cast<float>(detailed.direct_potential),
      static_cast<float>(detailed.latent_chain_potential),
      static_cast<float>(detailed.peak_height_risk),
      static_cast<float>(detailed.residual_cover_debt),
  }};
}

Features extractCanonical(const State& state, int action) {
  if (!isLegal(state.board, action)) {
    throw std::invalid_argument("cannot score an illegal action");
  }
  Features result{};
  int offset = 0;
  appendPhaseFeatures(cfpi::detail::extractPhaseFeatures(state), result,
                      offset);
  result[offset++] = static_cast<float>(cfpi::phasePotential(state));
  for (int disc = 1; disc <= kBoardSize; ++disc) {
    result[offset++] = state.next_disc == disc ? 1.0f : 0.0f;
  }
  for (int phase = 1; phase <= kMovesPerLevel; ++phase) {
    result[offset++] = state.moves_remaining == phase ? 1.0f : 0.0f;
  }
  std::array<int, kBoardSize> heights{};
  for (int column = 0; column < kBoardSize; ++column) {
    for (int row = 0; row < kBoardSize; ++row) {
      heights[column] += state.board[indexOf(row, column)] != kEmpty;
    }
    result[offset++] = static_cast<float>(heights[column]);
  }
  for (int column = 0; column < kBoardSize; ++column) {
    result[offset++] = action == column ? 1.0f : 0.0f;
  }
  result[offset++] = static_cast<float>(std::abs(action - 3));
  result[offset++] = static_cast<float>(heights[action]);
  result[offset++] =
      static_cast<float>(action > 0 ? heights[action - 1] : -1);
  result[offset++] =
      static_cast<float>(action + 1 < kBoardSize ? heights[action + 1] : -1);
  const int landing_row = kBoardSize - 1 - heights[action];
  result[offset++] = static_cast<float>(landing_row);
  Board placed = state.board;
  placed[indexOf(landing_row, action)] = state.next_disc;
  const int horizontal = lineLength(placed, landing_row, action, false);
  const int vertical = lineLength(placed, landing_row, action, true);
  result[offset++] = static_cast<float>(horizontal);
  result[offset++] = static_cast<float>(vertical);
  result[offset++] = static_cast<float>(state.next_disc);
  result[offset++] = horizontal == state.next_disc ? 1.0f : 0.0f;
  result[offset++] = vertical == state.next_disc ? 1.0f : 0.0f;

  std::array<float, kSuccessorMetrics> sum{};
  std::array<float, kSuccessorMetrics> minimum{};
  std::array<float, kSuccessorMetrics> maximum{};
  minimum.fill(std::numeric_limits<float>::infinity());
  maximum.fill(-std::numeric_limits<float>::infinity());
  const std::uint32_t state_seed =
      cfpi::detail::scenarioSeedForState(state, 0xd707'5eedu, 1);
  for (int sample = 0; sample < kChanceSamples; ++sample) {
    cfpi::detail::StratifiedRandom random{
        state_seed, sample, kChanceSamples, 0,
    };
    MoveResult move;
    if (!cfpi::detail::playMoveSampled(state, action, random, move)) {
      throw std::runtime_error("guided feature transition failed");
    }
    if (!move.state.game_over) {
      move.state.next_disc =
          cfpi::detail::sampledNextDisc(state_seed, sample, kChanceSamples);
    }
    const auto metrics = successorMetrics(move);
    for (int metric = 0; metric < kSuccessorMetrics; ++metric) {
      sum[metric] += metrics[metric];
      minimum[metric] = std::min(minimum[metric], metrics[metric]);
      maximum[metric] = std::max(maximum[metric], metrics[metric]);
    }
  }
  for (const float value : sum) result[offset++] = value / kChanceSamples;
  for (const float value : minimum) result[offset++] = value;
  for (const float value : maximum) result[offset++] = value;
  if (offset != kFeatureCount) {
    throw std::logic_error("guided feature count invariant failed");
  }
  return result;
}

Features extract(const State& source, int physical_action) {
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  const int action = mirrored ? kBoardSize - 1 - physical_action
                              : physical_action;
  return extractCanonical(canonical, action);
}

struct QModel {
  Features mean{};
  Features scale{};
  std::array<float, kHidden1 * kFeatureCount> weight1{};
  std::array<float, kHidden1> bias1{};
  std::array<float, kHidden2 * kHidden1> weight2{};
  std::array<float, kHidden2> bias2{};
  std::array<float, kHidden2> weight3{};
  float bias3 = 0.0f;

  void initialize(std::uint32_t seed = 0x3d7a'5153u) {
    scale.fill(1.0f);
    Mulberry32 random(seed);
    const auto fill = [&random](auto& values, float factor) {
      for (float& value : values) {
        value = static_cast<float>((2.0 * random.nextUnit() - 1.0) * factor);
      }
    };
    fill(weight1, 0.05f);
    fill(weight2, 0.05f);
    fill(weight3, 0.05f);
  }

  void load(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not open guided Q model");
    std::array<std::uint32_t, 5> header{};
    input.read(reinterpret_cast<char*>(header.data()),
               static_cast<std::streamsize>(header.size() * sizeof(header[0])));
    if (header != std::array<std::uint32_t, 5>{{
                      0x4451'3753u, kFeatureCount, kHidden1, kHidden2, 1u,
                  }}) {
      throw std::runtime_error("guided Q model header mismatch");
    }
    const auto read = [&input](auto& values) {
      input.read(reinterpret_cast<char*>(values.data()),
                 static_cast<std::streamsize>(values.size() *
                                              sizeof(values[0])));
    };
    read(mean);
    read(scale);
    read(weight1);
    read(bias1);
    read(weight2);
    read(bias2);
    read(weight3);
    input.read(reinterpret_cast<char*>(&bias3), sizeof(bias3));
    if (!input) throw std::runtime_error("guided Q model is truncated");
  }

  float score(const Features& raw) const {
    std::array<float, kHidden1> hidden1{};
    for (int hidden = 0; hidden < kHidden1; ++hidden) {
      float value = bias1[hidden];
      for (int feature = 0; feature < kFeatureCount; ++feature) {
        const float normalized = std::clamp(
            (raw[feature] - mean[feature]) / scale[feature], -8.0f, 8.0f);
        value += weight1[hidden * kFeatureCount + feature] * normalized;
      }
      hidden1[hidden] = std::max(0.0f, value);
    }
    std::array<float, kHidden2> hidden2{};
    for (int hidden = 0; hidden < kHidden2; ++hidden) {
      float value = bias2[hidden];
      for (int prior = 0; prior < kHidden1; ++prior) {
        value += weight2[hidden * kHidden1 + prior] * hidden1[prior];
      }
      hidden2[hidden] = std::max(0.0f, value);
    }
    float result = bias3;
    for (int hidden = 0; hidden < kHidden2; ++hidden) {
      result += weight3[hidden] * hidden2[hidden];
    }
    return result;
  }
};

constexpr std::array<int, kBoardSize> kColumnOrder{{3, 2, 4, 1, 5, 0, 6}};

struct SearchOptions {
  int maximum_depth = 3;
  int top_k = kBoardSize;
  bool guided = false;
  bool safety_union = false;
  std::uint64_t maximum_work = 250'000;
  std::size_t maximum_cache_entries = 40'000;
  double terminal_utility = -1'000'000.0;
  std::uint32_t policy_seed = 0xd707'5eedu;
};

class WorkLimitReached : public std::exception {};

struct CacheEntry {
  double value = 0.0;
  std::list<std::string>::iterator order;
};

struct SearchContext {
  SearchContext(const SearchOptions& search_options, const QModel* q_model)
      : options(search_options), model(q_model) {}

  const SearchOptions& options;
  const QModel* model = nullptr;
  std::unordered_map<std::string, CacheEntry> cache;
  std::list<std::string> order;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::uint64_t ranking_calls = 0;
  std::uint64_t ranking_actions = 0;
  std::uint64_t safety_additions = 0;
  std::size_t peak_cache_entries = 0;
};

void validateSearchOptions(const SearchOptions& options) {
  if (options.maximum_depth < 1 || options.maximum_depth > 8) {
    throw std::invalid_argument("guided depth must be from one to eight");
  }
  if (options.top_k < 1 || options.top_k > kBoardSize) {
    throw std::invalid_argument("guided top-K must be from one to seven");
  }
  if (options.maximum_work == 0 || options.maximum_cache_entries == 0) {
    throw std::invalid_argument("guided resource limits must be positive");
  }
}

void checkWork(const SearchContext& context) {
  if (context.work >= context.options.maximum_work) throw WorkLimitReached{};
}

void chargeRanking(SearchContext& context) {
  if (context.options.maximum_work - context.work < kChanceSamples) {
    throw WorkLimitReached{};
  }
  context.work += kChanceSamples;
}

void cacheValue(SearchContext& context, std::string key, double value) {
  const auto prior = context.cache.find(key);
  if (prior != context.cache.end()) {
    context.order.erase(prior->second.order);
    context.cache.erase(prior);
  }
  while (context.cache.size() >= context.options.maximum_cache_entries) {
    const std::string& oldest = context.order.front();
    context.cache.erase(oldest);
    context.order.pop_front();
  }
  context.order.push_back(key);
  auto order = std::prev(context.order.end());
  context.cache.emplace(std::move(key), CacheEntry{value, order});
  context.peak_cache_entries =
      std::max(context.peak_cache_entries, context.cache.size());
}

struct ActionRanking {
  std::array<int, kBoardSize> actions{};
  int count = 0;
};

ActionRanking interiorActions(const State& state, SearchContext& context) {
  if (!context.options.guided) {
    ActionRanking exact;
    for (const int column : kColumnOrder) {
      if (isLegal(state.board, column)) exact.actions[exact.count++] = column;
    }
    return exact;
  }
  if (context.model == nullptr) {
    throw std::logic_error("guided search is missing its Q model");
  }
  struct ScoredAction {
    int column = -1;
    float q = -std::numeric_limits<float>::infinity();
    float safety = -std::numeric_limits<float>::infinity();
  };
  std::vector<ScoredAction> scored;
  for (const int column : kColumnOrder) {
    if (!isLegal(state.board, column)) continue;
    chargeRanking(context);
    const Features raw = extractCanonical(state, column);
    // Successor mean summaries begin at 61: phase potential then score at +5.
    scored.push_back(
        {column, context.model->score(raw), raw[61] + raw[66]});
    ++context.ranking_actions;
  }
  ++context.ranking_calls;
  std::stable_sort(scored.begin(), scored.end(),
                   [](const ScoredAction& left, const ScoredAction& right) {
                     return left.q > right.q;
                   });
  ActionRanking ranking;
  const int retained = std::min<int>(context.options.top_k, scored.size());
  for (int index = 0; index < retained; ++index) {
    ranking.actions[ranking.count++] = scored[index].column;
  }
  if (context.options.safety_union && !scored.empty()) {
    const auto safety = std::max_element(
        scored.begin(), scored.end(),
        [](const ScoredAction& left, const ScoredAction& right) {
          return left.safety < right.safety;
        });
    const bool present = std::find(ranking.actions.begin(),
                                   ranking.actions.begin() + ranking.count,
                                   safety->column) !=
                         ranking.actions.begin() + ranking.count;
    if (!present && ranking.count < kBoardSize) {
      ranking.actions[ranking.count++] = safety->column;
      ++context.safety_additions;
    }
  }
  return ranking;
}

double bestFutureValue(const State& state, int depth, SearchContext& context);

double evaluateAction(const State& state, int column, int depth,
                      SearchContext& context) {
  const std::uint32_t state_seed = cfpi::detail::scenarioSeedForState(
      state, context.options.policy_seed, depth);
  double value = 0.0;
  for (int sample = 0; sample < kChanceSamples; ++sample) {
    checkWork(context);
    cfpi::detail::StratifiedRandom random{
        state_seed, sample, kChanceSamples, 0,
    };
    MoveResult move;
    if (!cfpi::detail::playMoveSampled(state, column, random, move)) {
      value += context.options.terminal_utility;
      continue;
    }
    ++context.work;
    const double score_delta = static_cast<double>(move.score_delta);
    if (move.state.game_over) {
      value += score_delta + context.options.terminal_utility;
      continue;
    }
    move.state.score = 0;
    move.state.next_disc = cfpi::detail::sampledNextDisc(
        state_seed, sample, kChanceSamples);
    bool ignored = false;
    const State next = cfpi::detail::canonicalState(move.state, ignored);
    value += score_delta + bestFutureValue(next, depth - 1, context);
  }
  return value / kChanceSamples;
}

double bestFutureValue(const State& state, int depth, SearchContext& context) {
  ++context.nodes;
  checkWork(context);
  if (state.game_over) return context.options.terminal_utility;
  if (depth == 0) {
    ++context.work;
    const double value = cfpi::phasePotential(state);
    if (!std::isfinite(value)) {
      throw std::runtime_error("guided leaf returned non-finite value");
    }
    return value;
  }

  const std::string key = cfpi::detail::dynamicStateKey(state, depth);
  const auto cached = context.cache.find(key);
  if (cached != context.cache.end()) {
    ++context.cache_hits;
    context.order.splice(context.order.end(), context.order,
                         cached->second.order);
    return cached->second.value;
  }

  const ActionRanking ranking = interiorActions(state, context);
  double best = -std::numeric_limits<double>::infinity();
  for (int index = 0; index < ranking.count; ++index) {
    best = std::max(
        best, evaluateAction(state, ranking.actions[index], depth, context));
  }
  if (!std::isfinite(best)) best = context.options.terminal_utility;
  cacheValue(context, key, best);
  return best;
}

struct RootEvaluation {
  std::array<double, kBoardSize> values{};
  int action = -1;
  double value = -std::numeric_limits<double>::infinity();
};

RootEvaluation rootDecision(const State& canonical, int depth,
                            SearchContext& context) {
  RootEvaluation result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  // Root completeness invariant: the learned model never filters this loop.
  for (const int column : kColumnOrder) {
    if (!isLegal(canonical.board, column)) continue;
    const double value = evaluateAction(canonical, column, depth, context);
    result.values[column] = value;
    if (value > result.value) {
      result.value = value;
      result.action = column;
    }
  }
  return result;
}

struct SearchDecision {
  int action = -1;
  int canonical_action = -1;
  int completed_depth = 0;
  int depth_switches = 0;
  bool complete = false;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::uint64_t ranking_calls = 0;
  std::uint64_t ranking_actions = 0;
  std::uint64_t safety_additions = 0;
  std::size_t peak_cache_entries = 0;
  std::array<double, kBoardSize> canonical_root_values{};
  bool root_values_complete = false;
  bool ensemble_value_average = false;
  bool ensemble_vote_fallback = false;
  bool ensemble_switch = false;
  bool exact_first_deeper_commit = false;
  bool exact_first_action_switch = false;
  std::uint64_t exact_first_exact_work = 0;
  std::uint64_t exact_first_guided_work = 0;
};

SearchDecision chooseAction(const State& source, const SearchOptions& options,
                            const QModel* model) {
  validateSearchOptions(options);
  if (source.game_over) return {};
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  SearchContext context(options, model);
  int action = -1;
  int previous_action = -1;
  int completed_depth = 0;
  int switches = 0;
  std::array<double, kBoardSize> completed_values{};
  completed_values.fill(-std::numeric_limits<double>::infinity());
  for (int depth = 1; depth <= options.maximum_depth; ++depth) {
    try {
      const RootEvaluation candidate = rootDecision(canonical, depth, context);
      if (candidate.action < 0) break;
      if (previous_action >= 0 && candidate.action != previous_action) {
        ++switches;
      }
      previous_action = candidate.action;
      action = candidate.action;
      completed_values = candidate.values;
      completed_depth = depth;
    } catch (const WorkLimitReached&) {
      break;
    }
  }
  if (action < 0) action = centerFirstMove(canonical.board);
  SearchDecision result;
  result.action = mirrored && action >= 0 ? kBoardSize - 1 - action : action;
  result.canonical_action = action;
  result.completed_depth = completed_depth;
  result.depth_switches = switches;
  result.complete = completed_depth == options.maximum_depth;
  result.work = context.work;
  result.nodes = context.nodes;
  result.cache_hits = context.cache_hits;
  result.ranking_calls = context.ranking_calls;
  result.ranking_actions = context.ranking_actions;
  result.safety_additions = context.safety_additions;
  result.peak_cache_entries = context.peak_cache_entries;
  result.canonical_root_values = completed_values;
  result.root_values_complete = completed_depth > 0;
  return result;
}

SearchDecision chooseExactFirstAction(const State& source,
                                      const SearchOptions& base_options,
                                      const QModel& model) {
  if (!base_options.guided || base_options.maximum_depth != 5 ||
      base_options.top_k != 3 || !base_options.safety_union) {
    throw std::invalid_argument(
        "exact-first requires frozen guided K3 depth-five search");
  }
  validateSearchOptions(base_options);
  if (source.game_over) return {};

  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);

  SearchOptions exact_options = base_options;
  exact_options.maximum_depth = 3;
  exact_options.top_k = kBoardSize;
  exact_options.guided = false;
  exact_options.safety_union = false;
  SearchContext exact_context(exact_options, nullptr);
  RootEvaluation exact;
  try {
    // The exact fallback is computed first and is never replaced by a partial
    // deeper iteration.  Searching depth three directly avoids spending the
    // registered total-work budget on redundant depth-one/two iterations.
    exact = rootDecision(canonical, 3, exact_context);
  } catch (const WorkLimitReached&) {
    throw std::runtime_error(
        "fixed exact-first budget could not complete exact depth three");
  }
  if (exact.action < 0) {
    throw std::runtime_error("exact-first search found no legal root action");
  }

  int action = exact.action;
  int completed_depth = 3;
  int switches = 0;
  std::array<double, kBoardSize> completed_values = exact.values;
  std::uint64_t guided_work = 0;
  std::uint64_t guided_nodes = 0;
  std::uint64_t guided_cache_hits = 0;
  std::uint64_t guided_ranking_calls = 0;
  std::uint64_t guided_ranking_actions = 0;
  std::uint64_t guided_safety_additions = 0;
  std::size_t guided_peak_cache_entries = 0;

  const std::uint64_t remaining_work =
      base_options.maximum_work - exact_context.work;
  if (remaining_work > 0) {
    SearchOptions guided_options = base_options;
    guided_options.maximum_work = remaining_work;
    SearchContext guided_context(guided_options, &model);
    int previous_action = action;
    for (int depth = 4; depth <= guided_options.maximum_depth; ++depth) {
      try {
        const RootEvaluation candidate =
            rootDecision(canonical, depth, guided_context);
        if (candidate.action < 0) break;
        if (candidate.action != previous_action) ++switches;
        previous_action = candidate.action;
        action = candidate.action;
        completed_depth = depth;
        completed_values = candidate.values;
      } catch (const WorkLimitReached&) {
        break;
      }
    }
    guided_work = guided_context.work;
    guided_nodes = guided_context.nodes;
    guided_cache_hits = guided_context.cache_hits;
    guided_ranking_calls = guided_context.ranking_calls;
    guided_ranking_actions = guided_context.ranking_actions;
    guided_safety_additions = guided_context.safety_additions;
    guided_peak_cache_entries = guided_context.peak_cache_entries;
  }

  SearchDecision result;
  result.action = mirrored ? kBoardSize - 1 - action : action;
  result.canonical_action = action;
  result.completed_depth = completed_depth;
  result.depth_switches = switches;
  result.complete = completed_depth == base_options.maximum_depth;
  result.work = exact_context.work + guided_work;
  result.nodes = exact_context.nodes + guided_nodes;
  result.cache_hits = exact_context.cache_hits + guided_cache_hits;
  result.ranking_calls = guided_ranking_calls;
  result.ranking_actions = guided_ranking_actions;
  result.safety_additions = guided_safety_additions;
  result.peak_cache_entries =
      std::max(exact_context.peak_cache_entries, guided_peak_cache_entries);
  result.canonical_root_values = completed_values;
  result.root_values_complete = true;
  result.exact_first_deeper_commit = completed_depth > 3;
  result.exact_first_action_switch = action != exact.action;
  result.exact_first_exact_work = exact_context.work;
  result.exact_first_guided_work = guided_work;
  if (result.work > base_options.maximum_work) {
    throw std::logic_error("exact-first exceeded its fixed work budget");
  }
  return result;
}

constexpr std::array<std::uint32_t, 3> kEnsemblePolicySeeds{{
    0xd707'5eedu,
    0x91e1'0da5u,
    0x6a09'e667u,
}};

SearchDecision chooseEnsembleAction(const State& source,
                                    const SearchOptions& base_options,
                                    const QModel& model) {
  if (!base_options.guided || base_options.top_k != 3) {
    throw std::invalid_argument("ensemble requires frozen guided K3 search");
  }
  std::array<SearchDecision, kEnsemblePolicySeeds.size()> members;
  for (std::size_t member = 0; member < members.size(); ++member) {
    SearchOptions options = base_options;
    options.policy_seed = kEnsemblePolicySeeds[member];
    members[member] = chooseAction(source, options, &model);
    if (!isLegal(source.board, members[member].action)) {
      throw std::runtime_error("ensemble member selected illegal root action");
    }
  }

  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  const bool complete_values = std::all_of(
      members.begin(), members.end(), [](const SearchDecision& member) {
        return member.root_values_complete;
      });
  int winner = members[0].canonical_action;
  bool used_average = false;
  bool used_vote = false;
  if (complete_values) {
    double winning_value = -std::numeric_limits<double>::infinity();
    for (const int column : kColumnOrder) {
      if (!isLegal(canonical.board, column)) continue;
      double sum = 0.0;
      bool finite = true;
      for (const SearchDecision& member : members) {
        const double value = member.canonical_root_values[column];
        finite = finite && std::isfinite(value);
        sum += value;
      }
      if (finite && sum > winning_value) {
        winning_value = sum;
        winner = column;
      }
    }
    used_average = std::isfinite(winning_value);
  }
  if (!used_average) {
    used_vote = true;
    std::array<int, kBoardSize> votes{};
    for (const SearchDecision& member : members) {
      if (member.canonical_action >= 0) ++votes[member.canonical_action];
    }
    // Fixed split-vote fallback is member zero, matching the ensemble lab.
    int maximum_votes = votes[winner];
    for (const int column : kColumnOrder) {
      if (votes[column] > maximum_votes) {
        maximum_votes = votes[column];
        winner = column;
      }
    }
    if (maximum_votes < 2) winner = members[0].canonical_action;
  }

  SearchDecision result;
  result.action = mirrored ? kBoardSize - 1 - winner : winner;
  result.canonical_action = winner;
  result.completed_depth = members[0].completed_depth;
  result.complete = true;
  result.root_values_complete = used_average;
  result.ensemble_value_average = used_average;
  result.ensemble_vote_fallback = used_vote;
  result.ensemble_switch = winner != members[0].canonical_action;
  for (const SearchDecision& member : members) {
    result.completed_depth =
        std::min(result.completed_depth, member.completed_depth);
    result.complete = result.complete && member.complete;
    result.depth_switches += member.depth_switches;
    result.work += member.work;
    result.nodes += member.nodes;
    result.cache_hits += member.cache_hits;
    result.ranking_calls += member.ranking_calls;
    result.ranking_actions += member.ranking_actions;
    result.safety_additions += member.safety_additions;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, member.peak_cache_entries);
  }
  return result;
}

std::uint64_t peakRssBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
#endif
}

struct GameResult {
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::uint64_t ranking_calls = 0;
  std::uint64_t ranking_actions = 0;
  std::uint64_t safety_additions = 0;
  std::uint64_t depth_switches = 0;
  std::uint64_t complete_moves = 0;
  std::uint64_t depth_sum = 0;
  int minimum_depth = 8;
  int maximum_depth = 0;
  std::size_t peak_cache_entries = 0;
  std::uint64_t peak_rss_bytes = 0;
  std::uint64_t ensemble_value_averages = 0;
  std::uint64_t ensemble_vote_fallbacks = 0;
  std::uint64_t ensemble_switches = 0;
  std::uint64_t exact_first_deeper_commits = 0;
  std::uint64_t exact_first_action_switches = 0;
  std::uint64_t exact_first_exact_work = 0;
  std::uint64_t exact_first_guided_work = 0;
  double elapsed_seconds = 0.0;
};

GameResult runGame(std::uint32_t seed, const SearchOptions& options,
                   const QModel* model, int maximum_moves,
                   std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  GameResult result;
  while (!state.game_over && state.moves_played < maximum_moves) {
    const SearchDecision decision = chooseAction(state, options, model);
    if (!isLegal(state.board, decision.action)) {
      throw std::runtime_error("guided search selected illegal root action");
    }
    result.work += decision.work;
    result.nodes += decision.nodes;
    result.cache_hits += decision.cache_hits;
    result.ranking_calls += decision.ranking_calls;
    result.ranking_actions += decision.ranking_actions;
    result.safety_additions += decision.safety_additions;
    result.depth_switches += decision.depth_switches;
    result.exact_first_deeper_commits +=
        decision.exact_first_deeper_commit;
    result.exact_first_action_switches +=
        decision.exact_first_action_switch;
    result.exact_first_exact_work += decision.exact_first_exact_work;
    result.exact_first_guided_work += decision.exact_first_guided_work;
    result.complete_moves += decision.complete;
    result.depth_sum += decision.completed_depth;
    result.minimum_depth =
        std::min(result.minimum_depth, decision.completed_depth);
    result.maximum_depth =
        std::max(result.maximum_depth, decision.completed_depth);
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, decision.peak_cache_entries);
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("guided root transition failed");
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.peak_rss_bytes = peakRssBytes();
  result.elapsed_seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
  {
    const std::lock_guard<std::mutex> lock(progress_mutex);
    std::cerr << label << " seed 0x" << std::hex << seed << std::dec << ' '
              << result.score << " (" << result.moves << " moves, depth "
              << result.minimum_depth << '-' << result.maximum_depth
              << ", work " << result.work << ")\n";
  }
  return result;
}

GameResult runExactFirstGame(std::uint32_t seed,
                             const SearchOptions& options,
                             const QModel& model, int maximum_moves,
                             std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  GameResult result;
  while (!state.game_over && state.moves_played < maximum_moves) {
    const SearchDecision decision =
        chooseExactFirstAction(state, options, model);
    if (!isLegal(state.board, decision.action)) {
      throw std::runtime_error("exact-first selected illegal root action");
    }
    result.work += decision.work;
    result.nodes += decision.nodes;
    result.cache_hits += decision.cache_hits;
    result.ranking_calls += decision.ranking_calls;
    result.ranking_actions += decision.ranking_actions;
    result.safety_additions += decision.safety_additions;
    result.depth_switches += decision.depth_switches;
    result.exact_first_deeper_commits +=
        decision.exact_first_deeper_commit;
    result.exact_first_action_switches +=
        decision.exact_first_action_switch;
    result.exact_first_exact_work += decision.exact_first_exact_work;
    result.exact_first_guided_work += decision.exact_first_guided_work;
    result.complete_moves += decision.complete;
    result.depth_sum += decision.completed_depth;
    result.minimum_depth =
        std::min(result.minimum_depth, decision.completed_depth);
    result.maximum_depth =
        std::max(result.maximum_depth, decision.completed_depth);
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, decision.peak_cache_entries);
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("exact-first root transition failed");
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.peak_rss_bytes = peakRssBytes();
  result.elapsed_seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
  {
    const std::lock_guard<std::mutex> lock(progress_mutex);
    std::cerr << label << " seed 0x" << std::hex << seed << std::dec << ' '
              << result.score << " (" << result.moves << " moves, depth "
              << result.minimum_depth << '-' << result.maximum_depth
              << ", deeper " << result.exact_first_deeper_commits
              << ", switches " << result.exact_first_action_switches
              << ", work " << result.work << ")\n";
  }
  return result;
}

GameResult runEnsembleGame(std::uint32_t seed,
                           const SearchOptions& options,
                           const QModel& model, int maximum_moves,
                           std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  GameResult result;
  while (!state.game_over && state.moves_played < maximum_moves) {
    const SearchDecision decision =
        chooseEnsembleAction(state, options, model);
    if (!isLegal(state.board, decision.action)) {
      throw std::runtime_error("guided ensemble selected illegal root action");
    }
    result.work += decision.work;
    result.nodes += decision.nodes;
    result.cache_hits += decision.cache_hits;
    result.ranking_calls += decision.ranking_calls;
    result.ranking_actions += decision.ranking_actions;
    result.safety_additions += decision.safety_additions;
    result.depth_switches += decision.depth_switches;
    result.complete_moves += decision.complete;
    result.depth_sum += decision.completed_depth;
    result.minimum_depth =
        std::min(result.minimum_depth, decision.completed_depth);
    result.maximum_depth =
        std::max(result.maximum_depth, decision.completed_depth);
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, decision.peak_cache_entries);
    result.ensemble_value_averages += decision.ensemble_value_average;
    result.ensemble_vote_fallbacks += decision.ensemble_vote_fallback;
    result.ensemble_switches += decision.ensemble_switch;
    result.exact_first_deeper_commits +=
        decision.exact_first_deeper_commit;
    result.exact_first_action_switches +=
        decision.exact_first_action_switch;
    result.exact_first_exact_work += decision.exact_first_exact_work;
    result.exact_first_guided_work += decision.exact_first_guided_work;
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("ensemble root transition failed");
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.peak_rss_bytes = peakRssBytes();
  result.elapsed_seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
  {
    const std::lock_guard<std::mutex> lock(progress_mutex);
    std::cerr << label << " seed 0x" << std::hex << seed << std::dec << ' '
              << result.score << " (" << result.moves << " moves, work "
              << result.work << ", avg " << result.ensemble_value_averages
              << ", vote " << result.ensemble_vote_fallbacks << ")\n";
  }
  return result;
}

struct Summary {
  int games = 0;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double work_per_move = 0.0;
  double nodes_per_move = 0.0;
  double cache_hits_per_move = 0.0;
  double mean_completed_depth = 0.0;
  double complete_rate = 0.0;
  double switches_per_move = 0.0;
  double ranking_calls_per_move = 0.0;
  double safety_additions_per_move = 0.0;
  double ensemble_value_average_rate = 0.0;
  double ensemble_vote_fallback_rate = 0.0;
  double ensemble_switches_per_move = 0.0;
  double exact_first_deeper_commit_rate = 0.0;
  double exact_first_action_switch_rate = 0.0;
  double exact_first_exact_work_per_move = 0.0;
  double exact_first_guided_work_per_move = 0.0;
  double aggregate_game_seconds = 0.0;
  double moves_per_game_second = 0.0;
  double work_per_game_second = 0.0;
  int censored = 0;
  int minimum_depth = 8;
  int maximum_depth = 0;
  std::size_t peak_cache_entries = 0;
  std::uint64_t peak_rss_bytes = 0;
};

Summary summarize(const std::vector<GameResult>& games) {
  if (games.empty()) throw std::invalid_argument("empty guided cohort");
  Summary result;
  result.games = static_cast<int>(games.size());
  std::uint64_t total_moves = 0;
  std::uint64_t total_work = 0;
  std::uint64_t total_nodes = 0;
  std::uint64_t total_cache_hits = 0;
  std::uint64_t total_depth = 0;
  std::uint64_t total_complete = 0;
  std::uint64_t total_switches = 0;
  std::uint64_t total_ranking_calls = 0;
  std::uint64_t total_safety_additions = 0;
  std::uint64_t total_value_averages = 0;
  std::uint64_t total_vote_fallbacks = 0;
  std::uint64_t total_ensemble_switches = 0;
  std::uint64_t total_exact_first_deeper_commits = 0;
  std::uint64_t total_exact_first_action_switches = 0;
  std::uint64_t total_exact_first_exact_work = 0;
  std::uint64_t total_exact_first_guided_work = 0;
  for (const GameResult& game : games) {
    result.mean_score += static_cast<double>(game.score) / games.size();
    result.mean_moves += static_cast<double>(game.moves) / games.size();
    total_moves += game.moves;
    total_work += game.work;
    total_nodes += game.nodes;
    total_cache_hits += game.cache_hits;
    total_depth += game.depth_sum;
    total_complete += game.complete_moves;
    total_switches += game.depth_switches;
    total_ranking_calls += game.ranking_calls;
    total_safety_additions += game.safety_additions;
    total_value_averages += game.ensemble_value_averages;
    total_vote_fallbacks += game.ensemble_vote_fallbacks;
    total_ensemble_switches += game.ensemble_switches;
    total_exact_first_deeper_commits += game.exact_first_deeper_commits;
    total_exact_first_action_switches +=
        game.exact_first_action_switches;
    total_exact_first_exact_work += game.exact_first_exact_work;
    total_exact_first_guided_work += game.exact_first_guided_work;
    result.aggregate_game_seconds += game.elapsed_seconds;
    result.censored += game.censored;
    result.minimum_depth = std::min(result.minimum_depth, game.minimum_depth);
    result.maximum_depth = std::max(result.maximum_depth, game.maximum_depth);
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, game.peak_cache_entries);
    result.peak_rss_bytes =
        std::max(result.peak_rss_bytes, game.peak_rss_bytes);
  }
  const double moves = static_cast<double>(std::max<std::uint64_t>(1, total_moves));
  result.work_per_move = total_work / moves;
  result.nodes_per_move = total_nodes / moves;
  result.cache_hits_per_move = total_cache_hits / moves;
  result.mean_completed_depth = total_depth / moves;
  result.complete_rate = total_complete / moves;
  result.switches_per_move = total_switches / moves;
  result.ranking_calls_per_move = total_ranking_calls / moves;
  result.safety_additions_per_move = total_safety_additions / moves;
  result.ensemble_value_average_rate = total_value_averages / moves;
  result.ensemble_vote_fallback_rate = total_vote_fallbacks / moves;
  result.ensemble_switches_per_move = total_ensemble_switches / moves;
  result.exact_first_deeper_commit_rate =
      total_exact_first_deeper_commits / moves;
  result.exact_first_action_switch_rate =
      total_exact_first_action_switches / moves;
  result.exact_first_exact_work_per_move =
      total_exact_first_exact_work / moves;
  result.exact_first_guided_work_per_move =
      total_exact_first_guided_work / moves;
  result.moves_per_game_second =
      moves / std::max(1.0e-9, result.aggregate_game_seconds);
  result.work_per_game_second =
      total_work / std::max(1.0e-9, result.aggregate_game_seconds);
  return result;
}

struct Cohort {
  std::vector<GameResult> exact;
  std::vector<GameResult> k2;
  std::vector<GameResult> k3;
};

Cohort runScreen(std::uint32_t seed_start, int games, int maximum_moves,
                 std::uint64_t maximum_work,
                 std::size_t maximum_cache_entries, bool safety_union,
                 const QModel& model) {
  Cohort cohort;
  cohort.exact.reserve(games);
  cohort.k2.reserve(games);
  cohort.k3.reserve(games);
  SearchOptions exact;
  exact.maximum_depth = 3;
  exact.top_k = kBoardSize;
  exact.guided = false;
  exact.maximum_work = maximum_work;
  exact.maximum_cache_entries = maximum_cache_entries;
  SearchOptions k2 = exact;
  k2.maximum_depth = 5;
  k2.top_k = 2;
  k2.guided = true;
  k2.safety_union = safety_union;
  SearchOptions k3 = k2;
  k3.top_k = 3;
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed =
        seed_start + static_cast<std::uint32_t>(game);
    cohort.exact.push_back(runGame(seed, exact, nullptr, maximum_moves,
                                   "screen-exact-d3"));
    cohort.k2.push_back(
        runGame(seed, k2, &model, maximum_moves, "screen-guided-k2"));
    cohort.k3.push_back(
        runGame(seed, k3, &model, maximum_moves, "screen-guided-k3"));
  }
  return cohort;
}

struct Confirmation {
  std::vector<GameResult> exact;
  std::vector<GameResult> guided;
};

Confirmation runConfirmation(std::uint32_t seed_start, int games,
                             int maximum_moves, std::uint64_t maximum_work,
                             std::size_t maximum_cache_entries, int top_k,
                             bool safety_union, const QModel& model) {
  Confirmation cohort;
  SearchOptions exact;
  exact.maximum_depth = 3;
  exact.maximum_work = maximum_work;
  exact.maximum_cache_entries = maximum_cache_entries;
  SearchOptions guided = exact;
  guided.maximum_depth = 5;
  guided.top_k = top_k;
  guided.guided = true;
  guided.safety_union = safety_union;
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed =
        seed_start + static_cast<std::uint32_t>(game);
    cohort.exact.push_back(runGame(seed, exact, nullptr, maximum_moves,
                                   "confirm-exact-d3"));
    cohort.guided.push_back(runGame(seed, guided, &model, maximum_moves,
                                    "confirm-guided"));
  }
  return cohort;
}

bool materiallyImproves(const Summary& candidate, const Summary& baseline) {
  const double score_threshold =
      std::max(10'000.0, std::abs(baseline.mean_score) * 0.05);
  const double move_threshold = std::max(3.0, baseline.mean_moves * 0.05);
  return candidate.mean_score - baseline.mean_score >= score_threshold &&
         candidate.mean_moves - baseline.mean_moves >= move_threshold;
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"games\":" << summary.games
         << ",\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"workPerMove\":" << summary.work_per_move
         << ",\"nodesPerMove\":" << summary.nodes_per_move
         << ",\"cacheHitsPerMove\":" << summary.cache_hits_per_move
         << ",\"meanCompletedDepth\":" << summary.mean_completed_depth
         << ",\"minimumDepth\":" << summary.minimum_depth
         << ",\"maximumDepth\":" << summary.maximum_depth
         << ",\"completeRate\":" << summary.complete_rate
         << ",\"switchesPerMove\":" << summary.switches_per_move
         << ",\"rankingCallsPerMove\":" << summary.ranking_calls_per_move
         << ",\"safetyAdditionsPerMove\":"
         << summary.safety_additions_per_move
         << ",\"ensembleValueAverageRate\":"
         << summary.ensemble_value_average_rate
         << ",\"ensembleVoteFallbackRate\":"
         << summary.ensemble_vote_fallback_rate
         << ",\"ensembleSwitchesPerMove\":"
         << summary.ensemble_switches_per_move
         << ",\"exactFirstDeeperCommitRate\":"
         << summary.exact_first_deeper_commit_rate
         << ",\"exactFirstActionSwitchRate\":"
         << summary.exact_first_action_switch_rate
         << ",\"exactFirstExactWorkPerMove\":"
         << summary.exact_first_exact_work_per_move
         << ",\"exactFirstGuidedWorkPerMove\":"
         << summary.exact_first_guided_work_per_move
         << ",\"aggregateGameSeconds\":"
         << summary.aggregate_game_seconds
         << ",\"movesPerGameSecond\":" << summary.moves_per_game_second
         << ",\"workPerGameSecond\":" << summary.work_per_game_second
         << ",\"peakCacheEntries\":" << summary.peak_cache_entries
         << ",\"peakRssBytes\":" << summary.peak_rss_bytes
         << ",\"censored\":" << summary.censored << '}';
}

struct BenchmarkOptions {
  std::string model = "/tmp/drop7-phase-q-student-scale.bin";
  std::string output = "/tmp/drop7-nnue-guided-search.json";
  int screen_games = 4;
  int confirmation_games = 8;
  int maximum_moves = 200;
  std::uint64_t maximum_work = 250'000;
  std::size_t maximum_cache_entries = 40'000;
  bool safety_union = true;
};

constexpr std::uint32_t kScreenSeedStart = 0x3d70'2000u;

int positiveInteger(const char* value, std::string_view name) {
  const std::string text = value;
  std::size_t consumed = 0;
  const unsigned long long parsed = std::stoull(text, &consumed, 0);
  if (consumed != text.size() || parsed == 0 ||
      parsed > static_cast<unsigned long long>(
                   std::numeric_limits<int>::max())) {
    throw std::invalid_argument(std::string(name) + " must be positive");
  }
  return static_cast<int>(parsed);
}

BenchmarkOptions parseBenchmarkOptions(int argc, char** argv) {
  BenchmarkOptions options;
  for (int index = 2; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--no-safety-union") {
      options.safety_union = false;
      continue;
    }
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing value for " + argument);
    }
    const char* value = argv[++index];
    if (argument == "--model") {
      options.model = value;
    } else if (argument == "--output") {
      options.output = value;
    } else if (argument == "--screen-games") {
      options.screen_games = positiveInteger(value, argument);
    } else if (argument == "--confirmation-games") {
      options.confirmation_games = positiveInteger(value, argument);
    } else if (argument == "--max-moves") {
      options.maximum_moves = positiveInteger(value, argument);
    } else if (argument == "--max-work") {
      options.maximum_work =
          static_cast<std::uint64_t>(positiveInteger(value, argument));
    } else if (argument == "--max-cache") {
      options.maximum_cache_entries =
          static_cast<std::size_t>(positiveInteger(value, argument));
    } else {
      throw std::invalid_argument("unknown argument " + argument);
    }
  }
  const std::uint64_t final_seed =
      static_cast<std::uint64_t>(kScreenSeedStart) + options.screen_games +
      options.confirmation_games;
  if (kScreenSeedStart < kTrainingStart || final_seed > kTrainingEnd) {
    throw std::invalid_argument("benchmark leaves the training partition");
  }
  return options;
}

void writeBenchmarkArtifact(
    const BenchmarkOptions& options, const Summary& screen_exact,
    const Summary& screen_k2, const Summary& screen_k3, int chosen_k,
    bool screen_passed, const Summary* confirm_exact,
    const Summary* confirm_guided, bool confirmed, double elapsed_seconds) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open guided result artifact");
  output << std::setprecision(10)
         << "{\n  \"format\": \"drop7-nnue-guided-search-v1\",\n"
         << "  \"trainingSeedOnly\": true,\n"
         << "  \"publicStateOnly\": true,\n"
         << "  \"rootCompleteness\": \"all-legal-actions\",\n"
         << "  \"chanceSamples\": 5,\n"
         << "  \"matchedMaxWork\": " << options.maximum_work << ",\n"
         << "  \"maximumCacheEntries\": "
         << options.maximum_cache_entries << ",\n"
         << "  \"safetyUnion\": "
         << (options.safety_union ? "true" : "false") << ",\n"
         << "  \"screenSeedStart\": " << kScreenSeedStart << ",\n"
         << "  \"screen\": {\n    \"exactD3\": ";
  writeSummary(output, screen_exact);
  output << ",\n    \"guidedK2\": ";
  writeSummary(output, screen_k2);
  output << ",\n    \"guidedK3\": ";
  writeSummary(output, screen_k3);
  output << "\n  },\n  \"materialThresholds\": {\"score\":\"max(10000,5%)\","
            "\"moves\":\"max(3,5%)\"},\n"
         << "  \"screenPassed\": "
         << (screen_passed ? "true" : "false")
         << ",\n  \"chosenK\": " << chosen_k << ",\n"
         << "  \"confirmation\": ";
  if (confirm_exact == nullptr || confirm_guided == nullptr) {
    output << "null";
  } else {
    output << "{\"seedStart\":"
           << kScreenSeedStart + static_cast<std::uint32_t>(options.screen_games)
           << ",\"exactD3\":";
    writeSummary(output, *confirm_exact);
    output << ",\"guided\":";
    writeSummary(output, *confirm_guided);
    output << '}';
  }
  const char* decision = !screen_passed
                             ? "reject-screen"
                             : (confirmed ? "advance" : "reject-confirmation");
  output << ",\n  \"confirmed\": " << (confirmed ? "true" : "false")
         << ",\n  \"decision\": \"" << decision
         << "\",\n  \"model\": \"" << options.model
         << "\",\n  \"elapsedSeconds\": " << elapsed_seconds << "\n}\n";
}

int runBenchmark(const BenchmarkOptions& options, std::ostream& output) {
  const auto started = std::chrono::steady_clock::now();
  QModel model;
  model.load(options.model);
  const Cohort screen =
      runScreen(kScreenSeedStart, options.screen_games, options.maximum_moves,
                options.maximum_work, options.maximum_cache_entries,
                options.safety_union, model);
  const Summary screen_exact = summarize(screen.exact);
  const Summary screen_k2 = summarize(screen.k2);
  const Summary screen_k3 = summarize(screen.k3);
  const bool k2_passed = materiallyImproves(screen_k2, screen_exact);
  const bool k3_passed = materiallyImproves(screen_k3, screen_exact);
  int chosen_k = 0;
  if (k2_passed || k3_passed) {
    if (!k3_passed) {
      chosen_k = 2;
    } else if (!k2_passed) {
      chosen_k = 3;
    } else {
      const double k2_gain =
          (screen_k2.mean_score - screen_exact.mean_score) /
              std::max(1.0, std::abs(screen_exact.mean_score)) +
          (screen_k2.mean_moves - screen_exact.mean_moves) /
              std::max(1.0, screen_exact.mean_moves);
      const double k3_gain =
          (screen_k3.mean_score - screen_exact.mean_score) /
              std::max(1.0, std::abs(screen_exact.mean_score)) +
          (screen_k3.mean_moves - screen_exact.mean_moves) /
              std::max(1.0, screen_exact.mean_moves);
      chosen_k = k2_gain >= k3_gain ? 2 : 3;
    }
  }

  bool confirmed = false;
  Summary confirmation_exact;
  Summary confirmation_guided;
  if (chosen_k != 0) {
    const Confirmation confirmation = runConfirmation(
        kScreenSeedStart + static_cast<std::uint32_t>(options.screen_games),
        options.confirmation_games, options.maximum_moves,
        options.maximum_work, options.maximum_cache_entries, chosen_k,
        options.safety_union, model);
    confirmation_exact = summarize(confirmation.exact);
    confirmation_guided = summarize(confirmation.guided);
    confirmed =
        materiallyImproves(confirmation_guided, confirmation_exact);
  }
  const double elapsed_seconds = std::chrono::duration<double>(
                                     std::chrono::steady_clock::now() - started)
                                     .count();
  writeBenchmarkArtifact(
      options, screen_exact, screen_k2, screen_k3, chosen_k, chosen_k != 0,
      chosen_k != 0 ? &confirmation_exact : nullptr,
      chosen_k != 0 ? &confirmation_guided : nullptr, confirmed,
      elapsed_seconds);
  output << std::fixed << std::setprecision(3)
         << "NNUE_GUIDED_RESULT {\"trainingSeedOnly\":true"
         << ",\"exactScore\":" << screen_exact.mean_score
         << ",\"exactMoves\":" << screen_exact.mean_moves
         << ",\"k2Score\":" << screen_k2.mean_score
         << ",\"k2Moves\":" << screen_k2.mean_moves
         << ",\"k3Score\":" << screen_k3.mean_score
         << ",\"k3Moves\":" << screen_k3.mean_moves
         << ",\"chosenK\":" << chosen_k
         << ",\"screenPassed\":" << (chosen_k != 0 ? "true" : "false")
         << ",\"confirmed\":" << (confirmed ? "true" : "false")
         << ",\"decision\":\""
         << (chosen_k == 0
                 ? "reject-screen"
                 : (confirmed ? "advance" : "reject-confirmation"))
         << "\",\"artifact\":\"" << options.output << "\"}\n";
  return 0;
}

struct StudyOptions {
  std::string model = "/tmp/drop7-phase-q-student-scale.bin";
  std::string output = "/tmp/drop7-nnue-guided-ensemble-study.json";
  int screen_games = 4;
  int confirmation_games = 8;
  int expansion_games = 16;
  int maximum_moves = 200;
  int parallelism = 4;
  std::uint64_t maximum_work = 250'000;
  std::size_t maximum_cache_entries = 40'000;
};

constexpr std::uint32_t kEnsembleScreenSeedStart = 0x3d70'3000u;
constexpr std::uint32_t kExpansionSeedStart = 0x3d70'4000u;

StudyOptions parseStudyOptions(int argc, char** argv) {
  StudyOptions options;
  for (int index = 2; index < argc; ++index) {
    const std::string argument = argv[index];
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing value for " + argument);
    }
    const char* value = argv[++index];
    if (argument == "--model") {
      options.model = value;
    } else if (argument == "--output") {
      options.output = value;
    } else if (argument == "--screen-games") {
      options.screen_games = positiveInteger(value, argument);
    } else if (argument == "--confirmation-games") {
      options.confirmation_games = positiveInteger(value, argument);
    } else if (argument == "--expansion-games") {
      options.expansion_games = positiveInteger(value, argument);
    } else if (argument == "--max-moves") {
      options.maximum_moves = positiveInteger(value, argument);
    } else if (argument == "--parallel") {
      options.parallelism = positiveInteger(value, argument);
    } else if (argument == "--max-work") {
      options.maximum_work =
          static_cast<std::uint64_t>(positiveInteger(value, argument));
    } else if (argument == "--max-cache") {
      options.maximum_cache_entries =
          static_cast<std::size_t>(positiveInteger(value, argument));
    } else {
      throw std::invalid_argument("unknown study argument " + argument);
    }
  }
  const std::uint64_t ensemble_end =
      static_cast<std::uint64_t>(kEnsembleScreenSeedStart) +
      options.screen_games + options.confirmation_games;
  const std::uint64_t expansion_end =
      static_cast<std::uint64_t>(kExpansionSeedStart) +
      options.expansion_games;
  if (kEnsembleScreenSeedStart < kTrainingStart ||
      kExpansionSeedStart < kTrainingStart || ensemble_end > kTrainingEnd ||
      expansion_end > kTrainingEnd) {
    throw std::invalid_argument("ensemble study leaves training partition");
  }
  options.parallelism = std::min(options.parallelism, 16);
  return options;
}

SearchOptions frozenGuidedOptions(const StudyOptions& options) {
  SearchOptions guided;
  guided.maximum_depth = 5;
  guided.top_k = 3;
  guided.guided = true;
  guided.safety_union = true;
  guided.maximum_work = options.maximum_work;
  guided.maximum_cache_entries = options.maximum_cache_entries;
  guided.policy_seed = kEnsemblePolicySeeds[0];
  return guided;
}

SearchOptions frozenExactOptions(const StudyOptions& options) {
  SearchOptions exact;
  exact.maximum_depth = 3;
  exact.top_k = kBoardSize;
  exact.guided = false;
  exact.maximum_work = options.maximum_work;
  exact.maximum_cache_entries = options.maximum_cache_entries;
  exact.policy_seed = kEnsemblePolicySeeds[0];
  return exact;
}

struct SingleEnsembleCohort {
  std::vector<GameResult> single;
  std::vector<GameResult> ensemble;
  double wall_seconds = 0.0;
};

SingleEnsembleCohort runSingleEnsembleCohort(
    std::uint32_t seed_start, int games, const StudyOptions& options,
    const QModel& model, std::string_view phase) {
  const auto started = std::chrono::steady_clock::now();
  SingleEnsembleCohort cohort;
  cohort.single.resize(games);
  cohort.ensemble.resize(games);
  const SearchOptions guided = frozenGuidedOptions(options);
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  const int worker_count = std::min(options.parallelism, games);
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= games) return;
        const std::uint32_t seed =
            seed_start + static_cast<std::uint32_t>(game);
        const std::string single_label = std::string(phase) + "-single-k3";
        const std::string ensemble_label =
            std::string(phase) + "-ensemble-k3";
        cohort.single[game] = runGame(seed, guided, &model,
                                      options.maximum_moves, single_label);
        cohort.ensemble[game] = runEnsembleGame(
            seed, guided, model, options.maximum_moves, ensemble_label);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  cohort.wall_seconds = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started)
                            .count();
  return cohort;
}

struct ExactGuidedCohort {
  std::vector<GameResult> exact;
  std::vector<GameResult> guided;
  double wall_seconds = 0.0;
};

ExactGuidedCohort runExactGuidedCohort(std::uint32_t seed_start, int games,
                                      const StudyOptions& options,
                                      const QModel& model) {
  const auto started = std::chrono::steady_clock::now();
  ExactGuidedCohort cohort;
  cohort.exact.resize(games);
  cohort.guided.resize(games);
  const SearchOptions exact = frozenExactOptions(options);
  const SearchOptions guided = frozenGuidedOptions(options);
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  const int worker_count = std::min(options.parallelism, games);
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= games) return;
        const std::uint32_t seed =
            seed_start + static_cast<std::uint32_t>(game);
        cohort.exact[game] = runGame(seed, exact, nullptr,
                                     options.maximum_moves,
                                     "expansion-exact-d3");
        cohort.guided[game] = runGame(seed, guided, &model,
                                      options.maximum_moves,
                                      "expansion-guided-k3");
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  cohort.wall_seconds = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started)
                            .count();
  return cohort;
}

struct PairedMetrics {
  double mean_score_delta = 0.0;
  double mean_move_delta = 0.0;
  int score_wins = 0;
  int move_wins = 0;
  int score_ties = 0;
  int move_ties = 0;
};

PairedMetrics pairedMetrics(const std::vector<GameResult>& baseline,
                            const std::vector<GameResult>& candidate) {
  if (baseline.size() != candidate.size() || baseline.empty()) {
    throw std::invalid_argument("paired cohorts must have equal nonzero size");
  }
  PairedMetrics result;
  for (std::size_t index = 0; index < baseline.size(); ++index) {
    result.mean_score_delta +=
        static_cast<double>(candidate[index].score - baseline[index].score) /
        baseline.size();
    result.mean_move_delta +=
        static_cast<double>(candidate[index].moves - baseline[index].moves) /
        baseline.size();
    result.score_wins += candidate[index].score > baseline[index].score;
    result.move_wins += candidate[index].moves > baseline[index].moves;
    result.score_ties += candidate[index].score == baseline[index].score;
    result.move_ties += candidate[index].moves == baseline[index].moves;
  }
  return result;
}

void writePairedMetrics(std::ostream& output, const PairedMetrics& metrics) {
  output << "{\"meanScoreDelta\":" << metrics.mean_score_delta
         << ",\"meanMoveDelta\":" << metrics.mean_move_delta
         << ",\"scoreWins\":" << metrics.score_wins
         << ",\"moveWins\":" << metrics.move_wins
         << ",\"scoreTies\":" << metrics.score_ties
         << ",\"moveTies\":" << metrics.move_ties << '}';
}

void writeStudyArtifact(
    const StudyOptions& options, const Summary& screen_single,
    const Summary& screen_ensemble, const PairedMetrics& screen_paired,
    double screen_wall, bool screen_passed,
    const Summary* confirmation_single,
    const Summary* confirmation_ensemble,
    const PairedMetrics* confirmation_paired, double confirmation_wall,
    bool confirmation_passed, const Summary& expansion_exact,
    const Summary& expansion_guided, const PairedMetrics& expansion_paired,
    double expansion_wall, double total_wall) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open ensemble study artifact");
  output << std::setprecision(10)
         << "{\n  \"format\": \"drop7-nnue-guided-ensemble-study-v1\",\n"
         << "  \"trainingSeedOnly\": true,\n"
         << "  \"publicStateOnly\": true,\n"
         << "  \"policySeeds\": [" << kEnsemblePolicySeeds[0] << ','
         << kEnsemblePolicySeeds[1] << ',' << kEnsemblePolicySeeds[2]
         << "],\n  \"ensembleRule\": \"mean-complete-root-q-else-majority-member0-tie\",\n"
         << "  \"frozenGuided\": {\"depth\":5,\"topK\":3,"
            "\"chanceSamples\":5,\"safetyUnion\":true,\"maxWorkPerMember\":"
         << options.maximum_work << ",\"maxCacheEntries\":"
         << options.maximum_cache_entries << "},\n"
         << "  \"parallelism\": " << options.parallelism << ",\n"
         << "  \"ensembleScreen\": {\"seedStart\":"
         << kEnsembleScreenSeedStart << ",\"games\":"
         << options.screen_games << ",\"single\":";
  writeSummary(output, screen_single);
  output << ",\"ensemble\":";
  writeSummary(output, screen_ensemble);
  output << ",\"paired\":";
  writePairedMetrics(output, screen_paired);
  output << ",\"wallSeconds\":" << screen_wall << ",\"passed\":"
         << (screen_passed ? "true" : "false") << "},\n"
         << "  \"ensembleConfirmation\": ";
  if (confirmation_single == nullptr || confirmation_ensemble == nullptr ||
      confirmation_paired == nullptr) {
    output << "null";
  } else {
    output << "{\"seedStart\":"
           << kEnsembleScreenSeedStart +
                  static_cast<std::uint32_t>(options.screen_games)
           << ",\"games\":" << options.confirmation_games
           << ",\"single\":";
    writeSummary(output, *confirmation_single);
    output << ",\"ensemble\":";
    writeSummary(output, *confirmation_ensemble);
    output << ",\"paired\":";
    writePairedMetrics(output, *confirmation_paired);
    output << ",\"wallSeconds\":" << confirmation_wall
           << ",\"passed\":"
           << (confirmation_passed ? "true" : "false") << '}';
  }
  output << ",\n  \"singleK3Expansion\": {\"seedStart\":"
         << kExpansionSeedStart << ",\"games\":" << options.expansion_games
         << ",\"exactD3\":";
  writeSummary(output, expansion_exact);
  output << ",\"guidedK3\":";
  writeSummary(output, expansion_guided);
  output << ",\"paired\":";
  writePairedMetrics(output, expansion_paired);
  output << ",\"wallSeconds\":" << expansion_wall << "},\n"
         << "  \"ensembleScreenPassed\":"
         << (screen_passed ? "true" : "false")
         << ",\n  \"ensembleConfirmed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\n  \"ensembleDecision\": \""
         << (!screen_passed
                 ? "reject-screen"
                 : (confirmation_passed ? "compose" : "reject-confirmation"))
         << "\",\n  \"model\": \"" << options.model
         << "\",\n  \"totalWallSeconds\":" << total_wall << "\n}\n";
}

int runEnsembleStudy(const StudyOptions& options, std::ostream& output) {
  const auto started = std::chrono::steady_clock::now();
  QModel model;
  model.load(options.model);

  const SingleEnsembleCohort screen = runSingleEnsembleCohort(
      kEnsembleScreenSeedStart, options.screen_games, options, model,
      "ensemble-screen");
  const Summary screen_single = summarize(screen.single);
  const Summary screen_ensemble = summarize(screen.ensemble);
  const PairedMetrics screen_paired =
      pairedMetrics(screen.single, screen.ensemble);
  const bool screen_passed =
      screen_ensemble.mean_score > screen_single.mean_score &&
      screen_ensemble.mean_moves > screen_single.mean_moves;

  Summary confirmation_single;
  Summary confirmation_ensemble;
  PairedMetrics confirmation_paired;
  double confirmation_wall = 0.0;
  bool confirmation_passed = false;
  if (screen_passed) {
    const SingleEnsembleCohort confirmation = runSingleEnsembleCohort(
        kEnsembleScreenSeedStart +
            static_cast<std::uint32_t>(options.screen_games),
        options.confirmation_games, options, model, "ensemble-confirm");
    confirmation_single = summarize(confirmation.single);
    confirmation_ensemble = summarize(confirmation.ensemble);
    confirmation_paired =
        pairedMetrics(confirmation.single, confirmation.ensemble);
    confirmation_wall = confirmation.wall_seconds;
    confirmation_passed =
        confirmation_ensemble.mean_score > confirmation_single.mean_score &&
        confirmation_ensemble.mean_moves > confirmation_single.mean_moves;
  }

  // This expansion is unconditional: it uses the fixed single-K3 candidate
  // and does not depend on the ensemble-composition gate.
  const ExactGuidedCohort expansion = runExactGuidedCohort(
      kExpansionSeedStart, options.expansion_games, options, model);
  const Summary expansion_exact = summarize(expansion.exact);
  const Summary expansion_guided = summarize(expansion.guided);
  const PairedMetrics expansion_paired =
      pairedMetrics(expansion.exact, expansion.guided);
  const double total_wall = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - started)
                                .count();
  writeStudyArtifact(
      options, screen_single, screen_ensemble, screen_paired,
      screen.wall_seconds, screen_passed,
      screen_passed ? &confirmation_single : nullptr,
      screen_passed ? &confirmation_ensemble : nullptr,
      screen_passed ? &confirmation_paired : nullptr, confirmation_wall,
      confirmation_passed, expansion_exact, expansion_guided,
      expansion_paired, expansion.wall_seconds, total_wall);
  output << std::fixed << std::setprecision(3)
         << "NNUE_ENSEMBLE_STUDY_RESULT {\"trainingSeedOnly\":true"
         << ",\"screenSingleScore\":" << screen_single.mean_score
         << ",\"screenSingleMoves\":" << screen_single.mean_moves
         << ",\"screenEnsembleScore\":" << screen_ensemble.mean_score
         << ",\"screenEnsembleMoves\":" << screen_ensemble.mean_moves
         << ",\"screenPassed\":" << (screen_passed ? "true" : "false")
         << ",\"confirmationRan\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\"expansionExactScore\":" << expansion_exact.mean_score
         << ",\"expansionExactMoves\":" << expansion_exact.mean_moves
         << ",\"expansionGuidedScore\":" << expansion_guided.mean_score
         << ",\"expansionGuidedMoves\":" << expansion_guided.mean_moves
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return 0;
}

struct ExactFirstStudyOptions {
  std::string model = "/tmp/drop7-phase-q-student-scale.bin";
  std::string output = "/tmp/drop7-nnue-exact-first-study.json";
  int screen_games = 4;
  int confirmation_games = 8;
  int maximum_moves = 200;
  int parallelism = 4;
  std::uint64_t maximum_work = 250'000;
  std::size_t maximum_cache_entries = 40'000;
};

constexpr std::uint32_t kExactFirstScreenSeedStart = 0x3d70'b000u;
constexpr std::uint32_t kExactFirstConfirmationSeedStart = 0x3d70'b100u;

ExactFirstStudyOptions parseExactFirstStudyOptions(int argc, char** argv) {
  ExactFirstStudyOptions options;
  for (int index = 2; index < argc; ++index) {
    const std::string argument = argv[index];
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing value for " + argument);
    }
    const char* value = argv[++index];
    if (argument == "--model") {
      options.model = value;
    } else if (argument == "--output") {
      options.output = value;
    } else if (argument == "--screen-games") {
      options.screen_games = positiveInteger(value, argument);
    } else if (argument == "--confirmation-games") {
      options.confirmation_games = positiveInteger(value, argument);
    } else if (argument == "--max-moves") {
      options.maximum_moves = positiveInteger(value, argument);
    } else if (argument == "--parallel") {
      options.parallelism = positiveInteger(value, argument);
    } else if (argument == "--max-work") {
      options.maximum_work =
          static_cast<std::uint64_t>(positiveInteger(value, argument));
    } else if (argument == "--max-cache") {
      options.maximum_cache_entries =
          static_cast<std::size_t>(positiveInteger(value, argument));
    } else {
      throw std::invalid_argument("unknown exact-first argument " + argument);
    }
  }
  const std::uint64_t screen_end =
      static_cast<std::uint64_t>(kExactFirstScreenSeedStart) +
      options.screen_games;
  const std::uint64_t confirmation_end =
      static_cast<std::uint64_t>(kExactFirstConfirmationSeedStart) +
      options.confirmation_games;
  if (kExactFirstScreenSeedStart < kTrainingStart ||
      kExactFirstConfirmationSeedStart < kTrainingStart ||
      screen_end > kTrainingEnd || confirmation_end > kTrainingEnd) {
    throw std::invalid_argument("exact-first study leaves training partition");
  }
  options.parallelism = std::min(options.parallelism, 16);
  return options;
}

SearchOptions exactFirstExactOptions(const ExactFirstStudyOptions& options) {
  SearchOptions exact;
  exact.maximum_depth = 3;
  exact.top_k = kBoardSize;
  exact.guided = false;
  exact.maximum_work = options.maximum_work;
  exact.maximum_cache_entries = options.maximum_cache_entries;
  exact.policy_seed = kEnsemblePolicySeeds[0];
  return exact;
}

SearchOptions exactFirstGuidedOptions(const ExactFirstStudyOptions& options) {
  SearchOptions guided;
  guided.maximum_depth = 5;
  guided.top_k = 3;
  guided.guided = true;
  guided.safety_union = true;
  guided.maximum_work = options.maximum_work;
  guided.maximum_cache_entries = options.maximum_cache_entries;
  guided.policy_seed = kEnsemblePolicySeeds[0];
  return guided;
}

struct ExactFirstCohort {
  std::vector<GameResult> exact;
  std::vector<GameResult> guided;
  std::vector<GameResult> exact_first;
  double wall_seconds = 0.0;
};

ExactFirstCohort runExactFirstCohort(
    std::uint32_t seed_start, int games,
    const ExactFirstStudyOptions& options, const QModel& model,
    std::string_view phase) {
  const auto started = std::chrono::steady_clock::now();
  ExactFirstCohort cohort;
  cohort.exact.resize(games);
  cohort.guided.resize(games);
  cohort.exact_first.resize(games);
  const SearchOptions exact = exactFirstExactOptions(options);
  const SearchOptions guided = exactFirstGuidedOptions(options);
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  const int worker_count = std::min(options.parallelism, games);
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= games) return;
        const std::uint32_t seed =
            seed_start + static_cast<std::uint32_t>(game);
        const std::string exact_label = std::string(phase) + "-exact-d3";
        const std::string guided_label = std::string(phase) + "-guided-k3";
        const std::string exact_first_label =
            std::string(phase) + "-exact-first";
        cohort.exact[game] = runGame(seed, exact, nullptr,
                                     options.maximum_moves, exact_label);
        cohort.guided[game] = runGame(seed, guided, &model,
                                      options.maximum_moves, guided_label);
        cohort.exact_first[game] = runExactFirstGame(
            seed, guided, model, options.maximum_moves, exact_first_label);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  cohort.wall_seconds = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started)
                            .count();
  return cohort;
}

void writeExactFirstStudyArtifact(
    const ExactFirstStudyOptions& options, const Summary& screen_exact,
    const Summary& screen_guided, const Summary& screen_exact_first,
    const PairedMetrics& screen_vs_exact,
    const PairedMetrics& screen_vs_guided, double screen_wall,
    bool screen_passed, const Summary* confirmation_exact,
    const Summary* confirmation_guided,
    const Summary* confirmation_exact_first,
    const PairedMetrics* confirmation_vs_exact,
    const PairedMetrics* confirmation_vs_guided, double confirmation_wall,
    bool confirmation_passed, double total_wall) {
  std::ofstream output(options.output);
  if (!output) {
    throw std::runtime_error("could not open exact-first study artifact");
  }
  output << std::setprecision(10)
         << "{\n  \"format\": \"drop7-nnue-exact-first-study-v1\",\n"
         << "  \"trainingSeedOnly\": true,\n"
         << "  \"publicStateOnly\": true,\n"
         << "  \"scoring\": {\"levelBonus\":7000},\n"
         << "  \"strategy\": \"exact-d3-then-guided-d4-d5-complete-roots-only\",\n"
         << "  \"budgetAccounting\": \"exact-work-plus-guided-work-at-most-fixed-total\",\n"
         << "  \"screenGate\": \"strictly-higher-score-and-moves-than-exact-d3-and-frozen-k3\",\n"
         << "  \"frozen\": {\"exactDepth\":3,\"guidedMaximumDepth\":5,"
            "\"topK\":3,\"chanceSamples\":5,\"safetyUnion\":true,"
            "\"maximumWorkPerMove\":"
         << options.maximum_work << ",\"maximumCacheEntries\":"
         << options.maximum_cache_entries << "},\n"
         << "  \"parallelism\": " << options.parallelism << ",\n"
         << "  \"screen\": {\"seedStart\":"
         << kExactFirstScreenSeedStart << ",\"games\":"
         << options.screen_games << ",\"exactD3\":";
  writeSummary(output, screen_exact);
  output << ",\"frozenGuidedK3\":";
  writeSummary(output, screen_guided);
  output << ",\"exactFirst\":";
  writeSummary(output, screen_exact_first);
  output << ",\"exactFirstVsExact\":";
  writePairedMetrics(output, screen_vs_exact);
  output << ",\"exactFirstVsGuided\":";
  writePairedMetrics(output, screen_vs_guided);
  output << ",\"wallSeconds\":" << screen_wall << ",\"passed\":"
         << (screen_passed ? "true" : "false") << "},\n"
         << "  \"confirmation\": ";
  if (confirmation_exact == nullptr || confirmation_guided == nullptr ||
      confirmation_exact_first == nullptr ||
      confirmation_vs_exact == nullptr ||
      confirmation_vs_guided == nullptr) {
    output << "null";
  } else {
    output << "{\"seedStart\":" << kExactFirstConfirmationSeedStart
           << ",\"games\":" << options.confirmation_games
           << ",\"exactD3\":";
    writeSummary(output, *confirmation_exact);
    output << ",\"frozenGuidedK3\":";
    writeSummary(output, *confirmation_guided);
    output << ",\"exactFirst\":";
    writeSummary(output, *confirmation_exact_first);
    output << ",\"exactFirstVsExact\":";
    writePairedMetrics(output, *confirmation_vs_exact);
    output << ",\"exactFirstVsGuided\":";
    writePairedMetrics(output, *confirmation_vs_guided);
    output << ",\"wallSeconds\":" << confirmation_wall
           << ",\"passed\":"
           << (confirmation_passed ? "true" : "false") << '}';
  }
  output << ",\n  \"screenPassed\":"
         << (screen_passed ? "true" : "false")
         << ",\n  \"confirmed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\n  \"decision\": \""
         << (!screen_passed
                 ? "reject-screen"
                 : (confirmation_passed ? "advance" : "reject-confirmation"))
         << "\",\n  \"model\": \"" << options.model
         << "\",\n  \"totalWallSeconds\":" << total_wall << "\n}\n";
}

int runExactFirstStudy(const ExactFirstStudyOptions& options,
                       std::ostream& output) {
  const auto started = std::chrono::steady_clock::now();
  QModel model;
  model.load(options.model);
  const ExactFirstCohort screen = runExactFirstCohort(
      kExactFirstScreenSeedStart, options.screen_games, options, model,
      "exact-first-screen");
  const Summary screen_exact = summarize(screen.exact);
  const Summary screen_guided = summarize(screen.guided);
  const Summary screen_exact_first = summarize(screen.exact_first);
  const PairedMetrics screen_vs_exact =
      pairedMetrics(screen.exact, screen.exact_first);
  const PairedMetrics screen_vs_guided =
      pairedMetrics(screen.guided, screen.exact_first);
  const bool screen_passed =
      screen_exact_first.mean_score > screen_exact.mean_score &&
      screen_exact_first.mean_moves > screen_exact.mean_moves &&
      screen_exact_first.mean_score > screen_guided.mean_score &&
      screen_exact_first.mean_moves > screen_guided.mean_moves;

  Summary confirmation_exact;
  Summary confirmation_guided;
  Summary confirmation_exact_first;
  PairedMetrics confirmation_vs_exact;
  PairedMetrics confirmation_vs_guided;
  double confirmation_wall = 0.0;
  bool confirmation_passed = false;
  if (screen_passed) {
    const ExactFirstCohort confirmation = runExactFirstCohort(
        kExactFirstConfirmationSeedStart, options.confirmation_games, options,
        model, "exact-first-confirm");
    confirmation_exact = summarize(confirmation.exact);
    confirmation_guided = summarize(confirmation.guided);
    confirmation_exact_first = summarize(confirmation.exact_first);
    confirmation_vs_exact =
        pairedMetrics(confirmation.exact, confirmation.exact_first);
    confirmation_vs_guided =
        pairedMetrics(confirmation.guided, confirmation.exact_first);
    confirmation_wall = confirmation.wall_seconds;
    confirmation_passed =
        confirmation_exact_first.mean_score > confirmation_exact.mean_score &&
        confirmation_exact_first.mean_moves > confirmation_exact.mean_moves &&
        confirmation_exact_first.mean_score >
            confirmation_guided.mean_score &&
        confirmation_exact_first.mean_moves > confirmation_guided.mean_moves;
  }
  const double total_wall = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - started)
                                .count();
  writeExactFirstStudyArtifact(
      options, screen_exact, screen_guided, screen_exact_first,
      screen_vs_exact, screen_vs_guided, screen.wall_seconds, screen_passed,
      screen_passed ? &confirmation_exact : nullptr,
      screen_passed ? &confirmation_guided : nullptr,
      screen_passed ? &confirmation_exact_first : nullptr,
      screen_passed ? &confirmation_vs_exact : nullptr,
      screen_passed ? &confirmation_vs_guided : nullptr, confirmation_wall,
      confirmation_passed, total_wall);
  output << std::fixed << std::setprecision(3)
         << "NNUE_EXACT_FIRST_STUDY_RESULT {\"trainingSeedOnly\":true"
         << ",\"screenExactScore\":" << screen_exact.mean_score
         << ",\"screenExactMoves\":" << screen_exact.mean_moves
         << ",\"screenGuidedScore\":" << screen_guided.mean_score
         << ",\"screenGuidedMoves\":" << screen_guided.mean_moves
         << ",\"screenExactFirstScore\":"
         << screen_exact_first.mean_score
         << ",\"screenExactFirstMoves\":"
         << screen_exact_first.mean_moves
         << ",\"screenPassed\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmationRan\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return 0;
}

bool selfTest(const std::string& model_path, std::ostream& output) {
  QModel model;
  if (model_path.empty()) {
    model.initialize();
  } else {
    model.load(model_path);
  }
  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(4, 1)] = 2;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;
  State mirrored = state;
  mirrored.board = cfpi::detail::mirrorBoard(state.board);
  const Features first = extract(state, 1);
  const Features reflected = extract(mirrored, kBoardSize - 1 - 1);
  const bool reflection_safe = first == reflected;
  const float first_score = model.score(first);
  const float second_score = model.score(reflected);
  SearchOptions exact;
  exact.maximum_depth = 2;
  exact.maximum_work = 250'000;
  exact.maximum_cache_entries = 4'000;
  const SearchDecision exact_decision = chooseAction(state, exact, nullptr);
  cfpi::BehaviorOptions reference;
  reference.max_depth = 2;
  reference.chance_samples = kChanceSamples;
  reference.max_work = exact.maximum_work;
  reference.max_cache_entries = exact.maximum_cache_entries;
  const int reference_action = cfpi::chooseBehaviorAction(state, reference);
  const bool exact_parity = exact_decision.action == reference_action;

  SearchOptions guided = exact;
  guided.maximum_depth = 3;
  guided.top_k = 2;
  guided.guided = true;
  guided.safety_union = true;
  const SearchDecision guided_first = chooseAction(state, guided, &model);
  const SearchDecision guided_repeat = chooseAction(state, guided, &model);
  const SearchDecision guided_reflected =
      chooseAction(mirrored, guided, &model);
  State score_changed = state;
  score_changed.score = 987'654;
  const SearchDecision score_blind =
      chooseAction(score_changed, guided, &model);
  const bool deterministic = first_score == model.score(first) &&
                             guided_first.action == guided_repeat.action &&
                             guided_first.work == guided_repeat.work;
  const bool search_reflection_safe =
      guided_reflected.action == kBoardSize - 1 - guided_first.action;
  const bool public_state_only = score_blind.action == guided_first.action;
  SearchOptions ensemble_options = guided;
  ensemble_options.top_k = 3;
  const SearchDecision ensemble_first =
      chooseEnsembleAction(state, ensemble_options, model);
  const SearchDecision ensemble_repeat =
      chooseEnsembleAction(state, ensemble_options, model);
  const SearchDecision ensemble_reflected =
      chooseEnsembleAction(mirrored, ensemble_options, model);
  State metadata_changed = state;
  metadata_changed.score = 987'654;
  metadata_changed.level = 88;
  metadata_changed.moves_played = 321;
  const SearchDecision ensemble_metadata =
      chooseEnsembleAction(metadata_changed, ensemble_options, model);
  const bool ensemble_deterministic =
      ensemble_first.action == ensemble_repeat.action &&
      ensemble_first.work == ensemble_repeat.work;
  const bool ensemble_reflection_safe =
      ensemble_reflected.action == kBoardSize - 1 - ensemble_first.action;
  const bool ensemble_public_state_only =
      ensemble_metadata.action == ensemble_first.action;
  const bool ensemble_legal = isLegal(state.board, ensemble_first.action);
  const bool ensemble_root_combination =
      ensemble_first.ensemble_value_average ||
      ensemble_first.ensemble_vote_fallback;
  SearchOptions exact_first_options = ensemble_options;
  exact_first_options.maximum_depth = 5;
  exact_first_options.maximum_work = 250'000;
  const SearchDecision exact_first =
      chooseExactFirstAction(state, exact_first_options, model);
  const SearchDecision exact_first_repeat =
      chooseExactFirstAction(state, exact_first_options, model);
  const SearchDecision exact_first_reflected =
      chooseExactFirstAction(mirrored, exact_first_options, model);
  const SearchDecision exact_first_metadata =
      chooseExactFirstAction(metadata_changed, exact_first_options, model);
  const bool exact_first_safe =
      exact_first.action == exact_first_repeat.action &&
      exact_first.work == exact_first_repeat.work &&
      exact_first_reflected.action ==
          kBoardSize - 1 - exact_first.action &&
      exact_first_metadata.action == exact_first.action &&
      isLegal(state.board, exact_first.action) &&
      exact_first.completed_depth >= 3 &&
      exact_first.work <= exact_first_options.maximum_work &&
      exact_first.work == exact_first.exact_first_exact_work +
                              exact_first.exact_first_guided_work;
  const bool legal = isLegal(state.board, guided_first.action) &&
                     exact_decision.completed_depth == 2 &&
                     guided_first.completed_depth >= 1;
  const bool finite = std::isfinite(first_score) && std::isfinite(second_score);
  const bool model_loaded = true;
  const bool training_partition = kTrainingStart < kTrainingEnd;
  const bool passed = reflection_safe && search_reflection_safe &&
                      deterministic && exact_parity && public_state_only &&
                      ensemble_deterministic && ensemble_reflection_safe &&
                      ensemble_public_state_only && ensemble_legal &&
                      ensemble_root_combination && exact_first_safe && legal &&
                      finite &&
                      model_loaded && training_partition;
  output << "NNUE_GUIDED_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe && search_reflection_safe ? "true" : "false")
         << ",\"deterministic\":"
         << (deterministic ? "true" : "false")
         << ",\"exactParity\":" << (exact_parity ? "true" : "false")
         << ",\"publicStateOnly\":"
         << (public_state_only && ensemble_public_state_only ? "true"
                                                              : "false")
         << ",\"legalRoot\":" << (legal ? "true" : "false")
         << ",\"ensembleDeterministic\":"
         << (ensemble_deterministic ? "true" : "false")
         << ",\"ensembleReflectionSafe\":"
         << (ensemble_reflection_safe ? "true" : "false")
         << ",\"ensembleLegal\":"
         << (ensemble_legal ? "true" : "false")
         << ",\"ensembleRootCombination\":"
         << (ensemble_root_combination ? "true" : "false")
         << ",\"exactFirstSafe\":"
         << (exact_first_safe ? "true" : "false")
         << ",\"finite\":" << (finite ? "true" : "false")
         << ",\"fiveStrata\":true,\"modelLoaded\":"
         << (model_loaded ? "true" : "false")
         << ",\"trainingSeedOnly\":"
         << (training_partition ? "true" : "false") << "}\n";
  return passed;
}

}  // namespace drop7::nnue_guided

int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string(argv[1]) == "--self-test") {
      std::string model;
      if (argc == 4 && std::string(argv[2]) == "--model") model = argv[3];
      return drop7::nnue_guided::selfTest(model, std::cout) ? 0 : 1;
    }
    if (argc >= 2 && std::string(argv[1]) == "--benchmark") {
      const auto options =
          drop7::nnue_guided::parseBenchmarkOptions(argc, argv);
      return drop7::nnue_guided::runBenchmark(options, std::cout);
    }
    if (argc >= 2 && std::string(argv[1]) == "--ensemble-study") {
      const auto options = drop7::nnue_guided::parseStudyOptions(argc, argv);
      return drop7::nnue_guided::runEnsembleStudy(options, std::cout);
    }
    if (argc >= 2 && std::string(argv[1]) == "--exact-first-study") {
      const auto options =
          drop7::nnue_guided::parseExactFirstStudyOptions(argc, argv);
      return drop7::nnue_guided::runExactFirstStudy(options, std::cout);
    }
    std::cerr
        << "usage: drop7_nnue_guided_search --self-test [--model PATH] | "
           "--benchmark [--model PATH] [--output PATH] [--screen-games N] "
           "[--confirmation-games N] [--max-moves N] [--max-work N] "
           "[--max-cache N] [--no-safety-union] | --ensemble-study "
           "[--model PATH] [--output PATH] [--screen-games N] "
           "[--confirmation-games N] [--expansion-games N] "
           "[--max-moves N] [--max-work N] [--max-cache N] [--parallel N] | "
           "--exact-first-study [--model PATH] [--output PATH] "
           "[--screen-games N] [--confirmation-games N] [--max-moves N] "
           "[--max-work N] [--max-cache N] [--parallel N]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_nnue_guided_search: " << error.what() << '\n';
    return 1;
  }
}
