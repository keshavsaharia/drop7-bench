#define DROP7_ORACLE_TOPOLOGY_RESIDUAL_LIBRARY
#include "oracle-topology-residual.cpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Replicates the fixed observable-topology residual in prediction-only mode.
// Nothing here retrains or selects the NNUE.  The reference heldout cohort is
// replayed solely to reconstruct pooled metrics and must match the reference
// artifact before the disjoint extension family is read.
namespace drop7::oracle_topology_residual_extension {

namespace base = drop7::oracle_topology_residual;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kOriginalHeldoutSeedStart = 0x3d9d'0000u;
constexpr std::uint32_t kExtensionSeedStart = 0x3d9d'0008u;
constexpr int kOriginalGames = 8;
constexpr int kExtensionGames = 8;
constexpr std::uint32_t kScreenSeedStart = 0x3ea9'0000u;
constexpr std::uint32_t kConfirmationSeedStart = 0x3eaa'0000u;
constexpr std::uint64_t kFrozenFingerprint = 0x0af6'ed6f'8889'5cfeull;
constexpr std::string_view kFrozenModelSha256 =
    "9b533353828773fa4a4df8bf5be80891b802ff4b3ea2057db0392ed4b5f8271a";
constexpr std::string_view kFrozenLabelsSha256 =
    "f61801abc9eefe86011f7202620a18c1277fcc1b5a24f4bce5947033b791dd89";
constexpr int kFrozenOriginalPairs = 85;
constexpr int kFrozenOriginalExamples = 170;
constexpr double kFrozenOriginalAuc = 0.6809688581;
constexpr double kFrozenOriginalPairAccuracy = 0.6705882353;
constexpr double kFrozenOriginalFirstHalf = 0.7027027027;
constexpr double kFrozenOriginalSecondHalf = 0.6458333333;
constexpr double kMetricTolerance = 1.0e-9;

static_assert(kOriginalHeldoutSeedStart == base::kHeldoutSeedStart);
static_assert(kOriginalGames == base::kHeldoutGames);
static_assert(kExtensionSeedStart == kOriginalHeldoutSeedStart + kOriginalGames);
static_assert(kExtensionSeedStart + kExtensionGames <= 0x3d9d'0010u);
static_assert(kScreenSeedStart == base::kScreenSeedStart);
static_assert(kConfirmationSeedStart == base::kConfirmationSeedStart);
static_assert(base::kTrainingMaximumMoves == 160);
static_assert(base::kGameplayMaximumMoves == 1'000);

std::string readFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not open frozen file " + path);
  std::ostringstream contents;
  contents << input.rdbuf();
  if (!input.good() && !input.eof()) {
    throw std::runtime_error("could not read frozen file " + path);
  }
  return contents.str();
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
      const std::uint32_t first = h + upper + choose +
                                  kSha256Constants[round] + words[round];
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
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const std::uint32_t value : hash) output << std::setw(8) << value;
  return output.str();
}

struct FrozenModel {
  base::NnueModel model{};
  std::string sha256;
  std::uint64_t fingerprint = 0;
};

FrozenModel loadFrozenModel(const std::string& path) {
  const std::string contents = readFile(path);
  FrozenModel result;
  result.sha256 = sha256(contents);
  if (result.sha256 != kFrozenModelSha256) {
    throw std::runtime_error("frozen model SHA-256 mismatch");
  }
  constexpr std::string_view marker = "\"parameters\":[";
  std::size_t cursor = contents.find(marker);
  if (cursor == std::string::npos) {
    throw std::runtime_error("frozen model has no parameter array");
  }
  cursor += marker.size();
  base::PackedParameters packed;
  for (double& parameter : packed.values) {
    while (cursor < contents.size() &&
           (contents[cursor] == ' ' || contents[cursor] == '\n' ||
            contents[cursor] == '\r' || contents[cursor] == '\t' ||
            contents[cursor] == ',')) {
      ++cursor;
    }
    if (cursor >= contents.size()) {
      throw std::runtime_error("truncated frozen parameter array");
    }
    char* end = nullptr;
    parameter = std::strtod(contents.c_str() + cursor, &end);
    if (end == contents.c_str() + cursor || !std::isfinite(parameter)) {
      throw std::runtime_error("invalid frozen model parameter");
    }
    cursor = static_cast<std::size_t>(end - contents.c_str());
  }
  while (cursor < contents.size() &&
         (contents[cursor] == ' ' || contents[cursor] == '\n' ||
          contents[cursor] == '\r' || contents[cursor] == '\t')) {
    ++cursor;
  }
  if (cursor >= contents.size() || contents[cursor] != ']') {
    throw std::runtime_error("frozen model parameter count mismatch");
  }
  result.model = base::unpack(packed);
  result.fingerprint = base::modelFingerprint(result.model);
  if (result.fingerprint != kFrozenFingerprint) {
    throw std::runtime_error("frozen model fingerprint mismatch");
  }
  return result;
}

base::MatchedDataset combine(const base::MatchedDataset& original,
                             const base::MatchedDataset& extension) {
  base::MatchedDataset result;
  result.raw_fair = original.raw_fair + extension.raw_fair;
  result.raw_oracle = original.raw_oracle + extension.raw_oracle;
  result.strata = original.strata + extension.strata;
  result.examples = original.examples;
  result.examples.insert(result.examples.end(), extension.examples.begin(),
                         extension.examples.end());
  result.pairs = original.pairs;
  result.pairs.insert(result.pairs.end(), extension.pairs.begin(),
                      extension.pairs.end());
  return result;
}

bool close(double left, double right) {
  return std::abs(left - right) <= kMetricTolerance;
}

bool originalReproduced(const base::PredictionMetrics& value) {
  return value.pairs == kFrozenOriginalPairs &&
         value.examples == kFrozenOriginalExamples &&
         close(value.auc, kFrozenOriginalAuc) &&
         close(value.pair_accuracy, kFrozenOriginalPairAccuracy) &&
         close(value.first_half_accuracy, kFrozenOriginalFirstHalf) &&
         close(value.second_half_accuracy, kFrozenOriginalSecondHalf);
}

bool extensionGate(const base::PredictionMetrics& value) {
  return value.auc >= base::kMinimumHeldoutAuc &&
         value.pair_accuracy >= base::kMinimumHeldoutPairAccuracy &&
         value.first_half_accuracy >= base::kMinimumHalfPairAccuracy &&
         value.second_half_accuracy >= base::kMinimumHalfPairAccuracy;
}

bool combinedGate(const base::PredictionMetrics& value) {
  return base::predictionGate(value);
}

int integerAfter(std::string_view line, std::string_view marker) {
  const std::size_t found = line.find(marker);
  if (found == std::string_view::npos) {
    throw std::runtime_error("missing behavior-label integer");
  }
  const char* begin = line.data() + found + marker.size();
  const char* end = line.data() + line.size();
  int result = 0;
  const auto parsed = std::from_chars(begin, end, result);
  if (parsed.ec != std::errc{}) {
    throw std::runtime_error("invalid behavior-label integer");
  }
  return result;
}

std::vector<State> loadTrainingDiagnostics(const std::string& path) {
  const std::string contents = readFile(path);
  if (sha256(contents) != kFrozenLabelsSha256) {
    throw std::runtime_error("frozen behavior-label SHA-256 mismatch");
  }
  std::vector<State> states;
  std::istringstream input(contents);
  std::string line;
  while (std::getline(input, line)) {
    if (line.find("\"split\":\"training\"") == std::string::npos) continue;
    constexpr std::string_view board_marker = "\"board\":\"";
    const std::size_t board_at = line.find(board_marker);
    if (board_at == std::string::npos ||
        board_at + board_marker.size() + kCellCount > line.size()) {
      throw std::runtime_error("invalid behavior-label board");
    }
    State state;
    for (int cell = 0; cell < kCellCount; ++cell) {
      const char encoded = line[board_at + board_marker.size() + cell];
      if (encoded < '0' || encoded > '9') {
        throw std::runtime_error("invalid behavior-label cell");
      }
      state.board[cell] = static_cast<std::uint8_t>(encoded - '0');
    }
    state.next_disc = static_cast<std::uint8_t>(
        integerAfter(line, "\"nextDisc\":"));
    state.moves_remaining = integerAfter(line, "\"movesRemaining\":");
    state.score = 0;
    state.level = 1;
    state.moves_played = 0;
    state.game_over = false;
    if (state.next_disc < 1 || state.next_disc > kBoardSize ||
        state.moves_remaining < 1 ||
        state.moves_remaining > kMovesPerLevel) {
      throw std::runtime_error("invalid behavior-label public state");
    }
    states.push_back(std::move(state));
  }
  if (states.size() != 1'508) {
    throw std::runtime_error("frozen training-label count mismatch");
  }
  std::vector<State> diagnostics;
  diagnostics.reserve(base::kDiagnosticStates);
  for (int index = 0; index < base::kDiagnosticStates; ++index) {
    const std::size_t selected = static_cast<std::size_t>(
        (static_cast<std::uint64_t>(2 * index + 1) * states.size()) /
        (2u * base::kDiagnosticStates));
    diagnostics.push_back(states[std::min(selected, states.size() - 1)]);
  }
  return diagnostics;
}

struct Options {
  std::string model = "/tmp/drop7-oracle-topology-residual-model.json";
  std::string labels = "/tmp/drop7-d4-public-root-labels.jsonl";
  std::string output =
      "/tmp/drop7-oracle-topology-residual-extension.json";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing extension option value");
    }
    const std::string flag = argv[index];
    if (flag == "--model") {
      result.model = argv[index + 1];
    } else if (flag == "--labels") {
      result.labels = argv[index + 1];
    } else if (flag == "--output") {
      result.output = argv[index + 1];
    } else {
      throw std::invalid_argument("unknown extension option " + flag);
    }
  }
  return result;
}

void writeReplication(std::ostream& output,
                      const base::MatchedDataset& dataset,
                      const base::PredictionMetrics& metrics) {
  output << "{\"dataset\":";
  base::writeDataset(output, dataset);
  output << ",\"prediction\":";
  base::writePrediction(output, metrics);
  output << '}';
}

void writeArtifact(
    const Options& options, const FrozenModel& frozen,
    const base::CollectedSplit& original_collection,
    const base::CollectedSplit& extension_collection,
    const base::MatchedDataset& original_data,
    const base::MatchedDataset& extension_data,
    const base::MatchedDataset& combined_data,
    const base::PredictionMetrics& original_prediction,
    const base::PredictionMetrics& extension_prediction,
    const base::PredictionMetrics& combined_prediction,
    bool original_reproduced, bool extension_passed, bool combined_passed,
    const base::PolicyDiagnostic& policy,
    const base::GameplayCohort* screen,
    const base::GameSummary* screen_baseline,
    const base::GameSummary* screen_candidate,
    const base::PairedGameplay* screen_paired, bool screen_passed,
    const base::GameplayCohort* confirmation,
    const base::GameSummary* confirmation_baseline,
    const base::GameSummary* confirmation_candidate,
    const base::PairedGameplay* confirmation_paired,
    bool confirmation_passed, double wall_seconds) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not write extension artifact");
  output << std::setprecision(10)
         << "{\n  \"experiment\":\"oracle-topology-residual-prediction-extension\",\n"
         << "  \"distinctFromOriginal\":true,\n"
         << "  \"priorResult\":{\"status\":\"underpowered-gate-failure\","
            "\"artifact\":\"/tmp/drop7-oracle-topology-residual.json\"},\n"
         << "  \"frozenModel\":{\"path\":\"" << options.model
         << "\",\"sha256\":\"" << frozen.sha256
         << "\",\"expectedSha256\":\"" << kFrozenModelSha256
         << "\",\"fingerprintFnv1a64\":\"0x" << std::hex
         << frozen.fingerprint << std::dec
         << "\",\"expectedFingerprintFnv1a64\":\"0x" << std::hex
         << kFrozenFingerprint << std::dec
         << "\",\"verifiedBeforeCollection\":true,\"retrained\":false},\n"
         << "  \"protocol\":{\"originalReplaySeedStart\":"
         << kOriginalHeldoutSeedStart << ",\"originalReplayGames\":"
         << kOriginalGames << ",\"extensionSeedStart\":"
         << kExtensionSeedStart << ",\"extensionGames\":"
         << kExtensionGames << ",\"maximumMoves\":"
         << base::kTrainingMaximumMoves
         << ",\"matching\":[\"risePhase\",\"exactOccupancy\","
            "\"exactMaximumHeight\",\"twentyMoveBand\"],"
            "\"modelInput\":\"reflection-canonical-board-only\","
            "\"noRetrainingOrTuning\":true},\n"
         << "  \"originalReplay\":";
  writeReplication(output, original_data, original_prediction);
  output << ",\n  \"extension\":";
  writeReplication(output, extension_data, extension_prediction);
  output << ",\n  \"combined\":";
  writeReplication(output, combined_data, combined_prediction);
  output << ",\n  \"gates\":{\"originalReproduced\":"
         << (original_reproduced ? "true" : "false")
         << ",\"extensionPassed\":"
         << (extension_passed ? "true" : "false")
         << ",\"combinedPassed\":"
         << (combined_passed ? "true" : "false")
         << ",\"replicationPassed\":"
         << (original_reproduced && extension_passed && combined_passed
                 ? "true"
                 : "false")
         << ",\"extensionThresholds\":{\"auc\":0.58,"
            "\"pairAccuracy\":0.56,\"halfPairAccuracy\":0.53},"
            "\"combinedThresholds\":{\"examples\":200,\"pairs\":100,"
            "\"auc\":0.58,\"pairAccuracy\":0.56,"
            "\"halfPairAccuracy\":0.53}},\n"
         << "  \"collectionWallSeconds\":{\"originalReplay\":"
         << original_collection.wall_seconds << ",\"extension\":"
         << extension_collection.wall_seconds << "},\n"
         << "  \"policyDiagnostic\":";
  base::writePolicyDiagnostic(output, policy);
  output << ",\n  \"diagnosticSource\":{\"path\":\"" << options.labels
         << "\",\"sha256\":\"" << kFrozenLabelsSha256
         << "\",\"split\":\"training\",\"uniformStates\":"
         << base::kDiagnosticStates
         << ",\"ranOnlyAfterReplicationGate\":"
         << (original_reproduced && extension_passed && combined_passed
                 ? "true"
                 : "false")
         << "},\n  \"screen\":";
  if (screen == nullptr) {
    output << "null";
  } else {
    base::writeGameplay(output, kScreenSeedStart, *screen, *screen_baseline,
                        *screen_candidate, *screen_paired, screen_passed);
  }
  output << ",\n  \"confirmation\":";
  if (confirmation == nullptr) {
    output << "null";
  } else {
    base::writeGameplay(output, kConfirmationSeedStart, *confirmation,
                        *confirmation_baseline, *confirmation_candidate,
                        *confirmation_paired, confirmation_passed);
  }
  output << ",\n  \"screenRan\":" << (screen != nullptr ? "true" : "false")
         << ",\n  \"screenPassed\":" << (screen_passed ? "true" : "false")
         << ",\n  \"confirmationRan\":"
         << (confirmation != nullptr ? "true" : "false")
         << ",\n  \"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\n  \"qualified\":"
         << (original_reproduced && extension_passed && combined_passed &&
                     policy.passed && screen_passed && confirmation_passed
                 ? "true"
                 : "false")
         << ",\n  \"wallSeconds\":" << wall_seconds
         << ",\n  \"peakRssBytes\":" << base::peakRssBytes() << "\n}\n";
}

bool selfTest(const Options& options, std::ostream& output) {
  const bool base_passed = base::selfTest(output);
  const bool sha_known =
      sha256("abc") ==
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  const FrozenModel frozen = loadFrozenModel(options.model);
  const std::vector<State> diagnostics =
      loadTrainingDiagnostics(options.labels);
  const State& state = diagnostics[diagnostics.size() / 2];
  const double value = base::boardLogit(frozen.model, state.board);
  const double reflected = base::boardLogit(
      frozen.model, cfpi::detail::mirrorBoard(state.board));
  const bool finite = std::isfinite(value);
  const bool reflection = value == reflected;
  base::PredictionMetrics passing;
  passing.examples = 200;
  passing.pairs = 100;
  passing.auc = 0.58;
  passing.pair_accuracy = 0.56;
  passing.first_half_accuracy = 0.53;
  passing.second_half_accuracy = 0.53;
  const bool gates = extensionGate(passing) && combinedGate(passing);
  --passing.pairs;
  const bool sample_gate = extensionGate(passing) && !combinedGate(passing);
  const bool ranges = kExtensionSeedStart == 0x3d9d'0008u &&
                      kExtensionSeedStart + kExtensionGames ==
                          0x3d9d'0010u &&
                      kScreenSeedStart == 0x3ea9'0000u &&
                      kConfirmationSeedStart == 0x3eaa'0000u;
  const bool frozen_ok = frozen.sha256 == kFrozenModelSha256 &&
                         frozen.fingerprint == kFrozenFingerprint;
  const bool passed = base_passed && sha_known && frozen_ok &&
                      diagnostics.size() == base::kDiagnosticStates && finite &&
                      reflection && gates && sample_gate && ranges;
  output << "ORACLE_TOPOLOGY_RESIDUAL_EXTENSION_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"basePassed\":" << (base_passed ? "true" : "false")
         << ",\"shaKnownVector\":" << (sha_known ? "true" : "false")
         << ",\"modelShaVerified\":"
         << (frozen.sha256 == kFrozenModelSha256 ? "true" : "false")
         << ",\"fingerprintVerified\":"
         << (frozen.fingerprint == kFrozenFingerprint ? "true" : "false")
         << ",\"trainingDiagnostics\":" << diagnostics.size()
         << ",\"finite\":" << (finite ? "true" : "false")
         << ",\"reflection\":" << (reflection ? "true" : "false")
         << ",\"gateBoundary\":" << (gates ? "true" : "false")
         << ",\"sampleGateBoundary\":"
         << (sample_gate ? "true" : "false")
         << ",\"ranges\":" << (ranges ? "true" : "false") << "}\n";
  return passed;
}

int run(const Options& options, std::ostream& output) {
  const auto started = Clock::now();
  const FrozenModel frozen = loadFrozenModel(options.model);

  const base::CollectedSplit original_collection =
      base::collectSplit(kOriginalHeldoutSeedStart, kOriginalGames,
                         "extension-original-replay");
  const base::MatchedDataset original_data = base::matchRecords(
      original_collection.fair, original_collection.oracle);
  const base::PredictionMetrics original_prediction = base::predictionMetrics(
      frozen.model, original_data,
      kOriginalHeldoutSeedStart + static_cast<std::uint32_t>(kOriginalGames / 2));
  const bool original_reproduced = originalReproduced(original_prediction);
  if (!original_reproduced) {
    throw std::runtime_error(
        "original heldout metrics did not reproduce; extension remains unread");
  }

  const base::CollectedSplit extension_collection =
      base::collectSplit(kExtensionSeedStart, kExtensionGames, "extension");
  const base::MatchedDataset extension_data = base::matchRecords(
      extension_collection.fair, extension_collection.oracle);
  const base::PredictionMetrics extension_prediction =
      base::predictionMetrics(
          frozen.model, extension_data,
          kExtensionSeedStart + static_cast<std::uint32_t>(kExtensionGames / 2));
  const base::MatchedDataset combined_data =
      combine(original_data, extension_data);
  const base::PredictionMetrics combined_prediction =
      base::predictionMetrics(frozen.model, combined_data, kExtensionSeedStart);
  const bool extension_passed = extensionGate(extension_prediction);
  const bool combined_passed = combinedGate(combined_prediction);
  const bool replication_passed =
      original_reproduced && extension_passed && combined_passed;

  base::PolicyDiagnostic policy;
  if (replication_passed) {
    policy = base::diagnosePolicy(
        frozen.model, loadTrainingDiagnostics(options.labels));
  }

  base::GameplayCohort screen;
  base::GameSummary screen_baseline;
  base::GameSummary screen_candidate;
  base::PairedGameplay screen_paired;
  bool screen_passed = false;
  if (replication_passed && policy.passed) {
    screen = base::runGameplayCohort(
        kScreenSeedStart, base::kScreenGames, frozen.model,
        *policy.selected_coefficient, "extension-screen");
    screen_baseline = base::summarizeGames(screen.baseline);
    screen_candidate = base::summarizeGames(screen.candidate);
    screen_paired = base::pairedGameplay(screen);
    screen_passed = base::improvesBoth(screen_baseline, screen_candidate);
  }

  base::GameplayCohort confirmation;
  base::GameSummary confirmation_baseline;
  base::GameSummary confirmation_candidate;
  base::PairedGameplay confirmation_paired;
  bool confirmation_passed = false;
  if (screen_passed) {
    confirmation = base::runGameplayCohort(
        kConfirmationSeedStart, base::kConfirmationGames, frozen.model,
        *policy.selected_coefficient, "extension-confirmation");
    confirmation_baseline = base::summarizeGames(confirmation.baseline);
    confirmation_candidate = base::summarizeGames(confirmation.candidate);
    confirmation_paired = base::pairedGameplay(confirmation);
    confirmation_passed =
        base::improvesBoth(confirmation_baseline, confirmation_candidate);
  }
  const double wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  writeArtifact(
      options, frozen, original_collection, extension_collection,
      original_data, extension_data, combined_data, original_prediction,
      extension_prediction, combined_prediction, original_reproduced,
      extension_passed, combined_passed, policy,
      replication_passed && policy.passed ? &screen : nullptr,
      replication_passed && policy.passed ? &screen_baseline : nullptr,
      replication_passed && policy.passed ? &screen_candidate : nullptr,
      replication_passed && policy.passed ? &screen_paired : nullptr,
      screen_passed, screen_passed ? &confirmation : nullptr,
      screen_passed ? &confirmation_baseline : nullptr,
      screen_passed ? &confirmation_candidate : nullptr,
      screen_passed ? &confirmation_paired : nullptr, confirmation_passed,
      wall_seconds);

  output << std::fixed << std::setprecision(4)
         << "ORACLE_TOPOLOGY_RESIDUAL_EXTENSION_RESULT {"
            "\"originalReproduced\":"
         << (original_reproduced ? "true" : "false")
         << ",\"extensionPairs\":" << extension_prediction.pairs
         << ",\"extensionAuc\":" << extension_prediction.auc
         << ",\"extensionPairAccuracy\":"
         << extension_prediction.pair_accuracy
         << ",\"extensionFirstHalf\":"
         << extension_prediction.first_half_accuracy
         << ",\"extensionSecondHalf\":"
         << extension_prediction.second_half_accuracy
         << ",\"combinedPairs\":" << combined_prediction.pairs
         << ",\"combinedAuc\":" << combined_prediction.auc
         << ",\"combinedPairAccuracy\":"
         << combined_prediction.pair_accuracy
         << ",\"combinedFirstHalf\":"
         << combined_prediction.first_half_accuracy
         << ",\"combinedSecondHalf\":"
         << combined_prediction.second_half_accuracy
         << ",\"extensionPassed\":"
         << (extension_passed ? "true" : "false")
         << ",\"combinedPassed\":"
         << (combined_passed ? "true" : "false")
         << ",\"policyPassed\":" << (policy.passed ? "true" : "false")
         << ",\"selectedCoefficient\":";
  if (policy.selected_coefficient.has_value()) {
    output << *policy.selected_coefficient;
  } else {
    output << "null";
  }
  output << ",\"screenRan\":"
         << (replication_passed && policy.passed ? "true" : "false")
         << ",\"screenPassed\":" << (screen_passed ? "true" : "false")
         << ",\"confirmationRan\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\"wallSeconds\":" << wall_seconds
         << ",\"peakRssBytes\":" << base::peakRssBytes()
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return 0;
}

}  // namespace drop7::oracle_topology_residual_extension

int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      const auto options =
          drop7::oracle_topology_residual_extension::parseOptions(argc, argv, 2);
      return drop7::oracle_topology_residual_extension::selfTest(options,
                                                                 std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::oracle_topology_residual_extension::parseOptions(argc, argv, 2);
      return drop7::oracle_topology_residual_extension::run(options,
                                                            std::cout);
    }
    std::cerr
        << "usage: drop7_oracle_topology_residual_extension --self-test | "
           "--run [--model PATH] [--labels PATH] [--output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_oracle_topology_residual_extension: " << error.what()
              << '\n';
    return 1;
  }
}
