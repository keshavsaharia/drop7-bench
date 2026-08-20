#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#include "../../fair-expectimax/reference/fair-only-depth4.cpp"
#undef DROP7_FAIR_ONLY_DEPTH4_LIBRARY

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
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
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

// Implements regenerative Drop7 expert iteration with a preflight that reads
// no new gameplay seeds.  Guarded game/training commands accept only the fixed
// seed lanes and an explicit protocol token.  `self-test`, `preregister`, and
// `preflight` use synthetic fixtures and a previously evaluated development
// D4 root corpus only.
namespace drop7::regenerative_expert_iteration {

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// Fixed protocol configuration
// ---------------------------------------------------------------------------

constexpr int kSearchSimulations = 98;
constexpr int kSearchDepthMoves = 20;
constexpr int kDeploymentMaximumPly = 8;
constexpr int kChanceStrata = 7;
constexpr int kWorkers = 8;
constexpr int kRounds = 8;
constexpr int kOnPolicyRootsPerRound = 20'000;
constexpr int kReanalysisRootsPerRound = 5'000;
constexpr int kTotalSearchedRoots =
    kRounds * (kOnPolicyRootsPerRound + kReanalysisRootsPerRound);
constexpr std::uint64_t kProjectedLeaves =
    static_cast<std::uint64_t>(kTotalSearchedRoots) * kSearchSimulations;
constexpr std::uint64_t kMaximumSyntheticTransitions =
    kProjectedLeaves * kSearchDepthMoves;
constexpr std::uint64_t kMaximumNnueEvaluations =
    kMaximumSyntheticTransitions + kTotalSearchedRoots;  // Includes root priors.
constexpr std::uint64_t kMaximumRssBytes = 512u * 1024u * 1024u;
constexpr double kMaximumWallSeconds = 12.0 * 60.0 * 60.0;
constexpr std::uint64_t kBrowserArenaBytes = 32u * 1024u * 1024u;
constexpr std::array<int, 4> kBrowserSimulationSteps{{49, 63, 77, 98}};

// Each stochastic domain is distinct and fixed before any fresh lane is read.
// Inputs are canonical public hashes plus node depth and visit/event indices.
constexpr std::uint32_t kRootPackDomain = 0x5250'4b31u;       // "RPK1"
constexpr std::uint32_t kChanceEventDomain = 0x4348'4e31u;    // "CHN1"
constexpr std::uint32_t kPolicySampleDomain = 0x504f'4c31u;   // "POL1"
constexpr std::uint32_t kReplayDomain = 0x5250'4c31u;         // "RPL1"
constexpr std::uint32_t kTrainingShuffleDomain = 0x5348'4631u;  // "SHF1"
constexpr std::uint32_t kCalibrationDomain = 0x4341'4c31u;    // "CAL1"

struct SeedLane {
  std::uint32_t first;
  std::uint32_t last;
};

constexpr SeedLane kD4InitializationLane{0x3da4'0000u, 0x3da4'003fu};
constexpr SeedLane kExpertGameLane{0x3da4'1000u, 0x3da7'ffffu};
constexpr SeedLane kStageALane{0x3da8'0000u, 0x3da8'001fu};
constexpr SeedLane kStageBLane{0x3da9'0000u, 0x3da9'007fu};
constexpr SeedLane kDevelopmentConfirmationLane{0x3daa'0000u,
                                                 0x3daa'00ffu};
constexpr SeedLane kReservedLane{0x3dab'0000u, 0x3dab'ffffu};

struct StageGate {
  int games;
  double minimum_score;
  double minimum_moves;
  double minimum_bottom_quartile_moves;
  double minimum_clears_per_move;
  double minimum_reveals_per_move;
  double minimum_score_ratio_vs_d4;
  double minimum_move_ratio_vs_d4;
  int minimum_joint_wins;
};

constexpr StageGate kStageAGate{32, 500'000.0, 150.0, 90.0, 2.15, 1.18,
                                1.15, 1.15, 20};
constexpr StageGate kStageBGate{128, 750'000.0, 220.0, 140.0, 2.25, 1.28,
                                1.15, 1.15, 80};

struct FinalDevelopmentGate {
  int games;
  int maximum_moves;
  double minimum_mean_score;
  double minimum_bootstrap_lower95_score;
  double minimum_mean_moves;
  double minimum_clears_per_move;
  double minimum_reveals_per_move;
};

constexpr FinalDevelopmentGate kStageCGate{256, 2'000, 1'050'000.0,
                                            1'000'000.0, 300.0, 2.30, 1.32};

constexpr double kPolicyLossWeight = 1.0;
constexpr double kScoreQuantileLossWeight = 0.5;
constexpr double kLifetimeQuantileLossWeight = 0.25;
constexpr double kRegenerationLossWeight = 0.2;
constexpr double kFlowLossWeight = 0.1;
constexpr double kL2Weight = 1.0e-5;
constexpr double kMeanUtilityWeight = 0.8;
constexpr double kCvarUtilityWeight = 0.2;
constexpr double kCvarFraction = 0.25;
// Fixed target normalizers keep every regression head in a trainable range.
// Checkpoints store normalized outputs; only selection/calibration decode them.
constexpr double kScoreTargetScale = 1'000'000.0;
constexpr double kLifetimeTargetScale = 500.0;
constexpr double kFlowPerMoveScale = 8.0;
constexpr std::array<int, 4> kFlowHorizonMoves{{5, 10, 20, 40}};

// Every non-calibration example is consumed exactly once per epoch.  This is
// deliberately an epoch schedule, not a small with-replacement update sample.
constexpr int kD4PretrainingEpochs = 4;
constexpr int kOptimizerEpochsPerRound = 1;
constexpr int kOptimizerBatchSize = 64;
constexpr double kOptimizerLearningRate = 2.5e-4;
constexpr double kOptimizerBeta1 = 0.9;
constexpr double kOptimizerBeta2 = 0.999;
constexpr double kOptimizerEpsilon = 1.0e-8;
constexpr double kGradientNormClip = 1.0;
constexpr double kDominanceTolerance = 1.0e-6;
constexpr double kScoreDominanceMargin = 25'000.0;
constexpr double kLifetimeDominanceMargin = 5.0;
constexpr double kRegenerationDominanceMargin = 0.02;
constexpr double kFlowDominanceMargin = 0.05;
constexpr int kCalibrationMinimumExamplesPerHalf = 1'024;
constexpr double kLifetimeCoverageTarget = 0.50;
constexpr double kLifetimeCoverageTolerance = 0.15;
constexpr double kLifetimeLowerCoverageTarget = 0.25;
constexpr double kLifetimeLowerCoverageTolerance = 0.10;
constexpr int kCalibrationMinimumPlayedPerColumn = 32;
constexpr double kRegenerationMaximumEce = 0.12;
constexpr double kRegenerationMaximumBrier = 0.20;
constexpr double kFlowMaximumNormalizedMae = 0.25;
constexpr int kMaximumGameMoves = 2'000;
constexpr std::uint64_t kMaximumD4BootstrapRoots =
    64u * static_cast<std::uint64_t>(kMaximumGameMoves);
constexpr std::uint64_t kMaximumFinalReplayRoots =
    kMaximumD4BootstrapRoots + kRounds * kOnPolicyRootsPerRound;
constexpr int kBootstrapReplicates = 10'000;

constexpr std::string_view kBurnedCorpusPath =
    "/tmp/drop7-d4-public-root-labels.jsonl";
constexpr std::uintmax_t kBurnedCorpusBytes = 527'391;
constexpr std::string_view kBurnedCorpusSha256 =
    "f61801abc9eefe86011f7202620a18c1277fcc1b5a24f4bce5947033b791dd89";
constexpr std::string_view kFreshExecutionToken =
    "EXECUTE_FROZEN_REGENERATIVE_3DA4_PROTOCOL";

static_assert(kLevelBonus == 17'000);
static_assert(kTotalSearchedRoots == 200'000);
static_assert(kProjectedLeaves == 19'600'000u);
static_assert(kMaximumSyntheticTransitions == 392'000'000u);
static_assert(kD4InitializationLane.last - kD4InitializationLane.first + 1u ==
              64u);
static_assert(kStageALane.last - kStageALane.first + 1u == 32u);
static_assert(kStageBLane.last - kStageBLane.first + 1u == 128u);
static_assert(kDevelopmentConfirmationLane.last -
                      kDevelopmentConfirmationLane.first +
                  1u ==
              256u);
static_assert(kD4InitializationLane.last < kExpertGameLane.first &&
              kExpertGameLane.last < kStageALane.first &&
              kStageALane.last < kStageBLane.first &&
              kStageBLane.last < kDevelopmentConfirmationLane.first &&
              kDevelopmentConfirmationLane.last < kReservedLane.first);
static_assert(kStageAGate.games == 32 && kStageAGate.minimum_score == 500'000.0 &&
              kStageAGate.minimum_moves == 150.0 &&
              kStageAGate.minimum_bottom_quartile_moves == 90.0 &&
              kStageAGate.minimum_clears_per_move == 2.15 &&
              kStageAGate.minimum_reveals_per_move == 1.18 &&
              kStageAGate.minimum_score_ratio_vs_d4 == 1.15 &&
              kStageAGate.minimum_move_ratio_vs_d4 == 1.15 &&
              kStageAGate.minimum_joint_wins == 20);
static_assert(kStageBGate.games == 128 &&
              kStageBGate.minimum_joint_wins == 80 &&
              kStageCGate.games == 256 &&
              kStageCGate.maximum_moves == 2'000 &&
              kStageCGate.minimum_bootstrap_lower95_score == 1'000'000.0);
static_assert(kPolicyLossWeight == 1.0 &&
              kScoreQuantileLossWeight == 0.5 &&
              kLifetimeQuantileLossWeight == 0.25 &&
              kRegenerationLossWeight == 0.2 && kFlowLossWeight == 0.1 &&
              kL2Weight == 1.0e-5);
static_assert(kOnPolicyRootsPerRound % kWorkers == 0 &&
              kReanalysisRootsPerRound % kWorkers == 0 &&
              kD4PretrainingEpochs == 4 &&
              kOptimizerEpochsPerRound == 1 &&
              kOptimizerBatchSize == 64 && kDeploymentMaximumPly == 8 &&
              kMaximumGameMoves == 2'000 && kGradientNormClip == 1.0 &&
              kChanceStrata == 7 && kBrowserSimulationSteps.front() == 49 &&
              kBrowserSimulationSteps.back() == kSearchSimulations);

// ---------------------------------------------------------------------------
// Public boundary, reflection, and domain hashing
// ---------------------------------------------------------------------------

struct PublicState {
  Board board{};
  std::uint8_t next_disc = 1;
  std::uint8_t phase = kMovesPerLevel;
  bool terminal = false;

  bool operator==(const PublicState&) const = default;
};

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

Board mirrorBoard(const Board& source) {
  Board result{};
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      result[indexOf(row, kBoardSize - 1 - column)] =
          source[indexOf(row, column)];
    }
  }
  return result;
}

PublicState mirror(const PublicState& source) {
  PublicState result = source;
  result.board = mirrorBoard(source.board);
  return result;
}

bool mirrorIsSmaller(const Board& source) {
  const Board reflected = mirrorBoard(source);
  return std::lexicographical_compare(reflected.begin(), reflected.end(),
                                      source.begin(), source.end());
}

PublicState canonical(const PublicState& source, bool& was_mirrored) {
  was_mirrored = mirrorIsSmaller(source.board);
  return was_mirrored ? mirror(source) : source;
}

std::uint64_t publicHash(const PublicState& source) {
  bool ignored = false;
  const PublicState value = canonical(source, ignored);
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  const auto add = [&hash](std::uint8_t byte) {
    hash ^= byte;
    hash *= 0x0000'0100'0000'01b3ull;
  };
  for (const std::uint8_t cell : value.board) add(cell);
  add(value.next_disc);
  add(value.phase);
  add(value.terminal ? 1u : 0u);
  return hash;
}

std::uint32_t foldHash(std::uint64_t value) {
  return mix32(static_cast<std::uint32_t>(value) ^
               static_cast<std::uint32_t>(value >> 32));
}

std::uint32_t domainBits(std::uint32_t domain, const PublicState& state,
                         int depth, std::uint32_t visit,
                         std::uint32_t event) {
  const std::uint32_t base = foldHash(publicHash(state));
  return mix32(base ^ domain ^
               (static_cast<std::uint32_t>(depth + 1) * 0x9e37'79b9u) ^
               ((visit + 1u) * 0x85eb'ca6bu) ^
               ((event + 1u) * 0xc2b2'ae35u));
}

class ChancePackRandom {
 public:
  ChancePackRandom(const PublicState& state, int action, int depth,
                   std::uint32_t visit, int stratum,
                   std::uint32_t event_prefix = 0)
      : state_(state),
        depth_(depth),
        pack_(visit / static_cast<std::uint32_t>(kChanceStrata)),
        stratum_(stratum),
        event_(event_prefix),
        event_prefix_(event_prefix),
        mirrored_(mirrorIsSmaller(state.board) ||
                  (state.board == mirrorBoard(state.board) &&
                   action > kBoardSize / 2)) {
    if (action < 0 || action >= kBoardSize) {
      throw std::invalid_argument("chance action outside board");
    }
    if (stratum < 0 || stratum >= kChanceStrata) {
      throw std::invalid_argument("chance stratum outside [0,7)");
    }
  }

  std::uint8_t nextDisc() {
    const std::uint32_t bits = domainBits(kChanceEventDomain, state_, depth_,
                                          pack_, event_++);
    const int rotation = static_cast<int>(bits % kChanceStrata);
    return static_cast<std::uint8_t>(1 + (stratum_ + rotation) % kChanceStrata);
  }

  std::uint8_t nextDiscFor(int row, int column, int wave_depth) const {
    // On a reflection-fixed board the action breaks the orientation tie for
    // side-move pairs.  The center action deliberately retains distinct event
    // identities for mirrored hidden discs; collapsing them would force those
    // two independent values to be equal in every Latin-hypercube visit.
    const int reflected_column = kBoardSize - 1 - column;
    const int canonical_column =
        mirrored_ ? reflected_column : column;
    const std::uint32_t event =
        event_prefix_ + static_cast<std::uint32_t>(wave_depth * 64) +
        static_cast<std::uint32_t>(row * kBoardSize + canonical_column);
    const std::uint32_t bits =
        domainBits(kChanceEventDomain, state_, depth_, pack_, event);
    const int rotation = static_cast<int>(bits % kChanceStrata);
    return static_cast<std::uint8_t>(1 + (stratum_ + rotation) % kChanceStrata);
  }

 private:
  PublicState state_;
  int depth_;
  // Keep the random rotation fixed for each consecutive seven-visit pack.
  // The stratum changes on every visit, so a stable event in visits 7p..7p+6
  // is an exact permutation of discs 1..7 while a later pack can rotate it
  // independently.  Hashing the raw visit here destroys that guarantee.
  std::uint32_t pack_;
  int stratum_;
  std::uint32_t event_;
  std::uint32_t event_prefix_;
  bool mirrored_;
};

// ---------------------------------------------------------------------------
// Exact generic transition (the same order and score semantics as engine.hpp)
// ---------------------------------------------------------------------------

template <typename Random>
void resolveCascadeGeneric(Board& board, Random& random, int starting_depth,
                           std::int64_t& score, std::vector<Wave>& waves) {
  for (int depth = starting_depth;; ++depth) {
    int popper_count = 0;
    const auto poppers = findPoppers(board, popper_count);
    if (popper_count == 0) return;

    std::array<bool, kCellCount> popping{};
    Board cleared = board;
    for (int offset = 0; offset < popper_count; ++offset) {
      const int index = poppers[offset];
      popping[index] = true;
      cleared[index] = kEmpty;
    }

    std::array<int, kCellCount> reveals{};
    int reveal_count = 0;
    constexpr std::array<std::array<int, 2>, 4> directions{{
        {{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}},
    }};
    for (int row = 0; row < kBoardSize; ++row) {
      for (int column = 0; column < kBoardSize; ++column) {
        const int index = indexOf(row, column);
        const std::uint8_t cell = board[index];
        if (cell != kSolid && cell != kCracked) continue;
        int hits = 0;
        for (const auto& direction : directions) {
          const int neighbor_row = row + direction[0];
          const int neighbor_column = column + direction[1];
          if (inside(neighbor_row, neighbor_column) &&
              popping[indexOf(neighbor_row, neighbor_column)]) {
            ++hits;
          }
        }
        if (hits == 0) continue;
        const int required = cell == kSolid ? 2 : 1;
        if (hits >= required) {
          reveals[reveal_count++] = index;
        } else {
          cleared[index] = kCracked;
        }
      }
    }
    for (int offset = 0; offset < reveal_count; ++offset) {
      const int reveal_index = reveals[offset];
      if constexpr (requires(Random& value) {
                      value.nextDiscFor(0, 0, 0);
                    }) {
        cleared[reveal_index] = random.nextDiscFor(
            reveal_index / kBoardSize, reveal_index % kBoardSize, depth);
      } else {
        cleared[reveal_index] = random.nextDisc();
      }
    }
    const std::int64_t points = popper_count * scoreForWave(depth);
    score += points;
    waves.push_back({depth, popper_count, reveal_count, points});
    board = applyGravity(cleared);
  }
}

template <typename Random>
bool playMoveGeneric(const State& state, int column, Random& random,
                     MoveResult& result) {
  if (state.game_over) return false;
  Board board = state.board;
  if (!placeDisc(board, column, state.next_disc)) return false;

  result = MoveResult{};
  std::int64_t first_score = 0;
  resolveCascadeGeneric(board, random, 1, first_score, result.waves);
  result.score_delta = first_score;
  result.cleared_board = isBoardEmpty(board);
  if (result.cleared_board) result.score_delta += kClearBonus;

  int level = state.level;
  int moves_remaining = state.moves_remaining - 1;
  bool game_over = false;
  if (moves_remaining == 0) {
    Board raised{};
    if (!raiseCoveredRow(board, raised)) {
      game_over = true;
    } else {
      result.level_advanced = true;
      ++level;
      moves_remaining = kMovesPerLevel;
      result.score_delta += kLevelBonus;
      board = raised;
      std::int64_t level_score = 0;
      const int next_depth =
          result.waves.empty() ? 1 : result.waves.back().depth + 1;
      resolveCascadeGeneric(board, random, next_depth, level_score,
                            result.waves);
      result.score_delta += level_score;
      if (isBoardEmpty(board)) {
        result.score_delta += kClearBonus;
        result.cleared_board = true;
      }
    }
  }

  int legal_count = 0;
  legalColumns(board, legal_count);
  if (!game_over && legal_count == 0) game_over = true;

  result.state.board = board;
  result.state.next_disc = game_over ? state.next_disc : random.nextDisc();
  result.state.score = state.score + result.score_delta;
  result.state.level = level;
  result.state.moves_remaining = moves_remaining;
  result.state.moves_played = state.moves_played + 1;
  result.state.game_over = game_over;
  return true;
}

struct PublicTransition {
  PublicState state{};
  std::int64_t score_delta = 0;
  int cleared = 0;
  int revealed = 0;
  bool level_advanced = false;
};

PublicTransition chanceTransition(const PublicState& source, int action,
                                  int depth, std::uint32_t visit, int stratum,
                                  std::uint32_t event_prefix = 0) {
  ChancePackRandom random(source, action, depth, visit, stratum,
                          event_prefix);
  MoveResult move;
  if (!playMoveGeneric(materialize(source), action, random, move)) {
    throw std::invalid_argument("illegal chance transition action");
  }
  int cleared = 0;
  int revealed = 0;
  for (const Wave& wave : move.waves) {
    cleared += wave.cleared;
    revealed += wave.revealed;
  }
  return {publicState(move.state), move.score_delta, cleared, revealed,
          move.level_advanced};
}

// ---------------------------------------------------------------------------
// Reflection-exact action-conditioned NNUE
// ---------------------------------------------------------------------------

constexpr int kCellKinds = 10;
constexpr int kStateUnits = 128;
constexpr int kRelativeUnits = 64;
constexpr int kTrunkUnits = 128;
constexpr int kScoreQuantiles = 32;
constexpr int kLifetimeQuantiles = 32;
constexpr int kRegenerationHeads = 4;
constexpr int kFlowHeads = 8;
constexpr int kRelativeDistances = 13;

struct ModelLayout {
  static constexpr int kStateBoard = 0;
  static constexpr int kStateNext =
      kStateBoard + kCellCount * kCellKinds * kStateUnits;
  static constexpr int kStatePhase =
      kStateNext + kBoardSize * kStateUnits;
  static constexpr int kStateBias =
      kStatePhase + kMovesPerLevel * kStateUnits;
  static constexpr int kRelative = kStateBias + kStateUnits;
  static constexpr int kRelativeBias =
      kRelative + kBoardSize * kRelativeDistances * kCellKinds *
                      kRelativeUnits;
  static constexpr int kFusion = kRelativeBias + kRelativeUnits;
  static constexpr int kTrunkBias =
      kFusion + (kStateUnits + kRelativeUnits) * kTrunkUnits;
  static constexpr int kPolicy = kTrunkBias + kTrunkUnits;
  static constexpr int kScore = kPolicy + kTrunkUnits + 1;
  static constexpr int kLifetime =
      kScore + kScoreQuantiles * kTrunkUnits + kScoreQuantiles;
  static constexpr int kRegeneration =
      kLifetime + kLifetimeQuantiles * kTrunkUnits + kLifetimeQuantiles;
  static constexpr int kFlow =
      kRegeneration + kRegenerationHeads * kTrunkUnits +
      kRegenerationHeads;
  static constexpr int kCount =
      kFlow + kFlowHeads * kTrunkUnits + kFlowHeads;
};

constexpr int kParameterCount = ModelLayout::kCount;
constexpr std::uint64_t kMaximumModelWeights = 196'608;
constexpr std::uint64_t kMaximumCheckpointBytes = 1024u * 1024u;
constexpr std::uint64_t kCheckpointHeaderBytes = 120;
constexpr std::uint64_t kFloat32CheckpointBytes =
    kCheckpointHeaderBytes + 4u * kParameterCount;

static_assert(kParameterCount == 157'325);
static_assert(kParameterCount <= kMaximumModelWeights);
static_assert(kFloat32CheckpointBytes <= kMaximumCheckpointBytes);

float clippedRelu(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

struct CandidatePrediction {
  float policy_logit = 0.0f;
  std::array<float, kScoreQuantiles> score{};
  std::array<float, kLifetimeQuantiles> lifetime{};
  std::array<float, kRegenerationHeads> regeneration{};
  std::array<float, kFlowHeads> flow{};

  bool operator==(const CandidatePrediction&) const = default;
};

struct Prediction {
  std::array<CandidatePrediction, kBoardSize> candidate{};
  std::array<bool, kBoardSize> legal{};
};

struct ConstraintTrust {
  bool lifetime = false;
  bool regeneration = false;
  bool flow = false;

  bool all() const { return lifetime && regeneration && flow; }
  bool operator==(const ConstraintTrust&) const = default;
};

class Model {
 public:
  Model() : weights_(kParameterCount, 0.0f) {}

  static Model initialized() {
    Model result;
    for (int index = 0; index < kParameterCount; ++index) {
      const std::uint32_t bits = mix32(
          kTrainingShuffleDomain ^
          (static_cast<std::uint32_t>(index + 1) * 0x9e37'79b9u));
      const int centered = static_cast<int>(bits % 2001u) - 1000;
      result.weights_[index] = static_cast<float>(centered) * 0.00002f;
    }
    return result;
  }

  const std::vector<float>& weights() const { return weights_; }
  std::vector<float>& weights() { return weights_; }

  Prediction predict(const PublicState& source) const {
    if (source.next_disc < 1 || source.next_disc > kBoardSize ||
        source.phase < 1 || source.phase > kMovesPerLevel || source.terminal) {
      throw std::invalid_argument("prediction outside public domain");
    }
    Prediction result;
    const PublicState reflected = mirror(source);
    const StateAccumulator direct_state = accumulateState(source);
    const StateAccumulator reflected_state = accumulateState(reflected);
    for (int action = 0; action < kBoardSize; ++action) {
      result.legal[action] = !source.terminal && isLegal(source.board, action);
      if (!result.legal[action]) {
        result.candidate[action].policy_logit =
            -std::numeric_limits<float>::infinity();
        continue;
      }
      const CandidatePrediction direct =
          evaluateOrientation(source, action, direct_state);
      const CandidatePrediction reverse = evaluateOrientation(
          reflected, kBoardSize - 1 - action, reflected_state);
      result.candidate[action] = average(direct, reverse);
    }
    return result;
  }

 private:
  using StateAccumulator = std::array<float, kStateUnits>;
  using RelativeAccumulator = std::array<float, kRelativeUnits>;
  using Trunk = std::array<float, kTrunkUnits>;

  static CandidatePrediction average(const CandidatePrediction& first,
                                     const CandidatePrediction& second) {
    CandidatePrediction result;
    result.policy_logit =
        static_cast<float>((static_cast<double>(first.policy_logit) +
                            static_cast<double>(second.policy_logit)) *
                           0.5);
    const auto blend = [](const auto& left, const auto& right, auto& output) {
      for (std::size_t index = 0; index < output.size(); ++index) {
        output[index] =
            static_cast<float>((static_cast<double>(left[index]) +
                                static_cast<double>(right[index])) *
                               0.5);
      }
    };
    blend(first.score, second.score, result.score);
    blend(first.lifetime, second.lifetime, result.lifetime);
    blend(first.regeneration, second.regeneration, result.regeneration);
    blend(first.flow, second.flow, result.flow);
    return result;
  }

  StateAccumulator accumulateState(const PublicState& source) const {
    StateAccumulator result{};
    for (int unit = 0; unit < kStateUnits; ++unit) {
      result[unit] = weights_[ModelLayout::kStateBias + unit];
    }
    for (int position = 0; position < kCellCount; ++position) {
      const int token = source.board[position];
      const int offset = ModelLayout::kStateBoard +
                         (position * kCellKinds + token) * kStateUnits;
      for (int unit = 0; unit < kStateUnits; ++unit) {
        result[unit] += weights_[offset + unit];
      }
    }
    const int next_offset = ModelLayout::kStateNext +
                            (source.next_disc - 1) * kStateUnits;
    const int phase_offset = ModelLayout::kStatePhase +
                             (source.phase - 1) * kStateUnits;
    for (int unit = 0; unit < kStateUnits; ++unit) {
      result[unit] += weights_[next_offset + unit];
      result[unit] += weights_[phase_offset + unit];
      result[unit] = clippedRelu(result[unit]);
    }
    return result;
  }

  RelativeAccumulator accumulateRelative(const PublicState& source,
                                         int candidate) const {
    RelativeAccumulator result{};
    for (int unit = 0; unit < kRelativeUnits; ++unit) {
      result[unit] = weights_[ModelLayout::kRelativeBias + unit];
    }
    for (int row = 0; row < kBoardSize; ++row) {
      for (int column = 0; column < kBoardSize; ++column) {
        const int distance = column - candidate + (kBoardSize - 1);
        const int token = source.board[indexOf(row, column)];
        const int category =
            ((row * kRelativeDistances + distance) * kCellKinds + token);
        const int offset =
            ModelLayout::kRelative + category * kRelativeUnits;
        for (int unit = 0; unit < kRelativeUnits; ++unit) {
          result[unit] += weights_[offset + unit];
        }
      }
    }
    for (float& value : result) value = clippedRelu(value);
    return result;
  }

  Trunk fuse(const StateAccumulator& state,
             const RelativeAccumulator& relative) const {
    Trunk result{};
    constexpr int inputs = kStateUnits + kRelativeUnits;
    for (int output = 0; output < kTrunkUnits; ++output) {
      double sum = weights_[ModelLayout::kTrunkBias + output];
      const int base = ModelLayout::kFusion + output * inputs;
      for (int unit = 0; unit < kStateUnits; ++unit) {
        sum += static_cast<double>(weights_[base + unit]) * state[unit];
      }
      for (int unit = 0; unit < kRelativeUnits; ++unit) {
        sum += static_cast<double>(weights_[base + kStateUnits + unit]) *
               relative[unit];
      }
      result[output] = clippedRelu(static_cast<float>(sum));
    }
    return result;
  }

  template <std::size_t Outputs>
  std::array<float, Outputs> head(const Trunk& trunk, int offset) const {
    std::array<float, Outputs> result{};
    const int bias = offset + static_cast<int>(Outputs) * kTrunkUnits;
    for (std::size_t output = 0; output < Outputs; ++output) {
      double sum = weights_[bias + static_cast<int>(output)];
      const int row = offset + static_cast<int>(output) * kTrunkUnits;
      for (int unit = 0; unit < kTrunkUnits; ++unit) {
        sum += static_cast<double>(weights_[row + unit]) * trunk[unit];
      }
      result[output] = static_cast<float>(sum);
    }
    return result;
  }

  CandidatePrediction evaluateOrientation(
      const PublicState& source, int candidate,
      const StateAccumulator& state) const {
    const RelativeAccumulator relative = accumulateRelative(source, candidate);
    const Trunk trunk = fuse(state, relative);
    CandidatePrediction result;
    result.policy_logit = head<1>(trunk, ModelLayout::kPolicy)[0];
    result.score = head<kScoreQuantiles>(trunk, ModelLayout::kScore);
    result.lifetime =
        head<kLifetimeQuantiles>(trunk, ModelLayout::kLifetime);
    result.regeneration =
        head<kRegenerationHeads>(trunk, ModelLayout::kRegeneration);
    result.flow = head<kFlowHeads>(trunk, ModelLayout::kFlow);
    return result;
  }

  std::vector<float> weights_;
};

// ---------------------------------------------------------------------------
// Frozen targets and losses
// ---------------------------------------------------------------------------

double lowerCvar(std::vector<double> values, double fraction) {
  if (values.empty() || !(fraction > 0.0 && fraction <= 1.0)) {
    throw std::invalid_argument("invalid CVaR request");
  }
  std::sort(values.begin(), values.end());
  const double exact_count = fraction * static_cast<double>(values.size());
  const int whole = static_cast<int>(std::floor(exact_count));
  const double remainder = exact_count - whole;
  double sum = std::accumulate(values.begin(), values.begin() + whole, 0.0);
  double denominator = static_cast<double>(whole);
  if (remainder > 0.0) {
    sum += remainder * values[whole];
    denominator += remainder;
  }
  if (denominator == 0.0) return values.front();
  return sum / denominator;
}

double strategyUtility(const std::vector<double>& outcomes) {
  if (outcomes.empty()) throw std::invalid_argument("empty strategy outcomes");
  const double mean =
      std::accumulate(outcomes.begin(), outcomes.end(), 0.0) /
      static_cast<double>(outcomes.size());
  return kMeanUtilityWeight * mean +
         kCvarUtilityWeight * lowerCvar(outcomes, kCvarFraction);
}

template <typename Value, std::size_t Size>
double predictedScoreUtility(const std::array<Value, Size>& quantiles) {
  std::vector<double> values;
  values.reserve(Size);
  for (const Value& value : quantiles) {
    values.push_back(static_cast<double>(value));
  }
  return strategyUtility(values);
}

double huber(double residual, double delta = 1.0) {
  const double magnitude = std::abs(residual);
  if (magnitude <= delta) return 0.5 * residual * residual;
  return delta * (magnitude - 0.5 * delta);
}

double quantileHuberLoss(const std::vector<double>& predictions,
                         double target, double delta = 1.0) {
  if (predictions.empty()) throw std::invalid_argument("empty quantile head");
  double loss = 0.0;
  const double count = static_cast<double>(predictions.size());
  for (std::size_t index = 0; index < predictions.size(); ++index) {
    const double quantile = (static_cast<double>(index) + 0.5) / count;
    const double residual = target - predictions[index];
    const double direction = residual < 0.0 ? 1.0 : 0.0;
    loss += std::abs(quantile - direction) * huber(residual, delta);
  }
  return loss / count;
}

double binaryCrossEntropyFromLogit(double logit, bool target) {
  const double magnitude = std::abs(logit);
  return std::max(logit, 0.0) - logit * (target ? 1.0 : 0.0) +
         std::log1p(std::exp(-magnitude));
}

int occupiedCount(const Board& board) {
  return static_cast<int>(std::count_if(
      board.begin(), board.end(),
      [](std::uint8_t cell) { return cell != kEmpty; }));
}

int coveredCount(const Board& board) {
  return static_cast<int>(std::count_if(
      board.begin(), board.end(), [](std::uint8_t cell) {
        return cell == kSolid || cell == kCracked;
      }));
}

struct TrajectoryStep {
  PublicState state{};
  int cleared = 0;
  int revealed = 0;
};

struct AuxiliaryTargets {
  int regeneration_cycle = 0;  // zero means not regenerated by eight cycles.
  std::array<bool, kRegenerationHeads> regenerated_by{};
  std::array<float, kFlowHeads> flow{};
};

AuxiliaryTargets auxiliaryTargets(const PublicState& root,
                                  const std::vector<TrajectoryStep>& steps) {
  constexpr std::array<int, kRegenerationHeads> cycles{{1, 2, 4, 8}};
  AuxiliaryTargets result;
  int cumulative_clears = 0;
  int cumulative_reveals = 0;
  std::size_t cursor = 0;
  for (int cycle = 1; cycle <= cycles.back(); ++cycle) {
    const std::size_t limit =
        std::min(steps.size(), static_cast<std::size_t>(cycle * 5));
    while (cursor < limit) {
      cumulative_clears += steps[cursor].cleared;
      cumulative_reveals += steps[cursor].revealed;
      ++cursor;
    }
    for (std::size_t head = 0; head < cycles.size(); ++head) {
      if (cycle == cycles[head]) {
        result.flow[2 * head] = static_cast<float>(cumulative_clears);
        result.flow[2 * head + 1] = static_cast<float>(cumulative_reveals);
      }
    }
    if (result.regeneration_cycle == 0 && limit == static_cast<std::size_t>(cycle * 5)) {
      const PublicState& state = steps[limit - 1].state;
      if (state.phase == root.phase &&
          occupiedCount(state.board) <= occupiedCount(root.board) &&
          coveredCount(state.board) <= coveredCount(root.board)) {
        result.regeneration_cycle = cycle;
      }
    }
  }
  for (std::size_t head = 0; head < cycles.size(); ++head) {
    result.regenerated_by[head] = result.regeneration_cycle > 0 &&
                                  result.regeneration_cycle <= cycles[head];
  }
  return result;
}

// ---------------------------------------------------------------------------
// Exact Float32 deployment checkpoint
// ---------------------------------------------------------------------------

struct IterationResumeState;

class DeploymentCertificate {
 public:
  std::uint64_t modelFingerprint() const { return model_fingerprint_; }
  std::uint64_t replayFingerprint() const { return replay_fingerprint_; }
  std::uint64_t calibrationFingerprint() const {
    return calibration_fingerprint_;
  }
  std::uint64_t ledgerFingerprint() const { return ledger_fingerprint_; }

 private:
  DeploymentCertificate(std::uint64_t model_fingerprint,
                        std::uint64_t replay_fingerprint,
                        std::uint64_t calibration_fingerprint,
                        std::uint64_t ledger_fingerprint)
      : model_fingerprint_(model_fingerprint),
        replay_fingerprint_(replay_fingerprint),
        calibration_fingerprint_(calibration_fingerprint),
        ledger_fingerprint_(ledger_fingerprint) {}

  friend std::optional<DeploymentCertificate> certifyCompletedIteration(
      const IterationResumeState& state);

  std::uint64_t model_fingerprint_ = 0;
  std::uint64_t replay_fingerprint_ = 0;
  std::uint64_t calibration_fingerprint_ = 0;
  std::uint64_t ledger_fingerprint_ = 0;
};

constexpr std::array<char, 8> kCheckpointMagic{{'D', '7', 'R', 'E', 'G', 'E',
                                                'N', '3'}};
constexpr std::uint32_t kCheckpointVersion = 3;
constexpr std::uint32_t kCheckpointFloat32Codec = 1;
constexpr std::uint32_t kCheckpointDeploymentCertificateFlag = 1;
constexpr std::uint32_t kCheckpointCertificateVersion = 1;
constexpr std::uint32_t kCheckpointHeadSchema =
    kScoreQuantiles | (kLifetimeQuantiles << 8) |
    (kRegenerationHeads << 16) | (kFlowHeads << 24);
constexpr std::uint64_t kGoldenCheckpointFnv1a64 =
    0x86cb'0b98'f62e'c6b6ull;

std::uint64_t fnv1a64(const std::vector<std::uint8_t>& bytes) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const std::uint8_t byte : bytes) {
    hash ^= byte;
    hash *= 0x0000'0100'0000'01b3ull;
  }
  return hash;
}

std::uint64_t checkpointChecksum(const std::vector<std::uint8_t>& bytes) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    const std::uint8_t byte = index >= 24 && index < 32 ? 0u : bytes[index];
    hash ^= byte;
    hash *= 0x0000'0100'0000'01b3ull;
  }
  return hash;
}

void appendU32(std::vector<std::uint8_t>& output, std::uint32_t value) {
  for (int byte = 0; byte < 4; ++byte) {
    output.push_back(static_cast<std::uint8_t>((value >> (8 * byte)) & 0xffu));
  }
}

void appendU64(std::vector<std::uint8_t>& output, std::uint64_t value) {
  for (int byte = 0; byte < 8; ++byte) {
    output.push_back(static_cast<std::uint8_t>((value >> (8 * byte)) & 0xffu));
  }
}

std::uint32_t readU32(const std::vector<std::uint8_t>& source,
                      std::size_t offset) {
  if (offset + 4 > source.size()) throw std::runtime_error("truncated u32");
  std::uint32_t result = 0;
  for (int byte = 0; byte < 4; ++byte) {
    result |= static_cast<std::uint32_t>(source[offset + byte]) << (8 * byte);
  }
  return result;
}

std::uint64_t readU64(const std::vector<std::uint8_t>& source,
                      std::size_t offset) {
  if (offset + 8 > source.size()) throw std::runtime_error("truncated u64");
  std::uint64_t result = 0;
  for (int byte = 0; byte < 8; ++byte) {
    result |= static_cast<std::uint64_t>(source[offset + byte]) << (8 * byte);
  }
  return result;
}

void appendDouble(std::vector<std::uint8_t>& output, double value) {
  std::uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  appendU64(output, bits);
}

double readDouble(const std::vector<std::uint8_t>& source,
                  std::size_t offset) {
  const std::uint64_t bits = readU64(source, offset);
  double value = 0.0;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

std::uint64_t modelFingerprint(const Model& model) {
  std::vector<std::uint8_t> payload;
  payload.reserve(4u * model.weights().size());
  for (const float weight : model.weights()) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &weight, sizeof(bits));
    appendU32(payload, bits);
  }
  return fnv1a64(payload);
}

std::vector<std::uint8_t> serializeCheckpointInternal(
    const Model& model, const DeploymentCertificate* certificate) {
  if (!std::all_of(model.weights().begin(), model.weights().end(),
                   [](float value) { return std::isfinite(value); })) {
    throw std::invalid_argument("cannot serialize non-finite checkpoint model");
  }
  const bool deployment_certified = certificate != nullptr;
  const std::uint64_t fingerprint = modelFingerprint(model);
  if (deployment_certified && certificate->modelFingerprint() != fingerprint) {
    throw std::invalid_argument(
        "deployment certificate does not belong to checkpoint model");
  }
  std::vector<std::uint8_t> output;
  output.reserve(kFloat32CheckpointBytes);
  for (const char value : kCheckpointMagic) {
    output.push_back(static_cast<std::uint8_t>(value));
  }
  appendU32(output, kCheckpointVersion);
  appendU32(output, kParameterCount);
  appendU32(output, kCheckpointFloat32Codec);
  appendU32(output, deployment_certified
                        ? kCheckpointDeploymentCertificateFlag
                        : 0u);
  appendU64(output, 0u);  // whole-schema checksum placeholder
  const auto append_configured_float = [&](double value) {
    const float configured = static_cast<float>(value);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &configured, sizeof(bits));
    appendU32(output, bits);
  };
  appendU32(output, kDeploymentMaximumPly);
  appendU32(output, kSearchDepthMoves);
  append_configured_float(kScoreTargetScale);
  append_configured_float(kLifetimeTargetScale);
  append_configured_float(kFlowPerMoveScale);
  appendU32(output, kCheckpointHeadSchema);
  appendU32(output,
            deployment_certified ? kCheckpointCertificateVersion : 0u);
  appendU32(output, deployment_certified ? kRounds : 0u);
  appendU32(output, deployment_certified ? kD4PretrainingEpochs : 0u);
  appendU32(output,
            deployment_certified ? kRounds * kOnPolicyRootsPerRound : 0u);
  appendU32(output,
            deployment_certified ? kRounds * kReanalysisRootsPerRound : 0u);
  appendU32(output, 0u);  // reserved
  appendU64(output, deployment_certified
                        ? certificate->replayFingerprint()
                        : 0u);
  appendU64(output, deployment_certified
                        ? certificate->calibrationFingerprint()
                        : 0u);
  appendU64(output, deployment_certified
                        ? certificate->ledgerFingerprint()
                        : 0u);
  appendU64(output, deployment_certified ? fingerprint : 0u);
  append_configured_float(kGradientNormClip);
  appendU32(output, 0u);  // reserved/alignment
  for (const float weight : model.weights()) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &weight, sizeof(bits));
    appendU32(output, bits);
  }
  const std::uint64_t checksum = checkpointChecksum(output);
  for (int byte = 0; byte < 8; ++byte) {
    output[24 + byte] =
        static_cast<std::uint8_t>((checksum >> (8 * byte)) & 0xffu);
  }
  return output;
}

std::vector<std::uint8_t> serializeCheckpoint(const Model& model) {
  return serializeCheckpointInternal(model, nullptr);
}

std::vector<std::uint8_t> serializeDeploymentCheckpoint(
    const Model& model, const DeploymentCertificate& certificate) {
  return serializeCheckpointInternal(model, &certificate);
}

Model deserializeCheckpoint(const std::vector<std::uint8_t>& source) {
  const auto configured_float = [&](std::size_t offset) {
    const std::uint32_t bits = readU32(source, offset);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  };
  if (source.size() != kFloat32CheckpointBytes ||
      !std::equal(kCheckpointMagic.begin(), kCheckpointMagic.end(),
                  source.begin()) ||
      readU32(source, 8) != kCheckpointVersion ||
      readU32(source, 12) != static_cast<std::uint32_t>(kParameterCount) ||
      readU32(source, 16) != kCheckpointFloat32Codec ||
      (readU32(source, 20) & ~kCheckpointDeploymentCertificateFlag) != 0u ||
      readU32(source, 32) != kDeploymentMaximumPly ||
      readU32(source, 36) != kSearchDepthMoves ||
      configured_float(40) != static_cast<float>(kScoreTargetScale) ||
      configured_float(44) != static_cast<float>(kLifetimeTargetScale) ||
      configured_float(48) != static_cast<float>(kFlowPerMoveScale) ||
      readU32(source, 52) != kCheckpointHeadSchema ||
      configured_float(112) != static_cast<float>(kGradientNormClip) ||
      readU32(source, 76) != 0u || readU32(source, 116) != 0u) {
    throw std::runtime_error("invalid regenerative checkpoint header");
  }
  const bool deployment_certified =
      (readU32(source, 20) & kCheckpointDeploymentCertificateFlag) != 0u;
  if ((!deployment_certified &&
       (readU32(source, 56) != 0u || readU32(source, 60) != 0u ||
        readU32(source, 64) != 0u || readU32(source, 68) != 0u ||
        readU32(source, 72) != 0u || readU64(source, 80) != 0u ||
        readU64(source, 88) != 0u || readU64(source, 96) != 0u ||
        readU64(source, 104) != 0u)) ||
      (deployment_certified &&
       (readU32(source, 56) != kCheckpointCertificateVersion ||
        readU32(source, 60) != kRounds ||
        readU32(source, 64) != kD4PretrainingEpochs ||
        readU32(source, 68) != kRounds * kOnPolicyRootsPerRound ||
        readU32(source, 72) != kRounds * kReanalysisRootsPerRound ||
        readU64(source, 80) == 0u || readU64(source, 88) == 0u ||
        readU64(source, 96) == 0u || readU64(source, 104) == 0u))) {
    throw std::runtime_error("invalid deployment certificate provenance");
  }
  if (readU64(source, 24) != checkpointChecksum(source)) {
    throw std::runtime_error("regenerative checkpoint checksum mismatch");
  }
  Model result;
  for (int index = 0; index < kParameterCount; ++index) {
    const std::uint32_t bits = readU32(
        source, kCheckpointHeaderBytes + 4u * static_cast<std::size_t>(index));
    float weight = 0.0f;
    std::memcpy(&weight, &bits, sizeof(weight));
    if (!std::isfinite(weight)) {
      throw std::runtime_error("non-finite regenerative checkpoint weight");
    }
    result.weights()[index] = weight;
  }
  if (deployment_certified &&
      readU64(source, 104) != modelFingerprint(result)) {
    throw std::runtime_error("deployment certificate model mismatch");
  }
  return result;
}

bool checkpointHasDeploymentCertificate(
    const std::vector<std::uint8_t>& source) {
  (void)deserializeCheckpoint(source);
  return (readU32(source, 20) & kCheckpointDeploymentCertificateFlag) != 0u;
}

void writeBytes(const std::string& path,
                const std::vector<std::uint8_t>& bytes) {
  const std::filesystem::path destination(path);
  const std::filesystem::path temporary =
      path + ".tmp." + std::to_string(static_cast<long long>(::getpid()));
  const int descriptor =
      ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (descriptor < 0) {
    throw std::runtime_error("could not create atomic binary artifact");
  }
  bool open = true;
  try {
    std::size_t cursor = 0;
    while (cursor < bytes.size()) {
      const ssize_t written =
          ::write(descriptor, bytes.data() + cursor, bytes.size() - cursor);
      if (written < 0 && errno == EINTR) continue;
      if (written <= 0) {
        throw std::runtime_error("could not write atomic binary artifact");
      }
      cursor += static_cast<std::size_t>(written);
    }
    if (::fsync(descriptor) != 0) {
      throw std::runtime_error("could not sync atomic binary artifact");
    }
    if (::close(descriptor) != 0) {
      open = false;
      throw std::runtime_error("could not close atomic binary artifact");
    }
    open = false;
    if (::rename(temporary.c_str(), destination.c_str()) != 0) {
      throw std::runtime_error("could not commit atomic binary artifact");
    }
    const std::filesystem::path parent = destination.has_parent_path()
                                             ? destination.parent_path()
                                             : std::filesystem::path(".");
    const int directory = ::open(parent.c_str(), O_RDONLY);
    if (directory >= 0) {
      (void)::fsync(directory);
      (void)::close(directory);
    }
  } catch (...) {
    if (open) (void)::close(descriptor);
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    throw;
  }
}

// ---------------------------------------------------------------------------
// Gumbel sequential-halving stochastic tree search
// ---------------------------------------------------------------------------

double deterministicGumbel(const PublicState& state, int action) {
  const std::uint32_t bits = domainBits(
      kRootPackDomain, state, 0, 0u, static_cast<std::uint32_t>(action));
  const double uniform =
      (static_cast<double>(bits) + 0.5) / 4'294'967'296.0;
  return -std::log(-std::log(uniform));
}

std::array<double, kBoardSize> policyProbabilities(
    const Prediction& prediction) {
  std::array<double, kBoardSize> result{};
  double maximum = -std::numeric_limits<double>::infinity();
  for (int action = 0; action < kBoardSize; ++action) {
    if (prediction.legal[action]) {
      maximum = std::max(maximum,
                         static_cast<double>(prediction.candidate[action]
                                                 .policy_logit));
    }
  }
  double total = 0.0;
  for (int action = 0; action < kBoardSize; ++action) {
    if (!prediction.legal[action]) continue;
    result[action] = std::exp(
        static_cast<double>(prediction.candidate[action].policy_logit) -
        maximum);
    total += result[action];
  }
  if (total == 0.0) return result;
  for (double& probability : result) probability /= total;
  return result;
}

struct ActionStatistics {
  int visits = 0;
  std::vector<double> returns;
};

struct SearchResult {
  int action = -1;
  std::array<int, kBoardSize> visits{};
  std::array<double, kBoardSize> utility{};
  int simulations = 0;
  int maximum_depth = 0;
  std::uint64_t transitions = 0;
  std::uint64_t nnue_leaves = 0;
  std::uint64_t nnue_evaluations = 0;
};

struct ExpertTarget {
  int played_action = -1;
  std::array<float, kBoardSize> policy{};
  double remaining_score = 0.0;
  double remaining_lifetime = 0.0;
  AuxiliaryTargets auxiliary{};
};

ExpertTarget makeExpertTarget(const SearchResult& search, int played_action,
                              double remaining_score,
                              double remaining_lifetime,
                              const AuxiliaryTargets& auxiliary) {
  if (played_action < 0 || played_action >= kBoardSize ||
      search.visits[played_action] <= 0) {
    throw std::invalid_argument("expert target action was not searched");
  }
  const int total =
      std::accumulate(search.visits.begin(), search.visits.end(), 0);
  if (total != search.simulations || total <= 0) {
    throw std::invalid_argument("expert visit target was incomplete");
  }
  ExpertTarget result;
  result.played_action = played_action;
  for (int action = 0; action < kBoardSize; ++action) {
    result.policy[action] =
        static_cast<float>(search.visits[action]) / static_cast<float>(total);
  }
  result.remaining_score = remaining_score;
  result.remaining_lifetime = remaining_lifetime;
  result.auxiliary = auxiliary;
  return result;
}

double policyTargetMass(const Prediction& prediction,
                        const ExpertTarget& target) {
  double mass = 0.0;
  for (int action = 0; action < kBoardSize; ++action) {
    const double value = target.policy[action];
    if (!std::isfinite(value) || value < 0.0 ||
        (!prediction.legal[action] && value != 0.0)) {
      throw std::invalid_argument("invalid policy target mass");
    }
    mass += value;
  }
  if (!(mass > 0.0) || !std::isfinite(mass)) {
    throw std::invalid_argument("empty/non-finite policy target mass");
  }
  return mass;
}

std::array<double, kBoardSize> policyLogitGradients(
    const Prediction& prediction, const ExpertTarget& target) {
  const double mass = policyTargetMass(prediction, target);
  const auto probabilities = policyProbabilities(prediction);
  std::array<double, kBoardSize> result{};
  for (int action = 0; action < kBoardSize; ++action) {
    if (!prediction.legal[action]) continue;
    result[action] =
        kPolicyLossWeight *
        (mass * probabilities[action] - target.policy[action]);
  }
  return result;
}

struct TrainingLoss {
  double policy = 0.0;
  double score_quantile = 0.0;
  double lifetime_quantile = 0.0;
  double regeneration = 0.0;
  double flow = 0.0;
  double l2 = 0.0;
  double total = 0.0;
};

double normalizedScoreTarget(double raw_score) {
  return raw_score / kScoreTargetScale;
}

double normalizedLifetimeTarget(double raw_lifetime) {
  return raw_lifetime / kLifetimeTargetScale;
}

double flowTargetScale(int head) {
  if (head < 0 || head >= kFlowHeads) {
    throw std::invalid_argument("flow head outside normalization domain");
  }
  return static_cast<double>(kFlowHorizonMoves[head / 2]) *
         kFlowPerMoveScale;
}

double normalizedFlowTarget(const AuxiliaryTargets& auxiliary, int head) {
  return static_cast<double>(auxiliary.flow[head]) / flowTargetScale(head);
}

TrainingLoss trainingLoss(const Prediction& prediction,
                          const ExpertTarget& target, const Model& model) {
  if (target.played_action < 0 || target.played_action >= kBoardSize ||
      !prediction.legal[target.played_action]) {
    throw std::invalid_argument("training target action was illegal");
  }
  TrainingLoss result;
  (void)policyTargetMass(prediction, target);
  double maximum = -std::numeric_limits<double>::infinity();
  for (int action = 0; action < kBoardSize; ++action) {
    if (prediction.legal[action]) {
      maximum = std::max(
          maximum,
          static_cast<double>(prediction.candidate[action].policy_logit));
    }
  }
  double denominator = 0.0;
  for (int action = 0; action < kBoardSize; ++action) {
    if (prediction.legal[action]) {
      denominator += std::exp(
          static_cast<double>(prediction.candidate[action].policy_logit) -
          maximum);
    }
  }
  for (int action = 0; action < kBoardSize; ++action) {
    if (target.policy[action] == 0.0f) continue;
    const double log_probability =
        static_cast<double>(prediction.candidate[action].policy_logit) -
        maximum - std::log(denominator);
    result.policy -= target.policy[action] * log_probability;
  }

  const CandidatePrediction& played =
      prediction.candidate[target.played_action];
  result.score_quantile = quantileHuberLoss(
      std::vector<double>(played.score.begin(), played.score.end()),
      normalizedScoreTarget(target.remaining_score));
  result.lifetime_quantile = quantileHuberLoss(
      std::vector<double>(played.lifetime.begin(), played.lifetime.end()),
      normalizedLifetimeTarget(target.remaining_lifetime));
  for (int head = 0; head < kRegenerationHeads; ++head) {
    result.regeneration += binaryCrossEntropyFromLogit(
        played.regeneration[head], target.auxiliary.regenerated_by[head]);
  }
  result.regeneration /= kRegenerationHeads;
  for (int head = 0; head < kFlowHeads; ++head) {
    result.flow += huber(static_cast<double>(played.flow[head]) -
                         normalizedFlowTarget(target.auxiliary, head));
  }
  result.flow /= kFlowHeads;
  for (const float weight : model.weights()) {
    result.l2 += static_cast<double>(weight) * weight;
  }
  result.total = kPolicyLossWeight * result.policy +
                 kScoreQuantileLossWeight * result.score_quantile +
                 kLifetimeQuantileLossWeight * result.lifetime_quantile +
                 kRegenerationLossWeight * result.regeneration +
                 kFlowLossWeight * result.flow + kL2Weight * result.l2;
  return result;
}

std::array<double, kScoreQuantiles> decodedScoreQuantiles(
    const CandidatePrediction& prediction);

double leafValue(const Prediction& prediction) {
  double best = -std::numeric_limits<double>::infinity();
  for (int action = 0; action < kBoardSize; ++action) {
    if (!prediction.legal[action]) continue;
    best = std::max(
        best, predictedScoreUtility(
                  decodedScoreQuantiles(prediction.candidate[action])));
  }
  return std::isfinite(best) ? best : 0.0;
}

using ScoreDistribution = std::array<double, kScoreQuantiles>;

std::vector<int> paretoSurvivors(
    const Prediction& prediction, ConstraintTrust trust = {});

ScoreDistribution degenerateScoreDistribution(double value) {
  ScoreDistribution result{};
  result.fill(value);
  return result;
}

ScoreDistribution leafScoreDistribution(const Prediction& prediction,
                                         ConstraintTrust trust) {
  int best_action = -1;
  double best_utility = -std::numeric_limits<double>::infinity();
  for (const int action : paretoSurvivors(prediction, trust)) {
    const double utility = predictedScoreUtility(
        decodedScoreQuantiles(prediction.candidate[action]));
    if (utility > best_utility) {
      best_utility = utility;
      best_action = action;
    }
  }
  ScoreDistribution result{};
  if (best_action < 0) return result;
  result = decodedScoreQuantiles(prediction.candidate[best_action]);
  return result;
}

double sigmoid(double value) {
  if (value >= 0.0) return 1.0 / (1.0 + std::exp(-value));
  const double exponential = std::exp(value);
  return exponential / (1.0 + exponential);
}

template <std::size_t Size>
std::array<double, Size> isotonicNondecreasing(
    const std::array<double, Size>& input) {
  struct Block {
    int begin = 0;
    int end = 0;
    double sum = 0.0;
    int count = 0;
  };
  std::array<Block, Size> blocks{};
  int block_count = 0;
  for (int index = 0; index < static_cast<int>(Size); ++index) {
    blocks[block_count++] = {index, index + 1, input[index], 1};
    while (block_count >= 2) {
      const Block& left = blocks[block_count - 2];
      const Block& right = blocks[block_count - 1];
      if (left.sum / left.count <= right.sum / right.count) break;
      blocks[block_count - 2] = {
          left.begin, right.end, left.sum + right.sum,
          left.count + right.count};
      --block_count;
    }
  }
  std::array<double, Size> result{};
  for (int block = 0; block < block_count; ++block) {
    const double value = blocks[block].sum / blocks[block].count;
    for (int index = blocks[block].begin; index < blocks[block].end; ++index) {
      result[index] = value;
    }
  }
  return result;
}

std::array<double, kScoreQuantiles> decodedScoreQuantiles(
    const CandidatePrediction& prediction) {
  std::array<double, kScoreQuantiles> result{};
  for (int index = 0; index < kScoreQuantiles; ++index) {
    if (!std::isfinite(prediction.score[index])) {
      throw std::runtime_error("non-finite score head");
    }
    result[index] = std::max(
        0.0, static_cast<double>(prediction.score[index]) * kScoreTargetScale);
  }
  return isotonicNondecreasing(result);
}

std::array<double, kLifetimeQuantiles> decodedLifetimeQuantiles(
    const CandidatePrediction& prediction) {
  std::array<double, kLifetimeQuantiles> result{};
  for (int index = 0; index < kLifetimeQuantiles; ++index) {
    if (!std::isfinite(prediction.lifetime[index])) {
      throw std::runtime_error("non-finite lifetime head");
    }
    result[index] = std::clamp(
        static_cast<double>(prediction.lifetime[index]) *
            kLifetimeTargetScale,
        0.0, static_cast<double>(kMaximumGameMoves));
  }
  return isotonicNondecreasing(result);
}

std::array<double, kRegenerationHeads> decodedRegeneration(
    const CandidatePrediction& prediction) {
  std::array<double, kRegenerationHeads> raw{};
  for (int head = 0; head < kRegenerationHeads; ++head) {
    if (!std::isfinite(prediction.regeneration[head])) {
      throw std::runtime_error("non-finite regeneration head");
    }
    raw[head] = sigmoid(prediction.regeneration[head]);
  }
  std::array<double, kRegenerationHeads> result =
      isotonicNondecreasing(raw);
  for (double& value : result) value = std::clamp(value, 0.0, 1.0);
  return result;
}

std::array<double, kFlowHeads> decodedCumulativeFlow(
    const CandidatePrediction& prediction) {
  std::array<double, 4> cumulative_clears{};
  std::array<double, 4> cumulative_reveals{};
  for (int horizon = 0; horizon < 4; ++horizon) {
    if (!std::isfinite(prediction.flow[2 * horizon]) ||
        !std::isfinite(prediction.flow[2 * horizon + 1])) {
      throw std::runtime_error("non-finite flow head");
    }
    cumulative_clears[horizon] =
        std::max(0.0, static_cast<double>(prediction.flow[2 * horizon]) *
                          flowTargetScale(2 * horizon));
    cumulative_reveals[horizon] =
        std::max(0.0, static_cast<double>(prediction.flow[2 * horizon + 1]) *
                          flowTargetScale(2 * horizon + 1));
  }
  cumulative_clears = isotonicNondecreasing(cumulative_clears);
  cumulative_reveals = isotonicNondecreasing(cumulative_reveals);
  std::array<double, kFlowHeads> result{};
  for (int horizon = 0; horizon < 4; ++horizon) {
    result[2 * horizon] = cumulative_clears[horizon];
    result[2 * horizon + 1] = cumulative_reveals[horizon];
  }
  return result;
}

std::array<double, kFlowHeads> decodedFlowRates(
    const CandidatePrediction& prediction) {
  std::array<double, kFlowHeads> result = decodedCumulativeFlow(prediction);
  for (int horizon = 0; horizon < 4; ++horizon) {
    result[2 * horizon] /= kFlowHorizonMoves[horizon];
    result[2 * horizon + 1] /= kFlowHorizonMoves[horizon];
  }
  return result;
}

template <typename Value, std::size_t Size>
double arrayMean(const std::array<Value, Size>& values, std::size_t begin,
                 std::size_t end) {
  if (begin >= end || end > Size) throw std::invalid_argument("bad array mean");
  double sum = 0.0;
  for (std::size_t index = begin; index < end; ++index) sum += values[index];
  return sum / static_cast<double>(end - begin);
}

struct ActionProfile {
  double score_downside = 0.0;
  double lifetime_downside = 0.0;
  std::array<double, kRegenerationHeads + kFlowHeads> regeneration_flow{};
  double score_utility = 0.0;
};

ActionProfile actionProfile(const CandidatePrediction& prediction) {
  ActionProfile result;
  constexpr std::size_t lower_count = kScoreQuantiles / 4;
  const auto score = decodedScoreQuantiles(prediction);
  const auto lifetime = decodedLifetimeQuantiles(prediction);
  const auto regeneration = decodedRegeneration(prediction);
  const auto flow = decodedFlowRates(prediction);
  result.score_downside = arrayMean(score, 0, lower_count);
  result.lifetime_downside = arrayMean(lifetime, 0, lower_count);
  for (int head = 0; head < kRegenerationHeads; ++head) {
    result.regeneration_flow[head] = regeneration[head];
  }
  for (int head = 0; head < kFlowHeads; ++head) {
    result.regeneration_flow[kRegenerationHeads + head] =
        flow[head];
  }
  result.score_utility = predictedScoreUtility(score);
  return result;
}

bool paretoDominates(double first_a, double second_a, double first_b,
                     double second_b, double first_margin,
                     double second_margin) {
  const bool no_worse = first_a + first_margin >= first_b &&
                        second_a + second_margin >= second_b;
  const bool strictly_better = first_a > first_b + first_margin ||
                               second_a > second_b + second_margin;
  return no_worse && strictly_better;
}

std::vector<int> paretoSurvivors(const Prediction& prediction,
                                 ConstraintTrust trust) {
  std::array<ActionProfile, kBoardSize> profile{};
  std::vector<int> legal;
  for (int action = 0; action < kBoardSize; ++action) {
    if (!prediction.legal[action]) continue;
    profile[action] = actionProfile(prediction.candidate[action]);
    legal.push_back(action);
  }
  std::vector<int> downside;
  if (!trust.lifetime) downside = legal;
  for (const int candidate : legal) {
    if (!trust.lifetime) break;
    bool dominated = false;
    for (const int alternative : legal) {
      if (alternative == candidate) continue;
      const bool dominated_with_lifetime = paretoDominates(
          profile[alternative].lifetime_downside,
          profile[alternative].score_downside,
          profile[candidate].lifetime_downside,
          profile[candidate].score_downside, kLifetimeDominanceMargin,
          kScoreDominanceMargin);
      if (dominated_with_lifetime) {
        dominated = true;
        break;
      }
    }
    if (!dominated) downside.push_back(candidate);
  }
  if (!trust.regeneration && !trust.flow) return downside;
  std::vector<int> flow;
  for (const int candidate : downside) {
    bool dominated = false;
    for (const int alternative : downside) {
      if (alternative == candidate) continue;
      bool no_worse = true;
      bool strictly_better = false;
      for (std::size_t metric = 0;
           metric < profile[candidate].regeneration_flow.size(); ++metric) {
        const bool enabled =
            (metric < kRegenerationHeads && trust.regeneration) ||
            (metric >= kRegenerationHeads && trust.flow);
        if (!enabled) continue;
        const double left = profile[alternative].regeneration_flow[metric];
        const double right = profile[candidate].regeneration_flow[metric];
        const double margin = metric < kRegenerationHeads
                                  ? kRegenerationDominanceMargin
                                  : kFlowDominanceMargin;
        no_worse = no_worse && left + margin >= right;
        strictly_better = strictly_better || left > right + margin;
      }
      if (no_worse && strictly_better) {
        dominated = true;
        break;
      }
    }
    if (!dominated) flow.push_back(candidate);
  }
  return flow.empty() ? downside : flow;
}

int constrainedPredictedAction(const PublicState& state,
                               const Prediction& prediction, int depth,
                               std::uint32_t simulation,
                               ConstraintTrust trust) {
  const std::vector<int> survivors = paretoSurvivors(prediction, trust);
  int best_action = -1;
  double best_utility = -std::numeric_limits<double>::infinity();
  double best_policy = -std::numeric_limits<double>::infinity();
  std::uint32_t best_tie = 0;
  for (const int action : survivors) {
    const ActionProfile profile = actionProfile(prediction.candidate[action]);
    const double policy = prediction.candidate[action].policy_logit;
    const std::uint32_t tie = domainBits(
        kPolicySampleDomain, state, depth, simulation,
        static_cast<std::uint32_t>(action));
    if (profile.score_utility > best_utility + kDominanceTolerance ||
        (std::abs(profile.score_utility - best_utility) <=
             kDominanceTolerance &&
         (policy > best_policy + kDominanceTolerance ||
          (std::abs(policy - best_policy) <= kDominanceTolerance &&
           tie > best_tie)))) {
      best_action = action;
      best_utility = profile.score_utility;
      best_policy = policy;
      best_tie = tie;
    }
  }
  return best_action;
}

int rolloutPolicyAction(const PublicState& state,
                        const Prediction& prediction, int depth,
                        std::uint32_t simulation, ConstraintTrust trust) {
  return constrainedPredictedAction(state, prediction, depth, simulation,
                                    trust);
}

int chanceStratum(std::uint32_t local_visit, int depth) {
  if (depth < 0) throw std::invalid_argument("negative chance depth");
  return static_cast<int>((local_visit + static_cast<std::uint32_t>(depth)) %
                          kChanceStrata);
}

ScoreDistribution simulateTrajectory(const PublicState& root, int root_action,
                                     std::uint32_t local_visit,
                                     const Model& model,
                                     SearchResult& diagnostics,
                                     ConstraintTrust trust,
                                     int search_depth) {
  PublicState state = root;
  int action = root_action;
  double earned = 0.0;
  for (int depth = 0; depth < search_depth; ++depth) {
    const int stratum = chanceStratum(local_visit, depth);
    const PublicTransition transition =
        chanceTransition(state, action, depth, local_visit, stratum);
    ++diagnostics.transitions;
    diagnostics.maximum_depth =
        std::max(diagnostics.maximum_depth, depth + 1);
    earned += static_cast<double>(transition.score_delta);
    state = transition.state;
    if (state.terminal) {
      return degenerateScoreDistribution(
          earned);  // Exact terminal continuation is zero.
    }

    const Prediction next = model.predict(state);
    ++diagnostics.nnue_evaluations;
    if (depth + 1 == search_depth) {
      ++diagnostics.nnue_leaves;
      ScoreDistribution leaf = leafScoreDistribution(next, trust);
      for (double& quantile : leaf) quantile += earned;
      return leaf;
    }
    action = rolloutPolicyAction(state, next, depth + 1, local_visit, trust);
    if (action < 0) return degenerateScoreDistribution(earned);
  }
  throw std::runtime_error("trajectory did not terminate at requested leaf");
}

SearchResult searchRoot(const PublicState& source, const Model& model,
                        int simulations = kSearchSimulations,
                        ConstraintTrust trust = {},
                        int search_depth = kSearchDepthMoves) {
  if (simulations < kBoardSize) {
    throw std::invalid_argument("search budget cannot cover every action");
  }
  if (search_depth < 1 || search_depth > kSearchDepthMoves) {
    throw std::invalid_argument("search depth outside frozen 1..20 domain");
  }
  if (source.terminal) return {};
  bool was_mirrored = false;
  const PublicState root = canonical(source, was_mirrored);
  const Prediction prediction = model.predict(root);
  const auto priors = policyProbabilities(prediction);
  std::vector<int> active;
  for (int action = 0; action < kBoardSize; ++action) {
    if (prediction.legal[action]) active.push_back(action);
  }
  if (active.empty()) return {};
  const int mandatory_warmup =
      static_cast<int>(active.size()) * kChanceStrata;
  if (simulations < mandatory_warmup) {
    throw std::invalid_argument(
        "search budget cannot give every legal action seven chance strata");
  }

  SearchResult result;
  result.nnue_evaluations = 1;  // Root prior/value evaluation.
  std::array<ActionStatistics, kBoardSize> statistics{};
  const auto evaluate_once = [&](int action) {
    const std::uint32_t local_visit =
        static_cast<std::uint32_t>(statistics[action].visits);
    const ScoreDistribution values =
        simulateTrajectory(root, action, local_visit, model, result, trust,
                           search_depth);
    ActionStatistics& stats = statistics[action];
    ++stats.visits;
    stats.returns.insert(stats.returns.end(), values.begin(), values.end());
    ++result.simulations;
  };

  // Coverage is unconditional: no learned head, prior, or Gumbel value may
  // delete a legal action before local visits 0..6 complete the paired root
  // chance pack.
  for (int visit = 0; visit < kChanceStrata; ++visit) {
    for (const int action : active) evaluate_once(action);
  }

  std::array<bool, kBoardSize> eligible{};
  for (const int action : paretoSurvivors(prediction, trust)) {
    eligible[action] = true;
  }
  active.erase(std::remove_if(active.begin(), active.end(), [&](int action) {
                 return !eligible[action];
               }),
               active.end());
  if (active.empty()) {
    throw std::runtime_error("Pareto selector removed every legal root action");
  }

  int remaining = simulations - result.simulations;
  while (active.size() > 1 && remaining > 0) {
    const int rounds_left =
        static_cast<int>(std::ceil(std::log2(active.size())));
    const int per_action = std::min(
        remaining / static_cast<int>(active.size()),
        std::max(1, remaining /
                        std::max(1, rounds_left *
                                        static_cast<int>(active.size()))));
    for (const int action : active) {
      for (int count = 0; count < per_action; ++count) {
        evaluate_once(action);
      }
    }
    remaining -= per_action * static_cast<int>(active.size());
    const int boundary_visits = statistics[active.front()].visits;
    if (!std::all_of(active.begin(), active.end(), [&](int action) {
          return statistics[action].visits == boundary_visits;
        })) {
      throw std::runtime_error(
          "sequential-halving boundary used unpaired local visits");
    }
    std::stable_sort(active.begin(), active.end(), [&](int left, int right) {
      const double left_score = strategyUtility(statistics[left].returns);
      const double right_score = strategyUtility(statistics[right].returns);
      if (std::abs(left_score - right_score) > kDominanceTolerance) {
        return left_score > right_score;
      }
      if (std::abs(priors[left] - priors[right]) > kDominanceTolerance) {
        return priors[left] > priors[right];
      }
      const double left_gumbel = deterministicGumbel(root, left);
      const double right_gumbel = deterministicGumbel(root, right);
      if (left_gumbel != right_gumbel) return left_gumbel > right_gumbel;
      return left < right;
    });
    active.resize((active.size() + 1) / 2);
  }
  while (active.size() > 1) {
    std::stable_sort(active.begin(), active.end(), [&](int left, int right) {
      const double left_score = strategyUtility(statistics[left].returns);
      const double right_score = strategyUtility(statistics[right].returns);
      if (std::abs(left_score - right_score) > kDominanceTolerance) {
        return left_score > right_score;
      }
      if (std::abs(priors[left] - priors[right]) > kDominanceTolerance) {
        return priors[left] > priors[right];
      }
      return left < right;
    });
    active.resize((active.size() + 1) / 2);
  }
  while (remaining-- > 0) evaluate_once(active.front());

  const int chosen = active.front();
  for (int action = 0; action < kBoardSize; ++action) {
    result.visits[action] = statistics[action].visits;
    if (statistics[action].visits == 0) {
      result.utility[action] = -std::numeric_limits<double>::infinity();
      continue;
    }
    result.utility[action] = strategyUtility(statistics[action].returns);
  }
  result.action = was_mirrored && chosen >= 0 ? kBoardSize - 1 - chosen : chosen;
  if (was_mirrored) {
    std::reverse(result.visits.begin(), result.visits.end());
    std::reverse(result.utility.begin(), result.utility.end());
  }
  return result;
}

SearchResult deploymentSearchRoot(
    const PublicState& source, const Model& model, int requested_ply,
    ConstraintTrust trust = ConstraintTrust{true, true, true},
    int simulations = kSearchSimulations) {
  // UI depth maps one-for-one to disc placements (including each stochastic
  // transition) and is intentionally capped at the existing 1..8 contract.
  if (requested_ply < 1 || requested_ply > kDeploymentMaximumPly) {
    throw std::invalid_argument("deployment ply must be in [1,8]");
  }
  return searchRoot(source, model, simulations, trust, requested_ply);
}

// ---------------------------------------------------------------------------
// Replay persistence and deterministic full-loss optimizer
// ---------------------------------------------------------------------------

struct TrainingExample {
  PublicState state{};
  ExpertTarget target{};
  // Calibration-only trajectory identity.  It is serialized for exact
  // whole-game splitting and is never passed to Model::predict.
  std::uint32_t trajectory_group = 0;
  // 0=trainable, 1=heldout half A, 2=heldout half B.  A heldout trajectory
  // stays reserved in every later replay/reanalysis round.
  std::uint8_t calibration_reservation = 0;
};

class ReplayBuffer {
 public:
  std::size_t size() const { return examples_.size(); }
  bool empty() const { return examples_.empty(); }
  const TrainingExample& operator[](std::size_t index) const {
    return examples_.at(index);
  }
  TrainingExample& operator[](std::size_t index) {
    return examples_.at(index);
  }
  void push(TrainingExample example) {
    examples_.push_back(std::move(example));
  }
  void append(std::vector<TrainingExample> examples) {
    examples_.insert(examples_.end(),
                     std::make_move_iterator(examples.begin()),
                     std::make_move_iterator(examples.end()));
  }
  const std::vector<TrainingExample>& examples() const { return examples_; }

 private:
  std::vector<TrainingExample> examples_;
};

constexpr std::array<char, 8> kReplayMagic{{'D', '7', 'R', 'E', 'P', 'L',
                                            '3', '\0'}};
constexpr std::uint32_t kReplayVersion = 3;
constexpr std::size_t kReplayHeaderBytes = 32;
constexpr std::size_t kSerializedExampleBytes = 142;

void validateReplayExample(const TrainingExample& example) {
  if (std::any_of(example.state.board.begin(), example.state.board.end(),
                  [](std::uint8_t value) { return value >= kCellKinds; }) ||
      example.state.next_disc < 1 ||
      example.state.next_disc > kBoardSize || example.state.phase < 1 ||
      example.state.phase > kMovesPerLevel || example.state.terminal ||
      example.target.played_action < 0 ||
      example.target.played_action >= kBoardSize ||
      !isLegal(example.state.board, example.target.played_action) ||
      example.calibration_reservation > 2) {
    throw std::runtime_error("replay record crossed public/legal boundary");
  }
  double policy_mass = 0.0;
  for (int action = 0; action < kBoardSize; ++action) {
    const double probability = example.target.policy[action];
    if (!std::isfinite(probability) || probability < 0.0 ||
        (!isLegal(example.state.board, action) && probability != 0.0)) {
      throw std::runtime_error("bad replay policy target");
    }
    policy_mass += probability;
  }
  constexpr std::array<int, kRegenerationHeads> kCycles{{1, 2, 4, 8}};
  const int cycle = example.target.auxiliary.regeneration_cycle;
  bool regeneration_consistent = cycle >= 0 && cycle <= kCycles.back();
  for (int head = 0; head < kRegenerationHeads; ++head) {
    regeneration_consistent &=
        example.target.auxiliary.regenerated_by[head] ==
        (cycle > 0 && cycle <= kCycles[head]);
  }
  if (!(policy_mass > 0.0) || !std::isfinite(policy_mass) ||
      !std::isfinite(example.target.remaining_score) ||
      example.target.remaining_score < 0.0 ||
      !std::isfinite(example.target.remaining_lifetime) ||
      example.target.remaining_lifetime < 0.0 || !regeneration_consistent ||
      std::any_of(example.target.auxiliary.flow.begin(),
                  example.target.auxiliary.flow.end(), [](float value) {
                    return !std::isfinite(value) || value < 0.0f;
                  })) {
    throw std::runtime_error("bad replay outcome target");
  }
}

void appendFloat(std::vector<std::uint8_t>& output, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  appendU32(output, bits);
}

float readFloat(const std::vector<std::uint8_t>& source, std::size_t offset) {
  const std::uint32_t bits = readU32(source, offset);
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

std::vector<std::uint8_t> serializeReplay(const ReplayBuffer& replay) {
  if (replay.size() > std::numeric_limits<std::uint32_t>::max()) {
    throw std::runtime_error("replay record count exceeded uint32");
  }
  std::vector<std::uint8_t> output;
  output.reserve(kReplayHeaderBytes + replay.size() * kSerializedExampleBytes);
  for (const char value : kReplayMagic) {
    output.push_back(static_cast<std::uint8_t>(value));
  }
  appendU32(output, kReplayVersion);
  appendU32(output, static_cast<std::uint32_t>(replay.size()));
  appendU64(output, 0u);  // payload checksum placeholder
  appendU64(output, 0u);  // frozen flags/reserved
  for (const TrainingExample& example : replay.examples()) {
    validateReplayExample(example);
    for (const std::uint8_t cell : example.state.board) output.push_back(cell);
    output.push_back(example.state.next_disc);
    output.push_back(example.state.phase);
    output.push_back(example.state.terminal ? 1u : 0u);
    output.push_back(static_cast<std::uint8_t>(example.target.played_action));
    for (const float probability : example.target.policy) {
      appendFloat(output, probability);
    }
    appendDouble(output, example.target.remaining_score);
    appendDouble(output, example.target.remaining_lifetime);
    appendU32(output,
              static_cast<std::uint32_t>(example.target.auxiliary
                                             .regeneration_cycle));
    for (const bool regenerated :
         example.target.auxiliary.regenerated_by) {
      output.push_back(regenerated ? 1u : 0u);
    }
    for (const float flow : example.target.auxiliary.flow) {
      appendFloat(output, flow);
    }
    appendU32(output, example.trajectory_group);
    output.push_back(example.calibration_reservation);
  }
  if (output.size() !=
      kReplayHeaderBytes + replay.size() * kSerializedExampleBytes) {
    throw std::runtime_error("replay serialization layout drifted");
  }
  const std::vector<std::uint8_t> payload(output.begin() + kReplayHeaderBytes,
                                          output.end());
  const std::uint64_t checksum = fnv1a64(payload);
  for (int byte = 0; byte < 8; ++byte) {
    output[16 + byte] =
        static_cast<std::uint8_t>((checksum >> (8 * byte)) & 0xffu);
  }
  return output;
}

ReplayBuffer deserializeReplay(const std::vector<std::uint8_t>& source) {
  if (source.size() < kReplayHeaderBytes ||
      !std::equal(kReplayMagic.begin(), kReplayMagic.end(), source.begin()) ||
      readU32(source, 8) != kReplayVersion || readU64(source, 24) != 0u) {
    throw std::runtime_error("invalid regenerative replay header");
  }
  const std::uint32_t count = readU32(source, 12);
  if (source.size() !=
      kReplayHeaderBytes +
          static_cast<std::size_t>(count) * kSerializedExampleBytes) {
    throw std::runtime_error("regenerative replay length mismatch");
  }
  const std::vector<std::uint8_t> payload(source.begin() + kReplayHeaderBytes,
                                          source.end());
  if (readU64(source, 16) != fnv1a64(payload)) {
    throw std::runtime_error("regenerative replay checksum mismatch");
  }
  ReplayBuffer result;
  std::size_t cursor = kReplayHeaderBytes;
  for (std::uint32_t record = 0; record < count; ++record) {
    TrainingExample example;
    for (std::uint8_t& cell : example.state.board) {
      cell = source[cursor++];
      if (cell >= kCellKinds) throw std::runtime_error("bad replay token");
    }
    example.state.next_disc = source[cursor++];
    example.state.phase = source[cursor++];
    const std::uint8_t terminal = source[cursor++];
    if (terminal > 1u) throw std::runtime_error("bad replay terminal flag");
    example.state.terminal = terminal != 0;
    example.target.played_action = source[cursor++];
    for (float& probability : example.target.policy) {
      probability = readFloat(source, cursor);
      cursor += 4;
    }
    example.target.remaining_score = readDouble(source, cursor);
    cursor += 8;
    example.target.remaining_lifetime = readDouble(source, cursor);
    cursor += 8;
    example.target.auxiliary.regeneration_cycle =
        static_cast<int>(readU32(source, cursor));
    cursor += 4;
    for (std::size_t head = 0;
         head < example.target.auxiliary.regenerated_by.size(); ++head) {
      const std::uint8_t regenerated = source[cursor++];
      if (regenerated > 1u) {
        throw std::runtime_error("bad replay regeneration flag");
      }
      example.target.auxiliary.regenerated_by[head] = regenerated != 0;
    }
    for (float& flow : example.target.auxiliary.flow) {
      flow = readFloat(source, cursor);
      cursor += 4;
    }
    example.trajectory_group = readU32(source, cursor);
    cursor += 4;
    example.calibration_reservation = source[cursor++];
    if (example.state.next_disc < 1 ||
        example.state.next_disc > kBoardSize || example.state.phase < 1 ||
        example.state.phase > kMovesPerLevel || example.state.terminal ||
        example.target.played_action < 0 ||
        example.target.played_action >= kBoardSize ||
        !isLegal(example.state.board, example.target.played_action)) {
      throw std::runtime_error("replay record crossed public/legal boundary");
    }
    double policy_mass = 0.0;
    for (int action = 0; action < kBoardSize; ++action) {
      const double probability = example.target.policy[action];
      if (!std::isfinite(probability) || probability < 0.0 ||
          (!isLegal(example.state.board, action) && probability != 0.0)) {
        throw std::runtime_error("bad replay policy target");
      }
      policy_mass += probability;
    }
    if (!(policy_mass > 0.0) || !std::isfinite(policy_mass) ||
        !std::isfinite(example.target.remaining_score) ||
        example.target.remaining_score < 0.0 ||
        !std::isfinite(example.target.remaining_lifetime) ||
        example.target.remaining_lifetime < 0.0 ||
        example.target.auxiliary.regeneration_cycle < 0 ||
        example.target.auxiliary.regeneration_cycle > 8 ||
        std::any_of(example.target.auxiliary.flow.begin(),
                    example.target.auxiliary.flow.end(), [](float value) {
                      return !std::isfinite(value) || value < 0.0f;
                    })) {
      throw std::runtime_error("bad replay outcome target");
    }
    if (example.calibration_reservation > 2) {
      throw std::runtime_error("bad replay calibration reservation");
    }
    validateReplayExample(example);
    result.push(std::move(example));
  }
  return result;
}

std::vector<std::uint8_t> readBytes(const std::string& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) throw std::runtime_error("could not open binary artifact");
  const std::streampos end = input.tellg();
  if (end < 0) throw std::runtime_error("invalid binary artifact size");
  std::vector<std::uint8_t> result(static_cast<std::size_t>(end));
  input.seekg(0);
  input.read(reinterpret_cast<char*>(result.data()),
             static_cast<std::streamsize>(result.size()));
  if (!input) throw std::runtime_error("could not read binary artifact");
  return result;
}

int calibrationPartition(std::uint32_t trajectory_group, int round) {
  if (round < 0 || round >= kRounds) {
    throw std::invalid_argument("calibration round outside frozen schedule");
  }
  return static_cast<int>(
      mix32(kCalibrationDomain ^ trajectory_group ^
            (static_cast<std::uint32_t>(round + 1) * 0x9e37'79b9u)) %
      10u);
}

struct CalibrationSplit {
  ReplayBuffer training;
  std::array<std::vector<TrainingExample>, 2> heldout;
};

CalibrationSplit splitFreshForCalibration(
    std::vector<TrainingExample>& fresh, int round) {
  CalibrationSplit result;
  for (TrainingExample& example : fresh) {
    const int partition = calibrationPartition(example.trajectory_group, round);
    if (partition == 0 || partition == 1) {
      example.calibration_reservation =
          static_cast<std::uint8_t>(partition + 1);
      result.heldout[partition].push_back(example);
    } else {
      example.calibration_reservation = 0;
      result.training.push(example);
    }
  }
  return result;
}

struct CalibrationHalfMetrics {
  int examples = 0;
  int trajectory_groups = 0;
  double lifetime_coverage = 0.0;
  double lifetime_lower_coverage = 0.0;
  std::array<int, kBoardSize> played_per_column{};
  std::array<double, kRegenerationHeads> regeneration_ece{};
  std::array<double, kRegenerationHeads> regeneration_brier{};
  std::array<double, kFlowHeads> flow_normalized_mae{};
  bool finite = true;
  bool lifetime_pass = false;
  bool regeneration_pass = false;
  bool flow_pass = false;
};

struct CalibrationResult {
  std::array<CalibrationHalfMetrics, 2> half{};

  ConstraintTrust trust() const {
    return {half[0].lifetime_pass && half[1].lifetime_pass,
            half[0].regeneration_pass && half[1].regeneration_pass,
            half[0].flow_pass && half[1].flow_pass};
  }
};

bool lifetimeCalibrationMetricsPass(bool enough, bool finite,
                                    double central_coverage,
                                    double lower_coverage) {
  return enough && finite &&
         std::abs(central_coverage - kLifetimeCoverageTarget) <=
             std::nextafter(kLifetimeCoverageTolerance,
                            std::numeric_limits<double>::infinity()) &&
         std::abs(lower_coverage - kLifetimeLowerCoverageTarget) <=
             std::nextafter(kLifetimeLowerCoverageTolerance,
                            std::numeric_limits<double>::infinity());
}

bool regenerationCalibrationMetricsPass(
    bool enough, bool finite,
    const std::array<double, kRegenerationHeads>& ece,
    const std::array<double, kRegenerationHeads>& brier) {
  return enough && finite &&
         std::all_of(ece.begin(), ece.end(), [](double value) {
           return value <= kRegenerationMaximumEce;
         }) &&
         std::all_of(brier.begin(), brier.end(), [](double value) {
           return value <= kRegenerationMaximumBrier;
         });
}

bool flowCalibrationMetricsPass(
    bool enough, bool finite,
    const std::array<double, kFlowHeads>& normalized_mae) {
  return enough && finite &&
         std::all_of(normalized_mae.begin(), normalized_mae.end(),
                     [](double value) {
                       return value <= kFlowMaximumNormalizedMae;
                     });
}

bool calibrationSupportPass(
    int examples, int groups,
    const std::array<int, kBoardSize>& played_per_column,
    int minimum_examples = kCalibrationMinimumExamplesPerHalf) {
  return examples >= minimum_examples && groups >= 2 &&
         std::all_of(played_per_column.begin(), played_per_column.end(),
                     [](int count) {
                       return count >= kCalibrationMinimumPlayedPerColumn;
                     });
}

CalibrationHalfMetrics calibrateHalf(
    const Model& model, const std::vector<TrainingExample>& examples,
    int minimum_examples = kCalibrationMinimumExamplesPerHalf) {
  CalibrationHalfMetrics result;
  result.examples = static_cast<int>(examples.size());
  std::vector<std::uint32_t> groups;
  groups.reserve(examples.size());
  std::array<std::array<int, 10>, kRegenerationHeads> bin_count{};
  std::array<std::array<double, 10>, kRegenerationHeads> bin_probability{};
  std::array<std::array<double, 10>, kRegenerationHeads> bin_outcome{};
  std::array<double, kRegenerationHeads> brier_sum{};
  std::array<double, kFlowHeads> flow_error_sum{};
  int lifetime_inside = 0;
  int lifetime_below_lower = 0;
  for (const TrainingExample& example : examples) {
    groups.push_back(example.trajectory_group);
    try {
      const Prediction prediction = model.predict(example.state);
      if (example.target.played_action < 0 ||
          example.target.played_action >= kBoardSize ||
          !prediction.legal[example.target.played_action]) {
        result.finite = false;
        continue;
      }
      const CandidatePrediction& selected =
          prediction.candidate[example.target.played_action];
      ++result.played_per_column[example.target.played_action];
      const auto lifetime = decodedLifetimeQuantiles(selected);
      const double lower = 0.5 * (lifetime[7] + lifetime[8]);
      const double upper = 0.5 * (lifetime[23] + lifetime[24]);
      lifetime_inside += example.target.remaining_lifetime >= lower &&
                         example.target.remaining_lifetime <= upper;
      lifetime_below_lower += example.target.remaining_lifetime <= lower;

      const auto regeneration = decodedRegeneration(selected);
      for (int head = 0; head < kRegenerationHeads; ++head) {
        const double probability = regeneration[head];
        const double outcome =
            example.target.auxiliary.regenerated_by[head] ? 1.0 : 0.0;
        const int bin = std::min(9, static_cast<int>(probability * 10.0));
        ++bin_count[head][bin];
        bin_probability[head][bin] += probability;
        bin_outcome[head][bin] += outcome;
        const double residual = probability - outcome;
        brier_sum[head] += residual * residual;
      }

      const auto flow = decodedCumulativeFlow(selected);
      for (int head = 0; head < kFlowHeads; ++head) {
        const double target = example.target.auxiliary.flow[head];
        flow_error_sum[head] +=
            std::abs(flow[head] - target) / (1.0 + std::abs(target));
      }
    } catch (const std::exception&) {
      result.finite = false;
    }
  }
  std::sort(groups.begin(), groups.end());
  groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
  result.trajectory_groups = static_cast<int>(groups.size());
  const double denominator = std::max(1, result.examples);
  result.lifetime_coverage = lifetime_inside / denominator;
  result.lifetime_lower_coverage = lifetime_below_lower / denominator;
  for (int head = 0; head < kRegenerationHeads; ++head) {
    for (int bin = 0; bin < 10; ++bin) {
      if (bin_count[head][bin] == 0) continue;
      const double inverse = 1.0 / bin_count[head][bin];
      result.regeneration_ece[head] +=
          static_cast<double>(bin_count[head][bin]) / denominator *
          std::abs(bin_probability[head][bin] * inverse -
                   bin_outcome[head][bin] * inverse);
    }
    result.regeneration_brier[head] = brier_sum[head] / denominator;
  }
  for (int head = 0; head < kFlowHeads; ++head) {
    result.flow_normalized_mae[head] = flow_error_sum[head] / denominator;
  }
  const bool enough = calibrationSupportPass(
      result.examples, result.trajectory_groups, result.played_per_column,
      minimum_examples);
  result.lifetime_pass = lifetimeCalibrationMetricsPass(
      enough, result.finite, result.lifetime_coverage,
      result.lifetime_lower_coverage);
  result.regeneration_pass = regenerationCalibrationMetricsPass(
      enough, result.finite, result.regeneration_ece,
      result.regeneration_brier);
  result.flow_pass = flowCalibrationMetricsPass(
      enough, result.finite, result.flow_normalized_mae);
  return result;
}

CalibrationResult calibrateHeldout(
    const Model& model,
    const std::array<std::vector<TrainingExample>, 2>& heldout,
    int minimum_examples = kCalibrationMinimumExamplesPerHalf) {
  return {{calibrateHalf(model, heldout[0], minimum_examples),
           calibrateHalf(model, heldout[1], minimum_examples)}};
}

ConstraintTrust trustForRound(int round,
                              const CalibrationResult& previous_round) {
  if (round < 0 || round >= kRounds) {
    throw std::invalid_argument("trust round outside frozen schedule");
  }
  return round < 2 ? ConstraintTrust{} : previous_round.trust();
}

double averageTrainingLoss(const Model& model, const ReplayBuffer& replay,
                           const std::vector<std::size_t>& indices) {
  if (indices.empty()) throw std::invalid_argument("empty optimizer batch");
  double result = 0.0;
  for (const std::size_t index : indices) {
    const TrainingExample& example = replay[index];
    result += trainingLoss(model.predict(example.state), example.target, model)
                  .total;
  }
  return result / static_cast<double>(indices.size());
}

struct OrientationCache {
  std::array<float, kStateUnits> state_pre{};
  std::array<float, kStateUnits> state{};
  std::array<float, kRelativeUnits> relative_pre{};
  std::array<float, kRelativeUnits> relative{};
  std::array<float, kTrunkUnits> trunk_pre{};
  std::array<float, kTrunkUnits> trunk{};
  PublicState input{};
  int action = -1;
};

OrientationCache orientationCache(const Model& model, const PublicState& input,
                                  int action) {
  OrientationCache cache;
  cache.input = input;
  cache.action = action;
  const auto& weights = model.weights();
  for (int unit = 0; unit < kStateUnits; ++unit) {
    float sum = weights[ModelLayout::kStateBias + unit];
    for (int position = 0; position < kCellCount; ++position) {
      const int token = input.board[position];
      const int offset = ModelLayout::kStateBoard +
                         (position * kCellKinds + token) * kStateUnits;
      sum += weights[offset + unit];
    }
    const int next_offset = ModelLayout::kStateNext +
                            (input.next_disc - 1) * kStateUnits;
    const int phase_offset = ModelLayout::kStatePhase +
                             (input.phase - 1) * kStateUnits;
    sum += weights[next_offset + unit];
    sum += weights[phase_offset + unit];
    cache.state_pre[unit] = sum;
    cache.state[unit] = clippedRelu(cache.state_pre[unit]);
  }
  for (int unit = 0; unit < kRelativeUnits; ++unit) {
    float sum = weights[ModelLayout::kRelativeBias + unit];
    for (int row = 0; row < kBoardSize; ++row) {
      for (int column = 0; column < kBoardSize; ++column) {
        const int distance = column - action + (kBoardSize - 1);
        const int token = input.board[indexOf(row, column)];
        const int category =
            ((row * kRelativeDistances + distance) * kCellKinds + token);
        sum += weights[ModelLayout::kRelative +
                       category * kRelativeUnits + unit];
      }
    }
    cache.relative_pre[unit] = sum;
    cache.relative[unit] = clippedRelu(cache.relative_pre[unit]);
  }
  constexpr int inputs = kStateUnits + kRelativeUnits;
  for (int output = 0; output < kTrunkUnits; ++output) {
    double sum = weights[ModelLayout::kTrunkBias + output];
    const int base = ModelLayout::kFusion + output * inputs;
    for (int unit = 0; unit < kStateUnits; ++unit) {
      sum += static_cast<double>(weights[base + unit]) * cache.state[unit];
    }
    for (int unit = 0; unit < kRelativeUnits; ++unit) {
      sum += static_cast<double>(weights[base + kStateUnits + unit]) *
             cache.relative[unit];
    }
    cache.trunk_pre[output] = static_cast<float>(sum);
    cache.trunk[output] = clippedRelu(cache.trunk_pre[output]);
  }
  return cache;
}

std::uint32_t floatBits(float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

template <std::size_t Outputs>
std::array<float, Outputs> cachedHead(const Model& model,
                                      const OrientationCache& cache,
                                      int offset) {
  std::array<float, Outputs> result{};
  const int bias = offset + static_cast<int>(Outputs) * kTrunkUnits;
  for (std::size_t output = 0; output < Outputs; ++output) {
    double sum = model.weights()[bias + static_cast<int>(output)];
    const int row = offset + static_cast<int>(output) * kTrunkUnits;
    for (int unit = 0; unit < kTrunkUnits; ++unit) {
      sum += static_cast<double>(model.weights()[row + unit]) *
             cache.trunk[unit];
    }
    result[output] = static_cast<float>(sum);
  }
  return result;
}

CandidatePrediction cachedOrientationPrediction(
    const Model& model, const OrientationCache& cache) {
  CandidatePrediction result;
  result.policy_logit = cachedHead<1>(model, cache, ModelLayout::kPolicy)[0];
  result.score =
      cachedHead<kScoreQuantiles>(model, cache, ModelLayout::kScore);
  result.lifetime = cachedHead<kLifetimeQuantiles>(
      model, cache, ModelLayout::kLifetime);
  result.regeneration = cachedHead<kRegenerationHeads>(
      model, cache, ModelLayout::kRegeneration);
  result.flow = cachedHead<kFlowHeads>(model, cache, ModelLayout::kFlow);
  return result;
}

CandidatePrediction averageCachedPredictions(
    const CandidatePrediction& first, const CandidatePrediction& second) {
  CandidatePrediction result;
  result.policy_logit = static_cast<float>(
      (static_cast<double>(first.policy_logit) + second.policy_logit) * 0.5);
  const auto blend = [](const auto& left, const auto& right, auto& output) {
    for (std::size_t index = 0; index < output.size(); ++index) {
      output[index] = static_cast<float>(
          (static_cast<double>(left[index]) + right[index]) * 0.5);
    }
  };
  blend(first.score, second.score, result.score);
  blend(first.lifetime, second.lifetime, result.lifetime);
  blend(first.regeneration, second.regeneration, result.regeneration);
  blend(first.flow, second.flow, result.flow);
  return result;
}

CandidatePrediction cachedPrediction(const Model& model,
                                     const PublicState& state, int action) {
  const PublicState reflected = mirror(state);
  return averageCachedPredictions(
      cachedOrientationPrediction(model,
                                  orientationCache(model, state, action)),
      cachedOrientationPrediction(
          model, orientationCache(model, reflected,
                                  kBoardSize - 1 - action)));
}

bool bitwiseEqual(const CandidatePrediction& first,
                  const CandidatePrediction& second) {
  if (floatBits(first.policy_logit) != floatBits(second.policy_logit)) {
    return false;
  }
  const auto equal = [](const auto& left, const auto& right) {
    for (std::size_t index = 0; index < left.size(); ++index) {
      if (floatBits(left[index]) != floatBits(right[index])) return false;
    }
    return true;
  };
  return equal(first.score, second.score) &&
         equal(first.lifetime, second.lifetime) &&
         equal(first.regeneration, second.regeneration) &&
         equal(first.flow, second.flow);
}

struct CandidateOutputGradient {
  double policy = 0.0;
  std::array<double, kScoreQuantiles> score{};
  std::array<double, kLifetimeQuantiles> lifetime{};
  std::array<double, kRegenerationHeads> regeneration{};
  std::array<double, kFlowHeads> flow{};
};

void backwardHead(const Model& model, const OrientationCache& cache,
                  int offset, const double* output_gradient, int outputs,
                  std::array<double, kTrunkUnits>& trunk_gradient,
                  std::vector<double>& gradient) {
  const auto& weights = model.weights();
  const int bias = offset + outputs * kTrunkUnits;
  for (int output = 0; output < outputs; ++output) {
    const double derivative = output_gradient[output];
    gradient[bias + output] += derivative;
    const int row = offset + output * kTrunkUnits;
    for (int unit = 0; unit < kTrunkUnits; ++unit) {
      gradient[row + unit] += derivative * cache.trunk[unit];
      trunk_gradient[unit] += derivative * weights[row + unit];
    }
  }
}

void backwardOrientation(const Model& model, const OrientationCache& cache,
                         const CandidateOutputGradient& output_gradient,
                         double orientation_weight,
                         std::vector<double>& gradient) {
  CandidateOutputGradient scaled = output_gradient;
  scaled.policy *= orientation_weight;
  const auto scale = [orientation_weight](auto& values) {
    for (double& value : values) value *= orientation_weight;
  };
  scale(scaled.score);
  scale(scaled.lifetime);
  scale(scaled.regeneration);
  scale(scaled.flow);

  std::array<double, kTrunkUnits> trunk_gradient{};
  backwardHead(model, cache, ModelLayout::kPolicy, &scaled.policy, 1,
               trunk_gradient, gradient);
  backwardHead(model, cache, ModelLayout::kScore, scaled.score.data(),
               kScoreQuantiles, trunk_gradient, gradient);
  backwardHead(model, cache, ModelLayout::kLifetime, scaled.lifetime.data(),
               kLifetimeQuantiles, trunk_gradient, gradient);
  backwardHead(model, cache, ModelLayout::kRegeneration,
               scaled.regeneration.data(), kRegenerationHeads, trunk_gradient,
               gradient);
  backwardHead(model, cache, ModelLayout::kFlow, scaled.flow.data(), kFlowHeads,
               trunk_gradient, gradient);

  const auto& weights = model.weights();
  std::array<double, kStateUnits> state_gradient{};
  std::array<double, kRelativeUnits> relative_gradient{};
  constexpr int inputs = kStateUnits + kRelativeUnits;
  for (int output = 0; output < kTrunkUnits; ++output) {
    if (!(cache.trunk_pre[output] > 0.0f &&
          cache.trunk_pre[output] < 1.0f)) {
      continue;
    }
    const double derivative = trunk_gradient[output];
    gradient[ModelLayout::kTrunkBias + output] += derivative;
    const int base = ModelLayout::kFusion + output * inputs;
    for (int unit = 0; unit < kStateUnits; ++unit) {
      gradient[base + unit] += derivative * cache.state[unit];
      state_gradient[unit] += derivative * weights[base + unit];
    }
    for (int unit = 0; unit < kRelativeUnits; ++unit) {
      gradient[base + kStateUnits + unit] +=
          derivative * cache.relative[unit];
      relative_gradient[unit] +=
          derivative * weights[base + kStateUnits + unit];
    }
  }

  for (int unit = 0; unit < kRelativeUnits; ++unit) {
    if (!(cache.relative_pre[unit] > 0.0f &&
          cache.relative_pre[unit] < 1.0f)) {
      continue;
    }
    const double derivative = relative_gradient[unit];
    gradient[ModelLayout::kRelativeBias + unit] += derivative;
    for (int row = 0; row < kBoardSize; ++row) {
      for (int column = 0; column < kBoardSize; ++column) {
        const int distance = column - cache.action + (kBoardSize - 1);
        const int token = cache.input.board[indexOf(row, column)];
        const int category =
            ((row * kRelativeDistances + distance) * kCellKinds + token);
        gradient[ModelLayout::kRelative + category * kRelativeUnits + unit] +=
            derivative;
      }
    }
  }
  for (int unit = 0; unit < kStateUnits; ++unit) {
    if (!(cache.state_pre[unit] > 0.0f && cache.state_pre[unit] < 1.0f)) {
      continue;
    }
    const double derivative = state_gradient[unit];
    gradient[ModelLayout::kStateBias + unit] += derivative;
    for (int position = 0; position < kCellCount; ++position) {
      const int token = cache.input.board[position];
      gradient[ModelLayout::kStateBoard +
               (position * kCellKinds + token) * kStateUnits + unit] +=
          derivative;
    }
    gradient[ModelLayout::kStateNext +
             (cache.input.next_disc - 1) * kStateUnits + unit] += derivative;
    gradient[ModelLayout::kStatePhase +
             (cache.input.phase - 1) * kStateUnits + unit] += derivative;
  }
}

double quantilePredictionGradient(double prediction, double target,
                                  int index, int count) {
  const double quantile = (static_cast<double>(index) + 0.5) / count;
  const double residual = target - prediction;
  const double direction = residual < 0.0 ? 1.0 : 0.0;
  return -std::abs(quantile - direction) *
         std::clamp(residual, -1.0, 1.0) / count;
}

double analyticLossAndGradient(const Model& model,
                               const TrainingExample& example,
                               std::vector<double>& gradient) {
  gradient.assign(kParameterCount, 0.0);
  const Prediction prediction = model.predict(example.state);
  const TrainingLoss loss = trainingLoss(prediction, example.target, model);
  const auto policy_gradient =
      policyLogitGradients(prediction, example.target);
  std::array<CandidateOutputGradient, kBoardSize> output_gradient{};
  for (int action = 0; action < kBoardSize; ++action) {
    if (!prediction.legal[action]) continue;
    output_gradient[action].policy = policy_gradient[action];
  }
  const int played = example.target.played_action;
  const CandidatePrediction& selected = prediction.candidate[played];
  for (int quantile = 0; quantile < kScoreQuantiles; ++quantile) {
    output_gradient[played].score[quantile] =
        kScoreQuantileLossWeight * quantilePredictionGradient(
                                       selected.score[quantile],
                                       normalizedScoreTarget(
                                           example.target.remaining_score),
                                       quantile, kScoreQuantiles);
  }
  for (int quantile = 0; quantile < kLifetimeQuantiles; ++quantile) {
    output_gradient[played].lifetime[quantile] =
        kLifetimeQuantileLossWeight * quantilePredictionGradient(
                                          selected.lifetime[quantile],
                                          normalizedLifetimeTarget(
                                              example.target.remaining_lifetime),
                                          quantile, kLifetimeQuantiles);
  }
  for (int head = 0; head < kRegenerationHeads; ++head) {
    output_gradient[played].regeneration[head] =
        kRegenerationLossWeight /
        static_cast<double>(kRegenerationHeads) *
        (sigmoid(selected.regeneration[head]) -
         (example.target.auxiliary.regenerated_by[head] ? 1.0 : 0.0));
  }
  for (int head = 0; head < kFlowHeads; ++head) {
    output_gradient[played].flow[head] =
        kFlowLossWeight / static_cast<double>(kFlowHeads) *
        std::clamp(static_cast<double>(selected.flow[head]) -
                       normalizedFlowTarget(example.target.auxiliary, head),
                   -1.0, 1.0);
  }

  const PublicState reflected = mirror(example.state);
  for (int action = 0; action < kBoardSize; ++action) {
    if (!prediction.legal[action]) continue;
    backwardOrientation(model, orientationCache(model, example.state, action),
                        output_gradient[action], 0.5, gradient);
    backwardOrientation(
        model,
        orientationCache(model, reflected, kBoardSize - 1 - action),
        output_gradient[action], 0.5, gradient);
  }
  for (int index = 0; index < kParameterCount; ++index) {
    gradient[index] += 2.0 * kL2Weight * model.weights()[index];
  }
  return loss.total;
}

double batchAnalyticLossAndGradient(const Model& model,
                                    const ReplayBuffer& replay,
                                    const std::vector<std::size_t>& indices,
                                    std::vector<double>& gradient) {
  if (indices.empty()) throw std::invalid_argument("empty analytic batch");
  gradient.assign(kParameterCount, 0.0);
  std::vector<double> example_gradient;
  double loss = 0.0;
  for (const std::size_t index : indices) {
    loss += analyticLossAndGradient(model, replay[index], example_gradient);
    for (int parameter = 0; parameter < kParameterCount; ++parameter) {
      gradient[parameter] += example_gradient[parameter];
    }
  }
  const double inverse = 1.0 / static_cast<double>(indices.size());
  for (double& value : gradient) value *= inverse;
  return loss * inverse;
}

struct GradientClipResult {
  double norm = 0.0;
  double scale = 1.0;
};

GradientClipResult gradientClipResult(const std::vector<double>& gradient) {
  double maximum = 0.0;
  for (const double value : gradient) {
    if (!std::isfinite(value)) {
      throw std::runtime_error("optimizer gradient was non-finite");
    }
    maximum = std::max(maximum, std::abs(value));
  }
  if (maximum == 0.0) return {};
  double scaled_squares = 0.0;
  for (const double value : gradient) {
    const double scaled = value / maximum;
    scaled_squares += scaled * scaled;
  }
  const double scaled_norm = std::sqrt(scaled_squares);
  const double overflow_limit =
      std::numeric_limits<double>::max() / scaled_norm;
  const double norm = maximum > overflow_limit
                          ? std::numeric_limits<double>::max()
                          : maximum * scaled_norm;
  const double scale =
      maximum > kGradientNormClip / scaled_norm
          ? (kGradientNormClip / maximum) / scaled_norm
          : 1.0;
  if (!(scale > 0.0) || !std::isfinite(scale)) {
    throw std::runtime_error("optimizer clip scale underflowed");
  }
  return {norm, scale};
}

class DeterministicOptimizer {
 public:
  DeterministicOptimizer()
      : first_moment_(kParameterCount, 0.0f),
        second_moment_(kParameterCount, 0.0f) {}

  double step(Model& model, const ReplayBuffer& replay,
              std::uint32_t round, std::uint32_t local_update) {
    if (replay.empty()) throw std::invalid_argument("cannot train empty replay");
    std::vector<std::size_t> batch;
    batch.reserve(kOptimizerBatchSize);
    for (int item = 0; item < kOptimizerBatchSize; ++item) {
      const std::uint32_t bits =
          mix32(kReplayDomain ^ ((round + 1u) * 0x9e37'79b9u) ^
                ((local_update + 1u) * 0x85eb'ca6bu) ^
                (static_cast<std::uint32_t>(item + 1) * 0xc2b2'ae35u));
      batch.push_back(bits % replay.size());
    }
    return stepBatch(model, replay, batch);
  }

  double stepBatch(Model& model, const ReplayBuffer& replay,
                   const std::vector<std::size_t>& batch) {
    if (replay.empty() || batch.empty()) {
      throw std::invalid_argument("cannot train empty replay/batch");
    }
    if (updates_ == std::numeric_limits<std::uint32_t>::max() ||
        !std::all_of(model.weights().begin(), model.weights().end(),
                     [](float value) { return std::isfinite(value); }) ||
        !std::all_of(first_moment_.begin(), first_moment_.end(),
                     [](float value) { return std::isfinite(value); }) ||
        !std::all_of(second_moment_.begin(), second_moment_.end(),
                     [](float value) {
                       return std::isfinite(value) && value >= 0.0f;
                     })) {
      throw std::runtime_error("optimizer state/model was invalid");
    }
    for (const std::size_t index : batch) {
      if (replay[index].calibration_reservation != 0) {
        throw std::invalid_argument(
            "optimizer batch contained a reserved calibration trajectory");
      }
    }
    std::vector<double> gradient;
    const double loss =
        batchAnalyticLossAndGradient(model, replay, batch, gradient);
    if (!std::isfinite(loss)) {
      throw std::runtime_error("optimizer loss was non-finite");
    }
    const GradientClipResult clipping = gradientClipResult(gradient);
    const std::uint32_t update = updates_;
    const double beta1_power =
        std::pow(kOptimizerBeta1, static_cast<double>(update + 1u));
    const double beta2_power =
        std::pow(kOptimizerBeta2, static_cast<double>(update + 1u));
    std::vector<float> next_first = first_moment_;
    std::vector<float> next_second = second_moment_;
    std::vector<float> next_weights = model.weights();
    for (int index = 0; index < kParameterCount; ++index) {
      const float derivative =
          static_cast<float>(gradient[index] * clipping.scale);
      next_first[index] =
          static_cast<float>(kOptimizerBeta1 * first_moment_[index] +
                             (1.0 - kOptimizerBeta1) * derivative);
      next_second[index] =
          static_cast<float>(kOptimizerBeta2 * second_moment_[index] +
                             (1.0 - kOptimizerBeta2) * derivative *
                                 derivative);
      const double corrected_first = next_first[index] / (1.0 - beta1_power);
      const double corrected_second =
          next_second[index] / (1.0 - beta2_power);
      next_weights[index] -= static_cast<float>(
          kOptimizerLearningRate * corrected_first /
          (std::sqrt(corrected_second) + kOptimizerEpsilon));
      if (!std::isfinite(derivative) || !std::isfinite(next_first[index]) ||
          !std::isfinite(next_second[index]) || next_second[index] < 0.0f ||
          !std::isfinite(next_weights[index])) {
        throw std::runtime_error(
            "optimizer candidate update was non-finite");
      }
    }
    first_moment_ = std::move(next_first);
    second_moment_ = std::move(next_second);
    model.weights() = std::move(next_weights);
    ++updates_;
    last_gradient_norm_ = clipping.norm;
    last_gradient_scale_ = clipping.scale;
    return loss;
  }

  std::uint32_t updates() const { return updates_; }
  double lastGradientNorm() const { return last_gradient_norm_; }
  double lastGradientScale() const { return last_gradient_scale_; }
  const std::vector<float>& firstMoment() const { return first_moment_; }
  const std::vector<float>& secondMoment() const { return second_moment_; }

  void restoreState(std::vector<float> first, std::vector<float> second,
                    std::uint32_t updates, double last_gradient_norm = 0.0,
                    double last_gradient_scale = 1.0) {
    if (first.size() != kParameterCount || second.size() != kParameterCount ||
        !std::all_of(first.begin(), first.end(), [](float value) {
          return std::isfinite(value);
        }) ||
        !std::all_of(second.begin(), second.end(), [](float value) {
          return std::isfinite(value) && value >= 0.0f;
        }) ||
        !std::isfinite(last_gradient_norm) || last_gradient_norm < 0.0 ||
        !std::isfinite(last_gradient_scale) || last_gradient_scale <= 0.0 ||
        last_gradient_scale > 1.0) {
      throw std::invalid_argument("invalid serialized Adam state");
    }
    first_moment_ = std::move(first);
    second_moment_ = std::move(second);
    updates_ = updates;
    last_gradient_norm_ = last_gradient_norm;
    last_gradient_scale_ = last_gradient_scale;
  }

 private:
  std::vector<float> first_moment_;
  std::vector<float> second_moment_;
  std::uint32_t updates_ = 0;
  double last_gradient_norm_ = 0.0;
  double last_gradient_scale_ = 1.0;
};

struct GradientCheckResult {
  int checked = 0;
  double maximum_relative_error = 0.0;
  int worst_parameter = -1;
  double worst_numeric = 0.0;
  double worst_analytic = 0.0;
  bool passed = false;
};

GradientCheckResult deterministicGradientCheck(const Model& model,
                                               const TrainingExample& example) {
  std::vector<double> analytic;
  analyticLossAndGradient(model, example, analytic);
  constexpr std::array<std::array<int, 2>, 8> ranges{{
      {{ModelLayout::kStateBoard, ModelLayout::kStateNext}},
      {{ModelLayout::kStateNext, ModelLayout::kRelative}},
      {{ModelLayout::kRelative, ModelLayout::kFusion}},
      {{ModelLayout::kFusion, ModelLayout::kPolicy}},
      {{ModelLayout::kPolicy, ModelLayout::kScore}},
      {{ModelLayout::kScore, ModelLayout::kLifetime}},
      {{ModelLayout::kLifetime, ModelLayout::kRegeneration}},
      {{ModelLayout::kRegeneration, ModelLayout::kCount}},
  }};
  GradientCheckResult result;
  constexpr float epsilon = 4.0e-4f;
  constexpr int checks_per_range = 4;
  for (const auto& range : ranges) {
    std::vector<int> selected(range[1] - range[0]);
    std::iota(selected.begin(), selected.end(), range[0]);
    std::partial_sort(
        selected.begin(), selected.begin() + checks_per_range, selected.end(),
        [&](int left, int right) {
          return std::abs(analytic[left]) > std::abs(analytic[right]);
        });
    for (int check = 0; check < checks_per_range; ++check) {
      const int parameter = selected[check];
      Model positive = model;
      Model negative = model;
      positive.weights()[parameter] += epsilon;
      negative.weights()[parameter] -= epsilon;
      const double positive_loss =
          trainingLoss(positive.predict(example.state), example.target,
                       positive)
              .total;
      const double negative_loss =
          trainingLoss(negative.predict(example.state), example.target,
                       negative)
              .total;
      const double numeric =
          (positive_loss - negative_loss) / (2.0 * epsilon);
      const double denominator = std::max(
          1.0e-6, std::abs(numeric) + std::abs(analytic[parameter]));
      const double relative_error =
          std::abs(numeric - analytic[parameter]) / denominator;
      if (relative_error > result.maximum_relative_error) {
        result.maximum_relative_error = relative_error;
        result.worst_parameter = parameter;
        result.worst_numeric = numeric;
        result.worst_analytic = analytic[parameter];
      }
      ++result.checked;
    }
  }
  result.passed =
      result.checked ==
          checks_per_range * static_cast<int>(ranges.size()) &&
      result.maximum_relative_error <= 0.005;
  return result;
}

std::vector<std::size_t> deterministicEpochOrder(std::size_t size,
                                                 std::uint32_t round,
                                                 std::uint32_t epoch) {
  if (size == 0 || size > std::numeric_limits<std::uint32_t>::max()) {
    throw std::invalid_argument("bad full-corpus epoch size");
  }
  std::vector<std::size_t> result(size);
  std::iota(result.begin(), result.end(), 0u);
  for (std::size_t remaining = size; remaining > 1; --remaining) {
    const std::uint32_t bits = mix32(
        kTrainingShuffleDomain ^ ((round + 1u) * 0x9e37'79b9u) ^
        ((epoch + 1u) * 0x85eb'ca6bu) ^
        (static_cast<std::uint32_t>(remaining) * 0xc2b2'ae35u));
    const std::size_t selected = bits % remaining;
    std::swap(result[remaining - 1], result[selected]);
  }
  return result;
}

int optimizeEpochs(Model& model, const ReplayBuffer& replay,
                   DeterministicOptimizer& optimizer, int epochs,
                   std::uint32_t round) {
  if (epochs <= 0) throw std::invalid_argument("epoch count must be positive");
  for (const TrainingExample& example : replay.examples()) {
    if (example.calibration_reservation != 0) {
      throw std::invalid_argument(
          "optimizer received a permanently reserved calibration group");
    }
  }
  int batches = 0;
  for (int epoch = 0; epoch < epochs; ++epoch) {
    const std::vector<std::size_t> order = deterministicEpochOrder(
        replay.size(), round, static_cast<std::uint32_t>(epoch));
    for (std::size_t begin = 0; begin < order.size();
         begin += kOptimizerBatchSize) {
      const std::size_t end =
          std::min(order.size(), begin + static_cast<std::size_t>(
                                             kOptimizerBatchSize));
      const std::vector<std::size_t> batch(order.begin() + begin,
                                           order.begin() + end);
      optimizer.stepBatch(model, replay, batch);
      ++batches;
    }
  }
  return batches;
}

// ---------------------------------------------------------------------------
// Guarded exact-D4 and public on-policy game/root runners
// ---------------------------------------------------------------------------

enum class FreshPurpose {
  kD4Initialization,
  kExpertGame,
  kStageA,
  kStageB,
  kDevelopmentConfirmation,
};

SeedLane laneFor(FreshPurpose purpose) {
  switch (purpose) {
    case FreshPurpose::kD4Initialization:
      return kD4InitializationLane;
    case FreshPurpose::kExpertGame:
      return kExpertGameLane;
    case FreshPurpose::kStageA:
      return kStageALane;
    case FreshPurpose::kStageB:
      return kStageBLane;
    case FreshPurpose::kDevelopmentConfirmation:
      return kDevelopmentConfirmationLane;
  }
  throw std::invalid_argument("unknown fresh-seed purpose");
}

class AuthorizedSeed {
 public:
  static AuthorizedSeed checked(FreshPurpose purpose, std::uint32_t seed) {
    const SeedLane lane = laneFor(purpose);
    if (seed < lane.first || seed > lane.last ||
        (seed & 0xff00'0000u) != 0x3d00'0000u) {
      throw std::invalid_argument("fresh seed escaped its frozen 0x3d lane");
    }
    return AuthorizedSeed(purpose, seed);
  }

  FreshPurpose purpose() const { return purpose_; }
  std::uint32_t value() const { return seed_; }

 private:
  AuthorizedSeed(FreshPurpose purpose, std::uint32_t seed)
      : purpose_(purpose), seed_(seed) {}
  FreshPurpose purpose_;
  std::uint32_t seed_;
};

void requireExactLane(FreshPurpose purpose, std::uint32_t first, int games) {
  if (games <= 0) throw std::invalid_argument("fresh cohort was empty");
  const SeedLane lane = laneFor(purpose);
  const std::uint64_t last = static_cast<std::uint64_t>(first) + games - 1u;
  if (first != lane.first || last != lane.last) {
    throw std::invalid_argument("fresh cohort did not exactly match frozen lane");
  }
}

std::uint32_t expertSeed(int round, int worker, int local_game) {
  if (round < 0 || round >= kRounds || worker < 0 || worker >= kWorkers ||
      local_game < 0 || local_game >= 0x700) {
    throw std::invalid_argument("expert seed coordinate outside frozen map");
  }
  const std::uint32_t seed =
      kExpertGameLane.first + static_cast<std::uint32_t>(round * 0x7000) +
      static_cast<std::uint32_t>(worker * 0x700 + local_game);
  return AuthorizedSeed::checked(FreshPurpose::kExpertGame, seed).value();
}

int directPolicyAction(const PublicState& source, const Model& model,
                       ConstraintTrust trust) {
  bool was_mirrored = false;
  const PublicState state = canonical(source, was_mirrored);
  const Prediction prediction = model.predict(state);
  const int best_action =
      constrainedPredictedAction(state, prediction, 0, 0u, trust);
  return was_mirrored && best_action >= 0 ? kBoardSize - 1 - best_action
                                          : best_action;
}

struct RootObservation {
  PublicState state{};
  SearchResult search{};
  int played_action = -1;
  std::int64_t score_before = 0;
  int move_before = 0;
};

struct CapturedGame {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  std::uint64_t cleared = 0;
  std::uint64_t revealed = 0;
  bool natural = false;
  std::vector<RootObservation> roots;
  std::vector<TrajectoryStep> steps;
};

TrajectoryStep trajectoryStep(const MoveResult& move) {
  TrajectoryStep result;
  result.state = publicState(move.state);
  for (const Wave& wave : move.waves) {
    result.cleared += wave.cleared;
    result.revealed += wave.revealed;
  }
  return result;
}

std::vector<TrainingExample> examplesFromGame(const CapturedGame& game) {
  if (!game.natural) {
    throw std::runtime_error("censored game cannot provide long-outcome labels");
  }
  std::vector<TrainingExample> result;
  result.reserve(game.roots.size());
  for (const RootObservation& root : game.roots) {
    if (root.move_before < 0 ||
        root.move_before >= static_cast<int>(game.steps.size())) {
      throw std::runtime_error("root/trajectory alignment failed");
    }
    const auto begin = game.steps.begin() + root.move_before;
    const auto end = game.steps.begin() + std::min(
        static_cast<int>(game.steps.size()), root.move_before + 40);
    const std::vector<TrajectoryStep> future(begin, end);
    const AuxiliaryTargets auxiliary = auxiliaryTargets(root.state, future);
    TrainingExample example;
    example.state = root.state;
    example.target = makeExpertTarget(
        root.search, root.played_action,
        static_cast<double>(game.score - root.score_before),
        static_cast<double>(game.moves - root.move_before), auxiliary);
    example.trajectory_group = game.seed;
    result.push_back(std::move(example));
  }
  return result;
}

CapturedGame runExactD4Game(const AuthorizedSeed& authorization,
                           bool capture_roots) {
  State state = initialHeadlessState(authorization.value());
  CapturedGame result;
  result.seed = authorization.value();
  while (!state.game_over && state.moves_played < kMaximumGameMoves) {
    const auto decision =
        drop7::fair_only_depth4::chooseDepth4Action(state);
    if (!decision.complete || decision.completed_depth != 4 ||
        !isLegal(state.board, decision.action)) {
      throw std::runtime_error("exact D4 initializer failed to complete");
    }
    if (capture_roots) {
      SearchResult expert;
      expert.action = decision.action;
      expert.visits[decision.action] = kSearchSimulations;
      expert.simulations = kSearchSimulations;
      result.roots.push_back({publicState(state), expert, decision.action,
                              state.score, state.moves_played});
    }
    MoveResult move;
    if (!playHeadlessMove(state, authorization.value(), decision.action,
                          move)) {
      throw std::runtime_error("exact D4 initializer transition failed");
    }
    const TrajectoryStep step = trajectoryStep(move);
    result.cleared += static_cast<std::uint64_t>(step.cleared);
    result.revealed += static_cast<std::uint64_t>(step.revealed);
    result.steps.push_back(step);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.natural = state.game_over;
  if (!result.natural) {
    throw std::runtime_error("exact D4 initializer game hit 2,000-move cap");
  }
  return result;
}

CapturedGame runOnPolicyGame(const AuthorizedSeed& authorization,
                            const Model& model, int searched_root_quota,
                            ConstraintTrust trust) {
  if (authorization.purpose() != FreshPurpose::kExpertGame ||
      searched_root_quota < 0) {
    throw std::invalid_argument("bad on-policy game authorization/quota");
  }
  State state = initialHeadlessState(authorization.value());
  CapturedGame result;
  result.seed = authorization.value();
  while (!state.game_over && state.moves_played < kMaximumGameMoves) {
    int action = -1;
    if (static_cast<int>(result.roots.size()) < searched_root_quota) {
      const SearchResult search = searchRoot(
          publicState(state), model, kSearchSimulations, trust,
          kSearchDepthMoves);
      action = search.action;
      result.roots.push_back({publicState(state), search, action, state.score,
                              state.moves_played});
    } else {
      // Finish the final quota-crossing game without creating extra searched
      // roots, so every round has exactly 20,000 new search targets.
      action = directPolicyAction(publicState(state), model, trust);
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("on-policy public runner chose illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, authorization.value(), action, move)) {
      throw std::runtime_error("on-policy public transition failed");
    }
    const TrajectoryStep step = trajectoryStep(move);
    result.cleared += static_cast<std::uint64_t>(step.cleared);
    result.revealed += static_cast<std::uint64_t>(step.revealed);
    result.steps.push_back(step);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.natural = state.game_over;
  if (!result.natural) {
    throw std::runtime_error("on-policy game hit 2,000-move cap");
  }
  return result;
}

ReplayBuffer runD4Initialization() {
  requireExactLane(FreshPurpose::kD4Initialization,
                   kD4InitializationLane.first, 64);
  std::array<std::vector<TrainingExample>, 64> examples;
  std::atomic<int> next{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < kWorkers; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next.fetch_add(1);
        if (game >= 64) return;
        const AuthorizedSeed seed = AuthorizedSeed::checked(
            FreshPurpose::kD4Initialization,
            kD4InitializationLane.first + static_cast<std::uint32_t>(game));
        examples[game] = examplesFromGame(runExactD4Game(seed, true));
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  ReplayBuffer replay;
  for (auto& game : examples) replay.append(std::move(game));
  return replay;
}

std::vector<TrainingExample> collectOnPolicyRound(const Model& model,
                                                  int round,
                                                  ConstraintTrust trust) {
  constexpr int per_worker = kOnPolicyRootsPerRound / kWorkers;
  std::array<std::vector<TrainingExample>, kWorkers> output;
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < kWorkers; ++worker) {
    workers.push_back(std::async(std::launch::async, [&, worker] {
      int remaining = per_worker;
      int local_game = 0;
      while (remaining > 0) {
        const AuthorizedSeed seed = AuthorizedSeed::checked(
            FreshPurpose::kExpertGame,
            expertSeed(round, worker, local_game++));
        const CapturedGame game =
            runOnPolicyGame(seed, model, remaining, trust);
        std::vector<TrainingExample> examples = examplesFromGame(game);
        if (static_cast<int>(examples.size()) > remaining) {
          examples.resize(remaining);
        }
        remaining -= static_cast<int>(examples.size());
        output[worker].insert(output[worker].end(),
                              std::make_move_iterator(examples.begin()),
                              std::make_move_iterator(examples.end()));
      }
      if (static_cast<int>(output[worker].size()) != per_worker) {
        throw std::runtime_error("on-policy worker root quota drifted");
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  std::vector<TrainingExample> result;
  result.reserve(kOnPolicyRootsPerRound);
  for (auto& worker : output) {
    result.insert(result.end(), std::make_move_iterator(worker.begin()),
                  std::make_move_iterator(worker.end()));
  }
  return result;
}

std::vector<std::size_t> reanalysisIndices(const ReplayBuffer& replay,
                                           int round, int count) {
  if (replay.empty() || round < 0 || round >= kRounds || count < 0) {
    throw std::invalid_argument("bad reanalysis index request");
  }
  std::vector<std::size_t> eligible;
  eligible.reserve(replay.size());
  for (std::size_t index = 0; index < replay.size(); ++index) {
    if (replay[index].calibration_reservation == 0) eligible.push_back(index);
  }
  if (eligible.empty()) {
    throw std::invalid_argument("reanalysis replay has no trainable groups");
  }
  std::vector<std::size_t> result;
  result.reserve(count);
  const std::uint32_t start_bits =
      mix32(kReplayDomain ^
            (static_cast<std::uint32_t>(round + 1) * 0x9e37'79b9u));
  std::size_t stride =
      1u + (mix32(start_bits ^ 0x85eb'ca6bu) % eligible.size());
  while (std::gcd(stride, eligible.size()) != 1u) ++stride;
  const std::size_t start = start_bits % eligible.size();
  for (int item = 0; item < count; ++item) {
    // If the buffer is smaller than the requested cohort, deterministic full
    // passes repeat.  This can occur only during the D4 bootstrap round.
    result.push_back(eligible[(start + static_cast<std::size_t>(item) *
                                      stride) %
                              eligible.size()]);
  }
  return result;
}

TrainingExample reanalyseExample(const TrainingExample& source,
                                  const Model& model,
                                  ConstraintTrust trust) {
  const SearchResult search = searchRoot(source.state, model,
                                         kSearchSimulations, trust,
                                         kSearchDepthMoves);
  TrainingExample result = source;
  result.target = makeExpertTarget(
      search, source.target.played_action, source.target.remaining_score,
      source.target.remaining_lifetime, source.target.auxiliary);
  return result;
}

std::vector<TrainingExample> reanalyseReplay(const ReplayBuffer& replay,
                                             const Model& model, int round,
                                             int count,
                                             ConstraintTrust trust) {
  const std::vector<std::size_t> indices =
      reanalysisIndices(replay, round, count);
  std::vector<TrainingExample> result(count);
  std::atomic<int> next{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < kWorkers; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int item = next.fetch_add(1);
        if (item >= count) return;
        result[item] =
            reanalyseExample(replay[indices[item]], model, trust);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

struct RoundLedger {
  int round = 0;
  int new_roots = 0;
  int reanalysed_roots = 0;
  int training_examples = 0;
  int optimizer_epochs = 0;
  int optimizer_updates = 0;
  ConstraintTrust search_trust{};
  CalibrationResult calibration{};
  std::size_t replay_size_after = 0;
  bool checkpoint_exported = false;
};

struct IterationLedger {
  int d4_pretraining_epochs = 0;
  int d4_pretraining_updates = 0;
  std::array<RoundLedger, kRounds> rounds{};
  int total_new_roots = 0;
  int total_reanalysed_roots = 0;
  int checkpoint_exports = 0;
  int deployed_round = 0;
};

class IterationCompletionCapability {
 public:
  std::uint64_t modelFingerprint() const { return model_fingerprint_; }
  std::uint64_t replayFingerprint() const { return replay_fingerprint_; }
  std::uint64_t calibrationFingerprint() const {
    return calibration_fingerprint_;
  }
  std::uint64_t ledgerFingerprint() const { return ledger_fingerprint_; }

 private:
  IterationCompletionCapability(std::uint64_t model_fingerprint,
                                std::uint64_t replay_fingerprint,
                                std::uint64_t calibration_fingerprint,
                                std::uint64_t ledger_fingerprint)
      : model_fingerprint_(model_fingerprint),
        replay_fingerprint_(replay_fingerprint),
        calibration_fingerprint_(calibration_fingerprint),
        ledger_fingerprint_(ledger_fingerprint) {}

  friend void advanceExpertIterationRound(IterationResumeState& state);
  friend IterationResumeState deserializeResumeState(
      const std::vector<std::uint8_t>& source);

  std::uint64_t model_fingerprint_ = 0;
  std::uint64_t replay_fingerprint_ = 0;
  std::uint64_t calibration_fingerprint_ = 0;
  std::uint64_t ledger_fingerprint_ = 0;
};

struct IterationResumeState {
  Model model{};
  ReplayBuffer replay{};
  DeterministicOptimizer optimizer{};
  IterationLedger ledger{};
  CalibrationResult previous_calibration{};
  int next_round = 0;

 private:
  friend void validateResumeState(const IterationResumeState& state);
  friend std::vector<std::uint8_t> serializeResumeState(
      const IterationResumeState& state);
  friend IterationResumeState deserializeResumeState(
      const std::vector<std::uint8_t>& source);
  friend void advanceExpertIterationRound(IterationResumeState& state);
  friend std::optional<DeploymentCertificate> certifyCompletedIteration(
      const IterationResumeState& state);
  std::optional<IterationCompletionCapability> completion_capability_;
};

void requireBytes(const std::vector<std::uint8_t>& source,
                  std::size_t cursor, std::size_t count,
                  std::string_view label) {
  if (cursor > source.size() || count > source.size() - cursor) {
    throw std::runtime_error(std::string("truncated ") + std::string(label));
  }
}

void appendCalibrationHalf(std::vector<std::uint8_t>& output,
                           const CalibrationHalfMetrics& half) {
  appendU32(output, static_cast<std::uint32_t>(half.examples));
  appendU32(output, static_cast<std::uint32_t>(half.trajectory_groups));
  appendDouble(output, half.lifetime_coverage);
  appendDouble(output, half.lifetime_lower_coverage);
  for (const int count : half.played_per_column) {
    appendU32(output, static_cast<std::uint32_t>(count));
  }
  for (const double value : half.regeneration_ece) appendDouble(output, value);
  for (const double value : half.regeneration_brier) appendDouble(output, value);
  for (const double value : half.flow_normalized_mae) appendDouble(output, value);
  output.push_back(half.finite ? 1u : 0u);
  output.push_back(half.lifetime_pass ? 1u : 0u);
  output.push_back(half.regeneration_pass ? 1u : 0u);
  output.push_back(half.flow_pass ? 1u : 0u);
}

CalibrationHalfMetrics readCalibrationHalf(
    const std::vector<std::uint8_t>& source, std::size_t& cursor) {
  CalibrationHalfMetrics half;
  constexpr std::size_t kSerializedHalfBytes =
      2u * 4u + 2u * 8u + kBoardSize * 4u +
      2u * kRegenerationHeads * 8u + kFlowHeads * 8u + 4u;
  requireBytes(source, cursor, kSerializedHalfBytes, "calibration half");
  half.examples = static_cast<int>(readU32(source, cursor));
  cursor += 4;
  half.trajectory_groups = static_cast<int>(readU32(source, cursor));
  cursor += 4;
  half.lifetime_coverage = readDouble(source, cursor);
  cursor += 8;
  half.lifetime_lower_coverage = readDouble(source, cursor);
  cursor += 8;
  for (int& count : half.played_per_column) {
    count = static_cast<int>(readU32(source, cursor));
    cursor += 4;
  }
  for (double& value : half.regeneration_ece) {
    value = readDouble(source, cursor);
    cursor += 8;
  }
  for (double& value : half.regeneration_brier) {
    value = readDouble(source, cursor);
    cursor += 8;
  }
  for (double& value : half.flow_normalized_mae) {
    value = readDouble(source, cursor);
    cursor += 8;
  }
  const auto read_bool = [&]() {
    const std::uint8_t value = source[cursor++];
    if (value > 1u) throw std::runtime_error("invalid calibration boolean");
    return value != 0u;
  };
  half.finite = read_bool();
  half.lifetime_pass = read_bool();
  half.regeneration_pass = read_bool();
  half.flow_pass = read_bool();
  if (half.examples < 0 || half.trajectory_groups < 0 ||
      std::any_of(half.played_per_column.begin(),
                  half.played_per_column.end(),
                  [](int value) { return value < 0; })) {
    throw std::runtime_error("invalid calibration count");
  }
  return half;
}

std::vector<std::uint8_t> serializeCalibration(
    const CalibrationResult& calibration) {
  std::vector<std::uint8_t> output;
  for (const CalibrationHalfMetrics& half : calibration.half) {
    appendCalibrationHalf(output, half);
  }
  return output;
}

CalibrationResult deserializeCalibration(
    const std::vector<std::uint8_t>& source) {
  CalibrationResult result;
  std::size_t cursor = 0;
  for (CalibrationHalfMetrics& half : result.half) {
    half = readCalibrationHalf(source, cursor);
  }
  if (cursor != source.size()) {
    throw std::runtime_error("calibration payload had trailing bytes");
  }
  return result;
}

void appendRoundLedger(std::vector<std::uint8_t>& output,
                       const RoundLedger& round) {
  for (const int value : {round.round, round.new_roots,
                          round.reanalysed_roots, round.training_examples,
                          round.optimizer_epochs, round.optimizer_updates}) {
    appendU32(output, static_cast<std::uint32_t>(value));
  }
  output.push_back(round.search_trust.lifetime ? 1u : 0u);
  output.push_back(round.search_trust.regeneration ? 1u : 0u);
  output.push_back(round.search_trust.flow ? 1u : 0u);
  const std::vector<std::uint8_t> calibration =
      serializeCalibration(round.calibration);
  output.insert(output.end(), calibration.begin(), calibration.end());
  appendU64(output, static_cast<std::uint64_t>(round.replay_size_after));
  output.push_back(round.checkpoint_exported ? 1u : 0u);
}

RoundLedger readRoundLedger(const std::vector<std::uint8_t>& source,
                            std::size_t& cursor,
                            std::size_t calibration_bytes) {
  requireBytes(source, cursor, 6u * 4u + 3u + calibration_bytes + 8u + 1u,
               "round ledger");
  RoundLedger round;
  int* values[] = {&round.round, &round.new_roots, &round.reanalysed_roots,
                   &round.training_examples, &round.optimizer_epochs,
                   &round.optimizer_updates};
  for (int* value : values) {
    *value = static_cast<int>(readU32(source, cursor));
    cursor += 4;
  }
  const auto read_bool = [&]() {
    const std::uint8_t value = source[cursor++];
    if (value > 1u) throw std::runtime_error("invalid ledger boolean");
    return value != 0u;
  };
  round.search_trust.lifetime = read_bool();
  round.search_trust.regeneration = read_bool();
  round.search_trust.flow = read_bool();
  const std::vector<std::uint8_t> calibration(
      source.begin() + static_cast<std::ptrdiff_t>(cursor),
      source.begin() + static_cast<std::ptrdiff_t>(cursor + calibration_bytes));
  round.calibration = deserializeCalibration(calibration);
  cursor += calibration_bytes;
  const std::uint64_t replay_size = readU64(source, cursor);
  cursor += 8;
  if (replay_size > std::numeric_limits<std::size_t>::max()) {
    throw std::runtime_error("resume replay size overflow");
  }
  round.replay_size_after = static_cast<std::size_t>(replay_size);
  round.checkpoint_exported = read_bool();
  return round;
}

std::vector<std::uint8_t> serializeIterationLedger(
    const IterationLedger& ledger) {
  std::vector<std::uint8_t> output;
  appendU32(output, static_cast<std::uint32_t>(ledger.d4_pretraining_epochs));
  appendU32(output, static_cast<std::uint32_t>(ledger.d4_pretraining_updates));
  for (const RoundLedger& round : ledger.rounds) {
    appendRoundLedger(output, round);
  }
  appendU32(output, static_cast<std::uint32_t>(ledger.total_new_roots));
  appendU32(output,
            static_cast<std::uint32_t>(ledger.total_reanalysed_roots));
  appendU32(output, static_cast<std::uint32_t>(ledger.checkpoint_exports));
  appendU32(output, static_cast<std::uint32_t>(ledger.deployed_round));
  return output;
}

IterationLedger deserializeIterationLedger(
    const std::vector<std::uint8_t>& source,
    std::size_t calibration_bytes) {
  requireBytes(source, 0, 8, "iteration ledger");
  IterationLedger ledger;
  std::size_t cursor = 0;
  ledger.d4_pretraining_epochs = static_cast<int>(readU32(source, cursor));
  cursor += 4;
  ledger.d4_pretraining_updates = static_cast<int>(readU32(source, cursor));
  cursor += 4;
  for (RoundLedger& round : ledger.rounds) {
    round = readRoundLedger(source, cursor, calibration_bytes);
  }
  requireBytes(source, cursor, 16, "iteration ledger totals");
  ledger.total_new_roots = static_cast<int>(readU32(source, cursor));
  cursor += 4;
  ledger.total_reanalysed_roots = static_cast<int>(readU32(source, cursor));
  cursor += 4;
  ledger.checkpoint_exports = static_cast<int>(readU32(source, cursor));
  cursor += 4;
  ledger.deployed_round = static_cast<int>(readU32(source, cursor));
  cursor += 4;
  if (cursor != source.size()) {
    throw std::runtime_error("iteration ledger had trailing bytes");
  }
  return ledger;
}

bool calibrationBitwiseEqual(const CalibrationResult& first,
                             const CalibrationResult& second) {
  return serializeCalibration(first) == serializeCalibration(second);
}

std::array<std::uint64_t, 4> completionFingerprints(
    const IterationResumeState& state) {
  return {{modelFingerprint(state.model),
           fnv1a64(serializeReplay(state.replay)),
           fnv1a64(serializeCalibration(state.previous_calibration)),
           fnv1a64(serializeIterationLedger(state.ledger))}};
}

void validateResumeState(const IterationResumeState& state) {
  if (state.next_round < 0 || state.next_round > kRounds ||
      state.replay.empty() ||
      state.ledger.d4_pretraining_epochs != kD4PretrainingEpochs ||
      state.ledger.d4_pretraining_updates < 1 ||
      state.ledger.checkpoint_exports != 0 ||
      state.ledger.deployed_round != 0 ||
      state.ledger.total_new_roots !=
          state.next_round * kOnPolicyRootsPerRound ||
      state.ledger.total_reanalysed_roots !=
          state.next_round * kReanalysisRootsPerRound ||
      state.replay.size() > kMaximumFinalReplayRoots ||
      (state.next_round == 0 &&
       state.replay.size() > kMaximumD4BootstrapRoots)) {
    throw std::runtime_error("invalid iteration resume schedule");
  }
  std::uint64_t expected_updates =
      static_cast<std::uint64_t>(state.ledger.d4_pretraining_updates);
  std::size_t prior_replay_size = 0;
  for (int index = 0; index < kRounds; ++index) {
    const RoundLedger& round = state.ledger.rounds[index];
    if (index < state.next_round) {
      if (round.round != index + 1 ||
          round.new_roots != kOnPolicyRootsPerRound ||
          round.reanalysed_roots != kReanalysisRootsPerRound ||
          round.training_examples < kReanalysisRootsPerRound ||
          round.optimizer_epochs != kOptimizerEpochsPerRound ||
          round.optimizer_updates < 1 || round.checkpoint_exported ||
          (index == 0 &&
           (round.replay_size_after <= kOnPolicyRootsPerRound ||
            round.replay_size_after >
                kMaximumD4BootstrapRoots + kOnPolicyRootsPerRound)) ||
          (index > 0 &&
           round.replay_size_after !=
               prior_replay_size + kOnPolicyRootsPerRound)) {
        throw std::runtime_error("completed resume round is inconsistent");
      }
      expected_updates += static_cast<std::uint64_t>(round.optimizer_updates);
      prior_replay_size = round.replay_size_after;
    } else if (round.round != 0 || round.new_roots != 0 ||
               round.reanalysed_roots != 0 ||
               round.training_examples != 0 || round.optimizer_epochs != 0 ||
               round.optimizer_updates != 0 ||
               round.search_trust != ConstraintTrust{} ||
               round.replay_size_after != 0 || round.checkpoint_exported ||
               !calibrationBitwiseEqual(round.calibration,
                                        CalibrationResult{})) {
      throw std::runtime_error("future resume round was not empty");
    }
  }
  if ((state.next_round > 0 &&
       (state.replay.size() != prior_replay_size ||
        !calibrationBitwiseEqual(
            state.previous_calibration,
            state.ledger.rounds[state.next_round - 1].calibration))) ||
      (state.next_round == 0 &&
       !calibrationBitwiseEqual(state.previous_calibration,
                                CalibrationResult{})) ||
      expected_updates != state.optimizer.updates()) {
    throw std::runtime_error("resume model/optimizer boundary is inconsistent");
  }
  const bool qualified_completion =
      state.next_round == kRounds &&
      state.previous_calibration.trust().all();
  if (qualified_completion != state.completion_capability_.has_value()) {
    throw std::runtime_error("resume completion capability was missing/spurious");
  }
  if (state.completion_capability_) {
    const auto fingerprints = completionFingerprints(state);
    const IterationCompletionCapability& capability =
        *state.completion_capability_;
    if (fingerprints[0] != capability.modelFingerprint() ||
        fingerprints[1] != capability.replayFingerprint() ||
        fingerprints[2] != capability.calibrationFingerprint() ||
        fingerprints[3] != capability.ledgerFingerprint()) {
      throw std::runtime_error("resume completion capability provenance drifted");
    }
  }
}

constexpr std::array<char, 8> kResumeMagic{{'D', '7', 'R', 'S', 'M', 'E',
                                            '1', '\0'}};
constexpr std::uint32_t kResumeVersion = 1;
constexpr std::uint32_t kResumeCompletionCapabilityVersion = 1;
constexpr std::size_t kResumeHeaderBytes = 120;

std::uint64_t resumeChecksum(const std::vector<std::uint8_t>& bytes) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    const std::uint8_t byte = index >= 16 && index < 24 ? 0u : bytes[index];
    hash ^= byte;
    hash *= 0x0000'0100'0000'01b3ull;
  }
  return hash;
}

std::vector<std::uint8_t> serializeResumeState(
    const IterationResumeState& state) {
  validateResumeState(state);
  const std::vector<std::uint8_t> model = serializeCheckpoint(state.model);
  const std::vector<std::uint8_t> replay = serializeReplay(state.replay);
  const std::vector<std::uint8_t> calibration =
      serializeCalibration(state.previous_calibration);
  const std::vector<std::uint8_t> ledger =
      serializeIterationLedger(state.ledger);
  std::vector<std::uint8_t> output;
  output.reserve(kResumeHeaderBytes + model.size() +
                 8u * static_cast<std::size_t>(kParameterCount) +
                 replay.size() + calibration.size() + ledger.size());
  for (const char value : kResumeMagic) {
    output.push_back(static_cast<std::uint8_t>(value));
  }
  appendU32(output, kResumeVersion);
  appendU32(output, static_cast<std::uint32_t>(state.next_round));
  appendU64(output, 0u);  // whole-state checksum placeholder
  appendU64(output, static_cast<std::uint64_t>(model.size()));
  appendU64(output, static_cast<std::uint64_t>(replay.size()));
  appendU32(output, kParameterCount);
  appendU32(output, state.optimizer.updates());
  appendU32(output, static_cast<std::uint32_t>(calibration.size()));
  appendU32(output, static_cast<std::uint32_t>(ledger.size()));
  appendU64(output, 0u);  // reserved
  const bool completed = state.completion_capability_.has_value();
  appendU32(output, completed ? 1u : 0u);
  appendU32(output,
            completed ? kResumeCompletionCapabilityVersion : 0u);
  appendU64(output, completed
                        ? state.completion_capability_->modelFingerprint()
                        : 0u);
  appendU64(output, completed
                        ? state.completion_capability_->replayFingerprint()
                        : 0u);
  appendU64(output,
            completed
                ? state.completion_capability_->calibrationFingerprint()
                : 0u);
  appendU64(output, completed
                        ? state.completion_capability_->ledgerFingerprint()
                        : 0u);
  appendDouble(output, state.optimizer.lastGradientNorm());
  appendDouble(output, state.optimizer.lastGradientScale());
  output.insert(output.end(), model.begin(), model.end());
  for (const float value : state.optimizer.firstMoment()) {
    appendFloat(output, value);
  }
  for (const float value : state.optimizer.secondMoment()) {
    appendFloat(output, value);
  }
  output.insert(output.end(), replay.begin(), replay.end());
  output.insert(output.end(), calibration.begin(), calibration.end());
  output.insert(output.end(), ledger.begin(), ledger.end());
  const std::uint64_t checksum = resumeChecksum(output);
  for (int byte = 0; byte < 8; ++byte) {
    output[16 + byte] =
        static_cast<std::uint8_t>((checksum >> (8 * byte)) & 0xffu);
  }
  return output;
}

IterationResumeState deserializeResumeState(
    const std::vector<std::uint8_t>& source) {
  if (source.size() < kResumeHeaderBytes ||
      !std::equal(kResumeMagic.begin(), kResumeMagic.end(), source.begin()) ||
      readU32(source, 8) != kResumeVersion || readU64(source, 56) != 0u ||
      readU64(source, 16) != resumeChecksum(source) ||
      readU32(source, 40) != kParameterCount || readU32(source, 64) > 1u) {
    throw std::runtime_error("invalid iteration resume header");
  }
  const bool completed = readU32(source, 64) != 0u;
  if ((!completed &&
       (readU32(source, 68) != 0u || readU64(source, 72) != 0u ||
        readU64(source, 80) != 0u || readU64(source, 88) != 0u ||
        readU64(source, 96) != 0u)) ||
      (completed &&
       (readU32(source, 68) != kResumeCompletionCapabilityVersion ||
        readU64(source, 72) == 0u || readU64(source, 80) == 0u ||
        readU64(source, 88) == 0u || readU64(source, 96) == 0u))) {
    throw std::runtime_error("invalid resume completion capability header");
  }
  const std::uint64_t model_size = readU64(source, 24);
  const std::uint64_t replay_size = readU64(source, 32);
  const std::uint32_t calibration_size = readU32(source, 48);
  const std::uint32_t ledger_size = readU32(source, 52);
  const std::size_t expected_calibration_size =
      serializeCalibration(CalibrationResult{}).size();
  const std::size_t expected_ledger_size =
      serializeIterationLedger(IterationLedger{}).size();
  const std::uint64_t maximum_replay_bytes =
      kReplayHeaderBytes +
      kMaximumFinalReplayRoots * kSerializedExampleBytes;
  if (model_size != kFloat32CheckpointBytes ||
      replay_size > maximum_replay_bytes ||
      calibration_size != expected_calibration_size ||
      ledger_size != expected_ledger_size) {
    throw std::runtime_error("iteration resume component size mismatch");
  }
  const std::uint64_t expected_size =
      kResumeHeaderBytes + model_size +
      8u * static_cast<std::uint64_t>(kParameterCount) + replay_size +
      calibration_size + ledger_size;
  if (expected_size != source.size()) {
    throw std::runtime_error("iteration resume length mismatch");
  }
  std::size_t cursor = kResumeHeaderBytes;
  const auto take = [&](std::uint64_t count) {
    if (count > std::numeric_limits<std::size_t>::max()) {
      throw std::runtime_error("iteration resume field overflow");
    }
    const std::size_t size = static_cast<std::size_t>(count);
    requireBytes(source, cursor, size, "iteration resume field");
    std::vector<std::uint8_t> value(
        source.begin() + static_cast<std::ptrdiff_t>(cursor),
        source.begin() + static_cast<std::ptrdiff_t>(cursor + size));
    cursor += size;
    return value;
  };
  IterationResumeState state;
  state.next_round = static_cast<int>(readU32(source, 12));
  state.model = deserializeCheckpoint(take(model_size));
  std::vector<float> first(kParameterCount);
  std::vector<float> second(kParameterCount);
  for (float& value : first) {
    value = readFloat(source, cursor);
    cursor += 4;
  }
  for (float& value : second) {
    value = readFloat(source, cursor);
    cursor += 4;
  }
  state.optimizer.restoreState(std::move(first), std::move(second),
                               readU32(source, 44), readDouble(source, 104),
                               readDouble(source, 112));
  state.replay = deserializeReplay(take(replay_size));
  state.previous_calibration =
      deserializeCalibration(take(calibration_size));
  state.ledger =
      deserializeIterationLedger(take(ledger_size), calibration_size);
  if (completed) {
    state.completion_capability_ = IterationCompletionCapability(
        readU64(source, 72), readU64(source, 80), readU64(source, 88),
        readU64(source, 96));
  }
  if (cursor != source.size()) {
    throw std::runtime_error("iteration resume had trailing bytes");
  }
  validateResumeState(state);
  return state;
}

IterationLedger frozenScheduleLedger(std::size_t initial_replay_size,
                                     bool final_calibration_passes = true) {
  IterationLedger result;
  result.d4_pretraining_epochs = kD4PretrainingEpochs;
  result.d4_pretraining_updates = static_cast<int>(
      ((initial_replay_size + kOptimizerBatchSize - 1) /
       kOptimizerBatchSize) *
      kD4PretrainingEpochs);
  std::size_t replay_size = initial_replay_size;
  for (int round = 0; round < kRounds; ++round) {
    replay_size += kOnPolicyRootsPerRound;
    RoundLedger ledger;
    ledger.round = round + 1;
    ledger.new_roots = kOnPolicyRootsPerRound;
    ledger.reanalysed_roots = kReanalysisRootsPerRound;
    ledger.optimizer_epochs = kOptimizerEpochsPerRound;
    ledger.replay_size_after = replay_size;
    ledger.checkpoint_exported =
        final_calibration_passes && round + 1 == kRounds;
    result.rounds[round] = ledger;
    result.total_new_roots += kOnPolicyRootsPerRound;
    result.total_reanalysed_roots += kReanalysisRootsPerRound;
  }
  result.checkpoint_exports = final_calibration_passes ? 1 : 0;
  result.deployed_round = final_calibration_passes ? 8 : 0;
  return result;
}

struct IterationResult {
  Model model{};
  ReplayBuffer replay{};
  IterationLedger ledger{};
  CalibrationResult final_calibration{};
  bool deployment_qualified = false;
  std::vector<std::uint8_t> round8_checkpoint;
};

IterationResumeState initializeExpertIteration(ReplayBuffer replay) {
  if (replay.empty()) {
    throw std::invalid_argument("expert iteration requires D4 bootstrap replay");
  }
  IterationResumeState state;
  state.model = Model::initialized();
  state.ledger.d4_pretraining_updates = optimizeEpochs(
      state.model, replay, state.optimizer, kD4PretrainingEpochs, 0u);
  state.ledger.d4_pretraining_epochs = kD4PretrainingEpochs;
  state.replay = std::move(replay);
  validateResumeState(state);
  return state;
}

void advanceExpertIterationRound(IterationResumeState& state) {
  validateResumeState(state);
  if (state.next_round >= kRounds) {
    throw std::invalid_argument("expert iteration already completed");
  }
  const int round = state.next_round;
  const ConstraintTrust search_trust =
      trustForRound(round, state.previous_calibration);
  std::vector<TrainingExample> fresh =
      collectOnPolicyRound(state.model, round, search_trust);
  if (static_cast<int>(fresh.size()) != kOnPolicyRootsPerRound) {
    throw std::runtime_error("round new-root count drifted");
  }
  // Reanalysis is completed against only the durable prior-round replay.  If
  // interrupted before the next boundary, replaying this round therefore
  // reproduces the same roots, targets, Adam updates, and calibration bytes.
  std::vector<TrainingExample> reanalysed = reanalyseReplay(
      state.replay, state.model, round, kReanalysisRootsPerRound,
      search_trust);
  if (static_cast<int>(reanalysed.size()) != kReanalysisRootsPerRound) {
    throw std::runtime_error("round reanalysis count drifted");
  }
  CalibrationSplit split = splitFreshForCalibration(fresh, round);
  ReplayBuffer training = std::move(split.training);
  training.append(std::move(reanalysed));
  const int optimizer_updates = optimizeEpochs(
      state.model, training, state.optimizer, kOptimizerEpochsPerRound,
      static_cast<std::uint32_t>(round + 1));
  const CalibrationResult calibration =
      calibrateHeldout(state.model, split.heldout);
  state.replay.append(std::move(fresh));

  RoundLedger ledger;
  ledger.round = round + 1;
  ledger.new_roots = kOnPolicyRootsPerRound;
  ledger.reanalysed_roots = kReanalysisRootsPerRound;
  ledger.training_examples = static_cast<int>(training.size());
  ledger.optimizer_epochs = kOptimizerEpochsPerRound;
  ledger.optimizer_updates = optimizer_updates;
  ledger.search_trust = search_trust;
  ledger.calibration = calibration;
  ledger.replay_size_after = state.replay.size();
  state.ledger.rounds[round] = ledger;
  state.ledger.total_new_roots += kOnPolicyRootsPerRound;
  state.ledger.total_reanalysed_roots += kReanalysisRootsPerRound;
  state.previous_calibration = calibration;
  ++state.next_round;
  if (state.next_round == kRounds &&
      state.previous_calibration.trust().all()) {
    const auto fingerprints = completionFingerprints(state);
    state.completion_capability_ = IterationCompletionCapability(
        fingerprints[0], fingerprints[1], fingerprints[2], fingerprints[3]);
  }
  validateResumeState(state);
}

std::optional<DeploymentCertificate> certifyCompletedIteration(
    const IterationResumeState& state) {
  validateResumeState(state);
  if (state.next_round != kRounds ||
      !state.previous_calibration.trust().all() ||
      !state.completion_capability_) {
    return std::nullopt;
  }
  const auto fingerprints = completionFingerprints(state);
  const std::uint64_t model_fingerprint = fingerprints[0];
  const std::uint64_t replay_fingerprint = fingerprints[1];
  const std::uint64_t calibration_fingerprint = fingerprints[2];
  const std::uint64_t ledger_fingerprint = fingerprints[3];
  if (model_fingerprint == 0u || replay_fingerprint == 0u ||
      calibration_fingerprint == 0u || ledger_fingerprint == 0u) {
    throw std::runtime_error("deployment certificate fingerprint was zero");
  }
  return DeploymentCertificate(model_fingerprint, replay_fingerprint,
                               calibration_fingerprint, ledger_fingerprint);
}

IterationResult finalizeExpertIteration(IterationResumeState state) {
  validateResumeState(state);
  if (state.next_round != kRounds) {
    throw std::invalid_argument("cannot finalize an incomplete iteration");
  }
  IterationResult result;
  const std::optional<DeploymentCertificate> certificate =
      certifyCompletedIteration(state);
  result.deployment_qualified = certificate.has_value();
  if (certificate) {
    result.round8_checkpoint =
        serializeDeploymentCheckpoint(state.model, *certificate);
    result.ledger = state.ledger;
    result.ledger.rounds[kRounds - 1].checkpoint_exported = true;
    result.ledger.checkpoint_exports = 1;
    result.ledger.deployed_round = kRounds;
  } else {
    result.ledger = state.ledger;
  }
  result.final_calibration = state.previous_calibration;
  result.model = std::move(state.model);
  result.replay = std::move(state.replay);
  if (result.deployment_qualified != !result.round8_checkpoint.empty() ||
      result.ledger.checkpoint_exports !=
          (result.deployment_qualified ? 1 : 0) ||
      result.ledger.deployed_round !=
          (result.deployment_qualified ? kRounds : 0)) {
    throw std::runtime_error("conditional round-8 deployment invariant failed");
  }
  return result;
}

using ResumeBoundaryCallback =
    std::function<void(const IterationResumeState& state)>;

IterationResult runExpertIteration(
    IterationResumeState state,
    const ResumeBoundaryCallback& boundary_callback = {}) {
  validateResumeState(state);
  while (state.next_round < kRounds) {
    advanceExpertIterationRound(state);
    if (boundary_callback) boundary_callback(state);
  }
  return finalizeExpertIteration(std::move(state));
}

IterationResult runExpertIteration(
    ReplayBuffer replay,
    const ResumeBoundaryCallback& boundary_callback = {}) {
  IterationResumeState state = initializeExpertIteration(std::move(replay));
  if (boundary_callback) boundary_callback(state);
  return runExpertIteration(std::move(state), boundary_callback);
}

CapturedGame runCandidateGame(const AuthorizedSeed& authorization,
                              const Model& model) {
  if (authorization.purpose() == FreshPurpose::kD4Initialization ||
      authorization.purpose() == FreshPurpose::kExpertGame) {
    throw std::invalid_argument("candidate gate used wrong fresh lane");
  }
  State state = initialHeadlessState(authorization.value());
  CapturedGame result;
  result.seed = authorization.value();
  while (!state.game_over && state.moves_played < kMaximumGameMoves) {
    const SearchResult search = deploymentSearchRoot(
        publicState(state), model, kDeploymentMaximumPly);
    if (!isLegal(state.board, search.action)) {
      throw std::runtime_error("candidate gate chose illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, authorization.value(), search.action, move)) {
      throw std::runtime_error("candidate gate transition failed");
    }
    const TrajectoryStep step = trajectoryStep(move);
    result.cleared += static_cast<std::uint64_t>(step.cleared);
    result.revealed += static_cast<std::uint64_t>(step.revealed);
    result.steps.push_back(step);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.natural = state.game_over;
  return result;
}

double meanOf(const std::vector<double>& values) {
  if (values.empty()) throw std::invalid_argument("mean of empty values");
  return std::accumulate(values.begin(), values.end(), 0.0) /
         static_cast<double>(values.size());
}

double lowerQuartile(std::vector<double> values) {
  if (values.empty()) throw std::invalid_argument("quartile of empty values");
  std::sort(values.begin(), values.end());
  const double position = 0.25 * static_cast<double>(values.size() - 1);
  const std::size_t lower = static_cast<std::size_t>(std::floor(position));
  const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
  const double fraction = position - static_cast<double>(lower);
  return values[lower] * (1.0 - fraction) + values[upper] * fraction;
}

double bootstrapLower95(const std::vector<double>& values) {
  if (values.empty()) throw std::invalid_argument("bootstrap of empty values");
  std::vector<double> means;
  means.reserve(kBootstrapReplicates);
  for (int replicate = 0; replicate < kBootstrapReplicates; ++replicate) {
    double sum = 0.0;
    for (std::size_t item = 0; item < values.size(); ++item) {
      const std::uint32_t bits = mix32(
          kReplayDomain ^
          (static_cast<std::uint32_t>(replicate + 1) * 0x9e37'79b9u) ^
          (static_cast<std::uint32_t>(item + 1) * 0x85eb'ca6bu));
      sum += values[bits % values.size()];
    }
    means.push_back(sum / static_cast<double>(values.size()));
  }
  std::sort(means.begin(), means.end());
  return means[static_cast<std::size_t>(
      std::floor(0.025 * static_cast<double>(means.size() - 1)))];
}

struct GateMetrics {
  int games = 0;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double bottom_quartile_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double bootstrap_lower95_score = 0.0;
  int natural_games = 0;
};

GateMetrics summarizeGames(const std::vector<CapturedGame>& games) {
  if (games.empty()) throw std::invalid_argument("empty gate cohort");
  std::vector<double> scores;
  std::vector<double> moves;
  double clears = 0.0;
  double reveals = 0.0;
  double total_moves = 0.0;
  int natural = 0;
  for (const CapturedGame& game : games) {
    scores.push_back(static_cast<double>(game.score));
    moves.push_back(static_cast<double>(game.moves));
    clears += static_cast<double>(game.cleared);
    reveals += static_cast<double>(game.revealed);
    total_moves += game.moves;
    natural += game.natural;
  }
  return {static_cast<int>(games.size()),
          meanOf(scores),
          meanOf(moves),
          lowerQuartile(moves),
          total_moves > 0.0 ? clears / total_moves : 0.0,
          total_moves > 0.0 ? reveals / total_moves : 0.0,
          bootstrapLower95(scores),
          natural};
}

struct GateResult {
  FreshPurpose purpose = FreshPurpose::kStageA;
  GateMetrics candidate{};
  GateMetrics d4{};
  int joint_wins = 0;
  bool baseline_attempted = false;
  bool passed = false;
};

bool passesAbsoluteGate(FreshPurpose purpose, const GateMetrics& candidate) {
  if (candidate.natural_games != candidate.games) return false;
  if (purpose == FreshPurpose::kStageA ||
      purpose == FreshPurpose::kStageB) {
    const StageGate gate = purpose == FreshPurpose::kStageA ? kStageAGate
                                                            : kStageBGate;
    return candidate.games == gate.games &&
           candidate.mean_score >= gate.minimum_score &&
           candidate.mean_moves >= gate.minimum_moves &&
           candidate.bottom_quartile_moves >=
               gate.minimum_bottom_quartile_moves &&
           candidate.clears_per_move >= gate.minimum_clears_per_move &&
           candidate.reveals_per_move >= gate.minimum_reveals_per_move;
  }
  if (purpose == FreshPurpose::kDevelopmentConfirmation) {
    return candidate.games == kStageCGate.games &&
           candidate.mean_score > kStageCGate.minimum_mean_score &&
           candidate.bootstrap_lower95_score >
               kStageCGate.minimum_bootstrap_lower95_score &&
           candidate.mean_moves > kStageCGate.minimum_mean_moves &&
           candidate.clears_per_move >=
               kStageCGate.minimum_clears_per_move &&
           candidate.reveals_per_move >=
               kStageCGate.minimum_reveals_per_move;
  }
  return false;
}

bool shouldAttemptBaseline(FreshPurpose purpose,
                           const GateMetrics& candidate) {
  return (purpose == FreshPurpose::kStageA ||
          purpose == FreshPurpose::kStageB) &&
         passesAbsoluteGate(purpose, candidate);
}

bool passesGate(FreshPurpose purpose, const GateMetrics& candidate,
                const GateMetrics& d4, int joint_wins) {
  if (!passesAbsoluteGate(purpose, candidate)) return false;
  if (purpose == FreshPurpose::kStageA ||
      purpose == FreshPurpose::kStageB) {
    const StageGate gate = purpose == FreshPurpose::kStageA ? kStageAGate
                                                            : kStageBGate;
    return d4.mean_score > 0.0 && d4.mean_moves > 0.0 &&
           candidate.mean_score / d4.mean_score >=
               gate.minimum_score_ratio_vs_d4 &&
           candidate.mean_moves / d4.mean_moves >=
               gate.minimum_move_ratio_vs_d4 &&
           joint_wins >= gate.minimum_joint_wins;
  }
  if (purpose == FreshPurpose::kDevelopmentConfirmation) {
    return true;
  }
  return false;
}

GateResult runGate(FreshPurpose purpose, const Model& model) {
  if (purpose != FreshPurpose::kStageA && purpose != FreshPurpose::kStageB &&
      purpose != FreshPurpose::kDevelopmentConfirmation) {
    throw std::invalid_argument("requested lane is not a gameplay gate");
  }
  const SeedLane lane = laneFor(purpose);
  const int games = static_cast<int>(lane.last - lane.first + 1u);
  requireExactLane(purpose, lane.first, games);
  std::vector<CapturedGame> candidate(games);
  std::atomic<int> next{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < kWorkers; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next.fetch_add(1);
        if (game >= games) return;
        const AuthorizedSeed seed = AuthorizedSeed::checked(
            purpose, lane.first + static_cast<std::uint32_t>(game));
        candidate[game] = runCandidateGame(seed, model);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  GateResult result;
  result.purpose = purpose;
  result.candidate = summarizeGames(candidate);
  // Absolute candidate floors are evaluated before a baseline runner is even
  // constructed.  A candidate below the gate therefore cannot start an exact-D4
  // baseline phase on the same seed cohort.
  if (!passesAbsoluteGate(purpose, result.candidate)) return result;
  if (purpose == FreshPurpose::kDevelopmentConfirmation) {
    result.passed = true;
    return result;
  }

  if (!shouldAttemptBaseline(purpose, result.candidate)) {
    throw std::runtime_error("candidate-first gate phase ordering drifted");
  }
  result.baseline_attempted = true;
  std::vector<CapturedGame> d4(games);
  next.store(0);
  workers.clear();
  for (int worker = 0; worker < kWorkers; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next.fetch_add(1);
        if (game >= games) return;
        const AuthorizedSeed seed = AuthorizedSeed::checked(
            purpose, lane.first + static_cast<std::uint32_t>(game));
        d4[game] = runExactD4Game(seed, false);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.d4 = summarizeGames(d4);
  for (int game = 0; game < games; ++game) {
    result.joint_wins += candidate[game].score > d4[game].score &&
                         candidate[game].moves > d4[game].moves;
  }
  result.passed =
      passesGate(purpose, result.candidate, result.d4, result.joint_wins);
  return result;
}

struct GateSequenceResult {
  std::vector<GateResult> attempted;
  bool passed_all = false;
};

GateSequenceResult runGateSequence(
    const std::vector<std::uint8_t>& checkpoint) {
  const Model model = deserializeCheckpoint(checkpoint);
  if (!checkpointHasDeploymentCertificate(checkpoint)) {
    throw std::invalid_argument(
        "gameplay gates require a certified round-8 checkpoint");
  }
  GateSequenceResult result;
  for (const FreshPurpose purpose : {FreshPurpose::kStageA,
                                     FreshPurpose::kStageB,
                                     FreshPurpose::kDevelopmentConfirmation}) {
    result.attempted.push_back(runGate(purpose, model));
    if (!result.attempted.back().passed) return result;
  }
  result.passed_all = true;
  return result;
}

int plannedGateAttempts(bool stage_a, bool stage_b, bool stage_c) {
  int attempts = 1;
  if (!stage_a) return attempts;
  ++attempts;
  if (!stage_b) return attempts;
  ++attempts;
  return stage_c ? attempts : attempts;
}

// ---------------------------------------------------------------------------
// Development-corpus loader and resource/performance proof
// ---------------------------------------------------------------------------

int integerAfter(std::string_view line, std::string_view marker) {
  const std::size_t at = line.find(marker);
  if (at == std::string_view::npos) {
    throw std::runtime_error("missing public corpus integer");
  }
  std::size_t cursor = at + marker.size();
  int value = 0;
  bool found = false;
  while (cursor < line.size() && line[cursor] >= '0' && line[cursor] <= '9') {
    found = true;
    value = value * 10 + (line[cursor] - '0');
    ++cursor;
  }
  if (!found) throw std::runtime_error("bad public corpus integer");
  return value;
}

PublicState parseBurnedRoot(std::string_view line) {
  constexpr std::string_view board_marker = "\"board\":\"";
  const std::size_t at = line.find(board_marker);
  if (at == std::string_view::npos ||
      at + board_marker.size() + kCellCount > line.size()) {
    throw std::runtime_error("bad burned root board");
  }
  PublicState result;
  for (int cell = 0; cell < kCellCount; ++cell) {
    const char digit = line[at + board_marker.size() + cell];
    if (digit < '0' || digit > '9') {
      throw std::runtime_error("bad burned root token");
    }
    result.board[cell] = static_cast<std::uint8_t>(digit - '0');
  }
  result.next_disc =
      static_cast<std::uint8_t>(integerAfter(line, "\"nextDisc\":"));
  result.phase =
      static_cast<std::uint8_t>(integerAfter(line, "\"movesRemaining\":"));
  return result;
}

std::vector<PublicState> loadBurnedRoots(const std::string& path, int count) {
  std::error_code error;
  const std::uintmax_t bytes = std::filesystem::file_size(path, error);
  if (error || bytes != kBurnedCorpusBytes) {
    throw std::runtime_error("burned public corpus size/provenance mismatch");
  }
  std::ifstream input(path);
  if (!input) throw std::runtime_error("could not read burned public corpus");
  std::string line;
  if (!std::getline(input, line) ||
      line.find("drop7-public-d4-root-labels-v1") == std::string::npos ||
      line.find("\"excluded\":[\"gameSeed\",\"score\",\"level\",\"moveIndex\",\"history\",\"futureTape\"]") ==
          std::string::npos) {
    throw std::runtime_error("burned corpus public boundary changed");
  }
  std::vector<PublicState> result;
  result.reserve(count);
  while (static_cast<int>(result.size()) < count && std::getline(input, line)) {
    result.push_back(parseBurnedRoot(line));
  }
  if (static_cast<int>(result.size()) != count) {
    throw std::runtime_error("burned corpus did not contain requested roots");
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

constexpr std::uint64_t align64(std::uint64_t bytes) {
  return (bytes + 63u) & ~63u;
}

struct ReplayRecordProof {
  std::array<std::uint8_t, kCellCount> board{};
  std::uint8_t next_disc = 0;
  std::uint8_t phase = 0;
  std::uint8_t terminal = 0;
  std::uint8_t action = 0;
  std::array<float, kBoardSize> policy{};
  std::array<float, kScoreQuantiles> score{};
  std::array<float, kLifetimeQuantiles> lifetime{};
  std::array<float, kRegenerationHeads> regeneration{};
  std::array<float, kFlowHeads> flow{};
  std::uint32_t trajectory_group = 0;
  std::uint8_t calibration_reservation = 0;
};

constexpr std::uint64_t kMaximumRoundScratchRoots =
    2u * kOnPolicyRootsPerRound + kReanalysisRootsPerRound;
constexpr std::uint64_t kReplayBytes =
    align64(sizeof(TrainingExample)) *
    (kMaximumFinalReplayRoots + kMaximumRoundScratchRoots);
constexpr std::uint64_t kOptimizerBytes =
    static_cast<std::uint64_t>(kParameterCount) * sizeof(double) * 8u;
constexpr std::uint64_t kMaximumResumeStateBytes =
    kResumeHeaderBytes + kFloat32CheckpointBytes +
    8u * static_cast<std::uint64_t>(kParameterCount) + kReplayHeaderBytes +
    kMaximumFinalReplayRoots * kSerializedExampleBytes + 16u * 1024u;
constexpr std::uint64_t kWorkerScratchBytes = 4u * 1024u * 1024u;
constexpr std::uint64_t kProjectedResidentBytes =
    kReplayBytes + kOptimizerBytes + kBrowserArenaBytes +
    kWorkers * kWorkerScratchBytes + kMaximumResumeStateBytes +
    32u * 1024u * 1024u;
constexpr std::uint64_t kMaximumD4CapturedRootBytes =
    align64(sizeof(TrainingExample)) * kMaximumD4BootstrapRoots;
constexpr std::uint64_t kD4InitializationResidentBytes =
    kMaximumD4CapturedRootBytes + kWorkers * 48u * 1024u * 1024u +
    32u * 1024u * 1024u;
constexpr std::uint64_t kProjectedPeakResidentBytes =
    std::max(kProjectedResidentBytes, kD4InitializationResidentBytes);

static_assert(kProjectedPeakResidentBytes < kMaximumRssBytes);

struct PerformanceProjection {
  int benchmark_roots = 0;
  double benchmark_seconds = 0.0;
  double seconds_per_root = 0.0;
  double optimizer_step_seconds = 0.0;
  double projected_search_seconds = 0.0;
  double projected_d4_initialization_seconds = 0.0;
  double projected_optimizer_seconds = 0.0;
  double projected_total_seconds = 0.0;
  std::uint64_t transitions = 0;
  std::uint64_t leaves = 0;
  std::uint64_t nnue_evaluations = 0;
  int maximum_depth = 0;
  std::uint64_t rss_bytes = 0;
  bool admitted = false;
};

constexpr std::uint64_t kProjectedD4TrainingRoots =
    kMaximumD4BootstrapRoots;
constexpr std::uint64_t kProjectedOptimizerBatches =
    ((kProjectedD4TrainingRoots + kOptimizerBatchSize - 1u) /
     kOptimizerBatchSize) *
        kD4PretrainingEpochs +
    static_cast<std::uint64_t>(kRounds) *
        ((kOnPolicyRootsPerRound + kReanalysisRootsPerRound +
          kOptimizerBatchSize - 1u) /
         kOptimizerBatchSize) *
        kOptimizerEpochsPerRound;

PerformanceProjection performancePreflight(const std::string& corpus_path,
                                           int roots) {
  if (roots < 1 || roots > 8) {
    throw std::invalid_argument("preflight roots must be in [1,8]");
  }
  const std::vector<PublicState> burned = loadBurnedRoots(corpus_path, roots);
  const Model model = Model::initialized();
  PerformanceProjection result;
  result.benchmark_roots = roots;
  const auto started = Clock::now();
  for (const PublicState& root : burned) {
    const SearchResult search = searchRoot(root, model);
    if (search.simulations != kSearchSimulations || search.action < 0) {
      throw std::runtime_error("production-shaped search did not complete");
    }
    result.transitions += search.transitions;
    result.leaves += search.nnue_leaves;
    result.nnue_evaluations += search.nnue_evaluations;
    result.maximum_depth =
        std::max(result.maximum_depth, search.maximum_depth);
  }
  result.benchmark_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  result.seconds_per_root = result.benchmark_seconds / roots;
  // Search roots parallelize over eight independent workers.  A 1.50 safety
  // multiplier covers allocator, training, checkpoint, and corpus overhead.
  result.projected_search_seconds =
      result.seconds_per_root * kTotalSearchedRoots / kWorkers * 1.50;
  ReplayBuffer optimizer_fixture;
  SearchResult expert;
  expert.action = centerFirstMove(burned.front().board);
  expert.visits[expert.action] = kSearchSimulations;
  expert.simulations = kSearchSimulations;
  TrainingExample optimizer_example;
  optimizer_example.state = burned.front();
  optimizer_example.target = makeExpertTarget(
      expert, expert.action, 20'000.0, 40.0, AuxiliaryTargets{});
  optimizer_fixture.push(std::move(optimizer_example));
  Model optimizer_model = model;
  DeterministicOptimizer optimizer;
  const auto optimizer_started = Clock::now();
  optimizer.step(optimizer_model, optimizer_fixture, 0u, 0u);
  result.optimizer_step_seconds =
      std::chrono::duration<double>(Clock::now() - optimizer_started).count();
  result.projected_optimizer_seconds =
      result.optimizer_step_seconds * kProjectedOptimizerBatches * 1.50;
  // Use 1.226816922 seconds per D4 decision as the conservative initialization
  // cost.  Projection uses the enforced 2,000-move cap for every initializer
  // game rather than an average game length.  Preflight does not invoke the
  // guarded gameplay runner.
  constexpr double burned_d4_seconds_per_move = 1.226816922;
  constexpr int projected_d4_moves_per_game = kMaximumGameMoves;
  result.projected_d4_initialization_seconds =
      burned_d4_seconds_per_move * projected_d4_moves_per_game * 64.0 /
      kWorkers * 1.25;
  result.projected_total_seconds = result.projected_search_seconds +
                                   result.projected_d4_initialization_seconds +
                                   result.projected_optimizer_seconds;
  result.rss_bytes = peakRssBytes();
  result.admitted = result.projected_total_seconds <= kMaximumWallSeconds &&
                    result.rss_bytes <= kMaximumRssBytes &&
                    kProjectedPeakResidentBytes <= kMaximumRssBytes;
  return result;
}

// ---------------------------------------------------------------------------
// Fixtures and commands
// ---------------------------------------------------------------------------

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

bool equalMove(const MoveResult& left, const MoveResult& right) {
  if (left.state.board != right.state.board ||
      left.state.next_disc != right.state.next_disc ||
      left.state.score != right.state.score ||
      left.state.level != right.state.level ||
      left.state.moves_remaining != right.state.moves_remaining ||
      left.state.moves_played != right.state.moves_played ||
      left.state.game_over != right.state.game_over ||
      left.score_delta != right.score_delta ||
      left.cleared_board != right.cleared_board ||
      left.level_advanced != right.level_advanced ||
      left.waves.size() != right.waves.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.waves.size(); ++index) {
    const Wave& first = left.waves[index];
    const Wave& second = right.waves[index];
    if (first.depth != second.depth || first.cleared != second.cleared ||
        first.revealed != second.revealed || first.points != second.points) {
      return false;
    }
  }
  return true;
}

PublicState fixtureState() {
  PublicState result;
  constexpr std::array<std::string_view, kBoardSize> rows{{
      "0000000", "0000000", "0000000", "0000000", "0020400",
      "0315260", "8898898",
  }};
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      result.board[indexOf(row, column)] =
          static_cast<std::uint8_t>(rows[row][column] - '0');
    }
  }
  result.next_disc = 6;
  result.phase = 3;
  return result;
}

struct SelfTestResult {
  std::uint64_t checkpoint_hash = 0;
  std::uint64_t checkpoint_bytes = 0;
  std::uint64_t peak_rss_bytes = 0;
  int gradient_checks = 0;
  double maximum_gradient_relative_error = 0.0;
  std::uint64_t trained_checkpoint_hash = 0;
};

SelfTestResult runSelfTests(const std::string& checkpoint_path) {
  // Exact engine parity includes a row rise, reveals, next-disc consumption,
  // scoring, and wave metadata.  The two RNGs start from the same state.
  State engine_state = materialize(fixtureState());
  engine_state.score = 1234;
  engine_state.level = 9;
  engine_state.moves_played = 42;
  engine_state.moves_remaining = 1;
  Mulberry32 reference_random(0x6f52'a91du);
  Mulberry32 generic_random(0x6f52'a91du);
  MoveResult reference_move;
  MoveResult generic_move;
  const int parity_action = centerFirstMove(engine_state.board);
  expect(playMove(engine_state, parity_action, reference_random,
                  reference_move) &&
             playMoveGeneric(engine_state, parity_action, generic_random,
                             generic_move) &&
             equalMove(reference_move, generic_move),
         "generic transition diverged from exact engine");

  const PublicState fixture = fixtureState();
  expect(mirror(mirror(fixture)) == fixture &&
             publicHash(fixture) == publicHash(mirror(fixture)),
         "canonical public reflection failed");

  PublicState symmetric_fixture;
  constexpr std::array<std::string_view, kBoardSize> symmetric_rows{{
      "0000000", "0100010", "0600060", "9180819", "2273722",
      "7632367", "4783874",
  }};
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      symmetric_fixture.board[indexOf(row, column)] =
          static_cast<std::uint8_t>(symmetric_rows[row][column] - '0');
    }
  }
  symmetric_fixture.next_disc = 4;
  symmetric_fixture.phase = 3;
  expect(mirror(symmetric_fixture) == symmetric_fixture,
         "symmetric chance fixture was not reflection-fixed");

  State metadata = materialize(fixture);
  metadata.score = 9'999'999;
  metadata.level = 777;
  metadata.moves_played = 888;
  expect(publicState(metadata) == fixture,
         "private score/level/move metadata crossed public boundary");

  // Reproduce the actual search call path: both local_visit and its derived
  // stratum change together.  Each stable sequential or coordinate event in
  // every consecutive seven-visit pack must enumerate discs 1..7 exactly.
  for (int depth = 0; depth < kSearchDepthMoves; ++depth) {
    for (std::uint32_t pack = 0; pack < 4; ++pack) {
      for (std::uint32_t event = 0; event < 16; ++event) {
        std::array<int, kChanceStrata> sequential_counts{};
        std::array<int, kChanceStrata> coordinate_counts{};
        for (std::uint32_t offset = 0; offset < kChanceStrata; ++offset) {
          const std::uint32_t local_visit =
              pack * static_cast<std::uint32_t>(kChanceStrata) + offset;
          const int stratum = chanceStratum(local_visit, depth);
          ChancePackRandom sequential(fixture, 0, depth, local_visit,
                                      stratum, event);
          ChancePackRandom coordinate(fixture, 0, depth, local_visit,
                                      stratum, event);
          ++sequential_counts[sequential.nextDisc() - 1];
          ++coordinate_counts[coordinate.nextDiscFor(4, 2, 3) - 1];
        }
        expect(std::all_of(sequential_counts.begin(), sequential_counts.end(),
                           [](int count) { return count == 1; }) &&
                   std::all_of(coordinate_counts.begin(),
                               coordinate_counts.end(),
                               [](int count) { return count == 1; }),
               "actual search chance pack was not exactly seven-stratified");
      }
    }
  }

  // Exercise the complete transition rather than only the RNG adapter.  The
  // visible next disc is a stable event shared across actions at the root.
  for (int depth = 0; depth < 5; ++depth) {
    for (int action = 0; action < kBoardSize; ++action) {
      if (!isLegal(fixture.board, action)) continue;
      std::array<int, kChanceStrata> next_disc_counts{};
      for (std::uint32_t local_visit = 0; local_visit < kChanceStrata;
           ++local_visit) {
        const PublicTransition transition = chanceTransition(
            fixture, action, depth, local_visit,
            chanceStratum(local_visit, depth));
        expect(!transition.state.terminal,
               "chance-pack transition fixture unexpectedly terminated");
        ++next_disc_counts[transition.state.next_disc - 1];
      }
      expect(std::all_of(next_disc_counts.begin(), next_disc_counts.end(),
                         [](int count) { return count == 1; }),
             "actual transition next disc was not exactly seven-stratified");
    }
  }

  // On a reflection-fixed board, the side action breaks the orientation tie:
  // action 0's event at c is action 6's event at 6-c.  A center action keeps
  // the two coordinate identities distinct so independent hidden discs are
  // not forced equal by the seven-sample Latin design.
  bool symmetric_center_kept_distinct_events = false;
  for (int depth = 0; depth < 5; ++depth) {
    for (std::uint32_t local_visit = 0; local_visit < kChanceStrata;
         ++local_visit) {
      const int stratum = chanceStratum(local_visit, depth);
      ChancePackRandom left_random(symmetric_fixture, 0, depth, local_visit,
                                   stratum);
      ChancePackRandom right_random(symmetric_fixture, kBoardSize - 1, depth,
                                    local_visit, stratum);
      ChancePackRandom center_random(symmetric_fixture, kBoardSize / 2, depth,
                                     local_visit, stratum);
      for (int row = 0; row < kBoardSize; ++row) {
        for (int column = 0; column < kBoardSize; ++column) {
          const int reflected_column = kBoardSize - 1 - column;
          expect(left_random.nextDiscFor(row, column, 2) ==
                     right_random.nextDiscFor(row, reflected_column, 2),
                 "symmetric side-action reveal fields did not reflect");
          symmetric_center_kept_distinct_events |=
              center_random.nextDiscFor(row, column, 2) !=
              center_random.nextDiscFor(row, reflected_column, 2);
        }
      }
      for (int action = 0; action < kBoardSize; ++action) {
        if (action == kBoardSize / 2) continue;
        if (!isLegal(symmetric_fixture.board, action)) continue;
        const PublicTransition direct = chanceTransition(
            symmetric_fixture, action, depth, local_visit, stratum);
        const PublicTransition reflected = chanceTransition(
            symmetric_fixture, kBoardSize - 1 - action, depth, local_visit,
            stratum);
        expect(direct.state == mirror(reflected.state) &&
                   direct.score_delta == reflected.score_delta &&
                   direct.cleared == reflected.cleared &&
                   direct.revealed == reflected.revealed &&
                   direct.level_advanced == reflected.level_advanced,
               "reflection-fixed chance transition diverged");
      }
    }
  }
  expect(symmetric_center_kept_distinct_events,
         "symmetric center action collapsed distinct reveal coordinates");
  ChancePackRandom pack_a(fixture, 0, 4, 11u, 3);
  ChancePackRandom pack_b(fixture, 0, 4, 18u, 3);
  ChancePackRandom pack_reflected(mirror(fixture), kBoardSize - 1, 4, 11u,
                                  3);
  std::array<std::uint8_t, 12> values_a{};
  std::array<std::uint8_t, 12> values_b{};
  std::array<std::uint8_t, 12> values_reflected{};
  for (std::size_t index = 0; index < values_a.size(); ++index) {
    values_a[index] = pack_a.nextDisc();
    values_b[index] = pack_b.nextDisc();
    values_reflected[index] = pack_reflected.nextDisc();
  }
  expect(values_a != values_b && values_a == values_reflected,
         "chance packs were fixed or reflection-inconsistent");
  for (int action = 0; action < kBoardSize; ++action) {
    if (!isLegal(fixture.board, action)) continue;
    for (std::uint32_t local_visit = 0; local_visit < kChanceStrata;
         ++local_visit) {
      const int stratum = chanceStratum(local_visit, 3);
      const PublicTransition direct =
          chanceTransition(fixture, action, 3, local_visit, stratum);
      const PublicTransition reflected = chanceTransition(
          mirror(fixture), kBoardSize - 1 - action, 3, local_visit, stratum);
      expect(direct.state == mirror(reflected.state) &&
                 direct.score_delta == reflected.score_delta &&
                 direct.cleared == reflected.cleared &&
                 direct.revealed == reflected.revealed &&
                 direct.level_advanced == reflected.level_advanced,
             "chance transition did not reflect exactly");
    }
  }
  expect(kRootPackDomain != kChanceEventDomain &&
             kRootPackDomain != kPolicySampleDomain &&
             kRootPackDomain != kReplayDomain &&
             kRootPackDomain != kTrainingShuffleDomain &&
             kChanceEventDomain != kPolicySampleDomain &&
             kChanceEventDomain != kReplayDomain &&
             kChanceEventDomain != kTrainingShuffleDomain &&
             kPolicySampleDomain != kReplayDomain &&
             kPolicySampleDomain != kTrainingShuffleDomain &&
             kReplayDomain != kTrainingShuffleDomain &&
             kCalibrationDomain != kRootPackDomain &&
             kCalibrationDomain != kChanceEventDomain &&
             kCalibrationDomain != kPolicySampleDomain &&
             kCalibrationDomain != kReplayDomain &&
             kCalibrationDomain != kTrainingShuffleDomain,
         "stochastic seed domains collided");

  const Model model = Model::initialized();
  const Prediction prediction = model.predict(fixture);
  const Prediction reflected_prediction = model.predict(mirror(fixture));
  for (int action = 0; action < kBoardSize; ++action) {
    const CandidatePrediction& direct = prediction.candidate[action];
    const CandidatePrediction& reverse =
        reflected_prediction.candidate[kBoardSize - 1 - action];
    expect(prediction.legal[action] ==
                   reflected_prediction.legal[kBoardSize - 1 - action] &&
               direct.policy_logit == reverse.policy_logit &&
               direct.score == reverse.score &&
               direct.lifetime == reverse.lifetime &&
               direct.regeneration == reverse.regeneration &&
               direct.flow == reverse.flow,
           "NNUE was not exactly reflection equivariant");
    if (prediction.legal[action]) {
      expect(bitwiseEqual(direct, cachedPrediction(model, fixture, action)),
             "forward/cache prediction lost Float32 bit parity");
    }
  }

  // This exact branch point distinguishes ordered Float32 additions from a
  // seemingly equivalent regrouping.  Backprop must cache the identical
  // positive activation that Model::predict consumed.
  PublicState arithmetic_state;
  arithmetic_state.next_disc = 1;
  arithmetic_state.phase = 1;
  Model arithmetic_model;
  arithmetic_model.weights()[ModelLayout::kStateBias] = 1.0f;
  arithmetic_model.weights()[ModelLayout::kStateNext] = -1.0f;
  arithmetic_model.weights()[ModelLayout::kStatePhase] =
      std::ldexp(1.0f, -25);
  arithmetic_model.weights()[ModelLayout::kFusion] = 1.0f;
  arithmetic_model.weights()[ModelLayout::kPolicy] = 1.0f;
  const OrientationCache arithmetic_cache =
      orientationCache(arithmetic_model, arithmetic_state, 0);
  const float ordered =
      (1.0f + -1.0f) + std::ldexp(1.0f, -25);
  const float regrouped =
      1.0f + (-1.0f + std::ldexp(1.0f, -25));
  expect(floatBits(ordered) != floatBits(regrouped) &&
             floatBits(arithmetic_cache.state_pre[0]) ==
                 floatBits(ordered) &&
             arithmetic_cache.state_pre[0] > 0.0f &&
             bitwiseEqual(arithmetic_model.predict(arithmetic_state)
                              .candidate[0],
                          cachedPrediction(arithmetic_model,
                                           arithmetic_state, 0)),
         "adversarial Float32 cache/branch parity failed");

  const std::vector<double> fusion_fixture{0.0, 10.0, 20.0, 30.0};
  expect(std::abs(lowerCvar(fusion_fixture, 0.25) - 0.0) < 1.0e-12 &&
             std::abs(strategyUtility(fusion_fixture) - 12.0) < 1.0e-12,
         "0.8 mean + 0.2 CVaR25 strategy fusion changed");
  std::array<double, kScoreQuantiles> exact_large{};
  std::array<double, kScoreQuantiles> exact_large_plus_one{};
  exact_large.fill(std::ldexp(1.0, 24));
  exact_large_plus_one.fill(std::ldexp(1.0, 24) + 1.0);
  std::array<double, kScoreQuantiles> beyond_float{};
  beyond_float.fill(static_cast<double>(
                        std::numeric_limits<float>::max()) *
                    kScoreTargetScale);
  std::array<double, kScoreQuantiles> pava_sensitive{};
  pava_sensitive.fill(std::ldexp(1.0, 24) + 10.0);
  pava_sensitive[0] = std::ldexp(1.0, 24) + 1.0;
  pava_sensitive[1] = std::ldexp(1.0, 24);
  const auto pava_sensitive_result =
      isotonicNondecreasing(pava_sensitive);
  expect(predictedScoreUtility(exact_large_plus_one) -
                 predictedScoreUtility(exact_large) ==
             1.0 &&
             pava_sensitive_result[0] ==
                 std::ldexp(1.0, 24) + 0.5 &&
             pava_sensitive_result[1] == pava_sensitive_result[0] &&
             std::isfinite(predictedScoreUtility(beyond_float)),
         "decoded score utility narrowed a finite double to Float32");

  const std::vector<double> quantile_fixture{-1.0, 0.0, 1.0, 2.0};
  const double quantile_loss = quantileHuberLoss(quantile_fixture, 1.0);
  expect(std::abs(quantile_loss - 0.109375) < 1.0e-12,
         "quantile Huber fixture changed");
  expect(std::abs(binaryCrossEntropyFromLogit(0.0, true) -
                  std::log(2.0)) < 1.0e-12,
         "regeneration BCE fixture changed");

  // The root has 12 occupied cells and seven covers.  Cycle one is worse;
  // cycle two returns to <= both root counts and is therefore the first
  // regeneration time.  Later improvements do not rewrite that time.
  PublicState regen_root;
  regen_root.phase = kMovesPerLevel;
  regen_root.next_disc = 4;
  for (int column = 0; column < kBoardSize; ++column) {
    regen_root.board[indexOf(6, column)] = kSolid;
  }
  for (int column = 0; column < 5; ++column) {
    regen_root.board[indexOf(5, column)] =
        static_cast<std::uint8_t>(column + 1);
  }
  std::vector<TrajectoryStep> trajectory(40);
  for (int move = 0; move < 40; ++move) {
    trajectory[move].state = regen_root;
    trajectory[move].state.phase =
        static_cast<std::uint8_t>(kMovesPerLevel - (move + 1) % 5);
    if (trajectory[move].state.phase == 0) {
      trajectory[move].state.phase = kMovesPerLevel;
    }
    trajectory[move].cleared = 2;
    trajectory[move].revealed = 1;
    if (move < 5) {
      trajectory[move].state.board[indexOf(4, 0)] = 6;
    } else if (move == 9) {
      trajectory[move].state.board[indexOf(5, 4)] = kEmpty;
    }
  }
  const AuxiliaryTargets auxiliary = auxiliaryTargets(regen_root, trajectory);
  expect(auxiliary.regeneration_cycle == 2 &&
             auxiliary.regenerated_by ==
                 std::array<bool, 4>{{false, true, true, true}} &&
             auxiliary.flow ==
                 std::array<float, 8>{{10, 5, 20, 10, 40, 20, 80, 40}},
         "multi-cycle regeneration/flow target changed");

  const std::array<double, 3> crossed{{3.0, 1.0, 2.0}};
  expect(isotonicNondecreasing(crossed) ==
             std::array<double, 3>{{2.0, 2.0, 2.0}} &&
             normalizedScoreTarget(1'000'000.0) == 1.0 &&
             normalizedLifetimeTarget(500.0) == 1.0 &&
             normalizedFlowTarget(auxiliary, 2) == 0.25,
         "normalized/PAVA target fixture changed");
  CandidatePrediction monotone_fixture;
  monotone_fixture.regeneration = {{3.0f, -3.0f, 1.0f, -1.0f}};
  monotone_fixture.flow = {{2.0f, 1.5f, 0.1f, 0.2f,
                            0.3f, 0.1f, 0.2f, 0.05f}};
  const auto monotone_regeneration =
      decodedRegeneration(monotone_fixture);
  const auto monotone_flow = decodedCumulativeFlow(monotone_fixture);
  expect(std::is_sorted(monotone_regeneration.begin(),
                        monotone_regeneration.end()) &&
             monotone_flow[0] <= monotone_flow[2] &&
             monotone_flow[2] <= monotone_flow[4] &&
             monotone_flow[4] <= monotone_flow[6] &&
             monotone_flow[1] <= monotone_flow[3] &&
             monotone_flow[3] <= monotone_flow[5] &&
             monotone_flow[5] <= monotone_flow[7],
         "regeneration/cumulative-flow projection crossed");

  const std::vector<std::uint8_t> checkpoint = serializeCheckpoint(model);
  expect(checkpoint.size() == kFloat32CheckpointBytes &&
             checkpoint.size() <= kMaximumCheckpointBytes &&
             fnv1a64(checkpoint) == kGoldenCheckpointFnv1a64,
         "Float32 checkpoint exceeded frozen byte bound");
  const Model restored = deserializeCheckpoint(checkpoint);
  const std::vector<std::uint8_t> roundtrip = serializeCheckpoint(restored);
  expect(checkpoint == roundtrip,
         "checkpoint was not deterministic across golden roundtrip");
  expect(!checkpointHasDeploymentCertificate(checkpoint),
         "ordinary serialization minted a deployment certificate");
  bool forged_certificate_header_rejected = false;
  std::vector<std::uint8_t> forged_certificate = checkpoint;
  forged_certificate[20] = kCheckpointDeploymentCertificateFlag;
  try {
    (void)deserializeCheckpoint(forged_certificate);
  } catch (const std::runtime_error&) {
    forged_certificate_header_rejected = true;
  }
  bool unknown_checkpoint_flag_rejected = false;
  std::vector<std::uint8_t> unknown_flag_checkpoint = checkpoint;
  unknown_flag_checkpoint[20] = 2u;
  try {
    (void)deserializeCheckpoint(unknown_flag_checkpoint);
  } catch (const std::runtime_error&) {
    unknown_checkpoint_flag_rejected = true;
  }
  bool wrong_clip_schema_rejected = false;
  std::vector<std::uint8_t> wrong_clip_checkpoint = checkpoint;
  const float wrong_clip = 5.0f;
  std::uint32_t wrong_clip_bits = 0;
  std::memcpy(&wrong_clip_bits, &wrong_clip, sizeof(wrong_clip_bits));
  for (int byte = 0; byte < 4; ++byte) {
    wrong_clip_checkpoint[112 + byte] = static_cast<std::uint8_t>(
        (wrong_clip_bits >> (8 * byte)) & 0xffu);
  }
  try {
    (void)deserializeCheckpoint(wrong_clip_checkpoint);
  } catch (const std::runtime_error&) {
    wrong_clip_schema_rejected = true;
  }
  bool untrusted_gate_rejected_before_seed = false;
  try {
    (void)runGateSequence(checkpoint);
  } catch (const std::invalid_argument&) {
    untrusted_gate_rejected_before_seed = true;
  }
  expect(forged_certificate_header_rejected &&
             unknown_checkpoint_flag_rejected &&
             wrong_clip_schema_rejected &&
             untrusted_gate_rejected_before_seed,
         "checkpoint gate accepted missing/unknown certificate provenance");
  const Prediction restored_prediction = restored.predict(fixture);
  const Prediction restored_reflected = restored.predict(mirror(fixture));
  for (int action = 0; action < kBoardSize; ++action) {
    expect(restored_prediction.candidate[action].policy_logit ==
               restored_reflected
                   .candidate[kBoardSize - 1 - action]
                   .policy_logit,
           "Float32 checkpoint broke reflection");
  }
  writeBytes(checkpoint_path, checkpoint);

  // Small Gumbel fixture verifies full legal coverage, exact budget, legal
  // output, and deterministic policy/visit targets without starting a game.
  const SearchResult search = searchRoot(fixture, restored, 98);
  const SearchResult repeated = searchRoot(fixture, restored, 98);
  const SearchResult mirrored_search = searchRoot(mirror(fixture), restored, 98);
  bool incomplete_root_pack_rejected = false;
  try {
    (void)searchRoot(fixture, restored, 48);
  } catch (const std::invalid_argument&) {
    incomplete_root_pack_rejected = true;
  }
  int legal_count = 0;
  int visit_total = 0;
  for (int action = 0; action < kBoardSize; ++action) {
    if (isLegal(fixture.board, action)) {
      ++legal_count;
      expect(search.visits[action] >= kChanceStrata,
             "root action was filtered before all seven chance strata");
    }
    visit_total += search.visits[action];
  }
  expect(legal_count == kBoardSize && incomplete_root_pack_rejected &&
             search.action >= 0 && isLegal(fixture.board, search.action) &&
             visit_total == kSearchSimulations &&
             search.maximum_depth == kSearchDepthMoves &&
             search.transitions > static_cast<std::uint64_t>(search.simulations) &&
             search.visits == repeated.visits &&
             search.utility == repeated.utility &&
             search.action == repeated.action &&
             mirrored_search.action == kBoardSize - 1 - search.action &&
             std::equal(search.visits.begin(), search.visits.end(),
                        mirrored_search.visits.rbegin()) &&
             std::equal(search.utility.begin(), search.utility.end(),
                        mirrored_search.utility.rbegin()),
         "Gumbel sequential-halving fixture changed");

  const SearchResult deployment_one =
      deploymentSearchRoot(fixture, restored, 1, {}, 98);
  const SearchResult deployment_eight =
      deploymentSearchRoot(fixture, restored, 8, {}, 98);
  bool deployment_zero_rejected = false;
  bool deployment_nine_rejected = false;
  try {
    (void)deploymentSearchRoot(fixture, restored, 0, {}, 98);
  } catch (const std::invalid_argument&) {
    deployment_zero_rejected = true;
  }
  try {
    (void)deploymentSearchRoot(fixture, restored, 9, {}, 98);
  } catch (const std::invalid_argument&) {
    deployment_nine_rejected = true;
  }
  std::array<int, kChanceStrata> local_strata{};
  for (std::uint32_t local_visit = 0; local_visit < kChanceStrata;
       ++local_visit) {
    ++local_strata[chanceStratum(local_visit, 0)];
  }
  expect(deployment_one.maximum_depth == 1 &&
             deployment_one.transitions ==
                 static_cast<std::uint64_t>(deployment_one.simulations) &&
             deployment_eight.maximum_depth == kDeploymentMaximumPly &&
             deployment_zero_rejected && deployment_nine_rejected &&
             std::all_of(local_strata.begin(), local_strata.end(),
                         [](int count) { return count == 1; }),
         "1..8 deployment mapping or paired local strata drifted");

  const ExpertTarget expert = makeExpertTarget(
      search, search.action, 12'345.0, 37.0, auxiliary);
  const double policy_mass =
      std::accumulate(expert.policy.begin(), expert.policy.end(), 0.0);
  const TrainingLoss wired_loss =
      trainingLoss(restored_prediction, expert, restored);
  const double recomposed =
      kPolicyLossWeight * wired_loss.policy +
      kScoreQuantileLossWeight * wired_loss.score_quantile +
      kLifetimeQuantileLossWeight * wired_loss.lifetime_quantile +
      kRegenerationLossWeight * wired_loss.regeneration +
      kFlowLossWeight * wired_loss.flow + kL2Weight * wired_loss.l2;
  expect(std::abs(policy_mass - 1.0) < 1.0e-6 &&
             wired_loss.policy > 0.0 && wired_loss.score_quantile > 0.0 &&
             wired_loss.lifetime_quantile > 0.0 &&
             wired_loss.regeneration > 0.0 && wired_loss.flow > 0.0 &&
             std::isfinite(wired_loss.total) &&
             std::abs(wired_loss.total - recomposed) < 1.0e-12,
         "expert visit target or frozen multi-head loss was not wired");

  Prediction nonunit_prediction;
  ExpertTarget nonunit_target;
  nonunit_target.played_action = 0;
  for (int action = 0; action < 3; ++action) {
    nonunit_prediction.legal[action] = true;
    nonunit_prediction.candidate[action].policy_logit =
        static_cast<float>(0.25 * action - 0.1);
  }
  nonunit_target.policy[0] = 0.1f;
  nonunit_target.policy[1] = 0.2f;
  nonunit_target.policy[2] = 0.3f;
  const double nonunit_mass =
      policyTargetMass(nonunit_prediction, nonunit_target);
  const auto nonunit_gradient =
      policyLogitGradients(nonunit_prediction, nonunit_target);
  const auto pure_policy_loss = [&](double perturbation) {
    Prediction perturbed = nonunit_prediction;
    perturbed.candidate[1].policy_logit = static_cast<float>(
        static_cast<double>(perturbed.candidate[1].policy_logit) +
        perturbation);
    const auto probabilities = policyProbabilities(perturbed);
    double loss = 0.0;
    for (int action = 0; action < 3; ++action) {
      loss -= static_cast<double>(nonunit_target.policy[action]) *
              std::log(probabilities[action]);
    }
    return loss;
  };
  constexpr double kPolicyDifference = 1.0e-3;
  const double numeric_policy_gradient =
      (pure_policy_loss(kPolicyDifference) -
       pure_policy_loss(-kPolicyDifference)) /
      (2.0 * kPolicyDifference);
  const double gradient_sum =
      std::accumulate(nonunit_gradient.begin(), nonunit_gradient.end(), 0.0);
  const double old_wrong_gradient =
      policyProbabilities(nonunit_prediction)[1] - nonunit_target.policy[1];
  expect(std::abs(nonunit_mass - 0.6) < 1.0e-6 &&
             std::abs(gradient_sum) < 1.0e-12 &&
             std::abs(nonunit_gradient[1] - numeric_policy_gradient) <
                 2.0e-5 &&
             std::abs(nonunit_gradient[1] - old_wrong_gradient) > 0.1,
         "non-unit Float32 policy mass used the normalized CE derivative");

  // Tiny development-corpus end-to-end fixture: public roots only, synthetic
  // outcomes only, and no headless game seed.  It exercises replay persistence,
  // reanalysis, deterministic full-loss optimization, and schedule accounting.
  const std::vector<PublicState> burned =
      loadBurnedRoots(std::string(kBurnedCorpusPath), 2);
  ReplayBuffer tiny_replay;
  for (std::size_t index = 0; index < burned.size(); ++index) {
    const SearchResult burned_search = searchRoot(burned[index], restored, 98);
    TrainingExample example;
    example.state = burned[index];
    example.target = makeExpertTarget(
        burned_search, burned_search.action,
        20'000.0 + static_cast<double>(index) * 1'000.0,
        40.0 + static_cast<double>(index), auxiliary);
    example.trajectory_group = static_cast<std::uint32_t>(100u + index);
    tiny_replay.push(std::move(example));
  }
  const std::vector<std::uint8_t> replay_bytes =
      serializeReplay(tiny_replay);
  const ReplayBuffer replay_roundtrip = deserializeReplay(replay_bytes);
  ReplayBuffer precision_replay;
  TrainingExample precision_example = tiny_replay[0];
  precision_example.target.remaining_score =
      std::ldexp(1.0, 24) + 1.25;
  precision_example.target.remaining_lifetime = 40.125;
  precision_replay.push(precision_example);
  const ReplayBuffer precision_roundtrip =
      deserializeReplay(serializeReplay(precision_replay));
  expect(replay_roundtrip.size() == tiny_replay.size() &&
             serializeReplay(replay_roundtrip) == replay_bytes,
         "tiny replay golden roundtrip failed");
  expect(precision_roundtrip[0].target.remaining_score ==
                 precision_example.target.remaining_score &&
             precision_roundtrip[0].target.remaining_lifetime ==
                 precision_example.target.remaining_lifetime,
         "replay persistence narrowed Float64 outcome targets");
  writeBytes("/tmp/drop7-regenerative-expert-self-test-replay.bin",
             replay_bytes);

  Prediction pareto_fixture;
  for (int action = 0; action < 3; ++action) {
    pareto_fixture.legal[action] = true;
  }
  for (float& value : pareto_fixture.candidate[0].score) value = 1.0f;
  for (float& value : pareto_fixture.candidate[0].lifetime) value = 0.1f;
  for (float& value : pareto_fixture.candidate[1].score) value = 0.1f;
  for (float& value : pareto_fixture.candidate[1].lifetime) value = 0.1f;
  for (float& value : pareto_fixture.candidate[2].score) value = 0.9f;
  for (float& value : pareto_fixture.candidate[2].lifetime) value = 1.0f;
  for (float& value : pareto_fixture.candidate[2].regeneration) value = 3.0f;
  for (float& value : pareto_fixture.candidate[2].flow) value = 1.0f;
  const ConstraintTrust all_trusted{true, true, true};
  const std::vector<int> untrusted_pareto =
      paretoSurvivors(pareto_fixture, {});
  const std::vector<int> trusted_pareto =
      paretoSurvivors(pareto_fixture, all_trusted);
  expect(untrusted_pareto == std::vector<int>({0, 1, 2}) &&
             trusted_pareto == std::vector<int>{2} &&
             constrainedPredictedAction(burned.front(), pareto_fixture, 0,
                                        0u, {}) == 0 &&
             constrainedPredictedAction(burned.front(), pareto_fixture, 0,
                                        0u, all_trusted) == 2,
         "downside/regeneration/flow Pareto ordering drifted");
  Prediction singleton_prediction;
  singleton_prediction.legal[4] = true;
  Prediction equal_prediction;
  equal_prediction.legal[1] = true;
  equal_prediction.legal[5] = true;
  Prediction nonfinite_prediction = singleton_prediction;
  nonfinite_prediction.candidate[4].score[0] =
      std::numeric_limits<float>::quiet_NaN();
  bool nonfinite_pareto_rejected = false;
  try {
    (void)paretoSurvivors(nonfinite_prediction, all_trusted);
  } catch (const std::runtime_error&) {
    nonfinite_pareto_rejected = true;
  }
  expect(paretoSurvivors(singleton_prediction, all_trusted) ==
                 std::vector<int>{4} &&
             paretoSurvivors(equal_prediction, all_trusted) ==
                 std::vector<int>({1, 5}) &&
             nonfinite_pareto_rejected,
         "Pareto selector lost nonempty/nonfinite safety");

  // Finite differences are undefined when the perturbation crosses one of the
  // clipped-ReLU knees.  Keep this composite-loss fixture strictly inside the
  // linear region; the adversarial cache fixture above separately tests the
  // exact runtime branch convention at a knee.
  Model gradient_fixture = restored;
  for (int unit = 0; unit < kStateUnits; ++unit) {
    gradient_fixture.weights()[ModelLayout::kStateBias + unit] += 0.35f;
  }
  for (int unit = 0; unit < kRelativeUnits; ++unit) {
    gradient_fixture.weights()[ModelLayout::kRelativeBias + unit] += 0.35f;
  }
  for (int unit = 0; unit < kTrunkUnits; ++unit) {
    gradient_fixture.weights()[ModelLayout::kTrunkBias + unit] += 0.35f;
  }
  bool smooth_gradient_fixture = true;
  constexpr float kGradientKneeMargin = 0.01f;
  const auto interior = [](const auto& values) {
    return std::all_of(values.begin(), values.end(), [](float value) {
      return value > kGradientKneeMargin &&
             value < 1.0f - kGradientKneeMargin;
    });
  };
  for (int action = 0; action < kBoardSize; ++action) {
    if (!isLegal(tiny_replay[0].state.board, action)) continue;
    const OrientationCache direct =
        orientationCache(gradient_fixture, tiny_replay[0].state, action);
    const OrientationCache reflected = orientationCache(
        gradient_fixture, mirror(tiny_replay[0].state),
        kBoardSize - 1 - action);
    smooth_gradient_fixture &=
        interior(direct.state_pre) && interior(direct.relative_pre) &&
        interior(direct.trunk_pre) && interior(reflected.state_pre) &&
        interior(reflected.relative_pre) && interior(reflected.trunk_pre);
  }
  expect(smooth_gradient_fixture,
         "analytic-gradient fixture crossed a clipped-ReLU knee");
  const GradientCheckResult gradient_check =
      deterministicGradientCheck(gradient_fixture, tiny_replay[0]);
  if (!gradient_check.passed) {
    throw std::runtime_error(
        "analytic composite-loss gradient check failed: max relative error=" +
        std::to_string(gradient_check.maximum_relative_error) +
        " parameter=" + std::to_string(gradient_check.worst_parameter) +
        " numeric=" + std::to_string(gradient_check.worst_numeric) +
        " analytic=" + std::to_string(gradient_check.worst_analytic));
  }

  Model trained_first = restored;
  Model trained_second = restored;
  DeterministicOptimizer optimizer_first;
  DeterministicOptimizer optimizer_second;
  const std::uint64_t before_training =
      fnv1a64(serializeCheckpoint(trained_first));
  const double first_loss = optimizer_first.step(trained_first, tiny_replay,
                                                 0u, 0u);
  const double second_loss = optimizer_second.step(trained_second, tiny_replay,
                                                   0u, 0u);
  const std::uint64_t after_training =
      fnv1a64(serializeCheckpoint(trained_first));
  const std::vector<std::uint8_t> trained_bytes =
      serializeCheckpoint(trained_first);
  const Model trained_restored = deserializeCheckpoint(trained_bytes);
  const Prediction trained_prediction = trained_first.predict(burned.front());
  const Prediction trained_roundtrip_prediction =
      trained_restored.predict(burned.front());
  expect(before_training != after_training && first_loss == second_loss &&
             serializeCheckpoint(trained_first) ==
                 serializeCheckpoint(trained_second) &&
             trained_first.weights() == trained_restored.weights() &&
             trained_prediction.candidate ==
                 trained_roundtrip_prediction.candidate &&
             constrainedPredictedAction(
                 burned.front(), trained_prediction, 0, 0u, {}) ==
                 constrainedPredictedAction(burned.front(),
                                            trained_roundtrip_prediction, 0,
                                            0u, {}) &&
             optimizer_first.updates() == 1u,
         "deterministic full-loss optimizer did not update parameters");

  const GradientClipResult clip_345 =
      gradientClipResult(std::vector<double>{3.0, 4.0});
  const GradientClipResult clip_huge = gradientClipResult(
      std::vector<double>{std::numeric_limits<double>::max() / 4.0,
                          std::numeric_limits<double>::max() / 8.0});
  expect(clip_345.norm == 5.0 && std::abs(clip_345.scale - 0.2) < 1.0e-15 &&
             clip_huge.scale > 0.0 && std::isfinite(clip_huge.scale) &&
             std::abs((std::numeric_limits<double>::max() / 4.0) *
                          clip_huge.scale -
                      2.0 * (std::numeric_limits<double>::max() / 8.0) *
                          clip_huge.scale) <
                 1.0e-12,
         "global-norm clip was not stable at the registered 1.0 radius");

  ReplayBuffer high_gradient_replay;
  TrainingExample high_gradient_example = tiny_replay[0];
  high_gradient_example.target.policy.fill(0.0f);
  high_gradient_example.target.policy[
      high_gradient_example.target.played_action] = 100.0f;
  high_gradient_replay.push(high_gradient_example);
  Model clipped_model = restored;
  DeterministicOptimizer clipped_optimizer;
  (void)clipped_optimizer.step(clipped_model, high_gradient_replay, 0u, 0u);
  expect(clipped_optimizer.lastGradientNorm() > kGradientNormClip &&
             std::abs(clipped_optimizer.lastGradientScale() -
                      kGradientNormClip /
                          clipped_optimizer.lastGradientNorm()) <
                 1.0e-12 &&
             clipped_model.weights() != restored.weights(),
         "optimizer did not apply the registered pre-Adam clip");

  ReplayBuffer nonfinite_replay;
  TrainingExample nonfinite_example = tiny_replay[0];
  nonfinite_example.target.remaining_score =
      std::numeric_limits<double>::quiet_NaN();
  nonfinite_replay.push(nonfinite_example);
  Model rejected_model = restored;
  Model clean_model = restored;
  DeterministicOptimizer rejected_optimizer_state;
  DeterministicOptimizer clean_optimizer_state;
  const std::vector<std::uint8_t> rejected_before =
      serializeCheckpoint(rejected_model);
  bool nonfinite_update_rejected = false;
  try {
    (void)rejected_optimizer_state.step(rejected_model, nonfinite_replay,
                                        0u, 0u);
  } catch (const std::runtime_error&) {
    nonfinite_update_rejected = true;
  }
  (void)rejected_optimizer_state.step(rejected_model, tiny_replay, 0u, 0u);
  (void)clean_optimizer_state.step(clean_model, tiny_replay, 0u, 0u);
  expect(nonfinite_update_rejected &&
             rejected_optimizer_state.updates() == 1u &&
             clean_optimizer_state.updates() == 1u &&
             rejected_model.weights() == clean_model.weights() &&
             rejected_optimizer_state.firstMoment() ==
                 clean_optimizer_state.firstMoment() &&
             rejected_optimizer_state.secondMoment() ==
                 clean_optimizer_state.secondMoment() &&
             rejected_before == serializeCheckpoint(restored),
         "non-finite batch mutated Adam or model before rejection");

  IterationResumeState durable_state;
  durable_state.model = trained_first;
  durable_state.replay = tiny_replay;
  durable_state.optimizer = optimizer_first;
  durable_state.ledger.d4_pretraining_epochs = kD4PretrainingEpochs;
  durable_state.ledger.d4_pretraining_updates = 1;
  const std::vector<std::uint8_t> durable_bytes =
      serializeResumeState(durable_state);
  IterationResumeState resumed_state =
      deserializeResumeState(durable_bytes);
  IterationResumeState forged_completion = durable_state;
  forged_completion.next_round = kRounds;
  bool forged_completion_rejected = false;
  try {
    (void)certifyCompletedIteration(forged_completion);
  } catch (const std::runtime_error&) {
    forged_completion_rejected = true;
  }
  expect(serializeResumeState(resumed_state) == durable_bytes &&
             resumed_state.optimizer.lastGradientNorm() ==
                 durable_state.optimizer.lastGradientNorm() &&
             resumed_state.optimizer.lastGradientScale() ==
                 durable_state.optimizer.lastGradientScale() &&
             !certifyCompletedIteration(durable_state).has_value() &&
             forged_completion_rejected &&
             !checkpointHasDeploymentCertificate(
                 serializeCheckpoint(durable_state.model)),
         "incomplete/uncalibrated state minted deployment authority");
  (void)durable_state.optimizer.step(durable_state.model,
                                     durable_state.replay, 0u, 1u);
  (void)resumed_state.optimizer.step(resumed_state.model,
                                     resumed_state.replay, 0u, 1u);
  expect(durable_state.model.weights() == resumed_state.model.weights() &&
             durable_state.optimizer.updates() ==
                 resumed_state.optimizer.updates() &&
             durable_state.optimizer.firstMoment() ==
                 resumed_state.optimizer.firstMoment() &&
             durable_state.optimizer.secondMoment() ==
                 resumed_state.optimizer.secondMoment() &&
             durable_state.optimizer.lastGradientNorm() ==
                 resumed_state.optimizer.lastGradientNorm() &&
             durable_state.optimizer.lastGradientScale() ==
                 resumed_state.optimizer.lastGradientScale() &&
             serializeReplay(durable_state.replay) ==
                 serializeReplay(resumed_state.replay) &&
             calibrationBitwiseEqual(durable_state.previous_calibration,
                                      resumed_state.previous_calibration) &&
             serializeIterationLedger(durable_state.ledger) ==
                 serializeIterationLedger(resumed_state.ledger),
         "interruption/resume changed exact model, Adam, replay, or ledger state");

  const std::vector<std::size_t> tiny_indices =
      reanalysisIndices(tiny_replay, 0, 5);
  const std::vector<TrainingExample> tiny_reanalysis =
      reanalyseReplay(tiny_replay, restored, 0, 2, {});
  expect(tiny_indices.size() == 5 && tiny_reanalysis.size() == 2 &&
             tiny_reanalysis[0].state ==
                 tiny_replay[tiny_indices[0]].state,
         "replay/reanalysis count or deterministic selection drifted");

  // Whole trajectories are permanently assigned to train/A/B.  Reserved
  // records remain serializable evidence but are unavailable to reanalysis
  // and are rejected by the full-corpus optimizer.
  std::array<std::uint32_t, 3> partition_group{};
  for (int desired = 0; desired < 3; ++desired) {
    for (std::uint32_t group = 1;; ++group) {
      if (calibrationPartition(group, 0) == desired) {
        partition_group[desired] = group;
        break;
      }
    }
  }
  std::vector<TrainingExample> split_source;
  for (int partition = 0; partition < 3; ++partition) {
    for (int duplicate = 0; duplicate < 2; ++duplicate) {
      TrainingExample example = tiny_replay[0];
      example.trajectory_group = partition_group[partition];
      split_source.push_back(std::move(example));
    }
  }
  std::vector<TrainingExample> split_source_copy = split_source;
  CalibrationSplit calibration_split =
      splitFreshForCalibration(split_source, 0);
  CalibrationSplit repeated_split =
      splitFreshForCalibration(split_source_copy, 0);
  ReplayBuffer reserved_replay;
  reserved_replay.append(std::vector<TrainingExample>(split_source));
  const std::vector<std::size_t> train_only_reanalysis =
      reanalysisIndices(reserved_replay, 0, 16);
  const std::vector<std::size_t> epoch_order =
      deterministicEpochOrder(calibration_split.training.size(), 1u, 0u);
  bool reserved_optimizer_rejected = false;
  try {
    Model rejected_model = restored;
    DeterministicOptimizer rejected_optimizer;
    (void)optimizeEpochs(rejected_model, reserved_replay,
                         rejected_optimizer, 1, 0u);
  } catch (const std::invalid_argument&) {
    reserved_optimizer_rejected = true;
  }
  std::vector<std::size_t> sorted_epoch = epoch_order;
  std::sort(sorted_epoch.begin(), sorted_epoch.end());
  expect(calibration_split.heldout[0].size() == 2 &&
             calibration_split.heldout[1].size() == 2 &&
             calibration_split.training.size() == 2 &&
             repeated_split.heldout[0].size() == 2 &&
             repeated_split.heldout[1].size() == 2 &&
             std::all_of(train_only_reanalysis.begin(),
                         train_only_reanalysis.end(),
                         [&](std::size_t index) {
                           return reserved_replay[index]
                                      .calibration_reservation == 0;
                         }) &&
             sorted_epoch == std::vector<std::size_t>({0, 1}) &&
             reserved_optimizer_rejected,
         "whole-trajectory calibration reservation leaked into training");

  Model calibration_model;
  const int lifetime_bias =
      ModelLayout::kLifetime + kLifetimeQuantiles * kTrunkUnits;
  for (int quantile = 0; quantile < kLifetimeQuantiles; ++quantile) {
    calibration_model.weights()[lifetime_bias + quantile] =
        static_cast<float>(0.02 * (quantile + 1));
  }
  const int regeneration_bias =
      ModelLayout::kRegeneration + kRegenerationHeads * kTrunkUnits;
  for (int head = 0; head < kRegenerationHeads; ++head) {
    calibration_model.weights()[regeneration_bias + head] = -3.0f;
  }
  PublicState calibration_state;
  calibration_state.next_disc = 4;
  calibration_state.phase = kMovesPerLevel;
  std::array<std::vector<TrainingExample>, 2> calibrated_halves;
  for (int half = 0; half < 2; ++half) {
    for (int action = 0; action < kBoardSize; ++action) {
      const auto lifetime = decodedLifetimeQuantiles(
          calibration_model.predict(calibration_state).candidate[action]);
      const double lower = 0.5 * (lifetime[7] + lifetime[8]);
      const double upper = 0.5 * (lifetime[23] + lifetime[24]);
      for (int repeat = 0; repeat < 32; ++repeat) {
        TrainingExample example;
        example.state = calibration_state;
        example.target.played_action = action;
        example.target.policy[action] = 1.0f;
        example.target.remaining_lifetime =
            repeat < 8 ? lower - 10.0
                       : (repeat < 24 ? 0.5 * (lower + upper)
                                      : upper + 10.0);
        example.trajectory_group = static_cast<std::uint32_t>(
            10'000 + half * 1'000 + action * 32 + repeat);
        example.calibration_reservation =
            static_cast<std::uint8_t>(half + 1);
        calibrated_halves[half].push_back(std::move(example));
      }
    }
  }
  const CalibrationResult calibrated = calibrateHeldout(
      calibration_model, calibrated_halves, 224);
  auto regeneration_failed_halves = calibrated_halves;
  for (auto& half : regeneration_failed_halves) {
    for (TrainingExample& example : half) {
      example.target.auxiliary.regenerated_by.fill(true);
    }
  }
  const CalibrationResult regeneration_failed = calibrateHeldout(
      calibration_model, regeneration_failed_halves, 224);
  std::array<double, kRegenerationHeads> boundary_ece{};
  std::array<double, kRegenerationHeads> boundary_brier{};
  boundary_ece.fill(kRegenerationMaximumEce);
  boundary_brier.fill(kRegenerationMaximumBrier);
  std::array<double, kFlowHeads> boundary_flow{};
  boundary_flow.fill(kFlowMaximumNormalizedMae);
  auto failed_ece = boundary_ece;
  failed_ece[0] = std::nextafter(
      kRegenerationMaximumEce,
      std::numeric_limits<double>::infinity());
  auto failed_flow = boundary_flow;
  failed_flow[0] = std::nextafter(
      kFlowMaximumNormalizedMae,
      std::numeric_limits<double>::infinity());
  std::array<int, kBoardSize> boundary_support{};
  boundary_support.fill(kCalibrationMinimumPlayedPerColumn);
  expect(calibrated.trust() == ConstraintTrust{true, true, true} &&
             regeneration_failed.trust() ==
                 ConstraintTrust{true, false, true} &&
             trustForRound(0, calibrated) == ConstraintTrust{} &&
             trustForRound(1, calibrated) == ConstraintTrust{} &&
             trustForRound(2, regeneration_failed) ==
                 ConstraintTrust{true, false, true} &&
             lifetimeCalibrationMetricsPass(true, true, 0.65, 0.25) &&
             !lifetimeCalibrationMetricsPass(
                 true, true,
                 std::nextafter(0.65,
                                std::numeric_limits<double>::infinity()),
                 0.25) &&
             regenerationCalibrationMetricsPass(
                 true, true, boundary_ece, boundary_brier) &&
             !regenerationCalibrationMetricsPass(
                 true, true, failed_ece, boundary_brier) &&
             flowCalibrationMetricsPass(true, true, boundary_flow) &&
             !flowCalibrationMetricsPass(true, true, failed_flow) &&
             calibrationSupportPass(kCalibrationMinimumExamplesPerHalf, 2,
                                    boundary_support) &&
             !calibrationSupportPass(
                 kCalibrationMinimumExamplesPerHalf - 1, 2,
                 boundary_support),
         "calibration metric/trust boundary fixture changed");

  const IterationLedger schedule = frozenScheduleLedger(tiny_replay.size());
  const IterationLedger fallback_schedule =
      frozenScheduleLedger(tiny_replay.size(), false);
  int exported_rounds = 0;
  for (int round = 0; round < kRounds; ++round) {
    expect(schedule.rounds[round].round == round + 1 &&
               schedule.rounds[round].new_roots ==
                   kOnPolicyRootsPerRound &&
               schedule.rounds[round].reanalysed_roots ==
                   kReanalysisRootsPerRound &&
               schedule.rounds[round].optimizer_epochs ==
                   kOptimizerEpochsPerRound,
           "eight-round production ledger drifted");
    exported_rounds += schedule.rounds[round].checkpoint_exported;
  }
  expect(schedule.total_new_roots == 160'000 &&
             schedule.total_reanalysed_roots == 40'000 &&
             schedule.total_new_roots + schedule.total_reanalysed_roots ==
                 kTotalSearchedRoots &&
             schedule.checkpoint_exports == 1 &&
             schedule.deployed_round == 8 && exported_rounds == 1 &&
             !schedule.rounds[6].checkpoint_exported &&
             schedule.rounds[7].checkpoint_exported &&
             fallback_schedule.checkpoint_exports == 0 &&
             fallback_schedule.deployed_round == 0 &&
             !fallback_schedule.rounds[7].checkpoint_exported,
         "round schedule performed checkpoint selection or wrong counts");

  bool lane_guard_rejected = false;
  try {
    requireExactLane(FreshPurpose::kStageA, kStageALane.first, 31);
  } catch (const std::invalid_argument&) {
    lane_guard_rejected = true;
  }
  GateMetrics failed_absolute;
  failed_absolute.games = kStageAGate.games;
  failed_absolute.natural_games = kStageAGate.games;
  failed_absolute.mean_score = kStageAGate.minimum_score - 1.0;
  failed_absolute.mean_moves = kStageAGate.minimum_moves;
  failed_absolute.bottom_quartile_moves =
      kStageAGate.minimum_bottom_quartile_moves;
  failed_absolute.clears_per_move = kStageAGate.minimum_clears_per_move;
  failed_absolute.reveals_per_move = kStageAGate.minimum_reveals_per_move;
  GateMetrics passed_absolute = failed_absolute;
  passed_absolute.mean_score = kStageAGate.minimum_score;
  expect(lane_guard_rejected && plannedGateAttempts(false, true, true) == 1 &&
             plannedGateAttempts(true, false, true) == 2 &&
             plannedGateAttempts(true, true, false) == 3 &&
             !shouldAttemptBaseline(FreshPurpose::kStageA,
                                    failed_absolute) &&
             shouldAttemptBaseline(FreshPurpose::kStageA,
                                   passed_absolute) &&
             !shouldAttemptBaseline(
                 FreshPurpose::kDevelopmentConfirmation,
                 passed_absolute) &&
             expertSeed(0, 0, 0) == kExpertGameLane.first,
         "seed guard or fail-stop gate sequence drifted");

  // Static arena/replay/model resource proof is independent of allocator RSS.
  expect(kParameterCount <= kMaximumModelWeights &&
             kMaximumFinalReplayRoots == 288'000u &&
             kProjectedD4TrainingRoots == 128'000u &&
             kProjectedOptimizerBatches == 11'128u &&
             kMaximumD4CapturedRootBytes ==
                 align64(sizeof(TrainingExample)) * 128'000u &&
             kProjectedPeakResidentBytes < kMaximumRssBytes &&
             kBrowserArenaBytes == 32u * 1024u * 1024u &&
             kBrowserSimulationSteps.back() == kSearchSimulations,
         "model or arena resource proof failed");

  return {fnv1a64(checkpoint), checkpoint.size(), peakRssBytes(),
          gradient_check.checked, gradient_check.maximum_relative_error,
          after_training};
}

std::string preregistrationJson() {
  std::ostringstream output;
  output << std::setprecision(12)
         << "{\n"
         << "  \"format\":\"drop7-regenerative-expert-iteration-prereg-v3\",\n"
         << "  \"status\":\"preflight-only-no-fresh-seed\",\n"
         << "  \"causalTarget\":\"five-move cycle injects 12 occupied cells and 7 covers; learn policy-conditioned multi-cycle regeneration\",\n"
         << "  \"search\":{\"kind\":\"stochastic-alphazero-gumbel-sequential-halving\",\"allLegalRootActions\":true,\"simulations\":"
         << kSearchSimulations << ",\"teacherDepthMoves\":" << kSearchDepthMoves
         << ",\"deploymentMaximumPly\":" << kDeploymentMaximumPly
         << ",\"browserPlyRange\":[1,8],\"plyDefinition\":\"one disc placement including its stochastic transition\""
         << ",\"chanceStrata\":" << kChanceStrata
         << ",\"freshCanonicalPacks\":true,\"rotationUsesVisitDiv7\":true,\"actualSearchPackIsPermutation1Through7\":true,\"symmetricBoardCoordinateOrientation\":\"side action breaks tie; center retains distinct coordinate events\",\"pairedByPerActionLocalVisit\":true,\"completeAllocationRoundsBeforeHalving\":true,\"determinization\":false,\"fixedReservoir\":false,\"utility\":\"single 0.8 mean + 0.2 CVaR25 over flattened stochastic leaf quantiles\"},\n"
         << "  \"iteration\":{\"d4InitializationGames\":64,\"rounds\":"
         << kRounds << ",\"onPolicyRootsPerRound\":"
         << kOnPolicyRootsPerRound << ",\"reanalysisRootsPerRound\":"
         << kReanalysisRootsPerRound << ",\"totalRoots\":"
         << kTotalSearchedRoots << ",\"d4PretrainingEpochs\":"
         << kD4PretrainingEpochs << ",\"optimizerEpochsPerRound\":"
         << kOptimizerEpochsPerRound << ",\"optimizerBatchSize\":"
         << kOptimizerBatchSize
         << ",\"optimizer\":\"ordinary analytic minibatch backprop plus global-norm-clipped Adam; deterministic no-replacement full-corpus epochs\",\"gradientNormClip\":"
         << kGradientNormClip
         << ",\"clipPosition\":\"averaged batch gradient before Adam moments\",\"freshSplit\":\"CAL1 hash mod 10: half A=0, half B=1, train=2..9; whole trajectories permanently reserved\",\"reanalysis\":\"exactly 5000 prior-replay train-only roots before current fresh append\",\"roundTrust\":\"rounds 0-1 disabled; round >=2 uses previous round independently calibrated groups\",\"learningRate\":"
         << kOptimizerLearningRate
         << ",\"checkpointSelection\":false,\"conditionalDeployRound\":8,\"resumeBoundary\":\"atomic exact state after D4 bootstrap and every completed round\",\"fallback\":\"no checkpoint; exact D4\"},\n"
         << "  \"model\":{\"reflection\":\"exact dual-orientation accumulator average\",\"stateUnits\":"
         << kStateUnits << ",\"relativeUnits\":" << kRelativeUnits
         << ",\"trunkUnits\":" << kTrunkUnits << ",\"scoreQuantiles\":"
         << kScoreQuantiles << ",\"lifetimeQuantiles\":"
         << kLifetimeQuantiles << ",\"regenerationHeads\":4,\"flowHeads\":8,\"weights\":"
         << kParameterCount << ",\"maximumWeights\":"
         << kMaximumModelWeights << ",\"targetScales\":{\"score\":"
         << kScoreTargetScale << ",\"lifetime\":" << kLifetimeTargetScale
         << ",\"flowPerMove\":" << kFlowPerMoveScale
         << "},\"selectionProjection\":\"nonnegative PAVA score/lifetime quantiles, sigmoid+PAVA regeneration, separate nonnegative PAVA cumulative clear/reveal\",\"checkpoint\":\"little-endian Float32 v3 with checksum, clip=1.0 training schema, and round-8 calibration/replay/ledger deployment certificate\",\"float32CheckpointBytes\":"
         << kFloat32CheckpointBytes << ",\"maximumCheckpointBytes\":"
         << kMaximumCheckpointBytes << "},\n"
         << "  \"loss\":{\"normalizedTargets\":true,\"policyCE\":1.0,\"scoreQR\":0.5,\"lifetimeQR\":0.25,\"regenerationBCE\":0.2,\"flowHuber\":0.1,\"l2\":1e-5},\n"
         << "  \"calibration\":{\"minimumExamplesPerHalf\":" << kCalibrationMinimumExamplesPerHalf
         << ",\"minimumPlayedPerColumn\":" << kCalibrationMinimumPlayedPerColumn
         << ",\"lifetimeCentral50AbsoluteError\":" << kLifetimeCoverageTolerance
         << ",\"lifetimeLower25AbsoluteError\":" << kLifetimeLowerCoverageTolerance
         << ",\"regenerationPerHeadEceMaximum\":" << kRegenerationMaximumEce
         << ",\"regenerationPerHeadBrierMaximum\":" << kRegenerationMaximumBrier
         << ",\"flowPerHeadNormalizedMaeMaximum\":" << kFlowMaximumNormalizedMae
         << ",\"bothHalvesRequired\":true,\"finalAllGroupsRequired\":true},\n"
         << "  \"publicBoundary\":{\"allowed\":[\"49 visible cells\",\"next disc\",\"five-move phase\",\"terminal\",\"candidate column\"],\"excluded\":[\"score\",\"level\",\"move index\",\"history\",\"origin seed\",\"future discs\",\"future reveals\",\"oracle actions\",\"search-tape identity\"]},\n"
         << "  \"domains\":{\"rootPack\":\"0x52504b31\",\"chanceEvent\":\"0x43484e31\",\"policySample\":\"0x504f4c31\",\"replay\":\"0x52504c31\",\"trainingShuffle\":\"0x53484631\",\"calibration\":\"0x43414c31\",\"chanceInputs\":[\"canonical public hash\",\"node depth\",\"seven-visit pack identity\",\"event index\"],\"coordinateOrientationInputs\":[\"public board reflection\",\"candidate side only on a symmetric board\"],\"originSeedExcluded\":true},\n"
         << "  \"unopenedLanes\":{\"d4Initialization\":\"0x3da40000...0x3da4003f\",\"expertGames\":\"0x3da41000...0x3da7ffff\",\"stageA\":\"0x3da80000...0x3da8001f\",\"stageB\":\"0x3da90000...0x3da9007f\",\"developmentConfirmation\":\"0x3daa0000...0x3daa00ff\",\"reserved\":\"0x3dab0000...0x3dabffff\",\"protected\":[\"0x7d...\",\"0xd7...\"]},\n"
         << "  \"gates\":{\"deploymentPly\":8,\"round8DeploymentCertificateRequiredBeforeSeed\":true,\"candidateAbsolutePhaseBeforeD4\":true,\"stageA\":{\"games\":32,\"score\":500000,\"moves\":150,\"bottomQuartileMoves\":90,\"clearsPerMove\":2.15,\"revealsPerMove\":1.18,\"scoreAndMoveRatioVsD4\":1.15,\"jointWins\":20},\"stageB\":{\"games\":128,\"score\":750000,\"moves\":220,\"bottomQuartileMoves\":140,\"clearsPerMove\":2.25,\"revealsPerMove\":1.28,\"scoreAndMoveRatioVsD4\":1.15,\"jointWins\":80},\"stageC\":{\"games\":256,\"maximumMoves\":2000,\"meanScoreGreaterThan\":1050000,\"bootstrapLower95GreaterThan\":1000000,\"meanMovesGreaterThan\":300,\"clearsPerMoveAtLeast\":2.30,\"revealsPerMoveAtLeast\":1.32},\"failure\":\"stop without retuning\"},\n"
         << "  \"resources\":{\"workers\":" << kWorkers
         << ",\"roots\":" << kTotalSearchedRoots
         << ",\"nnueLeaves\":" << kProjectedLeaves
         << ",\"maximumSyntheticTransitions\":"
         << kMaximumSyntheticTransitions
         << ",\"maximumNnueEvaluationsIncludingPolicyNodes\":"
         << kMaximumNnueEvaluations << ",\"maximumRssBytes\":"
         << kMaximumRssBytes << ",\"maximumWallSeconds\":"
         << kMaximumWallSeconds << ",\"browserArenaBytes\":"
         << kBrowserArenaBytes << ",\"projectedPeakResidentBytes\":"
         << kProjectedPeakResidentBytes
         << ",\"maximumFinalReplayRoots\":" << kMaximumFinalReplayRoots
         << ",\"maximumD4BootstrapRoots\":" << kMaximumD4BootstrapRoots
         << ",\"maximumD4CapturedRootBytes\":"
         << kMaximumD4CapturedRootBytes
         << ",\"maximumD4MovesPerGame\":" << kMaximumGameMoves
         << ",\"maximumResumeStateBytes\":" << kMaximumResumeStateBytes
         << ",\"projectedOptimizerBatches\":" << kProjectedOptimizerBatches
         << ",\"browserIterativeSimulations\":[49,63,77,98]},\n"
         << "  \"implementationBoundary\":{\"status\":\"compiled-but-fresh-runners-never-executed\",\"present\":[\"public engine adapter\",\"seven-stratum warmup for every legal root action\",\"depth-20 teacher and ply-1..8 deployment search\",\"reflection-exact NNUE and Float32 certified checkpoint\",\"normalized visit-policy and auxiliary targets\",\"analytic full-loss backprop and deterministic full-corpus Adam epochs\",\"checksummed replay/permanent calibration reservations/train-only reanalysis\",\"exact D4 64-game initializer\",\"eight-round 20k-new+5k-reanalysis loop\",\"atomic exact round-boundary resume state\",\"certificate-conditional round-8 export\",\"candidate-first fail-stop Stage A/B/C runners\",\"strict/sanitizer/performance preflight commands\"],\"availableCommands\":[\"preregister\",\"self-test\",\"preflight\",\"initialize-d4\",\"iterate\",\"resume-iterate\",\"gates\"],\"freshCommandsRequireExactTokenAndHardCodedLane\":true},\n"
         << "  \"burnedPreflightCorpus\":{\"path\":\""
         << kBurnedCorpusPath << "\",\"bytes\":" << kBurnedCorpusBytes
         << ",\"sha256\":\"" << kBurnedCorpusSha256 << "\"}\n"
         << "}\n";
  return output.str();
}

void writeText(const std::string& path, std::string_view value) {
  writeBytes(path, std::vector<std::uint8_t>(value.begin(), value.end()));
}

std::string selfTestJson(const SelfTestResult& result) {
  std::ostringstream output;
  output << "{\n"
         << "  \"format\":\"drop7-regenerative-expert-self-test-v3\",\n"
         << "  \"passed\":true,\n"
         << "  \"freshSeedsOpened\":false,\n"
         << "  \"fixtures\":[\"exact-engine\",\"reflection\",\"metadata-boundary\",\"float32-forward-cache-branch-parity\",\"double-score-and-PAVA-utility\",\"strategy-fusion\",\"actual-search-sequential-and-coordinate-seven-packs\",\"complete-transition-seven-pack\",\"symmetric-side-action-random-field\",\"symmetric-center-distinct-coordinate-events\",\"all-seven-strata-before-filtering\",\"normalized-quantile-loss\",\"nonunit-policy-mass-gradient\",\"regeneration-flow-target\",\"PAVA-output-projection\",\"float32-v3-checkpoint-golden\",\"deployment-certificate-and-pre-seed-rejection\",\"depth-20-teacher-coverage\",\"ply-1-and-ply-8-deployment\",\"trust-disabled-and-enabled-pareto\",\"pareto-nonempty-nonfinite-safety\",\"training-target-loss-wiring\",\"smooth-interior-analytic-gradient-check\",\"stable-clip-1.0\",\"nonfinite-optimizer-rollback\",\"replay-v3-float64-roundtrip\",\"exact-interruption-resume\",\"permanent-whole-trajectory-reservation\",\"train-only-reanalysis-and-epoch-order\",\"two-half-calibration-boundaries\",\"round-trust-guard\",\"deterministic-twin-adam\",\"trained-float32-action-parity\",\"burned-root-reanalysis\",\"eight-round-counts\",\"conditional-round8-export-fallback\",\"seed-guard\",\"candidate-first-gate\",\"worst-case-resource-proof\"],\n"
         << "  \"modelWeights\":" << kParameterCount << ",\n"
         << "  \"checkpointBytes\":" << result.checkpoint_bytes << ",\n"
         << "  \"checkpointFnv1a64\":\"0x" << std::hex
         << result.checkpoint_hash << std::dec << "\",\n"
         << "  \"gradientChecks\":" << result.gradient_checks << ",\n"
         << "  \"maximumGradientRelativeError\":"
         << std::setprecision(12) << result.maximum_gradient_relative_error
         << ",\n"
         << "  \"trainedCheckpointFnv1a64\":\"0x" << std::hex
         << result.trained_checkpoint_hash << std::dec << "\",\n"
         << "  \"projectedResidentBytes\":" << kProjectedPeakResidentBytes
         << ",\n"
         << "  \"peakRssBytes\":" << result.peak_rss_bytes << "\n"
         << "}\n";
  return output.str();
}

std::string performanceJson(const PerformanceProjection& result) {
  std::ostringstream output;
  output << std::setprecision(12)
         << "{\n"
         << "  \"format\":\"drop7-regenerative-expert-performance-preflight-v3\",\n"
         << "  \"passed\":" << (result.admitted ? "true" : "false")
         << ",\n"
         << "  \"admissionBoundary\":\"no fresh seed; D4 initialization and expert iteration not run\",\n"
         << "  \"freshSeedsOpened\":false,\n"
         << "  \"burnedCorpus\":{\"path\":\"" << kBurnedCorpusPath
         << "\",\"bytes\":" << kBurnedCorpusBytes << ",\"sha256\":\""
         << kBurnedCorpusSha256 << "\",\"rootsRead\":"
         << result.benchmark_roots << "},\n"
         << "  \"benchmark\":{\"productionShaped\":true,\"simulationsPerRoot\":"
         << kSearchSimulations << ",\"maximumDepthMoves\":"
         << kSearchDepthMoves << ",\"seconds\":"
         << result.benchmark_seconds << ",\"secondsPerRoot\":"
         << result.seconds_per_root << ",\"transitions\":"
         << result.transitions << ",\"nnueLeaves\":" << result.leaves
         << ",\"nnueEvaluations\":" << result.nnue_evaluations
         << ",\"observedMaximumDepth\":" << result.maximum_depth
         << ",\"transitionsPerSimulation\":"
         << (static_cast<double>(result.transitions) /
             (result.benchmark_roots * kSearchSimulations))
         << "},\n"
         << "  \"projection\":{\"workers\":" << kWorkers
         << ",\"searchedRoots\":" << kTotalSearchedRoots
         << ",\"searchSecondsWith1_5xMargin\":"
         << result.projected_search_seconds
         << ",\"d4InitializationGames\":64,\"d4MaximumMovesPerGame\":"
         << kMaximumGameMoves
         << ",\"d4SecondsWith1_25xMargin\":"
         << result.projected_d4_initialization_seconds
         << ",\"optimizerBatchSize\":" << kOptimizerBatchSize
         << ",\"projectedOptimizerBatches\":" << kProjectedOptimizerBatches
         << ",\"d4Epochs\":" << kD4PretrainingEpochs
         << ",\"roundEpochs\":" << kOptimizerEpochsPerRound
         << ",\"optimizerStepSeconds\":" << result.optimizer_step_seconds
         << ",\"optimizerSecondsWith1_5xMargin\":"
         << result.projected_optimizer_seconds
         << ",\"totalSeconds\":" << result.projected_total_seconds
         << ",\"limitSeconds\":" << kMaximumWallSeconds << "},\n"
         << "  \"memory\":{\"measuredPeakRssBytes\":"
         << result.rss_bytes << ",\"staticProjectedResidentBytes\":"
         << kProjectedResidentBytes
         << ",\"maximumD4CapturedRootBytes\":"
         << kMaximumD4CapturedRootBytes
         << ",\"d4ProjectedResidentBytes\":"
         << kD4InitializationResidentBytes
         << ",\"maximumFinalReplayRoots\":" << kMaximumFinalReplayRoots
         << ",\"maximumResumeStateBytes\":" << kMaximumResumeStateBytes
         << ",\"projectedPeakResidentBytes\":"
         << kProjectedPeakResidentBytes << ",\"limitBytes\":"
         << kMaximumRssBytes << "}\n"
         << "}\n";
  return output.str();
}

void requireFreshExecutionToken(std::string_view token) {
  if (token != kFreshExecutionToken) {
    throw std::invalid_argument(
        "fresh runner requires exact frozen-protocol execution token");
  }
}

std::string initializationJson(const ReplayBuffer& replay) {
  std::ostringstream output;
  output << "{\n"
         << "  \"format\":\"drop7-regenerative-d4-initialization-v1\",\n"
         << "  \"freshSeedLane\":\"0x3da40000...0x3da4003f\",\n"
         << "  \"games\":64,\n"
         << "  \"completeNaturalGamesRequired\":true,\n"
         << "  \"exactD4\":true,\n"
         << "  \"replayRecords\":" << replay.size() << "\n"
         << "}\n";
  return output.str();
}

template <std::size_t Size>
double maximumMetric(const std::array<double, Size>& values) {
  return *std::max_element(values.begin(), values.end());
}

void appendCalibrationHalfJson(std::ostringstream& output,
                               const CalibrationHalfMetrics& half) {
  output << "{\"examples\":" << half.examples
         << ",\"trajectoryGroups\":" << half.trajectory_groups
         << ",\"lifetimeCentral50Coverage\":" << half.lifetime_coverage
         << ",\"lifetimeLower25Coverage\":"
         << half.lifetime_lower_coverage
         << ",\"maximumRegenerationEce\":"
         << maximumMetric(half.regeneration_ece)
         << ",\"maximumRegenerationBrier\":"
         << maximumMetric(half.regeneration_brier)
         << ",\"maximumFlowNormalizedMae\":"
         << maximumMetric(half.flow_normalized_mae)
         << ",\"finite\":" << (half.finite ? "true" : "false")
         << ",\"passes\":{\"lifetime\":"
         << (half.lifetime_pass ? "true" : "false")
         << ",\"regeneration\":"
         << (half.regeneration_pass ? "true" : "false")
         << ",\"flow\":" << (half.flow_pass ? "true" : "false")
         << "}}";
}

std::string iterationJson(const IterationResult& result) {
  std::ostringstream output;
  output << "{\n"
         << "  \"format\":\"drop7-regenerative-expert-iteration-v3\",\n"
         << "  \"freshSeedLane\":\"0x3da41000...0x3da7ffff\",\n"
         << "  \"d4PretrainingEpochs\":"
         << result.ledger.d4_pretraining_epochs << ",\n"
         << "  \"d4PretrainingUpdates\":"
         << result.ledger.d4_pretraining_updates << ",\n"
         << "  \"rounds\":[";
  for (int round = 0; round < kRounds; ++round) {
    if (round != 0) output << ',';
    const RoundLedger& value = result.ledger.rounds[round];
    output << "{\"round\":" << value.round
           << ",\"newRoots\":" << value.new_roots
           << ",\"reanalysedRoots\":" << value.reanalysed_roots
           << ",\"trainingExamples\":" << value.training_examples
           << ",\"optimizerEpochs\":" << value.optimizer_epochs
           << ",\"optimizerUpdates\":" << value.optimizer_updates
           << ",\"searchTrust\":{\"lifetime\":"
           << (value.search_trust.lifetime ? "true" : "false")
           << ",\"regeneration\":"
           << (value.search_trust.regeneration ? "true" : "false")
           << ",\"flow\":" << (value.search_trust.flow ? "true" : "false")
           << "},\"calibration\":[";
    appendCalibrationHalfJson(output, value.calibration.half[0]);
    output << ',';
    appendCalibrationHalfJson(output, value.calibration.half[1]);
    output << ']'
           << ",\"replaySizeAfter\":" << value.replay_size_after
           << ",\"checkpointExported\":"
           << (value.checkpoint_exported ? "true" : "false") << '}';
  }
  output << "],\n"
         << "  \"totalNewRoots\":" << result.ledger.total_new_roots
         << ",\n"
         << "  \"totalReanalysedRoots\":"
         << result.ledger.total_reanalysed_roots << ",\n"
         << "  \"checkpointExports\":"
         << result.ledger.checkpoint_exports << ",\n"
         << "  \"deployedRound\":" << result.ledger.deployed_round
         << ",\n"
         << "  \"deploymentQualified\":"
         << (result.deployment_qualified ? "true" : "false") << ",\n"
         << "  \"checkpointSelection\":false,\n"
         << "  \"round8CheckpointBytes\":"
         << result.round8_checkpoint.size() << ",\n"
         << "  \"finalReplayRecords\":" << result.replay.size() << "\n"
         << "}\n";
  return output.str();
}

void appendGateJson(std::ostringstream& output, const GateResult& result) {
  output << "{\"candidate\":{\"games\":" << result.candidate.games
         << ",\"meanScore\":" << result.candidate.mean_score
         << ",\"meanMoves\":" << result.candidate.mean_moves
         << ",\"bottomQuartileMoves\":"
         << result.candidate.bottom_quartile_moves
         << ",\"clearsPerMove\":" << result.candidate.clears_per_move
         << ",\"revealsPerMove\":" << result.candidate.reveals_per_move
         << ",\"bootstrapLower95Score\":"
         << result.candidate.bootstrap_lower95_score << "},\"d4\":{\"games\":"
         << result.d4.games << ",\"meanScore\":" << result.d4.mean_score
         << ",\"meanMoves\":" << result.d4.mean_moves
         << "},\"baselineAttempted\":"
         << (result.baseline_attempted ? "true" : "false")
         << ",\"jointWins\":" << result.joint_wins
         << ",\"passed\":" << (result.passed ? "true" : "false") << '}';
}

std::string gateSequenceJson(const GateSequenceResult& result) {
  std::ostringstream output;
  output << std::setprecision(12)
         << "{\n"
         << "  \"format\":\"drop7-regenerative-gate-sequence-v3\",\n"
         << "  \"failStop\":true,\n"
         << "  \"deploymentPly\":" << kDeploymentMaximumPly << ",\n"
         << "  \"candidateAbsoluteBeforeBaseline\":true,\n"
         << "  \"attempted\":[";
  for (std::size_t index = 0; index < result.attempted.size(); ++index) {
    if (index != 0) output << ',';
    appendGateJson(output, result.attempted[index]);
  }
  output << "],\n"
         << "  \"passedAll\":" << (result.passed_all ? "true" : "false")
         << "\n}\n";
  return output.str();
}

}  // namespace drop7::regenerative_expert_iteration

#ifndef DROP7_REGENERATIVE_EXPERT_ITERATION_LIBRARY
int main(int argc, char** argv) {
  using namespace drop7::regenerative_expert_iteration;
  try {
    const std::string command = argc > 1 ? argv[1] : "self-test";
    if (command == "preregister") {
      const std::string output =
          argc > 2 ? argv[2] : "/tmp/drop7-regenerative-expert-prereg.json";
      writeText(output, preregistrationJson());
      std::cout << preregistrationJson();
      return 0;
    }
    if (command == "self-test") {
      const std::string output = argc > 2
                                     ? argv[2]
                                     : "/tmp/drop7-regenerative-expert-self-test.json";
      const std::string checkpoint =
          argc > 3 ? argv[3] : "/tmp/drop7-regenerative-expert-golden.bin";
      const SelfTestResult result = runSelfTests(checkpoint);
      const std::string json = selfTestJson(result);
      writeText(output, json);
      std::cout << json;
      return 0;
    }
    if (command == "preflight") {
      const std::string output =
          argc > 2 ? argv[2]
                   : "/tmp/drop7-regenerative-expert-preflight.json";
      const int roots = argc > 3 ? std::stoi(argv[3]) : 2;
      const PerformanceProjection result =
          performancePreflight(std::string(kBurnedCorpusPath), roots);
      const std::string json = performanceJson(result);
      writeText(output, json);
      std::cout << json;
      return result.admitted ? 0 : 2;
    }
    if (command == "initialize-d4") {
      if (argc != 5) {
        throw std::invalid_argument(
            "initialize-d4 TOKEN REPLAY_PATH OUTPUT_JSON");
      }
      requireFreshExecutionToken(argv[2]);
      const ReplayBuffer replay = runD4Initialization();
      writeBytes(argv[3], serializeReplay(replay));
      const std::string json = initializationJson(replay);
      writeText(argv[4], json);
      std::cout << json;
      return 0;
    }
    if (command == "iterate") {
      if (argc != 8) {
        throw std::invalid_argument(
            "iterate TOKEN INPUT_REPLAY ROUND8_CHECKPOINT UPDATED_REPLAY RESUME_STATE OUTPUT_JSON");
      }
      requireFreshExecutionToken(argv[2]);
      if (std::filesystem::exists(argv[4])) {
        throw std::invalid_argument(
            "round-8 checkpoint output must be a new path so fallback cannot leave a stale candidate");
      }
      ReplayBuffer replay = deserializeReplay(readBytes(argv[3]));
      const ResumeBoundaryCallback persist = [&](const IterationResumeState& state) {
        writeBytes(argv[6], serializeResumeState(state));
      };
      IterationResult result =
          runExpertIteration(std::move(replay), persist);
      if (result.deployment_qualified) {
        writeBytes(argv[4], result.round8_checkpoint);
      }
      writeBytes(argv[5], serializeReplay(result.replay));
      const std::string json = iterationJson(result);
      writeText(argv[7], json);
      std::cout << json;
      return result.deployment_qualified ? 0 : 2;
    }
    if (command == "resume-iterate") {
      if (argc != 7) {
        throw std::invalid_argument(
            "resume-iterate TOKEN RESUME_STATE ROUND8_CHECKPOINT UPDATED_REPLAY OUTPUT_JSON");
      }
      requireFreshExecutionToken(argv[2]);
      if (std::filesystem::exists(argv[4])) {
        throw std::invalid_argument(
            "round-8 checkpoint output must be a new path so fallback cannot leave a stale candidate");
      }
      IterationResumeState state =
          deserializeResumeState(readBytes(argv[3]));
      const ResumeBoundaryCallback persist = [&](const IterationResumeState& value) {
        writeBytes(argv[3], serializeResumeState(value));
      };
      IterationResult result = runExpertIteration(std::move(state), persist);
      if (result.deployment_qualified) {
        writeBytes(argv[4], result.round8_checkpoint);
      }
      writeBytes(argv[5], serializeReplay(result.replay));
      const std::string json = iterationJson(result);
      writeText(argv[6], json);
      std::cout << json;
      return result.deployment_qualified ? 0 : 2;
    }
    if (command == "gates") {
      if (argc != 5) {
        throw std::invalid_argument("gates TOKEN ROUND8_CHECKPOINT OUTPUT_JSON");
      }
      requireFreshExecutionToken(argv[2]);
      const std::vector<std::uint8_t> checkpoint = readBytes(argv[3]);
      const GateSequenceResult result = runGateSequence(checkpoint);
      const std::string json = gateSequenceJson(result);
      writeText(argv[4], json);
      std::cout << json;
      return result.passed_all ? 0 : 2;
    }
    throw std::invalid_argument(
        "expected preregister, self-test, preflight, initialize-d4, iterate, resume-iterate, or gates command");
  } catch (const std::exception& error) {
    std::cerr << "drop7 regenerative expert iteration: " << error.what()
              << '\n';
    return 1;
  }
}
#endif
