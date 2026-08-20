#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#include "../../fair-expectimax/reference/fair-only-depth4.cpp"
#undef DROP7_FAIR_ONLY_DEPTH4_LIBRARY

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
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// A public-information 25-move rollout may only veto the reference fair-D4
// action.  Every continuation decision is a fresh, completed, full-width fair
// D2 search whose type contains no tape or scenario metadata.  Synthetic
// reveals and future visible discs use separate, event-indexed domains.
namespace drop7::d4_d2_rollout_veto {

namespace d4 = drop7::fair_only_depth4;
namespace fair = drop7::fair_only_horizon;

constexpr std::uint32_t kFittingSeedStart = 0x3ded'0000u;
constexpr std::uint32_t kHeldoutSeedStart = 0x3dee'0000u;
constexpr std::uint32_t kScreenSeedStart = 0x3ebb'0000u;
constexpr std::uint32_t kConfirmationSeedStart = 0x3ebc'0000u;
constexpr int kFittingGames = 4;
constexpr int kHeldoutGames = 8;
constexpr int kScreenGames = 8;
constexpr int kConfirmationGames = 16;
constexpr int kMaximumMoves = 1'000;
constexpr int kParallelism = 4;
constexpr int kRolloutHorizon = 25;
constexpr int kScenarios = 7;
constexpr int kContinuationDepth = 2;
constexpr int kDangerHeight = 4;
constexpr int kEventsPerStep = 64;
constexpr double kPairedT975Df6 = 2.446912;
constexpr double kMaximumRootQLoss = static_cast<double>(kLevelBonus);
constexpr double kLowerTailRetention = 0.90;
constexpr double kMaximumProjectedWallSeconds = 45.0 * 60.0;
constexpr double kFullProtocolProjectionWaves = 18.0;
constexpr std::uint64_t kMaximumRssBytes = 128u * 1024u * 1024u;
constexpr std::uint32_t kTapeSeedDomain = 0x4432'5254u;  // "D2RT"
constexpr std::uint32_t kRevealTapeDomain = 0x4432'5256u;  // "D2RV"
constexpr std::uint32_t kVisibleTapeDomain = 0x4432'5653u;  // "D2VS"

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
constexpr std::uint64_t kWorstD2CacheEntries =
    kBoardSize * fair::kChanceSamples;
constexpr std::uint64_t kWorstD2CallsPerRoutedDecision =
    kBoardSize * kScenarios * (kRolloutHorizon - 1);
constexpr std::uint64_t kWorstD2WorkPerRoutedDecision =
    kWorstD2CallsPerRoutedDecision * kWorstD2Work;
constexpr std::uint64_t kWorstSyntheticTransitionsPerRoutedDecision =
    kBoardSize * kScenarios * kRolloutHorizon;

static_assert(kWorstD2Work == 2'485);
static_assert(kWorstD2CacheEntries == 35);
static_assert(kWorstD2CallsPerRoutedDecision == 1'176);
static_assert(kWorstD2WorkPerRoutedDecision == 2'922'360);
static_assert(kWorstSyntheticTransitionsPerRoutedDecision == 1'225);
static_assert(d4::kCandidateDepth == 4);
static_assert(fair::kChanceSamples == 5);
static_assert(fair::kTerminalUtility == -1'000'000.0);
static_assert(kLevelBonus == 7'000);
static_assert(kScenarios == kBoardSize);
static_assert(kEventsPerStep > kCellCount);
static_assert(kFittingSeedStart + kFittingGames < kHeldoutSeedStart);
static_assert(kHeldoutSeedStart + kHeldoutGames < kScreenSeedStart);
static_assert(kScreenSeedStart + kScreenGames < kConfirmationSeedStart);
static_assert((kFittingSeedStart >> 24) != 0x7du &&
              (kFittingSeedStart >> 24) != 0xd7u);
static_assert((kHeldoutSeedStart >> 24) != 0x7du &&
              (kHeldoutSeedStart >> 24) != 0xd7u);
static_assert((kScreenSeedStart >> 24) != 0x7du &&
              (kScreenSeedStart >> 24) != 0xd7u);
static_assert((kConfirmationSeedStart >> 24) != 0x7du &&
              (kConfirmationSeedStart >> 24) != 0xd7u);

std::mutex progress_mutex;

struct Options {
  std::string output = "/tmp/drop7-d4-d2-rollout-veto.json";
  std::string teacher_output =
      "/tmp/drop7-d4-d2-rollout-veto-teacher.jsonl";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string argument = argv[index];
    if (argument == "--output") {
      result.output = argv[index + 1];
    } else if (argument == "--teacher-output") {
      result.teacher_output = argv[index + 1];
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  return result;
}

struct ObservableState {
  Board board{};
  std::uint8_t next_disc = 1;
  int moves_remaining = kMovesPerLevel;
  bool game_over = false;
};

ObservableState observable(const State& source) {
  return {source.board, source.next_disc, source.moves_remaining,
          source.game_over};
}

State materialize(const ObservableState& source) {
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

bool sameObservable(const ObservableState& first,
                    const ObservableState& second) {
  return first.board == second.board && first.next_disc == second.next_disc &&
         first.moves_remaining == second.moves_remaining &&
         first.game_over == second.game_over;
}

ObservableState mirror(const ObservableState& source) {
  ObservableState result = source;
  result.board = cfpi::detail::mirrorBoard(source.board);
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
  const std::array<int, kBoardSize> heights =
      cfpi::detail::columnHeights(board);
  return *std::max_element(heights.begin(), heights.end());
}

bool isDanger(const ObservableState& state) {
  return maximumHeight(state.board) >= kDangerHeight;
}

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
static_assert(std::is_same_v<decltype(&fairDepthTwoAction),
                             ContinuationFunction>);

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
  const double unit = cfpi::detail::stratifiedUnit(
      root_seed, scenario, kScenarios, domain, step);
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

RolloutEvaluation evaluateRollouts(const ObservableState& source,
                                   int horizon = kRolloutHorizon) {
  if (source.game_over || horizon < 1 || horizon > kRolloutHorizon) {
    throw std::invalid_argument("invalid D2 rollout root or horizon");
  }
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
        if (!playSyntheticMove(state, selected, tape_seed, scenario, step,
                               move, &result.synthetic_transitions)) {
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
  if (result.synthetic_transitions >
          kWorstSyntheticTransitionsPerRoutedDecision ||
      result.continuation.calls > kWorstD2CallsPerRoutedDecision ||
      result.continuation.work > kWorstD2WorkPerRoutedDecision ||
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
    differences[scenario] = candidate.scenarios[scenario].value -
                            baseline.scenarios[scenario].value;
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
                                double candidate_root_q,
                                double baseline_root_q) {
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
  result.root_q_ok = result.root_q_loss <= kMaximumRootQLoss + 1.0e-9;
  result.passed = result.survivors_ok && result.clears_ok &&
                  result.return_ok && result.root_q_ok;
  return result;
}

struct Decision {
  int action = -1;
  int d4_action = -1;
  bool danger = false;
  bool routed = false;
  bool switched = false;
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

struct TeacherRecord {
  ObservableState state{};
  Decision decision{};
};

Decision chooseAction(const State& source, bool rollout_enabled = true,
                      int horizon = kRolloutHorizon) {
  Decision result;
  const auto d4_started = std::chrono::steady_clock::now();
  result.search = d4::chooseDepth4Action(source);
  result.d4_seconds = std::chrono::duration<double>(
                          std::chrono::steady_clock::now() - d4_started)
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
                               std::chrono::steady_clock::now() -
                               rollout_started)
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
    const AlternativeTest test = testAlternative(
        result.rollout.actions[action], baseline,
        result.search.root_values[action], baseline_q);
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

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t numbered_cleared = 0;
  std::uint64_t covers_revealed = 0;
  int maximum_chain = 0;
  std::uint64_t decisions = 0;
  std::uint64_t danger_decisions = 0;
  std::uint64_t routed_decisions = 0;
  std::uint64_t switches = 0;
  std::uint64_t passing_alternatives = 0;
  std::uint64_t survivor_rejections = 0;
  std::uint64_t clear_rejections = 0;
  std::uint64_t return_rejections = 0;
  std::uint64_t root_q_rejections = 0;
  double switch_return_lower95_sum = 0.0;
  double switch_clear_advantage_sum = 0.0;
  double switch_root_q_loss_sum = 0.0;
  std::int64_t switch_survivor_advantage_sum = 0;
  std::uint64_t d4_work = 0;
  std::uint64_t d4_nodes = 0;
  std::uint64_t d4_cache_hits = 0;
  std::size_t peak_d4_cache_entries = 0;
  std::uint64_t synthetic_transitions = 0;
  D2Metrics continuation{};
  std::uint64_t peak_rss_bytes = 0;
  double elapsed_seconds = 0.0;
};

void observeMove(const MoveResult& move, GameResult& result) {
  result.maximum_chain =
      std::max(result.maximum_chain, static_cast<int>(move.waves.size()));
  for (const Wave& wave : move.waves) {
    result.numbered_cleared += static_cast<std::uint64_t>(wave.cleared);
    result.covers_revealed += static_cast<std::uint64_t>(wave.revealed);
  }
}

void reportGame(std::string_view label, const GameResult& result) {
  const std::lock_guard<std::mutex> lock(progress_mutex);
  std::cerr << label << " seed 0x" << std::hex << result.seed << std::dec
            << ' ' << result.score << " (" << result.moves << " moves"
            << (result.censored ? ", capped" : "") << ", switches "
            << result.switches << '/' << result.routed_decisions
            << ", D2 calls " << result.continuation.calls << ")\n";
}

GameResult runGame(std::uint32_t seed, bool candidate,
                   std::string_view label,
                   std::vector<TeacherRecord>* teacher = nullptr) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    d4::SearchDecision search;
    int action = -1;
    if (!candidate) {
      search = d4::chooseDepth4Action(state);
      action = search.action;
    } else {
      const Decision decision = chooseAction(state);
      search = decision.search;
      action = decision.action;
      result.danger_decisions += decision.danger;
      result.routed_decisions += decision.routed;
      result.switches += decision.switched;
      result.passing_alternatives += decision.passing_alternatives;
      result.survivor_rejections += decision.survivor_rejections;
      result.clear_rejections += decision.clear_rejections;
      result.return_rejections += decision.return_rejections;
      result.root_q_rejections += decision.root_q_rejections;
      if (decision.switched) {
        result.switch_return_lower95_sum +=
            decision.selected_return_lower95;
        result.switch_clear_advantage_sum +=
            decision.selected_clear_advantage;
        result.switch_root_q_loss_sum += decision.selected_root_q_loss;
        result.switch_survivor_advantage_sum +=
            decision.selected_survivor_advantage;
      }
      result.synthetic_transitions += decision.rollout.synthetic_transitions;
      result.continuation.calls += decision.rollout.continuation.calls;
      result.continuation.work += decision.rollout.continuation.work;
      result.continuation.nodes += decision.rollout.continuation.nodes;
      result.continuation.cache_hits +=
          decision.rollout.continuation.cache_hits;
      result.continuation.peak_cache_entries = std::max(
          result.continuation.peak_cache_entries,
          decision.rollout.continuation.peak_cache_entries);
      result.continuation.root_actions +=
          decision.rollout.continuation.root_actions;
      result.continuation.full_root =
          result.continuation.full_root &&
          decision.rollout.continuation.full_root;
      if (teacher != nullptr && decision.routed) {
        teacher->push_back({observable(state), decision});
      }
    }
    if (!search.complete || search.completed_depth != d4::kCandidateDepth ||
        !isLegal(state.board, action)) {
      throw std::runtime_error("game policy failed D4 completion or legality");
    }
    ++result.decisions;
    result.d4_work += search.work;
    result.d4_nodes += search.nodes;
    result.d4_cache_hits += search.cache_hits;
    result.peak_d4_cache_entries =
        std::max(result.peak_d4_cache_entries, search.cache_entries);
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("headless D2 rollout-veto transition failed");
    }
    observeMove(move, result);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.peak_rss_bytes = d4::peakRssBytes();
  result.elapsed_seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
  if (result.peak_rss_bytes > kMaximumRssBytes ||
      result.peak_d4_cache_entries > d4::kMaximumCacheEntries ||
      result.continuation.peak_cache_entries > kWorstD2CacheEntries ||
      !result.continuation.full_root) {
    throw std::runtime_error("rollout-veto game exceeded resource bound");
  }
  reportGame(label, result);
  return result;
}

struct Cohort {
  std::vector<GameResult> baseline;
  std::vector<GameResult> candidate;
  double wall_seconds = 0.0;
};

Cohort runCohort(std::uint32_t seed_start, int games,
                 std::string_view phase) {
  const auto started = std::chrono::steady_clock::now();
  Cohort result;
  result.baseline.resize(games);
  result.candidate.resize(games);
  std::atomic<int> next_task{0};
  const int tasks = 2 * games;
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, tasks); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int task = next_task.fetch_add(1);
        if (task >= tasks) return;
        const int game = task / 2;
        const bool candidate = task % 2 != 0;
        const std::uint32_t seed =
            seed_start + static_cast<std::uint32_t>(game);
        if (candidate) {
          result.candidate[game] = runGame(
              seed, true, std::string(phase) + "-candidate");
        } else {
          result.baseline[game] = runGame(
              seed, false, std::string(phase) + "-baseline");
        }
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.wall_seconds = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started)
                            .count();
  return result;
}

void appendCohort(Cohort& target, Cohort source) {
  target.baseline.insert(target.baseline.end(),
                         std::make_move_iterator(source.baseline.begin()),
                         std::make_move_iterator(source.baseline.end()));
  target.candidate.insert(target.candidate.end(),
                          std::make_move_iterator(source.candidate.begin()),
                          std::make_move_iterator(source.candidate.end()));
  target.wall_seconds += source.wall_seconds;
}

struct Summary {
  int games = 0;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  int censored = 0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double lower_half_score = 0.0;
  double lower_half_moves = 0.0;
  std::uint64_t decisions = 0;
  std::uint64_t routed_decisions = 0;
  std::uint64_t switches = 0;
  double switch_rate = 0.0;
  std::uint64_t passing_alternatives = 0;
  std::uint64_t survivor_rejections = 0;
  std::uint64_t clear_rejections = 0;
  std::uint64_t return_rejections = 0;
  std::uint64_t root_q_rejections = 0;
  double mean_switch_return_lower95 = 0.0;
  double mean_switch_clear_advantage = 0.0;
  double mean_switch_root_q_loss = 0.0;
  double mean_switch_survivor_advantage = 0.0;
  std::uint64_t d4_work = 0;
  double d4_work_per_move = 0.0;
  std::size_t peak_d4_cache_entries = 0;
  std::uint64_t synthetic_transitions = 0;
  D2Metrics continuation{};
  double aggregate_game_seconds = 0.0;
  std::uint64_t peak_rss_bytes = 0;
};

double lowerHalfMean(std::vector<double> values) {
  if (values.empty()) throw std::invalid_argument("empty lower-half values");
  std::sort(values.begin(), values.end());
  const std::size_t count = (values.size() + 1) / 2;
  return std::accumulate(values.begin(), values.begin() + count, 0.0) /
         count;
}

Summary summarize(const std::vector<GameResult>& games) {
  if (games.empty()) throw std::invalid_argument("empty rollout cohort");
  Summary result;
  result.games = static_cast<int>(games.size());
  std::vector<double> scores;
  std::vector<double> moves_vector;
  std::uint64_t moves = 0;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
  double return_sum = 0.0;
  double clear_sum = 0.0;
  double q_sum = 0.0;
  std::int64_t survivor_sum = 0;
  for (const GameResult& game : games) {
    scores.push_back(static_cast<double>(game.score));
    moves_vector.push_back(static_cast<double>(game.moves));
    result.mean_score += static_cast<double>(game.score) / games.size();
    result.mean_moves += static_cast<double>(game.moves) / games.size();
    result.censored += game.censored;
    result.decisions += game.decisions;
    result.routed_decisions += game.routed_decisions;
    result.switches += game.switches;
    result.passing_alternatives += game.passing_alternatives;
    result.survivor_rejections += game.survivor_rejections;
    result.clear_rejections += game.clear_rejections;
    result.return_rejections += game.return_rejections;
    result.root_q_rejections += game.root_q_rejections;
    return_sum += game.switch_return_lower95_sum;
    clear_sum += game.switch_clear_advantage_sum;
    q_sum += game.switch_root_q_loss_sum;
    survivor_sum += game.switch_survivor_advantage_sum;
    result.d4_work += game.d4_work;
    result.peak_d4_cache_entries =
        std::max(result.peak_d4_cache_entries,
                 game.peak_d4_cache_entries);
    result.synthetic_transitions += game.synthetic_transitions;
    result.continuation.calls += game.continuation.calls;
    result.continuation.work += game.continuation.work;
    result.continuation.nodes += game.continuation.nodes;
    result.continuation.cache_hits += game.continuation.cache_hits;
    result.continuation.peak_cache_entries = std::max(
        result.continuation.peak_cache_entries,
        game.continuation.peak_cache_entries);
    result.continuation.root_actions += game.continuation.root_actions;
    result.continuation.full_root =
        result.continuation.full_root && game.continuation.full_root;
    result.aggregate_game_seconds += game.elapsed_seconds;
    result.peak_rss_bytes =
        std::max(result.peak_rss_bytes, game.peak_rss_bytes);
    moves += static_cast<std::uint64_t>(game.moves);
    clears += game.numbered_cleared;
    reveals += game.covers_revealed;
  }
  const double move_count =
      static_cast<double>(std::max<std::uint64_t>(1, moves));
  const double switch_count =
      static_cast<double>(std::max<std::uint64_t>(1, result.switches));
  result.clears_per_move = clears / move_count;
  result.reveals_per_move = reveals / move_count;
  result.lower_half_score = lowerHalfMean(scores);
  result.lower_half_moves = lowerHalfMean(moves_vector);
  result.switch_rate = result.switches / move_count;
  result.mean_switch_return_lower95 = return_sum / switch_count;
  result.mean_switch_clear_advantage = clear_sum / switch_count;
  result.mean_switch_root_q_loss = q_sum / switch_count;
  result.mean_switch_survivor_advantage = survivor_sum / switch_count;
  result.d4_work_per_move = result.d4_work / move_count;
  return result;
}

struct Difference {
  double mean = 0.0;
  double lower95 = 0.0;
  int wins = 0;
  int ties = 0;
  int losses = 0;
};

Difference difference(const std::vector<double>& values) {
  if (values.empty()) throw std::invalid_argument("empty paired differences");
  Difference result;
  for (const double value : values) {
    result.mean += value / values.size();
    result.wins += value > 0.0;
    result.ties += value == 0.0;
    result.losses += value < 0.0;
  }
  double squares = 0.0;
  for (const double value : values) {
    squares += (value - result.mean) * (value - result.mean);
  }
  const double deviation = values.size() > 1
                               ? std::sqrt(squares / (values.size() - 1))
                               : 0.0;
  result.lower95 =
      result.mean - 1.96 * deviation / std::sqrt(values.size());
  return result;
}

struct Paired {
  Difference score;
  Difference moves;
  int leave_one_out_positive_both = 0;
};

Paired paired(const Cohort& cohort) {
  if (cohort.baseline.size() != cohort.candidate.size() ||
      cohort.baseline.empty()) {
    throw std::invalid_argument("invalid paired cohort");
  }
  std::vector<double> scores;
  std::vector<double> moves;
  for (std::size_t index = 0; index < cohort.baseline.size(); ++index) {
    scores.push_back(static_cast<double>(cohort.candidate[index].score -
                                         cohort.baseline[index].score));
    moves.push_back(static_cast<double>(cohort.candidate[index].moves -
                                        cohort.baseline[index].moves));
  }
  Paired result{difference(scores), difference(moves), 0};
  if (scores.size() >= 2) {
    const double score_total =
        std::accumulate(scores.begin(), scores.end(), 0.0);
    const double move_total =
        std::accumulate(moves.begin(), moves.end(), 0.0);
    for (std::size_t index = 0; index < scores.size(); ++index) {
      result.leave_one_out_positive_both +=
          score_total - scores[index] > 0.0 &&
          move_total - moves[index] > 0.0;
    }
  }
  return result;
}

struct Gate {
  bool score_improved = false;
  bool moves_improved = false;
  bool clear_throughput_improved = false;
  bool lower_tail_retained = false;
  bool leave_one_out_ok = false;
  bool passed = false;
};

Gate evaluateGate(const Summary& baseline, const Summary& candidate,
                  const Paired& comparison, bool fitting) {
  Gate result;
  result.score_improved = candidate.mean_score > baseline.mean_score;
  result.moves_improved = candidate.mean_moves > baseline.mean_moves;
  result.clear_throughput_improved =
      candidate.clears_per_move > baseline.clears_per_move;
  result.lower_tail_retained =
      candidate.lower_half_score >=
          kLowerTailRetention * baseline.lower_half_score &&
      candidate.lower_half_moves >=
          kLowerTailRetention * baseline.lower_half_moves;
  result.leave_one_out_ok =
      !fitting || comparison.leave_one_out_positive_both >= 3;
  result.passed = result.score_improved && result.moves_improved &&
                  result.clear_throughput_improved &&
                  result.lower_tail_retained && result.leave_one_out_ok;
  return result;
}

struct Stage {
  std::uint32_t seed_start = 0;
  Cohort cohort;
  Summary baseline;
  Summary candidate;
  Paired comparison;
  Gate gate;
};

Stage makeStage(std::uint32_t seed_start, Cohort cohort, bool fitting) {
  Stage result;
  result.seed_start = seed_start;
  result.cohort = std::move(cohort);
  result.baseline = summarize(result.cohort.baseline);
  result.candidate = summarize(result.cohort.candidate);
  result.comparison = paired(result.cohort);
  result.gate = evaluateGate(result.baseline, result.candidate,
                             result.comparison, fitting);
  return result;
}

void writeGame(std::ostream& output, const GameResult& game) {
  output << "{\"seed\":" << game.seed << ",\"score\":" << game.score
         << ",\"moves\":" << game.moves
         << ",\"censored\":" << (game.censored ? "true" : "false")
         << ",\"numberedCleared\":" << game.numbered_cleared
         << ",\"coversRevealed\":" << game.covers_revealed
         << ",\"maximumChain\":" << game.maximum_chain
         << ",\"decisions\":" << game.decisions
         << ",\"routedDecisions\":" << game.routed_decisions
         << ",\"switches\":" << game.switches
         << ",\"passingAlternatives\":" << game.passing_alternatives
         << ",\"survivorRejections\":" << game.survivor_rejections
         << ",\"clearRejections\":" << game.clear_rejections
         << ",\"returnRejections\":" << game.return_rejections
         << ",\"rootQRejections\":" << game.root_q_rejections
         << ",\"d4Work\":" << game.d4_work
         << ",\"d4Nodes\":" << game.d4_nodes
         << ",\"d4CacheHits\":" << game.d4_cache_hits
         << ",\"peakD4CacheEntries\":" << game.peak_d4_cache_entries
         << ",\"syntheticTransitions\":" << game.synthetic_transitions
         << ",\"d2Calls\":" << game.continuation.calls
         << ",\"d2Work\":" << game.continuation.work
         << ",\"d2Nodes\":" << game.continuation.nodes
         << ",\"d2CacheHits\":" << game.continuation.cache_hits
         << ",\"d2RootActions\":" << game.continuation.root_actions
         << ",\"peakD2CacheEntries\":"
         << game.continuation.peak_cache_entries
         << ",\"d2FullRoot\":"
         << (game.continuation.full_root ? "true" : "false")
         << ",\"elapsedSeconds\":" << game.elapsed_seconds
         << ",\"peakRssBytes\":" << game.peak_rss_bytes << '}';
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"games\":" << summary.games
         << ",\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"censored\":" << summary.censored
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"lowerHalfScore\":" << summary.lower_half_score
         << ",\"lowerHalfMoves\":" << summary.lower_half_moves
         << ",\"decisions\":" << summary.decisions
         << ",\"routedDecisions\":" << summary.routed_decisions
         << ",\"switches\":" << summary.switches
         << ",\"switchRate\":" << summary.switch_rate
         << ",\"passingAlternatives\":"
         << summary.passing_alternatives
         << ",\"survivorRejections\":" << summary.survivor_rejections
         << ",\"clearRejections\":" << summary.clear_rejections
         << ",\"returnRejections\":" << summary.return_rejections
         << ",\"rootQRejections\":" << summary.root_q_rejections
         << ",\"meanSwitchReturnLower95\":"
         << summary.mean_switch_return_lower95
         << ",\"meanSwitchClearAdvantage\":"
         << summary.mean_switch_clear_advantage
         << ",\"meanSwitchRootQLoss\":"
         << summary.mean_switch_root_q_loss
         << ",\"meanSwitchSurvivorAdvantage\":"
         << summary.mean_switch_survivor_advantage
         << ",\"d4Work\":" << summary.d4_work
         << ",\"d4WorkPerMove\":" << summary.d4_work_per_move
         << ",\"peakD4CacheEntries\":"
         << summary.peak_d4_cache_entries
         << ",\"syntheticTransitions\":"
         << summary.synthetic_transitions
         << ",\"d2Calls\":" << summary.continuation.calls
         << ",\"d2Work\":" << summary.continuation.work
         << ",\"d2Nodes\":" << summary.continuation.nodes
         << ",\"d2CacheHits\":" << summary.continuation.cache_hits
         << ",\"d2RootActions\":" << summary.continuation.root_actions
         << ",\"peakD2CacheEntries\":"
         << summary.continuation.peak_cache_entries
         << ",\"d2FullRoot\":"
         << (summary.continuation.full_root ? "true" : "false")
         << ",\"aggregateGameSeconds\":"
         << summary.aggregate_game_seconds
         << ",\"peakRssBytes\":" << summary.peak_rss_bytes << '}';
}

void writeDifference(std::ostream& output, const Difference& value) {
  output << "{\"mean\":" << value.mean << ",\"lower95\":"
         << value.lower95 << ",\"wins\":" << value.wins
         << ",\"ties\":" << value.ties << ",\"losses\":"
         << value.losses << '}';
}

void writeStage(std::ostream& output, const Stage& stage) {
  output << "{\"seedStart\":" << stage.seed_start
         << ",\"maximumMoves\":" << kMaximumMoves
         << ",\"wallSeconds\":" << stage.cohort.wall_seconds
         << ",\"baseline\":";
  writeSummary(output, stage.baseline);
  output << ",\"candidate\":";
  writeSummary(output, stage.candidate);
  output << ",\"paired\":{\"score\":";
  writeDifference(output, stage.comparison.score);
  output << ",\"moves\":";
  writeDifference(output, stage.comparison.moves);
  output << ",\"leaveOneOutPositiveBoth\":"
         << stage.comparison.leave_one_out_positive_both
         << "},\"gate\":{\"scoreImproved\":"
         << (stage.gate.score_improved ? "true" : "false")
         << ",\"movesImproved\":"
         << (stage.gate.moves_improved ? "true" : "false")
         << ",\"clearThroughputImproved\":"
         << (stage.gate.clear_throughput_improved ? "true" : "false")
         << ",\"lowerTailRetained\":"
         << (stage.gate.lower_tail_retained ? "true" : "false")
         << ",\"leaveOneOutOk\":"
         << (stage.gate.leave_one_out_ok ? "true" : "false")
         << ",\"passed\":" << (stage.gate.passed ? "true" : "false")
         << "},\"pairs\":[";
  for (std::size_t index = 0; index < stage.cohort.baseline.size(); ++index) {
    if (index != 0) output << ',';
    output << "{\"baseline\":";
    writeGame(output, stage.cohort.baseline[index]);
    output << ",\"candidate\":";
    writeGame(output, stage.cohort.candidate[index]);
    output << '}';
  }
  output << "]}";
}

void writeFiniteOrNull(std::ostream& output, double value) {
  if (std::isfinite(value)) output << value;
  else output << "null";
}

void writeBoard(std::ostream& output, const Board& board) {
  output << '[';
  for (int index = 0; index < kCellCount; ++index) {
    if (index != 0) output << ',';
    output << static_cast<int>(board[index]);
  }
  output << ']';
}

void writeTeacherRecord(std::ostream& output, std::size_t index,
                        const TeacherRecord& record) {
  const ObservableState& state = record.state;
  const Decision& decision = record.decision;
  const ActionRollout& baseline =
      decision.rollout.actions[decision.d4_action];
  const std::uint32_t tape_seed = seed32(
      publicHash(state) ^ static_cast<std::uint64_t>(kTapeSeedDomain));
  output << std::setprecision(12)
         << "{\"type\":\"routed-decision\",\"index\":" << index
         << ",\"publicState\":{\"board\":";
  writeBoard(output, state.board);
  output << ",\"nextDisc\":" << static_cast<int>(state.next_disc)
         << ",\"movesRemaining\":" << state.moves_remaining
         << ",\"gameOver\":" << (state.game_over ? "true" : "false")
         << ",\"maximumHeight\":" << maximumHeight(state.board)
         << "},\"tapeSeed\":" << tape_seed
         << ",\"stockD4\":{\"action\":" << decision.d4_action
         << ",\"rootQ\":[";
  for (int action = 0; action < kBoardSize; ++action) {
    if (action != 0) output << ',';
    writeFiniteOrNull(output, decision.search.root_values[action]);
  }
  output << "],\"rootExpectedScore\":[";
  for (int action = 0; action < kBoardSize; ++action) {
    if (action != 0) output << ',';
    writeFiniteOrNull(output, decision.search.root_expected_scores[action]);
  }
  output << "],\"work\":" << decision.search.work
         << ",\"nodes\":" << decision.search.nodes
         << ",\"cacheHits\":" << decision.search.cache_hits
         << ",\"cacheEntries\":" << decision.search.cache_entries
         << ",\"seconds\":" << decision.d4_seconds
         << "},\"veto\":{\"action\":" << decision.action
         << ",\"switched\":" << (decision.switched ? "true" : "false")
         << ",\"passingAlternatives\":"
         << decision.passing_alternatives
         << ",\"selectedReturnLower95\":"
         << decision.selected_return_lower95
         << ",\"selectedClearAdvantage\":"
         << decision.selected_clear_advantage
         << ",\"selectedRootQLoss\":"
         << decision.selected_root_q_loss
         << ",\"selectedSurvivorAdvantage\":"
         << decision.selected_survivor_advantage
         << "},\"rollout\":{\"horizon\":" << kRolloutHorizon
         << ",\"scenarioCount\":" << kScenarios
         << ",\"seconds\":" << decision.rollout_seconds
         << ",\"syntheticTransitions\":"
         << decision.rollout.synthetic_transitions
         << ",\"d2Calls\":" << decision.rollout.continuation.calls
         << ",\"d2Work\":" << decision.rollout.continuation.work
         << ",\"d2Nodes\":" << decision.rollout.continuation.nodes
         << ",\"d2CacheHits\":"
         << decision.rollout.continuation.cache_hits
         << ",\"d2RootActions\":"
         << decision.rollout.continuation.root_actions
         << ",\"peakD2CacheEntries\":"
         << decision.rollout.continuation.peak_cache_entries
         << ",\"fullRoot\":"
         << (decision.rollout.full_root ? "true" : "false")
         << ",\"actions\":[";
  for (int action = 0; action < kBoardSize; ++action) {
    if (action != 0) output << ',';
    if (!decision.rollout.legal[action]) {
      output << "null";
      continue;
    }
    const ActionRollout& candidate = decision.rollout.actions[action];
    output << "{\"action\":" << action << ",\"meanReturn\":"
           << candidate.mean_value << ",\"survivingScenarios\":"
           << candidate.surviving_scenarios
           << ",\"meanNumberedClears\":"
           << candidate.mean_numbered_clears << ",\"comparisonToD4\":";
    if (action == decision.d4_action) {
      output << "null";
    } else {
      const AlternativeTest test = testAlternative(
          candidate, baseline, decision.search.root_values[action],
          decision.search.root_values[decision.d4_action]);
      output << "{\"returnLower95\":" << test.return_lower95
             << ",\"survivorAdvantage\":" << test.survivor_advantage
             << ",\"clearAdvantage\":" << test.clear_advantage
             << ",\"rootQLoss\":" << test.root_q_loss
             << ",\"survivorsOk\":"
             << (test.survivors_ok ? "true" : "false")
             << ",\"clearsOk\":" << (test.clears_ok ? "true" : "false")
             << ",\"returnOk\":" << (test.return_ok ? "true" : "false")
             << ",\"rootQOk\":" << (test.root_q_ok ? "true" : "false")
             << ",\"passed\":" << (test.passed ? "true" : "false")
             << '}';
    }
    output << ",\"scenarios\":[";
    for (int scenario = 0; scenario < kScenarios; ++scenario) {
      if (scenario != 0) output << ',';
      const ScenarioOutcome& outcome = candidate.scenarios[scenario];
      output << "{\"scenario\":" << scenario
             << ",\"return\":" << outcome.value
             << ",\"numberedClears\":" << outcome.numbered_clears
             << ",\"survivedHorizon\":"
             << (outcome.survived_horizon ? "true" : "false") << '}';
    }
    output << "]}";
  }
  output << "]}}\n";
}

int runTeacherReplay(const Options& options, std::ostream& report) {
  constexpr std::uint32_t kPilotSeed = kFittingSeedStart;
  constexpr std::int64_t kExpectedScore = 404'047;
  constexpr int kExpectedMoves = 250;
  constexpr std::uint64_t kExpectedRouted = 179;
  constexpr std::uint64_t kExpectedSwitches = 12;
  std::vector<TeacherRecord> records;
  const GameResult game =
      runGame(kPilotSeed, true, "teacher-replay", &records);
  if (game.score != kExpectedScore || game.moves != kExpectedMoves ||
      game.routed_decisions != kExpectedRouted ||
      game.switches != kExpectedSwitches ||
      records.size() != kExpectedRouted) {
    throw std::runtime_error("teacher replay did not match frozen pilot");
  }
  std::ofstream output(options.teacher_output);
  if (!output) throw std::runtime_error("could not open teacher output");
  output << std::setprecision(12)
         << "{\"type\":\"metadata\",\"experiment\":\"d4-d2-rollout-veto\""
         << ",\"instrumentationReplay\":true,\"formalInference\":false"
         << ",\"gameSeed\":" << kPilotSeed
         << ",\"uniqueGameSeedAlreadyRead\":true"
         << ",\"policyFrozenBeforeOriginalPilot\":true"
         << ",\"horizon\":" << kRolloutHorizon
         << ",\"scenarios\":" << kScenarios
         << ",\"records\":" << records.size() << "}\n";
  double d4_seconds = 0.0;
  double rollout_seconds = 0.0;
  for (std::size_t index = 0; index < records.size(); ++index) {
    writeTeacherRecord(output, index, records[index]);
    d4_seconds += records[index].decision.d4_seconds;
    rollout_seconds += records[index].decision.rollout_seconds;
  }
  output << "{\"type\":\"summary\",\"score\":" << game.score
         << ",\"moves\":" << game.moves
         << ",\"routedDecisions\":" << game.routed_decisions
         << ",\"switches\":" << game.switches
         << ",\"routedD4Seconds\":" << d4_seconds
         << ",\"rolloutSeconds\":" << rollout_seconds
         << ",\"wholeGameSeconds\":" << game.elapsed_seconds
         << ",\"syntheticTransitions\":" << game.synthetic_transitions
         << ",\"d2Calls\":" << game.continuation.calls
         << ",\"d2Work\":" << game.continuation.work << "}\n";
  output.close();
  report << std::fixed << std::setprecision(6)
         << "D4_D2_ROLLOUT_VETO_TEACHER {\"records\":" << records.size()
         << ",\"score\":" << game.score << ",\"moves\":" << game.moves
         << ",\"switches\":" << game.switches
         << ",\"routedD4Seconds\":" << d4_seconds
         << ",\"rolloutSeconds\":" << rollout_seconds
         << ",\"wholeGameSeconds\":" << game.elapsed_seconds
         << ",\"output\":\"" << options.teacher_output << "\"}\n";
  return 0;
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

ObservableState asymmetricFixture() {
  ObservableState state;
  state.board.fill(kEmpty);
  state.board[indexOf(6, 0)] = kSolid;
  state.board[indexOf(6, 1)] = 4;
  state.board[indexOf(6, 2)] = 2;
  state.board[indexOf(5, 2)] = kCracked;
  state.board[indexOf(6, 4)] = 6;
  state.next_disc = 3;
  state.moves_remaining = 3;
  return state;
}

bool selfTest(std::ostream& output) {
  const bool d4_test = d4::selfTest(output);
  const ObservableState fixture = asymmetricFixture();
  D2Metrics d2_first_metrics;
  D2Metrics d2_repeat_metrics;
  const int d2_first = fairDepthTwoAction(fixture, &d2_first_metrics);
  const int d2_repeat = fairDepthTwoAction(fixture, &d2_repeat_metrics);
  D2Metrics d2_mirror_metrics;
  const int d2_mirror =
      fairDepthTwoAction(mirror(fixture), &d2_mirror_metrics);
  State metadata_state = materialize(fixture);
  metadata_state.score = 9'999'999;
  metadata_state.level = 83;
  metadata_state.moves_played = 777;
  const ObservableState metadata_observable = observable(metadata_state);
  D2Metrics d2_metadata_metrics;
  const int d2_metadata =
      fairDepthTwoAction(metadata_observable, &d2_metadata_metrics);

  const RolloutEvaluation rollout = evaluateRollouts(fixture, 3);
  const RolloutEvaluation repeat = evaluateRollouts(fixture, 3);
  const RolloutEvaluation reflected = evaluateRollouts(mirror(fixture), 3);
  bool rollout_reflection = true;
  for (int column = 0; column < kBoardSize; ++column) {
    rollout_reflection =
        rollout_reflection &&
        reflected.legal[kBoardSize - 1 - column] == rollout.legal[column] &&
        reflected.actions[kBoardSize - 1 - column].mean_value ==
            rollout.actions[column].mean_value &&
        reflected.actions[kBoardSize - 1 - column].scenarios ==
            rollout.actions[column].scenarios;
  }
  ObservableState terminal_fixture;
  terminal_fixture.board.fill(kSolid);
  terminal_fixture.board[indexOf(0, 3)] = kEmpty;
  terminal_fixture.next_disc = 2;
  terminal_fixture.moves_remaining = 1;
  const RolloutEvaluation fixed_horizon_terminal =
      evaluateRollouts(terminal_fixture, kRolloutHorizon);
  bool terminal_semantics =
      fixed_horizon_terminal.legal_actions == 1 &&
      fixed_horizon_terminal.synthetic_transitions == kScenarios &&
      fixed_horizon_terminal.continuation.calls == 0;
  for (const ScenarioOutcome& outcome :
       fixed_horizon_terminal.actions[3].scenarios) {
    terminal_semantics = terminal_semantics &&
                         outcome.value == fair::kTerminalUtility &&
                         !outcome.survived_horizon;
  }

  State safe_state = materialize(fixture);
  safe_state.board = initialBoard();
  const Decision safe = chooseAction(safe_state);
  const d4::SearchDecision safe_d4 = d4::chooseDepth4Action(safe_state);
  State routed_state = materialize(fixture);
  for (int row = 3; row < kBoardSize; ++row) {
    routed_state.board[indexOf(row, 0)] = kSolid;
  }
  routed_state.board[indexOf(2, 0)] = kEmpty;
  const Decision disabled = chooseAction(routed_state, false);

  std::array<int, kBoardSize> visible_counts{};
  constexpr std::uint32_t tape_seed = 0x1234'5678u;
  for (int scenario = 0; scenario < kScenarios; ++scenario) {
    ++visible_counts[visibleDisc(tape_seed, scenario, 0) - 1];
  }
  bool exact_stratification = true;
  for (const int count : visible_counts) exact_stratification &= count == 1;
  std::array<int, kBoardSize> reveal_counts{};
  for (int scenario = 0; scenario < kScenarios; ++scenario) {
    RevealTape tape{tape_seed, scenario, 0, kRevealTapeDomain, 0};
    ++reveal_counts[tape.nextDisc() - 1];
  }
  for (const int count : reveal_counts) exact_stratification &= count == 1;
  RevealTape first_tape{tape_seed, 2, 3, kRevealTapeDomain, 0};
  RevealTape second_tape{tape_seed, 2, 3, kRevealTapeDomain, 0};
  bool deterministic_tape = true;
  for (int event = 0; event < 16; ++event) {
    deterministic_tape &= first_tape.nextDisc() == second_tape.nextDisc();
  }

  ObservableState reveal_fixture;
  reveal_fixture.board.fill(kEmpty);
  reveal_fixture.board[indexOf(6, 1)] = kCracked;
  reveal_fixture.next_disc = 1;
  reveal_fixture.moves_remaining = 4;
  MoveResult standard;
  expect(playSyntheticMove(reveal_fixture, 0, tape_seed, 0, 0, standard),
         "domain fixture transition failed");
  bool found_visible_change = false;
  bool found_reveal_change = false;
  for (std::uint32_t salt = 1; salt < 256; ++salt) {
    MoveResult changed_visible;
    expect(playSyntheticMove(
               reveal_fixture, 0, tape_seed, 0, 0, changed_visible, nullptr,
               {kRevealTapeDomain, kVisibleTapeDomain ^ salt}),
           "visible-domain transition failed");
    if (changed_visible.state.next_disc != standard.state.next_disc) {
      expect(changed_visible.state.board == standard.state.board &&
                 changed_visible.score_delta == standard.score_delta &&
                 changed_visible.waves.size() == standard.waves.size(),
             "visible domain leaked into reveal mechanics");
      found_visible_change = true;
    }
    MoveResult changed_reveal;
    expect(playSyntheticMove(
               reveal_fixture, 0, tape_seed, 0, 0, changed_reveal, nullptr,
               {kRevealTapeDomain ^ salt, kVisibleTapeDomain}),
           "reveal-domain transition failed");
    if (changed_reveal.state.board != standard.state.board) {
      expect(changed_reveal.state.next_disc == standard.state.next_disc,
             "reveal domain leaked into visible tape");
      found_reveal_change = true;
    }
  }

  ActionRollout baseline;
  ActionRollout challenger;
  for (int scenario = 0; scenario < kScenarios; ++scenario) {
    baseline.scenarios[scenario].value = 100.0;
    challenger.scenarios[scenario].value = 110.0;
    baseline.scenarios[scenario].numbered_clears = 10;
    challenger.scenarios[scenario].numbered_clears = 10;
    baseline.scenarios[scenario].survived_horizon = true;
    challenger.scenarios[scenario].survived_horizon = true;
  }
  baseline.mean_value = 100.0;
  challenger.mean_value = 110.0;
  baseline.mean_numbered_clears = 10.0;
  challenger.mean_numbered_clears = 10.0;
  baseline.surviving_scenarios = kScenarios;
  challenger.surviving_scenarios = kScenarios;
  const AlternativeTest gate_pass =
      testAlternative(challenger, baseline, 3'000.0, 10'000.0);
  const AlternativeTest q_fail =
      testAlternative(challenger, baseline, 2'999.0, 10'000.0);
  ActionRollout survivor_bad = challenger;
  survivor_bad.surviving_scenarios = kScenarios - 1;
  const AlternativeTest survivor_fail =
      testAlternative(survivor_bad, baseline, 3'000.0, 10'000.0);
  ActionRollout clear_bad = challenger;
  clear_bad.mean_numbered_clears = 9.99;
  const AlternativeTest clear_fail =
      testAlternative(clear_bad, baseline, 3'000.0, 10'000.0);
  ActionRollout return_bad = challenger;
  for (auto& scenario : return_bad.scenarios) scenario.value = 100.0;
  const AlternativeTest return_fail =
      testAlternative(return_bad, baseline, 3'000.0, 10'000.0);

  const bool d2_deterministic =
      d2_first == d2_repeat &&
      d2_first_metrics.work == d2_repeat_metrics.work &&
      d2_first_metrics.root_actions == d2_repeat_metrics.root_actions;
  const bool d2_reflection =
      d2_mirror == kBoardSize - 1 - d2_first &&
      d2_mirror_metrics.work == d2_first_metrics.work;
  const bool metadata_blind = sameObservable(fixture, metadata_observable) &&
                              d2_metadata == d2_first &&
                              d2_metadata_metrics.work ==
                                  d2_first_metrics.work;
  const bool continuation_public_api =
      std::is_same_v<decltype(&fairDepthTwoAction), ContinuationFunction>;
  const bool d2_bounded_full_root =
      d2_first_metrics.calls == 1 && d2_first_metrics.full_root &&
      d2_first_metrics.work <= kWorstD2Work &&
      d2_first_metrics.peak_cache_entries <= kWorstD2CacheEntries;
  const bool rollout_deterministic =
      rollout.actions == repeat.actions && rollout.legal == repeat.legal &&
      rollout.synthetic_transitions == repeat.synthetic_transitions &&
      rollout.continuation.work == repeat.continuation.work;
  const bool rollout_bounded_full_root =
      rollout.full_root &&
      rollout.synthetic_transitions <=
          kWorstSyntheticTransitionsPerRoutedDecision &&
      rollout.continuation.work <= kWorstD2WorkPerRoutedDecision &&
      rollout.continuation.peak_cache_entries <= kWorstD2CacheEntries;
  const bool routing_and_parity =
      !safe.danger && !safe.routed && safe.action == safe_d4.action &&
      safe.rollout.synthetic_transitions == 0 && disabled.danger &&
      !disabled.routed && !disabled.switched &&
      disabled.action == disabled.d4_action &&
      disabled.rollout.synthetic_transitions == 0;
  const bool domains_separate = found_visible_change && found_reveal_change;
  const bool gates = gate_pass.passed &&
                     std::abs(gate_pass.root_q_loss - kMaximumRootQLoss) <
                         1.0e-9 &&
                     !q_fail.passed && !q_fail.root_q_ok &&
                     !survivor_fail.passed && !survivor_fail.survivors_ok &&
                     !clear_fail.passed && !clear_fail.clears_ok &&
                     !return_fail.passed && !return_fail.return_ok;
  const bool protocol =
      kFittingSeedStart == 0x3ded'0000u &&
      kHeldoutSeedStart == 0x3dee'0000u &&
      kScreenSeedStart == 0x3ebb'0000u &&
      kConfirmationSeedStart == 0x3ebc'0000u && kMaximumMoves == 1'000 &&
      kRolloutHorizon == 25 && kScenarios == 7;
  const bool passed =
      d4_test && d2_deterministic && d2_reflection && metadata_blind &&
      continuation_public_api && d2_bounded_full_root &&
      rollout_deterministic && rollout_reflection &&
      rollout_bounded_full_root && terminal_semantics && routing_and_parity &&
      exact_stratification && deterministic_tape && domains_separate &&
      gates && protocol;
  output << std::setprecision(12)
         << "D4_D2_ROLLOUT_VETO_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"d4Dependency\":" << (d4_test ? "true" : "false")
         << ",\"d2Deterministic\":"
         << (d2_deterministic ? "true" : "false")
         << ",\"d2ReflectionSafe\":"
         << (d2_reflection ? "true" : "false")
         << ",\"metadataBlind\":"
         << (metadata_blind ? "true" : "false")
         << ",\"continuationApiPublicOnly\":"
         << (continuation_public_api ? "true" : "false")
         << ",\"d2FullRootBounded\":"
         << (d2_bounded_full_root ? "true" : "false")
         << ",\"rolloutDeterministic\":"
         << (rollout_deterministic ? "true" : "false")
         << ",\"rolloutReflectionSafe\":"
         << (rollout_reflection ? "true" : "false")
         << ",\"rolloutFullRootBounded\":"
         << (rollout_bounded_full_root ? "true" : "false")
         << ",\"fixedHorizonTerminalSemantics\":"
         << (terminal_semantics ? "true" : "false")
         << ",\"routeAndZeroSwitchParity\":"
         << (routing_and_parity ? "true" : "false")
         << ",\"exactScenarioStratification\":"
         << (exact_stratification ? "true" : "false")
         << ",\"deterministicTapes\":"
         << (deterministic_tape ? "true" : "false")
         << ",\"revealVisibleDomainsSeparate\":"
         << (domains_separate ? "true" : "false")
         << ",\"gateWiring\":" << (gates ? "true" : "false")
         << ",\"worstD2Work\":" << kWorstD2Work
         << ",\"worstD2Cache\":" << kWorstD2CacheEntries
         << ",\"worstRoutedD2Work\":"
         << kWorstD2WorkPerRoutedDecision
         << ",\"worstRoutedSyntheticTransitions\":"
         << kWorstSyntheticTransitionsPerRoutedDecision
         << "}\n";
  return passed;
}

void writePausedArtifact(const Options& options, const Cohort& pilot,
                         double projected_wall, bool runtime_stop,
                         bool no_switch_stop) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open paused artifact");
  const Stage stage = makeStage(kFittingSeedStart, pilot, false);
  output << std::setprecision(12)
         << "{\"experiment\":\"d4-d2-rollout-veto\""
         << ",\"pilotPairOnly\":true,\"formalInference\":false"
         << ",\"conclusion\":\"paused-by-preregistered-diagnostic-gate\""
         << ",\"runtimeStop\":" << (runtime_stop ? "true" : "false")
         << ",\"noSwitchStop\":" << (no_switch_stop ? "true" : "false")
         << ",\"readGameSeeds\":[\"0x3ded0000\"]"
         << ",\"untouchedRanges\":[\"0x3ded0001...003 fitting remainder\",\"0x3dee0000...007 heldout\",\"0x3ebb0000...007 screen\",\"0x3ebc0000...00f confirmation\",\"0x7d... protected\",\"0xd7... protected\"]"
         << ",\"firstPairWallSeconds\":" << pilot.wall_seconds
         << ",\"projectionWaves\":" << kFullProtocolProjectionWaves
         << ",\"projectedFullProtocolWallSeconds\":" << projected_wall
         << ",\"limitSeconds\":" << kMaximumProjectedWallSeconds
         << ",\"pilot\":";
  writeStage(output, stage);
  output << "}\n";
}

int run(const Options& options, std::ostream& report) {
  const auto all_started = std::chrono::steady_clock::now();
  Cohort fitting = runCohort(kFittingSeedStart, 1, "fit-first");
  const double first_pair_wall = fitting.wall_seconds;
  const double projected_wall =
      first_pair_wall * kFullProtocolProjectionWaves;
  const bool runtime_stop =
      projected_wall > kMaximumProjectedWallSeconds;
  const bool no_switch_stop =
      fitting.candidate[0].routed_decisions >= 20 &&
      fitting.candidate[0].switches == 0;
  {
    const std::lock_guard<std::mutex> lock(progress_mutex);
    std::cerr << "first paired wall " << first_pair_wall
              << " seconds; projected complete protocol " << projected_wall
              << " seconds; routed "
              << fitting.candidate[0].routed_decisions << ", switches "
              << fitting.candidate[0].switches << '\n';
  }
  if (runtime_stop || no_switch_stop) {
    writePausedArtifact(options, fitting, projected_wall, runtime_stop,
                        no_switch_stop);
    report << std::fixed << std::setprecision(6)
           << "D4_D2_ROLLOUT_VETO_PAUSED {\"firstPairWallSeconds\":"
           << first_pair_wall
           << ",\"projectedFullProtocolWallSeconds\":" << projected_wall
           << ",\"runtimeStop\":" << (runtime_stop ? "true" : "false")
           << ",\"noSwitchStop\":"
           << (no_switch_stop ? "true" : "false")
           << ",\"routedDecisions\":"
           << fitting.candidate[0].routed_decisions
           << ",\"switches\":" << fitting.candidate[0].switches
           << ",\"artifact\":\"" << options.output << "\"}\n";
    return 3;
  }

  appendCohort(fitting, runCohort(kFittingSeedStart + 1,
                                  kFittingGames - 1, "fitting"));
  Stage fitting_stage = makeStage(kFittingSeedStart, std::move(fitting), true);
  std::optional<Stage> heldout;
  std::optional<Stage> screen;
  std::optional<Stage> confirmation;
  if (fitting_stage.gate.passed) {
    heldout = makeStage(
        kHeldoutSeedStart,
        runCohort(kHeldoutSeedStart, kHeldoutGames, "heldout"), false);
  }
  if (heldout && heldout->gate.passed) {
    screen = makeStage(kScreenSeedStart,
                       runCohort(kScreenSeedStart, kScreenGames, "screen"),
                       false);
  }
  if (screen && screen->gate.passed) {
    confirmation = makeStage(
        kConfirmationSeedStart,
        runCohort(kConfirmationSeedStart, kConfirmationGames,
                  "confirmation"),
        false);
  }
  const bool qualified = confirmation && confirmation->gate.passed;
  const double total_wall = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - all_started)
                                .count();
  std::ofstream artifact(options.output);
  if (!artifact) throw std::runtime_error("could not open rollout artifact");
  artifact << std::setprecision(12)
           << "{\n  \"experiment\":\"d4-d2-rollout-veto\",\n"
           << "  \"preregistered\":true,\n"
           << "  \"publicStateOnly\":true,\n"
           << "  \"policy\":{\"baseline\":\"qualified-full-width-fair-d4-s5\""
           << ",\"route\":\"maxHeight>=4 in every rise phase\""
           << ",\"horizon\":" << kRolloutHorizon
           << ",\"scenarios\":" << kScenarios
           << ",\"continuation\":\"completed-full-width-fair-d2-s5\""
           << ",\"terminalUtility\":" << fair::kTerminalUtility
           << ",\"tail\":\"one unscaled fair leaf at nonterminal horizon\""
           << ",\"pairedT975Df6\":" << kPairedT975Df6
           << ",\"maximumD4RootQLoss\":" << kMaximumRootQLoss
           << ",\"maximumMoves\":" << kMaximumMoves << "},\n"
           << "  \"tapes\":{\"canonicalPublicStateSeeded\":true"
           << ",\"alignedAcrossRootActions\":true"
           << ",\"everyEventExactlyStratifiedAcrossSeven\":true"
           << ",\"revealVisibleDomainsSeparate\":true"
           << ",\"continuationCannotReceiveTapeMetadata\":true},\n"
           << "  \"resourceProof\":{\"worstD2Work\":" << kWorstD2Work
           << ",\"worstD2CacheEntries\":" << kWorstD2CacheEntries
           << ",\"worstD2CallsPerRoutedDecision\":"
           << kWorstD2CallsPerRoutedDecision
           << ",\"worstD2WorkPerRoutedDecision\":"
           << kWorstD2WorkPerRoutedDecision
           << ",\"worstSyntheticTransitionsPerRoutedDecision\":"
           << kWorstSyntheticTransitionsPerRoutedDecision
           << ",\"maximumRssBytes\":" << kMaximumRssBytes << "},\n"
           << "  \"gate\":{\"strictScoreAndMovesAndClearThroughput\":true"
           << ",\"lowerHalfRetention\":" << kLowerTailRetention
           << ",\"fittingLeaveOneOutPositiveBothMinimum\":3},\n"
           << "  \"runtimeProjection\":{\"firstPairSeconds\":"
           << first_pair_wall
           << ",\"fullProtocolWaves\":" << kFullProtocolProjectionWaves
           << ",\"projectedSecondsFromFirstPair\":" << projected_wall
           << ",\"limitSeconds\":" << kMaximumProjectedWallSeconds
           << ",\"paused\":false},\n  \"fitting\":";
  writeStage(artifact, fitting_stage);
  artifact << ",\n  \"heldout\":";
  if (heldout) writeStage(artifact, *heldout); else artifact << "null";
  artifact << ",\n  \"screen\":";
  if (screen) writeStage(artifact, *screen); else artifact << "null";
  artifact << ",\n  \"confirmation\":";
  if (confirmation) writeStage(artifact, *confirmation);
  else artifact << "null";
  artifact << ",\n  \"qualified\":" << (qualified ? "true" : "false")
           << ",\n  \"protectedRangesRead\":false"
           << ",\n  \"totalWallSeconds\":" << total_wall
           << ",\n  \"peakRssBytes\":" << d4::peakRssBytes() << "\n}\n";
  artifact.close();

  report << std::fixed << std::setprecision(6)
         << "D4_D2_ROLLOUT_VETO_RESULT {\"fitScoreDelta\":"
         << fitting_stage.comparison.score.mean
         << ",\"fitMoveDelta\":" << fitting_stage.comparison.moves.mean
         << ",\"fitSwitches\":" << fitting_stage.candidate.switches
         << ",\"fitPassed\":"
         << (fitting_stage.gate.passed ? "true" : "false")
         << ",\"heldoutRan\":" << (heldout ? "true" : "false")
         << ",\"heldoutPassed\":"
         << (heldout && heldout->gate.passed ? "true" : "false")
         << ",\"screenRan\":" << (screen ? "true" : "false")
         << ",\"screenPassed\":"
         << (screen && screen->gate.passed ? "true" : "false")
         << ",\"confirmationRan\":"
         << (confirmation ? "true" : "false")
         << ",\"qualified\":" << (qualified ? "true" : "false")
         << ",\"totalWallSeconds\":" << total_wall
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return 0;
}

}  // namespace drop7::d4_d2_rollout_veto

#ifndef DROP7_D4_D2_ROLLOUT_VETO_LIBRARY
int main(int argc, char** argv) {
  try {
    const auto options =
        drop7::d4_d2_rollout_veto::parseOptions(argc, argv, 2);
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::d4_d2_rollout_veto::selfTest(std::cout) ? EXIT_SUCCESS
                                                            : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      return drop7::d4_d2_rollout_veto::run(options, std::cout);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--trace-pilot") {
      return drop7::d4_d2_rollout_veto::runTeacherReplay(options, std::cout);
    }
    std::cerr << "usage: drop7_d4_d2_rollout_veto --self-test | --run | "
                 "--trace-pilot [--output PATH] [--teacher-output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
#endif
