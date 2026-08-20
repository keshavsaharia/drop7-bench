#include "../../../src/core/native/public-behavior.hpp"

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
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <thread>
#include <utility>
#include <vector>

// Runs stochastic UCT on public Drop7 states without determinization.  It never
// stores a future tape, game seed, or chance-generator state in a node, avoiding
// information leakage and strategy fusion.  Nodes are keyed only by the
// canonical observable state plus remaining search horizon.
// Chance outcomes are sampled only when an edge is visited and enter a bounded
// per-edge reservoir; later decisions see only the realized public successor.
namespace drop7::observable_mcts {

using Clock = std::chrono::steady_clock;

constexpr std::array<int, 4> kSimulationBudgets{{256, 1'024, 4'096, 16'384}};
constexpr std::array<int, 3> kHorizons{{8, 16, 32}};
constexpr int kSettingCount =
    static_cast<int>(kSimulationBudgets.size() * kHorizons.size());
constexpr int kMaximumHorizon = 32;
constexpr int kLabelHorizon = 60;
constexpr int kLabelScenarios = 32;
constexpr double kValueScale = 100'000.0;
constexpr double kTerminalUtility = -10.0;
constexpr double kUctExploration = 1.4142135623730951;
constexpr int kSuccessorReservoir = 8;
constexpr std::size_t kMaximumNodes = 16'385;
constexpr std::size_t kMaximumOutcomes = 16'384;
constexpr std::size_t kHashSlots = 65'536;
constexpr std::size_t kMemoryCapBytes = 32u * 1024u * 1024u;
constexpr int kMaximumMoves = 200;
constexpr int kDefaultThreads = 4;

constexpr std::uint32_t kFittingStart = 0x3da0'0000u;
constexpr int kFittingGames = 32;
constexpr int kFittingRoots = kFittingGames * 2;
constexpr std::uint32_t kHeldoutStart = 0x3da1'0000u;
constexpr int kHeldoutGames = 16;
constexpr int kHeldoutRoots = kHeldoutGames * 2;
constexpr std::array<int, 2> kRootMoves{{12, 24}};
constexpr std::uint32_t kScreenStart = 0x3e99'0000u;
constexpr int kScreenGames = 4;
constexpr std::uint32_t kConfirmationStart = 0x3e9a'0000u;
constexpr int kConfirmationGames = 8;
constexpr std::uint32_t kChanceDomain = 0x4d43'5453u;       // "MCTS"
constexpr std::uint32_t kRolloutDomain = 0x524f'4c4cu;      // "ROLL"
constexpr std::uint32_t kLabelDomain = 0x4c41'424cu;        // "LABL"
constexpr std::uint32_t kVisitMultiplier = 0x9e37'79b9u;
constexpr std::uint32_t kActionMultiplier = 0x85eb'ca6bu;
constexpr std::uint32_t kOrdinalMultiplier = 0xc2b2'ae35u;

static_assert(kLevelBonus == 7'000);
static_assert(kMaximumHorizon == kHorizons.back());
static_assert(kMaximumNodes ==
              static_cast<std::size_t>(kSimulationBudgets.back()) + 1u);
static_assert((kFittingStart >> 24u) != 0x7du &&
              (kFittingStart >> 24u) != 0xd7u);
static_assert((kHeldoutStart >> 24u) != 0x7du &&
              (kHeldoutStart >> 24u) != 0xd7u);
static_assert((kScreenStart >> 24u) != 0x7du &&
              (kScreenStart >> 24u) != 0xd7u);
static_assert((kConfirmationStart >> 24u) != 0x7du &&
              (kConfirmationStart >> 24u) != 0xd7u);

std::mutex progress_mutex;

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

State canonicalPublicState(const State& source, bool& mirrored) {
  return cfpi::detail::canonicalState(publicState(source), mirrored);
}

bool samePublicState(const State& left, const State& right) {
  return left.board == right.board && left.next_disc == right.next_disc &&
         left.moves_remaining == right.moves_remaining &&
         left.game_over == right.game_over;
}

std::uint64_t mix64(std::uint64_t value) {
  value ^= value >> 30u;
  value *= 0xbf58'476d'1ce4'e5b9ull;
  value ^= value >> 27u;
  value *= 0x94d0'49bb'1331'11ebull;
  return value ^ (value >> 31u);
}

std::uint64_t observableHash(const State& source, int remaining = -1) {
  const State state = publicState(source);
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint64_t>(cell + 1u);
    hash *= 0x0000'0100'0000'01b3ull;
  }
  hash ^= state.next_disc;
  hash *= 0x0000'0100'0000'01b3ull;
  hash ^= static_cast<std::uint64_t>(state.moves_remaining + 1);
  hash *= 0x0000'0100'0000'01b3ull;
  hash ^= static_cast<std::uint64_t>(state.game_over);
  if (remaining >= 0) {
    hash ^= static_cast<std::uint64_t>(remaining + 1) << 48u;
  }
  return mix64(hash);
}

std::uint32_t seed32(std::uint64_t value) {
  return mix32(static_cast<std::uint32_t>(value) ^
               static_cast<std::uint32_t>(value >> 32u));
}

int phaseDepthOneAction(const State& source) {
  if (source.game_over) return -1;
  bool mirrored = false;
  const State state = canonicalPublicState(source, mirrored);
  const std::uint32_t chance_seed = cfpi::detail::scenarioSeedForState(
      state, 0xd707'5eedu, 1);
  int best_action = -1;
  double best_value = -std::numeric_limits<double>::infinity();
  for (const int action : cfpi::detail::kColumnOrder) {
    if (!isLegal(state.board, action)) continue;
    cfpi::detail::StratifiedRandom random{chance_seed, 0, 1, 0};
    MoveResult move;
    if (!cfpi::detail::playMoveSampled(state, action, random, move)) {
      continue;
    }
    double value = static_cast<double>(move.score_delta) / kValueScale;
    if (move.state.game_over) {
      value += kTerminalUtility;
    } else {
      move.state.score = 0;
      move.state.next_disc =
          cfpi::detail::sampledNextDisc(chance_seed, 0, 1);
      value += cfpi::phasePotential(move.state) / kValueScale;
    }
    if (value > best_value) {
      best_value = value;
      best_action = action;
    }
  }
  if (best_action < 0) best_action = centerFirstMove(state.board);
  return mirrored && best_action >= 0 ? kBoardSize - 1 - best_action
                                      : best_action;
}

struct ActionStats {
  double value_sum = 0.0;
  std::uint32_t visits = 0;
  std::array<std::uint32_t, kSuccessorReservoir> outcomes{};
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

class Arena {
 public:
  Arena() {
    nodes_.reserve(kMaximumNodes);
    outcomes_.reserve(kMaximumOutcomes);
    table_.resize(kHashSlots);
    if (reservedBytes() > kMemoryCapBytes) {
      throw std::logic_error("observable MCTS arena exceeds 32 MiB cap");
    }
  }

  std::pair<std::uint32_t, bool> findOrInsert(const State& source,
                                              int remaining) {
    bool ignored = false;
    const State state = canonicalPublicState(source, ignored);
    const std::uint64_t hash = observableHash(state, remaining);
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
          samePublicState(candidate.state, state)) {
        return {index, false};
      }
      slot = (slot + 1u) & (kHashSlots - 1u);
    }
    throw std::runtime_error("observable MCTS hash arena is full");
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
  std::size_t reservedBytes() const {
    return kMaximumNodes * sizeof(Node) +
           kMaximumOutcomes * sizeof(Outcome) +
           kHashSlots * sizeof(HashSlot);
  }
  std::size_t activeBytes() const {
    return nodes_.size() * sizeof(Node) +
           outcomes_.size() * sizeof(Outcome) +
           kHashSlots * sizeof(HashSlot);
  }
  bool reservoirsBounded() const {
    for (const Node& node : nodes_) {
      for (const ActionStats& action : node.actions) {
        if (action.outcome_count > kSuccessorReservoir) return false;
      }
    }
    return true;
  }

 private:
  std::vector<Node> nodes_;
  std::vector<Outcome> outcomes_;
  std::vector<HashSlot> table_;
};

struct Transition {
  State state{};
  double reward = 0.0;
  bool terminal = false;
};

Transition samplePublicTransition(const State& source, int action,
                                  int ordinal) {
  const State state = publicState(source);
  const std::uint64_t state_hash = observableHash(state);
  const std::uint32_t random_seed = seed32(
      state_hash ^ kChanceDomain ^
      (static_cast<std::uint64_t>(action + 1) * kActionMultiplier) ^
      (static_cast<std::uint64_t>(ordinal + 1) * kOrdinalMultiplier));
  Mulberry32 random(random_seed);
  MoveResult move;
  if (!playMove(state, action, random, move)) {
    throw std::runtime_error("observable MCTS sampled an illegal transition");
  }
  Transition result;
  result.reward = static_cast<double>(move.score_delta) / kValueScale;
  result.terminal = move.state.game_over;
  bool ignored = false;
  result.state = canonicalPublicState(move.state, ignored);
  return result;
}

int progressiveWidth(std::uint32_t visits) {
  const int width =
      1 + static_cast<int>(std::floor(std::sqrt(visits)));
  return std::min(kSuccessorReservoir, width);
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
    const double bonus = kUctExploration *
                         std::sqrt(log_parent / edge.visits);
    const double score = mean + bonus;
    if (score > best) {
      best = score;
      selected = action;
    }
  }
  return selected;
}

struct PathStep {
  std::uint32_t node = 0;
  int action = -1;
  double reward = 0.0;
};

struct MctsSnapshot {
  int simulations = 0;
  int horizon = 0;
  int action = -1;
  std::array<double, kBoardSize> q{};
  std::array<std::uint32_t, kBoardSize> visits{};
  bool complete = false;
  std::size_t nodes = 0;
  std::size_t outcomes = 0;
  std::size_t active_bytes = 0;
  std::size_t reserved_bytes = 0;
  std::uint64_t tree_steps = 0;
  std::uint64_t rollout_steps = 0;
  std::uint64_t transposition_hits = 0;
  std::uint64_t arena_full = 0;
  double seconds = 0.0;
};

class MctsSearch {
 public:
  MctsSearch(const State& source, int horizon)
      : horizon_(horizon), started_(Clock::now()) {
    if (horizon < 1 || horizon > kMaximumHorizon || source.game_over) {
      throw std::invalid_argument("invalid observable MCTS root");
    }
    State canonical = canonicalPublicState(source, mirrored_);
    root_public_hash_ = observableHash(canonical);
    const auto [root, inserted] = arena_.findOrInsert(canonical, horizon);
    if (!inserted || root != 0) {
      throw std::logic_error("observable MCTS root insertion failed");
    }
  }

  void runTo(int target_simulations) {
    if (target_simulations < completed_simulations_ ||
        target_simulations > kSimulationBudgets.back()) {
      throw std::invalid_argument("invalid observable MCTS simulation target");
    }
    while (completed_simulations_ < target_simulations && !incomplete_) {
      simulate(completed_simulations_);
      ++completed_simulations_;
    }
  }

  MctsSnapshot snapshot() const {
    MctsSnapshot result;
    result.simulations = completed_simulations_;
    result.horizon = horizon_;
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
    result.complete = !incomplete_ &&
                      completed_simulations_ > 0 &&
                      result.action >= 0;
    result.nodes = arena_.nodeCount();
    result.outcomes = arena_.outcomeCount();
    result.active_bytes = arena_.activeBytes();
    result.reserved_bytes = arena_.reservedBytes();
    result.tree_steps = tree_steps_;
    result.rollout_steps = rollout_steps_;
    result.transposition_hits = transposition_hits_;
    result.arena_full = arena_full_;
    result.seconds =
        std::chrono::duration<double>(Clock::now() - started_).count();
    return result;
  }

  bool reservoirsBounded() const { return arena_.reservoirsBounded(); }

 private:
  double rollout(State state, int remaining, int simulation,
                 int starting_depth) {
    const std::uint32_t random_seed = seed32(
        root_public_hash_ ^ kRolloutDomain ^
        (static_cast<std::uint64_t>(simulation + 1) * kVisitMultiplier) ^
        (static_cast<std::uint64_t>(starting_depth + 1) *
         kOrdinalMultiplier));
    Mulberry32 random(random_seed);
    double value = 0.0;
    for (int step = 0; step < remaining && !state.game_over; ++step) {
      const int action = phaseDepthOneAction(state);
      if (!isLegal(state.board, action)) return value + kTerminalUtility;
      MoveResult move;
      if (!playMove(publicState(state), action, random, move)) {
        return value + kTerminalUtility;
      }
      ++rollout_steps_;
      value += static_cast<double>(move.score_delta) / kValueScale;
      state = publicState(move.state);
      if (state.game_over) return value + kTerminalUtility;
    }
    return value;
  }

  void simulate(int simulation) {
    std::array<PathStep, kMaximumHorizon> path{};
    int path_size = 0;
    std::uint32_t node_index = 0;
    double tail = 0.0;
    for (int depth = 0; depth < horizon_; ++depth) {
      const Node& selection_node = arena_.node(node_index);
      const int action = selectUctAction(selection_node);
      if (action < 0) {
        tail = kTerminalUtility;
        break;
      }
      const ActionStats& selection_edge = selection_node.actions[action];
      const int allowed = progressiveWidth(selection_edge.visits);
      if (selection_edge.outcome_count < allowed) {
        const int ordinal = selection_edge.outcome_count;
        const Transition transition = samplePublicTransition(
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
          seed32(observableHash(selection_node.state)) ^
          (static_cast<std::uint32_t>(action + 1) * kActionMultiplier) ^
          (static_cast<std::uint32_t>(selection_edge.visits + 1) *
           kVisitMultiplier));
      const int reservoir_index = static_cast<int>(
          choice_bits % selection_edge.outcome_count);
      const Outcome outcome = arena_.outcome(
          selection_edge.outcomes[reservoir_index]);
      path[path_size++] = {node_index, action, outcome.reward};
      ++tree_steps_;
      if (outcome.child == kTerminalChild) {
        tail = kTerminalUtility;
        break;
      }
      node_index = outcome.child;
      if (arena_.node(node_index).remaining == 0) break;
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

  int horizon_ = 0;
  bool mirrored_ = false;
  std::uint64_t root_public_hash_ = 0;
  Arena arena_;
  Clock::time_point started_;
  int completed_simulations_ = 0;
  bool incomplete_ = false;
  std::uint64_t tree_steps_ = 0;
  std::uint64_t rollout_steps_ = 0;
  std::uint64_t transposition_hits_ = 0;
  std::uint64_t arena_full_ = 0;
};

struct ExactRoot {
  int action = -1;
  std::array<double, kBoardSize> q{};
  std::uint64_t work = 0;
};

cfpi::BehaviorOptions exactOptions() {
  cfpi::BehaviorOptions options;
  options.max_depth = 3;
  options.chance_samples = 5;
  options.max_work = 1'000'000;
  options.max_cache_entries = 40'000;
  return options;
}

ExactRoot exactRootValues(const State& source) {
  bool mirrored = false;
  const State canonical = canonicalPublicState(source, mirrored);
  const cfpi::BehaviorOptions options = exactOptions();
  cfpi::detail::SearchContext context(options);
  ExactRoot result;
  result.q.fill(-std::numeric_limits<double>::infinity());
  int canonical_action = -1;
  double best = -std::numeric_limits<double>::infinity();
  try {
    for (int depth = 1; depth < 3; ++depth) {
      const auto iteration =
          cfpi::detail::bestRootAction(canonical, depth, context);
      if (iteration.first < 0) {
        throw std::runtime_error("exact d3 iterative root is empty");
      }
    }
    for (const int action : cfpi::detail::kColumnOrder) {
      if (!isLegal(canonical.board, action)) continue;
      const double value =
          cfpi::detail::evaluateAction(canonical, action, 3, context);
      const int physical = mirrored ? kBoardSize - 1 - action : action;
      result.q[physical] = value;
      if (value > best) {
        best = value;
        canonical_action = action;
      }
    }
  } catch (const cfpi::detail::WorkLimitReached&) {
    throw std::runtime_error("exact d3 root exceeded its work bound");
  }
  result.action = canonical_action < 0
                      ? -1
                      : (mirrored ? kBoardSize - 1 - canonical_action
                                  : canonical_action);
  result.work = context.work;
  if (result.action < 0) throw std::runtime_error("exact d3 root is empty");
  return result;
}

struct RootCase {
  std::uint32_t origin_seed = 0;
  int origin_move = 0;
  State state{};
};

std::vector<RootCase> collectRoots(std::uint32_t start, int games,
                                   std::string_view split) {
  std::vector<RootCase> result;
  result.reserve(static_cast<std::size_t>(games * kRootMoves.size()));
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed = start + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    std::size_t target = 0;
    while (!state.game_over && target < kRootMoves.size()) {
      if (state.moves_played == kRootMoves[target]) {
        result.push_back({seed, state.moves_played, publicState(state)});
        ++target;
        if (target == kRootMoves.size()) break;
      }
      cfpi::BehaviorMetrics metrics;
      const int action = cfpi::chooseBehaviorAction(
          publicState(state), exactOptions(), &metrics);
      if (!metrics.complete || metrics.completed_depth != 3) {
        throw std::runtime_error("root collection exact d3 was incomplete");
      }
      MoveResult move;
      if (!playHeadlessMove(state, seed, action, move)) {
        throw std::runtime_error("root collection transition failed");
      }
    }
    if (target != kRootMoves.size()) {
      throw std::runtime_error("root collection game ended before move 24");
    }
    const std::lock_guard<std::mutex> lock(progress_mutex);
    std::cerr << "mcts-roots " << split << ' ' << game + 1 << '/' << games
              << " seed 0x" << std::hex << seed << std::dec << '\n';
  }
  return result;
}

double rolloutLabel(const State& source, int first_action,
                    std::uint32_t scenario_seed) {
  State state = publicState(source);
  Mulberry32 random(scenario_seed);
  double value = 0.0;
  for (int step = 0; step < kLabelHorizon && !state.game_over; ++step) {
    const int action = step == 0 ? first_action : phaseDepthOneAction(state);
    if (!isLegal(state.board, action)) return value - 1'000'000.0;
    MoveResult move;
    if (!playMove(publicState(state), action, random, move)) {
      return value - 1'000'000.0;
    }
    value += static_cast<double>(move.score_delta);
    state = publicState(move.state);
    if (state.game_over) value -= 1'000'000.0;
  }
  return value;
}

std::array<double, kBoardSize> independentRolloutLabels(const State& state) {
  std::array<double, kBoardSize> result{};
  result.fill(-std::numeric_limits<double>::infinity());
  const std::uint64_t root_hash = observableHash(state);
  for (int action = 0; action < kBoardSize; ++action) {
    if (!isLegal(state.board, action)) continue;
    double sum = 0.0;
    for (int scenario = 0; scenario < kLabelScenarios; ++scenario) {
      // Identical seed for every sibling in a scenario: common random numbers.
      // These independent label tapes never enter MCTS nodes or policy calls.
      const std::uint32_t scenario_seed = seed32(
          root_hash ^ kLabelDomain ^
          (static_cast<std::uint64_t>(scenario + 1) * kVisitMultiplier));
      sum += rolloutLabel(state, action, scenario_seed);
    }
    result[action] = sum / kLabelScenarios;
  }
  return result;
}

struct RootAudit {
  RootCase root;
  std::array<double, kBoardSize> labels{};
  ExactRoot exact;
  std::array<MctsSnapshot, kSettingCount> settings{};
};

int settingIndex(int horizon_index, int budget_index) {
  return horizon_index * static_cast<int>(kSimulationBudgets.size()) +
         budget_index;
}

RootAudit auditFittingRoot(const RootCase& root) {
  RootAudit result;
  result.root = root;
  result.labels = independentRolloutLabels(root.state);
  result.exact = exactRootValues(root.state);
  for (int horizon = 0; horizon < static_cast<int>(kHorizons.size());
       ++horizon) {
    MctsSearch search(root.state, kHorizons[horizon]);
    for (int budget = 0;
         budget < static_cast<int>(kSimulationBudgets.size()); ++budget) {
      search.runTo(kSimulationBudgets[budget]);
      result.settings[settingIndex(horizon, budget)] = search.snapshot();
    }
  }
  return result;
}

RootAudit auditHeldoutRoot(const RootCase& root, int simulations,
                           int horizon) {
  RootAudit result;
  result.root = root;
  result.labels = independentRolloutLabels(root.state);
  result.exact = exactRootValues(root.state);
  MctsSearch search(root.state, horizon);
  search.runTo(simulations);
  result.settings[0] = search.snapshot();
  return result;
}

template <typename Function>
std::vector<RootAudit> parallelAudit(const std::vector<RootCase>& roots,
                                     int threads, std::string_view split,
                                     Function function) {
  std::vector<RootAudit> result(roots.size());
  std::atomic<int> next{0};
  std::atomic<bool> failed{false};
  std::mutex error_mutex;
  std::string error_message;
  std::vector<std::thread> workers;
  for (int worker = 0;
       worker < std::min(threads, static_cast<int>(roots.size())); ++worker) {
    workers.emplace_back([&] {
      while (!failed.load(std::memory_order_relaxed)) {
        const int index = next.fetch_add(1, std::memory_order_relaxed);
        if (index >= static_cast<int>(roots.size())) return;
        try {
          result[index] = function(roots[index]);
          const std::lock_guard<std::mutex> lock(progress_mutex);
          std::cerr << "mcts-audit " << split << ' ' << index + 1 << '/'
                    << roots.size() << " origin 0x" << std::hex
                    << roots[index].origin_seed << std::dec << " move "
                    << roots[index].origin_move << '\n';
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
    throw std::runtime_error("observable MCTS audit failed: " + error_message);
  }
  return result;
}

int bestColumn(const State& state,
               const std::array<double, kBoardSize>& values) {
  int result = -1;
  double best = -std::numeric_limits<double>::infinity();
  for (const int action : cfpi::detail::kColumnOrder) {
    if (!isLegal(state.board, action)) continue;
    if (values[action] > best) {
      best = values[action];
      result = action;
    }
  }
  return result;
}

struct RankingMetrics {
  int roots = 0;
  int top_one = 0;
  int pairs = 0;
  int concordant_pairs = 0;
  double regret_sum = 0.0;
  std::size_t maximum_nodes = 0;
  std::size_t maximum_outcomes = 0;
  std::size_t maximum_active_bytes = 0;
  std::size_t reserved_bytes = 0;
  std::uint64_t tree_steps = 0;
  std::uint64_t rollout_steps = 0;
  std::uint64_t transposition_hits = 0;
  std::uint64_t arena_full = 0;
  double seconds = 0.0;

  double topOneRate() const {
    return roots > 0 ? static_cast<double>(top_one) / roots : 0.0;
  }
  double pairwiseRate() const {
    return pairs > 0 ? static_cast<double>(concordant_pairs) / pairs : 0.0;
  }
  double meanRegret() const {
    return roots > 0 ? regret_sum / roots : 0.0;
  }
};

void addRanking(RankingMetrics& metrics, const RootAudit& root,
                const std::array<double, kBoardSize>& prediction,
                int action, const MctsSnapshot* snapshot) {
  const int target = bestColumn(root.root.state, root.labels);
  if (target < 0 || action < 0) return;
  ++metrics.roots;
  metrics.top_one += action == target;
  metrics.regret_sum += root.labels[target] - root.labels[action];
  for (int left = 0; left < kBoardSize; ++left) {
    if (!isLegal(root.root.state.board, left)) continue;
    for (int right = left + 1; right < kBoardSize; ++right) {
      if (!isLegal(root.root.state.board, right)) continue;
      const double target_delta = root.labels[left] - root.labels[right];
      const double predicted_delta = prediction[left] - prediction[right];
      if (std::abs(target_delta) <= 1.0e-9) continue;
      ++metrics.pairs;
      metrics.concordant_pairs += target_delta * predicted_delta > 0.0;
    }
  }
  if (snapshot != nullptr) {
    metrics.maximum_nodes = std::max(metrics.maximum_nodes, snapshot->nodes);
    metrics.maximum_outcomes =
        std::max(metrics.maximum_outcomes, snapshot->outcomes);
    metrics.maximum_active_bytes =
        std::max(metrics.maximum_active_bytes, snapshot->active_bytes);
    metrics.reserved_bytes =
        std::max(metrics.reserved_bytes, snapshot->reserved_bytes);
    metrics.tree_steps += snapshot->tree_steps;
    metrics.rollout_steps += snapshot->rollout_steps;
    metrics.transposition_hits += snapshot->transposition_hits;
    metrics.arena_full += snapshot->arena_full;
    metrics.seconds += snapshot->seconds;
  }
}

RankingMetrics exactRanking(const std::vector<RootAudit>& roots) {
  RankingMetrics result;
  for (const RootAudit& root : roots) {
    addRanking(result, root, root.exact.q, root.exact.action, nullptr);
  }
  return result;
}

RankingMetrics settingRanking(const std::vector<RootAudit>& roots,
                              int setting, bool heldout) {
  RankingMetrics result;
  for (const RootAudit& root : roots) {
    const MctsSnapshot& snapshot = root.settings[heldout ? 0 : setting];
    addRanking(result, root, snapshot.q, snapshot.action, &snapshot);
  }
  return result;
}

int selectSetting(const std::array<RankingMetrics, kSettingCount>& metrics) {
  int selected = 0;
  for (int setting = 1; setting < kSettingCount; ++setting) {
    const RankingMetrics& candidate = metrics[setting];
    const RankingMetrics& current = metrics[selected];
    const int candidate_horizon =
        setting / static_cast<int>(kSimulationBudgets.size());
    const int current_horizon =
        selected / static_cast<int>(kSimulationBudgets.size());
    const int candidate_budget =
        setting % static_cast<int>(kSimulationBudgets.size());
    const int current_budget =
        selected % static_cast<int>(kSimulationBudgets.size());
    bool better = false;
    if (candidate.meanRegret() < current.meanRegret() - 1.0e-9) {
      better = true;
    } else if (std::abs(candidate.meanRegret() - current.meanRegret()) <=
               1.0e-9) {
      if (candidate.pairwiseRate() > current.pairwiseRate() + 1.0e-12) {
        better = true;
      } else if (std::abs(candidate.pairwiseRate() -
                          current.pairwiseRate()) <= 1.0e-12) {
        if (candidate.topOneRate() > current.topOneRate() + 1.0e-12) {
          better = true;
        } else if (std::abs(candidate.topOneRate() -
                            current.topOneRate()) <= 1.0e-12) {
          better =
              kSimulationBudgets[candidate_budget] <
                  kSimulationBudgets[current_budget] ||
              (kSimulationBudgets[candidate_budget] ==
                   kSimulationBudgets[current_budget] &&
               kHorizons[candidate_horizon] < kHorizons[current_horizon]);
        }
      }
    }
    if (better) selected = setting;
  }
  return selected;
}

bool heldoutGate(const RankingMetrics& candidate,
                 const RankingMetrics& exact) {
  return candidate.topOneRate() >= 0.35 &&
         candidate.pairwiseRate() >= 0.62 &&
         candidate.meanRegret() < exact.meanRegret();
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

struct Game {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  int cleared = 0;
  int revealed = 0;
  int waves = 0;
  int fallbacks = 0;
  bool censored = false;
  std::uint64_t exact_work = 0;
  std::uint64_t simulations = 0;
  std::uint64_t tree_steps = 0;
  std::uint64_t rollout_steps = 0;
  std::size_t peak_active_bytes = 0;
  double seconds = 0.0;
};

Game runGame(std::uint32_t seed, bool candidate, int simulations,
             int horizon, std::string_view label) {
  const auto started = Clock::now();
  State state = initialHeadlessState(seed);
  Game result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    int action = -1;
    if (candidate) {
      MctsSearch search(publicState(state), horizon);
      search.runTo(simulations);
      const MctsSnapshot snapshot = search.snapshot();
      result.simulations += snapshot.simulations;
      result.tree_steps += snapshot.tree_steps;
      result.rollout_steps += snapshot.rollout_steps;
      result.peak_active_bytes =
          std::max(result.peak_active_bytes, snapshot.active_bytes);
      if (snapshot.complete && isLegal(state.board, snapshot.action)) {
        action = snapshot.action;
      } else {
        ++result.fallbacks;
      }
    }
    if (action < 0) {
      cfpi::BehaviorMetrics metrics;
      action = cfpi::chooseBehaviorAction(
          publicState(state), exactOptions(), &metrics);
      result.exact_work += metrics.work;
      if (!metrics.complete) {
        throw std::runtime_error("exact d3 fallback was incomplete");
      }
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("game policy selected an illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("game transition failed");
    }
    for (const Wave& wave : move.waves) {
      result.cleared += wave.cleared;
      result.revealed += wave.revealed;
      ++result.waves;
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  {
    const std::lock_guard<std::mutex> lock(progress_mutex);
    std::cerr << label << " seed 0x" << std::hex << seed << std::dec << ' '
              << result.score << " (" << result.moves << " moves, fallback "
              << result.fallbacks << ")\n";
  }
  return result;
}

struct Cohort {
  std::vector<Game> baseline;
  std::vector<Game> candidate;
  double wall_seconds = 0.0;
};

Cohort runCohort(std::uint32_t start, int games, int threads,
                 int simulations, int horizon, std::string_view phase) {
  const auto started = Clock::now();
  Cohort result;
  result.baseline.resize(games);
  result.candidate.resize(games);
  std::atomic<int> next{0};
  std::atomic<bool> failed{false};
  std::mutex error_mutex;
  std::string error_message;
  std::vector<std::thread> workers;
  for (int worker = 0; worker < std::min(threads, games); ++worker) {
    workers.emplace_back([&] {
      while (!failed.load(std::memory_order_relaxed)) {
        const int game = next.fetch_add(1, std::memory_order_relaxed);
        if (game >= games) return;
        try {
          const std::uint32_t seed = start + static_cast<std::uint32_t>(game);
          result.baseline[game] = runGame(
              seed, false, simulations, horizon,
              std::string(phase) + "-exact-d3");
          result.candidate[game] = runGame(
              seed, true, simulations, horizon,
              std::string(phase) + "-observable-mcts");
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
    throw std::runtime_error("observable MCTS cohort failed: " + error_message);
  }
  result.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

struct GameSummary {
  int games = 0;
  int censored = 0;
  int fallbacks = 0;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double waves_per_move = 0.0;
  double simulations_per_move = 0.0;
  double simulated_steps_per_second = 0.0;
  double aggregate_seconds = 0.0;
  std::size_t peak_active_bytes = 0;
};

GameSummary summarizeGames(const std::vector<Game>& games) {
  if (games.empty()) throw std::invalid_argument("empty MCTS game cohort");
  GameSummary result;
  result.games = static_cast<int>(games.size());
  double score = 0.0;
  double moves = 0.0;
  double clears = 0.0;
  double reveals = 0.0;
  double waves = 0.0;
  double simulations = 0.0;
  double steps = 0.0;
  for (const Game& game : games) {
    score += game.score;
    moves += game.moves;
    clears += game.cleared;
    reveals += game.revealed;
    waves += game.waves;
    simulations += static_cast<double>(game.simulations);
    steps += static_cast<double>(game.tree_steps + game.rollout_steps);
    result.aggregate_seconds += game.seconds;
    result.censored += game.censored;
    result.fallbacks += game.fallbacks;
    result.peak_active_bytes =
        std::max(result.peak_active_bytes, game.peak_active_bytes);
  }
  result.mean_score = score / games.size();
  result.mean_moves = moves / games.size();
  result.clears_per_move = clears / moves;
  result.reveals_per_move = reveals / moves;
  result.waves_per_move = waves / moves;
  result.simulations_per_move = simulations / moves;
  result.simulated_steps_per_second = steps / result.aggregate_seconds;
  return result;
}

struct PairedGames {
  double mean_score_delta = 0.0;
  double mean_move_delta = 0.0;
  int wins = 0;
  int ties = 0;
  int losses = 0;
};

PairedGames compareGames(const Cohort& cohort) {
  if (cohort.baseline.empty() ||
      cohort.baseline.size() != cohort.candidate.size()) {
    throw std::invalid_argument("MCTS game cohorts are not paired");
  }
  PairedGames result;
  for (std::size_t game = 0; game < cohort.baseline.size(); ++game) {
    result.mean_score_delta +=
        cohort.candidate[game].score - cohort.baseline[game].score;
    const int move_delta =
        cohort.candidate[game].moves - cohort.baseline[game].moves;
    result.mean_move_delta += move_delta;
    if (move_delta > 0) ++result.wins;
    else if (move_delta < 0) ++result.losses;
    else ++result.ties;
  }
  result.mean_score_delta /= cohort.baseline.size();
  result.mean_move_delta /= cohort.baseline.size();
  return result;
}

bool improvesBoth(const Cohort& cohort) {
  const GameSummary baseline = summarizeGames(cohort.baseline);
  const GameSummary candidate = summarizeGames(cohort.candidate);
  return candidate.mean_score > baseline.mean_score &&
         candidate.mean_moves > baseline.mean_moves;
}

void writeRanking(std::ostream& output, const RankingMetrics& metrics) {
  output << "{\"roots\":" << metrics.roots
         << ",\"top1\":" << metrics.topOneRate()
         << ",\"pairwise\":" << metrics.pairwiseRate()
         << ",\"meanRegret\":" << metrics.meanRegret()
         << ",\"pairs\":" << metrics.pairs
         << ",\"maximumNodes\":" << metrics.maximum_nodes
         << ",\"maximumOutcomes\":" << metrics.maximum_outcomes
         << ",\"maximumActiveBytes\":" << metrics.maximum_active_bytes
         << ",\"reservedBytes\":" << metrics.reserved_bytes
         << ",\"treeSteps\":" << metrics.tree_steps
         << ",\"rolloutSteps\":" << metrics.rollout_steps
         << ",\"transpositionHits\":" << metrics.transposition_hits
         << ",\"arenaFull\":" << metrics.arena_full
         << ",\"aggregateSeconds\":" << metrics.seconds << '}';
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

void writeRootAudit(std::ostream& output, const RootAudit& root,
                    int setting, bool heldout) {
  const MctsSnapshot& snapshot = root.settings[heldout ? 0 : setting];
  const int target = bestColumn(root.root.state, root.labels);
  output << "{\"originSeed\":" << root.root.origin_seed
         << ",\"originMove\":" << root.root.origin_move
         << ",\"labelBestAction\":" << target
         << ",\"exactAction\":" << root.exact.action
         << ",\"mctsAction\":" << snapshot.action
         << ",\"exactRegret\":"
         << root.labels[target] - root.labels[root.exact.action]
         << ",\"mctsRegret\":"
         << root.labels[target] - root.labels[snapshot.action]
         << ",\"labels\":";
  writeArray(output, root.labels);
  output << ",\"exactQ\":";
  writeArray(output, root.exact.q);
  output << ",\"mctsQ\":";
  writeArray(output, snapshot.q);
  output << ",\"nodes\":" << snapshot.nodes
         << ",\"outcomes\":" << snapshot.outcomes
         << ",\"activeBytes\":" << snapshot.active_bytes << '}';
}

void writeGameSummary(std::ostream& output, const GameSummary& summary) {
  output << "{\"games\":" << summary.games
         << ",\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"wavesPerMove\":" << summary.waves_per_move
         << ",\"simulationsPerMove\":" << summary.simulations_per_move
         << ",\"simulatedStepsPerSecond\":"
         << summary.simulated_steps_per_second
         << ",\"aggregateSeconds\":" << summary.aggregate_seconds
         << ",\"peakActiveBytes\":" << summary.peak_active_bytes
         << ",\"fallbacks\":" << summary.fallbacks
         << ",\"censored\":" << summary.censored << '}';
}

void writeCohort(std::ostream& output, const Cohort& cohort) {
  const GameSummary baseline = summarizeGames(cohort.baseline);
  const GameSummary candidate = summarizeGames(cohort.candidate);
  const PairedGames comparison = compareGames(cohort);
  output << "{\"baseline\":";
  writeGameSummary(output, baseline);
  output << ",\"candidate\":";
  writeGameSummary(output, candidate);
  output << ",\"paired\":{\"meanScoreDelta\":"
         << comparison.mean_score_delta << ",\"meanMoveDelta\":"
         << comparison.mean_move_delta << ",\"wins\":" << comparison.wins
         << ",\"ties\":" << comparison.ties
         << ",\"losses\":" << comparison.losses << "},\"games\":[";
  for (std::size_t game = 0; game < cohort.baseline.size(); ++game) {
    if (game > 0) output << ',';
    output << "{\"seed\":" << cohort.baseline[game].seed
           << ",\"baselineScore\":" << cohort.baseline[game].score
           << ",\"candidateScore\":" << cohort.candidate[game].score
           << ",\"scoreDelta\":"
           << cohort.candidate[game].score - cohort.baseline[game].score
           << ",\"baselineMoves\":" << cohort.baseline[game].moves
           << ",\"candidateMoves\":" << cohort.candidate[game].moves
           << ",\"moveDelta\":"
           << cohort.candidate[game].moves - cohort.baseline[game].moves
           << ",\"fallbacks\":" << cohort.candidate[game].fallbacks << '}';
  }
  output << "],\"wallSeconds\":" << cohort.wall_seconds << '}';
}

bool selfTest(std::ostream& output) {
  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;

  MctsSearch first_search(state, 8);
  first_search.runTo(256);
  const MctsSnapshot first = first_search.snapshot();
  MctsSearch repeat_search(state, 8);
  repeat_search.runTo(256);
  const MctsSnapshot repeat = repeat_search.snapshot();

  State metadata = state;
  metadata.score = 999'999;
  metadata.level = 77;
  metadata.moves_played = 321;
  MctsSearch metadata_search(metadata, 8);
  metadata_search.runTo(256);
  const MctsSnapshot metadata_result = metadata_search.snapshot();

  State mirrored = state;
  mirrored.board = cfpi::detail::mirrorBoard(state.board);
  MctsSearch mirror_search(mirrored, 8);
  mirror_search.runTo(256);
  const MctsSnapshot reflected = mirror_search.snapshot();

  const Transition chance_first = samplePublicTransition(state, 3, 0);
  const Transition chance_metadata = samplePublicTransition(metadata, 3, 0);
  const int fast_d1 = phaseDepthOneAction(state);
  const int header_d1 = cfpi::choosePhaseGreedyAction(publicState(state), 1);
  const ExactRoot exact = exactRootValues(state);
  const int exact_public =
      cfpi::chooseBehaviorAction(publicState(state), exactOptions());

  const bool deterministic = first.action == repeat.action &&
                             first.q == repeat.q &&
                             first.nodes == repeat.nodes &&
                             first.outcomes == repeat.outcomes;
  const bool public_boundary = metadata_result.action == first.action &&
                               metadata_result.q == first.q &&
                               observableHash(metadata, 8) ==
                                   observableHash(state, 8);
  const bool chance_boundary = chance_first.reward == chance_metadata.reward &&
                               chance_first.terminal == chance_metadata.terminal &&
                               samePublicState(chance_first.state,
                                               chance_metadata.state);
  const bool reflection_safe =
      reflected.action == kBoardSize - 1 - first.action;
  const bool bounded = first.reserved_bytes <= kMemoryCapBytes &&
                       first.nodes <= kMaximumNodes &&
                       first.outcomes <= kMaximumOutcomes &&
                       first_search.reservoirsBounded() &&
                       first.arena_full == 0;
  const bool exact_fallback = exact.action == exact_public &&
                              isLegal(state.board, exact.action);
  const bool phase_parity = fast_d1 == header_d1;
  const bool complete = first.complete && first.simulations == 256;
  const bool passed = deterministic && public_boundary && chance_boundary &&
                      reflection_safe && bounded && exact_fallback &&
                      phase_parity && complete;
  output << std::setprecision(10)
         << "{\"passed\":" << (passed ? "true" : "false")
         << ",\"deterministic\":" << (deterministic ? "true" : "false")
         << ",\"publicBoundary\":"
         << (public_boundary ? "true" : "false")
         << ",\"chanceBoundary\":"
         << (chance_boundary ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe ? "true" : "false")
         << ",\"bounded\":" << (bounded ? "true" : "false")
         << ",\"exactFallback\":"
         << (exact_fallback ? "true" : "false")
         << ",\"phaseD1Parity\":" << (phase_parity ? "true" : "false")
         << ",\"complete\":" << (complete ? "true" : "false")
         << ",\"action\":" << first.action
         << ",\"nodes\":" << first.nodes
         << ",\"outcomes\":" << first.outcomes
         << ",\"activeBytes\":" << first.active_bytes
         << ",\"reservedBytes\":" << first.reserved_bytes << "}\n";
  return passed;
}

struct Options {
  int threads = kDefaultThreads;
  std::string output = "/tmp/drop7-observable-mcts-lab.json";
};

Options parseOptions(int argc, char** argv) {
  Options result;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--threads" && index + 1 < argc) {
      result.threads = std::stoi(argv[++index]);
    } else if (argument == "--output" && index + 1 < argc) {
      result.output = argv[++index];
    } else {
      throw std::invalid_argument("unknown observable MCTS option");
    }
  }
  if (result.threads < 1 || result.threads > 16) {
    throw std::invalid_argument("observable MCTS threads must be from 1 to 16");
  }
  return result;
}

int run(int argc, char** argv) {
  const auto started = Clock::now();
  const Options options = parseOptions(argc, argv);
  const std::vector<RootCase> fitting_roots =
      collectRoots(kFittingStart, kFittingGames, "fitting");
  const std::vector<RootCase> heldout_roots =
      collectRoots(kHeldoutStart, kHeldoutGames, "heldout");
  if (static_cast<int>(fitting_roots.size()) != kFittingRoots ||
      static_cast<int>(heldout_roots.size()) != kHeldoutRoots) {
    throw std::runtime_error("observable MCTS root corpus size changed");
  }

  const std::vector<RootAudit> fitting = parallelAudit(
      fitting_roots, options.threads, "fitting", auditFittingRoot);
  const RankingMetrics fitting_exact = exactRanking(fitting);
  std::array<RankingMetrics, kSettingCount> fitting_metrics;
  for (int setting = 0; setting < kSettingCount; ++setting) {
    fitting_metrics[setting] = settingRanking(fitting, setting, false);
  }
  const int selected = selectSetting(fitting_metrics);
  const int selected_horizon_index =
      selected / static_cast<int>(kSimulationBudgets.size());
  const int selected_budget_index =
      selected % static_cast<int>(kSimulationBudgets.size());
  const int selected_horizon = kHorizons[selected_horizon_index];
  const int selected_simulations = kSimulationBudgets[selected_budget_index];

  const std::vector<RootAudit> heldout = parallelAudit(
      heldout_roots, options.threads, "heldout",
      [&](const RootCase& root) {
        return auditHeldoutRoot(root, selected_simulations,
                                selected_horizon);
      });
  const RankingMetrics heldout_exact = exactRanking(heldout);
  const RankingMetrics heldout_candidate =
      settingRanking(heldout, selected, true);
  const bool ranking_gate = heldoutGate(heldout_candidate, heldout_exact);

  Cohort screen;
  bool screen_passed = false;
  if (ranking_gate) {
    screen = runCohort(kScreenStart, kScreenGames, options.threads,
                       selected_simulations, selected_horizon, "screen");
    screen_passed = improvesBoth(screen);
  }
  Cohort confirmation;
  bool confirmation_passed = false;
  if (screen_passed) {
    confirmation = runCohort(
        kConfirmationStart, kConfirmationGames, options.threads,
        selected_simulations, selected_horizon, "confirmation");
    confirmation_passed = improvesBoth(confirmation);
  }

  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not write MCTS artifact");
  output << std::setprecision(10)
         << "{\n  \"format\":\"drop7-observable-mcts-lab-v1\",\n"
         << "  \"mechanics\":{\"levelBonus\":" << kLevelBonus << "},\n"
         << "  \"causalBoundary\":{\"nodeKey\":"
            "\"canonical-board,next-disc,moves-to-rise,terminal,remaining-search-horizon\","
            "\"gameSeedInPolicy\":false,\"futureTapeInPolicy\":false,"
            "\"chanceSampling\":\"on-edge-visit\","
            "\"laterDecisionInput\":\"realized-public-successor-only\","
            "\"determinizationWarning\":\"privileged-tape lookahead leaks future information and fuses incompatible future policies\"},\n"
         << "  \"search\":{\"algorithm\":\"UCT\",\"exploration\":"
         << kUctExploration
         << ",\"rolloutPolicy\":\"public-phase-d1\","
            "\"successorReservoir\":" << kSuccessorReservoir
         << ",\"progressiveWidth\":\"min(8,1+floor(sqrt(edgeVisits)))\","
            "\"nodeCap\":" << kMaximumNodes
         << ",\"outcomeCap\":" << kMaximumOutcomes
         << ",\"hashSlots\":" << kHashSlots
         << ",\"memoryCapBytes\":" << kMemoryCapBytes
         << ",\"arenaReservedBytes\":" << Arena{}.reservedBytes()
         << ",\"fallback\":\"exact-d3-on-incomplete-or-illegal\"},\n"
         << "  \"rootCorpus\":{\"fittingStart\":" << kFittingStart
         << ",\"fittingGames\":" << kFittingGames
         << ",\"fittingRoots\":" << kFittingRoots
         << ",\"heldoutStart\":" << kHeldoutStart
         << ",\"heldoutGames\":" << kHeldoutGames
         << ",\"heldoutRoots\":" << kHeldoutRoots
         << ",\"rootMoves\":[" << kRootMoves[0] << ',' << kRootMoves[1]
         << "],\"originGameDisjoint\":true},\n"
         << "  \"labels\":{\"horizon\":" << kLabelHorizon
         << ",\"scenariosPerSibling\":" << kLabelScenarios
         << ",\"commonRandomAcrossSiblings\":true,"
            "\"independentOfOriginTape\":true,"
            "\"continuationPolicy\":\"public-phase-d1\","
            "\"terminalUtility\":-1000000},\n"
         << "  \"selectionRule\":\"minimum-fitting-mean-regret; ties higher-pairwise, higher-top1, lower-budget, shorter-horizon\",\n"
         << "  \"fittingExactD3\":";
  writeRanking(output, fitting_exact);
  output << ",\n  \"fittingSettings\":[";
  for (int setting = 0; setting < kSettingCount; ++setting) {
    if (setting > 0) output << ',';
    const int horizon =
        setting / static_cast<int>(kSimulationBudgets.size());
    const int budget =
        setting % static_cast<int>(kSimulationBudgets.size());
    output << "{\"simulations\":" << kSimulationBudgets[budget]
           << ",\"horizon\":" << kHorizons[horizon]
           << ",\"selected\":" << (setting == selected ? "true" : "false")
           << ",\"metrics\":";
    writeRanking(output, fitting_metrics[setting]);
    output << '}';
  }
  output << "],\n  \"selected\":{\"simulations\":"
         << selected_simulations << ",\"horizon\":" << selected_horizon
         << "},\n  \"heldoutExactD3\":";
  writeRanking(output, heldout_exact);
  output << ",\n  \"heldoutCandidate\":";
  writeRanking(output, heldout_candidate);
  output << ",\n  \"heldoutGate\":{\"requiredTop1\":0.35,"
            "\"requiredPairwise\":0.62,"
            "\"requiresLowerRegretThanExactD3\":true,\"passed\":"
         << (ranking_gate ? "true" : "false") << "},\n"
         << "  \"heldoutRoots\":[";
  for (std::size_t root = 0; root < heldout.size(); ++root) {
    if (root > 0) output << ',';
    writeRootAudit(output, heldout[root], selected, true);
  }
  output << "],\n  \"screen\":";
  if (ranking_gate) writeCohort(output, screen);
  else output << "null";
  output << ",\n  \"screenPassed\":" << (screen_passed ? "true" : "false")
         << ",\n  \"confirmation\":";
  if (screen_passed) writeCohort(output, confirmation);
  else output << "null";
  output << ",\n  \"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\n  \"decision\":\""
         << (!ranking_gate
                 ? "reject-heldout-ranking"
                 : (!screen_passed
                        ? "reject-screen"
                        : (confirmation_passed ? "advance"
                                               : "reject-confirmation")))
         << "\",\n  \"forbiddenSeedFamiliesInspected\":false,\n"
         << "  \"peakRssBytes\":" << peakRssBytes()
         << ",\n  \"totalWallSeconds\":"
         << std::chrono::duration<double>(Clock::now() - started).count()
         << "\n}\n";

  std::cout << std::fixed << std::setprecision(4)
            << "OBSERVABLE_MCTS {\"selectedSimulations\":"
            << selected_simulations << ",\"selectedHorizon\":"
            << selected_horizon << ",\"heldoutTop1\":"
            << heldout_candidate.topOneRate() << ",\"heldoutPairwise\":"
            << heldout_candidate.pairwiseRate() << ",\"heldoutRegret\":"
            << heldout_candidate.meanRegret() << ",\"exactRegret\":"
            << heldout_exact.meanRegret() << ",\"rankingGate\":"
            << (ranking_gate ? "true" : "false")
            << ",\"screenPassed\":" << (screen_passed ? "true" : "false")
            << ",\"confirmationPassed\":"
            << (confirmation_passed ? "true" : "false")
            << ",\"peakRssBytes\":" << peakRssBytes() << "}\n";
  return EXIT_SUCCESS;
}

}  // namespace drop7::observable_mcts

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::observable_mcts::selfTest(std::cout) ? EXIT_SUCCESS
                                                         : EXIT_FAILURE;
    }
    return drop7::observable_mcts::run(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "drop7_observable_mcts_lab: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
