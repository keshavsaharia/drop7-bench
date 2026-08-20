#define DROP7_FAIR_ONLY_HORIZON_LIBRARY
#include "../../fair-expectimax/reference/fair-only-horizon.cpp"
#undef DROP7_FAIR_ONLY_HORIZON_LIBRARY

#include <bit>
#include <cstring>
#include <filesystem>
#include <numbers>
#include <optional>
#include <sstream>
#include <type_traits>
#include <unordered_set>

// Runs an artifact-only long-outcome diagnostic.  The corpus split, stochastic
// tape, horizon, balanced return, sparse architecture, optimizer, and
// acceptance gates are fixed before generating any horizon-100 label.  This
// translation unit has no gameplay runner and accepts no gameplay seed.
namespace drop7::curriculum_long_outcome_nnue {

namespace fair = drop7::fair_only_horizon;
namespace detail = drop7::cfpi::detail;
using Clock = std::chrono::steady_clock;

constexpr int kCorpusStates = 4'096;
constexpr int kTrainingStates = 3'072;
constexpr int kHeldoutStates = 1'024;
constexpr int kScenarios = 7;
constexpr int kHorizon = 100;
constexpr int kEventsPerStep = 128;
constexpr int kDefaultThreads = 8;
constexpr int kBoardTokens = 10;
constexpr int kBoardInputs = kCellCount * kBoardTokens;
constexpr int kNextInputs = kBoardSize;
constexpr int kPhaseInputs = kMovesPerLevel;
constexpr int kActionInputs = kBoardSize;
constexpr int kAggregateInputs = 32;
constexpr int kInputs =
    kBoardInputs + kNextInputs + kPhaseInputs + kActionInputs +
    kAggregateInputs;
constexpr int kActiveInputs = kCellCount + 3 + kAggregateInputs;
constexpr int kHidden = 96;
constexpr int kHeads = 5;
constexpr int kEpochs = 36;
constexpr int kBatchRoots = 32;
constexpr double kLearningRate = 0.0015;
constexpr double kFinalLearningRateRatio = 0.15;
constexpr double kWeightDecay = 0.00002;
constexpr double kPairwiseLossWeight = 0.06;
constexpr double kPairwiseTemperature = 0.15;
constexpr std::array<double, kHeads> kHeadLossWeights{{
    1.0, 0.25, 0.15, 0.15, 0.15,
}};

constexpr double kBalancedSurvivalWeight = 0.45;
constexpr double kBalancedScoreWeight = 0.30;
constexpr double kBalancedClearsWeight = 0.15;
constexpr double kBalancedRevealsWeight = 0.10;
constexpr double kScoreScale = 350'000.0;
constexpr double kClearsScale = 220.0;
constexpr double kRevealsScale = 120.0;

constexpr double kGateTop1Gain = 0.05;
constexpr double kGatePairwiseGain = 0.03;
constexpr double kGateRegretRatio = 0.90;
constexpr double kGateAbsoluteTop1 = 0.45;
constexpr double kGateAbsolutePairwise = 0.65;

constexpr double kWallLimitSeconds = 45.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr std::uint64_t kCheckpointLimitBytes = 1ull * 1024ull * 1024ull;
constexpr std::uintmax_t kExpectedCorpusBytes = 4'864'041;
constexpr std::string_view kExpectedCorpusSha256 =
    "c963ac242994e7d18020fd7369954be2f4015d7f6c972f6d5fffe79c371db226";
constexpr std::uint64_t kExpectedDatasetFingerprint =
    0xc649'f123'fc0c'c4b9ull;
constexpr std::uint64_t kSplitDomain = 0x4355'5252'5350'4c54ull;
constexpr std::uint64_t kHashHalfDomain = 0x4355'5252'4841'4c46ull;
constexpr std::uint64_t kChanceKeyDomain = 0x434c'4f4e'4754'4150ull;
constexpr std::uint32_t kRevealDomain = 0x434c'5256u;   // "CLRV"
constexpr std::uint32_t kVisibleDomain = 0x434c'5653u;  // "CLVS"
constexpr std::uint32_t kInitializationDomain = 0x434c'4e4eu;

static_assert(kLevelBonus == 17'000);
static_assert(kCorpusStates == kTrainingStates + kHeldoutStates);
static_assert(kScenarios == kBoardSize && kHorizon == 100);
static_assert(kEventsPerStep > kCellCount + kBoardSize);
static_assert(kInputs == 541 && kActiveInputs == 84);
static_assert(kHidden == 96 && kHeads == 5 && kEpochs == 36);
static_assert(kBalancedSurvivalWeight + kBalancedScoreWeight +
                      kBalancedClearsWeight + kBalancedRevealsWeight ==
                  1.0);
static_assert(kGateTop1Gain == 0.05 && kGatePairwiseGain == 0.03);
static_assert(kGateRegretRatio == 0.90 && kGateAbsoluteTop1 == 0.45 &&
              kGateAbsolutePairwise == 0.65);
static_assert(kRevealDomain != kVisibleDomain);
static_assert((kInputs * kHidden + kHidden + kHeads * kHidden + kHeads) *
                      sizeof(float) <
                  kCheckpointLimitBytes);

constexpr std::array<int, kBoardSize> kActionOrder{{3, 2, 4, 1, 5, 0, 6}};

struct Options {
  std::string states = "/tmp/drop7-oracle-curriculum-states.jsonl";
  std::string output = "/tmp/drop7-curriculum-long-outcome-nnue.json";
  std::string labels =
      "/tmp/drop7-curriculum-long-outcome-labels.jsonl";
  std::string checkpoint =
      "/tmp/drop7-curriculum-long-outcome-nnue.bin";
  int threads = kDefaultThreads;
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string flag = argv[index];
    if (flag == "--states") {
      result.states = argv[index + 1];
    } else if (flag == "--output") {
      result.output = argv[index + 1];
    } else if (flag == "--labels") {
      result.labels = argv[index + 1];
    } else if (flag == "--checkpoint") {
      result.checkpoint = argv[index + 1];
    } else if (flag == "--threads") {
      result.threads = std::stoi(argv[index + 1]);
      if (result.threads < 1 || result.threads > 16) {
        throw std::invalid_argument("threads must be in [1,16]");
      }
    } else {
      throw std::invalid_argument("unknown option " + flag);
    }
  }
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
    throw std::runtime_error("curriculum NNUE exceeded 256 MiB RSS cap");
  }
}

struct Deadline {
  Clock::time_point started = Clock::now();

  double elapsedSeconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }

  void check() const {
    if (elapsedSeconds() > kWallLimitSeconds) {
      throw std::runtime_error("curriculum NNUE exceeded 45 minute wall cap");
    }
  }
};

struct PublicState {
  Board board{};
  std::uint8_t next_disc = 1;
  std::uint8_t moves_remaining = kMovesPerLevel;

  bool operator==(const PublicState&) const = default;
};

State materialize(const PublicState& source) {
  State result;
  result.board = source.board;
  result.next_disc = source.next_disc;
  result.moves_remaining = source.moves_remaining;
  result.score = 0;
  result.level = 1;
  result.moves_played = 0;
  result.game_over = false;
  return result;
}

PublicState publicState(const State& source) {
  if (source.game_over || source.next_disc < 1 ||
      source.next_disc > kBoardSize || source.moves_remaining < 1 ||
      source.moves_remaining > kMovesPerLevel) {
    throw std::invalid_argument("invalid public restart state");
  }
  for (const std::uint8_t cell : source.board) {
    if (cell > kCracked) throw std::invalid_argument("invalid board token");
  }
  return {source.board, source.next_disc,
          static_cast<std::uint8_t>(source.moves_remaining)};
}

PublicState mirror(const PublicState& source) {
  PublicState result = source;
  result.board = detail::mirrorBoard(source.board);
  return result;
}

PublicState canonicalState(const PublicState& source, bool& mirrored) {
  mirrored = detail::mirroredRepresentationIsSmaller(source.board);
  return mirrored ? mirror(source) : source;
}

struct StateAction {
  PublicState state{};
  int action = -1;
};

StateAction canonicalStateAction(const PublicState& source, int action) {
  if (action < 0 || action >= kBoardSize) {
    throw std::invalid_argument("invalid state-action column");
  }
  const PublicState reflected = mirror(source);
  const int reflected_action = kBoardSize - 1 - action;
  if (reflected.board < source.board ||
      (reflected.board == source.board && reflected_action < action)) {
    return {reflected, reflected_action};
  }
  return {source, action};
}

std::string publicKey(const PublicState& source) {
  bool ignored = false;
  const PublicState state = canonicalState(source, ignored);
  std::string result;
  result.reserve(kCellCount + 2);
  for (const std::uint8_t cell : state.board) {
    result.push_back(static_cast<char>(cell));
  }
  result.push_back(static_cast<char>(state.next_disc));
  result.push_back(static_cast<char>(state.moves_remaining));
  return result;
}

std::uint64_t mix64(std::uint64_t value) {
  value ^= value >> 30u;
  value *= 0xbf58'476d'1ce4'e5b9ull;
  value ^= value >> 27u;
  value *= 0x94d0'49bb'1331'11ebull;
  return value ^ (value >> 31u);
}

std::uint64_t publicHash(const PublicState& source) {
  bool ignored = false;
  const PublicState state = canonicalState(source, ignored);
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint64_t>(cell + 1u);
    hash *= 0x0000'0100'0000'01b3ull;
  }
  hash ^= state.next_disc;
  hash *= 0x0000'0100'0000'01b3ull;
  hash ^= static_cast<std::uint64_t>(state.moves_remaining + 1u);
  return mix64(hash);
}

std::uint32_t publicChanceKey(const PublicState& source) {
  const std::uint64_t value = mix64(publicHash(source) ^ kChanceKeyDomain);
  return mix32(static_cast<std::uint32_t>(value) ^
               static_cast<std::uint32_t>(value >> 32u));
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
      const std::uint32_t s0 = std::rotr(words[word - 15], 7) ^
                               std::rotr(words[word - 15], 18) ^
                               (words[word - 15] >> 3);
      const std::uint32_t s1 = std::rotr(words[word - 2], 17) ^
                               std::rotr(words[word - 2], 19) ^
                               (words[word - 2] >> 10);
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
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(64);
  for (const std::uint32_t value : hash) {
    for (int nibble = 7; nibble >= 0; --nibble) {
      result.push_back(digits[(value >> (nibble * 4)) & 0x0fu]);
    }
  }
  return result;
}

std::string readWholeFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) throw std::runtime_error("could not read curriculum artifact");
  const std::streampos end = input.tellg();
  if (end < 0) throw std::runtime_error("invalid curriculum artifact size");
  std::string result(static_cast<std::size_t>(end), '\0');
  input.seekg(0);
  input.read(result.data(), static_cast<std::streamsize>(result.size()));
  if (!input) throw std::runtime_error("curriculum artifact read failed");
  return result;
}

std::size_t valueOffset(const std::string& line, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\":";
  const std::size_t found = line.find(needle);
  if (found == std::string::npos) {
    throw std::runtime_error("curriculum field missing: " +
                             std::string(key));
  }
  return found + needle.size();
}

std::string stringField(const std::string& line, std::string_view key) {
  std::size_t begin = valueOffset(line, key);
  if (begin >= line.size() || line[begin] != '"') {
    throw std::runtime_error("curriculum string field malformed");
  }
  ++begin;
  const std::size_t end = line.find('"', begin);
  if (end == std::string::npos) {
    throw std::runtime_error("curriculum string field unterminated");
  }
  return line.substr(begin, end - begin);
}

double numberField(const std::string& line, std::string_view key) {
  const std::size_t begin = valueOffset(line, key);
  char* end = nullptr;
  const double value = std::strtod(line.c_str() + begin, &end);
  if (end == line.c_str() + begin || !std::isfinite(value)) {
    throw std::runtime_error("curriculum numeric field malformed");
  }
  return value;
}

int integerField(const std::string& line, std::string_view key) {
  const double value = numberField(line, key);
  if (value != std::floor(value) || value < std::numeric_limits<int>::min() ||
      value > std::numeric_limits<int>::max()) {
    throw std::runtime_error("curriculum integer field malformed");
  }
  return static_cast<int>(value);
}

std::uint64_t parseHex64(std::string_view source) {
  if (source.size() != 18 || source.substr(0, 2) != "0x") {
    throw std::runtime_error("curriculum public hash malformed");
  }
  std::uint64_t result = 0;
  for (const char digit : source.substr(2)) {
    result <<= 4u;
    if (digit >= '0' && digit <= '9') result |= digit - '0';
    else if (digit >= 'a' && digit <= 'f') result |= digit - 'a' + 10;
    else throw std::runtime_error("curriculum public hash digit malformed");
  }
  return result;
}

enum class FlowBand : std::uint8_t {
  kBlocked,
  kClosed,
  kRecovering,
  kFlowing,
};

struct FlowAggregates {
  int occupancy = 0;
  int maximum_height = 0;
  int covers = 0;
  int legal_columns = 0;
  double mean_moves = 0.0;
  double survival = 0.0;
  double mean_score = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double mean_maximum_chain = 0.0;
  FlowBand band = FlowBand::kBlocked;
};

struct CorpusRecord {
  PublicState state{};
  std::uint64_t public_hash = 0;
  FlowAggregates flow{};
  int hash_half = 0;
};

FlowBand parseFlowBand(const std::string& value) {
  if (value == "blocked") return FlowBand::kBlocked;
  if (value == "closed") return FlowBand::kClosed;
  if (value == "recovering") return FlowBand::kRecovering;
  if (value == "flowing") return FlowBand::kFlowing;
  throw std::runtime_error("unknown curriculum flow band");
}

CorpusRecord parseRecord(const std::string& line) {
  if (stringField(line, "format") != "drop7-public-restart-v1" ||
      line.find("\"independentRestartValidated\":true") ==
          std::string::npos ||
      line.find("\"sourceSeed\"") != std::string::npos ||
      line.find("\"futureTape\"") != std::string::npos ||
      line.find("\"history\"") != std::string::npos) {
    throw std::runtime_error("curriculum public-boundary schema mismatch");
  }
  CorpusRecord result;
  const std::string board = stringField(line, "board");
  if (board.size() != kCellCount) {
    throw std::runtime_error("curriculum board length mismatch");
  }
  for (int index = 0; index < kCellCount; ++index) {
    if (board[index] < '0' || board[index] > '9') {
      throw std::runtime_error("curriculum board token malformed");
    }
    result.state.board[index] =
        static_cast<std::uint8_t>(board[index] - '0');
    if (result.state.board[index] > kCracked) {
      throw std::runtime_error("curriculum board token out of range");
    }
  }
  result.state.next_disc =
      static_cast<std::uint8_t>(integerField(line, "nextDisc"));
  result.state.moves_remaining =
      static_cast<std::uint8_t>(integerField(line, "movesRemaining"));
  if (result.state.next_disc < 1 || result.state.next_disc > kBoardSize ||
      result.state.moves_remaining < 1 ||
      result.state.moves_remaining > kMovesPerLevel) {
    throw std::runtime_error("curriculum visible state out of range");
  }
  result.public_hash = parseHex64(stringField(line, "publicHash"));
  if (result.public_hash != publicHash(result.state) ||
      detail::mirroredRepresentationIsSmaller(result.state.board)) {
    throw std::runtime_error("curriculum hash/canonicalization mismatch");
  }
  result.flow.occupancy = integerField(line, "occupancy");
  result.flow.maximum_height = integerField(line, "maximumHeight");
  result.flow.covers = integerField(line, "covers");
  result.flow.legal_columns = integerField(line, "legalColumns");
  result.flow.mean_moves = numberField(line, "meanMoves");
  result.flow.survival = numberField(line, "survivalRate");
  result.flow.mean_score = numberField(line, "meanScoreDelta");
  result.flow.clears_per_move = numberField(line, "clearsPerMove");
  result.flow.reveals_per_move = numberField(line, "revealsPerMove");
  result.flow.mean_maximum_chain = numberField(line, "meanMaximumChain");
  result.flow.band = parseFlowBand(stringField(line, "flowBand"));
  result.hash_half = static_cast<int>(
      mix64(result.public_hash ^ kHashHalfDomain) & 1ull);

  const auto heights = detail::columnHeights(result.state.board);
  const int occupied = static_cast<int>(std::count_if(
      result.state.board.begin(), result.state.board.end(),
      [](std::uint8_t cell) { return cell != kEmpty; }));
  const int covers = static_cast<int>(std::count_if(
      result.state.board.begin(), result.state.board.end(),
      [](std::uint8_t cell) { return cell == kSolid || cell == kCracked; }));
  int legal_count = 0;
  legalColumns(result.state.board, legal_count);
  if (result.flow.occupancy != occupied || result.flow.covers != covers ||
      result.flow.maximum_height !=
          *std::max_element(heights.begin(), heights.end()) ||
      result.flow.legal_columns != legal_count || legal_count < 1 ||
      result.flow.mean_moves < 0.0 || result.flow.mean_moves > 25.0 ||
      result.flow.survival < 0.0 || result.flow.survival > 1.0 ||
      result.flow.mean_score < 0.0 || result.flow.clears_per_move < 0.0 ||
      result.flow.reveals_per_move < 0.0 ||
      result.flow.mean_maximum_chain < 0.0) {
    throw std::runtime_error("curriculum flow/shape aggregate mismatch");
  }
  const FlowBand expected =
      result.flow.survival < 0.25
          ? FlowBand::kBlocked
          : (result.flow.reveals_per_move < 0.25
                 ? FlowBand::kClosed
                 : (result.flow.reveals_per_move < 0.60
                        ? FlowBand::kRecovering
                        : FlowBand::kFlowing));
  if (result.flow.band != expected) {
    throw std::runtime_error("curriculum flow band mismatch");
  }
  return result;
}

struct Corpus {
  std::vector<CorpusRecord> training;
  std::vector<CorpusRecord> heldout;
  std::array<int, 2> training_halves{};
  std::array<int, 2> heldout_halves{};
};

Corpus loadCorpus(const std::string& path) {
  std::error_code error;
  if (std::filesystem::file_size(path, error) != kExpectedCorpusBytes ||
      error) {
    throw std::runtime_error("curriculum byte-count checksum mismatch");
  }
  const std::string source = readWholeFile(path);
  if (sha256(source) != kExpectedCorpusSha256) {
    throw std::runtime_error("curriculum SHA-256 checksum mismatch");
  }
  std::vector<CorpusRecord> records;
  records.reserve(kCorpusStates);
  std::unordered_set<std::string> unique;
  std::istringstream input(source);
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty()) continue;
    CorpusRecord record = parseRecord(line);
    if (!unique.insert(publicKey(record.state)).second) {
      throw std::runtime_error("curriculum duplicate public state");
    }
    records.push_back(std::move(record));
  }
  if (records.size() != kCorpusStates || unique.size() != kCorpusStates) {
    throw std::runtime_error("curriculum record-count mismatch");
  }
  std::sort(records.begin(), records.end(),
            [](const CorpusRecord& first, const CorpusRecord& second) {
              const std::uint64_t first_rank =
                  mix64(first.public_hash ^ kSplitDomain);
              const std::uint64_t second_rank =
                  mix64(second.public_hash ^ kSplitDomain);
              if (first_rank != second_rank) return first_rank < second_rank;
              return first.public_hash < second.public_hash;
            });
  Corpus result;
  result.training.reserve(kTrainingStates);
  result.heldout.reserve(kHeldoutStates);
  for (int index = 0; index < kCorpusStates; ++index) {
    CorpusRecord record = std::move(records[index]);
    if (index < kTrainingStates) {
      ++result.training_halves[record.hash_half];
      result.training.push_back(std::move(record));
    } else {
      ++result.heldout_halves[record.hash_half];
      result.heldout.push_back(std::move(record));
    }
  }
  if (result.training.size() != kTrainingStates ||
      result.heldout.size() != kHeldoutStates ||
      result.training_halves[0] == 0 || result.training_halves[1] == 0 ||
      result.heldout_halves[0] == 0 || result.heldout_halves[1] == 0) {
    throw std::runtime_error("curriculum deterministic hash split failed");
  }
  return result;
}

struct PublicRandom {
  std::uint32_t key = 0;
  int scenario = 0;
  int step = 0;
  int event = 0;

  std::uint8_t nextDisc() {
    if (event >= kEventsPerStep) {
      throw std::runtime_error("public chance event slice exhausted");
    }
    const int event_index = step * kEventsPerStep + event++;
    const double unit = detail::stratifiedUnit(
        key, scenario, kScenarios, kRevealDomain, event_index);
    return static_cast<std::uint8_t>(
        std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
  }
};

std::uint8_t visibleDisc(std::uint32_t key, int scenario, int step) {
  const double unit = detail::stratifiedUnit(
      key, scenario, kScenarios, kVisibleDomain, step);
  return static_cast<std::uint8_t>(
      std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
}

struct PublicTransition {
  PublicState state{};
  Board terminal_board{};
  bool terminal = false;
  std::int64_t score = 0;
  int clears = 0;
  int reveals = 0;
  int waves = 0;
  int maximum_chain = 0;

  bool operator==(const PublicTransition&) const = default;
};

PublicTransition playPublicMove(const PublicState& source, int action,
                                std::uint32_t root_key, int scenario,
                                int step) {
  if (scenario < 0 || scenario >= kScenarios || step < 0 ||
      step >= kHorizon) {
    throw std::invalid_argument("invalid public chance coordinate");
  }
  const StateAction canonical = canonicalStateAction(source, action);
  if (!isLegal(canonical.state.board, canonical.action)) {
    throw std::invalid_argument("public chance action is illegal");
  }
  State state = materialize(canonical.state);
  PublicRandom random{root_key, scenario, step, 0};
  MoveResult move;
  if (!detail::playMoveSampled(state, canonical.action, random, move)) {
    throw std::runtime_error("public chance transition failed");
  }
  PublicTransition result;
  result.terminal = move.state.game_over;
  result.terminal_board = move.state.board;
  result.score = move.score_delta;
  result.waves = static_cast<int>(move.waves.size());
  for (const Wave& wave : move.waves) {
    result.clears += wave.cleared;
    result.reveals += wave.revealed;
    result.maximum_chain = std::max(result.maximum_chain, wave.depth);
  }
  if (!result.terminal) {
    move.state.score = 0;
    move.state.level = 1;
    move.state.moves_played = 0;
    move.state.next_disc = visibleDisc(root_key, scenario, step);
    bool ignored = false;
    result.state = canonicalState(publicState(move.state), ignored);
  }
  return result;
}

using PublicMove = PublicTransition (*)(const PublicState&, int,
                                        std::uint32_t, int, int);
static_assert(std::is_same_v<decltype(&playPublicMove), PublicMove>);
static_assert(!std::is_invocable_v<PublicMove, const State&, int,
                                   std::uint32_t, int, int>);

struct D1Evaluation {
  int action = -1;
  std::array<double, kBoardSize> values{};
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
};

D1Evaluation evaluateD1(const PublicState& source) {
  bool mirrored = false;
  const PublicState canonical = canonicalState(source, mirrored);
  fair::SearchContext context;
  const fair::RootEvaluation root =
      fair::rootDecision(materialize(canonical), 1, context);
  int legal = 0;
  int finite = 0;
  for (int action = 0; action < kBoardSize; ++action) {
    legal += isLegal(canonical.board, action);
    finite += std::isfinite(root.values[action]);
  }
  if (root.action < 0 || legal != finite || context.work > 70 ||
      !context.cache.empty()) {
    throw std::runtime_error("exact public fair-D1 failed");
  }
  D1Evaluation result;
  result.action = mirrored ? kBoardSize - 1 - root.action : root.action;
  result.values.fill(-std::numeric_limits<double>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    const int source_action = mirrored ? kBoardSize - 1 - action : action;
    result.values[source_action] = root.values[action];
  }
  result.work = context.work;
  result.nodes = context.nodes;
  return result;
}

struct Shape {
  int occupancy = 0;
  int maximum_height = 0;
  int covers = 0;
  int legal_columns = 0;
};

Shape shape(const Board& board) {
  Shape result;
  const auto heights = detail::columnHeights(board);
  result.maximum_height = *std::max_element(heights.begin(), heights.end());
  for (const std::uint8_t cell : board) {
    result.occupancy += cell != kEmpty;
    result.covers += cell == kSolid || cell == kCracked;
  }
  legalColumns(board, result.legal_columns);
  return result;
}

detail::PhaseFeatures boardCertificate(const Board& board,
                                       std::uint8_t next_disc,
                                       std::uint8_t moves_remaining) {
  PublicState state{board, next_disc, moves_remaining};
  return detail::extractPhaseFeatures(materialize(state));
}

double clogDebt(const detail::PhaseFeatures& value) {
  return 8.0 * value.adjacent_ones + 12.0 * value.triple_twos +
         4.0 * value.dead_low_numbers;
}

struct ScenarioLabel {
  int moves = 0;
  std::int64_t score = 0;
  int clears = 0;
  int reveals = 0;
  int maximum_chain = 0;
  bool survived = false;

  bool operator==(const ScenarioLabel&) const = default;
};

struct ActionCertificate {
  double terminal_rate = 0.0;
  double mean_score = 0.0;
  double mean_clears = 0.0;
  double mean_reveals = 0.0;
  double mean_waves = 0.0;
  double mean_maximum_chain = 0.0;
  double post_occupancy = 0.0;
  double post_height = 0.0;
  double post_covers = 0.0;
  double post_legal_columns = 0.0;
  double direct_delta = 0.0;
  double latent_delta = 0.0;
  double clog_improvement = 0.0;

  bool operator==(const ActionCertificate&) const = default;
};

struct ActionLabel {
  bool legal = false;
  std::array<ScenarioLabel, kScenarios> scenarios{};
  ActionCertificate certificate{};
  double survival = 0.0;
  double mean_score = 0.0;
  double mean_clears = 0.0;
  double mean_reveals = 0.0;
  double balanced_return = 0.0;
  double normalized_target = 0.0;
  double normalized_d1_q = 0.0;
  std::array<double, kHeads> targets{};
};

struct RootLabel {
  CorpusRecord record{};
  std::array<ActionLabel, kBoardSize> actions{};
  int labeled_action = -1;
  int d1_action = -1;
  std::uint64_t transitions = 0;
  std::uint64_t d1_calls = 0;
  std::uint64_t d1_work = 0;
  std::uint64_t d1_nodes = 0;
  double seconds = 0.0;
};

double positiveTanh(double value, double scale) {
  return std::tanh(std::max(0.0, value) / scale);
}

double balancedReturn(double survival, double score, double clears,
                      double reveals) {
  return kBalancedSurvivalWeight * survival +
         kBalancedScoreWeight * positiveTanh(score, kScoreScale) +
         kBalancedClearsWeight * positiveTanh(clears, kClearsScale) +
         kBalancedRevealsWeight * positiveTanh(reveals, kRevealsScale);
}

void observeCertificate(const PublicState& root,
                        const detail::PhaseFeatures& before,
                        const PublicTransition& transition,
                        ActionCertificate& certificate) {
  const Board& post_board = transition.terminal ? transition.terminal_board
                                                 : transition.state.board;
  const std::uint8_t post_next =
      transition.terminal ? root.next_disc : transition.state.next_disc;
  const std::uint8_t post_phase =
      transition.terminal ? std::uint8_t{1}
                          : transition.state.moves_remaining;
  const Shape post_shape = shape(post_board);
  const detail::PhaseFeatures after =
      boardCertificate(post_board, post_next, post_phase);
  constexpr double inverse = 1.0 / static_cast<double>(kScenarios);
  certificate.terminal_rate += transition.terminal ? inverse : 0.0;
  certificate.mean_score += transition.score * inverse;
  certificate.mean_clears += transition.clears * inverse;
  certificate.mean_reveals += transition.reveals * inverse;
  certificate.mean_waves += transition.waves * inverse;
  certificate.mean_maximum_chain += transition.maximum_chain * inverse;
  certificate.post_occupancy += post_shape.occupancy * inverse;
  certificate.post_height += post_shape.maximum_height * inverse;
  certificate.post_covers += post_shape.covers * inverse;
  certificate.post_legal_columns += post_shape.legal_columns * inverse;
  certificate.direct_delta +=
      (after.direct_potential - before.direct_potential) * inverse;
  certificate.latent_delta +=
      (after.latent_chain_potential - before.latent_chain_potential) *
      inverse;
  certificate.clog_improvement +=
      (clogDebt(before) - clogDebt(after)) * inverse;
}

RootLabel labelRoot(const CorpusRecord& source, const Deadline& deadline) {
  const auto started = Clock::now();
  RootLabel result;
  result.record = source;
  bool ignored = false;
  result.record.state = canonicalState(source.state, ignored);
  const std::uint32_t root_key = publicChanceKey(result.record.state);
  const D1Evaluation root_d1 = evaluateD1(result.record.state);
  result.d1_action = root_d1.action;
  result.d1_work += root_d1.work;
  result.d1_nodes += root_d1.nodes;
  ++result.d1_calls;
  const detail::PhaseFeatures before =
      detail::extractPhaseFeatures(materialize(result.record.state));

  for (const int forced_action : kActionOrder) {
    if (!isLegal(result.record.state.board, forced_action)) continue;
    ActionLabel& action = result.actions[forced_action];
    action.legal = true;
    for (int scenario = 0; scenario < kScenarios; ++scenario) {
      PublicState state = result.record.state;
      ScenarioLabel& outcome = action.scenarios[scenario];
      bool terminal = false;
      for (int step = 0; step < kHorizon; ++step) {
        deadline.check();
        int selected = forced_action;
        if (step > 0) {
          const D1Evaluation continuation = evaluateD1(state);
          selected = continuation.action;
          result.d1_work += continuation.work;
          result.d1_nodes += continuation.nodes;
          ++result.d1_calls;
        }
        if (!isLegal(state.board, selected)) {
          throw std::runtime_error("fair-D1 continuation selected illegal action");
        }
        const PublicTransition transition =
            playPublicMove(state, selected, root_key, scenario, step);
        ++result.transitions;
        ++outcome.moves;
        outcome.score += transition.score;
        outcome.clears += transition.clears;
        outcome.reveals += transition.reveals;
        outcome.maximum_chain =
            std::max(outcome.maximum_chain, transition.maximum_chain);
        if (step == 0) {
          observeCertificate(result.record.state, before, transition,
                             action.certificate);
        }
        if (transition.terminal) {
          terminal = true;
          break;
        }
        state = transition.state;
      }
      outcome.survived = !terminal && outcome.moves == kHorizon;
      constexpr double inverse = 1.0 / static_cast<double>(kScenarios);
      action.survival += outcome.survived ? inverse : 0.0;
      action.mean_score += outcome.score * inverse;
      action.mean_clears += outcome.clears * inverse;
      action.mean_reveals += outcome.reveals * inverse;
    }
    action.balanced_return =
        balancedReturn(action.survival, action.mean_score,
                       action.mean_clears, action.mean_reveals);
  }

  double target_minimum = std::numeric_limits<double>::infinity();
  double target_maximum = -std::numeric_limits<double>::infinity();
  double d1_minimum = std::numeric_limits<double>::infinity();
  double d1_maximum = -std::numeric_limits<double>::infinity();
  for (int action = 0; action < kBoardSize; ++action) {
    if (!result.actions[action].legal) continue;
    target_minimum =
        std::min(target_minimum, result.actions[action].balanced_return);
    target_maximum =
        std::max(target_maximum, result.actions[action].balanced_return);
    d1_minimum = std::min(d1_minimum, root_d1.values[action]);
    d1_maximum = std::max(d1_maximum, root_d1.values[action]);
  }
  const double target_range = target_maximum - target_minimum;
  const double d1_range = d1_maximum - d1_minimum;
  double best = -std::numeric_limits<double>::infinity();
  for (const int action_index : kActionOrder) {
    ActionLabel& action = result.actions[action_index];
    if (!action.legal) continue;
    action.normalized_target =
        target_range > 1.0e-12
            ? (action.balanced_return - target_minimum) / target_range
            : 0.5;
    action.normalized_d1_q =
        d1_range > 1.0e-12
            ? (root_d1.values[action_index] - d1_minimum) / d1_range
            : 0.5;
    action.targets = {{
        action.normalized_target - action.normalized_d1_q,
        action.survival,
        positiveTanh(action.mean_score, kScoreScale),
        positiveTanh(action.mean_clears, kClearsScale),
        positiveTanh(action.mean_reveals, kRevealsScale),
    }};
    if (action.balanced_return > best) {
      best = action.balanced_return;
      result.labeled_action = action_index;
    }
  }
  if (result.labeled_action < 0 || result.d1_action < 0 ||
      result.transitions >
          static_cast<std::uint64_t>(kBoardSize * kScenarios * kHorizon) ||
      result.d1_work > result.d1_calls * 70) {
    throw std::runtime_error("long-outcome root labeling invariant failed");
  }
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

struct LabelCost {
  std::uint64_t roots = 0;
  std::uint64_t legal_actions = 0;
  std::uint64_t transitions = 0;
  std::uint64_t d1_calls = 0;
  std::uint64_t d1_work = 0;
  std::uint64_t d1_nodes = 0;
  double aggregate_root_seconds = 0.0;
  double wall_seconds = 0.0;
};

struct LabeledRange {
  std::vector<RootLabel> roots;
  LabelCost cost{};
};

void addRootCost(const RootLabel& root, LabelCost& cost) {
  ++cost.roots;
  for (const ActionLabel& action : root.actions) {
    cost.legal_actions += action.legal;
  }
  cost.transitions += root.transitions;
  cost.d1_calls += root.d1_calls;
  cost.d1_work += root.d1_work;
  cost.d1_nodes += root.d1_nodes;
  cost.aggregate_root_seconds += root.seconds;
}

LabeledRange labelRange(const std::vector<CorpusRecord>& source,
                        std::string_view name, int threads,
                        const Deadline& deadline) {
  const auto started = Clock::now();
  LabeledRange result;
  result.roots.resize(source.size());
  std::atomic<std::size_t> next{0};
  std::atomic<std::size_t> completed{0};
  std::mutex exception_mutex;
  std::exception_ptr exception;
  std::vector<std::future<void>> workers;
  const int worker_count =
      std::max(1, std::min(threads, static_cast<int>(source.size())));
  workers.reserve(static_cast<std::size_t>(worker_count));
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      try {
        for (;;) {
          const std::size_t index = next.fetch_add(1);
          if (index >= source.size()) return;
          result.roots[index] = labelRoot(source[index], deadline);
          if ((index & 31u) == 0u) enforceRssLimit();
          const std::size_t count = completed.fetch_add(1) + 1;
          if (count % 64 == 0 || count == source.size()) {
            static std::mutex progress_mutex;
            const std::lock_guard<std::mutex> lock(progress_mutex);
            std::cerr << "curriculum-long-label " << name << ' ' << count
                      << '/' << source.size() << '\n';
          }
        }
      } catch (...) {
        const std::lock_guard<std::mutex> lock(exception_mutex);
        if (exception == nullptr) exception = std::current_exception();
        next.store(source.size());
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  if (exception != nullptr) std::rethrow_exception(exception);
  for (const RootLabel& root : result.roots) addRootCost(root, result.cost);
  result.cost.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  if (result.cost.roots != source.size() ||
      result.cost.legal_actions < result.cost.roots ||
      result.cost.d1_work > result.cost.d1_calls * 70) {
    throw std::runtime_error("long-outcome labeled range incomplete");
  }
  deadline.check();
  enforceRssLimit();
  return result;
}

struct SparseFeature {
  std::uint16_t index = 0;
  float value = 0.0f;

  bool operator==(const SparseFeature&) const = default;
};

struct SparseInput {
  std::array<SparseFeature, kActiveInputs> values{};
  int count = 0;

  void add(int index, double value) {
    if (index < 0 || index >= kInputs || count >= kActiveInputs ||
        !std::isfinite(value)) {
      throw std::runtime_error("sparse NNUE input invariant failed");
    }
    values[count++] = {static_cast<std::uint16_t>(index),
                       static_cast<float>(value)};
  }

  bool operator==(const SparseInput&) const = default;
};

double signedTanh(double value, double scale) {
  return std::tanh(value / scale);
}

SparseInput makeInput(const CorpusRecord& record,
                      const ActionLabel& action_label, int action) {
  const StateAction tuple = canonicalStateAction(record.state, action);
  SparseInput result;
  for (int cell = 0; cell < kCellCount; ++cell) {
    const int token = tuple.state.board[cell];
    if (token < 0 || token >= kBoardTokens) {
      throw std::runtime_error("NNUE board token out of range");
    }
    result.add(cell * kBoardTokens + token, 1.0);
  }
  constexpr int next_base = kBoardInputs;
  constexpr int phase_base = next_base + kNextInputs;
  constexpr int action_base = phase_base + kPhaseInputs;
  constexpr int aggregate_base = action_base + kActionInputs;
  result.add(next_base + tuple.state.next_disc - 1, 1.0);
  result.add(phase_base + tuple.state.moves_remaining - 1, 1.0);
  result.add(action_base + tuple.action, 1.0);

  const detail::PhaseFeatures certificate =
      detail::extractPhaseFeatures(materialize(tuple.state));
  const FlowAggregates& flow = record.flow;
  const ActionCertificate& action_value = action_label.certificate;
  const std::array<double, kAggregateInputs> aggregates{{
      flow.mean_moves / 25.0,
      flow.survival,
      positiveTanh(flow.mean_score, 90'000.0),
      positiveTanh(flow.clears_per_move, 3.0),
      positiveTanh(flow.reveals_per_move, 2.0),
      std::tanh(flow.mean_maximum_chain / 10.0),
      static_cast<double>(flow.band) / 3.0,
      static_cast<double>(flow.occupancy) / kCellCount,
      static_cast<double>(flow.maximum_height) / kBoardSize,
      static_cast<double>(flow.covers) / kCellCount,
      static_cast<double>(flow.legal_columns) / kBoardSize,
      std::tanh(certificate.direct_potential / 12.0),
      std::tanh(certificate.latent_chain_potential / 12.0),
      std::tanh(certificate.cracked_exposure / 12.0),
      std::tanh(certificate.solid_exposure / 12.0),
      std::tanh(certificate.adjacent_ones / 6.0),
      std::tanh(certificate.triple_twos / 6.0),
      std::tanh(certificate.dead_low_numbers / 12.0),
      std::tanh(certificate.quiet_build_options / 7.0),
      std::tanh(certificate.trigger_readiness / 12.0),
      action_value.terminal_rate,
      positiveTanh(action_value.mean_score, 30'000.0),
      positiveTanh(action_value.mean_clears, 12.0),
      positiveTanh(action_value.mean_reveals, 8.0),
      positiveTanh(action_value.mean_waves, 8.0),
      positiveTanh(action_value.mean_maximum_chain, 8.0),
      action_value.post_occupancy / kCellCount,
      action_value.post_height / kBoardSize,
      action_value.post_covers / kCellCount,
      signedTanh(action_value.direct_delta, 6.0),
      signedTanh(action_value.latent_delta, 6.0),
      signedTanh(action_value.clog_improvement, 6.0),
  }};
  for (int index = 0; index < kAggregateInputs; ++index) {
    result.add(aggregate_base + index, aggregates[index]);
  }
  if (result.count != kActiveInputs) {
    throw std::runtime_error("sparse NNUE active-input count changed");
  }
  return result;
}

struct PreparedAction {
  bool legal = false;
  SparseInput input{};
};

struct PreparedRoot {
  const RootLabel* label = nullptr;
  std::array<PreparedAction, kBoardSize> actions{};
};

std::vector<PreparedRoot> prepare(const std::vector<RootLabel>& labels) {
  std::vector<PreparedRoot> result(labels.size());
  for (std::size_t root = 0; root < labels.size(); ++root) {
    result[root].label = &labels[root];
    for (int action = 0; action < kBoardSize; ++action) {
      if (!labels[root].actions[action].legal) continue;
      result[root].actions[action].legal = true;
      result[root].actions[action].input = makeInput(
          labels[root].record, labels[root].actions[action], action);
    }
  }
  return result;
}

struct Network {
  std::vector<float> input_weights;
  std::array<float, kHidden> hidden_bias{};
  std::array<float, kHeads * kHidden> head_weights{};
  std::array<float, kHeads> head_bias{};

  Network()
      : input_weights(static_cast<std::size_t>(kInputs * kHidden)) {}
};

double randomSigned(std::uint32_t& state) {
  state = mix32(state + 0x9e37'79b9u);
  const double unit =
      static_cast<double>(state >> 8u) / static_cast<double>(1u << 24u);
  return 2.0 * unit - 1.0;
}

Network initializeNetwork() {
  Network result;
  std::uint32_t random = kInitializationDomain;
  for (float& value : result.input_weights) {
    value = static_cast<float>(0.025 * randomSigned(random));
  }
  result.hidden_bias.fill(0.025f);
  for (float& value : result.head_weights) {
    value = static_cast<float>(0.05 * randomSigned(random));
  }
  return result;
}

struct Forward {
  std::array<double, kHidden> z{};
  std::array<double, kHidden> hidden{};
  std::array<double, kHeads> heads{};
};

Forward forward(const Network& network, const SparseInput& input) {
  Forward result;
  for (int hidden = 0; hidden < kHidden; ++hidden) {
    result.z[hidden] = network.hidden_bias[hidden];
  }
  for (int feature = 0; feature < input.count; ++feature) {
    const int index = input.values[feature].index;
    const double value = input.values[feature].value;
    const float* weights =
        network.input_weights.data() + index * kHidden;
    for (int hidden = 0; hidden < kHidden; ++hidden) {
      result.z[hidden] += weights[hidden] * value;
    }
  }
  for (int hidden = 0; hidden < kHidden; ++hidden) {
    result.hidden[hidden] = std::clamp(result.z[hidden], 0.0, 1.0);
  }
  for (int head = 0; head < kHeads; ++head) {
    result.heads[head] = network.head_bias[head];
    for (int hidden = 0; hidden < kHidden; ++hidden) {
      result.heads[head] +=
          network.head_weights[head * kHidden + hidden] *
          result.hidden[hidden];
    }
  }
  return result;
}

struct Gradient {
  std::vector<double> input_weights;
  std::array<double, kHidden> hidden_bias{};
  std::array<double, kHeads * kHidden> head_weights{};
  std::array<double, kHeads> head_bias{};

  Gradient()
      : input_weights(static_cast<std::size_t>(kInputs * kHidden)) {}
};

void backpropagate(const Network& network, const SparseInput& input,
                   const Forward& computed,
                   const std::array<double, kHeads>& head_gradient,
                   Gradient& gradient) {
  std::array<double, kHidden> hidden_gradient{};
  for (int head = 0; head < kHeads; ++head) {
    gradient.head_bias[head] += head_gradient[head];
    for (int hidden = 0; hidden < kHidden; ++hidden) {
      gradient.head_weights[head * kHidden + hidden] +=
          head_gradient[head] * computed.hidden[hidden];
      hidden_gradient[hidden] +=
          head_gradient[head] *
          network.head_weights[head * kHidden + hidden];
    }
  }
  for (int hidden = 0; hidden < kHidden; ++hidden) {
    if (computed.z[hidden] <= 0.0 || computed.z[hidden] >= 1.0) {
      hidden_gradient[hidden] = 0.0;
    }
    gradient.hidden_bias[hidden] += hidden_gradient[hidden];
  }
  for (int feature = 0; feature < input.count; ++feature) {
    const int index = input.values[feature].index;
    const double value = input.values[feature].value;
    double* weights = gradient.input_weights.data() + index * kHidden;
    for (int hidden = 0; hidden < kHidden; ++hidden) {
      weights[hidden] += hidden_gradient[hidden] * value;
    }
  }
}

struct AdamState {
  Gradient first{};
  Gradient second{};
  std::uint64_t step = 0;
};

void updateParameter(float& parameter, double gradient, double& first,
                     double& second, std::uint64_t step,
                     double learning_rate) {
  first = 0.9 * first + 0.1 * gradient;
  second = 0.999 * second + 0.001 * gradient * gradient;
  const double corrected_first = first / (1.0 - std::pow(0.9, step));
  const double corrected_second = second / (1.0 - std::pow(0.999, step));
  parameter -= static_cast<float>(
      learning_rate * corrected_first /
      (std::sqrt(corrected_second) + 1.0e-8));
}

void applyAdam(Network& network, Gradient& gradient, AdamState& adam,
               std::uint64_t rows, std::uint64_t update,
               std::uint64_t total_updates) {
  if (rows == 0 || update < 1 || update > total_updates) {
    throw std::runtime_error("invalid Adam batch");
  }
  ++adam.step;
  const double progress =
      static_cast<double>(update - 1) /
      static_cast<double>(std::max<std::uint64_t>(1, total_updates - 1));
  const double cosine = 0.5 * (1.0 + std::cos(std::numbers::pi * progress));
  const double learning_rate =
      kLearningRate *
      (kFinalLearningRateRatio + (1.0 - kFinalLearningRateRatio) * cosine);
  const double inverse = 1.0 / static_cast<double>(rows);
  for (std::size_t index = 0; index < network.input_weights.size(); ++index) {
    const double value = gradient.input_weights[index] * inverse +
                         kWeightDecay * network.input_weights[index];
    updateParameter(network.input_weights[index], value,
                    adam.first.input_weights[index],
                    adam.second.input_weights[index], adam.step,
                    learning_rate);
  }
  for (int hidden = 0; hidden < kHidden; ++hidden) {
    updateParameter(network.hidden_bias[hidden],
                    gradient.hidden_bias[hidden] * inverse,
                    adam.first.hidden_bias[hidden],
                    adam.second.hidden_bias[hidden], adam.step,
                    learning_rate);
  }
  for (int index = 0; index < kHeads * kHidden; ++index) {
    const double value = gradient.head_weights[index] * inverse +
                         kWeightDecay * network.head_weights[index];
    updateParameter(network.head_weights[index], value,
                    adam.first.head_weights[index],
                    adam.second.head_weights[index], adam.step,
                    learning_rate);
  }
  for (int head = 0; head < kHeads; ++head) {
    updateParameter(network.head_bias[head],
                    gradient.head_bias[head] * inverse,
                    adam.first.head_bias[head],
                    adam.second.head_bias[head], adam.step,
                    learning_rate);
  }
}

void shuffleIndices(std::vector<std::size_t>& values, int epoch) {
  std::uint32_t random =
      mix32(kInitializationDomain ^ static_cast<std::uint32_t>(epoch));
  for (std::size_t end = values.size(); end > 1; --end) {
    random = mix32(random + 0x9e37'79b9u);
    const std::size_t other = random % end;
    std::swap(values[end - 1], values[other]);
  }
}

struct TrainingSummary {
  Network network{};
  std::uint64_t updates = 0;
  std::uint64_t rows = 0;
  std::uint64_t pairs = 0;
  double seconds = 0.0;
};

TrainingSummary train(const std::vector<PreparedRoot>& roots,
                      const Deadline& deadline) {
  if (roots.size() != kTrainingStates) {
    throw std::runtime_error("NNUE training split size changed");
  }
  const auto started = Clock::now();
  TrainingSummary result;
  result.network = initializeNetwork();
  AdamState adam;
  std::vector<std::size_t> order(roots.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  const std::uint64_t batches_per_epoch =
      (roots.size() + kBatchRoots - 1) / kBatchRoots;
  const std::uint64_t total_updates = kEpochs * batches_per_epoch;
  for (int epoch = 0; epoch < kEpochs; ++epoch) {
    shuffleIndices(order, epoch);
    for (std::size_t begin = 0; begin < roots.size(); begin += kBatchRoots) {
      deadline.check();
      Gradient gradient;
      std::uint64_t rows = 0;
      std::uint64_t pairs = 0;
      const std::size_t end =
          std::min(roots.size(), begin + static_cast<std::size_t>(kBatchRoots));
      for (std::size_t offset = begin; offset < end; ++offset) {
        const PreparedRoot& root = roots[order[offset]];
        std::array<Forward, kBoardSize> computed{};
        std::array<std::array<double, kHeads>, kBoardSize> head_gradient{};
        int legal = 0;
        for (int action = 0; action < kBoardSize; ++action) {
          if (!root.actions[action].legal) continue;
          ++legal;
          ++rows;
          computed[action] =
              forward(result.network, root.actions[action].input);
          const auto& target = root.label->actions[action].targets;
          for (int head = 0; head < kHeads; ++head) {
            head_gradient[action][head] =
                2.0 * kHeadLossWeights[head] *
                (computed[action].heads[head] - target[head]);
          }
        }
        int root_pairs = 0;
        for (int first = 0; first < kBoardSize; ++first) {
          if (!root.actions[first].legal) continue;
          for (int second = first + 1; second < kBoardSize; ++second) {
            if (!root.actions[second].legal) continue;
            const double first_target =
                root.label->actions[first].normalized_target;
            const double second_target =
                root.label->actions[second].normalized_target;
            root_pairs += std::abs(first_target - second_target) > 1.0e-12;
          }
        }
        pairs += root_pairs;
        if (root_pairs > 0) {
          const double pair_scale =
              kPairwiseLossWeight * legal / root_pairs;
          for (int first = 0; first < kBoardSize; ++first) {
            if (!root.actions[first].legal) continue;
            for (int second = first + 1; second < kBoardSize; ++second) {
              if (!root.actions[second].legal) continue;
              const double first_target =
                  root.label->actions[first].normalized_target;
              const double second_target =
                  root.label->actions[second].normalized_target;
              if (std::abs(first_target - second_target) <= 1.0e-12) continue;
              const int better = first_target > second_target ? first : second;
              const int worse = first_target > second_target ? second : first;
              const double better_score =
                  root.label->actions[better].normalized_d1_q +
                  computed[better].heads[0];
              const double worse_score =
                  root.label->actions[worse].normalized_d1_q +
                  computed[worse].heads[0];
              const double scaled = std::clamp(
                  (better_score - worse_score) / kPairwiseTemperature,
                  -40.0, 40.0);
              const double derivative =
                  -pair_scale /
                  (kPairwiseTemperature * (1.0 + std::exp(scaled)));
              head_gradient[better][0] += derivative;
              head_gradient[worse][0] -= derivative;
            }
          }
        }
        for (int action = 0; action < kBoardSize; ++action) {
          if (!root.actions[action].legal) continue;
          backpropagate(result.network, root.actions[action].input,
                        computed[action], head_gradient[action], gradient);
        }
      }
      ++result.updates;
      result.rows += rows;
      result.pairs += pairs;
      applyAdam(result.network, gradient, adam, rows, result.updates,
                total_updates);
    }
    enforceRssLimit();
    std::cerr << "curriculum-nnue epoch " << epoch + 1 << '/' << kEpochs
              << '\n';
  }
  if (result.updates != total_updates) {
    throw std::runtime_error("NNUE Adam update schedule incomplete");
  }
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

struct Ranking {
  std::uint64_t roots = 0;
  std::uint64_t top1 = 0;
  std::uint64_t pairs = 0;
  double pairwise_credit = 0.0;
  double normalized_regret = 0.0;
};

double top1Rate(const Ranking& value) {
  return value.roots > 0
             ? static_cast<double>(value.top1) / value.roots
             : 0.0;
}

double pairwiseRate(const Ranking& value) {
  return value.pairs > 0 ? value.pairwise_credit / value.pairs : 0.0;
}

double regret(const Ranking& value) {
  return value.roots > 0 ? value.normalized_regret / value.roots : 0.0;
}

int selectAction(const RootLabel& root,
                 const std::array<double, kBoardSize>& scores) {
  int selected = -1;
  double best = -std::numeric_limits<double>::infinity();
  for (const int action : kActionOrder) {
    if (!root.actions[action].legal) continue;
    if (scores[action] > best) {
      best = scores[action];
      selected = action;
    }
  }
  return selected;
}

void observeRanking(const RootLabel& root,
                    const std::array<double, kBoardSize>& scores,
                    Ranking& result) {
  const int selected = selectAction(root, scores);
  if (selected < 0) throw std::runtime_error("ranking has no legal action");
  ++result.roots;
  double best = -std::numeric_limits<double>::infinity();
  double minimum = std::numeric_limits<double>::infinity();
  for (int action = 0; action < kBoardSize; ++action) {
    if (!root.actions[action].legal) continue;
    best = std::max(best, root.actions[action].normalized_target);
    minimum = std::min(minimum, root.actions[action].normalized_target);
  }
  result.top1 +=
      best - root.actions[selected].normalized_target <= 1.0e-12;
  const double range = best - minimum;
  if (range > 1.0e-12) {
    result.normalized_regret +=
        (best - root.actions[selected].normalized_target) / range;
  }
  for (int first = 0; first < kBoardSize; ++first) {
    if (!root.actions[first].legal) continue;
    for (int second = first + 1; second < kBoardSize; ++second) {
      if (!root.actions[second].legal) continue;
      const double target_difference =
          root.actions[first].normalized_target -
          root.actions[second].normalized_target;
      if (std::abs(target_difference) <= 1.0e-12) continue;
      ++result.pairs;
      const double prediction_difference = scores[first] - scores[second];
      if (std::abs(prediction_difference) <= 1.0e-12) {
        result.pairwise_credit += 0.5;
      } else {
        result.pairwise_credit +=
            (prediction_difference > 0.0) == (target_difference > 0.0);
      }
    }
  }
}

struct Evaluation {
  Ranking baseline{};
  Ranking candidate{};
  std::array<Ranking, 2> baseline_halves{};
  std::array<Ranking, 2> candidate_halves{};
  std::array<double, kHeads> squared_error{};
  std::uint64_t rows = 0;
};

Evaluation evaluate(const Network& network,
                    const std::vector<PreparedRoot>& roots) {
  Evaluation result;
  for (const PreparedRoot& prepared : roots) {
    const RootLabel& root = *prepared.label;
    std::array<double, kBoardSize> baseline{};
    std::array<double, kBoardSize> candidate{};
    baseline.fill(-std::numeric_limits<double>::infinity());
    candidate.fill(-std::numeric_limits<double>::infinity());
    for (int action = 0; action < kBoardSize; ++action) {
      if (!prepared.actions[action].legal) continue;
      const Forward computed = forward(network, prepared.actions[action].input);
      baseline[action] = root.actions[action].normalized_d1_q;
      candidate[action] = baseline[action] + computed.heads[0];
      ++result.rows;
      for (int head = 0; head < kHeads; ++head) {
        const double error =
            computed.heads[head] - root.actions[action].targets[head];
        result.squared_error[head] += error * error;
      }
    }
    observeRanking(root, baseline, result.baseline);
    observeRanking(root, candidate, result.candidate);
    observeRanking(root, baseline,
                   result.baseline_halves[root.record.hash_half]);
    observeRanking(root, candidate,
                   result.candidate_halves[root.record.hash_half]);
  }
  return result;
}

bool nonregressed(const Ranking& candidate, const Ranking& baseline) {
  return top1Rate(candidate) + 1.0e-12 >= top1Rate(baseline) &&
         pairwiseRate(candidate) + 1.0e-12 >= pairwiseRate(baseline) &&
         regret(candidate) <= regret(baseline) + 1.0e-12;
}

struct Gate {
  bool top1_gain = false;
  bool pairwise_gain = false;
  bool regret_gain = false;
  bool absolute_top1 = false;
  bool absolute_pairwise = false;
  std::array<bool, 2> half_nonregression{};
  bool passed = false;
};

Gate applyGate(const Evaluation& heldout) {
  Gate result;
  result.top1_gain =
      top1Rate(heldout.candidate) >=
      top1Rate(heldout.baseline) + kGateTop1Gain;
  result.pairwise_gain =
      pairwiseRate(heldout.candidate) >=
      pairwiseRate(heldout.baseline) + kGatePairwiseGain;
  result.regret_gain =
      regret(heldout.candidate) <=
      kGateRegretRatio * regret(heldout.baseline);
  result.absolute_top1 =
      top1Rate(heldout.candidate) >= kGateAbsoluteTop1;
  result.absolute_pairwise =
      pairwiseRate(heldout.candidate) >= kGateAbsolutePairwise;
  for (int half = 0; half < 2; ++half) {
    result.half_nonregression[half] = nonregressed(
        heldout.candidate_halves[half], heldout.baseline_halves[half]);
  }
  result.passed = result.top1_gain && result.pairwise_gain &&
                  result.regret_gain && result.absolute_top1 &&
                  result.absolute_pairwise &&
                  result.half_nonregression[0] &&
                  result.half_nonregression[1];
  return result;
}

std::string jsonEscape(std::string_view source) {
  std::string result;
  for (const char character : source) {
    if (character == '\\' || character == '"') result.push_back('\\');
    result.push_back(character);
  }
  return result;
}

std::string hex64(std::uint64_t value) {
  std::ostringstream output;
  output << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
  return output.str();
}

void writeBoard(std::ostream& output, const Board& board) {
  for (const std::uint8_t cell : board) {
    output << static_cast<char>('0' + cell);
  }
}

void writeLabelRoot(std::ostream& output, const RootLabel& root,
                    std::string_view split) {
  output << std::setprecision(12) << "{\"split\":\"" << split
         << "\",\"publicHash\":\"" << hex64(root.record.public_hash)
         << "\",\"hashHalf\":" << root.record.hash_half
         << ",\"state\":{\"board\":\"";
  writeBoard(output, root.record.state.board);
  output << "\",\"nextDisc\":"
         << static_cast<int>(root.record.state.next_disc)
         << ",\"movesRemaining\":"
         << static_cast<int>(root.record.state.moves_remaining)
         << "},\"d1Action\":" << root.d1_action
         << ",\"optimalAction\":" << root.labeled_action
         << ",\"actions\":[";
  for (int action = 0; action < kBoardSize; ++action) {
    if (action != 0) output << ',';
    const ActionLabel& value = root.actions[action];
    if (!value.legal) {
      output << "null";
      continue;
    }
    output << "{\"action\":" << action
           << ",\"survival\":" << value.survival
           << ",\"cumulativeCorrectedScore\":" << value.mean_score
           << ",\"clears\":" << value.mean_clears
           << ",\"reveals\":" << value.mean_reveals
           << ",\"balancedReturn\":" << value.balanced_return
           << ",\"normalizedTarget\":" << value.normalized_target
           << ",\"normalizedD1Q\":" << value.normalized_d1_q
           << ",\"certificate\":{\"terminalRate\":"
           << value.certificate.terminal_rate
           << ",\"meanScore\":" << value.certificate.mean_score
           << ",\"meanClears\":" << value.certificate.mean_clears
           << ",\"meanReveals\":" << value.certificate.mean_reveals
           << ",\"meanWaves\":" << value.certificate.mean_waves
           << ",\"meanMaximumChain\":"
           << value.certificate.mean_maximum_chain
           << ",\"postOccupancy\":"
           << value.certificate.post_occupancy
           << ",\"postMaximumHeight\":"
           << value.certificate.post_height
           << ",\"postCovers\":" << value.certificate.post_covers
           << ",\"postLegalColumns\":"
           << value.certificate.post_legal_columns
           << ",\"directDelta\":" << value.certificate.direct_delta
           << ",\"latentDelta\":" << value.certificate.latent_delta
           << ",\"clogImprovement\":"
           << value.certificate.clog_improvement << "},\"scenarios\":[";
    for (int scenario = 0; scenario < kScenarios; ++scenario) {
      if (scenario != 0) output << ',';
      const ScenarioLabel& item = value.scenarios[scenario];
      output << "{\"moves\":" << item.moves
             << ",\"cumulativeCorrectedScore\":" << item.score
             << ",\"clears\":" << item.clears
             << ",\"reveals\":" << item.reveals
             << ",\"maximumChain\":" << item.maximum_chain
             << ",\"survived\":"
             << (item.survived ? "true" : "false") << '}';
    }
    output << "]}";
  }
  output << "],\"transitions\":" << root.transitions
         << ",\"d1Calls\":" << root.d1_calls
         << ",\"d1Work\":" << root.d1_work
         << ",\"d1Nodes\":" << root.d1_nodes
         << ",\"seconds\":" << root.seconds << "}\n";
}

void writeLabels(const Options& options, const LabeledRange& training,
                 const LabeledRange& heldout) {
  std::ofstream output(options.labels, std::ios::trunc);
  if (!output) throw std::runtime_error("could not write NNUE labels");
  output << std::setprecision(12)
         << "{\"type\":\"metadata\",\"format\":\"drop7-curriculum-public-long-outcomes-v1\""
         << ",\"sourceSha256\":\"" << kExpectedCorpusSha256 << "\""
         << ",\"sourceFingerprint\":\""
         << hex64(kExpectedDatasetFingerprint) << "\""
         << ",\"newGameplaySeeds\":0,\"publicStatesOnly\":true"
         << ",\"splitBeforeLabels\":true,\"trainingStates\":"
         << training.roots.size() << ",\"heldoutStates\":"
         << heldout.roots.size() << ",\"scenarios\":" << kScenarios
         << ",\"horizon\":" << kHorizon
         << ",\"continuation\":\"exact fair-D1\""
         << ",\"returnWeights\":{\"survival\":"
         << kBalancedSurvivalWeight << ",\"score\":"
         << kBalancedScoreWeight << ",\"clears\":"
         << kBalancedClearsWeight << ",\"reveals\":"
         << kBalancedRevealsWeight << "}}\n";
  for (const RootLabel& root : training.roots) {
    writeLabelRoot(output, root, "training");
  }
  for (const RootLabel& root : heldout.roots) {
    writeLabelRoot(output, root, "heldout");
  }
  output.close();
  if (!output) throw std::runtime_error("failed finishing NNUE labels");
}

template <typename Value>
void writeBinary(std::ostream& output, const Value& value) {
  static_assert(std::is_trivially_copyable_v<Value>);
  output.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

template <typename Value>
void readBinary(std::istream& input, Value& value) {
  static_assert(std::is_trivially_copyable_v<Value>);
  input.read(reinterpret_cast<char*>(&value), sizeof(value));
}

void writeCheckpoint(const std::string& path, const Network& network) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("could not write NNUE checkpoint");
  constexpr std::array<char, 8> magic{{'D', '7', 'C', 'L', 'N', 'N', '1', '\0'}};
  output.write(magic.data(), magic.size());
  constexpr std::uint32_t version = 1;
  constexpr std::uint32_t inputs = kInputs;
  constexpr std::uint32_t hidden = kHidden;
  constexpr std::uint32_t heads = kHeads;
  constexpr std::uint32_t level_bonus = kLevelBonus;
  writeBinary(output, version);
  writeBinary(output, inputs);
  writeBinary(output, hidden);
  writeBinary(output, heads);
  writeBinary(output, level_bonus);
  writeBinary(output, kExpectedDatasetFingerprint);
  output.write(kExpectedCorpusSha256.data(), kExpectedCorpusSha256.size());
  output.write(reinterpret_cast<const char*>(network.input_weights.data()),
               static_cast<std::streamsize>(network.input_weights.size() *
                                            sizeof(float)));
  output.write(reinterpret_cast<const char*>(network.hidden_bias.data()),
               sizeof(network.hidden_bias));
  output.write(reinterpret_cast<const char*>(network.head_weights.data()),
               sizeof(network.head_weights));
  output.write(reinterpret_cast<const char*>(network.head_bias.data()),
               sizeof(network.head_bias));
  output.close();
  if (!output) throw std::runtime_error("failed finishing NNUE checkpoint");
  std::error_code error;
  const std::uintmax_t bytes = std::filesystem::file_size(path, error);
  if (error || bytes > kCheckpointLimitBytes) {
    throw std::runtime_error("NNUE checkpoint exceeded 1 MiB cap");
  }
}

Network readCheckpoint(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not read NNUE checkpoint");
  std::array<char, 8> magic{};
  input.read(magic.data(), magic.size());
  constexpr std::array<char, 8> expected{{'D', '7', 'C', 'L', 'N', 'N', '1', '\0'}};
  std::uint32_t version = 0;
  std::uint32_t inputs = 0;
  std::uint32_t hidden = 0;
  std::uint32_t heads = 0;
  std::uint32_t level_bonus = 0;
  std::uint64_t fingerprint = 0;
  readBinary(input, version);
  readBinary(input, inputs);
  readBinary(input, hidden);
  readBinary(input, heads);
  readBinary(input, level_bonus);
  readBinary(input, fingerprint);
  std::array<char, 64> checksum{};
  input.read(checksum.data(), checksum.size());
  if (magic != expected || version != 1 || inputs != kInputs ||
      hidden != kHidden || heads != kHeads || level_bonus != kLevelBonus ||
      fingerprint != kExpectedDatasetFingerprint ||
      std::string_view(checksum.data(), checksum.size()) !=
          kExpectedCorpusSha256) {
    throw std::runtime_error("NNUE checkpoint header mismatch");
  }
  Network result;
  input.read(reinterpret_cast<char*>(result.input_weights.data()),
             static_cast<std::streamsize>(result.input_weights.size() *
                                          sizeof(float)));
  input.read(reinterpret_cast<char*>(result.hidden_bias.data()),
             sizeof(result.hidden_bias));
  input.read(reinterpret_cast<char*>(result.head_weights.data()),
             sizeof(result.head_weights));
  input.read(reinterpret_cast<char*>(result.head_bias.data()),
             sizeof(result.head_bias));
  if (!input) throw std::runtime_error("NNUE checkpoint truncated");
  char trailing = 0;
  if (input.read(&trailing, 1)) {
    throw std::runtime_error("NNUE checkpoint has trailing data");
  }
  return result;
}

void writeRanking(std::ostream& output, const Ranking& value) {
  output << std::setprecision(12) << "{\"roots\":" << value.roots
         << ",\"top1\":" << top1Rate(value)
         << ",\"pairs\":" << value.pairs
         << ",\"pairwise\":" << pairwiseRate(value)
         << ",\"normalizedRegret\":" << regret(value) << '}';
}

void writeCost(std::ostream& output, const LabelCost& value) {
  output << std::setprecision(12) << "{\"roots\":" << value.roots
         << ",\"legalActions\":" << value.legal_actions
         << ",\"transitions\":" << value.transitions
         << ",\"d1Calls\":" << value.d1_calls
         << ",\"d1Work\":" << value.d1_work
         << ",\"d1Nodes\":" << value.d1_nodes
         << ",\"aggregateRootSeconds\":"
         << value.aggregate_root_seconds << ",\"wallSeconds\":"
         << value.wall_seconds << '}';
}

void writeEvaluation(std::ostream& output, const Evaluation& value) {
  output << "{\"d1\":";
  writeRanking(output, value.baseline);
  output << ",\"nnue\":";
  writeRanking(output, value.candidate);
  output << ",\"hashHalves\":[";
  for (int half = 0; half < 2; ++half) {
    if (half != 0) output << ',';
    output << "{\"half\":" << half << ",\"d1\":";
    writeRanking(output, value.baseline_halves[half]);
    output << ",\"nnue\":";
    writeRanking(output, value.candidate_halves[half]);
    output << '}';
  }
  output << "],\"headRmse\":[";
  for (int head = 0; head < kHeads; ++head) {
    if (head != 0) output << ',';
    output << (value.rows > 0
                   ? std::sqrt(value.squared_error[head] / value.rows)
                   : 0.0);
  }
  output << "]}";
}

struct LabelMoments {
  std::uint64_t actions = 0;
  double survival = 0.0;
  double score = 0.0;
  double clears = 0.0;
  double reveals = 0.0;
  double balanced = 0.0;
};

LabelMoments labelMoments(const LabeledRange& range) {
  LabelMoments result;
  for (const RootLabel& root : range.roots) {
    for (const ActionLabel& action : root.actions) {
      if (!action.legal) continue;
      ++result.actions;
      result.survival += action.survival;
      result.score += action.mean_score;
      result.clears += action.mean_clears;
      result.reveals += action.mean_reveals;
      result.balanced += action.balanced_return;
    }
  }
  if (result.actions > 0) {
    const double inverse = 1.0 / result.actions;
    result.survival *= inverse;
    result.score *= inverse;
    result.clears *= inverse;
    result.reveals *= inverse;
    result.balanced *= inverse;
  }
  return result;
}

void writeArtifact(const Options& options, const Corpus& corpus,
                   const LabeledRange& training_labels,
                   const LabeledRange& heldout_labels,
                   const TrainingSummary& training,
                   const Evaluation& fitting,
                   const Evaluation& heldout, const Gate& gate,
                   std::string_view label_sha, std::string_view checkpoint_sha,
                   std::uintmax_t checkpoint_bytes, double total_seconds) {
  std::ofstream output(options.output, std::ios::trunc);
  if (!output) throw std::runtime_error("could not write NNUE artifact");
  const LabelMoments training_moments = labelMoments(training_labels);
  const LabelMoments heldout_moments = labelMoments(heldout_labels);
  output << std::setprecision(12)
         << "{\n  \"format\":\"drop7-curriculum-long-outcome-nnue-v1\",\n"
         << "  \"purpose\":\"artifact-only diagnostic; potential rollout/value initializer only; no gameplay claim\",\n"
         << "  \"source\":{\"path\":\"" << jsonEscape(options.states)
         << "\",\"bytes\":" << kExpectedCorpusBytes
         << ",\"sha256\":\"" << kExpectedCorpusSha256
         << "\",\"datasetFingerprint\":\""
         << hex64(kExpectedDatasetFingerprint)
         << "\",\"publicStates\":" << kCorpusStates
         << ",\"checksumValidated\":true,\"newGameplaySeeds\":0},\n"
         << "  \"split\":{\"method\":\"rank mix64(canonicalPublicHash xor frozen domain) before labels\",\"training\":"
         << corpus.training.size() << ",\"heldout\":"
         << corpus.heldout.size() << ",\"trainingHashHalves\":["
         << corpus.training_halves[0] << ',' << corpus.training_halves[1]
         << "],\"heldoutHashHalves\":[" << corpus.heldout_halves[0]
         << ',' << corpus.heldout_halves[1] << "]},\n"
         << "  \"labels\":{\"path\":\"" << jsonEscape(options.labels)
         << "\",\"sha256\":\"" << label_sha
         << "\",\"scenarios\":" << kScenarios
         << ",\"horizon\":" << kHorizon
         << ",\"rootAction\":\"forced\",\"continuation\":\"exact public fair-D1\",\"commonEventIndexedChance\":true,\"correctedLevelBonus\":"
         << kLevelBonus << ",\"balancedReturn\":{\"survival\":"
         << kBalancedSurvivalWeight << ",\"cumulativeScore\":"
         << kBalancedScoreWeight << ",\"clears\":"
         << kBalancedClearsWeight << ",\"reveals\":"
         << kBalancedRevealsWeight << "},\"trainingMeans\":{\"actions\":"
         << training_moments.actions << ",\"survival\":"
         << training_moments.survival << ",\"score\":"
         << training_moments.score << ",\"clears\":"
         << training_moments.clears << ",\"reveals\":"
         << training_moments.reveals << ",\"balanced\":"
         << training_moments.balanced
         << "},\"heldoutMeans\":{\"actions\":"
         << heldout_moments.actions << ",\"survival\":"
         << heldout_moments.survival << ",\"score\":"
         << heldout_moments.score << ",\"clears\":"
         << heldout_moments.clears << ",\"reveals\":"
         << heldout_moments.reveals << ",\"balanced\":"
         << heldout_moments.balanced << "},\"trainingCost\":";
  writeCost(output, training_labels.cost);
  output << ",\"heldoutCost\":";
  writeCost(output, heldout_labels.cost);
  output << "},\n  \"model\":{\"architecture\":\"sparse exact-tuple-reflection 541x96 clipped-ReLU, five heads\",\"boardOneHot\":490,\"nextDiscOneHot\":7,\"risePhaseOneHot\":5,\"actionOneHot\":7,\"certificateFlowAggregates\":32,\"anchor\":\"within-root normalized exact D1 Q\",\"heads\":[\"balancedReturnResidual\",\"survival\",\"cumulativeScore\",\"clears\",\"reveals\"],\"epochs\":"
         << kEpochs << ",\"batchRoots\":" << kBatchRoots
         << ",\"learningRate\":" << kLearningRate
         << ",\"finalLearningRateRatio\":"
         << kFinalLearningRateRatio << ",\"weightDecay\":"
         << kWeightDecay << ",\"pairwiseLossWeight\":"
         << kPairwiseLossWeight << ",\"updates\":" << training.updates
         << ",\"rows\":" << training.rows << ",\"pairs\":"
         << training.pairs << ",\"seconds\":" << training.seconds
         << ",\"checkpoint\":\"" << jsonEscape(options.checkpoint)
         << "\",\"checkpointBytes\":" << checkpoint_bytes
         << ",\"checkpointSha256\":\"" << checkpoint_sha
         << "\"},\n  \"fitting\":";
  writeEvaluation(output, fitting);
  output << ",\n  \"heldout\":";
  writeEvaluation(output, heldout);
  output << ",\n  \"gates\":{\"top1GainRequired\":" << kGateTop1Gain
         << ",\"pairwiseGainRequired\":" << kGatePairwiseGain
         << ",\"regretRatioRequired\":" << kGateRegretRatio
         << ",\"absoluteTop1Required\":" << kGateAbsoluteTop1
         << ",\"absolutePairwiseRequired\":"
         << kGateAbsolutePairwise << ",\"top1Gain\":"
         << (gate.top1_gain ? "true" : "false")
         << ",\"pairwiseGain\":"
         << (gate.pairwise_gain ? "true" : "false")
         << ",\"regretGain\":"
         << (gate.regret_gain ? "true" : "false")
         << ",\"absoluteTop1\":"
         << (gate.absolute_top1 ? "true" : "false")
         << ",\"absolutePairwise\":"
         << (gate.absolute_pairwise ? "true" : "false")
         << ",\"hashHalfNonregression\":["
         << (gate.half_nonregression[0] ? "true" : "false") << ','
         << (gate.half_nonregression[1] ? "true" : "false")
         << "],\"passed\":" << (gate.passed ? "true" : "false")
         << "},\n  \"initializerEligible\":"
         << (gate.passed ? "true" : "false")
         << ",\n  \"gameplayPerformed\":false,\n  \"gameplayClaim\":false,\n"
         << "  \"resourceCaps\":{\"wallSeconds\":" << kWallLimitSeconds
         << ",\"rssBytes\":" << kRssLimitBytes
         << ",\"checkpointBytes\":" << kCheckpointLimitBytes
         << "},\n  \"totalSeconds\":" << total_seconds
         << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
  output.close();
  if (!output) throw std::runtime_error("failed finishing NNUE artifact");
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

bool selfTest(const Options& options, std::ostream& output) {
  expect(sha256("abc") ==
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
         "SHA-256 known vector failed");
  const Corpus corpus = loadCorpus(options.states);
  expect(corpus.training.size() == kTrainingStates &&
             corpus.heldout.size() == kHeldoutStates,
         "checksum/split test failed");
  const CorpusRecord& fixture = corpus.training.front();
  State metadata = materialize(fixture.state);
  metadata.score = 9'876'543;
  metadata.level = 87;
  metadata.moves_played = 999;
  expect(publicState(metadata) == fixture.state,
         "hidden metadata crossed public boundary");

  const std::uint32_t key = publicChanceKey(fixture.state);
  for (const std::uint32_t domain : {kRevealDomain, kVisibleDomain}) {
    for (const int event : {0, 1, 127, 128, 12'799}) {
      std::array<int, kScenarios> strata{};
      for (int scenario = 0; scenario < kScenarios; ++scenario) {
        const double unit = detail::stratifiedUnit(
            key, scenario, kScenarios, domain, event);
        const int stratum =
            static_cast<int>(std::floor(unit * kScenarios));
        expect(stratum >= 0 && stratum < kScenarios,
               "chance stratum out of range");
        ++strata[stratum];
      }
      for (const int count : strata) {
        expect(count == 1, "chance event is not exactly stratified");
      }
    }
  }
  int action = -1;
  for (const int candidate : kActionOrder) {
    if (isLegal(fixture.state.board, candidate)) {
      action = candidate;
      break;
    }
  }
  expect(action >= 0, "fixture has no legal action");
  const PublicTransition first =
      playPublicMove(fixture.state, action, key, 0, 0);
  const PublicTransition repeated =
      playPublicMove(fixture.state, action, key, 0, 0);
  const PublicState reflected_state = mirror(fixture.state);
  const PublicTransition reflected = playPublicMove(
      reflected_state, kBoardSize - 1 - action,
      publicChanceKey(reflected_state), 0, 0);
  expect(first == repeated && first == reflected,
         "chance determinism/reflection failed");
  const D1Evaluation d1 = evaluateD1(fixture.state);
  const D1Evaluation reflected_d1 = evaluateD1(reflected_state);
  expect(d1.action == kBoardSize - 1 - reflected_d1.action &&
             isLegal(fixture.state.board, d1.action),
         "fair-D1 reflection failed");

  ActionLabel dummy;
  dummy.legal = true;
  CorpusRecord reflected_record = fixture;
  reflected_record.state = reflected_state;
  const SparseInput direct_input = makeInput(fixture, dummy, action);
  const SparseInput reflected_input = makeInput(
      reflected_record, dummy, kBoardSize - 1 - action);
  const Network network = initializeNetwork();
  expect(direct_input == reflected_input &&
             forward(network, direct_input).heads ==
                 forward(network, reflected_input).heads,
         "NNUE exact reflection failed");
  expect(balancedReturn(1.0, 0.0, 0.0, 0.0) ==
             kBalancedSurvivalWeight &&
             balancedReturn(0.0, 0.0, 0.0, 0.0) == 0.0,
         "balanced return freeze failed");
  expect((kInputs * kHidden + kHidden + kHeads * kHidden + kHeads) *
                 sizeof(float) <
             kCheckpointLimitBytes &&
             peakRssBytes() < kRssLimitBytes,
         "model resource certificate failed");
  enforceRssLimit();
  output << "CURRICULUM_LONG_OUTCOME_NNUE_SELF_TEST {\"passed\":true,"
         << "\"corpusSha256\":\"" << kExpectedCorpusSha256 << "\","
         << "\"states\":" << kCorpusStates
         << ",\"training\":" << kTrainingStates
         << ",\"heldout\":" << kHeldoutStates
         << ",\"metadataBlind\":true,\"chanceExact\":true,"
         << "\"reflectionExact\":true,\"checkpointUnder1MiB\":true,"
         << "\"peakRssBytes\":" << peakRssBytes() << "}\n";
  return true;
}

int run(const Options& options, std::ostream& output) {
  const Deadline deadline;
  const Corpus corpus = loadCorpus(options.states);
  const LabeledRange training_labels =
      labelRange(corpus.training, "training", options.threads, deadline);
  const LabeledRange heldout_labels =
      labelRange(corpus.heldout, "heldout", options.threads, deadline);
  writeLabels(options, training_labels, heldout_labels);
  const std::string label_sha = sha256(readWholeFile(options.labels));
  const std::vector<PreparedRoot> training = prepare(training_labels.roots);
  const std::vector<PreparedRoot> heldout = prepare(heldout_labels.roots);
  const TrainingSummary trained = train(training, deadline);
  const Evaluation fitting_evaluation = evaluate(trained.network, training);
  const Evaluation heldout_evaluation = evaluate(trained.network, heldout);
  const Gate gate = applyGate(heldout_evaluation);
  writeCheckpoint(options.checkpoint, trained.network);
  const Network reloaded = readCheckpoint(options.checkpoint);
  const PreparedRoot& check_root = heldout.front();
  int check_action = -1;
  for (const int action : kActionOrder) {
    if (check_root.actions[action].legal) {
      check_action = action;
      break;
    }
  }
  if (check_action < 0 ||
      forward(trained.network, check_root.actions[check_action].input).heads !=
          forward(reloaded, check_root.actions[check_action].input).heads) {
    throw std::runtime_error("NNUE checkpoint round-trip mismatch");
  }
  const std::string checkpoint_sha =
      sha256(readWholeFile(options.checkpoint));
  std::error_code error;
  const std::uintmax_t checkpoint_bytes =
      std::filesystem::file_size(options.checkpoint, error);
  if (error || checkpoint_bytes > kCheckpointLimitBytes) {
    throw std::runtime_error("NNUE checkpoint resource validation failed");
  }
  deadline.check();
  enforceRssLimit();
  const double total_seconds = deadline.elapsedSeconds();
  writeArtifact(options, corpus, training_labels, heldout_labels, trained,
                fitting_evaluation, heldout_evaluation, gate, label_sha,
                checkpoint_sha, checkpoint_bytes, total_seconds);
  output << std::fixed << std::setprecision(6)
         << "CURRICULUM_LONG_OUTCOME_NNUE_RESULT {\"d1Top1\":"
         << top1Rate(heldout_evaluation.baseline)
         << ",\"nnueTop1\":" << top1Rate(heldout_evaluation.candidate)
         << ",\"d1Pairwise\":"
         << pairwiseRate(heldout_evaluation.baseline)
         << ",\"nnuePairwise\":"
         << pairwiseRate(heldout_evaluation.candidate)
         << ",\"d1Regret\":" << regret(heldout_evaluation.baseline)
         << ",\"nnueRegret\":" << regret(heldout_evaluation.candidate)
         << ",\"hashHalvesNonregress\":["
         << (gate.half_nonregression[0] ? "true" : "false") << ','
         << (gate.half_nonregression[1] ? "true" : "false")
         << "],\"passed\":" << (gate.passed ? "true" : "false")
         << ",\"initializerEligible\":"
         << (gate.passed ? "true" : "false")
         << ",\"gameplayPerformed\":false,\"checkpointBytes\":"
         << checkpoint_bytes << ",\"checkpointSha256\":\""
         << checkpoint_sha << "\",\"totalSeconds\":" << total_seconds
         << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"artifact\":\"" << jsonEscape(options.output) << "\"}\n";
  return gate.passed ? EXIT_SUCCESS : 2;
}

}  // namespace drop7::curriculum_long_outcome_nnue

#ifndef DROP7_CURRICULUM_LONG_OUTCOME_NNUE_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      const auto options =
          drop7::curriculum_long_outcome_nnue::parseOptions(argc, argv, 2);
      return drop7::curriculum_long_outcome_nnue::selfTest(options, std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::curriculum_long_outcome_nnue::parseOptions(argc, argv, 2);
      return drop7::curriculum_long_outcome_nnue::run(options, std::cout);
    }
    std::cerr << "usage: drop7_curriculum_long_outcome_nnue --self-test | "
                 "--run [--states PATH] [--output PATH] [--labels PATH] "
                 "[--checkpoint PATH] [--threads N]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_curriculum_long_outcome_nnue: " << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
}
#endif
