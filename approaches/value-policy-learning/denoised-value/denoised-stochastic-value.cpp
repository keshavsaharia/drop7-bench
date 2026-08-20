#include "../../../src/core/native/public-behavior.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

// A bounded test of denoised public-state values. Roll-in games provide states
// only; every target is re-estimated from independent futures whose policy and
// randomness depend solely on the public board, next disc, and rise phase.
namespace drop7::denoised_stochastic_value {

constexpr int kHidden1 = 64;
constexpr int kHidden2 = 32;
constexpr int kHeads = 3;
constexpr int kTokenCount = 10;
constexpr int kMetricCount = 16;
constexpr int kCellCategoryCount = kCellCount * kTokenCount;
constexpr int kDiscCategoryBase = kCellCategoryCount;
constexpr int kPhaseCategoryBase = kDiscCategoryBase + kBoardSize;
constexpr int kCategoryCount = kPhaseCategoryBase + kMovesPerLevel;
constexpr int kActiveCategories = kCellCount + 2;
constexpr int kRollinGames = 64;
constexpr int kTrainingRollinGames = 48;
constexpr int kHoldoutRollinGames = 16;
constexpr int kSampleStride = 3;
constexpr int kMaximumStatesPerGame = 32;
constexpr int kContinuations = 32;
constexpr int kContinuationCap = 50;
constexpr int kGameMoveCap = 500;
constexpr int kEpochs = 30;
constexpr int kBatchSize = 64;
constexpr int kRootStrata = 5;
constexpr float kLearningRate = 0.001f;
constexpr float kWeightDecay = 1.0e-5f;
constexpr double kRequiredSpearman = 0.70;
constexpr double kRequiredSurvivalAuc = 0.80;
constexpr double kMaximumSurvivalEce = 0.10;
constexpr double kSwitchMargin = 3.0;
constexpr double kMinimumSupport = 20.0;
constexpr double kSupportRatio = 0.80;
constexpr double kMaximumOrientationGap = 5.0;
constexpr std::uint32_t kRollinStart = 0x3d70'6800u;
constexpr std::uint32_t kScreenStart = 0x3e82'0000u;
constexpr std::uint32_t kConfirmationStart = 0x3e83'0000u;
// Domains below are continuation/model salts, never headless game seeds.
constexpr std::uint32_t kTrainingLabelDomain = 0x4c42'5452u;
constexpr std::uint32_t kHoldoutLabelDomain = 0x4c42'484fu;

using Engineered = std::array<float, kMetricCount>;
using Categories = std::array<int, kActiveCategories>;

struct PublicState {
  Board board{};
  std::uint8_t next_disc = 1;
  std::uint8_t moves_remaining = kMovesPerLevel;
};

PublicState publicState(const State& state) {
  return {state.board, state.next_disc,
          static_cast<std::uint8_t>(state.moves_remaining)};
}

State materialize(const PublicState& state) {
  State result;
  result.board = state.board;
  result.next_disc = state.next_disc;
  result.moves_remaining = state.moves_remaining;
  return result;
}

PublicState mirror(const PublicState& state) {
  PublicState result = state;
  result.board = cfpi::detail::mirrorBoard(state.board);
  return result;
}

PublicState canonicalize(const PublicState& state) {
  return cfpi::detail::mirroredRepresentationIsSmaller(state.board)
             ? mirror(state)
             : state;
}

bool samePublicState(const PublicState& first, const PublicState& second) {
  return first.board == second.board && first.next_disc == second.next_disc &&
         first.moves_remaining == second.moves_remaining;
}

std::string publicKey(const PublicState& source) {
  const PublicState state = canonicalize(source);
  std::string result;
  result.reserve(kCellCount + 2);
  for (std::uint8_t cell : state.board) {
    result.push_back(static_cast<char>(cell));
  }
  result.push_back(static_cast<char>(state.next_disc));
  result.push_back(static_cast<char>(state.moves_remaining));
  return result;
}

std::uint32_t publicHash(const PublicState& source) {
  const PublicState state = canonicalize(source);
  std::uint32_t hash = 0x811c'9dc5u;
  for (std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint32_t>(cell) + 1u;
    hash *= 0x0100'0193u;
  }
  hash ^= state.next_disc;
  hash *= 0x0100'0193u;
  hash ^= state.moves_remaining;
  return mix32(hash);
}

Engineered rawEngineered(const PublicState& source) {
  const cfpi::detail::PhaseFeatures features =
      cfpi::detail::extractPhaseFeatures(materialize(source));
  return {{
      static_cast<float>(features.open_columns),
      static_cast<float>(features.direct_potential),
      static_cast<float>(features.latent_chain_potential),
      static_cast<float>(features.cracked_exposure),
      static_cast<float>(features.solid_exposure),
      static_cast<float>(features.adjacent_ones),
      static_cast<float>(features.triple_twos),
      static_cast<float>(features.dead_low_numbers),
      static_cast<float>(features.projected_occupancy_debt),
      static_cast<float>(features.residual_cover_debt),
      static_cast<float>(features.cover_altitude_debt),
      static_cast<float>(features.peak_height_risk),
      static_cast<float>(features.quiet_build_options),
      static_cast<float>(features.quiet_direct_gain),
      static_cast<float>(features.trigger_readiness),
      static_cast<float>(features.rise_trigger_readiness),
  }};
}

Categories activeCategories(const PublicState& state) {
  if (state.next_disc < 1 || state.next_disc > kBoardSize ||
      state.moves_remaining < 1 || state.moves_remaining > kMovesPerLevel) {
    throw std::invalid_argument("invalid denoised public state");
  }
  Categories result{};
  int offset = 0;
  for (int index = 0; index < kCellCount; ++index) {
    const int token = state.board[index];
    if (token < 0 || token >= kTokenCount) {
      throw std::invalid_argument("invalid denoised board token");
    }
    result[offset++] = index * kTokenCount + token;
  }
  result[offset++] = kDiscCategoryBase + state.next_disc - 1;
  result[offset++] = kPhaseCategoryBase + state.moves_remaining - 1;
  if (offset != kActiveCategories) {
    throw std::logic_error("denoised category-count invariant failed");
  }
  return result;
}

struct Target {
  PublicState state{};
  float expected_lifetime = 0;
  float survival_25 = 0;
  float survival_50 = 0;
  float lifetime_standard_error = 0;
  float survival_25_standard_error = 0;
  float survival_50_standard_error = 0;
  std::uint64_t simulated_moves = 0;
  std::uint64_t policy_work = 0;
};

std::uint32_t continuationSeed(const PublicState& state,
                               std::uint32_t domain, int continuation) {
  return mix32(publicHash(state) ^ domain ^
               (static_cast<std::uint32_t>(continuation + 1) *
                0x9e37'79b9u));
}

Target labelState(const PublicState& source, std::uint32_t domain) {
  const PublicState public_state = canonicalize(source);
  std::array<double, kContinuations> lifetimes{};
  int survived_25 = 0;
  int survived_50 = 0;
  Target result;
  result.state = public_state;
  for (int continuation = 0; continuation < kContinuations; ++continuation) {
    State state = materialize(public_state);
    Mulberry32 random(continuationSeed(public_state, domain, continuation));
    int moves = 0;
    bool alive_after_25 = false;
    while (!state.game_over && moves < kContinuationCap) {
      cfpi::BehaviorMetrics metrics;
      const int action = cfpi::choosePhaseGreedyAction(state, 1, &metrics);
      result.policy_work += metrics.work;
      MoveResult move;
      if (!playMove(state, action, random, move)) {
        throw std::runtime_error("public continuation selected illegal action");
      }
      state = move.state;
      ++moves;
      ++result.simulated_moves;
      if (moves == 25) alive_after_25 = !state.game_over;
    }
    lifetimes[continuation] = moves;
    survived_25 += alive_after_25 ? 1 : 0;
    survived_50 += moves == kContinuationCap && !state.game_over ? 1 : 0;
  }
  const double lifetime_mean =
      std::accumulate(lifetimes.begin(), lifetimes.end(), 0.0) /
      kContinuations;
  double lifetime_squares = 0;
  for (double lifetime : lifetimes) {
    lifetime_squares += (lifetime - lifetime_mean) *
                        (lifetime - lifetime_mean);
  }
  const double probability_25 =
      static_cast<double>(survived_25) / kContinuations;
  const double probability_50 =
      static_cast<double>(survived_50) / kContinuations;
  result.expected_lifetime = static_cast<float>(lifetime_mean);
  result.survival_25 = static_cast<float>(probability_25);
  result.survival_50 = static_cast<float>(probability_50);
  result.lifetime_standard_error = static_cast<float>(std::sqrt(
      lifetime_squares /
      static_cast<double>((kContinuations - 1) * kContinuations)));
  result.survival_25_standard_error = static_cast<float>(std::sqrt(
      probability_25 * (1.0 - probability_25) / kContinuations));
  result.survival_50_standard_error = static_cast<float>(std::sqrt(
      probability_50 * (1.0 - probability_50) / kContinuations));
  return result;
}

struct RollinGame {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t teacher_work = 0;
  std::vector<PublicState> states;
};

RollinGame collectRollin(std::uint32_t seed) {
  State state = initialHeadlessState(seed);
  RollinGame result;
  result.seed = seed;
  result.states.reserve(kMaximumStatesPerGame);
  while (!state.game_over && state.moves_played < kGameMoveCap) {
    if (state.moves_played % kSampleStride == 0 &&
        static_cast<int>(result.states.size()) < kMaximumStatesPerGame) {
      result.states.push_back(publicState(state));
    }
    cfpi::BehaviorMetrics metrics;
    const int action = cfpi::chooseBehaviorAction(state, {}, &metrics);
    result.teacher_work += metrics.work;
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("exact roll-in selected illegal action");
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  return result;
}

std::vector<RollinGame> collectRollins(int threads) {
  std::vector<RollinGame> games(kRollinGames);
  std::atomic<int> next{0};
  std::atomic<bool> failed{false};
  std::mutex error_mutex;
  std::string error_message;
  std::vector<std::thread> workers;
  const int worker_count = std::min(threads, kRollinGames);
  workers.reserve(worker_count);
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.emplace_back([&]() {
      while (!failed.load(std::memory_order_relaxed)) {
        const int index = next.fetch_add(1, std::memory_order_relaxed);
        if (index >= kRollinGames) break;
        try {
          games[index] = collectRollin(
              kRollinStart + static_cast<std::uint32_t>(index));
        } catch (const std::exception& error) {
          failed.store(true, std::memory_order_relaxed);
          std::lock_guard<std::mutex> lock(error_mutex);
          error_message = error.what();
        }
      }
    });
  }
  for (std::thread& worker : workers) worker.join();
  if (failed.load()) {
    throw std::runtime_error("parallel roll-in failed: " + error_message);
  }
  return games;
}

std::vector<Target> labelParallel(const std::vector<PublicState>& states,
                                  std::uint32_t domain, int threads) {
  std::vector<Target> targets(states.size());
  std::atomic<std::size_t> next{0};
  std::atomic<bool> failed{false};
  std::mutex error_mutex;
  std::string error_message;
  std::vector<std::thread> workers;
  const int worker_count =
      std::min<int>(threads, static_cast<int>(states.size()));
  workers.reserve(worker_count);
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.emplace_back([&]() {
      while (!failed.load(std::memory_order_relaxed)) {
        const std::size_t index =
            next.fetch_add(1, std::memory_order_relaxed);
        if (index >= states.size()) break;
        try {
          targets[index] = labelState(states[index], domain);
        } catch (const std::exception& error) {
          failed.store(true, std::memory_order_relaxed);
          std::lock_guard<std::mutex> lock(error_mutex);
          error_message = error.what();
        }
      }
    });
  }
  for (std::thread& worker : workers) worker.join();
  if (failed.load()) {
    throw std::runtime_error("parallel target labeling failed: " +
                             error_message);
  }
  return targets;
}

struct Normalizer {
  Engineered mean{};
  Engineered scale{};

  Engineered apply(const Engineered& raw) const {
    Engineered result{};
    for (int metric = 0; metric < kMetricCount; ++metric) {
      result[metric] = std::clamp(
          (raw[metric] - mean[metric]) / scale[metric], -6.0f, 6.0f);
    }
    return result;
  }
};

Normalizer fitNormalizer(const std::vector<Target>& targets) {
  if (targets.empty()) throw std::invalid_argument("empty normalizer target set");
  std::array<double, kMetricCount> sum{};
  std::array<double, kMetricCount> squares{};
  for (const Target& target : targets) {
    const Engineered raw = rawEngineered(target.state);
    for (int metric = 0; metric < kMetricCount; ++metric) {
      sum[metric] += raw[metric];
      squares[metric] += raw[metric] * raw[metric];
    }
  }
  Normalizer result;
  for (int metric = 0; metric < kMetricCount; ++metric) {
    const double mean = sum[metric] / targets.size();
    const double variance =
        std::max(0.0, squares[metric] / targets.size() - mean * mean);
    result.mean[metric] = static_cast<float>(mean);
    result.scale[metric] =
        variance < 1.0e-8 ? 1.0f : static_cast<float>(std::sqrt(variance));
  }
  return result;
}

struct Example {
  Target target{};
  Engineered metrics{};
};

std::vector<Example> prepare(const std::vector<Target>& targets,
                             const Normalizer& normalizer) {
  std::vector<Example> result;
  result.reserve(targets.size());
  for (const Target& target : targets) {
    result.push_back(
        {target, normalizer.apply(rawEngineered(target.state))});
  }
  return result;
}

float sigmoid(float value) {
  if (value >= 0) return 1.0f / (1.0f + std::exp(-value));
  const float exponential = std::exp(value);
  return exponential / (1.0f + exponential);
}

float logit(float probability) {
  const float clipped = std::clamp(probability, 1.0e-6f, 1.0f - 1.0e-6f);
  return std::log(clipped / (1.0f - clipped));
}

float leaky(float value) { return value >= 0 ? value : 0.05f * value; }
float leakyDerivative(float value) { return value >= 0 ? 1.0f : 0.05f; }

struct Parameters {
  std::vector<float> embedding =
      std::vector<float>(static_cast<std::size_t>(kCategoryCount) * kHidden1);
  std::array<float, kMetricCount * kHidden1> metric_weight{};
  std::array<float, kHidden1> bias1{};
  std::array<float, kHidden2 * kHidden1> weight2{};
  std::array<float, kHidden2> bias2{};
  std::array<float, kHeads * kHidden2> output_weight{};
  std::array<float, kHeads> output_bias{};
};

constexpr std::size_t parameterCount() {
  return static_cast<std::size_t>(kCategoryCount) * kHidden1 +
         static_cast<std::size_t>(kMetricCount) * kHidden1 + kHidden1 +
         static_cast<std::size_t>(kHidden2) * kHidden1 + kHidden2 +
         static_cast<std::size_t>(kHeads) * kHidden2 + kHeads;
}

struct ForwardCache {
  Categories categories{};
  Engineered metrics{};
  std::array<float, kHidden1> pre1{};
  std::array<float, kHidden1> hidden1{};
  std::array<float, kHidden2> pre2{};
  std::array<float, kHidden2> hidden2{};
  std::array<float, kHeads> probabilities{};
};

struct RawPrediction {
  std::array<float, kHeads> probabilities{};
  double orientation_gap = 0;
};

class Network {
 public:
  explicit Network(std::uint32_t seed = 0x444e'5356u) { initialize(seed); }

  void setOutputPriors(const std::array<float, kHeads>& priors) {
    for (int head = 0; head < kHeads; ++head) {
      parameters.output_bias[head] = logit(priors[head]);
    }
  }

  std::array<float, kHeads> forwardOrientation(
      const PublicState& state, const Engineered& metrics,
      ForwardCache* cache = nullptr) const {
    ForwardCache local;
    ForwardCache& output = cache == nullptr ? local : *cache;
    output.categories = activeCategories(state);
    output.metrics = metrics;
    output.pre1 = parameters.bias1;
    const float category_scale =
        1.0f / static_cast<float>(std::sqrt(kActiveCategories));
    const float metric_scale =
        1.0f / static_cast<float>(std::sqrt(kMetricCount));
    for (int category : output.categories) {
      const int base = category * kHidden1;
      for (int hidden = 0; hidden < kHidden1; ++hidden) {
        output.pre1[hidden] +=
            category_scale * parameters.embedding[base + hidden];
      }
    }
    for (int metric = 0; metric < kMetricCount; ++metric) {
      const int base = metric * kHidden1;
      const float value = metric_scale * metrics[metric];
      for (int hidden = 0; hidden < kHidden1; ++hidden) {
        output.pre1[hidden] +=
            value * parameters.metric_weight[base + hidden];
      }
    }
    for (int hidden = 0; hidden < kHidden1; ++hidden) {
      output.hidden1[hidden] = leaky(output.pre1[hidden]);
    }
    output.pre2 = parameters.bias2;
    for (int next = 0; next < kHidden2; ++next) {
      const int base = next * kHidden1;
      for (int hidden = 0; hidden < kHidden1; ++hidden) {
        output.pre2[next] +=
            parameters.weight2[base + hidden] * output.hidden1[hidden];
      }
      output.hidden2[next] = leaky(output.pre2[next]);
    }
    for (int head = 0; head < kHeads; ++head) {
      float value = parameters.output_bias[head];
      const int base = head * kHidden2;
      for (int hidden = 0; hidden < kHidden2; ++hidden) {
        value += parameters.output_weight[base + hidden] *
                 output.hidden2[hidden];
      }
      output.probabilities[head] = sigmoid(value);
    }
    return output.probabilities;
  }

  RawPrediction predict(const PublicState& state,
                        const Engineered& metrics) const {
    const auto forward = forwardOrientation(state, metrics);
    const auto reflected = forwardOrientation(mirror(state), metrics);
    RawPrediction result;
    for (int head = 0; head < kHeads; ++head) {
      result.probabilities[head] =
          0.5f * (forward[head] + reflected[head]);
    }
    result.orientation_gap =
        std::abs(forward[0] - reflected[0]) * kContinuationCap;
    return result;
  }

  std::size_t parameterBytes() const {
    return parameterCount() * sizeof(float);
  }

  Parameters parameters;

 private:
  void initialize(std::uint32_t seed) {
    Mulberry32 random(seed);
    const auto fill = [&random](auto& values, float radius) {
      for (float& value : values) {
        value = static_cast<float>((2.0 * random.nextUnit() - 1.0) * radius);
      }
    };
    fill(parameters.embedding, 0.04f);
    fill(parameters.metric_weight, 0.04f);
    fill(parameters.weight2, 0.16f);
    fill(parameters.output_weight, 0.16f);
    parameters.output_bias = {{logit(0.50f), logit(0.50f), logit(0.20f)}};
  }
};

struct Gradient {
  Gradient()
      : embedding(static_cast<std::size_t>(kCategoryCount) * kHidden1),
        touched_marker(kCategoryCount) {}

  void touch(int category) {
    if (touched_marker[category]) return;
    touched_marker[category] = 1;
    touched.push_back(category);
  }

  void reset() {
    for (int category : touched) {
      std::fill_n(embedding.begin() + category * kHidden1, kHidden1, 0.0f);
      touched_marker[category] = 0;
    }
    touched.clear();
    metric_weight.fill(0);
    bias1.fill(0);
    weight2.fill(0);
    bias2.fill(0);
    output_weight.fill(0);
    output_bias.fill(0);
  }

  std::vector<float> embedding;
  std::vector<std::uint8_t> touched_marker;
  std::vector<int> touched;
  std::array<float, kMetricCount * kHidden1> metric_weight{};
  std::array<float, kHidden1> bias1{};
  std::array<float, kHidden2 * kHidden1> weight2{};
  std::array<float, kHidden2> bias2{};
  std::array<float, kHeads * kHidden2> output_weight{};
  std::array<float, kHeads> output_bias{};
};

float binaryCrossEntropy(float probability, float target) {
  const float clipped = std::clamp(probability, 1.0e-6f, 1.0f - 1.0e-6f);
  return -(target * std::log(clipped) +
           (1.0f - target) * std::log(1.0f - clipped));
}

float accumulateExample(const Network& network, const PublicState& state,
                        const Engineered& metrics,
                        const std::array<float, kHeads>& targets,
                        Gradient& gradient) {
  ForwardCache cache;
  network.forwardOrientation(state, metrics, &cache);
  std::array<float, kHeads> derivative_head{};
  float loss = 0;
  for (int head = 0; head < kHeads; ++head) {
    loss += binaryCrossEntropy(cache.probabilities[head], targets[head]);
    derivative_head[head] = cache.probabilities[head] - targets[head];
    gradient.output_bias[head] += derivative_head[head];
  }
  std::array<float, kHidden2> derivative2{};
  for (int head = 0; head < kHeads; ++head) {
    const int base = head * kHidden2;
    for (int hidden = 0; hidden < kHidden2; ++hidden) {
      gradient.output_weight[base + hidden] +=
          derivative_head[head] * cache.hidden2[hidden];
      derivative2[hidden] +=
          derivative_head[head] * network.parameters.output_weight[base + hidden];
    }
  }
  for (int hidden = 0; hidden < kHidden2; ++hidden) {
    derivative2[hidden] *= leakyDerivative(cache.pre2[hidden]);
    gradient.bias2[hidden] += derivative2[hidden];
  }
  std::array<float, kHidden1> derivative1{};
  for (int next = 0; next < kHidden2; ++next) {
    const int base = next * kHidden1;
    for (int hidden = 0; hidden < kHidden1; ++hidden) {
      gradient.weight2[base + hidden] +=
          derivative2[next] * cache.hidden1[hidden];
      derivative1[hidden] +=
          derivative2[next] * network.parameters.weight2[base + hidden];
    }
  }
  const float category_scale =
      1.0f / static_cast<float>(std::sqrt(kActiveCategories));
  const float metric_scale =
      1.0f / static_cast<float>(std::sqrt(kMetricCount));
  for (int hidden = 0; hidden < kHidden1; ++hidden) {
    derivative1[hidden] *= leakyDerivative(cache.pre1[hidden]);
    gradient.bias1[hidden] += derivative1[hidden];
  }
  for (int category : cache.categories) {
    gradient.touch(category);
    const int base = category * kHidden1;
    for (int hidden = 0; hidden < kHidden1; ++hidden) {
      gradient.embedding[base + hidden] +=
          category_scale * derivative1[hidden];
    }
  }
  for (int metric = 0; metric < kMetricCount; ++metric) {
    const int base = metric * kHidden1;
    const float value = metric_scale * metrics[metric];
    for (int hidden = 0; hidden < kHidden1; ++hidden) {
      gradient.metric_weight[base + hidden] += value * derivative1[hidden];
    }
  }
  return loss;
}

struct AdamMoments {
  AdamMoments()
      : embedding_m(static_cast<std::size_t>(kCategoryCount) * kHidden1),
        embedding_v(embedding_m.size()) {}
  std::vector<float> embedding_m;
  std::vector<float> embedding_v;
  std::array<float, kMetricCount * kHidden1> metric_m{}, metric_v{};
  std::array<float, kHidden1> bias1_m{}, bias1_v{};
  std::array<float, kHidden2 * kHidden1> weight2_m{}, weight2_v{};
  std::array<float, kHidden2> bias2_m{}, bias2_v{};
  std::array<float, kHeads * kHidden2> output_m{}, output_v{};
  std::array<float, kHeads> output_bias_m{}, output_bias_v{};
  std::uint64_t steps = 0;
};

void adamScalar(float& parameter, float gradient, float& first, float& second,
                float correction1, float correction2, bool decay) {
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
               std::array<float, Size>& second, float gradient_scale,
               float correction1, float correction2, bool decay) {
  for (std::size_t index = 0; index < Size; ++index) {
    adamScalar(parameters[index], gradients[index] * gradient_scale,
               first[index], second[index], correction1, correction2, decay);
  }
}

void applyAdam(Network& network, Gradient& gradient, AdamMoments& moments,
               int examples) {
  if (examples < 1) throw std::invalid_argument("empty denoised batch");
  ++moments.steps;
  const float correction1 =
      1.0f - static_cast<float>(std::pow(0.9, moments.steps));
  const float correction2 =
      1.0f - static_cast<float>(std::pow(0.999, moments.steps));
  const float scale = 1.0f / examples;
  for (int category : gradient.touched) {
    const int base = category * kHidden1;
    for (int hidden = 0; hidden < kHidden1; ++hidden) {
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
  adamArray(network.parameters.bias1, gradient.bias1, moments.bias1_m,
            moments.bias1_v, scale, correction1, correction2, false);
  adamArray(network.parameters.weight2, gradient.weight2, moments.weight2_m,
            moments.weight2_v, scale, correction1, correction2, true);
  adamArray(network.parameters.bias2, gradient.bias2, moments.bias2_m,
            moments.bias2_v, scale, correction1, correction2, false);
  adamArray(network.parameters.output_weight, gradient.output_weight,
            moments.output_m, moments.output_v, scale, correction1,
            correction2, true);
  adamArray(network.parameters.output_bias, gradient.output_bias,
            moments.output_bias_m, moments.output_bias_v, scale, correction1,
            correction2, false);
  gradient.reset();
}

std::array<float, kHeads> targetsFor(const Target& target) {
  return {{target.expected_lifetime / kContinuationCap, target.survival_25,
           target.survival_50}};
}

double train(Network& network, const std::vector<Example>& examples) {
  if (examples.empty()) throw std::invalid_argument("empty denoised training set");
  std::array<double, kHeads> means{};
  for (const Example& example : examples) {
    const auto targets = targetsFor(example.target);
    for (int head = 0; head < kHeads; ++head) means[head] += targets[head];
  }
  std::array<float, kHeads> priors{};
  for (int head = 0; head < kHeads; ++head) {
    priors[head] = static_cast<float>(std::clamp(
        means[head] / examples.size(), 1.0e-4, 1.0 - 1.0e-4));
  }
  network.setOutputPriors(priors);
  std::vector<std::size_t> order(examples.size());
  std::iota(order.begin(), order.end(), 0);
  Gradient gradient;
  AdamMoments moments;
  double final_loss = 0;
  for (int epoch = 0; epoch < kEpochs; ++epoch) {
    Mulberry32 shuffle(mix32(0x444e'5452u ^
                            static_cast<std::uint32_t>(epoch + 1)));
    for (std::size_t cursor = order.size(); cursor > 1; --cursor) {
      const std::size_t target = static_cast<std::size_t>(
          (static_cast<std::uint64_t>(shuffle.nextBits()) * cursor) >> 32);
      std::swap(order[cursor - 1], order[target]);
    }
    double epoch_loss = 0;
    std::size_t augmented_examples = 0;
    for (std::size_t start = 0; start < order.size(); start += kBatchSize) {
      const std::size_t end = std::min(order.size(), start + kBatchSize);
      int batch_examples = 0;
      for (std::size_t offset = start; offset < end; ++offset) {
        const Example& example = examples[order[offset]];
        const auto targets = targetsFor(example.target);
        epoch_loss += accumulateExample(network, example.target.state,
                                        example.metrics, targets, gradient);
        epoch_loss += accumulateExample(network, mirror(example.target.state),
                                        example.metrics, targets, gradient);
        batch_examples += 2;
      }
      applyAdam(network, gradient, moments, batch_examples);
      augmented_examples += static_cast<std::size_t>(batch_examples);
    }
    final_loss = epoch_loss /
                 std::max<std::size_t>(1, augmented_examples);
  }
  return final_loss;
}

struct Calibrator {
  float lifetime_slope = 1;
  float lifetime_intercept = 0;
  std::array<float, 2> survival_slope{{1, 1}};
  std::array<float, 2> survival_intercept{{0, 0}};
};

struct ModelBundle {
  Network network{};
  Normalizer normalizer{};
  Calibrator calibrator{};
};

constexpr std::array<std::uint8_t, 8> kCheckpointMagic{{
    'D', '7', 'D', 'N', 'V', '0', '0', '1',
}};
constexpr std::uint32_t kCheckpointVersion = 1;
constexpr std::size_t kCheckpointHeaderBytes =
    kCheckpointMagic.size() + 10 * sizeof(std::uint32_t);
constexpr std::size_t kCheckpointPayloadBytes =
    (parameterCount() + 2 * kMetricCount + 6) * sizeof(float);

void appendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes,
                      std::size_t& offset) {
  if (offset + 4 > bytes.size()) {
    throw std::runtime_error("truncated denoised checkpoint integer");
  }
  std::uint32_t result = 0;
  for (int shift = 0; shift < 32; shift += 8) {
    result |= static_cast<std::uint32_t>(bytes[offset++]) << shift;
  }
  return result;
}

void appendFloat(std::vector<std::uint8_t>& bytes, float value) {
  appendU32(bytes, std::bit_cast<std::uint32_t>(value));
}

float readFloat(const std::vector<std::uint8_t>& bytes,
                std::size_t& offset) {
  return std::bit_cast<float>(readU32(bytes, offset));
}

template <typename Container>
void appendFloats(std::vector<std::uint8_t>& bytes,
                  const Container& values) {
  for (float value : values) appendFloat(bytes, value);
}

template <typename Container>
void readFloats(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
                Container& values) {
  for (float& value : values) value = readFloat(bytes, offset);
}

std::uint32_t checkpointChecksum(const std::vector<std::uint8_t>& bytes,
                                 std::size_t begin) {
  std::uint32_t checksum = 0x811c'9dc5u;
  for (std::size_t index = begin; index < bytes.size(); ++index) {
    checksum ^= bytes[index];
    checksum *= 0x0100'0193u;
  }
  return checksum;
}

std::vector<std::uint8_t> serializeModel(const ModelBundle& model) {
  std::vector<std::uint8_t> payload;
  payload.reserve(kCheckpointPayloadBytes);
  appendFloats(payload, model.normalizer.mean);
  appendFloats(payload, model.normalizer.scale);
  appendFloat(payload, model.calibrator.lifetime_slope);
  appendFloat(payload, model.calibrator.lifetime_intercept);
  appendFloats(payload, model.calibrator.survival_slope);
  appendFloats(payload, model.calibrator.survival_intercept);
  appendFloats(payload, model.network.parameters.embedding);
  appendFloats(payload, model.network.parameters.metric_weight);
  appendFloats(payload, model.network.parameters.bias1);
  appendFloats(payload, model.network.parameters.weight2);
  appendFloats(payload, model.network.parameters.bias2);
  appendFloats(payload, model.network.parameters.output_weight);
  appendFloats(payload, model.network.parameters.output_bias);
  if (payload.size() != kCheckpointPayloadBytes) {
    throw std::logic_error("denoised checkpoint payload-size invariant failed");
  }

  std::vector<std::uint8_t> result;
  result.reserve(kCheckpointHeaderBytes + payload.size());
  result.insert(result.end(), kCheckpointMagic.begin(), kCheckpointMagic.end());
  appendU32(result, kCheckpointVersion);
  appendU32(result, kCategoryCount);
  appendU32(result, kMetricCount);
  appendU32(result, kHidden1);
  appendU32(result, kHidden2);
  appendU32(result, kHeads);
  appendU32(result, kContinuationCap);
  appendU32(result, static_cast<std::uint32_t>(parameterCount()));
  appendU32(result, static_cast<std::uint32_t>(payload.size()));
  appendU32(result, checkpointChecksum(payload, 0));
  result.insert(result.end(), payload.begin(), payload.end());
  return result;
}

ModelBundle deserializeModel(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < kCheckpointHeaderBytes ||
      !std::equal(kCheckpointMagic.begin(), kCheckpointMagic.end(),
                  bytes.begin())) {
    throw std::runtime_error("denoised checkpoint magic mismatch");
  }
  std::size_t offset = kCheckpointMagic.size();
  const std::uint32_t version = readU32(bytes, offset);
  const std::uint32_t categories = readU32(bytes, offset);
  const std::uint32_t metrics = readU32(bytes, offset);
  const std::uint32_t hidden1 = readU32(bytes, offset);
  const std::uint32_t hidden2 = readU32(bytes, offset);
  const std::uint32_t heads = readU32(bytes, offset);
  const std::uint32_t cap = readU32(bytes, offset);
  const std::uint32_t parameters = readU32(bytes, offset);
  const std::uint32_t payload_bytes = readU32(bytes, offset);
  const std::uint32_t expected_checksum = readU32(bytes, offset);
  if (version != kCheckpointVersion || categories != kCategoryCount ||
      metrics != kMetricCount || hidden1 != kHidden1 || hidden2 != kHidden2 ||
      heads != kHeads || cap != kContinuationCap ||
      parameters != parameterCount() ||
      payload_bytes != kCheckpointPayloadBytes ||
      offset != kCheckpointHeaderBytes ||
      bytes.size() != kCheckpointHeaderBytes + payload_bytes) {
    throw std::runtime_error("denoised checkpoint metadata mismatch");
  }
  if (checkpointChecksum(bytes, kCheckpointHeaderBytes) !=
      expected_checksum) {
    throw std::runtime_error("denoised checkpoint checksum mismatch");
  }

  ModelBundle result;
  readFloats(bytes, offset, result.normalizer.mean);
  readFloats(bytes, offset, result.normalizer.scale);
  result.calibrator.lifetime_slope = readFloat(bytes, offset);
  result.calibrator.lifetime_intercept = readFloat(bytes, offset);
  readFloats(bytes, offset, result.calibrator.survival_slope);
  readFloats(bytes, offset, result.calibrator.survival_intercept);
  readFloats(bytes, offset, result.network.parameters.embedding);
  readFloats(bytes, offset, result.network.parameters.metric_weight);
  readFloats(bytes, offset, result.network.parameters.bias1);
  readFloats(bytes, offset, result.network.parameters.weight2);
  readFloats(bytes, offset, result.network.parameters.bias2);
  readFloats(bytes, offset, result.network.parameters.output_weight);
  readFloats(bytes, offset, result.network.parameters.output_bias);
  if (offset != bytes.size()) {
    throw std::runtime_error("denoised checkpoint has trailing payload");
  }
  return result;
}

void saveModel(const std::string& path, const ModelBundle& model) {
  const std::vector<std::uint8_t> bytes = serializeModel(model);
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("could not open denoised checkpoint");
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output) throw std::runtime_error("could not write denoised checkpoint");
}

std::vector<std::uint8_t> readCheckpointBytes(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not open denoised checkpoint");
  input.seekg(0, std::ios::end);
  const std::streamoff length = input.tellg();
  if (length < 0) throw std::runtime_error("could not size denoised checkpoint");
  input.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
  input.read(reinterpret_cast<char*>(bytes.data()), length);
  if (!input) throw std::runtime_error("could not read denoised checkpoint");
  return bytes;
}

ModelBundle loadModel(const std::string& path) {
  return deserializeModel(readCheckpointBytes(path));
}

std::pair<float, float> fitLogisticCalibration(
    const std::vector<float>& probabilities,
    const std::vector<float>& targets) {
  if (probabilities.size() != targets.size() || probabilities.empty()) {
    throw std::invalid_argument("invalid soft calibration inputs");
  }
  double slope = 1;
  double intercept = 0;
  for (int iteration = 0; iteration < 25; ++iteration) {
    double gradient_slope = 0;
    double gradient_intercept = 0;
    double hessian_ss = 1.0e-3;
    double hessian_si = 0;
    double hessian_ii = 1.0e-3;
    for (std::size_t index = 0; index < probabilities.size(); ++index) {
      const double input = logit(probabilities[index]);
      const double prediction =
          sigmoid(static_cast<float>(slope * input + intercept));
      const double residual = prediction - targets[index];
      const double weight = prediction * (1.0 - prediction);
      gradient_slope += residual * input;
      gradient_intercept += residual;
      hessian_ss += weight * input * input;
      hessian_si += weight * input;
      hessian_ii += weight;
    }
    const double determinant =
        hessian_ss * hessian_ii - hessian_si * hessian_si;
    if (determinant <= 1.0e-12) break;
    const double delta_slope =
        (hessian_ii * gradient_slope -
         hessian_si * gradient_intercept) /
        determinant;
    const double delta_intercept =
        (-hessian_si * gradient_slope +
         hessian_ss * gradient_intercept) /
        determinant;
    slope = std::clamp(slope - delta_slope, 0.05, 10.0);
    intercept = std::clamp(intercept - delta_intercept, -10.0, 10.0);
    if (std::abs(delta_slope) + std::abs(delta_intercept) < 1.0e-7) break;
  }
  return {static_cast<float>(slope), static_cast<float>(intercept)};
}

Calibrator fitCalibrator(const Network& network,
                         const std::vector<Example>& examples) {
  std::vector<double> lifetime_predictions;
  std::vector<double> lifetime_targets;
  std::array<std::vector<float>, 2> survival_predictions;
  std::array<std::vector<float>, 2> survival_targets;
  for (const Example& example : examples) {
    const RawPrediction raw =
        network.predict(example.target.state, example.metrics);
    lifetime_predictions.push_back(raw.probabilities[0] * kContinuationCap);
    lifetime_targets.push_back(example.target.expected_lifetime);
    survival_predictions[0].push_back(raw.probabilities[1]);
    survival_predictions[1].push_back(raw.probabilities[2]);
    survival_targets[0].push_back(example.target.survival_25);
    survival_targets[1].push_back(example.target.survival_50);
  }
  const double mean_prediction =
      std::accumulate(lifetime_predictions.begin(),
                      lifetime_predictions.end(), 0.0) /
      lifetime_predictions.size();
  const double mean_target =
      std::accumulate(lifetime_targets.begin(), lifetime_targets.end(), 0.0) /
      lifetime_targets.size();
  double variance = 0;
  double covariance = 0;
  for (std::size_t index = 0; index < lifetime_predictions.size(); ++index) {
    const double centered = lifetime_predictions[index] - mean_prediction;
    variance += centered * centered;
    covariance += centered * (lifetime_targets[index] - mean_target);
  }
  Calibrator result;
  result.lifetime_slope = static_cast<float>(std::clamp(
      variance > 1.0e-9 ? covariance / variance : 1.0, 0.05, 10.0));
  result.lifetime_intercept = static_cast<float>(std::clamp(
      mean_target - result.lifetime_slope * mean_prediction, -50.0, 50.0));
  for (int head = 0; head < 2; ++head) {
    const auto [slope, intercept] = fitLogisticCalibration(
        survival_predictions[head], survival_targets[head]);
    result.survival_slope[head] = slope;
    result.survival_intercept[head] = intercept;
  }
  return result;
}

struct Prediction {
  double lifetime = 0;
  double survival_25 = 0;
  double survival_50 = 0;
  double orientation_gap = 0;
};

Prediction calibratedPrediction(const Network& network,
                                 const Calibrator& calibrator,
                                 const PublicState& state,
                                 const Engineered& metrics) {
  const RawPrediction raw = network.predict(state, metrics);
  const double raw_lifetime = raw.probabilities[0] * kContinuationCap;
  return {
      std::clamp(calibrator.lifetime_slope * raw_lifetime +
                     calibrator.lifetime_intercept,
                 0.0, static_cast<double>(kContinuationCap)),
      sigmoid(calibrator.survival_slope[0] * logit(raw.probabilities[1]) +
              calibrator.survival_intercept[0]),
      sigmoid(calibrator.survival_slope[1] * logit(raw.probabilities[2]) +
              calibrator.survival_intercept[1]),
      raw.orientation_gap,
  };
}

Prediction predict(const Network& network, const Normalizer& normalizer,
                   const Calibrator& calibrator, const PublicState& state) {
  return calibratedPrediction(network, calibrator, state,
                              normalizer.apply(rawEngineered(state)));
}

std::vector<double> ranks(const std::vector<double>& values) {
  std::vector<std::size_t> order(values.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](std::size_t first,
                                             std::size_t second) {
    return values[first] < values[second];
  });
  std::vector<double> result(values.size());
  std::size_t cursor = 0;
  while (cursor < order.size()) {
    std::size_t end = cursor + 1;
    while (end < order.size() && values[order[end]] == values[order[cursor]]) {
      ++end;
    }
    const double rank = 0.5 * static_cast<double>(cursor + end - 1);
    for (std::size_t index = cursor; index < end; ++index) {
      result[order[index]] = rank;
    }
    cursor = end;
  }
  return result;
}

double correlation(const std::vector<double>& first,
                   const std::vector<double>& second) {
  if (first.size() != second.size() || first.empty()) {
    throw std::invalid_argument("invalid correlation inputs");
  }
  const double first_mean =
      std::accumulate(first.begin(), first.end(), 0.0) / first.size();
  const double second_mean =
      std::accumulate(second.begin(), second.end(), 0.0) / second.size();
  double covariance = 0;
  double first_variance = 0;
  double second_variance = 0;
  for (std::size_t index = 0; index < first.size(); ++index) {
    const double first_delta = first[index] - first_mean;
    const double second_delta = second[index] - second_mean;
    covariance += first_delta * second_delta;
    first_variance += first_delta * first_delta;
    second_variance += second_delta * second_delta;
  }
  const double denominator = std::sqrt(first_variance * second_variance);
  return denominator == 0 ? 0 : covariance / denominator;
}

double spearman(const std::vector<double>& first,
                const std::vector<double>& second) {
  return correlation(ranks(first), ranks(second));
}

double softAuc(const std::vector<double>& predictions,
               const std::vector<double>& probabilities) {
  if (predictions.size() != probabilities.size() || predictions.empty()) {
    throw std::invalid_argument("invalid soft AUC inputs");
  }
  double favorable = 0;
  double pairs = 0;
  for (std::size_t positive = 0; positive < predictions.size(); ++positive) {
    for (std::size_t negative = 0; negative < predictions.size(); ++negative) {
      const double weight = probabilities[positive] *
                            (1.0 - probabilities[negative]);
      pairs += weight;
      if (predictions[positive] > predictions[negative]) favorable += weight;
      else if (predictions[positive] == predictions[negative]) {
        favorable += 0.5 * weight;
      }
    }
  }
  return pairs == 0 ? 0.5 : favorable / pairs;
}

double softCalibrationError(const std::vector<double>& predictions,
                            const std::vector<double>& targets) {
  constexpr int bins = 10;
  std::array<double, bins> prediction_sum{};
  std::array<double, bins> target_sum{};
  std::array<int, bins> counts{};
  for (std::size_t index = 0; index < predictions.size(); ++index) {
    const int bin = std::min(
        bins - 1, static_cast<int>(std::floor(predictions[index] * bins)));
    prediction_sum[bin] += predictions[index];
    target_sum[bin] += targets[index];
    ++counts[bin];
  }
  double error = 0;
  for (int bin = 0; bin < bins; ++bin) {
    if (counts[bin] == 0) continue;
    error += static_cast<double>(counts[bin]) / predictions.size() *
             std::abs(prediction_sum[bin] / counts[bin] -
                      target_sum[bin] / counts[bin]);
  }
  return error;
}

struct PredictionMetrics {
  int examples = 0;
  double mean_lifetime_target = 0;
  double mean_lifetime_prediction = 0;
  double lifetime_mae = 0;
  double lifetime_rmse = 0;
  double rank_correlation = 0;
  double survival_25_auc = 0;
  double survival_50_auc = 0;
  double survival_25_brier = 0;
  double survival_50_brier = 0;
  double survival_25_ece = 0;
  double survival_50_ece = 0;
  double mean_orientation_gap = 0;
};

PredictionMetrics evaluatePredictions(const std::vector<Example>& examples,
                                      const Network& network,
                                      const Calibrator& calibrator) {
  if (examples.empty()) throw std::invalid_argument("empty prediction set");
  std::vector<double> lifetime_predictions;
  std::vector<double> lifetime_targets;
  std::vector<double> survival_25_predictions;
  std::vector<double> survival_50_predictions;
  std::vector<double> survival_25_targets;
  std::vector<double> survival_50_targets;
  PredictionMetrics result;
  result.examples = static_cast<int>(examples.size());
  for (const Example& example : examples) {
    const Prediction prediction = calibratedPrediction(
        network, calibrator, example.target.state, example.metrics);
    lifetime_predictions.push_back(prediction.lifetime);
    lifetime_targets.push_back(example.target.expected_lifetime);
    survival_25_predictions.push_back(prediction.survival_25);
    survival_50_predictions.push_back(prediction.survival_50);
    survival_25_targets.push_back(example.target.survival_25);
    survival_50_targets.push_back(example.target.survival_50);
    const double lifetime_error =
        prediction.lifetime - example.target.expected_lifetime;
    result.mean_lifetime_target += example.target.expected_lifetime;
    result.mean_lifetime_prediction += prediction.lifetime;
    result.lifetime_mae += std::abs(lifetime_error);
    result.lifetime_rmse += lifetime_error * lifetime_error;
    result.survival_25_brier +=
        std::pow(prediction.survival_25 - example.target.survival_25, 2.0);
    result.survival_50_brier +=
        std::pow(prediction.survival_50 - example.target.survival_50, 2.0);
    result.mean_orientation_gap += prediction.orientation_gap;
  }
  const double count = examples.size();
  result.mean_lifetime_target /= count;
  result.mean_lifetime_prediction /= count;
  result.lifetime_mae /= count;
  result.lifetime_rmse = std::sqrt(result.lifetime_rmse / count);
  result.survival_25_brier /= count;
  result.survival_50_brier /= count;
  result.mean_orientation_gap /= count;
  result.rank_correlation =
      spearman(lifetime_predictions, lifetime_targets);
  result.survival_25_auc =
      softAuc(survival_25_predictions, survival_25_targets);
  result.survival_50_auc =
      softAuc(survival_50_predictions, survival_50_targets);
  result.survival_25_ece =
      softCalibrationError(survival_25_predictions, survival_25_targets);
  result.survival_50_ece =
      softCalibrationError(survival_50_predictions, survival_50_targets);
  return result;
}

void printPredictionMetrics(std::string_view tag,
                            const PredictionMetrics& metrics) {
  std::cout << std::fixed << std::setprecision(6) << tag
            << " {\"examples\":" << metrics.examples
            << ",\"meanLifetimeTarget\":" << metrics.mean_lifetime_target
            << ",\"meanLifetimePrediction\":"
            << metrics.mean_lifetime_prediction
            << ",\"lifetimeMae\":" << metrics.lifetime_mae
            << ",\"lifetimeRmse\":" << metrics.lifetime_rmse
            << ",\"rankCorrelation\":" << metrics.rank_correlation
            << ",\"survival25Auc\":" << metrics.survival_25_auc
            << ",\"survival50Auc\":" << metrics.survival_50_auc
            << ",\"survival25Brier\":" << metrics.survival_25_brier
            << ",\"survival50Brier\":" << metrics.survival_50_brier
            << ",\"survival25Ece\":" << metrics.survival_25_ece
            << ",\"survival50Ece\":" << metrics.survival_50_ece
            << ",\"meanOrientationGap\":"
            << metrics.mean_orientation_gap << "}\n";
}

struct UncertaintyMetrics {
  double mean_lifetime_se = 0;
  double p90_lifetime_se = 0;
  double mean_survival_25_se = 0;
  double mean_survival_50_se = 0;
  std::uint64_t simulated_moves = 0;
  std::uint64_t policy_work = 0;
};

UncertaintyMetrics uncertainty(const std::vector<Target>& targets) {
  if (targets.empty()) throw std::invalid_argument("empty uncertainty set");
  UncertaintyMetrics result;
  std::vector<double> lifetime_standard_errors;
  lifetime_standard_errors.reserve(targets.size());
  for (const Target& target : targets) {
    result.mean_lifetime_se += target.lifetime_standard_error;
    result.mean_survival_25_se += target.survival_25_standard_error;
    result.mean_survival_50_se += target.survival_50_standard_error;
    result.simulated_moves += target.simulated_moves;
    result.policy_work += target.policy_work;
    lifetime_standard_errors.push_back(target.lifetime_standard_error);
  }
  result.mean_lifetime_se /= targets.size();
  result.mean_survival_25_se /= targets.size();
  result.mean_survival_50_se /= targets.size();
  std::sort(lifetime_standard_errors.begin(), lifetime_standard_errors.end());
  const std::size_t p90 = static_cast<std::size_t>(
      std::floor(0.90 * static_cast<double>(targets.size() - 1)));
  result.p90_lifetime_se = lifetime_standard_errors[p90];
  return result;
}

void printUncertainty(std::string_view tag, int states,
                      const UncertaintyMetrics& metrics) {
  std::cout << std::fixed << std::setprecision(6) << tag
            << " {\"states\":" << states
            << ",\"continuationsPerState\":" << kContinuations
            << ",\"labelRollouts\":"
            << static_cast<std::uint64_t>(states) * kContinuations
            << ",\"meanLifetimeStandardError\":"
            << metrics.mean_lifetime_se
            << ",\"p90LifetimeStandardError\":"
            << metrics.p90_lifetime_se
            << ",\"meanSurvival25StandardError\":"
            << metrics.mean_survival_25_se
            << ",\"meanSurvival50StandardError\":"
            << metrics.mean_survival_50_se
            << ",\"simulatedMoves\":" << metrics.simulated_moves
            << ",\"policyWork\":" << metrics.policy_work << "}\n";
}

struct SupportTracker {
  std::array<std::uint32_t, kCategoryCount> counts{};

  void observe(const PublicState& state) {
    for (int category : activeCategories(state)) ++counts[category];
    for (int category : activeCategories(mirror(state))) ++counts[category];
  }

  double support(const PublicState& state) const {
    std::array<std::uint32_t, kActiveCategories * 2> active{};
    int offset = 0;
    for (int category : activeCategories(state)) active[offset++] = counts[category];
    for (int category : activeCategories(mirror(state))) {
      active[offset++] = counts[category];
    }
    std::sort(active.begin(), active.end());
    return active[active.size() / 10];
  }
};

struct RootSuccessor {
  PublicState state{};
  bool terminal = false;
};

RootSuccessor rootSuccessor(const State& canonical, int action, int sample) {
  if (sample < 0 || sample >= kRootStrata ||
      !isLegal(canonical.board, action)) {
    throw std::invalid_argument("invalid denoised root successor");
  }
  const std::uint32_t seed = cfpi::detail::scenarioSeedForState(
      canonical, cfpi::BehaviorOptions{}.policy_seed, 1);
  cfpi::detail::StratifiedRandom random{seed, sample, kRootStrata, 0};
  MoveResult move;
  if (!cfpi::detail::playMoveSampled(canonical, action, random, move)) {
    throw std::runtime_error("denoised root transition failed");
  }
  if (!move.state.game_over) {
    move.state.next_disc =
        cfpi::detail::sampledNextDisc(seed, sample, kRootStrata);
  }
  return {publicState(move.state), move.state.game_over};
}

struct ActionEstimate {
  std::array<double, kRootStrata> values{};
  double support = 0;
  double orientation_gap = 0;
};

ActionEstimate evaluateAction(const State& canonical, int action,
                              const Network& network,
                              const Normalizer& normalizer,
                              const Calibrator& calibrator,
                              const SupportTracker& support) {
  ActionEstimate result;
  int live = 0;
  for (int sample = 0; sample < kRootStrata; ++sample) {
    const RootSuccessor successor = rootSuccessor(canonical, action, sample);
    if (successor.terminal) {
      result.values[sample] = 1.0;
      continue;
    }
    const Prediction prediction =
        predict(network, normalizer, calibrator, successor.state);
    result.values[sample] = 1.0 + prediction.lifetime;
    result.support += support.support(successor.state);
    result.orientation_gap =
        std::max(result.orientation_gap, prediction.orientation_gap);
    ++live;
  }
  if (live > 0) result.support /= live;
  return result;
}

double rootLower(const ActionEstimate& candidate,
                 const ActionEstimate& behavior) {
  std::array<double, kRootStrata> differences{};
  double mean = 0;
  for (int sample = 0; sample < kRootStrata; ++sample) {
    differences[sample] = candidate.values[sample] - behavior.values[sample];
    mean += differences[sample] / kRootStrata;
  }
  double squares = 0;
  for (double difference : differences) {
    squares += (difference - mean) * (difference - mean);
  }
  const double deviation =
      std::sqrt(squares / static_cast<double>(kRootStrata - 1));
  return mean - 1.96 * deviation / std::sqrt(kRootStrata);
}

struct PolicyCounters {
  std::uint64_t modeled_transitions = 0;
  std::uint64_t teacher_work = 0;
  int switches = 0;
  int support_rejections = 0;
  int confidence_rejections = 0;
};

int chooseConservativeAction(const State& state, int behavior_action,
                             const Network& network,
                             const Normalizer& normalizer,
                             const Calibrator& calibrator,
                             const SupportTracker& support,
                             PolicyCounters& counters) {
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(state, mirrored);
  const int canonical_behavior =
      mirrored ? kBoardSize - 1 - behavior_action : behavior_action;
  const ActionEstimate behavior = evaluateAction(
      canonical, canonical_behavior, network, normalizer, calibrator, support);
  counters.modeled_transitions += kRootStrata;
  int selected = canonical_behavior;
  double best_lower = kSwitchMargin;
  for (int action : cfpi::detail::kColumnOrder) {
    if (action == canonical_behavior || !isLegal(canonical.board, action)) {
      continue;
    }
    const ActionEstimate candidate = evaluateAction(
        canonical, action, network, normalizer, calibrator, support);
    counters.modeled_transitions += kRootStrata;
    if (candidate.support < kMinimumSupport ||
        candidate.support < kSupportRatio * behavior.support) {
      ++counters.support_rejections;
      continue;
    }
    const double lower = rootLower(candidate, behavior);
    if (candidate.orientation_gap > kMaximumOrientationGap ||
        lower <= kSwitchMargin) {
      ++counters.confidence_rejections;
      continue;
    }
    if (lower > best_lower) {
      best_lower = lower;
      selected = action;
    }
  }
  if (selected != canonical_behavior) ++counters.switches;
  return mirrored ? kBoardSize - 1 - selected : selected;
}

struct PolicyGame {
  std::int64_t score = 0;
  int moves = 0;
  int clears = 0;
  int reveals = 0;
  PolicyCounters counters{};
};

PolicyGame runPolicyGame(std::uint32_t seed, const Network* network,
                         const Normalizer& normalizer,
                         const Calibrator& calibrator,
                         const SupportTracker& support) {
  State state = initialHeadlessState(seed);
  PolicyGame result;
  while (!state.game_over && state.moves_played < kGameMoveCap) {
    cfpi::BehaviorMetrics metrics;
    const int behavior_action =
        cfpi::chooseBehaviorAction(state, {}, &metrics);
    result.counters.teacher_work += metrics.work;
    const int action = network == nullptr
                           ? behavior_action
                           : chooseConservativeAction(
                                 state, behavior_action, *network, normalizer,
                                 calibrator, support, result.counters);
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("denoised policy selected illegal action");
    }
    for (const Wave& wave : move.waves) {
      result.clears += wave.cleared;
      result.reveals += wave.revealed;
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  return result;
}

struct PolicySummary {
  double mean_score = 0;
  double mean_moves = 0;
  double clear_rate = 0;
  double reveal_rate = 0;
  double mean_switches = 0;
  std::uint64_t modeled_transitions = 0;
  std::uint64_t teacher_work = 0;
  int support_rejections = 0;
  int confidence_rejections = 0;
  std::vector<PolicyGame> games;
};

PolicySummary summarize(std::vector<PolicyGame> games) {
  PolicySummary result;
  result.games = std::move(games);
  std::uint64_t moves = 0;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
  for (const PolicyGame& game : result.games) {
    result.mean_score += game.score;
    result.mean_moves += game.moves;
    result.mean_switches += game.counters.switches;
    result.modeled_transitions += game.counters.modeled_transitions;
    result.teacher_work += game.counters.teacher_work;
    result.support_rejections += game.counters.support_rejections;
    result.confidence_rejections += game.counters.confidence_rejections;
    moves += game.moves;
    clears += game.clears;
    reveals += game.reveals;
  }
  const double count = result.games.size();
  result.mean_score /= count;
  result.mean_moves /= count;
  result.mean_switches /= count;
  result.clear_rate = static_cast<double>(clears) /
                      std::max<std::uint64_t>(1, moves);
  result.reveal_rate = static_cast<double>(reveals) /
                       std::max<std::uint64_t>(1, moves);
  return result;
}

void printPolicySummary(std::string_view tag, const PolicySummary& summary) {
  std::cout << std::fixed << std::setprecision(3) << tag
            << " {\"games\":" << summary.games.size()
            << ",\"meanScore\":" << summary.mean_score
            << ",\"meanMoves\":" << summary.mean_moves
            << ",\"clearRate\":" << summary.clear_rate
            << ",\"revealRate\":" << summary.reveal_rate
            << ",\"meanSwitches\":" << summary.mean_switches
            << ",\"modeledTransitions\":" << summary.modeled_transitions
            << ",\"supportRejections\":" << summary.support_rejections
            << ",\"confidenceRejections\":"
            << summary.confidence_rejections
            << ",\"teacherWork\":" << summary.teacher_work << "}\n";
}

double pairedGameLower(const PolicySummary& behavior,
                       const PolicySummary& candidate, bool score) {
  if (behavior.games.size() != candidate.games.size() ||
      behavior.games.empty()) {
    throw std::invalid_argument("invalid paired policy summaries");
  }
  std::vector<double> differences;
  for (std::size_t index = 0; index < behavior.games.size(); ++index) {
    differences.push_back(
        score ? candidate.games[index].score - behavior.games[index].score
              : candidate.games[index].moves - behavior.games[index].moves);
  }
  const double mean =
      std::accumulate(differences.begin(), differences.end(), 0.0) /
      differences.size();
  if (differences.size() < 2) {
    return -std::numeric_limits<double>::infinity();
  }
  double squares = 0;
  for (double difference : differences) {
    squares += (difference - mean) * (difference - mean);
  }
  const double deviation =
      std::sqrt(squares / static_cast<double>(differences.size() - 1));
  return mean - 1.96 * deviation / std::sqrt(differences.size());
}

std::uint64_t peakResidentBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
#endif
}

int runExperiment() {
  const int threads = static_cast<int>(
      std::min(8u, std::max(1u, std::thread::hardware_concurrency())));
  const auto started = std::chrono::steady_clock::now();
  std::cout << "DENOISED_VALUE_CONFIG {\"rollinGames\":" << kRollinGames
            << ",\"trainingRollinGames\":" << kTrainingRollinGames
            << ",\"holdoutRollinGames\":" << kHoldoutRollinGames
            << ",\"holdoutRule\":\"index_mod_4_zero\""
            << ",\"sampleStride\":" << kSampleStride
            << ",\"maximumStatesPerGame\":" << kMaximumStatesPerGame
            << ",\"continuationsPerState\":" << kContinuations
            << ",\"continuationCap\":" << kContinuationCap
            << ",\"labelPolicy\":\"public_phase_greedy_d1_s1\""
            << ",\"independentHoldoutRandomness\":true"
            << ",\"hidden\":[" << kHidden1 << ',' << kHidden2 << ']'
            << ",\"epochs\":" << kEpochs
            << ",\"requiredSpearman\":" << kRequiredSpearman
            << ",\"requiredSurvivalAuc\":" << kRequiredSurvivalAuc
            << ",\"maximumSurvivalEce\":" << kMaximumSurvivalEce
            << ",\"switchMargin\":" << kSwitchMargin
            << ",\"rootStrata\":" << kRootStrata
            << ",\"rollinSeedStart\":" << kRollinStart
            << ",\"screenSeedStart\":" << kScreenStart
            << ",\"confirmationSeedStart\":" << kConfirmationStart
            << ",\"gameSeedRanges\":[\"0x3d\",\"0x3e\"]"
            << ",\"threads\":" << threads << "}\n";

  const std::vector<RollinGame> games = collectRollins(threads);
  std::vector<PublicState> training_states;
  std::vector<PublicState> holdout_candidates;
  std::unordered_set<std::string> training_keys;
  int training_games = 0;
  int holdout_games = 0;
  int censored = 0;
  double mean_score = 0;
  double mean_moves = 0;
  std::uint64_t teacher_work = 0;
  for (int index = 0; index < kRollinGames; ++index) {
    const bool holdout = index % 4 == 0;
    if (holdout) ++holdout_games;
    else ++training_games;
    censored += games[index].censored ? 1 : 0;
    mean_score += games[index].score;
    mean_moves += games[index].moves;
    teacher_work += games[index].teacher_work;
    for (const PublicState& source : games[index].states) {
      const PublicState state = canonicalize(source);
      if (holdout) {
        holdout_candidates.push_back(state);
      } else if (training_keys.insert(publicKey(state)).second) {
        training_states.push_back(state);
      }
    }
  }
  std::unordered_set<std::string> holdout_keys;
  std::vector<PublicState> holdout_states;
  int overlap_removed = 0;
  int duplicate_holdout_removed = 0;
  for (const PublicState& state : holdout_candidates) {
    const std::string key = publicKey(state);
    if (training_keys.contains(key)) {
      ++overlap_removed;
      continue;
    }
    if (!holdout_keys.insert(key).second) {
      ++duplicate_holdout_removed;
      continue;
    }
    holdout_states.push_back(state);
  }
  mean_score /= kRollinGames;
  mean_moves /= kRollinGames;
  std::cout << std::fixed << std::setprecision(3)
            << "DENOISED_VALUE_ROLLINS {\"games\":" << kRollinGames
            << ",\"trainingGames\":" << training_games
            << ",\"holdoutGames\":" << holdout_games
            << ",\"trainingStates\":" << training_states.size()
            << ",\"holdoutStates\":" << holdout_states.size()
            << ",\"overlapRemoved\":" << overlap_removed
            << ",\"duplicateHoldoutRemoved\":"
            << duplicate_holdout_removed
            << ",\"meanScore\":" << mean_score
            << ",\"meanMoves\":" << mean_moves
            << ",\"censored\":" << censored
            << ",\"teacherWork\":" << teacher_work << "}\n";
  if (training_games != kTrainingRollinGames ||
      holdout_games != kHoldoutRollinGames || censored != 0 ||
      training_states.empty() || holdout_states.empty()) {
    std::cout << "DENOISED_VALUE_RESULT {\"qualified\":false,"
                 "\"stoppedAt\":\"rollins\",\"peakResidentBytes\":"
              << peakResidentBytes() << "}\n";
    return 3;
  }

  const std::vector<Target> training_targets = labelParallel(
      training_states, kTrainingLabelDomain, threads);
  const std::vector<Target> holdout_targets = labelParallel(
      holdout_states, kHoldoutLabelDomain, threads);
  const UncertaintyMetrics training_uncertainty = uncertainty(training_targets);
  const UncertaintyMetrics holdout_uncertainty = uncertainty(holdout_targets);
  printUncertainty("DENOISED_VALUE_TRAIN_LABELS",
                   static_cast<int>(training_targets.size()),
                   training_uncertainty);
  printUncertainty("DENOISED_VALUE_HOLDOUT_LABELS",
                   static_cast<int>(holdout_targets.size()),
                   holdout_uncertainty);

  const Normalizer normalizer = fitNormalizer(training_targets);
  const std::vector<Example> training = prepare(training_targets, normalizer);
  const std::vector<Example> holdout = prepare(holdout_targets, normalizer);
  Network network;
  const double final_loss = train(network, training);
  const Calibrator calibrator = fitCalibrator(network, training);
  const PredictionMetrics training_metrics =
      evaluatePredictions(training, network, calibrator);
  const PredictionMetrics holdout_metrics =
      evaluatePredictions(holdout, network, calibrator);
  constexpr std::size_t metadata_bytes =
      (2 * kMetricCount + 6) * sizeof(float);
  std::cout << "DENOISED_VALUE_MODEL {\"parameters\":" << parameterCount()
            << ",\"parameterBytes\":" << network.parameterBytes()
            << ",\"metadataBytes\":" << metadata_bytes
            << ",\"modelBytes\":"
            << network.parameterBytes() + metadata_bytes
            << ",\"finalAugmentedLoss\":" << final_loss
            << ",\"lifetimeCalibration\":["
            << calibrator.lifetime_slope << ','
            << calibrator.lifetime_intercept
            << "],\"survival25Calibration\":["
            << calibrator.survival_slope[0] << ','
            << calibrator.survival_intercept[0]
            << "],\"survival50Calibration\":["
            << calibrator.survival_slope[1] << ','
            << calibrator.survival_intercept[1] << "]}\n";
  printPredictionMetrics("DENOISED_VALUE_TRAIN", training_metrics);
  printPredictionMetrics("DENOISED_VALUE_HOLDOUT", holdout_metrics);
  const bool prediction_gate =
      holdout_metrics.rank_correlation >= kRequiredSpearman &&
      holdout_metrics.survival_25_auc >= kRequiredSurvivalAuc &&
      holdout_metrics.survival_50_auc >= kRequiredSurvivalAuc &&
      holdout_metrics.survival_25_ece <= kMaximumSurvivalEce &&
      holdout_metrics.survival_50_ece <= kMaximumSurvivalEce;
  std::cout << "DENOISED_VALUE_GATE {\"predictionPassed\":"
            << (prediction_gate ? "true" : "false") << "}\n";
  if (!prediction_gate) {
    const double seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
    std::cout << "DENOISED_VALUE_RESULT {\"qualified\":false,"
                 "\"stoppedAt\":\"prediction\",\"seconds\":"
              << seconds << ",\"peakResidentBytes\":"
              << peakResidentBytes() << "}\n";
    return 3;
  }

  SupportTracker support;
  for (const Target& target : training_targets) support.observe(target.state);
  const auto evaluate_pair = [&](std::uint32_t seed_start, int count) {
    std::vector<PolicyGame> behavior_games;
    std::vector<PolicyGame> candidate_games;
    for (int game = 0; game < count; ++game) {
      const std::uint32_t seed = seed_start + static_cast<std::uint32_t>(game);
      behavior_games.push_back(runPolicyGame(
          seed, nullptr, normalizer, calibrator, support));
      candidate_games.push_back(runPolicyGame(
          seed, &network, normalizer, calibrator, support));
    }
    return std::pair{summarize(std::move(behavior_games)),
                     summarize(std::move(candidate_games))};
  };
  auto [screen_behavior, screen_candidate] = evaluate_pair(kScreenStart, 4);
  printPolicySummary("DENOISED_VALUE_SCREEN_BEHAVIOR", screen_behavior);
  printPolicySummary("DENOISED_VALUE_SCREEN_CANDIDATE", screen_candidate);
  const double screen_score_lower =
      pairedGameLower(screen_behavior, screen_candidate, true);
  const double screen_moves_lower =
      pairedGameLower(screen_behavior, screen_candidate, false);
  const bool screen_pass =
      screen_candidate.mean_score > screen_behavior.mean_score &&
      screen_candidate.mean_moves > screen_behavior.mean_moves;
  std::cout << "DENOISED_VALUE_SCREEN {\"passed\":"
            << (screen_pass ? "true" : "false")
            << ",\"scoreLower95\":" << screen_score_lower
            << ",\"movesLower95\":" << screen_moves_lower << "}\n";
  if (!screen_pass) {
    const double seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
    std::cout << "DENOISED_VALUE_RESULT {\"qualified\":false,"
                 "\"stoppedAt\":\"screen\",\"seconds\":"
              << seconds << ",\"peakResidentBytes\":"
              << peakResidentBytes() << "}\n";
    return 3;
  }

  auto [confirmation_behavior, confirmation_candidate] =
      evaluate_pair(kConfirmationStart, 8);
  printPolicySummary("DENOISED_VALUE_CONFIRM_BEHAVIOR",
                     confirmation_behavior);
  printPolicySummary("DENOISED_VALUE_CONFIRM_CANDIDATE",
                     confirmation_candidate);
  const double confirmation_score_lower =
      pairedGameLower(confirmation_behavior, confirmation_candidate, true);
  const double confirmation_moves_lower =
      pairedGameLower(confirmation_behavior, confirmation_candidate, false);
  const bool qualified =
      confirmation_candidate.mean_score > confirmation_behavior.mean_score &&
      confirmation_candidate.mean_moves > confirmation_behavior.mean_moves;
  const double seconds = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - started)
                             .count();
  std::cout << "DENOISED_VALUE_RESULT {\"qualified\":"
            << (qualified ? "true" : "false")
            << ",\"stoppedAt\":\"confirmation\",\"scoreLower95\":"
            << confirmation_score_lower << ",\"movesLower95\":"
            << confirmation_moves_lower << ",\"seconds\":" << seconds
            << ",\"peakResidentBytes\":" << peakResidentBytes() << "}\n";
  return qualified ? 0 : 3;
}

int trainCheckpoint(const std::string& path) {
  const int threads = static_cast<int>(
      std::min(8u, std::max(1u, std::thread::hardware_concurrency())));
  const auto started = std::chrono::steady_clock::now();
  std::cout << "DENOISED_CHECKPOINT_CONFIG {\"rollinSeedStart\":"
            << kRollinStart << ",\"rollinGames\":" << kRollinGames
            << ",\"trainingRollinGames\":" << kTrainingRollinGames
            << ",\"continuationsPerState\":" << kContinuations
            << ",\"continuationCap\":" << kContinuationCap
            << ",\"epochs\":" << kEpochs
            << ",\"output\":\"" << path << "\"}\n";
  const std::vector<RollinGame> games = collectRollins(threads);
  std::unordered_set<std::string> keys;
  std::vector<PublicState> states;
  int censored = 0;
  for (int index = 0; index < kRollinGames; ++index) {
    censored += games[index].censored ? 1 : 0;
    if (index % 4 == 0) continue;
    for (const PublicState& source : games[index].states) {
      const PublicState state = canonicalize(source);
      if (keys.insert(publicKey(state)).second) states.push_back(state);
    }
  }
  if (censored != 0 || states.empty()) {
    throw std::runtime_error("checkpoint corpus did not reproduce cleanly");
  }
  const std::vector<Target> targets =
      labelParallel(states, kTrainingLabelDomain, threads);
  ModelBundle model;
  model.normalizer = fitNormalizer(targets);
  const std::vector<Example> examples = prepare(targets, model.normalizer);
  const double loss = train(model.network, examples);
  model.calibrator = fitCalibrator(model.network, examples);
  const PredictionMetrics metrics =
      evaluatePredictions(examples, model.network, model.calibrator);
  saveModel(path, model);
  const std::vector<std::uint8_t> bytes = readCheckpointBytes(path);
  const ModelBundle loaded = deserializeModel(bytes);
  if (serializeModel(loaded) != bytes) {
    throw std::runtime_error("saved denoised checkpoint did not round trip");
  }
  const double seconds = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - started)
                             .count();
  std::cout << std::fixed << std::setprecision(6)
            << "DENOISED_CHECKPOINT_RESULT {\"states\":" << states.size()
            << ",\"labelRollouts\":"
            << static_cast<std::uint64_t>(states.size()) * kContinuations
            << ",\"finalAugmentedLoss\":" << loss
            << ",\"trainingMae\":" << metrics.lifetime_mae
            << ",\"trainingSpearman\":" << metrics.rank_correlation
            << ",\"parameters\":" << parameterCount()
            << ",\"checkpointBytes\":" << bytes.size()
            << ",\"payloadChecksum\":"
            << checkpointChecksum(bytes, kCheckpointHeaderBytes)
            << ",\"seconds\":" << seconds
            << ",\"peakResidentBytes\":" << peakResidentBytes()
            << ",\"output\":\"" << path << "\"}\n";
  return 0;
}

bool rejectsCheckpoint(std::vector<std::uint8_t> bytes,
                       std::size_t corrupt_offset) {
  if (corrupt_offset >= bytes.size()) return false;
  bytes[corrupt_offset] ^= 0x01u;
  try {
    static_cast<void>(deserializeModel(bytes));
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

bool selfTest(std::ostream& output) {
  const bool behavior = cfpi::selfTest(output);
  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(4, 4)] = kCracked;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;
  const PublicState observable = publicState(state);
  const PublicState reflected = mirror(observable);
  const Engineered raw = rawEngineered(observable);
  const Engineered reflected_raw = rawEngineered(reflected);
  double metric_difference = 0;
  for (int metric = 0; metric < kMetricCount; ++metric) {
    metric_difference = std::max(
        metric_difference,
        std::abs(static_cast<double>(raw[metric] - reflected_raw[metric])));
  }
  Normalizer normalizer;
  normalizer.scale.fill(1.0f);
  const Engineered normalized = normalizer.apply(raw);
  Network network;
  const RawPrediction prediction = network.predict(observable, normalized);
  const RawPrediction reflected_prediction =
      network.predict(reflected, normalized);
  double reflection_difference = 0;
  for (int head = 0; head < kHeads; ++head) {
    reflection_difference = std::max(
        reflection_difference,
        std::abs(static_cast<double>(prediction.probabilities[head] -
                                     reflected_prediction.probabilities[head])));
  }

  State irrelevant = state;
  irrelevant.score = 4'000'000;
  irrelevant.level = 99;
  irrelevant.moves_played = 417;
  const bool public_only =
      samePublicState(observable, publicState(irrelevant));
  const bool domains_independent =
      continuationSeed(observable, kTrainingLabelDomain, 0) !=
      continuationSeed(observable, kHoldoutLabelDomain, 0);

  Gradient gradient;
  const std::array<float, kHeads> targets{{0.55f, 0.6f, 0.2f}};
  accumulateExample(network, observable, normalized, targets, gradient);
  const float analytic = gradient.output_bias[0];
  constexpr float epsilon = 1.0e-3f;
  const float original_bias = network.parameters.output_bias[0];
  network.parameters.output_bias[0] = original_bias + epsilon;
  const auto plus = network.forwardOrientation(observable, normalized);
  network.parameters.output_bias[0] = original_bias - epsilon;
  const auto minus = network.forwardOrientation(observable, normalized);
  network.parameters.output_bias[0] = original_bias;
  const float numeric =
      (binaryCrossEntropy(plus[0], targets[0]) -
       binaryCrossEntropy(minus[0], targets[0])) /
      (2.0f * epsilon);
  const bool gradient_ok = std::abs(analytic - numeric) < 2.0e-3f;
  AdamMoments moments;
  const float before_update = network.parameters.output_bias[0];
  applyAdam(network, gradient, moments, 1);
  const bool learner_wired =
      network.parameters.output_bias[0] != before_update;

  PublicState terminal_probe;
  terminal_probe.board.fill(kSolid);
  terminal_probe.board[indexOf(0, 3)] = kEmpty;
  terminal_probe.next_disc = 7;
  terminal_probe.moves_remaining = 1;
  const Target terminal_label =
      labelState(terminal_probe, kTrainingLabelDomain);
  const Target mirrored_terminal_label =
      labelState(mirror(terminal_probe), kTrainingLabelDomain);
  const bool fair_label =
      terminal_label.expected_lifetime == 1.0f &&
      terminal_label.survival_25 == 0.0f &&
      terminal_label.survival_50 == 0.0f &&
      terminal_label.expected_lifetime ==
          mirrored_terminal_label.expected_lifetime;
  const bool key_safe = publicKey(observable) == publicKey(reflected);
  ModelBundle checkpoint;
  checkpoint.network = network;
  checkpoint.normalizer = normalizer;
  checkpoint.normalizer.mean[0] = 1.25f;
  checkpoint.normalizer.scale[0] = 2.5f;
  checkpoint.calibrator.lifetime_slope = 1.125f;
  checkpoint.calibrator.lifetime_intercept = -0.75f;
  checkpoint.calibrator.survival_slope = {{1.25f, 0.875f}};
  checkpoint.calibrator.survival_intercept = {{-0.25f, 0.5f}};
  const std::vector<std::uint8_t> checkpoint_bytes =
      serializeModel(checkpoint);
  constexpr std::string_view checkpoint_path =
      "/tmp/drop7-denoised-value-self-test.bin";
  saveModel(std::string(checkpoint_path), checkpoint);
  const ModelBundle loaded = loadModel(std::string(checkpoint_path));
  const std::vector<std::uint8_t> loaded_bytes = serializeModel(loaded);
  const bool checkpoint_round_trip = checkpoint_bytes == loaded_bytes;
  const bool checkpoint_metadata =
      loaded.normalizer.mean[0] == checkpoint.normalizer.mean[0] &&
      loaded.normalizer.scale[0] == checkpoint.normalizer.scale[0] &&
      loaded.calibrator.lifetime_slope ==
          checkpoint.calibrator.lifetime_slope &&
      loaded.calibrator.survival_intercept ==
          checkpoint.calibrator.survival_intercept &&
      checkpoint_bytes.size() ==
          kCheckpointHeaderBytes + kCheckpointPayloadBytes;
  const bool checkpoint_corruption =
      rejectsCheckpoint(checkpoint_bytes, checkpoint_bytes.size() - 1) &&
      rejectsCheckpoint(checkpoint_bytes,
                        kCheckpointMagic.size() + sizeof(std::uint32_t));
  std::vector<std::uint8_t> truncated = checkpoint_bytes;
  truncated.pop_back();
  bool checkpoint_truncation = false;
  try {
    static_cast<void>(deserializeModel(truncated));
  } catch (const std::runtime_error&) {
    checkpoint_truncation = true;
  }
  const bool gates = kRequiredSpearman == 0.70 &&
                     kRequiredSurvivalAuc == 0.80 &&
                     kMaximumSurvivalEce == 0.10;
  const bool size = parameterCount() == network.parameterBytes() / sizeof(float);
  const bool passed = behavior && metric_difference <= 1.0e-5 &&
                      reflection_difference == 0 && public_only &&
                      domains_independent && gradient_ok && learner_wired &&
                      fair_label && key_safe && checkpoint_round_trip &&
                      checkpoint_metadata && checkpoint_corruption &&
                      checkpoint_truncation && gates && size;
  output << "DENOISED_VALUE_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"publicOnly\":" << (public_only ? "true" : "false")
         << ",\"reflectionInvariant\":"
         << (reflection_difference == 0 ? "true" : "false")
         << ",\"fairPublicLabel\":" << (fair_label ? "true" : "false")
         << ",\"independentDomains\":"
         << (domains_independent ? "true" : "false")
         << ",\"engineeredMirrorDifference\":" << metric_difference
         << ",\"gradientError\":" << std::abs(analytic - numeric)
         << ",\"learnerWired\":"
         << (learner_wired ? "true" : "false")
         << ",\"checkpointRoundTrip\":"
         << (checkpoint_round_trip ? "true" : "false")
         << ",\"checkpointMetadata\":"
         << (checkpoint_metadata ? "true" : "false")
         << ",\"checkpointCorruptionRejected\":"
         << (checkpoint_corruption ? "true" : "false")
         << ",\"checkpointTruncationRejected\":"
         << (checkpoint_truncation ? "true" : "false")
         << ",\"checkpointBytes\":" << checkpoint_bytes.size()
         << ",\"continuations\":" << kContinuations
         << ",\"parameters\":" << parameterCount()
         << ",\"gateSpearman\":" << kRequiredSpearman
         << ",\"gateSurvivalAuc\":" << kRequiredSurvivalAuc
         << ",\"gateMaximumEce\":" << kMaximumSurvivalEce << "}\n";
  return passed;
}

}  // namespace drop7::denoised_stochastic_value

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::denoised_stochastic_value::selfTest(std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--run") {
      return drop7::denoised_stochastic_value::runExperiment();
    }
    if (argc == 3 &&
        std::string_view(argv[1]) == "--train-checkpoint") {
      return drop7::denoised_stochastic_value::trainCheckpoint(argv[2]);
    }
    if (argc == 3 &&
        std::string_view(argv[1]) == "--verify-checkpoint") {
      const auto bytes =
          drop7::denoised_stochastic_value::readCheckpointBytes(argv[2]);
      static_cast<void>(
          drop7::denoised_stochastic_value::deserializeModel(bytes));
      std::cout << "DENOISED_CHECKPOINT_VERIFY {\"valid\":true,"
                   "\"checkpointBytes\":"
                << bytes.size() << ",\"payloadChecksum\":"
                << drop7::denoised_stochastic_value::checkpointChecksum(
                       bytes,
                       drop7::denoised_stochastic_value::
                           kCheckpointHeaderBytes)
                << "}\n";
      return 0;
    }
    std::cerr << "Usage: drop7_denoised_stochastic_value --self-test | --run "
                 "| --train-checkpoint PATH | --verify-checkpoint PATH\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
