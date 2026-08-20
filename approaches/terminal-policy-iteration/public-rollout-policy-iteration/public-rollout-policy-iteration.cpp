#define DROP7_FAIR_ONLY_HORIZON_LIBRARY
#include "../../fair-expectimax/reference/fair-only-horizon.cpp"
#undef DROP7_FAIR_ONLY_HORIZON_LIBRARY

#include <bit>
#include <filesystem>
#include <optional>
#include <sstream>
#include <type_traits>

// Performs one-step public policy improvement with a policy fixed before
// evaluation.  Each legal root action is evaluated on fifteen common,
// event-indexed synthetic chance tapes.
// After the fixed root action, every decision is a newly completed exact
// fair-D1 search with five chance strata.  Neither policy can receive an
// origin game seed, score, level, move index, history, scenario, or tape.
namespace drop7::public_rollout_policy_iteration {

namespace fair = drop7::fair_only_horizon;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kFittingSeedStart = 0x3d60'0000u;
constexpr std::uint32_t kDevelopmentSeedStart = 0x4d60'0000u;
constexpr int kFittingGames = 4;
constexpr int kDevelopmentGames = 8;
constexpr int kFittingMaximumMoves = 500;
constexpr int kDevelopmentMaximumMoves = 1'000;
constexpr int kScenarios = 15;
constexpr int kHorizon = 50;
constexpr int kContinuationDepth = 1;
constexpr int kContinuationStrata = 5;
constexpr int kDefaultThreads = 4;
constexpr int kEventsPerStep = 64;
constexpr double kTerminalPenalty = -1'000'000.0;
constexpr double kFittingScoreRatio = 1.20;
constexpr double kFittingMoveRatio = 1.20;
constexpr int kFittingJointWins = 3;
constexpr double kDevelopmentScoreRatio = 1.15;
constexpr double kDevelopmentMoveRatio = 1.15;
constexpr double kT95Df3 = 2.3533634348;
constexpr double kT95Df7 = 1.8945786051;
constexpr double kT95Df14 = 1.7613101358;
constexpr double kWallLimitSeconds = 30.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr std::uint32_t kTapeSeedDomain = 0x5052'5049u;  // "PRPI"
constexpr std::uint32_t kRevealTapeDomain = 0x5052'5256u;  // "PRRV"
constexpr std::uint32_t kVisibleTapeDomain = 0x5052'5653u; // "PRVS"
constexpr std::array<int, kBoardSize> kColumnOrder{{3, 2, 4, 1, 5, 0, 6}};

constexpr std::uint64_t kMaximumFairWorkPerCall =
    2ull * kBoardSize * kContinuationStrata;
constexpr std::uint64_t kMaximumFairCallsPerDecision =
    static_cast<std::uint64_t>(kBoardSize) * kScenarios * (kHorizon - 1) + 1;
constexpr std::uint64_t kMaximumFairWorkPerDecision =
    kMaximumFairCallsPerDecision * kMaximumFairWorkPerCall;
constexpr std::uint64_t kMaximumSyntheticTransitionsPerDecision =
    static_cast<std::uint64_t>(kBoardSize) * kScenarios * kHorizon;

static_assert(kLevelBonus == 17'000);
static_assert(kMovesPerLevel == 5);
static_assert(fair::kChanceSamples == kContinuationStrata);
static_assert(fair::kTerminalUtility == kTerminalPenalty);
static_assert(kScenarios == 15 && kHorizon == 50);
static_assert(kEventsPerStep > kCellCount);
static_assert(kMaximumFairWorkPerCall == 70);
static_assert(kMaximumFairCallsPerDecision == 5'146);
static_assert(kMaximumFairWorkPerDecision == 360'220);
static_assert(kMaximumSyntheticTransitionsPerDecision == 5'250);
static_assert(kFittingSeedStart + kFittingGames < kDevelopmentSeedStart);
static_assert((kFittingSeedStart >> 24u) == 0x3du);
static_assert((kDevelopmentSeedStart >> 24u) == 0x4du);
static_assert((kFittingSeedStart >> 24u) != 0x7du &&
              (kFittingSeedStart >> 24u) != 0xd7u);
static_assert((kDevelopmentSeedStart >> 24u) != 0x7du &&
              (kDevelopmentSeedStart >> 24u) != 0xd7u);

std::mutex report_mutex;

struct Options {
  std::string output = "/tmp/drop7-public-rollout-policy-iteration.json";
  std::string teacher_output =
      "/tmp/drop7-public-rollout-policy-iteration.jsonl";
  int threads = kDefaultThreads;
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
    } else if (argument == "--threads") {
      result.threads = std::stoi(argv[index + 1]);
      if (result.threads < 1 || result.threads > 16) {
        throw std::invalid_argument("threads must be in [1,16]");
      }
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  return result;
}

struct ObservableState {
  Board board{};
  std::uint8_t next_disc = 1;
  std::uint8_t moves_remaining = kMovesPerLevel;
  bool terminal = false;

  bool operator==(const ObservableState&) const = default;
};

ObservableState observable(const State& source) {
  if (source.next_disc < 1 || source.next_disc > kBoardSize ||
      source.moves_remaining < 0 || source.moves_remaining > kMovesPerLevel ||
      (!source.game_over && source.moves_remaining < 1)) {
    throw std::invalid_argument("invalid observable state metadata");
  }
  for (const std::uint8_t cell : source.board) {
    if (cell > kCracked) {
      throw std::invalid_argument("invalid observable board token");
    }
  }
  return {source.board, source.next_disc,
          static_cast<std::uint8_t>(source.moves_remaining), source.game_over};
}

State materialize(const ObservableState& source) {
  State result;
  result.board = source.board;
  result.next_disc = source.next_disc;
  result.score = 0;
  result.level = 1;
  result.moves_remaining = source.moves_remaining;
  result.moves_played = 0;
  result.game_over = source.terminal;
  return result;
}

ObservableState mirror(const ObservableState& source) {
  ObservableState result = source;
  result.board = cfpi::detail::mirrorBoard(source.board);
  return result;
}

ObservableState canonicalObservable(const ObservableState& source,
                                    bool& mirrored) {
  const State canonical =
      cfpi::detail::canonicalState(materialize(source), mirrored);
  return observable(canonical);
}

std::uint64_t mix64(std::uint64_t value) {
  value ^= value >> 30u;
  value *= 0xbf58'476d'1ce4'e5b9ull;
  value ^= value >> 27u;
  value *= 0x94d0'49bb'1331'11ebull;
  return value ^ (value >> 31u);
}

void hashCombine(std::uint64_t& hash, std::uint64_t value) {
  hash = mix64(hash ^ mix64(value + 0x9e37'79b9'7f4a'7c15ull));
}

std::uint64_t publicHash(const ObservableState& source) {
  bool ignored = false;
  const ObservableState state = canonicalObservable(source, ignored);
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint64_t>(cell + 1u);
    hash *= 0x0000'0100'0000'01b3ull;
  }
  hash ^= state.next_disc;
  hash *= 0x0000'0100'0000'01b3ull;
  hash ^= static_cast<std::uint64_t>(state.moves_remaining + 1u);
  hash *= 0x0000'0100'0000'01b3ull;
  hash ^= static_cast<std::uint64_t>(state.terminal);
  return mix64(hash);
}

std::uint32_t seed32(std::uint64_t value) {
  return mix32(static_cast<std::uint32_t>(value) ^
               static_cast<std::uint32_t>(value >> 32u));
}

std::uint32_t tapeSeed(const ObservableState& canonical_root) {
  return seed32(publicHash(canonical_root) ^
                static_cast<std::uint64_t>(kTapeSeedDomain));
}

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
  const std::uint64_t rss = peakRssBytes();
  if (rss > kRssLimitBytes) {
    throw std::runtime_error("public rollout exceeded 256 MiB RSS limit");
  }
}

struct Deadline {
  Clock::time_point started = Clock::now();

  void check() const {
    const double elapsed =
        std::chrono::duration<double>(Clock::now() - started).count();
    if (elapsed > kWallLimitSeconds) {
      throw std::runtime_error("public rollout exceeded 30 minute wall cap");
    }
  }

  double elapsedSeconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }
};

struct FairD1Decision {
  int action = -1;
  std::array<double, kBoardSize> values{};
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::size_t cache_entries = 0;
  int evaluated_actions = 0;
  bool complete = false;

  bool operator==(const FairD1Decision&) const = default;
};

// This is the complete continuation-policy boundary.  Its sole argument type
// contains only board, visible next disc, five-drop phase, and terminal flag.
FairD1Decision chooseFairDepthOne(const ObservableState& source) {
  FairD1Decision result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  if (source.terminal) return result;
  bool mirrored = false;
  const ObservableState canonical = canonicalObservable(source, mirrored);
  fair::SearchContext context;
  const fair::RootEvaluation root =
      fair::rootDecision(materialize(canonical), kContinuationDepth, context);
  int legal_actions = 0;
  int evaluated_actions = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    legal_actions += isLegal(canonical.board, column);
    evaluated_actions += std::isfinite(root.values[column]);
  }
  if (root.action < 0 || evaluated_actions != legal_actions ||
      context.work > kMaximumFairWorkPerCall || !context.cache.empty()) {
    throw std::runtime_error("fair D1 did not complete its full public root");
  }
  result.action = mirrored ? kBoardSize - 1 - root.action : root.action;
  for (int canonical_column = 0; canonical_column < kBoardSize;
       ++canonical_column) {
    const int source_column = mirrored
                                  ? kBoardSize - 1 - canonical_column
                                  : canonical_column;
    result.values[source_column] = root.values[canonical_column];
  }
  result.work = context.work;
  result.nodes = context.nodes;
  result.cache_hits = context.cache_hits;
  result.cache_entries = context.cache.size();
  result.evaluated_actions = evaluated_actions;
  result.complete = true;
  return result;
}

using PublicContinuation = FairD1Decision (*)(const ObservableState&);
static_assert(std::is_same_v<decltype(&chooseFairDepthOne),
                             PublicContinuation>);
static_assert(!std::is_invocable_v<PublicContinuation, const State&>);

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
    if (event >= kEventsPerStep) {
      throw std::runtime_error("synthetic reveal tape exhausted step slice");
    }
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
                       MoveResult& result, TapeDomains domains = {}) {
  if (source.terminal || scenario < 0 || scenario >= kScenarios || step < 0 ||
      step >= kHorizon || !isLegal(source.board, action)) {
    return false;
  }
  Board board = source.board;
  if (!placeDisc(board, action, source.next_disc)) return false;

  RevealTape reveals{root_seed, scenario, step, domains.reveal, 0};
  result = MoveResult{};
  std::int64_t score = 0;
  cfpi::detail::resolveCascadeSampled(board, reveals, 1, score, result.waves);
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
      const int next_depth =
          result.waves.empty() ? 1 : result.waves.back().depth + 1;
      cfpi::detail::resolveCascadeSampled(board, reveals, next_depth,
                                          rise_score, result.waves);
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
  result.state.next_disc =
      terminal ? source.next_disc
               : visibleDisc(root_seed, scenario, step, domains.visible);
  result.state.score = 0;
  result.state.level = 1;
  result.state.moves_remaining = moves_remaining;
  result.state.moves_played = 0;
  result.state.game_over = terminal;
  return true;
}

struct WorkMetrics {
  std::uint64_t synthetic_transitions = 0;
  std::uint64_t fair_d1_calls = 0;
  std::uint64_t fair_work = 0;
  std::uint64_t fair_nodes = 0;
  std::uint64_t fair_cache_hits = 0;
  std::size_t peak_fair_cache_entries = 0;
  std::uint64_t fair_root_actions = 0;
  bool full_fair_roots = true;

  bool operator==(const WorkMetrics&) const = default;

  WorkMetrics& operator+=(const WorkMetrics& other) {
    synthetic_transitions += other.synthetic_transitions;
    fair_d1_calls += other.fair_d1_calls;
    fair_work += other.fair_work;
    fair_nodes += other.fair_nodes;
    fair_cache_hits += other.fair_cache_hits;
    peak_fair_cache_entries =
        std::max(peak_fair_cache_entries, other.peak_fair_cache_entries);
    fair_root_actions += other.fair_root_actions;
    full_fair_roots = full_fair_roots && other.full_fair_roots;
    return *this;
  }
};

void observeFairDecision(const FairD1Decision& decision,
                         WorkMetrics& metrics) {
  ++metrics.fair_d1_calls;
  metrics.fair_work += decision.work;
  metrics.fair_nodes += decision.nodes;
  metrics.fair_cache_hits += decision.cache_hits;
  metrics.peak_fair_cache_entries =
      std::max(metrics.peak_fair_cache_entries, decision.cache_entries);
  metrics.fair_root_actions +=
      static_cast<std::uint64_t>(decision.evaluated_actions);
  metrics.full_fair_roots = metrics.full_fair_roots && decision.complete;
}

struct ScenarioOutcome {
  double value = 0.0;
  int moves = 0;
  int numbered_clears = 0;
  int covers_revealed = 0;
  bool survived_horizon = false;

  bool operator==(const ScenarioOutcome&) const = default;
};

struct PairedAudit {
  double mean_difference = 0.0;
  double standard_error = 0.0;
  double lower_one_sided_95 = 0.0;
  double upper_one_sided_95 = 0.0;
  int wins = 0;
  int ties = 0;
  int losses = 0;

  bool operator==(const PairedAudit&) const = default;
};

struct ActionRollout {
  std::array<ScenarioOutcome, kScenarios> scenarios{};
  double mean_return = -std::numeric_limits<double>::infinity();
  double mean_numbered_clears = 0.0;
  double mean_covers_revealed = 0.0;
  int surviving_scenarios = 0;
  PairedAudit paired_vs_fair_d1{};

  bool operator==(const ActionRollout&) const = default;
};

PairedAudit pairedScenarioAudit(const ActionRollout& candidate,
                                const ActionRollout& baseline) {
  PairedAudit result;
  std::array<double, kScenarios> differences{};
  for (int scenario = 0; scenario < kScenarios; ++scenario) {
    differences[scenario] = candidate.scenarios[scenario].value -
                            baseline.scenarios[scenario].value;
    result.mean_difference += differences[scenario] / kScenarios;
    result.wins += differences[scenario] > 0.0;
    result.ties += differences[scenario] == 0.0;
    result.losses += differences[scenario] < 0.0;
  }
  double squares = 0.0;
  for (const double difference : differences) {
    const double centered = difference - result.mean_difference;
    squares += centered * centered;
  }
  const double deviation =
      std::sqrt(squares / static_cast<double>(kScenarios - 1));
  result.standard_error = deviation / std::sqrt(kScenarios);
  result.lower_one_sided_95 =
      result.mean_difference - kT95Df14 * result.standard_error;
  result.upper_one_sided_95 =
      result.mean_difference + kT95Df14 * result.standard_error;
  return result;
}

struct RolloutEvaluation {
  std::array<ActionRollout, kBoardSize> actions{};
  std::array<bool, kBoardSize> legal{};
  int legal_actions = 0;
  int action = -1;
  int fair_d1_action = -1;
  int runner_up_action = -1;
  PairedAudit chosen_vs_runner_up{};
  WorkMetrics work{};
  std::uint64_t canonical_public_hash = 0;
  std::uint32_t tape_seed = 0;

  bool operator==(const RolloutEvaluation&) const = default;
};

ScenarioOutcome rolloutScenario(const ObservableState& root, int root_action,
                                std::uint32_t root_seed, int scenario,
                                WorkMetrics& work, const Deadline* deadline,
                                int horizon = kHorizon) {
  if (root.terminal || !isLegal(root.board, root_action) || horizon < 1 ||
      horizon > kHorizon) {
    throw std::invalid_argument("invalid public rollout scenario");
  }
  ObservableState state = root;
  ScenarioOutcome result;
  for (int step = 0; step < horizon; ++step) {
    if (deadline != nullptr) deadline->check();
    int action = root_action;
    if (step > 0) {
      const FairD1Decision continuation = chooseFairDepthOne(state);
      observeFairDecision(continuation, work);
      action = continuation.action;
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("public continuation selected illegal action");
    }
    MoveResult move;
    if (!playSyntheticMove(state, action, root_seed, scenario, step, move)) {
      throw std::runtime_error("synthetic public transition failed");
    }
    ++work.synthetic_transitions;
    result.value += static_cast<double>(move.score_delta);
    ++result.moves;
    for (const Wave& wave : move.waves) {
      result.numbered_clears += wave.cleared;
      result.covers_revealed += wave.revealed;
    }
    state = observable(move.state);
    if (state.terminal) {
      result.value += kTerminalPenalty;
      return result;
    }
  }
  result.survived_horizon = true;
  result.value += fair::fairLeaf(materialize(state));
  return result;
}

int selectMeanAction(const RolloutEvaluation& evaluation,
                     std::optional<int> omitted = std::nullopt) {
  int selected = -1;
  double best = -std::numeric_limits<double>::infinity();
  for (const int action : kColumnOrder) {
    if (!evaluation.legal[action] ||
        (omitted.has_value() && action == *omitted)) {
      continue;
    }
    const double value = evaluation.actions[action].mean_return;
    if (value > best) {
      best = value;
      selected = action;
    }
  }
  return selected;
}

RolloutEvaluation evaluateRollouts(const ObservableState& source,
                                   const Deadline* deadline = nullptr,
                                   int horizon = kHorizon) {
  if (source.terminal || horizon < 1 || horizon > kHorizon) {
    throw std::invalid_argument("invalid public rollout root");
  }
  RolloutEvaluation result;
  bool mirrored = false;
  const ObservableState root = canonicalObservable(source, mirrored);
  result.canonical_public_hash = publicHash(root);
  result.tape_seed = tapeSeed(root);

  for (const int action : kColumnOrder) {
    if (!isLegal(root.board, action)) continue;
    result.legal[action] = true;
    ++result.legal_actions;
    ActionRollout& action_result = result.actions[action];
    action_result.mean_return = 0.0;
    for (int scenario = 0; scenario < kScenarios; ++scenario) {
      ScenarioOutcome& outcome = action_result.scenarios[scenario];
      outcome = rolloutScenario(root, action, result.tape_seed, scenario,
                                result.work, deadline, horizon);
      action_result.mean_return += outcome.value / kScenarios;
      action_result.mean_numbered_clears +=
          static_cast<double>(outcome.numbered_clears) / kScenarios;
      action_result.mean_covers_revealed +=
          static_cast<double>(outcome.covers_revealed) / kScenarios;
      action_result.surviving_scenarios += outcome.survived_horizon;
    }
  }
  const FairD1Decision fair_decision = chooseFairDepthOne(root);
  observeFairDecision(fair_decision, result.work);
  result.fair_d1_action = fair_decision.action;
  result.action = selectMeanAction(result);
  result.runner_up_action = selectMeanAction(result, result.action);
  if (result.action < 0 || result.fair_d1_action < 0 ||
      !result.legal[result.action] || !result.legal[result.fair_d1_action]) {
    throw std::runtime_error("public rollout failed full legal root");
  }
  const ActionRollout& fair_rollout = result.actions[result.fair_d1_action];
  for (int action = 0; action < kBoardSize; ++action) {
    if (result.legal[action]) {
      result.actions[action].paired_vs_fair_d1 =
          pairedScenarioAudit(result.actions[action], fair_rollout);
    }
  }
  if (result.runner_up_action >= 0) {
    result.chosen_vs_runner_up = pairedScenarioAudit(
        result.actions[result.action], result.actions[result.runner_up_action]);
  }
  const std::uint64_t horizon_scale = static_cast<std::uint64_t>(horizon);
  const std::uint64_t maximum_synthetic =
      static_cast<std::uint64_t>(kBoardSize) * kScenarios * horizon_scale;
  const std::uint64_t maximum_calls =
      static_cast<std::uint64_t>(kBoardSize) * kScenarios *
          (horizon_scale - 1u) +
      1u;
  if (result.work.synthetic_transitions > maximum_synthetic ||
      result.work.fair_d1_calls > maximum_calls ||
      result.work.fair_work > maximum_calls * kMaximumFairWorkPerCall ||
      result.work.peak_fair_cache_entries != 0 ||
      !result.work.full_fair_roots) {
    throw std::runtime_error("public rollout exceeded frozen work bound");
  }

  if (!mirrored) return result;
  RolloutEvaluation reflected = result;
  for (int canonical_column = 0; canonical_column < kBoardSize;
       ++canonical_column) {
    const int source_column = kBoardSize - 1 - canonical_column;
    reflected.actions[source_column] = result.actions[canonical_column];
    reflected.legal[source_column] = result.legal[canonical_column];
  }
  reflected.action = kBoardSize - 1 - result.action;
  reflected.fair_d1_action = kBoardSize - 1 - result.fair_d1_action;
  reflected.runner_up_action = result.runner_up_action < 0
                                   ? -1
                                   : kBoardSize - 1 - result.runner_up_action;
  return reflected;
}

struct RootRecord {
  ObservableState state{};
  RolloutEvaluation evaluation{};
};

enum class SeedCohort { kFitting, kDevelopment };

bool allowedGameplaySeed(std::uint32_t seed, SeedCohort cohort) {
  const std::uint32_t start = cohort == SeedCohort::kFitting
                                  ? kFittingSeedStart
                                  : kDevelopmentSeedStart;
  const int count =
      cohort == SeedCohort::kFitting ? kFittingGames : kDevelopmentGames;
  return seed >= start && seed < start + static_cast<std::uint32_t>(count) &&
         (seed >> 24u) != 0x7du && (seed >> 24u) != 0xd7u;
}

void requireGameplaySeed(std::uint32_t seed, SeedCohort cohort) {
  if (!allowedGameplaySeed(seed, cohort)) {
    throw std::invalid_argument("game seed is outside frozen allowlist");
  }
}

std::uint64_t discStreamHash(std::uint32_t seed, int maximum_moves) {
  std::uint64_t hash = 0x9e37'79b9'7f4a'7c15ull;
  for (int move = 0; move < maximum_moves; ++move) {
    hashCombine(hash, headlessDisc(seed, move));
  }
  return hash;
}

std::uint64_t evaluationChecksum(const ObservableState& state,
                                 const RolloutEvaluation& evaluation) {
  std::uint64_t hash = publicHash(state);
  hashCombine(hash, static_cast<std::uint64_t>(evaluation.action + 1));
  hashCombine(hash, evaluation.tape_seed);
  for (int action = 0; action < kBoardSize; ++action) {
    hashCombine(hash, evaluation.legal[action]);
    if (!evaluation.legal[action]) continue;
    for (const ScenarioOutcome& outcome : evaluation.actions[action].scenarios) {
      hashCombine(hash, std::bit_cast<std::uint64_t>(outcome.value));
      hashCombine(hash, static_cast<std::uint64_t>(outcome.numbered_clears));
      hashCombine(hash, static_cast<std::uint64_t>(outcome.covers_revealed));
      hashCombine(hash, outcome.survived_horizon);
    }
  }
  return hash;
}

enum class Policy { kFairD1, kRollout };

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::int64_t numbered_clears = 0;
  std::int64_t covers_revealed = 0;
  int cleared_boards = 0;
  int maximum_chain = 0;
  double decision_seconds = 0.0;
  WorkMetrics work{};
  std::uint64_t disc_stream_hash = 0;
  std::uint64_t root_checksum = 0;
  std::uint64_t tape_checksum = 0;
  std::vector<RootRecord> roots;
};

void observeMove(const MoveResult& move, GameResult& result) {
  if (move.cleared_board) ++result.cleared_boards;
  for (const Wave& wave : move.waves) {
    result.numbered_clears += wave.cleared;
    result.covers_revealed += wave.revealed;
    result.maximum_chain = std::max(result.maximum_chain, wave.depth);
  }
}

GameResult runGame(std::uint32_t seed, SeedCohort cohort, int maximum_moves,
                   Policy policy, const Deadline& deadline) {
  requireGameplaySeed(seed, cohort);
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  result.disc_stream_hash = discStreamHash(seed, maximum_moves);
  result.root_checksum = 0x524f'4f54'4841'5348ull;
  result.tape_checksum = 0x5441'5045'4841'5348ull;
  if (policy == Policy::kRollout) {
    result.roots.reserve(static_cast<std::size_t>(maximum_moves));
  }

  while (!state.game_over && state.moves_played < maximum_moves) {
    deadline.check();
    if (state.next_disc != headlessDisc(seed, state.moves_played)) {
      throw std::runtime_error("actual visible disc stream guard failed");
    }
    const ObservableState public_state = observable(state);
    const auto decision_started = Clock::now();
    int action = -1;
    if (policy == Policy::kFairD1) {
      const FairD1Decision decision = chooseFairDepthOne(public_state);
      observeFairDecision(decision, result.work);
      action = decision.action;
    } else {
      RolloutEvaluation evaluation = evaluateRollouts(public_state, &deadline);
      action = evaluation.action;
      result.work += evaluation.work;
      hashCombine(result.root_checksum,
                  evaluationChecksum(public_state, evaluation));
      hashCombine(result.tape_checksum, evaluation.tape_seed);
      result.roots.push_back({public_state, std::move(evaluation)});
    }
    result.decision_seconds += std::chrono::duration<double>(
                                   Clock::now() - decision_started)
                                   .count();
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("actual policy selected illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("actual headless transition failed");
    }
    observeMove(move, result);
    enforceRssLimit();
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  if (policy == Policy::kRollout &&
      result.roots.size() != static_cast<std::size_t>(result.moves)) {
    throw std::runtime_error("candidate root export count mismatch");
  }
  return result;
}

struct PairedGame {
  GameResult fair_d1;
  GameResult candidate;
};

struct Cohort {
  std::vector<PairedGame> games;
  double wall_seconds = 0.0;
};

Cohort runCohort(std::uint32_t start, int games, SeedCohort seed_cohort,
                 int maximum_moves, int threads, const Deadline& deadline,
                 std::string_view label) {
  const auto started = Clock::now();
  Cohort result;
  result.games.resize(static_cast<std::size_t>(games));
  std::atomic<int> next{0};
  std::atomic<int> completed{0};
  std::vector<std::future<void>> workers;
  const int worker_count = std::max(1, std::min(threads, games));
  workers.reserve(static_cast<std::size_t>(worker_count));
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [&, worker]() {
      static_cast<void>(worker);
      for (;;) {
        const int game = next.fetch_add(1);
        if (game >= games) return;
        const std::uint32_t seed = start + static_cast<std::uint32_t>(game);
        PairedGame pair;
        pair.fair_d1 = runGame(seed, seed_cohort, maximum_moves,
                               Policy::kFairD1, deadline);
        pair.candidate = runGame(seed, seed_cohort, maximum_moves,
                                 Policy::kRollout, deadline);
        if (pair.fair_d1.disc_stream_hash !=
            pair.candidate.disc_stream_hash) {
          throw std::runtime_error("paired actual disc stream hashes differ");
        }
        result.games[static_cast<std::size_t>(game)] = std::move(pair);
        const int done = completed.fetch_add(1) + 1;
        const PairedGame& stored = result.games[static_cast<std::size_t>(game)];
        const std::lock_guard<std::mutex> lock(report_mutex);
        std::cerr << "public-rollout " << label << ' ' << done << '/' << games
                  << " seed 0x" << std::hex << seed << std::dec
                  << " fair=" << stored.fair_d1.score << '/'
                  << stored.fair_d1.moves << " candidate="
                  << stored.candidate.score << '/' << stored.candidate.moves
                  << '\n';
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

struct Summary {
  int games = 0;
  int natural = 0;
  int censored = 0;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double numbered_clears_per_move = 0.0;
  double covers_revealed_per_move = 0.0;
  double mean_decision_ms = 0.0;
  std::int64_t total_numbered_clears = 0;
  std::int64_t total_covers_revealed = 0;
  std::int64_t total_moves = 0;
  int total_roots = 0;
  WorkMetrics work{};
  std::uint64_t root_checksum = 0x5355'4d52'4f4f'5453ull;
  std::uint64_t tape_checksum = 0x5355'4d54'4150'4553ull;
};

Summary summarize(const Cohort& cohort, Policy policy) {
  if (cohort.games.empty()) throw std::invalid_argument("empty cohort");
  Summary result;
  result.games = static_cast<int>(cohort.games.size());
  double decision_seconds = 0.0;
  for (const PairedGame& pair : cohort.games) {
    const GameResult& game =
        policy == Policy::kFairD1 ? pair.fair_d1 : pair.candidate;
    result.mean_score += static_cast<double>(game.score) / result.games;
    result.mean_moves += static_cast<double>(game.moves) / result.games;
    result.natural += !game.censored;
    result.censored += game.censored;
    result.total_numbered_clears += game.numbered_clears;
    result.total_covers_revealed += game.covers_revealed;
    result.total_moves += game.moves;
    result.total_roots += static_cast<int>(game.roots.size());
    decision_seconds += game.decision_seconds;
    result.work += game.work;
    hashCombine(result.root_checksum, game.root_checksum);
    hashCombine(result.tape_checksum, game.tape_checksum);
  }
  if (result.total_moves > 0) {
    result.numbered_clears_per_move =
        static_cast<double>(result.total_numbered_clears) / result.total_moves;
    result.covers_revealed_per_move =
        static_cast<double>(result.total_covers_revealed) / result.total_moves;
    result.mean_decision_ms =
        1'000.0 * decision_seconds / result.total_moves;
  }
  return result;
}

struct DifferenceStats {
  double mean = 0.0;
  double standard_error = 0.0;
  double lower_one_sided_95 = 0.0;
  int wins = 0;
  int ties = 0;
  int losses = 0;
};

DifferenceStats differences(const std::vector<double>& values,
                            double critical) {
  if (values.size() < 2) {
    throw std::invalid_argument("paired inference requires at least two games");
  }
  DifferenceStats result;
  for (const double value : values) {
    result.mean += value / values.size();
    result.wins += value > 0.0;
    result.ties += value == 0.0;
    result.losses += value < 0.0;
  }
  double squares = 0.0;
  for (const double value : values) {
    const double centered = value - result.mean;
    squares += centered * centered;
  }
  const double deviation =
      std::sqrt(squares / static_cast<double>(values.size() - 1));
  result.standard_error = deviation / std::sqrt(values.size());
  result.lower_one_sided_95 = result.mean - critical * result.standard_error;
  return result;
}

struct PairedSummary {
  DifferenceStats score;
  DifferenceStats moves;
  int joint_score_move_wins = 0;
};

PairedSummary pairedSummary(const Cohort& cohort, double critical) {
  std::vector<double> scores;
  std::vector<double> moves;
  scores.reserve(cohort.games.size());
  moves.reserve(cohort.games.size());
  PairedSummary result;
  for (const PairedGame& pair : cohort.games) {
    const double score =
        static_cast<double>(pair.candidate.score - pair.fair_d1.score);
    const double move =
        static_cast<double>(pair.candidate.moves - pair.fair_d1.moves);
    scores.push_back(score);
    moves.push_back(move);
    result.joint_score_move_wins += score > 0.0 && move > 0.0;
  }
  result.score = differences(scores, critical);
  result.moves = differences(moves, critical);
  return result;
}

struct GateResult {
  double score_ratio = 0.0;
  double move_ratio = 0.0;
  bool score_ratio_passed = false;
  bool move_ratio_passed = false;
  bool reveal_rate_passed = false;
  bool joint_wins_passed = false;
  bool score_lower_bound_passed = false;
  bool move_lower_bound_passed = false;
  bool passed = false;
};

GateResult fittingGate(const Summary& baseline, const Summary& candidate,
                       const PairedSummary& paired) {
  GateResult result;
  result.score_ratio = candidate.mean_score / baseline.mean_score;
  result.move_ratio = candidate.mean_moves / baseline.mean_moves;
  result.score_ratio_passed = result.score_ratio >= kFittingScoreRatio;
  result.move_ratio_passed = result.move_ratio >= kFittingMoveRatio;
  result.reveal_rate_passed = candidate.covers_revealed_per_move >=
                              baseline.covers_revealed_per_move;
  result.joint_wins_passed =
      paired.joint_score_move_wins >= kFittingJointWins;
  result.passed = result.score_ratio_passed && result.move_ratio_passed &&
                  result.reveal_rate_passed && result.joint_wins_passed;
  return result;
}

GateResult developmentGate(const Summary& baseline, const Summary& candidate,
                           const PairedSummary& paired) {
  GateResult result;
  result.score_ratio = candidate.mean_score / baseline.mean_score;
  result.move_ratio = candidate.mean_moves / baseline.mean_moves;
  result.score_ratio_passed = result.score_ratio >= kDevelopmentScoreRatio;
  result.move_ratio_passed = result.move_ratio >= kDevelopmentMoveRatio;
  result.reveal_rate_passed = candidate.covers_revealed_per_move >=
                              baseline.covers_revealed_per_move;
  result.score_lower_bound_passed = paired.score.lower_one_sided_95 >= 0.0;
  result.move_lower_bound_passed = paired.moves.lower_one_sided_95 >= 0.0;
  result.passed = result.score_ratio_passed && result.move_ratio_passed &&
                  result.reveal_rate_passed &&
                  result.score_lower_bound_passed &&
                  result.move_lower_bound_passed;
  return result;
}

std::string hex64(std::uint64_t value) {
  std::ostringstream output;
  output << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
  return output.str();
}

std::string hex32(std::uint32_t value) {
  std::ostringstream output;
  output << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
  return output.str();
}

std::string jsonEscape(std::string_view source) {
  std::string result;
  for (const char character : source) {
    if (character == '\\' || character == '"') result.push_back('\\');
    result.push_back(character);
  }
  return result;
}

void writePairedAudit(std::ostream& output, const PairedAudit& audit) {
  output << "{\"meanDifference\":" << audit.mean_difference
         << ",\"standardError\":" << audit.standard_error
         << ",\"lowerOneSided95\":" << audit.lower_one_sided_95
         << ",\"upperOneSided95\":" << audit.upper_one_sided_95
         << ",\"wins\":" << audit.wins << ",\"ties\":" << audit.ties
         << ",\"losses\":" << audit.losses << '}';
}

void writeTeacherRoot(std::ostream& output, std::string_view cohort,
                      int game_ordinal, const RootRecord& record) {
  output << std::setprecision(12) << "{\"cohort\":\"" << cohort
         << "\",\"gameOrdinal\":" << game_ordinal
         << ",\"state\":{\"board\":\""
         << serializeBoard(record.state.board) << "\",\"nextDisc\":"
         << static_cast<int>(record.state.next_disc)
         << ",\"movesRemaining\":"
         << static_cast<int>(record.state.moves_remaining)
         << ",\"terminal\":" << (record.state.terminal ? "true" : "false")
         << "},\"canonicalPublicHash\":\""
         << hex64(record.evaluation.canonical_public_hash)
         << "\",\"tapeSeed\":\"" << hex32(record.evaluation.tape_seed)
         << "\",\"fairD1Action\":" << record.evaluation.fair_d1_action
         << ",\"chosenAction\":" << record.evaluation.action
         << ",\"runnerUpAction\":" << record.evaluation.runner_up_action
         << ",\"chosenVsRunnerUp\":";
  writePairedAudit(output, record.evaluation.chosen_vs_runner_up);
  output << ",\"actions\":[";
  for (int action = 0; action < kBoardSize; ++action) {
    if (action != 0) output << ',';
    if (!record.evaluation.legal[action]) {
      output << "null";
      continue;
    }
    const ActionRollout& rollout = record.evaluation.actions[action];
    output << "{\"action\":" << action << ",\"meanReturn\":"
           << rollout.mean_return << ",\"survivingScenarios\":"
           << rollout.surviving_scenarios
           << ",\"meanNumberedClears\":" << rollout.mean_numbered_clears
           << ",\"meanCoversRevealed\":" << rollout.mean_covers_revealed
           << ",\"pairedVsFairD1\":";
    writePairedAudit(output, rollout.paired_vs_fair_d1);
    output << ",\"scenarioReturns\":[";
    for (int scenario = 0; scenario < kScenarios; ++scenario) {
      if (scenario != 0) output << ',';
      output << rollout.scenarios[scenario].value;
    }
    output << "],\"survived\":[";
    for (int scenario = 0; scenario < kScenarios; ++scenario) {
      if (scenario != 0) output << ',';
      output << (rollout.scenarios[scenario].survived_horizon ? "true"
                                                                    : "false");
    }
    output << "],\"numberedClears\":[";
    for (int scenario = 0; scenario < kScenarios; ++scenario) {
      if (scenario != 0) output << ',';
      output << rollout.scenarios[scenario].numbered_clears;
    }
    output << "],\"coversRevealed\":[";
    for (int scenario = 0; scenario < kScenarios; ++scenario) {
      if (scenario != 0) output << ',';
      output << rollout.scenarios[scenario].covers_revealed;
    }
    output << "]}";
  }
  output << "]}\n";
}

std::uint64_t writeTeacher(const std::string& path, const Cohort& cohort,
                           std::string_view label, bool append) {
  std::ofstream output(path, append ? std::ios::app : std::ios::trunc);
  if (!output) throw std::runtime_error("could not open teacher JSONL");
  std::uint64_t records = 0;
  for (std::size_t game = 0; game < cohort.games.size(); ++game) {
    for (const RootRecord& root : cohort.games[game].candidate.roots) {
      writeTeacherRoot(output, label, static_cast<int>(game), root);
      ++records;
    }
  }
  output.close();
  if (!output) throw std::runtime_error("could not finish teacher JSONL");
  return records;
}

void writeWork(std::ostream& output, const WorkMetrics& work) {
  output << "{\"syntheticTransitions\":" << work.synthetic_transitions
         << ",\"fairD1Calls\":" << work.fair_d1_calls
         << ",\"fairWork\":" << work.fair_work
         << ",\"fairNodes\":" << work.fair_nodes
         << ",\"fairCacheHits\":" << work.fair_cache_hits
         << ",\"peakFairCacheEntries\":" << work.peak_fair_cache_entries
         << ",\"fairRootActions\":" << work.fair_root_actions
         << ",\"fullFairRoots\":"
         << (work.full_fair_roots ? "true" : "false") << '}';
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"games\":" << summary.games << ",\"natural\":"
         << summary.natural << ",\"censored\":" << summary.censored
         << ",\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"numberedClearsPerMove\":"
         << summary.numbered_clears_per_move
         << ",\"coversRevealedPerMove\":"
         << summary.covers_revealed_per_move
         << ",\"meanDecisionMs\":" << summary.mean_decision_ms
         << ",\"totalNumberedClears\":" << summary.total_numbered_clears
         << ",\"totalCoversRevealed\":" << summary.total_covers_revealed
         << ",\"totalMoves\":" << summary.total_moves
         << ",\"totalCandidateRoots\":" << summary.total_roots
         << ",\"rootChecksum\":\"" << hex64(summary.root_checksum)
         << "\",\"tapeChecksum\":\"" << hex64(summary.tape_checksum)
         << "\",\"work\":";
  writeWork(output, summary.work);
  output << '}';
}

void writeDifference(std::ostream& output, const DifferenceStats& difference) {
  output << "{\"mean\":" << difference.mean
         << ",\"standardError\":" << difference.standard_error
         << ",\"lowerOneSided95\":"
         << difference.lower_one_sided_95 << ",\"wins\":"
         << difference.wins << ",\"ties\":" << difference.ties
         << ",\"losses\":" << difference.losses << '}';
}

void writePairedSummary(std::ostream& output, const PairedSummary& paired) {
  output << "{\"score\":";
  writeDifference(output, paired.score);
  output << ",\"moves\":";
  writeDifference(output, paired.moves);
  output << ",\"jointScoreMoveWins\":" << paired.joint_score_move_wins
         << '}';
}

void writeGate(std::ostream& output, const GateResult& gate) {
  output << "{\"scoreRatio\":" << gate.score_ratio
         << ",\"moveRatio\":" << gate.move_ratio
         << ",\"scoreRatioPassed\":"
         << (gate.score_ratio_passed ? "true" : "false")
         << ",\"moveRatioPassed\":"
         << (gate.move_ratio_passed ? "true" : "false")
         << ",\"revealRatePassed\":"
         << (gate.reveal_rate_passed ? "true" : "false")
         << ",\"jointWinsPassed\":"
         << (gate.joint_wins_passed ? "true" : "false")
         << ",\"scoreLowerBoundPassed\":"
         << (gate.score_lower_bound_passed ? "true" : "false")
         << ",\"moveLowerBoundPassed\":"
         << (gate.move_lower_bound_passed ? "true" : "false")
         << ",\"passed\":" << (gate.passed ? "true" : "false") << '}';
}

void writeGame(std::ostream& output, const GameResult& game) {
  output << "{\"seed\":" << game.seed << ",\"score\":" << game.score
         << ",\"moves\":" << game.moves << ",\"natural\":"
         << (!game.censored ? "true" : "false") << ",\"censored\":"
         << (game.censored ? "true" : "false")
         << ",\"numberedClears\":" << game.numbered_clears
         << ",\"coversRevealed\":" << game.covers_revealed
         << ",\"clearedBoards\":" << game.cleared_boards
         << ",\"maximumChain\":" << game.maximum_chain
         << ",\"decisionSeconds\":" << game.decision_seconds
         << ",\"discStreamHash\":\"" << hex64(game.disc_stream_hash)
         << "\",\"rootChecksum\":\"" << hex64(game.root_checksum)
         << "\",\"tapeChecksum\":\"" << hex64(game.tape_checksum)
         << "\",\"candidateRoots\":" << game.roots.size()
         << ",\"work\":";
  writeWork(output, game.work);
  output << '}';
}

void writeCohort(std::ostream& output, const Cohort& cohort,
                 const Summary& baseline, const Summary& candidate,
                 const PairedSummary& paired, const GateResult& gate,
                 std::uint32_t seed_start, int maximum_moves) {
  output << "{\"seedStart\":" << seed_start << ",\"games\":"
         << cohort.games.size() << ",\"maximumMoves\":" << maximum_moves
         << ",\"wallSeconds\":" << cohort.wall_seconds
         << ",\"fairD1\":";
  writeSummary(output, baseline);
  output << ",\"candidate\":";
  writeSummary(output, candidate);
  output << ",\"pairedCandidateMinusFairD1\":";
  writePairedSummary(output, paired);
  output << ",\"gate\":";
  writeGate(output, gate);
  output << ",\"pairs\":[";
  for (std::size_t game = 0; game < cohort.games.size(); ++game) {
    if (game != 0) output << ',';
    output << "{\"fairD1\":";
    writeGame(output, cohort.games[game].fair_d1);
    output << ",\"candidate\":";
    writeGame(output, cohort.games[game].candidate);
    output << '}';
  }
  output << "]}";
}

void writeArtifact(const Options& options, const Cohort& fitting,
                   const Summary& fitting_baseline,
                   const Summary& fitting_candidate,
                   const PairedSummary& fitting_paired,
                   const GateResult& fitting_gate,
                   const Cohort* development,
                   const Summary* development_baseline,
                   const Summary* development_candidate,
                   const PairedSummary* development_paired,
                   const GateResult* development_gate,
                   std::uint64_t teacher_records, double total_wall) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open result artifact");
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"public-rollout-policy-iteration\",\n"
         << "  \"preregistered\":true,\n"
         << "  \"oneStepPolicyImprovement\":true,\n"
         << "  \"publicDecisionBoundary\":[\"board\",\"nextDisc\","
            "\"movesRemaining\",\"terminal\"],\n"
         << "  \"excludedFromDecisionBoundary\":[\"gameSeed\",\"score\","
            "\"level\",\"moveIndex\",\"history\",\"futureTape\","
            "\"scenario\"],\n"
         << "  \"scoring\":{\"levelBonus\":" << kLevelBonus << "},\n"
         << "  \"rootPolicy\":{\"selection\":\"highest mean return\","
            "\"tieBreak\":\"center-first\",\"pairedScenarioUncertainty\":"
            "\"audit-only; never gates or changes an action\"},\n"
         << "  \"continuation\":{\"policy\":\"fresh exact public fair-D1\","
            "\"depth\":" << kContinuationDepth << ",\"chanceStrata\":"
         << kContinuationStrata << ",\"fullWidthRequired\":true},\n"
         << "  \"rollout\":{\"scenarios\":" << kScenarios
         << ",\"horizonMoves\":" << kHorizon
         << ",\"terminalPenalty\":" << kTerminalPenalty
         << ",\"survivingTail\":\"unchanged fairLeaf\","
            "\"cumulativeRealScoreDeltas\":true,"
            "\"commonAcrossSiblingActions\":true,"
            "\"canonicalPublicStateSeeded\":true,"
            "\"exactlyStratifiedByEvent\":true,"
            "\"revealAndVisibleDomainsSeparate\":true,"
            "\"eventsPerStep\":" << kEventsPerStep << "},\n"
         << "  \"seedDiscipline\":{\"fittingStart\":"
         << kFittingSeedStart << ",\"fittingGames\":" << kFittingGames
         << ",\"developmentStart\":" << kDevelopmentSeedStart
         << ",\"developmentGames\":" << kDevelopmentGames
         << ",\"rejectedNeighborFamilies\":[\"0x3d3\",\"0x3d4\","
            "\"0x3d5\"],\"forbiddenFamilies\":[\"0x7d\",\"0xd7\"]},\n"
         << "  \"resourceCaps\":{\"wallSeconds\":" << kWallLimitSeconds
         << ",\"rssBytes\":" << kRssLimitBytes
         << ",\"maximumFairWorkPerDecision\":"
         << kMaximumFairWorkPerDecision
         << ",\"maximumSyntheticTransitionsPerDecision\":"
         << kMaximumSyntheticTransitionsPerDecision << "},\n"
         << "  \"fittingGateDefinition\":{\"scoreRatio\":"
         << kFittingScoreRatio << ",\"moveRatio\":" << kFittingMoveRatio
         << ",\"minimumJointScoreMoveWins\":" << kFittingJointWins
         << ",\"candidateRevealPerMoveAtLeastFairD1\":true},\n"
         << "  \"developmentGateDefinition\":{\"scoreRatio\":"
         << kDevelopmentScoreRatio << ",\"moveRatio\":"
         << kDevelopmentMoveRatio
         << ",\"pairedOneSided95LowerBoundsNonnegative\":true,"
            "\"candidateRevealPerMoveAtLeastFairD1\":true},\n"
         << "  \"fitting\":";
  writeCohort(output, fitting, fitting_baseline, fitting_candidate,
              fitting_paired, fitting_gate, kFittingSeedStart,
              kFittingMaximumMoves);
  output << ",\n  \"developmentOpened\":"
         << (development != nullptr ? "true" : "false")
         << ",\n  \"development\":";
  if (development == nullptr) {
    output << "null";
  } else {
    writeCohort(output, *development, *development_baseline,
                *development_candidate, *development_paired,
                *development_gate, kDevelopmentSeedStart,
                kDevelopmentMaximumMoves);
  }
  output << ",\n  \"teacher\":{\"exported\":"
         << (teacher_records > 0 ? "true" : "false")
         << ",\"conditionalOnFittingPass\":true,\"records\":"
         << teacher_records << ",\"path\":\""
         << jsonEscape(options.teacher_output) << "\"},\n"
         << "  \"qualified\":"
         << (development_gate != nullptr && development_gate->passed ? "true"
                                                                       : "false")
         << ",\n  \"totalWallSeconds\":" << total_wall
         << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
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

void verifyExactStrata(std::uint32_t seed, std::uint32_t domain, int event) {
  std::array<int, kScenarios> counts{};
  for (int scenario = 0; scenario < kScenarios; ++scenario) {
    const double unit = cfpi::detail::stratifiedUnit(
        seed, scenario, kScenarios, domain, event);
    const int stratum = static_cast<int>(std::floor(unit * kScenarios));
    expect(stratum >= 0 && stratum < kScenarios,
           "synthetic tape stratum out of range");
    ++counts[static_cast<std::size_t>(stratum)];
  }
  for (const int count : counts) {
    expect(count == 1, "synthetic chance event was not exactly stratified");
  }
}

bool selfTest(std::ostream& output) {
  expect(kLevelBonus == 17'000, "corrected level bonus regression");
  const ObservableState fixture = asymmetricFixture();
  const FairD1Decision fair_first = chooseFairDepthOne(fixture);
  const FairD1Decision fair_second = chooseFairDepthOne(fixture);
  expect(fair_first == fair_second && fair_first.complete &&
             fair_first.work <= kMaximumFairWorkPerCall &&
             fair_first.cache_entries == 0 &&
             isLegal(fixture.board, fair_first.action),
         "fresh exact fair D1 determinism/completion failed");

  const RolloutEvaluation first = evaluateRollouts(fixture, nullptr, 2);
  const RolloutEvaluation second = evaluateRollouts(fixture, nullptr, 2);
  expect(first == second && isLegal(fixture.board, first.action),
         "public rollout determinism/legality failed");
  ObservableState reflected_state = mirror(fixture);
  const RolloutEvaluation reflected =
      evaluateRollouts(reflected_state, nullptr, 2);
  expect(reflected.action == kBoardSize - 1 - first.action &&
             reflected.fair_d1_action ==
                 kBoardSize - 1 - first.fair_d1_action &&
             reflected.canonical_public_hash == first.canonical_public_hash &&
             reflected.tape_seed == first.tape_seed,
         "public rollout reflection failed");
  for (int column = 0; column < kBoardSize; ++column) {
    expect(reflected.legal[kBoardSize - 1 - column] == first.legal[column],
           "reflected legality mask failed");
    if (first.legal[column]) {
      expect(reflected.actions[kBoardSize - 1 - column] ==
                 first.actions[column],
             "reflected action outcomes failed");
    }
  }

  State metadata = materialize(fixture);
  metadata.score = 9'876'543;
  metadata.level = 82;
  metadata.moves_played = 731;
  const ObservableState normalized = observable(metadata);
  expect(normalized == fixture && publicHash(normalized) == publicHash(fixture),
         "public normalization used score/level/move index");
  expect(chooseFairDepthOne(normalized) == fair_first &&
             evaluateRollouts(normalized, nullptr, 2) == first,
         "policy used excluded public metadata");

  constexpr std::uint32_t test_seed = 0x1234'5678u;
  for (int event : {0, 1, 63, 64, 511}) {
    verifyExactStrata(test_seed, kRevealTapeDomain, event);
    verifyExactStrata(test_seed, kVisibleTapeDomain, event);
  }
  RevealTape tape_first{test_seed, 7, 3, kRevealTapeDomain, 0};
  RevealTape tape_second{test_seed, 7, 3, kRevealTapeDomain, 0};
  for (int event = 0; event < 32; ++event) {
    expect(tape_first.nextDisc() == tape_second.nextDisc(),
           "common sibling reveal tape failed determinism");
  }

  ObservableState reveal_fixture;
  reveal_fixture.board.fill(kEmpty);
  reveal_fixture.board[indexOf(6, 1)] = kCracked;
  reveal_fixture.next_disc = 1;
  reveal_fixture.moves_remaining = 4;
  MoveResult standard;
  expect(playSyntheticMove(reveal_fixture, 0, test_seed, 0, 0, standard),
         "domain fixture failed");
  bool visible_discriminates = false;
  bool reveal_discriminates = false;
  for (std::uint32_t salt = 1; salt < 512; ++salt) {
    MoveResult changed_visible;
    expect(playSyntheticMove(
               reveal_fixture, 0, test_seed, 0, 0, changed_visible,
               {kRevealTapeDomain, kVisibleTapeDomain ^ salt}),
           "changed visible domain fixture failed");
    if (changed_visible.state.next_disc != standard.state.next_disc) {
      expect(changed_visible.state.board == standard.state.board &&
                 changed_visible.score_delta == standard.score_delta,
             "visible tape leaked into reveal events");
      visible_discriminates = true;
    }
    MoveResult changed_reveal;
    expect(playSyntheticMove(
               reveal_fixture, 0, test_seed, 0, 0, changed_reveal,
               {kRevealTapeDomain ^ salt, kVisibleTapeDomain}),
           "changed reveal domain fixture failed");
    if (changed_reveal.state.board != standard.state.board) {
      expect(changed_reveal.state.next_disc == standard.state.next_disc,
             "reveal tape leaked into visible events");
      reveal_discriminates = true;
    }
  }
  expect(visible_discriminates && reveal_discriminates,
         "separate tape domains were not discriminating");

  ObservableState terminal_fixture;
  terminal_fixture.board.fill(kSolid);
  terminal_fixture.board[indexOf(0, 0)] = kEmpty;
  terminal_fixture.next_disc = 6;
  terminal_fixture.moves_remaining = 1;
  WorkMetrics terminal_work;
  const ScenarioOutcome terminal_outcome = rolloutScenario(
      terminal_fixture, 0, tapeSeed(terminal_fixture), 0, terminal_work,
      nullptr, 1);
  expect(!terminal_outcome.survived_horizon && terminal_outcome.moves == 1 &&
             terminal_outcome.value <= kTerminalPenalty + kLevelBonus,
         "terminal-before-horizon penalty failed");
  ObservableState survivor;
  survivor.board = initialBoard();
  survivor.next_disc = 3;
  survivor.moves_remaining = 5;
  WorkMetrics survivor_work;
  const std::uint32_t survivor_seed = tapeSeed(survivor);
  const ScenarioOutcome survivor_outcome = rolloutScenario(
      survivor, 3, survivor_seed, 0, survivor_work, nullptr, 1);
  MoveResult survivor_move;
  expect(playSyntheticMove(survivor, 3, survivor_seed, 0, 0, survivor_move),
         "surviving tail fixture failed");
  const double expected_survivor =
      static_cast<double>(survivor_move.score_delta) +
      fair::fairLeaf(materialize(observable(survivor_move.state)));
  expect(survivor_outcome.survived_horizon &&
             survivor_outcome.value == expected_survivor,
         "unchanged fair leaf at horizon failed");

  RolloutEvaluation tie_fixture;
  for (int action = 0; action < kBoardSize; ++action) {
    tie_fixture.legal[action] = true;
    tie_fixture.actions[action].mean_return = 10.0;
  }
  tie_fixture.actions[3].scenarios[0].value = -1'000'000.0;
  tie_fixture.actions[2].scenarios[0].value = 10.0;
  expect(selectMeanAction(tie_fixture) == 3,
         "center-first mean-only tie break failed");

  expect(allowedGameplaySeed(kFittingSeedStart, SeedCohort::kFitting) &&
             allowedGameplaySeed(kFittingSeedStart + 3,
                                 SeedCohort::kFitting) &&
             allowedGameplaySeed(kDevelopmentSeedStart + 7,
                                 SeedCohort::kDevelopment),
         "frozen game seed allowlist rejected an authorized seed");
  expect(throwsInvalid([] {
           requireGameplaySeed(0x3d30'0000u, SeedCohort::kFitting);
         }) &&
             throwsInvalid([] {
               requireGameplaySeed(0x3d40'0000u, SeedCohort::kFitting);
             }) &&
             throwsInvalid([] {
               requireGameplaySeed(0x3d50'0000u, SeedCohort::kFitting);
             }) &&
             throwsInvalid([] {
               requireGameplaySeed(0x7d60'0000u, SeedCohort::kFitting);
             }) &&
             throwsInvalid([] {
               requireGameplaySeed(0xd760'0000u, SeedCohort::kFitting);
             }),
         "game seed family guard failed");

  enforceRssLimit();
  output << std::setprecision(12)
         << "PUBLIC_ROLLOUT_POLICY_ITERATION_SELF_TEST {\"passed\":true,"
         << "\"levelBonus\":" << kLevelBonus
         << ",\"publicStateOnly\":true,\"reflection\":true,"
         << "\"deterministic\":true,\"legal\":true,"
         << "\"exactEventStratification\":true,"
         << "\"separateTapeDomains\":true,"
         << "\"terminalPenalty\":true,\"fairLeafAtHorizon\":true,"
         << "\"seedGuards\":true,\"peakRssBytes\":" << peakRssBytes()
         << "}\n";
  return true;
}

int run(const Options& options, std::ostream& output) {
  const Deadline deadline;
  const Cohort fitting =
      runCohort(kFittingSeedStart, kFittingGames, SeedCohort::kFitting,
                kFittingMaximumMoves, options.threads, deadline, "fitting");
  const Summary fitting_baseline = summarize(fitting, Policy::kFairD1);
  const Summary fitting_candidate = summarize(fitting, Policy::kRollout);
  const PairedSummary fitting_paired = pairedSummary(fitting, kT95Df3);
  const GateResult fitting_gate =
      fittingGate(fitting_baseline, fitting_candidate, fitting_paired);

  std::uint64_t teacher_records = 0;
  Cohort development;
  Summary development_baseline;
  Summary development_candidate;
  PairedSummary development_paired;
  GateResult development_gate;
  bool development_opened = false;
  if (fitting_gate.passed) {
    teacher_records = writeTeacher(options.teacher_output, fitting, "fitting",
                                   false);
    development = runCohort(
        kDevelopmentSeedStart, kDevelopmentGames, SeedCohort::kDevelopment,
        kDevelopmentMaximumMoves, options.threads, deadline, "development");
    development_opened = true;
    development_baseline = summarize(development, Policy::kFairD1);
    development_candidate = summarize(development, Policy::kRollout);
    development_paired = pairedSummary(development, kT95Df7);
    development_gate = developmentGate(
        development_baseline, development_candidate, development_paired);
    teacher_records += writeTeacher(options.teacher_output, development,
                                    "development", true);
  }
  deadline.check();
  enforceRssLimit();
  const double total_wall = deadline.elapsedSeconds();
  writeArtifact(
      options, fitting, fitting_baseline, fitting_candidate, fitting_paired,
      fitting_gate, development_opened ? &development : nullptr,
      development_opened ? &development_baseline : nullptr,
      development_opened ? &development_candidate : nullptr,
      development_opened ? &development_paired : nullptr,
      development_opened ? &development_gate : nullptr, teacher_records,
      total_wall);

  output << std::fixed << std::setprecision(3)
         << "PUBLIC_ROLLOUT_POLICY_ITERATION_RESULT {\"fittingFairScore\":"
         << fitting_baseline.mean_score << ",\"fittingFairMoves\":"
         << fitting_baseline.mean_moves << ",\"fittingCandidateScore\":"
         << fitting_candidate.mean_score
         << ",\"fittingCandidateMoves\":" << fitting_candidate.mean_moves
         << ",\"fittingScoreRatio\":" << fitting_gate.score_ratio
         << ",\"fittingMoveRatio\":" << fitting_gate.move_ratio
         << ",\"fittingJointWins\":"
         << fitting_paired.joint_score_move_wins
         << ",\"fittingPassed\":"
         << (fitting_gate.passed ? "true" : "false")
         << ",\"developmentOpened\":"
         << (development_opened ? "true" : "false")
         << ",\"developmentPassed\":"
         << (development_opened && development_gate.passed ? "true" : "false")
         << ",\"teacherRecords\":" << teacher_records
         << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"totalWallSeconds\":" << total_wall << ",\"artifact\":\""
         << jsonEscape(options.output) << "\"}\n";
  return development_opened && development_gate.passed ? 0 : 2;
}

}  // namespace drop7::public_rollout_policy_iteration

#ifndef DROP7_PUBLIC_ROLLOUT_POLICY_ITERATION_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::public_rollout_policy_iteration::selfTest(std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::public_rollout_policy_iteration::parseOptions(argc, argv, 2);
      return drop7::public_rollout_policy_iteration::run(options, std::cout);
    }
    std::cerr << "usage: drop7_public_rollout_policy_iteration "
                 "--self-test | --run [--output PATH] "
                 "[--teacher-output PATH] [--threads N]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_public_rollout_policy_iteration: " << error.what()
              << '\n';
    return 1;
  }
}
#endif
