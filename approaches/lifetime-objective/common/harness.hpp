#pragma once
// Shared exploratory whole-game harness.
//
// Provides one instrumented game loop, one cohort runner, and one artifact
// writer so that every exploratory arm reports the same fields and the same
// score decomposition.  It deliberately does not define any policy: a policy is
// supplied as a per-thread decider factory.
//
// Score decomposition identity enforced on every game:
//   score == 17,000 * rises + 70,000 * boardClears + sum(waveDepthPoints)

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace drop7::lifetime {

struct GameRecord {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  int rises = 0;
  int boardClears = 0;
  std::int64_t levelPoints = 0;
  std::int64_t clearPoints = 0;
  std::int64_t chainPoints = 0;
  std::uint64_t numberedCleared = 0;
  std::uint64_t coversRevealed = 0;
  int maxChainDepth = 0;
  std::array<std::uint64_t, 16> waveDepthHistogram{};
  double meanTopOccupiedRowAtRise = 0.0;
  double meanOccupancy = 0.0;
  double wallSeconds = 0.0;
  std::uint64_t work = 0;
  std::vector<int> actions;
};

inline int topOccupiedRow(const Board& board) {
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      if (board[indexOf(row, column)] != kEmpty) return row;
    }
  }
  return kBoardSize;
}

inline int occupiedCells(const Board& board) {
  int total = 0;
  for (std::uint8_t cell : board) {
    if (cell != kEmpty) ++total;
  }
  return total;
}

// Decider signature: int(const State&, std::uint64_t& work)
template <typename Decider>
GameRecord runGame(std::uint32_t seed, Decider& decide, int maximumMoves,
                   bool recordActions) {
  const auto started = std::chrono::steady_clock::now();
  GameRecord record;
  record.seed = seed;
  State state = initialHeadlessState(seed);

  double riseHeightSum = 0.0;
  int riseHeightCount = 0;
  double occupancySum = 0.0;

  while (!state.game_over && state.moves_played < maximumMoves) {
    int column = decide(state, record.work);
    if (column < 0 || !isLegal(state.board, column)) {
      column = centerFirstMove(state.board);
      if (column < 0) break;
    }
    if (recordActions) record.actions.push_back(column);

    const bool risingThisMove = state.moves_remaining == 1;
    const Board beforeBoard = state.board;
    occupancySum += static_cast<double>(occupiedCells(beforeBoard));

    MoveResult move;
    if (!playHeadlessMove(state, seed, column, move)) break;

    record.moves += 1;
    for (const Wave& wave : move.waves) {
      record.chainPoints += wave.points;
      record.numberedCleared += static_cast<std::uint64_t>(wave.cleared);
      record.coversRevealed += static_cast<std::uint64_t>(wave.revealed);
      record.maxChainDepth = std::max(record.maxChainDepth, wave.depth);
      const std::size_t bucket = std::min<std::size_t>(
          static_cast<std::size_t>(wave.depth),
          record.waveDepthHistogram.size() - 1);
      record.waveDepthHistogram[bucket] += 1;
    }
    if (move.level_advanced) {
      record.rises += 1;
      record.levelPoints += kLevelBonus;
    }
    if (move.cleared_board) record.boardClears += 1;
    if (risingThisMove) {
      riseHeightSum += static_cast<double>(topOccupiedRow(beforeBoard));
      riseHeightCount += 1;
    }
  }

  record.score = state.score;
  record.censored = !state.game_over && record.moves >= maximumMoves;
  record.clearPoints = record.score - record.levelPoints - record.chainPoints;
  record.meanTopOccupiedRowAtRise =
      riseHeightCount > 0 ? riseHeightSum / riseHeightCount : -1.0;
  record.meanOccupancy =
      record.moves > 0 ? occupancySum / static_cast<double>(record.moves) : 0.0;
  record.wallSeconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();
  return record;
}

struct CohortOptions {
  std::uint32_t seedStart = 0xa51d'0000u;
  int games = 8;
  int maximumMoves = 2000;
  int threads = static_cast<int>(std::thread::hardware_concurrency());
  bool recordActions = false;
  bool quiet = false;
};

inline std::mutex progressMutex;

// DeciderFactory signature: Decider(), called once per worker thread.
template <typename DeciderFactory>
std::vector<GameRecord> runCohort(const CohortOptions& options,
                                  DeciderFactory factory) {
  std::vector<GameRecord> records(static_cast<std::size_t>(options.games));
  std::atomic<int> nextIndex{0};
  std::atomic<int> finished{0};
  const int threads = std::max(1, std::min(options.threads, options.games));
  std::vector<std::thread> pool;
  pool.reserve(static_cast<std::size_t>(threads));
  for (int worker = 0; worker < threads; ++worker) {
    pool.emplace_back([&]() {
      auto decide = factory();
      for (;;) {
        const int index = nextIndex.fetch_add(1);
        if (index >= options.games) return;
        const std::uint32_t seed =
            options.seedStart + static_cast<std::uint32_t>(index);
        records[static_cast<std::size_t>(index)] =
            runGame(seed, decide, options.maximumMoves, options.recordActions);
        if (options.quiet) continue;
        const int done = finished.fetch_add(1) + 1;
        const std::lock_guard<std::mutex> lock(progressMutex);
        const GameRecord& r = records[static_cast<std::size_t>(index)];
        std::cerr << "[" << done << "/" << options.games << "] seed 0x"
                  << std::hex << seed << std::dec << " score " << r.score
                  << " moves " << r.moves << " rises " << r.rises
                  << " clears " << r.boardClears << " chain " << r.chainPoints
                  << (r.censored ? " CAPPED" : "") << " (" << std::fixed
                  << std::setprecision(1) << r.wallSeconds << "s)\n";
      }
    });
  }
  for (std::thread& thread : pool) thread.join();
  return records;
}

inline double quantile(std::vector<double> values, double q) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const double position = q * static_cast<double>(values.size() - 1);
  const auto low = static_cast<std::size_t>(std::floor(position));
  const auto high = static_cast<std::size_t>(std::ceil(position));
  const double weight = position - static_cast<double>(low);
  return values[low] * (1.0 - weight) + values[high] * weight;
}

// One-sided lower bound on the mean by percentile bootstrap over whole games.
inline double bootstrapLowerBound(const std::vector<double>& values,
                                  double alpha, int resamples,
                                  std::uint32_t bootstrapSeed) {
  if (values.size() < 2) return values.empty() ? 0.0 : values.front();
  Mulberry32 random(bootstrapSeed);
  std::vector<double> means;
  means.reserve(static_cast<std::size_t>(resamples));
  const std::size_t n = values.size();
  for (int draw = 0; draw < resamples; ++draw) {
    double total = 0.0;
    for (std::size_t index = 0; index < n; ++index) {
      const auto pick = static_cast<std::size_t>(
          (static_cast<std::uint64_t>(random.nextBits()) * n) >> 32);
      total += values[pick];
    }
    means.push_back(total / static_cast<double>(n));
  }
  return quantile(means, alpha);
}

inline void writeArtifact(std::ostream& out, const std::string& policyName,
                          const std::string& configJson,
                          const CohortOptions& options,
                          const std::vector<GameRecord>& records,
                          double wallSeconds) {
  std::vector<double> scores;
  std::vector<double> moves;
  std::int64_t levelTotal = 0, clearTotal = 0, chainTotal = 0, scoreTotal = 0;
  std::int64_t riseTotal = 0, boardClearTotal = 0;
  std::uint64_t clearedTotal = 0, revealedTotal = 0, moveTotal = 0, workTotal = 0;
  int censored = 0, identityFailures = 0, maxChain = 0;
  std::array<std::uint64_t, 16> waveHistogram{};
  double riseHeightSum = 0.0, occupancySum = 0.0;
  int riseHeightCount = 0;

  for (const GameRecord& r : records) {
    scores.push_back(static_cast<double>(r.score));
    moves.push_back(static_cast<double>(r.moves));
    levelTotal += r.levelPoints;
    clearTotal += r.clearPoints;
    chainTotal += r.chainPoints;
    scoreTotal += r.score;
    riseTotal += r.rises;
    boardClearTotal += r.boardClears;
    clearedTotal += r.numberedCleared;
    revealedTotal += r.coversRevealed;
    moveTotal += static_cast<std::uint64_t>(r.moves);
    workTotal += r.work;
    if (r.censored) ++censored;
    maxChain = std::max(maxChain, r.maxChainDepth);
    if (r.levelPoints + r.clearPoints + r.chainPoints != r.score) ++identityFailures;
    if (r.clearPoints % kClearBonus != 0) ++identityFailures;
    for (std::size_t b = 0; b < waveHistogram.size(); ++b) {
      waveHistogram[b] += r.waveDepthHistogram[b];
    }
    if (r.meanTopOccupiedRowAtRise >= 0.0) {
      riseHeightSum += r.meanTopOccupiedRowAtRise;
      ++riseHeightCount;
    }
    occupancySum += r.meanOccupancy;
  }

  const double n = static_cast<double>(records.size());
  const double meanScore = static_cast<double>(scoreTotal) / n;
  const double meanMoves = static_cast<double>(moveTotal) / n;
  double scoreVariance = 0.0, moveVariance = 0.0;
  for (double s : scores) scoreVariance += (s - meanScore) * (s - meanScore);
  for (double m : moves) moveVariance += (m - meanMoves) * (m - meanMoves);
  scoreVariance = records.size() > 1 ? scoreVariance / (n - 1.0) : 0.0;
  moveVariance = records.size() > 1 ? moveVariance / (n - 1.0) : 0.0;

  out << std::setprecision(12);
  out << "{\n";
  out << "  \"format\": \"drop7-lifetime-cohort-v1\",\n";
  out << "  \"policy\": \"" << policyName << "\",\n";
  out << "  \"config\": " << configJson << ",\n";
  out << "  \"seedLease\": \"SEEDLEASE-A51D\",\n";
  out << "  \"dataRole\": \"exploratory-development-diagnostic\",\n";
  out << "  \"seedStartHex\": \"0x" << std::hex << options.seedStart << std::dec << "\",\n";
  out << "  \"games\": " << records.size() << ",\n";
  out << "  \"maximumMoves\": " << options.maximumMoves << ",\n";
  out << "  \"threads\": " << options.threads << ",\n";
  out << "  \"wallSeconds\": " << wallSeconds << ",\n";
  out << "  \"scoreIdentityFailures\": " << identityFailures << ",\n";
  out << "  \"score\": {\"mean\": " << meanScore
      << ", \"median\": " << quantile(scores, 0.5)
      << ", \"q25\": " << quantile(scores, 0.25)
      << ", \"min\": " << *std::min_element(scores.begin(), scores.end())
      << ", \"max\": " << *std::max_element(scores.begin(), scores.end())
      << ", \"sd\": " << std::sqrt(scoreVariance)
      << ", \"bootstrapLower95\": "
      << bootstrapLowerBound(scores, 0.05, 20000, 0xb007'5eedu) << "},\n";
  out << "  \"moves\": {\"mean\": " << meanMoves
      << ", \"median\": " << quantile(moves, 0.5)
      << ", \"q25\": " << quantile(moves, 0.25)
      << ", \"min\": " << *std::min_element(moves.begin(), moves.end())
      << ", \"max\": " << *std::max_element(moves.begin(), moves.end())
      << ", \"sd\": " << std::sqrt(moveVariance) << "},\n";
  out << "  \"censoredGames\": " << censored << ",\n";
  out << "  \"decomposition\": {\"levelPointsTotal\": " << levelTotal
      << ", \"clearPointsTotal\": " << clearTotal
      << ", \"chainPointsTotal\": " << chainTotal
      << ", \"scoreTotal\": " << scoreTotal
      << ", \"levelShare\": " << static_cast<double>(levelTotal) / static_cast<double>(scoreTotal)
      << ", \"clearShare\": " << static_cast<double>(clearTotal) / static_cast<double>(scoreTotal)
      << ", \"chainShare\": " << static_cast<double>(chainTotal) / static_cast<double>(scoreTotal)
      << "},\n";
  out << "  \"risesPerGame\": " << static_cast<double>(riseTotal) / n << ",\n";
  out << "  \"boardClearsPerGame\": " << static_cast<double>(boardClearTotal) / n << ",\n";
  out << "  \"numberedClearsPerMove\": " << static_cast<double>(clearedTotal) / static_cast<double>(moveTotal) << ",\n";
  out << "  \"coverRevealsPerMove\": " << static_cast<double>(revealedTotal) / static_cast<double>(moveTotal) << ",\n";
  out << "  \"requiredClearsPerMove\": 2.4,\n";
  out << "  \"requiredRevealsPerMove\": 1.4,\n";
  out << "  \"maxChainDepth\": " << maxChain << ",\n";
  out << "  \"meanTopOccupiedRowAtRise\": "
      << (riseHeightCount > 0 ? riseHeightSum / riseHeightCount : -1.0) << ",\n";
  out << "  \"meanOccupiedCells\": " << occupancySum / n << ",\n";
  out << "  \"pointsPerMove\": " << static_cast<double>(scoreTotal) / static_cast<double>(moveTotal) << ",\n";
  out << "  \"workPerMove\": " << static_cast<double>(workTotal) / static_cast<double>(moveTotal) << ",\n";
  out << "  \"waveDepthHistogram\": [";
  for (std::size_t b = 0; b < waveHistogram.size(); ++b) {
    if (b != 0) out << ", ";
    out << waveHistogram[b];
  }
  out << "],\n";
  out << "  \"gamesDetail\": [\n";
  for (std::size_t index = 0; index < records.size(); ++index) {
    const GameRecord& r = records[index];
    if (index != 0) out << ",\n";
    out << "    {\"seedHex\": \"0x" << std::hex << r.seed << std::dec
        << "\", \"score\": " << r.score << ", \"moves\": " << r.moves
        << ", \"censored\": " << (r.censored ? "true" : "false")
        << ", \"rises\": " << r.rises
        << ", \"boardClears\": " << r.boardClears
        << ", \"levelPoints\": " << r.levelPoints
        << ", \"clearPoints\": " << r.clearPoints
        << ", \"chainPoints\": " << r.chainPoints
        << ", \"numberedCleared\": " << r.numberedCleared
        << ", \"coversRevealed\": " << r.coversRevealed
        << ", \"clearsPerMove\": "
        << (r.moves > 0 ? static_cast<double>(r.numberedCleared) / r.moves : 0.0)
        << ", \"revealsPerMove\": "
        << (r.moves > 0 ? static_cast<double>(r.coversRevealed) / r.moves : 0.0)
        << ", \"maxChainDepth\": " << r.maxChainDepth
        << ", \"meanTopOccupiedRowAtRise\": " << r.meanTopOccupiedRowAtRise
        << ", \"meanOccupiedCells\": " << r.meanOccupancy
        << ", \"wallSeconds\": " << r.wallSeconds << ", \"work\": " << r.work << "}";
  }
  out << "\n  ]\n}\n";
}

}  // namespace drop7::lifetime
