// Exploratory diagnostic: decompose where a Drop7 Hardcore score actually comes
// from, and measure the whole-game lifetime distribution.
//
// This program changes no existing policy.  It re-uses the frozen fair-only
// depth-3 and depth-4 decision functions unmodified and adds instrumentation
// around the game loop so that every point earned is attributed to exactly one
// of three sources:
//
//   levelPoints = 17,000 * (row rises survived)
//   clearPoints = 70,000 * (board clears)
//   chainPoints = sum over waves of popperCount * floor(7 * depth^2.5)
//
// The engine guarantees score == levelPoints + clearPoints + chainPoints, and
// this program asserts that identity on every game.
//
// It also runs two deliberately weak reference policies so that the marginal
// value of search can be separated from the baseline value of merely staying
// alive.
//
// Cohort role: exploratory development diagnostic on lease
// SEEDLEASE-A51D (0xa51d0000..0xa51dffff), a range that does not appear
// anywhere in the historical ledger, approach sources, or frozen protocols.

// The generated copy has its entry point renamed at build time; see build.sh.
#include "fair-only-depth4-noentry.cpp"


#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace drop7::lifetime {

namespace d4 = drop7::fair_only_depth4;
namespace d3 = drop7::fair_only_horizon;

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
  std::uint64_t waveCount = 0;
  std::array<std::uint64_t, 16> waveDepthHistogram{};
  // Height of the highest occupied row (0 = top row occupied) at the moment
  // just before each rise, averaged.  Diagnoses how close to death the policy
  // habitually runs.
  double meanTopOccupiedRowAtRise = 0.0;
  double wallSeconds = 0.0;
  std::uint64_t work = 0;
};

int topOccupiedRow(const Board& board) {
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      if (board[indexOf(row, column)] != kEmpty) return row;
    }
  }
  return kBoardSize;
}

enum class Policy { kDepth4, kDepth3, kCenterFirst, kLowestColumn, kRandomLegal };

int chooseCenterFirst(const State& state) { return centerFirstMove(state.board); }

int chooseLowestColumn(const State& state) {
  int best = -1;
  int bestHeight = 1000;
  // Deterministic tie-break by the same centre-out order the reference uses.
  for (int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(state.board, column)) continue;
    int height = 0;
    for (int row = 0; row < kBoardSize; ++row) {
      if (state.board[indexOf(row, column)] != kEmpty) {
        height = kBoardSize - row;
        break;
      }
    }
    if (height < bestHeight) {
      bestHeight = height;
      best = column;
    }
  }
  return best;
}

int chooseRandomLegal(const State& state, Mulberry32& random) {
  int count = 0;
  const auto legal = legalColumns(state.board, count);
  if (count == 0) return -1;
  const std::uint32_t pick =
      static_cast<std::uint32_t>((static_cast<std::uint64_t>(random.nextBits()) *
                                  static_cast<std::uint64_t>(count)) >> 32);
  return legal[pick];
}

GameRecord runGame(std::uint32_t seed, Policy policy, int maximumMoves) {
  const auto started = std::chrono::steady_clock::now();
  GameRecord record;
  record.seed = seed;
  State state = initialHeadlessState(seed);
  // Policy-side randomness lives in its own domain and is derived from the
  // policy seed only, never from the environment seed.
  Mulberry32 policyRandom(d3::kPolicySeed ^ 0x5eed'0a5du);

  double riseHeightSum = 0.0;
  int riseHeightCount = 0;

  while (!state.game_over && state.moves_played < maximumMoves) {
    int column = -1;
    switch (policy) {
      case Policy::kDepth4: {
        const d4::SearchDecision decision = d4::chooseDepth4Action(state);
        column = decision.action;
        record.work += decision.work;
        break;
      }
      case Policy::kDepth3: {
        const d3::SearchDecision decision = d3::chooseFairAction(state);
        column = decision.action;
        record.work += decision.work;
        break;
      }
      case Policy::kCenterFirst: column = chooseCenterFirst(state); break;
      case Policy::kLowestColumn: column = chooseLowestColumn(state); break;
      case Policy::kRandomLegal: column = chooseRandomLegal(state, policyRandom); break;
    }
    if (column < 0 || !isLegal(state.board, column)) {
      column = centerFirstMove(state.board);
      if (column < 0) break;
    }

    const bool risingThisMove = state.moves_remaining == 1;
    const Board beforeBoard = state.board;

    MoveResult move;
    if (!playHeadlessMove(state, seed, column, move)) break;

    record.moves += 1;
    record.chainPoints += std::accumulate(
        move.waves.begin(), move.waves.end(), std::int64_t{0},
        [](std::int64_t total, const Wave& wave) { return total + wave.points; });
    for (const Wave& wave : move.waves) {
      record.numberedCleared += static_cast<std::uint64_t>(wave.cleared);
      record.coversRevealed += static_cast<std::uint64_t>(wave.revealed);
      record.maxChainDepth = std::max(record.maxChainDepth, wave.depth);
      record.waveCount += 1;
      const std::size_t bucket = std::min<std::size_t>(
          static_cast<std::size_t>(wave.depth), record.waveDepthHistogram.size() - 1);
      record.waveDepthHistogram[bucket] += 1;
    }
    if (move.level_advanced) {
      record.rises += 1;
      record.levelPoints += kLevelBonus;
    }
    if (move.cleared_board) {
      // playMove can award the clear bonus twice in one move (once for the
      // placement cascade and once after the rise cascade); recompute from the
      // residual instead of assuming a single award.
      record.boardClears += 1;
    }
    if (risingThisMove) {
      riseHeightSum += static_cast<double>(topOccupiedRow(beforeBoard));
      riseHeightCount += 1;
    }
  }

  record.score = state.score;
  record.censored = !state.game_over && record.moves >= maximumMoves;
  // Attribute the residual to board clears, which is the only remaining source
  // and the only one that can be awarded twice within a single move.
  const std::int64_t residual =
      record.score - record.levelPoints - record.chainPoints;
  record.clearPoints = residual;
  record.meanTopOccupiedRowAtRise =
      riseHeightCount > 0 ? riseHeightSum / riseHeightCount : -1.0;
  record.wallSeconds = std::chrono::duration<double>(
                           std::chrono::steady_clock::now() - started).count();
  return record;
}

struct Options {
  std::uint32_t seedStart = 0xa51d'0000u;
  int games = 8;
  int maximumMoves = 2000;
  int threads = static_cast<int>(std::thread::hardware_concurrency());
  Policy policy = Policy::kDepth4;
  std::string policyName = "fair-d4";
  std::string output;
};

Policy parsePolicy(const std::string& name) {
  if (name == "fair-d4") return Policy::kDepth4;
  if (name == "fair-d3") return Policy::kDepth3;
  if (name == "center-first") return Policy::kCenterFirst;
  if (name == "lowest-column") return Policy::kLowestColumn;
  if (name == "random-legal") return Policy::kRandomLegal;
  throw std::invalid_argument("unknown policy " + name);
}

std::mutex progressMutex;

std::vector<GameRecord> runCohort(const Options& options) {
  std::vector<GameRecord> records(static_cast<std::size_t>(options.games));
  std::atomic<int> nextIndex{0};
  std::atomic<int> finished{0};
  const int threads = std::max(1, std::min(options.threads, options.games));
  std::vector<std::thread> pool;
  pool.reserve(static_cast<std::size_t>(threads));
  for (int worker = 0; worker < threads; ++worker) {
    pool.emplace_back([&]() {
      for (;;) {
        const int index = nextIndex.fetch_add(1);
        if (index >= options.games) return;
        const std::uint32_t seed =
            options.seedStart + static_cast<std::uint32_t>(index);
        records[static_cast<std::size_t>(index)] =
            runGame(seed, options.policy, options.maximumMoves);
        const int done = finished.fetch_add(1) + 1;
        const std::lock_guard<std::mutex> lock(progressMutex);
        const GameRecord& r = records[static_cast<std::size_t>(index)];
        std::cerr << "[" << done << "/" << options.games << "] seed 0x"
                  << std::hex << seed << std::dec << " score " << r.score
                  << " moves " << r.moves << " rises " << r.rises
                  << " clears " << r.boardClears
                  << " chain " << r.chainPoints
                  << (r.censored ? " CAPPED" : "")
                  << " (" << std::fixed << std::setprecision(1)
                  << r.wallSeconds << "s)\n";
      }
    });
  }
  for (std::thread& thread : pool) thread.join();
  return records;
}

double quantile(std::vector<double> values, double q) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const double position = q * static_cast<double>(values.size() - 1);
  const std::size_t low = static_cast<std::size_t>(std::floor(position));
  const std::size_t high = static_cast<std::size_t>(std::ceil(position));
  const double weight = position - static_cast<double>(low);
  return values[low] * (1.0 - weight) + values[high] * weight;
}

void writeArtifact(const Options& options, const std::vector<GameRecord>& records,
                   double wallSeconds) {
  std::vector<double> scores;
  std::vector<double> moves;
  std::int64_t levelTotal = 0;
  std::int64_t clearTotal = 0;
  std::int64_t chainTotal = 0;
  std::int64_t scoreTotal = 0;
  std::int64_t riseTotal = 0;
  std::int64_t boardClearTotal = 0;
  std::uint64_t clearedTotal = 0;
  std::uint64_t revealedTotal = 0;
  std::uint64_t moveTotal = 0;
  int censored = 0;
  int identityFailures = 0;
  int maxChain = 0;
  std::array<std::uint64_t, 16> waveHistogram{};
  double riseHeightSum = 0.0;
  int riseHeightCount = 0;
  for (const GameRecord& record : records) {
    scores.push_back(static_cast<double>(record.score));
    moves.push_back(static_cast<double>(record.moves));
    levelTotal += record.levelPoints;
    clearTotal += record.clearPoints;
    chainTotal += record.chainPoints;
    scoreTotal += record.score;
    riseTotal += record.rises;
    boardClearTotal += record.boardClears;
    clearedTotal += record.numberedCleared;
    revealedTotal += record.coversRevealed;
    moveTotal += static_cast<std::uint64_t>(record.moves);
    if (record.censored) censored += 1;
    maxChain = std::max(maxChain, record.maxChainDepth);
    if (record.levelPoints + record.clearPoints + record.chainPoints !=
        record.score) {
      identityFailures += 1;
    }
    if (record.clearPoints % kClearBonus != 0) identityFailures += 1;
    for (std::size_t bucket = 0; bucket < waveHistogram.size(); ++bucket) {
      waveHistogram[bucket] += record.waveDepthHistogram[bucket];
    }
    if (record.meanTopOccupiedRowAtRise >= 0.0) {
      riseHeightSum += record.meanTopOccupiedRowAtRise;
      riseHeightCount += 1;
    }
  }
  const double n = static_cast<double>(records.size());
  const double meanScore = static_cast<double>(scoreTotal) / n;
  const double meanMoves = static_cast<double>(moveTotal) / n;
  double variance = 0.0;
  for (double score : scores) variance += (score - meanScore) * (score - meanScore);
  variance = records.size() > 1 ? variance / (n - 1.0) : 0.0;

  std::ostream* stream = &std::cout;
  std::ofstream file;
  if (!options.output.empty()) {
    file.open(options.output);
    stream = &file;
  }
  std::ostream& out = *stream;
  out << std::setprecision(12);
  out << "{\n";
  out << "  \"format\": \"drop7-score-decomposition-v1\",\n";
  out << "  \"policy\": \"" << options.policyName << "\",\n";
  out << "  \"seedLease\": \"SEEDLEASE-A51D\",\n";
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
      << ", \"sd\": " << std::sqrt(variance) << "},\n";
  out << "  \"moves\": {\"mean\": " << meanMoves
      << ", \"median\": " << quantile(moves, 0.5)
      << ", \"q25\": " << quantile(moves, 0.25)
      << ", \"min\": " << *std::min_element(moves.begin(), moves.end())
      << ", \"max\": " << *std::max_element(moves.begin(), moves.end()) << "},\n";
  out << "  \"censoredGames\": " << censored << ",\n";
  out << "  \"decomposition\": {\n";
  out << "    \"levelPointsTotal\": " << levelTotal << ",\n";
  out << "    \"clearPointsTotal\": " << clearTotal << ",\n";
  out << "    \"chainPointsTotal\": " << chainTotal << ",\n";
  out << "    \"scoreTotal\": " << scoreTotal << ",\n";
  out << "    \"levelShare\": " << static_cast<double>(levelTotal) / static_cast<double>(scoreTotal) << ",\n";
  out << "    \"clearShare\": " << static_cast<double>(clearTotal) / static_cast<double>(scoreTotal) << ",\n";
  out << "    \"chainShare\": " << static_cast<double>(chainTotal) / static_cast<double>(scoreTotal) << "\n";
  out << "  },\n";
  out << "  \"risesPerGame\": " << static_cast<double>(riseTotal) / n << ",\n";
  out << "  \"boardClearsPerGame\": " << static_cast<double>(boardClearTotal) / n << ",\n";
  out << "  \"numberedClearsPerMove\": " << static_cast<double>(clearedTotal) / static_cast<double>(moveTotal) << ",\n";
  out << "  \"coverRevealsPerMove\": " << static_cast<double>(revealedTotal) / static_cast<double>(moveTotal) << ",\n";
  out << "  \"maxChainDepth\": " << maxChain << ",\n";
  out << "  \"meanTopOccupiedRowAtRise\": "
      << (riseHeightCount > 0 ? riseHeightSum / riseHeightCount : -1.0) << ",\n";
  out << "  \"pointsPerMove\": " << static_cast<double>(scoreTotal) / static_cast<double>(moveTotal) << ",\n";
  out << "  \"waveDepthHistogram\": [";
  for (std::size_t bucket = 0; bucket < waveHistogram.size(); ++bucket) {
    if (bucket != 0) out << ", ";
    out << waveHistogram[bucket];
  }
  out << "],\n";
  out << "  \"games_detail\": [\n";
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
        << ", \"maxChainDepth\": " << r.maxChainDepth
        << ", \"meanTopOccupiedRowAtRise\": " << r.meanTopOccupiedRowAtRise
        << ", \"wallSeconds\": " << r.wallSeconds
        << ", \"work\": " << r.work << "}";
  }
  out << "\n  ]\n}\n";
}

Options parseOptions(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string key = argv[index];
    const std::string value = argv[index + 1];
    if (key == "--seed-start") {
      options.seedStart = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
    } else if (key == "--games") {
      options.games = std::stoi(value);
    } else if (key == "--max-moves") {
      options.maximumMoves = std::stoi(value);
    } else if (key == "--threads") {
      options.threads = std::stoi(value);
    } else if (key == "--policy") {
      options.policyName = value;
      options.policy = parsePolicy(value);
    } else if (key == "--output") {
      options.output = value;
    } else {
      throw std::invalid_argument("unknown option " + key);
    }
  }
  if (options.games < 1) throw std::invalid_argument("--games must be positive");
  return options;
}

}  // namespace drop7::lifetime

int main(int argc, char** argv) {
  try {
    const auto options = drop7::lifetime::parseOptions(argc, argv);
    const auto started = std::chrono::steady_clock::now();
    const auto records = drop7::lifetime::runCohort(options);
    const double wall =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    drop7::lifetime::writeArtifact(options, records, wall);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "decompose failed: " << error.what() << '\n';
    return 1;
  }
}
