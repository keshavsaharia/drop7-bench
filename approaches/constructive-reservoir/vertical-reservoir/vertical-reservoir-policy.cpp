#define DROP7_FAIR_ONLY_HORIZON_LIBRARY
#include "../../fair-expectimax/reference/fair-only-horizon.cpp"
#undef DROP7_FAIR_ONLY_HORIZON_LIBRARY

#include <bit>
#include <cstring>
#include <thread>
#include <type_traits>

// Fits a public-state-only nonlinear successor evaluator for vertical-reservoir
// structure.  The model is linear on explicitly nonlinear board features:
// same-target powers, exact inert-addition release ladders, low escape wells,
// horizontal reachability, and cover/potential-popper topology.  Complete-game
// common-random-number CEM is the only fitting mechanism.
namespace drop7::vertical_reservoir_policy {

namespace fair = drop7::fair_only_horizon;
namespace detail = drop7::cfpi::detail;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kTrainingSeedStart = 0x3d63'0000u;
constexpr std::uint32_t kTrainingSeedEndExclusive = 0x3d64'0000u;
constexpr std::uint32_t kTournamentSeedStart = 0x3d64'0000u;
constexpr std::uint32_t kTournamentSeedEndExclusive = 0x3d65'0000u;
constexpr std::uint32_t kProbeSeedStart = 0x4d63'0000u;
constexpr std::uint32_t kProbeSeedEndExclusive = 0x4d63'0080u;
constexpr std::uint32_t kPolicySeed = 0x5652'5356u;  // "VRSV"
constexpr std::uint64_t kOptimizerSeed = 0x7672'2d63'656d'2d31ull;
constexpr std::uint64_t kCheckpointMagic = 0x4452'3756'5253'5631ull;
constexpr std::uint32_t kCheckpointVersion = 1;

constexpr int kSuccessorSamples = 7;
constexpr int kMaximumMoves = 1'000;
constexpr int kPopulation = 65;
constexpr int kElite = 13;
constexpr int kGenerations = 24;
constexpr int kBatchGames = 64;
constexpr int kTournamentGames = 128;
constexpr int kProbeGames = 32;
constexpr int kDefaultThreads = 8;
constexpr double kTerminalValue = -10'000.0;
constexpr double kWallLimitSeconds = 30.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr double kFittingMinimumMoves = 150.0;
constexpr double kFittingMinimumScore = 500'000.0;
constexpr double kFittingRatio = 1.50;
constexpr double kProbeMinimumMoves = 200.0;
constexpr double kPairedT975Df31 = 2.0395134464;
constexpr std::array<int, kBoardSize> kColumnOrder{{3, 2, 4, 1, 5, 0, 6}};

static_assert(kLevelBonus == 17'000);
static_assert(kMovesPerLevel == 5);
static_assert(kSuccessorSamples == kBoardSize);
static_assert(kPopulation == 65 && kPopulation % 2 == 1);
static_assert(kBatchGames >= 64 && kTournamentGames >= 128);
static_assert(kElite > 1 && kElite < kPopulation);
static_assert(kTrainingSeedStart + kGenerations * kBatchGames <=
              kTrainingSeedEndExclusive);
static_assert(kTournamentSeedStart + kTournamentGames <=
              kTournamentSeedEndExclusive);
static_assert(kProbeSeedStart + kProbeGames <= kProbeSeedEndExclusive);
static_assert((kTrainingSeedStart >> 24u) == 0x3du &&
              (kTournamentSeedStart >> 24u) == 0x3du &&
              (kProbeSeedStart >> 24u) == 0x4du);
static_assert((kTrainingSeedStart >> 24u) != 0x7du &&
              (kTrainingSeedStart >> 24u) != 0xd7u &&
              (kProbeSeedStart >> 24u) != 0x7du &&
              (kProbeSeedStart >> 24u) != 0xd7u);

enum BaseFeature : int {
  kBias,
  kVerticalViable,
  kVerticalDead,
  kVerticalInverseGap,
  kVerticalInverseGapSquared,
  kSameTargetPairs,
  kSameTargetQuadratic,
  kSameTargetCubic,
  kBestSameTargetMultiplicity,
  kReleaseClears1,
  kReleaseClears2,
  kReleaseClears3,
  kReleaseClears4,
  kReleaseClears5,
  kReleaseWaves1,
  kReleaseWaves2,
  kReleaseWaves3,
  kReleaseWaves4,
  kReleaseWaves5,
  kReleaseSimultaneous1,
  kReleaseSimultaneous2,
  kReleaseSimultaneous3,
  kReleaseSimultaneous4,
  kReleaseSimultaneous5,
  kEmptyWellExists,
  kEmptyWellCount,
  kHeightOneWellExists,
  kHeightOneWellCount,
  kEscapeWellDepth,
  kDeepestEscapeWell,
  kVisibleLowDiscEscape,
  kHorizontalViable,
  kHorizontalDead,
  kHorizontalInverseCost,
  kLowDeadCaps,
  kLowDeadCapAltitude,
  kAdjacentLowDeadCaps,
  kEdgeSolidAltitude,
  kInteriorSolidAltitude,
  kEdgeCrackedAltitude,
  kInteriorCrackedAltitude,
  kSolidOneViableNeighbor,
  kSolidTwoViableNeighbors,
  kCrackedOneViableNeighbor,
  kCrackedTwoViableNeighbors,
  kOccupancy,
  kMaximumHeight,
  kMaximumHeightSquared,
  kRoughness,
  kOpenColumns,
  kNextDiscVerticalOptions,
  kImmediateScore,
  kImmediateClears,
  kImmediateReveals,
  kImmediateWaves,
  kImmediateMaximumChain,
  kLevelAdvanced,
  kClearedBoard,
  kBaseFeatureCount,
};

constexpr std::array<std::string_view, kBaseFeatureCount> kBaseFeatureNames{{
    "bias",
    "verticalViable",
    "verticalDead",
    "verticalInverseGap",
    "verticalInverseGapSquared",
    "sameTargetPairs",
    "sameTargetQuadratic",
    "sameTargetCubic",
    "bestSameTargetMultiplicity",
    "releaseClearsAdd1",
    "releaseClearsAdd2",
    "releaseClearsAdd3",
    "releaseClearsAdd4",
    "releaseClearsAdd5",
    "releaseWavesAdd1",
    "releaseWavesAdd2",
    "releaseWavesAdd3",
    "releaseWavesAdd4",
    "releaseWavesAdd5",
    "releaseSimultaneousAdd1",
    "releaseSimultaneousAdd2",
    "releaseSimultaneousAdd3",
    "releaseSimultaneousAdd4",
    "releaseSimultaneousAdd5",
    "emptyWellExists",
    "emptyWellCount",
    "heightOneWellExists",
    "heightOneWellCount",
    "escapeWellDepth",
    "deepestEscapeWell",
    "visibleLowDiscEscape",
    "horizontalViable",
    "horizontalDead",
    "horizontalInverseCost",
    "lowDeadCaps",
    "lowDeadCapAltitude",
    "adjacentLowDeadCaps",
    "edgeSolidAltitude",
    "interiorSolidAltitude",
    "edgeCrackedAltitude",
    "interiorCrackedAltitude",
    "solidOneViableNeighbor",
    "solidTwoViableNeighbors",
    "crackedOneViableNeighbor",
    "crackedTwoViableNeighbors",
    "occupancy",
    "maximumHeight",
    "maximumHeightSquared",
    "roughness",
    "openColumns",
    "nextDiscVerticalOptions",
    "immediateScore",
    "immediateClears",
    "immediateReveals",
    "immediateWaves",
    "immediateMaximumChain",
    "levelAdvanced",
    "clearedBoard",
}};

enum PhaseComposite : int {
  kPhaseBias,
  kPhaseReservoir,
  kPhaseRelease,
  kPhaseEscape,
  kPhaseSafety,
  kPhaseImmediate,
  kPhaseCompositeCount,
};

constexpr std::array<std::string_view, kPhaseCompositeCount>
    kPhaseCompositeNames{{"bias", "reservoir", "release", "escape",
                          "safety", "immediate"}};

constexpr int kModelSize =
    kBaseFeatureCount + kMovesPerLevel * kPhaseCompositeCount;
using BaseFeatures = std::array<double, kBaseFeatureCount>;
using PhaseComposites = std::array<double, kPhaseCompositeCount>;
using Model = std::array<double, kModelSize>;

static_assert(kBaseFeatureCount == 58);
static_assert(kModelSize == 88);
static_assert(sizeof(Model) < 1024);

struct Options {
  std::string output = "/tmp/drop7-vertical-reservoir-policy.json";
  std::string checkpoint = "/tmp/drop7-vertical-reservoir-policy.bin";
  int threads = kDefaultThreads;
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string argument = argv[index];
    if (argument == "--output") {
      result.output = argv[index + 1];
    } else if (argument == "--checkpoint") {
      result.checkpoint = argv[index + 1];
    } else if (argument == "--threads") {
      result.threads = std::stoi(argv[index + 1]);
      if (result.threads < 1 || result.threads > 16) {
        throw std::invalid_argument("threads must be in [1,16]");
      }
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  return result;
}

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
    throw std::invalid_argument("invalid public reservoir state");
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
    throw std::runtime_error("vertical reservoir exceeded 256 MiB RSS cap");
  }
}

struct Deadline {
  Clock::time_point started = Clock::now();

  void check() const {
    if (elapsedSeconds() > kWallLimitSeconds) {
      throw std::runtime_error("vertical reservoir exceeded 30 minute cap");
    }
  }

  double elapsedSeconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }
};

struct SplitMix64 {
  explicit SplitMix64(std::uint64_t seed) : state(seed) {}

  std::uint64_t bits() {
    std::uint64_t value = (state += 0x9e37'79b9'7f4a'7c15ull);
    value = (value ^ (value >> 30u)) * 0xbf58'476d'1ce4'e5b9ull;
    value = (value ^ (value >> 27u)) * 0x94d0'49bb'1331'11ebull;
    return value ^ (value >> 31u);
  }

  double unit() {
    return static_cast<double>(bits() >> 11u) /
           static_cast<double>(std::uint64_t{1} << 53u);
  }

  double normal() {
    if (has_spare) {
      has_spare = false;
      return spare;
    }
    const double first = std::max(unit(), std::numeric_limits<double>::min());
    const double second = unit();
    const double radius = std::sqrt(-2.0 * std::log(first));
    const double angle = 6.283185307179586476925286766559 * second;
    spare = radius * std::sin(angle);
    has_spare = true;
    return radius * std::cos(angle);
  }

  std::uint64_t state = 0;
  bool has_spare = false;
  double spare = 0.0;
};

struct ReleaseResult {
  int clears = 0;
  int waves = 0;
  int simultaneous_extra = 0;
};

ReleaseResult inertRelease(const Board& board, int column, int additions) {
  if (column < 0 || column >= kBoardSize || additions < 1 || additions > 5) {
    throw std::invalid_argument("invalid inert release request");
  }
  std::array<std::uint8_t, kBoardSize> live{};
  int live_count = 0;
  for (int row = kBoardSize - 1; row >= 0; --row) {
    const std::uint8_t cell = board[indexOf(row, column)];
    if (cell != kEmpty) live[live_count++] = cell;
  }
  if (live_count + additions > kBoardSize) return {};
  for (int offset = 0; offset < additions; ++offset) {
    live[live_count++] = kSolid;
  }
  ReleaseResult result;
  for (;;) {
    int popping = 0;
    for (int index = 0; index < live_count; ++index) {
      popping += isNumbered(live[index]) && live[index] == live_count;
    }
    if (popping == 0) break;
    ++result.waves;
    result.clears += popping;
    result.simultaneous_extra += std::max(0, popping - 1);
    int retained = 0;
    for (int index = 0; index < live_count; ++index) {
      const std::uint8_t cell = live[index];
      if (isNumbered(cell) && cell == live_count) continue;
      live[retained++] = cell;
    }
    live_count = retained;
  }
  return result;
}

struct CellViability {
  std::array<bool, kCellCount> vertical{};
  std::array<bool, kCellCount> horizontal{};
  std::array<int, kCellCount> horizontal_cost{};
};

int horizontalAdditionCost(const Board& board,
                           const std::array<int, kBoardSize>& heights,
                           int row, int column, int value) {
  const int length = lineLength(board, row, column, false);
  if (value <= length) return -1;
  int start = column;
  int end = column;
  while (start > 0 && board[indexOf(row, start - 1)] != kEmpty) --start;
  while (end + 1 < kBoardSize &&
         board[indexOf(row, end + 1)] != kEmpty) {
    ++end;
  }
  const int needed = value - length;
  int best = std::numeric_limits<int>::max();
  const int target_height = kBoardSize - row;
  for (int left = 0; left <= needed; ++left) {
    const int right = needed - left;
    if (start - left < 0 || end + right >= kBoardSize) continue;
    int cost = 0;
    bool valid = true;
    for (int offset = 1; offset <= left; ++offset) {
      const int next_column = start - offset;
      if (board[indexOf(row, next_column)] != kEmpty ||
          heights[next_column] >= target_height) {
        valid = false;
        break;
      }
      cost += target_height - heights[next_column];
    }
    for (int offset = 1; valid && offset <= right; ++offset) {
      const int next_column = end + offset;
      if (board[indexOf(row, next_column)] != kEmpty ||
          heights[next_column] >= target_height) {
        valid = false;
        break;
      }
      cost += target_height - heights[next_column];
    }
    if (valid) best = std::min(best, cost);
  }
  return best <= kMovesPerLevel ? best : -1;
}

CellViability analyzeViability(
    const Board& board, const std::array<int, kBoardSize>& heights) {
  CellViability result;
  result.horizontal_cost.fill(-1);
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      const int index = indexOf(row, column);
      const int value = board[index];
      if (!isNumbered(static_cast<std::uint8_t>(value))) continue;
      result.vertical[index] = value > heights[column];
      const int cost =
          horizontalAdditionCost(board, heights, row, column, value);
      result.horizontal_cost[index] = cost;
      result.horizontal[index] = cost >= 1;
    }
  }
  return result;
}

BaseFeatures extractBaseFeatures(const MoveResult& move) {
  const PublicState state = publicState(move.state);
  BaseFeatures result{};
  result[kBias] = 1.0;
  const auto heights = detail::columnHeights(state.board);
  const CellViability viability = analyzeViability(state.board, heights);

  int vertical_viable = 0;
  int vertical_dead = 0;
  double inverse_gap = 0.0;
  double inverse_gap_squared = 0.0;
  double pair_mass = 0.0;
  double quadratic_mass = 0.0;
  double cubic_mass = 0.0;
  int best_multiplicity = 0;
  int horizontal_viable = 0;
  int horizontal_dead = 0;
  double horizontal_inverse = 0.0;
  int occupancy = 0;
  int maximum_height = 0;
  int roughness = 0;
  int open_columns = 0;
  int empty_wells = 0;
  int height_one_wells = 0;
  int escape_depth = 0;
  int deepest_escape = 0;
  int next_vertical_options = 0;
  int low_dead_caps = 0;
  int low_dead_cap_altitude = 0;
  std::array<bool, kBoardSize> low_dead_cap_column{};
  std::array<std::array<int, kBoardSize + 1>, kBoardSize> targets{};

  for (int column = 0; column < kBoardSize; ++column) {
    const int height = heights[column];
    occupancy += height;
    maximum_height = std::max(maximum_height, height);
    open_columns += height < kBoardSize;
    empty_wells += height == 0;
    height_one_wells += height == 1;
    if (height <= 1) {
      const int depth = kBoardSize - height;
      escape_depth += depth;
      deepest_escape = std::max(deepest_escape, depth);
    }
    next_vertical_options +=
        height < kBoardSize && height + 1 == state.next_disc;
    if (column > 0) roughness += std::abs(height - heights[column - 1]);

    for (int row = 0; row < kBoardSize; ++row) {
      const int index = indexOf(row, column);
      const std::uint8_t cell = state.board[index];
      if (!isNumbered(cell)) continue;
      if (viability.vertical[index]) {
        ++vertical_viable;
        const int gap = static_cast<int>(cell) - height;
        inverse_gap += 1.0 / gap;
        inverse_gap_squared += 1.0 / static_cast<double>(gap * gap);
        ++targets[column][cell];
      } else {
        ++vertical_dead;
      }
      if (viability.horizontal[index]) {
        ++horizontal_viable;
        horizontal_inverse +=
            1.0 / static_cast<double>(viability.horizontal_cost[index]);
      } else {
        ++horizontal_dead;
      }
    }
    for (int target = 1; target <= kBoardSize; ++target) {
      const int count = targets[column][target];
      pair_mass += count * (count - 1) / 2.0;
      quadratic_mass += count * count;
      cubic_mass += count * count * count;
      best_multiplicity = std::max(best_multiplicity, count);
    }
    if (height > 0) {
      const int top_row = kBoardSize - height;
      const int top_index = indexOf(top_row, column);
      const std::uint8_t cap = state.board[top_index];
      if ((cap == 1 || cap == 2) && !viability.vertical[top_index] &&
          !viability.horizontal[top_index]) {
        ++low_dead_caps;
        low_dead_cap_altitude += height;
        low_dead_cap_column[column] = true;
      }
    }
  }
  int adjacent_low_caps = 0;
  for (int column = 1; column < kBoardSize; ++column) {
    adjacent_low_caps +=
        low_dead_cap_column[column - 1] && low_dead_cap_column[column];
  }

  result[kVerticalViable] = vertical_viable / 49.0;
  result[kVerticalDead] = vertical_dead / 49.0;
  result[kVerticalInverseGap] = inverse_gap / 49.0;
  result[kVerticalInverseGapSquared] = inverse_gap_squared / 49.0;
  result[kSameTargetPairs] = pair_mass / 49.0;
  result[kSameTargetQuadratic] = quadratic_mass / 343.0;
  result[kSameTargetCubic] = cubic_mass / 2'401.0;
  result[kBestSameTargetMultiplicity] = best_multiplicity / 7.0;

  for (int additions = 1; additions <= 5; ++additions) {
    int clears = 0;
    int waves = 0;
    int simultaneous = 0;
    for (int column = 0; column < kBoardSize; ++column) {
      const ReleaseResult release =
          inertRelease(state.board, column, additions);
      clears += release.clears;
      waves += release.waves;
      simultaneous += release.simultaneous_extra;
    }
    result[kReleaseClears1 + additions - 1] = clears / 49.0;
    result[kReleaseWaves1 + additions - 1] = waves / 49.0;
    result[kReleaseSimultaneous1 + additions - 1] = simultaneous / 49.0;
  }

  result[kEmptyWellExists] = empty_wells > 0 ? 1.0 : 0.0;
  result[kEmptyWellCount] = empty_wells / 7.0;
  result[kHeightOneWellExists] = height_one_wells > 0 ? 1.0 : 0.0;
  result[kHeightOneWellCount] = height_one_wells / 7.0;
  result[kEscapeWellDepth] = escape_depth / 49.0;
  result[kDeepestEscapeWell] = deepest_escape / 7.0;
  if (state.next_disc == 1) {
    result[kVisibleLowDiscEscape] = empty_wells / 7.0;
  } else if (state.next_disc == 2) {
    result[kVisibleLowDiscEscape] = height_one_wells / 7.0;
  }
  result[kHorizontalViable] = horizontal_viable / 49.0;
  result[kHorizontalDead] = horizontal_dead / 49.0;
  result[kHorizontalInverseCost] = horizontal_inverse / 49.0;
  result[kLowDeadCaps] = low_dead_caps / 7.0;
  result[kLowDeadCapAltitude] = low_dead_cap_altitude / 49.0;
  result[kAdjacentLowDeadCaps] = adjacent_low_caps / 6.0;

  int edge_solid_altitude = 0;
  int interior_solid_altitude = 0;
  int edge_cracked_altitude = 0;
  int interior_cracked_altitude = 0;
  int solid_one = 0;
  int solid_two = 0;
  int cracked_one = 0;
  int cracked_two = 0;
  constexpr std::array<std::array<int, 2>, 4> directions{{
      {{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}},
  }};
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      const int index = indexOf(row, column);
      const std::uint8_t cell = state.board[index];
      if (cell != kSolid && cell != kCracked) continue;
      const bool edge = column == 0 || column == kBoardSize - 1;
      const int altitude = kBoardSize - row;
      if (cell == kSolid && edge) edge_solid_altitude += altitude;
      if (cell == kSolid && !edge) interior_solid_altitude += altitude;
      if (cell == kCracked && edge) edge_cracked_altitude += altitude;
      if (cell == kCracked && !edge) interior_cracked_altitude += altitude;
      int viable_neighbors = 0;
      for (const auto& direction : directions) {
        const int neighbor_row = row + direction[0];
        const int neighbor_column = column + direction[1];
        if (!inside(neighbor_row, neighbor_column)) continue;
        const int neighbor = indexOf(neighbor_row, neighbor_column);
        viable_neighbors += isNumbered(state.board[neighbor]) &&
                            (viability.vertical[neighbor] ||
                             viability.horizontal[neighbor]);
      }
      if (cell == kSolid) {
        solid_one += viable_neighbors >= 1;
        solid_two += viable_neighbors >= 2;
      } else {
        cracked_one += viable_neighbors >= 1;
        cracked_two += viable_neighbors >= 2;
      }
    }
  }
  result[kEdgeSolidAltitude] = edge_solid_altitude / 56.0;
  result[kInteriorSolidAltitude] = interior_solid_altitude / 140.0;
  result[kEdgeCrackedAltitude] = edge_cracked_altitude / 56.0;
  result[kInteriorCrackedAltitude] = interior_cracked_altitude / 140.0;
  result[kSolidOneViableNeighbor] = solid_one / 49.0;
  result[kSolidTwoViableNeighbors] = solid_two / 49.0;
  result[kCrackedOneViableNeighbor] = cracked_one / 49.0;
  result[kCrackedTwoViableNeighbors] = cracked_two / 49.0;

  result[kOccupancy] = occupancy / 49.0;
  result[kMaximumHeight] = maximum_height / 7.0;
  result[kMaximumHeightSquared] = maximum_height * maximum_height / 49.0;
  result[kRoughness] = roughness / 42.0;
  result[kOpenColumns] = open_columns / 7.0;
  result[kNextDiscVerticalOptions] = next_vertical_options / 7.0;

  int clears = 0;
  int reveals = 0;
  int maximum_chain = 0;
  for (const Wave& wave : move.waves) {
    clears += wave.cleared;
    reveals += wave.revealed;
    maximum_chain = std::max(maximum_chain, wave.depth);
  }
  result[kImmediateScore] = static_cast<double>(move.score_delta) / 17'000.0;
  result[kImmediateClears] = clears / 20.0;
  result[kImmediateReveals] = reveals / 10.0;
  result[kImmediateWaves] = move.waves.size() / 8.0;
  result[kImmediateMaximumChain] = maximum_chain / 8.0;
  result[kLevelAdvanced] = move.level_advanced ? 1.0 : 0.0;
  result[kClearedBoard] = move.cleared_board ? 1.0 : 0.0;
  return result;
}

PhaseComposites phaseComposites(const BaseFeatures& features) {
  PhaseComposites result{};
  result[kPhaseBias] = 1.0;
  result[kPhaseReservoir] =
      features[kVerticalViable] + features[kVerticalInverseGap] +
      features[kVerticalInverseGapSquared] + 2.0 * features[kSameTargetPairs] +
      features[kSameTargetQuadratic] + features[kSameTargetCubic] +
      features[kBestSameTargetMultiplicity] - features[kVerticalDead];
  for (int additions = 1; additions <= 5; ++additions) {
    const double readiness = 1.0 / additions;
    result[kPhaseRelease] +=
        readiness * (features[kReleaseClears1 + additions - 1] +
                     features[kReleaseWaves1 + additions - 1] +
                     features[kReleaseSimultaneous1 + additions - 1]);
  }
  result[kPhaseEscape] =
      features[kEmptyWellExists] + features[kEmptyWellCount] +
      features[kHeightOneWellExists] + features[kHeightOneWellCount] +
      features[kEscapeWellDepth] + features[kDeepestEscapeWell] +
      2.0 * features[kVisibleLowDiscEscape];
  result[kPhaseSafety] =
      features[kOpenColumns] + features[kEscapeWellDepth] -
      features[kMaximumHeightSquared] - features[kLowDeadCaps] -
      features[kLowDeadCapAltitude] - features[kAdjacentLowDeadCaps] -
      features[kEdgeSolidAltitude] - features[kInteriorSolidAltitude] -
      0.7 * features[kEdgeCrackedAltitude] -
      0.7 * features[kInteriorCrackedAltitude];
  result[kPhaseImmediate] =
      features[kImmediateScore] + features[kImmediateClears] +
      features[kImmediateReveals] + features[kImmediateWaves] +
      features[kImmediateMaximumChain];
  return result;
}

Model initialModel() {
  Model result{};
  result[kVerticalViable] = 3.0;
  result[kVerticalDead] = -3.0;
  result[kVerticalInverseGap] = 5.0;
  result[kVerticalInverseGapSquared] = 3.0;
  result[kSameTargetPairs] = 8.0;
  result[kSameTargetQuadratic] = 5.0;
  result[kSameTargetCubic] = 4.0;
  result[kBestSameTargetMultiplicity] = 4.0;
  for (int additions = 1; additions <= 5; ++additions) {
    const double readiness = 1.0 / additions;
    result[kReleaseClears1 + additions - 1] = 8.0 * readiness;
    result[kReleaseWaves1 + additions - 1] = 10.0 * readiness;
    result[kReleaseSimultaneous1 + additions - 1] = 12.0 * readiness;
  }
  result[kEmptyWellExists] = 4.0;
  result[kEmptyWellCount] = 4.0;
  result[kHeightOneWellExists] = 3.0;
  result[kHeightOneWellCount] = 3.0;
  result[kEscapeWellDepth] = 5.0;
  result[kDeepestEscapeWell] = 3.0;
  result[kVisibleLowDiscEscape] = 8.0;
  result[kHorizontalViable] = 2.0;
  result[kHorizontalDead] = -2.0;
  result[kHorizontalInverseCost] = 3.0;
  result[kLowDeadCaps] = -8.0;
  result[kLowDeadCapAltitude] = -8.0;
  result[kAdjacentLowDeadCaps] = -10.0;
  result[kEdgeSolidAltitude] = -5.0;
  result[kInteriorSolidAltitude] = -3.0;
  result[kEdgeCrackedAltitude] = -3.0;
  result[kInteriorCrackedAltitude] = -2.0;
  result[kSolidOneViableNeighbor] = 2.0;
  result[kSolidTwoViableNeighbors] = 5.0;
  result[kCrackedOneViableNeighbor] = 3.0;
  result[kCrackedTwoViableNeighbors] = 6.0;
  result[kOccupancy] = -2.0;
  result[kMaximumHeight] = -5.0;
  result[kMaximumHeightSquared] = -10.0;
  result[kRoughness] = -2.0;
  result[kOpenColumns] = 4.0;
  result[kNextDiscVerticalOptions] = 4.0;
  result[kImmediateScore] = 3.0;
  result[kImmediateClears] = 2.0;
  result[kImmediateReveals] = 3.0;
  result[kImmediateWaves] = 2.0;
  result[kImmediateMaximumChain] = 2.0;
  result[kLevelAdvanced] = 2.0;
  result[kClearedBoard] = 8.0;

  constexpr std::array<std::array<double, kPhaseCompositeCount>,
                       kMovesPerLevel>
      phase{{
          {{0.0, 1.0, 3.0, 2.0, 3.0, 2.0}},
          {{0.0, 2.0, 2.0, 2.0, 2.0, 1.5}},
          {{0.0, 3.0, 1.0, 2.0, 1.5, 1.0}},
          {{0.0, 4.0, 0.5, 2.0, 1.0, 0.8}},
          {{0.0, 5.0, 0.2, 2.0, 0.8, 0.5}},
      }};
  for (int moves_remaining = 1; moves_remaining <= kMovesPerLevel;
       ++moves_remaining) {
    for (int feature = 0; feature < kPhaseCompositeCount; ++feature) {
      const int index = kBaseFeatureCount +
                        (moves_remaining - 1) * kPhaseCompositeCount + feature;
      result[index] = phase[moves_remaining - 1][feature];
    }
  }
  return result;
}

Model initialSigma(const Model& mean) {
  Model result{};
  for (int index = 0; index < kModelSize; ++index) {
    result[index] = std::clamp(std::abs(mean[index]) * 0.35, 0.50, 3.0);
  }
  return result;
}

double scoreFeatures(const Model& model, const BaseFeatures& features,
                     int decision_phase) {
  if (decision_phase < 1 || decision_phase > kMovesPerLevel) {
    throw std::invalid_argument("invalid reservoir decision phase");
  }
  double result = 0.0;
  for (int index = 0; index < kBaseFeatureCount; ++index) {
    result += model[index] * features[index];
  }
  const PhaseComposites composites = phaseComposites(features);
  const int phase_offset =
      kBaseFeatureCount + (decision_phase - 1) * kPhaseCompositeCount;
  for (int index = 0; index < kPhaseCompositeCount; ++index) {
    result += model[phase_offset + index] * composites[index];
  }
  if (!std::isfinite(result)) {
    throw std::runtime_error("reservoir evaluator returned non-finite value");
  }
  return result;
}

struct PolicyMetrics {
  std::uint64_t decisions = 0;
  std::uint64_t sampled_transitions = 0;
  std::uint64_t feature_evaluations = 0;

  bool operator==(const PolicyMetrics&) const = default;
};

struct Decision {
  int action = -1;
  std::array<double, kBoardSize> values{};
  PolicyMetrics metrics{};

  bool operator==(const Decision&) const = default;
};

Decision chooseAction(const PublicState& source, const Model& model) {
  Decision result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  if (source.terminal) return result;
  bool mirrored = false;
  const State canonical_state = detail::canonicalState(materialize(source),
                                                        mirrored);
  const PublicState canonical = publicState(canonical_state);
  const std::uint32_t state_seed = detail::scenarioSeedForState(
      canonical_state, kPolicySeed, 1);
  int selected = -1;
  double best = -std::numeric_limits<double>::infinity();
  for (const int action : kColumnOrder) {
    if (!isLegal(canonical.board, action)) continue;
    double value = 0.0;
    for (int sample = 0; sample < kSuccessorSamples; ++sample) {
      detail::StratifiedRandom random{
          state_seed, sample, kSuccessorSamples, 0};
      MoveResult move;
      if (!detail::playMoveSampled(canonical_state, action, random, move)) {
        throw std::runtime_error("sampled successor rejected legal action");
      }
      ++result.metrics.sampled_transitions;
      double sample_value =
          static_cast<double>(move.score_delta) / 17'000.0;
      if (move.state.game_over) {
        sample_value += kTerminalValue;
      } else {
        move.state.score = 0;
        move.state.level = 1;
        move.state.moves_played = 0;
        move.state.next_disc = detail::sampledNextDisc(
            state_seed, sample, kSuccessorSamples);
        sample_value += scoreFeatures(
            model, extractBaseFeatures(move), canonical.moves_remaining);
        ++result.metrics.feature_evaluations;
      }
      value += sample_value / kSuccessorSamples;
    }
    result.values[action] = value;
    if (value > best) {
      best = value;
      selected = action;
    }
  }
  if (selected < 0) selected = centerFirstMove(canonical.board);
  result.metrics.decisions = 1;
  if (!mirrored) {
    result.action = selected;
    return result;
  }
  std::array<double, kBoardSize> source_values{};
  for (int column = 0; column < kBoardSize; ++column) {
    source_values[kBoardSize - 1 - column] = result.values[column];
  }
  result.values = source_values;
  result.action = kBoardSize - 1 - selected;
  return result;
}

using PublicPolicy = Decision (*)(const PublicState&, const Model&);
static_assert(std::is_same_v<decltype(&chooseAction), PublicPolicy>);
static_assert(!std::is_invocable_v<PublicPolicy, const State&, const Model&>);

struct FairD1Decision {
  int action = -1;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  bool complete = false;
};

FairD1Decision chooseFairDepthOne(const PublicState& source) {
  FairD1Decision result;
  if (source.terminal) return result;
  bool mirrored = false;
  const State canonical = detail::canonicalState(materialize(source), mirrored);
  fair::SearchContext context;
  const fair::RootEvaluation root = fair::rootDecision(canonical, 1, context);
  int legal = 0;
  int evaluated = 0;
  for (int action = 0; action < kBoardSize; ++action) {
    legal += isLegal(canonical.board, action);
    evaluated += std::isfinite(root.values[action]);
  }
  if (root.action < 0 || legal != evaluated || context.work > 70 ||
      !context.cache.empty()) {
    throw std::runtime_error("fair D1 failed exact full-width completion");
  }
  result.action = mirrored ? kBoardSize - 1 - root.action : root.action;
  result.work = context.work;
  result.nodes = context.nodes;
  result.complete = true;
  return result;
}

enum class SeedUse { kTraining, kTournament, kProbe };

bool allowedSeed(std::uint32_t seed, SeedUse use) {
  std::uint32_t start = 0;
  std::uint32_t end = 0;
  if (use == SeedUse::kTraining) {
    start = kTrainingSeedStart;
    end = kTrainingSeedStart + kGenerations * kBatchGames;
  } else if (use == SeedUse::kTournament) {
    start = kTournamentSeedStart;
    end = kTournamentSeedStart + kTournamentGames;
  } else {
    start = kProbeSeedStart;
    end = kProbeSeedStart + kProbeGames;
  }
  return seed >= start && seed < end && (seed >> 24u) != 0x7du &&
         (seed >> 24u) != 0xd7u;
}

void requireSeed(std::uint32_t seed, SeedUse use) {
  if (!allowedSeed(seed, use)) {
    throw std::invalid_argument("game seed outside vertical reservoir allowlist");
  }
}

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
  int maximum_chain = 0;
  PolicyMetrics policy{};
  std::uint64_t fair_work = 0;
  std::uint64_t fair_nodes = 0;
  std::uint64_t disc_stream_hash = 0;
};

std::uint64_t mix64(std::uint64_t value) {
  value ^= value >> 30u;
  value *= 0xbf58'476d'1ce4'e5b9ull;
  value ^= value >> 27u;
  value *= 0x94d0'49bb'1331'11ebull;
  return value ^ (value >> 31u);
}

std::uint64_t discStreamHash(std::uint32_t seed) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (int move = 0; move < kMaximumMoves; ++move) {
    hash ^= headlessDisc(seed, move);
    hash *= 0x0000'0100'0000'01b3ull;
  }
  return mix64(hash);
}

enum class Policy { kReservoir, kFairD1 };

GameResult playGame(std::uint32_t seed, SeedUse seed_use, Policy policy,
                    const Model* model, const Deadline& deadline) {
  requireSeed(seed, seed_use);
  if (policy == Policy::kReservoir && model == nullptr) {
    throw std::invalid_argument("reservoir game missing model");
  }
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  result.disc_stream_hash = discStreamHash(seed);
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    deadline.check();
    if (state.next_disc != headlessDisc(seed, state.moves_played)) {
      throw std::runtime_error("headless visible-disc guard failed");
    }
    int action = -1;
    if (policy == Policy::kReservoir) {
      const Decision decision = chooseAction(publicState(state), *model);
      action = decision.action;
      result.policy.decisions += decision.metrics.decisions;
      result.policy.sampled_transitions +=
          decision.metrics.sampled_transitions;
      result.policy.feature_evaluations +=
          decision.metrics.feature_evaluations;
    } else {
      const FairD1Decision decision = chooseFairDepthOne(publicState(state));
      if (!decision.complete) {
        throw std::runtime_error("fair D1 game decision incomplete");
      }
      action = decision.action;
      result.fair_work += decision.work;
      result.fair_nodes += decision.nodes;
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("game policy selected illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("headless transition rejected legal action");
    }
    for (const Wave& wave : move.waves) {
      result.clears += static_cast<std::uint64_t>(wave.cleared);
      result.reveals += static_cast<std::uint64_t>(wave.revealed);
      result.maximum_chain = std::max(result.maximum_chain, wave.depth);
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  return result;
}

double quantile(std::vector<double> values, double probability) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const double position = probability * (values.size() - 1.0);
  const std::size_t lower = static_cast<std::size_t>(std::floor(position));
  const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
  const double fraction = position - lower;
  return values[lower] * (1.0 - fraction) + values[upper] * fraction;
}

double bottomFractionMean(std::vector<double> values, double fraction) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const double mass = fraction * values.size();
  const int whole = static_cast<int>(std::floor(mass));
  const double partial = mass - whole;
  double sum = 0.0;
  for (int index = 0; index < whole; ++index) sum += values[index];
  if (whole < static_cast<int>(values.size())) sum += partial * values[whole];
  return sum / mass;
}

struct Evaluation {
  std::vector<GameResult> games;
  double objective = -std::numeric_limits<double>::infinity();
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double lower_quartile_score = 0.0;
  double lower_quartile_moves = 0.0;
  double bottom_quartile_mean_score = 0.0;
  double bottom_quartile_mean_moves = 0.0;
  double reveals_per_move = 0.0;
  double clears_per_move = 0.0;
  double sampled_transitions_per_move = 0.0;
  int natural = 0;
  int censored = 0;
  std::uint64_t total_moves = 0;
  std::uint64_t total_clears = 0;
  std::uint64_t total_reveals = 0;
  std::uint64_t sampled_transitions = 0;
  std::uint64_t feature_evaluations = 0;
  std::uint64_t fair_work = 0;
  std::uint64_t fair_nodes = 0;
  double seconds = 0.0;
};

void summarize(Evaluation& evaluation) {
  if (evaluation.games.empty()) throw std::invalid_argument("empty evaluation");
  std::vector<double> scores;
  std::vector<double> moves;
  for (const GameResult& game : evaluation.games) {
    evaluation.mean_score +=
        static_cast<double>(game.score) / evaluation.games.size();
    evaluation.mean_moves +=
        static_cast<double>(game.moves) / evaluation.games.size();
    evaluation.natural += !game.censored;
    evaluation.censored += game.censored;
    evaluation.total_moves += static_cast<std::uint64_t>(game.moves);
    evaluation.total_clears += game.clears;
    evaluation.total_reveals += game.reveals;
    evaluation.sampled_transitions += game.policy.sampled_transitions;
    evaluation.feature_evaluations += game.policy.feature_evaluations;
    evaluation.fair_work += game.fair_work;
    evaluation.fair_nodes += game.fair_nodes;
    scores.push_back(static_cast<double>(game.score));
    moves.push_back(static_cast<double>(game.moves));
  }
  evaluation.lower_quartile_score = quantile(scores, 0.25);
  evaluation.lower_quartile_moves = quantile(moves, 0.25);
  evaluation.bottom_quartile_mean_score = bottomFractionMean(scores, 0.25);
  evaluation.bottom_quartile_mean_moves = bottomFractionMean(moves, 0.25);
  if (evaluation.total_moves > 0) {
    evaluation.reveals_per_move =
        static_cast<double>(evaluation.total_reveals) / evaluation.total_moves;
    evaluation.clears_per_move =
        static_cast<double>(evaluation.total_clears) / evaluation.total_moves;
    evaluation.sampled_transitions_per_move =
        static_cast<double>(evaluation.sampled_transitions) /
        evaluation.total_moves;
  }
  const double mean_utility =
      0.5 * evaluation.mean_moves + evaluation.mean_score / 6'800.0;
  const double tail_utility =
      0.5 * evaluation.bottom_quartile_mean_moves +
      evaluation.bottom_quartile_mean_score / 6'800.0;
  evaluation.objective = 0.55 * mean_utility + 0.45 * tail_utility;
}

Evaluation evaluate(const Model* model, Policy policy, std::uint32_t start,
                    int games, SeedUse seed_use, const Deadline& deadline) {
  Evaluation result;
  result.games.reserve(static_cast<std::size_t>(games));
  const auto started = Clock::now();
  for (int game = 0; game < games; ++game) {
    result.games.push_back(playGame(
        start + static_cast<std::uint32_t>(game), seed_use, policy, model,
        deadline));
  }
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  summarize(result);
  enforceRssLimit();
  return result;
}

struct Candidate {
  Model model{};
  Evaluation evaluation{};
};

void evaluatePopulation(std::vector<Candidate>& candidates,
                        std::uint32_t start, int games, SeedUse seed_use,
                        int threads, const Deadline& deadline) {
  std::atomic<std::size_t> next{0};
  const int worker_count = std::max(
      1, std::min(threads, static_cast<int>(candidates.size())));
  std::vector<std::thread> workers;
  workers.reserve(static_cast<std::size_t>(worker_count));
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.emplace_back([&]() {
      for (;;) {
        const std::size_t index = next.fetch_add(1);
        if (index >= candidates.size()) return;
        candidates[index].evaluation = evaluate(
            &candidates[index].model, Policy::kReservoir, start, games,
            seed_use, deadline);
      }
    });
  }
  for (std::thread& worker : workers) worker.join();
}

std::uint64_t fingerprint(const Model& model) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const double value : model) {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    for (int byte = 0; byte < 8; ++byte) {
      hash ^= (bits >> (byte * 8)) & 0xffu;
      hash *= 0x0000'0100'0000'01b3ull;
    }
  }
  return hash;
}

void writeCheckpoint(std::ostream& output, const Model& model) {
  const std::uint32_t count = kModelSize;
  const std::uint64_t model_fingerprint = fingerprint(model);
  output.write(reinterpret_cast<const char*>(&kCheckpointMagic),
               sizeof(kCheckpointMagic));
  output.write(reinterpret_cast<const char*>(&kCheckpointVersion),
               sizeof(kCheckpointVersion));
  output.write(reinterpret_cast<const char*>(&count), sizeof(count));
  output.write(reinterpret_cast<const char*>(&model_fingerprint),
               sizeof(model_fingerprint));
  output.write(reinterpret_cast<const char*>(model.data()),
               static_cast<std::streamsize>(sizeof(double) * model.size()));
  if (!output) throw std::runtime_error("could not write reservoir checkpoint");
}

void saveCheckpoint(const std::string& path, const Model& model) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("could not open reservoir checkpoint");
  writeCheckpoint(output, model);
}

Model readCheckpoint(std::istream& input) {
  std::uint64_t magic = 0;
  std::uint32_t version = 0;
  std::uint32_t count = 0;
  std::uint64_t expected_fingerprint = 0;
  input.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  input.read(reinterpret_cast<char*>(&version), sizeof(version));
  input.read(reinterpret_cast<char*>(&count), sizeof(count));
  input.read(reinterpret_cast<char*>(&expected_fingerprint),
             sizeof(expected_fingerprint));
  Model result{};
  input.read(reinterpret_cast<char*>(result.data()),
             static_cast<std::streamsize>(sizeof(double) * result.size()));
  char trailing = 0;
  const bool has_trailing = static_cast<bool>(input.read(&trailing, 1));
  if (magic != kCheckpointMagic || version != kCheckpointVersion ||
      count != kModelSize || has_trailing ||
      fingerprint(result) != expected_fingerprint) {
    throw std::runtime_error("invalid reservoir checkpoint");
  }
  return result;
}

struct GenerationRecord {
  int generation = 0;
  std::uint32_t seed_start = 0;
  double best_objective = 0.0;
  double best_score = 0.0;
  double best_moves = 0.0;
  double best_lower_quartile_score = 0.0;
  double best_lower_quartile_moves = 0.0;
  double mean_objective = 0.0;
  std::uint64_t best_fingerprint = 0;
  double elapsed_seconds = 0.0;
};

struct TrainingResult {
  Model champion{};
  Evaluation champion_evaluation{};
  std::vector<GenerationRecord> generations;
  int archive_candidates = 0;
  double seconds = 0.0;
};

TrainingResult train(int threads, const Deadline& deadline) {
  const auto started = Clock::now();
  Model mean = initialModel();
  Model sigma = initialSigma(mean);
  SplitMix64 random(kOptimizerSeed);
  std::vector<Model> archive;
  archive.push_back(mean);
  TrainingResult result;
  result.generations.reserve(kGenerations);

  for (int generation = 0; generation < kGenerations; ++generation) {
    deadline.check();
    std::vector<Candidate> population(static_cast<std::size_t>(kPopulation));
    population[0].model = mean;
    for (int pair = 0; pair < (kPopulation - 1) / 2; ++pair) {
      Model positive = mean;
      Model negative = mean;
      for (int index = 0; index < kModelSize; ++index) {
        const double perturbation = sigma[index] * random.normal();
        positive[index] = std::clamp(mean[index] + perturbation, -40.0, 40.0);
        negative[index] = std::clamp(mean[index] - perturbation, -40.0, 40.0);
      }
      population[static_cast<std::size_t>(1 + pair * 2)].model = positive;
      population[static_cast<std::size_t>(2 + pair * 2)].model = negative;
    }
    const std::uint32_t seed_start =
        kTrainingSeedStart + generation * kBatchGames;
    evaluatePopulation(population, seed_start, kBatchGames, SeedUse::kTraining,
                       threads, deadline);
    const double sampled_mean_objective = population[0].evaluation.objective;
    std::stable_sort(population.begin(), population.end(),
                     [](const Candidate& first, const Candidate& second) {
                       return first.evaluation.objective >
                              second.evaluation.objective;
                     });

    Model elite_mean{};
    double rank_mass = 0.0;
    for (int rank = 0; rank < kElite; ++rank) {
      const double rank_weight = std::log(kElite + 0.5) - std::log(rank + 1.0);
      rank_mass += rank_weight;
      for (int index = 0; index < kModelSize; ++index) {
        elite_mean[index] += rank_weight * population[rank].model[index];
      }
    }
    for (double& value : elite_mean) value /= rank_mass;
    Model elite_variance{};
    for (int rank = 0; rank < kElite; ++rank) {
      const double rank_weight = std::log(kElite + 0.5) - std::log(rank + 1.0);
      for (int index = 0; index < kModelSize; ++index) {
        const double delta = population[rank].model[index] - elite_mean[index];
        elite_variance[index] += rank_weight * delta * delta;
      }
    }
    for (int index = 0; index < kModelSize; ++index) {
      mean[index] = std::clamp(0.30 * mean[index] + 0.70 * elite_mean[index],
                               -40.0, 40.0);
      const double selected_sigma =
          std::sqrt(elite_variance[index] / rank_mass + 1.0e-12);
      sigma[index] =
          std::clamp(0.35 * sigma[index] + 0.65 * selected_sigma, 0.05, 5.0);
    }
    if (generation % 2 == 1 || generation + 1 == kGenerations) {
      archive.push_back(mean);
      archive.push_back(population.front().model);
    }
    GenerationRecord record;
    record.generation = generation;
    record.seed_start = seed_start;
    record.best_objective = population.front().evaluation.objective;
    record.best_score = population.front().evaluation.mean_score;
    record.best_moves = population.front().evaluation.mean_moves;
    record.best_lower_quartile_score =
        population.front().evaluation.lower_quartile_score;
    record.best_lower_quartile_moves =
        population.front().evaluation.lower_quartile_moves;
    record.mean_objective = sampled_mean_objective;
    record.best_fingerprint = fingerprint(population.front().model);
    record.elapsed_seconds = deadline.elapsedSeconds();
    result.generations.push_back(record);
    {
      const std::lock_guard<std::mutex> lock(fair::progress_mutex);
      std::cerr << std::fixed << std::setprecision(3)
                << "reservoir generation " << generation << '/'
                << kGenerations << " seed 0x" << std::hex << seed_start
                << std::dec << " objective " << record.best_objective
                << " score " << record.best_score << " moves "
                << record.best_moves << " wall " << record.elapsed_seconds
                << '\n';
    }
  }

  std::vector<Candidate> finalists;
  finalists.reserve(archive.size());
  for (const Model& model : archive) finalists.push_back({model, {}});
  evaluatePopulation(finalists, kTournamentSeedStart, kTournamentGames,
                     SeedUse::kTournament, threads, deadline);
  std::stable_sort(finalists.begin(), finalists.end(),
                   [](const Candidate& first, const Candidate& second) {
                     return first.evaluation.objective >
                            second.evaluation.objective;
                   });
  result.champion = finalists.front().model;
  result.champion_evaluation = std::move(finalists.front().evaluation);
  result.archive_candidates = static_cast<int>(finalists.size());
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

struct DifferenceStats {
  double mean = 0.0;
  double standard_error = 0.0;
  double lower_95 = 0.0;
  int wins = 0;
  int ties = 0;
  int losses = 0;
};

DifferenceStats differences(const std::vector<double>& values,
                            double critical) {
  if (values.size() < 2) throw std::invalid_argument("too few paired games");
  DifferenceStats result;
  for (const double value : values) {
    result.mean += value / values.size();
    result.wins += value > 0.0;
    result.ties += value == 0.0;
    result.losses += value < 0.0;
  }
  double squares = 0.0;
  for (const double value : values) {
    const double centered = value - result.mean;
    squares += centered * centered;
  }
  const double deviation =
      std::sqrt(squares / static_cast<double>(values.size() - 1));
  result.standard_error = deviation / std::sqrt(values.size());
  result.lower_95 = result.mean - critical * result.standard_error;
  return result;
}

struct PairedSummary {
  DifferenceStats score;
  DifferenceStats moves;
  bool stream_hashes_match = true;
};

PairedSummary pair(const Evaluation& candidate, const Evaluation& baseline,
                   double critical) {
  if (candidate.games.size() != baseline.games.size() ||
      candidate.games.empty()) {
    throw std::invalid_argument("invalid paired evaluation");
  }
  std::vector<double> scores;
  std::vector<double> moves;
  PairedSummary result;
  for (std::size_t game = 0; game < candidate.games.size(); ++game) {
    if (candidate.games[game].seed != baseline.games[game].seed ||
        candidate.games[game].disc_stream_hash !=
            baseline.games[game].disc_stream_hash) {
      result.stream_hashes_match = false;
    }
    scores.push_back(static_cast<double>(candidate.games[game].score -
                                         baseline.games[game].score));
    moves.push_back(static_cast<double>(candidate.games[game].moves -
                                        baseline.games[game].moves));
  }
  result.score = differences(scores, critical);
  result.moves = differences(moves, critical);
  if (!result.stream_hashes_match) {
    throw std::runtime_error("paired visible-disc streams differ");
  }
  return result;
}

struct FittingGate {
  double score_ratio = 0.0;
  double move_ratio = 0.0;
  bool minimum_score = false;
  bool minimum_moves = false;
  bool score_ratio_passed = false;
  bool move_ratio_passed = false;
  bool lower_quartile_score = false;
  bool lower_quartile_moves = false;
  bool reveals_per_move = false;
  bool passed = false;
};

FittingGate fittingGate(const Evaluation& candidate,
                        const Evaluation& baseline) {
  FittingGate result;
  result.score_ratio = candidate.mean_score / baseline.mean_score;
  result.move_ratio = candidate.mean_moves / baseline.mean_moves;
  result.minimum_score = candidate.mean_score >= kFittingMinimumScore;
  result.minimum_moves = candidate.mean_moves >= kFittingMinimumMoves;
  result.score_ratio_passed = result.score_ratio >= kFittingRatio;
  result.move_ratio_passed = result.move_ratio >= kFittingRatio;
  result.lower_quartile_score =
      candidate.lower_quartile_score >= baseline.lower_quartile_score;
  result.lower_quartile_moves =
      candidate.lower_quartile_moves >= baseline.lower_quartile_moves;
  result.reveals_per_move =
      candidate.reveals_per_move >= baseline.reveals_per_move;
  result.passed = result.minimum_score && result.minimum_moves &&
                  result.score_ratio_passed && result.move_ratio_passed &&
                  result.lower_quartile_score && result.lower_quartile_moves &&
                  result.reveals_per_move;
  return result;
}

struct ProbeGate {
  bool score_lower_95_positive = false;
  bool move_lower_95_positive = false;
  bool minimum_moves = false;
  bool passed = false;
};

ProbeGate probeGate(const Evaluation& candidate, const PairedSummary& paired) {
  ProbeGate result;
  result.score_lower_95_positive = paired.score.lower_95 > 0.0;
  result.move_lower_95_positive = paired.moves.lower_95 > 0.0;
  result.minimum_moves = candidate.mean_moves >= kProbeMinimumMoves;
  result.passed = result.score_lower_95_positive &&
                  result.move_lower_95_positive && result.minimum_moves;
  return result;
}

std::string hex64(std::uint64_t value) {
  std::ostringstream output;
  output << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
  return output.str();
}

void writeGame(std::ostream& output, const GameResult& game) {
  output << "{\"seed\":" << game.seed << ",\"score\":" << game.score
         << ",\"moves\":" << game.moves << ",\"natural\":"
         << (!game.censored ? "true" : "false") << ",\"censored\":"
         << (game.censored ? "true" : "false") << ",\"clears\":"
         << game.clears << ",\"reveals\":" << game.reveals
         << ",\"maximumChain\":" << game.maximum_chain
         << ",\"sampledTransitions\":"
         << game.policy.sampled_transitions << ",\"fairWork\":"
         << game.fair_work << ",\"fairNodes\":" << game.fair_nodes
         << ",\"discStreamHash\":\"" << hex64(game.disc_stream_hash)
         << "\"}";
}

void writeEvaluation(std::ostream& output, const Evaluation& evaluation,
                     bool games) {
  output << "{\"games\":" << evaluation.games.size()
         << ",\"natural\":" << evaluation.natural
         << ",\"censored\":" << evaluation.censored
         << ",\"objective\":" << evaluation.objective
         << ",\"meanScore\":" << evaluation.mean_score
         << ",\"meanMoves\":" << evaluation.mean_moves
         << ",\"lowerQuartileScore\":" << evaluation.lower_quartile_score
         << ",\"lowerQuartileMoves\":" << evaluation.lower_quartile_moves
         << ",\"bottomQuartileMeanScore\":"
         << evaluation.bottom_quartile_mean_score
         << ",\"bottomQuartileMeanMoves\":"
         << evaluation.bottom_quartile_mean_moves
         << ",\"revealsPerMove\":" << evaluation.reveals_per_move
         << ",\"clearsPerMove\":" << evaluation.clears_per_move
         << ",\"sampledTransitionsPerMove\":"
         << evaluation.sampled_transitions_per_move
         << ",\"sampledTransitions\":" << evaluation.sampled_transitions
         << ",\"featureEvaluations\":" << evaluation.feature_evaluations
         << ",\"fairWork\":" << evaluation.fair_work
         << ",\"fairNodes\":" << evaluation.fair_nodes
         << ",\"seconds\":" << evaluation.seconds;
  if (games) {
    output << ",\"results\":[";
    for (std::size_t index = 0; index < evaluation.games.size(); ++index) {
      if (index != 0) output << ',';
      writeGame(output, evaluation.games[index]);
    }
    output << ']';
  }
  output << '}';
}

void writeDifference(std::ostream& output, const DifferenceStats& difference) {
  output << "{\"mean\":" << difference.mean
         << ",\"standardError\":" << difference.standard_error
         << ",\"lower95\":" << difference.lower_95 << ",\"wins\":"
         << difference.wins << ",\"ties\":" << difference.ties
         << ",\"losses\":" << difference.losses << '}';
}

void writePaired(std::ostream& output, const PairedSummary& paired) {
  output << "{\"score\":";
  writeDifference(output, paired.score);
  output << ",\"moves\":";
  writeDifference(output, paired.moves);
  output << ",\"streamHashesMatch\":"
         << (paired.stream_hashes_match ? "true" : "false") << '}';
}

void writeFittingGate(std::ostream& output, const FittingGate& gate) {
  output << "{\"scoreRatio\":" << gate.score_ratio
         << ",\"moveRatio\":" << gate.move_ratio
         << ",\"minimumScore\":" << (gate.minimum_score ? "true" : "false")
         << ",\"minimumMoves\":" << (gate.minimum_moves ? "true" : "false")
         << ",\"scoreRatioPassed\":"
         << (gate.score_ratio_passed ? "true" : "false")
         << ",\"moveRatioPassed\":"
         << (gate.move_ratio_passed ? "true" : "false")
         << ",\"lowerQuartileScoreNonRegression\":"
         << (gate.lower_quartile_score ? "true" : "false")
         << ",\"lowerQuartileMovesNonRegression\":"
         << (gate.lower_quartile_moves ? "true" : "false")
         << ",\"revealsPerMoveNonRegression\":"
         << (gate.reveals_per_move ? "true" : "false")
         << ",\"passed\":" << (gate.passed ? "true" : "false") << '}';
}

void writeModel(std::ostream& output, const Model& model) {
  output << "{\"fingerprint\":\"" << hex64(fingerprint(model))
         << "\",\"global\":{";
  for (int index = 0; index < kBaseFeatureCount; ++index) {
    if (index != 0) output << ',';
    output << '\"' << kBaseFeatureNames[index] << "\":" << model[index];
  }
  output << "},\"phase\":{";
  bool first = true;
  for (int phase = 1; phase <= kMovesPerLevel; ++phase) {
    for (int feature = 0; feature < kPhaseCompositeCount; ++feature) {
      if (!first) output << ',';
      first = false;
      const int index = kBaseFeatureCount +
                        (phase - 1) * kPhaseCompositeCount + feature;
      output << '\"' << kPhaseCompositeNames[feature] << "_m" << phase
             << "\":" << model[index];
    }
  }
  output << "}}";
}

void writeArtifact(const Options& options, const TrainingResult& training,
                   const Evaluation& fair_tournament,
                   const PairedSummary& tournament_paired,
                   const FittingGate& fitting_gate,
                   const Evaluation* probe_candidate,
                   const Evaluation* probe_baseline,
                   const PairedSummary* probe_paired,
                   const ProbeGate* probe_gate, double total_wall) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open reservoir artifact");
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"vertical-reservoir-policy\",\n"
         << "  \"preregistered\":true,\n"
         << "  \"scoring\":{\"levelBonus\":" << kLevelBonus << "},\n"
         << "  \"publicDecisionBoundary\":[\"board\",\"nextDisc\","
            "\"movesRemaining\",\"terminal\"],\n"
         << "  \"excluded\":[\"gameSeed\",\"score\",\"level\","
            "\"moveIndex\",\"history\",\"future\"],\n"
         << "  \"policy\":{\"successorSamples\":" << kSuccessorSamples
         << ",\"exactEventStratification\":true,"
            "\"commonRandomNumbersAcrossActions\":true,"
            "\"reflectionCanonical\":true,\"terminalValue\":"
         << kTerminalValue << ",\"baseFeatures\":" << kBaseFeatureCount
         << ",\"phaseComposites\":" << kPhaseCompositeCount
         << ",\"parameters\":" << kModelSize << "},\n"
         << "  \"featureHypothesis\":{"
            "\"verticalViableAndDead\":true,"
            "\"inverseGapReadiness\":true,"
            "\"sameTargetPairQuadraticCubic\":true,"
            "\"inertAdditionReleaseLadders\":[1,2,3,4,5],"
            "\"simultaneousClearAndWaveCounts\":true,"
            "\"lowEscapeWells\":true,\"horizontalViability\":true,"
            "\"lowDeadCaps\":true,\"edgeInteriorCoverAltitude\":true,"
            "\"coverViableNeighborTopology\":true,"
            "\"immediateTransitionFeatures\":true,"
            "\"shapeAndRoughness\":true},\n"
         << "  \"cem\":{\"population\":" << kPopulation
         << ",\"elite\":" << kElite << ",\"generations\":"
         << kGenerations << ",\"rotatingBatchGames\":" << kBatchGames
         << ",\"tournamentGames\":" << kTournamentGames
         << ",\"objective\":"
            "\"0.55*(0.5*meanMoves+meanScore/6800)+"
            "0.45*(0.5*bottomQuartileMeanMoves+bottomQuartileMeanScore/6800)"
            "\",\"optimizerSeed\":\"0x76722d63656d2d31\"},\n"
         << "  \"seedDiscipline\":{\"training\":[" << kTrainingSeedStart
         << ',' << (kTrainingSeedStart + kGenerations * kBatchGames - 1)
         << "],\"tournament\":[" << kTournamentSeedStart << ','
         << (kTournamentSeedStart + kTournamentGames - 1)
         << "],\"probeIfPromoted\":[" << kProbeSeedStart << ','
         << (kProbeSeedStart + kProbeGames - 1)
         << "],\"reservedConcurrentRanges\":[\"0x3d3\",\"0x3d40\","
            "\"0x3d50\",\"0x3d60\",\"0x3d61\"],"
            "\"forbidden\":[\"0x7d\",\"0xd7\"]},\n"
         << "  \"resourceCaps\":{\"maximumMoves\":" << kMaximumMoves
         << ",\"wallSeconds\":" << kWallLimitSeconds
         << ",\"rssBytes\":" << kRssLimitBytes << "},\n"
         << "  \"generations\":[";
  for (std::size_t index = 0; index < training.generations.size(); ++index) {
    if (index != 0) output << ',';
    const GenerationRecord& generation = training.generations[index];
    output << "{\"generation\":" << generation.generation
           << ",\"seedStart\":" << generation.seed_start
           << ",\"bestObjective\":" << generation.best_objective
           << ",\"bestScore\":" << generation.best_score
           << ",\"bestMoves\":" << generation.best_moves
           << ",\"bestLowerQuartileScore\":"
           << generation.best_lower_quartile_score
           << ",\"bestLowerQuartileMoves\":"
           << generation.best_lower_quartile_moves
           << ",\"meanObjective\":" << generation.mean_objective
           << ",\"bestFingerprint\":\""
           << hex64(generation.best_fingerprint)
           << "\",\"elapsedSeconds\":" << generation.elapsed_seconds
           << '}';
  }
  output << "],\n  \"trainingSeconds\":" << training.seconds
         << ",\n  \"archiveCandidates\":" << training.archive_candidates
         << ",\n  \"championModel\":";
  writeModel(output, training.champion);
  output << ",\n  \"tournament\":{\"candidate\":";
  writeEvaluation(output, training.champion_evaluation, true);
  output << ",\"fairD1\":";
  writeEvaluation(output, fair_tournament, true);
  output << ",\"pairedCandidateMinusFairD1\":";
  writePaired(output, tournament_paired);
  output << ",\"gate\":";
  writeFittingGate(output, fitting_gate);
  output << "},\n  \"probeOpened\":"
         << (probe_candidate != nullptr ? "true" : "false")
         << ",\n  \"probe\":";
  if (probe_candidate == nullptr) {
    output << "null";
  } else {
    output << "{\"candidate\":";
    writeEvaluation(output, *probe_candidate, true);
    output << ",\"fairD1\":";
    writeEvaluation(output, *probe_baseline, true);
    output << ",\"pairedCandidateMinusFairD1\":";
    writePaired(output, *probe_paired);
    output << ",\"gate\":{\"scoreLower95Positive\":"
           << (probe_gate->score_lower_95_positive ? "true" : "false")
           << ",\"moveLower95Positive\":"
           << (probe_gate->move_lower_95_positive ? "true" : "false")
           << ",\"candidateMeanMovesAtLeast200\":"
           << (probe_gate->minimum_moves ? "true" : "false")
           << ",\"passed\":" << (probe_gate->passed ? "true" : "false")
           << "}}";
  }
  output << ",\n  \"qualified\":"
         << (probe_gate != nullptr && probe_gate->passed ? "true" : "false")
         << ",\n  \"checkpoint\":\"" << options.checkpoint
         << "\",\n  \"modelFingerprint\":\""
         << hex64(fingerprint(training.champion))
         << "\",\n  \"totalWallSeconds\":" << total_wall
         << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
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

PublicState featureFixture() {
  PublicState state;
  state.board.fill(kEmpty);
  state.board[indexOf(6, 0)] = 4;
  state.board[indexOf(5, 0)] = 4;
  state.board[indexOf(6, 1)] = 1;
  state.board[indexOf(5, 1)] = kSolid;
  state.board[indexOf(6, 3)] = kCracked;
  state.board[indexOf(6, 4)] = 5;
  state.next_disc = 2;
  state.moves_remaining = 4;
  return state;
}

bool selfTest(std::ostream& output) {
  expect(kLevelBonus == 17'000, "corrected scoring regression");
  const PublicState fixture = featureFixture();
  MoveResult fixture_move;
  fixture_move.state = materialize(fixture);
  const BaseFeatures features = extractBaseFeatures(fixture_move);
  for (const double value : features) {
    expect(std::isfinite(value), "reservoir feature was non-finite");
  }
  expect(features[kVerticalViable] > 0.0 &&
             features[kVerticalDead] > 0.0 &&
             features[kSameTargetPairs] > 0.0 &&
             features[kSameTargetQuadratic] > 0.0 &&
             features[kSameTargetCubic] > 0.0,
         "same-target vertical reservoir fixture failed");
  MoveResult reflected_move = fixture_move;
  reflected_move.state.board = detail::mirrorBoard(fixture.board);
  const BaseFeatures reflected_features = extractBaseFeatures(reflected_move);
  expect(features == reflected_features,
         "engineered features were not reflection invariant");

  Board ladder{};
  ladder[indexOf(6, 2)] = 4;
  ladder[indexOf(5, 2)] = 5;
  const ReleaseResult release = inertRelease(ladder, 2, 3);
  expect(release.clears == 2 && release.waves == 2 &&
             release.simultaneous_extra == 0,
         "one-through-five inert release ladder failed");
  Board simultaneous{};
  simultaneous[indexOf(6, 2)] = 4;
  simultaneous[indexOf(5, 2)] = 4;
  const ReleaseResult simultaneous_release =
      inertRelease(simultaneous, 2, 2);
  expect(simultaneous_release.clears == 2 &&
             simultaneous_release.waves == 1 &&
             simultaneous_release.simultaneous_extra == 1,
         "simultaneous target multiplicity release failed");

  const Model model = initialModel();
  const Decision first = chooseAction(fixture, model);
  const Decision second = chooseAction(fixture, model);
  expect(first == second && isLegal(fixture.board, first.action),
         "reservoir decision determinism/legality failed");
  const PublicState mirrored = mirror(fixture);
  const Decision mirrored_decision = chooseAction(mirrored, model);
  expect(mirrored_decision.action == kBoardSize - 1 - first.action,
         "reservoir action reflection failed");
  for (int column = 0; column < kBoardSize; ++column) {
    expect(mirrored_decision.values[kBoardSize - 1 - column] ==
               first.values[column],
           "reservoir root values reflection failed");
  }
  State metadata = materialize(fixture);
  metadata.score = 9'999'999;
  metadata.level = 99;
  metadata.moves_played = 777;
  expect(publicState(metadata) == fixture &&
             chooseAction(publicState(metadata), model) == first,
         "reservoir policy used hidden metadata");
  PublicState terminal = fixture;
  terminal.terminal = true;
  expect(chooseAction(terminal, model).action == -1,
         "terminal reservoir state selected an action");

  bool terminal_sample_seen = false;
  PublicState danger;
  danger.board.fill(kSolid);
  danger.board[indexOf(0, 0)] = kEmpty;
  danger.next_disc = 6;
  danger.moves_remaining = 1;
  const Decision danger_decision = chooseAction(danger, model);
  for (const double value : danger_decision.values) {
    terminal_sample_seen = terminal_sample_seen ||
                           (std::isfinite(value) && value < -1'000.0);
  }
  expect(terminal_sample_seen && isLegal(danger.board, danger_decision.action),
         "hard terminal safety value failed");

  const State canonical = materialize(fixture);
  const std::uint32_t chance_seed =
      detail::scenarioSeedForState(canonical, kPolicySeed, 1);
  for (const std::uint32_t domain : {detail::kRevealSampleDomain,
                                     detail::kDiscSampleDomain}) {
    for (int event : {0, 1, 31}) {
      std::array<int, kSuccessorSamples> strata{};
      for (int sample = 0; sample < kSuccessorSamples; ++sample) {
        const double unit = detail::stratifiedUnit(
            chance_seed, sample, kSuccessorSamples, domain, event);
        const int stratum =
            static_cast<int>(std::floor(unit * kSuccessorSamples));
        expect(stratum >= 0 && stratum < kSuccessorSamples,
               "successor stratum out of range");
        ++strata[stratum];
      }
      for (const int count : strata) {
        expect(count == 1, "successor event was not exactly stratified");
      }
    }
  }

  std::stringstream checkpoint(std::ios::in | std::ios::out | std::ios::binary);
  writeCheckpoint(checkpoint, model);
  checkpoint.seekg(0);
  const Model round_trip = readCheckpoint(checkpoint);
  expect(round_trip == model && fingerprint(round_trip) == fingerprint(model),
         "checkpoint round trip failed");

  expect(allowedSeed(kTrainingSeedStart, SeedUse::kTraining) &&
             allowedSeed(kTournamentSeedStart + kTournamentGames - 1,
                         SeedUse::kTournament) &&
             allowedSeed(kProbeSeedStart + kProbeGames - 1, SeedUse::kProbe),
         "authorized reservoir seed rejected");
  expect(throwsInvalid([] {
           requireSeed(0x3d30'0000u, SeedUse::kTraining);
         }) &&
             throwsInvalid([] {
               requireSeed(0x3d40'0000u, SeedUse::kTraining);
             }) &&
             throwsInvalid([] {
               requireSeed(0x3d50'0000u, SeedUse::kTraining);
             }) &&
             throwsInvalid([] {
               requireSeed(0x3d60'0000u, SeedUse::kTraining);
             }) &&
             throwsInvalid([] {
               requireSeed(0x3d61'0000u, SeedUse::kTraining);
             }) &&
             throwsInvalid([] {
               requireSeed(0x7d63'0000u, SeedUse::kTraining);
             }) &&
             throwsInvalid([] {
               requireSeed(0xd763'0000u, SeedUse::kTraining);
             }),
         "reservoir game seed guards failed");
  enforceRssLimit();
  output << std::setprecision(12)
         << "VERTICAL_RESERVOIR_POLICY_SELF_TEST {\"passed\":true,"
         << "\"levelBonus\":" << kLevelBonus
         << ",\"baseFeatures\":" << kBaseFeatureCount
         << ",\"parameters\":" << kModelSize
         << ",\"sameTargetMultiplicity\":true,"
         << "\"releaseLadders\":true,\"lowWells\":true,"
         << "\"reflection\":true,\"publicMetadataBlind\":true,"
         << "\"deterministic\":true,\"terminalSafe\":true,"
         << "\"exactEventStratification\":true,"
         << "\"checkpoint\":true,\"seedGuards\":true,"
         << "\"peakRssBytes\":" << peakRssBytes() << "}\n";
  return true;
}

int run(const Options& options, std::ostream& output) {
  const Deadline deadline;
  TrainingResult training = train(options.threads, deadline);
  // Persist and lock the selected parameter vector before reading probe data.
  saveCheckpoint(options.checkpoint, training.champion);
  const Evaluation fair_tournament =
      evaluate(nullptr, Policy::kFairD1, kTournamentSeedStart,
               kTournamentGames, SeedUse::kTournament, deadline);
  const PairedSummary tournament_paired =
      pair(training.champion_evaluation, fair_tournament, 1.9791257001);
  const FittingGate fitting_gate =
      fittingGate(training.champion_evaluation, fair_tournament);

  Evaluation probe_candidate;
  Evaluation probe_baseline;
  PairedSummary probe_paired;
  ProbeGate probe_gate;
  bool probe_opened = false;
  if (fitting_gate.passed) {
    probe_candidate = evaluate(&training.champion, Policy::kReservoir,
                               kProbeSeedStart, kProbeGames, SeedUse::kProbe,
                               deadline);
    probe_baseline = evaluate(nullptr, Policy::kFairD1, kProbeSeedStart,
                              kProbeGames, SeedUse::kProbe, deadline);
    probe_paired =
        pair(probe_candidate, probe_baseline, kPairedT975Df31);
    probe_gate = probeGate(probe_candidate, probe_paired);
    probe_opened = true;
  }
  deadline.check();
  enforceRssLimit();
  const double total_wall = deadline.elapsedSeconds();
  writeArtifact(options, training, fair_tournament, tournament_paired,
                fitting_gate, probe_opened ? &probe_candidate : nullptr,
                probe_opened ? &probe_baseline : nullptr,
                probe_opened ? &probe_paired : nullptr,
                probe_opened ? &probe_gate : nullptr, total_wall);

  output << std::fixed << std::setprecision(3)
         << "VERTICAL_RESERVOIR_POLICY_RESULT {\"championScore\":"
         << training.champion_evaluation.mean_score
         << ",\"championMoves\":" << training.champion_evaluation.mean_moves
         << ",\"fairD1Score\":" << fair_tournament.mean_score
         << ",\"fairD1Moves\":" << fair_tournament.mean_moves
         << ",\"scoreRatio\":" << fitting_gate.score_ratio
         << ",\"moveRatio\":" << fitting_gate.move_ratio
         << ",\"revealsPerMove\":"
         << training.champion_evaluation.reveals_per_move
         << ",\"fairD1RevealsPerMove\":" << fair_tournament.reveals_per_move
         << ",\"fittingPassed\":"
         << (fitting_gate.passed ? "true" : "false")
         << ",\"probeOpened\":" << (probe_opened ? "true" : "false")
         << ",\"probePassed\":"
         << (probe_opened && probe_gate.passed ? "true" : "false")
         << ",\"fingerprint\":\""
         << hex64(fingerprint(training.champion))
         << "\",\"trainingSeconds\":" << training.seconds
         << ",\"totalWallSeconds\":" << total_wall
         << ",\"peakRssBytes\":" << peakRssBytes() << ",\"artifact\":\""
         << options.output << "\",\"checkpoint\":\"" << options.checkpoint
         << "\"}\n";
  return probe_opened && probe_gate.passed ? 0 : 2;
}

}  // namespace drop7::vertical_reservoir_policy

#ifndef DROP7_VERTICAL_RESERVOIR_POLICY_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::vertical_reservoir_policy::selfTest(std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::vertical_reservoir_policy::parseOptions(argc, argv, 2);
      return drop7::vertical_reservoir_policy::run(options, std::cout);
    }
    std::cerr << "usage: drop7_vertical_reservoir_policy --self-test | --run "
                 "[--output PATH] [--checkpoint PATH] [--threads N]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_vertical_reservoir_policy: " << error.what() << '\n';
    return 1;
  }
}
#endif
