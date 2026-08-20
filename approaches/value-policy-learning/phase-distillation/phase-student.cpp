#include "../../../src/core/native/public-behavior.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Distills the reference phase-D3/s5 behavior into a standalone policy.
// Environment trajectories are restricted to the training partition.
namespace drop7::phase_student {

constexpr std::uint32_t kTrainingStart = 0x3d70'0000u;
constexpr std::uint32_t kTrainingEnd = 0x4d00'0000u;
constexpr int kTokens = 10;
constexpr int kCellInputs = kCellCount * kTokens;
constexpr int kDiscInputs = kBoardSize;
constexpr int kPhaseInputs = kMovesPerLevel;
constexpr int kColumnHeightInputs = kBoardSize * (kBoardSize + 1);
constexpr int kRowCountInputs = kBoardSize * (kBoardSize + 1);
constexpr int kInputCount = kCellInputs + kDiscInputs + kPhaseInputs +
                            kColumnHeightInputs + kRowCountInputs;
constexpr int kHidden1 = 128;
constexpr int kHidden2 = 128;
constexpr int kOutputCount = kBoardSize;

bool mirrorIsSmaller(const Board& board) {
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      const std::uint8_t forward = board[indexOf(row, column)];
      const std::uint8_t reflected =
          board[indexOf(row, kBoardSize - 1 - column)];
      if (reflected < forward) return true;
      if (reflected > forward) return false;
    }
  }
  return false;
}

Board mirrorBoard(const Board& board) {
  Board result{};
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      result[indexOf(row, column)] =
          board[indexOf(row, kBoardSize - 1 - column)];
    }
  }
  return result;
}

struct CanonicalState {
  State state{};
  bool mirrored = false;
};

CanonicalState canonicalize(const State& source) {
  CanonicalState result{source, mirrorIsSmaller(source.board)};
  if (result.mirrored) result.state.board = mirrorBoard(source.board);
  return result;
}

using Input = std::array<float, kInputCount>;

constexpr int kActiveInputs = kCellCount + 2 * kBoardSize + 2;

struct ActiveInput {
  std::array<int, kActiveInputs> indices{};
  int count = 0;
};

Input encodeCanonical(const State& state) {
  Input input{};
  for (int cell = 0; cell < kCellCount; ++cell) {
    input[cell * kTokens + state.board[cell]] = 1.0f;
  }
  int offset = kCellInputs;
  input[offset + std::clamp<int>(state.next_disc, 1, kBoardSize) - 1] = 1.0f;
  offset += kDiscInputs;
  input[offset +
        std::clamp(state.moves_remaining, 1, kMovesPerLevel) - 1] = 1.0f;
  offset += kPhaseInputs;
  for (int column = 0; column < kBoardSize; ++column) {
    int height = 0;
    for (int row = 0; row < kBoardSize; ++row) {
      height += state.board[indexOf(row, column)] != kEmpty;
    }
    input[offset + column * (kBoardSize + 1) + height] = 1.0f;
  }
  offset += kColumnHeightInputs;
  for (int row = 0; row < kBoardSize; ++row) {
    int occupied = 0;
    for (int column = 0; column < kBoardSize; ++column) {
      occupied += state.board[indexOf(row, column)] != kEmpty;
    }
    input[offset + row * (kBoardSize + 1) + occupied] = 1.0f;
  }
  return input;
}

ActiveInput encodeActiveCanonical(const State& state) {
  ActiveInput result;
  for (int cell = 0; cell < kCellCount; ++cell) {
    result.indices[result.count++] = cell * kTokens + state.board[cell];
  }
  int offset = kCellInputs;
  result.indices[result.count++] =
      offset + std::clamp<int>(state.next_disc, 1, kBoardSize) - 1;
  offset += kDiscInputs;
  result.indices[result.count++] =
      offset + std::clamp(state.moves_remaining, 1, kMovesPerLevel) - 1;
  offset += kPhaseInputs;
  for (int column = 0; column < kBoardSize; ++column) {
    int height = 0;
    for (int row = 0; row < kBoardSize; ++row) {
      height += state.board[indexOf(row, column)] != kEmpty;
    }
    result.indices[result.count++] =
        offset + column * (kBoardSize + 1) + height;
  }
  offset += kColumnHeightInputs;
  for (int row = 0; row < kBoardSize; ++row) {
    int occupied = 0;
    for (int column = 0; column < kBoardSize; ++column) {
      occupied += state.board[indexOf(row, column)] != kEmpty;
    }
    result.indices[result.count++] =
        offset + row * (kBoardSize + 1) + occupied;
  }
  if (result.count != kActiveInputs) {
    throw std::logic_error("active input count invariant failed");
  }
  return result;
}

float relu(float value) { return std::max(0.0f, value); }

struct ForwardCache {
  ActiveInput input;
  std::array<float, kHidden1> pre1{};
  std::array<float, kHidden1> hidden1{};
  std::array<float, kHidden2> pre2{};
  std::array<float, kHidden2> hidden2{};
  std::array<float, kOutputCount> logits{};
};

struct Network {
  std::vector<float> weight1 =
      std::vector<float>(kHidden1 * kInputCount);
  std::array<float, kHidden1> bias1{};
  std::array<float, kHidden2 * kHidden1> weight2{};
  std::array<float, kHidden2> bias2{};
  std::array<float, kOutputCount * kHidden2> weight3{};
  std::array<float, kOutputCount> bias3{};

  explicit Network(std::uint32_t seed = 0x3d75'5544u) { initialize(seed); }

  void initialize(std::uint32_t seed) {
    Mulberry32 random(seed);
    const auto fill = [&random](auto& values, float scale) {
      for (float& value : values) {
        value = static_cast<float>((random.nextUnit() * 2.0 - 1.0) * scale);
      }
    };
    fill(weight1, std::sqrt(6.0f / (kInputCount + kHidden1)));
    fill(weight2, std::sqrt(6.0f / (kHidden1 + kHidden2)));
    fill(weight3, std::sqrt(6.0f / (kHidden2 + kOutputCount)));
  }

  std::array<float, kOutputCount> logitsCanonical(const State& state) const {
    return forwardCanonical(state, nullptr);
  }

  std::array<float, kOutputCount> forwardCanonical(
      const State& state, ForwardCache* cache) const {
    ForwardCache local;
    ForwardCache& result = cache != nullptr ? *cache : local;
    result.input = encodeActiveCanonical(state);
    for (int hidden = 0; hidden < kHidden1; ++hidden) {
      float value = bias1[hidden];
      const int base = hidden * kInputCount;
      for (int offset = 0; offset < result.input.count; ++offset) {
        value += weight1[base + result.input.indices[offset]];
      }
      result.pre1[hidden] = value;
      result.hidden1[hidden] = relu(value);
    }
    for (int hidden = 0; hidden < kHidden2; ++hidden) {
      float value = bias2[hidden];
      const int base = hidden * kHidden1;
      for (int prior = 0; prior < kHidden1; ++prior) {
        value += weight2[base + prior] * result.hidden1[prior];
      }
      result.pre2[hidden] = value;
      result.hidden2[hidden] = relu(value);
    }
    result.logits = bias3;
    for (int output = 0; output < kOutputCount; ++output) {
      const int base = output * kHidden2;
      for (int hidden = 0; hidden < kHidden2; ++hidden) {
        result.logits[output] +=
            weight3[base + hidden] * result.hidden2[hidden];
      }
    }
    return result.logits;
  }

  int chooseAction(const State& source) const {
    const CanonicalState canonical = canonicalize(source);
    const auto logits = logitsCanonical(canonical.state);
    constexpr std::array<int, kBoardSize> order{{3, 2, 4, 1, 5, 0, 6}};
    int best = -1;
    float best_value = -std::numeric_limits<float>::infinity();
    for (const int column : order) {
      if (!isLegal(canonical.state.board, column)) continue;
      if (logits[column] > best_value) {
        best_value = logits[column];
        best = column;
      }
    }
    return canonical.mirrored && best >= 0 ? kBoardSize - 1 - best : best;
  }

  std::size_t parameterBytes() const {
    return (weight1.size() + bias1.size() + weight2.size() + bias2.size() +
            weight3.size() + bias3.size()) *
           sizeof(float);
  }

  void save(const std::string& path) const {
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("could not open student checkpoint");
    const std::array<std::uint32_t, 5> header{{
        0x4450'3753u, kInputCount, kHidden1, kHidden2, kOutputCount,
    }};
    output.write(reinterpret_cast<const char*>(header.data()),
                 static_cast<std::streamsize>(header.size() *
                                              sizeof(header[0])));
    const auto write = [&output](const auto& values) {
      output.write(reinterpret_cast<const char*>(values.data()),
                   static_cast<std::streamsize>(values.size() *
                                                sizeof(values[0])));
    };
    write(weight1);
    write(bias1);
    write(weight2);
    write(bias2);
    write(weight3);
    write(bias3);
    if (!output) throw std::runtime_error("could not write student checkpoint");
  }
};

constexpr std::array<int, kBoardSize> kColumnOrder{{3, 2, 4, 1, 5, 0, 6}};

struct TeacherLabel {
  State canonical{};
  std::array<double, kOutputCount> values{};
  std::array<float, kOutputCount> targets{};
  int canonical_action = -1;
  int completed_depth = 0;
  std::uint64_t work = 0;
};

TeacherLabel queryTeacher(const State& source,
                          const cfpi::BehaviorOptions& options) {
  bool mirrored = false;
  State canonical = cfpi::detail::canonicalState(source, mirrored);
  (void)mirrored;
  cfpi::detail::SearchContext context(options);
  std::array<double, kOutputCount> completed_values{};
  completed_values.fill(-std::numeric_limits<double>::infinity());
  int completed_action = -1;
  int completed_depth = 0;
  for (int depth = 1; depth <= options.max_depth; ++depth) {
    std::array<double, kOutputCount> candidate_values{};
    candidate_values.fill(-std::numeric_limits<double>::infinity());
    int candidate_action = -1;
    double candidate_best = -std::numeric_limits<double>::infinity();
    try {
      for (const int column : kColumnOrder) {
        if (!isLegal(canonical.board, column)) continue;
        const double value =
            cfpi::detail::evaluateAction(canonical, column, depth, context);
        candidate_values[column] = value;
        if (value > candidate_best) {
          candidate_best = value;
          candidate_action = column;
        }
      }
    } catch (const cfpi::detail::WorkLimitReached&) {
      break;
    }
    if (candidate_action < 0) break;
    completed_values = candidate_values;
    completed_action = candidate_action;
    completed_depth = depth;
  }
  if (completed_action < 0) {
    completed_action = centerFirstMove(canonical.board);
    if (completed_action < 0) {
      throw std::runtime_error("teacher found no action in a live state");
    }
    completed_values[completed_action] = 0.0;
  }

  double mean = 0.0;
  int legal_count = 0;
  double maximum = -std::numeric_limits<double>::infinity();
  for (int column = 0; column < kOutputCount; ++column) {
    if (!std::isfinite(completed_values[column])) continue;
    mean += completed_values[column];
    maximum = std::max(maximum, completed_values[column]);
    ++legal_count;
  }
  mean /= legal_count;
  double squared = 0.0;
  for (const double value : completed_values) {
    if (std::isfinite(value)) squared += (value - mean) * (value - mean);
  }
  const double deviation = std::sqrt(squared / legal_count);
  const double temperature = std::max(1000.0, 0.5 * deviation);
  std::array<float, kOutputCount> targets{};
  double total = 0.0;
  for (int column = 0; column < kOutputCount; ++column) {
    if (!std::isfinite(completed_values[column])) continue;
    targets[column] = static_cast<float>(
        std::exp((completed_values[column] - maximum) / temperature));
    total += targets[column];
  }
  for (int column = 0; column < kOutputCount; ++column) {
    if (!std::isfinite(completed_values[column])) continue;
    const float soft = static_cast<float>(targets[column] / total);
    targets[column] = 0.15f * soft +
                      (column == completed_action ? 0.85f : 0.0f);
  }
  return {canonical, completed_values, targets, completed_action,
          completed_depth, context.work};
}

struct Example {
  State state{};
  std::array<float, kOutputCount> targets{};
  int teacher_action = -1;
  int teacher_depth = 0;
};

struct Corpus {
  std::vector<Example> examples;
  std::uint64_t teacher_work = 0;
  int depth_three_labels = 0;
};

void validateTrainingSeeds(std::uint32_t start, int games) {
  if (games <= 0) throw std::invalid_argument("game count must be positive");
  const std::uint64_t end =
      static_cast<std::uint64_t>(start) + static_cast<std::uint64_t>(games);
  if (start < kTrainingStart || end > kTrainingEnd) {
    throw std::invalid_argument("environment seed leaves training partition");
  }
}

Corpus collectCorpus(std::uint32_t seed_start, int games, int maximum_moves,
                     const cfpi::BehaviorOptions& teacher,
                     const Network* actor, std::string_view label) {
  validateTrainingSeeds(seed_start, games);
  Corpus corpus;
  corpus.examples.reserve(static_cast<std::size_t>(games * maximum_moves));
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed =
        seed_start + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < maximum_moves) {
      const TeacherLabel teacher_label = queryTeacher(state, teacher);
      corpus.examples.push_back({teacher_label.canonical,
                                 teacher_label.targets,
                                 teacher_label.canonical_action,
                                 teacher_label.completed_depth});
      corpus.teacher_work += teacher_label.work;
      corpus.depth_three_labels += teacher_label.completed_depth == 3;
      const bool mirrored = mirrorIsSmaller(state.board);
      const int teacher_action =
          mirrored ? kBoardSize - 1 - teacher_label.canonical_action
                   : teacher_label.canonical_action;
      const int action = actor != nullptr ? actor->chooseAction(state)
                                          : teacher_action;
      if (!isLegal(state.board, action)) {
        throw std::runtime_error("corpus actor selected illegal action");
      }
      MoveResult move;
      if (!playHeadlessMove(state, seed, action, move)) {
        throw std::runtime_error("corpus transition rejected legal action");
      }
    }
    std::cerr << label << ' ' << (game + 1) << '/' << games << " seed 0x"
              << std::hex << seed << std::dec << " states "
              << corpus.examples.size() << '\n';
  }
  return corpus;
}

struct Tensors {
  std::vector<float> weight1 =
      std::vector<float>(kHidden1 * kInputCount);
  std::array<float, kHidden1> bias1{};
  std::array<float, kHidden2 * kHidden1> weight2{};
  std::array<float, kHidden2> bias2{};
  std::array<float, kOutputCount * kHidden2> weight3{};
  std::array<float, kOutputCount> bias3{};

  void clear() {
    std::fill(weight1.begin(), weight1.end(), 0.0f);
    bias1.fill(0.0f);
    weight2.fill(0.0f);
    bias2.fill(0.0f);
    weight3.fill(0.0f);
    bias3.fill(0.0f);
  }
};

std::array<float, kOutputCount> legalProbabilities(
    const State& state, const std::array<float, kOutputCount>& logits) {
  float maximum = -std::numeric_limits<float>::infinity();
  for (int column = 0; column < kOutputCount; ++column) {
    if (isLegal(state.board, column)) maximum = std::max(maximum, logits[column]);
  }
  std::array<float, kOutputCount> probabilities{};
  float total = 0.0f;
  for (int column = 0; column < kOutputCount; ++column) {
    if (!isLegal(state.board, column)) continue;
    probabilities[column] = std::exp(logits[column] - maximum);
    total += probabilities[column];
  }
  if (!(total > 0.0f)) throw std::runtime_error("empty legal softmax");
  for (float& probability : probabilities) probability /= total;
  return probabilities;
}

float accumulateGradient(const Network& network, const Example& example,
                         Tensors& gradient) {
  ForwardCache cache;
  const auto logits = network.forwardCanonical(example.state, &cache);
  const auto probabilities = legalProbabilities(example.state, logits);
  std::array<float, kOutputCount> delta3{};
  float loss = 0.0f;
  for (int output = 0; output < kOutputCount; ++output) {
    if (example.targets[output] > 0.0f) {
      loss -= example.targets[output] *
              std::log(std::max(1.0e-12f, probabilities[output]));
    }
    delta3[output] = probabilities[output] - example.targets[output];
    gradient.bias3[output] += delta3[output];
    const int base = output * kHidden2;
    for (int hidden = 0; hidden < kHidden2; ++hidden) {
      gradient.weight3[base + hidden] +=
          delta3[output] * cache.hidden2[hidden];
    }
  }

  std::array<float, kHidden2> delta2{};
  for (int hidden = 0; hidden < kHidden2; ++hidden) {
    float value = 0.0f;
    for (int output = 0; output < kOutputCount; ++output) {
      value += network.weight3[output * kHidden2 + hidden] * delta3[output];
    }
    delta2[hidden] = cache.pre2[hidden] > 0.0f ? value : 0.0f;
    gradient.bias2[hidden] += delta2[hidden];
    const int base = hidden * kHidden1;
    for (int prior = 0; prior < kHidden1; ++prior) {
      gradient.weight2[base + prior] +=
          delta2[hidden] * cache.hidden1[prior];
    }
  }

  std::array<float, kHidden1> delta1{};
  for (int hidden = 0; hidden < kHidden1; ++hidden) {
    float value = 0.0f;
    for (int next = 0; next < kHidden2; ++next) {
      value += network.weight2[next * kHidden1 + hidden] * delta2[next];
    }
    delta1[hidden] = cache.pre1[hidden] > 0.0f ? value : 0.0f;
    gradient.bias1[hidden] += delta1[hidden];
    const int base = hidden * kInputCount;
    for (int offset = 0; offset < cache.input.count; ++offset) {
      gradient.weight1[base + cache.input.indices[offset]] += delta1[hidden];
    }
  }
  return loss;
}

struct Adam {
  Tensors first;
  Tensors second;
  int steps = 0;

  void update(Network& network, const Tensors& gradient, int batch_size,
              float learning_rate) {
    ++steps;
    constexpr float beta1 = 0.9f;
    constexpr float beta2 = 0.999f;
    constexpr float epsilon = 1.0e-8f;
    constexpr float decay = 1.0e-5f;
    const float correction =
        learning_rate *
        std::sqrt(1.0f - std::pow(beta2, static_cast<float>(steps))) /
        (1.0f - std::pow(beta1, static_cast<float>(steps)));
    const float inverse_batch = 1.0f / batch_size;
    const auto update_values = [&](auto& parameters, const auto& gradients,
                                   auto& first_values, auto& second_values,
                                   bool regularize) {
      for (std::size_t index = 0; index < parameters.size(); ++index) {
        const float value = gradients[index] * inverse_batch;
        first_values[index] = beta1 * first_values[index] +
                              (1.0f - beta1) * value;
        second_values[index] = beta2 * second_values[index] +
                               (1.0f - beta2) * value * value;
        parameters[index] -=
            correction * first_values[index] /
                (std::sqrt(second_values[index]) + epsilon) +
            (regularize ? learning_rate * decay * parameters[index] : 0.0f);
      }
    };
    update_values(network.weight1, gradient.weight1, first.weight1,
                  second.weight1, true);
    update_values(network.bias1, gradient.bias1, first.bias1, second.bias1,
                  false);
    update_values(network.weight2, gradient.weight2, first.weight2,
                  second.weight2, true);
    update_values(network.bias2, gradient.bias2, first.bias2, second.bias2,
                  false);
    update_values(network.weight3, gradient.weight3, first.weight3,
                  second.weight3, true);
    update_values(network.bias3, gradient.bias3, first.bias3, second.bias3,
                  false);
  }
};

struct PolicyMetrics {
  double loss = 0.0;
  double top1 = 0.0;
  double top2 = 0.0;
};

PolicyMetrics evaluatePolicy(const Network& network,
                             const std::vector<Example>& examples) {
  if (examples.empty()) throw std::invalid_argument("empty policy corpus");
  PolicyMetrics metrics;
  for (const Example& example : examples) {
    const auto logits = network.logitsCanonical(example.state);
    const auto probabilities = legalProbabilities(example.state, logits);
    for (int column = 0; column < kOutputCount; ++column) {
      if (example.targets[column] > 0.0f) {
        metrics.loss -= example.targets[column] *
                        std::log(std::max(1.0e-12f, probabilities[column]));
      }
    }
    std::array<int, kOutputCount> ranking{};
    std::iota(ranking.begin(), ranking.end(), 0);
    std::stable_sort(ranking.begin(), ranking.end(), [&](int left, int right) {
      const float left_value = isLegal(example.state.board, left)
                                   ? logits[left]
                                   : -std::numeric_limits<float>::infinity();
      const float right_value = isLegal(example.state.board, right)
                                    ? logits[right]
                                    : -std::numeric_limits<float>::infinity();
      return left_value > right_value;
    });
    metrics.top1 += ranking[0] == example.teacher_action;
    metrics.top2 += ranking[0] == example.teacher_action ||
                    ranking[1] == example.teacher_action;
  }
  const double denominator = static_cast<double>(examples.size());
  metrics.loss /= denominator;
  metrics.top1 /= denominator;
  metrics.top2 /= denominator;
  return metrics;
}

void train(Network& network, const std::vector<Example>& examples, int epochs,
           int batch_size, float learning_rate, std::string_view label) {
  if (examples.empty()) throw std::invalid_argument("empty training corpus");
  Adam adam;
  Tensors gradient;
  std::vector<int> order(examples.size());
  std::iota(order.begin(), order.end(), 0);
  Mulberry32 random(0x3d75'414du);
  for (int epoch = 0; epoch < epochs; ++epoch) {
    for (std::size_t offset = order.size(); offset > 1; --offset) {
      const std::size_t swap = static_cast<std::size_t>(
          (static_cast<std::uint64_t>(random.nextBits()) * offset) >> 32);
      std::swap(order[offset - 1], order[swap]);
    }
    double loss = 0.0;
    for (std::size_t begin = 0; begin < order.size();
         begin += static_cast<std::size_t>(batch_size)) {
      const std::size_t end = std::min(
          order.size(), begin + static_cast<std::size_t>(batch_size));
      gradient.clear();
      for (std::size_t index = begin; index < end; ++index) {
        loss += accumulateGradient(network, examples[order[index]], gradient);
      }
      adam.update(network, gradient, static_cast<int>(end - begin),
                  learning_rate);
    }
    if (epoch == 0 || (epoch + 1) % 10 == 0 || epoch + 1 == epochs) {
      std::cerr << label << " epoch " << (epoch + 1) << '/' << epochs
                << " online-ce " << std::fixed << std::setprecision(4)
                << loss / examples.size() << '\n';
    }
  }
}

struct GameMetrics {
  double mean_score = 0.0;
  double mean_moves = 0.0;
  int censored = 0;
};

GameMetrics evaluateGames(const Network& network, std::uint32_t seed_start,
                          int games, int maximum_moves,
                          std::string_view label) {
  validateTrainingSeeds(seed_start, games);
  GameMetrics metrics;
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed =
        seed_start + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < maximum_moves) {
      const int action = network.chooseAction(state);
      if (!isLegal(state.board, action)) {
        throw std::runtime_error("student selected illegal action");
      }
      MoveResult move;
      if (!playHeadlessMove(state, seed, action, move)) {
        throw std::runtime_error("student transition rejected legal action");
      }
    }
    metrics.mean_score += static_cast<double>(state.score) / games;
    metrics.mean_moves += static_cast<double>(state.moves_played) / games;
    metrics.censored += !state.game_over;
    std::cerr << label << ' ' << (game + 1) << '/' << games << " seed 0x"
              << std::hex << seed << std::dec << ' ' << state.score << " ("
              << state.moves_played << " moves)\n";
  }
  return metrics;
}

struct RunConfig {
  int train_games = 8;
  int heldout_games = 3;
  int evaluation_games = 6;
  int label_moves = 80;
  int evaluation_moves = 300;
  int epochs = 40;
  int batch_size = 64;
  int dagger_games = 3;
  int dagger_epochs = 20;
  float learning_rate = 0.001f;
  std::string output = "/tmp/drop7-phase-student.json";
  std::string model = "/tmp/drop7-phase-student.bin";
};

constexpr std::uint32_t kTrainSeedStart = 0x3d76'0000u;
constexpr std::uint32_t kHeldoutSeedStart = 0x3d77'0000u;
constexpr std::uint32_t kEvaluationSeedStart = 0x3d78'0000u;
constexpr std::uint32_t kDaggerSeedStart = 0x3d79'0000u;

int positiveInteger(const char* value, std::string_view name) {
  std::size_t consumed = 0;
  const long long parsed = std::stoll(value, &consumed, 0);
  if (consumed != std::string(value).size() || parsed <= 0 ||
      parsed > std::numeric_limits<int>::max()) {
    throw std::invalid_argument(std::string(name) + " must be positive");
  }
  return static_cast<int>(parsed);
}

float positiveFloat(const char* value, std::string_view name) {
  std::size_t consumed = 0;
  const float parsed = std::stof(value, &consumed);
  if (consumed != std::string(value).size() || !(parsed > 0.0f) ||
      !std::isfinite(parsed)) {
    throw std::invalid_argument(std::string(name) + " must be positive");
  }
  return parsed;
}

RunConfig parseRunConfig(int argc, char** argv) {
  RunConfig config;
  for (int index = 2; index < argc; ++index) {
    const std::string argument = argv[index];
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing value for " + argument);
    }
    const char* value = argv[++index];
    if (argument == "--train-games") {
      config.train_games = positiveInteger(value, argument);
    } else if (argument == "--heldout-games") {
      config.heldout_games = positiveInteger(value, argument);
    } else if (argument == "--evaluation-games") {
      config.evaluation_games = positiveInteger(value, argument);
    } else if (argument == "--label-moves") {
      config.label_moves = positiveInteger(value, argument);
    } else if (argument == "--evaluation-moves") {
      config.evaluation_moves = positiveInteger(value, argument);
    } else if (argument == "--epochs") {
      config.epochs = positiveInteger(value, argument);
    } else if (argument == "--batch-size") {
      config.batch_size = positiveInteger(value, argument);
    } else if (argument == "--dagger-games") {
      config.dagger_games = positiveInteger(value, argument);
    } else if (argument == "--dagger-epochs") {
      config.dagger_epochs = positiveInteger(value, argument);
    } else if (argument == "--learning-rate") {
      config.learning_rate = positiveFloat(value, argument);
    } else if (argument == "--output") {
      config.output = value;
    } else if (argument == "--model") {
      config.model = value;
    } else {
      throw std::invalid_argument("unknown argument " + argument);
    }
  }
  return config;
}

void writeArtifact(const RunConfig& config, std::size_t train_examples,
                   std::size_t heldout_examples,
                   const PolicyMetrics& heldout,
                   const GameMetrics& games, bool pilot_passed,
                   bool dagger_ran, std::size_t dagger_examples,
                   const PolicyMetrics& final_heldout,
                   const GameMetrics& final_games,
                   double teacher_depth_three_rate,
                   double elapsed_seconds) {
  std::ofstream output(config.output);
  if (!output) throw std::runtime_error("could not open result artifact");
  output << std::setprecision(10)
         << "{\n  \"format\": \"drop7-phase-student-v1\",\n"
         << "  \"trainingSeedOnly\": true,\n"
         << "  \"teacher\": \"phase-d3-s5\",\n"
         << "  \"network\": \"614x128x128x7-canonical\",\n"
         << "  \"parameterBytes\": 384540,\n"
         << "  \"trainExamples\": " << train_examples << ",\n"
         << "  \"heldoutExamples\": " << heldout_examples << ",\n"
         << "  \"teacherDepthThreeRate\": " << teacher_depth_three_rate
         << ",\n"
         << "  \"pilot\": {\"top1\": " << heldout.top1
         << ", \"top2\": " << heldout.top2
         << ", \"crossEntropy\": " << heldout.loss
         << ", \"studentMeanScore\": " << games.mean_score
         << ", \"studentMeanMoves\": " << games.mean_moves
         << ", \"censored\": " << games.censored << "},\n"
         << "  \"gates\": {\"top1\": 0.75, \"top2\": 0.92, "
            "\"studentMeanScore\": 250000, \"studentMeanMoves\": 75},\n"
         << "  \"pilotQualified\": "
         << (pilot_passed ? "true" : "false") << ",\n"
         << "  \"daggerRan\": " << (dagger_ran ? "true" : "false")
         << ",\n  \"daggerExamples\": " << dagger_examples << ",\n"
         << "  \"final\": {\"top1\": " << final_heldout.top1
         << ", \"top2\": " << final_heldout.top2
         << ", \"crossEntropy\": " << final_heldout.loss
         << ", \"studentMeanScore\": " << final_games.mean_score
         << ", \"studentMeanMoves\": " << final_games.mean_moves
         << ", \"censored\": " << final_games.censored << "},\n"
         << "  \"decision\": \"" << (pilot_passed ? "advance" : "reject")
         << "\",\n  \"model\": \"" << config.model << "\",\n"
         << "  \"elapsedSeconds\": " << elapsed_seconds << "\n}\n";
}

int runPilot(const RunConfig& config, std::ostream& output) {
  validateTrainingSeeds(kTrainSeedStart, config.train_games);
  validateTrainingSeeds(kHeldoutSeedStart, config.heldout_games);
  validateTrainingSeeds(kEvaluationSeedStart, config.evaluation_games);
  validateTrainingSeeds(kDaggerSeedStart, config.dagger_games);
  const auto started = std::chrono::steady_clock::now();
  cfpi::BehaviorOptions teacher;
  teacher.max_depth = 3;
  teacher.chance_samples = 5;
  teacher.max_work = 1'000'000;
  teacher.max_cache_entries = 40'000;

  Corpus training = collectCorpus(kTrainSeedStart, config.train_games,
                                  config.label_moves, teacher, nullptr,
                                  "teacher-train");
  const Corpus heldout = collectCorpus(kHeldoutSeedStart,
                                       config.heldout_games,
                                       config.label_moves, teacher, nullptr,
                                       "teacher-heldout");
  Network network;
  train(network, training.examples, config.epochs, config.batch_size,
        config.learning_rate, "distill");
  const PolicyMetrics heldout_metrics =
      evaluatePolicy(network, heldout.examples);
  const GameMetrics game_metrics = evaluateGames(
      network, kEvaluationSeedStart, config.evaluation_games,
      config.evaluation_moves, "student-pilot");
  const bool pilot_passed = heldout_metrics.top1 >= 0.75 &&
                            heldout_metrics.top2 >= 0.92 &&
                            game_metrics.mean_score >= 250'000.0 &&
                            game_metrics.mean_moves >= 75.0;

  bool dagger_ran = false;
  std::size_t dagger_examples = 0;
  PolicyMetrics final_heldout = heldout_metrics;
  GameMetrics final_games = game_metrics;
  if (pilot_passed) {
    dagger_ran = true;
    Corpus dagger = collectCorpus(kDaggerSeedStart, config.dagger_games,
                                  config.label_moves, teacher, &network,
                                  "dagger");
    dagger_examples = dagger.examples.size();
    training.examples.insert(training.examples.end(), dagger.examples.begin(),
                             dagger.examples.end());
    train(network, training.examples, config.dagger_epochs, config.batch_size,
          config.learning_rate * 0.5f, "dagger-distill");
    final_heldout = evaluatePolicy(network, heldout.examples);
    final_games = evaluateGames(network, kEvaluationSeedStart,
                                config.evaluation_games,
                                config.evaluation_moves, "student-dagger");
  }
  network.save(config.model);
  const int teacher_labels = static_cast<int>(training.examples.size() +
                                               heldout.examples.size());
  const double depth_three_rate =
      static_cast<double>(training.depth_three_labels +
                          heldout.depth_three_labels) /
      std::max(1, teacher_labels);
  const double elapsed_seconds = std::chrono::duration<double>(
                                     std::chrono::steady_clock::now() - started)
                                     .count();
  writeArtifact(config, training.examples.size() - dagger_examples,
                heldout.examples.size(), heldout_metrics, game_metrics,
                pilot_passed, dagger_ran, dagger_examples, final_heldout,
                final_games, depth_three_rate, elapsed_seconds);
  output << std::fixed << std::setprecision(6)
         << "PHASE_STUDENT_RESULT {\"trainingSeedOnly\":true"
         << ",\"trainExamples\":"
         << training.examples.size() - dagger_examples
         << ",\"heldoutExamples\":" << heldout.examples.size()
         << ",\"top1\":" << heldout_metrics.top1
         << ",\"top2\":" << heldout_metrics.top2
         << ",\"studentMeanScore\":" << game_metrics.mean_score
         << ",\"studentMeanMoves\":" << game_metrics.mean_moves
         << ",\"pilotQualified\":"
         << (pilot_passed ? "true" : "false")
         << ",\"daggerRan\":" << (dagger_ran ? "true" : "false")
         << ",\"decision\":\"" << (pilot_passed ? "advance" : "reject")
         << "\",\"artifact\":\"" << config.output << "\"}\n";
  return 0;
}

bool selfTest(std::ostream& output) {
  Network network;
  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(4, 1)] = 2;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;
  State mirrored = state;
  mirrored.board = mirrorBoard(state.board);
  const int action = network.chooseAction(state);
  const int reflected = network.chooseAction(mirrored);
  const bool reflection_safe = reflected == kBoardSize - 1 - action;
  const bool legal = isLegal(state.board, action) &&
                     isLegal(mirrored.board, reflected);
  const auto logits = network.logitsCanonical(canonicalize(state).state);
  const bool finite = std::all_of(
      logits.begin(), logits.end(),
      [](float value) { return std::isfinite(value); });
  const bool sized = network.parameterBytes() > 300'000;
  const bool seed_partition = kTrainingStart < kTrainingEnd;
  const bool passed = reflection_safe && legal && finite && sized &&
                      seed_partition;
  output << "PHASE_STUDENT_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe ? "true" : "false")
         << ",\"legalMask\":" << (legal ? "true" : "false")
         << ",\"finite\":" << (finite ? "true" : "false")
         << ",\"parametersBytes\":" << network.parameterBytes()
         << ",\"trainingSeedOnly\":"
         << (seed_partition ? "true" : "false") << "}\n";
  return passed;
}

}  // namespace drop7::phase_student

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
      return drop7::phase_student::selfTest(std::cout) ? 0 : 1;
    }
    if (argc >= 2 && std::string(argv[1]) == "--pilot") {
      const auto config = drop7::phase_student::parseRunConfig(argc, argv);
      return drop7::phase_student::runPilot(config, std::cout);
    }
    std::cerr
        << "usage: drop7_phase_student --self-test | --pilot "
           "[--train-games N] [--heldout-games N] [--evaluation-games N] "
           "[--label-moves N] [--evaluation-moves N] [--epochs N] "
           "[--batch-size N] [--dagger-games N] [--dagger-epochs N] "
           "[--learning-rate X] [--output PATH] [--model PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_phase_student: " << error.what() << '\n';
    return 1;
  }
}
