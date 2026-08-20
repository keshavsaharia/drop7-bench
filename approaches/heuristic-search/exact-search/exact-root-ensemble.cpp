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
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <thread>
#include <utility>
#include <vector>

// A causal test of chance-estimation noise at the root. Every member is the
// same full-width exact-depth-three expectimax policy. Members differ only in
// their deterministic stratified chance salt, and every legal root value is
// averaged before selecting an action.
namespace drop7::exact_root_ensemble {

using Clock = std::chrono::steady_clock;

constexpr int kDepth = 3;
constexpr int kChanceSamples = 5;
constexpr std::uint64_t kMaximumWork = 1'000'000;
constexpr std::size_t kMaximumCacheEntries = 40'000;
constexpr int kMaximumMoves = 500;
constexpr std::array<std::uint32_t, 3> kPolicySeeds{{
    0xd707'5eedu,
    0x91e1'0da5u,
    0x6a09'e667u,
}};
constexpr std::uint32_t kScreenStart = 0x3d70'd000u;
constexpr std::uint32_t kConfirmationStart = 0x3d70'd100u;
constexpr std::uint32_t kTrainingEnd = 0x4d00'0000u;
constexpr std::array<int, kBoardSize> kColumnOrder{{3, 2, 4, 1, 5, 0, 6}};

std::mutex progress_mutex;

struct Decision {
  int action = -1;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
};

Decision chooseAction(const State& source, int members) {
  if (members < 1 || members > static_cast<int>(kPolicySeeds.size())) {
    throw std::invalid_argument("exact ensemble member count is invalid");
  }
  if (source.game_over) return {};

  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  std::array<double, kBoardSize> sums{};
  std::array<int, kBoardSize> counts{};
  sums.fill(0.0);
  Decision result;

  for (int member = 0; member < members; ++member) {
    cfpi::BehaviorOptions options;
    options.max_depth = kDepth;
    options.chance_samples = kChanceSamples;
    options.max_work = kMaximumWork;
    options.max_cache_entries = kMaximumCacheEntries;
    options.policy_seed = kPolicySeeds[member];
    cfpi::detail::SearchContext context(options);
    for (const int column : kColumnOrder) {
      if (!isLegal(canonical.board, column)) continue;
      const double value =
          cfpi::detail::evaluateAction(canonical, column, kDepth, context);
      if (!std::isfinite(value)) {
        throw std::runtime_error("exact ensemble produced non-finite root Q");
      }
      sums[column] += value;
      ++counts[column];
    }
    result.work += context.work;
    result.nodes += context.nodes;
    result.cache_hits += context.cache_hits;
  }

  int winner = -1;
  double winning_value = -std::numeric_limits<double>::infinity();
  for (const int column : kColumnOrder) {
    if (!isLegal(canonical.board, column)) continue;
    if (counts[column] != members) {
      throw std::logic_error("exact ensemble did not complete every legal root");
    }
    const double value = sums[column] / members;
    if (value > winning_value) {
      winning_value = value;
      winner = column;
    }
  }
  if (winner < 0) winner = centerFirstMove(canonical.board);
  result.action = mirrored ? kBoardSize - 1 - winner : winner;
  return result;
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
  int clears = 0;
  int reveals = 0;
  bool censored = false;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  double seconds = 0;
};

Game runGame(std::uint32_t seed, int members, std::string_view label) {
  const auto started = Clock::now();
  State state = initialHeadlessState(seed);
  Game result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    const Decision decision = chooseAction(state, members);
    if (!isLegal(state.board, decision.action)) {
      throw std::runtime_error("exact ensemble selected an illegal move");
    }
    result.work += decision.work;
    result.nodes += decision.nodes;
    result.cache_hits += decision.cache_hits;
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("exact ensemble transition failed");
    }
    for (const Wave& wave : move.waves) {
      result.clears += wave.cleared;
      result.reveals += wave.revealed;
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
              << result.score << " (" << result.moves << " moves, work "
              << result.work << ")\n";
  }
  return result;
}

struct Summary {
  int games = 0;
  double mean_score = 0;
  double mean_moves = 0;
  double clears_per_move = 0;
  double reveals_per_move = 0;
  double work_per_move = 0;
  double nodes_per_move = 0;
  double cache_hits_per_move = 0;
  double aggregate_seconds = 0;
  int censored = 0;
  std::uint64_t peak_rss_bytes = 0;
};

Summary summarize(const std::vector<Game>& games) {
  if (games.empty()) throw std::invalid_argument("empty exact ensemble cohort");
  Summary result;
  result.games = static_cast<int>(games.size());
  double scores = 0;
  double moves = 0;
  double clears = 0;
  double reveals = 0;
  double work = 0;
  double nodes = 0;
  double cache_hits = 0;
  for (const Game& game : games) {
    scores += game.score;
    moves += game.moves;
    clears += game.clears;
    reveals += game.reveals;
    work += static_cast<double>(game.work);
    nodes += static_cast<double>(game.nodes);
    cache_hits += static_cast<double>(game.cache_hits);
    result.aggregate_seconds += game.seconds;
    result.censored += game.censored;
  }
  result.mean_score = scores / games.size();
  result.mean_moves = moves / games.size();
  result.clears_per_move = clears / moves;
  result.reveals_per_move = reveals / moves;
  result.work_per_move = work / moves;
  result.nodes_per_move = nodes / moves;
  result.cache_hits_per_move = cache_hits / moves;
  result.peak_rss_bytes = peakRssBytes();
  return result;
}

struct Paired {
  double mean_score_delta = 0;
  double mean_move_delta = 0;
  int wins = 0;
  int ties = 0;
  int losses = 0;
};

Paired paired(const std::vector<Game>& baseline,
              const std::vector<Game>& candidate) {
  if (baseline.size() != candidate.size() || baseline.empty()) {
    throw std::invalid_argument("invalid paired exact ensemble cohorts");
  }
  Paired result;
  for (std::size_t index = 0; index < baseline.size(); ++index) {
    const auto score_delta = candidate[index].score - baseline[index].score;
    const int move_delta = candidate[index].moves - baseline[index].moves;
    result.mean_score_delta += score_delta;
    result.mean_move_delta += move_delta;
    if (move_delta > 0) ++result.wins;
    else if (move_delta < 0) ++result.losses;
    else ++result.ties;
  }
  result.mean_score_delta /= baseline.size();
  result.mean_move_delta /= baseline.size();
  return result;
}

struct Cohort {
  std::vector<Game> baseline;
  std::vector<Game> candidate;
  double wall_seconds = 0;
};

Cohort runCohort(std::uint32_t start, int games, int threads,
                 std::string_view phase) {
  Cohort result;
  result.baseline.resize(games);
  result.candidate.resize(games);
  std::atomic<int> next{0};
  std::atomic<bool> failed{false};
  std::mutex error_mutex;
  std::string error_message;
  const auto started = Clock::now();
  std::vector<std::thread> workers;
  for (int worker = 0; worker < std::min(threads, games); ++worker) {
    workers.emplace_back([&]() {
      while (!failed.load(std::memory_order_relaxed)) {
        const int index = next.fetch_add(1, std::memory_order_relaxed);
        if (index >= games) break;
        try {
          const std::uint32_t seed = start + static_cast<std::uint32_t>(index);
          result.baseline[index] =
              runGame(seed, 1, std::string(phase) + "-exact-d3");
          result.candidate[index] =
              runGame(seed, 3, std::string(phase) + "-ensemble-3");
        } catch (const std::exception& error) {
          failed.store(true, std::memory_order_relaxed);
          const std::lock_guard<std::mutex> lock(error_mutex);
          error_message = error.what();
        }
      }
    });
  }
  for (std::thread& worker : workers) worker.join();
  if (failed.load()) {
    throw std::runtime_error("exact ensemble worker failed: " + error_message);
  }
  result.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"games\":" << summary.games
         << ",\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"workPerMove\":" << summary.work_per_move
         << ",\"nodesPerMove\":" << summary.nodes_per_move
         << ",\"cacheHitsPerMove\":" << summary.cache_hits_per_move
         << ",\"aggregateSeconds\":" << summary.aggregate_seconds
         << ",\"censored\":" << summary.censored
         << ",\"peakRssBytes\":" << summary.peak_rss_bytes << '}';
}

void writeGames(std::ostream& output, const std::vector<Game>& games) {
  output << '[';
  for (std::size_t index = 0; index < games.size(); ++index) {
    if (index != 0) output << ',';
    output << "{\"seed\":" << games[index].seed
           << ",\"score\":" << games[index].score
           << ",\"moves\":" << games[index].moves
           << ",\"censored\":" << (games[index].censored ? "true" : "false")
           << '}';
  }
  output << ']';
}

void writeCohort(std::ostream& output, const Cohort& cohort) {
  const Summary baseline = summarize(cohort.baseline);
  const Summary candidate = summarize(cohort.candidate);
  const Paired comparison = paired(cohort.baseline, cohort.candidate);
  output << "{\"baseline\":";
  writeSummary(output, baseline);
  output << ",\"candidate\":";
  writeSummary(output, candidate);
  output << ",\"paired\":{\"meanScoreDelta\":"
         << comparison.mean_score_delta << ",\"meanMoveDelta\":"
         << comparison.mean_move_delta << ",\"wins\":" << comparison.wins
         << ",\"ties\":" << comparison.ties << ",\"losses\":"
         << comparison.losses << "},\"baselineGames\":";
  writeGames(output, cohort.baseline);
  output << ",\"candidateGames\":";
  writeGames(output, cohort.candidate);
  output << ",\"wallSeconds\":" << cohort.wall_seconds << '}';
}

bool selfTest(std::ostream& output) {
  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;
  const Decision first = chooseAction(state, 3);
  const Decision repeat = chooseAction(state, 3);
  cfpi::BehaviorOptions baseline_options;
  baseline_options.max_depth = kDepth;
  baseline_options.chance_samples = kChanceSamples;
  baseline_options.max_work = kMaximumWork;
  baseline_options.max_cache_entries = kMaximumCacheEntries;
  baseline_options.policy_seed = kPolicySeeds[0];
  const int baseline_action =
      cfpi::chooseBehaviorAction(state, baseline_options);
  const Decision direct_baseline = chooseAction(state, 1);
  State reflected = state;
  reflected.board = cfpi::detail::mirrorBoard(state.board);
  const Decision mirror = chooseAction(reflected, 3);
  State metadata = state;
  metadata.score = 999'999;
  metadata.level = 77;
  metadata.moves_played = 321;
  const Decision metadata_decision = chooseAction(metadata, 3);
  const bool deterministic = first.action == repeat.action &&
                             first.work == repeat.work &&
                             first.nodes == repeat.nodes;
  const bool reflection_safe =
      mirror.action == kBoardSize - 1 - first.action;
  const bool public_state_only = metadata_decision.action == first.action;
  const bool legal = isLegal(state.board, first.action);
  const bool bounded = first.work <= kPolicySeeds.size() * kMaximumWork;
  const bool canonical_score = kLevelBonus == 7'000;
  const bool baseline_parity = direct_baseline.action == baseline_action;
  const bool passed = deterministic && reflection_safe && public_state_only &&
                      legal && bounded && canonical_score && baseline_parity;
  output << "{\"passed\":" << (passed ? "true" : "false")
         << ",\"deterministic\":" << (deterministic ? "true" : "false")
         << ",\"reflectionSafe\":" << (reflection_safe ? "true" : "false")
         << ",\"publicStateOnly\":" << (public_state_only ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false")
         << ",\"bounded\":" << (bounded ? "true" : "false")
         << ",\"canonicalScore\":" << (canonical_score ? "true" : "false")
         << ",\"baselineParity\":" << (baseline_parity ? "true" : "false")
         << ",\"action\":" << first.action << ",\"work\":" << first.work
         << "}\n";
  return passed;
}

int run(int argc, char** argv) {
  int threads = 4;
  std::string output_path = "/tmp/drop7-exact-root-ensemble.json";
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--threads" && index + 1 < argc) {
      threads = std::stoi(argv[++index]);
    } else if (argument == "--output" && index + 1 < argc) {
      output_path = argv[++index];
    } else {
      throw std::invalid_argument("unknown exact ensemble option");
    }
  }
  if (threads < 1 || threads > 64) {
    throw std::invalid_argument("exact ensemble threads must be from 1 to 64");
  }
  if (kConfirmationStart + 8u > kTrainingEnd) {
    throw std::logic_error("exact ensemble cohort leaves training partition");
  }

  const Cohort screen = runCohort(kScreenStart, 4, threads, "screen");
  const Summary screen_baseline = summarize(screen.baseline);
  const Summary screen_candidate = summarize(screen.candidate);
  const bool screen_passed =
      screen_candidate.mean_score > screen_baseline.mean_score &&
      screen_candidate.mean_moves > screen_baseline.mean_moves;

  Cohort confirmation;
  if (screen_passed) {
    confirmation =
        runCohort(kConfirmationStart, 8, threads, "confirmation");
  }

  std::ofstream output(output_path);
  if (!output) throw std::runtime_error("could not write exact ensemble artifact");
  output << std::fixed << std::setprecision(8)
         << "{\n  \"format\":\"drop7-exact-root-ensemble-v1\",\n"
         << "  \"canonicalLevelBonus\":" << kLevelBonus << ",\n"
         << "  \"publicStateOnly\":true,\n"
         << "  \"rootCompleteness\":\"all-legal-actions\",\n"
         << "  \"depth\":" << kDepth << ",\n"
         << "  \"chanceSamplesPerMember\":" << kChanceSamples << ",\n"
         << "  \"policySeeds\":[" << kPolicySeeds[0] << ','
         << kPolicySeeds[1] << ',' << kPolicySeeds[2] << "],\n"
         << "  \"maximumMoves\":" << kMaximumMoves << ",\n"
         << "  \"screenSeedStart\":" << kScreenStart << ",\n"
         << "  \"screen\":";
  writeCohort(output, screen);
  output << ",\n  \"screenPassed\":"
         << (screen_passed ? "true" : "false") << ",\n"
         << "  \"confirmation\":";
  if (screen_passed) writeCohort(output, confirmation);
  else output << "null";
  bool confirmed = false;
  if (screen_passed) {
    const Summary baseline = summarize(confirmation.baseline);
    const Summary candidate = summarize(confirmation.candidate);
    confirmed = candidate.mean_score > baseline.mean_score &&
                candidate.mean_moves > baseline.mean_moves;
  }
  output << ",\n  \"confirmed\":" << (confirmed ? "true" : "false")
         << ",\n  \"decision\":\""
         << (confirmed ? "advance" : "reject") << "\"\n}\n";
  std::cout << std::fixed << std::setprecision(3)
            << "EXACT_ROOT_ENSEMBLE {\"screenBaselineScore\":"
            << screen_baseline.mean_score << ",\"screenCandidateScore\":"
            << screen_candidate.mean_score << ",\"screenBaselineMoves\":"
            << screen_baseline.mean_moves << ",\"screenCandidateMoves\":"
            << screen_candidate.mean_moves << ",\"screenPassed\":"
            << (screen_passed ? "true" : "false")
            << ",\"confirmed\":" << (confirmed ? "true" : "false")
            << ",\"peakRssBytes\":" << peakRssBytes() << "}\n";
  return 0;
}

}  // namespace drop7::exact_root_ensemble

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::exact_root_ensemble::selfTest(std::cout) ? EXIT_SUCCESS
                                                             : EXIT_FAILURE;
    }
    return drop7::exact_root_ensemble::run(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "drop7_exact_root_ensemble: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
