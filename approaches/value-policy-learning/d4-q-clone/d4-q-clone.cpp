#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#include "../../fair-expectimax/reference/fair-only-depth4.cpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <list>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <utility>
#include <vector>

// Performs bounded behavior compression for the reference fair-only D4
// policy.  The clone learns only within-root action ordering from normalized
// root-Q vectors; absolute Q scale and all game metadata are excluded.
namespace drop7::d4_q_clone {

namespace fair = drop7::fair_only_depth4;
using Clock = std::chrono::steady_clock;

constexpr int kTrainingRecords = 1'508;
constexpr int kHeldoutRecords = 465;
constexpr int kTrainingGames = 16;
constexpr int kHeldoutGames = 8;
constexpr std::uint32_t kRolloutSeedStart = 0x3de3'0000u;
constexpr int kRolloutGames = 32;
constexpr int kMaximumMoves = 1'000;
constexpr int kParallelism = 4;

constexpr int kCellKinds = 10;
constexpr int kBoardInputs = kCellCount * kCellKinds;
constexpr int kNextDiscInputs = kBoardSize;
constexpr int kRiseInputs = kMovesPerLevel;
constexpr int kInputCount = kBoardInputs + kNextDiscInputs + kRiseInputs;
constexpr int kActiveInputs = kCellCount + 2;
constexpr int kHidden = 24;
constexpr int kParameterCount =
    kInputCount * kHidden + kHidden + kHidden * kBoardSize + kBoardSize;
constexpr int kEpochs = 260;
constexpr int kBatchSize = 64;
constexpr double kLearningRate = 0.0025;
constexpr double kL2 = 0.00015;
constexpr double kTargetTemperature = 0.18;
constexpr double kPairwiseWeight = 0.35;
constexpr double kTieTolerance = 1.0e-9;
constexpr std::uint32_t kInitializationSeed = 0x5143'4c4eu;

constexpr double kMinimumTop1WithTies = 0.35;
constexpr double kMinimumTop2 = 0.55;
constexpr double kMinimumPairwiseAccuracy = 0.65;
constexpr double kMinimumHalfPairwiseAccuracy = 0.62;
constexpr double kMaximumCenterRegretRatio = 0.90;
constexpr double kMaximumOnePlyRegretRatio = 0.95;

constexpr std::array<int, kBoardSize> kActionOrder{{3, 2, 4, 1, 5, 0, 6}};
constexpr std::array<char, 8> kCheckpointMagic{{'D', '7', 'Q', 'C', 'L', 'N',
                                                '1', '\0'}};

static_assert(kLevelBonus == 7'000);
static_assert(kInputCount == 502);
static_assert(kActiveInputs == 51);
static_assert(kParameterCount == 12'247);
static_assert(kParameterCount < 16'384);
static_assert(kRolloutSeedStart >= 0x3d00'0000u &&
              kRolloutSeedStart + kRolloutGames < 0x3e00'0000u);
static_assert(fair::kCandidateDepth == 4);

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

struct RootLabel {
  Board board{};
  std::uint8_t next_disc = 1;
  int moves_remaining = kMovesPerLevel;
  int labeled_action = -1;
  std::array<double, kBoardSize> q{};
  std::array<bool, kBoardSize> legal{};
  int game = -1;  // Diagnostic split only; never enters model input.
  int move_in_game = -1;  // Band diagnostic only; never enters model input.
};

int integerAfter(std::string_view line, std::string_view marker) {
  const std::size_t found = line.find(marker);
  if (found == std::string_view::npos) {
    throw std::runtime_error("missing Q-label integer field");
  }
  const char* begin = line.data() + found + marker.size();
  char* end = nullptr;
  const long parsed = std::strtol(begin, &end, 10);
  if (end == begin || parsed < std::numeric_limits<int>::min() ||
      parsed > std::numeric_limits<int>::max()) {
    throw std::runtime_error("invalid Q-label integer field");
  }
  return static_cast<int>(parsed);
}

RootLabel parseLabel(std::string_view line, std::string_view split,
                     int game) {
  const std::string split_marker = "\"split\":\"" + std::string(split) + "\"";
  if (line.find(split_marker) == std::string_view::npos) {
    throw std::runtime_error("Q-label split mismatch");
  }
  constexpr std::string_view board_marker = "\"board\":\"";
  const std::size_t board_at = line.find(board_marker);
  if (board_at == std::string_view::npos ||
      board_at + board_marker.size() + kCellCount > line.size()) {
    throw std::runtime_error("invalid Q-label board");
  }
  RootLabel result;
  result.game = game;
  for (int cell = 0; cell < kCellCount; ++cell) {
    const char encoded = line[board_at + board_marker.size() + cell];
    if (encoded < '0' || encoded > '9') {
      throw std::runtime_error("invalid Q-label cell");
    }
    result.board[cell] = static_cast<std::uint8_t>(encoded - '0');
  }
  result.next_disc =
      static_cast<std::uint8_t>(integerAfter(line, "\"nextDisc\":"));
  result.moves_remaining = integerAfter(line, "\"movesRemaining\":");
  result.labeled_action = integerAfter(line, "\"action\":");
  if (result.next_disc < 1 || result.next_disc > kBoardSize ||
      result.moves_remaining < 1 ||
      result.moves_remaining > kMovesPerLevel ||
      result.labeled_action < 0 || result.labeled_action >= kBoardSize) {
    throw std::runtime_error("invalid Q-label public fields");
  }
  constexpr std::string_view q_marker = "\"rootQ\":[";
  std::size_t cursor = line.find(q_marker);
  if (cursor == std::string_view::npos) {
    throw std::runtime_error("missing root-Q vector");
  }
  cursor += q_marker.size();
  result.q.fill(-std::numeric_limits<double>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    while (cursor < line.size() &&
           (line[cursor] == ' ' || line[cursor] == ',')) {
      ++cursor;
    }
    if (cursor >= line.size()) throw std::runtime_error("truncated root-Q vector");
    if (line.substr(cursor, 4) == "null") {
      cursor += 4;
      result.legal[action] = false;
      continue;
    }
    char* end = nullptr;
    const std::string owned(line);
    const char* begin = owned.c_str() + cursor;
    result.q[action] = std::strtod(begin, &end);
    if (end == begin || !std::isfinite(result.q[action])) {
      throw std::runtime_error("invalid root-Q value");
    }
    cursor = static_cast<std::size_t>(end - owned.c_str());
    result.legal[action] = true;
  }
  if (!result.legal[result.labeled_action] ||
      !isLegal(result.board, result.labeled_action)) {
    throw std::runtime_error("Q-label action is illegal");
  }
  for (int action = 0; action < kBoardSize; ++action) {
    if (result.legal[action] != isLegal(result.board, action)) {
      throw std::runtime_error("Q-label legal mask mismatch");
    }
  }
  if (cfpi::detail::mirroredRepresentationIsSmaller(result.board)) {
    throw std::runtime_error("Q-label board is not reflection canonical");
  }
  double maximum = -std::numeric_limits<double>::infinity();
  for (int action = 0; action < kBoardSize; ++action) {
    if (result.legal[action]) maximum = std::max(maximum, result.q[action]);
  }
  if (result.q[result.labeled_action] + kTieTolerance < maximum) {
    throw std::runtime_error("Q-label action is not root-Q optimal");
  }
  return result;
}

bool isInitialBoard(const Board& board) {
  return board == initialBoard();
}

std::vector<RootLabel> loadTraining(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("could not open Q-label dataset");
  std::string line;
  if (!std::getline(input, line) ||
      line.find("drop7-public-d4-root-labels-v1") == std::string::npos) {
    throw std::runtime_error("invalid Q-label header");
  }
  std::vector<RootLabel> result;
  result.reserve(kTrainingRecords);
  int game = -1;
  int move_in_game = -1;
  for (int index = 0; index < kTrainingRecords; ++index) {
    if (!std::getline(input, line)) {
      throw std::runtime_error("truncated training Q-label split");
    }
    RootLabel label = parseLabel(line, "training", game);
    if (isInitialBoard(label.board)) {
      ++game;
      move_in_game = 0;
      label.game = game;
    } else if (game < 0) {
      throw std::runtime_error("training split does not start at game boundary");
    } else {
      label.game = game;
      ++move_in_game;
    }
    label.move_in_game = move_in_game;
    result.push_back(std::move(label));
  }
  if (game + 1 != kTrainingGames) {
    throw std::runtime_error("training Q-label game count mismatch");
  }
  return result;
}

std::vector<RootLabel> loadHeldoutOnce(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("could not reopen Q-label dataset");
  std::string line;
  for (int skipped = 0; skipped <= kTrainingRecords; ++skipped) {
    if (!std::getline(input, line)) {
      throw std::runtime_error("could not seek heldout Q-label split");
    }
  }
  std::vector<RootLabel> result;
  result.reserve(kHeldoutRecords);
  int game = -1;
  int move_in_game = -1;
  for (int index = 0; index < kHeldoutRecords; ++index) {
    if (!std::getline(input, line)) {
      throw std::runtime_error("truncated heldout Q-label split");
    }
    RootLabel label = parseLabel(line, "heldout", game);
    if (isInitialBoard(label.board)) {
      ++game;
      move_in_game = 0;
      label.game = game;
    } else if (game < 0) {
      throw std::runtime_error("heldout split does not start at game boundary");
    } else {
      label.game = game;
      ++move_in_game;
    }
    label.move_in_game = move_in_game;
    result.push_back(std::move(label));
  }
  if (game + 1 != kHeldoutGames) {
    throw std::runtime_error("heldout Q-label game count mismatch");
  }
  if (std::getline(input, line)) {
    throw std::runtime_error("unexpected records after heldout Q-label split");
  }
  return result;
}

struct Model {
  std::array<double, kInputCount * kHidden> input{};
  std::array<double, kHidden> hidden_bias{};
  std::array<double, kHidden * kBoardSize> output{};
  std::array<double, kBoardSize> output_bias{};
};

struct Packed {
  std::array<double, kParameterCount> values{};
};

Packed pack(const Model& model) {
  Packed result;
  std::size_t cursor = 0;
  for (const double value : model.input) result.values[cursor++] = value;
  for (const double value : model.hidden_bias) result.values[cursor++] = value;
  for (const double value : model.output) result.values[cursor++] = value;
  for (const double value : model.output_bias) result.values[cursor++] = value;
  if (cursor != result.values.size()) throw std::logic_error("clone pack failed");
  return result;
}

Model unpack(const Packed& packed) {
  Model result;
  std::size_t cursor = 0;
  for (double& value : result.input) value = packed.values[cursor++];
  for (double& value : result.hidden_bias) value = packed.values[cursor++];
  for (double& value : result.output) value = packed.values[cursor++];
  for (double& value : result.output_bias) value = packed.values[cursor++];
  if (cursor != packed.values.size()) throw std::logic_error("clone unpack failed");
  return result;
}

Model initializedModel() {
  Model result;
  std::uint32_t random = kInitializationSeed;
  for (double& value : result.input) {
    random = mix32(random + 0x9e37'79b9u);
    value = (static_cast<double>(random) / 4'294'967'296.0 - 0.5) * 0.025;
  }
  for (double& value : result.output) {
    random = mix32(random + 0x9e37'79b9u);
    value = (static_cast<double>(random) / 4'294'967'296.0 - 0.5) * 0.04;
  }
  return result;
}

std::array<std::uint16_t, kActiveInputs> activeInputs(
    const Board& board, std::uint8_t next_disc, int moves_remaining) {
  std::array<std::uint16_t, kActiveInputs> result{};
  int cursor = 0;
  for (int cell = 0; cell < kCellCount; ++cell) {
    if (board[cell] >= kCellKinds) throw std::logic_error("invalid clone cell");
    result[cursor++] = static_cast<std::uint16_t>(
        cell * kCellKinds + static_cast<int>(board[cell]));
  }
  result[cursor++] = static_cast<std::uint16_t>(
      kBoardInputs + static_cast<int>(next_disc) - 1);
  result[cursor++] = static_cast<std::uint16_t>(
      kBoardInputs + kNextDiscInputs + moves_remaining - 1);
  if (cursor != kActiveInputs) throw std::logic_error("clone active input mismatch");
  return result;
}

struct BaseForward {
  std::array<std::uint16_t, kActiveInputs> active{};
  std::array<double, kHidden> preactivation{};
  std::array<double, kHidden> hidden{};
  std::array<double, kBoardSize> score{};
};

BaseForward forwardBase(const Model& model, const Board& board,
                        std::uint8_t next_disc, int moves_remaining) {
  BaseForward result;
  result.active = activeInputs(board, next_disc, moves_remaining);
  result.preactivation = model.hidden_bias;
  for (const std::uint16_t active : result.active) {
    const std::size_t offset = static_cast<std::size_t>(active) * kHidden;
    for (int hidden = 0; hidden < kHidden; ++hidden) {
      result.preactivation[hidden] += model.input[offset + hidden];
    }
  }
  for (int hidden = 0; hidden < kHidden; ++hidden) {
    result.hidden[hidden] = std::max(0.0, result.preactivation[hidden]);
  }
  result.score = model.output_bias;
  for (int action = 0; action < kBoardSize; ++action) {
    for (int hidden = 0; hidden < kHidden; ++hidden) {
      result.score[action] +=
          model.output[hidden * kBoardSize + action] * result.hidden[hidden];
    }
  }
  return result;
}

struct EquivariantForward {
  BaseForward direct{};
  BaseForward reflected{};
  std::array<double, kBoardSize> score{};
};

EquivariantForward forward(const Model& model, const Board& board,
                           std::uint8_t next_disc, int moves_remaining) {
  EquivariantForward result;
  result.direct = forwardBase(model, board, next_disc, moves_remaining);
  result.reflected = forwardBase(model, cfpi::detail::mirrorBoard(board),
                                 next_disc, moves_remaining);
  for (int action = 0; action < kBoardSize; ++action) {
    result.score[action] =
        0.5 * (result.direct.score[action] +
               result.reflected.score[kBoardSize - 1 - action]);
  }
  return result;
}

struct NormalizedRoot {
  std::array<double, kBoardSize> value{};
  double minimum = 0.0;
  double maximum = 0.0;
  double range = 0.0;
  int legal_count = 0;
};

NormalizedRoot normalizeRoot(const RootLabel& label) {
  NormalizedRoot result;
  result.minimum = std::numeric_limits<double>::infinity();
  result.maximum = -std::numeric_limits<double>::infinity();
  for (int action = 0; action < kBoardSize; ++action) {
    if (!label.legal[action]) continue;
    result.minimum = std::min(result.minimum, label.q[action]);
    result.maximum = std::max(result.maximum, label.q[action]);
    ++result.legal_count;
  }
  if (result.legal_count <= 0) throw std::logic_error("empty clone root");
  result.range = result.maximum - result.minimum;
  const double denominator = std::max(1.0e-9, result.range);
  for (int action = 0; action < kBoardSize; ++action) {
    result.value[action] = label.legal[action]
                               ? (label.q[action] - result.minimum) / denominator
                               : 0.0;
  }
  return result;
}

double sigmoid(double value) {
  if (value >= 0.0) {
    const double inverse = std::exp(-value);
    return 1.0 / (1.0 + inverse);
  }
  const double exponential = std::exp(value);
  return exponential / (1.0 + exponential);
}

std::array<double, kBoardSize> maskedSoftmax(
    const std::array<double, kBoardSize>& values,
    const std::array<bool, kBoardSize>& legal, double scale) {
  double maximum = -std::numeric_limits<double>::infinity();
  for (int action = 0; action < kBoardSize; ++action) {
    if (legal[action]) maximum = std::max(maximum, values[action] * scale);
  }
  std::array<double, kBoardSize> result{};
  double total = 0.0;
  for (int action = 0; action < kBoardSize; ++action) {
    if (!legal[action]) continue;
    result[action] = std::exp(values[action] * scale - maximum);
    total += result[action];
  }
  if (!(total > 0.0) || !std::isfinite(total)) {
    throw std::runtime_error("clone softmax failed");
  }
  for (double& value : result) value /= total;
  return result;
}

struct Objective {
  double loss = 0.0;
  double listwise_loss = 0.0;
  double pairwise_loss = 0.0;
  int pairs = 0;
  std::array<double, kBoardSize> score_gradient{};
};

Objective rootObjective(const RootLabel& label,
                        const std::array<double, kBoardSize>& scores) {
  const NormalizedRoot normalized = normalizeRoot(label);
  const auto target = maskedSoftmax(
      normalized.value, label.legal, 1.0 / kTargetTemperature);
  const auto predicted = maskedSoftmax(scores, label.legal, 1.0);
  Objective result;
  for (int action = 0; action < kBoardSize; ++action) {
    if (!label.legal[action]) continue;
    result.listwise_loss -=
        target[action] * std::log(std::max(1.0e-15, predicted[action]));
    result.score_gradient[action] = predicted[action] - target[action];
  }
  double pair_weight_sum = 0.0;
  std::array<double, kBoardSize> pair_gradient{};
  for (int first = 0; first < kBoardSize; ++first) {
    if (!label.legal[first]) continue;
    for (int second = first + 1; second < kBoardSize; ++second) {
      if (!label.legal[second]) continue;
      const double difference =
          normalized.value[first] - normalized.value[second];
      if (std::abs(difference) <= kTieTolerance) continue;
      const int better = difference > 0.0 ? first : second;
      const int worse = difference > 0.0 ? second : first;
      const double weight = 0.25 + 0.75 * std::abs(difference);
      const double margin = scores[better] - scores[worse];
      result.pairwise_loss +=
          weight * (std::max(-margin, 0.0) +
                    std::log1p(std::exp(-std::abs(margin))));
      const double derivative = -weight * sigmoid(-margin);
      pair_gradient[better] += derivative;
      pair_gradient[worse] -= derivative;
      pair_weight_sum += weight;
      ++result.pairs;
    }
  }
  if (pair_weight_sum > 0.0) {
    result.pairwise_loss /= pair_weight_sum;
    for (int action = 0; action < kBoardSize; ++action) {
      result.score_gradient[action] +=
          kPairwiseWeight * pair_gradient[action] / pair_weight_sum;
    }
  }
  result.loss = result.listwise_loss + kPairwiseWeight * result.pairwise_loss;
  return result;
}

using Gradient = Model;

void backpropagateBase(const Model& model, const BaseForward& pass,
                       const std::array<double, kBoardSize>& score_gradient,
                       Gradient& gradient) {
  std::array<double, kHidden> hidden_gradient{};
  for (int action = 0; action < kBoardSize; ++action) {
    gradient.output_bias[action] += score_gradient[action];
    for (int hidden = 0; hidden < kHidden; ++hidden) {
      gradient.output[hidden * kBoardSize + action] +=
          score_gradient[action] * pass.hidden[hidden];
      hidden_gradient[hidden] +=
          score_gradient[action] *
          model.output[hidden * kBoardSize + action];
    }
  }
  for (int hidden = 0; hidden < kHidden; ++hidden) {
    if (pass.preactivation[hidden] <= 0.0) continue;
    gradient.hidden_bias[hidden] += hidden_gradient[hidden];
    for (const std::uint16_t active : pass.active) {
      gradient.input[static_cast<std::size_t>(active) * kHidden + hidden] +=
          hidden_gradient[hidden];
    }
  }
}

Objective accumulateGradient(const Model& model, const RootLabel& label,
                             Gradient& gradient) {
  const EquivariantForward pass =
      forward(model, label.board, label.next_disc, label.moves_remaining);
  const Objective objective = rootObjective(label, pass.score);
  std::array<double, kBoardSize> direct_gradient{};
  std::array<double, kBoardSize> reflected_gradient{};
  for (int action = 0; action < kBoardSize; ++action) {
    direct_gradient[action] += 0.5 * objective.score_gradient[action];
    reflected_gradient[kBoardSize - 1 - action] +=
        0.5 * objective.score_gradient[action];
  }
  backpropagateBase(model, pass.direct, direct_gradient, gradient);
  backpropagateBase(model, pass.reflected, reflected_gradient, gradient);
  return objective;
}

double datasetLoss(const Model& model, const std::vector<RootLabel>& labels) {
  if (labels.empty()) throw std::invalid_argument("empty clone dataset");
  double result = 0.0;
  for (const RootLabel& label : labels) {
    const EquivariantForward pass =
        forward(model, label.board, label.next_disc, label.moves_remaining);
    result += rootObjective(label, pass.score).loss / labels.size();
  }
  return result;
}

template <typename Value>
void deterministicShuffle(std::vector<Value>& values, std::uint32_t seed) {
  for (std::size_t remaining = values.size(); remaining > 1; --remaining) {
    seed = mix32(seed + static_cast<std::uint32_t>(remaining));
    const std::size_t selected = seed % remaining;
    std::swap(values[remaining - 1], values[selected]);
  }
}

struct TrainingResult {
  Model model{};
  double initial_loss = 0.0;
  double final_loss = 0.0;
  double wall_seconds = 0.0;
};

TrainingResult train(const std::vector<RootLabel>& labels) {
  if (labels.size() != kTrainingRecords && labels.size() < 32) {
    throw std::invalid_argument("invalid clone training size");
  }
  const auto started = Clock::now();
  TrainingResult result;
  Model model = initializedModel();
  result.initial_loss = datasetLoss(model, labels);
  Packed parameters = pack(model);
  Packed first_moment;
  Packed second_moment;
  std::vector<std::size_t> order(labels.size());
  std::iota(order.begin(), order.end(), 0);
  std::uint64_t step = 0;
  for (int epoch = 0; epoch < kEpochs; ++epoch) {
    deterministicShuffle(order,
                         0xada4'0000u + static_cast<std::uint32_t>(epoch));
    for (std::size_t begin = 0; begin < labels.size(); begin += kBatchSize) {
      const std::size_t end = std::min(labels.size(), begin + kBatchSize);
      model = unpack(parameters);
      Gradient gradient;
      for (std::size_t offset = begin; offset < end; ++offset) {
        accumulateGradient(model, labels[order[offset]], gradient);
      }
      Packed packed_gradient = pack(gradient);
      const double inverse_batch = 1.0 / static_cast<double>(end - begin);
      ++step;
      const double first_correction =
          1.0 - std::pow(0.9, static_cast<double>(step));
      const double second_correction =
          1.0 - std::pow(0.999, static_cast<double>(step));
      for (std::size_t parameter = 0; parameter < parameters.values.size();
           ++parameter) {
        double value = packed_gradient.values[parameter] * inverse_batch;
        const bool bias =
            (parameter >= kInputCount * kHidden &&
             parameter < kInputCount * kHidden + kHidden) ||
            parameter >= kParameterCount - kBoardSize;
        if (!bias) value += kL2 * parameters.values[parameter];
        first_moment.values[parameter] =
            0.9 * first_moment.values[parameter] + 0.1 * value;
        second_moment.values[parameter] =
            0.999 * second_moment.values[parameter] + 0.001 * value * value;
        const double corrected_first =
            first_moment.values[parameter] / first_correction;
        const double corrected_second =
            second_moment.values[parameter] / second_correction;
        parameters.values[parameter] -=
            kLearningRate * corrected_first /
            (std::sqrt(corrected_second) + 1.0e-8);
      }
    }
  }
  result.model = unpack(parameters);
  result.final_loss = datasetLoss(result.model, labels);
  result.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

std::uint64_t fingerprint(const Model& model) {
  const Packed packed = pack(model);
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const double value : packed.values) {
    std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    for (int byte = 0; byte < 8; ++byte) {
      hash ^= bits & 0xffu;
      hash *= 0x0000'0100'0000'01b3ull;
      bits >>= 8;
    }
  }
  return hash;
}

struct CheckpointHeader {
  std::array<char, 8> magic{};
  std::uint32_t input_count = 0;
  std::uint32_t hidden = 0;
  std::uint32_t outputs = 0;
  std::uint32_t parameter_count = 0;
  std::uint64_t fingerprint = 0;
};

void writeCheckpoint(const std::string& path, const Model& model) {
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("could not write clone checkpoint");
  CheckpointHeader header;
  header.magic = kCheckpointMagic;
  header.input_count = kInputCount;
  header.hidden = kHidden;
  header.outputs = kBoardSize;
  header.parameter_count = kParameterCount;
  header.fingerprint = fingerprint(model);
  const Packed packed = pack(model);
  output.write(reinterpret_cast<const char*>(&header), sizeof(header));
  output.write(reinterpret_cast<const char*>(packed.values.data()),
               static_cast<std::streamsize>(sizeof(packed.values)));
  if (!output) throw std::runtime_error("clone checkpoint write failed");
}

Model readCheckpoint(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not read clone checkpoint");
  CheckpointHeader header;
  Packed packed;
  input.read(reinterpret_cast<char*>(&header), sizeof(header));
  input.read(reinterpret_cast<char*>(packed.values.data()),
             static_cast<std::streamsize>(sizeof(packed.values)));
  const bool payload_ok = static_cast<bool>(input);
  char trailing = 0;
  const bool has_trailing = static_cast<bool>(input.read(&trailing, 1));
  if (!payload_ok || !input.eof() || has_trailing ||
      header.magic != kCheckpointMagic ||
      header.input_count != kInputCount || header.hidden != kHidden ||
      header.outputs != kBoardSize ||
      header.parameter_count != kParameterCount) {
    throw std::runtime_error("clone checkpoint header/size mismatch");
  }
  const Model model = unpack(packed);
  if (fingerprint(model) != header.fingerprint) {
    throw std::runtime_error("clone checkpoint fingerprint mismatch");
  }
  return model;
}

bool qTied(double first, double second) {
  return std::abs(first - second) <=
         kTieTolerance * (1.0 + std::max(std::abs(first), std::abs(second)));
}

int cloneAction(const Model& model, const Board& board,
                std::uint8_t next_disc, int moves_remaining) {
  const auto scores = forward(model, board, next_disc, moves_remaining).score;
  int result = -1;
  double best = -std::numeric_limits<double>::infinity();
  for (const int action : kActionOrder) {
    if (!isLegal(board, action)) continue;
    if (result < 0 || scores[action] > best) {
      result = action;
      best = scores[action];
    }
  }
  return result;
}

int centerAction(const RootLabel& label) {
  for (const int action : kActionOrder) {
    if (label.legal[action]) return action;
  }
  return -1;
}

int onePlyAction(const RootLabel& label) {
  State state;
  state.board = label.board;
  state.next_disc = label.next_disc;
  state.moves_remaining = label.moves_remaining;
  state.score = 0;
  state.level = 1;
  state.moves_played = 0;
  state.game_over = false;
  fair::SearchContext context;
  const fair::RootEvaluation root = fair::rootDecision(state, 1, context);
  if (root.action < 0 || !label.legal[root.action]) {
    throw std::runtime_error("one-ply baseline chose an illegal action");
  }
  return root.action;
}

double normalizedRegret(const RootLabel& label, int action) {
  if (action < 0 || action >= kBoardSize || !label.legal[action]) return 1.0;
  const NormalizedRoot root = normalizeRoot(label);
  if (root.range <= 1.0e-9) return 0.0;
  return (root.maximum - label.q[action]) / root.range;
}

struct RankingAccumulator {
  int examples = 0;
  int top1_with_ties = 0;
  int top2_contains_optimal = 0;
  std::uint64_t pairwise_pairs = 0;
  double pairwise_credit = 0.0;
  double normalized_regret = 0.0;
  double center_regret = 0.0;
  double one_ply_regret = 0.0;
  double maximum_reflection_gap = 0.0;
};

void observeRanking(const Model& model, const RootLabel& label,
                    bool include_baselines, RankingAccumulator& result) {
  const EquivariantForward prediction =
      forward(model, label.board, label.next_disc, label.moves_remaining);
  int selected = -1;
  std::vector<int> ranked;
  for (const int action : kActionOrder) {
    if (label.legal[action]) ranked.push_back(action);
  }
  std::stable_sort(ranked.begin(), ranked.end(), [&](int left, int right) {
    return prediction.score[left] > prediction.score[right];
  });
  if (!ranked.empty()) selected = ranked.front();
  const NormalizedRoot normalized = normalizeRoot(label);
  ++result.examples;
  result.top1_with_ties +=
      selected >= 0 && qTied(label.q[selected], normalized.maximum);
  bool top2 = false;
  for (std::size_t index = 0; index < std::min<std::size_t>(2, ranked.size());
       ++index) {
    top2 = top2 || qTied(label.q[ranked[index]], normalized.maximum);
  }
  result.top2_contains_optimal += top2;
  result.normalized_regret += normalizedRegret(label, selected);
  if (include_baselines) {
    result.center_regret += normalizedRegret(label, centerAction(label));
    result.one_ply_regret += normalizedRegret(label, onePlyAction(label));
  }
  for (int first = 0; first < kBoardSize; ++first) {
    if (!label.legal[first]) continue;
    for (int second = first + 1; second < kBoardSize; ++second) {
      if (!label.legal[second] || qTied(label.q[first], label.q[second])) continue;
      const bool label_first = label.q[first] > label.q[second];
      const double predicted_difference =
          prediction.score[first] - prediction.score[second];
      if (std::abs(predicted_difference) <= kTieTolerance) {
        result.pairwise_credit += 0.5;
      } else {
        result.pairwise_credit +=
            (predicted_difference > 0.0) == label_first ? 1.0 : 0.0;
      }
      ++result.pairwise_pairs;
    }
  }
  const Board reflected_board = cfpi::detail::mirrorBoard(label.board);
  const auto reflected = forward(model, reflected_board, label.next_disc,
                                 label.moves_remaining)
                             .score;
  for (int action = 0; action < kBoardSize; ++action) {
    result.maximum_reflection_gap =
        std::max(result.maximum_reflection_gap,
                 std::abs(prediction.score[action] -
                          reflected[kBoardSize - 1 - action]));
  }
}

struct RankingMetrics {
  int examples = 0;
  double top1_with_ties = 0.0;
  double top2_contains_optimal = 0.0;
  std::uint64_t pairwise_pairs = 0;
  double pairwise_accuracy = 0.0;
  double normalized_regret = 0.0;
  double center_regret = 0.0;
  double one_ply_regret = 0.0;
  double maximum_reflection_gap = 0.0;
};

RankingMetrics finalize(const RankingAccumulator& source,
                        bool include_baselines) {
  if (source.examples <= 0 || source.pairwise_pairs == 0) {
    throw std::invalid_argument("empty clone ranking metrics");
  }
  RankingMetrics result;
  result.examples = source.examples;
  result.top1_with_ties =
      static_cast<double>(source.top1_with_ties) / source.examples;
  result.top2_contains_optimal =
      static_cast<double>(source.top2_contains_optimal) / source.examples;
  result.pairwise_pairs = source.pairwise_pairs;
  result.pairwise_accuracy =
      source.pairwise_credit / source.pairwise_pairs;
  result.normalized_regret = source.normalized_regret / source.examples;
  if (include_baselines) {
    result.center_regret = source.center_regret / source.examples;
    result.one_ply_regret = source.one_ply_regret / source.examples;
  }
  result.maximum_reflection_gap = source.maximum_reflection_gap;
  return result;
}

RankingMetrics evaluate(const Model& model,
                        const std::vector<RootLabel>& labels,
                        int game_begin, int game_end,
                        bool include_baselines, int move_begin = 0,
                        int move_end = std::numeric_limits<int>::max()) {
  RankingAccumulator accumulator;
  for (const RootLabel& label : labels) {
    if (label.game < game_begin || label.game >= game_end ||
        label.move_in_game < move_begin || label.move_in_game >= move_end) {
      continue;
    }
    observeRanking(model, label, include_baselines, accumulator);
  }
  return finalize(accumulator, include_baselines);
}

struct Evaluation {
  RankingMetrics all;
  RankingMetrics first_half;
  RankingMetrics second_half;
  RankingMetrics early_thirty;
  RankingMetrics late;
};

Evaluation evaluateHeldout(const Model& model,
                           const std::vector<RootLabel>& heldout) {
  return {evaluate(model, heldout, 0, kHeldoutGames, true),
          evaluate(model, heldout, 0, kHeldoutGames / 2, false),
          evaluate(model, heldout, kHeldoutGames / 2, kHeldoutGames, false),
          evaluate(model, heldout, 0, kHeldoutGames, false, 0, 30),
          evaluate(model, heldout, 0, kHeldoutGames, false, 30)};
}

bool labelGate(const Evaluation& evaluation) {
  const RankingMetrics& all = evaluation.all;
  return all.examples == kHeldoutRecords &&
         all.top1_with_ties >= kMinimumTop1WithTies &&
         all.top2_contains_optimal >= kMinimumTop2 &&
         all.pairwise_accuracy >= kMinimumPairwiseAccuracy &&
         evaluation.first_half.pairwise_accuracy >=
             kMinimumHalfPairwiseAccuracy &&
         evaluation.second_half.pairwise_accuracy >=
             kMinimumHalfPairwiseAccuracy &&
         all.normalized_regret <=
             kMaximumCenterRegretRatio * all.center_regret &&
         all.normalized_regret <=
             kMaximumOnePlyRegretRatio * all.one_ply_regret &&
         all.maximum_reflection_gap <= 1.0e-12;
}

struct Throughput {
  int evaluations = 0;
  double seconds = 0.0;
  double evaluations_per_second = 0.0;
  double checksum = 0.0;
};

Throughput benchmarkInference(const Model& model,
                              const std::vector<RootLabel>& labels) {
  constexpr int evaluations = 250'000;
  const auto started = Clock::now();
  double checksum = 0.0;
  for (int index = 0; index < evaluations; ++index) {
    const RootLabel& label = labels[static_cast<std::size_t>(index) % labels.size()];
    const auto scores =
        forward(model, label.board, label.next_disc, label.moves_remaining)
            .score;
    checksum += scores[index % kBoardSize] * 1.0e-9;
  }
  const double seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return {evaluations, seconds, evaluations / std::max(1.0e-12, seconds),
          checksum};
}

enum class Policy { kFairD4, kClone };

struct GameResult {
  std::uint32_t seed = 0;
  Policy policy = Policy::kFairD4;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t cleared = 0;
  std::uint64_t revealed = 0;
  int maximum_chain = 0;
  std::uint64_t work = 0;
  std::size_t peak_cache_entries = 0;
  double elapsed_seconds = 0.0;
};

void observeMove(const MoveResult& move, GameResult& result) {
  result.maximum_chain =
      std::max(result.maximum_chain, static_cast<int>(move.waves.size()));
  for (const Wave& wave : move.waves) {
    result.cleared += wave.cleared;
    result.revealed += wave.revealed;
  }
}

void reportGame(const GameResult& result) {
  const std::lock_guard<std::mutex> lock(progress_mutex);
  std::cerr << "d4-q-clone "
            << (result.policy == Policy::kFairD4 ? "fair-d4" : "clone")
            << " seed 0x" << std::hex << result.seed << std::dec << ' '
            << result.score << " (" << result.moves << " moves"
            << (result.censored ? ", capped" : "") << ")\n";
}

GameResult runFairGame(std::uint32_t seed) {
  const auto started = Clock::now();
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  result.policy = Policy::kFairD4;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    const fair::SearchDecision decision = fair::chooseDepth4Action(state);
    if (!decision.complete || decision.completed_depth != 4 ||
        !isLegal(state.board, decision.action)) {
      throw std::runtime_error("rollout fair D4 decision failed");
    }
    result.work += decision.work;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, decision.cache_entries);
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("rollout fair D4 transition failed");
    }
    observeMove(move, result);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.elapsed_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  reportGame(result);
  return result;
}

GameResult runCloneGame(std::uint32_t seed, const Model& model) {
  const auto started = Clock::now();
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  result.policy = Policy::kClone;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    const int action = cloneAction(model, state.board, state.next_disc,
                                   state.moves_remaining);
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("rollout clone chose an illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("rollout clone transition failed");
    }
    observeMove(move, result);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.elapsed_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  reportGame(result);
  return result;
}

struct Cohort {
  std::vector<GameResult> fair;
  std::vector<GameResult> clone;
  double wall_seconds = 0.0;
};

Cohort runCohort(const Model& model) {
  const auto started = Clock::now();
  Cohort result;
  result.fair.resize(kRolloutGames);
  result.clone.resize(kRolloutGames);
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < kParallelism; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= kRolloutGames) return;
        const std::uint32_t seed =
            kRolloutSeedStart + static_cast<std::uint32_t>(game);
        result.fair[game] = runFairGame(seed);
        result.clone[game] = runCloneGame(seed, model);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

struct PolicySummary {
  int games = 0;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  int censored = 0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double mean_maximum_chain = 0.0;
  double moves_per_second = 0.0;
  double work_per_move = 0.0;
  std::size_t peak_cache_entries = 0;
};

PolicySummary summarize(const std::vector<GameResult>& games) {
  if (games.empty()) throw std::invalid_argument("empty clone rollout");
  PolicySummary result;
  result.games = static_cast<int>(games.size());
  std::uint64_t moves = 0;
  std::uint64_t cleared = 0;
  std::uint64_t revealed = 0;
  std::uint64_t work = 0;
  double seconds = 0.0;
  for (const GameResult& game : games) {
    result.mean_score += static_cast<double>(game.score) / games.size();
    result.mean_moves += static_cast<double>(game.moves) / games.size();
    result.censored += game.censored;
    result.mean_maximum_chain +=
        static_cast<double>(game.maximum_chain) / games.size();
    moves += game.moves;
    cleared += game.cleared;
    revealed += game.revealed;
    work += game.work;
    seconds += game.elapsed_seconds;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, game.peak_cache_entries);
  }
  const double move_count = static_cast<double>(std::max<std::uint64_t>(1, moves));
  result.clears_per_move = cleared / move_count;
  result.reveals_per_move = revealed / move_count;
  result.moves_per_second = move_count / std::max(1.0e-12, seconds);
  result.work_per_move = work / move_count;
  return result;
}

struct Difference {
  double mean = 0.0;
  double lower_95 = 0.0;
  int wins = 0;
  int ties = 0;
  int losses = 0;
};

Difference difference(const std::vector<double>& values) {
  if (values.empty()) throw std::invalid_argument("empty clone differences");
  Difference result;
  result.mean = std::accumulate(values.begin(), values.end(), 0.0) /
                values.size();
  double squared = 0.0;
  for (const double value : values) {
    squared += (value - result.mean) * (value - result.mean);
    result.wins += value > 0.0;
    result.ties += value == 0.0;
    result.losses += value < 0.0;
  }
  const double deviation = values.size() > 1
                               ? std::sqrt(squared / (values.size() - 1))
                               : 0.0;
  result.lower_95 =
      result.mean - 1.96 * deviation / std::sqrt(values.size());
  return result;
}

struct PairedSummary {
  Difference score;
  Difference moves;
};

PairedSummary pairedSummary(const Cohort& cohort) {
  if (cohort.fair.size() != cohort.clone.size() || cohort.fair.empty()) {
    throw std::invalid_argument("invalid clone paired cohort");
  }
  std::vector<double> scores;
  std::vector<double> moves;
  for (std::size_t game = 0; game < cohort.fair.size(); ++game) {
    scores.push_back(static_cast<double>(cohort.clone[game].score -
                                         cohort.fair[game].score));
    moves.push_back(static_cast<double>(cohort.clone[game].moves -
                                        cohort.fair[game].moves));
  }
  return {difference(scores), difference(moves)};
}

void writeRanking(std::ostream& output, const RankingMetrics& value) {
  output << "{\"examples\":" << value.examples
         << ",\"top1WithTies\":" << value.top1_with_ties
         << ",\"top2ContainsOptimal\":" << value.top2_contains_optimal
         << ",\"pairwisePairs\":" << value.pairwise_pairs
         << ",\"pairwiseAccuracy\":" << value.pairwise_accuracy
         << ",\"normalizedRegret\":" << value.normalized_regret
         << ",\"centerRegret\":" << value.center_regret
         << ",\"onePlyRegret\":" << value.one_ply_regret
         << ",\"maximumReflectionGap\":"
         << value.maximum_reflection_gap << '}';
}

void writeEvaluation(std::ostream& output, const Evaluation& value) {
  output << "{\"all\":";
  writeRanking(output, value.all);
  output << ",\"firstFourGames\":";
  writeRanking(output, value.first_half);
  output << ",\"secondFourGames\":";
  writeRanking(output, value.second_half);
  output << ",\"moves0To29\":";
  writeRanking(output, value.early_thirty);
  output << ",\"moves30Plus\":";
  writeRanking(output, value.late);
  output << '}';
}

struct CompoundingProxy {
  int horizon = 30;
  double expected_top1_errors = 0.0;
  double all_top1_correct_independence = 0.0;
  double expected_pairwise_errors = 0.0;
  double all_pairwise_correct_independence = 0.0;
};

CompoundingProxy compoundingProxy(const RankingMetrics& metrics) {
  CompoundingProxy result;
  result.expected_top1_errors =
      result.horizon * (1.0 - metrics.top1_with_ties);
  result.all_top1_correct_independence =
      std::pow(metrics.top1_with_ties, result.horizon);
  result.expected_pairwise_errors =
      result.horizon * (1.0 - metrics.pairwise_accuracy);
  result.all_pairwise_correct_independence =
      std::pow(metrics.pairwise_accuracy, result.horizon);
  return result;
}

void writeCompounding(std::ostream& output, const CompoundingProxy& value) {
  output << "{\"horizon\":" << value.horizon
         << ",\"expectedTop1Errors\":" << value.expected_top1_errors
         << ",\"allTop1CorrectIndependenceProxy\":"
         << value.all_top1_correct_independence
         << ",\"expectedPairwiseErrors\":"
         << value.expected_pairwise_errors
         << ",\"allPairwiseCorrectIndependenceProxy\":"
         << value.all_pairwise_correct_independence << '}';
}

void writePolicySummary(std::ostream& output, const PolicySummary& value) {
  output << "{\"games\":" << value.games << ",\"meanScore\":"
         << value.mean_score << ",\"meanMoves\":" << value.mean_moves
         << ",\"censored\":" << value.censored
         << ",\"clearsPerMove\":" << value.clears_per_move
         << ",\"revealsPerMove\":" << value.reveals_per_move
         << ",\"meanMaximumChain\":" << value.mean_maximum_chain
         << ",\"movesPerSecond\":" << value.moves_per_second
         << ",\"workPerMove\":" << value.work_per_move
         << ",\"peakCacheEntries\":" << value.peak_cache_entries << '}';
}

void writeDifference(std::ostream& output, const Difference& value) {
  output << "{\"mean\":" << value.mean << ",\"lower95\":"
         << value.lower_95 << ",\"wins\":" << value.wins
         << ",\"ties\":" << value.ties << ",\"losses\":"
         << value.losses << '}';
}

void writeGame(std::ostream& output, const GameResult& game) {
  output << "{\"seed\":" << game.seed << ",\"score\":" << game.score
         << ",\"moves\":" << game.moves << ",\"censored\":"
         << (game.censored ? "true" : "false") << ",\"cleared\":"
         << game.cleared << ",\"revealed\":" << game.revealed
         << ",\"maximumChain\":" << game.maximum_chain
         << ",\"work\":" << game.work << ",\"peakCacheEntries\":"
         << game.peak_cache_entries << ",\"elapsedSeconds\":"
         << game.elapsed_seconds << '}';
}

void writeCohort(std::ostream& output, const Cohort& cohort,
                 const PolicySummary& fair_summary,
                 const PolicySummary& clone_summary,
                 const PairedSummary& paired) {
  output << "{\"seedStart\":" << kRolloutSeedStart
         << ",\"games\":" << kRolloutGames
         << ",\"maximumMoves\":" << kMaximumMoves << ",\"fairD4\":";
  writePolicySummary(output, fair_summary);
  output << ",\"clone\":";
  writePolicySummary(output, clone_summary);
  output << ",\"paired\":{\"score\":";
  writeDifference(output, paired.score);
  output << ",\"moves\":";
  writeDifference(output, paired.moves);
  output << "},\"wallSeconds\":" << cohort.wall_seconds << ",\"pairs\":[";
  for (std::size_t game = 0; game < cohort.fair.size(); ++game) {
    if (game != 0) output << ',';
    output << "{\"seed\":" << cohort.fair[game].seed << ",\"fairD4\":";
    writeGame(output, cohort.fair[game]);
    output << ",\"clone\":";
    writeGame(output, cohort.clone[game]);
    output << '}';
  }
  output << "]}";
}

std::uint64_t fileBytes(const std::string& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) throw std::runtime_error("could not size clone checkpoint");
  const std::streampos size = input.tellg();
  if (size < 0) throw std::runtime_error("invalid clone checkpoint size");
  return static_cast<std::uint64_t>(size);
}

struct Options {
  std::string labels = "/tmp/drop7-d4-public-root-labels.jsonl";
  std::string checkpoint = "/tmp/drop7-d4-q-clone.bin";
  std::string output = "/tmp/drop7-d4-q-clone.json";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing clone option value");
    const std::string flag = argv[index];
    if (flag == "--labels") {
      result.labels = argv[index + 1];
    } else if (flag == "--checkpoint") {
      result.checkpoint = argv[index + 1];
    } else if (flag == "--output") {
      result.output = argv[index + 1];
    } else {
      throw std::invalid_argument("unknown clone option " + flag);
    }
  }
  return result;
}

bool selfTest(const Options& options, std::ostream& output) {
  const bool fair_passed = fair::selfTest(output);
  const Board board = initialBoard();
  const std::string encoded = serializeBoard(board);
  std::string line =
      "{\"split\":\"training\",\"board\":\"" + encoded +
      "\",\"nextDisc\":3,\"movesRemaining\":5,\"action\":3,"
      "\"rootQ\":[0,1,2,3,2,1,0]}";
  RootLabel fixture = parseLabel(line, "training", 0);
  std::vector<RootLabel> tiny;
  tiny.reserve(32);
  for (int index = 0; index < 32; ++index) {
    RootLabel sample = fixture;
    sample.next_disc = static_cast<std::uint8_t>(index % kBoardSize + 1);
    sample.moves_remaining = index % kMovesPerLevel + 1;
    const int optimal = index % kBoardSize;
    for (int action = 0; action < kBoardSize; ++action) {
      sample.q[action] = -std::abs(action - optimal);
    }
    sample.labeled_action = optimal;
    tiny.push_back(sample);
  }
  const TrainingResult first = train(tiny);
  const TrainingResult repeat = train(tiny);
  const bool deterministic =
      fingerprint(first.model) == fingerprint(repeat.model);
  const bool learned = std::isfinite(first.initial_loss) &&
                       std::isfinite(first.final_loss) &&
                       first.final_loss < first.initial_loss;

  Gradient analytic;
  const Objective objective = accumulateGradient(first.model, fixture, analytic);
  constexpr int tested_action = 0;
  constexpr double epsilon = 1.0e-5;
  Model plus = first.model;
  Model minus = first.model;
  plus.output_bias[tested_action] += epsilon;
  minus.output_bias[tested_action] -= epsilon;
  const double numeric =
      (rootObjective(fixture,
                     forward(plus, fixture.board, fixture.next_disc,
                             fixture.moves_remaining)
                         .score)
           .loss -
       rootObjective(fixture,
                     forward(minus, fixture.board, fixture.next_disc,
                             fixture.moves_remaining)
                         .score)
           .loss) /
      (2.0 * epsilon);
  const double gradient_error =
      std::abs(numeric - analytic.output_bias[tested_action]);
  const bool gradient_ok = std::isfinite(objective.loss) &&
                           gradient_error < 2.0e-5;

  const std::string checkpoint = options.checkpoint + ".self-test";
  writeCheckpoint(checkpoint, first.model);
  const Model restored = readCheckpoint(checkpoint);
  const bool checkpoint_ok =
      fingerprint(first.model) == fingerprint(restored);
  const auto direct = forward(restored, fixture.board, fixture.next_disc,
                              fixture.moves_remaining)
                          .score;
  const auto reflected =
      forward(restored, cfpi::detail::mirrorBoard(fixture.board),
              fixture.next_disc, fixture.moves_remaining)
          .score;
  bool reflection_ok = true;
  for (int action = 0; action < kBoardSize; ++action) {
    reflection_ok = reflection_ok &&
                    direct[action] == reflected[kBoardSize - 1 - action];
  }
  RootLabel metadata = fixture;
  metadata.game = 999;
  metadata.move_in_game = 999;
  const auto metadata_scores =
      forward(restored, metadata.board, metadata.next_disc,
              metadata.moves_remaining)
          .score;
  const bool metadata_blind = direct == metadata_scores;
  const int action = cloneAction(restored, fixture.board, fixture.next_disc,
                                 fixture.moves_remaining);
  const bool legal = isLegal(fixture.board, action);
  const bool masks_and_ties = qTied(fixture.q[1], fixture.q[5]) &&
                              normalizeRoot(fixture).legal_count == kBoardSize;
  const bool resources = sizeof(Model) ==
                             static_cast<std::size_t>(kParameterCount) *
                                 sizeof(double) &&
                         fileBytes(checkpoint) < 100'000 &&
                         kParameterCount < 16'384;
  const bool ranges = kRolloutSeedStart == 0x3de3'0000u &&
                      kRolloutSeedStart + kRolloutGames == 0x3de3'0020u;
  const bool passed = fair_passed && deterministic && learned && gradient_ok &&
                      checkpoint_ok && reflection_ok && metadata_blind && legal &&
                      masks_and_ties && resources && ranges;
  output << "D4_Q_CLONE_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"fairPassed\":" << (fair_passed ? "true" : "false")
         << ",\"deterministic\":" << (deterministic ? "true" : "false")
         << ",\"learned\":" << (learned ? "true" : "false")
         << ",\"gradientError\":" << gradient_error
         << ",\"gradientOk\":" << (gradient_ok ? "true" : "false")
         << ",\"checkpoint\":" << (checkpoint_ok ? "true" : "false")
         << ",\"reflection\":" << (reflection_ok ? "true" : "false")
         << ",\"metadataBlind\":" << (metadata_blind ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false")
         << ",\"masksAndTies\":" << (masks_and_ties ? "true" : "false")
         << ",\"resources\":" << (resources ? "true" : "false")
         << ",\"ranges\":" << (ranges ? "true" : "false")
         << ",\"parameters\":" << kParameterCount << "}\n";
  return passed;
}

void writeArtifact(const Options& options, const TrainingResult& training,
                   const RankingMetrics& training_metrics,
                   const Evaluation& heldout, const Throughput& throughput,
                   bool gate_passed, std::uint64_t model_fingerprint,
                   std::uint64_t checkpoint_bytes, double total_wall_seconds) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not write clone artifact");
  const CompoundingProxy all_compounding = compoundingProxy(heldout.all);
  const CompoundingProxy early_compounding =
      compoundingProxy(heldout.early_thirty);
  const CompoundingProxy late_compounding = compoundingProxy(heldout.late);
  output << std::setprecision(10)
         << "{\n  \"experiment\":\"fair-d4-root-q-behavior-clone\",\n"
         << "  \"purpose\":\"fast policy compression for later observable rollouts; not a stronger-policy claim\",\n"
         << "  \"source\":{\"path\":\"" << options.labels
         << "\",\"sha256\":\"f61801abc9eefe86011f7202620a18c1277fcc1b5a24f4bce5947033b791dd89\","
            "\"trainingRecords\":"
         << kTrainingRecords << ",\"trainingGames\":" << kTrainingGames
         << ",\"heldoutRecords\":" << kHeldoutRecords
         << ",\"heldoutGames\":" << kHeldoutGames
         << ",\"heldoutOpenedOnceAfterCheckpointFreeze\":true},\n"
         << "  \"inputBoundary\":{\"included\":[\"board\",\"nextDisc\",\"movesRemaining\"],"
            "\"excluded\":[\"gameSeed\",\"score\",\"level\",\"moveIndex\",\"gameIndex\",\"history\",\"futureTape\"],"
            "\"reflection\":\"exact two-pass output symmetrization\"},\n"
         << "  \"targets\":{\"kind\":\"within-root normalized Q ranking\","
            "\"absoluteGlobalQUsed\":false,\"legalMasksPreserved\":true,"
            "\"tiesPreserved\":true,\"listwiseTemperature\":"
         << kTargetTemperature << ",\"pairwiseLossWeight\":"
         << kPairwiseWeight << "},\n"
         << "  \"architecture\":{\"inputCount\":" << kInputCount
         << ",\"activeInputsPerState\":" << kActiveInputs
         << ",\"hiddenRelu\":" << kHidden << ",\"outputs\":"
         << kBoardSize << ",\"parameterCount\":" << kParameterCount
         << ",\"parameterBytes\":"
         << static_cast<std::uint64_t>(sizeof(Model))
         << ",\"checkpointBytes\":" << checkpoint_bytes
         << ",\"checkpoint\":\"" << options.checkpoint
         << "\",\"fingerprintFnv1a64\":\"0x" << std::hex
         << model_fingerprint << std::dec << "\"},\n"
         << "  \"training\":{\"epochs\":" << kEpochs
         << ",\"batchSize\":" << kBatchSize
         << ",\"learningRate\":" << kLearningRate << ",\"l2\":"
         << kL2 << ",\"initialLoss\":" << training.initial_loss
         << ",\"finalLoss\":" << training.final_loss
         << ",\"wallSeconds\":" << training.wall_seconds
         << ",\"ranking\":";
  writeRanking(output, training_metrics);
  output << "},\n  \"heldout\":";
  writeEvaluation(output, heldout);
  output << ",\n  \"compoundingErrorProxy\":{"
            "\"warning\":\"independence proxy from per-root labels, not a stochastic rollout\","
            "\"all\":";
  writeCompounding(output, all_compounding);
  output << ",\"moves0To29\":";
  writeCompounding(output, early_compounding);
  output << ",\"moves30Plus\":";
  writeCompounding(output, late_compounding);
  output << "},\n  \"gate\":{\"minimumTop1WithTies\":"
         << kMinimumTop1WithTies << ",\"minimumTop2\":" << kMinimumTop2
         << ",\"minimumPairwiseAccuracy\":"
         << kMinimumPairwiseAccuracy
         << ",\"minimumHalfPairwiseAccuracy\":"
         << kMinimumHalfPairwiseAccuracy
         << ",\"maximumCenterRegretRatio\":"
         << kMaximumCenterRegretRatio
         << ",\"maximumOnePlyRegretRatio\":"
         << kMaximumOnePlyRegretRatio << ",\"passed\":"
         << (gate_passed ? "true" : "false") << "},\n"
         << "  \"inference\":{\"evaluations\":"
         << throughput.evaluations << ",\"seconds\":" << throughput.seconds
         << ",\"evaluationsPerSecond\":"
         << throughput.evaluations_per_second << ",\"checksum\":"
         << throughput.checksum << "},\n"
         << "  \"freshGameplay\":{\"ran\":false,"
            "\"reason\":\"superseding instruction: report label-band compounding and do not open fresh gameplay ranges\","
            "\"reservedTrainingSeedStart\":"
         << kRolloutSeedStart << ",\"seedsRead\":0},\n"
         << "  \"totalWallSeconds\":" << total_wall_seconds
         << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
}

int run(const Options& options, std::ostream& output) {
  const auto started = Clock::now();
  // This first pass reads exactly the header and training records, stopping
  // before the first heldout line.
  const std::vector<RootLabel> training_labels = loadTraining(options.labels);
  const TrainingResult training = train(training_labels);
  writeCheckpoint(options.checkpoint, training.model);
  const Model frozen = readCheckpoint(options.checkpoint);
  const std::uint64_t model_fingerprint = fingerprint(frozen);
  if (model_fingerprint != fingerprint(training.model)) {
    throw std::runtime_error("frozen clone checkpoint changed model weights");
  }
  const RankingMetrics training_metrics =
      evaluate(frozen, training_labels, 0, kTrainingGames, false);

  // This is the one heldout read, after architecture, hyperparameters,
  // weights, and checkpoint have all been locked.
  const std::vector<RootLabel> heldout_labels =
      loadHeldoutOnce(options.labels);
  const Evaluation heldout = evaluateHeldout(frozen, heldout_labels);
  const bool gate_passed = labelGate(heldout);
  const Throughput throughput = benchmarkInference(frozen, heldout_labels);
  const std::uint64_t checkpoint_bytes = fileBytes(options.checkpoint);
  const double total_wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  writeArtifact(options, training, training_metrics, heldout, throughput,
                gate_passed, model_fingerprint, checkpoint_bytes,
                total_wall_seconds);
  output << std::fixed << std::setprecision(4)
         << "D4_Q_CLONE_RESULT {\"heldoutTop1WithTies\":"
         << heldout.all.top1_with_ties << ",\"heldoutTop2\":"
         << heldout.all.top2_contains_optimal
         << ",\"heldoutPairwise\":" << heldout.all.pairwise_accuracy
         << ",\"firstHalfPairwise\":"
         << heldout.first_half.pairwise_accuracy
         << ",\"secondHalfPairwise\":"
         << heldout.second_half.pairwise_accuracy
         << ",\"normalizedRegret\":" << heldout.all.normalized_regret
         << ",\"centerRegret\":" << heldout.all.center_regret
         << ",\"onePlyRegret\":" << heldout.all.one_ply_regret
         << ",\"earlyTop1\":" << heldout.early_thirty.top1_with_ties
         << ",\"lateTop1\":" << heldout.late.top1_with_ties
         << ",\"earlyPairwise\":"
         << heldout.early_thirty.pairwise_accuracy
         << ",\"latePairwise\":" << heldout.late.pairwise_accuracy
         << ",\"gatePassed\":" << (gate_passed ? "true" : "false")
         << ",\"evaluationsPerSecond\":"
         << throughput.evaluations_per_second
         << ",\"checkpointBytes\":" << checkpoint_bytes
         << ",\"fingerprint\":\"0x" << std::hex << model_fingerprint
         << std::dec << "\",\"freshGameplayRan\":false,"
            "\"totalWallSeconds\":"
         << total_wall_seconds << ",\"artifact\":\"" << options.output
         << "\",\"checkpoint\":\"" << options.checkpoint << "\"}\n";
  return 0;
}

}  // namespace drop7::d4_q_clone

int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      const auto options = drop7::d4_q_clone::parseOptions(argc, argv, 2);
      return drop7::d4_q_clone::selfTest(options, std::cout) ? EXIT_SUCCESS
                                                             : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options = drop7::d4_q_clone::parseOptions(argc, argv, 2);
      return drop7::d4_q_clone::run(options, std::cout);
    }
    std::cerr << "usage: drop7_d4_q_clone --self-test | --run "
                 "[--labels PATH] [--checkpoint PATH] [--output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_d4_q_clone: " << error.what() << '\n';
    return 1;
  }
}
