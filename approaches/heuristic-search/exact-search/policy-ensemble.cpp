#include "../../../src/core/native/public-behavior.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;
using drop7::MoveResult;
using drop7::State;

constexpr std::array<std::uint32_t, 5> kPolicySeeds{{
    0xd707'5eedu,
    0x91e1'0da5u,
    0x6a09'e667u,
    0xbb67'ae85u,
    0x3c6e'f372u,
}};

struct Options {
  int games = 4;
  int max_moves = 1000;
  int members = 3;
  std::uint32_t seed_start = 0x3d70'7000u;
  drop7::cfpi::BehaviorOptions behavior;
};

enum class PolicyMode {
  kExact,
  kMember1,
  kMember2,
  kVote,
  kQMean,
};

struct VoteMetrics {
  int decisions = 0;
  int consensus = 0;
  int switches = 0;
  std::uint64_t work = 0;
};

struct RootValues {
  std::array<double, drop7::kBoardSize> canonical_q{};
  int canonical_action = -1;
  int action = -1;
  bool mirrored = false;
  drop7::cfpi::BehaviorMetrics metrics;
};

struct GameResult {
  std::int64_t score = 0;
  int moves = 0;
  int clears = 0;
  int reveals = 0;
  bool censored = false;
  VoteMetrics votes;
};

std::string valueAfter(int argc, char** argv, std::string_view flag,
                       std::string fallback) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == flag) return argv[index + 1];
  }
  return fallback;
}

bool hasFlag(int argc, char** argv, std::string_view flag) {
  for (int index = 1; index < argc; ++index) {
    if (argv[index] == flag) return true;
  }
  return false;
}

int parsePositive(std::string_view text, std::string_view flag) {
  std::size_t consumed = 0;
  const long long value = std::stoll(std::string(text), &consumed, 0);
  if (consumed != text.size() || value < 1 ||
      value > std::numeric_limits<int>::max()) {
    throw std::invalid_argument(std::string(flag) + " must be positive");
  }
  return static_cast<int>(value);
}

std::uint32_t parseSeed(std::string_view text) {
  std::size_t consumed = 0;
  const unsigned long long value =
      std::stoull(std::string(text), &consumed, 0);
  if (consumed != text.size() || value > 0xffff'ffffull) {
    throw std::invalid_argument("--seed-start must be a uint32");
  }
  return static_cast<std::uint32_t>(value);
}

RootValues searchRootValues(const State& input,
                            const drop7::cfpi::BehaviorOptions& options) {
  drop7::cfpi::detail::validateOptions(options);
  RootValues result;
  result.canonical_q.fill(-std::numeric_limits<double>::infinity());
  result.metrics.requested_depth = options.max_depth;
  if (input.game_over) return result;

  const State canonical =
      drop7::cfpi::detail::canonicalState(input, result.mirrored);
  drop7::cfpi::detail::SearchContext context(options);
  double completed_value = -std::numeric_limits<double>::infinity();
  for (int depth = 1; depth <= options.max_depth; ++depth) {
    std::array<double, drop7::kBoardSize> candidate_q{};
    candidate_q.fill(-std::numeric_limits<double>::infinity());
    int candidate_action = -1;
    double candidate_value = -std::numeric_limits<double>::infinity();
    try {
      for (int column : drop7::cfpi::detail::kColumnOrder) {
        if (!drop7::isLegal(canonical.board, column)) continue;
        const double value = drop7::cfpi::detail::evaluateAction(
            canonical, column, depth, context);
        candidate_q[column] = value;
        if (value > candidate_value) {
          candidate_value = value;
          candidate_action = column;
        }
      }
    } catch (const drop7::cfpi::detail::WorkLimitReached&) {
      break;
    }
    if (candidate_action < 0) break;
    result.canonical_q = candidate_q;
    result.canonical_action = candidate_action;
    completed_value = candidate_value;
    result.metrics.completed_depth = depth;
  }

  if (result.canonical_action < 0) {
    result.canonical_action = drop7::centerFirstMove(canonical.board);
  }
  result.metrics.complete =
      result.metrics.completed_depth == options.max_depth;
  result.metrics.nodes = context.nodes;
  result.metrics.work = context.work;
  result.metrics.cache_hits = context.cache_hits;
  result.metrics.cache_entries = context.cache.size();
  result.metrics.value = completed_value;
  result.action = result.mirrored && result.canonical_action >= 0
                      ? drop7::kBoardSize - 1 - result.canonical_action
                      : result.canonical_action;
  return result;
}

int chooseVoteActionEager(const State& state, const Options& options,
                          VoteMetrics& metrics) {
  std::array<int, drop7::kBoardSize> votes{};
  std::array<int, 5> actions{};
  for (int member = 0; member < options.members; ++member) {
    auto member_options = options.behavior;
    member_options.policy_seed = kPolicySeeds[member];
    drop7::cfpi::BehaviorMetrics behavior_metrics;
    actions[member] = drop7::cfpi::chooseBehaviorAction(
        state, member_options, &behavior_metrics);
    metrics.work += behavior_metrics.work;
    if (actions[member] >= 0) ++votes[actions[member]];
  }
  const int baseline = actions[0];
  int winner = baseline;
  int maximum_votes = baseline >= 0 ? votes[baseline] : 0;
  for (int column = 0; column < drop7::kBoardSize; ++column) {
    if (votes[column] > maximum_votes) {
      maximum_votes = votes[column];
      winner = column;
    }
  }
  ++metrics.decisions;
  if (maximum_votes > options.members / 2) {
    ++metrics.consensus;
  } else {
    // A split vote contains no robust direction; retain the reference member.
    winner = baseline;
  }
  if (winner != baseline) ++metrics.switches;
  return winner;
}

int chooseVoteActionLazy3(const State& state, const Options& options,
                          VoteMetrics& metrics) {
  if (options.members != 3) {
    return chooseVoteActionEager(state, options, metrics);
  }
  std::array<int, 3> actions{};
  for (int member = 0; member < 2; ++member) {
    auto member_options = options.behavior;
    member_options.policy_seed = kPolicySeeds[member];
    drop7::cfpi::BehaviorMetrics behavior_metrics;
    actions[member] = drop7::cfpi::chooseBehaviorAction(
        state, member_options, &behavior_metrics);
    metrics.work += behavior_metrics.work;
  }
  const int baseline = actions[0];
  int winner = baseline;
  if (actions[0] == actions[1]) {
    ++metrics.consensus;
  } else {
    auto member_options = options.behavior;
    member_options.policy_seed = kPolicySeeds[2];
    drop7::cfpi::BehaviorMetrics behavior_metrics;
    actions[2] = drop7::cfpi::chooseBehaviorAction(
        state, member_options, &behavior_metrics);
    metrics.work += behavior_metrics.work;
    if (actions[2] == actions[1]) winner = actions[1];
    if (actions[2] == actions[0] || actions[2] == actions[1]) {
      ++metrics.consensus;
    }
  }
  ++metrics.decisions;
  if (winner != baseline) ++metrics.switches;
  return winner;
}

int chooseQMeanAction(const State& state, const Options& options,
                      VoteMetrics& metrics) {
  bool canonical_mirrored = false;
  const State canonical =
      drop7::cfpi::detail::canonicalState(state, canonical_mirrored);
  std::array<RootValues, 3> members;
  for (int member = 0; member < 3; ++member) {
    auto member_options = options.behavior;
    member_options.policy_seed = kPolicySeeds[member];
    members[member] = searchRootValues(state, member_options);
    metrics.work += members[member].metrics.work;
  }
  const int baseline = members[0].canonical_action;
  if (baseline < 0) return -1;

  int winner = baseline;
  double winning_advantage = 0.0;
  for (int column : drop7::cfpi::detail::kColumnOrder) {
    if (column == baseline || !drop7::isLegal(canonical.board, column)) {
      continue;
    }
    int support = 0;
    double advantage_sum = 0.0;
    bool finite = true;
    for (const RootValues& member : members) {
      const double action_q = member.canonical_q[column];
      const double baseline_q = member.canonical_q[baseline];
      if (!std::isfinite(action_q) || !std::isfinite(baseline_q)) {
        finite = false;
        break;
      }
      const double advantage = action_q - baseline_q;
      advantage_sum += advantage;
      if (advantage > 0.0) ++support;
    }
    if (!finite || support < 2) continue;
    const double mean_advantage = advantage_sum / 3.0;
    if (mean_advantage > winning_advantage) {
      winning_advantage = mean_advantage;
      winner = column;
    }
  }
  ++metrics.decisions;
  if (winner != baseline) {
    ++metrics.consensus;
    ++metrics.switches;
  }
  return canonical_mirrored ? drop7::kBoardSize - 1 - winner : winner;
}

int chooseMemberAction(const State& state, const Options& options,
                       int member, VoteMetrics& metrics) {
  auto member_options = options.behavior;
  member_options.policy_seed = kPolicySeeds[member];
  drop7::cfpi::BehaviorMetrics behavior_metrics;
  const int action = drop7::cfpi::chooseBehaviorAction(
      state, member_options, &behavior_metrics);
  metrics.work += behavior_metrics.work;
  ++metrics.decisions;
  return action;
}

GameResult runGame(std::uint32_t seed, const Options& options,
                   PolicyMode mode) {
  State state = drop7::initialHeadlessState(seed);
  GameResult result;
  while (!state.game_over && state.moves_played < options.max_moves) {
    int action = -1;
    switch (mode) {
      case PolicyMode::kExact:
        action = chooseMemberAction(state, options, 0, result.votes);
        break;
      case PolicyMode::kMember1:
        action = chooseMemberAction(state, options, 1, result.votes);
        break;
      case PolicyMode::kMember2:
        action = chooseMemberAction(state, options, 2, result.votes);
        break;
      case PolicyMode::kVote:
        action = chooseVoteActionLazy3(state, options, result.votes);
        break;
      case PolicyMode::kQMean:
        action = chooseQMeanAction(state, options, result.votes);
        break;
    }
    if (!drop7::isLegal(state.board, action)) {
      throw std::runtime_error("policy selected an illegal action");
    }
    MoveResult move;
    if (!drop7::playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("engine rejected a legal action");
    }
    for (const auto& wave : move.waves) {
      result.clears += wave.cleared;
      result.reveals += wave.revealed;
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  return result;
}

double mean(const std::vector<GameResult>& games,
            const auto& projection) {
  return std::accumulate(
             games.begin(), games.end(), 0.0,
             [&](double sum, const GameResult& game) {
               return sum + projection(game);
             }) /
         static_cast<double>(games.size());
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
  return 0.0;
}

void printSummary(std::string_view label,
                  const std::vector<GameResult>& games, double seconds) {
  const double moves = mean(games, [](const GameResult& game) {
    return static_cast<double>(game.moves);
  });
  const int total_moves = std::accumulate(
      games.begin(), games.end(), 0,
      [](int sum, const GameResult& game) { return sum + game.moves; });
  const int total_decisions = std::accumulate(
      games.begin(), games.end(), 0, [](int sum, const GameResult& game) {
        return sum + game.votes.decisions;
      });
  const int consensus = std::accumulate(
      games.begin(), games.end(), 0, [](int sum, const GameResult& game) {
        return sum + game.votes.consensus;
      });
  const int switches = std::accumulate(
      games.begin(), games.end(), 0, [](int sum, const GameResult& game) {
        return sum + game.votes.switches;
      });
  std::cout << std::fixed << std::setprecision(3) << label
            << " {\"games\":" << games.size()
            << ",\"meanScore\":"
            << mean(games, [](const GameResult& game) {
                 return static_cast<double>(game.score);
               })
            << ",\"meanMoves\":" << moves
            << ",\"clearsPerMove\":"
            << mean(games, [](const GameResult& game) {
                 return static_cast<double>(game.clears);
               }) /
                   moves
            << ",\"revealsPerMove\":"
            << mean(games, [](const GameResult& game) {
                 return static_cast<double>(game.reveals);
               }) /
                   moves
            << ",\"consensusRate\":"
            << (total_decisions == 0
                    ? 0.0
                    : static_cast<double>(consensus) / total_decisions)
            << ",\"switchRate\":"
            << (total_decisions == 0
                    ? 0.0
                    : static_cast<double>(switches) / total_decisions)
            << ",\"workPerMove\":"
            << mean(games, [](const GameResult& game) {
                 return static_cast<double>(game.votes.work);
               }) /
                   moves
            << ",\"censored\":"
            << std::count_if(games.begin(), games.end(),
                             [](const GameResult& game) {
                               return game.censored;
                             })
            << ",\"seconds\":" << seconds
            << ",\"maxRssMiB\":" << maximumResidentMiB()
            << ",\"scores\":[";
  for (std::size_t index = 0; index < games.size(); ++index) {
    if (index > 0) std::cout << ',';
    std::cout << games[index].score;
  }
  std::cout << "],\"moves\":[";
  for (std::size_t index = 0; index < games.size(); ++index) {
    if (index > 0) std::cout << ',';
    std::cout << games[index].moves;
  }
  std::cout << "]}\n";
  if (total_moves <= 0) throw std::runtime_error("benchmark made no moves");
}

void printPairedComparison(std::string_view label,
                           const std::vector<GameResult>& reference,
                           const std::vector<GameResult>& candidate) {
  if (reference.size() != candidate.size() || reference.empty()) {
    throw std::invalid_argument("paired comparison requires equal games");
  }
  double score_difference = 0.0;
  double move_difference = 0.0;
  int wins = 0;
  int ties = 0;
  int losses = 0;
  for (std::size_t index = 0; index < reference.size(); ++index) {
    score_difference += static_cast<double>(candidate[index].score -
                                            reference[index].score);
    move_difference += static_cast<double>(candidate[index].moves -
                                           reference[index].moves);
    if (candidate[index].score > reference[index].score) {
      ++wins;
    } else if (candidate[index].score < reference[index].score) {
      ++losses;
    } else {
      ++ties;
    }
  }
  std::cout << std::fixed << std::setprecision(3) << label
            << " {\"games\":" << reference.size()
            << ",\"meanScoreDifference\":"
            << score_difference / static_cast<double>(reference.size())
            << ",\"meanMoveDifference\":"
            << move_difference / static_cast<double>(reference.size())
            << ",\"wins\":" << wins << ",\"ties\":" << ties
            << ",\"losses\":" << losses << "}\n";
}

int rootArgmax(const RootValues& root) {
  int action = -1;
  double value = -std::numeric_limits<double>::infinity();
  for (int column : drop7::cfpi::detail::kColumnOrder) {
    if (root.canonical_q[column] > value) {
      value = root.canonical_q[column];
      action = column;
    }
  }
  return action;
}

bool selfTest() {
  Options options;
  options.members = 3;
  options.behavior.max_depth = 2;
  options.behavior.chance_samples = 3;
  options.behavior.max_work = 100'000;
  State state;
  state.board = drop7::initialBoard();
  state.board[drop7::indexOf(5, 1)] = 5;
  state.board[drop7::indexOf(5, 4)] = 6;
  state.next_disc = 4;
  state.moves_remaining = 2;

  auto member0_options = options.behavior;
  member0_options.policy_seed = kPolicySeeds[0];
  drop7::cfpi::BehaviorMetrics exact_metrics;
  const int exact = drop7::cfpi::chooseBehaviorAction(
      state, member0_options, &exact_metrics);
  const RootValues root = searchRootValues(state, member0_options);
  const RootValues repeated_root = searchRootValues(state, member0_options);
  const bool root_exact = root.action == exact &&
                          root.canonical_action == rootArgmax(root) &&
                          root.metrics.completed_depth ==
                              exact_metrics.completed_depth &&
                          root.metrics.work == exact_metrics.work;
  const bool root_deterministic =
      root.action == repeated_root.action &&
      root.canonical_q == repeated_root.canonical_q &&
      root.metrics.work == repeated_root.metrics.work;
  auto bounded_options = member0_options;
  bounded_options.max_work = 1;
  drop7::cfpi::BehaviorMetrics bounded_exact_metrics;
  const int bounded_exact = drop7::cfpi::chooseBehaviorAction(
      state, bounded_options, &bounded_exact_metrics);
  const RootValues bounded_root = searchRootValues(state, bounded_options);
  const bool partial_depth_discarded =
      bounded_root.action == bounded_exact &&
      bounded_root.metrics.completed_depth ==
          bounded_exact_metrics.completed_depth &&
      bounded_root.metrics.work == bounded_exact_metrics.work &&
      std::none_of(bounded_root.canonical_q.begin(),
                   bounded_root.canonical_q.end(),
                   [](double value) { return std::isfinite(value); });

  VoteMetrics first_metrics;
  VoteMetrics repeat_metrics;
  const int first = chooseQMeanAction(state, options, first_metrics);
  const int repeat = chooseQMeanAction(state, options, repeat_metrics);
  State mirrored = state;
  mirrored.board = drop7::cfpi::detail::mirrorBoard(state.board);
  VoteMetrics mirror_metrics;
  const int mirror = chooseQMeanAction(mirrored, options, mirror_metrics);
  State metadata = state;
  metadata.score = 999'999;
  metadata.level = 99;
  metadata.moves_played = 333;
  VoteMetrics metadata_metrics;
  const int metadata_action =
      chooseQMeanAction(metadata, options, metadata_metrics);

  Options lazy_options = options;
  lazy_options.behavior.max_depth = 1;
  lazy_options.behavior.chance_samples = 1;
  State empty;
  empty.board = drop7::initialBoard();
  empty.next_disc = 4;
  empty.moves_remaining = 5;
  VoteMetrics eager_metrics;
  VoteMetrics lazy_metrics;
  const int eager =
      chooseVoteActionEager(empty, lazy_options, eager_metrics);
  const int lazy =
      chooseVoteActionLazy3(empty, lazy_options, lazy_metrics);
  const bool lazy_equivalent = eager == lazy &&
                               eager_metrics.decisions ==
                                   lazy_metrics.decisions &&
                               eager_metrics.consensus ==
                                   lazy_metrics.consensus &&
                               eager_metrics.switches ==
                                   lazy_metrics.switches;
  const bool lazy_saves_work = lazy_metrics.work < eager_metrics.work;

  const bool passed = root_exact && root_deterministic &&
                      partial_depth_discarded &&
                      first == repeat && drop7::isLegal(state.board, first) &&
                      mirror == drop7::kBoardSize - 1 - first &&
                      metadata_action == first &&
                      first_metrics.work == repeat_metrics.work &&
                      lazy_equivalent && lazy_saves_work;
  std::cout << "ENSEMBLE_SELF_TEST {\"rootArgmaxExact\":"
            << (root_exact ? "true" : "false")
            << ",\"rootDeterministic\":"
            << (root_deterministic ? "true" : "false")
            << ",\"partialDepthDiscarded\":"
            << (partial_depth_discarded ? "true" : "false")
            << ",\"qmeanDeterministic\":"
            << (first == repeat ? "true" : "false")
            << ",\"legal\":"
            << (drop7::isLegal(state.board, first) ? "true" : "false")
            << ",\"mirror\":"
            << (mirror == drop7::kBoardSize - 1 - first ? "true" : "false")
            << ",\"metadataBlind\":"
            << (metadata_action == first ? "true" : "false")
            << ",\"lazyVoteEquivalent\":"
            << (lazy_equivalent ? "true" : "false")
            << ",\"lazyVoteSavesWork\":"
            << (lazy_saves_work ? "true" : "false")
            << ",\"passed\":" << (passed ? "true" : "false") << "}\n";
  return passed;
}

int run(int argc, char** argv) {
  if (hasFlag(argc, argv, "--self-test")) return selfTest() ? 0 : 1;
  Options options;
  options.games = parsePositive(valueAfter(argc, argv, "--games", "4"),
                                "--games");
  options.max_moves = parsePositive(
      valueAfter(argc, argv, "--max-moves", "1000"), "--max-moves");
  options.members = parsePositive(
      valueAfter(argc, argv, "--members", "3"), "--members");
  if (options.members != 3 && options.members != 5) {
    throw std::invalid_argument("--members must be 3 or 5");
  }
  const std::string mode = valueAfter(argc, argv, "--mode", "vote");
  if (mode != "vote" && mode != "qmean" && mode != "screen") {
    throw std::invalid_argument("--mode must be vote, qmean, or screen");
  }
  if (mode != "vote" && options.members != 3) {
    throw std::invalid_argument("qmean and screen modes require 3 members");
  }
  options.seed_start =
      parseSeed(valueAfter(argc, argv, "--seed-start", "0x3d707000"));
  if (options.seed_start < 0x3d70'0000u ||
      static_cast<std::uint64_t>(options.seed_start) + options.games >
          0x3d71'0000ull) {
    throw std::invalid_argument("benchmark seeds must stay in 0x3d70xxxx");
  }

  struct Series {
    std::string label;
    PolicyMode policy;
    std::vector<GameResult> games;
    double seconds = 0.0;
  };
  std::vector<Series> series;
  if (mode == "vote") {
    series.push_back({"ENSEMBLE_BASELINE", PolicyMode::kExact, {}});
    series.push_back({"ENSEMBLE_CANDIDATE", PolicyMode::kVote, {}});
  } else if (mode == "qmean") {
    series.push_back({"QMEAN_VOTE", PolicyMode::kVote, {}});
    series.push_back({"QMEAN_CANDIDATE", PolicyMode::kQMean, {}});
  } else {
    series.push_back({"SCREEN_EXACT", PolicyMode::kExact, {}});
    series.push_back({"SCREEN_MEMBER1", PolicyMode::kMember1, {}});
    series.push_back({"SCREEN_MEMBER2", PolicyMode::kMember2, {}});
    series.push_back({"SCREEN_VOTE", PolicyMode::kVote, {}});
    series.push_back({"SCREEN_QMEAN", PolicyMode::kQMean, {}});
  }

  for (Series& item : series) {
    item.games.reserve(options.games);
    const auto started = Clock::now();
    for (int game = 0; game < options.games; ++game) {
      const std::uint32_t seed =
          options.seed_start + static_cast<std::uint32_t>(game);
      item.games.push_back(runGame(seed, options, item.policy));
      std::cerr << item.label << ' ' << (game + 1) << '/' << options.games
                << " 0x" << std::hex << seed << std::dec << ' '
                << item.games.back().score << '/'
                << item.games.back().moves << '\n';
    }
    item.seconds =
        std::chrono::duration<double>(Clock::now() - started).count();
    printSummary(item.label, item.games, item.seconds);
  }

  if (mode == "vote") {
    printPairedComparison("ENSEMBLE_CANDIDATE_VS_BASELINE",
                          series[0].games, series[1].games);
  } else if (mode == "qmean") {
    printPairedComparison("QMEAN_VS_VOTE", series[0].games,
                          series[1].games);
  } else {
    printPairedComparison("SCREEN_MEMBER1_VS_EXACT", series[0].games,
                          series[1].games);
    printPairedComparison("SCREEN_MEMBER2_VS_EXACT", series[0].games,
                          series[2].games);
    printPairedComparison("SCREEN_VOTE_VS_EXACT", series[0].games,
                          series[3].games);
    printPairedComparison("SCREEN_QMEAN_VS_EXACT", series[0].games,
                          series[4].games);
    printPairedComparison("SCREEN_QMEAN_VS_VOTE", series[3].games,
                          series[4].games);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "drop7_policy_ensemble: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
