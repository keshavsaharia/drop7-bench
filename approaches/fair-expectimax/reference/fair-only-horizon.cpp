#include "../../../src/core/native/public-behavior.hpp"

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
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <unordered_map>
#include <utility>
#include <vector>

// Implements the fair-only horizon evaluator in native code and excludes every
// phase-horizon residual.  Search is full-width iterative-deepening depth three
// with five stratified chance samples, an observable-state seed, an LRU cache,
// and the same terminal utility as the TypeScript reference.
namespace drop7::fair_only_horizon {

constexpr int kDepth = 3;
constexpr int kChanceSamples = 5;
constexpr std::uint64_t kMaximumWork = 1'000'000;
constexpr std::size_t kMaximumCacheEntries = 40'000;
constexpr double kTerminalUtility = -1'000'000.0;
constexpr double kFairTerminalUtility = -2'500'000.0;
constexpr std::uint32_t kPolicySeed = 0xd707'5eedu;
constexpr std::uint32_t kScreenSeedStart = 0x3e95'0000u;
constexpr std::uint32_t kConfirmationSeedStart = 0x3e96'0000u;
constexpr int kScreenGames = 8;
constexpr int kConfirmationGames = 16;
constexpr int kMaximumMoves = 1'000;
constexpr int kParallelism = 4;

// FAIR_PHASE_BASELINE_WEIGHTS from approaches/fair-expectimax/phase-fair-combination/main.ts.
// The first four and last twelve fair-tuner features are transition/action
// features and are zero in evaluateFairPosition's leaf vector.  Search already
// adds immediate score with its fixed unit coefficient.  In particular, the
// 300-point revealed-cover override is inert in this leaf-only use.
constexpr double kImmediateScoreWeight = 1.0;
constexpr double kRevealedCoverWeight = 300.0;
constexpr double kOpenColumnsWeight = 180.0;
constexpr double kHeightLoadWeight = -20.0;
constexpr double kSolidCellsWeight = -620.0;
constexpr double kCrackedCellsWeight = -220.0;
constexpr double kNumberedCellsWeight = -18.0;
constexpr double kHighLowNumbersWeight = -90.0;
constexpr double kDirectPotentialWeight = 1'600.0;
constexpr double kLatentChainPotentialWeight = 700.0;
constexpr double kCrackedExposureWeight = 100.0;
constexpr double kSolidExposureWeight = 40.0;
constexpr double kAdjacentOnesWeight = -550.0;
constexpr double kTripleTwosWeight = -750.0;
constexpr double kDeadLowNumbersWeight = -120.0;
constexpr double kCoveredHeightRiskWeight = -95.0;
constexpr double kLowNumberHeightRiskWeight = -85.0;
constexpr double kDangerHeightSquaredWeight = -1'250.0;
constexpr double kRoughnessWeight = 0.0;
constexpr double kRisePressureWeight = -35.0;
constexpr double kNextDiscVerticalOptionsWeight = 220.0;

static_assert(kLevelBonus == 17'000);
static_assert(kImmediateScoreWeight == 1.0);
static_assert(kRevealedCoverWeight == 300.0);
static_assert(kScreenSeedStart + kScreenGames < kConfirmationSeedStart);

std::mutex progress_mutex;

struct FairFeatures {
  cfpi::detail::PhaseFeatures heuristic{};
  double covered_height_risk = 0.0;
  double low_number_height_risk = 0.0;
  double danger_height_squared = 0.0;
  double roughness = 0.0;
  double rise_pressure = 0.0;
  double next_disc_vertical_options = 0.0;
};

FairFeatures extractFairFeatures(const State& state) {
  if (state.moves_remaining < 1 || state.moves_remaining > kMovesPerLevel ||
      state.next_disc < 1 || state.next_disc > kBoardSize) {
    throw std::invalid_argument("invalid public state for fair evaluator");
  }
  FairFeatures result;
  result.heuristic = cfpi::detail::extractPhaseFeatures(state);
  const auto heights = cfpi::detail::columnHeights(state.board);
  int maximum_height = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    const int height = heights[column];
    maximum_height = std::max(maximum_height, height);
    result.rise_pressure +=
        static_cast<double>(height * height * height) /
        state.moves_remaining;
    if (height < kBoardSize && height + 1 == state.next_disc) {
      result.next_disc_vertical_options += 1.0;
    }
  }
  for (int row = 0; row < kBoardSize; ++row) {
    const int elevation = kBoardSize - row;
    for (int column = 0; column < kBoardSize; ++column) {
      const std::uint8_t cell = state.board[indexOf(row, column)];
      const double edge_multiplier =
          column == 0 || column == kBoardSize - 1 ? 1.65 : 1.0;
      if (cell == kSolid) {
        result.covered_height_risk +=
            elevation * elevation * edge_multiplier;
      } else if (cell == kCracked) {
        result.covered_height_risk +=
            elevation * elevation * edge_multiplier * 0.72;
      } else if (cell == 1 || cell == 2) {
        const int height_risk = std::max(0, elevation - 2);
        result.low_number_height_risk += height_risk * height_risk;
      }
    }
  }
  for (int column = 1; column < kBoardSize; ++column) {
    result.roughness += std::abs(heights[column] - heights[column - 1]);
  }
  const int danger = std::max(0, maximum_height - 4);
  result.danger_height_squared = danger * danger;
  return result;
}

double fairLeaf(const State& state) {
  if (state.game_over) return kFairTerminalUtility;
  const FairFeatures features = extractFairFeatures(state);
  const auto& f = features.heuristic;
  // Preserve the TypeScript dot-product order for parity.
  double result = 0.0;
  result += kOpenColumnsWeight * f.open_columns;
  result += kHeightLoadWeight * f.height_load;
  result += kSolidCellsWeight * f.solid_cells;
  result += kCrackedCellsWeight * f.cracked_cells;
  result += kNumberedCellsWeight * f.numbered_cells;
  result += kHighLowNumbersWeight * f.high_low_numbers;
  result += kDirectPotentialWeight * f.direct_potential;
  result += kLatentChainPotentialWeight * f.latent_chain_potential;
  result += kCrackedExposureWeight * f.cracked_exposure;
  result += kSolidExposureWeight * f.solid_exposure;
  result += kAdjacentOnesWeight * f.adjacent_ones;
  result += kTripleTwosWeight * f.triple_twos;
  result += kDeadLowNumbersWeight * f.dead_low_numbers;
  result += kCoveredHeightRiskWeight * features.covered_height_risk;
  result += kLowNumberHeightRiskWeight * features.low_number_height_risk;
  result += kDangerHeightSquaredWeight * features.danger_height_squared;
  result += kRoughnessWeight * features.roughness;
  result += kRisePressureWeight * features.rise_pressure;
  result += kNextDiscVerticalOptionsWeight *
            features.next_disc_vertical_options;
  return result;
}

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
  const std::uint32_t state_seed =
      cfpi::detail::scenarioSeedForState(state, kPolicySeed, depth);
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
      result.value += kTerminalUtility;
      continue;
    }
    const double score_delta = static_cast<double>(move.score_delta);
    result.expected_score += score_delta;
    if (move.state.game_over) {
      result.value += score_delta + kTerminalUtility;
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
  const double value = fairLeaf(state);
  if (!std::isfinite(value)) {
    throw std::runtime_error("fair-only evaluator returned non-finite value");
  }
  return value;
}

double bestFutureValue(const State& state, int depth, SearchContext& context) {
  ++context.nodes;
  checkBudget(context);
  if (state.game_over) return kTerminalUtility;
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
  if (!std::isfinite(best)) best = kTerminalUtility;
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
  int completed_depth = 0;
  bool complete = false;
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
  std::uint64_t cache_hits = 0;
  std::size_t cache_entries = 0;
  std::array<double, kBoardSize> root_values{};
  std::array<double, kBoardSize> root_expected_scores{};
};

SearchDecision chooseFairAction(const State& source) {
  if (source.game_over) return {};
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  SearchContext context;
  RootEvaluation completed;
  int completed_depth = 0;
  for (int depth = 1; depth <= kDepth; ++depth) {
    try {
      completed = rootDecision(canonical, depth, context);
      if (completed.action < 0) break;
      completed_depth = depth;
    } catch (const WorkLimitReached&) {
      break;
    }
  }
  int action = completed.action;
  if (action < 0) action = centerFirstMove(canonical.board);
  SearchDecision result;
  result.action = mirrored ? kBoardSize - 1 - action : action;
  result.completed_depth = completed_depth;
  result.complete = completed_depth == kDepth;
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

cfpi::BehaviorOptions baselineOptions() {
  cfpi::BehaviorOptions result;
  result.max_depth = kDepth;
  result.chance_samples = kChanceSamples;
  result.max_work = kMaximumWork;
  result.max_cache_entries = kMaximumCacheEntries;
  result.terminal_utility = kTerminalUtility;
  result.policy_seed = kPolicySeed;
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
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::size_t maximum_cache_entries = 0;
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
            << (result.censored ? ", capped" : "") << ", clears "
            << result.numbered_cleared << ", reveals "
            << result.covers_revealed << ", work " << result.work << ")\n";
}

GameResult runBaselineGame(std::uint32_t seed, std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  const cfpi::BehaviorOptions options = baselineOptions();
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    cfpi::BehaviorMetrics metrics;
    const int action = cfpi::chooseBehaviorAction(state, options, &metrics);
    if (!metrics.complete || metrics.completed_depth != kDepth) {
      throw std::runtime_error("cfpi baseline did not complete exact depth three");
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("cfpi baseline chose an illegal action");
    }
    result.work += metrics.work;
    result.nodes += metrics.nodes;
    result.cache_hits += metrics.cache_hits;
    result.maximum_cache_entries =
        std::max(result.maximum_cache_entries, metrics.cache_entries);
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("cfpi baseline transition failed");
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

GameResult runFairGame(std::uint32_t seed, std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    const SearchDecision decision = chooseFairAction(state);
    if (!decision.complete || decision.completed_depth != kDepth) {
      throw std::runtime_error("fair-only search did not complete depth three");
    }
    if (!isLegal(state.board, decision.action)) {
      throw std::runtime_error("fair-only search chose an illegal action");
    }
    result.work += decision.work;
    result.nodes += decision.nodes;
    result.cache_hits += decision.cache_hits;
    result.maximum_cache_entries =
        std::max(result.maximum_cache_entries, decision.cache_entries);
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("fair-only transition failed");
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
  std::vector<GameResult> baseline;
  std::vector<GameResult> fair;
  double wall_seconds = 0.0;
};

Cohort runCohort(std::uint32_t seed_start, int games,
                 std::string_view phase) {
  const auto started = std::chrono::steady_clock::now();
  Cohort result;
  result.baseline.resize(games);
  result.fair.resize(games);
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= games) return;
        const std::uint32_t seed =
            seed_start + static_cast<std::uint32_t>(game);
        result.baseline[game] = runBaselineGame(
            seed, std::string(phase) + "-cfpi-d3");
        result.fair[game] =
            runFairGame(seed, std::string(phase) + "-fair-only-d3");
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
  double mean_score = 0.0;
  double mean_moves = 0.0;
  int censored = 0;
  double mean_numbered_cleared = 0.0;
  double mean_covers_revealed = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double mean_maximum_chain = 0.0;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  double work_per_move = 0.0;
  double aggregate_game_seconds = 0.0;
  std::size_t maximum_cache_entries = 0;
  std::uint64_t peak_rss_bytes = 0;
};

Summary summarize(const std::vector<GameResult>& games) {
  if (games.empty()) throw std::invalid_argument("empty fair-only cohort");
  Summary result;
  result.games = static_cast<int>(games.size());
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
    result.work += game.work;
    result.nodes += game.nodes;
    result.cache_hits += game.cache_hits;
    result.aggregate_game_seconds += game.elapsed_seconds;
    result.maximum_cache_entries =
        std::max(result.maximum_cache_entries, game.maximum_cache_entries);
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
  result.work_per_move = result.work / move_count;
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
  if (cohort.baseline.size() != cohort.fair.size() ||
      cohort.baseline.empty()) {
    throw std::invalid_argument("invalid fair-only paired cohort");
  }
  std::vector<double> scores;
  std::vector<double> moves;
  std::vector<double> cleared;
  std::vector<double> revealed;
  for (std::size_t game = 0; game < cohort.baseline.size(); ++game) {
    scores.push_back(static_cast<double>(cohort.fair[game].score -
                                         cohort.baseline[game].score));
    moves.push_back(static_cast<double>(cohort.fair[game].moves -
                                        cohort.baseline[game].moves));
    cleared.push_back(static_cast<double>(cohort.fair[game].numbered_cleared) -
                      cohort.baseline[game].numbered_cleared);
    revealed.push_back(static_cast<double>(cohort.fair[game].covers_revealed) -
                       cohort.baseline[game].covers_revealed);
  }
  return {differences(scores), differences(moves), differences(cleared),
          differences(revealed)};
}

bool improvesBothMeans(const Summary& baseline, const Summary& fair) {
  return fair.mean_score > baseline.mean_score &&
         fair.mean_moves > baseline.mean_moves;
}

void writeGame(std::ostream& output, const GameResult& game) {
  output << "{\"seed\":" << game.seed << ",\"score\":" << game.score
         << ",\"moves\":" << game.moves
         << ",\"censored\":" << (game.censored ? "true" : "false")
         << ",\"numberedCleared\":" << game.numbered_cleared
         << ",\"coversRevealed\":" << game.covers_revealed
         << ",\"maximumChain\":" << game.maximum_chain
         << ",\"work\":" << game.work << ",\"nodes\":" << game.nodes
         << ",\"cacheHits\":" << game.cache_hits
         << ",\"maximumCacheEntries\":" << game.maximum_cache_entries
         << ",\"elapsedSeconds\":" << game.elapsed_seconds
         << ",\"peakRssBytes\":" << game.peak_rss_bytes << '}';
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"games\":" << summary.games
         << ",\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"censored\":" << summary.censored
         << ",\"meanNumberedCleared\":"
         << summary.mean_numbered_cleared
         << ",\"meanCoversRevealed\":" << summary.mean_covers_revealed
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"meanMaximumChain\":" << summary.mean_maximum_chain
         << ",\"work\":" << summary.work
         << ",\"workPerMove\":" << summary.work_per_move
         << ",\"nodes\":" << summary.nodes
         << ",\"cacheHits\":" << summary.cache_hits
         << ",\"aggregateGameSeconds\":"
         << summary.aggregate_game_seconds
         << ",\"maximumCacheEntries\":" << summary.maximum_cache_entries
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
  for (std::size_t game = 0; game < cohort.baseline.size(); ++game) {
    if (game != 0) output << ',';
    output << "{\"seed\":" << cohort.baseline[game].seed
           << ",\"cfpi\":";
    writeGame(output, cohort.baseline[game]);
    output << ",\"fairOnly\":";
    writeGame(output, cohort.fair[game]);
    output << '}';
  }
  output << ']';
}

void writeCohort(std::ostream& output, std::uint32_t seed_start,
                 const Cohort& cohort, const Summary& baseline,
                 const Summary& fair, const PairedSummary& paired,
                 bool passed) {
  output << "{\"seedStart\":" << seed_start
         << ",\"maximumMoves\":" << kMaximumMoves << ",\"cfpi\":";
  writeSummary(output, baseline);
  output << ",\"fairOnly\":";
  writeSummary(output, fair);
  output << ",\"paired\":";
  writePaired(output, paired);
  output << ",\"wallSeconds\":" << cohort.wall_seconds
         << ",\"passed\":" << (passed ? "true" : "false")
         << ",\"pairs\":";
  writePairs(output, cohort);
  output << '}';
}

struct Options {
  std::string output = "/tmp/drop7-fair-only-horizon.json";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing fair-only option value");
    }
    const std::string argument = argv[index];
    if (argument == "--output") {
      result.output = argv[index + 1];
    } else {
      throw std::invalid_argument("unknown fair-only option " + argument);
    }
  }
  return result;
}

void writeArtifact(const Options& options, const Cohort& screen,
                   const Summary& screen_baseline,
                   const Summary& screen_fair,
                   const PairedSummary& screen_paired, bool screen_passed,
                   const Cohort* confirmation,
                   const Summary* confirmation_baseline,
                   const Summary* confirmation_fair,
                   const PairedSummary* confirmation_paired,
                   bool confirmation_passed, double total_wall) {
  std::ofstream output(options.output);
  if (!output) {
    throw std::runtime_error("could not open fair-only result artifact");
  }
  output << std::setprecision(10)
         << "{\n  \"experiment\":\"historical-fair-only-horizon\",\n"
         << "  \"preregistered\":true,\n"
         << "  \"publicStateOnly\":true,\n"
         << "  \"phaseResidualIncluded\":false,\n"
         << "  \"scoring\":{\"levelBonus\":" << kLevelBonus << "},\n"
         << "  \"fairLeafWeights\":{\"openColumns\":"
         << kOpenColumnsWeight << ",\"heightLoad\":" << kHeightLoadWeight
         << ",\"solidCells\":" << kSolidCellsWeight
         << ",\"crackedCells\":" << kCrackedCellsWeight
         << ",\"numberedCells\":" << kNumberedCellsWeight
         << ",\"highLowNumbers\":" << kHighLowNumbersWeight
         << ",\"directPotential\":" << kDirectPotentialWeight
         << ",\"latentChainPotential\":"
         << kLatentChainPotentialWeight
         << ",\"crackedExposure\":" << kCrackedExposureWeight
         << ",\"solidExposure\":" << kSolidExposureWeight
         << ",\"adjacentOnes\":" << kAdjacentOnesWeight
         << ",\"tripleTwos\":" << kTripleTwosWeight
         << ",\"deadLowNumbers\":" << kDeadLowNumbersWeight
         << ",\"coveredHeightRisk\":" << kCoveredHeightRiskWeight
         << ",\"lowNumberHeightRisk\":" << kLowNumberHeightRiskWeight
         << ",\"dangerHeightSquared\":"
         << kDangerHeightSquaredWeight
         << ",\"roughness\":" << kRoughnessWeight
         << ",\"risePressure\":" << kRisePressureWeight
         << ",\"nextDiscVerticalOptions\":"
         << kNextDiscVerticalOptionsWeight
         << ",\"inertLeafOnlyRevealedCoverOverride\":"
         << kRevealedCoverWeight << "},\n"
         << "  \"search\":{\"depth\":" << kDepth
         << ",\"chanceSamples\":" << kChanceSamples
         << ",\"policySeed\":" << kPolicySeed
         << ",\"maximumWork\":" << kMaximumWork
         << ",\"maximumCacheEntries\":" << kMaximumCacheEntries
         << ",\"terminalUtility\":" << kTerminalUtility
         << ",\"maximumMoves\":" << kMaximumMoves
         << ",\"parallelism\":" << kParallelism << "},\n"
         << "  \"historicalTwoSeedEvidence\":{\"legacyScoring\":true,"
            "\"seeds\":[493879296,493879297],\"moves\":[155,160],"
            "\"numberedCleared\":[331,351],"
            "\"coversRevealed\":[186,201],\"maximumChains\":[7,9]},\n"
         << "  \"screen\":";
  writeCohort(output, kScreenSeedStart, screen, screen_baseline, screen_fair,
              screen_paired, screen_passed);
  output << ",\n  \"confirmation\":";
  if (confirmation == nullptr) {
    output << "null";
  } else {
    writeCohort(output, kConfirmationSeedStart, *confirmation,
                *confirmation_baseline, *confirmation_fair,
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

struct ParityFixture {
  const char* name;
  const char* board;
  int next_disc;
  int moves_remaining;
  double leaf;
  int action;
  std::uint64_t nodes;
  std::uint64_t work;
  std::size_t cache_entries;
  std::uint64_t cache_hits;
  std::array<double, kBoardSize> root_values;
  std::array<double, kBoardSize> expected_scores;
};

constexpr std::array<ParityFixture, 3> kTypeScriptFixtures{{
    {"initial",
     "0000000000000000000000000000000000000000008888888", 4, 5,
     -4057.5, 3, 19'600, 38'430, 557, 213,
     {{-791.2325500000001, -906.8204000000002, -791.0671875,
       -747.3290875, -791.0671875, -906.8204000000002,
       -791.2325500000001}},
     {{0, 0, 0, 0, 0, 0, 0}}},
    {"manual",
     "0000000000000000000000000000000000000009003588488", 6, 3,
     -548.8708333333332, 1, 31'360, 61'670, 893, 157,
     {{14251.606231875001, 16008.323409843752, 15488.496067441407,
       15023.25721203125, 15915.650916679688, 14858.3069646875,
       15066.728959999999}},
     {{0, 0, 0, 0, 0, 0, 0}}},
    {"walked",
     "0000000000000000000006107166886898888888888888888", 3, 5,
     -23504.1, 6, 26'950, 52'850, 767, 283,
     {{-21506.26535234375, -26583.396035, -23511.670576875003,
       -25931.96154, -26695.164760000003, -21331.297685,
       -20260.29117125}},
     {{0, 0, 46, 0, 0, 0, 0}}},
}};

State fixtureState(const ParityFixture& fixture) {
  const std::string_view board(fixture.board);
  if (board.size() != kCellCount) {
    throw std::logic_error("invalid TypeScript parity board length");
  }
  State result;
  for (int index = 0; index < kCellCount; ++index) {
    const char token = board[index];
    if (token < '0' || token > '9') {
      throw std::logic_error("invalid TypeScript parity board token");
    }
    result.board[index] = static_cast<std::uint8_t>(token - '0');
  }
  result.next_disc = static_cast<std::uint8_t>(fixture.next_disc);
  result.moves_remaining = fixture.moves_remaining;
  return result;
}

bool selfTest(std::ostream& output) {
  const bool behavior = cfpi::selfTest(output);
  double maximum_leaf_error = 0.0;
  double maximum_root_error = 0.0;
  double maximum_score_error = 0.0;
  bool fixture_parity = true;
  for (const ParityFixture& fixture : kTypeScriptFixtures) {
    const State state = fixtureState(fixture);
    maximum_leaf_error =
        std::max(maximum_leaf_error, std::abs(fairLeaf(state) - fixture.leaf));
    const SearchDecision decision = chooseFairAction(state);
    fixture_parity = fixture_parity && decision.complete &&
                     decision.completed_depth == kDepth &&
                     decision.action == fixture.action &&
                     decision.nodes == fixture.nodes &&
                     decision.work == fixture.work &&
                     decision.cache_entries == fixture.cache_entries &&
                     decision.cache_hits == fixture.cache_hits;
    for (int column = 0; column < kBoardSize; ++column) {
      maximum_root_error = std::max(
          maximum_root_error,
          std::abs(decision.root_values[column] - fixture.root_values[column]));
      maximum_score_error = std::max(
          maximum_score_error,
          std::abs(decision.root_expected_scores[column] -
                   fixture.expected_scores[column]));
    }
  }
  fixture_parity = fixture_parity && maximum_leaf_error <= 1.0e-9 &&
                   maximum_root_error <= 1.0e-8 &&
                   maximum_score_error <= 1.0e-9;

  const State source = fixtureState(kTypeScriptFixtures[1]);
  State reflected = source;
  reflected.board = cfpi::detail::mirrorBoard(source.board);
  const SearchDecision source_decision = chooseFairAction(source);
  const SearchDecision reflected_decision = chooseFairAction(reflected);
  const bool reflection_safe =
      fairLeaf(source) == fairLeaf(reflected) &&
      reflected_decision.action ==
          kBoardSize - 1 - source_decision.action;
  State metadata = source;
  metadata.score = 8'765'432;
  metadata.level = 91;
  metadata.moves_played = 417;
  const SearchDecision metadata_decision = chooseFairAction(metadata);
  const bool public_only = fairLeaf(source) == fairLeaf(metadata) &&
                           metadata_decision.action == source_decision.action &&
                           metadata_decision.work == source_decision.work;
  State terminal = source;
  terminal.game_over = true;
  const bool terminal_safe = fairLeaf(terminal) == kFairTerminalUtility;
  const bool fixed_protocol =
      kLevelBonus == 17'000 && kDepth == 3 && kChanceSamples == 5 &&
      kMaximumWork == 1'000'000 && kMaximumCacheEntries == 40'000 &&
      kMaximumMoves == 1'000 && kScreenSeedStart == 0x3e95'0000u &&
      kConfirmationSeedStart == 0x3e96'0000u &&
      (kScreenSeedStart >> 24) != 0x7du &&
      (kScreenSeedStart >> 24) != 0xd7u &&
      (kConfirmationSeedStart >> 24) != 0x7du &&
      (kConfirmationSeedStart >> 24) != 0xd7u;
  const bool passed = behavior && fixture_parity && reflection_safe &&
                      public_only && terminal_safe && fixed_protocol;
  output << std::setprecision(12)
         << "FAIR_ONLY_HORIZON_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"typescriptFixtureParity\":"
         << (fixture_parity ? "true" : "false")
         << ",\"fixtureCount\":" << kTypeScriptFixtures.size()
         << ",\"maximumLeafError\":" << maximum_leaf_error
         << ",\"maximumRootError\":" << maximum_root_error
         << ",\"maximumExpectedScoreError\":" << maximum_score_error
         << ",\"reflectionSafe\":"
         << (reflection_safe ? "true" : "false")
         << ",\"publicStateOnly\":" << (public_only ? "true" : "false")
         << ",\"terminalSafe\":" << (terminal_safe ? "true" : "false")
         << ",\"phaseResidualIncluded\":false"
         << ",\"levelBonus\":" << kLevelBonus << "}\n";
  return passed;
}

int run(const Options& options, std::ostream& output) {
  const auto started = std::chrono::steady_clock::now();
  const Cohort screen =
      runCohort(kScreenSeedStart, kScreenGames, "screen");
  const Summary screen_baseline = summarize(screen.baseline);
  const Summary screen_fair = summarize(screen.fair);
  const PairedSummary screen_paired = pairedSummary(screen);
  const bool screen_passed =
      improvesBothMeans(screen_baseline, screen_fair);

  Cohort confirmation;
  Summary confirmation_baseline;
  Summary confirmation_fair;
  PairedSummary confirmation_paired;
  bool confirmation_passed = false;
  if (screen_passed) {
    confirmation = runCohort(kConfirmationSeedStart, kConfirmationGames,
                             "confirmation");
    confirmation_baseline = summarize(confirmation.baseline);
    confirmation_fair = summarize(confirmation.fair);
    confirmation_paired = pairedSummary(confirmation);
    confirmation_passed =
        improvesBothMeans(confirmation_baseline, confirmation_fair);
  }
  const double total_wall = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - started)
                                .count();
  writeArtifact(
      options, screen, screen_baseline, screen_fair, screen_paired,
      screen_passed, screen_passed ? &confirmation : nullptr,
      screen_passed ? &confirmation_baseline : nullptr,
      screen_passed ? &confirmation_fair : nullptr,
      screen_passed ? &confirmation_paired : nullptr, confirmation_passed,
      total_wall);
  output << std::fixed << std::setprecision(3)
         << "FAIR_ONLY_HORIZON_RESULT {\"levelBonus\":" << kLevelBonus
         << ",\"screenCfpiScore\":" << screen_baseline.mean_score
         << ",\"screenCfpiMoves\":" << screen_baseline.mean_moves
         << ",\"screenFairScore\":" << screen_fair.mean_score
         << ",\"screenFairMoves\":" << screen_fair.mean_moves
         << ",\"screenScoreDelta\":" << screen_paired.score.mean
         << ",\"screenMoveDelta\":" << screen_paired.moves.mean
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

}  // namespace drop7::fair_only_horizon

#ifndef DROP7_FAIR_ONLY_HORIZON_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::fair_only_horizon::selfTest(std::cout) ? EXIT_SUCCESS
                                                           : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::fair_only_horizon::parseOptions(argc, argv, 2);
      return drop7::fair_only_horizon::run(options, std::cout);
    }
    std::cerr << "usage: drop7_fair_only_horizon --self-test | --run "
                 "[--output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
#endif
