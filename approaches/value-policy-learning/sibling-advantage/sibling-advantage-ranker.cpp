// Performs bounded counterfactual policy improvement using labels that compare
// all legal siblings of one public root.  Each sibling is replayed on exactly the
// same public continuation tapes (common random numbers), so source-position
// difficulty cannot leak into the target.
#define main drop7_structured_value_nnue_frozen_entrypoint
#include "../structured-nnue/structured-value-nnue.cpp"
#undef main

#include <fstream>
#include <future>
#include <unordered_map>
#include <unordered_set>

namespace drop7::sibling_advantage_ranker {

namespace features = drop7::structured_value_nnue;

constexpr std::uint32_t kCollectionStart = 0x3d91'0000u;
constexpr int kTrainingGames = 14;
constexpr int kDevelopmentGames = 6;
constexpr int kTotalCollectionGames = kTrainingGames + kDevelopmentGames;
constexpr int kExactRootsPerGame = 2;
constexpr int kOnPolicyRootsPerGame = 1;
constexpr int kPerturbedRootsPerGame = 1;
constexpr int kContinuationTapes = 24;
constexpr int kDepthTwoTapes = 4;
constexpr int kContinuationHorizon = 60;
constexpr int kChanceStrata = 3;
constexpr int kRankHidden = 32;
constexpr int kTrainingEpochs = 72;
constexpr int kGroupBatchSize = 8;
constexpr float kLearningRate = 0.0015f;
constexpr float kWeightDecay = 2.0e-5f;
constexpr double kPairwiseLossWeight = 0.45;
constexpr double kResidualFraction = 0.15;
constexpr double kRequiredTopOne = 0.45;
constexpr double kRequiredPairwise = 0.68;
constexpr int kPolicyMaximumMoves = 500;
constexpr std::uint32_t kScreenStart = 0x3e8d'0000u;
constexpr int kScreenGames = 8;
constexpr std::uint32_t kConfirmationStart = 0x3e8e'0000u;
constexpr int kConfirmationGames = 16;
constexpr int kParallelism = 4;
constexpr std::uint32_t kPublicPolicySeed = 0x5352'414eu;
constexpr std::uint32_t kTapeDomain = 0x5441'5045u;
constexpr std::uint32_t kTapeDiscDomain = 0x4449'5343u;
constexpr std::uint32_t kTapeRevealDomain = 0x5245'564cu;
constexpr std::uint32_t kSuccessorDomain = 0x5355'4343u;
constexpr std::uint64_t kExactMaximumWork = 1'000'000;
constexpr std::size_t kExactMaximumCache = 40'000;

static_assert(kLevelBonus == 7'000);
static_assert(kContinuationTapes >= 24 && kContinuationTapes <= 32);
static_assert(kContinuationHorizon >= 50 && kContinuationHorizon <= 75);
static_assert(kDepthTwoTapes > 0 && kDepthTwoTapes < kContinuationTapes);
static_assert((kCollectionStart >> 24) != 0x7du &&
              (kCollectionStart >> 24) != 0xd7u);
static_assert((kScreenStart >> 24) != 0x7du &&
              (kScreenStart >> 24) != 0xd7u);
static_assert((kConfirmationStart >> 24) != 0x7du &&
              (kConfirmationStart >> 24) != 0xd7u);

constexpr std::array<int, kBoardSize> kColumnOrder{{3, 2, 4, 1, 5, 0, 6}};

std::mutex progress_mutex;

std::uint64_t peakRssBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
#endif
}

features::PublicState canonicalPublic(const State& source) {
  bool ignored = false;
  return features::publicState(cfpi::detail::canonicalState(source, ignored));
}

features::PublicState canonicalPublic(const features::PublicState& source) {
  return canonicalPublic(features::materialize(source));
}

std::string stateKey(const features::PublicState& source) {
  const features::PublicState state = canonicalPublic(source);
  std::string result;
  result.reserve(kCellCount + 2);
  for (const std::uint8_t cell : state.board) {
    result.push_back(static_cast<char>(cell));
  }
  result.push_back(static_cast<char>(state.next_disc));
  result.push_back(static_cast<char>(state.moves_remaining));
  return result;
}

std::uint32_t publicHash(const features::PublicState& source) {
  const features::PublicState state = canonicalPublic(source);
  std::uint32_t hash = 0x811c'9dc5u;
  for (const std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint32_t>(cell + 1u);
    hash *= 0x0100'0193u;
  }
  hash ^= state.next_disc;
  hash *= 0x0100'0193u;
  hash ^= state.moves_remaining;
  return mix32(hash);
}

std::uint32_t safePublicSeed(std::uint32_t seed) {
  const std::uint32_t family = seed >> 24;
  return family == 0x7du || family == 0xd7u ? seed ^ 0x4000'0000u : seed;
}

std::uint32_t tapeSeed(const features::PublicState& root, int tape) {
  if (tape < 0 || tape >= kContinuationTapes) {
    throw std::invalid_argument("invalid continuation tape");
  }
  return safePublicSeed(mix32(
      publicHash(root) ^ kTapeDomain ^
      (static_cast<std::uint32_t>(tape + 1) * 0x9e37'79b9u)));
}

// A stateless, replayable public tape.  Next discs are indexed by move and
// reveal discs by (move, reveal-event), so a sibling that reveals more covers
// cannot shift the future next-disc stream for the other siblings.  Policies
// never receive the seed or any future tape entry.
struct PublicTape {
  std::uint32_t seed = 0;
  int move = 0;

  std::uint8_t nextDiscForMove(int move_index) const {
    const std::uint32_t bits = mix32(
        seed ^ kTapeDiscDomain ^
        (static_cast<std::uint32_t>(move_index + 1) * 0x9e37'79b9u));
    return static_cast<std::uint8_t>(
        ((static_cast<std::uint64_t>(bits) * kBoardSize) >> 32) + 1u);
  }

  std::uint8_t revealDisc(int event) const {
    const std::uint32_t bits = mix32(
        seed ^ kTapeRevealDomain ^
        (static_cast<std::uint32_t>(move + 1) * 0x85eb'ca6bu) ^
        (static_cast<std::uint32_t>(event + 1) * 0xc2b2'ae35u));
    return static_cast<std::uint8_t>(
        ((static_cast<std::uint64_t>(bits) * kBoardSize) >> 32) + 1u);
  }
};

struct PublicMoveRandom {
  const PublicTape& tape;
  int event = 0;

  std::uint8_t nextDisc() { return tape.revealDisc(event++); }
};

bool playPublicTapeMove(State& state, int action, PublicTape& tape,
                        MoveResult& result) {
  PublicMoveRandom random{tape, 0};
  if (!cfpi::detail::playMoveSampled(state, action, random, result)) {
    return false;
  }
  ++tape.move;
  state = result.state;
  if (!state.game_over) state.next_disc = tape.nextDiscForMove(tape.move);
  result.state = state;
  return true;
}

cfpi::BehaviorOptions behaviorOptions(int depth, int samples) {
  cfpi::BehaviorOptions options;
  options.max_depth = depth;
  options.chance_samples = samples;
  options.max_work = depth == 3 ? kExactMaximumWork : 120'000;
  options.max_cache_entries = depth == 3 ? kExactMaximumCache : 4'000;
  options.policy_seed = kPublicPolicySeed;
  return options;
}

enum class RollInKind { kExactD3, kOnPolicyD2, kPerturbedD2 };

std::string_view rollInName(RollInKind kind) {
  switch (kind) {
    case RollInKind::kExactD3:
      return "exact-d3";
    case RollInKind::kOnPolicyD2:
      return "on-policy-d2-s3";
    case RollInKind::kPerturbedD2:
      return "perturbed-d2-s3";
  }
  throw std::logic_error("invalid roll-in kind");
}

struct Origin {
  std::uint32_t source_game = 0;
  RollInKind kind = RollInKind::kExactD3;
  int source_move = 0;
  features::PublicState root{};
};

struct RollInGame {
  std::uint32_t seed = 0;
  std::vector<Origin> origins;
  std::uint64_t search_work = 0;
};

int perturbedAction(const State& source,
                    const cfpi::BehaviorOptions& options,
                    std::uint64_t& work) {
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  cfpi::BehaviorMetrics metrics;
  const int preferred = cfpi::chooseBehaviorAction(canonical, options,
                                                    &metrics);
  if (!metrics.complete || metrics.completed_depth != options.max_depth) {
    throw std::runtime_error("perturbed roll-in search incomplete");
  }
  work += metrics.work;
  int offset = 0;
  while (offset < kBoardSize && kColumnOrder[offset] != preferred) ++offset;
  for (int step = 1; step < kBoardSize; ++step) {
    const int candidate = kColumnOrder[(offset + step) % kBoardSize];
    if (!isLegal(canonical.board, candidate)) continue;
    return mirrored ? kBoardSize - 1 - candidate : candidate;
  }
  return mirrored ? kBoardSize - 1 - preferred : preferred;
}

void appendOrigin(RollInGame& game, const State& state, RollInKind kind) {
  game.origins.push_back(
      {game.seed, kind, state.moves_played, canonicalPublic(state)});
}

void collectTrajectory(RollInGame& game, RollInKind kind,
                       const std::vector<int>& capture_moves) {
  if (capture_moves.empty()) return;
  State state = initialHeadlessState(game.seed);
  const int maximum_move = *std::max_element(capture_moves.begin(),
                                              capture_moves.end());
  const cfpi::BehaviorOptions options =
      kind == RollInKind::kExactD3 ? behaviorOptions(3, 5)
                                   : behaviorOptions(2, 3);
  std::size_t capture = 0;
  while (!state.game_over && state.moves_played <= maximum_move) {
    if (capture < capture_moves.size() &&
        state.moves_played == capture_moves[capture]) {
      appendOrigin(game, state, kind);
      ++capture;
      if (capture == capture_moves.size()) break;
    }
    int action = -1;
    if (kind == RollInKind::kPerturbedD2 &&
        state.moves_played % 6 == 5) {
      action = perturbedAction(state, options, game.search_work);
    } else {
      cfpi::BehaviorMetrics metrics;
      action = cfpi::chooseBehaviorAction(state, options, &metrics);
      if (!metrics.complete || metrics.completed_depth != options.max_depth) {
        throw std::runtime_error("roll-in search incomplete");
      }
      game.search_work += metrics.work;
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("roll-in selected illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, game.seed, action, move)) {
      throw std::runtime_error("roll-in transition failed");
    }
  }
}

RollInGame collectRollInGame(std::uint32_t seed) {
  RollInGame result;
  result.seed = seed;
  collectTrajectory(result, RollInKind::kExactD3, {15, 30});
  collectTrajectory(result, RollInKind::kOnPolicyD2, {22});
  collectTrajectory(result, RollInKind::kPerturbedD2, {22});
  return result;
}

std::vector<RollInGame> collectRollIns() {
  std::vector<RollInGame> result(kTotalCollectionGames);
  std::atomic<int> next{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0;
       worker < std::min(kParallelism, kTotalCollectionGames); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next.fetch_add(1);
        if (game >= kTotalCollectionGames) return;
        result[static_cast<std::size_t>(game)] = collectRollInGame(
            kCollectionStart + static_cast<std::uint32_t>(game));
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

struct SuccessorBranch {
  features::PublicState state{};
  features::Engineered metrics{};
  bool terminal = false;
  double immediate_return = 0;
};

struct SiblingAction {
  int action = -1;
  std::array<SuccessorBranch, kChanceStrata> branches{};
  std::array<double, kContinuationTapes> tape_returns{};
  double mean_return = 0;
  double mean_moves = 0;
  double mean_score = 0;
  double advantage = 0;
};

struct SiblingGroup {
  std::uint32_t source_game = 0;
  RollInKind kind = RollInKind::kExactD3;
  int source_move = 0;
  features::PublicState root{};
  std::string root_key;
  std::vector<SiblingAction> actions;
  std::uint64_t label_policy_work = 0;
};

struct Dataset {
  std::vector<SiblingGroup> groups;
  std::uint64_t duplicate_roots = 0;
};

SuccessorBranch publicSuccessor(const State& canonical, int action,
                                int sample) {
  if (!isLegal(canonical.board, action) || sample < 0 ||
      sample >= kChanceStrata) {
    throw std::invalid_argument("invalid public successor");
  }
  const std::uint32_t seed = cfpi::detail::scenarioSeedForState(
      canonical, kSuccessorDomain, 1);
  cfpi::detail::StratifiedRandom random{seed, sample, kChanceStrata, 0};
  MoveResult move;
  if (!cfpi::detail::playMoveSampled(canonical, action, random, move)) {
    throw std::runtime_error("public successor transition failed");
  }
  SuccessorBranch result;
  result.terminal = move.state.game_over;
  result.immediate_return =
      1.0 + static_cast<double>(move.score_delta) / kLevelBonus;
  if (!result.terminal) {
    move.state.next_disc =
        cfpi::detail::sampledNextDisc(seed, sample, kChanceStrata);
    result.state = canonicalPublic(move.state);
  }
  return result;
}

SiblingGroup makeGroup(const Origin& origin) {
  SiblingGroup group;
  group.source_game = origin.source_game;
  group.kind = origin.kind;
  group.source_move = origin.source_move;
  group.root = canonicalPublic(origin.root);
  group.root_key = stateKey(group.root);
  State canonical = features::materialize(group.root);
  for (const int action : kColumnOrder) {
    if (!isLegal(canonical.board, action)) continue;
    SiblingAction sibling;
    sibling.action = action;
    for (int sample = 0; sample < kChanceStrata; ++sample) {
      sibling.branches[sample] = publicSuccessor(canonical, action, sample);
    }
    group.actions.push_back(std::move(sibling));
  }
  return group;
}

Dataset buildDataset(const std::vector<RollInGame>& games,
                     int begin, int end) {
  Dataset result;
  std::unordered_set<std::string> seen;
  for (int game = begin; game < end; ++game) {
    for (const Origin& origin : games[static_cast<std::size_t>(game)].origins) {
      SiblingGroup group = makeGroup(origin);
      if (group.actions.size() < 2) continue;
      if (!seen.emplace(group.root_key).second) {
        ++result.duplicate_roots;
        continue;
      }
      result.groups.push_back(std::move(group));
    }
  }
  return result;
}

std::vector<std::string> groupStateKeys(const SiblingGroup& group) {
  std::vector<std::string> keys;
  keys.push_back(group.root_key);
  for (const SiblingAction& action : group.actions) {
    for (const SuccessorBranch& branch : action.branches) {
      if (!branch.terminal) keys.push_back(stateKey(branch.state));
    }
  }
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  return keys;
}

struct PurgeStats {
  int overlapping_groups = 0;
  int overlapping_states = 0;
};

PurgeStats purgeDevelopmentOverlap(const Dataset& training,
                                   Dataset& development) {
  std::unordered_set<std::string> training_states;
  for (const SiblingGroup& group : training.groups) {
    for (std::string& key : groupStateKeys(group)) {
      training_states.emplace(std::move(key));
    }
  }
  PurgeStats stats;
  std::vector<SiblingGroup> kept;
  kept.reserve(development.groups.size());
  for (SiblingGroup& group : development.groups) {
    int overlaps = 0;
    for (const std::string& key : groupStateKeys(group)) {
      overlaps += training_states.contains(key);
    }
    if (overlaps > 0) {
      ++stats.overlapping_groups;
      stats.overlapping_states += overlaps;
    } else {
      kept.push_back(std::move(group));
    }
  }
  development.groups = std::move(kept);
  return stats;
}

struct TapeOutcome {
  double value = 0;
  int moves = 0;
  std::int64_t score = 0;
  std::uint64_t policy_work = 0;
};

TapeOutcome replaySibling(const features::PublicState& public_root,
                          int action, int tape) {
  State state = features::materialize(public_root);
  state.score = 0;
  state.level = 1;
  state.moves_played = 0;
  PublicTape random{tapeSeed(public_root, tape), 0};
  MoveResult move;
  if (!playPublicTapeMove(state, action, random, move)) {
    throw std::runtime_error("counterfactual root transition failed");
  }
  TapeOutcome result;
  result.moves = 1;
  const bool depth_two = tape >= kContinuationTapes - kDepthTwoTapes;
  const cfpi::BehaviorOptions options =
      behaviorOptions(depth_two ? 2 : 1, 3);
  while (!state.game_over && result.moves < kContinuationHorizon) {
    cfpi::BehaviorMetrics metrics;
    const int continuation =
        cfpi::chooseBehaviorAction(state, options, &metrics);
    if (!metrics.complete || metrics.completed_depth != options.max_depth ||
        !isLegal(state.board, continuation)) {
      throw std::runtime_error("public continuation policy failed");
    }
    result.policy_work += metrics.work;
    if (!playPublicTapeMove(state, continuation, random, move)) {
      throw std::runtime_error("public continuation transition failed");
    }
    ++result.moves;
  }
  result.score = state.score;
  result.value = static_cast<double>(result.moves) +
                 static_cast<double>(result.score) / kLevelBonus;
  return result;
}

void labelGroup(SiblingGroup& group) {
  std::vector<std::array<TapeOutcome, kContinuationTapes>> outcomes(
      group.actions.size());
  // Tape is the outer loop intentionally: every legal action receives the
  // exact same public seed before another tape is considered.
  for (int tape = 0; tape < kContinuationTapes; ++tape) {
    for (std::size_t action = 0; action < group.actions.size(); ++action) {
      outcomes[action][tape] = replaySibling(
          group.root, group.actions[action].action, tape);
    }
  }
  double sibling_mean = 0;
  for (std::size_t action = 0; action < group.actions.size(); ++action) {
    SiblingAction& sibling = group.actions[action];
    for (int tape = 0; tape < kContinuationTapes; ++tape) {
      const TapeOutcome& outcome = outcomes[action][tape];
      sibling.tape_returns[tape] = outcome.value;
      sibling.mean_return += outcome.value / kContinuationTapes;
      sibling.mean_moves +=
          static_cast<double>(outcome.moves) / kContinuationTapes;
      sibling.mean_score +=
          static_cast<double>(outcome.score) / kContinuationTapes;
      group.label_policy_work += outcome.policy_work;
    }
    sibling_mean += sibling.mean_return / group.actions.size();
  }
  double sibling_variance = 0;
  for (const SiblingAction& sibling : group.actions) {
    const double delta = sibling.mean_return - sibling_mean;
    sibling_variance += delta * delta / group.actions.size();
  }
  const double sibling_scale =
      std::max(0.25, std::sqrt(sibling_variance));
  for (SiblingAction& sibling : group.actions) {
    sibling.advantage =
        (sibling.mean_return - sibling_mean) / sibling_scale;
  }
}

void labelDataset(Dataset& dataset, std::string_view name) {
  std::atomic<int> next{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0;
       worker < std::min<int>(kParallelism, dataset.groups.size()); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int index = next.fetch_add(1);
        if (index >= static_cast<int>(dataset.groups.size())) return;
        labelGroup(dataset.groups[static_cast<std::size_t>(index)]);
        const std::lock_guard<std::mutex> lock(progress_mutex);
        std::cerr << name << " labeled group " << index + 1 << '/'
                  << dataset.groups.size() << '\n';
      }
    }));
  }
  for (auto& worker : workers) worker.get();
}

features::Normalizer fitTrainingNormalizer(const Dataset& training) {
  std::vector<features::Label> labels;
  for (const SiblingGroup& group : training.groups) {
    labels.push_back({group.root, 0, 0, 0});
    for (const SiblingAction& action : group.actions) {
      for (const SuccessorBranch& branch : action.branches) {
        if (!branch.terminal) labels.push_back({branch.state, 0, 0, 0});
      }
    }
  }
  return features::fitNormalizer(labels);
}

void prepareMetrics(Dataset& dataset,
                    const features::Normalizer& normalizer) {
  for (SiblingGroup& group : dataset.groups) {
    for (SiblingAction& action : group.actions) {
      for (SuccessorBranch& branch : action.branches) {
        if (branch.terminal) continue;
        branch.metrics = normalizer.apply(features::rawEngineered(branch.state));
      }
    }
  }
}

struct RankParameters {
  std::vector<float> embedding = std::vector<float>(
      static_cast<std::size_t>(features::kCategoryCount) * kRankHidden);
  std::array<float, features::kMetricCount * kRankHidden> metric_weight{};
  std::array<float, kRankHidden> bias{};
  std::array<float, kRankHidden> output_weight{};
};

constexpr std::size_t rankParameterCount() {
  return static_cast<std::size_t>(features::kCategoryCount) * kRankHidden +
         static_cast<std::size_t>(features::kMetricCount) * kRankHidden +
         kRankHidden + kRankHidden;
}

struct RankForward {
  features::Categories categories{};
  features::Engineered metrics{};
  std::array<float, kRankHidden> pre{};
  std::array<float, kRankHidden> hidden{};
  float score = 0;
};

class RankNetwork {
 public:
  explicit RankNetwork(std::uint32_t seed = 0x5241'4e4bu) {
    Mulberry32 random(seed);
    const auto initialize = [&random](auto& values, float radius) {
      for (float& value : values) {
        value = static_cast<float>(
            (2.0 * random.nextUnit() - 1.0) * radius);
      }
    };
    initialize(parameters.embedding, 0.035f);
    initialize(parameters.metric_weight, 0.035f);
    initialize(parameters.output_weight, 0.10f);
  }

  float forwardOrientation(const features::PublicState& state,
                           const features::Engineered& metrics,
                           RankForward* cache = nullptr) const {
    RankForward local;
    RankForward& result = cache == nullptr ? local : *cache;
    result.categories = features::activeCategories(state);
    result.metrics = metrics;
    result.pre = parameters.bias;
    const float category_scale =
        1.0f / static_cast<float>(std::sqrt(features::kActiveCategories));
    const float metric_scale =
        1.0f / static_cast<float>(std::sqrt(features::kMetricCount));
    for (const int category : result.categories) {
      const int base = category * kRankHidden;
      for (int hidden = 0; hidden < kRankHidden; ++hidden) {
        result.pre[hidden] +=
            category_scale * parameters.embedding[base + hidden];
      }
    }
    for (int metric = 0; metric < features::kMetricCount; ++metric) {
      const float input = metric_scale * metrics[metric];
      const int base = metric * kRankHidden;
      for (int hidden = 0; hidden < kRankHidden; ++hidden) {
        result.pre[hidden] +=
            input * parameters.metric_weight[base + hidden];
      }
    }
    result.score = 0;
    for (int hidden = 0; hidden < kRankHidden; ++hidden) {
      result.hidden[hidden] = features::leaky(result.pre[hidden]);
      result.score +=
          parameters.output_weight[hidden] * result.hidden[hidden];
    }
    return result.score;
  }

  double score(const features::PublicState& state,
               const features::Engineered& metrics) const {
    return 0.5 * (forwardOrientation(state, metrics) +
                  forwardOrientation(features::mirror(state), metrics));
  }

  std::size_t parameterBytes() const {
    return rankParameterCount() * sizeof(float);
  }

  RankParameters parameters;
};

struct RankGradient {
  RankGradient()
      : embedding(static_cast<std::size_t>(features::kCategoryCount) *
                  kRankHidden),
        touched_marker(features::kCategoryCount) {}

  void touch(int category) {
    if (touched_marker[category]) return;
    touched_marker[category] = 1;
    touched.push_back(category);
  }

  void reset() {
    for (const int category : touched) {
      std::fill_n(embedding.begin() + category * kRankHidden,
                  kRankHidden, 0.0f);
      touched_marker[category] = 0;
    }
    touched.clear();
    metric_weight.fill(0);
    bias.fill(0);
    output_weight.fill(0);
  }

  std::vector<float> embedding;
  std::vector<std::uint8_t> touched_marker;
  std::vector<int> touched;
  std::array<float, features::kMetricCount * kRankHidden> metric_weight{};
  std::array<float, kRankHidden> bias{};
  std::array<float, kRankHidden> output_weight{};
};

void accumulateOrientation(const RankNetwork& network,
                           const features::PublicState& state,
                           const features::Engineered& metrics,
                           float derivative_score,
                           RankGradient& gradient) {
  RankForward cache;
  network.forwardOrientation(state, metrics, &cache);
  const float category_scale =
      1.0f / static_cast<float>(std::sqrt(features::kActiveCategories));
  const float metric_scale =
      1.0f / static_cast<float>(std::sqrt(features::kMetricCount));
  std::array<float, kRankHidden> derivative{};
  for (int hidden = 0; hidden < kRankHidden; ++hidden) {
    gradient.output_weight[hidden] +=
        derivative_score * cache.hidden[hidden];
    derivative[hidden] =
        derivative_score * network.parameters.output_weight[hidden] *
        features::leakyDerivative(cache.pre[hidden]);
    gradient.bias[hidden] += derivative[hidden];
  }
  for (const int category : cache.categories) {
    gradient.touch(category);
    const int base = category * kRankHidden;
    for (int hidden = 0; hidden < kRankHidden; ++hidden) {
      gradient.embedding[base + hidden] +=
          category_scale * derivative[hidden];
    }
  }
  for (int metric = 0; metric < features::kMetricCount; ++metric) {
    const int base = metric * kRankHidden;
    const float input = metric_scale * cache.metrics[metric];
    for (int hidden = 0; hidden < kRankHidden; ++hidden) {
      gradient.metric_weight[base + hidden] +=
          input * derivative[hidden];
    }
  }
}

void accumulateState(const RankNetwork& network,
                     const SuccessorBranch& branch,
                     double derivative_score,
                     RankGradient& gradient) {
  const float half = static_cast<float>(0.5 * derivative_score);
  accumulateOrientation(network, branch.state, branch.metrics, half,
                        gradient);
  accumulateOrientation(network, features::mirror(branch.state),
                        branch.metrics, half, gradient);
}

struct RankMoments {
  RankMoments()
      : embedding_m(static_cast<std::size_t>(features::kCategoryCount) *
                    kRankHidden),
        embedding_v(embedding_m.size()) {}

  std::vector<float> embedding_m;
  std::vector<float> embedding_v;
  std::array<float, features::kMetricCount * kRankHidden> metric_m{};
  std::array<float, features::kMetricCount * kRankHidden> metric_v{};
  std::array<float, kRankHidden> bias_m{};
  std::array<float, kRankHidden> bias_v{};
  std::array<float, kRankHidden> output_m{};
  std::array<float, kRankHidden> output_v{};
  std::uint64_t steps = 0;
};

void adamScalar(float& parameter, float gradient, float& first,
                float& second, float correction1, float correction2,
                bool decay) {
  constexpr float beta1 = 0.9f;
  constexpr float beta2 = 0.999f;
  float adjusted = gradient + (decay ? kWeightDecay * parameter : 0.0f);
  adjusted = std::clamp(adjusted, -5.0f, 5.0f);
  first = beta1 * first + (1.0f - beta1) * adjusted;
  second = beta2 * second + (1.0f - beta2) * adjusted * adjusted;
  parameter -= kLearningRate * (first / correction1) /
               (std::sqrt(second / correction2) + 1.0e-8f);
}

template <std::size_t Size>
void adamArray(std::array<float, Size>& parameters,
               const std::array<float, Size>& gradients,
               std::array<float, Size>& first,
               std::array<float, Size>& second, float scale,
               float correction1, float correction2, bool decay) {
  for (std::size_t index = 0; index < Size; ++index) {
    adamScalar(parameters[index], gradients[index] * scale,
               first[index], second[index], correction1, correction2,
               decay);
  }
}

void applyAdam(RankNetwork& network, RankGradient& gradient,
               RankMoments& moments, int groups) {
  if (groups < 1) throw std::invalid_argument("empty ranker batch");
  ++moments.steps;
  const float correction1 =
      1.0f - static_cast<float>(std::pow(0.9, moments.steps));
  const float correction2 =
      1.0f - static_cast<float>(std::pow(0.999, moments.steps));
  const float scale = 1.0f / groups;
  for (const int category : gradient.touched) {
    const int base = category * kRankHidden;
    for (int hidden = 0; hidden < kRankHidden; ++hidden) {
      const int index = base + hidden;
      adamScalar(network.parameters.embedding[index],
                 gradient.embedding[index] * scale,
                 moments.embedding_m[index], moments.embedding_v[index],
                 correction1, correction2, true);
    }
  }
  adamArray(network.parameters.metric_weight, gradient.metric_weight,
            moments.metric_m, moments.metric_v, scale, correction1,
            correction2, true);
  adamArray(network.parameters.bias, gradient.bias, moments.bias_m,
            moments.bias_v, scale, correction1, correction2, false);
  adamArray(network.parameters.output_weight, gradient.output_weight,
            moments.output_m, moments.output_v, scale, correction1,
            correction2, true);
  gradient.reset();
}

double actionScore(const RankNetwork& network,
                   const SiblingAction& action) {
  constexpr double terminal_leaf = -8.0;
  double result = 0;
  for (const SuccessorBranch& branch : action.branches) {
    result += branch.immediate_return +
              (branch.terminal
                   ? terminal_leaf
                   : network.score(branch.state, branch.metrics));
  }
  return result / kChanceStrata;
}

std::vector<double> softmax(const std::vector<double>& values) {
  if (values.empty()) throw std::invalid_argument("empty softmax");
  const double maximum =
      *std::max_element(values.begin(), values.end());
  std::vector<double> result(values.size());
  double sum = 0;
  for (std::size_t index = 0; index < values.size(); ++index) {
    result[index] = std::exp(std::clamp(values[index] - maximum,
                                        -40.0, 40.0));
    sum += result[index];
  }
  for (double& value : result) value /= sum;
  return result;
}

double sigmoidDouble(double value) {
  if (value >= 0) return 1.0 / (1.0 + std::exp(-value));
  const double exponential = std::exp(value);
  return exponential / (1.0 + exponential);
}

double accumulateGroup(const RankNetwork& network,
                       const SiblingGroup& group,
                       RankGradient& gradient) {
  const std::size_t count = group.actions.size();
  std::vector<double> predictions(count);
  std::vector<double> targets(count);
  for (std::size_t action = 0; action < count; ++action) {
    predictions[action] = actionScore(network, group.actions[action]);
    targets[action] = group.actions[action].advantage;
  }
  const std::vector<double> predicted_probability = softmax(predictions);
  const std::vector<double> target_probability = softmax(targets);
  std::vector<double> derivative(count);
  double loss = 0;
  for (std::size_t action = 0; action < count; ++action) {
    loss -= target_probability[action] *
            std::log(std::max(1.0e-12, predicted_probability[action]));
    derivative[action] =
        predicted_probability[action] - target_probability[action];
  }
  int pairs = 0;
  for (std::size_t first = 0; first < count; ++first) {
    for (std::size_t second = first + 1; second < count; ++second) {
      pairs += std::abs(targets[first] - targets[second]) > 1.0e-6;
    }
  }
  if (pairs > 0) {
    for (std::size_t first = 0; first < count; ++first) {
      for (std::size_t second = first + 1; second < count; ++second) {
        const double target_difference = targets[first] - targets[second];
        if (std::abs(target_difference) <= 1.0e-6) continue;
        const double sign = target_difference > 0 ? 1.0 : -1.0;
        const double difference = predictions[first] - predictions[second];
        const double weight = std::min(2.0, std::abs(target_difference));
        const double factor = kPairwiseLossWeight * weight / pairs;
        loss += factor * std::log1p(std::exp(
                             std::clamp(-sign * difference, -40.0, 40.0)));
        const double pair_derivative =
            -factor * sign * sigmoidDouble(-sign * difference);
        derivative[first] += pair_derivative;
        derivative[second] -= pair_derivative;
      }
    }
  }
  for (std::size_t action = 0; action < count; ++action) {
    const double branch_derivative = derivative[action] / kChanceStrata;
    for (const SuccessorBranch& branch : group.actions[action].branches) {
      if (!branch.terminal) {
        accumulateState(network, branch, branch_derivative, gradient);
      }
    }
  }
  return loss;
}

struct TrainingResult {
  double first_loss = 0;
  double final_loss = 0;
};

TrainingResult train(RankNetwork& network, const Dataset& training) {
  if (training.groups.empty()) {
    throw std::invalid_argument("empty sibling training set");
  }
  std::vector<std::size_t> order(training.groups.size());
  std::iota(order.begin(), order.end(), 0);
  RankGradient gradient;
  RankMoments moments;
  TrainingResult result;
  for (int epoch = 0; epoch < kTrainingEpochs; ++epoch) {
    Mulberry32 random(mix32(0x4c49'5354u ^
                           static_cast<std::uint32_t>(epoch + 1)));
    for (std::size_t cursor = order.size(); cursor > 1; --cursor) {
      const std::size_t target = static_cast<std::size_t>(
          (static_cast<std::uint64_t>(random.nextBits()) * cursor) >> 32);
      std::swap(order[cursor - 1], order[target]);
    }
    double epoch_loss = 0;
    int epoch_groups = 0;
    for (std::size_t start = 0; start < order.size();
         start += kGroupBatchSize) {
      const std::size_t end =
          std::min(order.size(), start + kGroupBatchSize);
      for (std::size_t offset = start; offset < end; ++offset) {
        epoch_loss += accumulateGroup(
            network, training.groups[order[offset]], gradient);
        ++epoch_groups;
      }
      applyAdam(network, gradient, moments,
                static_cast<int>(end - start));
    }
    const double mean_loss = epoch_loss / std::max(1, epoch_groups);
    if (epoch == 0) result.first_loss = mean_loss;
    result.final_loss = mean_loss;
  }
  return result;
}

struct ExactValues {
  std::vector<int> actions;
  std::vector<double> values;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::size_t cache_entries = 0;
};

ExactValues exactRootValues(const features::PublicState& public_root) {
  const cfpi::BehaviorOptions options = behaviorOptions(3, 5);
  cfpi::detail::SearchContext context(options);
  const State state = features::materialize(canonicalPublic(public_root));
  ExactValues result;
  for (const int action : kColumnOrder) {
    if (!isLegal(state.board, action)) continue;
    result.actions.push_back(action);
    result.values.push_back(
        cfpi::detail::evaluateAction(state, action, 3, context));
  }
  result.work = context.work;
  result.nodes = context.nodes;
  result.cache_entries = context.cache.size();
  return result;
}

std::vector<double> standardized(const std::vector<double>& values) {
  if (values.empty()) throw std::invalid_argument("empty standardization");
  const double mean =
      std::accumulate(values.begin(), values.end(), 0.0) / values.size();
  double variance = 0;
  for (const double value : values) {
    const double delta = value - mean;
    variance += delta * delta / values.size();
  }
  const double scale = std::max(1.0e-6, std::sqrt(variance));
  std::vector<double> result(values.size());
  for (std::size_t index = 0; index < values.size(); ++index) {
    result[index] = (values[index] - mean) / scale;
  }
  return result;
}

std::vector<double> residualScores(const std::vector<double>& exact,
                                   const std::vector<double>& ranker) {
  if (exact.size() != ranker.size() || exact.empty()) {
    throw std::invalid_argument("invalid residual score vectors");
  }
  const std::vector<double> exact_z = standardized(exact);
  const std::vector<double> ranker_z = standardized(ranker);
  std::vector<double> result(exact.size());
  for (std::size_t index = 0; index < exact.size(); ++index) {
    result[index] = exact_z[index] +
                    kResidualFraction *
                        std::clamp(ranker_z[index], -2.5, 2.5);
  }
  return result;
}

std::vector<double> directGroupScores(const SiblingGroup& group,
                                      const RankNetwork& network) {
  std::vector<double> result;
  result.reserve(group.actions.size());
  for (const SiblingAction& action : group.actions) {
    result.push_back(actionScore(network, action));
  }
  return result;
}

struct RootModelScores {
  std::vector<int> actions;
  std::vector<double> values;
  std::uint64_t work = 0;
};

RootModelScores directRootScores(
    const features::PublicState& public_root,
    const RankNetwork& network,
    const features::Normalizer& normalizer) {
  const features::PublicState root = canonicalPublic(public_root);
  const State canonical = features::materialize(root);
  RootModelScores result;
  for (const int action : kColumnOrder) {
    if (!isLegal(canonical.board, action)) continue;
    SiblingAction sibling;
    sibling.action = action;
    for (int sample = 0; sample < kChanceStrata; ++sample) {
      sibling.branches[sample] = publicSuccessor(canonical, action, sample);
      SuccessorBranch& branch = sibling.branches[sample];
      if (!branch.terminal) {
        branch.metrics = normalizer.apply(
            features::rawEngineered(branch.state));
      }
      ++result.work;
    }
    result.actions.push_back(action);
    result.values.push_back(actionScore(network, sibling));
  }
  return result;
}

int bestIndex(const std::vector<double>& values) {
  if (values.empty()) return -1;
  int result = 0;
  for (std::size_t index = 1; index < values.size(); ++index) {
    if (values[index] > values[static_cast<std::size_t>(result)]) {
      result = static_cast<int>(index);
    }
  }
  return result;
}

struct ActionMetrics {
  int groups = 0;
  int pairs = 0;
  double top1_accuracy = 0;
  double pairwise_accuracy = 0;
  double mean_regret = 0;
  double mean_spearman = 0;
};

void observeActionMetrics(ActionMetrics& result,
                          const std::vector<double>& predictions,
                          const std::vector<double>& targets) {
  if (predictions.size() != targets.size() || predictions.size() < 2) {
    throw std::invalid_argument("invalid action metric group");
  }
  const int target_best = bestIndex(targets);
  const int predicted_best = bestIndex(predictions);
  result.top1_accuracy += predicted_best == target_best;
  result.mean_regret +=
      targets[static_cast<std::size_t>(target_best)] -
      targets[static_cast<std::size_t>(predicted_best)];
  result.mean_spearman += features::spearman(predictions, targets);
  for (std::size_t first = 0; first < targets.size(); ++first) {
    for (std::size_t second = first + 1; second < targets.size(); ++second) {
      const double target_difference = targets[first] - targets[second];
      if (std::abs(target_difference) <= 1.0e-9) continue;
      const double prediction_difference =
          predictions[first] - predictions[second];
      result.pairwise_accuracy +=
          target_difference * prediction_difference > 0;
      ++result.pairs;
    }
  }
  ++result.groups;
}

void finishActionMetrics(ActionMetrics& result) {
  if (result.groups > 0) {
    result.top1_accuracy /= result.groups;
    result.mean_regret /= result.groups;
    result.mean_spearman /= result.groups;
  }
  if (result.pairs > 0) result.pairwise_accuracy /= result.pairs;
}

ActionMetrics directMetrics(const Dataset& dataset,
                            const RankNetwork& network) {
  ActionMetrics result;
  for (const SiblingGroup& group : dataset.groups) {
    std::vector<double> targets;
    targets.reserve(group.actions.size());
    for (const SiblingAction& action : group.actions) {
      targets.push_back(action.mean_return);
    }
    observeActionMetrics(result, directGroupScores(group, network), targets);
  }
  finishActionMetrics(result);
  return result;
}

struct HeldoutMetrics {
  ActionMetrics exact;
  ActionMetrics direct;
  ActionMetrics residual;
  std::uint64_t exact_work = 0;
  std::uint64_t exact_nodes = 0;
  std::size_t peak_cache_entries = 0;
};

HeldoutMetrics evaluateHeldout(const Dataset& development,
                               const RankNetwork& network) {
  HeldoutMetrics result;
  for (const SiblingGroup& group : development.groups) {
    std::vector<double> targets;
    targets.reserve(group.actions.size());
    for (const SiblingAction& action : group.actions) {
      targets.push_back(action.mean_return);
    }
    const std::vector<double> direct = directGroupScores(group, network);
    const ExactValues exact = exactRootValues(group.root);
    if (exact.actions.size() != group.actions.size()) {
      throw std::runtime_error("heldout exact action mismatch");
    }
    for (std::size_t index = 0; index < exact.actions.size(); ++index) {
      if (exact.actions[index] != group.actions[index].action) {
        throw std::runtime_error("heldout exact action order mismatch");
      }
    }
    const std::vector<double> residual =
        residualScores(exact.values, direct);
    observeActionMetrics(result.exact, exact.values, targets);
    observeActionMetrics(result.direct, direct, targets);
    observeActionMetrics(result.residual, residual, targets);
    result.exact_work += exact.work;
    result.exact_nodes += exact.nodes;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, exact.cache_entries);
  }
  finishActionMetrics(result.exact);
  finishActionMetrics(result.direct);
  finishActionMetrics(result.residual);
  return result;
}

enum class DeploymentMode { kDirect, kResidualD3 };

const ActionMetrics& deploymentMetrics(const HeldoutMetrics& heldout,
                                       DeploymentMode mode) {
  return mode == DeploymentMode::kDirect ? heldout.direct
                                         : heldout.residual;
}

bool positiveRegretReduction(const ActionMetrics& candidate,
                             const ActionMetrics& baseline) {
  return candidate.mean_regret + 1.0e-9 < baseline.mean_regret;
}

bool strongHeldout(const ActionMetrics& candidate,
                   const ActionMetrics& baseline) {
  return candidate.top1_accuracy >= kRequiredTopOne &&
         candidate.pairwise_accuracy >= kRequiredPairwise &&
         positiveRegretReduction(candidate, baseline);
}

DeploymentMode chooseDeployment(const HeldoutMetrics& heldout) {
  const bool direct_strong = strongHeldout(heldout.direct, heldout.exact);
  const bool residual_strong =
      strongHeldout(heldout.residual, heldout.exact);
  if (direct_strong != residual_strong) {
    return direct_strong ? DeploymentMode::kDirect
                         : DeploymentMode::kResidualD3;
  }
  if (heldout.residual.top1_accuracy != heldout.direct.top1_accuracy) {
    return heldout.residual.top1_accuracy > heldout.direct.top1_accuracy
               ? DeploymentMode::kResidualD3
               : DeploymentMode::kDirect;
  }
  if (heldout.residual.pairwise_accuracy !=
      heldout.direct.pairwise_accuracy) {
    return heldout.residual.pairwise_accuracy >
                   heldout.direct.pairwise_accuracy
               ? DeploymentMode::kResidualD3
               : DeploymentMode::kDirect;
  }
  return heldout.residual.mean_regret < heldout.direct.mean_regret
             ? DeploymentMode::kResidualD3
             : DeploymentMode::kDirect;
}

std::string_view deploymentName(DeploymentMode mode) {
  return mode == DeploymentMode::kDirect ? "direct-ranker"
                                         : "exact-d3-residual";
}

struct ModelDecision {
  int action = -1;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::size_t cache_entries = 0;
};

ModelDecision chooseModelAction(const State& source,
                                const RankNetwork& network,
                                const features::Normalizer& normalizer,
                                DeploymentMode mode) {
  bool mirrored = false;
  const State canonical_state =
      cfpi::detail::canonicalState(source, mirrored);
  const features::PublicState root = features::publicState(canonical_state);
  const RootModelScores direct =
      directRootScores(root, network, normalizer);
  std::vector<double> values = direct.values;
  ModelDecision result;
  result.work = direct.work;
  if (mode == DeploymentMode::kResidualD3) {
    const ExactValues exact = exactRootValues(root);
    if (exact.actions != direct.actions) {
      throw std::runtime_error("residual action order mismatch");
    }
    values = residualScores(exact.values, direct.values);
    result.work += exact.work;
    result.nodes = exact.nodes;
    result.cache_entries = exact.cache_entries;
  }
  const int best = bestIndex(values);
  if (best < 0) return result;
  const int canonical_action = direct.actions[static_cast<std::size_t>(best)];
  result.action = mirrored ? kBoardSize - 1 - canonical_action
                           : canonical_action;
  return result;
}

struct GameResult {
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::size_t peak_cache_entries = 0;
  std::uint64_t peak_rss_bytes = 0;
  double elapsed_seconds = 0;
};

GameResult runPolicyGame(std::uint32_t seed,
                         const RankNetwork& network,
                         const features::Normalizer& normalizer,
                         DeploymentMode mode, bool baseline,
                         std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  GameResult result;
  const cfpi::BehaviorOptions exact_options = behaviorOptions(3, 5);
  while (!state.game_over && state.moves_played < kPolicyMaximumMoves) {
    int action = -1;
    if (baseline) {
      cfpi::BehaviorMetrics metrics;
      action = cfpi::chooseBehaviorAction(state, exact_options, &metrics);
      if (!metrics.complete || metrics.completed_depth != 3) {
        throw std::runtime_error("screen exact-d3 search incomplete");
      }
      result.work += metrics.work;
      result.nodes += metrics.nodes;
      result.peak_cache_entries =
          std::max(result.peak_cache_entries, metrics.cache_entries);
    } else {
      const ModelDecision decision =
          chooseModelAction(state, network, normalizer, mode);
      action = decision.action;
      result.work += decision.work;
      result.nodes += decision.nodes;
      result.peak_cache_entries =
          std::max(result.peak_cache_entries, decision.cache_entries);
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("screen policy selected illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("screen policy transition failed");
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
                             const RankNetwork& network,
                             const features::Normalizer& normalizer,
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
            seed, network, normalizer, mode, true,
            std::string(phase) + "-exact-d3");
        result.candidate[static_cast<std::size_t>(game)] = runPolicyGame(
            seed, network, normalizer, mode, false,
            std::string(phase) + '-' + std::string(deploymentName(mode)));
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

struct PolicySummary {
  int games = 0;
  double mean_score = 0;
  double mean_moves = 0;
  double work_per_move = 0;
  double moves_per_game_second = 0;
  double aggregate_game_seconds = 0;
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
  result.work_per_move =
      static_cast<double>(work) / std::max<std::uint64_t>(1, moves);
  result.moves_per_game_second =
      static_cast<double>(moves) /
      std::max(1.0e-9, result.aggregate_game_seconds);
  return result;
}

struct PairedSummary {
  double mean_score_difference = 0;
  double mean_move_difference = 0;
  int wins = 0;
  int ties = 0;
  int losses = 0;
};

PairedSummary pairedPolicy(const PolicyCohort& cohort) {
  if (cohort.baseline.size() != cohort.candidate.size() ||
      cohort.baseline.empty()) {
    throw std::invalid_argument("invalid paired cohort");
  }
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

struct DatasetSummary {
  int groups = 0;
  int actions = 0;
  int exact_groups = 0;
  int on_policy_groups = 0;
  int perturbed_groups = 0;
  double mean_action_return = 0;
  double mean_action_moves = 0;
  double mean_action_score = 0;
  std::uint64_t label_policy_work = 0;
};

DatasetSummary summarizeDataset(const Dataset& dataset) {
  DatasetSummary result;
  result.groups = static_cast<int>(dataset.groups.size());
  for (const SiblingGroup& group : dataset.groups) {
    switch (group.kind) {
      case RollInKind::kExactD3:
        ++result.exact_groups;
        break;
      case RollInKind::kOnPolicyD2:
        ++result.on_policy_groups;
        break;
      case RollInKind::kPerturbedD2:
        ++result.perturbed_groups;
        break;
    }
    result.label_policy_work += group.label_policy_work;
    for (const SiblingAction& action : group.actions) {
      ++result.actions;
      result.mean_action_return += action.mean_return;
      result.mean_action_moves += action.mean_moves;
      result.mean_action_score += action.mean_score;
    }
  }
  if (result.actions > 0) {
    result.mean_action_return /= result.actions;
    result.mean_action_moves /= result.actions;
    result.mean_action_score /= result.actions;
  }
  return result;
}

void saveModel(const std::string& path, const RankNetwork& network,
               const features::Normalizer& normalizer) {
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("could not open ranker model");
  const std::array<std::uint32_t, 6> header{{
      0x5341'524bu, 1u, features::kCategoryCount, kRankHidden,
      features::kMetricCount, kContinuationTapes,
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
  write(network.parameters.bias);
  write(network.parameters.output_weight);
  if (!output) throw std::runtime_error("could not write ranker model");
}

void writeActionMetrics(std::ostream& output, const ActionMetrics& result) {
  output << "{\"groups\":" << result.groups
         << ",\"pairs\":" << result.pairs
         << ",\"top1Accuracy\":" << result.top1_accuracy
         << ",\"pairwiseAccuracy\":" << result.pairwise_accuracy
         << ",\"meanRegret\":" << result.mean_regret
         << ",\"meanWithinStateSpearman\":" << result.mean_spearman
         << '}';
}

void writeDatasetSummary(std::ostream& output,
                         const DatasetSummary& result) {
  output << "{\"groups\":" << result.groups
         << ",\"actions\":" << result.actions
         << ",\"exactD3Groups\":" << result.exact_groups
         << ",\"onPolicyD2Groups\":" << result.on_policy_groups
         << ",\"perturbedD2Groups\":" << result.perturbed_groups
         << ",\"meanActionReturn\":" << result.mean_action_return
         << ",\"meanActionMoves\":" << result.mean_action_moves
         << ",\"meanActionScore\":" << result.mean_action_score
         << ",\"labelPolicyWork\":" << result.label_policy_work << '}';
}

void writePolicySummary(std::ostream& output,
                        const PolicySummary& result) {
  output << "{\"games\":" << result.games
         << ",\"meanScore\":" << result.mean_score
         << ",\"meanMoves\":" << result.mean_moves
         << ",\"workPerMove\":" << result.work_per_move
         << ",\"movesPerGameSecond\":" << result.moves_per_game_second
         << ",\"aggregateGameSeconds\":"
         << result.aggregate_game_seconds
         << ",\"peakCacheEntries\":" << result.peak_cache_entries
         << ",\"peakRssBytes\":" << result.peak_rss_bytes
         << ",\"censored\":" << result.censored << '}';
}

void writePairedSummary(std::ostream& output,
                        const PairedSummary& result) {
  output << "{\"meanScoreDifference\":"
         << result.mean_score_difference
         << ",\"meanMoveDifference\":" << result.mean_move_difference
         << ",\"wins\":" << result.wins
         << ",\"ties\":" << result.ties
         << ",\"losses\":" << result.losses << '}';
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
  const features::PublicState root = canonicalPublic(state);
  const features::PublicState reflected = features::mirror(root);

  bool common_tapes = true;
  bool safe_tapes = true;
  for (int tape = 0; tape < kContinuationTapes; ++tape) {
    const std::uint32_t seed = tapeSeed(root, tape);
    common_tapes = common_tapes && seed == tapeSeed(reflected, tape);
    safe_tapes = safe_tapes && (seed >> 24) != 0x7du &&
                 (seed >> 24) != 0xd7u;
    const PublicTape first{seed, 0};
    const PublicTape repeat{seed, 0};
    for (int event = 0; event < 32; ++event) {
      common_tapes = common_tapes &&
                     first.revealDisc(event) == repeat.revealDisc(event) &&
                     first.nextDiscForMove(event) ==
                         repeat.nextDiscForMove(event);
    }
  }

  std::vector<features::Label> labels{{root, 0, 0, 0}};
  const features::Normalizer normalizer = features::fitNormalizer(labels);
  RankNetwork network;
  const features::Engineered metrics =
      normalizer.apply(features::rawEngineered(root));
  const bool reflection_safe =
      network.score(root, metrics) == network.score(reflected, metrics);
  const ModelDecision first = chooseModelAction(
      features::materialize(root), network, normalizer,
      DeploymentMode::kDirect);
  State reflected_state = features::materialize(reflected);
  const ModelDecision mirrored = chooseModelAction(
      reflected_state, network, normalizer, DeploymentMode::kDirect);
  const bool action_reflection_safe =
      mirrored.action == kBoardSize - 1 - first.action;

  Origin origin{kCollectionStart, RollInKind::kExactD3, 15, root};
  Dataset training;
  training.groups.push_back(makeGroup(origin));
  Dataset development;
  origin.source_game = kCollectionStart + kTrainingGames;
  origin.root = reflected;
  development.groups.push_back(makeGroup(origin));
  const PurgeStats purge =
      purgeDevelopmentOverlap(training, development);
  const bool overlap_purged = purge.overlapping_groups == 1 &&
                              development.groups.empty();

  SiblingGroup toy = makeGroup(
      {kCollectionStart, RollInKind::kExactD3, 15, root});
  for (std::size_t action = 0; action < toy.actions.size(); ++action) {
    toy.actions[action].mean_return = static_cast<double>(action);
    toy.actions[action].advantage =
        static_cast<double>(action) - toy.actions.size() / 2.0;
    for (SuccessorBranch& branch : toy.actions[action].branches) {
      if (!branch.terminal) {
        branch.metrics = normalizer.apply(
            features::rawEngineered(branch.state));
      }
    }
  }
  RankGradient gradient;
  const double toy_loss = accumulateGroup(network, toy, gradient);
  const bool listwise_finite = std::isfinite(toy_loss) && toy_loss > 0;

  ActionMetrics baseline;
  baseline.mean_regret = 2.0;
  ActionMetrics candidate;
  candidate.top1_accuracy = 0.5;
  candidate.pairwise_accuracy = 0.7;
  candidate.mean_regret = 1.0;
  const bool gate_enforced = strongHeldout(candidate, baseline) &&
                             !strongHeldout(baseline, candidate);
  const bool legal = isLegal(state.board, first.action);
  const bool bounded = first.work <= kBoardSize * kChanceStrata &&
                       kExactMaximumWork == 1'000'000 &&
                       kExactMaximumCache == 40'000;
  const bool seed_families_safe =
      (kCollectionStart >> 24) != 0x7du &&
      (kCollectionStart >> 24) != 0xd7u &&
      (kScreenStart >> 24) != 0x7du &&
      (kScreenStart >> 24) != 0xd7u &&
      (kConfirmationStart >> 24) != 0x7du &&
      (kConfirmationStart >> 24) != 0xd7u;
  const bool passed = common_tapes && safe_tapes && reflection_safe &&
                      action_reflection_safe && overlap_purged &&
                      listwise_finite && gate_enforced && legal && bounded &&
                      seed_families_safe && kLevelBonus == 7'000;
  output << "SIBLING_ADVANTAGE_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"commonPublicTapes\":"
         << (common_tapes ? "true" : "false")
         << ",\"safeTapeFamilies\":"
         << (safe_tapes ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe && action_reflection_safe ? "true" : "false")
         << ",\"overlapPurged\":"
         << (overlap_purged ? "true" : "false")
         << ",\"listwisePairwiseFinite\":"
         << (listwise_finite ? "true" : "false")
         << ",\"heldoutGateEnforced\":"
         << (gate_enforced ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false")
         << ",\"bounded\":" << (bounded ? "true" : "false")
         << ",\"seedFamiliesSafe\":"
         << (seed_families_safe ? "true" : "false")
         << ",\"levelBonus\":" << kLevelBonus << "}\n";
  return passed;
}

struct ProgramOptions {
  std::string artifact = "/tmp/drop7-sibling-advantage-ranker.json";
  std::string model = "/tmp/drop7-sibling-advantage-ranker.bin";
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
  std::uint64_t rollin_work = 0;
  int collected_origins = 0;
  for (const RollInGame& game : rollins) {
    rollin_work += game.search_work;
    collected_origins += static_cast<int>(game.origins.size());
  }
  Dataset training = buildDataset(rollins, 0, kTrainingGames);
  Dataset development = buildDataset(
      rollins, kTrainingGames, kTotalCollectionGames);
  const PurgeStats purge = purgeDevelopmentOverlap(training, development);
  if (training.groups.empty() || development.groups.size() < 4) {
    throw std::runtime_error("sibling dataset empty after overlap purge");
  }

  labelDataset(training, "training");
  labelDataset(development, "development");
  const features::Normalizer normalizer =
      fitTrainingNormalizer(training);
  prepareMetrics(training, normalizer);
  prepareMetrics(development, normalizer);

  RankNetwork network;
  const TrainingResult training_result = train(network, training);
  const ActionMetrics training_direct = directMetrics(training, network);
  const HeldoutMetrics heldout = evaluateHeldout(development, network);
  const DeploymentMode deployment = chooseDeployment(heldout);
  const ActionMetrics& selected = deploymentMetrics(heldout, deployment);
  const double regret_reduction =
      heldout.exact.mean_regret - selected.mean_regret;
  const bool heldout_passed = strongHeldout(selected, heldout.exact);
  saveModel(options.model, network, normalizer);

  PolicyCohort screen;
  PolicySummary screen_baseline;
  PolicySummary screen_candidate;
  PairedSummary screen_paired;
  bool screen_passed = false;
  if (heldout_passed) {
    screen = runPolicyCohort(kScreenStart, kScreenGames, network,
                             normalizer, deployment, "screen");
    screen_baseline = summarizePolicy(screen.baseline);
    screen_candidate = summarizePolicy(screen.candidate);
    screen_paired = pairedPolicy(screen);
    screen_passed = screen_paired.mean_score_difference > 0 &&
                    screen_paired.mean_move_difference > 0;
  }

  PolicyCohort confirmation;
  PolicySummary confirmation_baseline;
  PolicySummary confirmation_candidate;
  PairedSummary confirmation_paired;
  bool confirmed = false;
  if (screen_passed) {
    confirmation = runPolicyCohort(
        kConfirmationStart, kConfirmationGames, network, normalizer,
        deployment, "confirmation");
    confirmation_baseline = summarizePolicy(confirmation.baseline);
    confirmation_candidate = summarizePolicy(confirmation.candidate);
    confirmation_paired = pairedPolicy(confirmation);
    confirmed = confirmation_paired.mean_score_difference > 0 &&
                confirmation_paired.mean_move_difference > 0;
  }

  const DatasetSummary training_summary = summarizeDataset(training);
  const DatasetSummary development_summary = summarizeDataset(development);
  const double elapsed_seconds = std::chrono::duration<double>(
                                     std::chrono::steady_clock::now() -
                                     started)
                                     .count();
  std::ofstream artifact(options.artifact);
  if (!artifact) throw std::runtime_error("could not open ranker artifact");
  artifact << std::setprecision(10)
           << "{\n  \"format\": "
              "\"drop7-sibling-advantage-ranker-v1\",\n"
           << "  \"publicStateOnly\": true,\n"
           << "  \"commonRandomNumbers\": true,\n"
           << "  \"hiddenRealFuturesUsed\": false,\n"
           << "  \"levelBonus\": " << kLevelBonus << ",\n"
           << "  \"collectionStart\": " << kCollectionStart << ",\n"
           << "  \"trainingGames\": " << kTrainingGames << ",\n"
           << "  \"developmentGames\": " << kDevelopmentGames << ",\n"
           << "  \"collectedOrigins\": " << collected_origins << ",\n"
           << "  \"rollInWork\": " << rollin_work << ",\n"
           << "  \"rollIns\": {\"exactD3PerGame\":"
           << kExactRootsPerGame << ",\"onPolicyD2S3PerGame\":"
           << kOnPolicyRootsPerGame << ",\"perturbedD2S3PerGame\":"
           << kPerturbedRootsPerGame << "},\n"
           << "  \"continuation\": {\"tapes\":"
           << kContinuationTapes << ",\"horizon\":"
           << kContinuationHorizon << ",\"depth1Strata3Tapes\":"
           << kContinuationTapes - kDepthTwoTapes
           << ",\"depth2Strata3Tapes\":" << kDepthTwoTapes
           << ",\"return\":\"moves + score / 7000\"},\n"
           << "  \"chanceStrata\": " << kChanceStrata << ",\n"
           << "  \"trainingDataset\": ";
  writeDatasetSummary(artifact, training_summary);
  artifact << ",\n  \"developmentDataset\": ";
  writeDatasetSummary(artifact, development_summary);
  artifact << ",\n  \"trainingDuplicateRoots\": "
           << training.duplicate_roots
           << ",\n  \"developmentDuplicateRoots\": "
           << development.duplicate_roots
           << ",\n  \"overlapGroupsRemoved\": "
           << purge.overlapping_groups
           << ",\n  \"overlapStatesFound\": "
           << purge.overlapping_states
           << ",\n  \"ranker\": {\"hidden\":" << kRankHidden
           << ",\"epochs\":" << kTrainingEpochs
           << ",\"parameterBytes\":" << network.parameterBytes()
           << ",\"firstLoss\":" << training_result.first_loss
           << ",\"finalLoss\":" << training_result.final_loss
           << ",\"loss\":\"listwise-softmax + paired-logistic\"},\n"
           << "  \"trainingDirectRanking\": ";
  writeActionMetrics(artifact, training_direct);
  artifact << ",\n  \"developmentExactD3Ranking\": ";
  writeActionMetrics(artifact, heldout.exact);
  artifact << ",\n  \"developmentDirectRanking\": ";
  writeActionMetrics(artifact, heldout.direct);
  artifact << ",\n  \"developmentResidualRanking\": ";
  writeActionMetrics(artifact, heldout.residual);
  artifact << ",\n  \"developmentExactWork\": " << heldout.exact_work
           << ",\n  \"developmentExactNodes\": "
           << heldout.exact_nodes
           << ",\n  \"developmentPeakCacheEntries\": "
           << heldout.peak_cache_entries
           << ",\n  \"selectedDeployment\": \""
           << deploymentName(deployment)
           << "\",\n  \"heldoutGate\": {\"requiredTop1\":"
           << kRequiredTopOne << ",\"requiredPairwise\":"
           << kRequiredPairwise
           << ",\"requiresPositiveRegretReduction\":true,"
           << "\"observedRegretReduction\":" << regret_reduction
           << ",\"passed\":"
           << (heldout_passed ? "true" : "false") << "},\n"
           << "  \"screenSeedStart\": " << kScreenStart
           << ",\n  \"screen\": ";
  if (!heldout_passed) {
    artifact << "null";
  } else {
    artifact << "{\"exactD3\":";
    writePolicySummary(artifact, screen_baseline);
    artifact << ",\"candidate\":";
    writePolicySummary(artifact, screen_candidate);
    artifact << ",\"paired\":";
    writePairedSummary(artifact, screen_paired);
    artifact << ",\"exactTrajectories\":";
    writeTrajectories(artifact, screen.baseline);
    artifact << ",\"candidateTrajectories\":";
    writeTrajectories(artifact, screen.candidate);
    artifact << '}';
  }
  artifact << ",\n  \"screenPassed\": "
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
    writePairedSummary(artifact, confirmation_paired);
    artifact << ",\"exactTrajectories\":";
    writeTrajectories(artifact, confirmation.baseline);
    artifact << ",\"candidateTrajectories\":";
    writeTrajectories(artifact, confirmation.candidate);
    artifact << '}';
  }
  const std::string_view decision =
      !heldout_passed
          ? "reject-heldout"
          : (!screen_passed
                 ? "reject-screen"
                 : (confirmed ? "advance" : "reject-confirmation"));
  artifact << ",\n  \"confirmed\": "
           << (confirmed ? "true" : "false")
           << ",\n  \"decision\": \"" << decision
           << "\",\n  \"model\": \"" << options.model
           << "\",\n  \"peakRssBytes\": " << peakRssBytes()
           << ",\n  \"elapsedSeconds\": " << elapsed_seconds << "\n}\n";
  if (!artifact) throw std::runtime_error("could not write ranker artifact");

  output << std::fixed << std::setprecision(4)
         << "SIBLING_ADVANTAGE_RESULT {\"trainingGroups\":"
         << training.groups.size() << ",\"developmentGroups\":"
         << development.groups.size() << ",\"exactTop1\":"
         << heldout.exact.top1_accuracy << ",\"exactPairwise\":"
         << heldout.exact.pairwise_accuracy << ",\"directTop1\":"
         << heldout.direct.top1_accuracy << ",\"directPairwise\":"
         << heldout.direct.pairwise_accuracy << ",\"residualTop1\":"
         << heldout.residual.top1_accuracy
         << ",\"residualPairwise\":"
         << heldout.residual.pairwise_accuracy << ",\"deployment\":\""
         << deploymentName(deployment) << "\",\"regretReduction\":"
         << regret_reduction << ",\"heldoutPassed\":"
         << (heldout_passed ? "true" : "false")
         << ",\"screenRan\":" << (heldout_passed ? "true" : "false")
         << ",\"screenPassed\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmationRan\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmed\":" << (confirmed ? "true" : "false")
         << ",\"decision\":\"" << decision
         << "\",\"artifact\":\"" << options.artifact << "\"}\n";
  return 0;
}

}  // namespace drop7::sibling_advantage_ranker

#ifndef DROP7_SIBLING_ADVANTAGE_RANKER_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string(argv[1]) == "--self-test") {
      return drop7::sibling_advantage_ranker::selfTest(std::cout) ? 0 : 1;
    }
    if (argc >= 2 && std::string(argv[1]) == "--run") {
      const auto options =
          drop7::sibling_advantage_ranker::parseOptions(argc, argv, 2);
      return drop7::sibling_advantage_ranker::run(options, std::cout);
    }
    std::cerr << "usage: drop7_sibling_advantage_ranker --self-test | "
                 "--run [--artifact PATH] [--model PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_sibling_advantage_ranker: " << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
}
#endif
