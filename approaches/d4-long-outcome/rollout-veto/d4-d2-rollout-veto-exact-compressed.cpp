#define DROP7_D4_D2_ROLLOUT_VETO_LIBRARY
#include "d4-d2-rollout-veto.cpp"
#undef DROP7_D4_D2_ROLLOUT_VETO_LIBRARY

#include <unordered_map>

// Compresses the fixed 25-move fair-D2 rollout veto while preserving outcomes.
// It may replay only the previously evaluated 0x3ded0000 development pilot.
// The D4 root-Q veto is applied before simulation, the D4 baseline is evaluated first and
// once, deterministic public D2 actions are memoized by canonical observable
// state, and exact convergent continuations are reused only when public state,
// scenario, and tape step all match.
namespace drop7::d4_d2_rollout_veto_exact_compressed {

namespace original = drop7::d4_d2_rollout_veto;
namespace d4 = drop7::fair_only_depth4;
namespace fair = drop7::fair_only_horizon;

constexpr std::uint32_t kPilotSeed = original::kFittingSeedStart;
constexpr std::size_t kActionMemoCapacity =
    original::kWorstD2CallsPerRoutedDecision;
constexpr std::uint64_t kMaximumRssBytes = 128u * 1024u * 1024u;
constexpr double kFrozenBaselineSeconds = 191.369044375;
constexpr double kFrozenOriginalCandidateSeconds = 852.25518475;
constexpr std::uint64_t kFrozenOriginalSyntheticTransitions = 201'677;
constexpr std::uint64_t kFrozenOriginalD2Calls = 192'983;
constexpr std::uint64_t kFrozenOriginalD2Work = 388'480'293;
constexpr std::string_view kOriginalArtifact =
    "/tmp/drop7-d4-d2-rollout-veto.json";
constexpr std::string_view kOriginalArtifactSha256 =
    "5841c90412d21c0a42ee6adc7a2233b3b585087bcc04f54fab7ef3274d3a607e";

static_assert(kPilotSeed == 0x3ded'0000u);
static_assert(original::kRolloutHorizon == 25);
static_assert(original::kScenarios == 7);
static_assert(original::kContinuationDepth == 2);
static_assert(original::kMaximumRootQLoss == 7'000.0);
static_assert(original::kFullProtocolProjectionWaves == 18.0);
static_assert(original::kMaximumProjectedWallSeconds == 2'700.0);
static_assert(kActionMemoCapacity ==
              original::kWorstD2CallsPerRoutedDecision);

struct Options {
  std::string output =
      "/tmp/drop7-d4-d2-rollout-veto-exact-compressed.json";
  std::string trace_output =
      "/tmp/drop7-d4-d2-rollout-veto-exact-compressed-trace.jsonl";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string argument = argv[index];
    if (argument == "--output") {
      result.output = argv[index + 1];
    } else if (argument == "--trace-output") {
      result.trace_output = argv[index + 1];
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  return result;
}

struct ObservableKey {
  Board board{};
  std::uint8_t next_disc = 1;
  int moves_remaining = kMovesPerLevel;
  bool game_over = false;

  bool operator==(const ObservableKey&) const = default;
};

ObservableKey directKey(const original::ObservableState& state) {
  return {state.board, state.next_disc, state.moves_remaining,
          state.game_over};
}

original::ObservableState stateFromKey(const ObservableKey& key) {
  return {key.board, key.next_disc, key.moves_remaining, key.game_over};
}

ObservableKey canonicalKey(const original::ObservableState& source,
                           bool& mirrored) {
  const State canonical =
      cfpi::detail::canonicalState(original::materialize(source), mirrored);
  return directKey(original::observable(canonical));
}

struct ObservableKeyHash {
  std::size_t operator()(const ObservableKey& key) const {
    std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
    for (const std::uint8_t cell : key.board) {
      hash ^= static_cast<std::uint64_t>(cell + 1u);
      hash *= 0x0000'0100'0000'01b3ull;
    }
    hash ^= key.next_disc;
    hash *= 0x0000'0100'0000'01b3ull;
    hash ^= static_cast<std::uint64_t>(key.moves_remaining + 1);
    hash *= 0x0000'0100'0000'01b3ull;
    hash ^= static_cast<std::uint64_t>(key.game_over);
    return static_cast<std::size_t>(original::mix64(hash));
  }
};

void addD2Metrics(original::D2Metrics& target,
                  const original::D2Metrics& source) {
  target.calls += source.calls;
  target.work += source.work;
  target.nodes += source.nodes;
  target.cache_hits += source.cache_hits;
  target.peak_cache_entries =
      std::max(target.peak_cache_entries, source.peak_cache_entries);
  target.root_actions += source.root_actions;
  target.full_root = target.full_root && source.full_root;
}

struct CompressionMetrics {
  std::uint64_t legal_root_actions = 0;
  std::uint64_t evaluated_root_actions = 0;
  std::uint64_t root_q_filtered_actions = 0;
  std::uint64_t logical_synthetic_transitions = 0;
  std::uint64_t actual_synthetic_transitions = 0;
  std::uint64_t logical_d2_queries = 0;
  std::uint64_t action_memo_queries = 0;
  std::uint64_t action_memo_hits = 0;
  std::uint64_t action_memo_misses = 0;
  std::uint64_t action_memo_evictions = 0;
  std::size_t action_memo_entries = 0;
  std::size_t peak_action_memo_entries = 0;
  std::uint64_t suffix_queries = 0;
  std::uint64_t suffix_hits = 0;
  std::uint64_t suffix_misses = 0;
  std::uint64_t suffix_saved_transitions = 0;
  std::uint64_t suffix_saved_d2_queries = 0;
  std::size_t suffix_entries = 0;
  std::size_t peak_suffix_entries = 0;
  original::D2Metrics continuation{};
};

class D2ActionMemo {
 public:
  using Evaluator = int (*)(const original::ObservableState&,
                            original::D2Metrics*);

  explicit D2ActionMemo(
      std::size_t capacity = kActionMemoCapacity,
      Evaluator evaluator = &original::fairDepthTwoAction)
      : capacity_(capacity), evaluator_(evaluator) {
    if (capacity_ == 0) throw std::invalid_argument("zero D2 memo capacity");
    if (evaluator_ == nullptr) throw std::invalid_argument("null D2 evaluator");
    entries_.reserve(capacity_);
  }

  void beginDecision(CompressionMetrics& metrics) {
    if (active_ != nullptr) throw std::logic_error("nested D2 memo decision");
    entries_.clear();
    active_ = &metrics;
    active_->action_memo_entries = entries_.size();
    active_->peak_action_memo_entries = entries_.size();
  }

  void endDecision() {
    if (active_ == nullptr) throw std::logic_error("D2 memo not active");
    active_->action_memo_entries = entries_.size();
    active_->peak_action_memo_entries =
        std::max(active_->peak_action_memo_entries, entries_.size());
    active_ = nullptr;
  }

  int action(const original::ObservableState& source) {
    if (active_ == nullptr) throw std::logic_error("D2 memo outside decision");
    if (source.game_over) return -1;
    ++active_->action_memo_queries;
    bool mirrored = false;
    const ObservableKey key = canonicalKey(source, mirrored);
    const auto found = entries_.find(key);
    if (found != entries_.end()) {
      ++active_->action_memo_hits;
      const int canonical_action = found->second;
      return mirrored ? kBoardSize - 1 - canonical_action : canonical_action;
    }

    ++active_->action_memo_misses;
    original::D2Metrics call_metrics;
    const int canonical_action =
        evaluator_(stateFromKey(key), &call_metrics);
    addD2Metrics(active_->continuation, call_metrics);
    if (entries_.size() >= capacity_) {
      throw std::runtime_error("decision-local D2 memo exceeded proof bound");
    }
    entries_.emplace(key, canonical_action);
    active_->action_memo_entries = entries_.size();
    active_->peak_action_memo_entries =
        std::max(active_->peak_action_memo_entries, entries_.size());
    return mirrored ? kBoardSize - 1 - canonical_action : canonical_action;
  }

  std::size_t size() const { return entries_.size(); }

 private:
  std::size_t capacity_;
  Evaluator evaluator_;
  std::unordered_map<ObservableKey, int, ObservableKeyHash> entries_;
  CompressionMetrics* active_ = nullptr;
};

struct SuffixKey {
  ObservableKey state{};
  int scenario = 0;
  int step = 0;

  bool operator==(const SuffixKey&) const = default;
};

struct SuffixKeyHash {
  std::size_t operator()(const SuffixKey& key) const {
    const std::uint64_t state_hash = ObservableKeyHash{}(key.state);
    const std::uint64_t tagged =
        state_hash ^ (static_cast<std::uint64_t>(key.scenario + 1) << 48u) ^
        (static_cast<std::uint64_t>(key.step + 1) << 56u);
    return static_cast<std::size_t>(original::mix64(tagged));
  }
};

// Scores and terminal utility are integral.  Keeping the accumulated score
// separate from the one final floating leaf preserves the reference addition
// order exactly when materializing ScenarioOutcome::value.
struct TrajectoryOutcome {
  std::int64_t score_sum = 0;
  int numbered_clears = 0;
  bool survived_horizon = false;
  bool terminal = false;
  double leaf = 0.0;
  std::uint64_t logical_transitions = 0;
  std::uint64_t logical_d2_queries = 0;

  original::ScenarioOutcome materialize() const {
    const double tail = terminal ? fair::kTerminalUtility : leaf;
    return {static_cast<double>(score_sum) + tail, numbered_clears,
            survived_horizon};
  }
};

void addMove(const MoveResult& move, TrajectoryOutcome& outcome) {
  outcome.score_sum += move.score_delta;
  for (const Wave& wave : move.waves) {
    outcome.numbered_clears += wave.cleared;
  }
}

void prepend(TrajectoryOutcome& target, const TrajectoryOutcome& suffix) {
  target.score_sum += suffix.score_sum;
  target.numbered_clears += suffix.numbered_clears;
  target.survived_horizon = suffix.survived_horizon;
  target.terminal = suffix.terminal;
  target.leaf = suffix.leaf;
  target.logical_transitions += suffix.logical_transitions;
  target.logical_d2_queries += suffix.logical_d2_queries;
}

class DecisionRolloutContext {
 public:
  DecisionRolloutContext(const original::ObservableState& root, int horizon,
                         D2ActionMemo& actions,
                         CompressionMetrics& metrics)
      : horizon_(horizon),
        tape_seed_(original::seed32(
            original::publicHash(root) ^
            static_cast<std::uint64_t>(original::kTapeSeedDomain))),
        actions_(actions),
        metrics_(metrics) {
    if (horizon_ < 1 || horizon_ > original::kRolloutHorizon) {
      throw std::invalid_argument("invalid compressed rollout horizon");
    }
    suffixes_.reserve(original::kWorstD2CallsPerRoutedDecision);
  }

  original::ActionRollout evaluateRootAction(
      const original::ObservableState& root, int action) {
    if (!isLegal(root.board, action)) {
      throw std::invalid_argument("compressed rollout root action illegal");
    }
    original::ActionRollout result;
    for (int scenario = 0; scenario < original::kScenarios; ++scenario) {
      TrajectoryOutcome trajectory;
      MoveResult move;
      if (!original::playSyntheticMove(
              root, action, tape_seed_, scenario, 0, move,
              &metrics_.actual_synthetic_transitions)) {
        trajectory.terminal = true;
      } else {
        ++trajectory.logical_transitions;
        addMove(move, trajectory);
        const original::ObservableState next = original::observable(move.state);
        if (next.game_over) {
          trajectory.terminal = true;
        } else if (horizon_ == 1) {
          trajectory.survived_horizon = true;
          trajectory.leaf = fair::fairLeaf(original::materialize(next));
        } else {
          prepend(trajectory, evaluateSuffix(next, scenario, 1));
        }
      }
      const original::ScenarioOutcome outcome = trajectory.materialize();
      result.scenarios[scenario] = outcome;
      result.mean_value += outcome.value / original::kScenarios;
      result.mean_numbered_clears +=
          static_cast<double>(outcome.numbered_clears) /
          original::kScenarios;
      result.surviving_scenarios += outcome.survived_horizon;
      metrics_.logical_synthetic_transitions +=
          trajectory.logical_transitions;
      metrics_.logical_d2_queries += trajectory.logical_d2_queries;
    }
    return result;
  }

  std::size_t entries() const { return suffixes_.size(); }

 private:
  TrajectoryOutcome evaluateSuffix(const original::ObservableState& state,
                                   int scenario, int step) {
    if (step <= 0 || step >= horizon_) {
      throw std::logic_error("invalid exact suffix step");
    }
    const SuffixKey key{directKey(state), scenario, step};
    ++metrics_.suffix_queries;
    const auto cached = suffixes_.find(key);
    if (cached != suffixes_.end()) {
      ++metrics_.suffix_hits;
      metrics_.suffix_saved_transitions +=
          cached->second.logical_transitions;
      metrics_.suffix_saved_d2_queries +=
          cached->second.logical_d2_queries;
      return cached->second;
    }
    ++metrics_.suffix_misses;

    TrajectoryOutcome result;
    ++result.logical_d2_queries;
    const int selected = actions_.action(state);
    if (!isLegal(state.board, selected)) {
      result.terminal = true;
    } else {
      MoveResult move;
      if (!original::playSyntheticMove(
              state, selected, tape_seed_, scenario, step, move,
              &metrics_.actual_synthetic_transitions)) {
        result.terminal = true;
      } else {
        ++result.logical_transitions;
        addMove(move, result);
        const original::ObservableState next = original::observable(move.state);
        if (next.game_over) {
          result.terminal = true;
        } else if (step + 1 == horizon_) {
          result.survived_horizon = true;
          result.leaf = fair::fairLeaf(original::materialize(next));
        } else {
          prepend(result, evaluateSuffix(next, scenario, step + 1));
        }
      }
    }
    suffixes_.emplace(key, result);
    metrics_.suffix_entries = suffixes_.size();
    metrics_.peak_suffix_entries =
        std::max(metrics_.peak_suffix_entries, suffixes_.size());
    return result;
  }

  int horizon_ = original::kRolloutHorizon;
  std::uint32_t tape_seed_ = 0;
  D2ActionMemo& actions_;
  CompressionMetrics& metrics_;
  std::unordered_map<SuffixKey, TrajectoryOutcome, SuffixKeyHash> suffixes_;
};

struct SelectiveRollout {
  std::array<original::ActionRollout, kBoardSize> actions{};
  std::array<bool, kBoardSize> legal{};
  std::array<bool, kBoardSize> root_q_admissible{};
  std::array<bool, kBoardSize> evaluated{};
  CompressionMetrics metrics{};
};

SelectiveRollout evaluateSelective(
    const original::ObservableState& source,
    const d4::SearchDecision& search, int d4_action, int horizon,
    D2ActionMemo& action_memo) {
  if (source.game_over || !isLegal(source.board, d4_action)) {
    throw std::invalid_argument("invalid compressed rollout root");
  }
  SelectiveRollout result;
  bool mirrored = false;
  const State canonical_state = cfpi::detail::canonicalState(
      original::materialize(source), mirrored);
  const original::ObservableState root =
      original::observable(canonical_state);
  const auto sourceAction = [mirrored](int canonical_action) {
    return mirrored ? kBoardSize - 1 - canonical_action : canonical_action;
  };
  const int canonical_d4_action =
      mirrored ? kBoardSize - 1 - d4_action : d4_action;
  const double baseline_q = search.root_values[d4_action];
  if (!std::isfinite(baseline_q)) {
    throw std::runtime_error("non-finite D4 baseline root Q");
  }
  for (int canonical_action = 0; canonical_action < kBoardSize;
       ++canonical_action) {
    if (!isLegal(root.board, canonical_action)) continue;
    const int action = sourceAction(canonical_action);
    result.legal[action] = true;
    ++result.metrics.legal_root_actions;
    const bool admissible =
        action == d4_action ||
        baseline_q - search.root_values[action] <=
            original::kMaximumRootQLoss + 1.0e-9;
    result.root_q_admissible[action] = admissible;
    result.metrics.root_q_filtered_actions += !admissible;
  }

  action_memo.beginDecision(result.metrics);
  DecisionRolloutContext context(root, horizon, action_memo, result.metrics);
  result.actions[d4_action] =
      context.evaluateRootAction(root, canonical_d4_action);
  result.evaluated[d4_action] = true;
  ++result.metrics.evaluated_root_actions;
  for (const int canonical_action : cfpi::detail::kColumnOrder) {
    const int action = sourceAction(canonical_action);
    if (action == d4_action || !result.legal[action] ||
        !result.root_q_admissible[action]) {
      continue;
    }
    result.actions[action] =
        context.evaluateRootAction(root, canonical_action);
    result.evaluated[action] = true;
    ++result.metrics.evaluated_root_actions;
  }
  result.metrics.suffix_entries = context.entries();
  action_memo.endDecision();

  const std::uint64_t worst_transitions =
      result.metrics.evaluated_root_actions * original::kScenarios * horizon;
  const std::uint64_t worst_d2_queries =
      result.metrics.evaluated_root_actions * original::kScenarios *
      static_cast<std::uint64_t>(std::max(0, horizon - 1));
  if (result.metrics.actual_synthetic_transitions >
          result.metrics.logical_synthetic_transitions ||
      result.metrics.logical_synthetic_transitions > worst_transitions ||
      result.metrics.action_memo_queries > result.metrics.logical_d2_queries ||
      result.metrics.logical_d2_queries > worst_d2_queries ||
      result.metrics.action_memo_hits + result.metrics.action_memo_misses !=
          result.metrics.action_memo_queries ||
      result.metrics.continuation.calls != result.metrics.action_memo_misses ||
      result.metrics.continuation.work >
          result.metrics.action_memo_misses * original::kWorstD2Work ||
      result.metrics.continuation.peak_cache_entries >
          original::kWorstD2CacheEntries ||
      result.metrics.action_memo_entries > kActionMemoCapacity ||
      result.metrics.suffix_entries > worst_d2_queries ||
      !result.metrics.continuation.full_root) {
    throw std::runtime_error("compressed rollout resource proof failed");
  }
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
  std::array<original::AlternativeTest, kBoardSize> tests{};
  std::array<bool, kBoardSize> compared{};
  SelectiveRollout rollout{};
  d4::SearchDecision search{};
};

Decision chooseAction(const State& source, D2ActionMemo& action_memo,
                      bool rollout_enabled = true,
                      int horizon = original::kRolloutHorizon) {
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
    throw std::runtime_error("compressed policy D4 did not complete");
  }
  result.danger = original::isDanger(original::observable(source));
  result.routed = rollout_enabled && result.danger;
  if (!result.routed) return result;

  const auto rollout_started = std::chrono::steady_clock::now();
  result.rollout = evaluateSelective(original::observable(source),
                                     result.search, result.d4_action,
                                     horizon, action_memo);
  result.rollout_seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() -
                               rollout_started)
                               .count();
  const original::ActionRollout& baseline =
      result.rollout.actions[result.d4_action];
  const double baseline_q = result.search.root_values[result.d4_action];
  int selected = result.d4_action;
  double best_lower = 0.0;
  original::AlternativeTest selected_test;
  for (const int action : cfpi::detail::kColumnOrder) {
    if (!result.rollout.legal[action] || action == result.d4_action) continue;
    if (!result.rollout.root_q_admissible[action]) {
      ++result.root_q_rejections;
      continue;
    }
    if (!result.rollout.evaluated[action]) {
      throw std::runtime_error("admissible rollout action was not evaluated");
    }
    const original::AlternativeTest test = original::testAlternative(
        result.rollout.actions[action], baseline,
        result.search.root_values[action], baseline_q);
    result.tests[action] = test;
    result.compared[action] = true;
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
    throw std::runtime_error("compressed rollout selected illegal action");
  }
  return result;
}

bool exactEvaluatedParity(const original::Decision& reference,
                          const Decision& compressed) {
  if (reference.action != compressed.action ||
      reference.d4_action != compressed.d4_action ||
      reference.switched != compressed.switched ||
      reference.passing_alternatives !=
          compressed.passing_alternatives ||
      reference.search.root_values != compressed.search.root_values) {
    return false;
  }
  for (int action = 0; action < kBoardSize; ++action) {
    if (!compressed.rollout.evaluated[action]) continue;
    if (!reference.rollout.legal[action] ||
        reference.rollout.actions[action] !=
            compressed.rollout.actions[action]) {
      return false;
    }
  }
  return true;
}

struct TraceRecord {
  original::ObservableState state{};
  Decision decision{};
};

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t numbered_cleared = 0;
  std::uint64_t covers_revealed = 0;
  int maximum_chain = 0;
  std::uint64_t routed_decisions = 0;
  std::uint64_t switches = 0;
  std::uint64_t passing_alternatives = 0;
  std::uint64_t root_q_rejections = 0;
  std::uint64_t d4_work = 0;
  std::uint64_t d4_nodes = 0;
  std::uint64_t d4_cache_hits = 0;
  std::size_t peak_d4_cache_entries = 0;
  CompressionMetrics compression{};
  double d4_seconds = 0.0;
  double rollout_seconds = 0.0;
  double elapsed_seconds = 0.0;
  std::uint64_t peak_rss_bytes = 0;
};

void addCompression(CompressionMetrics& target,
                    const CompressionMetrics& source) {
  target.legal_root_actions += source.legal_root_actions;
  target.evaluated_root_actions += source.evaluated_root_actions;
  target.root_q_filtered_actions += source.root_q_filtered_actions;
  target.logical_synthetic_transitions +=
      source.logical_synthetic_transitions;
  target.actual_synthetic_transitions += source.actual_synthetic_transitions;
  target.logical_d2_queries += source.logical_d2_queries;
  target.action_memo_queries += source.action_memo_queries;
  target.action_memo_hits += source.action_memo_hits;
  target.action_memo_misses += source.action_memo_misses;
  target.action_memo_evictions += source.action_memo_evictions;
  target.action_memo_entries = source.action_memo_entries;
  target.peak_action_memo_entries =
      std::max(target.peak_action_memo_entries,
               source.peak_action_memo_entries);
  target.suffix_queries += source.suffix_queries;
  target.suffix_hits += source.suffix_hits;
  target.suffix_misses += source.suffix_misses;
  target.suffix_saved_transitions += source.suffix_saved_transitions;
  target.suffix_saved_d2_queries += source.suffix_saved_d2_queries;
  target.suffix_entries += source.suffix_entries;
  target.peak_suffix_entries =
      std::max(target.peak_suffix_entries, source.peak_suffix_entries);
  addD2Metrics(target.continuation, source.continuation);
}

GameResult replayPilot(std::vector<TraceRecord>& records) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(kPilotSeed);
  GameResult result;
  result.seed = kPilotSeed;
  D2ActionMemo action_memo;
  while (!state.game_over && state.moves_played < original::kMaximumMoves) {
    Decision decision = chooseAction(state, action_memo);
    result.d4_work += decision.search.work;
    result.d4_nodes += decision.search.nodes;
    result.d4_cache_hits += decision.search.cache_hits;
    result.peak_d4_cache_entries =
        std::max(result.peak_d4_cache_entries,
                 decision.search.cache_entries);
    result.d4_seconds += decision.d4_seconds;
    result.rollout_seconds += decision.rollout_seconds;
    result.routed_decisions += decision.routed;
    result.switches += decision.switched;
    result.passing_alternatives += decision.passing_alternatives;
    result.root_q_rejections += decision.root_q_rejections;
    if (decision.routed) {
      addCompression(result.compression, decision.rollout.metrics);
      records.push_back({original::observable(state), std::move(decision)});
    }
    MoveResult move;
    const int action = decision.action;
    if (!playHeadlessMove(state, kPilotSeed, action, move)) {
      throw std::runtime_error("compressed pilot transition failed");
    }
    result.maximum_chain =
        std::max(result.maximum_chain, static_cast<int>(move.waves.size()));
    for (const Wave& wave : move.waves) {
      result.numbered_cleared += static_cast<std::uint64_t>(wave.cleared);
      result.covers_revealed += static_cast<std::uint64_t>(wave.revealed);
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.elapsed_seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
  result.peak_rss_bytes = d4::peakRssBytes();
  if (result.score != 404'047 || result.moves != 250 || result.censored ||
      result.numbered_cleared != 569 || result.covers_revealed != 329 ||
      result.routed_decisions != 179 || result.switches != 12 ||
      result.passing_alternatives != 15 ||
      result.d4_work != 353'804'442 || records.size() != 179) {
    throw std::runtime_error("compressed pilot diverged from frozen outcome");
  }
  if (result.peak_rss_bytes > kMaximumRssBytes) {
    throw std::runtime_error("compressed pilot exceeded RSS bound");
  }
  return result;
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

void writeCompression(std::ostream& output,
                      const CompressionMetrics& metrics) {
  output << "{\"legalRootActions\":" << metrics.legal_root_actions
         << ",\"evaluatedRootActions\":"
         << metrics.evaluated_root_actions
         << ",\"rootQFilteredActions\":"
         << metrics.root_q_filtered_actions
         << ",\"logicalSyntheticTransitions\":"
         << metrics.logical_synthetic_transitions
         << ",\"actualSyntheticTransitions\":"
         << metrics.actual_synthetic_transitions
         << ",\"logicalD2Queries\":" << metrics.logical_d2_queries
         << ",\"actionMemoQueries\":" << metrics.action_memo_queries
         << ",\"actionMemoHits\":" << metrics.action_memo_hits
         << ",\"actionMemoMisses\":" << metrics.action_memo_misses
         << ",\"actionMemoEvictions\":"
         << metrics.action_memo_evictions
         << ",\"actionMemoEntries\":" << metrics.action_memo_entries
         << ",\"peakActionMemoEntries\":"
         << metrics.peak_action_memo_entries
         << ",\"suffixQueries\":" << metrics.suffix_queries
         << ",\"suffixHits\":" << metrics.suffix_hits
         << ",\"suffixMisses\":" << metrics.suffix_misses
         << ",\"suffixSavedTransitions\":"
         << metrics.suffix_saved_transitions
         << ",\"suffixSavedD2Queries\":"
         << metrics.suffix_saved_d2_queries
         << ",\"suffixEntries\":" << metrics.suffix_entries
         << ",\"peakSuffixEntries\":" << metrics.peak_suffix_entries
         << ",\"actualD2Calls\":" << metrics.continuation.calls
         << ",\"actualD2Work\":" << metrics.continuation.work
         << ",\"actualD2Nodes\":" << metrics.continuation.nodes
         << ",\"innerD2CacheHits\":"
         << metrics.continuation.cache_hits
         << ",\"D2RootActions\":" << metrics.continuation.root_actions
         << ",\"peakInnerD2CacheEntries\":"
         << metrics.continuation.peak_cache_entries
         << ",\"D2FullRoot\":"
         << (metrics.continuation.full_root ? "true" : "false") << '}';
}

void writeTraceRecord(std::ostream& output, std::size_t index,
                      const TraceRecord& record) {
  const original::ObservableState& state = record.state;
  const Decision& decision = record.decision;
  output << std::setprecision(12)
         << "{\"type\":\"routed-decision\",\"index\":" << index
         << ",\"publicState\":{\"board\":";
  writeBoard(output, state.board);
  output << ",\"nextDisc\":" << static_cast<int>(state.next_disc)
         << ",\"movesRemaining\":" << state.moves_remaining
         << ",\"gameOver\":" << (state.game_over ? "true" : "false")
         << ",\"maximumHeight\":" << original::maximumHeight(state.board)
         << ",\"canonicalHash\":" << original::publicHash(state)
         << "},\"stockD4\":{\"action\":" << decision.d4_action
         << ",\"rootQ\":[";
  for (int action = 0; action < kBoardSize; ++action) {
    if (action != 0) output << ',';
    writeFiniteOrNull(output, decision.search.root_values[action]);
  }
  output << "],\"rootExpectedScore\":[";
  for (int action = 0; action < kBoardSize; ++action) {
    if (action != 0) output << ',';
    writeFiniteOrNull(output,
                      decision.search.root_expected_scores[action]);
  }
  output << "],\"work\":" << decision.search.work
         << ",\"nodes\":" << decision.search.nodes
         << ",\"cacheHits\":" << decision.search.cache_hits
         << ",\"cacheEntries\":" << decision.search.cache_entries
         << ",\"seconds\":" << decision.d4_seconds
         << "},\"selection\":{\"action\":" << decision.action
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
         << "},\"compression\":";
  writeCompression(output, decision.rollout.metrics);
  output << ",\"rolloutSeconds\":" << decision.rollout_seconds
         << ",\"actions\":[";
  const original::ActionRollout& baseline =
      decision.rollout.actions[decision.d4_action];
  for (int action = 0; action < kBoardSize; ++action) {
    if (action != 0) output << ',';
    if (!decision.rollout.legal[action]) {
      output << "null";
      continue;
    }
    output << "{\"action\":" << action << ",\"rootQ\":";
    writeFiniteOrNull(output, decision.search.root_values[action]);
    output << ",\"status\":\"";
    if (action == decision.d4_action) output << "baseline";
    else if (!decision.rollout.root_q_admissible[action]) output << "root-q-filtered";
    else output << "evaluated-alternative";
    output << "\",\"evaluated\":"
           << (decision.rollout.evaluated[action] ? "true" : "false");
    if (decision.rollout.evaluated[action]) {
      const original::ActionRollout& rollout =
          decision.rollout.actions[action];
      output << ",\"meanReturn\":" << rollout.mean_value
             << ",\"survivingScenarios\":"
             << rollout.surviving_scenarios
             << ",\"meanNumberedClears\":"
             << rollout.mean_numbered_clears;
      if (action != decision.d4_action) {
        const original::AlternativeTest& test = decision.tests[action];
        output << ",\"comparisonToD4\":{\"returnLower95\":"
               << test.return_lower95
               << ",\"survivorAdvantage\":"
               << test.survivor_advantage
               << ",\"clearAdvantage\":" << test.clear_advantage
               << ",\"rootQLoss\":" << test.root_q_loss
               << ",\"passed\":" << (test.passed ? "true" : "false")
               << '}';
      } else {
        static_cast<void>(baseline);
      }
      output << ",\"scenarios\":[";
      for (int scenario = 0; scenario < original::kScenarios; ++scenario) {
        if (scenario != 0) output << ',';
        const original::ScenarioOutcome& outcome =
            rollout.scenarios[scenario];
        output << "{\"scenario\":" << scenario
               << ",\"return\":" << outcome.value
               << ",\"numberedClears\":"
               << outcome.numbered_clears
               << ",\"survivedHorizon\":"
               << (outcome.survived_horizon ? "true" : "false") << '}';
      }
      output << ']';
    }
    output << '}';
  }
  output << "]}\n";
}

original::ObservableState routedFixture() {
  original::ObservableState result = original::asymmetricFixture();
  for (int row = 3; row < kBoardSize; ++row) {
    result.board[indexOf(row, 0)] = kSolid;
  }
  result.board[indexOf(2, 0)] = kEmpty;
  return result;
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

bool selfTest(std::ostream& output) {
  const bool dependency = original::selfTest(output);
  const original::ObservableState fixture = routedFixture();

  D2ActionMemo memo;
  CompressionMetrics memo_metrics;
  memo.beginDecision(memo_metrics);
  const int direct = original::fairDepthTwoAction(fixture);
  const int cached_first = memo.action(fixture);
  const int cached_repeat = memo.action(fixture);
  const int cached_mirror = memo.action(original::mirror(fixture));
  memo.endDecision();
  const bool canonical_action_memo =
      direct == cached_first && cached_repeat == direct &&
      cached_mirror == kBoardSize - 1 - direct &&
      memo_metrics.action_memo_queries == 3 &&
      memo_metrics.action_memo_misses == 1 &&
      memo_metrics.action_memo_hits == 2 &&
      memo_metrics.continuation.calls == 1;

  State state = original::materialize(fixture);
  const original::Decision reference = original::chooseAction(state, true, 3);
  D2ActionMemo decision_memo;
  const Decision compressed = chooseAction(state, decision_memo, true, 3);
  const bool reference_parity = exactEvaluatedParity(reference, compressed);
  int unequal_rollout_action = -1;
  int unequal_scenario = -1;
  for (int action = 0; action < kBoardSize; ++action) {
    if (compressed.rollout.evaluated[action] &&
        reference.rollout.actions[action] !=
            compressed.rollout.actions[action]) {
      unequal_rollout_action = action;
      for (int scenario = 0; scenario < original::kScenarios; ++scenario) {
        if (!(reference.rollout.actions[action].scenarios[scenario] ==
              compressed.rollout.actions[action].scenarios[scenario])) {
          unequal_scenario = scenario;
          break;
        }
      }
      break;
    }
  }

  State reflected_state = original::materialize(original::mirror(fixture));
  const original::Decision reflected_reference =
      original::chooseAction(reflected_state, true, 3);
  const Decision reflected =
      chooseAction(reflected_state, decision_memo, true, 3);
  const bool reflection_parity =
      exactEvaluatedParity(reflected_reference, reflected) &&
      reflected.action == kBoardSize - 1 - compressed.action &&
      reflected.d4_action == kBoardSize - 1 - compressed.d4_action;

  State metadata = state;
  metadata.score = 9'999'999;
  metadata.level = 73;
  metadata.moves_played = 777;
  D2ActionMemo metadata_memo;
  const Decision metadata_decision =
      chooseAction(metadata, metadata_memo, true, 3);
  const bool metadata_blind =
      metadata_decision.action == compressed.action &&
      metadata_decision.d4_action == compressed.d4_action &&
      metadata_decision.search.root_values == compressed.search.root_values &&
      metadata_decision.rollout.actions == compressed.rollout.actions &&
      metadata_decision.rollout.evaluated == compressed.rollout.evaluated;

  CompressionMetrics reuse_metrics;
  D2ActionMemo reuse_memo;
  reuse_memo.beginDecision(reuse_metrics);
  DecisionRolloutContext reuse_context(fixture, 3, reuse_memo, reuse_metrics);
  int repeated_action = -1;
  for (const int action : cfpi::detail::kColumnOrder) {
    if (isLegal(fixture.board, action)) {
      repeated_action = action;
      break;
    }
  }
  const original::ActionRollout reuse_first =
      reuse_context.evaluateRootAction(fixture, repeated_action);
  const std::uint64_t work_after_first = reuse_metrics.continuation.work;
  const original::ActionRollout reuse_second =
      reuse_context.evaluateRootAction(fixture, repeated_action);
  reuse_memo.endDecision();
  const bool exact_suffix_reuse =
      reuse_first == reuse_second && reuse_metrics.suffix_hits > 0 &&
      reuse_metrics.suffix_saved_transitions > 0 &&
      reuse_metrics.continuation.work == work_after_first;

  bool filtered_exact = true;
  for (int action = 0; action < kBoardSize; ++action) {
    if (!compressed.rollout.legal[action] ||
        compressed.rollout.root_q_admissible[action]) {
      continue;
    }
    filtered_exact =
        filtered_exact && !compressed.rollout.evaluated[action] &&
        compressed.search.root_values[compressed.d4_action] -
                compressed.search.root_values[action] >
            original::kMaximumRootQLoss + 1.0e-9;
  }
  const bool resources =
      compressed.rollout.metrics.actual_synthetic_transitions <=
          compressed.rollout.metrics.logical_synthetic_transitions &&
      compressed.rollout.metrics.action_memo_misses ==
          compressed.rollout.metrics.continuation.calls &&
      compressed.rollout.metrics.action_memo_entries <=
          kActionMemoCapacity &&
      compressed.rollout.metrics.continuation.peak_cache_entries <=
          original::kWorstD2CacheEntries &&
      compressed.rollout.metrics.continuation.full_root;
  const bool protocol =
      kPilotSeed == 0x3ded'0000u && original::kFittingSeedStart == kPilotSeed &&
      original::kHeldoutSeedStart == 0x3dee'0000u &&
      original::kScreenSeedStart == 0x3ebb'0000u &&
      original::kConfirmationSeedStart == 0x3ebc'0000u;
  const bool passed = dependency && canonical_action_memo &&
                      reference_parity && reflection_parity &&
                      metadata_blind && exact_suffix_reuse &&
                      filtered_exact && resources && protocol;
  output << std::setprecision(12)
         << "D4_D2_EXACT_COMPRESSED_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"dependency\":" << (dependency ? "true" : "false")
         << ",\"canonicalActionMemo\":"
         << (canonical_action_memo ? "true" : "false")
         << ",\"originalOutcomeActionParity\":"
         << (reference_parity ? "true" : "false")
         << ",\"referenceAction\":" << reference.action
         << ",\"compressedAction\":" << compressed.action
         << ",\"referencePassing\":"
         << reference.passing_alternatives
         << ",\"compressedPassing\":"
         << compressed.passing_alternatives
         << ",\"unequalRolloutAction\":" << unequal_rollout_action
         << ",\"unequalScenario\":" << unequal_scenario;
  if (unequal_rollout_action >= 0 && unequal_scenario >= 0) {
    const auto& expected = reference.rollout
                               .actions[unequal_rollout_action]
                               .scenarios[unequal_scenario];
    const auto& actual = compressed.rollout
                             .actions[unequal_rollout_action]
                             .scenarios[unequal_scenario];
    output << std::setprecision(17)
           << ",\"expectedValue\":" << expected.value
           << ",\"actualValue\":" << actual.value
           << ",\"valueDifference\":" << actual.value - expected.value
           << ",\"expectedClears\":" << expected.numbered_clears
           << ",\"actualClears\":" << actual.numbered_clears
           << ",\"expectedSurvived\":"
           << (expected.survived_horizon ? "true" : "false")
           << ",\"actualSurvived\":"
           << (actual.survived_horizon ? "true" : "false");
  }
  output
         << ",\"reflectionParity\":"
         << (reflection_parity ? "true" : "false")
         << ",\"metadataBlind\":"
         << (metadata_blind ? "true" : "false")
         << ",\"exactSuffixReuse\":"
         << (exact_suffix_reuse ? "true" : "false")
         << ",\"rootQPrefilterExact\":"
         << (filtered_exact ? "true" : "false")
         << ",\"resources\":" << (resources ? "true" : "false")
         << ",\"memoHits\":" << memo_metrics.action_memo_hits
         << ",\"reuseSuffixHits\":" << reuse_metrics.suffix_hits
         << ",\"memoCapacity\":" << kActionMemoCapacity << "}\n";
  return passed;
}

void writeArtifact(const Options& options, const GameResult& game,
                   std::size_t trace_records) {
  const double first_pair_seconds =
      std::max(kFrozenBaselineSeconds, game.elapsed_seconds);
  const double projected_seconds =
      first_pair_seconds * original::kFullProtocolProjectionWaves;
  const bool fits =
      projected_seconds <= original::kMaximumProjectedWallSeconds;
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open compressed artifact");
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"d4-d2-rollout-veto-exact-compressed\",\n"
         << "  \"formalInference\":false,\n"
         << "  \"outcomePreserving\":true,\n"
         << "  \"publicStateOnly\":true,\n"
         << "  \"uniqueSeedRead\":\"0x3ded0000\",\n"
         << "  \"uniqueSeedAlreadyOpen\":true,\n"
         << "  \"freshGameplaySeedsOpened\":false,\n"
         << "  \"untouched\":[\"0x3ded0001...003\",\"0x3dee0000...007\",\"0x3ebb...\",\"0x3ebc...\",\"0x7d...\",\"0xd7...\"],\n"
         << "  \"originalArtifact\":{\"path\":\"" << kOriginalArtifact
         << "\",\"sha256\":\"" << kOriginalArtifactSha256
         << "\",\"candidateSeconds\":"
         << kFrozenOriginalCandidateSeconds
         << ",\"syntheticTransitions\":"
         << kFrozenOriginalSyntheticTransitions
         << ",\"d2Calls\":" << kFrozenOriginalD2Calls
         << ",\"d2Work\":" << kFrozenOriginalD2Work << "},\n"
         << "  \"optimizations\":{\"rootQBeforeSimulation\":true,"
            "\"baselineEvaluatedFirstOnce\":true,"
            "\"lazyAdmissibleAlternatives\":true,"
            "\"canonicalPublicD2ActionMemo\":true,"
            "\"actionMemoScope\":\"bounded-decision\","
            "\"actionMemoCapacity\":" << kActionMemoCapacity
         << ",\"exactContinuationReuse\":true,"
            "\"continuationReuseKey\":\"observable-state+scenario+step\"},\n"
         << "  \"frozenOutcomeParity\":{\"score\":" << game.score
         << ",\"moves\":" << game.moves
         << ",\"numberedCleared\":" << game.numbered_cleared
         << ",\"coversRevealed\":" << game.covers_revealed
         << ",\"routedDecisions\":" << game.routed_decisions
         << ",\"switches\":" << game.switches
         << ",\"passingAlternatives\":"
         << game.passing_alternatives
         << ",\"d4Work\":" << game.d4_work
         << ",\"matchedOriginal\":true},\n"
         << "  \"pilot\":{\"elapsedSeconds\":" << game.elapsed_seconds
         << ",\"d4Seconds\":" << game.d4_seconds
         << ",\"rolloutSeconds\":" << game.rollout_seconds
         << ",\"rootQRejections\":" << game.root_q_rejections
         << ",\"traceRecords\":" << trace_records
         << ",\"tracePath\":\"" << options.trace_output
         << "\",\"peakD4CacheEntries\":"
         << game.peak_d4_cache_entries << ",\"peakRssBytes\":"
         << game.peak_rss_bytes << ",\"compression\":";
  writeCompression(output, game.compression);
  output << "},\n  \"runtimeProjection\":{\"frozenBaselineSeconds\":"
         << kFrozenBaselineSeconds
         << ",\"optimizedCandidateSeconds\":" << game.elapsed_seconds
         << ",\"firstPairSeconds\":" << first_pair_seconds
         << ",\"waves\":" << original::kFullProtocolProjectionWaves
         << ",\"projectedFullProtocolSeconds\":" << projected_seconds
         << ",\"limitSeconds\":"
         << original::kMaximumProjectedWallSeconds
         << ",\"fits\":" << (fits ? "true" : "false") << "},\n"
         << "  \"remainingFittingAuthorized\":false,\n"
         << "  \"approximationMenuStatus\":\"proposed-not-run\",\n"
         << "  \"proposedApproximationMenu\":["
         << "{\"name\":\"h8-s7-d2\",\"horizon\":8,\"scenarios\":7,"
            "\"continuationDepth\":2,\"isolates\":\"shorter-horizon\","
            "\"maximumD2CallsPerSevenActionDecision\":343},"
         << "{\"name\":\"h25-s7-d1\",\"horizon\":25,"
            "\"scenarios\":7,\"continuationDepth\":1,"
            "\"isolates\":\"cheaper-continuation\","
            "\"maximumContinuationCallsPerSevenActionDecision\":1176}]\n"
         << "}\n";
}

int replay(const Options& options, std::ostream& report) {
  std::vector<TraceRecord> records;
  const GameResult game = replayPilot(records);
  std::ofstream trace(options.trace_output);
  if (!trace) throw std::runtime_error("could not open compressed trace");
  trace << std::setprecision(12)
        << "{\"type\":\"metadata\","
        << "\"experiment\":\"d4-d2-rollout-veto-exact-compressed\","
        << "\"gameSeed\":" << kPilotSeed
        << ",\"uniqueSeedAlreadyOpen\":true,\"records\":"
        << records.size() << "}\n";
  for (std::size_t index = 0; index < records.size(); ++index) {
    writeTraceRecord(trace, index, records[index]);
  }
  trace << "{\"type\":\"summary\",\"score\":" << game.score
        << ",\"moves\":" << game.moves
        << ",\"routedDecisions\":" << game.routed_decisions
        << ",\"switches\":" << game.switches
        << ",\"elapsedSeconds\":" << game.elapsed_seconds
        << ",\"compression\":";
  writeCompression(trace, game.compression);
  trace << "}\n";
  trace.close();
  writeArtifact(options, game, records.size());

  const double projected =
      std::max(kFrozenBaselineSeconds, game.elapsed_seconds) *
      original::kFullProtocolProjectionWaves;
  report << std::fixed << std::setprecision(6)
         << "D4_D2_EXACT_COMPRESSED_REPLAY {\"score\":" << game.score
         << ",\"moves\":" << game.moves
         << ",\"switches\":" << game.switches
         << ",\"seconds\":" << game.elapsed_seconds
         << ",\"rootActionsEvaluated\":"
         << game.compression.evaluated_root_actions
         << ",\"rootActionsFiltered\":"
         << game.compression.root_q_filtered_actions
         << ",\"actionMemoHits\":"
         << game.compression.action_memo_hits
         << ",\"suffixHits\":" << game.compression.suffix_hits
         << ",\"actualD2Calls\":"
         << game.compression.continuation.calls
         << ",\"projectedProtocolSeconds\":" << projected
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return projected <= original::kMaximumProjectedWallSeconds ? 0 : 3;
}

}  // namespace drop7::d4_d2_rollout_veto_exact_compressed

#ifndef DROP7_D4_D2_ROLLOUT_VETO_EXACT_COMPRESSED_LIBRARY
int main(int argc, char** argv) {
  try {
    using namespace drop7::d4_d2_rollout_veto_exact_compressed;
    const Options options = parseOptions(argc, argv, 2);
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return selfTest(std::cout) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--replay-pilot") {
      return replay(options, std::cout);
    }
    std::cerr << "usage: drop7_d4_d2_rollout_veto_exact_compressed "
                 "--self-test | --replay-pilot [--output PATH] "
                 "[--trace-output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
#endif
