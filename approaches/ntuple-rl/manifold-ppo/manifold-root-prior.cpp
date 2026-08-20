#define DROP7_CURRICULUM_OPTION_PPO_LIBRARY
#include "../curriculum-option-ppo/curriculum-option-ppo.cpp"
#undef DROP7_CURRICULUM_OPTION_PPO_LIBRARY

#include <map>
#include <optional>

// Fits a standalone scalar to the fixed, exact-load-matched public
// oracle-manifold dataset.  The scalar can break only close exact-fair-D3 root
// decisions and is not used as a search-leaf value.
namespace drop7::manifold_root_prior {

namespace prior = drop7::curriculum_option_ppo;
namespace vr = drop7::viability_reservoir_controller;
namespace fair = drop7::fair_only_horizon;
namespace detail = drop7::cfpi::detail;

using Clock = std::chrono::steady_clock;
using PublicState = prior::PublicState;

// Reconstruct the checksum-stable matching set and exact public feature
// transform locally so this executable has no external entry-point or artifact
// dependency.  The negative games are development-only replay data; replay
// does not inspect a new seed.
namespace manifold {

constexpr std::uint32_t kNegativeSeedStart = 0x3d6b'0000u;
constexpr std::uint32_t kNegativeSeedEndExclusive = 0x3d6b'0400u;
constexpr int kNegativeMaximumMoves = 1'000;
constexpr std::uint32_t kPositiveFoldDomain = 0x504f'5346u;
constexpr std::uint32_t kNegativeFoldDomain = 0x4e45'4746u;

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
  std::size_t matched = 0;
  std::uint64_t fingerprint = 0xcbf2'9ce4'8422'2325ull;
};

void fingerprintWord(std::uint64_t& hash, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    prior::fingerprintByte(hash, static_cast<std::uint8_t>(value >> shift));
  }
}

template <typename DeadlineType>
MatchedDataset buildMatchedDataset(const prior::Curriculum& curriculum,
                                   const DeadlineType& deadline) {
  using Bucket = std::array<std::vector<NegativeState>, 2>;
  std::map<Stratum, Bucket> negatives;
  for (std::uint32_t seed = kNegativeSeedStart;
       seed < kNegativeSeedEndExclusive; ++seed) {
    if (((seed - kNegativeSeedStart) & 31u) == 0) deadline.check();
    State state = initialHeadlessState(seed);
    const int fold =
        static_cast<int>(mix32(seed ^ kNegativeFoldDomain) & 1u);
    while (!state.game_over && state.moves_played < kNegativeMaximumMoves) {
      const PublicState public_state = vr::publicState(state);
      const vr::BaselineDecision decision =
          vr::chooseFairDepthOne(public_state);
      if (!decision.complete || !isLegal(state.board, decision.action)) {
        throw std::runtime_error("fair-D1 matching replay was incomplete");
      }
      negatives[stratum(public_state)][fold].push_back(
          {public_state, seed, publicHash(public_state)});
      MoveResult move;
      if (!playHeadlessMove(state, seed, decision.action, move)) {
        throw std::runtime_error("matching replay transition failed");
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
    const int fold =
        static_cast<int>(mix64(hash ^ kPositiveFoldDomain) & 1u);
    positives[fold].push_back({hash, state});
  }
  for (auto& values : positives) {
    std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) {
      return a.first < b.first;
    });
  }
  MatchedDataset result;
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
      if (pair.load != stratum(pair.positive) ||
          pair.load != stratum(pair.negative) || pair.fold != fold ||
          static_cast<int>(mix32(pair.negative_origin ^
                                 kNegativeFoldDomain) &
                           1u) != fold ||
          static_cast<int>(mix64(pair.positive_hash ^
                                 kPositiveFoldDomain) &
                           1u) != fold) {
        throw std::runtime_error("matched dataset integrity failure");
      }
      fingerprintWord(result.fingerprint, pair.positive_hash);
      fingerprintWord(result.fingerprint, pair.negative_hash);
    }
    result.matched += result.folds[fold].size();
  }
  return result;
}

constexpr int kDiscriminatorHidden = 24;
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
        const float value = observation.input[
            prior::kKeysOffset +
            (disc * kBoardSize + column) * prior::kKeyInputs + feature];
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
    throw std::runtime_error("root-prior feature layout mismatch");
  }
  return result;
}

}  // namespace manifold

constexpr std::uint32_t kFitSeedStart = 0x3d6f'0000u;
constexpr std::uint32_t kFitSeedEndExclusive = 0x3d6f'0010u;
constexpr int kFitGames = 16;
constexpr std::uint32_t kScreenSeedStart = 0x3d6f'1000u;
constexpr std::uint32_t kScreenSeedEndExclusive = 0x3d6f'1020u;
constexpr int kScreenGames = 32;
constexpr int kMaximumMoves = 1'000;
constexpr int kMaximumThreads = 4;
constexpr int kSearchDepth = 3;
constexpr double kRootQWindow = 2'500.0;
constexpr int kMaximumAdmittedActions = 2;
constexpr int kSuccessorScenarios = 7;
constexpr std::uint32_t kSuccessorPolicySeed = 0x4d52'5052u;

constexpr double kFitScoreRatio = 1.15;
constexpr double kFitMoveRatio = 1.15;
constexpr double kFitFlowGain = 0.05;
constexpr int kFitJointWins = 11;
constexpr double kScreenScoreRatio = 1.10;
constexpr double kScreenMoveRatio = 1.10;
constexpr int kScreenJointWins = 20;

constexpr int kEpochs = 48;
constexpr int kBatchPairs = 128;
constexpr float kLearningRate = 0.0015f;
constexpr float kGradientNorm = 1.0f;
constexpr float kL2 = 0.00002f;
constexpr std::uint32_t kModelSeed = 0x4d41'4e31u;
constexpr std::uint32_t kShuffleDomain = 0x4449'5348u;
constexpr std::uint64_t kExpectedCurriculumFingerprint =
    0x8657'ac0d'c83c'6041ull;
constexpr std::uint64_t kExpectedMatchedFingerprint =
    0xc1ad'c1ba'7dae'1d99ull;
constexpr int kExpectedMatchedPairs = 3'032;
constexpr double kWallLimitSeconds = 45.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr std::uint64_t kCheckpointMagic = 0x4437'4d52'5052'3031ull;
constexpr std::uint32_t kCheckpointVersion = 1;

static_assert(kLevelBonus == 17'000);
static_assert(kFitSeedEndExclusive - kFitSeedStart == kFitGames);
static_assert(kScreenSeedEndExclusive - kScreenSeedStart == kScreenGames);
static_assert(kSearchDepth == fair::kDepth);
static_assert(kSuccessorScenarios == kBoardSize);
static_assert(manifold::kDiscriminatorInputs == 295);
static_assert(manifold::kDiscriminatorHidden == 24);

struct Deadline {
  Clock::time_point started = Clock::now();
  double elapsedSeconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }
  void check() const {
    if (elapsedSeconds() > kWallLimitSeconds) {
      throw std::runtime_error("manifold root prior exceeded 45 minute cap");
    }
  }
};

std::uint64_t peakRssBytes() { return prior::peakRssBytes(); }

void enforceRss() {
  if (peakRssBytes() > kRssLimitBytes) {
    throw std::runtime_error("manifold root prior exceeded 256 MiB RSS");
  }
}

enum class SeedUse : std::uint8_t { kFit, kScreen };

bool allowedSeed(std::uint32_t seed, SeedUse use) {
  const std::uint32_t begin =
      use == SeedUse::kFit ? kFitSeedStart : kScreenSeedStart;
  const std::uint32_t end = use == SeedUse::kFit
                                ? kFitSeedEndExclusive
                                : kScreenSeedEndExclusive;
  const std::uint8_t prefix = static_cast<std::uint8_t>(seed >> 24u);
  return seed >= begin && seed < end && prefix != 0x4d && prefix != 0x7d &&
         prefix != 0xd7;
}

void requireSeed(std::uint32_t seed, SeedUse use) {
  if (!allowedSeed(seed, use)) {
    throw std::invalid_argument("seed outside frozen 3d6f root-prior lanes");
  }
}

struct Layout {
  static constexpr int w1 = 0;
  static constexpr int b1 =
      w1 + manifold::kDiscriminatorHidden * manifold::kDiscriminatorInputs;
  static constexpr int w2 = b1 + manifold::kDiscriminatorHidden;
  static constexpr int b2 = w2 + manifold::kDiscriminatorHidden;
  static constexpr int count = b2 + 1;
};
static_assert(Layout::count == 7'129);

struct Cache {
  std::array<float, manifold::kDiscriminatorHidden> hidden{};
  float logit = 0.0f;
};

class Model {
 public:
  explicit Model(std::uint32_t seed = kModelSeed)
      : parameters_(Layout::count, 0.0f), first_(Layout::count, 0.0f),
        second_(Layout::count, 0.0f) {
    Mulberry32 random(seed);
    const float radius = std::sqrt(
        6.0f / static_cast<float>(manifold::kDiscriminatorInputs +
                                  manifold::kDiscriminatorHidden));
    for (int index = 0;
         index < manifold::kDiscriminatorHidden *
                     manifold::kDiscriminatorInputs;
         ++index) {
      parameters_[Layout::w1 + index] = static_cast<float>(
          (2.0 * random.nextUnit() - 1.0) * radius);
    }
  }

  Cache forward(const manifold::TopologyFeatures& features) const {
    Cache cache;
    for (int output = 0; output < manifold::kDiscriminatorHidden; ++output) {
      float value = parameters_[Layout::b1 + output];
      const int weights =
          Layout::w1 + output * manifold::kDiscriminatorInputs;
      for (int input = 0; input < manifold::kDiscriminatorInputs; ++input) {
        value += parameters_[weights + input] * features[input];
      }
      cache.hidden[output] = std::tanh(value);
    }
    cache.logit = parameters_[Layout::b2];
    for (int hidden = 0; hidden < manifold::kDiscriminatorHidden; ++hidden) {
      cache.logit +=
          parameters_[Layout::w2 + hidden] * cache.hidden[hidden];
    }
    return cache;
  }

  float logit(const PublicState& state) const {
    return forward(manifold::topologyFeatures(state)).logit;
  }

  std::vector<float> gradient() const {
    return std::vector<float>(Layout::count, 0.0f);
  }

  void accumulate(const manifold::TopologyFeatures& features,
                  const Cache& cache, float derivative,
                  std::vector<float>& gradient) const {
    gradient[Layout::b2] += derivative;
    for (int hidden = 0; hidden < manifold::kDiscriminatorHidden; ++hidden) {
      gradient[Layout::w2 + hidden] += derivative * cache.hidden[hidden];
      const float hidden_derivative =
          derivative * parameters_[Layout::w2 + hidden] *
          (1.0f - cache.hidden[hidden] * cache.hidden[hidden]);
      gradient[Layout::b1 + hidden] += hidden_derivative;
      const int weights =
          Layout::w1 + hidden * manifold::kDiscriminatorInputs;
      for (int input = 0; input < manifold::kDiscriminatorInputs; ++input) {
        gradient[weights + input] += hidden_derivative * features[input];
      }
    }
  }

  void apply(std::vector<float>& gradient) {
    double squared_norm = 0.0;
    for (int index = 0; index < Layout::count; ++index) {
      if (index < Layout::b2) gradient[index] += kL2 * parameters_[index];
      squared_norm += gradient[index] * gradient[index];
    }
    const double norm = std::sqrt(squared_norm);
    const float scale = norm > kGradientNorm
                            ? static_cast<float>(kGradientNorm / norm)
                            : 1.0f;
    ++step_;
    constexpr float beta1 = 0.9f;
    constexpr float beta2 = 0.999f;
    constexpr float epsilon = 1.0e-8f;
    const float correction1 = 1.0f - std::pow(beta1, static_cast<float>(step_));
    const float correction2 = 1.0f - std::pow(beta2, static_cast<float>(step_));
    for (int index = 0; index < Layout::count; ++index) {
      const float value = gradient[index] * scale;
      first_[index] = beta1 * first_[index] + (1.0f - beta1) * value;
      second_[index] =
          beta2 * second_[index] + (1.0f - beta2) * value * value;
      parameters_[index] -=
          kLearningRate * (first_[index] / correction1) /
          (std::sqrt(second_[index] / correction2) + epsilon);
      if (!std::isfinite(parameters_[index])) {
        throw std::runtime_error("non-finite root-prior parameter");
      }
    }
  }

  const std::vector<float>& parameters() const { return parameters_; }

  void setParameters(const std::vector<float>& parameters) {
    if (parameters.size() != parameters_.size()) {
      throw std::invalid_argument("root-prior parameter count mismatch");
    }
    parameters_ = parameters;
    std::fill(first_.begin(), first_.end(), 0.0f);
    std::fill(second_.begin(), second_.end(), 0.0f);
    step_ = 0;
  }

 private:
  std::vector<float> parameters_;
  std::vector<float> first_;
  std::vector<float> second_;
  std::uint64_t step_ = 0;
};

using PublicScalar = float (Model::*)(const PublicState&) const;
static_assert(std::is_same_v<decltype(&Model::logit), PublicScalar>);
static_assert(
    !std::is_invocable_v<PublicScalar, const Model&, const State&>);

std::uint64_t modelFingerprint(const Model& model) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const float parameter : model.parameters()) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(parameter);
    for (int shift = 0; shift < 32; shift += 8) {
      prior::fingerprintByte(hash,
                             static_cast<std::uint8_t>(bits >> shift));
    }
  }
  return hash;
}

void saveCheckpoint(const std::string& path, const Model& model) {
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("could not open root-prior checkpoint");
  const std::uint32_t count = Layout::count;
  const std::uint64_t fingerprint = modelFingerprint(model);
  output.write(reinterpret_cast<const char*>(&kCheckpointMagic),
               sizeof(kCheckpointMagic));
  output.write(reinterpret_cast<const char*>(&kCheckpointVersion),
               sizeof(kCheckpointVersion));
  output.write(reinterpret_cast<const char*>(&count), sizeof(count));
  output.write(reinterpret_cast<const char*>(&fingerprint),
               sizeof(fingerprint));
  output.write(reinterpret_cast<const char*>(model.parameters().data()),
               static_cast<std::streamsize>(model.parameters().size() *
                                            sizeof(float)));
  if (!output) throw std::runtime_error("failed writing root-prior checkpoint");
}

Model loadCheckpoint(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not open root-prior checkpoint");
  std::uint64_t magic = 0;
  std::uint32_t version = 0;
  std::uint32_t count = 0;
  std::uint64_t expected = 0;
  input.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  input.read(reinterpret_cast<char*>(&version), sizeof(version));
  input.read(reinterpret_cast<char*>(&count), sizeof(count));
  input.read(reinterpret_cast<char*>(&expected), sizeof(expected));
  if (magic != kCheckpointMagic || version != kCheckpointVersion ||
      count != Layout::count) {
    throw std::runtime_error("invalid root-prior checkpoint header");
  }
  std::vector<float> parameters(count);
  input.read(reinterpret_cast<char*>(parameters.data()),
             static_cast<std::streamsize>(parameters.size() * sizeof(float)));
  char trailing = 0;
  if (!input || input.read(&trailing, 1)) {
    throw std::runtime_error("invalid root-prior checkpoint payload");
  }
  Model result;
  result.setParameters(parameters);
  if (modelFingerprint(result) != expected) {
    throw std::runtime_error("root-prior checkpoint fingerprint mismatch");
  }
  return result;
}

float sigmoid(float value) {
  if (value >= 0.0f) {
    const float exponential = std::exp(-value);
    return 1.0f / (1.0f + exponential);
  }
  const float exponential = std::exp(value);
  return exponential / (1.0f + exponential);
}

Model fitModel(const manifold::MatchedDataset& dataset,
               const Deadline& deadline) {
  std::vector<const manifold::MatchedPair*> pairs;
  pairs.reserve(dataset.matched);
  for (const auto& fold : dataset.folds) {
    for (const manifold::MatchedPair& pair : fold) pairs.push_back(&pair);
  }
  if (pairs.size() != kExpectedMatchedPairs) {
    throw std::runtime_error("root-prior fit did not receive 3,032 pairs");
  }
  Model model;
  std::vector<std::size_t> indexes(pairs.size());
  std::iota(indexes.begin(), indexes.end(), 0u);
  Mulberry32 random(mix32(kModelSeed ^ kShuffleDomain));
  for (int epoch = 0; epoch < kEpochs; ++epoch) {
    for (std::size_t index = indexes.size(); index > 1; --index) {
      const std::size_t selected = static_cast<std::size_t>(
          (static_cast<std::uint64_t>(random.nextBits()) * index) >> 32u);
      std::swap(indexes[index - 1], indexes[selected]);
    }
    for (std::size_t begin = 0; begin < indexes.size();
         begin += kBatchPairs) {
      if ((begin & 1'023u) == 0) deadline.check();
      const std::size_t end = std::min(indexes.size(), begin + kBatchPairs);
      const float inverse = 1.0f / static_cast<float>(2 * (end - begin));
      std::vector<float> gradient = model.gradient();
      for (std::size_t offset = begin; offset < end; ++offset) {
        const manifold::MatchedPair& pair = *pairs[indexes[offset]];
        const manifold::TopologyFeatures positive =
            manifold::topologyFeatures(pair.positive);
        const manifold::TopologyFeatures negative =
            manifold::topologyFeatures(pair.negative);
        const Cache positive_cache = model.forward(positive);
        const Cache negative_cache = model.forward(negative);
        model.accumulate(positive, positive_cache,
                         (sigmoid(positive_cache.logit) - 1.0f) * inverse,
                         gradient);
        model.accumulate(negative, negative_cache,
                         sigmoid(negative_cache.logit) * inverse, gradient);
      }
      model.apply(gradient);
    }
  }
  return model;
}

struct LabelMetrics {
  double auc = 0.0;
  double pair_ranking = 0.0;
  double loss = 0.0;
};

LabelMetrics labelMetrics(const Model& model,
                          const manifold::MatchedDataset& dataset) {
  struct Scored {
    double score = 0.0;
    bool positive = false;
  };
  std::vector<Scored> scores;
  scores.reserve(dataset.matched * 2);
  double wins = 0.0;
  double loss = 0.0;
  for (const auto& fold : dataset.folds) {
    for (const manifold::MatchedPair& pair : fold) {
      const double positive = model.logit(pair.positive);
      const double negative = model.logit(pair.negative);
      scores.push_back({positive, true});
      scores.push_back({negative, false});
      wins += positive > negative ? 1.0 : (positive == negative ? 0.5 : 0.0);
      loss += std::log1p(std::exp(-std::clamp(positive, -40.0, 40.0)));
      loss += std::log1p(std::exp(std::clamp(negative, -40.0, 40.0)));
    }
  }
  std::sort(scores.begin(), scores.end(), [](const Scored& a,
                                             const Scored& b) {
    return a.score < b.score;
  });
  double rank_sum = 0.0;
  std::size_t begin = 0;
  while (begin < scores.size()) {
    std::size_t end = begin + 1;
    while (end < scores.size() && scores[end].score == scores[begin].score) {
      ++end;
    }
    const double rank =
        0.5 * (static_cast<double>(begin + 1) + static_cast<double>(end));
    for (std::size_t index = begin; index < end; ++index) {
      if (scores[index].positive) rank_sum += rank;
    }
    begin = end;
  }
  const double count = static_cast<double>(dataset.matched);
  return {(rank_sum - count * (count + 1.0) * 0.5) / (count * count),
          wins / count, loss / (2.0 * count)};
}

std::vector<int> admittedActions(
    const std::array<double, kBoardSize>& values,
    const Board& board, int anchor) {
  if (anchor < 0 || !isLegal(board, anchor) ||
      !std::isfinite(values[anchor])) {
    throw std::invalid_argument("invalid D3 anchor");
  }
  std::vector<int> eligible;
  for (int column = 0; column < kBoardSize; ++column) {
    if (isLegal(board, column) && std::isfinite(values[column]) &&
        values[column] >= values[anchor] - kRootQWindow) {
      eligible.push_back(column);
    }
  }
  std::sort(eligible.begin(), eligible.end(), [&](int a, int b) {
    if (a == anchor) return true;
    if (b == anchor) return false;
    if (values[a] != values[b]) return values[a] > values[b];
    int a_order = 0;
    int b_order = 0;
    for (int order = 0; order < kBoardSize; ++order) {
      if (vr::kColumnOrder[order] == a) a_order = order;
      if (vr::kColumnOrder[order] == b) b_order = order;
    }
    return a_order < b_order;
  });
  if (eligible.size() > kMaximumAdmittedActions) {
    eligible.resize(kMaximumAdmittedActions);
  }
  if (eligible.empty() || eligible.front() != anchor) {
    throw std::runtime_error("D3 anchor was not retained");
  }
  return eligible;
}

double expectedSuccessorLogit(const PublicState& canonical, int action,
                              const Model& model) {
  const State public_state = vr::materialize(canonical);
  const std::uint32_t state_seed =
      detail::scenarioSeedForState(public_state, kSuccessorPolicySeed, 0);
  double total = 0.0;
  for (int scenario = 0; scenario < kSuccessorScenarios; ++scenario) {
    detail::StratifiedRandom random{state_seed, scenario,
                                    kSuccessorScenarios, 0};
    MoveResult move;
    if (!detail::playMoveSampled(public_state, action, random, move)) {
      throw std::runtime_error("root-prior successor rejected legal action");
    }
    if (move.state.game_over) {
      total += -10.0;
      continue;
    }
    move.state.score = 0;
    move.state.level = 1;
    move.state.moves_played = 0;
    move.state.next_disc = detail::sampledNextDisc(
        state_seed, scenario, kSuccessorScenarios);
    total += model.logit(vr::publicState(move.state));
  }
  return total / kSuccessorScenarios;
}

struct Decision {
  int action = -1;
  int d3_action = -1;
  int admitted = 0;
  bool changed = false;
  std::uint64_t work = 0;
  std::array<double, kBoardSize> root_values{};
  std::array<double, kBoardSize> manifold_values{};

  bool operator==(const Decision&) const = default;
};

Decision chooseAction(const PublicState& source, const Model& model) {
  if (source.terminal) return {};
  bool mirrored = false;
  const PublicState canonical = vr::canonicalState(source, mirrored);
  const State state = vr::materialize(canonical);
  const fair::SearchDecision search = fair::chooseFairAction(state);
  if (!search.complete || search.completed_depth != kSearchDepth) {
    throw std::runtime_error("root prior did not complete exact fair D3");
  }
  const std::vector<int> admitted =
      admittedActions(search.root_values, canonical.board, search.action);
  int selected = search.action;
  std::array<double, kBoardSize> manifold_values{};
  manifold_values.fill(-std::numeric_limits<double>::infinity());
  if (admitted.size() == 2) {
    for (const int action : admitted) {
      manifold_values[action] =
          expectedSuccessorLogit(canonical, action, model);
    }
    const int challenger = admitted[1];
    if (manifold_values[challenger] > manifold_values[search.action]) {
      selected = challenger;
    }
  }
  Decision result;
  result.action = mirrored ? kBoardSize - 1 - selected : selected;
  result.d3_action =
      mirrored ? kBoardSize - 1 - search.action : search.action;
  result.admitted = static_cast<int>(admitted.size());
  result.changed = selected != search.action;
  result.work = search.work;
  result.root_values.fill(-std::numeric_limits<double>::infinity());
  result.manifold_values.fill(-std::numeric_limits<double>::infinity());
  for (int canonical_action = 0; canonical_action < kBoardSize;
       ++canonical_action) {
    const int physical = mirrored ? kBoardSize - 1 - canonical_action
                                  : canonical_action;
    result.root_values[physical] = search.root_values[canonical_action];
    result.manifold_values[physical] = manifold_values[canonical_action];
  }
  return result;
}

using PublicPolicy = Decision (*)(const PublicState&, const Model&);
static_assert(std::is_same_v<decltype(&chooseAction), PublicPolicy>);
static_assert(!std::is_invocable_v<PublicPolicy, const State&, const Model&>);

enum class Policy : std::uint8_t { kRootPrior, kFairD3 };

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  int clears = 0;
  int reveals = 0;
  int maximum_chain = 0;
  int changed = 0;
  int two_admitted = 0;
  bool censored = false;
  std::uint64_t work = 0;
};

GameResult playGame(const Model& model, std::uint32_t seed, Policy policy,
                    SeedUse use, const Deadline& deadline) {
  requireSeed(seed, use);
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    if ((state.moves_played & 15) == 0) deadline.check();
    const PublicState public_state = vr::publicState(state);
    int action = -1;
    if (policy == Policy::kRootPrior) {
      const Decision decision = chooseAction(public_state, model);
      action = decision.action;
      result.changed += decision.changed;
      result.two_admitted += decision.admitted == 2;
      result.work += decision.work;
    } else {
      const State stripped = vr::materialize(public_state);
      const fair::SearchDecision decision = fair::chooseFairAction(stripped);
      if (!decision.complete || decision.completed_depth != kSearchDepth) {
        throw std::runtime_error("baseline did not complete exact fair D3");
      }
      action = decision.action;
      result.work += decision.work;
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("root-prior evaluation chose illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("root-prior evaluation transition failed");
    }
    for (const Wave& wave : move.waves) {
      result.clears += wave.cleared;
      result.reveals += wave.revealed;
      result.maximum_chain = std::max(result.maximum_chain, wave.depth);
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  return result;
}

struct Cohort {
  std::vector<GameResult> candidate;
  std::vector<GameResult> baseline;
  double wall_seconds = 0.0;
};

Cohort runCohort(const Model& model, std::uint32_t seed_start, int games,
                 SeedUse use, int threads, const Deadline& deadline) {
  const Clock::time_point started = Clock::now();
  Cohort result;
  result.candidate.resize(games);
  result.baseline.resize(games);
  std::atomic<int> next{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(threads, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next.fetch_add(1);
        if (game >= games) return;
        const std::uint32_t seed =
            seed_start + static_cast<std::uint32_t>(game);
        result.candidate[game] =
            playGame(model, seed, Policy::kRootPrior, use, deadline);
        result.baseline[game] =
            playGame(model, seed, Policy::kFairD3, use, deadline);
        std::cerr << "root-prior seed 0x" << std::hex << seed << std::dec
                  << " candidate " << result.candidate[game].score << '/'
                  << result.candidate[game].moves << " D3 "
                  << result.baseline[game].score << '/'
                  << result.baseline[game].moves << '\n';
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  enforceRss();
  return result;
}

struct Summary {
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double changed_per_move = 0.0;
  double two_admitted_per_move = 0.0;
  double work_per_move = 0.0;
  int maximum_chain = 0;
  int censored = 0;
};

Summary summarize(const std::vector<GameResult>& games) {
  if (games.empty()) throw std::invalid_argument("cannot summarize no games");
  Summary result;
  std::int64_t score = 0;
  std::int64_t moves = 0;
  std::int64_t clears = 0;
  std::int64_t reveals = 0;
  std::int64_t changed = 0;
  std::int64_t admitted = 0;
  std::uint64_t work = 0;
  for (const GameResult& game : games) {
    score += game.score;
    moves += game.moves;
    clears += game.clears;
    reveals += game.reveals;
    changed += game.changed;
    admitted += game.two_admitted;
    work += game.work;
    result.maximum_chain = std::max(result.maximum_chain, game.maximum_chain);
    result.censored += game.censored;
  }
  result.mean_score = static_cast<double>(score) / games.size();
  result.mean_moves = static_cast<double>(moves) / games.size();
  result.clears_per_move = static_cast<double>(clears) / moves;
  result.reveals_per_move = static_cast<double>(reveals) / moves;
  result.changed_per_move = static_cast<double>(changed) / moves;
  result.two_admitted_per_move = static_cast<double>(admitted) / moves;
  result.work_per_move = static_cast<double>(work) / moves;
  return result;
}

struct Paired {
  int score_wins = 0;
  int move_wins = 0;
  int joint_wins = 0;
  double score_delta = 0.0;
  double move_delta = 0.0;
};

Paired pair(const Cohort& cohort) {
  if (cohort.candidate.size() != cohort.baseline.size() ||
      cohort.candidate.empty()) {
    throw std::invalid_argument("invalid paired cohort");
  }
  Paired result;
  for (std::size_t index = 0; index < cohort.candidate.size(); ++index) {
    const GameResult& candidate = cohort.candidate[index];
    const GameResult& baseline = cohort.baseline[index];
    if (candidate.seed != baseline.seed) {
      throw std::runtime_error("paired seed mismatch");
    }
    const bool score_win = candidate.score > baseline.score;
    const bool move_win = candidate.moves > baseline.moves;
    result.score_wins += score_win;
    result.move_wins += move_win;
    result.joint_wins += score_win && move_win;
    result.score_delta += candidate.score - baseline.score;
    result.move_delta += candidate.moves - baseline.moves;
  }
  result.score_delta /= cohort.candidate.size();
  result.move_delta /= cohort.candidate.size();
  return result;
}

bool passesFit(const Summary& candidate, const Summary& baseline,
               const Paired& paired) {
  return candidate.mean_score >= kFitScoreRatio * baseline.mean_score &&
         candidate.mean_moves >= kFitMoveRatio * baseline.mean_moves &&
         candidate.clears_per_move >=
             baseline.clears_per_move + kFitFlowGain &&
         candidate.reveals_per_move >=
             baseline.reveals_per_move + kFitFlowGain &&
         paired.joint_wins >= kFitJointWins;
}

bool passesScreen(const Summary& candidate, const Summary& baseline,
                  const Paired& paired) {
  return candidate.mean_score >= kScreenScoreRatio * baseline.mean_score &&
         candidate.mean_moves >= kScreenMoveRatio * baseline.mean_moves &&
         candidate.clears_per_move >= baseline.clears_per_move &&
         candidate.reveals_per_move >= baseline.reveals_per_move &&
         paired.joint_wins >= kScreenJointWins;
}

struct Options {
  std::string curriculum = "/tmp/drop7-oracle-curriculum-states.jsonl";
  std::string checkpoint = "/tmp/drop7-manifold-root-prior.bin";
  std::string output = "/tmp/drop7-manifold-root-prior.json";
  int threads = 4;
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--curriculum" && index + 1 < argc) {
      result.curriculum = argv[++index];
    } else if (argument == "--checkpoint" && index + 1 < argc) {
      result.checkpoint = argv[++index];
    } else if (argument == "--output" && index + 1 < argc) {
      result.output = argv[++index];
    } else if (argument == "--threads" && index + 1 < argc) {
      result.threads = std::stoi(argv[++index]);
    } else {
      throw std::invalid_argument("unknown or incomplete option");
    }
  }
  if (result.curriculum.empty() || result.checkpoint.empty() ||
      result.output.empty() || result.threads < 1 ||
      result.threads > kMaximumThreads) {
    throw std::invalid_argument("invalid root-prior options");
  }
  return result;
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"changedPerMove\":" << summary.changed_per_move
         << ",\"twoAdmittedPerMove\":" << summary.two_admitted_per_move
         << ",\"workPerMove\":" << summary.work_per_move
         << ",\"maximumChain\":" << summary.maximum_chain
         << ",\"censored\":" << summary.censored << '}';
}

void writePaired(std::ostream& output, const Paired& paired) {
  output << "{\"scoreWins\":" << paired.score_wins
         << ",\"moveWins\":" << paired.move_wins
         << ",\"jointWins\":" << paired.joint_wins
         << ",\"meanScoreDelta\":" << paired.score_delta
         << ",\"meanMoveDelta\":" << paired.move_delta << '}';
}

void writeCohort(std::ostream& output, const Cohort& cohort,
                 const Summary& candidate, const Summary& baseline,
                 const Paired& paired, bool passed) {
  output << "{\"candidate\":";
  writeSummary(output, candidate);
  output << ",\"fairD3\":";
  writeSummary(output, baseline);
  output << ",\"paired\":";
  writePaired(output, paired);
  output << ",\"passed\":" << (passed ? "true" : "false")
         << ",\"wallSeconds\":" << cohort.wall_seconds << '}';
}

void writeArtifact(const Options& options,
                   const manifold::MatchedDataset& dataset,
                   const LabelMetrics& labels, const Model& model,
                   const Cohort& fit, const Summary& fit_candidate,
                   const Summary& fit_baseline, const Paired& fit_paired,
                   bool fit_passed, const std::optional<Cohort>& screen,
                   const std::optional<Summary>& screen_candidate,
                   const std::optional<Summary>& screen_baseline,
                   const std::optional<Paired>& screen_paired,
                   bool screen_passed, double wall_seconds) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open root-prior artifact");
  output << std::setprecision(12)
         << "{\n  \"format\":\"drop7-manifold-root-prior-v1\","
         << "\n  \"hypothesis\":\"fixed public manifold scalar as close-D3 root tie-break only\","
         << "\n  \"model\":{\"input\":295,\"hidden\":24,\"output\":1,\"parameters\":7129,"
         << "\"reflection\":\"exact canonical public state plus invariant aggregates\","
         << "\"epochs\":" << kEpochs << ",\"pairs\":"
         << dataset.matched << ",\"datasetFingerprint\":\"0x" << std::hex
         << dataset.fingerprint << "\",\"modelFingerprint\":\"0x"
         << modelFingerprint(model) << std::dec << "\",\"trainingAuc\":"
         << labels.auc << ",\"trainingPairRanking\":"
         << labels.pair_ranking << ",\"trainingLoss\":" << labels.loss
         << "},"
         << "\n  \"rootPolicy\":{\"search\":\"exact fair D3\",\"qWindow\":"
         << kRootQWindow << ",\"maximumAdmittedActions\":"
         << kMaximumAdmittedActions << ",\"successors\":"
         << kSuccessorScenarios
         << ",\"successorSource\":\"public-state-derived stratified immediate outcomes\",\"ties\":\"fair D3\"},"
         << "\n  \"seedLanes\":{\"discriminatorNegatives\":\"replay of already-opened 0x3d6b0000..0x3d6b03ff\",\"fit\":\"0x3d6f0000..0x3d6f000f\",\"screen\":"
         << (screen ? "\"0x3d6f1000..0x3d6f101f\"" : "\"unopened\"")
         << "},"
         << "\n  \"fitGate\":{\"scoreRatio\":" << kFitScoreRatio
         << ",\"moveRatio\":" << kFitMoveRatio
         << ",\"clearGainPerMove\":" << kFitFlowGain
         << ",\"revealGainPerMove\":" << kFitFlowGain
         << ",\"jointWins\":" << kFitJointWins << "},"
         << "\n  \"fit\":";
  writeCohort(output, fit, fit_candidate, fit_baseline, fit_paired,
              fit_passed);
  output << ",\n  \"screenGate\":{\"scoreRatio\":" << kScreenScoreRatio
         << ",\"moveRatio\":" << kScreenMoveRatio
         << ",\"flowNonregression\":true,\"jointWins\":"
         << kScreenJointWins << "},\n  \"screen\":";
  if (screen) {
    writeCohort(output, *screen, *screen_candidate, *screen_baseline,
                *screen_paired, screen_passed);
  } else {
    output << "null";
  }
  output << ",\n  \"checkpoint\":\"" << options.checkpoint
         << "\",\n  \"passed\":"
         << (fit_passed && screen_passed ? "true" : "false")
         << ",\n  \"wallSeconds\":" << wall_seconds
         << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
  if (!output) throw std::runtime_error("failed writing root-prior artifact");
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
  const Deadline deadline;
  const prior::Curriculum curriculum = prior::loadCurriculum(options.curriculum);
  expect(curriculum.fingerprint == kExpectedCurriculumFingerprint,
         "curriculum checksum self-test failed");
  const manifold::MatchedDataset dataset =
      manifold::buildMatchedDataset(curriculum, deadline);
  expect(dataset.matched == kExpectedMatchedPairs &&
             dataset.fingerprint == kExpectedMatchedFingerprint,
         "matched dataset self-test failed");
  const Model first = fitModel(dataset, deadline);
  const Model second = fitModel(dataset, deadline);
  expect(first.parameters() == second.parameters() &&
             modelFingerprint(first) == modelFingerprint(second),
         "deterministic fit self-test failed");

  const PublicState fixture = prior::fixtureState();
  expect(first.logit(fixture) == first.logit(vr::mirror(fixture)),
         "scalar reflection self-test failed");
  const Decision action = chooseAction(fixture, first);
  const Decision reflected = chooseAction(vr::mirror(fixture), first);
  expect(reflected.action == kBoardSize - 1 - action.action &&
             reflected.d3_action == kBoardSize - 1 - action.d3_action,
         "root policy reflection self-test failed");
  State metadata = vr::materialize(fixture);
  metadata.score = 8'888'888;
  metadata.level = 444;
  metadata.moves_played = 333;
  expect(vr::publicState(metadata) == fixture &&
             first.logit(vr::publicState(metadata)) == first.logit(fixture) &&
             chooseAction(vr::publicState(metadata), first) == action,
         "root policy retained hidden metadata");

  const std::string checkpoint = options.checkpoint + ".self-test";
  saveCheckpoint(checkpoint, first);
  const Model restored = loadCheckpoint(checkpoint);
  expect(restored.parameters() == first.parameters() &&
             modelFingerprint(restored) == modelFingerprint(first) &&
             restored.logit(fixture) == first.logit(fixture),
         "checkpoint round-trip self-test failed");

  Board empty{};
  empty.fill(kEmpty);
  std::array<double, kBoardSize> values{};
  values.fill(-10'000.0);
  values[3] = 10'000.0;
  values[2] = 7'501.0;
  values[4] = 7'499.0;
  const std::vector<int> admitted = admittedActions(values, empty, 3);
  expect(admitted.size() == 2 && admitted[0] == 3 && admitted[1] == 2,
         "top-two/window admission self-test failed");

  Summary fit_candidate;
  fit_candidate.mean_score = 116;
  fit_candidate.mean_moves = 116;
  fit_candidate.clears_per_move = 2.051;
  fit_candidate.reveals_per_move = 1.151;
  Summary baseline;
  baseline.mean_score = 100;
  baseline.mean_moves = 100;
  baseline.clears_per_move = 2.0;
  baseline.reveals_per_move = 1.1;
  Paired fit_pair;
  fit_pair.joint_wins = 11;
  expect(passesFit(fit_candidate, baseline, fit_pair),
         "positive fit gate self-test failed");
  fit_candidate.mean_score = 114;
  expect(!passesFit(fit_candidate, baseline, fit_pair),
         "negative fit gate self-test failed");
  Summary screen_candidate = baseline;
  screen_candidate.mean_score = 111;
  screen_candidate.mean_moves = 111;
  Paired screen_pair;
  screen_pair.joint_wins = 20;
  expect(passesScreen(screen_candidate, baseline, screen_pair),
         "positive screen gate self-test failed");
  screen_candidate.reveals_per_move = 1.09;
  expect(!passesScreen(screen_candidate, baseline, screen_pair),
         "negative screen gate self-test failed");

  expect(allowedSeed(kFitSeedStart, SeedUse::kFit) &&
             allowedSeed(kFitSeedEndExclusive - 1, SeedUse::kFit) &&
             allowedSeed(kScreenSeedStart, SeedUse::kScreen) &&
             allowedSeed(kScreenSeedEndExclusive - 1, SeedUse::kScreen) &&
             throwsInvalid([] {
               requireSeed(0x3d6f'0010u, SeedUse::kFit);
             }) &&
             throwsInvalid([] {
               requireSeed(0x3d6f'1020u, SeedUse::kScreen);
             }) &&
             throwsInvalid([] {
               requireSeed(0x3d68'0000u, SeedUse::kFit);
             }) &&
             throwsInvalid([] {
               requireSeed(0x3d69'0000u, SeedUse::kScreen);
             }) &&
             throwsInvalid([] {
               requireSeed(0x4d6f'0000u, SeedUse::kFit);
             }) &&
             throwsInvalid([] {
               requireSeed(0x7d6f'0000u, SeedUse::kScreen);
             }) &&
             throwsInvalid([] {
               requireSeed(0xd76f'0000u, SeedUse::kScreen);
             }),
         "seed guards self-test failed");
  enforceRss();
  output << std::setprecision(12)
         << "MANIFOLD_ROOT_PRIOR_SELF_TEST {\"passed\":true,"
         << "\"curriculumChecksum\":true,\"matchedDataset\":"
         << dataset.matched << ",\"deterministicFit\":true,"
         << "\"reflectionExact\":true,\"metadataBlind\":true,"
         << "\"checkpointRoundTrip\":true,\"rootAdmission\":true,"
         << "\"gateWiring\":true,\"seedGuards\":true,"
         << "\"modelFingerprint\":\"0x" << std::hex
         << modelFingerprint(first) << std::dec << "\",\"peakRssBytes\":"
         << peakRssBytes() << "}\n";
  return true;
}

int run(const Options& options, std::ostream& output) {
  const Deadline deadline;
  const prior::Curriculum curriculum = prior::loadCurriculum(options.curriculum);
  if (curriculum.fingerprint != kExpectedCurriculumFingerprint) {
    throw std::runtime_error("public curriculum checksum mismatch");
  }
  const manifold::MatchedDataset dataset =
      manifold::buildMatchedDataset(curriculum, deadline);
  if (dataset.matched != kExpectedMatchedPairs ||
      dataset.fingerprint != kExpectedMatchedFingerprint) {
    throw std::runtime_error("exact matched dataset fingerprint mismatch");
  }
  const Model trained = fitModel(dataset, deadline);
  const LabelMetrics labels = labelMetrics(trained, dataset);
  saveCheckpoint(options.checkpoint, trained);
  const Model model = loadCheckpoint(options.checkpoint);
  if (modelFingerprint(model) != modelFingerprint(trained)) {
    throw std::runtime_error("frozen root-prior checkpoint mismatch");
  }
  output << std::setprecision(8)
         << "MANIFOLD_ROOT_PRIOR_MODEL {\"pairs\":" << dataset.matched
         << ",\"auc\":" << labels.auc << ",\"pairRanking\":"
         << labels.pair_ranking << ",\"loss\":" << labels.loss
         << ",\"fingerprint\":\"0x" << std::hex
         << modelFingerprint(model) << std::dec << "\"}\n";

  const Cohort fit = runCohort(model, kFitSeedStart, kFitGames, SeedUse::kFit,
                               options.threads, deadline);
  const Summary fit_candidate = summarize(fit.candidate);
  const Summary fit_baseline = summarize(fit.baseline);
  const Paired fit_paired = pair(fit);
  const bool fit_passed = passesFit(fit_candidate, fit_baseline, fit_paired);
  output << std::fixed << std::setprecision(3)
         << "MANIFOLD_ROOT_PRIOR_FIT {\"candidateScore\":"
         << fit_candidate.mean_score << ",\"candidateMoves\":"
         << fit_candidate.mean_moves << ",\"d3Score\":"
         << fit_baseline.mean_score << ",\"d3Moves\":"
         << fit_baseline.mean_moves << ",\"candidateFlow\":"
         << fit_candidate.clears_per_move << '/'
         << fit_candidate.reveals_per_move << ",\"d3Flow\":"
         << fit_baseline.clears_per_move << '/'
         << fit_baseline.reveals_per_move << ",\"jointWins\":"
         << fit_paired.joint_wins << ",\"passed\":"
         << (fit_passed ? "true" : "false") << "}\n";

  std::optional<Cohort> screen;
  std::optional<Summary> screen_candidate;
  std::optional<Summary> screen_baseline;
  std::optional<Paired> screen_paired;
  bool screen_passed = false;
  if (fit_passed) {
    screen = runCohort(model, kScreenSeedStart, kScreenGames,
                       SeedUse::kScreen, options.threads, deadline);
    screen_candidate = summarize(screen->candidate);
    screen_baseline = summarize(screen->baseline);
    screen_paired = pair(*screen);
    screen_passed = passesScreen(*screen_candidate, *screen_baseline,
                                 *screen_paired);
  }
  deadline.check();
  enforceRss();
  writeArtifact(options, dataset, labels, model, fit, fit_candidate,
                fit_baseline, fit_paired, fit_passed, screen,
                screen_candidate, screen_baseline, screen_paired,
                screen_passed, deadline.elapsedSeconds());
  output << std::fixed << std::setprecision(3)
         << "MANIFOLD_ROOT_PRIOR_RESULT {\"fitPassed\":"
         << (fit_passed ? "true" : "false") << ",\"screenOpened\":"
         << (screen ? "true" : "false") << ",\"screenPassed\":"
         << (screen_passed ? "true" : "false") << ",\"wallSeconds\":"
         << deadline.elapsedSeconds() << ",\"peakRssBytes\":"
         << peakRssBytes() << ",\"artifact\":\"" << options.output
         << "\"}\n";
  return fit_passed && screen_passed ? EXIT_SUCCESS : 2;
}

}  // namespace drop7::manifold_root_prior

int main(int argc, char** argv) {
  try {
    if (argc < 2) throw std::invalid_argument("missing mode");
    const std::string_view mode(argv[1]);
    const drop7::manifold_root_prior::Options options =
        drop7::manifold_root_prior::parseOptions(argc, argv, 2);
    if (mode == "--self-test") {
      return drop7::manifold_root_prior::selfTest(options, std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (mode == "--run") {
      return drop7::manifold_root_prior::run(options, std::cout);
    }
    throw std::invalid_argument(
        "usage: drop7_manifold_root_prior --self-test | --run [--curriculum PATH] [--checkpoint PATH] [--output PATH] [--threads 1..4]");
  } catch (const std::exception& error) {
    std::cerr << "drop7_manifold_root_prior: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
