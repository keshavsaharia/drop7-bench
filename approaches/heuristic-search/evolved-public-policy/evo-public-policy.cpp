// Fast, public-state-only Drop7 policy optimized directly on complete games.
//
// This lab deliberately owns no shared policy code.  It uses the shared engine
// and public chance sampler, but the policy boundary is only board, next_disc,
// and moves_remaining.  Complete-game common-random-number (CRN) batches tune
// a compact linear successor evaluator with a diagonal cross-entropy method.

#include "../../../src/core/native/public-behavior.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
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
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#endif

namespace drop7::evo_public_policy {

namespace detail = drop7::cfpi::detail;

constexpr std::uint32_t kFittingSeedStart = 0x3d50'0000u;
constexpr std::uint32_t kFittingSeedEnd = 0x3d52'0000u;
constexpr std::uint32_t kProbeSeedStart = 0x4d50'0000u;
constexpr std::uint32_t kProbeSeedEnd = 0x4d50'0080u;
constexpr std::uint32_t kPolicySeed = 0xe701'5eedu;
constexpr std::uint64_t kOptimizerSeed = 0x6576'6f2d'6372'6e31ull;
constexpr std::uint64_t kCheckpointMagic = 0x4452'3745'564f'5031ull;
constexpr std::uint32_t kCheckpointVersion = 2;
constexpr int kBaseFeatureCount = 33;
constexpr int kPhaseCount = kMovesPerLevel;
constexpr int kFeatureCount = kBaseFeatureCount * kPhaseCount;
constexpr int kDefaultSamples = 7;

static_assert(kLevelBonus == 17'000);
static_assert((kFittingSeedStart >> 24) == 0x3du);
static_assert((kProbeSeedStart >> 24) == 0x4du);
static_assert(kFittingSeedEnd <= 0x3d52'0000u);
static_assert(kProbeSeedEnd <= 0x4d50'0080u);

using Features = std::array<double, kFeatureCount>;
using Weights = std::array<double, kFeatureCount>;

constexpr std::array<std::string_view, kBaseFeatureCount> kFeatureNames{{
    "openColumns",
    "heightLoad",
    "solidCells",
    "crackedCells",
    "numberedCells",
    "highLowNumbers",
    "directPotential",
    "latentChainPotential",
    "crackedExposure",
    "solidExposure",
    "adjacentOnes",
    "tripleTwos",
    "deadLowNumbers",
    "projectedOccupancyDebt",
    "residualCoverDebt",
    "coverAltitudeDebt",
    "imminentCoverAltitudeDebt",
    "peakHeightRisk",
    "lowCapLoad",
    "adjacentLowCapLoad",
    "quietBuildOptions",
    "quietDirectGain",
    "triggerReadiness",
    "riseTriggerReadiness",
    "scoreDelta",
    "numberedCleared",
    "coversRevealed",
    "waveCount",
    "maximumChainDepth",
    "clearedBoard",
    "levelAdvanced",
    "gameOver",
    "nextDiscVerticalOptions",
}};

struct Options {
  enum class Mode { kSelfTest, kBaseline, kTrain, kEvaluate };
  Mode mode = Mode::kSelfTest;
  int games = 32;
  int maximum_moves = 1'000;
  int samples = kDefaultSamples;
  bool samples_explicit = false;
  int threads = 4;
  int generations = 40;
  int population = 25;
  int batch_games = 24;
  int tournament_games = 64;
  int elite = 6;
  int rollout_horizon = 0;
  int rollout_scenarios = 7;
  int search_depth = 0;
  int search_width = 2;
  std::uint32_t seed_start = kFittingSeedStart;
  std::string checkpoint = "/tmp/drop7-evo-public-policy.bin";
  bool summary_only = false;
};

struct PolicyMetrics {
  std::uint64_t sampled_transitions = 0;
  std::uint64_t feature_evaluations = 0;
};

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool terminal = false;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
  int maximum_chain = 0;
  std::uint64_t sampled_transitions = 0;
};

struct Evaluation {
  std::vector<GameResult> games;
  double objective = -std::numeric_limits<double>::infinity();
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double lower_quartile_score = 0.0;
  double lower_quartile_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double sampled_transitions_per_move = 0.0;
  int censored = 0;
  double seconds = 0.0;
};

struct Candidate {
  Weights weights{};
  Evaluation evaluation;
};

class SplitMix64 {
 public:
  explicit SplitMix64(std::uint64_t state) : state_(state) {}

  std::uint64_t nextBits() {
    std::uint64_t value = (state_ += 0x9e37'79b9'7f4a'7c15ull);
    value = (value ^ (value >> 30)) * 0xbf58'476d'1ce4'e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d0'49bb'1331'11ebull;
    return value ^ (value >> 31);
  }

  double unit() {
    return static_cast<double>(nextBits() >> 11) /
           static_cast<double>(std::uint64_t{1} << 53);
  }

  double normal() {
    if (has_spare_) {
      has_spare_ = false;
      return spare_;
    }
    const double first = std::max(unit(), std::numeric_limits<double>::min());
    const double second = unit();
    const double radius = std::sqrt(-2.0 * std::log(first));
    const double angle = 6.283185307179586476925286766559 * second;
    spare_ = radius * std::sin(angle);
    has_spare_ = true;
    return radius * std::cos(angle);
  }

 private:
  std::uint64_t state_;
  bool has_spare_ = false;
  double spare_ = 0.0;
};

std::array<double, kBaseFeatureCount> baseInitialWeights() {
  // Phase-safety coefficients transformed into the normalized feature basis
  // and divided by 10,000.  Transition terms start conservatively; evolution
  // is free to alter every coordinate independently.
  return std::array<double, kBaseFeatureCount>{{
      0.126,    // 180 * 7 / 10000
      -0.343,   // -10 * 343 / 10000
      -3.038,   // -620 * 49 / 10000
      -1.078,   // -220 * 49 / 10000
      -0.0882,  // -18 * 49 / 10000
      -0.09,
      0.36,
      0.80,
      0.54,
      0.194,
      -0.275,
      -0.375,
      -0.06,
      -9.60,
      -8.00,
      -5.00,
      -7.00,
      -22.50,
      -3.60,
      -3.60,
      0.21,
      0.30,
      0.60,
      1.20,
      1.70,
      0.00,
      0.30,
      0.00,
      0.00,
      7.00,
      0.00,
      -100.0,
      0.154,
  }};
}

Weights initialWeights() {
  const auto baseline = baseInitialWeights();
  Weights result{};
  // Index zero is the move immediately before a row rise; index four is the
  // first move after one.  The initialization encodes build/release behavior,
  // while every phase/feature coordinate remains independently
  // evolvable.  Early-cycle moves value stored energy; late-cycle moves value
  // release and safety.
  constexpr std::array<double, kPhaseCount> build{{0.55, 0.80, 1.15, 1.45,
                                                    1.80}};
  constexpr std::array<double, kPhaseCount> release{{1.40, 1.10, 0.80, 0.55,
                                                      0.35}};
  constexpr std::array<double, kPhaseCount> safety{{1.60, 1.30, 1.05, 0.85,
                                                     0.70}};
  for (int phase = 0; phase < kPhaseCount; ++phase) {
    for (int feature = 0; feature < kBaseFeatureCount; ++feature) {
      result[phase * kBaseFeatureCount + feature] = baseline[feature];
    }
    for (const int feature : {6, 7, 20, 21}) {
      result[phase * kBaseFeatureCount + feature] *= build[phase];
    }
    for (const int feature : {24, 25, 26, 27, 28, 29}) {
      result[phase * kBaseFeatureCount + feature] *= release[phase];
    }
    for (const int feature : {1, 5, 10, 11, 12, 13, 14, 15, 16, 17, 18,
                              19}) {
      result[phase * kBaseFeatureCount + feature] *= safety[phase];
    }
    result[phase * kBaseFeatureCount + 31] = -100.0;
  }
  return result;
}

Weights initialSigma() {
  const Weights baseline = initialWeights();
  Weights sigma{};
  for (int index = 0; index < kFeatureCount; ++index) {
    sigma[index] = std::clamp(std::abs(baseline[index]) * 0.30, 0.18, 3.0);
  }
  // Terminal utility is a safety invariant, not an evolutionary temptation.
  for (int phase = 0; phase < kPhaseCount; ++phase) {
    sigma[phase * kBaseFeatureCount + 31] = 0.0;
  }
  return sigma;
}

State publicState(const State& input) {
  State result;
  result.board = input.board;
  result.next_disc = input.next_disc;
  result.moves_remaining = input.moves_remaining;
  result.game_over = input.game_over;
  return result;
}

double nextDiscVerticalOptions(const State& state) {
  const auto heights = detail::columnHeights(state.board);
  double result = 0.0;
  for (int column = 0; column < kBoardSize; ++column) {
    if (heights[column] < kBoardSize &&
        heights[column] + 1 == state.next_disc) {
      result += 1.0;
    }
  }
  return result;
}

std::array<double, kBaseFeatureCount> extractBaseFeatures(
    const MoveResult& move) {
  const State state = publicState(move.state);
  const detail::PhaseFeatures f = detail::extractPhaseFeatures(state);
  std::uint64_t cleared = 0;
  std::uint64_t revealed = 0;
  int maximum_chain = 0;
  for (const Wave& wave : move.waves) {
    cleared += static_cast<std::uint64_t>(wave.cleared);
    revealed += static_cast<std::uint64_t>(wave.revealed);
    maximum_chain = std::max(maximum_chain, wave.depth);
  }
  return std::array<double, kBaseFeatureCount>{{
      f.open_columns / 7.0,
      f.height_load / 343.0,
      f.solid_cells / 49.0,
      f.cracked_cells / 49.0,
      f.numbered_cells / 49.0,
      f.high_low_numbers / 10.0,
      f.direct_potential / 10.0,
      f.latent_chain_potential / 10.0,
      f.cracked_exposure / 10.0,
      f.solid_exposure / 10.0,
      f.adjacent_ones / 5.0,
      f.triple_twos / 5.0,
      f.dead_low_numbers / 5.0,
      f.projected_occupancy_debt / 400.0,
      f.residual_cover_debt / 400.0,
      f.cover_altitude_debt / 1'000.0,
      f.imminent_cover_altitude_debt / 1'000.0,
      f.peak_height_risk / 125.0,
      f.low_cap_load / 300.0,
      f.adjacent_low_cap_load / 200.0,
      f.quiet_build_options / 7.0,
      f.quiet_direct_gain / 5.0,
      f.trigger_readiness / 10.0,
      f.rise_trigger_readiness / 10.0,
      static_cast<double>(move.score_delta) / 17'000.0,
      static_cast<double>(cleared) / 20.0,
      static_cast<double>(revealed) / 10.0,
      static_cast<double>(move.waves.size()) / 8.0,
      static_cast<double>(maximum_chain) / 8.0,
      move.cleared_board ? 1.0 : 0.0,
      move.level_advanced ? 1.0 : 0.0,
      move.state.game_over ? 1.0 : 0.0,
      nextDiscVerticalOptions(state) / 7.0,
  }};
}

Features extractFeatures(const MoveResult& move, int decision_phase) {
  if (decision_phase < 1 || decision_phase > kMovesPerLevel) {
    throw std::invalid_argument("decision phase outside Hardcore cycle");
  }
  const auto base = extractBaseFeatures(move);
  Features result{};
  const int offset = (decision_phase - 1) * kBaseFeatureCount;
  std::copy(base.begin(), base.end(), result.begin() + offset);
  return result;
}

double dot(const Weights& weights, const Features& features) {
  double result = 0.0;
  for (int index = 0; index < kFeatureCount; ++index) {
    result += weights[index] * features[index];
  }
  return result;
}

int chooseAction(const State& input, const Weights& weights, int samples,
                 PolicyMetrics* metrics = nullptr) {
  if (input.game_over) return -1;
  if (samples < 1 || samples > 32) {
    throw std::invalid_argument("samples must be from 1 to 32");
  }
  bool mirrored = false;
  const State canonical = detail::canonicalState(publicState(input), mirrored);
  const std::uint32_t state_seed =
      detail::scenarioSeedForState(canonical, kPolicySeed, 1);
  int best_action = -1;
  double best_value = -std::numeric_limits<double>::infinity();
  for (const int action : detail::kColumnOrder) {
    if (!isLegal(canonical.board, action)) continue;
    double value = 0.0;
    for (int sample = 0; sample < samples; ++sample) {
      detail::StratifiedRandom random{state_seed, sample, samples, 0};
      MoveResult move;
      if (!detail::playMoveSampled(canonical, action, random, move)) {
        value += -100.0;
        continue;
      }
      if (!move.state.game_over) {
        move.state.next_disc =
            detail::sampledNextDisc(state_seed, sample, samples);
      }
      value += dot(weights,
                   extractFeatures(move, canonical.moves_remaining));
      if (metrics != nullptr) {
        ++metrics->sampled_transitions;
        ++metrics->feature_evaluations;
      }
    }
    value /= static_cast<double>(samples);
    if (value > best_value) {
      best_value = value;
      best_action = action;
    }
  }
  if (best_action < 0) best_action = centerFirstMove(canonical.board);
  return mirrored && best_action >= 0 ? kBoardSize - 1 - best_action
                                      : best_action;
}

int chooseRolloutAction(const State& input, const Weights& weights,
                        int policy_samples, int horizon, int scenarios,
                        PolicyMetrics* metrics = nullptr) {
  if (horizon <= 0) {
    return chooseAction(input, weights, policy_samples, metrics);
  }
  if (scenarios < 1 || scenarios > 32) {
    throw std::invalid_argument("rollout scenarios must be from 1 to 32");
  }
  bool mirrored = false;
  const State canonical = detail::canonicalState(publicState(input), mirrored);
  const std::uint32_t root_seed =
      detail::scenarioSeedForState(canonical, 0xe701'7011u, horizon);
  int best_action = -1;
  double best_value = -std::numeric_limits<double>::infinity();
  for (const int root_action : detail::kColumnOrder) {
    if (!isLegal(canonical.board, root_action)) continue;
    double action_value = 0.0;
    for (int scenario = 0; scenario < scenarios; ++scenario) {
      State state = canonical;
      double scenario_value = 0.0;
      for (int step = 0; step < horizon && !state.game_over; ++step) {
        const int action = step == 0
                               ? root_action
                               : chooseAction(state, weights, policy_samples,
                                              metrics);
        const std::uint32_t step_seed = mix32(
            root_seed ^
            (static_cast<std::uint32_t>(step + 1) * 0x7f4a'7c15u));
        detail::StratifiedRandom random{step_seed, scenario, scenarios, 0};
        MoveResult move;
        if (!detail::playMoveSampled(state, action, random, move)) {
          scenario_value += -100.0;
          state.game_over = true;
          break;
        }
        if (metrics != nullptr) ++metrics->sampled_transitions;
        scenario_value +=
            static_cast<double>(move.score_delta) / 10'000.0;
        state = publicState(move.state);
        if (!state.game_over) {
          state.next_disc =
              detail::sampledNextDisc(step_seed, scenario, scenarios);
        }
      }
      if (state.game_over) {
        scenario_value += -100.0;
      } else {
        // At a bounded horizon retain the same observable successor evaluator;
        // unlike a tape-conditioned plan, the continuation policy above is a
        // single function of each newly observed state.
        MoveResult leaf;
        leaf.state = state;
        scenario_value +=
            dot(weights, extractFeatures(leaf, state.moves_remaining));
        if (metrics != nullptr) ++metrics->feature_evaluations;
      }
      action_value += scenario_value;
    }
    action_value /= static_cast<double>(scenarios);
    if (action_value > best_value) {
      best_value = action_value;
      best_action = root_action;
    }
  }
  if (best_action < 0) best_action = centerFirstMove(canonical.board);
  return mirrored && best_action >= 0 ? kBoardSize - 1 - best_action
                                      : best_action;
}

struct SearchMetrics {
  std::uint64_t transitions = 0;
  std::uint64_t leaves = 0;
};

double searchBestFuture(const State& state, const Weights& weights, int depth,
                        int width, int samples, SearchMetrics& metrics);

double searchActionValue(const State& state, int action,
                         const Weights& weights, int depth, int width,
                         int samples, SearchMetrics& metrics) {
  const std::uint32_t state_seed =
      detail::scenarioSeedForState(state, 0xe701'5ea2u, depth);
  double value = 0.0;
  for (int sample = 0; sample < samples; ++sample) {
    detail::StratifiedRandom random{state_seed, sample, samples, 0};
    MoveResult move;
    if (!detail::playMoveSampled(state, action, random, move)) {
      value += -100.0;
      continue;
    }
    ++metrics.transitions;
    double outcome = static_cast<double>(move.score_delta) / 10'000.0;
    if (move.state.game_over) {
      outcome += -100.0;
    } else {
      move.state = publicState(move.state);
      move.state.next_disc =
          detail::sampledNextDisc(state_seed, sample, samples);
      if (depth <= 1) {
        outcome += dot(weights,
                       extractFeatures(move, move.state.moves_remaining));
        ++metrics.leaves;
      } else {
        outcome += searchBestFuture(move.state, weights, depth - 1, width,
                                    samples, metrics);
      }
    }
    value += outcome;
  }
  return value / static_cast<double>(samples);
}

double searchBestFuture(const State& state, const Weights& weights, int depth,
                        int width, int samples, SearchMetrics& metrics) {
  struct RankedAction {
    int action = -1;
    double rank_value = -std::numeric_limits<double>::infinity();
  };
  std::array<RankedAction, kBoardSize> actions{};
  int count = 0;
  for (const int action : detail::kColumnOrder) {
    if (!isLegal(state.board, action)) continue;
    actions[count++] = RankedAction{
        action, searchActionValue(state, action, weights, 1, width, samples,
                                  metrics)};
  }
  std::stable_sort(actions.begin(), actions.begin() + count,
                   [](const RankedAction& first, const RankedAction& second) {
                     return first.rank_value > second.rank_value;
                   });
  const int expanded = std::min(width, count);
  double best = -std::numeric_limits<double>::infinity();
  for (int index = 0; index < expanded; ++index) {
    const double candidate = searchActionValue(
        state, actions[index].action, weights, depth, width, samples, metrics);
    best = std::max(best, candidate);
  }
  return std::isfinite(best) ? best : -100.0;
}

int chooseSearchAction(const State& input, const Weights& weights, int depth,
                       int width, int samples,
                       SearchMetrics* output_metrics = nullptr) {
  if (depth < 1 || depth > 8 || width < 1 || width > kBoardSize ||
      samples < 1 || samples > 7) {
    throw std::invalid_argument("invalid selective-search configuration");
  }
  bool mirrored = false;
  const State canonical = detail::canonicalState(publicState(input), mirrored);
  SearchMetrics metrics;
  int best_action = -1;
  double best_value = -std::numeric_limits<double>::infinity();
  for (const int action : detail::kColumnOrder) {
    if (!isLegal(canonical.board, action)) continue;
    const double value = searchActionValue(canonical, action, weights, depth,
                                           width, samples, metrics);
    if (value > best_value) {
      best_value = value;
      best_action = action;
    }
  }
  if (output_metrics != nullptr) *output_metrics = metrics;
  if (best_action < 0) best_action = centerFirstMove(canonical.board);
  return mirrored && best_action >= 0 ? kBoardSize - 1 - best_action
                                      : best_action;
}

GameResult playGame(std::uint32_t seed, const Weights& weights, int samples,
                    int maximum_moves, int rollout_horizon = 0,
                    int rollout_scenarios = 7) {
  if (maximum_moves < 1) throw std::invalid_argument("maximum moves invalid");
  GameResult result;
  result.seed = seed;
  State state = initialHeadlessState(seed);
  PolicyMetrics metrics;
  while (!state.game_over && state.moves_played < maximum_moves) {
    const State before = state;
    const int action = chooseRolloutAction(
        before, weights, samples, rollout_horizon, rollout_scenarios, &metrics);
    if (!isLegal(before.board, action)) {
      throw std::runtime_error("public evolution policy returned illegal move");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("headless engine rejected legal policy move");
    }
    for (const Wave& wave : move.waves) {
      result.clears += static_cast<std::uint64_t>(wave.cleared);
      result.reveals += static_cast<std::uint64_t>(wave.revealed);
      result.maximum_chain = std::max(result.maximum_chain, wave.depth);
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.terminal = state.game_over;
  result.sampled_transitions = metrics.sampled_transitions;
  return result;
}

GameResult playSearchGame(std::uint32_t seed, const Weights& weights,
                          int samples, int maximum_moves, int search_depth,
                          int search_width) {
  GameResult result;
  result.seed = seed;
  State state = initialHeadlessState(seed);
  while (!state.game_over && state.moves_played < maximum_moves) {
    SearchMetrics metrics;
    const int action = chooseSearchAction(state, weights, search_depth,
                                          search_width, samples, &metrics);
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("selective search returned illegal move");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("headless engine rejected search move");
    }
    for (const Wave& wave : move.waves) {
      result.clears += static_cast<std::uint64_t>(wave.cleared);
      result.reveals += static_cast<std::uint64_t>(wave.revealed);
      result.maximum_chain = std::max(result.maximum_chain, wave.depth);
    }
    result.sampled_transitions += metrics.transitions;
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.terminal = state.game_over;
  return result;
}

double lowerFractionMean(std::vector<double> values, double fraction) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const double mass = fraction * static_cast<double>(values.size());
  const int whole = static_cast<int>(std::floor(mass));
  const double partial = mass - whole;
  double sum = 0.0;
  for (int index = 0; index < whole; ++index) sum += values[index];
  if (whole < static_cast<int>(values.size())) {
    sum += partial * values[whole];
  }
  return mass > 0.0 ? sum / mass : values.front();
}

void summarize(Evaluation& evaluation) {
  if (evaluation.games.empty()) return;
  std::vector<double> scores;
  std::vector<double> moves;
  double total_score = 0.0;
  double total_moves = 0.0;
  double total_clears = 0.0;
  double total_reveals = 0.0;
  double total_sampled_transitions = 0.0;
  for (const GameResult& game : evaluation.games) {
    total_score += static_cast<double>(game.score);
    total_moves += game.moves;
    total_clears += static_cast<double>(game.clears);
    total_reveals += static_cast<double>(game.reveals);
    total_sampled_transitions +=
        static_cast<double>(game.sampled_transitions);
    scores.push_back(static_cast<double>(game.score));
    moves.push_back(game.moves);
    if (!game.terminal) ++evaluation.censored;
  }
  const double count = static_cast<double>(evaluation.games.size());
  evaluation.mean_score = total_score / count;
  evaluation.mean_moves = total_moves / count;
  evaluation.lower_quartile_score = lowerFractionMean(scores, 0.25);
  evaluation.lower_quartile_moves = lowerFractionMean(moves, 0.25);
  evaluation.clears_per_move = total_moves > 0 ? total_clears / total_moves : 0;
  evaluation.reveals_per_move = total_moves > 0 ? total_reveals / total_moves : 0;
  evaluation.sampled_transitions_per_move =
      total_moves > 0 ? total_sampled_transitions / total_moves : 0;
  // The level award makes score and lifetime nearly collinear.  Retaining both
  // rewards unusually productive chains without permitting one lucky game to
  // dominate: forty percent of fitness is the lower quartile.
  const double mean_utility = evaluation.mean_moves +
                              evaluation.mean_score / 17'000.0;
  const double tail_utility = evaluation.lower_quartile_moves +
                              evaluation.lower_quartile_score / 17'000.0;
  evaluation.objective = 0.60 * mean_utility + 0.40 * tail_utility;
}

Evaluation evaluate(const Weights& weights, std::uint32_t seed_start, int games,
                    int samples, int maximum_moves, int rollout_horizon = 0,
                    int rollout_scenarios = 7) {
  if (games < 1) throw std::invalid_argument("games must be positive");
  Evaluation result;
  result.games.reserve(static_cast<std::size_t>(games));
  const auto started = std::chrono::steady_clock::now();
  for (int game = 0; game < games; ++game) {
    result.games.push_back(playGame(seed_start + static_cast<std::uint32_t>(game),
                                    weights, samples, maximum_moves,
                                    rollout_horizon, rollout_scenarios));
  }
  result.seconds = std::chrono::duration<double>(
                       std::chrono::steady_clock::now() - started)
                       .count();
  summarize(result);
  return result;
}

Evaluation evaluateSearch(const Weights& weights, std::uint32_t seed_start,
                          int games, int samples, int maximum_moves,
                          int search_depth, int search_width) {
  Evaluation result;
  result.games.reserve(static_cast<std::size_t>(games));
  const auto started = std::chrono::steady_clock::now();
  for (int game = 0; game < games; ++game) {
    result.games.push_back(playSearchGame(
        seed_start + static_cast<std::uint32_t>(game), weights, samples,
        maximum_moves, search_depth, search_width));
  }
  result.seconds = std::chrono::duration<double>(
                       std::chrono::steady_clock::now() - started)
                       .count();
  summarize(result);
  return result;
}

void evaluatePopulation(std::vector<Candidate>& candidates,
                        std::uint32_t seed_start, int games, int samples,
                        int maximum_moves, int threads,
                        int rollout_horizon = 0,
                        int rollout_scenarios = 7) {
  std::atomic<std::size_t> next{0};
  const int worker_count = std::max(1, std::min(
      threads, static_cast<int>(candidates.size())));
  std::vector<std::thread> workers;
  workers.reserve(static_cast<std::size_t>(worker_count));
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.emplace_back([&]() {
      for (;;) {
        const std::size_t index = next.fetch_add(1);
        if (index >= candidates.size()) break;
        candidates[index].evaluation = evaluate(
            candidates[index].weights, seed_start, games, samples,
            maximum_moves, rollout_horizon, rollout_scenarios);
      }
    });
  }
  for (std::thread& worker : workers) worker.join();
}

std::uint64_t fingerprint(const Weights& weights) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (double weight : weights) {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(weight);
    for (int byte = 0; byte < 8; ++byte) {
      hash ^= (bits >> (byte * 8)) & 0xffu;
      hash *= 0x0000'0100'0000'01b3ull;
    }
  }
  return hash;
}

void saveCheckpoint(const std::string& path, const Weights& weights,
                    int samples) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("unable to create checkpoint");
  const std::uint64_t magic = kCheckpointMagic;
  const std::uint32_t version = kCheckpointVersion;
  const std::uint32_t count = kFeatureCount;
  const std::uint32_t stored_samples = static_cast<std::uint32_t>(samples);
  const std::uint32_t reserved = 0;
  const std::uint64_t stored_fingerprint = fingerprint(weights);
  output.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
  output.write(reinterpret_cast<const char*>(&version), sizeof(version));
  output.write(reinterpret_cast<const char*>(&count), sizeof(count));
  output.write(reinterpret_cast<const char*>(&stored_samples),
               sizeof(stored_samples));
  output.write(reinterpret_cast<const char*>(&reserved), sizeof(reserved));
  output.write(reinterpret_cast<const char*>(&stored_fingerprint),
               sizeof(stored_fingerprint));
  output.write(reinterpret_cast<const char*>(weights.data()),
               static_cast<std::streamsize>(sizeof(double) * weights.size()));
  if (!output) throw std::runtime_error("unable to write checkpoint");
}

std::pair<Weights, int> loadCheckpoint(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("unable to open checkpoint");
  std::uint64_t magic = 0;
  std::uint32_t version = 0;
  std::uint32_t count = 0;
  std::uint32_t samples = 0;
  std::uint32_t reserved = 0;
  std::uint64_t stored_fingerprint = 0;
  input.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  input.read(reinterpret_cast<char*>(&version), sizeof(version));
  input.read(reinterpret_cast<char*>(&count), sizeof(count));
  input.read(reinterpret_cast<char*>(&samples), sizeof(samples));
  input.read(reinterpret_cast<char*>(&reserved), sizeof(reserved));
  input.read(reinterpret_cast<char*>(&stored_fingerprint),
             sizeof(stored_fingerprint));
  Weights weights{};
  input.read(reinterpret_cast<char*>(weights.data()),
             static_cast<std::streamsize>(sizeof(double) * weights.size()));
  char trailing = 0;
  const bool has_trailing = static_cast<bool>(input.read(&trailing, 1));
  if (magic != kCheckpointMagic || version != kCheckpointVersion ||
      count != kFeatureCount || reserved != 0 || samples < 1 || samples > 32 ||
      has_trailing || fingerprint(weights) != stored_fingerprint) {
    throw std::runtime_error("invalid evolution checkpoint");
  }
  return {weights, static_cast<int>(samples)};
}

void printEvaluation(std::string_view label, const Evaluation& evaluation,
                     const Weights& weights) {
  std::cout << std::fixed << std::setprecision(6)
            << "{\"label\":\"" << label << "\",\"games\":"
            << evaluation.games.size() << ",\"meanScore\":"
            << evaluation.mean_score << ",\"meanMoves\":"
            << evaluation.mean_moves << ",\"lowerQuartileScore\":"
            << evaluation.lower_quartile_score
            << ",\"lowerQuartileMoves\":"
            << evaluation.lower_quartile_moves << ",\"clearsPerMove\":"
            << evaluation.clears_per_move << ",\"revealsPerMove\":"
            << evaluation.reveals_per_move << ",\"censored\":"
            << evaluation.censored
            << ",\"sampledTransitionsPerMove\":"
            << evaluation.sampled_transitions_per_move
            << ",\"objective\":"
            << evaluation.objective << ",\"seconds\":"
            << evaluation.seconds << ",\"fingerprint\":\"0x" << std::hex
            << fingerprint(weights) << std::dec << "\"}\n";
}

void printWeights(const Weights& weights) {
  std::cout << "{\"weights\":{";
  for (int index = 0; index < kFeatureCount; ++index) {
    if (index != 0) std::cout << ',';
    const int phase = index / kBaseFeatureCount + 1;
    const int feature = index % kBaseFeatureCount;
    std::cout << '\"' << kFeatureNames[feature] << "_p" << phase
              << "\":" << std::fixed << std::setprecision(9)
              << weights[index];
  }
  std::cout << "}}\n";
}

Weights train(const Options& options) {
  if (options.population < 5 || options.population % 2 == 0) {
    throw std::invalid_argument("population must be odd and at least five");
  }
  if (options.elite < 2 || options.elite >= options.population) {
    throw std::invalid_argument("elite count invalid");
  }
  if (options.batch_games < 2 || options.tournament_games < 4) {
    throw std::invalid_argument("training cohorts too small");
  }
  const std::uint64_t fitting_used =
      static_cast<std::uint64_t>(options.generations) * options.batch_games;
  const std::uint32_t tournament_start = 0x3d51'0000u;
  if (kFittingSeedStart + fitting_used > tournament_start ||
      tournament_start + static_cast<std::uint32_t>(options.tournament_games) >
          kFittingSeedEnd) {
    throw std::invalid_argument("training configuration exceeds reserved seeds");
  }

  Weights mean = initialWeights();
  Weights sigma = initialSigma();
  SplitMix64 random(kOptimizerSeed);
  std::vector<Weights> archive;
  archive.push_back(mean);
  const auto started = std::chrono::steady_clock::now();

  for (int generation = 0; generation < options.generations; ++generation) {
    std::vector<Candidate> population(
        static_cast<std::size_t>(options.population));
    population[0].weights = mean;
    for (int pair = 0; pair < (options.population - 1) / 2; ++pair) {
      Weights positive = mean;
      Weights negative = mean;
      for (int index = 0; index < kFeatureCount; ++index) {
        const double perturbation = sigma[index] * random.normal();
        positive[index] = std::clamp(mean[index] + perturbation, -120.0, 40.0);
        negative[index] = std::clamp(mean[index] - perturbation, -120.0, 40.0);
      }
      for (int phase = 0; phase < kPhaseCount; ++phase) {
        positive[phase * kBaseFeatureCount + 31] = -100.0;
        negative[phase * kBaseFeatureCount + 31] = -100.0;
      }
      population[static_cast<std::size_t>(1 + pair * 2)].weights = positive;
      population[static_cast<std::size_t>(2 + pair * 2)].weights = negative;
    }

    const std::uint32_t seed_start =
        kFittingSeedStart + static_cast<std::uint32_t>(
                                generation * options.batch_games);
    evaluatePopulation(population, seed_start, options.batch_games,
                       options.samples, options.maximum_moves,
                       options.threads, options.rollout_horizon,
                       options.rollout_scenarios);
    std::stable_sort(population.begin(), population.end(),
                     [](const Candidate& first, const Candidate& second) {
                       return first.evaluation.objective >
                              second.evaluation.objective;
                     });

    Weights elite_mean{};
    double rank_mass = 0.0;
    for (int rank = 0; rank < options.elite; ++rank) {
      const double rank_weight =
          std::log(static_cast<double>(options.elite) + 0.5) -
          std::log(static_cast<double>(rank) + 1.0);
      rank_mass += rank_weight;
      for (int index = 0; index < kFeatureCount; ++index) {
        elite_mean[index] += rank_weight * population[rank].weights[index];
      }
    }
    for (double& value : elite_mean) value /= rank_mass;
    Weights elite_variance{};
    for (int rank = 0; rank < options.elite; ++rank) {
      const double rank_weight =
          std::log(static_cast<double>(options.elite) + 0.5) -
          std::log(static_cast<double>(rank) + 1.0);
      for (int index = 0; index < kFeatureCount; ++index) {
        const double delta = population[rank].weights[index] - elite_mean[index];
        elite_variance[index] += rank_weight * delta * delta;
      }
    }
    for (int index = 0; index < kFeatureCount; ++index) {
      mean[index] = 0.25 * mean[index] + 0.75 * elite_mean[index];
      const double selected_sigma =
          std::sqrt(elite_variance[index] / rank_mass + 1e-12);
      sigma[index] = std::clamp(0.30 * sigma[index] +
                                    0.70 * selected_sigma,
                                0.025, 6.0);
    }
    for (int phase = 0; phase < kPhaseCount; ++phase) {
      mean[phase * kBaseFeatureCount + 31] = -100.0;
      sigma[phase * kBaseFeatureCount + 31] = 0.0;
    }
    if (generation % 4 == 3 || generation + 1 == options.generations) {
      archive.push_back(mean);
      archive.push_back(population.front().weights);
    }
    std::cout << std::fixed << std::setprecision(5)
              << "{\"generation\":" << generation
              << ",\"seedStart\":\"0x" << std::hex << seed_start
              << std::dec << "\",\"bestObjective\":"
              << population.front().evaluation.objective
              << ",\"bestScore\":"
              << population.front().evaluation.mean_score
              << ",\"bestMoves\":"
              << population.front().evaluation.mean_moves
              << ",\"meanObjective\":"
              << population[0].evaluation.objective << "}\n";
  }

  std::vector<Candidate> finalists;
  finalists.reserve(archive.size());
  for (const Weights& weights : archive) {
    finalists.push_back(Candidate{weights, {}});
  }
  evaluatePopulation(finalists, tournament_start, options.tournament_games,
                     options.samples, options.maximum_moves, options.threads,
                     options.rollout_horizon, options.rollout_scenarios);
  std::stable_sort(finalists.begin(), finalists.end(),
                   [](const Candidate& first, const Candidate& second) {
                     return first.evaluation.objective >
                            second.evaluation.objective;
                   });
  printEvaluation("tournamentChampion", finalists.front().evaluation,
                  finalists.front().weights);
  printWeights(finalists.front().weights);
  const Evaluation baseline = evaluate(initialWeights(), tournament_start,
                                       options.tournament_games,
                                       options.samples, options.maximum_moves,
                                       options.rollout_horizon,
                                       options.rollout_scenarios);
  printEvaluation("tournamentBaseline", baseline, initialWeights());
  const double wall_seconds = std::chrono::duration<double>(
                                  std::chrono::steady_clock::now() - started)
                                  .count();
  std::cout << "{\"trainingWallSeconds\":" << std::fixed
            << std::setprecision(6) << wall_seconds
            << ",\"archiveCandidates\":" << finalists.size() << "}\n";
  saveCheckpoint(options.checkpoint, finalists.front().weights,
                 options.samples);
  return finalists.front().weights;
}

bool selfTest() {
  const Weights weights = initialWeights();
  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;
  const int first = chooseAction(state, weights, 3);
  const int second = chooseAction(state, weights, 3);
  const bool deterministic = first == second;
  const bool legal = isLegal(state.board, first);

  State metadata = state;
  metadata.score = 987'654'321;
  metadata.level = 123;
  metadata.moves_played = 456;
  const bool metadata_blind = chooseAction(metadata, weights, 3) == first;

  State mirrored = state;
  mirrored.board = detail::mirrorBoard(state.board);
  const int reflected = chooseAction(mirrored, weights, 3);
  const bool reflection_safe = reflected == kBoardSize - 1 - first;

  SearchMetrics first_search_metrics;
  SearchMetrics second_search_metrics;
  const int first_search =
      chooseSearchAction(state, weights, 2, 2, 3, &first_search_metrics);
  const int second_search =
      chooseSearchAction(state, weights, 2, 2, 3, &second_search_metrics);
  const int reflected_search =
      chooseSearchAction(mirrored, weights, 2, 2, 3);
  const int metadata_search =
      chooseSearchAction(metadata, weights, 2, 2, 3);
  const bool search_safe =
      first_search == second_search &&
      first_search_metrics.transitions == second_search_metrics.transitions &&
      isLegal(state.board, first_search) &&
      reflected_search == kBoardSize - 1 - first_search &&
      metadata_search == first_search && first_search_metrics.transitions > 0;

  State seeded_a = state;
  State seeded_b = state;
  seeded_a.next_disc = headlessDisc(0x3d50'1234u, 0);
  seeded_b.next_disc = headlessDisc(0x3d50'1234u, 0);
  const bool headless_reproducible = seeded_a.next_disc == seeded_b.next_disc;

  const std::string checkpoint = "/tmp/drop7-evo-public-self-test.bin";
  saveCheckpoint(checkpoint, weights, 3);
  const auto [loaded, samples] = loadCheckpoint(checkpoint);
  const bool round_trip = loaded == weights && samples == 3;

  const bool passed = deterministic && legal && metadata_blind &&
                      reflection_safe && search_safe &&
                      headless_reproducible && round_trip;
  std::cout << "{\"deterministic\":" << (deterministic ? "true" : "false")
            << ",\"legal\":" << (legal ? "true" : "false")
            << ",\"metadataBlind\":"
            << (metadata_blind ? "true" : "false")
            << ",\"reflectionSafe\":"
            << (reflection_safe ? "true" : "false")
            << ",\"selectiveSearchSafe\":"
            << (search_safe ? "true" : "false")
            << ",\"headlessReproducible\":"
            << (headless_reproducible ? "true" : "false")
            << ",\"checkpointRoundTrip\":"
            << (round_trip ? "true" : "false")
            << ",\"featureCount\":" << kFeatureCount
            << ",\"levelBonus\":" << kLevelBonus
            << ",\"passed\":" << (passed ? "true" : "false") << "}\n";
  return passed;
}

std::uint32_t parseSeed(std::string_view value) {
  std::size_t consumed = 0;
  const unsigned long parsed = std::stoul(std::string(value), &consumed, 0);
  if (consumed != value.size() || parsed > 0xffff'fffful) {
    throw std::invalid_argument("invalid seed");
  }
  return static_cast<std::uint32_t>(parsed);
}

int parsePositive(std::string_view value, std::string_view name) {
  std::size_t consumed = 0;
  const int parsed = std::stoi(std::string(value), &consumed, 10);
  if (consumed != value.size() || parsed < 1) {
    throw std::invalid_argument(std::string(name) + " must be positive");
  }
  return parsed;
}

Options parseOptions(int argc, char** argv) {
  Options result;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    auto requireValue = [&](std::string_view name) -> std::string_view {
      if (++index >= argc) {
        throw std::invalid_argument(std::string(name) + " requires a value");
      }
      return argv[index];
    };
    if (argument == "--self-test") result.mode = Options::Mode::kSelfTest;
    else if (argument == "--baseline") result.mode = Options::Mode::kBaseline;
    else if (argument == "--train") result.mode = Options::Mode::kTrain;
    else if (argument == "--evaluate") result.mode = Options::Mode::kEvaluate;
    else if (argument == "--games") {
      result.games = parsePositive(requireValue(argument), argument);
    } else if (argument == "--max-moves") {
      result.maximum_moves = parsePositive(requireValue(argument), argument);
    } else if (argument == "--samples") {
      result.samples = parsePositive(requireValue(argument), argument);
      result.samples_explicit = true;
    } else if (argument == "--threads") {
      result.threads = parsePositive(requireValue(argument), argument);
    } else if (argument == "--generations") {
      result.generations = parsePositive(requireValue(argument), argument);
    } else if (argument == "--population") {
      result.population = parsePositive(requireValue(argument), argument);
    } else if (argument == "--batch-games") {
      result.batch_games = parsePositive(requireValue(argument), argument);
    } else if (argument == "--tournament-games") {
      result.tournament_games =
          parsePositive(requireValue(argument), argument);
    } else if (argument == "--elite") {
      result.elite = parsePositive(requireValue(argument), argument);
    } else if (argument == "--rollout-horizon") {
      result.rollout_horizon =
          parsePositive(requireValue(argument), argument);
    } else if (argument == "--rollout-scenarios") {
      result.rollout_scenarios =
          parsePositive(requireValue(argument), argument);
    } else if (argument == "--search-depth") {
      result.search_depth = parsePositive(requireValue(argument), argument);
    } else if (argument == "--search-width") {
      result.search_width = parsePositive(requireValue(argument), argument);
    } else if (argument == "--seed-start") {
      result.seed_start = parseSeed(requireValue(argument));
    } else if (argument == "--checkpoint") {
      result.checkpoint = std::string(requireValue(argument));
    } else if (argument == "--summary-only") {
      result.summary_only = true;
    } else {
      throw std::invalid_argument("unknown argument: " + std::string(argument));
    }
  }
  if (result.samples < 1 || result.samples > 32) {
    throw std::invalid_argument("samples must be from 1 to 32");
  }
  return result;
}

bool allowedSeedRange(std::uint32_t start, int games) {
  if (games < 1) return false;
  const std::uint64_t end = static_cast<std::uint64_t>(start) +
                            static_cast<std::uint64_t>(games);
  const bool fitting = start >= kFittingSeedStart && end <= kFittingSeedEnd;
  const bool probe = start >= kProbeSeedStart && end <= kProbeSeedEnd;
  return fitting || probe;
}

std::uint64_t peakRssBytes() {
#if defined(__APPLE__)
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#elif defined(__linux__)
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
#else
  return 0;
#endif
}

}  // namespace drop7::evo_public_policy

int main(int argc, char** argv) {
  using namespace drop7::evo_public_policy;
  try {
    const Options options = parseOptions(argc, argv);
    if (options.mode == Options::Mode::kSelfTest) {
      return selfTest() ? 0 : 1;
    }
    if (options.mode == Options::Mode::kTrain) {
      train(options);
      std::cout << "{\"peakRssBytes\":" << peakRssBytes() << "}\n";
      return 0;
    }
    if (!allowedSeedRange(options.seed_start, options.games)) {
      throw std::invalid_argument(
          "evaluation seeds must stay inside the reserved 0x3d50/0x3d51 "
          "fitting or 0x4d500000..7f probe ranges");
    }

    Weights weights = initialWeights();
    int samples = options.samples;
    std::string_view label = "baseline";
    if (options.mode == Options::Mode::kEvaluate) {
      auto loaded = loadCheckpoint(options.checkpoint);
      weights = loaded.first;
      if (!options.samples_explicit) samples = loaded.second;
      label = "checkpoint";
    }
    const Evaluation evaluation = options.search_depth > 0
                                      ? evaluateSearch(
                                            weights, options.seed_start,
                                            options.games, samples,
                                            options.maximum_moves,
                                            options.search_depth,
                                            options.search_width)
                                      : evaluate(
                                            weights, options.seed_start,
                                            options.games, samples,
                                            options.maximum_moves,
                                            options.rollout_horizon,
                                            options.rollout_scenarios);
    printEvaluation(label, evaluation, weights);
    if (!options.summary_only) printWeights(weights);
    std::cout << "{\"peakRssBytes\":" << peakRssBytes()
              << ",\"checkpointBytes\":"
              << (sizeof(std::uint64_t) * 2 + sizeof(std::uint32_t) * 4 +
                  sizeof(double) * kFeatureCount)
              << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "drop7_evo_public_policy: " << error.what() << '\n';
    return 2;
  }
}
