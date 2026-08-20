// Exploratory retest: the 25-move / seven-scenario / completed-fair-D2 rollout
// veto over fair D4, ported to corrected 17,000-point Hardcore scoring.
//
// PROVENANCE.  Every policy-relevant routine below is a transcription of
// approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto.cpp.  That file is
// NOT modified and NOT included: it cannot compile today because it asserts
// kLevelBonus == 7'000 while the engine now defines 17'000.  The complete list
// of intended differences is in the header comment of build.sh and in
// docs/exploratory/finding-03-rollout-veto-17k.md.  Nothing else was changed.
//
// POLICY (unchanged from the original):
//   * fair D4 (full-width, completed, five stratified chance samples) decides
//     every move by default;
//   * a decision is ROUTED only when maxColumnHeight >= 4 ("danger");
//   * at a routed decision every legal root action is played forward for
//     `horizon` synthetic moves under `kScenarios` = 7 common, exactly
//     stratified random scenarios; after the first (candidate) move every
//     continuation move is a fresh completed full-width fair D2 search that
//     receives only the public observable state;
//   * an alternative may VETO the D4 action only if it jointly satisfies
//       survivors >= D4 survivors,
//       mean numbered clears >= D4 mean numbered clears,
//       paired one-sided t lower bound on the scenario return > 0, and
//       D4 root-Q loss <= --root-q-loss;
//   * among passing alternatives the one with the largest return lower bound is
//     taken.
//
// Cohort role: exploratory development diagnostic on lease SEEDLEASE-A51D-VETO
// (0xa51e0000..0xa51e3fff).  No protected or final seed is touched.

#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#include "fair-only-depth4.cpp"

#include "../common/harness.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace drop7::rollout_veto_17k {

namespace d4 = drop7::fair_only_depth4;
namespace fair = drop7::fair_only_horizon;
namespace lifetime = drop7::lifetime;

// ---------------------------------------------------------------------------
// Frozen structural constants (identical to the original source).
// ---------------------------------------------------------------------------
constexpr int kMaximumRolloutHorizon = 25;
constexpr int kScenarios = 7;
constexpr int kContinuationDepth = 2;
constexpr int kDangerHeight = 4;
constexpr int kEventsPerStep = 64;
constexpr double kPairedT975Df6 = 2.446912;  // t(0.975, df = kScenarios - 1)
constexpr std::uint32_t kTapeSeedDomain = 0x4432'5254u;    // "D2RT"
constexpr std::uint32_t kRevealTapeDomain = 0x4432'5256u;  // "D2RV"
constexpr std::uint32_t kVisibleTapeDomain = 0x4432'5653u;  // "D2VS"

// Seed lease for this experiment.
constexpr std::uint32_t kLeaseFirst = 0xa51e'0000u;
constexpr std::uint32_t kLeaseLast = 0xa51e'3fffu;

constexpr std::uint64_t power(std::uint64_t base, int exponent) {
  std::uint64_t result = 1;
  for (int count = 0; count < exponent; ++count) result *= base;
  return result;
}

// A full D2 call has 35 root transitions, at most 1,225 second-ply
// transitions, and at most 1,225 leaf evaluations.  Only the 35 depth-one
// public states can enter its fresh transposition cache.
constexpr std::uint64_t kWorstD2Work =
    kBoardSize * fair::kChanceSamples +
    2u * power(kBoardSize * fair::kChanceSamples, 2);
constexpr std::uint64_t kWorstD2CacheEntries = kBoardSize * fair::kChanceSamples;

static_assert(kWorstD2Work == 2'485);
static_assert(kWorstD2CacheEntries == 35);
static_assert(d4::kCandidateDepth == 4);
static_assert(fair::kChanceSamples == 5);
static_assert(fair::kTerminalUtility == -1'000'000.0);
// PORT CHANGE 1 of 2 to the policy code: the original asserted 7'000 here and
// therefore cannot be compiled against the corrected engine.
static_assert(kLevelBonus == 17'000);
static_assert(kClearBonus == 70'000);
static_assert(kScenarios == kBoardSize);
static_assert(kEventsPerStep > kCellCount);
static_assert((kLeaseFirst >> 24) != 0x7du && (kLeaseFirst >> 24) != 0xd7u);

// ---------------------------------------------------------------------------
// Runtime configuration (the original hard-coded every one of these).
// ---------------------------------------------------------------------------
struct Config {
  std::uint32_t seedStart = kLeaseFirst;
  int games = 8;
  int threads = 8;
  int maximumMoves = 2000;
  int horizon = kMaximumRolloutHorizon;
  // PORT CHANGE 2 of 2 to the policy code: the original wrote
  //   constexpr double kMaximumRootQLoss = static_cast<double>(kLevelBonus);
  // i.e. "one canonical level bonus", which evaluated to 7,000 at the time.
  // Under corrected scoring the intended band is 17,000.  Exposed as an option
  // so both the original and the corrected width can be measured.
  double maximumRootQLoss = static_cast<double>(kLevelBonus);
  std::string output = "veto-cohort.json";
  bool baselineOnly = false;
  bool candidateOnly = false;
  bool quiet = false;
};

std::mutex progressMutex;

// ---------------------------------------------------------------------------
// Public observable projection (verbatim from the original).
// ---------------------------------------------------------------------------
struct ObservableState {
  Board board{};
  std::uint8_t next_disc = 1;
  int moves_remaining = kMovesPerLevel;
  bool game_over = false;

  bool operator==(const ObservableState&) const = default;
};

ObservableState observable(const State& source) {
  return ObservableState{source.board, source.next_disc,
                         source.moves_remaining, source.game_over};
}

State materialize(const ObservableState& source) {
  State result;
  result.board = source.board;
  result.next_disc = source.next_disc;
  result.score = 0;
  result.level = 1;
  result.moves_remaining = source.moves_remaining;
  result.moves_played = 0;
  result.game_over = source.game_over;
  return result;
}

std::uint64_t mix64(std::uint64_t value) {
  value ^= value >> 30u;
  value *= 0xbf58'476d'1ce4'e5b9ull;
  value ^= value >> 27u;
  value *= 0x94d0'49bb'1331'11ebull;
  return value ^ (value >> 31u);
}

std::uint64_t publicHash(const ObservableState& source) {
  bool ignored = false;
  const State canonical =
      cfpi::detail::canonicalState(materialize(source), ignored);
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const std::uint8_t cell : canonical.board) {
    hash ^= static_cast<std::uint64_t>(cell + 1u);
    hash *= 0x0000'0100'0000'01b3ull;
  }
  hash ^= canonical.next_disc;
  hash *= 0x0000'0100'0000'01b3ull;
  hash ^= static_cast<std::uint64_t>(canonical.moves_remaining + 1);
  hash *= 0x0000'0100'0000'01b3ull;
  hash ^= static_cast<std::uint64_t>(canonical.game_over);
  return mix64(hash);
}

std::uint32_t seed32(std::uint64_t value) {
  return mix32(static_cast<std::uint32_t>(value) ^
               static_cast<std::uint32_t>(value >> 32u));
}

int maximumHeight(const Board& board) {
  const std::array<int, kBoardSize> heights = cfpi::detail::columnHeights(board);
  return *std::max_element(heights.begin(), heights.end());
}

bool isDanger(const ObservableState& state) {
  return maximumHeight(state.board) >= kDangerHeight;
}

// ---------------------------------------------------------------------------
// Completed full-width fair D2 continuation (verbatim from the original).
// ---------------------------------------------------------------------------
struct D2Metrics {
  std::uint64_t calls = 0;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::size_t peak_cache_entries = 0;
  std::uint64_t root_actions = 0;
  bool full_root = true;
};

int fairDepthTwoAction(const ObservableState& source,
                       D2Metrics* aggregate = nullptr) {
  if (source.game_over) return -1;
  bool mirrored = false;
  const State canonical =
      cfpi::detail::canonicalState(materialize(source), mirrored);
  fair::SearchContext context;
  const fair::RootEvaluation root =
      fair::rootDecision(canonical, kContinuationDepth, context);
  int legal_count = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    legal_count += isLegal(canonical.board, column);
  }
  int evaluated_count = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    evaluated_count += std::isfinite(root.values[column]);
  }
  if (root.action < 0 || evaluated_count != legal_count ||
      context.work > kWorstD2Work ||
      context.cache.size() > kWorstD2CacheEntries) {
    throw std::runtime_error("fair D2 failed full-width resource proof");
  }
  if (aggregate != nullptr) {
    ++aggregate->calls;
    aggregate->work += context.work;
    aggregate->nodes += context.nodes;
    aggregate->cache_hits += context.cache_hits;
    aggregate->peak_cache_entries =
        std::max(aggregate->peak_cache_entries, context.cache.size());
    aggregate->root_actions += static_cast<std::uint64_t>(evaluated_count);
    aggregate->full_root =
        aggregate->full_root && evaluated_count == legal_count;
  }
  return mirrored ? kBoardSize - 1 - root.action : root.action;
}

using ContinuationFunction = int (*)(const ObservableState&, D2Metrics*);
static_assert(
    std::is_same_v<decltype(&fairDepthTwoAction), ContinuationFunction>);

// ---------------------------------------------------------------------------
// Aligned synthetic tapes (verbatim from the original).
// ---------------------------------------------------------------------------
struct TapeDomains {
  std::uint32_t reveal = kRevealTapeDomain;
  std::uint32_t visible = kVisibleTapeDomain;
};

struct RevealTape {
  std::uint32_t root_seed = 0;
  int scenario = 0;
  int step = 0;
  std::uint32_t domain = kRevealTapeDomain;
  int event = 0;

  std::uint8_t nextDisc() {
    const int event_index = step * kEventsPerStep + event++;
    const double unit = cfpi::detail::stratifiedUnit(
        root_seed, scenario, kScenarios, domain, event_index);
    return static_cast<std::uint8_t>(
        std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
  }
};

std::uint8_t visibleDisc(std::uint32_t root_seed, int scenario, int step,
                         std::uint32_t domain = kVisibleTapeDomain) {
  const double unit = cfpi::detail::stratifiedUnit(root_seed, scenario,
                                                   kScenarios, domain, step);
  return static_cast<std::uint8_t>(
      std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
}

bool playSyntheticMove(const ObservableState& source, int action,
                       std::uint32_t root_seed, int scenario, int step,
                       MoveResult& result,
                       std::uint64_t* transitions = nullptr,
                       TapeDomains domains = {}) {
  if (source.game_over || !isLegal(source.board, action)) return false;
  Board board = source.board;
  if (!placeDisc(board, action, source.next_disc)) return false;

  RevealTape reveals{root_seed, scenario, step, domains.reveal, 0};
  result = MoveResult{};
  std::int64_t first_score = 0;
  cfpi::detail::resolveCascadeSampled(board, reveals, 1, first_score,
                                      result.waves);
  result.score_delta = first_score;
  result.cleared_board = isBoardEmpty(board);
  if (result.cleared_board) result.score_delta += kClearBonus;

  int moves_remaining = source.moves_remaining - 1;
  bool game_over = false;
  if (moves_remaining == 0) {
    Board raised{};
    if (!raiseCoveredRow(board, raised)) {
      game_over = true;
    } else {
      result.level_advanced = true;
      moves_remaining = kMovesPerLevel;
      result.score_delta += kLevelBonus;
      board = raised;
      std::int64_t level_score = 0;
      const int next_depth =
          result.waves.empty() ? 1 : result.waves.back().depth + 1;
      cfpi::detail::resolveCascadeSampled(board, reveals, next_depth,
                                          level_score, result.waves);
      result.score_delta += level_score;
      if (isBoardEmpty(board)) {
        result.score_delta += kClearBonus;
        result.cleared_board = true;
      }
    }
  }
  int legal_count = 0;
  legalColumns(board, legal_count);
  if (!game_over && legal_count == 0) game_over = true;

  result.state.board = board;
  result.state.next_disc =
      game_over ? source.next_disc
                : visibleDisc(root_seed, scenario, step, domains.visible);
  result.state.score = 0;
  result.state.level = 1;
  result.state.moves_remaining = moves_remaining;
  result.state.moves_played = 0;
  result.state.game_over = game_over;
  if (transitions != nullptr) ++*transitions;
  return true;
}

// ---------------------------------------------------------------------------
// Rollout evaluation (verbatim from the original except that `horizon` is a
// runtime value and the worst-case bounds are therefore computed from it).
// ---------------------------------------------------------------------------
struct ScenarioOutcome {
  double value = 0.0;
  int numbered_clears = 0;
  bool survived_horizon = false;

  bool operator==(const ScenarioOutcome&) const = default;
};

struct ActionRollout {
  std::array<ScenarioOutcome, kScenarios> scenarios{};
  double mean_value = 0.0;
  double mean_numbered_clears = 0.0;
  int surviving_scenarios = 0;

  bool operator==(const ActionRollout&) const = default;
};

struct RolloutEvaluation {
  std::array<ActionRollout, kBoardSize> actions{};
  std::array<bool, kBoardSize> legal{};
  int legal_actions = 0;
  std::uint64_t synthetic_transitions = 0;
  D2Metrics continuation{};
  bool full_root = true;
};

RolloutEvaluation evaluateRollouts(const ObservableState& source, int horizon) {
  if (source.game_over || horizon < 1 || horizon > kMaximumRolloutHorizon) {
    throw std::invalid_argument("invalid D2 rollout root or horizon");
  }
  const std::uint64_t worstCalls = static_cast<std::uint64_t>(kBoardSize) *
                                   kScenarios *
                                   static_cast<std::uint64_t>(horizon - 1);
  const std::uint64_t worstTransitions = static_cast<std::uint64_t>(kBoardSize) *
                                         kScenarios *
                                         static_cast<std::uint64_t>(horizon);
  const std::uint64_t worstWork = worstCalls * kWorstD2Work;

  RolloutEvaluation result;
  bool mirrored = false;
  const State canonical_state =
      cfpi::detail::canonicalState(materialize(source), mirrored);
  const ObservableState root = observable(canonical_state);
  const std::uint32_t tape_seed =
      seed32(publicHash(root) ^ static_cast<std::uint64_t>(kTapeSeedDomain));
  for (const int action : cfpi::detail::kColumnOrder) {
    if (!isLegal(root.board, action)) continue;
    result.legal[action] = true;
    ++result.legal_actions;
    ActionRollout& action_result = result.actions[action];
    for (int scenario = 0; scenario < kScenarios; ++scenario) {
      ObservableState state = root;
      ScenarioOutcome& outcome = action_result.scenarios[scenario];
      for (int step = 0; step < horizon; ++step) {
        const int selected =
            step == 0 ? action : fairDepthTwoAction(state, &result.continuation);
        if (!isLegal(state.board, selected)) {
          outcome.value += fair::kTerminalUtility;
          state.game_over = true;
          break;
        }
        MoveResult move;
        if (!playSyntheticMove(state, selected, tape_seed, scenario, step, move,
                               &result.synthetic_transitions)) {
          outcome.value += fair::kTerminalUtility;
          state.game_over = true;
          break;
        }
        outcome.value += static_cast<double>(move.score_delta);
        for (const Wave& wave : move.waves) {
          outcome.numbered_clears += wave.cleared;
        }
        state = observable(move.state);
        if (state.game_over) {
          outcome.value += fair::kTerminalUtility;
          break;
        }
      }
      if (!state.game_over) {
        outcome.survived_horizon = true;
        outcome.value += fair::fairLeaf(materialize(state));
      }
      action_result.mean_value += outcome.value / kScenarios;
      action_result.mean_numbered_clears +=
          static_cast<double>(outcome.numbered_clears) / kScenarios;
      action_result.surviving_scenarios += outcome.survived_horizon;
    }
  }
  result.full_root = result.legal_actions > 0 && result.continuation.full_root;
  if (result.synthetic_transitions > worstTransitions ||
      result.continuation.calls > worstCalls ||
      result.continuation.work > worstWork ||
      result.continuation.peak_cache_entries > kWorstD2CacheEntries ||
      !result.full_root) {
    throw std::runtime_error("D2 rollout exceeded fixed resource/full-root bound");
  }
  if (!mirrored) return result;

  RolloutEvaluation reflected = result;
  for (int column = 0; column < kBoardSize; ++column) {
    reflected.actions[kBoardSize - 1 - column] = result.actions[column];
    reflected.legal[kBoardSize - 1 - column] = result.legal[column];
  }
  return reflected;
}

double pairedReturnLower95(const ActionRollout& candidate,
                           const ActionRollout& baseline) {
  std::array<double, kScenarios> differences{};
  double mean = 0.0;
  for (int scenario = 0; scenario < kScenarios; ++scenario) {
    differences[scenario] =
        candidate.scenarios[scenario].value - baseline.scenarios[scenario].value;
    mean += differences[scenario] / kScenarios;
  }
  double squares = 0.0;
  for (const double value : differences) {
    squares += (value - mean) * (value - mean);
  }
  const double deviation =
      std::sqrt(squares / static_cast<double>(kScenarios - 1));
  return mean - kPairedT975Df6 * deviation / std::sqrt(kScenarios);
}

struct AlternativeTest {
  double return_lower95 = -std::numeric_limits<double>::infinity();
  double clear_advantage = -std::numeric_limits<double>::infinity();
  double root_q_loss = std::numeric_limits<double>::infinity();
  int survivor_advantage = std::numeric_limits<int>::min();
  bool survivors_ok = false;
  bool clears_ok = false;
  bool return_ok = false;
  bool root_q_ok = false;
  bool passed = false;
};

AlternativeTest testAlternative(const ActionRollout& candidate,
                                const ActionRollout& baseline,
                                double candidate_root_q, double baseline_root_q,
                                double maximumRootQLoss) {
  AlternativeTest result;
  result.return_lower95 = pairedReturnLower95(candidate, baseline);
  result.clear_advantage =
      candidate.mean_numbered_clears - baseline.mean_numbered_clears;
  result.root_q_loss = baseline_root_q - candidate_root_q;
  result.survivor_advantage =
      candidate.surviving_scenarios - baseline.surviving_scenarios;
  result.survivors_ok = result.survivor_advantage >= 0;
  result.clears_ok = result.clear_advantage >= 0.0;
  result.return_ok = result.return_lower95 > 0.0;
  result.root_q_ok = result.root_q_loss <= maximumRootQLoss + 1.0e-9;
  result.passed = result.survivors_ok && result.clears_ok && result.return_ok &&
                  result.root_q_ok;
  return result;
}

struct Decision {
  int action = -1;
  int d4_action = -1;
  bool danger = false;
  bool routed = false;
  bool switched = false;
  int alternatives = 0;
  int passing_alternatives = 0;
  int survivor_rejections = 0;
  int clear_rejections = 0;
  int return_rejections = 0;
  int root_q_rejections = 0;
  double selected_return_lower95 = 0.0;
  double selected_clear_advantage = 0.0;
  double selected_root_q_loss = 0.0;
  int selected_survivor_advantage = 0;
  double d4_seconds = 0.0;
  double rollout_seconds = 0.0;
  RolloutEvaluation rollout{};
  d4::SearchDecision search{};
};

Decision chooseAction(const State& source, bool rollout_enabled, int horizon,
                      double maximumRootQLoss) {
  Decision result;
  const auto d4_started = std::chrono::steady_clock::now();
  result.search = d4::chooseDepth4Action(source);
  result.d4_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - d4_started)
          .count();
  result.action = result.search.action;
  result.d4_action = result.search.action;
  if (!result.search.complete ||
      result.search.completed_depth != d4::kCandidateDepth ||
      !isLegal(source.board, result.action)) {
    throw std::runtime_error("qualified fair D4 failed to complete");
  }
  result.danger = isDanger(observable(source));
  result.routed = rollout_enabled && result.danger;
  if (!result.routed) return result;

  const auto rollout_started = std::chrono::steady_clock::now();
  result.rollout = evaluateRollouts(observable(source), horizon);
  result.rollout_seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - rollout_started)
                               .count();
  if (!result.rollout.legal[result.d4_action]) {
    throw std::runtime_error("D4 action missing from full rollout root");
  }
  const ActionRollout& baseline = result.rollout.actions[result.d4_action];
  const double baseline_q = result.search.root_values[result.d4_action];
  int selected = result.d4_action;
  double best_lower = 0.0;
  AlternativeTest selected_test;
  for (const int action : cfpi::detail::kColumnOrder) {
    if (!result.rollout.legal[action] || action == result.d4_action) continue;
    ++result.alternatives;
    const AlternativeTest test =
        testAlternative(result.rollout.actions[action], baseline,
                        result.search.root_values[action], baseline_q,
                        maximumRootQLoss);
    result.survivor_rejections += !test.survivors_ok;
    result.clear_rejections += !test.clears_ok;
    result.return_rejections += !test.return_ok;
    result.root_q_rejections += !test.root_q_ok;
    if (!test.passed) continue;
    ++result.passing_alternatives;
    if (test.return_lower95 > best_lower) {
      best_lower = test.return_lower95;
      selected = action;
      selected_test = test;
    }
  }
  result.action = selected;
  result.switched = selected != result.d4_action;
  if (result.switched) {
    result.selected_return_lower95 = selected_test.return_lower95;
    result.selected_clear_advantage = selected_test.clear_advantage;
    result.selected_root_q_loss = selected_test.root_q_loss;
    result.selected_survivor_advantage = selected_test.survivor_advantage;
  }
  if (!isLegal(source.board, result.action)) {
    throw std::runtime_error("D2 rollout veto selected illegal action");
  }
  return result;
}

// ---------------------------------------------------------------------------
// Deciders for the shared harness.
// ---------------------------------------------------------------------------
struct VetoCounters {
  std::uint64_t decisions = 0;
  std::uint64_t danger_decisions = 0;
  std::uint64_t routed_decisions = 0;   // veto opportunities
  std::uint64_t alternatives = 0;
  std::uint64_t switches = 0;           // vetoes taken
  std::uint64_t passing_alternatives = 0;
  std::uint64_t survivor_rejections = 0;
  std::uint64_t clear_rejections = 0;
  std::uint64_t return_rejections = 0;
  std::uint64_t root_q_rejections = 0;
  std::uint64_t d2_calls = 0;
  std::uint64_t d2_work = 0;
  std::uint64_t d4_work = 0;
  std::uint64_t synthetic_transitions = 0;
  double d4_seconds = 0.0;
  double rollout_seconds = 0.0;

  VetoCounters operator-(const VetoCounters& other) const {
    VetoCounters r;
    r.decisions = decisions - other.decisions;
    r.danger_decisions = danger_decisions - other.danger_decisions;
    r.routed_decisions = routed_decisions - other.routed_decisions;
    r.alternatives = alternatives - other.alternatives;
    r.switches = switches - other.switches;
    r.passing_alternatives = passing_alternatives - other.passing_alternatives;
    r.survivor_rejections = survivor_rejections - other.survivor_rejections;
    r.clear_rejections = clear_rejections - other.clear_rejections;
    r.return_rejections = return_rejections - other.return_rejections;
    r.root_q_rejections = root_q_rejections - other.root_q_rejections;
    r.d2_calls = d2_calls - other.d2_calls;
    r.d2_work = d2_work - other.d2_work;
    r.d4_work = d4_work - other.d4_work;
    r.synthetic_transitions = synthetic_transitions - other.synthetic_transitions;
    r.d4_seconds = d4_seconds - other.d4_seconds;
    r.rollout_seconds = rollout_seconds - other.rollout_seconds;
    return r;
  }
};

struct VetoDecider {
  int horizon = kMaximumRolloutHorizon;
  double maximumRootQLoss = static_cast<double>(kLevelBonus);
  bool rollout_enabled = true;
  VetoCounters counters;

  int operator()(const State& state, std::uint64_t& work) {
    const Decision decision =
        chooseAction(state, rollout_enabled, horizon, maximumRootQLoss);
    ++counters.decisions;
    counters.danger_decisions += decision.danger;
    counters.routed_decisions += decision.routed;
    counters.alternatives += static_cast<std::uint64_t>(decision.alternatives);
    counters.switches += decision.switched;
    counters.passing_alternatives +=
        static_cast<std::uint64_t>(decision.passing_alternatives);
    counters.survivor_rejections +=
        static_cast<std::uint64_t>(decision.survivor_rejections);
    counters.clear_rejections +=
        static_cast<std::uint64_t>(decision.clear_rejections);
    counters.return_rejections +=
        static_cast<std::uint64_t>(decision.return_rejections);
    counters.root_q_rejections +=
        static_cast<std::uint64_t>(decision.root_q_rejections);
    counters.d2_calls += decision.rollout.continuation.calls;
    counters.d2_work += decision.rollout.continuation.work;
    counters.d4_work += decision.search.work;
    counters.synthetic_transitions += decision.rollout.synthetic_transitions;
    counters.d4_seconds += decision.d4_seconds;
    counters.rollout_seconds += decision.rollout_seconds;
    work += decision.search.work + decision.rollout.continuation.work;
    return decision.action;
  }
};

// ---------------------------------------------------------------------------
// Cohort runner.  Uses drop7::lifetime::runGame and the shared artifact writer
// verbatim; only the per-game veto counters are captured alongside.
// ---------------------------------------------------------------------------
struct ArmResult {
  std::vector<lifetime::GameRecord> records;
  std::vector<VetoCounters> counters;
  double wallSeconds = 0.0;
  int failures = 0;
};

ArmResult runArm(const Config& config, bool candidate,
                 const std::string& label) {
  const auto started = std::chrono::steady_clock::now();
  ArmResult arm;
  arm.records.resize(static_cast<std::size_t>(config.games));
  arm.counters.resize(static_cast<std::size_t>(config.games));
  std::atomic<int> nextIndex{0};
  std::atomic<int> finished{0};
  std::atomic<int> failures{0};
  const int threads = std::max(1, std::min(config.threads, config.games));
  std::vector<std::thread> pool;
  pool.reserve(static_cast<std::size_t>(threads));
  for (int worker = 0; worker < threads; ++worker) {
    pool.emplace_back([&]() {
      VetoDecider decider;
      decider.horizon = config.horizon;
      decider.maximumRootQLoss = config.maximumRootQLoss;
      decider.rollout_enabled = candidate;
      for (;;) {
        const int index = nextIndex.fetch_add(1);
        if (index >= config.games) return;
        const std::uint32_t seed =
            config.seedStart + static_cast<std::uint32_t>(index);
        const VetoCounters before = decider.counters;
        try {
          arm.records[static_cast<std::size_t>(index)] =
              lifetime::runGame(seed, decider, config.maximumMoves, false);
        } catch (const std::exception& error) {
          failures.fetch_add(1);
          const std::lock_guard<std::mutex> lock(progressMutex);
          std::cerr << "FAILURE seed 0x" << std::hex << seed << std::dec << ": "
                    << error.what() << '\n';
          continue;
        }
        arm.counters[static_cast<std::size_t>(index)] =
            decider.counters - before;
        if (config.quiet) continue;
        const int done = finished.fetch_add(1) + 1;
        const lifetime::GameRecord& r = arm.records[static_cast<std::size_t>(index)];
        const VetoCounters& c = arm.counters[static_cast<std::size_t>(index)];
        const std::lock_guard<std::mutex> lock(progressMutex);
        std::cerr << "[" << label << ' ' << done << '/' << config.games
                  << "] seed 0x" << std::hex << seed << std::dec << " score "
                  << r.score << " moves " << r.moves << " rises " << r.rises
                  << " vetoes " << c.switches << '/' << c.routed_decisions
                  << (r.censored ? " CAPPED" : "") << " (" << std::fixed
                  << std::setprecision(1) << r.wallSeconds << "s)\n"
                  << std::defaultfloat;
      }
    });
  }
  for (std::thread& thread : pool) thread.join();
  arm.failures = failures.load();
  arm.wallSeconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();
  return arm;
}

// ---------------------------------------------------------------------------
// Paired statistics.
// ---------------------------------------------------------------------------
struct PairedSummary {
  double meanDelta = 0.0;
  double medianDelta = 0.0;
  double bootstrapLower95 = 0.0;
  int wins = 0;
  int ties = 0;
  int losses = 0;
};

PairedSummary pairedSummary(const std::vector<double>& candidate,
                            const std::vector<double>& baseline,
                            std::uint32_t bootstrapSeed) {
  PairedSummary summary;
  std::vector<double> deltas;
  deltas.reserve(candidate.size());
  for (std::size_t index = 0; index < candidate.size(); ++index) {
    const double delta = candidate[index] - baseline[index];
    deltas.push_back(delta);
    if (delta > 0.0) ++summary.wins;
    else if (delta < 0.0) ++summary.losses;
    else ++summary.ties;
  }
  if (deltas.empty()) return summary;
  summary.meanDelta =
      std::accumulate(deltas.begin(), deltas.end(), 0.0) /
      static_cast<double>(deltas.size());
  summary.medianDelta = lifetime::quantile(deltas, 0.5);
  summary.bootstrapLower95 =
      lifetime::bootstrapLowerBound(deltas, 0.05, 20000, bootstrapSeed);
  return summary;
}

VetoCounters totalCounters(const std::vector<VetoCounters>& counters) {
  VetoCounters total;
  for (const VetoCounters& c : counters) {
    total.decisions += c.decisions;
    total.danger_decisions += c.danger_decisions;
    total.routed_decisions += c.routed_decisions;
    total.alternatives += c.alternatives;
    total.switches += c.switches;
    total.passing_alternatives += c.passing_alternatives;
    total.survivor_rejections += c.survivor_rejections;
    total.clear_rejections += c.clear_rejections;
    total.return_rejections += c.return_rejections;
    total.root_q_rejections += c.root_q_rejections;
    total.d2_calls += c.d2_calls;
    total.d2_work += c.d2_work;
    total.d4_work += c.d4_work;
    total.synthetic_transitions += c.synthetic_transitions;
    total.d4_seconds += c.d4_seconds;
    total.rollout_seconds += c.rollout_seconds;
  }
  return total;
}

void writeCounters(std::ostream& out, const VetoCounters& c) {
  out << "{\"decisions\": " << c.decisions
      << ", \"dangerDecisions\": " << c.danger_decisions
      << ", \"vetoOpportunities\": " << c.routed_decisions
      << ", \"alternativesConsidered\": " << c.alternatives
      << ", \"vetoesTaken\": " << c.switches
      << ", \"passingAlternatives\": " << c.passing_alternatives
      << ", \"survivorRejections\": " << c.survivor_rejections
      << ", \"clearRejections\": " << c.clear_rejections
      << ", \"returnRejections\": " << c.return_rejections
      << ", \"rootQRejections\": " << c.root_q_rejections
      << ", \"d2Calls\": " << c.d2_calls << ", \"d2Work\": " << c.d2_work
      << ", \"d4Work\": " << c.d4_work
      << ", \"syntheticTransitions\": " << c.synthetic_transitions
      << ", \"d4Seconds\": " << c.d4_seconds
      << ", \"rolloutSeconds\": " << c.rollout_seconds << "}";
}

std::string configJson(const Config& config) {
  std::ostringstream out;
  out << std::setprecision(12) << "{\"horizon\": " << config.horizon
      << ", \"scenarios\": " << kScenarios
      << ", \"continuationDepth\": " << kContinuationDepth
      << ", \"dangerHeight\": " << kDangerHeight
      << ", \"maximumRootQLoss\": " << config.maximumRootQLoss
      << ", \"levelBonus\": " << kLevelBonus
      << ", \"pairedT975Df6\": " << kPairedT975Df6 << "}";
  return out.str();
}

// ---------------------------------------------------------------------------
// CHECK-tier self test.
// ---------------------------------------------------------------------------
bool selfTest(std::ostream& out) {
  bool ok = true;

  // 1. Scoring and cadence.
  ok = ok && kLevelBonus == 17'000 && kClearBonus == 70'000 &&
       kMovesPerLevel == 5;

  // 2. Determinism: the same public state yields the same rollout twice.
  // Play a cheap fixed policy forward until the routing predicate fires, so
  // that the veto-selection path itself is exercised.
  State state = initialHeadlessState(0xa51e'0000u);
  for (int move = 0; move < 200 && !state.game_over; ++move) {
    if (isDanger(observable(state)) && move >= 12) break;
    MoveResult result;
    if (!playHeadlessMove(state, 0xa51e'0000u, centerFirstMove(state.board),
                          result)) {
      ok = false;
      break;
    }
  }
  ok = ok && !state.game_over && isDanger(observable(state));
  const ObservableState observed = observable(state);
  const RolloutEvaluation first = evaluateRollouts(observed, 8);
  const RolloutEvaluation second = evaluateRollouts(observed, 8);
  bool deterministic = first.legal == second.legal &&
                       first.legal_actions == second.legal_actions &&
                       first.synthetic_transitions == second.synthetic_transitions;
  for (int column = 0; column < kBoardSize; ++column) {
    deterministic = deterministic && first.actions[column] == second.actions[column];
  }
  ok = ok && deterministic;

  // 3. Reflection: the mirrored public state gives the mirrored rollout.
  ObservableState reflected = observed;
  reflected.board = cfpi::detail::mirrorBoard(observed.board);
  const RolloutEvaluation mirrored = evaluateRollouts(reflected, 8);
  bool reflectionOk = true;
  for (int column = 0; column < kBoardSize; ++column) {
    const int partner = kBoardSize - 1 - column;
    reflectionOk = reflectionOk && first.legal[column] == mirrored.legal[partner];
    if (!first.legal[column]) continue;
    reflectionOk =
        reflectionOk &&
        std::abs(first.actions[column].mean_value -
                 mirrored.actions[partner].mean_value) < 1.0e-6;
  }
  ok = ok && reflectionOk;

  // 4. Metadata blindness: score / level / move counter must not matter.
  State metadata = materialize(observed);
  metadata.score = 987'654'321;
  metadata.level = 42;
  metadata.moves_played = 999;
  const RolloutEvaluation blind = evaluateRollouts(observable(metadata), 8);
  bool blindOk = true;
  for (int column = 0; column < kBoardSize; ++column) {
    blindOk = blindOk && first.legal[column] == blind.legal[column] &&
              (!first.legal[column] ||
               first.actions[column] == blind.actions[column]);
  }
  ok = ok && blindOk;

  // 5. Legality of the vetoed decision at a routed root.
  const Decision decision =
      chooseAction(state, true, 8, static_cast<double>(kLevelBonus));
  ok = ok && isLegal(state.board, decision.action) && decision.routed;
  // A wider root-Q band can only ever admit more alternatives, never fewer.
  const Decision narrow = chooseAction(state, true, 8, 7'000.0);
  const Decision wide = chooseAction(state, true, 8, 17'000.0);
  const bool bandMonotone =
      wide.passing_alternatives >= narrow.passing_alternatives &&
      wide.root_q_rejections <= narrow.root_q_rejections;
  ok = ok && bandMonotone;
  // Disabling the rollout must reproduce fair D4 exactly.
  const Decision plain = chooseAction(state, false, 8, 17'000.0);
  const bool fallbackOk =
      !plain.routed && plain.action == plain.d4_action &&
      plain.d4_action == decision.d4_action;
  ok = ok && fallbackOk;

  // 6. Resource proof: worst-case D2 arithmetic.
  ok = ok && kWorstD2Work == 2'485 && kWorstD2CacheEntries == 35;

  // 7. Seed lease containment.
  ok = ok && kLeaseFirst <= kLeaseLast && (kLeaseLast - kLeaseFirst) == 0x3fffu;

  out << "ROLLOUT_VETO_17K_SELFTEST {\"scoring\": true, \"deterministic\": "
      << (deterministic ? "true" : "false") << ", \"reflection\": "
      << (reflectionOk ? "true" : "false") << ", \"metadataBlind\": "
      << (blindOk ? "true" : "false") << ", \"routedAction\": "
      << decision.action << ", \"routed\": "
      << (decision.routed ? "true" : "false")
      << ", \"alternatives\": " << decision.alternatives
      << ", \"passingAlternatives7k\": " << narrow.passing_alternatives
      << ", \"passingAlternatives17k\": " << wide.passing_alternatives
      << ", \"bandMonotone\": " << (bandMonotone ? "true" : "false")
      << ", \"d4Fallback\": " << (fallbackOk ? "true" : "false")
      << ", \"passed\": "
      << (ok ? "true" : "false") << "}\n";
  return ok;
}

// ---------------------------------------------------------------------------
// Differential parity dump.
//
// Prints a canonical digest of evaluateRollouts over a deterministic walk of
// public states.  parity-original.cpp prints the same digest using the
// UNMODIFIED historical source (with only its 7'000 assertion patched in the
// build tree) so that "minimal port" is a checked claim, not an assertion.
// ---------------------------------------------------------------------------
constexpr std::uint32_t kParitySeed = 0xa51e'3f20u;
constexpr int kParityHorizon = 6;
constexpr int kParityStates = 10;

void parityDump(std::ostream& out) {
  out << std::setprecision(17);
  State state = initialHeadlessState(kParitySeed);
  int emitted = 0;
  for (int move = 0; move < 400 && !state.game_over && emitted < kParityStates;
       ++move) {
    if (move >= 6 && move % 4 == 0) {
      const RolloutEvaluation r =
          evaluateRollouts(observable(state), kParityHorizon);
      for (int column = 0; column < kBoardSize; ++column) {
        out << "S" << emitted << " C" << column << ' '
            << (r.legal[column] ? 1 : 0) << ' '
            << r.actions[column].mean_value << ' '
            << r.actions[column].surviving_scenarios << ' '
            << r.actions[column].mean_numbered_clears << '\n';
      }
      out << "S" << emitted << " T " << r.legal_actions << ' '
          << r.synthetic_transitions << ' ' << r.continuation.calls << ' '
          << r.continuation.work << ' ' << r.continuation.nodes << '\n';
      ++emitted;
    }
    MoveResult result;
    const int column = fairDepthTwoAction(observable(state), nullptr);
    if (column < 0 || !playHeadlessMove(state, kParitySeed, column, result)) {
      break;
    }
  }
}

// ---------------------------------------------------------------------------
// Veto-condition diagnostic.
//
// For every alternative at every routed decision of a single probe game, dump
// the raw quantities the four veto conditions are computed from.  This answers
// "why does the rule never fire?" quantitatively.  Probe seeds only.
// ---------------------------------------------------------------------------
void vetoDiagnostic(std::uint32_t seed, int moves, int horizon,
                    std::ostream& out) {
  out << "moveIndex,alternative,d4Action,meanDelta,sdDelta,required,"
         "lower95,survivorAdv,clearAdv,rootQLoss\n";
  out << std::setprecision(10);
  State state = initialHeadlessState(seed);
  for (int move = 0; move < moves && !state.game_over; ++move) {
    const Decision decision =
        chooseAction(state, true, horizon, static_cast<double>(kLevelBonus));
    if (decision.routed) {
      const ActionRollout& base = decision.rollout.actions[decision.d4_action];
      for (const int action : cfpi::detail::kColumnOrder) {
        if (!decision.rollout.legal[action] || action == decision.d4_action) {
          continue;
        }
        const ActionRollout& alt = decision.rollout.actions[action];
        std::array<double, kScenarios> differences{};
        double mean = 0.0;
        for (int s = 0; s < kScenarios; ++s) {
          differences[s] = alt.scenarios[s].value - base.scenarios[s].value;
          mean += differences[s] / kScenarios;
        }
        double squares = 0.0;
        for (const double value : differences) {
          squares += (value - mean) * (value - mean);
        }
        const double sd =
            std::sqrt(squares / static_cast<double>(kScenarios - 1));
        const double required =
            kPairedT975Df6 * sd / std::sqrt(static_cast<double>(kScenarios));
        out << move << ',' << action << ',' << decision.d4_action << ',' << mean
            << ',' << sd << ',' << required << ',' << (mean - required) << ','
            << (alt.surviving_scenarios - base.surviving_scenarios) << ','
            << (alt.mean_numbered_clears - base.mean_numbered_clears) << ','
            << (decision.search.root_values[decision.d4_action] -
                decision.search.root_values[action])
            << '\n';
      }
    }
    MoveResult result;
    if (!playHeadlessMove(state, seed, decision.action, result)) break;
  }
}

// ---------------------------------------------------------------------------
// Entry point.
// ---------------------------------------------------------------------------
Config parseConfig(int argc, char** argv, int begin) {
  Config config;
  for (int index = begin; index < argc; ++index) {
    const std::string argument = argv[index];
    auto value = [&]() -> std::string {
      if (index + 1 >= argc) {
        throw std::invalid_argument("missing value for " + argument);
      }
      return argv[++index];
    };
    if (argument == "--seed-start") {
      config.seedStart = static_cast<std::uint32_t>(
          std::stoul(value(), nullptr, 0));
    } else if (argument == "--games") {
      config.games = std::stoi(value());
    } else if (argument == "--threads") {
      config.threads = std::stoi(value());
    } else if (argument == "--max-moves") {
      config.maximumMoves = std::stoi(value());
    } else if (argument == "--horizon") {
      config.horizon = std::stoi(value());
    } else if (argument == "--root-q-loss") {
      config.maximumRootQLoss = std::stod(value());
    } else if (argument == "--output") {
      config.output = value();
    } else if (argument == "--baseline-only") {
      config.baselineOnly = true;
    } else if (argument == "--candidate-only") {
      config.candidateOnly = true;
    } else if (argument == "--quiet") {
      config.quiet = true;
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  if (config.games < 1) throw std::invalid_argument("--games must be >= 1");
  if (config.threads < 1) throw std::invalid_argument("--threads must be >= 1");
  if (config.horizon < 1 || config.horizon > kMaximumRolloutHorizon) {
    throw std::invalid_argument("--horizon must be in 1..25");
  }
  const std::uint64_t last = static_cast<std::uint64_t>(config.seedStart) +
                             static_cast<std::uint64_t>(config.games) - 1;
  if (config.seedStart < kLeaseFirst || last > kLeaseLast) {
    throw std::invalid_argument(
        "requested seeds fall outside lease SEEDLEASE-A51D-VETO "
        "(0xa51e0000..0xa51e3fff)");
  }
  if (config.output.rfind("/tmp/", 0) == 0) {
    throw std::invalid_argument("refusing to write to a shared /tmp path");
  }
  return config;
}

int run(const Config& config, std::ostream& report) {
  const auto started = std::chrono::steady_clock::now();
  const std::string base =
      config.output.size() > 5 &&
              config.output.compare(config.output.size() - 5, 5, ".json") == 0
          ? config.output.substr(0, config.output.size() - 5)
          : config.output;

  ArmResult baseline;
  ArmResult candidate;
  if (!config.candidateOnly) baseline = runArm(config, false, "d4");
  if (!config.baselineOnly) candidate = runArm(config, true, "veto");

  lifetime::CohortOptions options;
  options.seedStart = config.seedStart;
  options.games = config.games;
  options.maximumMoves = config.maximumMoves;
  options.threads = config.threads;
  options.quiet = config.quiet;

  if (!config.candidateOnly) {
    std::ofstream out(base + "-baseline.json");
    if (!out) throw std::runtime_error("cannot open baseline artifact");
    lifetime::writeArtifact(out, "fair-d4-s5-unmodified", configJson(config),
                            options, baseline.records, baseline.wallSeconds);
  }
  if (!config.baselineOnly) {
    std::ofstream out(base + "-candidate.json");
    if (!out) throw std::runtime_error("cannot open candidate artifact");
    lifetime::writeArtifact(out, "d4-d2-rollout-veto-17k", configJson(config),
                            options, candidate.records, candidate.wallSeconds);
  }

  const double totalWall =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();

  std::ofstream out(config.output);
  if (!out) throw std::runtime_error("cannot open paired artifact");
  out << std::setprecision(12);
  out << "{\n  \"format\": \"drop7-rollout-veto-17k-paired-v1\",\n";
  out << "  \"experiment\": \"rollout-veto-17k\",\n";
  out << "  \"seedLease\": \"SEEDLEASE-A51D-VETO\",\n";
  out << "  \"dataRole\": \"exploratory-development-diagnostic\",\n";
  out << "  \"seedStartHex\": \"0x" << std::hex << config.seedStart << std::dec
      << "\",\n";
  out << "  \"games\": " << config.games << ",\n";
  out << "  \"maximumMoves\": " << config.maximumMoves << ",\n";
  out << "  \"threads\": " << config.threads << ",\n";
  out << "  \"config\": " << configJson(config) << ",\n";
  out << "  \"baselineWallSeconds\": " << baseline.wallSeconds << ",\n";
  out << "  \"candidateWallSeconds\": " << candidate.wallSeconds << ",\n";
  out << "  \"totalWallSeconds\": " << totalWall << ",\n";
  out << "  \"baselineFailures\": " << baseline.failures << ",\n";
  out << "  \"candidateFailures\": " << candidate.failures << ",\n";
  out << "  \"candidateCounters\": ";
  writeCounters(out, totalCounters(candidate.counters));
  out << ",\n";
  out << "  \"baselineCounters\": ";
  writeCounters(out, totalCounters(baseline.counters));
  out << ",\n";

  if (!config.candidateOnly && !config.baselineOnly &&
      baseline.failures == 0 && candidate.failures == 0) {
    std::vector<double> candidateScores, baselineScores;
    std::vector<double> candidateMoves, baselineMoves;
    for (int index = 0; index < config.games; ++index) {
      candidateScores.push_back(
          static_cast<double>(candidate.records[static_cast<std::size_t>(index)].score));
      baselineScores.push_back(
          static_cast<double>(baseline.records[static_cast<std::size_t>(index)].score));
      candidateMoves.push_back(
          static_cast<double>(candidate.records[static_cast<std::size_t>(index)].moves));
      baselineMoves.push_back(
          static_cast<double>(baseline.records[static_cast<std::size_t>(index)].moves));
    }
    const PairedSummary score =
        pairedSummary(candidateScores, baselineScores, 0xa51e'5eedu);
    const PairedSummary moves =
        pairedSummary(candidateMoves, baselineMoves, 0xa51e'6eedu);
    out << "  \"pairedScore\": {\"meanDelta\": " << score.meanDelta
        << ", \"medianDelta\": " << score.medianDelta
        << ", \"bootstrapLower95\": " << score.bootstrapLower95
        << ", \"wins\": " << score.wins << ", \"ties\": " << score.ties
        << ", \"losses\": " << score.losses << "},\n";
    out << "  \"pairedMoves\": {\"meanDelta\": " << moves.meanDelta
        << ", \"medianDelta\": " << moves.medianDelta
        << ", \"bootstrapLower95\": " << moves.bootstrapLower95
        << ", \"wins\": " << moves.wins << ", \"ties\": " << moves.ties
        << ", \"losses\": " << moves.losses << "},\n";
    report << std::setprecision(10)
           << "ROLLOUT_VETO_17K_PAIRED {\"scoreMeanDelta\": " << score.meanDelta
           << ", \"scoreLower95\": " << score.bootstrapLower95
           << ", \"scoreWTL\": \"" << score.wins << '-' << score.ties << '-'
           << score.losses << "\", \"moveMeanDelta\": " << moves.meanDelta
           << ", \"moveLower95\": " << moves.bootstrapLower95
           << ", \"moveWTL\": \"" << moves.wins << '-' << moves.ties << '-'
           << moves.losses << "\"}\n";
  }

  out << "  \"perGame\": [\n";
  for (int index = 0; index < config.games; ++index) {
    if (index != 0) out << ",\n";
    const auto i = static_cast<std::size_t>(index);
    out << "    {\"seedHex\": \"0x" << std::hex
        << (config.seedStart + static_cast<std::uint32_t>(index)) << std::dec
        << "\"";
    if (!config.candidateOnly) {
      const lifetime::GameRecord& b = baseline.records[i];
      out << ", \"baseline\": {\"score\": " << b.score
          << ", \"moves\": " << b.moves
          << ", \"censored\": " << (b.censored ? "true" : "false")
          << ", \"rises\": " << b.rises
          << ", \"boardClears\": " << b.boardClears
          << ", \"numberedCleared\": " << b.numberedCleared
          << ", \"coversRevealed\": " << b.coversRevealed
          << ", \"maxChainDepth\": " << b.maxChainDepth
          << ", \"wallSeconds\": " << b.wallSeconds << "}";
    }
    if (!config.baselineOnly) {
      const lifetime::GameRecord& c = candidate.records[i];
      out << ", \"candidate\": {\"score\": " << c.score
          << ", \"moves\": " << c.moves
          << ", \"censored\": " << (c.censored ? "true" : "false")
          << ", \"rises\": " << c.rises
          << ", \"boardClears\": " << c.boardClears
          << ", \"numberedCleared\": " << c.numberedCleared
          << ", \"coversRevealed\": " << c.coversRevealed
          << ", \"maxChainDepth\": " << c.maxChainDepth
          << ", \"wallSeconds\": " << c.wallSeconds << "}";
      out << ", \"veto\": ";
      writeCounters(out, candidate.counters[i]);
    }
    out << "}";
  }
  out << "\n  ]\n}\n";
  out.close();

  report << "ROLLOUT_VETO_17K_DONE {\"artifact\": \"" << config.output
         << "\", \"totalWallSeconds\": " << totalWall << "}\n";
  return 0;
}

}  // namespace drop7::rollout_veto_17k

int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::rollout_veto_17k::selfTest(std::cout) ? EXIT_SUCCESS
                                                          : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--veto-diagnostic") {
      const std::uint32_t seed =
          argc >= 3 ? static_cast<std::uint32_t>(std::stoul(argv[2], nullptr, 0))
                    : 0xa51e'3f30u;
      const int moves = argc >= 4 ? std::stoi(argv[3]) : 40;
      const int horizon = argc >= 5 ? std::stoi(argv[4]) : 25;
      if (seed < drop7::rollout_veto_17k::kLeaseFirst ||
          seed > drop7::rollout_veto_17k::kLeaseLast) {
        throw std::invalid_argument("diagnostic seed outside lease");
      }
      drop7::rollout_veto_17k::vetoDiagnostic(seed, moves, horizon, std::cout);
      return EXIT_SUCCESS;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--parity-dump") {
      drop7::rollout_veto_17k::parityDump(std::cout);
      return EXIT_SUCCESS;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto config = drop7::rollout_veto_17k::parseConfig(argc, argv, 2);
      return drop7::rollout_veto_17k::run(config, std::cout);
    }
    std::cerr
        << "usage: veto --self-test\n"
           "       veto --run [--seed-start HEX] [--games N] [--threads N]\n"
           "                  [--max-moves N] [--horizon N] [--root-q-loss X]\n"
           "                  [--output PATH] [--baseline-only]"
           " [--candidate-only] [--quiet]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
