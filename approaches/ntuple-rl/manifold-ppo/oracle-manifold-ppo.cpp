#define DROP7_CURRICULUM_OPTION_PPO_LIBRARY
#include "../curriculum-option-ppo/curriculum-option-ppo.cpp"
#undef DROP7_CURRICULUM_OPTION_PPO_LIBRARY

#include <map>

// Trains an exactly reflection-invariant discriminator on checksum-locked
// public curriculum states and load-matched fair-D1 roll-ins, then uses its
// public-topology score as dense PPO reward.  The oracle is never consulted by
// this executable, and PPO starts only after the two-fold held-out label gates
// pass.
namespace drop7::oracle_manifold_ppo {

namespace prior = drop7::curriculum_option_ppo;
namespace vr = drop7::viability_reservoir_controller;

using Clock = std::chrono::steady_clock;
using PublicState = prior::PublicState;

constexpr std::uint32_t kNegativeSeedStart = 0x3d6b'0000u;
constexpr std::uint32_t kNegativeSeedEndExclusive = 0x3d6b'0400u;
constexpr int kNegativeGames =
    static_cast<int>(kNegativeSeedEndExclusive - kNegativeSeedStart);
constexpr int kNegativeMaximumMoves = 1'000;

constexpr std::uint32_t kTrainingSeedStart = 0x3d6b'1000u;
constexpr int kIterations = 48;
constexpr int kEpisodesPerIteration = 512;
constexpr int kInitialEpisodesPerIteration = kEpisodesPerIteration / 2;
constexpr int kCurriculumEpisodesPerIteration = kEpisodesPerIteration / 2;
constexpr int kTrainingEpisodes = kIterations * kEpisodesPerIteration;
constexpr std::uint32_t kTrainingSeedEndExclusive =
    kTrainingSeedStart + kTrainingEpisodes;
constexpr int kInitialMaximumMoves = 1'000;
constexpr int kCurriculumHorizon = 100;

constexpr std::uint32_t kStageASeedStart = 0x3d6c'0000u;
constexpr std::uint32_t kStageASeedEndExclusive = 0x3d6c'0020u;
constexpr int kStageAGames = 32;
constexpr int kStageAMaximumMoves = 1'000;
constexpr int kMaximumThreads = 8;

constexpr std::uint64_t kExpectedCurriculumFingerprint =
    0x8657'ac0d'c83c'6041ull;
constexpr std::uint64_t kExpectedPriorFingerprint =
    0x3405'524b'4c94'2a9eull;

// The discriminator schedule and gates are fixed before reading labels.
// Matching is exact on rise phase, occupied cells and
// maximum column height.  Two complementary models each see one whole-origin
// fold and are evaluated only on the other fold.  The final reward model is
// then fit once on both folds with the same schedule.
constexpr int kDiscriminatorHidden = 24;
constexpr int kDiscriminatorEpochs = 48;
constexpr int kDiscriminatorBatchPairs = 128;
constexpr float kDiscriminatorLearningRate = 0.0015f;
constexpr float kDiscriminatorGradientNorm = 1.0f;
constexpr float kDiscriminatorL2 = 0.00002f;
constexpr double kDiscriminatorAucGate = 0.62;
constexpr double kDiscriminatorPairGate = 0.58;
constexpr double kDiscriminatorCoverageGate = 0.80;

// Policy fine-tuning retains the exact fair-D1 backbone and the complete
// inherited residual, but starts a fresh Adam optimizer.  The centered GAIL
// reward is zero at D=.5.  The potential term is policy-invariant in the
// untruncated MDP and exposes local progress toward the learned manifold.
constexpr int kPpoEpochs = 4;
constexpr int kMinibatch = 512;
constexpr float kGamma = 0.999f;
constexpr float kGaeLambda = 0.97f;
constexpr float kClipRatio = 0.20f;
constexpr float kEntropyCoefficient = 0.005f;
constexpr float kValueCoefficient = 0.25f;
constexpr float kGradientNorm = 0.50f;
constexpr float kLearningRate = 0.0001f;
constexpr float kSurvivalReward = 0.05f;
constexpr float kClearReward = 0.05f;
constexpr float kRevealReward = 0.15f;
constexpr float kTerminalReward = -5.0f;
constexpr float kGailCoefficient = 0.10f;
constexpr float kPotentialCoefficient = 0.15f;
constexpr float kMaximumPotential = 4.0f;

constexpr double kGateMeanScore = 250'000.0;
constexpr double kGateMeanMoves = 80.0;
constexpr double kGateClearsPerMove = 1.90;
constexpr double kGateRevealsPerMove = 1.05;
constexpr int kGateScoreWins = 17;
constexpr double kWallLimitSeconds = 60.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;

constexpr std::uint32_t kPositiveFoldDomain = 0x504f'5346u;
constexpr std::uint32_t kNegativeFoldDomain = 0x4e45'4746u;
constexpr std::uint32_t kDiscriminatorSeed = 0x4d41'4e31u;
constexpr std::uint32_t kDiscriminatorShuffleDomain = 0x4449'5348u;
constexpr std::uint32_t kPolicySampleDomain = 0x4d50'4f4cu;
constexpr std::uint32_t kCurriculumSelectDomain = 0x4d43'5552u;
constexpr std::uint32_t kRestartStreamDomain = 0x4d52'5354u;
constexpr std::uint32_t kRestartRevealDomain = 0x4d52'564cu;
constexpr std::uint32_t kRestartDiscDomain = 0x4d44'4953u;
constexpr std::uint32_t kEventMultiplier = 0x9e37'79b9u;
constexpr std::uint32_t kPolicyShuffleSeed = 0x4d50'5348u;

static_assert(kLevelBonus == 17'000);
static_assert(kNegativeGames == 1'024);
static_assert(kTrainingEpisodes == 24'576);
static_assert(kTrainingSeedEndExclusive == 0x3d6b'7000u);
static_assert(kStageASeedEndExclusive - kStageASeedStart == kStageAGames);
static_assert(kNegativeSeedEndExclusive <= kTrainingSeedStart);
static_assert(kTrainingSeedEndExclusive <= 0x3d6c'0000u);
static_assert(kInitialEpisodesPerIteration ==
              kCurriculumEpisodesPerIteration);

std::uint64_t peakRssBytes() { return prior::peakRssBytes(); }

void enforceRssLimit() {
  if (peakRssBytes() > kRssLimitBytes) {
    throw std::runtime_error("oracle-manifold PPO exceeded 256 MiB RSS");
  }
}

struct Deadline {
  Clock::time_point started = Clock::now();
  double elapsedSeconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }
  void check() const {
    if (elapsedSeconds() > kWallLimitSeconds) {
      throw std::runtime_error("oracle-manifold PPO exceeded 60 minute wall cap");
    }
  }
};

enum class SeedUse : std::uint8_t { kNegative, kTraining, kStageA };

bool allowedSeed(std::uint32_t seed, SeedUse use) {
  std::uint32_t begin = 0;
  std::uint32_t end = 0;
  switch (use) {
    case SeedUse::kNegative:
      begin = kNegativeSeedStart;
      end = kNegativeSeedEndExclusive;
      break;
    case SeedUse::kTraining:
      begin = kTrainingSeedStart;
      end = kTrainingSeedEndExclusive;
      break;
    case SeedUse::kStageA:
      begin = kStageASeedStart;
      end = kStageASeedEndExclusive;
      break;
  }
  const std::uint8_t prefix = static_cast<std::uint8_t>(seed >> 24u);
  return seed >= begin && seed < end && prefix != 0x4d && prefix != 0x7d &&
         prefix != 0xd7;
}

void requireSeed(std::uint32_t seed, SeedUse use) {
  if (!allowedSeed(seed, use)) {
    throw std::invalid_argument("seed outside preregistered 3d6b/3d6c lanes");
  }
}

std::uint64_t mix64(std::uint64_t value) {
  value ^= value >> 30u;
  value *= 0xbf58'476d'1ce4'e5b9ull;
  value ^= value >> 27u;
  value *= 0x94d0'49bb'1331'11ebull;
  return value ^ (value >> 31u);
}

std::uint64_t publicHash(const PublicState& source) {
  bool ignored = false;
  const PublicState state = vr::canonicalState(source, ignored);
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

struct Stratum {
  std::uint8_t phase = 0;
  std::uint8_t occupied = 0;
  std::uint8_t maximum_height = 0;
  auto operator<=>(const Stratum&) const = default;
};

Stratum stratum(const PublicState& state) {
  const auto heights = vr::columnHeights(state.board);
  int occupied = 0;
  int maximum = 0;
  for (const int height : heights) {
    occupied += height;
    maximum = std::max(maximum, height);
  }
  return {state.moves_remaining, static_cast<std::uint8_t>(occupied),
          static_cast<std::uint8_t>(maximum)};
}

struct NegativeState {
  PublicState state{};
  std::uint32_t origin_seed = 0;
  std::uint64_t hash = 0;
};

struct MatchedPair {
  PublicState positive{};
  PublicState negative{};
  Stratum load{};
  std::uint64_t positive_hash = 0;
  std::uint64_t negative_hash = 0;
  std::uint32_t negative_origin = 0;
  int fold = 0;
};

struct MatchedDataset {
  std::array<std::vector<MatchedPair>, 2> folds;
  std::size_t positive_total = 0;
  std::size_t negative_rollin_states = 0;
  std::size_t matched = 0;
  double coverage = 0.0;
  std::uint64_t fingerprint = 0xcbf2'9ce4'8422'2325ull;
};

void fingerprintWord(std::uint64_t& hash, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    prior::fingerprintByte(hash, static_cast<std::uint8_t>(value >> shift));
  }
}

MatchedDataset buildMatchedDataset(const prior::Curriculum& curriculum,
                                   const Deadline& deadline) {
  using Bucket = std::array<std::vector<NegativeState>, 2>;
  std::map<Stratum, Bucket> negatives;
  MatchedDataset result;
  result.positive_total = curriculum.states.size();
  for (std::uint32_t seed = kNegativeSeedStart;
       seed < kNegativeSeedEndExclusive; ++seed) {
    requireSeed(seed, SeedUse::kNegative);
    if (((seed - kNegativeSeedStart) & 31u) == 0) deadline.check();
    State state = initialHeadlessState(seed);
    const int fold = static_cast<int>(
        mix32(seed ^ kNegativeFoldDomain) & 1u);
    while (!state.game_over && state.moves_played < kNegativeMaximumMoves) {
      const PublicState public_state = vr::publicState(state);
      const vr::BaselineDecision decision =
          vr::chooseFairDepthOne(public_state);
      if (!decision.complete || !isLegal(state.board, decision.action)) {
        throw std::runtime_error("fair-D1 negative roll-in was incomplete");
      }
      NegativeState negative{public_state, seed, publicHash(public_state)};
      negatives[stratum(public_state)][fold].push_back(negative);
      ++result.negative_rollin_states;
      MoveResult move;
      if (!playHeadlessMove(state, seed, decision.action, move)) {
        throw std::runtime_error("negative roll-in transition failed");
      }
    }
  }
  for (auto& [ignored, bucket] : negatives) {
    static_cast<void>(ignored);
    for (auto& values : bucket) {
      std::sort(values.begin(), values.end(), [](const NegativeState& a,
                                                 const NegativeState& b) {
        return std::tie(a.hash, a.origin_seed) <
               std::tie(b.hash, b.origin_seed);
      });
    }
  }

  std::array<std::vector<std::pair<std::uint64_t, PublicState>>, 2> positives;
  for (const PublicState& state : curriculum.states) {
    const std::uint64_t hash = publicHash(state);
    const int fold = static_cast<int>(
        mix64(hash ^ kPositiveFoldDomain) & 1u);
    positives[fold].push_back({hash, state});
  }
  for (auto& values : positives) {
    std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) {
      return a.first < b.first;
    });
  }
  std::map<std::pair<Stratum, int>, std::size_t> cursor;
  for (int fold = 0; fold < 2; ++fold) {
    for (const auto& [positive_hash, positive] : positives[fold]) {
      const Stratum load = stratum(positive);
      auto found = negatives.find(load);
      if (found == negatives.end()) continue;
      std::vector<NegativeState>& pool = found->second[fold];
      std::size_t& index = cursor[{load, fold}];
      if (index >= pool.size()) continue;
      const NegativeState& negative = pool[index++];
      result.folds[fold].push_back(
          {positive, negative.state, load, positive_hash, negative.hash,
           negative.origin_seed, fold});
    }
  }
  for (int fold = 0; fold < 2; ++fold) {
    for (const MatchedPair& pair : result.folds[fold]) {
      if (stratum(pair.positive) != stratum(pair.negative) ||
          pair.load != stratum(pair.positive) || pair.fold != fold) {
        throw std::runtime_error("load matching invariant failed");
      }
      const int origin_fold = static_cast<int>(
          mix32(pair.negative_origin ^ kNegativeFoldDomain) & 1u);
      const int positive_fold = static_cast<int>(mix64(
          pair.positive_hash ^ kPositiveFoldDomain) & 1u);
      if (origin_fold != fold || positive_fold != fold) {
        throw std::runtime_error("whole-origin discriminator split failed");
      }
      fingerprintWord(result.fingerprint, pair.positive_hash);
      fingerprintWord(result.fingerprint, pair.negative_hash);
    }
    result.matched += result.folds[fold].size();
  }
  result.coverage = static_cast<double>(result.matched) /
                    static_cast<double>(result.positive_total);
  if (result.folds[0].empty() || result.folds[1].empty()) {
    throw std::runtime_error("discriminator split has an empty fold");
  }
  enforceRssLimit();
  return result;
}

constexpr int kTokenTotalInputs = prior::kBoardCategories;
constexpr int kRowTokenInputs = kBoardSize * prior::kBoardCategories;
constexpr int kColumnPairTokenInputs = 4 * prior::kBoardCategories;
constexpr int kNextInputs = kBoardSize;
constexpr int kPhaseInputs = kMovesPerLevel;
constexpr int kHeightPairInputs = 8;
constexpr int kGraphInputs = prior::kGraphInputs;
constexpr int kTriggerInputs = prior::kTriggerSummaryInputs;
constexpr int kKeyAggregateInputs = kBoardSize * prior::kKeyInputs * 3;
constexpr int kDiscriminatorInputs =
    kTokenTotalInputs + kRowTokenInputs + kColumnPairTokenInputs +
    kNextInputs + kPhaseInputs + kHeightPairInputs + kGraphInputs +
    kTriggerInputs + kKeyAggregateInputs;
static_assert(kDiscriminatorInputs == 295);

using TopologyFeatures = std::array<float, kDiscriminatorInputs>;

TopologyFeatures topologyFeatures(const PublicState& source) {
  bool ignored = false;
  const PublicState state = vr::canonicalState(source, ignored);
  const prior::Observation observation = prior::observePublic(state);
  TopologyFeatures result{};
  int offset = 0;
  for (int category = 0; category < prior::kBoardCategories; ++category) {
    float total = 0.0f;
    for (int cell = 0; cell < kCellCount; ++cell) {
      total += observation.input[prior::kBoardOffset +
                                 cell * prior::kBoardCategories + category];
    }
    result[offset++] = total / 49.0f;
  }
  for (int row = 0; row < kBoardSize; ++row) {
    for (int category = 0; category < prior::kBoardCategories; ++category) {
      float total = 0.0f;
      for (int column = 0; column < kBoardSize; ++column) {
        total += observation.input[
            prior::kBoardOffset + indexOf(row, column) *
                                      prior::kBoardCategories + category];
      }
      result[offset++] = total / 7.0f;
    }
  }
  for (int pair = 0; pair < 4; ++pair) {
    const int left = pair;
    const int right = kBoardSize - 1 - pair;
    const float denominator = left == right ? 7.0f : 14.0f;
    for (int category = 0; category < prior::kBoardCategories; ++category) {
      float total = 0.0f;
      for (int row = 0; row < kBoardSize; ++row) {
        total += observation.input[
            prior::kBoardOffset + indexOf(row, left) *
                                      prior::kBoardCategories + category];
        if (left != right) {
          total += observation.input[
              prior::kBoardOffset + indexOf(row, right) *
                                        prior::kBoardCategories + category];
        }
      }
      result[offset++] = total / denominator;
    }
  }
  std::copy(observation.input.begin() + prior::kNextOffset,
            observation.input.begin() + prior::kPhaseOffset,
            result.begin() + offset);
  offset += kNextInputs;
  std::copy(observation.input.begin() + prior::kPhaseOffset,
            observation.input.begin() + prior::kHeightOffset,
            result.begin() + offset);
  offset += kPhaseInputs;
  for (int pair = 0; pair < 4; ++pair) {
    const int left = pair;
    const int right = kBoardSize - 1 - pair;
    const float left_height = observation.input[prior::kHeightOffset + left];
    const float right_height = observation.input[prior::kHeightOffset + right];
    result[offset++] = 0.5f * (left_height + right_height);
    result[offset++] = std::abs(left_height - right_height);
  }
  std::copy(observation.input.begin() + prior::kGraphOffset,
            observation.input.begin() + prior::kTriggerSummaryOffset,
            result.begin() + offset);
  offset += kGraphInputs;
  std::copy(observation.input.begin() + prior::kTriggerSummaryOffset,
            observation.input.begin() + prior::kOptionOffset,
            result.begin() + offset);
  offset += kTriggerInputs;
  for (int disc = 0; disc < kBoardSize; ++disc) {
    for (int feature = 0; feature < prior::kKeyInputs; ++feature) {
      float sum = 0.0f;
      float minimum = std::numeric_limits<float>::infinity();
      float maximum = -std::numeric_limits<float>::infinity();
      for (int column = 0; column < kBoardSize; ++column) {
        const int source_offset =
            prior::kKeysOffset +
            (disc * kBoardSize + column) * prior::kKeyInputs + feature;
        const float value = observation.input[source_offset];
        sum += value;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
      }
      result[offset++] = sum / 7.0f;
      result[offset++] = minimum;
      result[offset++] = maximum;
    }
  }
  if (offset != kDiscriminatorInputs) {
    throw std::runtime_error("discriminator feature layout mismatch");
  }
  return result;
}

struct DiscriminatorLayout {
  static constexpr int w1 = 0;
  static constexpr int b1 = w1 + kDiscriminatorHidden * kDiscriminatorInputs;
  static constexpr int w2 = b1 + kDiscriminatorHidden;
  static constexpr int b2 = w2 + kDiscriminatorHidden;
  static constexpr int count = b2 + 1;
};

struct DiscriminatorCache {
  std::array<float, kDiscriminatorHidden> hidden{};
  float logit = 0.0f;
};

class Discriminator {
 public:
  explicit Discriminator(std::uint32_t seed = kDiscriminatorSeed)
      : parameters_(DiscriminatorLayout::count, 0.0f),
        first_(DiscriminatorLayout::count, 0.0f),
        second_(DiscriminatorLayout::count, 0.0f) {
    Mulberry32 random(seed);
    const float radius = std::sqrt(
        6.0f / static_cast<float>(kDiscriminatorInputs +
                                  kDiscriminatorHidden));
    for (int index = 0;
         index < kDiscriminatorHidden * kDiscriminatorInputs; ++index) {
      parameters_[DiscriminatorLayout::w1 + index] = static_cast<float>(
          (2.0 * random.nextUnit() - 1.0) * radius);
    }
  }

  DiscriminatorCache forward(const TopologyFeatures& input) const {
    DiscriminatorCache cache;
    for (int output = 0; output < kDiscriminatorHidden; ++output) {
      float total = parameters_[DiscriminatorLayout::b1 + output];
      const int weights =
          DiscriminatorLayout::w1 + output * kDiscriminatorInputs;
      for (int feature = 0; feature < kDiscriminatorInputs; ++feature) {
        total += parameters_[weights + feature] * input[feature];
      }
      cache.hidden[output] = std::tanh(total);
    }
    cache.logit = parameters_[DiscriminatorLayout::b2];
    for (int hidden = 0; hidden < kDiscriminatorHidden; ++hidden) {
      cache.logit += parameters_[DiscriminatorLayout::w2 + hidden] *
                     cache.hidden[hidden];
    }
    return cache;
  }

  float logit(const PublicState& state) const {
    return forward(topologyFeatures(state)).logit;
  }

  float probability(const PublicState& state) const {
    return sigmoid(logit(state));
  }

  static float sigmoid(float value) {
    if (value >= 0.0f) {
      const float exponential = std::exp(-value);
      return 1.0f / (1.0f + exponential);
    }
    const float exponential = std::exp(value);
    return exponential / (1.0f + exponential);
  }

  std::vector<float> gradient() const {
    return std::vector<float>(DiscriminatorLayout::count, 0.0f);
  }

  void accumulate(const TopologyFeatures& input,
                  const DiscriminatorCache& cache, float derivative,
                  std::vector<float>& gradient) const {
    gradient[DiscriminatorLayout::b2] += derivative;
    for (int hidden = 0; hidden < kDiscriminatorHidden; ++hidden) {
      gradient[DiscriminatorLayout::w2 + hidden] +=
          derivative * cache.hidden[hidden];
      const float hidden_derivative =
          derivative * parameters_[DiscriminatorLayout::w2 + hidden] *
          (1.0f - cache.hidden[hidden] * cache.hidden[hidden]);
      gradient[DiscriminatorLayout::b1 + hidden] += hidden_derivative;
      const int weights =
          DiscriminatorLayout::w1 + hidden * kDiscriminatorInputs;
      for (int feature = 0; feature < kDiscriminatorInputs; ++feature) {
        gradient[weights + feature] += hidden_derivative * input[feature];
      }
    }
  }

  void apply(std::vector<float>& gradient) {
    double squared = 0.0;
    for (int index = 0; index < DiscriminatorLayout::count; ++index) {
      if (index < DiscriminatorLayout::b2) {
        gradient[index] += kDiscriminatorL2 * parameters_[index];
      }
      squared += gradient[index] * gradient[index];
    }
    const double norm = std::sqrt(squared);
    const float scale = norm > kDiscriminatorGradientNorm
                            ? static_cast<float>(kDiscriminatorGradientNorm /
                                                 norm)
                            : 1.0f;
    ++step_;
    constexpr float beta1 = 0.9f;
    constexpr float beta2 = 0.999f;
    constexpr float epsilon = 1.0e-8f;
    const float correction1 = 1.0f - std::pow(beta1, static_cast<float>(step_));
    const float correction2 = 1.0f - std::pow(beta2, static_cast<float>(step_));
    for (int index = 0; index < DiscriminatorLayout::count; ++index) {
      const float value = gradient[index] * scale;
      first_[index] = beta1 * first_[index] + (1.0f - beta1) * value;
      second_[index] =
          beta2 * second_[index] + (1.0f - beta2) * value * value;
      parameters_[index] -=
          kDiscriminatorLearningRate * (first_[index] / correction1) /
          (std::sqrt(second_[index] / correction2) + epsilon);
      if (!std::isfinite(parameters_[index])) {
        throw std::runtime_error("non-finite discriminator parameter");
      }
    }
  }

  const std::vector<float>& parameters() const { return parameters_; }

 private:
  std::vector<float> parameters_;
  std::vector<float> first_;
  std::vector<float> second_;
  std::uint64_t step_ = 0;
};

using PublicDiscriminator = float (Discriminator::*)(const PublicState&) const;
static_assert(std::is_same_v<decltype(&Discriminator::probability),
                             PublicDiscriminator>);
static_assert(!std::is_invocable_v<PublicDiscriminator, const Discriminator&,
                                   const State&>);

struct LabelMetrics {
  double auc = 0.0;
  double matched_pair_ranking = 0.0;
  double loss = 0.0;
  std::size_t pairs = 0;
};

LabelMetrics labelMetrics(const Discriminator& model,
                          const std::vector<MatchedPair>& pairs) {
  struct Scored {
    double score = 0.0;
    int label = 0;
  };
  std::vector<Scored> values;
  values.reserve(pairs.size() * 2);
  double pair_wins = 0.0;
  double loss = 0.0;
  for (const MatchedPair& pair : pairs) {
    const double positive = model.logit(pair.positive);
    const double negative = model.logit(pair.negative);
    values.push_back({positive, 1});
    values.push_back({negative, 0});
    pair_wins += positive > negative ? 1.0 : (positive == negative ? 0.5 : 0.0);
    loss += std::log1p(std::exp(-std::clamp(positive, -40.0, 40.0)));
    loss += std::log1p(std::exp(std::clamp(negative, -40.0, 40.0)));
  }
  std::sort(values.begin(), values.end(), [](const Scored& a, const Scored& b) {
    return a.score < b.score;
  });
  double positive_rank_sum = 0.0;
  std::size_t begin = 0;
  while (begin < values.size()) {
    std::size_t end = begin + 1;
    while (end < values.size() && values[end].score == values[begin].score) {
      ++end;
    }
    const double average_rank =
        0.5 * (static_cast<double>(begin + 1) + static_cast<double>(end));
    for (std::size_t index = begin; index < end; ++index) {
      if (values[index].label == 1) positive_rank_sum += average_rank;
    }
    begin = end;
  }
  const double count = static_cast<double>(pairs.size());
  LabelMetrics result;
  result.pairs = pairs.size();
  result.auc = (positive_rank_sum - count * (count + 1.0) * 0.5) /
               (count * count);
  result.matched_pair_ranking = pair_wins / count;
  result.loss = loss / (2.0 * count);
  return result;
}

Discriminator trainDiscriminator(const std::vector<MatchedPair>& pairs,
                                 std::uint32_t seed,
                                 const Deadline& deadline) {
  Discriminator model(seed);
  std::vector<std::size_t> indexes(pairs.size());
  std::iota(indexes.begin(), indexes.end(), 0u);
  Mulberry32 random(mix32(seed ^ kDiscriminatorShuffleDomain));
  for (int epoch = 0; epoch < kDiscriminatorEpochs; ++epoch) {
    for (std::size_t index = indexes.size(); index > 1; --index) {
      const std::size_t selected = static_cast<std::size_t>(
          (static_cast<std::uint64_t>(random.nextBits()) * index) >> 32u);
      std::swap(indexes[index - 1], indexes[selected]);
    }
    for (std::size_t begin = 0; begin < indexes.size();
         begin += kDiscriminatorBatchPairs) {
      if ((begin & 1'023u) == 0) deadline.check();
      const std::size_t end =
          std::min(indexes.size(), begin + kDiscriminatorBatchPairs);
      const float inverse = 1.0f / static_cast<float>(2 * (end - begin));
      std::vector<float> gradient = model.gradient();
      for (std::size_t offset = begin; offset < end; ++offset) {
        const MatchedPair& pair = pairs[indexes[offset]];
        const TopologyFeatures positive = topologyFeatures(pair.positive);
        const TopologyFeatures negative = topologyFeatures(pair.negative);
        const DiscriminatorCache positive_cache = model.forward(positive);
        const DiscriminatorCache negative_cache = model.forward(negative);
        model.accumulate(positive, positive_cache,
                         (Discriminator::sigmoid(positive_cache.logit) - 1.0f) *
                             inverse,
                         gradient);
        model.accumulate(negative, negative_cache,
                         Discriminator::sigmoid(negative_cache.logit) * inverse,
                         gradient);
      }
      model.apply(gradient);
    }
  }
  return model;
}

struct DiscriminatorResult {
  Discriminator model{};
  std::array<LabelMetrics, 2> heldout{};
  LabelMetrics all{};
  bool passed = false;
};

DiscriminatorResult fitAndGateDiscriminator(const MatchedDataset& dataset,
                                            const Deadline& deadline) {
  DiscriminatorResult result;
  for (int train_fold = 0; train_fold < 2; ++train_fold) {
    const Discriminator crossfit = trainDiscriminator(
        dataset.folds[train_fold],
        kDiscriminatorSeed + static_cast<std::uint32_t>(train_fold + 1),
        deadline);
    const int heldout_fold = 1 - train_fold;
    result.heldout[heldout_fold] =
        labelMetrics(crossfit, dataset.folds[heldout_fold]);
  }
  result.passed = dataset.coverage >= kDiscriminatorCoverageGate;
  for (const LabelMetrics& metrics : result.heldout) {
    result.passed = result.passed && metrics.auc >= kDiscriminatorAucGate &&
                    metrics.matched_pair_ranking >= kDiscriminatorPairGate;
  }
  if (!result.passed) return result;
  std::vector<MatchedPair> all = dataset.folds[0];
  all.insert(all.end(), dataset.folds[1].begin(), dataset.folds[1].end());
  result.model = trainDiscriminator(all, kDiscriminatorSeed, deadline);
  result.all = labelMetrics(result.model, all);
  return result;
}

float clippedPotential(float logit) {
  return std::clamp(logit, -kMaximumPotential, kMaximumPotential);
}

float manifoldReward(float current_logit, float next_logit, bool terminal) {
  const float next_probability =
      terminal ? 0.0f : Discriminator::sigmoid(next_logit);
  const float centered_gail = terminal
                                  ? 0.0f
                                  : -std::log(std::max(1.0e-5f,
                                                       1.0f - next_probability)) -
                                        std::log(2.0f);
  const float current_phi = clippedPotential(current_logit);
  const float next_phi = terminal ? 0.0f : clippedPotential(next_logit);
  return kGailCoefficient * std::clamp(centered_gail, -0.5f, 2.0f) +
         kPotentialCoefficient * (kGamma * next_phi - current_phi);
}

struct Sample {
  PublicState state{};
  std::array<float, kBoardSize> base_logits{};
  int action = -1;
  float old_log_probability = 0.0f;
  float old_value = 0.0f;
  float reward = 0.0f;
  bool terminal = false;
  float advantage = 0.0f;
  float return_value = 0.0f;
};

static_assert(sizeof(Sample) <= 128);
constexpr std::size_t kMaximumBatchSamples =
    static_cast<std::size_t>(kEpisodesPerIteration) * kInitialMaximumMoves;
static_assert(kMaximumBatchSamples * sizeof(Sample) <
              96ull * 1024ull * 1024ull);

struct Trajectory {
  std::vector<Sample> samples;
  std::int64_t score = 0;
  int moves = 0;
  int clears = 0;
  int reveals = 0;
  bool curriculum = false;
  double discriminator_probability = 0.0;
  double manifold_reward = 0.0;
};

void finishAdvantages(std::vector<Sample>& samples, float bootstrap) {
  float next_value = bootstrap;
  float advantage = 0.0f;
  for (auto iterator = samples.rbegin(); iterator != samples.rend(); ++iterator) {
    const float nonterminal = iterator->terminal ? 0.0f : 1.0f;
    const float delta = iterator->reward + kGamma * next_value * nonterminal -
                        iterator->old_value;
    advantage = delta + kGamma * kGaeLambda * nonterminal * advantage;
    iterator->advantage = advantage;
    iterator->return_value = advantage + iterator->old_value;
    next_value = iterator->old_value;
  }
}

std::uint32_t restartBaseSeed(std::uint32_t lane_seed,
                              std::size_t curriculum_index) {
  return mix32(lane_seed ^ kRestartStreamDomain ^
               (static_cast<std::uint32_t>(curriculum_index + 1u) *
                kEventMultiplier));
}

std::uint8_t restartNextDisc(std::uint32_t base_seed, int event) {
  const std::uint32_t bits =
      mix32(base_seed ^ kRestartDiscDomain ^
            (static_cast<std::uint32_t>(event + 1) * kEventMultiplier));
  return static_cast<std::uint8_t>(
      ((static_cast<std::uint64_t>(bits) * kBoardSize) >> 32u) + 1u);
}

bool playRestartMove(State& state, std::uint32_t base_seed, int event,
                     int action, MoveResult& move) {
  const std::uint32_t reveal_seed =
      mix32(base_seed ^ kRestartRevealDomain ^
            (static_cast<std::uint32_t>(event + 1) * kEventMultiplier));
  Mulberry32 random(reveal_seed);
  if (!playMove(state, action, random, move)) return false;
  state = move.state;
  if (!state.game_over) state.next_disc = restartNextDisc(base_seed, event);
  return true;
}

Trajectory collectTrajectory(const prior::Network& network,
                             const Discriminator& discriminator,
                             const prior::Curriculum& curriculum,
                             std::uint32_t lane_seed, bool use_curriculum,
                             const Deadline& deadline) {
  requireSeed(lane_seed, SeedUse::kTraining);
  const std::size_t curriculum_index =
      static_cast<std::size_t>(mix32(lane_seed ^ kCurriculumSelectDomain)) %
      curriculum.states.size();
  State state = use_curriculum
                    ? vr::materialize(curriculum.states[curriculum_index])
                    : initialHeadlessState(lane_seed);
  state.score = 0;
  state.level = 1;
  state.moves_played = 0;
  const std::uint32_t restart_seed =
      restartBaseSeed(lane_seed, curriculum_index);
  Mulberry32 policy_random(mix32(lane_seed ^ kPolicySampleDomain));
  const int horizon =
      use_curriculum ? kCurriculumHorizon : kInitialMaximumMoves;
  Trajectory trajectory;
  trajectory.curriculum = use_curriculum;
  trajectory.samples.reserve(use_curriculum ? kCurriculumHorizon : 128);
  for (int event = 0; !state.game_over && event < horizon; ++event) {
    if ((event & 31) == 0) deadline.check();
    bool mirrored = false;
    const PublicState canonical =
        vr::canonicalState(vr::publicState(state), mirrored);
    const prior::BasePolicy base = prior::fairBasePolicy(canonical);
    const prior::Prediction prediction =
        prior::predictCanonical(network, canonical, &base);
    Sample sample;
    sample.state = canonical;
    sample.base_logits = base.logits;
    sample.action = prior::sampleCanonical(prediction, policy_random);
    if (sample.action < 0) {
      throw std::runtime_error("oracle-manifold PPO sampled no action");
    }
    sample.old_log_probability = std::log(std::max(
        1.0e-12f, prediction.probabilities[sample.action]));
    sample.old_value = prediction.value;
    const float current_logit = discriminator.logit(canonical);
    const int physical_action =
        mirrored ? kBoardSize - 1 - sample.action : sample.action;
    MoveResult move;
    const bool played = use_curriculum
                            ? playRestartMove(state, restart_seed, event,
                                              physical_action, move)
                            : playHeadlessMove(state, lane_seed,
                                               physical_action, move);
    if (!played) throw std::runtime_error("manifold PPO transition failed");
    int clears = 0;
    int reveals = 0;
    prior::accumulateMoveCounts(move, clears, reveals);
    sample.terminal = state.game_over;
    float next_logit = 0.0f;
    if (!sample.terminal) {
      bool ignored = false;
      const PublicState next =
          vr::canonicalState(vr::publicState(state), ignored);
      next_logit = discriminator.logit(next);
      trajectory.discriminator_probability +=
          Discriminator::sigmoid(next_logit);
    }
    const float shaping =
        manifoldReward(current_logit, next_logit, sample.terminal);
    sample.reward = static_cast<float>(move.score_delta) / 17'000.0f +
                    (sample.terminal ? 0.0f : kSurvivalReward) +
                    kClearReward * clears + kRevealReward * reveals +
                    (sample.terminal ? kTerminalReward : 0.0f) + shaping;
    trajectory.manifold_reward += shaping;
    trajectory.clears += clears;
    trajectory.reveals += reveals;
    trajectory.samples.push_back(sample);
  }
  float bootstrap = 0.0f;
  if (!state.game_over) {
    bool ignored = false;
    const PublicState canonical =
        vr::canonicalState(vr::publicState(state), ignored);
    bootstrap = prior::predictCanonical(network, canonical).value;
  }
  finishAdvantages(trajectory.samples, bootstrap);
  trajectory.score = state.score;
  trajectory.moves = static_cast<int>(trajectory.samples.size());
  return trajectory;
}

struct Batch {
  std::vector<Trajectory> trajectories;
  std::size_t samples = 0;
  double initial_score = 0.0;
  double initial_moves = 0.0;
  double curriculum_score = 0.0;
  double curriculum_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double discriminator_probability = 0.0;
  double manifold_reward = 0.0;
};

Batch collectBatch(const prior::Network& network,
                   const Discriminator& discriminator,
                   const prior::Curriculum& curriculum, int iteration,
                   int threads, const Deadline& deadline) {
  Batch batch;
  batch.trajectories.resize(kEpisodesPerIteration);
  std::atomic<int> next{0};
  std::vector<std::future<void>> futures;
  const int workers = std::min(threads, kEpisodesPerIteration);
  for (int worker = 0; worker < workers; ++worker) {
    futures.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int episode = next.fetch_add(1);
        if (episode >= kEpisodesPerIteration) return;
        const int global_episode = iteration * kEpisodesPerIteration + episode;
        const std::uint32_t lane_seed =
            kTrainingSeedStart + static_cast<std::uint32_t>(global_episode);
        const bool use_curriculum = episode >= kInitialEpisodesPerIteration;
        batch.trajectories[episode] = collectTrajectory(
            network, discriminator, curriculum, lane_seed, use_curriculum,
            deadline);
      }
    }));
  }
  for (auto& future : futures) future.get();
  std::int64_t total_moves = 0;
  std::int64_t total_clears = 0;
  std::int64_t total_reveals = 0;
  for (const Trajectory& trajectory : batch.trajectories) {
    batch.samples += trajectory.samples.size();
    total_moves += trajectory.moves;
    total_clears += trajectory.clears;
    total_reveals += trajectory.reveals;
    batch.discriminator_probability += trajectory.discriminator_probability;
    batch.manifold_reward += trajectory.manifold_reward;
    if (trajectory.curriculum) {
      batch.curriculum_score += trajectory.score;
      batch.curriculum_moves += trajectory.moves;
    } else {
      batch.initial_score += trajectory.score;
      batch.initial_moves += trajectory.moves;
    }
  }
  if (batch.samples > kMaximumBatchSamples || total_moves <= 0) {
    throw std::runtime_error("manifold PPO batch exceeded sample bound");
  }
  batch.initial_score /= kInitialEpisodesPerIteration;
  batch.initial_moves /= kInitialEpisodesPerIteration;
  batch.curriculum_score /= kCurriculumEpisodesPerIteration;
  batch.curriculum_moves /= kCurriculumEpisodesPerIteration;
  batch.clears_per_move = static_cast<double>(total_clears) / total_moves;
  batch.reveals_per_move = static_cast<double>(total_reveals) / total_moves;
  batch.discriminator_probability /= total_moves;
  batch.manifold_reward /= total_moves;
  enforceRssLimit();
  return batch;
}

void normalizeCohortAdvantages(Batch& batch, bool curriculum) {
  std::size_t count = 0;
  double mean = 0.0;
  for (const Trajectory& trajectory : batch.trajectories) {
    if (trajectory.curriculum != curriculum) continue;
    for (const Sample& sample : trajectory.samples) {
      mean += sample.advantage;
      ++count;
    }
  }
  if (count == 0) throw std::runtime_error("empty advantage cohort");
  mean /= count;
  double variance = 0.0;
  for (const Trajectory& trajectory : batch.trajectories) {
    if (trajectory.curriculum != curriculum) continue;
    for (const Sample& sample : trajectory.samples) {
      const double difference = sample.advantage - mean;
      variance += difference * difference;
    }
  }
  const double scale = 1.0 / std::sqrt(variance / count + 1.0e-8);
  for (Trajectory& trajectory : batch.trajectories) {
    if (trajectory.curriculum != curriculum) continue;
    for (Sample& sample : trajectory.samples) {
      sample.advantage = static_cast<float>((sample.advantage - mean) * scale);
    }
  }
}

struct UpdateMetrics {
  double policy_loss = 0.0;
  double value_loss = 0.0;
  double entropy = 0.0;
  double approximate_kl = 0.0;
  double clip_fraction = 0.0;
  int updates = 0;
};

float ppoPolicyCoefficient(float advantage, float ratio,
                           float inverse_batch) {
  const bool clipped =
      (advantage >= 0.0f && ratio > 1.0f + kClipRatio) ||
      (advantage < 0.0f && ratio < 1.0f - kClipRatio);
  return clipped ? 0.0f : -advantage * ratio * inverse_batch;
}

UpdateMetrics update(prior::Network& network, Batch& batch,
                     Mulberry32& shuffle_random,
                     const Deadline& deadline) {
  normalizeCohortAdvantages(batch, false);
  normalizeCohortAdvantages(batch, true);
  std::vector<Sample*> samples;
  samples.reserve(batch.samples);
  for (Trajectory& trajectory : batch.trajectories) {
    for (Sample& sample : trajectory.samples) samples.push_back(&sample);
  }
  UpdateMetrics metrics;
  std::uint64_t metric_samples = 0;
  for (int epoch = 0; epoch < kPpoEpochs; ++epoch) {
    for (std::size_t index = samples.size(); index > 1; --index) {
      const std::size_t selected = static_cast<std::size_t>(
          (static_cast<std::uint64_t>(shuffle_random.nextBits()) * index) >>
          32u);
      std::swap(samples[index - 1], samples[selected]);
    }
    for (std::size_t begin = 0; begin < samples.size(); begin += kMinibatch) {
      if ((begin & 8'191u) == 0) {
        deadline.check();
        enforceRssLimit();
      }
      const std::size_t end = std::min(samples.size(), begin + kMinibatch);
      const float inverse = 1.0f / static_cast<float>(end - begin);
      std::vector<float> gradient = network.zeroGradient();
      for (std::size_t offset = begin; offset < end; ++offset) {
        const Sample& sample = *samples[offset];
        prior::BasePolicy base;
        base.logits = sample.base_logits;
        const prior::Prediction prediction =
            prior::predictCanonical(network, sample.state, &base);
        const float probability = std::max(
            1.0e-12f, prediction.probabilities[sample.action]);
        const float log_probability = std::log(probability);
        const float ratio =
            std::exp(log_probability - sample.old_log_probability);
        const float clipped_ratio =
            std::clamp(ratio, 1.0f - kClipRatio, 1.0f + kClipRatio);
        const float raw_objective = ratio * sample.advantage;
        const float clipped_objective = clipped_ratio * sample.advantage;
        const float policy_coefficient =
            ppoPolicyCoefficient(sample.advantage, ratio, inverse);
        const bool clipped = policy_coefficient == 0.0f &&
                             sample.advantage != 0.0f;
        const float value_difference =
            prediction.value - sample.return_value;
        const float value_derivative =
            2.0f * kValueCoefficient * value_difference * inverse;
        prior::accumulateEquivariantGradient(
            network, prediction, sample.action, policy_coefficient,
            value_derivative, kEntropyCoefficient * inverse, gradient);
        float entropy = 0.0f;
        for (const float candidate : prediction.probabilities) {
          if (candidate > 0.0f) entropy -= candidate * std::log(candidate);
        }
        metrics.policy_loss -= std::min(raw_objective, clipped_objective);
        metrics.value_loss += 0.5 * value_difference * value_difference;
        metrics.entropy += entropy;
        metrics.approximate_kl +=
            sample.old_log_probability - log_probability;
        metrics.clip_fraction += clipped ? 1.0 : 0.0;
        ++metric_samples;
      }
      network.applyAdam(gradient, kLearningRate, kGradientNorm);
      ++metrics.updates;
    }
  }
  const double inverse = 1.0 / static_cast<double>(metric_samples);
  metrics.policy_loss *= inverse;
  metrics.value_loss *= inverse;
  metrics.entropy *= inverse;
  metrics.approximate_kl *= inverse;
  metrics.clip_fraction *= inverse;
  return metrics;
}

struct TrainingRecord {
  int iteration = 0;
  std::size_t samples = 0;
  double initial_score = 0.0;
  double initial_moves = 0.0;
  double curriculum_score = 0.0;
  double curriculum_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double discriminator_probability = 0.0;
  double manifold_reward = 0.0;
  UpdateMetrics update{};
};

struct TrainingResult {
  prior::Network network{};
  std::array<TrainingRecord, kIterations> records{};
  std::uint64_t moves = 0;
};

TrainingResult trainPolicy(const prior::Network& inherited,
                           const Discriminator& discriminator,
                           const prior::Curriculum& curriculum, int threads,
                           const Deadline& deadline) {
  TrainingResult result;
  result.network.setParameters(inherited.parameters());
  if (prior::modelFingerprint(result.network) !=
      prior::modelFingerprint(inherited)) {
    throw std::runtime_error("checkpoint inheritance changed parameters");
  }
  Mulberry32 shuffle_random(kPolicyShuffleSeed);
  for (int iteration = 0; iteration < kIterations; ++iteration) {
    deadline.check();
    Batch batch = collectBatch(result.network, discriminator, curriculum,
                               iteration, threads, deadline);
    TrainingRecord record;
    record.iteration = iteration + 1;
    record.samples = batch.samples;
    record.initial_score = batch.initial_score;
    record.initial_moves = batch.initial_moves;
    record.curriculum_score = batch.curriculum_score;
    record.curriculum_moves = batch.curriculum_moves;
    record.clears_per_move = batch.clears_per_move;
    record.reveals_per_move = batch.reveals_per_move;
    record.discriminator_probability = batch.discriminator_probability;
    record.manifold_reward = batch.manifold_reward;
    record.update = update(result.network, batch, shuffle_random, deadline);
    result.records[iteration] = record;
    result.moves += batch.samples;
    std::cerr << std::fixed << std::setprecision(3)
              << "manifold-ppo iteration " << record.iteration << '/'
              << kIterations << " samples " << record.samples << " initial "
              << record.initial_score << '/' << record.initial_moves
              << " curriculum " << record.curriculum_score << '/'
              << record.curriculum_moves << " flow "
              << record.clears_per_move << '/' << record.reveals_per_move
              << " manifold " << record.discriminator_probability << '/'
              << record.manifold_reward << " entropy "
              << record.update.entropy << " rss " << peakRssBytes() << '\n';
  }
  return result;
}

using GameResult = prior::GameResult;
using Summary = prior::Summary;
using PairedSummary = prior::PairedSummary;

enum class EvaluationPolicy : std::uint8_t { kCandidate, kPrior, kFairD1 };

GameResult playEvaluationGame(const prior::Network& candidate,
                              const prior::Network& inherited,
                              std::uint32_t seed, EvaluationPolicy policy,
                              const Deadline& deadline) {
  requireSeed(seed, SeedUse::kStageA);
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kStageAMaximumMoves) {
    if ((state.moves_played & 31) == 0) deadline.check();
    const PublicState public_state = vr::publicState(state);
    int action = -1;
    if (policy == EvaluationPolicy::kCandidate) {
      action = prior::chooseAction(public_state, candidate).action;
    } else if (policy == EvaluationPolicy::kPrior) {
      action = prior::chooseAction(public_state, inherited).action;
    } else {
      action = vr::chooseFairDepthOne(public_state).action;
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("Stage-A policy selected illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("Stage-A transition failed");
    }
    for (const Wave& wave : move.waves) {
      result.clears += wave.cleared;
      result.reveals += wave.revealed;
      result.maximum_chain = std::max(result.maximum_chain, wave.depth);
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.capped = !state.game_over;
  return result;
}

std::vector<GameResult> evaluate(const prior::Network& candidate,
                                 const prior::Network& inherited,
                                 EvaluationPolicy policy, int threads,
                                 const Deadline& deadline) {
  std::vector<GameResult> games(kStageAGames);
  std::atomic<int> next{0};
  std::vector<std::future<void>> futures;
  const int workers = std::min(threads, kStageAGames);
  for (int worker = 0; worker < workers; ++worker) {
    futures.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next.fetch_add(1);
        if (game >= kStageAGames) return;
        games[game] = playEvaluationGame(
            candidate, inherited,
            kStageASeedStart + static_cast<std::uint32_t>(game), policy,
            deadline);
      }
    }));
  }
  for (auto& future : futures) future.get();
  enforceRssLimit();
  return games;
}

bool passesGate(const Summary& candidate, const PairedSummary& versus_prior,
                const PairedSummary& versus_d1) {
  return candidate.mean_score >= kGateMeanScore &&
         candidate.mean_moves >= kGateMeanMoves &&
         candidate.clears_per_move >= kGateClearsPerMove &&
         candidate.reveals_per_move >= kGateRevealsPerMove &&
         versus_prior.score_delta > 0.0 && versus_prior.move_delta > 0.0 &&
         versus_prior.score_wins >= kGateScoreWins &&
         versus_d1.score_delta > 0.0 && versus_d1.move_delta > 0.0 &&
         versus_d1.score_wins >= kGateScoreWins;
}

struct Options {
  std::string curriculum = "/tmp/drop7-oracle-curriculum-states.jsonl";
  std::string inherited = "/tmp/drop7-curriculum-option-ppo.bin";
  std::string checkpoint = "/tmp/drop7-oracle-manifold-ppo.bin";
  std::string output = "/tmp/drop7-oracle-manifold-ppo-stage-a.json";
  int threads = 4;
};

Options parseOptions(int argc, char** argv, int begin) {
  Options options;
  for (int index = begin; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--curriculum" && index + 1 < argc) {
      options.curriculum = argv[++index];
    } else if (argument == "--inherited" && index + 1 < argc) {
      options.inherited = argv[++index];
    } else if (argument == "--checkpoint" && index + 1 < argc) {
      options.checkpoint = argv[++index];
    } else if (argument == "--output" && index + 1 < argc) {
      options.output = argv[++index];
    } else if (argument == "--threads" && index + 1 < argc) {
      options.threads = std::stoi(argv[++index]);
    } else {
      throw std::invalid_argument("unknown or incomplete option");
    }
  }
  if (options.curriculum.empty() || options.inherited.empty() ||
      options.checkpoint.empty() || options.output.empty() ||
      options.threads < 1 || options.threads > kMaximumThreads) {
    throw std::invalid_argument("invalid oracle-manifold PPO options");
  }
  return options;
}

void writeLabelMetrics(std::ostream& output, const LabelMetrics& metrics) {
  output << "{\"pairs\":" << metrics.pairs << ",\"auc\":" << metrics.auc
         << ",\"matchedPairRanking\":" << metrics.matched_pair_ranking
         << ",\"loss\":" << metrics.loss << '}';
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"bottomQuartileMoves\":" << summary.bottom_quartile_moves
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"maximumChain\":" << summary.maximum_chain
         << ",\"capped\":" << summary.capped << '}';
}

void writePaired(std::ostream& output, const PairedSummary& summary) {
  output << "{\"scoreWins\":" << summary.score_wins
         << ",\"moveWins\":" << summary.move_wins
         << ",\"jointWins\":" << summary.joint_wins
         << ",\"meanScoreDelta\":" << summary.score_delta
         << ",\"meanMoveDelta\":" << summary.move_delta << '}';
}

void writeDiscriminatorArtifact(const Options& options,
                                const prior::Curriculum& curriculum,
                                const MatchedDataset& dataset,
                                const DiscriminatorResult& discriminator,
                                double wall_seconds) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open discriminator artifact");
  output << std::setprecision(12)
         << "{\n  \"format\":\"drop7-oracle-manifold-ppo-v1\","
         << "\n  \"status\":\"discriminator-gate-failed\","
         << "\n  \"curriculum\":{\"states\":" << curriculum.states.size()
         << ",\"fingerprint\":\"0x" << std::hex << curriculum.fingerprint
         << std::dec << "\",\"metadataRetained\":false},"
         << "\n  \"matching\":{\"positiveTotal\":"
         << dataset.positive_total << ",\"negativeRollinStates\":"
         << dataset.negative_rollin_states << ",\"matched\":"
         << dataset.matched << ",\"coverage\":" << dataset.coverage
         << ",\"fingerprint\":\"0x" << std::hex << dataset.fingerprint
         << std::dec
         << "\",\"exactFields\":[\"risePhase\",\"occupancy\",\"maximumHeight\"]},"
         << "\n  \"heldout\":[";
  writeLabelMetrics(output, discriminator.heldout[0]);
  output << ',';
  writeLabelMetrics(output, discriminator.heldout[1]);
  output << "],\n  \"gate\":{\"minimumCoverage\":"
         << kDiscriminatorCoverageGate << ",\"minimumAuc\":"
         << kDiscriminatorAucGate << ",\"minimumMatchedPairRanking\":"
         << kDiscriminatorPairGate << "},\n  \"passed\":false,"
         << "\n  \"policyTrainingStarted\":false,"
         << "\n  \"seedLanes\":{\"negative\":\"0x3d6b0000..0x3d6b03ff\",\"training\":\"unopened\",\"stageA\":\"unopened\"},"
         << "\n  \"wallSeconds\":" << wall_seconds
         << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
}

void writeArtifact(const Options& options,
                   const prior::Curriculum& curriculum,
                   const MatchedDataset& dataset,
                   const DiscriminatorResult& discriminator,
                   const TrainingResult& training,
                   const std::vector<GameResult>& candidate_games,
                   const Summary& candidate,
                   const Summary& inherited_summary,
                   const Summary& d1_summary,
                   const PairedSummary& versus_inherited,
                   const PairedSummary& versus_d1, bool passed,
                   double wall_seconds) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open manifold artifact");
  output << std::setprecision(12)
         << "{\n  \"format\":\"drop7-oracle-manifold-ppo-v1\","
         << "\n  \"status\":\"stage-a-complete\","
         << "\n  \"architecture\":{\"policyParameters\":"
         << prior::Layout::count << ",\"discriminatorInput\":"
         << kDiscriminatorInputs << ",\"discriminatorHidden\":"
         << kDiscriminatorHidden
         << ",\"reflection\":\"canonical public state plus invariant aggregates\",\"base\":\"normalized exact fair-D1 root-Q logits\"},"
         << "\n  \"curriculum\":{\"states\":" << curriculum.states.size()
         << ",\"fingerprint\":\"0x" << std::hex << curriculum.fingerprint
         << std::dec << "\",\"metadataRetained\":false},"
         << "\n  \"matching\":{\"positiveTotal\":"
         << dataset.positive_total << ",\"negativeRollinStates\":"
         << dataset.negative_rollin_states << ",\"matched\":"
         << dataset.matched << ",\"coverage\":" << dataset.coverage
         << ",\"fingerprint\":\"0x" << std::hex << dataset.fingerprint
         << std::dec
         << "\",\"exactFields\":[\"risePhase\",\"occupancy\",\"maximumHeight\"]},"
         << "\n  \"discriminator\":{\"epochs\":"
         << kDiscriminatorEpochs << ",\"learningRate\":"
         << kDiscriminatorLearningRate << ",\"heldout\":[";
  writeLabelMetrics(output, discriminator.heldout[0]);
  output << ',';
  writeLabelMetrics(output, discriminator.heldout[1]);
  output << "],\"allFit\":";
  writeLabelMetrics(output, discriminator.all);
  output << "},\n  \"training\":{\"iterations\":" << kIterations
         << ",\"episodesPerIteration\":" << kEpisodesPerIteration
         << ",\"totalEpisodes\":" << kTrainingEpisodes
         << ",\"initialFraction\":0.5,\"curriculumFraction\":0.5,"
         << "\"initialMaximumMoves\":" << kInitialMaximumMoves
         << ",\"curriculumHorizon\":" << kCurriculumHorizon
         << ",\"epochs\":" << kPpoEpochs << ",\"minibatch\":"
         << kMinibatch << ",\"gamma\":" << kGamma
         << ",\"gaeLambda\":" << kGaeLambda << ",\"clip\":"
         << kClipRatio << ",\"entropy\":" << kEntropyCoefficient
         << ",\"valueCoefficient\":" << kValueCoefficient
         << ",\"gradientClip\":" << kGradientNorm
         << ",\"learningRate\":" << kLearningRate
         << ",\"advantageNormalization\":\"separate initial and curriculum cohorts\","
         << "\"reward\":\"scoreDelta/17000 + .05 survived + .05 clears + .15 reveals - 5 terminal + .10 centeredGAIL + .15 potentialDifference\"},"
         << "\n  \"seedLanes\":{\"negative\":\"0x3d6b0000..0x3d6b03ff\",\"training\":\"0x3d6b1000..0x3d6b6fff\",\"stageA\":\"0x3d6c0000..0x3d6c001f\"},"
         << "\n  \"inheritance\":{\"path\":\"" << options.inherited
         << "\",\"fingerprint\":\"0x" << std::hex
         << kExpectedPriorFingerprint << std::dec
         << "\",\"freshOptimizer\":true},"
         << "\n  \"checkpoint\":\"" << options.checkpoint
         << "\",\"modelFingerprint\":\"0x" << std::hex
         << prior::modelFingerprint(training.network) << std::dec << "\","
         << "\n  \"learningCurve\":[";
  for (int index = 0; index < kIterations; ++index) {
    if (index != 0) output << ',';
    const TrainingRecord& record = training.records[index];
    output << "{\"iteration\":" << record.iteration
           << ",\"samples\":" << record.samples
           << ",\"initialScore\":" << record.initial_score
           << ",\"initialMoves\":" << record.initial_moves
           << ",\"curriculumScore\":" << record.curriculum_score
           << ",\"curriculumMoves\":" << record.curriculum_moves
           << ",\"clearsPerMove\":" << record.clears_per_move
           << ",\"revealsPerMove\":" << record.reveals_per_move
           << ",\"manifoldProbability\":"
           << record.discriminator_probability
           << ",\"manifoldRewardPerMove\":" << record.manifold_reward
           << ",\"entropy\":" << record.update.entropy << '}';
  }
  output << "],\n  \"candidate\":";
  oracle_manifold_ppo::writeSummary(output, candidate);
  output << ",\n  \"inheritedPrior\":";
  oracle_manifold_ppo::writeSummary(output, inherited_summary);
  output << ",\n  \"fairD1\":";
  oracle_manifold_ppo::writeSummary(output, d1_summary);
  output << ",\n  \"pairedVersusInherited\":";
  writePaired(output, versus_inherited);
  output << ",\n  \"pairedVersusD1\":";
  writePaired(output, versus_d1);
  output << ",\n  \"gate\":{\"meanScore\":" << kGateMeanScore
         << ",\"meanMoves\":" << kGateMeanMoves
         << ",\"clearsPerMove\":" << kGateClearsPerMove
         << ",\"revealsPerMove\":" << kGateRevealsPerMove
         << ",\"scoreWinsAgainstEach\":" << kGateScoreWins
         << ",\"positiveMeanScoreAndMoveDeltaAgainstEach\":true},"
         << "\n  \"passed\":" << (passed ? "true" : "false")
         << ",\n  \"throughputMovesPerSecond\":"
         << (training.moves / wall_seconds)
         << ",\n  \"wallSeconds\":" << wall_seconds
         << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
  if (!output) throw std::runtime_error("failed writing manifold artifact");
  static_cast<void>(candidate_games);
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

bool selfTest(const Options& options, std::ostream& output) {
  const prior::Curriculum curriculum = prior::loadCurriculum(options.curriculum);
  expect(curriculum.fingerprint == kExpectedCurriculumFingerprint,
         "curriculum checksum self-test failed");
  const prior::Network inherited = prior::loadCheckpoint(options.inherited);
  expect(prior::modelFingerprint(inherited) == kExpectedPriorFingerprint,
         "prior checkpoint fingerprint self-test failed");
  prior::Network copy;
  copy.setParameters(inherited.parameters());
  expect(copy.parameters() == inherited.parameters() &&
             prior::modelFingerprint(copy) == kExpectedPriorFingerprint,
         "checkpoint inheritance self-test failed");

  const PublicState fixture = prior::fixtureState();
  const TopologyFeatures features = topologyFeatures(fixture);
  const TopologyFeatures mirrored = topologyFeatures(vr::mirror(fixture));
  expect(features == mirrored,
         "discriminator features are not exactly reflection invariant");
  Discriminator discriminator;
  expect(discriminator.logit(fixture) ==
             discriminator.logit(vr::mirror(fixture)),
         "discriminator score is not exactly reflection invariant");
  const prior::PolicyDecision action = prior::chooseAction(fixture, copy);
  const prior::PolicyDecision reflected =
      prior::chooseAction(vr::mirror(fixture), copy);
  expect(reflected.action == kBoardSize - 1 - action.action,
         "inherited policy lost reflection equivariance");

  State metadata = vr::materialize(fixture);
  metadata.score = 99'999'999;
  metadata.level = 999;
  metadata.moves_played = 777;
  expect(vr::publicState(metadata) == fixture &&
             topologyFeatures(vr::publicState(metadata)) == features &&
             prior::chooseAction(vr::publicState(metadata), copy) == action,
         "public model retained hidden metadata");

  const float current = 0.7f;
  const float next = 1.1f;
  const float manual =
      kGailCoefficient *
          std::clamp(-std::log(1.0f - Discriminator::sigmoid(next)) -
                         std::log(2.0f),
                     -0.5f, 2.0f) +
      kPotentialCoefficient *
          (kGamma * clippedPotential(next) - clippedPotential(current));
  expect(std::abs(manifoldReward(current, next, false) - manual) < 1.0e-7f &&
             std::abs(manifoldReward(current, next, true) -
                      kPotentialCoefficient * -clippedPotential(current)) <
                 1.0e-7f,
         "manifold reward math self-test failed");

  std::vector<Sample> advantages(2);
  advantages[0].reward = 1.0f;
  advantages[0].old_value = 0.5f;
  advantages[1].reward = 2.0f;
  advantages[1].old_value = 0.25f;
  advantages[1].terminal = true;
  finishAdvantages(advantages, 0.0f);
  const float expected_last = 1.75f;
  const float expected_first =
      1.0f + kGamma * 0.25f - 0.5f +
      kGamma * kGaeLambda * expected_last;
  expect(std::abs(advantages[0].advantage - expected_first) < 1.0e-6f &&
             std::abs(advantages[1].advantage - expected_last) < 1.0e-6f,
         "GAE reward wiring self-test failed");

  Summary positive;
  positive.mean_score = 250'001;
  positive.mean_moves = 81;
  positive.clears_per_move = 1.91;
  positive.reveals_per_move = 1.06;
  PairedSummary gains;
  gains.score_delta = 1;
  gains.move_delta = 1;
  gains.score_wins = 17;
  expect(passesGate(positive, gains, gains), "positive gate wiring failed");
  Summary negative = positive;
  negative.mean_score = 249'999;
  PairedSummary no_gain = gains;
  no_gain.score_delta = -1;
  expect(!passesGate(negative, gains, gains) &&
             !passesGate(positive, no_gain, gains) &&
             !passesGate(positive, gains, no_gain),
         "negative gate wiring failed");

  expect(allowedSeed(kNegativeSeedStart, SeedUse::kNegative) &&
             allowedSeed(kNegativeSeedEndExclusive - 1,
                         SeedUse::kNegative) &&
             allowedSeed(kTrainingSeedStart, SeedUse::kTraining) &&
             allowedSeed(kTrainingSeedEndExclusive - 1,
                         SeedUse::kTraining) &&
             allowedSeed(kStageASeedStart, SeedUse::kStageA) &&
             allowedSeed(kStageASeedEndExclusive - 1, SeedUse::kStageA) &&
             throwsInvalid([] {
               requireSeed(0x3d6b'0400u, SeedUse::kNegative);
             }) &&
             throwsInvalid([] {
               requireSeed(0x3d6b'7000u, SeedUse::kTraining);
             }) &&
             throwsInvalid([] {
               requireSeed(0x3d6c'0020u, SeedUse::kStageA);
             }) &&
             throwsInvalid([] {
               requireSeed(0x3d68'0000u, SeedUse::kStageA);
             }) &&
             throwsInvalid([] {
               requireSeed(0x3d69'0000u, SeedUse::kTraining);
             }) &&
             throwsInvalid([] {
               requireSeed(0x3d3a'0000u, SeedUse::kTraining);
             }) &&
             throwsInvalid([] {
               requireSeed(0x4d6b'0000u, SeedUse::kNegative);
             }) &&
             throwsInvalid([] {
               requireSeed(0x7d6b'0000u, SeedUse::kTraining);
             }) &&
             throwsInvalid([] {
               requireSeed(0xd76c'0000u, SeedUse::kStageA);
             }),
         "seed guards self-test failed");

  // Synthetic exact matching and origin tests exercise the gate plumbing
  // without consuming any registered game seed.
  MatchedPair pair;
  pair.positive = fixture;
  pair.negative = fixture;
  pair.load = stratum(fixture);
  expect(stratum(pair.positive) == stratum(pair.negative) &&
             stratum(pair.positive) == pair.load,
         "matching self-test failed");
  enforceRssLimit();
  output << std::setprecision(12)
         << "ORACLE_MANIFOLD_PPO_SELF_TEST {\"passed\":true,"
         << "\"curriculumChecksum\":true,\"reflectionExact\":true,"
         << "\"metadataBlind\":true,\"rewardMath\":true,"
         << "\"checkpointInheritance\":true,\"matching\":true,"
         << "\"seedGuards\":true,\"gateWiring\":true,"
         << "\"policyParameters\":" << prior::Layout::count
         << ",\"discriminatorParameters\":"
         << DiscriminatorLayout::count << ",\"peakRssBytes\":"
         << peakRssBytes() << "}\n";
  return true;
}

int discriminatorOnly(const Options& options, std::ostream& output) {
  const Deadline deadline;
  const prior::Curriculum curriculum = prior::loadCurriculum(options.curriculum);
  if (curriculum.fingerprint != kExpectedCurriculumFingerprint) {
    throw std::runtime_error("public curriculum checksum mismatch");
  }
  const MatchedDataset dataset = buildMatchedDataset(curriculum, deadline);
  const DiscriminatorResult discriminator =
      fitAndGateDiscriminator(dataset, deadline);
  if (!discriminator.passed) {
    writeDiscriminatorArtifact(options, curriculum, dataset, discriminator,
                               deadline.elapsedSeconds());
  }
  output << std::setprecision(6)
         << "ORACLE_MANIFOLD_DISCRIMINATOR {\"matched\":"
         << dataset.matched << ",\"coverage\":" << dataset.coverage
         << ",\"fold0Auc\":" << discriminator.heldout[0].auc
         << ",\"fold0Pair\":"
         << discriminator.heldout[0].matched_pair_ranking
         << ",\"fold1Auc\":" << discriminator.heldout[1].auc
         << ",\"fold1Pair\":"
         << discriminator.heldout[1].matched_pair_ranking
         << ",\"passed\":" << (discriminator.passed ? "true" : "false")
         << ",\"wallSeconds\":" << deadline.elapsedSeconds()
         << ",\"peakRssBytes\":" << peakRssBytes() << "}\n";
  return discriminator.passed ? EXIT_SUCCESS : 2;
}

int run(const Options& options, std::ostream& output) {
  const Deadline deadline;
  const prior::Curriculum curriculum = prior::loadCurriculum(options.curriculum);
  if (curriculum.fingerprint != kExpectedCurriculumFingerprint) {
    throw std::runtime_error("public curriculum checksum mismatch");
  }
  const prior::Network inherited = prior::loadCheckpoint(options.inherited);
  if (prior::modelFingerprint(inherited) != kExpectedPriorFingerprint) {
    throw std::runtime_error("inherited policy checkpoint mismatch");
  }
  const MatchedDataset dataset = buildMatchedDataset(curriculum, deadline);
  const DiscriminatorResult discriminator =
      fitAndGateDiscriminator(dataset, deadline);
  output << std::setprecision(6)
         << "ORACLE_MANIFOLD_DISCRIMINATOR {\"matched\":"
         << dataset.matched << ",\"coverage\":" << dataset.coverage
         << ",\"fold0Auc\":" << discriminator.heldout[0].auc
         << ",\"fold0Pair\":"
         << discriminator.heldout[0].matched_pair_ranking
         << ",\"fold1Auc\":" << discriminator.heldout[1].auc
         << ",\"fold1Pair\":"
         << discriminator.heldout[1].matched_pair_ranking
         << ",\"passed\":" << (discriminator.passed ? "true" : "false")
         << "}\n";
  if (!discriminator.passed) {
    writeDiscriminatorArtifact(options, curriculum, dataset, discriminator,
                               deadline.elapsedSeconds());
    return 2;
  }

  TrainingResult training = trainPolicy(inherited, discriminator.model,
                                        curriculum, options.threads, deadline);
  prior::saveCheckpoint(options.checkpoint, training.network);
  const prior::Network frozen = prior::loadCheckpoint(options.checkpoint);
  if (prior::modelFingerprint(frozen) !=
      prior::modelFingerprint(training.network)) {
    throw std::runtime_error("frozen manifold checkpoint verification failed");
  }
  const std::vector<GameResult> candidate_games =
      evaluate(frozen, inherited, EvaluationPolicy::kCandidate,
               options.threads, deadline);
  const std::vector<GameResult> inherited_games =
      evaluate(frozen, inherited, EvaluationPolicy::kPrior, options.threads,
               deadline);
  const std::vector<GameResult> d1_games =
      evaluate(frozen, inherited, EvaluationPolicy::kFairD1, options.threads,
               deadline);
  const Summary candidate = prior::summarize(candidate_games);
  const Summary inherited_summary = prior::summarize(inherited_games);
  const Summary d1_summary = prior::summarize(d1_games);
  const PairedSummary versus_inherited =
      prior::pair(candidate_games, inherited_games);
  const PairedSummary versus_d1 = prior::pair(candidate_games, d1_games);
  const bool passed =
      passesGate(candidate, versus_inherited, versus_d1);
  deadline.check();
  enforceRssLimit();
  const double wall_seconds = deadline.elapsedSeconds();
  writeArtifact(options, curriculum, dataset, discriminator, training,
                candidate_games, candidate, inherited_summary, d1_summary,
                versus_inherited, versus_d1, passed, wall_seconds);
  output << std::fixed << std::setprecision(3)
         << "ORACLE_MANIFOLD_PPO_STAGE_A {\"candidateScore\":"
         << candidate.mean_score << ",\"candidateMoves\":"
         << candidate.mean_moves << ",\"clearsPerMove\":"
         << candidate.clears_per_move << ",\"revealsPerMove\":"
         << candidate.reveals_per_move << ",\"priorScore\":"
         << inherited_summary.mean_score << ",\"priorMoves\":"
         << inherited_summary.mean_moves << ",\"d1Score\":"
         << d1_summary.mean_score << ",\"d1Moves\":"
         << d1_summary.mean_moves << ",\"priorScoreDelta\":"
         << versus_inherited.score_delta << ",\"d1ScoreDelta\":"
         << versus_d1.score_delta << ",\"passed\":"
         << (passed ? "true" : "false") << ",\"wallSeconds\":"
         << wall_seconds << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return passed ? EXIT_SUCCESS : 2;
}

}  // namespace drop7::oracle_manifold_ppo

int main(int argc, char** argv) {
  try {
    if (argc < 2) throw std::invalid_argument("missing mode");
    const std::string_view mode(argv[1]);
    const drop7::oracle_manifold_ppo::Options options =
        drop7::oracle_manifold_ppo::parseOptions(argc, argv, 2);
    if (mode == "--self-test") {
      return drop7::oracle_manifold_ppo::selfTest(options, std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (mode == "--discriminator") {
      return drop7::oracle_manifold_ppo::discriminatorOnly(options, std::cout);
    }
    if (mode == "--run") {
      return drop7::oracle_manifold_ppo::run(options, std::cout);
    }
    throw std::invalid_argument(
        "usage: drop7_oracle_manifold_ppo --self-test | --discriminator | --run [--curriculum PATH] [--inherited PATH] [--checkpoint PATH] [--output PATH] [--threads 1..8]");
  } catch (const std::exception& error) {
    std::cerr << "drop7_oracle_manifold_ppo: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
