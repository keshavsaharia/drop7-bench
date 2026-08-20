#include "../../../src/core/native/engine.hpp"

// The 100m development-cohort gate compares against the fixed exact-D4
// implementation on identical seeds.  Library inclusion avoids
// accepting an unverifiable external table of comparator scores.
#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#define DROP7_FAIR_ONLY_DEPTH4_NO_MAIN
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
// GCC 14's optimized std::sort on fixed arrays emits a known false positive
// in the fixed exact-D4 dependency despite its constant four-element
// range.  Keep -Werror for this translation unit and isolate that diagnostic
// to the dependency's lexical include only.
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
#include "../../fair-expectimax/reference/fair-only-depth4.cpp"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#undef DROP7_FAIR_ONLY_DEPTH4_NO_MAIN
#undef DROP7_FAIR_ONLY_DEPTH4_LIBRARY

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <utility>
#include <vector>

// Learns the public chance-state value U(board, moves-to-rise) before the next
// visible numbered disc is sampled, using an isolated phase-aware n-tuple
// model.
// Drop7 does not have 2048's deterministic afterstate: covered-disc reveals
// can occur while an action's cascade is resolving.
namespace drop7::optimistic_phase_ntuple {

constexpr int kAlphabet = 10;
constexpr int kPatternCount = 10'000;
constexpr int kSharedTables = 17;
constexpr int kAbsoluteTables = 92;
constexpr int kTuplePlacements = 92;
constexpr int kActiveOccurrences = 2 * kTuplePlacements;
constexpr int kPooledEntries =
    (kSharedTables + kAbsoluteTables) * kPatternCount;
constexpr int kPhaseEntries =
    (kSharedTables + kAbsoluteTables) * kMovesPerLevel * kPatternCount;

constexpr float kOptimisticValue = 60.0f;
constexpr float kScoreScale = 17'000.0f;
constexpr float kGamma = 1.0f;
constexpr float kLambda = 0.5f;
constexpr int kTraceHorizon = 3;
constexpr std::uint64_t kPromotionTransition = 20'000'000;
constexpr std::uint64_t kLearningRateDrop1 = 50'000'000;
constexpr std::uint64_t kLearningRateDrop2 = 75'000'000;
constexpr std::uint64_t kTemporalCoherenceTransition = 90'000'000;
constexpr std::uint64_t kFrozenTrainingTransitions = 100'000'000;
constexpr std::uint64_t kBurnedStageATransitions = 50'000'000;
constexpr int kFrozenMaximumMoves = 2'000;
constexpr int kFrozenRevealSamples = 7;
constexpr int kFrozenEventBoundaries = 2;
constexpr int kFrozenInternalActionWidth = 2;
constexpr std::uint64_t kFrozenMaximumSearchWork = 100'000;
constexpr std::uint32_t kCheckpointFormatVersion = 3;
constexpr float kLearningRate0 = 0.1f;
constexpr float kLearningRate1 = 0.01f;
constexpr float kLearningRate2 = 0.001f;
constexpr float kTemporalCoherenceRate = 1.0f;

constexpr std::uint32_t kPolicyRevealDomain = 0x4f50'5452u;
constexpr std::uint32_t kSearchRevealDomain = 0x4556'4e54u;
constexpr std::uint32_t kSearchOrderDomain = 0x4f52'4452u;
constexpr std::uint32_t kChanceCoordinateDomain = 0x434f'4f52u;

// Seed authority is a build capability rather than a source edit: the
// immutable audited source is compiled with exact half-open ranges and the
// SHA-256 of a canonical lane manifest.  Missing macros keep a lane closed.
#if defined(DROP7_TRAINING_SEED_BEGIN)
constexpr std::uint32_t kTrainingSeedBegin = DROP7_TRAINING_SEED_BEGIN;
#else
constexpr std::uint32_t kTrainingSeedBegin = 0;
#endif
#if defined(DROP7_TRAINING_SEED_END)
constexpr std::uint32_t kTrainingSeedEnd = DROP7_TRAINING_SEED_END;
#else
constexpr std::uint32_t kTrainingSeedEnd = 0;
#endif
#if defined(DROP7_TRAINING_LANE_MANIFEST_SHA256)
constexpr std::string_view kCompiledTrainingLaneManifestSha256 =
    DROP7_TRAINING_LANE_MANIFEST_SHA256;
#else
constexpr std::string_view kCompiledTrainingLaneManifestSha256 = "";
#endif
constexpr std::uint32_t kBurnedStageASeedBegin = 0x3d20'0000u;
constexpr std::uint32_t kBurnedStageASeedEnd = 0x3d20'0040u;
#if defined(DROP7_DEVELOPMENT_SEED_BEGIN)
constexpr std::uint32_t kDevelopmentSeedBegin =
    DROP7_DEVELOPMENT_SEED_BEGIN;
#else
constexpr std::uint32_t kDevelopmentSeedBegin = 0;
#endif
#if defined(DROP7_DEVELOPMENT_SEED_END)
constexpr std::uint32_t kDevelopmentSeedEnd = DROP7_DEVELOPMENT_SEED_END;
#else
constexpr std::uint32_t kDevelopmentSeedEnd = 0;
#endif
#if defined(DROP7_DEVELOPMENT_LANE_MANIFEST_SHA256)
constexpr std::string_view kCompiledDevelopmentLaneManifestSha256 =
    DROP7_DEVELOPMENT_LANE_MANIFEST_SHA256;
#else
constexpr std::string_view kCompiledDevelopmentLaneManifestSha256 = "";
#endif
#if defined(DROP7_STAGE_A_QUALIFICATION_SHA256)
constexpr std::string_view kCompiledStageAQualificationSha256 =
    DROP7_STAGE_A_QUALIFICATION_SHA256;
#else
constexpr std::string_view kCompiledStageAQualificationSha256 = "";
#endif

#if defined(DROP7_OPTIMISTIC_PHASE_NTUPLE_SOURCE_SHA256)
constexpr std::string_view kCompiledSourceSha256 =
    DROP7_OPTIMISTIC_PHASE_NTUPLE_SOURCE_SHA256;
#else
constexpr std::string_view kCompiledSourceSha256 = "";
#endif
#if defined(DROP7_ENGINE_SOURCE_SHA256)
constexpr std::string_view kCompiledEngineSha256 = DROP7_ENGINE_SOURCE_SHA256;
#else
constexpr std::string_view kCompiledEngineSha256 = "";
#endif
#if defined(DROP7_CORRECTED_D4_SOURCE_SHA256)
constexpr std::string_view kCompiledD4Sha256 =
    DROP7_CORRECTED_D4_SOURCE_SHA256;
#else
constexpr std::string_view kCompiledD4Sha256 = "";
#endif
#if defined(DROP7_CORRECTED_D4_LEAF_SOURCE_SHA256)
constexpr std::string_view kCompiledD4LeafSha256 =
    DROP7_CORRECTED_D4_LEAF_SOURCE_SHA256;
#else
constexpr std::string_view kCompiledD4LeafSha256 = "";
#endif
#if defined(DROP7_CFPI_BEHAVIOR_SHA256)
constexpr std::string_view kCompiledCfpiBehaviorSha256 =
    DROP7_CFPI_BEHAVIOR_SHA256;
#else
constexpr std::string_view kCompiledCfpiBehaviorSha256 = "";
#endif

static_assert(kAlphabet * kAlphabet * kAlphabet * kAlphabet ==
              kPatternCount);
static_assert(kSharedTables == 4 + 4 + 9);
static_assert(kTuplePlacements == 28 + 28 + 36);
static_assert(kPooledEntries == 1'090'000);
static_assert(kPhaseEntries == 5'450'000);
static_assert(kPromotionTransition < kLearningRateDrop1);
static_assert(kLearningRateDrop1 < kLearningRateDrop2);
static_assert(kLearningRateDrop2 < kTemporalCoherenceTransition);
static_assert(kTemporalCoherenceTransition < kFrozenTrainingTransitions);
static_assert(kTrainingSeedBegin <= kTrainingSeedEnd);
static_assert(kBurnedStageASeedEnd - kBurnedStageASeedBegin == 64u);
static_assert(kDevelopmentSeedBegin <= kDevelopmentSeedEnd);

enum class ModelStage : std::uint8_t { kPooled = 0, kPhase = 1 };
enum class UpdateRule : std::uint8_t { kFixed = 0, kTemporalCoherence = 1 };
enum class SeedUse : std::uint8_t {
  kTraining = 0,
  kBurnedStageA = 1,
  kDevelopment = 2
};
enum class EvaluationStage : std::uint8_t {
  kBurnedStageA = 0,
  kFinalDevelopment = 1
};

struct ValueState {
  Board board{};
  std::uint8_t moves_remaining = kMovesPerLevel;
  bool terminal = false;

  bool operator==(const ValueState&) const = default;
};

ValueState valueState(const State& state) {
  return {state.board, static_cast<std::uint8_t>(state.moves_remaining),
          state.game_over};
}

Board mirrorBoard(const Board& source) {
  Board result{};
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      result[indexOf(row, column)] =
          source[indexOf(row, kBoardSize - 1 - column)];
    }
  }
  return result;
}

bool mirrorIsSmaller(const Board& board) {
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      const std::uint8_t forward = board[indexOf(row, column)];
      const std::uint8_t reflected =
          board[indexOf(row, kBoardSize - 1 - column)];
      if (reflected < forward) return true;
      if (reflected > forward) return false;
    }
  }
  return false;
}

struct CanonicalState {
  State state{};
  bool mirrored = false;
};

CanonicalState canonicalize(const State& source) {
  CanonicalState result{source, mirrorIsSmaller(source.board)};
  if (result.mirrored) result.state.board = mirrorBoard(source.board);
  return result;
}

ValueState canonicalize(const ValueState& source) {
  ValueState result = source;
  if (mirrorIsSmaller(source.board)) result.board = mirrorBoard(source.board);
  return result;
}

int physicalAction(int canonical_action, bool mirrored) {
  if (canonical_action < 0) return canonical_action;
  return mirrored ? kBoardSize - 1 - canonical_action : canonical_action;
}

int patternCode(std::uint8_t first, std::uint8_t second,
                std::uint8_t third, std::uint8_t fourth) {
  return ((first * kAlphabet + second) * kAlphabet + third) * kAlphabet +
         fourth;
}

struct FeatureOccurrences {
  std::array<std::uint32_t, kActiveOccurrences> ids{};
};

std::uint32_t sharedFeatureId(int table, int phase, int pattern,
                              ModelStage stage) {
  if (table < 0 || table >= kSharedTables || pattern < 0 ||
      pattern >= kPatternCount) {
    throw std::logic_error("shared n-tuple feature outside table bounds");
  }
  if (stage == ModelStage::kPooled) {
    return static_cast<std::uint32_t>(table * kPatternCount + pattern);
  }
  return static_cast<std::uint32_t>(
      ((phase * kSharedTables + table) * kPatternCount) + pattern);
}

std::uint32_t absoluteFeatureId(int table, int phase, int pattern,
                                ModelStage stage) {
  if (table < 0 || table >= kAbsoluteTables || pattern < 0 ||
      pattern >= kPatternCount) {
    throw std::logic_error("absolute n-tuple feature outside table bounds");
  }
  if (stage == ModelStage::kPooled) {
    return static_cast<std::uint32_t>(
        (kSharedTables + table) * kPatternCount + pattern);
  }
  const int shared_entries = kSharedTables * kMovesPerLevel * kPatternCount;
  return static_cast<std::uint32_t>(
      shared_entries +
      ((phase * kAbsoluteTables + table) * kPatternCount) + pattern);
}

FeatureOccurrences featureOccurrences(const ValueState& source,
                                      ModelStage stage) {
  if (source.moves_remaining < 1 ||
      source.moves_remaining > kMovesPerLevel) {
    throw std::logic_error("invalid moves-to-rise phase");
  }
  const ValueState state = canonicalize(source);
  const int phase = static_cast<int>(state.moves_remaining) - 1;
  FeatureOccurrences result;
  int occurrence = 0;
  int absolute_table = 0;
  auto add = [&](int shared_table, int pattern) {
    result.ids[occurrence++] =
        sharedFeatureId(shared_table, phase, pattern, stage);
    result.ids[occurrence++] =
        absoluteFeatureId(absolute_table++, phase, pattern, stage);
  };

  for (int row = 0; row < kBoardSize; ++row) {
    for (int start = 0; start <= kBoardSize - 4; ++start) {
      add(start,
          patternCode(state.board[indexOf(row, start)],
                      state.board[indexOf(row, start + 1)],
                      state.board[indexOf(row, start + 2)],
                      state.board[indexOf(row, start + 3)]));
    }
  }
  for (int column = 0; column < kBoardSize; ++column) {
    for (int start = 0; start <= kBoardSize - 4; ++start) {
      add(4 + start,
          patternCode(state.board[indexOf(start, column)],
                      state.board[indexOf(start + 1, column)],
                      state.board[indexOf(start + 2, column)],
                      state.board[indexOf(start + 3, column)]));
    }
  }
  for (int row = 0; row < kBoardSize - 1; ++row) {
    for (int column = 0; column < kBoardSize - 1; ++column) {
      add(8 + (row % 3) * 3 + (column % 3),
          patternCode(state.board[indexOf(row, column)],
                      state.board[indexOf(row, column + 1)],
                      state.board[indexOf(row + 1, column)],
                      state.board[indexOf(row + 1, column + 1)]));
    }
  }
  if (occurrence != kActiveOccurrences ||
      absolute_table != kAbsoluteTables) {
    throw std::logic_error("n-tuple occurrence count invariant failed");
  }
  return result;
}

struct ActiveFeatures {
  std::array<std::uint32_t, kActiveOccurrences> ids{};
  std::array<std::uint16_t, kActiveOccurrences> multiplicities{};
  int count = 0;
  int squared_norm = 0;
  int maximum_multiplicity = 0;
};

ActiveFeatures activeFeatures(const ValueState& state, ModelStage stage) {
  auto sorted = featureOccurrences(state, stage).ids;
  std::sort(sorted.begin(), sorted.end());
  ActiveFeatures result;
  for (std::uint32_t id : sorted) {
    if (result.count == 0 || result.ids[result.count - 1] != id) {
      result.ids[result.count] = id;
      result.multiplicities[result.count] = 1;
      ++result.count;
    } else {
      ++result.multiplicities[result.count - 1];
    }
  }
  for (int index = 0; index < result.count; ++index) {
    const int multiplicity = result.multiplicities[index];
    result.squared_norm += multiplicity * multiplicity;
    result.maximum_multiplicity =
        std::max(result.maximum_multiplicity, multiplicity);
  }
  if (result.count < 1 || result.squared_norm < kActiveOccurrences) {
    throw std::logic_error("n-tuple gradient aggregation invariant failed");
  }
  return result;
}

struct UpdateReport {
  float prediction_before = 0;
  float prediction_after = 0;
  float mean_beta = 1;
  int unique_parameters = 0;
  int squared_norm = 0;
  int maximum_multiplicity = 0;
};

struct Progress {
  std::uint64_t transitions = 0;
  std::uint64_t completed_games = 0;
  std::uint32_t training_seed_start = 0;
  bool game_active = false;
  bool training_finalized = false;
  std::uint32_t active_game_seed = 0;
  State active_state{};
  std::uint64_t cumulative_score = 0;
  std::uint64_t cumulative_moves = 0;
  std::uint64_t censored_training_games = 0;
};

struct TrainingContract {
  std::uint32_t maximum_moves = kFrozenMaximumMoves;
  std::uint32_t reveal_samples = kFrozenRevealSamples;
  std::uint32_t training_seed_begin = kTrainingSeedBegin;
  std::uint32_t training_seed_end = kTrainingSeedEnd;
  std::string training_lane_manifest_sha256{
      kCompiledTrainingLaneManifestSha256};
  std::string source_sha256{kCompiledSourceSha256};
  std::string engine_sha256{kCompiledEngineSha256};
  std::string corrected_d4_sha256{kCompiledD4Sha256};
  std::string corrected_d4_leaf_sha256{kCompiledD4LeafSha256};
  std::string cfpi_behavior_sha256{kCompiledCfpiBehaviorSha256};

  bool operator==(const TrainingContract&) const = default;
};

template <typename T>
void writeScalar(std::ostream& output, const T& value) {
  output.write(reinterpret_cast<const char*>(&value), sizeof(value));
  if (!output) throw std::runtime_error("failed writing n-tuple checkpoint");
}

template <typename T>
T readScalar(std::istream& input) {
  T value{};
  input.read(reinterpret_cast<char*>(&value), sizeof(value));
  if (!input) throw std::runtime_error("truncated n-tuple checkpoint");
  return value;
}

void writeFloats(std::ostream& output, const std::vector<float>& values) {
  output.write(reinterpret_cast<const char*>(values.data()),
               static_cast<std::streamsize>(values.size() * sizeof(float)));
  if (!output) throw std::runtime_error("failed writing n-tuple checkpoint");
}

void readFloats(std::istream& input, std::vector<float>& values) {
  input.read(reinterpret_cast<char*>(values.data()),
             static_cast<std::streamsize>(values.size() * sizeof(float)));
  if (!input) throw std::runtime_error("truncated n-tuple checkpoint");
  if (std::any_of(values.begin(), values.end(),
                  [](float value) { return !std::isfinite(value); })) {
    throw std::runtime_error("non-finite parameter in n-tuple checkpoint");
  }
}

std::uint64_t checksumBytes(std::string_view bytes) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const unsigned char byte : bytes) {
    hash ^= byte;
    hash *= 0x0000'0100'0000'01b3ull;
  }
  return hash;
}

constexpr std::array<std::uint32_t, 64> kSha256Constants{{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
}};

std::string sha256(std::string_view source) {
  std::vector<std::uint8_t> message(source.begin(), source.end());
  const std::uint64_t bit_length =
      static_cast<std::uint64_t>(message.size()) * 8u;
  message.push_back(0x80u);
  while (message.size() % 64 != 56) message.push_back(0u);
  for (int byte = 7; byte >= 0; --byte) {
    message.push_back(
        static_cast<std::uint8_t>((bit_length >> (byte * 8)) & 0xffu));
  }
  std::array<std::uint32_t, 8> hash{{
      0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
  }};
  for (std::size_t offset = 0; offset < message.size(); offset += 64) {
    std::array<std::uint32_t, 64> words{};
    for (int word = 0; word < 16; ++word) {
      const std::size_t begin = offset + static_cast<std::size_t>(word * 4);
      words[word] = (static_cast<std::uint32_t>(message[begin]) << 24) |
                    (static_cast<std::uint32_t>(message[begin + 1]) << 16) |
                    (static_cast<std::uint32_t>(message[begin + 2]) << 8) |
                    static_cast<std::uint32_t>(message[begin + 3]);
    }
    for (int word = 16; word < 64; ++word) {
      const std::uint32_t s0 =
          std::rotr(words[word - 15], 7) ^
          std::rotr(words[word - 15], 18) ^ (words[word - 15] >> 3);
      const std::uint32_t s1 =
          std::rotr(words[word - 2], 17) ^
          std::rotr(words[word - 2], 19) ^ (words[word - 2] >> 10);
      words[word] = words[word - 16] + s0 + words[word - 7] + s1;
    }
    std::uint32_t a = hash[0];
    std::uint32_t b = hash[1];
    std::uint32_t c = hash[2];
    std::uint32_t d = hash[3];
    std::uint32_t e = hash[4];
    std::uint32_t f = hash[5];
    std::uint32_t g = hash[6];
    std::uint32_t h = hash[7];
    for (int round = 0; round < 64; ++round) {
      const std::uint32_t upper =
          std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
      const std::uint32_t choose = (e & f) ^ (~e & g);
      const std::uint32_t first =
          h + upper + choose + kSha256Constants[round] + words[round];
      const std::uint32_t lower =
          std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t second = lower + majority;
      h = g;
      g = f;
      f = e;
      e = d + first;
      d = c;
      c = b;
      b = a;
      a = first + second;
    }
    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
    hash[5] += f;
    hash[6] += g;
    hash[7] += h;
  }
  std::string result;
  constexpr char digits[] = "0123456789abcdef";
  result.reserve(64);
  for (const std::uint32_t value : hash) {
    for (int nibble = 7; nibble >= 0; --nibble) {
      result.push_back(digits[(value >> (nibble * 4)) & 0x0fu]);
    }
  }
  return result;
}

std::string readWholeFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) throw std::runtime_error("could not read artifact for SHA-256");
  const std::streampos end = input.tellg();
  if (end < 0) throw std::runtime_error("invalid artifact byte count");
  std::string result(static_cast<std::size_t>(end), '\0');
  input.seekg(0);
  input.read(result.data(), static_cast<std::streamsize>(result.size()));
  if (!input) throw std::runtime_error("artifact SHA-256 read failed");
  return result;
}

std::string fileSha256(const std::filesystem::path& path) {
  return sha256(readWholeFile(path));
}

bool validSha256Hex(std::string_view value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

struct ProvenanceHashes {
  std::string source;
  std::string engine;
  std::string corrected_d4;
  std::string corrected_d4_leaf;
  std::string cfpi_behavior;
};

bool compiledProvenanceLocksValid() {
  return validSha256Hex(kCompiledSourceSha256) &&
         validSha256Hex(kCompiledEngineSha256) &&
         validSha256Hex(kCompiledD4Sha256) &&
         validSha256Hex(kCompiledD4LeafSha256) &&
         validSha256Hex(kCompiledCfpiBehaviorSha256);
}

ProvenanceHashes verifyCompiledProvenance(
    const std::filesystem::path& source_directory) {
  if (!compiledProvenanceLocksValid()) {
    throw std::runtime_error(
        "binary lacks complete compile-time source SHA-256 locks");
  }
  const std::filesystem::path canonical_directory =
      std::filesystem::canonical(source_directory);
  ProvenanceHashes result{
      fileSha256(canonical_directory / "approaches/ntuple-rl/optimistic-phase/optimistic-phase-ntuple.cpp"),
      fileSha256(canonical_directory / "src/core/native/engine.hpp"),
      fileSha256(canonical_directory / "approaches/fair-expectimax/reference/fair-only-depth4.cpp"),
      fileSha256(canonical_directory / "approaches/fair-expectimax/reference/fair-only-horizon.cpp"),
      fileSha256(canonical_directory / "src/core/native/public-behavior.hpp")};
  if (result.source != kCompiledSourceSha256 ||
      result.engine != kCompiledEngineSha256 ||
      result.corrected_d4 != kCompiledD4Sha256 ||
      result.corrected_d4_leaf != kCompiledD4LeafSha256 ||
      result.cfpi_behavior != kCompiledCfpiBehaviorSha256) {
    throw std::runtime_error(
        "on-disk source differs from the binary's build locks");
  }
  return result;
}

std::string_view compiledLaneManifestSha256(SeedUse use) {
  if (use == SeedUse::kTraining) {
    return kCompiledTrainingLaneManifestSha256;
  }
  if (use == SeedUse::kDevelopment) {
    return kCompiledDevelopmentLaneManifestSha256;
  }
  throw std::invalid_argument("burned Stage A has no external lane manifest");
}

std::string canonicalLaneManifest(SeedUse use) {
  if (use == SeedUse::kBurnedStageA) {
    throw std::invalid_argument("burned Stage A lane is source-locked");
  }
  const bool training = use == SeedUse::kTraining;
  const std::uint32_t begin =
      training ? kTrainingSeedBegin : kDevelopmentSeedBegin;
  const std::uint32_t end =
      training ? kTrainingSeedEnd : kDevelopmentSeedEnd;
  std::ostringstream manifest;
  manifest << "drop7-seed-lane-v1\n"
           << "purpose=" << (training ? "training" : "final-development")
           << "\nseedBegin=0x" << std::hex << std::setw(8)
           << std::setfill('0') << begin << "\nseedEndExclusive=0x"
           << std::setw(8) << end << std::dec << std::setfill(' ')
           << "\nsourceSha256=" << kCompiledSourceSha256
           << "\nengineSha256=" << kCompiledEngineSha256
           << "\nmaximumMoves=" << kFrozenMaximumMoves
           << "\nrevealSamples=" << kFrozenRevealSamples
           << "\ntrainingTransitions=" << kFrozenTrainingTransitions;
  if (!training) {
    manifest << "\ngames=256"
             << "\neventBoundaries=" << kFrozenEventBoundaries
             << "\ninternalActionWidth=" << kFrozenInternalActionWidth
             << "\nmaximumWork=" << kFrozenMaximumSearchWork;
  }
  manifest << '\n';
  return manifest.str();
}

std::string verifyLaneManifest(const std::filesystem::path& path,
                               SeedUse use) {
  const std::string_view compiled_hash = compiledLaneManifestSha256(use);
  if (!validSha256Hex(compiled_hash)) {
    throw std::runtime_error("binary lacks a compile-time lane-manifest lock");
  }
  const std::string contents = readWholeFile(path);
  const std::string hash = sha256(contents);
  if (hash != compiled_hash || contents != canonicalLaneManifest(use)) {
    throw std::runtime_error(
        "lane manifest differs from the compiled seed capability");
  }
  return hash;
}

bool sameFilesystemTarget(const std::filesystem::path& first,
                          const std::filesystem::path& second) {
  std::error_code error;
  if (std::filesystem::exists(first, error) && !error &&
      std::filesystem::exists(second, error) && !error &&
      std::filesystem::equivalent(first, second, error) && !error) {
    return true;
  }
  error.clear();
  const std::filesystem::path first_resolved =
      std::filesystem::weakly_canonical(first, error);
  if (error) throw std::runtime_error("could not resolve output path");
  const std::filesystem::path second_resolved =
      std::filesystem::weakly_canonical(second, error);
  if (error) throw std::runtime_error("could not resolve protected path");
  return first_resolved == second_resolved;
}

void writeCheckpointHash(std::ostream& output, std::string_view value) {
  if (!value.empty() && !validSha256Hex(value)) {
    throw std::runtime_error("invalid checkpoint provenance hash");
  }
  writeScalar<std::uint8_t>(output,
                            static_cast<std::uint8_t>(value.size()));
  output.write(value.data(), static_cast<std::streamsize>(value.size()));
  if (!output) throw std::runtime_error("failed writing checkpoint provenance");
}

std::string readCheckpointHash(std::istream& input) {
  const std::uint8_t size = readScalar<std::uint8_t>(input);
  if (size != 0 && size != 64) {
    throw std::runtime_error("invalid checkpoint provenance hash length");
  }
  std::string result(size, '\0');
  input.read(result.data(), static_cast<std::streamsize>(result.size()));
  if (!input) throw std::runtime_error("truncated checkpoint provenance");
  if (!result.empty() && !validSha256Hex(result)) {
    throw std::runtime_error("invalid checkpoint provenance hash");
  }
  return result;
}

void writeState(std::ostream& output, const State& state) {
  output.write(reinterpret_cast<const char*>(state.board.data()),
               static_cast<std::streamsize>(state.board.size()));
  if (!output) throw std::runtime_error("failed writing n-tuple checkpoint");
  writeScalar<std::uint8_t>(output, state.next_disc);
  writeScalar<std::int64_t>(output, state.score);
  writeScalar<std::int32_t>(output, state.level);
  writeScalar<std::int32_t>(output, state.moves_remaining);
  writeScalar<std::int32_t>(output, state.moves_played);
  writeScalar<std::uint8_t>(output, state.game_over ? 1u : 0u);
}

State readState(std::istream& input) {
  State state;
  input.read(reinterpret_cast<char*>(state.board.data()),
             static_cast<std::streamsize>(state.board.size()));
  if (!input) throw std::runtime_error("truncated n-tuple checkpoint");
  state.next_disc = readScalar<std::uint8_t>(input);
  state.score = readScalar<std::int64_t>(input);
  state.level = readScalar<std::int32_t>(input);
  state.moves_remaining = readScalar<std::int32_t>(input);
  state.moves_played = readScalar<std::int32_t>(input);
  const std::uint8_t terminal = readScalar<std::uint8_t>(input);
  if (std::any_of(state.board.begin(), state.board.end(),
                  [](std::uint8_t cell) { return cell > kCracked; }) ||
      state.next_disc < 1 || state.next_disc > 7 || state.score < 0 ||
      state.level < 1 || state.moves_remaining < 0 ||
      state.moves_remaining > kMovesPerLevel || state.moves_played < 0 ||
      terminal > 1 || (state.moves_remaining == 0 && terminal == 0)) {
    throw std::runtime_error("invalid game state in n-tuple checkpoint");
  }
  state.game_over = terminal != 0;
  return state;
}

class Model {
 public:
  explicit Model(float optimistic_value = kOptimisticValue)
      : weights_(kPooledEntries,
                 optimistic_value /
                     static_cast<float>(kActiveOccurrences)) {}

  ModelStage stage() const { return stage_; }
  bool promoted() const { return stage_ == ModelStage::kPhase; }
  bool temporalCoherenceEnabled() const { return tc_enabled_; }
  std::size_t entries() const { return weights_.size(); }
  std::size_t parameterBytes() const {
    return (weights_.size() + signed_error_.size() +
            absolute_error_.size()) *
           sizeof(float);
  }

  std::uint64_t fingerprint() const {
    std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
    const auto append = [&](const void* data, std::size_t size) {
      const auto* bytes = static_cast<const unsigned char*>(data);
      for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 0x0000'0100'0000'01b3ull;
      }
    };
    const std::uint8_t stage = static_cast<std::uint8_t>(stage_);
    const std::uint8_t tc = tc_enabled_ ? 1u : 0u;
    append(&stage, sizeof(stage));
    append(&tc, sizeof(tc));
    append(weights_.data(), weights_.size() * sizeof(float));
    append(signed_error_.data(), signed_error_.size() * sizeof(float));
    append(absolute_error_.data(), absolute_error_.size() * sizeof(float));
    return hash;
  }

  std::string parameterSha256() const {
    std::string bytes;
    bytes.reserve(2u + parameterBytes());
    bytes.push_back(static_cast<char>(stage_));
    bytes.push_back(tc_enabled_ ? '\1' : '\0');
    const auto append = [&](const std::vector<float>& values) {
      if (!values.empty()) {
        bytes.append(reinterpret_cast<const char*>(values.data()),
                     values.size() * sizeof(float));
      }
    };
    append(weights_);
    append(signed_error_);
    append(absolute_error_);
    return sha256(bytes);
  }

  float value(const ValueState& state) const {
    if (state.terminal) return 0.0f;
    const FeatureOccurrences active = featureOccurrences(state, stage_);
    double result = 0.0;
    for (std::uint32_t id : active.ids) result += weights_[id];
    return static_cast<float>(result);
  }

  float value(const State& state) const { return value(valueState(state)); }

  UpdateReport update(const ValueState& state, float credit, float rate,
                      UpdateRule rule) {
    if (state.terminal) {
      throw std::invalid_argument("cannot update a terminal chance state");
    }
    if (!std::isfinite(credit) || !std::isfinite(rate) || rate <= 0.0f) {
      throw std::invalid_argument("invalid n-tuple update");
    }
    if (rule == UpdateRule::kTemporalCoherence && !tc_enabled_) {
      throw std::logic_error("temporal coherence used before schedule switch");
    }
    const ActiveFeatures active = activeFeatures(state, stage_);
    UpdateReport report;
    report.prediction_before = valueFromFeatures(active);
    report.unique_parameters = active.count;
    report.squared_norm = active.squared_norm;
    report.maximum_multiplicity = active.maximum_multiplicity;
    const float normalized =
        rate * credit / static_cast<float>(active.squared_norm);
    double beta_total = 0.0;
    for (int index = 0; index < active.count; ++index) {
      const std::uint32_t id = active.ids[index];
      const int multiplicity = active.multiplicities[index];
      float beta = 1.0f;
      if (rule == UpdateRule::kTemporalCoherence) {
        beta = absolute_error_[id] > 0.0f
                   ? std::abs(signed_error_[id]) / absolute_error_[id]
                   : 1.0f;
      }
      weights_[id] += beta * normalized * multiplicity;
      beta_total += beta;
      if (rule == UpdateRule::kTemporalCoherence) {
        const float parameter_error = credit * multiplicity;
        signed_error_[id] += parameter_error;
        absolute_error_[id] += std::abs(parameter_error);
        if (absolute_error_[id] > 1.0e7f) {
          signed_error_[id] *= 0.5f;
          absolute_error_[id] *= 0.5f;
        }
      }
    }
    report.mean_beta =
        static_cast<float>(beta_total / static_cast<double>(active.count));
    report.prediction_after = valueFromFeatures(active);
    return report;
  }

  void promote() {
    if (promoted()) throw std::logic_error("phase model already promoted");
    if (tc_enabled_) {
      throw std::logic_error("cannot promote after temporal coherence starts");
    }
    std::vector<float> promoted(kPhaseEntries);
    const int phase_shared_entries =
        kSharedTables * kMovesPerLevel * kPatternCount;
    for (int phase = 0; phase < kMovesPerLevel; ++phase) {
      for (int table = 0; table < kSharedTables; ++table) {
        const std::size_t source =
            static_cast<std::size_t>(table) * kPatternCount;
        const std::size_t destination =
            static_cast<std::size_t>(phase * kSharedTables + table) *
            kPatternCount;
        std::copy_n(weights_.begin() + static_cast<std::ptrdiff_t>(source),
                    kPatternCount,
                    promoted.begin() +
                        static_cast<std::ptrdiff_t>(destination));
      }
      for (int table = 0; table < kAbsoluteTables; ++table) {
        const std::size_t source =
            static_cast<std::size_t>(kSharedTables + table) * kPatternCount;
        const std::size_t destination =
            static_cast<std::size_t>(phase_shared_entries) +
            static_cast<std::size_t>(phase * kAbsoluteTables + table) *
                kPatternCount;
        std::copy_n(weights_.begin() + static_cast<std::ptrdiff_t>(source),
                    kPatternCount,
                    promoted.begin() +
                        static_cast<std::ptrdiff_t>(destination));
      }
    }
    weights_ = std::move(promoted);
    stage_ = ModelStage::kPhase;
  }

  void enableTemporalCoherence() {
    if (!promoted()) {
      throw std::logic_error("temporal coherence requires promoted phase heads");
    }
    if (tc_enabled_) {
      throw std::logic_error("temporal coherence already enabled");
    }
    signed_error_.assign(weights_.size(), 0.0f);
    absolute_error_.assign(weights_.size(), 0.0f);
    tc_enabled_ = true;
  }

  void writeCheckpoint(std::ostream& output, const Progress& progress,
                       const TrainingContract& contract) const {
    constexpr std::array<char, 8> magic{{'D', '7', 'O', 'P', 'N', 'T', '3', 0}};
    output.write(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!output) throw std::runtime_error("failed writing n-tuple checkpoint");
    writeScalar<std::uint32_t>(output, kCheckpointFormatVersion);
    writeScalar<std::uint8_t>(output, static_cast<std::uint8_t>(stage_));
    writeScalar<std::uint8_t>(output, tc_enabled_ ? 1u : 0u);
    writeScalar<std::uint8_t>(output, progress.game_active ? 1u : 0u);
    writeScalar<std::uint8_t>(output, progress.training_finalized ? 1u : 0u);
    writeScalar<std::uint64_t>(output, progress.transitions);
    writeScalar<std::uint64_t>(output, progress.completed_games);
    writeScalar<std::uint32_t>(output, progress.training_seed_start);
    writeScalar<std::uint32_t>(output, progress.active_game_seed);
    writeState(output, progress.active_state);
    writeScalar<std::uint64_t>(output, progress.cumulative_score);
    writeScalar<std::uint64_t>(output, progress.cumulative_moves);
    writeScalar<std::uint64_t>(output, progress.censored_training_games);
    writeScalar<std::uint32_t>(output, contract.maximum_moves);
    writeScalar<std::uint32_t>(output, contract.reveal_samples);
    writeScalar<std::uint32_t>(output, contract.training_seed_begin);
    writeScalar<std::uint32_t>(output, contract.training_seed_end);
    writeCheckpointHash(output, contract.training_lane_manifest_sha256);
    writeCheckpointHash(output, contract.source_sha256);
    writeCheckpointHash(output, contract.engine_sha256);
    writeCheckpointHash(output, contract.corrected_d4_sha256);
    writeCheckpointHash(output, contract.corrected_d4_leaf_sha256);
    writeCheckpointHash(output, contract.cfpi_behavior_sha256);
    writeScalar<std::uint64_t>(output, kPromotionTransition);
    writeScalar<std::uint64_t>(output, kLearningRateDrop1);
    writeScalar<std::uint64_t>(output, kLearningRateDrop2);
    writeScalar<std::uint64_t>(output, kTemporalCoherenceTransition);
    writeScalar<std::uint64_t>(output, kFrozenTrainingTransitions);
    writeScalar<float>(output, kOptimisticValue);
    writeScalar<float>(output, kScoreScale);
    writeScalar<float>(output, kGamma);
    writeScalar<float>(output, kLambda);
    writeScalar<float>(output, kLearningRate0);
    writeScalar<float>(output, kLearningRate1);
    writeScalar<float>(output, kLearningRate2);
    writeScalar<float>(output, kTemporalCoherenceRate);
    writeScalar<std::uint32_t>(output, kTraceHorizon);
    writeScalar<std::uint64_t>(output, weights_.size());
    writeScalar<std::uint64_t>(output, signed_error_.size());
    writeScalar<std::uint64_t>(output, absolute_error_.size());
    writeFloats(output, weights_);
    writeFloats(output, signed_error_);
    writeFloats(output, absolute_error_);
  }

  Progress readCheckpoint(std::istream& input, TrainingContract& contract) {
    std::array<char, 8> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    constexpr std::array<char, 8> expected{{'D', '7', 'O', 'P', 'N', 'T', '3', 0}};
    if (!input || magic != expected) {
      throw std::runtime_error("incompatible optimistic n-tuple checkpoint");
    }
    if (readScalar<std::uint32_t>(input) != kCheckpointFormatVersion) {
      throw std::runtime_error("unsupported optimistic n-tuple checkpoint");
    }
    const std::uint8_t stage = readScalar<std::uint8_t>(input);
    const std::uint8_t tc_byte = readScalar<std::uint8_t>(input);
    const std::uint8_t game_active = readScalar<std::uint8_t>(input);
    const std::uint8_t finalized = readScalar<std::uint8_t>(input);
    Progress progress;
    progress.game_active = game_active != 0;
    progress.training_finalized = finalized != 0;
    progress.transitions = readScalar<std::uint64_t>(input);
    progress.completed_games = readScalar<std::uint64_t>(input);
    progress.training_seed_start = readScalar<std::uint32_t>(input);
    progress.active_game_seed = readScalar<std::uint32_t>(input);
    progress.active_state = readState(input);
    progress.cumulative_score = readScalar<std::uint64_t>(input);
    progress.cumulative_moves = readScalar<std::uint64_t>(input);
    progress.censored_training_games = readScalar<std::uint64_t>(input);
    contract.maximum_moves = readScalar<std::uint32_t>(input);
    contract.reveal_samples = readScalar<std::uint32_t>(input);
    contract.training_seed_begin = readScalar<std::uint32_t>(input);
    contract.training_seed_end = readScalar<std::uint32_t>(input);
    contract.training_lane_manifest_sha256 = readCheckpointHash(input);
    contract.source_sha256 = readCheckpointHash(input);
    contract.engine_sha256 = readCheckpointHash(input);
    contract.corrected_d4_sha256 = readCheckpointHash(input);
    contract.corrected_d4_leaf_sha256 = readCheckpointHash(input);
    contract.cfpi_behavior_sha256 = readCheckpointHash(input);
    const auto promotion = readScalar<std::uint64_t>(input);
    const auto learning_rate_drop1 = readScalar<std::uint64_t>(input);
    const auto learning_rate_drop2 = readScalar<std::uint64_t>(input);
    const auto temporal_coherence = readScalar<std::uint64_t>(input);
    const auto frozen_transitions = readScalar<std::uint64_t>(input);
    const float optimistic = readScalar<float>(input);
    const float score_scale = readScalar<float>(input);
    const float gamma = readScalar<float>(input);
    const float lambda = readScalar<float>(input);
    const float learning_rate0 = readScalar<float>(input);
    const float learning_rate1 = readScalar<float>(input);
    const float learning_rate2 = readScalar<float>(input);
    const float tc_rate = readScalar<float>(input);
    const auto horizon = readScalar<std::uint32_t>(input);
    const auto weight_count = readScalar<std::uint64_t>(input);
    const auto signed_count = readScalar<std::uint64_t>(input);
    const auto absolute_count = readScalar<std::uint64_t>(input);
    if (stage > static_cast<std::uint8_t>(ModelStage::kPhase) || tc_byte > 1 ||
        game_active > 1 || finalized > 1 ||
        progress.transitions > kFrozenTrainingTransitions ||
        (progress.training_finalized &&
         progress.transitions != kFrozenTrainingTransitions) ||
        (progress.training_finalized && progress.game_active) ||
        contract.maximum_moves != kFrozenMaximumMoves ||
        contract.reveal_samples != kFrozenRevealSamples ||
        contract.training_seed_begin != kTrainingSeedBegin ||
        contract.training_seed_end != kTrainingSeedEnd ||
        contract.training_lane_manifest_sha256 !=
            kCompiledTrainingLaneManifestSha256 ||
        progress.training_seed_start != contract.training_seed_begin ||
        contract.source_sha256 != kCompiledSourceSha256 ||
        contract.engine_sha256 != kCompiledEngineSha256 ||
        contract.corrected_d4_sha256 != kCompiledD4Sha256 ||
        contract.corrected_d4_leaf_sha256 != kCompiledD4LeafSha256 ||
        contract.cfpi_behavior_sha256 != kCompiledCfpiBehaviorSha256 ||
        promotion != kPromotionTransition ||
        learning_rate_drop1 != kLearningRateDrop1 ||
        learning_rate_drop2 != kLearningRateDrop2 ||
        temporal_coherence != kTemporalCoherenceTransition ||
        frozen_transitions != kFrozenTrainingTransitions ||
        optimistic != kOptimisticValue || score_scale != kScoreScale ||
        gamma != kGamma || lambda != kLambda ||
        learning_rate0 != kLearningRate0 ||
        learning_rate1 != kLearningRate1 ||
        learning_rate2 != kLearningRate2 ||
        tc_rate != kTemporalCoherenceRate || horizon != kTraceHorizon) {
      throw std::runtime_error("checkpoint training contract mismatch");
    }
    const bool tc = tc_byte != 0;
    const ModelStage loaded_stage = static_cast<ModelStage>(stage);
    const std::uint64_t expected_weights =
        loaded_stage == ModelStage::kPooled ? kPooledEntries : kPhaseEntries;
    const std::uint64_t expected_aux = tc ? expected_weights : 0u;
    if (weight_count != expected_weights || signed_count != expected_aux ||
        absolute_count != expected_aux ||
        (tc && loaded_stage != ModelStage::kPhase)) {
      throw std::runtime_error("checkpoint parameter shape mismatch");
    }
    stage_ = loaded_stage;
    tc_enabled_ = tc;
    weights_.assign(static_cast<std::size_t>(weight_count), 0.0f);
    signed_error_.assign(static_cast<std::size_t>(signed_count), 0.0f);
    absolute_error_.assign(static_cast<std::size_t>(absolute_count), 0.0f);
    readFloats(input, weights_);
    readFloats(input, signed_error_);
    readFloats(input, absolute_error_);
    if ((progress.transitions >= kPromotionTransition) != promoted() ||
        (progress.transitions >= kTemporalCoherenceTransition) !=
            tc_enabled_) {
      throw std::runtime_error("checkpoint stage disagrees with transition count");
    }
    return progress;
  }

 private:
  float valueFromFeatures(const ActiveFeatures& active) const {
    double result = 0.0;
    for (int index = 0; index < active.count; ++index) {
      result += static_cast<double>(weights_[active.ids[index]]) *
                active.multiplicities[index];
    }
    return static_cast<float>(result);
  }

  ModelStage stage_ = ModelStage::kPooled;
  bool tc_enabled_ = false;
  std::vector<float> weights_;
  std::vector<float> signed_error_;
  std::vector<float> absolute_error_;
};

float rewardForMove(const MoveResult& move) {
  return static_cast<float>(move.score_delta) / kScoreScale;
}

float tdTarget(float reward, float continuation, bool terminal) {
  return reward + (terminal ? 0.0f : kGamma * continuation);
}

std::uint64_t publicHash(const State& source) {
  const State state = canonicalize(source).state;
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint64_t>(cell + 1u);
    hash *= 0x0000'0100'0000'01b3ull;
  }
  hash ^= state.next_disc;
  hash *= 0x0000'0100'0000'01b3ull;
  hash ^= static_cast<std::uint64_t>(state.moves_remaining);
  hash *= 0x0000'0100'0000'01b3ull;
  return hash;
}

std::uint32_t chanceEventBits(const State& state, std::uint32_t domain,
                              std::uint64_t nonce,
                              int depth, std::uint32_t event) {
  const std::uint64_t hash = publicHash(state);
  std::uint32_t value = static_cast<std::uint32_t>(hash) ^
                        static_cast<std::uint32_t>(hash >> 32u) ^ domain ^
                        kChanceCoordinateDomain;
  value = mix32(value ^ static_cast<std::uint32_t>(nonce));
  value = mix32(value ^ static_cast<std::uint32_t>(nonce >> 32u));
  value = mix32(value ^ static_cast<std::uint32_t>(depth + 1) *
                            0x9e37'79b9u);
  return mix32(value ^ (event + 1u) * 0xc2b2'ae35u);
}

// A Latin-hypercube chance pack.  Every stable reveal coordinate and every
// sequential next-visible-disc event is exactly 1..7 over strata 0..6.  The
// action resolves reflection orientation on reflection-fixed boards, so side
// actions mirror exactly; center actions retain distinct left/right events.
class ChancePackRandom {
 public:
  ChancePackRandom(const State& state, int action, std::uint32_t domain,
                   std::uint64_t nonce, int depth, int stratum,
                   std::uint32_t event_prefix = 0)
      : state_(state),
        domain_(domain),
        nonce_(nonce),
        depth_(depth),
        stratum_(stratum),
        event_(event_prefix),
        event_prefix_(event_prefix),
        mirrored_(mirrorIsSmaller(state.board) ||
                  (state.board == mirrorBoard(state.board) &&
                   action > kBoardSize / 2)) {
    if (action < 0 || action >= kBoardSize || stratum < 0 || stratum >= 7) {
      throw std::invalid_argument("chance-pack coordinate outside domain");
    }
  }

  std::uint8_t nextDisc() {
    const int rotation = static_cast<int>(
        chanceEventBits(state_, domain_, nonce_, depth_, event_++) %
        7u);
    return static_cast<std::uint8_t>(1 + (stratum_ + rotation) % 7);
  }

  std::uint8_t nextDiscFor(int row, int column, int wave_depth) const {
    const int canonical_column =
        mirrored_ ? kBoardSize - 1 - column : column;
    const std::uint32_t event =
        event_prefix_ + static_cast<std::uint32_t>(wave_depth * 64) +
        static_cast<std::uint32_t>(row * kBoardSize + canonical_column);
    const int rotation = static_cast<int>(
        chanceEventBits(state_, domain_, nonce_, depth_, event) % 7u);
    return static_cast<std::uint8_t>(1 + (stratum_ + rotation) % 7);
  }

 private:
  State state_;
  std::uint32_t domain_;
  std::uint64_t nonce_;
  int depth_;
  int stratum_;
  std::uint32_t event_;
  std::uint32_t event_prefix_;
  bool mirrored_;
};

template <typename Random>
void resolveCascadeWithChance(Board& board, Random& random,
                              int starting_depth, std::int64_t& score,
                              std::vector<Wave>& waves) {
  for (int depth = starting_depth;; ++depth) {
    int popper_count = 0;
    const auto poppers = findPoppers(board, popper_count);
    if (popper_count == 0) return;
    std::array<bool, kCellCount> popping{};
    Board cleared = board;
    for (int offset = 0; offset < popper_count; ++offset) {
      popping[poppers[offset]] = true;
      cleared[poppers[offset]] = kEmpty;
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
      cleared[reveal_index] = random.nextDiscFor(
          reveal_index / kBoardSize, reveal_index % kBoardSize, depth);
    }
    const std::int64_t points = popper_count * scoreForWave(depth);
    score += points;
    waves.push_back({depth, popper_count, reveal_count, points});
    board = applyGravity(cleared);
  }
}

template <typename Random>
bool playMoveWithChance(const State& state, int column, Random& random,
                        MoveResult& result) {
  if (state.game_over) return false;
  Board board = state.board;
  if (!placeDisc(board, column, state.next_disc)) return false;
  result = MoveResult{};
  std::int64_t first_score = 0;
  resolveCascadeWithChance(board, random, 1, first_score, result.waves);
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
      resolveCascadeWithChance(board, random, next_depth, level_score,
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

std::array<float, kBoardSize> oneStepActionValues(
    const Model& model, const State& source, int reveal_samples,
    std::uint32_t domain, std::uint64_t nonce) {
  if (reveal_samples < 1 || reveal_samples > 7) {
    throw std::invalid_argument("reveal sample count must be in [1, 7]");
  }
  const CanonicalState canonical = canonicalize(source);
  std::array<float, kBoardSize> physical_values{};
  physical_values.fill(-std::numeric_limits<float>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    if (!isLegal(canonical.state.board, action)) continue;
    double total = 0.0;
    for (int sample = 0; sample < reveal_samples; ++sample) {
      ChancePackRandom random(canonical.state, action, domain, nonce, 0,
                              sample);
      MoveResult move;
      if (!playMoveWithChance(canonical.state, action, random, move)) {
        throw std::logic_error("one-step evaluator selected illegal action");
      }
      const float reward = rewardForMove(move);
      total += tdTarget(reward, model.value(move.state),
                        move.state.game_over);
    }
    physical_values[physicalAction(action, canonical.mirrored)] =
        static_cast<float>(total / reveal_samples);
  }
  return physical_values;
}

int greedyAction(const Model& model, const State& state, int reveal_samples,
                 std::uint32_t domain, std::uint64_t nonce) {
  const auto values =
      oneStepActionValues(model, state, reveal_samples, domain, nonce);
  const bool mirrored = canonicalize(state).mirrored;
  constexpr std::array<int, kBoardSize> order{{3, 2, 4, 1, 5, 0, 6}};
  int selected = -1;
  float best = -std::numeric_limits<float>::infinity();
  for (int canonical_action : order) {
    const int physical = physicalAction(canonical_action, mirrored);
    if (values[physical] > best) {
      best = values[physical];
      selected = physical;
    }
  }
  if (!isLegal(state.board, selected)) {
    throw std::logic_error("optimistic n-tuple selected illegal action");
  }
  return selected;
}

struct Schedule {
  UpdateRule rule = UpdateRule::kFixed;
  float rate = kLearningRate0;
  bool should_promote = false;
  bool should_enable_tc = false;
};

Schedule scheduleForTransition(std::uint64_t transitions,
                               const Model& model) {
  Schedule result;
  result.should_promote =
      transitions >= kPromotionTransition && !model.promoted();
  result.should_enable_tc =
      transitions >= kTemporalCoherenceTransition &&
      !model.temporalCoherenceEnabled();
  if (transitions >= kTemporalCoherenceTransition) {
    result.rule = UpdateRule::kTemporalCoherence;
    result.rate = kTemporalCoherenceRate;
  } else if (transitions >= kLearningRateDrop2) {
    result.rate = kLearningRate2;
  } else if (transitions >= kLearningRateDrop1) {
    result.rate = kLearningRate1;
  }
  return result;
}

// Conventional three-delta forward view: each state receives exactly
// delta_t + lambda*delta_{t+1} + lambda^2*delta_{t+2}.  The queue is delayed
// so that checkpoint boundaries can preserve the two not-yet-complete views.
class DelayedForwardTrace {
 public:
  struct Summary {
    std::uint64_t state_updates = 0;
    double absolute_credit = 0.0;
    double beta_sum = 0.0;
    std::uint64_t beta_count = 0;
    float last_credit = 0.0f;
  };

  void observe(const ValueState& state, float td_error, UpdateRule rule,
               float rate, Model& model) {
    if (!std::isfinite(td_error)) {
      throw std::invalid_argument("non-finite TD error");
    }
    for (Pending& pending : pending_) {
      pending.credit += pending.next_coefficient * td_error;
      pending.next_coefficient *= kLambda;
      ++pending.terms;
    }
    pending_.push_back({state, td_error, kLambda, 1, rule, rate});
    if (pending_.front().terms >= kTraceHorizon) applyFront(model);
  }

  void flush(Model& model) {
    while (!pending_.empty()) applyFront(model);
  }

  bool empty() const { return pending_.empty(); }
  std::size_t size() const { return pending_.size(); }
  const Summary& summary() const { return summary_; }

  void writeCheckpoint(std::ostream& output) const {
    writeScalar<std::uint64_t>(output, pending_.size());
    for (const Pending& pending : pending_) {
      output.write(reinterpret_cast<const char*>(pending.state.board.data()),
                   static_cast<std::streamsize>(pending.state.board.size()));
      if (!output) throw std::runtime_error("failed writing n-tuple checkpoint");
      writeScalar<std::uint8_t>(output, pending.state.moves_remaining);
      writeScalar<std::uint8_t>(output, pending.state.terminal ? 1u : 0u);
      writeScalar<float>(output, pending.credit);
      writeScalar<float>(output, pending.next_coefficient);
      writeScalar<std::int32_t>(output, pending.terms);
      writeScalar<std::uint8_t>(output,
                                static_cast<std::uint8_t>(pending.rule));
      writeScalar<float>(output, pending.rate);
    }
    writeScalar<std::uint64_t>(output, summary_.state_updates);
    writeScalar<double>(output, summary_.absolute_credit);
    writeScalar<double>(output, summary_.beta_sum);
    writeScalar<std::uint64_t>(output, summary_.beta_count);
    writeScalar<float>(output, summary_.last_credit);
  }

  void readCheckpoint(std::istream& input) {
    pending_.clear();
    const std::uint64_t count = readScalar<std::uint64_t>(input);
    if (count >= static_cast<std::uint64_t>(kTraceHorizon)) {
      throw std::runtime_error("invalid pending forward-view queue");
    }
    for (std::uint64_t index = 0; index < count; ++index) {
      Pending pending;
      input.read(reinterpret_cast<char*>(pending.state.board.data()),
                 static_cast<std::streamsize>(pending.state.board.size()));
      if (!input) throw std::runtime_error("truncated n-tuple checkpoint");
      pending.state.moves_remaining = readScalar<std::uint8_t>(input);
      const std::uint8_t terminal = readScalar<std::uint8_t>(input);
      pending.credit = readScalar<float>(input);
      pending.next_coefficient = readScalar<float>(input);
      pending.terms = readScalar<std::int32_t>(input);
      const std::uint8_t rule = readScalar<std::uint8_t>(input);
      pending.rate = readScalar<float>(input);
      if (std::any_of(pending.state.board.begin(), pending.state.board.end(),
                      [](std::uint8_t cell) { return cell > kCracked; }) ||
          pending.state.moves_remaining < 1 ||
          pending.state.moves_remaining > kMovesPerLevel || terminal > 1 ||
          terminal != 0 || !std::isfinite(pending.credit) ||
          !std::isfinite(pending.next_coefficient) ||
          !std::isfinite(pending.rate) || pending.rate <= 0.0f ||
          pending.terms < 1 || pending.terms >= kTraceHorizon ||
          rule > static_cast<std::uint8_t>(UpdateRule::kTemporalCoherence)) {
        throw std::runtime_error("invalid pending forward-view entry");
      }
      pending.state.terminal = false;
      pending.rule = static_cast<UpdateRule>(rule);
      pending_.push_back(pending);
    }
    summary_.state_updates = readScalar<std::uint64_t>(input);
    summary_.absolute_credit = readScalar<double>(input);
    summary_.beta_sum = readScalar<double>(input);
    summary_.beta_count = readScalar<std::uint64_t>(input);
    summary_.last_credit = readScalar<float>(input);
    if (!std::isfinite(summary_.absolute_credit) ||
        !std::isfinite(summary_.beta_sum) ||
        !std::isfinite(summary_.last_credit) ||
        summary_.absolute_credit < 0.0 ||
        summary_.beta_count != summary_.state_updates) {
      throw std::runtime_error("invalid forward-view summary");
    }
  }

 private:
  struct Pending {
    ValueState state{};
    float credit = 0.0f;
    float next_coefficient = kLambda;
    int terms = 1;
    UpdateRule rule = UpdateRule::kFixed;
    float rate = kLearningRate0;
  };

  void applyFront(Model& model) {
    const Pending pending = pending_.front();
    pending_.pop_front();
    const UpdateReport report =
        model.update(pending.state, pending.credit, pending.rate,
                     pending.rule);
    ++summary_.state_updates;
    summary_.absolute_credit += std::abs(pending.credit);
    summary_.beta_sum += report.mean_beta;
    ++summary_.beta_count;
    summary_.last_credit = pending.credit;
  }

  std::deque<Pending> pending_;
  Summary summary_;
};

void applyScheduleBoundary(std::uint64_t transitions, Model& model,
                           DelayedForwardTrace& trace) {
  const Schedule schedule = scheduleForTransition(transitions, model);
  if (schedule.should_promote) {
    trace.flush(model);
    model.promote();
  }
  if (schedule.should_enable_tc) {
    trace.flush(model);
    model.enableTemporalCoherence();
  }
}

struct SearchOptions {
  int maximum_boundaries = 1;
  int reveal_samples = 7;
  int internal_action_width = 2;
  std::uint64_t maximum_work = 100'000;
};

struct SearchDecision {
  int action = -1;
  float value = -std::numeric_limits<float>::infinity();
  int completed_boundaries = 0;
  std::uint64_t work = 0;
  bool last_iteration_complete = false;
  bool full_root = false;
  bool used_direct_fallback = true;
};

struct SearchContext {
  const Model& model;
  const SearchOptions& options;
  std::uint64_t nonce = 0;
  std::uint64_t work = 0;
  bool complete = true;
};

std::uint64_t boundaryWorkUpperBound(const State& state, int boundaries) {
  int legal_count = 0;
  legalColumns(state.board, legal_count);
  const int moves_through_boundary =
      state.moves_remaining + (boundaries - 1) * kMovesPerLevel;
  const std::uint64_t root_work =
      static_cast<std::uint64_t>(legal_count) * 7u;
  const std::uint64_t per_internal_move =
      static_cast<std::uint64_t>(kBoardSize * 7 +
                                 kFrozenInternalActionWidth * 7);
  return root_work +
         static_cast<std::uint64_t>(legal_count) *
             static_cast<std::uint64_t>(moves_through_boundary - 1) *
             per_internal_move;
}

struct ActionBundle {
  int action = -1;
  float expected_total = -std::numeric_limits<float>::infinity();
  float expected_reward = 0.0f;
  MoveResult representative{};
};

ActionBundle evaluateActionBundle(const State& canonical, int action,
                                  std::uint32_t domain, int depth,
                                  SearchContext& context) {
  std::array<MoveResult, 7> outcomes{};
  std::array<float, 7> backed_values{};
  double total_value = 0.0;
  double total_reward = 0.0;
  for (int stratum = 0; stratum < 7; ++stratum) {
    if (context.work >= context.options.maximum_work) {
      context.complete = false;
      return {};
    }
    ChancePackRandom random(canonical, action, domain, context.nonce, depth,
                            stratum);
    if (!playMoveWithChance(canonical, action, random, outcomes[stratum])) {
      throw std::logic_error("chance bundle selected illegal action");
    }
    ++context.work;
    const float reward = rewardForMove(outcomes[stratum]);
    backed_values[stratum] =
        tdTarget(reward, context.model.value(outcomes[stratum].state),
                 outcomes[stratum].state.game_over);
    total_reward += reward;
    total_value += backed_values[stratum];
  }
  ActionBundle result;
  result.action = action;
  result.expected_total = static_cast<float>(total_value / 7.0);
  result.expected_reward = static_cast<float>(total_reward / 7.0);
  int representative = 0;
  float best_distance =
      std::abs(backed_values[0] - result.expected_total);
  for (int stratum = 1; stratum < 7; ++stratum) {
    const float distance =
        std::abs(backed_values[stratum] - result.expected_total);
    if (distance < best_distance) {
      best_distance = distance;
      representative = stratum;
    }
  }
  result.representative = std::move(outcomes[representative]);
  return result;
}

std::optional<ActionBundle> selectInternalBundle(const State& source,
                                                 int depth,
                                                 SearchContext& context) {
  const CanonicalState canonical = canonicalize(source);
  int legal_count = 0;
  const auto legal = legalColumns(canonical.state.board, legal_count);
  struct RankedAction {
    int action = -1;
    float value = -std::numeric_limits<float>::infinity();
  };
  std::array<RankedAction, kBoardSize> ranked{};
  for (int offset = 0; offset < legal_count; ++offset) {
    const int action = legal[offset];
    const ActionBundle ordering = evaluateActionBundle(
        canonical.state, action, kSearchOrderDomain, depth, context);
    ranked[offset] = {action, ordering.expected_total};
    if (!context.complete) return std::nullopt;
  }
  constexpr std::array<int, kBoardSize> tie_rank{{5, 3, 1, 0, 2, 4, 6}};
  const auto ranks_before = [&](const RankedAction& first,
                                const RankedAction& second) {
    if (first.value != second.value) return first.value > second.value;
    return tie_rank[first.action] < tie_rank[second.action];
  };
  for (int index = 1; index < legal_count; ++index) {
    const RankedAction value = ranked[index];
    int destination = index;
    while (destination > 0 &&
           ranks_before(value, ranked[destination - 1])) {
      ranked[destination] = ranked[destination - 1];
      --destination;
    }
    ranked[destination] = value;
  }
  const int admitted =
      std::min(context.options.internal_action_width, legal_count);
  if (admitted < 1) return std::nullopt;
  ActionBundle selected;
  for (int index = 0; index < admitted; ++index) {
    ActionBundle candidate = evaluateActionBundle(
        canonical.state, ranked[index].action, kSearchRevealDomain, depth,
        context);
    if (!context.complete) return std::nullopt;
    if (candidate.expected_total > selected.expected_total ||
        (candidate.expected_total == selected.expected_total &&
         (selected.action < 0 ||
          tie_rank[candidate.action] < tie_rank[selected.action]))) {
      selected = std::move(candidate);
    }
  }
  return selected;
}

float rolloutRootAction(const State& canonical, int root_action,
                        int boundaries, SearchContext& context) {
  ActionBundle root = evaluateActionBundle(
      canonical, root_action, kSearchRevealDomain, 0, context);
  if (!context.complete) return context.model.value(canonical);
  float value = root.expected_reward;
  State state = root.representative.state;
  int remaining =
      boundaries - (root.representative.level_advanced ? 1 : 0);
  int depth = 1;
  while (!state.game_over && remaining > 0) {
    const std::optional<ActionBundle> selected =
        selectInternalBundle(state, depth, context);
    if (!context.complete || !selected.has_value()) {
      return context.model.value(canonical);
    }
    value += selected->expected_reward;
    state = selected->representative.state;
    if (selected->representative.level_advanced) --remaining;
    ++depth;
  }
  if (!state.game_over) value += context.model.value(state);
  return value;
}

SearchDecision searchRoot(const State& source, int boundaries,
                          SearchContext& context) {
  const CanonicalState canonical = canonicalize(source);
  int legal_count = 0;
  const auto legal = legalColumns(canonical.state.board, legal_count);
  constexpr std::array<int, kBoardSize> tie_rank{{5, 3, 1, 0, 2, 4, 6}};
  int best_action = -1;
  float best = -std::numeric_limits<float>::infinity();
  for (int offset = 0; offset < legal_count; ++offset) {
    const int action = legal[offset];
    const float value =
        rolloutRootAction(canonical.state, action, boundaries, context);
    if (!context.complete) return {};
    if (value > best ||
        (value == best && best_action >= 0 &&
         tie_rank[action] < tie_rank[best_action])) {
      best = value;
      best_action = action;
    }
  }
  return {physicalAction(best_action, canonical.mirrored), best, boundaries,
          context.work, true, true, false};
}

SearchDecision chooseEventBoundaryAction(const Model& model,
                                         const State& source,
                                         const SearchOptions& options,
                                         std::uint64_t nonce = 0) {
  if (source.game_over) return {};
  if (options.maximum_boundaries < 1 || options.maximum_boundaries > 2 ||
      options.reveal_samples != 7 || options.internal_action_width != 2 ||
      options.maximum_work < 1) {
    throw std::invalid_argument("invalid event-boundary search options");
  }
  const int fallback = centerFirstMove(source.board);
  SearchDecision best;
  best.action = fallback;
  best.value = model.value(source);
  best.work = 0;
  best.used_direct_fallback = true;

  SearchContext context{model, options, nonce};
  for (int boundaries = 1; boundaries <= options.maximum_boundaries;
       ++boundaries) {
    const std::uint64_t bound = boundaryWorkUpperBound(source, boundaries);
    if (bound > options.maximum_work - context.work) {
      best.work = context.work;
      best.last_iteration_complete = false;
      break;
    }
    context.complete = true;
    SearchDecision candidate = searchRoot(source, boundaries, context);
    if (!context.complete || !candidate.last_iteration_complete ||
        !candidate.full_root ||
        !isLegal(source.board, candidate.action)) {
      best.work = context.work;
      best.last_iteration_complete = false;
      break;
    }
    candidate.work = context.work;
    candidate.used_direct_fallback = false;
    best = candidate;
  }
  best.work = context.work;
  return best;
}

bool laneOpen(SeedUse use) {
  switch (use) {
    case SeedUse::kTraining:
      return kTrainingSeedBegin < kTrainingSeedEnd &&
             validSha256Hex(kCompiledTrainingLaneManifestSha256);
    case SeedUse::kBurnedStageA:
      return kBurnedStageASeedBegin < kBurnedStageASeedEnd;
    case SeedUse::kDevelopment:
      return kDevelopmentSeedBegin < kDevelopmentSeedEnd &&
             validSha256Hex(kCompiledDevelopmentLaneManifestSha256) &&
             validSha256Hex(kCompiledStageAQualificationSha256);
  }
  return false;
}

bool allowedSeed(std::uint32_t seed, SeedUse use) {
  const std::uint32_t family = seed >> 24u;
  if (family == 0x4du || family == 0x7du || family == 0xd7u) return false;
  std::uint32_t begin = 0;
  std::uint32_t end = 0;
  switch (use) {
    case SeedUse::kTraining:
      begin = kTrainingSeedBegin;
      end = kTrainingSeedEnd;
      break;
    case SeedUse::kBurnedStageA:
      begin = kBurnedStageASeedBegin;
      end = kBurnedStageASeedEnd;
      break;
    case SeedUse::kDevelopment:
      begin = kDevelopmentSeedBegin;
      end = kDevelopmentSeedEnd;
      break;
  }
  return laneOpen(use) && seed >= begin && seed < end;
}

void requireSeedRange(std::uint32_t begin, std::uint64_t games, SeedUse use) {
  if (games < 1 || games > std::numeric_limits<std::uint32_t>::max()) {
    throw std::invalid_argument("invalid game-seed range length");
  }
  const std::uint64_t last =
      static_cast<std::uint64_t>(begin) + games - 1u;
  if (last > std::numeric_limits<std::uint32_t>::max() ||
      !allowedSeed(begin, use) ||
      !allowedSeed(static_cast<std::uint32_t>(last), use)) {
    throw std::invalid_argument(
        "gameplay seed lane is closed pending a preregistered source freeze");
  }
}

std::uint64_t peakResidentBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
#endif
}

constexpr std::array<char, 8> kCheckpointEnvelopeMagic{
    {'D', '7', 'C', 'K', 'P', 'T', '3', 0}};
constexpr std::uint64_t kCheckpointHeaderBytes =
    kCheckpointEnvelopeMagic.size() + sizeof(std::uint32_t) +
    sizeof(std::uint64_t) + sizeof(std::uint64_t);

std::uint64_t checksumStream(std::istream& input, std::uint64_t bytes) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  std::array<char, 64 * 1024> buffer{};
  while (bytes > 0) {
    const std::size_t chunk = static_cast<std::size_t>(
        std::min<std::uint64_t>(bytes, buffer.size()));
    input.read(buffer.data(), static_cast<std::streamsize>(chunk));
    if (!input) throw std::runtime_error("truncated n-tuple checkpoint");
    for (std::size_t index = 0; index < chunk; ++index) {
      hash ^= static_cast<unsigned char>(buffer[index]);
      hash *= 0x0000'0100'0000'01b3ull;
    }
    bytes -= chunk;
  }
  return hash;
}

void saveCheckpoint(const std::string& path, const Model& model,
                    const Progress& progress,
                    const DelayedForwardTrace& trace,
                    const TrainingContract& contract) {
  if (path.empty()) throw std::invalid_argument("empty checkpoint path");
  const std::filesystem::path destination(path);
  const std::filesystem::path temporary = destination.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("could not open checkpoint temp file");
    output.write(kCheckpointEnvelopeMagic.data(),
                 static_cast<std::streamsize>(kCheckpointEnvelopeMagic.size()));
    writeScalar<std::uint32_t>(output, kCheckpointFormatVersion);
    writeScalar<std::uint64_t>(output, 0u);
    writeScalar<std::uint64_t>(output, 0u);
    model.writeCheckpoint(output, progress, contract);
    trace.writeCheckpoint(output);
    output.flush();
    if (!output) throw std::runtime_error("failed flushing checkpoint temp file");
  }

  const std::uint64_t file_bytes = std::filesystem::file_size(temporary);
  if (file_bytes <= kCheckpointHeaderBytes) {
    throw std::runtime_error("empty n-tuple checkpoint payload");
  }
  const std::uint64_t payload_bytes = file_bytes - kCheckpointHeaderBytes;
  std::uint64_t checksum = 0;
  {
    std::ifstream input(temporary, std::ios::binary);
    if (!input) throw std::runtime_error("could not verify checkpoint temp file");
    input.seekg(static_cast<std::streamoff>(kCheckpointHeaderBytes));
    checksum = checksumStream(input, payload_bytes);
  }
  {
    std::fstream output(temporary,
                        std::ios::binary | std::ios::in | std::ios::out);
    if (!output) throw std::runtime_error("could not seal checkpoint temp file");
    output.seekp(static_cast<std::streamoff>(kCheckpointEnvelopeMagic.size() +
                                             sizeof(std::uint32_t)));
    writeScalar<std::uint64_t>(output, payload_bytes);
    writeScalar<std::uint64_t>(output, checksum);
    output.flush();
    if (!output) throw std::runtime_error("failed sealing checkpoint temp file");
  }
  std::error_code error;
  std::filesystem::rename(temporary, destination, error);
  if (error) {
    std::filesystem::remove(temporary);
    throw std::runtime_error("atomic checkpoint rename failed: " +
                             error.message());
  }
}

Progress loadCheckpoint(const std::string& path, Model& model,
                        DelayedForwardTrace& trace,
                        TrainingContract& contract) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not open checkpoint for read");
  std::array<char, 8> magic{};
  input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  const std::uint32_t version = readScalar<std::uint32_t>(input);
  const std::uint64_t payload_bytes = readScalar<std::uint64_t>(input);
  const std::uint64_t expected_checksum = readScalar<std::uint64_t>(input);
  const std::uint64_t file_bytes = std::filesystem::file_size(path);
  if (magic != kCheckpointEnvelopeMagic ||
      version != kCheckpointFormatVersion ||
      payload_bytes == 0 ||
      payload_bytes != file_bytes - kCheckpointHeaderBytes) {
    throw std::runtime_error("invalid n-tuple checkpoint envelope");
  }
  const std::streampos payload_position = input.tellg();
  if (checksumStream(input, payload_bytes) != expected_checksum) {
    throw std::runtime_error("n-tuple checkpoint checksum mismatch");
  }
  input.clear();
  input.seekg(payload_position);
  Progress progress = model.readCheckpoint(input, contract);
  trace.readCheckpoint(input);
  const std::streampos consumed = input.tellg();
  if (consumed < payload_position ||
      static_cast<std::uint64_t>(consumed - payload_position) !=
          payload_bytes || input.peek() != std::char_traits<char>::eof()) {
    throw std::runtime_error("trailing bytes in n-tuple checkpoint payload");
  }
  if (!progress.game_active && !trace.empty()) {
    throw std::runtime_error("checkpoint game/trace boundary mismatch");
  }
  return progress;
}

struct TrainingOptions {
  std::uint64_t additional_games = 1;
  std::uint64_t stop_after_transitions = kFrozenTrainingTransitions;
  int maximum_moves = kFrozenMaximumMoves;
  int reveal_samples = kFrozenRevealSamples;
  std::uint32_t training_seed_start = 0;
  bool training_seed_start_set = false;
  std::string checkpoint;
  std::string resume;
  std::string source_directory;
  std::string lane_manifest;
};

struct EvaluationOptions {
  std::uint64_t games = 64;
  int maximum_moves = kFrozenMaximumMoves;
  int reveal_samples = kFrozenRevealSamples;
  int event_boundaries = kFrozenEventBoundaries;
  int internal_action_width = kFrozenInternalActionWidth;
  std::uint64_t maximum_work = kFrozenMaximumSearchWork;
  std::uint32_t seed_start = 0;
  bool seed_start_set = false;
  EvaluationStage stage = EvaluationStage::kFinalDevelopment;
  bool stage_set = false;
  std::string checkpoint;
  std::string source_directory;
  std::string lane_manifest;
  std::string qualification;
  std::string qualification_output;
};

std::uint64_t parseUnsigned(std::string_view text, std::string_view name) {
  std::size_t consumed = 0;
  const std::string copy(text);
  const unsigned long long value = std::stoull(copy, &consumed, 0);
  if (consumed != copy.size()) {
    throw std::invalid_argument("invalid " + std::string(name));
  }
  return static_cast<std::uint64_t>(value);
}

int parseInt(std::string_view text, std::string_view name) {
  const std::uint64_t value = parseUnsigned(text, name);
  if (value > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
    throw std::invalid_argument("out-of-range " + std::string(name));
  }
  return static_cast<int>(value);
}

std::uint32_t parseSeed(std::string_view text, std::string_view name) {
  const std::uint64_t value = parseUnsigned(text, name);
  if (value > std::numeric_limits<std::uint32_t>::max()) {
    throw std::invalid_argument("out-of-range " + std::string(name));
  }
  return static_cast<std::uint32_t>(value);
}

TrainingOptions parseTrainingOptions(int argc, char** argv, int begin) {
  TrainingOptions result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string_view option(argv[index]);
    const std::string_view value(argv[index + 1]);
    if (option == "--games") {
      result.additional_games = parseUnsigned(value, "games");
    } else if (option == "--stop-after-transitions") {
      result.stop_after_transitions =
          parseUnsigned(value, "stop-after-transitions");
    } else if (option == "--max-moves") {
      result.maximum_moves = parseInt(value, "max-moves");
    } else if (option == "--reveal-samples") {
      result.reveal_samples = parseInt(value, "reveal-samples");
    } else if (option == "--training-seed-start") {
      result.training_seed_start = parseSeed(value, "training-seed-start");
      result.training_seed_start_set = true;
    } else if (option == "--checkpoint") {
      result.checkpoint = value;
    } else if (option == "--resume") {
      result.resume = value;
    } else if (option == "--source-directory") {
      result.source_directory = value;
    } else if (option == "--lane-manifest") {
      result.lane_manifest = value;
    } else {
      throw std::invalid_argument("unknown training option " +
                                  std::string(option));
    }
  }
  return result;
}

EvaluationOptions parseEvaluationOptions(int argc, char** argv, int begin) {
  EvaluationOptions result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string_view option(argv[index]);
    const std::string_view value(argv[index + 1]);
    if (option == "--games") {
      result.games = parseUnsigned(value, "games");
    } else if (option == "--max-moves") {
      result.maximum_moves = parseInt(value, "max-moves");
    } else if (option == "--reveal-samples") {
      result.reveal_samples = parseInt(value, "reveal-samples");
    } else if (option == "--event-boundaries") {
      result.event_boundaries = parseInt(value, "event-boundaries");
    } else if (option == "--internal-action-width") {
      result.internal_action_width =
          parseInt(value, "internal-action-width");
    } else if (option == "--max-work") {
      result.maximum_work = parseUnsigned(value, "max-work");
    } else if (option == "--seed-start") {
      result.seed_start = parseSeed(value, "seed-start");
      result.seed_start_set = true;
    } else if (option == "--evaluation-stage") {
      if (value == "burned-stage-a") {
        result.stage = EvaluationStage::kBurnedStageA;
      } else if (value == "final-development") {
        result.stage = EvaluationStage::kFinalDevelopment;
      } else {
        throw std::invalid_argument("unknown evaluation stage");
      }
      result.stage_set = true;
    } else if (option == "--checkpoint") {
      result.checkpoint = value;
    } else if (option == "--source-directory") {
      result.source_directory = value;
    } else if (option == "--lane-manifest") {
      result.lane_manifest = value;
    } else if (option == "--qualification") {
      result.qualification = value;
    } else if (option == "--qualification-output") {
      result.qualification_output = value;
    } else {
      throw std::invalid_argument("unknown evaluation option " +
                                  std::string(option));
    }
  }
  return result;
}

int train(const TrainingOptions& options, std::ostream& output) {
  if (options.additional_games < 1 ||
      options.maximum_moves != kFrozenMaximumMoves ||
      options.reveal_samples != kFrozenRevealSamples ||
      options.stop_after_transitions < 1 ||
      options.stop_after_transitions > kFrozenTrainingTransitions ||
      options.checkpoint.empty() || options.source_directory.empty() ||
      options.lane_manifest.empty()) {
    throw std::invalid_argument("invalid optimistic n-tuple training options");
  }

  const std::filesystem::path source_directory(options.source_directory);
  const std::array<std::filesystem::path, 6> protected_paths{{
      std::filesystem::path(options.lane_manifest),
      source_directory / "approaches/ntuple-rl/optimistic-phase/optimistic-phase-ntuple.cpp",
      source_directory / "src/core/native/engine.hpp",
      source_directory / "approaches/fair-expectimax/reference/fair-only-depth4.cpp",
      source_directory / "approaches/fair-expectimax/reference/fair-only-horizon.cpp",
      source_directory / "src/core/native/public-behavior.hpp",
  }};
  const std::filesystem::path checkpoint_output(options.checkpoint);
  const std::filesystem::path checkpoint_temporary =
      checkpoint_output.string() + ".tmp";
  for (const std::filesystem::path& protected_path : protected_paths) {
    if (sameFilesystemTarget(checkpoint_output, protected_path) ||
        sameFilesystemTarget(checkpoint_temporary, protected_path)) {
      throw std::invalid_argument("checkpoint output aliases a protected input");
    }
  }

  const ProvenanceHashes provenance =
      verifyCompiledProvenance(options.source_directory);
  const std::string training_lane_manifest_sha256 =
      verifyLaneManifest(options.lane_manifest, SeedUse::kTraining);

  Model model;
  Progress progress;
  DelayedForwardTrace trace;
  TrainingContract contract;
  if (!options.resume.empty()) {
    progress = loadCheckpoint(options.resume, model, trace, contract);
    if (options.training_seed_start_set &&
        options.training_seed_start != progress.training_seed_start) {
      throw std::invalid_argument("resume seed start differs from checkpoint");
    }
    if (contract != TrainingContract{}) {
      throw std::invalid_argument("resume training contract differs");
    }
  } else {
    if (!options.training_seed_start_set) {
      throw std::invalid_argument("new training requires --training-seed-start");
    }
    if (options.training_seed_start != kTrainingSeedBegin) {
      throw std::invalid_argument("training must start at the fit-lane begin");
    }
    progress.training_seed_start = options.training_seed_start;
  }
  if (progress.training_finalized ||
      options.stop_after_transitions < progress.transitions) {
    throw std::invalid_argument("cannot overtrain or rewind a checkpoint");
  }

  const std::uint64_t first_game = progress.completed_games;
  if (first_game > std::numeric_limits<std::uint32_t>::max()) {
    throw std::invalid_argument("training game cursor overflow");
  }
  const std::uint64_t first_seed_wide = progress.game_active
      ? progress.active_game_seed
      : static_cast<std::uint64_t>(progress.training_seed_start) + first_game;
  if (first_seed_wide > std::numeric_limits<std::uint32_t>::max()) {
    throw std::invalid_argument("training seed cursor overflow");
  }
  requireSeedRange(static_cast<std::uint32_t>(first_seed_wide),
                   options.additional_games, SeedUse::kTraining);

  const auto started = std::chrono::steady_clock::now();
  const std::uint64_t score_before = progress.cumulative_score;
  const std::uint64_t moves_before = progress.cumulative_moves;
  std::uint64_t games_run = 0;
  while (games_run < options.additional_games &&
         progress.transitions < options.stop_after_transitions) {
    if (!progress.game_active) {
      const std::uint64_t seed_wide =
          static_cast<std::uint64_t>(progress.training_seed_start) +
          progress.completed_games;
      if (seed_wide > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("training seed overflow");
      }
      progress.active_game_seed = static_cast<std::uint32_t>(seed_wide);
      if (!allowedSeed(progress.active_game_seed, SeedUse::kTraining)) {
        throw std::runtime_error("training escaped frozen seed lane");
      }
      progress.active_state = initialHeadlessState(progress.active_game_seed);
      progress.game_active = true;
      if (!trace.empty()) {
        throw std::logic_error("new game started with a pending forward view");
      }
    }
    State& state = progress.active_state;
    const std::uint32_t seed = progress.active_game_seed;
    while (!state.game_over && state.moves_played < options.maximum_moves &&
           progress.transitions < options.stop_after_transitions) {
      applyScheduleBoundary(progress.transitions, model, trace);
      const Schedule schedule =
          scheduleForTransition(progress.transitions, model);
      const ValueState previous = valueState(state);
      const float prediction = model.value(previous);
      const int action = greedyAction(
          model, state, options.reveal_samples, kPolicyRevealDomain,
          progress.transitions);
      MoveResult move;
      if (!playHeadlessMove(state, seed, action, move)) {
        throw std::logic_error("training policy selected illegal action");
      }
      if (!state.game_over &&
          state.next_disc != headlessDisc(seed, state.moves_played)) {
        throw std::logic_error("headless visible-disc stream mismatch");
      }
      const float target =
          tdTarget(rewardForMove(move), model.value(state), state.game_over);
      trace.observe(previous, target - prediction, schedule.rule,
                    schedule.rate, model);
      ++progress.transitions;
    }
    const bool game_finished =
        state.game_over || state.moves_played >= options.maximum_moves;
    const bool frozen_limit =
        progress.transitions == kFrozenTrainingTransitions;
    if (game_finished || frozen_limit) {
      trace.flush(model);
      progress.cumulative_score += static_cast<std::uint64_t>(state.score);
      progress.cumulative_moves +=
          static_cast<std::uint64_t>(state.moves_played);
      if (!state.game_over) ++progress.censored_training_games;
      progress.game_active = false;
      ++progress.completed_games;
      ++games_run;
      if (frozen_limit) progress.training_finalized = true;
    } else {
      break;
    }
  }
  applyScheduleBoundary(progress.transitions, model, trace);
  if (progress.transitions == kFrozenTrainingTransitions) {
    if (progress.game_active) {
      throw std::logic_error("frozen training limit retained an active game");
    }
    progress.training_finalized = true;
  }
  saveCheckpoint(options.checkpoint, model, progress, trace, contract);
  const double seconds = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - started)
                             .count();
  const auto& trace_summary = trace.summary();
  output << std::fixed << std::setprecision(6)
         << "OPTIMISTIC_PHASE_NTUPLE_TRAIN {\"games\":" << games_run
         << ",\"transitions\":" << progress.transitions
         << ",\"stage\":\""
         << (model.promoted() ? "phase" : "pooled") << "\""
         << ",\"temporalCoherence\":"
         << (model.temporalCoherenceEnabled() ? "true" : "false")
         << ",\"trainingSourceSha256\":\"" << provenance.source
         << "\",\"trainingEngineSha256\":\"" << provenance.engine
         << "\",\"trainingCorrectedD4Sha256\":\""
         << provenance.corrected_d4
         << "\",\"trainingCorrectedD4LeafSha256\":\""
         << provenance.corrected_d4_leaf
         << "\",\"trainingCfpiBehaviorSha256\":\""
         << provenance.cfpi_behavior << "\""
         << ",\"trainingLaneManifestSha256\":\""
         << training_lane_manifest_sha256 << "\""
         << ",\"meanScore\":"
         << (games_run > 0
                 ? static_cast<double>(progress.cumulative_score -
                                       score_before) /
                       games_run
                 : 0.0)
         << ",\"meanMoves\":"
         << (games_run > 0
                 ? static_cast<double>(progress.cumulative_moves -
                                       moves_before) /
                       games_run
                 : 0.0)
         << ",\"stateUpdates\":" << trace_summary.state_updates
         << ",\"meanAbsoluteTraceCredit\":"
         << (trace_summary.state_updates > 0
                 ? trace_summary.absolute_credit /
                       trace_summary.state_updates
                 : 0.0)
         << ",\"meanTcBeta\":"
         << (trace_summary.beta_count > 0
                 ? trace_summary.beta_sum / trace_summary.beta_count
                 : 0.0)
         << ",\"seconds\":" << seconds
         << ",\"parameterBytes\":" << model.parameterBytes()
         << ",\"gameActive\":"
         << (progress.game_active ? "true" : "false")
         << ",\"trainingFinalized\":"
         << (progress.training_finalized ? "true" : "false")
         << ",\"pendingForwardViews\":" << trace.size()
         << ",\"censoredTrainingGames\":"
         << progress.censored_training_games
         << ",\"peakResidentBytes\":" << peakResidentBytes()
         << ",\"checkpoint\":\"" << options.checkpoint << "\"}\n";
  return 0;
}

enum class PolicyKind : std::uint8_t { kDirect = 0, kBoundarySearch = 1 };

struct PolicyGameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t numbered_cleared = 0;
  std::uint64_t covered_revealed = 0;
  std::uint64_t chain_depth_sum = 0;
  int maximum_chain_depth = 0;
  std::uint64_t work = 0;
  std::string checksum;
};

std::string resultChecksum(const PolicyGameResult& result,
                           const Board& final_board) {
  std::ostringstream canonical;
  canonical << result.seed << ':' << result.score << ':' << result.moves << ':'
            << (result.censored ? 1 : 0) << ':' << result.numbered_cleared
            << ':' << result.covered_revealed << ':'
            << result.chain_depth_sum << ':' << result.maximum_chain_depth
            << ':' << result.work << ':' << serializeBoard(final_board);
  return sha256(canonical.str());
}

PolicyGameResult runPolicyGame(const Model& model, std::uint32_t seed,
                               PolicyKind policy,
                               const EvaluationOptions& options) {
  State state = initialHeadlessState(seed);
  PolicyGameResult result;
  result.seed = seed;
  SearchOptions search_options{options.event_boundaries,
                               options.reveal_samples,
                               options.internal_action_width,
                               options.maximum_work};
  while (!state.game_over && state.moves_played < options.maximum_moves) {
    int action = -1;
    if (policy == PolicyKind::kDirect) {
      action = greedyAction(model, state, options.reveal_samples,
                            kPolicyRevealDomain, 0);
    } else {
      const SearchDecision decision =
          chooseEventBoundaryAction(model, state, search_options, 0);
      if (!decision.last_iteration_complete ||
          decision.completed_boundaries != options.event_boundaries ||
          !decision.full_root || decision.used_direct_fallback) {
        throw std::runtime_error(
            "frozen boundary search failed its fixed-work completion proof");
      }
      action = decision.action;
      result.work += decision.work;
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::logic_error("evaluation policy selected illegal action");
    }
    int move_chain = 0;
    for (const Wave& wave : move.waves) {
      result.numbered_cleared += static_cast<std::uint64_t>(wave.cleared);
      result.covered_revealed += static_cast<std::uint64_t>(wave.revealed);
      move_chain = std::max(move_chain, wave.depth);
    }
    result.chain_depth_sum += static_cast<std::uint64_t>(move_chain);
    result.maximum_chain_depth =
        std::max(result.maximum_chain_depth, move_chain);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.checksum = resultChecksum(result, state.board);
  return result;
}

PolicyGameResult runCorrectedD4Game(std::uint32_t seed) {
  State state = initialHeadlessState(seed);
  PolicyGameResult result;
  result.seed = seed;
  while (!state.game_over &&
         state.moves_played < drop7::fair_only_depth4::kMaximumMoves) {
    const auto decision = drop7::fair_only_depth4::chooseDepth4Action(state);
    if (!decision.complete || decision.completed_depth != 4 ||
        !isLegal(state.board, decision.action)) {
      throw std::runtime_error("corrected fair-D4 comparator did not complete");
    }
    result.work += decision.work;
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::logic_error("corrected fair-D4 selected illegal action");
    }
    int move_chain = 0;
    for (const Wave& wave : move.waves) {
      result.numbered_cleared += static_cast<std::uint64_t>(wave.cleared);
      result.covered_revealed += static_cast<std::uint64_t>(wave.revealed);
      move_chain = std::max(move_chain, wave.depth);
    }
    result.chain_depth_sum += static_cast<std::uint64_t>(move_chain);
    result.maximum_chain_depth =
        std::max(result.maximum_chain_depth, move_chain);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.checksum = resultChecksum(result, state.board);
  return result;
}

double empiricalQuantile(std::vector<double> values, double probability) {
  if (values.empty() || probability < 0.0 || probability > 1.0) {
    throw std::invalid_argument("invalid empirical quantile");
  }
  std::sort(values.begin(), values.end());
  const double position = probability * (values.size() - 1);
  const std::size_t lower = static_cast<std::size_t>(position);
  const std::size_t upper = std::min(lower + 1, values.size() - 1);
  const double fraction = position - static_cast<double>(lower);
  return values[lower] * (1.0 - fraction) + values[upper] * fraction;
}

double arithmeticMean(const std::vector<double>& values) {
  if (values.empty()) throw std::invalid_argument("mean of empty sample");
  return std::accumulate(values.begin(), values.end(), 0.0) /
         static_cast<double>(values.size());
}

double studentCritical(std::size_t games) {
  if (games == 64) return 1.669013;
  if (games == 256) return 1.650851;
  throw std::invalid_argument("no frozen one-sided t critical for cohort");
}

double studentLower95(const std::vector<double>& values) {
  const double mean = arithmeticMean(values);
  double squared = 0.0;
  for (double value : values) squared += (value - mean) * (value - mean);
  const double deviation =
      std::sqrt(squared / static_cast<double>(values.size() - 1));
  return mean - studentCritical(values.size()) * deviation /
                    std::sqrt(static_cast<double>(values.size()));
}

double bootstrapLower95(const std::vector<double>& values,
                        int replicates = 100'000) {
  if (values.empty() || replicates < 1) {
    throw std::invalid_argument("invalid whole-game bootstrap");
  }
  Mulberry32 random(0xd7b0'057au);
  std::vector<double> means(static_cast<std::size_t>(replicates));
  for (double& mean : means) {
    double total = 0.0;
    for (std::size_t draw = 0; draw < values.size(); ++draw) {
      const std::size_t index = static_cast<std::size_t>(
          (static_cast<std::uint64_t>(random.nextBits()) * values.size()) >>
          32u);
      total += values[index];
    }
    mean = total / static_cast<double>(values.size());
  }
  std::sort(means.begin(), means.end());
  return means[static_cast<std::size_t>(
      std::floor(0.05 * static_cast<double>(means.size() - 1)))];
}

struct EvaluationSummary {
  double mean_score = 0.0;
  double median_score = 0.0;
  double lower_quartile_score = 0.0;
  double minimum_score = 0.0;
  double mean_moves = 0.0;
  double lower_quartile_moves = 0.0;
  double mean_work = 0.0;
  double numbered_clears_per_move = 0.0;
  double covered_reveals_per_move = 0.0;
  double mean_chain_depth = 0.0;
  int maximum_chain_depth = 0;
  int censored = 0;
  double bootstrap_lower95 = 0.0;
  double student_t_lower95 = 0.0;
};

EvaluationSummary summarizeGames(
    const std::vector<PolicyGameResult>& games) {
  if (games.size() != 64 && games.size() != 256) {
    throw std::invalid_argument("summary requires a frozen cohort size");
  }
  std::vector<double> scores;
  std::vector<double> moves;
  scores.reserve(games.size());
  moves.reserve(games.size());
  std::uint64_t total_moves = 0;
  std::uint64_t total_clears = 0;
  std::uint64_t total_reveals = 0;
  std::uint64_t total_chain = 0;
  std::uint64_t total_work = 0;
  EvaluationSummary result;
  for (const PolicyGameResult& game : games) {
    scores.push_back(static_cast<double>(game.score));
    moves.push_back(static_cast<double>(game.moves));
    total_moves += static_cast<std::uint64_t>(game.moves);
    total_clears += game.numbered_cleared;
    total_reveals += game.covered_revealed;
    total_chain += game.chain_depth_sum;
    total_work += game.work;
    result.maximum_chain_depth =
        std::max(result.maximum_chain_depth, game.maximum_chain_depth);
    result.censored += game.censored ? 1 : 0;
  }
  result.mean_score = arithmeticMean(scores);
  result.median_score = empiricalQuantile(scores, 0.5);
  result.lower_quartile_score = empiricalQuantile(scores, 0.25);
  result.minimum_score = *std::min_element(scores.begin(), scores.end());
  result.mean_moves = arithmeticMean(moves);
  result.lower_quartile_moves = empiricalQuantile(moves, 0.25);
  result.mean_work = static_cast<double>(total_work) / games.size();
  if (total_moves > 0) {
    result.numbered_clears_per_move =
        static_cast<double>(total_clears) / total_moves;
    result.covered_reveals_per_move =
        static_cast<double>(total_reveals) / total_moves;
    result.mean_chain_depth = static_cast<double>(total_chain) / total_moves;
  }
  result.bootstrap_lower95 = bootstrapLower95(scores);
  result.student_t_lower95 = studentLower95(scores);
  return result;
}

std::vector<double> pairedValues(
    const std::vector<PolicyGameResult>& candidate,
    const std::vector<PolicyGameResult>& comparator, bool score) {
  if (candidate.size() != comparator.size()) {
    throw std::invalid_argument("paired cohorts differ in size");
  }
  std::vector<double> result(candidate.size());
  for (std::size_t index = 0; index < candidate.size(); ++index) {
    if (candidate[index].seed != comparator[index].seed) {
      throw std::invalid_argument("paired cohorts differ in seed order");
    }
    result[index] = score
        ? static_cast<double>(candidate[index].score - comparator[index].score)
        : static_cast<double>(candidate[index].moves - comparator[index].moves);
  }
  return result;
}

double halfMean(const std::vector<PolicyGameResult>& games,
                std::size_t begin, bool score) {
  double total = 0.0;
  for (std::size_t index = begin; index < begin + 32; ++index) {
    total += score ? static_cast<double>(games[index].score)
                   : static_cast<double>(games[index].moves);
  }
  return total / 32.0;
}

void writeSummaryJson(std::ostream& output, const EvaluationSummary& summary) {
  output << "{\"meanScore\":" << summary.mean_score
         << ",\"medianScore\":" << summary.median_score
         << ",\"lowerQuartileScore\":" << summary.lower_quartile_score
         << ",\"minimumScore\":" << summary.minimum_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"lowerQuartileMoves\":" << summary.lower_quartile_moves
         << ",\"meanWorkPerGame\":" << summary.mean_work
         << ",\"numberedClearsPerMove\":"
         << summary.numbered_clears_per_move
         << ",\"coveredRevealsPerMove\":"
         << summary.covered_reveals_per_move
         << ",\"meanChainDepthPerMove\":" << summary.mean_chain_depth
         << ",\"maximumChainDepth\":" << summary.maximum_chain_depth
         << ",\"censored\":" << summary.censored
         << ",\"bootstrapOneSidedLower95\":"
         << summary.bootstrap_lower95
         << ",\"studentTOneSidedLower95\":"
         << summary.student_t_lower95 << '}';
}

void writeGamesJson(std::ostream& output,
                    const std::vector<PolicyGameResult>& games) {
  output << '[';
  for (std::size_t index = 0; index < games.size(); ++index) {
    if (index != 0) output << ',';
    const PolicyGameResult& game = games[index];
    output << "{\"seed\":\"0x" << std::hex << std::setw(8)
           << std::setfill('0') << game.seed << std::dec << std::setfill(' ')
           << "\",\"score\":" << game.score << ",\"moves\":" << game.moves
           << ",\"censored\":" << (game.censored ? "true" : "false")
           << ",\"numberedCleared\":" << game.numbered_cleared
           << ",\"coveredRevealed\":" << game.covered_revealed
           << ",\"chainDepthSum\":" << game.chain_depth_sum
           << ",\"maximumChainDepth\":" << game.maximum_chain_depth
           << ",\"work\":" << game.work << ",\"checksum\":\""
           << game.checksum << "\"}";
  }
  output << ']';
}

std::string compilerIdentity() {
#if defined(__clang__)
  return std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
  return std::string("gcc ") + __VERSION__;
#else
  return "unknown-cxx20-compiler";
#endif
}

bool passesStageAAbsoluteGate(double mean_score, double mean_moves) {
  return mean_score >= 300'000.0 && mean_moves >= 90.0;
}

bool passesOrderedHalfGate(double direct_score, double direct_moves,
                           double search_score, double search_moves) {
  return search_score > direct_score && search_moves > direct_moves;
}

bool requiresCorrectedD4Comparison(std::uint64_t transitions) {
  return transitions == kFrozenTrainingTransitions;
}

bool passesCorrectedD4Gate(double search_score, double search_moves,
                           double d4_score, double d4_moves,
                           double paired_score_lower95,
                           double paired_moves_lower95) {
  return search_score >= d4_score && search_moves >= d4_moves &&
         paired_score_lower95 >= 0.0 && paired_moves_lower95 >= 0.0;
}

bool passesFinalDevelopmentGate(double mean_score,
                                double bootstrap_lower95) {
  return mean_score > 1'050'000.0 && bootstrap_lower95 > 1'000'000.0;
}

bool eligibleForFutureDevelopment(std::uint64_t transitions,
                                  bool absolute_gate, bool half_gate,
                                  bool corrected_d4_gate) {
  return requiresCorrectedD4Comparison(transitions) && absolute_gate &&
         half_gate && corrected_d4_gate;
}

struct QualificationArtifact {
  std::string training_source_sha256;
  std::string engine_sha256;
  std::string corrected_d4_sha256;
  std::string corrected_d4_leaf_sha256;
  std::string cfpi_behavior_sha256;
  std::string training_lane_manifest_sha256;
  std::string checkpoint_sha256;
  std::string model_sha256;
  std::string ordered_results_sha256;
  std::uint64_t training_transitions = 0;
  std::uint32_t seed_start = 0;
  std::uint64_t games = 0;
  double search_mean_score = 0.0;
  double search_mean_moves = 0.0;
  std::array<double, 2> direct_half_scores{};
  std::array<double, 2> direct_half_moves{};
  std::array<double, 2> search_half_scores{};
  std::array<double, 2> search_half_moves{};
  double corrected_d4_mean_score = 0.0;
  double corrected_d4_mean_moves = 0.0;
  double paired_d4_score_lower95 = 0.0;
  double paired_d4_moves_lower95 = 0.0;
};

void writeQualificationArtifact(const std::filesystem::path& path,
                                const QualificationArtifact& artifact) {
  if (path.empty()) throw std::invalid_argument("empty qualification path");
  const std::array<std::string_view, 9> hashes{{
      artifact.training_source_sha256,
      artifact.engine_sha256,
      artifact.corrected_d4_sha256,
      artifact.corrected_d4_leaf_sha256,
      artifact.cfpi_behavior_sha256,
      artifact.training_lane_manifest_sha256,
      artifact.checkpoint_sha256,
      artifact.model_sha256,
      artifact.ordered_results_sha256,
  }};
  if (std::any_of(hashes.begin(), hashes.end(),
                  [](std::string_view value) {
                    return !validSha256Hex(value);
                  })) {
    throw std::invalid_argument("invalid qualification provenance hash");
  }
  const std::filesystem::path temporary = path.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      throw std::runtime_error("could not open qualification temp file");
    }
    constexpr std::array<char, 8> magic{
        {'D', '7', 'Q', 'U', 'A', 'L', '1', 0}};
    output.write(magic.data(), static_cast<std::streamsize>(magic.size()));
    writeScalar<std::uint32_t>(output, 1u);
    writeCheckpointHash(output, artifact.training_source_sha256);
    writeCheckpointHash(output, artifact.engine_sha256);
    writeCheckpointHash(output, artifact.corrected_d4_sha256);
    writeCheckpointHash(output, artifact.corrected_d4_leaf_sha256);
    writeCheckpointHash(output, artifact.cfpi_behavior_sha256);
    writeCheckpointHash(output, artifact.training_lane_manifest_sha256);
    writeCheckpointHash(output, artifact.checkpoint_sha256);
    writeCheckpointHash(output, artifact.model_sha256);
    writeCheckpointHash(output, artifact.ordered_results_sha256);
    writeScalar<std::uint64_t>(output, artifact.training_transitions);
    writeScalar<std::uint32_t>(output, artifact.seed_start);
    writeScalar<std::uint64_t>(output, artifact.games);
    writeScalar<double>(output, artifact.search_mean_score);
    writeScalar<double>(output, artifact.search_mean_moves);
    for (double value : artifact.direct_half_scores) {
      writeScalar<double>(output, value);
    }
    for (double value : artifact.direct_half_moves) {
      writeScalar<double>(output, value);
    }
    for (double value : artifact.search_half_scores) {
      writeScalar<double>(output, value);
    }
    for (double value : artifact.search_half_moves) {
      writeScalar<double>(output, value);
    }
    writeScalar<double>(output, artifact.corrected_d4_mean_score);
    writeScalar<double>(output, artifact.corrected_d4_mean_moves);
    writeScalar<double>(output, artifact.paired_d4_score_lower95);
    writeScalar<double>(output, artifact.paired_d4_moves_lower95);
    output.flush();
    if (!output) throw std::runtime_error("failed writing qualification file");
  }
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(temporary);
    throw std::runtime_error("atomic qualification rename failed: " +
                             error.message());
  }
}

QualificationArtifact readQualificationArtifact(
    const std::filesystem::path& path) {
  const std::string bytes = readWholeFile(path);
  std::istringstream input(bytes, std::ios::in | std::ios::binary);
  std::array<char, 8> magic{};
  input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  constexpr std::array<char, 8> expected{
      {'D', '7', 'Q', 'U', 'A', 'L', '1', 0}};
  if (!input || magic != expected || readScalar<std::uint32_t>(input) != 1u) {
    throw std::runtime_error("invalid Stage-A qualification artifact");
  }
  QualificationArtifact result;
  result.training_source_sha256 = readCheckpointHash(input);
  result.engine_sha256 = readCheckpointHash(input);
  result.corrected_d4_sha256 = readCheckpointHash(input);
  result.corrected_d4_leaf_sha256 = readCheckpointHash(input);
  result.cfpi_behavior_sha256 = readCheckpointHash(input);
  result.training_lane_manifest_sha256 = readCheckpointHash(input);
  result.checkpoint_sha256 = readCheckpointHash(input);
  result.model_sha256 = readCheckpointHash(input);
  result.ordered_results_sha256 = readCheckpointHash(input);
  result.training_transitions = readScalar<std::uint64_t>(input);
  result.seed_start = readScalar<std::uint32_t>(input);
  result.games = readScalar<std::uint64_t>(input);
  result.search_mean_score = readScalar<double>(input);
  result.search_mean_moves = readScalar<double>(input);
  for (double& value : result.direct_half_scores) {
    value = readScalar<double>(input);
  }
  for (double& value : result.direct_half_moves) {
    value = readScalar<double>(input);
  }
  for (double& value : result.search_half_scores) {
    value = readScalar<double>(input);
  }
  for (double& value : result.search_half_moves) {
    value = readScalar<double>(input);
  }
  result.corrected_d4_mean_score = readScalar<double>(input);
  result.corrected_d4_mean_moves = readScalar<double>(input);
  result.paired_d4_score_lower95 = readScalar<double>(input);
  result.paired_d4_moves_lower95 = readScalar<double>(input);
  if (input.peek() != std::char_traits<char>::eof()) {
    throw std::runtime_error("trailing bytes in qualification artifact");
  }
  return result;
}

bool qualificationMatchesCheckpoint(
    const QualificationArtifact& artifact, const TrainingContract& contract,
    std::string_view checkpoint_sha256, std::string_view model_sha256) {
  const std::array<std::string_view, 11> hashes{{
      artifact.training_source_sha256,
      artifact.engine_sha256,
      artifact.corrected_d4_sha256,
      artifact.corrected_d4_leaf_sha256,
      artifact.cfpi_behavior_sha256,
      artifact.training_lane_manifest_sha256,
      artifact.checkpoint_sha256,
      artifact.model_sha256,
      artifact.ordered_results_sha256,
      checkpoint_sha256,
      model_sha256,
  }};
  if (std::any_of(hashes.begin(), hashes.end(),
                  [](std::string_view value) {
                    return !validSha256Hex(value);
                  })) {
    return false;
  }
  const std::array<double, 14> numeric{{
      artifact.search_mean_score,
      artifact.search_mean_moves,
      artifact.direct_half_scores[0],
      artifact.direct_half_scores[1],
      artifact.direct_half_moves[0],
      artifact.direct_half_moves[1],
      artifact.search_half_scores[0],
      artifact.search_half_scores[1],
      artifact.search_half_moves[0],
      artifact.search_half_moves[1],
      artifact.corrected_d4_mean_score,
      artifact.corrected_d4_mean_moves,
      artifact.paired_d4_score_lower95,
      artifact.paired_d4_moves_lower95,
  }};
  if (std::any_of(numeric.begin(), numeric.end(),
                  [](double value) { return !std::isfinite(value); })) {
    return false;
  }
  bool halves_pass = true;
  for (std::size_t half = 0; half < 2; ++half) {
    halves_pass = halves_pass &&
        passesOrderedHalfGate(
            artifact.direct_half_scores[half],
            artifact.direct_half_moves[half],
            artifact.search_half_scores[half],
            artifact.search_half_moves[half]);
  }
  return artifact.training_transitions == kFrozenTrainingTransitions &&
         artifact.seed_start == kBurnedStageASeedBegin &&
         artifact.games == 64 &&
         artifact.training_source_sha256 == contract.source_sha256 &&
         artifact.engine_sha256 == contract.engine_sha256 &&
         artifact.corrected_d4_sha256 == contract.corrected_d4_sha256 &&
         artifact.corrected_d4_leaf_sha256 ==
             contract.corrected_d4_leaf_sha256 &&
         artifact.cfpi_behavior_sha256 == contract.cfpi_behavior_sha256 &&
         artifact.training_lane_manifest_sha256 ==
             contract.training_lane_manifest_sha256 &&
         artifact.checkpoint_sha256 == checkpoint_sha256 &&
         artifact.model_sha256 == model_sha256 &&
         artifact.corrected_d4_mean_score == 176'925.25 &&
         artifact.corrected_d4_mean_moves == 116.375 &&
         passesStageAAbsoluteGate(artifact.search_mean_score,
                                  artifact.search_mean_moves) &&
         halves_pass &&
         passesCorrectedD4Gate(
             artifact.search_mean_score, artifact.search_mean_moves,
             artifact.corrected_d4_mean_score,
             artifact.corrected_d4_mean_moves,
             artifact.paired_d4_score_lower95,
             artifact.paired_d4_moves_lower95);
}

bool developmentAdmission(bool compiled_lane_open,
                          bool lane_manifest_verified,
                          bool qualification_token_verified,
                          bool qualification_matches_checkpoint) {
  return compiled_lane_open && lane_manifest_verified &&
         qualification_token_verified && qualification_matches_checkpoint;
}

bool evaluationCheckpointEligible(const Progress& progress,
                                  const DelayedForwardTrace& trace,
                                  EvaluationStage stage) {
  if (progress.transitions > kFrozenTrainingTransitions) return false;
  if (stage == EvaluationStage::kBurnedStageA) {
    if (progress.transitions == kBurnedStageATransitions) return true;
    if (progress.transitions != kFrozenTrainingTransitions) return false;
  } else if (progress.transitions != kFrozenTrainingTransitions) {
    return false;
  }
  return progress.training_finalized && !progress.game_active && trace.empty();
}

int evaluate(const EvaluationOptions& options, std::ostream& output) {
  if (!options.stage_set || options.maximum_moves != kFrozenMaximumMoves ||
      options.reveal_samples != kFrozenRevealSamples ||
      options.event_boundaries != kFrozenEventBoundaries ||
      options.internal_action_width != kFrozenInternalActionWidth ||
      options.maximum_work != kFrozenMaximumSearchWork ||
      !options.seed_start_set || options.checkpoint.empty() ||
      options.source_directory.empty()) {
    throw std::invalid_argument("evaluation differs from frozen configuration");
  }
  const bool stage_a = options.stage == EvaluationStage::kBurnedStageA;
  if ((stage_a &&
       (options.qualification_output.empty() ||
        !options.qualification.empty() || !options.lane_manifest.empty())) ||
      (!stage_a &&
       (!options.qualification_output.empty() ||
        options.qualification.empty() || options.lane_manifest.empty()))) {
    throw std::invalid_argument("evaluation authority options differ by stage");
  }
  if (stage_a) {
    const std::filesystem::path source_directory(options.source_directory);
    const std::array<std::filesystem::path, 6> protected_paths{{
        std::filesystem::path(options.checkpoint),
        source_directory / "approaches/ntuple-rl/optimistic-phase/optimistic-phase-ntuple.cpp",
        source_directory / "src/core/native/engine.hpp",
        source_directory / "approaches/fair-expectimax/reference/fair-only-depth4.cpp",
        source_directory / "approaches/fair-expectimax/reference/fair-only-horizon.cpp",
        source_directory / "src/core/native/public-behavior.hpp",
    }};
    const std::filesystem::path qualification_output(
        options.qualification_output);
    const std::filesystem::path qualification_temporary =
        qualification_output.string() + ".tmp";
    for (const std::filesystem::path& protected_path : protected_paths) {
      if (sameFilesystemTarget(qualification_output, protected_path) ||
          sameFilesystemTarget(qualification_temporary, protected_path)) {
        throw std::invalid_argument(
            "qualification output aliases a protected input");
      }
    }
  }
  if ((stage_a &&
       (options.games != 64 || options.seed_start != kBurnedStageASeedBegin)) ||
      (!stage_a &&
       (options.games != 256 || options.seed_start != kDevelopmentSeedBegin))) {
    throw std::invalid_argument("evaluation cohort differs from stage contract");
  }
  const ProvenanceHashes provenance =
      verifyCompiledProvenance(options.source_directory);
  std::string development_lane_manifest_sha256;
  if (!stage_a) {
    development_lane_manifest_sha256 =
        verifyLaneManifest(options.lane_manifest, SeedUse::kDevelopment);
  }
  requireSeedRange(options.seed_start, options.games,
                   stage_a ? SeedUse::kBurnedStageA
                           : SeedUse::kDevelopment);
  Model model;
  DelayedForwardTrace trace;
  TrainingContract contract;
  const Progress progress =
      loadCheckpoint(options.checkpoint, model, trace, contract);
  if (!evaluationCheckpointEligible(progress, trace, options.stage)) {
    throw std::invalid_argument(
        "checkpoint is partial, pooled, unfinalized, or overtrained");
  }
  const std::string model_sha256 = model.parameterSha256();
  const std::string checkpoint_sha256 = fileSha256(options.checkpoint);
  std::string qualification_sha256;
  bool development_authorized = false;
  if (!stage_a) {
    qualification_sha256 = fileSha256(options.qualification);
    const QualificationArtifact qualification =
        readQualificationArtifact(options.qualification);
    development_authorized = developmentAdmission(
        laneOpen(SeedUse::kDevelopment),
        development_lane_manifest_sha256 ==
            kCompiledDevelopmentLaneManifestSha256,
        qualification_sha256 == kCompiledStageAQualificationSha256,
        qualificationMatchesCheckpoint(
            qualification, contract, checkpoint_sha256, model_sha256));
    if (!development_authorized) {
      throw std::runtime_error(
          "development capability does not authorize this checkpoint");
    }
  }

  const auto started = std::chrono::steady_clock::now();
  std::vector<PolicyGameResult> direct;
  std::vector<PolicyGameResult> search;
  direct.reserve(options.games);
  search.reserve(options.games);
  for (std::uint64_t game = 0; game < options.games; ++game) {
    const std::uint32_t seed =
        options.seed_start + static_cast<std::uint32_t>(game);
    direct.push_back(
        runPolicyGame(model, seed, PolicyKind::kDirect, options));
    search.push_back(
        runPolicyGame(model, seed, PolicyKind::kBoundarySearch, options));
  }
  const EvaluationSummary direct_summary = summarizeGames(direct);
  const EvaluationSummary search_summary = summarizeGames(search);

  bool stage_a_absolute_gate = false;
  bool stage_a_half_gate = false;
  bool d4_gate = false;
  std::array<double, 2> direct_half_scores{};
  std::array<double, 2> direct_half_moves{};
  std::array<double, 2> search_half_scores{};
  std::array<double, 2> search_half_moves{};
  double paired_d4_score_lower95 =
      -std::numeric_limits<double>::infinity();
  double paired_d4_moves_lower95 =
      -std::numeric_limits<double>::infinity();
  std::vector<PolicyGameResult> d4;
  std::optional<EvaluationSummary> d4_summary;
  if (stage_a) {
    stage_a_absolute_gate = passesStageAAbsoluteGate(
        search_summary.mean_score, search_summary.mean_moves);
    stage_a_half_gate = true;
    for (std::size_t half = 0; half < 2; ++half) {
      const std::size_t begin = half * 32;
      direct_half_scores[half] = halfMean(direct, begin, true);
      direct_half_moves[half] = halfMean(direct, begin, false);
      search_half_scores[half] = halfMean(search, begin, true);
      search_half_moves[half] = halfMean(search, begin, false);
      stage_a_half_gate =
          stage_a_half_gate &&
          passesOrderedHalfGate(direct_half_scores[half],
                                direct_half_moves[half],
                                search_half_scores[half],
                                search_half_moves[half]);
    }
    if (requiresCorrectedD4Comparison(progress.transitions)) {
      d4.reserve(options.games);
      for (std::uint64_t game = 0; game < options.games; ++game) {
        const std::uint32_t seed =
            options.seed_start + static_cast<std::uint32_t>(game);
        d4.push_back(runCorrectedD4Game(seed));
      }
      d4_summary = summarizeGames(d4);
      if (d4_summary->censored != 0) {
        throw std::runtime_error(
            "corrected fair-D4 comparator was censored at its locked horizon");
      }
      if (std::abs(d4_summary->mean_score - 176'925.25) > 1.0e-6 ||
          std::abs(d4_summary->mean_moves - 116.375) > 1.0e-6) {
        throw std::runtime_error(
            "corrected fair-D4 burned reference no longer matches ledger");
      }
      const std::vector<double> paired_scores =
          pairedValues(search, d4, true);
      const std::vector<double> paired_moves =
          pairedValues(search, d4, false);
      paired_d4_score_lower95 = studentLower95(paired_scores);
      paired_d4_moves_lower95 = studentLower95(paired_moves);
      d4_gate = passesCorrectedD4Gate(
          search_summary.mean_score, search_summary.mean_moves,
          d4_summary->mean_score, d4_summary->mean_moves,
          paired_d4_score_lower95, paired_d4_moves_lower95);
    }
  }
  const bool final_development_gate =
      !stage_a && passesFinalDevelopmentGate(
                      search_summary.mean_score,
                      search_summary.bootstrap_lower95);

  std::ostringstream ordered_results;
  for (std::size_t index = 0; index < search.size(); ++index) {
    ordered_results << direct[index].checksum << search[index].checksum;
    if (!d4.empty()) ordered_results << d4[index].checksum;
  }
  const std::string ordered_results_sha256 =
      sha256(ordered_results.str());
  bool qualification_artifact_written = false;
  if (stage_a &&
      eligibleForFutureDevelopment(
          progress.transitions, stage_a_absolute_gate,
          stage_a_half_gate, d4_gate)) {
    if (!d4_summary.has_value()) {
      throw std::logic_error("qualified Stage A lacks corrected-D4 results");
    }
    QualificationArtifact qualification;
    qualification.training_source_sha256 = contract.source_sha256;
    qualification.engine_sha256 = contract.engine_sha256;
    qualification.corrected_d4_sha256 = contract.corrected_d4_sha256;
    qualification.corrected_d4_leaf_sha256 =
        contract.corrected_d4_leaf_sha256;
    qualification.cfpi_behavior_sha256 = contract.cfpi_behavior_sha256;
    qualification.training_lane_manifest_sha256 =
        contract.training_lane_manifest_sha256;
    qualification.checkpoint_sha256 = checkpoint_sha256;
    qualification.model_sha256 = model_sha256;
    qualification.ordered_results_sha256 = ordered_results_sha256;
    qualification.training_transitions = progress.transitions;
    qualification.seed_start = options.seed_start;
    qualification.games = options.games;
    qualification.search_mean_score = search_summary.mean_score;
    qualification.search_mean_moves = search_summary.mean_moves;
    qualification.direct_half_scores = direct_half_scores;
    qualification.direct_half_moves = direct_half_moves;
    qualification.search_half_scores = search_half_scores;
    qualification.search_half_moves = search_half_moves;
    qualification.corrected_d4_mean_score = d4_summary->mean_score;
    qualification.corrected_d4_mean_moves = d4_summary->mean_moves;
    qualification.paired_d4_score_lower95 = paired_d4_score_lower95;
    qualification.paired_d4_moves_lower95 = paired_d4_moves_lower95;
    writeQualificationArtifact(options.qualification_output, qualification);
    qualification_sha256 = fileSha256(options.qualification_output);
    qualification_artifact_written = true;
  }
  std::ostringstream config;
  config << std::setprecision(17);
  config << "format=opnt-evaluation-v3"
         << "|stage=" << static_cast<int>(options.stage)
         << "|games=" << options.games
         << "|seedStart=" << options.seed_start
         << "|maximumMoves=" << options.maximum_moves
         << "|revealSamples=" << options.reveal_samples
         << "|eventBoundaries=" << options.event_boundaries
         << "|internalActionWidth=" << options.internal_action_width
         << "|maximumWork=" << options.maximum_work
         << "|trainingTransitions=" << progress.transitions
         << "|traceLambda=" << kLambda
         << "|traceDeltas=" << kTraceHorizon
         << "|scoreScale=" << kScoreScale
         << "|bootstrapReplicates=100000"
         << "|bootstrapSeed=3618637178"
         << "|studentCritical=" << studentCritical(options.games)
         << "|stageAMinScore=300000|stageAMinMoves=90"
         << "|developmentMinMeanExclusive=1050000"
         << "|developmentMinBootstrapExclusive=1000000"
         << "|compiler=" << compilerIdentity()
         << "|source=" << provenance.source
         << "|engine=" << provenance.engine
         << "|d4=" << provenance.corrected_d4
         << "|d4Leaf=" << provenance.corrected_d4_leaf
         << "|cfpiBehavior=" << provenance.cfpi_behavior
         << "|trainingSource=" << contract.source_sha256
         << "|trainingEngine=" << contract.engine_sha256
         << "|trainingD4=" << contract.corrected_d4_sha256
         << "|trainingD4Leaf=" << contract.corrected_d4_leaf_sha256
         << "|trainingCfpiBehavior=" << contract.cfpi_behavior_sha256
         << "|trainingLaneManifest="
         << contract.training_lane_manifest_sha256
         << "|developmentLaneManifest="
         << development_lane_manifest_sha256
         << "|stageAQualification=" << qualification_sha256
         << "|model=" << model_sha256
         << "|checkpoint=" << checkpoint_sha256;
  const double seconds = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - started)
                             .count();
  output << std::fixed << std::setprecision(6)
         << "OPTIMISTIC_PHASE_NTUPLE_EVALUATION {\"evaluationStage\":\""
         << (stage_a ? "burned-stage-a" : "final-development")
         << "\",\"games\":" << options.games
         << ",\"seedStart\":\"0x" << std::hex << std::setw(8)
         << std::setfill('0') << options.seed_start << std::dec
         << std::setfill(' ') << "\""
         << ",\"trainingTransitions\":" << progress.transitions
         << ",\"trainingFinalized\":"
         << (progress.training_finalized ? "true" : "false")
         << ",\"configuration\":{\"maximumMoves\":"
         << options.maximum_moves << ",\"revealSamples\":"
         << options.reveal_samples << ",\"eventBoundaries\":"
         << options.event_boundaries << ",\"internalActionWidth\":"
         << options.internal_action_width << ",\"maximumWork\":"
         << options.maximum_work
         << ",\"bootstrapReplicates\":100000"
         << ",\"bootstrapSeed\":\"0xd7b0057a\",\"compiler\":\""
         << compilerIdentity()
         << "\"},\"hashes\":{\"configurationSha256\":\""
         << sha256(config.str()) << "\",\"sourceSha256\":\""
         << provenance.source << "\",\"engineSha256\":\""
         << provenance.engine
         << "\",\"correctedD4SourceSha256\":\""
         << provenance.corrected_d4
         << "\",\"correctedD4LeafSourceSha256\":\""
         << provenance.corrected_d4_leaf
         << "\",\"cfpiBehaviorSourceSha256\":\""
         << provenance.cfpi_behavior
         << "\",\"trainingSourceSha256\":\""
         << contract.source_sha256
         << "\",\"trainingEngineSha256\":\""
         << contract.engine_sha256
         << "\",\"trainingCorrectedD4SourceSha256\":\""
         << contract.corrected_d4_sha256
         << "\",\"trainingCorrectedD4LeafSourceSha256\":\""
         << contract.corrected_d4_leaf_sha256
         << "\",\"trainingCfpiBehaviorSourceSha256\":\""
         << contract.cfpi_behavior_sha256
         << "\",\"trainingLaneManifestSha256\":\""
         << contract.training_lane_manifest_sha256
         << "\",\"developmentLaneManifestSha256\":\""
         << development_lane_manifest_sha256
         << "\",\"stageAQualificationSha256\":\""
         << qualification_sha256
         << "\",\"modelParametersSha256\":\"" << model_sha256
         << "\",\"checkpointSha256\":\"" << checkpoint_sha256
         << "\",\"orderedResultsSha256\":\""
         << ordered_results_sha256 << "\"},\"directSummary\":";
  writeSummaryJson(output, direct_summary);
  output << ",\"searchSummary\":";
  writeSummaryJson(output, search_summary);
  if (d4_summary.has_value()) {
    output << ",\"correctedD4Summary\":";
    writeSummaryJson(output, *d4_summary);
  }
  if (stage_a) {
    output << ",\"orderedHalves\":[";
    for (std::size_t half = 0; half < 2; ++half) {
      if (half != 0) output << ',';
      output << "{\"index\":" << half
             << ",\"directMeanScore\":" << direct_half_scores[half]
             << ",\"directMeanMoves\":" << direct_half_moves[half]
             << ",\"searchMeanScore\":" << search_half_scores[half]
             << ",\"searchMeanMoves\":" << search_half_moves[half]
             << ",\"scoreImproved\":"
             << (search_half_scores[half] > direct_half_scores[half]
                     ? "true"
                     : "false")
             << ",\"movesImproved\":"
             << (search_half_moves[half] > direct_half_moves[half]
                     ? "true"
                     : "false")
             << '}';
    }
    output << ']';
  }
  output << ",\"gates\":{\"stageAAbsolute\":"
         << (stage_a_absolute_gate ? "true" : "false")
         << ",\"stageAHalfComparisons\":"
         << (stage_a_half_gate ? "true" : "false")
         << ",\"correctedD4Noninferiority\":"
         << (d4_gate ? "true" : "false")
         << ",\"burnedStageAPassed\":"
         << ((stage_a_absolute_gate && stage_a_half_gate) ? "true" : "false")
         << ",\"eligibleForFuturePreregisteredDevelopment\":"
         << (eligibleForFutureDevelopment(
                 progress.transitions, stage_a_absolute_gate,
                 stage_a_half_gate, d4_gate)
                 ? "true"
                 : "false")
         << ",\"qualificationArtifactWritten\":"
         << (qualification_artifact_written ? "true" : "false")
         << ",\"developmentAuthorityVerified\":"
         << (development_authorized ? "true" : "false")
         << ",\"pairedD4ScoreStudentTLower95\":";
  if (std::isfinite(paired_d4_score_lower95)) {
    output << paired_d4_score_lower95;
  } else {
    output << "null";
  }
  output << ",\"pairedD4MovesStudentTLower95\":";
  if (std::isfinite(paired_d4_moves_lower95)) {
    output << paired_d4_moves_lower95;
  } else {
    output << "null";
  }
  output
         << ",\"finalDevelopment\":"
         << (final_development_gate ? "true" : "false")
         << ",\"freshDevelopmentLaneOpened\":"
         << (development_authorized ? "true" : "false")
         << "},\"directGames\":";
  writeGamesJson(output, direct);
  output << ",\"searchGames\":";
  writeGamesJson(output, search);
  if (!d4.empty()) {
    output << ",\"correctedD4Games\":";
    writeGamesJson(output, d4);
  }
  output << ",\"illegalMoves\":0,\"runnerFailures\":0"
         << ",\"incompleteDecisions\":0,\"seconds\":" << seconds
         << ",\"peakResidentBytes\":" << peakResidentBytes() << "}\n";
  return 0;
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

template <typename Function>
bool throwsInvalid(Function function) {
  try {
    function();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

template <typename Function>
bool throwsRuntime(Function function) {
  try {
    function();
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

bool approximatelyEqual(float first, float second, float tolerance = 1e-4f) {
  return std::abs(first - second) <= tolerance;
}

State fixtureState() {
  State state;
  state.board.fill(kEmpty);
  for (int column = 0; column < kBoardSize; ++column) {
    state.board[indexOf(kBoardSize - 1, column)] = kSolid;
  }
  state.board[indexOf(kBoardSize - 2, 0)] = kCracked;
  state.board[indexOf(kBoardSize - 2, 4)] = kSolid;
  state.board[indexOf(kBoardSize - 3, 4)] = kCracked;
  state.next_disc = 3;
  state.moves_remaining = 3;
  state.game_over = false;
  return state;
}

bool selfTest(std::ostream& output) {
  State fixture = fixtureState();
  State metadata = fixture;
  metadata.score = 9'999'999;
  metadata.level = 777;
  metadata.moves_played = 888;
  State reflected = fixture;
  reflected.board = mirrorBoard(fixture.board);
  State alternate_disc = fixture;
  alternate_disc.next_disc = 7;

  Model gradient_model;
  expect(approximatelyEqual(gradient_model.value(fixture), kOptimisticValue,
                            2e-3f),
         "optimistic initialization did not sum to 60");
  expect(gradient_model.value(fixture) == gradient_model.value(metadata),
         "chance-state value used forbidden metadata");
  expect(gradient_model.value(fixture) ==
             gradient_model.value(alternate_disc),
         "chance-state value depended on the visible disc");
  expect(gradient_model.value(fixture) == gradient_model.value(reflected),
         "chance-state value violated reflection");
  const ActiveFeatures gradient =
      activeFeatures(valueState(fixture), ModelStage::kPooled);
  expect(gradient.count < kActiveOccurrences &&
             gradient.maximum_multiplicity > 1 &&
             gradient.squared_norm > kActiveOccurrences,
         "synthetic fixture did not exercise shared-feature collisions");
  const float before_gradient = gradient_model.value(fixture);
  const UpdateReport fixed = gradient_model.update(
      valueState(fixture), 2.0f, 0.25f, UpdateRule::kFixed);
  expect(approximatelyEqual(gradient_model.value(fixture) - before_gradient,
                            0.5f, 2e-3f) &&
             fixed.squared_norm == gradient.squared_norm,
         "collision-correct normalized gradient failed");

  Model promotion_model;
  (void)promotion_model.update(valueState(fixture), 1.25f, 0.1f,
                               UpdateRule::kFixed);
  std::array<float, kMovesPerLevel> before_promotion{};
  for (int phase = 1; phase <= kMovesPerLevel; ++phase) {
    State phased = fixture;
    phased.moves_remaining = phase;
    before_promotion[phase - 1] = promotion_model.value(phased);
  }
  promotion_model.promote();
  for (int phase = 1; phase <= kMovesPerLevel; ++phase) {
    State phased = fixture;
    phased.moves_remaining = phase;
    expect(approximatelyEqual(promotion_model.value(phased),
                              before_promotion[phase - 1], 2e-4f),
           "pooled-to-phase promotion changed a value");
  }
  State phase_three = fixture;
  phase_three.moves_remaining = 3;
  State phase_two = fixture;
  phase_two.moves_remaining = 2;
  const float phase_two_before = promotion_model.value(phase_two);
  (void)promotion_model.update(valueState(phase_three), 1.0f, 0.1f,
                               UpdateRule::kFixed);
  expect(promotion_model.value(phase_two) == phase_two_before,
         "phase-head update leaked into another phase");

  Model tc_model;
  tc_model.promote();
  tc_model.enableTemporalCoherence();
  const UpdateReport tc_positive = tc_model.update(
      valueState(fixture), 1.0f, 1.0f,
      UpdateRule::kTemporalCoherence);
  const UpdateReport tc_negative = tc_model.update(
      valueState(fixture), -1.0f, 1.0f,
      UpdateRule::kTemporalCoherence);
  const UpdateReport tc_after_reversal = tc_model.update(
      valueState(fixture), 1.0f, 1.0f,
      UpdateRule::kTemporalCoherence);
  expect(approximatelyEqual(tc_positive.mean_beta, 1.0f) &&
             approximatelyEqual(tc_negative.mean_beta, 1.0f) &&
             approximatelyEqual(tc_after_reversal.mean_beta, 0.0f) &&
             approximatelyEqual(tc_after_reversal.prediction_before,
                                tc_after_reversal.prediction_after),
         "temporal-coherence prior-history ordering failed");
  tc_model = Model(0.0f);

  DelayedForwardTrace trace;
  Model trace_model;
  const float trace_prediction_before = trace_model.value(fixture);
  trace.observe(valueState(fixture), 1.0f, UpdateRule::kFixed, 0.1f,
                trace_model);
  trace.observe(valueState(fixture), 2.0f, UpdateRule::kFixed, 0.1f,
                trace_model);
  expect(trace.summary().state_updates == 0 && trace.size() == 2,
         "forward view updated before three deltas existed");
  trace.observe(valueState(fixture), 4.0f, UpdateRule::kFixed, 0.1f,
                trace_model);
  expect(trace.summary().state_updates == 1 &&
             trace.size() == static_cast<std::size_t>(kTraceHorizon - 1) &&
             approximatelyEqual(trace.summary().last_credit, 3.0f) &&
             approximatelyEqual(trace_model.value(fixture) -
                                    trace_prediction_before,
                                0.3f, 2e-3f),
         "three-delta forward TD(lambda) numeric credit failed");
  trace.flush(trace_model);
  expect(trace.summary().state_updates == kTraceHorizon && trace.empty(),
         "three-delta forward TD(lambda) flush failed");
  expect(tdTarget(2.0f, 5.0f, true) == 2.0f &&
             tdTarget(2.0f, 5.0f, false) == 7.0f,
         "terminal/truncation bootstrap distinction failed");

  expect(scheduleForTransition(0, gradient_model).rate == kLearningRate0 &&
             scheduleForTransition(kPromotionTransition, gradient_model)
                 .should_promote &&
             scheduleForTransition(kLearningRateDrop1, promotion_model).rate ==
                 kLearningRate1 &&
             scheduleForTransition(kLearningRateDrop2, promotion_model).rate ==
                 kLearningRate2 &&
             scheduleForTransition(kTemporalCoherenceTransition,
                                   promotion_model)
                 .should_enable_tc,
         "fixed-rate to delayed-TC schedule hooks failed");
  gradient_model = Model(0.0f);
  trace_model = Model(0.0f);

  const auto values = oneStepActionValues(
      promotion_model, fixture, 7, kPolicyRevealDomain, 0);
  const auto repeated = oneStepActionValues(
      promotion_model, fixture, 7, kPolicyRevealDomain, 0);
  const auto metadata_values = oneStepActionValues(
      promotion_model, metadata, 7, kPolicyRevealDomain, 0);
  const auto reflected_values = oneStepActionValues(
      promotion_model, reflected, 7, kPolicyRevealDomain, 0);
  expect(values == repeated && values == metadata_values,
         "public chance sampler was nondeterministic or metadata-dependent");
  for (int column = 0; column < kBoardSize; ++column) {
    const float left = values[column];
    const float right = reflected_values[kBoardSize - 1 - column];
    expect(std::isfinite(left) == std::isfinite(right) &&
               (!std::isfinite(left) || approximatelyEqual(left, right)),
           "chance-sampled action values violated reflection");
  }
  expect(greedyAction(promotion_model, fixture, 7, kPolicyRevealDomain, 0) ==
             kBoardSize - 1 -
                 greedyAction(promotion_model, reflected, 7,
                              kPolicyRevealDomain, 0),
         "greedy action violated reflection");

  State symmetric;
  constexpr std::array<std::string_view, kBoardSize> symmetric_rows{{
      "0000000", "0100010", "0600060", "9180819", "2273722",
      "7632367", "4783874",
  }};
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      symmetric.board[indexOf(row, column)] =
          static_cast<std::uint8_t>(symmetric_rows[row][column] - '0');
    }
  }
  symmetric.next_disc = 4;
  symmetric.moves_remaining = 3;
  expect(symmetric.board == mirrorBoard(symmetric.board),
         "symmetric chance fixture was not reflection-fixed");
  bool center_coordinates_distinct = false;
  for (int depth = 0; depth < 3; ++depth) {
    std::array<int, 7> visible_counts{};
    std::array<int, 7> coordinate_counts{};
    for (int stratum = 0; stratum < 7; ++stratum) {
      ChancePackRandom sequential(fixture, 0, kPolicyRevealDomain, 11,
                                  depth, stratum, 17);
      ChancePackRandom coordinate(fixture, 0, kPolicyRevealDomain, 11,
                                  depth, stratum, 17);
      ++visible_counts[sequential.nextDisc() - 1];
      ++coordinate_counts[coordinate.nextDiscFor(4, 2, 3) - 1];
      ChancePackRandom left(symmetric, 0, kSearchRevealDomain, 13, depth,
                            stratum);
      ChancePackRandom right(symmetric, 6, kSearchRevealDomain, 13, depth,
                             stratum);
      ChancePackRandom center(symmetric, 3, kSearchRevealDomain, 13, depth,
                              stratum);
      for (int row = 0; row < kBoardSize; ++row) {
        for (int column = 0; column < kBoardSize; ++column) {
          const int reverse = kBoardSize - 1 - column;
          expect(left.nextDiscFor(row, column, 2) ==
                     right.nextDiscFor(row, reverse, 2),
                 "side-action reveal coordinates did not reflect exactly");
          center_coordinates_distinct |=
              center.nextDiscFor(row, column, 2) !=
              center.nextDiscFor(row, reverse, 2);
        }
      }
    }
    expect(std::all_of(visible_counts.begin(), visible_counts.end(),
                       [](int count) { return count == 1; }) &&
               std::all_of(coordinate_counts.begin(), coordinate_counts.end(),
                           [](int count) { return count == 1; }),
           "chance pack did not enumerate every visible/reveal value once");
  }
  expect(center_coordinates_distinct,
         "center action collapsed mirrored reveal coordinates");
  for (int action = 0; action < kBoardSize; ++action) {
    std::array<int, 7> next_disc_counts{};
    for (int stratum = 0; stratum < 7; ++stratum) {
      ChancePackRandom random(fixture, action, kPolicyRevealDomain, 23, 1,
                              stratum);
      MoveResult move;
      expect(playMoveWithChance(fixture, action, random, move) &&
                 !move.state.game_over,
             "exact visible-disc transition fixture terminated");
      ++next_disc_counts[move.state.next_disc - 1];
    }
    expect(std::all_of(next_disc_counts.begin(), next_disc_counts.end(),
                       [](int count) { return count == 1; }),
           "full chance transition did not enumerate visible discs 1..7");
  }
  bool exercised_multiple_reveals = false;
  for (int stratum = 0; stratum < 7; ++stratum) {
    ChancePackRandom left(symmetric, 0, kSearchRevealDomain, 29, 2,
                          stratum);
    ChancePackRandom right(symmetric, 6, kSearchRevealDomain, 29, 2,
                           stratum);
    MoveResult left_move;
    MoveResult right_move;
    expect(playMoveWithChance(symmetric, 0, left, left_move) &&
               playMoveWithChance(symmetric, 6, right, right_move) &&
               left_move.state.board == mirrorBoard(right_move.state.board) &&
               left_move.state.next_disc == right_move.state.next_disc &&
               left_move.state.moves_remaining ==
                   right_move.state.moves_remaining &&
               left_move.state.game_over == right_move.state.game_over &&
               left_move.score_delta == right_move.score_delta &&
               left_move.waves.size() == right_move.waves.size(),
           "multi-reveal symmetric transition did not reflect exactly");
    int reveal_count = 0;
    for (const Wave& wave : left_move.waves) reveal_count += wave.revealed;
    exercised_multiple_reveals |= reveal_count > 1;
  }
  expect(exercised_multiple_reveals,
         "symmetric chance fixture did not exercise multiple reveals");
  for (int action = 0; action < kBoardSize; ++action) {
    for (int stratum = 0; stratum < 7; ++stratum) {
      ChancePackRandom direct_random(fixture, action, kSearchRevealDomain,
                                     31, 2, stratum);
      ChancePackRandom reverse_random(reflected, kBoardSize - 1 - action,
                                      kSearchRevealDomain, 31, 2, stratum);
      MoveResult direct_move;
      MoveResult reverse_move;
      expect(playMoveWithChance(fixture, action, direct_random, direct_move) &&
                 playMoveWithChance(reflected, kBoardSize - 1 - action,
                                    reverse_random, reverse_move) &&
                 direct_move.state.board ==
                     mirrorBoard(reverse_move.state.board) &&
                 direct_move.state.next_disc == reverse_move.state.next_disc &&
                 direct_move.score_delta == reverse_move.score_delta &&
                 direct_move.level_advanced == reverse_move.level_advanced,
             "asymmetric chance transition did not reflect exactly");
    }
  }
  const std::uint32_t policy_bits = chanceEventBits(
      fixture, kPolicyRevealDomain, 0, 1, 7);
  const std::uint32_t search_bits = chanceEventBits(
      fixture, kSearchRevealDomain, 0, 1, 7);
  expect(publicHash(fixture) == publicHash(metadata) &&
             policy_bits != search_bits,
         "coordinate/domain-safe chance separation failed");

  State boundary = fixture;
  // Two decisions are required before the covered-row rise.  This exercises
  // both the exact seven-way next-visible-disc branch and the event stop.
  boundary.moves_remaining = 2;
  SearchOptions search_options;
  search_options.maximum_boundaries = 1;
  search_options.reveal_samples = 7;
  search_options.internal_action_width = 2;
  search_options.maximum_work = 100'000;
  const SearchDecision search = chooseEventBoundaryAction(
      promotion_model, boundary, search_options);
  State boundary_reflected = boundary;
  boundary_reflected.board = mirrorBoard(boundary.board);
  const SearchDecision search_reflected = chooseEventBoundaryAction(
      promotion_model, boundary_reflected, search_options);
  State boundary_metadata = boundary;
  boundary_metadata.score = 1'234'567;
  boundary_metadata.level = 99;
  boundary_metadata.moves_played = 456;
  const SearchDecision search_metadata = chooseEventBoundaryAction(
      promotion_model, boundary_metadata, search_options);
  expect(search.completed_boundaries == 1 &&
             search.last_iteration_complete && search.full_root &&
             !search.used_direct_fallback &&
             search.work > kBoardSize &&
             search.work <= search_options.maximum_work &&
             isLegal(boundary.board, search.action),
         "event-boundary search did not complete exactly one rise boundary");
  expect(search_reflected.action ==
                 kBoardSize - 1 - search.action &&
             approximatelyEqual(search_reflected.value, search.value) &&
             search_reflected.work == search.work,
         "event-boundary search violated reflection");
  expect(search_metadata.action == search.action &&
             search_metadata.value == search.value &&
             search_metadata.work == search.work,
         "event-boundary search used forbidden metadata");
  for (int phase = 1; phase <= kMovesPerLevel; ++phase) {
    State phased = fixture;
    phased.moves_remaining = phase;
    const SearchDecision completed = chooseEventBoundaryAction(
        promotion_model, phased, search_options, 71);
    expect(completed.completed_boundaries == 1 &&
               completed.last_iteration_complete && completed.full_root &&
               !completed.used_direct_fallback &&
               completed.work <= search_options.maximum_work,
           "one-boundary bounded rollout failed a rise phase");
  }
  SearchOptions two_boundary_options = search_options;
  two_boundary_options.maximum_boundaries = 2;
  const SearchDecision two_boundary = chooseEventBoundaryAction(
      promotion_model, fixture, two_boundary_options, 73);
  expect(two_boundary.completed_boundaries == 2 &&
             two_boundary.last_iteration_complete &&
             two_boundary.full_root &&
             !two_boundary.used_direct_fallback &&
             two_boundary.work <= two_boundary_options.maximum_work,
         "two-boundary bounded rollout did not complete");
  for (int phase = 1; phase <= kMovesPerLevel; ++phase) {
    State phased = fixture;
    phased.moves_remaining = phase;
    const SearchDecision completed = chooseEventBoundaryAction(
        promotion_model, phased, two_boundary_options,
        80 + static_cast<std::uint64_t>(phase));
    expect(completed.completed_boundaries == 2 &&
               completed.last_iteration_complete && completed.full_root &&
               !completed.used_direct_fallback &&
               completed.work <= two_boundary_options.maximum_work,
           "two-boundary bounded rollout failed a rise phase");
  }
  SearchOptions capped_options = search_options;
  capped_options.maximum_work = 1;
  const SearchDecision capped = chooseEventBoundaryAction(
      promotion_model, boundary, capped_options);
  expect(capped.completed_boundaries == 0 && capped.used_direct_fallback &&
             capped.work <= capped_options.maximum_work &&
             isLegal(boundary.board, capped.action),
         "event-boundary work cap/fallback failed");
  SearchOptions wrong_width = search_options;
  wrong_width.internal_action_width = 1;
  expect(throwsInvalid([&] {
           (void)chooseEventBoundaryAction(promotion_model, boundary,
                                           wrong_width);
         }),
         "frozen search accepted a non-two internal action width");

  Progress checkpoint_progress;
  checkpoint_progress.transitions = kPromotionTransition;
  checkpoint_progress.completed_games = 123;
  checkpoint_progress.training_seed_start = kTrainingSeedBegin;
  DelayedForwardTrace checkpoint_trace;
  TrainingContract checkpoint_contract;
  const std::filesystem::path checkpoint_path =
      std::filesystem::temp_directory_path() /
      ("optimistic-phase-ntuple-selftest-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()) +
       ".bin");
  saveCheckpoint(checkpoint_path.string(), promotion_model,
                 checkpoint_progress, checkpoint_trace, checkpoint_contract);
  Model loaded;
  DelayedForwardTrace loaded_trace;
  TrainingContract loaded_contract;
  const Progress loaded_progress = loadCheckpoint(
      checkpoint_path.string(), loaded, loaded_trace, loaded_contract);
  expect(loaded_progress.transitions == checkpoint_progress.transitions &&
             loaded_progress.completed_games ==
                 checkpoint_progress.completed_games &&
             loaded_contract == checkpoint_contract && loaded_trace.empty() &&
             loaded.promoted() && !loaded.temporalCoherenceEnabled() &&
             loaded.fingerprint() == promotion_model.fingerprint() &&
             !std::filesystem::exists(checkpoint_path.string() + ".tmp"),
         "atomic checkpoint round trip failed");

  {
    std::string trailing = readWholeFile(checkpoint_path);
    trailing.push_back('x');
    const std::uint64_t payload_bytes =
        trailing.size() - kCheckpointHeaderBytes;
    const std::uint64_t checksum = checksumBytes(std::string_view(
        trailing.data() + kCheckpointHeaderBytes,
        static_cast<std::size_t>(payload_bytes)));
    std::memcpy(trailing.data() + kCheckpointEnvelopeMagic.size() +
                    sizeof(std::uint32_t),
                &payload_bytes, sizeof(payload_bytes));
    std::memcpy(trailing.data() + kCheckpointEnvelopeMagic.size() +
                    sizeof(std::uint32_t) + sizeof(std::uint64_t),
                &checksum, sizeof(checksum));
    std::ofstream output_file(checkpoint_path,
                              std::ios::binary | std::ios::trunc);
    output_file.write(trailing.data(),
                      static_cast<std::streamsize>(trailing.size()));
  }
  expect(throwsRuntime([&] {
           Model rejected;
           DelayedForwardTrace rejected_trace;
           TrainingContract rejected_contract;
           (void)loadCheckpoint(checkpoint_path.string(), rejected,
                                rejected_trace, rejected_contract);
         }),
         "checkpoint accepted checksum-valid trailing payload bytes");
  saveCheckpoint(checkpoint_path.string(), promotion_model,
                 checkpoint_progress, checkpoint_trace, checkpoint_contract);
  {
    std::fstream corrupt(checkpoint_path,
                         std::ios::binary | std::ios::in | std::ios::out);
    corrupt.seekp(static_cast<std::streamoff>(kCheckpointHeaderBytes + 37));
    const char changed = static_cast<char>(0xa5);
    corrupt.write(&changed, 1);
  }
  expect(throwsRuntime([&] {
           Model rejected;
           DelayedForwardTrace rejected_trace;
           TrainingContract rejected_contract;
           (void)loadCheckpoint(checkpoint_path.string(), rejected,
                                rejected_trace, rejected_contract);
         }),
         "checkpoint accepted a checksum mismatch");
  {
    std::ostringstream valid(std::ios::out | std::ios::binary);
    promotion_model.writeCheckpoint(valid, checkpoint_progress,
                                    checkpoint_contract);
    std::string nonfinite = valid.str();
    const float not_finite = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(nonfinite.data() + nonfinite.size() - sizeof(float),
                &not_finite, sizeof(float));
    std::istringstream input(nonfinite,
                             std::ios::in | std::ios::binary);
    expect(throwsRuntime([&] {
             Model rejected;
             TrainingContract rejected_contract;
             (void)rejected.readCheckpoint(input, rejected_contract);
           }),
           "checkpoint model parser accepted a non-finite parameter");
  }
  {
    TrainingContract foreign_contract = checkpoint_contract;
    foreign_contract.source_sha256 = sha256("foreign training source");
    std::ostringstream foreign(std::ios::out | std::ios::binary);
    promotion_model.writeCheckpoint(foreign, checkpoint_progress,
                                    foreign_contract);
    std::istringstream input(foreign.str(),
                             std::ios::in | std::ios::binary);
    expect(throwsRuntime([&] {
             Model rejected;
             TrainingContract rejected_contract;
             (void)rejected.readCheckpoint(input, rejected_contract);
           }),
           "checkpoint accepted a different training implementation hash");
  }
  promotion_model = Model(0.0f);
  loaded = Model(0.0f);

  tc_model = Model();
  tc_model.promote();
  tc_model.enableTemporalCoherence();
  DelayedForwardTrace tc_checkpoint_trace;
  tc_checkpoint_trace.observe(valueState(fixture), 0.5f,
                              UpdateRule::kTemporalCoherence, 1.0f, tc_model);
  tc_checkpoint_trace.observe(valueState(phase_two), -0.25f,
                              UpdateRule::kTemporalCoherence, 1.0f, tc_model);
  tc_checkpoint_trace.observe(valueState(phase_three), 0.125f,
                              UpdateRule::kTemporalCoherence, 1.0f, tc_model);
  Progress tc_progress;
  tc_progress.transitions = kTemporalCoherenceTransition;
  tc_progress.completed_games = 17;
  tc_progress.training_seed_start = kTrainingSeedBegin;
  tc_progress.game_active = true;
  tc_progress.active_game_seed = 0;
  tc_progress.active_state = fixture;
  tc_progress.active_state.score = 123'456;
  tc_progress.active_state.level = 9;
  tc_progress.active_state.moves_played = 42;
  tc_progress.cumulative_score = 7'654'321;
  tc_progress.cumulative_moves = 9'876;
  tc_progress.censored_training_games = 3;
  saveCheckpoint(checkpoint_path.string(), tc_model, tc_progress,
                 tc_checkpoint_trace, checkpoint_contract);
  Model tc_loaded;
  DelayedForwardTrace tc_loaded_trace;
  TrainingContract tc_loaded_contract;
  const Progress tc_loaded_progress = loadCheckpoint(
      checkpoint_path.string(), tc_loaded, tc_loaded_trace,
      tc_loaded_contract);
  expect(tc_loaded_progress.game_active &&
             tc_loaded_progress.active_state.board == fixture.board &&
             tc_loaded_progress.active_state.score == 123'456 &&
             tc_loaded_progress.active_state.level == 9 &&
             tc_loaded_progress.active_state.moves_played == 42 &&
             tc_loaded_progress.cumulative_score == 7'654'321 &&
             tc_loaded_progress.cumulative_moves == 9'876 &&
             tc_loaded_progress.censored_training_games == 3 &&
             tc_loaded_trace.size() == 2 &&
             tc_loaded_trace.summary().state_updates == 1 &&
             tc_loaded_trace.summary().absolute_credit ==
                 tc_checkpoint_trace.summary().absolute_credit &&
             tc_loaded.fingerprint() == tc_model.fingerprint() &&
             tc_loaded_contract == checkpoint_contract,
         "temporal-coherence checkpoint did not preserve pending state");
  tc_model = Model(0.0f);
  tc_loaded = Model(0.0f);

  constexpr std::array<float, 7> synthetic_errors{{
      1.0f, -0.5f, 2.0f, 0.25f, -1.5f, 0.75f, 3.0f,
  }};
  const auto synthetic_step = [&](std::size_t index, Model& model,
                                  DelayedForwardTrace& forward) {
    State state = fixture;
    state.moves_remaining = 1 + static_cast<int>(index % kMovesPerLevel);
    forward.observe(valueState(state), synthetic_errors[index],
                    UpdateRule::kFixed, 0.01f, model);
  };
  Model uninterrupted_model;
  DelayedForwardTrace uninterrupted_trace;
  for (std::size_t index = 0; index < synthetic_errors.size(); ++index) {
    synthetic_step(index, uninterrupted_model, uninterrupted_trace);
  }
  uninterrupted_trace.flush(uninterrupted_model);

  Model interrupted_model;
  DelayedForwardTrace interrupted_trace;
  synthetic_step(0, interrupted_model, interrupted_trace);
  synthetic_step(1, interrupted_model, interrupted_trace);
  Progress interrupted_progress;
  interrupted_progress.transitions = 2;
  interrupted_progress.training_seed_start = kTrainingSeedBegin;
  interrupted_progress.game_active = true;
  interrupted_progress.active_state = fixture;
  saveCheckpoint(checkpoint_path.string(), interrupted_model,
                 interrupted_progress, interrupted_trace,
                 checkpoint_contract);
  Model resumed_model;
  DelayedForwardTrace resumed_trace;
  TrainingContract resumed_contract;
  Progress resumed_progress = loadCheckpoint(
      checkpoint_path.string(), resumed_model, resumed_trace,
      resumed_contract);
  for (std::size_t index = 2; index < synthetic_errors.size(); ++index) {
    synthetic_step(index, resumed_model, resumed_trace);
    ++resumed_progress.transitions;
  }
  resumed_trace.flush(resumed_model);
  Progress uninterrupted_progress = interrupted_progress;
  uninterrupted_progress.transitions = synthetic_errors.size();
  std::ostringstream uninterrupted_bytes(std::ios::out | std::ios::binary);
  uninterrupted_model.writeCheckpoint(uninterrupted_bytes,
                                      uninterrupted_progress,
                                      checkpoint_contract);
  uninterrupted_trace.writeCheckpoint(uninterrupted_bytes);
  std::ostringstream resumed_bytes(std::ios::out | std::ios::binary);
  resumed_model.writeCheckpoint(resumed_bytes, resumed_progress,
                                resumed_contract);
  resumed_trace.writeCheckpoint(resumed_bytes);
  expect(resumed_progress.transitions == synthetic_errors.size() &&
             resumed_progress.game_active &&
             resumed_trace.summary().state_updates ==
                 uninterrupted_trace.summary().state_updates &&
             resumed_trace.summary().absolute_credit ==
                 uninterrupted_trace.summary().absolute_credit &&
             resumed_model.fingerprint() == uninterrupted_model.fingerprint() &&
             resumed_bytes.str() == uninterrupted_bytes.str(),
         "transition checkpoint/resume differed from uninterrupted training");
  uninterrupted_model = Model(0.0f);
  interrupted_model = Model(0.0f);
  resumed_model = Model(0.0f);
  std::filesystem::remove(checkpoint_path);

  const bool training_lane_compiled =
      kTrainingSeedBegin < kTrainingSeedEnd &&
      validSha256Hex(kCompiledTrainingLaneManifestSha256);
  const bool development_lane_compiled =
      kDevelopmentSeedBegin < kDevelopmentSeedEnd &&
      validSha256Hex(kCompiledDevelopmentLaneManifestSha256) &&
      validSha256Hex(kCompiledStageAQualificationSha256);
  expect(laneOpen(SeedUse::kTraining) == training_lane_compiled &&
             laneOpen(SeedUse::kBurnedStageA) &&
             laneOpen(SeedUse::kDevelopment) ==
                 development_lane_compiled &&
             allowedSeed(kBurnedStageASeedBegin,
                         SeedUse::kBurnedStageA) &&
             allowedSeed(kBurnedStageASeedEnd - 1,
                         SeedUse::kBurnedStageA) &&
             !allowedSeed(kBurnedStageASeedEnd,
                          SeedUse::kBurnedStageA) &&
             !allowedSeed(0x4d00'0000u, SeedUse::kDevelopment) &&
             !allowedSeed(0x7d00'0000u, SeedUse::kTraining) &&
             !allowedSeed(0xd700'0000u, SeedUse::kDevelopment),
         "compile-time capability seed guards failed");

  Progress eligibility;
  DelayedForwardTrace eligibility_trace;
  eligibility.transitions = kBurnedStageATransitions - 1;
  expect(!evaluationCheckpointEligible(
             eligibility, eligibility_trace,
             EvaluationStage::kBurnedStageA),
         "Stage A accepted a partial checkpoint");
  eligibility.transitions = kBurnedStageATransitions;
  expect(evaluationCheckpointEligible(
             eligibility, eligibility_trace,
             EvaluationStage::kBurnedStageA) &&
             !evaluationCheckpointEligible(
                 eligibility, eligibility_trace,
                 EvaluationStage::kFinalDevelopment),
         "50m checkpoint was not isolated to burned Stage A");
  eligibility.transitions = kFrozenTrainingTransitions;
  eligibility.training_finalized = true;
  expect(evaluationCheckpointEligible(
             eligibility, eligibility_trace,
             EvaluationStage::kBurnedStageA) &&
             evaluationCheckpointEligible(
                 eligibility, eligibility_trace,
                 EvaluationStage::kFinalDevelopment),
         "finalized 100m checkpoint failed evaluation admission");
  eligibility.game_active = true;
  expect(!evaluationCheckpointEligible(
             eligibility, eligibility_trace,
             EvaluationStage::kFinalDevelopment),
         "development accepted an active-game checkpoint");
  eligibility.game_active = false;
  eligibility.transitions = kFrozenTrainingTransitions + 1;
  expect(!evaluationCheckpointEligible(
             eligibility, eligibility_trace,
             EvaluationStage::kBurnedStageA),
         "evaluation accepted an overtrained checkpoint");

  expect(passesStageAAbsoluteGate(300'000.0, 90.0) &&
             !passesStageAAbsoluteGate(299'999.0, 90.0) &&
             !passesStageAAbsoluteGate(300'000.0, 89.999) &&
             passesOrderedHalfGate(100.0, 10.0, 100.001, 10.001) &&
             !passesOrderedHalfGate(100.0, 10.0, 100.0, 11.0) &&
             !passesOrderedHalfGate(100.0, 10.0, 101.0, 10.0),
         "Stage-A absolute/ordered-half gate boundaries failed");
  expect(!requiresCorrectedD4Comparison(kBurnedStageATransitions) &&
             requiresCorrectedD4Comparison(kFrozenTrainingTransitions) &&
             passesCorrectedD4Gate(200.0, 100.0, 200.0, 100.0, 0.0,
                                   0.0) &&
             !passesCorrectedD4Gate(199.0, 100.0, 200.0, 100.0, 0.0,
                                    0.0) &&
             !passesCorrectedD4Gate(200.0, 99.0, 200.0, 100.0, 0.0,
                                    0.0) &&
             !passesCorrectedD4Gate(200.0, 100.0, 200.0, 100.0, -0.001,
                                    0.0) &&
             !passesCorrectedD4Gate(200.0, 100.0, 200.0, 100.0, 0.0,
                                    -0.001),
         "corrected-D4 gate boundaries failed");
  expect(passesFinalDevelopmentGate(1'050'000.001, 1'000'000.001) &&
             !passesFinalDevelopmentGate(1'050'000.0, 1'000'000.001) &&
             !passesFinalDevelopmentGate(1'050'000.001, 1'000'000.0),
         "final-development gate boundaries failed");
  for (int mask = 0; mask < 8; ++mask) {
    const bool absolute = (mask & 1) != 0;
    const bool halves = (mask & 2) != 0;
    const bool d4 = (mask & 4) != 0;
    expect(!eligibleForFutureDevelopment(
               kBurnedStageATransitions, absolute, halves, d4) &&
               eligibleForFutureDevelopment(
                   kFrozenTrainingTransitions, absolute, halves, d4) ==
                   (mask == 7),
           "future-development conjunction admitted the wrong gate mask");
  }

  TrainingContract qualified_contract;
  qualified_contract.source_sha256 = sha256("qualified source");
  qualified_contract.engine_sha256 = sha256("qualified engine");
  qualified_contract.corrected_d4_sha256 = sha256("qualified d4");
  qualified_contract.corrected_d4_leaf_sha256 = sha256("qualified d4 leaf");
  qualified_contract.cfpi_behavior_sha256 = sha256("qualified behavior");
  qualified_contract.training_lane_manifest_sha256 =
      sha256("qualified fit lane");
  const std::string qualified_checkpoint_sha256 =
      sha256("qualified checkpoint");
  const std::string qualified_model_sha256 = sha256("qualified model");
  QualificationArtifact qualified;
  qualified.training_source_sha256 = qualified_contract.source_sha256;
  qualified.engine_sha256 = qualified_contract.engine_sha256;
  qualified.corrected_d4_sha256 = qualified_contract.corrected_d4_sha256;
  qualified.corrected_d4_leaf_sha256 =
      qualified_contract.corrected_d4_leaf_sha256;
  qualified.cfpi_behavior_sha256 =
      qualified_contract.cfpi_behavior_sha256;
  qualified.training_lane_manifest_sha256 =
      qualified_contract.training_lane_manifest_sha256;
  qualified.checkpoint_sha256 = qualified_checkpoint_sha256;
  qualified.model_sha256 = qualified_model_sha256;
  qualified.ordered_results_sha256 = sha256("qualified ordered results");
  qualified.training_transitions = kFrozenTrainingTransitions;
  qualified.seed_start = kBurnedStageASeedBegin;
  qualified.games = 64;
  qualified.search_mean_score = 300'000.0;
  qualified.search_mean_moves = 120.0;
  qualified.direct_half_scores = {{100.0, 200.0}};
  qualified.direct_half_moves = {{10.0, 20.0}};
  qualified.search_half_scores = {{101.0, 201.0}};
  qualified.search_half_moves = {{11.0, 21.0}};
  qualified.corrected_d4_mean_score = 176'925.25;
  qualified.corrected_d4_mean_moves = 116.375;
  qualified.paired_d4_score_lower95 = 0.0;
  qualified.paired_d4_moves_lower95 = 0.0;
  const std::filesystem::path qualification_path =
      std::filesystem::temp_directory_path() /
      ("optimistic-phase-ntuple-qualification-selftest-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()) +
       ".bin");
  writeQualificationArtifact(qualification_path, qualified);
  const QualificationArtifact qualified_round_trip =
      readQualificationArtifact(qualification_path);
  const bool qualification_matches = qualificationMatchesCheckpoint(
      qualified_round_trip, qualified_contract,
      qualified_checkpoint_sha256, qualified_model_sha256);
  QualificationArtifact failed_qualification = qualified_round_trip;
  failed_qualification.paired_d4_score_lower95 = -0.001;
  expect(qualification_matches &&
             !developmentAdmission(false, true, true, true) &&
             !developmentAdmission(true, true, false, true) &&
             !developmentAdmission(true, true, true, false) &&
             developmentAdmission(true, true, true, true) &&
             !qualificationMatchesCheckpoint(
                 failed_qualification, qualified_contract,
                 qualified_checkpoint_sha256, qualified_model_sha256) &&
             !qualificationMatchesCheckpoint(
                 qualified_round_trip, qualified_contract,
                 sha256("arbitrary checkpoint"), qualified_model_sha256) &&
             !std::filesystem::exists(qualification_path.string() + ".tmp"),
         "staged qualification admitted an unqualified checkpoint");
  std::filesystem::remove(qualification_path);

  const std::string closed_training_manifest =
      canonicalLaneManifest(SeedUse::kTraining);
  const std::string closed_development_manifest =
      canonicalLaneManifest(SeedUse::kDevelopment);
  expect(closed_training_manifest.find("purpose=training") !=
             std::string::npos &&
             closed_development_manifest.find(
                 "purpose=final-development") != std::string::npos &&
             closed_training_manifest != closed_development_manifest,
         "canonical lane manifests did not bind their purpose/configuration");

  std::vector<PolicyGameResult> synthetic_games(64);
  std::vector<PolicyGameResult> synthetic_comparator(64);
  for (std::size_t index = 0; index < synthetic_games.size(); ++index) {
    PolicyGameResult game;
    game.seed = kBurnedStageASeedBegin + static_cast<std::uint32_t>(index);
    game.score = 1'000 + static_cast<std::int64_t>(index) * 10;
    game.moves = 20 + static_cast<int>(index % 8);
    game.censored = index == 63;
    game.numbered_cleared = static_cast<std::uint64_t>(game.moves * 2);
    game.covered_revealed = static_cast<std::uint64_t>(game.moves);
    game.chain_depth_sum = static_cast<std::uint64_t>(game.moves * 3);
    game.maximum_chain_depth = 5;
    game.work = index;
    game.checksum = sha256(std::to_string(index));
    synthetic_games[index] = game;
    synthetic_comparator[index] = game;
    synthetic_comparator[index].score -= 100;
    synthetic_comparator[index].moves -= 1;
  }
  const EvaluationSummary synthetic_summary =
      summarizeGames(synthetic_games);
  const std::vector<double> paired_scores =
      pairedValues(synthetic_games, synthetic_comparator, true);
  const std::vector<double> paired_moves =
      pairedValues(synthetic_games, synthetic_comparator, false);
  std::ostringstream synthetic_json;
  writeSummaryJson(synthetic_json, synthetic_summary);
  writeGamesJson(synthetic_json, synthetic_games);
  expect(synthetic_summary.mean_score == 1'315.0 &&
             synthetic_summary.median_score == 1'315.0 &&
             synthetic_summary.lower_quartile_score == 1'157.5 &&
             synthetic_summary.minimum_score == 1'000.0 &&
             synthetic_summary.censored == 1 &&
             synthetic_summary.numbered_clears_per_move == 2.0 &&
             synthetic_summary.covered_reveals_per_move == 1.0 &&
             synthetic_summary.mean_chain_depth == 3.0 &&
             synthetic_summary.maximum_chain_depth == 5 &&
             synthetic_summary.bootstrap_lower95 <
                 synthetic_summary.mean_score &&
             synthetic_summary.student_t_lower95 <
                 synthetic_summary.mean_score &&
             approximatelyEqual(
                 static_cast<float>(studentLower95(paired_scores)), 100.0f) &&
             approximatelyEqual(
                 static_cast<float>(studentLower95(paired_moves)), 1.0f) &&
             synthetic_json.str().find("bootstrapOneSidedLower95") !=
                 std::string::npos &&
             synthetic_json.str().find("checksum") != std::string::npos,
         "synthetic evaluation artifact/statistics failed");
  expect(sha256("abc") ==
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
         "SHA-256 implementation failed known vector");
  expect(validSha256Hex(sha256("abc")) &&
             !validSha256Hex(
                 "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD") &&
             !validSha256Hex("abc"),
         "compile-time provenance lock validation failed");

  output << std::fixed << std::setprecision(6)
         << "OPTIMISTIC_PHASE_NTUPLE_SELF_TEST {\"passed\":true"
         << ",\"publicChanceState\":true"
         << ",\"discIndependentValue\":true"
         << ",\"metadataBlind\":true"
         << ",\"reflectionExact\":true"
         << ",\"optimisticValue\":" << kOptimisticValue
         << ",\"scoreScale\":" << kScoreScale
         << ",\"lambda\":" << kLambda
         << ",\"traceHorizon\":" << kTraceHorizon
         << ",\"pooledEntries\":" << kPooledEntries
         << ",\"phaseEntries\":" << kPhaseEntries
         << ",\"activeOccurrences\":" << kActiveOccurrences
         << ",\"fixtureUniqueParameters\":" << gradient.count
         << ",\"fixtureSquaredNorm\":" << gradient.squared_norm
         << ",\"fixtureMaximumMultiplicity\":"
         << gradient.maximum_multiplicity
         << ",\"promotionParity\":true"
         << ",\"temporalCoherencePriorHistory\":true"
         << ",\"threeDeltaForwardView\":true"
         << ",\"terminalVsTruncationBootstrap\":true"
         << ",\"coordinateStratifiedChanceSampling\":true"
         << ",\"eventBoundaryBoundedRollout\":true"
         << ",\"eventBoundaryWork\":" << search.work
         << ",\"twoBoundaryWork\":" << two_boundary.work
         << ",\"atomicCheckpointRoundTrip\":true"
         << ",\"transitionResumeBitEqual\":true"
         << ",\"tcCheckpointRoundTrip\":true"
         << ",\"checkpointCorruptionRejected\":true"
         << ",\"trainingImplementationCheckpointBound\":true"
         << ",\"capabilityGuardedSeedLanes\":true"
         << ",\"burnedStageAOnly\":true"
         << ",\"evaluationArtifactStatistics\":true"
         << ",\"parameterBytesWithTc\":"
         << static_cast<std::uint64_t>(kPhaseEntries) * 3u * sizeof(float)
         << ",\"peakResidentBytes\":" << peakResidentBytes() << "}\n";
  return true;
}

}  // namespace drop7::optimistic_phase_ntuple

#ifndef DROP7_OPTIMISTIC_PHASE_NTUPLE_LIBRARY
int main(int argc, char** argv) {
  try {
    using namespace drop7::optimistic_phase_ntuple;
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return selfTest(std::cout) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--train") {
      return train(parseTrainingOptions(argc, argv, 2), std::cout);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--evaluate") {
      return evaluate(parseEvaluationOptions(argc, argv, 2), std::cout);
    }
    if (argc == 2 &&
        std::string_view(argv[1]) == "--print-training-lane-manifest") {
      std::cout << canonicalLaneManifest(SeedUse::kTraining);
      return 0;
    }
    if (argc == 2 &&
        std::string_view(argv[1]) == "--print-development-lane-manifest") {
      std::cout << canonicalLaneManifest(SeedUse::kDevelopment);
      return 0;
    }
    std::cerr
        << "usage: drop7_optimistic_phase_ntuple --self-test | "
           "--train OPTIONS | --evaluate OPTIONS | "
           "--print-training-lane-manifest | "
           "--print-development-lane-manifest\n"
        << "gameplay lanes require compile-time manifest capabilities\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
#endif
