#define DROP7_D2_LONG_OUTCOME_FEATURE_AUDIT_LIBRARY
#include "../../d4-long-outcome/long-outcome/d2-long-outcome-feature-audit.cpp"
#undef DROP7_D2_LONG_OUTCOME_FEATURE_AUDIT_LIBRARY

// The embedded lab assumes a 7,000-point rise bonus.  This alias satisfies its
// compile-time assertion while compiling the search against the shared engine.
namespace drop7 {
constexpr std::int64_t kObservableMctsHistoricalLevelBonus = 7'000;
}
#define kLevelBonus kObservableMctsHistoricalLevelBonus
#define main drop7_observable_mcts_lab_embedded_main
#include "observable-mcts-lab.cpp"
#undef main
#undef kLevelBonus

#include <atomic>
#include <functional>
#include <sstream>

// Performs a ranking-only scale audit for public-state stochastic MCTS.  It
// loads only persisted development root/label files, reconstructs no origin
// game, and cannot read gameplay, screen, validation, or protected seeds.
//
// The candidate is fixed before ranking at 65,536 simulations, horizon 64,
// a 16-outcome per-edge reservoir, and one public fair leaf at a surviving
// horizon.  A fitting-only 2x2 ablation isolates scale from the leaf.  The
// reference 16,384/h32/reservoir-8/zero-tail search uses the same roots.
namespace drop7::observable_mcts_scaled_audit {

namespace data = drop7::d2_long_outcome_feature_audit;
namespace labels = drop7::d2_long_outcome_ranker;
namespace d4 = drop7::scaled_d4_distill;
namespace fair = drop7::fair_only_horizon;
namespace fair4 = drop7::fair_only_depth4;
namespace old = drop7::observable_mcts;
using Clock = std::chrono::steady_clock;

constexpr int kCandidateSimulations = 65'536;
constexpr int kCandidateHorizon = 64;
constexpr int kCandidateReservoir = 16;
constexpr int kOldSimulations = 16'384;
constexpr int kOldHorizon = 32;
constexpr int kOldReservoir = 8;
constexpr int kMaximumReservoir = 16;
constexpr std::size_t kMaximumNodes = 65'537;
constexpr std::size_t kMaximumOutcomes = 65'536;
constexpr std::size_t kHashSlots = 262'144;
constexpr std::size_t kMemoryCapBytes = 128u * 1024u * 1024u;
constexpr int kCorpusRootsPerGame = 12;
constexpr int kSelectedRootOrdinal = 5;  // fixed middle root, zero based
constexpr int kFolds = 6;
constexpr int kDefaultThreads = 4;
constexpr double kValueScale = 100'000.0;
constexpr double kTerminalUtility = -10.0;
constexpr double kTieTolerance = 1.0e-9;

constexpr std::string_view kLongCorpusSha256 =
    "621302a0cd8334fa56e5b77c191beb5529eda0e5413b8e7e20d524c852e7ea7a";
constexpr std::string_view kD4CorpusSha256 =
    "e97f0a00dad76ce0e47bd60d5824e4e921e57b2cb47990b28b5bd4a562dd56bf";

static_assert(kCandidateSimulations == 4 * kOldSimulations);
static_assert(kLevelBonus == 17'000);
static_assert(kCandidateHorizon == 2 * kOldHorizon);
static_assert(kCandidateReservoir == 2 * kOldReservoir);
static_assert(kMaximumNodes ==
              static_cast<std::size_t>(kCandidateSimulations) + 1u);
static_assert((kHashSlots & (kHashSlots - 1u)) == 0u);
static_assert(kSelectedRootOrdinal >= 0 &&
              kSelectedRootOrdinal < kCorpusRootsPerGame);

struct SearchConfig {
  int simulations = 0;
  int horizon = 0;
  int reservoir = 0;
  bool fair_leaf = false;
};

constexpr SearchConfig kOldConfig{
    kOldSimulations, kOldHorizon, kOldReservoir, false};
constexpr SearchConfig kScaleOnlyConfig{
    kCandidateSimulations, kCandidateHorizon, kCandidateReservoir, false};
constexpr SearchConfig kLeafOnlyConfig{
    kOldSimulations, kOldHorizon, kOldReservoir, true};
constexpr SearchConfig kCandidateConfig{
    kCandidateSimulations, kCandidateHorizon, kCandidateReservoir, true};

struct ActionStats {
  double value_sum = 0.0;
  std::uint32_t visits = 0;
  std::array<std::uint32_t, kMaximumReservoir> outcomes{};
  std::uint8_t outcome_count = 0;
  bool legal = false;
};

struct Node {
  State state{};
  std::array<ActionStats, kBoardSize> actions{};
  std::uint32_t visits = 0;
  std::uint8_t remaining = 0;
};

constexpr std::uint32_t kTerminalChild =
    std::numeric_limits<std::uint32_t>::max();

struct Outcome {
  std::uint32_t child = kTerminalChild;
  double reward = 0.0;
};

struct HashSlot {
  std::uint64_t hash = 0;
  std::uint32_t node_plus_one = 0;
};

constexpr std::size_t arenaReservedBytes() {
  return kMaximumNodes * sizeof(Node) +
         kMaximumOutcomes * sizeof(Outcome) +
         kHashSlots * sizeof(HashSlot);
}

static_assert(arenaReservedBytes() <= kMemoryCapBytes,
              "scaled observable MCTS arena exceeds 128 MiB");

class Arena {
 public:
  Arena() {
    nodes_.reserve(kMaximumNodes);
    outcomes_.reserve(kMaximumOutcomes);
    table_.resize(kHashSlots);
  }

  std::pair<std::uint32_t, bool> findOrInsert(const State& source,
                                              int remaining) {
    bool ignored = false;
    const State state = old::canonicalPublicState(source, ignored);
    const std::uint64_t hash = old::observableHash(state, remaining);
    std::size_t slot = static_cast<std::size_t>(hash) & (kHashSlots - 1u);
    for (std::size_t probe = 0; probe < kHashSlots; ++probe) {
      HashSlot& entry = table_[slot];
      if (entry.node_plus_one == 0) {
        if (nodes_.size() >= kMaximumNodes) return {kTerminalChild, false};
        Node node;
        node.state = state;
        node.remaining = static_cast<std::uint8_t>(remaining);
        for (int action = 0; action < kBoardSize; ++action) {
          node.actions[action].legal = isLegal(state.board, action);
        }
        const std::uint32_t index =
            static_cast<std::uint32_t>(nodes_.size());
        nodes_.push_back(std::move(node));
        entry.hash = hash;
        entry.node_plus_one = index + 1u;
        return {index, true};
      }
      const std::uint32_t index = entry.node_plus_one - 1u;
      const Node& candidate = nodes_[index];
      if (entry.hash == hash && candidate.remaining == remaining &&
          old::samePublicState(candidate.state, state)) {
        return {index, false};
      }
      slot = (slot + 1u) & (kHashSlots - 1u);
    }
    throw std::runtime_error("scaled observable MCTS hash arena is full");
  }

  std::uint32_t addOutcome(const Outcome& outcome) {
    if (outcomes_.size() >= kMaximumOutcomes) return kTerminalChild;
    const std::uint32_t index =
        static_cast<std::uint32_t>(outcomes_.size());
    outcomes_.push_back(outcome);
    return index;
  }

  Node& node(std::uint32_t index) { return nodes_[index]; }
  const Node& node(std::uint32_t index) const { return nodes_[index]; }
  const Outcome& outcome(std::uint32_t index) const {
    return outcomes_[index];
  }
  std::size_t nodeCount() const { return nodes_.size(); }
  std::size_t outcomeCount() const { return outcomes_.size(); }
  std::size_t activeBytes() const {
    return nodes_.size() * sizeof(Node) +
           outcomes_.size() * sizeof(Outcome) +
           kHashSlots * sizeof(HashSlot);
  }
  bool reservoirsBounded(int limit) const {
    for (const Node& node : nodes_) {
      for (const ActionStats& action : node.actions) {
        if (action.outcome_count > limit) return false;
      }
    }
    return true;
  }

 private:
  std::vector<Node> nodes_;
  std::vector<Outcome> outcomes_;
  std::vector<HashSlot> table_;
};

int progressiveWidth(std::uint32_t visits, int reservoir) {
  const int width = 1 + static_cast<int>(std::floor(std::sqrt(visits)));
  return std::min(reservoir, width);
}

int selectUctAction(const Node& node) {
  for (const int action : cfpi::detail::kColumnOrder) {
    if (node.actions[action].legal && node.actions[action].visits == 0) {
      return action;
    }
  }
  int selected = -1;
  double best = -std::numeric_limits<double>::infinity();
  const double log_parent =
      std::log(static_cast<double>(std::max<std::uint32_t>(1, node.visits)));
  for (const int action : cfpi::detail::kColumnOrder) {
    const ActionStats& edge = node.actions[action];
    if (!edge.legal || edge.visits == 0) continue;
    const double mean = edge.value_sum / edge.visits;
    const double bonus = old::kUctExploration *
                         std::sqrt(log_parent / edge.visits);
    const double score = mean + bonus;
    if (score > best) {
      best = score;
      selected = action;
    }
  }
  return selected;
}

double horizonTail(const State& state, bool fair_leaf) {
  if (state.game_over) return kTerminalUtility;
  return fair_leaf ? fair::fairLeaf(old::publicState(state)) / kValueScale
                   : 0.0;
}

struct PathStep {
  std::uint32_t node = 0;
  int action = -1;
  double reward = 0.0;
};

struct Snapshot {
  int simulations = 0;
  int horizon = 0;
  int reservoir = 0;
  bool fair_leaf = false;
  int action = -1;
  std::array<double, kBoardSize> q{};
  std::array<std::uint32_t, kBoardSize> visits{};
  bool complete = false;
  std::size_t nodes = 0;
  std::size_t outcomes = 0;
  std::size_t active_bytes = 0;
  std::uint64_t tree_steps = 0;
  std::uint64_t rollout_steps = 0;
  std::uint64_t leaf_evaluations = 0;
  std::uint64_t transposition_hits = 0;
  std::uint64_t arena_full = 0;
  double seconds = 0.0;
};

class Search {
 public:
  Search(const State& source, SearchConfig config)
      : config_(config), started_(Clock::now()) {
    if (source.game_over || config.simulations < 1 ||
        config.simulations > kCandidateSimulations || config.horizon < 1 ||
        config.horizon > kCandidateHorizon || config.reservoir < 1 ||
        config.reservoir > kMaximumReservoir) {
      throw std::invalid_argument("invalid scaled observable MCTS root/config");
    }
    const State canonical = old::canonicalPublicState(source, mirrored_);
    root_public_hash_ = old::observableHash(canonical);
    const auto [root, inserted] =
        arena_.findOrInsert(canonical, config.horizon);
    if (!inserted || root != 0) {
      throw std::logic_error("scaled observable MCTS root insertion failed");
    }
  }

  void runTo(int target_simulations) {
    if (target_simulations < completed_simulations_ ||
        target_simulations > config_.simulations) {
      throw std::invalid_argument("invalid scaled MCTS simulation target");
    }
    while (completed_simulations_ < target_simulations && !incomplete_) {
      simulate(completed_simulations_);
      ++completed_simulations_;
    }
  }

  Snapshot snapshot() const {
    Snapshot result;
    result.simulations = completed_simulations_;
    result.horizon = config_.horizon;
    result.reservoir = config_.reservoir;
    result.fair_leaf = config_.fair_leaf;
    result.q.fill(-std::numeric_limits<double>::infinity());
    const Node& root = arena_.node(0);
    int canonical_action = -1;
    double best = -std::numeric_limits<double>::infinity();
    for (const int action : cfpi::detail::kColumnOrder) {
      const ActionStats& edge = root.actions[action];
      if (!edge.legal || edge.visits == 0) continue;
      const int physical = mirrored_ ? kBoardSize - 1 - action : action;
      result.q[physical] = edge.value_sum / edge.visits;
      result.visits[physical] = edge.visits;
      if (result.q[physical] > best) {
        best = result.q[physical];
        canonical_action = action;
      }
    }
    result.action = canonical_action < 0
                        ? -1
                        : (mirrored_ ? kBoardSize - 1 - canonical_action
                                     : canonical_action);
    result.complete = !incomplete_ && completed_simulations_ > 0 &&
                      result.action >= 0;
    result.nodes = arena_.nodeCount();
    result.outcomes = arena_.outcomeCount();
    result.active_bytes = arena_.activeBytes();
    result.tree_steps = tree_steps_;
    result.rollout_steps = rollout_steps_;
    result.leaf_evaluations = leaf_evaluations_;
    result.transposition_hits = transposition_hits_;
    result.arena_full = arena_full_;
    result.seconds =
        std::chrono::duration<double>(Clock::now() - started_).count();
    return result;
  }

  bool reservoirsBounded() const {
    return arena_.reservoirsBounded(config_.reservoir);
  }

 private:
  double leaf(const State& state) {
    if (!config_.fair_leaf) return 0.0;
    ++leaf_evaluations_;
    return horizonTail(state, true);
  }

  double rollout(State state, int remaining, int simulation,
                 int starting_depth) {
    const std::uint32_t random_seed = old::seed32(
        root_public_hash_ ^ old::kRolloutDomain ^
        (static_cast<std::uint64_t>(simulation + 1) *
         old::kVisitMultiplier) ^
        (static_cast<std::uint64_t>(starting_depth + 1) *
         old::kOrdinalMultiplier));
    Mulberry32 random(random_seed);
    double value = 0.0;
    for (int step = 0; step < remaining && !state.game_over; ++step) {
      // Hold this public-D1 continuation constant so the ablation isolates
      // simulation scale and the calibrated horizon tail.
      const int action = old::phaseDepthOneAction(state);
      if (!isLegal(state.board, action)) return value + kTerminalUtility;
      MoveResult move;
      if (!playMove(old::publicState(state), action, random, move)) {
        return value + kTerminalUtility;
      }
      ++rollout_steps_;
      value += static_cast<double>(move.score_delta) / kValueScale;
      state = old::publicState(move.state);
      if (state.game_over) return value + kTerminalUtility;
    }
    return value + leaf(state);
  }

  void simulate(int simulation) {
    std::array<PathStep, kCandidateHorizon> path{};
    int path_size = 0;
    std::uint32_t node_index = 0;
    double tail = 0.0;
    for (int depth = 0; depth < config_.horizon; ++depth) {
      const Node& selection_node = arena_.node(node_index);
      const int action = selectUctAction(selection_node);
      if (action < 0) {
        tail = kTerminalUtility;
        break;
      }
      const ActionStats& selection_edge = selection_node.actions[action];
      const int allowed =
          progressiveWidth(selection_edge.visits, config_.reservoir);
      if (selection_edge.outcome_count < allowed) {
        const int ordinal = selection_edge.outcome_count;
        const old::Transition transition = old::samplePublicTransition(
            selection_node.state, action, ordinal);
        Outcome outcome;
        outcome.reward = transition.reward;
        bool inserted = false;
        if (!transition.terminal) {
          const auto child = arena_.findOrInsert(
              transition.state, selection_node.remaining - 1);
          outcome.child = child.first;
          inserted = child.second;
          if (outcome.child == kTerminalChild) {
            ++arena_full_;
            incomplete_ = true;
            return;
          }
          if (!inserted) ++transposition_hits_;
        }
        const std::uint32_t outcome_index = arena_.addOutcome(outcome);
        if (outcome_index == kTerminalChild) {
          ++arena_full_;
          incomplete_ = true;
          return;
        }
        Node& node = arena_.node(node_index);
        ActionStats& edge = node.actions[action];
        edge.outcomes[edge.outcome_count++] = outcome_index;
        path[path_size++] = {node_index, action, outcome.reward};
        ++tree_steps_;
        if (transition.terminal) {
          tail = kTerminalUtility;
        } else {
          tail = rollout(transition.state, node.remaining - 1,
                         simulation, depth + 1);
        }
        break;
      }

      const std::uint32_t choice_bits = mix32(
          old::seed32(old::observableHash(selection_node.state)) ^
          (static_cast<std::uint32_t>(action + 1) *
           old::kActionMultiplier) ^
          (static_cast<std::uint32_t>(selection_edge.visits + 1) *
           old::kVisitMultiplier));
      const int reservoir_index = static_cast<int>(
          choice_bits % selection_edge.outcome_count);
      const Outcome outcome =
          arena_.outcome(selection_edge.outcomes[reservoir_index]);
      path[path_size++] = {node_index, action, outcome.reward};
      ++tree_steps_;
      if (outcome.child == kTerminalChild) {
        tail = kTerminalUtility;
        break;
      }
      node_index = outcome.child;
      if (arena_.node(node_index).remaining == 0) {
        tail = leaf(arena_.node(node_index).state);
        break;
      }
    }

    double value = tail;
    for (int index = path_size - 1; index >= 0; --index) {
      value += path[index].reward;
      Node& node = arena_.node(path[index].node);
      ActionStats& edge = node.actions[path[index].action];
      ++edge.visits;
      edge.value_sum += value;
      ++node.visits;
    }
  }

  SearchConfig config_{};
  bool mirrored_ = false;
  std::uint64_t root_public_hash_ = 0;
  Arena arena_;
  Clock::time_point started_;
  int completed_simulations_ = 0;
  bool incomplete_ = false;
  std::uint64_t tree_steps_ = 0;
  std::uint64_t rollout_steps_ = 0;
  std::uint64_t leaf_evaluations_ = 0;
  std::uint64_t transposition_hits_ = 0;
  std::uint64_t arena_full_ = 0;
};

struct RootAudit {
  data::StoredRoot stored{};
  d4::RootLabel current_long{};
  d4::RootLabel current_d4{};
  std::uint64_t long_transitions = 0;
  std::uint64_t long_d2_calls = 0;
  std::uint64_t long_d2_work = 0;
  double long_seconds = 0.0;
  fair::SearchDecision fair_d3{};
  fair4::SearchDecision fair_d4{};
  Snapshot old{};
  Snapshot scale_only{};
  Snapshot leaf_only{};
  Snapshot candidate{};
  bool has_ablation = false;
};

std::vector<data::StoredRoot> selectRoots(
    const std::vector<data::StoredRoot>& source, int expected_games) {
  std::vector<data::StoredRoot> result;
  result.reserve(static_cast<std::size_t>(expected_games));
  std::vector<int> counts(static_cast<std::size_t>(expected_games), 0);
  for (const data::StoredRoot& root : source) {
    const int game = root.label.game;
    if (game < 0 || game >= expected_games) {
      throw std::runtime_error("persisted root game index changed");
    }
    const int ordinal = counts[static_cast<std::size_t>(game)]++;
    if (ordinal == kSelectedRootOrdinal) result.push_back(root);
  }
  for (const int count : counts) {
    if (count != kCorpusRootsPerGame) {
      throw std::runtime_error("persisted roots-per-game changed");
    }
  }
  if (static_cast<int>(result.size()) != expected_games) {
    throw std::runtime_error("selected root count changed");
  }
  return result;
}

Snapshot runSearch(const State& state, SearchConfig config) {
  Search search(state, config);
  search.runTo(config.simulations);
  const Snapshot snapshot = search.snapshot();
  if (!snapshot.complete || snapshot.simulations != config.simulations ||
      snapshot.arena_full != 0 || !search.reservoirsBounded() ||
      snapshot.active_bytes > arenaReservedBytes() ||
      !isLegal(state.board, snapshot.action)) {
    throw std::runtime_error("scaled observable MCTS search was incomplete");
  }
  return snapshot;
}

RootAudit auditRoot(data::StoredRoot stored, bool fitting) {
  RootAudit result;
  result.stored = std::move(stored);
  const State state = d4::publicState(result.stored.label);
  // The persisted roots remain valid public observations, but their numeric
  // Q labels used the superseded 7,000-point rise bonus.  Replay only their
  // already-defined public synthetic tapes under the corrected 17,000-point
  // engine; this neither reconstructs nor reads any gameplay seed.
  const labels::OutcomeLabel current_long =
      labels::evaluateRoot(result.stored.label);
  result.current_long = current_long.label;
  result.long_transitions = current_long.transitions;
  result.long_d2_calls = current_long.d2.calls;
  result.long_d2_work = current_long.d2.work;
  result.long_seconds = current_long.wall_seconds;
  if (result.current_long.board != result.stored.label.board ||
      result.current_long.next_disc != result.stored.label.next_disc ||
      result.current_long.moves_remaining !=
          result.stored.label.moves_remaining ||
      result.current_long.legal != result.stored.label.legal) {
    throw std::runtime_error("17k long-label replay changed public root");
  }
  result.fair_d3 = fair::chooseFairAction(state);
  if (!result.fair_d3.complete || result.fair_d3.completed_depth != 3 ||
      !isLegal(state.board, result.fair_d3.action)) {
    throw std::runtime_error("fair D3 comparison was incomplete");
  }
  result.fair_d4 = fair4::chooseDepth4Action(state);
  if (!result.fair_d4.complete || result.fair_d4.completed_depth != 4 ||
      !isLegal(state.board, result.fair_d4.action)) {
    throw std::runtime_error("fair D4 comparison was incomplete");
  }
  result.current_d4 = result.stored.label;
  result.current_d4.q = result.fair_d4.root_values;
  result.current_d4.labeled_action = result.fair_d4.action;
  result.old = runSearch(state, kOldConfig);
  if (fitting) {
    result.scale_only = runSearch(state, kScaleOnlyConfig);
    result.leaf_only = runSearch(state, kLeafOnlyConfig);
    result.has_ablation = true;
  }
  result.candidate = runSearch(state, kCandidateConfig);
  return result;
}

std::vector<RootAudit> parallelAudit(std::vector<data::StoredRoot> roots,
                                     bool fitting, int threads,
                                     std::string_view split) {
  std::vector<RootAudit> result(roots.size());
  std::atomic<std::size_t> next{0};
  std::atomic<bool> failed{false};
  std::mutex error_mutex;
  std::mutex progress_mutex;
  std::string error_message;
  std::vector<std::thread> workers;
  const int worker_count =
      std::min<int>(threads, static_cast<int>(roots.size()));
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.emplace_back([&] {
      while (!failed.load(std::memory_order_relaxed)) {
        const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
        if (index >= roots.size()) return;
        try {
          result[index] = auditRoot(std::move(roots[index]), fitting);
          const std::lock_guard<std::mutex> lock(progress_mutex);
          std::cerr << "scaled-mcts " << split << ' ' << index + 1 << '/'
                    << roots.size() << " game "
                    << result[index].stored.label.game << " move "
                    << result[index].stored.label.move_in_game << '\n';
        } catch (const std::exception& error) {
          failed.store(true, std::memory_order_relaxed);
          const std::lock_guard<std::mutex> lock(error_mutex);
          if (error_message.empty()) error_message = error.what();
        }
      }
    });
  }
  for (std::thread& worker : workers) worker.join();
  if (failed.load()) {
    throw std::runtime_error("scaled MCTS audit failed: " + error_message);
  }
  return result;
}

enum class Target { LongOutcome, FairD4 };
enum class Predictor { Old, ScaleOnly, LeafOnly, Candidate, FairD3, FairD4 };

const d4::RootLabel& targetLabel(const RootAudit& root, Target target) {
  return target == Target::LongOutcome ? root.current_long : root.current_d4;
}

std::array<double, kBoardSize> prediction(const RootAudit& root,
                                          Predictor predictor) {
  switch (predictor) {
    case Predictor::Old:
      return root.old.q;
    case Predictor::ScaleOnly:
      return root.scale_only.q;
    case Predictor::LeafOnly:
      return root.leaf_only.q;
    case Predictor::Candidate:
      return root.candidate.q;
    case Predictor::FairD3:
      return root.fair_d3.root_values;
    case Predictor::FairD4:
      return root.current_d4.q;
  }
  throw std::logic_error("unknown ranking predictor");
}

bool tied(double left, double right) {
  return std::abs(left - right) <=
         kTieTolerance *
             (1.0 + std::max(std::abs(left), std::abs(right)));
}

int bestAction(const d4::RootLabel& label,
               const std::array<double, kBoardSize>& values) {
  int selected = -1;
  double best = -std::numeric_limits<double>::infinity();
  for (const int action : cfpi::detail::kColumnOrder) {
    if (!label.legal[action]) continue;
    if (selected < 0 || values[action] > best) {
      selected = action;
      best = values[action];
    }
  }
  return selected;
}

struct Metrics {
  int roots = 0;
  int top_one = 0;
  std::uint64_t pairs = 0;
  double pairwise_credit = 0.0;
  double raw_regret_sum = 0.0;
  double normalized_regret_sum = 0.0;

  double topOneRate() const {
    return roots > 0 ? static_cast<double>(top_one) / roots : 0.0;
  }
  double pairwiseRate() const {
    return pairs > 0 ? pairwise_credit / static_cast<double>(pairs) : 0.0;
  }
  double rawRegret() const {
    return roots > 0 ? raw_regret_sum / roots : 0.0;
  }
  double normalizedRegret() const {
    return roots > 0 ? normalized_regret_sum / roots : 0.0;
  }
};

Metrics ranking(const std::vector<RootAudit>& roots, Target target,
                Predictor predictor,
                const std::function<bool(const RootAudit&)>& include) {
  Metrics result;
  for (const RootAudit& root : roots) {
    if (!include(root)) continue;
    if ((predictor == Predictor::ScaleOnly ||
         predictor == Predictor::LeafOnly) &&
        !root.has_ablation) {
      throw std::logic_error("ablation requested outside fitting roots");
    }
    const d4::RootLabel& truth = targetLabel(root, target);
    const auto scores = prediction(root, predictor);
    const int selected = bestAction(truth, scores);
    if (selected < 0) throw std::logic_error("empty ranking prediction");
    double maximum = -std::numeric_limits<double>::infinity();
    double minimum = std::numeric_limits<double>::infinity();
    for (int action = 0; action < kBoardSize; ++action) {
      if (!truth.legal[action]) continue;
      maximum = std::max(maximum, truth.q[action]);
      minimum = std::min(minimum, truth.q[action]);
    }
    ++result.roots;
    result.top_one += tied(truth.q[selected], maximum);
    result.raw_regret_sum += maximum - truth.q[selected];
    result.normalized_regret_sum +=
        (maximum - truth.q[selected]) / std::max(1.0e-9, maximum - minimum);
    for (int left = 0; left < kBoardSize; ++left) {
      if (!truth.legal[left]) continue;
      for (int right = left + 1; right < kBoardSize; ++right) {
        if (!truth.legal[right] || tied(truth.q[left], truth.q[right])) {
          continue;
        }
        const double delta = scores[left] - scores[right];
        if (std::abs(delta) <= kTieTolerance) {
          result.pairwise_credit += 0.5;
        } else {
          result.pairwise_credit +=
              ((delta > 0.0) == (truth.q[left] > truth.q[right])) ? 1.0
                                                                  : 0.0;
        }
        ++result.pairs;
      }
    }
  }
  if (result.roots == 0 || result.pairs == 0) {
    throw std::logic_error("empty scaled MCTS metric range");
  }
  return result;
}

Metrics rankingAll(const std::vector<RootAudit>& roots, Target target,
                   Predictor predictor) {
  return ranking(roots, target, predictor,
                 [](const RootAudit&) { return true; });
}

struct ResourceMetrics {
  std::size_t maximum_nodes = 0;
  std::size_t maximum_outcomes = 0;
  std::size_t maximum_active_bytes = 0;
  std::uint64_t tree_steps = 0;
  std::uint64_t rollout_steps = 0;
  std::uint64_t leaf_evaluations = 0;
  std::uint64_t transposition_hits = 0;
  std::uint64_t arena_full = 0;
  double aggregate_seconds = 0.0;
};

ResourceMetrics resources(const std::vector<RootAudit>& roots,
                          Predictor predictor) {
  ResourceMetrics result;
  for (const RootAudit& root : roots) {
    const Snapshot* snapshot = nullptr;
    if (predictor == Predictor::Old) snapshot = &root.old;
    else if (predictor == Predictor::ScaleOnly) snapshot = &root.scale_only;
    else if (predictor == Predictor::LeafOnly) snapshot = &root.leaf_only;
    else if (predictor == Predictor::Candidate) snapshot = &root.candidate;
    else throw std::logic_error("non-MCTS resource request");
    result.maximum_nodes = std::max(result.maximum_nodes, snapshot->nodes);
    result.maximum_outcomes =
        std::max(result.maximum_outcomes, snapshot->outcomes);
    result.maximum_active_bytes =
        std::max(result.maximum_active_bytes, snapshot->active_bytes);
    result.tree_steps += snapshot->tree_steps;
    result.rollout_steps += snapshot->rollout_steps;
    result.leaf_evaluations += snapshot->leaf_evaluations;
    result.transposition_hits += snapshot->transposition_hits;
    result.arena_full += snapshot->arena_full;
    result.aggregate_seconds += snapshot->seconds;
  }
  return result;
}

void writeArray(std::ostream& output,
                const std::array<double, kBoardSize>& values) {
  output << '[';
  for (int action = 0; action < kBoardSize; ++action) {
    if (action > 0) output << ',';
    if (std::isfinite(values[action])) output << values[action];
    else output << "null";
  }
  output << ']';
}

void writeMetrics(std::ostream& output, const Metrics& value) {
  output << "{\"roots\":" << value.roots
         << ",\"top1\":" << value.topOneRate()
         << ",\"pairwise\":" << value.pairwiseRate()
         << ",\"rawMeanRegret\":" << value.rawRegret()
         << ",\"normalizedMeanRegret\":" << value.normalizedRegret()
         << ",\"pairs\":" << value.pairs << '}';
}

void writeResources(std::ostream& output, const ResourceMetrics& value) {
  output << "{\"maximumNodes\":" << value.maximum_nodes
         << ",\"maximumOutcomes\":" << value.maximum_outcomes
         << ",\"maximumActiveBytes\":" << value.maximum_active_bytes
         << ",\"arenaReservedBytes\":" << arenaReservedBytes()
         << ",\"treeSteps\":" << value.tree_steps
         << ",\"rolloutSteps\":" << value.rollout_steps
         << ",\"leafEvaluations\":" << value.leaf_evaluations
         << ",\"transpositionHits\":" << value.transposition_hits
         << ",\"arenaFull\":" << value.arena_full
         << ",\"aggregateSeconds\":" << value.aggregate_seconds << '}';
}

std::uint64_t peakRssBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
#endif
}

bool selfTest(std::ostream& output) {
  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;

  const SearchConfig parity_config{256, 8, kOldReservoir, false};
  Search search(state, parity_config);
  search.runTo(256);
  const Snapshot first = search.snapshot();
  Search repeat_search(state, parity_config);
  repeat_search.runTo(256);
  const Snapshot repeat = repeat_search.snapshot();
  old::MctsSearch embedded_old(state, 8);
  embedded_old.runTo(256);
  const old::MctsSnapshot reference = embedded_old.snapshot();

  State metadata = state;
  metadata.score = 999'999;
  metadata.level = 77;
  metadata.moves_played = 321;
  Search metadata_search(metadata, parity_config);
  metadata_search.runTo(256);
  const Snapshot metadata_snapshot = metadata_search.snapshot();

  State reflected_state = state;
  reflected_state.board = cfpi::detail::mirrorBoard(state.board);
  Search reflected_search(reflected_state, parity_config);
  reflected_search.runTo(256);
  const Snapshot reflected = reflected_search.snapshot();

  const SearchConfig candidate_probe{kCandidateSimulations,
                                      kCandidateHorizon,
                                      kCandidateReservoir, true};
  Search candidate_search(state, candidate_probe);
  candidate_search.runTo(256);
  const Snapshot candidate = candidate_search.snapshot();

  const bool deterministic =
      first.action == repeat.action && first.q == repeat.q &&
      first.visits == repeat.visits && first.nodes == repeat.nodes &&
      first.outcomes == repeat.outcomes;
  const bool old_parity =
      first.action == reference.action && first.q == reference.q &&
      first.visits == reference.visits && first.nodes == reference.nodes &&
      first.outcomes == reference.outcomes &&
      first.tree_steps == reference.tree_steps &&
      first.rollout_steps == reference.rollout_steps;
  const bool public_boundary =
      first.action == metadata_snapshot.action &&
      first.q == metadata_snapshot.q &&
      horizonTail(state, true) == horizonTail(metadata, true);
  const bool reflection_safe =
      reflected.action == kBoardSize - 1 - first.action;
  const bool resource_safe = arenaReservedBytes() <= kMemoryCapBytes &&
                             candidate.active_bytes <= arenaReservedBytes() &&
                             candidate.nodes <= kMaximumNodes &&
                             candidate.outcomes <= kMaximumOutcomes &&
                             candidate_search.reservoirsBounded() &&
                             candidate.arena_full == 0;
  const bool leaf_safe =
      std::isfinite(horizonTail(state, true)) &&
      horizonTail(state, false) == 0.0 && candidate.leaf_evaluations > 0;
  const bool legal = isLegal(state.board, first.action) &&
                     isLegal(state.board, candidate.action);
  const bool passed = deterministic && old_parity && public_boundary &&
                      reflection_safe && resource_safe && leaf_safe && legal;
  output << std::setprecision(10)
         << "{\"passed\":" << (passed ? "true" : "false")
         << ",\"deterministic\":" << (deterministic ? "true" : "false")
         << ",\"oldParity\":" << (old_parity ? "true" : "false")
         << ",\"publicBoundary\":"
         << (public_boundary ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe ? "true" : "false")
         << ",\"resourceSafe\":"
         << (resource_safe ? "true" : "false")
         << ",\"leafSafe\":" << (leaf_safe ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false")
         << ",\"arenaReservedBytes\":" << arenaReservedBytes()
         << ",\"nodeBytes\":" << sizeof(Node)
         << ",\"candidateProbeNodes\":" << candidate.nodes
         << ",\"candidateProbeOutcomes\":" << candidate.outcomes << "}\n";
  return passed;
}

struct Options {
  int threads = kDefaultThreads;
  std::string labels = "/tmp/drop7-d2-long-outcome-labels.jsonl";
  std::string d4_source = "/tmp/drop7-scaled-d4-distill-labels.jsonl";
  std::string output = "/tmp/drop7-observable-mcts-scaled-audit.json";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string argument = argv[index];
    if (argument == "--threads") result.threads = std::stoi(argv[index + 1]);
    else if (argument == "--labels") result.labels = argv[index + 1];
    else if (argument == "--d4-source") result.d4_source = argv[index + 1];
    else if (argument == "--output") result.output = argv[index + 1];
    else throw std::invalid_argument("unknown scaled MCTS option " + argument);
  }
  if (result.threads < 1 || result.threads > 8) {
    throw std::invalid_argument("scaled MCTS threads must be from 1 to 8");
  }
  return result;
}

void writePredictorSet(std::ostream& output,
                       const std::vector<RootAudit>& roots, Target target,
                       bool include_ablation) {
  output << "{\"old16384h32\":";
  writeMetrics(output, rankingAll(roots, target, Predictor::Old));
  if (include_ablation) {
    output << ",\"scaleOnly65536h64r16\":";
    writeMetrics(output, rankingAll(roots, target, Predictor::ScaleOnly));
    output << ",\"leafOnly16384h32r8\":";
    writeMetrics(output, rankingAll(roots, target, Predictor::LeafOnly));
  }
  output << ",\"candidate65536h64r16FairLeaf\":";
  writeMetrics(output, rankingAll(roots, target, Predictor::Candidate));
  output << ",\"fairD3\":";
  writeMetrics(output, rankingAll(roots, target, Predictor::FairD3));
  output << ",\"fairD4\":";
  writeMetrics(output, rankingAll(roots, target, Predictor::FairD4));
  output << '}';
}

void writeFolds(std::ostream& output, const std::vector<RootAudit>& roots,
                Target target) {
  output << '[';
  for (int fold = 0; fold < kFolds; ++fold) {
    if (fold > 0) output << ',';
    const auto include = [fold](const RootAudit& root) {
      return root.stored.label.game % kFolds == fold;
    };
    output << "{\"fold\":" << fold << ",\"old\":";
    writeMetrics(output, ranking(roots, target, Predictor::Old, include));
    output << ",\"candidate\":";
    writeMetrics(output,
                 ranking(roots, target, Predictor::Candidate, include));
    output << ",\"fairD3\":";
    writeMetrics(output, ranking(roots, target, Predictor::FairD3, include));
    output << ",\"fairD4\":";
    writeMetrics(output, ranking(roots, target, Predictor::FairD4, include));
    output << '}';
  }
  output << ']';
}

bool proposalGate(const std::vector<RootAudit>& heldout) {
  const auto all = [](const RootAudit&) { return true; };
  const Metrics old_all =
      ranking(heldout, Target::LongOutcome, Predictor::Old, all);
  const Metrics candidate_all =
      ranking(heldout, Target::LongOutcome, Predictor::Candidate, all);
  if (candidate_all.topOneRate() < old_all.topOneRate() ||
      candidate_all.pairwiseRate() <= old_all.pairwiseRate() ||
      candidate_all.normalizedRegret() >= old_all.normalizedRegret()) {
    return false;
  }
  for (int half = 0; half < 2; ++half) {
    const auto include = [half](const RootAudit& root) {
      return root.stored.label.game / 6 == half;
    };
    const Metrics old_half =
        ranking(heldout, Target::LongOutcome, Predictor::Old, include);
    const Metrics candidate_half =
        ranking(heldout, Target::LongOutcome, Predictor::Candidate, include);
    if (candidate_half.topOneRate() < old_half.topOneRate() ||
        candidate_half.pairwiseRate() < old_half.pairwiseRate() ||
        candidate_half.normalizedRegret() > old_half.normalizedRegret()) {
      return false;
    }
  }
  return true;
}

int runAudit(const Options& options) {
  const auto started = Clock::now();
  data::StoredCorpus corpus = data::loadCorpus(options.labels);
  data::joinD4(corpus, options.d4_source);
  std::vector<data::StoredRoot> fitting =
      selectRoots(corpus.fitting, d4::kTrainingGames);
  std::vector<data::StoredRoot> heldout =
      selectRoots(corpus.heldout, d4::kHeldoutGames);

  std::vector<RootAudit> fitting_audit = parallelAudit(
      std::move(fitting), true, options.threads, "fitting");
  std::vector<RootAudit> heldout_audit = parallelAudit(
      std::move(heldout), false, options.threads, "heldout");
  const bool propose_fresh_gameplay = proposalGate(heldout_audit);

  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not write scaled MCTS artifact");
  output << std::setprecision(10)
         << "{\n  \"format\":\"drop7-observable-mcts-scaled-audit-v1\",\n"
         << "  \"mechanics\":{\"levelBonus\":" << kLevelBonus
         << ",\"historicalCorpusLevelBonus\":7000,"
            "\"historicalLabelsUsedForRanking\":false},\n"
         << "  \"scope\":{\"rankingOnly\":true,\"gameplaySeedsOpened\":0,"
            "\"newRootSeedsOpened\":0,\"forbidden3eOpened\":false,"
            "\"forbidden7dOpened\":false,\"forbiddenD7Opened\":false,"
            "\"unused3dOpened\":false},\n"
         << "  \"source\":{\"implementation\":"
            "\"approaches/tree-search/observable-mcts/observable-mcts-lab.cpp\","
            "\"longOutcomeCorpusSha256\":\"" << kLongCorpusSha256
         << "\",\"d4CorpusSha256\":\"" << kD4CorpusSha256
         << "\",\"corpusRole\":"
            "\"public roots, grouping, and frozen synthetic tape identity only\"},\n"
         << "  \"weaknessAudit\":{\"oldChanceModel\":"
            "\"at most 8 deterministic samples per edge, then replay only\","
            "\"oldRollout\":\"public phase-D1; retained to isolate architecture\","
            "\"oldHorizonReturn\":\"zero for every surviving cutoff\","
            "\"candidateChanceModel\":"
            "\"at most 16 deterministic samples per edge, then replay only\","
            "\"candidateHorizonReturn\":"
            "\"one public fair leaf divided by 100000\"},\n"
         << "  \"frozenCandidate\":{\"simulations\":"
         << kCandidateSimulations << ",\"horizon\":" << kCandidateHorizon
         << ",\"successorReservoir\":" << kCandidateReservoir
         << ",\"rollout\":\"public-phase-D1\",\"fairLeaf\":true,"
            "\"arenaReservedBytes\":" << arenaReservedBytes()
         << ",\"arenaCapBytes\":" << kMemoryCapBytes << "},\n"
         << "  \"rootSelection\":{\"sourceRootsPerGame\":"
         << kCorpusRootsPerGame << ",\"selectedZeroBasedOrdinal\":"
         << kSelectedRootOrdinal << ",\"rootsPerGame\":1,"
            "\"fittingGames\":" << fitting_audit.size()
         << ",\"heldoutGames\":" << heldout_audit.size()
         << ",\"wholeGameGrouped\":true},\n"
         << "  \"labelRegeneration\":{\"levelBonus\":17000,"
            "\"rootSource\":\"persisted public states only\","
            "\"longTarget\":"
            "\"same already-defined public synthetic tapes replayed under corrected mechanics\","
            "\"fairD4Target\":\"fresh exact public D4 on the same persisted roots\","
            "\"gameplaySeedInput\":false},\n"
         << "  \"primaryTarget\":"
            "\"17k replay of 25-move, seven-scenario, closed-loop public-D2 continuation Q\",\n"
         << "  \"fittingLongOutcome\":";
  writePredictorSet(output, fitting_audit, Target::LongOutcome, true);
  output << ",\n  \"fittingFairD4Labels\":";
  writePredictorSet(output, fitting_audit, Target::FairD4, true);
  output << ",\n  \"fittingWholeGameFoldsLongOutcome\":";
  writeFolds(output, fitting_audit, Target::LongOutcome);
  output << ",\n  \"fittingWholeGameFoldsFairD4\":";
  writeFolds(output, fitting_audit, Target::FairD4);
  output << ",\n  \"heldoutLongOutcome\":";
  writePredictorSet(output, heldout_audit, Target::LongOutcome, false);
  output << ",\n  \"heldoutFairD4Labels\":";
  writePredictorSet(output, heldout_audit, Target::FairD4, false);
  output << ",\n  \"heldoutHalvesLongOutcome\":[";
  for (int half = 0; half < 2; ++half) {
    if (half > 0) output << ',';
    const auto include = [half](const RootAudit& root) {
      return root.stored.label.game / 6 == half;
    };
    output << "{\"half\":" << half << ",\"old\":";
    writeMetrics(output, ranking(heldout_audit, Target::LongOutcome,
                                 Predictor::Old, include));
    output << ",\"candidate\":";
    writeMetrics(output, ranking(heldout_audit, Target::LongOutcome,
                                 Predictor::Candidate, include));
    output << '}';
  }
  output << "],\n  \"resources\":{\"fittingOld\":";
  writeResources(output, resources(fitting_audit, Predictor::Old));
  output << ",\"fittingScaleOnly\":";
  writeResources(output, resources(fitting_audit, Predictor::ScaleOnly));
  output << ",\"fittingLeafOnly\":";
  writeResources(output, resources(fitting_audit, Predictor::LeafOnly));
  output << ",\"fittingCandidate\":";
  writeResources(output, resources(fitting_audit, Predictor::Candidate));
  output << ",\"heldoutOld\":";
  writeResources(output, resources(heldout_audit, Predictor::Old));
  output << ",\"heldoutCandidate\":";
  writeResources(output, resources(heldout_audit, Predictor::Candidate));
  output << "},\n  \"historicalOldHeldout\":{"
            "\"artifact\":\"/tmp/drop7-observable-mcts-lab.json\","
            "\"levelBonus\":7000,\"comparableToCurrentRanking\":false,"
            "\"roots\":32,\"top1\":0.34375,"
            "\"pairwise\":0.6497622821,"
            "\"rawMeanRegret\":28420.47949},\n"
         << "  \"proposalGate\":{"
            "\"requiresHeldoutTop1NonRegression\":true,"
            "\"requiresStrictPairwiseGain\":true,"
            "\"requiresStrictNormalizedRegretGain\":true,"
            "\"requiresNoHalfRegression\":true,\"passed\":"
         << (propose_fresh_gameplay ? "true" : "false") << "},\n"
         << "  \"decision\":\""
         << (propose_fresh_gameplay
                 ? "propose-separately-preregistered-fresh-gameplay;not-run"
                 : "reject-before-gameplay")
         << "\",\n  \"roots\":[";
  bool first_root = true;
  const auto write_roots = [&](const std::vector<RootAudit>& roots,
                               std::string_view split) {
    for (const RootAudit& root : roots) {
      if (!first_root) output << ',';
      first_root = false;
      const d4::RootLabel& truth = root.current_long;
      output << "{\"split\":\"" << split << "\",\"game\":"
             << truth.game << ",\"moveInSourceGame\":"
             << truth.move_in_game << ",\"longOutcomeAction\":"
             << bestAction(truth, truth.q) << ",\"fairD4Action\":"
             << root.current_d4.labeled_action
             << ",\"historicalLongOutcomeAction\":"
             << root.stored.label.labeled_action
             << ",\"historicalFairD4Action\":"
             << root.stored.d4.labeled_action << ",\"fairD3Action\":"
             << root.fair_d3.action << ",\"oldAction\":"
             << root.old.action << ",\"candidateAction\":"
             << root.candidate.action << ",\"longOutcomeQ\":";
      writeArray(output, truth.q);
      output << ",\"d4Q\":";
      writeArray(output, root.current_d4.q);
      output << ",\"oldQ\":";
      writeArray(output, root.old.q);
      output << ",\"candidateQ\":";
      writeArray(output, root.candidate.q);
      output << '}';
    }
  };
  write_roots(fitting_audit, "fitting");
  write_roots(heldout_audit, "heldout");
  output << "],\n  \"peakRssBytes\":" << peakRssBytes()
         << ",\n  \"totalWallSeconds\":"
         << std::chrono::duration<double>(Clock::now() - started).count()
         << "\n}\n";

  const Metrics heldout_old = rankingAll(
      heldout_audit, Target::LongOutcome, Predictor::Old);
  const Metrics heldout_candidate = rankingAll(
      heldout_audit, Target::LongOutcome, Predictor::Candidate);
  std::cout << std::fixed << std::setprecision(5)
            << "SCALED_OBSERVABLE_MCTS {\"oldTop1\":"
            << heldout_old.topOneRate() << ",\"candidateTop1\":"
            << heldout_candidate.topOneRate() << ",\"oldPairwise\":"
            << heldout_old.pairwiseRate() << ",\"candidatePairwise\":"
            << heldout_candidate.pairwiseRate()
            << ",\"oldNormalizedRegret\":"
            << heldout_old.normalizedRegret()
            << ",\"candidateNormalizedRegret\":"
            << heldout_candidate.normalizedRegret()
            << ",\"proposalGate\":"
            << (propose_fresh_gameplay ? "true" : "false") << "}\n";
  return EXIT_SUCCESS;
}

}  // namespace drop7::observable_mcts_scaled_audit

#ifndef DROP7_OBSERVABLE_MCTS_SCALED_AUDIT_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::observable_mcts_scaled_audit::selfTest(std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--audit") {
      const auto options =
          drop7::observable_mcts_scaled_audit::parseOptions(argc, argv, 2);
      return drop7::observable_mcts_scaled_audit::runAudit(options);
    }
    throw std::invalid_argument(
        "usage: drop7_observable_mcts_scaled_audit --self-test | --audit "
        "[--threads N] [--labels PATH] [--d4-source PATH] [--output PATH]");
  } catch (const std::exception& error) {
    std::cerr << "drop7_observable_mcts_scaled_audit: " << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
}
#endif
