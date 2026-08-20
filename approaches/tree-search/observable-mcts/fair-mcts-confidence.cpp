#define main drop7_fair_only_horizon_embedded_main
#include "../../fair-expectimax/reference/fair-only-horizon.cpp"
#undef main

#define main drop7_observable_mcts_embedded_main
#include "observable-mcts-lab.cpp"
#undef main

#include <sstream>

// Runs confidence-gated observable MCTS over the reference fair-only depth-three
// policy.  Both embedded implementations retain their standalone self-tests;
// this lab changes neither one.  MCTS may only override the fair action when a
// simple Q-and-visit rule fixed on fitting roots accepts the challenger.
namespace drop7::fair_mcts_confidence {

namespace fair = fair_only_horizon;
namespace mcts = observable_mcts;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kFittingStart = 0x3db0'0000u;
constexpr int kFittingGames = 32;
constexpr int kFittingRoots = kFittingGames * 2;
constexpr std::uint32_t kHeldoutStart = 0x3db1'0000u;
constexpr int kHeldoutGames = 16;
constexpr int kHeldoutRoots = kHeldoutGames * 2;
constexpr std::array<int, 2> kRootMoves{{12, 24}};
constexpr int kLabelScenarios = 64;
constexpr int kLabelHorizon = 80;
constexpr double kLabelTerminalUtility = -1'000'000.0;
constexpr int kMctsSimulations = 16'384;
constexpr int kMctsHorizon = 32;
// The initial fitting-only grid ended at q=.20 and visit=.10.  It produced no
// nonzero rule at or below half of the raw switch count, before any heldout
// label was evaluated.  The same conjunctive rule family was therefore widened
// once on fitting data only; this final grid is fixed before heldout.
constexpr std::array<double, 8> kQThresholds{{
    0.02, 0.05, 0.10, 0.20, 0.40, 0.80, 1.60, 3.20,
}};
constexpr std::array<double, 8> kVisitThresholds{{
    0.00, 0.02, 0.05, 0.10, 0.20, 0.40, 0.60, 0.80,
}};
constexpr int kRuleCount =
    static_cast<int>(kQThresholds.size() * kVisitThresholds.size());
constexpr double kMaximumFittingSwitchFractionOfRaw = 0.50;
constexpr std::uint32_t kLabelDomain = 0x464d'4c42u;  // "FMLB"
constexpr std::uint32_t kScenarioMultiplier = 0x9e37'79b9u;
constexpr std::uint32_t kScreenStart = 0x3e9f'0000u;
constexpr int kScreenGames = 8;
constexpr std::uint32_t kConfirmationStart = 0x3ea0'0000u;
constexpr int kConfirmationGames = 16;
constexpr int kMaximumMoves = 1'000;
constexpr int kDefaultThreads = 4;

static_assert(kLevelBonus == 7'000);
static_assert(kMctsSimulations == mcts::kSimulationBudgets.back());
static_assert(kMctsHorizon == mcts::kMaximumHorizon);
static_assert((kFittingStart >> 24u) != 0x7du &&
              (kFittingStart >> 24u) != 0xd7u);
static_assert((kHeldoutStart >> 24u) != 0x7du &&
              (kHeldoutStart >> 24u) != 0xd7u);
static_assert((kScreenStart >> 24u) != 0x7du &&
              (kScreenStart >> 24u) != 0xd7u);
static_assert((kConfirmationStart >> 24u) != 0x7du &&
              (kConfirmationStart >> 24u) != 0xd7u);

std::mutex confidence_progress_mutex;

int fairDepthOneAction(const State& source) {
  if (source.game_over) return -1;
  bool mirrored = false;
  const State state = cfpi::detail::canonicalState(
      mcts::publicState(source), mirrored);
  const std::uint32_t chance_seed = cfpi::detail::scenarioSeedForState(
      state, fair::kPolicySeed, 1);
  int selected = -1;
  double best = -std::numeric_limits<double>::infinity();
  for (const int action : cfpi::detail::kColumnOrder) {
    if (!isLegal(state.board, action)) continue;
    cfpi::detail::StratifiedRandom random{chance_seed, 0, 1, 0};
    MoveResult move;
    if (!cfpi::detail::playMoveSampled(state, action, random, move)) continue;
    double value = static_cast<double>(move.score_delta);
    if (move.state.game_over) {
      value += fair::kTerminalUtility;
    } else {
      move.state.score = 0;
      move.state.next_disc =
          cfpi::detail::sampledNextDisc(chance_seed, 0, 1);
      value += fair::fairLeaf(move.state);
    }
    if (value > best) {
      best = value;
      selected = action;
    }
  }
  if (selected < 0) selected = centerFirstMove(state.board);
  return mirrored && selected >= 0 ? kBoardSize - 1 - selected : selected;
}

struct RootCase {
  std::uint32_t origin_seed = 0;
  int origin_move = 0;
  State state{};
};

std::vector<RootCase> collectFairRoots(std::uint32_t start, int games,
                                       std::string_view split) {
  std::vector<RootCase> result;
  result.reserve(static_cast<std::size_t>(games * kRootMoves.size()));
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed = start + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    std::size_t target = 0;
    while (!state.game_over && target < kRootMoves.size()) {
      if (state.moves_played == kRootMoves[target]) {
        result.push_back({seed, state.moves_played, mcts::publicState(state)});
        ++target;
        if (target == kRootMoves.size()) break;
      }
      const fair::SearchDecision decision =
          fair::chooseFairAction(mcts::publicState(state));
      if (!decision.complete || decision.completed_depth != fair::kDepth) {
        throw std::runtime_error("fair root collection search was incomplete");
      }
      MoveResult move;
      if (!playHeadlessMove(state, seed, decision.action, move)) {
        throw std::runtime_error("fair root collection transition failed");
      }
    }
    if (target != kRootMoves.size()) {
      throw std::runtime_error("fair root collection ended before move 24");
    }
    const std::lock_guard<std::mutex> lock(confidence_progress_mutex);
    std::cerr << "fair-mcts-roots " << split << ' ' << game + 1 << '/'
              << games << " seed 0x" << std::hex << seed << std::dec << '\n';
  }
  return result;
}

double continuationLabel(const State& source, int first_action,
                         std::uint32_t scenario_seed) {
  State state = mcts::publicState(source);
  Mulberry32 random(scenario_seed);
  double value = 0.0;
  for (int step = 0; step < kLabelHorizon && !state.game_over; ++step) {
    const int action = step == 0 ? first_action : fairDepthOneAction(state);
    if (!isLegal(state.board, action)) {
      return value + kLabelTerminalUtility;
    }
    MoveResult move;
    if (!playMove(mcts::publicState(state), action, random, move)) {
      return value + kLabelTerminalUtility;
    }
    value += static_cast<double>(move.score_delta);
    state = mcts::publicState(move.state);
    if (state.game_over) value += kLabelTerminalUtility;
  }
  return value;
}

std::array<double, kBoardSize> alignedLabels(const State& state) {
  std::array<double, kBoardSize> result{};
  result.fill(-std::numeric_limits<double>::infinity());
  const std::uint64_t root_hash = mcts::observableHash(state);
  for (int action = 0; action < kBoardSize; ++action) {
    if (!isLegal(state.board, action)) continue;
    double sum = 0.0;
    for (int scenario = 0; scenario < kLabelScenarios; ++scenario) {
      // Every sibling receives the same independently seeded environment tape
      // for a scenario.  The tape is used only to form offline labels; neither
      // fair search nor MCTS can inspect it.
      const std::uint32_t seed = mcts::seed32(
          root_hash ^ kLabelDomain ^
          (static_cast<std::uint64_t>(scenario + 1) *
           kScenarioMultiplier));
      sum += continuationLabel(state, action, seed);
    }
    result[action] = sum / kLabelScenarios;
  }
  return result;
}

struct Evidence {
  int fair_action = -1;
  int mcts_action = -1;
  double q_margin = 0.0;
  double visit_margin = 0.0;
  bool raw_switch = false;
};

Evidence evidenceFor(const fair::SearchDecision& fair_decision,
                     const mcts::MctsSnapshot& snapshot) {
  Evidence result;
  result.fair_action = fair_decision.action;
  result.mcts_action = snapshot.action;
  if (result.fair_action < 0 || result.mcts_action < 0) return result;
  result.raw_switch = result.fair_action != result.mcts_action;
  result.q_margin =
      snapshot.q[result.mcts_action] - snapshot.q[result.fair_action];
  result.visit_margin =
      (static_cast<double>(snapshot.visits[result.mcts_action]) -
       static_cast<double>(snapshot.visits[result.fair_action])) /
      snapshot.simulations;
  return result;
}

struct RootAudit {
  RootCase root;
  std::array<double, kBoardSize> labels{};
  fair::SearchDecision fair_decision;
  mcts::MctsSnapshot mcts_snapshot;
  Evidence evidence;
};

RootAudit auditRoot(const RootCase& root) {
  RootAudit result;
  result.root = root;
  result.labels = alignedLabels(root.state);
  result.fair_decision = fair::chooseFairAction(root.state);
  if (!result.fair_decision.complete) {
    throw std::runtime_error("fair fitting decision was incomplete");
  }
  mcts::MctsSearch search(root.state, kMctsHorizon);
  search.runTo(kMctsSimulations);
  result.mcts_snapshot = search.snapshot();
  if (!result.mcts_snapshot.complete ||
      result.mcts_snapshot.reserved_bytes > mcts::kMemoryCapBytes ||
      result.mcts_snapshot.arena_full != 0) {
    throw std::runtime_error("MCTS fitting decision was incomplete or unbounded");
  }
  result.evidence = evidenceFor(result.fair_decision,
                                result.mcts_snapshot);
  return result;
}

std::vector<RootAudit> parallelAudit(const std::vector<RootCase>& roots,
                                     int threads, std::string_view split) {
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
          result[index] = auditRoot(roots[index]);
          const std::lock_guard<std::mutex> lock(confidence_progress_mutex);
          std::cerr << "fair-mcts-audit " << split << ' ' << index + 1 << '/'
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
    throw std::runtime_error("fair MCTS audit failed: " + error_message);
  }
  return result;
}

struct ConfidenceRule {
  double q_threshold = 0.0;
  double visit_threshold = 0.0;
};

std::array<ConfidenceRule, kRuleCount> confidenceRules() {
  std::array<ConfidenceRule, kRuleCount> result{};
  int index = 0;
  for (const double q : kQThresholds) {
    for (const double visit : kVisitThresholds) {
      result[index++] = {q, visit};
    }
  }
  return result;
}

bool accepts(const ConfidenceRule& rule, const Evidence& evidence) {
  return evidence.raw_switch && evidence.q_margin >= rule.q_threshold &&
         evidence.visit_margin >= rule.visit_threshold;
}

int bestLabelAction(const RootAudit& root) {
  int selected = -1;
  double best = -std::numeric_limits<double>::infinity();
  for (const int action : cfpi::detail::kColumnOrder) {
    if (!isLegal(root.root.state.board, action)) continue;
    if (root.labels[action] > best) {
      best = root.labels[action];
      selected = action;
    }
  }
  return selected;
}

bool isExactLabelBest(const RootAudit& root, int action) {
  const int best = bestLabelAction(root);
  return best >= 0 && action >= 0 &&
         std::abs(root.labels[action] - root.labels[best]) <= 1.0e-9;
}

int labelBestCount(const RootAudit& root) {
  const int best = bestLabelAction(root);
  if (best < 0) return 0;
  int count = 0;
  for (int action = 0; action < kBoardSize; ++action) {
    if (isLegal(root.root.state.board, action) &&
        std::abs(root.labels[action] - root.labels[best]) <= 1.0e-9) {
      ++count;
    }
  }
  return count;
}

struct PolicyMetrics {
  int roots = 0;
  int top_one = 0;
  int label_tie_roots = 0;
  int tie_top_one = 0;
  int pairs = 0;
  int concordant_pairs = 0;
  int switches = 0;
  double regret_sum = 0.0;

  double topOneRate() const {
    return roots > 0 ? static_cast<double>(top_one) / roots : 0.0;
  }
  double pairwiseRate() const {
    return pairs > 0 ? static_cast<double>(concordant_pairs) / pairs : 0.0;
  }
  double meanRegret() const {
    return roots > 0 ? regret_sum / roots : 0.0;
  }
  double switchRate() const {
    return roots > 0 ? static_cast<double>(switches) / roots : 0.0;
  }
};

void addPolicyRoot(PolicyMetrics& metrics, const RootAudit& root,
                   int action,
                   const std::array<double, kBoardSize>& ranking) {
  const int target = bestLabelAction(root);
  if (target < 0 || action < 0) return;
  ++metrics.roots;
  const bool correct = isExactLabelBest(root, action);
  metrics.top_one += correct;
  const bool tied = labelBestCount(root) > 1;
  metrics.label_tie_roots += tied;
  metrics.tie_top_one += tied && correct;
  metrics.regret_sum += root.labels[target] - root.labels[action];
  metrics.switches += action != root.fair_decision.action;
  for (int left = 0; left < kBoardSize; ++left) {
    if (!isLegal(root.root.state.board, left)) continue;
    for (int right = left + 1; right < kBoardSize; ++right) {
      if (!isLegal(root.root.state.board, right)) continue;
      const double label_delta = root.labels[left] - root.labels[right];
      if (std::abs(label_delta) <= 1.0e-9) continue;
      const double rank_delta = ranking[left] - ranking[right];
      ++metrics.pairs;
      metrics.concordant_pairs += label_delta * rank_delta > 0.0;
    }
  }
}

PolicyMetrics fairMetrics(const std::vector<RootAudit>& roots) {
  PolicyMetrics result;
  for (const RootAudit& root : roots) {
    addPolicyRoot(result, root, root.fair_decision.action,
                  root.fair_decision.root_values);
  }
  return result;
}

PolicyMetrics rawMctsMetrics(const std::vector<RootAudit>& roots) {
  PolicyMetrics result;
  for (const RootAudit& root : roots) {
    addPolicyRoot(result, root, root.mcts_snapshot.action,
                  root.mcts_snapshot.q);
  }
  return result;
}

PolicyMetrics gatedMetrics(const std::vector<RootAudit>& roots,
                           const ConfidenceRule& rule) {
  PolicyMetrics result;
  for (const RootAudit& root : roots) {
    const bool override = accepts(rule, root.evidence);
    addPolicyRoot(result, root,
                  override ? root.mcts_snapshot.action
                           : root.fair_decision.action,
                  override ? root.mcts_snapshot.q
                           : root.fair_decision.root_values);
  }
  return result;
}

int selectRule(const std::array<ConfidenceRule, kRuleCount>& rules,
               const std::array<PolicyMetrics, kRuleCount>& metrics,
               const PolicyMetrics& raw) {
  const int maximum_switches = static_cast<int>(std::floor(
      raw.switches * kMaximumFittingSwitchFractionOfRaw));
  int selected = -1;
  for (int rule = 0; rule < kRuleCount; ++rule) {
    const PolicyMetrics& candidate = metrics[rule];
    if (candidate.switches == 0 ||
        candidate.switches > maximum_switches) {
      continue;
    }
    if (selected < 0) {
      selected = rule;
      continue;
    }
    const PolicyMetrics& current = metrics[selected];
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
          if (candidate.switches < current.switches) {
            better = true;
          } else if (candidate.switches == current.switches) {
            better = rules[rule].q_threshold +
                         rules[rule].visit_threshold >
                     rules[selected].q_threshold +
                         rules[selected].visit_threshold;
          }
        }
      }
    }
    if (better) selected = rule;
  }
  if (selected < 0) {
    throw std::runtime_error("no confidence rule made a nonzero bounded switch");
  }
  return selected;
}

bool heldoutGate(const PolicyMetrics& candidate,
                 const PolicyMetrics& fair_metrics) {
  return candidate.meanRegret() < fair_metrics.meanRegret() &&
         candidate.pairwiseRate() > fair_metrics.pairwiseRate() &&
         candidate.topOneRate() > fair_metrics.topOneRate();
}

struct Decision {
  int action = -1;
  bool override = false;
  bool fallback = false;
  fair::SearchDecision fair_decision;
  mcts::MctsSnapshot mcts_snapshot;
  Evidence evidence;
};

Decision chooseGatedAction(const State& state, const ConfidenceRule& rule) {
  Decision result;
  result.fair_decision = fair::chooseFairAction(mcts::publicState(state));
  if (!result.fair_decision.complete ||
      !isLegal(state.board, result.fair_decision.action)) {
    throw std::runtime_error("fair fallback did not complete");
  }
  result.action = result.fair_decision.action;
  mcts::MctsSearch search(mcts::publicState(state), kMctsHorizon);
  search.runTo(kMctsSimulations);
  result.mcts_snapshot = search.snapshot();
  if (!result.mcts_snapshot.complete ||
      !isLegal(state.board, result.mcts_snapshot.action)) {
    result.fallback = true;
    return result;
  }
  result.evidence = evidenceFor(result.fair_decision,
                                result.mcts_snapshot);
  result.override = accepts(rule, result.evidence);
  if (result.override) result.action = result.mcts_snapshot.action;
  return result;
}

struct Game {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  int cleared = 0;
  int revealed = 0;
  int waves = 0;
  int overrides = 0;
  int fallbacks = 0;
  bool censored = false;
  std::uint64_t fair_work = 0;
  std::uint64_t simulations = 0;
  std::uint64_t simulated_steps = 0;
  std::size_t peak_active_bytes = 0;
  double seconds = 0.0;
};

Game runGame(std::uint32_t seed, bool candidate,
             const ConfidenceRule& rule, std::string_view label) {
  const auto started = Clock::now();
  State state = initialHeadlessState(seed);
  Game result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    int action = -1;
    if (!candidate) {
      const fair::SearchDecision decision =
          fair::chooseFairAction(mcts::publicState(state));
      if (!decision.complete) {
        throw std::runtime_error("fair game decision was incomplete");
      }
      action = decision.action;
      result.fair_work += decision.work;
    } else {
      const Decision decision = chooseGatedAction(state, rule);
      action = decision.action;
      result.fair_work += decision.fair_decision.work;
      result.overrides += decision.override;
      result.fallbacks += decision.fallback;
      result.simulations += decision.mcts_snapshot.simulations;
      result.simulated_steps += decision.mcts_snapshot.tree_steps +
                                decision.mcts_snapshot.rollout_steps;
      result.peak_active_bytes =
          std::max(result.peak_active_bytes,
                   decision.mcts_snapshot.active_bytes);
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("fair MCTS game selected an illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("fair MCTS game transition failed");
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
    const std::lock_guard<std::mutex> lock(confidence_progress_mutex);
    std::cerr << label << " seed 0x" << std::hex << seed << std::dec << ' '
              << result.score << " (" << result.moves << " moves, overrides "
              << result.overrides << ")\n";
  }
  return result;
}

struct Cohort {
  std::vector<Game> fair_games;
  std::vector<Game> candidate_games;
  double wall_seconds = 0.0;
};

Cohort runCohort(std::uint32_t start, int games, int threads,
                 const ConfidenceRule& rule, std::string_view phase) {
  const auto started = Clock::now();
  Cohort result;
  result.fair_games.resize(games);
  result.candidate_games.resize(games);
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
          result.fair_games[game] = runGame(
              seed, false, rule, std::string(phase) + "-fair-d3");
          result.candidate_games[game] = runGame(
              seed, true, rule, std::string(phase) + "-fair-mcts-gated");
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
    throw std::runtime_error("fair MCTS cohort failed: " + error_message);
  }
  result.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

struct GameSummary {
  int games = 0;
  int censored = 0;
  int overrides = 0;
  int fallbacks = 0;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double waves_per_move = 0.0;
  double override_rate = 0.0;
  double simulations_per_move = 0.0;
  double steps_per_second = 0.0;
  double aggregate_seconds = 0.0;
  std::size_t peak_active_bytes = 0;
};

GameSummary summarizeGames(const std::vector<Game>& games) {
  if (games.empty()) throw std::invalid_argument("empty fair MCTS cohort");
  GameSummary result;
  result.games = static_cast<int>(games.size());
  double scores = 0.0;
  double moves = 0.0;
  double clears = 0.0;
  double reveals = 0.0;
  double waves = 0.0;
  double simulations = 0.0;
  double steps = 0.0;
  for (const Game& game : games) {
    scores += game.score;
    moves += game.moves;
    clears += game.cleared;
    reveals += game.revealed;
    waves += game.waves;
    simulations += static_cast<double>(game.simulations);
    steps += static_cast<double>(game.simulated_steps);
    result.overrides += game.overrides;
    result.fallbacks += game.fallbacks;
    result.censored += game.censored;
    result.aggregate_seconds += game.seconds;
    result.peak_active_bytes =
        std::max(result.peak_active_bytes, game.peak_active_bytes);
  }
  result.mean_score = scores / games.size();
  result.mean_moves = moves / games.size();
  result.clears_per_move = clears / moves;
  result.reveals_per_move = reveals / moves;
  result.waves_per_move = waves / moves;
  result.override_rate = result.overrides / moves;
  result.simulations_per_move = simulations / moves;
  result.steps_per_second = steps / result.aggregate_seconds;
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
  if (cohort.fair_games.empty() ||
      cohort.fair_games.size() != cohort.candidate_games.size()) {
    throw std::invalid_argument("fair MCTS games are not paired");
  }
  PairedGames result;
  for (std::size_t game = 0; game < cohort.fair_games.size(); ++game) {
    result.mean_score_delta +=
        cohort.candidate_games[game].score - cohort.fair_games[game].score;
    const int move_delta =
        cohort.candidate_games[game].moves - cohort.fair_games[game].moves;
    result.mean_move_delta += move_delta;
    if (move_delta > 0) ++result.wins;
    else if (move_delta < 0) ++result.losses;
    else ++result.ties;
  }
  result.mean_score_delta /= cohort.fair_games.size();
  result.mean_move_delta /= cohort.fair_games.size();
  return result;
}

bool improvesBoth(const Cohort& cohort) {
  const GameSummary baseline = summarizeGames(cohort.fair_games);
  const GameSummary candidate = summarizeGames(cohort.candidate_games);
  return candidate.mean_score > baseline.mean_score &&
         candidate.mean_moves > baseline.mean_moves;
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

void writePolicyMetrics(std::ostream& output,
                        const PolicyMetrics& metrics) {
  output << "{\"roots\":" << metrics.roots
         << ",\"top1\":" << metrics.topOneRate()
         << ",\"pairwise\":" << metrics.pairwiseRate()
         << ",\"meanRegret\":" << metrics.meanRegret()
         << ",\"switches\":" << metrics.switches
         << ",\"switchRate\":" << metrics.switchRate()
         << ",\"labelTieRoots\":" << metrics.label_tie_roots
         << ",\"tieTop1\":" << metrics.tie_top_one
         << ",\"pairs\":" << metrics.pairs << '}';
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

void writeRoot(std::ostream& output, const RootAudit& root,
               const ConfidenceRule& rule) {
  const bool override = accepts(rule, root.evidence);
  const int action = override ? root.mcts_snapshot.action
                              : root.fair_decision.action;
  const int target = bestLabelAction(root);
  output << "{\"originSeed\":" << root.root.origin_seed
         << ",\"originMove\":" << root.root.origin_move
         << ",\"labelBestAction\":" << target
         << ",\"labelBestCount\":" << labelBestCount(root)
         << ",\"fairAction\":" << root.fair_decision.action
         << ",\"mctsAction\":" << root.mcts_snapshot.action
         << ",\"candidateAction\":" << action
         << ",\"override\":" << (override ? "true" : "false")
         << ",\"qMargin\":" << root.evidence.q_margin
         << ",\"visitMargin\":" << root.evidence.visit_margin
         << ",\"fairRegret\":"
         << root.labels[target] - root.labels[root.fair_decision.action]
         << ",\"candidateRegret\":"
         << root.labels[target] - root.labels[action]
         << ",\"labels\":";
  writeArray(output, root.labels);
  output << ",\"fairQ\":";
  writeArray(output, root.fair_decision.root_values);
  output << ",\"mctsQ\":";
  writeArray(output, root.mcts_snapshot.q);
  output << '}';
}

void writeGameSummary(std::ostream& output, const GameSummary& summary) {
  output << "{\"games\":" << summary.games
         << ",\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"wavesPerMove\":" << summary.waves_per_move
         << ",\"overrides\":" << summary.overrides
         << ",\"overrideRate\":" << summary.override_rate
         << ",\"fallbacks\":" << summary.fallbacks
         << ",\"simulationsPerMove\":" << summary.simulations_per_move
         << ",\"simulatedStepsPerSecond\":" << summary.steps_per_second
         << ",\"peakActiveBytes\":" << summary.peak_active_bytes
         << ",\"aggregateSeconds\":" << summary.aggregate_seconds
         << ",\"censored\":" << summary.censored << '}';
}

void writeCohort(std::ostream& output, const Cohort& cohort) {
  const GameSummary baseline = summarizeGames(cohort.fair_games);
  const GameSummary candidate = summarizeGames(cohort.candidate_games);
  const PairedGames comparison = compareGames(cohort);
  output << "{\"fairD3\":";
  writeGameSummary(output, baseline);
  output << ",\"candidate\":";
  writeGameSummary(output, candidate);
  output << ",\"paired\":{\"meanScoreDelta\":"
         << comparison.mean_score_delta << ",\"meanMoveDelta\":"
         << comparison.mean_move_delta << ",\"wins\":" << comparison.wins
         << ",\"ties\":" << comparison.ties
         << ",\"losses\":" << comparison.losses << "},\"games\":[";
  for (std::size_t game = 0; game < cohort.fair_games.size(); ++game) {
    if (game > 0) output << ',';
    output << "{\"seed\":" << cohort.fair_games[game].seed
           << ",\"fairScore\":" << cohort.fair_games[game].score
           << ",\"candidateScore\":" << cohort.candidate_games[game].score
           << ",\"scoreDelta\":"
           << cohort.candidate_games[game].score -
                  cohort.fair_games[game].score
           << ",\"fairMoves\":" << cohort.fair_games[game].moves
           << ",\"candidateMoves\":" << cohort.candidate_games[game].moves
           << ",\"moveDelta\":"
           << cohort.candidate_games[game].moves -
                  cohort.fair_games[game].moves
           << ",\"overrides\":" << cohort.candidate_games[game].overrides
           << '}';
  }
  output << "],\"wallSeconds\":" << cohort.wall_seconds << '}';
}

bool selfTest(std::ostream& output) {
  std::ostringstream embedded_output;
  const bool fair_self_test = fair::selfTest(embedded_output);
  const bool mcts_self_test = mcts::selfTest(embedded_output);

  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;
  const ConfidenceRule rule{0.05, 0.02};
  const Decision first = chooseGatedAction(state, rule);
  const Decision repeat = chooseGatedAction(state, rule);

  State metadata = state;
  metadata.score = 999'999;
  metadata.level = 88;
  metadata.moves_played = 444;
  const Decision metadata_decision = chooseGatedAction(metadata, rule);
  State mirrored = state;
  mirrored.board = cfpi::detail::mirrorBoard(state.board);
  const Decision reflected = chooseGatedAction(mirrored, rule);

  const bool deterministic = first.action == repeat.action &&
                             first.override == repeat.override &&
                             first.evidence.q_margin ==
                                 repeat.evidence.q_margin &&
                             first.evidence.visit_margin ==
                                 repeat.evidence.visit_margin;
  const bool public_only = metadata_decision.action == first.action &&
                           metadata_decision.override == first.override;
  const bool reflection_safe =
      reflected.action == kBoardSize - 1 - first.action &&
      reflected.override == first.override;
  const bool bounded =
      first.mcts_snapshot.reserved_bytes <= mcts::kMemoryCapBytes &&
      first.mcts_snapshot.arena_full == 0;
  const bool legal = isLegal(state.board, first.action);
  const bool passed = fair_self_test && mcts_self_test && deterministic &&
                      public_only && reflection_safe && bounded && legal;
  output << std::setprecision(10)
         << "{\"passed\":" << (passed ? "true" : "false")
         << ",\"fairSelfTest\":" << (fair_self_test ? "true" : "false")
         << ",\"mctsSelfTest\":" << (mcts_self_test ? "true" : "false")
         << ",\"deterministic\":" << (deterministic ? "true" : "false")
         << ",\"publicOnly\":" << (public_only ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe ? "true" : "false")
         << ",\"bounded\":" << (bounded ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false")
         << ",\"action\":" << first.action
         << ",\"override\":" << (first.override ? "true" : "false")
         << ",\"qMargin\":" << first.evidence.q_margin
         << ",\"visitMargin\":" << first.evidence.visit_margin
         << ",\"reservedBytes\":" << first.mcts_snapshot.reserved_bytes
         << "}\n";
  return passed;
}

struct Options {
  int threads = kDefaultThreads;
  std::string output = "/tmp/drop7-fair-mcts-confidence.json";
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
      throw std::invalid_argument("unknown fair MCTS confidence option");
    }
  }
  if (result.threads < 1 || result.threads > 16) {
    throw std::invalid_argument("fair MCTS threads must be from 1 to 16");
  }
  return result;
}

int run(int argc, char** argv) {
  const auto started = Clock::now();
  const Options options = parseOptions(argc, argv);
  const std::vector<RootCase> fitting_roots =
      collectFairRoots(kFittingStart, kFittingGames, "fitting");
  const std::vector<RootCase> heldout_roots =
      collectFairRoots(kHeldoutStart, kHeldoutGames, "heldout");
  if (static_cast<int>(fitting_roots.size()) != kFittingRoots ||
      static_cast<int>(heldout_roots.size()) != kHeldoutRoots) {
    throw std::runtime_error("fair MCTS root corpus size changed");
  }

  const std::vector<RootAudit> fitting =
      parallelAudit(fitting_roots, options.threads, "fitting");
  const PolicyMetrics fitting_fair = fairMetrics(fitting);
  const PolicyMetrics fitting_raw = rawMctsMetrics(fitting);
  const auto rules = confidenceRules();
  std::array<PolicyMetrics, kRuleCount> fitting_rules;
  for (int rule = 0; rule < kRuleCount; ++rule) {
    fitting_rules[rule] = gatedMetrics(fitting, rules[rule]);
  }
  const int selected_index = selectRule(rules, fitting_rules, fitting_raw);
  const ConfidenceRule selected_rule = rules[selected_index];
  const PolicyMetrics selected_fitting = fitting_rules[selected_index];
  const bool fitting_target =
      selected_fitting.meanRegret() < fitting_fair.meanRegret() &&
      selected_fitting.switches <= static_cast<int>(std::floor(
          fitting_raw.switches * kMaximumFittingSwitchFractionOfRaw));

  const std::vector<RootAudit> heldout =
      parallelAudit(heldout_roots, options.threads, "heldout");
  const PolicyMetrics heldout_fair = fairMetrics(heldout);
  const PolicyMetrics heldout_raw = rawMctsMetrics(heldout);
  const PolicyMetrics heldout_candidate =
      gatedMetrics(heldout, selected_rule);
  const bool ranking_gate = heldoutGate(heldout_candidate, heldout_fair);

  Cohort screen;
  bool screen_passed = false;
  if (ranking_gate) {
    screen = runCohort(kScreenStart, kScreenGames, options.threads,
                       selected_rule, "screen");
    screen_passed = improvesBoth(screen);
  }
  Cohort confirmation;
  bool confirmation_passed = false;
  if (screen_passed) {
    confirmation = runCohort(
        kConfirmationStart, kConfirmationGames, options.threads,
        selected_rule, "confirmation");
    confirmation_passed = improvesBoth(confirmation);
  }

  std::ofstream output(options.output);
  if (!output) {
    throw std::runtime_error("could not write fair MCTS artifact");
  }
  output << std::setprecision(10)
         << "{\n  \"format\":\"drop7-fair-mcts-confidence-v1\",\n"
         << "  \"mechanics\":{\"levelBonus\":" << kLevelBonus << "},\n"
         << "  \"publicStateOnly\":true,\n"
         << "  \"baseline\":\"confirmed-fair-only-full-width-d3-s5\",\n"
         << "  \"mcts\":{\"simulations\":" << kMctsSimulations
         << ",\"horizon\":" << kMctsHorizon
         << ",\"nodeKey\":\"canonical-public-state-plus-search-horizon\","
            "\"futureTapeInPolicy\":false,\"arenaReservedBytes\":"
         << mcts::Arena{}.reservedBytes()
         << ",\"memoryCapBytes\":" << mcts::kMemoryCapBytes << "},\n"
         << "  \"rootCorpus\":{\"fittingStart\":" << kFittingStart
         << ",\"fittingGames\":" << kFittingGames
         << ",\"fittingRoots\":" << kFittingRoots
         << ",\"heldoutStart\":" << kHeldoutStart
         << ",\"heldoutGames\":" << kHeldoutGames
         << ",\"heldoutRoots\":" << kHeldoutRoots
         << ",\"rootMoves\":[" << kRootMoves[0] << ',' << kRootMoves[1]
         << "],\"fairD3Trajectories\":true,"
            "\"originGameDisjoint\":true},\n"
         << "  \"labels\":{\"alignedTapesPerSibling\":"
         << kLabelScenarios << ",\"horizon\":" << kLabelHorizon
         << ",\"continuation\":\"public-fair-d1-s1\","
            "\"independentOfOriginFuture\":true,"
            "\"terminalUtility\":" << kLabelTerminalUtility << "},\n"
         << "  \"selection\":{\"rule\":"
            "\"override iff raw-switch and qMargin>=threshold and visitShareMargin>=threshold\","
            "\"fittingOnlyGridExpansion\":\"initial q<=.20 and visit<=.10 grid had zero nonzero rules at <=half raw switches; widened before heldout\","
            "\"maximumSwitchFractionOfRaw\":"
         << kMaximumFittingSwitchFractionOfRaw
         << ",\"objective\":\"minimum-fitting-regret then pairwise,top1,fewer-switches,stricter-rule\","
            "\"selectedQThreshold\":" << selected_rule.q_threshold
         << ",\"selectedVisitThreshold\":"
         << selected_rule.visit_threshold << "},\n"
         << "  \"fittingFair\":";
  writePolicyMetrics(output, fitting_fair);
  output << ",\n  \"fittingRawMcts\":";
  writePolicyMetrics(output, fitting_raw);
  output << ",\n  \"fittingRules\":[";
  for (int rule = 0; rule < kRuleCount; ++rule) {
    if (rule > 0) output << ',';
    output << "{\"qThreshold\":" << rules[rule].q_threshold
           << ",\"visitThreshold\":" << rules[rule].visit_threshold
           << ",\"selected\":"
           << (rule == selected_index ? "true" : "false")
           << ",\"metrics\":";
    writePolicyMetrics(output, fitting_rules[rule]);
    output << '}';
  }
  output << "],\n  \"fittingTargetPassed\":"
         << (fitting_target ? "true" : "false")
         << ",\n  \"heldoutFair\":";
  writePolicyMetrics(output, heldout_fair);
  output << ",\n  \"heldoutRawMcts\":";
  writePolicyMetrics(output, heldout_raw);
  output << ",\n  \"heldoutCandidate\":";
  writePolicyMetrics(output, heldout_candidate);
  output << ",\n  \"heldoutGate\":{\"requiresLowerRegret\":true,"
            "\"requiresHigherPairwise\":true,"
            "\"requiresHigherTieAwareTop1\":true,\"passed\":"
         << (ranking_gate ? "true" : "false") << "},\n"
         << "  \"heldoutRoots\":[";
  for (std::size_t root = 0; root < heldout.size(); ++root) {
    if (root > 0) output << ',';
    writeRoot(output, heldout[root], selected_rule);
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
            << "FAIR_MCTS_CONFIDENCE {\"qThreshold\":"
            << selected_rule.q_threshold << ",\"visitThreshold\":"
            << selected_rule.visit_threshold << ",\"fittingFairRegret\":"
            << fitting_fair.meanRegret() << ",\"fittingCandidateRegret\":"
            << selected_fitting.meanRegret() << ",\"fittingRawSwitches\":"
            << fitting_raw.switches << ",\"fittingCandidateSwitches\":"
            << selected_fitting.switches << ",\"heldoutFairTop1\":"
            << heldout_fair.topOneRate() << ",\"heldoutCandidateTop1\":"
            << heldout_candidate.topOneRate()
            << ",\"heldoutFairPairwise\":" << heldout_fair.pairwiseRate()
            << ",\"heldoutCandidatePairwise\":"
            << heldout_candidate.pairwiseRate()
            << ",\"heldoutFairRegret\":" << heldout_fair.meanRegret()
            << ",\"heldoutCandidateRegret\":"
            << heldout_candidate.meanRegret() << ",\"rankingGate\":"
            << (ranking_gate ? "true" : "false")
            << ",\"screenPassed\":" << (screen_passed ? "true" : "false")
            << ",\"confirmationPassed\":"
            << (confirmation_passed ? "true" : "false")
            << ",\"peakRssBytes\":" << peakRssBytes() << "}\n";
  return EXIT_SUCCESS;
}

}  // namespace drop7::fair_mcts_confidence

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::fair_mcts_confidence::selfTest(std::cout) ? EXIT_SUCCESS
                                                              : EXIT_FAILURE;
    }
    return drop7::fair_mcts_confidence::run(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "drop7_fair_mcts_confidence: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
