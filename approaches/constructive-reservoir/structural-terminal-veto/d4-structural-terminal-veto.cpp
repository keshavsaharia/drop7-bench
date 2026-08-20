#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#define DROP7_FAIR_ONLY_DEPTH4_NO_MAIN
#include "../../fair-expectimax/reference/fair-only-depth4.cpp"
#undef DROP7_FAIR_ONLY_DEPTH4_NO_MAIN
#undef DROP7_FAIR_ONLY_DEPTH4_LIBRARY

#include <bit>
#include <filesystem>
#include <optional>
#include <sstream>

// A conservative terminal-rollout veto around the immutable fair-D4/s5
// policy.  D4 supplies the root action and complete ordering; only its top two
// legal actions are compared.  The forced root action is followed by the exact
// fixed constructiveContinuation (not its D3/D4-shielded outer policy) on two
// independent panels of 127 event-stratified public chance streams.  The D4
// runner-up may replace D4 only when the fixed terminal-classifier ultra gate
// passes independently on both panels.
namespace drop7::d4_structural_terminal_veto {

namespace d4 = drop7::fair_only_depth4;
namespace detail = drop7::cfpi::detail;
using Clock = std::chrono::steady_clock;

// Exact copy of the fixed cheap continuation and its transitive structural
// evaluator from constructive-spectrum-depth4.cpp.  Keeping it local avoids
// importing the D4-shielded outer policy.
namespace frozen {

constexpr int kChanceSamples = 7;
constexpr std::uint32_t kPolicySeed = 0x4353'5031u;  // "CSP1"
constexpr double kTerminalValue = -1.0e9;
constexpr std::array<int, kBoardSize> kColumnOrder{{3, 2, 4, 1, 5, 0, 6}};

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
    throw std::invalid_argument("invalid public D4 constructive state");
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
  result.distinct_discs = std::accumulate(discs.begin(), discs.end(), 0);
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
      const int deficit =
          static_cast<int>(cell) - std::max(horizontal, vertical);
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

OneStepDecision constructiveContinuation(const State& source, int depth_tag) {
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
      total += static_cast<double>(step.score_delta) +
               5'000.0 * step.clears + 8'000.0 * step.reveals +
               500.0 * step.waves + structuralValue(publicState(step.state));
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

}  // namespace frozen

using PublicState = frozen::PublicState;

constexpr std::uint32_t kFittingSeedStart = 0x3d6e'4000u;
constexpr int kFittingGames = 4;
constexpr std::uint32_t kScreenSeedStart = 0x3d6e'5000u;
constexpr int kScreenGames = 8;
constexpr int kMaximumMoves = 1'000;
constexpr int kScenarios = 127;
constexpr int kHorizon = 200;
constexpr int kEventsPerStep = 64;
constexpr int kDefaultThreads = 4;
constexpr double kT99Df126 = 2.35631;
constexpr double kNormal99 = 2.326347874;
constexpr double kMinimumScoreLcb = 10'000.0;
constexpr double kMinimumMoveLcb = 2.0;
constexpr double kMaterialScoreLoss = -100'000.0;
constexpr double kMaterialMoveLoss = -25.0;
constexpr double kMaximumDownsideUpper99 = 0.10;
constexpr double kFittingScoreRatio = 1.10;
constexpr double kFittingMoveRatio = 1.10;
constexpr int kFittingJointWins = 3;
constexpr double kScreenScoreRatio = 1.05;
constexpr double kScreenMoveRatio = 1.05;
constexpr int kScreenJointWins = 5;
constexpr double kWallLimitSeconds = 75.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr int kProjectionMovesPerGame = 175;
constexpr double kProjectionSafetyFactor = 1.35;
constexpr double kProjectionReserveSeconds = 45.0;
constexpr std::uint32_t kPanelADomain = 0x4434'5041u;  // "D4PA"
constexpr std::uint32_t kPanelBDomain = 0x4434'5042u;  // "D4PB"
constexpr std::uint32_t kRevealDomain = 0x4434'5256u;  // "D4RV"
constexpr std::uint32_t kVisibleDomain = 0x4434'5653u; // "D4VS"

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
constexpr bool kAddressSanitizerBuild = true;
#else
constexpr bool kAddressSanitizerBuild = false;
#endif
#else
constexpr bool kAddressSanitizerBuild = false;
#endif

constexpr std::uint64_t kMaximumD4WorkPerDecision = d4::kMaximumWork;
constexpr std::uint64_t kMaximumSyntheticTransitionsPerDecision =
    2ull * 2ull * kScenarios * kHorizon;
constexpr std::uint64_t kMaximumContinuationCallsPerDecision =
    2ull * 2ull * kScenarios * (kHorizon - 1);
constexpr std::uint64_t kMaximumConstructiveWorkPerCall =
    static_cast<std::uint64_t>(kBoardSize) * frozen::kChanceSamples;
constexpr std::uint64_t kMaximumConstructiveWorkPerDecision =
    kMaximumContinuationCallsPerDecision * kMaximumConstructiveWorkPerCall;

static_assert(kLevelBonus == 17'000);
static_assert(kMovesPerLevel == 5);
static_assert(d4::kCandidateDepth == 4 && d4::kChanceSamples == 5);
static_assert(d4::kMaximumWork > d4::kWorstCaseD4Work);
static_assert(frozen::kChanceSamples == 7);
static_assert(frozen::kPolicySeed == 0x4353'5031u);
static_assert(frozen::kTerminalValue == -1.0e9);
static_assert(kScenarios == 127 && kHorizon == 200);
static_assert(kEventsPerStep > kCellCount);
static_assert(kMaximumSyntheticTransitionsPerDecision == 101'600);
static_assert(kMaximumContinuationCallsPerDecision == 101'092);
static_assert(kMaximumConstructiveWorkPerCall == 49);
static_assert(kMaximumConstructiveWorkPerDecision == 4'953'508);
static_assert(kFittingSeedStart + kFittingGames <= kScreenSeedStart);
static_assert(kScreenSeedStart + kScreenGames <= 0x3d6e'ffffu);
static_assert((kFittingSeedStart >> 16u) == 0x3d6eu);
static_assert((kScreenSeedStart >> 16u) == 0x3d6eu);

std::mutex report_mutex;

struct Options {
  std::string output = "/tmp/drop7-d4-structural-terminal-veto.json";
  std::string switches =
      "/tmp/drop7-d4-structural-terminal-veto-switches.jsonl";
  std::string readme =
      "/tmp/drop7-d4-structural-terminal-veto-README.md";
  std::string source_sha256;
  int threads = kDefaultThreads;
};

bool isSha256(std::string_view value) {
  if (value.size() != 64) return false;
  return std::all_of(value.begin(), value.end(), [](char character) {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f');
  });
}

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string argument = argv[index];
    if (argument == "--output") {
      result.output = argv[index + 1];
    } else if (argument == "--switches") {
      result.switches = argv[index + 1];
    } else if (argument == "--readme") {
      result.readme = argv[index + 1];
    } else if (argument == "--source-sha256") {
      result.source_sha256 = argv[index + 1];
    } else if (argument == "--threads") {
      result.threads = std::stoi(argv[index + 1]);
      if (result.threads < 1 || result.threads > 16) {
        throw std::invalid_argument("threads must be in [1,16]");
      }
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  if (!isSha256(result.source_sha256)) {
    throw std::invalid_argument("--source-sha256 must be 64 lowercase hex");
  }
  return result;
}

std::uint64_t mix64(std::uint64_t value) {
  value ^= value >> 30u;
  value *= 0xbf58'476d'1ce4'e5b9ull;
  value ^= value >> 27u;
  value *= 0x94d0'49bb'1331'11ebull;
  return value ^ (value >> 31u);
}

void hashCombine(std::uint64_t& hash, std::uint64_t value) {
  hash = mix64(hash ^ mix64(value + 0x9e37'79b9'7f4a'7c15ull));
}

std::uint32_t seed32(std::uint64_t value) {
  return mix32(static_cast<std::uint32_t>(value) ^
               static_cast<std::uint32_t>(value >> 32u));
}

std::uint64_t publicHash(const PublicState& source) {
  bool ignored = false;
  const PublicState state = frozen::canonicalPublic(source, ignored);
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint64_t>(cell + 1u);
    hash *= 0x0000'0100'0000'01b3ull;
  }
  hash ^= state.next_disc;
  hash *= 0x0000'0100'0000'01b3ull;
  hash ^= static_cast<std::uint64_t>(state.moves_remaining + 1u);
  hash *= 0x0000'0100'0000'01b3ull;
  hash ^= static_cast<std::uint64_t>(state.terminal);
  return mix64(hash);
}

std::uint32_t panelSeed(const PublicState& source, std::uint32_t domain) {
  return seed32(publicHash(source) ^ static_cast<std::uint64_t>(domain));
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
  // ASan's shadow memory is outside the runtime RSS contract.  The
  // optimized binary enforces 256 MiB after every actual and synthetic root.
  if (!kAddressSanitizerBuild && peakRssBytes() > kRssLimitBytes) {
    throw std::runtime_error("D4 structural veto exceeded 256 MiB RSS");
  }
}

struct Deadline {
  Clock::time_point started = Clock::now();

  double seconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }

  void check() const {
    if (seconds() > kWallLimitSeconds) {
      throw std::runtime_error("D4 structural veto exceeded 75m wall");
    }
  }
};

struct D4Anchor {
  int action = -1;
  int alternative = -1;
  int legal_actions = 0;
  bool complete = false;
  int completed_depth = 0;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::size_t cache_entries = 0;
  std::array<double, kBoardSize> values{};

  bool operator==(const D4Anchor&) const = default;
};

D4Anchor chooseD4Canonical(const PublicState& canonical) {
  if (canonical.terminal) return {};
  const d4::SearchDecision decision =
      d4::chooseDepth4Action(frozen::materialize(canonical));
  D4Anchor result;
  result.action = decision.action;
  result.complete = decision.complete;
  result.completed_depth = decision.completed_depth;
  result.work = decision.work;
  result.nodes = decision.nodes;
  result.cache_hits = decision.cache_hits;
  result.cache_entries = decision.cache_entries;
  result.values = decision.root_values;
  std::array<int, kBoardSize> ranked{};
  for (const int column : frozen::kColumnOrder) {
    if (isLegal(canonical.board, column)) ranked[result.legal_actions++] = column;
  }
  std::stable_sort(ranked.begin(), ranked.begin() + result.legal_actions,
                   [&](int left, int right) {
                     return result.values[left] > result.values[right];
                   });
  if (!result.complete || result.completed_depth != d4::kCandidateDepth ||
      result.action < 0 || result.legal_actions < 1 ||
      ranked[0] != result.action || result.work > d4::kMaximumWork ||
      result.cache_entries > d4::kMaximumCacheEntries) {
    throw std::runtime_error("immutable fair-D4/s5 anchor did not complete");
  }
  if (result.legal_actions >= 2) result.alternative = ranked[1];
  return result;
}

D4Anchor chooseD4(const PublicState& source) {
  bool mirrored = false;
  const PublicState canonical = frozen::canonicalPublic(source, mirrored);
  D4Anchor result = chooseD4Canonical(canonical);
  if (!mirrored) return result;
  result.action = kBoardSize - 1 - result.action;
  if (result.alternative >= 0) {
    result.alternative = kBoardSize - 1 - result.alternative;
  }
  std::array<double, kBoardSize> values{};
  for (int column = 0; column < kBoardSize; ++column) {
    values[column] = result.values[kBoardSize - 1 - column];
  }
  result.values = values;
  return result;
}

struct PublicContinuationDecision {
  int action = -1;
  double value = -std::numeric_limits<double>::infinity();
  std::uint64_t work = 0;

  bool operator==(const PublicContinuationDecision&) const = default;
};

// This wrapper is the runtime continuation boundary.  It can carry only public
// board, visible-disc, and phase data, and invokes the fixed implementation
// through its reference entry point.
PublicContinuationDecision chooseConstructiveContinuation(
    const PublicState& source, int depth_tag) {
  if (source.terminal) return {};
  bool mirrored = false;
  const PublicState canonical = frozen::canonicalPublic(source, mirrored);
  const frozen::OneStepDecision decision = frozen::constructiveContinuation(
      frozen::materialize(canonical), depth_tag);
  PublicContinuationDecision result;
  result.action = mirrored && decision.action >= 0
                      ? kBoardSize - 1 - decision.action
                      : decision.action;
  result.value = decision.value;
  result.work = decision.work;
  if (!isLegal(source.board, result.action) ||
      result.work > kMaximumConstructiveWorkPerCall) {
    throw std::runtime_error("frozen constructive continuation was invalid");
  }
  return result;
}

using PublicContinuation = PublicContinuationDecision (*)(const PublicState&,
                                                           int);
static_assert(std::is_same_v<decltype(&chooseConstructiveContinuation),
                             PublicContinuation>);
static_assert(!std::is_invocable_v<PublicContinuation, const State&, int>);

std::uint8_t visibleDisc(std::uint32_t root_seed, int scenario, int step,
                         std::uint32_t domain = kVisibleDomain) {
  const double unit = detail::stratifiedUnit(root_seed, scenario, kScenarios,
                                             domain, step);
  return static_cast<std::uint8_t>(
      std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
}

bool playSyntheticMove(const PublicState& source, int action,
                       std::uint32_t root_seed, int scenario, int step,
                       MoveResult& result,
                       std::uint32_t reveal_domain = kRevealDomain,
                       std::uint32_t visible_domain = kVisibleDomain) {
  if (source.terminal || scenario < 0 || scenario >= kScenarios || step < 0 ||
      step >= kHorizon || !isLegal(source.board, action)) {
    return false;
  }
  Board board = source.board;
  if (!placeDisc(board, action, source.next_disc)) return false;
  struct RevealTape {
    std::uint32_t root_seed;
    int scenario;
    int step;
    std::uint32_t domain;
    int event = 0;

    std::uint8_t nextDisc() {
      if (event >= kEventsPerStep) {
        throw std::runtime_error("structural reveal event slice exhausted");
      }
      const int event_index = step * kEventsPerStep + event++;
      const double unit = detail::stratifiedUnit(
          root_seed, scenario, kScenarios, domain, event_index);
      return static_cast<std::uint8_t>(
          std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
    }
  } reveals{root_seed, scenario, step, reveal_domain};

  result = MoveResult{};
  std::int64_t score = 0;
  detail::resolveCascadeSampled(board, reveals, 1, score, result.waves);
  result.score_delta = score;
  result.cleared_board = isBoardEmpty(board);
  if (result.cleared_board) result.score_delta += kClearBonus;
  int moves_remaining = source.moves_remaining - 1;
  bool terminal = false;
  if (moves_remaining == 0) {
    Board raised{};
    if (!raiseCoveredRow(board, raised)) {
      terminal = true;
    } else {
      result.level_advanced = true;
      moves_remaining = kMovesPerLevel;
      result.score_delta += kLevelBonus;
      board = raised;
      std::int64_t rise_score = 0;
      const int next_depth =
          result.waves.empty() ? 1 : result.waves.back().depth + 1;
      detail::resolveCascadeSampled(board, reveals, next_depth, rise_score,
                                    result.waves);
      result.score_delta += rise_score;
      if (isBoardEmpty(board)) {
        result.score_delta += kClearBonus;
        result.cleared_board = true;
      }
    }
  }
  int legal_count = 0;
  legalColumns(board, legal_count);
  if (!terminal && legal_count == 0) terminal = true;
  result.state.board = board;
  result.state.next_disc =
      terminal ? source.next_disc
               : visibleDisc(root_seed, scenario, step, visible_domain);
  result.state.score = 0;
  result.state.level = 1;
  result.state.moves_remaining = moves_remaining;
  result.state.moves_played = 0;
  result.state.game_over = terminal;
  return true;
}

struct WorkMetrics {
  std::uint64_t d4_work = 0;
  std::uint64_t d4_nodes = 0;
  std::uint64_t d4_cache_hits = 0;
  std::size_t peak_d4_cache_entries = 0;
  std::uint64_t synthetic_transitions = 0;
  std::uint64_t continuation_calls = 0;
  std::uint64_t constructive_work = 0;

  bool operator==(const WorkMetrics&) const = default;

  WorkMetrics& operator+=(const WorkMetrics& other) {
    d4_work += other.d4_work;
    d4_nodes += other.d4_nodes;
    d4_cache_hits += other.d4_cache_hits;
    peak_d4_cache_entries =
        std::max(peak_d4_cache_entries, other.peak_d4_cache_entries);
    synthetic_transitions += other.synthetic_transitions;
    continuation_calls += other.continuation_calls;
    constructive_work += other.constructive_work;
    return *this;
  }
};

void observeD4(const D4Anchor& anchor, WorkMetrics& work) {
  work.d4_work += anchor.work;
  work.d4_nodes += anchor.nodes;
  work.d4_cache_hits += anchor.cache_hits;
  work.peak_d4_cache_entries =
      std::max(work.peak_d4_cache_entries, anchor.cache_entries);
}

struct ScenarioOutcome {
  double score_return = 0.0;
  double survived_moves = 0.0;
  int numbered_clears = 0;
  int covers_revealed = 0;
  bool survived_cutoff = false;

  bool operator==(const ScenarioOutcome&) const = default;
};

ScenarioOutcome rolloutScenario(const PublicState& root, int root_action,
                                std::uint32_t root_seed, int scenario,
                                WorkMetrics& work, const Deadline* deadline,
                                int horizon = kHorizon) {
  if (root.terminal || !isLegal(root.board, root_action) || horizon < 1 ||
      horizon > kHorizon) {
    throw std::invalid_argument("invalid structural terminal scenario");
  }
  PublicState state = root;
  ScenarioOutcome result;
  for (int step = 0; step < horizon; ++step) {
    if (deadline != nullptr) deadline->check();
    int action = root_action;
    if (step > 0) {
      const PublicContinuationDecision continuation =
          chooseConstructiveContinuation(state, horizon - step);
      ++work.continuation_calls;
      work.constructive_work += continuation.work;
      action = continuation.action;
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("structural continuation selected illegal move");
    }
    MoveResult move;
    if (!playSyntheticMove(state, action, root_seed, scenario, step, move)) {
      throw std::runtime_error("structural synthetic transition failed");
    }
    ++work.synthetic_transitions;
    result.score_return += static_cast<double>(move.score_delta);
    result.survived_moves += 1.0;
    for (const Wave& wave : move.waves) {
      result.numbered_clears += wave.cleared;
      result.covers_revealed += wave.revealed;
    }
    state = frozen::publicState(move.state);
    if (state.terminal) return result;
  }
  result.survived_cutoff = true;
  return result;
}

struct PairedMetric {
  double mean = 0.0;
  double standard_error = 0.0;
  double lower_one_sided_99 = 0.0;
  double minimum = 0.0;
  double maximum = 0.0;
  int wins = 0;
  int ties = 0;
  int losses = 0;

  bool operator==(const PairedMetric&) const = default;
};

PairedMetric pairedMetric(const std::array<double, kScenarios>& differences) {
  PairedMetric result;
  result.minimum = std::numeric_limits<double>::infinity();
  result.maximum = -std::numeric_limits<double>::infinity();
  for (const double difference : differences) {
    result.mean += difference / kScenarios;
    result.minimum = std::min(result.minimum, difference);
    result.maximum = std::max(result.maximum, difference);
    result.wins += difference > 0.0;
    result.ties += difference == 0.0;
    result.losses += difference < 0.0;
  }
  double squares = 0.0;
  for (const double difference : differences) {
    const double centered = difference - result.mean;
    squares += centered * centered;
  }
  const double deviation =
      std::sqrt(squares / static_cast<double>(kScenarios - 1));
  result.standard_error = deviation / std::sqrt(kScenarios);
  result.lower_one_sided_99 =
      result.mean - kT99Df126 * result.standard_error;
  return result;
}

double wilsonUpper99(int events, int trials) {
  if (events < 0 || trials < 1 || events > trials) {
    throw std::invalid_argument("invalid Wilson inputs");
  }
  const double n = static_cast<double>(trials);
  const double p = static_cast<double>(events) / n;
  const double z2 = kNormal99 * kNormal99;
  const double center = p + z2 / (2.0 * n);
  const double radius = kNormal99 *
      std::sqrt((p * (1.0 - p) + z2 / (4.0 * n)) / n);
  return (center + radius) / (1.0 + z2 / n);
}

struct PairedAudit {
  PairedMetric score{};
  PairedMetric moves{};
  int material_downsides = 0;
  double material_downside_upper99 = 1.0;

  bool operator==(const PairedAudit&) const = default;
};

struct ActionPanel {
  std::array<ScenarioOutcome, kScenarios> scenarios{};
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double mean_clears = 0.0;
  double mean_reveals = 0.0;
  int survived_cutoffs = 0;

  bool operator==(const ActionPanel&) const = default;
};

PairedAudit pairedAudit(const ActionPanel& candidate,
                        const ActionPanel& baseline) {
  std::array<double, kScenarios> score_differences{};
  std::array<double, kScenarios> move_differences{};
  PairedAudit result;
  for (int scenario = 0; scenario < kScenarios; ++scenario) {
    score_differences[scenario] =
        candidate.scenarios[scenario].score_return -
        baseline.scenarios[scenario].score_return;
    move_differences[scenario] =
        candidate.scenarios[scenario].survived_moves -
        baseline.scenarios[scenario].survived_moves;
    result.material_downsides +=
        score_differences[scenario] < kMaterialScoreLoss ||
        move_differences[scenario] < kMaterialMoveLoss;
  }
  result.score = pairedMetric(score_differences);
  result.moves = pairedMetric(move_differences);
  result.material_downside_upper99 =
      wilsonUpper99(result.material_downsides, kScenarios);
  return result;
}

bool passesUltra(const PairedAudit& audit) {
  return audit.score.lower_one_sided_99 >= kMinimumScoreLcb &&
         audit.moves.lower_one_sided_99 >= kMinimumMoveLcb &&
         audit.material_downside_upper99 <= kMaximumDownsideUpper99;
}

struct IndependentPanel {
  std::uint32_t seed = 0;
  std::array<ActionPanel, 2> actions{};
  PairedAudit alternative_vs_d4{};

  bool operator==(const IndependentPanel&) const = default;
};

struct Evaluation {
  D4Anchor anchor{};
  std::array<IndependentPanel, 2> panels{};
  int action = -1;
  bool switched = false;
  WorkMetrics work{};
  std::uint64_t canonical_public_hash = 0;
  double seconds = 0.0;

  bool operator==(const Evaluation&) const = default;
};

ActionPanel evaluateActionPanel(const PublicState& root, int action,
                                std::uint32_t root_seed, WorkMetrics& work,
                                const Deadline* deadline, int horizon) {
  ActionPanel result;
  for (int scenario = 0; scenario < kScenarios; ++scenario) {
    ScenarioOutcome& outcome = result.scenarios[scenario];
    outcome = rolloutScenario(root, action, root_seed, scenario, work,
                              deadline, horizon);
    result.mean_score += outcome.score_return / kScenarios;
    result.mean_moves += outcome.survived_moves / kScenarios;
    result.mean_clears +=
        static_cast<double>(outcome.numbered_clears) / kScenarios;
    result.mean_reveals +=
        static_cast<double>(outcome.covers_revealed) / kScenarios;
    result.survived_cutoffs += outcome.survived_cutoff;
  }
  return result;
}

Evaluation evaluateCanonical(const PublicState& root, const Deadline* deadline,
                             int horizon = kHorizon) {
  if (root.terminal || horizon < 1 || horizon > kHorizon) {
    throw std::invalid_argument("invalid D4 structural evaluation root");
  }
  const auto started = Clock::now();
  Evaluation result;
  result.anchor = chooseD4Canonical(root);
  observeD4(result.anchor, result.work);
  result.action = result.anchor.action;
  result.canonical_public_hash = publicHash(root);
  if (result.anchor.alternative < 0) {
    result.seconds =
        std::chrono::duration<double>(Clock::now() - started).count();
    return result;
  }
  constexpr std::array<std::uint32_t, 2> panel_domains{{kPanelADomain,
                                                        kPanelBDomain}};
  for (int panel = 0; panel < 2; ++panel) {
    IndependentPanel& panel_result = result.panels[panel];
    panel_result.seed = panelSeed(root, panel_domains[panel]);
    panel_result.actions[0] = evaluateActionPanel(
        root, result.anchor.action, panel_result.seed, result.work, deadline,
        horizon);
    panel_result.actions[1] = evaluateActionPanel(
        root, result.anchor.alternative, panel_result.seed, result.work,
        deadline, horizon);
    panel_result.alternative_vs_d4 = pairedAudit(
        panel_result.actions[1], panel_result.actions[0]);
  }
  if (passesUltra(result.panels[0].alternative_vs_d4) &&
      passesUltra(result.panels[1].alternative_vs_d4)) {
    result.action = result.anchor.alternative;
    result.switched = true;
  }
  const std::uint64_t horizon_scale = static_cast<std::uint64_t>(horizon);
  const std::uint64_t maximum_transitions =
      2ull * 2ull * kScenarios * horizon_scale;
  const std::uint64_t maximum_calls =
      2ull * 2ull * kScenarios * (horizon_scale - 1u);
  if (result.work.d4_work > kMaximumD4WorkPerDecision ||
      result.work.peak_d4_cache_entries > d4::kMaximumCacheEntries ||
      result.work.synthetic_transitions > maximum_transitions ||
      result.work.continuation_calls > maximum_calls ||
      result.work.constructive_work >
          maximum_calls * kMaximumConstructiveWorkPerCall) {
    throw std::runtime_error("D4 structural evaluation exceeded work proof");
  }
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

Evaluation chooseAction(const PublicState& source, const Deadline* deadline,
                        int horizon = kHorizon) {
  bool mirrored = false;
  const PublicState canonical = frozen::canonicalPublic(source, mirrored);
  Evaluation result = evaluateCanonical(canonical, deadline, horizon);
  if (!mirrored) return result;
  result.action = kBoardSize - 1 - result.action;
  result.anchor.action = kBoardSize - 1 - result.anchor.action;
  if (result.anchor.alternative >= 0) {
    result.anchor.alternative = kBoardSize - 1 - result.anchor.alternative;
  }
  std::array<double, kBoardSize> values{};
  for (int column = 0; column < kBoardSize; ++column) {
    values[column] = result.anchor.values[kBoardSize - 1 - column];
  }
  result.anchor.values = values;
  return result;
}

enum class SeedCohort { kFitting, kScreen };

bool allowedSeed(std::uint32_t seed, SeedCohort cohort) {
  const std::uint32_t start =
      cohort == SeedCohort::kFitting ? kFittingSeedStart : kScreenSeedStart;
  const int games =
      cohort == SeedCohort::kFitting ? kFittingGames : kScreenGames;
  return seed >= start && seed < start + static_cast<std::uint32_t>(games) &&
         (seed >> 16u) == 0x3d6eu;
}

void requireSeed(std::uint32_t seed, SeedCohort cohort) {
  if (!allowedSeed(seed, cohort)) {
    throw std::invalid_argument("seed outside D4 structural veto allowlist");
  }
}

struct SwitchRecord {
  PublicState state{};
  int move_index = 0;
  int d4_action = -1;
  int alternative = -1;
  std::array<double, kBoardSize> d4_values{};
  std::array<IndependentPanel, 2> panels{};
};

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::int64_t numbered_clears = 0;
  std::int64_t covers_revealed = 0;
  int maximum_chain = 0;
  int decisions = 0;
  int switches = 0;
  double decision_seconds = 0.0;
  WorkMetrics work{};
  std::uint64_t disc_stream_hash = 0;
  std::uint64_t decision_checksum = 0x4434'5354'5654'4f21ull;
  std::vector<SwitchRecord> switch_records;
};

std::uint64_t discStreamHash(std::uint32_t seed, int maximum_moves) {
  std::uint64_t hash = 0x9e37'79b9'7f4a'7c15ull;
  for (int move = 0; move < maximum_moves; ++move) {
    hashCombine(hash, headlessDisc(seed, move));
  }
  return hash;
}

void observeMove(const MoveResult& move, GameResult& result) {
  for (const Wave& wave : move.waves) {
    result.numbered_clears += wave.cleared;
    result.covers_revealed += wave.revealed;
    result.maximum_chain = std::max(result.maximum_chain, wave.depth);
  }
}

enum class Policy { kD4, kVeto };

GameResult runGame(std::uint32_t seed, SeedCohort cohort, Policy policy,
                   const Deadline& deadline) {
  requireSeed(seed, cohort);
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  result.disc_stream_hash = discStreamHash(seed, kMaximumMoves);
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    deadline.check();
    if (state.next_disc != headlessDisc(seed, state.moves_played)) {
      throw std::runtime_error("actual visible disc stream guard failed");
    }
    const PublicState public_state = frozen::publicState(state);
    const auto decision_started = Clock::now();
    int action = -1;
    if (policy == Policy::kD4) {
      const D4Anchor anchor = chooseD4(public_state);
      observeD4(anchor, result.work);
      action = anchor.action;
    } else {
      const Evaluation evaluation = chooseAction(public_state, &deadline);
      result.work += evaluation.work;
      action = evaluation.action;
      ++result.decisions;
      result.switches += evaluation.switched;
      hashCombine(result.decision_checksum,
                  evaluation.canonical_public_hash);
      hashCombine(result.decision_checksum,
                  static_cast<std::uint64_t>(action + 1));
      if (evaluation.switched) {
        result.switch_records.push_back({
            public_state,
            state.moves_played,
            evaluation.anchor.action,
            evaluation.anchor.alternative,
            evaluation.anchor.values,
            evaluation.panels,
        });
      }
    }
    result.decision_seconds +=
        std::chrono::duration<double>(Clock::now() - decision_started).count();
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("actual D4 structural policy chose illegal move");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("actual D4 structural transition failed");
    }
    observeMove(move, result);
    enforceRssLimit();
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  return result;
}

struct PairedGame {
  GameResult d4{};
  GameResult candidate{};
};

struct Cohort {
  std::vector<std::optional<PairedGame>> games;
  int attempted = 0;
  int completed = 0;
  bool aborted = false;
  std::string abort_reason;
  double wall_seconds = 0.0;
};

Cohort runCohort(std::uint32_t start, int games, SeedCohort seed_cohort,
                 int threads, const Deadline& deadline,
                 std::string_view label) {
  const auto started = Clock::now();
  Cohort result;
  result.games.resize(static_cast<std::size_t>(games));
  std::atomic<int> next{0};
  std::atomic<int> completed{0};
  std::atomic<bool> stopped{false};
  std::mutex failure_mutex;
  std::vector<std::future<void>> workers;
  const int worker_count = std::max(1, std::min(threads, games));
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [&, worker]() {
      static_cast<void>(worker);
      while (!stopped.load()) {
        const int game = next.fetch_add(1);
        if (game >= games) return;
        try {
          const std::uint32_t seed = start + static_cast<std::uint32_t>(game);
          PairedGame pair;
          pair.d4 = runGame(seed, seed_cohort, Policy::kD4, deadline);
          pair.candidate =
              runGame(seed, seed_cohort, Policy::kVeto, deadline);
          if (pair.d4.disc_stream_hash != pair.candidate.disc_stream_hash) {
            throw std::runtime_error("paired actual disc streams differed");
          }
          result.games[static_cast<std::size_t>(game)] = std::move(pair);
          const int done = completed.fetch_add(1) + 1;
          const PairedGame& stored =
              *result.games[static_cast<std::size_t>(game)];
          const std::lock_guard<std::mutex> lock(report_mutex);
          std::cerr << "D4-structural " << label << ' ' << done << '/'
                    << games << " seed=0x" << std::hex << seed << std::dec
                    << " d4=" << stored.d4.score << '/' << stored.d4.moves
                    << " candidate=" << stored.candidate.score << '/'
                    << stored.candidate.moves << " switches="
                    << stored.candidate.switches << '\n';
        } catch (const std::exception& error) {
          stopped.store(true);
          const std::lock_guard<std::mutex> lock(failure_mutex);
          if (result.abort_reason.empty()) result.abort_reason = error.what();
        }
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.attempted = std::min(next.load(), games);
  result.completed = completed.load();
  result.aborted = result.completed != games;
  result.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

struct Summary {
  int games = 0;
  int natural = 0;
  int censored = 0;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double mean_decision_ms = 0.0;
  std::int64_t total_moves = 0;
  std::int64_t total_clears = 0;
  std::int64_t total_reveals = 0;
  int decisions = 0;
  int switches = 0;
  WorkMetrics work{};
  std::uint64_t checksum = 0x5355'4d44'3456'4554ull;
};

Summary summarize(const Cohort& cohort, Policy policy) {
  Summary result;
  double decision_seconds = 0.0;
  for (const std::optional<PairedGame>& optional_pair : cohort.games) {
    if (!optional_pair.has_value()) continue;
    const GameResult& game =
        policy == Policy::kD4 ? optional_pair->d4 : optional_pair->candidate;
    ++result.games;
    result.mean_score += game.score;
    result.mean_moves += game.moves;
    result.natural += !game.censored;
    result.censored += game.censored;
    result.total_moves += game.moves;
    result.total_clears += game.numbered_clears;
    result.total_reveals += game.covers_revealed;
    result.decisions += game.decisions;
    result.switches += game.switches;
    decision_seconds += game.decision_seconds;
    result.work += game.work;
    hashCombine(result.checksum, game.decision_checksum);
  }
  if (result.games > 0) {
    result.mean_score /= result.games;
    result.mean_moves /= result.games;
  }
  if (result.total_moves > 0) {
    result.clears_per_move =
        static_cast<double>(result.total_clears) / result.total_moves;
    result.reveals_per_move =
        static_cast<double>(result.total_reveals) / result.total_moves;
    result.mean_decision_ms = 1'000.0 * decision_seconds / result.total_moves;
  }
  return result;
}

struct Gate {
  double score_ratio = 0.0;
  double move_ratio = 0.0;
  int joint_wins = 0;
  bool complete = false;
  bool score_passed = false;
  bool moves_passed = false;
  bool clears_passed = false;
  bool reveals_passed = false;
  bool joint_passed = false;
  bool passed = false;
};

Gate cohortGate(const Cohort& cohort, const Summary& baseline,
                const Summary& candidate, double score_ratio,
                double move_ratio, int joint_wins) {
  Gate result;
  result.complete = cohort.completed == static_cast<int>(cohort.games.size()) &&
                    !cohort.aborted;
  if (baseline.games == 0 || baseline.mean_score <= 0.0 ||
      baseline.mean_moves <= 0.0) {
    return result;
  }
  result.score_ratio = candidate.mean_score / baseline.mean_score;
  result.move_ratio = candidate.mean_moves / baseline.mean_moves;
  result.score_passed = result.score_ratio >= score_ratio;
  result.moves_passed = result.move_ratio >= move_ratio;
  result.clears_passed = candidate.clears_per_move >= baseline.clears_per_move;
  result.reveals_passed =
      candidate.reveals_per_move >= baseline.reveals_per_move;
  for (const std::optional<PairedGame>& pair : cohort.games) {
    if (!pair.has_value()) continue;
    result.joint_wins += pair->candidate.score > pair->d4.score &&
                         pair->candidate.moves > pair->d4.moves;
  }
  result.joint_passed = result.joint_wins >= joint_wins;
  result.passed = result.complete && result.score_passed &&
                  result.moves_passed && result.clears_passed &&
                  result.reveals_passed && result.joint_passed;
  return result;
}

std::string hex64(std::uint64_t value) {
  std::ostringstream output;
  output << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
  return output.str();
}

std::string jsonEscape(std::string_view source) {
  std::string result;
  for (const char character : source) {
    if (character == '\\' || character == '"') result.push_back('\\');
    result.push_back(character);
  }
  return result;
}

void writeMetric(std::ostream& output, const PairedMetric& metric) {
  output << "{\"mean\":" << metric.mean
         << ",\"standardError\":" << metric.standard_error
         << ",\"lowerOneSided99\":" << metric.lower_one_sided_99
         << ",\"minimum\":" << metric.minimum
         << ",\"maximum\":" << metric.maximum << ",\"wins\":"
         << metric.wins << ",\"ties\":" << metric.ties
         << ",\"losses\":" << metric.losses << '}';
}

void writeAudit(std::ostream& output, const PairedAudit& audit) {
  output << "{\"score\":";
  writeMetric(output, audit.score);
  output << ",\"moves\":";
  writeMetric(output, audit.moves);
  output << ",\"materialDownsides\":" << audit.material_downsides
         << ",\"materialDownsideUpper99\":"
         << audit.material_downside_upper99 << '}';
}

void writeActionPanel(std::ostream& output, const ActionPanel& panel) {
  output << "{\"meanScore\":" << panel.mean_score
         << ",\"meanMoves\":" << panel.mean_moves
         << ",\"meanNumberedClears\":" << panel.mean_clears
         << ",\"meanCoversRevealed\":" << panel.mean_reveals
         << ",\"survivedCutoffs\":" << panel.survived_cutoffs << '}';
}

std::uint64_t writeSwitches(const std::string& path, const Cohort& cohort,
                            std::string_view phase) {
  std::ofstream output(path, phase == "fitting" ? std::ios::trunc
                                                 : std::ios::app);
  if (!output) throw std::runtime_error("could not open switch JSONL");
  output << std::setprecision(12);
  std::uint64_t records = 0;
  for (const std::optional<PairedGame>& optional_pair : cohort.games) {
    if (!optional_pair.has_value()) continue;
    const GameResult& game = optional_pair->candidate;
    for (const SwitchRecord& record : game.switch_records) {
      output << "{\"phase\":\"" << phase
             << "\",\"provenance\":{\"gameSeed\":" << game.seed
             << ",\"moveIndex\":" << record.move_index << "},"
             << "\"modelInput\":{\"board\":\""
             << serializeBoard(record.state.board)
             << "\",\"nextDisc\":"
             << static_cast<int>(record.state.next_disc)
             << ",\"movesRemaining\":"
             << static_cast<int>(record.state.moves_remaining)
             << ",\"terminal\":"
             << (record.state.terminal ? "true" : "false") << "},"
             << "\"excludedFromModelInput\":[\"gameSeed\",\"moveIndex\","
                "\"score\",\"level\",\"history\",\"scenario\","
                "\"futureTape\"],\"d4Action\":" << record.d4_action
             << ",\"alternative\":" << record.alternative
             << ",\"d4RootValues\":[";
      for (int column = 0; column < kBoardSize; ++column) {
        if (column != 0) output << ',';
        if (std::isfinite(record.d4_values[column])) {
          output << record.d4_values[column];
        } else {
          output << "null";
        }
      }
      output << "],\"panels\":[";
      for (int panel = 0; panel < 2; ++panel) {
        if (panel != 0) output << ',';
        output << "{\"d4\":";
        writeActionPanel(output, record.panels[panel].actions[0]);
        output << ",\"alternative\":";
        writeActionPanel(output, record.panels[panel].actions[1]);
        output << ",\"alternativeVsD4\":";
        writeAudit(output, record.panels[panel].alternative_vs_d4);
        output << '}';
      }
      output << "]}\n";
      ++records;
    }
  }
  output.close();
  if (!output) throw std::runtime_error("could not finish switch JSONL");
  return records;
}

void writeWork(std::ostream& output, const WorkMetrics& work) {
  output << "{\"d4Work\":" << work.d4_work
         << ",\"d4Nodes\":" << work.d4_nodes
         << ",\"d4CacheHits\":" << work.d4_cache_hits
         << ",\"peakD4CacheEntries\":" << work.peak_d4_cache_entries
         << ",\"syntheticTransitions\":" << work.synthetic_transitions
         << ",\"continuationCalls\":" << work.continuation_calls
         << ",\"constructiveWork\":" << work.constructive_work << '}';
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"games\":" << summary.games << ",\"natural\":"
         << summary.natural << ",\"censored\":" << summary.censored
         << ",\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"numberedClearsPerMove\":" << summary.clears_per_move
         << ",\"coversRevealedPerMove\":" << summary.reveals_per_move
         << ",\"meanDecisionMs\":" << summary.mean_decision_ms
         << ",\"decisions\":" << summary.decisions
         << ",\"switches\":" << summary.switches
         << ",\"checksum\":\"" << hex64(summary.checksum)
         << "\",\"work\":";
  writeWork(output, summary.work);
  output << '}';
}

void writeGate(std::ostream& output, const Gate& gate) {
  output << "{\"scoreRatio\":" << gate.score_ratio
         << ",\"moveRatio\":" << gate.move_ratio
         << ",\"jointWins\":" << gate.joint_wins
         << ",\"complete\":" << (gate.complete ? "true" : "false")
         << ",\"scorePassed\":"
         << (gate.score_passed ? "true" : "false")
         << ",\"movesPassed\":"
         << (gate.moves_passed ? "true" : "false")
         << ",\"clearsPassed\":"
         << (gate.clears_passed ? "true" : "false")
         << ",\"revealsPassed\":"
         << (gate.reveals_passed ? "true" : "false")
         << ",\"jointPassed\":"
         << (gate.joint_passed ? "true" : "false")
         << ",\"passed\":" << (gate.passed ? "true" : "false") << '}';
}

void writeCohort(std::ostream& output, const Cohort& cohort,
                 const Summary& baseline, const Summary& candidate,
                 const Gate& gate, std::uint32_t seed_start) {
  output << "{\"seedStart\":" << seed_start << ",\"attempted\":"
         << cohort.attempted << ",\"completed\":" << cohort.completed
         << ",\"aborted\":" << (cohort.aborted ? "true" : "false")
         << ",\"abortReason\":\"" << jsonEscape(cohort.abort_reason)
         << "\",\"wallSeconds\":" << cohort.wall_seconds
         << ",\"d4\":";
  writeSummary(output, baseline);
  output << ",\"candidate\":";
  writeSummary(output, candidate);
  output << ",\"gate\":";
  writeGate(output, gate);
  output << ",\"pairs\":[";
  bool first = true;
  for (const std::optional<PairedGame>& pair : cohort.games) {
    if (!pair.has_value()) continue;
    if (!first) output << ',';
    first = false;
    output << "{\"seed\":" << pair->d4.seed
           << ",\"d4Score\":" << pair->d4.score
           << ",\"d4Moves\":" << pair->d4.moves
           << ",\"candidateScore\":" << pair->candidate.score
           << ",\"candidateMoves\":" << pair->candidate.moves
           << ",\"switches\":" << pair->candidate.switches << '}';
  }
  output << "]}";
}

void writeArtifact(const Options& options, double preflight_seconds,
                   double projected_total_seconds, bool projection_passed,
                   const Cohort* fitting, const Summary* fitting_d4,
                   const Summary* fitting_candidate, const Gate* fitting_gate,
                   std::uint64_t fitting_switches, const Cohort* screen,
                   const Summary* screen_d4, const Summary* screen_candidate,
                   const Gate* screen_gate, std::uint64_t screen_switches,
                   double total_wall_seconds) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open D4 veto artifact");
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"d4-structural-terminal-veto\",\n"
         << "  \"sourceSha256\":\"" << options.source_sha256 << "\",\n"
         << "  \"preregistered\":true,\n"
         << "  \"policy\":{\"immutableAnchor\":\"completed fair-D4/s5\","
            "\"rootCandidates\":\"D4 top two only\","
            "\"continuation\":\"frozen constructiveContinuation\","
            "\"outerConstructivePolicyUsed\":false,"
            "\"fallback\":\"exact D4\"},\n"
         << "  \"publicBoundary\":[\"board\",\"nextDisc\","
            "\"movesRemaining\",\"terminal\"],\n"
         << "  \"excludedFromDecision\":[\"gameSeed\",\"score\",\"level\","
            "\"moveIndex\",\"history\",\"scenario\",\"futureTape\"],\n"
         << "  \"rollout\":{\"panels\":2,\"scenariosPerPanel\":"
         << kScenarios << ",\"horizon\":" << kHorizon
         << ",\"reward\":\"unchanged engine score and survived moves\","
            "\"cutoffTail\":0,\"commonSiblingStreams\":true,"
            "\"independentPanels\":true,\"eventStratified\":true,"
            "\"revealVisibleDomainsSeparate\":true},\n"
         << "  \"ultraGate\":{\"requiredInEachPanel\":true,"
            "\"scoreLowerOneSided99\":" << kMinimumScoreLcb
         << ",\"moveLowerOneSided99\":" << kMinimumMoveLcb
         << ",\"materialScoreLoss\":" << kMaterialScoreLoss
         << ",\"materialMoveLoss\":" << kMaterialMoveLoss
         << ",\"maximumWilsonDownsideUpper99\":"
         << kMaximumDownsideUpper99 << "},\n"
         << "  \"seedDiscipline\":{\"fittingStart\":"
         << kFittingSeedStart << ",\"fittingGames\":" << kFittingGames
         << ",\"screenStart\":" << kScreenSeedStart
         << ",\"screenGames\":" << kScreenGames
         << ",\"screenConditionalOnFit\":true,"
            "\"forbiddenFamilies\":[\"0x4d\",\"0x7d\",\"0xd7\"]},\n"
         << "  \"resources\":{\"wallLimitSeconds\":"
         << kWallLimitSeconds << ",\"rssLimitBytes\":" << kRssLimitBytes
         << ",\"maximumD4WorkPerDecision\":"
         << kMaximumD4WorkPerDecision
         << ",\"maximumSyntheticTransitionsPerDecision\":"
         << kMaximumSyntheticTransitionsPerDecision
         << ",\"maximumContinuationCallsPerDecision\":"
         << kMaximumContinuationCallsPerDecision
         << ",\"maximumConstructiveWorkPerDecision\":"
         << kMaximumConstructiveWorkPerDecision
         << ",\"preflightDecisionSeconds\":" << preflight_seconds
         << ",\"projectionMovesPerGame\":" << kProjectionMovesPerGame
         << ",\"projectionSafetyFactor\":" << kProjectionSafetyFactor
         << ",\"projectedTotalSeconds\":" << projected_total_seconds
         << ",\"projectionPassed\":"
         << (projection_passed ? "true" : "false") << "},\n"
         << "  \"fittingGateDefinition\":{\"scoreRatio\":"
         << kFittingScoreRatio << ",\"moveRatio\":" << kFittingMoveRatio
         << ",\"jointWins\":" << kFittingJointWins
         << ",\"clearRevealNonregression\":true},\n"
         << "  \"screenGateDefinition\":{\"scoreRatio\":"
         << kScreenScoreRatio << ",\"moveRatio\":" << kScreenMoveRatio
         << ",\"jointWins\":" << kScreenJointWins
         << ",\"clearRevealNonregression\":true},\n"
         << "  \"fitting\":";
  if (fitting == nullptr) {
    output << "null";
  } else {
    writeCohort(output, *fitting, *fitting_d4, *fitting_candidate,
                *fitting_gate, kFittingSeedStart);
  }
  output << ",\n  \"fittingSwitchPanelRecords\":" << fitting_switches
         << ",\n  \"screenOpened\":"
         << (screen != nullptr ? "true" : "false") << ",\n  \"screen\":";
  if (screen == nullptr) {
    output << "null";
  } else {
    writeCohort(output, *screen, *screen_d4, *screen_candidate, *screen_gate,
                kScreenSeedStart);
  }
  output << ",\n  \"screenSwitchPanelRecords\":" << screen_switches
         << ",\n  \"qualified\":"
         << (screen_gate != nullptr && screen_gate->passed ? "true" : "false")
         << ",\n  \"switchPanelPath\":\"" << jsonEscape(options.switches)
         << "\",\n  \"totalWallSeconds\":" << total_wall_seconds
         << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
  output.close();
  if (!output) throw std::runtime_error("could not finish D4 veto artifact");
}

void writeReadme(const Options& options, double preflight_seconds,
                 double projected_total_seconds, bool projection_passed,
                 const Summary* fitting_d4,
                 const Summary* fitting_candidate, const Gate* fitting_gate,
                 const Summary* screen_d4, const Summary* screen_candidate,
                 const Gate* screen_gate, double total_wall_seconds) {
  std::ofstream output(options.readme);
  if (!output) throw std::runtime_error("could not open D4 veto README");
  output << std::fixed << std::setprecision(6)
         << "# D4 structural terminal veto\n\n"
         << "Source SHA-256: `" << options.source_sha256 << "`\n\n"
         << "The immutable completed fair-D4/s5 policy supplies the root "
            "ordering. Only its top two legal actions are rolled out. Every "
            "later move is the frozen cheap `constructiveContinuation`; its "
            "D3/D4-shielded outer policy is never called. Two independent "
            "127-scenario, public-hash-derived panels run for at most 200 "
            "moves with unchanged engine score and survived-move rewards. "
            "The runner-up replaces D4 only when the prior ultra 99% gate "
            "passes independently in both panels.\n\n"
         << "- Preflight decision seconds: " << preflight_seconds << "\n"
         << "- Projected total seconds: " << projected_total_seconds << "\n"
         << "- Projection passed: " << (projection_passed ? "yes" : "no")
         << "\n- Total wall seconds: " << total_wall_seconds << "\n"
         << "- Peak RSS bytes: " << peakRssBytes() << "\n";
  if (fitting_gate != nullptr) {
    output << "\n## Fitting (0x3d6e4000..0x3d6e4003)\n\n"
           << "- D4 mean: " << fitting_d4->mean_score << " points / "
           << fitting_d4->mean_moves << " moves\n"
           << "- Candidate mean: " << fitting_candidate->mean_score
           << " points / " << fitting_candidate->mean_moves << " moves\n"
           << "- Score/move ratios: " << fitting_gate->score_ratio << " / "
           << fitting_gate->move_ratio << "\n"
           << "- Joint wins: " << fitting_gate->joint_wins << "/4\n"
           << "- Passed: " << (fitting_gate->passed ? "yes" : "no") << "\n";
  }
  if (screen_gate != nullptr) {
    output << "\n## Screen (0x3d6e5000..0x3d6e5007)\n\n"
           << "- D4 mean: " << screen_d4->mean_score << " points / "
           << screen_d4->mean_moves << " moves\n"
           << "- Candidate mean: " << screen_candidate->mean_score
           << " points / " << screen_candidate->mean_moves << " moves\n"
           << "- Score/move ratios: " << screen_gate->score_ratio << " / "
           << screen_gate->move_ratio << "\n"
           << "- Joint wins: " << screen_gate->joint_wins << "/8\n"
           << "- Passed: " << (screen_gate->passed ? "yes" : "no") << "\n";
  } else if (fitting_gate != nullptr && !fitting_gate->passed) {
    output << "\nThe fitting gate failed, so the screen remained sealed.\n";
  }
  output.close();
  if (!output) throw std::runtime_error("could not finish D4 veto README");
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

PublicState asymmetricFixture() {
  PublicState state;
  state.board.fill(kEmpty);
  state.board[indexOf(6, 0)] = kSolid;
  state.board[indexOf(6, 1)] = 4;
  state.board[indexOf(6, 2)] = 2;
  state.board[indexOf(5, 2)] = kCracked;
  state.board[indexOf(6, 4)] = 6;
  state.next_disc = 3;
  state.moves_remaining = 3;
  return state;
}

void verifyExactStrata(std::uint32_t seed, std::uint32_t domain, int event) {
  std::array<int, kScenarios> counts{};
  for (int scenario = 0; scenario < kScenarios; ++scenario) {
    const double unit = detail::stratifiedUnit(seed, scenario, kScenarios,
                                               domain, event);
    const int stratum = static_cast<int>(std::floor(unit * kScenarios));
    expect(stratum >= 0 && stratum < kScenarios,
           "chance stratum out of range");
    ++counts[static_cast<std::size_t>(stratum)];
  }
  for (const int count : counts) {
    expect(count == 1, "chance event was not exactly 127-stratified");
  }
}

bool selfTest(std::ostream& output) {
  expect(kLevelBonus == 17'000, "corrected level bonus regression");
  const PublicState fixture = asymmetricFixture();
  const D4Anchor d4_first = chooseD4(fixture);
  const D4Anchor d4_second = chooseD4(fixture);
  expect(d4_first == d4_second && d4_first.complete &&
             d4_first.completed_depth == 4 &&
             isLegal(fixture.board, d4_first.action) &&
             isLegal(fixture.board, d4_first.alternative),
         "immutable D4 anchor determinism/completion failed");
  const PublicContinuationDecision continuation =
      chooseConstructiveContinuation(fixture, 17);
  bool fixture_mirrored = false;
  const PublicState canonical =
      frozen::canonicalPublic(fixture, fixture_mirrored);
  const frozen::OneStepDecision direct = frozen::constructiveContinuation(
      frozen::materialize(canonical), 17);
  const int mapped_direct = fixture_mirrored
                                ? kBoardSize - 1 - direct.action
                                : direct.action;
  expect(continuation.action == mapped_direct &&
             continuation.value == direct.value &&
             continuation.work == direct.work &&
             isLegal(fixture.board, continuation.action),
         "constructiveContinuation wrapper changed frozen mechanism");

  const Evaluation first = chooseAction(fixture, nullptr, 1);
  const Evaluation second = chooseAction(fixture, nullptr, 1);
  const bool deterministic_evidence =
      first.anchor == second.anchor && first.panels == second.panels &&
      first.action == second.action && first.switched == second.switched &&
      first.work == second.work &&
      first.canonical_public_hash == second.canonical_public_hash;
  expect(deterministic_evidence && !first.switched &&
             first.action == d4_first.action &&
             first.anchor.values == d4_first.values &&
             first.anchor.work == d4_first.work,
         "exact zero-switch D4 parity/determinism failed");
  const Evaluation reflected = chooseAction(frozen::mirror(fixture), nullptr, 1);
  expect(reflected.action == kBoardSize - 1 - first.action &&
             reflected.anchor.action ==
                 kBoardSize - 1 - first.anchor.action &&
             reflected.anchor.alternative ==
                 kBoardSize - 1 - first.anchor.alternative &&
             reflected.canonical_public_hash == first.canonical_public_hash &&
             reflected.panels == first.panels,
         "D4 structural reflection failed");

  State metadata = frozen::materialize(fixture);
  metadata.score = 8'765'432;
  metadata.level = 71;
  metadata.moves_played = 912;
  const PublicState normalized = frozen::publicState(metadata);
  const Evaluation normalized_evaluation =
      chooseAction(normalized, nullptr, 1);
  expect(normalized == fixture && publicHash(normalized) == publicHash(fixture) &&
             normalized_evaluation.anchor == first.anchor &&
             normalized_evaluation.panels == first.panels &&
             normalized_evaluation.action == first.action &&
             normalized_evaluation.switched == first.switched &&
             normalized_evaluation.work == first.work,
         "D4 structural policy used hidden metadata");

  constexpr std::uint32_t test_seed = 0x1234'5678u;
  for (const int event : {0, 1, 63, 64, 12'799}) {
    verifyExactStrata(test_seed, kRevealDomain, event);
    verifyExactStrata(test_seed, kVisibleDomain, event);
  }
  expect(panelSeed(fixture, kPanelADomain) !=
             panelSeed(fixture, kPanelBDomain),
         "independent panel seeds collided");
  PublicState tape_fixture;
  tape_fixture.board.fill(kEmpty);
  tape_fixture.board[indexOf(6, 1)] = kCracked;
  tape_fixture.next_disc = 1;
  tape_fixture.moves_remaining = 4;
  MoveResult standard;
  expect(playSyntheticMove(tape_fixture, 0, test_seed, 0, 0, standard),
         "chance-domain fixture failed");
  bool visible_discriminates = false;
  bool reveal_discriminates = false;
  for (std::uint32_t salt = 1; salt < 512; ++salt) {
    MoveResult changed_visible;
    expect(playSyntheticMove(tape_fixture, 0, test_seed, 0, 0,
                             changed_visible, kRevealDomain,
                             kVisibleDomain ^ salt),
           "visible-domain fixture failed");
    if (changed_visible.state.next_disc != standard.state.next_disc) {
      expect(changed_visible.state.board == standard.state.board &&
                 changed_visible.score_delta == standard.score_delta,
             "visible chance leaked into reveals");
      visible_discriminates = true;
    }
    MoveResult changed_reveal;
    expect(playSyntheticMove(tape_fixture, 0, test_seed, 0, 0,
                             changed_reveal, kRevealDomain ^ salt,
                             kVisibleDomain),
           "reveal-domain fixture failed");
    if (changed_reveal.state.board != standard.state.board) {
      expect(changed_reveal.state.next_disc == standard.state.next_disc,
             "reveal chance leaked into visible disc");
      reveal_discriminates = true;
    }
  }
  expect(visible_discriminates && reveal_discriminates,
         "chance domains were not discriminating/independent");

  std::array<double, kScenarios> constants{};
  constants.fill(2.0);
  const PairedMetric constant_metric = pairedMetric(constants);
  expect(std::abs(constant_metric.mean - 2.0) < 1e-12 &&
             constant_metric.standard_error < 1e-12 &&
             std::abs(constant_metric.lower_one_sided_99 - 2.0) < 1e-12,
         "paired confidence constant-vector math failed");
  constants[0] = -2.0;
  const PairedMetric varied = pairedMetric(constants);
  expect(varied.standard_error > 0.0 &&
             varied.lower_one_sided_99 < varied.mean,
         "paired confidence variance math failed");
  PairedAudit passing;
  passing.score.lower_one_sided_99 = kMinimumScoreLcb;
  passing.moves.lower_one_sided_99 = kMinimumMoveLcb;
  passing.material_downside_upper99 = kMaximumDownsideUpper99;
  expect(passesUltra(passing), "inclusive ultra boundary failed");
  passing.moves.lower_one_sided_99 =
      std::nextafter(kMinimumMoveLcb, 0.0);
  expect(!passesUltra(passing), "ultra gate accepted sub-bound audit");
  expect(wilsonUpper99(0, kScenarios) > 0.0 &&
             wilsonUpper99(0, kScenarios) < 0.05 &&
             std::abs(wilsonUpper99(kScenarios, kScenarios) - 1.0) < 1e-12,
         "Wilson downside confidence failed");

  expect(first.work.synthetic_transitions <=
             2ull * 2ull * kScenarios &&
             first.work.continuation_calls == 0 &&
             first.work.d4_work <= kMaximumD4WorkPerDecision &&
             kMaximumSyntheticTransitionsPerDecision == 101'600 &&
             kMaximumConstructiveWorkPerDecision == 4'953'508,
         "resource proof changed");
  expect(allowedSeed(kFittingSeedStart, SeedCohort::kFitting) &&
             allowedSeed(kFittingSeedStart + 3, SeedCohort::kFitting) &&
             allowedSeed(kScreenSeedStart + 7, SeedCohort::kScreen),
         "authorized seeds rejected");
  expect(throwsInvalid([] {
           requireSeed(0x3d6e'3fffu, SeedCohort::kFitting);
         }) &&
             throwsInvalid([] {
               requireSeed(0x3d6e'4004u, SeedCohort::kFitting);
             }) &&
             throwsInvalid([] {
               requireSeed(0x4d6e'4000u, SeedCohort::kFitting);
             }) &&
             throwsInvalid([] {
               requireSeed(0x7d6e'5000u, SeedCohort::kScreen);
             }) &&
             throwsInvalid([] {
               requireSeed(0xd76e'5000u, SeedCohort::kScreen);
             }),
         "seed guards failed");
  enforceRssLimit();
  output << std::setprecision(12)
         << "D4_STRUCTURAL_TERMINAL_VETO_SELF_TEST {\"passed\":true,"
         << "\"immutableD4\":true,\"frozenConstructiveContinuation\":true,"
         << "\"exactZeroSwitchD4Parity\":true,\"publicOnly\":true,"
         << "\"metadataBlind\":true,\"reflection\":true,"
         << "\"deterministic\":true,\"legal\":true,\"panels\":2,"
         << "\"scenariosPerPanel\":" << kScenarios
         << ",\"horizon\":" << kHorizon
         << ",\"chanceDomains\":true,\"confidenceMath\":true,"
         << "\"resourceProof\":true,\"seedGuards\":true,"
         << "\"fixtureD4Work\":" << first.work.d4_work
         << ",\"peakRssBytes\":" << peakRssBytes() << "}\n";
  return true;
}

struct Projection {
  double preflight_seconds = 0.0;
  double projected_total_seconds = 0.0;
  bool passed = false;
  Evaluation evaluation{};
};

Projection measureProjection(int threads, const Deadline& deadline) {
  PublicState preflight;
  preflight.board = initialBoard();
  preflight.next_disc = 3;
  preflight.moves_remaining = kMovesPerLevel;
  Projection result;
  result.evaluation = chooseAction(preflight, &deadline, kHorizon);
  result.preflight_seconds = result.evaluation.seconds;
  const int fitting_batches = (kFittingGames + threads - 1) / threads;
  const int screen_batches = (kScreenGames + threads - 1) / threads;
  result.projected_total_seconds =
      deadline.seconds() +
      result.preflight_seconds * kProjectionMovesPerGame *
          (fitting_batches + screen_batches) * kProjectionSafetyFactor +
      kProjectionReserveSeconds;
  result.passed = result.projected_total_seconds <= kWallLimitSeconds;
  enforceRssLimit();
  return result;
}

int preflightOnly(const Options& options, std::ostream& output) {
  const Deadline deadline;
  const Projection projection = measureProjection(options.threads, deadline);
  output << std::fixed << std::setprecision(3)
         << "D4_STRUCTURAL_TERMINAL_VETO_PREFLIGHT {\"passed\":"
         << (projection.passed ? "true" : "false")
         << ",\"gameSeedsOpened\":false,\"preflightSeconds\":"
         << projection.preflight_seconds << ",\"projectedTotalSeconds\":"
         << projection.projected_total_seconds
         << ",\"projectedFittingSeconds\":"
         << projection.preflight_seconds * kProjectionMovesPerGame *
                ((kFittingGames + options.threads - 1) / options.threads) *
                kProjectionSafetyFactor
         << ",\"d4Work\":" << projection.evaluation.work.d4_work
         << ",\"syntheticTransitions\":"
         << projection.evaluation.work.synthetic_transitions
         << ",\"continuationCalls\":"
         << projection.evaluation.work.continuation_calls
         << ",\"constructiveWork\":"
         << projection.evaluation.work.constructive_work
         << ",\"switched\":"
         << (projection.evaluation.switched ? "true" : "false")
         << ",\"peakRssBytes\":" << peakRssBytes() << "}\n";
  return projection.passed ? 0 : 2;
}

int run(const Options& options, std::ostream& output) {
  const Deadline deadline;
  const Projection projection = measureProjection(options.threads, deadline);
  const double preflight_seconds = projection.preflight_seconds;
  const double projected_total_seconds = projection.projected_total_seconds;
  const bool projection_passed = projection.passed;
  if (!projection_passed) {
    writeArtifact(options, preflight_seconds, projected_total_seconds, false,
                  nullptr, nullptr, nullptr, nullptr, 0, nullptr, nullptr,
                  nullptr, nullptr, 0, deadline.seconds());
    writeReadme(options, preflight_seconds, projected_total_seconds, false,
                nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                deadline.seconds());
    output << std::fixed << std::setprecision(3)
           << "D4_STRUCTURAL_TERMINAL_VETO_RESULT {\"projectionPassed\":false,"
           << "\"preflightSeconds\":" << preflight_seconds
           << ",\"projectedTotalSeconds\":" << projected_total_seconds
           << ",\"fittingOpened\":false,\"screenOpened\":false,"
           << "\"artifact\":\"" << jsonEscape(options.output) << "\"}\n";
    return 2;
  }

  const Cohort fitting = runCohort(kFittingSeedStart, kFittingGames,
                                   SeedCohort::kFitting, options.threads,
                                   deadline, "fitting");
  const Summary fitting_d4 = summarize(fitting, Policy::kD4);
  const Summary fitting_candidate = summarize(fitting, Policy::kVeto);
  const Gate fitting_gate = cohortGate(
      fitting, fitting_d4, fitting_candidate, kFittingScoreRatio,
      kFittingMoveRatio, kFittingJointWins);
  const std::uint64_t fitting_switches =
      writeSwitches(options.switches, fitting, "fitting");

  std::optional<Cohort> screen;
  Summary screen_d4;
  Summary screen_candidate;
  Gate screen_gate;
  std::uint64_t screen_switches = 0;
  if (fitting_gate.passed) {
    screen = runCohort(kScreenSeedStart, kScreenGames, SeedCohort::kScreen,
                       options.threads, deadline, "screen");
    screen_d4 = summarize(*screen, Policy::kD4);
    screen_candidate = summarize(*screen, Policy::kVeto);
    screen_gate = cohortGate(*screen, screen_d4, screen_candidate,
                             kScreenScoreRatio, kScreenMoveRatio,
                             kScreenJointWins);
    screen_switches = writeSwitches(options.switches, *screen, "screen");
  }
  enforceRssLimit();
  writeArtifact(
      options, preflight_seconds, projected_total_seconds, true, &fitting,
      &fitting_d4, &fitting_candidate, &fitting_gate, fitting_switches,
      screen.has_value() ? &*screen : nullptr,
      screen.has_value() ? &screen_d4 : nullptr,
      screen.has_value() ? &screen_candidate : nullptr,
      screen.has_value() ? &screen_gate : nullptr, screen_switches,
      deadline.seconds());
  writeReadme(options, preflight_seconds, projected_total_seconds, true,
              &fitting_d4, &fitting_candidate, &fitting_gate,
              screen.has_value() ? &screen_d4 : nullptr,
              screen.has_value() ? &screen_candidate : nullptr,
              screen.has_value() ? &screen_gate : nullptr,
              deadline.seconds());
  output << std::fixed << std::setprecision(3)
         << "D4_STRUCTURAL_TERMINAL_VETO_RESULT {\"projectionPassed\":true,"
         << "\"preflightSeconds\":" << preflight_seconds
         << ",\"projectedTotalSeconds\":" << projected_total_seconds
         << ",\"fittingD4Score\":" << fitting_d4.mean_score
         << ",\"fittingD4Moves\":" << fitting_d4.mean_moves
         << ",\"fittingCandidateScore\":" << fitting_candidate.mean_score
         << ",\"fittingCandidateMoves\":"
         << fitting_candidate.mean_moves << ",\"fittingScoreRatio\":"
         << fitting_gate.score_ratio << ",\"fittingMoveRatio\":"
         << fitting_gate.move_ratio << ",\"fittingJointWins\":"
         << fitting_gate.joint_wins << ",\"fittingSwitches\":"
         << fitting_candidate.switches << ",\"fittingPassed\":"
         << (fitting_gate.passed ? "true" : "false")
         << ",\"screenOpened\":"
         << (screen.has_value() ? "true" : "false")
         << ",\"screenPassed\":"
         << (screen.has_value() && screen_gate.passed ? "true" : "false")
         << ",\"totalWallSeconds\":" << deadline.seconds()
         << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"artifact\":\"" << jsonEscape(options.output) << "\"}\n";
  return screen.has_value() && screen_gate.passed ? 0 : 2;
}

}  // namespace drop7::d4_structural_terminal_veto

int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::d4_structural_terminal_veto::selfTest(std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::d4_structural_terminal_veto::parseOptions(argc, argv, 2);
      return drop7::d4_structural_terminal_veto::run(options, std::cout);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--preflight") {
      const auto options =
          drop7::d4_structural_terminal_veto::parseOptions(argc, argv, 2);
      return drop7::d4_structural_terminal_veto::preflightOnly(options,
                                                               std::cout);
    }
    std::cerr << "usage: drop7_d4_structural_terminal_veto --self-test | "
                 "--preflight --source-sha256 HASH [--threads N] | "
                 "--run --source-sha256 HASH [--output PATH] "
                 "[--switches PATH] [--readme PATH] [--threads N]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_d4_structural_terminal_veto: " << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
}
