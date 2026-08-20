#define DROP7_FAIR_ONLY_HORIZON_LIBRARY
#include "../../fair-expectimax/reference/fair-only-horizon.cpp"
#undef DROP7_FAIR_ONLY_HORIZON_LIBRARY

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <type_traits>
#include <utility>
#include <vector>

// Implements a public, causal constructive strategy that plans across a
// complete five-drop rise cycle with a bounded stochastic beam.  Its terminal
// objective is an
// explicit target shape: a broad spectrum of column heights, accessible edge
// covers, safe surface caps, and a reservoir of untriggered high discs whose
// exact (next-disc,column) trigger keys overlap.  The oracle curriculum is used
// only to measure that target motif; no oracle action, future disc, reveal RNG,
// seed, score, level, move index, or history enters the deployed policy.
namespace drop7::constructive_spectrum {

namespace fair = drop7::fair_only_horizon;
namespace detail = drop7::cfpi::detail;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kAnalysisSeedStart = 0x3d69'0000u;
constexpr std::uint32_t kAnalysisSeedEndExclusive = 0x3d69'0040u;
constexpr int kAnalysisGames = 64;
constexpr std::uint32_t kStageASeedStart = 0x3d69'c000u;
constexpr std::uint32_t kStageASeedEndExclusive = 0x3d69'c020u;
constexpr int kStageAGames = 32;
constexpr int kMaximumMoves = 1'000;
constexpr int kFirstAnalysisMove = 10;
constexpr int kChanceSamples = 7;
constexpr int kMinimumHorizon = 3;
constexpr int kMaximumHorizon = 7;
constexpr int kTacticalDepth = 3;
constexpr int kTacticalShortlist = 2;
constexpr double kTacticalNearTie = 2'500.0;
constexpr int kDefaultThreads = 8;
constexpr std::uint32_t kPolicySeed = 0x4353'5031u;  // "CSP1"
constexpr double kTerminalValue = -1.0e9;
constexpr double kWallLimitSeconds = 30.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr std::array<int, kBoardSize> kColumnOrder{{3, 2, 4, 1, 5, 0, 6}};

static_assert(kLevelBonus == 17'000);
static_assert(kMovesPerLevel == 5);
static_assert(kAnalysisSeedEndExclusive - kAnalysisSeedStart ==
              kAnalysisGames);
static_assert(kStageASeedEndExclusive - kStageASeedStart == kStageAGames);
static_assert((kAnalysisSeedStart >> 16u) == 0x3d69u &&
              ((kAnalysisSeedEndExclusive - 1u) >> 16u) == 0x3d69u &&
              (kStageASeedStart >> 16u) == 0x3d69u &&
              ((kStageASeedEndExclusive - 1u) >> 16u) == 0x3d69u);
static_assert(kAnalysisSeedEndExclusive <= kStageASeedStart);
static_assert((kAnalysisSeedStart >> 24u) != 0x4du &&
              (kAnalysisSeedStart >> 24u) != 0x7du &&
              (kAnalysisSeedStart >> 24u) != 0xd7u);
static_assert((kStageASeedStart >> 24u) != 0x4du &&
              (kStageASeedStart >> 24u) != 0x7du &&
              (kStageASeedStart >> 24u) != 0xd7u);

struct PublicState {
  Board board{};
  std::uint8_t next_disc = 1;
  std::uint8_t moves_remaining = kMovesPerLevel;
  bool terminal = false;

  bool operator==(const PublicState&) const = default;
};

PublicState publicState(const State& source) {
  if (source.next_disc < 1 || source.next_disc > kBoardSize ||
      source.moves_remaining < 0 || source.moves_remaining > kMovesPerLevel ||
      (!source.game_over && source.moves_remaining < 1)) {
    throw std::invalid_argument("invalid public constructive-spectrum state");
  }
  for (const std::uint8_t cell : source.board) {
    if (cell > kCracked) throw std::invalid_argument("invalid board token");
  }
  return {source.board, source.next_disc,
          static_cast<std::uint8_t>(source.moves_remaining), source.game_over};
}

State materialize(const PublicState& source) {
  State result;
  result.board = source.board;
  result.next_disc = source.next_disc;
  result.moves_remaining = source.moves_remaining;
  result.game_over = source.terminal;
  return result;
}

PublicState mirror(const PublicState& source) {
  PublicState result = source;
  result.board = detail::mirrorBoard(source.board);
  return result;
}

PublicState canonicalPublic(const PublicState& source, bool& mirrored) {
  mirrored = detail::mirroredRepresentationIsSmaller(source.board);
  return mirrored ? mirror(source) : source;
}

std::uint64_t peakRssBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ull;
#endif
}

void enforceRssLimit() {
  if (peakRssBytes() > kRssLimitBytes) {
    throw std::runtime_error("constructive spectrum exceeded 256 MiB RSS cap");
  }
}

struct Deadline {
  Clock::time_point started = Clock::now();

  double seconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }

  void check() const {
    if (seconds() > kWallLimitSeconds) {
      throw std::runtime_error("constructive spectrum exceeded 30 minute cap");
    }
  }
};

std::array<int, kBoardSize> columnHeights(const Board& board) {
  std::array<int, kBoardSize> heights{};
  for (int column = 0; column < kBoardSize; ++column) {
    for (int row = 0; row < kBoardSize; ++row) {
      heights[column] += board[indexOf(row, column)] != kEmpty;
    }
  }
  return heights;
}

int topRow(const Board& board, int column) {
  for (int row = 0; row < kBoardSize; ++row) {
    if (board[indexOf(row, column)] != kEmpty) return row;
  }
  return kBoardSize;
}

struct TriggerKeys {
  int legal = 0;
  int any = 0;
  int multiple = 0;
  int placed = 0;
  int high = 0;
  int cover_contact = 0;
  int distinct_discs = 0;
  int distinct_columns = 0;
};

TriggerKeys exactTriggerKeys(const Board& source) {
  TriggerKeys result;
  std::array<bool, kBoardSize + 1> discs{};
  std::array<bool, kBoardSize> columns{};
  for (int disc = 1; disc <= kBoardSize; ++disc) {
    for (int column = 0; column < kBoardSize; ++column) {
      Board board = source;
      if (!placeDisc(board, column, static_cast<std::uint8_t>(disc))) continue;
      ++result.legal;
      int placed_index = -1;
      for (int row = 0; row < kBoardSize; ++row) {
        if (source[indexOf(row, column)] == kEmpty &&
            board[indexOf(row, column)] != kEmpty) {
          placed_index = indexOf(row, column);
          break;
        }
      }
      int count = 0;
      const auto poppers = findPoppers(board, count);
      if (count == 0) continue;
      ++result.any;
      result.multiple += count >= 2;
      discs[disc] = true;
      columns[column] = true;
      bool has_high = false;
      bool touches_cover = false;
      for (int offset = 0; offset < count; ++offset) {
        const int cell_index = poppers[offset];
        result.placed += cell_index == placed_index;
        has_high = has_high || board[cell_index] >= 5;
        const int row = cell_index / kBoardSize;
        const int pop_column = cell_index % kBoardSize;
        for (const auto [dr, dc] :
             std::array<std::array<int, 2>, 4>{{
                 {{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}},
             }}) {
          const int nr = row + dr;
          const int nc = pop_column + dc;
          if (!inside(nr, nc)) continue;
          const auto neighbor = board[indexOf(nr, nc)];
          touches_cover = touches_cover || neighbor == kSolid ||
                          neighbor == kCracked;
        }
      }
      result.high += has_high;
      result.cover_contact += touches_cover;
    }
  }
  result.distinct_discs =
      std::accumulate(discs.begin(), discs.end(), 0);
  result.distinct_columns =
      std::accumulate(columns.begin(), columns.end(), 0);
  return result;
}

enum Metric : int {
  kOccupancy,
  kCovers,
  kMaximumHeight,
  kHeightMean,
  kHeightStddev,
  kHeightRange,
  kDistinctHeights,
  kAdjacentHeightSteps,
  kUnitHeightSteps,
  kRoughness,
  kInteriorWells,
  kEdgeHeight,
  kSurfaceNumbered,
  kSurfaceHigh,
  kSurfaceLow,
  kSurfaceCover,
  kHighReservoir,
  kHighReady,
  kSameTargetHighPairs,
  kEdgeCovers,
  kEdgeCoverFrontier,
  kCoverNumberContacts,
  kTriggerAny,
  kTriggerMultiple,
  kTriggerPlaced,
  kTriggerHigh,
  kTriggerCover,
  kTriggerDiscBreadth,
  kTriggerColumnBreadth,
  kMetricCount,
};

constexpr std::array<std::string_view, kMetricCount> kMetricNames{{
    "occupancy",          "covers",             "maximumHeight",
    "heightMean",        "heightStddev",       "heightRange",
    "distinctHeights",   "adjacentHeightSteps", "unitHeightSteps",
    "roughness",          "interiorWells",      "edgeHeight",
    "surfaceNumbered",    "surfaceHigh",        "surfaceLow",
    "surfaceCover",       "highReservoir",      "highReady",
    "sameTargetHighPairs", "edgeCovers",         "edgeCoverFrontier",
    "coverNumberContacts", "triggerAny",         "triggerMultiple",
    "triggerPlaced",      "triggerHigh",        "triggerCover",
    "triggerDiscBreadth", "triggerColumnBreadth",
}};

using Metrics = std::array<double, kMetricCount>;

Metrics extractMetrics(const PublicState& state) {
  Metrics result{};
  const auto heights = columnHeights(state.board);
  const double mean = std::accumulate(heights.begin(), heights.end(), 0.0) /
                      static_cast<double>(kBoardSize);
  std::array<bool, kBoardSize + 1> seen_heights{};
  int minimum = kBoardSize;
  int maximum = 0;
  std::array<int, kBoardSize + 1> ready_high_by_target{};
  for (int column = 0; column < kBoardSize; ++column) {
    minimum = std::min(minimum, heights[column]);
    maximum = std::max(maximum, heights[column]);
    seen_heights[heights[column]] = true;
    result[kOccupancy] += heights[column];
    result[kHeightStddev] +=
        (static_cast<double>(heights[column]) - mean) *
        (static_cast<double>(heights[column]) - mean);
    if (column > 0) {
      const int difference = std::abs(heights[column] - heights[column - 1]);
      result[kRoughness] += difference;
      result[kAdjacentHeightSteps] += difference > 0;
      result[kUnitHeightSteps] += difference == 1;
    }
    if (column > 0 && column + 1 < kBoardSize &&
        heights[column] < heights[column - 1] &&
        heights[column] < heights[column + 1]) {
      ++result[kInteriorWells];
    }
    if (column == 0 || column == kBoardSize - 1) {
      result[kEdgeHeight] += heights[column];
    }
    const int surface_row = topRow(state.board, column);
    if (surface_row < kBoardSize) {
      const std::uint8_t cap = state.board[indexOf(surface_row, column)];
      result[kSurfaceNumbered] += isNumbered(cap);
      result[kSurfaceHigh] += cap >= 5 && cap <= 7;
      result[kSurfaceLow] += cap == 1 || cap == 2;
      result[kSurfaceCover] += cap == kSolid || cap == kCracked;
    }
  }
  result[kHeightMean] = mean;
  result[kHeightStddev] = std::sqrt(result[kHeightStddev] / kBoardSize);
  result[kMaximumHeight] = maximum;
  result[kHeightRange] = maximum - minimum;
  result[kDistinctHeights] =
      std::accumulate(seen_heights.begin(), seen_heights.end(), 0);

  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      const std::uint8_t cell = state.board[indexOf(row, column)];
      if (cell == kSolid || cell == kCracked) {
        ++result[kCovers];
        if (column == 0 || column == kBoardSize - 1) {
          ++result[kEdgeCovers];
          if (row == topRow(state.board, column)) {
            ++result[kEdgeCoverFrontier];
          }
        }
        for (const auto [dr, dc] :
             std::array<std::array<int, 2>, 4>{{
                 {{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}},
             }}) {
          const int nr = row + dr;
          const int nc = column + dc;
          if (inside(nr, nc) &&
              isNumbered(state.board[indexOf(nr, nc)])) {
            ++result[kCoverNumberContacts];
          }
        }
      }
      if (cell < 5 || cell > 7) continue;
      const int horizontal = lineLength(state.board, row, column, false);
      const int vertical = lineLength(state.board, row, column, true);
      const int deficit = static_cast<int>(cell) -
                          std::max(horizontal, vertical);
      if (deficit > 0) ++result[kHighReservoir];
      if (deficit == 1 || deficit == 2) {
        ++result[kHighReady];
        ++ready_high_by_target[cell];
      }
    }
  }
  for (int target = 5; target <= 7; ++target) {
    result[kSameTargetHighPairs] +=
        ready_high_by_target[target] * (ready_high_by_target[target] - 1) / 2;
  }
  const TriggerKeys triggers = exactTriggerKeys(state.board);
  result[kTriggerAny] = triggers.any;
  result[kTriggerMultiple] = triggers.multiple;
  result[kTriggerPlaced] = triggers.placed;
  result[kTriggerHigh] = triggers.high;
  result[kTriggerCover] = triggers.cover_contact;
  result[kTriggerDiscBreadth] = triggers.distinct_discs;
  result[kTriggerColumnBreadth] = triggers.distinct_columns;
  return result;
}

struct CorpusStats {
  std::size_t count = 0;
  Metrics sum{};
  Metrics sum_squared{};

  void add(const PublicState& state) {
    const Metrics metrics = extractMetrics(state);
    ++count;
    for (int index = 0; index < kMetricCount; ++index) {
      sum[index] += metrics[index];
      sum_squared[index] += metrics[index] * metrics[index];
    }
  }

  double mean(int metric) const {
    return sum[metric] / static_cast<double>(count);
  }

  double standardDeviation(int metric) const {
    const double average = mean(metric);
    return std::sqrt(std::max(
        0.0, sum_squared[metric] / static_cast<double>(count) -
                 average * average));
  }
};

std::vector<PublicState> loadCurriculum(std::string_view path) {
  std::ifstream input{std::string(path)};
  if (!input) throw std::runtime_error("cannot open curriculum states");
  std::vector<PublicState> result;
  std::string line;
  while (std::getline(input, line)) {
    const std::string board_tag = "\"board\":\"";
    const std::string next_tag = "\"nextDisc\":";
    const std::string phase_tag = "\"movesRemaining\":";
    const auto board_begin = line.find(board_tag);
    const auto next_begin = line.find(next_tag);
    const auto phase_begin = line.find(phase_tag);
    if (board_begin == std::string::npos || next_begin == std::string::npos ||
        phase_begin == std::string::npos) {
      throw std::runtime_error("malformed curriculum record");
    }
    const auto begin = board_begin + board_tag.size();
    const auto end = line.find('"', begin);
    if (end == std::string::npos || end - begin != kCellCount) {
      throw std::runtime_error("invalid curriculum board encoding");
    }
    PublicState state;
    for (int index = 0; index < kCellCount; ++index) {
      const char token = line[begin + index];
      if (token < '0' || token > '9') {
        throw std::runtime_error("invalid curriculum board token");
      }
      state.board[index] = static_cast<std::uint8_t>(token - '0');
    }
    state.next_disc = static_cast<std::uint8_t>(
        std::stoi(line.substr(next_begin + next_tag.size())));
    state.moves_remaining = static_cast<std::uint8_t>(
        std::stoi(line.substr(phase_begin + phase_tag.size())));
    (void)publicState(materialize(state));
    result.push_back(state);
  }
  if (result.size() != 4'096) {
    throw std::runtime_error("curriculum must contain exactly 4096 states");
  }
  return result;
}

int chooseFairD1(const PublicState& source) {
  if (source.terminal) return -1;
  bool mirrored = false;
  const PublicState canonical = canonicalPublic(source, mirrored);
  fair::SearchContext context;
  const fair::RootEvaluation root =
      fair::rootDecision(materialize(canonical), 1, context);
  if (root.action < 0 || context.work > 70 || !context.cache.empty()) {
    throw std::runtime_error("exact fair D1 did not complete");
  }
  return mirrored ? kBoardSize - 1 - root.action : root.action;
}

bool allowedAnalysisSeed(std::uint32_t seed) {
  return seed >= kAnalysisSeedStart && seed < kAnalysisSeedEndExclusive;
}

bool allowedStageASeed(std::uint32_t seed) {
  return seed >= kStageASeedStart && seed < kStageASeedEndExclusive;
}

void requireAnalysisSeed(std::uint32_t seed) {
  if (!allowedAnalysisSeed(seed)) {
    throw std::invalid_argument("seed outside exact 0x3d690000 analysis bank");
  }
}

void requireStageASeed(std::uint32_t seed) {
  if (!allowedStageASeed(seed)) {
    throw std::invalid_argument("seed outside exact 0x3d69c000 Stage-A bank");
  }
}

struct AnalysisTrace {
  CorpusStats states;
  std::int64_t total_score = 0;
  int total_moves = 0;
  int total_clears = 0;
  int total_reveals = 0;
};

AnalysisTrace generateD1Analysis(const Deadline& deadline) {
  AnalysisTrace result;
  for (std::uint32_t seed = kAnalysisSeedStart;
       seed < kAnalysisSeedEndExclusive; ++seed) {
    requireAnalysisSeed(seed);
    State state = initialHeadlessState(seed);
    int clears = 0;
    int reveals = 0;
    while (!state.game_over && state.moves_played < kMaximumMoves) {
      deadline.check();
      const int action = chooseFairD1(publicState(state));
      MoveResult move;
      if (!playHeadlessMove(state, seed, action, move)) {
        throw std::runtime_error("analysis D1 transition failed");
      }
      for (const Wave& wave : move.waves) {
        clears += wave.cleared;
        reveals += wave.revealed;
      }
      if (!state.game_over && state.moves_played >= kFirstAnalysisMove) {
        result.states.add(publicState(state));
      }
    }
    result.total_score += state.score;
    result.total_moves += state.moves_played;
    result.total_clears += clears;
    result.total_reveals += reveals;
  }
  return result;
}

void writeCorpus(std::ostream& output, const CorpusStats& corpus) {
  output << "{\"states\":" << corpus.count << ",\"metrics\":{";
  for (int index = 0; index < kMetricCount; ++index) {
    if (index > 0) output << ',';
    output << '\"' << kMetricNames[index] << "\":{\"mean\":"
           << corpus.mean(index) << ",\"sd\":"
           << corpus.standardDeviation(index) << '}';
  }
  output << "}}";
}

void writeAnalysis(std::string_view output_path,
                   const std::vector<PublicState>& oracle,
                   const AnalysisTrace& baseline, double seconds) {
  CorpusStats oracle_stats;
  for (const PublicState& state : oracle) oracle_stats.add(state);
  std::ofstream output{std::string(output_path)};
  if (!output) throw std::runtime_error("cannot write analysis artifact");
  output << std::fixed << std::setprecision(9)
         << "{\n  \"format\":\"drop7-constructive-spectrum-analysis-v1\","
         << "\n  \"publicOnly\":true,"
         << "\n  \"oracleInput\":\"4096 canonical public curriculum states\","
         << "\n  \"baselineSeeds\":{\"start\":\"0x3d690000\","
            "\"endExclusive\":\"0x3d690040\",\"games\":64},"
         << "\n  \"oracle\":";
  writeCorpus(output, oracle_stats);
  output << ",\n  \"fairD1\":";
  writeCorpus(output, baseline.states);
  output << ",\n  \"standardizedMeanDifferences\":{";
  for (int index = 0; index < kMetricCount; ++index) {
    if (index > 0) output << ',';
    const double pooled = std::sqrt(
        (oracle_stats.standardDeviation(index) *
             oracle_stats.standardDeviation(index) +
         baseline.states.standardDeviation(index) *
             baseline.states.standardDeviation(index)) /
        2.0);
    const double effect = pooled > 1.0e-12
                              ? (oracle_stats.mean(index) -
                                 baseline.states.mean(index)) /
                                    pooled
                              : 0.0;
    output << '\"' << kMetricNames[index] << "\":" << effect;
  }
  output << "},\n  \"fairD1Games\":{\"meanScore\":"
         << static_cast<double>(baseline.total_score) / kAnalysisGames
         << ",\"meanMoves\":"
         << static_cast<double>(baseline.total_moves) / kAnalysisGames
         << ",\"clearsPerMove\":"
         << static_cast<double>(baseline.total_clears) / baseline.total_moves
         << ",\"revealsPerMove\":"
         << static_cast<double>(baseline.total_reveals) / baseline.total_moves
         << "},\n  \"wallSeconds\":" << seconds
         << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
  if (!output) throw std::runtime_error("failed writing analysis artifact");
}

struct Options {
  std::string states = "/tmp/drop7-oracle-curriculum-states.jsonl";
  std::string output = "/tmp/drop7-constructive-spectrum-analysis.json";
  int threads = kDefaultThreads;
  std::uint32_t fit_seed_start = 0x3d69'0100u;
  int fit_games = 8;
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string argument = argv[index];
    if (argument == "--states") {
      result.states = argv[index + 1];
    } else if (argument == "--output") {
      result.output = argv[index + 1];
    } else if (argument == "--threads") {
      result.threads = std::stoi(argv[index + 1]);
      if (result.threads < 1 || result.threads > 16) {
        throw std::invalid_argument("threads must be in [1,16]");
      }
    } else if (argument == "--fit-seed-start") {
      result.fit_seed_start = static_cast<std::uint32_t>(
          std::stoul(argv[index + 1], nullptr, 0));
    } else if (argument == "--fit-games") {
      result.fit_games = std::stoi(argv[index + 1]);
      if (result.fit_games < 1 || result.fit_games > 32) {
        throw std::invalid_argument("fit games must be in [1,32]");
      }
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  return result;
}

int analyze(const Options& options, std::ostream& output) {
  const Deadline deadline;
  const auto oracle = loadCurriculum(options.states);
  const AnalysisTrace baseline = generateD1Analysis(deadline);
  writeAnalysis(options.output, oracle, baseline, deadline.seconds());
  output << std::fixed << std::setprecision(3)
         << "CONSTRUCTIVE_SPECTRUM_ANALYSIS {\"oracleStates\":"
         << oracle.size() << ",\"fairD1States\":" << baseline.states.count
         << ",\"fairD1Score\":"
         << static_cast<double>(baseline.total_score) / kAnalysisGames
         << ",\"fairD1Moves\":"
         << static_cast<double>(baseline.total_moves) / kAnalysisGames
         << ",\"wallSeconds\":" << deadline.seconds()
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return EXIT_SUCCESS;
}

// The target is intentionally asymmetric: falling below the oracle's load is
// safe, while exceeding it incurs rapidly increasing debt.  Conversely, high
// reservoirs and trigger coverage saturate at the oracle motif instead of
// rewarding an arbitrarily full board.  Values are in score-like units so the
// two rise bonuses covered by the rollout remain meaningful.
double structuralValue(const PublicState& state) {
  if (state.terminal) return kTerminalValue;
  const Metrics m = extractMetrics(state);
  const auto excess = [](double value, double target) {
    return std::max(0.0, value - target);
  };
  const auto capped = [](double value, double target) {
    return std::min(value, target);
  };
  double value = 0.0;
  value -= 2'400.0 * excess(m[kOccupancy], 15.0);
  value -= 3'000.0 * excess(m[kCovers], 8.0);
  value -= 13'000.0 * std::pow(excess(m[kMaximumHeight], 4.0), 2.0);
  value -= 2'000.0 * excess(m[kEdgeCovers], 3.0);
  value -= 6'500.0 * excess(m[kSurfaceLow], 1.0);
  value -= 1'300.0 * excess(m[kRoughness], 7.0);
  value += 3'600.0 * capped(m[kHighReservoir], 4.0);
  value += 2'000.0 * capped(m[kSurfaceHigh], 3.0);
  value += 1'200.0 * capped(m[kSameTargetHighPairs], 2.0);
  value += 550.0 * capped(m[kTriggerCover], 13.0);
  value += 900.0 * capped(m[kTriggerMultiple], 5.0);
  value += 500.0 * capped(m[kTriggerDiscBreadth], 7.0);
  value += 350.0 * capped(m[kTriggerColumnBreadth], 7.0);
  value += 700.0 * capped(m[kDistinctHeights], 4.0);
  value += 350.0 * capped(m[kUnitHeightSteps], 3.0);
  value += 1'500.0 * capped(m[kEdgeCoverFrontier], 2.0);
  // Rises are most dangerous when projected load is already above the motif.
  const double urgency =
      static_cast<double>(kMovesPerLevel - state.moves_remaining) /
      static_cast<double>(kMovesPerLevel - 1);
  value -= 1'500.0 * urgency * excess(m[kOccupancy] + 7.0, 19.0);
  return value;
}

struct SampledStep {
  State state{};
  std::int64_t score_delta = 0;
  int clears = 0;
  int reveals = 0;
  int waves = 0;
  bool played = false;
};

SampledStep sampledStep(const State& source, int source_column, int sample,
                        int depth_tag) {
  bool mirrored = false;
  const State canonical = detail::canonicalState(source, mirrored);
  const int column = mirrored ? kBoardSize - 1 - source_column : source_column;
  SampledStep result;
  if (!isLegal(canonical.board, column)) return result;
  const std::uint32_t seed =
      detail::scenarioSeedForState(canonical, kPolicySeed, depth_tag);
  detail::StratifiedRandom random{seed, sample, kChanceSamples, 0};
  MoveResult move;
  if (!detail::playMoveSampled(canonical, column, random, move)) return result;
  result.played = true;
  result.score_delta = move.score_delta;
  result.waves = static_cast<int>(move.waves.size());
  for (const Wave& wave : move.waves) {
    result.clears += wave.cleared;
    result.reveals += wave.revealed;
  }
  if (!move.state.game_over) {
    move.state.next_disc =
        detail::sampledNextDisc(seed, sample, kChanceSamples);
  }
  bool ignored = false;
  result.state = detail::canonicalState(move.state, ignored);
  return result;
}

struct OneStepDecision {
  int action = -1;
  double value = -std::numeric_limits<double>::infinity();
  std::uint64_t work = 0;
};

OneStepDecision constructiveContinuation(const State& source,
                                         int depth_tag) {
  OneStepDecision result;
  bool ignored = false;
  const State canonical = detail::canonicalState(source, ignored);
  for (const int column : kColumnOrder) {
    if (!isLegal(canonical.board, column)) continue;
    double total = 0.0;
    for (int sample = 0; sample < kChanceSamples; ++sample) {
      const SampledStep step = sampledStep(canonical, column, sample, depth_tag);
      ++result.work;
      if (!step.played || step.state.game_over) {
        total += kTerminalValue;
        continue;
      }
      const PublicState after = publicState(step.state);
      total += static_cast<double>(step.score_delta) +
               5'000.0 * step.clears + 8'000.0 * step.reveals +
               500.0 * step.waves + structuralValue(after);
    }
    total /= kChanceSamples;
    if (total > result.value) {
      result.value = total;
      result.action = column;
    }
  }
  if (result.action < 0) result.action = centerFirstMove(canonical.board);
  return result;
}

struct Decision {
  int action = -1;
  int tactical_action = -1;
  int shortlist = 0;
  int horizon = 0;
  std::uint64_t work = 0;
  std::array<double, kBoardSize> values{};

  bool operator==(const Decision&) const = default;
};

Decision chooseActionCanonical(const PublicState& source) {
  Decision result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  if (source.terminal) return result;
  const State root = materialize(source);
  fair::SearchContext tactical_context;
  const fair::RootEvaluation tactical =
      fair::rootDecision(root, kTacticalDepth, tactical_context);
  result.work += tactical_context.work;
  result.tactical_action = tactical.action;
  std::array<int, kBoardSize> tactical_rank{};
  tactical_rank.fill(kBoardSize);
  std::array<int, kBoardSize> ranked_columns{};
  int ranked_count = 0;
  for (const int column : kColumnOrder) {
    if (isLegal(root.board, column)) ranked_columns[ranked_count++] = column;
  }
  std::stable_sort(ranked_columns.begin(), ranked_columns.begin() + ranked_count,
                   [&](int left, int right) {
                     return tactical.values[left] > tactical.values[right];
                   });
  for (int rank = 0; rank < ranked_count; ++rank) {
    tactical_rank[ranked_columns[rank]] = rank;
  }
  // Finish the current rise cycle and then observe one full build cycle.  This
  // is the smallest horizon that can distinguish quiet reservoir construction
  // from a superficially attractive immediate pop in every rise phase.
  result.horizon = std::clamp(static_cast<int>(source.moves_remaining) +
                                  kMovesPerLevel,
                              kMinimumHorizon, kMaximumHorizon);
  for (const int root_column : kColumnOrder) {
    if (!isLegal(root.board, root_column)) continue;
    if (tactical_rank[root_column] >= kTacticalShortlist) continue;
    if (tactical.values[root_column] <
        tactical.value - kTacticalNearTie) continue;
    ++result.shortlist;
    double root_total = 0.0;
    for (int root_sample = 0; root_sample < kChanceSamples; ++root_sample) {
      const SampledStep first =
          sampledStep(root, root_column, root_sample, result.horizon);
      ++result.work;
      if (!first.played || first.state.game_over) {
        root_total += kTerminalValue;
        continue;
      }
      State state = first.state;
      double trajectory = static_cast<double>(first.score_delta) +
                          5'000.0 * first.clears +
                          8'000.0 * first.reveals + 500.0 * first.waves;
      bool terminal = false;
      for (int step_index = 1; step_index < result.horizon; ++step_index) {
        const int depth_tag = result.horizon - step_index;
        const OneStepDecision continuation =
            constructiveContinuation(state, depth_tag);
        result.work += continuation.work;
        if (continuation.action < 0) {
          terminal = true;
          break;
        }
        const int sample =
            (root_sample + 2 * step_index) % kChanceSamples;
        const SampledStep next =
            sampledStep(state, continuation.action, sample, depth_tag);
        ++result.work;
        if (!next.played || next.state.game_over) {
          terminal = true;
          break;
        }
        trajectory += static_cast<double>(next.score_delta) +
                      5'000.0 * next.clears + 8'000.0 * next.reveals +
                      500.0 * next.waves;
        state = next.state;
      }
      root_total += terminal
                        ? kTerminalValue
                        : trajectory + structuralValue(publicState(state));
    }
    result.values[root_column] = root_total / kChanceSamples;
    if (result.action < 0 ||
        result.values[root_column] > result.values[result.action]) {
      result.action = root_column;
    }
  }
  if (result.action < 0) result.action = centerFirstMove(root.board);
  return result;
}

Decision chooseAction(const PublicState& source) {
  if (source.terminal) return {};
  bool mirrored = false;
  const PublicState canonical = canonicalPublic(source, mirrored);
  Decision result = chooseActionCanonical(canonical);
  if (!mirrored) return result;
  result.action = kBoardSize - 1 - result.action;
  result.tactical_action = kBoardSize - 1 - result.tactical_action;
  std::array<double, kBoardSize> values{};
  for (int column = 0; column < kBoardSize; ++column) {
    values[column] = result.values[kBoardSize - 1 - column];
  }
  result.values = values;
  return result;
}

using PublicPolicy = Decision (*)(const PublicState&);
static_assert(std::is_same_v<decltype(&chooseAction), PublicPolicy>);
static_assert(!std::is_invocable_v<PublicPolicy, const State&>);

enum class Policy : std::uint8_t { kConstructive, kFairD1 };

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  int clears = 0;
  int reveals = 0;
  int waves = 0;
  int maximum_chain = 0;
  bool natural_terminal = false;
  bool capped = false;
  std::uint64_t work = 0;
  std::uint64_t disc_hash = 0xcbf2'9ce4'8422'2325ull;
};

void observeDisc(GameResult& result, std::uint8_t disc) {
  result.disc_hash ^= disc;
  result.disc_hash *= 0x0000'0100'0000'01b3ull;
}

GameResult playGame(std::uint32_t seed, Policy policy,
                    const Deadline& deadline, bool stage_a) {
  if (stage_a) {
    requireStageASeed(seed);
  } else if (seed < 0x3d69'0100u || seed >= 0x3d69'c000u) {
    throw std::invalid_argument("seed outside exact fitting bank");
  }
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    deadline.check();
    enforceRssLimit();
    if (state.next_disc != headlessDisc(seed, state.moves_played)) {
      throw std::runtime_error("headless disc stream guard failed");
    }
    observeDisc(result, state.next_disc);
    int action = -1;
    if (policy == Policy::kConstructive) {
      const Decision decision = chooseAction(publicState(state));
      action = decision.action;
      result.work += decision.work;
    } else {
      action = chooseFairD1(publicState(state));
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("gameplay policy selected illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("gameplay transition failed");
    }
    result.waves += static_cast<int>(move.waves.size());
    for (const Wave& wave : move.waves) {
      result.clears += wave.cleared;
      result.reveals += wave.revealed;
      result.maximum_chain = std::max(result.maximum_chain, wave.depth);
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.natural_terminal = state.game_over;
  result.capped = !state.game_over && state.moves_played == kMaximumMoves;
  return result;
}

struct Summary {
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double bottom_quartile_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double waves_per_move = 0.0;
  int natural_terminals = 0;
  int capped = 0;
  int maximum_chain = 0;
  std::uint64_t work = 0;
};

Summary summarize(const std::vector<GameResult>& games) {
  if (games.empty()) throw std::invalid_argument("cannot summarize no games");
  Summary result;
  std::vector<int> moves;
  std::int64_t score = 0;
  std::int64_t move_count = 0;
  std::int64_t clears = 0;
  std::int64_t reveals = 0;
  std::int64_t waves = 0;
  for (const GameResult& game : games) {
    score += game.score;
    move_count += game.moves;
    clears += game.clears;
    reveals += game.reveals;
    waves += game.waves;
    moves.push_back(game.moves);
    result.natural_terminals += game.natural_terminal;
    result.capped += game.capped;
    result.maximum_chain = std::max(result.maximum_chain, game.maximum_chain);
    result.work += game.work;
  }
  std::sort(moves.begin(), moves.end());
  const std::size_t quartile_count = std::max<std::size_t>(1, moves.size() / 4);
  result.bottom_quartile_moves =
      std::accumulate(moves.begin(), moves.begin() + quartile_count, 0.0) /
      quartile_count;
  result.mean_score = static_cast<double>(score) / games.size();
  result.mean_moves = static_cast<double>(move_count) / games.size();
  result.clears_per_move = static_cast<double>(clears) / move_count;
  result.reveals_per_move = static_cast<double>(reveals) / move_count;
  result.waves_per_move = static_cast<double>(waves) / move_count;
  return result;
}

struct Paired {
  int score_wins = 0;
  int move_wins = 0;
  int joint_wins = 0;
  double mean_score_delta = 0.0;
  double mean_move_delta = 0.0;
};

Paired pair(const std::vector<GameResult>& candidate,
            const std::vector<GameResult>& baseline) {
  if (candidate.size() != baseline.size()) {
    throw std::invalid_argument("paired cohorts differ in size");
  }
  Paired result;
  for (std::size_t index = 0; index < candidate.size(); ++index) {
    if (candidate[index].seed != baseline[index].seed) {
      throw std::runtime_error("paired seed mismatch");
    }
    const bool score_win = candidate[index].score > baseline[index].score;
    const bool move_win = candidate[index].moves > baseline[index].moves;
    result.score_wins += score_win;
    result.move_wins += move_win;
    result.joint_wins += score_win && move_win;
    result.mean_score_delta += candidate[index].score - baseline[index].score;
    result.mean_move_delta += candidate[index].moves - baseline[index].moves;
  }
  result.mean_score_delta /= candidate.size();
  result.mean_move_delta /= candidate.size();
  return result;
}

std::vector<GameResult> evaluate(std::uint32_t seed_start, int games,
                                 Policy policy, int threads,
                                 const Deadline& deadline, bool stage_a) {
  std::vector<GameResult> result(games);
  std::atomic<int> next{0};
  std::mutex progress;
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(threads, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int index = next.fetch_add(1);
        if (index >= games) return;
        const std::uint32_t seed = seed_start + index;
        result[index] = playGame(seed, policy, deadline, stage_a);
        const std::lock_guard<std::mutex> lock(progress);
        std::cerr << (policy == Policy::kConstructive ? "constructive" : "d1")
                  << " seed 0x" << std::hex << seed << std::dec << ' '
                  << result[index].score << " (" << result[index].moves
                  << " moves)\n";
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"bottomQuartileMoves\":" << summary.bottom_quartile_moves
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"wavesPerMove\":" << summary.waves_per_move
         << ",\"naturalTerminals\":" << summary.natural_terminals
         << ",\"capped\":" << summary.capped
         << ",\"maximumChain\":" << summary.maximum_chain
         << ",\"work\":" << summary.work << '}';
}

void writeGame(std::ostream& output, const GameResult& game) {
  output << "{\"seed\":\"0x" << std::hex << std::setw(8)
         << std::setfill('0') << game.seed << std::dec << std::setfill(' ')
         << "\",\"score\":" << game.score << ",\"moves\":" << game.moves
         << ",\"clears\":" << game.clears
         << ",\"reveals\":" << game.reveals << ",\"waves\":" << game.waves
         << ",\"maximumChain\":" << game.maximum_chain
         << ",\"naturalTerminal\":"
         << (game.natural_terminal ? "true" : "false")
         << ",\"capped\":" << (game.capped ? "true" : "false")
         << ",\"work\":" << game.work << ",\"discHash\":\"0x" << std::hex
         << game.disc_hash << std::dec << "\"}";
}

int runCohort(const Options& options, std::ostream& output, bool stage_a) {
  const Deadline deadline;
  const std::uint32_t seed_start =
      stage_a ? kStageASeedStart : options.fit_seed_start;
  const int games = stage_a ? kStageAGames : options.fit_games;
  if (!stage_a &&
      (seed_start < 0x3d69'0100u ||
       static_cast<std::uint64_t>(seed_start) + games > 0x3d69'c000ull)) {
    throw std::invalid_argument("fitting cohort outside 0x3d690100..0x3d69bfff");
  }
  const auto candidate = evaluate(seed_start, games, Policy::kConstructive,
                                  options.threads, deadline, stage_a);
  const auto baseline = evaluate(seed_start, games, Policy::kFairD1,
                                 options.threads, deadline, stage_a);
  const Summary candidate_summary = summarize(candidate);
  const Summary baseline_summary = summarize(baseline);
  const Paired paired = pair(candidate, baseline);
  // Fixed before Stage-A evaluation: a ten-percent survival gain, both
  // flow rates higher by at least 0.03 event/move, and a paired majority.
  const bool passed = candidate_summary.mean_score >=
                          1.10 * baseline_summary.mean_score &&
                      candidate_summary.mean_moves >=
                          1.10 * baseline_summary.mean_moves &&
                      candidate_summary.clears_per_move >=
                          baseline_summary.clears_per_move + 0.03 &&
                      candidate_summary.reveals_per_move >=
                          baseline_summary.reveals_per_move + 0.03 &&
                      paired.joint_wins >=
                          (stage_a ? 20 : (games * 5 + 7) / 8);
  std::ofstream artifact(options.output);
  if (!artifact) throw std::runtime_error("cannot write cohort artifact");
  artifact << std::fixed << std::setprecision(9)
           << "{\n  \"format\":\"drop7-constructive-spectrum-stage-a-v1\","
           << "\n  \"phase\":\"" << (stage_a ? "stage-a" : "fitting")
           << "\",\n  \"publicOnly\":true,\n  \"causal\":true,"
           << "\n  \"planner\":{\"kind\":\"cycle-constructive-rollout\","
              "\"chanceSamples\":"
           << kChanceSamples << ",\"horizon\":[" << kMinimumHorizon << ','
           << kMaximumHorizon << "],\"targetSource\":"
              "\"public-oracle-shape-contrast\"},"
           << "\n  \"seedBank\":{\"start\":\"0x" << std::hex
           << seed_start << "\",\"endExclusive\":\"0x"
           << seed_start + games << std::dec << "\",\"games\":" << games
           << "},\n  \"candidate\":";
  writeSummary(artifact, candidate_summary);
  artifact << ",\n  \"fairD1\":";
  writeSummary(artifact, baseline_summary);
  artifact << ",\n  \"paired\":{\"scoreWins\":" << paired.score_wins
           << ",\"moveWins\":" << paired.move_wins
           << ",\"jointWins\":" << paired.joint_wins
           << ",\"meanScoreDelta\":" << paired.mean_score_delta
           << ",\"meanMoveDelta\":" << paired.mean_move_delta << "},"
           << "\n  \"gate\":{\"scoreRatio\":1.10,\"moveRatio\":1.10,"
              "\"clearDelta\":0.03,\"revealDelta\":0.03,"
              "\"jointWins\":"
           << (stage_a ? 20 : (games * 5 + 7) / 8)
           << "},\n  \"passed\":"
           << (passed ? "true" : "false")
           << ",\n  \"wallSeconds\":" << deadline.seconds()
           << ",\n  \"peakRssBytes\":" << peakRssBytes()
           << ",\n  \"candidateGames\":[";
  for (std::size_t index = 0; index < candidate.size(); ++index) {
    if (index) artifact << ',';
    writeGame(artifact, candidate[index]);
  }
  artifact << "],\n  \"fairD1Games\":[";
  for (std::size_t index = 0; index < baseline.size(); ++index) {
    if (index) artifact << ',';
    writeGame(artifact, baseline[index]);
  }
  artifact << "]\n}\n";
  output << std::fixed << std::setprecision(3)
         << "CONSTRUCTIVE_SPECTRUM_" << (stage_a ? "STAGE_A" : "FIT")
         << " {\"candidateScore\":" << candidate_summary.mean_score
         << ",\"candidateMoves\":" << candidate_summary.mean_moves
         << ",\"candidateClears\":"
         << candidate_summary.clears_per_move
         << ",\"candidateReveals\":"
         << candidate_summary.reveals_per_move
         << ",\"fairD1Score\":" << baseline_summary.mean_score
         << ",\"fairD1Moves\":" << baseline_summary.mean_moves
         << ",\"fairD1Clears\":" << baseline_summary.clears_per_move
         << ",\"fairD1Reveals\":" << baseline_summary.reveals_per_move
         << ",\"jointWins\":" << paired.joint_wins
         << ",\"passed\":" << (passed ? "true" : "false")
         << ",\"wallSeconds\":" << deadline.seconds()
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return passed ? EXIT_SUCCESS : 2;
}

using HeightProfile = std::array<std::uint8_t, kBoardSize>;

HeightProfile heightProfile(const Board& board) {
  HeightProfile result{};
  const auto heights = columnHeights(board);
  for (int column = 0; column < kBoardSize; ++column) {
    result[column] = static_cast<std::uint8_t>(heights[column]);
  }
  const HeightProfile reflected{result[6], result[5], result[4], result[3],
                                result[2], result[1], result[0]};
  return reflected < result ? reflected : result;
}

std::vector<HeightProfile> buildHeightLibrary(
    const std::vector<PublicState>& oracle) {
  std::vector<HeightProfile> result;
  result.reserve(oracle.size());
  for (const PublicState& state : oracle) {
    result.push_back(heightProfile(state.board));
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

int nearestHeightSquared(const Board& board,
                         const std::vector<HeightProfile>& library) {
  const HeightProfile source = heightProfile(board);
  int best = std::numeric_limits<int>::max();
  for (const HeightProfile& target : library) {
    int distance = 0;
    for (int column = 0; column < kBoardSize; ++column) {
      const int difference = static_cast<int>(source[column]) - target[column];
      distance += difference * difference;
    }
    best = std::min(best, distance);
  }
  return best;
}

struct MotifDecision {
  int action = -1;
  int tactical_action = -1;
  int shortlist = 0;
  double tactical_regret = 0.0;
};

MotifDecision chooseNearestHeight(const PublicState& source,
                                  const std::vector<HeightProfile>& library) {
  bool mirrored = false;
  const PublicState public_canonical = canonicalPublic(source, mirrored);
  const State root = materialize(public_canonical);
  fair::SearchContext context;
  const fair::RootEvaluation tactical =
      fair::rootDecision(root, kTacticalDepth, context);
  std::array<int, kBoardSize> columns{};
  int count = 0;
  for (const int column : kColumnOrder) {
    if (isLegal(root.board, column)) columns[count++] = column;
  }
  std::stable_sort(columns.begin(), columns.begin() + count,
                   [&](int left, int right) {
                     return tactical.values[left] > tactical.values[right];
                   });
  MotifDecision result;
  result.tactical_action = tactical.action;
  double best_distance = std::numeric_limits<double>::infinity();
  for (int rank = 0; rank < std::min(count, kTacticalShortlist); ++rank) {
    const int column = columns[rank];
    if (tactical.values[column] < tactical.value - kTacticalNearTie) continue;
    ++result.shortlist;
    double distance = 0.0;
    for (int sample = 0; sample < kChanceSamples; ++sample) {
      const SampledStep step = sampledStep(root, column, sample, 1);
      distance += step.played && !step.state.game_over
                      ? nearestHeightSquared(step.state.board, library)
                      : 1'000'000.0;
    }
    distance /= kChanceSamples;
    if (distance < best_distance) {
      best_distance = distance;
      result.action = column;
    }
  }
  if (result.action < 0) result.action = tactical.action;
  result.tactical_regret = tactical.value - tactical.values[result.action];
  if (mirrored) {
    result.action = kBoardSize - 1 - result.action;
    result.tactical_action = kBoardSize - 1 - result.tactical_action;
  }
  return result;
}

int motifAudit(const Options& options, std::ostream& output) {
  const Deadline deadline;
  const auto oracle = loadCurriculum(options.states);
  const auto library = buildHeightLibrary(oracle);
  constexpr std::uint32_t seed_start = 0x3d69'0200u;
  constexpr int games = 16;
  std::uint64_t roots = 0;
  std::uint64_t nontrivial = 0;
  std::uint64_t rollout_tactical_agreement = 0;
  std::uint64_t nearest_tactical_agreement = 0;
  std::uint64_t rollout_nearest_agreement = 0;
  double nearest_regret = 0.0;
  double rollout_regret = 0.0;
  for (std::uint32_t seed = seed_start; seed < seed_start + games; ++seed) {
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < kMaximumMoves) {
      deadline.check();
      const PublicState current = publicState(state);
      const Decision rollout = chooseAction(current);
      const MotifDecision nearest = chooseNearestHeight(current, library);
      bool ignored = false;
      const PublicState canonical = canonicalPublic(current, ignored);
      fair::SearchContext context;
      const fair::RootEvaluation tactical = fair::rootDecision(
          materialize(canonical), kTacticalDepth, context);
      const int rollout_canonical =
          ignored ? kBoardSize - 1 - rollout.action : rollout.action;
      ++roots;
      nontrivial += rollout.shortlist > 1;
      rollout_tactical_agreement += rollout.action == rollout.tactical_action;
      nearest_tactical_agreement += nearest.action == nearest.tactical_action;
      rollout_nearest_agreement += rollout.action == nearest.action;
      rollout_regret +=
          tactical.value - tactical.values[rollout_canonical];
      nearest_regret += nearest.tactical_regret;
      const int action = chooseFairD1(current);
      MoveResult move;
      if (!playHeadlessMove(state, seed, action, move)) {
        throw std::runtime_error("motif-audit transition failed");
      }
    }
  }
  std::ofstream artifact(options.output);
  if (!artifact) throw std::runtime_error("cannot write motif audit artifact");
  artifact << std::fixed << std::setprecision(9)
           << "{\n  \"format\":\"drop7-constructive-spectrum-motif-audit-v1\","
           << "\n  \"publicOracleStates\":" << oracle.size()
           << ",\n  \"uniqueCanonicalHeightProfiles\":" << library.size()
           << ",\n  \"fittingRoots\":" << roots
           << ",\n  \"nontrivialNearTieRoots\":" << nontrivial
           << ",\n  \"rolloutTacticalAgreement\":"
           << static_cast<double>(rollout_tactical_agreement) / roots
           << ",\n  \"nearestHeightTacticalAgreement\":"
           << static_cast<double>(nearest_tactical_agreement) / roots
           << ",\n  \"rolloutNearestAgreement\":"
           << static_cast<double>(rollout_nearest_agreement) / roots
           << ",\n  \"rolloutMeanTacticalRegret\":" << rollout_regret / roots
           << ",\n  \"nearestHeightMeanTacticalRegret\":"
           << nearest_regret / roots
           << ",\n  \"conclusion\":\"diagnostic-only; nearest height is not "
              "used by the frozen controller\","
           << "\n  \"wallSeconds\":" << deadline.seconds()
           << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
  output << std::fixed << std::setprecision(6)
         << "CONSTRUCTIVE_SPECTRUM_MOTIF_AUDIT {\"profiles\":"
         << library.size() << ",\"roots\":" << roots
         << ",\"nontrivial\":" << nontrivial
         << ",\"rolloutTacticalAgreement\":"
         << static_cast<double>(rollout_tactical_agreement) / roots
         << ",\"nearestTacticalAgreement\":"
         << static_cast<double>(nearest_tactical_agreement) / roots
         << ",\"rolloutRegret\":" << rollout_regret / roots
         << ",\"nearestRegret\":" << nearest_regret / roots
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return EXIT_SUCCESS;
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

template <typename Function>
bool throwsInvalid(Function&& function) {
  try {
    function();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

bool selfTest(std::ostream& output) {
  expect(kLevelBonus == 17'000, "Hardcore level bonus regression");
  PublicState fixture;
  fixture.board.fill(kEmpty);
  fixture.board[indexOf(6, 0)] = kSolid;
  fixture.board[indexOf(5, 0)] = 6;
  fixture.board[indexOf(6, 1)] = kCracked;
  fixture.board[indexOf(6, 2)] = 5;
  fixture.board[indexOf(5, 2)] = 4;
  fixture.board[indexOf(6, 3)] = kSolid;
  fixture.board[indexOf(6, 4)] = 7;
  fixture.next_disc = 3;
  fixture.moves_remaining = 4;
  const Metrics metrics = extractMetrics(fixture);
  const Metrics reflected = extractMetrics(mirror(fixture));
  expect(metrics == reflected && metrics[kHighReservoir] == 2 &&
             metrics[kCovers] == 3,
         "shape metrics reflection/fixture failed");
  const TriggerKeys keys = exactTriggerKeys(fixture.board);
  const TriggerKeys reflected_keys = exactTriggerKeys(mirror(fixture).board);
  expect(keys.legal == reflected_keys.legal && keys.any == reflected_keys.any &&
             keys.high == reflected_keys.high &&
             keys.cover_contact == reflected_keys.cover_contact,
         "exact trigger-key reflection failed");
  State metadata = materialize(fixture);
  metadata.score = 9'999'999;
  metadata.level = 777;
  metadata.moves_played = 888;
  expect(publicState(metadata) == fixture &&
             extractMetrics(publicState(metadata)) == metrics,
         "shape extraction used private metadata");
  const int d1 = chooseFairD1(fixture);
  const int d1_reflected = chooseFairD1(mirror(fixture));
  expect(isLegal(fixture.board, d1) &&
             d1_reflected == kBoardSize - 1 - d1,
         "fair D1 reflection/legality failed");
  const Decision first = chooseAction(fixture);
  const Decision repeated = chooseAction(fixture);
  const Decision policy_reflected = chooseAction(mirror(fixture));
  expect(first == repeated && isLegal(fixture.board, first.action) &&
             first.horizon >= kMinimumHorizon &&
             first.horizon <= kMaximumHorizon,
         "constructive controller determinism/legality failed");
  expect(policy_reflected.action == kBoardSize - 1 - first.action &&
             policy_reflected.work == first.work,
         "constructive controller reflection failed");
  for (int column = 0; column < kBoardSize; ++column) {
    expect(first.values[column] ==
               policy_reflected.values[kBoardSize - 1 - column],
           "constructive values failed reflection");
  }
  expect(chooseAction(publicState(metadata)) == first,
         "constructive controller used hidden metadata");
  PublicState terminal = fixture;
  terminal.terminal = true;
  expect(chooseAction(terminal).action == -1,
         "constructive controller selected in terminal state");
  expect(allowedAnalysisSeed(kAnalysisSeedStart) &&
             allowedAnalysisSeed(kAnalysisSeedEndExclusive - 1u) &&
             !allowedAnalysisSeed(kAnalysisSeedStart - 1u) &&
             !allowedAnalysisSeed(kAnalysisSeedEndExclusive) &&
             allowedStageASeed(kStageASeedStart) &&
             allowedStageASeed(kStageASeedEndExclusive - 1u) &&
             !allowedStageASeed(kStageASeedStart - 1u) &&
             !allowedStageASeed(kStageASeedEndExclusive) &&
             throwsInvalid([] { requireAnalysisSeed(0x4d69'0000u); }) &&
             throwsInvalid([] { requireAnalysisSeed(0x7d69'0000u); }) &&
             throwsInvalid([] { requireAnalysisSeed(0xd769'0000u); }) &&
             throwsInvalid([] { requireAnalysisSeed(0x3d3a'0000u); }) &&
             throwsInvalid([] { requireAnalysisSeed(0x3d68'0000u); }) &&
             throwsInvalid([] { requireStageASeed(0x4d69'c000u); }) &&
             throwsInvalid([] { requireStageASeed(0x7d69'c000u); }) &&
             throwsInvalid([] { requireStageASeed(0xd769'c000u); }),
         "seed guards failed");
  enforceRssLimit();
  output << "CONSTRUCTIVE_SPECTRUM_SELF_TEST {\"passed\":true,"
         << "\"publicOnly\":true,\"causal\":true,"
         << "\"metadataBlind\":true,\"reflection\":true,"
         << "\"deterministic\":true,\"triggerKeys\":49,"
         << "\"cycleAware\":true,\"maximumHorizon\":"
         << kMaximumHorizon << ",\"workFixture\":" << first.work << ','
         << "\"seedGuards\":true,\"peakRssBytes\":" << peakRssBytes()
         << "}\n";
  return true;
}

}  // namespace drop7::constructive_spectrum

int main(int argc, char** argv) {
  try {
    using namespace drop7::constructive_spectrum;
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return selfTest(std::cout) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--analyze") {
      const Options options = parseOptions(argc, argv, 2);
      return analyze(options, std::cout);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--fit") {
      Options options = parseOptions(argc, argv, 2);
      if (options.output ==
          "/tmp/drop7-constructive-spectrum-analysis.json") {
        options.output = "/tmp/drop7-constructive-spectrum-fit.json";
      }
      return runCohort(options, std::cout, false);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--stage-a") {
      Options options = parseOptions(argc, argv, 2);
      if (options.output ==
          "/tmp/drop7-constructive-spectrum-analysis.json") {
        options.output = "/tmp/drop7-constructive-spectrum-stage-a.json";
      }
      return runCohort(options, std::cout, true);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--motif-audit") {
      Options options = parseOptions(argc, argv, 2);
      if (options.output ==
          "/tmp/drop7-constructive-spectrum-analysis.json") {
        options.output = "/tmp/drop7-constructive-spectrum-motif-audit.json";
      }
      return motifAudit(options, std::cout);
    }
    std::cerr << "usage: drop7_constructive_spectrum --self-test | "
                 "--analyze [--states PATH] [--output PATH] | "
                 "--fit [--output PATH] [--threads N] | "
                 "--stage-a [--output PATH] [--threads N] | "
                 "--motif-audit [--states PATH] [--output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_constructive_spectrum: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
