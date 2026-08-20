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
#include <list>
#include <numeric>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using drop7::MoveResult;
using drop7::State;

constexpr std::uint32_t kScreenTrainingSeedStart = 0x3d70'0500u;
constexpr std::uint32_t kConfirmTrainingSeedStart = 0x3d70'0600u;
constexpr double kCvarFraction = 0.4;

struct RiskProfile {
  std::string_view name;
  int height_threshold = 5;
  int occupancy_threshold = 20;
  double cover_backlog_threshold = 4;
  double risk_weight = 0;
};

constexpr std::array<RiskProfile, 5> kProfiles{{
    {"control", 5, 20, 4.0, 0.0},
    {"early-cvar25", 5, 20, 4.0, 0.25},
    {"early-cvar50", 5, 20, 4.0, 0.50},
    {"late-cvar25", 6, 24, 6.0, 0.25},
    {"late-cvar50", 6, 24, 6.0, 0.50},
}};

struct Options {
  int screen_games = 8;
  int confirm_games = 8;
  int maximum_moves = 500;
  drop7::cfpi::BehaviorOptions behavior;
};

struct CriticalFeatures {
  int occupied = 0;
  int covers = 0;
  int maximum_height = 0;
  int moves_until_rise = drop7::kMovesPerLevel;
  double cover_backlog = 0;
};

struct SearchResult {
  int action = -1;
  int mean_action = -1;
  bool critical = false;
  int completed_depth = 0;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
};

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  int critical_states = 0;
  int noncritical_states = 0;
  int critical_action_switches = 0;
  int noncritical_action_switches = 0;
  int cleared_discs = 0;
  int revealed_covers = 0;
  bool terminal = false;
  std::uint64_t search_work = 0;
};

struct ProfileSummary {
  RiskProfile profile;
  std::vector<GameResult> games;
  double mean_score = 0;
  double mean_moves = 0;
  double mean_score_difference = 0;
  double mean_move_difference = 0;
  double trigger_rate = 0;
  int critical_states = 0;
  int noncritical_states = 0;
  int critical_action_switches = 0;
  int noncritical_action_switches = 0;
  double critical_switch_rate = 0;
  double clear_per_move = 0;
  double reveal_per_move = 0;
  int total_cleared = 0;
  int total_revealed = 0;
  int total_moves = 0;
  std::uint64_t search_work = 0;
};

CriticalFeatures extractCriticalFeatures(const State& state) {
  CriticalFeatures result;
  result.moves_until_rise =
      std::max(1, std::min(drop7::kMovesPerLevel, state.moves_remaining));
  for (int column = 0; column < drop7::kBoardSize; ++column) {
    int height = 0;
    for (int row = 0; row < drop7::kBoardSize; ++row) {
      const std::uint8_t cell =
          state.board[drop7::indexOf(row, column)];
      if (cell == drop7::kEmpty) continue;
      ++height;
      ++result.occupied;
      if (cell == drop7::kSolid || cell == drop7::kCracked) {
        ++result.covers;
      }
    }
    result.maximum_height = std::max(result.maximum_height, height);
  }
  result.cover_backlog = std::max(
      0.0, result.covers - 1.4 * result.moves_until_rise);
  return result;
}

bool isCritical(const State& state, const RiskProfile& profile) {
  const CriticalFeatures features = extractCriticalFeatures(state);
  return features.maximum_height >= profile.height_threshold &&
         (features.occupied >= profile.occupancy_threshold ||
          features.cover_backlog >= profile.cover_backlog_threshold);
}

double riskAggregate(std::vector<double> samples, double risk_weight) {
  if (samples.empty()) {
    throw std::invalid_argument("risk aggregation requires samples");
  }
  const double mean =
      std::accumulate(samples.begin(), samples.end(), 0.0) /
      static_cast<double>(samples.size());
  if (risk_weight <= 0) return mean;
  std::sort(samples.begin(), samples.end());
  const int tail_count = std::max(
      1, static_cast<int>(std::ceil(
             samples.size() * kCvarFraction - 1e-12)));
  const double cvar = std::accumulate(
                          samples.begin(),
                          samples.begin() + tail_count, 0.0) /
                      static_cast<double>(tail_count);
  return mean * (1.0 - risk_weight) + cvar * risk_weight;
}

class WorkLimitReached : public std::exception {};

struct CacheEntry {
  double value = 0;
  std::list<std::string>::iterator order;
};

struct SearchContext {
  explicit SearchContext(const Options& lab_options)
      : options(lab_options) {}

  const Options& options;
  std::unordered_map<std::string, CacheEntry> cache;
  std::list<std::string> order;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
};

struct ActionValue {
  double mean = -std::numeric_limits<double>::infinity();
  double risk_adjusted = -std::numeric_limits<double>::infinity();
};

void checkBudget(const SearchContext& context) {
  if (context.work >= context.options.behavior.max_work) {
    throw WorkLimitReached{};
  }
}

void setCachedValue(SearchContext& context, std::string key, double value) {
  const auto prior = context.cache.find(key);
  if (prior != context.cache.end()) {
    context.order.erase(prior->second.order);
    context.cache.erase(prior);
  }
  while (context.cache.size() >=
         context.options.behavior.max_cache_entries) {
    const std::string& oldest = context.order.front();
    context.cache.erase(oldest);
    context.order.pop_front();
  }
  context.order.push_back(key);
  const auto order = std::prev(context.order.end());
  context.cache.emplace(std::move(key), CacheEntry{value, order});
}

double bestFutureValue(const State& state, int depth,
                       SearchContext& context);

ActionValue evaluateAction(const State& state, int column, int depth,
                           bool root_risk, double risk_weight,
                           SearchContext& context) {
  const int sample_count = context.options.behavior.chance_samples;
  const std::uint32_t state_seed =
      drop7::cfpi::detail::scenarioSeedForState(
          state, context.options.behavior.policy_seed, depth);
  std::vector<double> samples;
  samples.reserve(static_cast<std::size_t>(sample_count));
  for (int sample = 0; sample < sample_count; ++sample) {
    checkBudget(context);
    drop7::cfpi::detail::StratifiedRandom random{
        state_seed, sample, sample_count, 0,
    };
    MoveResult move;
    if (!drop7::cfpi::detail::playMoveSampled(
            state, column, random, move)) {
      samples.push_back(context.options.behavior.terminal_utility);
      continue;
    }
    ++context.work;
    const double score_delta = static_cast<double>(move.score_delta);
    if (move.state.game_over) {
      samples.push_back(
          score_delta + context.options.behavior.terminal_utility);
      continue;
    }
    move.state.score = 0;
    move.state.next_disc = drop7::cfpi::detail::sampledNextDisc(
        state_seed, sample, sample_count);
    bool ignored = false;
    const State next =
        drop7::cfpi::detail::canonicalState(move.state, ignored);
    samples.push_back(
        score_delta + bestFutureValue(next, depth - 1, context));
  }
  const double mean =
      std::accumulate(samples.begin(), samples.end(), 0.0) /
      static_cast<double>(samples.size());
  return {
      mean,
      root_risk ? riskAggregate(samples, risk_weight) : mean,
  };
}

double evaluateLeaf(const State& state, SearchContext& context) {
  checkBudget(context);
  ++context.work;
  const double value = drop7::cfpi::phasePotential(state);
  if (!std::isfinite(value)) {
    throw std::runtime_error("critical-risk evaluator returned non-finite");
  }
  return value;
}

double bestFutureValue(const State& state, int depth,
                       SearchContext& context) {
  ++context.nodes;
  checkBudget(context);
  if (state.game_over) {
    return context.options.behavior.terminal_utility;
  }
  if (depth == 0) return evaluateLeaf(state, context);

  const std::string key =
      drop7::cfpi::detail::dynamicStateKey(state, depth);
  const auto cached = context.cache.find(key);
  if (cached != context.cache.end()) {
    ++context.cache_hits;
    const double value = cached->second.value;
    context.order.splice(context.order.end(), context.order,
                         cached->second.order);
    return value;
  }
  double best = -std::numeric_limits<double>::infinity();
  for (int column : drop7::cfpi::detail::kColumnOrder) {
    if (!drop7::isLegal(state.board, column)) continue;
    const ActionValue action = evaluateAction(
        state, column, depth, false, 0, context);
    best = std::max(best, action.mean);
  }
  if (!std::isfinite(best)) {
    best = context.options.behavior.terminal_utility;
  }
  setCachedValue(context, key, best);
  return best;
}

std::pair<int, int> bestRootActions(
    const State& canonical, int depth, bool root_critical,
    const RiskProfile& profile, SearchContext& context) {
  int mean_column = -1;
  int risk_column = -1;
  double best_mean = -std::numeric_limits<double>::infinity();
  double best_risk = -std::numeric_limits<double>::infinity();
  for (int column : drop7::cfpi::detail::kColumnOrder) {
    if (!drop7::isLegal(canonical.board, column)) continue;
    const ActionValue value = evaluateAction(
        canonical, column, depth, root_critical,
        profile.risk_weight, context);
    if (value.mean > best_mean) {
      best_mean = value.mean;
      mean_column = column;
    }
    if (value.risk_adjusted > best_risk) {
      best_risk = value.risk_adjusted;
      risk_column = column;
    }
  }
  return {risk_column, mean_column};
}

SearchResult chooseAction(const State& input, const RiskProfile& profile,
                          const Options& options) {
  SearchResult result;
  if (input.game_over) return result;
  bool mirrored = false;
  const State canonical =
      drop7::cfpi::detail::canonicalState(input, mirrored);
  result.critical = isCritical(canonical, profile);
  SearchContext context(options);
  int risk_column = -1;
  int mean_column = -1;
  for (int depth = 1; depth <= options.behavior.max_depth; ++depth) {
    try {
      const auto [next_risk, next_mean] = bestRootActions(
          canonical, depth, result.critical, profile, context);
      if (next_risk < 0 || next_mean < 0) break;
      risk_column = next_risk;
      mean_column = next_mean;
      result.completed_depth = depth;
    } catch (const WorkLimitReached&) {
      break;
    }
  }
  if (risk_column < 0) {
    risk_column = drop7::centerFirstMove(canonical.board);
    mean_column = risk_column;
  }
  result.action =
      mirrored ? drop7::kBoardSize - 1 - risk_column : risk_column;
  result.mean_action =
      mirrored ? drop7::kBoardSize - 1 - mean_column : mean_column;
  result.work = context.work;
  result.nodes = context.nodes;
  result.cache_hits = context.cache_hits;
  return result;
}

GameResult runGame(std::uint32_t seed, const RiskProfile& profile,
                   const Options& options) {
  State state = drop7::initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < options.maximum_moves) {
    const SearchResult search = chooseAction(state, profile, options);
    result.search_work += search.work;
    const bool switched = search.action != search.mean_action;
    if (search.critical) {
      ++result.critical_states;
      if (switched) ++result.critical_action_switches;
    } else {
      ++result.noncritical_states;
      if (switched) ++result.noncritical_action_switches;
    }
    MoveResult move;
    if (!drop7::playHeadlessMove(state, seed, search.action, move)) {
      throw std::runtime_error("critical-risk policy selected illegal action");
    }
    for (const drop7::Wave& wave : move.waves) {
      result.cleared_discs += wave.cleared;
      result.revealed_covers += wave.revealed;
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.terminal = state.game_over;
  return result;
}

ProfileSummary summarize(const RiskProfile& profile,
                         std::vector<GameResult> games,
                         const ProfileSummary* baseline) {
  ProfileSummary result;
  result.profile = profile;
  result.games = std::move(games);
  for (const GameResult& game : result.games) {
    result.mean_score += static_cast<double>(game.score);
    result.mean_moves += static_cast<double>(game.moves);
    result.critical_states += game.critical_states;
    result.noncritical_states += game.noncritical_states;
    result.critical_action_switches += game.critical_action_switches;
    result.noncritical_action_switches += game.noncritical_action_switches;
    result.total_cleared += game.cleared_discs;
    result.total_revealed += game.revealed_covers;
    result.total_moves += game.moves;
    result.search_work += game.search_work;
  }
  if (!result.games.empty()) {
    result.mean_score /= static_cast<double>(result.games.size());
    result.mean_moves /= static_cast<double>(result.games.size());
  }
  const int states = result.critical_states + result.noncritical_states;
  if (states > 0) {
    result.trigger_rate =
        static_cast<double>(result.critical_states) / states;
  }
  if (result.critical_states > 0) {
    result.critical_switch_rate =
        static_cast<double>(result.critical_action_switches) /
        result.critical_states;
  }
  if (result.total_moves > 0) {
    result.clear_per_move =
        static_cast<double>(result.total_cleared) / result.total_moves;
    result.reveal_per_move =
        static_cast<double>(result.total_revealed) / result.total_moves;
  }
  if (baseline != nullptr) {
    if (baseline->games.size() != result.games.size()) {
      throw std::runtime_error("paired profile game counts differ");
    }
    double score_difference = 0;
    double move_difference = 0;
    for (std::size_t index = 0; index < result.games.size(); ++index) {
      score_difference += static_cast<double>(
          result.games[index].score - baseline->games[index].score);
      move_difference +=
          result.games[index].moves - baseline->games[index].moves;
    }
    result.mean_score_difference =
        score_difference / static_cast<double>(result.games.size());
    result.mean_move_difference =
        move_difference / static_cast<double>(result.games.size());
  }
  return result;
}

std::vector<ProfileSummary> runProfiles(
    std::uint32_t seed_start, int games,
    const std::vector<RiskProfile>& profiles, const Options& options) {
  std::vector<std::vector<GameResult>> results(profiles.size());
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed =
        seed_start + static_cast<std::uint32_t>(game);
    std::vector<std::future<GameResult>> pending;
    for (const RiskProfile& profile : profiles) {
      pending.push_back(std::async(std::launch::async, [&, seed, profile] {
        return runGame(seed, profile, options);
      }));
    }
    for (std::size_t index = 0; index < pending.size(); ++index) {
      results[index].push_back(pending[index].get());
    }
  }
  std::vector<ProfileSummary> summaries;
  summaries.reserve(profiles.size());
  for (std::size_t index = 0; index < profiles.size(); ++index) {
    summaries.push_back(summarize(
        profiles[index], std::move(results[index]),
        index == 0 ? nullptr : &summaries.front()));
  }
  return summaries;
}

int selectWinner(const std::vector<ProfileSummary>& summaries) {
  int winner = -1;
  for (int index = 1; index < static_cast<int>(summaries.size()); ++index) {
    const ProfileSummary& candidate =
        summaries[static_cast<std::size_t>(index)];
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

void printSummary(const ProfileSummary& summary) {
  std::cout << "{\"name\":\"" << summary.profile.name
            << "\",\"height_threshold\":"
            << summary.profile.height_threshold
            << ",\"occupancy_threshold\":"
            << summary.profile.occupancy_threshold
            << ",\"cover_backlog_threshold\":"
            << summary.profile.cover_backlog_threshold
            << ",\"risk_weight\":" << summary.profile.risk_weight
            << ",\"mean_score\":" << summary.mean_score
            << ",\"mean_moves\":" << summary.mean_moves
            << ",\"paired_mean_score_difference\":"
            << summary.mean_score_difference
            << ",\"paired_mean_move_difference\":"
            << summary.mean_move_difference
            << ",\"trigger_rate\":" << summary.trigger_rate
            << ",\"critical_states\":" << summary.critical_states
            << ",\"noncritical_states\":"
            << summary.noncritical_states
            << ",\"critical_action_switches\":"
            << summary.critical_action_switches
            << ",\"noncritical_action_switches\":"
            << summary.noncritical_action_switches
            << ",\"critical_switch_rate\":"
            << summary.critical_switch_rate
            << ",\"clear_per_move\":" << summary.clear_per_move
            << ",\"reveal_per_move\":" << summary.reveal_per_move
            << ",\"search_work\":" << summary.search_work
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
  Options quick;
  quick.behavior.max_depth = 2;
  quick.behavior.chance_samples = 3;
  quick.behavior.max_work = 100'000;
  quick.behavior.max_cache_entries = 4'000;

  State ordinary;
  ordinary.board = drop7::initialBoard();
  ordinary.next_disc = 4;
  ordinary.moves_remaining = 5;
  const SearchResult control =
      chooseAction(ordinary, kProfiles.front(), quick);
  const int verified =
      drop7::cfpi::chooseBehaviorAction(ordinary, quick.behavior);
  bool ordinary_identical =
      !control.critical && control.action == verified &&
      control.action == control.mean_action;
  for (std::size_t index = 1; index < kProfiles.size(); ++index) {
    const SearchResult candidate =
        chooseAction(ordinary, kProfiles[index], quick);
    ordinary_identical =
        ordinary_identical && !candidate.critical &&
        candidate.action == verified &&
        candidate.action == candidate.mean_action;
  }

  State critical;
  critical.board.fill(drop7::kEmpty);
  for (int column = 0; column < drop7::kBoardSize; ++column) {
    for (int row = 1; row < drop7::kBoardSize; ++row) {
      critical.board[drop7::indexOf(row, column)] =
          row >= 5 ? drop7::kSolid
                   : static_cast<std::uint8_t>((column + row) % 7 + 1);
    }
  }
  critical.next_disc = 7;
  critical.moves_remaining = 2;
  const bool critical_trigger =
      isCritical(critical, kProfiles[1]) &&
      isCritical(critical, kProfiles[3]);
  const SearchResult first = chooseAction(critical, kProfiles[2], quick);
  const SearchResult second = chooseAction(critical, kProfiles[2], quick);
  const bool deterministic = first.action == second.action;
  const bool legal = drop7::isLegal(critical.board, first.action);

  State mirrored = critical;
  mirrored.board =
      drop7::cfpi::detail::mirrorBoard(critical.board);
  const SearchResult reflected =
      chooseAction(mirrored, kProfiles[2], quick);
  const bool mirror_safe =
      reflected.action ==
      drop7::kBoardSize - 1 - first.action;

  const std::vector<double> synthetic{-100, 0, 10, 20, 30};
  const double mean = riskAggregate(synthetic, 0);
  const double risk = riskAggregate(synthetic, 0.5);
  const bool lower_tail = risk < mean;
  const bool passed = ordinary_identical && critical_trigger &&
                      deterministic && legal && mirror_safe &&
                      lower_tail;
  output << "{\"ordinary_identical\":"
         << (ordinary_identical ? "true" : "false")
         << ",\"critical_trigger\":"
         << (critical_trigger ? "true" : "false")
         << ",\"deterministic\":"
         << (deterministic ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false")
         << ",\"mirror_safe\":"
         << (mirror_safe ? "true" : "false")
         << ",\"lower_tail\":"
         << (lower_tail ? "true" : "false")
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
    const std::vector<RiskProfile> screen_profiles(
        kProfiles.begin(), kProfiles.end());
    const auto screen = runProfiles(
        kScreenTrainingSeedStart, options.screen_games,
        screen_profiles, options);
    const int winner = selectWinner(screen);
    std::vector<ProfileSummary> confirmation;
    bool accepted = false;
    if (winner >= 0) {
      const std::vector<RiskProfile> confirmation_profiles{
          kProfiles.front(),
          kProfiles[static_cast<std::size_t>(winner)],
      };
      confirmation = runProfiles(
          kConfirmTrainingSeedStart, options.confirm_games,
          confirmation_profiles, options);
      accepted =
          confirmation[1].mean_score_difference > 0 &&
          confirmation[1].mean_move_difference > 0;
    }
    const double elapsed_seconds = std::chrono::duration<double>(
        Clock::now() - started).count();

    std::cout << std::fixed << std::setprecision(3)
              << "{\"mode\":\"critical-risk-lab\""
              << ",\"screen_seed_start\":\"0x3d700500\""
              << ",\"confirm_seed_start\":\"0x3d700600\""
              << ",\"screen_games\":" << options.screen_games
              << ",\"confirm_games\":"
              << (winner >= 0 ? options.confirm_games : 0)
              << ",\"cvar_fraction\":" << kCvarFraction
              << ",\"elapsed_seconds\":" << elapsed_seconds
              << ",\"screen_winner\":";
    if (winner < 0) {
      std::cout << "null";
    } else {
      std::cout << "\"" << kProfiles[static_cast<std::size_t>(winner)].name
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
    std::cerr << "drop7_critical_risk_lab: " << error.what() << '\n';
    return 2;
  }
}
