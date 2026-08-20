#include "../../../src/core/native/public-behavior.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
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
#include <utility>
#include <vector>

// Trains a bounded supervised value model with explicit position/token
// embeddings and a small dense phase summary.  It sees only the public board,
// next disc, and moves-until-rise. Reflection
// augmentation is used while fitting, and inference averages both orientations
// so the deployed value is exactly reflection invariant.
namespace drop7::structured_value_nnue {

constexpr int kHidden1 = 128;
constexpr int kHidden2 = 64;
constexpr int kHeads = 3;
constexpr int kTokenCount = 10;
constexpr int kMetricCount = 20;
constexpr int kCellCategoryBase = 0;
constexpr int kCellCategoryCount = kCellCount * kTokenCount;
constexpr int kDiscCategoryBase = kCellCategoryBase + kCellCategoryCount;
constexpr int kDiscCategoryCount = kBoardSize;
constexpr int kPhaseCategoryBase = kDiscCategoryBase + kDiscCategoryCount;
constexpr int kPhaseCategoryCount = kMovesPerLevel;
constexpr int kCategoryCount = kPhaseCategoryBase + kPhaseCategoryCount;
constexpr int kActiveCategories = kCellCount + 2;
constexpr int kMaximumLifetime = 500;
constexpr int kTrainingGames = 128;
constexpr int kHoldoutGames = 32;
constexpr int kTotalGames = kTrainingGames + kHoldoutGames;
constexpr int kEpochs = 24;
constexpr int kBatchSize = 64;
constexpr int kRootStrata = 5;
constexpr float kLearningRate = 0.001f;
constexpr float kWeightDecay = 1.0e-5f;
constexpr double kRequiredAuc = 0.80;
constexpr double kRequiredSpearman = 0.65;
constexpr double kSwitchMargin = 8.0;
constexpr double kMinimumSupport = 40.0;
constexpr double kSupportRatio = 0.80;
constexpr double kMaximumOrientationGap = 12.0;
constexpr std::uint32_t kCollectionStart = 0x3d70'6000u;
constexpr std::uint32_t kScreenStart = 0x3e7b'0000u;
constexpr std::uint32_t kConfirmationStart = 0x3e7c'0000u;

using Engineered = std::array<float, kMetricCount>;

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

PublicState mirror(const PublicState& source) {
  PublicState result = source;
  result.board = cfpi::detail::mirrorBoard(source.board);
  return result;
}

bool samePublicState(const PublicState& first, const PublicState& second) {
  return first.board == second.board && first.next_disc == second.next_disc &&
         first.moves_remaining == second.moves_remaining;
}

Engineered rawEngineered(const PublicState& source) {
  const cfpi::detail::PhaseFeatures features =
      cfpi::detail::extractPhaseFeatures(materialize(source));
  return {{
      static_cast<float>(features.open_columns),
      static_cast<float>(features.height_load),
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
      static_cast<float>(features.imminent_cover_altitude_debt),
      static_cast<float>(features.peak_height_risk),
      static_cast<float>(features.low_cap_load),
      static_cast<float>(features.adjacent_low_cap_load),
      static_cast<float>(features.quiet_build_options),
      static_cast<float>(features.quiet_direct_gain),
      static_cast<float>(features.trigger_readiness),
      static_cast<float>(features.rise_trigger_readiness),
  }};
}

using Categories = std::array<int, kActiveCategories>;

Categories activeCategories(const PublicState& state) {
  if (state.next_disc < 1 || state.next_disc > kBoardSize ||
      state.moves_remaining < 1 || state.moves_remaining > kMovesPerLevel) {
    throw std::invalid_argument("structured NNUE received invalid public state");
  }
  Categories result{};
  int offset = 0;
  for (int index = 0; index < kCellCount; ++index) {
    const int token = state.board[index];
    if (token < 0 || token >= kTokenCount) {
      throw std::invalid_argument("structured NNUE received invalid cell");
    }
    result[offset++] = kCellCategoryBase + index * kTokenCount + token;
  }
  result[offset++] = kDiscCategoryBase + state.next_disc - 1;
  result[offset++] = kPhaseCategoryBase + state.moves_remaining - 1;
  if (offset != kActiveCategories) {
    throw std::logic_error("structured NNUE category invariant failed");
  }
  return result;
}

struct Label {
  PublicState state{};
  float lifetime = 0;
  float survival_25 = 0;
  float survival_50 = 0;
};

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

Normalizer fitNormalizer(const std::vector<Label>& labels) {
  if (labels.empty()) throw std::invalid_argument("empty normalization set");
  std::array<double, kMetricCount> sum{};
  std::array<double, kMetricCount> squares{};
  for (const Label& label : labels) {
    const Engineered raw = rawEngineered(label.state);
    for (int metric = 0; metric < kMetricCount; ++metric) {
      sum[metric] += raw[metric];
      squares[metric] += raw[metric] * raw[metric];
    }
  }
  Normalizer result;
  for (int metric = 0; metric < kMetricCount; ++metric) {
    const double mean = sum[metric] / labels.size();
    const double variance =
        std::max(0.0, squares[metric] / labels.size() - mean * mean);
    result.mean[metric] = static_cast<float>(mean);
    result.scale[metric] =
        variance < 1.0e-8 ? 1.0f : static_cast<float>(std::sqrt(variance));
  }
  return result;
}

struct Example {
  Label label{};
  Engineered metrics{};
};

std::vector<Example> prepare(const std::vector<Label>& labels,
                             const Normalizer& normalizer) {
  std::vector<Example> result;
  result.reserve(labels.size());
  for (const Label& label : labels) {
    result.push_back({label, normalizer.apply(rawEngineered(label.state))});
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
  explicit Network(std::uint32_t seed = 0x5356'4e4eu) { initialize(seed); }

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
      const float value = metric_scale * metrics[metric];
      const int base = metric * kHidden1;
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
        std::abs(forward[0] - reflected[0]) * kMaximumLifetime;
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
    fill(parameters.weight2, 0.12f);
    fill(parameters.output_weight, 0.12f);
    parameters.output_bias = {{logit(0.12f), logit(0.70f), logit(0.48f)}};
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
    const float value = metric_scale * cache.metrics[metric];
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
  if (examples < 1) throw std::invalid_argument("empty NNUE batch");
  ++moments.steps;
  const float correction1 =
      1.0f - static_cast<float>(std::pow(0.9, moments.steps));
  const float correction2 =
      1.0f - static_cast<float>(std::pow(0.999, moments.steps));
  const float gradient_scale = 1.0f / examples;
  for (int category : gradient.touched) {
    const int base = category * kHidden1;
    for (int hidden = 0; hidden < kHidden1; ++hidden) {
      const int index = base + hidden;
      adamScalar(network.parameters.embedding[index],
                 gradient.embedding[index] * gradient_scale,
                 moments.embedding_m[index], moments.embedding_v[index],
                 correction1, correction2, true);
    }
  }
  adamArray(network.parameters.metric_weight, gradient.metric_weight,
            moments.metric_m, moments.metric_v, gradient_scale, correction1,
            correction2, true);
  adamArray(network.parameters.bias1, gradient.bias1, moments.bias1_m,
            moments.bias1_v, gradient_scale, correction1, correction2, false);
  adamArray(network.parameters.weight2, gradient.weight2, moments.weight2_m,
            moments.weight2_v, gradient_scale, correction1, correction2, true);
  adamArray(network.parameters.bias2, gradient.bias2, moments.bias2_m,
            moments.bias2_v, gradient_scale, correction1, correction2, false);
  adamArray(network.parameters.output_weight, gradient.output_weight,
            moments.output_m, moments.output_v, gradient_scale, correction1,
            correction2, true);
  adamArray(network.parameters.output_bias, gradient.output_bias,
            moments.output_bias_m, moments.output_bias_v, gradient_scale,
            correction1, correction2, false);
  gradient.reset();
}

std::array<float, kHeads> targetFor(const Label& label) {
  return {{label.lifetime / kMaximumLifetime, label.survival_25,
           label.survival_50}};
}

double train(Network& network, const std::vector<Example>& examples) {
  if (examples.empty()) throw std::invalid_argument("empty NNUE training set");
  std::array<double, kHeads> mean_targets{};
  for (const Example& example : examples) {
    const auto targets = targetFor(example.label);
    for (int head = 0; head < kHeads; ++head) mean_targets[head] += targets[head];
  }
  std::array<float, kHeads> priors{};
  for (int head = 0; head < kHeads; ++head) {
    priors[head] = static_cast<float>(std::clamp(
        mean_targets[head] / examples.size(), 1.0e-4, 1.0 - 1.0e-4));
  }
  network.setOutputPriors(priors);

  std::vector<std::size_t> order(examples.size());
  std::iota(order.begin(), order.end(), 0);
  Gradient gradient;
  AdamMoments moments;
  double final_loss = 0;
  for (int epoch = 0; epoch < kEpochs; ++epoch) {
    Mulberry32 shuffle(mix32(0x4e4e'5545u ^
                            static_cast<std::uint32_t>(epoch + 1)));
    for (std::size_t cursor = order.size(); cursor > 1; --cursor) {
      const std::size_t target = static_cast<std::size_t>(
          (static_cast<std::uint64_t>(shuffle.nextBits()) * cursor) >> 32);
      std::swap(order[cursor - 1], order[target]);
    }
    double epoch_loss = 0;
    std::size_t epoch_examples = 0;
    for (std::size_t start = 0; start < order.size(); start += kBatchSize) {
      const std::size_t end = std::min(order.size(), start + kBatchSize);
      int augmented = 0;
      for (std::size_t offset = start; offset < end; ++offset) {
        const Example& example = examples[order[offset]];
        const auto targets = targetFor(example.label);
        epoch_loss += accumulateExample(network, example.label.state,
                                        example.metrics, targets, gradient);
        epoch_loss += accumulateExample(network, mirror(example.label.state),
                                        example.metrics, targets, gradient);
        augmented += 2;
      }
      applyAdam(network, gradient, moments, augmented);
      epoch_examples += static_cast<std::size_t>(augmented);
    }
    final_loss = epoch_loss / std::max<std::size_t>(1, epoch_examples);
  }
  return final_loss;
}

struct Calibrator {
  float lifetime_slope = 1;
  float lifetime_intercept = 0;
  std::array<float, 2> survival_slope{{1, 1}};
  std::array<float, 2> survival_intercept{{0, 0}};
};

std::pair<float, float> fitLogisticCalibration(
    const std::vector<float>& probabilities,
    const std::vector<float>& targets) {
  if (probabilities.size() != targets.size() || probabilities.empty()) {
    throw std::invalid_argument("invalid calibration set");
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
      const double prediction = sigmoid(
          static_cast<float>(slope * input + intercept));
      const double residual = prediction - targets[index];
      const double weight = prediction * (1.0 - prediction);
      gradient_slope += residual * input;
      gradient_intercept += residual;
      hessian_ss += weight * input * input;
      hessian_si += weight * input;
      hessian_ii += weight;
    }
    const double determinant = hessian_ss * hessian_ii -
                               hessian_si * hessian_si;
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
  lifetime_predictions.reserve(examples.size());
  lifetime_targets.reserve(examples.size());
  for (const Example& example : examples) {
    const RawPrediction raw =
        network.predict(example.label.state, example.metrics);
    lifetime_predictions.push_back(raw.probabilities[0] * kMaximumLifetime);
    lifetime_targets.push_back(example.label.lifetime);
    survival_predictions[0].push_back(raw.probabilities[1]);
    survival_predictions[1].push_back(raw.probabilities[2]);
    survival_targets[0].push_back(example.label.survival_25);
    survival_targets[1].push_back(example.label.survival_50);
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
    const double centered_prediction =
        lifetime_predictions[index] - mean_prediction;
    variance += centered_prediction * centered_prediction;
    covariance += centered_prediction *
                  (lifetime_targets[index] - mean_target);
  }
  Calibrator result;
  result.lifetime_slope = static_cast<float>(
      std::clamp(variance > 1.0e-9 ? covariance / variance : 1.0, 0.05, 10.0));
  result.lifetime_intercept = static_cast<float>(std::clamp(
      mean_target - result.lifetime_slope * mean_prediction, -500.0, 500.0));
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

Prediction predict(const Network& network, const Normalizer& normalizer,
                   const Calibrator& calibrator, const PublicState& state) {
  const Engineered metrics = normalizer.apply(rawEngineered(state));
  const RawPrediction raw = network.predict(state, metrics);
  const double raw_lifetime = raw.probabilities[0] * kMaximumLifetime;
  return {
      std::clamp(calibrator.lifetime_slope * raw_lifetime +
                     calibrator.lifetime_intercept,
                 0.0, static_cast<double>(kMaximumLifetime)),
      sigmoid(calibrator.survival_slope[0] * logit(raw.probabilities[1]) +
              calibrator.survival_intercept[0]),
      sigmoid(calibrator.survival_slope[1] * logit(raw.probabilities[2]) +
              calibrator.survival_intercept[1]),
      raw.orientation_gap,
  };
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

double auc(const std::vector<double>& predictions,
           const std::vector<int>& labels) {
  if (predictions.size() != labels.size() || predictions.empty()) {
    throw std::invalid_argument("invalid AUC inputs");
  }
  std::vector<std::size_t> order(predictions.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](std::size_t first,
                                             std::size_t second) {
    return predictions[first] < predictions[second];
  });
  double positive_rank_sum = 0;
  std::uint64_t positives = 0;
  std::uint64_t negatives = 0;
  std::size_t cursor = 0;
  while (cursor < order.size()) {
    std::size_t end = cursor + 1;
    while (end < order.size() &&
           predictions[order[end]] == predictions[order[cursor]]) {
      ++end;
    }
    const double average_rank =
        0.5 * static_cast<double>(cursor + 1 + end);
    for (std::size_t index = cursor; index < end; ++index) {
      if (labels[order[index]] == 1) {
        positive_rank_sum += average_rank;
        ++positives;
      } else {
        ++negatives;
      }
    }
    cursor = end;
  }
  if (positives == 0 || negatives == 0) return 0.5;
  return (positive_rank_sum -
          static_cast<double>(positives) * (positives + 1) / 2.0) /
         static_cast<double>(positives * negatives);
}

double calibrationError(const std::vector<double>& predictions,
                        const std::vector<int>& labels) {
  constexpr int bins = 10;
  std::array<double, bins> prediction_sum{};
  std::array<double, bins> label_sum{};
  std::array<int, bins> counts{};
  for (std::size_t index = 0; index < predictions.size(); ++index) {
    const int bin = std::min(
        bins - 1, static_cast<int>(std::floor(predictions[index] * bins)));
    prediction_sum[bin] += predictions[index];
    label_sum[bin] += labels[index];
    ++counts[bin];
  }
  double error = 0;
  for (int bin = 0; bin < bins; ++bin) {
    if (counts[bin] == 0) continue;
    error += static_cast<double>(counts[bin]) / predictions.size() *
             std::abs(prediction_sum[bin] / counts[bin] -
                      label_sum[bin] / counts[bin]);
  }
  return error;
}

struct PredictionMetrics {
  int examples = 0;
  double mean_label = 0;
  double mean_prediction = 0;
  double lifetime_mae = 0;
  double death_25_auc = 0;
  double death_50_auc = 0;
  double death_25_brier = 0;
  double death_50_brier = 0;
  double death_25_ece = 0;
  double death_50_ece = 0;
  double rank_correlation = 0;
  double mean_orientation_gap = 0;
};

PredictionMetrics evaluatePredictions(const std::vector<Example>& examples,
                                      const Network& network,
                                      const Normalizer& normalizer,
                                      const Calibrator& calibrator) {
  if (examples.empty()) throw std::invalid_argument("empty evaluation set");
  std::vector<double> lifetime_predictions;
  std::vector<double> lifetime_labels;
  std::vector<double> death_25_predictions;
  std::vector<double> death_50_predictions;
  std::vector<int> death_25_labels;
  std::vector<int> death_50_labels;
  lifetime_predictions.reserve(examples.size());
  lifetime_labels.reserve(examples.size());
  PredictionMetrics result;
  result.examples = static_cast<int>(examples.size());
  for (const Example& example : examples) {
    const Prediction prediction = predict(network, normalizer, calibrator,
                                          example.label.state);
    const int death_25 = example.label.lifetime < 25 ? 1 : 0;
    const int death_50 = example.label.lifetime < 50 ? 1 : 0;
    const double predicted_death_25 = 1.0 - prediction.survival_25;
    const double predicted_death_50 = 1.0 - prediction.survival_50;
    lifetime_predictions.push_back(prediction.lifetime);
    lifetime_labels.push_back(example.label.lifetime);
    death_25_predictions.push_back(predicted_death_25);
    death_50_predictions.push_back(predicted_death_50);
    death_25_labels.push_back(death_25);
    death_50_labels.push_back(death_50);
    result.mean_label += example.label.lifetime;
    result.mean_prediction += prediction.lifetime;
    result.lifetime_mae +=
        std::abs(prediction.lifetime - example.label.lifetime);
    result.death_25_brier +=
        (predicted_death_25 - death_25) * (predicted_death_25 - death_25);
    result.death_50_brier +=
        (predicted_death_50 - death_50) * (predicted_death_50 - death_50);
    result.mean_orientation_gap += prediction.orientation_gap;
  }
  const double count = examples.size();
  result.mean_label /= count;
  result.mean_prediction /= count;
  result.lifetime_mae /= count;
  result.death_25_brier /= count;
  result.death_50_brier /= count;
  result.mean_orientation_gap /= count;
  result.death_25_auc = auc(death_25_predictions, death_25_labels);
  result.death_50_auc = auc(death_50_predictions, death_50_labels);
  result.death_25_ece =
      calibrationError(death_25_predictions, death_25_labels);
  result.death_50_ece =
      calibrationError(death_50_predictions, death_50_labels);
  result.rank_correlation =
      spearman(lifetime_predictions, lifetime_labels);
  return result;
}

void printPredictionMetrics(std::string_view tag,
                            const PredictionMetrics& metrics) {
  std::cout << std::fixed << std::setprecision(6) << tag
            << " {\"examples\":" << metrics.examples
            << ",\"meanLabel\":" << metrics.mean_label
            << ",\"meanPrediction\":" << metrics.mean_prediction
            << ",\"lifetimeMae\":" << metrics.lifetime_mae
            << ",\"death25Auc\":" << metrics.death_25_auc
            << ",\"death50Auc\":" << metrics.death_50_auc
            << ",\"death25Brier\":" << metrics.death_25_brier
            << ",\"death50Brier\":" << metrics.death_50_brier
            << ",\"death25Ece\":" << metrics.death_25_ece
            << ",\"death50Ece\":" << metrics.death_50_ece
            << ",\"rankCorrelation\":" << metrics.rank_correlation
            << ",\"meanOrientationGap\":"
            << metrics.mean_orientation_gap << "}\n";
}

struct CollectedGame {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  int clears = 0;
  int reveals = 0;
  bool censored = false;
  std::uint64_t teacher_work = 0;
  std::vector<Label> labels;
};

CollectedGame collectGame(std::uint32_t seed) {
  State state = initialHeadlessState(seed);
  std::vector<PublicState> trajectory;
  trajectory.reserve(160);
  CollectedGame result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumLifetime) {
    trajectory.push_back(publicState(state));
    cfpi::BehaviorMetrics metrics;
    const int action = cfpi::chooseBehaviorAction(state, {}, &metrics);
    result.teacher_work += metrics.work;
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("structured collector selected illegal action");
    }
    for (const Wave& wave : move.waves) {
      result.clears += wave.cleared;
      result.reveals += wave.revealed;
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  if (!result.censored) {
    result.labels.reserve(trajectory.size());
    for (std::size_t index = 0; index < trajectory.size(); ++index) {
      const int remaining = state.moves_played - static_cast<int>(index);
      result.labels.push_back({
          trajectory[index], static_cast<float>(remaining),
          remaining >= 25 ? 1.0f : 0.0f,
          remaining >= 50 ? 1.0f : 0.0f,
      });
    }
  }
  return result;
}

std::vector<CollectedGame> collectParallel(int threads) {
  std::vector<CollectedGame> games(kTotalGames);
  std::atomic<int> next{0};
  std::atomic<bool> failed{false};
  std::mutex error_mutex;
  std::string error_message;
  std::vector<std::thread> workers;
  const int worker_count = std::min(threads, kTotalGames);
  workers.reserve(worker_count);
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.emplace_back([&]() {
      while (!failed.load(std::memory_order_relaxed)) {
        const int index = next.fetch_add(1, std::memory_order_relaxed);
        if (index >= kTotalGames) break;
        try {
          games[index] = collectGame(
              kCollectionStart + static_cast<std::uint32_t>(index));
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
    throw std::runtime_error("parallel structured collection failed: " +
                             error_message);
  }
  return games;
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
    throw std::invalid_argument("invalid structured root successor");
  }
  const std::uint32_t seed = cfpi::detail::scenarioSeedForState(
      canonical, cfpi::BehaviorOptions{}.policy_seed, 1);
  cfpi::detail::StratifiedRandom random{seed, sample, kRootStrata, 0};
  MoveResult move;
  if (!cfpi::detail::playMoveSampled(canonical, action, random, move)) {
    throw std::runtime_error("structured root transition failed");
  }
  if (!move.state.game_over) {
    move.state.next_disc =
        cfpi::detail::sampledNextDisc(seed, sample, kRootStrata);
  }
  return {publicState(move.state), move.state.game_over};
}

struct ActionEstimate {
  int canonical_action = -1;
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
  result.canonical_action = action;
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

double pairedLower(const std::array<double, kRootStrata>& candidate,
                   const std::array<double, kRootStrata>& behavior) {
  std::array<double, kRootStrata> differences{};
  double mean = 0;
  for (int sample = 0; sample < kRootStrata; ++sample) {
    differences[sample] = candidate[sample] - behavior[sample];
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
  int agreement_rejections = 0;
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
    const bool supported =
        candidate.support >= kMinimumSupport &&
        candidate.support >= kSupportRatio * behavior.support;
    if (!supported) {
      ++counters.support_rejections;
      continue;
    }
    const double lower = pairedLower(candidate.values, behavior.values);
    if (candidate.orientation_gap > kMaximumOrientationGap ||
        lower <= kSwitchMargin) {
      ++counters.agreement_rejections;
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
  while (!state.game_over && state.moves_played < kMaximumLifetime) {
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
      throw std::runtime_error("structured policy selected illegal action");
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
  int agreement_rejections = 0;
  std::vector<PolicyGame> games;
};

PolicySummary summarize(std::vector<PolicyGame> games) {
  PolicySummary result;
  result.games = std::move(games);
  std::uint64_t total_moves = 0;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
  for (const PolicyGame& game : result.games) {
    result.mean_score += game.score;
    result.mean_moves += game.moves;
    result.mean_switches += game.counters.switches;
    result.modeled_transitions += game.counters.modeled_transitions;
    result.teacher_work += game.counters.teacher_work;
    result.support_rejections += game.counters.support_rejections;
    result.agreement_rejections += game.counters.agreement_rejections;
    total_moves += game.moves;
    clears += game.clears;
    reveals += game.reveals;
  }
  const double count = result.games.size();
  result.mean_score /= count;
  result.mean_moves /= count;
  result.mean_switches /= count;
  result.clear_rate = static_cast<double>(clears) /
                      std::max<std::uint64_t>(1, total_moves);
  result.reveal_rate = static_cast<double>(reveals) /
                       std::max<std::uint64_t>(1, total_moves);
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
            << ",\"agreementRejections\":" << summary.agreement_rejections
            << ",\"teacherWork\":" << summary.teacher_work << "}\n";
}

double pairedGameLower(const PolicySummary& behavior,
                       const PolicySummary& candidate, bool score) {
  if (behavior.games.size() != candidate.games.size() ||
      behavior.games.empty()) {
    throw std::invalid_argument("invalid paired policy summaries");
  }
  std::vector<double> differences;
  differences.reserve(behavior.games.size());
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
  std::cout << "STRUCTURED_NNUE_CONFIG {\"trainingGames\":"
            << kTrainingGames << ",\"holdoutGames\":" << kHoldoutGames
            << ",\"splitRule\":\"index_mod_5_zero_is_holdout\""
            << ",\"epochs\":" << kEpochs
            << ",\"batchSize\":" << kBatchSize
            << ",\"learningRate\":" << kLearningRate
            << ",\"hidden\":[" << kHidden1 << ',' << kHidden2 << ']'
            << ",\"engineeredMetrics\":" << kMetricCount
            << ",\"reflectionAugmentation\":true"
            << ",\"reflectionAveraging\":true"
            << ",\"requiredAuc\":" << kRequiredAuc
            << ",\"requiredSpearman\":" << kRequiredSpearman
            << ",\"rootStrata\":" << kRootStrata
            << ",\"switchMargin\":" << kSwitchMargin
            << ",\"minimumSupport\":" << kMinimumSupport
            << ",\"supportRatio\":" << kSupportRatio
            << ",\"maximumOrientationGap\":"
            << kMaximumOrientationGap
            << ",\"collectionSeedStart\":" << kCollectionStart
            << ",\"screenSeedStart\":" << kScreenStart
            << ",\"confirmationSeedStart\":" << kConfirmationStart
            << ",\"threads\":" << threads
            << ",\"gameSeedRanges\":[\"0x3d\",\"0x3e\"]}\n";

  const std::vector<CollectedGame> games = collectParallel(threads);
  std::vector<Label> training_labels;
  std::vector<Label> holdout_labels;
  int training_games = 0;
  int holdout_games = 0;
  int censored = 0;
  double mean_score = 0;
  double mean_moves = 0;
  std::uint64_t teacher_work = 0;
  for (int index = 0; index < kTotalGames; ++index) {
    const CollectedGame& game = games[index];
    const bool holdout = index % 5 == 0;
    auto& labels = holdout ? holdout_labels : training_labels;
    labels.insert(labels.end(), game.labels.begin(), game.labels.end());
    if (holdout) ++holdout_games;
    else ++training_games;
    censored += game.censored ? 1 : 0;
    mean_score += game.score;
    mean_moves += game.moves;
    teacher_work += game.teacher_work;
  }
  mean_score /= kTotalGames;
  mean_moves /= kTotalGames;
  std::cout << std::fixed << std::setprecision(3)
            << "STRUCTURED_NNUE_COLLECTION {\"games\":" << kTotalGames
            << ",\"trainingGames\":" << training_games
            << ",\"holdoutGames\":" << holdout_games
            << ",\"trainingLabels\":" << training_labels.size()
            << ",\"holdoutLabels\":" << holdout_labels.size()
            << ",\"meanScore\":" << mean_score
            << ",\"meanMoves\":" << mean_moves
            << ",\"censored\":" << censored
            << ",\"teacherWork\":" << teacher_work << "}\n";
  if (training_games != kTrainingGames || holdout_games != kHoldoutGames ||
      censored != 0 || training_labels.empty() || holdout_labels.empty()) {
    std::cout << "STRUCTURED_NNUE_RESULT {\"qualified\":false,"
                 "\"stoppedAt\":\"collection\",\"peakResidentBytes\":"
              << peakResidentBytes() << "}\n";
    return 3;
  }

  const Normalizer normalizer = fitNormalizer(training_labels);
  const std::vector<Example> training = prepare(training_labels, normalizer);
  const std::vector<Example> holdout = prepare(holdout_labels, normalizer);
  Network network;
  const double final_loss = train(network, training);
  const Calibrator calibrator = fitCalibrator(network, training);
  const PredictionMetrics training_metrics =
      evaluatePredictions(training, network, normalizer, calibrator);
  const PredictionMetrics holdout_metrics =
      evaluatePredictions(holdout, network, normalizer, calibrator);
  constexpr std::size_t metadata_bytes =
      (2 * kMetricCount + 6) * sizeof(float);
  std::cout << "STRUCTURED_NNUE_MODEL {\"parameters\":"
            << parameterCount() << ",\"parameterBytes\":"
            << network.parameterBytes() << ",\"metadataBytes\":"
            << metadata_bytes << ",\"modelBytes\":"
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
  printPredictionMetrics("STRUCTURED_NNUE_TRAIN", training_metrics);
  printPredictionMetrics("STRUCTURED_NNUE_HOLDOUT", holdout_metrics);
  const bool prediction_gate =
      holdout_metrics.death_25_auc >= kRequiredAuc &&
      holdout_metrics.death_50_auc >= kRequiredAuc &&
      holdout_metrics.rank_correlation >= kRequiredSpearman;
  std::cout << "STRUCTURED_NNUE_GATE {\"predictionPassed\":"
            << (prediction_gate ? "true" : "false") << "}\n";
  if (!prediction_gate) {
    const double seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
    std::cout << "STRUCTURED_NNUE_RESULT {\"qualified\":false,"
                 "\"stoppedAt\":\"prediction\",\"seconds\":"
              << seconds << ",\"peakResidentBytes\":"
              << peakResidentBytes() << "}\n";
    return 3;
  }

  SupportTracker support;
  for (const Label& label : training_labels) support.observe(label.state);
  const auto evaluate_pair = [&](std::uint32_t seed_start, int count) {
    std::vector<PolicyGame> behavior_games;
    std::vector<PolicyGame> candidate_games;
    behavior_games.reserve(count);
    candidate_games.reserve(count);
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
  printPolicySummary("STRUCTURED_NNUE_SCREEN_BEHAVIOR", screen_behavior);
  printPolicySummary("STRUCTURED_NNUE_SCREEN_CANDIDATE", screen_candidate);
  const double screen_score_lower =
      pairedGameLower(screen_behavior, screen_candidate, true);
  const double screen_moves_lower =
      pairedGameLower(screen_behavior, screen_candidate, false);
  const bool screen_pass =
      screen_candidate.mean_score > screen_behavior.mean_score &&
      screen_candidate.mean_moves > screen_behavior.mean_moves;
  std::cout << "STRUCTURED_NNUE_SCREEN {\"passed\":"
            << (screen_pass ? "true" : "false")
            << ",\"scoreLower95\":" << screen_score_lower
            << ",\"movesLower95\":" << screen_moves_lower << "}\n";
  if (!screen_pass) {
    const double seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
    std::cout << "STRUCTURED_NNUE_RESULT {\"qualified\":false,"
                 "\"stoppedAt\":\"screen\",\"seconds\":"
              << seconds << ",\"peakResidentBytes\":"
              << peakResidentBytes() << "}\n";
    return 3;
  }

  auto [confirmation_behavior, confirmation_candidate] =
      evaluate_pair(kConfirmationStart, 8);
  printPolicySummary("STRUCTURED_NNUE_CONFIRM_BEHAVIOR",
                     confirmation_behavior);
  printPolicySummary("STRUCTURED_NNUE_CONFIRM_CANDIDATE",
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
  std::cout << "STRUCTURED_NNUE_RESULT {\"qualified\":"
            << (qualified ? "true" : "false")
            << ",\"stoppedAt\":\"confirmation\",\"scoreLower95\":"
            << confirmation_score_lower << ",\"movesLower95\":"
            << confirmation_moves_lower << ",\"seconds\":" << seconds
            << ",\"peakResidentBytes\":" << peakResidentBytes() << "}\n";
  return qualified ? 0 : 3;
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
  const RawPrediction forward = network.predict(observable, normalized);
  const RawPrediction mirrored_prediction =
      network.predict(reflected, normalized);
  double reflection_difference = 0;
  for (int head = 0; head < kHeads; ++head) {
    reflection_difference = std::max(
        reflection_difference,
        std::abs(static_cast<double>(forward.probabilities[head] -
                                     mirrored_prediction.probabilities[head])));
  }

  State irrelevant = state;
  irrelevant.score = 9'999'999;
  irrelevant.level = 77;
  irrelevant.moves_played = 321;
  const bool public_only =
      samePublicState(observable, publicState(irrelevant));

  Gradient gradient;
  const std::array<float, kHeads> targets{{0.24f, 1.0f, 0.0f}};
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
  const RawPrediction trained_forward = network.predict(observable, normalized);
  const RawPrediction trained_reflected = network.predict(reflected, normalized);
  double trained_reflection_difference = 0;
  for (int head = 0; head < kHeads; ++head) {
    trained_reflection_difference = std::max(
        trained_reflection_difference,
        std::abs(static_cast<double>(trained_forward.probabilities[head] -
                                     trained_reflected.probabilities[head])));
  }

  bool ignored = false;
  const State canonical = cfpi::detail::canonicalState(state, ignored);
  int legal_count = 0;
  const auto legal = legalColumns(canonical.board, legal_count);
  bool successors = legal_count > 0;
  for (int sample = 0; sample < kRootStrata; ++sample) {
    const RootSuccessor successor = rootSuccessor(canonical, legal[0], sample);
    successors = successors &&
                 (successor.terminal || successor.state.next_disc >= 1) &&
                 (successor.terminal || successor.state.next_disc <= 7);
  }
  const bool size = parameterCount() == network.parameterBytes() / sizeof(float);
  const bool gates = kRequiredAuc == 0.80 && kRequiredSpearman == 0.65;
  const bool passed = behavior && metric_difference <= 1.0e-5 &&
                      reflection_difference == 0 &&
                      trained_reflection_difference == 0 && public_only &&
                      gradient_ok && learner_wired && successors && size && gates;
  output << "STRUCTURED_NNUE_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"publicOnly\":" << (public_only ? "true" : "false")
         << ",\"reflectionInvariant\":"
         << (trained_reflection_difference == 0 ? "true" : "false")
         << ",\"engineeredMirrorDifference\":" << metric_difference
         << ",\"gradientError\":" << std::abs(analytic - numeric)
         << ",\"learnerWired\":"
         << (learner_wired ? "true" : "false")
         << ",\"rootStrata\":" << kRootStrata
         << ",\"parameters\":" << parameterCount()
         << ",\"gateAuc\":" << kRequiredAuc
         << ",\"gateSpearman\":" << kRequiredSpearman << "}\n";
  return passed;
}

}  // namespace drop7::structured_value_nnue

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::structured_value_nnue::selfTest(std::cout) ? EXIT_SUCCESS
                                                               : EXIT_FAILURE;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--run") {
      return drop7::structured_value_nnue::runExperiment();
    }
    std::cerr << "Usage: drop7_structured_value_nnue --self-test | --run\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
