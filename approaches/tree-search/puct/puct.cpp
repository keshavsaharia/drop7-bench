#include "../../../src/core/native/public-behavior.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;
using drop7::MoveResult;
using drop7::State;

constexpr std::uint32_t kScreenTrainingSeedStart = 0x3d70'1200u;
constexpr std::uint32_t kConfirmTrainingSeedStart = 0x3d70'1300u;
constexpr std::uint32_t kSimulationDomain = 0x5055'4354u;  // "PUCT"
constexpr std::uint32_t kRevealDomain = 0x5245'564cu;
constexpr std::uint32_t kDiscDomain = 0x4449'5343u;
constexpr std::uint32_t kSimulationMultiplier = 0x9e37'79b9u;
constexpr std::uint32_t kDepthMultiplier = 0x85eb'ca6bu;
constexpr std::uint32_t kEventMultiplier = 0xc2b2'ae35u;
constexpr double kValueScale = 100'000.0;
constexpr double kPriorTemperature = 0.75;
constexpr int kPriorSamples = 3;

struct PuctConfig {
  std::string_view name;
  int simulations = 128;
  int horizon = 8;
  double exploration = 1.25;
  int maximum_nodes = 4096;
};

constexpr PuctConfig kBestConfig{
    "puct256-h8-c1.25", 256, 8, 1.25, 4096,
};

struct GateProfile {
  std::string_view name;
  double visit_share = 0.65;
  double value_margin = 0.10;
  int minimum_visits = 32;
};

constexpr std::array<GateProfile, 4> kGates{{
    {"gate65-margin10", 0.65, 0.10, 32},
    {"gate65-margin25", 0.65, 0.25, 32},
    {"gate80-margin10", 0.80, 0.10, 32},
    {"gate80-margin25", 0.80, 0.25, 32},
}};

struct Options {
  int screen_games = 8;
  int confirm_games = 8;
  int maximum_moves = 500;
  drop7::cfpi::BehaviorOptions behavior;
};

struct ActionStats {
  double prior = 0;
  double value_sum = 0;
  int visits = 0;
  bool legal = false;
};

struct Node {
  State state;
  int remaining = 0;
  int visits = 0;
  std::array<ActionStats, drop7::kBoardSize> actions{};
};

struct SearchStats {
  int action = -1;
  int challenger_visits = 0;
  double challenger_visit_share = 0;
  double challenger_mean = -std::numeric_limits<double>::infinity();
  double baseline_mean = -std::numeric_limits<double>::infinity();
  double normalized_advantage = -std::numeric_limits<double>::infinity();
  int nodes = 0;
  int peak_nodes = 0;
  int simulations = 0;
  std::uint64_t simulated_steps = 0;
  std::uint64_t prior_work = 0;
  std::uint64_t transposition_hits = 0;
  std::uint64_t arena_full = 0;
  double seconds = 0;
};

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  int proposed_switches = 0;
  int accepted_switches = 0;
  std::int64_t accepted_immediate_score = 0;
  double proposed_visit_share_sum = 0;
  double proposed_advantage_sum = 0;
  double accepted_visit_share_sum = 0;
  double accepted_advantage_sum = 0;
  int cleared = 0;
  int revealed = 0;
  bool terminal = false;
  int peak_nodes = 0;
  std::uint64_t exact_work = 0;
  std::uint64_t simulations = 0;
  std::uint64_t simulated_steps = 0;
  std::uint64_t prior_work = 0;
  std::uint64_t transposition_hits = 0;
  std::uint64_t arena_full = 0;
  double search_seconds = 0;
};

struct Summary {
  std::string_view name;
  PuctConfig config;
  GateProfile gate;
  bool baseline = false;
  std::vector<GameResult> games;
  double mean_score = 0;
  double mean_moves = 0;
  double mean_score_difference = 0;
  double mean_move_difference = 0;
  int proposed_switches = 0;
  int accepted_switches = 0;
  int abstained_switches = 0;
  double proposed_switch_rate = 0;
  double accepted_switch_rate = 0;
  double mean_proposed_visit_share = 0;
  double mean_proposed_advantage = 0;
  double mean_accepted_visit_share = 0;
  double mean_accepted_advantage = 0;
  std::int64_t accepted_immediate_score = 0;
  int peak_nodes = 0;
  std::uint64_t exact_work = 0;
  std::uint64_t simulations = 0;
  std::uint64_t simulated_steps = 0;
  std::uint64_t prior_work = 0;
  std::uint64_t transposition_hits = 0;
  std::uint64_t arena_full = 0;
  double search_seconds = 0;
  double simulation_steps_per_second = 0;
  double clear_per_move = 0;
  double reveal_per_move = 0;
  int total_moves = 0;
  int total_cleared = 0;
  int total_revealed = 0;
};

std::uint32_t observableHash(const State& state) {
  std::uint32_t hash = 0x811c'9dc5u;
  for (std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint32_t>(cell + 1u);
    hash *= 0x0100'0193u;
  }
  hash ^= static_cast<std::uint32_t>(state.next_disc);
  hash *= 0x0100'0193u;
  hash ^= static_cast<std::uint32_t>(state.moves_remaining);
  return drop7::mix32(hash ^ kSimulationDomain);
}

std::uint32_t simulationBits(std::uint32_t root_hash, int simulation,
                             int depth, int event,
                             std::uint32_t domain) {
  return drop7::mix32(
      root_hash ^ domain ^
      (static_cast<std::uint32_t>(simulation + 1) *
       kSimulationMultiplier) ^
      (static_cast<std::uint32_t>(depth + 1) * kDepthMultiplier) ^
      (static_cast<std::uint32_t>(event + 1) * kEventMultiplier));
}

std::uint8_t bitsToDisc(std::uint32_t bits) {
  return static_cast<std::uint8_t>(
      ((static_cast<std::uint64_t>(bits) * drop7::kBoardSize) >> 32) + 1u);
}

struct SimulationRandom {
  std::uint32_t root_hash = 0;
  int simulation = 0;
  int depth = 0;
  int event = 0;

  std::uint8_t nextDisc() {
    return bitsToDisc(simulationBits(
        root_hash, simulation, depth, event++, kRevealDomain));
  }
};

std::uint8_t simulationNextDisc(std::uint32_t root_hash, int simulation,
                                int depth) {
  return bitsToDisc(simulationBits(
      root_hash, simulation, depth, 0, kDiscDomain));
}

std::string nodeKey(const State& state, int remaining) {
  std::string key =
      drop7::cfpi::detail::dynamicStateKey(state, 0);
  key.back() = static_cast<char>(remaining);
  return key;
}

double normalizedLeafValue(const State& state) {
  if (state.game_over) return -10.0;
  return std::clamp(
      drop7::cfpi::phasePotential(state) / kValueScale, -10.0, 5.0);
}

double priorActionValue(const State& state, int column,
                        std::uint64_t& prior_work) {
  const std::uint32_t seed =
      drop7::cfpi::detail::scenarioSeedForState(
          state, 0xd707'5eedu, 1);
  double total = 0;
  for (int sample = 0; sample < kPriorSamples; ++sample) {
    drop7::cfpi::detail::StratifiedRandom random{
        seed, sample, kPriorSamples, 0,
    };
    MoveResult move;
    if (!drop7::cfpi::detail::playMoveSampled(
            state, column, random, move)) {
      total -= 10.0;
      continue;
    }
    ++prior_work;
    const double reward =
        static_cast<double>(move.score_delta) / kValueScale;
    if (move.state.game_over) {
      total += reward - 10.0;
      continue;
    }
    move.state.score = 0;
    move.state.next_disc =
        drop7::cfpi::detail::sampledNextDisc(
            seed, sample, kPriorSamples);
    total += reward + normalizedLeafValue(move.state);
  }
  return total / kPriorSamples;
}

void initializePriors(Node& node, std::uint64_t& prior_work) {
  std::array<double, drop7::kBoardSize> logits{};
  double maximum = -std::numeric_limits<double>::infinity();
  int legal_count = 0;
  for (int column : drop7::cfpi::detail::kColumnOrder) {
    if (!drop7::isLegal(node.state.board, column)) continue;
    node.actions[column].legal = true;
    logits[column] =
        priorActionValue(node.state, column, prior_work);
    maximum = std::max(maximum, logits[column]);
    ++legal_count;
  }
  if (legal_count == 0) return;
  double total = 0;
  for (int column = 0; column < drop7::kBoardSize; ++column) {
    if (!node.actions[column].legal) continue;
    const double exponent =
        std::clamp((logits[column] - maximum) / kPriorTemperature,
                   -40.0, 0.0);
    node.actions[column].prior = std::exp(exponent);
    total += node.actions[column].prior;
  }
  if (!(total > 0) || !std::isfinite(total)) {
    for (ActionStats& action : node.actions) {
      if (action.legal) action.prior = 1.0 / legal_count;
    }
    return;
  }
  for (ActionStats& action : node.actions) {
    if (action.legal) action.prior /= total;
  }
}

int selectPuctAction(const Node& node, double exploration) {
  int selected = -1;
  double best = -std::numeric_limits<double>::infinity();
  const double root = std::sqrt(static_cast<double>(node.visits + 1));
  for (int column : drop7::cfpi::detail::kColumnOrder) {
    const ActionStats& action = node.actions[column];
    if (!action.legal) continue;
    const double mean =
        action.visits > 0 ? action.value_sum / action.visits : 0;
    const double bonus =
        exploration * action.prior * root / (action.visits + 1);
    const double value = mean + bonus;
    if (value > best) {
      best = value;
      selected = column;
    }
  }
  return selected;
}

int recommendedAction(const Node& root) {
  int selected = -1;
  int best_visits = -1;
  double best_mean = -std::numeric_limits<double>::infinity();
  for (int column : drop7::cfpi::detail::kColumnOrder) {
    const ActionStats& action = root.actions[column];
    if (!action.legal) continue;
    const double mean =
        action.visits > 0 ? action.value_sum / action.visits
                          : -std::numeric_limits<double>::infinity();
    if (action.visits > best_visits ||
        (action.visits == best_visits && mean > best_mean)) {
      best_visits = action.visits;
      best_mean = mean;
      selected = column;
    }
  }
  return selected;
}

struct PathEntry {
  int node = -1;
  int action = -1;
  double reward = 0;
};

SearchStats runPuct(const State& input, const PuctConfig& config,
                    int baseline_action = -1) {
  const auto started = Clock::now();
  bool mirrored = false;
  State root_state =
      drop7::cfpi::detail::canonicalState(input, mirrored);
  root_state.score = 0;
  const int canonical_baseline =
      mirrored && baseline_action >= 0
          ? drop7::kBoardSize - 1 - baseline_action
          : baseline_action;
  const std::uint32_t root_hash = observableHash(root_state);

  std::vector<Node> arena;
  arena.reserve(static_cast<std::size_t>(config.maximum_nodes));
  std::unordered_map<std::string, int> transpositions;
  transpositions.reserve(static_cast<std::size_t>(
      config.maximum_nodes * 4 / 3 + 1));
  std::uint64_t prior_work = 0;
  arena.push_back({root_state, config.horizon, 0, {}});
  initializePriors(arena.front(), prior_work);
  transpositions.emplace(nodeKey(root_state, config.horizon), 0);

  SearchStats stats;
  stats.simulations = config.simulations;
  std::vector<PathEntry> path;
  path.reserve(static_cast<std::size_t>(config.horizon));
  for (int simulation = 0; simulation < config.simulations; ++simulation) {
    path.clear();
    int node_index = 0;
    double leaf_value = 0;
    for (int depth = 0; depth < config.horizon; ++depth) {
      Node& node = arena[static_cast<std::size_t>(node_index)];
      const int column =
          selectPuctAction(node, config.exploration);
      if (column < 0) {
        leaf_value = -10.0;
        break;
      }
      SimulationRandom random{root_hash, simulation, depth, 0};
      MoveResult move;
      if (!drop7::cfpi::detail::playMoveSampled(
              node.state, column, random, move)) {
        throw std::runtime_error("PUCT selected illegal tree action");
      }
      ++stats.simulated_steps;
      const double reward =
          static_cast<double>(move.score_delta) / kValueScale;
      path.push_back({node_index, column, reward});
      if (move.state.game_over) {
        leaf_value = -10.0;
        break;
      }

      move.state.score = 0;
      move.state.next_disc =
          simulationNextDisc(root_hash, simulation, depth);
      bool ignored = false;
      State next =
          drop7::cfpi::detail::canonicalState(move.state, ignored);
      const int remaining = config.horizon - depth - 1;
      if (remaining == 0) {
        leaf_value = normalizedLeafValue(next);
        break;
      }
      const std::string key = nodeKey(next, remaining);
      const auto found = transpositions.find(key);
      if (found != transpositions.end()) {
        ++stats.transposition_hits;
        node_index = found->second;
        continue;
      }
      if (static_cast<int>(arena.size()) >= config.maximum_nodes) {
        ++stats.arena_full;
        leaf_value = normalizedLeafValue(next);
        break;
      }
      const int child = static_cast<int>(arena.size());
      arena.push_back({next, remaining, 0, {}});
      initializePriors(arena.back(), prior_work);
      transpositions.emplace(key, child);
      leaf_value = normalizedLeafValue(next);
      break;
    }

    double value = leaf_value;
    for (auto entry = path.rbegin(); entry != path.rend(); ++entry) {
      value += entry->reward;
      Node& node = arena[static_cast<std::size_t>(entry->node)];
      ActionStats& action = node.actions[entry->action];
      ++action.visits;
      action.value_sum += value;
      ++node.visits;
    }
  }
  const Node& root = arena.front();
  int action = recommendedAction(root);
  if (action >= 0) {
    const ActionStats& challenger = root.actions[action];
    stats.challenger_visits = challenger.visits;
    stats.challenger_visit_share =
        root.visits > 0
            ? static_cast<double>(challenger.visits) / root.visits
            : 0;
    if (challenger.visits > 0) {
      stats.challenger_mean =
          challenger.value_sum / challenger.visits;
    }
    if (canonical_baseline >= 0 &&
        canonical_baseline < drop7::kBoardSize) {
      const ActionStats& baseline = root.actions[canonical_baseline];
      if (baseline.visits > 0) {
        stats.baseline_mean = baseline.value_sum / baseline.visits;
        stats.normalized_advantage =
            stats.challenger_mean - stats.baseline_mean;
      }
    }
  }
  if (mirrored && action >= 0) action = drop7::kBoardSize - 1 - action;
  stats.action = action;
  stats.nodes = static_cast<int>(arena.size());
  stats.peak_nodes = stats.nodes;
  stats.prior_work = prior_work;
  stats.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return stats;
}

GameResult runBaselineGame(std::uint32_t seed, const Options& options) {
  State state = drop7::initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < options.maximum_moves) {
    drop7::cfpi::BehaviorMetrics metrics;
    const int action = drop7::cfpi::chooseBehaviorAction(
        state, options.behavior, &metrics);
    result.exact_work += metrics.work;
    MoveResult move;
    if (!drop7::playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("exact baseline selected illegal action");
    }
    for (const drop7::Wave& wave : move.waves) {
      result.cleared += wave.cleared;
      result.revealed += wave.revealed;
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.terminal = state.game_over;
  return result;
}

bool acceptsSwitch(const SearchStats& search, int baseline,
                   const GateProfile& gate) {
  return search.action >= 0 && search.action != baseline &&
         search.challenger_visits >= gate.minimum_visits &&
         search.challenger_visit_share > gate.visit_share &&
         search.normalized_advantage > gate.value_margin;
}

GameResult runPuctGame(std::uint32_t seed, const GateProfile& gate,
                       const Options& options) {
  State state = drop7::initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < options.maximum_moves) {
    drop7::cfpi::BehaviorMetrics metrics;
    const int baseline = drop7::cfpi::chooseBehaviorAction(
        state, options.behavior, &metrics);
    result.exact_work += metrics.work;
    const SearchStats search = runPuct(state, kBestConfig, baseline);
    const bool proposed =
        search.action >= 0 && search.action != baseline;
    const bool accepted = acceptsSwitch(search, baseline, gate);
    if (proposed) {
      ++result.proposed_switches;
      result.proposed_visit_share_sum +=
          search.challenger_visit_share;
      result.proposed_advantage_sum +=
          search.normalized_advantage;
    }
    if (accepted) {
      ++result.accepted_switches;
      result.accepted_visit_share_sum +=
          search.challenger_visit_share;
      result.accepted_advantage_sum +=
          search.normalized_advantage;
    }
    const int action = accepted ? search.action : baseline;
    result.peak_nodes = std::max(result.peak_nodes, search.peak_nodes);
    result.simulations +=
        static_cast<std::uint64_t>(search.simulations);
    result.simulated_steps += search.simulated_steps;
    result.prior_work += search.prior_work;
    result.transposition_hits += search.transposition_hits;
    result.arena_full += search.arena_full;
    result.search_seconds += search.seconds;
    MoveResult move;
    if (!drop7::playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("PUCT policy selected illegal action");
    }
    if (accepted) {
      result.accepted_immediate_score += move.score_delta;
    }
    for (const drop7::Wave& wave : move.waves) {
      result.cleared += wave.cleared;
      result.revealed += wave.revealed;
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.terminal = state.game_over;
  return result;
}

Summary summarize(std::string_view name, const PuctConfig& config,
                  const GateProfile& gate, bool baseline,
                  std::vector<GameResult> games,
                  const Summary* control) {
  Summary result;
  result.name = name;
  result.config = config;
  result.gate = gate;
  result.baseline = baseline;
  result.games = std::move(games);
  for (const GameResult& game : result.games) {
    result.mean_score += static_cast<double>(game.score);
    result.mean_moves += game.moves;
    result.proposed_switches += game.proposed_switches;
    result.accepted_switches += game.accepted_switches;
    result.accepted_immediate_score +=
        game.accepted_immediate_score;
    result.mean_proposed_visit_share +=
        game.proposed_visit_share_sum;
    result.mean_proposed_advantage +=
        game.proposed_advantage_sum;
    result.mean_accepted_visit_share +=
        game.accepted_visit_share_sum;
    result.mean_accepted_advantage +=
        game.accepted_advantage_sum;
    result.peak_nodes = std::max(result.peak_nodes, game.peak_nodes);
    result.exact_work += game.exact_work;
    result.simulations += game.simulations;
    result.simulated_steps += game.simulated_steps;
    result.prior_work += game.prior_work;
    result.transposition_hits += game.transposition_hits;
    result.arena_full += game.arena_full;
    result.search_seconds += game.search_seconds;
    result.total_moves += game.moves;
    result.total_cleared += game.cleared;
    result.total_revealed += game.revealed;
  }
  if (!result.games.empty()) {
    result.mean_score /= static_cast<double>(result.games.size());
    result.mean_moves /= static_cast<double>(result.games.size());
  }
  if (result.total_moves > 0) {
    result.proposed_switch_rate =
        static_cast<double>(result.proposed_switches) /
        result.total_moves;
    result.accepted_switch_rate =
        static_cast<double>(result.accepted_switches) /
        result.total_moves;
    result.clear_per_move =
        static_cast<double>(result.total_cleared) / result.total_moves;
    result.reveal_per_move =
        static_cast<double>(result.total_revealed) / result.total_moves;
  }
  result.abstained_switches =
      result.proposed_switches - result.accepted_switches;
  if (result.proposed_switches > 0) {
    result.mean_proposed_visit_share /=
        result.proposed_switches;
    result.mean_proposed_advantage /=
        result.proposed_switches;
  }
  if (result.accepted_switches > 0) {
    result.mean_accepted_visit_share /=
        result.accepted_switches;
    result.mean_accepted_advantage /=
        result.accepted_switches;
  }
  if (result.search_seconds > 0) {
    result.simulation_steps_per_second =
        result.simulated_steps / result.search_seconds;
  }
  if (control != nullptr) {
    if (control->games.size() != result.games.size()) {
      throw std::runtime_error("paired game counts differ");
    }
    double score_difference = 0;
    double move_difference = 0;
    for (std::size_t index = 0; index < result.games.size(); ++index) {
      score_difference += static_cast<double>(
          result.games[index].score - control->games[index].score);
      move_difference +=
          result.games[index].moves - control->games[index].moves;
    }
    result.mean_score_difference =
        score_difference / static_cast<double>(result.games.size());
    result.mean_move_difference =
        move_difference / static_cast<double>(result.games.size());
  }
  return result;
}

std::vector<Summary> runScreen(std::uint32_t seed_start, int games,
                               const std::vector<GateProfile>& gates,
                               const Options& options) {
  std::vector<GameResult> baseline_games;
  std::vector<std::vector<GameResult>> puct_games(gates.size());
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed =
        seed_start + static_cast<std::uint32_t>(game);
    auto baseline = std::async(std::launch::async, [&, seed] {
      return runBaselineGame(seed, options);
    });
    std::vector<std::future<GameResult>> pending;
    for (const GateProfile& gate : gates) {
      pending.push_back(std::async(std::launch::async, [&, seed, gate] {
        return runPuctGame(seed, gate, options);
      }));
    }
    baseline_games.push_back(baseline.get());
    for (std::size_t index = 0; index < pending.size(); ++index) {
      puct_games[index].push_back(pending[index].get());
    }
  }
  std::vector<Summary> summaries;
  summaries.push_back(summarize(
      "exact-d3-s5", PuctConfig{}, GateProfile{}, true,
      std::move(baseline_games), nullptr));
  for (std::size_t index = 0; index < gates.size(); ++index) {
    summaries.push_back(summarize(
        gates[index].name, kBestConfig, gates[index], false,
        std::move(puct_games[index]), &summaries.front()));
  }
  return summaries;
}

int selectWinner(const std::vector<Summary>& summaries) {
  int winner = -1;
  for (int index = 1; index < static_cast<int>(summaries.size()); ++index) {
    const Summary& candidate = summaries[static_cast<std::size_t>(index)];
    if (candidate.mean_score_difference <= 0 ||
        candidate.mean_move_difference <= 0) {
      continue;
    }
    if (winner < 0 ||
        candidate.mean_score_difference >
            summaries[static_cast<std::size_t>(winner)]
                .mean_score_difference ||
        (candidate.mean_score_difference ==
             summaries[static_cast<std::size_t>(winner)]
                 .mean_score_difference &&
         candidate.mean_move_difference >
             summaries[static_cast<std::size_t>(winner)]
                 .mean_move_difference)) {
      winner = index;
    }
  }
  return winner;
}

double maximumResidentMiB() {
#if defined(__APPLE__) || defined(__linux__)
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(__APPLE__)
    return usage.ru_maxrss / (1024.0 * 1024.0);
#else
    return usage.ru_maxrss / 1024.0;
#endif
  }
#endif
  return 0;
}

void printSummary(const Summary& summary) {
  std::cout << "{\"name\":\"" << summary.name
            << "\",\"baseline\":"
            << (summary.baseline ? "true" : "false")
            << ",\"simulations_per_move\":"
            << (summary.baseline ? 0 : summary.config.simulations)
            << ",\"horizon\":"
            << (summary.baseline ? 0 : summary.config.horizon)
            << ",\"exploration\":"
            << (summary.baseline ? 0 : summary.config.exploration)
            << ",\"node_cap\":"
            << (summary.baseline ? 0 : summary.config.maximum_nodes)
            << ",\"visit_share_gate\":"
            << (summary.baseline ? 0 : summary.gate.visit_share)
            << ",\"value_margin_gate\":"
            << (summary.baseline ? 0 : summary.gate.value_margin)
            << ",\"minimum_challenger_visits\":"
            << (summary.baseline ? 0 : summary.gate.minimum_visits)
            << ",\"mean_score\":" << summary.mean_score
            << ",\"mean_moves\":" << summary.mean_moves
            << ",\"paired_mean_score_difference\":"
            << summary.mean_score_difference
            << ",\"paired_mean_move_difference\":"
            << summary.mean_move_difference
            << ",\"proposed_switches\":"
            << summary.proposed_switches
            << ",\"accepted_switches\":"
            << summary.accepted_switches
            << ",\"abstained_switches\":"
            << summary.abstained_switches
            << ",\"proposed_switch_rate\":"
            << summary.proposed_switch_rate
            << ",\"accepted_switch_rate\":"
            << summary.accepted_switch_rate
            << ",\"mean_proposed_visit_share\":"
            << summary.mean_proposed_visit_share
            << ",\"mean_proposed_advantage\":"
            << summary.mean_proposed_advantage
            << ",\"mean_accepted_visit_share\":"
            << summary.mean_accepted_visit_share
            << ",\"mean_accepted_advantage\":"
            << summary.mean_accepted_advantage
            << ",\"accepted_immediate_score\":"
            << summary.accepted_immediate_score
            << ",\"peak_nodes\":" << summary.peak_nodes
            << ",\"exact_work\":" << summary.exact_work
            << ",\"simulations\":" << summary.simulations
            << ",\"simulated_steps\":" << summary.simulated_steps
            << ",\"prior_work\":" << summary.prior_work
            << ",\"transposition_hits\":"
            << summary.transposition_hits
            << ",\"arena_full\":" << summary.arena_full
            << ",\"search_seconds\":" << summary.search_seconds
            << ",\"simulation_steps_per_second\":"
            << summary.simulation_steps_per_second
            << ",\"clear_per_move\":" << summary.clear_per_move
            << ",\"reveal_per_move\":" << summary.reveal_per_move
            << ",\"scores\":[";
  for (std::size_t index = 0; index < summary.games.size(); ++index) {
    if (index > 0) std::cout << ',';
    std::cout << summary.games[index].score;
  }
  std::cout << "],\"moves\":[";
  for (std::size_t index = 0; index < summary.games.size(); ++index) {
    if (index > 0) std::cout << ',';
    std::cout << summary.games[index].moves;
  }
  std::cout << "]}";
}

bool selfTest(std::ostream& output) {
  PuctConfig quick{"self-test", 32, 4, 1.25, 128};
  State state;
  state.board = drop7::initialBoard();
  state.board[drop7::indexOf(5, 0)] = 3;
  state.board[drop7::indexOf(5, 1)] = 5;
  state.board[drop7::indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;
  drop7::cfpi::BehaviorOptions behavior;
  const int baseline =
      drop7::cfpi::chooseBehaviorAction(state, behavior);
  const SearchStats first = runPuct(state, quick, baseline);
  const SearchStats second = runPuct(state, quick, baseline);
  const bool deterministic =
      first.action == second.action && first.nodes == second.nodes &&
      first.simulated_steps == second.simulated_steps;
  const bool legal = drop7::isLegal(state.board, first.action);
  const bool bounded =
      first.nodes <= quick.maximum_nodes &&
      first.simulations == quick.simulations;

  State mirrored = state;
  mirrored.board =
      drop7::cfpi::detail::mirrorBoard(state.board);
  const SearchStats reflection = runPuct(
      mirrored, quick, drop7::kBoardSize - 1 - baseline);
  const bool mirror_safe =
      reflection.action ==
      drop7::kBoardSize - 1 - first.action;

  State same_observable = state;
  same_observable.score = 987'654;
  same_observable.moves_played = 321;
  same_observable.level = 77;
  const SearchStats history_free =
      runPuct(same_observable, quick, baseline);
  const bool seed_blind =
      history_free.action == first.action &&
      history_free.nodes == first.nodes &&
      history_free.simulated_steps == first.simulated_steps;
  const bool disc_range =
      simulationNextDisc(observableHash(state), 0, 0) >= 1 &&
      simulationNextDisc(observableHash(state), 0, 0) <= 7;
  SearchStats synthetic;
  synthetic.action = baseline == 0 ? 1 : 0;
  synthetic.challenger_visits = 40;
  synthetic.challenger_visit_share = 0.81;
  synthetic.normalized_advantage = 0.26;
  const bool gate_accepts =
      acceptsSwitch(synthetic, baseline, kGates.back());
  synthetic.challenger_visits = 31;
  const bool minimum_rejects =
      !acceptsSwitch(synthetic, baseline, kGates.back());
  synthetic.challenger_visits = 40;
  synthetic.challenger_visit_share = 0.80;
  const bool boundary_rejects =
      !acceptsSwitch(synthetic, baseline, kGates.back());
  const bool gate_logic =
      gate_accepts && minimum_rejects && boundary_rejects;
  const bool passed = deterministic && legal && bounded && mirror_safe &&
                      seed_blind && disc_range && gate_logic;
  output << "{\"deterministic\":"
         << (deterministic ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false")
         << ",\"bounded\":" << (bounded ? "true" : "false")
         << ",\"mirror_safe\":"
         << (mirror_safe ? "true" : "false")
         << ",\"seed_blind\":"
         << (seed_blind ? "true" : "false")
         << ",\"disc_range\":"
         << (disc_range ? "true" : "false")
         << ",\"gate_logic\":"
         << (gate_logic ? "true" : "false")
         << ",\"nodes\":" << first.nodes
         << ",\"transposition_hits\":"
         << first.transposition_hits
         << ",\"passed\":" << (passed ? "true" : "false")
         << "}\n";
  return passed;
}

int parsePositive(std::string_view value, std::string_view name) {
  std::size_t consumed = 0;
  const long long parsed = std::stoll(std::string(value), &consumed, 10);
  if (consumed != value.size() || parsed < 1 ||
      parsed > std::numeric_limits<int>::max()) {
    throw std::invalid_argument(std::string(name) + " must be positive");
  }
  return static_cast<int>(parsed);
}

Options parseOptions(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--self-test") continue;
    if (index + 1 >= argc) {
      throw std::invalid_argument(std::string(argument) + " needs a value");
    }
    const std::string_view value = argv[++index];
    if (argument == "--screen-games") {
      options.screen_games = parsePositive(value, argument);
      if (options.screen_games > 8) {
        throw std::invalid_argument("--screen-games must be from 1 to 8");
      }
    } else if (argument == "--confirm-games") {
      options.confirm_games = parsePositive(value, argument);
      if (options.confirm_games > 8) {
        throw std::invalid_argument("--confirm-games must be from 1 to 8");
      }
    } else if (argument == "--max-moves") {
      options.maximum_moves = parsePositive(value, argument);
      if (options.maximum_moves > 500) {
        throw std::invalid_argument("--max-moves must be from 1 to 500");
      }
    } else {
      throw std::invalid_argument("unknown argument " +
                                  std::string(argument));
    }
  }
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    for (int index = 1; index < argc; ++index) {
      if (std::string_view(argv[index]) == "--self-test") {
        return selfTest(std::cout) ? 0 : 1;
      }
    }
    const Options options = parseOptions(argc, argv);
    const auto started = Clock::now();
    const std::vector<GateProfile> gates(
        kGates.begin(), kGates.end());
    const auto screen = runScreen(
        kScreenTrainingSeedStart, options.screen_games,
        gates, options);
    const int winner = selectWinner(screen);
    std::vector<Summary> confirmation;
    bool accepted = false;
    if (winner >= 0) {
      const std::vector<GateProfile> selected{
          kGates[static_cast<std::size_t>(winner - 1)],
      };
      confirmation = runScreen(
          kConfirmTrainingSeedStart, options.confirm_games,
          selected, options);
      accepted =
          confirmation[1].mean_score_difference > 0 &&
          confirmation[1].mean_move_difference > 0;
    }
    const double elapsed_seconds = std::chrono::duration<double>(
        Clock::now() - started).count();
    std::cout << std::fixed << std::setprecision(3)
              << "{\"mode\":\"conservative-puct-gates\""
              << ",\"screen_seed_start\":\"0x3d701200\""
              << ",\"confirm_seed_start\":\"0x3d701300\""
              << ",\"screen_games\":" << options.screen_games
              << ",\"confirm_games\":"
              << (winner >= 0 ? options.confirm_games : 0)
              << ",\"elapsed_seconds\":" << elapsed_seconds
              << ",\"max_rss_mib\":" << maximumResidentMiB()
              << ",\"screen_winner\":";
    if (winner < 0) {
      std::cout << "null";
    } else {
      std::cout << "\"" << screen[static_cast<std::size_t>(winner)].name
                << "\"";
    }
    std::cout << ",\"accepted\":" << (accepted ? "true" : "false")
              << ",\"screen\":[";
    for (std::size_t index = 0; index < screen.size(); ++index) {
      if (index > 0) std::cout << ',';
      printSummary(screen[index]);
    }
    std::cout << "],\"confirmation\":[";
    for (std::size_t index = 0; index < confirmation.size(); ++index) {
      if (index > 0) std::cout << ',';
      printSummary(confirmation[index]);
    }
    std::cout << "]}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "drop7_puct: " << error.what() << '\n';
    return 2;
  }
}
