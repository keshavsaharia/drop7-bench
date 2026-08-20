#include "../../../src/core/native/engine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <vector>

// Learns the chance-state value U(board, moves-until-rise) with fitted
// multi-step TD.  U deliberately does not see next_disc.
// The current visible disc still conditions action selection because it is
// placed before successor U values are compared.  Averaging U over actual
// trajectories marginalizes the *future* disc without a seven-way input split.
namespace drop7::nnue_value {

constexpr int kHidden1 = 32;
constexpr int kHidden2 = 16;
constexpr int kMaxFeatures = 158;

constexpr int kCellBase = 0;
constexpr int kCellFeatures = kCellCount * 10;
constexpr int kPhaseBase = kCellBase + kCellFeatures;
constexpr int kPhaseFeatures = kMovesPerLevel;
constexpr int kColumnBase = kPhaseBase + kPhaseFeatures;
constexpr int kColumnFeatures = kBoardSize * (kBoardSize + 1);
constexpr int kRowBase = kColumnBase + kColumnFeatures;
constexpr int kRowFeatures = kBoardSize * (kBoardSize + 1);
constexpr int kCountBase = kRowBase + kRowFeatures;
constexpr int kCountFeatures = 10 * (kCellCount + 1);
constexpr int kHorizontalPairBase = kCountBase + kCountFeatures;
constexpr int kPairPlacements = kBoardSize * (kBoardSize - 1);
constexpr int kPairFeatures = kPairPlacements * 100;
constexpr int kVerticalPairBase = kHorizontalPairBase + kPairFeatures;
constexpr int kFeatureCount = kVerticalPairBase + kPairFeatures;

struct Rng {
  explicit Rng(std::uint32_t seed) : random(seed) {}
  std::uint32_t bits() { return random.nextBits(); }
  float unit() { return static_cast<float>(random.nextUnit()); }
  int bounded(int bound) {
    return static_cast<int>((static_cast<std::uint64_t>(bits()) * bound) >> 32);
  }
  Mulberry32 random;
};

struct CompactState {
  Board board{};
  std::uint8_t moves_remaining = kMovesPerLevel;
  std::uint8_t terminal = 0;
};

CompactState compact(const State& state) {
  return {state.board, static_cast<std::uint8_t>(state.moves_remaining),
          static_cast<std::uint8_t>(state.game_over)};
}

bool mirrorIsSmaller(const Board& board) {
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      const auto forward = board[indexOf(row, column)];
      const auto reflected = board[indexOf(row, kBoardSize - 1 - column)];
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

CompactState canonicalize(const CompactState& source) {
  CompactState result = source;
  if (mirrorIsSmaller(source.board)) result.board = mirrorBoard(source.board);
  return result;
}

int physicalAction(int canonical_action, bool mirrored) {
  return mirrored ? kBoardSize - 1 - canonical_action : canonical_action;
}

std::uint32_t observableHash(const State& state) {
  std::uint32_t hash = 0x811c'9dc5u;
  for (std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint32_t>(cell + 1);
    hash *= 0x0100'0193u;
  }
  // Current next_disc is observable and affects the candidate transitions.
  // It is used only to choose reproducible common chance samples, never as a
  // learned input to future chance-state U.
  hash ^= state.next_disc;
  hash *= 0x0100'0193u;
  hash ^= static_cast<std::uint32_t>(state.moves_remaining);
  return mix32(hash);
}

struct FeatureSet {
  std::array<int, kMaxFeatures> ids{};
  int count = 0;
};

FeatureSet extractFeatures(const CompactState& original) {
  const CompactState state = canonicalize(original);
  FeatureSet result;
  std::array<int, 10> token_counts{};
  for (int index = 0; index < kCellCount; ++index) {
    const int token = state.board[index];
    result.ids[result.count++] = kCellBase + index * 10 + token;
    ++token_counts[token];
  }
  result.ids[result.count++] =
      kPhaseBase + std::clamp<int>(state.moves_remaining, 1, kMovesPerLevel) - 1;

  for (int column = 0; column < kBoardSize; ++column) {
    int occupied = 0;
    for (int row = 0; row < kBoardSize; ++row) {
      occupied += state.board[indexOf(row, column)] != kEmpty;
    }
    result.ids[result.count++] =
        kColumnBase + column * (kBoardSize + 1) + occupied;
  }
  for (int row = 0; row < kBoardSize; ++row) {
    int occupied = 0;
    for (int column = 0; column < kBoardSize; ++column) {
      occupied += state.board[indexOf(row, column)] != kEmpty;
    }
    result.ids[result.count++] =
        kRowBase + row * (kBoardSize + 1) + occupied;
  }
  for (int token = 0; token < 10; ++token) {
    result.ids[result.count++] =
        kCountBase + token * (kCellCount + 1) + token_counts[token];
  }
  int placement = 0;
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize - 1; ++column, ++placement) {
      const int pair = state.board[indexOf(row, column)] * 10 +
                       state.board[indexOf(row, column + 1)];
      result.ids[result.count++] =
          kHorizontalPairBase + placement * 100 + pair;
    }
  }
  placement = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    for (int row = 0; row < kBoardSize - 1; ++row, ++placement) {
      const int pair = state.board[indexOf(row, column)] * 10 +
                       state.board[indexOf(row + 1, column)];
      result.ids[result.count++] =
          kVerticalPairBase + placement * 100 + pair;
    }
  }
  if (result.count != kMaxFeatures) {
    throw std::logic_error("NNUE feature-count invariant failed");
  }
  return result;
}

float leaky(float value) { return value >= 0 ? value : value * 0.05f; }
float leakyDerivative(float value) { return value >= 0 ? 1.0f : 0.05f; }

struct Parameters {
  std::vector<float> embedding =
      std::vector<float>(kFeatureCount * kHidden1);
  std::array<float, kHidden1> bias1{};
  std::array<float, kHidden2 * kHidden1> weight2{};
  std::array<float, kHidden2> bias2{};
  std::array<float, kHidden2> output_weight{};
  float output_bias = 60.0f;
};

struct ForwardCache {
  FeatureSet features;
  std::array<float, kHidden1> pre1{};
  std::array<float, kHidden1> hidden1{};
  std::array<float, kHidden2> pre2{};
  std::array<float, kHidden2> hidden2{};
  float value = 0;
};

class Network {
 public:
  explicit Network(std::uint32_t seed = 0x4e4e'5545u) { initialize(seed); }

  float value(const CompactState& state) const {
    if (state.terminal) return 0;
    return forward(state, nullptr);
  }
  float value(const State& state) const { return value(compact(state)); }

  float forward(const CompactState& state, ForwardCache* cache) const {
    ForwardCache local;
    ForwardCache& output = cache ? *cache : local;
    output.features = extractFeatures(state);
    output.pre1 = parameters.bias1;
    for (int offset = 0; offset < output.features.count; ++offset) {
      const int feature = output.features.ids[offset];
      const int base = feature * kHidden1;
      for (int hidden = 0; hidden < kHidden1; ++hidden) {
        output.pre1[hidden] += parameters.embedding[base + hidden];
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
    output.value = parameters.output_bias;
    for (int hidden = 0; hidden < kHidden2; ++hidden) {
      output.value +=
          parameters.output_weight[hidden] * output.hidden2[hidden];
    }
    return output.value;
  }

  std::size_t parameterBytes() const {
    return (parameters.embedding.size() + parameters.bias1.size() +
            parameters.weight2.size() + parameters.bias2.size() +
            parameters.output_weight.size() + 1) *
           sizeof(float);
  }

  void save(const std::string& path) const {
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("could not open NNUE checkpoint");
    constexpr std::array<char, 8> magic{{'D', '7', 'N', 'N', 'U', 'E', 'U', '1'}};
    output.write(magic.data(), magic.size());
    const std::array<std::uint32_t, 4> dimensions{
        kFeatureCount, kHidden1, kHidden2,
        static_cast<std::uint32_t>(parameters.embedding.size())};
    output.write(reinterpret_cast<const char*>(dimensions.data()),
                 sizeof(dimensions));
    write(output, parameters.embedding);
    write(output, parameters.bias1);
    write(output, parameters.weight2);
    write(output, parameters.bias2);
    write(output, parameters.output_weight);
    output.write(reinterpret_cast<const char*>(&parameters.output_bias),
                 sizeof(parameters.output_bias));
    if (!output) throw std::runtime_error("failed writing NNUE checkpoint");
  }

  void load(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not open NNUE checkpoint");
    std::array<char, 8> magic{};
    input.read(magic.data(), magic.size());
    constexpr std::array<char, 8> expected{{'D', '7', 'N', 'N', 'U', 'E', 'U', '1'}};
    std::array<std::uint32_t, 4> dimensions{};
    input.read(reinterpret_cast<char*>(dimensions.data()), sizeof(dimensions));
    if (magic != expected || dimensions[0] != kFeatureCount ||
        dimensions[1] != kHidden1 || dimensions[2] != kHidden2 ||
        dimensions[3] != parameters.embedding.size()) {
      throw std::runtime_error("incompatible NNUE checkpoint");
    }
    read(input, parameters.embedding);
    read(input, parameters.bias1);
    read(input, parameters.weight2);
    read(input, parameters.bias2);
    read(input, parameters.output_weight);
    input.read(reinterpret_cast<char*>(&parameters.output_bias),
               sizeof(parameters.output_bias));
    if (!input) throw std::runtime_error("truncated NNUE checkpoint");
  }

  Parameters parameters;

 private:
  template <typename Container>
  static void write(std::ofstream& output, const Container& values) {
    output.write(reinterpret_cast<const char*>(values.data()),
                 static_cast<std::streamsize>(values.size() * sizeof(float)));
  }
  template <typename Container>
  static void read(std::ifstream& input, Container& values) {
    input.read(reinterpret_cast<char*>(values.data()),
               static_cast<std::streamsize>(values.size() * sizeof(float)));
  }

  void initialize(std::uint32_t seed) {
    Rng rng(seed);
    auto uniform = [&rng](float radius) {
      return (rng.unit() * 2.0f - 1.0f) * radius;
    };
    for (float& weight : parameters.embedding) weight = uniform(0.006f);
    for (float& weight : parameters.weight2) weight = uniform(0.12f);
    for (float& weight : parameters.output_weight) weight = uniform(0.04f);
    parameters.output_bias = 60.0f;
  }
};

struct Gradient {
  Gradient()
      : embedding(kFeatureCount * kHidden1),
        touched_marker(kFeatureCount, 0) {}
  void clearDense() {
    bias1.fill(0);
    weight2.fill(0);
    bias2.fill(0);
    output_weight.fill(0);
    output_bias = 0;
    touched.clear();
  }
  void touch(int feature) {
    if (touched_marker[feature]) return;
    touched_marker[feature] = 1;
    touched.push_back(feature);
  }
  void releaseTouched() {
    for (int feature : touched) {
      std::fill_n(embedding.begin() + feature * kHidden1, kHidden1, 0.0f);
      touched_marker[feature] = 0;
    }
    touched.clear();
  }

  std::vector<float> embedding;
  std::vector<std::uint8_t> touched_marker;
  std::vector<int> touched;
  std::array<float, kHidden1> bias1{};
  std::array<float, kHidden2 * kHidden1> weight2{};
  std::array<float, kHidden2> bias2{};
  std::array<float, kHidden2> output_weight{};
  float output_bias = 0;
};

void backward(const Network& network, const ForwardCache& cache,
              float derivative, Gradient& gradient) {
  gradient.output_bias += derivative;
  std::array<float, kHidden2> derivative2{};
  for (int hidden = 0; hidden < kHidden2; ++hidden) {
    gradient.output_weight[hidden] += derivative * cache.hidden2[hidden];
    derivative2[hidden] = derivative * network.parameters.output_weight[hidden] *
                          leakyDerivative(cache.pre2[hidden]);
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
  for (int hidden = 0; hidden < kHidden1; ++hidden) {
    derivative1[hidden] *= leakyDerivative(cache.pre1[hidden]);
    gradient.bias1[hidden] += derivative1[hidden];
  }
  for (int offset = 0; offset < cache.features.count; ++offset) {
    const int feature = cache.features.ids[offset];
    gradient.touch(feature);
    const int base = feature * kHidden1;
    for (int hidden = 0; hidden < kHidden1; ++hidden) {
      gradient.embedding[base + hidden] += derivative1[hidden];
    }
  }
}

struct AdamMoments {
  AdamMoments()
      : embedding_m(kFeatureCount * kHidden1),
        embedding_v(kFeatureCount * kHidden1) {}
  std::vector<float> embedding_m;
  std::vector<float> embedding_v;
  std::array<float, kHidden1> bias1_m{}, bias1_v{};
  std::array<float, kHidden2 * kHidden1> weight2_m{}, weight2_v{};
  std::array<float, kHidden2> bias2_m{}, bias2_v{};
  std::array<float, kHidden2> output_m{}, output_v{};
  float output_bias_m = 0;
  float output_bias_v = 0;
  std::uint64_t steps = 0;
};

void adamScalar(float& parameter, float gradient, float& first, float& second,
                float rate, float correction1, float correction2) {
  constexpr float beta1 = 0.9f;
  constexpr float beta2 = 0.999f;
  first = beta1 * first + (1 - beta1) * gradient;
  second = beta2 * second + (1 - beta2) * gradient * gradient;
  parameter -= rate * (first / correction1) /
               (std::sqrt(second / correction2) + 1e-8f);
}

template <std::size_t Size>
void adamArray(std::array<float, Size>& parameter,
               const std::array<float, Size>& gradient,
               std::array<float, Size>& first, std::array<float, Size>& second,
               float rate, float correction1, float correction2) {
  for (std::size_t index = 0; index < Size; ++index) {
    adamScalar(parameter[index], gradient[index], first[index], second[index],
               rate, correction1, correction2);
  }
}

void applyAdam(Network& network, Gradient& gradient, AdamMoments& moments,
               float rate) {
  ++moments.steps;
  const float correction1 = 1.0f - std::pow(0.9f, moments.steps);
  const float correction2 = 1.0f - std::pow(0.999f, moments.steps);
  for (int feature : gradient.touched) {
    const int base = feature * kHidden1;
    for (int hidden = 0; hidden < kHidden1; ++hidden) {
      const int index = base + hidden;
      adamScalar(network.parameters.embedding[index], gradient.embedding[index],
                 moments.embedding_m[index], moments.embedding_v[index], rate,
                 correction1, correction2);
    }
  }
  adamArray(network.parameters.bias1, gradient.bias1, moments.bias1_m,
            moments.bias1_v, rate, correction1, correction2);
  adamArray(network.parameters.weight2, gradient.weight2, moments.weight2_m,
            moments.weight2_v, rate, correction1, correction2);
  adamArray(network.parameters.bias2, gradient.bias2, moments.bias2_m,
            moments.bias2_v, rate, correction1, correction2);
  adamArray(network.parameters.output_weight, gradient.output_weight,
            moments.output_m, moments.output_v, rate, correction1, correction2);
  adamScalar(network.parameters.output_bias, gradient.output_bias,
             moments.output_bias_m, moments.output_bias_v, rate, correction1,
             correction2);
  gradient.releaseTouched();
}

struct Options {
  int training_games = 10'000;
  int probe_games = 64;
  int max_moves = 500;
  int chance_samples = 7;
  int report_every = 1'000;
  int replay_capacity = 200'000;
  int batch_size = 32;
  int n_step = 12;
  int target_every = 1'000;
  std::uint32_t training_seed_start = 0x3d70'0000u;
  std::uint32_t probe_seed_start = 0x4d70'0000u;
  std::uint32_t network_seed = 0x4e4e'5545u;
  float gamma = 0.999f;
  float learning_rate = 0.0003f;
  float replay_ratio = 0.5f;
  float monte_carlo_weight = 0.35f;
  float epsilon_start = 0.0f;
  float epsilon_end = 0.0f;
  int curriculum_epochs = 120;
  float ranking_weight = 20.0f;
  float ranking_margin_scale = 0.25f;
  std::string curriculum;
  std::string checkpoint;
  std::string resume;
};

std::array<float, kBoardSize> actionValues(const Network& network,
                                            const State& source,
                                            const Options& options) {
  const CanonicalState canonical = canonicalize(source);
  const State& state = canonical.state;
  const std::uint32_t hash = observableHash(state);
  std::vector<std::uint32_t> chance_seeds(options.chance_samples);
  const int reveal_offset = static_cast<int>(mix32(hash ^ 0x5354'5241u) % 7u);
  for (int sample = 0; sample < options.chance_samples; ++sample) {
    const std::uint8_t desired_first_reveal = static_cast<std::uint8_t>(
        ((reveal_offset + sample) % kBoardSize) + 1);
    // Rejection-search a deterministic seed whose first reveal is the desired
    // value. Seven samples therefore exactly stratify the first hidden disc.
    // Subsequent reveals remain unbiased pseudo-random draws.
    for (std::uint32_t attempt = 0;; ++attempt) {
      const std::uint32_t candidate = mix32(
          hash ^ (static_cast<std::uint32_t>(sample + 1) * 0xc2b2'ae35u) ^
          (attempt * 0x9e37'79b9u) ^ 0x5245'564cu);
      Mulberry32 probe(candidate);
      if (probe.nextDisc() == desired_first_reveal) {
        chance_seeds[sample] = candidate;
        break;
      }
    }
  }
  std::array<float, kBoardSize> physical_values{};
  physical_values.fill(-std::numeric_limits<float>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    if (!isLegal(state.board, action)) continue;
    double total = 0;
    for (int sample = 0; sample < options.chance_samples; ++sample) {
      // The identical sample stream is used for every legal action (common
      // random numbers). It depends only on the canonical observable state.
      Mulberry32 chance(chance_seeds[sample]);
      MoveResult move;
      if (!playMove(state, action, chance, move)) {
        throw std::logic_error("NNUE evaluator chose an illegal move");
      }
      const double sample_value =
          1.0 + (move.state.game_over
                     ? 0.0
                     : static_cast<double>(options.gamma) *
                           network.value(move.state));
      total += sample_value;
      // With no reveal, all chance samples have the same board and phase. U
      // ignores the sampled future next_disc, so avoid six redundant NNUE
      // evaluations on the overwhelmingly common quiet transition.
      const bool consumed_reveal =
          std::any_of(move.waves.begin(), move.waves.end(),
                      [](const Wave& wave) { return wave.revealed > 0; });
      if (sample == 0 && !consumed_reveal) {
        total = sample_value * options.chance_samples;
        break;
      }
    }
    physical_values[physicalAction(action, canonical.mirrored)] =
        static_cast<float>(total / options.chance_samples);
  }
  return physical_values;
}

int greedyAction(const Network& network, const State& state,
                 const Options& options) {
  const auto values = actionValues(network, state, options);
  const bool mirrored = canonicalize(state).mirrored;
  constexpr std::array<int, kBoardSize> tie_order{{3, 2, 4, 1, 5, 0, 6}};
  int selected_canonical = -1;
  float best = -std::numeric_limits<float>::infinity();
  for (int canonical_column : tie_order) {
    const int physical = physicalAction(canonical_column, mirrored);
    if (!isLegal(state.board, physical)) continue;
    if (selected_canonical < 0 || values[physical] > best) {
      selected_canonical = canonical_column;
      best = values[physical];
    }
  }
  return selected_canonical < 0
             ? -1
             : physicalAction(selected_canonical, mirrored);
}

struct Experience {
  CompactState state;
  CompactState next;
  std::uint16_t steps = 0;
  float monte_carlo = 0;
  std::uint8_t has_monte_carlo = 0;
};

class Replay {
 public:
  explicit Replay(int capacity) : entries_(capacity) {}
  void add(const Experience& experience) {
    entries_[cursor_] = experience;
    cursor_ = (cursor_ + 1) % entries_.size();
    size_ = std::min(size_ + 1, entries_.size());
  }
  const Experience& sample(Rng& random) const {
    return entries_[random.bounded(static_cast<int>(size_))];
  }
  std::size_t size() const { return size_; }
  std::size_t bytes() const { return entries_.size() * sizeof(Experience); }

 private:
  std::vector<Experience> entries_;
  std::size_t cursor_ = 0;
  std::size_t size_ = 0;
};

float discountedSteps(int steps, float gamma) {
  if (steps <= 0) return 0;
  if (std::abs(gamma - 1.0f) < 1e-7f) return static_cast<float>(steps);
  return (1.0f - std::pow(gamma, static_cast<float>(steps))) / (1.0f - gamma);
}

struct Evaluation {
  double mean_score = 0;
  double mean_moves = 0;
  std::int64_t minimum_score = std::numeric_limits<std::int64_t>::max();
  std::int64_t maximum_score = std::numeric_limits<std::int64_t>::min();
  int minimum_moves = std::numeric_limits<int>::max();
  int maximum_moves = std::numeric_limits<int>::min();
  int censored = 0;
};

long peakRssKiB();

Evaluation evaluate(const Network& network, const Options& options) {
  Evaluation result;
  for (int game = 0; game < options.probe_games; ++game) {
    const std::uint32_t seed =
        options.probe_seed_start + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < options.max_moves) {
      const int action = greedyAction(network, state, options);
      MoveResult move;
      if (action < 0 || !playHeadlessMove(state, seed, action, move)) {
        throw std::logic_error("NNUE probe policy chose an illegal move");
      }
    }
    result.mean_score += state.score;
    result.mean_moves += state.moves_played;
    result.minimum_score = std::min(result.minimum_score, state.score);
    result.maximum_score = std::max(result.maximum_score, state.score);
    result.minimum_moves = std::min(result.minimum_moves, state.moves_played);
    result.maximum_moves = std::max(result.maximum_moves, state.moves_played);
    if (!state.game_over) ++result.censored;
  }
  result.mean_score /= options.probe_games;
  result.mean_moves /= options.probe_games;
  return result;
}

int evaluateCheckpoint(const Options& options) {
  if (options.resume.empty() || options.probe_games < 1 ||
      options.max_moves < 1 || options.chance_samples < 1) {
    throw std::invalid_argument(
        "checkpoint evaluation needs --resume and positive probe settings");
  }
  Network network(options.network_seed);
  network.load(options.resume);
  const auto started = std::chrono::steady_clock::now();
  const Evaluation result = evaluate(network, options);
  const double elapsed = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - started)
                             .count();
  std::cout << std::fixed << std::setprecision(3)
            << "NNUE_U_EVALUATE {\"checkpoint\":\"" << options.resume
            << "\",\"seedStart\":" << options.probe_seed_start
            << ",\"games\":" << options.probe_games
            << ",\"chanceSamples\":" << options.chance_samples
            << ",\"meanScore\":" << result.mean_score
            << ",\"meanMoves\":" << result.mean_moves
            << ",\"minimumScore\":" << result.minimum_score
            << ",\"maximumScore\":" << result.maximum_score
            << ",\"minimumMoves\":" << result.minimum_moves
            << ",\"maximumMoves\":" << result.maximum_moves
            << ",\"censored\":" << result.censored
            << ",\"seconds\":" << elapsed
            << ",\"peakRssMiB\":" << peakRssKiB() / 1024.0 << "}\n";
  return 0;
}

long peakRssKiB() {
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
#if defined(__APPLE__)
  return usage.ru_maxrss / 1024;
#else
  return usage.ru_maxrss;
#endif
}

void printProbe(const Evaluation& probe, int games, std::uint64_t transitions,
                double elapsed, const Replay& replay) {
  std::cout << std::fixed << std::setprecision(3)
            << "NNUE_U_PROBE {\"trainingGames\":" << games
            << ",\"transitions\":" << transitions
            << ",\"meanScore\":" << probe.mean_score
            << ",\"meanMoves\":" << probe.mean_moves
            << ",\"minimumScore\":" << probe.minimum_score
            << ",\"maximumScore\":" << probe.maximum_score
            << ",\"minimumMoves\":" << probe.minimum_moves
            << ",\"maximumMoves\":" << probe.maximum_moves
            << ",\"censored\":" << probe.censored
            << ",\"transitionsPerSecond\":"
            << (elapsed > 0 ? transitions / elapsed : 0)
            << ",\"replayMiB\":" << replay.bytes() / 1'048'576.0
            << ",\"peakRssMiB\":" << peakRssKiB() / 1024.0
            << ",\"continueGate\":"
            << (probe.mean_score >= 300'000 ? "true" : "false") << "}\n";
}

void addEpisodeToReplay(const std::vector<State>& trajectory, bool terminal,
                        const Options& options, Replay& replay) {
  const int moves = static_cast<int>(trajectory.size()) - 1;
  for (int start = 0; start < moves; ++start) {
    const int steps = std::min(options.n_step, moves - start);
    const int end = start + steps;
    Experience experience;
    experience.state = compact(trajectory[start]);
    experience.next = compact(trajectory[end]);
    experience.steps = static_cast<std::uint16_t>(steps);
    if (terminal) {
      experience.has_monte_carlo = 1;
      experience.monte_carlo = discountedSteps(moves - start, options.gamma);
    }
    replay.add(experience);
  }
}

float trainBatch(Network& online, const Network& target, Replay& replay,
                 Rng& random, Gradient& gradient, AdamMoments& moments,
                 const Options& options) {
  gradient.clearDense();
  double total_loss = 0;
  for (int item = 0; item < options.batch_size; ++item) {
    const Experience& experience = replay.sample(random);
    const bool terminal = experience.next.terminal;
    float target_value = discountedSteps(experience.steps, options.gamma);
    if (!terminal) {
      target_value +=
          std::pow(options.gamma, static_cast<float>(experience.steps)) *
          target.value(experience.next);
    }
    if (experience.has_monte_carlo) {
      target_value = (1.0f - options.monte_carlo_weight) * target_value +
                     options.monte_carlo_weight * experience.monte_carlo;
    }
    ForwardCache cache;
    const float prediction = online.forward(experience.state, &cache);
    const float error = prediction - target_value;
    const float absolute = std::abs(error);
    const float loss = absolute <= 10.0f
                           ? 0.5f * error * error
                           : 10.0f * (absolute - 5.0f);
    const float derivative =
        std::clamp(error, -10.0f, 10.0f) / options.batch_size;
    backward(online, cache, derivative, gradient);
    total_loss += loss;
  }
  applyAdam(online, gradient, moments, options.learning_rate);
  return static_cast<float>(total_loss / options.batch_size);
}

int train(const Options& options) {
  if (options.training_games < 1 || options.probe_games < 1 ||
      options.max_moves < 1 || options.chance_samples < 1 ||
      options.report_every < 1 || options.replay_capacity < options.batch_size ||
      options.batch_size < 1 || options.n_step < 1 || options.target_every < 1 ||
      options.gamma <= 0 || options.gamma > 1 || options.learning_rate <= 0 ||
      options.replay_ratio < 0 || options.monte_carlo_weight < 0 ||
      options.monte_carlo_weight > 1) {
    throw std::invalid_argument("invalid NNUE training options");
  }
  Network online(options.network_seed);
  if (!options.resume.empty()) online.load(options.resume);
  Network target = online;
  Replay replay(options.replay_capacity);
  Rng training_random(mix32(options.network_seed ^ 0x5452'4149u));
  Gradient gradient;
  AdamMoments moments;
  std::uint64_t transitions = 0;
  std::uint64_t updates = 0;
  double update_credit = 0;
  double rolling_loss = 0;
  int rolling_updates = 0;
  const auto started = std::chrono::steady_clock::now();

  std::cout << "NNUE_U_CONFIG {\"trainingSeedStart\":"
            << options.training_seed_start << ",\"probeSeedStart\":"
            << options.probe_seed_start << ",\"games\":"
            << options.training_games << ",\"chanceSamples\":"
            << options.chance_samples << ",\"gamma\":" << options.gamma
            << ",\"nStep\":" << options.n_step << ",\"replayCapacity\":"
            << options.replay_capacity << ",\"batchSize\":"
            << options.batch_size << ",\"parameterMiB\":"
            << online.parameterBytes() / 1'048'576.0
            << ",\"discIndependentChanceState\":true}\n";
  printProbe(evaluate(online, options), 0, 0, 0, replay);

  for (int game = 0; game < options.training_games; ++game) {
    const std::uint32_t seed =
        options.training_seed_start + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    std::vector<State> trajectory;
    trajectory.reserve(options.max_moves + 1);
    trajectory.push_back(state);
    const float progress = static_cast<float>(game) /
                           std::max(1, options.training_games - 1);
    const float epsilon = options.epsilon_start +
                          (options.epsilon_end - options.epsilon_start) *
                              progress;
    while (!state.game_over && state.moves_played < options.max_moves) {
      int action = -1;
      int legal_count = 0;
      const auto legal = legalColumns(state.board, legal_count);
      if (training_random.unit() < epsilon) {
        action = legal[training_random.bounded(legal_count)];
      } else {
        action = greedyAction(online, state, options);
      }
      MoveResult move;
      if (action < 0 || !playHeadlessMove(state, seed, action, move)) {
        throw std::logic_error("NNUE training policy chose an illegal move");
      }
      trajectory.push_back(state);
      ++transitions;
    }
    addEpisodeToReplay(trajectory, state.game_over, options, replay);
    update_credit +=
        (trajectory.size() - 1) * options.replay_ratio / options.batch_size;
    while (replay.size() >= static_cast<std::size_t>(options.batch_size) &&
           update_credit >= 1.0) {
      rolling_loss += trainBatch(online, target, replay, training_random,
                                 gradient, moments, options);
      ++rolling_updates;
      ++updates;
      update_credit -= 1.0;
      if (updates % options.target_every == 0) target = online;
    }

    const int completed = game + 1;
    if (completed % options.report_every == 0 ||
        completed == options.training_games) {
      const double elapsed = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
      std::cout << "NNUE_U_TRAIN {\"trainingGames\":" << completed
                << ",\"updates\":" << updates << ",\"meanLoss\":"
                << (rolling_updates ? rolling_loss / rolling_updates : 0)
                << ",\"epsilon\":" << epsilon << "}\n";
      printProbe(evaluate(online, options), completed, transitions, elapsed,
                 replay);
      rolling_loss = 0;
      rolling_updates = 0;
      if (!options.checkpoint.empty()) online.save(options.checkpoint);
    }
  }
  return 0;
}

struct CurriculumRecord {
  CompactState state;
  std::uint16_t remaining_moves = 0;
  std::uint8_t source = 0;
  std::int32_t remaining_score = 0;
};

struct CurriculumData {
  std::vector<CurriculumRecord> records;
  std::uint32_t declared_oracle = 0;
  std::uint32_t declared_negative = 0;
};

template <typename Value>
Value readBinary(std::ifstream& input) {
  Value value{};
  input.read(reinterpret_cast<char*>(&value), sizeof(value));
  if (!input) throw std::runtime_error("truncated curriculum dataset");
  return value;
}

CurriculumData loadCurriculum(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not open curriculum dataset");
  std::array<char, 8> magic{};
  input.read(magic.data(), magic.size());
  constexpr std::array<char, 8> expected{{'D', '7', 'C', 'U', 'R', 'R', '1', 0}};
  const auto version = readBinary<std::uint32_t>(input);
  const auto count = readBinary<std::uint32_t>(input);
  CurriculumData data;
  data.declared_oracle = readBinary<std::uint32_t>(input);
  data.declared_negative = readBinary<std::uint32_t>(input);
  const auto record_bytes = readBinary<std::uint32_t>(input);
  constexpr std::uint32_t expected_record_bytes = kCellCount + 1 + 2 + 1 + 4;
  if (magic != expected || version != 1 ||
      record_bytes != expected_record_bytes || count < 1 || count > 100'000 ||
      data.declared_oracle + data.declared_negative != count) {
    throw std::runtime_error("invalid curriculum dataset header");
  }
  data.records.reserve(count);
  std::uint32_t observed_oracle = 0;
  for (std::uint32_t index = 0; index < count; ++index) {
    CurriculumRecord record;
    input.read(reinterpret_cast<char*>(record.state.board.data()), kCellCount);
    record.state.moves_remaining = readBinary<std::uint8_t>(input);
    record.remaining_moves = readBinary<std::uint16_t>(input);
    record.source = readBinary<std::uint8_t>(input);
    record.remaining_score = readBinary<std::int32_t>(input);
    record.state.terminal = 0;
    if (!input || record.state.moves_remaining < 1 ||
        record.state.moves_remaining > kMovesPerLevel ||
        record.remaining_moves < 1 || record.remaining_moves > 5'000 ||
        record.source < 1 || record.source > 3 ||
        std::any_of(record.state.board.begin(), record.state.board.end(),
                    [](std::uint8_t cell) { return cell > kCracked; })) {
      throw std::runtime_error("invalid curriculum record");
    }
    observed_oracle += record.source == 1;
    data.records.push_back(record);
  }
  if (observed_oracle != data.declared_oracle ||
      count - observed_oracle != data.declared_negative ||
      input.peek() != std::ifstream::traits_type::eof()) {
    throw std::runtime_error("curriculum counts or trailing bytes mismatch");
  }
  return data;
}

float huberLoss(float error, float threshold) {
  const float absolute = std::abs(error);
  return absolute <= threshold
             ? 0.5f * error * error
             : threshold * (absolute - 0.5f * threshold);
}

float softplusNegative(float value) {
  // log(1 + exp(-value)), evaluated without overflow.
  return value >= 0 ? std::log1p(std::exp(-value))
                    : -value + std::log1p(std::exp(value));
}

struct CurriculumMetrics {
  double oracle_mae = 0;
  double negative_mae = 0;
  double mean_rank_gap = 0;
  double ranking_accuracy = 0;
  int oracle_count = 0;
  int negative_count = 0;
  int ranking_pairs = 0;
};

CurriculumMetrics curriculumMetrics(
    const Network& model, const CurriculumData& data,
    const std::array<std::vector<int>, kMovesPerLevel + 1>& negatives_by_phase) {
  CurriculumMetrics result;
  for (int index = 0; index < static_cast<int>(data.records.size()); ++index) {
    const auto& record = data.records[index];
    const float prediction = model.value(record.state);
    if (record.source == 1) {
      result.oracle_mae += std::abs(prediction - record.remaining_moves);
      ++result.oracle_count;
      const auto& negatives = negatives_by_phase[record.state.moves_remaining];
      if (!negatives.empty()) {
        const auto& negative =
            data.records[negatives[index % negatives.size()]];
        if (record.remaining_moves > negative.remaining_moves + 25) {
          const float gap = prediction - model.value(negative.state);
          result.mean_rank_gap += gap;
          result.ranking_accuracy += gap > 0;
          ++result.ranking_pairs;
        }
      }
    } else {
      result.negative_mae += std::abs(prediction - record.remaining_moves);
      ++result.negative_count;
    }
  }
  if (result.oracle_count) result.oracle_mae /= result.oracle_count;
  if (result.negative_count) result.negative_mae /= result.negative_count;
  if (result.ranking_pairs) {
    result.mean_rank_gap /= result.ranking_pairs;
    result.ranking_accuracy /= result.ranking_pairs;
  }
  return result;
}

int trainCurriculum(const Options& options) {
  if (options.curriculum.empty() || options.curriculum_epochs < 1 ||
      options.batch_size < 1 || options.learning_rate <= 0 ||
      options.ranking_weight < 0 || options.ranking_margin_scale < 0 ||
      options.probe_games < 1 || options.max_moves < 1 ||
      options.chance_samples < 1) {
    throw std::invalid_argument("invalid curriculum training options");
  }
  const std::uint64_t probe_end =
      static_cast<std::uint64_t>(options.probe_seed_start) +
      options.probe_games - 1;
  if (options.probe_seed_start < 0x4d70'0000u ||
      probe_end >= 0x5d70'0000ull) {
    throw std::invalid_argument("curriculum probe outside 0x4d70 partition");
  }
  const CurriculumData data = loadCurriculum(options.curriculum);
  std::vector<int> positives;
  std::array<std::vector<int>, kMovesPerLevel + 1> negatives_by_phase;
  int combined_count = 0;
  int phase_count = 0;
  for (int index = 0; index < static_cast<int>(data.records.size()); ++index) {
    const auto& record = data.records[index];
    if (record.source == 1) {
      positives.push_back(index);
    } else {
      negatives_by_phase[record.state.moves_remaining].push_back(index);
      combined_count += record.source == 2;
      phase_count += record.source == 3;
    }
  }
  if (positives.empty()) throw std::runtime_error("no oracle curriculum states");
  for (int phase = 1; phase <= kMovesPerLevel; ++phase) {
    if (negatives_by_phase[phase].empty()) {
      throw std::runtime_error("curriculum lacks a matched negative phase");
    }
  }

  Network model(options.network_seed);
  if (!options.resume.empty()) model.load(options.resume);
  Gradient gradient;
  AdamMoments moments;
  Rng random(mix32(options.network_seed ^ 0x4355'5252u));
  std::vector<int> order = positives;
  std::uint64_t updates = 0;
  const auto started = std::chrono::steady_clock::now();
  std::cout << "CURRICULUM_CONFIG {\"records\":" << data.records.size()
            << ",\"oracleStates\":" << positives.size()
            << ",\"combinedStates\":" << combined_count
            << ",\"phaseStates\":" << phase_count
            << ",\"epochs\":" << options.curriculum_epochs
            << ",\"batchSize\":" << options.batch_size
            << ",\"learningRate\":" << options.learning_rate
            << ",\"rankingWeight\":" << options.ranking_weight
            << ",\"rankingMarginScale\":"
            << options.ranking_margin_scale
            << ",\"parameterMiB\":"
            << model.parameterBytes() / 1'048'576.0
            << ",\"discIndependentChanceState\":true,"
               "\"probeAccessBeforeFreeze\":false}\n";

  for (int epoch = 0; epoch < options.curriculum_epochs; ++epoch) {
    for (int index = static_cast<int>(order.size()) - 1; index > 0; --index) {
      std::swap(order[index], order[random.bounded(index + 1)]);
    }
    double regression_loss = 0;
    double ranking_loss = 0;
    int regression_examples = 0;
    int ranking_pairs = 0;
    for (int start = 0; start < static_cast<int>(order.size());
         start += options.batch_size) {
      const int count =
          std::min(options.batch_size, static_cast<int>(order.size()) - start);
      gradient.clearDense();
      for (int item = 0; item < count; ++item) {
        const auto& positive = data.records[order[start + item]];
        const auto& bucket =
            negatives_by_phase[positive.state.moves_remaining];
        const auto& negative =
            data.records[bucket[random.bounded(static_cast<int>(bucket.size()))]];
        ForwardCache positive_cache;
        ForwardCache negative_cache;
        const float positive_value =
            model.forward(positive.state, &positive_cache);
        const float negative_value =
            model.forward(negative.state, &negative_cache);
        const float positive_error =
            positive_value - positive.remaining_moves;
        const float negative_error =
            negative_value - negative.remaining_moves;
        constexpr float huber_threshold = 20.0f;
        backward(model, positive_cache,
                 std::clamp(positive_error, -huber_threshold,
                            huber_threshold) /
                     (2.0f * count),
                 gradient);
        backward(model, negative_cache,
                 std::clamp(negative_error, -huber_threshold,
                            huber_threshold) /
                     (2.0f * count),
                 gradient);
        regression_loss += huberLoss(positive_error, huber_threshold) +
                           huberLoss(negative_error, huber_threshold);
        regression_examples += 2;

        const int lifetime_difference =
            positive.remaining_moves - negative.remaining_moves;
        if (lifetime_difference > 25 && options.ranking_weight > 0) {
          const float desired_margin = std::min(
              100.0f, options.ranking_margin_scale * lifetime_difference);
          constexpr float temperature = 20.0f;
          const float normalized_gap =
              (positive_value - negative_value - desired_margin) / temperature;
          const float pair_derivative =
              -options.ranking_weight /
              (temperature * count * (1.0f + std::exp(normalized_gap)));
          backward(model, positive_cache, pair_derivative, gradient);
          backward(model, negative_cache, -pair_derivative, gradient);
          ranking_loss +=
              options.ranking_weight * softplusNegative(normalized_gap);
          ++ranking_pairs;
        }
      }
      applyAdam(model, gradient, moments, options.learning_rate);
      ++updates;
    }
    const int completed = epoch + 1;
    if (completed == 1 || completed % 10 == 0 ||
        completed == options.curriculum_epochs) {
      const auto metrics = curriculumMetrics(model, data, negatives_by_phase);
      std::cout << std::fixed << std::setprecision(3)
                << "CURRICULUM_TRAIN {\"epoch\":" << completed
                << ",\"updates\":" << updates
                << ",\"regressionLoss\":"
                << (regression_examples
                        ? regression_loss / regression_examples
                        : 0)
                << ",\"rankingLoss\":"
                << (ranking_pairs ? ranking_loss / ranking_pairs : 0)
                << ",\"oracleMae\":" << metrics.oracle_mae
                << ",\"negativeMae\":" << metrics.negative_mae
                << ",\"rankingAccuracy\":" << metrics.ranking_accuracy
                << ",\"meanRankGap\":" << metrics.mean_rank_gap << "}\n";
    }
  }
  if (!options.checkpoint.empty()) model.save(options.checkpoint);

  // Read probe data once, after locking all weights.
  Options policy_options = options;
  policy_options.gamma = 1.0f;
  const Evaluation probe = evaluate(model, policy_options);
  const double elapsed = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - started)
                             .count();
  std::cout << std::fixed << std::setprecision(3)
            << "CURRICULUM_FROZEN_PROBE {\"probeSeedStart\":"
            << policy_options.probe_seed_start
            << ",\"probeGames\":" << policy_options.probe_games
            << ",\"meanScore\":" << probe.mean_score
            << ",\"meanMoves\":" << probe.mean_moves
            << ",\"minimumScore\":" << probe.minimum_score
            << ",\"maximumScore\":" << probe.maximum_score
            << ",\"minimumMoves\":" << probe.minimum_moves
            << ",\"maximumMoves\":" << probe.maximum_moves
            << ",\"censored\":" << probe.censored
            << ",\"elapsedSeconds\":" << elapsed
            << ",\"peakRssMiB\":" << peakRssKiB() / 1024.0
            << ",\"continueGate\":"
            << (probe.mean_score >= 300'000 ? "true" : "false") << "}\n";
  return 0;
}

double squaredLoss(Network& network, const CompactState& state, float target) {
  const double error = static_cast<double>(network.value(state)) - target;
  return 0.5 * error * error;
}

bool gradientCheck(std::ostream& output) {
  Network network(0x1234'5678u);
  State state = initialHeadlessState(0x2d70'0042u);
  for (int action : {3, 1, 5, 2, 4, 0, 6, 3}) {
    MoveResult move;
    if (!playHeadlessMove(state, 0x2d70'0042u, action, move)) break;
  }
  const CompactState input = compact(state);
  constexpr float target = 43.25f;
  ForwardCache cache;
  const float prediction = network.forward(input, &cache);
  Gradient gradient;
  gradient.clearDense();
  backward(network, cache, prediction - target, gradient);

  struct Probe {
    float* parameter;
    float analytic;
  };
  int best_output = 0;
  for (int index = 1; index < kHidden2; ++index) {
    if (std::abs(gradient.output_weight[index]) >
        std::abs(gradient.output_weight[best_output])) best_output = index;
  }
  int best_w2 = 0;
  for (int index = 1; index < kHidden2 * kHidden1; ++index) {
    if (std::abs(gradient.weight2[index]) >
        std::abs(gradient.weight2[best_w2])) best_w2 = index;
  }
  int best_b1 = 0;
  for (int index = 1; index < kHidden1; ++index) {
    if (std::abs(gradient.bias1[index]) > std::abs(gradient.bias1[best_b1]))
      best_b1 = index;
  }
  const int feature = cache.features.ids[0];
  const int embedding_index = feature * kHidden1 + best_b1;
  std::array<Probe, 5> probes{{
      {&network.parameters.output_bias, gradient.output_bias},
      {&network.parameters.output_weight[best_output],
       gradient.output_weight[best_output]},
      {&network.parameters.weight2[best_w2], gradient.weight2[best_w2]},
      {&network.parameters.bias1[best_b1], gradient.bias1[best_b1]},
      {&network.parameters.embedding[embedding_index],
       gradient.embedding[embedding_index]},
  }};
  float maximum_relative_error = 0;
  // Float-valued sparse accumulators need a slightly looser check than a
  // double network: 0.01 is large enough to beat output quantization and small
  // enough not to cross the selected state's leaky-ReLU branches.
  constexpr float epsilon = 0.01f;
  for (Probe& probe : probes) {
    const float original = *probe.parameter;
    *probe.parameter = original + epsilon;
    const double plus = squaredLoss(network, input, target);
    *probe.parameter = original - epsilon;
    const double minus = squaredLoss(network, input, target);
    *probe.parameter = original;
    const float numerical = static_cast<float>((plus - minus) / (2 * epsilon));
    const float scale = std::max(1e-3f, std::abs(numerical) +
                                           std::abs(probe.analytic));
    maximum_relative_error =
        std::max(maximum_relative_error,
                 std::abs(numerical - probe.analytic) / scale);
  }
  gradient.releaseTouched();
  const bool passed = maximum_relative_error < 0.04f;
  output << std::setprecision(8)
         << "NNUE_U_GRADIENT {\"passed\":" << (passed ? "true" : "false")
         << ",\"maximumRelativeError\":" << maximum_relative_error
         << ",\"probes\":" << probes.size() << "}\n";
  return passed;
}

bool selfTest(std::ostream& output) {
  Options options;
  options.chance_samples = 7;
  Network network(0x1357'2468u);
  State state = initialHeadlessState(0x2d70'0011u);
  for (int action : {3, 1, 5, 2, 4, 0}) {
    MoveResult move;
    if (!playHeadlessMove(state, 0x2d70'0011u, action, move)) break;
  }
  State mirrored = state;
  mirrored.board = mirrorBoard(state.board);
  const float value = network.value(state);
  const float mirror_value = network.value(mirrored);
  const auto actions = actionValues(network, state, options);
  const auto mirror_actions = actionValues(network, mirrored, options);
  const auto repeated = actionValues(network, state, options);
  bool action_mirror = true;
  for (int column = 0; column < kBoardSize; ++column) {
    const float left = actions[column];
    const float right = mirror_actions[kBoardSize - 1 - column];
    if (std::isfinite(left) != std::isfinite(right) ||
        (std::isfinite(left) && std::abs(left - right) > 1e-5f)) {
      action_mirror = false;
    }
  }
  const bool deterministic = actions == repeated;
  const bool mapped_tie = greedyAction(network, state, options) ==
                          kBoardSize - 1 -
                              greedyAction(network, mirrored, options);
  // U must be identical when only the visible disc changes. The disc still
  // affects action transitions, which is separately covered by actionValues.
  State other_disc = state;
  other_disc.next_disc = static_cast<std::uint8_t>(state.next_disc % 7 + 1);
  const bool disc_independent = network.value(state) == network.value(other_disc);
  const bool passed = std::abs(value - mirror_value) < 1e-6f && action_mirror &&
                      deterministic && mapped_tie && disc_independent;
  output << "NNUE_U_SELF_TEST {\"passed\":" << (passed ? "true" : "false")
         << ",\"mirrorValue\":"
         << (std::abs(value - mirror_value) < 1e-6f ? "true" : "false")
         << ",\"mirrorActions\":" << (action_mirror ? "true" : "false")
         << ",\"mappedTieBehavior\":" << (mapped_tie ? "true" : "false")
         << ",\"seedBlindDeterministic\":"
         << (deterministic ? "true" : "false")
         << ",\"discIndependent\":" << (disc_independent ? "true" : "false")
         << ",\"features\":" << kFeatureCount
         << ",\"activeFeatures\":" << kMaxFeatures << "}\n";
  return passed;
}

std::string valueAfter(int argc, char** argv, const std::string& name,
                       const std::string& fallback) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == name) return argv[index + 1];
  }
  return fallback;
}

int parseInt(const std::string& value, const char* name) {
  std::size_t consumed = 0;
  const long parsed = std::stol(value, &consumed, 0);
  if (consumed != value.size() || parsed < std::numeric_limits<int>::min() ||
      parsed > std::numeric_limits<int>::max()) {
    throw std::invalid_argument(std::string("invalid ") + name);
  }
  return static_cast<int>(parsed);
}

std::uint32_t parseUint32(const std::string& value, const char* name) {
  std::size_t consumed = 0;
  const unsigned long parsed = std::stoul(value, &consumed, 0);
  if (consumed != value.size() ||
      parsed > std::numeric_limits<std::uint32_t>::max()) {
    throw std::invalid_argument(std::string("invalid ") + name);
  }
  return static_cast<std::uint32_t>(parsed);
}

float parseFloat(const std::string& value, const char* name) {
  std::size_t consumed = 0;
  const float parsed = std::stof(value, &consumed);
  if (consumed != value.size() || !std::isfinite(parsed)) {
    throw std::invalid_argument(std::string("invalid ") + name);
  }
  return parsed;
}

Options parseOptions(int argc, char** argv) {
  Options options;
  options.training_games =
      parseInt(valueAfter(argc, argv, "--games", "10000"), "--games");
  options.probe_games = parseInt(
      valueAfter(argc, argv, "--probe-games", "64"), "--probe-games");
  options.max_moves = parseInt(
      valueAfter(argc, argv, "--max-moves", "500"), "--max-moves");
  options.chance_samples = parseInt(
      valueAfter(argc, argv, "--chance-samples", "7"), "--chance-samples");
  options.report_every = parseInt(
      valueAfter(argc, argv, "--report-every", "1000"), "--report-every");
  options.replay_capacity = parseInt(
      valueAfter(argc, argv, "--replay-capacity", "200000"),
      "--replay-capacity");
  options.batch_size = parseInt(
      valueAfter(argc, argv, "--batch-size", "32"), "--batch-size");
  options.n_step =
      parseInt(valueAfter(argc, argv, "--n-step", "12"), "--n-step");
  options.target_every = parseInt(
      valueAfter(argc, argv, "--target-every", "1000"), "--target-every");
  options.training_seed_start = parseUint32(
      valueAfter(argc, argv, "--training-seed-start", "0x3d700000"),
      "--training-seed-start");
  options.probe_seed_start = parseUint32(
      valueAfter(argc, argv, "--probe-seed-start", "0x4d700000"),
      "--probe-seed-start");
  options.network_seed = parseUint32(
      valueAfter(argc, argv, "--network-seed", "0x4e4e5545"),
      "--network-seed");
  options.gamma =
      parseFloat(valueAfter(argc, argv, "--gamma", "0.999"), "--gamma");
  options.learning_rate = parseFloat(
      valueAfter(argc, argv, "--learning-rate", "0.0003"),
      "--learning-rate");
  options.replay_ratio = parseFloat(
      valueAfter(argc, argv, "--replay-ratio", "0.5"), "--replay-ratio");
  options.monte_carlo_weight = parseFloat(
      valueAfter(argc, argv, "--mc-weight", "0.35"), "--mc-weight");
  options.epsilon_start = parseFloat(
      valueAfter(argc, argv, "--epsilon-start", "0"), "--epsilon-start");
  options.epsilon_end = parseFloat(
      valueAfter(argc, argv, "--epsilon-end", "0"), "--epsilon-end");
  options.curriculum_epochs = parseInt(
      valueAfter(argc, argv, "--epochs", "120"), "--epochs");
  options.ranking_weight = parseFloat(
      valueAfter(argc, argv, "--ranking-weight", "20"),
      "--ranking-weight");
  options.ranking_margin_scale = parseFloat(
      valueAfter(argc, argv, "--ranking-margin-scale", "0.25"),
      "--ranking-margin-scale");
  options.curriculum = valueAfter(argc, argv, "--curriculum", "");
  options.checkpoint = valueAfter(argc, argv, "--checkpoint", "");
  options.resume = valueAfter(argc, argv, "--resume", "");
  return options;
}

}  // namespace drop7::nnue_value

int main(int argc, char** argv) {
  try {
    std::cout.setf(std::ios::unitbuf);
    if (argc < 2) {
      std::cerr
          << "usage: drop7_nnue_value --self-test | --gradient-check | --train "
             "| --evaluate "
             "| --train-curriculum "
             "[--games N] [--probe-games N] [--chance-samples N] ...\n";
      return 2;
    }
    const std::string mode = argv[1];
    if (mode == "--self-test") {
      return drop7::nnue_value::selfTest(std::cout) ? 0 : 1;
    }
    if (mode == "--gradient-check") {
      return drop7::nnue_value::gradientCheck(std::cout) ? 0 : 1;
    }
    if (mode == "--train") {
      return drop7::nnue_value::train(
          drop7::nnue_value::parseOptions(argc, argv));
    }
    if (mode == "--evaluate") {
      return drop7::nnue_value::evaluateCheckpoint(
          drop7::nnue_value::parseOptions(argc, argv));
    }
    if (mode == "--train-curriculum") {
      return drop7::nnue_value::trainCurriculum(
          drop7::nnue_value::parseOptions(argc, argv));
    }
    throw std::invalid_argument("unknown mode: " + mode);
  } catch (const std::exception& error) {
    std::cerr << "drop7_nnue_value: " << error.what() << '\n';
    return 1;
  }
}
