#define DROP7_FULL_PANEL_CPI_PREFLIGHT_LIBRARY
#include "../deployment-panel/full-panel-cpi-preflight.cpp"
#undef DROP7_FULL_PANEL_CPI_PREFLIGHT_LIBRARY

#include <cstdio>
#include <filesystem>
#include <future>
#include <optional>
#include <type_traits>

// Public Regenerative Policy Iteration, B0.
//
// This executable is deliberately split into three capabilities:
//
//   --self-test  uses constructed public fixtures only;
//   --preflight  measures one constructed public root and writes a
//                source-bound admission artifact without reading the corpus;
//   --run        can read only the checksum-locked, development-only 477-root
//                public H200 corpus, and only after validating that artifact.
//
// No entrypoint can start or replay a gameplay seed.  Origin-game provenance
// is consumed only by the run coordinator when aggregating eight whole-origin
// folds.  The deployed policy boundary below accepts PublicState and cannot
// carry an origin seed, move index, score, level, history, scenario, or tape.
namespace drop7::public_regenerative_policy_iteration_b0 {

namespace frozen = drop7::full_panel_cpi_preflight;
namespace d4 = drop7::fair_only_depth4;
namespace fair = drop7::fair_only_horizon;
namespace detail = drop7::cfpi::detail;
using Clock = std::chrono::steady_clock;
using PublicState = frozen::PublicState;

constexpr std::string_view kExpectedCorpusSha256 =
    frozen::kExpectedCorpusSha256;
constexpr int kExpectedRecords = frozen::kExpectedRecords;
constexpr int kExpectedOrigins = frozen::kExpectedGames;

// Frozen planner schedule.  A1 is a nested seven-scenario subset of A2.
constexpr int kA1Scenarios = 7;
constexpr int kA2Scenarios = 21;
constexpr int kBScenarios = 21;
constexpr int kCScenarios = 35;
constexpr int kA1Horizon = 25;
constexpr int kA2Horizon = 50;
constexpr int kBHorizon = 75;
constexpr int kCHorizon = 75;
constexpr int kRetainedChallengers = 3;
constexpr int kContinuationDepth = 2;
constexpr int kEventsPerStep = 64;
constexpr int kMaximumThreads = 4;
constexpr double kTerminalUtility = -1'000'000.0;

// Panel identity and event identity are independent.  All root siblings in a
// panel share the same canonical-public-state root seed.  Scenario identity is
// available only to the transition sampler, never to chooseFairD2().
constexpr std::uint32_t kPanelAMasterDomain = 0x5052'4130u;  // "PRA0"
constexpr std::uint32_t kPanelBMasterDomain = 0x5052'4230u;  // "PRB0"
constexpr std::uint32_t kPanelCMasterDomain = 0x5052'4330u;  // "PRC0"
constexpr std::uint32_t kRevealDomain = 0x5052'5256u;        // "PRRV"
constexpr std::uint32_t kVisibleDomain = 0x5052'5653u;       // "PRVS"

// Frozen B0 admission gate.
constexpr double kStabilityMinimum = 0.70;
constexpr double kOverrideCoverageMinimum = 0.05;
constexpr double kOverridePrecisionMinimum = 0.75;
constexpr double kRawScoreRatioMinimum = 1.10;
constexpr double kRmstRatioMinimum = 1.05;
constexpr int kRequiredNonregressingOrigins = 6;
constexpr double kPairedT95Df20 = 1.724718;
constexpr double kOriginT95Df7 = 1.894579;
constexpr double kComparisonTolerance = 1.0e-9;

// Frozen resources.  The work proof assumes the maximum seven legal actions,
// four retained A2 actions (three challengers plus D4), and two actions in B/C.
constexpr std::uint64_t power(std::uint64_t base, int exponent) {
  std::uint64_t result = 1;
  for (int count = 0; count < exponent; ++count) result *= base;
  return result;
}
constexpr std::uint64_t kMaximumD2Work =
    kBoardSize * fair::kChanceSamples +
    2u * power(kBoardSize * fair::kChanceSamples, 2);
constexpr std::uint64_t kMaximumD2CacheEntries =
    kBoardSize * fair::kChanceSamples;
constexpr std::uint64_t kMaximumTransitionsPerRoot =
    static_cast<std::uint64_t>(kBoardSize) * kA1Scenarios * kA1Horizon +
    static_cast<std::uint64_t>(kRetainedChallengers + 1) *
        (kA1Scenarios * (kA2Horizon - kA1Horizon) +
         (kA2Scenarios - kA1Scenarios) * kA2Horizon) +
    2ull * kBScenarios * kBHorizon +
    2ull * kCScenarios * kCHorizon;
constexpr std::uint64_t kMaximumD2CallsPerRoot =
    static_cast<std::uint64_t>(kBoardSize) * kA1Scenarios *
        (kA1Horizon - 1) +
    static_cast<std::uint64_t>(kRetainedChallengers + 1) *
        (kA1Scenarios * (kA2Horizon - kA1Horizon) +
         (kA2Scenarios - kA1Scenarios) * (kA2Horizon - 1)) +
    2ull * kBScenarios * (kBHorizon - 1) +
    2ull * kCScenarios * (kCHorizon - 1);
constexpr std::uint64_t kMaximumSyntheticTransitions =
    kExpectedRecords * kMaximumTransitionsPerRoot;
constexpr std::uint64_t kMaximumD2Calls =
    kExpectedRecords * kMaximumD2CallsPerRoot;
constexpr std::uint64_t kMaximumD2LogicalWork =
    kMaximumD2Calls * kMaximumD2Work;
constexpr std::uint64_t kMaximumD4LogicalWork =
    static_cast<std::uint64_t>(kExpectedRecords) * d4::kMaximumWork;
constexpr std::uint64_t kMaximumLogicalWork =
    kMaximumD2LogicalWork + kMaximumD4LogicalWork;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr double kWallLimitSeconds = 75.0 * 60.0;
constexpr double kProjectionSafetyFactor = 1.25;
constexpr double kProjectionReserveSeconds = 30.0;
constexpr std::uint64_t kCheckpointByteLimit = 16ull * 1024ull * 1024ull;
constexpr std::uint64_t kSourceByteLimit = 4ull * 1024ull * 1024ull;

static_assert(kLevelBonus == 17'000 && kMovesPerLevel == 5);
static_assert(fair::kChanceSamples == 5);
static_assert(fair::kTerminalUtility == kTerminalUtility);
static_assert(kMaximumD2Work == 2'485);
static_assert(kMaximumD2CacheEntries == 35);
static_assert(kMaximumTransitionsPerRoot == 13'125);
static_assert(kMaximumD2CallsPerRoot == 12'908);
static_assert(kMaximumSyntheticTransitions == 6'260'625);
static_assert(kMaximumD2Calls == 6'157'116);
static_assert(kMaximumD2LogicalWork == 15'300'433'260ull);
static_assert(kMaximumLogicalWork == 16'826'833'260ull);
static_assert(kA2Scenarios % kBoardSize == 0 &&
              kBScenarios % kBoardSize == 0 &&
              kCScenarios % kBoardSize == 0);
static_assert(kEventsPerStep > kCellCount);

std::uint64_t configFingerprint() {
  std::uint64_t value = 0x5052'5049'4230'0001ull;
  for (const std::uint64_t item : std::array<std::uint64_t, 27>{{
           kA1Scenarios, kA2Scenarios, kBScenarios, kCScenarios,
           kA1Horizon, kA2Horizon, kBHorizon, kCHorizon,
           kRetainedChallengers, kContinuationDepth, kEventsPerStep,
           kPanelAMasterDomain, kPanelBMasterDomain, kPanelCMasterDomain,
           kRevealDomain, kVisibleDomain,
           std::bit_cast<std::uint64_t>(kTerminalUtility),
           std::bit_cast<std::uint64_t>(kStabilityMinimum),
           std::bit_cast<std::uint64_t>(kOverrideCoverageMinimum),
           std::bit_cast<std::uint64_t>(kOverridePrecisionMinimum),
           std::bit_cast<std::uint64_t>(kRawScoreRatioMinimum),
           std::bit_cast<std::uint64_t>(kRmstRatioMinimum),
           kRequiredNonregressingOrigins,
           kMaximumTransitionsPerRoot, kMaximumD2CallsPerRoot,
           kMaximumLogicalWork, kRssLimitBytes,
       }}) {
    value = frozen::mix64(value ^ frozen::mix64(item));
  }
  return value;
}

struct Deadline {
  Clock::time_point started = Clock::now();

  double seconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }

  void check() const {
    if (seconds() > kWallLimitSeconds) {
      throw std::runtime_error("PRPI B0 exceeded 75 minute wall limit");
    }
    if (frozen::peakRssBytes() > kRssLimitBytes) {
      throw std::runtime_error("PRPI B0 exceeded 256 MiB RSS limit");
    }
  }
};

struct D2Metrics {
  std::uint64_t calls = 0;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::uint64_t root_actions = 0;
  std::size_t peak_cache_entries = 0;
  bool full_root = true;

  D2Metrics& operator+=(const D2Metrics& other) {
    calls += other.calls;
    work += other.work;
    nodes += other.nodes;
    cache_hits += other.cache_hits;
    root_actions += other.root_actions;
    peak_cache_entries =
        std::max(peak_cache_entries, other.peak_cache_entries);
    full_root = full_root && other.full_root;
    return *this;
  }

  bool operator==(const D2Metrics&) const = default;
};

struct D2Decision {
  int action = -1;
  std::array<double, kBoardSize> values{};
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::size_t cache_entries = 0;
  int evaluated_actions = 0;

  bool operator==(const D2Decision&) const = default;
};

// This is the complete future-policy boundary.  Scenario/tape metadata has no
// representation in its parameter type.
D2Decision chooseFairD2(const PublicState& source) {
  D2Decision result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  if (source.terminal) return result;
  bool mirrored = false;
  const PublicState canonical = frozen::canonicalPublic(source, mirrored);
  fair::SearchContext context;
  const fair::RootEvaluation root = fair::rootDecision(
      frozen::materialize(canonical), kContinuationDepth, context);
  int legal = 0;
  int evaluated = 0;
  for (int action = 0; action < kBoardSize; ++action) {
    legal += isLegal(canonical.board, action);
    evaluated += std::isfinite(root.values[action]);
  }
  if (root.action < 0 || legal != evaluated || context.work > kMaximumD2Work ||
      context.cache.size() > kMaximumD2CacheEntries) {
    throw std::runtime_error("exact public D2 did not complete full width");
  }
  result.action = mirrored ? kBoardSize - 1 - root.action : root.action;
  for (int canonical_action = 0; canonical_action < kBoardSize;
       ++canonical_action) {
    const int source_action =
        mirrored ? kBoardSize - 1 - canonical_action : canonical_action;
    result.values[source_action] = root.values[canonical_action];
  }
  result.work = context.work;
  result.nodes = context.nodes;
  result.cache_hits = context.cache_hits;
  result.cache_entries = context.cache.size();
  result.evaluated_actions = evaluated;
  return result;
}

using PublicD2Boundary = D2Decision (*)(const PublicState&);
static_assert(std::is_same_v<decltype(&chooseFairD2), PublicD2Boundary>);
static_assert(!std::is_invocable_v<PublicD2Boundary,
                                   const frozen::PanelRecord&>);

enum class Panel : std::uint8_t { kA, kB, kC };

constexpr std::uint32_t panelMasterDomain(Panel panel) {
  switch (panel) {
    case Panel::kA:
      return kPanelAMasterDomain;
    case Panel::kB:
      return kPanelBMasterDomain;
    case Panel::kC:
      return kPanelCMasterDomain;
  }
  throw std::invalid_argument("invalid panel");
}

std::uint32_t panelRootSeed(const PublicState& source, Panel panel) {
  return frozen::seed32(frozen::publicHash(source) ^
                        static_cast<std::uint64_t>(panelMasterDomain(panel)));
}

constexpr std::array<int, kA1Scenarios> kA1ScenarioIds{{
    1, 4, 7, 10, 13, 16, 19,
}};

bool isA1Scenario(int scenario) {
  return std::find(kA1ScenarioIds.begin(), kA1ScenarioIds.end(), scenario) !=
         kA1ScenarioIds.end();
}

std::uint8_t sampledDisc(std::uint32_t root_seed, int scenario,
                         int scenario_count, std::uint32_t domain,
                         int event) {
  if (scenario < 0 || scenario >= scenario_count || event < 0) {
    throw std::invalid_argument("invalid stratified event coordinate");
  }
  const double unit = detail::stratifiedUnit(
      root_seed, scenario, scenario_count, domain, event);
  return static_cast<std::uint8_t>(
      std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
}

struct RevealStream {
  std::uint32_t root_seed = 0;
  int scenario = 0;
  int scenario_count = 0;
  int step = 0;
  int event = 0;

  std::uint8_t nextDisc() {
    if (event >= kEventsPerStep) {
      throw std::runtime_error("synthetic reveal event slice exhausted");
    }
    return sampledDisc(root_seed, scenario, scenario_count, kRevealDomain,
                       step * kEventsPerStep + event++);
  }
};

bool playSyntheticMove(const PublicState& source, int action,
                       std::uint32_t root_seed, int scenario,
                       int scenario_count, int step, MoveResult& result) {
  if (source.terminal || scenario < 0 || scenario >= scenario_count ||
      step < 0 || step >= kCHorizon || !isLegal(source.board, action)) {
    return false;
  }
  Board board = source.board;
  if (!placeDisc(board, action, source.next_disc)) return false;
  RevealStream reveals{root_seed, scenario, scenario_count, step, 0};
  result = MoveResult{};
  std::int64_t score = 0;
  detail::resolveCascadeSampled(board, reveals, 1, score, result.waves);
  result.score_delta = score;
  result.cleared_board = isBoardEmpty(board);
  if (result.cleared_board) result.score_delta += kClearBonus;

  int moves_remaining = source.moves_remaining - 1;
  bool terminal = false;
  if (moves_remaining == 0) {
    Board raised{};
    if (!raiseCoveredRow(board, raised)) {
      terminal = true;
    } else {
      result.level_advanced = true;
      moves_remaining = kMovesPerLevel;
      result.score_delta += kLevelBonus;
      board = raised;
      std::int64_t rise_score = 0;
      const int depth = result.waves.empty() ? 1 : result.waves.back().depth + 1;
      detail::resolveCascadeSampled(board, reveals, depth, rise_score,
                                    result.waves);
      result.score_delta += rise_score;
      if (isBoardEmpty(board)) {
        result.score_delta += kClearBonus;
        result.cleared_board = true;
      }
    }
  }
  int legal_count = 0;
  legalColumns(board, legal_count);
  if (!terminal && legal_count == 0) terminal = true;
  result.state.board = board;
  result.state.next_disc = terminal
                               ? source.next_disc
                               : sampledDisc(root_seed, scenario, scenario_count,
                                             kVisibleDomain, step);
  result.state.score = 0;
  result.state.level = 1;
  result.state.moves_remaining = moves_remaining;
  result.state.moves_played = 0;
  result.state.game_over = terminal;
  return true;
}

struct Work {
  std::uint64_t transitions = 0;
  D2Metrics d2{};
  std::uint64_t d4_work = 0;
  std::uint64_t d4_nodes = 0;
  std::size_t peak_d4_cache_entries = 0;

  Work& operator+=(const Work& other) {
    transitions += other.transitions;
    d2 += other.d2;
    d4_work += other.d4_work;
    d4_nodes += other.d4_nodes;
    peak_d4_cache_entries =
        std::max(peak_d4_cache_entries, other.peak_d4_cache_entries);
    return *this;
  }

  bool operator==(const Work&) const = default;
};

struct Path {
  PublicState state{};
  double raw_score = 0.0;
  double moves = 0.0;
  int clears = 0;
  int reveals = 0;

  bool operator==(const Path&) const = default;
};

struct ScenarioOutcome {
  double raw_score = 0.0;
  double utility = 0.0;
  double moves = 0.0;
  int clears = 0;
  int reveals = 0;
  bool survived = false;

  bool operator==(const ScenarioOutcome&) const = default;
};

ScenarioOutcome finishPath(const Path& path) {
  ScenarioOutcome result;
  result.raw_score = path.raw_score;
  result.moves = path.moves;
  result.clears = path.clears;
  result.reveals = path.reveals;
  result.survived = !path.state.terminal;
  result.utility = path.raw_score +
                   (path.state.terminal
                        ? kTerminalUtility
                        : fair::fairLeaf(frozen::materialize(path.state)));
  return result;
}

Path advancePath(Path path, int root_action, std::uint32_t root_seed,
                 int scenario, int scenario_count, int begin_step,
                 int end_step, Work& work, const Deadline* deadline) {
  if (begin_step < 0 || end_step < begin_step || end_step > kCHorizon) {
    throw std::invalid_argument("invalid rollout step interval");
  }
  for (int step = begin_step; step < end_step && !path.state.terminal; ++step) {
    if (deadline != nullptr) deadline->check();
    int action = root_action;
    if (step > 0) {
      const D2Decision decision = chooseFairD2(path.state);
      action = decision.action;
      ++work.d2.calls;
      work.d2.work += decision.work;
      work.d2.nodes += decision.nodes;
      work.d2.cache_hits += decision.cache_hits;
      work.d2.peak_cache_entries =
          std::max(work.d2.peak_cache_entries, decision.cache_entries);
      work.d2.root_actions += decision.evaluated_actions;
      int legal = 0;
      for (int column = 0; column < kBoardSize; ++column) {
        legal += isLegal(path.state.board, column);
      }
      work.d2.full_root =
          work.d2.full_root && decision.evaluated_actions == legal;
    }
    if (!isLegal(path.state.board, action)) {
      throw std::runtime_error("public D2 continuation selected illegal action");
    }
    MoveResult move;
    if (!playSyntheticMove(path.state, action, root_seed, scenario,
                           scenario_count, step, move)) {
      throw std::runtime_error("synthetic public transition failed");
    }
    ++work.transitions;
    path.raw_score += static_cast<double>(move.score_delta);
    path.moves += 1.0;
    for (const Wave& wave : move.waves) {
      path.clears += wave.cleared;
      path.reveals += wave.revealed;
    }
    path.state = frozen::publicState(move.state);
  }
  return path;
}

Path rolloutPath(const PublicState& root, int root_action, Panel panel,
                 int scenario, int scenario_count, int horizon, Work& work,
                 const Deadline* deadline) {
  if (root.terminal || !isLegal(root.board, root_action) || horizon < 1 ||
      horizon > kCHorizon) {
    throw std::invalid_argument("invalid rollout root/action/horizon");
  }
  Path path;
  path.state = root;
  return advancePath(path, root_action, panelRootSeed(root, panel), scenario,
                     scenario_count, 0, horizon, work, deadline);
}

struct ActionSummary {
  double mean_raw_score = 0.0;
  double mean_utility = 0.0;
  double mean_moves = 0.0;
  double mean_clears = 0.0;
  double mean_reveals = 0.0;
  int survivors = 0;
  int scenarios = 0;

  bool operator==(const ActionSummary&) const = default;
};

ActionSummary summarize(std::span<const ScenarioOutcome> outcomes) {
  if (outcomes.empty()) throw std::invalid_argument("cannot summarize no paths");
  ActionSummary result;
  result.scenarios = static_cast<int>(outcomes.size());
  for (const ScenarioOutcome& outcome : outcomes) {
    result.mean_raw_score += outcome.raw_score / outcomes.size();
    result.mean_utility += outcome.utility / outcomes.size();
    result.mean_moves += outcome.moves / outcomes.size();
    result.mean_clears += static_cast<double>(outcome.clears) / outcomes.size();
    result.mean_reveals +=
        static_cast<double>(outcome.reveals) / outcomes.size();
    result.survivors += outcome.survived;
  }
  return result;
}

struct ActionPanel {
  std::vector<ScenarioOutcome> outcomes;
  ActionSummary summary{};
};

ActionPanel evaluateAction(const PublicState& root, int action, Panel panel,
                           int scenarios, int horizon, Work& work,
                           const Deadline* deadline) {
  ActionPanel result;
  result.outcomes.reserve(static_cast<std::size_t>(scenarios));
  for (int scenario = 0; scenario < scenarios; ++scenario) {
    const Path path = rolloutPath(root, action, panel, scenario, scenarios,
                                  horizon, work, deadline);
    result.outcomes.push_back(finishPath(path));
  }
  result.summary = summarize(result.outcomes);
  return result;
}

double pairedLower95(const ActionPanel& candidate,
                     const ActionPanel& baseline, bool moves) {
  if (candidate.outcomes.size() != baseline.outcomes.size() ||
      candidate.outcomes.size() != kBScenarios) {
    throw std::invalid_argument("paired B panels differ from frozen K=21");
  }
  double mean = 0.0;
  std::array<double, kBScenarios> differences{};
  for (int scenario = 0; scenario < kBScenarios; ++scenario) {
    differences[scenario] =
        moves ? candidate.outcomes[scenario].moves -
                    baseline.outcomes[scenario].moves
              : candidate.outcomes[scenario].utility -
                    baseline.outcomes[scenario].utility;
    mean += differences[scenario] / kBScenarios;
  }
  double squares = 0.0;
  for (const double difference : differences) {
    const double centered = difference - mean;
    squares += centered * centered;
  }
  const double deviation =
      std::sqrt(squares / static_cast<double>(kBScenarios - 1));
  return mean - kPairedT95Df20 * deviation / std::sqrt(kBScenarios);
}

bool betterA1(const ActionSummary& left, int left_action,
              const ActionSummary& right, int right_action) {
  if (left.survivors != right.survivors) {
    return left.survivors > right.survivors;
  }
  if (std::abs(left.mean_clears - right.mean_clears) >
      kComparisonTolerance) {
    return left.mean_clears > right.mean_clears;
  }
  if (std::abs(left.mean_utility - right.mean_utility) >
      kComparisonTolerance) {
    return left.mean_utility > right.mean_utility;
  }
  const auto position = [](int action) {
    const auto found = std::find(detail::kColumnOrder.begin(),
                                 detail::kColumnOrder.end(), action);
    return static_cast<int>(found - detail::kColumnOrder.begin());
  };
  return position(left_action) < position(right_action);
}

int bestA1Action(const std::array<ActionSummary, kBoardSize>& summaries,
                 const std::array<bool, kBoardSize>& legal) {
  int best = -1;
  for (const int action : detail::kColumnOrder) {
    if (!legal[action]) continue;
    if (best < 0 || betterA1(summaries[action], action, summaries[best], best)) {
      best = action;
    }
  }
  return best;
}

std::array<bool, kBoardSize> retainedActions(
    const std::array<ActionSummary, kBoardSize>& summaries,
    const std::array<bool, kBoardSize>& legal, int d4_action) {
  if (d4_action < 0 || d4_action >= kBoardSize || !legal[d4_action]) {
    throw std::invalid_argument("invalid D4 action for A1 retention");
  }
  std::array<int, kBoardSize> ranked{};
  int count = 0;
  for (const int action : detail::kColumnOrder) {
    if (legal[action]) ranked[count++] = action;
  }
  std::stable_sort(ranked.begin(), ranked.begin() + count,
                   [&](int left, int right) {
                     return betterA1(summaries[left], left, summaries[right],
                                     right);
                   });
  std::array<bool, kBoardSize> result{};
  for (int index = 0; index < std::min(count, kRetainedChallengers); ++index) {
    result[ranked[index]] = true;
  }
  result[d4_action] = true;
  return result;
}

int selectA2Challenger(const std::array<ActionSummary, kBoardSize>& summaries,
                       const std::array<bool, kBoardSize>& retained,
                       int d4_action) {
  if (d4_action < 0 || d4_action >= kBoardSize || !retained[d4_action]) {
    throw std::invalid_argument("invalid D4 action for A2 selection");
  }
  const ActionSummary& baseline = summaries[d4_action];
  int result = -1;
  for (const int action : detail::kColumnOrder) {
    if (!retained[action]) continue;
    const ActionSummary& candidate = summaries[action];
    if (candidate.survivors < baseline.survivors ||
        candidate.mean_clears + kComparisonTolerance <
            baseline.mean_clears) {
      continue;
    }
    if (result < 0 || candidate.mean_utility >
                          summaries[result].mean_utility +
                              kComparisonTolerance) {
      result = action;
    }
  }
  if (result < 0) throw std::runtime_error("A2 rejected its D4 baseline");
  return result;
}

struct BConfirmation {
  bool evaluated = false;
  bool passed = false;
  double utility_lcb = 0.0;
  double moves_lcb = 0.0;
  ActionSummary challenger{};
  ActionSummary d4{};

  bool operator==(const BConfirmation&) const = default;
};

BConfirmation confirmB(const ActionPanel& candidate,
                       const ActionPanel& baseline) {
  BConfirmation result;
  result.evaluated = true;
  result.utility_lcb = pairedLower95(candidate, baseline, false);
  result.moves_lcb = pairedLower95(candidate, baseline, true);
  result.challenger = candidate.summary;
  result.d4 = baseline.summary;
  result.passed =
      result.utility_lcb > 0.0 && result.moves_lcb >= -kComparisonTolerance &&
      result.challenger.survivors >= result.d4.survivors &&
      result.challenger.mean_clears + kComparisonTolerance >=
          result.d4.mean_clears &&
      result.challenger.mean_reveals + kComparisonTolerance >=
          result.d4.mean_reveals;
  return result;
}

struct RootResult {
  std::uint64_t public_hash = 0;
  int origin_slot = -1;
  int d4_action = -1;
  int a1_action = -1;
  int a2_action = -1;
  int final_action = -1;
  bool a_stable = false;
  bool switched = false;
  BConfirmation b{};
  ActionSummary c_final{};
  ActionSummary c_d4{};
  bool c_beneficial = false;
  Work work{};
  double seconds = 0.0;

  bool operator==(const RootResult&) const = default;
};

std::string serializeRoot(std::size_t index, const RootResult& result);
RootResult parseRoot(std::string_view line, std::size_t expected_index);

using PublicPlannerBoundary = RootResult (*)(const PublicState&,
                                             const Deadline*);

RootResult evaluatePublicRoot(const PublicState& source,
                              const Deadline* deadline) {
  if (source.terminal) throw std::invalid_argument("terminal PRPI B0 root");
  const auto started = Clock::now();
  bool mirrored = false;
  const PublicState root = frozen::canonicalPublic(source, mirrored);
  RootResult result;
  result.public_hash = frozen::publicHash(root);

  const d4::SearchDecision d4_decision =
      d4::chooseDepth4Action(frozen::materialize(root));
  if (!d4_decision.complete ||
      d4_decision.completed_depth != d4::kCandidateDepth ||
      d4_decision.action < 0 || d4_decision.work > d4::kMaximumWork ||
      d4_decision.cache_entries > d4::kMaximumCacheEntries) {
    throw std::runtime_error("exact public D4 root did not complete");
  }
  result.d4_action = d4_decision.action;
  result.work.d4_work = d4_decision.work;
  result.work.d4_nodes = d4_decision.nodes;
  result.work.peak_d4_cache_entries = d4_decision.cache_entries;

  std::array<bool, kBoardSize> legal{};
  std::array<ActionSummary, kBoardSize> a1_summaries{};
  std::array<std::array<Path, kA1Scenarios>, kBoardSize> a1_paths{};
  const std::uint32_t panel_a_seed = panelRootSeed(root, Panel::kA);
  for (const int action : detail::kColumnOrder) {
    if (!isLegal(root.board, action)) continue;
    legal[action] = true;
    std::array<ScenarioOutcome, kA1Scenarios> outcomes{};
    for (int position = 0; position < kA1Scenarios; ++position) {
      const int scenario = kA1ScenarioIds[position];
      a1_paths[action][position] = advancePath(
          Path{root}, action, panel_a_seed, scenario, kA2Scenarios, 0,
          kA1Horizon, result.work, deadline);
      outcomes[position] = finishPath(a1_paths[action][position]);
    }
    a1_summaries[action] = summarize(outcomes);
  }
  result.a1_action = bestA1Action(a1_summaries, legal);
  if (result.a1_action < 0) throw std::runtime_error("A1 had no legal action");
  const std::array<bool, kBoardSize> retained =
      retainedActions(a1_summaries, legal, result.d4_action);

  std::array<ActionSummary, kBoardSize> a2_summaries{};
  for (const int action : detail::kColumnOrder) {
    if (!retained[action]) continue;
    std::array<ScenarioOutcome, kA2Scenarios> outcomes{};
    int a1_position = 0;
    for (int scenario = 0; scenario < kA2Scenarios; ++scenario) {
      Path path;
      if (isA1Scenario(scenario)) {
        if (a1_position >= kA1Scenarios ||
            kA1ScenarioIds[a1_position] != scenario) {
          throw std::runtime_error("A1/A2 nested scenario accounting failed");
        }
        path = advancePath(a1_paths[action][a1_position], action, panel_a_seed,
                           scenario, kA2Scenarios, kA1Horizon, kA2Horizon,
                           result.work, deadline);
        ++a1_position;
      } else {
        path = advancePath(Path{root}, action, panel_a_seed, scenario,
                           kA2Scenarios, 0, kA2Horizon, result.work, deadline);
      }
      outcomes[scenario] = finishPath(path);
    }
    if (a1_position != kA1Scenarios) {
      throw std::runtime_error("A2 did not consume all nested A1 paths");
    }
    a2_summaries[action] = summarize(outcomes);
  }
  result.a2_action =
      selectA2Challenger(a2_summaries, retained, result.d4_action);
  result.a_stable = result.a1_action == result.a2_action;
  result.final_action = result.d4_action;

  if (result.a2_action != result.d4_action) {
    const ActionPanel candidate = evaluateAction(
        root, result.a2_action, Panel::kB, kBScenarios, kBHorizon,
        result.work, deadline);
    const ActionPanel baseline = evaluateAction(
        root, result.d4_action, Panel::kB, kBScenarios, kBHorizon,
        result.work, deadline);
    result.b = confirmB(candidate, baseline);
    if (result.b.passed) {
      result.final_action = result.a2_action;
      result.switched = true;
    }
  }

  const ActionPanel c_d4 = evaluateAction(
      root, result.d4_action, Panel::kC, kCScenarios, kCHorizon,
      result.work, deadline);
  result.c_d4 = c_d4.summary;
  if (result.final_action == result.d4_action) {
    result.c_final = result.c_d4;
  } else {
    const ActionPanel c_final = evaluateAction(
        root, result.final_action, Panel::kC, kCScenarios, kCHorizon,
        result.work, deadline);
    result.c_final = c_final.summary;
  }
  result.c_beneficial =
      result.switched &&
      result.c_final.mean_utility >
          result.c_d4.mean_utility + kComparisonTolerance &&
      result.c_final.mean_moves + kComparisonTolerance >=
          result.c_d4.mean_moves;

  if (result.work.transitions > kMaximumTransitionsPerRoot ||
      result.work.d2.calls > kMaximumD2CallsPerRoot ||
      result.work.d2.work > kMaximumD2CallsPerRoot * kMaximumD2Work ||
      result.work.d2.peak_cache_entries > kMaximumD2CacheEntries ||
      result.work.d4_work > d4::kMaximumWork ||
      result.work.peak_d4_cache_entries > d4::kMaximumCacheEntries ||
      !result.work.d2.full_root || !isLegal(root.board, result.final_action)) {
    throw std::runtime_error("PRPI B0 per-root resource/completion proof failed");
  }

  if (mirrored) {
    result.d4_action = kBoardSize - 1 - result.d4_action;
    result.a1_action = kBoardSize - 1 - result.a1_action;
    result.a2_action = kBoardSize - 1 - result.a2_action;
    result.final_action = kBoardSize - 1 - result.final_action;
  }
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  if (deadline != nullptr) deadline->check();
  return result;
}

static_assert(std::is_same_v<decltype(&evaluatePublicRoot),
                             PublicPlannerBoundary>);
static_assert(!std::is_invocable_v<PublicPlannerBoundary,
                                   const frozen::PanelRecord&,
                                   const Deadline*>);

std::string readLimitedFile(const std::string& path, std::uint64_t limit) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) throw std::runtime_error("unable to open bounded input " + path);
  const std::streampos end = input.tellg();
  if (end < 0 || static_cast<std::uint64_t>(end) > limit) {
    throw std::runtime_error("bounded input size rejected");
  }
  std::string result(static_cast<std::size_t>(end), '\0');
  input.seekg(0);
  input.read(result.data(), static_cast<std::streamsize>(result.size()));
  if (!input || input.peek() != std::char_traits<char>::eof()) {
    throw std::runtime_error("short or unstable bounded input read");
  }
  return result;
}

void validateSource(const std::string& path, const std::string& expected_sha) {
  if (expected_sha.size() != 64 ||
      frozen::sha256(readLimitedFile(path, kSourceByteLimit)) != expected_sha) {
    throw std::runtime_error("PRPI B0 source SHA-256 mismatch");
  }
}

void atomicWrite(const std::string& path, std::string_view contents) {
  if (path.empty()) throw std::invalid_argument("empty atomic output path");
  const std::string temporary = path + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("unable to open atomic temporary");
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.flush();
    if (!output) throw std::runtime_error("atomic temporary write failed");
  }
  if (std::rename(temporary.c_str(), path.c_str()) != 0) {
    throw std::runtime_error("atomic output rename failed");
  }
}

struct CommonOptions {
  std::string source =
      "approaches/terminal-policy-iteration/public-regenerative-b0/public-regenerative-policy-iteration-b0.cpp";
  std::string source_sha256;
  int threads = kMaximumThreads;
};

struct PreflightOptions : CommonOptions {
  std::string output = "/tmp/drop7-prpi-b0-preflight.json";
};

struct RunOptions : CommonOptions {
  std::string input =
      "/tmp/drop7-terminal-policy-deployment-panels.jsonl";
  std::string input_sha256 = std::string(kExpectedCorpusSha256);
  std::string preflight = "/tmp/drop7-prpi-b0-preflight.json";
  std::string checkpoint = "/tmp/drop7-prpi-b0-checkpoint.jsonl";
  std::string output = "/tmp/drop7-prpi-b0.json";
};

void validateCommon(const CommonOptions& options) {
  if (options.source.empty() || options.source_sha256.size() != 64) {
    throw std::invalid_argument("source path and SHA-256 are required");
  }
  if (options.threads < 1 || options.threads > kMaximumThreads) {
    throw std::invalid_argument("threads must be in [1,4]");
  }
}

PreflightOptions parsePreflightOptions(int argc, char** argv, int begin) {
  PreflightOptions result;
  for (int index = begin; index < argc; ++index) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string_view flag(argv[index]);
    const std::string value(argv[++index]);
    if (flag == "--source") result.source = value;
    else if (flag == "--source-sha256") result.source_sha256 = value;
    else if (flag == "--threads") result.threads = std::stoi(value);
    else if (flag == "--output") result.output = value;
    else throw std::invalid_argument("unknown preflight option " +
                                     std::string(flag));
  }
  validateCommon(result);
  if (result.output.empty()) throw std::invalid_argument("empty preflight path");
  return result;
}

RunOptions parseRunOptions(int argc, char** argv, int begin) {
  RunOptions result;
  for (int index = begin; index < argc; ++index) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string_view flag(argv[index]);
    const std::string value(argv[++index]);
    if (flag == "--source") result.source = value;
    else if (flag == "--source-sha256") result.source_sha256 = value;
    else if (flag == "--threads") result.threads = std::stoi(value);
    else if (flag == "--input") result.input = value;
    else if (flag == "--input-sha256") result.input_sha256 = value;
    else if (flag == "--preflight") result.preflight = value;
    else if (flag == "--checkpoint") result.checkpoint = value;
    else if (flag == "--output") result.output = value;
    else throw std::invalid_argument("unknown run option " +
                                     std::string(flag));
  }
  validateCommon(result);
  if (result.input_sha256 != kExpectedCorpusSha256) {
    throw std::invalid_argument("frozen corpus checksum is not configurable");
  }
  if (result.input.empty() || result.preflight.empty() ||
      result.checkpoint.empty() || result.output.empty()) {
    throw std::invalid_argument("empty PRPI B0 run path");
  }
  const std::array<std::string, 4> paths{{
      result.input, result.preflight, result.checkpoint, result.output,
  }};
  for (std::size_t left = 0; left < paths.size(); ++left) {
    for (std::size_t right = left + 1; right < paths.size(); ++right) {
      if (frozen::resolvedPath(paths[left]) ==
          frozen::resolvedPath(paths[right])) {
        throw std::invalid_argument("PRPI B0 run paths must be distinct");
      }
    }
  }
  return result;
}

struct PreflightRecord {
  std::string source_sha256;
  std::uint64_t config_fingerprint = 0;
  int threads = 0;
  double measured_seconds = 0.0;
  double projected_seconds = 0.0;
  std::uint64_t peak_rss_bytes = 0;
  RootResult fixture{};
  bool passed = false;
};

std::string preflightJson(const PreflightRecord& record) {
  std::ostringstream output;
  output << std::setprecision(17)
         << "{\n  \"format\":\"drop7-prpi-b0-preflight-v1\",\n"
         << "  \"sourceSha256\":\"" << record.source_sha256 << "\",\n"
         << "  \"corpusSha256\":\"" << kExpectedCorpusSha256 << "\",\n"
         << "  \"configFingerprint\":\""
         << frozen::hex64(record.config_fingerprint) << "\",\n"
         << "  \"capability\":{\"corpusOpened\":false,"
            "\"originTransitions\":0,\"gameplaySeedsOpened\":0,"
            "\"protectedSeedsOpened\":0,\"finalSeedsOpened\":0},\n"
         << "  \"schedule\":{\"A1\":{\"K\":7,\"h\":25},"
            "\"A2\":{\"K\":21,\"h\":50,\"retained\":3},"
            "\"B\":{\"K\":21,\"h\":75},"
            "\"C\":{\"K\":35,\"h\":75},"
            "\"continuation\":\"exact-public-D2-s5\"},\n"
         << "  \"resources\":{\"threads\":" << record.threads
         << ",\"measuredFixtureSeconds\":" << record.measured_seconds
         << ",\"projectedWorstCaseSeconds\":" << record.projected_seconds
         << ",\"wallLimitSeconds\":" << kWallLimitSeconds
         << ",\"peakRssBytes\":" << record.peak_rss_bytes
         << ",\"rssLimitBytes\":" << kRssLimitBytes
         << ",\"maximumTransitions\":" << kMaximumSyntheticTransitions
         << ",\"maximumD2Calls\":" << kMaximumD2Calls
         << ",\"maximumD2Work\":" << kMaximumD2LogicalWork
         << ",\"maximumLogicalWork\":" << kMaximumLogicalWork << "},\n"
         << "  \"fixture\":{\"publicHash\":\""
         << frozen::hex64(record.fixture.public_hash)
         << "\",\"d4Action\":" << record.fixture.d4_action
         << ",\"A1Action\":" << record.fixture.a1_action
         << ",\"A2Action\":" << record.fixture.a2_action
         << ",\"finalAction\":" << record.fixture.final_action
         << ",\"switched\":"
         << (record.fixture.switched ? "true" : "false")
         << ",\"transitions\":" << record.fixture.work.transitions
         << ",\"d2Calls\":" << record.fixture.work.d2.calls
         << ",\"d2Work\":" << record.fixture.work.d2.work
         << ",\"d4Work\":" << record.fixture.work.d4_work << "},\n"
         << "  \"passed\":" << (record.passed ? "true" : "false")
         << "\n}\n";
  return output.str();
}

void validatePreflight(const RunOptions& options) {
  const std::string bytes = readLimitedFile(options.preflight, 256 * 1024);
  if (bytes.find("\"format\":\"drop7-prpi-b0-preflight-v1\"") ==
          std::string::npos ||
      frozen::stringAfter(bytes, "\"sourceSha256\":\"") !=
          options.source_sha256 ||
      frozen::stringAfter(bytes, "\"corpusSha256\":\"") !=
          kExpectedCorpusSha256 ||
      frozen::parseHex64(frozen::stringAfter(
          bytes, "\"configFingerprint\":\"")) != configFingerprint() ||
      frozen::booleanAfter(bytes, "\"corpusOpened\":") ||
      frozen::integerAfter(bytes, "\"originTransitions\":") != 0 ||
      frozen::integerAfter(bytes, "\"gameplaySeedsOpened\":") != 0 ||
      !frozen::booleanAfter(bytes, "\"passed\":")) {
    throw std::runtime_error("source-bound seed-free preflight is not valid");
  }
  const double projected =
      frozen::numberAfter(bytes, "\"projectedWorstCaseSeconds\":");
  if (projected > kWallLimitSeconds) {
    throw std::runtime_error("preflight projection exceeds frozen wall cap");
  }
}

void writeSummaryFields(std::ostream& output, std::string_view prefix,
                        const ActionSummary& summary) {
  output << ",\"" << prefix << "Raw\":" << summary.mean_raw_score
         << ",\"" << prefix << "Utility\":" << summary.mean_utility
         << ",\"" << prefix << "Moves\":" << summary.mean_moves
         << ",\"" << prefix << "Clears\":" << summary.mean_clears
         << ",\"" << prefix << "Reveals\":" << summary.mean_reveals
         << ",\"" << prefix << "Survivors\":" << summary.survivors
         << ",\"" << prefix << "Scenarios\":" << summary.scenarios;
}

ActionSummary parseSummary(std::string_view line, std::string_view prefix) {
  const std::string p(prefix);
  ActionSummary result;
  result.mean_raw_score =
      frozen::numberAfter(line, "\"" + p + "Raw\":");
  result.mean_utility =
      frozen::numberAfter(line, "\"" + p + "Utility\":");
  result.mean_moves =
      frozen::numberAfter(line, "\"" + p + "Moves\":");
  result.mean_clears =
      frozen::numberAfter(line, "\"" + p + "Clears\":");
  result.mean_reveals =
      frozen::numberAfter(line, "\"" + p + "Reveals\":");
  result.survivors =
      static_cast<int>(frozen::integerAfter(line, "\"" + p + "Survivors\":"));
  result.scenarios =
      static_cast<int>(frozen::integerAfter(line, "\"" + p + "Scenarios\":"));
  return result;
}

std::string checkpointHeader(std::string_view source_sha256) {
  std::ostringstream output;
  output << "{\"format\":\"drop7-prpi-b0-checkpoint-v1\","
         << "\"sourceSha256\":\"" << source_sha256 << "\","
         << "\"corpusSha256\":\"" << kExpectedCorpusSha256 << "\","
         << "\"configFingerprint\":\""
         << frozen::hex64(configFingerprint()) << "\","
         << "\"expectedRecords\":" << kExpectedRecords << "}";
  return output.str();
}

void saveCheckpoint(const std::string& path, std::string_view source_sha256,
                    std::span<const RootResult> results) {
  if (results.size() > kExpectedRecords) {
    throw std::invalid_argument("checkpoint result count exceeds corpus");
  }
  std::ostringstream payload;
  payload << checkpointHeader(source_sha256) << '\n';
  for (std::size_t index = 0; index < results.size(); ++index) {
    payload << serializeRoot(index, results[index]) << '\n';
  }
  const std::string body = payload.str();
  std::ostringstream complete;
  complete << body << "{\"completeCount\":" << results.size()
           << ",\"payloadSha256\":\"" << frozen::sha256(body)
           << "\"}\n";
  if (complete.str().size() > kCheckpointByteLimit) {
    throw std::runtime_error("checkpoint exceeds frozen byte limit");
  }
  atomicWrite(path, complete.str());
}

std::vector<RootResult> loadCheckpoint(const std::string& path,
                                       std::string_view source_sha256) {
  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    if (error) throw std::runtime_error("could not inspect checkpoint path");
    return {};
  }
  const std::string bytes = readLimitedFile(path, kCheckpointByteLimit);
  std::vector<std::string_view> lines;
  std::size_t begin = 0;
  while (begin < bytes.size()) {
    const std::size_t newline = bytes.find('\n', begin);
    const std::size_t end =
        newline == std::string::npos ? bytes.size() : newline;
    if (end > begin) lines.push_back(std::string_view(bytes).substr(begin, end - begin));
    begin = newline == std::string::npos ? bytes.size() : newline + 1;
  }
  if (lines.size() < 2 ||
      lines.front().find("\"format\":\"drop7-prpi-b0-checkpoint-v1\"") ==
          std::string_view::npos ||
      frozen::stringAfter(lines.front(), "\"sourceSha256\":\"") !=
          source_sha256 ||
      frozen::stringAfter(lines.front(), "\"corpusSha256\":\"") !=
          kExpectedCorpusSha256 ||
      frozen::parseHex64(frozen::stringAfter(
          lines.front(), "\"configFingerprint\":\"")) !=
          configFingerprint() ||
      frozen::integerAfter(lines.front(), "\"expectedRecords\":") !=
          kExpectedRecords) {
    throw std::runtime_error("checkpoint header/provenance mismatch");
  }
  const long long complete_count =
      frozen::integerAfter(lines.back(), "\"completeCount\":");
  if (complete_count < 0 || complete_count > kExpectedRecords ||
      lines.size() != static_cast<std::size_t>(complete_count) + 2) {
    throw std::runtime_error("checkpoint prefix count mismatch");
  }
  const std::size_t footer_begin =
      static_cast<std::size_t>(lines.back().data() - bytes.data());
  const std::string_view body(bytes.data(), footer_begin);
  if (frozen::sha256(body) !=
      frozen::stringAfter(lines.back(), "\"payloadSha256\":\"")) {
    throw std::runtime_error("checkpoint payload checksum mismatch");
  }
  std::vector<RootResult> results;
  results.reserve(static_cast<std::size_t>(complete_count));
  for (int index = 0; index < complete_count; ++index) {
    results.push_back(parseRoot(lines[static_cast<std::size_t>(index) + 1],
                                static_cast<std::size_t>(index)));
  }
  return results;
}

struct OriginMetrics {
  int roots = 0;
  double candidate_raw = 0.0;
  double d4_raw = 0.0;
  double candidate_utility = 0.0;
  double d4_utility = 0.0;
  double candidate_moves = 0.0;
  double d4_moves = 0.0;
  double candidate_clears = 0.0;
  double d4_clears = 0.0;
  double candidate_reveals = 0.0;
  double d4_reveals = 0.0;
  double utility_delta = 0.0;
  double moves_delta = 0.0;
  bool nonregressing = false;
};

struct GateResult {
  int roots = 0;
  int stable = 0;
  int overrides = 0;
  int beneficial_overrides = 0;
  double stability = 0.0;
  double override_coverage = 0.0;
  double override_precision = 0.0;
  double raw_score_ratio = 0.0;
  double rmst_ratio = 0.0;
  double candidate_clears_per_move = 0.0;
  double d4_clears_per_move = 0.0;
  double candidate_reveals_per_move = 0.0;
  double d4_reveals_per_move = 0.0;
  double utility_delta_mean = 0.0;
  double utility_delta_lcb95 = 0.0;
  double moves_delta_mean = 0.0;
  double moves_delta_lcb95 = 0.0;
  int nonregressing_origins = 0;
  std::array<OriginMetrics, kExpectedOrigins> origins{};
  std::array<bool, 2> ordered_halves{{false, false}};
  bool stability_gate = false;
  bool coverage_gate = false;
  bool precision_gate = false;
  bool score_gate = false;
  bool rmst_gate = false;
  bool confidence_gate = false;
  bool flow_gate = false;
  bool origin_gate = false;
  bool halves_gate = false;
  bool passed = false;
};

double meanLower95(const std::array<double, kExpectedOrigins>& values,
                   double& mean) {
  mean = std::accumulate(values.begin(), values.end(), 0.0) /
         static_cast<double>(values.size());
  double squares = 0.0;
  for (const double value : values) {
    const double centered = value - mean;
    squares += centered * centered;
  }
  const double deviation =
      std::sqrt(squares / static_cast<double>(values.size() - 1));
  return mean - kOriginT95Df7 * deviation /
                    std::sqrt(static_cast<double>(values.size()));
}

GateResult evaluateGate(std::span<const RootResult> roots) {
  if (roots.size() != kExpectedRecords) {
    throw std::invalid_argument("B0 gate requires all 477 roots");
  }
  GateResult result;
  result.roots = static_cast<int>(roots.size());
  for (const RootResult& root : roots) {
    if (root.origin_slot < 0 || root.origin_slot >= kExpectedOrigins) {
      throw std::runtime_error("B0 root missing whole-origin provenance");
    }
    result.stable += root.a_stable;
    result.overrides += root.switched;
    result.beneficial_overrides += root.c_beneficial;
    OriginMetrics& origin = result.origins[root.origin_slot];
    ++origin.roots;
    origin.candidate_raw += root.c_final.mean_raw_score;
    origin.d4_raw += root.c_d4.mean_raw_score;
    origin.candidate_utility += root.c_final.mean_utility;
    origin.d4_utility += root.c_d4.mean_utility;
    origin.candidate_moves += root.c_final.mean_moves;
    origin.d4_moves += root.c_d4.mean_moves;
    origin.candidate_clears += root.c_final.mean_clears;
    origin.d4_clears += root.c_d4.mean_clears;
    origin.candidate_reveals += root.c_final.mean_reveals;
    origin.d4_reveals += root.c_d4.mean_reveals;
  }
  result.stability = static_cast<double>(result.stable) / result.roots;
  result.override_coverage =
      static_cast<double>(result.overrides) / result.roots;
  result.override_precision =
      result.overrides > 0
          ? static_cast<double>(result.beneficial_overrides) / result.overrides
          : 0.0;

  std::array<double, kExpectedOrigins> utility_deltas{};
  std::array<double, kExpectedOrigins> moves_deltas{};
  double score_ratios = 0.0;
  double move_ratios = 0.0;
  double candidate_clear_rates = 0.0;
  double d4_clear_rates = 0.0;
  double candidate_reveal_rates = 0.0;
  double d4_reveal_rates = 0.0;
  for (int slot = 0; slot < kExpectedOrigins; ++slot) {
    OriginMetrics& origin = result.origins[slot];
    if (origin.roots != frozen::kExpectedGameRecords[slot] ||
        origin.d4_raw <= 0.0 || origin.d4_moves <= 0.0 ||
        origin.candidate_moves <= 0.0) {
      throw std::runtime_error("whole-origin metric accounting failed");
    }
    origin.utility_delta =
        (origin.candidate_utility - origin.d4_utility) / origin.roots;
    origin.moves_delta =
        (origin.candidate_moves - origin.d4_moves) / origin.roots;
    origin.nonregressing =
        origin.utility_delta >= -kComparisonTolerance &&
        origin.moves_delta >= -kComparisonTolerance;
    result.nonregressing_origins += origin.nonregressing;
    utility_deltas[slot] = origin.utility_delta;
    moves_deltas[slot] = origin.moves_delta;
    score_ratios += (origin.candidate_raw / origin.d4_raw) / kExpectedOrigins;
    move_ratios +=
        (origin.candidate_moves / origin.d4_moves) / kExpectedOrigins;
    candidate_clear_rates +=
        (origin.candidate_clears / origin.candidate_moves) / kExpectedOrigins;
    d4_clear_rates +=
        (origin.d4_clears / origin.d4_moves) / kExpectedOrigins;
    candidate_reveal_rates +=
        (origin.candidate_reveals / origin.candidate_moves) / kExpectedOrigins;
    d4_reveal_rates +=
        (origin.d4_reveals / origin.d4_moves) / kExpectedOrigins;
  }
  result.raw_score_ratio = score_ratios;
  result.rmst_ratio = move_ratios;
  result.candidate_clears_per_move = candidate_clear_rates;
  result.d4_clears_per_move = d4_clear_rates;
  result.candidate_reveals_per_move = candidate_reveal_rates;
  result.d4_reveals_per_move = d4_reveal_rates;
  result.utility_delta_lcb95 =
      meanLower95(utility_deltas, result.utility_delta_mean);
  result.moves_delta_lcb95 =
      meanLower95(moves_deltas, result.moves_delta_mean);
  for (int half = 0; half < 2; ++half) {
    double utility = 0.0;
    double moves = 0.0;
    for (int offset = 0; offset < 4; ++offset) {
      utility += utility_deltas[half * 4 + offset];
      moves += moves_deltas[half * 4 + offset];
    }
    result.ordered_halves[half] =
        utility >= -kComparisonTolerance && moves >= -kComparisonTolerance;
  }

  result.stability_gate = result.stability >= kStabilityMinimum;
  result.coverage_gate =
      result.override_coverage >= kOverrideCoverageMinimum;
  result.precision_gate =
      result.override_precision >= kOverridePrecisionMinimum;
  result.score_gate = result.raw_score_ratio >= kRawScoreRatioMinimum;
  result.rmst_gate = result.rmst_ratio >= kRmstRatioMinimum;
  result.confidence_gate = result.utility_delta_lcb95 > 0.0 &&
                           result.moves_delta_lcb95 > 0.0;
  result.flow_gate =
      result.candidate_clears_per_move + kComparisonTolerance >=
          result.d4_clears_per_move &&
      result.candidate_reveals_per_move + kComparisonTolerance >=
          result.d4_reveals_per_move;
  result.origin_gate =
      result.nonregressing_origins >= kRequiredNonregressingOrigins;
  result.halves_gate =
      result.ordered_halves[0] && result.ordered_halves[1];
  result.passed = result.stability_gate && result.coverage_gate &&
                  result.precision_gate && result.score_gate &&
                  result.rmst_gate && result.confidence_gate &&
                  result.flow_gate && result.origin_gate &&
                  result.halves_gate;
  return result;
}

void writeActionSummary(std::ostream& output, const ActionSummary& summary) {
  output << std::setprecision(17)
         << "{\"meanRawScore\":" << summary.mean_raw_score
         << ",\"meanUtility\":" << summary.mean_utility
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"meanClears\":" << summary.mean_clears
         << ",\"meanReveals\":" << summary.mean_reveals
         << ",\"survivors\":" << summary.survivors
         << ",\"scenarios\":" << summary.scenarios << '}';
}

void writeGate(std::ostream& output, const GateResult& gate) {
  output << std::setprecision(17)
         << "{\"observed\":{\"roots\":" << gate.roots
         << ",\"stable\":" << gate.stable
         << ",\"overrides\":" << gate.overrides
         << ",\"beneficialOverrides\":" << gate.beneficial_overrides
         << ",\"stability\":" << gate.stability
         << ",\"overrideCoverage\":" << gate.override_coverage
         << ",\"overridePrecision\":" << gate.override_precision
         << ",\"rawScoreRatio\":" << gate.raw_score_ratio
         << ",\"rmstRatio\":" << gate.rmst_ratio
         << ",\"utilityDeltaMean\":" << gate.utility_delta_mean
         << ",\"utilityDeltaLcb95\":" << gate.utility_delta_lcb95
         << ",\"movesDeltaMean\":" << gate.moves_delta_mean
         << ",\"movesDeltaLcb95\":" << gate.moves_delta_lcb95
         << ",\"candidateClearsPerMove\":"
         << gate.candidate_clears_per_move
         << ",\"d4ClearsPerMove\":" << gate.d4_clears_per_move
         << ",\"candidateRevealsPerMove\":"
         << gate.candidate_reveals_per_move
         << ",\"d4RevealsPerMove\":" << gate.d4_reveals_per_move
         << ",\"nonregressingOrigins\":" << gate.nonregressing_origins
         << ",\"orderedHalves\":["
         << (gate.ordered_halves[0] ? "true" : "false") << ','
         << (gate.ordered_halves[1] ? "true" : "false")
         << "]},\"thresholds\":{\"stability\":" << kStabilityMinimum
         << ",\"overrideCoverage\":" << kOverrideCoverageMinimum
         << ",\"overridePrecision\":" << kOverridePrecisionMinimum
         << ",\"rawScoreRatio\":" << kRawScoreRatioMinimum
         << ",\"rmstRatio\":" << kRmstRatioMinimum
         << ",\"positiveOriginClusterLcb\":true,"
            "\"flowNonregression\":true,\"nonregressingOrigins\":"
         << kRequiredNonregressingOrigins
         << ",\"bothOrderedHalvesNonregressing\":true},\"checks\":{"
         << "\"stability\":" << (gate.stability_gate ? "true" : "false")
         << ",\"coverage\":" << (gate.coverage_gate ? "true" : "false")
         << ",\"precision\":" << (gate.precision_gate ? "true" : "false")
         << ",\"score\":" << (gate.score_gate ? "true" : "false")
         << ",\"rmst\":" << (gate.rmst_gate ? "true" : "false")
         << ",\"confidence\":"
         << (gate.confidence_gate ? "true" : "false")
         << ",\"flow\":" << (gate.flow_gate ? "true" : "false")
         << ",\"origins\":" << (gate.origin_gate ? "true" : "false")
         << ",\"halves\":" << (gate.halves_gate ? "true" : "false")
         << "},\"origins\":[";
  for (int slot = 0; slot < kExpectedOrigins; ++slot) {
    if (slot) output << ',';
    const OriginMetrics& origin = gate.origins[slot];
    output << "{\"slot\":" << slot << ",\"game\":\""
           << frozen::hex64(frozen::kExpectedGameStart + slot)
           << "\",\"roots\":" << origin.roots
           << ",\"utilityDelta\":" << origin.utility_delta
           << ",\"movesDelta\":" << origin.moves_delta
           << ",\"nonregressing\":"
           << (origin.nonregressing ? "true" : "false") << '}';
  }
  output << "],\"passed\":" << (gate.passed ? "true" : "false") << '}';
}

std::string finalArtifact(std::string_view source_sha256,
                          std::span<const RootResult> roots,
                          const GateResult& gate, const Work& work,
                          double effective_wall_seconds,
                          std::uint64_t peak_rss_bytes) {
  std::ostringstream output;
  output << std::setprecision(17)
         << "{\n  \"experiment\":\"public-regenerative-policy-iteration-b0\",\n"
         << "  \"sourceSha256\":\"" << source_sha256 << "\",\n"
         << "  \"corpus\":{\"pathRole\":\"burned-public-root-inventory\","
            "\"sha256\":\""
         << kExpectedCorpusSha256 << "\",\"records\":" << kExpectedRecords
         << ",\"origins\":" << kExpectedOrigins
         << ",\"storedD1OutcomesUsedForSelection\":false,"
            "\"originTransitions\":0},\n"
         << "  \"publicBoundary\":[\"board\",\"nextDisc\","
            "\"movesRemaining\",\"terminal\"],\n"
         << "  \"excludedFromPolicy\":[\"originGame\",\"moveIndex\","
            "\"score\",\"level\",\"history\",\"scenario\","
            "\"tape\",\"futureDisc\",\"futureReveal\"],\n"
         << "  \"planner\":{\"A1\":{\"K\":7,\"h\":25,"
            "\"allLegalRootActions\":true},\"A2\":{\"K\":21,"
            "\"h\":50,\"retainedChallengers\":3,"
            "\"D4AlwaysRetained\":true},\"B\":{\"K\":21,"
            "\"h\":75,\"independent\":true,"
            "\"pairedReturnLcb\":\"one-sided-95-df20\"},"
            "\"C\":{\"K\":35,\"h\":75,\"evaluationOnly\":true},"
            "\"continuation\":\"completed-full-width-public-D2-s5\","
            "\"fallback\":\"exact-public-D4-s5\","
            "\"D4QBand\":null,\"eventStratified\":true,"
            "\"commonSiblingStreams\":true,"
            "\"panelDomainsIndependent\":true,"
            "\"revealVisibleDomainsSeparate\":true},\n"
         << "  \"gate\":";
  writeGate(output, gate);
  output << ",\n  \"resources\":{\"syntheticTransitions\":"
         << work.transitions << ",\"d2Calls\":" << work.d2.calls
         << ",\"d2Work\":" << work.d2.work
         << ",\"d2Nodes\":" << work.d2.nodes
         << ",\"d2PeakCacheEntries\":" << work.d2.peak_cache_entries
         << ",\"d4Work\":" << work.d4_work
         << ",\"d4Nodes\":" << work.d4_nodes
         << ",\"d4PeakCacheEntries\":" << work.peak_d4_cache_entries
         << ",\"effectiveWallSeconds\":" << effective_wall_seconds
         << ",\"wallLimitSeconds\":" << kWallLimitSeconds
         << ",\"peakRssBytes\":" << peak_rss_bytes
         << ",\"rssLimitBytes\":" << kRssLimitBytes
         << ",\"maximumTransitions\":" << kMaximumSyntheticTransitions
         << ",\"maximumD2Calls\":" << kMaximumD2Calls
         << ",\"maximumLogicalWork\":" << kMaximumLogicalWork << "},\n"
         << "  \"seedAudit\":{\"gameplaySeedsOpened\":0,"
            "\"protectedSeedsOpened\":0,\"finalSeedsOpened\":0},\n"
         << "  \"roots\":[";
  for (std::size_t index = 0; index < roots.size(); ++index) {
    if (index) output << ',';
    output << serializeRoot(index, roots[index]);
  }
  output << "],\n  \"passed\":" << (gate.passed ? "true" : "false")
         << "\n}\n";
  return output.str();
}

PreflightRecord measurePreflight(const PreflightOptions& options) {
  validateSource(options.source, options.source_sha256);
  Deadline deadline;
  PublicState fixture;
  fixture.board = initialBoard();
  fixture.next_disc = 3;
  fixture.moves_remaining = kMovesPerLevel;
  PreflightRecord result;
  result.source_sha256 = options.source_sha256;
  result.config_fingerprint = configFingerprint();
  result.threads = options.threads;
  result.fixture = evaluatePublicRoot(fixture, &deadline);
  result.measured_seconds = result.fixture.seconds;
  const int waves =
      (kExpectedRecords + options.threads - 1) / options.threads;
  result.projected_seconds =
      result.measured_seconds * waves * kProjectionSafetyFactor +
      kProjectionReserveSeconds;
  result.peak_rss_bytes = frozen::peakRssBytes();
  result.passed = result.projected_seconds <= kWallLimitSeconds &&
                  result.peak_rss_bytes <= kRssLimitBytes &&
                  result.fixture.work.transitions <=
                      kMaximumTransitionsPerRoot &&
                  result.fixture.work.d2.calls <= kMaximumD2CallsPerRoot &&
                  result.fixture.work.d2.full_root;
  atomicWrite(options.output, preflightJson(result));
  return result;
}

int preflight(const PreflightOptions& options, std::ostream& summary) {
  const PreflightRecord result = measurePreflight(options);
  summary << std::fixed << std::setprecision(6)
          << "PUBLIC_REGENERATIVE_POLICY_ITERATION_B0_PREFLIGHT {"
          << "\"passed\":" << (result.passed ? "true" : "false")
          << ",\"corpusOpened\":false,\"gameplaySeedsOpened\":0"
          << ",\"fixtureSeconds\":" << result.measured_seconds
          << ",\"projectedWorstCaseSeconds\":"
          << result.projected_seconds
          << ",\"fixtureTransitions\":"
          << result.fixture.work.transitions
          << ",\"fixtureD2Calls\":" << result.fixture.work.d2.calls
          << ",\"fixtureD2Work\":" << result.fixture.work.d2.work
          << ",\"fixtureD4Work\":" << result.fixture.work.d4_work
          << ",\"peakRssBytes\":" << result.peak_rss_bytes
          << ",\"artifact\":\"" << frozen::jsonEscape(options.output)
          << "\"}\n";
  return result.passed ? 0 : 2;
}

int run(const RunOptions& options, std::ostream& summary) {
  // Capability checks happen before reading the development-corpus path.
  validateSource(options.source, options.source_sha256);
  validatePreflight(options);
  Deadline deadline;

  frozen::RunOptions corpus_options;
  corpus_options.input = options.input;
  corpus_options.input_sha256 = options.input_sha256;
  corpus_options.output = options.output;
  corpus_options.threads = options.threads;
  const std::vector<frozen::PanelRecord> panels =
      frozen::loadLockedCorpus(corpus_options);
  std::vector<RootResult> roots =
      loadCheckpoint(options.checkpoint, options.source_sha256);
  for (std::size_t index = 0; index < roots.size(); ++index) {
    if (roots[index].public_hash != frozen::publicHash(panels[index].state) ||
        roots[index].origin_slot != panels[index].origin_slot) {
      throw std::runtime_error("checkpoint prefix differs from locked corpus");
    }
  }

  while (roots.size() < panels.size()) {
    const std::size_t begin = roots.size();
    const std::size_t count =
        std::min<std::size_t>(options.threads, panels.size() - begin);
    std::vector<std::future<RootResult>> futures;
    futures.reserve(count);
    for (std::size_t offset = 0; offset < count; ++offset) {
      const PublicState state = panels[begin + offset].state;
      futures.push_back(std::async(std::launch::async, [state, &deadline] {
        return evaluatePublicRoot(state, &deadline);
      }));
    }
    for (std::size_t offset = 0; offset < count; ++offset) {
      RootResult root = futures[offset].get();
      root.origin_slot = panels[begin + offset].origin_slot;
      roots.push_back(root);
    }
    saveCheckpoint(options.checkpoint, options.source_sha256, roots);
    double root_seconds = 0.0;
    for (const RootResult& root : roots) root_seconds += root.seconds;
    const double effective = root_seconds / options.threads;
    if (effective > kWallLimitSeconds) {
      throw std::runtime_error("resumed aggregate root time exceeds wall cap");
    }
    deadline.check();
    std::cerr << "PRPI B0 burned public roots " << roots.size() << '/'
              << panels.size() << " checkpointed\n";
  }

  Work work;
  double root_seconds = 0.0;
  for (const RootResult& root : roots) {
    work += root.work;
    root_seconds += root.seconds;
  }
  const double effective_wall_seconds = root_seconds / options.threads;
  if (work.transitions > kMaximumSyntheticTransitions ||
      work.d2.calls > kMaximumD2Calls ||
      work.d2.work > kMaximumD2LogicalWork ||
      work.d4_work > kMaximumD4LogicalWork ||
      work.d2.work + work.d4_work > kMaximumLogicalWork ||
      work.d2.peak_cache_entries > kMaximumD2CacheEntries ||
      work.peak_d4_cache_entries > d4::kMaximumCacheEntries ||
      !work.d2.full_root || effective_wall_seconds > kWallLimitSeconds ||
      frozen::peakRssBytes() > kRssLimitBytes) {
    throw std::runtime_error("PRPI B0 aggregate resource proof failed");
  }
  const GateResult gate = evaluateGate(roots);
  atomicWrite(options.output,
              finalArtifact(options.source_sha256, roots, gate, work,
                            effective_wall_seconds, frozen::peakRssBytes()));
  summary << std::fixed << std::setprecision(6)
          << "PUBLIC_REGENERATIVE_POLICY_ITERATION_B0 {\"passed\":"
          << (gate.passed ? "true" : "false")
          << ",\"roots\":" << roots.size()
          << ",\"stability\":" << gate.stability
          << ",\"overrideCoverage\":" << gate.override_coverage
          << ",\"overridePrecision\":" << gate.override_precision
          << ",\"rawScoreRatio\":" << gate.raw_score_ratio
          << ",\"rmstRatio\":" << gate.rmst_ratio
          << ",\"utilityLcb95\":" << gate.utility_delta_lcb95
          << ",\"movesLcb95\":" << gate.moves_delta_lcb95
          << ",\"nonregressingOrigins\":"
          << gate.nonregressing_origins
          << ",\"transitions\":" << work.transitions
          << ",\"d2Calls\":" << work.d2.calls
          << ",\"d2Work\":" << work.d2.work
          << ",\"d4Work\":" << work.d4_work
          << ",\"effectiveWallSeconds\":" << effective_wall_seconds
          << ",\"peakRssBytes\":" << frozen::peakRssBytes()
          << ",\"originTransitions\":0,\"newGameplaySeeds\":0"
          << ",\"artifact\":\"" << frozen::jsonEscape(options.output)
          << "\"}\n";
  return gate.passed ? 0 : 2;
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

template <typename Function>
bool throwsAny(Function&& function) {
  try {
    function();
  } catch (...) {
    return true;
  }
  return false;
}

PublicState asymmetricFixture() {
  PublicState state;
  state.board.fill(kEmpty);
  state.board[indexOf(6, 0)] = kSolid;
  state.board[indexOf(5, 0)] = 6;
  state.board[indexOf(6, 1)] = kCracked;
  state.board[indexOf(6, 2)] = 5;
  state.board[indexOf(5, 2)] = 4;
  state.board[indexOf(6, 3)] = kSolid;
  state.board[indexOf(6, 4)] = 7;
  state.next_disc = 3;
  state.moves_remaining = 4;
  return state;
}

ActionPanel constantPanel(double utility, double moves, int clears,
                          int reveals, bool survived) {
  ActionPanel panel;
  panel.outcomes.resize(kBScenarios);
  for (ScenarioOutcome& outcome : panel.outcomes) {
    outcome.raw_score = utility;
    outcome.utility = utility;
    outcome.moves = moves;
    outcome.clears = clears;
    outcome.reveals = reveals;
    outcome.survived = survived;
  }
  panel.summary = summarize(panel.outcomes);
  return panel;
}

RootResult syntheticGateRoot(int origin, bool switched = true,
                             bool beneficial = true) {
  RootResult result;
  result.public_hash = frozen::mix64(static_cast<std::uint64_t>(origin + 1));
  result.origin_slot = origin;
  result.d4_action = 3;
  result.a1_action = 2;
  result.a2_action = 2;
  result.final_action = switched ? 2 : 3;
  result.a_stable = true;
  result.switched = switched;
  result.c_beneficial = switched && beneficial;
  result.c_d4.mean_raw_score = 100.0;
  result.c_d4.mean_utility = 100.0;
  result.c_d4.mean_moves = 10.0;
  result.c_d4.mean_clears = 20.0;
  result.c_d4.mean_reveals = 10.0;
  result.c_d4.survivors = kCScenarios;
  result.c_d4.scenarios = kCScenarios;
  // Keep the positive-control fixture clear of floating-point threshold boundaries;
  // separate boundary tests exercise each gate exactly.
  result.c_final.mean_raw_score = 120.0;
  result.c_final.mean_utility = 120.0;
  result.c_final.mean_moves = 11.0;
  result.c_final.mean_clears = 22.0;
  result.c_final.mean_reveals = 11.0;
  result.c_final.survivors = kCScenarios;
  result.c_final.scenarios = kCScenarios;
  return result;
}

bool selfTest(std::ostream& output) {
  expect(kMaximumTransitionsPerRoot == 13'125 &&
             kMaximumD2CallsPerRoot == 12'908 &&
             kMaximumSyntheticTransitions == 6'260'625 &&
             kMaximumD2Calls == 6'157'116 &&
             kMaximumD2LogicalWork == 15'300'433'260ull &&
             kMaximumLogicalWork == 16'826'833'260ull,
         "frozen accounting constants changed");
  expect(configFingerprint() == configFingerprint(),
         "configuration fingerprint is not deterministic");

  const PublicState fixture = asymmetricFixture();
  const PublicState reflected_fixture = frozen::mirror(fixture);
  const D2Decision d2 = chooseFairD2(fixture);
  const D2Decision d2_repeat = chooseFairD2(fixture);
  const D2Decision d2_reflected = chooseFairD2(reflected_fixture);
  expect(d2 == d2_repeat && isLegal(fixture.board, d2.action) &&
             d2_reflected.action == kBoardSize - 1 - d2.action &&
             d2.work <= kMaximumD2Work &&
             d2.cache_entries <= kMaximumD2CacheEntries,
         "public D2 determinism/reflection/resource proof failed");
  for (int action = 0; action < kBoardSize; ++action) {
    expect(d2.values[action] ==
               d2_reflected.values[kBoardSize - 1 - action],
           "public D2 action values failed reflection mapping");
  }
  const d4::SearchDecision d4_fixture =
      d4::chooseDepth4Action(frozen::materialize(fixture));
  const d4::SearchDecision d4_reflected =
      d4::chooseDepth4Action(frozen::materialize(reflected_fixture));
  expect(d4_fixture.complete && d4_reflected.complete &&
             d4_reflected.action == kBoardSize - 1 - d4_fixture.action,
         "exact D4 reflection/action mapping failed");

  State metadata = frozen::materialize(fixture);
  metadata.score = 999'999'999;
  metadata.level = 777;
  metadata.moves_played = 888;
  expect(frozen::publicState(metadata) == fixture &&
             chooseFairD2(frozen::publicState(metadata)) == d2,
         "public continuation observed forbidden metadata");

  bool ignored = false;
  const PublicState canonical = frozen::canonicalPublic(fixture, ignored);
  expect(panelRootSeed(canonical, Panel::kA) ==
             panelRootSeed(frozen::mirror(canonical), Panel::kA) &&
             panelRootSeed(canonical, Panel::kA) !=
                 panelRootSeed(canonical, Panel::kB) &&
             panelRootSeed(canonical, Panel::kB) !=
                 panelRootSeed(canonical, Panel::kC),
         "canonical/independent panel domains failed");
  std::array<int, kBoardSize + 1> a_visible{};
  std::array<int, kBoardSize + 1> a_reveal{};
  std::array<int, kBoardSize + 1> a1_visible{};
  const std::uint32_t a_seed = panelRootSeed(canonical, Panel::kA);
  for (int scenario = 0; scenario < kA2Scenarios; ++scenario) {
    ++a_visible[sampledDisc(a_seed, scenario, kA2Scenarios, kVisibleDomain, 0)];
    ++a_reveal[sampledDisc(a_seed, scenario, kA2Scenarios, kRevealDomain, 0)];
    if (isA1Scenario(scenario)) {
      ++a1_visible[
          sampledDisc(a_seed, scenario, kA2Scenarios, kVisibleDomain, 0)];
    }
  }
  std::array<int, kBoardSize + 1> c_visible{};
  const std::uint32_t c_seed = panelRootSeed(canonical, Panel::kC);
  for (int scenario = 0; scenario < kCScenarios; ++scenario) {
    ++c_visible[
        sampledDisc(c_seed, scenario, kCScenarios, kVisibleDomain, 0)];
  }
  for (int disc = 1; disc <= kBoardSize; ++disc) {
    expect(a_visible[disc] == 3 && a_reveal[disc] == 3 &&
               a1_visible[disc] == 1 && c_visible[disc] == 5,
           "nested/exact chance stratification failed");
  }
  expect(kRevealDomain != kVisibleDomain &&
             sampledDisc(a_seed, 0, kA2Scenarios, kRevealDomain, 7) ==
                 sampledDisc(a_seed, 0, kA2Scenarios, kRevealDomain, 7),
         "event-domain determinism/isolation failed");

  const int legal_action = centerFirstMove(canonical.board);
  Work tiny_first;
  Work tiny_second;
  const Path first_path = rolloutPath(canonical, legal_action, Panel::kA,
                                      kA1ScenarioIds[0], kA2Scenarios, 3,
                                      tiny_first, nullptr);
  const Path second_path = rolloutPath(canonical, legal_action, Panel::kA,
                                       kA1ScenarioIds[0], kA2Scenarios, 3,
                                       tiny_second, nullptr);
  expect(first_path == second_path && tiny_first == tiny_second &&
             tiny_first.transitions <= 3 && tiny_first.d2.calls <= 2 &&
             tiny_first.d2.full_root,
         "synthetic CRN determinism/accounting failed");

  std::array<ActionSummary, kBoardSize> known_summaries{};
  std::array<bool, kBoardSize> known_legal{};
  known_legal[1] = known_legal[2] = known_legal[3] = true;
  known_summaries[1] = ActionSummary{100.0, 100.0, 25.0, 20.0, 10.0,
                                     kA1Scenarios, kA1Scenarios};
  known_summaries[2] = ActionSummary{200.0, 200.0, 25.0, 22.0, 11.0,
                                     kA1Scenarios, kA1Scenarios};
  known_summaries[3] = ActionSummary{150.0, 150.0, 25.0, 21.0, 10.0,
                                     kA1Scenarios, kA1Scenarios};
  expect(bestA1Action(known_summaries, known_legal) == 2,
         "synthetic known A1 winner failed");
  const auto retained = retainedActions(known_summaries, known_legal, 3);
  known_summaries[1].scenarios =
      known_summaries[2].scenarios =
          known_summaries[3].scenarios = kA2Scenarios;
  known_summaries[1].survivors =
      known_summaries[2].survivors =
          known_summaries[3].survivors = kA2Scenarios;
  expect(selectA2Challenger(known_summaries, retained, 3) == 2,
         "synthetic known A2 winner failed");
  const ActionPanel b_baseline = constantPanel(100.0, 75.0, 20, 10, true);
  const ActionPanel b_winner = constantPanel(200.0, 75.0, 21, 11, true);
  const ActionPanel b_loser = constantPanel(50.0, 75.0, 21, 11, true);
  expect(confirmB(b_winner, b_baseline).passed &&
             !confirmB(b_loser, b_baseline).passed,
         "synthetic B known-winner/loser confidence gate failed");

  std::vector<RootResult> passing_roots;
  passing_roots.reserve(kExpectedRecords);
  for (int origin = 0; origin < kExpectedOrigins; ++origin) {
    for (int root = 0; root < frozen::kExpectedGameRecords[origin]; ++root) {
      passing_roots.push_back(syntheticGateRoot(origin));
    }
  }
  const GateResult passing_gate = evaluateGate(passing_roots);
  expect(passing_gate.passed && passing_gate.nonregressing_origins == 8,
         "synthetic complete admission gate failed");
  for (RootResult& root : passing_roots) {
    root.c_beneficial = false;
  }
  const GateResult precision_failure = evaluateGate(passing_roots);
  expect(!precision_failure.precision_gate && !precision_failure.passed,
         "sub-threshold precision should reject B0");

  const auto unique = static_cast<unsigned long long>(
      Clock::now().time_since_epoch().count());
  const std::string checkpoint =
      (std::filesystem::temp_directory_path() /
       ("drop7-prpi-b0-selftest-" + std::to_string(unique) + ".jsonl"))
          .string();
  std::vector<RootResult> checkpoint_roots;
  checkpoint_roots.push_back(syntheticGateRoot(0));
  checkpoint_roots.back().public_hash = 0x1234;
  checkpoint_roots.push_back(syntheticGateRoot(0));
  checkpoint_roots.back().public_hash = 0x5678;
  const std::string source_sha(64, 'a');
  saveCheckpoint(checkpoint, source_sha, checkpoint_roots);
  const std::vector<RootResult> round_trip =
      loadCheckpoint(checkpoint, source_sha);
  expect(round_trip == checkpoint_roots,
         "atomic checkpoint round trip failed");
  {
    std::ofstream corrupt(checkpoint, std::ios::app);
    corrupt << 'x';
  }
  expect(throwsAny([&] { (void)loadCheckpoint(checkpoint, source_sha); }),
         "checkpoint corruption was not rejected");
  std::error_code remove_error;
  std::filesystem::remove(checkpoint, remove_error);
  std::filesystem::remove(checkpoint + ".tmp", remove_error);

  expect(kPanelAMasterDomain != kPanelBMasterDomain &&
             kPanelAMasterDomain != kPanelCMasterDomain &&
             kPanelBMasterDomain != kPanelCMasterDomain &&
             !std::is_invocable_v<PublicD2Boundary,
                                  const frozen::PanelRecord&>,
         "public capability boundary changed");
  output << "PUBLIC_REGENERATIVE_POLICY_ITERATION_B0_SELF_TEST {"
         << "\"passed\":true,\"corpusOpened\":false,"
         << "\"gameplaySeedsOpened\":0,\"publicOnly\":true,"
         << "\"nonanticipative\":true,\"reflection\":true,"
         << "\"actionMapping\":true,\"commonSiblingDomains\":true,"
         << "\"nestedA1A2\":true,\"independentBC\":true,"
         << "\"exactStratification\":true,\"knownWinner\":true,"
         << "\"checkpointAtomic\":true,\"checkpointCorruptionRejected\":true,"
         << "\"maximumTransitions\":" << kMaximumSyntheticTransitions
         << ",\"maximumD2Calls\":" << kMaximumD2Calls
         << ",\"maximumD2Work\":" << kMaximumD2LogicalWork
         << ",\"maximumLogicalWork\":" << kMaximumLogicalWork
         << ",\"peakRssBytes\":" << frozen::peakRssBytes() << "}\n";
  return true;
}

}  // namespace drop7::public_regenerative_policy_iteration_b0

#ifndef DROP7_PUBLIC_REGENERATIVE_POLICY_ITERATION_B0_LIBRARY
int main(int argc, char** argv) {
  try {
    using namespace drop7::public_regenerative_policy_iteration_b0;
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
      return selfTest(std::cout) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--preflight") {
      return preflight(parsePreflightOptions(argc, argv, 2), std::cout);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      return run(parseRunOptions(argc, argv, 2), std::cout);
    }
    std::cerr
        << "usage: drop7_public_regenerative_policy_iteration_b0 --self-test | "
           "--preflight --source PATH --source-sha256 HASH [--threads 4] "
           "[--output PATH] | --run --source PATH --source-sha256 HASH "
           "--preflight PATH [--input PATH] [--input-sha256 HASH] "
           "[--checkpoint PATH] [--output PATH] [--threads 4]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_public_regenerative_policy_iteration_b0: "
              << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
#endif

namespace drop7::public_regenerative_policy_iteration_b0 {

std::string serializeRoot(std::size_t index, const RootResult& result) {
  std::ostringstream output;
  output << std::setprecision(17) << "{\"index\":" << index
         << ",\"publicHash\":\"" << frozen::hex64(result.public_hash)
         << "\",\"origin\":" << result.origin_slot
         << ",\"d4Action\":" << result.d4_action
         << ",\"A1Action\":" << result.a1_action
         << ",\"A2Action\":" << result.a2_action
         << ",\"finalAction\":" << result.final_action
         << ",\"AStable\":" << (result.a_stable ? "true" : "false")
         << ",\"switched\":" << (result.switched ? "true" : "false")
         << ",\"BEvaluated\":" << (result.b.evaluated ? "true" : "false")
         << ",\"BPassed\":" << (result.b.passed ? "true" : "false")
         << ",\"BUtilityLcb\":" << result.b.utility_lcb
         << ",\"BMovesLcb\":" << result.b.moves_lcb
         << ",\"CBeneficial\":"
         << (result.c_beneficial ? "true" : "false");
  writeSummaryFields(output, "BChallenger", result.b.challenger);
  writeSummaryFields(output, "BD4", result.b.d4);
  writeSummaryFields(output, "CFinal", result.c_final);
  writeSummaryFields(output, "CD4", result.c_d4);
  output << ",\"transitions\":" << result.work.transitions
         << ",\"d2Calls\":" << result.work.d2.calls
         << ",\"d2Work\":" << result.work.d2.work
         << ",\"d2Nodes\":" << result.work.d2.nodes
         << ",\"d2CacheHits\":" << result.work.d2.cache_hits
         << ",\"d2RootActions\":" << result.work.d2.root_actions
         << ",\"d2PeakCache\":" << result.work.d2.peak_cache_entries
         << ",\"d2FullRoot\":"
         << (result.work.d2.full_root ? "true" : "false")
         << ",\"d4Work\":" << result.work.d4_work
         << ",\"d4Nodes\":" << result.work.d4_nodes
         << ",\"d4PeakCache\":" << result.work.peak_d4_cache_entries
         << ",\"seconds\":" << result.seconds << "}";
  return output.str();
}

RootResult parseRoot(std::string_view line, std::size_t expected_index) {
  if (frozen::integerAfter(line, "\"index\":") !=
      static_cast<long long>(expected_index)) {
    throw std::runtime_error("checkpoint result index is not a prefix");
  }
  RootResult result;
  result.public_hash = frozen::parseHex64(
      frozen::stringAfter(line, "\"publicHash\":\""));
  result.origin_slot =
      static_cast<int>(frozen::integerAfter(line, "\"origin\":"));
  result.d4_action =
      static_cast<int>(frozen::integerAfter(line, "\"d4Action\":"));
  result.a1_action =
      static_cast<int>(frozen::integerAfter(line, "\"A1Action\":"));
  result.a2_action =
      static_cast<int>(frozen::integerAfter(line, "\"A2Action\":"));
  result.final_action =
      static_cast<int>(frozen::integerAfter(line, "\"finalAction\":"));
  result.a_stable = frozen::booleanAfter(line, "\"AStable\":");
  result.switched = frozen::booleanAfter(line, "\"switched\":");
  result.b.evaluated = frozen::booleanAfter(line, "\"BEvaluated\":");
  result.b.passed = frozen::booleanAfter(line, "\"BPassed\":");
  result.b.utility_lcb = frozen::numberAfter(line, "\"BUtilityLcb\":");
  result.b.moves_lcb = frozen::numberAfter(line, "\"BMovesLcb\":");
  result.c_beneficial = frozen::booleanAfter(line, "\"CBeneficial\":");
  result.b.challenger = parseSummary(line, "BChallenger");
  result.b.d4 = parseSummary(line, "BD4");
  result.c_final = parseSummary(line, "CFinal");
  result.c_d4 = parseSummary(line, "CD4");
  result.work.transitions =
      static_cast<std::uint64_t>(frozen::integerAfter(line, "\"transitions\":"));
  result.work.d2.calls =
      static_cast<std::uint64_t>(frozen::integerAfter(line, "\"d2Calls\":"));
  result.work.d2.work =
      static_cast<std::uint64_t>(frozen::integerAfter(line, "\"d2Work\":"));
  result.work.d2.nodes =
      static_cast<std::uint64_t>(frozen::integerAfter(line, "\"d2Nodes\":"));
  result.work.d2.cache_hits = static_cast<std::uint64_t>(
      frozen::integerAfter(line, "\"d2CacheHits\":"));
  result.work.d2.root_actions = static_cast<std::uint64_t>(
      frozen::integerAfter(line, "\"d2RootActions\":"));
  result.work.d2.peak_cache_entries = static_cast<std::size_t>(
      frozen::integerAfter(line, "\"d2PeakCache\":"));
  result.work.d2.full_root = frozen::booleanAfter(line, "\"d2FullRoot\":");
  result.work.d4_work =
      static_cast<std::uint64_t>(frozen::integerAfter(line, "\"d4Work\":"));
  result.work.d4_nodes =
      static_cast<std::uint64_t>(frozen::integerAfter(line, "\"d4Nodes\":"));
  result.work.peak_d4_cache_entries = static_cast<std::size_t>(
      frozen::integerAfter(line, "\"d4PeakCache\":"));
  result.seconds = frozen::numberAfter(line, "\"seconds\":");
  if (result.origin_slot < 0 || result.origin_slot >= kExpectedOrigins ||
      result.d4_action < 0 || result.d4_action >= kBoardSize ||
      result.a1_action < 0 || result.a1_action >= kBoardSize ||
      result.a2_action < 0 || result.a2_action >= kBoardSize ||
      result.final_action < 0 || result.final_action >= kBoardSize ||
      result.work.transitions > kMaximumTransitionsPerRoot ||
      result.work.d2.calls > kMaximumD2CallsPerRoot ||
      result.work.d2.work > kMaximumD2CallsPerRoot * kMaximumD2Work ||
      !result.work.d2.full_root || result.seconds < 0.0) {
    throw std::runtime_error("checkpoint result failed domain validation");
  }
  return result;
}

}  // namespace drop7::public_regenerative_policy_iteration_b0
