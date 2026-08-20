#define main drop7_constructive_spectrum_direct_ranker_entrypoint
#include "../constructive-spectrum/constructive-spectrum.cpp"
#undef main

#include <cstdio>
#include <cstring>
#include <list>
#include <unordered_map>
#include <unordered_set>

// Trains a development-only direct sibling ranker on the previously evaluated
// 0x3d6c1000..0x3d6c13ff panel-value corpus.  It has no gameplay screen.  Every
// example is a complete legal-action panel at a public root; continuation
// states are not sampled into a state-value reservoir.
namespace drop7::direct_sibling_ranker {

namespace constructive = drop7::constructive_spectrum;
namespace fair = drop7::fair_only_horizon;
namespace detail = drop7::cfpi::detail;

using Clock = std::chrono::steady_clock;
using PublicState = constructive::PublicState;

constexpr std::uint32_t kOriginSeedStart = 0x3d6c'1000u;
constexpr std::uint32_t kOriginSeedEndExclusive = 0x3d6c'1400u;
constexpr int kOriginGames = 1'024;
constexpr int kD1OriginGames = 512;
constexpr int kConstructiveOriginGames = 512;
constexpr int kMaximumRootsPerGame = 8;
constexpr std::array<int, kMaximumRootsPerGame> kMilestones{{
    5, 10, 15, 20, 25, 30, 40, 50,
}};
constexpr int kTrainingPerPolicy = 384;
constexpr int kHeldoutPerPolicy = 128;
constexpr int kCrossValidationFolds = 3;
constexpr std::uint32_t kSplitDomain = 0x5041'4e53u;
constexpr std::uint32_t kFoldDomain = 0x4453'5246u;

constexpr int kLabelScenarios = 15;
constexpr int kLabelHorizon = 100;
constexpr std::uint32_t kTapeDomain = 0x5041'4e4cu;
constexpr std::uint32_t kTapeDiscDomain = 0x5044'4953u;
constexpr std::uint32_t kTapeRevealDomain = 0x5052'564cu;
constexpr std::uint32_t kEventMultiplier = 0x9e37'79b9u;

constexpr int kRootScenarios = kBoardSize;
constexpr std::uint32_t kRootSuccessorDomain = 0x4453'5253u;
constexpr int kFeatureCount = 96;
constexpr int kHeads = 5;
constexpr int kBoardTokens = 10;
constexpr int kBoardCategories = kCellCount * kBoardTokens;
constexpr int kNextCategories = kBoardSize;
constexpr int kPhaseCategories = kMovesPerLevel;
constexpr int kActionCategories = kBoardSize;
constexpr int kCategoryCount = kBoardCategories + kNextCategories +
                               kPhaseCategories + kActionCategories;
constexpr int kActiveCategories = kCellCount + 3;

constexpr int kEpochs = 16;
constexpr int kBatchRoots = 32;
constexpr float kLearningRate = 0.0015f;
constexpr float kWeightDecay = 1.0e-5f;
constexpr float kGradientNorm = 3.0f;
constexpr float kListTemperature = 0.35f;
constexpr std::uint32_t kNetworkDomain = 0x4453'4e4eu;
constexpr std::uint32_t kShuffleDomain = 0x4453'5348u;

constexpr double kNearTieFraction = 0.10;
constexpr double kDecisiveFraction = 0.50;
constexpr int kCalibrationBins = 10;
constexpr double kRequiredTopOneGain = 0.02;
constexpr double kRequiredPairwiseGain = 0.03;
constexpr double kRequiredRegretRatio = 0.85;

constexpr double kWallLimitSeconds = 75.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr std::uint64_t kDeployLimitBytes = 512ull * 1024ull;
constexpr int kMaximumThreads = 8;

constexpr std::uint64_t kCheckpointMagic = 0x4437'4453'524b'3031ull;
constexpr std::uint32_t kCheckpointVersion = 1;

static_assert(kLevelBonus == 17'000);
static_assert(kOriginSeedEndExclusive - kOriginSeedStart == kOriginGames);
static_assert(kD1OriginGames + kConstructiveOriginGames == kOriginGames);
static_assert(kTrainingPerPolicy + kHeldoutPerPolicy == 512);
static_assert(kTrainingPerPolicy % kCrossValidationFolds == 0);
static_assert(kLabelScenarios == 15 && kLabelHorizon == 100);
static_assert(kRootScenarios == kBoardSize);
static_assert(kCategoryCount == 509);
static_assert(constructive::kMetricCount == 29);

std::uint64_t mix64(std::uint64_t value) {
  value ^= value >> 30u;
  value *= 0xbf58'476d'1ce4'e5b9ull;
  value ^= value >> 27u;
  value *= 0x94d0'49bb'1331'11ebull;
  return value ^ (value >> 31u);
}

std::uint64_t peakRssBytes() { return constructive::peakRssBytes(); }

void enforceResources() {
  if (peakRssBytes() > kRssLimitBytes) {
    throw std::runtime_error("direct sibling ranker exceeded 256 MiB RSS");
  }
}

struct Deadline {
  Clock::time_point started = Clock::now();
  double seconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }
  void check() const {
    if (seconds() > kWallLimitSeconds) {
      throw std::runtime_error("direct sibling ranker exceeded 75 minutes");
    }
    enforceResources();
  }
};

// Exact fixed fair-D4 reference, copied without semantic changes from
// approaches/fair-expectimax/reference/fair-only-depth4.cpp (SHA-256
// 1cb42629db07b17850045bf3e5678c1fed5b58c73ab38bcfb699c94ee34fe6aa).
// It is invoked only after architecture selection and final fitting, on the
// previously evaluated development holdout.  Its Q values are never training
// targets or model inputs.
namespace d4_benchmark {

constexpr int kCandidateDepth = 4;
constexpr int kChanceSamples = fair::kChanceSamples;
constexpr std::uint64_t kMaximumWork = 3'200'000;
constexpr std::size_t kMaximumCacheEntries = 60'000;
static_assert(fair::kPolicySeed == 0xd707'5eedu);
static_assert(fair::kTerminalUtility == -1'000'000.0);

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
  std::uint64_t cache_hits = 0;
};

void checkBudget(const SearchContext& context) {
  if (context.work >= kMaximumWork) throw WorkLimitReached{};
}

void cacheValue(SearchContext& context, std::string key, double value) {
  const auto prior = context.cache.find(key);
  if (prior != context.cache.end()) {
    context.order.erase(prior->second.order);
    context.cache.erase(prior);
  }
  while (context.cache.size() >= kMaximumCacheEntries) {
    const std::string& oldest = context.order.front();
    context.cache.erase(oldest);
    context.order.pop_front();
  }
  context.order.push_back(key);
  const auto order = std::prev(context.order.end());
  context.cache.emplace(std::move(key), CacheEntry{value, order});
}

double bestFutureValue(const State& state, int depth,
                       SearchContext& context);

struct ActionValue {
  double value = 0.0;
  double expected_score = 0.0;
};

ActionValue evaluateAction(const State& state, int column, int depth,
                           SearchContext& context) {
  const std::uint32_t state_seed = detail::scenarioSeedForState(
      state, fair::kPolicySeed, depth);
  ActionValue result;
  for (int sample = 0; sample < kChanceSamples; ++sample) {
    checkBudget(context);
    detail::StratifiedRandom random{
        state_seed, sample, kChanceSamples, 0};
    MoveResult move;
    const bool played =
        detail::playMoveSampled(state, column, random, move);
    ++context.work;
    if (!played) {
      result.value += fair::kTerminalUtility;
      continue;
    }
    const double score_delta = static_cast<double>(move.score_delta);
    result.expected_score += score_delta;
    if (move.state.game_over) {
      result.value += score_delta + fair::kTerminalUtility;
      continue;
    }
    move.state.score = 0;
    move.state.next_disc = detail::sampledNextDisc(
        state_seed, sample, kChanceSamples);
    bool ignored = false;
    const State next = detail::canonicalState(move.state, ignored);
    result.value +=
        score_delta + bestFutureValue(next, depth - 1, context);
  }
  result.value /= kChanceSamples;
  result.expected_score /= kChanceSamples;
  return result;
}

double evaluateLeaf(const State& state, SearchContext& context) {
  checkBudget(context);
  ++context.work;
  const double value = fair::fairLeaf(state);
  if (!std::isfinite(value)) {
    throw std::runtime_error("direct-ranker D4 leaf was non-finite");
  }
  return value;
}

double bestFutureValue(const State& state, int depth,
                       SearchContext& context) {
  ++context.nodes;
  checkBudget(context);
  if (state.game_over) return fair::kTerminalUtility;
  if (depth == 0) return evaluateLeaf(state, context);
  const std::string key = detail::dynamicStateKey(state, depth);
  const auto cached = context.cache.find(key);
  if (cached != context.cache.end()) {
    ++context.cache_hits;
    const double value = cached->second.value;
    context.order.splice(context.order.end(), context.order,
                         cached->second.order);
    return value;
  }
  double best = -std::numeric_limits<double>::infinity();
  for (const int column : detail::kColumnOrder) {
    if (!isLegal(state.board, column)) continue;
    best = std::max(best,
                    evaluateAction(state, column, depth, context).value);
  }
  if (!std::isfinite(best)) best = fair::kTerminalUtility;
  cacheValue(context, key, best);
  return best;
}

struct RootEvaluation {
  int action = -1;
  double value = -std::numeric_limits<double>::infinity();
  std::array<double, kBoardSize> values{};
  std::array<double, kBoardSize> expected_scores{};
};

RootEvaluation rootDecision(const State& canonical, int depth,
                            SearchContext& context) {
  RootEvaluation result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  result.expected_scores.fill(-std::numeric_limits<double>::infinity());
  for (const int column : detail::kColumnOrder) {
    if (!isLegal(canonical.board, column)) continue;
    const ActionValue candidate =
        evaluateAction(canonical, column, depth, context);
    result.values[column] = candidate.value;
    result.expected_scores[column] = candidate.expected_score;
    if (candidate.value > result.value) {
      result.value = candidate.value;
      result.action = column;
    }
  }
  return result;
}

struct SearchDecision {
  int action = -1;
  int completed_depth = 0;
  bool complete = false;
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
  std::uint64_t cache_hits = 0;
  std::size_t cache_entries = 0;
  std::array<double, kBoardSize> root_values{};
};

SearchDecision chooseDepth4Action(const State& source) {
  if (source.game_over) return {};
  bool mirrored = false;
  const State canonical = detail::canonicalState(source, mirrored);
  SearchContext context;
  RootEvaluation completed;
  int completed_depth = 0;
  for (int depth = 1; depth <= kCandidateDepth; ++depth) {
    try {
      completed = rootDecision(canonical, depth, context);
      if (completed.action < 0) break;
      completed_depth = depth;
    } catch (const WorkLimitReached&) {
      break;
    }
  }
  int action = completed.action;
  if (action < 0) action = centerFirstMove(canonical.board);
  SearchDecision result;
  result.action = mirrored ? kBoardSize - 1 - action : action;
  result.completed_depth = completed_depth;
  result.complete = completed_depth == kCandidateDepth;
  result.nodes = context.nodes;
  result.work = context.work;
  result.cache_hits = context.cache_hits;
  result.cache_entries = context.cache.size();
  result.root_values.fill(-std::numeric_limits<double>::infinity());
  if (completed_depth > 0) {
    for (int canonical_column = 0; canonical_column < kBoardSize;
         ++canonical_column) {
      const int source_column = mirrored
                                    ? kBoardSize - 1 - canonical_column
                                    : canonical_column;
      result.root_values[source_column] = completed.values[canonical_column];
    }
  }
  return result;
}

}  // namespace d4_benchmark

bool allowedOriginSeed(std::uint32_t seed) {
  const std::uint8_t prefix = static_cast<std::uint8_t>(seed >> 24u);
  return seed >= kOriginSeedStart && seed < kOriginSeedEndExclusive &&
         prefix != 0x4d && prefix != 0x7d && prefix != 0xd7;
}

void requireOriginSeed(std::uint32_t seed) {
  if (!allowedOriginSeed(seed)) {
    throw std::invalid_argument(
        "seed outside burned direct-ranker origin corpus");
  }
}

PublicState canonicalPublic(const PublicState& source) {
  bool ignored = false;
  return constructive::canonicalPublic(source, ignored);
}

std::string publicKey(const PublicState& source) {
  const PublicState state = canonicalPublic(source);
  std::string key;
  key.reserve(kCellCount + 2);
  for (const std::uint8_t token : state.board) {
    key.push_back(static_cast<char>(token));
  }
  key.push_back(static_cast<char>(state.next_disc));
  key.push_back(static_cast<char>(state.moves_remaining));
  return key;
}

std::uint64_t publicHash(const PublicState& source) {
  const PublicState state = canonicalPublic(source);
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint64_t>(cell + 1u);
    hash *= 0x0000'0100'0000'01b3ull;
  }
  hash ^= state.next_disc;
  hash *= 0x0000'0100'0000'01b3ull;
  hash ^= static_cast<std::uint64_t>(state.moves_remaining + 1u);
  hash *= 0x0000'0100'0000'01b3ull;
  return mix64(hash);
}

enum class OriginPolicy : std::uint8_t { kFairD1, kConstructive };

std::string_view policyName(OriginPolicy policy) {
  return policy == OriginPolicy::kFairD1 ? "fair-d1"
                                         : "constructive-spectrum";
}

struct SplitTable {
  std::array<bool, kOriginGames> heldout{};
  std::array<std::int8_t, kOriginGames> fold{};
};

SplitTable buildSplit() {
  SplitTable result;
  result.fold.fill(-1);
  for (int policy = 0; policy < 2; ++policy) {
    const int base = policy * 512;
    std::vector<std::pair<std::uint32_t, int>> order;
    order.reserve(512);
    for (int offset = 0; offset < 512; ++offset) {
      const int game = base + offset;
      const std::uint32_t seed =
          kOriginSeedStart + static_cast<std::uint32_t>(game);
      order.push_back({mix32(seed ^ kSplitDomain), game});
    }
    std::sort(order.begin(), order.end());
    for (int rank = 0; rank < kHeldoutPerPolicy; ++rank) {
      result.heldout[order[rank].second] = true;
    }
    std::vector<std::pair<std::uint32_t, int>> training;
    training.reserve(kTrainingPerPolicy);
    for (int offset = 0; offset < 512; ++offset) {
      const int game = base + offset;
      if (result.heldout[game]) continue;
      const std::uint32_t seed =
          kOriginSeedStart + static_cast<std::uint32_t>(game);
      training.push_back({mix32(seed ^ kFoldDomain), game});
    }
    std::sort(training.begin(), training.end());
    if (training.size() != kTrainingPerPolicy) {
      throw std::runtime_error("direct-ranker split size mismatch");
    }
    for (std::size_t rank = 0; rank < training.size(); ++rank) {
      result.fold[training[rank].second] =
          static_cast<std::int8_t>(rank % kCrossValidationFolds);
    }
  }
  std::array<std::array<int, kCrossValidationFolds>, 2> counts{};
  for (int game = 0; game < kOriginGames; ++game) {
    if (result.heldout[game]) {
      if (result.fold[game] != -1) {
        throw std::runtime_error("heldout origin assigned CV fold");
      }
      continue;
    }
    if (result.fold[game] < 0 ||
        result.fold[game] >= kCrossValidationFolds) {
      throw std::runtime_error("training origin missing CV fold");
    }
    ++counts[game >= 512][result.fold[game]];
  }
  for (const auto& half : counts) {
    for (const int count : half) {
      if (count != kTrainingPerPolicy / kCrossValidationFolds) {
        throw std::runtime_error("unbalanced whole-origin CV folds");
      }
    }
  }
  return result;
}

struct Root {
  std::uint32_t origin_seed = 0;
  OriginPolicy policy = OriginPolicy::kFairD1;
  int milestone = 0;
  int fold = -1;
  bool heldout = false;
  PublicState state{};
};

std::vector<Root> collectGameRoots(std::uint32_t seed,
                                   const SplitTable& split,
                                   const Deadline& deadline) {
  requireOriginSeed(seed);
  const int game = static_cast<int>(seed - kOriginSeedStart);
  const OriginPolicy policy = game < kD1OriginGames
                                  ? OriginPolicy::kFairD1
                                  : OriginPolicy::kConstructive;
  State state = initialHeadlessState(seed);
  std::vector<Root> result;
  result.reserve(kMaximumRootsPerGame);
  int milestone = 0;
  while (!state.game_over && milestone < kMaximumRootsPerGame) {
    if ((state.moves_played & 7) == 0) deadline.check();
    if (state.moves_played == kMilestones[milestone]) {
      int legal_count = 0;
      legalColumns(state.board, legal_count);
      if (legal_count >= 2) {
        result.push_back({seed, policy, kMilestones[milestone],
                          split.fold[game], split.heldout[game],
                          canonicalPublic(constructive::publicState(state))});
      }
      ++milestone;
      if (milestone >= kMaximumRootsPerGame) break;
    }
    const PublicState public_state = constructive::publicState(state);
    const int action = policy == OriginPolicy::kFairD1
                           ? constructive::chooseFairD1(public_state)
                           : constructive::chooseAction(public_state).action;
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("direct-ranker roll-in selected illegal move");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("direct-ranker roll-in transition failed");
    }
  }
  return result;
}

struct RootCollection {
  std::vector<Root> training;
  std::vector<Root> heldout;
  int duplicate_training = 0;
  int duplicate_heldout = 0;
  int heldout_overlap_purged = 0;
};

RootCollection collectAllRoots(const SplitTable& split, int threads,
                               const Deadline& deadline) {
  std::vector<std::vector<Root>> by_game(kOriginGames);
  std::atomic<int> next{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(threads, kOriginGames); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next.fetch_add(1);
        if (game >= kOriginGames) return;
        const std::uint32_t seed =
            kOriginSeedStart + static_cast<std::uint32_t>(game);
        by_game[game] = collectGameRoots(seed, split, deadline);
        if ((game & 63) == 63) {
          std::cerr << "direct roots replayed " << game + 1 << '/'
                    << kOriginGames << '\n';
        }
      }
    }));
  }
  for (auto& worker : workers) worker.get();

  RootCollection result;
  std::unordered_set<std::string> training_keys;
  std::unordered_set<std::string> heldout_keys;
  for (int game = 0; game < kOriginGames; ++game) {
    for (Root& root : by_game[game]) {
      const std::string key = publicKey(root.state);
      if (root.heldout) {
        if (!heldout_keys.insert(key).second) {
          ++result.duplicate_heldout;
          continue;
        }
        result.heldout.push_back(std::move(root));
      } else {
        if (!training_keys.insert(key).second) {
          ++result.duplicate_training;
          continue;
        }
        result.training.push_back(std::move(root));
      }
    }
  }
  std::vector<Root> clean;
  clean.reserve(result.heldout.size());
  for (Root& root : result.heldout) {
    if (training_keys.contains(publicKey(root.state))) {
      ++result.heldout_overlap_purged;
    } else {
      clean.push_back(std::move(root));
    }
  }
  result.heldout = std::move(clean);
  if (result.training.empty() || result.heldout.empty()) {
    throw std::runtime_error("direct-ranker root collection empty");
  }
  enforceResources();
  return result;
}

struct PublicTape {
  std::uint32_t seed = 0;
  int move = 0;

  std::uint8_t nextDiscForMove(int move_index) const {
    const std::uint32_t bits =
        mix32(seed ^ kTapeDiscDomain ^
              (static_cast<std::uint32_t>(move_index + 1) *
               kEventMultiplier));
    return static_cast<std::uint8_t>(
        ((static_cast<std::uint64_t>(bits) * kBoardSize) >> 32u) + 1u);
  }

  std::uint8_t revealDisc(int event) const {
    const std::uint32_t bits =
        mix32(seed ^ kTapeRevealDomain ^
              (static_cast<std::uint32_t>(move + 1) * 0x85eb'ca6bu) ^
              (static_cast<std::uint32_t>(event + 1) * 0xc2b2'ae35u));
    return static_cast<std::uint8_t>(
        ((static_cast<std::uint64_t>(bits) * kBoardSize) >> 32u) + 1u);
  }
};

struct PublicMoveRandom {
  const PublicTape& tape;
  int event = 0;
  std::uint8_t nextDisc() { return tape.revealDisc(event++); }
};

bool playTapeMove(State& state, int action, PublicTape& tape,
                  MoveResult& result) {
  PublicMoveRandom random{tape, 0};
  if (!detail::playMoveSampled(state, action, random, result)) return false;
  ++tape.move;
  state = result.state;
  if (!state.game_over) state.next_disc = tape.nextDiscForMove(tape.move);
  result.state = state;
  return true;
}

std::uint32_t tapeSeed(const PublicState& root, int scenario) {
  if (scenario < 0 || scenario >= kLabelScenarios) {
    throw std::invalid_argument("invalid direct-ranker label scenario");
  }
  return static_cast<std::uint32_t>(mix64(
      publicHash(root) ^ kTapeDomain ^
      (static_cast<std::uint64_t>(scenario + 1) *
       0x9e37'79b9'7f4a'7c15ull)));
}

struct ActionLabel {
  int action = -1;
  double mean_return = 0.0;
  double survival = 0.0;
  double clears = 0.0;
  double reveals = 0.0;
  double downside_return = 0.0;
};

struct Panel {
  Root root{};
  std::vector<ActionLabel> actions;
  std::uint64_t transitions = 0;
  std::uint64_t d1_work = 0;
};

struct ScenarioOutcome {
  double value = 0.0;
  int clears = 0;
  int reveals = 0;
  bool survived = false;
  std::uint64_t transitions = 0;
  std::uint64_t d1_work = 0;
};

ScenarioOutcome replayAction(const Root& root, int action, int scenario,
                             const Deadline& deadline) {
  State state = constructive::materialize(root.state);
  state.score = 0;
  state.level = 1;
  state.moves_played = 0;
  PublicTape tape{tapeSeed(root.state, scenario), 0};
  ScenarioOutcome result;
  MoveResult move;
  if (!playTapeMove(state, action, tape, move)) {
    throw std::runtime_error("direct-ranker forced action failed");
  }
  ++result.transitions;
  int moves = 1;
  for (const Wave& wave : move.waves) {
    result.clears += wave.cleared;
    result.reveals += wave.revealed;
  }
  while (!state.game_over && moves < kLabelHorizon) {
    if ((moves & 31) == 0) deadline.check();
    const PublicState public_state = constructive::publicState(state);
    fair::SearchContext context;
    const fair::RootEvaluation decision =
        fair::rootDecision(constructive::materialize(public_state), 1,
                           context);
    if (decision.action < 0 || context.work > 70 || !context.cache.empty()) {
      throw std::runtime_error("direct-ranker fair-D1 continuation failed");
    }
    result.d1_work += context.work;
    if (!playTapeMove(state, decision.action, tape, move)) {
      throw std::runtime_error("direct-ranker h100 transition failed");
    }
    ++result.transitions;
    ++moves;
    for (const Wave& wave : move.waves) {
      result.clears += wave.cleared;
      result.reveals += wave.revealed;
    }
  }
  result.survived = !state.game_over && moves == kLabelHorizon;
  result.value = static_cast<double>(moves) +
                 static_cast<double>(state.score) / 17'000.0;
  return result;
}

Panel labelPanel(const Root& root, const Deadline& deadline) {
  Panel result;
  result.root = root;
  const State canonical = constructive::materialize(root.state);
  for (const int action : constructive::kColumnOrder) {
    if (!isLegal(canonical.board, action)) continue;
    ActionLabel label;
    label.action = action;
    std::array<double, kLabelScenarios> returns{};
    for (int scenario = 0; scenario < kLabelScenarios; ++scenario) {
      const ScenarioOutcome outcome =
          replayAction(root, action, scenario, deadline);
      returns[scenario] = outcome.value;
      label.mean_return += outcome.value / kLabelScenarios;
      label.survival += static_cast<double>(outcome.survived) /
                        kLabelScenarios;
      label.clears += static_cast<double>(outcome.clears) / kLabelScenarios;
      label.reveals += static_cast<double>(outcome.reveals) / kLabelScenarios;
      result.transitions += outcome.transitions;
      result.d1_work += outcome.d1_work;
    }
    std::sort(returns.begin(), returns.end());
    label.downside_return =
        (returns[0] + returns[1] + returns[2]) / 3.0;
    result.actions.push_back(label);
  }
  if (result.actions.size() < 2) {
    throw std::runtime_error("direct-ranker panel has fewer than two actions");
  }
  return result;
}

struct LabelledCorpus {
  std::vector<Panel> training;
  std::vector<Panel> heldout;
  std::uint64_t transitions = 0;
  std::uint64_t d1_work = 0;
  double seconds = 0.0;
};

LabelledCorpus labelRoots(const RootCollection& roots, int threads,
                          const Deadline& deadline) {
  const Clock::time_point started = Clock::now();
  LabelledCorpus result;
  result.training.resize(roots.training.size());
  result.heldout.resize(roots.heldout.size());
  const int training_count = static_cast<int>(roots.training.size());
  const int total = training_count + static_cast<int>(roots.heldout.size());
  std::atomic<int> next{0};
  std::atomic<int> completed{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(threads, total); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int index = next.fetch_add(1);
        if (index >= total) return;
        if (index < training_count) {
          result.training[index] = labelPanel(roots.training[index], deadline);
        } else {
          result.heldout[index - training_count] =
              labelPanel(roots.heldout[index - training_count], deadline);
        }
        const int count = completed.fetch_add(1) + 1;
        if ((count & 127) == 0 || count == total) {
          std::cerr << "direct panels labelled " << count << '/' << total
                    << '\n';
        }
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  for (const auto* panels : {&result.training, &result.heldout}) {
    for (const Panel& panel : *panels) {
      result.transitions += panel.transitions;
      result.d1_work += panel.d1_work;
    }
  }
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  enforceResources();
  return result;
}

struct RootBranch {
  PublicState state{};
  double immediate_return = 0.0;
  bool terminal = false;
};

RootBranch rootSuccessor(const PublicState& canonical, int action,
                         int scenario) {
  if (scenario < 0 || scenario >= kRootScenarios ||
      !isLegal(canonical.board, action)) {
    throw std::invalid_argument("invalid direct-ranker root successor");
  }
  const State root = constructive::materialize(canonical);
  const std::uint32_t seed = detail::scenarioSeedForState(
      root, kRootSuccessorDomain, 0);
  detail::StratifiedRandom random{seed, scenario, kRootScenarios, 0};
  MoveResult move;
  if (!detail::playMoveSampled(root, action, random, move)) {
    throw std::runtime_error("direct-ranker successor transition failed");
  }
  RootBranch result;
  result.immediate_return =
      1.0 + static_cast<double>(move.score_delta) / 17'000.0;
  result.terminal = move.state.game_over;
  if (!result.terminal) {
    move.state.score = 0;
    move.state.level = 1;
    move.state.moves_played = 0;
    move.state.next_disc =
        detail::sampledNextDisc(seed, scenario, kRootScenarios);
    result.state = canonicalPublic(constructive::publicState(move.state));
  }
  return result;
}

struct RawAction {
  int action = -1;
  std::array<float, kFeatureCount> features{};
  ActionLabel label{};
  double d1_q = 0.0;
};

struct RawPanel {
  Root root{};
  std::vector<RawAction> actions;
};

std::array<int, kBoardSize> columnHeights(const Board& board) {
  std::array<int, kBoardSize> result{};
  for (int column = 0; column < kBoardSize; ++column) {
    for (int row = 0; row < kBoardSize; ++row) {
      result[column] += board[indexOf(row, column)] != kEmpty;
    }
  }
  return result;
}

RawPanel prepareRawPanel(const Panel& source) {
  RawPanel result;
  result.root = source.root;
  bool mirrored = false;
  const PublicState root =
      constructive::canonicalPublic(source.root.state, mirrored);
  if (mirrored) {
    throw std::runtime_error("collected direct root was not canonical");
  }
  fair::SearchContext context;
  const fair::RootEvaluation d1 =
      fair::rootDecision(constructive::materialize(root), 1, context);
  if (d1.action < 0 || context.work > 70 || !context.cache.empty()) {
    throw std::runtime_error("direct-ranker feature D1 failed");
  }
  const constructive::Metrics root_metrics = constructive::extractMetrics(root);
  const auto heights = columnHeights(root.board);
  int legal_count = 0;
  legalColumns(root.board, legal_count);
  result.actions.reserve(source.actions.size());
  for (const ActionLabel& label : source.actions) {
    RawAction action;
    action.action = label.action;
    action.label = label;
    action.d1_q = d1.values[label.action];
    std::array<double, constructive::kMetricCount> sum{};
    std::array<double, constructive::kMetricCount> squares{};
    double immediate_sum = 0.0;
    double immediate_squares = 0.0;
    int terminal = 0;
    for (int scenario = 0; scenario < kRootScenarios; ++scenario) {
      const RootBranch branch = rootSuccessor(root, label.action, scenario);
      immediate_sum += branch.immediate_return;
      immediate_squares += branch.immediate_return * branch.immediate_return;
      terminal += branch.terminal;
      if (!branch.terminal) {
        const constructive::Metrics metrics =
            constructive::extractMetrics(branch.state);
        for (int metric = 0; metric < constructive::kMetricCount; ++metric) {
          sum[metric] += metrics[metric];
          squares[metric] += metrics[metric] * metrics[metric];
        }
      }
    }
    int feature = 0;
    for (const double value : root_metrics) {
      action.features[feature++] = static_cast<float>(value);
    }
    std::array<double, constructive::kMetricCount> successor_mean{};
    for (int metric = 0; metric < constructive::kMetricCount; ++metric) {
      successor_mean[metric] = sum[metric] / kRootScenarios;
      action.features[feature++] =
          static_cast<float>(successor_mean[metric]);
    }
    for (int metric = 0; metric < constructive::kMetricCount; ++metric) {
      const double variance = std::max(
          0.0, squares[metric] / kRootScenarios -
                   successor_mean[metric] * successor_mean[metric]);
      action.features[feature++] = static_cast<float>(std::sqrt(variance));
    }
    const double immediate_mean = immediate_sum / kRootScenarios;
    const double immediate_variance =
        std::max(0.0, immediate_squares / kRootScenarios -
                          immediate_mean * immediate_mean);
    action.features[feature++] =
        static_cast<float>(d1.values[label.action] / 17'000.0);
    action.features[feature++] = static_cast<float>(
        (d1.value - d1.values[label.action]) / 17'000.0);
    action.features[feature++] = static_cast<float>(immediate_mean - 1.0);
    action.features[feature++] =
        static_cast<float>(std::sqrt(immediate_variance));
    action.features[feature++] =
        static_cast<float>(terminal) / kRootScenarios;
    action.features[feature++] = static_cast<float>(
        std::abs(label.action - kBoardSize / 2) /
        static_cast<double>(kBoardSize / 2));
    action.features[feature++] =
        static_cast<float>(heights[label.action]) / kBoardSize;
    action.features[feature++] =
        static_cast<float>(legal_count) / kBoardSize;
    action.features[feature++] = static_cast<float>(
        successor_mean[constructive::kOccupancy] -
        root_metrics[constructive::kOccupancy]);
    if (feature != kFeatureCount) {
      throw std::runtime_error("direct-ranker feature-count mismatch");
    }
    result.actions.push_back(action);
  }
  return result;
}

std::vector<RawPanel> prepareRawPanels(const std::vector<Panel>& source,
                                       const Deadline& deadline) {
  std::vector<RawPanel> result;
  result.reserve(source.size());
  for (std::size_t index = 0; index < source.size(); ++index) {
    if ((index & 127u) == 0u) deadline.check();
    result.push_back(prepareRawPanel(source[index]));
  }
  enforceResources();
  return result;
}

struct Normalizer {
  std::array<float, kFeatureCount> feature_mean{};
  std::array<float, kFeatureCount> feature_scale{};
  float clear_mean = 0.0f;
  float clear_scale = 1.0f;
  float reveal_mean = 0.0f;
  float reveal_scale = 1.0f;
  float downside_mean = 0.0f;
  float downside_scale = 1.0f;

  std::array<float, kFeatureCount> normalize(
      const std::array<float, kFeatureCount>& raw) const {
    std::array<float, kFeatureCount> result{};
    for (int feature = 0; feature < kFeatureCount; ++feature) {
      result[feature] = std::clamp(
          (raw[feature] - feature_mean[feature]) * feature_scale[feature],
          -6.0f, 6.0f);
    }
    return result;
  }
};

Normalizer fitNormalizer(const std::vector<RawPanel>& panels,
                         const std::vector<std::size_t>& indices) {
  if (indices.empty()) throw std::invalid_argument("empty normalizer fold");
  std::array<double, kFeatureCount> sums{};
  std::array<double, kFeatureCount> squares{};
  double clear_sum = 0.0;
  double clear_squares = 0.0;
  double reveal_sum = 0.0;
  double reveal_squares = 0.0;
  double downside_sum = 0.0;
  double downside_squares = 0.0;
  std::uint64_t count = 0;
  for (const std::size_t index : indices) {
    if (index >= panels.size()) {
      throw std::invalid_argument("normalizer panel index out of bounds");
    }
    for (const RawAction& action : panels[index].actions) {
      for (int feature = 0; feature < kFeatureCount; ++feature) {
        sums[feature] += action.features[feature];
        squares[feature] +=
            static_cast<double>(action.features[feature]) *
            action.features[feature];
      }
      clear_sum += action.label.clears;
      clear_squares += action.label.clears * action.label.clears;
      reveal_sum += action.label.reveals;
      reveal_squares += action.label.reveals * action.label.reveals;
      downside_sum += action.label.downside_return;
      downside_squares +=
          action.label.downside_return * action.label.downside_return;
      ++count;
    }
  }
  if (count == 0) throw std::runtime_error("normalizer has no actions");
  const double denominator = static_cast<double>(count);
  Normalizer result;
  for (int feature = 0; feature < kFeatureCount; ++feature) {
    const double mean = sums[feature] / denominator;
    const double variance =
        std::max(1.0e-6, squares[feature] / denominator - mean * mean);
    result.feature_mean[feature] = static_cast<float>(mean);
    result.feature_scale[feature] =
        static_cast<float>(1.0 / std::sqrt(variance));
  }
  const auto target = [denominator](double sum, double square, float& mean_out,
                                    float& scale_out) {
    const double mean = sum / denominator;
    const double variance =
        std::max(1.0e-4, square / denominator - mean * mean);
    mean_out = static_cast<float>(mean);
    scale_out = static_cast<float>(1.0 / std::sqrt(variance));
  };
  target(clear_sum, clear_squares, result.clear_mean, result.clear_scale);
  target(reveal_sum, reveal_squares, result.reveal_mean,
         result.reveal_scale);
  target(downside_sum, downside_squares, result.downside_mean,
         result.downside_scale);
  return result;
}

struct PreparedAction {
  int action = -1;
  std::array<float, kFeatureCount> features{};
  std::array<float, kHeads> targets{};
  double mean_return = 0.0;
  double d1_q = 0.0;
};

struct PreparedPanel {
  Root root{};
  std::vector<PreparedAction> actions;
};

std::vector<PreparedPanel> preparePanels(
    const std::vector<RawPanel>& source,
    const std::vector<std::size_t>& indices, const Normalizer& normalizer) {
  std::vector<PreparedPanel> result;
  result.reserve(indices.size());
  for (const std::size_t index : indices) {
    if (index >= source.size()) {
      throw std::invalid_argument("prepared panel index out of bounds");
    }
    const RawPanel& raw = source[index];
    PreparedPanel panel;
    panel.root = raw.root;
    panel.actions.reserve(raw.actions.size());
    double mean = 0.0;
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (const RawAction& action : raw.actions) {
      mean += action.label.mean_return / raw.actions.size();
      minimum = std::min(minimum, action.label.mean_return);
      maximum = std::max(maximum, action.label.mean_return);
    }
    const double advantage_scale = std::max(1.0, maximum - minimum);
    for (const RawAction& raw_action : raw.actions) {
      PreparedAction action;
      action.action = raw_action.action;
      action.features = normalizer.normalize(raw_action.features);
      action.targets[0] = static_cast<float>(
          (raw_action.label.mean_return - mean) / advantage_scale);
      action.targets[1] = static_cast<float>(raw_action.label.survival);
      action.targets[2] = static_cast<float>(
          (raw_action.label.clears - normalizer.clear_mean) *
          normalizer.clear_scale);
      action.targets[3] = static_cast<float>(
          (raw_action.label.reveals - normalizer.reveal_mean) *
          normalizer.reveal_scale);
      action.targets[4] = static_cast<float>(
          (raw_action.label.downside_return - normalizer.downside_mean) *
          normalizer.downside_scale);
      action.mean_return = raw_action.label.mean_return;
      action.d1_q = raw_action.d1_q;
      panel.actions.push_back(action);
    }
    result.push_back(std::move(panel));
  }
  return result;
}

struct Variant {
  std::string_view name;
  int hidden = 0;
  float pair_weight = 0.0f;
  float list_weight = 0.0f;
  float point_weight = 0.0f;
  float auxiliary_weight = 0.0f;
};

// The ablation menu is fixed before evaluation.  Selection is lowest
// whole-origin-CV normalized regret, then highest pairwise/top-two accuracy,
// then the smaller model.  The development holdout never chooses a variant.
constexpr std::array<Variant, 3> kVariants{{
    {"pair64", 64, 1.0f, 0.0f, 0.20f, 0.15f},
    {"hybrid64", 64, 1.0f, 0.75f, 0.20f, 0.15f},
    {"hybrid96", 96, 1.0f, 0.75f, 0.20f, 0.15f},
}};

struct Layout {
  explicit Layout(int hidden_width) : hidden(hidden_width) {
    if (hidden < 1 || hidden > 96) {
      throw std::invalid_argument("direct-ranker hidden width out of range");
    }
    numeric_weight = kCategoryCount * hidden;
    bias = numeric_weight + kFeatureCount * hidden;
    output_weight = bias + hidden;
    output_bias = output_weight + kHeads * hidden;
    count = output_bias + kHeads;
  }

  int hidden = 0;
  int embedding = 0;
  int numeric_weight = 0;
  int bias = 0;
  int output_weight = 0;
  int output_bias = 0;
  int count = 0;
};

struct OrientationCache {
  std::array<int, kActiveCategories> categories{};
  std::vector<float> pre;
  std::vector<float> hidden;
  std::array<float, kHeads> output{};
};

struct ForwardCache {
  OrientationCache direct;
  OrientationCache reflected;
  std::array<float, kHeads> output{};
};

class RankNetwork {
 public:
  RankNetwork(int hidden, std::uint32_t seed)
      : layout_(hidden), parameters_(layout_.count, 0.0f),
        first_(layout_.count, 0.0f), second_(layout_.count, 0.0f) {
    Mulberry32 random(seed);
    const float embedding_radius = 0.035f;
    for (int index = layout_.embedding; index < layout_.numeric_weight;
         ++index) {
      parameters_[index] = static_cast<float>(
          (2.0 * random.nextUnit() - 1.0) * embedding_radius);
    }
    const float numeric_radius = std::sqrt(
        6.0f / static_cast<float>(kFeatureCount + layout_.hidden));
    for (int index = layout_.numeric_weight; index < layout_.bias; ++index) {
      parameters_[index] = static_cast<float>(
          (2.0 * random.nextUnit() - 1.0) * numeric_radius);
    }
    const float output_radius = std::sqrt(
        6.0f / static_cast<float>(layout_.hidden + kHeads));
    for (int index = layout_.output_weight; index < layout_.output_bias;
         ++index) {
      parameters_[index] = static_cast<float>(
          (2.0 * random.nextUnit() - 1.0) * output_radius);
    }
  }

  OrientationCache forwardOrientation(
      const PublicState& state, int action,
      const std::array<float, kFeatureCount>& features) const {
    if (state.terminal || !isLegal(state.board, action)) {
      throw std::invalid_argument("invalid direct-ranker network input");
    }
    OrientationCache cache;
    cache.pre.resize(layout_.hidden);
    cache.hidden.resize(layout_.hidden);
    int active = 0;
    for (int cell = 0; cell < kCellCount; ++cell) {
      const int token = state.board[cell];
      if (token < 0 || token >= kBoardTokens) {
        throw std::invalid_argument("invalid direct-ranker board token");
      }
      cache.categories[active++] = cell * kBoardTokens + token;
    }
    cache.categories[active++] =
        kBoardCategories + static_cast<int>(state.next_disc) - 1;
    cache.categories[active++] =
        kBoardCategories + kNextCategories +
        static_cast<int>(state.moves_remaining) - 1;
    cache.categories[active++] =
        kBoardCategories + kNextCategories + kPhaseCategories + action;
    if (active != kActiveCategories) {
      throw std::runtime_error("direct-ranker active-category mismatch");
    }
    const float category_scale =
        1.0f / std::sqrt(static_cast<float>(kActiveCategories));
    const float feature_scale =
        1.0f / std::sqrt(static_cast<float>(kFeatureCount));
    for (int hidden = 0; hidden < layout_.hidden; ++hidden) {
      float value = parameters_[layout_.bias + hidden];
      for (const int category : cache.categories) {
        value += category_scale *
                 parameters_[layout_.embedding + category * layout_.hidden +
                             hidden];
      }
      for (int feature = 0; feature < kFeatureCount; ++feature) {
        value += feature_scale * features[feature] *
                 parameters_[layout_.numeric_weight +
                             feature * layout_.hidden + hidden];
      }
      cache.pre[hidden] = value;
      cache.hidden[hidden] = std::clamp(value, 0.0f, 1.0f);
    }
    for (int head = 0; head < kHeads; ++head) {
      float value = parameters_[layout_.output_bias + head];
      for (int hidden = 0; hidden < layout_.hidden; ++hidden) {
        value += parameters_[layout_.output_weight +
                             head * layout_.hidden + hidden] *
                 cache.hidden[hidden];
      }
      cache.output[head] = value;
    }
    return cache;
  }

  ForwardCache forward(
      const PublicState& root, int action,
      const std::array<float, kFeatureCount>& features) const {
    ForwardCache result;
    result.direct = forwardOrientation(root, action, features);
    result.reflected = forwardOrientation(
        constructive::mirror(root), kBoardSize - 1 - action, features);
    for (int head = 0; head < kHeads; ++head) {
      result.output[head] =
          0.5f * (result.direct.output[head] +
                  result.reflected.output[head]);
    }
    return result;
  }

  std::array<float, kHeads> predict(
      const PublicState& root, int action,
      const std::array<float, kFeatureCount>& features) const {
    return forward(root, action, features).output;
  }

  std::vector<float> gradient() const {
    return std::vector<float>(layout_.count, 0.0f);
  }

  void accumulateOrientation(
      const std::array<float, kFeatureCount>& features,
      const OrientationCache& cache,
      const std::array<float, kHeads>& output_derivative,
      std::vector<float>& gradient) const {
    std::vector<float> hidden_derivative(layout_.hidden, 0.0f);
    for (int head = 0; head < kHeads; ++head) {
      gradient[layout_.output_bias + head] += output_derivative[head];
      for (int hidden = 0; hidden < layout_.hidden; ++hidden) {
        const int index =
            layout_.output_weight + head * layout_.hidden + hidden;
        gradient[index] += output_derivative[head] * cache.hidden[hidden];
        hidden_derivative[hidden] +=
            output_derivative[head] * parameters_[index];
      }
    }
    const float category_scale =
        1.0f / std::sqrt(static_cast<float>(kActiveCategories));
    const float feature_scale =
        1.0f / std::sqrt(static_cast<float>(kFeatureCount));
    for (int hidden = 0; hidden < layout_.hidden; ++hidden) {
      const float derivative =
          cache.pre[hidden] > 0.0f && cache.pre[hidden] < 1.0f
              ? hidden_derivative[hidden]
              : 0.0f;
      gradient[layout_.bias + hidden] += derivative;
      for (const int category : cache.categories) {
        gradient[layout_.embedding + category * layout_.hidden + hidden] +=
            category_scale * derivative;
      }
      for (int feature = 0; feature < kFeatureCount; ++feature) {
        gradient[layout_.numeric_weight + feature * layout_.hidden + hidden] +=
            feature_scale * features[feature] * derivative;
      }
    }
  }

  void backpropagate(const PreparedAction& action,
                     const ForwardCache& cache,
                     const std::array<float, kHeads>& derivative,
                     std::vector<float>& gradient) const {
    std::array<float, kHeads> half{};
    for (int head = 0; head < kHeads; ++head) {
      half[head] = 0.5f * derivative[head];
    }
    accumulateOrientation(action.features, cache.direct, half, gradient);
    accumulateOrientation(action.features, cache.reflected, half, gradient);
  }

  void apply(std::vector<float>& gradient) {
    if (gradient.size() != parameters_.size()) {
      throw std::invalid_argument("direct-ranker gradient size mismatch");
    }
    double squared_norm = 0.0;
    for (int index = 0; index < layout_.count; ++index) {
      const bool decay = index < layout_.bias ||
                         (index >= layout_.output_weight &&
                          index < layout_.output_bias);
      if (decay) gradient[index] += kWeightDecay * parameters_[index];
      squared_norm +=
          static_cast<double>(gradient[index]) * gradient[index];
    }
    const double norm = std::sqrt(squared_norm);
    const float clipping = norm > kGradientNorm
                               ? static_cast<float>(kGradientNorm / norm)
                               : 1.0f;
    ++step_;
    constexpr float beta1 = 0.9f;
    constexpr float beta2 = 0.999f;
    constexpr float epsilon = 1.0e-8f;
    const float correction1 =
        1.0f - std::pow(beta1, static_cast<float>(step_));
    const float correction2 =
        1.0f - std::pow(beta2, static_cast<float>(step_));
    for (int index = 0; index < layout_.count; ++index) {
      const float value = clipping * gradient[index];
      first_[index] = beta1 * first_[index] + (1.0f - beta1) * value;
      second_[index] =
          beta2 * second_[index] + (1.0f - beta2) * value * value;
      parameters_[index] -=
          kLearningRate * (first_[index] / correction1) /
          (std::sqrt(second_[index] / correction2) + epsilon);
      if (!std::isfinite(parameters_[index])) {
        throw std::runtime_error("non-finite direct-ranker parameter");
      }
    }
  }

  int hidden() const { return layout_.hidden; }
  int parameterCount() const { return layout_.count; }
  const std::vector<float>& parameters() const { return parameters_; }

  void setParameters(const std::vector<float>& source) {
    if (source.size() != parameters_.size()) {
      throw std::invalid_argument("direct-ranker parameter-count mismatch");
    }
    parameters_ = source;
    std::fill(first_.begin(), first_.end(), 0.0f);
    std::fill(second_.begin(), second_.end(), 0.0f);
    step_ = 0;
  }

 private:
  Layout layout_;
  std::vector<float> parameters_;
  std::vector<float> first_;
  std::vector<float> second_;
  std::uint64_t step_ = 0;
};

using PublicActionEvaluator = std::array<float, kHeads> (RankNetwork::*)(
    const PublicState&, int,
    const std::array<float, kFeatureCount>&) const;
static_assert(std::is_same_v<decltype(&RankNetwork::predict),
                             PublicActionEvaluator>);
static_assert(!std::is_invocable_v<PublicActionEvaluator, const RankNetwork&,
                                   const State&, int,
                                   const std::array<float, kFeatureCount>&>);

double softplus(double value) {
  if (value > 30.0) return value;
  if (value < -30.0) return std::exp(value);
  return std::log1p(std::exp(value));
}

double sigmoid(double value) {
  if (value >= 0.0) {
    const double exponential = std::exp(-value);
    return 1.0 / (1.0 + exponential);
  }
  const double exponential = std::exp(value);
  return exponential / (1.0 + exponential);
}

void accumulatePanel(const RankNetwork& network, const PreparedPanel& panel,
                     const Variant& variant, float inverse_batch,
                     std::vector<float>& gradient, double& loss) {
  const std::size_t count = panel.actions.size();
  if (count < 2) throw std::invalid_argument("invalid training panel");
  std::vector<ForwardCache> caches;
  caches.reserve(count);
  std::vector<std::array<float, kHeads>> derivatives(count);
  for (const PreparedAction& action : panel.actions) {
    caches.push_back(
        network.forward(panel.root.state, action.action, action.features));
  }

  int pairs = 0;
  for (std::size_t first = 0; first < count; ++first) {
    for (std::size_t second = first + 1; second < count; ++second) {
      pairs += std::abs(panel.actions[first].mean_return -
                        panel.actions[second].mean_return) > 1.0e-9;
    }
  }
  if (pairs > 0 && variant.pair_weight > 0.0f) {
    const auto [minimum, maximum] = std::minmax_element(
        panel.actions.begin(), panel.actions.end(),
        [](const PreparedAction& left, const PreparedAction& right) {
          return left.mean_return < right.mean_return;
        });
    const double range =
        std::max(1.0e-9, maximum->mean_return - minimum->mean_return);
    for (std::size_t first = 0; first < count; ++first) {
      for (std::size_t second = first + 1; second < count; ++second) {
        const double target_difference =
            panel.actions[first].mean_return -
            panel.actions[second].mean_return;
        if (std::abs(target_difference) <= 1.0e-9) continue;
        const double sign = target_difference > 0.0 ? 1.0 : -1.0;
        const double margin = sign *
                              (caches[first].output[0] -
                               caches[second].output[0]);
        const double importance =
            0.25 + 0.75 * std::abs(target_difference) / range;
        const double weight =
            variant.pair_weight * importance / static_cast<double>(pairs);
        loss += weight * softplus(-margin);
        const float margin_derivative = static_cast<float>(
            -weight * sigmoid(-margin));
        derivatives[first][0] +=
            static_cast<float>(sign) * margin_derivative;
        derivatives[second][0] -=
            static_cast<float>(sign) * margin_derivative;
      }
    }
  }

  if (variant.list_weight > 0.0f) {
    std::vector<double> predicted(count);
    std::vector<double> target(count);
    double predicted_max = -std::numeric_limits<double>::infinity();
    double target_max = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < count; ++index) {
      predicted[index] = caches[index].output[0];
      target[index] = panel.actions[index].targets[0] / kListTemperature;
      predicted_max = std::max(predicted_max, predicted[index]);
      target_max = std::max(target_max, target[index]);
    }
    double predicted_sum = 0.0;
    double target_sum = 0.0;
    for (std::size_t index = 0; index < count; ++index) {
      predicted[index] = std::exp(predicted[index] - predicted_max);
      target[index] = std::exp(target[index] - target_max);
      predicted_sum += predicted[index];
      target_sum += target[index];
    }
    for (std::size_t index = 0; index < count; ++index) {
      const double probability = predicted[index] / predicted_sum;
      const double target_probability = target[index] / target_sum;
      loss += -variant.list_weight * target_probability *
              std::log(std::max(1.0e-12, probability));
      derivatives[index][0] += static_cast<float>(
          variant.list_weight * (probability - target_probability));
    }
  }

  for (std::size_t index = 0; index < count; ++index) {
    const PreparedAction& action = panel.actions[index];
    const auto& prediction = caches[index].output;
    const float point_error = prediction[0] - action.targets[0];
    loss += 0.5 * variant.point_weight * point_error * point_error / count;
    derivatives[index][0] +=
        variant.point_weight * point_error / static_cast<float>(count);

    const double survival = sigmoid(prediction[1]);
    loss += -variant.auxiliary_weight /
            static_cast<double>(count) *
            (action.targets[1] * std::log(std::max(1.0e-9, survival)) +
             (1.0 - action.targets[1]) *
                 std::log(std::max(1.0e-9, 1.0 - survival)));
    derivatives[index][1] += static_cast<float>(
        variant.auxiliary_weight * (survival - action.targets[1]) / count);
    for (const int head : {2, 3, 4}) {
      const float error = prediction[head] - action.targets[head];
      loss += 0.5 * variant.auxiliary_weight * error * error / count;
      derivatives[index][head] +=
          variant.auxiliary_weight * error / static_cast<float>(count);
    }
  }

  for (std::size_t index = 0; index < count; ++index) {
    for (float& value : derivatives[index]) value *= inverse_batch;
    network.backpropagate(panel.actions[index], caches[index],
                          derivatives[index], gradient);
  }
}

struct TrainingResult {
  RankNetwork network;
  std::vector<double> losses;

  TrainingResult(int hidden, std::uint32_t seed) : network(hidden, seed) {}
};

TrainingResult train(const std::vector<PreparedPanel>& panels,
                     const Variant& variant, std::uint32_t seed,
                     int epochs, const Deadline& deadline,
                     bool report_progress) {
  if (panels.empty() || epochs < 1) {
    throw std::invalid_argument("invalid direct-ranker training request");
  }
  TrainingResult result(variant.hidden, seed);
  std::vector<std::size_t> order(panels.size());
  std::iota(order.begin(), order.end(), 0u);
  result.losses.reserve(epochs);
  for (int epoch = 0; epoch < epochs; ++epoch) {
    Mulberry32 random(mix32(kShuffleDomain ^ seed ^
                           static_cast<std::uint32_t>(epoch + 1)));
    for (std::size_t cursor = order.size(); cursor > 1; --cursor) {
      const std::size_t selected = static_cast<std::size_t>(
          (static_cast<std::uint64_t>(random.nextBits()) * cursor) >> 32u);
      std::swap(order[cursor - 1], order[selected]);
    }
    double loss = 0.0;
    for (std::size_t begin = 0; begin < order.size();
         begin += kBatchRoots) {
      if ((begin & 511u) == 0u) deadline.check();
      const std::size_t end =
          std::min(order.size(), begin + kBatchRoots);
      const float inverse = 1.0f / static_cast<float>(end - begin);
      std::vector<float> gradient = result.network.gradient();
      for (std::size_t offset = begin; offset < end; ++offset) {
        accumulatePanel(result.network, panels[order[offset]], variant,
                        inverse, gradient, loss);
      }
      result.network.apply(gradient);
    }
    result.losses.push_back(loss / panels.size());
    if (report_progress) {
      std::cerr << "direct ranker " << variant.name << " epoch "
                << epoch + 1 << '/' << epochs << " loss "
                << result.losses.back() << " rss " << peakRssBytes()
                << '\n';
    }
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

struct RankingAccumulator {
  int roots = 0;
  int pairs = 0;
  double top1_hits = 0.0;
  double top2_hits = 0.0;
  double pair_hits = 0.0;
  double regret_sum = 0.0;

  void merge(const RankingAccumulator& source) {
    roots += source.roots;
    pairs += source.pairs;
    top1_hits += source.top1_hits;
    top2_hits += source.top2_hits;
    pair_hits += source.pair_hits;
    regret_sum += source.regret_sum;
  }
};

struct RankingMetrics {
  int roots = 0;
  int pairs = 0;
  double top1 = 0.0;
  double top2 = 0.0;
  double pairwise = 0.0;
  double normalized_regret = 0.0;
};

void observeRanking(RankingAccumulator& result,
                    const std::vector<double>& predictions,
                    const std::vector<double>& targets) {
  if (predictions.size() != targets.size() || targets.size() < 2) {
    throw std::invalid_argument("invalid direct action-ranking panel");
  }
  std::vector<int> order(predictions.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
    return predictions[left] > predictions[right];
  });
  const double target_maximum =
      *std::max_element(targets.begin(), targets.end());
  const auto optimal = [&](int index) {
    return std::abs(targets[static_cast<std::size_t>(index)] -
                    target_maximum) <= 1.0e-9;
  };
  result.top1_hits += optimal(order[0]);
  result.top2_hits += optimal(order[0]) || optimal(order[1]);
  const auto [minimum, maximum] =
      std::minmax_element(targets.begin(), targets.end());
  const double range = std::max(1.0e-9, *maximum - *minimum);
  result.regret_sum +=
      (target_maximum - targets[static_cast<std::size_t>(order[0])]) / range;
  for (std::size_t first = 0; first < targets.size(); ++first) {
    for (std::size_t second = first + 1; second < targets.size(); ++second) {
      const double target_difference = targets[first] - targets[second];
      if (std::abs(target_difference) <= 1.0e-9) continue;
      const double predicted_difference =
          predictions[first] - predictions[second];
      if (std::abs(predicted_difference) <= 1.0e-12) {
        result.pair_hits += 0.5;
      } else {
        result.pair_hits +=
            target_difference * predicted_difference > 0.0;
      }
      ++result.pairs;
    }
  }
  ++result.roots;
}

RankingMetrics finish(const RankingAccumulator& source) {
  RankingMetrics result;
  result.roots = source.roots;
  result.pairs = source.pairs;
  if (source.roots > 0) {
    result.top1 = source.top1_hits / source.roots;
    result.top2 = source.top2_hits / source.roots;
    result.normalized_regret = source.regret_sum / source.roots;
  }
  if (source.pairs > 0) {
    result.pairwise = source.pair_hits / source.pairs;
  }
  return result;
}

struct CalibrationAccumulator {
  int pairs = 0;
  double ranker_correct = 0.0;
  double d1_correct = 0.0;
  double brier_sum = 0.0;
  double confidence_sum = 0.0;
  std::array<int, kCalibrationBins> bin_count{};
  std::array<double, kCalibrationBins> bin_correct{};
  std::array<double, kCalibrationBins> bin_confidence{};

  void merge(const CalibrationAccumulator& source) {
    pairs += source.pairs;
    ranker_correct += source.ranker_correct;
    d1_correct += source.d1_correct;
    brier_sum += source.brier_sum;
    confidence_sum += source.confidence_sum;
    for (int bin = 0; bin < kCalibrationBins; ++bin) {
      bin_count[bin] += source.bin_count[bin];
      bin_correct[bin] += source.bin_correct[bin];
      bin_confidence[bin] += source.bin_confidence[bin];
    }
  }
};

struct CalibrationMetrics {
  int pairs = 0;
  double ranker_accuracy = 0.0;
  double d1_accuracy = 0.0;
  double brier = 0.0;
  double mean_confidence = 0.0;
  double expected_calibration_error = 0.0;
};

CalibrationMetrics finish(const CalibrationAccumulator& source) {
  CalibrationMetrics result;
  result.pairs = source.pairs;
  if (source.pairs == 0) return result;
  result.ranker_accuracy = source.ranker_correct / source.pairs;
  result.d1_accuracy = source.d1_correct / source.pairs;
  result.brier = source.brier_sum / source.pairs;
  result.mean_confidence = source.confidence_sum / source.pairs;
  for (int bin = 0; bin < kCalibrationBins; ++bin) {
    if (source.bin_count[bin] == 0) continue;
    const double accuracy =
        source.bin_correct[bin] / source.bin_count[bin];
    const double confidence =
        source.bin_confidence[bin] / source.bin_count[bin];
    result.expected_calibration_error +=
        static_cast<double>(source.bin_count[bin]) / source.pairs *
        std::abs(accuracy - confidence);
  }
  return result;
}

void observeCalibration(CalibrationAccumulator& result,
                        double ranker_first, double ranker_second,
                        double d1_first, double d1_second,
                        double target_first, double target_second) {
  const double target_difference = target_first - target_second;
  if (std::abs(target_difference) <= 1.0e-9) return;
  const double probability = sigmoid(ranker_first - ranker_second);
  const double target = target_difference > 0.0 ? 1.0 : 0.0;
  const bool ranker_choice = probability >= 0.5;
  const bool correct_choice = target > 0.5;
  const double ranker_correct = ranker_choice == correct_choice;
  const double d1_difference = d1_first - d1_second;
  double d1_correct = 0.5;
  if (std::abs(d1_difference) > 1.0e-12) {
    d1_correct = (d1_difference > 0.0) == correct_choice;
  }
  const double confidence = std::max(probability, 1.0 - probability);
  const int bin = std::clamp(
      static_cast<int>((confidence - 0.5) * 2.0 * kCalibrationBins),
      0, kCalibrationBins - 1);
  ++result.pairs;
  result.ranker_correct += ranker_correct;
  result.d1_correct += d1_correct;
  const double error = probability - target;
  result.brier_sum += error * error;
  result.confidence_sum += confidence;
  ++result.bin_count[bin];
  result.bin_correct[bin] += ranker_correct;
  result.bin_confidence[bin] += confidence;
}

struct EvaluationAccumulator {
  RankingAccumulator overall_ranker;
  RankingAccumulator overall_d1;
  RankingAccumulator d1_ranker;
  RankingAccumulator d1_baseline;
  RankingAccumulator constructive_ranker;
  RankingAccumulator constructive_baseline;
  CalibrationAccumulator near_tied;
  CalibrationAccumulator decisive;

  void merge(const EvaluationAccumulator& source) {
    overall_ranker.merge(source.overall_ranker);
    overall_d1.merge(source.overall_d1);
    d1_ranker.merge(source.d1_ranker);
    d1_baseline.merge(source.d1_baseline);
    constructive_ranker.merge(source.constructive_ranker);
    constructive_baseline.merge(source.constructive_baseline);
    near_tied.merge(source.near_tied);
    decisive.merge(source.decisive);
  }
};

struct Evaluation {
  RankingMetrics overall_ranker;
  RankingMetrics overall_d1;
  RankingMetrics d1_ranker;
  RankingMetrics d1_baseline;
  RankingMetrics constructive_ranker;
  RankingMetrics constructive_baseline;
  CalibrationMetrics near_tied;
  CalibrationMetrics decisive;
};

struct D4Benchmark {
  RankingMetrics overall;
  RankingMetrics d1_origins;
  RankingMetrics constructive_origins;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::size_t maximum_cache_entries = 0;
  double seconds = 0.0;
};

Evaluation finish(const EvaluationAccumulator& source) {
  return {finish(source.overall_ranker),
          finish(source.overall_d1),
          finish(source.d1_ranker),
          finish(source.d1_baseline),
          finish(source.constructive_ranker),
          finish(source.constructive_baseline),
          finish(source.near_tied),
          finish(source.decisive)};
}

EvaluationAccumulator evaluate(
    const RankNetwork& network, const Normalizer& normalizer,
    const std::vector<RawPanel>& panels,
    const std::vector<std::size_t>& indices, const Deadline& deadline) {
  EvaluationAccumulator result;
  for (std::size_t offset = 0; offset < indices.size(); ++offset) {
    if ((offset & 127u) == 0u) deadline.check();
    const RawPanel& panel = panels.at(indices[offset]);
    std::vector<double> ranker;
    std::vector<double> baseline;
    std::vector<double> targets;
    ranker.reserve(panel.actions.size());
    baseline.reserve(panel.actions.size());
    targets.reserve(panel.actions.size());
    for (const RawAction& action : panel.actions) {
      const auto normalized = normalizer.normalize(action.features);
      ranker.push_back(
          network.predict(panel.root.state, action.action, normalized)[0]);
      baseline.push_back(action.d1_q);
      targets.push_back(action.label.mean_return);
    }
    observeRanking(result.overall_ranker, ranker, targets);
    observeRanking(result.overall_d1, baseline, targets);
    if (panel.root.policy == OriginPolicy::kFairD1) {
      observeRanking(result.d1_ranker, ranker, targets);
      observeRanking(result.d1_baseline, baseline, targets);
    } else {
      observeRanking(result.constructive_ranker, ranker, targets);
      observeRanking(result.constructive_baseline, baseline, targets);
    }
    const auto [minimum, maximum] =
        std::minmax_element(targets.begin(), targets.end());
    const double range = std::max(1.0e-9, *maximum - *minimum);
    for (std::size_t first = 0; first < targets.size(); ++first) {
      for (std::size_t second = first + 1; second < targets.size(); ++second) {
        const double fraction =
            std::abs(targets[first] - targets[second]) / range;
        if (fraction <= kNearTieFraction) {
          observeCalibration(result.near_tied, ranker[first], ranker[second],
                             baseline[first], baseline[second], targets[first],
                             targets[second]);
        }
        if (fraction >= kDecisiveFraction) {
          observeCalibration(result.decisive, ranker[first], ranker[second],
                             baseline[first], baseline[second], targets[first],
                             targets[second]);
        }
      }
    }
  }
  return result;
}

D4Benchmark evaluateD4Benchmark(const std::vector<RawPanel>& panels,
                                int threads, const Deadline& deadline) {
  const Clock::time_point started = Clock::now();
  std::vector<d4_benchmark::SearchDecision> searches(panels.size());
  std::atomic<std::size_t> next{0};
  std::atomic<std::size_t> completed{0};
  std::vector<std::future<void>> workers;
  const int parallelism = std::min<int>(4, std::min<int>(threads, panels.size()));
  for (int worker = 0; worker < parallelism; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const std::size_t index = next.fetch_add(1);
        if (index >= panels.size()) return;
        deadline.check();
        const d4_benchmark::SearchDecision decision =
            d4_benchmark::chooseDepth4Action(
                constructive::materialize(panels[index].root.state));
        if (!decision.complete ||
            decision.completed_depth != d4_benchmark::kCandidateDepth ||
            decision.action < 0) {
          throw std::runtime_error("heldout exact D4 did not complete");
        }
        searches[index] = decision;
        const std::size_t count = completed.fetch_add(1) + 1;
        if (count % 100 == 0 || count == panels.size()) {
          std::cerr << "direct heldout D4 " << count << '/' << panels.size()
                    << '\n';
        }
      }
    }));
  }
  for (auto& worker : workers) worker.get();

  RankingAccumulator overall;
  RankingAccumulator d1_origins;
  RankingAccumulator constructive_origins;
  D4Benchmark result;
  for (std::size_t index = 0; index < panels.size(); ++index) {
    const RawPanel& panel = panels[index];
    const d4_benchmark::SearchDecision& search = searches[index];
    std::vector<double> predictions;
    std::vector<double> targets;
    for (const RawAction& action : panel.actions) {
      const double value = search.root_values[action.action];
      if (!std::isfinite(value)) {
        throw std::runtime_error("heldout D4 legal action missing Q");
      }
      predictions.push_back(value);
      targets.push_back(action.label.mean_return);
    }
    observeRanking(overall, predictions, targets);
    observeRanking(panel.root.policy == OriginPolicy::kFairD1
                       ? d1_origins
                       : constructive_origins,
                   predictions, targets);
    result.work += search.work;
    result.nodes += search.nodes;
    result.cache_hits += search.cache_hits;
    result.maximum_cache_entries =
        std::max(result.maximum_cache_entries, search.cache_entries);
  }
  result.overall = finish(overall);
  result.d1_origins = finish(d1_origins);
  result.constructive_origins = finish(constructive_origins);
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  enforceResources();
  return result;
}

std::vector<std::size_t> allIndices(std::size_t count) {
  std::vector<std::size_t> result(count);
  std::iota(result.begin(), result.end(), 0u);
  return result;
}

struct FoldResult {
  int fold = 0;
  int training_origins = 0;
  int validation_origins = 0;
  int training_roots = 0;
  int validation_roots = 0;
  double first_loss = 0.0;
  double final_loss = 0.0;
  Evaluation evaluation{};
};

struct VariantResult {
  int variant = 0;
  std::array<FoldResult, kCrossValidationFolds> folds{};
  Evaluation aggregate{};
};

std::pair<int, int> distinctOrigins(const std::vector<RawPanel>& panels,
                                    const std::vector<std::size_t>& training,
                                    const std::vector<std::size_t>& validation) {
  std::unordered_set<std::uint32_t> training_origins;
  std::unordered_set<std::uint32_t> validation_origins;
  for (const std::size_t index : training) {
    training_origins.insert(panels.at(index).root.origin_seed);
  }
  for (const std::size_t index : validation) {
    validation_origins.insert(panels.at(index).root.origin_seed);
  }
  for (const std::uint32_t seed : validation_origins) {
    if (training_origins.contains(seed)) {
      throw std::runtime_error("whole-origin CV leakage");
    }
  }
  return {static_cast<int>(training_origins.size()),
          static_cast<int>(validation_origins.size())};
}

VariantResult crossValidate(const std::vector<RawPanel>& panels,
                            int variant_index, const Deadline& deadline) {
  if (variant_index < 0 ||
      variant_index >= static_cast<int>(kVariants.size())) {
    throw std::invalid_argument("invalid ablation variant");
  }
  const Variant& variant = kVariants[variant_index];
  VariantResult result;
  result.variant = variant_index;
  EvaluationAccumulator aggregate;
  for (int fold = 0; fold < kCrossValidationFolds; ++fold) {
    std::vector<std::size_t> training_indices;
    std::vector<std::size_t> validation_indices;
    for (std::size_t index = 0; index < panels.size(); ++index) {
      if (panels[index].root.heldout || panels[index].root.fold < 0) {
        throw std::runtime_error("invalid panel in CV training corpus");
      }
      (panels[index].root.fold == fold ? validation_indices
                                       : training_indices)
          .push_back(index);
    }
    const auto [training_origins, validation_origins] =
        distinctOrigins(panels, training_indices, validation_indices);
    const Normalizer normalizer =
        fitNormalizer(panels, training_indices);
    const std::vector<PreparedPanel> prepared =
        preparePanels(panels, training_indices, normalizer);
    const std::uint32_t seed = mix32(
        kNetworkDomain ^ static_cast<std::uint32_t>(variant_index + 1) ^
        (static_cast<std::uint32_t>(fold + 1) * 0x9e37'79b9u));
    TrainingResult training = train(prepared, variant, seed, kEpochs,
                                    deadline, false);
    EvaluationAccumulator fold_accumulator = evaluate(
        training.network, normalizer, panels, validation_indices, deadline);
    aggregate.merge(fold_accumulator);
    FoldResult& record = result.folds[fold];
    record.fold = fold;
    record.training_origins = training_origins;
    record.validation_origins = validation_origins;
    record.training_roots = static_cast<int>(training_indices.size());
    record.validation_roots = static_cast<int>(validation_indices.size());
    record.first_loss = training.losses.front();
    record.final_loss = training.losses.back();
    record.evaluation = finish(fold_accumulator);
    std::cerr << "DIRECT_SIBLING_CV {\"variant\":\"" << variant.name
              << "\",\"fold\":" << fold
              << ",\"top1\":" << record.evaluation.overall_ranker.top1
              << ",\"top2\":" << record.evaluation.overall_ranker.top2
              << ",\"pairwise\":"
              << record.evaluation.overall_ranker.pairwise
              << ",\"regret\":"
              << record.evaluation.overall_ranker.normalized_regret
              << ",\"loss\":" << record.final_loss << "}\n";
  }
  result.aggregate = finish(aggregate);
  return result;
}

int selectVariant(const std::array<VariantResult, kVariants.size()>& results) {
  int best = 0;
  for (int candidate = 1; candidate < static_cast<int>(results.size());
       ++candidate) {
    const RankingMetrics& left =
        results[candidate].aggregate.overall_ranker;
    const RankingMetrics& right = results[best].aggregate.overall_ranker;
    bool replace = false;
    if (left.normalized_regret < right.normalized_regret - 1.0e-12) {
      replace = true;
    } else if (std::abs(left.normalized_regret -
                        right.normalized_regret) <= 1.0e-12 &&
               left.pairwise > right.pairwise + 1.0e-12) {
      replace = true;
    } else if (std::abs(left.normalized_regret -
                        right.normalized_regret) <= 1.0e-12 &&
               std::abs(left.pairwise - right.pairwise) <= 1.0e-12 &&
               left.top2 > right.top2 + 1.0e-12) {
      replace = true;
    } else if (std::abs(left.normalized_regret -
                        right.normalized_regret) <= 1.0e-12 &&
               std::abs(left.pairwise - right.pairwise) <= 1.0e-12 &&
               std::abs(left.top2 - right.top2) <= 1.0e-12 &&
               kVariants[candidate].hidden < kVariants[best].hidden) {
      replace = true;
    }
    if (replace) best = candidate;
  }
  return best;
}

bool halfDoesNotRegressBoth(const RankingMetrics& ranker,
                            const RankingMetrics& baseline) {
  return !(ranker.pairwise < baseline.pairwise &&
           ranker.normalized_regret > baseline.normalized_regret);
}

bool justifiesFreshConfirmation(const Evaluation& evaluation,
                                const D4Benchmark& d4) {
  return evaluation.overall_ranker.top1 >=
             evaluation.overall_d1.top1 + kRequiredTopOneGain &&
         evaluation.overall_ranker.pairwise >=
             evaluation.overall_d1.pairwise + kRequiredPairwiseGain &&
         evaluation.overall_ranker.normalized_regret <=
             kRequiredRegretRatio *
                 evaluation.overall_d1.normalized_regret &&
         halfDoesNotRegressBoth(evaluation.d1_ranker,
                                evaluation.d1_baseline) &&
         halfDoesNotRegressBoth(evaluation.constructive_ranker,
                                evaluation.constructive_baseline) &&
         evaluation.overall_ranker.pairwise > d4.overall.pairwise &&
         evaluation.overall_ranker.normalized_regret <
             d4.overall.normalized_regret;
}

void fingerprintFloat(std::uint64_t& hash, float value) {
  const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
  for (int shift = 0; shift < 32; shift += 8) {
    hash ^= static_cast<std::uint8_t>(bits >> shift);
    hash *= 0x0000'0100'0000'01b3ull;
  }
}

std::uint64_t modelFingerprint(const RankNetwork& network,
                               const Normalizer& normalizer,
                               int variant_index) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  hash ^= static_cast<std::uint64_t>(network.hidden());
  hash *= 0x0000'0100'0000'01b3ull;
  hash ^= static_cast<std::uint64_t>(variant_index + 1);
  hash *= 0x0000'0100'0000'01b3ull;
  for (const float value : network.parameters()) fingerprintFloat(hash, value);
  for (const float value : normalizer.feature_mean) {
    fingerprintFloat(hash, value);
  }
  for (const float value : normalizer.feature_scale) {
    fingerprintFloat(hash, value);
  }
  for (const float value :
       {normalizer.clear_mean, normalizer.clear_scale,
        normalizer.reveal_mean, normalizer.reveal_scale,
        normalizer.downside_mean, normalizer.downside_scale}) {
    fingerprintFloat(hash, value);
  }
  return hash;
}

std::uint64_t deployBytes(const RankNetwork& network) {
  return 8u + 5u * sizeof(std::uint32_t) + sizeof(std::uint64_t) +
         sizeof(Normalizer) +
         network.parameters().size() * sizeof(float);
}

void saveCheckpoint(const std::string& path, const RankNetwork& network,
                    const Normalizer& normalizer, int variant_index) {
  if (variant_index < 0 ||
      variant_index >= static_cast<int>(kVariants.size()) ||
      deployBytes(network) > kDeployLimitBytes) {
    throw std::invalid_argument("invalid deployable direct-ranker model");
  }
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("could not open direct-ranker checkpoint");
  }
  const std::uint32_t hidden = static_cast<std::uint32_t>(network.hidden());
  const std::uint32_t variant = static_cast<std::uint32_t>(variant_index);
  const std::uint32_t count =
      static_cast<std::uint32_t>(network.parameterCount());
  const std::uint32_t normalizer_size = sizeof(Normalizer);
  const std::uint64_t fingerprint =
      modelFingerprint(network, normalizer, variant_index);
  output.write(reinterpret_cast<const char*>(&kCheckpointMagic),
               sizeof(kCheckpointMagic));
  output.write(reinterpret_cast<const char*>(&kCheckpointVersion),
               sizeof(kCheckpointVersion));
  output.write(reinterpret_cast<const char*>(&hidden), sizeof(hidden));
  output.write(reinterpret_cast<const char*>(&variant), sizeof(variant));
  output.write(reinterpret_cast<const char*>(&count), sizeof(count));
  output.write(reinterpret_cast<const char*>(&normalizer_size),
               sizeof(normalizer_size));
  output.write(reinterpret_cast<const char*>(&fingerprint),
               sizeof(fingerprint));
  output.write(reinterpret_cast<const char*>(&normalizer), sizeof(normalizer));
  output.write(reinterpret_cast<const char*>(network.parameters().data()),
               static_cast<std::streamsize>(network.parameters().size() *
                                            sizeof(float)));
  if (!output) {
    throw std::runtime_error("failed writing direct-ranker checkpoint");
  }
}

struct FrozenModel {
  int variant = 0;
  Normalizer normalizer{};
  RankNetwork network;

  FrozenModel(int variant_index, int hidden, std::uint32_t seed)
      : variant(variant_index), network(hidden, seed) {}
};

FrozenModel loadCheckpoint(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("could not open direct-ranker checkpoint");
  }
  std::uint64_t magic = 0;
  std::uint32_t version = 0;
  std::uint32_t hidden = 0;
  std::uint32_t variant = 0;
  std::uint32_t count = 0;
  std::uint32_t normalizer_size = 0;
  std::uint64_t expected = 0;
  input.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  input.read(reinterpret_cast<char*>(&version), sizeof(version));
  input.read(reinterpret_cast<char*>(&hidden), sizeof(hidden));
  input.read(reinterpret_cast<char*>(&variant), sizeof(variant));
  input.read(reinterpret_cast<char*>(&count), sizeof(count));
  input.read(reinterpret_cast<char*>(&normalizer_size),
             sizeof(normalizer_size));
  input.read(reinterpret_cast<char*>(&expected), sizeof(expected));
  if (magic != kCheckpointMagic || version != kCheckpointVersion ||
      variant >= kVariants.size() ||
      hidden != static_cast<std::uint32_t>(kVariants[variant].hidden) ||
      normalizer_size != sizeof(Normalizer)) {
    throw std::runtime_error("invalid direct-ranker checkpoint header");
  }
  FrozenModel result(static_cast<int>(variant), static_cast<int>(hidden),
                     kNetworkDomain);
  if (count != static_cast<std::uint32_t>(result.network.parameterCount())) {
    throw std::runtime_error("invalid direct-ranker parameter count");
  }
  input.read(reinterpret_cast<char*>(&result.normalizer),
             sizeof(result.normalizer));
  std::vector<float> parameters(count);
  input.read(reinterpret_cast<char*>(parameters.data()),
             static_cast<std::streamsize>(parameters.size() * sizeof(float)));
  char trailing = 0;
  if (!input || input.read(&trailing, 1)) {
    throw std::runtime_error("invalid direct-ranker checkpoint payload");
  }
  result.network.setParameters(parameters);
  if (deployBytes(result.network) > kDeployLimitBytes ||
      modelFingerprint(result.network, result.normalizer, result.variant) !=
          expected) {
    throw std::runtime_error("direct-ranker checkpoint fingerprint mismatch");
  }
  return result;
}

RawPanel publicFeaturePanel(const PublicState& source) {
  if (source.terminal) {
    throw std::invalid_argument("cannot rank terminal public state");
  }
  bool mirrored = false;
  const PublicState canonical =
      constructive::canonicalPublic(source, mirrored);
  static_cast<void>(mirrored);
  Panel panel;
  panel.root.state = canonical;
  panel.root.policy = OriginPolicy::kFairD1;
  const State state = constructive::materialize(canonical);
  for (const int action : constructive::kColumnOrder) {
    if (isLegal(state.board, action)) {
      ActionLabel label;
      label.action = action;
      panel.actions.push_back(label);
    }
  }
  return prepareRawPanel(panel);
}

struct PublicScores {
  std::array<double, kBoardSize> ranker{};
  std::array<double, kBoardSize> fair_d1{};
  std::array<bool, kBoardSize> legal{};

  bool operator==(const PublicScores&) const = default;
};

PublicScores evaluatePublic(const PublicState& source,
                            const FrozenModel& model) {
  if (source.terminal) return {};
  bool mirrored = false;
  const PublicState canonical =
      constructive::canonicalPublic(source, mirrored);
  const RawPanel panel = publicFeaturePanel(canonical);
  PublicScores result;
  result.ranker.fill(-std::numeric_limits<double>::infinity());
  result.fair_d1.fill(-std::numeric_limits<double>::infinity());
  for (const RawAction& action : panel.actions) {
    const auto features = model.normalizer.normalize(action.features);
    const double score = model.network.predict(
        canonical, action.action, features)[0];
    const int output_action =
        mirrored ? kBoardSize - 1 - action.action : action.action;
    result.ranker[output_action] = score;
    result.fair_d1[output_action] = action.d1_q;
    result.legal[output_action] = true;
  }
  return result;
}

using PublicModelEvaluator = PublicScores (*)(const PublicState&,
                                              const FrozenModel&);
static_assert(std::is_same_v<decltype(&evaluatePublic),
                             PublicModelEvaluator>);
static_assert(!std::is_invocable_v<PublicModelEvaluator, const State&,
                                   const FrozenModel&>);

void writeRanking(std::ostream& output, const RankingMetrics& metrics) {
  output << "{\"roots\":" << metrics.roots << ",\"pairs\":"
         << metrics.pairs << ",\"top1\":" << metrics.top1
         << ",\"top2\":" << metrics.top2 << ",\"pairwise\":"
         << metrics.pairwise << ",\"normalizedRegret\":"
         << metrics.normalized_regret << '}';
}

void writeCalibration(std::ostream& output,
                      const CalibrationMetrics& metrics) {
  output << "{\"pairs\":" << metrics.pairs
         << ",\"rankerAccuracy\":" << metrics.ranker_accuracy
         << ",\"d1Accuracy\":" << metrics.d1_accuracy
         << ",\"brier\":" << metrics.brier
         << ",\"meanConfidence\":" << metrics.mean_confidence
         << ",\"ece\":" << metrics.expected_calibration_error << '}';
}

void writeEvaluation(std::ostream& output, const Evaluation& evaluation) {
  output << "{\"overall\":{\"ranker\":";
  writeRanking(output, evaluation.overall_ranker);
  output << ",\"fairD1\":";
  writeRanking(output, evaluation.overall_d1);
  output << "},\"fairD1Origins\":{\"ranker\":";
  writeRanking(output, evaluation.d1_ranker);
  output << ",\"fairD1\":";
  writeRanking(output, evaluation.d1_baseline);
  output << "},\"constructiveOrigins\":{\"ranker\":";
  writeRanking(output, evaluation.constructive_ranker);
  output << ",\"fairD1\":";
  writeRanking(output, evaluation.constructive_baseline);
  output << "},\"calibration\":{\"nearTied\":";
  writeCalibration(output, evaluation.near_tied);
  output << ",\"decisive\":";
  writeCalibration(output, evaluation.decisive);
  output << "}}";
}

void writeGolden(const std::string& path, const FrozenModel& model,
                 const std::vector<RawPanel>& panels) {
  if (panels.size() < 4) {
    throw std::invalid_argument("not enough direct-ranker golden panels");
  }
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("could not open direct-ranker golden fixture");
  }
  output << std::setprecision(12)
         << "{\n  \"format\":\"drop7-direct-sibling-ranker-golden-v1\","
         << "\n  \"modelFingerprint\":\"0x" << std::hex
         << modelFingerprint(model.network, model.normalizer, model.variant)
         << std::dec << "\",\n  \"cases\":[";
  for (int fixture = 0; fixture < 4; ++fixture) {
    const RawPanel& panel =
        panels[static_cast<std::size_t>(fixture) * panels.size() / 4];
    if (fixture > 0) output << ',';
    output << "{\"publicHash\":\"0x" << std::hex
           << publicHash(panel.root.state) << std::dec
           << "\",\"actions\":[";
    for (std::size_t index = 0; index < panel.actions.size(); ++index) {
      if (index > 0) output << ',';
      const RawAction& action = panel.actions[index];
      const auto features = model.normalizer.normalize(action.features);
      const double score = model.network.predict(
          panel.root.state, action.action, features)[0];
      const double reflected = model.network.predict(
          constructive::mirror(panel.root.state),
          kBoardSize - 1 - action.action, features)[0];
      output << "{\"column\":" << action.action << ",\"ranker\":"
             << score << ",\"reflected\":" << reflected
             << ",\"fairD1\":" << action.d1_q
             << ",\"target\":" << action.label.mean_return << '}';
    }
    output << "]}";
  }
  output << "]\n}\n";
  if (!output) {
    throw std::runtime_error("failed writing direct-ranker golden fixture");
  }
}

struct Options {
  std::string checkpoint = "/tmp/drop7-direct-sibling-ranker.bin";
  std::string golden = "/tmp/drop7-direct-sibling-ranker-golden.json";
  std::string output = "/tmp/drop7-direct-sibling-ranker.json";
  int threads = 4;
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--checkpoint" && index + 1 < argc) {
      result.checkpoint = argv[++index];
    } else if (argument == "--golden" && index + 1 < argc) {
      result.golden = argv[++index];
    } else if (argument == "--output" && index + 1 < argc) {
      result.output = argv[++index];
    } else if (argument == "--threads" && index + 1 < argc) {
      result.threads = std::stoi(argv[++index]);
    } else {
      throw std::invalid_argument("unknown direct-ranker option");
    }
  }
  if (result.checkpoint.empty() || result.golden.empty() ||
      result.output.empty() || result.threads < 1 ||
      result.threads > kMaximumThreads) {
    throw std::invalid_argument("invalid direct-ranker options");
  }
  return result;
}

void writeD4(std::ostream& output, const D4Benchmark& d4) {
  output << "{\"overall\":";
  writeRanking(output, d4.overall);
  output << ",\"fairD1Origins\":";
  writeRanking(output, d4.d1_origins);
  output << ",\"constructiveOrigins\":";
  writeRanking(output, d4.constructive_origins);
  output << ",\"work\":" << d4.work << ",\"nodes\":" << d4.nodes
         << ",\"cacheHits\":" << d4.cache_hits
         << ",\"maximumCacheEntries\":" << d4.maximum_cache_entries
         << ",\"seconds\":" << d4.seconds << '}';
}

struct Timings {
  double collection = 0.0;
  double labels = 0.0;
  double features = 0.0;
  double cross_validation = 0.0;
  double final_training = 0.0;
  double wall = 0.0;
};

void writeArtifact(
    const Options& options, const RootCollection& roots,
    const LabelledCorpus& labels,
    const std::array<VariantResult, kVariants.size()>& variants,
    int selected_variant, double first_loss, double final_loss,
    const FrozenModel& model, const Evaluation& heldout,
    const D4Benchmark& d4, bool confirmation_worthy,
    const Timings& timings) {
  std::ofstream output(options.output);
  if (!output) {
    throw std::runtime_error("could not open direct-ranker artifact");
  }
  output << std::setprecision(12)
         << "{\n  \"format\":\"drop7-direct-sibling-ranker-v1\","
         << "\n  \"status\":\"developmental-complete\","
         << "\n  \"scope\":{\"developmentalOnly\":true,"
            "\"reusedBurnedCorpusOnly\":true,\"newGameplaySeeds\":0,"
            "\"gameplayRun\":false,\"screen\":null,"
            "\"originRange\":\"0x3d6c1000..0x3d6c13ff\","
            "\"oldWholeOriginHoldoutPreserved\":true},"
         << "\n  \"abortedFidelityAttempt\":{"
            "\"occurred\":true,\"stage\":\"post-label checksum guard\","
            "\"cause\":\"raw-byte public tape hash instead of predecessor cell-plus-one/phase-plus-one hash\","
            "\"modelTraining\":false,\"crossValidation\":false,"
            "\"d4Evaluation\":false,\"metricsUsed\":false,"
            "\"newGameplaySeeds\":0},"
         << "\n  \"corpus\":{\"originGames\":" << kOriginGames
         << ",\"trainingRoots\":" << roots.training.size()
         << ",\"heldoutRoots\":" << roots.heldout.size()
         << ",\"duplicateTraining\":" << roots.duplicate_training
         << ",\"duplicateHeldout\":" << roots.duplicate_heldout
         << ",\"heldoutOverlapPurged\":"
         << roots.heldout_overlap_purged
         << ",\"labelScenarios\":" << kLabelScenarios
         << ",\"labelHorizon\":" << kLabelHorizon
         << ",\"transitions\":" << labels.transitions
         << ",\"d1Work\":" << labels.d1_work << "},"
         << "\n  \"features\":{\"publicOnly\":true,"
            "\"reflection\":\"exact orientation average\","
            "\"rootBoardEmbedding\":true,\"candidateActionEmbedding\":true,"
            "\"numericCount\":" << kFeatureCount
         << ",\"numeric\":[\"root structural metrics\","
            "\"common-seven successor structural means\","
            "\"common-seven successor structural dispersion\","
            "\"exact fair-D1 Q and gap\","
            "\"expected immediate score and dispersion\","
            "\"terminal rate and action geometry\"]},"
         << "\n  \"training\":{\"objective\":"
            "\"grouped within-root advantages; pairwise/listwise ranking plus survival/clear/reveal/downside auxiliaries\","
            "\"folds\":" << kCrossValidationFolds
         << ",\"foldAssignment\":"
            "\"whole-origin, 128 games per policy per validation fold\","
            "\"epochs\":" << kEpochs << ",\"batchRoots\":"
         << kBatchRoots << ",\"learningRate\":" << kLearningRate
         << ",\"selectionRule\":"
            "\"minimum whole-origin-CV normalized regret, then pairwise, top2, smaller model\","
            "\"variants\":[";
  for (std::size_t variant = 0; variant < variants.size(); ++variant) {
    if (variant > 0) output << ',';
    const Variant& definition = kVariants[variant];
    const VariantResult& result = variants[variant];
    output << "{\"name\":\"" << definition.name << "\",\"hidden\":"
           << definition.hidden << ",\"pairWeight\":"
           << definition.pair_weight << ",\"listWeight\":"
           << definition.list_weight << ",\"pointWeight\":"
           << definition.point_weight << ",\"auxiliaryWeight\":"
           << definition.auxiliary_weight << ",\"selected\":"
           << (static_cast<int>(variant) == selected_variant ? "true"
                                                             : "false")
           << ",\"aggregate\":";
    writeEvaluation(output, result.aggregate);
    output << ",\"folds\":[";
    for (int fold = 0; fold < kCrossValidationFolds; ++fold) {
      if (fold > 0) output << ',';
      const FoldResult& record = result.folds[fold];
      output << "{\"fold\":" << fold
             << ",\"trainingOrigins\":" << record.training_origins
             << ",\"validationOrigins\":" << record.validation_origins
             << ",\"trainingRoots\":" << record.training_roots
             << ",\"validationRoots\":" << record.validation_roots
             << ",\"firstLoss\":" << record.first_loss
             << ",\"finalLoss\":" << record.final_loss
             << ",\"evaluation\":";
      writeEvaluation(output, record.evaluation);
      output << '}';
    }
    output << "]}";
  }
  output << "]},"
         << "\n  \"model\":{\"variant\":\""
         << kVariants[selected_variant].name << "\",\"hidden\":"
         << model.network.hidden() << ",\"parameters\":"
         << model.network.parameterCount() << ",\"serializedBytes\":"
         << deployBytes(model.network) << ",\"fingerprint\":\"0x"
         << std::hex
         << modelFingerprint(model.network, model.normalizer, model.variant)
         << std::dec << "\",\"firstLoss\":" << first_loss
         << ",\"finalLoss\":" << final_loss << "},"
         << "\n  \"burnedHoldout\":{\"developmental\":true,"
            "\"rankerVsD1\":";
  writeEvaluation(output, heldout);
  output << ",\"readOnlyExactD4\":";
  writeD4(output, d4);
  output << "},"
         << "\n  \"freshConfirmationGate\":{"
            "\"top1GainOverD1\":" << kRequiredTopOneGain
         << ",\"pairwiseGainOverD1\":" << kRequiredPairwiseGain
         << ",\"maximumRegretRatioToD1\":" << kRequiredRegretRatio
         << ",\"mustBeatD4Pairwise\":true,"
            "\"mustBeatD4Regret\":true,"
            "\"neitherPolicyHalfRegressesBothD1Metrics\":true,"
            "\"passed\":"
         << (confirmation_worthy ? "true" : "false") << "},"
         << "\n  \"checkpoint\":\"" << options.checkpoint
         << "\",\"golden\":\"" << options.golden << "\","
         << "\n  \"timing\":{\"collectionSeconds\":"
         << timings.collection << ",\"labelSeconds\":" << timings.labels
         << ",\"featureSeconds\":" << timings.features
         << ",\"crossValidationSeconds\":" << timings.cross_validation
         << ",\"finalTrainingSeconds\":" << timings.final_training
         << ",\"d4Seconds\":" << d4.seconds
         << ",\"wallSeconds\":" << timings.wall << "},"
         << "\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
  if (!output) {
    throw std::runtime_error("failed writing direct-ranker artifact");
  }
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

template <typename Function>
bool throwsInvalid(Function&& function) {
  try {
    function();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

PublicState selfTestFixture() {
  PublicState fixture;
  fixture.board.fill(kEmpty);
  fixture.board[indexOf(6, 0)] = kSolid;
  fixture.board[indexOf(5, 0)] = 6;
  fixture.board[indexOf(6, 1)] = kCracked;
  fixture.board[indexOf(6, 2)] = 5;
  fixture.board[indexOf(5, 2)] = 4;
  fixture.board[indexOf(6, 3)] = kSolid;
  fixture.board[indexOf(6, 4)] = 7;
  fixture.next_disc = 3;
  fixture.moves_remaining = 4;
  return fixture;
}

std::vector<RawPanel> selfTestPanels() {
  std::vector<RawPanel> result;
  for (int fixture = 0; fixture < 4; ++fixture) {
    Panel panel;
    panel.root.state = selfTestFixture();
    panel.root.state.next_disc =
        static_cast<std::uint8_t>(2 + fixture);
    panel.root.state.moves_remaining =
        static_cast<std::uint8_t>(1 + fixture);
    panel.root.state = canonicalPublic(panel.root.state);
    panel.root.fold = fixture % kCrossValidationFolds;
    for (const int action : constructive::kColumnOrder) {
      if (!isLegal(panel.root.state.board, action)) continue;
      ActionLabel label;
      label.action = action;
      label.mean_return = 20.0 + fixture +
                          0.5 * action - 0.3 * std::abs(action - 3);
      label.survival = static_cast<double>((action + fixture) % 3) / 2.0;
      label.clears = 12.0 + action + fixture;
      label.reveals = 5.0 + 0.5 * action;
      label.downside_return = label.mean_return - 4.0 - 0.2 * action;
      panel.actions.push_back(label);
    }
    result.push_back(prepareRawPanel(panel));
  }
  return result;
}

bool selfTest(const Options& options, std::ostream& output) {
  expect(kLevelBonus == 17'000, "Hardcore bonus regression");
  const SplitTable split = buildSplit();
  std::array<std::array<int, kCrossValidationFolds>, 2> fold_counts{};
  int heldout_d1 = 0;
  int heldout_constructive = 0;
  for (int game = 0; game < kOriginGames; ++game) {
    if (split.heldout[game]) {
      (game < 512 ? heldout_d1 : heldout_constructive)++;
    } else {
      ++fold_counts[game >= 512][split.fold[game]];
    }
  }
  expect(heldout_d1 == kHeldoutPerPolicy &&
             heldout_constructive == kHeldoutPerPolicy,
         "old whole-origin holdout changed");
  for (const auto& half : fold_counts) {
    for (const int count : half) {
      expect(count == 128, "whole-origin fold balance changed");
    }
  }
  expect(allowedOriginSeed(kOriginSeedStart) &&
             allowedOriginSeed(kOriginSeedEndExclusive - 1) &&
             !allowedOriginSeed(kOriginSeedStart - 1) &&
             !allowedOriginSeed(kOriginSeedEndExclusive) &&
             throwsInvalid([] { requireOriginSeed(0x3d6c'8000u); }) &&
             throwsInvalid([] { requireOriginSeed(0x4d00'0000u); }),
         "burned-corpus seed guard failed");

  // This byte-for-byte reference tape fixture distinguishes canonical key-byte
  // hashing: a raw-key-byte implementation hashes the state to
  // 0x921fa7fd97df7bdb.  Check it before any corpus replay.
  const PublicState tape_fixture = selfTestFixture();
  constexpr std::array<std::uint32_t, kLabelScenarios> expected_tapes{{
      0xdab4'f97du, 0x1bd0'51a3u, 0x47ac'ca51u, 0x3cc6'8255u,
      0xfeb1'3bbdu, 0xf0d6'f14au, 0x552f'8992u, 0x64d1'2bb8u,
      0xbafc'4bbbu, 0x1506'52e6u, 0xd68c'71a5u, 0xd251'4239u,
      0xcaef'373bu, 0xde49'0c0au, 0x7f1f'8ebeu,
  }};
  expect(publicHash(tape_fixture) == 0xef21'e6b7'394f'bd87ull,
         "predecessor public hash fixture changed");
  for (int scenario = 0; scenario < kLabelScenarios; ++scenario) {
    expect(tapeSeed(tape_fixture, scenario) == expected_tapes[scenario],
           "predecessor common-tape fixture changed");
  }

  std::vector<RawPanel> raw = selfTestPanels();
  const std::vector<std::size_t> indices = allIndices(raw.size());
  const Normalizer normalizer = fitNormalizer(raw, indices);
  const std::vector<PreparedPanel> prepared =
      preparePanels(raw, indices, normalizer);
  Deadline deadline;
  const std::uint32_t training_seed = mix32(kNetworkDomain ^ 0x5151u);
  TrainingResult first = train(prepared, kVariants[1], training_seed, 2,
                               deadline, false);
  TrainingResult repeated = train(prepared, kVariants[1], training_seed, 2,
                                  deadline, false);
  expect(first.network.parameters() == repeated.network.parameters() &&
             first.losses == repeated.losses,
         "grouped ranking training is not deterministic");

  const std::string checkpoint = options.checkpoint + ".self-test";
  const std::string golden = options.golden + ".self-test";
  saveCheckpoint(checkpoint, first.network, normalizer, 1);
  const FrozenModel frozen = loadCheckpoint(checkpoint);
  expect(frozen.network.parameters() == first.network.parameters() &&
             frozen.normalizer.feature_mean == normalizer.feature_mean &&
             deployBytes(frozen.network) <= kDeployLimitBytes,
         "checkpoint/resource round-trip failed");
  writeGolden(golden, frozen, raw);
  std::ifstream golden_input(golden);
  std::ostringstream golden_text;
  golden_text << golden_input.rdbuf();
  expect(golden_input.good() &&
             golden_text.str().find(
                 "drop7-direct-sibling-ranker-golden-v1") !=
                 std::string::npos,
         "golden fixture write failed");

  const PublicState fixture = selfTestFixture();
  const PublicScores direct = evaluatePublic(fixture, frozen);
  const PublicScores reflected =
      evaluatePublic(constructive::mirror(fixture), frozen);
  for (int action = 0; action < kBoardSize; ++action) {
    const int mirror_action = kBoardSize - 1 - action;
    expect(direct.legal[action] == reflected.legal[mirror_action] &&
               direct.ranker[action] == reflected.ranker[mirror_action] &&
               direct.fair_d1[action] == reflected.fair_d1[mirror_action],
           "public ranker reflection failed");
  }
  State metadata = constructive::materialize(fixture);
  metadata.score = 9'999'999;
  metadata.level = 777;
  metadata.moves_played = 888;
  expect(constructive::publicState(metadata) == fixture &&
             evaluatePublic(constructive::publicState(metadata), frozen) ==
                 direct,
         "public ranker used private metadata");

  const d4_benchmark::SearchDecision d4 =
      d4_benchmark::chooseDepth4Action(
          constructive::materialize(fixture));
  const d4_benchmark::SearchDecision d4_reflected =
      d4_benchmark::chooseDepth4Action(
          constructive::materialize(constructive::mirror(fixture)));
  expect(d4.complete && d4_reflected.complete &&
             d4_reflected.action == kBoardSize - 1 - d4.action,
         "exact D4 benchmark/reflection failed");
  for (int action = 0; action < kBoardSize; ++action) {
    expect(d4.root_values[action] ==
               d4_reflected.root_values[kBoardSize - 1 - action],
           "exact D4 Q reflection failed");
  }

  std::array<VariantResult, kVariants.size()> selection{};
  selection[0].aggregate.overall_ranker.normalized_regret = 0.30;
  selection[1].aggregate.overall_ranker.normalized_regret = 0.20;
  selection[2].aggregate.overall_ranker.normalized_regret = 0.25;
  expect(selectVariant(selection) == 1,
         "frozen whole-origin selection rule failed");
  Evaluation gate_evaluation;
  gate_evaluation.overall_d1 = {10, 10, 0.20, 0.40, 0.55, 0.40};
  gate_evaluation.overall_ranker = {10, 10, 0.30, 0.50, 0.65, 0.20};
  gate_evaluation.d1_baseline = gate_evaluation.overall_d1;
  gate_evaluation.constructive_baseline = gate_evaluation.overall_d1;
  gate_evaluation.d1_ranker = gate_evaluation.overall_ranker;
  gate_evaluation.constructive_ranker = gate_evaluation.overall_ranker;
  D4Benchmark gate_d4;
  gate_d4.overall = {10, 10, 0.25, 0.45, 0.64, 0.21};
  expect(justifiesFreshConfirmation(gate_evaluation, gate_d4),
         "D4-strengthened positive gate failed");
  gate_d4.overall.pairwise = 0.66;
  expect(!justifiesFreshConfirmation(gate_evaluation, gate_d4),
         "D4 pairwise rejection gate failed");
  gate_d4.overall.pairwise = 0.64;
  gate_d4.overall.normalized_regret = 0.19;
  expect(!justifiesFreshConfirmation(gate_evaluation, gate_d4),
         "D4 regret rejection gate failed");
  enforceResources();

  output << "DIRECT_SIBLING_RANKER_SELF_TEST {"
         << "\"passed\":true,\"burnedSeedGuard\":true,"
            "\"predecessorTapeChecksum\":true,"
            "\"wholeOriginFolds\":true,\"directPanels\":true,"
            "\"reflectionExact\":true,\"metadataBlind\":true,"
            "\"groupedLossDeterministic\":true,"
            "\"checkpointGolden\":true,\"d4ReadOnlyGate\":true,"
            "\"maximumParameters\":"
         << Layout(96).count << ",\"maximumSerializedBytes\":"
         << 8u + 5u * sizeof(std::uint32_t) + sizeof(std::uint64_t) +
                sizeof(Normalizer) +
                static_cast<std::uint64_t>(Layout(96).count) * sizeof(float)
         << ",\"peakRssBytes\":" << peakRssBytes() << "}\n";
  return true;
}

int run(const Options& options, std::ostream& output) {
  const Deadline deadline;
  const SplitTable split = buildSplit();
  Timings timings;

  Clock::time_point phase = Clock::now();
  RootCollection roots = collectAllRoots(split, options.threads, deadline);
  timings.collection =
      std::chrono::duration<double>(Clock::now() - phase).count();
  if (roots.training.size() != 5'611 || roots.heldout.size() != 1'871 ||
      roots.duplicate_training != 4 || roots.duplicate_heldout != 2 ||
      roots.heldout_overlap_purged != 2) {
    throw std::runtime_error("burned panel root corpus checksum changed");
  }
  std::cerr << "DIRECT_SIBLING_ROOTS {\"training\":"
            << roots.training.size() << ",\"heldout\":"
            << roots.heldout.size() << ",\"seconds\":"
            << timings.collection << "}\n";

  LabelledCorpus labels =
      labelRoots(roots, options.threads, deadline);
  timings.labels = labels.seconds;
  if (labels.transitions != 27'807'360ull ||
      labels.d1_work != 1'788'054'468ull) {
    throw std::runtime_error("burned panel labels checksum changed");
  }
  std::cerr << "DIRECT_SIBLING_LABELS {\"transitions\":"
            << labels.transitions << ",\"d1Work\":" << labels.d1_work
            << ",\"seconds\":" << labels.seconds << "}\n";

  phase = Clock::now();
  std::vector<RawPanel> training =
      prepareRawPanels(labels.training, deadline);
  std::vector<RawPanel> heldout =
      prepareRawPanels(labels.heldout, deadline);
  timings.features =
      std::chrono::duration<double>(Clock::now() - phase).count();
  labels.training.clear();
  labels.heldout.clear();
  enforceResources();

  phase = Clock::now();
  std::array<VariantResult, kVariants.size()> variants;
  for (int variant = 0; variant < static_cast<int>(kVariants.size());
       ++variant) {
    variants[variant] = crossValidate(training, variant, deadline);
    const RankingMetrics& metrics =
        variants[variant].aggregate.overall_ranker;
    std::cerr << "DIRECT_SIBLING_ABLATION {\"variant\":\""
              << kVariants[variant].name << "\",\"top1\":"
              << metrics.top1 << ",\"top2\":" << metrics.top2
              << ",\"pairwise\":" << metrics.pairwise
              << ",\"regret\":" << metrics.normalized_regret << "}\n";
  }
  timings.cross_validation =
      std::chrono::duration<double>(Clock::now() - phase).count();
  const int selected = selectVariant(variants);

  phase = Clock::now();
  const std::vector<std::size_t> training_indices =
      allIndices(training.size());
  const Normalizer normalizer =
      fitNormalizer(training, training_indices);
  const std::vector<PreparedPanel> prepared =
      preparePanels(training, training_indices, normalizer);
  const std::uint32_t final_seed = mix32(
      kNetworkDomain ^ static_cast<std::uint32_t>(selected + 1) ^
      0xf17a'11u);
  TrainingResult final_training =
      train(prepared, kVariants[selected], final_seed, kEpochs, deadline,
            true);
  timings.final_training =
      std::chrono::duration<double>(Clock::now() - phase).count();
  const double first_loss = final_training.losses.front();
  const double final_loss = final_training.losses.back();
  saveCheckpoint(options.checkpoint, final_training.network, normalizer,
                 selected);
  const FrozenModel model = loadCheckpoint(options.checkpoint);
  if (model.network.parameters() != final_training.network.parameters()) {
    throw std::runtime_error("frozen direct-ranker model mismatch");
  }
  const std::vector<std::size_t> heldout_indices =
      allIndices(heldout.size());
  const Evaluation heldout_evaluation = finish(evaluate(
      model.network, model.normalizer, heldout, heldout_indices, deadline));
  writeGolden(options.golden, model, heldout);

  // Lock the model and every selection decision before running this read-only
  // benchmark.  D4 values cannot influence fitting or ablation.
  const D4Benchmark d4 =
      evaluateD4Benchmark(heldout, options.threads, deadline);
  const bool confirmation_worthy =
      justifiesFreshConfirmation(heldout_evaluation, d4);
  timings.wall = deadline.seconds();
  writeArtifact(options, roots, labels, variants, selected, first_loss,
                final_loss, model, heldout_evaluation, d4,
                confirmation_worthy, timings);
  enforceResources();

  output << std::setprecision(12)
         << "DIRECT_SIBLING_RANKER_RESULT {\"developmental\":true,"
            "\"selected\":\"" << kVariants[selected].name
         << "\",\"rankerTop1\":"
         << heldout_evaluation.overall_ranker.top1
         << ",\"d1Top1\":" << heldout_evaluation.overall_d1.top1
         << ",\"d4Top1\":" << d4.overall.top1
         << ",\"rankerPairwise\":"
         << heldout_evaluation.overall_ranker.pairwise
         << ",\"d1Pairwise\":"
         << heldout_evaluation.overall_d1.pairwise
         << ",\"d4Pairwise\":" << d4.overall.pairwise
         << ",\"rankerRegret\":"
         << heldout_evaluation.overall_ranker.normalized_regret
         << ",\"d1Regret\":"
         << heldout_evaluation.overall_d1.normalized_regret
         << ",\"d4Regret\":" << d4.overall.normalized_regret
         << ",\"freshConfirmationRecommended\":"
         << (confirmation_worthy ? "true" : "false")
         << ",\"newGameplaySeeds\":0,\"wallSeconds\":"
         << timings.wall << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return EXIT_SUCCESS;
}

}  // namespace drop7::direct_sibling_ranker

int main(int argc, char** argv) {
  try {
    if (argc < 2) throw std::invalid_argument("missing mode");
    const std::string_view mode(argv[1]);
    const auto options =
        drop7::direct_sibling_ranker::parseOptions(argc, argv, 2);
    if (mode == "--self-test") {
      return drop7::direct_sibling_ranker::selfTest(options, std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (mode == "--run") {
      return drop7::direct_sibling_ranker::run(options, std::cout);
    }
    throw std::invalid_argument(
        "usage: drop7_direct_sibling_ranker --self-test | --run "
        "[--threads 1..8] [--checkpoint PATH] [--golden PATH] "
        "[--output PATH]");
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
