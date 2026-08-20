#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#include "../../fair-expectimax/reference/fair-only-depth4.cpp"
#undef DROP7_FAIR_ONLY_DEPTH4_LIBRARY

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <type_traits>
#include <utility>
#include <vector>

// Trains a long-horizon Drop7 regeneration policy with primal-dual PPO.
//
// The policy can observe only board cells, the visible next disc, and the
// five-drop phase.  It is an exactly reflection-equivariant sparse-NNUE
// residual over a fixed, deterministic, fair one-ply policy.  Eight critics
// separate score, lifetime, terminal hazard, regenerative flow, and the four
// medium/high-load occupancy/cover constraints.  PPO advantages are formed
// from five-move returns; constraint targets are aligned full five-move cycles
// and their Lagrange multipliers are updated by projected dual ascent.
//
// `--self-test`, `--preregister`, and `--preflight` read no gameplay seeds.
// Gameplay commands require an exact protocol token and source SHA.  Candidate
// gates finish the absolute candidate cohort before the D4 comparator reads
// the same lane.
namespace drop7::primal_dual_actor_critic {

namespace d4 = drop7::fair_only_depth4;
namespace fair = drop7::fair_only_horizon;
namespace detail = drop7::cfpi::detail;
using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// Frozen protocol
// ---------------------------------------------------------------------------

constexpr int kIterations = 128;
constexpr int kEpisodesPerIteration = 1'024;
constexpr int kTrainingEpisodes = kIterations * kEpisodesPerIteration;
constexpr int kInitialEpisodesPerIteration = kEpisodesPerIteration / 2;
constexpr int kRestartEpisodesPerIteration = kEpisodesPerIteration / 2;
constexpr int kInitialMaximumMoves = 500;
constexpr int kRestartMaximumMoves = 200;
constexpr int kMacroMoves = kMovesPerLevel;
constexpr int kPpoEpochs = 4;
constexpr int kMinibatch = 512;
constexpr int kWorkers = 8;
constexpr int kReservoirCapacity = 8'192;
constexpr int kCalibrationWindowIterations = 8;
constexpr int kMaximumGateMoves = 2'000;
constexpr int kBootstrapReplicates = 10'000;

constexpr double kGamma = 0.995;
constexpr double kClipRatio = 0.20;
constexpr double kEntropyCoefficient = 0.006;
constexpr double kValueCoefficient = 0.25;
constexpr double kLearningRate = 1.5e-4;
constexpr double kAdamBeta1 = 0.9;
constexpr double kAdamBeta2 = 0.999;
constexpr double kAdamEpsilon = 1.0e-8;
constexpr double kGradientNormClip = 2.0;
constexpr double kResidualLogitScale = 1.5;
constexpr double kBaseLogitScale = 2.0;
constexpr double kLifetimeAdvantageWeight = 0.35;
constexpr double kRegenerationAdvantageWeight = 0.20;
constexpr double kTerminalInitialLambda = 2.0;
constexpr double kTerminalLambdaMaximum = 20.0;
constexpr double kConstraintLambdaMaximum = 20.0;
constexpr double kDualLearningRate = 0.025;
// A two-percent per-cycle hazard corresponds to roughly fifty five-move
// cycles (250 moves) before censoring; the final gate remains stricter at
// >300 mean moves and >1M bootstrap-lower score.
constexpr double kTerminalRiskLimit = 0.02;
constexpr double kTailPolicyWeight = 2.0;
constexpr double kScoreScale = 17'000.0;
constexpr double kLifetimeScale = 5.0;
constexpr double kRegenerationClearScale = 2.4;
constexpr double kRegenerationRevealScale = 1.35;

// Five-move drift + margin <= 0.  A positive margin asks the policy for
// strictly negative drift as load rises rather than accepting a merely flat
// but fragile board.
constexpr double kMediumOccupancyMargin = 0.00;
constexpr double kHighOccupancyMargin = 0.25;
constexpr double kMediumCoverMargin = 0.00;
constexpr double kHighCoverMargin = 0.15;
constexpr int kMediumOccupancyMinimum = 18;
constexpr int kHighOccupancyMinimum = 30;
constexpr int kMediumCoverMinimum = 8;
constexpr int kHighCoverMinimum = 15;

constexpr std::uint64_t kMaximumRssBytes = 512ull * 1024ull * 1024ull;
constexpr double kMaximumWallSeconds = 12.0 * 60.0 * 60.0;
constexpr double kCheckpointIoReserveSeconds = 1.0;
constexpr std::uint64_t kMaximumCheckpointBytes = 16ull * 1024ull * 1024ull;
constexpr std::uint64_t kMaximumResidentTransitions =
    static_cast<std::uint64_t>(kEpisodesPerIteration) * kInitialMaximumMoves;
constexpr std::uint64_t kMaximumTrainingMoves =
    static_cast<std::uint64_t>(kIterations) *
    (static_cast<std::uint64_t>(kInitialEpisodesPerIteration) *
         kInitialMaximumMoves +
     static_cast<std::uint64_t>(kRestartEpisodesPerIteration) *
         kRestartMaximumMoves);
constexpr std::uint64_t kMaximumOptimizerSteps =
    static_cast<std::uint64_t>(kIterations) * kPpoEpochs *
    ((kMaximumResidentTransitions + kMinibatch - 1u) / kMinibatch);

struct SeedLane {
  std::uint32_t first;
  std::uint32_t last;
};

constexpr SeedLane kTrainingLane{0x3dac'0000u, 0x3dad'ffffu};
constexpr SeedLane kStageALane{0x3dae'0000u, 0x3dae'001fu};
constexpr SeedLane kStageBLane{0x3dae'1000u, 0x3dae'107fu};
constexpr SeedLane kStageCLane{0x3daf'0000u, 0x3daf'00ffu};
constexpr SeedLane kReservedLane{0x3daf'1000u, 0x3daf'ffffu};
constexpr SeedLane kBurnedPreflightLane{0x3d6e'4000u, 0x3d6e'4003u};

constexpr std::string_view kFreshExecutionToken =
    "EXECUTE_FROZEN_PRIMAL_DUAL_3DAC_PROTOCOL";
constexpr std::uint32_t kNetworkSeed = 0x5044'4143u;        // PDAC
constexpr std::uint32_t kPolicyDomain = 0x504f'4c32u;       // POL2
constexpr std::uint32_t kRestartDomain = 0x5253'5432u;      // RST2
constexpr std::uint32_t kReservoirDomain = 0x5253'5632u;    // RSV2
constexpr std::uint32_t kShuffleDomain = 0x5348'4632u;      // SHF2
constexpr std::uint32_t kBootstrapDomain = 0x4253'5432u;    // BST2
constexpr std::uint32_t kCalibrationDomain = 0x4341'4c32u;  // CAL2

struct StageGate {
  int games;
  double score;
  double moves;
  double lower_quartile_moves;
  double clears_per_move;
  double reveals_per_move;
  double score_ratio_vs_d4;
  double move_ratio_vs_d4;
  int joint_wins;
};

constexpr StageGate kStageAGate{32, 500'000.0, 150.0, 90.0, 2.15, 1.18,
                                1.15, 1.15, 20};
constexpr StageGate kStageBGate{128, 750'000.0, 220.0, 140.0, 2.25, 1.28,
                                1.15, 1.15, 80};

static_assert(kLevelBonus == 17'000);
static_assert(kMacroMoves == 5 && kTrainingEpisodes == 131'072);
static_assert(kTrainingLane.last - kTrainingLane.first + 1u ==
              static_cast<std::uint32_t>(kTrainingEpisodes));
static_assert(kStageALane.last - kStageALane.first + 1u == 32u);
static_assert(kStageBLane.last - kStageBLane.first + 1u == 128u);
static_assert(kStageCLane.last - kStageCLane.first + 1u == 256u);
static_assert(kTrainingLane.last < kStageALane.first &&
              kStageALane.last < kStageBLane.first &&
              kStageBLane.last < kStageCLane.first &&
              kStageCLane.last < kReservedLane.first);
static_assert(kMaximumTrainingMoves == 45'875'200u);
static_assert(kMaximumResidentTransitions == 512'000u);
static_assert(kStageAGate.games == 32 && kStageBGate.games == 128);

bool isSha256(std::string_view value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](char token) {
           return (token >= '0' && token <= '9') ||
                  (token >= 'a' && token <= 'f');
         });
}

bool protectedPrefix(std::uint32_t seed) {
  const std::uint32_t prefix = seed >> 24u;
  return prefix == 0x4du || prefix == 0x7du || prefix == 0xd7u;
}

bool inLane(std::uint32_t seed, SeedLane lane) {
  return seed >= lane.first && seed <= lane.last && !protectedPrefix(seed);
}

enum class SeedUse { kTraining, kStageA, kStageB, kStageC, kBurnedPreflight };

SeedLane laneFor(SeedUse use) {
  switch (use) {
    case SeedUse::kTraining: return kTrainingLane;
    case SeedUse::kStageA: return kStageALane;
    case SeedUse::kStageB: return kStageBLane;
    case SeedUse::kStageC: return kStageCLane;
    case SeedUse::kBurnedPreflight: return kBurnedPreflightLane;
  }
  throw std::logic_error("unknown seed use");
}

void requireSeed(std::uint32_t seed, SeedUse use) {
  if (!inLane(seed, laneFor(use))) {
    throw std::invalid_argument("seed outside the exact preregistered lane");
  }
}

void requireFreshAuthorization(std::string_view token,
                               std::string_view source_sha256) {
  if (token != kFreshExecutionToken || !isSha256(source_sha256)) {
    throw std::invalid_argument(
        "fresh command requires the frozen protocol token and source SHA-256");
  }
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

struct Deadline {
  explicit Deadline(double prior_seconds = 0) : prior_seconds(prior_seconds) {
    if (!std::isfinite(prior_seconds) || prior_seconds < 0 ||
        prior_seconds > kMaximumWallSeconds) {
      throw std::invalid_argument("invalid cumulative wall time");
    }
  }
  Clock::time_point started = Clock::now();
  double prior_seconds = 0;
  double seconds() const {
    return prior_seconds +
        std::chrono::duration<double>(Clock::now() - started).count();
  }
  void check() const {
    if (seconds() > kMaximumWallSeconds) {
      throw std::runtime_error("primal-dual experiment exceeded 12 hour cap");
    }
    if (peakRssBytes() > kMaximumRssBytes) {
      throw std::runtime_error("primal-dual experiment exceeded 512 MiB RSS");
    }
  }
};

// ---------------------------------------------------------------------------
// Public boundary and features
// ---------------------------------------------------------------------------

struct PublicState {
  Board board{};
  std::uint8_t next_disc = 1;
  std::uint8_t phase = kMovesPerLevel;
  bool terminal = false;

  bool operator==(const PublicState&) const = default;
};

void validatePublicState(const PublicState& source) {
  if (source.next_disc < 1 || source.next_disc > kBoardSize ||
      source.phase > kMovesPerLevel ||
      (source.phase == 0 && !source.terminal) ||
      std::any_of(source.board.begin(), source.board.end(),
                  [](std::uint8_t cell) { return cell > kCracked; })) {
    throw std::invalid_argument("public state is malformed");
  }
  int legal_count = 0;
  (void)legalColumns(source.board, legal_count);
  if (!source.terminal && legal_count == 0) {
    throw std::invalid_argument("nonterminal public state has no legal move");
  }
}

PublicState publicState(const State& source) {
  if (source.next_disc < 1 || source.next_disc > kBoardSize ||
      source.moves_remaining < 0 || source.moves_remaining > kMovesPerLevel ||
      (source.moves_remaining == 0 && !source.game_over)) {
    throw std::invalid_argument("state is outside the public model domain");
  }
  return {source.board, source.next_disc,
          static_cast<std::uint8_t>(source.moves_remaining), source.game_over};
}

State materialize(const PublicState& source) {
  validatePublicState(source);
  State result;
  result.board = source.board;
  result.next_disc = source.next_disc;
  result.score = 0;
  result.level = 1;
  result.moves_remaining = source.phase;
  result.moves_played = 0;
  result.game_over = source.terminal;
  return result;
}

PublicState mirror(const PublicState& source) {
  PublicState result = source;
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      result.board[indexOf(row, kBoardSize - 1 - column)] =
          source.board[indexOf(row, column)];
    }
  }
  return result;
}

int occupancy(const Board& board) {
  return static_cast<int>(std::count_if(board.begin(), board.end(),
                                        [](std::uint8_t cell) {
                                          return cell != kEmpty;
                                        }));
}

int covered(const Board& board) {
  return static_cast<int>(std::count_if(board.begin(), board.end(),
                                        [](std::uint8_t cell) {
                                          return cell == kSolid ||
                                                 cell == kCracked;
                                        }));
}

constexpr int kScalarFeatures = 24;
constexpr int kHidden1 = 48;
constexpr int kHidden2 = 48;
constexpr int kValueHeads = 8;
constexpr int kCellCategories = 10;

enum ValueHead : int {
  kScoreHead = 0,
  kLifetimeHead = 1,
  kTerminalHead = 2,
  kOccupancyMediumHead = 3,
  kOccupancyHighHead = 4,
  kCoverMediumHead = 5,
  kCoverHighHead = 6,
  kRegenerationHead = 7,
};

std::array<double, kScalarFeatures> scalarFeatures(const PublicState& state) {
  std::array<double, kScalarFeatures> result{};
  std::array<int, kBoardSize> heights{};
  int occupied = 0;
  int covers = 0;
  int solids = 0;
  int cracked = 0;
  int numbered = 0;
  int top = 0;
  int edge = 0;
  int center = 0;
  int exposed_covers = 0;
  int adjacent_ones = 0;
  int adjacent_twos = 0;
  int triple_twos = 0;
  int near_one = 0;
  int near_two = 0;
  int isolated_low = 0;
  int covered_height_risk = 0;

  constexpr std::array<std::array<int, 2>, 4> directions{{
      {{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}},
  }};
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      const std::uint8_t cell = state.board[indexOf(row, column)];
      if (cell == kEmpty) continue;
      ++occupied;
      ++heights[column];
      top += row == 0;
      edge += column == 0 || column == kBoardSize - 1;
      center += column >= 2 && column <= 4;
      if (cell == kSolid || cell == kCracked) {
        ++covers;
        solids += cell == kSolid;
        cracked += cell == kCracked;
        covered_height_risk += (kBoardSize - row) * (kBoardSize - row);
        for (const auto& direction : directions) {
          const int nr = row + direction[0];
          const int nc = column + direction[1];
          if (inside(nr, nc) && isNumbered(state.board[indexOf(nr, nc)])) {
            ++exposed_covers;
            break;
          }
        }
      } else if (isNumbered(cell)) {
        ++numbered;
        const int horizontal = lineLength(state.board, row, column, false);
        const int vertical = lineLength(state.board, row, column, true);
        const int distance = std::min(std::abs(static_cast<int>(cell) - horizontal),
                                      std::abs(static_cast<int>(cell) - vertical));
        near_one += distance == 1;
        near_two += distance == 2;
        if (cell <= 2) {
          bool neighbor = false;
          for (const auto& direction : directions) {
            const int nr = row + direction[0];
            const int nc = column + direction[1];
            if (inside(nr, nc) &&
                state.board[indexOf(nr, nc)] == cell) neighbor = true;
          }
          isolated_low += !neighbor;
        }
      }
      if (column + 1 < kBoardSize) {
        adjacent_ones += cell == 1 &&
                         state.board[indexOf(row, column + 1)] == 1;
        adjacent_twos += cell == 2 &&
                         state.board[indexOf(row, column + 1)] == 2;
      }
      if (column + 2 < kBoardSize) {
        triple_twos += cell == 2 &&
                       state.board[indexOf(row, column + 1)] == 2 &&
                       state.board[indexOf(row, column + 2)] == 2;
      }
    }
  }
  const int maximum_height = *std::max_element(heights.begin(), heights.end());
  const double mean_height = occupied / 7.0;
  int height_square_sum = 0;
  int roughness = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    height_square_sum += heights[column] * heights[column];
    if (column != 0) roughness += std::abs(heights[column] - heights[column - 1]);
  }
  int row_segments = 0;
  for (int row = 0; row < kBoardSize; ++row) {
    bool active = false;
    for (int column = 0; column < kBoardSize; ++column) {
      const bool here = state.board[indexOf(row, column)] != kEmpty;
      row_segments += here && !active;
      active = here;
    }
  }

  result[0] = occupied / 49.0;
  result[1] = covers / 49.0;
  result[2] = solids / 49.0;
  result[3] = cracked / 49.0;
  result[4] = numbered / 49.0;
  result[5] = maximum_height / 7.0;
  result[6] = mean_height / 7.0;
  result[7] = roughness / 36.0;
  result[8] = top / 7.0;
  result[9] = static_cast<double>(std::count(heights.begin(), heights.end(), 0)) / 7.0;
  result[10] = adjacent_ones / 7.0;
  result[11] = adjacent_twos / 7.0;
  result[12] = triple_twos / 5.0;
  result[13] = near_one / 49.0;
  result[14] = near_two / 49.0;
  result[15] = exposed_covers / 49.0;
  result[16] = isolated_low / 49.0;
  result[17] = state.phase / 5.0;
  result[18] = state.next_disc / 7.0;
  result[19] = row_segments / 28.0;
  // Algebraic integer form of sum((h - mean)^2) makes this feature bit-exact
  // under reflection; summing floating squares in reverse column order does not.
  result[20] = (height_square_sum * 7 - occupied * occupied) / (7.0 * 84.0);
  result[21] = edge / 14.0;
  result[22] = center / 21.0;
  result[23] = covered_height_risk / 1'715.0;
  return result;
}

struct Layout {
  static constexpr int cell = 0;
  static constexpr int disc = cell + kCellCount * kCellCategories * kHidden1;
  static constexpr int phase = disc + kBoardSize * kHidden1;
  static constexpr int scalar = phase + kMovesPerLevel * kHidden1;
  static constexpr int b1 = scalar + kScalarFeatures * kHidden1;
  static constexpr int w2 = b1 + kHidden1;
  static constexpr int b2 = w2 + kHidden2 * kHidden1;
  static constexpr int policy_w = b2 + kHidden2;
  static constexpr int policy_b = policy_w + kBoardSize * kHidden2;
  static constexpr int value_w = policy_b + kBoardSize;
  static constexpr int value_b = value_w + kValueHeads * kHidden2;
  static constexpr int count = value_b + kValueHeads;
};

static_assert(Layout::count == 28'383);

struct RawCache {
  PublicState state{};
  std::array<double, kScalarFeatures> scalars{};
  std::array<double, kHidden1> hidden1{};
  std::array<double, kHidden2> hidden2{};
  std::array<double, kBoardSize> policy{};
  std::array<double, kValueHeads> values{};
};

struct Prediction {
  std::array<double, kBoardSize> logits{};
  std::array<double, kBoardSize> probabilities{};
  std::array<double, kBoardSize> log_probabilities{};
  std::array<double, kValueHeads> values{};
  std::uint8_t legal_mask = 0;
  RawCache forward{};
  RawCache reflected{};
};

class Network {
 public:
  explicit Network(std::uint32_t seed = kNetworkSeed)
      : parameters_(Layout::count), first_moment_(Layout::count),
        second_moment_(Layout::count), proposed_parameters_(Layout::count),
        proposed_first_(Layout::count), proposed_second_(Layout::count) {
    Mulberry32 random(seed);
    const auto initialize = [&](int begin, int count, double radius) {
      for (int index = 0; index < count; ++index) {
        parameters_[begin + index] = static_cast<float>(
            (random.nextUnit() * 2.0 - 1.0) * radius);
      }
    };
    initialize(Layout::cell, Layout::disc - Layout::cell, 0.025);
    initialize(Layout::disc, Layout::phase - Layout::disc, 0.04);
    initialize(Layout::phase, Layout::scalar - Layout::phase, 0.04);
    initialize(Layout::scalar, Layout::b1 - Layout::scalar, 0.03);
    initialize(Layout::w2, Layout::b2 - Layout::w2,
               std::sqrt(6.0 / (kHidden1 + kHidden2)));
    // Zero heads make the initial policy exactly the fixed D1 backbone and
    // make every critic's initial prediction neutral.
  }

  RawCache forwardRaw(const PublicState& state) const {
    validatePublicState(state);
    if (state.terminal || state.phase == 0) {
      throw std::invalid_argument("network cannot evaluate a terminal state");
    }
    RawCache cache;
    cache.state = state;
    cache.scalars = scalarFeatures(state);
    for (int hidden = 0; hidden < kHidden1; ++hidden) {
      double total = parameters_[Layout::b1 + hidden];
      for (int cell = 0; cell < kCellCount; ++cell) {
        const int category = state.board[cell];
        total += parameters_[Layout::cell +
                             (cell * kCellCategories + category) * kHidden1 +
                             hidden];
      }
      total += parameters_[Layout::disc +
                           (state.next_disc - 1) * kHidden1 + hidden];
      total += parameters_[Layout::phase +
                           (state.phase - 1) * kHidden1 + hidden];
      for (int scalar = 0; scalar < kScalarFeatures; ++scalar) {
        total += parameters_[Layout::scalar + scalar * kHidden1 + hidden] *
                 cache.scalars[scalar];
      }
      cache.hidden1[hidden] = std::tanh(total / 7.0);
    }
    for (int output = 0; output < kHidden2; ++output) {
      double total = parameters_[Layout::b2 + output];
      for (int input = 0; input < kHidden1; ++input) {
        total += parameters_[Layout::w2 + output * kHidden1 + input] *
                 cache.hidden1[input];
      }
      cache.hidden2[output] = std::tanh(total);
    }
    for (int action = 0; action < kBoardSize; ++action) {
      double total = parameters_[Layout::policy_b + action];
      for (int input = 0; input < kHidden2; ++input) {
        total += parameters_[Layout::policy_w + action * kHidden2 + input] *
                 cache.hidden2[input];
      }
      cache.policy[action] = total;
    }
    for (int head = 0; head < kValueHeads; ++head) {
      double total = parameters_[Layout::value_b + head];
      for (int input = 0; input < kHidden2; ++input) {
        total += parameters_[Layout::value_w + head * kHidden2 + input] *
                 cache.hidden2[input];
      }
      cache.values[head] = total;
    }
    return cache;
  }

  Prediction predict(const PublicState& state,
                     const std::array<double, kBoardSize>& base_logits) const {
    Prediction result;
    result.forward = forwardRaw(state);
    result.reflected = forwardRaw(mirror(state));
    double maximum = -std::numeric_limits<double>::infinity();
    for (int action = 0; action < kBoardSize; ++action) {
      if (!isLegal(state.board, action)) {
        result.logits[action] = -std::numeric_limits<double>::infinity();
        continue;
      }
      result.legal_mask |= static_cast<std::uint8_t>(1u << action);
      result.logits[action] =
          base_logits[action] +
          kResidualLogitScale * 0.5 *
              (result.forward.policy[action] +
               result.reflected.policy[kBoardSize - 1 - action]);
      maximum = std::max(maximum, result.logits[action]);
    }
    if (result.legal_mask == 0) {
      throw std::runtime_error("network observed no legal action");
    }
    std::array<double, kBoardSize> exponentials{};
    int exponential_count = 0;
    for (int action = 0; action < kBoardSize; ++action) {
      if ((result.legal_mask & (1u << action)) == 0) continue;
      result.probabilities[action] = std::exp(result.logits[action] - maximum);
      exponentials[exponential_count++] = result.probabilities[action];
    }
    // The multiset is identical after reflection. Sorting before reduction
    // prevents an orientation-dependent denominator from floating add order.
    std::sort(exponentials.begin(), exponentials.begin() + exponential_count);
    const double denominator = std::accumulate(
        exponentials.begin(), exponentials.begin() + exponential_count, 0.0);
    const double log_denominator = std::log(denominator);
    for (int action = 0; action < kBoardSize; ++action) {
      if ((result.legal_mask & (1u << action)) == 0) {
        result.log_probabilities[action] =
            -std::numeric_limits<double>::infinity();
        continue;
      }
      result.log_probabilities[action] =
          result.logits[action] - maximum - log_denominator;
      result.probabilities[action] =
          std::exp(result.log_probabilities[action]);
    }
    for (int head = 0; head < kValueHeads; ++head) {
      result.values[head] = 0.5 *
          (result.forward.values[head] + result.reflected.values[head]);
    }
    return result;
  }

  void backwardRaw(const RawCache& cache,
                   const std::array<double, kBoardSize>& policy_derivative,
                   const std::array<double, kValueHeads>& value_derivative,
                   std::vector<double>& gradient) const {
    std::array<double, kHidden2> hidden2_derivative{};
    for (int action = 0; action < kBoardSize; ++action) {
      gradient[Layout::policy_b + action] += policy_derivative[action];
      for (int input = 0; input < kHidden2; ++input) {
        gradient[Layout::policy_w + action * kHidden2 + input] +=
            policy_derivative[action] * cache.hidden2[input];
        hidden2_derivative[input] +=
            policy_derivative[action] *
            parameters_[Layout::policy_w + action * kHidden2 + input];
      }
    }
    for (int head = 0; head < kValueHeads; ++head) {
      gradient[Layout::value_b + head] += value_derivative[head];
      for (int input = 0; input < kHidden2; ++input) {
        gradient[Layout::value_w + head * kHidden2 + input] +=
            value_derivative[head] * cache.hidden2[input];
        hidden2_derivative[input] +=
            value_derivative[head] *
            parameters_[Layout::value_w + head * kHidden2 + input];
      }
    }
    std::array<double, kHidden1> hidden1_derivative{};
    for (int output = 0; output < kHidden2; ++output) {
      hidden2_derivative[output] *=
          1.0 - cache.hidden2[output] * cache.hidden2[output];
      gradient[Layout::b2 + output] += hidden2_derivative[output];
      for (int input = 0; input < kHidden1; ++input) {
        gradient[Layout::w2 + output * kHidden1 + input] +=
            hidden2_derivative[output] * cache.hidden1[input];
        hidden1_derivative[input] +=
            hidden2_derivative[output] *
            parameters_[Layout::w2 + output * kHidden1 + input];
      }
    }
    for (int hidden = 0; hidden < kHidden1; ++hidden) {
      hidden1_derivative[hidden] *=
          (1.0 - cache.hidden1[hidden] * cache.hidden1[hidden]) / 7.0;
      gradient[Layout::b1 + hidden] += hidden1_derivative[hidden];
      for (int cell = 0; cell < kCellCount; ++cell) {
        const int category = cache.state.board[cell];
        gradient[Layout::cell +
                 (cell * kCellCategories + category) * kHidden1 + hidden] +=
            hidden1_derivative[hidden];
      }
      gradient[Layout::disc +
               (cache.state.next_disc - 1) * kHidden1 + hidden] +=
          hidden1_derivative[hidden];
      gradient[Layout::phase +
               (cache.state.phase - 1) * kHidden1 + hidden] +=
          hidden1_derivative[hidden];
      for (int scalar = 0; scalar < kScalarFeatures; ++scalar) {
        gradient[Layout::scalar + scalar * kHidden1 + hidden] +=
            hidden1_derivative[hidden] * cache.scalars[scalar];
      }
    }
  }

  void backward(const Prediction& prediction,
                const std::array<double, kBoardSize>& logit_derivative,
                const std::array<double, kValueHeads>& value_derivative,
                std::vector<double>& gradient) const {
    std::array<double, kBoardSize> forward_policy{};
    std::array<double, kBoardSize> reflected_policy{};
    std::array<double, kValueHeads> split_value{};
    for (int action = 0; action < kBoardSize; ++action) {
      const double split = 0.5 * kResidualLogitScale * logit_derivative[action];
      forward_policy[action] += split;
      reflected_policy[kBoardSize - 1 - action] += split;
    }
    for (int head = 0; head < kValueHeads; ++head) {
      split_value[head] = 0.5 * value_derivative[head];
    }
    backwardRaw(prediction.forward, forward_policy, split_value, gradient);
    backwardRaw(prediction.reflected, reflected_policy, split_value, gradient);
  }

  void applyAdam(const std::vector<double>& gradient, std::size_t examples) {
    if (gradient.size() != parameters_.size() || examples == 0) {
      throw std::invalid_argument("invalid optimizer batch");
    }
    if (optimizer_step_ >= kMaximumOptimizerSteps) {
      throw std::runtime_error("optimizer step exceeds protocol maximum");
    }
    double maximum = 0;
    for (double value : gradient) {
      if (!std::isfinite(value)) throw std::runtime_error("non-finite gradient");
      const double normalized = value / static_cast<double>(examples);
      if (!std::isfinite(normalized)) {
        throw std::runtime_error("gradient normalization overflow");
      }
      maximum = std::max(maximum, std::abs(normalized));
    }
    double scaled_square_sum = 0;
    if (maximum != 0) {
      for (double value : gradient) {
        const double scaled =
            value / static_cast<double>(examples) / maximum;
        scaled_square_sum += scaled * scaled;
      }
    }
    const double clip = maximum == 0
        ? 1.0
        : std::min(1.0, kGradientNormClip / maximum /
                            std::sqrt(scaled_square_sum));
    if (!std::isfinite(clip) || clip < 0 || clip > 1) {
      throw std::runtime_error("invalid gradient clip scale");
    }
    const std::uint64_t next_step = optimizer_step_ + 1;
    const double correction1 = 1.0 - std::pow(kAdamBeta1, next_step);
    const double correction2 = 1.0 - std::pow(kAdamBeta2, next_step);
    for (std::size_t index = 0; index < parameters_.size(); ++index) {
      const double value = gradient[index] / examples * clip;
      const double first_value =
          kAdamBeta1 * first_moment_[index] + (1.0 - kAdamBeta1) * value;
      const double second_value =
          kAdamBeta2 * second_moment_[index] +
          (1.0 - kAdamBeta2) * value * value;
      if (!std::isfinite(first_value) || !std::isfinite(second_value) ||
          second_value < 0 ||
          std::abs(first_value) > std::numeric_limits<float>::max() ||
          second_value > std::numeric_limits<float>::max()) {
        throw std::runtime_error("optimizer moment update overflow");
      }
      proposed_first_[index] = static_cast<float>(first_value);
      proposed_second_[index] = static_cast<float>(second_value);
      const double first = proposed_first_[index] / correction1;
      const double second = proposed_second_[index] / correction2;
      const double proposed = parameters_[index] -
          kLearningRate * first / (std::sqrt(second) + kAdamEpsilon);
      if (!std::isfinite(proposed) ||
          std::abs(proposed) > std::numeric_limits<float>::max()) {
        throw std::runtime_error("optimizer produced a non-finite parameter");
      }
      proposed_parameters_[index] = static_cast<float>(proposed);
    }
    parameters_.swap(proposed_parameters_);
    first_moment_.swap(proposed_first_);
    second_moment_.swap(proposed_second_);
    optimizer_step_ = next_step;
  }

  std::vector<float>& parameters() { return parameters_; }
  const std::vector<float>& parameters() const { return parameters_; }
  std::vector<float>& firstMoment() { return first_moment_; }
  const std::vector<float>& firstMoment() const { return first_moment_; }
  std::vector<float>& secondMoment() { return second_moment_; }
  const std::vector<float>& secondMoment() const { return second_moment_; }
  std::uint64_t optimizerStep() const { return optimizer_step_; }
  void setOptimizerStep(std::uint64_t step) { optimizer_step_ = step; }

 private:
  std::vector<float> parameters_;
  std::vector<float> first_moment_;
  std::vector<float> second_moment_;
  std::vector<float> proposed_parameters_;
  std::vector<float> proposed_first_;
  std::vector<float> proposed_second_;
  std::uint64_t optimizer_step_ = 0;
};

// ---------------------------------------------------------------------------
// Frozen fair-D1 backbone and exact equivariant policy
// ---------------------------------------------------------------------------

std::array<double, kBoardSize> rawD1Values(const PublicState& source) {
  std::array<double, kBoardSize> values{};
  values.fill(-std::numeric_limits<double>::infinity());
  State state = materialize(source);
  state.game_over = false;
  d4::SearchContext context;
  for (int action = 0; action < kBoardSize; ++action) {
    if (!isLegal(state.board, action)) continue;
    const d4::ActionValue evaluated = d4::evaluateAction(state, action, 1, context);
    values[action] = evaluated.value;
  }
  return values;
}

std::array<double, kBoardSize> baseLogits(const PublicState& source) {
  const auto forward = rawD1Values(source);
  const auto reflected = rawD1Values(mirror(source));
  std::array<double, kBoardSize> symmetric{};
  std::array<double, kBoardSize> ordered{};
  int legal = 0;
  for (int action = 0; action < kBoardSize; ++action) {
    if (!isLegal(source.board, action)) {
      symmetric[action] = -std::numeric_limits<double>::infinity();
      continue;
    }
    symmetric[action] = 0.5 *
        (forward[action] + reflected[kBoardSize - 1 - action]);
    ordered[legal++] = symmetric[action];
  }
  if (legal == 0) throw std::runtime_error("D1 backbone saw no legal move");
  std::sort(ordered.begin(), ordered.begin() + legal);
  const double mean =
      std::accumulate(ordered.begin(), ordered.begin() + legal, 0.0) / legal;
  double variance = 0;
  for (int index = 0; index < legal; ++index) {
    variance += (ordered[index] - mean) * (ordered[index] - mean);
  }
  const double scale = std::sqrt(variance / legal + 1.0e-9);
  for (int action = 0; action < kBoardSize; ++action) {
    if (isLegal(source.board, action)) {
      symmetric[action] = kBaseLogitScale * (symmetric[action] - mean) / scale;
    }
  }
  return symmetric;
}

Prediction predict(const Network& network, const PublicState& state) {
  return network.predict(state, baseLogits(state));
}

int greedyAction(const Prediction& prediction) {
  int best = -1;
  for (int action : detail::kColumnOrder) {
    if ((prediction.legal_mask & (1u << action)) == 0) continue;
    if (best < 0 || prediction.probabilities[action] >
                        prediction.probabilities[best]) {
      best = action;
    }
  }
  if (best < 0) throw std::runtime_error("greedy policy had no legal action");
  return best;
}

int sampleAction(const Prediction& prediction, Mulberry32& random) {
  double draw = random.nextUnit();
  int last = -1;
  for (int action = 0; action < kBoardSize; ++action) {
    if ((prediction.legal_mask & (1u << action)) == 0) continue;
    if (prediction.probabilities[action] > 0) last = action;
    draw -= prediction.probabilities[action];
    if (prediction.probabilities[action] > 0 && draw <= 0) return action;
  }
  if (last < 0) throw std::runtime_error("sample policy had no legal action");
  return last;
}

// ---------------------------------------------------------------------------
// Five-move trajectories and primal-dual targets
// ---------------------------------------------------------------------------

struct Step {
  PublicState state{};
  std::array<double, kBoardSize> base_logits{};
  int action = -1;
  double old_log_probability = 0;
  double old_entropy = 0;
  std::array<double, kValueHeads> old_values{};
  double score_reward = 0;
  double lifetime_reward = 0;
  double regeneration_reward = 0;
  int numbered_cleared = 0;
  int covers_revealed = 0;
  bool terminal_after = false;
};

struct Trajectory {
  std::uint32_t episode_seed = 0;
  bool calibration = false;
  bool restart = false;
  bool censored = false;
  std::vector<Step> steps;
  PublicState final_state{};
  std::int64_t observed_score = 0;
  int observed_moves = 0;
  std::uint64_t numbered_cleared = 0;
  std::uint64_t covers_revealed = 0;
  std::vector<PublicState> reservoir_candidates;
};

struct Moment {
  double sum = 0;
  double square_sum = 0;
  std::uint64_t count = 0;

  void add(double value) {
    if (!std::isfinite(value)) throw std::runtime_error("non-finite moment");
    sum += value;
    square_sum += value * value;
    ++count;
  }
  void merge(const Moment& other) {
    sum += other.sum;
    square_sum += other.square_sum;
    count += other.count;
  }
  double mean() const { return count == 0 ? 0 : sum / count; }
  double variance() const {
    if (count < 2) return 0;
    return std::max(0.0, (square_sum - sum * sum / count) / (count - 1));
  }
  double upper95() const {
    if (count < 2) return std::numeric_limits<double>::infinity();
    return mean() + 1.96 * std::sqrt(variance() / count);
  }
};

struct ConstraintMoments {
  std::array<Moment, 4> drift{};
  Moment terminal_rate{};
  Moment episode_score{};
  Moment episode_moves{};
  Moment entropy{};

  void merge(const ConstraintMoments& other) {
    for (int index = 0; index < 4; ++index) drift[index].merge(other.drift[index]);
    terminal_rate.merge(other.terminal_rate);
    episode_score.merge(other.episode_score);
    episode_moves.merge(other.episode_moves);
    entropy.merge(other.entropy);
  }
};

struct DualState {
  std::array<double, 4> constraint{{0, 0, 0, 0}};
  double terminal = kTerminalInitialLambda;
};

struct Sample {
  PublicState state{};
  std::array<double, kBoardSize> base_logits{};
  int action = -1;
  double old_log_probability = 0;
  std::array<double, kValueHeads> targets{};
  std::array<double, kValueHeads> old_values{};
  std::array<bool, kValueHeads> value_mask{};
  double policy_advantage = 0;
  double trajectory_weight = 1;
};

int constraintIndexForHead(int head) {
  if (head < kOccupancyMediumHead || head > kCoverHighHead) return -1;
  return head - kOccupancyMediumHead;
}

bool calibrationGame(std::uint32_t seed) {
  requireSeed(seed, SeedUse::kTraining);
  const std::uint32_t offset = seed - kTrainingLane.first;
  const int iteration = static_cast<int>(offset / kEpisodesPerIteration);
  const int episode = static_cast<int>(offset % kEpisodesPerIteration);
  return episode < kInitialEpisodesPerIteration &&
         (iteration == kIterations - 1 ||
          mix32(seed ^ kCalibrationDomain) % 5u == 0);
}

std::uint32_t syntheticRestartStream(std::uint32_t episode_seed) {
  requireSeed(episode_seed, SeedUse::kTraining);
  // An odd affine permutation modulo 2^17 is injective across all 131,072
  // episode IDs. Synthetic continuations occupy an explicit disjoint lane.
  const std::uint32_t offset = episode_seed - kTrainingLane.first;
  const std::uint32_t permuted =
      (offset * 0x1f123u + (kRestartDomain & 0x1ffffu)) & 0x1ffffu;
  return 0x2e00'0000u + permuted;
}

Trajectory rollout(const Network& network, std::uint32_t episode_seed,
                   const std::optional<PublicState>& restart_state,
                   int maximum_moves, const Deadline* deadline = nullptr,
                   bool stochastic_policy = true) {
  requireSeed(episode_seed, SeedUse::kTraining);
  Trajectory trajectory;
  trajectory.episode_seed = episode_seed;
  trajectory.restart = restart_state.has_value();
  trajectory.calibration = !trajectory.restart && calibrationGame(episode_seed);
  State state = restart_state ? materialize(*restart_state)
                              : initialHeadlessState(episode_seed);
  const std::uint32_t stream_seed = restart_state
                                        ? syntheticRestartStream(episode_seed)
                                        : episode_seed;
  Mulberry32 policy_random(mix32(episode_seed ^ kPolicyDomain));
  trajectory.steps.reserve(static_cast<std::size_t>(maximum_moves));

  while (!state.game_over &&
         static_cast<int>(trajectory.steps.size()) < maximum_moves) {
    const PublicState visible = publicState(state);
    const auto backbone = baseLogits(visible);
    const Prediction prediction = network.predict(visible, backbone);
    const int action = stochastic_policy ? sampleAction(prediction, policy_random)
                                         : greedyAction(prediction);
    Step step;
    step.state = visible;
    step.base_logits = backbone;
    step.action = action;
    step.old_log_probability = prediction.log_probabilities[action];
    for (int candidate = 0; candidate < kBoardSize; ++candidate) {
      const double probability = prediction.probabilities[candidate];
      if (probability > 0) step.old_entropy -= probability * std::log(probability);
    }
    step.old_values = prediction.values;
    if (!trajectory.calibration && !trajectory.restart &&
        visible.phase == kMovesPerLevel &&
        (occupancy(visible.board) >= kMediumOccupancyMinimum ||
         covered(visible.board) >= kMediumCoverMinimum)) {
      trajectory.reservoir_candidates.push_back(visible);
    }
    MoveResult move;
    if (!playHeadlessMove(state, stream_seed, action, move)) {
      throw std::runtime_error("exact rollout transition failed");
    }
    step.score_reward = move.score_delta / kScoreScale;
    step.lifetime_reward = 1.0 / kLifetimeScale;
    for (const Wave& wave : move.waves) {
      step.numbered_cleared += wave.cleared;
      step.covers_revealed += wave.revealed;
    }
    step.regeneration_reward = 0.5 *
        (step.numbered_cleared / kRegenerationClearScale +
         step.covers_revealed / kRegenerationRevealScale);
    step.terminal_after = state.game_over;
    trajectory.numbered_cleared += step.numbered_cleared;
    trajectory.covers_revealed += step.covers_revealed;
    trajectory.steps.push_back(step);
    if (deadline && (trajectory.steps.size() & 31u) == 0) deadline->check();
  }
  trajectory.censored = !state.game_over;
  trajectory.final_state = publicState(state);
  trajectory.observed_score = state.score;
  trajectory.observed_moves = static_cast<int>(trajectory.steps.size());
  return trajectory;
}

struct CycleTarget {
  std::array<double, 4> costs{};
  std::array<bool, 4> masks{};
  double terminal_cost = 0;
  bool terminal_mask = false;
};

CycleTarget absoluteCycleDrift(const PublicState& first,
                               const PublicState& after) {
  CycleTarget result;
  const int initial_occupancy = occupancy(first.board);
  const int final_occupancy = occupancy(after.board);
  const int initial_covers = covered(first.board);
  const int final_covers = covered(after.board);
  const double occupancy_drift = final_occupancy - initial_occupancy;
  const double cover_drift = final_covers - initial_covers;
  if (initial_occupancy >= kMediumOccupancyMinimum &&
      initial_occupancy < kHighOccupancyMinimum) {
    result.masks[0] = true;
    result.costs[0] = occupancy_drift + kMediumOccupancyMargin;
  }
  if (initial_occupancy >= kHighOccupancyMinimum) {
    result.masks[1] = true;
    result.costs[1] = occupancy_drift + kHighOccupancyMargin;
  }
  if (initial_covers >= kMediumCoverMinimum &&
      initial_covers < kHighCoverMinimum) {
    result.masks[2] = true;
    result.costs[2] = cover_drift + kMediumCoverMargin;
  }
  if (initial_covers >= kHighCoverMinimum) {
    result.masks[3] = true;
    result.costs[3] = cover_drift + kHighCoverMargin;
  }
  return result;
}

std::vector<CycleTarget> cycleTargets(const Trajectory& trajectory,
                                      ConstraintMoments& moments) {
  std::vector<CycleTarget> targets(trajectory.steps.size());
  for (std::size_t start = 0; start < trajectory.steps.size();) {
    if (trajectory.steps[start].state.phase != kMovesPerLevel) {
      ++start;
      continue;
    }
    const std::size_t end = std::min(start + kMacroMoves, trajectory.steps.size());
    const PublicState after = end < trajectory.steps.size()
                                  ? trajectory.steps[end].state
                                  : trajectory.final_state;
    CycleTarget target = absoluteCycleDrift(trajectory.steps[start].state, after);
    const bool censored_partial =
        trajectory.censored && end == trajectory.steps.size() &&
        end - start < kMacroMoves;
    if (!censored_partial) {
      const double terminal =
          trajectory.steps[end - 1].terminal_after ? 1.0 : 0.0;
      moments.terminal_rate.add(terminal);
      target.terminal_cost = terminal;
      target.terminal_mask = true;
    }
    // Partial cycles are exact observations but not comparable to a five-move
    // constraint. They never enter a dual denominator or critic target.
    if (end - start == kMacroMoves) {
      for (int constraint = 0; constraint < 4; ++constraint) {
        if (target.masks[constraint]) moments.drift[constraint].add(
            target.costs[constraint]);
      }
      for (std::size_t index = start; index < end; ++index) targets[index] = target;
    } else if (!censored_partial) {
      for (std::size_t index = start; index < end; ++index) {
        targets[index].terminal_cost = target.terminal_cost;
        targets[index].terminal_mask = target.terminal_mask;
      }
    }
    start = end;
  }
  return targets;
}

double trajectoryRankKey(const Trajectory& trajectory) {
  // Both fields are observed lower bounds. Censored games receive no imagined
  // post-cap score or lifetime.
  return trajectory.observed_moves * 10'000.0 + trajectory.observed_score;
}

std::vector<Sample> prepareSamples(const std::vector<Trajectory>& trajectories,
                                   const DualState& dual,
                                   ConstraintMoments& training_moments,
                                   ConstraintMoments& calibration_moments) {
  std::array<std::vector<double>, 2> training_outcomes;
  for (const Trajectory& trajectory : trajectories) {
    if (!trajectory.calibration) {
      training_outcomes[trajectory.restart ? 1 : 0].push_back(
          trajectoryRankKey(trajectory));
    }
  }
  std::array<double, 2> tail_cutoff{};
  for (int cohort = 0; cohort < 2; ++cohort) {
    std::sort(training_outcomes[cohort].begin(),
              training_outcomes[cohort].end());
    tail_cutoff[cohort] = training_outcomes[cohort].empty()
        ? -std::numeric_limits<double>::infinity()
        : training_outcomes[cohort][training_outcomes[cohort].size() / 4];
  }

  std::vector<Sample> samples;
  for (const Trajectory& trajectory : trajectories) {
    ConstraintMoments local;
    const auto cycles = cycleTargets(trajectory, local);
    local.episode_score.add(static_cast<double>(trajectory.observed_score));
    local.episode_moves.add(trajectory.observed_moves);
    for (const Step& step : trajectory.steps) local.entropy.add(step.old_entropy);
    const auto add_game_estimands = [&](ConstraintMoments& destination) {
      for (int constraint = 0; constraint < 4; ++constraint) {
        if (local.drift[constraint].count != 0) {
          destination.drift[constraint].add(
              local.drift[constraint].mean());
        }
      }
      if (local.terminal_rate.count != 0) {
        destination.terminal_rate.add(local.terminal_rate.mean());
      }
      destination.episode_score.add(
          static_cast<double>(trajectory.observed_score));
      destination.episode_moves.add(trajectory.observed_moves);
      if (local.entropy.count != 0) {
        destination.entropy.add(local.entropy.mean());
      }
    };
    if (trajectory.calibration) {
      // Both dual updates and confidence bounds use the same independent-game
      // estimand; correlated cycles never inflate either denominator.
      add_game_estimands(calibration_moments);
      continue;  // Whole games are reserved before any transition is trained.
    }
    add_game_estimands(training_moments);
    const double tail_weight =
        trajectoryRankKey(trajectory) <= tail_cutoff[trajectory.restart ? 1 : 0]
                                   ? kTailPolicyWeight
                                   : 1.0;
    const std::size_t length = trajectory.steps.size();
    for (std::size_t at = 0; at < length; ++at) {
      const Step& step = trajectory.steps[at];
      Sample sample;
      sample.state = step.state;
      sample.base_logits = step.base_logits;
      sample.action = step.action;
      sample.old_log_probability = step.old_log_probability;
      sample.old_values = step.old_values;
      sample.trajectory_weight = 1.0;
      sample.value_mask.fill(true);

      const std::size_t end = std::min(length, at + kMacroMoves);
      double discount = 1;
      for (std::size_t future = at; future < end; ++future) {
        sample.targets[kScoreHead] +=
            discount * trajectory.steps[future].score_reward;
        sample.targets[kLifetimeHead] +=
            discount * trajectory.steps[future].lifetime_reward;
        sample.targets[kRegenerationHead] +=
            discount * trajectory.steps[future].regeneration_reward;
        if (trajectory.steps[future].terminal_after) {
          discount = 0;
          break;
        }
        discount *= kGamma;
      }
      if (discount != 0 && end < length) {
        for (int head : {kScoreHead, kLifetimeHead, kRegenerationHead}) {
          sample.targets[head] +=
              discount * trajectory.steps[end].old_values[head];
        }
      } else if (discount != 0 && end == length && !trajectory.censored &&
                 !trajectory.final_state.terminal) {
        throw std::runtime_error("uncensored trajectory ended nonterminal");
      }
      // Terminal cost is the same undiscounted phase-aligned five-move event
      // used by the dual denominator. Censored partial cycles are masked.
      sample.targets[kTerminalHead] = cycles[at].terminal_cost;
      sample.value_mask[kTerminalHead] = cycles[at].terminal_mask;

      for (int constraint = 0; constraint < 4; ++constraint) {
        const int head = kOccupancyMediumHead + constraint;
        sample.value_mask[head] = cycles[at].masks[constraint];
        sample.targets[head] = cycles[at].costs[constraint];
      }

      const double score_advantage =
          sample.targets[kScoreHead] - sample.old_values[kScoreHead];
      const double lifetime_advantage =
          sample.targets[kLifetimeHead] - sample.old_values[kLifetimeHead];
      const double terminal_advantage = sample.value_mask[kTerminalHead]
          ? sample.targets[kTerminalHead] - sample.old_values[kTerminalHead]
          : 0;
      const double regeneration_advantage =
          sample.targets[kRegenerationHead] -
          sample.old_values[kRegenerationHead];
      const double benefit_advantage =
          score_advantage + kLifetimeAdvantageWeight * lifetime_advantage +
          kRegenerationAdvantageWeight * regeneration_advantage;
      sample.policy_advantage = tail_weight * benefit_advantage;
      if (sample.value_mask[kTerminalHead] &&
          local.terminal_rate.count != 0) {
        sample.policy_advantage -= dual.terminal * terminal_advantage /
                                   local.terminal_rate.count;
      }
      for (int constraint = 0; constraint < 4; ++constraint) {
        const int head = kOccupancyMediumHead + constraint;
        if (sample.value_mask[head] && local.drift[constraint].count != 0) {
          sample.policy_advantage -=
              dual.constraint[constraint] *
              (sample.targets[head] - sample.old_values[head]) /
              local.drift[constraint].count;
        }
      }
      samples.push_back(std::move(sample));
    }
  }
  return samples;
}

void normalizeAdvantages(std::vector<Sample>& samples) {
  if (samples.empty()) return;
  double mean = 0;
  for (const Sample& sample : samples) mean += sample.policy_advantage;
  mean /= samples.size();
  double variance = 0;
  for (const Sample& sample : samples) {
    variance += (sample.policy_advantage - mean) *
                (sample.policy_advantage - mean);
  }
  const double scale = std::sqrt(variance / samples.size() + 1.0e-8);
  for (Sample& sample : samples) {
    sample.policy_advantage = (sample.policy_advantage - mean) / scale;
  }
}

void updateDuals(DualState& dual, const ConstraintMoments& training) {
  for (int constraint = 0; constraint < 4; ++constraint) {
    // No support means no update: an inactive mask cannot silently create or
    // decay a multiplier.
    if (training.drift[constraint].count == 0) continue;
    dual.constraint[constraint] = std::clamp(
        dual.constraint[constraint] +
            kDualLearningRate * training.drift[constraint].mean(),
        0.0, kConstraintLambdaMaximum);
  }
  if (training.terminal_rate.count != 0) {
    dual.terminal = std::clamp(
        dual.terminal + kDualLearningRate *
                            (training.terminal_rate.mean() - kTerminalRiskLimit),
        0.0, kTerminalLambdaMaximum);
  }
}

double batchLossAndGradient(const Network& network,
                            const std::vector<Sample>& samples,
                            const std::vector<std::size_t>& order,
                            std::size_t begin, std::size_t end,
                            std::vector<double>* gradient) {
  if (begin >= end || end > order.size()) {
    throw std::invalid_argument("invalid PPO minibatch range");
  }
  if (gradient) gradient->assign(Layout::count, 0.0);
  double loss = 0;
  for (std::size_t position = begin; position < end; ++position) {
    const Sample& sample = samples[order[position]];
    const Prediction prediction =
        network.predict(sample.state, sample.base_logits);
    const double log_probability =
        prediction.log_probabilities[sample.action];
    const double raw_log_ratio =
        log_probability - sample.old_log_probability;
    const double clipped_log_ratio = std::clamp(raw_log_ratio, -20.0, 20.0);
    const bool log_ratio_saturated = clipped_log_ratio != raw_log_ratio;
    const double ratio = std::exp(clipped_log_ratio);
    const double clipped_ratio =
        std::clamp(ratio, 1.0 - kClipRatio, 1.0 + kClipRatio);
    const double plain = ratio * sample.policy_advantage;
    const double clipped = clipped_ratio * sample.policy_advantage;
    const bool plain_selected = plain <= clipped + 1.0e-15;
    loss -= sample.trajectory_weight * std::min(plain, clipped);

    std::array<double, kBoardSize> logit_derivative{};
    if (gradient && plain_selected && !log_ratio_saturated) {
      const double selected_derivative =
          -sample.trajectory_weight * ratio * sample.policy_advantage;
      for (int action = 0; action < kBoardSize; ++action) {
        logit_derivative[action] += selected_derivative *
            ((action == sample.action ? 1.0 : 0.0) -
             prediction.probabilities[action]);
      }
    }

    double entropy = 0;
    for (double candidate : prediction.probabilities) {
      if (candidate > 0) entropy -= candidate * std::log(candidate);
    }
    loss -= kEntropyCoefficient * entropy;
    if (gradient) {
      for (int action = 0; action < kBoardSize; ++action) {
        const double candidate = prediction.probabilities[action];
        if (candidate > 0) {
          logit_derivative[action] += kEntropyCoefficient * candidate *
              (std::log(candidate) + entropy);
        }
      }
    }

    std::array<double, kValueHeads> value_derivative{};
    for (int head = 0; head < kValueHeads; ++head) {
      if (!sample.value_mask[head]) continue;
      const double error = prediction.values[head] - sample.targets[head];
      const double absolute = std::abs(error);
      const double huber = absolute <= 1.0
                               ? 0.5 * error * error
                               : absolute - 0.5;
      loss += kValueCoefficient * huber;
      if (gradient) {
        value_derivative[head] = kValueCoefficient *
            (absolute <= 1.0 ? error : std::copysign(1.0, error));
      }
    }
    if (gradient) {
      network.backward(prediction, logit_derivative, value_derivative,
                       *gradient);
    }
  }
  const double examples = static_cast<double>(end - begin);
  if (gradient) {
    for (double& value : *gradient) value /= examples;
  }
  return loss / examples;
}

void deterministicShuffle(std::vector<std::size_t>& order,
                          std::uint32_t seed) {
  Mulberry32 random(seed);
  for (std::size_t remaining = order.size(); remaining > 1; --remaining) {
    const std::size_t swap = static_cast<std::size_t>(
        (static_cast<std::uint64_t>(random.nextBits()) * remaining) >> 32u);
    std::swap(order[remaining - 1], order[swap]);
  }
}

struct OptimizerStats {
  double final_loss = 0;
  std::uint64_t batches = 0;
  std::uint64_t examples = 0;
};

OptimizerStats optimize(Network& network, std::vector<Sample>& samples,
                        int iteration, const Deadline& deadline) {
  if (samples.empty()) throw std::runtime_error("iteration has no training data");
  normalizeAdvantages(samples);
  std::vector<std::size_t> order(samples.size());
  std::iota(order.begin(), order.end(), 0);
  OptimizerStats result;
  for (int epoch = 0; epoch < kPpoEpochs; ++epoch) {
    deterministicShuffle(order, mix32(kShuffleDomain ^
        static_cast<std::uint32_t>(iteration * kPpoEpochs + epoch)));
    for (std::size_t begin = 0; begin < order.size(); begin += kMinibatch) {
      const std::size_t end = std::min(order.size(), begin + kMinibatch);
      std::vector<double> gradient;
      result.final_loss =
          batchLossAndGradient(network, samples, order, begin, end, &gradient);
      network.applyAdam(gradient, 1);
      ++result.batches;
      result.examples += end - begin;
      if ((result.batches & 31u) == 0) deadline.check();
    }
  }
  return result;
}

// ---------------------------------------------------------------------------
// Deterministic, resumable checkpoint
// ---------------------------------------------------------------------------

constexpr std::uint64_t kCheckpointMagic = 0x4437'5044'4143'3031ull;
constexpr std::uint32_t kCheckpointVersion = 2;
constexpr std::uint64_t kConfigurationFingerprint =
    0x8a3e'9471'2bc5'0d6full;

class ByteWriter {
 public:
  template <typename T>
  void integer(T value) {
    static_assert(std::is_integral_v<T>);
    using U = std::make_unsigned_t<T>;
    U bits = static_cast<U>(value);
    for (std::size_t byte = 0; byte < sizeof(T); ++byte) {
      bytes_.push_back(static_cast<std::uint8_t>(bits >> (8u * byte)));
    }
  }
  void floating(float value) { integer(std::bit_cast<std::uint32_t>(value)); }
  void floating(double value) { integer(std::bit_cast<std::uint64_t>(value)); }
  void text(std::string_view value) {
    integer<std::uint32_t>(static_cast<std::uint32_t>(value.size()));
    bytes_.insert(bytes_.end(), value.begin(), value.end());
  }
  void publicStateValue(const PublicState& state) {
    bytes_.insert(bytes_.end(), state.board.begin(), state.board.end());
    integer(state.next_disc);
    integer(state.phase);
    integer<std::uint8_t>(state.terminal ? 1 : 0);
  }
  const std::vector<std::uint8_t>& bytes() const { return bytes_; }
  std::vector<std::uint8_t>& bytes() { return bytes_; }

 private:
  std::vector<std::uint8_t> bytes_;
};

class ByteReader {
 public:
  explicit ByteReader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}
  template <typename T>
  T integer() {
    static_assert(std::is_integral_v<T>);
    if (remaining() < sizeof(T)) throw std::runtime_error("checkpoint truncated");
    using U = std::make_unsigned_t<T>;
    U value = 0;
    for (std::size_t byte = 0; byte < sizeof(T); ++byte) {
      value |= static_cast<U>(bytes_[position_++]) << (8u * byte);
    }
    return static_cast<T>(value);
  }
  float floatValue() { return std::bit_cast<float>(integer<std::uint32_t>()); }
  double doubleValue() { return std::bit_cast<double>(integer<std::uint64_t>()); }
  std::string text() {
    const auto size = integer<std::uint32_t>();
    if (remaining() < size) throw std::runtime_error("checkpoint text truncated");
    std::string result(reinterpret_cast<const char*>(bytes_.data() + position_),
                       size);
    position_ += size;
    return result;
  }
  PublicState publicStateValue() {
    if (remaining() < kCellCount + 3u) {
      throw std::runtime_error("checkpoint public state truncated");
    }
    PublicState state;
    std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(position_),
                kCellCount, state.board.begin());
    position_ += kCellCount;
    state.next_disc = integer<std::uint8_t>();
    state.phase = integer<std::uint8_t>();
    state.terminal = integer<std::uint8_t>() != 0;
    (void)materialize(state);  // Structural validation at the trust boundary.
    return state;
  }
  std::size_t remaining() const { return bytes_.size() - position_; }

 private:
  const std::vector<std::uint8_t>& bytes_;
  std::size_t position_ = 0;
};

std::uint64_t fnv64(const std::uint8_t* data, std::size_t size) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= data[index];
    hash *= 0x0000'0100'0000'01b3ull;
  }
  return hash;
}

void writeMoment(ByteWriter& writer, const Moment& moment) {
  writer.floating(moment.sum);
  writer.floating(moment.square_sum);
  writer.integer(moment.count);
}

Moment readMoment(ByteReader& reader) {
  Moment result;
  result.sum = reader.doubleValue();
  result.square_sum = reader.doubleValue();
  result.count = reader.integer<std::uint64_t>();
  if (!std::isfinite(result.sum) || !std::isfinite(result.square_sum) ||
      result.square_sum < 0 || result.count > kMaximumTrainingMoves) {
    throw std::runtime_error("checkpoint moment is invalid");
  }
  return result;
}

void writeConstraintMoments(ByteWriter& writer,
                            const ConstraintMoments& moments) {
  for (const Moment& moment : moments.drift) writeMoment(writer, moment);
  writeMoment(writer, moments.terminal_rate);
  writeMoment(writer, moments.episode_score);
  writeMoment(writer, moments.episode_moves);
  writeMoment(writer, moments.entropy);
}

ConstraintMoments readConstraintMoments(ByteReader& reader) {
  ConstraintMoments result;
  for (Moment& moment : result.drift) moment = readMoment(reader);
  result.terminal_rate = readMoment(reader);
  result.episode_score = readMoment(reader);
  result.episode_moves = readMoment(reader);
  result.entropy = readMoment(reader);
  return result;
}

struct TrainingState {
  Network network{};
  DualState dual{};
  int completed_iterations = 0;
  bool trusted = false;
  double cumulative_wall_seconds = 0;
  std::uint64_t reservoir_seen = 0;
  std::vector<PublicState> reservoir;
  std::array<ConstraintMoments, kCalibrationWindowIterations> calibration{};
  std::string source_sha256;
};

void validateTrainingState(const TrainingState& state) {
  if (state.completed_iterations < 0 || state.completed_iterations > kIterations ||
      state.reservoir.size() > kReservoirCapacity ||
      state.reservoir_seen < state.reservoir.size() ||
      !isSha256(state.source_sha256) ||
      !std::isfinite(state.cumulative_wall_seconds) ||
      state.cumulative_wall_seconds < 0 ||
      state.cumulative_wall_seconds > kMaximumWallSeconds) {
    throw std::runtime_error("checkpoint training state is invalid");
  }
  if (state.network.optimizerStep() > kMaximumOptimizerSteps) {
    throw std::runtime_error("checkpoint optimizer step is impossible");
  }
  for (double value : state.dual.constraint) {
    if (!std::isfinite(value) || value < 0 ||
        value > kConstraintLambdaMaximum) {
      throw std::runtime_error("checkpoint constraint multiplier is invalid");
    }
  }
  if (!std::isfinite(state.dual.terminal) || state.dual.terminal < 0 ||
      state.dual.terminal > kTerminalLambdaMaximum) {
    throw std::runtime_error("checkpoint terminal multiplier is invalid");
  }
  for (float parameter : state.network.parameters()) {
    if (!std::isfinite(parameter)) throw std::runtime_error("non-finite model");
  }
  for (float moment : state.network.firstMoment()) {
    if (!std::isfinite(moment)) {
      throw std::runtime_error("non-finite optimizer first moment");
    }
  }
  for (float moment : state.network.secondMoment()) {
    if (!std::isfinite(moment) || moment < 0) {
      throw std::runtime_error("invalid optimizer second moment");
    }
  }
  for (const PublicState& restart : state.reservoir) {
    validatePublicState(restart);
    if (restart.terminal) {
      throw std::runtime_error("restart reservoir contains a terminal state");
    }
  }
  if (state.trusted && state.completed_iterations != kIterations) {
    throw std::runtime_error("partial checkpoint cannot be trusted");
  }
}

std::vector<std::uint8_t> serializeCheckpoint(const TrainingState& state) {
  validateTrainingState(state);
  ByteWriter writer;
  writer.integer(kCheckpointMagic);
  writer.integer(kCheckpointVersion);
  writer.integer(kConfigurationFingerprint);
  writer.text(state.source_sha256);
  writer.integer<std::uint32_t>(state.completed_iterations);
  writer.integer<std::uint8_t>(state.trusted ? 1 : 0);
  writer.floating(state.cumulative_wall_seconds);
  writer.integer(state.network.optimizerStep());
  for (double value : state.dual.constraint) writer.floating(value);
  writer.floating(state.dual.terminal);
  writer.integer(state.reservoir_seen);
  writer.integer<std::uint32_t>(static_cast<std::uint32_t>(state.reservoir.size()));
  for (const PublicState& value : state.reservoir) writer.publicStateValue(value);
  for (const ConstraintMoments& value : state.calibration) {
    writeConstraintMoments(writer, value);
  }
  const auto writeVector = [&](const std::vector<float>& values) {
    writer.integer<std::uint32_t>(static_cast<std::uint32_t>(values.size()));
    for (float value : values) writer.floating(value);
  };
  writeVector(state.network.parameters());
  writeVector(state.network.firstMoment());
  writeVector(state.network.secondMoment());
  const std::uint64_t checksum = fnv64(writer.bytes().data(), writer.bytes().size());
  writer.integer(checksum);
  return writer.bytes();
}

TrainingState deserializeCheckpoint(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < sizeof(std::uint64_t)) {
    throw std::runtime_error("checkpoint is too small");
  }
  const std::size_t payload_size = bytes.size() - sizeof(std::uint64_t);
  ByteReader footer(bytes);
  // Read the checksum without trusting a native-layout reinterpret cast.
  for (std::size_t ignored = 0; ignored < payload_size; ++ignored) {
    (void)footer.integer<std::uint8_t>();
  }
  const std::uint64_t expected = footer.integer<std::uint64_t>();
  if (fnv64(bytes.data(), payload_size) != expected) {
    throw std::runtime_error("checkpoint checksum mismatch");
  }
  std::vector<std::uint8_t> payload(bytes.begin(),
                                    bytes.begin() +
                                        static_cast<std::ptrdiff_t>(payload_size));
  ByteReader reader(payload);
  if (reader.integer<std::uint64_t>() != kCheckpointMagic ||
      reader.integer<std::uint32_t>() != kCheckpointVersion ||
      reader.integer<std::uint64_t>() != kConfigurationFingerprint) {
    throw std::runtime_error("checkpoint schema mismatch");
  }
  TrainingState result;
  result.source_sha256 = reader.text();
  result.completed_iterations = reader.integer<std::uint32_t>();
  result.trusted = reader.integer<std::uint8_t>() != 0;
  result.cumulative_wall_seconds = reader.doubleValue();
  result.network.setOptimizerStep(reader.integer<std::uint64_t>());
  for (double& value : result.dual.constraint) value = reader.doubleValue();
  result.dual.terminal = reader.doubleValue();
  result.reservoir_seen = reader.integer<std::uint64_t>();
  const auto reservoir_size = reader.integer<std::uint32_t>();
  if (reservoir_size > kReservoirCapacity) {
    throw std::runtime_error("checkpoint reservoir exceeds capacity");
  }
  result.reservoir.reserve(reservoir_size);
  for (std::uint32_t index = 0; index < reservoir_size; ++index) {
    result.reservoir.push_back(reader.publicStateValue());
  }
  for (ConstraintMoments& value : result.calibration) {
    value = readConstraintMoments(reader);
  }
  const auto readVector = [&](std::vector<float>& destination) {
    const auto size = reader.integer<std::uint32_t>();
    if (size != Layout::count) throw std::runtime_error("model size mismatch");
    for (float& value : destination) value = reader.floatValue();
  };
  readVector(result.network.parameters());
  readVector(result.network.firstMoment());
  readVector(result.network.secondMoment());
  if (reader.remaining() != 0) throw std::runtime_error("checkpoint has trailing data");
  validateTrainingState(result);
  return result;
}

std::vector<std::uint8_t> readFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not open checkpoint " + path);
  input.seekg(0, std::ios::end);
  const auto end = input.tellg();
  if (end < 0 || static_cast<std::uint64_t>(end) > kMaximumCheckpointBytes) {
    throw std::runtime_error("checkpoint has invalid size");
  }
  input.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> result(static_cast<std::size_t>(end));
  input.read(reinterpret_cast<char*>(result.data()),
             static_cast<std::streamsize>(result.size()));
  if (!input) throw std::runtime_error("checkpoint read failed");
  return result;
}

void writeCheckpointAtomic(const std::string& path,
                           const TrainingState& state) {
  const auto bytes = serializeCheckpoint(state);
  if (bytes.size() > kMaximumCheckpointBytes) {
    throw std::runtime_error("checkpoint exceeds 16 MiB cap");
  }
  const std::string temporary = path + ".partial";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("could not create checkpoint temp file");
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output) throw std::runtime_error("checkpoint write failed");
  }
  std::filesystem::rename(temporary, path);
}

TrainingState readCheckpoint(const std::string& path) {
  return deserializeCheckpoint(readFile(path));
}

void updateReservoir(TrainingState& state,
                     const std::vector<Trajectory>& trajectories) {
  for (const Trajectory& trajectory : trajectories) {
    if (trajectory.calibration || trajectory.restart) continue;
    for (const PublicState& candidate : trajectory.reservoir_candidates) {
      ++state.reservoir_seen;
      if (state.reservoir.size() < kReservoirCapacity) {
        state.reservoir.push_back(candidate);
        continue;
      }
      const std::uint64_t mixed =
          (static_cast<std::uint64_t>(mix32(
               static_cast<std::uint32_t>(state.reservoir_seen) ^
               kReservoirDomain)) << 32u) |
          mix32(static_cast<std::uint32_t>(state.reservoir_seen >> 32u) ^
                kReservoirDomain);
      const std::uint64_t slot = mixed % state.reservoir_seen;
      if (slot < kReservoirCapacity) {
        state.reservoir[static_cast<std::size_t>(slot)] = candidate;
      }
    }
  }
}

ConstraintMoments calibrationWindow(const TrainingState& state) {
  ConstraintMoments result;
  for (const ConstraintMoments& value : state.calibration) result.merge(value);
  return result;
}

constexpr std::uint64_t kMinimumCalibrationSupportPerConstraint = 256;

bool calibrationPasses(const TrainingState& state) {
  if (state.completed_iterations != kIterations) return false;
  const ConstraintMoments& final = state.calibration[
      (state.completed_iterations - 1) % kCalibrationWindowIterations];
  if (final.episode_score.count != kInitialEpisodesPerIteration ||
      final.episode_moves.count != kInitialEpisodesPerIteration) return false;
  for (const Moment& drift : final.drift) {
    if (drift.count < kMinimumCalibrationSupportPerConstraint ||
        drift.upper95() > 0.0) return false;
  }
  return final.terminal_rate.count >= 384 &&
         final.terminal_rate.upper95() <= kTerminalRiskLimit &&
         final.entropy.count == kInitialEpisodesPerIteration &&
         final.entropy.mean() >= 0.05 &&
         std::isfinite(final.episode_score.mean()) &&
         std::isfinite(final.episode_moves.mean());
}

std::uint32_t trainingSeed(int iteration, int episode) {
  if (iteration < 0 || iteration >= kIterations || episode < 0 ||
      episode >= kEpisodesPerIteration) {
    throw std::invalid_argument("training seed coordinates out of range");
  }
  const std::uint64_t offset =
      static_cast<std::uint64_t>(iteration) * kEpisodesPerIteration + episode;
  const std::uint32_t seed = static_cast<std::uint32_t>(kTrainingLane.first + offset);
  requireSeed(seed, SeedUse::kTraining);
  if (iteration == kIterations - 1 && episode == kEpisodesPerIteration - 1 &&
      seed != kTrainingLane.last) {
    throw std::logic_error("training lane is not consumed exactly");
  }
  return seed;
}

template <typename Function>
void parallelIndices(int count, Function function) {
  std::atomic<int> next{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kWorkers, count); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int index = next.fetch_add(1);
        if (index >= count) return;
        function(index);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
}

std::vector<PublicState> iterationRestartPool(
    const std::vector<PublicState>& existing,
    const std::vector<Trajectory>& initial) {
  if (!existing.empty()) return existing;
  std::vector<PublicState> result;
  result.reserve(kReservoirCapacity);
  for (const Trajectory& trajectory : initial) {
    if (trajectory.calibration) continue;
    for (const PublicState& candidate : trajectory.reservoir_candidates) {
      if (result.size() == kReservoirCapacity) return result;
      result.push_back(candidate);
    }
  }
  if (result.empty()) {
    throw std::runtime_error(
        "iteration zero produced no non-calibration public restart states");
  }
  return result;
}

std::vector<Trajectory> collectIteration(const TrainingState& state,
                                         int iteration,
                                         const Deadline& deadline) {
  std::vector<Trajectory> initial(kInitialEpisodesPerIteration);
  // Whole initial games are generated first so iteration zero can construct a
  // public-state restart pool without sharing futures or trajectory fragments.
  parallelIndices(kInitialEpisodesPerIteration, [&](int local) {
    const std::uint32_t seed = trainingSeed(iteration, local);
    if (calibrationGame(seed)) return;  // Reserved for the post-update network.
    initial[local] = rollout(state.network, seed, std::nullopt,
                             kInitialMaximumMoves, &deadline);
  });
  deadline.check();
  const std::vector<PublicState> pool =
      iterationRestartPool(state.reservoir, initial);
  std::vector<Trajectory> restarts(kRestartEpisodesPerIteration);
  parallelIndices(kRestartEpisodesPerIteration, [&](int local) {
    const int episode = kInitialEpisodesPerIteration + local;
    const std::uint32_t seed = trainingSeed(iteration, episode);
    const std::size_t selected = static_cast<std::size_t>(
        (static_cast<std::uint64_t>(mix32(seed ^ kReservoirDomain)) *
         pool.size()) >> 32u);
    restarts[local] = rollout(state.network, seed, pool[selected],
                              kRestartMaximumMoves, &deadline);
  });
  deadline.check();
  std::vector<Trajectory> trajectories;
  trajectories.reserve(kEpisodesPerIteration);
  for (Trajectory& trajectory : initial) {
    if (!trajectory.steps.empty()) trajectories.push_back(std::move(trajectory));
  }
  for (Trajectory& trajectory : restarts) {
    trajectories.push_back(std::move(trajectory));
  }
  return trajectories;
}

std::vector<Trajectory> collectCalibrationIteration(
    const TrainingState& state, int iteration, const Deadline& deadline) {
  std::vector<int> episodes;
  for (int episode = 0; episode < kInitialEpisodesPerIteration; ++episode) {
    if (calibrationGame(trainingSeed(iteration, episode))) {
      episodes.push_back(episode);
    }
  }
  std::vector<Trajectory> trajectories(episodes.size());
  parallelIndices(static_cast<int>(episodes.size()), [&](int index) {
    const std::uint32_t seed = trainingSeed(iteration, episodes[index]);
    trajectories[index] = rollout(state.network, seed, std::nullopt,
                                  kInitialMaximumMoves, &deadline, false);
    if (!trajectories[index].calibration || trajectories[index].restart) {
      throw std::logic_error("calibration reservation crossed a game boundary");
    }
  });
  deadline.check();
  return trajectories;
}

void writeTrainingArtifact(const std::string& path,
                           const TrainingState& state,
                           const ConstraintMoments& calibration,
                           const OptimizerStats& optimizer,
                           double wall_seconds) {
  std::ofstream output(path, std::ios::trunc);
  if (!output) throw std::runtime_error("could not create training artifact");
  const ConstraintMoments window = calibrationWindow(state);
  output << std::fixed << std::setprecision(9)
         << "{\n  \"format\":\"drop7-primal-dual-actor-critic-v1\","
         << "\n  \"completedIterations\":" << state.completed_iterations
         << ",\n  \"trainingEpisodesConsumed\":"
         << static_cast<std::uint64_t>(state.completed_iterations) *
                kEpisodesPerIteration
         << ",\n  \"lastSeedConsumed\":\"0x" << std::hex
         << (kTrainingLane.first +
             static_cast<std::uint32_t>(state.completed_iterations *
                                            kEpisodesPerIteration -
                                        (state.completed_iterations == 0 ? 0 : 1)))
         << std::dec << "\",\n  \"trusted\":"
         << (state.trusted ? "true" : "false")
         << ",\n  \"sourceSha256\":\"" << state.source_sha256 << "\","
         << "\n  \"optimizer\":{\"step\":" << state.network.optimizerStep()
         << ",\"lastLoss\":" << optimizer.final_loss
         << ",\"lastBatches\":" << optimizer.batches << "},"
         << "\n  \"dual\":{\"occupancyMedium\":"
         << state.dual.constraint[0] << ",\"occupancyHigh\":"
         << state.dual.constraint[1] << ",\"coverMedium\":"
         << state.dual.constraint[2] << ",\"coverHigh\":"
         << state.dual.constraint[3] << ",\"terminal\":"
         << state.dual.terminal << "},"
         << "\n  \"lastCalibration\":{\"games\":"
         << calibration.episode_score.count << ",\"meanScore\":"
         << calibration.episode_score.mean() << ",\"meanMoves\":"
         << calibration.episode_moves.mean() << ",\"support\":[";
  for (int index = 0; index < 4; ++index) {
    if (index) output << ',';
    output << calibration.drift[index].count;
  }
  output << "],\"meanDrift\":[";
  for (int index = 0; index < 4; ++index) {
    if (index) output << ',';
    output << calibration.drift[index].mean();
  }
  output << "],\"upper95\":[";
  for (int index = 0; index < 4; ++index) {
    if (index) output << ',';
    const double upper = calibration.drift[index].upper95();
    if (std::isfinite(upper)) output << upper;
    else output << "null";
  }
  output << "],\"terminalSupport\":" << calibration.terminal_rate.count
         << ",\"terminalMean\":" << calibration.terminal_rate.mean()
         << ",\"terminalUpper95\":";
  if (std::isfinite(calibration.terminal_rate.upper95())) {
    output << calibration.terminal_rate.upper95();
  } else {
    output << "null";
  }
  output << ",\"meanEntropy\":" << calibration.entropy.mean() << "},"
         << "\n  \"calibrationWindow\":{\"support\":[";
  for (int index = 0; index < 4; ++index) {
    if (index) output << ',';
    output << window.drift[index].count;
  }
  output << "],\"meanDrift\":[";
  for (int index = 0; index < 4; ++index) {
    if (index) output << ',';
    output << window.drift[index].mean();
  }
  output << "],\"upper95\":[";
  for (int index = 0; index < 4; ++index) {
    if (index) output << ',';
    const double upper = window.drift[index].upper95();
    if (std::isfinite(upper)) output << upper;
    else output << "null";
  }
  output << "]},\n  \"reservoir\":{\"size\":" << state.reservoir.size()
         << ",\"seen\":" << state.reservoir_seen << "},"
         << "\n  \"resources\":{\"wallSeconds\":" << wall_seconds
         << ",\"peakRssBytes\":" << peakRssBytes() << "},"
         << "\n  \"freshSeedAudit\":{\"trainingOnly\":\"0x3dac0000..0x3dadffff\","
            "\"gatesOpened\":false,\"protectedOpened\":false}\n}\n";
  if (!output) throw std::runtime_error("training artifact write failed");
}

int train(std::string_view token, std::string_view source_sha256,
          const std::string& checkpoint_path, const std::string& artifact_path,
          bool resume, std::ostream& progress) {
  requireFreshAuthorization(token, source_sha256);
  const bool exists = std::filesystem::exists(checkpoint_path);
  if (resume != exists) {
    throw std::runtime_error(
        resume ? "resume checkpoint does not exist"
               : "NEW mode refuses to overwrite an existing checkpoint");
  }
  TrainingState state;
  if (resume) {
    state = readCheckpoint(checkpoint_path);
    if (state.source_sha256 != source_sha256) {
      throw std::runtime_error("resume source SHA does not match checkpoint");
    }
    if (state.trusted || state.completed_iterations == kIterations) {
      throw std::runtime_error("completed checkpoint is immutable");
    }
  } else {
    state.source_sha256 = std::string(source_sha256);
  }
  const Deadline deadline(state.cumulative_wall_seconds);
  OptimizerStats last_optimizer;
  ConstraintMoments last_calibration;
  for (int iteration = state.completed_iterations; iteration < kIterations;
       ++iteration) {
    std::vector<Trajectory> trajectories =
        collectIteration(state, iteration, deadline);
    ConstraintMoments training_moments;
    ConstraintMoments ignored_calibration;
    std::vector<Sample> samples = prepareSamples(
        trajectories, state.dual, training_moments, ignored_calibration);
    if (ignored_calibration.episode_score.count != 0) {
      throw std::logic_error("pre-update calibration trajectory leaked");
    }
    deadline.check();
    last_optimizer = optimize(state.network, samples, iteration, deadline);
    updateDuals(state.dual, training_moments);
    updateReservoir(state, trajectories);
    const std::vector<Trajectory> calibration_trajectories =
        collectCalibrationIteration(state, iteration, deadline);
    ConstraintMoments ignored_training;
    ConstraintMoments calibration_moments;
    const std::vector<Sample> calibration_samples = prepareSamples(
        calibration_trajectories, state.dual, ignored_training,
        calibration_moments);
    if (!calibration_samples.empty() ||
        ignored_training.episode_score.count != 0) {
      throw std::logic_error("post-update calibration entered optimizer data");
    }
    deadline.check();
    state.calibration[iteration % kCalibrationWindowIterations] =
        calibration_moments;
    state.completed_iterations = iteration + 1;
    state.trusted = calibrationPasses(state);
    // The live deadline includes checkpoint I/O during uninterrupted runs. A
    // conservative persisted reserve prevents a resumed run from recovering
    // the small interval between timestamping and the atomic rename.
    state.cumulative_wall_seconds =
        deadline.seconds() + kCheckpointIoReserveSeconds;
    if (state.cumulative_wall_seconds > kMaximumWallSeconds) {
      throw std::runtime_error(
          "checkpoint I/O reserve would exceed cumulative training wall cap");
    }
    writeCheckpointAtomic(checkpoint_path, state);
    last_calibration = calibration_moments;
    progress << std::fixed << std::setprecision(6)
             << "PRIMAL_DUAL_SAFE_BOUNDARY {\"iteration\":"
             << state.completed_iterations << ",\"nextSeed\":\"0x"
             << std::hex
             << (state.completed_iterations == kIterations
                     ? kTrainingLane.last
                     : trainingSeed(state.completed_iterations, 0))
             << std::dec << "\",\"trainingSamples\":" << samples.size()
             << ",\"calibrationGames\":"
             << calibration_moments.episode_score.count
             << ",\"trusted\":" << (state.trusted ? "true" : "false")
             << ",\"elapsedSeconds\":" << deadline.seconds()
             << ",\"peakRssBytes\":" << peakRssBytes() << "}\n";
    deadline.check();
  }
  if (!state.trusted) {
    // A complete but untrusted checkpoint is retained for diagnosis, never
    // accepted by gate mode. Training seeds must not be reread for tuning.
    writeTrainingArtifact(artifact_path, state, last_calibration,
                          last_optimizer, deadline.seconds());
    throw std::runtime_error(
        "final calibration failed; checkpoint sealed untrusted and gates forbidden");
  }
  writeTrainingArtifact(artifact_path, state, last_calibration, last_optimizer,
                        deadline.seconds());
  progress << "PRIMAL_DUAL_TRAINED {\"trusted\":true,\"checkpoint\":\""
           << checkpoint_path << "\",\"artifact\":\"" << artifact_path
           << "\",\"wallSeconds\":" << deadline.seconds() << "}\n";
  return 0;
}

// ---------------------------------------------------------------------------
// Candidate-first fresh development gates
// ---------------------------------------------------------------------------

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
};

void observeWaves(const MoveResult& move, GameResult& result) {
  for (const Wave& wave : move.waves) {
    result.clears += static_cast<std::uint64_t>(wave.cleared);
    result.reveals += static_cast<std::uint64_t>(wave.revealed);
  }
}

GameResult runCandidateGame(const Network& network, std::uint32_t seed,
                            SeedUse use, int maximum_moves,
                            const Deadline& deadline) {
  requireSeed(seed, use);
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < maximum_moves) {
    const Prediction prediction = predict(network, publicState(state));
    const int action = greedyAction(prediction);
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("candidate gate transition failed");
    }
    observeWaves(move, result);
    if ((state.moves_played & 31) == 0) deadline.check();
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  return result;
}

GameResult runD4Game(std::uint32_t seed, SeedUse use, int maximum_moves,
                     const Deadline& deadline) {
  requireSeed(seed, use);
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < maximum_moves) {
    const d4::SearchDecision decision = d4::chooseDepth4Action(state);
    if (!decision.complete || decision.completed_depth != d4::kCandidateDepth ||
        !isLegal(state.board, decision.action)) {
      throw std::runtime_error("D4 comparator did not complete exactly");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("D4 gate transition failed");
    }
    observeWaves(move, result);
    deadline.check();
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  return result;
}

std::vector<GameResult> runCandidateCohort(const Network& network,
                                           SeedLane lane, SeedUse use,
                                           int games, int maximum_moves,
                                           const Deadline& deadline) {
  std::vector<GameResult> results(games);
  parallelIndices(games, [&](int game) {
    const std::uint32_t seed = lane.first + static_cast<std::uint32_t>(game);
    results[game] =
        runCandidateGame(network, seed, use, maximum_moves, deadline);
  });
  return results;
}

std::vector<GameResult> runD4Cohort(SeedLane lane, SeedUse use, int games,
                                    int maximum_moves,
                                    const Deadline& deadline) {
  std::vector<GameResult> results(games);
  parallelIndices(std::min(games, 4), [&](int worker) {
    for (int game = worker; game < games; game += std::min(games, 4)) {
      const std::uint32_t seed = lane.first + static_cast<std::uint32_t>(game);
      results[game] = runD4Game(seed, use, maximum_moves, deadline);
    }
  });
  return results;
}

struct Summary {
  int games = 0;
  double mean_score = 0;
  double mean_moves = 0;
  double lower_quartile_moves = 0;
  double clears_per_move = 0;
  double reveals_per_move = 0;
  int censored = 0;
};

Summary summarize(const std::vector<GameResult>& games) {
  if (games.empty()) throw std::invalid_argument("cannot summarize empty cohort");
  Summary result;
  result.games = static_cast<int>(games.size());
  std::vector<int> moves;
  std::uint64_t total_clears = 0;
  std::uint64_t total_reveals = 0;
  std::uint64_t total_moves = 0;
  for (const GameResult& game : games) {
    result.mean_score += static_cast<double>(game.score);
    result.mean_moves += game.moves;
    moves.push_back(game.moves);
    total_moves += game.moves;
    total_clears += game.clears;
    total_reveals += game.reveals;
    result.censored += game.censored;
  }
  result.mean_score /= games.size();
  result.mean_moves /= games.size();
  std::sort(moves.begin(), moves.end());
  result.lower_quartile_moves = moves[moves.size() / 4];
  if (total_moves != 0) {
    result.clears_per_move = static_cast<double>(total_clears) / total_moves;
    result.reveals_per_move = static_cast<double>(total_reveals) / total_moves;
  }
  return result;
}

bool absolutePass(const Summary& summary, const StageGate& gate) {
  return summary.games == gate.games && summary.mean_score >= gate.score &&
         summary.mean_moves >= gate.moves &&
         summary.lower_quartile_moves >= gate.lower_quartile_moves &&
         summary.clears_per_move >= gate.clears_per_move &&
         summary.reveals_per_move >= gate.reveals_per_move;
}

template <typename Function>
auto comparatorAfterAbsolute(const Summary& candidate, const StageGate& gate,
                             Function function)
    -> std::optional<decltype(function())> {
  if (!absolutePass(candidate, gate)) return std::nullopt;
  return function();
}

struct PairedResult {
  double score_ratio = 0;
  double move_ratio = 0;
  int joint_wins = 0;
  bool passed = false;
};

PairedResult paired(const std::vector<GameResult>& candidate,
                    const std::vector<GameResult>& baseline,
                    const StageGate& gate) {
  if (candidate.size() != baseline.size() || candidate.empty()) {
    throw std::invalid_argument("paired cohorts do not match");
  }
  const Summary candidate_summary = summarize(candidate);
  const Summary baseline_summary = summarize(baseline);
  PairedResult result;
  result.score_ratio = candidate_summary.mean_score /
                       std::max(1.0, baseline_summary.mean_score);
  result.move_ratio = candidate_summary.mean_moves /
                      std::max(1.0, baseline_summary.mean_moves);
  for (std::size_t game = 0; game < candidate.size(); ++game) {
    result.joint_wins += candidate[game].score > baseline[game].score &&
                         candidate[game].moves > baseline[game].moves;
  }
  result.passed = result.score_ratio >= gate.score_ratio_vs_d4 &&
                  result.move_ratio >= gate.move_ratio_vs_d4 &&
                  result.joint_wins >= gate.joint_wins;
  return result;
}

double bootstrapLower95(const std::vector<GameResult>& games) {
  if (games.empty()) throw std::invalid_argument("bootstrap cohort is empty");
  std::vector<double> means;
  means.reserve(kBootstrapReplicates);
  Mulberry32 random(kBootstrapDomain);
  for (int replicate = 0; replicate < kBootstrapReplicates; ++replicate) {
    double total = 0;
    for (std::size_t draw = 0; draw < games.size(); ++draw) {
      const std::size_t selected = static_cast<std::size_t>(
          (static_cast<std::uint64_t>(random.nextBits()) * games.size()) >> 32u);
      total += static_cast<double>(games[selected].score);
    }
    means.push_back(total / games.size());
  }
  std::sort(means.begin(), means.end());
  return means[static_cast<std::size_t>(0.025 * kBootstrapReplicates)];
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"games\":" << summary.games
         << ",\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"lowerQuartileMoves\":" << summary.lower_quartile_moves
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"censored\":" << summary.censored << '}';
}

void writeGateArtifact(const std::string& path, std::string_view source_sha256,
                       std::string_view stopped_after,
                       const std::optional<Summary>& stage_a,
                       const std::optional<PairedResult>& paired_a,
                       const std::optional<Summary>& stage_b,
                       const std::optional<PairedResult>& paired_b,
                       const std::optional<Summary>& stage_c,
                       const std::optional<double>& lower95,
                       bool passed, const Deadline& deadline) {
  std::ofstream output(path, std::ios::trunc);
  if (!output) throw std::runtime_error("could not create gate artifact");
  output << std::fixed << std::setprecision(9)
         << "{\n  \"format\":\"drop7-primal-dual-gates-v1\","
         << "\n  \"sourceSha256\":\"" << source_sha256 << "\","
         << "\n  \"stoppedAfter\":\"" << stopped_after << "\","
         << "\n  \"passed\":" << (passed ? "true" : "false")
         << ",\n  \"stageA\":";
  if (stage_a) writeSummary(output, *stage_a); else output << "null";
  output << ",\n  \"stageAPaired\":";
  if (paired_a) {
    output << "{\"scoreRatio\":" << paired_a->score_ratio
           << ",\"moveRatio\":" << paired_a->move_ratio
           << ",\"jointWins\":" << paired_a->joint_wins
           << ",\"passed\":" << (paired_a->passed ? "true" : "false") << '}';
  } else output << "null";
  output << ",\n  \"stageB\":";
  if (stage_b) writeSummary(output, *stage_b); else output << "null";
  output << ",\n  \"stageBPaired\":";
  if (paired_b) {
    output << "{\"scoreRatio\":" << paired_b->score_ratio
           << ",\"moveRatio\":" << paired_b->move_ratio
           << ",\"jointWins\":" << paired_b->joint_wins
           << ",\"passed\":" << (paired_b->passed ? "true" : "false") << '}';
  } else output << "null";
  output << ",\n  \"stageC\":";
  if (stage_c) writeSummary(output, *stage_c); else output << "null";
  output << ",\n  \"stageCBootstrapLower95\":";
  if (lower95) output << *lower95; else output << "null";
  output << ",\n  \"resources\":{\"wallSeconds\":" << deadline.seconds()
         << ",\"peakRssBytes\":" << peakRssBytes() << "},"
         << "\n  \"seedAudit\":{\"candidateFirst\":true,"
            "\"trainingReopened\":false,\"protectedOpened\":false}\n}\n";
}

int gates(std::string_view token, std::string_view source_sha256,
          const std::string& checkpoint_path, const std::string& artifact_path,
          std::ostream& progress) {
  requireFreshAuthorization(token, source_sha256);
  const TrainingState state = readCheckpoint(checkpoint_path);
  if (!state.trusted || state.completed_iterations != kIterations ||
      state.source_sha256 != source_sha256 || !calibrationPasses(state)) {
    throw std::runtime_error("gates require the trusted final checkpoint");
  }
  const Deadline deadline;
  std::optional<Summary> stage_a;
  std::optional<PairedResult> paired_a;
  std::optional<Summary> stage_b;
  std::optional<PairedResult> paired_b;
  std::optional<Summary> stage_c;
  std::optional<double> lower95;

  // Candidate first. No D4 state in this lane is touched before all absolute
  // Stage-A metrics are known to pass.
  const auto candidate_a = runCandidateCohort(
      state.network, kStageALane, SeedUse::kStageA, kStageAGate.games, 1'000,
      deadline);
  stage_a = summarize(candidate_a);
  if (!absolutePass(*stage_a, kStageAGate)) {
    writeGateArtifact(artifact_path, source_sha256, "stage-a-absolute",
                      stage_a, paired_a, stage_b, paired_b, stage_c, lower95,
                      false, deadline);
    progress << "PRIMAL_DUAL_GATE_STOP {\"stage\":\"stage-a-absolute\"}\n";
    return 1;
  }
  const auto baseline_a = comparatorAfterAbsolute(*stage_a, kStageAGate, [&] {
    return runD4Cohort(kStageALane, SeedUse::kStageA, kStageAGate.games, 1'000,
                       deadline);
  });
  if (!baseline_a) throw std::logic_error("Stage-A comparator guard regressed");
  paired_a = paired(candidate_a, *baseline_a, kStageAGate);
  if (!paired_a->passed) {
    writeGateArtifact(artifact_path, source_sha256, "stage-a-paired",
                      stage_a, paired_a, stage_b, paired_b, stage_c, lower95,
                      false, deadline);
    progress << "PRIMAL_DUAL_GATE_STOP {\"stage\":\"stage-a-paired\"}\n";
    return 1;
  }

  const auto candidate_b = runCandidateCohort(
      state.network, kStageBLane, SeedUse::kStageB, kStageBGate.games, 1'000,
      deadline);
  stage_b = summarize(candidate_b);
  if (!absolutePass(*stage_b, kStageBGate)) {
    writeGateArtifact(artifact_path, source_sha256, "stage-b-absolute",
                      stage_a, paired_a, stage_b, paired_b, stage_c, lower95,
                      false, deadline);
    progress << "PRIMAL_DUAL_GATE_STOP {\"stage\":\"stage-b-absolute\"}\n";
    return 1;
  }
  const auto baseline_b = comparatorAfterAbsolute(*stage_b, kStageBGate, [&] {
    return runD4Cohort(kStageBLane, SeedUse::kStageB, kStageBGate.games, 1'000,
                       deadline);
  });
  if (!baseline_b) throw std::logic_error("Stage-B comparator guard regressed");
  paired_b = paired(candidate_b, *baseline_b, kStageBGate);
  if (!paired_b->passed) {
    writeGateArtifact(artifact_path, source_sha256, "stage-b-paired",
                      stage_a, paired_a, stage_b, paired_b, stage_c, lower95,
                      false, deadline);
    progress << "PRIMAL_DUAL_GATE_STOP {\"stage\":\"stage-b-paired\"}\n";
    return 1;
  }

  const auto candidate_c = runCandidateCohort(
      state.network, kStageCLane, SeedUse::kStageC, 256, kMaximumGateMoves,
      deadline);
  stage_c = summarize(candidate_c);
  lower95 = bootstrapLower95(candidate_c);
  const bool stage_c_passed =
      stage_c->mean_score > 1'050'000.0 && *lower95 > 1'000'000.0 &&
      stage_c->mean_moves > 300.0 && stage_c->clears_per_move >= 2.30 &&
      stage_c->reveals_per_move >= 1.32;
  writeGateArtifact(artifact_path, source_sha256, "stage-c", stage_a, paired_a,
                    stage_b, paired_b, stage_c, lower95, stage_c_passed,
                    deadline);
  progress << "PRIMAL_DUAL_GATE_RESULT {\"passed\":"
           << (stage_c_passed ? "true" : "false")
           << ",\"meanScore\":" << stage_c->mean_score
           << ",\"bootstrapLower95\":" << *lower95
           << ",\"artifact\":\"" << artifact_path << "\"}\n";
  return stage_c_passed ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Zero-fresh verification and preregistration
// ---------------------------------------------------------------------------

template <typename Function>
bool throwsException(Function function) {
  try {
    function();
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

PublicState gradientFixture() {
  PublicState state;
  state.board = initialBoard();
  state.next_disc = 4;
  state.phase = 3;
  // A stable, gravity-valid public fixture with covers and latent low numbers.
  state.board[indexOf(5, 0)] = 7;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(5, 3)] = 2;
  state.board[indexOf(4, 3)] = 6;
  state.board[indexOf(5, 5)] = kCracked;
  return state;
}

struct GradientCheck {
  int checked = 0;
  double maximum_absolute_error = 0;
  double maximum_relative_error = 0;
  bool passed = false;
};

bool reflectionBitExact(const Network& network) {
  Mulberry32 random(0x5246'4c58u);
  for (int fixture_index = 0; fixture_index < 24; ++fixture_index) {
    PublicState generated;
    generated.next_disc = random.nextDisc();
    generated.phase = static_cast<std::uint8_t>(
        1u + random.nextBits() % kMovesPerLevel);
    for (int column = 0; column < kBoardSize; ++column) {
      const int height = static_cast<int>(random.nextBits() % 7u);
      for (int offset = 0; offset < height; ++offset) {
        generated.board[indexOf(kBoardSize - 1 - offset, column)] =
            static_cast<std::uint8_t>(1u + random.nextBits() % 9u);
      }
    }
    const Prediction left = predict(network, generated);
    const Prediction right = predict(network, mirror(generated));
    for (int action = 0; action < kBoardSize; ++action) {
      if (std::bit_cast<std::uint64_t>(left.probabilities[action]) !=
          std::bit_cast<std::uint64_t>(
              right.probabilities[kBoardSize - 1 - action])) return false;
    }
    for (int head = 0; head < kValueHeads; ++head) {
      if (std::bit_cast<std::uint64_t>(left.values[head]) !=
          std::bit_cast<std::uint64_t>(right.values[head])) return false;
    }
  }
  return true;
}

GradientCheck gradientCheck() {
  Network network(0x4752'4144u);
  for (int index = 0; index < kBoardSize * kHidden2; ++index) {
    network.parameters()[Layout::policy_w + index] =
        static_cast<float>((index % 11 - 5) * 0.004);
  }
  for (int index = 0; index < kValueHeads * kHidden2; ++index) {
    network.parameters()[Layout::value_w + index] =
        static_cast<float>((index % 13 - 6) * 0.003);
  }
  const PublicState fixture = gradientFixture();
  const auto backbone = baseLogits(fixture);
  const Prediction original = network.predict(fixture, backbone);
  Sample sample;
  sample.state = fixture;
  sample.base_logits = backbone;
  sample.action = 3;
  sample.old_log_probability = original.log_probabilities[sample.action];
  sample.old_values = original.values;
  sample.policy_advantage = 0.73;
  sample.trajectory_weight = 1.4;
  sample.value_mask.fill(true);
  for (int head = 0; head < kValueHeads; ++head) {
    sample.targets[head] = original.values[head] + 0.1 * (head - 3);
  }
  const std::vector<Sample> samples{sample};
  const std::vector<std::size_t> order{0};
  std::vector<double> analytic;
  (void)batchLossAndGradient(network, samples, order, 0, 1, &analytic);

  const std::array<int, 32> indices{{
      Layout::cell + (0 * kCellCategories + kEmpty) * kHidden1,
      Layout::cell + (6 * kCellCategories + kEmpty) * kHidden1 + 7,
      Layout::cell + (42 * kCellCategories + kSolid) * kHidden1 + 11,
      Layout::cell + (45 * kCellCategories + kSolid) * kHidden1 + 17,
      Layout::cell + (38 * kCellCategories + kCracked) * kHidden1 + 3,
      Layout::disc + 3 * kHidden1,
      Layout::disc + 3 * kHidden1 + 19,
      Layout::phase + 2 * kHidden1,
      Layout::phase + 2 * kHidden1 + 23,
      Layout::scalar,
      Layout::scalar + 7 * kHidden1 + 9,
      Layout::scalar + 23 * kHidden1 + 31,
      Layout::b1,
      Layout::b1 + 29,
      Layout::w2,
      Layout::w2 + 13 * kHidden1 + 7,
      Layout::w2 + 41 * kHidden1 + 37,
      Layout::b2,
      Layout::b2 + 33,
      Layout::policy_w + 3 * kHidden2,
      Layout::policy_w + 3 * kHidden2 + 27,
      Layout::policy_w + 5 * kHidden2 + 11,
      Layout::policy_b + 3,
      Layout::policy_b + 5,
      Layout::value_w,
      Layout::value_w + kLifetimeHead * kHidden2 + 17,
      Layout::value_w + kTerminalHead * kHidden2 + 31,
      Layout::value_w + kCoverHighHead * kHidden2 + 9,
      Layout::value_w + kRegenerationHead * kHidden2 + 47,
      Layout::value_b + kScoreHead,
      Layout::value_b + kCoverHighHead,
      Layout::value_b + kRegenerationHead,
  }};
  constexpr float epsilon = 0.002f;
  GradientCheck result;
  for (int index : indices) {
    const float original_value = network.parameters()[index];
    network.parameters()[index] = original_value + epsilon;
    const double plus =
        batchLossAndGradient(network, samples, order, 0, 1, nullptr);
    network.parameters()[index] = original_value - epsilon;
    const double minus =
        batchLossAndGradient(network, samples, order, 0, 1, nullptr);
    network.parameters()[index] = original_value;
    const double numeric = (plus - minus) / (2.0 * epsilon);
    const double absolute = std::abs(numeric - analytic[index]);
    const double relative = absolute /
        std::max(1.0e-5, std::abs(numeric) + std::abs(analytic[index]));
    result.maximum_absolute_error =
        std::max(result.maximum_absolute_error, absolute);
    result.maximum_relative_error =
        std::max(result.maximum_relative_error, relative);
    ++result.checked;
  }
  result.passed = result.checked == static_cast<int>(indices.size()) &&
                  result.maximum_absolute_error < 8.0e-4 &&
                  result.maximum_relative_error < 0.06;
  return result;
}

bool saturatedLogRatioGradientCheck() {
  Network network(0x434c'414du);
  const PublicState fixture = gradientFixture();
  const auto backbone = baseLogits(fixture);
  const Prediction prediction = network.predict(fixture, backbone);
  for (const auto [raw_log_ratio, advantage] :
       {std::pair{30.0, -0.7}, std::pair{-30.0, 0.7}}) {
    Sample sample;
    sample.state = fixture;
    sample.base_logits = backbone;
    sample.action = 3;
    sample.old_log_probability =
        prediction.log_probabilities[sample.action] - raw_log_ratio;
    sample.policy_advantage = advantage;
    sample.value_mask.fill(false);
    const std::vector<Sample> samples{sample};
    const std::vector<std::size_t> order{0};
    std::vector<double> analytic;
    (void)batchLossAndGradient(network, samples, order, 0, 1, &analytic);
    const int index = Layout::policy_b + sample.action;
    constexpr float epsilon = 0.002f;
    const float original = network.parameters()[index];
    network.parameters()[index] = original + epsilon;
    const double plus =
        batchLossAndGradient(network, samples, order, 0, 1, nullptr);
    network.parameters()[index] = original - epsilon;
    const double minus =
        batchLossAndGradient(network, samples, order, 0, 1, nullptr);
    network.parameters()[index] = original;
    const double numeric = (plus - minus) / (2.0 * epsilon);
    if (std::abs(numeric - analytic[index]) > 2.0e-5) return false;
  }
  return true;
}

Trajectory terminalCycleFixture(int moves, bool terminal, bool censored) {
  if (moves < 1 || moves > kMacroMoves) {
    throw std::invalid_argument("terminal cycle fixture has invalid length");
  }
  Trajectory trajectory;
  trajectory.censored = censored;
  PublicState visible{initialBoard(), 3, 5, false};
  for (int move = 0; move < moves; ++move) {
    Step step;
    step.state = visible;
    step.state.phase = static_cast<std::uint8_t>(kMacroMoves - move);
    step.terminal_after = terminal && move == moves - 1;
    trajectory.steps.push_back(step);
  }
  trajectory.final_state = visible;
  trajectory.final_state.phase = terminal ? 0 : 5;
  trajectory.final_state.terminal = terminal;
  return trajectory;
}

Trajectory syntheticTrajectory(std::uint32_t seed, bool calibration) {
  Trajectory result;
  result.episode_seed = seed;
  result.calibration = calibration;
  result.censored = true;
  Step step;
  step.state = gradientFixture();
  step.action = 3;
  const Network network;
  step.base_logits = baseLogits(step.state);
  const Prediction prediction = network.predict(step.state, step.base_logits);
  step.old_log_probability = prediction.log_probabilities[step.action];
  step.old_values = prediction.values;
  step.old_entropy = 1.0;
  step.score_reward = 1.0;
  step.lifetime_reward = 0.2;
  result.steps.push_back(step);
  result.final_state = step.state;
  result.observed_score = 17'000;
  result.observed_moves = 1;
  return result;
}

bool selfTest(std::ostream& output) {
  const GradientCheck gradient = gradientCheck();
  const bool saturated_gradient = saturatedLogRatioGradientCheck();
  const PublicState fixture = gradientFixture();
  const PublicState reflected = mirror(fixture);
  Network network;
  Network reflection_network(0x5245'5349u);
  for (int index = 0; index < kBoardSize * kHidden2; ++index) {
    reflection_network.parameters()[Layout::policy_w + index] =
        static_cast<float>((index % 17 - 8) * 0.007);
  }
  for (int index = 0; index < kBoardSize; ++index) {
    reflection_network.parameters()[Layout::policy_b + index] =
        static_cast<float>((index - 3) * 0.013);
  }
  for (int index = 0; index < kValueHeads * kHidden2; ++index) {
    reflection_network.parameters()[Layout::value_w + index] =
        static_cast<float>((index % 19 - 9) * 0.005);
  }
  for (int index = 0; index < kValueHeads; ++index) {
    reflection_network.parameters()[Layout::value_b + index] =
        static_cast<float>((index - 4) * 0.011);
  }
  const bool reflection_bit_exact = reflectionBitExact(reflection_network);
  const Prediction prediction = predict(network, fixture);
  const Prediction reflected_prediction = predict(network, reflected);
  double reflection_error = 0;
  for (int action = 0; action < kBoardSize; ++action) {
    reflection_error = std::max(
        reflection_error,
        std::abs(prediction.probabilities[action] -
                 reflected_prediction.probabilities[kBoardSize - 1 - action]));
  }
  for (int head = 0; head < kValueHeads; ++head) {
    reflection_error = std::max(
        reflection_error,
        std::abs(prediction.values[head] - reflected_prediction.values[head]));
  }

  State metadata = materialize(fixture);
  metadata.score = 9'999'999;
  metadata.level = 817;
  metadata.moves_played = 4'003;
  const bool public_boundary = publicState(metadata) == fixture;

  PublicState medium_first;
  medium_first.next_disc = 3;
  medium_first.phase = 5;
  for (int index = 29; index < 49; ++index) medium_first.board[index] = 4;
  for (int index = 39; index < 49; ++index) medium_first.board[index] = kSolid;
  PublicState medium_after = medium_first;
  medium_after.board[29] = kEmpty;
  medium_after.board[30] = kEmpty;
  medium_after.board[39] = kEmpty;
  const CycleTarget medium = absoluteCycleDrift(medium_first, medium_after);
  PublicState high_first = medium_first;
  for (int index = 17; index < 29; ++index) high_first.board[index] = 5;
  for (int index = 17; index < 23; ++index) high_first.board[index] = kCracked;
  PublicState high_after = high_first;
  high_after.board[17] = kEmpty;
  high_after.board[18] = kEmpty;
  high_after.board[39] = kEmpty;
  const CycleTarget high = absoluteCycleDrift(high_first, high_after);
  const CycleTarget inactive = absoluteCycleDrift(
      PublicState{initialBoard(), 1, 5, false},
      PublicState{initialBoard(), 1, 5, false});
  const bool drift_targets = medium.masks[0] && medium.masks[2] &&
      !medium.masks[1] && !medium.masks[3] && medium.costs[0] == -3.0 &&
      medium.costs[2] == -1.0 && high.masks[1] && high.masks[3] &&
      high.costs[1] == -3.0 + kHighOccupancyMargin &&
      high.costs[3] == -3.0 + kHighCoverMargin &&
      std::none_of(inactive.masks.begin(), inactive.masks.end(),
                   [](bool value) { return value; });

  ConstraintMoments terminal_full_moments;
  const auto terminal_full = cycleTargets(
      terminalCycleFixture(5, true, false), terminal_full_moments);
  ConstraintMoments capped_full_moments;
  const auto capped_full = cycleTargets(
      terminalCycleFixture(5, false, true), capped_full_moments);
  ConstraintMoments terminal_partial_moments;
  const auto terminal_partial = cycleTargets(
      terminalCycleFixture(3, true, false), terminal_partial_moments);
  ConstraintMoments capped_partial_moments;
  const auto capped_partial = cycleTargets(
      terminalCycleFixture(3, false, true), capped_partial_moments);
  const bool terminal_alignment =
      terminal_full_moments.terminal_rate.count == 1 &&
      terminal_full_moments.terminal_rate.mean() == 1.0 &&
      std::all_of(terminal_full.begin(), terminal_full.end(),
                  [](const CycleTarget& target) {
                    return target.terminal_mask && target.terminal_cost == 1.0;
                  }) &&
      capped_full_moments.terminal_rate.count == 1 &&
      capped_full_moments.terminal_rate.mean() == 0.0 &&
      std::all_of(capped_full.begin(), capped_full.end(),
                  [](const CycleTarget& target) {
                    return target.terminal_mask && target.terminal_cost == 0.0;
                  }) &&
      terminal_partial_moments.terminal_rate.count == 1 &&
      std::all_of(terminal_partial.begin(), terminal_partial.end(),
                  [](const CycleTarget& target) {
                    return target.terminal_mask && target.terminal_cost == 1.0 &&
                           std::none_of(target.masks.begin(), target.masks.end(),
                                        [](bool value) { return value; });
                  }) &&
      capped_partial_moments.terminal_rate.count == 0 &&
      std::all_of(capped_partial.begin(), capped_partial.end(),
                  [](const CycleTarget& target) {
                    return !target.terminal_mask;
                  });

  DualState dual;
  const DualState before = dual;
  ConstraintMoments no_support;
  updateDuals(dual, no_support);
  const bool inactive_dual = dual.constraint == before.constraint;
  ConstraintMoments supported;
  supported.drift[0].add(2.0);
  supported.drift[2].add(-2.0);
  updateDuals(dual, supported);
  const bool active_dual = dual.constraint[0] > 0 && dual.constraint[1] == 0 &&
                           dual.constraint[2] == 0 && dual.constraint[3] == 0;

  std::uint32_t calibration_seed = kTrainingLane.first;
  while (!calibrationGame(calibration_seed)) ++calibration_seed;
  std::uint32_t training_seed = kTrainingLane.first;
  while (calibrationGame(training_seed)) ++training_seed;
  std::vector<Trajectory> isolation{
      syntheticTrajectory(training_seed, false),
      syntheticTrajectory(calibration_seed, true),
  };
  ConstraintMoments training_moments;
  ConstraintMoments calibration_moments;
  const std::vector<Sample> isolated_samples = prepareSamples(
      isolation, before, training_moments, calibration_moments);
  const bool calibration_isolation = isolated_samples.size() == 1 &&
      training_moments.episode_score.count == 1 &&
      calibration_moments.episode_score.count == 1 &&
      !isolated_samples.front().value_mask[kTerminalHead];

  TrainingState final_calibration_state;
  final_calibration_state.completed_iterations = kIterations;
  ConstraintMoments& final_calibration = final_calibration_state.calibration[
      (kIterations - 1) % kCalibrationWindowIterations];
  for (int game = 0; game < kInitialEpisodesPerIteration; ++game) {
    final_calibration.episode_score.add(600'000.0);
    final_calibration.episode_moves.add(200.0);
    final_calibration.entropy.add(0.5);
    final_calibration.terminal_rate.add(0.0);
    if (game < static_cast<int>(kMinimumCalibrationSupportPerConstraint)) {
      for (Moment& drift : final_calibration.drift) drift.add(-0.5);
    }
  }
  const bool final_calibration_pass =
      calibrationPasses(final_calibration_state);
  TrainingState bad_final_calibration = final_calibration_state;
  bad_final_calibration.calibration[
      (kIterations - 1) % kCalibrationWindowIterations].drift[0] = Moment{};
  // Calibration support for an intermediate policy cannot satisfy the final
  // policy's calibration requirement.
  bad_final_calibration.calibration[0] = final_calibration;
  const bool final_policy_only =
      !calibrationPasses(bad_final_calibration);
  int final_calibration_reservations = 0;
  for (int episode = 0; episode < kInitialEpisodesPerIteration; ++episode) {
    final_calibration_reservations += calibrationGame(
        trainingSeed(kIterations - 1, episode));
  }
  const bool calibration_reservation =
      final_calibration_reservations == kInitialEpisodesPerIteration;

  TrainingState checkpoint;
  checkpoint.source_sha256 = std::string(64, 'a');
  checkpoint.completed_iterations = 3;
  checkpoint.cumulative_wall_seconds = 123.5;
  checkpoint.reservoir.push_back(fixture);
  checkpoint.reservoir_seen = 1;
  checkpoint.calibration[0].drift[0].add(-1.0);
  const auto bytes = serializeCheckpoint(checkpoint);
  const TrainingState restored = deserializeCheckpoint(bytes);
  auto corrupted = bytes;
  corrupted[20] ^= 0x80u;
  const bool checkpoint_roundtrip =
      restored.completed_iterations == 3 && restored.reservoir.size() == 1 &&
      restored.cumulative_wall_seconds == 123.5 &&
      restored.reservoir.front() == fixture &&
      throwsException([&] { (void)deserializeCheckpoint(corrupted); });

  Network optimizer_guard;
  const auto optimizer_parameters_before = optimizer_guard.parameters();
  const auto optimizer_first_before = optimizer_guard.firstMoment();
  const auto optimizer_second_before = optimizer_guard.secondMoment();
  std::vector<double> invalid_gradient(Layout::count, 0.0);
  invalid_gradient[17] = std::numeric_limits<double>::quiet_NaN();
  const bool rejected_nan = throwsException(
      [&] { optimizer_guard.applyAdam(invalid_gradient, 1); });
  const bool optimizer_unchanged =
      optimizer_guard.optimizerStep() == 0 &&
      optimizer_guard.parameters() == optimizer_parameters_before &&
      optimizer_guard.firstMoment() == optimizer_first_before &&
      optimizer_guard.secondMoment() == optimizer_second_before;
  std::vector<double> large_gradient(Layout::count, 0.0);
  large_gradient[3] = std::numeric_limits<double>::max() / 4.0;
  large_gradient[9] = -std::numeric_limits<double>::max() / 8.0;
  const bool accepted_large = !throwsException(
      [&] { optimizer_guard.applyAdam(large_gradient, 1); });
  const bool optimizer_guarded = rejected_nan && optimizer_unchanged &&
      accepted_large && optimizer_guard.optimizerStep() == 1 &&
      std::all_of(optimizer_guard.parameters().begin(),
                  optimizer_guard.parameters().end(),
                  [](float value) { return std::isfinite(value); });

  Summary failing;
  failing.games = kStageAGate.games;
  int comparator_calls = 0;
  const auto guarded = comparatorAfterAbsolute(failing, kStageAGate, [&] {
    ++comparator_calls;
    return 1;
  });
  const bool candidate_first = !guarded && comparator_calls == 0;

  const bool seed_guards =
      trainingSeed(0, 0) == kTrainingLane.first &&
      trainingSeed(kIterations - 1, kEpisodesPerIteration - 1) ==
          kTrainingLane.last &&
      throwsException([] { requireSeed(0x7d00'0000u, SeedUse::kTraining); }) &&
      throwsException([] { requireSeed(0xd700'0000u, SeedUse::kStageC); }) &&
      throwsException([] { requireSeed(0x3dab'ffffu, SeedUse::kTraining); }) &&
      throwsException([] {
        requireFreshAuthorization("wrong", std::string(64, 'a'));
      });
  std::vector<bool> restart_stream_seen(kTrainingEpisodes, false);
  bool restart_stream_injective = true;
  for (int episode = 0; episode < kTrainingEpisodes; ++episode) {
    const std::uint32_t seed =
        kTrainingLane.first + static_cast<std::uint32_t>(episode);
    const std::uint32_t stream = syntheticRestartStream(seed);
    const std::uint32_t offset = stream - 0x2e00'0000u;
    if ((stream >> 16u) < 0x2e00u || (stream >> 16u) > 0x2e01u ||
        offset >= restart_stream_seen.size() || restart_stream_seen[offset]) {
      restart_stream_injective = false;
      break;
    }
    restart_stream_seen[offset] = true;
  }

  const bool legal = prediction.probabilities[0] > 0 &&
      std::abs(std::accumulate(prediction.probabilities.begin(),
                               prediction.probabilities.end(), 0.0) -
               1.0) < 1.0e-12;
  const bool passed = gradient.passed && saturated_gradient &&
      reflection_error < 1.0e-12 &&
      reflection_bit_exact &&
      public_boundary && drift_targets && terminal_alignment &&
      inactive_dual && active_dual &&
      calibration_isolation && final_calibration_pass && final_policy_only &&
      calibration_reservation && checkpoint_roundtrip && candidate_first &&
      optimizer_guarded && seed_guards && restart_stream_injective && legal;
  output << std::fixed << std::setprecision(12)
         << "PRIMAL_DUAL_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"gradientChecks\":" << gradient.checked
         << ",\"gradientMaxAbsolute\":" << gradient.maximum_absolute_error
         << ",\"gradientMaxRelative\":" << gradient.maximum_relative_error
         << ",\"saturatedGradient\":"
         << (saturated_gradient ? "true" : "false")
         << ",\"reflectionError\":" << reflection_error
         << ",\"reflectionBitExact\":"
         << (reflection_bit_exact ? "true" : "false")
         << ",\"publicBoundary\":" << (public_boundary ? "true" : "false")
         << ",\"absoluteDrift\":" << (drift_targets ? "true" : "false")
         << ",\"terminalAlignment\":"
         << (terminal_alignment ? "true" : "false")
         << ",\"inactiveDual\":" << (inactive_dual ? "true" : "false")
         << ",\"calibrationIsolation\":"
         << (calibration_isolation ? "true" : "false")
         << ",\"finalPolicyCalibration\":"
         << (final_calibration_pass && final_policy_only &&
                     calibration_reservation
                 ? "true"
                 : "false")
         << ",\"checkpointRoundtrip\":"
         << (checkpoint_roundtrip ? "true" : "false")
         << ",\"optimizerTransactional\":"
         << (optimizer_guarded ? "true" : "false")
         << ",\"candidateFirst\":" << (candidate_first ? "true" : "false")
         << ",\"seedGuards\":" << (seed_guards ? "true" : "false")
         << ",\"restartStreamInjective\":"
         << (restart_stream_injective ? "true" : "false")
         << "}\n";
  return passed;
}

void writePreregistration(const std::string& path,
                          std::string_view source_sha256) {
  if (!isSha256(source_sha256)) throw std::invalid_argument("invalid source SHA");
  std::ofstream output(path, std::ios::trunc);
  if (!output) throw std::runtime_error("could not create preregistration");
  output << std::fixed << std::setprecision(6)
         << "{\n  \"format\":\"drop7-primal-dual-preregistration-v1\","
         << "\n  \"sourceSha256\":\"" << source_sha256 << "\","
         << "\n  \"publicInputs\":[\"board\",\"nextDisc\",\"fiveDropPhase\"],"
         << "\n  \"forbiddenInputs\":[\"gameSeed\",\"futureDiscs\",\"futureReveals\","
            "\"score\",\"level\",\"movesPlayed\",\"history\",\"oracleAction\"],"
         << "\n  \"architecture\":{\"kind\":\"sparse-NNUE-residual\","
            "\"parameters\":" << Layout::count
         << ",\"hidden\":[" << kHidden1 << ',' << kHidden2
         << "],\"reflection\":\"exact two-orientation average\","
            "\"backbone\":\"frozen deterministic fair D1, five chance strata\","
            "\"valueHeads\":[\"score\",\"lifetime\",\"terminalHazard\","
            "\"occupancyMedium\",\"occupancyHigh\",\"coverMedium\","
            "\"coverHigh\",\"regeneration\"]},"
         << "\n  \"constraints\":{\"windowMoves\":5,"
            "\"quantity\":\"absolute end-minus-start cell counts\","
            "\"loadMasks\":{\"occupancyMedium\":\"18..29\","
            "\"occupancyHigh\":\">=30\",\"coverMedium\":\"8..14\","
            "\"coverHigh\":\">=15\"},\"margins\":["
         << kMediumOccupancyMargin << ',' << kHighOccupancyMargin << ','
         << kMediumCoverMargin << ',' << kHighCoverMargin
         << "],\"dualLearningRate\":" << kDualLearningRate
         << ",\"constraintLambdaMaximum\":" << kConstraintLambdaMaximum
         << ",\"terminal\":{\"event\":\"undiscounted death in aligned five-move cycle\","
            "\"riskLimit\":" << kTerminalRiskLimit
         << ",\"initialLambda\":" << kTerminalInitialLambda
         << ",\"lambdaMaximum\":" << kTerminalLambdaMaximum << "}"
         << ",\"estimand\":\"mean of per-game cycle means for dual and confidence; primal cycle penalties inverse-counted per game\""
         << ",\"inactiveRule\":\"zero support leaves multiplier unchanged\","
            "\"trustSupportPerMask\":"
         << kMinimumCalibrationSupportPerConstraint
         << ",\"finalTerminalSupport\":384,"
            "\"finalEntropySupport\":512,\"finalEntropyFloor\":0.05,"
            "\"trustRule\":\"final checkpoint alone: drift upper95 <= 0, terminal upper95 <= riskLimit, and mean greedy entropy >= floor\"},"
         << "\n  \"training\":{\"iterations\":" << kIterations
         << ",\"episodesPerIteration\":" << kEpisodesPerIteration
         << ",\"episodes\":" << kTrainingEpisodes
         << ",\"initialPerIteration\":" << kInitialEpisodesPerIteration
         << ",\"publicRestartsPerIteration\":" << kRestartEpisodesPerIteration
         << ",\"initialMoveCap\":" << kInitialMaximumMoves
         << ",\"restartMoveCap\":" << kRestartMaximumMoves
         << ",\"ppoEpochs\":" << kPpoEpochs
         << ",\"minibatch\":" << kMinibatch
         << ",\"wholeGameCalibration\":\"initial-board games only; hash(seed) mod 5 earlier, all 512 initial slots in final iteration; evaluated post-update with greedy deployment policy\","
            "\"finalIterationTraining\":\"512 restart continuations; 512 initial games reserved for final calibration\","
            "\"ppoLogRatio\":\"clamped to [-20,20] with zero derivative while saturated\","
            "\"tailWeight\":{\"bottomQuartile\":" << kTailPolicyWeight
         << ",\"scope\":\"training trajectories only; separate initial-game and restart-continuation cutoffs\"},"
            "\"censoring\":\"observed lower bounds only; full cycles count observed survival, censored partial cycle hazard masked\"},"
         << "\n  \"seedLanes\":{\"training\":\"0x3dac0000..0x3dadffff\","
            "\"stageA\":\"0x3dae0000..0x3dae001f\","
            "\"stageB\":\"0x3dae1000..0x3dae107f\","
            "\"stageC\":\"0x3daf0000..0x3daf00ff\","
            "\"reserved\":\"0x3daf1000..0x3dafffff\","
            "\"syntheticRestartStreams\":\"injective odd affine permutation of training offset into 0x2e000000..0x2e01ffff\","
            "\"burnedPreflightOnly\":\"0x3d6e4000..0x3d6e4003\","
            "\"protected\":[\"0x4d...\",\"0x7d...\",\"0xd7...\"]},"
         << "\n  \"resume\":{\"boundary\":\"after each complete iteration\","
            "\"mapping\":\"seed = 0x3dac0000 + iteration*1024 + episode\","
            "\"midIterationCrash\":\"replay same already-opened iteration from prior atomic checkpoint\","
            "\"iterationZeroCrash\":\"NEW with absent checkpoint deterministically replays iteration zero\","
            "\"failedProcessTimeAccounting\":\"failed mid-iteration process time is excluded; successful committed training time is cumulative\","
            "\"sourceShaRequired\":true,\"cumulativeWallSecondsPersisted\":true,"
            "\"checkpointIoReserveSeconds\":" << kCheckpointIoReserveSeconds << ','
         << "\"completedCheckpointImmutable\":true},"
         << "\n  \"commands\":{\"selfTest\":\"--self-test\","
            "\"preflight\":\"--preflight SOURCE_SHA OUTPUT\","
            "\"newTraining\":\"--train EXECUTE_FROZEN_PRIMAL_DUAL_3DAC_PROTOCOL SOURCE_SHA CHECKPOINT OUTPUT NEW\","
            "\"resumeTraining\":\"--train EXECUTE_FROZEN_PRIMAL_DUAL_3DAC_PROTOCOL SOURCE_SHA CHECKPOINT OUTPUT RESUME\","
            "\"gates\":\"--gates EXECUTE_FROZEN_PRIMAL_DUAL_3DAC_PROTOCOL SOURCE_SHA CHECKPOINT OUTPUT\"},"
         << "\n  \"gates\":{\"order\":[\"stageA-candidate\","
            "\"stageA-D4\",\"stageB-candidate\",\"stageB-D4\","
            "\"stageC-candidate\"],\"candidateFirst\":true,"
            "\"stageA\":{\"score\":500000,\"moves\":150,"
            "\"lowerQuartileMoves\":90,\"clearsPerMove\":2.15,"
            "\"revealsPerMove\":1.18,\"D4Ratios\":1.15,\"jointWins\":20},"
            "\"stageB\":{\"score\":750000,\"moves\":220,"
            "\"lowerQuartileMoves\":140,\"clearsPerMove\":2.25,"
            "\"revealsPerMove\":1.28,\"D4Ratios\":1.15,\"jointWins\":80},"
            "\"stageC\":{\"games\":256,\"moveCap\":2000,"
            "\"meanScoreStrictlyAbove\":1050000,"
            "\"bootstrapLower95StrictlyAbove\":1000000,"
            "\"meanMovesStrictlyAbove\":300,\"clearsPerMove\":2.30,"
            "\"revealsPerMove\":1.32}},"
         << "\n  \"resources\":{\"trainingWallSecondsCumulativeCommitted\":"
         << kMaximumWallSeconds
         << ",\"gateWallSecondsSeparateCommand\":" << kMaximumWallSeconds
         << ",\"rssBytes\":" << kMaximumRssBytes
         << ",\"checkpointBytes\":" << kMaximumCheckpointBytes
         << ",\"maximumTrainingMoves\":" << kMaximumTrainingMoves
         << ",\"maximumResidentTransitions\":"
         << kMaximumResidentTransitions << "},"
         << "\n  \"failStop\":[\"self-test or preflight failure\","
            "\"resource cap\",\"nonfinite gradient or parameter\","
            "\"final greedy calibration support, entropy floor, drift UCB, or terminal-risk UCB failure\","
            "\"any candidate absolute gate failure\","
            "\"any paired D4 gate failure\"],"
         << "\n  \"freshGameplayExecutedDuringPreregistration\":false\n}\n";
  if (!output) throw std::runtime_error("preregistration write failed");
}

struct BurnedPreflightResult {
  std::uint64_t decisions = 0;
  std::int64_t total_score = 0;
  double seconds = 0;
};

BurnedPreflightResult burnedPreflight(const Network& network) {
  const auto started = Clock::now();
  BurnedPreflightResult result;
  for (std::uint32_t seed = kBurnedPreflightLane.first;
       seed <= kBurnedPreflightLane.last; ++seed) {
    requireSeed(seed, SeedUse::kBurnedPreflight);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < 25) {
      const int action = greedyAction(predict(network, publicState(state)));
      MoveResult move;
      if (!playHeadlessMove(state, seed, action, move)) {
        throw std::runtime_error("burned preflight transition failed");
      }
      ++result.decisions;
    }
    result.total_score += state.score;
  }
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

int preflight(std::string_view source_sha256, const std::string& path,
              std::ostream& progress) {
  if (!isSha256(source_sha256)) throw std::invalid_argument("invalid source SHA");
  if (!selfTest(progress)) return 1;
  Network rollout_network;
  const BurnedPreflightResult burned = burnedPreflight(rollout_network);
  if (burned.decisions == 0) throw std::runtime_error("empty burned preflight");

  Sample sample;
  sample.state = gradientFixture();
  sample.base_logits = baseLogits(sample.state);
  const Prediction prediction =
      rollout_network.predict(sample.state, sample.base_logits);
  sample.action = greedyAction(prediction);
  sample.old_log_probability = prediction.log_probabilities[sample.action];
  sample.old_values = prediction.values;
  sample.value_mask.fill(true);
  sample.policy_advantage = 0.5;
  sample.trajectory_weight = 1;
  for (int head = 0; head < kValueHeads; ++head) {
    sample.targets[head] = prediction.values[head] + 0.1;
  }
  std::vector<Sample> benchmark_samples(64, sample);
  std::vector<std::size_t> order(benchmark_samples.size());
  std::iota(order.begin(), order.end(), 0);
  const auto optimizer_started = Clock::now();
  std::vector<double> gradient;
  const double optimizer_loss = batchLossAndGradient(
      rollout_network, benchmark_samples, order, 0, order.size(), &gradient);
  rollout_network.applyAdam(gradient, 1);
  const double optimizer_seconds = std::chrono::duration<double>(
      Clock::now() - optimizer_started).count();
  if (!std::isfinite(optimizer_loss) || optimizer_seconds <= 0) {
    throw std::runtime_error("optimizer preflight failed");
  }

  constexpr std::uint64_t expected_training_moves =
      static_cast<std::uint64_t>(kIterations) *
      (static_cast<std::uint64_t>(kInitialEpisodesPerIteration) * 120u +
       static_cast<std::uint64_t>(kRestartEpisodesPerIteration) * 100u);
  const double rollout_seconds_per_move = burned.seconds / burned.decisions;
  const double optimizer_seconds_per_sample =
      optimizer_seconds / benchmark_samples.size();
  const double projected_rollout_seconds =
      expected_training_moves * rollout_seconds_per_move / kWorkers;
  const double projected_optimizer_seconds =
      expected_training_moves * kPpoEpochs * optimizer_seconds_per_sample;
  const double projected_total_seconds =
      1.35 * (projected_rollout_seconds + projected_optimizer_seconds);
  const double hard_projected_total_seconds = 1.35 *
      (kMaximumTrainingMoves * rollout_seconds_per_move / kWorkers +
       kMaximumTrainingMoves * kPpoEpochs * optimizer_seconds_per_sample);
  const std::uint64_t projected_memory =
      kMaximumResidentTransitions * (sizeof(Step) + sizeof(Sample)) +
      64ull * 1024ull * 1024ull;
  TrainingState checkpoint;
  checkpoint.source_sha256 = std::string(source_sha256);
  const std::size_t checkpoint_bytes = serializeCheckpoint(checkpoint).size();
  const bool passed = projected_total_seconds <= kMaximumWallSeconds &&
                      hard_projected_total_seconds <= kMaximumWallSeconds &&
                      projected_memory <= kMaximumRssBytes &&
                      checkpoint_bytes <= kMaximumCheckpointBytes &&
                      peakRssBytes() <= kMaximumRssBytes;

  std::ofstream output(path, std::ios::trunc);
  if (!output) throw std::runtime_error("could not create preflight artifact");
  output << std::fixed << std::setprecision(9)
         << "{\n  \"format\":\"drop7-primal-dual-preflight-v1\","
         << "\n  \"passed\":" << (passed ? "true" : "false")
         << ",\n  \"sourceSha256\":\"" << source_sha256 << "\","
         << "\n  \"freshGameplaySeedsOpened\":0,"
         << "\n  \"burnedReplay\":{\"lane\":\"0x3d6e4000..0x3d6e4003\","
            "\"games\":4,\"maximumMovesEach\":25,\"decisions\":"
         << burned.decisions << ",\"totalObservedScore\":"
         << burned.total_score << ",\"seconds\":" << burned.seconds << "},"
         << "\n  \"throughput\":{\"rolloutSecondsPerDecision\":"
         << rollout_seconds_per_move << ",\"optimizerSecondsPerSample\":"
         << optimizer_seconds_per_sample << ",\"optimizerProbeLoss\":"
         << optimizer_loss << "},"
         << "\n  \"projection\":{\"expectedTrainingMoves\":"
         << expected_training_moves << ",\"hardMaximumTrainingMoves\":"
         << kMaximumTrainingMoves << ",\"rolloutSeconds\":"
         << projected_rollout_seconds << ",\"optimizerSeconds\":"
         << projected_optimizer_seconds << ",\"totalWith35PercentGuard\":"
         << projected_total_seconds << ",\"hardMaximumWith35PercentGuard\":"
         << hard_projected_total_seconds << ",\"wallCapSeconds\":"
         << kMaximumWallSeconds << "},"
         << "\n  \"memory\":{\"sizeofStep\":" << sizeof(Step)
         << ",\"sizeofSample\":" << sizeof(Sample)
         << ",\"projectedPeakBytes\":" << projected_memory
         << ",\"measuredPeakRssBytes\":" << peakRssBytes()
         << ",\"rssCapBytes\":" << kMaximumRssBytes << "},"
         << "\n  \"checkpoint\":{\"bytes\":" << checkpoint_bytes
         << ",\"capBytes\":" << kMaximumCheckpointBytes << "},"
         << "\n  \"guards\":{\"strictSeedLanes\":true,"
            "\"wholeGameCalibration\":true,\"candidateFirst\":true,"
            "\"absoluteFiveMoveDrift\":true,\"resumeRoundtrip\":true}\n}\n";
  if (!output) throw std::runtime_error("preflight artifact write failed");
  progress << "PRIMAL_DUAL_PREFLIGHT {\"passed\":"
           << (passed ? "true" : "false")
           << ",\"projectedSeconds\":" << projected_total_seconds
           << ",\"projectedPeakBytes\":" << projected_memory
           << ",\"artifact\":\"" << path << "\"}\n";
  return passed ? 0 : 1;
}

}  // namespace drop7::primal_dual_actor_critic

#ifndef DROP7_PRIMAL_DUAL_ACTOR_CRITIC_LIBRARY
int main(int argc, char** argv) {
  using namespace drop7::primal_dual_actor_critic;
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
      return selfTest(std::cout) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc == 4 && std::string_view(argv[1]) == "--preregister") {
      writePreregistration(argv[3], argv[2]);
      std::cout << "PRIMAL_DUAL_PREREGISTERED {\"sourceSha256\":\""
                << argv[2] << "\",\"artifact\":\"" << argv[3]
                << "\",\"freshGameplaySeedsOpened\":0}\n";
      return EXIT_SUCCESS;
    }
    if (argc == 4 && std::string_view(argv[1]) == "--preflight") {
      return preflight(argv[2], argv[3], std::cout);
    }
    if (argc == 7 && std::string_view(argv[1]) == "--train") {
      const std::string_view mode(argv[6]);
      if (mode != "NEW" && mode != "RESUME") {
        throw std::invalid_argument("train mode must be NEW or RESUME");
      }
      return train(argv[2], argv[3], argv[4], argv[5], mode == "RESUME",
                   std::cout);
    }
    if (argc == 6 && std::string_view(argv[1]) == "--gates") {
      return gates(argv[2], argv[3], argv[4], argv[5], std::cout);
    }
    std::cerr
        << "usage: drop7_primal_dual_actor_critic --self-test | "
           "--preregister SOURCE_SHA OUTPUT | --preflight SOURCE_SHA OUTPUT | "
           "--train TOKEN SOURCE_SHA CHECKPOINT OUTPUT NEW|RESUME | "
           "--gates TOKEN SOURCE_SHA CHECKPOINT OUTPUT\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
#endif
