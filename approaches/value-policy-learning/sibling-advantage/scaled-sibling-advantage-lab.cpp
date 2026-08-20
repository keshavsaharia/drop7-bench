// Scales the data and model capacity used for sibling-relative Drop7 targets.
// It reuses the fixed aligned-tape implementation without editing it.
#define DROP7_SIBLING_ADVANTAGE_RANKER_LIBRARY
#include "sibling-advantage-ranker.cpp"
#undef DROP7_SIBLING_ADVANTAGE_RANKER_LIBRARY

#include <fstream>
#include <future>
#include <sstream>

namespace drop7::scaled_sibling_advantage {

namespace base = drop7::sibling_advantage_ranker;
namespace features = drop7::structured_value_nnue;

constexpr std::uint32_t kFittingStart = 0x3d93'0000u;
constexpr int kFittingGames = 26;
constexpr std::uint32_t kHeldoutStart = 0x3d94'0000u;
constexpr int kHeldoutGames = 7;
constexpr int kRootsPerTrajectory = 12;
constexpr int kExpectedRootsPerGame = 3 * kRootsPerTrajectory;
constexpr int kMinimumFittingGroups = 800;
constexpr int kMinimumHeldoutGroups = 200;
constexpr std::array<int, kRootsPerTrajectory> kCaptureMoves{{
    8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52,
}};
constexpr int kFeatureCount = 80;
constexpr int kTinyHidden = 16;
constexpr int kTrainingEpochs = 80;
constexpr int kTrainingBatchGroups = 32;
constexpr float kLinearLearningRate = 0.008f;
constexpr float kTinyLearningRate = 0.003f;
constexpr float kLinearWeightDecay = 8.0e-4f;
constexpr float kTinyWeightDecay = 2.0e-4f;
constexpr double kPairwiseWeight = 0.45;
constexpr double kRequiredTopOne = 0.35;
constexpr double kRequiredPairwise = 0.62;
constexpr double kRequiredMoveRegretImprovement = 0.35;
constexpr std::array<int, 5> kCurveGroupCounts{{50, 100, 200, 400, 800}};
constexpr std::uint32_t kScreenStart = 0x3e97'0000u;
constexpr int kScreenGames = 8;
constexpr std::uint32_t kConfirmationStart = 0x3e98'0000u;
constexpr int kConfirmationGames = 16;
constexpr int kPolicyMaximumMoves = 500;
constexpr int kParallelism = 4;

static_assert(kLevelBonus == 7'000);
static_assert(base::kContinuationTapes == 24);
static_assert(base::kContinuationHorizon == 60);
static_assert(kTinyHidden <= 16);
static_assert(kFittingGames * kExpectedRootsPerGame >= kMinimumFittingGroups);
static_assert(kHeldoutGames * kExpectedRootsPerGame >= kMinimumHeldoutGroups);
static_assert((kFittingStart >> 24) != 0x7du &&
              (kFittingStart >> 24) != 0xd7u);
static_assert((kHeldoutStart >> 24) != 0x7du &&
              (kHeldoutStart >> 24) != 0xd7u);
static_assert((kScreenStart >> 24) != 0x7du &&
              (kScreenStart >> 24) != 0xd7u);
static_assert((kConfirmationStart >> 24) != 0x7du &&
              (kConfirmationStart >> 24) != 0xd7u);

std::mutex scaled_progress_mutex;

std::vector<int> captureMoves() {
  return {kCaptureMoves.begin(), kCaptureMoves.end()};
}

base::RollInGame collectScaledGame(std::uint32_t seed) {
  base::RollInGame result;
  result.seed = seed;
  const std::vector<int> captures = captureMoves();
  base::collectTrajectory(result, base::RollInKind::kExactD3, captures);
  base::collectTrajectory(result, base::RollInKind::kOnPolicyD2, captures);
  base::collectTrajectory(result, base::RollInKind::kPerturbedD2, captures);
  return result;
}

std::vector<base::RollInGame> collectScaledGames(std::uint32_t seed_start,
                                                int games,
                                                std::string_view label) {
  std::vector<base::RollInGame> result(static_cast<std::size_t>(games));
  std::atomic<int> next{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next.fetch_add(1);
        if (game >= games) return;
        result[static_cast<std::size_t>(game)] = collectScaledGame(
            seed_start + static_cast<std::uint32_t>(game));
        const std::lock_guard<std::mutex> lock(scaled_progress_mutex);
        std::cerr << label << " roll-in game " << game + 1 << '/' << games
                  << '\n';
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

void labelScaledDataset(base::Dataset& dataset, std::string_view label) {
  std::atomic<int> next{0};
  std::atomic<int> completed{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0;
       worker < std::min<int>(kParallelism, dataset.groups.size()); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int index = next.fetch_add(1);
        if (index >= static_cast<int>(dataset.groups.size())) return;
        base::labelGroup(dataset.groups[static_cast<std::size_t>(index)]);
        const int done = completed.fetch_add(1) + 1;
        if (done % 25 == 0 || done == static_cast<int>(dataset.groups.size())) {
          const std::lock_guard<std::mutex> lock(scaled_progress_mutex);
          std::cerr << label << " labeled " << done << '/'
                    << dataset.groups.size() << " groups\n";
        }
      }
    }));
  }
  for (auto& worker : workers) worker.get();
}

using RawActionFeatures = std::array<float, kFeatureCount>;

int columnHeight(const Board& board, int column) {
  int height = 0;
  for (int row = 0; row < kBoardSize; ++row) {
    height += board[indexOf(row, column)] != kEmpty;
  }
  return height;
}

RawActionFeatures extractActionFeatures(
    const features::PublicState& public_root,
    const base::SiblingAction& action) {
  const features::PublicState root = base::canonicalPublic(public_root);
  const features::Engineered root_metrics = features::rawEngineered(root);
  std::array<double, features::kMetricCount> mean{};
  std::array<double, features::kMetricCount> squares{};
  double immediate_return = 0;
  double terminal_probability = 0;
  double immediate_positive = 0;
  for (const base::SuccessorBranch& branch : action.branches) {
    const features::Engineered branch_metrics =
        branch.terminal ? root_metrics : features::rawEngineered(branch.state);
    for (int metric = 0; metric < features::kMetricCount; ++metric) {
      mean[metric] += branch_metrics[metric] / base::kChanceStrata;
      squares[metric] +=
          static_cast<double>(branch_metrics[metric]) *
          branch_metrics[metric] / base::kChanceStrata;
    }
    immediate_return += branch.immediate_return / base::kChanceStrata;
    terminal_probability +=
        static_cast<double>(branch.terminal) / base::kChanceStrata;
    immediate_positive +=
        static_cast<double>(branch.immediate_return > 1.0) /
        base::kChanceStrata;
  }

  RawActionFeatures result{};
  int offset = 0;
  for (int metric = 0; metric < features::kMetricCount; ++metric) {
    result[offset++] =
        static_cast<float>(mean[metric] - root_metrics[metric]);
  }
  for (int metric = 0; metric < features::kMetricCount; ++metric) {
    result[offset++] = static_cast<float>(mean[metric]);
  }
  for (int metric = 0; metric < features::kMetricCount; ++metric) {
    const double variance =
        std::max(0.0, squares[metric] - mean[metric] * mean[metric]);
    result[offset++] = static_cast<float>(std::sqrt(variance));
  }
  const int height = columnHeight(root.board, action.action);
  const int left = action.action > 0
                       ? columnHeight(root.board, action.action - 1)
                       : height;
  const int right = action.action + 1 < kBoardSize
                        ? columnHeight(root.board, action.action + 1)
                        : height;
  result[offset++] = static_cast<float>(immediate_return);
  result[offset++] = static_cast<float>(terminal_probability);
  result[offset++] = static_cast<float>(height) / kBoardSize;
  result[offset++] = static_cast<float>(left + right) /
                     (2.0f * kBoardSize);
  result[offset++] = static_cast<float>(std::abs(left - right)) /
                     kBoardSize;
  result[offset++] =
      static_cast<float>(std::abs(action.action - kBoardSize / 2)) /
      (kBoardSize / 2);
  result[offset++] =
      static_cast<float>(action.action == 0 ||
                         action.action == kBoardSize - 1);
  result[offset++] = static_cast<float>(immediate_positive);
  for (int disc = 1; disc <= kBoardSize; ++disc) {
    result[offset++] = static_cast<float>(root.next_disc == disc);
  }
  for (int phase = 1; phase <= kMovesPerLevel; ++phase) {
    result[offset++] = static_cast<float>(root.moves_remaining == phase);
  }
  if (offset != kFeatureCount) {
    throw std::logic_error("scaled sibling feature count mismatch");
  }
  return result;
}

struct FeatureAction {
  int action = -1;
  RawActionFeatures raw{};
  double advantage = 0;
  double mean_return = 0;
  double mean_moves = 0;
  double mean_score = 0;
};

struct FeatureGroup {
  std::uint32_t source_game = 0;
  features::PublicState root{};
  std::vector<FeatureAction> actions;
};

std::vector<FeatureGroup> featureDataset(const base::Dataset& dataset) {
  std::vector<FeatureGroup> result;
  result.reserve(dataset.groups.size());
  for (const base::SiblingGroup& source : dataset.groups) {
    FeatureGroup group;
    group.source_game = source.source_game;
    group.root = source.root;
    for (const base::SiblingAction& action : source.actions) {
      group.actions.push_back({
          action.action,
          extractActionFeatures(source.root, action),
          action.advantage,
          action.mean_return,
          action.mean_moves,
          action.mean_score,
      });
    }
    result.push_back(std::move(group));
  }
  return result;
}

struct FeatureNormalizer {
  RawActionFeatures mean{};
  RawActionFeatures scale{};

  RawActionFeatures apply(const RawActionFeatures& raw) const {
    RawActionFeatures result{};
    for (int feature = 0; feature < kFeatureCount; ++feature) {
      result[feature] = std::clamp(
          (raw[feature] - mean[feature]) / scale[feature], -6.0f, 6.0f);
    }
    return result;
  }
};

FeatureNormalizer fitFeatureNormalizer(
    const std::vector<FeatureGroup>& groups,
    const std::vector<std::size_t>& order, int group_count) {
  if (group_count < 1 || group_count > static_cast<int>(order.size())) {
    throw std::invalid_argument("invalid normalization group count");
  }
  std::array<double, kFeatureCount> sum{};
  std::array<double, kFeatureCount> squares{};
  int examples = 0;
  for (int offset = 0; offset < group_count; ++offset) {
    for (const FeatureAction& action :
         groups[order[static_cast<std::size_t>(offset)]].actions) {
      ++examples;
      for (int feature = 0; feature < kFeatureCount; ++feature) {
        sum[feature] += action.raw[feature];
        squares[feature] +=
            static_cast<double>(action.raw[feature]) * action.raw[feature];
      }
    }
  }
  FeatureNormalizer result;
  for (int feature = 0; feature < kFeatureCount; ++feature) {
    const double mean = sum[feature] / examples;
    const double variance =
        std::max(0.0, squares[feature] / examples - mean * mean);
    result.mean[feature] = static_cast<float>(mean);
    result.scale[feature] =
        variance < 1.0e-8 ? 1.0f : static_cast<float>(std::sqrt(variance));
  }
  return result;
}

std::vector<std::size_t> shuffledGroupOrder(std::size_t groups) {
  std::vector<std::size_t> result(groups);
  std::iota(result.begin(), result.end(), 0);
  Mulberry32 random(0x4355'5256u);
  for (std::size_t cursor = result.size(); cursor > 1; --cursor) {
    const std::size_t target = static_cast<std::size_t>(
        (static_cast<std::uint64_t>(random.nextBits()) * cursor) >> 32);
    std::swap(result[cursor - 1], result[target]);
  }
  return result;
}

std::vector<double> softmax(const std::vector<double>& values) {
  if (values.empty()) throw std::invalid_argument("empty rank softmax");
  const double maximum =
      *std::max_element(values.begin(), values.end());
  std::vector<double> result(values.size());
  double total = 0;
  for (std::size_t index = 0; index < values.size(); ++index) {
    result[index] = std::exp(
        std::clamp(values[index] - maximum, -40.0, 40.0));
    total += result[index];
  }
  for (double& value : result) value /= total;
  return result;
}

double sigmoid(double value) {
  if (value >= 0) return 1.0 / (1.0 + std::exp(-value));
  const double exponential = std::exp(value);
  return exponential / (1.0 + exponential);
}

struct RankLoss {
  double loss = 0;
  std::vector<double> derivative;
};

RankLoss rankingLoss(const std::vector<double>& predictions,
                     const std::vector<double>& targets) {
  if (predictions.size() != targets.size() || predictions.size() < 2) {
    throw std::invalid_argument("invalid sibling rank loss");
  }
  const std::vector<double> predicted_probability = softmax(predictions);
  const std::vector<double> target_probability = softmax(targets);
  RankLoss result;
  result.derivative.resize(predictions.size());
  for (std::size_t action = 0; action < predictions.size(); ++action) {
    result.loss -= target_probability[action] *
                   std::log(std::max(1.0e-12,
                                     predicted_probability[action]));
    result.derivative[action] =
        predicted_probability[action] - target_probability[action];
  }
  int pairs = 0;
  for (std::size_t first = 0; first < targets.size(); ++first) {
    for (std::size_t second = first + 1; second < targets.size(); ++second) {
      pairs += std::abs(targets[first] - targets[second]) > 1.0e-6;
    }
  }
  if (pairs == 0) return result;
  for (std::size_t first = 0; first < targets.size(); ++first) {
    for (std::size_t second = first + 1; second < targets.size(); ++second) {
      const double target_difference = targets[first] - targets[second];
      if (std::abs(target_difference) <= 1.0e-6) continue;
      const double sign = target_difference > 0 ? 1.0 : -1.0;
      const double difference = predictions[first] - predictions[second];
      const double weight = std::min(2.0, std::abs(target_difference));
      const double factor = kPairwiseWeight * weight / pairs;
      result.loss += factor * std::log1p(std::exp(
                                  std::clamp(-sign * difference,
                                             -40.0, 40.0)));
      const double derivative =
          -factor * sign * sigmoid(-sign * difference);
      result.derivative[first] += derivative;
      result.derivative[second] -= derivative;
    }
  }
  return result;
}

struct LinearModel {
  std::array<float, kFeatureCount> weight{};

  double score(const RawActionFeatures& raw,
               const FeatureNormalizer& normalizer) const {
    const RawActionFeatures input = normalizer.apply(raw);
    double result = 0;
    for (int feature = 0; feature < kFeatureCount; ++feature) {
      result += weight[feature] * input[feature];
    }
    return result;
  }

  std::size_t parameterBytes() const { return sizeof(weight); }
};

struct LinearMoments {
  std::array<float, kFeatureCount> first{};
  std::array<float, kFeatureCount> second{};
  std::uint64_t steps = 0;
};

void adamScalar(float& parameter, float gradient, float& first,
                float& second, float learning_rate, float weight_decay,
                float correction1, float correction2) {
  constexpr float beta1 = 0.9f;
  constexpr float beta2 = 0.999f;
  float adjusted = gradient + weight_decay * parameter;
  adjusted = std::clamp(adjusted, -5.0f, 5.0f);
  first = beta1 * first + (1.0f - beta1) * adjusted;
  second = beta2 * second + (1.0f - beta2) * adjusted * adjusted;
  parameter -= learning_rate * (first / correction1) /
               (std::sqrt(second / correction2) + 1.0e-8f);
}

void applyLinearGradient(LinearModel& model,
                         const std::array<float, kFeatureCount>& gradient,
                         LinearMoments& moments, int groups) {
  ++moments.steps;
  const float correction1 =
      1.0f - static_cast<float>(std::pow(0.9, moments.steps));
  const float correction2 =
      1.0f - static_cast<float>(std::pow(0.999, moments.steps));
  for (int feature = 0; feature < kFeatureCount; ++feature) {
    adamScalar(model.weight[feature], gradient[feature] / groups,
               moments.first[feature], moments.second[feature],
               kLinearLearningRate, kLinearWeightDecay,
               correction1, correction2);
  }
}

struct TrainingSummary {
  double first_loss = 0;
  double final_loss = 0;
};

TrainingSummary trainLinear(LinearModel& model,
                            const std::vector<FeatureGroup>& training,
                            const std::vector<std::size_t>& base_order,
                            int group_count,
                            const FeatureNormalizer& normalizer) {
  std::vector<std::size_t> selected(
      base_order.begin(), base_order.begin() + group_count);
  LinearMoments moments;
  TrainingSummary summary;
  for (int epoch = 0; epoch < kTrainingEpochs; ++epoch) {
    std::vector<std::size_t> order = selected;
    Mulberry32 random(mix32(0x4c49'4e45u ^
                           static_cast<std::uint32_t>(epoch + 1)));
    for (std::size_t cursor = order.size(); cursor > 1; --cursor) {
      const std::size_t target = static_cast<std::size_t>(
          (static_cast<std::uint64_t>(random.nextBits()) * cursor) >> 32);
      std::swap(order[cursor - 1], order[target]);
    }
    double epoch_loss = 0;
    int observed_groups = 0;
    for (std::size_t start = 0; start < order.size();
         start += kTrainingBatchGroups) {
      const std::size_t end =
          std::min(order.size(), start + kTrainingBatchGroups);
      std::array<float, kFeatureCount> gradient{};
      for (std::size_t offset = start; offset < end; ++offset) {
        const FeatureGroup& group = training[order[offset]];
        std::vector<double> predictions;
        std::vector<double> targets;
        predictions.reserve(group.actions.size());
        targets.reserve(group.actions.size());
        for (const FeatureAction& action : group.actions) {
          predictions.push_back(model.score(action.raw, normalizer));
          targets.push_back(action.advantage);
        }
        const RankLoss loss = rankingLoss(predictions, targets);
        epoch_loss += loss.loss;
        ++observed_groups;
        for (std::size_t action = 0; action < group.actions.size(); ++action) {
          const RawActionFeatures input =
              normalizer.apply(group.actions[action].raw);
          for (int feature = 0; feature < kFeatureCount; ++feature) {
            gradient[feature] += static_cast<float>(
                loss.derivative[action] * input[feature]);
          }
        }
      }
      applyLinearGradient(model, gradient, moments,
                          static_cast<int>(end - start));
    }
    const double mean_loss = epoch_loss / std::max(1, observed_groups);
    if (epoch == 0) summary.first_loss = mean_loss;
    summary.final_loss = mean_loss;
  }
  return summary;
}

struct TinyForward {
  RawActionFeatures input{};
  std::array<float, kTinyHidden> pre{};
  std::array<float, kTinyHidden> hidden{};
  float score = 0;
};

struct TinyModel {
  TinyModel() {
    Mulberry32 random(0x5449'4e59u);
    for (float& value : weight) {
      value = static_cast<float>(
          (2.0 * random.nextUnit() - 1.0) * 0.08);
    }
    for (float& value : output) {
      value = static_cast<float>(
          (2.0 * random.nextUnit() - 1.0) * 0.12);
    }
  }

  float forward(const RawActionFeatures& raw,
                const FeatureNormalizer& normalizer,
                TinyForward* cache = nullptr) const {
    TinyForward local;
    TinyForward& result = cache == nullptr ? local : *cache;
    result.input = normalizer.apply(raw);
    result.pre = bias;
    const float input_scale =
        1.0f / static_cast<float>(std::sqrt(kFeatureCount));
    for (int hidden = 0; hidden < kTinyHidden; ++hidden) {
      const int base_index = hidden * kFeatureCount;
      for (int feature = 0; feature < kFeatureCount; ++feature) {
        result.pre[hidden] += input_scale *
            weight[base_index + feature] * result.input[feature];
      }
      result.hidden[hidden] = features::leaky(result.pre[hidden]);
    }
    result.score = 0;
    for (int hidden = 0; hidden < kTinyHidden; ++hidden) {
      result.score += output[hidden] * result.hidden[hidden];
    }
    return result.score;
  }

  double score(const RawActionFeatures& raw,
               const FeatureNormalizer& normalizer) const {
    return forward(raw, normalizer);
  }

  std::size_t parameterBytes() const {
    return (weight.size() + bias.size() + output.size()) * sizeof(float);
  }

  std::array<float, kTinyHidden * kFeatureCount> weight{};
  std::array<float, kTinyHidden> bias{};
  std::array<float, kTinyHidden> output{};
};

struct TinyGradient {
  std::array<float, kTinyHidden * kFeatureCount> weight{};
  std::array<float, kTinyHidden> bias{};
  std::array<float, kTinyHidden> output{};
};

struct TinyMoments {
  TinyGradient first;
  TinyGradient second;
  std::uint64_t steps = 0;
};

void accumulateTiny(const TinyModel& model, const RawActionFeatures& raw,
                    const FeatureNormalizer& normalizer,
                    double derivative_score, TinyGradient& gradient) {
  TinyForward cache;
  model.forward(raw, normalizer, &cache);
  const float input_scale =
      1.0f / static_cast<float>(std::sqrt(kFeatureCount));
  for (int hidden = 0; hidden < kTinyHidden; ++hidden) {
    gradient.output[hidden] += static_cast<float>(
        derivative_score * cache.hidden[hidden]);
    const float derivative = static_cast<float>(
        derivative_score * model.output[hidden] *
        features::leakyDerivative(cache.pre[hidden]));
    gradient.bias[hidden] += derivative;
    const int base_index = hidden * kFeatureCount;
    for (int feature = 0; feature < kFeatureCount; ++feature) {
      gradient.weight[base_index + feature] +=
          input_scale * derivative * cache.input[feature];
    }
  }
}

template <std::size_t Size>
void applyTinyArray(std::array<float, Size>& parameter,
                    const std::array<float, Size>& gradient,
                    std::array<float, Size>& first,
                    std::array<float, Size>& second, float scale,
                    float correction1, float correction2,
                    float weight_decay) {
  for (std::size_t index = 0; index < Size; ++index) {
    adamScalar(parameter[index], gradient[index] * scale,
               first[index], second[index], kTinyLearningRate,
               weight_decay, correction1, correction2);
  }
}

void applyTinyGradient(TinyModel& model, TinyGradient& gradient,
                       TinyMoments& moments, int groups) {
  ++moments.steps;
  const float correction1 =
      1.0f - static_cast<float>(std::pow(0.9, moments.steps));
  const float correction2 =
      1.0f - static_cast<float>(std::pow(0.999, moments.steps));
  const float scale = 1.0f / groups;
  applyTinyArray(model.weight, gradient.weight, moments.first.weight,
                 moments.second.weight, scale, correction1, correction2,
                 kTinyWeightDecay);
  applyTinyArray(model.bias, gradient.bias, moments.first.bias,
                 moments.second.bias, scale, correction1, correction2, 0);
  applyTinyArray(model.output, gradient.output, moments.first.output,
                 moments.second.output, scale, correction1, correction2,
                 kTinyWeightDecay);
  gradient = TinyGradient{};
}

TrainingSummary trainTiny(TinyModel& model,
                          const std::vector<FeatureGroup>& training,
                          const std::vector<std::size_t>& base_order,
                          int group_count,
                          const FeatureNormalizer& normalizer) {
  std::vector<std::size_t> selected(
      base_order.begin(), base_order.begin() + group_count);
  TinyMoments moments;
  TrainingSummary summary;
  for (int epoch = 0; epoch < kTrainingEpochs; ++epoch) {
    std::vector<std::size_t> order = selected;
    Mulberry32 random(mix32(0x5449'4e59u ^
                           static_cast<std::uint32_t>(epoch + 1)));
    for (std::size_t cursor = order.size(); cursor > 1; --cursor) {
      const std::size_t target = static_cast<std::size_t>(
          (static_cast<std::uint64_t>(random.nextBits()) * cursor) >> 32);
      std::swap(order[cursor - 1], order[target]);
    }
    double epoch_loss = 0;
    int observed_groups = 0;
    for (std::size_t start = 0; start < order.size();
         start += kTrainingBatchGroups) {
      const std::size_t end =
          std::min(order.size(), start + kTrainingBatchGroups);
      TinyGradient gradient;
      for (std::size_t offset = start; offset < end; ++offset) {
        const FeatureGroup& group = training[order[offset]];
        std::vector<double> predictions;
        std::vector<double> targets;
        predictions.reserve(group.actions.size());
        targets.reserve(group.actions.size());
        for (const FeatureAction& action : group.actions) {
          predictions.push_back(model.score(action.raw, normalizer));
          targets.push_back(action.advantage);
        }
        const RankLoss loss = rankingLoss(predictions, targets);
        epoch_loss += loss.loss;
        ++observed_groups;
        for (std::size_t action = 0; action < group.actions.size(); ++action) {
          accumulateTiny(model, group.actions[action].raw, normalizer,
                         loss.derivative[action], gradient);
        }
      }
      applyTinyGradient(model, gradient, moments,
                        static_cast<int>(end - start));
    }
    const double mean_loss = epoch_loss / std::max(1, observed_groups);
    if (epoch == 0) summary.first_loss = mean_loss;
    summary.final_loss = mean_loss;
  }
  return summary;
}

struct RankingMetrics {
  int groups = 0;
  int pairs = 0;
  double top1_accuracy = 0;
  double pairwise_accuracy = 0;
  double mean_return_regret = 0;
  double mean_move_regret = 0;
  double mean_spearman = 0;
};

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

void observeMetrics(RankingMetrics& result,
                    const std::vector<double>& predictions,
                    const FeatureGroup& group) {
  if (predictions.size() != group.actions.size() ||
      predictions.size() < 2) {
    throw std::invalid_argument("invalid scaled ranking group");
  }
  std::vector<double> returns;
  std::vector<double> moves;
  returns.reserve(group.actions.size());
  moves.reserve(group.actions.size());
  for (const FeatureAction& action : group.actions) {
    returns.push_back(action.mean_return);
    moves.push_back(action.mean_moves);
  }
  const int target_best = bestIndex(returns);
  const int move_best = bestIndex(moves);
  const int predicted_best = bestIndex(predictions);
  result.top1_accuracy += predicted_best == target_best;
  result.mean_return_regret +=
      returns[static_cast<std::size_t>(target_best)] -
      returns[static_cast<std::size_t>(predicted_best)];
  result.mean_move_regret +=
      moves[static_cast<std::size_t>(move_best)] -
      moves[static_cast<std::size_t>(predicted_best)];
  result.mean_spearman += features::spearman(predictions, returns);
  for (std::size_t first = 0; first < returns.size(); ++first) {
    for (std::size_t second = first + 1; second < returns.size(); ++second) {
      const double target_difference = returns[first] - returns[second];
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

void finishMetrics(RankingMetrics& result) {
  if (result.groups > 0) {
    result.top1_accuracy /= result.groups;
    result.mean_return_regret /= result.groups;
    result.mean_move_regret /= result.groups;
    result.mean_spearman /= result.groups;
  }
  if (result.pairs > 0) result.pairwise_accuracy /= result.pairs;
}

template <typename Model>
RankingMetrics evaluateModel(const Model& model,
                             const FeatureNormalizer& normalizer,
                             const std::vector<FeatureGroup>& groups) {
  RankingMetrics result;
  for (const FeatureGroup& group : groups) {
    std::vector<double> predictions;
    predictions.reserve(group.actions.size());
    for (const FeatureAction& action : group.actions) {
      predictions.push_back(model.score(action.raw, normalizer));
    }
    observeMetrics(result, predictions, group);
  }
  finishMetrics(result);
  return result;
}

template <typename Model>
RankingMetrics evaluateModelSubset(
    const Model& model, const FeatureNormalizer& normalizer,
    const std::vector<FeatureGroup>& groups,
    const std::vector<std::size_t>& order, int group_count) {
  RankingMetrics result;
  for (int offset = 0; offset < group_count; ++offset) {
    const FeatureGroup& group =
        groups[order[static_cast<std::size_t>(offset)]];
    std::vector<double> predictions;
    predictions.reserve(group.actions.size());
    for (const FeatureAction& action : group.actions) {
      predictions.push_back(model.score(action.raw, normalizer));
    }
    observeMetrics(result, predictions, group);
  }
  finishMetrics(result);
  return result;
}

struct ExactReference {
  std::vector<std::vector<double>> predictions;
  RankingMetrics metrics;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::size_t peak_cache_entries = 0;
};

ExactReference evaluateExactHeldout(
    const std::vector<FeatureGroup>& heldout) {
  ExactReference result;
  result.predictions.reserve(heldout.size());
  for (const FeatureGroup& group : heldout) {
    const base::ExactValues exact = base::exactRootValues(group.root);
    if (exact.actions.size() != group.actions.size()) {
      throw std::runtime_error("scaled exact action count mismatch");
    }
    for (std::size_t action = 0; action < exact.actions.size(); ++action) {
      if (exact.actions[action] != group.actions[action].action) {
        throw std::runtime_error("scaled exact action order mismatch");
      }
    }
    result.predictions.push_back(exact.values);
    observeMetrics(result.metrics, exact.values, group);
    result.work += exact.work;
    result.nodes += exact.nodes;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, exact.cache_entries);
  }
  finishMetrics(result.metrics);
  return result;
}

struct CurvePoint {
  int fitting_groups = 0;
  TrainingSummary linear_training;
  TrainingSummary tiny_training;
  RankingMetrics linear_fitting;
  RankingMetrics tiny_fitting;
  RankingMetrics linear_heldout;
  RankingMetrics tiny_heldout;
};

struct LearningResult {
  std::vector<CurvePoint> curve;
  LinearModel linear;
  TinyModel tiny;
  FeatureNormalizer normalizer;
  TrainingSummary linear_training;
  TrainingSummary tiny_training;
};

LearningResult runLearningCurves(
    const std::vector<FeatureGroup>& fitting,
    const std::vector<FeatureGroup>& heldout,
    const std::vector<std::size_t>& order) {
  std::vector<int> counts;
  for (const int count : kCurveGroupCounts) {
    if (count <= static_cast<int>(fitting.size())) counts.push_back(count);
  }
  if (counts.empty() || counts.back() != static_cast<int>(fitting.size())) {
    counts.push_back(static_cast<int>(fitting.size()));
  }
  LearningResult result;
  for (const int count : counts) {
    const FeatureNormalizer normalizer =
        fitFeatureNormalizer(fitting, order, count);
    LinearModel linear;
    TinyModel tiny;
    const TrainingSummary linear_training =
        trainLinear(linear, fitting, order, count, normalizer);
    const TrainingSummary tiny_training =
        trainTiny(tiny, fitting, order, count, normalizer);
    CurvePoint point;
    point.fitting_groups = count;
    point.linear_training = linear_training;
    point.tiny_training = tiny_training;
    point.linear_fitting = evaluateModelSubset(
        linear, normalizer, fitting, order, count);
    point.tiny_fitting = evaluateModelSubset(
        tiny, normalizer, fitting, order, count);
    point.linear_heldout = evaluateModel(linear, normalizer, heldout);
    point.tiny_heldout = evaluateModel(tiny, normalizer, heldout);
    result.curve.push_back(point);
    {
      const std::lock_guard<std::mutex> lock(scaled_progress_mutex);
      std::cerr << "learning curve " << count
                << " linear heldout " << point.linear_heldout.top1_accuracy
                << '/' << point.linear_heldout.pairwise_accuracy
                << " tiny heldout " << point.tiny_heldout.top1_accuracy
                << '/' << point.tiny_heldout.pairwise_accuracy << '\n';
    }
    if (count == static_cast<int>(fitting.size())) {
      result.linear = std::move(linear);
      result.tiny = std::move(tiny);
      result.normalizer = normalizer;
      result.linear_training = linear_training;
      result.tiny_training = tiny_training;
    }
  }
  return result;
}

enum class Architecture { kLinear, kTiny16 };

std::string_view architectureName(Architecture architecture) {
  return architecture == Architecture::kLinear ? "linear-action-delta"
                                                : "tiny16-action-delta";
}

double moveRegretImprovement(const RankingMetrics& candidate,
                             const RankingMetrics& exact) {
  return exact.mean_move_regret - candidate.mean_move_regret;
}

bool clearsGate(const RankingMetrics& candidate,
                const RankingMetrics& exact) {
  return candidate.top1_accuracy >= kRequiredTopOne &&
         candidate.pairwise_accuracy >= kRequiredPairwise &&
         moveRegretImprovement(candidate, exact) >=
             kRequiredMoveRegretImprovement;
}

Architecture chooseArchitecture(const RankingMetrics& linear,
                                const RankingMetrics& tiny,
                                const RankingMetrics& exact) {
  const bool linear_passes = clearsGate(linear, exact);
  const bool tiny_passes = clearsGate(tiny, exact);
  if (linear_passes != tiny_passes) {
    return linear_passes ? Architecture::kLinear : Architecture::kTiny16;
  }
  const double linear_improvement = moveRegretImprovement(linear, exact);
  const double tiny_improvement = moveRegretImprovement(tiny, exact);
  if (linear_improvement != tiny_improvement) {
    return linear_improvement > tiny_improvement ? Architecture::kLinear
                                                 : Architecture::kTiny16;
  }
  if (linear.top1_accuracy != tiny.top1_accuracy) {
    return linear.top1_accuracy > tiny.top1_accuracy ? Architecture::kLinear
                                                     : Architecture::kTiny16;
  }
  return linear.pairwise_accuracy >= tiny.pairwise_accuracy
             ? Architecture::kLinear
             : Architecture::kTiny16;
}

struct FrozenPolicy {
  Architecture architecture = Architecture::kLinear;
  const LinearModel& linear;
  const TinyModel& tiny;
  const FeatureNormalizer& normalizer;
};

double frozenScore(const FrozenPolicy& policy,
                   const RawActionFeatures& raw) {
  return policy.architecture == Architecture::kLinear
             ? policy.linear.score(raw, policy.normalizer)
             : policy.tiny.score(raw, policy.normalizer);
}

struct PolicyDecision {
  int action = -1;
  std::uint64_t work = 0;
};

PolicyDecision chooseFrozenAction(const State& source,
                                  const FrozenPolicy& policy) {
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  const features::PublicState public_root = features::publicState(canonical);
  const base::Origin origin{
      0u, base::RollInKind::kExactD3, 0, public_root,
  };
  const base::SiblingGroup group = base::makeGroup(origin);
  std::vector<double> predictions;
  predictions.reserve(group.actions.size());
  PolicyDecision result;
  for (const base::SiblingAction& action : group.actions) {
    predictions.push_back(frozenScore(
        policy, extractActionFeatures(group.root, action)));
    result.work += base::kChanceStrata;
  }
  const int best = bestIndex(predictions);
  if (best < 0) return result;
  const int canonical_action =
      group.actions[static_cast<std::size_t>(best)].action;
  result.action = mirrored ? kBoardSize - 1 - canonical_action
                           : canonical_action;
  return result;
}

base::GameResult runPolicyGame(std::uint32_t seed,
                               const FrozenPolicy& policy,
                               bool baseline,
                               std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  base::GameResult result;
  const cfpi::BehaviorOptions exact_options = base::behaviorOptions(3, 5);
  while (!state.game_over && state.moves_played < kPolicyMaximumMoves) {
    int action = -1;
    if (baseline) {
      cfpi::BehaviorMetrics metrics;
      action = cfpi::chooseBehaviorAction(state, exact_options, &metrics);
      if (!metrics.complete || metrics.completed_depth != 3) {
        throw std::runtime_error("scaled screen exact-d3 incomplete");
      }
      result.work += metrics.work;
      result.nodes += metrics.nodes;
      result.peak_cache_entries =
          std::max(result.peak_cache_entries, metrics.cache_entries);
    } else {
      const PolicyDecision decision = chooseFrozenAction(state, policy);
      action = decision.action;
      result.work += decision.work;
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("scaled screen selected illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("scaled screen transition failed");
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.peak_rss_bytes = base::peakRssBytes();
  result.elapsed_seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
  {
    const std::lock_guard<std::mutex> lock(scaled_progress_mutex);
    std::cerr << label << " seed 0x" << std::hex << seed << std::dec << ' '
              << result.score << '/' << result.moves << " work "
              << result.work << '\n';
  }
  return result;
}

base::PolicyCohort runPolicyCohort(std::uint32_t seed_start, int games,
                                   const FrozenPolicy& policy,
                                   std::string_view phase) {
  base::PolicyCohort result;
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
            seed, policy, true, std::string(phase) + "-exact-d3");
        result.candidate[static_cast<std::size_t>(game)] = runPolicyGame(
            seed, policy, false,
            std::string(phase) + '-' +
                std::string(architectureName(policy.architecture)));
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

void saveFrozenModel(const std::string& path,
                     const FrozenPolicy& policy) {
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("could not open scaled model");
  const std::array<std::uint32_t, 7> header{{
      0x5353'414cu,
      1u,
      static_cast<std::uint32_t>(policy.architecture == Architecture::kLinear
                                     ? 0
                                     : 1),
      kFeatureCount,
      kTinyHidden,
      base::kContinuationTapes,
      base::kContinuationHorizon,
  }};
  const auto write = [&output](const auto& values) {
    output.write(reinterpret_cast<const char*>(values.data()),
                 static_cast<std::streamsize>(values.size() *
                                              sizeof(values[0])));
  };
  write(header);
  write(policy.normalizer.mean);
  write(policy.normalizer.scale);
  if (policy.architecture == Architecture::kLinear) {
    write(policy.linear.weight);
  } else {
    write(policy.tiny.weight);
    write(policy.tiny.bias);
    write(policy.tiny.output);
  }
  if (!output) throw std::runtime_error("could not write scaled model");
}

void writeRankingMetrics(std::ostream& output,
                         const RankingMetrics& result) {
  output << "{\"groups\":" << result.groups
         << ",\"pairs\":" << result.pairs
         << ",\"top1Accuracy\":" << result.top1_accuracy
         << ",\"pairwiseAccuracy\":" << result.pairwise_accuracy
         << ",\"meanReturnRegret\":" << result.mean_return_regret
         << ",\"meanMoveRegret\":" << result.mean_move_regret
         << ",\"meanWithinStateSpearman\":" << result.mean_spearman
         << '}';
}

void writeTrainingSummary(std::ostream& output,
                          const TrainingSummary& result) {
  output << "{\"firstLoss\":" << result.first_loss
         << ",\"finalLoss\":" << result.final_loss << '}';
}

void writeCurve(std::ostream& output,
                const std::vector<CurvePoint>& curve) {
  output << '[';
  for (std::size_t index = 0; index < curve.size(); ++index) {
    if (index > 0) output << ',';
    const CurvePoint& point = curve[index];
    output << "{\"fittingGroups\":" << point.fitting_groups
           << ",\"linearTraining\":";
    writeTrainingSummary(output, point.linear_training);
    output << ",\"tinyTraining\":";
    writeTrainingSummary(output, point.tiny_training);
    output << ",\"linearFitting\":";
    writeRankingMetrics(output, point.linear_fitting);
    output << ",\"tinyFitting\":";
    writeRankingMetrics(output, point.tiny_fitting);
    output << ",\"linearHeldout\":";
    writeRankingMetrics(output, point.linear_heldout);
    output << ",\"tinyHeldout\":";
    writeRankingMetrics(output, point.tiny_heldout);
    output << '}';
  }
  output << ']';
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
  const features::PublicState root = base::canonicalPublic(state);
  const features::PublicState reflected = features::mirror(root);
  const base::Origin first_origin{
      kFittingStart, base::RollInKind::kExactD3, 8, root,
  };
  base::Origin second_origin = first_origin;
  second_origin.root = reflected;
  const base::SiblingGroup first_group = base::makeGroup(first_origin);
  const base::SiblingGroup second_group = base::makeGroup(second_origin);
  bool reflection_safe =
      first_group.actions.size() == second_group.actions.size();
  for (std::size_t action = 0;
       reflection_safe && action < first_group.actions.size(); ++action) {
    reflection_safe =
        first_group.actions[action].action == second_group.actions[action].action &&
        extractActionFeatures(first_group.root, first_group.actions[action]) ==
            extractActionFeatures(second_group.root,
                                  second_group.actions[action]);
  }

  FeatureGroup toy;
  toy.root = root;
  for (std::size_t action = 0; action < first_group.actions.size(); ++action) {
    toy.actions.push_back({
        first_group.actions[action].action,
        extractActionFeatures(first_group.root, first_group.actions[action]),
        static_cast<double>(action) - 3.0,
        static_cast<double>(action),
        static_cast<double>(action),
        0,
    });
  }
  const std::vector<FeatureGroup> toy_groups{toy};
  const std::vector<std::size_t> toy_order{0};
  const FeatureNormalizer normalizer =
      fitFeatureNormalizer(toy_groups, toy_order, 1);
  LinearModel linear;
  TinyModel tiny;
  std::vector<double> linear_predictions;
  std::vector<double> tiny_predictions;
  std::vector<double> targets;
  for (const FeatureAction& action : toy.actions) {
    linear_predictions.push_back(linear.score(action.raw, normalizer));
    tiny_predictions.push_back(tiny.score(action.raw, normalizer));
    targets.push_back(action.advantage);
  }
  const RankLoss linear_loss = rankingLoss(linear_predictions, targets);
  const RankLoss tiny_loss = rankingLoss(tiny_predictions, targets);
  const bool losses_finite = std::isfinite(linear_loss.loss) &&
                             std::isfinite(tiny_loss.loss);

  RankingMetrics exact;
  exact.mean_move_regret = 1.0;
  RankingMetrics candidate;
  candidate.top1_accuracy = 0.35;
  candidate.pairwise_accuracy = 0.62;
  candidate.mean_move_regret = 0.65;
  const bool gate_enforced = clearsGate(candidate, exact) &&
                             !clearsGate(exact, candidate);

  bool aligned_tapes = true;
  bool safe_tapes = true;
  for (int tape = 0; tape < base::kContinuationTapes; ++tape) {
    const std::uint32_t seed = base::tapeSeed(root, tape);
    const base::PublicTape first{seed, 7};
    const base::PublicTape repeat{seed, 7};
    aligned_tapes = aligned_tapes &&
                    seed == base::tapeSeed(reflected, tape) &&
                    first.nextDiscForMove(8) ==
                        repeat.nextDiscForMove(8) &&
                    first.revealDisc(3) == repeat.revealDisc(3);
    safe_tapes = safe_tapes && (seed >> 24) != 0x7du &&
                 (seed >> 24) != 0xd7u;
  }
  const FrozenPolicy policy{
      Architecture::kLinear, linear, tiny, normalizer,
  };
  const PolicyDecision decision = chooseFrozenAction(state, policy);
  State mirrored_state = state;
  mirrored_state.board = cfpi::detail::mirrorBoard(state.board);
  const PolicyDecision mirrored_decision =
      chooseFrozenAction(mirrored_state, policy);
  const bool policy_reflection =
      mirrored_decision.action == kBoardSize - 1 - decision.action;
  const bool legal = isLegal(state.board, decision.action);
  const bool bounded = decision.work <= kBoardSize * base::kChanceStrata &&
                       linear.parameterBytes() ==
                           kFeatureCount * sizeof(float) &&
                       tiny.parameterBytes() <= 6'000;
  const bool disjoint_seeds =
      kFittingStart + kFittingGames <= kHeldoutStart &&
      kHeldoutStart + kHeldoutGames <= kScreenStart &&
      kScreenStart + kScreenGames <= kConfirmationStart;
  const bool source_counts =
      kFittingGames * kExpectedRootsPerGame >= kMinimumFittingGroups &&
      kHeldoutGames * kExpectedRootsPerGame >= kMinimumHeldoutGroups;
  const bool passed = reflection_safe && losses_finite && gate_enforced &&
                      aligned_tapes && safe_tapes && policy_reflection &&
                      legal && bounded && disjoint_seeds && source_counts &&
                      kLevelBonus == 7'000;
  output << "SCALED_SIBLING_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe && policy_reflection ? "true" : "false")
         << ",\"lossesFinite\":"
         << (losses_finite ? "true" : "false")
         << ",\"gateEnforced\":"
         << (gate_enforced ? "true" : "false")
         << ",\"alignedPublicTapes\":"
         << (aligned_tapes ? "true" : "false")
         << ",\"safeTapeFamilies\":"
         << (safe_tapes ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false")
         << ",\"bounded\":" << (bounded ? "true" : "false")
         << ",\"disjointSeeds\":"
         << (disjoint_seeds ? "true" : "false")
         << ",\"sourceCounts\":"
         << (source_counts ? "true" : "false")
         << ",\"levelBonus\":" << kLevelBonus << "}\n";
  return passed;
}

struct ProgramOptions {
  std::string artifact = "/tmp/drop7-scaled-sibling-advantage.json";
  std::string model = "/tmp/drop7-scaled-sibling-advantage.bin";
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

std::uint64_t rollInWork(const std::vector<base::RollInGame>& games) {
  std::uint64_t result = 0;
  for (const base::RollInGame& game : games) result += game.search_work;
  return result;
}

int run(const ProgramOptions& options, std::ostream& output) {
  const auto started = std::chrono::steady_clock::now();
  const std::vector<base::RollInGame> fitting_games =
      collectScaledGames(kFittingStart, kFittingGames, "fitting");
  const std::vector<base::RollInGame> heldout_games =
      collectScaledGames(kHeldoutStart, kHeldoutGames, "heldout");
  base::Dataset fitting = base::buildDataset(
      fitting_games, 0, static_cast<int>(fitting_games.size()));
  base::Dataset heldout = base::buildDataset(
      heldout_games, 0, static_cast<int>(heldout_games.size()));
  const base::PurgeStats purge =
      base::purgeDevelopmentOverlap(fitting, heldout);
  if (static_cast<int>(fitting.groups.size()) < kMinimumFittingGroups ||
      static_cast<int>(heldout.groups.size()) < kMinimumHeldoutGroups) {
    throw std::runtime_error("scaled sibling group minimum not reached");
  }

  labelScaledDataset(fitting, "fitting");
  labelScaledDataset(heldout, "heldout");
  const std::vector<FeatureGroup> fitting_features =
      featureDataset(fitting);
  const std::vector<FeatureGroup> heldout_features =
      featureDataset(heldout);
  const std::vector<std::size_t> order =
      shuffledGroupOrder(fitting_features.size());
  const ExactReference exact = evaluateExactHeldout(heldout_features);
  LearningResult learning = runLearningCurves(
      fitting_features, heldout_features, order);
  const CurvePoint& full = learning.curve.back();
  const RankingMetrics& linear_heldout = full.linear_heldout;
  const RankingMetrics& tiny_heldout = full.tiny_heldout;
  const Architecture architecture = chooseArchitecture(
      linear_heldout, tiny_heldout, exact.metrics);
  const RankingMetrics& selected =
      architecture == Architecture::kLinear ? linear_heldout : tiny_heldout;
  const bool heldout_passed = clearsGate(selected, exact.metrics);
  const FrozenPolicy policy{
      architecture, learning.linear, learning.tiny, learning.normalizer,
  };
  saveFrozenModel(options.model, policy);

  base::PolicyCohort screen;
  base::PolicySummary screen_baseline;
  base::PolicySummary screen_candidate;
  base::PairedSummary screen_paired;
  bool screen_passed = false;
  if (heldout_passed) {
    screen = runPolicyCohort(kScreenStart, kScreenGames, policy, "screen");
    screen_baseline = base::summarizePolicy(screen.baseline);
    screen_candidate = base::summarizePolicy(screen.candidate);
    screen_paired = base::pairedPolicy(screen);
    screen_passed = screen_paired.mean_score_difference > 0 &&
                    screen_paired.mean_move_difference > 0;
  }

  base::PolicyCohort confirmation;
  base::PolicySummary confirmation_baseline;
  base::PolicySummary confirmation_candidate;
  base::PairedSummary confirmation_paired;
  bool confirmed = false;
  if (screen_passed) {
    confirmation = runPolicyCohort(kConfirmationStart, kConfirmationGames,
                                   policy, "confirmation");
    confirmation_baseline = base::summarizePolicy(confirmation.baseline);
    confirmation_candidate = base::summarizePolicy(confirmation.candidate);
    confirmation_paired = base::pairedPolicy(confirmation);
    confirmed = confirmation_paired.mean_score_difference > 0 &&
                confirmation_paired.mean_move_difference > 0;
  }

  const base::DatasetSummary fitting_summary =
      base::summarizeDataset(fitting);
  const base::DatasetSummary heldout_summary =
      base::summarizeDataset(heldout);
  const double move_regret_improvement =
      moveRegretImprovement(selected, exact.metrics);
  const double elapsed_seconds = std::chrono::duration<double>(
                                     std::chrono::steady_clock::now() -
                                     started)
                                     .count();
  const std::string_view decision =
      !heldout_passed
          ? "reject-heldout"
          : (!screen_passed
                 ? "reject-screen"
                 : (confirmed ? "advance" : "reject-confirmation"));

  std::ofstream artifact(options.artifact);
  if (!artifact) {
    throw std::runtime_error("could not open scaled sibling artifact");
  }
  artifact << std::setprecision(10)
           << "{\n  \"format\": "
              "\"drop7-scaled-sibling-advantage-v1\",\n"
           << "  \"publicStateOnly\": true,\n"
           << "  \"commonRandomNumbers\": true,\n"
           << "  \"hiddenRealFuturesUsed\": false,\n"
           << "  \"levelBonus\": " << kLevelBonus << ",\n"
           << "  \"fittingSeedStart\": " << kFittingStart << ",\n"
           << "  \"fittingGames\": " << kFittingGames << ",\n"
           << "  \"heldoutSeedStart\": " << kHeldoutStart << ",\n"
           << "  \"heldoutGames\": " << kHeldoutGames << ",\n"
           << "  \"captureMoves\": [";
  for (std::size_t index = 0; index < kCaptureMoves.size(); ++index) {
    if (index > 0) artifact << ',';
    artifact << kCaptureMoves[index];
  }
  artifact << "],\n  \"rollInsPerGame\": {\"exactD3\":"
           << kRootsPerTrajectory << ",\"onPolicyD2S3\":"
           << kRootsPerTrajectory << ",\"perturbedD2S3\":"
           << kRootsPerTrajectory << "},\n"
           << "  \"fittingRollInWork\": " << rollInWork(fitting_games)
           << ",\n  \"heldoutRollInWork\": "
           << rollInWork(heldout_games)
           << ",\n  \"continuation\": {\"tapes\":"
           << base::kContinuationTapes << ",\"horizon\":"
           << base::kContinuationHorizon
           << ",\"alignedNextDiscByMove\":true,"
              "\"alignedRevealByMoveAndEvent\":true,"
              "\"depth1Strata3Tapes\":"
           << base::kContinuationTapes - base::kDepthTwoTapes
           << ",\"depth2Strata3Tapes\":" << base::kDepthTwoTapes
           << ",\"return\":\"moves + score / 7000\"},\n"
           << "  \"fittingDataset\": ";
  base::writeDatasetSummary(artifact, fitting_summary);
  artifact << ",\n  \"heldoutDataset\": ";
  base::writeDatasetSummary(artifact, heldout_summary);
  artifact << ",\n  \"fittingDuplicateRoots\": "
           << fitting.duplicate_roots
           << ",\n  \"heldoutDuplicateRoots\": "
           << heldout.duplicate_roots
           << ",\n  \"overlapGroupsRemoved\": "
           << purge.overlapping_groups
           << ",\n  \"overlapStatesFound\": "
           << purge.overlapping_states
           << ",\n  \"features\": {\"count\":" << kFeatureCount
           << ",\"kind\":\"regularized public action-delta\"},\n"
           << "  \"architectures\": {\"linearParameterBytes\":"
           << learning.linear.parameterBytes()
           << ",\"tinyHidden\":" << kTinyHidden
           << ",\"tinyParameterBytes\":"
           << learning.tiny.parameterBytes() << "},\n"
           << "  \"learningCurve\": ";
  writeCurve(artifact, learning.curve);
  artifact << ",\n  \"heldoutExactD3\": ";
  writeRankingMetrics(artifact, exact.metrics);
  artifact << ",\n  \"heldoutExactWork\": " << exact.work
           << ",\n  \"heldoutExactNodes\": " << exact.nodes
           << ",\n  \"heldoutPeakCacheEntries\": "
           << exact.peak_cache_entries
           << ",\n  \"selectedArchitecture\": \""
           << architectureName(architecture)
           << "\",\n  \"heldoutGate\": {\"requiredTop1\":"
           << kRequiredTopOne << ",\"requiredPairwise\":"
           << kRequiredPairwise
           << ",\"requiredMoveRegretImprovement\":"
           << kRequiredMoveRegretImprovement
           << ",\"observedMoveRegretImprovement\":"
           << move_regret_improvement << ",\"passed\":"
           << (heldout_passed ? "true" : "false") << "},\n"
           << "  \"screenSeedStart\": " << kScreenStart
           << ",\n  \"screen\": ";
  if (!heldout_passed) {
    artifact << "null";
  } else {
    artifact << "{\"exactD3\":";
    base::writePolicySummary(artifact, screen_baseline);
    artifact << ",\"candidate\":";
    base::writePolicySummary(artifact, screen_candidate);
    artifact << ",\"paired\":";
    base::writePairedSummary(artifact, screen_paired);
    artifact << ",\"exactTrajectories\":";
    base::writeTrajectories(artifact, screen.baseline);
    artifact << ",\"candidateTrajectories\":";
    base::writeTrajectories(artifact, screen.candidate);
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
    base::writePolicySummary(artifact, confirmation_baseline);
    artifact << ",\"candidate\":";
    base::writePolicySummary(artifact, confirmation_candidate);
    artifact << ",\"paired\":";
    base::writePairedSummary(artifact, confirmation_paired);
    artifact << ",\"exactTrajectories\":";
    base::writeTrajectories(artifact, confirmation.baseline);
    artifact << ",\"candidateTrajectories\":";
    base::writeTrajectories(artifact, confirmation.candidate);
    artifact << '}';
  }
  artifact << ",\n  \"confirmed\": "
           << (confirmed ? "true" : "false")
           << ",\n  \"decision\": \"" << decision
           << "\",\n  \"model\": \"" << options.model
           << "\",\n  \"peakRssBytes\": " << base::peakRssBytes()
           << ",\n  \"elapsedSeconds\": " << elapsed_seconds << "\n}\n";
  if (!artifact) {
    throw std::runtime_error("could not write scaled sibling artifact");
  }

  output << std::fixed << std::setprecision(4)
         << "SCALED_SIBLING_RESULT {\"fittingGroups\":"
         << fitting.groups.size() << ",\"heldoutGroups\":"
         << heldout.groups.size() << ",\"exactTop1\":"
         << exact.metrics.top1_accuracy << ",\"exactPairwise\":"
         << exact.metrics.pairwise_accuracy << ",\"linearTop1\":"
         << linear_heldout.top1_accuracy << ",\"linearPairwise\":"
         << linear_heldout.pairwise_accuracy << ",\"tinyTop1\":"
         << tiny_heldout.top1_accuracy << ",\"tinyPairwise\":"
         << tiny_heldout.pairwise_accuracy << ",\"architecture\":\""
         << architectureName(architecture)
         << "\",\"moveRegretImprovement\":"
         << move_regret_improvement << ",\"heldoutPassed\":"
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

}  // namespace drop7::scaled_sibling_advantage

int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string(argv[1]) == "--self-test") {
      return drop7::scaled_sibling_advantage::selfTest(std::cout) ? 0 : 1;
    }
    if (argc >= 2 && std::string(argv[1]) == "--run") {
      const auto options =
          drop7::scaled_sibling_advantage::parseOptions(argc, argv, 2);
      return drop7::scaled_sibling_advantage::run(options, std::cout);
    }
    std::cerr << "usage: drop7_scaled_sibling_advantage_lab --self-test | "
                 "--run [--artifact PATH] [--model PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_scaled_sibling_advantage_lab: " << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
}
