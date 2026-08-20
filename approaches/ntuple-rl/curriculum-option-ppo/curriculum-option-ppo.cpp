#define DROP7_VIABILITY_RESERVOIR_CONTROLLER_LIBRARY
#include "../../constructive-reservoir/viability-controller/viability-reservoir-controller.cpp"
#undef DROP7_VIABILITY_RESERVOIR_CONTROLLER_LIBRARY

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

// Trains a public-state option/reservoir PPO policy using normalized exact
// fair-D1 root-Q logits as its backbone.  A small two-pass MLP adds only an
// exactly reflection-equivariant residual.  With a zero residual head, greedy
// play is bit-for-bit identical to the fixed fair-D1 action policy.
namespace drop7::curriculum_option_ppo {

namespace vr = drop7::viability_reservoir_controller;
namespace fair = drop7::fair_only_horizon;
namespace detail = drop7::cfpi::detail;

using Clock = std::chrono::steady_clock;
using PublicState = vr::PublicState;

constexpr std::uint32_t kTrainingSeedStart = 0x3d67'0000u;
constexpr std::uint32_t kTrainingSeedEndExclusive = 0x3d67'8000u;
constexpr std::uint32_t kStageASeedStart = 0x3d68'0000u;
constexpr std::uint32_t kStageASeedEndExclusive = 0x3d68'0020u;
constexpr int kIterations = 64;
constexpr int kEpisodesPerIteration = 512;
constexpr int kInitialEpisodesPerIteration = kEpisodesPerIteration / 2;
constexpr int kCurriculumEpisodesPerIteration = kEpisodesPerIteration / 2;
constexpr int kTrainingEpisodes = kIterations * kEpisodesPerIteration;
constexpr int kInitialMaximumMoves = 1'000;
constexpr int kCurriculumHorizon = 100;
constexpr int kStageAGames = 32;
constexpr int kStageAMaximumMoves = 1'000;
constexpr int kPpoEpochs = 4;
constexpr int kMinibatch = 512;
constexpr int kMaximumThreads = 8;
constexpr int kExpectedCurriculumStates = 4'096;

constexpr float kGamma = 0.999f;
constexpr float kGaeLambda = 0.97f;
constexpr float kClipRatio = 0.20f;
constexpr float kEntropyCoefficient = 0.01f;
constexpr float kValueCoefficient = 0.25f;
constexpr float kGradientNorm = 0.50f;
constexpr float kLearningRate = 0.0001f;
constexpr float kBaseLogitScale = 6.0f;
constexpr float kSurvivalReward = 0.05f;
constexpr float kClearReward = 0.05f;
constexpr float kRevealReward = 0.15f;
constexpr float kTerminalReward = -5.0f;
constexpr double kWallLimitSeconds = 60.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;

constexpr double kGateMeanScore = 700'000.0;
constexpr double kGateMeanMoves = 200.0;
constexpr double kGateBottomQuartileMoves = 120.0;
constexpr double kGateClearsPerMove = 2.20;
constexpr double kGateRevealsPerMove = 1.20;
constexpr int kGateJointWins = 24;

constexpr std::uint32_t kNetworkSeed = 0x4355'5231u;  // "CUR1"
constexpr std::uint32_t kPolicySampleDomain = 0x504f'4c59u;
constexpr std::uint32_t kCurriculumSelectDomain = 0x4355'5253u;
constexpr std::uint32_t kRestartStreamDomain = 0x5253'5452u;
constexpr std::uint32_t kRestartRevealDomain = 0x5256'4c32u;
constexpr std::uint32_t kRestartDiscDomain = 0x4449'5342u;
constexpr std::uint32_t kRestartEventMultiplier = 0x9e37'79b9u;
constexpr std::uint32_t kShuffleSeed = 0x5050'4f31u;
constexpr std::uint64_t kCheckpointMagic = 0x4437'4355'5250'5031ull;
constexpr std::uint32_t kCheckpointVersion = 1;

constexpr int kBoardCategories = 10;
constexpr int kBoardInputs = kCellCount * kBoardCategories;
constexpr int kNextDiscInputs = kBoardSize;
constexpr int kPhaseInputs = kMovesPerLevel;
constexpr int kHeightInputs = kBoardSize;
constexpr int kGraphInputs = 20;
constexpr int kTriggerSummaryInputs = 9;
constexpr int kOptionInputs =
    static_cast<int>(vr::OptionMode::kCount);
constexpr int kKeyInputs = 6;
constexpr int kTriggerKeyInputs =
    kBoardSize * kBoardSize * kKeyInputs;
constexpr int kInputSize = kBoardInputs + kNextDiscInputs + kPhaseInputs +
                           kHeightInputs + kGraphInputs +
                           kTriggerSummaryInputs + kOptionInputs +
                           kTriggerKeyInputs;
constexpr int kHidden1 = 64;
constexpr int kHidden2 = 64;

struct Layout {
  static constexpr int w1 = 0;
  static constexpr int b1 = w1 + kHidden1 * kInputSize;
  static constexpr int w2 = b1 + kHidden1;
  static constexpr int b2 = w2 + kHidden2 * kHidden1;
  static constexpr int policy_w = b2 + kHidden2;
  static constexpr int policy_b = policy_w + kBoardSize * kHidden2;
  static constexpr int value_w = policy_b + kBoardSize;
  static constexpr int value_b = value_w + kHidden2;
  static constexpr int count = value_b + 1;
};

static_assert(kLevelBonus == 17'000);
static_assert(kInputSize == 837);
static_assert(Layout::count == 58'312);
static_assert(Layout::count < 200'000);
static_assert(kTrainingEpisodes == 32'768);
static_assert(kTrainingSeedEndExclusive - kTrainingSeedStart ==
              kTrainingEpisodes);
static_assert(kStageASeedEndExclusive - kStageASeedStart == kStageAGames);
static_assert((kTrainingSeedStart >> 16u) == 0x3d67u);
static_assert((kTrainingSeedEndExclusive - 1u) >> 16u == 0x3d67u);
static_assert((kStageASeedStart >> 16u) == 0x3d68u);
static_assert((kStageASeedEndExclusive - 1u) >> 16u == 0x3d68u);
static_assert((kTrainingSeedStart >> 24u) != 0x4du &&
              (kTrainingSeedStart >> 24u) != 0x7du &&
              (kTrainingSeedStart >> 24u) != 0xd7u);
static_assert(kInitialEpisodesPerIteration ==
              kCurriculumEpisodesPerIteration);

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
    throw std::runtime_error("curriculum PPO exceeded 256 MiB RSS");
  }
}

struct Deadline {
  Clock::time_point started = Clock::now();

  double elapsedSeconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }

  void check() const {
    if (elapsedSeconds() > kWallLimitSeconds) {
      throw std::runtime_error("curriculum PPO exceeded 60 minute wall cap");
    }
  }
};

enum class SeedUse : std::uint8_t { kTraining, kStageA };

bool allowedSeed(std::uint32_t seed, SeedUse use) {
  const std::uint32_t start =
      use == SeedUse::kTraining ? kTrainingSeedStart : kStageASeedStart;
  const std::uint32_t end = use == SeedUse::kTraining
                                ? kTrainingSeedEndExclusive
                                : kStageASeedEndExclusive;
  return seed >= start && seed < end && (seed >> 24u) != 0x4du &&
         (seed >> 24u) != 0x7du && (seed >> 24u) != 0xd7u;
}

void requireSeed(std::uint32_t seed, SeedUse use) {
  if (!allowedSeed(seed, use)) {
    throw std::invalid_argument("seed outside preregistered curriculum PPO lane");
  }
}

struct Curriculum {
  std::vector<PublicState> states;
  std::uint64_t fingerprint = 0xcbf2'9ce4'8422'2325ull;
};

std::string parseQuotedField(const std::string& line,
                             std::string_view field) {
  const std::string needle = "\"" + std::string(field) + "\":\"";
  const std::size_t begin = line.find(needle);
  if (begin == std::string::npos) {
    throw std::runtime_error("curriculum line missing quoted field");
  }
  const std::size_t value_begin = begin + needle.size();
  const std::size_t end = line.find('\"', value_begin);
  if (end == std::string::npos) {
    throw std::runtime_error("curriculum quoted field is unterminated");
  }
  return line.substr(value_begin, end - value_begin);
}

int parseIntegerField(const std::string& line, std::string_view field) {
  const std::string needle = "\"" + std::string(field) + "\":";
  const std::size_t begin = line.find(needle);
  if (begin == std::string::npos) {
    throw std::runtime_error("curriculum line missing integer field");
  }
  std::size_t consumed = 0;
  const int value = std::stoi(line.substr(begin + needle.size()), &consumed);
  if (consumed == 0) throw std::runtime_error("empty curriculum integer");
  return value;
}

void fingerprintByte(std::uint64_t& hash, std::uint8_t value) {
  hash ^= value;
  hash *= 0x0000'0100'0000'01b3ull;
}

Curriculum loadCurriculum(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("could not open public curriculum");
  Curriculum result;
  result.states.reserve(kExpectedCurriculumStates);
  std::unordered_set<std::string> unique;
  unique.reserve(kExpectedCurriculumStates * 2);
  std::string line;
  while (std::getline(input, line)) {
    if (parseQuotedField(line, "format") != "drop7-public-restart-v1" ||
        line.find("\"independentRestartValidated\":true") ==
            std::string::npos) {
      throw std::runtime_error("unvalidated curriculum record");
    }
    const std::size_t state_begin = line.find("\"state\":{");
    const std::size_t state_end = line.find('}', state_begin);
    if (state_begin == std::string::npos || state_end == std::string::npos) {
      throw std::runtime_error("curriculum state object is malformed");
    }
    const std::string state_object =
        line.substr(state_begin, state_end - state_begin + 1);
    for (const std::string_view forbidden :
         {"score", "level", "movesPlayed", "gameSeed", "future"}) {
      if (state_object.find(forbidden) != std::string::npos) {
        throw std::runtime_error("curriculum retained forbidden metadata");
      }
    }
    const std::string board_text = parseQuotedField(state_object, "board");
    if (board_text.size() != kCellCount) {
      throw std::runtime_error("curriculum board has wrong size");
    }
    PublicState state;
    for (int cell = 0; cell < kCellCount; ++cell) {
      const char token = board_text[static_cast<std::size_t>(cell)];
      if (token < '0' || token > '9') {
        throw std::runtime_error("curriculum board token is invalid");
      }
      state.board[cell] = static_cast<std::uint8_t>(token - '0');
    }
    state.next_disc =
        static_cast<std::uint8_t>(parseIntegerField(state_object, "nextDisc"));
    state.moves_remaining = static_cast<std::uint8_t>(
        parseIntegerField(state_object, "movesRemaining"));
    state.terminal = false;
    const State materialized = vr::materialize(state);
    static_cast<void>(vr::publicState(materialized));
    int legal_count = 0;
    legalColumns(state.board, legal_count);
    int popper_count = 0;
    findPoppers(state.board, popper_count);
    if (legal_count == 0 || popper_count != 0) {
      throw std::runtime_error("curriculum state is terminal or unstable");
    }
    std::string key = serializeBoard(state.board);
    key.push_back(static_cast<char>(state.next_disc));
    key.push_back(static_cast<char>(state.moves_remaining));
    if (!unique.insert(key).second) {
      throw std::runtime_error("duplicate public curriculum state");
    }
    for (const std::uint8_t cell : state.board) {
      fingerprintByte(result.fingerprint, cell);
    }
    fingerprintByte(result.fingerprint, state.next_disc);
    fingerprintByte(result.fingerprint, state.moves_remaining);
    result.states.push_back(state);
  }
  if (!input.eof()) throw std::runtime_error("failed reading curriculum");
  if (result.states.size() != kExpectedCurriculumStates) {
    throw std::runtime_error("unexpected public curriculum state count");
  }
  return result;
}

constexpr int kBoardOffset = 0;
constexpr int kNextOffset = kBoardOffset + kBoardInputs;
constexpr int kPhaseOffset = kNextOffset + kNextDiscInputs;
constexpr int kHeightOffset = kPhaseOffset + kPhaseInputs;
constexpr int kGraphOffset = kHeightOffset + kHeightInputs;
constexpr int kTriggerSummaryOffset = kGraphOffset + kGraphInputs;
constexpr int kOptionOffset =
    kTriggerSummaryOffset + kTriggerSummaryInputs;
constexpr int kKeysOffset = kOptionOffset + kOptionInputs;
static_assert(kKeysOffset + kTriggerKeyInputs == kInputSize);

struct Observation {
  std::array<float, kInputSize> input{};
  std::uint8_t legal_mask = 0;

  bool operator==(const Observation&) const = default;
};

float clippedRatio(int value, float denominator, float bound = 2.0f) {
  return std::clamp(static_cast<float>(value) / denominator, -bound, bound);
}

Observation observePublic(const PublicState& state) {
  if (state.terminal) throw std::invalid_argument("cannot observe terminal state");
  Observation result;
  for (int cell = 0; cell < kCellCount; ++cell) {
    const std::uint8_t token = state.board[cell];
    if (token >= kBoardCategories) {
      throw std::invalid_argument("observation board token out of range");
    }
    result.input[kBoardOffset + cell * kBoardCategories + token] = 1.0f;
  }
  result.input[kNextOffset + state.next_disc - 1] = 1.0f;
  result.input[kPhaseOffset + state.moves_remaining - 1] = 1.0f;
  const auto heights = vr::columnHeights(state.board);
  for (int column = 0; column < kBoardSize; ++column) {
    result.input[kHeightOffset + column] = heights[column] / 7.0f;
    if (isLegal(state.board, column)) {
      result.legal_mask |= static_cast<std::uint8_t>(1u << column);
    }
  }

  const vr::CertificateGraph graph = vr::buildCertificateGraph(state.board);
  const vr::GraphStats& g = graph.stats;
  const std::array<float, kGraphInputs> graph_values{{
      g.occupied / 49.0f,
      g.maximum_height / 7.0f,
      g.open_columns / 7.0f,
      g.solid_cells / 49.0f,
      g.cracked_cells / 49.0f,
      g.numbered_cells / 49.0f,
      g.cover_altitude_debt / 7'203.0f,
      g.edge_cover_debt / 2'058.0f,
      g.frontier_access / 1'200.0f,
      g.stored_mass / 5'000.0f,
      g.release_ready / 98.0f,
      g.same_target_pairs / 147.0f,
      g.adjacent_ones / 84.0f,
      g.triple_twos / 70.0f,
      g.dead_low_numbers / 49.0f,
      g.capped_low_columns / 7.0f,
      g.clog_debt / 1'000.0f,
      g.line_edges / 512.0f,
      g.support_edges / 512.0f,
      g.frontier_edges / 512.0f,
  }};
  std::copy(graph_values.begin(), graph_values.end(),
            result.input.begin() + kGraphOffset);

  std::uint64_t ignored_work = 0;
  const vr::TriggerMatrix triggers = vr::buildTriggerMatrix(state, ignored_work);
  const vr::TriggerSummary& t = triggers.summary;
  const std::array<float, kTriggerSummaryInputs> trigger_values{{
      t.worst_safe_columns / 7.0f,
      t.worst_productive_columns / 7.0f,
      t.worst_best_release / 256.0f,
      clippedRatio(t.worst_best_quality, 32'768.0f),
      t.total_safe_columns / 49.0f,
      t.total_productive_columns / 49.0f,
      t.total_best_release / 1'792.0f,
      clippedRatio(t.total_best_quality, 229'376.0f),
      t.strong_keys / 49.0f,
  }};
  std::copy(trigger_values.begin(), trigger_values.end(),
            result.input.begin() + kTriggerSummaryOffset);
  const vr::OptionMode option = vr::selectOption(state, graph, triggers);
  result.input[kOptionOffset + static_cast<int>(option)] = 1.0f;
  for (int disc = 0; disc < kBoardSize; ++disc) {
    for (int column = 0; column < kBoardSize; ++column) {
      const vr::KeyCertificate& key = triggers.keys[disc][column];
      const int offset =
          kKeysOffset + (disc * kBoardSize + column) * kKeyInputs;
      result.input[offset + 0] = key.survives ? 1.0f : 0.0f;
      result.input[offset + 1] = key.clears / 10.0f;
      result.input[offset + 2] = (key.reveals + key.cracks) / 5.0f;
      result.input[offset + 3] = key.waves / 5.0f;
      result.input[offset + 4] = clippedRatio(key.build_gain, 512.0f);
      result.input[offset + 5] = clippedRatio(key.clog_improvement, 64.0f);
    }
  }
  return result;
}

Observation mirrorObservation(const Observation& source) {
  Observation result;
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      const int mirrored_column = kBoardSize - 1 - column;
      for (int category = 0; category < kBoardCategories; ++category) {
        result.input[kBoardOffset + indexOf(row, mirrored_column) *
                                        kBoardCategories + category] =
            source.input[kBoardOffset + indexOf(row, column) *
                                           kBoardCategories + category];
      }
    }
  }
  std::copy(source.input.begin() + kNextOffset,
            source.input.begin() + kHeightOffset,
            result.input.begin() + kNextOffset);
  for (int column = 0; column < kBoardSize; ++column) {
    result.input[kHeightOffset + kBoardSize - 1 - column] =
        source.input[kHeightOffset + column];
  }
  std::copy(source.input.begin() + kGraphOffset,
            source.input.begin() + kKeysOffset,
            result.input.begin() + kGraphOffset);
  for (int disc = 0; disc < kBoardSize; ++disc) {
    for (int column = 0; column < kBoardSize; ++column) {
      const int source_offset =
          kKeysOffset + (disc * kBoardSize + column) * kKeyInputs;
      const int target_offset =
          kKeysOffset +
          (disc * kBoardSize + (kBoardSize - 1 - column)) * kKeyInputs;
      std::copy(source.input.begin() + source_offset,
                source.input.begin() + source_offset + kKeyInputs,
                result.input.begin() + target_offset);
    }
  }
  for (int column = 0; column < kBoardSize; ++column) {
    if ((source.legal_mask & (1u << column)) != 0) {
      result.legal_mask |=
          static_cast<std::uint8_t>(1u << (kBoardSize - 1 - column));
    }
  }
  return result;
}

struct BasePolicy {
  std::array<float, kBoardSize> logits{};
  std::array<double, kBoardSize> root_values{};
  int action = -1;
  std::uint64_t work = 0;

  bool operator==(const BasePolicy&) const = default;
};

BasePolicy fairBasePolicy(const PublicState& canonical) {
  if (canonical.terminal) return {};
  State state = vr::materialize(canonical);
  fair::SearchContext context;
  const fair::RootEvaluation root = fair::rootDecision(state, 1, context);
  BasePolicy result;
  result.logits.fill(-std::numeric_limits<float>::infinity());
  result.root_values = root.values;
  result.action = root.action;
  result.work = context.work;
  if (result.action < 0 || context.work > 70 || !context.cache.empty()) {
    throw std::runtime_error("fair D1 base failed exact completion");
  }
  double mean = 0.0;
  int count = 0;
  for (int action = 0; action < kBoardSize; ++action) {
    if (!isLegal(canonical.board, action)) continue;
    mean += root.values[action];
    ++count;
  }
  mean /= count;
  double variance = 0.0;
  for (int action = 0; action < kBoardSize; ++action) {
    if (!isLegal(canonical.board, action)) continue;
    const double difference = root.values[action] - mean;
    variance += difference * difference;
  }
  const double scale = std::sqrt(variance / count);
  for (int action = 0; action < kBoardSize; ++action) {
    if (!isLegal(canonical.board, action)) continue;
    const double normalized =
        scale > 1.0e-12 ? (root.values[action] - mean) / scale : 0.0;
    int tie_priority = 0;
    for (int order = 0; order < kBoardSize; ++order) {
      if (vr::kColumnOrder[order] == action) tie_priority = kBoardSize - order;
    }
    result.logits[action] = static_cast<float>(
        kBaseLogitScale * normalized + tie_priority * 1.0e-6);
  }
  return result;
}

struct BranchCache {
  std::array<float, kHidden1> hidden1{};
  std::array<float, kHidden2> hidden2{};
  std::array<float, kBoardSize> residual{};
  float value = 0.0f;
};

class Network {
 public:
  explicit Network(std::uint32_t seed = kNetworkSeed)
      : parameters_(Layout::count, 0.0f),
        first_moment_(Layout::count, 0.0f),
        second_moment_(Layout::count, 0.0f) {
    Mulberry32 random(seed);
    initializeXavier(random, Layout::w1, kHidden1, kInputSize);
    initializeXavier(random, Layout::w2, kHidden2, kHidden1);
    // Both output heads are exactly zero.  In particular, every policy
    // residual is zero while the fair-D1 logits remain fully operative.
  }

  BranchCache forward(const Observation& observation) const {
    BranchCache cache;
    for (int output = 0; output < kHidden1; ++output) {
      float total = parameters_[Layout::b1 + output];
      const int weights = Layout::w1 + output * kInputSize;
      for (int input = 0; input < kInputSize; ++input) {
        total += parameters_[weights + input] * observation.input[input];
      }
      cache.hidden1[output] = std::tanh(total);
    }
    for (int output = 0; output < kHidden2; ++output) {
      float total = parameters_[Layout::b2 + output];
      const int weights = Layout::w2 + output * kHidden1;
      for (int input = 0; input < kHidden1; ++input) {
        total += parameters_[weights + input] * cache.hidden1[input];
      }
      cache.hidden2[output] = std::tanh(total);
    }
    for (int action = 0; action < kBoardSize; ++action) {
      float total = parameters_[Layout::policy_b + action];
      const int weights = Layout::policy_w + action * kHidden2;
      for (int input = 0; input < kHidden2; ++input) {
        total += parameters_[weights + input] * cache.hidden2[input];
      }
      cache.residual[action] = total;
    }
    cache.value = parameters_[Layout::value_b];
    for (int input = 0; input < kHidden2; ++input) {
      cache.value += parameters_[Layout::value_w + input] *
                     cache.hidden2[input];
    }
    return cache;
  }

  std::vector<float> zeroGradient() const {
    return std::vector<float>(Layout::count, 0.0f);
  }

  void accumulateBranchGradient(
      const Observation& observation, const BranchCache& cache,
      const std::array<float, kBoardSize>& residual_gradient,
      float value_gradient, std::vector<float>& gradient) const {
    std::array<float, kHidden2> hidden2_gradient{};
    for (int action = 0; action < kBoardSize; ++action) {
      const float derivative = residual_gradient[action];
      gradient[Layout::policy_b + action] += derivative;
      const int weights = Layout::policy_w + action * kHidden2;
      for (int input = 0; input < kHidden2; ++input) {
        gradient[weights + input] += derivative * cache.hidden2[input];
        hidden2_gradient[input] += derivative * parameters_[weights + input];
      }
    }
    gradient[Layout::value_b] += value_gradient;
    for (int input = 0; input < kHidden2; ++input) {
      gradient[Layout::value_w + input] +=
          value_gradient * cache.hidden2[input];
      hidden2_gradient[input] +=
          value_gradient * parameters_[Layout::value_w + input];
    }

    std::array<float, kHidden1> hidden1_gradient{};
    for (int output = 0; output < kHidden2; ++output) {
      const float derivative = hidden2_gradient[output] *
                               (1.0f - cache.hidden2[output] *
                                           cache.hidden2[output]);
      gradient[Layout::b2 + output] += derivative;
      const int weights = Layout::w2 + output * kHidden1;
      for (int input = 0; input < kHidden1; ++input) {
        gradient[weights + input] += derivative * cache.hidden1[input];
        hidden1_gradient[input] += derivative * parameters_[weights + input];
      }
    }
    for (int output = 0; output < kHidden1; ++output) {
      const float derivative = hidden1_gradient[output] *
                               (1.0f - cache.hidden1[output] *
                                           cache.hidden1[output]);
      gradient[Layout::b1 + output] += derivative;
      const int weights = Layout::w1 + output * kInputSize;
      for (int input = 0; input < kInputSize; ++input) {
        gradient[weights + input] += derivative * observation.input[input];
      }
    }
  }

  void applyAdam(std::vector<float>& gradient, float learning_rate,
                 float maximum_norm) {
    double squared_norm = 0.0;
    for (const float value : gradient) squared_norm += value * value;
    const double norm = std::sqrt(squared_norm);
    const float scale = norm > maximum_norm
                            ? static_cast<float>(maximum_norm / norm)
                            : 1.0f;
    ++adam_step_;
    constexpr float beta1 = 0.9f;
    constexpr float beta2 = 0.999f;
    constexpr float epsilon = 1.0e-8f;
    const float first_correction =
        1.0f - std::pow(beta1, static_cast<float>(adam_step_));
    const float second_correction =
        1.0f - std::pow(beta2, static_cast<float>(adam_step_));
    for (int index = 0; index < Layout::count; ++index) {
      const float value = gradient[index] * scale;
      first_moment_[index] =
          beta1 * first_moment_[index] + (1.0f - beta1) * value;
      second_moment_[index] = beta2 * second_moment_[index] +
                              (1.0f - beta2) * value * value;
      const float corrected_first = first_moment_[index] / first_correction;
      const float corrected_second = second_moment_[index] / second_correction;
      parameters_[index] -= learning_rate * corrected_first /
                            (std::sqrt(corrected_second) + epsilon);
      if (!std::isfinite(parameters_[index])) {
        throw std::runtime_error("non-finite curriculum PPO parameter");
      }
    }
  }

  bool residualIsZero() const {
    for (int index = Layout::policy_w; index < Layout::value_w; ++index) {
      if (parameters_[index] != 0.0f) return false;
    }
    return true;
  }

  float parameter(int index) const { return parameters_.at(index); }
  void setParameter(int index, float value) { parameters_.at(index) = value; }
  const std::vector<float>& parameters() const { return parameters_; }

  void setParameters(const std::vector<float>& source) {
    if (source.size() != parameters_.size()) {
      throw std::invalid_argument("checkpoint parameter count mismatch");
    }
    parameters_ = source;
    std::fill(first_moment_.begin(), first_moment_.end(), 0.0f);
    std::fill(second_moment_.begin(), second_moment_.end(), 0.0f);
    adam_step_ = 0;
  }

 private:
  void initializeXavier(Mulberry32& random, int offset, int outputs,
                        int inputs) {
    const float radius = std::sqrt(6.0f / (inputs + outputs));
    for (int index = 0; index < outputs * inputs; ++index) {
      parameters_[offset + index] = static_cast<float>(
          (2.0 * random.nextUnit() - 1.0) * radius);
    }
  }

  std::vector<float> parameters_;
  std::vector<float> first_moment_;
  std::vector<float> second_moment_;
  std::uint64_t adam_step_ = 0;
};

struct Prediction {
  Observation direct_observation{};
  Observation mirrored_observation{};
  BranchCache direct{};
  BranchCache mirrored{};
  std::array<float, kBoardSize> base_logits{};
  std::array<float, kBoardSize> logits{};
  std::array<float, kBoardSize> probabilities{};
  float value = 0.0f;
  int base_action = -1;
};

Prediction predictCanonical(const Network& network,
                            const PublicState& canonical,
                            const BasePolicy* saved_base = nullptr) {
  Prediction result;
  result.direct_observation = observePublic(canonical);
  result.mirrored_observation = mirrorObservation(result.direct_observation);
  result.direct = network.forward(result.direct_observation);
  result.mirrored = network.forward(result.mirrored_observation);
  const BasePolicy computed = saved_base ? BasePolicy{} : fairBasePolicy(canonical);
  if (saved_base) {
    result.base_logits = saved_base->logits;
    result.base_action = saved_base->action;
  } else {
    result.base_logits = computed.logits;
    result.base_action = computed.action;
  }
  float maximum = -std::numeric_limits<float>::infinity();
  for (int action = 0; action < kBoardSize; ++action) {
    if (!isLegal(canonical.board, action)) {
      result.logits[action] = -std::numeric_limits<float>::infinity();
      continue;
    }
    result.logits[action] =
        result.base_logits[action] +
        0.5f * (result.direct.residual[action] +
                result.mirrored.residual[kBoardSize - 1 - action]);
    maximum = std::max(maximum, result.logits[action]);
  }
  float denominator = 0.0f;
  for (int action = 0; action < kBoardSize; ++action) {
    if (!isLegal(canonical.board, action)) continue;
    result.probabilities[action] = std::exp(result.logits[action] - maximum);
    denominator += result.probabilities[action];
  }
  if (!(denominator > 0.0f)) {
    throw std::runtime_error("curriculum policy has no legal probability mass");
  }
  for (float& probability : result.probabilities) probability /= denominator;
  result.value = 0.5f * (result.direct.value + result.mirrored.value);
  return result;
}

int greedyCanonical(const Prediction& prediction) {
  int selected = -1;
  float best = -1.0f;
  for (const int action : vr::kColumnOrder) {
    if (prediction.probabilities[action] > best) {
      best = prediction.probabilities[action];
      selected = action;
    }
  }
  return selected;
}

int sampleCanonical(const Prediction& prediction, Mulberry32& random) {
  const double sample = random.nextUnit();
  double cumulative = 0.0;
  int fallback = -1;
  for (int action = 0; action < kBoardSize; ++action) {
    if (prediction.probabilities[action] <= 0.0f) continue;
    fallback = action;
    cumulative += prediction.probabilities[action];
    if (sample < cumulative) return action;
  }
  return fallback;
}

void accumulateEquivariantGradient(
    const Network& network, const Prediction& prediction, int action,
    float policy_coefficient, float value_derivative,
    float entropy_coefficient, std::vector<float>& gradient) {
  float entropy = 0.0f;
  for (const float probability : prediction.probabilities) {
    if (probability > 0.0f) entropy -= probability * std::log(probability);
  }
  std::array<float, kBoardSize> total_gradient{};
  for (int candidate = 0; candidate < kBoardSize; ++candidate) {
    const float probability = prediction.probabilities[candidate];
    if (probability <= 0.0f) continue;
    total_gradient[candidate] =
        policy_coefficient *
            ((candidate == action ? 1.0f : 0.0f) - probability) +
        entropy_coefficient * probability *
            (std::log(probability) + entropy);
  }
  std::array<float, kBoardSize> direct_gradient{};
  std::array<float, kBoardSize> mirrored_gradient{};
  for (int candidate = 0; candidate < kBoardSize; ++candidate) {
    direct_gradient[candidate] = 0.5f * total_gradient[candidate];
    mirrored_gradient[kBoardSize - 1 - candidate] =
        0.5f * total_gradient[candidate];
  }
  network.accumulateBranchGradient(
      prediction.direct_observation, prediction.direct, direct_gradient,
      value_derivative * 0.5f, gradient);
  network.accumulateBranchGradient(
      prediction.mirrored_observation, prediction.mirrored,
      mirrored_gradient, value_derivative * 0.5f, gradient);
}

struct PolicyDecision {
  int action = -1;
  int base_action = -1;
  std::array<float, kBoardSize> probabilities{};
  float value = 0.0f;

  bool operator==(const PolicyDecision&) const = default;
};

PolicyDecision chooseAction(const PublicState& source, const Network& network) {
  if (source.terminal) return {};
  bool mirrored = false;
  const PublicState canonical = vr::canonicalState(source, mirrored);
  const Prediction prediction = predictCanonical(network, canonical);
  const int canonical_action = greedyCanonical(prediction);
  PolicyDecision result;
  result.action = mirrored ? kBoardSize - 1 - canonical_action
                           : canonical_action;
  result.base_action = mirrored ? kBoardSize - 1 - prediction.base_action
                                : prediction.base_action;
  result.value = prediction.value;
  for (int action = 0; action < kBoardSize; ++action) {
    const int source_action = mirrored ? kBoardSize - 1 - action : action;
    result.probabilities[source_action] = prediction.probabilities[action];
  }
  return result;
}

using PublicPolicy = PolicyDecision (*)(const PublicState&, const Network&);
static_assert(std::is_same_v<decltype(&chooseAction), PublicPolicy>);
static_assert(!std::is_invocable_v<PublicPolicy, const State&, const Network&>);

void accumulateMoveCounts(const MoveResult& move, int& clears, int& reveals) {
  for (const Wave& wave : move.waves) {
    clears += wave.cleared;
    reveals += wave.revealed;
  }
}

std::uint32_t restartBaseSeed(std::uint32_t lane_seed,
                              std::size_t curriculum_index) {
  return mix32(lane_seed ^ kRestartStreamDomain ^
               (static_cast<std::uint32_t>(curriculum_index + 1u) *
                kRestartEventMultiplier));
}

std::uint8_t restartNextDisc(std::uint32_t base_seed, int event) {
  const std::uint32_t bits = mix32(
      base_seed ^ kRestartDiscDomain ^
      (static_cast<std::uint32_t>(event + 1) * kRestartEventMultiplier));
  return static_cast<std::uint8_t>(
      ((static_cast<std::uint64_t>(bits) * kBoardSize) >> 32u) + 1u);
}

bool playRestartMove(State& state, std::uint32_t base_seed, int event,
                     int action, MoveResult& move) {
  const std::uint32_t reveal_seed = mix32(
      base_seed ^ kRestartRevealDomain ^
      (static_cast<std::uint32_t>(event + 1) * kRestartEventMultiplier));
  Mulberry32 random(reveal_seed);
  if (!playMove(state, action, random, move)) return false;
  state = move.state;
  if (!state.game_over) state.next_disc = restartNextDisc(base_seed, event);
  return true;
}

struct Sample {
  PublicState state{};
  std::array<float, kBoardSize> base_logits{};
  int action = -1;
  float old_log_probability = 0.0f;
  float old_value = 0.0f;
  float reward = 0.0f;
  bool terminal = false;
  float advantage = 0.0f;
  float return_value = 0.0f;
};

static_assert(sizeof(Sample) <= 128);
constexpr std::size_t kMaximumBatchSamples =
    static_cast<std::size_t>(kEpisodesPerIteration) * kInitialMaximumMoves;
static_assert(kMaximumBatchSamples * sizeof(Sample) < 96ull * 1024ull * 1024ull);

struct Trajectory {
  std::vector<Sample> samples;
  std::int64_t score = 0;
  int moves = 0;
  int clears = 0;
  int reveals = 0;
  bool curriculum = false;
};

void finishAdvantages(std::vector<Sample>& samples, float bootstrap) {
  float next_value = bootstrap;
  float advantage = 0.0f;
  for (auto iterator = samples.rbegin(); iterator != samples.rend(); ++iterator) {
    const float nonterminal = iterator->terminal ? 0.0f : 1.0f;
    const float delta = iterator->reward + kGamma * next_value * nonterminal -
                        iterator->old_value;
    advantage = delta + kGamma * kGaeLambda * nonterminal * advantage;
    iterator->advantage = advantage;
    iterator->return_value = advantage + iterator->old_value;
    next_value = iterator->old_value;
  }
}

Trajectory collectTrajectory(const Network& network,
                             const Curriculum& curriculum,
                             std::uint32_t lane_seed, bool use_curriculum,
                             const Deadline& deadline) {
  requireSeed(lane_seed, SeedUse::kTraining);
  const std::size_t curriculum_index =
      static_cast<std::size_t>(mix32(lane_seed ^ kCurriculumSelectDomain)) %
      curriculum.states.size();
  State state = use_curriculum
                    ? vr::materialize(curriculum.states[curriculum_index])
                    : initialHeadlessState(lane_seed);
  state.score = 0;
  state.level = 1;
  state.moves_played = 0;
  const std::uint32_t restart_seed =
      restartBaseSeed(lane_seed, curriculum_index);
  Mulberry32 policy_random(mix32(lane_seed ^ kPolicySampleDomain));
  const int horizon =
      use_curriculum ? kCurriculumHorizon : kInitialMaximumMoves;
  Trajectory trajectory;
  trajectory.curriculum = use_curriculum;
  trajectory.samples.reserve(use_curriculum ? kCurriculumHorizon : 128);
  for (int event = 0; !state.game_over && event < horizon; ++event) {
    if ((event & 31) == 0) deadline.check();
    bool mirrored = false;
    const PublicState canonical =
        vr::canonicalState(vr::publicState(state), mirrored);
    const BasePolicy base = fairBasePolicy(canonical);
    const Prediction prediction = predictCanonical(network, canonical, &base);
    Sample sample;
    sample.state = canonical;
    sample.base_logits = base.logits;
    sample.action = sampleCanonical(prediction, policy_random);
    if (sample.action < 0) {
      throw std::runtime_error("curriculum PPO sampled no action");
    }
    sample.old_log_probability = std::log(std::max(
        1.0e-12f, prediction.probabilities[sample.action]));
    sample.old_value = prediction.value;
    const int physical_action =
        mirrored ? kBoardSize - 1 - sample.action : sample.action;
    MoveResult move;
    const bool played = use_curriculum
                            ? playRestartMove(state, restart_seed, event,
                                              physical_action, move)
                            : playHeadlessMove(state, lane_seed,
                                               physical_action, move);
    if (!played) throw std::runtime_error("PPO environment rejected action");
    int clears = 0;
    int reveals = 0;
    accumulateMoveCounts(move, clears, reveals);
    sample.terminal = state.game_over;
    sample.reward = static_cast<float>(move.score_delta) / 17'000.0f +
                    (sample.terminal ? 0.0f : kSurvivalReward) +
                    kClearReward * clears + kRevealReward * reveals +
                    (sample.terminal ? kTerminalReward : 0.0f);
    trajectory.clears += clears;
    trajectory.reveals += reveals;
    trajectory.samples.push_back(sample);
  }
  float bootstrap = 0.0f;
  if (!state.game_over) {
    bool ignored = false;
    const PublicState canonical =
        vr::canonicalState(vr::publicState(state), ignored);
    bootstrap = predictCanonical(network, canonical).value;
  }
  finishAdvantages(trajectory.samples, bootstrap);
  trajectory.score = state.score;
  trajectory.moves = static_cast<int>(trajectory.samples.size());
  return trajectory;
}

struct Batch {
  std::vector<Trajectory> trajectories;
  std::size_t samples = 0;
  double initial_score = 0.0;
  double initial_moves = 0.0;
  double curriculum_score = 0.0;
  double curriculum_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
};

Batch collectBatch(const Network& network, const Curriculum& curriculum,
                   int iteration, int threads, const Deadline& deadline) {
  Batch batch;
  batch.trajectories.resize(kEpisodesPerIteration);
  std::atomic<int> next{0};
  const int workers = std::min(threads, kEpisodesPerIteration);
  std::vector<std::future<void>> futures;
  futures.reserve(workers);
  for (int worker = 0; worker < workers; ++worker) {
    futures.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int episode = next.fetch_add(1);
        if (episode >= kEpisodesPerIteration) return;
        const int global_episode = iteration * kEpisodesPerIteration + episode;
        const std::uint32_t lane_seed =
            kTrainingSeedStart + static_cast<std::uint32_t>(global_episode);
        const bool use_curriculum = episode >= kInitialEpisodesPerIteration;
        batch.trajectories[episode] = collectTrajectory(
            network, curriculum, lane_seed, use_curriculum, deadline);
      }
    }));
  }
  for (auto& future : futures) future.get();
  std::int64_t total_moves = 0;
  std::int64_t total_clears = 0;
  std::int64_t total_reveals = 0;
  for (const Trajectory& trajectory : batch.trajectories) {
    batch.samples += trajectory.samples.size();
    total_moves += trajectory.moves;
    total_clears += trajectory.clears;
    total_reveals += trajectory.reveals;
    if (trajectory.curriculum) {
      batch.curriculum_score += trajectory.score;
      batch.curriculum_moves += trajectory.moves;
    } else {
      batch.initial_score += trajectory.score;
      batch.initial_moves += trajectory.moves;
    }
  }
  if (batch.samples > kMaximumBatchSamples || total_moves <= 0) {
    throw std::runtime_error("PPO batch exceeded static sample bound");
  }
  batch.initial_score /= kInitialEpisodesPerIteration;
  batch.initial_moves /= kInitialEpisodesPerIteration;
  batch.curriculum_score /= kCurriculumEpisodesPerIteration;
  batch.curriculum_moves /= kCurriculumEpisodesPerIteration;
  batch.clears_per_move =
      static_cast<double>(total_clears) / static_cast<double>(total_moves);
  batch.reveals_per_move =
      static_cast<double>(total_reveals) / static_cast<double>(total_moves);
  enforceRssLimit();
  return batch;
}

void deterministicShuffle(std::vector<Sample*>& values, Mulberry32& random) {
  for (std::size_t index = values.size(); index > 1; --index) {
    const std::size_t selected = static_cast<std::size_t>(
        (static_cast<std::uint64_t>(random.nextBits()) * index) >> 32u);
    std::swap(values[index - 1], values[selected]);
  }
}

float ppoPolicyCoefficient(float advantage, float ratio,
                           float inverse_batch) {
  const bool clipped =
      (advantage >= 0.0f && ratio > 1.0f + kClipRatio) ||
      (advantage < 0.0f && ratio < 1.0f - kClipRatio);
  return clipped ? 0.0f : -advantage * ratio * inverse_batch;
}

struct UpdateMetrics {
  double policy_loss = 0.0;
  double value_loss = 0.0;
  double entropy = 0.0;
  double approximate_kl = 0.0;
  double clip_fraction = 0.0;
  int updates = 0;
};

UpdateMetrics update(Network& network, Batch& batch,
                     Mulberry32& shuffle_random,
                     const Deadline& deadline) {
  std::vector<Sample*> samples;
  samples.reserve(batch.samples);
  for (Trajectory& trajectory : batch.trajectories) {
    for (Sample& sample : trajectory.samples) samples.push_back(&sample);
  }
  if (samples.empty()) throw std::runtime_error("empty PPO batch");
  double mean = 0.0;
  for (const Sample* sample : samples) mean += sample->advantage;
  mean /= samples.size();
  double variance = 0.0;
  for (const Sample* sample : samples) {
    const double difference = sample->advantage - mean;
    variance += difference * difference;
  }
  const float scale = static_cast<float>(
      1.0 / std::sqrt(variance / samples.size() + 1.0e-8));
  for (Sample* sample : samples) {
    sample->advantage =
        static_cast<float>((sample->advantage - mean) * scale);
  }

  UpdateMetrics metrics;
  std::uint64_t metric_samples = 0;
  for (int epoch = 0; epoch < kPpoEpochs; ++epoch) {
    deterministicShuffle(samples, shuffle_random);
    for (std::size_t begin = 0; begin < samples.size(); begin += kMinibatch) {
      if ((begin & 8'191u) == 0) {
        deadline.check();
        enforceRssLimit();
      }
      const std::size_t end = std::min(samples.size(), begin + kMinibatch);
      const float inverse_batch = 1.0f / static_cast<float>(end - begin);
      std::vector<float> gradient = network.zeroGradient();
      for (std::size_t offset = begin; offset < end; ++offset) {
        const Sample& sample = *samples[offset];
        BasePolicy saved;
        saved.logits = sample.base_logits;
        const Prediction prediction =
            predictCanonical(network, sample.state, &saved);
        const float probability = std::max(
            1.0e-12f, prediction.probabilities[sample.action]);
        const float log_probability = std::log(probability);
        const float ratio =
            std::exp(log_probability - sample.old_log_probability);
        const float clipped_ratio =
            std::clamp(ratio, 1.0f - kClipRatio, 1.0f + kClipRatio);
        const float raw_objective = ratio * sample.advantage;
        const float clipped_objective = clipped_ratio * sample.advantage;
        const float policy_coefficient =
            ppoPolicyCoefficient(sample.advantage, ratio, inverse_batch);
        const bool clipped = policy_coefficient == 0.0f &&
                             sample.advantage != 0.0f;
        const float value_difference =
            prediction.value - sample.return_value;
        const float value_derivative =
            2.0f * kValueCoefficient * value_difference * inverse_batch;
        accumulateEquivariantGradient(
            network, prediction, sample.action, policy_coefficient,
            value_derivative, kEntropyCoefficient * inverse_batch, gradient);
        float entropy = 0.0f;
        for (const float candidate : prediction.probabilities) {
          if (candidate > 0.0f) entropy -= candidate * std::log(candidate);
        }
        metrics.policy_loss -= std::min(raw_objective, clipped_objective);
        metrics.value_loss += 0.5 * value_difference * value_difference;
        metrics.entropy += entropy;
        metrics.approximate_kl +=
            sample.old_log_probability - log_probability;
        metrics.clip_fraction += clipped ? 1.0 : 0.0;
        ++metric_samples;
      }
      network.applyAdam(gradient, kLearningRate, kGradientNorm);
      ++metrics.updates;
    }
  }
  const double inverse = 1.0 / static_cast<double>(metric_samples);
  metrics.policy_loss *= inverse;
  metrics.value_loss *= inverse;
  metrics.entropy *= inverse;
  metrics.approximate_kl *= inverse;
  metrics.clip_fraction *= inverse;
  return metrics;
}

std::uint64_t modelFingerprint(const Network& network) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const float parameter : network.parameters()) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(parameter);
    for (int shift = 0; shift < 32; shift += 8) {
      fingerprintByte(hash,
                      static_cast<std::uint8_t>(bits >> shift));
    }
  }
  return hash;
}

void saveCheckpoint(const std::string& path, const Network& network) {
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("could not open PPO checkpoint");
  const std::uint64_t fingerprint = modelFingerprint(network);
  const std::uint32_t count = Layout::count;
  output.write(reinterpret_cast<const char*>(&kCheckpointMagic),
               sizeof(kCheckpointMagic));
  output.write(reinterpret_cast<const char*>(&kCheckpointVersion),
               sizeof(kCheckpointVersion));
  output.write(reinterpret_cast<const char*>(&count), sizeof(count));
  output.write(reinterpret_cast<const char*>(&fingerprint), sizeof(fingerprint));
  output.write(reinterpret_cast<const char*>(network.parameters().data()),
               static_cast<std::streamsize>(network.parameters().size() *
                                            sizeof(float)));
  if (!output) throw std::runtime_error("failed writing PPO checkpoint");
}

Network loadCheckpoint(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not open PPO checkpoint");
  std::uint64_t magic = 0;
  std::uint32_t version = 0;
  std::uint32_t count = 0;
  std::uint64_t expected_fingerprint = 0;
  input.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  input.read(reinterpret_cast<char*>(&version), sizeof(version));
  input.read(reinterpret_cast<char*>(&count), sizeof(count));
  input.read(reinterpret_cast<char*>(&expected_fingerprint),
             sizeof(expected_fingerprint));
  if (magic != kCheckpointMagic || version != kCheckpointVersion ||
      count != Layout::count) {
    throw std::runtime_error("invalid PPO checkpoint header");
  }
  std::vector<float> parameters(count);
  input.read(reinterpret_cast<char*>(parameters.data()),
             static_cast<std::streamsize>(parameters.size() * sizeof(float)));
  char trailing = 0;
  if (!input || input.read(&trailing, 1)) {
    throw std::runtime_error("invalid PPO checkpoint payload");
  }
  Network result;
  result.setParameters(parameters);
  if (modelFingerprint(result) != expected_fingerprint) {
    throw std::runtime_error("PPO checkpoint fingerprint mismatch");
  }
  return result;
}

struct TrainingRecord {
  int iteration = 0;
  std::size_t samples = 0;
  double initial_score = 0.0;
  double initial_moves = 0.0;
  double curriculum_score = 0.0;
  double curriculum_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  UpdateMetrics update{};
};

struct TrainingResult {
  Network network{};
  std::array<TrainingRecord, kIterations> records{};
};

TrainingResult train(const Curriculum& curriculum, int threads,
                     const Deadline& deadline) {
  TrainingResult result;
  Mulberry32 shuffle_random(kShuffleSeed);
  for (int iteration = 0; iteration < kIterations; ++iteration) {
    deadline.check();
    Batch batch =
        collectBatch(result.network, curriculum, iteration, threads, deadline);
    TrainingRecord record;
    record.iteration = iteration + 1;
    record.samples = batch.samples;
    record.initial_score = batch.initial_score;
    record.initial_moves = batch.initial_moves;
    record.curriculum_score = batch.curriculum_score;
    record.curriculum_moves = batch.curriculum_moves;
    record.clears_per_move = batch.clears_per_move;
    record.reveals_per_move = batch.reveals_per_move;
    record.update = update(result.network, batch, shuffle_random, deadline);
    result.records[iteration] = record;
    std::cerr << std::fixed << std::setprecision(3)
              << "curriculum-ppo iteration " << record.iteration << '/'
              << kIterations << " samples " << record.samples
              << " initial " << record.initial_score << '/'
              << record.initial_moves << " curriculum "
              << record.curriculum_score << '/' << record.curriculum_moves
              << " flow " << record.clears_per_move << '/'
              << record.reveals_per_move << " loss "
              << record.update.policy_loss << '/' << record.update.value_loss
              << " entropy " << record.update.entropy << " rss "
              << peakRssBytes() << '\n';
  }
  return result;
}

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  int clears = 0;
  int reveals = 0;
  int maximum_chain = 0;
  bool capped = false;
};

enum class EvaluationPolicy : std::uint8_t { kNetwork, kFairD1 };

GameResult playEvaluationGame(const Network& network, std::uint32_t seed,
                              EvaluationPolicy policy,
                              const Deadline& deadline) {
  requireSeed(seed, SeedUse::kStageA);
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kStageAMaximumMoves) {
    if ((state.moves_played & 31) == 0) deadline.check();
    const PublicState public_state = vr::publicState(state);
    const int action = policy == EvaluationPolicy::kNetwork
                           ? chooseAction(public_state, network).action
                           : vr::chooseFairDepthOne(public_state).action;
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("Stage-A policy selected illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("Stage-A transition failed");
    }
    for (const Wave& wave : move.waves) {
      result.clears += wave.cleared;
      result.reveals += wave.revealed;
      result.maximum_chain = std::max(result.maximum_chain, wave.depth);
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.capped = !state.game_over;
  return result;
}

std::vector<GameResult> evaluate(const Network& network,
                                 EvaluationPolicy policy, int threads,
                                 const Deadline& deadline) {
  std::vector<GameResult> games(kStageAGames);
  std::atomic<int> next{0};
  const int workers = std::min(threads, kStageAGames);
  std::vector<std::future<void>> futures;
  for (int worker = 0; worker < workers; ++worker) {
    futures.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next.fetch_add(1);
        if (game >= kStageAGames) return;
        games[game] = playEvaluationGame(
            network, kStageASeedStart + static_cast<std::uint32_t>(game),
            policy, deadline);
      }
    }));
  }
  for (auto& future : futures) future.get();
  enforceRssLimit();
  return games;
}

struct Summary {
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double bottom_quartile_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  int maximum_chain = 0;
  int capped = 0;
};

Summary summarize(const std::vector<GameResult>& games) {
  if (games.empty()) throw std::invalid_argument("cannot summarize no games");
  Summary result;
  std::int64_t score = 0;
  std::int64_t moves = 0;
  std::int64_t clears = 0;
  std::int64_t reveals = 0;
  std::vector<int> move_values;
  for (const GameResult& game : games) {
    score += game.score;
    moves += game.moves;
    clears += game.clears;
    reveals += game.reveals;
    result.maximum_chain = std::max(result.maximum_chain, game.maximum_chain);
    result.capped += game.capped;
    move_values.push_back(game.moves);
  }
  result.mean_score = static_cast<double>(score) / games.size();
  result.mean_moves = static_cast<double>(moves) / games.size();
  result.clears_per_move = static_cast<double>(clears) / moves;
  result.reveals_per_move = static_cast<double>(reveals) / moves;
  std::sort(move_values.begin(), move_values.end());
  const std::size_t bottom = games.size() / 4;
  result.bottom_quartile_moves =
      static_cast<double>(std::accumulate(move_values.begin(),
                                          move_values.begin() + bottom, 0LL)) /
      bottom;
  return result;
}

struct PairedSummary {
  int score_wins = 0;
  int move_wins = 0;
  int joint_wins = 0;
  double score_delta = 0.0;
  double move_delta = 0.0;
};

PairedSummary pair(const std::vector<GameResult>& candidate,
                   const std::vector<GameResult>& baseline) {
  if (candidate.size() != baseline.size()) {
    throw std::invalid_argument("Stage-A cohorts do not align");
  }
  PairedSummary result;
  for (std::size_t index = 0; index < candidate.size(); ++index) {
    if (candidate[index].seed != baseline[index].seed) {
      throw std::runtime_error("Stage-A seed mismatch");
    }
    const bool score_win = candidate[index].score > baseline[index].score;
    const bool move_win = candidate[index].moves > baseline[index].moves;
    result.score_wins += score_win;
    result.move_wins += move_win;
    result.joint_wins += score_win && move_win;
    result.score_delta += candidate[index].score - baseline[index].score;
    result.move_delta += candidate[index].moves - baseline[index].moves;
  }
  result.score_delta /= candidate.size();
  result.move_delta /= candidate.size();
  return result;
}

struct Options {
  std::string curriculum = "/tmp/drop7-oracle-curriculum-states.jsonl";
  std::string checkpoint = "/tmp/drop7-curriculum-option-ppo.bin";
  std::string output = "/tmp/drop7-curriculum-option-ppo-stage-a.json";
  int threads = 4;
};

Options parseOptions(int argc, char** argv, int begin) {
  Options options;
  for (int index = begin; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--curriculum" && index + 1 < argc) {
      options.curriculum = argv[++index];
    } else if (argument == "--checkpoint" && index + 1 < argc) {
      options.checkpoint = argv[++index];
    } else if (argument == "--output" && index + 1 < argc) {
      options.output = argv[++index];
    } else if (argument == "--threads" && index + 1 < argc) {
      options.threads = std::stoi(argv[++index]);
    } else {
      throw std::invalid_argument("unknown or incomplete option");
    }
  }
  if (options.curriculum.empty() || options.checkpoint.empty() ||
      options.output.empty() || options.threads < 1 ||
      options.threads > kMaximumThreads) {
    throw std::invalid_argument("invalid curriculum PPO options");
  }
  return options;
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"bottomQuartileMoves\":" << summary.bottom_quartile_moves
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"maximumChain\":" << summary.maximum_chain
         << ",\"capped\":" << summary.capped << '}';
}

void writeArtifact(const Options& options, const Curriculum& curriculum,
                   const TrainingResult& training,
                   const std::vector<GameResult>& candidate,
                   const Summary& candidate_summary,
                   const std::vector<GameResult>& baseline,
                   const Summary& baseline_summary,
                   const PairedSummary& paired, bool passed,
                   double wall_seconds) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open PPO artifact");
  output << std::setprecision(12)
         << "{\n  \"format\":\"drop7-curriculum-option-ppo-v1\","
         << "\n  \"architecture\":{\"input\":" << kInputSize
         << ",\"hidden\":[" << kHidden1 << ',' << kHidden2
         << "],\"parameters\":" << Layout::count
         << ",\"reflection\":\"exact two-pass shared MLP residual\","
         << "\"base\":\"normalized exact fair-D1 root-Q logits\"},"
         << "\n  \"training\":{\"iterations\":" << kIterations
         << ",\"episodesPerIteration\":" << kEpisodesPerIteration
         << ",\"totalEpisodes\":" << kTrainingEpisodes
         << ",\"initialFraction\":0.5,\"curriculumFraction\":0.5,"
         << "\"initialMaximumMoves\":" << kInitialMaximumMoves
         << ",\"curriculumHorizon\":" << kCurriculumHorizon
         << ",\"epochs\":" << kPpoEpochs
         << ",\"minibatch\":" << kMinibatch
         << ",\"gamma\":" << kGamma << ",\"gaeLambda\":"
         << kGaeLambda << ",\"clip\":" << kClipRatio
         << ",\"entropy\":" << kEntropyCoefficient
         << ",\"valueCoefficient\":" << kValueCoefficient
         << ",\"gradientClip\":" << kGradientNorm
         << ",\"learningRate\":" << kLearningRate
         << ",\"reward\":\"scoreDelta/17000 + .05 survived + .05 clears + .15 reveals - 5 terminal\"},"
         << "\n  \"curriculum\":{\"states\":" << curriculum.states.size()
         << ",\"fingerprint\":\"0x" << std::hex << curriculum.fingerprint
         << std::dec << "\",\"sourceMetadataRetained\":false,"
         << "\"independentEventStreams\":true},"
         << "\n  \"seedLanes\":{\"training\":\"0x3d670000..0x3d677fff\","
         << "\"stageA\":\"0x3d680000..0x3d68001f\"},"
         << "\n  \"checkpoint\":\"" << options.checkpoint
         << "\",\n  \"modelFingerprint\":\"0x" << std::hex
         << modelFingerprint(training.network) << std::dec << "\","
         << "\n  \"lastTrainingRecord\":{\"samples\":"
         << training.records.back().samples << ",\"initialScore\":"
         << training.records.back().initial_score << ",\"initialMoves\":"
         << training.records.back().initial_moves
         << ",\"curriculumScore\":"
         << training.records.back().curriculum_score
         << ",\"curriculumMoves\":"
         << training.records.back().curriculum_moves
         << ",\"clearsPerMove\":"
         << training.records.back().clears_per_move
         << ",\"revealsPerMove\":"
         << training.records.back().reveals_per_move << "},"
         << "\n  \"candidate\":";
  writeSummary(output, candidate_summary);
  output << ",\n  \"fairD1\":";
  writeSummary(output, baseline_summary);
  output << ",\n  \"paired\":{\"scoreWins\":" << paired.score_wins
         << ",\"moveWins\":" << paired.move_wins
         << ",\"jointWins\":" << paired.joint_wins
         << ",\"meanScoreDelta\":" << paired.score_delta
         << ",\"meanMoveDelta\":" << paired.move_delta << "},"
         << "\n  \"gate\":{\"meanScore\":" << kGateMeanScore
         << ",\"meanMoves\":" << kGateMeanMoves
         << ",\"bottomQuartileMoves\":" << kGateBottomQuartileMoves
         << ",\"clearsPerMove\":" << kGateClearsPerMove
         << ",\"revealsPerMove\":" << kGateRevealsPerMove
         << ",\"jointWins\":" << kGateJointWins << "},"
         << "\n  \"passed\":" << (passed ? "true" : "false")
         << ",\n  \"wallSeconds\":" << wall_seconds
         << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
  if (!output) throw std::runtime_error("failed writing PPO artifact");
  static_cast<void>(candidate);
  static_cast<void>(baseline);
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

PublicState fixtureState() {
  PublicState state;
  state.board.fill(kEmpty);
  state.board[indexOf(6, 0)] = kSolid;
  state.board[indexOf(5, 0)] = 4;
  state.board[indexOf(6, 1)] = kSolid;
  state.board[indexOf(5, 1)] = 1;
  state.board[indexOf(6, 2)] = kSolid;
  state.board[indexOf(6, 3)] = kCracked;
  state.board[indexOf(6, 4)] = kSolid;
  state.board[indexOf(5, 4)] = 6;
  state.board[indexOf(4, 4)] = 5;
  state.next_disc = 3;
  state.moves_remaining = 4;
  return state;
}

double gradientLoss(const Network& network, const PublicState& state,
                    const BasePolicy& base, int action,
                    float policy_coefficient, float value_derivative,
                    float entropy_coefficient) {
  const Prediction prediction = predictCanonical(network, state, &base);
  double entropy = 0.0;
  for (const float probability : prediction.probabilities) {
    if (probability > 0.0f) entropy -= probability * std::log(probability);
  }
  return policy_coefficient *
             std::log(std::max(1.0e-12f,
                               prediction.probabilities[action])) +
         value_derivative * prediction.value -
         entropy_coefficient * entropy;
}

bool selfTest(const Options& options, std::ostream& output) {
  const Curriculum curriculum = loadCurriculum(options.curriculum);
  expect(curriculum.states.size() == kExpectedCurriculumStates,
         "curriculum size self-test");
  const PublicState fixture = fixtureState();
  const Observation observation = observePublic(fixture);
  expect(mirrorObservation(mirrorObservation(observation)) == observation,
         "observation mirror is not an involution");

  Network zero;
  expect(zero.residualIsZero(), "initial residual head is not zero");
  const PolicyDecision decision = chooseAction(fixture, zero);
  const PolicyDecision repeated = chooseAction(fixture, zero);
  const PolicyDecision reflected = chooseAction(vr::mirror(fixture), zero);
  expect(decision == repeated && decision.action == decision.base_action &&
             isLegal(fixture.board, decision.action),
         "zero residual is not exact deterministic fair D1");
  expect(reflected.action == kBoardSize - 1 - decision.action,
         "policy action reflection failed");
  for (int action = 0; action < kBoardSize; ++action) {
    expect(reflected.probabilities[kBoardSize - 1 - action] ==
               decision.probabilities[action],
           "policy probability reflection failed");
  }
  for (std::size_t index = 0; index < curriculum.states.size(); index += 64) {
    const PublicState& state = curriculum.states[index];
    const PolicyDecision zero_decision = chooseAction(state, zero);
    const vr::BaselineDecision d1 = vr::chooseFairDepthOne(state);
    const PolicyDecision mirror_decision =
        chooseAction(vr::mirror(state), zero);
    expect(d1.complete && zero_decision.action == d1.action &&
               isLegal(state.board, zero_decision.action) &&
               mirror_decision.action ==
                   kBoardSize - 1 - zero_decision.action,
           "curriculum zero-residual D1/reflection sweep failed");
  }

  State metadata = vr::materialize(fixture);
  metadata.score = 9'999'999;
  metadata.level = 777;
  metadata.moves_played = 888;
  expect(vr::publicState(metadata) == fixture &&
             chooseAction(vr::publicState(metadata), zero) == decision,
         "policy retained hidden metadata");

  const std::uint32_t restart_seed = restartBaseSeed(0x1234'5678u, 19);
  State restart_first = vr::materialize(fixture);
  State restart_second = restart_first;
  MoveResult first_move;
  MoveResult second_move;
  expect(playRestartMove(restart_first, restart_seed, 3, decision.action,
                         first_move) &&
             playRestartMove(restart_second, restart_seed, 3, decision.action,
                             second_move) &&
             restart_first.board == restart_second.board &&
             restart_first.next_disc == restart_second.next_disc &&
             first_move.score_delta == second_move.score_delta &&
             restartNextDisc(restart_seed, 3) ==
                 restartNextDisc(restart_seed, 3) &&
             restartNextDisc(restart_seed, 3) !=
                 restartNextDisc(restart_seed ^ kRestartDiscDomain, 3),
         "independent event-indexed restart stream failed");

  std::vector<Sample> gae(2);
  gae[0].reward = 1.0f;
  gae[0].old_value = 0.5f;
  gae[1].reward = 2.0f;
  gae[1].old_value = 0.25f;
  gae[1].terminal = true;
  finishAdvantages(gae, 0.0f);
  const float expected_last = 2.0f - 0.25f;
  const float expected_first =
      1.0f + kGamma * 0.25f - 0.5f +
      kGamma * kGaeLambda * expected_last;
  expect(std::abs(gae[1].advantage - expected_last) < 1.0e-6f &&
             std::abs(gae[0].advantage - expected_first) < 1.0e-6f &&
             ppoPolicyCoefficient(1.0f, 1.21f, 1.0f) == 0.0f &&
             ppoPolicyCoefficient(-1.0f, 0.79f, 1.0f) == 0.0f &&
             ppoPolicyCoefficient(1.0f, 1.10f, 1.0f) < 0.0f,
         "GAE/PPO clipping math failed");

  Network gradient_network;
  gradient_network.setParameter(Layout::policy_w + 7, 0.031f);
  gradient_network.setParameter(Layout::policy_w + 3 * kHidden2 + 11,
                                -0.027f);
  gradient_network.setParameter(Layout::value_w + 5, 0.023f);
  const BasePolicy base = fairBasePolicy(fixture);
  const Prediction prediction =
      predictCanonical(gradient_network, fixture, &base);
  constexpr float policy_coefficient = 0.37f;
  constexpr float value_derivative = -0.19f;
  constexpr float entropy_coefficient = 0.013f;
  std::vector<float> analytic = gradient_network.zeroGradient();
  accumulateEquivariantGradient(
      gradient_network, prediction, decision.action, policy_coefficient,
      value_derivative, entropy_coefficient, analytic);
  const std::array<int, 8> indexes{{
      Layout::w1 + 17,
      Layout::b1 + 7,
      Layout::w2 + 5 * kHidden1 + 9,
      Layout::b2 + 12,
      Layout::policy_w + decision.action * kHidden2 + 3,
      Layout::policy_b + decision.action,
      Layout::value_w + 6,
      Layout::value_b,
  }};
  constexpr float epsilon = 0.001f;
  double maximum_scaled_error = 0.0;
  double maximum_absolute_error = 0.0;
  for (const int index : indexes) {
    const float original = gradient_network.parameter(index);
    gradient_network.setParameter(index, original + epsilon);
    const double positive = gradientLoss(
        gradient_network, fixture, base, decision.action, policy_coefficient,
        value_derivative, entropy_coefficient);
    gradient_network.setParameter(index, original - epsilon);
    const double negative = gradientLoss(
        gradient_network, fixture, base, decision.action, policy_coefficient,
        value_derivative, entropy_coefficient);
    gradient_network.setParameter(index, original);
    const double numerical = (positive - negative) / (2.0 * epsilon);
    const double absolute_error = std::abs(numerical - analytic[index]);
    const double error = absolute_error /
                         std::max(1.0e-4, std::abs(numerical) +
                                                  std::abs(analytic[index]));
    maximum_absolute_error =
        std::max(maximum_absolute_error, absolute_error);
    maximum_scaled_error = std::max(maximum_scaled_error, error);
  }
  expect(maximum_absolute_error < 2.0e-4 || maximum_scaled_error < 0.035,
         "equivariant MLP gradient check failed");

  const std::string checkpoint = options.checkpoint + ".self-test";
  saveCheckpoint(checkpoint, gradient_network);
  const Network restored = loadCheckpoint(checkpoint);
  expect(restored.parameters() == gradient_network.parameters() &&
             modelFingerprint(restored) == modelFingerprint(gradient_network),
         "checkpoint round trip failed");

  expect(allowedSeed(kTrainingSeedStart, SeedUse::kTraining) &&
             allowedSeed(kTrainingSeedEndExclusive - 1u,
                         SeedUse::kTraining) &&
             allowedSeed(kStageASeedStart, SeedUse::kStageA) &&
             allowedSeed(kStageASeedEndExclusive - 1u, SeedUse::kStageA) &&
             throwsInvalid([] {
               requireSeed(0x3d65'1000u, SeedUse::kTraining);
             }) &&
             throwsInvalid([] {
               requireSeed(0x3d67'8000u, SeedUse::kTraining);
             }) &&
             throwsInvalid([] {
               requireSeed(0x3d68'0020u, SeedUse::kStageA);
             }) &&
             throwsInvalid([] {
               requireSeed(0x4d67'0000u, SeedUse::kTraining);
             }) &&
             throwsInvalid([] {
               requireSeed(0x7d67'0000u, SeedUse::kTraining);
             }) &&
             throwsInvalid([] {
               requireSeed(0xd767'0000u, SeedUse::kTraining);
             }),
         "curriculum PPO seed guards failed");
  enforceRssLimit();
  output << std::setprecision(12)
         << "CURRICULUM_OPTION_PPO_SELF_TEST {\"passed\":true,"
         << "\"parameters\":" << Layout::count
         << ",\"zeroResidualExactD1\":true,\"reflectionExact\":true,"
         << "\"metadataBlind\":true,\"restartIndependent\":true,"
         << "\"ppoMath\":true,\"gradientAbsoluteError\":"
         << maximum_absolute_error << ",\"gradientScaledError\":"
         << maximum_scaled_error << ",\"curriculumStates\":"
         << curriculum.states.size() << ",\"seedGuards\":true,"
         << "\"peakRssBytes\":" << peakRssBytes() << "}\n";
  return true;
}

int memoryPreflight(const Options& options, std::ostream& output) {
  const Curriculum curriculum = loadCurriculum(options.curriculum);
  Network network;
  std::vector<Sample> maximum_batch(kMaximumBatchSamples);
  volatile unsigned char* const sample_bytes =
      reinterpret_cast<volatile unsigned char*>(maximum_batch.data());
  const std::size_t allocated_bytes =
      maximum_batch.size() * sizeof(Sample);
  std::uint64_t touch_checksum = 0;
  for (std::size_t offset = 0; offset < allocated_bytes; offset += 4'096) {
    sample_bytes[offset] = static_cast<unsigned char>(offset >> 12u);
    touch_checksum += sample_bytes[offset];
  }
  sample_bytes[allocated_bytes - 1] = 0xa5u;
  touch_checksum += sample_bytes[allocated_bytes - 1];
  const Observation observation = observePublic(curriculum.states.front());
  const BranchCache cache = network.forward(observation);
  std::vector<float> gradient = network.zeroGradient();
  maximum_batch.front().old_value = cache.value;
  gradient.front() = maximum_batch.front().old_value;
  enforceRssLimit();
  output << "CURRICULUM_OPTION_PPO_MEMORY_PREFLIGHT {\"passed\":true,"
         << "\"samples\":" << maximum_batch.size()
         << ",\"sampleBytes\":" << sizeof(Sample)
         << ",\"parameters\":" << Layout::count
         << ",\"curriculumStates\":" << curriculum.states.size()
         << ",\"touchChecksum\":" << touch_checksum
         << ",\"peakRssBytes\":" << peakRssBytes() << "}\n";
  return EXIT_SUCCESS;
}

int trainAndStageA(const Options& options, std::ostream& output) {
  const Deadline deadline;
  const Curriculum curriculum = loadCurriculum(options.curriculum);
  TrainingResult training = train(curriculum, options.threads, deadline);
  // Persist and reload before reading any Stage-A seed.  The reloaded
  // checkpoint is the immutable policy used for both candidate and report.
  saveCheckpoint(options.checkpoint, training.network);
  const Network frozen = loadCheckpoint(options.checkpoint);
  if (modelFingerprint(frozen) != modelFingerprint(training.network)) {
    throw std::runtime_error("frozen checkpoint verification failed");
  }
  const std::vector<GameResult> candidate =
      evaluate(frozen, EvaluationPolicy::kNetwork, options.threads, deadline);
  const Summary candidate_summary = summarize(candidate);
  const std::vector<GameResult> baseline =
      evaluate(frozen, EvaluationPolicy::kFairD1, options.threads, deadline);
  const Summary baseline_summary = summarize(baseline);
  const PairedSummary paired = pair(candidate, baseline);
  const bool passed =
      candidate_summary.mean_score >= kGateMeanScore &&
      candidate_summary.mean_moves >= kGateMeanMoves &&
      candidate_summary.bottom_quartile_moves >=
          kGateBottomQuartileMoves &&
      candidate_summary.clears_per_move >= kGateClearsPerMove &&
      candidate_summary.reveals_per_move >= kGateRevealsPerMove &&
      paired.joint_wins >= kGateJointWins;
  deadline.check();
  enforceRssLimit();
  const double wall_seconds = deadline.elapsedSeconds();
  writeArtifact(options, curriculum, training, candidate, candidate_summary,
                baseline, baseline_summary, paired, passed, wall_seconds);
  output << std::fixed << std::setprecision(3)
         << "CURRICULUM_OPTION_PPO_STAGE_A {\"candidateScore\":"
         << candidate_summary.mean_score << ",\"candidateMoves\":"
         << candidate_summary.mean_moves << ",\"bottomQuartileMoves\":"
         << candidate_summary.bottom_quartile_moves
         << ",\"clearsPerMove\":" << candidate_summary.clears_per_move
         << ",\"revealsPerMove\":" << candidate_summary.reveals_per_move
         << ",\"fairD1Score\":" << baseline_summary.mean_score
         << ",\"fairD1Moves\":" << baseline_summary.mean_moves
         << ",\"jointWins\":" << paired.joint_wins
         << ",\"passed\":" << (passed ? "true" : "false")
         << ",\"fingerprint\":\"0x" << std::hex
         << modelFingerprint(frozen) << std::dec << "\",\"wallSeconds\":"
         << wall_seconds << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return passed ? EXIT_SUCCESS : 2;
}

}  // namespace drop7::curriculum_option_ppo

#ifndef DROP7_CURRICULUM_OPTION_PPO_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc < 2) {
      throw std::invalid_argument("missing curriculum PPO mode");
    }
    const std::string_view mode(argv[1]);
    const auto options =
        drop7::curriculum_option_ppo::parseOptions(argc, argv, 2);
    if (mode == "--self-test") {
      return drop7::curriculum_option_ppo::selfTest(options, std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (mode == "--memory-preflight") {
      return drop7::curriculum_option_ppo::memoryPreflight(options,
                                                           std::cout);
    }
    if (mode == "--train-and-stage-a") {
      return drop7::curriculum_option_ppo::trainAndStageA(options,
                                                          std::cout);
    }
    throw std::invalid_argument(
        "usage: drop7_curriculum_option_ppo --self-test | "
        "--memory-preflight | --train-and-stage-a "
        "[--curriculum PATH] [--checkpoint PATH] [--output PATH] "
        "[--threads N]");
  } catch (const std::exception& error) {
    std::cerr << "drop7_curriculum_option_ppo: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
#endif
