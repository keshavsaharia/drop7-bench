#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#include "../../fair-expectimax/reference/fair-only-depth4.cpp"

#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <future>
#include <numeric>
#include <mutex>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <vector>

// Performs scaled, leakage-controlled compression of the reference public
// fair-D4 root-Q policy.  A separate D4 label artifact is available only to
// the explicit architecture-design command.  The main run uses disjoint
// whole-game fitting and heldout families and never reads gameplay
// screen, validation, or final seeds.
namespace drop7::scaled_d4_distill {

namespace fair = drop7::fair_only_depth4;

constexpr double kTieTolerance = 1.0e-9;
constexpr std::array<int, kBoardSize> kActionOrder{{3, 2, 4, 1, 5, 0, 6}};

struct RootLabel {
  Board board{};
  std::uint8_t next_disc = 1;
  int moves_remaining = kMovesPerLevel;
  int labeled_action = -1;
  std::array<double, kBoardSize> q{};
  std::array<bool, kBoardSize> legal{};
  int game = -1;
  int move_in_game = -1;
};

int integerAfter(std::string_view line, std::string_view marker) {
  const std::size_t found = line.find(marker);
  if (found == std::string_view::npos) {
    throw std::runtime_error("missing label integer field");
  }
  const char* begin = line.data() + found + marker.size();
  char* end = nullptr;
  const long parsed = std::strtol(begin, &end, 10);
  if (end == begin || parsed < std::numeric_limits<int>::min() ||
      parsed > std::numeric_limits<int>::max()) {
    throw std::runtime_error("invalid label integer field");
  }
  return static_cast<int>(parsed);
}

RootLabel parseLabel(std::string_view line, std::string_view split) {
  const std::string split_marker = "\"split\":\"" + std::string(split) + "\"";
  if (line.find(split_marker) == std::string_view::npos) {
    throw std::runtime_error("label split mismatch");
  }
  constexpr std::string_view board_marker = "\"board\":\"";
  const std::size_t board_at = line.find(board_marker);
  if (board_at == std::string_view::npos ||
      board_at + board_marker.size() + kCellCount > line.size()) {
    throw std::runtime_error("invalid label board");
  }
  RootLabel result;
  for (int cell = 0; cell < kCellCount; ++cell) {
    const char encoded = line[board_at + board_marker.size() + cell];
    if (encoded < '0' || encoded > '9') {
      throw std::runtime_error("invalid label cell");
    }
    result.board[cell] = static_cast<std::uint8_t>(encoded - '0');
  }
  result.next_disc =
      static_cast<std::uint8_t>(integerAfter(line, "\"nextDisc\":"));
  result.moves_remaining = integerAfter(line, "\"movesRemaining\":");
  result.labeled_action = integerAfter(line, "\"action\":");
  if (result.next_disc < 1 || result.next_disc > kBoardSize ||
      result.moves_remaining < 1 || result.moves_remaining > kMovesPerLevel ||
      result.labeled_action < 0 || result.labeled_action >= kBoardSize) {
    throw std::runtime_error("invalid label public fields");
  }
  constexpr std::string_view q_marker = "\"rootQ\":[";
  std::size_t cursor = line.find(q_marker);
  if (cursor == std::string_view::npos) {
    throw std::runtime_error("missing root-Q vector");
  }
  cursor += q_marker.size();
  const std::string owned(line);
  result.q.fill(-std::numeric_limits<double>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    while (cursor < line.size() &&
           (line[cursor] == ' ' || line[cursor] == ',')) {
      ++cursor;
    }
    if (line.substr(cursor, 4) == "null") {
      cursor += 4;
      result.legal[action] = false;
      continue;
    }
    char* end = nullptr;
    const char* begin = owned.c_str() + cursor;
    result.q[action] = std::strtod(begin, &end);
    if (end == begin || !std::isfinite(result.q[action])) {
      throw std::runtime_error("invalid root-Q value");
    }
    cursor = static_cast<std::size_t>(end - owned.c_str());
    result.legal[action] = true;
  }
  for (int action = 0; action < kBoardSize; ++action) {
    if (result.legal[action] != isLegal(result.board, action)) {
      throw std::runtime_error("root-Q legal mask mismatch");
    }
  }
  return result;
}

std::vector<RootLabel> loadSplit(const std::string& path,
                                 std::string_view split) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("could not open root-label file");
  std::string line;
  if (!std::getline(input, line) ||
      (line.find("drop7-public-d4-root-labels-v1") == std::string::npos &&
       line.find("drop7-public-d4-root-labels-v2") == std::string::npos)) {
    throw std::runtime_error("invalid root-label header");
  }
  std::vector<RootLabel> result;
  int game = -1;
  int move = -1;
  while (std::getline(input, line)) {
    if (line.find("\"split\":\"" + std::string(split) + "\"") ==
        std::string::npos) {
      continue;
    }
    RootLabel label = parseLabel(line, split);
    if (label.board == initialBoard()) {
      ++game;
      move = 0;
    } else if (game < 0) {
      throw std::runtime_error("label split does not start at game boundary");
    } else {
      ++move;
    }
    label.game = game;
    label.move_in_game = move;
    result.push_back(std::move(label));
  }
  if (result.empty()) throw std::runtime_error("empty root-label split");
  return result;
}

bool qTied(double first, double second) {
  return std::abs(first - second) <=
         kTieTolerance *
             (1.0 + std::max(std::abs(first), std::abs(second)));
}

struct Ranking {
  int roots = 0;
  int top1 = 0;
  int top2 = 0;
  std::uint64_t pairs = 0;
  double pairwise_credit = 0.0;
  double normalized_regret = 0.0;
};

State publicState(const RootLabel& label) {
  State state;
  state.board = label.board;
  state.next_disc = label.next_disc;
  state.moves_remaining = label.moves_remaining;
  state.score = 0;
  state.level = 1;
  state.moves_played = 0;
  state.game_over = false;
  return state;
}

struct ShallowRoot {
  std::array<double, kBoardSize> values{};
  std::array<double, kBoardSize> expected_scores{};
};

ShallowRoot shallowRoot(const RootLabel& label, int depth) {
  fair::SearchContext context;
  const fair::RootEvaluation root =
      fair::rootDecision(publicState(label), depth, context);
  return {root.values, root.expected_scores};
}

std::array<double, kBoardSize> normalized(
    const std::array<double, kBoardSize>& values,
    const std::array<bool, kBoardSize>& legal) {
  double minimum = std::numeric_limits<double>::infinity();
  double maximum = -std::numeric_limits<double>::infinity();
  for (int action = 0; action < kBoardSize; ++action) {
    if (!legal[action]) continue;
    minimum = std::min(minimum, values[action]);
    maximum = std::max(maximum, values[action]);
  }
  const double range = std::max(1.0e-9, maximum - minimum);
  std::array<double, kBoardSize> result{};
  for (int action = 0; action < kBoardSize; ++action) {
    if (legal[action]) result[action] = (values[action] - minimum) / range;
  }
  return result;
}

constexpr int kRelativeBoardFeatures = kBoardSize * 13 * 10;
constexpr int kRelativeHeightFeatures = 13 * 8;
constexpr int kActionFeatures = kBoardSize;
constexpr int kActionNextFeatures = kBoardSize * kBoardSize;
constexpr int kActionPhaseFeatures = kBoardSize * kMovesPerLevel;
constexpr int kNextOwnCellFeatures = kBoardSize * kBoardSize * 10;
constexpr int kPhaseHeightFeatures = kMovesPerLevel * 8;
constexpr int kDenseFeatures = 12;
constexpr int kFeatureCount =
    kRelativeBoardFeatures + kRelativeHeightFeatures + kActionFeatures +
    kActionNextFeatures + kActionPhaseFeatures + kNextOwnCellFeatures +
    kPhaseHeightFeatures + kDenseFeatures;
constexpr double kBaseLogitScale = 5.0;
constexpr double kPairwiseLossWeight = 0.25;
constexpr int kDesignEpochs = 100;
constexpr int kDesignBatchSize = 64;
constexpr double kDesignLearningRate = 0.01;

constexpr std::uint32_t kTrainingSeedStart = 0x3df2'0000u;
constexpr std::uint32_t kHeldoutSeedStart = 0x3df3'0000u;
constexpr int kTrainingGames = 24;
constexpr int kHeldoutGames = 12;
constexpr int kCollectionMaximumMoves = 250;
constexpr int kParallelism = 4;
constexpr double kMaximumProjectedWallSeconds = 45.0 * 60.0;
constexpr double kProjectionSafetyFactor = 1.25;
constexpr int kMinimumTrainingLabels = 1'200;
constexpr int kMinimumHeldoutLabels = 600;
constexpr double kMinimumHeldoutTop1 = 0.55;
constexpr double kMinimumHeldoutTop2 = 0.70;
constexpr double kMinimumHeldoutPairwise = 0.72;
constexpr double kMinimumHalfTop1 = 0.50;
constexpr double kMinimumHalfPairwise = 0.72;
constexpr double kMaximumHeldoutRegret = 0.18;
constexpr double kMinimumTop1ImprovementOverD2 = 0.01;
constexpr double kMinimumTop2ImprovementOverD2 = 0.005;
constexpr double kMinimumPairwiseImprovementOverD2 = 0.005;
constexpr double kMaximumRegretRatioToD2 = 0.95;
constexpr std::uint64_t kMaximumCheckpointBytes = 32'768;
constexpr double kMinimumInferenceRootsPerSecond = 250.0;
constexpr double kMinimumRolloutScoreRatio = 0.55;
constexpr double kMinimumRolloutMoveRatio = 0.55;
constexpr double kMinimumRolloutThroughputRatio = 0.85;

static_assert(kTrainingSeedStart + kTrainingGames < kHeldoutSeedStart);
static_assert(kHeldoutSeedStart + kHeldoutGames < 0x3e00'0000u);
static_assert((kTrainingSeedStart >> 24) != 0x7du &&
              (kTrainingSeedStart >> 24) != 0xd7u);
static_assert((kHeldoutSeedStart >> 24) != 0x7du &&
              (kHeldoutSeedStart >> 24) != 0xd7u);

static_assert(kFeatureCount == 1'647);

struct SparseFeature {
  std::uint16_t index = 0;
  double value = 0.0;
};

using FeatureVector = std::vector<SparseFeature>;

std::array<int, kBoardSize> columnHeights(const Board& board) {
  return cfpi::detail::columnHeights(board);
}

FeatureVector actionFeatures(
    const Board& board, std::uint8_t next_disc, int moves_remaining,
    int action, double d1, double immediate, double d2) {
  FeatureVector result;
  result.reserve(84);
  const auto heights = columnHeights(board);
  int offset = 0;
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      const int relative = column - action + 6;
      const int cell = row * kBoardSize + column;
      const int kind = static_cast<int>(board[cell]);
      result.push_back({static_cast<std::uint16_t>(
                            offset + (row * 13 + relative) * 10 + kind),
                        1.0});
    }
  }
  offset += kRelativeBoardFeatures;
  for (int column = 0; column < kBoardSize; ++column) {
    const int relative = column - action + 6;
    result.push_back({static_cast<std::uint16_t>(
                          offset + relative * 8 + heights[column]),
                      1.0});
  }
  offset += kRelativeHeightFeatures;
  result.push_back(
      {static_cast<std::uint16_t>(offset + action), 1.0});
  offset += kActionFeatures;
  result.push_back({static_cast<std::uint16_t>(
                        offset + action * kBoardSize + next_disc - 1),
                    1.0});
  offset += kActionNextFeatures;
  result.push_back({static_cast<std::uint16_t>(
                        offset + action * kMovesPerLevel + moves_remaining - 1),
                    1.0});
  offset += kActionPhaseFeatures;
  for (int row = 0; row < kBoardSize; ++row) {
    const int kind = static_cast<int>(board[row * kBoardSize + action]);
    result.push_back({static_cast<std::uint16_t>(
                          offset + ((next_disc - 1) * kBoardSize + row) * 10 +
                          kind),
                      1.0});
  }
  offset += kNextOwnCellFeatures;
  result.push_back({static_cast<std::uint16_t>(
                        offset + (moves_remaining - 1) * 8 + heights[action]),
                    1.0});
  offset += kPhaseHeightFeatures;
  const double height = static_cast<double>(heights[action]) / kBoardSize;
  const double left = action > 0
                          ? static_cast<double>(heights[action - 1]) / kBoardSize
                          : 1.0;
  const double right = action + 1 < kBoardSize
                           ? static_cast<double>(heights[action + 1]) /
                                 kBoardSize
                           : 1.0;
  const double center = 1.0 - std::abs(action - 3) / 3.0;
  const double phase = static_cast<double>(moves_remaining) / kMovesPerLevel;
  const std::array<double, kDenseFeatures> dense{{
      d1,
      immediate,
      height,
      0.5 * (left + right),
      std::abs(left - right),
      center,
      height * height,
      d1 * height,
      immediate * phase,
      heights[action] >= kBoardSize - 1 ? 1.0 : 0.0,
      d1 - d2,
      d2 * height,
  }};
  for (int index = 0; index < kDenseFeatures; ++index) {
    result.push_back({static_cast<std::uint16_t>(offset + index),
                      dense[index]});
  }
  if (offset + kDenseFeatures != kFeatureCount) {
    throw std::logic_error("scaled-distill feature layout mismatch");
  }
  return result;
}

struct PreparedRoot {
  RootLabel label{};
  std::array<double, kBoardSize> target{};
  std::array<double, kBoardSize> d1{};
  std::array<double, kBoardSize> d2{};
  std::array<double, kBoardSize> immediate{};
  std::array<FeatureVector, kBoardSize> direct{};
  std::array<FeatureVector, kBoardSize> reflected{};
};

PreparedRoot prepare(const RootLabel& label) {
  PreparedRoot result;
  result.label = label;
  const ShallowRoot d1 = shallowRoot(label, 1);
  const ShallowRoot d2 = shallowRoot(label, 2);
  result.target = normalized(label.q, label.legal);
  result.d1 = normalized(d1.values, label.legal);
  result.d2 = normalized(d2.values, label.legal);
  result.immediate = normalized(d1.expected_scores, label.legal);
  const Board reflected_board = cfpi::detail::mirrorBoard(label.board);
  for (int action = 0; action < kBoardSize; ++action) {
    if (!label.legal[action]) continue;
    result.direct[action] = actionFeatures(
        label.board, label.next_disc, label.moves_remaining, action,
        result.d1[action], result.immediate[action], result.d2[action]);
    result.reflected[action] = actionFeatures(
        reflected_board, label.next_disc, label.moves_remaining,
        kBoardSize - 1 - action, result.d1[action], result.immediate[action],
        result.d2[action]);
  }
  return result;
}

std::vector<PreparedRoot> prepareAll(const std::vector<RootLabel>& labels) {
  std::vector<PreparedRoot> result;
  result.reserve(labels.size());
  for (const RootLabel& label : labels) result.push_back(prepare(label));
  return result;
}

using LinearModel = std::array<double, kFeatureCount>;

double dot(const LinearModel& model, const FeatureVector& features) {
  double result = 0.0;
  for (const SparseFeature& feature : features) {
    result += model[feature.index] * feature.value;
  }
  return result;
}

std::array<double, kBoardSize> modelScores(const LinearModel& model,
                                           const PreparedRoot& root) {
  std::array<double, kBoardSize> result{};
  for (int action = 0; action < kBoardSize; ++action) {
    if (!root.label.legal[action]) {
      result[action] = -std::numeric_limits<double>::infinity();
      continue;
    }
    const double residual =
        0.5 * (dot(model, root.direct[action]) +
               dot(model, root.reflected[action]));
    result[action] = kBaseLogitScale * root.d2[action] + residual;
  }
  return result;
}

std::array<double, kBoardSize> softmax(
    const std::array<double, kBoardSize>& values,
    const std::array<bool, kBoardSize>& legal) {
  double maximum = -std::numeric_limits<double>::infinity();
  for (int action = 0; action < kBoardSize; ++action) {
    if (legal[action]) maximum = std::max(maximum, values[action]);
  }
  std::array<double, kBoardSize> result{};
  double total = 0.0;
  for (int action = 0; action < kBoardSize; ++action) {
    if (!legal[action]) continue;
    result[action] = std::exp(values[action] - maximum);
    total += result[action];
  }
  if (!(total > 0.0)) throw std::logic_error("scaled softmax failed");
  for (double& value : result) value /= total;
  return result;
}

void addFeatureGradient(LinearModel& gradient, const FeatureVector& features,
                        double scale) {
  for (const SparseFeature& feature : features) {
    gradient[feature.index] += scale * feature.value;
  }
}

void accumulateGradient(const LinearModel& model, const PreparedRoot& root,
                        LinearModel& gradient) {
  const auto score = modelScores(model, root);
  std::array<double, kBoardSize> target_logit{};
  for (int action = 0; action < kBoardSize; ++action) {
    if (root.label.legal[action]) {
      target_logit[action] = kBaseLogitScale * root.target[action];
    }
  }
  const auto predicted = softmax(score, root.label.legal);
  const auto target = softmax(target_logit, root.label.legal);
  std::array<double, kBoardSize> score_gradient{};
  for (int action = 0; action < kBoardSize; ++action) {
    if (root.label.legal[action]) {
      score_gradient[action] = predicted[action] - target[action];
    }
  }
  double pair_weight = 0.0;
  std::array<double, kBoardSize> pair_gradient{};
  for (int first = 0; first < kBoardSize; ++first) {
    if (!root.label.legal[first]) continue;
    for (int second = first + 1; second < kBoardSize; ++second) {
      if (!root.label.legal[second]) continue;
      const double difference = root.target[first] - root.target[second];
      if (std::abs(difference) <= kTieTolerance) continue;
      const int better = difference > 0.0 ? first : second;
      const int worse = difference > 0.0 ? second : first;
      const double weight = 0.25 + 0.75 * std::abs(difference);
      const double margin = score[better] - score[worse];
      const double derivative = -weight / (1.0 + std::exp(margin));
      pair_gradient[better] += derivative;
      pair_gradient[worse] -= derivative;
      pair_weight += weight;
    }
  }
  if (pair_weight > 0.0) {
    for (int action = 0; action < kBoardSize; ++action) {
      score_gradient[action] +=
          kPairwiseLossWeight * pair_gradient[action] / pair_weight;
    }
  }
  for (int action = 0; action < kBoardSize; ++action) {
    if (!root.label.legal[action]) continue;
    addFeatureGradient(gradient, root.direct[action],
                       0.5 * score_gradient[action]);
    addFeatureGradient(gradient, root.reflected[action],
                       0.5 * score_gradient[action]);
  }
}

void deterministicShuffle(std::vector<std::size_t>& values,
                          std::uint32_t seed) {
  for (std::size_t remaining = values.size(); remaining > 1; --remaining) {
    seed = mix32(seed + static_cast<std::uint32_t>(remaining));
    std::swap(values[remaining - 1], values[seed % remaining]);
  }
}

LinearModel trainLinear(const std::vector<PreparedRoot>& training, double l2) {
  LinearModel model{};
  LinearModel first_moment{};
  LinearModel second_moment{};
  std::vector<std::size_t> order(training.size());
  std::iota(order.begin(), order.end(), 0);
  std::uint64_t step = 0;
  for (int epoch = 0; epoch < kDesignEpochs; ++epoch) {
    deterministicShuffle(order,
                         0x5ca1'0000u + static_cast<std::uint32_t>(epoch));
    for (std::size_t begin = 0; begin < training.size();
         begin += kDesignBatchSize) {
      const std::size_t end =
          std::min(training.size(), begin + kDesignBatchSize);
      LinearModel gradient{};
      for (std::size_t index = begin; index < end; ++index) {
        accumulateGradient(model, training[order[index]], gradient);
      }
      ++step;
      const double inverse_batch = 1.0 / static_cast<double>(end - begin);
      const double first_correction = 1.0 - std::pow(0.9, step);
      const double second_correction = 1.0 - std::pow(0.999, step);
      for (int parameter = 0; parameter < kFeatureCount; ++parameter) {
        const double value =
            gradient[parameter] * inverse_batch + l2 * model[parameter];
        first_moment[parameter] =
            0.9 * first_moment[parameter] + 0.1 * value;
        second_moment[parameter] =
            0.999 * second_moment[parameter] + 0.001 * value * value;
        model[parameter] -=
            kDesignLearningRate * (first_moment[parameter] / first_correction) /
            (std::sqrt(second_moment[parameter] / second_correction) + 1.0e-8);
      }
    }
  }
  return model;
}

void observe(const RootLabel& label,
             const std::array<double, kBoardSize>& prediction,
             Ranking& result);

Ranking evaluateModel(const LinearModel& model,
                      const std::vector<PreparedRoot>& roots) {
  Ranking result;
  for (const PreparedRoot& root : roots) {
    observe(root.label, modelScores(model, root), result);
  }
  return result;
}

void observe(const RootLabel& label,
             const std::array<double, kBoardSize>& prediction,
             Ranking& result) {
  std::vector<int> ranked;
  for (const int action : kActionOrder) {
    if (label.legal[action]) ranked.push_back(action);
  }
  std::stable_sort(ranked.begin(), ranked.end(), [&](int left, int right) {
    return prediction[left] > prediction[right];
  });
  if (ranked.empty()) throw std::logic_error("empty ranking root");
  double maximum = -std::numeric_limits<double>::infinity();
  double minimum = std::numeric_limits<double>::infinity();
  for (int action = 0; action < kBoardSize; ++action) {
    if (!label.legal[action]) continue;
    maximum = std::max(maximum, label.q[action]);
    minimum = std::min(minimum, label.q[action]);
  }
  ++result.roots;
  result.top1 += qTied(label.q[ranked.front()], maximum);
  for (std::size_t index = 0; index < std::min<std::size_t>(2, ranked.size());
       ++index) {
    if (qTied(label.q[ranked[index]], maximum)) {
      ++result.top2;
      break;
    }
  }
  const double range = std::max(1.0e-9, maximum - minimum);
  result.normalized_regret +=
      (maximum - label.q[ranked.front()]) / range;
  for (int first = 0; first < kBoardSize; ++first) {
    if (!label.legal[first]) continue;
    for (int second = first + 1; second < kBoardSize; ++second) {
      if (!label.legal[second] ||
          qTied(label.q[first], label.q[second])) {
        continue;
      }
      const double predicted = prediction[first] - prediction[second];
      if (std::abs(predicted) <= kTieTolerance) {
        result.pairwise_credit += 0.5;
      } else {
        result.pairwise_credit +=
            ((predicted > 0.0) == (label.q[first] > label.q[second])) ? 1.0
                                                                     : 0.0;
      }
      ++result.pairs;
    }
  }
}

double top1Rate(const Ranking& value) {
  return static_cast<double>(value.top1) / value.roots;
}

double top2Rate(const Ranking& value) {
  return static_cast<double>(value.top2) / value.roots;
}

double pairwiseRate(const Ranking& value) {
  return value.pairwise_credit / value.pairs;
}

double regret(const Ranking& value) {
  return value.normalized_regret / value.roots;
}

Ranking evaluateRange(const LinearModel* model,
                      const std::vector<PreparedRoot>& roots, int game_begin,
                      int game_end) {
  Ranking result;
  for (const PreparedRoot& root : roots) {
    if (root.label.game < game_begin || root.label.game >= game_end) continue;
    observe(root.label, model == nullptr ? root.d2 : modelScores(*model, root),
            result);
  }
  if (result.roots <= 0 || result.pairs == 0) {
    throw std::logic_error("empty scaled-distill game range");
  }
  return result;
}

std::uint64_t fingerprint(const LinearModel& model) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const double value : model) {
    std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    for (int byte = 0; byte < 8; ++byte) {
      hash ^= bits & 0xffu;
      hash *= 0x0000'0100'0000'01b3ull;
      bits >>= 8;
    }
  }
  return hash;
}

constexpr std::array<char, 8> kCheckpointMagic{{
    'D', '7', 'S', 'D', '4', 'R', '1', '\0',
}};

struct CheckpointHeader {
  std::array<char, 8> magic{};
  std::uint32_t feature_count = 0;
  std::uint32_t epochs = 0;
  double l2 = 0.0;
  double base_scale = 0.0;
  std::uint64_t fingerprint = 0;
};

void writeCheckpoint(const std::string& path, const LinearModel& model) {
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("could not write distill checkpoint");
  const CheckpointHeader header{kCheckpointMagic, kFeatureCount, kDesignEpochs,
                                0.03, kBaseLogitScale, fingerprint(model)};
  output.write(reinterpret_cast<const char*>(&header), sizeof(header));
  output.write(reinterpret_cast<const char*>(model.data()),
               static_cast<std::streamsize>(sizeof(model)));
  if (!output) throw std::runtime_error("distill checkpoint write failed");
}

LinearModel readCheckpoint(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not read distill checkpoint");
  CheckpointHeader header;
  LinearModel model{};
  input.read(reinterpret_cast<char*>(&header), sizeof(header));
  input.read(reinterpret_cast<char*>(model.data()),
             static_cast<std::streamsize>(sizeof(model)));
  const bool payload_ok = static_cast<bool>(input);
  char trailing = 0;
  const bool has_trailing = static_cast<bool>(input.read(&trailing, 1));
  if (!payload_ok || !input.eof() || has_trailing ||
      header.magic != kCheckpointMagic ||
      header.feature_count != kFeatureCount ||
      header.epochs != kDesignEpochs || header.l2 != 0.03 ||
      header.base_scale != kBaseLogitScale ||
      header.fingerprint != fingerprint(model)) {
    throw std::runtime_error("invalid distill checkpoint");
  }
  return model;
}

std::uint64_t fileBytes(const std::string& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) throw std::runtime_error("could not size distill checkpoint");
  const std::streampos end = input.tellg();
  if (end < 0) throw std::runtime_error("invalid distill checkpoint size");
  return static_cast<std::uint64_t>(end);
}

PreparedRoot prepareInference(const State& canonical) {
  PreparedRoot result;
  RootLabel& label = result.label;
  label.board = canonical.board;
  label.next_disc = canonical.next_disc;
  label.moves_remaining = canonical.moves_remaining;
  for (int action = 0; action < kBoardSize; ++action) {
    label.legal[action] = isLegal(label.board, action);
  }
  const ShallowRoot d1 = shallowRoot(label, 1);
  const ShallowRoot d2 = shallowRoot(label, 2);
  result.target = normalized(d2.values, label.legal);
  result.d1 = normalized(d1.values, label.legal);
  result.d2 = result.target;
  result.immediate = normalized(d1.expected_scores, label.legal);
  const Board reflected_board = cfpi::detail::mirrorBoard(label.board);
  for (int action = 0; action < kBoardSize; ++action) {
    if (!label.legal[action]) continue;
    result.direct[action] = actionFeatures(
        label.board, label.next_disc, label.moves_remaining, action,
        result.d1[action], result.immediate[action], result.d2[action]);
    result.reflected[action] = actionFeatures(
        reflected_board, label.next_disc, label.moves_remaining,
        kBoardSize - 1 - action, result.d1[action], result.immediate[action],
        result.d2[action]);
  }
  return result;
}

int studentAction(const LinearModel& model, const State& source) {
  if (source.game_over) return -1;
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  const PreparedRoot root = prepareInference(canonical);
  const auto score = modelScores(model, root);
  int selected = -1;
  double best = -std::numeric_limits<double>::infinity();
  for (const int action : kActionOrder) {
    if (!root.label.legal[action]) continue;
    if (selected < 0 || score[action] > best) {
      selected = action;
      best = score[action];
    }
  }
  if (selected < 0) return -1;
  return mirrored ? kBoardSize - 1 - selected : selected;
}

int d2Action(const State& source) {
  if (source.game_over) return -1;
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  fair::SearchContext context;
  const fair::RootEvaluation root = fair::rootDecision(canonical, 2, context);
  int selected = -1;
  double best = -std::numeric_limits<double>::infinity();
  for (const int action : kActionOrder) {
    if (!isLegal(canonical.board, action)) continue;
    if (selected < 0 || root.values[action] > best) {
      selected = action;
      best = root.values[action];
    }
  }
  if (selected < 0) return -1;
  return mirrored ? kBoardSize - 1 - selected : selected;
}

struct Throughput {
  int roots = 0;
  double seconds = 0.0;
  double roots_per_second = 0.0;
  std::uint64_t checksum = 0;
};

Throughput benchmarkInference(const LinearModel& model,
                              const std::vector<RootLabel>& labels) {
  constexpr int evaluations = 2'000;
  const auto started = std::chrono::steady_clock::now();
  std::uint64_t checksum = 0;
  for (int index = 0; index < evaluations; ++index) {
    const RootLabel& label = labels[static_cast<std::size_t>(index) %
                                    labels.size()];
    checksum = checksum * 11u +
               static_cast<std::uint64_t>(studentAction(model,
                                                        publicState(label)) +
                                          1);
  }
  const double seconds = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - started)
                             .count();
  return {evaluations, seconds, evaluations / seconds, checksum};
}

Throughput benchmarkD2Inference(const std::vector<RootLabel>& labels) {
  constexpr int evaluations = 2'000;
  const auto started = std::chrono::steady_clock::now();
  std::uint64_t checksum = 0;
  for (int index = 0; index < evaluations; ++index) {
    const RootLabel& label = labels[static_cast<std::size_t>(index) %
                                    labels.size()];
    checksum = checksum * 11u +
               static_cast<std::uint64_t>(d2Action(publicState(label)) + 1);
  }
  const double seconds = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - started)
                             .count();
  return {evaluations, seconds, evaluations / seconds, checksum};
}

struct SearchCost {
  int roots = 0;
  double d2_work_per_root = 0.0;
  double d2_nodes_per_root = 0.0;
  double d2_cache_hits_per_root = 0.0;
  double d1_plus_d2_work_per_root = 0.0;
  std::uint64_t maximum_d2_work = 0;
  std::size_t maximum_d2_cache_entries = 0;
};

SearchCost measureSearchCost(const std::vector<RootLabel>& labels) {
  SearchCost result;
  result.roots = static_cast<int>(labels.size());
  std::uint64_t d2_work = 0;
  std::uint64_t d2_nodes = 0;
  std::uint64_t d2_cache_hits = 0;
  std::uint64_t combined_work = 0;
  for (const RootLabel& label : labels) {
    const State state = publicState(label);
    fair::SearchContext d1_context;
    fair::SearchContext d2_context;
    (void)fair::rootDecision(state, 1, d1_context);
    (void)fair::rootDecision(state, 2, d2_context);
    d2_work += d2_context.work;
    d2_nodes += d2_context.nodes;
    d2_cache_hits += d2_context.cache_hits;
    combined_work += d1_context.work + d2_context.work;
    result.maximum_d2_work =
        std::max(result.maximum_d2_work, d2_context.work);
    result.maximum_d2_cache_entries =
        std::max(result.maximum_d2_cache_entries, d2_context.cache.size());
  }
  result.d2_work_per_root = static_cast<double>(d2_work) / result.roots;
  result.d2_nodes_per_root = static_cast<double>(d2_nodes) / result.roots;
  result.d2_cache_hits_per_root =
      static_cast<double>(d2_cache_hits) / result.roots;
  result.d1_plus_d2_work_per_root =
      static_cast<double>(combined_work) / result.roots;
  return result;
}

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

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t cleared = 0;
  std::uint64_t revealed = 0;
  int maximum_chain = 0;
  std::uint64_t teacher_work = 0;
  double elapsed_seconds = 0.0;
};

struct CollectedGame {
  GameResult game{};
  std::vector<RootLabel> labels;
};

void observeMove(const MoveResult& move, GameResult& result) {
  result.maximum_chain =
      std::max(result.maximum_chain, static_cast<int>(move.waves.size()));
  for (const Wave& wave : move.waves) {
    result.cleared += static_cast<std::uint64_t>(wave.cleared);
    result.revealed += static_cast<std::uint64_t>(wave.revealed);
  }
}

RootLabel behaviorLabel(const State& source,
                        const fair::SearchDecision& decision) {
  const bool mirrored =
      cfpi::detail::mirroredRepresentationIsSmaller(source.board);
  RootLabel result;
  result.board = mirrored ? cfpi::detail::mirrorBoard(source.board)
                          : source.board;
  result.next_disc = source.next_disc;
  result.moves_remaining = source.moves_remaining;
  result.labeled_action =
      mirrored ? kBoardSize - 1 - decision.action : decision.action;
  for (int canonical_action = 0; canonical_action < kBoardSize;
       ++canonical_action) {
    const int source_action = mirrored ? kBoardSize - 1 - canonical_action
                                       : canonical_action;
    result.legal[canonical_action] = isLegal(result.board, canonical_action);
    result.q[canonical_action] = decision.root_values[source_action];
    if (result.legal[canonical_action] !=
        std::isfinite(result.q[canonical_action])) {
      throw std::runtime_error("teacher label legal/Q mismatch");
    }
  }
  double maximum = -std::numeric_limits<double>::infinity();
  for (int action = 0; action < kBoardSize; ++action) {
    if (result.legal[action]) maximum = std::max(maximum, result.q[action]);
  }
  if (!result.legal[result.labeled_action] ||
      !qTied(result.q[result.labeled_action], maximum)) {
    throw std::runtime_error("teacher label action is not Q-optimal");
  }
  return result;
}

void reportGame(std::string_view label, const GameResult& game) {
  const std::lock_guard<std::mutex> lock(progress_mutex);
  std::cerr << "scaled-d4 " << label << " seed 0x" << std::hex << game.seed
            << std::dec << ' ' << game.score << " (" << game.moves
            << " moves" << (game.censored ? ", capped" : "")
            << ", clears " << game.cleared << ", reveals " << game.revealed
            << ", " << std::fixed << std::setprecision(3)
            << game.elapsed_seconds << "s)\n";
}

CollectedGame collectTeacherGame(std::uint32_t seed,
                                 std::string_view split) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  CollectedGame result;
  result.game.seed = seed;
  result.labels.reserve(kCollectionMaximumMoves);
  while (!state.game_over && state.moves_played < kCollectionMaximumMoves) {
    const fair::SearchDecision decision = fair::chooseDepth4Action(state);
    if (!decision.complete || decision.completed_depth != fair::kCandidateDepth ||
        !isLegal(state.board, decision.action)) {
      throw std::runtime_error("scaled-distill teacher decision failed");
    }
    RootLabel label = behaviorLabel(state, decision);
    label.move_in_game = state.moves_played;
    result.labels.push_back(std::move(label));
    result.game.teacher_work += decision.work;
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("scaled-distill teacher transition failed");
    }
    observeMove(move, result.game);
  }
  result.game.score = state.score;
  result.game.moves = state.moves_played;
  result.game.censored = !state.game_over;
  result.game.elapsed_seconds = std::chrono::duration<double>(
                                    std::chrono::steady_clock::now() - started)
                                    .count();
  reportGame(split, result.game);
  return result;
}

std::vector<CollectedGame> collectTeacherRange(std::uint32_t seed_start,
                                               int games,
                                               std::string_view split) {
  std::vector<CollectedGame> result(games);
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= games) return;
        result[game] = collectTeacherGame(
            seed_start + static_cast<std::uint32_t>(game), split);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

std::vector<RootLabel> flattenLabels(
    const std::vector<CollectedGame>& games) {
  std::vector<RootLabel> result;
  for (std::size_t game = 0; game < games.size(); ++game) {
    for (RootLabel label : games[game].labels) {
      label.game = static_cast<int>(game);
      result.push_back(std::move(label));
    }
  }
  return result;
}

GameResult runStudentGame(std::uint32_t seed, const LinearModel& model) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kCollectionMaximumMoves) {
    const int action = studentAction(model, state);
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("scaled-distill student chose illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("scaled-distill student transition failed");
    }
    observeMove(move, result);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.elapsed_seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
  reportGame("student-sanity", result);
  return result;
}

std::vector<GameResult> runStudentRange(std::uint32_t seed_start, int games,
                                        const LinearModel& model) {
  std::vector<GameResult> result(games);
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= games) return;
        result[game] = runStudentGame(
            seed_start + static_cast<std::uint32_t>(game), model);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

struct GameSummary {
  int games = 0;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  int censored = 0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double moves_per_second = 0.0;
};

GameSummary summarize(const std::vector<GameResult>& games) {
  if (games.empty()) throw std::logic_error("empty game summary");
  GameSummary result;
  result.games = static_cast<int>(games.size());
  double total_seconds = 0.0;
  std::uint64_t total_moves = 0;
  std::uint64_t total_cleared = 0;
  std::uint64_t total_revealed = 0;
  for (const GameResult& game : games) {
    result.mean_score +=
        static_cast<double>(game.score) / static_cast<double>(games.size());
    result.mean_moves +=
        static_cast<double>(game.moves) / static_cast<double>(games.size());
    result.censored += game.censored;
    total_moves += static_cast<std::uint64_t>(game.moves);
    total_cleared += game.cleared;
    total_revealed += game.revealed;
    total_seconds += game.elapsed_seconds;
  }
  if (total_moves > 0) {
    result.clears_per_move =
        static_cast<double>(total_cleared) / total_moves;
    result.reveals_per_move =
        static_cast<double>(total_revealed) / total_moves;
  }
  if (total_seconds > 0.0) result.moves_per_second = total_moves / total_seconds;
  return result;
}

GameSummary summarizeTeacher(const std::vector<CollectedGame>& games) {
  std::vector<GameResult> values;
  values.reserve(games.size());
  for (const CollectedGame& game : games) values.push_back(game.game);
  return summarize(values);
}

void writeRanking(std::ostream& output, std::string_view label,
                  const Ranking& value) {
  if (value.roots <= 0 || value.pairs == 0) {
    throw std::logic_error("empty ranking summary");
  }
  output << std::setprecision(10) << '"' << label << "\":{\"roots\":"
         << value.roots << ",\"top1WithTies\":" << top1Rate(value)
         << ",\"top2ContainsOptimal\":" << top2Rate(value)
         << ",\"pairwisePairs\":" << value.pairs
         << ",\"pairwiseAccuracy\":" << pairwiseRate(value)
         << ",\"normalizedRegret\":" << regret(value) << '}';
}

void printRanking(std::string_view label, const Ranking& value) {
  writeRanking(std::cout, label, value);
}

int designOld(const std::string& labels) {
  const std::vector<RootLabel> training = loadSplit(labels, "training");
  const std::vector<RootLabel> heldout = loadSplit(labels, "heldout");
  const std::vector<PreparedRoot> prepared_training = prepareAll(training);
  const std::vector<PreparedRoot> prepared_heldout = prepareAll(heldout);
  Ranking train_d1;
  Ranking train_d2;
  Ranking heldout_d1;
  Ranking heldout_d2;
  for (const PreparedRoot& root : prepared_training) {
    observe(root.label, root.d1, train_d1);
    observe(root.label, root.d2, train_d2);
  }
  for (const PreparedRoot& root : prepared_heldout) {
    observe(root.label, root.d1, heldout_d1);
    observe(root.label, root.d2, heldout_d2);
  }
  std::cout << "SCALED_D4_DESIGN {";
  printRanking("trainingD1", train_d1);
  std::cout << ',';
  printRanking("trainingD2", train_d2);
  std::cout << ',';
  printRanking("heldoutD1", heldout_d1);
  std::cout << ',';
  printRanking("heldoutD2", heldout_d2);
  constexpr std::array<double, 6> l2_grid{{
      0.001, 0.003, 0.01, 0.03, 0.1, 0.3,
  }};
  std::cout << ",\"linearResidualGrid\":[";
  for (std::size_t index = 0; index < l2_grid.size(); ++index) {
    if (index != 0) std::cout << ',';
    const LinearModel model = trainLinear(prepared_training, l2_grid[index]);
    const Ranking training_metrics =
        evaluateModel(model, prepared_training);
    const Ranking heldout_metrics = evaluateModel(model, prepared_heldout);
    std::cout << "{\"l2\":" << l2_grid[index] << ',';
    printRanking("training", training_metrics);
    std::cout << ',';
    printRanking("heldout", heldout_metrics);
    std::cout << '}';
  }
  std::cout << ']';
  std::cout << "}\n";
  return 0;
}

struct Options {
  std::string output = "/tmp/drop7-scaled-d4-distill.json";
  std::string checkpoint = "/tmp/drop7-scaled-d4-distill.bin";
  std::string labels = "/tmp/drop7-scaled-d4-distill-labels.jsonl";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing scaled-distill option value");
    }
    const std::string flag = argv[index];
    if (flag == "--output") {
      result.output = argv[index + 1];
    } else if (flag == "--checkpoint") {
      result.checkpoint = argv[index + 1];
    } else if (flag == "--labels") {
      result.labels = argv[index + 1];
    } else {
      throw std::invalid_argument("unknown scaled-distill option " + flag);
    }
  }
  return result;
}

void writeLabels(const std::string& path,
                 const std::vector<RootLabel>& training,
                 const std::vector<RootLabel>& heldout) {
  std::ofstream output(path);
  if (!output) throw std::runtime_error("could not write scaled labels");
  output << std::setprecision(17)
         << "{\"format\":\"drop7-public-d4-root-labels-v2\","
            "\"wholeGameSplit\":true,\"trainingSeedStart\":"
         << kTrainingSeedStart << ",\"trainingGames\":" << kTrainingGames
         << ",\"trainingRecords\":" << training.size()
         << ",\"heldoutSeedStart\":" << kHeldoutSeedStart
         << ",\"heldoutGames\":" << kHeldoutGames
         << ",\"heldoutRecords\":" << heldout.size()
         << ",\"maximumMoves\":" << kCollectionMaximumMoves
         << ",\"excluded\":[\"gameSeed\",\"score\",\"level\","
            "\"moveIndex\",\"history\",\"futureTape\"]}\n";
  const auto write_split = [&](std::string_view split,
                               const std::vector<RootLabel>& labels) {
    for (const RootLabel& label : labels) {
      output << "{\"split\":\"" << split << "\",\"board\":\"";
      for (const std::uint8_t cell : label.board) {
        output << static_cast<char>('0' + cell);
      }
      output << "\",\"nextDisc\":" << static_cast<int>(label.next_disc)
             << ",\"movesRemaining\":" << label.moves_remaining
             << ",\"action\":" << label.labeled_action
             << ",\"rootQ\":[";
      for (int action = 0; action < kBoardSize; ++action) {
        if (action != 0) output << ',';
        if (label.legal[action]) {
          output << label.q[action];
        } else {
          output << "null";
        }
      }
      output << "]}\n";
    }
  };
  write_split("training", training);
  write_split("heldout", heldout);
  if (!output) throw std::runtime_error("scaled label write failed");
}

void writeGameSummary(std::ostream& output, const GameSummary& value) {
  output << "{\"games\":" << value.games << ",\"meanScore\":"
         << value.mean_score << ",\"meanMoves\":" << value.mean_moves
         << ",\"censored\":" << value.censored
         << ",\"clearsPerMove\":" << value.clears_per_move
         << ",\"revealsPerMove\":" << value.reveals_per_move
         << ",\"movesPerSecond\":" << value.moves_per_second << '}';
}

void writeGames(std::ostream& output,
                const std::vector<CollectedGame>& games) {
  output << '[';
  for (std::size_t index = 0; index < games.size(); ++index) {
    if (index != 0) output << ',';
    const GameResult& game = games[index].game;
    output << "{\"seed\":" << game.seed << ",\"score\":" << game.score
           << ",\"moves\":" << game.moves << ",\"censored\":"
           << (game.censored ? "true" : "false")
           << ",\"labels\":" << games[index].labels.size()
           << ",\"cleared\":" << game.cleared
           << ",\"revealed\":" << game.revealed
           << ",\"maximumChain\":" << game.maximum_chain
           << ",\"teacherWork\":" << game.teacher_work
           << ",\"elapsedSeconds\":" << game.elapsed_seconds << '}';
  }
  output << ']';
}

void writeStudentGames(std::ostream& output,
                       const std::vector<GameResult>& games) {
  output << '[';
  for (std::size_t index = 0; index < games.size(); ++index) {
    if (index != 0) output << ',';
    const GameResult& game = games[index];
    output << "{\"seed\":" << game.seed << ",\"score\":" << game.score
           << ",\"moves\":" << game.moves << ",\"censored\":"
           << (game.censored ? "true" : "false")
           << ",\"cleared\":" << game.cleared
           << ",\"revealed\":" << game.revealed
           << ",\"maximumChain\":" << game.maximum_chain
           << ",\"elapsedSeconds\":" << game.elapsed_seconds << '}';
  }
  output << ']';
}

bool absoluteRankingGate(const Ranking& all, const Ranking& first_half,
                         const Ranking& second_half) {
  return top1Rate(all) >= kMinimumHeldoutTop1 &&
         top2Rate(all) >= kMinimumHeldoutTop2 &&
         pairwiseRate(all) >= kMinimumHeldoutPairwise &&
         regret(all) <= kMaximumHeldoutRegret &&
         top1Rate(first_half) >= kMinimumHalfTop1 &&
         top1Rate(second_half) >= kMinimumHalfTop1 &&
         pairwiseRate(first_half) >= kMinimumHalfPairwise &&
         pairwiseRate(second_half) >= kMinimumHalfPairwise;
}

bool residualImprovesD2(const Ranking& residual, const Ranking& d2) {
  return top1Rate(residual) >=
             top1Rate(d2) + kMinimumTop1ImprovementOverD2 &&
         top2Rate(residual) >=
             top2Rate(d2) + kMinimumTop2ImprovementOverD2 &&
         pairwiseRate(residual) >=
             pairwiseRate(d2) + kMinimumPairwiseImprovementOverD2 &&
         regret(residual) <= kMaximumRegretRatioToD2 * regret(d2);
}

bool rolloutGate(const GameSummary& teacher, const GameSummary& candidate) {
  return candidate.mean_score >= kMinimumRolloutScoreRatio * teacher.mean_score &&
         candidate.mean_moves >= kMinimumRolloutMoveRatio * teacher.mean_moves &&
         candidate.clears_per_move >=
             kMinimumRolloutThroughputRatio * teacher.clears_per_move &&
         candidate.reveals_per_move >=
             kMinimumRolloutThroughputRatio * teacher.reveals_per_move;
}

void writePausedArtifact(const Options& options, const CollectedGame& pilot,
                         double projected_wall, std::string_view reason) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not write paused artifact");
  output << std::setprecision(10)
         << "{\n  \"experiment\":\"scaled-fair-d4-distillation\",\n"
            "  \"status\":\"paused-runtime-gate\",\n"
            "  \"reason\":\""
         << reason << "\",\n  \"seedProtocol\":{\"trainingSeedStart\":"
         << kTrainingSeedStart << ",\"trainingSeedsRead\":1,"
            "\"heldoutSeedStart\":"
         << kHeldoutSeedStart << ",\"heldoutSeedsRead\":0},\n"
            "  \"runtimeProjection\":{\"pilotSeconds\":"
         << pilot.game.elapsed_seconds << ",\"projectedWallSeconds\":"
         << projected_wall << ",\"limitSeconds\":"
         << kMaximumProjectedWallSeconds << "},\n  \"pilot\":";
  writeGames(output, std::vector<CollectedGame>{pilot});
  output << ",\n  \"protectedSeedFamiliesRead\":false,\n"
            "  \"peakRssBytes\":"
         << peakRssBytes() << "\n}\n";
}

void writeArtifact(
    const Options& options, const std::vector<CollectedGame>& training_games,
    const std::vector<CollectedGame>& heldout_games,
    const Ranking& training_d2, const Ranking& training_residual,
    const Ranking& heldout_d2, const Ranking& heldout_d2_first,
    const Ranking& heldout_d2_second, const Ranking& heldout_residual,
    const Ranking& heldout_residual_first,
    const Ranking& heldout_residual_second, const Throughput& d2_throughput,
    const Throughput& residual_throughput, const GameSummary& teacher_summary,
    const std::vector<GameResult>* d2_games,
    const GameSummary* d2_summary, bool d2_ranking_passed,
    bool d2_rollout_passed, const std::vector<GameResult>* residual_games,
    const GameSummary* residual_summary, bool residual_ranking_passed,
    bool residual_rollout_passed, std::uint64_t checkpoint_bytes,
    std::uint64_t model_fingerprint, double pilot_projection,
    double total_wall_seconds) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not write distill artifact");
  output << std::setprecision(10)
         << "{\n  \"experiment\":\"scaled-fair-d4-distillation\",\n"
            "  \"status\":\"complete\",\n"
            "  \"claimBoundary\":\"D4 behavior compression only; no stronger-policy claim\",\n"
            "  \"seedProtocol\":{\"trainingSeedStart\":"
         << kTrainingSeedStart << ",\"trainingGames\":" << kTrainingGames
         << ",\"heldoutSeedStart\":" << kHeldoutSeedStart
         << ",\"heldoutGames\":" << kHeldoutGames
         << ",\"maximumMoves\":" << kCollectionMaximumMoves
         << ",\"wholeGameSplit\":true,\"fresh3eSeedsRead\":0,"
            "\"validation7dSeedsRead\":0,\"finalD7SeedsRead\":0},\n"
            "  \"runtimeGate\":{\"pilotProjectedWallSeconds\":"
         << pilot_projection << ",\"limitSeconds\":"
         << kMaximumProjectedWallSeconds << ",\"passed\":true},\n"
            "  \"inputBoundary\":{\"included\":[\"board\",\"nextDisc\",\"movesRemaining\"],"
            "\"excluded\":[\"gameSeed\",\"score\",\"level\",\"movesPlayed\",\"history\",\"futureTape\"],"
            "\"reflection\":\"canonical public state plus two-pass residual\"},\n"
            "  \"architecture\":{\"anchor\":\"full-width public fair D2/five strata\","
            "\"residual\":\"action-relative sparse linear NNUE\","
            "\"featureCount\":"
         << kFeatureCount << ",\"checkpointBytes\":" << checkpoint_bytes
         << ",\"checkpointLimitBytes\":" << kMaximumCheckpointBytes
         << ",\"checkpoint\":\"" << options.checkpoint
         << "\",\"fingerprintFnv1a64\":\"0x" << std::hex
         << model_fingerprint << std::dec << "\",\"labels\":\""
         << options.labels << "\"},\n"
            "  \"training\":{\"epochs\":"
         << kDesignEpochs << ",\"batchSize\":" << kDesignBatchSize
         << ",\"learningRate\":" << kDesignLearningRate
         << ",\"l2\":0.03,\"pairwiseLossWeight\":"
         << kPairwiseLossWeight << ",\"baseLogitScale\":"
         << kBaseLogitScale << ",\"records\":";
  std::size_t training_records = 0;
  for (const CollectedGame& game : training_games) {
    training_records += game.labels.size();
  }
  std::size_t heldout_records = 0;
  for (const CollectedGame& game : heldout_games) {
    heldout_records += game.labels.size();
  }
  output << training_records << ',';
  writeRanking(output, "d2", training_d2);
  output << ',';
  writeRanking(output, "residual", training_residual);
  output << "},\n  \"heldout\":{\"records\":" << heldout_records << ',';
  writeRanking(output, "d2All", heldout_d2);
  output << ',';
  writeRanking(output, "d2FirstSixGames", heldout_d2_first);
  output << ',';
  writeRanking(output, "d2SecondSixGames", heldout_d2_second);
  output << ',';
  writeRanking(output, "residualAll", heldout_residual);
  output << ',';
  writeRanking(output, "residualFirstSixGames", heldout_residual_first);
  output << ',';
  writeRanking(output, "residualSecondSixGames", heldout_residual_second);
  output << "},\n  \"gates\":{\"absoluteMinimums\":{"
            "\"top1\":"
         << kMinimumHeldoutTop1 << ",\"top2\":" << kMinimumHeldoutTop2
         << ",\"pairwise\":" << kMinimumHeldoutPairwise
         << ",\"halfTop1\":" << kMinimumHalfTop1
         << ",\"halfPairwise\":" << kMinimumHalfPairwise
         << ",\"maximumRegret\":" << kMaximumHeldoutRegret
         << "},\"residualImprovementMinimums\":{\"top1\":"
         << kMinimumTop1ImprovementOverD2 << ",\"top2\":"
         << kMinimumTop2ImprovementOverD2 << ",\"pairwise\":"
         << kMinimumPairwiseImprovementOverD2
         << ",\"maximumRegretRatio\":" << kMaximumRegretRatioToD2
         << "},\"d2RankingPassed\":"
         << (d2_ranking_passed ? "true" : "false")
         << ",\"residualRankingPassed\":"
         << (residual_ranking_passed ? "true" : "false") << "},\n"
            "  \"inference\":{\"d2\":{\"roots\":"
         << d2_throughput.roots << ",\"seconds\":" << d2_throughput.seconds
         << ",\"rootsPerSecond\":" << d2_throughput.roots_per_second
         << ",\"checksum\":" << d2_throughput.checksum
         << "},\"residual\":{\"roots\":" << residual_throughput.roots
         << ",\"seconds\":" << residual_throughput.seconds
         << ",\"rootsPerSecond\":"
         << residual_throughput.roots_per_second << ",\"checksum\":"
         << residual_throughput.checksum << "}},\n"
            "  \"rolloutSanity\":{\"teacher\":";
  writeGameSummary(output, teacher_summary);
  output << ",\"d2\":";
  if (d2_summary == nullptr) {
    output << "null";
  } else {
    writeGameSummary(output, *d2_summary);
  }
  output << ",\"d2Passed\":" << (d2_rollout_passed ? "true" : "false")
         << ",\"residual\":";
  if (residual_summary == nullptr) {
    output << "null";
  } else {
    writeGameSummary(output, *residual_summary);
  }
  output << ",\"residualPassed\":"
         << (residual_rollout_passed ? "true" : "false") << "},\n"
            "  \"teacherGames\":";
  writeGames(output, heldout_games);
  output << ",\n  \"d2Games\":";
  if (d2_games == nullptr) {
    output << "null";
  } else {
    writeStudentGames(output, *d2_games);
  }
  output << ",\n  \"residualGames\":";
  if (residual_games == nullptr) {
    output << "null";
  } else {
    writeStudentGames(output, *residual_games);
  }
  output << ",\n  \"trainingTeacherGames\":";
  writeGames(output, training_games);
  output << ",\n  \"totalWallSeconds\":" << total_wall_seconds
         << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
}

bool selfTest(const Options& options, std::ostream& output) {
  const bool inherited = fair::selfTest(output);
  const State fixture = fair::frozen::fixtureState(
      fair::frozen::kTypeScriptFixtures[1]);
  const fair::SearchDecision teacher = fair::chooseDepth4Action(fixture);
  RootLabel label = behaviorLabel(fixture, teacher);
  label.game = 0;
  label.move_in_game = 0;
  const PreparedRoot prepared = prepare(label);
  std::vector<PreparedRoot> tiny(32, prepared);
  const LinearModel first = trainLinear(tiny, 0.03);
  const LinearModel repeat = trainLinear(tiny, 0.03);
  const bool deterministic = fingerprint(first) == fingerprint(repeat);
  writeCheckpoint(options.checkpoint, first);
  const LinearModel restored = readCheckpoint(options.checkpoint);
  const bool checkpoint = fingerprint(first) == fingerprint(restored) &&
                          fileBytes(options.checkpoint) <=
                              kMaximumCheckpointBytes;
  const int action = studentAction(restored, fixture);
  State reflected = fixture;
  reflected.board = cfpi::detail::mirrorBoard(fixture.board);
  const int reflected_action = studentAction(restored, reflected);
  State metadata = fixture;
  metadata.score = 9'000'000;
  metadata.level = 80;
  metadata.moves_played = 490;
  const bool reflection = reflected_action == kBoardSize - 1 - action;
  const bool metadata_blind = studentAction(restored, metadata) == action;
  const bool legal = isLegal(fixture.board, action) &&
                     isLegal(fixture.board, d2Action(fixture));
  LinearModel zero{};
  const auto zero_scores = modelScores(zero, prepared);
  int direct_d2 = -1;
  for (const int candidate : kActionOrder) {
    if (!label.legal[candidate]) continue;
    if (direct_d2 < 0 ||
        zero_scores[candidate] > zero_scores[direct_d2]) {
      direct_d2 = candidate;
    }
  }
  const bool anchor = d2Action(fixture) == direct_d2;
  bool feature_bounds = true;
  for (const auto& by_action : {prepared.direct, prepared.reflected}) {
    for (const FeatureVector& features : by_action) {
      for (const SparseFeature& feature : features) {
        feature_bounds = feature_bounds && feature.index < kFeatureCount &&
                         std::isfinite(feature.value);
      }
    }
  }
  const bool resources = sizeof(LinearModel) ==
                             static_cast<std::size_t>(kFeatureCount) *
                                 sizeof(double) &&
                         kFeatureCount == 1'647 &&
                         fileBytes(options.checkpoint) <= 32'768;
  const bool protocol = kTrainingSeedStart == 0x3df2'0000u &&
                        kHeldoutSeedStart == 0x3df3'0000u &&
                        kTrainingGames == 24 && kHeldoutGames == 12 &&
                        kMinimumHeldoutTop1 == 0.55 &&
                        kMinimumHeldoutPairwise == 0.72 &&
                        kMinimumTop1ImprovementOverD2 == 0.01;
  const bool passed = inherited && deterministic && checkpoint && reflection &&
                      metadata_blind && legal && anchor && feature_bounds &&
                      resources && protocol;
  output << "SCALED_D4_DISTILL_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"inheritedD4\":" << (inherited ? "true" : "false")
         << ",\"deterministicTraining\":"
         << (deterministic ? "true" : "false")
         << ",\"checkpoint\":" << (checkpoint ? "true" : "false")
         << ",\"reflection\":" << (reflection ? "true" : "false")
         << ",\"metadataBlind\":"
         << (metadata_blind ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false")
         << ",\"zeroResidualD2Anchor\":" << (anchor ? "true" : "false")
         << ",\"featureBounds\":" << (feature_bounds ? "true" : "false")
         << ",\"resources\":" << (resources ? "true" : "false")
         << ",\"protocol\":" << (protocol ? "true" : "false")
         << ",\"parameters\":" << kFeatureCount << "}\n";
  return passed;
}

int pilotOnly(std::ostream& output) {
  const CollectedGame pilot =
      collectTeacherGame(kTrainingSeedStart, "pilot-only");
  const double projected_wall =
      pilot.game.elapsed_seconds *
      std::ceil(static_cast<double>(kTrainingGames + kHeldoutGames) /
                kParallelism) *
      kProjectionSafetyFactor;
  output << std::fixed << std::setprecision(4)
         << "SCALED_D4_DISTILL_PILOT {\"seed\":" << pilot.game.seed
         << ",\"score\":" << pilot.game.score
         << ",\"moves\":" << pilot.game.moves
         << ",\"censored\":" << (pilot.game.censored ? "true" : "false")
         << ",\"labels\":" << pilot.labels.size()
         << ",\"elapsedSeconds\":" << pilot.game.elapsed_seconds
         << ",\"projectedWallSeconds\":" << projected_wall
         << ",\"limitSeconds\":" << kMaximumProjectedWallSeconds
         << ",\"passed\":"
         << (projected_wall <= kMaximumProjectedWallSeconds ? "true" : "false")
         << ",\"nextSeedRead\":false}\n";
  return 0;
}

int auditExisting(const Options& options, std::ostream& output) {
  const auto started = std::chrono::steady_clock::now();
  const std::vector<RootLabel> training_labels =
      loadSplit(options.labels, "training");
  const std::vector<RootLabel> heldout_labels =
      loadSplit(options.labels, "heldout");
  if (training_labels.size() != 1'885 || heldout_labels.size() != 926) {
    throw std::runtime_error("unexpected frozen scaled-distill label counts");
  }
  const std::vector<PreparedRoot> prepared_training =
      prepareAll(training_labels);
  const std::vector<PreparedRoot> prepared_heldout = prepareAll(heldout_labels);
  const LinearModel frozen = readCheckpoint(options.checkpoint);
  const LinearModel repeated = trainLinear(prepared_training, 0.03);
  if (fingerprint(frozen) != fingerprint(repeated)) {
    throw std::runtime_error("frozen scaled-distill retrain mismatch");
  }
  const Ranking d2_all =
      evaluateRange(nullptr, prepared_heldout, 0, kHeldoutGames);
  const Ranking d2_first =
      evaluateRange(nullptr, prepared_heldout, 0, kHeldoutGames / 2);
  const Ranking d2_second = evaluateRange(
      nullptr, prepared_heldout, kHeldoutGames / 2, kHeldoutGames);
  const Ranking residual_all =
      evaluateRange(&frozen, prepared_heldout, 0, kHeldoutGames);
  const Ranking residual_first =
      evaluateRange(&frozen, prepared_heldout, 0, kHeldoutGames / 2);
  const Ranking residual_second = evaluateRange(
      &frozen, prepared_heldout, kHeldoutGames / 2, kHeldoutGames);
  const Throughput d2_throughput = benchmarkD2Inference(heldout_labels);
  const Throughput residual_throughput =
      benchmarkInference(frozen, heldout_labels);
  const SearchCost search_cost = measureSearchCost(heldout_labels);
  const bool d2_passed =
      absoluteRankingGate(d2_all, d2_first, d2_second) &&
      d2_throughput.roots_per_second >= kMinimumInferenceRootsPerSecond;
  const bool residual_passed =
      absoluteRankingGate(residual_all, residual_first, residual_second) &&
      residualImprovesD2(residual_all, d2_all) &&
      residualImprovesD2(residual_first, d2_first) &&
      residualImprovesD2(residual_second, d2_second) &&
      residual_throughput.roots_per_second >=
          kMinimumInferenceRootsPerSecond &&
      fileBytes(options.checkpoint) <= kMaximumCheckpointBytes;
  const double elapsed = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - started)
                             .count();
  std::ofstream artifact(options.output);
  if (!artifact) throw std::runtime_error("could not write frozen audit");
  artifact << std::setprecision(10)
           << "{\n  \"experiment\":\"scaled-fair-d4-frozen-label-audit\",\n"
              "  \"status\":\"complete\",\n"
              "  \"sourceProtocol\":{\"trainingSeedStart\":"
           << kTrainingSeedStart << ",\"trainingGames\":" << kTrainingGames
           << ",\"heldoutSeedStart\":" << kHeldoutSeedStart
           << ",\"heldoutGames\":" << kHeldoutGames
           << ",\"trainingRecords\":" << training_labels.size()
           << ",\"heldoutRecords\":" << heldout_labels.size()
           << ",\"maximumMoves\":" << kCollectionMaximumMoves
           << ",\"pilotSeedUnique\":1,\"pilotSeedExecutions\":2,"
              "\"pilotOnlyLabelsPersisted\":false,\"fresh3eSeedsRead\":0,"
              "\"validation7dSeedsRead\":0,\"finalD7SeedsRead\":0},\n"
              "  \"protocolDeviation\":{\"kind\":\"inadvertent-D2-sanity-after-residual-rejection\","
              "\"newSeedFamiliesOpened\":false,\"residualGameplayRan\":false,"
              "\"diagnosticAccepted\":false,\"canonicalEvidence\":\"ranking-only\"},\n"
              "  \"architecture\":{\"anchor\":\"full-width public fair D2/five strata\","
              "\"residual\":\"1647-weight action-relative reflection-averaged sparse linear NNUE\","
              "\"checkpointBytes\":"
           << fileBytes(options.checkpoint)
           << ",\"fingerprintFnv1a64\":\"0x" << std::hex
           << fingerprint(frozen) << std::dec
           << "\",\"deterministicRetrain\":true},\n  \"heldout\":{";
  writeRanking(artifact, "d2All", d2_all);
  artifact << ',';
  writeRanking(artifact, "d2FirstSixGames", d2_first);
  artifact << ',';
  writeRanking(artifact, "d2SecondSixGames", d2_second);
  artifact << ',';
  writeRanking(artifact, "residualAll", residual_all);
  artifact << ',';
  writeRanking(artifact, "residualFirstSixGames", residual_first);
  artifact << ',';
  writeRanking(artifact, "residualSecondSixGames", residual_second);
  artifact << "},\n  \"gate\":{\"d2Passed\":"
           << (d2_passed ? "true" : "false")
           << ",\"residualPassed\":"
           << (residual_passed ? "true" : "false")
           << ",\"residualRequiredToBeatD2OnAllMetricsAndHalves\":true},\n"
              "  \"searchCost\":{\"roots\":"
           << search_cost.roots << ",\"d2WorkPerRoot\":"
           << search_cost.d2_work_per_root << ",\"d2NodesPerRoot\":"
           << search_cost.d2_nodes_per_root
           << ",\"d2CacheHitsPerRoot\":"
           << search_cost.d2_cache_hits_per_root
           << ",\"d1PlusD2WorkPerResidualRoot\":"
           << search_cost.d1_plus_d2_work_per_root
           << ",\"maximumD2Work\":" << search_cost.maximum_d2_work
           << ",\"maximumD2CacheEntries\":"
           << search_cost.maximum_d2_cache_entries << "},\n"
              "  \"throughput\":{\"d2RootsPerSecond\":"
           << d2_throughput.roots_per_second
           << ",\"d2BenchmarkSeconds\":" << d2_throughput.seconds
           << ",\"d2Checksum\":" << d2_throughput.checksum
           << ",\"residualRootsPerSecond\":"
           << residual_throughput.roots_per_second
           << ",\"residualBenchmarkSeconds\":"
           << residual_throughput.seconds << ",\"residualChecksum\":"
           << residual_throughput.checksum << "},\n"
              "  \"acceptedGameplayEvidence\":null,\n"
              "  \"conclusion\":\"exact public D2 is the accepted fast rollout primitive; learned residual rejected\",\n"
              "  \"auditWallSeconds\":"
           << elapsed << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
  output << std::fixed << std::setprecision(6)
         << "SCALED_D4_FROZEN_AUDIT {\"d2Top1\":" << top1Rate(d2_all)
         << ",\"d2Pairwise\":" << pairwiseRate(d2_all)
         << ",\"d2Regret\":" << regret(d2_all)
         << ",\"residualTop1\":" << top1Rate(residual_all)
         << ",\"residualPairwise\":" << pairwiseRate(residual_all)
         << ",\"residualRegret\":" << regret(residual_all)
         << ",\"d2Passed\":" << (d2_passed ? "true" : "false")
         << ",\"residualPassed\":" << (residual_passed ? "true" : "false")
         << ",\"d2WorkPerRoot\":" << search_cost.d2_work_per_root
         << ",\"d2RootsPerSecond\":" << d2_throughput.roots_per_second
         << ",\"gameplayAccepted\":false,\"artifact\":\""
         << options.output << "\"}\n";
  return 0;
}

int run(const Options& options, std::ostream& output) {
  const auto started = std::chrono::steady_clock::now();
  // Architecture, optimizer, split, and all gates above are constants.  The
  // first evaluation seed is used only for a wall-time projection, never selection.
  CollectedGame pilot = collectTeacherGame(kTrainingSeedStart, "pilot-fit");
  const double projected_wall =
      pilot.game.elapsed_seconds *
      std::ceil(static_cast<double>(kTrainingGames + kHeldoutGames) /
                kParallelism) *
      kProjectionSafetyFactor;
  if (projected_wall > kMaximumProjectedWallSeconds) {
    writePausedArtifact(options, pilot, projected_wall,
                        "first-seed useful-corpus projection exceeded limit");
    output << "SCALED_D4_DISTILL_RESULT {\"status\":\"paused-runtime-gate\","
              "\"trainingSeedsRead\":1,\"heldoutSeedsRead\":0,"
              "\"projectedWallSeconds\":"
           << projected_wall << "}\n";
    return 0;
  }

  std::vector<CollectedGame> training_games(kTrainingGames);
  training_games[0] = std::move(pilot);
  std::vector<CollectedGame> remaining = collectTeacherRange(
      kTrainingSeedStart + 1, kTrainingGames - 1, "fitting");
  for (int index = 1; index < kTrainingGames; ++index) {
    training_games[index] = std::move(remaining[index - 1]);
  }
  const std::vector<RootLabel> training_labels = flattenLabels(training_games);
  if (static_cast<int>(training_labels.size()) < kMinimumTrainingLabels) {
    throw std::runtime_error("scaled-distill fitting corpus was too small");
  }
  const std::vector<PreparedRoot> prepared_training =
      prepareAll(training_labels);
  const LinearModel trained = trainLinear(prepared_training, 0.03);
  writeCheckpoint(options.checkpoint, trained);
  const LinearModel frozen = readCheckpoint(options.checkpoint);
  const std::uint64_t model_fingerprint = fingerprint(frozen);

  double training_cpu_seconds = 0.0;
  for (const CollectedGame& game : training_games) {
    training_cpu_seconds += game.game.elapsed_seconds;
  }
  const double heldout_projection =
      training_cpu_seconds / kTrainingGames * kHeldoutGames / kParallelism *
      kProjectionSafetyFactor;
  const double elapsed_before_holdout = std::chrono::duration<double>(
                                            std::chrono::steady_clock::now() -
                                            started)
                                            .count();
  if (elapsed_before_holdout + heldout_projection >
      kMaximumProjectedWallSeconds) {
    std::ofstream paused(options.output);
    if (!paused) throw std::runtime_error("could not write training pause");
    paused << std::setprecision(10)
           << "{\"experiment\":\"scaled-fair-d4-distillation\","
              "\"status\":\"paused-before-heldout-runtime-gate\","
              "\"trainingSeedStart\":"
           << kTrainingSeedStart << ",\"trainingSeedsRead\":"
           << kTrainingGames << ",\"trainingRecords\":"
           << training_labels.size() << ",\"heldoutSeedsRead\":0,"
              "\"projectedTotalWallSeconds\":"
           << elapsed_before_holdout + heldout_projection
           << ",\"limitSeconds\":" << kMaximumProjectedWallSeconds << "}\n";
    output << "SCALED_D4_DISTILL_RESULT {\"status\":\"paused-before-heldout-runtime-gate\","
              "\"trainingSeedsRead\":"
           << kTrainingGames << ",\"heldoutSeedsRead\":0}\n";
    return 0;
  }

  // Read the heldout exactly once after locking the checkpoint.
  const std::vector<CollectedGame> heldout_games = collectTeacherRange(
      kHeldoutSeedStart, kHeldoutGames, "heldout");
  const std::vector<RootLabel> heldout_labels = flattenLabels(heldout_games);
  if (static_cast<int>(heldout_labels.size()) < kMinimumHeldoutLabels) {
    throw std::runtime_error("scaled-distill heldout corpus was too small");
  }
  writeLabels(options.labels, training_labels, heldout_labels);
  const std::vector<PreparedRoot> prepared_heldout = prepareAll(heldout_labels);

  const Ranking training_d2 =
      evaluateRange(nullptr, prepared_training, 0, kTrainingGames);
  const Ranking training_residual =
      evaluateRange(&frozen, prepared_training, 0, kTrainingGames);
  const Ranking heldout_d2 =
      evaluateRange(nullptr, prepared_heldout, 0, kHeldoutGames);
  const Ranking heldout_d2_first = evaluateRange(
      nullptr, prepared_heldout, 0, kHeldoutGames / 2);
  const Ranking heldout_d2_second = evaluateRange(
      nullptr, prepared_heldout, kHeldoutGames / 2, kHeldoutGames);
  const Ranking heldout_residual =
      evaluateRange(&frozen, prepared_heldout, 0, kHeldoutGames);
  const Ranking heldout_residual_first = evaluateRange(
      &frozen, prepared_heldout, 0, kHeldoutGames / 2);
  const Ranking heldout_residual_second = evaluateRange(
      &frozen, prepared_heldout, kHeldoutGames / 2, kHeldoutGames);

  const Throughput d2_throughput = benchmarkD2Inference(heldout_labels);
  const Throughput residual_throughput =
      benchmarkInference(frozen, heldout_labels);
  const std::uint64_t checkpoint_bytes = fileBytes(options.checkpoint);
  const bool d2_ranking_passed =
      absoluteRankingGate(heldout_d2, heldout_d2_first,
                          heldout_d2_second) &&
      d2_throughput.roots_per_second >= kMinimumInferenceRootsPerSecond;
  const bool residual_ranking_passed =
      absoluteRankingGate(heldout_residual, heldout_residual_first,
                          heldout_residual_second) &&
      residualImprovesD2(heldout_residual, heldout_d2) &&
      residualImprovesD2(heldout_residual_first, heldout_d2_first) &&
      residualImprovesD2(heldout_residual_second, heldout_d2_second) &&
      residual_throughput.roots_per_second >=
          kMinimumInferenceRootsPerSecond &&
      checkpoint_bytes <= kMaximumCheckpointBytes;

  const GameSummary teacher_summary = summarizeTeacher(heldout_games);
  std::vector<GameResult> d2_games;
  GameSummary d2_summary;
  bool d2_rollout_passed = false;
  // Gameplay is diagnostic only after the learned residual itself proves a
  // material, split-stable improvement over exact D2.  A D2-only label pass
  // establishes a rollout primitive, not permission for another policy test.
  if (residual_ranking_passed && d2_ranking_passed) {
    d2_games = runStudentRange(kHeldoutSeedStart, kHeldoutGames,
                               LinearModel{});
    d2_summary = summarize(d2_games);
    d2_rollout_passed = rolloutGate(teacher_summary, d2_summary);
  }
  std::vector<GameResult> residual_games;
  GameSummary residual_summary;
  bool residual_rollout_passed = false;
  if (residual_ranking_passed) {
    residual_games =
        runStudentRange(kHeldoutSeedStart, kHeldoutGames, frozen);
    residual_summary = summarize(residual_games);
    residual_rollout_passed = rolloutGate(teacher_summary, residual_summary);
  }

  const double total_wall_seconds = std::chrono::duration<double>(
                                        std::chrono::steady_clock::now() -
                                        started)
                                        .count();
  writeArtifact(
      options, training_games, heldout_games, training_d2, training_residual,
      heldout_d2, heldout_d2_first, heldout_d2_second, heldout_residual,
      heldout_residual_first, heldout_residual_second, d2_throughput,
      residual_throughput, teacher_summary,
      residual_ranking_passed && d2_ranking_passed ? &d2_games : nullptr,
      residual_ranking_passed && d2_ranking_passed ? &d2_summary : nullptr,
      d2_ranking_passed,
      d2_rollout_passed,
      residual_ranking_passed ? &residual_games : nullptr,
      residual_ranking_passed ? &residual_summary : nullptr,
      residual_ranking_passed, residual_rollout_passed, checkpoint_bytes,
      model_fingerprint, projected_wall, total_wall_seconds);

  output << std::fixed << std::setprecision(4)
         << "SCALED_D4_DISTILL_RESULT {\"status\":\"complete\","
            "\"trainingRecords\":"
         << training_labels.size() << ",\"heldoutRecords\":"
         << heldout_labels.size() << ",\"d2Top1\":"
         << top1Rate(heldout_d2) << ",\"d2Pairwise\":"
         << pairwiseRate(heldout_d2) << ",\"d2Regret\":"
         << regret(heldout_d2) << ",\"residualTop1\":"
         << top1Rate(heldout_residual) << ",\"residualPairwise\":"
         << pairwiseRate(heldout_residual) << ",\"residualRegret\":"
         << regret(heldout_residual) << ",\"d2RankingPassed\":"
         << (d2_ranking_passed ? "true" : "false")
         << ",\"d2RolloutPassed\":"
         << (d2_rollout_passed ? "true" : "false")
         << ",\"residualRankingPassed\":"
         << (residual_ranking_passed ? "true" : "false")
         << ",\"residualRolloutRan\":"
         << (residual_ranking_passed ? "true" : "false")
         << ",\"d2RootsPerSecond\":" << d2_throughput.roots_per_second
         << ",\"checkpointBytes\":" << checkpoint_bytes
         << ",\"totalWallSeconds\":" << total_wall_seconds
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return 0;
}

}  // namespace drop7::scaled_d4_distill

#ifndef DROP7_SCALED_D4_DISTILL_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--design-old") {
      std::string labels = "/tmp/drop7-d4-public-root-labels.jsonl";
      if (argc == 4 && std::string_view(argv[2]) == "--labels") {
        labels = argv[3];
      } else if (argc != 2) {
        throw std::invalid_argument("invalid --design-old arguments");
      }
      return drop7::scaled_d4_distill::designOld(labels);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      const auto options =
          drop7::scaled_d4_distill::parseOptions(argc, argv, 2);
      return drop7::scaled_d4_distill::selfTest(options, std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--pilot-only") {
      return drop7::scaled_d4_distill::pilotOnly(std::cout);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--audit-existing") {
      const auto options =
          drop7::scaled_d4_distill::parseOptions(argc, argv, 2);
      return drop7::scaled_d4_distill::auditExisting(options, std::cout);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::scaled_d4_distill::parseOptions(argc, argv, 2);
      return drop7::scaled_d4_distill::run(options, std::cout);
    }
    std::cerr << "usage: drop7_scaled_d4_distill "
                 "--design-old [--labels PATH] | --self-test | "
                 "--pilot-only | --audit-existing | --run "
                 "[--output PATH --checkpoint PATH --labels PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_scaled_d4_distill: " << error.what() << '\n';
    return 1;
  }
}
#endif
