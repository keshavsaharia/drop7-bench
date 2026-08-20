#include "../../../src/core/native/public-behavior.hpp"

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <tuple>
#include <thread>
#include <utility>
#include <vector>

#ifndef DROP7_TORCH_ENV_CORE_ONLY
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#endif

// High-throughput Python boundary for neural Drop7 experiments.  The pure C++
// core owns all game states, game seeds, and episode accumulators.  Python sees
// only the public board, visible disc, rise phase, legal/active masks, local
// rewards, and completed-episode summaries.  Future random values and seed
// identity never cross the boundary.
namespace drop7::torch_env {

constexpr std::uint32_t kAllowedSeedMinimum = 0x3d30'0000u;
constexpr std::uint64_t kAllowedSeedEndExclusive = 0x3d40'0000ull;
constexpr int kMaximumEnvironments = 16'384;
constexpr int kMaximumMoves = 4'000;
constexpr int kTeacherChanceSamples = 5;
constexpr std::uint32_t kTeacherPolicySeed = 0xd707'5eedu;
constexpr double kTeacherTerminalUtility = -1'000'000.0;

static_assert(kAllowedSeedMinimum >= 0x3d30'0000u);
static_assert(kAllowedSeedEndExclusive <= 0x3d40'0000ull);
static_assert((kAllowedSeedMinimum >> 24u) == 0x3du);
static_assert((kAllowedSeedMinimum >> 24u) != 0x3eu &&
              (kAllowedSeedMinimum >> 24u) != 0x7du &&
              (kAllowedSeedMinimum >> 24u) != 0xd7u);

struct Episode {
  std::int64_t score = 0;
  int moves = 0;
  std::uint64_t numbered_cleared = 0;
  std::uint64_t covers_revealed = 0;
  int maximum_chain = 0;
  bool censored = false;
};

struct RunningEpisode {
  std::uint64_t numbered_cleared = 0;
  std::uint64_t covers_revealed = 0;
  int maximum_chain = 0;
};

struct Transition {
  bool stepped = false;
  std::int64_t score_delta = 0;
  int numbered_cleared = 0;
  int covers_revealed = 0;
  bool terminated = false;
  bool truncated = false;
  Episode episode{};
};

std::uint8_t legalMask(const Board& board) {
  std::uint8_t result = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    if (isLegal(board, column)) {
      result = static_cast<std::uint8_t>(result | (1u << column));
    }
  }
  return result;
}

State publicState(const State& source) {
  State result;
  result.board = source.board;
  result.next_disc = source.next_disc;
  result.moves_remaining = source.moves_remaining;
  result.game_over = source.game_over;
  result.score = 0;
  result.level = 1;
  result.moves_played = 0;
  return result;
}

Board mirrorBoard(const Board& source) {
  Board result{};
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      result[indexOf(row, kBoardSize - 1 - column)] =
          source[indexOf(row, column)];
    }
  }
  return result;
}

bool equalState(const State& first, const State& second) {
  return first.board == second.board && first.next_disc == second.next_disc &&
         first.score == second.score && first.level == second.level &&
         first.moves_remaining == second.moves_remaining &&
         first.moves_played == second.moves_played &&
         first.game_over == second.game_over;
}

// This is the established public fair leaf used by the fair D1/D2 baselines.
// Search code below deliberately consumes only publicState(source).  It uses
// the engine's current score constants through playMoveSampled, so the Python
// boundary itself does not clone or reinterpret a mode-specific level bonus.
struct TeacherFeatures {
  cfpi::detail::PhaseFeatures phase{};
  double covered_height_risk = 0;
  double low_number_height_risk = 0;
  double danger_height_squared = 0;
  double roughness = 0;
  double rise_pressure = 0;
  double next_disc_vertical_options = 0;
};

TeacherFeatures teacherFeatures(const State& state) {
  if (state.moves_remaining < 1 || state.moves_remaining > kMovesPerLevel ||
      state.next_disc < 1 || state.next_disc > kBoardSize) {
    throw std::invalid_argument("invalid public teacher state");
  }
  TeacherFeatures result;
  result.phase = cfpi::detail::extractPhaseFeatures(state);
  const auto heights = cfpi::detail::columnHeights(state.board);
  int maximum_height = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    const int height = heights[column];
    maximum_height = std::max(maximum_height, height);
    result.rise_pressure +=
        static_cast<double>(height * height * height) /
        state.moves_remaining;
    if (height < kBoardSize && height + 1 == state.next_disc) {
      result.next_disc_vertical_options += 1;
    }
  }
  for (int row = 0; row < kBoardSize; ++row) {
    const int elevation = kBoardSize - row;
    for (int column = 0; column < kBoardSize; ++column) {
      const std::uint8_t cell = state.board[indexOf(row, column)];
      const double edge_multiplier =
          column == 0 || column == kBoardSize - 1 ? 1.65 : 1.0;
      if (cell == kSolid) {
        result.covered_height_risk +=
            elevation * elevation * edge_multiplier;
      } else if (cell == kCracked) {
        result.covered_height_risk +=
            elevation * elevation * edge_multiplier * 0.72;
      } else if (cell == 1 || cell == 2) {
        const int height_risk = std::max(0, elevation - 2);
        result.low_number_height_risk += height_risk * height_risk;
      }
    }
  }
  for (int column = 1; column < kBoardSize; ++column) {
    result.roughness += std::abs(heights[column] - heights[column - 1]);
  }
  const int danger = std::max(0, maximum_height - 4);
  result.danger_height_squared = danger * danger;
  return result;
}

double teacherLeaf(const State& state) {
  if (state.game_over) return -2'500'000.0;
  const TeacherFeatures features = teacherFeatures(state);
  const auto& f = features.phase;
  double result = 0;
  result += 180.0 * f.open_columns;
  result += -20.0 * f.height_load;
  result += -620.0 * f.solid_cells;
  result += -220.0 * f.cracked_cells;
  result += -18.0 * f.numbered_cells;
  result += -90.0 * f.high_low_numbers;
  result += 1'600.0 * f.direct_potential;
  result += 700.0 * f.latent_chain_potential;
  result += 100.0 * f.cracked_exposure;
  result += 40.0 * f.solid_exposure;
  result += -550.0 * f.adjacent_ones;
  result += -750.0 * f.triple_twos;
  result += -120.0 * f.dead_low_numbers;
  result += -95.0 * features.covered_height_risk;
  result += -85.0 * features.low_number_height_risk;
  result += -1'250.0 * features.danger_height_squared;
  result += -35.0 * features.rise_pressure;
  result += 220.0 * features.next_disc_vertical_options;
  return result;
}

struct TeacherDecision {
  int action = -1;
  std::array<double, kBoardSize> values{};
  std::uint64_t work = 0;
};

double teacherBestValue(const State& state, int depth, std::uint64_t& work);

double teacherActionValue(const State& state, int action, int depth,
                          std::uint64_t& work) {
  const std::uint32_t state_seed =
      cfpi::detail::scenarioSeedForState(state, kTeacherPolicySeed, depth);
  double total = 0;
  for (int sample = 0; sample < kTeacherChanceSamples; ++sample) {
    cfpi::detail::StratifiedRandom random{
        state_seed, sample, kTeacherChanceSamples, 0};
    MoveResult move;
    if (!cfpi::detail::playMoveSampled(state, action, random, move)) {
      total += kTeacherTerminalUtility;
      ++work;
      continue;
    }
    ++work;
    const double reward = static_cast<double>(move.score_delta);
    if (move.state.game_over) {
      total += reward + kTeacherTerminalUtility;
      continue;
    }
    move.state = publicState(move.state);
    move.state.next_disc = cfpi::detail::sampledNextDisc(
        state_seed, sample, kTeacherChanceSamples);
    bool ignored = false;
    const State next = cfpi::detail::canonicalState(move.state, ignored);
    total += reward + teacherBestValue(next, depth - 1, work);
  }
  return total / kTeacherChanceSamples;
}

double teacherBestValue(const State& state, int depth, std::uint64_t& work) {
  if (state.game_over) return kTeacherTerminalUtility;
  if (depth == 0) {
    ++work;
    return teacherLeaf(state);
  }
  double best = -std::numeric_limits<double>::infinity();
  for (int action : cfpi::detail::kColumnOrder) {
    if (!isLegal(state.board, action)) continue;
    best = std::max(best,
                    teacherActionValue(state, action, depth, work));
  }
  return std::isfinite(best) ? best : kTeacherTerminalUtility;
}

TeacherDecision teacherDecision(const State& source, int depth) {
  if (depth < 1 || depth > 2) {
    throw std::invalid_argument("teacher depth must be one or two");
  }
  TeacherDecision result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  if (source.game_over) return result;
  bool mirrored = false;
  const State canonical =
      cfpi::detail::canonicalState(publicState(source), mirrored);
  int canonical_action = -1;
  double best = -std::numeric_limits<double>::infinity();
  std::array<double, kBoardSize> canonical_values{};
  canonical_values.fill(-std::numeric_limits<double>::infinity());
  for (int action : cfpi::detail::kColumnOrder) {
    if (!isLegal(canonical.board, action)) continue;
    const double value =
        teacherActionValue(canonical, action, depth, result.work);
    canonical_values[action] = value;
    if (canonical_action < 0 || value > best) {
      canonical_action = action;
      best = value;
    }
  }
  for (int action = 0; action < kBoardSize; ++action) {
    const int physical = mirrored ? kBoardSize - 1 - action : action;
    result.values[physical] = canonical_values[action];
  }
  result.action = canonical_action < 0
                      ? -1
                      : (mirrored ? kBoardSize - 1 - canonical_action
                                  : canonical_action);
  return result;
}

class CoreVectorEnvironment {
 public:
  CoreVectorEnvironment(int environments, std::uint32_t seed_start,
                        std::uint32_t seed_count, int maximum_moves)
      : maximum_moves_(maximum_moves), next_seed_(seed_start),
        seed_end_(static_cast<std::uint64_t>(seed_start) + seed_count) {
    if (environments < 1 || environments > kMaximumEnvironments) {
      throw std::invalid_argument("environment count is outside fixed bounds");
    }
    if (maximum_moves < 1 || maximum_moves > kMaximumMoves) {
      throw std::invalid_argument("maximum moves is outside fixed bounds");
    }
    if (seed_count < static_cast<std::uint32_t>(environments) ||
        seed_start < kAllowedSeedMinimum ||
        seed_end_ > kAllowedSeedEndExclusive) {
      throw std::invalid_argument(
          "RL seeds must stay within the sealed 0x3d30..0x3d3f range");
    }
    const std::size_t count = static_cast<std::size_t>(environments);
    states_.resize(count);
    seeds_.resize(count);
    running_.resize(count);
    active_.assign(count, false);
    needs_reset_.assign(count, false);
    for (int index = 0; index < environments; ++index) {
      if (!startEpisode(index)) {
        throw std::logic_error("initial seed allocation unexpectedly failed");
      }
    }
  }

  int size() const { return static_cast<int>(states_.size()); }
  std::uint64_t gamesStarted() const { return games_started_; }
  std::uint64_t gamesCompleted() const { return games_completed_; }

  bool active(int index) const { return active_.at(offset(index)); }
  const State& state(int index) const { return states_.at(offset(index)); }
  std::uint8_t publicLegalMask(int index) const {
    return active(index) ? legalMask(state(index).board) : 0;
  }

  std::vector<Transition> step(const std::vector<int>& actions) {
    if (actions.size() != states_.size()) {
      throw std::invalid_argument("action vector size mismatch");
    }
    std::vector<Transition> result(states_.size());
    for (int index = 0; index < size(); ++index) {
      const std::size_t at = static_cast<std::size_t>(index);
      if (!active_[at]) {
        if (actions[at] != -1) {
          throw std::invalid_argument("inactive environments require action -1");
        }
        continue;
      }
      State& current = states_[at];
      const int action = actions[at];
      if (!isLegal(current.board, action)) {
        throw std::invalid_argument("policy supplied an illegal action");
      }
      MoveResult move;
      if (!playHeadlessMove(current, seeds_[at], action, move)) {
        throw std::runtime_error("native RL transition failed");
      }
      Transition& transition = result[at];
      transition.stepped = true;
      transition.score_delta = move.score_delta;
      for (const Wave& wave : move.waves) {
        transition.numbered_cleared += wave.cleared;
        transition.covers_revealed += wave.revealed;
      }
      RunningEpisode& running = running_[at];
      running.numbered_cleared +=
          static_cast<std::uint64_t>(transition.numbered_cleared);
      running.covers_revealed +=
          static_cast<std::uint64_t>(transition.covers_revealed);
      running.maximum_chain =
          std::max(running.maximum_chain, static_cast<int>(move.waves.size()));
      transition.terminated = current.game_over;
      transition.truncated =
          !transition.terminated && current.moves_played >= maximum_moves_;
      if (transition.terminated || transition.truncated) {
        transition.episode = {
            current.score, current.moves_played, running.numbered_cleared,
            running.covers_revealed, running.maximum_chain,
            transition.truncated};
        completed_.push_back(transition.episode);
        ++games_completed_;
        active_[at] = false;
        needs_reset_[at] = true;
      }
    }
    return result;
  }

  int resetDone() {
    int started = 0;
    for (int index = 0; index < size(); ++index) {
      const std::size_t at = static_cast<std::size_t>(index);
      if (!needs_reset_[at]) continue;
      needs_reset_[at] = false;
      if (startEpisode(index)) ++started;
    }
    return started;
  }

  std::vector<Episode> takeCompleted() {
    std::vector<Episode> result;
    result.swap(completed_);
    return result;
  }

  TeacherDecision teacher(int index, int depth) const {
    if (!active(index)) return {};
    return teacherDecision(state(index), depth);
  }

 private:
  std::size_t offset(int index) const {
    if (index < 0 || index >= size()) {
      throw std::out_of_range("environment index out of range");
    }
    return static_cast<std::size_t>(index);
  }

  bool startEpisode(int index) {
    if (next_seed_ >= seed_end_) return false;
    const std::size_t at = offset(index);
    const std::uint32_t seed = static_cast<std::uint32_t>(next_seed_++);
    seeds_[at] = seed;
    states_[at] = initialHeadlessState(seed);
    running_[at] = {};
    active_[at] = true;
    needs_reset_[at] = false;
    ++games_started_;
    return true;
  }

  int maximum_moves_ = 0;
  std::uint64_t next_seed_ = 0;
  std::uint64_t seed_end_ = 0;
  std::uint64_t games_started_ = 0;
  std::uint64_t games_completed_ = 0;
  std::vector<State> states_;
  std::vector<std::uint32_t> seeds_;
  std::vector<RunningEpisode> running_;
  std::vector<bool> active_;
  std::vector<bool> needs_reset_;
  std::vector<Episode> completed_;
};

struct SelfTestReport {
  bool passed = false;
  bool exact_engine_parity = false;
  bool cumulative_episode_counters = false;
  bool deterministic_seed_boundary = false;
  bool conflicting_seed_starts_rejected = false;
  bool terminal_reset_semantics = false;
  bool illegal_action_rejected = false;
  bool teacher_metadata_blind = false;
  bool teacher_reflection = false;
  bool teacher_depths = false;
  std::uint64_t parity_transitions = 0;
  std::uint64_t teacher_d1_work = 0;
  std::uint64_t teacher_d2_work = 0;
};

SelfTestReport selfTestCore() {
  SelfTestReport report;
  constexpr std::uint32_t parity_start = 0x3d30'0000u;
  constexpr int parity_games = 3;
  CoreVectorEnvironment environment(parity_games, parity_start, parity_games,
                                    1'000);
  std::array<State, parity_games> direct{};
  std::array<RunningEpisode, parity_games> counters{};
  for (int index = 0; index < parity_games; ++index) {
    direct[index] =
        initialHeadlessState(parity_start + static_cast<std::uint32_t>(index));
  }
  bool parity = true;
  bool cumulative = true;
  while (environment.gamesCompleted() < parity_games) {
    std::vector<int> actions(parity_games, -1);
    for (int index = 0; index < parity_games; ++index) {
      if (environment.active(index)) {
        actions[static_cast<std::size_t>(index)] =
            centerFirstMove(direct[index].board);
      }
    }
    const auto transitions = environment.step(actions);
    for (int index = 0; index < parity_games; ++index) {
      if (!transitions[index].stepped) continue;
      MoveResult move;
      const std::uint32_t seed =
          parity_start + static_cast<std::uint32_t>(index);
      const bool played =
          playHeadlessMove(direct[index], seed, actions[index], move);
      parity &= played && equalState(direct[index], environment.state(index));
      parity &= transitions[index].score_delta == move.score_delta;
      int cleared = 0;
      int revealed = 0;
      for (const Wave& wave : move.waves) {
        cleared += wave.cleared;
        revealed += wave.revealed;
      }
      counters[index].numbered_cleared += cleared;
      counters[index].covers_revealed += revealed;
      counters[index].maximum_chain =
          std::max(counters[index].maximum_chain,
                   static_cast<int>(move.waves.size()));
      parity &= transitions[index].numbered_cleared == cleared;
      parity &= transitions[index].covers_revealed == revealed;
      ++report.parity_transitions;
      if (transitions[index].terminated || transitions[index].truncated) {
        cumulative &= transitions[index].episode.score == direct[index].score;
        cumulative &=
            transitions[index].episode.moves == direct[index].moves_played;
        cumulative &= transitions[index].episode.numbered_cleared ==
                      counters[index].numbered_cleared;
        cumulative &= transitions[index].episode.covers_revealed ==
                      counters[index].covers_revealed;
        cumulative &= transitions[index].episode.maximum_chain ==
                      counters[index].maximum_chain;
        cumulative &= !transitions[index].episode.censored;
      }
    }
  }
  const auto completed = environment.takeCompleted();
  cumulative &= completed.size() == parity_games;
  report.exact_engine_parity = parity;
  report.cumulative_episode_counters = cumulative;

  CoreVectorEnvironment boundary(2, 0x3d30'0010u, 4, 1);
  const std::vector<int> first_actions{3, 3};
  const auto first = boundary.step(first_actions);
  bool reset_semantics = first[0].truncated && first[1].truncated &&
                         !first[0].terminated && !first[1].terminated &&
                         !boundary.active(0) && !boundary.active(1);
  reset_semantics &= boundary.resetDone() == 2;
  reset_semantics &= boundary.gamesStarted() == 4;
  const auto second = boundary.step(first_actions);
  reset_semantics &= second[0].truncated && second[1].truncated;
  reset_semantics &= boundary.resetDone() == 0;
  reset_semantics &= !boundary.active(0) && !boundary.active(1) &&
                     boundary.gamesStarted() == 4 &&
                     boundary.gamesCompleted() == 4;
  bool rejected_low = false;
  bool rejected_high = false;
  bool rejected_conflicts = true;
  try {
    CoreVectorEnvironment invalid(1, kAllowedSeedMinimum - 1, 1, 10);
    (void)invalid;
  } catch (const std::invalid_argument&) {
    rejected_low = true;
  }
  try {
    CoreVectorEnvironment invalid(
        1, static_cast<std::uint32_t>(kAllowedSeedEndExclusive - 1), 2, 10);
    (void)invalid;
  } catch (const std::invalid_argument&) {
    rejected_high = true;
  }
  for (const std::uint32_t start :
       {0x3d40'0000u, 0x3d50'0000u, 0x3d60'0000u}) {
    try {
      CoreVectorEnvironment invalid(1, start, 1, 10);
      (void)invalid;
      rejected_conflicts = false;
    } catch (const std::invalid_argument&) {
    }
  }
  report.deterministic_seed_boundary = rejected_low && rejected_high;
  report.conflicting_seed_starts_rejected = rejected_conflicts;
  report.terminal_reset_semantics = reset_semantics;

  bool illegal_rejected = false;
  try {
    CoreVectorEnvironment illegal(1, 0x3d30'0018u, 1, 10);
    (void)illegal.step(std::vector<int>{-1});
  } catch (const std::invalid_argument&) {
    illegal_rejected = true;
  }
  report.illegal_action_rejected = illegal_rejected;

  State fixture = initialHeadlessState(0x3d30'0020u);
  for (int action : {3, 2, 4, 1, 5, 0}) {
    MoveResult move;
    if (!playHeadlessMove(fixture, 0x3d30'0020u, action, move)) break;
  }
  const TeacherDecision d1 = teacherDecision(fixture, 1);
  const TeacherDecision d2 = teacherDecision(fixture, 2);
  State metadata = fixture;
  metadata.score = 9'876'543;
  metadata.level = 777;
  metadata.moves_played = 999;
  const TeacherDecision metadata_d2 = teacherDecision(metadata, 2);
  report.teacher_metadata_blind =
      d2.action == metadata_d2.action && d2.values == metadata_d2.values &&
      d2.work == metadata_d2.work;
  State mirrored = fixture;
  mirrored.board = mirrorBoard(fixture.board);
  const TeacherDecision mirror_d2 = teacherDecision(mirrored, 2);
  bool reflection =
      mirror_d2.action == kBoardSize - 1 - d2.action && d2.action >= 0;
  for (int action = 0; action < kBoardSize; ++action) {
    const double direct_value = d2.values[action];
    const double reflected_value = mirror_d2.values[kBoardSize - 1 - action];
    reflection &= (std::isfinite(direct_value) ==
                   std::isfinite(reflected_value));
    if (std::isfinite(direct_value)) {
      reflection &= direct_value == reflected_value;
    }
  }
  report.teacher_reflection = reflection;
  report.teacher_d1_work = d1.work;
  report.teacher_d2_work = d2.work;
  report.teacher_depths = d1.action >= 0 && d2.action >= 0 &&
                          isLegal(fixture.board, d1.action) &&
                          isLegal(fixture.board, d2.action) &&
                          d1.work > 0 && d2.work > d1.work;
  report.passed =
      report.exact_engine_parity && report.cumulative_episode_counters &&
      report.deterministic_seed_boundary &&
      report.conflicting_seed_starts_rejected &&
      report.terminal_reset_semantics && report.illegal_action_rejected &&
      report.teacher_metadata_blind && report.teacher_reflection &&
      report.teacher_depths;
  return report;
}

void printSelfTest(const SelfTestReport& report, std::ostream& output) {
  output << "TORCH_ENV_SELF_TEST {\"passed\":"
         << (report.passed ? "true" : "false")
         << ",\"exactEngineParity\":"
         << (report.exact_engine_parity ? "true" : "false")
         << ",\"cumulativeEpisodeCounters\":"
         << (report.cumulative_episode_counters ? "true" : "false")
         << ",\"deterministicSeedBoundary\":"
         << (report.deterministic_seed_boundary ? "true" : "false")
         << ",\"conflictingSeedStartsRejected\":"
         << (report.conflicting_seed_starts_rejected ? "true" : "false")
         << ",\"terminalResetSemantics\":"
         << (report.terminal_reset_semantics ? "true" : "false")
         << ",\"illegalActionRejected\":"
         << (report.illegal_action_rejected ? "true" : "false")
         << ",\"teacherMetadataBlind\":"
         << (report.teacher_metadata_blind ? "true" : "false")
         << ",\"teacherReflection\":"
         << (report.teacher_reflection ? "true" : "false")
         << ",\"teacherDepths\":"
         << (report.teacher_depths ? "true" : "false")
         << ",\"parityTransitions\":" << report.parity_transitions
         << ",\"teacherD1Work\":" << report.teacher_d1_work
         << ",\"teacherD2Work\":" << report.teacher_d2_work << "}\n";
}

#ifndef DROP7_TORCH_ENV_CORE_ONLY

namespace py = pybind11;

class VectorEnvironment {
 public:
  VectorEnvironment(int environments, std::uint32_t seed_start,
                    std::uint32_t seed_count, int maximum_moves)
      : core_(environments, seed_start, seed_count, maximum_moves) {}

  py::tuple observations() const {
    const py::ssize_t count = core_.size();
    py::array_t<std::uint8_t> boards({count, py::ssize_t{kCellCount}});
    py::array_t<std::uint8_t> discs(count);
    py::array_t<std::uint8_t> phases(count);
    py::array_t<std::uint8_t> legal_masks(count);
    py::array_t<std::uint8_t> active(count);
    auto board_view = boards.mutable_unchecked<2>();
    auto disc_view = discs.mutable_unchecked<1>();
    auto phase_view = phases.mutable_unchecked<1>();
    auto mask_view = legal_masks.mutable_unchecked<1>();
    auto active_view = active.mutable_unchecked<1>();
    for (py::ssize_t index = 0; index < count; ++index) {
      const State& state = core_.state(static_cast<int>(index));
      for (int cell = 0; cell < kCellCount; ++cell) {
        board_view(index, cell) = state.board[static_cast<std::size_t>(cell)];
      }
      disc_view(index) = state.next_disc;
      phase_view(index) = static_cast<std::uint8_t>(state.moves_remaining);
      mask_view(index) = core_.publicLegalMask(static_cast<int>(index));
      active_view(index) = core_.active(static_cast<int>(index)) ? 1 : 0;
    }
    return py::make_tuple(std::move(boards), std::move(discs),
                          std::move(phases), std::move(legal_masks),
                          std::move(active));
  }

  py::tuple step(
      py::array_t<std::int64_t, py::array::c_style | py::array::forcecast>
          actions) {
    if (actions.ndim() != 1 || actions.shape(0) != core_.size()) {
      throw std::invalid_argument("actions must have shape [environments]");
    }
    std::vector<int> native_actions(static_cast<std::size_t>(core_.size()));
    const auto action_view = actions.unchecked<1>();
    for (int index = 0; index < core_.size(); ++index) {
      const std::int64_t action = action_view(index);
      if (action < std::numeric_limits<int>::min() ||
          action > std::numeric_limits<int>::max()) {
        throw std::invalid_argument("action does not fit native integer");
      }
      native_actions[static_cast<std::size_t>(index)] =
          static_cast<int>(action);
    }
    const auto transitions = core_.step(native_actions);
    const py::ssize_t count = core_.size();
    py::array_t<std::int64_t> score_deltas(count);
    py::array_t<std::int32_t> numbered_cleared(count);
    py::array_t<std::int32_t> covers_revealed(count);
    py::array_t<std::uint8_t> terminated(count);
    py::array_t<std::uint8_t> truncated(count);
    py::array_t<std::int64_t> episode_scores(count);
    py::array_t<std::int32_t> episode_moves(count);
    py::array_t<std::int64_t> episode_cleared(count);
    py::array_t<std::int64_t> episode_revealed(count);
    py::array_t<std::int32_t> episode_maximum_chain(count);
    auto score_view = score_deltas.mutable_unchecked<1>();
    auto clear_view = numbered_cleared.mutable_unchecked<1>();
    auto reveal_view = covers_revealed.mutable_unchecked<1>();
    auto terminated_view = terminated.mutable_unchecked<1>();
    auto truncated_view = truncated.mutable_unchecked<1>();
    auto episode_score_view = episode_scores.mutable_unchecked<1>();
    auto episode_move_view = episode_moves.mutable_unchecked<1>();
    auto episode_clear_view = episode_cleared.mutable_unchecked<1>();
    auto episode_reveal_view = episode_revealed.mutable_unchecked<1>();
    auto episode_chain_view = episode_maximum_chain.mutable_unchecked<1>();
    for (py::ssize_t index = 0; index < count; ++index) {
      const Transition& transition =
          transitions[static_cast<std::size_t>(index)];
      score_view(index) = transition.score_delta;
      clear_view(index) = transition.numbered_cleared;
      reveal_view(index) = transition.covers_revealed;
      terminated_view(index) = transition.terminated ? 1 : 0;
      truncated_view(index) = transition.truncated ? 1 : 0;
      const bool done = transition.terminated || transition.truncated;
      episode_score_view(index) = done ? transition.episode.score : -1;
      episode_move_view(index) = done ? transition.episode.moves : -1;
      episode_clear_view(index) =
          done ? static_cast<std::int64_t>(
                     transition.episode.numbered_cleared)
               : -1;
      episode_reveal_view(index) =
          done ? static_cast<std::int64_t>(
                     transition.episode.covers_revealed)
               : -1;
      episode_chain_view(index) =
          done ? transition.episode.maximum_chain : -1;
    }
    py::tuple next = observations();
    return py::make_tuple(
        next[0], next[1], next[2], next[3], next[4],
        std::move(score_deltas), std::move(numbered_cleared),
        std::move(covers_revealed), std::move(terminated),
        std::move(truncated), std::move(episode_scores),
        std::move(episode_moves), std::move(episode_cleared),
        std::move(episode_revealed), std::move(episode_maximum_chain));
  }

  py::tuple resetDone() {
    const int started = core_.resetDone();
    py::tuple next = observations();
    return py::make_tuple(next[0], next[1], next[2], next[3], next[4],
                          started);
  }

  py::tuple teacherActions(int depth, int threads) const {
    if (depth < 1 || depth > 2) {
      throw std::invalid_argument("teacher depth must be one or two");
    }
    if (threads < 1 || threads > 16) {
      throw std::invalid_argument("teacher threads must be from 1 to 16");
    }
    const py::ssize_t count = core_.size();
    std::vector<TeacherDecision> decisions(
        static_cast<std::size_t>(count));
    std::atomic<py::ssize_t> cursor{0};
    std::exception_ptr worker_error;
    std::mutex worker_error_mutex;
    const int worker_count =
        std::min<int>(threads, static_cast<int>(count));
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(worker_count));
    {
      py::gil_scoped_release release;
      for (int worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&]() {
          try {
            for (;;) {
              const py::ssize_t index = cursor.fetch_add(1);
              if (index >= count) return;
              decisions[static_cast<std::size_t>(index)] =
                  core_.teacher(static_cast<int>(index), depth);
            }
          } catch (...) {
            const std::lock_guard<std::mutex> lock(worker_error_mutex);
            if (!worker_error) worker_error = std::current_exception();
            cursor.store(count);
          }
        });
      }
      for (std::thread& worker : workers) worker.join();
    }
    if (worker_error) std::rethrow_exception(worker_error);
    py::array_t<std::int64_t> actions(count);
    py::array_t<double> values({count, py::ssize_t{kBoardSize}});
    py::array_t<std::uint64_t> work(count);
    auto action_view = actions.mutable_unchecked<1>();
    auto value_view = values.mutable_unchecked<2>();
    auto work_view = work.mutable_unchecked<1>();
    for (py::ssize_t index = 0; index < count; ++index) {
      const TeacherDecision& decision =
          decisions[static_cast<std::size_t>(index)];
      action_view(index) = decision.action;
      work_view(index) = decision.work;
      for (int action = 0; action < kBoardSize; ++action) {
        value_view(index, action) = decision.values[action];
      }
    }
    return py::make_tuple(std::move(actions), std::move(values),
                          std::move(work));
  }

  py::list takeCompleted() {
    py::list result;
    for (const Episode& episode : core_.takeCompleted()) {
      py::dict item;
      item["score"] = episode.score;
      item["moves"] = episode.moves;
      item["numberedCleared"] = episode.numbered_cleared;
      item["coversRevealed"] = episode.covers_revealed;
      item["maximumChain"] = episode.maximum_chain;
      item["censored"] = episode.censored;
      result.append(std::move(item));
    }
    return result;
  }

  std::uint64_t gamesStarted() const { return core_.gamesStarted(); }
  std::uint64_t gamesCompleted() const { return core_.gamesCompleted(); }
  int size() const { return core_.size(); }

 private:
  CoreVectorEnvironment core_;
};

py::dict pythonSelfTest() {
  const SelfTestReport report = selfTestCore();
  py::dict result;
  result["passed"] = report.passed;
  result["exactEngineParity"] = report.exact_engine_parity;
  result["cumulativeEpisodeCounters"] =
      report.cumulative_episode_counters;
  result["deterministicSeedBoundary"] =
      report.deterministic_seed_boundary;
  result["conflictingSeedStartsRejected"] =
      report.conflicting_seed_starts_rejected;
  result["terminalResetSemantics"] = report.terminal_reset_semantics;
  result["illegalActionRejected"] = report.illegal_action_rejected;
  result["teacherMetadataBlind"] = report.teacher_metadata_blind;
  result["teacherReflection"] = report.teacher_reflection;
  result["teacherDepths"] = report.teacher_depths;
  result["parityTransitions"] = report.parity_transitions;
  result["teacherD1Work"] = report.teacher_d1_work;
  result["teacherD2Work"] = report.teacher_d2_work;
  return result;
}

#endif

}  // namespace drop7::torch_env

#if defined(DROP7_TORCH_ENV_STANDALONE)
int main() {
  try {
    const auto report = drop7::torch_env::selfTestCore();
    drop7::torch_env::printSelfTest(report, std::cout);
    return report.passed ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "drop7_torch_env: " << error.what() << '\n';
    return 1;
  }
}
#elif !defined(DROP7_TORCH_ENV_CORE_ONLY)
PYBIND11_MODULE(TORCH_EXTENSION_NAME, module) {
  namespace env = drop7::torch_env;
  pybind11::class_<env::VectorEnvironment>(module, "VectorEnvironment")
      .def(pybind11::init<int, std::uint32_t, std::uint32_t, int>(),
           pybind11::arg("environments"), pybind11::arg("seed_start"),
           pybind11::arg("seed_count"), pybind11::arg("maximum_moves"))
      .def("observations", &env::VectorEnvironment::observations)
      .def("step", &env::VectorEnvironment::step)
      .def("reset_done", &env::VectorEnvironment::resetDone)
      .def("teacher_actions", &env::VectorEnvironment::teacherActions,
           pybind11::arg("depth"), pybind11::arg("threads") = 4)
      .def("take_completed", &env::VectorEnvironment::takeCompleted)
      .def_property_readonly("games_started",
                             &env::VectorEnvironment::gamesStarted)
      .def_property_readonly("games_completed",
                             &env::VectorEnvironment::gamesCompleted)
      .def_property_readonly("size", &env::VectorEnvironment::size);
  module.def("self_test", &env::pythonSelfTest);
  module.attr("board_size") = drop7::kBoardSize;
  module.attr("cell_count") = drop7::kCellCount;
  module.attr("moves_per_level") = drop7::kMovesPerLevel;
  module.attr("level_bonus") = drop7::kLevelBonus;
}
#endif
