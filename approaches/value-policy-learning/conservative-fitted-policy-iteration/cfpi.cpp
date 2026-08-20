#include "../../../src/core/native/public-behavior.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <vector>

namespace drop7::cfpi_lab {

constexpr int kChanceStrata = 7;
constexpr int kAtomSpacing = 5;
constexpr int kMaximumLifetime = 500;
constexpr int kAtoms = kMaximumLifetime / kAtomSpacing + 1;
constexpr int kAuxiliaryHeads = 5;
constexpr int kOutputs = kAtoms + kAuxiliaryHeads;
constexpr int kEnsembleSize = 4;
constexpr int kHashBuckets = 8'192;
constexpr std::uint64_t kMaximumModeledTransitions = 20'000'000;

using Distribution = std::array<float, kAtoms>;

struct ObservableState {
  Board board{};
  std::uint8_t next_disc = 1;
  std::uint8_t moves_remaining = kMovesPerLevel;
};

struct TransitionBudget {
  std::uint64_t used = 0;
  std::uint64_t limit = kMaximumModeledTransitions;

  void reserve() {
    if (used >= limit) throw std::runtime_error("modeled-transition budget exhausted");
    ++used;
  }
};

inline ObservableState observable(const State& state) {
  return {state.board, state.next_disc,
          static_cast<std::uint8_t>(state.moves_remaining)};
}

inline State materialize(const ObservableState& state) {
  State result;
  result.board = state.board;
  result.next_disc = state.next_disc;
  result.moves_remaining = state.moves_remaining;
  return result;
}

inline std::uint32_t observableHash(const ObservableState& state) {
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

inline std::uint32_t seedWithFirstDisc(std::uint32_t base,
                                       std::uint8_t target) {
  std::uint32_t candidate = base;
  for (std::uint32_t attempt = 0; attempt < 1'000; ++attempt) {
    Mulberry32 probe(candidate);
    if (probe.nextDisc() == target) return candidate;
    candidate = mix32(candidate + 0x9e37'79b9u + attempt);
  }
  throw std::runtime_error("could not construct reveal stratum");
}

struct Counterfactual {
  State state{};
  MoveResult move{};
  std::uint8_t reveal_stratum = 1;
  std::uint8_t next_disc_stratum = 1;
};

inline Counterfactual counterfactual(const ObservableState& source, int action,
                                     int stratum,
                                     TransitionBudget& budget) {
  if (stratum < 0 || stratum >= kChanceStrata) {
    throw std::invalid_argument("chance stratum must be in [0, 7)");
  }
  State state = materialize(source);
  const std::uint32_t hash = observableHash(source);
  const int reveal_offset = static_cast<int>(mix32(hash ^ 0x5245'564cu) % 7u);
  const int disc_offset = static_cast<int>(mix32(hash ^ 0x4449'5343u) % 7u);
  const auto reveal = static_cast<std::uint8_t>(
      ((reveal_offset + stratum) % kChanceStrata) + 1);
  const auto next_disc = static_cast<std::uint8_t>(
      ((disc_offset + 3 * stratum) % kChanceStrata) + 1);
  const std::uint32_t base = mix32(
      hash ^ (static_cast<std::uint32_t>(stratum + 1) * 0xc2b2'ae35u) ^
      0x4346'5049u);
  Mulberry32 random(seedWithFirstDisc(base, reveal));
  MoveResult move;
  budget.reserve();
  if (!playMove(state, action, random, move)) {
    throw std::runtime_error("counterfactual enumerated an illegal action");
  }
  State child = move.state;
  if (!child.game_over) child.next_disc = next_disc;
  child.score = 0;
  child.level = 1;
  child.moves_played = 0;
  return {child, move, reveal, next_disc};
}

inline Distribution pointMass(double remaining_moves) {
  Distribution result{};
  const double bounded = std::clamp(remaining_moves, 0.0,
                                    static_cast<double>(kMaximumLifetime));
  const double position = bounded / kAtomSpacing;
  const int lower = static_cast<int>(std::floor(position));
  const int upper = std::min(kAtoms - 1, lower + 1);
  const float upper_weight = static_cast<float>(position - lower);
  result[lower] += 1.0f - upper_weight;
  result[upper] += upper_weight;
  return result;
}

inline Distribution shiftOneMove(const Distribution& child) {
  Distribution result{};
  for (int atom = 0; atom < kAtoms; ++atom) {
    const float probability = child[atom];
    const double shifted = std::min(
        static_cast<double>(kMaximumLifetime),
        1.0 + static_cast<double>(atom * kAtomSpacing));
    const double position = shifted / kAtomSpacing;
    const int lower = static_cast<int>(std::floor(position));
    const int upper = std::min(kAtoms - 1, lower + 1);
    const float upper_weight = static_cast<float>(position - lower);
    result[lower] += probability * (1.0f - upper_weight);
    result[upper] += probability * upper_weight;
  }
  return result;
}

inline double survivalAuc(const Distribution& distribution) {
  double expectation = 0;
  for (int atom = 0; atom < kAtoms; ++atom) {
    expectation += distribution[atom] * atom * kAtomSpacing;
  }
  return expectation;
}

inline double lowerConfidenceMargin(const std::array<double, 4>& candidate,
                                    const std::array<double, 4>& behavior,
                                    double z = 1.96) {
  std::array<double, 4> differences{};
  for (int index = 0; index < 4; ++index) {
    differences[index] = candidate[index] - behavior[index];
  }
  const double mean = std::accumulate(differences.begin(), differences.end(),
                                      0.0) /
                      differences.size();
  double sum_squares = 0;
  for (double difference : differences) {
    sum_squares += (difference - mean) * (difference - mean);
  }
  const double standard_deviation =
      std::sqrt(sum_squares / (differences.size() - 1));
  return mean - z * standard_deviation / std::sqrt(differences.size());
}

inline bool mirrorIsSmaller(const Board& board) {
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

struct CanonicalObservable {
  ObservableState state{};
  int action = -1;
};

inline CanonicalObservable canonicalize(const ObservableState& source,
                                        int action) {
  CanonicalObservable result{source, action};
  if (!mirrorIsSmaller(source.board)) return result;
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      result.state.board[indexOf(row, column)] =
          source.board[indexOf(row, kBoardSize - 1 - column)];
    }
  }
  if (action >= 0) result.action = kBoardSize - 1 - action;
  return result;
}

inline int tupleCode(std::uint8_t first, std::uint8_t second,
                     std::uint8_t third, std::uint8_t fourth) {
  return ((first * 10 + second) * 10 + third) * 10 + fourth;
}

inline std::vector<std::uint16_t> hashedFeatures(
    const ObservableState& source, int action, std::uint32_t model_seed) {
  const CanonicalObservable canonical = canonicalize(source, action);
  const auto& state = canonical.state;
  const auto emit = [&](std::uint32_t code) {
    return static_cast<std::uint16_t>(
        mix32(code ^ model_seed) % static_cast<std::uint32_t>(kHashBuckets));
  };
  std::vector<std::uint16_t> features;
  features.reserve(160);
  features.push_back(emit(0x4249'4153u));
  features.push_back(emit(0x5048'4153u ^
                          (static_cast<std::uint32_t>(state.moves_remaining) << 8) ^
                          (static_cast<std::uint32_t>(state.next_disc) << 16) ^
                          static_cast<std::uint32_t>(canonical.action)));
  int occupied = 0;
  int covers = 0;
  int maximum_height = 0;
  for (int index = 0; index < kCellCount; ++index) {
    const std::uint8_t cell = state.board[index];
    occupied += cell != kEmpty;
    covers += cell == kSolid || cell == kCracked;
    features.push_back(emit(
        0x4345'4c4cu ^ (static_cast<std::uint32_t>(index) * 0x9e37'79b9u) ^
        (static_cast<std::uint32_t>(cell) << 20) ^
        (static_cast<std::uint32_t>(canonical.action) << 27)));
  }
  for (int row = 0; row < kBoardSize; ++row) {
    for (int start = 0; start <= kBoardSize - 4; ++start) {
      const int pattern = tupleCode(
          state.board[indexOf(row, start)],
          state.board[indexOf(row, start + 1)],
          state.board[indexOf(row, start + 2)],
          state.board[indexOf(row, start + 3)]);
      features.push_back(emit(
          0x524f'5734u ^ (static_cast<std::uint32_t>(row * 4 + start) << 16) ^
          static_cast<std::uint32_t>(pattern * 7 + canonical.action)));
    }
  }
  for (int column = 0; column < kBoardSize; ++column) {
    int height = 0;
    for (int row = 0; row < kBoardSize; ++row) {
      height += state.board[indexOf(row, column)] != kEmpty;
    }
    maximum_height = std::max(maximum_height, height);
    features.push_back(emit(
        0x4845'4947u ^ (static_cast<std::uint32_t>(column) << 8) ^
        (static_cast<std::uint32_t>(height) << 16) ^
        static_cast<std::uint32_t>(canonical.action)));
    for (int start = 0; start <= kBoardSize - 4; ++start) {
      const int pattern = tupleCode(
          state.board[indexOf(start, column)],
          state.board[indexOf(start + 1, column)],
          state.board[indexOf(start + 2, column)],
          state.board[indexOf(start + 3, column)]);
      features.push_back(emit(
          0x434f'4c34u ^
          (static_cast<std::uint32_t>(column * 4 + start) << 16) ^
          static_cast<std::uint32_t>(pattern * 7 + canonical.action)));
    }
  }
  for (int row = 0; row < kBoardSize - 1; ++row) {
    for (int column = 0; column < kBoardSize - 1; ++column) {
      const int pattern = tupleCode(
          state.board[indexOf(row, column)],
          state.board[indexOf(row, column + 1)],
          state.board[indexOf(row + 1, column)],
          state.board[indexOf(row + 1, column + 1)]);
      features.push_back(emit(
          0x5351'5232u ^
          (static_cast<std::uint32_t>(row * 6 + column) << 16) ^
          static_cast<std::uint32_t>(pattern * 7 + canonical.action)));
    }
  }
  features.push_back(emit(
      0x474c'4f42u ^ (static_cast<std::uint32_t>(occupied) << 8) ^
      (static_cast<std::uint32_t>(covers) << 16) ^
      (static_cast<std::uint32_t>(maximum_height) << 24) ^
      static_cast<std::uint32_t>(canonical.action)));
  std::sort(features.begin(), features.end());
  features.erase(std::unique(features.begin(), features.end()), features.end());
  return features;
}

inline float sigmoid(float value) {
  if (value >= 0) return 1.0f / (1.0f + std::exp(-value));
  const float exponential = std::exp(value);
  return exponential / (1.0f + exponential);
}

struct Prediction {
  Distribution distribution{};
  double expected_lifetime = 0;
  double shaped_return = 0;
  double next_rise_survival = 0;
  double five_move_clears = 0;
  double five_move_reveals = 0;
  double top_risk = 0;
};

struct TrainingTarget {
  ObservableState state{};
  int action = -1;
  Distribution distribution{};
  float shaped_return = 0;
  float next_rise_survival = 0;
  float five_move_clears = 0;
  float five_move_reveals = 0;
  float top_risk = 0;
  std::uint32_t identifier = 0;
};

class DistributionModel {
 public:
  explicit DistributionModel(std::uint32_t seed = 1)
      : seed_(seed),
        weights_(static_cast<std::size_t>(kHashBuckets) * kOutputs),
        squared_gradients_(weights_.size(), 1e-3f) {
    double normalizer = 0;
    for (int atom = 0; atom < kAtoms; ++atom) {
      const double distance = (atom * kAtomSpacing - 80.0) / 30.0;
      prior_logits_[atom] = static_cast<float>(-0.5 * distance * distance);
      normalizer += std::exp(prior_logits_[atom]);
    }
    (void)normalizer;
  }

  Prediction predict(const ObservableState& state, int action) const {
    const auto features = hashedFeatures(state, action, seed_);
    const float scale = 1.0f / std::sqrt(static_cast<float>(features.size()));
    std::array<float, kOutputs> output{};
    for (int atom = 0; atom < kAtoms; ++atom) output[atom] = prior_logits_[atom];
    output[kAtoms] = 80.0f / 500.0f;
    output[kAtoms + 1] = 2.2f;
    output[kAtoms + 2] = 12.0f / 15.0f;
    output[kAtoms + 3] = 7.0f / 10.0f;
    output[kAtoms + 4] = -2.2f;
    for (std::uint16_t feature : features) {
      const std::size_t base = static_cast<std::size_t>(feature) * kOutputs;
      for (int index = 0; index < kOutputs; ++index) {
        output[index] += scale * weights_[base + index];
      }
    }
    const float maximum = *std::max_element(output.begin(),
                                             output.begin() + kAtoms);
    double sum = 0;
    Prediction prediction;
    for (int atom = 0; atom < kAtoms; ++atom) {
      prediction.distribution[atom] = std::exp(output[atom] - maximum);
      sum += prediction.distribution[atom];
    }
    for (int atom = 0; atom < kAtoms; ++atom) {
      prediction.distribution[atom] /= static_cast<float>(sum);
    }
    prediction.expected_lifetime = survivalAuc(prediction.distribution);
    prediction.shaped_return = output[kAtoms] * 500.0;
    prediction.next_rise_survival = sigmoid(output[kAtoms + 1]);
    prediction.five_move_clears = output[kAtoms + 2] * 15.0;
    prediction.five_move_reveals = output[kAtoms + 3] * 10.0;
    prediction.top_risk = sigmoid(output[kAtoms + 4]);
    return prediction;
  }

  void train(const TrainingTarget& target, float learning_rate) {
    if ((mix32(target.identifier ^ seed_) & 7u) == 0u) return;
    const auto features = hashedFeatures(target.state, target.action, seed_);
    const float scale = 1.0f / std::sqrt(static_cast<float>(features.size()));
    const Prediction prediction = predict(target.state, target.action);
    std::array<float, kOutputs> gradient{};
    for (int atom = 0; atom < kAtoms; ++atom) {
      gradient[atom] = prediction.distribution[atom] - target.distribution[atom];
    }
    gradient[kAtoms] = 0.2f * 2.0f * static_cast<float>(
        (prediction.shaped_return - target.shaped_return) / 500.0);
    gradient[kAtoms + 1] =
        0.1f * static_cast<float>(prediction.next_rise_survival -
                                  target.next_rise_survival);
    gradient[kAtoms + 2] = 0.1f * 2.0f * static_cast<float>(
        (prediction.five_move_clears - target.five_move_clears) / 15.0);
    gradient[kAtoms + 3] = 0.1f * 2.0f * static_cast<float>(
        (prediction.five_move_reveals - target.five_move_reveals) / 10.0);
    gradient[kAtoms + 4] =
        0.1f * static_cast<float>(prediction.top_risk - target.top_risk);
    for (std::uint16_t feature : features) {
      const std::size_t base = static_cast<std::size_t>(feature) * kOutputs;
      for (int index = 0; index < kOutputs; ++index) {
        const float local = gradient[index] * scale;
        float& accumulator = squared_gradients_[base + index];
        accumulator += local * local;
        weights_[base + index] -=
            learning_rate * local / std::sqrt(accumulator);
      }
    }
  }

  std::size_t bytes() const {
    return (weights_.size() + squared_gradients_.size()) * sizeof(float);
  }

 private:
  std::uint32_t seed_ = 1;
  std::vector<float> weights_;
  std::vector<float> squared_gradients_;
  std::array<float, kAtoms> prior_logits_{};
};

using Ensemble = std::array<DistributionModel, kEnsembleSize>;

inline Ensemble createEnsemble(std::uint32_t seed) {
  return {{DistributionModel(mix32(seed ^ 0x1111'1111u)),
           DistributionModel(mix32(seed ^ 0x2222'2222u)),
           DistributionModel(mix32(seed ^ 0x3333'3333u)),
           DistributionModel(mix32(seed ^ 0x4444'4444u))}};
}

inline std::array<double, kEnsembleSize> ensembleScores(
    const Ensemble& ensemble, const ObservableState& state, int action) {
  std::array<double, kEnsembleSize> result{};
  for (int member = 0; member < kEnsembleSize; ++member) {
    const Prediction prediction = ensemble[member].predict(state, action);
    result[member] = 0.8 * prediction.expected_lifetime +
                     0.2 * prediction.shaped_return;
  }
  return result;
}

inline int conservativeAction(const ObservableState& state,
                              int behavior_action,
                              const Ensemble& ensemble,
                              double confidence_z, double minimum_margin,
                              int* switched = nullptr) {
  const auto behavior = ensembleScores(ensemble, state, behavior_action);
  int selected = behavior_action;
  double best_lower = 0;
  for (int action = 0; action < kBoardSize; ++action) {
    if (action == behavior_action || !isLegal(state.board, action)) continue;
    const double lower =
        lowerConfidenceMargin(ensembleScores(ensemble, state, action), behavior,
                              confidence_z) -
        minimum_margin;
    if (lower > best_lower) {
      best_lower = lower;
      selected = action;
    }
  }
  if (switched != nullptr && selected != behavior_action) ++*switched;
  return selected;
}

struct PilotOptions {
  int iterations = 1;
  int behavior_games = 1;
  int improved_games = 1;
  int stage_games = 4;
  int max_moves = 500;
  int collection_stride = 3;
  int epochs = 2;
  int maximum_states = 1'500;
  float learning_rate = 0.05f;
  double confidence_z = 1.96;
  double minimum_margin = 3.0;
  double shaping_scale = 1.0;
  std::uint64_t maximum_modeled_transitions = kMaximumModeledTransitions;
};

struct CollectedState {
  ObservableState state{};
  int actual_action = -1;
  int behavior_action = -1;
  int move_index = 0;
  int remaining_lifetime = 0;
  bool terminal_observed = false;
};

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  int clears = 0;
  int reveals = 0;
  int switches = 0;
  bool censored = false;
  std::uint64_t behavior_search_work = 0;
};

struct Summary {
  double mean_score = 0;
  double mean_moves = 0;
  double clear_rate = 0;
  double reveal_rate = 0;
  double mean_switches = 0;
  std::uint64_t behavior_search_work = 0;
  std::vector<GameResult> games;
};

inline std::pair<int, int> throughput(const MoveResult& move) {
  int clears = 0;
  int reveals = 0;
  for (const Wave& wave : move.waves) {
    clears += wave.cleared;
    reveals += wave.revealed;
  }
  return {clears, reveals};
}

inline bool hasTopRisk(const State& state) {
  if (state.game_over) return true;
  const cfpi::PhaseMetrics metrics = cfpi::evaluatePhaseMetrics(state);
  return metrics.maximum_height >= 6 || metrics.legal_columns <= 2;
}

inline double normalizedPotential(const ObservableState& state) {
  return 10.0 * std::tanh(cfpi::phasePotential(materialize(state)) / 250'000.0);
}

GameResult runGame(std::uint32_t seed, const Ensemble* ensemble,
                   bool improved, const PilotOptions& options,
                   std::vector<CollectedState>* collected) {
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  std::vector<std::size_t> collected_indexes;
  while (!state.game_over && state.moves_played < options.max_moves) {
    cfpi::BehaviorMetrics behavior_metrics;
    const int behavior_action =
        cfpi::chooseBehaviorAction(state, {}, &behavior_metrics);
    result.behavior_search_work += behavior_metrics.work;
    if (!isLegal(state.board, behavior_action)) {
      throw std::runtime_error("phase-safety behavior selected illegal action");
    }
    int action = behavior_action;
    if (improved) {
      if (ensemble == nullptr) {
        throw std::invalid_argument("improved rollout requires ensemble");
      }
      action = conservativeAction(observable(state), behavior_action, *ensemble,
                                  options.confidence_z,
                                  options.minimum_margin, &result.switches);
    }
    if (collected != nullptr &&
        state.moves_played % options.collection_stride == 0 &&
        static_cast<int>(collected->size()) < options.maximum_states) {
      collected_indexes.push_back(collected->size());
      collected->push_back({observable(state), action, behavior_action,
                            state.moves_played, 0, false});
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("rollout policy selected illegal action");
    }
    const auto [clears, reveals] = throughput(move);
    result.clears += clears;
    result.reveals += reveals;
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  if (collected != nullptr) {
    for (std::size_t index : collected_indexes) {
      CollectedState& sample = (*collected)[index];
      sample.remaining_lifetime = state.moves_played - sample.move_index;
      sample.terminal_observed = state.game_over;
    }
  }
  return result;
}

Summary summarize(std::vector<GameResult> games) {
  Summary result;
  result.games = std::move(games);
  std::uint64_t moves = 0;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
  for (const GameResult& game : result.games) {
    result.mean_score += game.score;
    result.mean_moves += game.moves;
    result.mean_switches += game.switches;
    result.behavior_search_work += game.behavior_search_work;
    moves += static_cast<std::uint64_t>(game.moves);
    clears += static_cast<std::uint64_t>(game.clears);
    reveals += static_cast<std::uint64_t>(game.reveals);
  }
  const double count = static_cast<double>(result.games.size());
  result.mean_score /= count;
  result.mean_moves /= count;
  result.mean_switches /= count;
  result.clear_rate = static_cast<double>(clears) / std::max<std::uint64_t>(1, moves);
  result.reveal_rate =
      static_cast<double>(reveals) / std::max<std::uint64_t>(1, moves);
  return result;
}

struct AuxiliaryTarget {
  float next_rise_survival = 0;
  float clears = 0;
  float reveals = 0;
  float top_risk = 0;
};

AuxiliaryTarget auxiliaryRollout(const Counterfactual& first, int sample,
                                 const Ensemble& selector,
                                 const PilotOptions& options,
                                 TransitionBudget& budget) {
  AuxiliaryTarget target;
  State state = first.state;
  bool survived_rise = first.move.level_advanced;
  auto [clears, reveals] = throughput(first.move);
  target.clears += clears;
  target.reveals += reveals;
  target.top_risk = hasTopRisk(state) ? 1.0f : 0.0f;
  for (int step = 1; step < 5 && !state.game_over; ++step) {
    const int behavior = cfpi::choosePhaseGreedyAction(state);
    const int action = conservativeAction(
        observable(state), behavior, selector, options.confidence_z,
        options.minimum_margin);
    const Counterfactual next = counterfactual(
        observable(state), action, (sample + 2 * step) % kChanceStrata, budget);
    state = next.state;
    survived_rise = survived_rise || next.move.level_advanced;
    const auto [step_clears, step_reveals] = throughput(next.move);
    target.clears += step_clears;
    target.reveals += step_reveals;
    if (hasTopRisk(state)) target.top_risk = 1.0f;
  }
  target.next_rise_survival = survived_rise ? 1.0f : 0.0f;
  return target;
}

TrainingTarget fittedTarget(const CollectedState& sample, int action,
                            const Ensemble& selector, const Ensemble& target,
                            const PilotOptions& options,
                            TransitionBudget& budget,
                            std::uint32_t identifier) {
  TrainingTarget result;
  result.state = sample.state;
  result.action = action;
  result.identifier = identifier;
  const double parent_potential = normalizedPotential(sample.state);
  for (int stratum = 0; stratum < kChanceStrata; ++stratum) {
    const Counterfactual outcome =
        counterfactual(sample.state, action, stratum, budget);
    Distribution shifted{};
    double shaped = 1.0 - options.shaping_scale * parent_potential;
    if (outcome.state.game_over) {
      shifted = pointMass(1);
    } else {
      const ObservableState child = observable(outcome.state);
      const int behavior = cfpi::choosePhaseGreedyAction(outcome.state);
      const int selected = conservativeAction(
          child, behavior, selector, options.confidence_z,
          options.minimum_margin);
      Distribution mixture{};
      double child_shaped = 0;
      for (int member = 0; member < kEnsembleSize; ++member) {
        // Double estimator: selection uses the online ensemble, while member
        // e is evaluated by a distinct fixed target member e+1.
        const Prediction prediction =
            target[(member + 1) % kEnsembleSize].predict(child, selected);
        for (int atom = 0; atom < kAtoms; ++atom) {
          mixture[atom] += prediction.distribution[atom] / kEnsembleSize;
        }
        child_shaped += prediction.shaped_return / kEnsembleSize;
      }
      shifted = shiftOneMove(mixture);
      shaped += options.shaping_scale * normalizedPotential(child) +
                child_shaped;
    }
    for (int atom = 0; atom < kAtoms; ++atom) {
      result.distribution[atom] += shifted[atom] / kChanceStrata;
    }
    result.shaped_return += static_cast<float>(shaped / kChanceStrata);
    const AuxiliaryTarget auxiliary =
        auxiliaryRollout(outcome, stratum, selector, options, budget);
    result.next_rise_survival +=
        auxiliary.next_rise_survival / kChanceStrata;
    result.five_move_clears += auxiliary.clears / kChanceStrata;
    result.five_move_reveals += auxiliary.reveals / kChanceStrata;
    result.top_risk += auxiliary.top_risk / kChanceStrata;
  }
  if (action == sample.actual_action && sample.terminal_observed) {
    const Distribution monte_carlo = pointMass(sample.remaining_lifetime);
    for (int atom = 0; atom < kAtoms; ++atom) {
      result.distribution[atom] =
          0.5f * result.distribution[atom] + 0.5f * monte_carlo[atom];
    }
    const double shaped_monte_carlo =
        sample.remaining_lifetime - options.shaping_scale * parent_potential;
    result.shaped_return = static_cast<float>(
        0.5 * result.shaped_return + 0.5 * shaped_monte_carlo);
  }
  return result;
}

inline std::uint64_t peakResidentBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
#endif
}

double pairedLower95(const Summary& behavior, const Summary& improved) {
  if (behavior.games.size() != improved.games.size()) {
    throw std::invalid_argument("paired summaries differ in size");
  }
  const int count = static_cast<int>(behavior.games.size());
  std::vector<double> differences(count);
  for (int index = 0; index < count; ++index) {
    differences[index] = improved.games[index].score - behavior.games[index].score;
  }
  const double mean =
      std::accumulate(differences.begin(), differences.end(), 0.0) / count;
  if (count < 2) return -std::numeric_limits<double>::infinity();
  double squares = 0;
  for (double difference : differences) {
    squares += (difference - mean) * (difference - mean);
  }
  const double deviation = std::sqrt(squares / (count - 1));
  return mean - 1.96 * deviation / std::sqrt(static_cast<double>(count));
}

void printSummary(std::string_view tag, int iteration, const Summary& summary) {
  std::cout << std::fixed << std::setprecision(3) << tag
            << " {\"iteration\":" << iteration
            << ",\"games\":" << summary.games.size()
            << ",\"meanScore\":" << summary.mean_score
            << ",\"meanMoves\":" << summary.mean_moves
            << ",\"clearRate\":" << summary.clear_rate
            << ",\"revealRate\":" << summary.reveal_rate
            << ",\"meanSwitches\":" << summary.mean_switches
            << ",\"behaviorSearchWork\":"
            << summary.behavior_search_work << "}\n";
}

int runPilot(const PilotOptions& options) {
  if (options.iterations < 1 || options.behavior_games < 1 ||
      options.improved_games < 1 || options.stage_games < 1 ||
      options.max_moves < 1 || options.collection_stride < 1 ||
      options.epochs < 1 || options.maximum_states < 1 ||
      options.learning_rate <= 0 || options.maximum_modeled_transitions < 1 ||
      options.maximum_modeled_transitions > kMaximumModeledTransitions) {
    throw std::invalid_argument("invalid CFPI pilot options");
  }
  Ensemble ensemble = createEnsemble(0xcf91'0001u);
  TransitionBudget budget{0, options.maximum_modeled_transitions};
  std::vector<CollectedState> states;
  states.reserve(options.maximum_states);
  std::cout << "CFPI_CONFIG {\"atoms\":" << kAtoms
            << ",\"atomSpacing\":" << kAtomSpacing
            << ",\"ensemble\":" << kEnsembleSize
            << ",\"hashBuckets\":" << kHashBuckets
            << ",\"transitionLimit\":" << options.maximum_modeled_transitions
            << ",\"trainingRanges\":[\"0x3d\",\"0x3e\"]}\n";

  for (int iteration = 0; iteration < options.iterations; ++iteration) {
    const std::uint32_t behavior_start =
        0x3d7c'0000u + static_cast<std::uint32_t>(iteration * 0x1'0000);
    for (int game = 0; game < options.behavior_games; ++game) {
      runGame(behavior_start + static_cast<std::uint32_t>(game), nullptr,
              false, options, &states);
    }
    if (iteration > 0) {
      const std::uint32_t improved_start =
          0x3d7d'0000u + static_cast<std::uint32_t>(iteration * 0x1'0000);
      for (int game = 0; game < options.improved_games; ++game) {
        runGame(improved_start + static_cast<std::uint32_t>(game), &ensemble,
                true, options, &states);
      }
    }

    const Ensemble frozen_target = ensemble;
    std::vector<TrainingTarget> targets;
    targets.reserve(states.size() * 6);
    std::uint32_t identifier = 0;
    for (const CollectedState& state : states) {
      for (int action = 0; action < kBoardSize; ++action) {
        if (!isLegal(state.state.board, action)) continue;
        targets.push_back(fittedTarget(state, action, ensemble, frozen_target,
                                       options, budget, identifier++));
      }
    }
    for (int epoch = 0; epoch < options.epochs; ++epoch) {
      for (std::size_t offset = 0; offset < targets.size(); ++offset) {
        const std::size_t index =
            (offset + static_cast<std::size_t>(epoch) * 7'919u) %
            targets.size();
        for (DistributionModel& member : ensemble) {
          member.train(targets[index], options.learning_rate);
        }
      }
    }

    const std::uint32_t stage_start =
        0x3e70'0000u + static_cast<std::uint32_t>(iteration * 0x1'0000);
    std::vector<GameResult> behavior_games;
    std::vector<GameResult> improved_games;
    behavior_games.reserve(options.stage_games);
    improved_games.reserve(options.stage_games);
    for (int game = 0; game < options.stage_games; ++game) {
      const std::uint32_t seed = stage_start + static_cast<std::uint32_t>(game);
      behavior_games.push_back(runGame(seed, nullptr, false, options, nullptr));
      improved_games.push_back(runGame(seed, &ensemble, true, options, nullptr));
    }
    const Summary behavior = summarize(std::move(behavior_games));
    const Summary improved = summarize(std::move(improved_games));
    const double paired_lower = pairedLower95(behavior, improved);
    const bool qualified = options.stage_games >= 64 &&
                           improved.mean_score >= 400'000 &&
                           improved.mean_moves >= 120 &&
                           improved.clear_rate >= 2.20 &&
                           improved.reveal_rate >= 1.25 && paired_lower > 0;
    printSummary("CFPI_BEHAVIOR", iteration, behavior);
    printSummary("CFPI_IMPROVED", iteration, improved);
    std::cout << "CFPI_STAGE {\"iteration\":" << iteration
              << ",\"states\":" << states.size()
              << ",\"actionTargets\":" << targets.size()
              << ",\"modeledTransitions\":" << budget.used
              << ",\"pairedScoreLower95\":" << paired_lower
              << ",\"qualified\":" << (qualified ? "true" : "false")
              << ",\"requires64PairedGames\":true"
              << ",\"peakResidentBytes\":" << peakResidentBytes() << "}\n";
  }
  return 0;
}

bool selfTest(std::ostream& output) {
  const bool behavior = cfpi::selfTest(output);

  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;
  State irrelevant = state;
  irrelevant.score = 987'654;
  irrelevant.level = 42;
  irrelevant.moves_played = 271;
  const bool seed_blind_observable =
      observableHash(observable(state)) == observableHash(observable(irrelevant));

  int legal_count = 0;
  const auto legal = legalColumns(state.board, legal_count);
  bool all_legal_enumerated = legal_count > 0;
  TransitionBudget budget{0, static_cast<std::uint64_t>(legal_count * 7)};
  std::array<bool, 8> reveals{};
  std::array<bool, 8> discs{};
  for (int offset = 0; offset < legal_count; ++offset) {
    for (int sample = 0; sample < kChanceStrata; ++sample) {
      const Counterfactual result =
          counterfactual(observable(state), legal[offset], sample, budget);
      reveals[result.reveal_stratum] = true;
      discs[result.next_disc_stratum] = true;
      all_legal_enumerated =
          all_legal_enumerated && result.move.state.moves_played == 1;
    }
  }
  const bool exact_strata =
      std::all_of(reveals.begin() + 1, reveals.end(), [](bool value) {
        return value;
      }) &&
      std::all_of(discs.begin() + 1, discs.end(), [](bool value) {
        return value;
      });
  const bool budget_exact =
      budget.used == static_cast<std::uint64_t>(legal_count * 7);

  const Distribution lifetime = pointMass(120);
  const Distribution shifted = shiftOneMove(lifetime);
  const double mass = std::accumulate(shifted.begin(), shifted.end(), 0.0);
  const bool distribution = std::abs(mass - 1.0) < 1e-6 &&
                            std::abs(survivalAuc(shifted) - 121.0) < 1e-5;

  const std::array<double, 4> behavior_scores{{10, 10, 10, 10}};
  const bool conservative_switch =
      lowerConfidenceMargin({{13, 13, 13, 13}}, behavior_scores) > 0 &&
      lowerConfidenceMargin({{14, 7, 14, 7}}, behavior_scores) < 0;

  ObservableState mirrored_observable = observable(state);
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      mirrored_observable.board[indexOf(row, kBoardSize - 1 - column)] =
          state.board[indexOf(row, column)];
    }
  }
  DistributionModel model(0x51f7'0001u);
  const Prediction forward = model.predict(observable(state), 1);
  const Prediction reflected = model.predict(mirrored_observable, 5);
  const bool model_mirror_safe =
      std::abs(forward.expected_lifetime - reflected.expected_lifetime) < 1e-9 &&
      std::abs(forward.shaped_return - reflected.shaped_return) < 1e-9;
  TrainingTarget training;
  training.state = observable(state);
  training.action = 1;
  training.distribution = pointMass(140);
  training.shaped_return = 140;
  training.next_rise_survival = 1;
  training.five_move_clears = 13;
  training.five_move_reveals = 8;
  training.top_risk = 0;
  const double before_training = model.predict(training.state, 1).expected_lifetime;
  for (std::uint32_t step = 0; step < 32; ++step) {
    training.identifier = step;
    model.train(training, 0.05f);
  }
  const double after_training = model.predict(training.state, 1).expected_lifetime;
  const bool learner_wired = std::isfinite(after_training) &&
                             after_training > before_training;

  bool budget_rejected = false;
  try {
    TransitionBudget one{0, 1};
    (void)counterfactual(observable(state), legal[0], 0, one);
    (void)counterfactual(observable(state), legal[0], 1, one);
  } catch (const std::runtime_error&) {
    budget_rejected = true;
  }

  const bool passed = behavior && seed_blind_observable &&
                      all_legal_enumerated && exact_strata && budget_exact &&
                      distribution && conservative_switch &&
                      model_mirror_safe && learner_wired && budget_rejected;
  output << "CFPI_SELF_TEST {\"passed\":" << (passed ? "true" : "false")
         << ",\"seedBlindObservable\":"
         << (seed_blind_observable ? "true" : "false")
         << ",\"legalActions\":" << legal_count
         << ",\"counterfactuals\":" << budget.used
         << ",\"exactRevealAndDiscStrata\":"
         << (exact_strata ? "true" : "false")
         << ",\"distributionAuc\":" << survivalAuc(shifted)
         << ",\"conservativeSwitch\":"
         << (conservative_switch ? "true" : "false")
         << ",\"modelMirrorSafe\":"
         << (model_mirror_safe ? "true" : "false")
         << ",\"learnerWired\":" << (learner_wired ? "true" : "false")
         << ",\"budgetRejected\":"
         << (budget_rejected ? "true" : "false") << "}\n";
  return passed;
}

}  // namespace drop7::cfpi_lab

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::cfpi_lab::selfTest(std::cout) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    const auto value_after = [&](std::string_view flag,
                                 std::string fallback) {
      for (int index = 1; index + 1 < argc; ++index) {
        if (std::string_view(argv[index]) == flag) return std::string(argv[index + 1]);
      }
      return fallback;
    };
    const auto has_flag = [&](std::string_view flag) {
      for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == flag) return true;
      }
      return false;
    };
    if (has_flag("--pilot")) {
      drop7::cfpi_lab::PilotOptions options;
      options.iterations =
          std::stoi(value_after("--iterations", std::to_string(options.iterations)));
      options.behavior_games = std::stoi(value_after(
          "--behavior-games", std::to_string(options.behavior_games)));
      options.improved_games = std::stoi(value_after(
          "--improved-games", std::to_string(options.improved_games)));
      options.stage_games = std::stoi(
          value_after("--stage-games", std::to_string(options.stage_games)));
      options.max_moves = std::stoi(
          value_after("--max-moves", std::to_string(options.max_moves)));
      options.collection_stride = std::stoi(value_after(
          "--collection-stride", std::to_string(options.collection_stride)));
      options.epochs =
          std::stoi(value_after("--epochs", std::to_string(options.epochs)));
      options.maximum_states = std::stoi(value_after(
          "--maximum-states", std::to_string(options.maximum_states)));
      options.learning_rate = std::stof(value_after(
          "--learning-rate", std::to_string(options.learning_rate)));
      options.confidence_z = std::stod(value_after(
          "--confidence-z", std::to_string(options.confidence_z)));
      options.minimum_margin = std::stod(value_after(
          "--minimum-margin", std::to_string(options.minimum_margin)));
      options.shaping_scale = std::stod(value_after(
          "--shaping-scale", std::to_string(options.shaping_scale)));
      options.maximum_modeled_transitions = std::stoull(value_after(
          "--maximum-modeled-transitions",
          std::to_string(options.maximum_modeled_transitions)));
      return drop7::cfpi_lab::runPilot(options);
    }
    std::cerr << "Usage: drop7_cfpi --self-test | --pilot [options]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
