#pragma once

#include "engine.hpp"

#include <algorithm>
#include <array>
#include <atomic>
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
#include <thread>
#include <utility>
#include <vector>

namespace drop7::ppo {

constexpr int kBoardCategories = 10;
constexpr int kBoardEmbeddingSize = 4;
constexpr int kDiscCategories = 8;
constexpr int kDiscEmbeddingSize = 4;
constexpr int kScalarCount = 13;
constexpr int kInputSize =
    kCellCount * kBoardEmbeddingSize + kDiscEmbeddingSize + kScalarCount;
constexpr int kHidden1 = 32;
constexpr int kHidden2 = 32;
constexpr int kActions = kBoardSize;

struct Layout {
  static constexpr int board_embedding = 0;
  static constexpr int disc_embedding =
      board_embedding + kBoardCategories * kBoardEmbeddingSize;
  static constexpr int w1 =
      disc_embedding + kDiscCategories * kDiscEmbeddingSize;
  static constexpr int b1 = w1 + kHidden1 * kInputSize;
  static constexpr int w2 = b1 + kHidden1;
  static constexpr int b2 = w2 + kHidden2 * kHidden1;
  static constexpr int policy_w = b2 + kHidden2;
  static constexpr int policy_b = policy_w + kActions * kHidden2;
  static constexpr int value_w = policy_b + kActions;
  static constexpr int value_b = value_w + kHidden2;
  static constexpr int count = value_b + 1;
};

static_assert(kInputSize == 213);
static_assert(Layout::count == 8240);

struct Observation {
  Board board{};
  std::uint8_t next_disc = 1;
  std::array<float, kScalarCount> scalars{};
  std::uint8_t legal_mask = 0;
};

struct CanonicalObservation {
  Observation observation{};
  bool mirrored = false;
};

inline bool mirroredBoardIsSmaller(const Board& board) {
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      const std::uint8_t forward = board[indexOf(row, column)];
      const std::uint8_t mirrored =
          board[indexOf(row, kBoardSize - 1 - column)];
      if (mirrored < forward) return true;
      if (mirrored > forward) return false;
    }
  }
  return false;
}

inline CanonicalObservation observeCanonical(const State& state) {
  CanonicalObservation canonical;
  canonical.mirrored = mirroredBoardIsSmaller(state.board);
  Observation observation;
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      const int source_column =
          canonical.mirrored ? kBoardSize - 1 - column : column;
      observation.board[indexOf(row, column)] =
          state.board[indexOf(row, source_column)];
    }
  }
  observation.next_disc = state.next_disc;
  int occupancy = 0;
  int covered = 0;
  std::array<int, kBoardSize> heights{};
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      const std::uint8_t cell = observation.board[indexOf(row, column)];
      if (cell == kEmpty) continue;
      ++occupancy;
      ++heights[column];
      if (cell == kSolid || cell == kCracked) ++covered;
    }
  }
  observation.scalars[0] =
      static_cast<float>(state.moves_remaining) / kMovesPerLevel;
  observation.scalars[1] =
      std::min(20, state.level) / 20.0f;
  observation.scalars[2] =
      std::min(200, state.moves_played) / 200.0f;
  observation.scalars[3] = occupancy / static_cast<float>(kCellCount);
  observation.scalars[4] = covered / static_cast<float>(kCellCount);
  observation.scalars[5] = static_cast<float>(
      std::min(1.0, std::log1p(static_cast<double>(std::max<std::int64_t>(
                        0, state.score))) /
                        std::log1p(2'000'000.0)));
  for (int column = 0; column < kBoardSize; ++column) {
    observation.scalars[6 + column] = heights[column] / 7.0f;
    if (observation.board[column] == kEmpty) {
      observation.legal_mask |= static_cast<std::uint8_t>(1u << column);
    }
  }
  canonical.observation = observation;
  return canonical;
}

inline Observation observe(const State& state) {
  return observeCanonical(state).observation;
}

inline int physicalAction(int canonical_action, bool mirrored) {
  return mirrored ? kBoardSize - 1 - canonical_action : canonical_action;
}

struct ForwardCache {
  std::array<float, kInputSize> input{};
  std::array<float, kHidden1> hidden1{};
  std::array<float, kHidden2> hidden2{};
  std::array<float, kActions> probabilities{};
  float value = 0;
};

class Network {
 public:
  explicit Network(std::uint32_t seed = 0xd707'c0deu)
      : parameters_(Layout::count), first_moment_(Layout::count),
        second_moment_(Layout::count) {
    Mulberry32 random(seed);
    initializeUniform(random, Layout::board_embedding,
                      kBoardCategories * kBoardEmbeddingSize, 0.12f);
    initializeUniform(random, Layout::disc_embedding,
                      kDiscCategories * kDiscEmbeddingSize, 0.12f);
    initializeXavier(random, Layout::w1, kHidden1, kInputSize);
    initializeXavier(random, Layout::w2, kHidden2, kHidden1);
    initializeUniform(random, Layout::policy_w, kActions * kHidden2, 0.03f);
    initializeUniform(random, Layout::value_w, kHidden2, 0.03f);
  }

  ForwardCache forward(const Observation& observation) const {
    ForwardCache cache;
    int input_index = 0;
    for (std::uint8_t cell : observation.board) {
      for (int embedding = 0; embedding < kBoardEmbeddingSize; ++embedding) {
        cache.input[input_index++] = parameters_[
            Layout::board_embedding + cell * kBoardEmbeddingSize + embedding];
      }
    }
    for (int embedding = 0; embedding < kDiscEmbeddingSize; ++embedding) {
      cache.input[input_index++] =
          parameters_[Layout::disc_embedding +
                      observation.next_disc * kDiscEmbeddingSize + embedding];
    }
    for (float scalar : observation.scalars) cache.input[input_index++] = scalar;

    for (int output = 0; output < kHidden1; ++output) {
      float total = parameters_[Layout::b1 + output];
      const int weight_offset = Layout::w1 + output * kInputSize;
      for (int input = 0; input < kInputSize; ++input) {
        total += parameters_[weight_offset + input] * cache.input[input];
      }
      cache.hidden1[output] = std::tanh(total);
    }
    for (int output = 0; output < kHidden2; ++output) {
      float total = parameters_[Layout::b2 + output];
      const int weight_offset = Layout::w2 + output * kHidden1;
      for (int input = 0; input < kHidden1; ++input) {
        total += parameters_[weight_offset + input] * cache.hidden1[input];
      }
      cache.hidden2[output] = std::tanh(total);
    }

    std::array<float, kActions> logits{};
    float maximum = -std::numeric_limits<float>::infinity();
    int legal_count = 0;
    for (int action = 0; action < kActions; ++action) {
      if ((observation.legal_mask & (1u << action)) == 0) {
        logits[action] = -std::numeric_limits<float>::infinity();
        continue;
      }
      ++legal_count;
      float total = parameters_[Layout::policy_b + action];
      const int weight_offset = Layout::policy_w + action * kHidden2;
      for (int input = 0; input < kHidden2; ++input) {
        total += parameters_[weight_offset + input] * cache.hidden2[input];
      }
      logits[action] = total;
      maximum = std::max(maximum, total);
    }
    if (legal_count == 0) throw std::runtime_error("policy observed no legal move");
    float denominator = 0;
    for (int action = 0; action < kActions; ++action) {
      if ((observation.legal_mask & (1u << action)) == 0) continue;
      cache.probabilities[action] = std::exp(logits[action] - maximum);
      denominator += cache.probabilities[action];
    }
    for (float& probability : cache.probabilities) probability /= denominator;

    cache.value = parameters_[Layout::value_b];
    for (int input = 0; input < kHidden2; ++input) {
      cache.value += parameters_[Layout::value_w + input] * cache.hidden2[input];
    }
    return cache;
  }

  int greedyAction(const Observation& observation) const {
    const auto result = forward(observation);
    int selected = -1;
    float best = -1;
    for (int action = 0; action < kActions; ++action) {
      if (result.probabilities[action] > best) {
        best = result.probabilities[action];
        selected = action;
      }
    }
    return selected;
  }

  int sampleAction(const ForwardCache& result, Mulberry32& random) const {
    const double sample = random.nextUnit();
    double cumulative = 0;
    int fallback = -1;
    for (int action = 0; action < kActions; ++action) {
      if (result.probabilities[action] <= 0) continue;
      fallback = action;
      cumulative += result.probabilities[action];
      if (sample < cumulative) return action;
    }
    return fallback;
  }

  std::vector<float> zeroGradient() const {
    return std::vector<float>(Layout::count, 0.0f);
  }

  void accumulateGradient(const Observation& observation,
                          const ForwardCache& cache, int action,
                          float policy_coefficient, float value_derivative,
                          float entropy_coefficient,
                          std::vector<float>& gradient) const {
    std::array<float, kActions> logits_gradient{};
    float entropy = 0;
    for (int candidate = 0; candidate < kActions; ++candidate) {
      const float probability = cache.probabilities[candidate];
      if (probability > 0) entropy -= probability * std::log(probability);
    }
    for (int candidate = 0; candidate < kActions; ++candidate) {
      const float probability = cache.probabilities[candidate];
      if (probability <= 0) continue;
      logits_gradient[candidate] =
          policy_coefficient *
              ((candidate == action ? 1.0f : 0.0f) - probability) +
          entropy_coefficient * probability *
              (std::log(probability) + entropy);
    }

    std::array<float, kHidden2> hidden2_gradient{};
    for (int candidate = 0; candidate < kActions; ++candidate) {
      const float derivative = logits_gradient[candidate];
      gradient[Layout::policy_b + candidate] += derivative;
      const int weight_offset = Layout::policy_w + candidate * kHidden2;
      for (int input = 0; input < kHidden2; ++input) {
        gradient[weight_offset + input] += derivative * cache.hidden2[input];
        hidden2_gradient[input] += parameters_[weight_offset + input] * derivative;
      }
    }
    gradient[Layout::value_b] += value_derivative;
    for (int input = 0; input < kHidden2; ++input) {
      gradient[Layout::value_w + input] +=
          value_derivative * cache.hidden2[input];
      hidden2_gradient[input] +=
          parameters_[Layout::value_w + input] * value_derivative;
    }

    std::array<float, kHidden1> hidden1_gradient{};
    for (int output = 0; output < kHidden2; ++output) {
      const float derivative = hidden2_gradient[output] *
                               (1.0f - cache.hidden2[output] *
                                           cache.hidden2[output]);
      gradient[Layout::b2 + output] += derivative;
      const int weight_offset = Layout::w2 + output * kHidden1;
      for (int input = 0; input < kHidden1; ++input) {
        gradient[weight_offset + input] += derivative * cache.hidden1[input];
        hidden1_gradient[input] += parameters_[weight_offset + input] * derivative;
      }
    }

    std::array<float, kInputSize> input_gradient{};
    for (int output = 0; output < kHidden1; ++output) {
      const float derivative = hidden1_gradient[output] *
                               (1.0f - cache.hidden1[output] *
                                           cache.hidden1[output]);
      gradient[Layout::b1 + output] += derivative;
      const int weight_offset = Layout::w1 + output * kInputSize;
      for (int input = 0; input < kInputSize; ++input) {
        gradient[weight_offset + input] += derivative * cache.input[input];
        input_gradient[input] += parameters_[weight_offset + input] * derivative;
      }
    }

    int input_index = 0;
    for (std::uint8_t cell : observation.board) {
      for (int embedding = 0; embedding < kBoardEmbeddingSize; ++embedding) {
        gradient[Layout::board_embedding + cell * kBoardEmbeddingSize +
                 embedding] += input_gradient[input_index++];
      }
    }
    for (int embedding = 0; embedding < kDiscEmbeddingSize; ++embedding) {
      gradient[Layout::disc_embedding +
               observation.next_disc * kDiscEmbeddingSize + embedding] +=
          input_gradient[input_index++];
    }
  }

  void applyAdam(std::vector<float>& gradient, float learning_rate,
                 float maximum_norm) {
    double squared_norm = 0;
    for (float value : gradient) squared_norm += value * value;
    const double norm = std::sqrt(squared_norm);
    const float scale = norm > maximum_norm
                            ? static_cast<float>(maximum_norm / norm)
                            : 1.0f;
    ++adam_step_;
    constexpr float beta1 = 0.9f;
    constexpr float beta2 = 0.999f;
    constexpr float epsilon = 1e-8f;
    const float first_correction =
        1.0f - std::pow(beta1, static_cast<float>(adam_step_));
    const float second_correction =
        1.0f - std::pow(beta2, static_cast<float>(adam_step_));
    for (int index = 0; index < Layout::count; ++index) {
      const float value = gradient[index] * scale;
      first_moment_[index] = beta1 * first_moment_[index] + (1 - beta1) * value;
      second_moment_[index] =
          beta2 * second_moment_[index] + (1 - beta2) * value * value;
      const float corrected_first = first_moment_[index] / first_correction;
      const float corrected_second = second_moment_[index] / second_correction;
      parameters_[index] -= learning_rate * corrected_first /
                            (std::sqrt(corrected_second) + epsilon);
    }
  }

  const std::vector<float>& parameters() const { return parameters_; }

  float parameter(int index) const { return parameters_.at(index); }

  void setParameter(int index, float value) { parameters_.at(index) = value; }

 private:
  void initializeUniform(Mulberry32& random, int offset, int count,
                         float radius) {
    for (int index = 0; index < count; ++index) {
      parameters_[offset + index] =
          static_cast<float>((random.nextUnit() * 2.0 - 1.0) * radius);
    }
  }

  void initializeXavier(Mulberry32& random, int offset, int outputs,
                        int inputs) {
    const float radius = std::sqrt(6.0f / (inputs + outputs));
    initializeUniform(random, offset, outputs * inputs, radius);
  }

  std::vector<float> parameters_;
  std::vector<float> first_moment_;
  std::vector<float> second_moment_;
  std::uint64_t adam_step_ = 0;
};

struct Sample {
  Observation observation{};
  int action = 0;
  float old_log_probability = 0;
  float old_value = 0;
  float reward = 0;
  bool terminal = false;
  float advantage = 0;
  float return_value = 0;
};

struct Collection {
  std::vector<Sample> samples;
  std::vector<std::int64_t> scores;
  std::vector<int> moves;
};

struct TrainingOptions {
  int iterations = 20;
  int episodes_per_iteration = 512;
  int threads = 8;
  int max_moves = 500;
  int epochs = 4;
  int minibatch_size = 512;
  int probe_games = 64;
  int probe_every = 1;
  // These are intentionally disjoint from the 0x7d70..0xd700 held-out
  // validation/final ranges used by the TypeScript experiments.
  std::uint32_t training_seed_start = 0x3d70'0000u;
  std::uint32_t probe_seed_start = 0x4d70'0000u;
  std::uint32_t network_seed = 0xd707'c0deu;
  float gamma = 0.995f;
  float gae_lambda = 0.95f;
  float learning_rate = 0.0003f;
  float clip_ratio = 0.2f;
  float entropy_coefficient = 0.01f;
  float value_coefficient = 0.5f;
  float gradient_norm = 0.5f;
  std::string checkpoint = "/tmp/drop7-native-ppo.json";
};

struct UpdateMetrics {
  double policy_loss = 0;
  double value_loss = 0;
  double entropy = 0;
  double approximate_kl = 0;
  double clip_fraction = 0;
  int updates = 0;
};

struct Evaluation {
  double mean_score = 0;
  double mean_moves = 0;
  std::int64_t minimum_score = 0;
  std::int64_t maximum_score = 0;
  int censored = 0;
};

inline double gradientCheckLoss(const Network& network,
                                const Observation& observation, int action,
                                float policy_coefficient,
                                float value_derivative,
                                float entropy_coefficient) {
  const auto prediction = network.forward(observation);
  double entropy = 0;
  for (float probability : prediction.probabilities) {
    if (probability > 0) entropy -= probability * std::log(probability);
  }
  return policy_coefficient *
             std::log(std::max(1e-12f, prediction.probabilities[action])) +
         value_derivative * prediction.value -
         entropy_coefficient * entropy;
}

inline bool gradientCheck(std::ostream& output) {
  Network network(0x6a09'e667u);
  State state = initialHeadlessState(0x2d70'0042u);
  // Move away from the highly symmetric initial observation so embeddings,
  // state scalars, and both policy/value heads all receive useful gradients.
  for (int action : {3, 1, 5, 2}) {
    MoveResult move;
    if (!playHeadlessMove(state, 0x2d70'0042u, action, move)) {
      throw std::runtime_error("gradient-check setup chose an illegal action");
    }
  }
  const Observation observation = observe(state);
  constexpr int action = 4;
  constexpr float policy_coefficient = 0.37f;
  constexpr float value_derivative = -0.19f;
  constexpr float entropy_coefficient = 0.013f;
  const auto prediction = network.forward(observation);
  auto analytic = network.zeroGradient();
  network.accumulateGradient(
      observation, prediction, action, policy_coefficient, value_derivative,
      entropy_coefficient, analytic);

  const std::array<int, 14> indexes{{
      Layout::board_embedding,
      Layout::board_embedding + state.board[indexOf(6, 3)] *
                                    kBoardEmbeddingSize + 2,
      Layout::disc_embedding + state.next_disc * kDiscEmbeddingSize + 1,
      Layout::w1 + 17,
      Layout::w1 + 11 * kInputSize + 127,
      Layout::b1 + 7,
      Layout::w2 + 5 * kHidden1 + 9,
      Layout::b2 + 12,
      Layout::policy_w + action * kHidden2 + 3,
      Layout::policy_w + 2 * kHidden2 + 11,
      Layout::policy_b + action,
      Layout::value_w + 6,
      Layout::value_b,
      Layout::board_embedding + kSolid * kBoardEmbeddingSize + 3,
  }};
  constexpr float epsilon = 0.001f;
  double maximum_absolute_error = 0;
  double maximum_scaled_error = 0;
  for (int index : indexes) {
    const float original = network.parameter(index);
    network.setParameter(index, original + epsilon);
    const double positive = gradientCheckLoss(
        network, observation, action, policy_coefficient, value_derivative,
        entropy_coefficient);
    network.setParameter(index, original - epsilon);
    const double negative = gradientCheckLoss(
        network, observation, action, policy_coefficient, value_derivative,
        entropy_coefficient);
    network.setParameter(index, original);
    const double numerical = (positive - negative) / (2 * epsilon);
    const double absolute_error = std::abs(numerical - analytic[index]);
    const double scaled_error =
        absolute_error / std::max(1e-4, std::abs(numerical) +
                                            std::abs(analytic[index]));
    maximum_absolute_error = std::max(maximum_absolute_error, absolute_error);
    maximum_scaled_error = std::max(maximum_scaled_error, scaled_error);
    output << "GRADIENT {\"index\":" << index
           << ",\"analytic\":" << analytic[index]
           << ",\"numerical\":" << numerical
           << ",\"absoluteError\":" << absolute_error
           << ",\"scaledError\":" << scaled_error << "}\n";
  }
  const bool passed = maximum_absolute_error < 2e-4 ||
                      maximum_scaled_error < 0.015;
  output << "GRADIENT_CHECK {\"passed\":" << (passed ? "true" : "false")
         << ",\"maximumAbsoluteError\":" << maximum_absolute_error
         << ",\"maximumScaledError\":" << maximum_scaled_error << "}\n";
  return passed;
}

inline void finishAdvantages(std::vector<Sample>& samples, float bootstrap,
                             float gamma, float lambda) {
  float next_value = bootstrap;
  float advantage = 0;
  for (auto iterator = samples.rbegin(); iterator != samples.rend(); ++iterator) {
    const float nonterminal = iterator->terminal ? 0.0f : 1.0f;
    const float delta = iterator->reward + gamma * next_value * nonterminal -
                        iterator->old_value;
    advantage = delta + gamma * lambda * nonterminal * advantage;
    iterator->advantage = advantage;
    iterator->return_value = advantage + iterator->old_value;
    next_value = iterator->old_value;
  }
}

inline Collection collectEpisodes(const Network& network,
                                  const TrainingOptions& options,
                                  std::uint32_t seed_start) {
  const int thread_count =
      std::max(1, std::min(options.threads, options.episodes_per_iteration));
  std::vector<Collection> partial(static_cast<std::size_t>(thread_count));
  std::vector<std::thread> workers;
  workers.reserve(thread_count);
  for (int thread = 0; thread < thread_count; ++thread) {
    workers.emplace_back([&, thread] {
      Collection& destination = partial[thread];
      for (int episode = thread; episode < options.episodes_per_iteration;
           episode += thread_count) {
        const std::uint32_t seed = seed_start + static_cast<std::uint32_t>(episode);
        State state = initialHeadlessState(seed);
        Mulberry32 policy_random(mix32(seed ^ 0x504f'4c49u));
        std::vector<Sample> trajectory;
        trajectory.reserve(128);
        while (!state.game_over && state.moves_played < options.max_moves) {
          Sample sample;
          const auto canonical = observeCanonical(state);
          sample.observation = canonical.observation;
          const auto prediction = network.forward(sample.observation);
          sample.action = network.sampleAction(prediction, policy_random);
          if (sample.action < 0) throw std::runtime_error("policy found no action");
          sample.old_log_probability =
              std::log(std::max(1e-12f, prediction.probabilities[sample.action]));
          sample.old_value = prediction.value;
          MoveResult move;
          const int environment_action =
              physicalAction(sample.action, canonical.mirrored);
          if (!playHeadlessMove(state, seed, environment_action, move)) {
            throw std::runtime_error("policy sampled an illegal action");
          }
          // This is the game's actual return, scaled to keep it subordinate to
          // the one-point survival reward; no hand-authored board shaping.
          sample.reward =
              1.0f + static_cast<float>(move.score_delta) / 100'000.0f;
          sample.terminal = state.game_over;
          trajectory.push_back(std::move(sample));
        }
        const float bootstrap = state.game_over
                                    ? 0.0f
                                    : network.forward(observe(state)).value;
        finishAdvantages(trajectory, bootstrap, options.gamma,
                         options.gae_lambda);
        destination.samples.insert(destination.samples.end(),
                                   std::make_move_iterator(trajectory.begin()),
                                   std::make_move_iterator(trajectory.end()));
        destination.scores.push_back(state.score);
        destination.moves.push_back(state.moves_played);
      }
    });
  }
  for (auto& worker : workers) worker.join();

  Collection result;
  std::size_t sample_count = 0;
  for (const auto& item : partial) sample_count += item.samples.size();
  result.samples.reserve(sample_count);
  result.scores.reserve(options.episodes_per_iteration);
  result.moves.reserve(options.episodes_per_iteration);
  for (auto& item : partial) {
    result.samples.insert(result.samples.end(),
                          std::make_move_iterator(item.samples.begin()),
                          std::make_move_iterator(item.samples.end()));
    result.scores.insert(result.scores.end(), item.scores.begin(), item.scores.end());
    result.moves.insert(result.moves.end(), item.moves.begin(), item.moves.end());
  }
  return result;
}

inline void shuffle(std::vector<int>& values, Mulberry32& random) {
  for (std::size_t index = values.size(); index > 1; --index) {
    const std::size_t selected = static_cast<std::size_t>(
        (static_cast<std::uint64_t>(random.nextBits()) * index) >> 32);
    std::swap(values[index - 1], values[selected]);
  }
}

inline UpdateMetrics update(Network& network, std::vector<Sample>& samples,
                            const TrainingOptions& options,
                            Mulberry32& training_random) {
  if (samples.empty()) throw std::runtime_error("PPO batch was empty");
  double advantage_mean = 0;
  for (const auto& sample : samples) advantage_mean += sample.advantage;
  advantage_mean /= samples.size();
  double advantage_variance = 0;
  for (const auto& sample : samples) {
    const double difference = sample.advantage - advantage_mean;
    advantage_variance += difference * difference;
  }
  const float advantage_scale = static_cast<float>(
      1.0 / std::sqrt(advantage_variance / samples.size() + 1e-8));
  for (auto& sample : samples) {
    sample.advantage =
        static_cast<float>((sample.advantage - advantage_mean) * advantage_scale);
  }

  std::vector<int> order(samples.size());
  std::iota(order.begin(), order.end(), 0);
  UpdateMetrics metrics;
  std::uint64_t metric_samples = 0;
  for (int epoch = 0; epoch < options.epochs; ++epoch) {
    shuffle(order, training_random);
    for (std::size_t begin = 0; begin < order.size();
         begin += options.minibatch_size) {
      const std::size_t end =
          std::min(order.size(), begin + options.minibatch_size);
      const float inverse_batch = 1.0f / static_cast<float>(end - begin);
      auto gradient = network.zeroGradient();
      for (std::size_t offset = begin; offset < end; ++offset) {
        const Sample& sample = samples[order[offset]];
        const auto prediction = network.forward(sample.observation);
        const float probability =
            std::max(1e-12f, prediction.probabilities[sample.action]);
        const float log_probability = std::log(probability);
        const float ratio =
            std::exp(log_probability - sample.old_log_probability);
        const float clipped_ratio = std::clamp(
            ratio, 1.0f - options.clip_ratio, 1.0f + options.clip_ratio);
        const float raw_objective = ratio * sample.advantage;
        const float clipped_objective = clipped_ratio * sample.advantage;
        const bool clipped =
            (sample.advantage >= 0 && ratio > 1.0f + options.clip_ratio) ||
            (sample.advantage < 0 && ratio < 1.0f - options.clip_ratio);
        const float policy_coefficient =
            clipped ? 0.0f : -sample.advantage * ratio * inverse_batch;
        const float value_difference = prediction.value - sample.return_value;
        const float value_derivative =
            2.0f * options.value_coefficient * value_difference * inverse_batch;
        network.accumulateGradient(
            sample.observation, prediction, sample.action, policy_coefficient,
            value_derivative, options.entropy_coefficient * inverse_batch,
            gradient);

        float entropy = 0;
        for (float candidate : prediction.probabilities) {
          if (candidate > 0) entropy -= candidate * std::log(candidate);
        }
        metrics.policy_loss -= std::min(raw_objective, clipped_objective);
        metrics.value_loss += 0.5 * value_difference * value_difference;
        metrics.entropy += entropy;
        metrics.approximate_kl +=
            sample.old_log_probability - log_probability;
        metrics.clip_fraction += clipped ? 1.0 : 0.0;
        ++metric_samples;
      }
      network.applyAdam(gradient, options.learning_rate,
                        options.gradient_norm);
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

inline Evaluation evaluate(const Network& network, std::uint32_t seed_start,
                           int games, int max_moves, int threads) {
  const int thread_count = std::max(1, std::min(threads, games));
  std::vector<std::vector<std::pair<std::int64_t, int>>> partial(
      static_cast<std::size_t>(thread_count));
  std::vector<std::thread> workers;
  for (int thread = 0; thread < thread_count; ++thread) {
    workers.emplace_back([&, thread] {
      for (int game = thread; game < games; game += thread_count) {
        const std::uint32_t seed = seed_start + static_cast<std::uint32_t>(game);
        State state = initialHeadlessState(seed);
        while (!state.game_over && state.moves_played < max_moves) {
          const auto canonical = observeCanonical(state);
          const int action = network.greedyAction(canonical.observation);
          MoveResult move;
          if (!playHeadlessMove(
                  state, seed, physicalAction(action, canonical.mirrored), move)) {
            throw std::runtime_error("greedy policy selected an illegal action");
          }
        }
        partial[thread].push_back({state.score, state.moves_played});
      }
    });
  }
  for (auto& worker : workers) worker.join();
  Evaluation result;
  result.minimum_score = std::numeric_limits<std::int64_t>::max();
  result.maximum_score = std::numeric_limits<std::int64_t>::min();
  for (const auto& group : partial) {
    for (const auto& [score, moves] : group) {
      result.mean_score += score;
      result.mean_moves += moves;
      result.minimum_score = std::min(result.minimum_score, score);
      result.maximum_score = std::max(result.maximum_score, score);
      if (moves >= max_moves) ++result.censored;
    }
  }
  result.mean_score /= games;
  result.mean_moves /= games;
  return result;
}

inline double mean(const std::vector<int>& values) {
  if (values.empty()) return 0;
  return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

inline double mean(const std::vector<std::int64_t>& values) {
  if (values.empty()) return 0;
  return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

inline void saveCheckpoint(const Network& network,
                           const TrainingOptions& options, int iteration,
                           const Evaluation& probe) {
  std::ofstream output(options.checkpoint);
  if (!output) throw std::runtime_error("could not open checkpoint path");
  output << std::setprecision(9)
         << "{\"format\":\"drop7-native-actor-critic-v1\",\"version\":1,"
         << "\"architecture\":{\"boardCategories\":" << kBoardCategories
         << ",\"boardEmbedding\":" << kBoardEmbeddingSize
         << ",\"discCategories\":" << kDiscCategories
         << ",\"discEmbedding\":" << kDiscEmbeddingSize
         << ",\"scalars\":" << kScalarCount << ",\"input\":"
         << kInputSize << ",\"hidden\":[" << kHidden1 << ',' << kHidden2
         << "],\"actions\":" << kActions << "},\"layout\":{"
         << "\"boardEmbedding\":" << Layout::board_embedding
         << ",\"discEmbedding\":" << Layout::disc_embedding
         << ",\"w1\":" << Layout::w1 << ",\"b1\":" << Layout::b1
         << ",\"w2\":" << Layout::w2 << ",\"b2\":" << Layout::b2
         << ",\"policyW\":" << Layout::policy_w
         << ",\"policyB\":" << Layout::policy_b
         << ",\"valueW\":" << Layout::value_w
         << ",\"valueB\":" << Layout::value_b << ",\"count\":"
         << Layout::count << "},\"training\":{\"iteration\":" << iteration
         << ",\"trainingSeedStart\":" << options.training_seed_start
         << ",\"probeSeedStart\":" << options.probe_seed_start
         << ",\"probeGames\":" << options.probe_games
         << ",\"objective\":\"survival-plus-true-score\","
         << "\"horizontalCanonicalization\":true},\"probe\":{"
         << "\"meanScore\":" << probe.mean_score << ",\"meanMoves\":"
         << probe.mean_moves << ",\"minimumScore\":" << probe.minimum_score
         << ",\"maximumScore\":" << probe.maximum_score
         << ",\"censored\":" << probe.censored << "},\"parameters\":[";
  const auto& parameters = network.parameters();
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    if (index != 0) output << ',';
    output << parameters[index];
  }
  output << "]}\n";
}

inline int train(const TrainingOptions& options) {
  if (options.iterations < 1 || options.episodes_per_iteration < 1 ||
      options.threads < 1 || options.epochs < 1 ||
      options.minibatch_size < 1 || options.probe_games < 1 ||
      options.probe_every < 1) {
    throw std::invalid_argument("training counts must be positive");
  }
  Network network(options.network_seed);
  Mulberry32 training_random(mix32(options.network_seed ^ 0x5550'4441u));
  double best_probe_score = -std::numeric_limits<double>::infinity();
  std::uint64_t total_training_episodes = 0;
  const auto training_started = std::chrono::steady_clock::now();

  const Evaluation initial = evaluate(
      network, options.probe_seed_start, options.probe_games,
      options.max_moves, options.threads);
  std::cout << std::fixed << std::setprecision(3)
            << "PROBE {\"iteration\":0,\"meanScore\":"
            << initial.mean_score << ",\"meanMoves\":" << initial.mean_moves
            << ",\"minimumScore\":" << initial.minimum_score
            << ",\"maximumScore\":" << initial.maximum_score
            << ",\"censored\":" << initial.censored
            << ",\"validationEligible\":false}\n";

  for (int iteration = 1; iteration <= options.iterations; ++iteration) {
    const auto started = std::chrono::steady_clock::now();
    const std::uint32_t seed_start =
        options.training_seed_start +
        static_cast<std::uint32_t>(total_training_episodes);
    Collection collection = collectEpisodes(network, options, seed_start);
    total_training_episodes += options.episodes_per_iteration;
    const UpdateMetrics metrics =
        update(network, collection.samples, options, training_random);
    const double seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
    std::cout << "TRAIN {\"iteration\":" << iteration
              << ",\"episodes\":" << options.episodes_per_iteration
              << ",\"samples\":" << collection.samples.size()
              << ",\"meanScore\":" << mean(collection.scores)
              << ",\"meanMoves\":" << mean(collection.moves)
              << ",\"policyLoss\":" << metrics.policy_loss
              << ",\"valueLoss\":" << metrics.value_loss
              << ",\"entropy\":" << metrics.entropy
              << ",\"approximateKl\":" << metrics.approximate_kl
              << ",\"clipFraction\":" << metrics.clip_fraction
              << ",\"updates\":" << metrics.updates
              << ",\"seconds\":" << seconds
              << ",\"samplesPerSecond\":"
              << collection.samples.size() / seconds << "}\n";

    if (iteration % options.probe_every != 0) continue;
    const Evaluation probe = evaluate(
        network, options.probe_seed_start, options.probe_games,
        options.max_moves, options.threads);
    const bool eligible = probe.mean_score >= 400'000.0;
    const bool selected = probe.mean_score > best_probe_score;
    if (selected) {
      best_probe_score = probe.mean_score;
      saveCheckpoint(network, options, iteration, probe);
    }
    std::cout << "PROBE {\"iteration\":" << iteration
              << ",\"meanScore\":" << probe.mean_score
              << ",\"meanMoves\":" << probe.mean_moves
              << ",\"minimumScore\":" << probe.minimum_score
              << ",\"maximumScore\":" << probe.maximum_score
              << ",\"censored\":" << probe.censored
              << ",\"selected\":" << (selected ? "true" : "false")
              << ",\"validationEligible\":"
              << (eligible ? "true" : "false") << "}\n";
  }
  const double total_seconds = std::chrono::duration<double>(
                                   std::chrono::steady_clock::now() -
                                   training_started)
                                   .count();
  std::cout << "DONE {\"iterations\":" << options.iterations
            << ",\"trainingEpisodes\":" << total_training_episodes
            << ",\"bestProbeScore\":" << best_probe_score
            << ",\"checkpoint\":\"" << options.checkpoint
            << "\",\"seconds\":" << total_seconds << "}\n";
  return 0;
}

}  // namespace drop7::ppo
