#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#include "../../fair-expectimax/reference/fair-only-depth4.cpp"
#undef DROP7_FAIR_ONLY_DEPTH4_LIBRARY

#include <bit>
#include <filesystem>
#include <optional>
#include <set>
#include <span>
#include <type_traits>

// B0 is an offline, falsification-first architecture/fidelity gate.  It is
// deliberately incapable of starting or replaying an origin game.  Its only
// stochastic evaluations begin at public roots in the checksum-locked,
// development-only corpus and use its recorded public-state-derived tape seed.
//
// Provenance exists in PanelRecord only to define an entire-origin-game fold.
// The actor boundary below accepts ActorInput, which contains public state and
// exact public D1 values but cannot carry the origin seed, move index, stored
// tape, history, score, level, or scenario id.
namespace drop7::full_panel_cpi_preflight {

namespace d1 = drop7::fair_only_horizon;
namespace d4 = drop7::fair_only_depth4;
namespace detail = drop7::cfpi::detail;
using Clock = std::chrono::steady_clock;

constexpr std::string_view kExpectedCorpusSha256 =
    "bfda8ae32fa0be3577c6b27f6413aba28f2854930e2f91a0dcf7674808f04196";
constexpr int kExpectedRecords = 477;
constexpr int kExpectedGames = 8;
constexpr std::uint32_t kExpectedGameStart = 0x3d6d'0010u;
constexpr std::array<int, kExpectedGames> kExpectedGameRecords{{
    77, 50, 55, 35, 35, 65, 55, 105,
}};
constexpr std::array<int, 4> kSampleSizes{{7, 21, 35, 63}};
constexpr int kMaximumSampleSize = 63;
constexpr int kStoredScenarioCount = 255;
constexpr int kHorizon = 200;
constexpr int kContinuationDepth = 1;
constexpr int kContinuationStrata = 5;
constexpr int kEventsPerStep = 64;
constexpr std::uint32_t kDeploymentPanelDomain = 0x5444'4550u;
constexpr std::uint32_t kRevealTapeDomain = 0x5452'564cu;
constexpr std::uint32_t kVisibleTapeDomain = 0x5456'4953u;
constexpr std::array<int, kBoardSize> kActionOrder{{3, 2, 4, 1, 5, 0, 6}};

constexpr double kStabilityMinimum = 0.70;
constexpr double kOverridePrecisionMinimum = 0.80;
constexpr double kOverrideRecallMinimum = 0.25;
constexpr double kPairwiseGainMinimum = 0.02;
constexpr double kRegretRatioMaximum = 0.90;
constexpr int kRequiredNonregressingFolds = 6;
constexpr double kActorOverrideMargin = 0.10;
constexpr double kTieTolerance = 1.0e-9;
constexpr double kPredictionTieTolerance = 1.0e-12;

constexpr int kActorWeights = 1024;
constexpr int kActorEpochs = 28;
constexpr float kActorLearningRate = 0.0125f;
constexpr float kActorWeightDecay = 2.0e-5f;
constexpr float kActorPointWeight = 0.35f;
constexpr float kActorPairWeight = 0.65f;
constexpr float kActorPairMargin = 0.20f;
constexpr float kActorGradientClip = 4.0f;
constexpr std::uint32_t kActorSeedA = 0x4230'4131u;
constexpr std::uint32_t kActorSeedB = 0x4230'4232u;

constexpr std::uint64_t kInputByteLimit = 32ull * 1024ull * 1024ull;
constexpr std::size_t kLineByteLimit = 512ull * 1024ull;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr double kWallLimitSeconds = 45.0 * 60.0;
constexpr int kMaximumThreads = 4;
constexpr std::uint64_t kMaximumSyntheticTransitions =
    static_cast<std::uint64_t>(kExpectedRecords) * kBoardSize * 2ull *
    kMaximumSampleSize * kHorizon;
constexpr std::uint64_t kMaximumD4Work =
    static_cast<std::uint64_t>(kExpectedRecords) * d4::kMaximumWork;

static_assert(kStoredScenarioCount == 255);
static_assert(kHorizon == 200 && kContinuationDepth == 1);
static_assert(kContinuationStrata == d1::kChanceSamples);
static_assert(kEventsPerStep > kCellCount);
static_assert(kLevelBonus == 17'000 && kMovesPerLevel == 5);
static_assert(kExpectedGameStart + kExpectedGames == 0x3d6d'0018u);
static_assert(kSampleSizes.back() == kMaximumSampleSize);

struct RunOptions {
  std::string input =
      "/tmp/drop7-terminal-policy-deployment-panels.jsonl";
  std::string input_sha256 = std::string(kExpectedCorpusSha256);
  std::string output = "/tmp/drop7-full-panel-cpi-preflight.json";
  int threads = kMaximumThreads;
};

std::filesystem::path resolvedPath(const std::string& source) {
  std::error_code error;
  const std::filesystem::path absolute =
      std::filesystem::absolute(std::filesystem::path(source), error);
  if (error) throw std::invalid_argument("could not resolve absolute path");
  const std::filesystem::path resolved =
      std::filesystem::weakly_canonical(absolute, error);
  if (error) throw std::invalid_argument("could not weakly canonicalize path");
  return resolved;
}

void validateDistinctInputOutput(const RunOptions& options) {
  const std::filesystem::path input = resolvedPath(options.input);
  const std::filesystem::path output = resolvedPath(options.output);
  bool equivalent = input == output;
  std::error_code exists_error;
  const bool input_exists = std::filesystem::exists(input, exists_error);
  if (exists_error) throw std::invalid_argument("could not inspect input path");
  exists_error.clear();
  const bool output_exists = std::filesystem::exists(output, exists_error);
  if (exists_error) throw std::invalid_argument("could not inspect output path");
  if (input_exists && output_exists) {
    std::error_code equivalent_error;
    equivalent = equivalent ||
                 std::filesystem::equivalent(input, output, equivalent_error);
    if (equivalent_error) {
      throw std::invalid_argument("could not compare input/output identity");
    }
  }
  if (equivalent) {
    throw std::invalid_argument(
        "input and output resolve to the same filesystem object");
  }
}

RunOptions parseOptions(int argc, char** argv, int begin) {
  RunOptions result;
  for (int index = begin; index < argc; ++index) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string_view flag(argv[index]);
    const std::string value(argv[++index]);
    if (flag == "--input") result.input = value;
    else if (flag == "--input-sha256") result.input_sha256 = value;
    else if (flag == "--output") result.output = value;
    else if (flag == "--threads") result.threads = std::stoi(value);
    else throw std::invalid_argument("unknown option " + std::string(flag));
  }
  if (result.input.empty() || result.output.empty() ||
      result.input_sha256 != kExpectedCorpusSha256) {
    throw std::invalid_argument("the frozen panel checksum is not configurable");
  }
  if (result.threads < 1 || result.threads > kMaximumThreads) {
    throw std::invalid_argument("threads must be in [1,4]");
  }
  validateDistinctInputOutput(result);
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

struct Deadline {
  Clock::time_point started = Clock::now();

  double seconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }

  void check() const {
    if (seconds() > kWallLimitSeconds) {
      throw std::runtime_error("B0 preflight exceeded 45 minute wall limit");
    }
    if (peakRssBytes() > kRssLimitBytes) {
      throw std::runtime_error("B0 preflight exceeded 256 MiB RSS limit");
    }
  }
};

std::uint64_t mix64(std::uint64_t value) {
  value ^= value >> 30u;
  value *= 0xbf58'476d'1ce4'e5b9ull;
  value ^= value >> 27u;
  value *= 0x94d0'49bb'1331'11ebull;
  return value ^ (value >> 31u);
}

std::uint32_t seed32(std::uint64_t value) {
  return mix32(static_cast<std::uint32_t>(value) ^
               static_cast<std::uint32_t>(value >> 32u));
}

std::string hex64(std::uint64_t value) {
  std::ostringstream output;
  output << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
  return output.str();
}

std::string jsonEscape(std::string_view value) {
  std::string result;
  for (const char token : value) {
    if (token == '"' || token == '\\') result.push_back('\\');
    if (token == '\n') result += "\\n";
    else result.push_back(token);
  }
  return result;
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
    message.push_back(static_cast<std::uint8_t>(bit_length >> (byte * 8)));
  }
  std::array<std::uint32_t, 8> hash{{
      0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
  }};
  for (std::size_t offset = 0; offset < message.size(); offset += 64) {
    std::array<std::uint32_t, 64> words{};
    for (int word = 0; word < 16; ++word) {
      const std::size_t at = offset + static_cast<std::size_t>(word * 4);
      words[word] = (static_cast<std::uint32_t>(message[at]) << 24) |
                    (static_cast<std::uint32_t>(message[at + 1]) << 16) |
                    (static_cast<std::uint32_t>(message[at + 2]) << 8) |
                    static_cast<std::uint32_t>(message[at + 3]);
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
    std::uint32_t a = hash[0], b = hash[1], c = hash[2], d = hash[3];
    std::uint32_t e = hash[4], f = hash[5], g = hash[6], h = hash[7];
    for (int round = 0; round < 64; ++round) {
      const std::uint32_t s1 =
          std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
      const std::uint32_t choose = (e & f) ^ (~e & g);
      const std::uint32_t first =
          h + s1 + choose + kSha256Constants[round] + words[round];
      const std::uint32_t s0 =
          std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t second = s0 + majority;
      h = g; g = f; f = e; e = d + first;
      d = c; c = b; b = a; a = first + second;
    }
    hash[0] += a; hash[1] += b; hash[2] += c; hash[3] += d;
    hash[4] += e; hash[5] += f; hash[6] += g; hash[7] += h;
  }
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(64);
  for (const std::uint32_t value : hash) {
    for (int shift = 28; shift >= 0; shift -= 4) {
      result.push_back(digits[(value >> shift) & 0xfu]);
    }
  }
  return result;
}

std::string readBoundedFile(const std::string& path) {
  std::error_code error;
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  if (error) throw std::runtime_error("could not stat frozen panel corpus");
  if (size == 0 || size > kInputByteLimit) {
    throw std::runtime_error("frozen panel corpus violates byte bound");
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not open frozen panel corpus");
  std::string result(static_cast<std::size_t>(size), '\0');
  input.read(result.data(), static_cast<std::streamsize>(result.size()));
  if (!input || input.peek() != std::char_traits<char>::eof()) {
    throw std::runtime_error("short or unstable frozen panel corpus read");
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

State materialize(const PublicState& source) {
  State result;
  result.board = source.board;
  result.next_disc = source.next_disc;
  result.score = 0;
  result.level = 1;
  result.moves_remaining = source.moves_remaining;
  result.moves_played = 0;
  result.game_over = source.terminal;
  return result;
}

PublicState publicState(const State& source) {
  return {source.board, source.next_disc,
          static_cast<std::uint8_t>(source.moves_remaining), source.game_over};
}

PublicState mirror(const PublicState& source) {
  PublicState result = source;
  result.board = detail::mirrorBoard(source.board);
  return result;
}

PublicState canonicalPublic(const PublicState& source, bool& mirrored) {
  return publicState(detail::canonicalState(materialize(source), mirrored));
}

std::uint64_t publicHash(const PublicState& source) {
  bool ignored = false;
  const PublicState state = canonicalPublic(source, ignored);
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

std::uint32_t panelSeed(const PublicState& source) {
  return seed32(publicHash(source) ^
                static_cast<std::uint64_t>(kDeploymentPanelDomain));
}

std::size_t afterMarker(std::string_view text, std::string_view marker,
                        std::size_t begin = 0) {
  const std::size_t found = text.find(marker, begin);
  if (found == std::string_view::npos) {
    throw std::runtime_error("missing JSON marker " + std::string(marker));
  }
  return found + marker.size();
}

long long integerAfter(std::string_view text, std::string_view marker,
                       std::size_t begin = 0) {
  const std::size_t cursor = afterMarker(text, marker, begin);
  const std::string owned(text);
  char* end = nullptr;
  const char* first = owned.c_str() + cursor;
  const long long result = std::strtoll(first, &end, 10);
  if (end == first) throw std::runtime_error("invalid JSON integer");
  return result;
}

double numberAfter(std::string_view text, std::string_view marker,
                   std::size_t begin = 0) {
  const std::size_t cursor = afterMarker(text, marker, begin);
  const std::string owned(text);
  char* end = nullptr;
  const char* first = owned.c_str() + cursor;
  const double result = std::strtod(first, &end);
  if (end == first || !std::isfinite(result)) {
    throw std::runtime_error("invalid JSON number");
  }
  return result;
}

bool booleanAfter(std::string_view text, std::string_view marker,
                  std::size_t begin = 0) {
  const std::size_t cursor = afterMarker(text, marker, begin);
  if (text.substr(cursor, 4) == "true") return true;
  if (text.substr(cursor, 5) == "false") return false;
  throw std::runtime_error("invalid JSON boolean");
}

std::string stringAfter(std::string_view text, std::string_view marker,
                        std::size_t begin = 0) {
  const std::size_t cursor = afterMarker(text, marker, begin);
  const std::size_t end = text.find('"', cursor);
  if (end == std::string_view::npos) {
    throw std::runtime_error("unterminated JSON string");
  }
  return std::string(text.substr(cursor, end - cursor));
}

std::size_t matchingDelimiter(std::string_view text, std::size_t begin,
                              char open, char close) {
  if (begin >= text.size() || text[begin] != open) {
    throw std::runtime_error("invalid JSON delimiter start");
  }
  int depth = 0;
  bool quoted = false;
  bool escaped = false;
  for (std::size_t cursor = begin; cursor < text.size(); ++cursor) {
    const char token = text[cursor];
    if (quoted) {
      if (escaped) escaped = false;
      else if (token == '\\') escaped = true;
      else if (token == '"') quoted = false;
      continue;
    }
    if (token == '"') quoted = true;
    else if (token == open) ++depth;
    else if (token == close && --depth == 0) return cursor;
  }
  throw std::runtime_error("unterminated JSON delimiter");
}

void skipSeparators(std::string_view text, std::size_t& cursor) {
  while (cursor < text.size() &&
         (text[cursor] == ' ' || text[cursor] == '\t' ||
          text[cursor] == ',')) {
    ++cursor;
  }
}

std::uint64_t parseHex64(std::string_view text) {
  const std::string owned(text);
  char* end = nullptr;
  const unsigned long long result = std::strtoull(owned.c_str(), &end, 0);
  if (end == owned.c_str() || *end != '\0') {
    throw std::runtime_error("invalid hexadecimal integer");
  }
  return static_cast<std::uint64_t>(result);
}

struct StoredAction {
  bool legal = false;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double mean_clears = 0.0;
  double mean_reveals = 0.0;
  int surviving_cutoffs = 0;
};

// Only origin_slot is later used, and only by the split coordinator.  Neither
// this record nor any function accepting it is an actor inference boundary.
struct PanelRecord {
  std::uint32_t origin_game = 0;
  int origin_slot = -1;
  int move_index = -1;
  std::uint64_t stored_public_hash = 0;
  std::uint32_t tape_seed = 0;
  PublicState state{};
  int stored_d1_action = -1;
  int stored_deployment_action = -1;
  std::array<StoredAction, kBoardSize> actions{};
};

StoredAction parseStoredAction(std::string_view object, int expected_action) {
  StoredAction result;
  result.legal = true;
  if (integerAfter(object, "\"action\":") != expected_action) {
    throw std::runtime_error("panel action index mismatch");
  }
  result.mean_score = numberAfter(object, "\"meanScoreReturn\":");
  result.mean_moves = numberAfter(object, "\"meanSurvivedMoves\":");
  result.mean_clears = numberAfter(object, "\"meanNumberedClears\":");
  result.mean_reveals = numberAfter(object, "\"meanCoversRevealed\":");
  result.surviving_cutoffs =
      static_cast<int>(integerAfter(object, "\"survivingCutoffs\":"));
  // The fixed h200 target includes the public fair leaf at a surviving
  // cutoff, so a finite score return may legitimately be negative.
  if (result.mean_moves < 0.0 || result.mean_moves > kHorizon ||
      result.mean_clears < 0.0 ||
      result.mean_reveals < 0.0 || result.surviving_cutoffs < 0 ||
      result.surviving_cutoffs > kStoredScenarioCount) {
    throw std::runtime_error("out-of-domain stored action summary");
  }
  return result;
}

PanelRecord parsePanel(std::string_view line) {
  constexpr std::string_view excluded =
      "\"excludedFromModelInput\":[\"screenSeed\",\"moveIndex\","
      "\"canonicalPublicHash\",\"tapeSeed\",\"score\",\"level\","
      "\"history\",\"scenario\"]";
  if (line.empty() || line.size() > kLineByteLimit || line.front() != '{' ||
      line.back() != '}' || line.find('\0') != std::string_view::npos ||
      line.find("\"recordType\":\"deployment-panel-export-replay\"") ==
          std::string_view::npos ||
      line.find("\"gate\":\"ultra\"") == std::string_view::npos ||
      line.find(excluded) == std::string_view::npos) {
    throw std::runtime_error("unexpected panel record metadata");
  }

  PanelRecord result;
  const long long origin = integerAfter(line, "\"screenSeed\":");
  const long long tape = integerAfter(line, "\"tapeSeed\":");
  if (origin < 0 || origin > std::numeric_limits<std::uint32_t>::max() ||
      tape < 0 || tape > std::numeric_limits<std::uint32_t>::max()) {
    throw std::runtime_error("invalid panel provenance integer");
  }
  result.origin_game = static_cast<std::uint32_t>(origin);
  result.move_index = static_cast<int>(integerAfter(line, "\"moveIndex\":"));
  result.stored_public_hash =
      parseHex64(stringAfter(line, "\"canonicalPublicHash\":\""));
  result.tape_seed = static_cast<std::uint32_t>(tape);

  const std::string board = stringAfter(line, "\"board\":\"");
  if (board.size() != kCellCount) throw std::runtime_error("invalid board size");
  for (int cell = 0; cell < kCellCount; ++cell) {
    if (board[cell] < '0' || board[cell] > '9') {
      throw std::runtime_error("invalid board token");
    }
    result.state.board[cell] = static_cast<std::uint8_t>(board[cell] - '0');
    if (result.state.board[cell] > kCracked) {
      throw std::runtime_error("out-of-domain board token");
    }
  }
  result.state.next_disc =
      static_cast<std::uint8_t>(integerAfter(line, "\"nextDisc\":"));
  result.state.moves_remaining =
      static_cast<std::uint8_t>(integerAfter(line, "\"movesRemaining\":"));
  result.state.terminal = booleanAfter(line, "\"terminal\":");
  result.stored_d1_action =
      static_cast<int>(integerAfter(line, "\"fairD1Action\":"));
  result.stored_deployment_action =
      static_cast<int>(integerAfter(line, "\"selectedAction\":"));
  if (result.move_index < 0 || result.state.terminal ||
      result.state.next_disc < 1 || result.state.next_disc > kBoardSize ||
      result.state.moves_remaining < 1 ||
      result.state.moves_remaining > kMovesPerLevel) {
    throw std::runtime_error("invalid public root metadata");
  }

  std::size_t cursor = afterMarker(line, "\"actions\":[");
  for (int action = 0; action < kBoardSize; ++action) {
    skipSeparators(line, cursor);
    if (line.substr(cursor, 4) == "null") {
      cursor += 4;
      continue;
    }
    if (cursor >= line.size() || line[cursor] != '{') {
      throw std::runtime_error("invalid action array");
    }
    const std::size_t end = matchingDelimiter(line, cursor, '{', '}');
    result.actions[action] =
        parseStoredAction(line.substr(cursor, end - cursor + 1), action);
    cursor = end + 1;
  }
  skipSeparators(line, cursor);
  if (cursor >= line.size() || line[cursor] != ']') {
    throw std::runtime_error("unterminated action array");
  }
  for (int action = 0; action < kBoardSize; ++action) {
    if (result.actions[action].legal != isLegal(result.state.board, action)) {
      throw std::runtime_error("stored legal mask differs from public state");
    }
  }
  if (result.stored_d1_action < 0 ||
      result.stored_d1_action >= kBoardSize ||
      result.stored_deployment_action < 0 ||
      result.stored_deployment_action >= kBoardSize ||
      !result.actions[result.stored_d1_action].legal ||
      !result.actions[result.stored_deployment_action].legal ||
      publicHash(result.state) != result.stored_public_hash ||
      panelSeed(result.state) != result.tape_seed) {
    throw std::runtime_error("panel public-state/hash/tape invariant failed");
  }
  return result;
}

std::vector<PanelRecord> parseLockedCorpus(std::string_view bytes) {
  if (bytes.size() > kInputByteLimit || sha256(bytes) != kExpectedCorpusSha256) {
    throw std::runtime_error("frozen panel corpus SHA-256 mismatch");
  }
  std::vector<PanelRecord> result;
  result.reserve(kExpectedRecords);
  std::size_t begin = 0;
  while (begin < bytes.size()) {
    const std::size_t newline = bytes.find('\n', begin);
    const std::size_t end =
        newline == std::string_view::npos ? bytes.size() : newline;
    if (end > begin) result.push_back(parsePanel(bytes.substr(begin, end - begin)));
    begin = newline == std::string_view::npos ? bytes.size() : newline + 1;
  }
  if (result.size() != kExpectedRecords) {
    throw std::runtime_error("frozen panel record count mismatch");
  }
  std::array<int, kExpectedGames> counts{};
  std::array<int, kExpectedGames> next_move{};
  for (PanelRecord& panel : result) {
    const std::int64_t slot64 =
        static_cast<std::int64_t>(panel.origin_game) - kExpectedGameStart;
    if (slot64 < 0 || slot64 >= kExpectedGames) {
      throw std::runtime_error("out-of-domain origin game");
    }
    panel.origin_slot = static_cast<int>(slot64);
    if (panel.move_index != next_move[panel.origin_slot]++) {
      throw std::runtime_error("origin records are not whole ordered games");
    }
    ++counts[panel.origin_slot];
  }
  if (counts != kExpectedGameRecords || next_move != kExpectedGameRecords) {
    throw std::runtime_error("frozen per-origin record counts changed");
  }
  return result;
}

std::vector<PanelRecord> loadLockedCorpus(const RunOptions& options) {
  if (options.input_sha256 != kExpectedCorpusSha256) {
    throw std::runtime_error("frozen checksum declaration changed");
  }
  return parseLockedCorpus(readBoundedFile(options.input));
}

// Each half takes a different point from each of 63 equal strata over the
// source 255 scenario ids.  The 63 base positions are seven blocks of nine.
// Within every block, K=7 selects {4}, K=21 selects {1,4,7}, K=35 selects
// {1,2,4,6,7}, and K=63 selects all positions.  Consequently each smaller set
// is an exact subset of every larger set and each of the seven coarse strata
// contributes exactly K/7 samples.  The two scenario halves remain disjoint.
constexpr int scenarioAt(int half, int base_position) {
  return ((4 * base_position + (half == 0 ? 1 : 3)) *
          kStoredScenarioCount) /
         (4 * kMaximumSampleSize);
}

constexpr int selectedBasePosition(int sample_index, int sample_position) {
  if (sample_index < 0 || sample_index >= 4 || sample_position < 0 ||
      sample_position >= kSampleSizes[sample_index]) {
    throw std::invalid_argument("invalid nested sample position");
  }
  if (sample_index == 0) return sample_position * 9 + 4;
  if (sample_index == 1) {
    constexpr std::array<int, 3> offsets{{1, 4, 7}};
    return (sample_position / 3) * 9 + offsets[sample_position % 3];
  }
  if (sample_index == 2) {
    constexpr std::array<int, 5> offsets{{1, 2, 4, 6, 7}};
    return (sample_position / 5) * 9 + offsets[sample_position % 5];
  }
  return sample_position;
}

std::vector<int> selectedBasePositions(int sample_index) {
  if (sample_index < 0 || sample_index >= 4) {
    throw std::invalid_argument("invalid sample index");
  }
  std::vector<int> result;
  result.reserve(kSampleSizes[sample_index]);
  for (int position = 0; position < kSampleSizes[sample_index]; ++position) {
    result.push_back(selectedBasePosition(sample_index, position));
  }
  return result;
}

std::vector<int> selectedScenarios(int half, int sample_index) {
  if (half < 0 || half > 1) throw std::invalid_argument("invalid sample half");
  std::vector<int> result;
  for (const int base : selectedBasePositions(sample_index)) {
    result.push_back(scenarioAt(half, base));
  }
  return result;
}

std::array<int, kMaximumSampleSize> scenarioHalf(int half) {
  if (half < 0 || half > 1) throw std::invalid_argument("invalid sample half");
  std::array<int, kMaximumSampleSize> result{};
  for (int position = 0; position < kMaximumSampleSize; ++position) {
    result[position] = scenarioAt(half, position);
  }
  return result;
}

struct FairD1Decision {
  int action = -1;
  std::array<double, kBoardSize> values{};
  std::uint64_t work = 0;
  int evaluated_actions = 0;
};

// Public-only boundary.  In particular, this cannot accept PanelRecord.
FairD1Decision chooseFairD1(const PublicState& source) {
  FairD1Decision result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  if (source.terminal) return result;
  bool mirrored = false;
  const PublicState canonical = canonicalPublic(source, mirrored);
  d1::SearchContext context;
  const d1::RootEvaluation root =
      d1::rootDecision(materialize(canonical), kContinuationDepth, context);
  int legal = 0;
  int evaluated = 0;
  for (int action = 0; action < kBoardSize; ++action) {
    legal += isLegal(canonical.board, action);
    evaluated += std::isfinite(root.values[action]);
  }
  if (root.action < 0 || legal != evaluated || !context.cache.empty() ||
      context.work > 2ull * kBoardSize * kContinuationStrata) {
    throw std::runtime_error("exact public D1 root did not complete");
  }
  result.action = mirrored ? kBoardSize - 1 - root.action : root.action;
  for (int canonical_action = 0; canonical_action < kBoardSize;
       ++canonical_action) {
    const int source_action = mirrored ? kBoardSize - 1 - canonical_action
                                       : canonical_action;
    result.values[source_action] = root.values[canonical_action];
  }
  result.work = context.work;
  result.evaluated_actions = evaluated;
  return result;
}

using PublicD1Boundary = FairD1Decision (*)(const PublicState&);
static_assert(std::is_same_v<decltype(&chooseFairD1), PublicD1Boundary>);
static_assert(!std::is_invocable_v<PublicD1Boundary, const PanelRecord&>);

struct SyntheticWork {
  std::uint64_t transitions = 0;
  std::uint64_t d1_calls = 0;
  std::uint64_t d1_work = 0;

  SyntheticWork& operator+=(const SyntheticWork& other) {
    transitions += other.transitions;
    d1_calls += other.d1_calls;
    d1_work += other.d1_work;
    return *this;
  }
};

std::uint8_t visibleDisc(std::uint32_t root_seed, int scenario, int step) {
  const double unit = detail::stratifiedUnit(
      root_seed, scenario, kStoredScenarioCount, kVisibleTapeDomain, step);
  return static_cast<std::uint8_t>(
      std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
}

bool playSyntheticMove(const PublicState& source, int action,
                       std::uint32_t root_seed, int scenario, int step,
                       MoveResult& result) {
  if (source.terminal || scenario < 0 || scenario >= kStoredScenarioCount ||
      step < 0 || step >= kHorizon || !isLegal(source.board, action)) {
    return false;
  }
  Board board = source.board;
  if (!placeDisc(board, action, source.next_disc)) return false;
  struct RevealTape {
    std::uint32_t root_seed;
    int scenario;
    int step;
    int event = 0;
    std::uint8_t nextDisc() {
      if (event >= kEventsPerStep) {
        throw std::runtime_error("synthetic reveal slice exhausted");
      }
      const int event_index = step * kEventsPerStep + event++;
      const double unit = detail::stratifiedUnit(
          root_seed, scenario, kStoredScenarioCount, kRevealTapeDomain,
          event_index);
      return static_cast<std::uint8_t>(
          std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
    }
  } reveals{root_seed, scenario, step};

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
      const int depth = result.waves.empty() ? 1 : result.waves.back().depth + 1;
      detail::resolveCascadeSampled(board, reveals, depth, rise_score,
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
      terminal ? source.next_disc : visibleDisc(root_seed, scenario, step);
  result.state.score = 0;
  result.state.level = 1;
  result.state.moves_remaining = moves_remaining;
  result.state.moves_played = 0;
  result.state.game_over = terminal;
  return true;
}

struct ScenarioOutcome {
  double score = 0.0;
  double moves = 0.0;

  bool operator==(const ScenarioOutcome&) const = default;
};

ScenarioOutcome rolloutScenario(const PublicState& root, int root_action,
                                std::uint32_t root_seed, int scenario,
                                SyntheticWork& work,
                                const Deadline* deadline,
                                int horizon = kHorizon) {
  if (root.terminal || !isLegal(root.board, root_action) || horizon < 1 ||
      horizon > kHorizon) {
    throw std::invalid_argument("invalid synthetic rollout root");
  }
  PublicState state = root;
  ScenarioOutcome result;
  for (int step = 0; step < horizon; ++step) {
    if (deadline != nullptr) deadline->check();
    int action = root_action;
    if (step > 0) {
      const FairD1Decision continuation = chooseFairD1(state);
      action = continuation.action;
      ++work.d1_calls;
      work.d1_work += continuation.work;
    }
    MoveResult move;
    if (!playSyntheticMove(state, action, root_seed, scenario, step, move)) {
      throw std::runtime_error("synthetic rollout transition failed");
    }
    ++work.transitions;
    result.score += static_cast<double>(move.score_delta);
    result.moves += 1.0;
    state = publicState(move.state);
    if (state.terminal) return result;
  }
  result.score += d1::fairLeaf(materialize(state));
  return result;
}

struct SparseFeatures {
  static constexpr int kCapacity = 96;
  std::array<std::uint16_t, kCapacity> index{};
  std::array<float, kCapacity> value{};
  std::uint16_t size = 0;

  void add(int at, float amount) {
    if (at < 0 || at >= kActorWeights || !std::isfinite(amount)) {
      throw std::invalid_argument("invalid actor feature");
    }
    for (int item = 0; item < size; ++item) {
      if (index[item] == at) {
        value[item] += amount;
        return;
      }
    }
    if (size >= kCapacity) throw std::runtime_error("actor feature overflow");
    index[size] = static_cast<std::uint16_t>(at);
    value[size] = amount;
    ++size;
  }

  bool operator==(const SparseFeatures&) const = default;
};

struct ActorInput {
  PublicState state{};
  std::array<double, kBoardSize> d1_q{};
};

static_assert(!std::is_constructible_v<ActorInput, PanelRecord>);

struct CanonicalActorInput {
  PublicState state{};
  std::array<double, kBoardSize> d1_q{};
  int action = -1;
  bool mirrored = false;
};

CanonicalActorInput canonicalizeStateAction(const ActorInput& input,
                                             int source_action) {
  if (source_action < 0 || source_action >= kBoardSize) {
    throw std::invalid_argument("actor action is out of range");
  }
  const PublicState reflected = mirror(input.state);
  const int reflected_action = kBoardSize - 1 - source_action;
  const bool reflected_state_is_smaller =
      std::lexicographical_compare(reflected.board.begin(),
                                   reflected.board.end(),
                                   input.state.board.begin(),
                                   input.state.board.end());
  const bool state_is_symmetric = reflected.board == input.state.board;
  const bool use_reflection =
      reflected_state_is_smaller ||
      (state_is_symmetric && reflected_action < source_action);

  CanonicalActorInput result;
  result.state = use_reflection ? reflected : input.state;
  result.action = use_reflection ? reflected_action : source_action;
  result.mirrored = use_reflection;
  for (int canonical_action = 0; canonical_action < kBoardSize;
       ++canonical_action) {
    const int original_action =
        use_reflection ? kBoardSize - 1 - canonical_action : canonical_action;
    result.d1_q[canonical_action] = input.d1_q[original_action];
  }
  return result;
}

int hashedFeature(std::uint64_t domain, std::uint64_t key) {
  constexpr int begin = 64;
  return begin + static_cast<int>(mix64(domain ^ key) %
                                  static_cast<std::uint64_t>(kActorWeights - begin));
}

SparseFeatures actorFeatures(const ActorInput& input, int source_action) {
  if (source_action < 0 || source_action >= kBoardSize ||
      !isLegal(input.state.board, source_action)) {
    throw std::invalid_argument("illegal actor feature action");
  }
  const CanonicalActorInput canonical =
      canonicalizeStateAction(input, source_action);
  const PublicState& state = canonical.state;
  const int action = canonical.action;
  const auto& q = canonical.d1_q;
  double q_min = std::numeric_limits<double>::infinity();
  double q_max = -std::numeric_limits<double>::infinity();
  double q_sum = 0.0;
  int legal = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    if (!isLegal(state.board, column)) continue;
    if (!std::isfinite(q[column])) throw std::invalid_argument("invalid D1 Q");
    q_min = std::min(q_min, q[column]);
    q_max = std::max(q_max, q[column]);
    q_sum += q[column];
    ++legal;
  }
  const double q_mean = q_sum / static_cast<double>(legal);
  const double q_scale = std::max(1.0, q_max - q_min);

  SparseFeatures result;
  result.add(0, 1.0f);
  result.add(1, static_cast<float>((q[action] - q_mean) / q_scale));
  result.add(2, static_cast<float>((q[action] - q_max) / q_scale));
  result.add(3, static_cast<float>(std::abs(action - 3) / 3.0));
  result.add(8 + action, 1.0f);
  result.add(16 + state.moves_remaining - 1, 1.0f);
  result.add(24 + state.next_disc - 1, 1.0f);

  const auto heights = detail::columnHeights(state.board);
  int occupied = 0;
  int maximum_height = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    occupied += heights[column];
    maximum_height = std::max(maximum_height, heights[column]);
    const int relative = column - action + (kBoardSize - 1);
    const std::uint64_t key =
        static_cast<std::uint64_t>(relative * (kBoardSize + 1) + heights[column]);
    result.add(hashedFeature(0x4845'4947ull, key), 1.0f);
  }
  result.add(32, static_cast<float>(occupied) / kCellCount);
  result.add(33, static_cast<float>(maximum_height) / kBoardSize);
  result.add(34, static_cast<float>(heights[action]) / kBoardSize);

  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      const int relative = column - action + (kBoardSize - 1);
      const std::uint64_t key =
          static_cast<std::uint64_t>(((row * (2 * kBoardSize - 1) + relative) *
                                      (kCracked + 1)) +
                                     state.board[indexOf(row, column)]);
      result.add(hashedFeature(0x4345'4c4cull, key), 1.0f);
    }
  }
  return result;
}

using PublicActorBoundary = SparseFeatures (*)(const ActorInput&, int);
static_assert(std::is_same_v<decltype(&actorFeatures), PublicActorBoundary>);
static_assert(!std::is_invocable_v<PublicActorBoundary, const PanelRecord&, int>);

struct ActorModel {
  std::array<float, kActorWeights> weights{};

  double predict(const SparseFeatures& features) const {
    double result = 0.0;
    for (int item = 0; item < features.size; ++item) {
      result += static_cast<double>(weights[features.index[item]]) *
                features.value[item];
    }
    return result;
  }
};

static_assert(sizeof(ActorModel) == kActorWeights * sizeof(float));
static_assert(sizeof(ActorModel) <= 8ull * 1024ull);

struct SampleMeans {
  std::array<std::array<std::array<double, kBoardSize>, 4>, 2> score{};
  std::array<std::array<std::array<double, kBoardSize>, 4>, 2> moves{};
};

struct RootData {
  PanelRecord panel{};
  FairD1Decision d1_search{};
  d4::SearchDecision d4_search{};
  std::array<SparseFeatures, kBoardSize> features{};
  SampleMeans samples{};
  SyntheticWork work{};
};

std::array<double, kBoardSize> storedTeacher(const RootData& root) {
  std::array<double, kBoardSize> result{};
  result.fill(-std::numeric_limits<double>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    if (root.panel.actions[action].legal) {
      result[action] = root.panel.actions[action].mean_score;
    }
  }
  return result;
}

std::array<double, kBoardSize> normalizedTargets(
    const RootData& root, int half, int sample_index) {
  std::array<double, kBoardSize> result{};
  double minimum = std::numeric_limits<double>::infinity();
  double maximum = -std::numeric_limits<double>::infinity();
  double sum = 0.0;
  int legal = 0;
  for (int action = 0; action < kBoardSize; ++action) {
    if (!root.panel.actions[action].legal) continue;
    const double value = root.samples.score[half][sample_index][action];
    minimum = std::min(minimum, value);
    maximum = std::max(maximum, value);
    sum += value;
    ++legal;
  }
  const double mean = sum / static_cast<double>(legal);
  const double scale = std::max(1.0, maximum - minimum);
  for (int action = 0; action < kBoardSize; ++action) {
    result[action] = root.panel.actions[action].legal
                         ? (root.samples.score[half][sample_index][action] - mean) /
                               scale
                         : -std::numeric_limits<double>::infinity();
  }
  return result;
}

int bestAction(const std::array<double, kBoardSize>& values,
               const std::array<StoredAction, kBoardSize>& legal) {
  int best = -1;
  for (const int action : kActionOrder) {
    if (!legal[action].legal) continue;
    if (best < 0 || values[action] > values[best] + kTieTolerance) best = action;
  }
  if (best < 0) throw std::runtime_error("no legal best action");
  return best;
}

void addGradient(ActorModel& model, const SparseFeatures& features,
                 double gradient, float learning_rate) {
  gradient = std::clamp(gradient, -static_cast<double>(kActorGradientClip),
                        static_cast<double>(kActorGradientClip));
  for (int item = 0; item < features.size; ++item) {
    float& weight = model.weights[features.index[item]];
    const float feature = features.value[item];
    weight -= learning_rate *
              (static_cast<float>(gradient) * feature +
               kActorWeightDecay * weight);
  }
}

struct FoldSelection {
  int heldout_origin = -1;
  std::vector<bool> eligible;
  std::size_t heldout_origin_roots = 0;
  std::size_t purged_canonical_state_roots = 0;
  std::size_t training_roots = 0;
};

PublicState exactCanonicalPublicState(const PublicState& source) {
  bool ignored = false;
  return canonicalPublic(source, ignored);
}

FoldSelection buildFoldSelection(const std::vector<RootData>& roots,
                                 int heldout_origin) {
  if (heldout_origin < 0 || heldout_origin >= kExpectedGames) {
    throw std::invalid_argument("invalid heldout origin");
  }
  FoldSelection result;
  result.heldout_origin = heldout_origin;
  result.eligible.assign(roots.size(), false);
  std::vector<PublicState> heldout_states;
  heldout_states.reserve(roots.size());
  for (const RootData& root : roots) {
    if (root.panel.origin_slot == heldout_origin) {
      heldout_states.push_back(exactCanonicalPublicState(root.panel.state));
      ++result.heldout_origin_roots;
    }
  }
  if (heldout_states.empty()) {
    throw std::runtime_error("whole-origin fold has no heldout roots");
  }
  for (std::size_t index = 0; index < roots.size(); ++index) {
    const RootData& root = roots[index];
    if (root.panel.origin_slot == heldout_origin) continue;
    const PublicState candidate = exactCanonicalPublicState(root.panel.state);
    const bool duplicates_heldout =
        std::find(heldout_states.begin(), heldout_states.end(), candidate) !=
        heldout_states.end();
    if (duplicates_heldout) {
      ++result.purged_canonical_state_roots;
      continue;
    }
    result.eligible[index] = true;
    ++result.training_roots;
  }
  if (result.training_roots + result.heldout_origin_roots +
          result.purged_canonical_state_roots !=
      roots.size()) {
    throw std::runtime_error("fold exclusion accounting failed");
  }
  return result;
}

std::vector<std::size_t> trainingOrder(const std::vector<RootData>& roots,
                                       const FoldSelection& selection,
                                       std::uint32_t domain, int epoch) {
  if (selection.eligible.size() != roots.size()) {
    throw std::invalid_argument("fold selection size mismatch");
  }
  std::vector<std::size_t> result;
  result.reserve(roots.size());
  for (std::size_t index = 0; index < roots.size(); ++index) {
    if (selection.eligible[index]) result.push_back(index);
  }
  if (result.size() != selection.training_roots) {
    throw std::runtime_error("fold training count changed");
  }
  std::uint32_t state = mix32(domain ^ static_cast<std::uint32_t>(epoch));
  for (std::size_t index = result.size(); index > 1; --index) {
    state = mix32(state + 0x9e37'79b9u);
    const std::size_t other = state % index;
    std::swap(result[index - 1], result[other]);
  }
  return result;
}

ActorModel trainActor(const std::vector<RootData>& roots,
                      const FoldSelection& selection, int half,
                      int sample_index, std::uint32_t domain) {
  if (selection.heldout_origin < 0 ||
      selection.heldout_origin >= kExpectedGames || half < 0 || half > 1 ||
      sample_index < 0 || sample_index >= 4 ||
      selection.eligible.size() != roots.size()) {
    throw std::invalid_argument("invalid actor training split");
  }
  ActorModel result;

  for (int epoch = 0; epoch < kActorEpochs; ++epoch) {
    const float learning_rate =
        kActorLearningRate / std::sqrt(1.0f + 0.12f * epoch);
    const std::vector<std::size_t> order =
        trainingOrder(roots, selection, domain, epoch);
    for (const std::size_t root_index : order) {
      const RootData& root = roots[root_index];
      if (!selection.eligible[root_index] ||
          root.panel.origin_slot == selection.heldout_origin) {
        throw std::runtime_error("whole-origin/state-group fold leakage");
      }
      const auto target = normalizedTargets(root, half, sample_index);
      std::array<double, kBoardSize> prediction{};
      prediction.fill(-std::numeric_limits<double>::infinity());
      for (int action = 0; action < kBoardSize; ++action) {
        if (!root.panel.actions[action].legal) continue;
        prediction[action] = result.predict(root.features[action]);
        addGradient(result, root.features[action],
                    kActorPointWeight * (prediction[action] - target[action]),
                    learning_rate);
      }
      const int teacher_best = bestAction(target, root.panel.actions);
      for (int action = 0; action < kBoardSize; ++action) {
        if (!root.panel.actions[action].legal || action == teacher_best) continue;
        const double target_gap = target[teacher_best] - target[action];
        if (target_gap <= kTieTolerance) continue;
        const double predicted_gap =
            result.predict(root.features[teacher_best]) -
            result.predict(root.features[action]);
        if (predicted_gap >= kActorPairMargin) continue;
        const double gradient = kActorPairWeight *
                                (predicted_gap - kActorPairMargin);
        addGradient(result, root.features[teacher_best], gradient,
                    learning_rate);
        addGradient(result, root.features[action], -gradient, learning_rate);
      }
    }
  }
  return result;
}

std::uint64_t actorFingerprint(const ActorModel& model) {
  std::uint64_t result = 0x4230'4143'544f'5221ull;
  for (const float weight : model.weights) {
    result = mix64(result ^ std::bit_cast<std::uint32_t>(weight));
  }
  return result;
}

RootData evaluateRoot(const PanelRecord& panel, const Deadline& deadline) {
  RootData result;
  result.panel = panel;
  result.d1_search = chooseFairD1(panel.state);
  if (result.d1_search.action != panel.stored_d1_action) {
    throw std::runtime_error("stored and exact public D1 actions differ");
  }
  result.d4_search = d4::chooseDepth4Action(materialize(panel.state));
  if (!result.d4_search.complete ||
      result.d4_search.completed_depth != d4::kCandidateDepth ||
      result.d4_search.action < 0 ||
      result.d4_search.work > d4::kMaximumWork) {
    throw std::runtime_error("exact public D4 root did not complete");
  }
  ActorInput actor_input{panel.state, result.d1_search.values};
  for (int action = 0; action < kBoardSize; ++action) {
    if (panel.actions[action].legal) {
      result.features[action] = actorFeatures(actor_input, action);
    }
  }

  std::array<std::array<std::array<ScenarioOutcome, kMaximumSampleSize>, 2>,
             kBoardSize>
      outcomes{};
  for (int action = 0; action < kBoardSize; ++action) {
    if (!panel.actions[action].legal) continue;
    for (int half = 0; half < 2; ++half) {
      for (int position = 0; position < kMaximumSampleSize; ++position) {
        outcomes[action][half][position] = rolloutScenario(
            panel.state, action, panel.tape_seed, scenarioAt(half, position),
            result.work, &deadline);
      }
    }
  }
  for (int half = 0; half < 2; ++half) {
    for (int sample_index = 0; sample_index < 4; ++sample_index) {
      const int sample_size = kSampleSizes[sample_index];
      for (int action = 0; action < kBoardSize; ++action) {
        if (!panel.actions[action].legal) continue;
        double score = 0.0;
        double moves = 0.0;
        for (int position = 0; position < sample_size; ++position) {
          const int base = selectedBasePosition(sample_index, position);
          score += outcomes[action][half][base].score;
          moves += outcomes[action][half][base].moves;
        }
        result.samples.score[half][sample_index][action] =
            score / sample_size;
        result.samples.moves[half][sample_index][action] =
            moves / sample_size;
      }
    }
  }
  if (result.work.transitions >
      static_cast<std::uint64_t>(kBoardSize) * 2ull * kMaximumSampleSize *
          kHorizon) {
    throw std::runtime_error("per-root synthetic work proof failed");
  }
  deadline.check();
  return result;
}

struct EvaluatedCorpus {
  std::vector<RootData> roots;
  SyntheticWork synthetic{};
  std::uint64_t d4_work = 0;
  std::uint64_t d4_nodes = 0;
};

EvaluatedCorpus evaluateCorpus(const std::vector<PanelRecord>& panels,
                               int threads, const Deadline& deadline) {
  EvaluatedCorpus result;
  result.roots.resize(panels.size());
  std::atomic<std::size_t> next{0};
  std::atomic<std::size_t> completed{0};
  std::mutex progress_mutex;
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < threads; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const std::size_t index = next.fetch_add(1);
        if (index >= panels.size()) return;
        result.roots[index] = evaluateRoot(panels[index], deadline);
        const std::size_t done = completed.fetch_add(1) + 1;
        if (done % 10 == 0 || done == panels.size()) {
          const std::lock_guard<std::mutex> lock(progress_mutex);
          std::cerr << "B0 burned-root evaluation " << done << '/'
                    << panels.size() << '\n';
        }
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  for (const RootData& root : result.roots) {
    result.synthetic += root.work;
    result.d4_work += root.d4_search.work;
    result.d4_nodes += root.d4_search.nodes;
  }
  if (result.synthetic.transitions > kMaximumSyntheticTransitions ||
      result.d4_work > kMaximumD4Work) {
    throw std::runtime_error("whole-corpus work proof failed");
  }
  deadline.check();
  return result;
}

struct RankingAccumulator {
  std::uint64_t roots = 0;
  std::uint64_t pairs = 0;
  double pairwise_credit = 0.0;
  double top1_credit = 0.0;
  double normalized_regret = 0.0;
};

void addRanking(RankingAccumulator& result,
                const std::array<double, kBoardSize>& prediction,
                const std::array<double, kBoardSize>& teacher,
                const std::array<StoredAction, kBoardSize>& actions) {
  const int selected = bestAction(prediction, actions);
  const int teacher_best = bestAction(teacher, actions);
  double teacher_minimum = std::numeric_limits<double>::infinity();
  double teacher_maximum = -std::numeric_limits<double>::infinity();
  for (int action = 0; action < kBoardSize; ++action) {
    if (!actions[action].legal) continue;
    teacher_minimum = std::min(teacher_minimum, teacher[action]);
    teacher_maximum = std::max(teacher_maximum, teacher[action]);
  }
  ++result.roots;
  result.top1_credit +=
      std::abs(teacher[selected] - teacher[teacher_best]) <= kTieTolerance;
  const double range = teacher_maximum - teacher_minimum;
  if (range > kTieTolerance) {
    result.normalized_regret +=
        (teacher_maximum - teacher[selected]) / range;
  }
  for (int first = 0; first < kBoardSize; ++first) {
    if (!actions[first].legal) continue;
    for (int second = first + 1; second < kBoardSize; ++second) {
      if (!actions[second].legal) continue;
      const double truth = teacher[first] - teacher[second];
      const double guessed = prediction[first] - prediction[second];
      result.pairwise_credit +=
          std::abs(truth) <= kTieTolerance ||
                  std::abs(guessed) <= kPredictionTieTolerance
              ? 0.5
              : ((truth > 0.0) == (guessed > 0.0) ? 1.0 : 0.0);
      ++result.pairs;
    }
  }
}

struct RankingMetrics {
  double top1 = 0.0;
  double pairwise = 0.0;
  double normalized_regret = 0.0;
};

RankingMetrics finishRanking(const RankingAccumulator& source) {
  if (source.roots == 0 || source.pairs == 0) return {};
  return {source.top1_credit / source.roots,
          source.pairwise_credit / source.pairs,
          source.normalized_regret / source.roots};
}

struct FoldMetrics {
  RankingMetrics actor{};
  RankingMetrics d4{};
  bool pairwise_regret_nonregression = false;
  std::size_t heldout_origin_roots = 0;
  std::size_t purged_canonical_state_roots = 0;
  std::size_t training_roots = 0;
};

struct HalfMetrics {
  RankingMetrics actor{};
  RankingMetrics d4{};
  double hybrid_mean_score = 0.0;
  double d4_mean_score = 0.0;
  bool pairwise_regret_nonregression = false;
  bool hybrid_score_nonregression = false;
};

struct SampleEvaluation {
  int sample_size = 0;
  double independent_half_top_action_stability = 0.0;
  double half_a_full_top_fidelity = 0.0;
  double half_b_full_top_fidelity = 0.0;
  RankingMetrics actor{};
  RankingMetrics d4{};
  std::uint64_t predicted_overrides = 0;
  std::uint64_t beneficial_override_roots = 0;
  std::uint64_t true_positive_overrides = 0;
  double override_precision = 0.0;
  double override_recall = 0.0;
  int nonregressing_folds = 0;
  std::array<FoldMetrics, kExpectedGames> folds{};
  std::array<HalfMetrics, 2> halves{};
  bool stability_gate = false;
  bool precision_gate = false;
  bool recall_gate = false;
  bool pairwise_gate = false;
  bool regret_gate = false;
  bool fold_gate = false;
  bool halves_ranking_gate = false;
  bool halves_hybrid_score_gate = false;
  bool passed = false;
};

struct RootPrediction {
  std::array<double, kBoardSize> actor_a{};
  std::array<double, kBoardSize> actor_b{};
  std::array<double, kBoardSize> actor_mean{};
  int hybrid_action = -1;
};

bool rankingNonregression(const RankingMetrics& actor,
                          const RankingMetrics& baseline) {
  return actor.pairwise + kTieTolerance >= baseline.pairwise &&
         actor.normalized_regret <= baseline.normalized_regret + kTieTolerance;
}

void applyFrozenGates(SampleEvaluation& result) {
  result.stability_gate =
      result.independent_half_top_action_stability >= kStabilityMinimum;
  result.precision_gate =
      result.override_precision >= kOverridePrecisionMinimum;
  result.recall_gate = result.override_recall >= kOverrideRecallMinimum;
  result.pairwise_gate =
      result.actor.pairwise + kTieTolerance >=
      result.d4.pairwise + kPairwiseGainMinimum;
  result.regret_gate =
      result.actor.normalized_regret <=
      kRegretRatioMaximum * result.d4.normalized_regret + kTieTolerance;
  result.fold_gate =
      result.nonregressing_folds >= kRequiredNonregressingFolds;
  result.halves_ranking_gate =
      result.halves[0].pairwise_regret_nonregression &&
      result.halves[1].pairwise_regret_nonregression;
  result.halves_hybrid_score_gate =
      result.halves[0].hybrid_score_nonregression &&
      result.halves[1].hybrid_score_nonregression;
  result.passed = result.stability_gate && result.precision_gate &&
                  result.recall_gate && result.pairwise_gate &&
                  result.regret_gate && result.fold_gate &&
                  result.halves_ranking_gate &&
                  result.halves_hybrid_score_gate;
}

SampleEvaluation evaluateSample(const std::vector<RootData>& roots,
                                int sample_index) {
  SampleEvaluation result;
  result.sample_size = kSampleSizes[sample_index];
  std::vector<RootPrediction> predictions(roots.size());

  for (int fold = 0; fold < kExpectedGames; ++fold) {
    const FoldSelection selection = buildFoldSelection(roots, fold);
    result.folds[fold].heldout_origin_roots =
        selection.heldout_origin_roots;
    result.folds[fold].purged_canonical_state_roots =
        selection.purged_canonical_state_roots;
    result.folds[fold].training_roots = selection.training_roots;
    const ActorModel actor_a =
        trainActor(roots, selection, 0, sample_index, kActorSeedA);
    const ActorModel actor_b =
        trainActor(roots, selection, 1, sample_index, kActorSeedB);
    for (std::size_t index = 0; index < roots.size(); ++index) {
      const RootData& root = roots[index];
      if (root.panel.origin_slot != fold) continue;
      RootPrediction& prediction = predictions[index];
      prediction.actor_a.fill(-std::numeric_limits<double>::infinity());
      prediction.actor_b.fill(-std::numeric_limits<double>::infinity());
      prediction.actor_mean.fill(-std::numeric_limits<double>::infinity());
      for (int action = 0; action < kBoardSize; ++action) {
        if (!root.panel.actions[action].legal) continue;
        prediction.actor_a[action] = actor_a.predict(root.features[action]);
        prediction.actor_b[action] = actor_b.predict(root.features[action]);
        prediction.actor_mean[action] =
            0.5 * (prediction.actor_a[action] + prediction.actor_b[action]);
      }
      const int action_a = bestAction(prediction.actor_a, root.panel.actions);
      const int action_b = bestAction(prediction.actor_b, root.panel.actions);
      const int d1_action = root.d1_search.action;
      prediction.hybrid_action = d1_action;
      if (action_a == action_b && action_a != d1_action &&
          prediction.actor_a[action_a] - prediction.actor_a[d1_action] >=
              kActorOverrideMargin &&
          prediction.actor_b[action_b] - prediction.actor_b[d1_action] >=
              kActorOverrideMargin) {
        prediction.hybrid_action = action_a;
      }
    }
  }

  RankingAccumulator actor_overall;
  RankingAccumulator d4_overall;
  std::array<RankingAccumulator, kExpectedGames> actor_fold{};
  std::array<RankingAccumulator, kExpectedGames> d4_fold{};
  std::array<RankingAccumulator, 2> actor_half{};
  std::array<RankingAccumulator, 2> d4_half{};
  std::array<double, 2> hybrid_score{};
  std::array<double, 2> d4_score{};
  std::array<int, 2> half_roots{};
  std::uint64_t stable = 0;
  std::uint64_t fidelity_a = 0;
  std::uint64_t fidelity_b = 0;

  for (std::size_t index = 0; index < roots.size(); ++index) {
    const RootData& root = roots[index];
    const RootPrediction& prediction = predictions[index];
    const auto teacher = storedTeacher(root);
    const int full_best = bestAction(teacher, root.panel.actions);
    const int sample_a =
        bestAction(root.samples.score[0][sample_index], root.panel.actions);
    const int sample_b =
        bestAction(root.samples.score[1][sample_index], root.panel.actions);
    stable += sample_a == sample_b;
    fidelity_a += sample_a == full_best;
    fidelity_b += sample_b == full_best;

    const int fold = root.panel.origin_slot;
    const int half = fold < 4 ? 0 : 1;
    addRanking(actor_overall, prediction.actor_mean, teacher,
               root.panel.actions);
    addRanking(d4_overall, root.d4_search.root_values, teacher,
               root.panel.actions);
    addRanking(actor_fold[fold], prediction.actor_mean, teacher,
               root.panel.actions);
    addRanking(d4_fold[fold], root.d4_search.root_values, teacher,
               root.panel.actions);
    addRanking(actor_half[half], prediction.actor_mean, teacher,
               root.panel.actions);
    addRanking(d4_half[half], root.d4_search.root_values, teacher,
               root.panel.actions);

    const int d1_action = root.d1_search.action;
    const int hybrid_action = prediction.hybrid_action;
    const bool beneficial = teacher[full_best] > teacher[d1_action] +
                                                    kTieTolerance;
    result.beneficial_override_roots += beneficial;
    if (hybrid_action != d1_action) {
      ++result.predicted_overrides;
      if (teacher[hybrid_action] > teacher[d1_action] + kTieTolerance) {
        ++result.true_positive_overrides;
      }
    }
    hybrid_score[half] += teacher[hybrid_action];
    d4_score[half] += teacher[root.d4_search.action];
    ++half_roots[half];
  }

  result.independent_half_top_action_stability =
      static_cast<double>(stable) / roots.size();
  result.half_a_full_top_fidelity = static_cast<double>(fidelity_a) / roots.size();
  result.half_b_full_top_fidelity = static_cast<double>(fidelity_b) / roots.size();
  result.actor = finishRanking(actor_overall);
  result.d4 = finishRanking(d4_overall);
  result.override_precision =
      result.predicted_overrides == 0
          ? 0.0
          : static_cast<double>(result.true_positive_overrides) /
                result.predicted_overrides;
  result.override_recall =
      result.beneficial_override_roots == 0
          ? 0.0
          : static_cast<double>(result.true_positive_overrides) /
                result.beneficial_override_roots;

  for (int fold = 0; fold < kExpectedGames; ++fold) {
    result.folds[fold].actor = finishRanking(actor_fold[fold]);
    result.folds[fold].d4 = finishRanking(d4_fold[fold]);
    result.folds[fold].pairwise_regret_nonregression =
        rankingNonregression(result.folds[fold].actor, result.folds[fold].d4);
    result.nonregressing_folds +=
        result.folds[fold].pairwise_regret_nonregression;
  }
  for (int half = 0; half < 2; ++half) {
    result.halves[half].actor = finishRanking(actor_half[half]);
    result.halves[half].d4 = finishRanking(d4_half[half]);
    result.halves[half].hybrid_mean_score =
        hybrid_score[half] / half_roots[half];
    result.halves[half].d4_mean_score = d4_score[half] / half_roots[half];
    result.halves[half].pairwise_regret_nonregression =
        rankingNonregression(result.halves[half].actor,
                             result.halves[half].d4);
    result.halves[half].hybrid_score_nonregression =
        result.halves[half].hybrid_mean_score + kTieTolerance >=
        result.halves[half].d4_mean_score;
  }

  applyFrozenGates(result);
  return result;
}

void writeRanking(std::ostream& output, const RankingMetrics& value) {
  output << "{\"top1\":" << value.top1 << ",\"pairwise\":"
         << value.pairwise << ",\"normalizedRegret\":"
         << value.normalized_regret << '}';
}

void writeSample(std::ostream& output, const SampleEvaluation& value) {
  output << "{\"K\":" << value.sample_size
         << ",\"independentHalfTopActionStability\":"
         << value.independent_half_top_action_stability
         << ",\"halfAFullTopFidelity\":" << value.half_a_full_top_fidelity
         << ",\"halfBFullTopFidelity\":" << value.half_b_full_top_fidelity
         << ",\"actor\":";
  writeRanking(output, value.actor);
  output << ",\"exactD4\":";
  writeRanking(output, value.d4);
  output << ",\"overrides\":{\"predicted\":" << value.predicted_overrides
         << ",\"beneficialRoots\":" << value.beneficial_override_roots
         << ",\"truePositive\":" << value.true_positive_overrides
         << ",\"precision\":" << value.override_precision
         << ",\"recall\":" << value.override_recall << "},\"folds\":[";
  for (int fold = 0; fold < kExpectedGames; ++fold) {
    if (fold) output << ',';
    output << "{\"heldoutOrigin\":\"" << hex64(kExpectedGameStart + fold)
           << "\",\"heldoutOriginRoots\":"
           << value.folds[fold].heldout_origin_roots
           << ",\"purgedExactCanonicalPublicStateRoots\":"
           << value.folds[fold].purged_canonical_state_roots
           << ",\"trainingRootsAfterPurge\":"
           << value.folds[fold].training_roots
           << ",\"actor\":";
    writeRanking(output, value.folds[fold].actor);
    output << ",\"exactD4\":";
    writeRanking(output, value.folds[fold].d4);
    output << ",\"pairwiseRegretNonregression\":"
           << (value.folds[fold].pairwise_regret_nonregression ? "true" : "false")
           << '}';
  }
  output << "],\"fourGameHalves\":[";
  for (int half = 0; half < 2; ++half) {
    if (half) output << ',';
    output << "{\"actor\":";
    writeRanking(output, value.halves[half].actor);
    output << ",\"exactD4\":";
    writeRanking(output, value.halves[half].d4);
    output << ",\"hybridMeanScore\":" << value.halves[half].hybrid_mean_score
           << ",\"exactD4MeanScore\":" << value.halves[half].d4_mean_score
           << ",\"pairwiseRegretNonregression\":"
           << (value.halves[half].pairwise_regret_nonregression ? "true" : "false")
           << ",\"hybridScoreNonregression\":"
           << (value.halves[half].hybrid_score_nonregression ? "true" : "false")
           << '}';
  }
  output << "],\"gates\":{\"stability\":"
         << (value.stability_gate ? "true" : "false")
         << ",\"overridePrecision\":"
         << (value.precision_gate ? "true" : "false")
         << ",\"overrideRecall\":"
         << (value.recall_gate ? "true" : "false")
         << ",\"pairwiseGain\":"
         << (value.pairwise_gate ? "true" : "false")
         << ",\"regretRatio\":"
         << (value.regret_gate ? "true" : "false")
         << ",\"wholeOriginFolds\":"
         << (value.fold_gate ? "true" : "false")
         << ",\"bothFourGameHalvesRanking\":"
         << (value.halves_ranking_gate ? "true" : "false")
         << ",\"bothFourGameHalvesHybridScore\":"
         << (value.halves_hybrid_score_gate ? "true" : "false")
         << ",\"passed\":" << (value.passed ? "true" : "false") << "}}";
}

void writeIntegerArray(std::ostream& output, const std::vector<int>& values) {
  output << '[';
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index) output << ',';
    output << values[index];
  }
  output << ']';
}

void writeSamplingSelections(std::ostream& output) {
  output << '[';
  for (int sample_index = 0; sample_index < 4; ++sample_index) {
    if (sample_index) output << ',';
    output << "{\"K\":" << kSampleSizes[sample_index]
           << ",\"basePositions\":";
    writeIntegerArray(output, selectedBasePositions(sample_index));
    output << ",\"halfAScenarios\":";
    writeIntegerArray(output, selectedScenarios(0, sample_index));
    output << ",\"halfBScenarios\":";
    writeIntegerArray(output, selectedScenarios(1, sample_index));
    output << '}';
  }
  output << ']';
}

void writeArtifact(const RunOptions& options,
                   const std::array<SampleEvaluation, 4>& evaluations,
                   const EvaluatedCorpus& corpus, double wall_seconds) {
  // Repeat immediately before truncation to narrow path/symlink TOCTOU risk.
  validateDistinctInputOutput(options);
  std::ofstream output(options.output, std::ios::trunc);
  if (!output) throw std::runtime_error("could not write B0 artifact");
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"drop7-full-panel-cpi-preflight-b0\","
         << "\n  \"status\":\""
         << (evaluations.back().passed ? "passed" : "falsified") << "\","
         << "\n  \"corpus\":{\"path\":\"" << jsonEscape(options.input)
         << "\",\"sha256\":\"" << kExpectedCorpusSha256
         << "\",\"records\":" << corpus.roots.size()
         << ",\"wholeOriginGames\":" << kExpectedGames
         << ",\"newGameplaySeeds\":0,\"originTransitions\":0},"
         << "\n  \"sampling\":{\"storedScenarioCount\":"
         << kStoredScenarioCount
         << ",\"balancedDisjointHalves\":true,\"nestedK\":[7,21,35,63],"
            "\"coarseBalance\":\"exactly K/7 positions in each contiguous "
            "nine-position base block\",\"continuation\":\"exact public D1\","
            "\"horizon\":"
         << kHorizon << ",\"exactSelections\":";
  writeSamplingSelections(output);
  output << "},"
         << "\n  \"actor\":{\"publicInputs\":[\"board\",\"nextDisc\","
            "\"movesRemaining\",\"action\",\"exactPublicD1Q\"],"
            "\"forbiddenInputs\":[\"screenSeed\",\"moveIndex\","
            "\"canonicalPublicHash\",\"tapeSeed\",\"score\",\"level\","
            "\"history\",\"scenario\"],\"reflectionExact\":true,"
            "\"canonicalization\":\"joint lexicographic public-state/action; "
            "symmetric-state ties use the smaller reflected action and remap "
            "the complete D1-Q vector\","
            "\"weightsPerMember\":"
         << kActorWeights << ",\"float32BytesPerMember\":"
         << sizeof(ActorModel) << ",\"members\":2,\"epochs\":"
         << kActorEpochs << ",\"overrideMargin\":"
         << kActorOverrideMargin
         << ",\"fallback\":\"exact public D1\"},"
         << "\n  \"frozenThresholds\":{\"K63StabilityMinimum\":"
         << kStabilityMinimum << ",\"overridePrecisionMinimum\":"
         << kOverridePrecisionMinimum << ",\"overrideRecallMinimum\":"
         << kOverrideRecallMinimum << ",\"pairwiseGainOverD4Minimum\":"
         << kPairwiseGainMinimum << ",\"regretRatioToD4Maximum\":"
         << kRegretRatioMaximum << ",\"nonregressingWholeOriginFolds\":"
         << kRequiredNonregressingFolds
         << ",\"bothFourGameHalvesMustPass\":true,"
            "\"crossValidationExclusion\":\"entire heldout origin plus every "
            "other-origin root with exactly equal canonical board, nextDisc, "
            "movesRemaining, and terminal flag; no hash surrogate\"},"
         << "\n  \"metricDefinitions\":{\"pairwiseDenominator\":"
            "\"every unordered legal-action pair, including ties\","
            "\"targetTieAbsoluteTolerance\":"
         << kTieTolerance << ",\"predictionTieAbsoluteTolerance\":"
         << kPredictionTieTolerance
         << ",\"targetOrPredictionTieCredit\":0.5,"
            "\"nonTieCredit\":\"1 iff signs agree, otherwise 0\","
            "\"normalizedRegret\":\"(teacher maximum - teacher value of "
            "predicted top action)/(teacher maximum - teacher minimum), zero "
            "for a tied teacher range\"},"
         << "\n  \"evaluations\":[";
  for (int index = 0; index < 4; ++index) {
    if (index) output << ',';
    writeSample(output, evaluations[index]);
  }
  output << "],\n  \"B0Decision\":{\"usesK\":63,\"passed\":"
         << (evaluations.back().passed ? "true" : "false")
         << ",\"consequence\":\""
         << (evaluations.back().passed
                 ? "architecture may proceed to a separately preregistered training lane"
                 : "stop before opening any fresh gameplay lane")
         << "\"},"
         << "\n  \"resources\":{\"syntheticTransitions\":"
         << corpus.synthetic.transitions << ",\"syntheticTransitionLimit\":"
         << kMaximumSyntheticTransitions << ",\"exactD1Calls\":"
         << corpus.synthetic.d1_calls << ",\"exactD1Work\":"
         << corpus.synthetic.d1_work << ",\"exactD4Work\":"
         << corpus.d4_work << ",\"exactD4WorkLimit\":" << kMaximumD4Work
         << ",\"exactD4Nodes\":" << corpus.d4_nodes
         << ",\"wallSeconds\":" << wall_seconds << ",\"wallLimitSeconds\":"
         << kWallLimitSeconds << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"rssLimitBytes\":" << kRssLimitBytes << "},"
         << "\n  \"claims\":{\"architectureEvidenceOnly\":true,"
            "\"gameplayPerformanceClaim\":false,\"censorRateGate\":false,"
            "\"terminalScoreImputation\":false}\n}\n";
  if (!output) throw std::runtime_error("could not finish B0 artifact");
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

std::string fixturePanelJson(const PublicState& state) {
  std::ostringstream output;
  output << std::setprecision(12)
         << "{\"recordType\":\"deployment-panel-export-replay\","
         << "\"provenance\":{\"screenSeed\":" << kExpectedGameStart
         << ",\"moveIndex\":0,\"canonicalPublicHash\":\""
         << hex64(publicHash(state)) << "\",\"tapeSeed\":"
         << panelSeed(state) << "},\"modelInput\":{\"board\":\"";
  for (const std::uint8_t cell : state.board) output << static_cast<int>(cell);
  output << "\",\"nextDisc\":" << static_cast<int>(state.next_disc)
         << ",\"movesRemaining\":" << static_cast<int>(state.moves_remaining)
         << ",\"terminal\":false},"
            "\"excludedFromModelInput\":[\"screenSeed\",\"moveIndex\","
            "\"canonicalPublicHash\",\"tapeSeed\",\"score\",\"level\","
            "\"history\",\"scenario\"],\"gate\":\"ultra\","
            "\"fairD1Action\":3,\"selectedAction\":3,\"switched\":false,"
            "\"actions\":[";
  for (int action = 0; action < kBoardSize; ++action) {
    if (action) output << ',';
    if (!isLegal(state.board, action)) {
      output << "null";
      continue;
    }
    output << "{\"action\":" << action
           << ",\"meanScoreReturn\":" << 1000 + action
           << ",\"meanSurvivedMoves\":20,\"meanNumberedClears\":1,"
              "\"meanCoversRevealed\":2,\"survivingCutoffs\":0,"
              "\"pairedVsFairD1\":{\"score\":{\"lowerOneSided99\":0},"
              "\"moves\":{\"lowerOneSided99\":0},\"materialDownsides\":0,"
              "\"materialDownsideUpper99\":0}}";
  }
  output << "]}";
  return output.str();
}

RootData syntheticTrainingRoot(int origin) {
  RootData result;
  result.panel.origin_slot = origin;
  result.panel.origin_game = kExpectedGameStart + origin;
  result.panel.state.next_disc = static_cast<std::uint8_t>(origin % 7 + 1);
  result.panel.state.moves_remaining = static_cast<std::uint8_t>(origin % 5 + 1);
  result.panel.stored_d1_action = 3;
  result.d1_search.action = 3;
  result.d1_search.values.fill(0.0);
  for (int action = 0; action < kBoardSize; ++action) {
    result.panel.actions[action].legal = true;
    result.d1_search.values[action] = -std::abs(action - 3);
  }
  ActorInput input{result.panel.state, result.d1_search.values};
  for (int action = 0; action < kBoardSize; ++action) {
    result.features[action] = actorFeatures(input, action);
    for (int half = 0; half < 2; ++half) {
      for (int sample = 0; sample < 4; ++sample) {
        result.samples.score[half][sample][action] =
            1000.0 + 25.0 * (action == origin % 7) -
            std::abs(action - origin % 7) + half * 0.01;
      }
    }
  }
  return result;
}

void refreshSyntheticRootFeatures(RootData& root) {
  ActorInput input{root.panel.state, root.d1_search.values};
  for (int action = 0; action < kBoardSize; ++action) {
    if (root.panel.actions[action].legal) {
      root.features[action] = actorFeatures(input, action);
    }
  }
}

bool selfTest(std::ostream& output) {
  expect(sha256("") ==
             "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
         "empty SHA-256 vector failed");
  expect(sha256("abc") ==
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
         "abc SHA-256 vector failed");

  std::set<int> all_scenarios;
  for (int half = 0; half < 2; ++half) {
    const auto scenarios = scenarioHalf(half);
    expect(std::is_sorted(scenarios.begin(), scenarios.end()),
           "scenario half is not ordered");
    expect(std::set<int>(scenarios.begin(), scenarios.end()).size() ==
               kMaximumSampleSize,
           "scenario half contains duplicates");
    for (const int scenario : scenarios) {
      expect(scenario >= 0 && scenario < kStoredScenarioCount,
             "scenario out of range");
      expect(all_scenarios.insert(scenario).second,
             "independent scenario halves overlap");
    }
  }
  constexpr std::array<int, 4> maximum_base_gap{{9, 3, 3, 1}};
  constexpr std::array<int, 4> maximum_scenario_gap{{37, 13, 13, 5}};
  constexpr std::array<int, 4> maximum_boundary_gap{{20, 7, 7, 4}};
  std::array<std::set<int>, 4> position_sets;
  for (int sample_index = 0; sample_index < 4; ++sample_index) {
    const int sample_size = kSampleSizes[sample_index];
    const std::vector<int> positions = selectedBasePositions(sample_index);
    expect(static_cast<int>(positions.size()) == sample_size &&
               std::is_sorted(positions.begin(), positions.end()),
           "nested sample size/order changed");
    position_sets[sample_index] =
        std::set<int>(positions.begin(), positions.end());
    expect(static_cast<int>(position_sets[sample_index].size()) == sample_size,
           "nested sample contains duplicates");
    std::array<int, 7> coarse_counts{};
    int observed_base_gap = 0;
    for (std::size_t index = 0; index < positions.size(); ++index) {
      expect(positions[index] >= 0 && positions[index] < kMaximumSampleSize,
             "nested sample position out of range");
      ++coarse_counts[positions[index] / 9];
      if (index) {
        observed_base_gap =
            std::max(observed_base_gap, positions[index] - positions[index - 1]);
      }
    }
    for (const int count : coarse_counts) {
      expect(count == sample_size / 7,
             "nested sample coarse-stratum balance changed");
    }
    expect(observed_base_gap <= maximum_base_gap[sample_index],
           "nested sample base max-gap changed");
    for (int half = 0; half < 2; ++half) {
      const std::vector<int> scenarios = selectedScenarios(half, sample_index);
      expect(std::is_sorted(scenarios.begin(), scenarios.end()) &&
                 std::set<int>(scenarios.begin(), scenarios.end()).size() ==
                     scenarios.size(),
             "selected scenario order/uniqueness changed");
      int observed_scenario_gap = 0;
      for (std::size_t index = 1; index < scenarios.size(); ++index) {
        observed_scenario_gap = std::max(
            observed_scenario_gap, scenarios[index] - scenarios[index - 1]);
      }
      expect(observed_scenario_gap <= maximum_scenario_gap[sample_index],
             "selected scenario max-gap changed");
      expect(scenarios.front() <= maximum_boundary_gap[sample_index] &&
                 kStoredScenarioCount - 1 - scenarios.back() <=
                     maximum_boundary_gap[sample_index],
             "selected scenario boundary balance changed");
    }
  }
  for (int smaller = 0; smaller < 3; ++smaller) {
    expect(std::includes(position_sets[smaller + 1].begin(),
                         position_sets[smaller + 1].end(),
                         position_sets[smaller].begin(),
                         position_sets[smaller].end()),
           "K sample sets are not truly nested");
  }

  PublicState empty;
  empty.next_disc = 1;
  empty.moves_remaining = 5;
  const PanelRecord parsed = parsePanel(fixturePanelJson(empty));
  expect(parsed.state == empty && parsed.tape_seed == panelSeed(empty) &&
             parsed.stored_public_hash == publicHash(empty),
         "strict panel parser fixture failed");
  std::string altered = fixturePanelJson(empty);
  const std::size_t gate = altered.find("\"gate\":\"ultra\"");
  altered.replace(gate, std::string("\"gate\":\"ultra\"").size(),
                  "\"gate\":\"other\"");
  bool rejected = false;
  try {
    (void)parsePanel(altered);
  } catch (const std::exception&) {
    rejected = true;
  }
  expect(rejected, "metadata mutation was not rejected");
  rejected = false;
  try {
    (void)parseLockedCorpus(fixturePanelJson(empty));
  } catch (const std::exception&) {
    rejected = true;
  }
  expect(rejected, "checksum mutation was not rejected");

  const std::filesystem::path io_base =
      std::filesystem::temp_directory_path() /
      "drop7-b0-nonexistent-io-selftest";
  RunOptions same_io;
  same_io.input = (io_base / "corpus.jsonl").string();
  same_io.output = (io_base / "child" / ".." / "corpus.jsonl").string();
  rejected = false;
  try {
    validateDistinctInputOutput(same_io);
  } catch (const std::exception&) {
    rejected = true;
  }
  expect(rejected, "weakly-canonical identical input/output was accepted");
  same_io.output = (io_base / "artifact.json").string();
  validateDistinctInputOutput(same_io);

  PublicState state;
  state.board[indexOf(6, 0)] = 2;
  state.board[indexOf(6, 1)] = kCracked;
  state.board[indexOf(6, 4)] = 5;
  state.next_disc = 4;
  state.moves_remaining = 3;
  std::array<double, kBoardSize> q{};
  for (int action = 0; action < kBoardSize; ++action) q[action] = action * 1.25;
  ActorInput input{state, q};
  PublicState reflected = mirror(state);
  std::array<double, kBoardSize> reflected_q{};
  for (int action = 0; action < kBoardSize; ++action) {
    reflected_q[kBoardSize - 1 - action] = q[action];
  }
  ActorInput reflected_input{reflected, reflected_q};
  for (int action = 0; action < kBoardSize; ++action) {
    expect(actorFeatures(input, action) ==
               actorFeatures(reflected_input, kBoardSize - 1 - action),
           "actor features are not reflection exact");
  }

  PublicState symmetric_empty;
  symmetric_empty.next_disc = 3;
  symmetric_empty.moves_remaining = 2;
  std::array<double, kBoardSize> asymmetric_q{{
      -4.0, -2.0, -1.0, 0.25, 1.5, 3.0, 8.0,
  }};
  std::array<double, kBoardSize> asymmetric_q_reflected{};
  for (int action = 0; action < kBoardSize; ++action) {
    asymmetric_q_reflected[kBoardSize - 1 - action] = asymmetric_q[action];
  }
  const ActorInput empty_side_left{symmetric_empty, asymmetric_q};
  const ActorInput empty_side_right{symmetric_empty, asymmetric_q_reflected};
  expect(actorFeatures(empty_side_left, 0) ==
             actorFeatures(empty_side_right, kBoardSize - 1),
         "empty symmetric side-action joint canonicalization failed");
  const CanonicalActorInput empty_left_canonical =
      canonicalizeStateAction(empty_side_left, 0);
  const CanonicalActorInput empty_right_canonical =
      canonicalizeStateAction(empty_side_right, kBoardSize - 1);
  expect(empty_left_canonical.state == empty_right_canonical.state &&
             empty_left_canonical.action == empty_right_canonical.action &&
             empty_left_canonical.d1_q == empty_right_canonical.d1_q,
         "empty symmetric D1-Q remapping failed");

  std::array<double, kBoardSize> symmetric_q{{
      -3.0, -2.0, -1.0, 0.0, -1.0, -2.0, -3.0,
  }};
  const ActorInput empty_center{symmetric_empty, symmetric_q};
  const CanonicalActorInput empty_center_canonical =
      canonicalizeStateAction(empty_center, 3);
  expect(empty_center_canonical.state == symmetric_empty &&
             empty_center_canonical.action == 3 &&
             empty_center_canonical.d1_q == symmetric_q &&
             actorFeatures(empty_center, 3) ==
                 actorFeatures(ActorInput{mirror(symmetric_empty), symmetric_q},
                               3),
         "empty symmetric center-action canonicalization failed");

  PublicState symmetric_nonempty = symmetric_empty;
  symmetric_nonempty.board[indexOf(6, 0)] = 2;
  symmetric_nonempty.board[indexOf(6, 6)] = 2;
  symmetric_nonempty.board[indexOf(6, 3)] = kCracked;
  symmetric_nonempty.board[indexOf(5, 3)] = 4;
  const ActorInput nonempty_side_left{symmetric_nonempty, asymmetric_q};
  const ActorInput nonempty_side_right{
      symmetric_nonempty, asymmetric_q_reflected};
  expect(actorFeatures(nonempty_side_left, 1) ==
             actorFeatures(nonempty_side_right, kBoardSize - 1 - 1),
         "nonempty symmetric side-action joint canonicalization failed");
  const ActorInput nonempty_center{symmetric_nonempty, symmetric_q};
  expect(actorFeatures(nonempty_center, 3) ==
             actorFeatures(ActorInput{mirror(symmetric_nonempty), symmetric_q},
                           3),
         "nonempty symmetric center-action canonicalization failed");

  PanelRecord metadata_a;
  metadata_a.origin_game = 1;
  metadata_a.move_index = 2;
  metadata_a.tape_seed = 3;
  PanelRecord metadata_b = metadata_a;
  metadata_b.origin_game = 0xffff'ffffu;
  metadata_b.move_index = 999999;
  metadata_b.tape_seed = 0xdead'beefu;
  expect(actorFeatures(input, 3) == actorFeatures(input, 3) &&
             metadata_a.origin_game != metadata_b.origin_game,
         "metadata-blind actor boundary failed");

  const FairD1Decision first_d1 = chooseFairD1(state);
  const FairD1Decision mirror_d1 = chooseFairD1(reflected);
  expect(first_d1.action == kBoardSize - 1 - mirror_d1.action,
         "public D1 reflection action failed");
  for (int action = 0; action < kBoardSize; ++action) {
    expect(std::abs(first_d1.values[action] -
                    mirror_d1.values[kBoardSize - 1 - action]) < 1.0e-8,
           "public D1 reflection values failed");
  }
  SyntheticWork work_a, work_b;
  const int scenario = scenarioAt(0, 7);
  const std::uint32_t tape = panelSeed(state);
  const ScenarioOutcome outcome_a =
      rolloutScenario(state, 2, tape, scenario, work_a, nullptr, 3);
  const ScenarioOutcome outcome_b = rolloutScenario(
      reflected, kBoardSize - 1 - 2, tape, scenario, work_b, nullptr, 3);
  expect(outcome_a == outcome_b && work_a.transitions == work_b.transitions,
         "synthetic tape reflection failed");

  std::vector<RootData> training;
  for (int origin = 0; origin < kExpectedGames; ++origin) {
    training.push_back(syntheticTrainingRoot(origin));
  }
  // Make origin 3 asymmetric, then add a mirrored copy under another origin.
  // Exact canonical state grouping must purge that copy in addition to every
  // root from origin 3.  A second, distinct origin-3 root proves whole-origin
  // exclusion remains in force independently of the duplicate-state purge.
  training[3].panel.state.board[indexOf(6, 0)] = 2;
  refreshSyntheticRootFeatures(training[3]);
  RootData mirrored_duplicate = training[3];
  mirrored_duplicate.panel.origin_slot = 4;
  mirrored_duplicate.panel.origin_game = kExpectedGameStart + 4;
  mirrored_duplicate.panel.state = mirror(training[3].panel.state);
  for (int action = 0; action < kBoardSize; ++action) {
    mirrored_duplicate.d1_search.values[kBoardSize - 1 - action] =
        training[3].d1_search.values[action];
  }
  refreshSyntheticRootFeatures(mirrored_duplicate);
  RootData second_heldout = syntheticTrainingRoot(3);
  second_heldout.panel.state.board[indexOf(6, 2)] = 5;
  refreshSyntheticRootFeatures(second_heldout);
  training.push_back(mirrored_duplicate);
  training.push_back(second_heldout);

  const FoldSelection selection = buildFoldSelection(training, 3);
  expect(selection.heldout_origin_roots == 2 &&
             selection.purged_canonical_state_roots == 1 &&
             selection.training_roots == 7,
         "whole-origin/exact-state purge accounting failed");
  std::vector<PublicState> synthetic_heldout_states;
  for (const RootData& root : training) {
    if (root.panel.origin_slot == 3) {
      synthetic_heldout_states.push_back(
          exactCanonicalPublicState(root.panel.state));
    }
  }
  for (std::size_t index = 0; index < training.size(); ++index) {
    if (!selection.eligible[index]) continue;
    expect(training[index].panel.origin_slot != 3 &&
               std::find(synthetic_heldout_states.begin(),
                         synthetic_heldout_states.end(),
                         exactCanonicalPublicState(
                             training[index].panel.state)) ==
                   synthetic_heldout_states.end(),
           "heldout origin or exact heldout state leaked into training");
  }
  const ActorModel model_first =
      trainActor(training, selection, 0, 3, kActorSeedA);
  const ActorModel model_second =
      trainActor(training, selection, 0, 3, kActorSeedA);
  expect(actorFingerprint(model_first) == actorFingerprint(model_second),
         "deterministic purged whole-origin actor training failed");

  std::array<StoredAction, kBoardSize> tie_actions{};
  std::array<double, kBoardSize> tie_prediction{};
  std::array<double, kBoardSize> tie_teacher{};
  tie_prediction.fill(-std::numeric_limits<double>::infinity());
  tie_teacher.fill(-std::numeric_limits<double>::infinity());
  for (int action = 0; action < 3; ++action) tie_actions[action].legal = true;
  tie_prediction[0] = 2.0;
  tie_prediction[1] = 1.0;
  tie_prediction[2] = 1.0;
  tie_teacher[0] = 1.0;
  tie_teacher[1] = 1.0;
  tie_teacher[2] = 0.0;
  RankingAccumulator tie_ranking;
  addRanking(tie_ranking, tie_prediction, tie_teacher, tie_actions);
  expect(tie_ranking.pairs == 3 &&
             std::abs(tie_ranking.pairwise_credit - 2.0) < kTieTolerance,
         "pairwise target/prediction tie accounting changed");

  SampleEvaluation passing;
  passing.independent_half_top_action_stability = kStabilityMinimum;
  passing.override_precision = kOverridePrecisionMinimum;
  passing.override_recall = kOverrideRecallMinimum;
  passing.actor.pairwise = 0.82;
  passing.d4.pairwise = 0.80;
  passing.actor.normalized_regret = 0.09;
  passing.d4.normalized_regret = 0.10;
  passing.nonregressing_folds = 6;
  for (HalfMetrics& half : passing.halves) {
    half.pairwise_regret_nonregression = true;
    half.hybrid_score_nonregression = true;
  }
  applyFrozenGates(passing);
  expect(passing.passed, "exact frozen gate boundary should pass");
  {
    SampleEvaluation below = passing;
    below.independent_half_top_action_stability =
        std::nextafter(kStabilityMinimum, 0.0);
    applyFrozenGates(below);
    expect(!below.stability_gate && !below.passed,
           "sub-threshold stability should fail");
  }
  {
    SampleEvaluation below = passing;
    below.override_precision =
        std::nextafter(kOverridePrecisionMinimum, 0.0);
    applyFrozenGates(below);
    expect(!below.precision_gate && !below.passed,
           "sub-threshold precision should fail");
  }
  {
    SampleEvaluation below = passing;
    below.override_recall = std::nextafter(kOverrideRecallMinimum, 0.0);
    applyFrozenGates(below);
    expect(!below.recall_gate && !below.passed,
           "sub-threshold recall should fail");
  }
  {
    SampleEvaluation below = passing;
    below.actor.pairwise = below.d4.pairwise + kPairwiseGainMinimum -
                           2.0 * kTieTolerance;
    applyFrozenGates(below);
    expect(!below.pairwise_gate && !below.passed,
           "sub-threshold pairwise gain should fail");
  }
  {
    SampleEvaluation below = passing;
    below.actor.normalized_regret =
        kRegretRatioMaximum * below.d4.normalized_regret +
        2.0 * kTieTolerance;
    applyFrozenGates(below);
    expect(!below.regret_gate && !below.passed,
           "above-threshold regret ratio should fail");
  }
  {
    SampleEvaluation below = passing;
    below.nonregressing_folds = kRequiredNonregressingFolds - 1;
    applyFrozenGates(below);
    expect(!below.fold_gate && !below.passed,
           "sub-threshold whole-origin fold count should fail");
  }
  {
    SampleEvaluation below = passing;
    below.halves[1].pairwise_regret_nonregression = false;
    applyFrozenGates(below);
    expect(!below.halves_ranking_gate && !below.passed,
           "one failing ranking half should fail");
  }
  {
    SampleEvaluation below = passing;
    below.halves[0].hybrid_score_nonregression = false;
    applyFrozenGates(below);
    expect(!below.halves_hybrid_score_gate && !below.passed,
           "one failing hybrid-score half should fail");
  }
  expect(sizeof(ActorModel) <= 8ull * 1024ull &&
             kMaximumSyntheticTransitions == 84'142'800ull,
         "static resource proof changed");

  output << "FULL_PANEL_CPI_PREFLIGHT_SELF_TEST {"
         << "\"passed\":true,\"sha256\":true,\"strictParser\":true,"
            "\"checksumLock\":true,\"balancedK\":true,"
            "\"disjointHalves\":true,\"publicMetadataBlind\":true,"
            "\"jointStateActionReflectionExact\":true,"
            "\"symmetricStateActions\":true,\"syntheticTapeReflection\":true,"
            "\"wholeOriginSplit\":true,\"exactStateGroupPurge\":true,"
            "\"pairwiseTieAccounting\":true,\"distinctInputOutput\":true,"
            "\"deterministicActor\":true,\"everyGateBoundary\":true,"
            "\"gameplaySeedsOpened\":0,"
            "\"originTransitions\":0,\"actorBytesPerMember\":"
         << sizeof(ActorModel) << ",\"maximumSyntheticTransitions\":"
         << kMaximumSyntheticTransitions << ",\"peakRssBytes\":"
         << peakRssBytes() << "}\n";
  return true;
}

int run(const RunOptions& options, std::ostream& summary) {
  validateDistinctInputOutput(options);
  const Deadline deadline;
  const std::vector<PanelRecord> panels = loadLockedCorpus(options);
  const EvaluatedCorpus corpus = evaluateCorpus(panels, options.threads, deadline);
  std::array<SampleEvaluation, 4> evaluations{};
  for (int sample = 0; sample < 4; ++sample) {
    evaluations[sample] = evaluateSample(corpus.roots, sample);
    deadline.check();
  }
  writeArtifact(options, evaluations, corpus, deadline.seconds());
  const SampleEvaluation& b0 = evaluations.back();
  summary << std::fixed << std::setprecision(6)
          << "FULL_PANEL_CPI_PREFLIGHT_RESULT {\"K\":63,"
          << "\"stability\":" << b0.independent_half_top_action_stability
          << ",\"overridePrecision\":" << b0.override_precision
          << ",\"overrideRecall\":" << b0.override_recall
          << ",\"actorPairwise\":" << b0.actor.pairwise
          << ",\"d4Pairwise\":" << b0.d4.pairwise
          << ",\"actorRegret\":" << b0.actor.normalized_regret
          << ",\"d4Regret\":" << b0.d4.normalized_regret
          << ",\"nonregressingFolds\":" << b0.nonregressing_folds
          << ",\"bothHalvesRanking\":"
          << (b0.halves_ranking_gate ? "true" : "false")
          << ",\"bothHalvesHybridScore\":"
          << (b0.halves_hybrid_score_gate ? "true" : "false")
          << ",\"passed\":" << (b0.passed ? "true" : "false")
          << ",\"newGameplaySeeds\":0,\"originTransitions\":0,"
          << "\"artifact\":\"" << jsonEscape(options.output) << "\"}\n";
  return b0.passed ? 0 : 2;
}

}  // namespace drop7::full_panel_cpi_preflight

#ifndef DROP7_FULL_PANEL_CPI_PREFLIGHT_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::full_panel_cpi_preflight::selfTest(std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::full_panel_cpi_preflight::parseOptions(argc, argv, 2);
      return drop7::full_panel_cpi_preflight::run(options, std::cout);
    }
    std::cerr << "usage: drop7_full_panel_cpi_preflight --self-test | "
                 "--run [--input PATH] [--input-sha256 LOCKED_SHA256] "
                 "[--output PATH] [--threads 1..4]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_full_panel_cpi_preflight: " << error.what() << '\n';
    return 1;
  }
}
#endif
