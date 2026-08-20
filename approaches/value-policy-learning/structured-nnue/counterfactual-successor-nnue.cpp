// Reuses the incremental structured-NNUE implementation without modifying its
// standalone entry point or source.
#define main drop7_structured_value_nnue_frozen_entrypoint
#include "structured-value-nnue.cpp"
#undef main

#include <fstream>
#include <future>
#include <list>
#include <unordered_map>
#include <unordered_set>

namespace drop7::counterfactual_successor {

namespace nnue = drop7::structured_value_nnue;

constexpr std::uint32_t kCollectionStart = 0x3e86'0000u;
constexpr int kTrainingGames = 12;
constexpr int kDevelopmentGames = 4;
constexpr int kOriginsPerGame = 4;
constexpr int kOriginSpacing = 15;
constexpr int kChanceStrata = 3;
constexpr int kContinuationRollouts = 8;
constexpr int kContinuationCap = 75;
constexpr int kPolicyMaximumMoves = 500;
constexpr int kScreenGames = 4;
constexpr int kConfirmationGames = 16;
constexpr int kParallelism = 4;
constexpr int kModelSearchDepth = 3;
constexpr std::uint64_t kModelSearchMaximumWork = 50'000;
constexpr std::size_t kModelSearchMaximumCache = 4'000;
constexpr std::uint32_t kCounterfactualSeed = 0x5355'4343u;
constexpr std::uint32_t kScreenStart = 0x3e87'0000u;
constexpr std::uint32_t kConfirmationStart = 0x3e88'0000u;
constexpr double kLifetimeScale =
    static_cast<double>(nnue::kMaximumLifetime) / kContinuationCap;

static_assert(kLevelBonus == 7'000);
static_assert((kCollectionStart >> 24) != 0x7du &&
              (kCollectionStart >> 24) != 0xd7u);
static_assert((kScreenStart >> 24) != 0x7du &&
              (kScreenStart >> 24) != 0xd7u);
static_assert((kConfirmationStart >> 24) != 0x7du &&
              (kConfirmationStart >> 24) != 0xd7u);

constexpr std::array<int, kBoardSize> kColumnOrder{{3, 2, 4, 1, 5, 0, 6}};

std::mutex experiment_progress_mutex;

std::uint64_t peakRssBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
#endif
}

nnue::PublicState canonicalPublic(const State& source) {
  bool ignored = false;
  return nnue::publicState(cfpi::detail::canonicalState(source, ignored));
}

nnue::PublicState canonicalPublic(const nnue::PublicState& source) {
  return canonicalPublic(nnue::materialize(source));
}

std::string stateKey(const nnue::PublicState& source) {
  const nnue::PublicState state = canonicalPublic(source);
  std::string result;
  result.reserve(kCellCount + 2);
  for (std::uint8_t cell : state.board) {
    result.push_back(static_cast<char>(cell));
  }
  result.push_back(static_cast<char>(state.next_disc));
  result.push_back(static_cast<char>(state.moves_remaining));
  return result;
}

std::uint32_t publicHash(const nnue::PublicState& source) {
  const nnue::PublicState state = canonicalPublic(source);
  std::uint32_t hash = 0x811c'9dc5u;
  for (std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint32_t>(cell + 1u);
    hash *= 0x0100'0193u;
  }
  hash ^= state.next_disc;
  hash *= 0x0100'0193u;
  hash ^= state.moves_remaining;
  return mix32(hash);
}

std::uint32_t safeContinuationSeed(const nnue::PublicState& state,
                                   int rollout) {
  std::uint32_t seed = mix32(
      publicHash(state) ^ 0x434f'4e54u ^
      (static_cast<std::uint32_t>(rollout + 1) * 0x9e37'79b9u));
  const std::uint32_t family = seed >> 24;
  if (family == 0x7du || family == 0xd7u) seed ^= 0x4000'0000u;
  return seed;
}

struct RollInGame {
  std::uint32_t seed = 0;
  std::vector<State> origins;
  std::uint64_t exact_work = 0;
};

RollInGame collectRollIn(std::uint32_t seed) {
  RollInGame result;
  result.seed = seed;
  State state = initialHeadlessState(seed);
  while (!state.game_over &&
         static_cast<int>(result.origins.size()) < kOriginsPerGame) {
    if (state.moves_played > 0 &&
        state.moves_played % kOriginSpacing == 0) {
      State public_only = state;
      public_only.score = 0;
      public_only.level = 1;
      public_only.moves_played = 0;
      result.origins.push_back(public_only);
    }
    cfpi::BehaviorMetrics metrics;
    const int action = cfpi::chooseBehaviorAction(state, {}, &metrics);
    if (metrics.completed_depth != 3 || !metrics.complete) {
      throw std::runtime_error("exact-d3 roll-in failed to complete");
    }
    result.exact_work += metrics.work;
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("exact-d3 roll-in transition failed");
    }
  }
  return result;
}

std::vector<RollInGame> collectRollIns() {
  constexpr int games = kTrainingGames + kDevelopmentGames;
  std::vector<RollInGame> result(games);
  std::atomic<int> next{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next.fetch_add(1);
        if (game >= games) return;
        result[static_cast<std::size_t>(game)] = collectRollIn(
            kCollectionStart + static_cast<std::uint32_t>(game));
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

struct Branch {
  int state_index = -1;
  bool terminal = false;
};

struct ActionBranches {
  int action = -1;
  std::array<Branch, kChanceStrata> branches{};
};

struct OriginGroup {
  std::uint32_t origin_game = 0;
  nnue::PublicState root{};
  std::vector<ActionBranches> actions;
};

struct UniqueSuccessor {
  nnue::PublicState state{};
  std::string key;
  std::uint32_t origin_game = 0;
  nnue::Label label{};
};

struct Dataset {
  std::vector<UniqueSuccessor> states;
  std::vector<OriginGroup> groups;
  std::unordered_map<std::string, int> index;
  std::uint64_t raw_branches = 0;
  std::uint64_t deduplicated_branches = 0;
};

struct EnumeratedSuccessor {
  nnue::PublicState state{};
  bool terminal = false;
};

EnumeratedSuccessor successor(const State& canonical, int action,
                              int sample) {
  if (!isLegal(canonical.board, action) || sample < 0 ||
      sample >= kChanceStrata) {
    throw std::invalid_argument("invalid counterfactual successor request");
  }
  const std::uint32_t seed = cfpi::detail::scenarioSeedForState(
      canonical, kCounterfactualSeed, 1);
  cfpi::detail::StratifiedRandom random{
      seed, sample, kChanceStrata, 0,
  };
  MoveResult move;
  if (!cfpi::detail::playMoveSampled(canonical, action, random, move)) {
    throw std::runtime_error("counterfactual successor transition failed");
  }
  if (!move.state.game_over) {
    move.state.next_disc =
        cfpi::detail::sampledNextDisc(seed, sample, kChanceStrata);
  }
  return {canonicalPublic(move.state), move.state.game_over};
}

int observeState(Dataset& dataset, const nnue::PublicState& source,
                 std::uint32_t origin_game) {
  const nnue::PublicState state = canonicalPublic(source);
  const std::string key = stateKey(state);
  const auto found = dataset.index.find(key);
  if (found != dataset.index.end()) {
    ++dataset.deduplicated_branches;
    return found->second;
  }
  const int index = static_cast<int>(dataset.states.size());
  dataset.states.push_back({state, key, origin_game, {}});
  dataset.index.emplace(dataset.states.back().key, index);
  return index;
}

void enumerateOrigin(Dataset& dataset, const State& source,
                     std::uint32_t origin_game) {
  bool ignored = false;
  const State canonical = cfpi::detail::canonicalState(source, ignored);
  OriginGroup group;
  group.origin_game = origin_game;
  group.root = nnue::publicState(canonical);
  for (int action : kColumnOrder) {
    if (!isLegal(canonical.board, action)) continue;
    ActionBranches action_branches;
    action_branches.action = action;
    for (int sample = 0; sample < kChanceStrata; ++sample) {
      const EnumeratedSuccessor next = successor(canonical, action, sample);
      ++dataset.raw_branches;
      Branch branch;
      branch.terminal = next.terminal;
      if (!branch.terminal) {
        branch.state_index = observeState(dataset, next.state, origin_game);
      }
      action_branches.branches[sample] = branch;
    }
    group.actions.push_back(action_branches);
  }
  if (group.actions.size() >= 2) dataset.groups.push_back(std::move(group));
}

Dataset buildDataset(const std::vector<RollInGame>& games,
                     int begin, int end) {
  Dataset result;
  for (int game = begin; game < end; ++game) {
    for (const State& origin : games[static_cast<std::size_t>(game)].origins) {
      enumerateOrigin(result, origin,
                      games[static_cast<std::size_t>(game)].seed);
    }
  }
  return result;
}

struct PurgeStats {
  int overlapping_states = 0;
  int dropped_groups = 0;
};

PurgeStats purgeDevelopmentOverlap(const Dataset& training,
                                   Dataset& development) {
  std::unordered_set<std::string> training_keys;
  training_keys.reserve(training.states.size() * 2);
  for (const UniqueSuccessor& state : training.states) {
    training_keys.insert(state.key);
  }
  std::vector<int> remap(development.states.size(), -1);
  std::vector<UniqueSuccessor> kept;
  kept.reserve(development.states.size());
  PurgeStats stats;
  for (std::size_t index = 0; index < development.states.size(); ++index) {
    if (training_keys.contains(development.states[index].key)) {
      ++stats.overlapping_states;
      continue;
    }
    remap[index] = static_cast<int>(kept.size());
    kept.push_back(std::move(development.states[index]));
  }
  std::vector<OriginGroup> groups;
  groups.reserve(development.groups.size());
  for (OriginGroup group : development.groups) {
    bool overlaps = false;
    for (ActionBranches& action : group.actions) {
      for (Branch& branch : action.branches) {
        if (branch.terminal) continue;
        const int mapped = remap[static_cast<std::size_t>(branch.state_index)];
        if (mapped < 0) {
          overlaps = true;
          break;
        }
        branch.state_index = mapped;
      }
      if (overlaps) break;
    }
    if (overlaps) {
      ++stats.dropped_groups;
    } else {
      groups.push_back(std::move(group));
    }
  }
  development.states = std::move(kept);
  development.groups = std::move(groups);
  development.index.clear();
  for (std::size_t index = 0; index < development.states.size(); ++index) {
    development.index.emplace(development.states[index].key,
                              static_cast<int>(index));
  }
  return stats;
}

nnue::Label continuationLabel(const nnue::PublicState& public_state) {
  double lifetime = 0.0;
  double survival25 = 0.0;
  double survival50 = 0.0;
  for (int rollout = 0; rollout < kContinuationRollouts; ++rollout) {
    State state = nnue::materialize(public_state);
    state.score = 0;
    state.level = 1;
    state.moves_played = 0;
    const std::uint32_t seed = safeContinuationSeed(public_state, rollout);
    int moves = 0;
    while (!state.game_over && moves < kContinuationCap) {
      const int action = cfpi::choosePhaseGreedyAction(state, 1);
      if (!isLegal(state.board, action)) {
        throw std::runtime_error("continuation policy selected illegal action");
      }
      MoveResult move;
      if (!playHeadlessMove(state, seed, action, move)) {
        throw std::runtime_error("continuation transition failed");
      }
      ++moves;
    }
    lifetime += moves;
    survival25 += moves >= 25;
    survival50 += moves >= 50;
  }
  lifetime /= kContinuationRollouts;
  survival25 /= kContinuationRollouts;
  survival50 /= kContinuationRollouts;
  return {canonicalPublic(public_state),
          static_cast<float>(lifetime * kLifetimeScale),
          static_cast<float>(survival25),
          static_cast<float>(survival50)};
}

void labelDataset(Dataset& dataset, std::string_view label) {
  std::atomic<int> next{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0;
       worker < std::min<int>(kParallelism, dataset.states.size()); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int index = next.fetch_add(1);
        if (index >= static_cast<int>(dataset.states.size())) return;
        dataset.states[static_cast<std::size_t>(index)].label =
            continuationLabel(
                dataset.states[static_cast<std::size_t>(index)].state);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  const std::lock_guard<std::mutex> lock(experiment_progress_mutex);
  std::cerr << label << " labeled " << dataset.states.size()
            << " unique successors\n";
}

std::vector<nnue::Label> labels(const Dataset& dataset) {
  std::vector<nnue::Label> result;
  result.reserve(dataset.states.size());
  for (const UniqueSuccessor& state : dataset.states) {
    result.push_back(state.label);
  }
  return result;
}

double predictedLifetime(const nnue::Network& network,
                         const nnue::Normalizer& normalizer,
                         const nnue::Calibrator& calibrator,
                         const nnue::PublicState& state) {
  return nnue::predict(network, normalizer, calibrator,
                       canonicalPublic(state))
             .lifetime /
         kLifetimeScale;
}

double labeledLifetime(const UniqueSuccessor& state) {
  return state.label.lifetime / kLifetimeScale;
}

struct ValueBundle {
  const nnue::Network& network;
  const nnue::Normalizer& normalizer;
  const nnue::Calibrator& calibrator;
};

struct ModelSearchContext {
  explicit ModelSearchContext(const ValueBundle& value_bundle)
      : values(value_bundle) {}

  const ValueBundle& values;
  std::unordered_map<std::string, double> cache;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
};

double modelFutureValue(const State& state, int depth,
                        ModelSearchContext& context);

double modelActionValue(const State& state, int action, int depth,
                        ModelSearchContext& context) {
  const std::uint32_t seed = cfpi::detail::scenarioSeedForState(
      state, kCounterfactualSeed, 1);
  double value = 0.0;
  for (int sample = 0; sample < kChanceStrata; ++sample) {
    if (context.work >= kModelSearchMaximumWork) {
      throw std::runtime_error("model search work bound exceeded");
    }
    cfpi::detail::StratifiedRandom random{
        seed, sample, kChanceStrata, 0,
    };
    MoveResult move;
    if (!cfpi::detail::playMoveSampled(state, action, random, move)) {
      continue;
    }
    ++context.work;
    if (move.state.game_over) {
      value += 1.0;
      continue;
    }
    move.state.next_disc =
        cfpi::detail::sampledNextDisc(seed, sample, kChanceStrata);
    bool ignored = false;
    const State next = cfpi::detail::canonicalState(move.state, ignored);
    value += 1.0 + modelFutureValue(next, depth - 1, context);
  }
  return value / kChanceStrata;
}

double modelFutureValue(const State& state, int depth,
                        ModelSearchContext& context) {
  ++context.nodes;
  if (depth == 0) {
    ++context.work;
    return predictedLifetime(context.values.network,
                             context.values.normalizer,
                             context.values.calibrator,
                             nnue::publicState(state));
  }
  const std::string key = cfpi::detail::dynamicStateKey(state, depth);
  const auto found = context.cache.find(key);
  if (found != context.cache.end()) return found->second;
  double best = -std::numeric_limits<double>::infinity();
  for (int action : kColumnOrder) {
    if (!isLegal(state.board, action)) continue;
    best = std::max(best,
                    modelActionValue(state, action, depth, context));
  }
  if (!std::isfinite(best)) best = 0.0;
  if (context.cache.size() >= kModelSearchMaximumCache) {
    context.cache.clear();
  }
  context.cache.emplace(key, best);
  return best;
}

struct ModelDecision {
  int action = -1;
  std::array<double, kBoardSize> canonical_values{};
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::size_t cache_entries = 0;
};

ModelDecision chooseLeafAction(const State& source,
                               const ValueBundle& values) {
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  ModelSearchContext context(values);
  ModelDecision result;
  result.canonical_values.fill(-std::numeric_limits<double>::infinity());
  double best = -std::numeric_limits<double>::infinity();
  int canonical_action = -1;
  for (int action : kColumnOrder) {
    if (!isLegal(canonical.board, action)) continue;
    const double value =
        modelActionValue(canonical, action, kModelSearchDepth, context);
    result.canonical_values[action] = value;
    if (value > best) {
      best = value;
      canonical_action = action;
    }
  }
  result.action = mirrored && canonical_action >= 0
                      ? kBoardSize - 1 - canonical_action
                      : canonical_action;
  result.work = context.work;
  result.nodes = context.nodes;
  result.cache_entries = context.cache.size();
  return result;
}

ModelDecision chooseDirectAction(const State& source,
                                 const ValueBundle& values) {
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  ModelDecision result;
  result.canonical_values.fill(-std::numeric_limits<double>::infinity());
  double best = -std::numeric_limits<double>::infinity();
  int canonical_action = -1;
  for (int action : kColumnOrder) {
    if (!isLegal(canonical.board, action)) continue;
    double value = 0.0;
    for (int sample = 0; sample < kChanceStrata; ++sample) {
      const EnumeratedSuccessor next = successor(canonical, action, sample);
      ++result.work;
      value += next.terminal
                   ? 1.0
                   : 1.0 + predictedLifetime(
                               values.network, values.normalizer,
                               values.calibrator, next.state);
    }
    value /= kChanceStrata;
    result.canonical_values[action] = value;
    if (value > best) {
      best = value;
      canonical_action = action;
    }
  }
  result.action = mirrored && canonical_action >= 0
                      ? kBoardSize - 1 - canonical_action
                      : canonical_action;
  return result;
}

enum class DeploymentMode { kDirect, kLeaf };

struct PredictionMetrics {
  int examples = 0;
  double spearman = 0.0;
  double mae = 0.0;
  double mean_label = 0.0;
  double mean_prediction = 0.0;
};

PredictionMetrics predictionMetrics(const Dataset& dataset,
                                    const ValueBundle& values) {
  PredictionMetrics result;
  result.examples = static_cast<int>(dataset.states.size());
  std::vector<double> predictions;
  std::vector<double> targets;
  predictions.reserve(dataset.states.size());
  targets.reserve(dataset.states.size());
  for (const UniqueSuccessor& state : dataset.states) {
    const double prediction = predictedLifetime(
        values.network, values.normalizer, values.calibrator, state.state);
    const double target = labeledLifetime(state);
    predictions.push_back(prediction);
    targets.push_back(target);
    result.mae += std::abs(prediction - target);
    result.mean_label += target;
    result.mean_prediction += prediction;
  }
  if (!predictions.empty()) {
    result.spearman = nnue::spearman(predictions, targets);
    result.mae /= predictions.size();
    result.mean_label /= predictions.size();
    result.mean_prediction /= predictions.size();
  }
  return result;
}

struct ActionMetrics {
  int groups = 0;
  int pairs = 0;
  double top1_accuracy = 0.0;
  double pairwise_accuracy = 0.0;
  double mean_regret = 0.0;
  double mean_spearman = 0.0;
};

int bestAction(const std::vector<int>& actions,
               const std::vector<double>& values) {
  int result = -1;
  double best = -std::numeric_limits<double>::infinity();
  for (int preferred : kColumnOrder) {
    const auto found =
        std::find(actions.begin(), actions.end(), preferred);
    if (found == actions.end()) continue;
    const std::size_t index =
        static_cast<std::size_t>(found - actions.begin());
    if (values[index] > best) {
      best = values[index];
      result = preferred;
    }
  }
  return result;
}

ActionMetrics actionMetrics(const Dataset& dataset,
                            const ValueBundle& bundle,
                            DeploymentMode mode) {
  ActionMetrics result;
  for (const OriginGroup& group : dataset.groups) {
    std::vector<int> actions;
    std::vector<double> targets;
    std::vector<double> predictions;
    ModelDecision leaf;
    if (mode == DeploymentMode::kLeaf) {
      leaf = chooseLeafAction(nnue::materialize(group.root), bundle);
    }
    for (const ActionBranches& action : group.actions) {
      double target = 0.0;
      double prediction = 0.0;
      for (const Branch& branch : action.branches) {
        if (branch.terminal) {
          target += 1.0;
          prediction += 1.0;
        } else {
          const UniqueSuccessor& state =
              dataset.states[static_cast<std::size_t>(branch.state_index)];
          target += 1.0 + labeledLifetime(state);
          prediction += 1.0 + predictedLifetime(
                                      bundle.network, bundle.normalizer,
                                      bundle.calibrator, state.state);
        }
      }
      actions.push_back(action.action);
      targets.push_back(target / kChanceStrata);
      predictions.push_back(
          mode == DeploymentMode::kLeaf
              ? leaf.canonical_values[action.action]
              : prediction / kChanceStrata);
    }
    if (actions.size() < 2) continue;
    const int target_action = bestAction(actions, targets);
    const int predicted_action = bestAction(actions, predictions);
    const std::size_t target_index = static_cast<std::size_t>(
        std::find(actions.begin(), actions.end(), target_action) -
        actions.begin());
    const std::size_t predicted_index = static_cast<std::size_t>(
        std::find(actions.begin(), actions.end(), predicted_action) -
        actions.begin());
    result.top1_accuracy += predicted_action == target_action;
    result.mean_regret +=
        targets[target_index] - targets[predicted_index];
    result.mean_spearman += nnue::spearman(predictions, targets);
    for (std::size_t first = 0; first < actions.size(); ++first) {
      for (std::size_t second = first + 1; second < actions.size(); ++second) {
        if (targets[first] == targets[second]) continue;
        const double target_difference = targets[first] - targets[second];
        const double prediction_difference =
            predictions[first] - predictions[second];
        result.pairwise_accuracy +=
            target_difference * prediction_difference > 0.0;
        ++result.pairs;
      }
    }
    ++result.groups;
  }
  if (result.groups > 0) {
    result.top1_accuracy /= result.groups;
    result.mean_regret /= result.groups;
    result.mean_spearman /= result.groups;
  }
  if (result.pairs > 0) result.pairwise_accuracy /= result.pairs;
  return result;
}

DeploymentMode chooseDeployment(const ActionMetrics& direct,
                                const ActionMetrics& leaf) {
  if (leaf.top1_accuracy > direct.top1_accuracy) {
    return DeploymentMode::kLeaf;
  }
  if (leaf.top1_accuracy == direct.top1_accuracy &&
      leaf.mean_regret < direct.mean_regret) {
    return DeploymentMode::kLeaf;
  }
  return DeploymentMode::kDirect;
}

struct GameResult {
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::size_t peak_cache_entries = 0;
  std::uint64_t peak_rss_bytes = 0;
  double elapsed_seconds = 0.0;
};

GameResult runPolicyGame(std::uint32_t seed, const ValueBundle& values,
                         DeploymentMode mode, bool baseline,
                         std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  GameResult result;
  while (!state.game_over && state.moves_played < kPolicyMaximumMoves) {
    int action = -1;
    if (baseline) {
      cfpi::BehaviorMetrics metrics;
      action = cfpi::chooseBehaviorAction(state, {}, &metrics);
      if (metrics.completed_depth != 3 || !metrics.complete) {
        throw std::runtime_error("screen exact-d3 failed to complete");
      }
      result.work += metrics.work;
      result.nodes += metrics.nodes;
      result.peak_cache_entries =
          std::max(result.peak_cache_entries, metrics.cache_entries);
    } else {
      const ModelDecision decision =
          mode == DeploymentMode::kDirect
              ? chooseDirectAction(state, values)
              : chooseLeafAction(state, values);
      action = decision.action;
      result.work += decision.work;
      result.nodes += decision.nodes;
      result.peak_cache_entries =
          std::max(result.peak_cache_entries, decision.cache_entries);
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("successor policy selected illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("successor policy transition failed");
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
    const std::lock_guard<std::mutex> lock(experiment_progress_mutex);
    std::cerr << label << " seed 0x" << std::hex << seed << std::dec << ' '
              << result.score << '/' << result.moves << " work "
              << result.work << '\n';
  }
  return result;
}

struct PolicyCohort {
  std::vector<GameResult> baseline;
  std::vector<GameResult> candidate;
};

PolicyCohort runPolicyCohort(std::uint32_t seed_start, int games,
                             const ValueBundle& values,
                             DeploymentMode mode,
                             std::string_view phase) {
  PolicyCohort result;
  result.baseline.resize(static_cast<std::size_t>(games));
  result.candidate.resize(static_cast<std::size_t>(games));
  std::atomic<int> next{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next.fetch_add(1);
        if (game >= games) return;
        const std::uint32_t seed =
            seed_start + static_cast<std::uint32_t>(game);
        result.baseline[static_cast<std::size_t>(game)] = runPolicyGame(
            seed, values, mode, true, std::string(phase) + "-exact-d3");
        result.candidate[static_cast<std::size_t>(game)] = runPolicyGame(
            seed, values, mode, false,
            std::string(phase) +
                (mode == DeploymentMode::kDirect ? "-direct" : "-leaf-d3"));
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

struct PolicySummary {
  int games = 0;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double work_per_move = 0.0;
  double moves_per_game_second = 0.0;
  double work_per_game_second = 0.0;
  double aggregate_game_seconds = 0.0;
  int censored = 0;
  std::size_t peak_cache_entries = 0;
  std::uint64_t peak_rss_bytes = 0;
};

PolicySummary summarizePolicy(const std::vector<GameResult>& games) {
  if (games.empty()) throw std::invalid_argument("empty policy cohort");
  PolicySummary result;
  result.games = static_cast<int>(games.size());
  std::uint64_t moves = 0;
  std::uint64_t work = 0;
  for (const GameResult& game : games) {
    result.mean_score += static_cast<double>(game.score) / games.size();
    result.mean_moves += static_cast<double>(game.moves) / games.size();
    moves += game.moves;
    work += game.work;
    result.aggregate_game_seconds += game.elapsed_seconds;
    result.censored += game.censored;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, game.peak_cache_entries);
    result.peak_rss_bytes =
        std::max(result.peak_rss_bytes, game.peak_rss_bytes);
  }
  const double move_count =
      static_cast<double>(std::max<std::uint64_t>(1, moves));
  result.work_per_move = work / move_count;
  result.moves_per_game_second =
      move_count / std::max(1.0e-9, result.aggregate_game_seconds);
  result.work_per_game_second =
      work / std::max(1.0e-9, result.aggregate_game_seconds);
  return result;
}

struct PairedSummary {
  double mean_score_difference = 0.0;
  double mean_move_difference = 0.0;
  int wins = 0;
  int ties = 0;
  int losses = 0;
};

PairedSummary pairedPolicy(const PolicyCohort& cohort) {
  PairedSummary result;
  for (std::size_t game = 0; game < cohort.baseline.size(); ++game) {
    const GameResult& baseline = cohort.baseline[game];
    const GameResult& candidate = cohort.candidate[game];
    result.mean_score_difference +=
        static_cast<double>(candidate.score - baseline.score) /
        cohort.baseline.size();
    result.mean_move_difference +=
        static_cast<double>(candidate.moves - baseline.moves) /
        cohort.baseline.size();
    if (candidate.score > baseline.score) {
      ++result.wins;
    } else if (candidate.score < baseline.score) {
      ++result.losses;
    } else {
      ++result.ties;
    }
  }
  return result;
}

void saveModel(const std::string& path, const nnue::Network& network,
               const nnue::Normalizer& normalizer,
               const nnue::Calibrator& calibrator) {
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("could not open successor model");
  const std::array<std::uint32_t, 4> header{{
      0x4353'4e4eu, nnue::kCategoryCount, nnue::kHidden1, nnue::kHidden2,
  }};
  const auto write = [&output](const auto& values) {
    output.write(reinterpret_cast<const char*>(values.data()),
                 static_cast<std::streamsize>(values.size() *
                                              sizeof(values[0])));
  };
  write(header);
  write(normalizer.mean);
  write(normalizer.scale);
  write(network.parameters.embedding);
  write(network.parameters.metric_weight);
  write(network.parameters.bias1);
  write(network.parameters.weight2);
  write(network.parameters.bias2);
  write(network.parameters.output_weight);
  write(network.parameters.output_bias);
  output.write(reinterpret_cast<const char*>(&calibrator),
               sizeof(calibrator));
  if (!output) throw std::runtime_error("could not write successor model");
}

void writePredictionMetrics(std::ostream& output,
                            const PredictionMetrics& result) {
  output << "{\"examples\":" << result.examples
         << ",\"spearman\":" << result.spearman
         << ",\"mae\":" << result.mae
         << ",\"meanLabel\":" << result.mean_label
         << ",\"meanPrediction\":" << result.mean_prediction << '}';
}

void writeActionMetrics(std::ostream& output,
                        const ActionMetrics& result) {
  output << "{\"groups\":" << result.groups
         << ",\"pairs\":" << result.pairs
         << ",\"top1Accuracy\":" << result.top1_accuracy
         << ",\"pairwiseAccuracy\":" << result.pairwise_accuracy
         << ",\"meanRegret\":" << result.mean_regret
         << ",\"meanWithinStateSpearman\":" << result.mean_spearman
         << '}';
}

void writePolicySummary(std::ostream& output,
                        const PolicySummary& result) {
  output << "{\"games\":" << result.games
         << ",\"meanScore\":" << result.mean_score
         << ",\"meanMoves\":" << result.mean_moves
         << ",\"workPerMove\":" << result.work_per_move
         << ",\"movesPerGameSecond\":" << result.moves_per_game_second
         << ",\"workPerGameSecond\":" << result.work_per_game_second
         << ",\"aggregateGameSeconds\":"
         << result.aggregate_game_seconds
         << ",\"peakCacheEntries\":" << result.peak_cache_entries
         << ",\"peakRssBytes\":" << result.peak_rss_bytes
         << ",\"censored\":" << result.censored << '}';
}

void writePaired(std::ostream& output, const PairedSummary& result) {
  output << "{\"meanScoreDifference\":" << result.mean_score_difference
         << ",\"meanMoveDifference\":" << result.mean_move_difference
         << ",\"wins\":" << result.wins << ",\"ties\":"
         << result.ties << ",\"losses\":" << result.losses << '}';
}

void writeTrajectories(std::ostream& output,
                       const std::vector<GameResult>& games) {
  output << "{\"scores\":[";
  for (std::size_t game = 0; game < games.size(); ++game) {
    if (game > 0) output << ',';
    output << games[game].score;
  }
  output << "],\"moves\":[";
  for (std::size_t game = 0; game < games.size(); ++game) {
    if (game > 0) output << ',';
    output << games[game].moves;
  }
  output << "]}";
}

bool selfTest(std::ostream& output) {
  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(4, 1)] = 2;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;
  bool ignored = false;
  const State canonical = cfpi::detail::canonicalState(state, ignored);
  int legal_count = 0;
  const auto legal_actions = legalColumns(canonical.board, legal_count);
  bool shared_strata = legal_count > 1;
  for (int sample = 0; sample < kChanceStrata; ++sample) {
    const EnumeratedSuccessor first =
        successor(canonical, legal_actions[0], sample);
    const EnumeratedSuccessor repeat =
        successor(canonical, legal_actions[0], sample);
    const EnumeratedSuccessor other =
        successor(canonical, legal_actions[1], sample);
    shared_strata = shared_strata && first.terminal == repeat.terminal &&
                     first.state.board == repeat.state.board &&
                     first.state.next_disc == repeat.state.next_disc &&
                     (first.terminal || other.terminal ||
                      first.state.next_disc == other.state.next_disc);
  }
  const nnue::PublicState public_state = canonicalPublic(state);
  const nnue::PublicState reflected = nnue::mirror(public_state);
  nnue::Network network;
  std::vector<nnue::Label> test_labels{{
      public_state, 100.0f, 1.0f, 0.0f,
  }};
  const nnue::Normalizer normalizer = nnue::fitNormalizer(test_labels);
  const nnue::Calibrator calibrator;
  const ValueBundle values{network, normalizer, calibrator};
  const double first_prediction = predictedLifetime(
      network, normalizer, calibrator, public_state);
  const double reflected_prediction = predictedLifetime(
      network, normalizer, calibrator, reflected);
  const ModelDecision direct = chooseDirectAction(state, values);
  State mirrored_state = state;
  mirrored_state.board = cfpi::detail::mirrorBoard(state.board);
  const ModelDecision direct_reflected =
      chooseDirectAction(mirrored_state, values);
  const ModelDecision leaf = chooseLeafAction(state, values);
  const ModelDecision leaf_repeat = chooseLeafAction(state, values);
  const ModelDecision leaf_reflected =
      chooseLeafAction(mirrored_state, values);
  const bool reflection_safe = first_prediction == reflected_prediction &&
                               direct_reflected.action ==
                                   kBoardSize - 1 - direct.action &&
                               leaf_reflected.action ==
                                   kBoardSize - 1 - leaf.action;
  const bool leaf_deterministic =
      leaf.action == leaf_repeat.action && leaf.work == leaf_repeat.work;
  const bool continuation_public =
      safeContinuationSeed(public_state, 0) ==
          safeContinuationSeed(reflected, 0) &&
      (safeContinuationSeed(public_state, 0) >> 24) != 0x7du &&
      (safeContinuationSeed(public_state, 0) >> 24) != 0xd7u;
  Dataset training;
  observeState(training, public_state, kCollectionStart);
  Dataset development;
  observeState(development, reflected,
               kCollectionStart + kTrainingGames);
  const PurgeStats purge =
      purgeDevelopmentOverlap(training, development);
  const bool overlap_purged = purge.overlapping_states == 1 &&
                              development.states.empty();
  const bool legal = isLegal(state.board, direct.action);
  const bool bounded = direct.work <= kBoardSize * kChanceStrata &&
                       leaf.work <= kModelSearchMaximumWork &&
                       leaf.cache_entries <= kModelSearchMaximumCache;
  const bool passed = shared_strata && reflection_safe &&
                      leaf_deterministic && continuation_public &&
                      overlap_purged && legal && bounded &&
                      kLevelBonus == 7'000;
  output << "COUNTERFACTUAL_SUCCESSOR_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"sharedThreeStrata\":"
         << (shared_strata ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe ? "true" : "false")
         << ",\"leafDeterministic\":"
         << (leaf_deterministic ? "true" : "false")
         << ",\"continuationPublicOnly\":"
         << (continuation_public ? "true" : "false")
         << ",\"overlapPurged\":"
         << (overlap_purged ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false")
         << ",\"bounded\":" << (bounded ? "true" : "false")
         << ",\"levelBonus\":" << kLevelBonus << "}\n";
  return passed;
}

struct ProgramOptions {
  std::string artifact =
      "/tmp/drop7-counterfactual-successor-nnue.json";
  std::string model =
      "/tmp/drop7-counterfactual-successor-nnue.bin";
};

ProgramOptions parseOptions(int argc, char** argv, int first_argument) {
  ProgramOptions options;
  for (int index = first_argument; index < argc; ++index) {
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing option value");
    }
    const std::string argument = argv[index++];
    if (argument == "--artifact") {
      options.artifact = argv[index];
    } else if (argument == "--model") {
      options.model = argv[index];
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  return options;
}

int run(const ProgramOptions& options, std::ostream& output) {
  const auto started = std::chrono::steady_clock::now();
  const std::vector<RollInGame> rollins = collectRollIns();
  Dataset training = buildDataset(rollins, 0, kTrainingGames);
  Dataset development = buildDataset(
      rollins, kTrainingGames, kTrainingGames + kDevelopmentGames);
  const PurgeStats purge = purgeDevelopmentOverlap(training, development);
  if (training.states.empty() || development.states.empty() ||
      development.groups.empty()) {
    throw std::runtime_error("counterfactual dataset is empty after purge");
  }
  labelDataset(training, "training");
  labelDataset(development, "development");

  const std::vector<nnue::Label> training_labels = labels(training);
  const nnue::Normalizer normalizer = nnue::fitNormalizer(training_labels);
  const std::vector<nnue::Example> training_examples =
      nnue::prepare(training_labels, normalizer);
  nnue::Network network;
  const double training_loss = nnue::train(network, training_examples);
  const nnue::Calibrator calibrator =
      nnue::fitCalibrator(network, training_examples);
  const ValueBundle values{network, normalizer, calibrator};
  const PredictionMetrics training_predictions =
      predictionMetrics(training, values);
  const PredictionMetrics development_predictions =
      predictionMetrics(development, values);
  const ActionMetrics direct =
      actionMetrics(development, values, DeploymentMode::kDirect);
  const ActionMetrics leaf =
      actionMetrics(development, values, DeploymentMode::kLeaf);
  const DeploymentMode deployment = chooseDeployment(direct, leaf);
  saveModel(options.model, network, normalizer, calibrator);

  const PolicyCohort screen = runPolicyCohort(
      kScreenStart, kScreenGames, values, deployment, "screen");
  const PolicySummary screen_baseline =
      summarizePolicy(screen.baseline);
  const PolicySummary screen_candidate =
      summarizePolicy(screen.candidate);
  const PairedSummary screen_paired = pairedPolicy(screen);
  const bool screen_passed = screen_paired.mean_score_difference > 0.0 &&
                             screen_paired.mean_move_difference > 0.0;

  PolicyCohort confirmation;
  PolicySummary confirmation_baseline;
  PolicySummary confirmation_candidate;
  PairedSummary confirmation_paired;
  bool confirmed = false;
  if (screen_passed) {
    confirmation = runPolicyCohort(
        kConfirmationStart, kConfirmationGames, values, deployment,
        "confirmation");
    confirmation_baseline = summarizePolicy(confirmation.baseline);
    confirmation_candidate = summarizePolicy(confirmation.candidate);
    confirmation_paired = pairedPolicy(confirmation);
    confirmed = confirmation_paired.mean_score_difference > 0.0 &&
                confirmation_paired.mean_move_difference > 0.0;
  }
  const double elapsed_seconds = std::chrono::duration<double>(
                                     std::chrono::steady_clock::now() -
                                     started)
                                     .count();
  std::ofstream artifact(options.artifact);
  if (!artifact) {
    throw std::runtime_error("could not open successor artifact");
  }
  artifact << std::setprecision(10)
           << "{\n  \"format\": \"drop7-counterfactual-successor-nnue-v1\",\n"
           << "  \"trainingSeedOnly\": true,\n"
           << "  \"publicStateOnly\": true,\n"
           << "  \"levelBonus\": " << kLevelBonus << ",\n"
           << "  \"collectionStart\": " << kCollectionStart << ",\n"
           << "  \"trainingGames\": " << kTrainingGames << ",\n"
           << "  \"developmentGames\": " << kDevelopmentGames << ",\n"
           << "  \"originsPerGame\": " << kOriginsPerGame << ",\n"
           << "  \"chanceStrata\": " << kChanceStrata << ",\n"
           << "  \"continuationRollouts\": "
           << kContinuationRollouts << ",\n"
           << "  \"continuationCap\": " << kContinuationCap << ",\n"
           << "  \"trainingUniqueSuccessors\": "
           << training.states.size() << ",\n"
           << "  \"developmentUniqueSuccessors\": "
           << development.states.size() << ",\n"
           << "  \"trainingRawBranches\": " << training.raw_branches
           << ",\n  \"trainingDeduplicatedBranches\": "
           << training.deduplicated_branches
           << ",\n  \"developmentRawBranches\": "
           << development.raw_branches
           << ",\n  \"developmentDeduplicatedBranches\": "
           << development.deduplicated_branches << ",\n"
           << "  \"trainingGroups\": " << training.groups.size()
           << ",\n  \"developmentGroups\": "
           << development.groups.size() << ",\n"
           << "  \"overlapStatesRemoved\": "
           << purge.overlapping_states << ",\n"
           << "  \"overlapGroupsRemoved\": " << purge.dropped_groups
           << ",\n  \"trainingLoss\": " << training_loss
           << ",\n  \"parameterBytes\": " << network.parameterBytes()
           << ",\n  \"trainingPrediction\": ";
  writePredictionMetrics(artifact, training_predictions);
  artifact << ",\n  \"developmentPrediction\": ";
  writePredictionMetrics(artifact, development_predictions);
  artifact << ",\n  \"developmentDirectRanking\": ";
  writeActionMetrics(artifact, direct);
  artifact << ",\n  \"developmentLeafRanking\": ";
  writeActionMetrics(artifact, leaf);
  artifact << ",\n  \"deployment\": \""
           << (deployment == DeploymentMode::kDirect ? "direct" : "leaf-d3")
           << "\",\n  \"screenSeedStart\": " << kScreenStart
           << ",\n  \"screen\": {\"exactD3\":";
  writePolicySummary(artifact, screen_baseline);
  artifact << ",\"candidate\":";
  writePolicySummary(artifact, screen_candidate);
  artifact << ",\"paired\":";
  writePaired(artifact, screen_paired);
  artifact << ",\"exactTrajectories\":";
  writeTrajectories(artifact, screen.baseline);
  artifact << ",\"candidateTrajectories\":";
  writeTrajectories(artifact, screen.candidate);
  artifact << "},\n  \"screenPassed\": "
           << (screen_passed ? "true" : "false")
           << ",\n  \"confirmation\": ";
  if (!screen_passed) {
    artifact << "null";
  } else {
    artifact << "{\"seedStart\":" << kConfirmationStart
             << ",\"exactD3\":";
    writePolicySummary(artifact, confirmation_baseline);
    artifact << ",\"candidate\":";
    writePolicySummary(artifact, confirmation_candidate);
    artifact << ",\"paired\":";
    writePaired(artifact, confirmation_paired);
    artifact << ",\"exactTrajectories\":";
    writeTrajectories(artifact, confirmation.baseline);
    artifact << ",\"candidateTrajectories\":";
    writeTrajectories(artifact, confirmation.candidate);
    artifact << '}';
  }
  artifact << ",\n  \"confirmed\": " << (confirmed ? "true" : "false")
           << ",\n  \"decision\": \""
           << (!screen_passed
                   ? "reject-screen"
                   : (confirmed ? "advance" : "reject-confirmation"))
           << "\",\n  \"model\": \"" << options.model
           << "\",\n  \"peakRssBytes\": " << peakRssBytes()
           << ",\n  \"elapsedSeconds\": " << elapsed_seconds << "\n}\n";

  output << std::fixed << std::setprecision(4)
         << "COUNTERFACTUAL_SUCCESSOR_RESULT {\"trainingExamples\":"
         << training.states.size() << ",\"developmentExamples\":"
         << development.states.size() << ",\"developmentSpearman\":"
         << development_predictions.spearman
         << ",\"directAccuracy\":" << direct.top1_accuracy
         << ",\"directRegret\":" << direct.mean_regret
         << ",\"leafAccuracy\":" << leaf.top1_accuracy
         << ",\"leafRegret\":" << leaf.mean_regret
         << ",\"deployment\":\""
         << (deployment == DeploymentMode::kDirect ? "direct" : "leaf-d3")
         << "\",\"screenScoreDifference\":"
         << screen_paired.mean_score_difference
         << ",\"screenMoveDifference\":"
         << screen_paired.mean_move_difference
         << ",\"screenPassed\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmationRan\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmed\":" << (confirmed ? "true" : "false")
         << ",\"decision\":\""
         << (!screen_passed
                 ? "reject-screen"
                 : (confirmed ? "advance" : "reject-confirmation"))
         << "\",\"artifact\":\"" << options.artifact << "\"}\n";
  return 0;
}

}  // namespace drop7::counterfactual_successor

int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string(argv[1]) == "--self-test") {
      return drop7::counterfactual_successor::selfTest(std::cout) ? 0 : 1;
    }
    if (argc >= 2 && std::string(argv[1]) == "--run") {
      const auto options =
          drop7::counterfactual_successor::parseOptions(argc, argv, 2);
      return drop7::counterfactual_successor::run(options, std::cout);
    }
    std::cerr << "usage: drop7_counterfactual_successor_nnue --self-test | "
                 "--run [--artifact PATH] [--model PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_counterfactual_successor_nnue: " << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
}
