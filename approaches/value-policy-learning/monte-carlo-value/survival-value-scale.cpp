// Reuses the reflection-safe hashed value model and observable encoding.  The
// embedded CLI is renamed; this executable owns all collection and evaluation
// seeds and does not invoke the embedded pilot entry point.
#define main drop7_mc_value_embedded_cli
#include "mc-value-policy.cpp"
#undef main

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <thread>
#include <vector>

namespace drop7::survival_scale {

namespace mc = drop7::mc_value;

constexpr int kRootStrata = 5;
constexpr double kRequiredAuc = 0.75;
constexpr double kRequiredRankCorrelation = 0.60;
constexpr double kDefaultSwitchMargin = 8.0;

struct CanonicalState {
  mc::ObservableState state{};
  bool mirrored = false;
};

CanonicalState canonicalizeRoot(const mc::ObservableState& source) {
  CanonicalState result{source, mc::mirrorIsSmaller(source.board)};
  if (!result.mirrored) return result;
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      result.state.board[indexOf(row, column)] =
          source.board[indexOf(row, kBoardSize - 1 - column)];
    }
  }
  return result;
}

inline int physicalAction(int canonical_action, bool mirrored) {
  return mirrored ? kBoardSize - 1 - canonical_action : canonical_action;
}

struct RootSuccessor {
  mc::ObservableState state{};
  bool terminal = false;
  std::uint8_t reveal_target = 1;
  std::uint8_t next_disc_target = 1;
};

RootSuccessor rootSuccessorCanonical(const mc::ObservableState& canonical,
                                     int action, int sample) {
  if (sample < 0 || sample >= kRootStrata ||
      !isLegal(canonical.board, action)) {
    throw std::invalid_argument("invalid root successor request");
  }
  const std::uint32_t hash = mc::observableHash(canonical);
  const int reveal_rotation = static_cast<int>(mix32(hash ^ 0x5256'3530u) % 7u);
  const int disc_rotation = static_cast<int>(mix32(hash ^ 0x4453'3530u) % 7u);
  const int stratum = std::min(
      6, static_cast<int>((sample + 0.5) * 7.0 / kRootStrata));
  const auto reveal = static_cast<std::uint8_t>(
      ((reveal_rotation + stratum) % kBoardSize) + 1);
  const auto next_disc = static_cast<std::uint8_t>(
      ((disc_rotation + 3 * stratum) % kBoardSize) + 1);
  const std::uint32_t base = mix32(
      hash ^ (static_cast<std::uint32_t>(sample + 1) * 0xc2b2'ae35u) ^
      0x5356'3530u);
  Mulberry32 random(mc::seedWithFirstDisc(base, reveal));
  MoveResult move;
  if (!playMove(mc::materialize(canonical), action, random, move)) {
    throw std::runtime_error("root successor rejected legal action");
  }
  if (!move.state.game_over) move.state.next_disc = next_disc;
  return {mc::observable(move.state), move.state.game_over, reveal, next_disc};
}

struct ActionEstimate {
  int canonical_action = -1;
  std::array<double, mc::kEnsembleSize> members{};
  double support = 0;
  double disagreement = 0;
};

ActionEstimate evaluateCanonicalAction(const mc::ObservableState& canonical,
                                       int action,
                                       const mc::Ensemble& ensemble) {
  ActionEstimate result;
  result.canonical_action = action;
  int live_predictions = 0;
  for (int sample = 0; sample < kRootStrata; ++sample) {
    const RootSuccessor successor =
        rootSuccessorCanonical(canonical, action, sample);
    for (int member = 0; member < mc::kEnsembleSize; ++member) {
      if (successor.terminal) {
        result.members[member] += 1.0 / kRootStrata;
      } else {
        const mc::Prediction prediction =
            ensemble[member].predict(successor.state);
        result.members[member] +=
            (1.0 + prediction.lifetime) / kRootStrata;
        result.support += prediction.support;
        ++live_predictions;
      }
    }
  }
  if (live_predictions > 0) result.support /= live_predictions;
  const double mean =
      std::accumulate(result.members.begin(), result.members.end(), 0.0) /
      result.members.size();
  double squares = 0;
  for (double value : result.members) {
    squares += (value - mean) * (value - mean);
  }
  result.disagreement =
      std::sqrt(squares / (result.members.size() - 1));
  return result;
}

double auc(const std::vector<double>& predictions,
           const std::vector<int>& labels) {
  if (predictions.size() != labels.size() || predictions.empty()) {
    throw std::invalid_argument("invalid AUC inputs");
  }
  double favorable = 0;
  std::uint64_t pairs = 0;
  for (std::size_t positive = 0; positive < labels.size(); ++positive) {
    if (labels[positive] != 1) continue;
    for (std::size_t negative = 0; negative < labels.size(); ++negative) {
      if (labels[negative] != 0) continue;
      ++pairs;
      if (predictions[positive] > predictions[negative]) favorable += 1;
      if (predictions[positive] == predictions[negative]) favorable += 0.5;
    }
  }
  return pairs == 0 ? 0.5 : favorable / pairs;
}

std::vector<double> ranks(const std::vector<double>& values) {
  std::vector<std::size_t> order(values.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](std::size_t first,
                                             std::size_t second) {
    return values[first] < values[second];
  });
  std::vector<double> result(values.size());
  std::size_t cursor = 0;
  while (cursor < order.size()) {
    std::size_t end = cursor + 1;
    while (end < order.size() && values[order[end]] == values[order[cursor]]) {
      ++end;
    }
    const double rank = (cursor + end - 1) / 2.0;
    for (std::size_t index = cursor; index < end; ++index) {
      result[order[index]] = rank;
    }
    cursor = end;
  }
  return result;
}

double correlation(const std::vector<double>& first,
                   const std::vector<double>& second) {
  if (first.size() != second.size() || first.empty()) {
    throw std::invalid_argument("invalid correlation inputs");
  }
  const double first_mean =
      std::accumulate(first.begin(), first.end(), 0.0) / first.size();
  const double second_mean =
      std::accumulate(second.begin(), second.end(), 0.0) / second.size();
  double covariance = 0;
  double first_variance = 0;
  double second_variance = 0;
  for (std::size_t index = 0; index < first.size(); ++index) {
    const double first_difference = first[index] - first_mean;
    const double second_difference = second[index] - second_mean;
    covariance += first_difference * second_difference;
    first_variance += first_difference * first_difference;
    second_variance += second_difference * second_difference;
  }
  const double denominator = std::sqrt(first_variance * second_variance);
  return denominator == 0 ? 0 : covariance / denominator;
}

double spearman(const std::vector<double>& predictions,
                const std::vector<double>& labels) {
  return correlation(ranks(predictions), ranks(labels));
}

struct Options {
  int trajectories = 64;
  int holdout_games = 16;
  int epochs = 30;
  int max_moves = 500;
  int threads = std::min(8u, std::max(1u, std::thread::hardware_concurrency()));
  int screen_games = 4;
  int confirmation_games = 8;
  float learning_rate = 0.03f;
  double switch_margin = kDefaultSwitchMargin;
  double minimum_support = 8.0;
  double support_ratio = 0.8;
  double maximum_disagreement = 25.0;
};

struct CollectedGame {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  int clears = 0;
  int reveals = 0;
  bool censored = false;
  std::uint64_t teacher_work = 0;
  std::vector<mc::Label> labels;
};

CollectedGame collectGame(std::uint32_t seed, int max_moves,
                          std::uint32_t game_identifier) {
  State state = initialHeadlessState(seed);
  std::vector<mc::ObservableState> trajectory;
  trajectory.reserve(160);
  CollectedGame result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < max_moves) {
    trajectory.push_back(mc::observable(state));
    cfpi::BehaviorMetrics metrics;
    const int action = cfpi::chooseBehaviorAction(state, {}, &metrics);
    result.teacher_work += metrics.work;
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("scale collector teacher chose illegal action");
    }
    for (const Wave& wave : move.waves) {
      result.clears += wave.cleared;
      result.reveals += wave.revealed;
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  if (!result.censored) {
    result.labels.reserve(trajectory.size());
    for (std::size_t index = 0; index < trajectory.size(); ++index) {
      const int remaining =
          state.moves_played - static_cast<int>(index);
      result.labels.push_back({
          trajectory[index],
          static_cast<float>(remaining),
          remaining >= 25 ? 1.0f : 0.0f,
          remaining >= 50 ? 1.0f : 0.0f,
          mix32(game_identifier ^ static_cast<std::uint32_t>(index + 1)),
      });
    }
  }
  return result;
}

std::vector<CollectedGame> collectParallel(const Options& options,
                                           std::uint32_t seed_start) {
  std::vector<CollectedGame> games(options.trajectories);
  std::atomic<int> next{0};
  std::atomic<bool> failed{false};
  std::mutex error_mutex;
  std::string error_message;
  const int worker_count = std::min(options.threads, options.trajectories);
  std::vector<std::thread> workers;
  workers.reserve(worker_count);
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.emplace_back([&]() {
      while (!failed.load(std::memory_order_relaxed)) {
        const int index = next.fetch_add(1, std::memory_order_relaxed);
        if (index >= options.trajectories) break;
        try {
          games[index] = collectGame(
              seed_start + static_cast<std::uint32_t>(index),
              options.max_moves,
              mix32(0x5343'414cu ^ static_cast<std::uint32_t>(index)));
        } catch (const std::exception& error) {
          failed.store(true, std::memory_order_relaxed);
          std::lock_guard<std::mutex> lock(error_mutex);
          error_message = error.what();
        }
      }
    });
  }
  for (std::thread& worker : workers) worker.join();
  if (failed.load()) {
    throw std::runtime_error("parallel collection failed: " + error_message);
  }
  return games;
}

struct PredictionMetrics {
  int examples = 0;
  double mean_label = 0;
  double mean_prediction = 0;
  double lifetime_mae = 0;
  double death_25_auc = 0;
  double death_50_auc = 0;
  double death_25_brier = 0;
  double death_50_brier = 0;
  double death_25_ece = 0;
  double death_50_ece = 0;
  double rank_correlation = 0;
  double mean_support = 0;
  double p10_support = 0;
};

double calibrationError(const std::vector<double>& predictions,
                        const std::vector<int>& labels) {
  constexpr int bins = 10;
  std::array<double, bins> prediction_sum{};
  std::array<double, bins> label_sum{};
  std::array<int, bins> counts{};
  for (std::size_t index = 0; index < predictions.size(); ++index) {
    const int bin = std::min(
        bins - 1, static_cast<int>(std::floor(predictions[index] * bins)));
    prediction_sum[bin] += predictions[index];
    label_sum[bin] += labels[index];
    ++counts[bin];
  }
  double result = 0;
  for (int bin = 0; bin < bins; ++bin) {
    if (counts[bin] == 0) continue;
    result += static_cast<double>(counts[bin]) / predictions.size() *
              std::abs(prediction_sum[bin] / counts[bin] -
                       label_sum[bin] / counts[bin]);
  }
  return result;
}

PredictionMetrics evaluatePredictions(const std::vector<mc::Label>& labels,
                                      const mc::Ensemble& ensemble) {
  if (labels.empty()) throw std::invalid_argument("empty metric labels");
  std::vector<double> lifetime_predictions;
  std::vector<double> lifetimes;
  std::vector<double> death_25_predictions;
  std::vector<double> death_50_predictions;
  std::vector<int> death_25_labels;
  std::vector<int> death_50_labels;
  std::vector<double> supports;
  lifetime_predictions.reserve(labels.size());
  lifetimes.reserve(labels.size());
  death_25_predictions.reserve(labels.size());
  death_50_predictions.reserve(labels.size());
  death_25_labels.reserve(labels.size());
  death_50_labels.reserve(labels.size());
  supports.reserve(labels.size());
  PredictionMetrics result;
  result.examples = static_cast<int>(labels.size());
  for (const mc::Label& label : labels) {
    double lifetime = 0;
    double survival_25 = 0;
    double survival_50 = 0;
    double support = 0;
    for (const mc::ValueModel& member : ensemble) {
      const mc::Prediction prediction = member.predict(label.state);
      lifetime += prediction.lifetime / mc::kEnsembleSize;
      survival_25 += prediction.survival_25 / mc::kEnsembleSize;
      survival_50 += prediction.survival_50 / mc::kEnsembleSize;
      support += prediction.support / mc::kEnsembleSize;
    }
    const double death_25 = 1.0 - survival_25;
    const double death_50 = 1.0 - survival_50;
    const int died_25 = label.lifetime < 25 ? 1 : 0;
    const int died_50 = label.lifetime < 50 ? 1 : 0;
    lifetime_predictions.push_back(lifetime);
    lifetimes.push_back(label.lifetime);
    death_25_predictions.push_back(death_25);
    death_50_predictions.push_back(death_50);
    death_25_labels.push_back(died_25);
    death_50_labels.push_back(died_50);
    supports.push_back(support);
    result.mean_label += label.lifetime;
    result.mean_prediction += lifetime;
    result.lifetime_mae += std::abs(lifetime - label.lifetime);
    result.death_25_brier += (death_25 - died_25) * (death_25 - died_25);
    result.death_50_brier += (death_50 - died_50) * (death_50 - died_50);
    result.mean_support += support;
  }
  const double count = labels.size();
  result.mean_label /= count;
  result.mean_prediction /= count;
  result.lifetime_mae /= count;
  result.death_25_brier /= count;
  result.death_50_brier /= count;
  result.mean_support /= count;
  result.death_25_auc = auc(death_25_predictions, death_25_labels);
  result.death_50_auc = auc(death_50_predictions, death_50_labels);
  result.death_25_ece =
      calibrationError(death_25_predictions, death_25_labels);
  result.death_50_ece =
      calibrationError(death_50_predictions, death_50_labels);
  result.rank_correlation = spearman(lifetime_predictions, lifetimes);
  std::sort(supports.begin(), supports.end());
  result.p10_support = supports[static_cast<std::size_t>(
      std::floor(0.10 * static_cast<double>(supports.size() - 1)))];
  return result;
}

void printPredictionMetrics(std::string_view tag,
                            const PredictionMetrics& metrics) {
  std::cout << std::fixed << std::setprecision(6) << tag
            << " {\"examples\":" << metrics.examples
            << ",\"meanLabel\":" << metrics.mean_label
            << ",\"meanPrediction\":" << metrics.mean_prediction
            << ",\"lifetimeMae\":" << metrics.lifetime_mae
            << ",\"death25Auc\":" << metrics.death_25_auc
            << ",\"death50Auc\":" << metrics.death_50_auc
            << ",\"death25Brier\":" << metrics.death_25_brier
            << ",\"death50Brier\":" << metrics.death_50_brier
            << ",\"death25Ece\":" << metrics.death_25_ece
            << ",\"death50Ece\":" << metrics.death_50_ece
            << ",\"rankCorrelation\":" << metrics.rank_correlation
            << ",\"meanSupport\":" << metrics.mean_support
            << ",\"p10Support\":" << metrics.p10_support << "}\n";
}

struct PolicyCounters {
  std::uint64_t modeled_transitions = 0;
  std::uint64_t teacher_work = 0;
  int switches = 0;
  int support_rejections = 0;
  int agreement_rejections = 0;
};

struct PolicyGame {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  int clears = 0;
  int reveals = 0;
  PolicyCounters counters{};
};

struct PolicySummary {
  double mean_score = 0;
  double mean_moves = 0;
  double clear_rate = 0;
  double reveal_rate = 0;
  double mean_switches = 0;
  std::uint64_t modeled_transitions = 0;
  std::uint64_t teacher_work = 0;
  int support_rejections = 0;
  int agreement_rejections = 0;
  std::vector<PolicyGame> games;
};

int chooseConservativeAction(const State& state, int behavior_action,
                             const mc::Ensemble& ensemble,
                             const Options& options,
                             PolicyCounters& counters) {
  const CanonicalState canonical = canonicalizeRoot(mc::observable(state));
  const int canonical_behavior =
      canonical.mirrored ? kBoardSize - 1 - behavior_action : behavior_action;
  const ActionEstimate behavior = evaluateCanonicalAction(
      canonical.state, canonical_behavior, ensemble);
  counters.modeled_transitions += kRootStrata;
  int selected = canonical_behavior;
  double best_mean_margin = 0;
  for (int action = 0; action < kBoardSize; ++action) {
    if (action == canonical_behavior || !isLegal(canonical.state.board, action)) {
      continue;
    }
    const ActionEstimate candidate =
        evaluateCanonicalAction(canonical.state, action, ensemble);
    counters.modeled_transitions += kRootStrata;
    const bool supported =
        candidate.support >= options.minimum_support &&
        candidate.support >= options.support_ratio * behavior.support &&
        candidate.disagreement <= options.maximum_disagreement;
    if (!supported) {
      ++counters.support_rejections;
      continue;
    }
    double mean_margin = 0;
    bool members_agree = true;
    for (int member = 0; member < mc::kEnsembleSize; ++member) {
      const double margin =
          candidate.members[member] - behavior.members[member];
      mean_margin += margin / mc::kEnsembleSize;
      if (margin <= options.switch_margin) members_agree = false;
    }
    if (!members_agree) {
      ++counters.agreement_rejections;
      continue;
    }
    if (mean_margin > best_mean_margin) {
      best_mean_margin = mean_margin;
      selected = action;
    }
  }
  if (selected != canonical_behavior) ++counters.switches;
  return physicalAction(selected, canonical.mirrored);
}

PolicyGame runPolicyGame(std::uint32_t seed, const mc::Ensemble* ensemble,
                         const Options& options) {
  State state = initialHeadlessState(seed);
  PolicyGame result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < options.max_moves) {
    cfpi::BehaviorMetrics metrics;
    const int behavior_action =
        cfpi::chooseBehaviorAction(state, {}, &metrics);
    result.counters.teacher_work += metrics.work;
    const int action = ensemble == nullptr
                           ? behavior_action
                           : chooseConservativeAction(
                                 state, behavior_action, *ensemble, options,
                                 result.counters);
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("scale policy selected illegal action");
    }
    for (const Wave& wave : move.waves) {
      result.clears += wave.cleared;
      result.reveals += wave.revealed;
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  return result;
}

PolicySummary summarizePolicies(std::vector<PolicyGame> games) {
  PolicySummary result;
  result.games = std::move(games);
  std::uint64_t moves = 0;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
  for (const PolicyGame& game : result.games) {
    result.mean_score += game.score;
    result.mean_moves += game.moves;
    result.mean_switches += game.counters.switches;
    result.modeled_transitions += game.counters.modeled_transitions;
    result.teacher_work += game.counters.teacher_work;
    result.support_rejections += game.counters.support_rejections;
    result.agreement_rejections += game.counters.agreement_rejections;
    moves += static_cast<std::uint64_t>(game.moves);
    clears += static_cast<std::uint64_t>(game.clears);
    reveals += static_cast<std::uint64_t>(game.reveals);
  }
  const double count = result.games.size();
  result.mean_score /= count;
  result.mean_moves /= count;
  result.mean_switches /= count;
  result.clear_rate = static_cast<double>(clears) / std::max<std::uint64_t>(1, moves);
  result.reveal_rate =
      static_cast<double>(reveals) / std::max<std::uint64_t>(1, moves);
  return result;
}

void printPolicySummary(std::string_view tag, const PolicySummary& summary) {
  std::cout << std::fixed << std::setprecision(3) << tag
            << " {\"games\":" << summary.games.size()
            << ",\"meanScore\":" << summary.mean_score
            << ",\"meanMoves\":" << summary.mean_moves
            << ",\"clearRate\":" << summary.clear_rate
            << ",\"revealRate\":" << summary.reveal_rate
            << ",\"meanSwitches\":" << summary.mean_switches
            << ",\"modeledTransitions\":" << summary.modeled_transitions
            << ",\"supportRejections\":" << summary.support_rejections
            << ",\"agreementRejections\":"
            << summary.agreement_rejections
            << ",\"teacherWork\":" << summary.teacher_work << "}\n";
}

struct PairedResult {
  double score_lower_95 = 0;
  double moves_lower_95 = 0;
};

double pairedLower(const std::vector<double>& differences) {
  const double mean =
      std::accumulate(differences.begin(), differences.end(), 0.0) /
      differences.size();
  if (differences.size() < 2) {
    return -std::numeric_limits<double>::infinity();
  }
  double squares = 0;
  for (double difference : differences) {
    squares += (difference - mean) * (difference - mean);
  }
  const double deviation =
      std::sqrt(squares / (differences.size() - 1));
  return mean - 1.96 * deviation / std::sqrt(differences.size());
}

PairedResult pairedResult(const PolicySummary& behavior,
                          const PolicySummary& candidate) {
  if (behavior.games.size() != candidate.games.size()) {
    throw std::invalid_argument("paired policy summaries differ in size");
  }
  std::vector<double> score_differences;
  std::vector<double> move_differences;
  for (std::size_t index = 0; index < behavior.games.size(); ++index) {
    score_differences.push_back(candidate.games[index].score -
                                behavior.games[index].score);
    move_differences.push_back(candidate.games[index].moves -
                               behavior.games[index].moves);
  }
  return {pairedLower(score_differences), pairedLower(move_differences)};
}

std::uint64_t peakResidentBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
#endif
}

int runScale(const Options& options) {
  if (options.trajectories < 64 || options.holdout_games < 8 ||
      options.holdout_games >= options.trajectories || options.epochs < 1 ||
      options.max_moves < 1 || options.threads < 1 || options.screen_games < 4 ||
      options.confirmation_games < 8 || options.learning_rate <= 0 ||
      options.switch_margin < 0 || options.minimum_support < 0 ||
      options.support_ratio < 0 || options.support_ratio > 1 ||
      options.maximum_disagreement <= 0) {
    throw std::invalid_argument("invalid survival scale options");
  }
  const auto started = std::chrono::steady_clock::now();
  constexpr std::uint32_t collection_start = 0x3d70'4000u;
  std::cout << "SURVIVAL_SCALE_CONFIG {\"trajectories\":"
            << options.trajectories
            << ",\"holdoutGames\":" << options.holdout_games
            << ",\"epochs\":" << options.epochs
            << ",\"threads\":" << options.threads
            << ",\"learningRate\":" << options.learning_rate
            << ",\"rootStrata\":" << kRootStrata
            << ",\"requiredAuc\":" << kRequiredAuc
            << ",\"requiredRankCorrelation\":"
            << kRequiredRankCorrelation
            << ",\"switchMargin\":" << options.switch_margin
            << ",\"minimumSupport\":" << options.minimum_support
            << ",\"supportRatio\":" << options.support_ratio
            << ",\"maximumDisagreement\":"
            << options.maximum_disagreement
            << ",\"collectionSeedStart\":" << collection_start
            << ",\"screenSeedStart\":" << 0x3e79'0000u
            << ",\"confirmationSeedStart\":" << 0x3e7a'0000u
            << ",\"seedRanges\":[\"0x3d\",\"0x3e\"]}\n";

  std::vector<CollectedGame> games =
      collectParallel(options, collection_start);
  const int training_games = options.trajectories - options.holdout_games;
  std::vector<mc::Label> training_labels;
  std::vector<mc::Label> holdout_labels;
  std::uint64_t teacher_work = 0;
  int censored = 0;
  double mean_score = 0;
  double mean_moves = 0;
  for (int game = 0; game < options.trajectories; ++game) {
    teacher_work += games[game].teacher_work;
    mean_score += games[game].score;
    mean_moves += games[game].moves;
    censored += games[game].censored ? 1 : 0;
    auto& target = game < training_games ? training_labels : holdout_labels;
    target.insert(target.end(), games[game].labels.begin(),
                  games[game].labels.end());
  }
  mean_score /= options.trajectories;
  mean_moves /= options.trajectories;
  std::cout << std::fixed << std::setprecision(3)
            << "SURVIVAL_SCALE_COLLECTION {\"games\":"
            << options.trajectories
            << ",\"trainingGames\":" << training_games
            << ",\"holdoutGames\":" << options.holdout_games
            << ",\"trainingLabels\":" << training_labels.size()
            << ",\"holdoutLabels\":" << holdout_labels.size()
            << ",\"meanScore\":" << mean_score
            << ",\"meanMoves\":" << mean_moves
            << ",\"censored\":" << censored
            << ",\"teacherWork\":" << teacher_work << "}\n";

  mc::Ensemble ensemble = mc::createEnsemble(0x6c53'1001u);
  for (mc::ValueModel& member : ensemble) {
    for (const mc::Label& label : training_labels) member.observe(label.state);
  }
  for (int epoch = 0; epoch < options.epochs; ++epoch) {
    for (std::size_t offset = 0; offset < training_labels.size(); ++offset) {
      const std::size_t index =
          (offset + static_cast<std::size_t>(epoch) * 7'919u) %
          training_labels.size();
      for (mc::ValueModel& member : ensemble) {
        member.train(training_labels[index], options.learning_rate);
      }
    }
  }
  const PredictionMetrics training_metrics =
      evaluatePredictions(training_labels, ensemble);
  const PredictionMetrics holdout_metrics =
      evaluatePredictions(holdout_labels, ensemble);
  printPredictionMetrics("SURVIVAL_SCALE_TRAIN", training_metrics);
  printPredictionMetrics("SURVIVAL_SCALE_HOLDOUT", holdout_metrics);
  const bool prediction_gate =
      std::min(holdout_metrics.death_25_auc,
               holdout_metrics.death_50_auc) >= kRequiredAuc &&
      holdout_metrics.rank_correlation >= kRequiredRankCorrelation;
  std::cout << "SURVIVAL_SCALE_GATE {\"predictionPassed\":"
            << (prediction_gate ? "true" : "false") << "}\n";
  if (!prediction_gate) {
    const double seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
    std::cout << "SURVIVAL_SCALE_RESULT {\"qualified\":false,"
                 "\"stoppedAt\":\"prediction\",\"seconds\":"
              << seconds << ",\"peakResidentBytes\":"
              << peakResidentBytes() << "}\n";
    return 3;
  }

  const auto evaluate_pair = [&](std::uint32_t seed_start, int count) {
    std::vector<PolicyGame> behavior_games;
    std::vector<PolicyGame> candidate_games;
    behavior_games.reserve(count);
    candidate_games.reserve(count);
    for (int game = 0; game < count; ++game) {
      const std::uint32_t seed = seed_start + static_cast<std::uint32_t>(game);
      behavior_games.push_back(runPolicyGame(seed, nullptr, options));
      candidate_games.push_back(runPolicyGame(seed, &ensemble, options));
    }
    return std::pair{summarizePolicies(std::move(behavior_games)),
                     summarizePolicies(std::move(candidate_games))};
  };
  auto [screen_behavior, screen_candidate] =
      evaluate_pair(0x3e79'0000u, options.screen_games);
  const PairedResult screen_paired =
      pairedResult(screen_behavior, screen_candidate);
  printPolicySummary("SURVIVAL_SCALE_SCREEN_BEHAVIOR", screen_behavior);
  printPolicySummary("SURVIVAL_SCALE_SCREEN_CANDIDATE", screen_candidate);
  const bool screen_pass =
      screen_candidate.mean_score > screen_behavior.mean_score &&
      screen_candidate.mean_moves > screen_behavior.mean_moves;
  std::cout << "SURVIVAL_SCALE_SCREEN {\"passed\":"
            << (screen_pass ? "true" : "false")
            << ",\"scoreLower95\":" << screen_paired.score_lower_95
            << ",\"movesLower95\":" << screen_paired.moves_lower_95
            << "}\n";
  if (!screen_pass) {
    const double seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
    std::cout << "SURVIVAL_SCALE_RESULT {\"qualified\":false,"
                 "\"stoppedAt\":\"screen\",\"seconds\":"
              << seconds << ",\"peakResidentBytes\":"
              << peakResidentBytes() << "}\n";
    return 3;
  }

  auto [confirmation_behavior, confirmation_candidate] =
      evaluate_pair(0x3e7a'0000u, options.confirmation_games);
  const PairedResult confirmation_paired =
      pairedResult(confirmation_behavior, confirmation_candidate);
  const bool qualified =
      confirmation_candidate.mean_score > confirmation_behavior.mean_score &&
      confirmation_candidate.mean_moves > confirmation_behavior.mean_moves &&
      confirmation_paired.score_lower_95 > 0 &&
      confirmation_paired.moves_lower_95 > 0;
  printPolicySummary("SURVIVAL_SCALE_CONFIRM_BEHAVIOR",
                     confirmation_behavior);
  printPolicySummary("SURVIVAL_SCALE_CONFIRM_CANDIDATE",
                     confirmation_candidate);
  const double seconds = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - started)
                             .count();
  std::cout << "SURVIVAL_SCALE_RESULT {\"qualified\":"
            << (qualified ? "true" : "false")
            << ",\"stoppedAt\":\"confirmation\",\"scoreLower95\":"
            << confirmation_paired.score_lower_95
            << ",\"movesLower95\":" << confirmation_paired.moves_lower_95
            << ",\"seconds\":" << seconds
            << ",\"peakResidentBytes\":" << peakResidentBytes() << "}\n";
  return qualified ? 0 : 3;
}

bool selfTest(std::ostream& output) {
  const bool base = mc::selfTest(output);
  const std::vector<double> perfect_predictions{0.1, 0.2, 0.8, 0.9};
  const std::vector<int> binary{0, 0, 1, 1};
  const std::vector<double> ordered{1, 2, 3, 4};
  const bool metrics = std::abs(auc(perfect_predictions, binary) - 1.0) < 1e-12 &&
                       std::abs(spearman(ordered, ordered) - 1.0) < 1e-12;
  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;
  const CanonicalState canonical = canonicalizeRoot(mc::observable(state));
  mc::Ensemble ensemble = mc::createEnsemble(0x6c53'0001u);
  int legal_count = 0;
  const auto legal = legalColumns(canonical.state.board, legal_count);
  std::array<bool, 8> reveal_targets{};
  std::array<bool, 8> disc_targets{};
  bool finite = legal_count > 0;
  for (int offset = 0; offset < legal_count; ++offset) {
    const ActionEstimate estimate =
        evaluateCanonicalAction(canonical.state, legal[offset], ensemble);
    finite = finite && std::isfinite(estimate.members[0]);
  }
  for (int sample = 0; sample < kRootStrata; ++sample) {
    const RootSuccessor successor =
        rootSuccessorCanonical(canonical.state, legal[0], sample);
    reveal_targets[successor.reveal_target] = true;
    disc_targets[successor.next_disc_target] = true;
  }
  const int distinct_reveals = static_cast<int>(std::count(
      reveal_targets.begin() + 1, reveal_targets.end(), true));
  const int distinct_discs = static_cast<int>(
      std::count(disc_targets.begin() + 1, disc_targets.end(), true));
  const bool stratified = distinct_reveals == kRootStrata &&
                          distinct_discs == kRootStrata;
  const bool gates = kRequiredAuc == 0.75 &&
                     kRequiredRankCorrelation == 0.60 &&
                     kDefaultSwitchMargin == 8.0;
  const bool passed = base && metrics && finite && stratified && gates;
  output << "SURVIVAL_SCALE_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"metricRules\":" << (metrics ? "true" : "false")
         << ",\"legalActions\":" << legal_count
         << ",\"rootStrata\":" << kRootStrata
         << ",\"distinctRevealTargets\":" << distinct_reveals
         << ",\"distinctDiscTargets\":" << distinct_discs << "}\n";
  return passed;
}

}  // namespace drop7::survival_scale

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::survival_scale::selfTest(std::cout) ? EXIT_SUCCESS
                                                        : EXIT_FAILURE;
    }
    const auto value_after = [&](std::string_view flag,
                                 std::string fallback) {
      for (int index = 1; index + 1 < argc; ++index) {
        if (std::string_view(argv[index]) == flag) {
          return std::string(argv[index + 1]);
        }
      }
      return fallback;
    };
    const auto has_flag = [&](std::string_view flag) {
      for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == flag) return true;
      }
      return false;
    };
    if (has_flag("--run")) {
      drop7::survival_scale::Options options;
      options.trajectories = std::stoi(value_after(
          "--trajectories", std::to_string(options.trajectories)));
      options.holdout_games = std::stoi(value_after(
          "--holdout-games", std::to_string(options.holdout_games)));
      options.epochs =
          std::stoi(value_after("--epochs", std::to_string(options.epochs)));
      options.max_moves = std::stoi(value_after(
          "--max-moves", std::to_string(options.max_moves)));
      options.threads =
          std::stoi(value_after("--threads", std::to_string(options.threads)));
      options.screen_games = std::stoi(value_after(
          "--screen-games", std::to_string(options.screen_games)));
      options.confirmation_games = std::stoi(value_after(
          "--confirmation-games",
          std::to_string(options.confirmation_games)));
      options.learning_rate = std::stof(value_after(
          "--learning-rate", std::to_string(options.learning_rate)));
      options.switch_margin = std::stod(value_after(
          "--switch-margin", std::to_string(options.switch_margin)));
      options.minimum_support = std::stod(value_after(
          "--minimum-support", std::to_string(options.minimum_support)));
      options.support_ratio = std::stod(value_after(
          "--support-ratio", std::to_string(options.support_ratio)));
      options.maximum_disagreement = std::stod(value_after(
          "--maximum-disagreement",
          std::to_string(options.maximum_disagreement)));
      return drop7::survival_scale::runScale(options);
    }
    std::cerr << "Usage: drop7_survival_value_scale --self-test | --run [options]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
