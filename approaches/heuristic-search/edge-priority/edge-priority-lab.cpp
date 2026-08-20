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

using drop7::MoveResult;
using drop7::State;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kScreenTrainingSeedStart = 0x3d70'0300u;
constexpr std::uint32_t kConfirmTrainingSeedStart = 0x3d70'0400u;

struct CoverProfile {
  std::string_view name;
  double altitude_exponent = 2;
  double edge_multiplier = 1.3;
};

constexpr std::array<CoverProfile, 6> kProfiles{{
    // Adam Saltsman's two rules are isolated by the edge-only rows, the
    // altitude-only row, and one joint row. The reference profile is the
    // internal control and remains unchanged outside this executable.
    {"baseline", 2.0, 1.3},
    {"edge2", 2.0, 2.0},
    {"edge3", 2.0, 3.0},
    {"edge4", 2.0, 4.0},
    {"altitude3", 3.0, 1.3},
    {"altitude3-edge3", 3.0, 3.0},
}};

struct Options {
  int screen_games = 8;
  int confirm_games = 8;
  int maximum_moves = 500;
  drop7::cfpi::BehaviorOptions behavior;
};

struct SearchMetrics {
  int completed_depth = 0;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
};

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  int cleared_discs = 0;
  int revealed_covers = 0;
  int board_clears = 0;
  int rises = 0;
  bool terminal = false;
  std::uint64_t search_work = 0;
};

struct ProfileSummary {
  CoverProfile profile;
  std::vector<GameResult> games;
  double mean_score = 0;
  double mean_moves = 0;
  double mean_score_difference = 0;
  double mean_move_difference = 0;
  double clear_per_move = 0;
  double reveal_per_move = 0;
  int board_clears = 0;
  int total_cleared = 0;
  int total_revealed = 0;
  int total_moves = 0;
  std::uint64_t search_work = 0;
};

double coverAltitudeDebt(const State& state, const CoverProfile& profile) {
  double debt = 0;
  for (int row = 0; row < drop7::kBoardSize; ++row) {
    const double elevation =
        static_cast<double>(drop7::kBoardSize - row);
    for (int column = 0; column < drop7::kBoardSize; ++column) {
      const std::uint8_t cell =
          state.board[drop7::indexOf(row, column)];
      if (cell != drop7::kSolid && cell != drop7::kCracked) continue;
      const double cover_factor = cell == drop7::kSolid ? 1.0 : 0.65;
      const double edge_factor =
          column == 0 || column == drop7::kBoardSize - 1
              ? profile.edge_multiplier
              : 1.0;
      debt += std::pow(elevation, profile.altitude_exponent) *
              cover_factor * edge_factor;
    }
  }
  return debt;
}

double profilePotential(const State& state, const CoverProfile& profile) {
  if (state.game_over) return -250'000.0;
  drop7::cfpi::detail::PhaseFeatures f =
      drop7::cfpi::detail::extractPhaseFeatures(state);
  f.cover_altitude_debt = coverAltitudeDebt(state, profile);
  const int moves_until_rise =
      std::max(1, std::min(drop7::kMovesPerLevel, state.moves_remaining));
  const double rise_urgency =
      static_cast<double>(drop7::kMovesPerLevel - moves_until_rise) /
      static_cast<double>(drop7::kMovesPerLevel - 1);
  f.imminent_cover_altitude_debt =
      f.cover_altitude_debt * rise_urgency;

  return
      180.0 * f.open_columns - 10.0 * f.height_load -
      620.0 * f.solid_cells - 220.0 * f.cracked_cells -
      18.0 * f.numbered_cells - 90.0 * f.high_low_numbers +
      140.0 * f.direct_potential + 360.0 * f.latent_chain_potential +
      100.0 * f.cracked_exposure + 40.0 * f.solid_exposure -
      550.0 * f.adjacent_ones - 750.0 * f.triple_twos -
      120.0 * f.dead_low_numbers -
      240.0 * f.projected_occupancy_debt -
      200.0 * f.residual_cover_debt -
      50.0 * f.cover_altitude_debt -
      70.0 * f.imminent_cover_altitude_debt -
      1800.0 * f.peak_height_risk - 120.0 * f.low_cap_load -
      180.0 * f.adjacent_low_cap_load +
      220.0 * f.direct_potential + 300.0 * f.quiet_build_options +
      600.0 * f.quiet_direct_gain +
      600.0 * f.trigger_readiness +
      440.0 * (f.latent_chain_potential + f.cracked_exposure +
               0.35 * f.solid_exposure) +
      1200.0 * f.rise_trigger_readiness;
}

class WorkLimitReached : public std::exception {};

struct CacheEntry {
  double value = 0;
  std::list<std::string>::iterator order;
};

struct SearchContext {
  SearchContext(const Options& lab_options,
                const CoverProfile& cover_profile)
      : options(lab_options), profile(cover_profile) {}

  const Options& options;
  const CoverProfile& profile;
  std::unordered_map<std::string, CacheEntry> cache;
  std::list<std::string> order;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
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

double evaluateAction(const State& state, int column, int depth,
                      SearchContext& context) {
  const int samples = context.options.behavior.chance_samples;
  const std::uint32_t state_seed =
      drop7::cfpi::detail::scenarioSeedForState(
          state, context.options.behavior.policy_seed, depth);
  double total = 0;
  for (int sample = 0; sample < samples; ++sample) {
    checkBudget(context);
    drop7::cfpi::detail::StratifiedRandom random{
        state_seed, sample, samples, 0,
    };
    MoveResult move;
    if (!drop7::cfpi::detail::playMoveSampled(
            state, column, random, move)) {
      total += context.options.behavior.terminal_utility;
      continue;
    }
    ++context.work;
    const double score_delta = static_cast<double>(move.score_delta);
    if (move.state.game_over) {
      total += score_delta + context.options.behavior.terminal_utility;
      continue;
    }
    move.state.score = 0;
    move.state.next_disc = drop7::cfpi::detail::sampledNextDisc(
        state_seed, sample, samples);
    bool ignored = false;
    const State next =
        drop7::cfpi::detail::canonicalState(move.state, ignored);
    total += score_delta + bestFutureValue(next, depth - 1, context);
  }
  return total / static_cast<double>(samples);
}

double evaluateLeaf(const State& state, SearchContext& context) {
  checkBudget(context);
  ++context.work;
  const double value = profilePotential(state, context.profile);
  if (!std::isfinite(value)) {
    throw std::runtime_error("edge-priority evaluator returned non-finite");
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
    best = std::max(best, evaluateAction(state, column, depth, context));
  }
  if (!std::isfinite(best)) {
    best = context.options.behavior.terminal_utility;
  }
  setCachedValue(context, key, best);
  return best;
}

std::pair<int, double> bestRootAction(
    const State& canonical, int depth, SearchContext& context) {
  int best_column = -1;
  double best_value = -std::numeric_limits<double>::infinity();
  for (int column : drop7::cfpi::detail::kColumnOrder) {
    if (!drop7::isLegal(canonical.board, column)) continue;
    const double value = evaluateAction(
        canonical, column, depth, context);
    if (value > best_value) {
      best_value = value;
      best_column = column;
    }
  }
  return {best_column, best_value};
}

int chooseAction(const State& input, const CoverProfile& profile,
                 const Options& options,
                 SearchMetrics* metrics = nullptr) {
  if (input.game_over) return -1;
  bool mirrored = false;
  const State canonical =
      drop7::cfpi::detail::canonicalState(input, mirrored);
  SearchContext context(options, profile);
  int completed_column = -1;
  int completed_depth = 0;
  for (int depth = 1; depth <= options.behavior.max_depth; ++depth) {
    try {
      const auto [column, value] =
          bestRootAction(canonical, depth, context);
      (void)value;
      if (column < 0) break;
      completed_column = column;
      completed_depth = depth;
    } catch (const WorkLimitReached&) {
      break;
    }
  }
  if (completed_column < 0) {
    completed_column = drop7::centerFirstMove(canonical.board);
  }
  if (metrics != nullptr) {
    metrics->completed_depth = completed_depth;
    metrics->work = context.work;
    metrics->nodes = context.nodes;
    metrics->cache_hits = context.cache_hits;
  }
  return mirrored && completed_column >= 0
             ? drop7::kBoardSize - 1 - completed_column
             : completed_column;
}

GameResult runGame(std::uint32_t seed, const CoverProfile& profile,
                   const Options& options) {
  State state = drop7::initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < options.maximum_moves) {
    SearchMetrics metrics;
    const int action = chooseAction(state, profile, options, &metrics);
    result.search_work += metrics.work;
    MoveResult move;
    if (!drop7::playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("edge-priority policy selected illegal action");
    }
    for (const drop7::Wave& wave : move.waves) {
      result.cleared_discs += wave.cleared;
      result.revealed_covers += wave.revealed;
    }
    if (move.cleared_board) ++result.board_clears;
    if (move.level_advanced) ++result.rises;
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.terminal = state.game_over;
  return result;
}

ProfileSummary summarize(const CoverProfile& profile,
                         std::vector<GameResult> games,
                         const ProfileSummary* baseline) {
  ProfileSummary result;
  result.profile = profile;
  result.games = std::move(games);
  for (const GameResult& game : result.games) {
    result.mean_score += static_cast<double>(game.score);
    result.mean_moves += static_cast<double>(game.moves);
    result.total_cleared += game.cleared_discs;
    result.total_revealed += game.revealed_covers;
    result.total_moves += game.moves;
    result.board_clears += game.board_clears;
    result.search_work += game.search_work;
  }
  if (!result.games.empty()) {
    result.mean_score /= static_cast<double>(result.games.size());
    result.mean_moves /= static_cast<double>(result.games.size());
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
    const std::vector<CoverProfile>& profiles, const Options& options) {
  std::vector<std::vector<GameResult>> results(profiles.size());
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed =
        seed_start + static_cast<std::uint32_t>(game);
    std::vector<std::future<GameResult>> pending;
    pending.reserve(profiles.size());
    for (const CoverProfile& profile : profiles) {
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

int selectScreenWinner(const std::vector<ProfileSummary>& summaries) {
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
            << "\",\"altitude_exponent\":"
            << summary.profile.altitude_exponent
            << ",\"edge_multiplier\":"
            << summary.profile.edge_multiplier
            << ",\"mean_score\":" << summary.mean_score
            << ",\"mean_moves\":" << summary.mean_moves
            << ",\"paired_mean_score_difference\":"
            << summary.mean_score_difference
            << ",\"paired_mean_move_difference\":"
            << summary.mean_move_difference
            << ",\"clear_per_move\":" << summary.clear_per_move
            << ",\"reveal_per_move\":" << summary.reveal_per_move
            << ",\"total_cleared\":" << summary.total_cleared
            << ",\"total_revealed\":" << summary.total_revealed
            << ",\"board_clears\":" << summary.board_clears
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

  State state;
  state.board = drop7::initialBoard();
  state.board[drop7::indexOf(5, 0)] = 3;
  state.board[drop7::indexOf(5, 2)] = drop7::kCracked;
  state.board[drop7::indexOf(5, 4)] = 5;
  state.next_disc = 6;
  state.moves_remaining = 3;
  const double baseline_value =
      profilePotential(state, kProfiles.front());
  const bool baseline_exact =
      std::abs(baseline_value -
               drop7::cfpi::phasePotential(state)) <= 1e-9;

  const int first = chooseAction(state, kProfiles.front(), quick);
  const int second = chooseAction(state, kProfiles.front(), quick);
  drop7::cfpi::BehaviorOptions behavior = quick.behavior;
  const int verified =
      drop7::cfpi::chooseBehaviorAction(state, behavior);
  const bool deterministic = first == second;
  const bool verified_action = first == verified;
  const bool legal = drop7::isLegal(state.board, first);

  State mirrored = state;
  mirrored.board =
      drop7::cfpi::detail::mirrorBoard(state.board);
  const int reflected =
      chooseAction(mirrored, kProfiles.back(), quick);
  const int forward = chooseAction(state, kProfiles.back(), quick);
  const bool mirror_safe =
      reflected == drop7::kBoardSize - 1 - forward;

  State without_covers = state;
  for (std::uint8_t& cell : without_covers.board) {
    if (cell == drop7::kSolid || cell == drop7::kCracked) {
      cell = 4;
    }
  }
  const bool cover_only =
      std::abs(profilePotential(without_covers, kProfiles.front()) -
               profilePotential(without_covers, kProfiles.back())) <= 1e-9;
  const bool stronger_penalty =
      profilePotential(state, kProfiles.back()) <
      profilePotential(state, kProfiles.front());

  const bool passed = baseline_exact && deterministic && verified_action &&
                      legal && mirror_safe && cover_only &&
                      stronger_penalty;
  output << "{\"baseline_exact\":"
         << (baseline_exact ? "true" : "false")
         << ",\"deterministic\":"
         << (deterministic ? "true" : "false")
         << ",\"verified_action\":"
         << (verified_action ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false")
         << ",\"mirror_safe\":"
         << (mirror_safe ? "true" : "false")
         << ",\"cover_only\":"
         << (cover_only ? "true" : "false")
         << ",\"stronger_penalty\":"
         << (stronger_penalty ? "true" : "false")
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
    const std::vector<CoverProfile> screen_profiles(
        kProfiles.begin(), kProfiles.end());
    const auto screen = runProfiles(
        kScreenTrainingSeedStart, options.screen_games,
        screen_profiles, options);
    const int winner = selectScreenWinner(screen);

    std::vector<ProfileSummary> confirmation;
    bool accepted = false;
    if (winner >= 0) {
      const std::vector<CoverProfile> confirm_profiles{
          kProfiles.front(),
          kProfiles[static_cast<std::size_t>(winner)],
      };
      confirmation = runProfiles(
          kConfirmTrainingSeedStart, options.confirm_games,
          confirm_profiles, options);
      accepted =
          confirmation[1].mean_score_difference > 0 &&
          confirmation[1].mean_move_difference > 0;
    }
    const double elapsed_seconds = std::chrono::duration<double>(
        Clock::now() - started).count();

    std::cout << std::fixed << std::setprecision(3)
              << "{\"mode\":\"edge-priority-lab\""
              << ",\"screen_seed_start\":\"0x3d700300\""
              << ",\"confirm_seed_start\":\"0x3d700400\""
              << ",\"screen_games\":" << options.screen_games
              << ",\"confirm_games\":"
              << (winner >= 0 ? options.confirm_games : 0)
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
    std::cerr << "drop7_edge_priority_lab: " << error.what() << '\n';
    return 2;
  }
}
