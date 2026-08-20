#include "../../../src/core/native/public-behavior.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <vector>

namespace drop7::exact_depth4 {

constexpr std::uint32_t kScreenSeedStart = 0x3d70'e000u;
constexpr std::uint32_t kConfirmationSeedStart = 0x3d70'e100u;
constexpr int kScreenGames = 4;
constexpr int kConfirmationGames = 8;
constexpr int kMaximumMoves = 500;
constexpr int kParallelism = 4;
constexpr int kChanceSamples = 5;
constexpr int kBaselineDepth = 3;
constexpr int kCandidateDepth = 4;
constexpr std::uint64_t kMaximumWork = 3'200'000;
constexpr std::size_t kMaximumCacheEntries = 60'000;

static_assert(kLevelBonus == 7'000);

constexpr std::uint64_t power(std::uint64_t base, int exponent) {
  std::uint64_t result = 1;
  for (int count = 0; count < exponent; ++count) result *= base;
  return result;
}

constexpr std::uint64_t worstCaseIterativeWork(int maximum_depth) {
  constexpr std::uint64_t branches = kBoardSize * kChanceSamples;
  std::uint64_t result = 0;
  for (int depth = 1; depth <= maximum_depth; ++depth) {
    for (int level = 1; level <= depth; ++level) {
      result += power(branches, level);
    }
    result += power(branches, depth);  // Leaf evaluations.
  }
  return result;
}

constexpr std::uint64_t worstCaseIterativeCacheEntries(int maximum_depth) {
  constexpr std::uint64_t branches = kBoardSize * kChanceSamples;
  std::uint64_t result = 0;
  for (int depth = 2; depth <= maximum_depth; ++depth) {
    for (int level = 1; level < depth; ++level) {
      result += power(branches, level);
    }
  }
  return result;
}

constexpr std::uint64_t kWorstCaseD4Work =
    worstCaseIterativeWork(kCandidateDepth);
constexpr std::uint64_t kWorstCaseD4CacheEntries =
    worstCaseIterativeCacheEntries(kCandidateDepth);
static_assert(kWorstCaseD4Work == 3'134'950);
static_assert(kWorstCaseD4CacheEntries == 45'430);
static_assert(kMaximumWork > kWorstCaseD4Work);
static_assert(kMaximumCacheEntries > kWorstCaseD4CacheEntries);

std::mutex progress_mutex;

std::uint64_t peakRssBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
#endif
}

cfpi::BehaviorOptions behaviorOptions(int depth) {
  cfpi::BehaviorOptions options;
  options.max_depth = depth;
  options.chance_samples = kChanceSamples;
  options.max_work = kMaximumWork;
  options.max_cache_entries = kMaximumCacheEntries;
  return options;
}

struct GameResult {
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::uint64_t depth_switches = 0;
  std::size_t peak_cache_entries = 0;
  std::uint64_t peak_rss_bytes = 0;
  double elapsed_seconds = 0.0;
};

GameResult runGame(std::uint32_t seed, int depth,
                   std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  const cfpi::BehaviorOptions options = behaviorOptions(depth);
  State state = initialHeadlessState(seed);
  GameResult result;
  int previous_action = -1;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    cfpi::BehaviorMetrics metrics;
    const int action = cfpi::chooseBehaviorAction(state, options, &metrics);
    if (metrics.completed_depth != depth || !metrics.complete) {
      throw std::runtime_error(
          "exact search failed its completed-depth assertion");
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("exact search selected an illegal action");
    }
    if (previous_action >= 0 && previous_action != action) {
      ++result.depth_switches;
    }
    previous_action = action;
    result.work += metrics.work;
    result.nodes += metrics.nodes;
    result.cache_hits += metrics.cache_hits;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, metrics.cache_entries);
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("exact search transition failed");
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.peak_rss_bytes = peakRssBytes();
  result.elapsed_seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
  {
    const std::lock_guard<std::mutex> lock(progress_mutex);
    std::cerr << label << " seed 0x" << std::hex << seed << std::dec << ' '
              << result.score << " (" << result.moves << " moves, work "
              << result.work << ", cache " << result.peak_cache_entries
              << ")\n";
  }
  return result;
}

struct Cohort {
  std::vector<GameResult> depth3;
  std::vector<GameResult> depth4;
};

Cohort runCohort(std::uint32_t seed_start, int games,
                 std::string_view phase) {
  Cohort cohort;
  cohort.depth3.resize(static_cast<std::size_t>(games));
  cohort.depth4.resize(static_cast<std::size_t>(games));
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  const int worker_count = std::min(kParallelism, games);
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= games) return;
        const std::uint32_t seed =
            seed_start + static_cast<std::uint32_t>(game);
        cohort.depth3[static_cast<std::size_t>(game)] = runGame(
            seed, kBaselineDepth, std::string(phase) + "-exact-d3");
        cohort.depth4[static_cast<std::size_t>(game)] = runGame(
            seed, kCandidateDepth, std::string(phase) + "-exact-d4");
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return cohort;
}

struct Summary {
  int games = 0;
  int completed_depth = 0;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double work_per_move = 0.0;
  double nodes_per_move = 0.0;
  double cache_hits_per_move = 0.0;
  double moves_per_game_second = 0.0;
  double work_per_game_second = 0.0;
  double aggregate_game_seconds = 0.0;
  double switches_per_move = 0.0;
  int censored = 0;
  std::size_t peak_cache_entries = 0;
  std::uint64_t peak_rss_bytes = 0;
};

Summary summarize(const std::vector<GameResult>& games,
                  int completed_depth) {
  if (games.empty()) throw std::invalid_argument("empty exact cohort");
  Summary result;
  result.games = static_cast<int>(games.size());
  result.completed_depth = completed_depth;
  std::uint64_t moves = 0;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::uint64_t switches = 0;
  for (const GameResult& game : games) {
    result.mean_score += static_cast<double>(game.score) / games.size();
    result.mean_moves += static_cast<double>(game.moves) / games.size();
    moves += game.moves;
    work += game.work;
    nodes += game.nodes;
    cache_hits += game.cache_hits;
    switches += game.depth_switches;
    result.aggregate_game_seconds += game.elapsed_seconds;
    result.censored += game.censored;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, game.peak_cache_entries);
    result.peak_rss_bytes =
        std::max(result.peak_rss_bytes, game.peak_rss_bytes);
  }
  const double move_count =
      static_cast<double>(std::max<std::uint64_t>(1, moves));
  result.work_per_move = work / move_count;
  result.nodes_per_move = nodes / move_count;
  result.cache_hits_per_move = cache_hits / move_count;
  result.switches_per_move = switches / move_count;
  result.moves_per_game_second =
      move_count / std::max(1.0e-9, result.aggregate_game_seconds);
  result.work_per_game_second =
      work / std::max(1.0e-9, result.aggregate_game_seconds);
  return result;
}

struct PairedSummary {
  double mean_score_difference = 0.0;
  double mean_move_difference = 0.0;
  int wins = 0;
  int ties = 0;
  int losses = 0;
};

PairedSummary pairedSummary(const Cohort& cohort) {
  if (cohort.depth3.size() != cohort.depth4.size() ||
      cohort.depth3.empty()) {
    throw std::invalid_argument("exact cohort is not paired");
  }
  PairedSummary result;
  for (std::size_t game = 0; game < cohort.depth3.size(); ++game) {
    const GameResult& depth3 = cohort.depth3[game];
    const GameResult& depth4 = cohort.depth4[game];
    result.mean_score_difference +=
        static_cast<double>(depth4.score - depth3.score) /
        cohort.depth3.size();
    result.mean_move_difference +=
        static_cast<double>(depth4.moves - depth3.moves) /
        cohort.depth3.size();
    if (depth4.score > depth3.score) {
      ++result.wins;
    } else if (depth4.score < depth3.score) {
      ++result.losses;
    } else {
      ++result.ties;
    }
  }
  return result;
}

void writeSummary(std::ostream& output, const Summary& result) {
  output << "{\"games\":" << result.games
         << ",\"meanScore\":" << result.mean_score
         << ",\"meanMoves\":" << result.mean_moves
         << ",\"completedDepth\":" << result.completed_depth
         << ",\"workPerMove\":" << result.work_per_move
         << ",\"nodesPerMove\":" << result.nodes_per_move
         << ",\"cacheHitsPerMove\":" << result.cache_hits_per_move
         << ",\"movesPerGameSecond\":" << result.moves_per_game_second
         << ",\"workPerGameSecond\":" << result.work_per_game_second
         << ",\"aggregateGameSeconds\":"
         << result.aggregate_game_seconds
         << ",\"switchesPerMove\":" << result.switches_per_move
         << ",\"peakCacheEntries\":" << result.peak_cache_entries
         << ",\"peakRssBytes\":" << result.peak_rss_bytes
         << ",\"censored\":" << result.censored << '}';
}

void writePaired(std::ostream& output, const PairedSummary& result) {
  output << "{\"meanScoreDifference\":" << result.mean_score_difference
         << ",\"meanMoveDifference\":" << result.mean_move_difference
         << ",\"wins\":" << result.wins << ",\"ties\":"
         << result.ties << ",\"losses\":" << result.losses << '}';
}

void writeTrajectories(std::ostream& output,
                       const std::vector<GameResult>& games) {
  output << "{\"scores\":[";
  for (std::size_t game = 0; game < games.size(); ++game) {
    if (game > 0) output << ',';
    output << games[game].score;
  }
  output << "],\"moves\":[";
  for (std::size_t game = 0; game < games.size(); ++game) {
    if (game > 0) output << ',';
    output << games[game].moves;
  }
  output << "]}";
}

bool selfTest(std::ostream& output) {
  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(4, 1)] = 2;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;
  const cfpi::BehaviorOptions options = behaviorOptions(kCandidateDepth);
  cfpi::BehaviorMetrics first_metrics;
  cfpi::BehaviorMetrics repeat_metrics;
  const int first =
      cfpi::chooseBehaviorAction(state, options, &first_metrics);
  const int repeat =
      cfpi::chooseBehaviorAction(state, options, &repeat_metrics);
  State mirrored = state;
  mirrored.board = cfpi::detail::mirrorBoard(state.board);
  cfpi::BehaviorMetrics reflected_metrics;
  const int reflected =
      cfpi::chooseBehaviorAction(mirrored, options, &reflected_metrics);
  State metadata = state;
  metadata.score = 999'999;
  metadata.level = 88;
  metadata.moves_played = 321;
  const int metadata_action = cfpi::chooseBehaviorAction(metadata, options);
  const bool completed = first_metrics.completed_depth == kCandidateDepth &&
                         first_metrics.complete;
  const bool deterministic = first == repeat &&
                             first_metrics.work == repeat_metrics.work;
  const bool reflection_safe =
      reflected == kBoardSize - 1 - first &&
      reflected_metrics.completed_depth == kCandidateDepth;
  const bool public_state_only = metadata_action == first;
  const bool bounded = first_metrics.work <= kMaximumWork &&
                       first_metrics.cache_entries <= kMaximumCacheEntries;
  const bool legal = isLegal(state.board, first);
  const bool theoretical_completion =
      kMaximumWork > kWorstCaseD4Work &&
      kMaximumCacheEntries > kWorstCaseD4CacheEntries;
  const bool passed = completed && deterministic && reflection_safe &&
                      public_state_only && bounded && legal &&
                      theoretical_completion;
  output << "EXACT_DEPTH4_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"completedD4\":" << (completed ? "true" : "false")
         << ",\"deterministic\":"
         << (deterministic ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe ? "true" : "false")
         << ",\"publicStateOnly\":"
         << (public_state_only ? "true" : "false")
         << ",\"bounded\":" << (bounded ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false")
         << ",\"canonicalLevelBonus\":" << kLevelBonus
         << ",\"predeclaredMaxWork\":" << kMaximumWork
         << ",\"worstCaseWork\":" << kWorstCaseD4Work
         << ",\"predeclaredMaxCache\":" << kMaximumCacheEntries
         << ",\"worstCaseCache\":" << kWorstCaseD4CacheEntries
         << "}\n";
  return passed;
}

struct ProgramOptions {
  std::string output = "/tmp/drop7-exact-depth4.json";
};

ProgramOptions parseOptions(int argc, char** argv, int first_argument) {
  ProgramOptions options;
  for (int index = first_argument; index < argc; ++index) {
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing option value");
    }
    const std::string argument = argv[index++];
    if (argument == "--output") {
      options.output = argv[index];
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  return options;
}

int benchmark(const ProgramOptions& options, std::ostream& output) {
  const auto started = std::chrono::steady_clock::now();
  const Cohort screen = runCohort(kScreenSeedStart, kScreenGames, "screen");
  const Summary screen_depth3 = summarize(screen.depth3, kBaselineDepth);
  const Summary screen_depth4 = summarize(screen.depth4, kCandidateDepth);
  const PairedSummary screen_paired = pairedSummary(screen);
  const bool screen_passed = screen_paired.mean_score_difference > 0.0 &&
                             screen_paired.mean_move_difference > 0.0;

  Cohort confirmation;
  Summary confirmation_depth3;
  Summary confirmation_depth4;
  PairedSummary confirmation_paired;
  bool confirmed = false;
  if (screen_passed) {
    confirmation = runCohort(kConfirmationSeedStart, kConfirmationGames,
                             "confirmation");
    confirmation_depth3 =
        summarize(confirmation.depth3, kBaselineDepth);
    confirmation_depth4 =
        summarize(confirmation.depth4, kCandidateDepth);
    confirmation_paired = pairedSummary(confirmation);
    confirmed = confirmation_paired.mean_score_difference > 0.0 &&
                confirmation_paired.mean_move_difference > 0.0;
  }
  const double elapsed_seconds = std::chrono::duration<double>(
                                     std::chrono::steady_clock::now() -
                                     started)
                                     .count();
  std::ofstream artifact(options.output);
  if (!artifact) throw std::runtime_error("could not open d4 artifact");
  artifact << std::setprecision(10)
           << "{\n  \"format\": \"drop7-exact-depth4-v1\",\n"
           << "  \"trainingSeedOnly\": true,\n"
           << "  \"publicStateOnly\": true,\n"
           << "  \"levelBonus\": " << kLevelBonus << ",\n"
           << "  \"chanceSamples\": " << kChanceSamples << ",\n"
           << "  \"fullWidth\": true,\n"
           << "  \"phaseLeaf\": true,\n"
           << "  \"maximumWork\": " << kMaximumWork << ",\n"
           << "  \"worstCaseD4Work\": " << kWorstCaseD4Work << ",\n"
           << "  \"maximumCacheEntries\": " << kMaximumCacheEntries
           << ",\n  \"worstCaseD4CacheEntries\": "
           << kWorstCaseD4CacheEntries
           << ",\n  \"maximumMoves\": " << kMaximumMoves
           << ",\n  \"parallelism\": " << kParallelism
           << ",\n  \"screenSeedStart\": " << kScreenSeedStart
           << ",\n  \"screen\": {\"exactD3\":";
  writeSummary(artifact, screen_depth3);
  artifact << ",\"exactD4\":";
  writeSummary(artifact, screen_depth4);
  artifact << ",\"paired\":";
  writePaired(artifact, screen_paired);
  artifact << ",\"d3Trajectories\":";
  writeTrajectories(artifact, screen.depth3);
  artifact << ",\"d4Trajectories\":";
  writeTrajectories(artifact, screen.depth4);
  artifact << "},\n  \"screenPassed\": "
           << (screen_passed ? "true" : "false")
           << ",\n  \"confirmation\": ";
  if (!screen_passed) {
    artifact << "null";
  } else {
    artifact << "{\"seedStart\":" << kConfirmationSeedStart
             << ",\"exactD3\":";
    writeSummary(artifact, confirmation_depth3);
    artifact << ",\"exactD4\":";
    writeSummary(artifact, confirmation_depth4);
    artifact << ",\"paired\":";
    writePaired(artifact, confirmation_paired);
    artifact << ",\"d3Trajectories\":";
    writeTrajectories(artifact, confirmation.depth3);
    artifact << ",\"d4Trajectories\":";
    writeTrajectories(artifact, confirmation.depth4);
    artifact << '}';
  }
  artifact << ",\n  \"confirmed\": " << (confirmed ? "true" : "false")
           << ",\n  \"decision\": \""
           << (!screen_passed
                   ? "reject-screen"
                   : (confirmed ? "advance" : "reject-confirmation"))
           << "\",\n  \"elapsedSeconds\": " << elapsed_seconds << "\n}\n";

  output << std::fixed << std::setprecision(3)
         << "EXACT_DEPTH4_RESULT {\"screenD3Score\":"
         << screen_depth3.mean_score
         << ",\"screenD3Moves\":" << screen_depth3.mean_moves
         << ",\"screenD4Score\":" << screen_depth4.mean_score
         << ",\"screenD4Moves\":" << screen_depth4.mean_moves
         << ",\"screenScoreDifference\":"
         << screen_paired.mean_score_difference
         << ",\"screenMoveDifference\":"
         << screen_paired.mean_move_difference
         << ",\"screenPassed\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmationRan\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmed\":" << (confirmed ? "true" : "false")
         << ",\"decision\":\""
         << (!screen_passed
                 ? "reject-screen"
                 : (confirmed ? "advance" : "reject-confirmation"))
         << "\",\"elapsedSeconds\":" << elapsed_seconds
         << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return 0;
}

}  // namespace drop7::exact_depth4

int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string(argv[1]) == "--self-test") {
      return drop7::exact_depth4::selfTest(std::cout) ? 0 : 1;
    }
    if (argc >= 2 && std::string(argv[1]) == "--benchmark") {
      const auto options = drop7::exact_depth4::parseOptions(argc, argv, 2);
      return drop7::exact_depth4::benchmark(options, std::cout);
    }
    std::cerr << "usage: drop7_exact_depth4 --self-test | "
                 "--benchmark [--output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_exact_depth4: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
