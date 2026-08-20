#include "../../../src/core/native/public-behavior.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <exception>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using drop7::MoveResult;
using drop7::State;

constexpr std::uint32_t kTapeDomain = 0x5441'5045u;  // "TAPE"
constexpr std::uint32_t kTapeRevealDomain = 0x5245'564cu;
constexpr std::uint32_t kTapeDiscDomain = 0x4449'5343u;
constexpr std::uint32_t kScenarioMultiplier = 0x9e37'79b9u;
constexpr std::uint32_t kStepMultiplier = 0x85eb'ca6bu;
constexpr std::uint32_t kEventMultiplier = 0xc2b2'ae35u;
constexpr std::uint32_t kCriticalTrainingSeedStart = 0x3d70'0200u;
constexpr std::array<int, drop7::kBoardSize> kColumnOrder{{
    3, 2, 4, 1, 5, 0, 6,
}};

struct Options {
  int challengers = 2;
  int maximum_scenarios = 8;
  int maximum_moves = 500;
  int audit_games = 4;
  int audit_time_limit_seconds = 115;
  double confidence = 0.99;
  drop7::cfpi::BehaviorOptions behavior;
};

struct ScreenedAction {
  int column = -1;
  double value = -std::numeric_limits<double>::infinity();
};

struct RolloutResult {
  std::int64_t score = 0;
  int moves = 0;
  bool terminal = false;
  std::uint64_t behavior_calls = 0;
  std::uint64_t behavior_work = 0;
};

struct PairedStats {
  int samples = 0;
  double mean_difference = 0;
  double standard_error = std::numeric_limits<double>::infinity();
  double lower_bound = -std::numeric_limits<double>::infinity();
  double minimum_difference = 0;
  double maximum_difference = 0;
  double mean_move_difference = 0;
  bool all_positive = false;
  bool all_terminal = false;
};

struct ChallengerResult {
  int column = -1;
  double screen_value = -std::numeric_limits<double>::infinity();
  std::vector<RolloutResult> rollouts;
  PairedStats paired;
};

struct TeacherResult {
  int baseline_column = -1;
  int selected_column = -1;
  bool switched = false;
  int scenarios = 0;
  int initial_challengers = 0;
  int surviving_challengers = 0;
  std::vector<RolloutResult> baseline_rollouts;
  std::vector<ChallengerResult> challengers;
  std::uint64_t behavior_calls = 0;
  std::uint64_t behavior_work = 0;
  std::uint64_t simulated_moves = 0;
  double elapsed_seconds = 0;
};

struct CriticalState {
  std::uint32_t game_seed = 0;
  int moves_before_death = 0;
  State state;
};

struct CriticalComparison {
  std::uint32_t game_seed = 0;
  int moves_before_death = 0;
  int baseline_column = -1;
  int challenger_column = -1;
  int scenarios = 0;
  double screen_value = -std::numeric_limits<double>::infinity();
  PairedStats paired;
  std::uint64_t behavior_calls = 0;
  std::uint64_t behavior_work = 0;
  std::uint64_t simulated_moves = 0;
  double elapsed_seconds = 0;
};

struct CriticalAuditResult {
  int requested_games = 0;
  int completed_games = 0;
  int terminal_games = 0;
  int collected_states = 0;
  int compared_states = 0;
  int advanced_to_four = 0;
  int positive_mean_score = 0;
  int positive_mean_moves = 0;
  int mean_move_gain_at_least_25 = 0;
  int positive_score_and_positive_moves = 0;
  int positive_score_and_25_moves = 0;
  bool truncated = false;
  std::vector<std::int64_t> game_scores;
  std::vector<int> game_moves;
  std::vector<CriticalComparison> comparisons;
  std::uint64_t behavior_calls = 0;
  std::uint64_t behavior_work = 0;
  std::uint64_t simulated_moves = 0;
  double elapsed_seconds = 0;
};

int tieRank(int column) {
  for (int rank = 0; rank < drop7::kBoardSize; ++rank) {
    if (kColumnOrder[rank] == column) return rank;
  }
  return drop7::kBoardSize;
}

std::uint32_t observableHash(const State& state) {
  std::uint32_t hash = 0x811c'9dc5u;
  for (std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint32_t>(cell + 1u);
    hash *= 0x0100'0193u;
  }
  hash ^= static_cast<std::uint32_t>(state.next_disc);
  hash *= 0x0100'0193u;
  hash ^= static_cast<std::uint32_t>(state.moves_remaining);
  return drop7::mix32(hash ^ kTapeDomain);
}

std::uint32_t tapeBits(std::uint32_t root_hash, int scenario, int step,
                       int event, std::uint32_t domain) {
  return drop7::mix32(
      root_hash ^ domain ^
      (static_cast<std::uint32_t>(scenario + 1) * kScenarioMultiplier) ^
      (static_cast<std::uint32_t>(step + 1) * kStepMultiplier) ^
      (static_cast<std::uint32_t>(event + 1) * kEventMultiplier));
}

std::uint8_t bitsToDisc(std::uint32_t bits) {
  return static_cast<std::uint8_t>(
      ((static_cast<std::uint64_t>(bits) * drop7::kBoardSize) >> 32) + 1u);
}

struct TapeRandom {
  std::uint32_t root_hash = 0;
  int scenario = 0;
  int step = 0;
  int event = 0;

  std::uint8_t nextDisc() {
    return bitsToDisc(tapeBits(root_hash, scenario, step, event++,
                               kTapeRevealDomain));
  }
};

std::uint8_t nextTapeDisc(std::uint32_t root_hash, int scenario, int step) {
  return bitsToDisc(
      tapeBits(root_hash, scenario, step, 0, kTapeDiscDomain));
}

double screenAction(const State& state, int column,
                    const Options& options) {
  const int samples = options.behavior.chance_samples;
  const std::uint32_t state_seed =
      drop7::cfpi::detail::scenarioSeedForState(
          state, options.behavior.policy_seed, 1);
  double total = 0;
  for (int sample = 0; sample < samples; ++sample) {
    drop7::cfpi::detail::StratifiedRandom random{
        state_seed, sample, samples, 0,
    };
    MoveResult move;
    if (!drop7::cfpi::detail::playMoveSampled(
            state, column, random, move)) {
      total += options.behavior.terminal_utility;
      continue;
    }
    if (move.state.game_over) {
      total += static_cast<double>(move.score_delta) +
               options.behavior.terminal_utility;
      continue;
    }
    move.state.score = 0;
    move.state.next_disc = drop7::cfpi::detail::sampledNextDisc(
        state_seed, sample, samples);
    total += static_cast<double>(move.score_delta) +
             drop7::cfpi::phasePotential(move.state);
  }
  return total / static_cast<double>(samples);
}

std::vector<ScreenedAction> screenChallengers(
    const State& canonical, int baseline, const Options& options) {
  std::vector<ScreenedAction> screened;
  for (int column : kColumnOrder) {
    if (column == baseline || !drop7::isLegal(canonical.board, column)) {
      continue;
    }
    screened.push_back({column, screenAction(canonical, column, options)});
  }
  std::stable_sort(
      screened.begin(), screened.end(),
      [](const ScreenedAction& first, const ScreenedAction& second) {
        if (std::abs(first.value - second.value) > 1e-9) {
          return first.value > second.value;
        }
        return tieRank(first.column) < tieRank(second.column);
      });
  if (static_cast<int>(screened.size()) > options.challengers) {
    screened.resize(options.challengers);
  }
  return screened;
}

RolloutResult rollToTerminal(const State& root, int first_action,
                             std::uint32_t root_hash, int scenario,
                             const Options& options) {
  State state = root;
  const std::int64_t initial_score = state.score;
  const int initial_moves = state.moves_played;
  RolloutResult result;
  int step = 0;
  while (!state.game_over && state.moves_played < options.maximum_moves) {
    int action = first_action;
    if (step > 0) {
      drop7::cfpi::BehaviorMetrics metrics;
      action = drop7::cfpi::chooseBehaviorAction(
          state, options.behavior, &metrics);
      ++result.behavior_calls;
      result.behavior_work += metrics.work;
    }
    if (!drop7::isLegal(state.board, action)) {
      throw std::runtime_error("terminal rollout selected an illegal action");
    }

    TapeRandom random{root_hash, scenario, step, 0};
    MoveResult move;
    if (!drop7::cfpi::detail::playMoveSampled(
            state, action, random, move)) {
      throw std::runtime_error("terminal rollout could not play legal action");
    }
    state = move.state;
    if (!state.game_over) {
      state.next_disc = nextTapeDisc(root_hash, scenario, step);
    }
    ++step;
  }
  result.score = state.score - initial_score;
  result.moves = state.moves_played - initial_moves;
  result.terminal = state.game_over;
  return result;
}

double studentCritical99(int samples) {
  // One-sided 99% Student-t critical values. Only the staged sample sizes used
  // by this feasibility teacher are admitted.
  switch (samples) {
    case 2:
      return 31.821;
    case 4:
      return 4.541;
    case 8:
      return 2.998;
    default:
      throw std::invalid_argument("unsupported confidence sample count");
  }
}

double studentCritical95(int samples) {
  switch (samples) {
    case 2:
      return 6.314;
    case 4:
      return 2.353;
    case 8:
      return 1.895;
    default:
      throw std::invalid_argument("unsupported confidence sample count");
  }
}

PairedStats pairedStats(const std::vector<RolloutResult>& baseline,
                        const std::vector<RolloutResult>& challenger,
                        double confidence) {
  if (baseline.size() != challenger.size() || baseline.size() < 2) {
    throw std::invalid_argument("paired samples must have equal size >= 2");
  }
  PairedStats result;
  result.samples = static_cast<int>(baseline.size());
  result.minimum_difference = std::numeric_limits<double>::infinity();
  result.maximum_difference = -std::numeric_limits<double>::infinity();
  result.all_positive = true;
  result.all_terminal = true;
  std::vector<double> differences;
  differences.reserve(baseline.size());
  for (std::size_t index = 0; index < baseline.size(); ++index) {
    const double difference = static_cast<double>(
        challenger[index].score - baseline[index].score);
    differences.push_back(difference);
    result.mean_difference += difference;
    result.mean_move_difference +=
        challenger[index].moves - baseline[index].moves;
    result.minimum_difference =
        std::min(result.minimum_difference, difference);
    result.maximum_difference =
        std::max(result.maximum_difference, difference);
    result.all_positive = result.all_positive && difference > 0;
    result.all_terminal =
        result.all_terminal && baseline[index].terminal &&
        challenger[index].terminal;
  }
  result.mean_difference /= static_cast<double>(differences.size());
  result.mean_move_difference /= static_cast<double>(differences.size());
  double squared = 0;
  for (double difference : differences) {
    const double residual = difference - result.mean_difference;
    squared += residual * residual;
  }
  const double variance =
      squared / static_cast<double>(differences.size() - 1);
  result.standard_error =
      std::sqrt(variance / static_cast<double>(differences.size()));
  const double critical = confidence >= 0.99
                              ? studentCritical99(result.samples)
                              : studentCritical95(result.samples);
  result.lower_bound =
      result.mean_difference - critical * result.standard_error;
  return result;
}

void accumulateWork(TeacherResult& result,
                    const RolloutResult& rollout) {
  result.behavior_calls += rollout.behavior_calls;
  result.behavior_work += rollout.behavior_work;
  result.simulated_moves += static_cast<std::uint64_t>(rollout.moves);
}

void evaluateNewScenarios(const State& canonical, std::uint32_t root_hash,
                          int baseline, int previous, int target,
                          std::vector<int> active,
                          const Options& options, TeacherResult& result) {
  struct Pending {
    bool baseline = false;
    int challenger_index = -1;
    int scenario = 0;
    std::future<RolloutResult> future;
  };
  std::vector<Pending> pending;
  pending.reserve(
      static_cast<std::size_t>((target - previous) * (active.size() + 1)));
  for (int scenario = previous; scenario < target; ++scenario) {
    pending.push_back({
        true,
        -1,
        scenario,
        std::async(std::launch::async, [&, scenario] {
          return rollToTerminal(canonical, baseline, root_hash, scenario,
                                options);
        }),
    });
    for (int challenger_index : active) {
      const int action = result.challengers[challenger_index].column;
      pending.push_back({
          false,
          challenger_index,
          scenario,
          std::async(std::launch::async, [&, scenario, action] {
            return rollToTerminal(canonical, action, root_hash, scenario,
                                  options);
          }),
      });
    }
  }

  for (Pending& item : pending) {
    RolloutResult rollout = item.future.get();
    accumulateWork(result, rollout);
    if (item.baseline) {
      result.baseline_rollouts.push_back(rollout);
    } else {
      result.challengers[item.challenger_index].rollouts.push_back(rollout);
    }
  }
}

TeacherResult evaluateTeacher(const State& input, const Options& options) {
  if (input.game_over) return {};
  const auto started = Clock::now();
  bool mirrored = false;
  const State canonical =
      drop7::cfpi::detail::canonicalState(input, mirrored);
  drop7::cfpi::BehaviorMetrics root_metrics;
  const int baseline = drop7::cfpi::chooseBehaviorAction(
      canonical, options.behavior, &root_metrics);
  if (baseline < 0) throw std::runtime_error("behavior policy found no action");

  TeacherResult result;
  result.baseline_column = baseline;
  result.selected_column = baseline;
  result.behavior_calls = 1;
  result.behavior_work = root_metrics.work;
  const auto screened =
      screenChallengers(canonical, baseline, options);
  result.initial_challengers = static_cast<int>(screened.size());
  for (const ScreenedAction& candidate : screened) {
    result.challengers.push_back(
        {candidate.column, candidate.value, {}, {}});
  }

  std::vector<int> active;
  for (int index = 0;
       index < static_cast<int>(result.challengers.size()); ++index) {
    active.push_back(index);
  }
  const std::uint32_t root_hash = observableHash(canonical);
  int previous = 0;
  for (int target : {2, 4, 8}) {
    if (target > options.maximum_scenarios || active.empty()) break;
    evaluateNewScenarios(canonical, root_hash, baseline, previous, target,
                         active, options, result);
    result.scenarios = target;
    std::vector<int> survivors;
    for (int challenger_index : active) {
      ChallengerResult& challenger = result.challengers[challenger_index];
      challenger.paired = pairedStats(
          result.baseline_rollouts, challenger.rollouts, options.confidence);
      // The two-scenario look additionally requires both paired outcomes to be
      // positive. This rejects noisy lifetime wins before spending four or
      // eight scenarios on them.
      if (challenger.paired.all_terminal &&
          challenger.paired.lower_bound > 0 &&
          (target > 2 || challenger.paired.all_positive)) {
        survivors.push_back(challenger_index);
      }
    }
    active = std::move(survivors);
    previous = target;
  }

  result.surviving_challengers = static_cast<int>(active.size());
  if (result.scenarios == options.maximum_scenarios && !active.empty()) {
    const int best = *std::max_element(
        active.begin(), active.end(), [&](int first, int second) {
          const auto& first_stats = result.challengers[first].paired;
          const auto& second_stats = result.challengers[second].paired;
          if (std::abs(first_stats.lower_bound -
                       second_stats.lower_bound) > 1e-9) {
            return first_stats.lower_bound < second_stats.lower_bound;
          }
          return tieRank(result.challengers[first].column) >
                 tieRank(result.challengers[second].column);
        });
    result.selected_column = result.challengers[best].column;
    result.switched = true;
  }

  if (mirrored) {
    result.baseline_column =
        drop7::kBoardSize - 1 - result.baseline_column;
    result.selected_column =
        drop7::kBoardSize - 1 - result.selected_column;
    for (ChallengerResult& challenger : result.challengers) {
      challenger.column = drop7::kBoardSize - 1 - challenger.column;
    }
  }
  result.elapsed_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

std::vector<CriticalState> collectCriticalStates(
    const Options& options, CriticalAuditResult& audit,
    Clock::time_point deadline) {
  constexpr std::array<int, 3> offsets{{15, 30, 45}};
  std::vector<CriticalState> collected;
  for (int game = 0; game < options.audit_games; ++game) {
    if (Clock::now() >= deadline) {
      audit.truncated = true;
      break;
    }
    const std::uint32_t seed =
        kCriticalTrainingSeedStart + static_cast<std::uint32_t>(game);
    State state = drop7::initialHeadlessState(seed);
    std::vector<State> trajectory;
    trajectory.reserve(static_cast<std::size_t>(options.maximum_moves));
    while (!state.game_over && state.moves_played < options.maximum_moves) {
      trajectory.push_back(state);
      drop7::cfpi::BehaviorMetrics metrics;
      const int action = drop7::cfpi::chooseBehaviorAction(
          state, options.behavior, &metrics);
      ++audit.behavior_calls;
      audit.behavior_work += metrics.work;
      MoveResult move;
      if (!drop7::playHeadlessMove(state, seed, action, move)) {
        throw std::runtime_error(
            "critical audit behavior selected illegal action");
      }
    }
    ++audit.completed_games;
    audit.game_scores.push_back(state.score);
    audit.game_moves.push_back(state.moves_played);
    if (!state.game_over) continue;
    ++audit.terminal_games;
    const int count = static_cast<int>(trajectory.size());
    for (int offset : offsets) {
      if (offset > count) continue;
      collected.push_back({
          seed,
          offset,
          trajectory[static_cast<std::size_t>(count - offset)],
      });
    }
  }
  audit.collected_states = static_cast<int>(collected.size());
  return collected;
}

void appendCriticalRollouts(
    const State& canonical, int baseline, int challenger,
    std::uint32_t root_hash, int previous, int target,
    const Options& options, std::vector<RolloutResult>& baseline_rollouts,
    std::vector<RolloutResult>& challenger_rollouts,
    CriticalComparison& comparison) {
  struct Pair {
    int scenario = 0;
    std::future<RolloutResult> baseline;
    std::future<RolloutResult> challenger;
  };
  std::vector<Pair> pending;
  pending.reserve(static_cast<std::size_t>(target - previous));
  for (int scenario = previous; scenario < target; ++scenario) {
    pending.push_back({
        scenario,
        std::async(std::launch::async, [&, scenario] {
          return rollToTerminal(canonical, baseline, root_hash, scenario,
                                options);
        }),
        std::async(std::launch::async, [&, scenario] {
          return rollToTerminal(canonical, challenger, root_hash, scenario,
                                options);
        }),
    });
  }
  for (Pair& pair : pending) {
    RolloutResult baseline_result = pair.baseline.get();
    RolloutResult challenger_result = pair.challenger.get();
    comparison.behavior_calls += baseline_result.behavior_calls +
                                 challenger_result.behavior_calls;
    comparison.behavior_work += baseline_result.behavior_work +
                                challenger_result.behavior_work;
    comparison.simulated_moves +=
        static_cast<std::uint64_t>(baseline_result.moves +
                                   challenger_result.moves);
    baseline_rollouts.push_back(baseline_result);
    challenger_rollouts.push_back(challenger_result);
  }
}

CriticalComparison compareCriticalState(
    const CriticalState& critical, const Options& options,
    Clock::time_point deadline) {
  const auto started = Clock::now();
  bool mirrored = false;
  const State canonical =
      drop7::cfpi::detail::canonicalState(critical.state, mirrored);
  drop7::cfpi::BehaviorMetrics root_metrics;
  const int baseline = drop7::cfpi::chooseBehaviorAction(
      canonical, options.behavior, &root_metrics);
  Options screening_options = options;
  screening_options.challengers = 1;
  const auto screened =
      screenChallengers(canonical, baseline, screening_options);

  CriticalComparison result;
  result.game_seed = critical.game_seed;
  result.moves_before_death = critical.moves_before_death;
  result.baseline_column = baseline;
  result.behavior_calls = 1;
  result.behavior_work = root_metrics.work;
  if (screened.empty()) {
    result.challenger_column = -1;
    result.elapsed_seconds =
        std::chrono::duration<double>(Clock::now() - started).count();
    return result;
  }
  result.challenger_column = screened.front().column;
  result.screen_value = screened.front().value;

  std::vector<RolloutResult> baseline_rollouts;
  std::vector<RolloutResult> challenger_rollouts;
  const std::uint32_t root_hash = observableHash(canonical);
  appendCriticalRollouts(
      canonical, baseline, result.challenger_column, root_hash, 0, 2,
      options, baseline_rollouts, challenger_rollouts, result);
  result.scenarios = 2;
  const bool both_favor_challenger =
      challenger_rollouts[0].score > baseline_rollouts[0].score &&
      challenger_rollouts[1].score > baseline_rollouts[1].score;
  if (both_favor_challenger && Clock::now() < deadline) {
    appendCriticalRollouts(
        canonical, baseline, result.challenger_column, root_hash, 2, 4,
        options, baseline_rollouts, challenger_rollouts, result);
    result.scenarios = 4;
  }
  result.paired = pairedStats(
      baseline_rollouts, challenger_rollouts, options.confidence);
  if (mirrored) {
    result.baseline_column =
        drop7::kBoardSize - 1 - result.baseline_column;
    result.challenger_column =
        drop7::kBoardSize - 1 - result.challenger_column;
  }
  result.elapsed_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

CriticalAuditResult runCriticalAudit(const Options& options) {
  const auto started = Clock::now();
  const auto deadline =
      started + std::chrono::seconds(options.audit_time_limit_seconds);
  CriticalAuditResult result;
  result.requested_games = options.audit_games;
  const std::vector<CriticalState> states =
      collectCriticalStates(options, result, deadline);
  for (const CriticalState& state : states) {
    if (Clock::now() >= deadline) {
      result.truncated = true;
      break;
    }
    CriticalComparison comparison =
        compareCriticalState(state, options, deadline);
    if (comparison.challenger_column < 0) continue;
    ++result.compared_states;
    if (comparison.scenarios == 4) ++result.advanced_to_four;
    if (comparison.paired.all_terminal &&
        comparison.paired.mean_difference > 0) {
      ++result.positive_mean_score;
    }
    if (comparison.paired.all_terminal &&
        comparison.paired.mean_move_difference > 0) {
      ++result.positive_mean_moves;
    }
    if (comparison.paired.all_terminal &&
        comparison.paired.mean_move_difference >= 25) {
      ++result.mean_move_gain_at_least_25;
    }
    if (comparison.paired.all_terminal &&
        comparison.paired.mean_difference > 0 &&
        comparison.paired.mean_move_difference > 0) {
      ++result.positive_score_and_positive_moves;
    }
    if (comparison.paired.all_terminal &&
        comparison.paired.mean_difference > 0 &&
        comparison.paired.mean_move_difference >= 25) {
      ++result.positive_score_and_25_moves;
    }
    result.behavior_calls += comparison.behavior_calls;
    result.behavior_work += comparison.behavior_work;
    result.simulated_moves += comparison.simulated_moves;
    result.comparisons.push_back(std::move(comparison));
  }
  result.elapsed_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

void printCriticalAudit(const CriticalAuditResult& result) {
  const double mean_score =
      result.game_scores.empty()
          ? 0
          : static_cast<double>(std::accumulate(
                result.game_scores.begin(), result.game_scores.end(),
                std::int64_t{0})) /
                static_cast<double>(result.game_scores.size());
  const double mean_moves =
      result.game_moves.empty()
          ? 0
          : static_cast<double>(std::accumulate(
                result.game_moves.begin(), result.game_moves.end(), 0)) /
                static_cast<double>(result.game_moves.size());
  std::cout << std::fixed << std::setprecision(3)
            << "{\"mode\":\"critical-audit\""
            << ",\"training_seed_start\":\"0x3d700200\""
            << ",\"requested_games\":" << result.requested_games
            << ",\"completed_games\":" << result.completed_games
            << ",\"terminal_games\":" << result.terminal_games
            << ",\"baseline_mean_score\":" << mean_score
            << ",\"baseline_mean_moves\":" << mean_moves
            << ",\"audit_offsets\":[15,30,45]"
            << ",\"game_scores\":[";
  for (std::size_t index = 0; index < result.game_scores.size(); ++index) {
    if (index > 0) std::cout << ',';
    std::cout << result.game_scores[index];
  }
  std::cout << "],\"game_moves\":[";
  for (std::size_t index = 0; index < result.game_moves.size(); ++index) {
    if (index > 0) std::cout << ',';
    std::cout << result.game_moves[index];
  }
  std::cout << ']'
            << ",\"collected_states\":" << result.collected_states
            << ",\"compared_states\":" << result.compared_states
            << ",\"advanced_to_four\":" << result.advanced_to_four
            << ",\"positive_mean_score\":"
            << result.positive_mean_score
            << ",\"positive_mean_moves\":"
            << result.positive_mean_moves
            << ",\"mean_move_gain_at_least_25\":"
            << result.mean_move_gain_at_least_25
            << ",\"positive_score_and_positive_moves\":"
            << result.positive_score_and_positive_moves
            << ",\"positive_score_and_25_moves\":"
            << result.positive_score_and_25_moves
            << ",\"truncated\":"
            << (result.truncated ? "true" : "false")
            << ",\"behavior_calls\":" << result.behavior_calls
            << ",\"behavior_work\":" << result.behavior_work
            << ",\"simulated_moves\":" << result.simulated_moves
            << ",\"elapsed_seconds\":" << result.elapsed_seconds
            << ",\"comparisons\":[";
  for (std::size_t index = 0; index < result.comparisons.size(); ++index) {
    if (index > 0) std::cout << ',';
    const CriticalComparison& comparison = result.comparisons[index];
    std::cout << "{\"seed\":\"0x" << std::hex
              << comparison.game_seed << std::dec << "\""
              << ",\"moves_before_death\":"
              << comparison.moves_before_death
              << ",\"baseline\":" << comparison.baseline_column
              << ",\"challenger\":" << comparison.challenger_column
              << ",\"scenarios\":" << comparison.scenarios
              << ",\"screen_value\":" << comparison.screen_value
              << ",\"mean_score_difference\":"
              << comparison.paired.mean_difference
              << ",\"mean_move_difference\":"
              << comparison.paired.mean_move_difference
              << ",\"minimum_score_difference\":"
              << comparison.paired.minimum_difference
              << ",\"maximum_score_difference\":"
              << comparison.paired.maximum_difference
              << ",\"lower_bound\":"
              << comparison.paired.lower_bound
              << ",\"all_terminal\":"
              << (comparison.paired.all_terminal ? "true" : "false")
              << ",\"elapsed_seconds\":"
              << comparison.elapsed_seconds << '}';
  }
  std::cout << "]}\n";
}

void printResult(const TeacherResult& result) {
  std::cout << std::fixed << std::setprecision(3)
            << "{\"baseline\":" << result.baseline_column
            << ",\"selected\":" << result.selected_column
            << ",\"switched\":" << (result.switched ? "true" : "false")
            << ",\"scenarios\":" << result.scenarios
            << ",\"initial_challengers\":"
            << result.initial_challengers
            << ",\"surviving_challengers\":"
            << result.surviving_challengers
            << ",\"behavior_calls\":" << result.behavior_calls
            << ",\"behavior_work\":" << result.behavior_work
            << ",\"simulated_moves\":" << result.simulated_moves
            << ",\"elapsed_seconds\":" << result.elapsed_seconds
            << ",\"rollout_moves_per_second\":"
            << (result.elapsed_seconds > 0
                    ? result.simulated_moves / result.elapsed_seconds
                    : 0)
            << ",\"challengers\":[";
  for (std::size_t index = 0; index < result.challengers.size(); ++index) {
    if (index > 0) std::cout << ',';
    const ChallengerResult& challenger = result.challengers[index];
    std::cout << "{\"column\":" << challenger.column
              << ",\"screen_value\":" << challenger.screen_value
              << ",\"samples\":" << challenger.paired.samples
              << ",\"mean_difference\":"
              << challenger.paired.mean_difference
              << ",\"standard_error\":"
              << challenger.paired.standard_error
              << ",\"lower_bound\":"
              << challenger.paired.lower_bound
              << ",\"minimum_difference\":"
              << challenger.paired.minimum_difference
              << ",\"maximum_difference\":"
              << challenger.paired.maximum_difference
              << ",\"mean_move_difference\":"
              << challenger.paired.mean_move_difference
              << ",\"all_terminal\":"
              << (challenger.paired.all_terminal ? "true" : "false")
              << '}';
  }
  std::cout << "]}\n";
}

bool selfTest() {
  Options options;
  options.maximum_scenarios = 2;
  State state;
  state.board.fill(drop7::kEmpty);
  for (int row = 1; row < drop7::kBoardSize; ++row) {
    for (int column = 0; column < drop7::kBoardSize; ++column) {
      state.board[drop7::indexOf(row, column)] = drop7::kSolid;
    }
  }
  state.next_disc = 6;
  state.moves_remaining = 1;

  const TeacherResult first = evaluateTeacher(state, options);
  const TeacherResult second = evaluateTeacher(state, options);
  const bool deterministic =
      first.baseline_column == second.baseline_column &&
      first.selected_column == second.selected_column &&
      first.scenarios == second.scenarios &&
      first.challengers.size() == second.challengers.size();
  const bool legal =
      drop7::isLegal(state.board, first.selected_column);
  const bool retained =
      !first.switched && first.selected_column == first.baseline_column;
  const bool terminal =
      std::all_of(first.baseline_rollouts.begin(),
                  first.baseline_rollouts.end(),
                  [](const RolloutResult& rollout) {
                    return rollout.terminal && rollout.moves == 1;
                  });
  bool paired_equal = true;
  for (const ChallengerResult& challenger : first.challengers) {
    paired_equal =
        paired_equal && challenger.paired.mean_difference == 0 &&
        challenger.paired.lower_bound == 0;
  }

  State reflected = state;
  reflected.board =
      drop7::cfpi::detail::mirrorBoard(state.board);
  const TeacherResult mirror = evaluateTeacher(reflected, options);
  const bool mirror_safe =
      mirror.selected_column ==
      drop7::kBoardSize - 1 - first.selected_column;
  const bool passed = deterministic && legal && retained && terminal &&
                      paired_equal && mirror_safe;
  std::cout << "{\"deterministic\":"
            << (deterministic ? "true" : "false")
            << ",\"legal\":" << (legal ? "true" : "false")
            << ",\"retained\":" << (retained ? "true" : "false")
            << ",\"terminal\":" << (terminal ? "true" : "false")
            << ",\"paired_equal\":"
            << (paired_equal ? "true" : "false")
            << ",\"mirror_safe\":"
            << (mirror_safe ? "true" : "false")
            << ",\"passed\":" << (passed ? "true" : "false")
            << "}\n";
  return passed;
}

int parsePositive(std::string_view value, std::string_view name) {
  std::size_t consumed = 0;
  const long long parsed = std::stoll(std::string(value), &consumed, 10);
  if (consumed != value.size() || parsed < 1 ||
      parsed > std::numeric_limits<int>::max()) {
    throw std::invalid_argument(std::string(name) + " must be positive");
  }
  return static_cast<int>(parsed);
}

Options parseOptions(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--self-test" || argument == "--benchmark" ||
        argument == "--critical-audit") {
      continue;
    }
    if (index + 1 >= argc) {
      throw std::invalid_argument(std::string(argument) + " requires a value");
    }
    const std::string_view value = argv[++index];
    if (argument == "--challengers") {
      options.challengers = parsePositive(value, argument);
      if (options.challengers > 2) {
        throw std::invalid_argument("--challengers must be 1 or 2");
      }
    } else if (argument == "--max-scenarios") {
      options.maximum_scenarios = parsePositive(value, argument);
      if (options.maximum_scenarios != 2 &&
          options.maximum_scenarios != 4 &&
          options.maximum_scenarios != 8) {
        throw std::invalid_argument(
            "--max-scenarios must be 2, 4, or 8");
      }
    } else if (argument == "--max-moves") {
      options.maximum_moves = parsePositive(value, argument);
    } else if (argument == "--games") {
      options.audit_games = parsePositive(value, argument);
      if (options.audit_games > 4) {
        throw std::invalid_argument("--games must be from 1 to 4");
      }
    } else if (argument == "--time-limit-seconds") {
      options.audit_time_limit_seconds = parsePositive(value, argument);
      if (options.audit_time_limit_seconds > 120) {
        throw std::invalid_argument(
            "--time-limit-seconds must be from 1 to 120");
      }
    } else if (argument == "--confidence") {
      options.confidence = std::stod(std::string(value));
      if (options.confidence != 0.95 && options.confidence != 0.99) {
        throw std::invalid_argument("--confidence must be 0.95 or 0.99");
      }
    } else {
      throw std::invalid_argument("unknown argument " +
                                  std::string(argument));
    }
  }
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    bool run_self_test = false;
    bool run_critical_audit = false;
    for (int index = 1; index < argc; ++index) {
      if (std::string_view(argv[index]) == "--self-test") {
        run_self_test = true;
      } else if (std::string_view(argv[index]) == "--critical-audit") {
        run_critical_audit = true;
      }
    }
    if (run_self_test) return selfTest() ? 0 : 1;

    const Options options = parseOptions(argc, argv);
    if (run_critical_audit) {
      printCriticalAudit(runCriticalAudit(options));
      return 0;
    }
    State observable;
    observable.board = drop7::initialBoard();
    observable.next_disc = 4;
    observable.moves_remaining = drop7::kMovesPerLevel;
    const TeacherResult result = evaluateTeacher(observable, options);
    printResult(result);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "drop7_terminal_rollout: " << error.what() << '\n';
    return 2;
  }
}
