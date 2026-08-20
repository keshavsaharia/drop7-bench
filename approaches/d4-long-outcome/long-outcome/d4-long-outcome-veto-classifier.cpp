#define DROP7_D2_LONG_OUTCOME_FEATURE_AUDIT_LIBRARY
#include "d2-long-outcome-feature-audit.cpp"
#undef DROP7_D2_LONG_OUTCOME_FEATURE_AUDIT_LIBRARY

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Trains a conservative development-only veto classifier on a fixed, joined
// 432-root corpus.  Exact public D4 is the immutable fallback.  This executable
// reads no gameplay seed, new root, or new label family.
namespace drop7::d4_long_outcome_veto_classifier {

namespace data = drop7::d2_long_outcome_feature_audit;
namespace prior = drop7::d2_long_outcome_ranker;
namespace base = drop7::scaled_d4_distill;
using Clock = std::chrono::steady_clock;

constexpr int kFolds = 6;
constexpr int kFeatures = 9;
constexpr int kHeadEpochs = 40;
constexpr int kClassifierEpochs = 500;
constexpr double kClassifierLearningRate = 0.03;
constexpr double kClassifierL2 = 0.01;
constexpr double kSwitchProbability = 0.90;
constexpr double kMaximumD4QLoss = static_cast<double>(kLevelBonus);
constexpr double kMinimumMaterialMeanGain = 10'000.0;
constexpr double kPredictedSurvivalSlack = 0.02;
constexpr double kPredictedClearSlack = 0.02;
constexpr double kT975Df6 = 2.446912;
constexpr std::uint64_t kMaximumCombinedCheckpointBytes = 256u * 1024u;
constexpr std::uint64_t kMaximumRssBytes = 256u * 1024u * 1024u;

// Preregistered architecture-development gate.  A zero-switch classifier
// cannot pass: coverage and active-fold requirements are explicit.
constexpr double kMinimumPrecision = 0.80;
constexpr double kMinimumCoverage = 0.20;
constexpr double kMinimumMeanSwitchGain = 10'000.0;
constexpr double kMinimumScenarioQ10 = -7'000.0;
constexpr double kMinimumFallbackRate = 0.85;
constexpr int kMinimumSwitches = 12;
constexpr int kMinimumActiveFolds = 4;
constexpr int kMinimumStableFolds = 4;
constexpr double kMinimumHalfPrecision = 2.0 / 3.0;

static_assert(kFolds == data::kFolds && kHeadEpochs == 40);
static_assert(kSwitchProbability == 0.90);
static_assert(kMaximumD4QLoss == 7'000.0);
static_assert(kMinimumStableFolds <= kFolds);

struct Options {
  std::string labels = "/tmp/drop7-d2-long-outcome-labels.jsonl";
  std::string d4_source = "/tmp/drop7-scaled-d4-distill-labels.jsonl";
  std::string derived =
      "/tmp/drop7-d2-long-outcome-feature-derived.jsonl";
  std::string head_checkpoint =
      "/tmp/drop7-d2-long-outcome-multihead-nnue.bin";
  std::string output =
      "/tmp/drop7-d4-long-outcome-veto-classifier.json";
  std::string checkpoint =
      "/tmp/drop7-d4-long-outcome-veto-classifier.bin";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string flag = argv[index];
    if (flag == "--labels") result.labels = argv[index + 1];
    else if (flag == "--d4-source") result.d4_source = argv[index + 1];
    else if (flag == "--derived") result.derived = argv[index + 1];
    else if (flag == "--head-checkpoint") {
      result.head_checkpoint = argv[index + 1];
    }
    else if (flag == "--output") result.output = argv[index + 1];
    else if (flag == "--checkpoint") result.checkpoint = argv[index + 1];
    else throw std::invalid_argument("unknown option " + flag);
  }
  return result;
}

double doubleAfter(std::string_view line, std::string_view marker) {
  const std::size_t found = line.find(marker);
  if (found == std::string_view::npos) {
    throw std::runtime_error("missing derived floating field");
  }
  std::size_t cursor = found + marker.size();
  return data::parseDouble(line, cursor);
}

template <std::size_t Size>
std::array<double, Size> doublesAfter(std::string_view line,
                                      std::string_view marker) {
  const std::size_t found = line.find(marker);
  if (found == std::string_view::npos) {
    throw std::runtime_error("missing derived numeric vector");
  }
  std::size_t cursor = found + marker.size();
  std::array<double, Size> result{};
  for (std::size_t index = 0; index < Size; ++index) {
    data::skipSeparators(line, cursor);
    result[index] = data::parseDouble(line, cursor);
  }
  return result;
}

template <std::size_t Size>
std::array<int, Size> integersAfter(std::string_view line,
                                    std::string_view marker) {
  const std::size_t found = line.find(marker);
  if (found == std::string_view::npos) {
    throw std::runtime_error("missing derived integer vector");
  }
  std::size_t cursor = found + marker.size();
  std::array<int, Size> result{};
  for (std::size_t index = 0; index < Size; ++index) {
    data::skipSeparators(line, cursor);
    const std::string owned(line);
    char* end = nullptr;
    const long value = std::strtol(owned.c_str() + cursor, &end, 10);
    if (end == owned.c_str() + cursor ||
        value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) {
      throw std::runtime_error("invalid derived integer vector");
    }
    result[index] = static_cast<int>(value);
    cursor = static_cast<std::size_t>(end - owned.c_str());
  }
  return result;
}

template <std::size_t Size>
std::array<bool, Size> booleansAfter(std::string_view line,
                                     std::string_view marker) {
  const std::size_t found = line.find(marker);
  if (found == std::string_view::npos) {
    throw std::runtime_error("missing derived boolean vector");
  }
  std::size_t cursor = found + marker.size();
  std::array<bool, Size> result{};
  for (std::size_t index = 0; index < Size; ++index) {
    data::skipSeparators(line, cursor);
    if (line.substr(cursor, 4) == "true") {
      result[index] = true;
      cursor += 4;
    } else if (line.substr(cursor, 5) == "false") {
      result[index] = false;
      cursor += 5;
    } else {
      throw std::runtime_error("invalid derived boolean vector");
    }
  }
  return result;
}

void fillDerivedRoot(std::string_view line, data::AuditRoot& root) {
  const int game = base::integerAfter(line, "\"game\":");
  const int move = base::integerAfter(line, "\"moveInSourceGame\":");
  if (game != root.stored.label.game ||
      move != root.stored.label.move_in_game ||
      line.find("\"board\":\"" +
                prior::encodedBoard(root.stored.label.board) + "\"") ==
          std::string_view::npos ||
      base::integerAfter(line, "\"d4Action\":") !=
          root.stored.d4.labeled_action) {
    throw std::runtime_error("derived/root join order changed");
  }
  const double pre = doubleAfter(line, "\"preLadderEnergy\":");
  std::size_t cursor = line.find("\"actions\":[");
  if (cursor == std::string_view::npos) {
    throw std::runtime_error("derived actions missing");
  }
  cursor += std::string_view("\"actions\":[").size();
  for (int action = 0; action < kBoardSize; ++action) {
    data::skipSeparators(line, cursor);
    if (!root.stored.label.legal[action]) {
      if (line.substr(cursor, 4) != "null") {
        throw std::runtime_error("derived illegal action mismatch");
      }
      cursor += 4;
      continue;
    }
    if (cursor >= line.size() || line[cursor] != '{') {
      throw std::runtime_error("derived action object missing");
    }
    const std::size_t end = line.find('}', cursor);
    if (end == std::string_view::npos) {
      throw std::runtime_error("derived action object unterminated");
    }
    const std::string_view object = line.substr(cursor, end - cursor + 1);
    data::ActionAux& aux = root.actions[action];
    aux.pre_ladder = pre;
    aux.post_ladder = doublesAfter<prior::kScenarios>(
        object, "\"postLadderByScenario\":[");
    aux.expected_post_ladder =
        doubleAfter(object, "\"expectedPostLadder\":");
    aux.ladder_delta = doubleAfter(object, "\"ladderDelta\":");
    const auto returns = doublesAfter<prior::kScenarios>(
        object, "\"scenarioReturns\":[");
    aux.scenario_survived = booleansAfter<prior::kScenarios>(
        object, "\"scenarioSurvived\":[");
    aux.scenario_clears = integersAfter<prior::kScenarios>(
        object, "\"scenarioNumberedClears\":[");
    aux.survival = doubleAfter(object, "\"survivalRate\":");
    aux.raw_mean_clears =
        doubleAfter(object, "\"rawMeanNumberedClears\":");
    aux.mean_clears =
        doubleAfter(object, "\"normalizedMeanNumberedClears\":");
    aux.downside = doubleAfter(object, "\"normalizedDownside\":");
    aux.variance = doubleAfter(object, "\"normalizedVariance\":");
    for (int scenario = 0; scenario < prior::kScenarios; ++scenario) {
      if (returns[scenario] != root.stored.returns[action][scenario]) {
        throw std::runtime_error("derived scenario return mismatch");
      }
    }
    cursor = end + 1;
  }
}

struct JoinedCorpus {
  std::vector<data::AuditRoot> fitting;
  std::vector<data::AuditRoot> heldout;
};

JoinedCorpus loadJoined(const Options& options) {
  data::StoredCorpus stored = data::loadCorpus(options.labels);
  data::joinD4(stored, options.d4_source);
  JoinedCorpus result;
  result.fitting.reserve(stored.fitting.size());
  result.heldout.reserve(stored.heldout.size());
  for (data::StoredRoot& root : stored.fitting) {
    data::AuditRoot audit;
    audit.stored = std::move(root);
    audit.prepared = base::prepare(audit.stored.label);
    result.fitting.push_back(std::move(audit));
  }
  for (data::StoredRoot& root : stored.heldout) {
    data::AuditRoot audit;
    audit.stored = std::move(root);
    audit.prepared = base::prepare(audit.stored.label);
    result.heldout.push_back(std::move(audit));
  }

  std::ifstream input(options.derived);
  if (!input) throw std::runtime_error("could not open joined derived corpus");
  std::string line;
  if (!std::getline(input, line) ||
      line.find("drop7-long-outcome-derived-features-v1") ==
          std::string::npos ||
      line.find("\"newRoots\":0") == std::string::npos ||
      line.find("\"newGameSeeds\":0") == std::string::npos) {
    throw std::runtime_error("derived corpus metadata mismatch");
  }
  std::size_t fitting_index = 0;
  std::size_t heldout_index = 0;
  while (std::getline(input, line)) {
    if (line.find("\"split\":\"fitting\"") != std::string::npos) {
      if (fitting_index >= result.fitting.size()) {
        throw std::runtime_error("too many derived fitting roots");
      }
      fillDerivedRoot(line, result.fitting[fitting_index++]);
    } else if (line.find(
                   "\"split\":\"old-heldout-architecture-development\"") !=
               std::string::npos) {
      if (heldout_index >= result.heldout.size()) {
        throw std::runtime_error("too many derived heldout roots");
      }
      fillDerivedRoot(line, result.heldout[heldout_index++]);
    } else {
      throw std::runtime_error("unknown derived split");
    }
  }
  if (fitting_index != result.fitting.size() ||
      heldout_index != result.heldout.size()) {
    throw std::runtime_error("derived corpus root count mismatch");
  }
  return result;
}

using HeadVector = std::array<double, data::kHeads>;
using RootHeads = std::array<HeadVector, kBoardSize>;
using HeadPredictions = std::vector<RootHeads>;

HeadPredictions predictHeads(const data::NeuralModel& model,
                             const std::vector<data::AuditRoot>& roots) {
  HeadPredictions result(roots.size());
  for (std::size_t index = 0; index < roots.size(); ++index) {
    for (int action = 0; action < kBoardSize; ++action) {
      if (!roots[index].stored.label.legal[action]) continue;
      result[index][action] = data::forward(
          model, roots[index], action).heads;
    }
  }
  return result;
}

struct ExactAlternative {
  bool eligible = false;
  double d4_q_loss = 0.0;
  double mean_return_gain = 0.0;
  double paired_lower95 = 0.0;
  int survival_delta = 0;
  double clear_delta = 0.0;
  std::array<double, prior::kScenarios> paired_returns{};
};

double d4QLoss(const data::AuditRoot& root, int alternative) {
  const int fallback = root.stored.d4.labeled_action;
  if (fallback < 0 || !root.stored.label.legal[fallback] ||
      alternative < 0 || alternative >= kBoardSize ||
      !root.stored.label.legal[alternative] || alternative == fallback) {
    throw std::invalid_argument("invalid veto alternative");
  }
  return root.stored.d4.q[fallback] - root.stored.d4.q[alternative];
}

ExactAlternative exactAlternative(const data::AuditRoot& root,
                                  int alternative) {
  const int fallback = root.stored.d4.labeled_action;
  ExactAlternative result;
  result.d4_q_loss = d4QLoss(root, alternative);
  double squares = 0.0;
  int alternative_survivors = 0;
  int fallback_survivors = 0;
  for (int scenario = 0; scenario < prior::kScenarios; ++scenario) {
    const double difference =
        root.stored.returns[alternative][scenario] -
        root.stored.returns[fallback][scenario];
    result.paired_returns[scenario] = difference;
    result.mean_return_gain += difference / prior::kScenarios;
    alternative_survivors +=
        root.actions[alternative].scenario_survived[scenario];
    fallback_survivors += root.actions[fallback].scenario_survived[scenario];
  }
  for (const double difference : result.paired_returns) {
    const double centered = difference - result.mean_return_gain;
    squares += centered * centered;
  }
  const double deviation =
      std::sqrt(squares / static_cast<double>(prior::kScenarios - 1));
  result.paired_lower95 =
      result.mean_return_gain -
      kT975Df6 * deviation / std::sqrt(static_cast<double>(prior::kScenarios));
  result.survival_delta = alternative_survivors - fallback_survivors;
  result.clear_delta =
      root.actions[alternative].raw_mean_clears -
      root.actions[fallback].raw_mean_clears;
  result.eligible =
      result.d4_q_loss <= kMaximumD4QLoss &&
      result.mean_return_gain >= kMinimumMaterialMeanGain &&
      result.paired_lower95 > 0.0 && result.survival_delta >= 0 &&
      result.clear_delta >= 0.0;
  return result;
}

int maximumHeight(const Board& board) {
  int maximum = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    int height = 0;
    for (int row = 0; row < kBoardSize; ++row) {
      height += board[indexOf(row, column)] != kEmpty;
    }
    maximum = std::max(maximum, height);
  }
  return maximum;
}

std::array<double, kFeatures> rawFeatures(
    const data::AuditRoot& root, const RootHeads& predicted,
    int alternative) {
  const int fallback = root.stored.d4.labeled_action;
  const double predicted_alt_return =
      root.prepared.d2[alternative] +
      predicted[alternative][data::kMeanReturnResidual];
  const double predicted_fallback_return =
      root.prepared.d2[fallback] +
      predicted[fallback][data::kMeanReturnResidual];
  return {{
      d4QLoss(root, alternative) / kMaximumD4QLoss,
      predicted_alt_return - predicted_fallback_return,
      predicted[alternative][data::kSurvival] -
          predicted[fallback][data::kSurvival],
      predicted[alternative][data::kNumberedClears] -
          predicted[fallback][data::kNumberedClears],
      predicted[alternative][data::kDownside] -
          predicted[fallback][data::kDownside],
      predicted[fallback][data::kVariance] -
          predicted[alternative][data::kVariance],
      root.prepared.d2[alternative] - root.prepared.d2[fallback],
      root.prepared.immediate[alternative] -
          root.prepared.immediate[fallback],
      static_cast<double>(maximumHeight(root.stored.label.board)) /
          kBoardSize,
  }};
}

struct Classifier {
  std::array<double, kFeatures> mean{};
  std::array<double, kFeatures> scale{};
  std::array<double, kFeatures> weight{};
  double bias = 0.0;
};

double sigmoid(double value) {
  if (value >= 0.0) return 1.0 / (1.0 + std::exp(-value));
  const double exponential = std::exp(value);
  return exponential / (1.0 + exponential);
}

double probability(const Classifier& model,
                   const std::array<double, kFeatures>& raw) {
  double logit = model.bias;
  for (int feature = 0; feature < kFeatures; ++feature) {
    logit += model.weight[feature] *
             (raw[feature] - model.mean[feature]) * model.scale[feature];
  }
  return sigmoid(logit);
}

struct TrainingRow {
  std::array<double, kFeatures> feature{};
  bool positive = false;
};

std::vector<TrainingRow> classifierRows(
    const std::vector<data::AuditRoot>& roots,
    const HeadPredictions& predictions,
    const std::vector<bool>& included) {
  if (roots.size() != predictions.size() || roots.size() != included.size()) {
    throw std::invalid_argument("classifier row dimensions mismatch");
  }
  std::vector<TrainingRow> result;
  for (std::size_t index = 0; index < roots.size(); ++index) {
    if (!included[index]) continue;
    const data::AuditRoot& root = roots[index];
    const int fallback = root.stored.d4.labeled_action;
    for (int action = 0; action < kBoardSize; ++action) {
      if (!root.stored.label.legal[action] || action == fallback) continue;
      result.push_back({rawFeatures(root, predictions[index], action),
                        exactAlternative(root, action).eligible});
    }
  }
  return result;
}

Classifier trainClassifier(const std::vector<TrainingRow>& rows) {
  if (rows.empty()) throw std::runtime_error("empty classifier training set");
  int positives = 0;
  for (const TrainingRow& row : rows) positives += row.positive;
  const int negatives = static_cast<int>(rows.size()) - positives;
  if (positives == 0 || negatives == 0) {
    throw std::runtime_error("classifier training fold has one class");
  }
  Classifier model;
  for (const TrainingRow& row : rows) {
    for (int feature = 0; feature < kFeatures; ++feature) {
      model.mean[feature] += row.feature[feature] / rows.size();
    }
  }
  for (const TrainingRow& row : rows) {
    for (int feature = 0; feature < kFeatures; ++feature) {
      const double centered = row.feature[feature] - model.mean[feature];
      model.scale[feature] += centered * centered / rows.size();
    }
  }
  for (double& scale : model.scale) {
    scale = 1.0 / std::max(1.0e-6, std::sqrt(scale));
  }

  std::array<double, kFeatures> first{};
  std::array<double, kFeatures> second{};
  double first_bias = 0.0;
  double second_bias = 0.0;
  for (int epoch = 1; epoch <= kClassifierEpochs; ++epoch) {
    std::array<double, kFeatures> gradient{};
    double bias_gradient = 0.0;
    for (const TrainingRow& row : rows) {
      const double prediction = probability(model, row.feature);
      const double class_weight =
          row.positive ? 0.5 / positives : 0.5 / negatives;
      const double derivative =
          class_weight * (prediction - (row.positive ? 1.0 : 0.0));
      bias_gradient += derivative;
      for (int feature = 0; feature < kFeatures; ++feature) {
        gradient[feature] +=
            derivative * (row.feature[feature] - model.mean[feature]) *
            model.scale[feature];
      }
    }
    const double first_correction = 1.0 - std::pow(0.9, epoch);
    const double second_correction = 1.0 - std::pow(0.999, epoch);
    for (int feature = 0; feature < kFeatures; ++feature) {
      gradient[feature] += kClassifierL2 * model.weight[feature];
      first[feature] = 0.9 * first[feature] + 0.1 * gradient[feature];
      second[feature] =
          0.999 * second[feature] +
          0.001 * gradient[feature] * gradient[feature];
      model.weight[feature] -=
          kClassifierLearningRate *
          (first[feature] / first_correction) /
          (std::sqrt(second[feature] / second_correction) + 1.0e-8);
    }
    first_bias = 0.9 * first_bias + 0.1 * bias_gradient;
    second_bias =
        0.999 * second_bias + 0.001 * bias_gradient * bias_gradient;
    model.bias -=
        kClassifierLearningRate * (first_bias / first_correction) /
        (std::sqrt(second_bias / second_correction) + 1.0e-8);
  }
  return model;
}

struct Decision {
  int fallback = -1;
  int selected = -1;
  double probability = 0.0;
  bool switched = false;
};

Decision choose(const data::AuditRoot& root, const RootHeads& predicted,
                const Classifier& classifier) {
  Decision result;
  result.fallback = root.stored.d4.labeled_action;
  result.selected = result.fallback;
  for (int action = 0; action < kBoardSize; ++action) {
    if (!root.stored.label.legal[action] || action == result.fallback) continue;
    if (d4QLoss(root, action) > kMaximumD4QLoss ||
        predicted[action][data::kSurvival] + kPredictedSurvivalSlack <
            predicted[result.fallback][data::kSurvival] ||
        predicted[action][data::kNumberedClears] + kPredictedClearSlack <
            predicted[result.fallback][data::kNumberedClears]) {
      continue;
    }
    const double candidate_probability =
        probability(classifier, rawFeatures(root, predicted, action));
    if (candidate_probability < kSwitchProbability) continue;
    const double candidate_return =
        root.prepared.d2[action] +
        predicted[action][data::kMeanReturnResidual];
    const double selected_return =
        root.prepared.d2[result.selected] +
        predicted[result.selected][data::kMeanReturnResidual];
    if (!result.switched || candidate_probability > result.probability ||
        (candidate_probability == result.probability &&
         candidate_return > selected_return)) {
      result.selected = action;
      result.probability = candidate_probability;
      result.switched = true;
    }
  }
  return result;
}

double quantile(std::vector<double> values, double probability_value) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const double position =
      probability_value * static_cast<double>(values.size() - 1);
  const std::size_t lower = static_cast<std::size_t>(std::floor(position));
  const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
  const double fraction = position - static_cast<double>(lower);
  return values[lower] * (1.0 - fraction) + values[upper] * fraction;
}

struct VetoMetrics {
  int roots = 0;
  int positive_roots = 0;
  int switches = 0;
  int true_switches = 0;
  int survival_nonloss = 0;
  int clear_nonloss = 0;
  double mean_return_gain_sum = 0.0;
  double mean_d4_q_loss_sum = 0.0;
  std::vector<double> scenario_differences;
};

void addMetrics(VetoMetrics& target, const VetoMetrics& source) {
  target.roots += source.roots;
  target.positive_roots += source.positive_roots;
  target.switches += source.switches;
  target.true_switches += source.true_switches;
  target.survival_nonloss += source.survival_nonloss;
  target.clear_nonloss += source.clear_nonloss;
  target.mean_return_gain_sum += source.mean_return_gain_sum;
  target.mean_d4_q_loss_sum += source.mean_d4_q_loss_sum;
  target.scenario_differences.insert(target.scenario_differences.end(),
                                     source.scenario_differences.begin(),
                                     source.scenario_differences.end());
}

double precision(const VetoMetrics& value) {
  return value.switches > 0
             ? static_cast<double>(value.true_switches) / value.switches
             : 0.0;
}

double coverage(const VetoMetrics& value) {
  return value.positive_roots > 0
             ? static_cast<double>(value.true_switches) / value.positive_roots
             : 0.0;
}

double fallbackRate(const VetoMetrics& value) {
  return static_cast<double>(value.roots - value.switches) / value.roots;
}

double meanReturnGain(const VetoMetrics& value) {
  return value.switches > 0
             ? value.mean_return_gain_sum / value.switches
             : 0.0;
}

double meanD4QLoss(const VetoMetrics& value) {
  return value.switches > 0 ? value.mean_d4_q_loss_sum / value.switches : 0.0;
}

double scenarioQ10(const VetoMetrics& value) {
  return quantile(value.scenario_differences, 0.10);
}

double survivalRetention(const VetoMetrics& value) {
  return value.switches > 0
             ? static_cast<double>(value.survival_nonloss) / value.switches
             : 0.0;
}

double clearRetention(const VetoMetrics& value) {
  return value.switches > 0
             ? static_cast<double>(value.clear_nonloss) / value.switches
             : 0.0;
}

template <typename IncludeFunction>
VetoMetrics evaluate(const std::vector<data::AuditRoot>& roots,
                     const HeadPredictions& predictions,
                     const Classifier& classifier, IncludeFunction include) {
  VetoMetrics result;
  for (std::size_t index = 0; index < roots.size(); ++index) {
    const data::AuditRoot& root = roots[index];
    if (!include(root)) continue;
    ++result.roots;
    const int fallback = root.stored.d4.labeled_action;
    bool has_positive = false;
    for (int action = 0; action < kBoardSize; ++action) {
      if (!root.stored.label.legal[action] || action == fallback) continue;
      has_positive = has_positive || exactAlternative(root, action).eligible;
    }
    result.positive_roots += has_positive;
    const Decision decision = choose(root, predictions[index], classifier);
    if (!decision.switched) continue;
    ++result.switches;
    const ExactAlternative selected =
        exactAlternative(root, decision.selected);
    result.true_switches += selected.eligible;
    result.survival_nonloss += selected.survival_delta >= 0;
    result.clear_nonloss += selected.clear_delta >= 0.0;
    result.mean_return_gain_sum += selected.mean_return_gain;
    result.mean_d4_q_loss_sum += selected.d4_q_loss;
    result.scenario_differences.insert(result.scenario_differences.end(),
                                       selected.paired_returns.begin(),
                                       selected.paired_returns.end());
  }
  if (result.roots == 0) throw std::runtime_error("empty veto metric range");
  return result;
}

struct Gate {
  bool switches = false;
  bool precision = false;
  bool coverage = false;
  bool mean_gain = false;
  bool downside = false;
  bool fallback = false;
  bool survival = false;
  bool clears = false;
  int active_folds = 0;
  int stable_folds = 0;
  bool halves = false;
  bool passed = false;
};

Gate gate(const VetoMetrics& all,
          const std::array<VetoMetrics, kFolds>& folds,
          const std::array<VetoMetrics, 2>* halves,
          int minimum_switches) {
  Gate result;
  result.switches = all.switches >= minimum_switches;
  result.precision = precision(all) >= kMinimumPrecision;
  result.coverage = coverage(all) >= kMinimumCoverage;
  result.mean_gain = meanReturnGain(all) >= kMinimumMeanSwitchGain;
  result.downside = scenarioQ10(all) >= kMinimumScenarioQ10;
  result.fallback = fallbackRate(all) >= kMinimumFallbackRate;
  result.survival = survivalRetention(all) >= 0.90;
  result.clears = clearRetention(all) >= 0.90;
  for (const VetoMetrics& fold : folds) {
    if (fold.switches == 0) continue;
    ++result.active_folds;
    result.stable_folds +=
        precision(fold) >= kMinimumHalfPrecision &&
        meanReturnGain(fold) > 0.0 &&
        scenarioQ10(fold) >= kMinimumScenarioQ10 &&
        fallbackRate(fold) >= kMinimumFallbackRate;
  }
  result.halves = true;
  if (halves != nullptr) {
    for (const VetoMetrics& half : *halves) {
      result.halves =
          result.halves && half.switches > 0 &&
          precision(half) >= kMinimumHalfPrecision &&
          meanReturnGain(half) > 0.0 &&
          scenarioQ10(half) >= kMinimumScenarioQ10 &&
          fallbackRate(half) >= kMinimumFallbackRate;
    }
  }
  result.passed =
      result.switches && result.precision && result.coverage &&
      result.mean_gain && result.downside && result.fallback &&
      result.survival && result.clears &&
      result.active_folds >= kMinimumActiveFolds &&
      result.stable_folds >= kMinimumStableFolds && result.halves;
  return result;
}

void writeMetrics(std::ostream& output, const VetoMetrics& value) {
  output << std::setprecision(10) << "{\"roots\":" << value.roots
         << ",\"rootsWithEligibleAlternative\":" << value.positive_roots
         << ",\"switches\":" << value.switches
         << ",\"trueEligibleSwitches\":" << value.true_switches
         << ",\"switchPrecision\":" << precision(value)
         << ",\"eligibleRootCoverage\":" << coverage(value)
         << ",\"fallbackRate\":" << fallbackRate(value)
         << ",\"meanPairedReturnGain\":" << meanReturnGain(value)
         << ",\"pairedScenarioQ10\":" << scenarioQ10(value)
         << ",\"survivalNonlossRate\":" << survivalRetention(value)
         << ",\"clearNonlossRate\":" << clearRetention(value)
         << ",\"meanD4RootQLoss\":" << meanD4QLoss(value) << '}';
}

void writeGate(std::ostream& output, const Gate& value) {
  output << "{\"passed\":" << (value.passed ? "true" : "false")
         << ",\"minimumSwitches\":" << (value.switches ? "true" : "false")
         << ",\"precision\":" << (value.precision ? "true" : "false")
         << ",\"coverage\":" << (value.coverage ? "true" : "false")
         << ",\"meanGain\":" << (value.mean_gain ? "true" : "false")
         << ",\"downside\":" << (value.downside ? "true" : "false")
         << ",\"fallbackRetention\":"
         << (value.fallback ? "true" : "false")
         << ",\"survivalRetention\":"
         << (value.survival ? "true" : "false")
         << ",\"clearRetention\":" << (value.clears ? "true" : "false")
         << ",\"activeFolds\":" << value.active_folds
         << ",\"stableFolds\":" << value.stable_folds
         << ",\"bothHalves\":" << (value.halves ? "true" : "false")
         << '}';
}

void copyFold(const HeadPredictions& source, HeadPredictions& target,
              const std::vector<data::AuditRoot>& roots, int fold) {
  if (source.size() != target.size() || source.size() != roots.size()) {
    throw std::invalid_argument("head prediction copy dimensions mismatch");
  }
  for (std::size_t index = 0; index < roots.size(); ++index) {
    if (roots[index].stored.label.game % kFolds == fold) {
      target[index] = source[index];
    }
  }
}

struct Audit {
  VetoMetrics fitting_cv{};
  std::array<VetoMetrics, kFolds> fitting_folds{};
  HeadPredictions fitting_oof_heads;
  Classifier final_classifier{};
  data::NeuralModel final_head_model{};
  VetoMetrics heldout{};
  std::array<VetoMetrics, kFolds> heldout_folds{};
  std::array<VetoMetrics, 2> heldout_halves{};
};

Audit runNested(const std::vector<data::AuditRoot>& fitting,
                const std::vector<data::AuditRoot>& heldout) {
  Audit result;
  result.fitting_oof_heads.resize(fitting.size());
  for (int outer = 0; outer < kFolds; ++outer) {
    HeadPredictions nested_training_heads(fitting.size());
    for (int inner = 0; inner < kFolds; ++inner) {
      if (inner == outer) continue;
      const data::NeuralModel head_model = data::trainModel(
          fitting,
          [outer, inner](const data::AuditRoot& root) {
            const int fold = root.stored.label.game % kFolds;
            return fold != outer && fold != inner;
          },
          kHeadEpochs,
          0x5648'0000u + static_cast<std::uint32_t>(outer * kFolds + inner));
      copyFold(predictHeads(head_model, fitting), nested_training_heads,
               fitting, inner);
    }
    const data::NeuralModel outer_head = data::trainModel(
        fitting,
        [outer](const data::AuditRoot& root) {
          return root.stored.label.game % kFolds != outer;
        },
        kHeadEpochs, 0x564f'0000u + static_cast<std::uint32_t>(outer));
    const HeadPredictions outer_predictions =
        predictHeads(outer_head, fitting);
    copyFold(outer_predictions, result.fitting_oof_heads, fitting, outer);

    std::vector<bool> classifier_training(fitting.size());
    for (std::size_t index = 0; index < fitting.size(); ++index) {
      classifier_training[index] =
          fitting[index].stored.label.game % kFolds != outer;
    }
    const Classifier classifier = trainClassifier(
        classifierRows(fitting, nested_training_heads, classifier_training));
    result.fitting_folds[outer] = evaluate(
        fitting, outer_predictions, classifier,
        [outer](const data::AuditRoot& root) {
          return root.stored.label.game % kFolds == outer;
        });
    addMetrics(result.fitting_cv, result.fitting_folds[outer]);
  }

  std::vector<bool> all_fitting(fitting.size(), true);
  result.final_classifier = trainClassifier(classifierRows(
      fitting, result.fitting_oof_heads, all_fitting));
  result.final_head_model = data::trainModel(
      fitting, [](const data::AuditRoot&) { return true; }, kHeadEpochs,
      0x4e4e'464eu);
  const HeadPredictions heldout_predictions =
      predictHeads(result.final_head_model, heldout);
  result.heldout = evaluate(
      heldout, heldout_predictions, result.final_classifier,
      [](const data::AuditRoot&) { return true; });
  for (int fold = 0; fold < kFolds; ++fold) {
    result.heldout_folds[fold] = evaluate(
        heldout, heldout_predictions, result.final_classifier,
        [fold](const data::AuditRoot& root) {
          return root.stored.label.game % kFolds == fold;
        });
  }
  for (int half = 0; half < 2; ++half) {
    result.heldout_halves[half] = evaluate(
        heldout, heldout_predictions, result.final_classifier,
        [half](const data::AuditRoot& root) {
          const int middle = base::kHeldoutGames / 2;
          return half == 0 ? root.stored.label.game < middle
                           : root.stored.label.game >= middle;
        });
  }
  return result;
}

std::uint64_t classifierFingerprint(const Classifier& model,
                                    std::uint64_t head_fingerprint) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  const auto consume = [&hash](double value) {
    std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    for (int byte = 0; byte < 8; ++byte) {
      hash ^= bits & 0xffu;
      hash *= 0x0000'0100'0000'01b3ull;
      bits >>= 8u;
    }
  };
  for (const double value : model.mean) consume(value);
  for (const double value : model.scale) consume(value);
  for (const double value : model.weight) consume(value);
  consume(model.bias);
  consume(kSwitchProbability);
  hash ^= head_fingerprint;
  hash *= 0x0000'0100'0000'01b3ull;
  return hash;
}

constexpr std::array<char, 8> kCheckpointMagic{{
    'D', '7', 'V', 'C', 'L', 'F', '1', '\0',
}};

struct CheckpointHeader {
  std::array<char, 8> magic{};
  std::uint32_t features = 0;
  std::uint32_t head_epochs = 0;
  std::uint32_t classifier_epochs = 0;
  std::uint32_t reserved = 0;
  double switch_probability = 0.0;
  std::uint64_t head_fingerprint = 0;
  std::uint64_t fingerprint = 0;
};

void writeCheckpoint(const std::string& path, const Classifier& model,
                     std::uint64_t head_fingerprint) {
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("could not write veto checkpoint");
  const CheckpointHeader header{
      kCheckpointMagic, kFeatures, kHeadEpochs, kClassifierEpochs, 0,
      kSwitchProbability, head_fingerprint,
      classifierFingerprint(model, head_fingerprint)};
  output.write(reinterpret_cast<const char*>(&header), sizeof(header));
  output.write(reinterpret_cast<const char*>(&model), sizeof(model));
  if (!output) throw std::runtime_error("veto checkpoint write failed");
}

std::pair<Classifier, std::uint64_t> readCheckpoint(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not read veto checkpoint");
  CheckpointHeader header;
  Classifier model;
  input.read(reinterpret_cast<char*>(&header), sizeof(header));
  input.read(reinterpret_cast<char*>(&model), sizeof(model));
  const bool payload_ok = static_cast<bool>(input);
  char trailing = 0;
  const bool has_trailing = static_cast<bool>(input.read(&trailing, 1));
  if (!payload_ok || !input.eof() || has_trailing ||
      header.magic != kCheckpointMagic || header.features != kFeatures ||
      header.head_epochs != kHeadEpochs ||
      header.classifier_epochs != kClassifierEpochs ||
      header.switch_probability != kSwitchProbability ||
      header.fingerprint !=
          classifierFingerprint(model, header.head_fingerprint)) {
    throw std::runtime_error("invalid veto checkpoint");
  }
  return {model, header.head_fingerprint};
}

std::uint64_t fileBytes(const std::string& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) throw std::runtime_error("could not size veto checkpoint");
  const std::streampos end = input.tellg();
  if (end < 0) throw std::runtime_error("invalid veto checkpoint size");
  return static_cast<std::uint64_t>(end);
}

struct Throughput {
  std::uint64_t roots = 0;
  double seconds = 0.0;
  double roots_per_second = 0.0;
  double checksum = 0.0;
};

Throughput benchmark(const std::vector<data::AuditRoot>& roots,
                     const data::NeuralModel& head,
                     const Classifier& classifier) {
  constexpr int kRepetitions = 100;
  const HeadPredictions predictions = predictHeads(head, roots);
  Throughput result;
  const auto started = Clock::now();
  for (int repetition = 0; repetition < kRepetitions; ++repetition) {
    for (std::size_t index = 0; index < roots.size(); ++index) {
      const Decision decision =
          choose(roots[index], predictions[index], classifier);
      result.checksum +=
          static_cast<double>((decision.selected + 1) * (repetition + 1)) +
          decision.probability;
      ++result.roots;
    }
  }
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  result.roots_per_second = result.roots / result.seconds;
  return result;
}

void writeFolds(std::ostream& output,
                const std::array<VetoMetrics, kFolds>& folds) {
  output << '[';
  for (int fold = 0; fold < kFolds; ++fold) {
    if (fold > 0) output << ',';
    output << "{\"fold\":" << fold << ",\"metrics\":";
    writeMetrics(output, folds[fold]);
    output << '}';
  }
  output << ']';
}

void writeHalves(std::ostream& output,
                 const std::array<VetoMetrics, 2>& halves) {
  output << '[';
  for (int half = 0; half < 2; ++half) {
    if (half > 0) output << ',';
    output << "{\"half\":" << half << ",\"metrics\":";
    writeMetrics(output, halves[half]);
    output << '}';
  }
  output << ']';
}

void writeArtifact(const Options& options, const Audit& audit,
                   const Gate& fitting_gate, const Gate& heldout_gate,
                   std::uint64_t classifier_bytes,
                   std::uint64_t head_bytes,
                   std::uint64_t classifier_fingerprint,
                   std::uint64_t head_fingerprint,
                   const Throughput& throughput, double elapsed_seconds,
                   bool resources_passed) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not write veto artifact");
  const bool passed =
      fitting_gate.passed && heldout_gate.passed && resources_passed;
  output << std::setprecision(10)
         << "{\n  \"experiment\":\"d4-long-outcome-veto-classifier\",\n"
            "  \"status\":\"complete\",\n"
            "  \"evidenceClass\":\"architecture-development-only\",\n"
            "  \"claimBoundary\":\"nested fitting CV and previously burned heldout only; no gameplay or new label collection\",\n"
            "  \"input\":{\"derivedJoinedCorpus\":\""
         << options.derived
         << "\",\"sha256\":\"b75363c1071fb2eb93401dda899b944f93c31b3172c9168899a307d978135c6c\","
            "\"fittingRoots\":288,\"oldHeldoutRoots\":144,"
            "\"newRoots\":0,\"newGameSeeds\":0,\"newTapeDomains\":0,"
            "\"fresh3eSeedsRead\":0,\"validation7dSeedsRead\":0,"
            "\"finalD7SeedsRead\":0},\n"
            "  \"policy\":{\"default\":\"exact public D4 action\","
            "\"switchTarget\":\"alternative with D4-Q loss <=7000, mean paired 25-move gain >=10000, positive t(6) paired lower bound, no survival loss, no mean clear loss\","
            "\"features\":[\"D4-Q loss\",\"predicted return delta\","
            "\"predicted survival delta\",\"predicted clear delta\","
            "\"predicted downside delta\",\"predicted inverse variance delta\","
            "\"exact D2 delta\",\"immediate-score delta\",\"public maximum height\"],"
            "\"classifier\":\"balanced L2 logistic regression\","
            "\"switchProbability\":"
         << kSwitchProbability
         << ",\"predictedSurvivalSlack\":" << kPredictedSurvivalSlack
         << ",\"predictedClearSlack\":" << kPredictedClearSlack
         << ",\"headEpochs\":" << kHeadEpochs
         << ",\"nestedStacking\":\"outer whole-game fold excluded from every head/classifier fit; inner cross-fitted heads train classifier; full outer-train head predicts outer fold\"},\n"
            "  \"frozenGate\":{\"minimumPrecision\":"
         << kMinimumPrecision << ",\"minimumCoverage\":" << kMinimumCoverage
         << ",\"minimumMeanSwitchGain\":" << kMinimumMeanSwitchGain
         << ",\"minimumScenarioQ10\":" << kMinimumScenarioQ10
         << ",\"minimumFallbackRate\":" << kMinimumFallbackRate
         << ",\"minimumFittingSwitches\":" << kMinimumSwitches
         << ",\"minimumOldHeldoutSwitches\":6,"
            "\"minimumActiveFolds\":"
         << kMinimumActiveFolds << ",\"minimumStableFolds\":"
         << kMinimumStableFolds
         << ",\"minimumSurvivalAndClearRetention\":0.9},\n"
            "  \"fittingNestedCV\":{\"all\":";
  writeMetrics(output, audit.fitting_cv);
  output << ",\"folds\":";
  writeFolds(output, audit.fitting_folds);
  output << ",\"gate\":";
  writeGate(output, fitting_gate);
  output << "},\n  \"oldHeldoutArchitectureDevelopment\":{\"reusableFormalEvidence\":false,\"all\":";
  writeMetrics(output, audit.heldout);
  output << ",\"folds\":";
  writeFolds(output, audit.heldout_folds);
  output << ",\"halves\":";
  writeHalves(output, audit.heldout_halves);
  output << ",\"gate\":";
  writeGate(output, heldout_gate);
  output << "},\n  \"deployment\":{\"headCheckpointBytes\":" << head_bytes
         << ",\"classifierCheckpointBytes\":" << classifier_bytes
         << ",\"combinedCheckpointBytes\":"
         << head_bytes + classifier_bytes
         << ",\"combinedLimitBytes\":" << kMaximumCombinedCheckpointBytes
         << ",\"headFingerprintFnv1a64\":\"0x" << std::hex
         << head_fingerprint << "\",\"classifierFingerprintFnv1a64\":\"0x"
         << classifier_fingerprint << std::dec
         << "\",\"vetoRootsPerSecondAfterPreparedHeads\":"
         << throughput.roots_per_second << ",\"benchmarkRoots\":"
         << throughput.roots << ",\"benchmarkSeconds\":"
         << throughput.seconds << ",\"benchmarkChecksum\":"
         << throughput.checksum << "},\n"
            "  \"resourceChecks\":{\"passed\":"
         << (resources_passed ? "true" : "false")
         << ",\"peakRssBytes\":" << prior::peakRssBytes()
         << ",\"rssLimitBytes\":" << kMaximumRssBytes << "},\n"
            "  \"allArchitectureDevelopmentGatesPassed\":"
         << (passed ? "true" : "false")
         << ",\n  \"newDisjointLabelProtocol\":";
  if (passed) {
    output << "{\"proposed\":true,\"executed\":false,"
              "\"freezeBeforeCollection\":true,"
              "\"scope\":\"new disjoint training-only public-root corpus\","
              "\"formalGate\":\"repeat precision/downside/coverage/fallback/fold-stability gates on untouched whole-game families before any gameplay\"}";
  } else {
    output << "{\"proposed\":false,\"executed\":false,"
              "\"reason\":\"conservative veto failed at least one frozen architecture-development gate\"}";
  }
  output << ",\n  \"elapsedSeconds\":" << elapsed_seconds
         << ",\n  \"conclusion\":\""
         << (passed
                 ? "veto merits a separately frozen disjoint label protocol; no collection was performed"
                 : "veto classifier rejected; exact public D4 remains unchanged fallback and no new corpus is warranted")
         << "\"\n}\n";
}

int run(const Options& options, std::ostream& report) {
  const auto started = Clock::now();
  const JoinedCorpus corpus = loadJoined(options);
  const Audit audit = runNested(corpus.fitting, corpus.heldout);
  const data::NeuralModel persisted_head =
      data::readCheckpoint(options.head_checkpoint);
  const std::uint64_t head_fingerprint =
      data::modelFingerprint(audit.final_head_model);
  if (data::modelFingerprint(persisted_head) != head_fingerprint) {
    throw std::runtime_error("full fitting head retrain changed fingerprint");
  }
  writeCheckpoint(options.checkpoint, audit.final_classifier,
                  head_fingerprint);
  const auto restored = readCheckpoint(options.checkpoint);
  if (restored.second != head_fingerprint ||
      classifierFingerprint(restored.first, restored.second) !=
          classifierFingerprint(audit.final_classifier, head_fingerprint)) {
    throw std::runtime_error("veto checkpoint roundtrip failed");
  }
  const Gate fitting_gate =
      gate(audit.fitting_cv, audit.fitting_folds, nullptr, kMinimumSwitches);
  const Gate heldout_gate =
      gate(audit.heldout, audit.heldout_folds, &audit.heldout_halves, 6);
  const std::uint64_t classifier_bytes = fileBytes(options.checkpoint);
  const std::uint64_t head_bytes = data::fileBytes(options.head_checkpoint);
  const Throughput throughput = benchmark(
      corpus.heldout, persisted_head, restored.first);
  const bool resources_passed =
      classifier_bytes + head_bytes <= kMaximumCombinedCheckpointBytes &&
      prior::peakRssBytes() <= kMaximumRssBytes;
  const double elapsed_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  const std::uint64_t classifier_fingerprint =
      classifierFingerprint(restored.first, restored.second);
  writeArtifact(options, audit, fitting_gate, heldout_gate,
                classifier_bytes, head_bytes, classifier_fingerprint,
                head_fingerprint, throughput, elapsed_seconds,
                resources_passed);
  report << std::fixed << std::setprecision(6)
         << "D4_LONG_VETO_CLASSIFIER {\"status\":\"complete\","
            "\"cvSwitches\":"
         << audit.fitting_cv.switches << ",\"cvPrecision\":"
         << precision(audit.fitting_cv) << ",\"cvCoverage\":"
         << coverage(audit.fitting_cv) << ",\"cvMeanGain\":"
         << meanReturnGain(audit.fitting_cv) << ",\"cvQ10\":"
         << scenarioQ10(audit.fitting_cv) << ",\"heldoutSwitches\":"
         << audit.heldout.switches << ",\"heldoutPrecision\":"
         << precision(audit.heldout) << ",\"heldoutCoverage\":"
         << coverage(audit.heldout) << ",\"fittingGatePassed\":"
         << (fitting_gate.passed ? "true" : "false")
         << ",\"heldoutGatePassed\":"
         << (heldout_gate.passed ? "true" : "false")
         << ",\"newCorpusCollected\":false,\"artifact\":\""
         << options.output << "\"}\n";
  return 0;
}

bool selfTest(const Options& options, std::ostream& output) {
  const bool inherited = base::fair::selfTest(output);

  const State fixture = base::fair::frozen::fixtureState(
      base::fair::frozen::kTypeScriptFixtures[1]);
  data::AuditRoot root;
  root.stored.label = prior::rootLabel(fixture);
  root.stored.d4 = root.stored.label;
  root.stored.split = "fitting";
  int fallback = -1;
  int alternative = -1;
  for (const int action : base::kActionOrder) {
    if (!root.stored.label.legal[action]) continue;
    if (fallback < 0) fallback = action;
    else if (alternative < 0) alternative = action;
  }
  if (fallback < 0 || alternative < 0) {
    throw std::runtime_error("veto self-test fixture lacks legal siblings");
  }
  root.stored.label.labeled_action = fallback;
  root.stored.d4.labeled_action = fallback;
  for (int action = 0; action < kBoardSize; ++action) {
    if (!root.stored.label.legal[action]) continue;
    root.stored.label.q[action] = 0.0;
    root.stored.d4.q[action] = action == fallback ? 10'000.0 : 5'000.0;
    for (int scenario = 0; scenario < prior::kScenarios; ++scenario) {
      root.stored.returns[action][scenario] =
          action == alternative ? 20'000.0
          : action == fallback   ? 0.0
                                 : -20'000.0;
      root.actions[action].scenario_survived[scenario] = true;
      root.actions[action].scenario_clears[scenario] =
          action == alternative ? 2 : 1;
    }
    root.actions[action].survival = 1.0;
    root.actions[action].raw_mean_clears =
        action == alternative ? 2.0 : 1.0;
    root.actions[action].mean_clears =
        root.actions[action].raw_mean_clears / 16.0;
  }
  root.prepared = base::prepare(root.stored.label);

  RootHeads predictions{};
  predictions[fallback][data::kSurvival] = 0.5;
  predictions[fallback][data::kNumberedClears] = 0.5;
  predictions[fallback][data::kVariance] = 0.5;
  predictions[alternative][data::kMeanReturnResidual] = 20'000.0;
  predictions[alternative][data::kSurvival] = 0.8;
  predictions[alternative][data::kNumberedClears] = 0.8;
  predictions[alternative][data::kDownside] = 0.5;
  predictions[alternative][data::kVariance] = 0.1;
  for (int action = 0; action < kBoardSize; ++action) {
    if (action == fallback || action == alternative) continue;
    predictions[action][data::kSurvival] = 0.0;
    predictions[action][data::kNumberedClears] = 0.0;
  }
  Classifier permissive;
  permissive.scale.fill(1.0);
  permissive.bias = std::log(0.95 / 0.05);
  const ExactAlternative exact = exactAlternative(root, alternative);
  const Decision decision = choose(root, predictions, permissive);
  const bool target_and_veto =
      exact.eligible &&
      std::abs(exact.mean_return_gain - 20'000.0) <= 1.0e-9 &&
      std::abs(exact.paired_lower95 - 20'000.0) <= 1.0e-9 &&
      decision.switched &&
      decision.fallback == fallback && decision.selected == alternative &&
      decision.probability >= kSwitchProbability;

  data::AuditRoot changed_outcomes = root;
  for (int action = 0; action < kBoardSize; ++action) {
    for (int scenario = 0; scenario < prior::kScenarios; ++scenario) {
      changed_outcomes.stored.returns[action][scenario] +=
          static_cast<double>((action + 1) * (scenario + 3) * 123'456);
      changed_outcomes.actions[action].scenario_survived[scenario] = false;
      changed_outcomes.actions[action].scenario_clears[scenario] = -99;
    }
    changed_outcomes.actions[action].raw_mean_clears = -999.0;
  }
  const Decision changed_outcome_decision =
      choose(changed_outcomes, predictions, permissive);
  const bool outcome_label_blind =
      changed_outcome_decision.fallback == decision.fallback &&
      changed_outcome_decision.selected == decision.selected &&
      changed_outcome_decision.probability == decision.probability &&
      rawFeatures(changed_outcomes, predictions, alternative) ==
          rawFeatures(root, predictions, alternative);

  data::AuditRoot changed_metadata = root;
  changed_metadata.stored.label.game = 999;
  changed_metadata.stored.label.move_in_game = -777;
  changed_metadata.stored.d4.game = -123;
  changed_metadata.stored.d4.move_in_game = 456;
  const bool metadata_blind =
      rawFeatures(root, predictions, alternative) ==
      rawFeatures(changed_metadata, predictions, alternative);

  std::vector<TrainingRow> rows;
  for (int index = 0; index < 24; ++index) {
    TrainingRow row;
    row.positive = index % 3 == 0;
    for (int feature = 0; feature < kFeatures; ++feature) {
      row.feature[feature] =
          static_cast<double>((index + 1) * (feature + 2)) / 17.0 +
          (row.positive ? 0.75 : -0.25);
    }
    rows.push_back(row);
  }
  const Classifier first = trainClassifier(rows);
  const Classifier repeat = trainClassifier(rows);
  constexpr std::uint64_t kSyntheticHeadFingerprint =
      0x1234'5678'9abc'def0ull;
  const bool deterministic =
      classifierFingerprint(first, kSyntheticHeadFingerprint) ==
      classifierFingerprint(repeat, kSyntheticHeadFingerprint);
  writeCheckpoint(options.checkpoint, first, kSyntheticHeadFingerprint);
  const auto restored = readCheckpoint(options.checkpoint);
  const bool checkpoint =
      restored.second == kSyntheticHeadFingerprint &&
      classifierFingerprint(restored.first, restored.second) ==
          classifierFingerprint(first, kSyntheticHeadFingerprint) &&
      fileBytes(options.checkpoint) <= kMaximumCombinedCheckpointBytes;

  VetoMetrics passing;
  passing.roots = 120;
  passing.positive_roots = 40;
  passing.switches = 12;
  passing.true_switches = 12;
  passing.survival_nonloss = 12;
  passing.clear_nonloss = 12;
  passing.mean_return_gain_sum = 240'000.0;
  passing.mean_d4_q_loss_sum = 60'000.0;
  passing.scenario_differences.assign(12 * prior::kScenarios, 20'000.0);
  std::array<VetoMetrics, kFolds> passing_folds{};
  for (int fold = 0; fold < kFolds; ++fold) {
    VetoMetrics& value = passing_folds[fold];
    value.roots = 20;
    value.positive_roots = fold < 4 ? 7 : 6;
    if (fold >= 4) continue;
    value.switches = 3;
    value.true_switches = 3;
    value.survival_nonloss = 3;
    value.clear_nonloss = 3;
    value.mean_return_gain_sum = 60'000.0;
    value.mean_d4_q_loss_sum = 15'000.0;
    value.scenario_differences.assign(3 * prior::kScenarios, 20'000.0);
  }
  const Gate pass_gate = gate(passing, passing_folds, nullptr, 12);
  VetoMetrics zero_switch;
  zero_switch.roots = 120;
  zero_switch.positive_roots = 12;
  const std::array<VetoMetrics, kFolds> empty_folds{};
  const Gate zero_gate = gate(zero_switch, empty_folds, nullptr, 12);
  const bool frozen_gate = pass_gate.passed && !zero_gate.passed &&
                           pass_gate.active_folds == 4 &&
                           pass_gate.stable_folds == 4;

  const bool protocol =
      kFolds == 6 && kFeatures == 9 && kHeadEpochs == 40 &&
      kClassifierEpochs == 500 && kSwitchProbability == 0.90 &&
      kMaximumD4QLoss == 7'000.0 && kMinimumMaterialMeanGain == 10'000.0 &&
      kMinimumPrecision == 0.80 && kMinimumCoverage == 0.20 &&
      prior::kTrainingRoots == 288 && prior::kHeldoutRoots == 144;
  const bool passed = inherited && target_and_veto && outcome_label_blind &&
                      metadata_blind && deterministic && checkpoint &&
                      frozen_gate && protocol;
  output << std::setprecision(12)
         << "D4_LONG_VETO_CLASSIFIER_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"inheritedD4\":" << (inherited ? "true" : "false")
         << ",\"targetAndVeto\":"
         << (target_and_veto ? "true" : "false")
         << ",\"syntheticEligible\":"
         << (exact.eligible ? "true" : "false")
         << ",\"syntheticMeanGain\":" << exact.mean_return_gain
         << ",\"syntheticLower95\":" << exact.paired_lower95
         << ",\"syntheticSelected\":" << decision.selected
         << ",\"syntheticAlternative\":" << alternative
         << ",\"syntheticProbability\":" << decision.probability
         << ",\"outcomeLabelBlind\":"
         << (outcome_label_blind ? "true" : "false")
         << ",\"metadataBlind\":"
         << (metadata_blind ? "true" : "false")
         << ",\"deterministicTraining\":"
         << (deterministic ? "true" : "false")
         << ",\"checkpoint\":" << (checkpoint ? "true" : "false")
         << ",\"frozenGate\":" << (frozen_gate ? "true" : "false")
         << ",\"zeroSwitchRejected\":"
         << (!zero_gate.passed ? "true" : "false")
         << ",\"protocol\":" << (protocol ? "true" : "false")
         << "}\n";
  return passed;
}

}  // namespace drop7::d4_long_outcome_veto_classifier

#ifndef DROP7_D4_LONG_OUTCOME_VETO_CLASSIFIER_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      const auto options =
          drop7::d4_long_outcome_veto_classifier::parseOptions(argc, argv, 2);
      return drop7::d4_long_outcome_veto_classifier::selfTest(
                 options, std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::d4_long_outcome_veto_classifier::parseOptions(argc, argv, 2);
      return drop7::d4_long_outcome_veto_classifier::run(options, std::cout);
    }
    std::cerr
        << "usage: drop7_d4_long_outcome_veto_classifier "
           "--self-test|--run [--labels PATH] [--d4-source PATH] "
           "[--derived PATH] [--head-checkpoint PATH] [--output PATH] "
           "[--checkpoint PATH]\n";
    return EXIT_FAILURE;
  } catch (const std::exception& error) {
    std::cerr << "drop7_d4_long_outcome_veto_classifier: " << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
}
#endif
