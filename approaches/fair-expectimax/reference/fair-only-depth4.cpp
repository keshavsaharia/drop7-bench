#define main drop7_fair_only_horizon_frozen_entrypoint
#include "fair-only-horizon.cpp"
#undef main

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <list>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <unordered_map>
#include <utility>
#include <vector>

// Compares the fixed fair-only leaf at completed full-width
// depths three and four.  No weights, chance semantics, or action ranking are
// changed; the candidate differs only by one additional exact max/chance ply.
namespace drop7::fair_only_depth4 {

namespace frozen = drop7::fair_only_horizon;

constexpr int kBaselineDepth = 3;
constexpr int kCandidateDepth = 4;
constexpr int kChanceSamples = frozen::kChanceSamples;
constexpr std::uint64_t kMaximumWork = 3'200'000;
constexpr std::size_t kMaximumCacheEntries = 60'000;
constexpr std::uint32_t kScreenSeedStart = 0x3e9b'0000u;
constexpr std::uint32_t kConfirmationSeedStart = 0x3e9c'0000u;
constexpr int kScreenGames = 4;
constexpr int kConfirmationGames = 8;
constexpr int kMaximumMoves = 1'000;
constexpr int kParallelism = 4;

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
    result += power(branches, depth);
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
static_assert(kLevelBonus == 17'000);
static_assert(kScreenSeedStart + kScreenGames < kConfirmationSeedStart);
static_assert(frozen::kPolicySeed == 0xd707'5eedu);
static_assert(frozen::kTerminalUtility == -1'000'000.0);

std::mutex progress_mutex;

class WorkLimitReached : public std::exception {};

struct CacheEntry {
  double value = 0.0;
  std::list<std::string>::iterator order;
};

struct SearchContext {
  std::unordered_map<std::string, CacheEntry> cache;
  std::list<std::string> order;
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
  std::uint64_t cache_hits = 0;
};

void checkBudget(const SearchContext& context) {
  if (context.work >= kMaximumWork) throw WorkLimitReached{};
}

void cacheValue(SearchContext& context, std::string key, double value) {
  const auto prior = context.cache.find(key);
  if (prior != context.cache.end()) {
    context.order.erase(prior->second.order);
    context.cache.erase(prior);
  }
  while (context.cache.size() >= kMaximumCacheEntries) {
    const std::string& oldest = context.order.front();
    context.cache.erase(oldest);
    context.order.pop_front();
  }
  context.order.push_back(key);
  const auto order = std::prev(context.order.end());
  context.cache.emplace(std::move(key), CacheEntry{value, order});
}

double bestFutureValue(const State& state, int depth, SearchContext& context);

struct ActionValue {
  double value = 0.0;
  double expected_score = 0.0;
};

ActionValue evaluateAction(const State& state, int column, int depth,
                           SearchContext& context) {
  const std::uint32_t state_seed = cfpi::detail::scenarioSeedForState(
      state, frozen::kPolicySeed, depth);
  ActionValue result;
  for (int sample = 0; sample < kChanceSamples; ++sample) {
    checkBudget(context);
    cfpi::detail::StratifiedRandom random{
        state_seed, sample, kChanceSamples, 0};
    MoveResult move;
    const bool played =
        cfpi::detail::playMoveSampled(state, column, random, move);
    ++context.work;
    if (!played) {
      result.value += frozen::kTerminalUtility;
      continue;
    }
    const double score_delta = static_cast<double>(move.score_delta);
    result.expected_score += score_delta;
    if (move.state.game_over) {
      result.value += score_delta + frozen::kTerminalUtility;
      continue;
    }
    move.state.score = 0;
    move.state.next_disc = cfpi::detail::sampledNextDisc(
        state_seed, sample, kChanceSamples);
    bool ignored = false;
    const State next = cfpi::detail::canonicalState(move.state, ignored);
    result.value +=
        score_delta + bestFutureValue(next, depth - 1, context);
  }
  result.value /= kChanceSamples;
  result.expected_score /= kChanceSamples;
  return result;
}

double evaluateLeaf(const State& state, SearchContext& context) {
  checkBudget(context);
  ++context.work;
  const double value = frozen::fairLeaf(state);
  if (!std::isfinite(value)) {
    throw std::runtime_error("fair depth-four leaf returned non-finite value");
  }
  return value;
}

double bestFutureValue(const State& state, int depth,
                       SearchContext& context) {
  ++context.nodes;
  checkBudget(context);
  if (state.game_over) return frozen::kTerminalUtility;
  if (depth == 0) return evaluateLeaf(state, context);
  const std::string key = cfpi::detail::dynamicStateKey(state, depth);
  const auto cached = context.cache.find(key);
  if (cached != context.cache.end()) {
    ++context.cache_hits;
    const double value = cached->second.value;
    context.order.splice(context.order.end(), context.order,
                         cached->second.order);
    return value;
  }
  double best = -std::numeric_limits<double>::infinity();
  for (const int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(state.board, column)) continue;
    best = std::max(best, evaluateAction(state, column, depth, context).value);
  }
  if (!std::isfinite(best)) best = frozen::kTerminalUtility;
  cacheValue(context, key, best);
  return best;
}

struct RootEvaluation {
  int action = -1;
  double value = -std::numeric_limits<double>::infinity();
  std::array<double, kBoardSize> values{};
  std::array<double, kBoardSize> expected_scores{};
};

RootEvaluation rootDecision(const State& canonical, int depth,
                            SearchContext& context) {
  RootEvaluation result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  result.expected_scores.fill(-std::numeric_limits<double>::infinity());
  for (const int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(canonical.board, column)) continue;
    const ActionValue candidate =
        evaluateAction(canonical, column, depth, context);
    result.values[column] = candidate.value;
    result.expected_scores[column] = candidate.expected_score;
    if (candidate.value > result.value) {
      result.value = candidate.value;
      result.action = column;
    }
  }
  return result;
}

struct SearchDecision {
  int action = -1;
  int depth3_action = -1;
  int completed_depth = 0;
  bool complete = false;
  bool switched_from_depth3 = false;
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
  std::uint64_t cache_hits = 0;
  std::size_t cache_entries = 0;
  std::array<double, kBoardSize> root_values{};
  std::array<double, kBoardSize> root_expected_scores{};
};

SearchDecision chooseDepth4Action(const State& source) {
  if (source.game_over) return {};
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  SearchContext context;
  RootEvaluation completed;
  int completed_depth = 0;
  int depth3_action = -1;
  for (int depth = 1; depth <= kCandidateDepth; ++depth) {
    try {
      completed = rootDecision(canonical, depth, context);
      if (completed.action < 0) break;
      completed_depth = depth;
      if (depth == kBaselineDepth) depth3_action = completed.action;
    } catch (const WorkLimitReached&) {
      break;
    }
  }
  int action = completed.action;
  if (action < 0) action = centerFirstMove(canonical.board);
  if (depth3_action < 0) depth3_action = action;
  SearchDecision result;
  result.action = mirrored ? kBoardSize - 1 - action : action;
  result.depth3_action =
      mirrored ? kBoardSize - 1 - depth3_action : depth3_action;
  result.completed_depth = completed_depth;
  result.complete = completed_depth == kCandidateDepth;
  result.switched_from_depth3 = result.action != result.depth3_action;
  result.nodes = context.nodes;
  result.work = context.work;
  result.cache_hits = context.cache_hits;
  result.cache_entries = context.cache.size();
  result.root_values.fill(-std::numeric_limits<double>::infinity());
  result.root_expected_scores.fill(-std::numeric_limits<double>::infinity());
  if (completed_depth > 0) {
    for (int canonical_column = 0; canonical_column < kBoardSize;
         ++canonical_column) {
      const int source_column = mirrored
                                    ? kBoardSize - 1 - canonical_column
                                    : canonical_column;
      result.root_values[source_column] = completed.values[canonical_column];
      result.root_expected_scores[source_column] =
          completed.expected_scores[canonical_column];
    }
  }
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

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t numbered_cleared = 0;
  std::uint64_t covers_revealed = 0;
  int maximum_chain = 0;
  std::array<std::uint64_t, kBoardSize> action_counts{};
  std::uint64_t depth_switches = 0;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::size_t peak_cache_entries = 0;
  std::uint64_t peak_rss_bytes = 0;
  double elapsed_seconds = 0.0;
};

void observeMove(const MoveResult& move, GameResult& result) {
  result.maximum_chain =
      std::max(result.maximum_chain, static_cast<int>(move.waves.size()));
  for (const Wave& wave : move.waves) {
    result.numbered_cleared += static_cast<std::uint64_t>(wave.cleared);
    result.covers_revealed += static_cast<std::uint64_t>(wave.revealed);
  }
}

void reportGame(std::string_view label, const GameResult& result) {
  const std::lock_guard<std::mutex> lock(progress_mutex);
  std::cerr << label << " seed 0x" << std::hex << result.seed << std::dec
            << ' ' << result.score << " (" << result.moves << " moves"
            << (result.censored ? ", capped" : "") << ", switches "
            << result.depth_switches << ", clears "
            << result.numbered_cleared << ", reveals "
            << result.covers_revealed << ", work " << result.work
            << ", cache " << result.peak_cache_entries << ")\n";
}

GameResult runDepth3Game(std::uint32_t seed, std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    const frozen::SearchDecision decision = frozen::chooseFairAction(state);
    if (!decision.complete || decision.completed_depth != kBaselineDepth) {
      throw std::runtime_error("fair depth three did not complete");
    }
    if (!isLegal(state.board, decision.action)) {
      throw std::runtime_error("fair depth three chose an illegal action");
    }
    ++result.action_counts[decision.action];
    result.work += decision.work;
    result.nodes += decision.nodes;
    result.cache_hits += decision.cache_hits;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, decision.cache_entries);
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("fair depth-three transition failed");
    }
    observeMove(move, result);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.peak_rss_bytes = peakRssBytes();
  result.elapsed_seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
  reportGame(label, result);
  return result;
}

GameResult runDepth4Game(std::uint32_t seed, std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    const SearchDecision decision = chooseDepth4Action(state);
    if (!decision.complete || decision.completed_depth != kCandidateDepth) {
      throw std::runtime_error("fair depth four did not complete");
    }
    if (!isLegal(state.board, decision.action) ||
        !isLegal(state.board, decision.depth3_action)) {
      throw std::runtime_error("fair depth-four decision was illegal");
    }
    result.depth_switches += decision.switched_from_depth3;
    ++result.action_counts[decision.action];
    result.work += decision.work;
    result.nodes += decision.nodes;
    result.cache_hits += decision.cache_hits;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, decision.cache_entries);
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("fair depth-four transition failed");
    }
    observeMove(move, result);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.peak_rss_bytes = peakRssBytes();
  result.elapsed_seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
  reportGame(label, result);
  return result;
}

struct Cohort {
  std::vector<GameResult> depth3;
  std::vector<GameResult> depth4;
  double wall_seconds = 0.0;
};

Cohort runCohort(std::uint32_t seed_start, int games,
                 std::string_view phase) {
  const auto started = std::chrono::steady_clock::now();
  Cohort result;
  result.depth3.resize(games);
  result.depth4.resize(games);
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= games) return;
        const std::uint32_t seed =
            seed_start + static_cast<std::uint32_t>(game);
        result.depth3[game] = runDepth3Game(
            seed, std::string(phase) + "-fair-d3");
        result.depth4[game] = runDepth4Game(
            seed, std::string(phase) + "-fair-d4");
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.wall_seconds = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started)
                            .count();
  return result;
}

struct Summary {
  int games = 0;
  int depth = 0;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  int censored = 0;
  double mean_numbered_cleared = 0.0;
  double mean_covers_revealed = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double mean_maximum_chain = 0.0;
  std::array<std::uint64_t, kBoardSize> action_counts{};
  std::uint64_t depth_switches = 0;
  double switch_rate = 0.0;
  std::uint64_t work = 0;
  double work_per_move = 0.0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::size_t peak_cache_entries = 0;
  double aggregate_game_seconds = 0.0;
  double moves_per_game_second = 0.0;
  std::uint64_t peak_rss_bytes = 0;
};

Summary summarize(const std::vector<GameResult>& games, int depth) {
  if (games.empty()) throw std::invalid_argument("empty fair depth cohort");
  Summary result;
  result.games = static_cast<int>(games.size());
  result.depth = depth;
  std::uint64_t moves = 0;
  std::uint64_t cleared = 0;
  std::uint64_t revealed = 0;
  for (const GameResult& game : games) {
    result.mean_score += static_cast<double>(game.score) / games.size();
    result.mean_moves += static_cast<double>(game.moves) / games.size();
    result.censored += game.censored;
    result.mean_numbered_cleared +=
        static_cast<double>(game.numbered_cleared) / games.size();
    result.mean_covers_revealed +=
        static_cast<double>(game.covers_revealed) / games.size();
    result.mean_maximum_chain +=
        static_cast<double>(game.maximum_chain) / games.size();
    for (int column = 0; column < kBoardSize; ++column) {
      result.action_counts[column] += game.action_counts[column];
    }
    result.depth_switches += game.depth_switches;
    result.work += game.work;
    result.nodes += game.nodes;
    result.cache_hits += game.cache_hits;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, game.peak_cache_entries);
    result.aggregate_game_seconds += game.elapsed_seconds;
    result.peak_rss_bytes =
        std::max(result.peak_rss_bytes, game.peak_rss_bytes);
    moves += static_cast<std::uint64_t>(game.moves);
    cleared += game.numbered_cleared;
    revealed += game.covers_revealed;
  }
  const double move_count =
      static_cast<double>(std::max<std::uint64_t>(1, moves));
  result.clears_per_move = cleared / move_count;
  result.reveals_per_move = revealed / move_count;
  result.switch_rate = result.depth_switches / move_count;
  result.work_per_move = result.work / move_count;
  result.moves_per_game_second =
      move_count / std::max(1.0e-9, result.aggregate_game_seconds);
  return result;
}

struct DifferenceStats {
  double mean = 0.0;
  double lower_95 = 0.0;
  int wins = 0;
  int ties = 0;
  int losses = 0;
};

DifferenceStats differences(const std::vector<double>& values) {
  if (values.empty()) throw std::invalid_argument("empty paired differences");
  DifferenceStats result;
  for (const double value : values) {
    result.mean += value / values.size();
    result.wins += value > 0.0;
    result.ties += value == 0.0;
    result.losses += value < 0.0;
  }
  double squares = 0.0;
  for (const double value : values) {
    squares += (value - result.mean) * (value - result.mean);
  }
  const double deviation = values.size() > 1
                               ? std::sqrt(squares / (values.size() - 1))
                               : 0.0;
  result.lower_95 =
      result.mean - 1.96 * deviation / std::sqrt(values.size());
  return result;
}

struct PairedSummary {
  DifferenceStats score;
  DifferenceStats moves;
  DifferenceStats numbered_cleared;
  DifferenceStats covers_revealed;
};

PairedSummary pairedSummary(const Cohort& cohort) {
  if (cohort.depth3.size() != cohort.depth4.size() ||
      cohort.depth3.empty()) {
    throw std::invalid_argument("invalid fair-depth paired cohort");
  }
  std::vector<double> scores;
  std::vector<double> moves;
  std::vector<double> cleared;
  std::vector<double> revealed;
  for (std::size_t game = 0; game < cohort.depth3.size(); ++game) {
    scores.push_back(static_cast<double>(cohort.depth4[game].score -
                                         cohort.depth3[game].score));
    moves.push_back(static_cast<double>(cohort.depth4[game].moves -
                                        cohort.depth3[game].moves));
    cleared.push_back(static_cast<double>(cohort.depth4[game].numbered_cleared) -
                      cohort.depth3[game].numbered_cleared);
    revealed.push_back(static_cast<double>(cohort.depth4[game].covers_revealed) -
                       cohort.depth3[game].covers_revealed);
  }
  return {differences(scores), differences(moves), differences(cleared),
          differences(revealed)};
}

bool improvesBothMeans(const Summary& depth3, const Summary& depth4) {
  return depth4.mean_score > depth3.mean_score &&
         depth4.mean_moves > depth3.mean_moves;
}

void writeActions(std::ostream& output,
                  const std::array<std::uint64_t, kBoardSize>& actions) {
  output << '[';
  for (int column = 0; column < kBoardSize; ++column) {
    if (column != 0) output << ',';
    output << actions[column];
  }
  output << ']';
}

void writeGame(std::ostream& output, const GameResult& game) {
  output << "{\"seed\":" << game.seed << ",\"score\":" << game.score
         << ",\"moves\":" << game.moves
         << ",\"censored\":" << (game.censored ? "true" : "false")
         << ",\"numberedCleared\":" << game.numbered_cleared
         << ",\"coversRevealed\":" << game.covers_revealed
         << ",\"maximumChain\":" << game.maximum_chain
         << ",\"actionCounts\":";
  writeActions(output, game.action_counts);
  output << ",\"depthSwitches\":" << game.depth_switches
         << ",\"work\":" << game.work << ",\"nodes\":" << game.nodes
         << ",\"cacheHits\":" << game.cache_hits
         << ",\"peakCacheEntries\":" << game.peak_cache_entries
         << ",\"elapsedSeconds\":" << game.elapsed_seconds
         << ",\"peakRssBytes\":" << game.peak_rss_bytes << '}';
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"games\":" << summary.games << ",\"depth\":" << summary.depth
         << ",\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"censored\":" << summary.censored
         << ",\"meanNumberedCleared\":"
         << summary.mean_numbered_cleared
         << ",\"meanCoversRevealed\":" << summary.mean_covers_revealed
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"meanMaximumChain\":" << summary.mean_maximum_chain
         << ",\"actionCounts\":";
  writeActions(output, summary.action_counts);
  output << ",\"depthSwitches\":" << summary.depth_switches
         << ",\"switchRate\":" << summary.switch_rate
         << ",\"work\":" << summary.work
         << ",\"workPerMove\":" << summary.work_per_move
         << ",\"nodes\":" << summary.nodes
         << ",\"cacheHits\":" << summary.cache_hits
         << ",\"peakCacheEntries\":" << summary.peak_cache_entries
         << ",\"aggregateGameSeconds\":"
         << summary.aggregate_game_seconds
         << ",\"movesPerGameSecond\":" << summary.moves_per_game_second
         << ",\"peakRssBytes\":" << summary.peak_rss_bytes << '}';
}

void writeDifference(std::ostream& output, const DifferenceStats& difference) {
  output << "{\"mean\":" << difference.mean
         << ",\"lower95\":" << difference.lower_95
         << ",\"wins\":" << difference.wins
         << ",\"ties\":" << difference.ties
         << ",\"losses\":" << difference.losses << '}';
}

void writePaired(std::ostream& output, const PairedSummary& paired) {
  output << "{\"score\":";
  writeDifference(output, paired.score);
  output << ",\"moves\":";
  writeDifference(output, paired.moves);
  output << ",\"numberedCleared\":";
  writeDifference(output, paired.numbered_cleared);
  output << ",\"coversRevealed\":";
  writeDifference(output, paired.covers_revealed);
  output << '}';
}

void writePairs(std::ostream& output, const Cohort& cohort) {
  output << '[';
  for (std::size_t game = 0; game < cohort.depth3.size(); ++game) {
    if (game != 0) output << ',';
    output << "{\"seed\":" << cohort.depth3[game].seed
           << ",\"fairD3\":";
    writeGame(output, cohort.depth3[game]);
    output << ",\"fairD4\":";
    writeGame(output, cohort.depth4[game]);
    output << '}';
  }
  output << ']';
}

void writeCohort(std::ostream& output, std::uint32_t seed_start,
                 const Cohort& cohort, const Summary& depth3,
                 const Summary& depth4, const PairedSummary& paired,
                 bool passed) {
  output << "{\"seedStart\":" << seed_start
         << ",\"maximumMoves\":" << kMaximumMoves << ",\"fairD3\":";
  writeSummary(output, depth3);
  output << ",\"fairD4\":";
  writeSummary(output, depth4);
  output << ",\"paired\":";
  writePaired(output, paired);
  output << ",\"wallSeconds\":" << cohort.wall_seconds
         << ",\"passed\":" << (passed ? "true" : "false")
         << ",\"pairs\":";
  writePairs(output, cohort);
  output << '}';
}

struct Options {
  std::string output = "/tmp/drop7-fair-only-depth4.json";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing fair-depth option value");
    }
    const std::string argument = argv[index];
    if (argument == "--output") {
      result.output = argv[index + 1];
    } else {
      throw std::invalid_argument("unknown fair-depth option " + argument);
    }
  }
  return result;
}

void writeArtifact(const Options& options, const Cohort& screen,
                   const Summary& screen_depth3,
                   const Summary& screen_depth4,
                   const PairedSummary& screen_paired, bool screen_passed,
                   const Cohort* confirmation,
                   const Summary* confirmation_depth3,
                   const Summary* confirmation_depth4,
                   const PairedSummary* confirmation_paired,
                   bool confirmation_passed, double total_wall) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open fair d4 artifact");
  output << std::setprecision(10)
         << "{\n  \"experiment\":\"fair-only-full-width-depth4\",\n"
         << "  \"preregistered\":true,\n"
         << "  \"publicStateOnly\":true,\n"
         << "  \"phaseResidualIncluded\":false,\n"
         << "  \"scoring\":{\"levelBonus\":" << kLevelBonus << "},\n"
         << "  \"search\":{\"baselineDepth\":" << kBaselineDepth
         << ",\"candidateDepth\":" << kCandidateDepth
         << ",\"chanceSamples\":" << kChanceSamples
         << ",\"fullWidth\":true,\"policySeed\":"
         << frozen::kPolicySeed << ",\"maximumWork\":" << kMaximumWork
         << ",\"worstCaseD4Work\":" << kWorstCaseD4Work
         << ",\"maximumCacheEntries\":" << kMaximumCacheEntries
         << ",\"worstCaseD4CacheEntries\":"
         << kWorstCaseD4CacheEntries
         << ",\"maximumMoves\":" << kMaximumMoves
         << ",\"parallelism\":" << kParallelism << "},\n"
         << "  \"screen\":";
  writeCohort(output, kScreenSeedStart, screen, screen_depth3, screen_depth4,
              screen_paired, screen_passed);
  output << ",\n  \"confirmation\":";
  if (confirmation == nullptr) {
    output << "null";
  } else {
    writeCohort(output, kConfirmationSeedStart, *confirmation,
                *confirmation_depth3, *confirmation_depth4,
                *confirmation_paired, confirmation_passed);
  }
  output << ",\n  \"screenPassed\":"
         << (screen_passed ? "true" : "false")
         << ",\n  \"confirmationRan\":"
         << (confirmation != nullptr ? "true" : "false")
         << ",\n  \"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\n  \"qualified\":"
         << (screen_passed && confirmation_passed ? "true" : "false")
         << ",\n  \"totalWallSeconds\":" << total_wall
         << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
}

bool selfTest(std::ostream& output) {
  const bool frozen_test = frozen::selfTest(output);
  const State state = frozen::fixtureState(frozen::kTypeScriptFixtures[1]);
  const frozen::SearchDecision depth3 = frozen::chooseFairAction(state);
  const SearchDecision first = chooseDepth4Action(state);
  const SearchDecision repeat = chooseDepth4Action(state);
  State reflected = state;
  reflected.board = cfpi::detail::mirrorBoard(state.board);
  const SearchDecision mirrored = chooseDepth4Action(reflected);
  State metadata = state;
  metadata.score = 8'000'000;
  metadata.level = 73;
  metadata.moves_played = 412;
  const SearchDecision metadata_decision = chooseDepth4Action(metadata);

  constexpr std::array<double, kBoardSize> kTypeScriptD4Values{{
      15988.359303565918, 17315.5934006875, 17048.297768316406,
      17099.6503713125, 18121.713585319824, 16714.705867296875,
      16937.883952363282,
  }};
  double maximum_root_error = 0.0;
  for (int column = 0; column < kBoardSize; ++column) {
    maximum_root_error = std::max(
        maximum_root_error,
        std::abs(first.root_values[column] - kTypeScriptD4Values[column]));
  }
  const bool typescript_parity =
      first.action == 4 && first.completed_depth == 4 && first.complete &&
      first.nodes == 957'740 && first.work == 1'877'470 &&
      first.cache_entries == 27'360 && first.cache_hits == 10'650 &&
      maximum_root_error <= 1.0e-8;
  const bool depth3_parity = first.depth3_action == depth3.action &&
                             first.depth3_action == 1 &&
                             first.switched_from_depth3;
  const bool deterministic =
      repeat.action == first.action && repeat.depth3_action == first.depth3_action &&
      repeat.work == first.work && repeat.nodes == first.nodes &&
      repeat.cache_entries == first.cache_entries &&
      repeat.cache_hits == first.cache_hits;
  const bool reflection_safe =
      mirrored.action == kBoardSize - 1 - first.action &&
      mirrored.depth3_action == kBoardSize - 1 - first.depth3_action &&
      mirrored.work == first.work;
  const bool public_only = metadata_decision.action == first.action &&
                           metadata_decision.depth3_action == first.depth3_action &&
                           metadata_decision.work == first.work;
  const bool legal = isLegal(state.board, first.action) &&
                     isLegal(state.board, first.depth3_action);
  const bool bounded = first.work <= kMaximumWork &&
                       first.cache_entries <= kMaximumCacheEntries;
  const bool completion_proven =
      kMaximumWork > kWorstCaseD4Work &&
      kMaximumCacheEntries > kWorstCaseD4CacheEntries;
  const bool fixed_protocol =
      kLevelBonus == 17'000 && kScreenSeedStart == 0x3e9b'0000u &&
      kConfirmationSeedStart == 0x3e9c'0000u && kMaximumMoves == 1'000 &&
      (kScreenSeedStart >> 24) != 0x7du &&
      (kScreenSeedStart >> 24) != 0xd7u &&
      (kConfirmationSeedStart >> 24) != 0x7du &&
      (kConfirmationSeedStart >> 24) != 0xd7u;
  const bool passed = frozen_test && typescript_parity && depth3_parity &&
                      deterministic && reflection_safe && public_only && legal &&
                      bounded && completion_proven && fixed_protocol;
  output << std::setprecision(12)
         << "FAIR_ONLY_DEPTH4_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"frozenLeafTest\":"
         << (frozen_test ? "true" : "false")
         << ",\"typescriptD4Parity\":"
         << (typescript_parity ? "true" : "false")
         << ",\"maximumRootError\":" << maximum_root_error
         << ",\"completedDepth\":" << first.completed_depth
         << ",\"depth3Action\":" << first.depth3_action
         << ",\"depth4Action\":" << first.action
         << ",\"actionSwitched\":"
         << (first.switched_from_depth3 ? "true" : "false")
         << ",\"work\":" << first.work
         << ",\"cacheEntries\":" << first.cache_entries
         << ",\"deterministic\":"
         << (deterministic ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe ? "true" : "false")
         << ",\"publicStateOnly\":" << (public_only ? "true" : "false")
         << ",\"completionProven\":"
         << (completion_proven ? "true" : "false")
         << ",\"worstCaseWork\":" << kWorstCaseD4Work
         << ",\"worstCaseCache\":" << kWorstCaseD4CacheEntries
         << ",\"levelBonus\":" << kLevelBonus << "}\n";
  return passed;
}

int run(const Options& options, std::ostream& output) {
  const auto started = std::chrono::steady_clock::now();
  const Cohort screen =
      runCohort(kScreenSeedStart, kScreenGames, "screen");
  const Summary screen_depth3 = summarize(screen.depth3, kBaselineDepth);
  const Summary screen_depth4 = summarize(screen.depth4, kCandidateDepth);
  const PairedSummary screen_paired = pairedSummary(screen);
  const bool screen_passed = improvesBothMeans(screen_depth3, screen_depth4);

  Cohort confirmation;
  Summary confirmation_depth3;
  Summary confirmation_depth4;
  PairedSummary confirmation_paired;
  bool confirmation_passed = false;
  if (screen_passed) {
    confirmation = runCohort(kConfirmationSeedStart, kConfirmationGames,
                             "confirmation");
    confirmation_depth3 = summarize(confirmation.depth3, kBaselineDepth);
    confirmation_depth4 = summarize(confirmation.depth4, kCandidateDepth);
    confirmation_paired = pairedSummary(confirmation);
    confirmation_passed =
        improvesBothMeans(confirmation_depth3, confirmation_depth4);
  }
  const double total_wall = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - started)
                                .count();
  writeArtifact(
      options, screen, screen_depth3, screen_depth4, screen_paired,
      screen_passed, screen_passed ? &confirmation : nullptr,
      screen_passed ? &confirmation_depth3 : nullptr,
      screen_passed ? &confirmation_depth4 : nullptr,
      screen_passed ? &confirmation_paired : nullptr, confirmation_passed,
      total_wall);
  output << std::fixed << std::setprecision(3)
         << "FAIR_ONLY_DEPTH4_RESULT {\"levelBonus\":" << kLevelBonus
         << ",\"screenD3Score\":" << screen_depth3.mean_score
         << ",\"screenD3Moves\":" << screen_depth3.mean_moves
         << ",\"screenD4Score\":" << screen_depth4.mean_score
         << ",\"screenD4Moves\":" << screen_depth4.mean_moves
         << ",\"screenScoreDelta\":" << screen_paired.score.mean
         << ",\"screenMoveDelta\":" << screen_paired.moves.mean
         << ",\"screenSwitchRate\":" << screen_depth4.switch_rate
         << ",\"screenPassed\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmationRan\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"totalWallSeconds\":" << total_wall
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return 0;
}

}  // namespace drop7::fair_only_depth4

#ifndef DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#ifndef DROP7_FAIR_ONLY_DEPTH4_NO_MAIN
int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::fair_only_depth4::selfTest(std::cout) ? EXIT_SUCCESS
                                                          : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::fair_only_depth4::parseOptions(argc, argv, 2);
      return drop7::fair_only_depth4::run(options, std::cout);
    }
    std::cerr << "usage: drop7_fair_only_depth4 --self-test | --run "
                 "[--output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
#endif
#endif
