#define DROP7_RAINBOW_NTUPLE_Q_LIBRARY
#include "rainbow-ntuple-q.cpp"
#undef DROP7_RAINBOW_NTUPLE_Q_LIBRARY

#include <future>
#include <map>
#include <set>

// Resumes high-scale Rainbow-lite training from a checksum-locked checkpoint.
// Training observations remain strictly public.  Half of all
// episodes begin at an ordinary initial board; half begin at a canonical
// public state from the fixed oracle curriculum, but use an independent
// synthetic future stream.  Oracle actions, source seeds, future tapes,
// scores, levels, move indices, histories, and recovery diagnostics are never
// parsed into the learner-facing state.
namespace drop7::flow_curriculum_rainbow {

namespace base = drop7::rainbow_ntuple_q;
namespace fair = drop7::fair_only_horizon;
using Clock = std::chrono::steady_clock;
using PublicState = base::PublicState;

constexpr std::string_view kWarmCheckpointSha256 =
    "68083e2f2f4f921fb9f8815ba832d5f7e9bec0507edd23c228a9824fb614de9d";
constexpr std::string_view kCurriculumSha256 =
    "c963ac242994e7d18020fd7369954be2f4015d7f6c972f6d5fffe79c371db226";
constexpr std::string_view kRainbowSourceSha256 =
    "4bd88e0cd65ed318c98fc712fb6c110d369a3f9831c7c5690581837ed8e26ead";
constexpr std::string_view kCurriculumSourceSha256 =
    "b5030710443415ed164f16a1b26dd9b993287f37f7b1d436767c8dcff73b1611";
constexpr std::string_view kFrozenCheckpointSha256 =
    "caa590cb29a98bb43490fe47f3e1f67a3d75180115e7dc14ff7cca252eafebf3";
constexpr std::string_view kFreezeRunSourceSha256 =
    "2d803f943d0a79698069e6294d46dabd9abb12637988f8298d9b7ebb8548a42b";
constexpr std::uint64_t kWarmFingerprint = 0x8f45'dfc3'54fd'922bull;
constexpr std::uint64_t kFrozenFingerprint = 0x9265'd0f2'8b97'000dull;
constexpr std::uint64_t kCurriculumFingerprint = 0xc649'f123'fc0c'c4b9ull;
constexpr std::uint64_t kWarmTransitions = 250'025;
constexpr int kCurriculumStates = 4'096;

constexpr std::uint32_t kTrainingSeedStart = 0x3d83'0000u;
constexpr std::uint32_t kTrainingSeedEndExclusive = 0x3d90'0000u;
constexpr std::uint32_t kStageASeedStart = 0x3d90'0000u;
constexpr int kStageAGames = 32;
constexpr int kTrainingInitialHorizon = 1'000;
constexpr int kRestartHorizon = 200;
constexpr int kEvaluationMaximumMoves = 1'000;
constexpr std::uint64_t kTargetTransitions = 16'000'000;
constexpr std::uint64_t kFreezeTrainingPeakRssBytes = 179'568'640;
constexpr std::uint64_t kFreezeGuardPeakRssBytes = 287'703'040;
constexpr std::uint64_t kEpsilonEndTransition = 2'000'000;
constexpr std::uint64_t kDiagnosticInterval = 2'000'000;
constexpr float kEpsilonStart = 0.20f;
constexpr float kEpsilonEnd = 0.02f;
constexpr std::uint32_t kLearnerDomain = 0x464c'524eu;          // FLRN
constexpr std::uint32_t kRestartSelectionDomain = 0x4652'534cu; // FRSL
constexpr std::uint32_t kRestartStreamDomain = 0x4652'5354u;    // FRST
constexpr std::uint32_t kSyntheticStreamPrefix = 0x2f00'0000u;

constexpr double kStageAMinimumScore = 400'000.0;
constexpr double kStageAMinimumMoves = 120.0;
constexpr double kStageAMinimumBottomQuartileMoves = 70.0;
constexpr double kStageAMinimumClearsPerMove = 2.10;
constexpr double kStageAMinimumRevealsPerMove = 1.15;
constexpr double kD4ScoreRatio = 1.10;
constexpr double kD4MoveRatio = 1.10;
constexpr int kMinimumJointWins = 20;

constexpr int kMaximumThreads = 4;
constexpr double kWallLimitSeconds = 90.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr std::uint64_t kCheckpointLimitBytes = 128ull * 1024ull * 1024ull;

static_assert(kLevelBonus == 17'000);
static_assert(base::kNstep == 5 && base::kGamma == 0.997f);
static_assert(base::kReplayCapacity == (1 << 17));
static_assert(base::kHashBits == 23 && base::kActiveFeatures == 259);
static_assert(kWarmTransitions < kEpsilonEndTransition);
static_assert(kEpsilonEndTransition < kTargetTransitions);
static_assert(kTargetTransitions % kDiagnosticInterval == 0);
static_assert(kTrainingSeedEndExclusive == kStageASeedStart);
static_assert(kStageASeedStart + kStageAGames == 0x3d90'0020u);
static_assert((kTrainingSeedStart >> 24u) == 0x3du &&
              (kStageASeedStart >> 24u) == 0x3du &&
              (kSyntheticStreamPrefix >> 24u) == 0x2fu);

struct Options {
  std::string warm_checkpoint = "/tmp/drop7-rainbow-ntuple-q.bin";
  std::string curriculum = "/tmp/drop7-oracle-curriculum-states.jsonl";
  std::string checkpoint = "/tmp/drop7-flow-curriculum-rainbow.bin";
  std::string output = "/tmp/drop7-flow-curriculum-rainbow.json";
  std::string readme = "/tmp/drop7-flow-curriculum-rainbow-README.md";
  std::string source_sha256;
  int threads = 4;
};

bool isSha256(std::string_view value) {
  return value.size() == 64 &&
      std::all_of(value.begin(), value.end(), [](char token) {
        return (token >= '0' && token <= '9') ||
               (token >= 'a' && token <= 'f');
      });
}

Options parseOptions(int argc, char** argv, int begin, bool require_source) {
  Options result;
  for (int index = begin; index < argc; ++index) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string_view flag(argv[index]);
    const std::string value(argv[++index]);
    if (flag == "--warm-checkpoint") result.warm_checkpoint = value;
    else if (flag == "--curriculum") result.curriculum = value;
    else if (flag == "--checkpoint") result.checkpoint = value;
    else if (flag == "--output") result.output = value;
    else if (flag == "--readme") result.readme = value;
    else if (flag == "--source-sha256") result.source_sha256 = value;
    else if (flag == "--threads") result.threads = std::stoi(value);
    else throw std::invalid_argument("unknown option " + std::string(flag));
  }
  if ((require_source && !isSha256(result.source_sha256)) ||
      result.threads < 1 || result.threads > kMaximumThreads ||
      result.warm_checkpoint.empty() || result.curriculum.empty() ||
      result.checkpoint.empty() || result.output.empty() ||
      result.readme.empty() || result.checkpoint == result.warm_checkpoint) {
    throw std::invalid_argument("invalid flow-curriculum options");
  }
  return result;
}

struct Deadline {
  Clock::time_point started = Clock::now();
  double seconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }
  void check() const {
    if (seconds() > kWallLimitSeconds) {
      throw std::runtime_error("flow curriculum exceeded 90 minute wall cap");
    }
    if (fair::peakRssBytes() > kRssLimitBytes) {
      throw std::runtime_error(
          "flow curriculum exceeded 256 MiB RSS cap: " +
          std::to_string(fair::peakRssBytes()) + " bytes");
    }
  }
};

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

class Sha256 {
 public:
  void update(const char* source, std::size_t size) {
    if (size > std::numeric_limits<std::uint64_t>::max() - bytes_) {
      throw std::runtime_error("SHA-256 input is too large");
    }
    bytes_ += static_cast<std::uint64_t>(size);
    const auto* data = reinterpret_cast<const std::uint8_t*>(source);
    if (pending_size_ != 0) {
      const std::size_t copied = std::min(size, pending_.size() - pending_size_);
      std::copy_n(data, copied, pending_.begin() + pending_size_);
      pending_size_ += copied;
      data += copied;
      size -= copied;
      if (pending_size_ != pending_.size()) return;
      compress(pending_.data());
      pending_size_ = 0;
    }
    while (size >= pending_.size()) {
      compress(data);
      data += pending_.size();
      size -= pending_.size();
    }
    std::copy_n(data, size, pending_.begin());
    pending_size_ = size;
  }

  std::string finish() {
    if (bytes_ > std::numeric_limits<std::uint64_t>::max() / 8u) {
      throw std::runtime_error("SHA-256 bit length overflow");
    }
    const std::uint64_t bits = bytes_ * 8u;
    pending_[pending_size_++] = 0x80u;
    if (pending_size_ > 56) {
      std::fill(pending_.begin() + pending_size_, pending_.end(), 0);
      compress(pending_.data());
      pending_size_ = 0;
    }
    std::fill(pending_.begin() + pending_size_, pending_.begin() + 56, 0);
    for (int byte = 7; byte >= 0; --byte) {
      pending_[56 + (7 - byte)] =
          static_cast<std::uint8_t>(bits >> (byte * 8));
    }
    compress(pending_.data());
    pending_size_ = 0;
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (std::uint32_t value : hash_) {
      for (int shift = 28; shift >= 0; shift -= 4) {
        result.push_back(digits[(value >> shift) & 0xfu]);
      }
    }
    return result;
  }

 private:
  void compress(const std::uint8_t* block) {
    std::array<std::uint32_t, 64> words{};
    for (int word = 0; word < 16; ++word) {
      const std::size_t at = static_cast<std::size_t>(4 * word);
      words[word] = (static_cast<std::uint32_t>(block[at]) << 24) |
                    (static_cast<std::uint32_t>(block[at + 1]) << 16) |
                    (static_cast<std::uint32_t>(block[at + 2]) << 8) |
                    static_cast<std::uint32_t>(block[at + 3]);
    }
    for (int word = 16; word < 64; ++word) {
      const std::uint32_t first = std::rotr(words[word - 15], 7) ^
                                  std::rotr(words[word - 15], 18) ^
                                  (words[word - 15] >> 3);
      const std::uint32_t second = std::rotr(words[word - 2], 17) ^
                                   std::rotr(words[word - 2], 19) ^
                                   (words[word - 2] >> 10);
      words[word] = words[word - 16] + first + words[word - 7] + second;
    }
    std::uint32_t a = hash_[0], b = hash_[1], c = hash_[2], d = hash_[3];
    std::uint32_t e = hash_[4], f = hash_[5], g = hash_[6], h = hash_[7];
    for (int round = 0; round < 64; ++round) {
      const std::uint32_t upper = std::rotr(e, 6) ^ std::rotr(e, 11) ^
                                  std::rotr(e, 25);
      const std::uint32_t choose = (e & f) ^ (~e & g);
      const std::uint32_t first = h + upper + choose +
                                  kSha256Constants[round] + words[round];
      const std::uint32_t lower = std::rotr(a, 2) ^ std::rotr(a, 13) ^
                                  std::rotr(a, 22);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t second = lower + majority;
      h = g; g = f; f = e; e = d + first;
      d = c; c = b; b = a; a = first + second;
    }
    hash_[0] += a; hash_[1] += b; hash_[2] += c; hash_[3] += d;
    hash_[4] += e; hash_[5] += f; hash_[6] += g; hash_[7] += h;
  }

  std::array<std::uint32_t, 8> hash_{{
      0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
  }};
  std::array<std::uint8_t, 64> pending_{};
  std::size_t pending_size_ = 0;
  std::uint64_t bytes_ = 0;
};

std::string sha256(std::string_view source) {
  Sha256 digest;
  digest.update(source.data(), source.size());
  return digest.finish();
}

std::string fileSha256(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not read " + path);
  Sha256 digest;
  std::array<char, 64 * 1024> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read = input.gcount();
    if (read > 0) digest.update(buffer.data(), static_cast<std::size_t>(read));
  }
  if (!input.eof()) throw std::runtime_error("failed reading " + path);
  return digest.finish();
}

std::size_t afterMarker(std::string_view text, std::string_view marker) {
  const std::size_t found = text.find(marker);
  if (found == std::string_view::npos) {
    throw std::runtime_error("missing locked curriculum marker " +
                             std::string(marker));
  }
  return found + marker.size();
}

std::string stringAfter(std::string_view text, std::string_view marker) {
  const std::size_t begin = afterMarker(text, marker);
  const std::size_t end = text.find('"', begin);
  if (end == std::string_view::npos) throw std::runtime_error("bad curriculum string");
  return std::string(text.substr(begin, end - begin));
}

int integerAfter(std::string_view text, std::string_view marker) {
  const std::size_t begin = afterMarker(text, marker);
  const std::string owned(text);
  char* end = nullptr;
  const long value = std::strtol(owned.c_str() + begin, &end, 10);
  if (end == owned.c_str() + begin || value < std::numeric_limits<int>::min() ||
      value > std::numeric_limits<int>::max()) {
    throw std::runtime_error("bad curriculum integer");
  }
  return static_cast<int>(value);
}

std::uint64_t mix64(std::uint64_t value) {
  value ^= value >> 30u;
  value *= 0xbf58'476d'1ce4'e5b9ull;
  value ^= value >> 27u;
  value *= 0x94d0'49bb'1331'11ebull;
  return value ^ (value >> 31u);
}

PublicState canonicalPublic(const PublicState& source, bool& mirrored) {
  const State canonical = cfpi::detail::canonicalState(base::materialize(source), mirrored);
  return base::publicState(canonical);
}

std::uint64_t publicHash(const PublicState& source) {
  bool ignored = false;
  const PublicState state = canonicalPublic(source, ignored);
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint64_t>(cell + 1u);
    hash *= 0x0000'0100'0000'01b3ull;
  }
  hash ^= state.next_disc;
  hash *= 0x0000'0100'0000'01b3ull;
  hash ^= static_cast<std::uint64_t>(state.moves_remaining + 1u);
  return mix64(hash);
}

std::uint64_t parseHex64(std::string_view value) {
  const std::string owned(value);
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(owned.c_str(), &end, 0);
  if (end == owned.c_str() || *end != '\0') {
    throw std::runtime_error("bad curriculum public hash");
  }
  return static_cast<std::uint64_t>(parsed);
}

PublicState parseCurriculumLine(std::string_view line) {
  if (line.empty() || line.front() != '{' || line.back() != '}' ||
      line.find("\"format\":\"drop7-public-restart-v1\"") ==
          std::string_view::npos ||
      line.find("\"independentRestartValidated\":true") ==
          std::string_view::npos) {
    throw std::runtime_error("bad locked curriculum record envelope");
  }
  PublicState result;
  const std::string board = stringAfter(line, "\"board\":\"");
  if (board.size() != kCellCount) throw std::runtime_error("bad curriculum board size");
  for (int cell = 0; cell < kCellCount; ++cell) {
    if (board[cell] < '0' || board[cell] > '9') {
      throw std::runtime_error("bad curriculum board token");
    }
    result.board[cell] = static_cast<std::uint8_t>(board[cell] - '0');
  }
  result.next_disc = static_cast<std::uint8_t>(
      integerAfter(line, "\"nextDisc\":"));
  result.moves_remaining = static_cast<std::uint8_t>(
      integerAfter(line, "\"movesRemaining\":"));
  if (result.next_disc < 1 || result.next_disc > kBoardSize ||
      result.moves_remaining < 1 || result.moves_remaining > kMovesPerLevel ||
      base::legalMask(result) == 0) {
    throw std::runtime_error("bad curriculum public boundary");
  }
  bool mirrored = false;
  const PublicState canonical = canonicalPublic(result, mirrored);
  if (mirrored || canonical != result ||
      publicHash(result) != parseHex64(stringAfter(line, "\"publicHash\":\""))) {
    throw std::runtime_error("curriculum reflection/hash invariant failed");
  }
  return result;
}

struct Curriculum {
  std::vector<PublicState> states;
  std::uint64_t fingerprint = 0xcbf2'9ce4'8422'2325ull;
};

Curriculum loadCurriculum(const std::string& path) {
  if (fileSha256(path) != kCurriculumSha256) {
    throw std::runtime_error("locked curriculum checksum mismatch");
  }
  std::ifstream input(path);
  if (!input) throw std::runtime_error("could not open locked curriculum");
  Curriculum result;
  std::set<std::uint64_t> hashes;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty()) continue;
    const PublicState state = parseCurriculumLine(line);
    const std::uint64_t hash = publicHash(state);
    if (!hashes.insert(hash).second) throw std::runtime_error("duplicate curriculum hash");
    // Maintain an additional public-only FNV digest.  The producer's published
    // dataset fingerprint also covers recovery diagnostics, which are
    // intentionally never parsed into this learner; SHA-256 fixes those
    // ignored bytes and all 4,096 public hashes validate the usable boundary.
    for (std::uint8_t cell : state.board) {
      result.fingerprint ^= cell;
      result.fingerprint *= 0x0000'0100'0000'01b3ull;
    }
    result.fingerprint ^= state.next_disc;
    result.fingerprint *= 0x0000'0100'0000'01b3ull;
    result.fingerprint ^= state.moves_remaining;
    result.fingerprint *= 0x0000'0100'0000'01b3ull;
    result.states.push_back(state);
  }
  if (result.states.size() != kCurriculumStates || hashes.size() != kCurriculumStates) {
    throw std::runtime_error("locked curriculum count mismatch");
  }
  return result;
}

bool allowedTrainingSeed(std::uint32_t seed) {
  return seed >= kTrainingSeedStart && seed < kTrainingSeedEndExclusive &&
         (seed >> 24u) != 0x4du && (seed >> 24u) != 0x7du &&
         (seed >> 24u) != 0xd7u;
}

bool allowedStageASeed(std::uint32_t seed) {
  return seed >= kStageASeedStart &&
         seed < kStageASeedStart + kStageAGames &&
         (seed >> 24u) != 0x4du && (seed >> 24u) != 0x7du &&
         (seed >> 24u) != 0xd7u;
}

void requireTrainingSeed(std::uint32_t seed) {
  if (!allowedTrainingSeed(seed)) {
    throw std::invalid_argument("training seed outside exact 0x3d83..0x3d8f allowlist");
  }
}

void requireStageASeed(std::uint32_t seed) {
  if (!allowedStageASeed(seed)) {
    throw std::invalid_argument("Stage-A seed outside exact 32-seed allowlist");
  }
}

std::uint32_t syntheticRestartStream(std::uint64_t restart_episode) {
  const std::uint32_t mixed = mix32(
      kRestartStreamDomain ^ static_cast<std::uint32_t>(restart_episode) ^
      static_cast<std::uint32_t>(restart_episode >> 32u));
  const std::uint32_t result = kSyntheticStreamPrefix | (mixed & 0x00ff'ffffu);
  if ((result >> 24u) == 0x4du || (result >> 24u) == 0x7du ||
      (result >> 24u) == 0xd7u || allowedTrainingSeed(result) ||
      allowedStageASeed(result)) {
    throw std::logic_error("synthetic restart stream escaped separate domain");
  }
  return result;
}

// Exact fixed fair-D4 reference, copied without semantic changes from
// fair-only-depth4.cpp.  Its SHA-256 is recorded in the artifact, and it is
// used only after the learned candidate passes every absolute Stage-A floor.
namespace exact_d4 {

constexpr int kDepth = 4;
constexpr int kChanceSamples = fair::kChanceSamples;
constexpr std::uint64_t kMaximumWork = 3'200'000;
constexpr std::size_t kMaximumCacheEntries = 60'000;
class WorkLimitReached : public std::exception {};
struct CacheEntry { double value = 0.0; std::list<std::string>::iterator order; };
struct SearchContext {
  std::unordered_map<std::string, CacheEntry> cache;
  std::list<std::string> order;
  std::uint64_t nodes = 0, work = 0, cache_hits = 0;
};
void check(const SearchContext& context) {
  if (context.work >= kMaximumWork) throw WorkLimitReached{};
}
void cacheValue(SearchContext& context, std::string key, double value) {
  const auto prior = context.cache.find(key);
  if (prior != context.cache.end()) {
    context.order.erase(prior->second.order); context.cache.erase(prior);
  }
  while (context.cache.size() >= kMaximumCacheEntries) {
    context.cache.erase(context.order.front()); context.order.pop_front();
  }
  context.order.push_back(key);
  context.cache.emplace(std::move(key),
                        CacheEntry{value, std::prev(context.order.end())});
}
double bestFutureValue(const State&, int, SearchContext&);
double evaluateAction(const State& state, int action, int depth,
                      SearchContext& context) {
  const std::uint32_t seed = cfpi::detail::scenarioSeedForState(
      state, fair::kPolicySeed, depth);
  double result = 0.0;
  for (int sample = 0; sample < kChanceSamples; ++sample) {
    check(context);
    cfpi::detail::StratifiedRandom random{seed, sample, kChanceSamples, 0};
    MoveResult move;
    const bool played = cfpi::detail::playMoveSampled(state, action, random, move);
    ++context.work;
    if (!played) { result += fair::kTerminalUtility; continue; }
    const double score = static_cast<double>(move.score_delta);
    if (move.state.game_over) { result += score + fair::kTerminalUtility; continue; }
    move.state.score = 0;
    move.state.next_disc = cfpi::detail::sampledNextDisc(seed, sample, kChanceSamples);
    bool ignored = false;
    const State next = cfpi::detail::canonicalState(move.state, ignored);
    result += score + bestFutureValue(next, depth - 1, context);
  }
  return result / kChanceSamples;
}
double bestFutureValue(const State& state, int depth, SearchContext& context) {
  ++context.nodes; check(context);
  if (state.game_over) return fair::kTerminalUtility;
  if (depth == 0) { ++context.work; return fair::fairLeaf(state); }
  const std::string key = cfpi::detail::dynamicStateKey(state, depth);
  const auto found = context.cache.find(key);
  if (found != context.cache.end()) {
    ++context.cache_hits;
    context.order.splice(context.order.end(), context.order, found->second.order);
    return found->second.value;
  }
  double best = -std::numeric_limits<double>::infinity();
  for (int action : cfpi::detail::kColumnOrder) {
    if (isLegal(state.board, action)) {
      best = std::max(best, evaluateAction(state, action, depth, context));
    }
  }
  if (!std::isfinite(best)) best = fair::kTerminalUtility;
  cacheValue(context, key, best);
  return best;
}
struct Decision {
  int action = -1;
  int completed_depth = 0;
  bool complete = false;
  std::uint64_t work = 0, nodes = 0, cache_hits = 0;
  std::size_t cache_entries = 0;
};
Decision choose(const State& source) {
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  SearchContext context;
  int completed_depth = 0, completed_action = -1;
  for (int depth = 1; depth <= kDepth; ++depth) {
    try {
      double best = -std::numeric_limits<double>::infinity();
      int action = -1;
      for (int candidate : cfpi::detail::kColumnOrder) {
        if (!isLegal(canonical.board, candidate)) continue;
        const double value = evaluateAction(canonical, candidate, depth, context);
        if (value > best) { best = value; action = candidate; }
      }
      if (action < 0) break;
      completed_action = action; completed_depth = depth;
    } catch (const WorkLimitReached&) { break; }
  }
  if (completed_action < 0) completed_action = centerFirstMove(canonical.board);
  return {mirrored ? kBoardSize - 1 - completed_action : completed_action,
          completed_depth, completed_depth == kDepth, context.work,
          context.nodes, context.cache_hits, context.cache.size()};
}

}  // namespace exact_d4

class Learner {
 public:
  explicit Learner(base::Model warm)
      : online_(std::move(warm)), random_(kLearnerDomain),
        environment_steps_(kWarmTransitions) {
    target_.weights = online_.weights;
  }

  int behaviorAction(const PublicState& state) {
    const std::uint8_t mask = base::legalMask(state);
    std::array<int, kBoardSize> legal{};
    int count = 0;
    for (int action = 0; action < kBoardSize; ++action) {
      if ((mask & (1u << action)) != 0) legal[count++] = action;
    }
    if (count == 0) return -1;
    if (random_.unit() < epsilon()) return legal[random_.bounded(count)];
    return base::greedyAction(online_, state);
  }

  void add(const base::Transition& transition) { replay_.add(transition); }
  void finishEnvironmentStep() {
    ++environment_steps_;
    if (replay_.size() >= base::kReplayWarmup &&
        environment_steps_ % base::kTrainEvery == 0) trainBatch();
  }
  float epsilon() const {
    const std::uint64_t continuation = environment_steps_ - kWarmTransitions;
    const std::uint64_t duration = kEpsilonEndTransition - kWarmTransitions;
    return base::linearSchedule(kEpsilonStart, kEpsilonEnd,
                                continuation, duration);
  }
  float beta() const {
    return base::linearSchedule(base::kPriorityBetaStart, 1.0f,
                                environment_steps_, kTargetTransitions);
  }
  const base::Model& model() const { return online_; }
  const base::LearnStats& stats() const { return stats_; }
  std::uint64_t environmentSteps() const { return environment_steps_; }
  int replaySize() const { return replay_.size(); }

 private:
  void trainBatch() {
    std::array<int, base::kBatchSize> indices{};
    std::array<float, base::kBatchSize> importance{};
    float maximum_importance = 0.0f;
    for (int sample = 0; sample < base::kBatchSize; ++sample) {
      const float unit = (static_cast<float>(sample) + random_.unit()) /
                         base::kBatchSize;
      indices[sample] = replay_.sample(unit);
      const float probability = replay_.probability(indices[sample]);
      importance[sample] = std::pow(
          std::max(1.0e-12f, replay_.size() * probability), -beta());
      maximum_importance = std::max(maximum_importance, importance[sample]);
    }
    for (float& value : importance) value /= maximum_importance;
    for (int sample = 0; sample < base::kBatchSize; ++sample) {
      const base::Transition& transition = replay_.at(indices[sample]);
      const auto q = base::ensembleValues(online_, transition.state);
      const float target = base::doubleDqnTarget(online_, target_, transition);
      const float td = target - q[transition.action];
      const float signal = importance[sample] * std::clamp(td, -1.0f, 1.0f);
      stats_.maximum_parameter_change = std::max(
          stats_.maximum_parameter_change,
          base::normalizedQUpdate(online_, transition.state,
                                  transition.action, signal));
      replay_.update(indices[sample], std::abs(td));
      stats_.absolute_td_sum += std::abs(td);
      stats_.maximum_absolute_td = std::max(
          stats_.maximum_absolute_td, static_cast<double>(std::abs(td)));
      stats_.huber_loss_sum +=
          std::abs(td) <= 1.0f ? 0.5 * td * td : std::abs(td) - 0.5;
      ++stats_.sampled_transitions;
    }
    ++stats_.batch_updates;
    if (stats_.batch_updates % base::kTargetSyncUpdates == 0) {
      target_.weights = online_.weights;
      ++stats_.target_syncs;
    }
  }

  base::Model online_{};
  base::Model target_{};
  base::Replay replay_{};
  base::Rng random_;
  base::LearnStats stats_{};
  std::uint64_t environment_steps_ = kWarmTransitions;
};

void addTransitions(Learner& learner,
                    const std::vector<base::Transition>& transitions) {
  for (const base::Transition& transition : transitions) learner.add(transition);
}

struct TrainingStats {
  std::uint64_t episodes = 0;
  std::uint64_t initial_episodes = 0;
  std::uint64_t restart_episodes = 0;
  std::uint64_t initial_transitions = 0;
  std::uint64_t restart_transitions = 0;
  std::uint64_t natural_episodes = 0;
  std::uint64_t censored_episodes = 0;
  std::uint32_t next_initial_seed = kTrainingSeedStart;
  std::uint64_t restart_number = 0;
  std::array<bool, kCurriculumStates> restart_states_seen{};
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
  std::int64_t score_sum = 0;
  std::uint64_t move_sum = 0;
  double seconds = 0.0;
};

class TrainingRun {
 public:
  TrainingRun(base::Model warm, const Curriculum& curriculum,
              const Deadline& deadline)
      : learner_(std::move(warm)), curriculum_(curriculum), deadline_(deadline) {}

  void train(std::ostream& progress) {
    const Clock::time_point started = Clock::now();
    while (learner_.environmentSteps() < kTargetTransitions) {
      deadline_.check();
      const bool restart = (stats_.episodes & 1u) != 0u;
      if (restart) trainRestartEpisode(progress);
      else trainInitialEpisode(progress);
      ++stats_.episodes;
    }
    if (learner_.environmentSteps() != kTargetTransitions) {
      throw std::logic_error("continuation did not stop at exactly 16m transitions");
    }
    stats_.seconds = std::chrono::duration<double>(Clock::now() - started).count();
  }

  const Learner& learner() const { return learner_; }
  const TrainingStats& stats() const { return stats_; }

 private:
  void observeProgress(std::ostream& progress) {
    const std::uint64_t steps = learner_.environmentSteps();
    if (steps % kDiagnosticInterval != 0) return;
    progress << std::setprecision(9)
             << "FLOW_RAINBOW_DIAGNOSTIC {\"transitions\":" << steps
             << ",\"epsilon\":" << learner_.epsilon()
             << ",\"priorityBeta\":" << learner_.beta()
             << ",\"replay\":" << learner_.replaySize()
             << ",\"episodes\":" << stats_.episodes
             << ",\"initialEpisodes\":" << stats_.initial_episodes
             << ",\"restartEpisodes\":" << stats_.restart_episodes
             << ",\"initialTransitions\":" << stats_.initial_transitions
             << ",\"restartTransitions\":" << stats_.restart_transitions
             << ",\"peakRssBytes\":" << fair::peakRssBytes() << "}\n";
  }

  void trainInitialEpisode(std::ostream& progress) {
    requireTrainingSeed(stats_.next_initial_seed);
    const std::uint32_t seed = stats_.next_initial_seed++;
    State state = initialHeadlessState(seed);
    trainEpisode(state, seed, kTrainingInitialHorizon, false, progress);
    ++stats_.initial_episodes;
  }

  void trainRestartEpisode(std::ostream& progress) {
    const std::uint32_t bits = mix32(
        kRestartSelectionDomain ^ static_cast<std::uint32_t>(stats_.restart_number) ^
        static_cast<std::uint32_t>(stats_.restart_number >> 32u));
    const std::size_t index = bits & (kCurriculumStates - 1);
    stats_.restart_states_seen[index] = true;
    State state = base::materialize(curriculum_.states[index]);
    const std::uint32_t stream = syntheticRestartStream(stats_.restart_number++);
    trainEpisode(state, stream, kRestartHorizon, true, progress);
    ++stats_.restart_episodes;
  }

  void trainEpisode(State& state, std::uint32_t stream, int horizon,
                    bool restart, std::ostream& progress) {
    base::NstepAccumulator accumulator;
    const int start_moves = state.moves_played;
    std::uint64_t episode_clears = 0, episode_reveals = 0;
    while (!state.game_over && state.moves_played - start_moves < horizon &&
           learner_.environmentSteps() < kTargetTransitions) {
      if (!restart && state.next_disc != headlessDisc(stream, state.moves_played)) {
        throw std::runtime_error("ordinary training future stream mismatch");
      }
      if (restart && state.moves_played > 0 &&
          state.next_disc != headlessDisc(stream, state.moves_played)) {
        throw std::runtime_error("restart synthetic future stream mismatch");
      }
      const PublicState observation = base::publicState(state);
      const int action = learner_.behaviorAction(observation);
      if (!isLegal(state.board, action)) {
        throw std::runtime_error("continuation behavior selected illegal action");
      }
      MoveResult move;
      if (!playHeadlessMove(state, stream, action, move)) {
        throw std::runtime_error("continuation move failed");
      }
      for (const Wave& wave : move.waves) {
        episode_clears += wave.cleared;
        episode_reveals += wave.revealed;
      }
      const base::OneStep step{
          observation, base::publicState(state),
          static_cast<float>(move.score_delta) / static_cast<float>(kLevelBonus),
          action, state.game_over};
      addTransitions(learner_, accumulator.push(step));
      learner_.finishEnvironmentStep();
      if (restart) ++stats_.restart_transitions;
      else ++stats_.initial_transitions;
      observeProgress(progress);
      if ((learner_.environmentSteps() & 0xffffu) == 0u) deadline_.check();
    }
    if (!state.game_over) {
      addTransitions(learner_, accumulator.truncate());
      ++stats_.censored_episodes;
    } else {
      if (!accumulator.empty()) throw std::logic_error("terminal n-step queue not empty");
      ++stats_.natural_episodes;
    }
    stats_.clears += episode_clears;
    stats_.reveals += episode_reveals;
    stats_.score_sum += state.score;
    stats_.move_sum += static_cast<std::uint64_t>(state.moves_played - start_moves);
  }

  Learner learner_;
  const Curriculum& curriculum_;
  const Deadline& deadline_;
  TrainingStats stats_{};
};

struct Outcome {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
  int maximum_chain = 0;
  std::uint64_t d4_work = 0;
};

void observeMove(const MoveResult& move, Outcome& result) {
  result.maximum_chain = std::max(result.maximum_chain,
                                  static_cast<int>(move.waves.size()));
  for (const Wave& wave : move.waves) {
    result.clears += wave.cleared;
    result.reveals += wave.revealed;
  }
}

Outcome evaluateLearned(std::uint32_t seed, const base::Model& model,
                        const Deadline& deadline) {
  requireStageASeed(seed);
  State state = initialHeadlessState(seed);
  Outcome result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kEvaluationMaximumMoves) {
    if ((state.moves_played & 255) == 0) deadline.check();
    if (state.next_disc != headlessDisc(seed, state.moves_played)) {
      throw std::runtime_error("candidate Stage-A future stream mismatch");
    }
    const int action = base::greedyAction(model, base::publicState(state));
    if (!isLegal(state.board, action)) throw std::runtime_error("candidate chose illegal move");
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("candidate Stage-A transition failed");
    }
    observeMove(move, result);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  return result;
}

Outcome evaluateD4(std::uint32_t seed, const Deadline& deadline) {
  requireStageASeed(seed);
  State state = initialHeadlessState(seed);
  Outcome result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kEvaluationMaximumMoves) {
    deadline.check();
    if (state.next_disc != headlessDisc(seed, state.moves_played)) {
      throw std::runtime_error("D4 Stage-A future stream mismatch");
    }
    const exact_d4::Decision decision = exact_d4::choose(state);
    if (!decision.complete || decision.completed_depth != exact_d4::kDepth ||
        !isLegal(state.board, decision.action)) {
      throw std::runtime_error("exact D4 did not complete in paired Stage A");
    }
    result.d4_work += decision.work;
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("D4 Stage-A transition failed");
    }
    observeMove(move, result);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  return result;
}

struct Summary {
  int games = 0;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double bottom_quartile_mean_moves = 0.0;
  double lower_quartile_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double mean_maximum_chain = 0.0;
  int censored = 0;
  std::int64_t minimum_score = std::numeric_limits<std::int64_t>::max();
  int minimum_moves = std::numeric_limits<int>::max();
  std::uint64_t total_d4_work = 0;
};

Summary summarize(const std::vector<Outcome>& games) {
  Summary result;
  result.games = games.size();
  std::vector<int> moves;
  std::uint64_t total_moves = 0, total_clears = 0, total_reveals = 0;
  for (const Outcome& game : games) {
    result.mean_score += static_cast<double>(game.score) / games.size();
    result.mean_moves += static_cast<double>(game.moves) / games.size();
    result.mean_maximum_chain +=
        static_cast<double>(game.maximum_chain) / games.size();
    result.censored += game.censored;
    result.minimum_score = std::min(result.minimum_score, game.score);
    result.minimum_moves = std::min(result.minimum_moves, game.moves);
    result.total_d4_work += game.d4_work;
    total_moves += game.moves; total_clears += game.clears;
    total_reveals += game.reveals; moves.push_back(game.moves);
  }
  std::sort(moves.begin(), moves.end());
  const int quartile_count = std::max(1, static_cast<int>(moves.size()) / 4);
  for (int index = 0; index < quartile_count; ++index) {
    result.bottom_quartile_mean_moves +=
        static_cast<double>(moves[index]) / quartile_count;
  }
  result.lower_quartile_moves = moves[quartile_count - 1];
  result.clears_per_move = static_cast<double>(total_clears) /
                           std::max<std::uint64_t>(1, total_moves);
  result.reveals_per_move = static_cast<double>(total_reveals) /
                            std::max<std::uint64_t>(1, total_moves);
  return result;
}

struct AbsoluteGate {
  bool score = false, moves = false, bottom_quartile = false;
  bool clears = false, reveals = false, resources = false, passed = false;
};

AbsoluteGate absoluteGate(const Summary& value) {
  AbsoluteGate result;
  result.score = value.mean_score >= kStageAMinimumScore;
  result.moves = value.mean_moves >= kStageAMinimumMoves;
  result.bottom_quartile =
      value.bottom_quartile_mean_moves >= kStageAMinimumBottomQuartileMoves;
  result.clears = value.clears_per_move >= kStageAMinimumClearsPerMove;
  result.reveals = value.reveals_per_move >= kStageAMinimumRevealsPerMove;
  result.resources = fair::peakRssBytes() <= kRssLimitBytes;
  result.passed = result.score && result.moves && result.bottom_quartile &&
                  result.clears && result.reveals && result.resources;
  return result;
}

struct FinalGate {
  bool score_ratio = false, move_ratio = false;
  bool joint_wins = false, clears_nonregression = false;
  bool reveals_nonregression = false, resources = false, passed = false;
  int joint_win_count = 0;
};

FinalGate finalGate(const std::vector<Outcome>& candidate,
                    const Summary& candidate_summary,
                    const std::vector<Outcome>& d4,
                    const Summary& d4_summary,
                    const AbsoluteGate& absolute) {
  FinalGate result;
  result.score_ratio =
      candidate_summary.mean_score >= kD4ScoreRatio * d4_summary.mean_score;
  result.move_ratio =
      candidate_summary.mean_moves >= kD4MoveRatio * d4_summary.mean_moves;
  for (std::size_t index = 0; index < candidate.size(); ++index) {
    if (candidate[index].seed != d4[index].seed) {
      throw std::runtime_error("paired Stage-A seed mismatch");
    }
    result.joint_win_count +=
        candidate[index].score > d4[index].score &&
        candidate[index].moves > d4[index].moves;
  }
  result.joint_wins = result.joint_win_count >= kMinimumJointWins;
  result.clears_nonregression =
      candidate_summary.clears_per_move >= d4_summary.clears_per_move;
  result.reveals_nonregression =
      candidate_summary.reveals_per_move >= d4_summary.reveals_per_move;
  result.resources = fair::peakRssBytes() <= kRssLimitBytes;
  result.passed = absolute.passed && result.score_ratio && result.move_ratio &&
                  result.joint_wins && result.clears_nonregression &&
                  result.reveals_nonregression && result.resources;
  return result;
}

struct Preflight {
  base::Model warm;
  Curriculum curriculum;
  std::uint64_t checkpoint_steps = 0;
  std::uint64_t checkpoint_fingerprint = 0;
  double seconds = 0.0;
};

struct FrozenResume {
  base::Model model;
  Curriculum curriculum;
  std::uint64_t checkpoint_steps = 0;
  std::uint64_t checkpoint_fingerprint = 0;
  std::string checkpoint_sha256;
  double seconds = 0.0;
};

base::CheckpointHeader readCheckpointHeader(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not read frozen checkpoint header");
  base::CheckpointHeader header;
  input.read(reinterpret_cast<char*>(&header), sizeof(header));
  if (!input) throw std::runtime_error("truncated frozen checkpoint header");
  return header;
}

bool seedGuardsPass() {
  if (!allowedTrainingSeed(kTrainingSeedStart) ||
      !allowedTrainingSeed(kTrainingSeedEndExclusive - 1) ||
      allowedTrainingSeed(kTrainingSeedStart - 1) ||
      allowedTrainingSeed(kTrainingSeedEndExclusive) ||
      !allowedStageASeed(kStageASeedStart) ||
      !allowedStageASeed(kStageASeedStart + kStageAGames - 1) ||
      allowedStageASeed(kStageASeedStart + kStageAGames) ||
      (syntheticRestartStream(0) >> 24u) != 0x2fu) {
    return false;
  }
  for (std::uint32_t forbidden :
       {0x4d00'0000u, 0x7d00'0000u, 0xd700'0000u}) {
    if (allowedTrainingSeed(forbidden) || allowedStageASeed(forbidden)) {
      return false;
    }
  }
  return true;
}

FrozenResume frozenResumePreflight(const Options& options,
                                   const Deadline& deadline) {
  const Clock::time_point started = Clock::now();
  const std::string sha_before = fileSha256(options.checkpoint);
  if (sha_before != kFrozenCheckpointSha256) {
    throw std::runtime_error(
        "frozen 16m checkpoint checksum mismatch; stop before Stage A");
  }
  constexpr std::uint64_t expected_bytes =
      sizeof(base::CheckpointHeader) + base::kHashBuckets * sizeof(float);
  static_assert(expected_bytes == 33'554'472);
  if (base::fileBytes(options.checkpoint) != expected_bytes) {
    throw std::runtime_error(
        "frozen 16m checkpoint byte count mismatch; stop before Stage A");
  }
  const base::CheckpointHeader header = readCheckpointHeader(options.checkpoint);
  if (header.magic != base::kCheckpointMagic || header.version != 1 ||
      header.level_bonus != kLevelBonus || header.buckets != base::kHashBuckets ||
      header.environment_steps != kTargetTransitions ||
      header.fingerprint != kFrozenFingerprint) {
    throw std::runtime_error(
        "frozen 16m checkpoint header mismatch; stop before Stage A");
  }
  Curriculum curriculum = loadCurriculum(options.curriculum);
  auto restored = base::readCheckpoint(options.checkpoint);
  const std::uint64_t fingerprint_before = base::modelFingerprint(restored.first);
  if (restored.second != kTargetTransitions ||
      fingerprint_before != kFrozenFingerprint) {
    throw std::runtime_error(
        "frozen 16m checkpoint payload mismatch; stop before Stage A");
  }
  const PublicState& fixture = curriculum.states.front();
  const auto direct = base::ensembleValues(restored.first, fixture);
  const auto reflected =
      base::ensembleValues(restored.first, base::mirrorState(fixture));
  for (int action = 0; action < kBoardSize; ++action) {
    if (direct[action] != reflected[kBoardSize - 1 - action]) {
      throw std::runtime_error(
          "frozen model reflection mismatch; stop before Stage A");
    }
  }
  State poisoned = base::materialize(fixture);
  poisoned.score = 99'999'999;
  poisoned.level = 777;
  poisoned.moves_played = 888;
  if (base::publicState(poisoned) != fixture || !seedGuardsPass()) {
    throw std::runtime_error(
        "resume public/seed boundary mismatch; stop before Stage A");
  }
  const std::uint64_t fingerprint_after = base::modelFingerprint(restored.first);
  const std::string sha_after = fileSha256(options.checkpoint);
  if (fingerprint_after != fingerprint_before || sha_after != sha_before) {
    throw std::runtime_error(
        "frozen checkpoint changed during read-only preflight");
  }
  deadline.check();
  return {std::move(restored.first), std::move(curriculum), restored.second,
          fingerprint_after, sha_after,
          std::chrono::duration<double>(Clock::now() - started).count()};
}

void replayStress(const PublicState& state) {
  base::Replay replay;
  for (int index = 0; index < 60'000; ++index) {
    base::Transition transition;
    transition.state = state;
    transition.next = state;
    transition.action = static_cast<std::uint8_t>(index % kBoardSize);
    replay.add(transition);
    replay.update(index, static_cast<float>((index % 10'000) + 1));
  }
  for (int sample = 0; sample < 10'000; ++sample) {
    const float unit = static_cast<float>(
        static_cast<double>(mix32(static_cast<std::uint32_t>(sample))) /
        4'294'967'296.0);
    const int index = replay.sample(unit);
    if (index < 0 || index >= replay.size() || replay.probability(index) <= 0.0f) {
      throw std::runtime_error("partial replay-tree stress failed");
    }
  }
}

Preflight productionPreflight(const Options& options, const Deadline& deadline) {
  const Clock::time_point started = Clock::now();
  if (fileSha256(options.warm_checkpoint) != kWarmCheckpointSha256) {
    throw std::runtime_error("warm checkpoint checksum mismatch; stop before seeds");
  }
  Curriculum curriculum = loadCurriculum(options.curriculum);
  // Treat the producer artifact's fingerprint as an independent fixed
  // reference. SHA-256 plus all 4,096 public hashes is authoritative here.
  const PublicState fixture = curriculum.states.front();
  // Stress the partially populated sum tree before allocating the two-model
  // learner. This preserves the hard RSS cap under sanitizer instrumentation.
  replayStress(fixture);
  auto restored = base::readCheckpoint(options.warm_checkpoint);
  if (restored.second != kWarmTransitions ||
      base::modelFingerprint(restored.first) != kWarmFingerprint) {
    throw std::runtime_error("warm checkpoint metadata mismatch; stop before seeds");
  }
  const auto direct = base::ensembleValues(restored.first, fixture);
  const auto reflected = base::ensembleValues(restored.first, base::mirrorState(fixture));
  for (int action = 0; action < kBoardSize; ++action) {
    if (direct[action] != reflected[kBoardSize - 1 - action]) {
      throw std::runtime_error("warm model reflection preflight failed");
    }
  }
  State poisoned = base::materialize(fixture);
  poisoned.score = 99'999'999;
  poisoned.level = 777;
  poisoned.moves_played = 888;
  if (base::publicState(poisoned) != fixture) {
    throw std::runtime_error("public metadata boundary preflight failed");
  }
  if (!allowedTrainingSeed(kTrainingSeedStart) ||
      !allowedTrainingSeed(kTrainingSeedEndExclusive - 1) ||
      allowedTrainingSeed(kTrainingSeedStart - 1) ||
      allowedTrainingSeed(kTrainingSeedEndExclusive) ||
      !allowedStageASeed(kStageASeedStart) ||
      !allowedStageASeed(kStageASeedStart + kStageAGames - 1) ||
      allowedStageASeed(kStageASeedStart + kStageAGames) ||
      (syntheticRestartStream(0) >> 24u) != 0x2fu) {
    throw std::runtime_error("seed allowlist preflight failed");
  }
  for (std::uint32_t forbidden :
       {0x4d00'0000u, 0x7d00'0000u, 0xd700'0000u}) {
    if (allowedTrainingSeed(forbidden) || allowedStageASeed(forbidden)) {
      throw std::runtime_error("forbidden seed family passed preflight");
    }
  }
  const std::uint64_t runtime_estimate =
      2ull * base::kHashBuckets * sizeof(float) +
      static_cast<std::uint64_t>(base::kReplayCapacity) * sizeof(base::Transition) +
      2ull * base::kReplayCapacity * sizeof(double) +
      kCurriculumStates * sizeof(PublicState);
  if (runtime_estimate >= kRssLimitBytes) {
    throw std::runtime_error("static runtime estimate exceeds RSS cap");
  }
  deadline.check();
  return {std::move(restored.first), std::move(curriculum), restored.second,
          kWarmFingerprint,
          std::chrono::duration<double>(Clock::now() - started).count()};
}

void writeSummary(std::ostream& output, const Summary& value) {
  output << "{\"games\":" << value.games << ",\"meanScore\":"
         << value.mean_score << ",\"meanMoves\":" << value.mean_moves
         << ",\"bottomQuartileMeanMoves\":"
         << value.bottom_quartile_mean_moves
         << ",\"lowerQuartileMoves\":" << value.lower_quartile_moves
         << ",\"clearsPerMove\":" << value.clears_per_move
         << ",\"revealsPerMove\":" << value.reveals_per_move
         << ",\"meanMaximumChain\":" << value.mean_maximum_chain
         << ",\"minimumScore\":" << value.minimum_score
         << ",\"minimumMoves\":" << value.minimum_moves
         << ",\"censored\":" << value.censored
         << ",\"exactD4Work\":" << value.total_d4_work << '}';
}

void writeArtifact(const Options& options, const Preflight& preflight,
                   const TrainingRun& training,
                   const std::vector<Outcome>& candidate,
                   const Summary& candidate_summary,
                   const AbsoluteGate& absolute,
                   const std::optional<std::vector<Outcome>>& d4,
                   const std::optional<Summary>& d4_summary,
                   const std::optional<FinalGate>& final,
                   double candidate_seconds, double d4_seconds,
                   double total_seconds) {
  const TrainingStats& stats = training.stats();
  const base::LearnStats& learned = training.learner().stats();
  const int unique_restarts = std::accumulate(
      stats.restart_states_seen.begin(), stats.restart_states_seen.end(), 0);
  std::ofstream output(options.output, std::ios::trunc);
  if (!output) throw std::runtime_error("could not write flow artifact");
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"flow-curriculum-rainbow-16m\","
         << "\n  \"status\":\""
         << (!absolute.passed ? "stopped-at-absolute-stage-a-floor"
                              : (final->passed ? "paired-D4-gate-passed"
                                               : "stopped-at-paired-D4-gate"))
         << "\","
         << "\n  \"scope\":{\"developmentOnly\":true,"
            "\"warmStartOnly\":true,\"intermediateGameplayEvaluations\":0,"
            "\"intermediateCheckpoints\":0,\"checkpointFreezeTransitions\":"
         << kTargetTransitions << ",\"opened4dSeeds\":0,\"opened7dSeeds\":0,"
            "\"openedD7Seeds\":0},"
         << "\n  \"hashes\":{\"sourceSha256\":\"" << options.source_sha256
         << "\",\"warmCheckpointSha256\":\"" << kWarmCheckpointSha256
         << "\",\"curriculumSha256\":\"" << kCurriculumSha256
         << "\",\"rainbowSourceSha256\":\"" << kRainbowSourceSha256
         << "\",\"curriculumSourceSha256\":\"" << kCurriculumSourceSha256
         << "\",\"frozenCheckpointSha256\":\""
         << fileSha256(options.checkpoint) << "\"},"
         << "\n  \"preflight\":{\"passed\":true,\"gameplaySeedsOpened\":0,"
            "\"warmTransitions\":" << preflight.checkpoint_steps
         << ",\"warmFingerprint\":\"0x" << std::hex
         << preflight.checkpoint_fingerprint << std::dec
         << "\",\"curriculumStates\":" << preflight.curriculum.states.size()
         << ",\"producerDatasetFingerprint\":\"0x" << std::hex
         << kCurriculumFingerprint << std::dec
         << "\",\"checksumParser\":true,\"partialReplayTreeStress\":true,"
            "\"restartBoundary\":true,\"reflection\":true,"
            "\"metadataBlind\":true,\"seedGuards\":true,\"seconds\":"
         << preflight.seconds << "},"
         << "\n  \"observation\":{\"modelInputs\":[\"public board\","
            "\"visible next disc\",\"moves remaining\",\"candidate action\"],"
            "\"excluded\":[\"oracle action\",\"oracle recovery diagnostic\","
            "\"origin seed\",\"future tape\",\"score\",\"level\","
            "\"move index\",\"history\"],\"reflection\":"
            "\"exact direct/mirrored action-value mean\"},"
         << "\n  \"learner\":{\"model\":\"frozen public hashed n-tuple Q\","
            "\"reward\":\"unclipped scoreDelta/17000\",\"doubleDqn\":true,"
            "\"nStep\":" << base::kNstep << ",\"gamma\":" << base::kGamma
         << ",\"prioritizedReplay\":true,\"replayCapacity\":"
         << base::kReplayCapacity << ",\"epsilon\":{\"start\":"
         << kEpsilonStart << ",\"end\":" << kEpsilonEnd
         << ",\"endAtTotalTransition\":" << kEpsilonEndTransition << "}},"
         << "\n  \"training\":{\"totalEnvironmentTransitions\":"
         << training.learner().environmentSteps()
         << ",\"newTransitions\":"
         << training.learner().environmentSteps() - kWarmTransitions
         << ",\"episodes\":" << stats.episodes
         << ",\"initialEpisodes\":" << stats.initial_episodes
         << ",\"restartEpisodes\":" << stats.restart_episodes
         << ",\"initialTransitions\":" << stats.initial_transitions
         << ",\"restartTransitions\":" << stats.restart_transitions
         << ",\"uniqueRestartStates\":" << unique_restarts
         << ",\"restartHorizon\":" << kRestartHorizon
         << ",\"ordinaryHorizon\":" << kTrainingInitialHorizon
         << ",\"nextUnopenedTrainingSeed\":" << stats.next_initial_seed
         << ",\"finalEpsilon\":" << training.learner().epsilon()
         << ",\"finalPriorityBeta\":" << training.learner().beta()
         << ",\"replaySize\":" << training.learner().replaySize()
         << ",\"batchUpdates\":" << learned.batch_updates
         << ",\"sampledReplayTransitions\":" << learned.sampled_transitions
         << ",\"targetSyncs\":" << learned.target_syncs
         << ",\"meanAbsoluteTd\":"
         << learned.absolute_td_sum / std::max<std::uint64_t>(1, learned.sampled_transitions)
         << ",\"maximumAbsoluteTd\":" << learned.maximum_absolute_td
         << ",\"maximumParameterChange\":" << learned.maximum_parameter_change
         << ",\"seconds\":" << stats.seconds
         << ",\"transitionsPerSecond\":"
         << (training.learner().environmentSteps() - kWarmTransitions) / stats.seconds
         << "},"
         << "\n  \"stageA\":{\"seedStart\":" << kStageASeedStart
         << ",\"games\":" << kStageAGames
         << ",\"maximumMoves\":" << kEvaluationMaximumMoves
         << ",\"candidateEvaluatedFirst\":true,\"candidate\":";
  writeSummary(output, candidate_summary);
  output << ",\"absoluteGate\":{\"scoreFloor\":" << kStageAMinimumScore
         << ",\"movesFloor\":" << kStageAMinimumMoves
         << ",\"bottomQuartileMeanMovesFloor\":"
         << kStageAMinimumBottomQuartileMoves
         << ",\"clearsPerMoveFloor\":" << kStageAMinimumClearsPerMove
         << ",\"revealsPerMoveFloor\":" << kStageAMinimumRevealsPerMove
         << ",\"scorePassed\":" << (absolute.score ? "true" : "false")
         << ",\"movesPassed\":" << (absolute.moves ? "true" : "false")
         << ",\"bottomQuartilePassed\":"
         << (absolute.bottom_quartile ? "true" : "false")
         << ",\"clearsPassed\":" << (absolute.clears ? "true" : "false")
         << ",\"revealsPassed\":" << (absolute.reveals ? "true" : "false")
         << ",\"passed\":" << (absolute.passed ? "true" : "false") << "},"
         << "\"exactD4Opened\":" << (d4.has_value() ? "true" : "false");
  if (d4.has_value()) {
    output << ",\"exactD4\":"; writeSummary(output, *d4_summary);
    output << ",\"pairedGate\":{\"minimumScoreRatio\":" << kD4ScoreRatio
           << ",\"minimumMoveRatio\":" << kD4MoveRatio
           << ",\"minimumJointWins\":" << kMinimumJointWins
           << ",\"jointWins\":" << final->joint_win_count
           << ",\"scoreRatioPassed\":" << (final->score_ratio ? "true" : "false")
           << ",\"moveRatioPassed\":" << (final->move_ratio ? "true" : "false")
           << ",\"jointWinsPassed\":" << (final->joint_wins ? "true" : "false")
           << ",\"clearsNonregression\":"
           << (final->clears_nonregression ? "true" : "false")
           << ",\"revealsNonregression\":"
           << (final->reveals_nonregression ? "true" : "false")
           << ",\"passed\":" << (final->passed ? "true" : "false") << '}';
  }
  output << ",\"candidateSeconds\":" << candidate_seconds
         << ",\"d4Seconds\":" << d4_seconds << "},"
         << "\n  \"checkpoint\":{\"path\":\"" << options.checkpoint
         << "\",\"bytes\":" << base::fileBytes(options.checkpoint)
         << ",\"environmentTransitions\":" << kTargetTransitions
         << ",\"fingerprint\":\"0x" << std::hex
         << base::modelFingerprint(training.learner().model()) << std::dec << "\"},"
         << "\n  \"tests\":{\"strictWerror\":true,\"asanUbsan\":true,"
            "\"curriculumChecksum\":true,\"checkpointChecksum\":true,"
            "\"partialReplayTree\":true,\"restartPublicBoundary\":true,"
            "\"reflection\":true,\"metadataBlind\":true,"
            "\"seedGuards\":true,\"productionNoSeedPreflight\":true},"
         << "\n  \"resources\":{\"totalSeconds\":" << total_seconds
         << ",\"peakRssBytes\":" << fair::peakRssBytes()
         << ",\"rssLimitBytes\":" << kRssLimitBytes
         << ",\"wallLimitSeconds\":" << kWallLimitSeconds << "}\n}\n";
  if (!output) throw std::runtime_error("flow artifact write failed");
  static_cast<void>(candidate);
}

void writeOutcomes(std::ostream& output, const std::vector<Outcome>& outcomes) {
  output << '[';
  for (std::size_t index = 0; index < outcomes.size(); ++index) {
    if (index != 0) output << ',';
    const Outcome& game = outcomes[index];
    output << "{\"seed\":" << game.seed << ",\"score\":" << game.score
           << ",\"moves\":" << game.moves << ",\"censored\":"
           << (game.censored ? "true" : "false")
           << ",\"clears\":" << game.clears
           << ",\"reveals\":" << game.reveals
           << ",\"maximumChain\":" << game.maximum_chain
           << ",\"exactD4Work\":" << game.d4_work << '}';
  }
  output << ']';
}

void writeResumeArtifact(const Options& options, const FrozenResume& frozen,
                         const std::vector<Outcome>& candidate,
                         const Summary& candidate_summary,
                         const AbsoluteGate& absolute,
                         const std::optional<std::vector<Outcome>>& d4,
                         const std::optional<Summary>& d4_summary,
                         const std::optional<FinalGate>& final,
                         double candidate_seconds, double d4_seconds,
                         double total_seconds) {
  std::ofstream output(options.output, std::ios::trunc);
  if (!output) throw std::runtime_error("could not write resume artifact");
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"flow-curriculum-rainbow-16m-resume\","
         << "\n  \"status\":\""
         << (!absolute.passed ? "stopped-at-absolute-stage-a-floor"
                              : (final->passed ? "paired-D4-gate-passed"
                                               : "stopped-at-paired-D4-gate"))
         << "\","
         << "\n  \"recovery\":{\"mode\":\"immutable-frozen-checkpoint\","
            "\"retrained\":false,\"weightsUpdated\":false,"
            "\"originalFreezeSourceSha256\":\""
         << kFreezeRunSourceSha256
         << "\",\"originalFailure\":\"post-freeze RSS guard\","
            "\"trainingPeakRssBytes\":" << kFreezeTrainingPeakRssBytes
         << ",\"guardPeakRssBytes\":" << kFreezeGuardPeakRssBytes << "},"
         << "\n  \"scope\":{\"developmentOnly\":true,"
            "\"intermediateGameplayEvaluations\":0,"
            "\"checkpointFreezeTransitions\":" << kTargetTransitions
         << ",\"opened4dSeeds\":0,\"opened7dSeeds\":0,"
            "\"openedD7Seeds\":0},"
         << "\n  \"hashes\":{\"resumeSourceSha256\":\""
         << options.source_sha256 << "\",\"warmCheckpointSha256\":\""
         << kWarmCheckpointSha256 << "\",\"curriculumSha256\":\""
         << kCurriculumSha256 << "\",\"frozenCheckpointSha256\":\""
         << frozen.checkpoint_sha256 << "\"},"
         << "\n  \"resumePreflight\":{\"passed\":true,"
            "\"gameplaySeedsOpened\":0,\"checkpointReadOnly\":true,"
            "\"weightsModified\":false,\"streamingSha256\":true,"
            "\"exactHeader\":true,\"exactPayloadFingerprint\":true,"
            "\"reflection\":true,\"metadataBlind\":true,"
            "\"seedGuards\":true,\"curriculumStates\":"
         << frozen.curriculum.states.size() << ",\"seconds\":"
         << frozen.seconds << "},"
         << "\n  \"trainingLineage\":{\"totalEnvironmentTransitions\":"
         << kTargetTransitions << ",\"warmTransitions\":" << kWarmTransitions
         << ",\"newTransitions\":" << kTargetTransitions - kWarmTransitions
         << ",\"episodes\":413797,\"initialEpisodes\":206899,"
            "\"restartEpisodes\":206898,\"initialTransitions\":8237448,"
            "\"restartTransitions\":7512527,\"finalEpsilon\":0.02,"
            "\"finalPriorityBeta\":1,\"replaySize\":131072},"
         << "\n  \"stageA\":{\"seedStart\":" << kStageASeedStart
         << ",\"games\":" << kStageAGames
         << ",\"maximumMoves\":" << kEvaluationMaximumMoves
         << ",\"candidateEvaluatedFirst\":true,\"candidate\":";
  writeSummary(output, candidate_summary);
  output << ",\"candidateGames\":";
  writeOutcomes(output, candidate);
  output << ",\"absoluteGate\":{\"scoreFloor\":" << kStageAMinimumScore
         << ",\"movesFloor\":" << kStageAMinimumMoves
         << ",\"bottomQuartileMeanMovesFloor\":"
         << kStageAMinimumBottomQuartileMoves
         << ",\"clearsPerMoveFloor\":" << kStageAMinimumClearsPerMove
         << ",\"revealsPerMoveFloor\":" << kStageAMinimumRevealsPerMove
         << ",\"scorePassed\":" << (absolute.score ? "true" : "false")
         << ",\"movesPassed\":" << (absolute.moves ? "true" : "false")
         << ",\"bottomQuartilePassed\":"
         << (absolute.bottom_quartile ? "true" : "false")
         << ",\"clearsPassed\":" << (absolute.clears ? "true" : "false")
         << ",\"revealsPassed\":" << (absolute.reveals ? "true" : "false")
         << ",\"passed\":" << (absolute.passed ? "true" : "false") << "},"
         << "\"exactD4Opened\":" << (d4.has_value() ? "true" : "false");
  if (d4.has_value()) {
    output << ",\"exactD4\":";
    writeSummary(output, *d4_summary);
    output << ",\"exactD4Games\":";
    writeOutcomes(output, *d4);
    output << ",\"pairedGate\":{\"minimumScoreRatio\":" << kD4ScoreRatio
           << ",\"minimumMoveRatio\":" << kD4MoveRatio
           << ",\"minimumJointWins\":" << kMinimumJointWins
           << ",\"jointWins\":" << final->joint_win_count
           << ",\"scoreRatioPassed\":"
           << (final->score_ratio ? "true" : "false")
           << ",\"moveRatioPassed\":"
           << (final->move_ratio ? "true" : "false")
           << ",\"jointWinsPassed\":"
           << (final->joint_wins ? "true" : "false")
           << ",\"clearsNonregression\":"
           << (final->clears_nonregression ? "true" : "false")
           << ",\"revealsNonregression\":"
           << (final->reveals_nonregression ? "true" : "false")
           << ",\"passed\":" << (final->passed ? "true" : "false") << '}';
  }
  output << ",\"candidateSeconds\":" << candidate_seconds
         << ",\"d4Seconds\":" << d4_seconds << "},"
         << "\n  \"checkpoint\":{\"path\":\"" << options.checkpoint
         << "\",\"bytes\":" << base::fileBytes(options.checkpoint)
         << ",\"environmentTransitions\":" << frozen.checkpoint_steps
         << ",\"fingerprint\":\"0x" << std::hex
         << frozen.checkpoint_fingerprint << std::dec << "\"},"
         << "\n  \"tests\":{\"strictWerror\":true,\"asanUbsan\":true,"
            "\"streamingCheckpointChecksum\":true,"
            "\"exactCheckpointHeader\":true,"
            "\"exactCheckpointFingerprint\":true,"
            "\"checkpointReadOnly\":true,\"reflection\":true,"
            "\"metadataBlind\":true,\"seedGuards\":true,"
            "\"productionNoSeedResumePreflight\":true},"
         << "\n  \"resources\":{\"totalSeconds\":" << total_seconds
         << ",\"peakRssBytes\":" << fair::peakRssBytes()
         << ",\"rssLimitBytes\":" << kRssLimitBytes
         << ",\"wallLimitSeconds\":" << kWallLimitSeconds << "}\n}\n";
  if (!output) throw std::runtime_error("resume artifact write failed");
}

void writeReadme(const Options& options, const Summary& candidate,
                 const AbsoluteGate& absolute,
                 const std::optional<Summary>& d4,
                 const std::optional<FinalGate>& final) {
  std::ofstream output(options.readme, std::ios::trunc);
  if (!output) throw std::runtime_error("could not write flow README snippet");
  output << std::fixed << std::setprecision(3)
         << "## Flow-curriculum Rainbow continuation\n\n"
         << "The checksum-locked 250,025-transition n-tuple checkpoint was "
            "continued to exactly 16,000,000 total transitions with alternating "
            "ordinary and public-restart episodes. No intermediate gameplay "
            "evaluation or checkpoint selection occurred.\n\n"
         << "Candidate Stage A: score " << candidate.mean_score << ", moves "
         << candidate.mean_moves << ", bottom-quartile mean moves "
         << candidate.bottom_quartile_mean_moves << ", clears/move "
         << candidate.clears_per_move << ", reveals/move "
         << candidate.reveals_per_move << ". Absolute gate: **"
         << (absolute.passed ? "pass" : "fail") << "**.\n";
  if (d4.has_value()) {
    output << "Exact D4: score " << d4->mean_score << ", moves "
           << d4->mean_moves << ", clears/move " << d4->clears_per_move
           << ", reveals/move " << d4->reveals_per_move
           << ". Final paired gate: **" << (final->passed ? "pass" : "fail")
           << "** (joint wins " << final->joint_win_count << "/32).\n";
  } else {
    output << "The absolute floor failed, so exact D4 was not opened.\n";
  }
  output << "\nSee `" << options.output
         << "` for checksums, training coverage, diagnostics, gates, and resources.\n";
}

bool selfTest(const Options& options, std::ostream& output) {
  const Deadline deadline;
  const Preflight preflight = productionPreflight(options, deadline);
  const float initial_epsilon = base::linearSchedule(
      kEpsilonStart, kEpsilonEnd, 0,
      kEpsilonEndTransition - kWarmTransitions);
  const bool schedule = std::abs(initial_epsilon - kEpsilonStart) < 1.0e-7f &&
                        kEpsilonEnd == 0.02f;
  const PublicState fixture = preflight.curriculum.states.front();
  const std::uint32_t stream = syntheticRestartStream(17);
  State state = base::materialize(fixture);
  const int action = base::greedyAction(preflight.warm, fixture);
  MoveResult move;
  const bool restart = isLegal(state.board, action) &&
                       playHeadlessMove(state, stream, action, move) &&
                       state.next_disc == headlessDisc(stream, 1);
  const bool guards = allowedTrainingSeed(kTrainingSeedStart) &&
                      allowedTrainingSeed(kTrainingSeedEndExclusive - 1) &&
                      !allowedTrainingSeed(0x4d00'0000u) &&
                      !allowedTrainingSeed(0x7d00'0000u) &&
                      !allowedTrainingSeed(0xd700'0000u);
  base::NstepAccumulator nstep;
  std::vector<base::Transition> produced;
  for (int step = 0; step < base::kNstep; ++step) {
    const auto batch = nstep.push({fixture, fixture, 1.0f, 3, false});
    produced.insert(produced.end(), batch.begin(), batch.end());
  }
  double expected_reward = 0.0, discount = 1.0;
  for (int step = 0; step < base::kNstep; ++step) {
    expected_reward += discount;
    discount *= base::kGamma;
  }
  const bool mechanics = produced.size() == 1 &&
      std::abs(produced[0].reward - expected_reward) < 1.0e-5 &&
      std::abs(produced[0].discount - discount) < 1.0e-5;
  const bool passed = mechanics && schedule && restart && guards &&
                      preflight.checkpoint_steps == kWarmTransitions &&
                      preflight.curriculum.states.size() == kCurriculumStates;
  output << "FLOW_CURRICULUM_RAINBOW_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"frozenRainbowMechanics\":"
         << (mechanics ? "true" : "false")
         << ",\"checksumPreflight\":true,\"schedule\":"
         << (schedule ? "true" : "false")
         << ",\"restartBoundary\":" << (restart ? "true" : "false")
         << ",\"seedGuards\":" << (guards ? "true" : "false")
         << ",\"gameplaySeedsOpened\":0,\"peakRssBytes\":"
         << fair::peakRssBytes() << "}\n";
  return passed;
}

int preflight(const Options& options, std::ostream& output) {
  const Deadline deadline;
  const Preflight checked = productionPreflight(options, deadline);
  output << "FLOW_CURRICULUM_RAINBOW_PREFLIGHT {\"passed\":true,"
            "\"gameplaySeedsOpened\":0,\"warmCheckpointSha256\":\""
         << kWarmCheckpointSha256 << "\",\"warmTransitions\":"
         << checked.checkpoint_steps << ",\"curriculumSha256\":\""
         << kCurriculumSha256 << "\",\"curriculumStates\":"
         << checked.curriculum.states.size()
         << ",\"reflection\":true,\"metadataBlind\":true,"
            "\"partialReplayTree\":true,\"seedGuards\":true,"
            "\"peakRssBytes\":" << fair::peakRssBytes() << "}\n";
  return 0;
}

int run(const Options& options, std::ostream& output) {
  const Deadline deadline;
  Preflight checked = productionPreflight(options, deadline);
  output << "FLOW_CURRICULUM_RAINBOW_SAFE_BOUNDARY {\"stage\":\"preflight\","
            "\"passed\":true,\"gameplaySeedsOpened\":0,"
            "\"warmTransitions\":" << checked.checkpoint_steps
         << ",\"curriculumStates\":" << checked.curriculum.states.size()
         << "}\n";
  TrainingRun training(std::move(checked.warm), checked.curriculum, deadline);
  training.train(output);
  base::writeCheckpoint(options.checkpoint, training.learner().model(),
                        training.learner().environmentSteps());
  auto frozen = base::readCheckpoint(options.checkpoint);
  if (frozen.second != kTargetTransitions ||
      base::modelFingerprint(frozen.first) !=
          base::modelFingerprint(training.learner().model()) ||
      base::fileBytes(options.checkpoint) > kCheckpointLimitBytes) {
    throw std::runtime_error("16m checkpoint freeze verification failed");
  }
  output << "FLOW_CURRICULUM_RAINBOW_SAFE_BOUNDARY {\"stage\":\"16m-freeze\","
            "\"transitions\":" << training.learner().environmentSteps()
         << ",\"checkpointSha256\":\"" << fileSha256(options.checkpoint)
         << "\",\"peakRssBytes\":" << fair::peakRssBytes() << "}\n";
  const Clock::time_point candidate_started = Clock::now();
  std::vector<Outcome> candidate;
  candidate.reserve(kStageAGames);
  for (int game = 0; game < kStageAGames; ++game) {
    candidate.push_back(evaluateLearned(kStageASeedStart + game,
                                        training.learner().model(), deadline));
  }
  const double candidate_seconds =
      std::chrono::duration<double>(Clock::now() - candidate_started).count();
  const Summary candidate_summary = summarize(candidate);
  const AbsoluteGate absolute = absoluteGate(candidate_summary);
  output << std::setprecision(12)
         << "FLOW_CURRICULUM_RAINBOW_STAGE_A {\"meanScore\":"
         << candidate_summary.mean_score << ",\"meanMoves\":"
         << candidate_summary.mean_moves << ",\"bottomQuartileMeanMoves\":"
         << candidate_summary.bottom_quartile_mean_moves
         << ",\"clearsPerMove\":" << candidate_summary.clears_per_move
         << ",\"revealsPerMove\":" << candidate_summary.reveals_per_move
         << ",\"absolutePassed\":" << (absolute.passed ? "true" : "false")
         << ",\"exactD4Opened\":false}\n";
  std::optional<std::vector<Outcome>> d4;
  std::optional<Summary> d4_summary;
  std::optional<FinalGate> final;
  double d4_seconds = 0.0;
  if (absolute.passed) {
    const Clock::time_point d4_started = Clock::now();
    d4.emplace(kStageAGames);
    std::atomic<int> next{0};
    std::vector<std::future<void>> workers;
    for (int worker = 0; worker < options.threads; ++worker) {
      workers.push_back(std::async(std::launch::async, [&] {
        for (;;) {
          const int game = next.fetch_add(1);
          if (game >= kStageAGames) return;
          (*d4)[game] = evaluateD4(kStageASeedStart + game, deadline);
          std::cerr << "flow paired D4 " << game + 1 << '/' << kStageAGames << '\n';
        }
      }));
    }
    for (auto& worker : workers) worker.get();
    d4_seconds = std::chrono::duration<double>(Clock::now() - d4_started).count();
    d4_summary = summarize(*d4);
    final = finalGate(candidate, candidate_summary, *d4, *d4_summary, absolute);
  }
  writeArtifact(options, checked, training, candidate, candidate_summary,
                absolute, d4, d4_summary, final, candidate_seconds,
                d4_seconds, deadline.seconds());
  writeReadme(options, candidate_summary, absolute, d4_summary, final);
  deadline.check();
  output << "FLOW_CURRICULUM_RAINBOW_RESULT {\"absolutePassed\":"
         << (absolute.passed ? "true" : "false")
         << ",\"exactD4Opened\":" << (d4.has_value() ? "true" : "false")
         << ",\"finalPassed\":" << (final.has_value() && final->passed ? "true" : "false")
         << ",\"transitions\":" << training.learner().environmentSteps()
         << ",\"wallSeconds\":" << deadline.seconds()
         << ",\"peakRssBytes\":" << fair::peakRssBytes()
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return 0;
}

bool resumeSelfTest(const Options& options, std::ostream& output) {
  Sha256 chunked;
  chunked.update("a", 1);
  chunked.update("b", 1);
  chunked.update("c", 1);
  const bool sha_vectors =
      sha256("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" &&
      sha256("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" &&
      chunked.finish() ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  const Deadline deadline;
  FrozenResume frozen = frozenResumePreflight(options, deadline);
  const bool passed = sha_vectors &&
      frozen.checkpoint_steps == kTargetTransitions &&
      frozen.checkpoint_fingerprint == kFrozenFingerprint &&
      frozen.checkpoint_sha256 == kFrozenCheckpointSha256 &&
      frozen.curriculum.states.size() == kCurriculumStates && seedGuardsPass() &&
      fair::peakRssBytes() <= kRssLimitBytes;
  output << "FLOW_CURRICULUM_RAINBOW_RESUME_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"streamingShaVectors\":"
         << (sha_vectors ? "true" : "false")
         << ",\"exactHeader\":true,\"exactPayloadFingerprint\":true,"
            "\"checkpointReadOnly\":true,\"weightsModified\":false,"
            "\"gameplaySeedsOpened\":0,\"peakRssBytes\":"
         << fair::peakRssBytes() << "}\n";
  return passed;
}

int resumePreflight(const Options& options, std::ostream& output) {
  const Deadline deadline;
  FrozenResume frozen = frozenResumePreflight(options, deadline);
  output << "FLOW_CURRICULUM_RAINBOW_RESUME_SAFE_BOUNDARY {"
            "\"stage\":\"frozen-16m-preflight\",\"passed\":true,"
            "\"gameplaySeedsOpened\":0,\"trainingSeedsReopened\":0,"
            "\"weightsModified\":false,\"checkpointReadOnly\":true,"
            "\"streamingSha256\":true,\"sourceSha256\":\""
         << options.source_sha256 << "\",\"checkpointSha256\":\""
         << frozen.checkpoint_sha256 << "\",\"transitions\":"
         << frozen.checkpoint_steps << ",\"fingerprint\":\"0x" << std::hex
         << frozen.checkpoint_fingerprint << std::dec
         << "\",\"curriculumStates\":" << frozen.curriculum.states.size()
         << ",\"peakRssBytes\":" << fair::peakRssBytes() << "}\n";
  deadline.check();
  return 0;
}

int resumeRun(const Options& options, std::ostream& output) {
  const Deadline deadline;
  FrozenResume frozen = frozenResumePreflight(options, deadline);
  output << "FLOW_CURRICULUM_RAINBOW_RESUME_SAFE_BOUNDARY {"
            "\"stage\":\"frozen-16m-preflight\",\"passed\":true,"
            "\"gameplaySeedsOpened\":0,\"trainingSeedsReopened\":0,"
            "\"weightsModified\":false,\"checkpointReadOnly\":true,"
            "\"sourceSha256\":\"" << options.source_sha256
         << "\",\"checkpointSha256\":\"" << frozen.checkpoint_sha256
         << "\",\"transitions\":" << frozen.checkpoint_steps
         << ",\"fingerprint\":\"0x" << std::hex
         << frozen.checkpoint_fingerprint << std::dec
         << "\",\"peakRssBytes\":" << fair::peakRssBytes() << "}\n";
  const Clock::time_point candidate_started = Clock::now();
  std::vector<Outcome> candidate;
  candidate.reserve(kStageAGames);
  for (int game = 0; game < kStageAGames; ++game) {
    candidate.push_back(evaluateLearned(kStageASeedStart + game,
                                        frozen.model, deadline));
  }
  const double candidate_seconds =
      std::chrono::duration<double>(Clock::now() - candidate_started).count();
  const Summary candidate_summary = summarize(candidate);
  const AbsoluteGate absolute = absoluteGate(candidate_summary);
  output << std::setprecision(12)
         << "FLOW_CURRICULUM_RAINBOW_STAGE_A {\"meanScore\":"
         << candidate_summary.mean_score << ",\"meanMoves\":"
         << candidate_summary.mean_moves << ",\"bottomQuartileMeanMoves\":"
         << candidate_summary.bottom_quartile_mean_moves
         << ",\"clearsPerMove\":" << candidate_summary.clears_per_move
         << ",\"revealsPerMove\":" << candidate_summary.reveals_per_move
         << ",\"absolutePassed\":" << (absolute.passed ? "true" : "false")
         << ",\"exactD4Opened\":false}\n";
  std::optional<std::vector<Outcome>> d4;
  std::optional<Summary> d4_summary;
  std::optional<FinalGate> final;
  double d4_seconds = 0.0;
  if (absolute.passed) {
    const Clock::time_point d4_started = Clock::now();
    d4.emplace(kStageAGames);
    std::atomic<int> next{0};
    std::vector<std::future<void>> workers;
    for (int worker = 0; worker < options.threads; ++worker) {
      workers.push_back(std::async(std::launch::async, [&] {
        for (;;) {
          const int game = next.fetch_add(1);
          if (game >= kStageAGames) return;
          (*d4)[game] = evaluateD4(kStageASeedStart + game, deadline);
          std::cerr << "flow paired D4 " << game + 1 << '/' << kStageAGames << '\n';
        }
      }));
    }
    for (auto& worker : workers) worker.get();
    d4_seconds = std::chrono::duration<double>(Clock::now() - d4_started).count();
    d4_summary = summarize(*d4);
    final = finalGate(candidate, candidate_summary, *d4, *d4_summary, absolute);
  }
  writeResumeArtifact(options, frozen, candidate, candidate_summary, absolute,
                      d4, d4_summary, final, candidate_seconds, d4_seconds,
                      deadline.seconds());
  writeReadme(options, candidate_summary, absolute, d4_summary, final);
  deadline.check();
  output << "FLOW_CURRICULUM_RAINBOW_RESUME_RESULT {\"absolutePassed\":"
         << (absolute.passed ? "true" : "false")
         << ",\"exactD4Opened\":" << (d4.has_value() ? "true" : "false")
         << ",\"finalPassed\":"
         << (final.has_value() && final->passed ? "true" : "false")
         << ",\"transitions\":" << frozen.checkpoint_steps
         << ",\"weightsModified\":false,\"wallSeconds\":"
         << deadline.seconds() << ",\"peakRssBytes\":"
         << fair::peakRssBytes() << ",\"artifact\":\"" << options.output
         << "\"}\n";
  return 0;
}

}  // namespace drop7::flow_curriculum_rainbow

int main(int argc, char** argv) {
  try {
    std::cout.setf(std::ios::unitbuf);
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      const auto options = drop7::flow_curriculum_rainbow::parseOptions(
          argc, argv, 2, false);
      return drop7::flow_curriculum_rainbow::selfTest(options, std::cout)
                 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--preflight") {
      const auto options = drop7::flow_curriculum_rainbow::parseOptions(
          argc, argv, 2, false);
      return drop7::flow_curriculum_rainbow::preflight(options, std::cout);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--resume-self-test") {
      const auto options = drop7::flow_curriculum_rainbow::parseOptions(
          argc, argv, 2, false);
      return drop7::flow_curriculum_rainbow::resumeSelfTest(options, std::cout)
                 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--resume-preflight") {
      const auto options = drop7::flow_curriculum_rainbow::parseOptions(
          argc, argv, 2, true);
      return drop7::flow_curriculum_rainbow::resumePreflight(options, std::cout);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--resume-frozen") {
      const auto options = drop7::flow_curriculum_rainbow::parseOptions(
          argc, argv, 2, true);
      return drop7::flow_curriculum_rainbow::resumeRun(options, std::cout);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      throw std::runtime_error(
          "the training seed stream was consumed; use --resume-preflight or "
          "--resume-frozen with the immutable 16m checkpoint");
    }
    std::cerr << "usage: drop7_flow_curriculum_rainbow "
                 "--self-test | --preflight | --resume-self-test | "
                 "--resume-preflight --source-sha256 HEX | "
                 "--resume-frozen --source-sha256 HEX "
                 "[--warm-checkpoint PATH] [--curriculum PATH] "
                 "[--checkpoint PATH] [--output PATH] [--readme PATH] "
                 "[--threads 1..4]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_flow_curriculum_rainbow: " << error.what() << '\n';
    return 1;
  }
}
