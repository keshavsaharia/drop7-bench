#define main drop7_constructive_spectrum_frozen_entrypoint
#include "constructive-spectrum.cpp"
#undef main

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <type_traits>
#include <utility>
#include <vector>

// Applies a coefficient-free risk gate to the fixed H12 policy, with H7 as the
// immutable fallback.  If the two actions differ, both actions are
// evaluated on the same independent 21-stratum stochastic tape derived only
// from the canonical public root.  H12 is accepted only by Pareto dominance
// over mean and lower-quartile return, terminal count, and clear/reveal flow.
namespace drop7::constructive_h12_risk_gate {

namespace frozen = drop7::constructive_spectrum;
namespace detail = drop7::cfpi::detail;
using Clock = std::chrono::steady_clock;
using PublicState = frozen::PublicState;
using ProposalDecision = frozen::Decision;

constexpr std::uint32_t kFittingSeedStart = 0x3d6a'6000u;
constexpr std::uint32_t kFittingSeedEndExclusive = 0x3d6a'6020u;
constexpr int kFittingGames = 32;
constexpr std::uint32_t kScreenSeedStart = 0x3d6a'7000u;
constexpr std::uint32_t kScreenSeedEndExclusive = 0x3d6a'7040u;
constexpr int kScreenGames = 64;
constexpr int kMaximumMoves = 1'000;
constexpr int kPanelScenarios = 21;
constexpr int kLowerQuartileScenarios = kPanelScenarios / 4;
constexpr int kDefaultThreads = 8;
constexpr double kRequiredRatio = 1.10;
constexpr int kFitJointWins = 20;
constexpr int kScreenJointWins = 40;
constexpr double kWallLimitSeconds = 45.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr std::uint32_t kPanelMasterDomain = 0x4831'3250u;  // "H12P"
constexpr std::uint32_t kPanelRevealDomain = 0x5052'564cu;  // "PRVL"
constexpr std::uint32_t kPanelVisibleDomain = 0x5056'4953u; // "PVIS"
constexpr std::uint32_t kPanelStepMultiplier = 0x27d4'eb2du;
constexpr std::uint32_t kPanelEventMultiplier = 0x1656'67b1u;

static_assert(kLevelBonus == 17'000 && kMovesPerLevel == 5);
static_assert(frozen::kChanceSamples == 7 &&
              frozen::kTacticalDepth == 3 &&
              frozen::kTacticalShortlist == 2 &&
              frozen::kTacticalNearTie == 2'500.0 &&
              frozen::kPolicySeed == 0x4353'5031u);
static_assert(frozen::kMaximumHorizon == 7);
static_assert(kPanelScenarios == 21 && kLowerQuartileScenarios == 5);
static_assert(kPanelMasterDomain != frozen::kPolicySeed &&
              kPanelRevealDomain != detail::kRevealSampleDomain &&
              kPanelRevealDomain != detail::kDiscSampleDomain &&
              kPanelVisibleDomain != detail::kRevealSampleDomain &&
              kPanelVisibleDomain != detail::kDiscSampleDomain &&
              kPanelRevealDomain != kPanelVisibleDomain);
static_assert(kFittingSeedEndExclusive - kFittingSeedStart == kFittingGames);
static_assert(kScreenSeedEndExclusive - kScreenSeedStart == kScreenGames);
static_assert(kFittingSeedEndExclusive <= kScreenSeedStart);
static_assert((kFittingSeedStart >> 16u) == 0x3d6au &&
              ((kFittingSeedEndExclusive - 1u) >> 16u) == 0x3d6au &&
              (kScreenSeedStart >> 16u) == 0x3d6au &&
              ((kScreenSeedEndExclusive - 1u) >> 16u) == 0x3d6au);
static_assert((kFittingSeedStart >> 24u) != 0x4du &&
              (kFittingSeedStart >> 24u) != 0x7du &&
              (kFittingSeedStart >> 24u) != 0xd7u &&
              (kScreenSeedStart >> 24u) != 0x4du &&
              (kScreenSeedStart >> 24u) != 0x7du &&
              (kScreenSeedStart >> 24u) != 0xd7u);

std::uint64_t peakRssBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ull;
#endif
}

void enforceRssLimit() {
  if (peakRssBytes() > kRssLimitBytes) {
    throw std::runtime_error("H12 risk gate exceeded 256 MiB RSS cap");
  }
}

struct Deadline {
  Clock::time_point started = Clock::now();

  double seconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }

  void check() const {
    if (seconds() > kWallLimitSeconds) {
      throw std::runtime_error("H12 risk gate exceeded 45 minute wall cap");
    }
  }
};

int h12Horizon(const PublicState& state) {
  return std::clamp(static_cast<int>(state.moves_remaining) +
                        2 * kMovesPerLevel,
                    frozen::kMinimumHorizon, 12);
}

ProposalDecision chooseH12Canonical(const PublicState& source) {
  ProposalDecision result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  if (source.terminal) return result;
  const State root = frozen::materialize(source);
  fair_only_horizon::SearchContext tactical_context;
  const fair_only_horizon::RootEvaluation tactical =
      fair_only_horizon::rootDecision(root, frozen::kTacticalDepth,
                                      tactical_context);
  result.work += tactical_context.work;
  result.tactical_action = tactical.action;
  std::array<int, kBoardSize> tactical_rank{};
  tactical_rank.fill(kBoardSize);
  std::array<int, kBoardSize> ranked_columns{};
  int ranked_count = 0;
  for (const int column : frozen::kColumnOrder) {
    if (isLegal(root.board, column)) ranked_columns[ranked_count++] = column;
  }
  std::stable_sort(ranked_columns.begin(), ranked_columns.begin() + ranked_count,
                   [&](int left, int right) {
                     return tactical.values[left] > tactical.values[right];
                   });
  for (int rank = 0; rank < ranked_count; ++rank) {
    tactical_rank[ranked_columns[rank]] = rank;
  }
  result.horizon = h12Horizon(source);
  for (const int root_column : frozen::kColumnOrder) {
    if (!isLegal(root.board, root_column)) continue;
    if (tactical_rank[root_column] >= frozen::kTacticalShortlist) continue;
    if (tactical.values[root_column] <
        tactical.value - frozen::kTacticalNearTie) {
      continue;
    }
    ++result.shortlist;
    double root_total = 0.0;
    for (int root_sample = 0; root_sample < frozen::kChanceSamples;
         ++root_sample) {
      const frozen::SampledStep first = frozen::sampledStep(
          root, root_column, root_sample, result.horizon);
      ++result.work;
      if (!first.played || first.state.game_over) {
        root_total += frozen::kTerminalValue;
        continue;
      }
      State state = first.state;
      double trajectory = static_cast<double>(first.score_delta) +
                          5'000.0 * first.clears +
                          8'000.0 * first.reveals + 500.0 * first.waves;
      bool terminal = false;
      for (int step_index = 1; step_index < result.horizon; ++step_index) {
        const int depth_tag = result.horizon - step_index;
        const frozen::OneStepDecision continuation =
            frozen::constructiveContinuation(state, depth_tag);
        result.work += continuation.work;
        if (continuation.action < 0) {
          terminal = true;
          break;
        }
        const int sample =
            (root_sample + 2 * step_index) % frozen::kChanceSamples;
        const frozen::SampledStep next = frozen::sampledStep(
            state, continuation.action, sample, depth_tag);
        ++result.work;
        if (!next.played || next.state.game_over) {
          terminal = true;
          break;
        }
        trajectory += static_cast<double>(next.score_delta) +
                      5'000.0 * next.clears + 8'000.0 * next.reveals +
                      500.0 * next.waves;
        state = next.state;
      }
      root_total += terminal
                        ? frozen::kTerminalValue
                        : trajectory +
                              frozen::structuralValue(
                                  frozen::publicState(state));
    }
    result.values[root_column] = root_total / frozen::kChanceSamples;
    if (result.action < 0 ||
        result.values[root_column] > result.values[result.action]) {
      result.action = root_column;
    }
  }
  if (result.action < 0) result.action = centerFirstMove(root.board);
  return result;
}

ProposalDecision chooseH12(const PublicState& source) {
  if (source.terminal) return {};
  bool mirrored = false;
  const PublicState canonical = frozen::canonicalPublic(source, mirrored);
  ProposalDecision result = chooseH12Canonical(canonical);
  if (!mirrored) return result;
  result.action = kBoardSize - 1 - result.action;
  result.tactical_action = kBoardSize - 1 - result.tactical_action;
  std::array<double, kBoardSize> values{};
  for (int column = 0; column < kBoardSize; ++column) {
    values[column] = result.values[kBoardSize - 1 - column];
  }
  result.values = values;
  return result;
}

std::uint32_t publicPanelHash(const PublicState& source) {
  bool ignored = false;
  const PublicState state = frozen::canonicalPublic(source, ignored);
  std::uint32_t hash = 0x811c'9dc5u;
  for (const std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint32_t>(cell + 1u);
    hash *= 0x0100'0193u;
  }
  hash ^= state.next_disc;
  hash *= 0x0100'0193u;
  hash ^= state.moves_remaining;
  hash *= 0x0100'0193u;
  return mix32(hash ^ kPanelMasterDomain);
}

struct PanelRandom {
  std::uint32_t master = 0;
  int scenario = 0;
  int step = 0;
  int event = 0;

  std::uint8_t nextDisc() {
    const std::uint32_t seed = mix32(
        master ^
        (static_cast<std::uint32_t>(step + 1) * kPanelStepMultiplier) ^
        (static_cast<std::uint32_t>(event + 1) * kPanelEventMultiplier));
    const double unit = detail::stratifiedUnit(
        seed, scenario, kPanelScenarios, kPanelRevealDomain, event++);
    return static_cast<std::uint8_t>(
        std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
  }
};

std::uint8_t panelVisibleDisc(std::uint32_t master, int scenario, int step) {
  const std::uint32_t seed = mix32(
      master ^
      (static_cast<std::uint32_t>(step + 1) * kPanelStepMultiplier));
  const double unit = detail::stratifiedUnit(
      seed, scenario, kPanelScenarios, kPanelVisibleDomain, 0);
  return static_cast<std::uint8_t>(
      std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
}

struct PanelStep {
  State state{};
  std::int64_t score_delta = 0;
  int clears = 0;
  int reveals = 0;
  int waves = 0;
  bool played = false;
};

PanelStep panelStep(const State& source, int source_column,
                    std::uint32_t master, int scenario, int step_index) {
  bool mirrored = false;
  const State canonical = detail::canonicalState(source, mirrored);
  const int column = mirrored ? kBoardSize - 1 - source_column : source_column;
  PanelStep result;
  if (!isLegal(canonical.board, column)) return result;
  PanelRandom random{master, scenario, step_index, 0};
  MoveResult move;
  if (!detail::playMoveSampled(canonical, column, random, move)) return result;
  result.played = true;
  result.score_delta = move.score_delta;
  result.waves = static_cast<int>(move.waves.size());
  for (const Wave& wave : move.waves) {
    result.clears += wave.cleared;
    result.reveals += wave.revealed;
  }
  if (!move.state.game_over) {
    move.state.next_disc = panelVisibleDisc(master, scenario, step_index);
  }
  bool ignored = false;
  result.state = detail::canonicalState(move.state, ignored);
  return result;
}

struct ScenarioResult {
  double value = frozen::kTerminalValue;
  int clears = 0;
  int reveals = 0;
  int moves = 0;
  bool terminal = true;
};

ScenarioResult runPanelScenario(const State& root, int root_action,
                                std::uint32_t master, int scenario,
                                int horizon, std::uint64_t& work) {
  ScenarioResult result;
  const PanelStep first =
      panelStep(root, root_action, master, scenario, 0);
  ++work;
  if (!first.played || first.state.game_over) return result;
  State state = first.state;
  result.value = static_cast<double>(first.score_delta) +
                 5'000.0 * first.clears + 8'000.0 * first.reveals +
                 500.0 * first.waves;
  result.clears = first.clears;
  result.reveals = first.reveals;
  result.moves = 1;
  result.terminal = false;
  for (int step_index = 1; step_index < horizon; ++step_index) {
    const int depth_tag = horizon - step_index;
    const frozen::OneStepDecision continuation =
        frozen::constructiveContinuation(state, depth_tag);
    work += continuation.work;
    if (continuation.action < 0) {
      result.terminal = true;
      result.value = frozen::kTerminalValue;
      return result;
    }
    const PanelStep next = panelStep(state, continuation.action, master,
                                     scenario, step_index);
    ++work;
    if (!next.played || next.state.game_over) {
      result.terminal = true;
      result.value = frozen::kTerminalValue;
      return result;
    }
    result.value += static_cast<double>(next.score_delta) +
                    5'000.0 * next.clears + 8'000.0 * next.reveals +
                    500.0 * next.waves;
    result.clears += next.clears;
    result.reveals += next.reveals;
    ++result.moves;
    state = next.state;
  }
  result.value += frozen::structuralValue(frozen::publicState(state));
  return result;
}

struct PanelStats {
  double mean_return = 0.0;
  double lower_quartile_return = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  int terminals = 0;
  std::uint64_t work = 0;

  bool operator==(const PanelStats&) const = default;
};

PanelStats evaluatePanelAction(const PublicState& public_root, int action) {
  bool mirrored = false;
  const PublicState canonical =
      frozen::canonicalPublic(public_root, mirrored);
  const int canonical_action =
      mirrored ? kBoardSize - 1 - action : action;
  const State root = frozen::materialize(canonical);
  const std::uint32_t master = publicPanelHash(canonical);
  const int horizon = h12Horizon(canonical);
  PanelStats result;
  std::array<double, kPanelScenarios> returns{};
  int clears = 0;
  int reveals = 0;
  int moves = 0;
  for (int scenario = 0; scenario < kPanelScenarios; ++scenario) {
    const ScenarioResult sample = runPanelScenario(
        root, canonical_action, master, scenario, horizon, result.work);
    returns[scenario] = sample.value;
    result.mean_return += sample.value;
    result.terminals += sample.terminal;
    clears += sample.clears;
    reveals += sample.reveals;
    moves += sample.moves;
  }
  result.mean_return /= kPanelScenarios;
  std::sort(returns.begin(), returns.end());
  result.lower_quartile_return =
      std::accumulate(returns.begin(),
                      returns.begin() + kLowerQuartileScenarios, 0.0) /
      kLowerQuartileScenarios;
  if (moves > 0) {
    result.clears_per_move = static_cast<double>(clears) / moves;
    result.reveals_per_move = static_cast<double>(reveals) / moves;
  }
  return result;
}

bool paretoDominates(const PanelStats& candidate,
                     const PanelStats& fallback) {
  const bool weak =
      candidate.mean_return >= fallback.mean_return &&
      candidate.lower_quartile_return >= fallback.lower_quartile_return &&
      candidate.terminals <= fallback.terminals &&
      candidate.clears_per_move >= fallback.clears_per_move &&
      candidate.reveals_per_move >= fallback.reveals_per_move;
  const bool strict =
      candidate.mean_return > fallback.mean_return ||
      candidate.lower_quartile_return > fallback.lower_quartile_return ||
      candidate.terminals < fallback.terminals ||
      candidate.clears_per_move > fallback.clears_per_move ||
      candidate.reveals_per_move > fallback.reveals_per_move;
  return weak && strict;
}

struct RiskDecision {
  int action = -1;
  int h7_action = -1;
  int h12_action = -1;
  bool proposals_agree = false;
  bool panel_evaluated = false;
  bool switched = false;
  ProposalDecision selected{};
  PanelStats h7_panel{};
  PanelStats h12_panel{};
  std::uint64_t proposal_work = 0;
  std::uint64_t panel_work = 0;

  bool operator==(const RiskDecision&) const = default;
};

RiskDecision chooseRiskCanonical(const PublicState& source) {
  RiskDecision result;
  if (source.terminal) return result;
  const ProposalDecision h7 = frozen::chooseActionCanonical(source);
  const ProposalDecision h12 = chooseH12Canonical(source);
  result.h7_action = h7.action;
  result.h12_action = h12.action;
  result.proposal_work = h7.work + h12.work;
  result.proposals_agree = h7.action == h12.action;
  result.selected = h7;
  result.action = h7.action;
  if (result.proposals_agree) return result;

  result.panel_evaluated = true;
  result.h7_panel = evaluatePanelAction(source, h7.action);
  result.h12_panel = evaluatePanelAction(source, h12.action);
  result.panel_work = result.h7_panel.work + result.h12_panel.work;
  if (paretoDominates(result.h12_panel, result.h7_panel)) {
    result.selected = h12;
    result.action = h12.action;
    result.switched = true;
  }
  return result;
}

void reflectProposal(ProposalDecision& proposal) {
  if (proposal.action >= 0) proposal.action = kBoardSize - 1 - proposal.action;
  if (proposal.tactical_action >= 0) {
    proposal.tactical_action =
        kBoardSize - 1 - proposal.tactical_action;
  }
  std::array<double, kBoardSize> values{};
  for (int column = 0; column < kBoardSize; ++column) {
    values[column] = proposal.values[kBoardSize - 1 - column];
  }
  proposal.values = values;
}

RiskDecision chooseRisk(const PublicState& source) {
  if (source.terminal) return {};
  bool mirrored = false;
  const PublicState canonical = frozen::canonicalPublic(source, mirrored);
  RiskDecision result = chooseRiskCanonical(canonical);
  if (!mirrored) return result;
  result.action = kBoardSize - 1 - result.action;
  result.h7_action = kBoardSize - 1 - result.h7_action;
  result.h12_action = kBoardSize - 1 - result.h12_action;
  reflectProposal(result.selected);
  return result;
}

using PublicRiskPolicy = RiskDecision (*)(const PublicState&);
static_assert(std::is_same_v<decltype(&chooseRisk), PublicRiskPolicy>);
static_assert(!std::is_invocable_v<PublicRiskPolicy, const State&>);

bool allowedFittingSeed(std::uint32_t seed) {
  return seed >= kFittingSeedStart && seed < kFittingSeedEndExclusive;
}

bool allowedScreenSeed(std::uint32_t seed) {
  return seed >= kScreenSeedStart && seed < kScreenSeedEndExclusive;
}

void requireSeed(std::uint32_t seed, bool screen) {
  if (screen ? !allowedScreenSeed(seed) : !allowedFittingSeed(seed)) {
    throw std::invalid_argument(
        screen ? "seed outside exact 0x3d6a7000 screen bank"
               : "seed outside exact 0x3d6a6000 fitting bank");
  }
}

enum class Policy : std::uint8_t { kRiskGate, kH7 };

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  int clears = 0;
  int reveals = 0;
  int waves = 0;
  int maximum_chain = 0;
  bool natural_terminal = false;
  bool capped = false;
  int agreements = 0;
  int disagreements = 0;
  int panel_evaluations = 0;
  int switches = 0;
  std::uint64_t proposal_work = 0;
  std::uint64_t panel_work = 0;
  double h7_panel_mean_return_sum = 0.0;
  double h12_panel_mean_return_sum = 0.0;
  double h7_panel_lower_quartile_sum = 0.0;
  double h12_panel_lower_quartile_sum = 0.0;
  double h7_panel_clears_sum = 0.0;
  double h12_panel_clears_sum = 0.0;
  double h7_panel_reveals_sum = 0.0;
  double h12_panel_reveals_sum = 0.0;
  int h7_panel_terminals = 0;
  int h12_panel_terminals = 0;
  std::uint64_t disc_hash = 0xcbf2'9ce4'8422'2325ull;
};

GameResult playGame(std::uint32_t seed, Policy policy,
                    const Deadline& deadline, bool screen) {
  requireSeed(seed, screen);
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    deadline.check();
    enforceRssLimit();
    if (state.next_disc != headlessDisc(seed, state.moves_played)) {
      throw std::runtime_error("headless disc stream guard failed");
    }
    result.disc_hash ^= state.next_disc;
    result.disc_hash *= 0x0000'0100'0000'01b3ull;
    int action = -1;
    if (policy == Policy::kRiskGate) {
      const RiskDecision decision = chooseRisk(frozen::publicState(state));
      action = decision.action;
      result.agreements += decision.proposals_agree;
      result.disagreements += !decision.proposals_agree;
      result.panel_evaluations += decision.panel_evaluated;
      result.switches += decision.switched;
      result.proposal_work += decision.proposal_work;
      result.panel_work += decision.panel_work;
      if (decision.panel_evaluated) {
        result.h7_panel_mean_return_sum += decision.h7_panel.mean_return;
        result.h12_panel_mean_return_sum += decision.h12_panel.mean_return;
        result.h7_panel_lower_quartile_sum +=
            decision.h7_panel.lower_quartile_return;
        result.h12_panel_lower_quartile_sum +=
            decision.h12_panel.lower_quartile_return;
        result.h7_panel_clears_sum += decision.h7_panel.clears_per_move;
        result.h12_panel_clears_sum += decision.h12_panel.clears_per_move;
        result.h7_panel_reveals_sum += decision.h7_panel.reveals_per_move;
        result.h12_panel_reveals_sum += decision.h12_panel.reveals_per_move;
        result.h7_panel_terminals += decision.h7_panel.terminals;
        result.h12_panel_terminals += decision.h12_panel.terminals;
      }
    } else {
      const ProposalDecision decision =
          frozen::chooseAction(frozen::publicState(state));
      action = decision.action;
      result.proposal_work += decision.work;
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("risk-gated policy selected illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("headless transition failed");
    }
    result.waves += static_cast<int>(move.waves.size());
    for (const Wave& wave : move.waves) {
      result.clears += wave.cleared;
      result.reveals += wave.revealed;
      result.maximum_chain = std::max(result.maximum_chain, wave.depth);
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.natural_terminal = state.game_over;
  result.capped = !state.game_over && state.moves_played == kMaximumMoves;
  return result;
}

struct Summary {
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double bottom_quartile_score = 0.0;
  double bottom_quartile_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double waves_per_move = 0.0;
  double agreement_rate = 0.0;
  double panel_rate = 0.0;
  double switch_rate_per_move = 0.0;
  double switch_rate_per_panel = 0.0;
  double mean_h7_panel_return = 0.0;
  double mean_h12_panel_return = 0.0;
  double mean_h7_panel_lower_quartile = 0.0;
  double mean_h12_panel_lower_quartile = 0.0;
  double mean_h7_panel_clears = 0.0;
  double mean_h12_panel_clears = 0.0;
  double mean_h7_panel_reveals = 0.0;
  double mean_h12_panel_reveals = 0.0;
  int h7_panel_terminals = 0;
  int h12_panel_terminals = 0;
  int agreements = 0;
  int disagreements = 0;
  int panel_evaluations = 0;
  int switches = 0;
  int natural_terminals = 0;
  int capped = 0;
  int maximum_chain = 0;
  std::uint64_t proposal_work = 0;
  std::uint64_t panel_work = 0;
};

Summary summarize(const std::vector<GameResult>& games) {
  if (games.empty()) throw std::invalid_argument("cannot summarize no games");
  Summary result;
  std::vector<std::int64_t> ordered_scores;
  std::vector<int> ordered_moves;
  std::int64_t scores = 0;
  std::int64_t moves = 0;
  std::int64_t clears = 0;
  std::int64_t reveals = 0;
  std::int64_t waves = 0;
  double h7_return = 0.0;
  double h12_return = 0.0;
  double h7_lower = 0.0;
  double h12_lower = 0.0;
  double h7_clears = 0.0;
  double h12_clears = 0.0;
  double h7_reveals = 0.0;
  double h12_reveals = 0.0;
  for (const GameResult& game : games) {
    scores += game.score;
    moves += game.moves;
    clears += game.clears;
    reveals += game.reveals;
    waves += game.waves;
    ordered_scores.push_back(game.score);
    ordered_moves.push_back(game.moves);
    result.agreements += game.agreements;
    result.disagreements += game.disagreements;
    result.panel_evaluations += game.panel_evaluations;
    result.switches += game.switches;
    result.natural_terminals += game.natural_terminal;
    result.capped += game.capped;
    result.maximum_chain = std::max(result.maximum_chain, game.maximum_chain);
    result.proposal_work += game.proposal_work;
    result.panel_work += game.panel_work;
    h7_return += game.h7_panel_mean_return_sum;
    h12_return += game.h12_panel_mean_return_sum;
    h7_lower += game.h7_panel_lower_quartile_sum;
    h12_lower += game.h12_panel_lower_quartile_sum;
    h7_clears += game.h7_panel_clears_sum;
    h12_clears += game.h12_panel_clears_sum;
    h7_reveals += game.h7_panel_reveals_sum;
    h12_reveals += game.h12_panel_reveals_sum;
    result.h7_panel_terminals += game.h7_panel_terminals;
    result.h12_panel_terminals += game.h12_panel_terminals;
  }
  std::sort(ordered_scores.begin(), ordered_scores.end());
  std::sort(ordered_moves.begin(), ordered_moves.end());
  const std::size_t quartile =
      std::max<std::size_t>(1, games.size() / 4);
  result.bottom_quartile_score = std::accumulate(
      ordered_scores.begin(), ordered_scores.begin() + quartile, 0.0) /
      quartile;
  result.bottom_quartile_moves = std::accumulate(
      ordered_moves.begin(), ordered_moves.begin() + quartile, 0.0) /
      quartile;
  result.mean_score = static_cast<double>(scores) / games.size();
  result.mean_moves = static_cast<double>(moves) / games.size();
  result.clears_per_move = static_cast<double>(clears) / moves;
  result.reveals_per_move = static_cast<double>(reveals) / moves;
  result.waves_per_move = static_cast<double>(waves) / moves;
  result.agreement_rate = static_cast<double>(result.agreements) / moves;
  result.panel_rate = static_cast<double>(result.panel_evaluations) / moves;
  result.switch_rate_per_move = static_cast<double>(result.switches) / moves;
  if (result.panel_evaluations > 0) {
    const double panels = result.panel_evaluations;
    result.switch_rate_per_panel = result.switches / panels;
    result.mean_h7_panel_return = h7_return / panels;
    result.mean_h12_panel_return = h12_return / panels;
    result.mean_h7_panel_lower_quartile = h7_lower / panels;
    result.mean_h12_panel_lower_quartile = h12_lower / panels;
    result.mean_h7_panel_clears = h7_clears / panels;
    result.mean_h12_panel_clears = h12_clears / panels;
    result.mean_h7_panel_reveals = h7_reveals / panels;
    result.mean_h12_panel_reveals = h12_reveals / panels;
  }
  return result;
}

struct Paired {
  int score_wins = 0;
  int move_wins = 0;
  int joint_wins = 0;
  double mean_score_delta = 0.0;
  double mean_move_delta = 0.0;
};

Paired pair(const std::vector<GameResult>& candidate,
            const std::vector<GameResult>& baseline) {
  if (candidate.size() != baseline.size()) {
    throw std::invalid_argument("paired cohorts differ in size");
  }
  Paired result;
  for (std::size_t index = 0; index < candidate.size(); ++index) {
    if (candidate[index].seed != baseline[index].seed) {
      throw std::runtime_error("paired seed mismatch");
    }
    const bool score_win = candidate[index].score > baseline[index].score;
    const bool move_win = candidate[index].moves > baseline[index].moves;
    result.score_wins += score_win;
    result.move_wins += move_win;
    result.joint_wins += score_win && move_win;
    result.mean_score_delta += candidate[index].score - baseline[index].score;
    result.mean_move_delta += candidate[index].moves - baseline[index].moves;
  }
  result.mean_score_delta /= candidate.size();
  result.mean_move_delta /= candidate.size();
  return result;
}

std::vector<GameResult> evaluate(std::uint32_t seed_start, int games,
                                 Policy policy, int threads,
                                 const Deadline& deadline, bool screen) {
  std::vector<GameResult> result(games);
  std::atomic<int> next{0};
  std::mutex progress;
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(threads, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int index = next.fetch_add(1);
        if (index >= games) return;
        const std::uint32_t seed = seed_start + index;
        result[index] = playGame(seed, policy, deadline, screen);
        const std::lock_guard<std::mutex> lock(progress);
        std::cerr << (policy == Policy::kRiskGate ? "risk-gate" : "H7")
                  << " seed 0x" << std::hex << seed << std::dec << ' '
                  << result[index].score << " (" << result[index].moves
                  << " moves, panels " << result[index].panel_evaluations
                  << ", switches " << result[index].switches << ", work "
                  << result[index].proposal_work + result[index].panel_work
                  << ")\n";
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

struct Options {
  std::string output;
  std::string readme =
      "/tmp/drop7-constructive-h12-risk-gate-README.md";
  std::string qualification;
  std::string source_sha256;
  int threads = kDefaultThreads;
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string argument = argv[index];
    if (argument == "--output") {
      result.output = argv[index + 1];
    } else if (argument == "--readme") {
      result.readme = argv[index + 1];
    } else if (argument == "--qualification") {
      result.qualification = argv[index + 1];
    } else if (argument == "--source-sha256") {
      result.source_sha256 = argv[index + 1];
    } else if (argument == "--threads") {
      result.threads = std::stoi(argv[index + 1]);
      if (result.threads < 1 || result.threads > 8) {
        throw std::invalid_argument("threads must be in [1,8]");
      }
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  if (result.source_sha256.size() != 64) {
    throw std::invalid_argument("exact 64-character source SHA-256 required");
  }
  return result;
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"bottomQuartileScore\":"
         << summary.bottom_quartile_score
         << ",\"bottomQuartileMoves\":" << summary.bottom_quartile_moves
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"wavesPerMove\":" << summary.waves_per_move
         << ",\"agreements\":" << summary.agreements
         << ",\"disagreements\":" << summary.disagreements
         << ",\"panelEvaluations\":" << summary.panel_evaluations
         << ",\"switches\":" << summary.switches
         << ",\"agreementRate\":" << summary.agreement_rate
         << ",\"panelRate\":" << summary.panel_rate
         << ",\"switchRatePerMove\":" << summary.switch_rate_per_move
         << ",\"switchRatePerPanel\":" << summary.switch_rate_per_panel
         << ",\"meanH7PanelReturn\":" << summary.mean_h7_panel_return
         << ",\"meanH12PanelReturn\":" << summary.mean_h12_panel_return
         << ",\"meanH7PanelLowerQuartile\":"
         << summary.mean_h7_panel_lower_quartile
         << ",\"meanH12PanelLowerQuartile\":"
         << summary.mean_h12_panel_lower_quartile
         << ",\"meanH7PanelClears\":" << summary.mean_h7_panel_clears
         << ",\"meanH12PanelClears\":" << summary.mean_h12_panel_clears
         << ",\"meanH7PanelReveals\":" << summary.mean_h7_panel_reveals
         << ",\"meanH12PanelReveals\":" << summary.mean_h12_panel_reveals
         << ",\"H7PanelTerminals\":" << summary.h7_panel_terminals
         << ",\"H12PanelTerminals\":" << summary.h12_panel_terminals
         << ",\"naturalTerminals\":" << summary.natural_terminals
         << ",\"capped\":" << summary.capped
         << ",\"maximumChain\":" << summary.maximum_chain
         << ",\"proposalWork\":" << summary.proposal_work
         << ",\"panelWork\":" << summary.panel_work << '}';
}

void writePaired(std::ostream& output, const Paired& paired) {
  output << "{\"scoreWins\":" << paired.score_wins
         << ",\"moveWins\":" << paired.move_wins
         << ",\"jointWins\":" << paired.joint_wins
         << ",\"meanScoreDelta\":" << paired.mean_score_delta
         << ",\"meanMoveDelta\":" << paired.mean_move_delta << '}';
}

void writeGame(std::ostream& output, const GameResult& game) {
  output << "{\"seed\":\"0x" << std::hex << std::setw(8)
         << std::setfill('0') << game.seed << std::dec << std::setfill(' ')
         << "\",\"score\":" << game.score << ",\"moves\":" << game.moves
         << ",\"clears\":" << game.clears
         << ",\"reveals\":" << game.reveals << ",\"waves\":" << game.waves
         << ",\"maximumChain\":" << game.maximum_chain
         << ",\"naturalTerminal\":"
         << (game.natural_terminal ? "true" : "false")
         << ",\"capped\":" << (game.capped ? "true" : "false")
         << ",\"agreements\":" << game.agreements
         << ",\"disagreements\":" << game.disagreements
         << ",\"panelEvaluations\":" << game.panel_evaluations
         << ",\"switches\":" << game.switches
         << ",\"proposalWork\":" << game.proposal_work
         << ",\"panelWork\":" << game.panel_work
         << ",\"H7PanelTerminals\":" << game.h7_panel_terminals
         << ",\"H12PanelTerminals\":" << game.h12_panel_terminals
         << ",\"discHash\":\"0x" << std::hex << game.disc_hash << std::dec
         << "\"}";
}

bool resultGate(const Summary& candidate, const Summary& baseline,
                const Paired& paired, int joint_wins) {
  return candidate.mean_score >= kRequiredRatio * baseline.mean_score &&
         candidate.mean_moves >= kRequiredRatio * baseline.mean_moves &&
         candidate.clears_per_move + 1.0e-12 >=
             baseline.clears_per_move &&
         candidate.reveals_per_move + 1.0e-12 >=
             baseline.reveals_per_move &&
         paired.joint_wins >= joint_wins;
}

bool qualificationAllowsScreen(const Options& options) {
  if (options.qualification.empty()) return false;
  std::ifstream input(options.qualification);
  if (!input) return false;
  const std::string contents((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
  return contents.find("\"phase\":\"fitting\"") != std::string::npos &&
         contents.find("\"passed\":true") != std::string::npos &&
         contents.find("\"sourceSha256\":\"" + options.source_sha256 +
                       "\"") != std::string::npos;
}

double projectedScreenSeconds(double fitting_seconds) {
  if (fitting_seconds < 0) {
    throw std::invalid_argument("negative fitting time projection");
  }
  return fitting_seconds * kScreenGames / kFittingGames;
}

void writeReadme(const Options& options, bool screen,
                 const Summary& candidate, const Summary& baseline,
                 const Paired& paired, bool passed, double wall_seconds,
                 double projected_seconds) {
  std::ofstream output(options.readme);
  if (!output) throw std::runtime_error("cannot write risk-gate README");
  output << "# Drop7 H12 Pareto risk gate\n\n"
         << "H7 is immutable fallback and H12 is only a proposal. Differing "
            "actions are evaluated on one common independent 21-scenario "
            "public-hash H12 panel. H12 must Pareto-dominate on mean return, "
            "mean lowest-five return, terminal count, and clear/reveal flow. "
            "No coefficient or acceptance threshold is used.\n\n"
         << "- Phase: " << (screen ? "screen" : "fitting") << "\n"
         << "- Seeds: `0x" << std::hex
         << (screen ? kScreenSeedStart : kFittingSeedStart) << "..0x"
         << ((screen ? kScreenSeedEndExclusive : kFittingSeedEndExclusive) -
             1u)
         << std::dec << "`\n"
         << "- Maximum moves: 1000\n"
         << "- Source SHA-256: `" << options.source_sha256 << "`\n"
         << "- Candidate mean score/moves: " << candidate.mean_score << " / "
         << candidate.mean_moves << "\n"
         << "- H7 mean score/moves: " << baseline.mean_score << " / "
         << baseline.mean_moves << "\n"
         << "- Candidate clear/reveal flow: " << candidate.clears_per_move
         << " / " << candidate.reveals_per_move << "\n"
         << "- H7 clear/reveal flow: " << baseline.clears_per_move << " / "
         << baseline.reveals_per_move << "\n"
         << "- Agreements/disagreements/panels/switches: "
         << candidate.agreements << " / " << candidate.disagreements << " / "
         << candidate.panel_evaluations << " / " << candidate.switches << "\n"
         << "- Paired joint wins: " << paired.joint_wins << "\n"
         << "- Natural/censored candidate: " << candidate.natural_terminals
         << " / " << candidate.capped << "\n"
         << "- Natural/censored H7: " << baseline.natural_terminals << " / "
         << baseline.capped << "\n"
         << "- Wall seconds: " << wall_seconds << "\n";
  if (!screen) {
    output << "- Projected screen seconds: " << projected_seconds << "\n";
  }
  output << "- Passed: " << (passed ? "yes" : "no") << "\n\n"
         << "No screen or `0x4d`, `0x7d`, or `0xd7` seed is opened unless "
            "the fitting gate passes.\n";
}

int runPhase(const Options& options, bool screen, std::ostream& output) {
  if (screen && !qualificationAllowsScreen(options)) {
    throw std::invalid_argument(
        "screen requires matching passed fitting qualification");
  }
  const Deadline deadline;
  const std::uint32_t seed_start =
      screen ? kScreenSeedStart : kFittingSeedStart;
  const int games = screen ? kScreenGames : kFittingGames;
  const int joint_wins = screen ? kScreenJointWins : kFitJointWins;
  const auto candidate = evaluate(seed_start, games, Policy::kRiskGate,
                                  options.threads, deadline, screen);
  const auto baseline = evaluate(seed_start, games, Policy::kH7,
                                 options.threads, deadline, screen);
  const Summary candidate_summary = summarize(candidate);
  const Summary baseline_summary = summarize(baseline);
  const Paired paired = pair(candidate, baseline);
  const double projected_seconds =
      screen ? deadline.seconds() : projectedScreenSeconds(deadline.seconds());
  const bool results = resultGate(candidate_summary, baseline_summary, paired,
                                  joint_wins);
  const bool resources = deadline.seconds() <= kWallLimitSeconds &&
                         peakRssBytes() <= kRssLimitBytes &&
                         (screen || projected_seconds <= kWallLimitSeconds);
  const bool passed = results && resources;
  const std::string output_path =
      options.output.empty()
          ? (screen ? "/tmp/drop7-constructive-h12-risk-gate-screen.json"
                    : "/tmp/drop7-constructive-h12-risk-gate-fit.json")
          : options.output;
  std::ofstream artifact(output_path);
  if (!artifact) throw std::runtime_error("cannot write risk-gate artifact");
  artifact << std::fixed << std::setprecision(9)
           << "{\n  \"format\":\"drop7-constructive-h12-risk-gate-v1\","
           << "\n  \"phase\":\"" << (screen ? "screen" : "fitting")
           << "\",\n  \"sourceSha256\":\"" << options.source_sha256
           << "\",\n  \"publicOnly\":true,\n  \"causal\":true,"
           << "\n  \"fallback\":\"H7\",\n  \"proposal\":\"H12\","
           << "\n  \"panel\":{\"scenarios\":21,\"horizon\":\"H12\","
              "\"commonRandomNumbers\":true,"
              "\"masterDomain\":\"0x48313250\","
              "\"revealDomain\":\"0x5052564c\","
              "\"visibleDomain\":\"0x50564953\","
              "\"lowerQuartileCount\":5},"
           << "\n  \"paretoDimensions\":[\"meanReturn\","
              "\"lowerQuartileReturn\",\"terminalCountNoMore\","
              "\"clearsPerMove\",\"revealsPerMove\"],"
           << "\n  \"seedBank\":{\"start\":\"0x" << std::hex
           << seed_start << "\",\"endExclusive\":\"0x"
           << seed_start + games << std::dec << "\",\"games\":" << games
           << ",\"maximumMoves\":1000},\n  \"candidate\":";
  writeSummary(artifact, candidate_summary);
  artifact << ",\n  \"H7\":";
  writeSummary(artifact, baseline_summary);
  artifact << ",\n  \"paired\":";
  writePaired(artifact, paired);
  artifact << ",\n  \"gate\":{\"scoreRatio\":1.10,\"moveRatio\":1.10,"
              "\"clearNonregression\":true,"
              "\"revealNonregression\":true,\"jointWins\":"
           << joint_wins << "},\n  \"resultGate\":"
           << (results ? "true" : "false")
           << ",\n  \"resourceGate\":" << (resources ? "true" : "false")
           << ",\n  \"projectedScreenSeconds\":" << projected_seconds
           << ",\n  \"passed\":" << (passed ? "true" : "false")
           << ",\n  \"wallSeconds\":" << deadline.seconds()
           << ",\n  \"peakRssBytes\":" << peakRssBytes()
           << ",\n  \"candidateGames\":[";
  for (std::size_t index = 0; index < candidate.size(); ++index) {
    if (index) artifact << ',';
    writeGame(artifact, candidate[index]);
  }
  artifact << "],\n  \"H7Games\":[";
  for (std::size_t index = 0; index < baseline.size(); ++index) {
    if (index) artifact << ',';
    writeGame(artifact, baseline[index]);
  }
  artifact << "]\n}\n";
  writeReadme(options, screen, candidate_summary, baseline_summary, paired,
              passed, deadline.seconds(), projected_seconds);
  output << std::fixed << std::setprecision(3)
         << "CONSTRUCTIVE_H12_RISK_GATE_"
         << (screen ? "SCREEN" : "FIT") << " {\"candidateScore\":"
         << candidate_summary.mean_score << ",\"candidateMoves\":"
         << candidate_summary.mean_moves << ",\"candidateClears\":"
         << candidate_summary.clears_per_move
         << ",\"candidateReveals\":"
         << candidate_summary.reveals_per_move << ",\"H7Score\":"
         << baseline_summary.mean_score << ",\"H7Moves\":"
         << baseline_summary.mean_moves << ",\"H7Clears\":"
         << baseline_summary.clears_per_move << ",\"H7Reveals\":"
         << baseline_summary.reveals_per_move << ",\"panels\":"
         << candidate_summary.panel_evaluations << ",\"switches\":"
         << candidate_summary.switches << ",\"jointWins\":"
         << paired.joint_wins << ",\"projectedScreenSeconds\":"
         << projected_seconds << ",\"passed\":"
         << (passed ? "true" : "false") << ",\"wallSeconds\":"
         << deadline.seconds() << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"artifact\":\"" << output_path << "\"}\n";
  return passed ? EXIT_SUCCESS : 2;
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

template <typename Function>
bool throwsInvalid(Function&& function) {
  try {
    function();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

PublicState asymmetricFixture() {
  PublicState fixture;
  fixture.board.fill(kEmpty);
  fixture.board[indexOf(6, 0)] = kSolid;
  fixture.board[indexOf(5, 0)] = 6;
  fixture.board[indexOf(6, 1)] = kCracked;
  fixture.board[indexOf(6, 2)] = 5;
  fixture.board[indexOf(5, 2)] = 4;
  fixture.board[indexOf(6, 3)] = kSolid;
  fixture.board[indexOf(6, 4)] = 7;
  fixture.next_disc = 3;
  fixture.moves_remaining = 4;
  return fixture;
}

bool selfTest(std::ostream& output) {
  expect(kLevelBonus == 17'000 && kPanelScenarios == 21 &&
             kLowerQuartileScenarios == 5 &&
             frozen::kChanceSamples == 7 &&
             frozen::kTacticalNearTie == 2'500.0,
         "frozen risk-gate constants changed");
  PublicState no_switch;
  no_switch.board = initialBoard();
  no_switch.next_disc = 4;
  no_switch.moves_remaining = 5;
  const ProposalDecision exact_h7 = frozen::chooseAction(no_switch);
  const RiskDecision agreement = chooseRisk(no_switch);
  expect(agreement.proposals_agree && !agreement.panel_evaluated &&
             !agreement.switched && agreement.action == exact_h7.action &&
             agreement.selected == exact_h7 && agreement.panel_work == 0,
         "exact no-switch H7 parity failed");

  const PublicState fixture = asymmetricFixture();
  const std::uint32_t hash = publicPanelHash(fixture);
  expect(hash == publicPanelHash(frozen::mirror(fixture)),
         "public panel hash failed reflection");
  std::array<int, kBoardSize + 1> visible_counts{};
  std::array<int, kBoardSize + 1> reveal_counts{};
  for (int scenario = 0; scenario < kPanelScenarios; ++scenario) {
    ++visible_counts[panelVisibleDisc(hash, scenario, 0)];
    PanelRandom random{hash, scenario, 0, 0};
    ++reveal_counts[random.nextDisc()];
  }
  for (int disc = 1; disc <= kBoardSize; ++disc) {
    expect(visible_counts[disc] == 3 && reveal_counts[disc] == 3,
           "21-stratum panel domain balance failed");
  }
  const int panel_action = centerFirstMove(fixture.board);
  const PanelStats panel = evaluatePanelAction(fixture, panel_action);
  const PanelStats panel_repeat = evaluatePanelAction(fixture, panel_action);
  const PanelStats panel_reflected = evaluatePanelAction(
      frozen::mirror(fixture), kBoardSize - 1 - panel_action);
  expect(panel == panel_repeat && panel == panel_reflected &&
             panel.work > 0,
         "panel determinism/reflection/common-domain failed");

  PanelStats base;
  base.mean_return = 1.0;
  base.lower_quartile_return = 1.0;
  base.clears_per_move = 1.0;
  base.reveals_per_move = 1.0;
  PanelStats better = base;
  better.mean_return = 2.0;
  expect(!paretoDominates(base, base) && paretoDominates(better, base),
         "strict Pareto acceptance failed");
  better = base;
  better.terminals = 1;
  better.mean_return = 2.0;
  expect(!paretoDominates(better, base),
         "terminal nonincrease Pareto guard failed");

  const RiskDecision first = chooseRisk(fixture);
  const RiskDecision repeat = chooseRisk(fixture);
  const RiskDecision reflected = chooseRisk(frozen::mirror(fixture));
  expect(first == repeat && isLegal(fixture.board, first.action),
         "risk policy determinism/legality failed");
  expect(reflected.action == kBoardSize - 1 - first.action &&
             reflected.h7_action == kBoardSize - 1 - first.h7_action &&
             reflected.h12_action == kBoardSize - 1 - first.h12_action &&
             reflected.proposals_agree == first.proposals_agree &&
             reflected.panel_evaluated == first.panel_evaluated &&
             reflected.switched == first.switched &&
             reflected.proposal_work == first.proposal_work &&
             reflected.panel_work == first.panel_work &&
             reflected.h7_panel == first.h7_panel &&
             reflected.h12_panel == first.h12_panel,
         "risk policy reflection failed");
  State metadata = frozen::materialize(fixture);
  metadata.score = 9'999'999;
  metadata.level = 777;
  metadata.moves_played = 888;
  expect(frozen::publicState(metadata) == fixture &&
             chooseRisk(frozen::publicState(metadata)) == first,
         "risk policy used hidden metadata");
  PublicState terminal = fixture;
  terminal.terminal = true;
  expect(chooseRisk(terminal).action == -1,
         "terminal risk policy selected an action");
  expect(projectedScreenSeconds(10.0) == 20.0 &&
             throwsInvalid([] { (void)projectedScreenSeconds(-1.0); }),
         "risk-gate projection guard failed");
  expect(allowedFittingSeed(kFittingSeedStart) &&
             allowedFittingSeed(kFittingSeedEndExclusive - 1u) &&
             !allowedFittingSeed(kFittingSeedStart - 1u) &&
             !allowedFittingSeed(kFittingSeedEndExclusive) &&
             allowedScreenSeed(kScreenSeedStart) &&
             allowedScreenSeed(kScreenSeedEndExclusive - 1u) &&
             !allowedScreenSeed(kScreenSeedStart - 1u) &&
             !allowedScreenSeed(kScreenSeedEndExclusive) &&
             throwsInvalid([] { requireSeed(0x4d6a'6000u, false); }) &&
             throwsInvalid([] { requireSeed(0x7d6a'6000u, false); }) &&
             throwsInvalid([] { requireSeed(0xd76a'6000u, false); }) &&
             throwsInvalid([] { requireSeed(0x4d6a'7000u, true); }) &&
             throwsInvalid([] { requireSeed(0x7d6a'7000u, true); }) &&
             throwsInvalid([] { requireSeed(0xd76a'7000u, true); }),
         "risk-gate seed guards failed");
  enforceRssLimit();
  output << "CONSTRUCTIVE_H12_RISK_GATE_SELF_TEST {\"passed\":true,"
         << "\"publicOnly\":true,\"metadataBlind\":true,"
         << "\"deterministic\":true,\"reflection\":true,"
         << "\"legal\":true,\"exactNoSwitchH7Parity\":true,"
         << "\"panelScenarios\":21,\"balancedChanceDomains\":true,"
         << "\"paretoOnly\":true,\"projectionGuard\":true,"
         << "\"seedGuards\":true,\"fixtureAgreement\":"
         << (first.proposals_agree ? "true" : "false")
         << ",\"fixturePanelWork\":" << first.panel_work
         << ",\"peakRssBytes\":" << peakRssBytes() << "}\n";
  return true;
}

}  // namespace drop7::constructive_h12_risk_gate

int main(int argc, char** argv) {
  try {
    using namespace drop7::constructive_h12_risk_gate;
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return selfTest(std::cout) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--fit") {
      return runPhase(parseOptions(argc, argv, 2), false, std::cout);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--screen") {
      return runPhase(parseOptions(argc, argv, 2), true, std::cout);
    }
    std::cerr << "usage: drop7_constructive_h12_risk_gate --self-test | "
                 "--fit --source-sha256 HASH [--output PATH] [--readme PATH] "
                 "[--threads N] | --screen --source-sha256 HASH "
                 "--qualification FIT_JSON [--output PATH] [--readme PATH] "
                 "[--threads N]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_constructive_h12_risk_gate: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
