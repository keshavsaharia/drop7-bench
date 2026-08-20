#define DROP7_FAIR_ONLY_HORIZON_LIBRARY
#include "../../fair-expectimax/reference/fair-only-horizon.cpp"
#undef DROP7_FAIR_ONLY_HORIZON_LIBRARY

#include <atomic>
#include <bit>
#include <filesystem>
#include <optional>
#include <sstream>
#include <type_traits>

// A conservative one-step policy-improvement test around exact public fair-D1.
// Every sibling action sees the same 255 event-stratified chance streams.  The
// root action is forced and every later decision is a fresh, completed fair-D1
// search whose type boundary contains public information only.  A candidate
// action can replace fair-D1 only when paired one-sided 99% lower confidence
// bounds establish gains in both corrected score and survived moves.
namespace drop7::terminal_policy_iteration {

namespace fair = drop7::fair_only_horizon;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kCalibrationSeedStart = 0x3d6d'0000u;
constexpr int kCalibrationGames = 4;
constexpr std::uint32_t kScreenSeedStart = 0x3d6d'0010u;
constexpr int kScreenGames = 8;
constexpr int kCalibrationMaximumMoves = 200;
constexpr int kScreenMaximumMoves = 1'000;
constexpr int kCalibrationRootsPerGame = 2;
constexpr int kScenarios = 255;
constexpr int kHorizon = 200;
constexpr int kContinuationDepth = 1;
constexpr int kContinuationStrata = 5;
constexpr int kEventsPerStep = 64;
constexpr int kDefaultThreads = 4;
constexpr double kT99Df254 = 2.3412;
constexpr double kNormal99 = 2.326347874;
constexpr double kWallLimitSeconds = 45.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr double kProjectionSafetyFactor = 1.35;
constexpr double kProjectionFixedReserveSeconds = 30.0;
constexpr double kScreenScoreRatio = 1.20;
constexpr double kScreenMoveRatio = 1.20;
constexpr int kScreenJointWins = 6;
constexpr double kMaterialScoreLoss = -100'000.0;
constexpr double kMaterialMoveLoss = -25.0;
constexpr std::uint32_t kCalibrationPanelADomain = 0x5443'4131u; // "TCA1"
constexpr std::uint32_t kCalibrationPanelBDomain = 0x5443'4232u; // "TCB2"
constexpr std::uint32_t kDeploymentPanelDomain = 0x5444'4550u;   // "TDEP"
constexpr std::uint32_t kRevealTapeDomain = 0x5452'564cu;        // "TRVL"
constexpr std::uint32_t kVisibleTapeDomain = 0x5456'4953u;       // "TVIS"
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
static_assert(kScenarios == 255 && kHorizon == 200);
static_assert(kEventsPerStep > kCellCount);
static_assert(kMaximumFairCallsPerDecision == 355'216);
static_assert(kMaximumFairWorkPerDecision == 24'865'120);
static_assert(kMaximumSyntheticTransitionsPerDecision == 357'000);
static_assert(kCalibrationSeedStart + kCalibrationGames <= kScreenSeedStart);
static_assert(kScreenSeedStart + kScreenGames <= 0x3d6d'ffffu);
static_assert((kCalibrationSeedStart >> 16u) == 0x3d6du);
static_assert((kScreenSeedStart >> 16u) == 0x3d6du);

std::mutex report_mutex;

struct Options {
  std::string output = "/tmp/drop7-terminal-policy-iteration.json";
  std::string root_output =
      "/tmp/drop7-terminal-policy-deployment-panels.jsonl";
  int threads = kDefaultThreads;
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string argument = argv[index];
    if (argument == "--output") {
      result.output = argv[index + 1];
    } else if (argument == "--root-output") {
      result.root_output = argv[index + 1];
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
  return observable(cfpi::detail::canonicalState(materialize(source), mirrored));
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

std::uint32_t panelSeed(const ObservableState& root, std::uint32_t domain) {
  return seed32(publicHash(root) ^ static_cast<std::uint64_t>(domain));
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
  if (peakRssBytes() > kRssLimitBytes) {
    throw std::runtime_error("terminal policy iteration exceeded 256 MiB RSS");
  }
}

struct Deadline {
  Clock::time_point started = Clock::now();

  double elapsedSeconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }

  void check() const {
    if (elapsedSeconds() > kWallLimitSeconds) {
      throw std::runtime_error("terminal policy iteration exceeded 45m wall");
    }
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

// The continuation policy's complete input type is deliberately incapable of
// carrying a game seed, score, level, move index, history, panel, or scenario.
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
    throw std::runtime_error("fair-D1 did not complete its public full root");
  }
  result.action = mirrored ? kBoardSize - 1 - root.action : root.action;
  for (int canonical_column = 0; canonical_column < kBoardSize;
       ++canonical_column) {
    const int source_column =
        mirrored ? kBoardSize - 1 - canonical_column : canonical_column;
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
static_assert(std::is_same_v<decltype(&chooseFairDepthOne), PublicContinuation>);
static_assert(!std::is_invocable_v<PublicContinuation, const State&>);

struct RevealTape {
  std::uint32_t root_seed = 0;
  int scenario = 0;
  int step = 0;
  int event = 0;

  std::uint8_t nextDisc() {
    if (event >= kEventsPerStep) {
      throw std::runtime_error("synthetic reveal event slice exhausted");
    }
    const int event_index = step * kEventsPerStep + event++;
    const double unit = cfpi::detail::stratifiedUnit(
        root_seed, scenario, kScenarios, kRevealTapeDomain, event_index);
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
                       std::uint32_t reveal_domain = kRevealTapeDomain,
                       std::uint32_t visible_domain = kVisibleTapeDomain) {
  if (source.terminal || scenario < 0 || scenario >= kScenarios || step < 0 ||
      step >= kHorizon || !isLegal(source.board, action)) {
    return false;
  }
  Board board = source.board;
  if (!placeDisc(board, action, source.next_disc)) return false;

  struct DomainTape {
    std::uint32_t root_seed;
    int scenario;
    int step;
    std::uint32_t domain;
    int event = 0;
    std::uint8_t nextDisc() {
      if (event >= kEventsPerStep) {
        throw std::runtime_error("synthetic reveal event slice exhausted");
      }
      const int event_index = step * kEventsPerStep + event++;
      const double unit = cfpi::detail::stratifiedUnit(
          root_seed, scenario, kScenarios, domain, event_index);
      return static_cast<std::uint8_t>(
          std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
    }
  } reveals{root_seed, scenario, step, reveal_domain};

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
               : visibleDisc(root_seed, scenario, step, visible_domain);
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

void observeFairDecision(const FairD1Decision& decision, WorkMetrics& work) {
  ++work.fair_d1_calls;
  work.fair_work += decision.work;
  work.fair_nodes += decision.nodes;
  work.fair_cache_hits += decision.cache_hits;
  work.peak_fair_cache_entries =
      std::max(work.peak_fair_cache_entries, decision.cache_entries);
  work.fair_root_actions +=
      static_cast<std::uint64_t>(decision.evaluated_actions);
  work.full_fair_roots = work.full_fair_roots && decision.complete;
}

struct ScenarioOutcome {
  double score_return = 0.0;
  double survived_moves = 0.0;
  int numbered_clears = 0;
  int covers_revealed = 0;
  bool survived_cutoff = false;

  bool operator==(const ScenarioOutcome&) const = default;
};

ScenarioOutcome rolloutScenario(const ObservableState& root, int root_action,
                                std::uint32_t root_seed, int scenario,
                                WorkMetrics& work, const Deadline* deadline,
                                int horizon = kHorizon) {
  if (root.terminal || !isLegal(root.board, root_action) || horizon < 1 ||
      horizon > kHorizon) {
    throw std::invalid_argument("invalid terminal rollout scenario");
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
      throw std::runtime_error("fair-D1 continuation selected illegal action");
    }
    MoveResult move;
    if (!playSyntheticMove(state, action, root_seed, scenario, step, move)) {
      throw std::runtime_error("synthetic terminal transition failed");
    }
    ++work.synthetic_transitions;
    result.score_return += static_cast<double>(move.score_delta);
    result.survived_moves += 1.0;
    for (const Wave& wave : move.waves) {
      result.numbered_clears += wave.cleared;
      result.covers_revealed += wave.revealed;
    }
    state = observable(move.state);
    if (state.terminal) return result;
  }
  // Frozen before gameplay: surviving cutoffs receive the existing public
  // fair leaf as their score tail and zero invented future moves.
  result.survived_cutoff = true;
  result.score_return += fair::fairLeaf(materialize(state));
  return result;
}

struct PairedMetric {
  double mean = 0.0;
  double standard_error = 0.0;
  double lower_one_sided_99 = 0.0;
  double minimum = 0.0;
  double maximum = 0.0;
  int wins = 0;
  int ties = 0;
  int losses = 0;
};

PairedMetric pairedMetric(const std::vector<double>& differences) {
  if (differences.size() < 2) {
    throw std::invalid_argument("paired confidence requires n >= 2");
  }
  PairedMetric result;
  result.minimum = std::numeric_limits<double>::infinity();
  result.maximum = -std::numeric_limits<double>::infinity();
  for (const double difference : differences) {
    result.mean += difference / differences.size();
    result.minimum = std::min(result.minimum, difference);
    result.maximum = std::max(result.maximum, difference);
    result.wins += difference > 0.0;
    result.ties += difference == 0.0;
    result.losses += difference < 0.0;
  }
  double squares = 0.0;
  for (const double difference : differences) {
    const double centered = difference - result.mean;
    squares += centered * centered;
  }
  const double deviation =
      std::sqrt(squares / static_cast<double>(differences.size() - 1));
  result.standard_error = deviation / std::sqrt(differences.size());
  result.lower_one_sided_99 =
      result.mean - kT99Df254 * result.standard_error;
  return result;
}

double wilsonUpper99(int events, int trials) {
  if (events < 0 || trials < 1 || events > trials) {
    throw std::invalid_argument("invalid Wilson interval inputs");
  }
  const double n = static_cast<double>(trials);
  const double p = static_cast<double>(events) / n;
  const double z2 = kNormal99 * kNormal99;
  const double center = p + z2 / (2.0 * n);
  const double radius = kNormal99 *
      std::sqrt((p * (1.0 - p) + z2 / (4.0 * n)) / n);
  return (center + radius) / (1.0 + z2 / n);
}

struct PairedActionAudit {
  PairedMetric score{};
  PairedMetric moves{};
  int material_downsides = 0;
  double material_downside_upper99 = 1.0;
};

struct ActionRollout {
  std::array<ScenarioOutcome, kScenarios> scenarios{};
  double mean_score_return = 0.0;
  double mean_survived_moves = 0.0;
  double mean_numbered_clears = 0.0;
  double mean_covers_revealed = 0.0;
  int surviving_cutoffs = 0;
  PairedActionAudit paired_vs_fair_d1{};
};

PairedActionAudit pairedAudit(const ActionRollout& candidate,
                              const ActionRollout& baseline) {
  std::vector<double> scores;
  std::vector<double> moves;
  scores.reserve(kScenarios);
  moves.reserve(kScenarios);
  PairedActionAudit result;
  for (int scenario = 0; scenario < kScenarios; ++scenario) {
    const double score = candidate.scenarios[scenario].score_return -
                         baseline.scenarios[scenario].score_return;
    const double move = candidate.scenarios[scenario].survived_moves -
                        baseline.scenarios[scenario].survived_moves;
    scores.push_back(score);
    moves.push_back(move);
    result.material_downsides +=
        score < kMaterialScoreLoss || move < kMaterialMoveLoss;
  }
  result.score = pairedMetric(scores);
  result.moves = pairedMetric(moves);
  result.material_downside_upper99 =
      wilsonUpper99(result.material_downsides, kScenarios);
  return result;
}

struct GateProfile {
  std::string_view name;
  double minimum_score_lcb;
  double minimum_move_lcb;
  double maximum_downside_upper99;
};

// All candidates require strictly positive 99% LCBs in both metrics.
// Calibration may choose only among these fixed, nested gates.
constexpr std::array<GateProfile, 4> kGateProfiles{{
    {"balanced", 1'000.0, 0.25, 0.25},
    {"guarded", 2'500.0, 0.50, 0.20},
    {"strict", 5'000.0, 1.00, 0.15},
    {"ultra", 10'000.0, 2.00, 0.10},
}};

bool passesGate(const PairedActionAudit& audit, const GateProfile& gate) {
  return audit.score.lower_one_sided_99 >= gate.minimum_score_lcb &&
         audit.moves.lower_one_sided_99 >= gate.minimum_move_lcb &&
         audit.material_downside_upper99 <= gate.maximum_downside_upper99;
}

struct RolloutEvaluation {
  std::array<ActionRollout, kBoardSize> actions{};
  std::array<bool, kBoardSize> legal{};
  int legal_actions = 0;
  int fair_d1_action = -1;
  WorkMetrics work{};
  std::uint64_t canonical_public_hash = 0;
  std::uint32_t tape_seed = 0;
  double seconds = 0.0;
};

RolloutEvaluation evaluateRollouts(const ObservableState& source,
                                   std::uint32_t panel_domain,
                                   const Deadline* deadline = nullptr,
                                   int horizon = kHorizon) {
  if (source.terminal || horizon < 1 || horizon > kHorizon) {
    throw std::invalid_argument("invalid terminal rollout root");
  }
  const auto started = Clock::now();
  RolloutEvaluation result;
  bool mirrored = false;
  const ObservableState root = canonicalObservable(source, mirrored);
  result.canonical_public_hash = publicHash(root);
  result.tape_seed = panelSeed(root, panel_domain);

  for (const int action : kColumnOrder) {
    if (!isLegal(root.board, action)) continue;
    result.legal[action] = true;
    ++result.legal_actions;
    ActionRollout& action_result = result.actions[action];
    for (int scenario = 0; scenario < kScenarios; ++scenario) {
      ScenarioOutcome& outcome = action_result.scenarios[scenario];
      outcome = rolloutScenario(root, action, result.tape_seed, scenario,
                                result.work, deadline, horizon);
      action_result.mean_score_return += outcome.score_return / kScenarios;
      action_result.mean_survived_moves += outcome.survived_moves / kScenarios;
      action_result.mean_numbered_clears +=
          static_cast<double>(outcome.numbered_clears) / kScenarios;
      action_result.mean_covers_revealed +=
          static_cast<double>(outcome.covers_revealed) / kScenarios;
      action_result.surviving_cutoffs += outcome.survived_cutoff;
    }
  }

  const FairD1Decision baseline = chooseFairDepthOne(root);
  observeFairDecision(baseline, result.work);
  result.fair_d1_action = baseline.action;
  if (!result.legal[result.fair_d1_action]) {
    throw std::runtime_error("fair-D1 baseline action was not legal");
  }
  const ActionRollout& baseline_rollout = result.actions[result.fair_d1_action];
  for (int action = 0; action < kBoardSize; ++action) {
    if (result.legal[action]) {
      result.actions[action].paired_vs_fair_d1 =
          pairedAudit(result.actions[action], baseline_rollout);
    }
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
    throw std::runtime_error("terminal rollout exceeded frozen work proof");
  }
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();

  if (!mirrored) return result;
  RolloutEvaluation reflected = result;
  for (int canonical_column = 0; canonical_column < kBoardSize;
       ++canonical_column) {
    const int source_column = kBoardSize - 1 - canonical_column;
    reflected.actions[source_column] = result.actions[canonical_column];
    reflected.legal[source_column] = result.legal[canonical_column];
  }
  reflected.fair_d1_action = kBoardSize - 1 - result.fair_d1_action;
  return reflected;
}

struct PolicyDecision {
  int action = -1;
  int fair_d1_action = -1;
  bool switched = false;
  int eligible_switches = 0;
  PairedActionAudit selected_audit{};
};

PolicyDecision chooseWithGate(const RolloutEvaluation& evaluation,
                              const GateProfile& gate) {
  PolicyDecision result;
  result.action = evaluation.fair_d1_action;
  result.fair_d1_action = evaluation.fair_d1_action;
  double best_score_lcb = -std::numeric_limits<double>::infinity();
  double best_move_lcb = -std::numeric_limits<double>::infinity();
  for (const int action : kColumnOrder) {
    if (!evaluation.legal[action] || action == evaluation.fair_d1_action) {
      continue;
    }
    const PairedActionAudit& audit =
        evaluation.actions[action].paired_vs_fair_d1;
    if (!passesGate(audit, gate)) continue;
    ++result.eligible_switches;
    const bool better = audit.score.lower_one_sided_99 > best_score_lcb ||
        (audit.score.lower_one_sided_99 == best_score_lcb &&
         audit.moves.lower_one_sided_99 > best_move_lcb);
    if (better) {
      result.action = action;
      result.selected_audit = audit;
      best_score_lcb = audit.score.lower_one_sided_99;
      best_move_lcb = audit.moves.lower_one_sided_99;
    }
  }
  result.switched = result.action != result.fair_d1_action;
  if (!evaluation.legal[result.action]) {
    throw std::runtime_error("conservative selector returned illegal action");
  }
  return result;
}

struct CalibrationRoot {
  ObservableState state{};
  std::uint32_t source_seed = 0;
  int source_move = 0;
  RolloutEvaluation discovery{};
  RolloutEvaluation confirmation{};
};

struct CalibrationPath {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::vector<ObservableState> states;
};

enum class SeedCohort { kCalibration, kScreen };

bool allowedGameplaySeed(std::uint32_t seed, SeedCohort cohort) {
  const std::uint32_t start = cohort == SeedCohort::kCalibration
                                  ? kCalibrationSeedStart
                                  : kScreenSeedStart;
  const int games =
      cohort == SeedCohort::kCalibration ? kCalibrationGames : kScreenGames;
  return seed >= start && seed < start + static_cast<std::uint32_t>(games) &&
         (seed >> 16u) == 0x3d6du;
}

void requireGameplaySeed(std::uint32_t seed, SeedCohort cohort) {
  if (!allowedGameplaySeed(seed, cohort)) {
    throw std::invalid_argument("game seed outside terminal-policy allowlist");
  }
}

CalibrationPath generateCalibrationPath(std::uint32_t seed,
                                        const Deadline& deadline) {
  requireGameplaySeed(seed, SeedCohort::kCalibration);
  State state = initialHeadlessState(seed);
  CalibrationPath result;
  result.seed = seed;
  result.states.push_back(observable(state));
  while (!state.game_over && state.moves_played < kCalibrationMaximumMoves) {
    deadline.check();
    const FairD1Decision decision = chooseFairDepthOne(observable(state));
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("calibration D1 transition failed");
    }
    if (!state.game_over) result.states.push_back(observable(state));
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  return result;
}

std::vector<CalibrationRoot> selectCalibrationRoots(
    const std::vector<CalibrationPath>& paths) {
  std::vector<CalibrationRoot> roots;
  roots.reserve(paths.size() * kCalibrationRootsPerGame);
  std::vector<std::uint64_t> hashes;
  for (const CalibrationPath& path : paths) {
    if (path.states.empty()) continue;
    const std::array<std::size_t, kCalibrationRootsPerGame> offsets{{
        path.states.size() / 3u,
        (2u * path.states.size()) / 3u,
    }};
    for (const std::size_t raw_offset : offsets) {
      const std::size_t offset = std::min(raw_offset, path.states.size() - 1u);
      const std::uint64_t hash = publicHash(path.states[offset]);
      if (std::find(hashes.begin(), hashes.end(), hash) != hashes.end()) {
        continue;
      }
      hashes.push_back(hash);
      roots.push_back({path.states[offset], path.seed,
                       static_cast<int>(offset), {}, {}});
    }
  }
  if (roots.size() < 4) {
    throw std::runtime_error("too few distinct public calibration roots");
  }
  return roots;
}

struct ProfileCalibration {
  int discovery_switches = 0;
  int independently_positive = 0;
  int independently_lcb_positive = 0;
  PairedMetric pooled_confirmation_score{};
  PairedMetric pooled_confirmation_moves{};
  int pooled_material_downsides = 0;
  double pooled_downside_upper99 = 1.0;
  bool qualified = false;
};

struct CalibrationResult {
  std::vector<CalibrationPath> paths;
  std::vector<CalibrationRoot> roots;
  std::array<ProfileCalibration, kGateProfiles.size()> profiles{};
  int selected_profile = static_cast<int>(kGateProfiles.size()) - 1;
  bool any_profile_qualified = false;
  double mean_path_moves = 0.0;
  double mean_evaluation_seconds = 0.0;
  double maximum_evaluation_seconds = 0.0;
  double wall_seconds = 0.0;
  std::uint64_t checksum = 0x4341'4c49'4252'4154ull;
};

CalibrationResult calibrateGate(int threads, const Deadline& deadline) {
  const auto started = Clock::now();
  CalibrationResult result;
  for (int game = 0; game < kCalibrationGames; ++game) {
    const std::uint32_t seed =
        kCalibrationSeedStart + static_cast<std::uint32_t>(game);
    result.paths.push_back(generateCalibrationPath(seed, deadline));
    result.mean_path_moves +=
        static_cast<double>(result.paths.back().moves) / kCalibrationGames;
  }
  result.roots = selectCalibrationRoots(result.paths);

  std::atomic<int> next{0};
  std::atomic<int> completed{0};
  std::vector<std::future<void>> workers;
  const int evaluations = static_cast<int>(result.roots.size()) * 2;
  const int worker_count = std::max(1, std::min(threads, evaluations));
  workers.reserve(static_cast<std::size_t>(worker_count));
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [&, worker]() {
      static_cast<void>(worker);
      for (;;) {
        const int evaluation_index = next.fetch_add(1);
        if (evaluation_index >= evaluations) return;
        CalibrationRoot& root =
            result.roots[static_cast<std::size_t>(evaluation_index / 2)];
        if ((evaluation_index & 1) == 0) {
          root.discovery = evaluateRollouts(
              root.state, kCalibrationPanelADomain, &deadline);
        } else {
          root.confirmation = evaluateRollouts(
              root.state, kCalibrationPanelBDomain, &deadline);
        }
        enforceRssLimit();
        const int done = completed.fetch_add(1) + 1;
        const std::lock_guard<std::mutex> lock(report_mutex);
        std::cerr << "terminal-policy calibration " << done << '/'
                  << evaluations << " source=0x" << std::hex
                  << root.source_seed << std::dec << " move="
                  << root.source_move << '\n';
      }
    }));
  }
  for (auto& worker : workers) worker.get();

  for (const CalibrationRoot& root : result.roots) {
    for (const RolloutEvaluation* panel : {&root.discovery,
                                           &root.confirmation}) {
      result.mean_evaluation_seconds +=
          panel->seconds / static_cast<double>(evaluations);
      result.maximum_evaluation_seconds =
          std::max(result.maximum_evaluation_seconds, panel->seconds);
      hashCombine(result.checksum, panel->canonical_public_hash);
      hashCombine(result.checksum, panel->tape_seed);
      hashCombine(result.checksum, panel->work.synthetic_transitions);
    }
  }

  for (std::size_t profile_index = 0; profile_index < kGateProfiles.size();
       ++profile_index) {
    const GateProfile& gate = kGateProfiles[profile_index];
    ProfileCalibration& profile = result.profiles[profile_index];
    std::vector<double> pooled_scores;
    std::vector<double> pooled_moves;
    for (const CalibrationRoot& root : result.roots) {
      const PolicyDecision discovery = chooseWithGate(root.discovery, gate);
      if (!discovery.switched) continue;
      ++profile.discovery_switches;
      const ActionRollout& candidate =
          root.confirmation.actions[discovery.action];
      const ActionRollout& baseline =
          root.confirmation.actions[root.confirmation.fair_d1_action];
      const PairedActionAudit confirmation =
          pairedAudit(candidate, baseline);
      profile.independently_positive +=
          confirmation.score.mean > 0.0 && confirmation.moves.mean > 0.0;
      profile.independently_lcb_positive +=
          confirmation.score.lower_one_sided_99 > 0.0 &&
          confirmation.moves.lower_one_sided_99 > 0.0;
      for (int scenario = 0; scenario < kScenarios; ++scenario) {
        const double score = candidate.scenarios[scenario].score_return -
                             baseline.scenarios[scenario].score_return;
        const double move = candidate.scenarios[scenario].survived_moves -
                            baseline.scenarios[scenario].survived_moves;
        pooled_scores.push_back(score);
        pooled_moves.push_back(move);
        profile.pooled_material_downsides +=
            score < kMaterialScoreLoss || move < kMaterialMoveLoss;
      }
    }
    if (!pooled_scores.empty()) {
      profile.pooled_confirmation_score = pairedMetric(pooled_scores);
      profile.pooled_confirmation_moves = pairedMetric(pooled_moves);
      profile.pooled_downside_upper99 = wilsonUpper99(
          profile.pooled_material_downsides,
          static_cast<int>(pooled_scores.size()));
    }
    profile.qualified =
        profile.discovery_switches >= 2 &&
        profile.independently_positive * 4 >=
            profile.discovery_switches * 3 &&
        profile.pooled_confirmation_score.lower_one_sided_99 > 0.0 &&
        profile.pooled_confirmation_moves.lower_one_sided_99 > 0.0 &&
        profile.pooled_downside_upper99 <= gate.maximum_downside_upper99;
    if (!result.any_profile_qualified && profile.qualified) {
      result.selected_profile = static_cast<int>(profile_index);
      result.any_profile_qualified = true;
    }
  }
  // Use the strictest fixed fallback when no gate qualifies independently.
  // Gameplay never participates in this choice.
  result.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

enum class Policy { kFairD1, kConservative };

struct SwitchRecord {
  int move = 0;
  int fair_action = -1;
  int selected_action = -1;
  PairedActionAudit audit{};
};

struct ActionPanelSummary {
  bool legal = false;
  double mean_score_return = 0.0;
  double mean_survived_moves = 0.0;
  double mean_numbered_clears = 0.0;
  double mean_covers_revealed = 0.0;
  int surviving_cutoffs = 0;
  PairedActionAudit paired_vs_fair_d1{};
};

struct DeploymentPanelRecord {
  ObservableState public_state{};
  int move_index = 0;
  std::uint64_t canonical_public_hash = 0;
  std::uint32_t tape_seed = 0;
  int fair_d1_action = -1;
  int selected_action = -1;
  bool switched = false;
  std::array<ActionPanelSummary, kBoardSize> actions{};
};

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::int64_t numbered_clears = 0;
  std::int64_t covers_revealed = 0;
  int maximum_chain = 0;
  int decisions = 0;
  int switches = 0;
  int eligible_switches = 0;
  double decision_seconds = 0.0;
  WorkMetrics work{};
  std::uint64_t disc_stream_hash = 0;
  std::uint64_t decision_checksum = 0x4445'4349'5349'4f4eull;
  std::vector<SwitchRecord> switch_records;
  std::vector<DeploymentPanelRecord> panel_records;
};

std::uint64_t discStreamHash(std::uint32_t seed, int maximum_moves) {
  std::uint64_t hash = 0x9e37'79b9'7f4a'7c15ull;
  for (int move = 0; move < maximum_moves; ++move) {
    hashCombine(hash, headlessDisc(seed, move));
  }
  return hash;
}

void observeMove(const MoveResult& move, GameResult& result) {
  for (const Wave& wave : move.waves) {
    result.numbered_clears += wave.cleared;
    result.covers_revealed += wave.revealed;
    result.maximum_chain = std::max(result.maximum_chain, wave.depth);
  }
}

GameResult runGame(std::uint32_t seed, Policy policy,
                   const GateProfile& gate, const Deadline& deadline,
                   bool capture_panels = false) {
  requireGameplaySeed(seed, SeedCohort::kScreen);
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  result.disc_stream_hash = discStreamHash(seed, kScreenMaximumMoves);
  while (!state.game_over && state.moves_played < kScreenMaximumMoves) {
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
      const RolloutEvaluation evaluation = evaluateRollouts(
          public_state, kDeploymentPanelDomain, &deadline);
      const PolicyDecision decision = chooseWithGate(evaluation, gate);
      result.work += evaluation.work;
      action = decision.action;
      ++result.decisions;
      result.switches += decision.switched;
      result.eligible_switches += decision.eligible_switches;
      hashCombine(result.decision_checksum, evaluation.canonical_public_hash);
      hashCombine(result.decision_checksum, evaluation.tape_seed);
      hashCombine(result.decision_checksum,
                  static_cast<std::uint64_t>(decision.action + 1));
      if (decision.switched) {
        result.switch_records.push_back({
            state.moves_played, decision.fair_d1_action, decision.action,
            decision.selected_audit});
      }
      if (capture_panels) {
        DeploymentPanelRecord record;
        record.public_state = public_state;
        record.move_index = state.moves_played;
        record.canonical_public_hash = evaluation.canonical_public_hash;
        record.tape_seed = evaluation.tape_seed;
        record.fair_d1_action = decision.fair_d1_action;
        record.selected_action = decision.action;
        record.switched = decision.switched;
        for (int column = 0; column < kBoardSize; ++column) {
          if (!evaluation.legal[column]) continue;
          const ActionRollout& action = evaluation.actions[column];
          record.actions[column] = {
              true,
              action.mean_score_return,
              action.mean_survived_moves,
              action.mean_numbered_clears,
              action.mean_covers_revealed,
              action.surviving_cutoffs,
              action.paired_vs_fair_d1,
          };
        }
        result.panel_records.push_back(std::move(record));
      }
    }
    result.decision_seconds +=
        std::chrono::duration<double>(Clock::now() - decision_started).count();
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("screen policy selected illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("actual screen transition failed");
    }
    observeMove(move, result);
    enforceRssLimit();
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  return result;
}

struct PairedGame {
  GameResult fair_d1{};
  GameResult candidate{};
};

struct ScreenResult {
  std::vector<std::optional<PairedGame>> games;
  int attempted = 0;
  int completed = 0;
  bool aborted = false;
  std::string abort_reason;
  double wall_seconds = 0.0;
};

ScreenResult runScreen(int threads, const GateProfile& gate,
                       const Deadline& deadline) {
  const auto started = Clock::now();
  ScreenResult result;
  result.games.resize(kScreenGames);
  std::atomic<int> next{0};
  std::atomic<int> completed{0};
  std::atomic<bool> stopped{false};
  std::mutex failure_mutex;
  std::vector<std::future<void>> workers;
  const int worker_count = std::max(1, std::min(threads, kScreenGames));
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [&, worker]() {
      static_cast<void>(worker);
      while (!stopped.load()) {
        const int game = next.fetch_add(1);
        if (game >= kScreenGames) return;
        try {
          const std::uint32_t seed =
              kScreenSeedStart + static_cast<std::uint32_t>(game);
          PairedGame pair;
          pair.fair_d1 = runGame(seed, Policy::kFairD1, gate, deadline);
          pair.candidate =
              runGame(seed, Policy::kConservative, gate, deadline);
          if (pair.fair_d1.disc_stream_hash !=
              pair.candidate.disc_stream_hash) {
            throw std::runtime_error("paired actual disc streams differed");
          }
          result.games[static_cast<std::size_t>(game)] = std::move(pair);
          const int done = completed.fetch_add(1) + 1;
          const PairedGame& stored =
              *result.games[static_cast<std::size_t>(game)];
          const std::lock_guard<std::mutex> lock(report_mutex);
          std::cerr << "terminal-policy screen " << done << '/' << kScreenGames
                    << " seed=0x" << std::hex << seed << std::dec
                    << " fair=" << stored.fair_d1.score << '/'
                    << stored.fair_d1.moves << " candidate="
                    << stored.candidate.score << '/'
                    << stored.candidate.moves << " switches="
                    << stored.candidate.switches << '\n';
        } catch (const std::exception& error) {
          stopped.store(true);
          const std::lock_guard<std::mutex> lock(failure_mutex);
          if (result.abort_reason.empty()) result.abort_reason = error.what();
        }
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.attempted = std::min(next.load(), kScreenGames);
  result.completed = completed.load();
  result.aborted = result.completed != kScreenGames;
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
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double mean_decision_ms = 0.0;
  std::int64_t total_moves = 0;
  std::int64_t total_clears = 0;
  std::int64_t total_reveals = 0;
  int decisions = 0;
  int switches = 0;
  int eligible_switches = 0;
  WorkMetrics work{};
  std::uint64_t checksum = 0x5355'4d4d'4152'5921ull;
};

Summary summarize(const ScreenResult& screen, Policy policy) {
  Summary result;
  double decision_seconds = 0.0;
  for (const std::optional<PairedGame>& optional_pair : screen.games) {
    if (!optional_pair.has_value()) continue;
    const GameResult& game = policy == Policy::kFairD1
                                 ? optional_pair->fair_d1
                                 : optional_pair->candidate;
    ++result.games;
    result.mean_score += game.score;
    result.mean_moves += game.moves;
    result.natural += !game.censored;
    result.censored += game.censored;
    result.total_moves += game.moves;
    result.total_clears += game.numbered_clears;
    result.total_reveals += game.covers_revealed;
    result.decisions += game.decisions;
    result.switches += game.switches;
    result.eligible_switches += game.eligible_switches;
    decision_seconds += game.decision_seconds;
    result.work += game.work;
    hashCombine(result.checksum, game.decision_checksum);
  }
  if (result.games > 0) {
    result.mean_score /= result.games;
    result.mean_moves /= result.games;
  }
  if (result.total_moves > 0) {
    result.clears_per_move =
        static_cast<double>(result.total_clears) / result.total_moves;
    result.reveals_per_move =
        static_cast<double>(result.total_reveals) / result.total_moves;
    result.mean_decision_ms = 1'000.0 * decision_seconds / result.total_moves;
  }
  return result;
}

struct ScreenGate {
  double score_ratio = 0.0;
  double move_ratio = 0.0;
  int joint_wins = 0;
  bool score_passed = false;
  bool moves_passed = false;
  bool clears_passed = false;
  bool reveals_passed = false;
  bool joint_passed = false;
  bool complete = false;
  bool passed = false;
};

ScreenGate screenGate(const ScreenResult& screen, const Summary& baseline,
                      const Summary& candidate) {
  ScreenGate result;
  result.complete = screen.completed == kScreenGames && !screen.aborted;
  if (baseline.games == 0 || baseline.mean_score <= 0.0 ||
      baseline.mean_moves <= 0.0) {
    return result;
  }
  result.score_ratio = candidate.mean_score / baseline.mean_score;
  result.move_ratio = candidate.mean_moves / baseline.mean_moves;
  result.score_passed = result.score_ratio >= kScreenScoreRatio;
  result.moves_passed = result.move_ratio >= kScreenMoveRatio;
  result.clears_passed = candidate.clears_per_move >= baseline.clears_per_move;
  result.reveals_passed =
      candidate.reveals_per_move >= baseline.reveals_per_move;
  for (const std::optional<PairedGame>& pair : screen.games) {
    if (!pair.has_value()) continue;
    result.joint_wins += pair->candidate.score > pair->fair_d1.score &&
                         pair->candidate.moves > pair->fair_d1.moves;
  }
  result.joint_passed = result.joint_wins >= kScreenJointWins;
  result.passed = result.complete && result.score_passed &&
                  result.moves_passed && result.clears_passed &&
                  result.reveals_passed && result.joint_passed;
  return result;
}

std::string hex64(std::uint64_t value) {
  std::ostringstream output;
  output << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
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

void writeMetric(std::ostream& output, const PairedMetric& metric) {
  output << "{\"mean\":" << metric.mean
         << ",\"standardError\":" << metric.standard_error
         << ",\"lowerOneSided99\":" << metric.lower_one_sided_99
         << ",\"minimum\":" << metric.minimum
         << ",\"maximum\":" << metric.maximum << ",\"wins\":"
         << metric.wins << ",\"ties\":" << metric.ties
         << ",\"losses\":" << metric.losses << '}';
}

void writeAudit(std::ostream& output, const PairedActionAudit& audit) {
  output << "{\"score\":";
  writeMetric(output, audit.score);
  output << ",\"moves\":";
  writeMetric(output, audit.moves);
  output << ",\"materialDownsides\":" << audit.material_downsides
         << ",\"materialDownsideUpper99\":"
         << audit.material_downside_upper99 << '}';
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
         << ",\"numberedClearsPerMove\":" << summary.clears_per_move
         << ",\"coversRevealedPerMove\":" << summary.reveals_per_move
         << ",\"meanDecisionMs\":" << summary.mean_decision_ms
         << ",\"decisions\":" << summary.decisions
         << ",\"switches\":" << summary.switches
         << ",\"eligibleSwitches\":" << summary.eligible_switches
         << ",\"checksum\":\"" << hex64(summary.checksum)
         << "\",\"work\":";
  writeWork(output, summary.work);
  output << '}';
}

void writeArtifact(const Options& options, const CalibrationResult& calibration,
                   double projected_total_seconds, bool screen_opened,
                   const ScreenResult* screen, const Summary* baseline,
                   const Summary* candidate, const ScreenGate* gate,
                   double total_wall_seconds) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open terminal artifact");
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"terminal-policy-iteration\",\n"
         << "  \"preregistered\":true,\n"
         << "  \"oneStepPolicyImprovement\":true,\n"
         << "  \"publicDecisionBoundary\":[\"board\",\"nextDisc\","
            "\"movesRemaining\",\"terminal\"],\n"
         << "  \"excludedFromDecisionBoundary\":[\"gameSeed\",\"score\","
            "\"level\",\"moveIndex\",\"history\",\"scenario\","
            "\"futureTape\"],\n"
         << "  \"scoring\":{\"levelBonus\":" << kLevelBonus << "},\n"
         << "  \"continuation\":{\"policy\":\"fresh exact public fair-D1\","
            "\"depth\":" << kContinuationDepth
         << ",\"chanceStrata\":" << kContinuationStrata
         << ",\"fullRootRequired\":true},\n"
         << "  \"rollout\":{\"scenarios\":" << kScenarios
         << ",\"horizonMoves\":" << kHorizon
         << ",\"scoreReturn\":\"corrected cumulative real score deltas\","
            "\"survivedMoves\":\"actual moves before terminal/cutoff\","
            "\"cutoffScoreTail\":\"fixed public fairLeaf\","
            "\"cutoffMoveTail\":0,\"commonAcrossSiblingActions\":true,"
            "\"eventStratified\":true,\"revealVisibleDomainsSeparate\":true,"
            "\"eventsPerStep\":" << kEventsPerStep << "},\n"
         << "  \"confidence\":{\"oneSided\":0.99,\"studentCriticalDf254\":"
         << kT99Df254 << ",\"materialScoreLoss\":" << kMaterialScoreLoss
         << ",\"materialMoveLoss\":" << kMaterialMoveLoss
         << ",\"downsideBound\":\"one-sided 99% Wilson upper\"},\n"
         << "  \"seedDiscipline\":{\"allowedLane\":\"0x3d6d0000..0x3d6dffff\","
            "\"calibrationStart\":" << kCalibrationSeedStart
         << ",\"calibrationGames\":" << kCalibrationGames
         << ",\"screenStart\":" << kScreenSeedStart
         << ",\"screenGames\":" << kScreenGames
         << ",\"forbidden\":[\"0x3d3a\",\"0x3d68\",\"0x3d69\","
            "\"0x3d6b\",\"0x3d6c\",\"0x4d\",\"0x7d\",\"0xd7\"]},\n"
         << "  \"resourceProof\":{\"wallLimitSeconds\":"
         << kWallLimitSeconds << ",\"rssLimitBytes\":" << kRssLimitBytes
         << ",\"maximumFairCallsPerDecision\":"
         << kMaximumFairCallsPerDecision
         << ",\"maximumFairWorkPerDecision\":"
         << kMaximumFairWorkPerDecision
         << ",\"maximumSyntheticTransitionsPerDecision\":"
         << kMaximumSyntheticTransitionsPerDecision
         << ",\"measuredMeanRootSeconds\":"
         << calibration.mean_evaluation_seconds
         << ",\"measuredMaximumRootSeconds\":"
         << calibration.maximum_evaluation_seconds
         << ",\"projectionSafetyFactor\":" << kProjectionSafetyFactor
         << ",\"projectedTotalSeconds\":" << projected_total_seconds
         << "},\n"
         << "  \"calibration\":{\"gameplayPolicy\":\"fair-D1 only; candidate"
            " gameplay prohibited\",\"paths\":[";
  for (std::size_t index = 0; index < calibration.paths.size(); ++index) {
    if (index != 0) output << ',';
    const CalibrationPath& path = calibration.paths[index];
    output << "{\"seed\":" << path.seed << ",\"score\":" << path.score
           << ",\"moves\":" << path.moves << ",\"censored\":"
           << (path.censored ? "true" : "false") << '}';
  }
  output << "],\"roots\":[";
  for (std::size_t index = 0; index < calibration.roots.size(); ++index) {
    if (index != 0) output << ',';
    const CalibrationRoot& root = calibration.roots[index];
    output << "{\"sourceSeed\":" << root.source_seed
           << ",\"sourceMove\":" << root.source_move
           << ",\"publicHash\":\"" << hex64(publicHash(root.state))
           << "\",\"discoverySeconds\":" << root.discovery.seconds
           << ",\"confirmationSeconds\":" << root.confirmation.seconds
           << '}';
  }
  output << "],\"profiles\":[";
  for (std::size_t index = 0; index < kGateProfiles.size(); ++index) {
    if (index != 0) output << ',';
    const GateProfile& profile = kGateProfiles[index];
    const ProfileCalibration& result = calibration.profiles[index];
    output << "{\"name\":\"" << profile.name
           << "\",\"minimumScoreLcb\":" << profile.minimum_score_lcb
           << ",\"minimumMoveLcb\":" << profile.minimum_move_lcb
           << ",\"maximumDownsideUpper99\":"
           << profile.maximum_downside_upper99
           << ",\"discoverySwitches\":" << result.discovery_switches
           << ",\"independentlyPositive\":"
           << result.independently_positive
           << ",\"independentlyLcbPositive\":"
           << result.independently_lcb_positive
           << ",\"pooledConfirmationScore\":";
    writeMetric(output, result.pooled_confirmation_score);
    output << ",\"pooledConfirmationMoves\":";
    writeMetric(output, result.pooled_confirmation_moves);
    output << ",\"pooledMaterialDownsides\":"
           << result.pooled_material_downsides
           << ",\"pooledDownsideUpper99\":"
           << result.pooled_downside_upper99 << ",\"qualified\":"
           << (result.qualified ? "true" : "false") << '}';
  }
  const GateProfile& selected =
      kGateProfiles[static_cast<std::size_t>(calibration.selected_profile)];
  output << "],\"selectedProfile\":\"" << selected.name
         << "\",\"anyProfileQualified\":"
         << (calibration.any_profile_qualified ? "true" : "false")
         << ",\"checksum\":\"" << hex64(calibration.checksum)
         << "\",\"wallSeconds\":" << calibration.wall_seconds << "},\n"
         << "  \"screenOpened\":" << (screen_opened ? "true" : "false")
         << ",\n  \"screen\":";
  if (!screen_opened || screen == nullptr || baseline == nullptr ||
      candidate == nullptr || gate == nullptr) {
    output << "null";
  } else {
    output << "{\"attempted\":" << screen->attempted
           << ",\"completed\":" << screen->completed
           << ",\"aborted\":" << (screen->aborted ? "true" : "false")
           << ",\"abortReason\":\"" << jsonEscape(screen->abort_reason)
           << "\",\"wallSeconds\":" << screen->wall_seconds
           << ",\"fairD1\":";
    writeSummary(output, *baseline);
    output << ",\"candidate\":";
    writeSummary(output, *candidate);
    output << ",\"gate\":{\"requiredScoreRatio\":" << kScreenScoreRatio
           << ",\"requiredMoveRatio\":" << kScreenMoveRatio
           << ",\"requiredJointWins\":" << kScreenJointWins
           << ",\"scoreRatio\":" << gate->score_ratio
           << ",\"moveRatio\":" << gate->move_ratio
           << ",\"jointWins\":" << gate->joint_wins
           << ",\"noClearRegression\":"
           << (gate->clears_passed ? "true" : "false")
           << ",\"noRevealRegression\":"
           << (gate->reveals_passed ? "true" : "false")
           << ",\"passed\":" << (gate->passed ? "true" : "false")
           << "},\"pairs\":[";
    bool first_pair = true;
    for (const std::optional<PairedGame>& pair : screen->games) {
      if (!pair.has_value()) continue;
      if (!first_pair) output << ',';
      first_pair = false;
      output << "{\"seed\":" << pair->fair_d1.seed
             << ",\"fairScore\":" << pair->fair_d1.score
             << ",\"fairMoves\":" << pair->fair_d1.moves
             << ",\"candidateScore\":" << pair->candidate.score
             << ",\"candidateMoves\":" << pair->candidate.moves
             << ",\"switches\":" << pair->candidate.switches
             << ",\"switchRecords\":[";
      for (std::size_t switch_index = 0;
           switch_index < pair->candidate.switch_records.size();
           ++switch_index) {
        if (switch_index != 0) output << ',';
        const SwitchRecord& record =
            pair->candidate.switch_records[switch_index];
        output << "{\"move\":" << record.move
               << ",\"fairAction\":" << record.fair_action
               << ",\"selectedAction\":" << record.selected_action
               << ",\"audit\":";
        writeAudit(output, record.audit);
        output << '}';
      }
      output << "]}";
    }
    output << "]}";
  }
  output << ",\n  \"qualified\":"
         << (gate != nullptr && gate->passed ? "true" : "false")
         << ",\n  \"totalWallSeconds\":" << total_wall_seconds
         << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
  output.close();
  if (!output) throw std::runtime_error("could not finish terminal artifact");
}

struct ReplayResult {
  std::vector<std::optional<GameResult>> games;
  int completed = 0;
  bool aborted = false;
  std::string abort_reason;
  double wall_seconds = 0.0;
};

ReplayResult replayCandidatePanels(int threads, const GateProfile& gate,
                                   const Deadline& deadline) {
  const auto started = Clock::now();
  ReplayResult result;
  result.games.resize(kScreenGames);
  std::atomic<int> next{0};
  std::atomic<int> completed{0};
  std::atomic<bool> stopped{false};
  std::mutex failure_mutex;
  std::vector<std::future<void>> workers;
  const int worker_count = std::max(1, std::min(threads, kScreenGames));
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [&, worker]() {
      static_cast<void>(worker);
      while (!stopped.load()) {
        const int game = next.fetch_add(1);
        if (game >= kScreenGames) return;
        try {
          const std::uint32_t seed =
              kScreenSeedStart + static_cast<std::uint32_t>(game);
          GameResult replay =
              runGame(seed, Policy::kConservative, gate, deadline, true);
          if (replay.panel_records.size() !=
              static_cast<std::size_t>(replay.moves)) {
            throw std::runtime_error("deployment-panel replay lost roots");
          }
          result.games[static_cast<std::size_t>(game)] = std::move(replay);
          const int done = completed.fetch_add(1) + 1;
          const GameResult& stored =
              *result.games[static_cast<std::size_t>(game)];
          const std::lock_guard<std::mutex> lock(report_mutex);
          std::cerr << "terminal-policy export-replay " << done << '/'
                    << kScreenGames << " seed=0x" << std::hex << seed
                    << std::dec << " candidate=" << stored.score << '/'
                    << stored.moves << " panels="
                    << stored.panel_records.size() << '\n';
        } catch (const std::exception& error) {
          stopped.store(true);
          const std::lock_guard<std::mutex> lock(failure_mutex);
          if (result.abort_reason.empty()) result.abort_reason = error.what();
        }
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.completed = completed.load();
  result.aborted = result.completed != kScreenGames;
  result.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

std::uint64_t writeDeploymentPanels(const std::string& path,
                                    const ReplayResult& replay,
                                    const GateProfile& gate) {
  std::ofstream output(path, std::ios::trunc);
  if (!output) throw std::runtime_error("could not open panel JSONL");
  output << std::setprecision(12);
  std::uint64_t records = 0;
  for (const std::optional<GameResult>& optional_game : replay.games) {
    if (!optional_game.has_value()) continue;
    const GameResult& game = *optional_game;
    for (const DeploymentPanelRecord& record : game.panel_records) {
      // Provenance is explicitly separated from modelInput.  A downstream
      // evaluator must consume only modelInput (plus action) as its features.
      output << "{\"recordType\":\"deployment-panel-export-replay\","
             << "\"provenance\":{\"screenSeed\":" << game.seed
             << ",\"moveIndex\":" << record.move_index
             << ",\"canonicalPublicHash\":\""
             << hex64(record.canonical_public_hash)
             << "\",\"tapeSeed\":" << record.tape_seed << "},"
             << "\"modelInput\":{\"board\":\""
             << serializeBoard(record.public_state.board)
             << "\",\"nextDisc\":"
             << static_cast<int>(record.public_state.next_disc)
             << ",\"movesRemaining\":"
             << static_cast<int>(record.public_state.moves_remaining)
             << ",\"terminal\":"
             << (record.public_state.terminal ? "true" : "false") << "},"
             << "\"excludedFromModelInput\":[\"screenSeed\",\"moveIndex\","
                "\"canonicalPublicHash\",\"tapeSeed\",\"score\","
                "\"level\",\"history\",\"scenario\"],"
             << "\"gate\":\"" << gate.name << "\","
             << "\"fairD1Action\":" << record.fair_d1_action
             << ",\"selectedAction\":" << record.selected_action
             << ",\"switched\":"
             << (record.switched ? "true" : "false")
             << ",\"actions\":[";
      for (int action = 0; action < kBoardSize; ++action) {
        if (action != 0) output << ',';
        const ActionPanelSummary& summary = record.actions[action];
        if (!summary.legal) {
          output << "null";
          continue;
        }
        output << "{\"action\":" << action
               << ",\"meanScoreReturn\":" << summary.mean_score_return
               << ",\"meanSurvivedMoves\":"
               << summary.mean_survived_moves
               << ",\"meanNumberedClears\":"
               << summary.mean_numbered_clears
               << ",\"meanCoversRevealed\":"
               << summary.mean_covers_revealed
               << ",\"survivingCutoffs\":" << summary.surviving_cutoffs
               << ",\"pairedVsFairD1\":";
        writeAudit(output, summary.paired_vs_fair_d1);
        output << '}';
      }
      output << "]}\n";
      ++records;
    }
  }
  output.close();
  if (!output) throw std::runtime_error("could not finish panel JSONL");
  return records;
}

int exportReplay(const Options& options, std::ostream& output) {
  const Deadline deadline;
  const CalibrationResult calibration = calibrateGate(options.threads, deadline);
  const GateProfile& selected =
      kGateProfiles[static_cast<std::size_t>(calibration.selected_profile)];
  const ReplayResult replay =
      replayCandidatePanels(options.threads, selected, deadline);
  if (replay.aborted) {
    throw std::runtime_error("panel export replay aborted after " +
                             std::to_string(replay.completed) + " games: " +
                             replay.abort_reason);
  }
  const std::uint64_t records =
      writeDeploymentPanels(options.root_output, replay, selected);
  std::uint64_t expected_records = 0;
  int switches = 0;
  for (const std::optional<GameResult>& game : replay.games) {
    if (!game.has_value()) continue;
    expected_records += game->panel_records.size();
    switches += game->switches;
  }
  if (records != expected_records || records == 0) {
    throw std::runtime_error("panel JSONL record accounting failed");
  }
  enforceRssLimit();
  output << std::fixed << std::setprecision(3)
         << "TERMINAL_POLICY_PANEL_EXPORT {\"exportReplay\":true,"
         << "\"policyChanged\":false,\"calibratedGate\":\""
         << selected.name << "\",\"completedGames\":" << replay.completed
         << ",\"records\":" << records << ",\"switches\":" << switches
         << ",\"wallSeconds\":" << deadline.elapsedSeconds()
         << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"path\":\"" << jsonEscape(options.root_output) << "\"}\n";
  return 0;
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
    expect(count == 1, "chance event was not exactly 255-stratified");
  }
}

bool selfTest(std::ostream& output) {
  expect(kLevelBonus == 17'000, "corrected level bonus regression");
  const ObservableState fixture = asymmetricFixture();
  const FairD1Decision fair_first = chooseFairDepthOne(fixture);
  const FairD1Decision fair_second = chooseFairDepthOne(fixture);
  expect(fair_first == fair_second && fair_first.complete &&
             fair_first.cache_entries == 0 &&
             fair_first.work <= kMaximumFairWorkPerCall &&
             isLegal(fixture.board, fair_first.action),
         "exact fair-D1 determinism/completion failed");

  const RolloutEvaluation first =
      evaluateRollouts(fixture, kDeploymentPanelDomain, nullptr, 2);
  const PolicyDecision fallback = chooseWithGate(first, kGateProfiles.back());
  expect(fallback.action == first.fair_d1_action || fallback.switched,
         "selector did not preserve exact fair-D1 fallback");
  RolloutEvaluation no_switch = first;
  for (int action = 0; action < kBoardSize; ++action) {
    if (no_switch.legal[action]) {
      no_switch.actions[action].paired_vs_fair_d1.score.lower_one_sided_99 =
          -1.0;
      no_switch.actions[action].paired_vs_fair_d1.moves.lower_one_sided_99 =
          -1.0;
    }
  }
  const PolicyDecision exact_fallback =
      chooseWithGate(no_switch, kGateProfiles.front());
  expect(!exact_fallback.switched &&
             exact_fallback.action == first.fair_d1_action,
         "failed exact fair-D1 fallback");

  const RolloutEvaluation reflected = evaluateRollouts(
      mirror(fixture), kDeploymentPanelDomain, nullptr, 2);
  expect(reflected.fair_d1_action ==
             kBoardSize - 1 - first.fair_d1_action &&
             reflected.canonical_public_hash == first.canonical_public_hash &&
             reflected.tape_seed == first.tape_seed,
         "reflection/canonical tape failed");
  for (int column = 0; column < kBoardSize; ++column) {
    expect(reflected.legal[kBoardSize - 1 - column] == first.legal[column],
           "reflected legal mask failed");
    if (first.legal[column]) {
      expect(reflected.actions[kBoardSize - 1 - column].scenarios ==
                 first.actions[column].scenarios,
             "reflected sibling scenarios failed");
    }
  }

  State metadata = materialize(fixture);
  metadata.score = 9'876'543;
  metadata.level = 81;
  metadata.moves_played = 731;
  const ObservableState normalized = observable(metadata);
  expect(normalized == fixture && chooseFairDepthOne(normalized) == fair_first &&
             publicHash(normalized) == publicHash(fixture),
         "public policy used excluded metadata");

  constexpr std::uint32_t test_seed = 0x1234'5678u;
  for (const int event : {0, 1, 63, 64, 12'799}) {
    verifyExactStrata(test_seed, kRevealTapeDomain, event);
    verifyExactStrata(test_seed, kVisibleTapeDomain, event);
  }
  ObservableState tape_fixture;
  tape_fixture.board.fill(kEmpty);
  tape_fixture.board[indexOf(6, 1)] = kCracked;
  tape_fixture.next_disc = 1;
  tape_fixture.moves_remaining = 4;
  MoveResult standard;
  expect(playSyntheticMove(tape_fixture, 0, test_seed, 0, 0, standard),
         "tape fixture failed");
  bool visible_discriminates = false;
  bool reveal_discriminates = false;
  for (std::uint32_t salt = 1; salt < 512; ++salt) {
    MoveResult changed_visible;
    expect(playSyntheticMove(tape_fixture, 0, test_seed, 0, 0,
                             changed_visible, kRevealTapeDomain,
                             kVisibleTapeDomain ^ salt),
           "visible-domain fixture failed");
    if (changed_visible.state.next_disc != standard.state.next_disc) {
      expect(changed_visible.state.board == standard.state.board &&
                 changed_visible.score_delta == standard.score_delta,
             "visible domain leaked into reveal events");
      visible_discriminates = true;
    }
    MoveResult changed_reveal;
    expect(playSyntheticMove(tape_fixture, 0, test_seed, 0, 0,
                             changed_reveal, kRevealTapeDomain ^ salt,
                             kVisibleTapeDomain),
           "reveal-domain fixture failed");
    if (changed_reveal.state.board != standard.state.board) {
      expect(changed_reveal.state.next_disc == standard.state.next_disc,
             "reveal domain leaked into visible events");
      reveal_discriminates = true;
    }
  }
  expect(visible_discriminates && reveal_discriminates,
         "chance domains were not independent/discriminating");

  std::vector<double> constants(kScenarios, 2.0);
  const PairedMetric constant_metric = pairedMetric(constants);
  expect(std::abs(constant_metric.mean - 2.0) < 1e-12 &&
             constant_metric.standard_error < 1e-12 &&
             std::abs(constant_metric.lower_one_sided_99 - 2.0) < 1e-12,
         "paired confidence constant-vector math failed");
  constants[0] = -2.0;
  const PairedMetric varied_metric = pairedMetric(constants);
  expect(varied_metric.standard_error > 0.0 &&
             varied_metric.lower_one_sided_99 < varied_metric.mean,
         "paired confidence variance/bound failed");
  expect(wilsonUpper99(0, kScenarios) > 0.0 &&
             wilsonUpper99(0, kScenarios) < 0.03 &&
             std::abs(wilsonUpper99(kScenarios, kScenarios) - 1.0) < 1e-12,
         "one-sided Wilson downside bound failed");
  PairedActionAudit passing;
  passing.score.lower_one_sided_99 = 1'000.0;
  passing.moves.lower_one_sided_99 = 0.25;
  passing.material_downside_upper99 = 0.25;
  expect(passesGate(passing, kGateProfiles.front()),
         "inclusive conservative gate boundary failed");
  passing.moves.lower_one_sided_99 = std::nextafter(0.25, 0.0);
  expect(!passesGate(passing, kGateProfiles.front()),
         "conservative gate accepted sub-bound action");

  expect(kMaximumFairCallsPerDecision == 355'216 &&
             kMaximumFairWorkPerDecision == 24'865'120 &&
             kMaximumSyntheticTransitionsPerDecision == 357'000,
         "static resource proof changed");
  expect(allowedGameplaySeed(kCalibrationSeedStart,
                             SeedCohort::kCalibration) &&
             allowedGameplaySeed(kScreenSeedStart + 7,
                                 SeedCohort::kScreen),
         "authorized 0x3d6d seeds rejected");
  expect(throwsInvalid([] {
           requireGameplaySeed(0x3d3a'0000u, SeedCohort::kCalibration);
         }) &&
             throwsInvalid([] {
               requireGameplaySeed(0x3d68'0000u, SeedCohort::kScreen);
             }) &&
             throwsInvalid([] {
               requireGameplaySeed(0x3d69'0000u, SeedCohort::kScreen);
             }) &&
             throwsInvalid([] {
               requireGameplaySeed(0x3d6b'0000u, SeedCohort::kScreen);
             }) &&
             throwsInvalid([] {
               requireGameplaySeed(0x3d6c'0000u, SeedCohort::kScreen);
             }) &&
             throwsInvalid([] {
               requireGameplaySeed(0x4d6d'0000u, SeedCohort::kScreen);
             }) &&
             throwsInvalid([] {
               requireGameplaySeed(0x7d6d'0000u, SeedCohort::kScreen);
             }) &&
             throwsInvalid([] {
               requireGameplaySeed(0xd76d'0000u, SeedCohort::kScreen);
             }),
         "protected seed guards failed");

  enforceRssLimit();
  output << std::setprecision(12)
         << "TERMINAL_POLICY_ITERATION_SELF_TEST {\"passed\":true,"
         << "\"exactD1Fallback\":true,\"publicBoundary\":true,"
         << "\"metadataBlind\":true,\"reflection\":true,"
         << "\"legalActions\":true,\"scenarios\":" << kScenarios
         << ",\"horizon\":" << kHorizon
         << ",\"eventStratified\":true,\"domainIndependent\":true,"
         << "\"confidenceMath\":true,\"resourceProof\":true,"
         << "\"seedGuards\":true,\"peakRssBytes\":" << peakRssBytes()
         << "}\n";
  return true;
}

int run(const Options& options, std::ostream& output) {
  const Deadline deadline;
  const CalibrationResult calibration = calibrateGate(options.threads, deadline);
  const GateProfile& selected =
      kGateProfiles[static_cast<std::size_t>(calibration.selected_profile)];

  const int parallel_batches =
      (kScreenGames + options.threads - 1) / options.threads;
  const double projected_screen_seconds =
      calibration.maximum_evaluation_seconds * calibration.mean_path_moves *
      parallel_batches * kProjectionSafetyFactor;
  const double projected_total_seconds = deadline.elapsedSeconds() +
                                         projected_screen_seconds +
                                         kProjectionFixedReserveSeconds;
  const bool projection_passed = projected_total_seconds <= kWallLimitSeconds;
  if (!projection_passed) {
    writeArtifact(options, calibration, projected_total_seconds, false,
                  nullptr, nullptr, nullptr, nullptr,
                  deadline.elapsedSeconds());
    output << std::fixed << std::setprecision(3)
           << "TERMINAL_POLICY_ITERATION_RESULT {\"calibratedGate\":\""
           << selected.name << "\",\"calibrationQualified\":"
           << (calibration.any_profile_qualified ? "true" : "false")
           << ",\"screenOpened\":false,\"reason\":\"measured wall "
              "projection exceeded cap\",\"projectedTotalSeconds\":"
           << projected_total_seconds << ",\"peakRssBytes\":"
           << peakRssBytes() << ",\"artifact\":\""
           << jsonEscape(options.output) << "\"}\n";
    return 2;
  }

  const ScreenResult screen = runScreen(options.threads, selected, deadline);
  const Summary baseline = summarize(screen, Policy::kFairD1);
  const Summary candidate = summarize(screen, Policy::kConservative);
  const ScreenGate gate = screenGate(screen, baseline, candidate);
  enforceRssLimit();
  writeArtifact(options, calibration, projected_total_seconds, true, &screen,
                &baseline, &candidate, &gate, deadline.elapsedSeconds());
  output << std::fixed << std::setprecision(3)
         << "TERMINAL_POLICY_ITERATION_RESULT {\"calibratedGate\":\""
         << selected.name << "\",\"calibrationQualified\":"
         << (calibration.any_profile_qualified ? "true" : "false")
         << ",\"screenOpened\":true,\"completedGames\":"
         << screen.completed << ",\"fairScore\":" << baseline.mean_score
         << ",\"fairMoves\":" << baseline.mean_moves
         << ",\"candidateScore\":" << candidate.mean_score
         << ",\"candidateMoves\":" << candidate.mean_moves
         << ",\"scoreRatio\":" << gate.score_ratio
         << ",\"moveRatio\":" << gate.move_ratio
         << ",\"jointWins\":" << gate.joint_wins
         << ",\"switches\":" << candidate.switches
         << ",\"passed\":" << (gate.passed ? "true" : "false")
         << ",\"totalWallSeconds\":" << deadline.elapsedSeconds()
         << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"artifact\":\"" << jsonEscape(options.output) << "\"}\n";
  return gate.passed ? 0 : 2;
}

}  // namespace drop7::terminal_policy_iteration

#ifndef DROP7_TERMINAL_POLICY_ITERATION_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::terminal_policy_iteration::selfTest(std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::terminal_policy_iteration::parseOptions(argc, argv, 2);
      return drop7::terminal_policy_iteration::run(options, std::cout);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--export-replay") {
      const auto options =
          drop7::terminal_policy_iteration::parseOptions(argc, argv, 2);
      return drop7::terminal_policy_iteration::exportReplay(options,
                                                             std::cout);
    }
    std::cerr << "usage: drop7_terminal_policy_iteration --self-test | "
                 "--run [--output PATH] [--threads N] | --export-replay "
                 "[--root-output PATH] [--threads N]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_terminal_policy_iteration: " << error.what() << '\n';
    return 1;
  }
}
#endif
