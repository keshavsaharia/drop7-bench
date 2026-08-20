#include "../../../src/core/native/public-behavior.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstdint>
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
#include <unordered_map>
#include <utility>
#include <vector>

// Fits nonnegative magnitudes for fixed, interpretable accessible-energy
// feature directions using public-state fair-policy continuations.  Privileged
// oracle tapes are unavailable to training roll-ins, ranking continuations,
// and the deployed policy.
namespace drop7::accessible_energy {

using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kCollectionStart = 0x3d90'f000u;
constexpr int kCollectionGames = 32;
constexpr int kTrainingGames = 24;
constexpr int kHeldoutGames = kCollectionGames - kTrainingGames;
constexpr int kContinuationHorizon = 30;
constexpr int kCollectionMaximumMoves = 180;
constexpr int kRankingStride = 10;
constexpr int kRankingMaximumStatesPerGame = 7;
constexpr int kRankingScenarios = 3;
constexpr std::uint32_t kScreenStart = 0x3e89'0000u;
constexpr int kScreenGames = 8;
constexpr std::uint32_t kConfirmationStart = 0x3e8a'0000u;
constexpr int kConfirmationGames = 16;
constexpr int kEvaluationMaximumMoves = 200;
constexpr int kParallelism = 4;
constexpr double kCalibrationRidge = 0.10;
constexpr double kFeatureRidge = 1.0;
constexpr int kRidgeIterations = 500;
constexpr double kRidgeTolerance = 1.0e-9;
constexpr double kResidualClipStandardDeviations = 3.0;
constexpr std::uint64_t kSearchMaximumWork = 1'000'000;
constexpr std::size_t kSearchMaximumCacheEntries = 40'000;
constexpr int kChanceSamples = 5;

static_assert(kTrainingGames > 0 && kHeldoutGames > 0);
static_assert(kLevelBonus == 7'000);

std::mutex progress_mutex;

enum Feature : std::size_t {
  kDirectPotential,
  kLatentChainPotential,
  kTriggerReadiness,
  kRiseTriggerReadiness,
  kStoredHighNumbers,
  kCrackedExposure,
  kSolidCells,
  kSolidAltitude,
  kProjectedOccupancyDebt,
  kDeadLowNumbers,
  kLowCapLoad,
  kAdjacentLowCapLoad,
  kAdjacentOnes,
  kFeatureCount,
};

enum Group : int { kEnergy, kCoverDamage, kCongestion, kGroupCount };

struct FeatureSpec {
  const char* name;
  Group group;
  double sign;
};

constexpr std::array<FeatureSpec, kFeatureCount> kFeatureSpecs{{
    {"directPotential", kEnergy, 1.0},
    {"latentChainPotential", kEnergy, 1.0},
    {"triggerReadiness", kEnergy, 1.0},
    {"riseTriggerReadiness", kEnergy, 1.0},
    {"storedHighNumbers", kEnergy, 1.0},
    {"crackedExposure", kCoverDamage, 1.0},
    {"solidCells", kCoverDamage, -1.0},
    {"solidAltitude", kCoverDamage, -1.0},
    {"projectedOccupancyDebt", kCongestion, -1.0},
    {"deadLowNumbers", kCongestion, -1.0},
    {"lowCapLoad", kCongestion, -1.0},
    {"adjacentLowCapLoad", kCongestion, -1.0},
    {"adjacentOnes", kCongestion, -1.0},
}};

constexpr std::array<const char*, kGroupCount> kGroupNames{{
    "storedEnergy", "coverDamage", "lowNumberCongestion",
}};

using RawFeatures = std::array<double, kFeatureCount>;

double readiness(int required) {
  return required >= 1 ? std::ldexp(1.0, 1 - required) : 0.0;
}

double unionReadiness(double first, double second) {
  return 1.0 - (1.0 - first) * (1.0 - second);
}

State publicState(const State& source) {
  State result;
  result.board = source.board;
  result.next_disc = source.next_disc;
  result.moves_remaining = source.moves_remaining;
  result.score = 0;
  result.level = 1;
  result.moves_played = 0;
  result.game_over = false;
  return result;
}

RawFeatures extractRawFeatures(const State& source) {
  const State state = publicState(source);
  const cfpi::detail::PhaseFeatures phase =
      cfpi::detail::extractPhaseFeatures(state);
  RawFeatures result{};
  result[kDirectPotential] = phase.direct_potential;
  result[kLatentChainPotential] = phase.latent_chain_potential;
  result[kTriggerReadiness] = phase.trigger_readiness;
  result[kRiseTriggerReadiness] = phase.rise_trigger_readiness;
  result[kCrackedExposure] = phase.cracked_exposure;
  result[kSolidCells] = phase.solid_cells;
  result[kProjectedOccupancyDebt] = phase.projected_occupancy_debt;
  result[kDeadLowNumbers] = phase.dead_low_numbers;
  result[kLowCapLoad] = phase.low_cap_load;
  result[kAdjacentLowCapLoad] = phase.adjacent_low_cap_load;
  result[kAdjacentOnes] = phase.adjacent_ones;

  for (int row = 0; row < kBoardSize; ++row) {
    const int elevation = kBoardSize - row;
    for (int column = 0; column < kBoardSize; ++column) {
      const std::uint8_t cell = state.board[indexOf(row, column)];
      if (cell == kSolid) result[kSolidAltitude] += elevation * elevation;
      if (cell < 5 || cell > 7) continue;
      const int horizontal = lineLength(state.board, row, column, false);
      const int vertical = lineLength(state.board, row, column, true);
      double ready = 0.0;
      if (horizontal < cell) ready = readiness(cell - horizontal);
      if (vertical < cell) {
        ready = unionReadiness(ready, readiness(cell - vertical));
      }
      if (ready > 0 && horizontal != cell && vertical != cell) {
        result[kStoredHighNumbers] += ready * (cell - 3) / 4.0;
      }
    }
  }
  for (std::size_t feature = 0; feature < kFeatureCount; ++feature) {
    result[feature] *= kFeatureSpecs[feature].sign;
    if (!std::isfinite(result[feature])) {
      throw std::runtime_error("accessible-energy feature is non-finite");
    }
  }
  return result;
}

struct StepOutcome {
  int cleared = 0;
  int revealed = 0;
  std::int64_t score = 0;
};

StepOutcome stepOutcome(const MoveResult& move) {
  StepOutcome result;
  result.score = move.score_delta;
  for (const Wave& wave : move.waves) {
    result.cleared += wave.cleared;
    result.revealed += wave.revealed;
  }
  return result;
}

struct RollinGame {
  std::uint32_t seed = 0;
  std::vector<State> states;
  std::vector<StepOutcome> outcomes;
  std::int64_t final_score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t work = 0;
};

RollinGame collectRollin(std::uint32_t seed) {
  RollinGame result;
  result.seed = seed;
  result.states.reserve(kCollectionMaximumMoves);
  result.outcomes.reserve(kCollectionMaximumMoves);
  State state = initialHeadlessState(seed);
  cfpi::BehaviorOptions options;
  options.max_depth = 3;
  options.chance_samples = kChanceSamples;
  options.max_work = kSearchMaximumWork;
  options.max_cache_entries = kSearchMaximumCacheEntries;
  while (!state.game_over && state.moves_played < kCollectionMaximumMoves) {
    result.states.push_back(publicState(state));
    cfpi::BehaviorMetrics metrics;
    const int action =
        cfpi::chooseBehaviorAction(publicState(state), options, &metrics);
    if (!isLegal(state.board, action) || metrics.completed_depth != 3) {
      throw std::runtime_error("collection exact-d3 action is incomplete");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("collection transition failed");
    }
    result.outcomes.push_back(stepOutcome(move));
    result.work += metrics.work;
  }
  result.final_score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  if (result.states.size() != result.outcomes.size()) {
    throw std::logic_error("roll-in state/outcome alignment failed");
  }
  return result;
}

std::vector<RollinGame> collectRollins() {
  std::vector<RollinGame> result(kCollectionGames);
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < kParallelism; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= kCollectionGames) return;
        const std::uint32_t seed =
            kCollectionStart + static_cast<std::uint32_t>(game);
        result[game] = collectRollin(seed);
        const std::lock_guard<std::mutex> lock(progress_mutex);
        std::cerr << "energy-collect " << game + 1 << '/'
                  << kCollectionGames << " seed 0x" << std::hex << seed
                  << std::dec << ' ' << result[game].final_score << " ("
                  << result[game].moves << " moves)\n";
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

struct ContinuationExample {
  int game = 0;
  RawFeatures features{};
  double phase_potential = 0.0;
  double survived_moves = 0.0;
  double cleared = 0.0;
  double revealed = 0.0;
  double future_score = 0.0;
};

std::vector<ContinuationExample> buildExamples(
    const std::vector<RollinGame>& games) {
  std::vector<ContinuationExample> result;
  for (int game = 0; game < static_cast<int>(games.size()); ++game) {
    const RollinGame& rollin = games[game];
    for (std::size_t index = 0; index < rollin.states.size(); ++index) {
      const std::size_t remaining = rollin.states.size() - index;
      if (rollin.censored && remaining < kContinuationHorizon) continue;
      const std::size_t steps =
          std::min<std::size_t>(kContinuationHorizon, remaining);
      ContinuationExample example;
      example.game = game;
      example.features = extractRawFeatures(rollin.states[index]);
      example.phase_potential =
          cfpi::phasePotential(rollin.states[index]);
      example.survived_moves = static_cast<double>(steps);
      for (std::size_t offset = 0; offset < steps; ++offset) {
        const StepOutcome& outcome = rollin.outcomes[index + offset];
        example.cleared += outcome.cleared;
        example.revealed += outcome.revealed;
        example.future_score += outcome.score;
      }
      result.push_back(example);
    }
  }
  return result;
}

template <std::size_t N>
struct RidgeModel {
  std::array<double, N> mean{};
  std::array<double, N> scale{};
  std::array<double, N> beta{};
  std::array<bool, N> enabled{};
  double intercept = 0.0;

  double predict(const std::array<double, N>& raw) const {
    double result = intercept;
    for (std::size_t feature = 0; feature < N; ++feature) {
      if (!enabled[feature]) continue;
      result += beta[feature] * (raw[feature] - mean[feature]) /
                scale[feature];
    }
    return result;
  }
};

template <std::size_t N>
RidgeModel<N> fitNonnegativeRidge(
    const std::vector<std::array<double, N>>& inputs,
    const std::vector<double>& targets, double lambda,
    const std::array<bool, N>& enabled) {
  if (inputs.empty() || inputs.size() != targets.size()) {
    throw std::invalid_argument("ridge data must be nonempty and aligned");
  }
  RidgeModel<N> model;
  model.enabled = enabled;
  model.scale.fill(0.0);
  for (const auto& input : inputs) {
    for (std::size_t feature = 0; feature < N; ++feature) {
      model.mean[feature] += input[feature] / inputs.size();
    }
  }
  for (const auto& input : inputs) {
    for (std::size_t feature = 0; feature < N; ++feature) {
      const double delta = input[feature] - model.mean[feature];
      model.scale[feature] += delta * delta / inputs.size();
    }
  }
  for (double& scale : model.scale) {
    scale = std::sqrt(scale);
    if (scale < 1.0e-9) scale = 1.0;
  }
  model.intercept =
      std::accumulate(targets.begin(), targets.end(), 0.0) /
      targets.size();
  std::vector<double> predictions(inputs.size(), model.intercept);
  const double penalty = lambda * inputs.size();
  for (int iteration = 0; iteration < kRidgeIterations; ++iteration) {
    double maximum_change = 0.0;
    const double old_intercept = model.intercept;
    double intercept_sum = 0.0;
    for (std::size_t row = 0; row < inputs.size(); ++row) {
      intercept_sum += targets[row] - (predictions[row] - model.intercept);
    }
    model.intercept = intercept_sum / inputs.size();
    const double intercept_change = model.intercept - old_intercept;
    for (double& prediction : predictions) prediction += intercept_change;
    maximum_change = std::abs(intercept_change);

    for (std::size_t feature = 0; feature < N; ++feature) {
      if (!enabled[feature]) continue;
      double numerator = 0.0;
      double denominator = penalty;
      for (std::size_t row = 0; row < inputs.size(); ++row) {
        const double standardized =
            (inputs[row][feature] - model.mean[feature]) /
            model.scale[feature];
        const double without =
            predictions[row] - model.beta[feature] * standardized;
        numerator += standardized * (targets[row] - without);
        denominator += standardized * standardized;
      }
      const double updated =
          denominator > 0.0 ? std::max(0.0, numerator / denominator) : 0.0;
      const double change = updated - model.beta[feature];
      if (change != 0.0) {
        for (std::size_t row = 0; row < inputs.size(); ++row) {
          const double standardized =
              (inputs[row][feature] - model.mean[feature]) /
              model.scale[feature];
          predictions[row] += change * standardized;
        }
      }
      model.beta[feature] = updated;
      maximum_change = std::max(maximum_change, std::abs(change));
    }
    if (maximum_change < kRidgeTolerance) break;
  }
  for (double value : model.beta) {
    if (!std::isfinite(value) || value < 0.0) {
      throw std::runtime_error("nonnegative ridge invariant failed");
    }
  }
  return model;
}

using CalibrationInput = std::array<double, 3>;

CalibrationInput calibrationInput(const ContinuationExample& example) {
  return {{example.survived_moves, example.cleared, example.revealed}};
}

struct PhaseBaseline {
  double intercept = 0.0;
  double slope = 0.0;

  double predict(double phase_potential) const {
    return intercept + slope * phase_potential;
  }
};

PhaseBaseline fitPhaseBaseline(const std::vector<double>& phase,
                               const std::vector<double>& targets) {
  if (phase.empty() || phase.size() != targets.size()) {
    throw std::invalid_argument("phase baseline data is misaligned");
  }
  const double phase_mean =
      std::accumulate(phase.begin(), phase.end(), 0.0) / phase.size();
  const double target_mean =
      std::accumulate(targets.begin(), targets.end(), 0.0) / targets.size();
  double covariance = 0.0;
  double variance = 0.0;
  for (std::size_t row = 0; row < phase.size(); ++row) {
    covariance += (phase[row] - phase_mean) *
                  (targets[row] - target_mean);
    variance += (phase[row] - phase_mean) * (phase[row] - phase_mean);
  }
  PhaseBaseline result;
  result.slope = variance > 1.0e-12 ? std::max(0.0, covariance / variance)
                                    : 0.0;
  result.intercept = target_mean - result.slope * phase_mean;
  return result;
}

double sampleStandardDeviation(const std::vector<double>& values) {
  if (values.size() < 2) return 0.0;
  const double mean =
      std::accumulate(values.begin(), values.end(), 0.0) / values.size();
  double squared = 0.0;
  for (const double value : values) {
    squared += (value - mean) * (value - mean);
  }
  return std::sqrt(squared / static_cast<double>(values.size() - 1));
}

struct ResidualModel {
  std::string name;
  std::array<bool, kFeatureCount> enabled{};
  RidgeModel<kFeatureCount> ridge;
  double clip = 0.0;
  double deployment_scale = 0.0;

  double residual(const RawFeatures& features) const {
    return std::clamp(ridge.predict(features), -clip, clip);
  }

  double deploymentResidual(const RawFeatures& features) const {
    return residual(features) * deployment_scale;
  }
};

struct LearnedModels {
  RidgeModel<3> calibration;
  PhaseBaseline phase;
  double residual_target_sd = 0.0;
  std::vector<ResidualModel> variants;
};

std::array<bool, kFeatureCount> featureMask(int omitted_group) {
  std::array<bool, kFeatureCount> result{};
  for (std::size_t feature = 0; feature < kFeatureCount; ++feature) {
    result[feature] =
        omitted_group < 0 || kFeatureSpecs[feature].group != omitted_group;
  }
  return result;
}

LearnedModels fitModels(const std::vector<ContinuationExample>& examples) {
  std::vector<CalibrationInput> calibration_inputs;
  std::vector<double> future_scores;
  for (const ContinuationExample& example : examples) {
    if (example.game >= kTrainingGames) continue;
    calibration_inputs.push_back(calibrationInput(example));
    future_scores.push_back(example.future_score);
  }
  std::array<bool, 3> calibration_enabled{};
  calibration_enabled.fill(true);
  LearnedModels result;
  result.calibration = fitNonnegativeRidge(
      calibration_inputs, future_scores, kCalibrationRidge,
      calibration_enabled);

  std::vector<RawFeatures> training_features;
  std::vector<double> phase_values;
  std::vector<double> calibrated_targets;
  for (const ContinuationExample& example : examples) {
    if (example.game >= kTrainingGames) continue;
    training_features.push_back(example.features);
    phase_values.push_back(example.phase_potential);
    calibrated_targets.push_back(
        result.calibration.predict(calibrationInput(example)));
  }
  result.phase = fitPhaseBaseline(phase_values, calibrated_targets);
  std::vector<double> residual_targets;
  residual_targets.reserve(calibrated_targets.size());
  for (std::size_t row = 0; row < calibrated_targets.size(); ++row) {
    residual_targets.push_back(
        calibrated_targets[row] - result.phase.predict(phase_values[row]));
  }
  result.residual_target_sd = sampleStandardDeviation(residual_targets);
  const double clip =
      std::max(1.0, kResidualClipStandardDeviations *
                        result.residual_target_sd);
  for (int omitted = -1; omitted < kGroupCount; ++omitted) {
    ResidualModel model;
    model.name = omitted < 0
                     ? "full"
                     : std::string("without-") + kGroupNames[omitted];
    model.enabled = featureMask(omitted);
    model.ridge = fitNonnegativeRidge(
        training_features, residual_targets, kFeatureRidge, model.enabled);
    model.clip = clip;
    model.deployment_scale =
        result.phase.slope > 1.0e-9 ? 1.0 / result.phase.slope : 0.0;
    result.variants.push_back(std::move(model));
  }
  return result;
}

struct PredictionMetrics {
  int examples = 0;
  double mae_calibrated = 0.0;
  double r2_calibrated = 0.0;
  double mae_future_score = 0.0;
  double r2_future_score = 0.0;
};

PredictionMetrics predictionMetrics(
    const std::vector<ContinuationExample>& examples,
    const LearnedModels& models, const ResidualModel* residual,
    bool training) {
  std::vector<double> calibrated_targets;
  std::vector<double> future_targets;
  std::vector<double> predictions;
  for (const ContinuationExample& example : examples) {
    const bool in_training = example.game < kTrainingGames;
    if (in_training != training) continue;
    const double calibrated =
        models.calibration.predict(calibrationInput(example));
    double prediction = models.phase.predict(example.phase_potential);
    if (residual != nullptr) prediction += residual->residual(example.features);
    calibrated_targets.push_back(calibrated);
    future_targets.push_back(example.future_score);
    predictions.push_back(prediction);
  }
  PredictionMetrics result;
  result.examples = static_cast<int>(predictions.size());
  if (predictions.empty()) return result;
  const auto score = [&](const std::vector<double>& targets,
                         double& mae, double& r2) {
    const double mean =
        std::accumulate(targets.begin(), targets.end(), 0.0) /
        targets.size();
    double error = 0.0;
    double total = 0.0;
    for (std::size_t row = 0; row < targets.size(); ++row) {
      mae += std::abs(targets[row] - predictions[row]);
      error += (targets[row] - predictions[row]) *
               (targets[row] - predictions[row]);
      total += (targets[row] - mean) * (targets[row] - mean);
    }
    mae /= targets.size();
    r2 = total > 1.0e-12 ? 1.0 - error / total : 0.0;
  };
  score(calibrated_targets, result.mae_calibrated, result.r2_calibrated);
  score(future_targets, result.mae_future_score, result.r2_future_score);
  return result;
}

struct CalibrationMetrics {
  int examples = 0;
  double mae = 0.0;
  double r2 = 0.0;
};

CalibrationMetrics calibrationMetrics(
    const std::vector<ContinuationExample>& examples,
    const RidgeModel<3>& calibration, bool training) {
  std::vector<double> targets;
  std::vector<double> predictions;
  for (const ContinuationExample& example : examples) {
    if ((example.game < kTrainingGames) != training) continue;
    targets.push_back(example.future_score);
    predictions.push_back(calibration.predict(calibrationInput(example)));
  }
  CalibrationMetrics result;
  result.examples = static_cast<int>(targets.size());
  if (targets.empty()) return result;
  const double mean =
      std::accumulate(targets.begin(), targets.end(), 0.0) / targets.size();
  double error = 0.0;
  double total = 0.0;
  for (std::size_t row = 0; row < targets.size(); ++row) {
    result.mae += std::abs(targets[row] - predictions[row]);
    error += (targets[row] - predictions[row]) *
             (targets[row] - predictions[row]);
    total += (targets[row] - mean) * (targets[row] - mean);
  }
  result.mae /= targets.size();
  result.r2 = total > 1.0e-12 ? 1.0 - error / total : 0.0;
  return result;
}

std::uint32_t publicStateHash(const State& state) {
  std::uint32_t hash = 0x811c'9dc5u;
  for (const std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint32_t>(cell + 1u);
    hash *= 0x0100'0193u;
  }
  hash ^= state.next_disc;
  hash *= 0x0100'0193u;
  hash ^= static_cast<std::uint32_t>(state.moves_remaining);
  return mix32(hash);
}

struct SimulatedContinuation {
  int survived = 0;
  int cleared = 0;
  int revealed = 0;
  std::int64_t score = 0;
  State first_successor{};
  std::int64_t first_score = 0;
  bool first_terminal = false;
};

SimulatedContinuation simulateContinuation(const State& source, int action,
                                            std::uint32_t random_seed) {
  State state = publicState(source);
  Mulberry32 random(random_seed);
  SimulatedContinuation result;
  for (int step = 0; step < kContinuationHorizon && !state.game_over; ++step) {
    const int chosen = step == 0
                           ? action
                           : cfpi::choosePhaseGreedyAction(publicState(state),
                                                           3);
    if (!isLegal(state.board, chosen)) break;
    MoveResult move;
    if (!playMove(state, chosen, random, move)) break;
    const StepOutcome outcome = stepOutcome(move);
    ++result.survived;
    result.cleared += outcome.cleared;
    result.revealed += outcome.revealed;
    result.score += outcome.score;
    state = move.state;
    if (step == 0) {
      result.first_successor = publicState(state);
      result.first_score = outcome.score;
      result.first_terminal = state.game_over;
    }
  }
  return result;
}

struct RankingMetrics {
  std::string name;
  int states = 0;
  int top_one = 0;
  int top_two = 0;
  int pairs = 0;
  int concordant_pairs = 0;

  double topOneRate() const {
    return states > 0 ? static_cast<double>(top_one) / states : 0.0;
  }

  double topTwoRate() const {
    return states > 0 ? static_cast<double>(top_two) / states : 0.0;
  }

  double pairwiseRate() const {
    return pairs > 0 ? static_cast<double>(concordant_pairs) / pairs : 0.0;
  }
};

int bestColumn(const State& state,
               const std::array<double, kBoardSize>& values) {
  for (const int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(state.board, column)) continue;
    bool best = true;
    for (int other = 0; other < kBoardSize; ++other) {
      if (isLegal(state.board, other) && values[other] > values[column]) {
        best = false;
        break;
      }
    }
    if (best) return column;
  }
  return -1;
}

std::vector<int> topTwoColumns(
    const State& state, const std::array<double, kBoardSize>& values) {
  std::vector<int> columns;
  for (const int column : cfpi::detail::kColumnOrder) {
    if (isLegal(state.board, column)) columns.push_back(column);
  }
  std::stable_sort(columns.begin(), columns.end(),
                   [&](int left, int right) {
                     return values[left] > values[right];
                   });
  if (columns.size() > 2) columns.resize(2);
  return columns;
}

void updateRanking(RankingMetrics& metrics, const State& state,
                   const std::array<double, kBoardSize>& targets,
                   const std::array<double, kBoardSize>& predictions) {
  const int target = bestColumn(state, targets);
  const int predicted = bestColumn(state, predictions);
  if (target < 0 || predicted < 0) return;
  ++metrics.states;
  metrics.top_one += target == predicted;
  const std::vector<int> top_two = topTwoColumns(state, predictions);
  metrics.top_two +=
      std::find(top_two.begin(), top_two.end(), target) != top_two.end();
  for (int left = 0; left < kBoardSize; ++left) {
    if (!isLegal(state.board, left)) continue;
    for (int right = left + 1; right < kBoardSize; ++right) {
      if (!isLegal(state.board, right)) continue;
      const double target_delta = targets[left] - targets[right];
      const double prediction_delta = predictions[left] - predictions[right];
      if (std::abs(target_delta) < 1.0e-9) continue;
      ++metrics.pairs;
      metrics.concordant_pairs += target_delta * prediction_delta > 0.0;
    }
  }
}

std::vector<RankingMetrics> evaluateRankings(
    const std::vector<RollinGame>& games, const LearnedModels& models) {
  std::vector<RankingMetrics> result;
  result.push_back({"phase-baseline"});
  for (const ResidualModel& model : models.variants) {
    result.push_back({model.name});
  }
  for (int game = kTrainingGames; game < kCollectionGames; ++game) {
    const RollinGame& rollin = games[game];
    int emitted = 0;
    for (std::size_t index = 0;
         index < rollin.states.size() &&
         emitted < kRankingMaximumStatesPerGame;
         index += kRankingStride) {
      const State& state = rollin.states[index];
      std::array<double, kBoardSize> targets{};
      std::vector<std::array<double, kBoardSize>> predictions(result.size());
      targets.fill(-std::numeric_limits<double>::infinity());
      for (auto& values : predictions) {
        values.fill(-std::numeric_limits<double>::infinity());
      }
      const std::uint32_t state_hash = publicStateHash(state);
      for (int column = 0; column < kBoardSize; ++column) {
        if (!isLegal(state.board, column)) continue;
        double target_sum = 0.0;
        std::vector<double> prediction_sum(result.size(), 0.0);
        for (int scenario = 0; scenario < kRankingScenarios; ++scenario) {
          const std::uint32_t random_seed = mix32(
              state_hash ^ 0xa511'e9b3u ^
              (static_cast<std::uint32_t>(scenario + 1) * 0x9e37'79b9u));
          const SimulatedContinuation simulation =
              simulateContinuation(state, column, random_seed);
          const CalibrationInput target_input{{
              static_cast<double>(simulation.survived),
              static_cast<double>(simulation.cleared),
              static_cast<double>(simulation.revealed),
          }};
          target_sum += models.calibration.predict(target_input);
          if (simulation.survived == 0 || simulation.first_terminal) {
            for (double& value : prediction_sum) {
              value += -1'000'000.0;
            }
            continue;
          }
          const double phase =
              cfpi::phasePotential(simulation.first_successor);
          prediction_sum[0] += simulation.first_score + phase;
          const RawFeatures raw =
              extractRawFeatures(simulation.first_successor);
          for (std::size_t variant = 0;
               variant < models.variants.size(); ++variant) {
            prediction_sum[variant + 1] +=
                simulation.first_score + phase +
                models.variants[variant].deploymentResidual(raw);
          }
        }
        targets[column] = target_sum / kRankingScenarios;
        for (std::size_t model = 0; model < result.size(); ++model) {
          predictions[model][column] =
              prediction_sum[model] / kRankingScenarios;
        }
      }
      for (std::size_t model = 0; model < result.size(); ++model) {
        updateRanking(result[model], state, targets, predictions[model]);
      }
      ++emitted;
    }
  }
  return result;
}

class WorkLimitReached : public std::exception {};

struct CacheEntry {
  double value = 0.0;
  std::list<std::string>::iterator order;
};

struct SearchMetrics {
  int completed_depth = 0;
  bool complete = false;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::size_t peak_cache_entries = 0;
};

struct SearchContext {
  explicit SearchContext(const ResidualModel& residual_model)
      : model(residual_model) {}

  const ResidualModel& model;
  std::unordered_map<std::string, CacheEntry> cache;
  std::list<std::string> order;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::size_t peak_cache_entries = 0;
};

void checkWork(const SearchContext& context) {
  if (context.work >= kSearchMaximumWork) throw WorkLimitReached{};
}

void cacheValue(SearchContext& context, std::string key, double value) {
  const auto prior = context.cache.find(key);
  if (prior != context.cache.end()) {
    context.order.erase(prior->second.order);
    context.cache.erase(prior);
  }
  while (context.cache.size() >= kSearchMaximumCacheEntries) {
    const std::string& oldest = context.order.front();
    context.cache.erase(oldest);
    context.order.pop_front();
  }
  context.order.push_back(key);
  auto order = std::prev(context.order.end());
  context.cache.emplace(std::move(key), CacheEntry{value, order});
  context.peak_cache_entries =
      std::max(context.peak_cache_entries, context.cache.size());
}

double bestFutureValue(const State& state, int depth,
                       SearchContext& context);

double evaluateAction(const State& state, int column, int depth,
                      SearchContext& context) {
  const std::uint32_t state_seed = cfpi::detail::scenarioSeedForState(
      state, 0xd707'5eedu, depth);
  double value = 0.0;
  for (int sample = 0; sample < kChanceSamples; ++sample) {
    checkWork(context);
    cfpi::detail::StratifiedRandom random{
        state_seed, sample, kChanceSamples, 0,
    };
    MoveResult move;
    if (!cfpi::detail::playMoveSampled(state, column, random, move)) {
      value += -1'000'000.0;
      continue;
    }
    ++context.work;
    const double score_delta = static_cast<double>(move.score_delta);
    if (move.state.game_over) {
      value += score_delta - 1'000'000.0;
      continue;
    }
    move.state.score = 0;
    move.state.next_disc = cfpi::detail::sampledNextDisc(
        state_seed, sample, kChanceSamples);
    bool ignored = false;
    const State next = cfpi::detail::canonicalState(move.state, ignored);
    value += score_delta + bestFutureValue(next, depth - 1, context);
  }
  return value / kChanceSamples;
}

double bestFutureValue(const State& state, int depth,
                       SearchContext& context) {
  ++context.nodes;
  checkWork(context);
  if (state.game_over) return -1'000'000.0;
  if (depth == 0) {
    ++context.work;
    const double value =
        cfpi::phasePotential(state) +
        context.model.deploymentResidual(extractRawFeatures(state));
    if (!std::isfinite(value)) {
      throw std::runtime_error("accessible-energy leaf is non-finite");
    }
    return value;
  }
  const std::string key = cfpi::detail::dynamicStateKey(state, depth);
  const auto cached = context.cache.find(key);
  if (cached != context.cache.end()) {
    ++context.cache_hits;
    context.order.splice(context.order.end(), context.order,
                         cached->second.order);
    return cached->second.value;
  }
  double best = -std::numeric_limits<double>::infinity();
  for (const int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(state.board, column)) continue;
    best = std::max(best, evaluateAction(state, column, depth, context));
  }
  if (!std::isfinite(best)) best = -1'000'000.0;
  cacheValue(context, key, best);
  return best;
}

std::pair<int, double> bestRootAction(const State& canonical, int depth,
                                      SearchContext& context) {
  int action = -1;
  double best = -std::numeric_limits<double>::infinity();
  // Full-width root invariant: every legal root action is evaluated.
  for (const int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(canonical.board, column)) continue;
    const double value = evaluateAction(canonical, column, depth, context);
    if (value > best) {
      best = value;
      action = column;
    }
  }
  return {action, best};
}

int chooseEnergyAction(const State& source, const ResidualModel& model,
                       SearchMetrics* metrics = nullptr) {
  if (source.game_over) return -1;
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(
      publicState(source), mirrored);
  SearchContext context(model);
  int action = -1;
  int completed_depth = 0;
  for (int depth = 1; depth <= 3; ++depth) {
    try {
      const auto [candidate, value] =
          bestRootAction(canonical, depth, context);
      (void)value;
      if (candidate < 0) break;
      action = candidate;
      completed_depth = depth;
    } catch (const WorkLimitReached&) {
      break;
    }
  }
  if (action < 0) action = centerFirstMove(canonical.board);
  if (metrics != nullptr) {
    metrics->completed_depth = completed_depth;
    metrics->complete = completed_depth == 3;
    metrics->work = context.work;
    metrics->nodes = context.nodes;
    metrics->cache_hits = context.cache_hits;
    metrics->peak_cache_entries = context.peak_cache_entries;
  }
  return mirrored ? kBoardSize - 1 - action : action;
}

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
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::uint64_t completed_depth = 0;
  std::uint64_t complete_moves = 0;
  std::size_t peak_cache_entries = 0;
  std::uint64_t peak_rss_bytes = 0;
  std::uint64_t cleared = 0;
  std::uint64_t revealed = 0;
  double elapsed_seconds = 0.0;
};

GameResult runEvaluationGame(std::uint32_t seed,
                             const ResidualModel* model,
                             std::string_view label) {
  const auto started = Clock::now();
  GameResult result;
  result.seed = seed;
  State state = initialHeadlessState(seed);
  cfpi::BehaviorOptions exact;
  exact.max_depth = 3;
  exact.chance_samples = kChanceSamples;
  exact.max_work = kSearchMaximumWork;
  exact.max_cache_entries = kSearchMaximumCacheEntries;
  while (!state.game_over && state.moves_played < kEvaluationMaximumMoves) {
    int action = -1;
    if (model == nullptr) {
      cfpi::BehaviorMetrics metrics;
      action = cfpi::chooseBehaviorAction(publicState(state), exact, &metrics);
      result.work += metrics.work;
      result.nodes += metrics.nodes;
      result.cache_hits += metrics.cache_hits;
      result.completed_depth += metrics.completed_depth;
      result.complete_moves += metrics.complete;
      result.peak_cache_entries =
          std::max(result.peak_cache_entries, metrics.cache_entries);
    } else {
      SearchMetrics metrics;
      action = chooseEnergyAction(publicState(state), *model, &metrics);
      result.work += metrics.work;
      result.nodes += metrics.nodes;
      result.cache_hits += metrics.cache_hits;
      result.completed_depth += metrics.completed_depth;
      result.complete_moves += metrics.complete;
      result.peak_cache_entries =
          std::max(result.peak_cache_entries, metrics.peak_cache_entries);
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("evaluation selected an illegal root action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("evaluation transition failed");
    }
    const StepOutcome outcome = stepOutcome(move);
    result.cleared += outcome.cleared;
    result.revealed += outcome.revealed;
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.peak_rss_bytes = peakRssBytes();
  result.elapsed_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  {
    const std::lock_guard<std::mutex> lock(progress_mutex);
    std::cerr << label << " seed 0x" << std::hex << seed << std::dec << ' '
              << result.score << " (" << result.moves << " moves, work "
              << result.work << ")\n";
  }
  return result;
}

struct EvaluationCohort {
  std::vector<GameResult> exact;
  std::vector<GameResult> candidate;
  double wall_seconds = 0.0;
};

EvaluationCohort runEvaluationCohort(std::uint32_t seed_start, int games,
                                     const ResidualModel& model,
                                     std::string_view phase) {
  const auto started = Clock::now();
  EvaluationCohort result;
  result.exact.resize(games);
  result.candidate.resize(games);
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= games) return;
        const std::uint32_t seed =
            seed_start + static_cast<std::uint32_t>(game);
        result.exact[game] = runEvaluationGame(
            seed, nullptr, std::string(phase) + "-exact-d3");
        result.candidate[game] = runEvaluationGame(
            seed, &model, std::string(phase) + "-energy-d3");
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

struct EvaluationSummary {
  int games = 0;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double work_per_move = 0.0;
  double nodes_per_move = 0.0;
  double cache_hits_per_move = 0.0;
  double mean_depth = 0.0;
  double complete_rate = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double aggregate_seconds = 0.0;
  double moves_per_second = 0.0;
  std::size_t peak_cache_entries = 0;
  std::uint64_t peak_rss_bytes = 0;
  int censored = 0;
};

EvaluationSummary summarizeEvaluation(const std::vector<GameResult>& games) {
  if (games.empty()) throw std::invalid_argument("empty evaluation cohort");
  EvaluationSummary result;
  result.games = static_cast<int>(games.size());
  std::uint64_t moves = 0;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::uint64_t depth = 0;
  std::uint64_t complete = 0;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
  for (const GameResult& game : games) {
    result.mean_score += static_cast<double>(game.score) / games.size();
    result.mean_moves += static_cast<double>(game.moves) / games.size();
    result.censored += game.censored;
    result.aggregate_seconds += game.elapsed_seconds;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, game.peak_cache_entries);
    result.peak_rss_bytes =
        std::max(result.peak_rss_bytes, game.peak_rss_bytes);
    moves += game.moves;
    work += game.work;
    nodes += game.nodes;
    cache_hits += game.cache_hits;
    depth += game.completed_depth;
    complete += game.complete_moves;
    clears += game.cleared;
    reveals += game.revealed;
  }
  const double move_count = static_cast<double>(std::max<std::uint64_t>(1, moves));
  result.work_per_move = work / move_count;
  result.nodes_per_move = nodes / move_count;
  result.cache_hits_per_move = cache_hits / move_count;
  result.mean_depth = depth / move_count;
  result.complete_rate = complete / move_count;
  result.clears_per_move = clears / move_count;
  result.reveals_per_move = reveals / move_count;
  result.moves_per_second =
      move_count / std::max(1.0e-9, result.aggregate_seconds);
  return result;
}

struct PairedMetrics {
  double mean_score_delta = 0.0;
  double mean_move_delta = 0.0;
  int score_wins = 0;
  int move_wins = 0;
  int score_ties = 0;
  int move_ties = 0;
};

PairedMetrics pairedMetrics(const EvaluationCohort& cohort) {
  if (cohort.exact.size() != cohort.candidate.size() ||
      cohort.exact.empty()) {
    throw std::invalid_argument("evaluation cohorts are not paired");
  }
  PairedMetrics result;
  for (std::size_t game = 0; game < cohort.exact.size(); ++game) {
    const GameResult& exact = cohort.exact[game];
    const GameResult& candidate = cohort.candidate[game];
    if (exact.seed != candidate.seed) {
      throw std::logic_error("evaluation seed pairing failed");
    }
    result.mean_score_delta +=
        static_cast<double>(candidate.score - exact.score) /
        cohort.exact.size();
    result.mean_move_delta +=
        static_cast<double>(candidate.moves - exact.moves) /
        cohort.exact.size();
    result.score_wins += candidate.score > exact.score;
    result.move_wins += candidate.moves > exact.moves;
    result.score_ties += candidate.score == exact.score;
    result.move_ties += candidate.moves == exact.moves;
  }
  return result;
}

void writePredictionMetrics(std::ostream& output,
                            const PredictionMetrics& metrics) {
  output << "{\"examples\":" << metrics.examples
         << ",\"maeCalibrated\":" << metrics.mae_calibrated
         << ",\"r2Calibrated\":" << metrics.r2_calibrated
         << ",\"maeFutureScore\":" << metrics.mae_future_score
         << ",\"r2FutureScore\":" << metrics.r2_future_score << '}';
}

void writeRankingMetrics(std::ostream& output,
                         const RankingMetrics& metrics) {
  output << "{\"name\":\"" << metrics.name << "\",\"states\":"
         << metrics.states << ",\"topOne\":" << metrics.topOneRate()
         << ",\"topTwo\":" << metrics.topTwoRate()
         << ",\"pairs\":" << metrics.pairs
         << ",\"pairwiseConcordance\":" << metrics.pairwiseRate() << '}';
}

void writeEvaluationSummary(std::ostream& output,
                            const EvaluationSummary& summary) {
  output << "{\"games\":" << summary.games
         << ",\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"workPerMove\":" << summary.work_per_move
         << ",\"nodesPerMove\":" << summary.nodes_per_move
         << ",\"cacheHitsPerMove\":" << summary.cache_hits_per_move
         << ",\"meanCompletedDepth\":" << summary.mean_depth
         << ",\"completeRate\":" << summary.complete_rate
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"aggregateGameSeconds\":" << summary.aggregate_seconds
         << ",\"movesPerGameSecond\":" << summary.moves_per_second
         << ",\"peakCacheEntries\":" << summary.peak_cache_entries
         << ",\"peakRssBytes\":" << summary.peak_rss_bytes
         << ",\"censored\":" << summary.censored << '}';
}

void writePairedMetrics(std::ostream& output,
                        const PairedMetrics& metrics) {
  output << "{\"meanScoreDelta\":" << metrics.mean_score_delta
         << ",\"meanMoveDelta\":" << metrics.mean_move_delta
         << ",\"scoreWins\":" << metrics.score_wins
         << ",\"moveWins\":" << metrics.move_wins
         << ",\"scoreTies\":" << metrics.score_ties
         << ",\"moveTies\":" << metrics.move_ties << '}';
}

void writeGamePairs(std::ostream& output, const EvaluationCohort& cohort) {
  output << '[';
  for (std::size_t game = 0; game < cohort.exact.size(); ++game) {
    if (game > 0) output << ',';
    output << "{\"seed\":" << cohort.exact[game].seed
           << ",\"exactScore\":" << cohort.exact[game].score
           << ",\"exactMoves\":" << cohort.exact[game].moves
           << ",\"candidateScore\":" << cohort.candidate[game].score
           << ",\"candidateMoves\":" << cohort.candidate[game].moves
           << '}';
  }
  output << ']';
}

struct ExperimentResult {
  std::vector<RollinGame> rollins;
  std::vector<ContinuationExample> examples;
  LearnedModels models;
  CalibrationMetrics calibration_train;
  CalibrationMetrics calibration_heldout;
  PredictionMetrics baseline_train;
  PredictionMetrics baseline_heldout;
  std::vector<PredictionMetrics> variant_train;
  std::vector<PredictionMetrics> variant_heldout;
  std::vector<RankingMetrics> rankings;
  EvaluationCohort screen;
  bool screen_passed = false;
  EvaluationCohort confirmation;
  bool confirmation_ran = false;
  bool confirmation_passed = false;
  double total_wall_seconds = 0.0;
};

void writeArtifact(const ExperimentResult& result,
                   const std::string& path) {
  const ResidualModel& full = result.models.variants.front();
  const EvaluationSummary screen_exact =
      summarizeEvaluation(result.screen.exact);
  const EvaluationSummary screen_candidate =
      summarizeEvaluation(result.screen.candidate);
  const PairedMetrics screen_paired = pairedMetrics(result.screen);
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("could not open accessible-energy artifact");
  }
  output << std::setprecision(10)
         << "{\n  \"format\":\"drop7-accessible-energy-lab-v1\",\n"
         << "  \"publicStateOnly\":true,\n"
         << "  \"privilegedTapeAtDeployment\":false,\n"
         << "  \"mechanics\":{\"levelBonus\":7000},\n"
         << "  \"seeds\":{\"collectionStart\":" << kCollectionStart
         << ",\"collectionGames\":" << kCollectionGames
         << ",\"trainingGames\":" << kTrainingGames
         << ",\"heldoutGames\":" << kHeldoutGames
         << ",\"screenStart\":" << kScreenStart
         << ",\"screenGames\":" << kScreenGames
         << ",\"confirmationStart\":" << kConfirmationStart
         << ",\"confirmationGames\":" << kConfirmationGames
         << ",\"forbiddenFamiliesInspected\":false},\n"
         << "  \"continuationTargets\":{\"horizon\":"
         << kContinuationHorizon
         << ",\"rollin\":\"public exact-d3\","
            "\"components\":[\"survivedMoves\",\"numberedCleared\","
            "\"coversRevealed\"],\"scoreCalibration\":\"nonnegative-ridge\","
            "\"trainingExamples\":"
         << result.calibration_train.examples
         << ",\"heldoutExamples\":" << result.calibration_heldout.examples
         << ",\"calibrationLambda\":" << kCalibrationRidge
         << ",\"interceptAtTrainingMeans\":"
         << result.models.calibration.intercept
         << ",\"rawCoefficients\":{\"survivedMoves\":"
         << result.models.calibration.beta[0] /
                result.models.calibration.scale[0]
         << ",\"numberedCleared\":"
         << result.models.calibration.beta[1] /
                result.models.calibration.scale[1]
         << ",\"coversRevealed\":"
         << result.models.calibration.beta[2] /
                result.models.calibration.scale[2]
         << "},\"trainMetrics\":{\"mae\":"
         << result.calibration_train.mae << ",\"r2\":"
         << result.calibration_train.r2
         << "},\"heldoutMetrics\":{\"mae\":"
         << result.calibration_heldout.mae << ",\"r2\":"
         << result.calibration_heldout.r2 << "}},\n"
         << "  \"phaseResidualization\":{\"intercept\":"
         << result.models.phase.intercept << ",\"slope\":"
         << result.models.phase.slope << ",\"residualTargetSd\":"
         << result.models.residual_target_sd
         << ",\"deploymentScale\":" << full.deployment_scale
         << ",\"clipBeforeDeploymentScale\":" << full.clip << "},\n"
         << "  \"frozenResidual\":{\"ridgeLambda\":" << kFeatureRidge
         << ",\"rootTieBreaker\":false,\"features\":[";
  for (std::size_t feature = 0; feature < kFeatureCount; ++feature) {
    if (feature > 0) output << ',';
    const double raw_weight =
        full.ridge.beta[feature] / full.ridge.scale[feature] *
        kFeatureSpecs[feature].sign * full.deployment_scale;
    output << "{\"name\":\"" << kFeatureSpecs[feature].name
           << "\",\"group\":\""
           << kGroupNames[kFeatureSpecs[feature].group]
           << "\",\"frozenSign\":"
           << kFeatureSpecs[feature].sign
           << ",\"nonnegativeStandardizedMagnitude\":"
           << full.ridge.beta[feature]
           << ",\"deploymentRawWeight\":" << raw_weight
           << ",\"meanSignedRaw\":" << full.ridge.mean[feature]
           << ",\"scaleSignedRaw\":" << full.ridge.scale[feature] << '}';
  }
  output << "]},\n  \"heldoutPredictionAblations\":{\"phaseBaseline\":";
  writePredictionMetrics(output, result.baseline_heldout);
  output << ",\"variants\":[";
  for (std::size_t variant = 0; variant < result.models.variants.size();
       ++variant) {
    if (variant > 0) output << ',';
    output << "{\"name\":\"" << result.models.variants[variant].name
           << "\",\"train\":";
    writePredictionMetrics(output, result.variant_train[variant]);
    output << ",\"heldout\":";
    writePredictionMetrics(output, result.variant_heldout[variant]);
    output << '}';
  }
  output << "]},\n  \"heldoutActionRanking\":[";
  for (std::size_t ranking = 0; ranking < result.rankings.size(); ++ranking) {
    if (ranking > 0) output << ',';
    writeRankingMetrics(output, result.rankings[ranking]);
  }
  output << "],\n  \"screen\":{\"seedStart\":" << kScreenStart
         << ",\"exactD3\":";
  writeEvaluationSummary(output, screen_exact);
  output << ",\"energyD3\":";
  writeEvaluationSummary(output, screen_candidate);
  output << ",\"paired\":";
  writePairedMetrics(output, screen_paired);
  output << ",\"games\":";
  writeGamePairs(output, result.screen);
  output << ",\"passed\":" << (result.screen_passed ? "true" : "false")
         << ",\"wallSeconds\":" << result.screen.wall_seconds << "},\n"
         << "  \"confirmation\":";
  if (!result.confirmation_ran) {
    output << "null";
  } else {
    output << "{\"seedStart\":" << kConfirmationStart
           << ",\"exactD3\":";
    writeEvaluationSummary(output,
                           summarizeEvaluation(result.confirmation.exact));
    output << ",\"energyD3\":";
    writeEvaluationSummary(
        output, summarizeEvaluation(result.confirmation.candidate));
    output << ",\"paired\":";
    writePairedMetrics(output, pairedMetrics(result.confirmation));
    output << ",\"games\":";
    writeGamePairs(output, result.confirmation);
    output << ",\"passed\":"
           << (result.confirmation_passed ? "true" : "false")
           << ",\"wallSeconds\":" << result.confirmation.wall_seconds << '}';
  }
  output << ",\n  \"decision\":\""
         << (!result.screen_passed
                 ? "reject-screen"
                 : (result.confirmation_passed ? "advance"
                                               : "reject-confirmation"))
         << "\",\n  \"totalWallSeconds\":" << result.total_wall_seconds
         << "\n}\n";
}

ExperimentResult runExperiment() {
  const auto started = Clock::now();
  ExperimentResult result;
  result.rollins = collectRollins();
  result.examples = buildExamples(result.rollins);
  result.models = fitModels(result.examples);
  result.calibration_train =
      calibrationMetrics(result.examples, result.models.calibration, true);
  result.calibration_heldout =
      calibrationMetrics(result.examples, result.models.calibration, false);
  result.baseline_train =
      predictionMetrics(result.examples, result.models, nullptr, true);
  result.baseline_heldout =
      predictionMetrics(result.examples, result.models, nullptr, false);
  for (const ResidualModel& model : result.models.variants) {
    result.variant_train.push_back(
        predictionMetrics(result.examples, result.models, &model, true));
    result.variant_heldout.push_back(
        predictionMetrics(result.examples, result.models, &model, false));
  }
  result.rankings = evaluateRankings(result.rollins, result.models);
  result.screen = runEvaluationCohort(
      kScreenStart, kScreenGames, result.models.variants.front(), "energy-screen");
  const EvaluationSummary screen_exact =
      summarizeEvaluation(result.screen.exact);
  const EvaluationSummary screen_candidate =
      summarizeEvaluation(result.screen.candidate);
  result.screen_passed =
      screen_candidate.mean_score > screen_exact.mean_score &&
      screen_candidate.mean_moves > screen_exact.mean_moves;
  if (result.screen_passed) {
    result.confirmation_ran = true;
    result.confirmation = runEvaluationCohort(
        kConfirmationStart, kConfirmationGames,
        result.models.variants.front(), "energy-confirm");
    const EvaluationSummary confirmation_exact =
        summarizeEvaluation(result.confirmation.exact);
    const EvaluationSummary confirmation_candidate =
        summarizeEvaluation(result.confirmation.candidate);
    result.confirmation_passed =
        confirmation_candidate.mean_score > confirmation_exact.mean_score &&
        confirmation_candidate.mean_moves > confirmation_exact.mean_moves;
  }
  result.total_wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

bool selfTest(std::ostream& output) {
  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 0)] = 5;
  state.board[indexOf(4, 0)] = 5;
  state.board[indexOf(5, 1)] = kCracked;
  state.board[indexOf(5, 4)] = 2;
  state.next_disc = 6;
  state.moves_remaining = 2;
  const RawFeatures raw = extractRawFeatures(state);
  State mirrored = state;
  mirrored.board = cfpi::detail::mirrorBoard(state.board);
  const RawFeatures reflected = extractRawFeatures(mirrored);
  bool reflection_safe = true;
  bool finite = true;
  for (std::size_t feature = 0; feature < kFeatureCount; ++feature) {
    reflection_safe = reflection_safe &&
                      std::abs(raw[feature] - reflected[feature]) < 1.0e-9;
    finite = finite && std::isfinite(raw[feature]);
  }

  ResidualModel zero;
  zero.name = "zero";
  zero.enabled.fill(true);
  zero.ridge.enabled.fill(true);
  zero.ridge.scale.fill(1.0);
  zero.clip = 1.0;
  zero.deployment_scale = 0.0;
  SearchMetrics zero_metrics;
  const int zero_action = chooseEnergyAction(state, zero, &zero_metrics);
  cfpi::BehaviorOptions exact;
  exact.max_depth = 3;
  exact.chance_samples = kChanceSamples;
  exact.max_work = kSearchMaximumWork;
  exact.max_cache_entries = kSearchMaximumCacheEntries;
  const int exact_action = cfpi::chooseBehaviorAction(state, exact);
  const bool exact_parity = zero_action == exact_action &&
                            zero_metrics.completed_depth == 3;

  ResidualModel synthetic = zero;
  synthetic.name = "synthetic";
  synthetic.deployment_scale = 1.0;
  synthetic.clip = 10'000.0;
  synthetic.ridge.beta.fill(25.0);
  const int first = chooseEnergyAction(state, synthetic);
  const int repeat = chooseEnergyAction(state, synthetic);
  const int mirror_action = chooseEnergyAction(mirrored, synthetic);
  State metadata_changed = state;
  metadata_changed.score = 999'999;
  metadata_changed.level = 99;
  metadata_changed.moves_played = 777;
  const int metadata_action =
      chooseEnergyAction(metadata_changed, synthetic);
  const bool deterministic = first == repeat;
  const bool search_reflection_safe =
      mirror_action == kBoardSize - 1 - first;
  const bool public_only = metadata_action == first;
  const bool legal = isLegal(state.board, first);

  std::vector<std::array<double, 2>> ridge_inputs{{
      {{0.0, 0.0}}, {{1.0, 1.0}}, {{2.0, 0.0}}, {{3.0, 1.0}},
  }};
  std::vector<double> ridge_targets{{0.0, 1.0, 2.0, 3.0}};
  std::array<bool, 2> ridge_enabled{{true, true}};
  const RidgeModel<2> ridge = fitNonnegativeRidge(
      ridge_inputs, ridge_targets, 0.1, ridge_enabled);
  const bool ridge_safe = ridge.beta[0] > 0.0 && ridge.beta[1] >= 0.0 &&
                          std::isfinite(ridge.predict({{1.5, 0.5}}));

  const std::uint32_t simulation_seed = 0x51f7'a11eu;
  const SimulatedContinuation simulation_first =
      simulateContinuation(state, first, simulation_seed);
  const SimulatedContinuation simulation_repeat =
      simulateContinuation(state, first, simulation_seed);
  const bool simulation_deterministic =
      simulation_first.survived == simulation_repeat.survived &&
      simulation_first.cleared == simulation_repeat.cleared &&
      simulation_first.revealed == simulation_repeat.revealed &&
      simulation_first.score == simulation_repeat.score;
  const bool seed_guard =
      (kCollectionStart & 0xff00'0000u) == 0x3d00'0000u &&
      (kScreenStart & 0xff00'0000u) == 0x3e00'0000u &&
      (kConfirmationStart & 0xff00'0000u) == 0x3e00'0000u &&
      (kCollectionStart >> 24) != 0x7du &&
      (kCollectionStart >> 24) != 0xd7u &&
      (kScreenStart >> 24) != 0x7du && (kScreenStart >> 24) != 0xd7u;
  const bool passed = kLevelBonus == 7'000 && reflection_safe && finite &&
                      exact_parity && deterministic &&
                      search_reflection_safe && public_only && legal &&
                      ridge_safe && simulation_deterministic && seed_guard;
  output << "ACCESSIBLE_ENERGY_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"levelBonus\":" << kLevelBonus
         << ",\"featureReflectionSafe\":"
         << (reflection_safe ? "true" : "false")
         << ",\"exactZeroResidualParity\":"
         << (exact_parity ? "true" : "false")
         << ",\"searchReflectionSafe\":"
         << (search_reflection_safe ? "true" : "false")
         << ",\"deterministic\":"
         << (deterministic && simulation_deterministic ? "true" : "false")
         << ",\"publicStateOnly\":"
         << (public_only ? "true" : "false")
         << ",\"legalFullWidthRoot\":" << (legal ? "true" : "false")
         << ",\"nonnegativeRidge\":"
         << (ridge_safe ? "true" : "false")
         << ",\"seedGuard\":" << (seed_guard ? "true" : "false")
         << "}\n";
  return passed;
}

int run(const std::string& artifact, std::ostream& output) {
  const ExperimentResult result = runExperiment();
  writeArtifact(result, artifact);
  const EvaluationSummary exact = summarizeEvaluation(result.screen.exact);
  const EvaluationSummary candidate =
      summarizeEvaluation(result.screen.candidate);
  const PairedMetrics paired = pairedMetrics(result.screen);
  output << std::fixed << std::setprecision(3)
         << "ACCESSIBLE_ENERGY_RESULT {\"publicStateOnly\":true"
         << ",\"trainingExamples\":"
         << result.calibration_train.examples
         << ",\"heldoutExamples\":"
         << result.calibration_heldout.examples
         << ",\"screenExactScore\":" << exact.mean_score
         << ",\"screenExactMoves\":" << exact.mean_moves
         << ",\"screenCandidateScore\":" << candidate.mean_score
         << ",\"screenCandidateMoves\":" << candidate.mean_moves
         << ",\"screenScoreDelta\":" << paired.mean_score_delta
         << ",\"screenMoveDelta\":" << paired.mean_move_delta
         << ",\"screenPassed\":"
         << (result.screen_passed ? "true" : "false")
         << ",\"confirmationRan\":"
         << (result.confirmation_ran ? "true" : "false")
         << ",\"confirmationPassed\":"
         << (result.confirmation_passed ? "true" : "false")
         << ",\"artifact\":\"" << artifact << "\"}\n";
  return 0;
}

}  // namespace drop7::accessible_energy

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
      return drop7::accessible_energy::selfTest(std::cout) ? 0 : 1;
    }
    if (argc >= 2 && std::string(argv[1]) == "--run") {
      std::string artifact = "/tmp/drop7-accessible-energy-lab.json";
      if (argc == 4 && std::string(argv[2]) == "--output") {
        artifact = argv[3];
      } else if (argc != 2) {
        throw std::invalid_argument("--run accepts only optional --output PATH");
      }
      return drop7::accessible_energy::run(artifact, std::cout);
    }
    std::cerr << "usage: drop7_accessible_energy_lab --self-test | --run "
                 "[--output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_accessible_energy_lab: " << error.what() << '\n';
    return 1;
  }
}
