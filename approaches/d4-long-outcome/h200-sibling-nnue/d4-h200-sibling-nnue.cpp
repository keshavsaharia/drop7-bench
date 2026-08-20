#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#include "../../fair-expectimax/reference/fair-only-depth4.cpp"
#undef DROP7_FAIR_ONLY_DEPTH4_LIBRARY

#include <bit>
#include <filesystem>
#include <map>
#include <set>
#include <type_traits>

// Compares sibling-ranking architectures offline on the checksum-locked
// 477-record h200 development-panel corpus.  It has no gameplay lane: it
// never creates an initial state, advances an origin game, reads a new seed,
// generates a panel, or runs a policy screen.  Provenance identifies the one
// whole origin game held out in each fold and is never presented to the model.
namespace drop7::d4_h200_sibling_nnue {

namespace detail = drop7::cfpi::detail;
using Clock = std::chrono::steady_clock;

// The locked parser and exact-search adapter are included locally, with their
// dependency hash recorded below.  This avoids importing another executable's
// entry point or diagnostic model.
namespace frozen_audit {

namespace d1 = drop7::fair_only_horizon;
namespace d4 = drop7::fair_only_depth4;

constexpr int kExpectedRecords = 477;
constexpr int kExpectedGames = 8;
constexpr std::uint32_t kExpectedGameStart = 0x3d6d'0010u;
constexpr std::string_view kExpectedInputSha256 =
    "bfda8ae32fa0be3577c6b27f6413aba28f2854930e2f91a0dcf7674808f04196";

struct Options {
  std::string input;
  std::string input_sha256;
};

std::uint64_t peakRssBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ull;
#endif
}

void enforceRss() {
  if (peakRssBytes() > 256ull * 1024ull * 1024ull) {
    throw std::runtime_error("frozen exact audit exceeded 256 MiB RSS");
  }
}

struct Deadline {
  Clock::time_point started = Clock::now();
  void check() const {
    if (std::chrono::duration<double>(Clock::now() - started).count() >
        30.0 * 60.0) {
      throw std::runtime_error("frozen exact audit exceeded 30 minute wall");
    }
  }
};

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

std::uint64_t mix64(std::uint64_t value) {
  value ^= value >> 30u;
  value *= 0xbf58'476d'1ce4'e5b9ull;
  value ^= value >> 27u;
  value *= 0x94d0'49bb'1331'11ebull;
  return value ^ (value >> 31u);
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
  hash *= 0x0000'0100'0000'01b3ull;
  hash ^= static_cast<std::uint64_t>(state.terminal);
  return mix64(hash);
}

std::string hex64(std::uint64_t value) {
  std::ostringstream output;
  output << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
  return output.str();
}

std::string jsonEscape(std::string_view value) {
  std::string result;
  for (char token : value) {
    if (token == '"' || token == '\\') result.push_back('\\');
    if (token == '\n') result += "\\n";
    else result.push_back(token);
  }
  return result;
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
  const long long value = std::strtoll(first, &end, 10);
  if (end == first) throw std::runtime_error("invalid JSON integer");
  return value;
}

double numberAfter(std::string_view text, std::string_view marker,
                   std::size_t begin = 0) {
  const std::size_t cursor = afterMarker(text, marker, begin);
  const std::string owned(text);
  char* end = nullptr;
  const char* first = owned.c_str() + cursor;
  const double value = std::strtod(first, &end);
  if (end == first || !std::isfinite(value)) {
    throw std::runtime_error("invalid JSON number");
  }
  return value;
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
  bool quoted = false, escaped = false;
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
          text[cursor] == ',')) ++cursor;
}

struct PanelAction {
  bool legal = false;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double mean_clears = 0.0;
  double mean_reveals = 0.0;
  int surviving_cutoffs = 0;
  double score_lcb99 = 0.0;
  double move_lcb99 = 0.0;
  int material_downsides = 0;
  double material_downside_upper99 = 0.0;
};

struct PanelRecord {
  std::uint32_t origin_game = 0;
  int move_index = -1;
  std::uint64_t stored_public_hash = 0;
  PublicState state{};
  int stored_d1_action = -1;
  int deployment_action = -1;
  bool deployment_switched = false;
  std::array<PanelAction, kBoardSize> actions{};
};

std::uint64_t parseHex64(std::string_view value) {
  const std::string owned(value);
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(owned.c_str(), &end, 0);
  if (end == owned.c_str() || *end != '\0') {
    throw std::runtime_error("invalid stored public hash");
  }
  return static_cast<std::uint64_t>(parsed);
}

PanelAction parsePanelAction(std::string_view object, int expected_action) {
  PanelAction result;
  result.legal = true;
  if (integerAfter(object, "\"action\":") != expected_action) {
    throw std::runtime_error("panel action index mismatch");
  }
  result.mean_score = numberAfter(object, "\"meanScoreReturn\":");
  result.mean_moves = numberAfter(object, "\"meanSurvivedMoves\":");
  result.mean_clears = numberAfter(object, "\"meanNumberedClears\":");
  result.mean_reveals = numberAfter(object, "\"meanCoversRevealed\":");
  result.surviving_cutoffs = static_cast<int>(
      integerAfter(object, "\"survivingCutoffs\":"));
  const std::size_t paired = afterMarker(object, "\"pairedVsFairD1\":");
  const std::size_t score = afterMarker(object, "\"score\":", paired);
  const std::size_t score_end = matchingDelimiter(object, score, '{', '}');
  result.score_lcb99 = numberAfter(
      object.substr(score, score_end - score + 1), "\"lowerOneSided99\":");
  const std::size_t moves = afterMarker(object, "\"moves\":", score_end + 1);
  const std::size_t moves_end = matchingDelimiter(object, moves, '{', '}');
  result.move_lcb99 = numberAfter(
      object.substr(moves, moves_end - moves + 1), "\"lowerOneSided99\":");
  result.material_downsides = static_cast<int>(
      integerAfter(object, "\"materialDownsides\":", moves_end));
  result.material_downside_upper99 =
      numberAfter(object, "\"materialDownsideUpper99\":", moves_end);
  return result;
}

PanelRecord parsePanel(std::string_view line) {
  if (line.empty() || line.front() != '{' || line.back() != '}' ||
      line.find('\0') != std::string_view::npos ||
      line.find("\"recordType\":\"deployment-panel-export-replay\"") ==
          std::string_view::npos ||
      line.find("\"gate\":\"ultra\"") == std::string_view::npos ||
      line.find("\"excludedFromModelInput\"") == std::string_view::npos) {
    throw std::runtime_error("unexpected panel record metadata");
  }
  PanelRecord result;
  const long long game = integerAfter(line, "\"screenSeed\":");
  if (game < 0 || game > std::numeric_limits<std::uint32_t>::max()) {
    throw std::runtime_error("invalid panel origin game");
  }
  result.origin_game = static_cast<std::uint32_t>(game);
  result.move_index = static_cast<int>(integerAfter(line, "\"moveIndex\":"));
  result.stored_public_hash = parseHex64(
      stringAfter(line, "\"canonicalPublicHash\":\""));
  const std::string board = stringAfter(line, "\"board\":\"");
  if (board.size() != kCellCount) throw std::runtime_error("invalid board size");
  for (int cell = 0; cell < kCellCount; ++cell) {
    if (board[cell] < '0' || board[cell] > '9') {
      throw std::runtime_error("invalid board token");
    }
    result.state.board[cell] = static_cast<std::uint8_t>(board[cell] - '0');
  }
  result.state.next_disc =
      static_cast<std::uint8_t>(integerAfter(line, "\"nextDisc\":"));
  result.state.moves_remaining = static_cast<std::uint8_t>(
      integerAfter(line, "\"movesRemaining\":"));
  result.state.terminal = booleanAfter(line, "\"terminal\":");
  result.stored_d1_action =
      static_cast<int>(integerAfter(line, "\"fairD1Action\":"));
  result.deployment_action =
      static_cast<int>(integerAfter(line, "\"selectedAction\":"));
  result.deployment_switched = booleanAfter(line, "\"switched\":");
  if (result.state.next_disc < 1 || result.state.next_disc > kBoardSize ||
      result.state.moves_remaining < 1 ||
      result.state.moves_remaining > kMovesPerLevel || result.state.terminal ||
      result.move_index < 0) {
    throw std::runtime_error("invalid public-state metadata");
  }
  std::size_t cursor = afterMarker(line, "\"actions\":[");
  for (int action = 0; action < kBoardSize; ++action) {
    skipSeparators(line, cursor);
    if (line.substr(cursor, 4) == "null") { cursor += 4; continue; }
    if (cursor >= line.size() || line[cursor] != '{') {
      throw std::runtime_error("invalid panel action array");
    }
    const std::size_t end = matchingDelimiter(line, cursor, '{', '}');
    result.actions[action] =
        parsePanelAction(line.substr(cursor, end - cursor + 1), action);
    cursor = end + 1;
  }
  skipSeparators(line, cursor);
  if (cursor >= line.size() || line[cursor] != ']') {
    throw std::runtime_error("unterminated panel action array");
  }
  for (int action = 0; action < kBoardSize; ++action) {
    if (result.actions[action].legal != isLegal(result.state.board, action)) {
      throw std::runtime_error("panel legal mask mismatch");
    }
  }
  if (result.stored_d1_action < 0 || result.stored_d1_action >= kBoardSize ||
      result.deployment_action < 0 || result.deployment_action >= kBoardSize ||
      !result.actions[result.stored_d1_action].legal ||
      !result.actions[result.deployment_action].legal ||
      publicHash(result.state) != result.stored_public_hash) {
    throw std::runtime_error("panel public-state invariant failed");
  }
  return result;
}

std::vector<PanelRecord> loadPanels(const Options& options) {
  if (options.input_sha256 != kExpectedInputSha256) {
    throw std::runtime_error("frozen panel hash declaration changed");
  }
  std::ifstream input(options.input);
  if (!input) throw std::runtime_error("could not open frozen panel corpus");
  std::vector<PanelRecord> result;
  std::string line;
  while (std::getline(input, line)) if (!line.empty()) result.push_back(parsePanel(line));
  if (result.size() != kExpectedRecords) {
    throw std::runtime_error("frozen panel record count mismatch");
  }
  std::map<std::uint32_t, std::set<int>> moves;
  for (const PanelRecord& panel : result) moves[panel.origin_game].insert(panel.move_index);
  if (moves.size() != kExpectedGames) {
    throw std::runtime_error("frozen panel game count mismatch");
  }
  for (int game = 0; game < kExpectedGames; ++game) {
    const auto found = moves.find(kExpectedGameStart + game);
    if (found == moves.end() || found->second.empty() ||
        *found->second.begin() != 0 ||
        *found->second.rbegin() + 1 != static_cast<int>(found->second.size())) {
      throw std::runtime_error("panel origin-game boundary mismatch");
    }
  }
  return result;
}

struct ExactSearch {
  int d1_action = -1;
  int d4_action = -1;
  std::array<double, kBoardSize> d1_q{};
  std::array<double, kBoardSize> d4_q{};
  std::array<double, kBoardSize> d4_immediate_score{};
  std::uint64_t d1_work = 0;
  std::uint64_t d4_work = 0;
  std::uint64_t d4_nodes = 0;
  std::uint64_t d4_cache_hits = 0;
  std::size_t d4_cache_entries = 0;
};

ExactSearch exactSearch(const PublicState& source) {
  ExactSearch result;
  result.d1_q.fill(-std::numeric_limits<double>::infinity());
  bool mirrored = false;
  const State canonical = detail::canonicalState(materialize(source), mirrored);
  d1::SearchContext d1_context;
  const d1::RootEvaluation d1_root = d1::rootDecision(canonical, 1, d1_context);
  if (d1_root.action < 0 || !d1_context.cache.empty()) {
    throw std::runtime_error("exact fair-D1 root did not complete");
  }
  result.d1_action = mirrored ? kBoardSize - 1 - d1_root.action : d1_root.action;
  for (int canonical_action = 0; canonical_action < kBoardSize; ++canonical_action) {
    const int source_action = mirrored ? kBoardSize - 1 - canonical_action
                                       : canonical_action;
    result.d1_q[source_action] = d1_root.values[canonical_action];
  }
  result.d1_work = d1_context.work;
  const d4::SearchDecision decision = d4::chooseDepth4Action(materialize(source));
  if (!decision.complete || decision.completed_depth != d4::kCandidateDepth ||
      decision.action < 0) {
    throw std::runtime_error("exact fair-D4 root did not complete");
  }
  result.d4_action = decision.action;
  result.d4_q = decision.root_values;
  result.d4_immediate_score = decision.root_expected_scores;
  result.d4_work = decision.work;
  result.d4_nodes = decision.nodes;
  result.d4_cache_hits = decision.cache_hits;
  result.d4_cache_entries = decision.cache_entries;
  return result;
}

struct AuditRoot {
  PanelRecord panel{};
  ExactSearch search{};
};

std::vector<AuditRoot> evaluateAll(const std::vector<PanelRecord>& panels,
                                   int threads, const Deadline& deadline) {
  std::vector<AuditRoot> result(panels.size());
  std::atomic<std::size_t> next{0}, completed{0};
  std::mutex output_mutex;
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < threads; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const std::size_t index = next.fetch_add(1);
        if (index >= panels.size()) return;
        deadline.check();
        AuditRoot root;
        root.panel = panels[index];
        root.search = exactSearch(root.panel.state);
        if (root.search.d1_action != root.panel.stored_d1_action) {
          throw std::runtime_error("stored and exact fair-D1 actions differ");
        }
        result[index] = std::move(root);
        enforceRss();
        const std::size_t done = completed.fetch_add(1) + 1;
        if (done % 20 == 0 || done == panels.size()) {
          const std::lock_guard<std::mutex> lock(output_mutex);
          std::cerr << "D4 h200 exact audit " << done << '/' << panels.size() << '\n';
        }
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

}  // namespace frozen_audit

namespace audit = frozen_audit;
using PublicState = audit::PublicState;

constexpr int kRecords = 477;
constexpr int kGames = 8;
constexpr std::uint32_t kGameStart = 0x3d6d'0010u;
constexpr std::array<int, kGames> kExpectedGameRecords{{77, 50, 55, 35,
                                                        35, 65, 55, 105}};
constexpr std::string_view kCorpusSha256 =
    "bfda8ae32fa0be3577c6b27f6413aba28f2854930e2f91a0dcf7674808f04196";
constexpr std::string_view kAuditSourceSha256 =
    "b04e75123b7c29a350868d7ef8781256b51bc9f16878787f6d0611dc42371308";
constexpr std::string_view kD4SourceSha256 =
    "1cb42629db07b17850045bf3e5678c1fed5b58c73ab38bcfb699c94ee34fe6aa";
constexpr std::string_view kD1SourceSha256 =
    "f9d4ea210e282ce5cc22894c17b5be92efb12029242aa5c3c6dc6412b383f42b";
constexpr std::string_view kBehaviorSha256 =
    "e5e81fa103589a9a911b6019a15aa48339c78ae2460ea0b0df4b0f66d59f27df";
constexpr std::string_view kEngineSha256 =
    "b6dcde5f40dc39c6931b9a88e42bb351acd6fadaddd1e07691c41a82e44f3090";

constexpr int kPhaseFeatures = 24;
constexpr int kFeatureCount = 96;
constexpr int kHeads = 5;
constexpr int kHidden = 64;
constexpr int kBoardTokens = 10;
constexpr int kBoardCategories = kCellCount * kBoardTokens;
constexpr int kNextCategories = kBoardSize;
constexpr int kRiseCategories = kMovesPerLevel;
constexpr int kActionCategories = kBoardSize;
constexpr int kCategoryCount = kBoardCategories + kNextCategories +
                               kRiseCategories + kActionCategories;
constexpr int kActiveCategories = kCellCount + 3;
constexpr int kSuccessorSamples = kBoardSize;
constexpr std::uint32_t kSuccessorDomain = 0x4434'4853u;

// One architecture and training schedule fixed before evaluation.  There is no
// validation-driven epoch, seed, feature, width, or loss selection.
constexpr int kEpochs = 48;
constexpr int kBatchRoots = 32;
constexpr float kLearningRate = 0.0012f;
constexpr float kWeightDecay = 1.0e-5f;
constexpr float kGradientNorm = 3.0f;
constexpr float kPairWeight = 1.0f;
constexpr float kListWeight = 0.75f;
constexpr float kPointWeight = 0.25f;
constexpr float kAuxiliaryWeight = 0.12f;
constexpr float kResidualWeight = 0.015f;
constexpr float kListTemperature = 0.35f;
constexpr std::uint32_t kNetworkSeed = 0x4434'4e4eu;
constexpr std::uint32_t kShuffleDomain = 0x4434'5348u;

constexpr double kNearTieFraction = 0.10;
constexpr double kDecisiveFraction = 0.50;
constexpr int kCalibrationBins = 10;
constexpr double kTop1Gain = 0.03;
constexpr double kPairwiseGain = 0.015;
constexpr double kRegretRatio = 0.90;
constexpr int kRequiredImprovedFolds = 6;
constexpr double kTieTolerance = 1.0e-9;

constexpr double kWallLimitSeconds = 30.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr std::uint64_t kCheckpointLimitBytes = 512ull * 1024ull;
constexpr std::uint64_t kCheckpointMagic = 0x4437'4832'4e4e'3031ull;
constexpr std::uint32_t kCheckpointVersion = 1;
constexpr std::uint64_t kAuditCacheMagic = 0x4437'4434'5143'3031ull;
constexpr std::uint32_t kAuditCacheVersion = 1;

static_assert(kFeatureCount == 3 * kPhaseFeatures + 24);
static_assert(kCategoryCount == 509);
static_assert(kHidden == 64 && kHeads == 5);
static_assert(audit::kExpectedRecords == kRecords);
static_assert(audit::kExpectedGames == kGames);
static_assert(audit::kExpectedInputSha256 == kCorpusSha256);
static_assert(kLevelBonus == 17'000);

struct RunOptions {
  std::string input = "/tmp/drop7-terminal-policy-deployment-panels.jsonl";
  std::string output = "/tmp/drop7-d4-h200-sibling-nnue.json";
  std::string checkpoint = "/tmp/drop7-d4-h200-sibling-nnue.bin";
  std::string golden = "/tmp/drop7-d4-h200-sibling-nnue-golden.json";
  std::string readme = "/tmp/drop7-d4-h200-sibling-nnue-README.md";
  std::string audit_cache = "/tmp/drop7-d4-h200-d4-audit.bin";
  std::string source_sha256;
  int threads = 4;
};

bool lowercaseSha256(std::string_view value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](char token) {
           return (token >= '0' && token <= '9') ||
                  (token >= 'a' && token <= 'f');
         });
}

RunOptions parseOptions(int argc, char** argv, int begin) {
  RunOptions result;
  for (int index = begin; index < argc; ++index) {
    const std::string_view flag(argv[index]);
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string value(argv[++index]);
    if (flag == "--input") result.input = value;
    else if (flag == "--output") result.output = value;
    else if (flag == "--checkpoint") result.checkpoint = value;
    else if (flag == "--golden") result.golden = value;
    else if (flag == "--readme") result.readme = value;
    else if (flag == "--audit-cache") result.audit_cache = value;
    else if (flag == "--source-sha256") result.source_sha256 = value;
    else if (flag == "--threads") result.threads = std::stoi(value);
    else throw std::invalid_argument("unknown option " + std::string(flag));
  }
  if (!lowercaseSha256(result.source_sha256)) {
    throw std::invalid_argument("--source-sha256 must be 64 lowercase hex");
  }
  if (result.threads < 1 || result.threads > 4 || result.input.empty() ||
      result.output.empty() || result.checkpoint.empty() ||
      result.golden.empty() || result.readme.empty() ||
      result.audit_cache.empty()) {
    throw std::invalid_argument("invalid offline residual options");
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
      throw std::runtime_error("D4 h200 residual exceeded 30 minute wall");
    }
    if (audit::peakRssBytes() > kRssLimitBytes) {
      throw std::runtime_error("D4 h200 residual exceeded 256 MiB RSS");
    }
  }
};

std::string readWholeFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not read " + path);
  std::ostringstream output;
  output << input.rdbuf();
  if (!input.good() && !input.eof()) {
    throw std::runtime_error("failed reading " + path);
  }
  return output.str();
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
      const std::uint32_t s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^
                               std::rotr(e, 25);
      const std::uint32_t choose = (e & f) ^ (~e & g);
      const std::uint32_t first =
          h + s1 + choose + kSha256Constants[round] + words[round];
      const std::uint32_t s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^
                               std::rotr(a, 22);
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

std::string fileSha256(const std::string& path) {
  return sha256(readWholeFile(path));
}

std::vector<audit::PanelRecord> loadLockedPanels(const RunOptions& options) {
  const std::string contents = readWholeFile(options.input);
  if (sha256(contents) != kCorpusSha256) {
    throw std::runtime_error("frozen h200 panel checksum mismatch");
  }
  audit::Options inherited;
  inherited.input = options.input;
  inherited.input_sha256 = std::string(kCorpusSha256);
  const std::vector<audit::PanelRecord> panels = audit::loadPanels(inherited);
  std::array<int, kGames> counts{};
  for (const audit::PanelRecord& panel : panels) {
    const int game = static_cast<int>(panel.origin_game - kGameStart);
    if (game < 0 || game >= kGames) {
      throw std::runtime_error("out-of-domain origin in locked panel corpus");
    }
    ++counts[game];
  }
  if (counts != kExpectedGameRecords) {
    throw std::runtime_error("locked panel per-game counts changed");
  }
  return panels;
}

template <typename Value>
void writePod(std::ostream& output, const Value& value) {
  static_assert(std::is_trivially_copyable_v<Value>);
  output.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

template <typename Value>
void readPod(std::istream& input, Value& value) {
  static_assert(std::is_trivially_copyable_v<Value>);
  input.read(reinterpret_cast<char*>(&value), sizeof(value));
}

struct ExactCorpus {
  std::vector<audit::ExactSearch> searches;
  bool cache_hit = false;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::size_t maximum_cache_entries = 0;
  double seconds = 0.0;
};

bool loadAuditCache(const std::string& path,
                    const std::vector<audit::PanelRecord>& panels,
                    ExactCorpus& result) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return false;
  std::uint64_t magic = 0;
  std::uint32_t version = 0, count = 0;
  std::array<char, 64> corpus{};
  std::array<char, 64> d4_source{};
  readPod(input, magic); readPod(input, version); readPod(input, count);
  readPod(input, corpus); readPod(input, d4_source);
  if (!input || magic != kAuditCacheMagic || version != kAuditCacheVersion ||
      count != panels.size() || std::string_view(corpus.data(), 64) != kCorpusSha256 ||
      std::string_view(d4_source.data(), 64) != kD4SourceSha256) {
    return false;
  }
  std::vector<audit::ExactSearch> searches(count);
  for (std::size_t index = 0; index < searches.size(); ++index) {
    std::uint64_t hash = 0;
    readPod(input, hash);
    audit::ExactSearch& value = searches[index];
    readPod(input, value.d1_action); readPod(input, value.d4_action);
    readPod(input, value.d1_q); readPod(input, value.d4_q);
    readPod(input, value.d4_immediate_score);
    readPod(input, value.d1_work); readPod(input, value.d4_work);
    readPod(input, value.d4_nodes); readPod(input, value.d4_cache_hits);
    readPod(input, value.d4_cache_entries);
    if (!input || hash != audit::publicHash(panels[index].state) ||
        value.d1_action != panels[index].stored_d1_action) return false;
    for (int action = 0; action < kBoardSize; ++action) {
      const bool legal = panels[index].actions[action].legal;
      if (legal != std::isfinite(value.d1_q[action]) ||
          legal != std::isfinite(value.d4_q[action])) return false;
    }
  }
  char trailing = 0;
  if (input.read(&trailing, 1)) return false;
  result.searches = std::move(searches);
  result.cache_hit = true;
  return true;
}

void saveAuditCache(const std::string& path,
                    const std::vector<audit::PanelRecord>& panels,
                    const std::vector<audit::ExactSearch>& searches) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("could not write exact-D4 audit cache");
  const std::uint32_t count = static_cast<std::uint32_t>(searches.size());
  std::array<char, 64> corpus{}, d4_source{};
  std::copy(kCorpusSha256.begin(), kCorpusSha256.end(), corpus.begin());
  std::copy(kD4SourceSha256.begin(), kD4SourceSha256.end(), d4_source.begin());
  writePod(output, kAuditCacheMagic); writePod(output, kAuditCacheVersion);
  writePod(output, count); writePod(output, corpus); writePod(output, d4_source);
  for (std::size_t index = 0; index < searches.size(); ++index) {
    const std::uint64_t hash = audit::publicHash(panels[index].state);
    const audit::ExactSearch& value = searches[index];
    writePod(output, hash);
    writePod(output, value.d1_action); writePod(output, value.d4_action);
    writePod(output, value.d1_q); writePod(output, value.d4_q);
    writePod(output, value.d4_immediate_score);
    writePod(output, value.d1_work); writePod(output, value.d4_work);
    writePod(output, value.d4_nodes); writePod(output, value.d4_cache_hits);
    writePod(output, value.d4_cache_entries);
  }
  if (!output) throw std::runtime_error("exact-D4 audit cache write failed");
}

void summarizeExact(ExactCorpus& result) {
  for (const audit::ExactSearch& search : result.searches) {
    result.work += search.d4_work;
    result.nodes += search.d4_nodes;
    result.cache_hits += search.d4_cache_hits;
    result.maximum_cache_entries =
        std::max(result.maximum_cache_entries, search.d4_cache_entries);
  }
}

ExactCorpus exactCorpus(const RunOptions& options,
                        const std::vector<audit::PanelRecord>& panels,
                        const Deadline& deadline) {
  const Clock::time_point started = Clock::now();
  ExactCorpus result;
  if (!loadAuditCache(options.audit_cache, panels, result)) {
    audit::Deadline inherited_deadline;
    const std::vector<audit::AuditRoot> roots =
        audit::evaluateAll(panels, options.threads, inherited_deadline);
    result.searches.reserve(roots.size());
    for (const audit::AuditRoot& root : roots) {
      result.searches.push_back(root.search);
    }
    saveAuditCache(options.audit_cache, panels, result.searches);
  }
  result.seconds = std::chrono::duration<double>(Clock::now() - started).count();
  summarizeExact(result);
  deadline.check();
  return result;
}

std::array<double, kPhaseFeatures> phaseVector(const State& state) {
  const detail::PhaseFeatures f = detail::extractPhaseFeatures(state);
  return {{
      static_cast<double>(f.open_columns), f.height_load,
      static_cast<double>(f.solid_cells), static_cast<double>(f.cracked_cells),
      static_cast<double>(f.numbered_cells),
      static_cast<double>(f.high_low_numbers), f.direct_potential,
      f.latent_chain_potential, f.cracked_exposure, f.solid_exposure,
      f.adjacent_ones, f.triple_twos, f.dead_low_numbers,
      f.projected_occupancy_debt, f.residual_cover_debt,
      f.cover_altitude_debt, f.imminent_cover_altitude_debt,
      f.peak_height_risk, f.low_cap_load, f.adjacent_low_cap_load,
      f.quiet_build_options, f.quiet_direct_gain, f.trigger_readiness,
      f.rise_trigger_readiness,
  }};
}

struct RawAction {
  int action = -1;
  std::array<float, kFeatureCount> features{};
  audit::PanelAction label{};
  float anchor = 0.0f;
};

struct RawPanel {
  std::uint32_t origin_game = 0;
  int move_index = -1;
  PublicState state{};
  std::vector<RawAction> actions;
};

std::pair<double, double> meanAndRange(
    const std::array<double, kBoardSize>& values,
    const std::array<audit::PanelAction, kBoardSize>& actions) {
  double mean = 0.0;
  double minimum = std::numeric_limits<double>::infinity();
  double maximum = -std::numeric_limits<double>::infinity();
  int count = 0;
  for (int action = 0; action < kBoardSize; ++action) {
    if (!actions[action].legal) continue;
    mean += values[action];
    minimum = std::min(minimum, values[action]);
    maximum = std::max(maximum, values[action]);
    ++count;
  }
  if (count < 1) throw std::runtime_error("panel has no legal actions");
  return {mean / count, std::max(1.0, maximum - minimum)};
}

RawPanel prepareRawPanel(const audit::PanelRecord& panel,
                         const audit::ExactSearch& search) {
  RawPanel result;
  result.origin_game = panel.origin_game;
  result.move_index = panel.move_index;
  result.state = panel.state;
  const State root = audit::materialize(panel.state);
  const auto root_phase = phaseVector(root);
  const auto heights = detail::columnHeights(root.board);
  const double height_mean =
      std::accumulate(heights.begin(), heights.end(), 0.0) / kBoardSize;
  double height_variance = 0.0, roughness = 0.0;
  int height_minimum = kBoardSize, height_maximum = 0, occupancy = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    height_variance += (heights[column] - height_mean) *
                       (heights[column] - height_mean) / kBoardSize;
    height_minimum = std::min(height_minimum, heights[column]);
    height_maximum = std::max(height_maximum, heights[column]);
    occupancy += heights[column];
    if (column) roughness += std::abs(heights[column] - heights[column - 1]);
  }
  int legal_count = 0;
  legalColumns(root.board, legal_count);
  const auto [d4_mean, d4_range] = meanAndRange(search.d4_q, panel.actions);
  const auto [d1_mean, d1_range] = meanAndRange(search.d1_q, panel.actions);
  double best_d1 = -std::numeric_limits<double>::infinity();
  for (int action = 0; action < kBoardSize; ++action) {
    if (panel.actions[action].legal) best_d1 = std::max(best_d1, search.d1_q[action]);
  }
  const std::uint32_t seed = detail::scenarioSeedForState(
      root, kSuccessorDomain, 0);
  for (int action = 0; action < kBoardSize; ++action) {
    if (!panel.actions[action].legal) continue;
    RawAction raw;
    raw.action = action;
    raw.label = panel.actions[action];
    raw.anchor = static_cast<float>((search.d4_q[action] - d4_mean) / d4_range);
    std::array<double, kPhaseFeatures> sum{}, squares{};
    double immediate_sum = 0.0, immediate_squares = 0.0;
    double immediate_minimum = std::numeric_limits<double>::infinity();
    double immediate_maximum = -std::numeric_limits<double>::infinity();
    double wave_sum = 0.0, wave_squares = 0.0;
    double clear_sum = 0.0, clear_squares = 0.0;
    double reveal_sum = 0.0, reveal_squares = 0.0;
    double occupancy_delta_sum = 0.0;
    int terminal = 0, cleared_board = 0, level_advanced = 0;
    for (int sample = 0; sample < kSuccessorSamples; ++sample) {
      detail::StratifiedRandom random{seed, sample, kSuccessorSamples, 0};
      MoveResult move;
      if (!detail::playMoveSampled(root, action, random, move)) {
        throw std::runtime_error("common immediate successor failed");
      }
      const double immediate = static_cast<double>(move.score_delta);
      immediate_sum += immediate;
      immediate_squares += immediate * immediate;
      immediate_minimum = std::min(immediate_minimum, immediate);
      immediate_maximum = std::max(immediate_maximum, immediate);
      terminal += move.state.game_over;
      cleared_board += move.cleared_board;
      level_advanced += move.level_advanced;
      double clears = 0.0, reveals = 0.0;
      for (const Wave& wave : move.waves) {
        clears += wave.cleared;
        reveals += wave.revealed;
      }
      wave_sum += move.waves.size();
      wave_squares += move.waves.size() * move.waves.size();
      clear_sum += clears; clear_squares += clears * clears;
      reveal_sum += reveals; reveal_squares += reveals * reveals;
      State successor = move.state;
      successor.score = 0;
      successor.level = 1;
      successor.moves_played = 0;
      if (!successor.game_over) {
        successor.next_disc =
            detail::sampledNextDisc(seed, sample, kSuccessorSamples);
      }
      const auto phase = phaseVector(successor);
      for (int feature = 0; feature < kPhaseFeatures; ++feature) {
        sum[feature] += phase[feature];
        squares[feature] += phase[feature] * phase[feature];
      }
      int successor_occupancy = 0;
      for (std::uint8_t cell : successor.board) successor_occupancy += cell != kEmpty;
      occupancy_delta_sum += successor_occupancy - occupancy;
    }
    int feature = 0;
    for (double value : root_phase) raw.features[feature++] = value;
    for (int index = 0; index < kPhaseFeatures; ++index) {
      raw.features[feature++] = static_cast<float>(sum[index] / kSuccessorSamples);
    }
    for (int index = 0; index < kPhaseFeatures; ++index) {
      const double mean = sum[index] / kSuccessorSamples;
      raw.features[feature++] = static_cast<float>(std::sqrt(std::max(
          0.0, squares[index] / kSuccessorSamples - mean * mean)));
    }
    const auto dispersion = [](double sum, double squares) {
      const double mean = sum / kSuccessorSamples;
      return std::sqrt(std::max(0.0, squares / kSuccessorSamples - mean * mean));
    };
    const double immediate_mean = immediate_sum / kSuccessorSamples;
    raw.features[feature++] = raw.anchor;
    raw.features[feature++] = static_cast<float>((search.d1_q[action] - d1_mean) / d1_range);
    raw.features[feature++] = static_cast<float>((best_d1 - search.d1_q[action]) / d1_range);
    raw.features[feature++] = static_cast<float>(search.d4_immediate_score[action] / 17'000.0);
    raw.features[feature++] = static_cast<float>(immediate_mean / 17'000.0);
    raw.features[feature++] = static_cast<float>(dispersion(immediate_sum, immediate_squares) / 17'000.0);
    raw.features[feature++] = static_cast<float>(immediate_minimum / 17'000.0);
    raw.features[feature++] = static_cast<float>(immediate_maximum / 17'000.0);
    raw.features[feature++] = static_cast<float>(terminal) / kSuccessorSamples;
    raw.features[feature++] = static_cast<float>(wave_sum / kSuccessorSamples);
    raw.features[feature++] = static_cast<float>(dispersion(wave_sum, wave_squares));
    raw.features[feature++] = static_cast<float>(clear_sum / kSuccessorSamples);
    raw.features[feature++] = static_cast<float>(dispersion(clear_sum, clear_squares));
    raw.features[feature++] = static_cast<float>(reveal_sum / kSuccessorSamples);
    raw.features[feature++] = static_cast<float>(dispersion(reveal_sum, reveal_squares));
    raw.features[feature++] = static_cast<float>(cleared_board) / kSuccessorSamples;
    raw.features[feature++] = static_cast<float>(level_advanced) / kSuccessorSamples;
    raw.features[feature++] = static_cast<float>(heights[action]) / kBoardSize;
    raw.features[feature++] = static_cast<float>(std::abs(action - 3)) / 3.0f;
    raw.features[feature++] = static_cast<float>(height_mean / kBoardSize);
    raw.features[feature++] = static_cast<float>(std::sqrt(height_variance) / kBoardSize);
    raw.features[feature++] = static_cast<float>(height_maximum - height_minimum) / kBoardSize;
    raw.features[feature++] = static_cast<float>(roughness / 42.0);
    raw.features[feature++] = static_cast<float>(occupancy_delta_sum /
                                                 (kSuccessorSamples * kCellCount));
    if (feature != kFeatureCount) {
      throw std::runtime_error("D4 h200 feature-count mismatch");
    }
    // A few aggregate PhaseFeatures traverse mirrored cells in the opposite
    // addition order.  Quantizing well below the normalizer's useful
    // resolution removes last-bit accumulation noise, making the complete
    // public feature map (not just the categorical tower) reflection exact.
    for (float& value : raw.features) {
      value = std::nearbyint(value * 100'000.0f) / 100'000.0f;
    }
    result.actions.push_back(raw);
  }
  return result;
}

std::vector<RawPanel> prepareRawPanels(
    const std::vector<audit::PanelRecord>& panels,
    const std::vector<audit::ExactSearch>& searches,
    const Deadline& deadline) {
  if (panels.size() != searches.size()) {
    throw std::invalid_argument("panel/search count mismatch");
  }
  std::vector<RawPanel> result;
  result.reserve(panels.size());
  for (std::size_t index = 0; index < panels.size(); ++index) {
    if ((index & 63u) == 0u) deadline.check();
    result.push_back(prepareRawPanel(panels[index], searches[index]));
  }
  return result;
}

struct Normalizer {
  std::array<float, kFeatureCount> mean{};
  std::array<float, kFeatureCount> scale{};
  std::array<float, 4> auxiliary_mean{};
  std::array<float, 4> auxiliary_scale{};

  std::array<float, kFeatureCount> features(
      const std::array<float, kFeatureCount>& raw) const {
    std::array<float, kFeatureCount> result{};
    for (int index = 0; index < kFeatureCount; ++index) {
      result[index] = std::clamp(
          (raw[index] - mean[index]) * scale[index], -6.0f, 6.0f);
    }
    return result;
  }
};

std::array<double, 4> auxiliaryTargets(const audit::PanelAction& action) {
  return {{action.mean_moves, action.mean_clears, action.mean_reveals,
           action.material_downside_upper99}};
}

Normalizer fitNormalizer(const std::vector<RawPanel>& panels,
                         const std::vector<std::size_t>& indices) {
  if (indices.empty()) throw std::invalid_argument("empty normalization fold");
  std::array<double, kFeatureCount> sum{}, squares{};
  std::array<double, 4> aux_sum{}, aux_squares{};
  std::uint64_t count = 0;
  for (std::size_t panel_index : indices) {
    for (const RawAction& action : panels.at(panel_index).actions) {
      for (int feature = 0; feature < kFeatureCount; ++feature) {
        sum[feature] += action.features[feature];
        squares[feature] += static_cast<double>(action.features[feature]) *
                            action.features[feature];
      }
      const auto targets = auxiliaryTargets(action.label);
      for (int target = 0; target < 4; ++target) {
        aux_sum[target] += targets[target];
        aux_squares[target] += targets[target] * targets[target];
      }
      ++count;
    }
  }
  Normalizer result;
  const auto fit = [count](double sum, double squares, float& mean,
                           float& scale) {
    const double center = sum / count;
    const double variance = std::max(1.0e-6, squares / count - center * center);
    mean = static_cast<float>(center);
    scale = static_cast<float>(1.0 / std::sqrt(variance));
  };
  for (int feature = 0; feature < kFeatureCount; ++feature) {
    fit(sum[feature], squares[feature], result.mean[feature],
        result.scale[feature]);
  }
  for (int target = 0; target < 4; ++target) {
    fit(aux_sum[target], aux_squares[target], result.auxiliary_mean[target],
        result.auxiliary_scale[target]);
  }
  return result;
}

struct PreparedAction {
  int action = -1;
  std::array<float, kFeatureCount> features{};
  std::array<float, kHeads> targets{};
  double score = 0.0;
  float anchor = 0.0f;
};

struct PreparedPanel {
  PublicState state{};
  std::vector<PreparedAction> actions;
};

std::vector<PreparedPanel> preparePanels(
    const std::vector<RawPanel>& source,
    const std::vector<std::size_t>& indices,
    const Normalizer& normalizer) {
  std::vector<PreparedPanel> result;
  result.reserve(indices.size());
  for (std::size_t index : indices) {
    const RawPanel& raw = source.at(index);
    PreparedPanel panel;
    panel.state = raw.state;
    double mean = 0.0;
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (const RawAction& action : raw.actions) {
      mean += action.label.mean_score / raw.actions.size();
      minimum = std::min(minimum, action.label.mean_score);
      maximum = std::max(maximum, action.label.mean_score);
    }
    const double range = std::max(1.0, maximum - minimum);
    for (const RawAction& raw_action : raw.actions) {
      PreparedAction action;
      action.action = raw_action.action;
      action.features = normalizer.features(raw_action.features);
      const float score_target = static_cast<float>(
          (raw_action.label.mean_score - mean) / range);
      action.targets[0] = score_target - raw_action.anchor;
      const auto auxiliary = auxiliaryTargets(raw_action.label);
      for (int target = 0; target < 4; ++target) {
        action.targets[target + 1] = static_cast<float>(
            (auxiliary[target] - normalizer.auxiliary_mean[target]) *
            normalizer.auxiliary_scale[target]);
      }
      action.score = raw_action.label.mean_score;
      action.anchor = raw_action.anchor;
      panel.actions.push_back(action);
    }
    result.push_back(std::move(panel));
  }
  return result;
}

struct Layout {
  int embedding = 0;
  int numeric = kCategoryCount * kHidden;
  int bias = numeric + kFeatureCount * kHidden;
  int output = bias + kHidden;
  int output_bias = output + kHeads * kHidden;
  int count = output_bias + kHeads;
};

struct OrientationCache {
  std::array<int, kActiveCategories> categories{};
  std::array<float, kHidden> pre{};
  std::array<float, kHidden> hidden{};
  std::array<float, kHeads> output{};
};

struct ForwardCache {
  OrientationCache direct{};
  OrientationCache reflected{};
  std::array<float, kHeads> output{};
};

class Network {
 public:
  explicit Network(std::uint32_t seed)
      : parameters_(layout_.count), first_(layout_.count), second_(layout_.count) {
    Mulberry32 random(seed);
    for (int index = layout_.embedding; index < layout_.numeric; ++index) {
      parameters_[index] = static_cast<float>((2.0 * random.nextUnit() - 1.0) * 0.035);
    }
    const float radius = std::sqrt(6.0f / (kFeatureCount + kHidden));
    for (int index = layout_.numeric; index < layout_.bias; ++index) {
      parameters_[index] = static_cast<float>((2.0 * random.nextUnit() - 1.0) * radius);
    }
    // The primary head starts at an exact zero residual, so the untrained
    // model is exactly always-D4.  Auxiliary heads retain small Xavier output
    // weights and cannot affect the primary value directly.
    const float output_radius = std::sqrt(6.0f / (kHidden + kHeads));
    for (int head = 1; head < kHeads; ++head) {
      for (int hidden = 0; hidden < kHidden; ++hidden) {
        parameters_[layout_.output + head * kHidden + hidden] =
            static_cast<float>((2.0 * random.nextUnit() - 1.0) * output_radius);
      }
    }
  }

  OrientationCache forwardOrientation(
      const PublicState& state, int action,
      const std::array<float, kFeatureCount>& features) const {
    if (state.terminal || !isLegal(state.board, action)) {
      throw std::invalid_argument("invalid public action for NNUE");
    }
    OrientationCache cache;
    int active = 0;
    for (int cell = 0; cell < kCellCount; ++cell) {
      cache.categories[active++] = cell * kBoardTokens + state.board[cell];
    }
    cache.categories[active++] = kBoardCategories + state.next_disc - 1;
    cache.categories[active++] =
        kBoardCategories + kNextCategories + state.moves_remaining - 1;
    cache.categories[active++] =
        kBoardCategories + kNextCategories + kRiseCategories + action;
    if (active != kActiveCategories) throw std::runtime_error("category mismatch");
    const float category_scale = 1.0f / std::sqrt(kActiveCategories);
    const float numeric_scale = 1.0f / std::sqrt(kFeatureCount);
    for (int hidden = 0; hidden < kHidden; ++hidden) {
      float value = parameters_[layout_.bias + hidden];
      for (int category : cache.categories) {
        value += category_scale *
            parameters_[layout_.embedding + category * kHidden + hidden];
      }
      for (int feature = 0; feature < kFeatureCount; ++feature) {
        value += numeric_scale * features[feature] *
            parameters_[layout_.numeric + feature * kHidden + hidden];
      }
      cache.pre[hidden] = value;
      cache.hidden[hidden] = std::clamp(value, 0.0f, 1.0f);
    }
    for (int head = 0; head < kHeads; ++head) {
      float value = parameters_[layout_.output_bias + head];
      for (int hidden = 0; hidden < kHidden; ++hidden) {
        value += parameters_[layout_.output + head * kHidden + hidden] *
                 cache.hidden[hidden];
      }
      cache.output[head] = value;
    }
    return cache;
  }

  ForwardCache forward(const PublicState& state, int action,
                       const std::array<float, kFeatureCount>& features) const {
    ForwardCache result;
    result.direct = forwardOrientation(state, action, features);
    result.reflected = forwardOrientation(
        audit::mirror(state), kBoardSize - 1 - action, features);
    for (int head = 0; head < kHeads; ++head) {
      result.output[head] =
          0.5f * (result.direct.output[head] + result.reflected.output[head]);
    }
    return result;
  }

  std::array<float, kHeads> predict(
      const PublicState& state, int action,
      const std::array<float, kFeatureCount>& features) const {
    return forward(state, action, features).output;
  }

  void accumulateOrientation(
      const std::array<float, kFeatureCount>& features,
      const OrientationCache& cache,
      const std::array<float, kHeads>& derivative,
      std::vector<float>& gradient) const {
    std::array<float, kHidden> hidden_derivative{};
    for (int head = 0; head < kHeads; ++head) {
      gradient[layout_.output_bias + head] += derivative[head];
      for (int hidden = 0; hidden < kHidden; ++hidden) {
        const int index = layout_.output + head * kHidden + hidden;
        gradient[index] += derivative[head] * cache.hidden[hidden];
        hidden_derivative[hidden] += derivative[head] * parameters_[index];
      }
    }
    const float category_scale = 1.0f / std::sqrt(kActiveCategories);
    const float numeric_scale = 1.0f / std::sqrt(kFeatureCount);
    for (int hidden = 0; hidden < kHidden; ++hidden) {
      const float value = cache.pre[hidden] > 0.0f && cache.pre[hidden] < 1.0f
                              ? hidden_derivative[hidden] : 0.0f;
      gradient[layout_.bias + hidden] += value;
      for (int category : cache.categories) {
        gradient[layout_.embedding + category * kHidden + hidden] +=
            category_scale * value;
      }
      for (int feature = 0; feature < kFeatureCount; ++feature) {
        gradient[layout_.numeric + feature * kHidden + hidden] +=
            numeric_scale * features[feature] * value;
      }
    }
  }

  void backpropagate(const PreparedAction& action,
                     const ForwardCache& cache,
                     const std::array<float, kHeads>& derivative,
                     std::vector<float>& gradient) const {
    std::array<float, kHeads> half{};
    for (int head = 0; head < kHeads; ++head) half[head] = 0.5f * derivative[head];
    accumulateOrientation(action.features, cache.direct, half, gradient);
    accumulateOrientation(action.features, cache.reflected, half, gradient);
  }

  std::vector<float> gradient() const {
    return std::vector<float>(parameters_.size(), 0.0f);
  }

  void apply(std::vector<float>& gradient) {
    double squared_norm = 0.0;
    for (int index = 0; index < layout_.count; ++index) {
      const bool decay = index < layout_.bias ||
                         (index >= layout_.output && index < layout_.output_bias);
      if (decay) gradient[index] += kWeightDecay * parameters_[index];
      squared_norm += static_cast<double>(gradient[index]) * gradient[index];
    }
    const double norm = std::sqrt(squared_norm);
    const float clipping = norm > kGradientNorm
                               ? static_cast<float>(kGradientNorm / norm) : 1.0f;
    ++step_;
    constexpr float beta1 = 0.9f, beta2 = 0.999f, epsilon = 1.0e-8f;
    const float correction1 = 1.0f - std::pow(beta1, static_cast<float>(step_));
    const float correction2 = 1.0f - std::pow(beta2, static_cast<float>(step_));
    for (int index = 0; index < layout_.count; ++index) {
      const float value = clipping * gradient[index];
      first_[index] = beta1 * first_[index] + (1.0f - beta1) * value;
      second_[index] = beta2 * second_[index] + (1.0f - beta2) * value * value;
      parameters_[index] -= kLearningRate * (first_[index] / correction1) /
          (std::sqrt(second_[index] / correction2) + epsilon);
      if (!std::isfinite(parameters_[index])) {
        throw std::runtime_error("non-finite NNUE parameter");
      }
    }
  }

  const std::vector<float>& parameters() const { return parameters_; }
  void setParameters(const std::vector<float>& values) {
    if (values.size() != parameters_.size()) {
      throw std::invalid_argument("NNUE parameter count mismatch");
    }
    parameters_ = values;
    std::fill(first_.begin(), first_.end(), 0.0f);
    std::fill(second_.begin(), second_.end(), 0.0f);
    step_ = 0;
  }
  int parameterCount() const { return layout_.count; }

 private:
  Layout layout_{};
  std::vector<float> parameters_, first_, second_;
  std::uint64_t step_ = 0;
};

using PublicEvaluator = std::array<float, kHeads> (Network::*)(
    const PublicState&, int,
    const std::array<float, kFeatureCount>&) const;
static_assert(std::is_same_v<decltype(&Network::predict), PublicEvaluator>);
static_assert(!std::is_invocable_v<PublicEvaluator, const Network&, const State&,
                                   int, const std::array<float, kFeatureCount>&>);

double sigmoid(double value) {
  if (value >= 0.0) return 1.0 / (1.0 + std::exp(-value));
  const double e = std::exp(value);
  return e / (1.0 + e);
}

double softplus(double value) {
  if (value > 30.0) return value;
  if (value < -30.0) return std::exp(value);
  return std::log1p(std::exp(value));
}

void accumulatePanel(const Network& network, const PreparedPanel& panel,
                     float inverse_batch, std::vector<float>& gradient,
                     double& loss) {
  const std::size_t count = panel.actions.size();
  std::vector<ForwardCache> caches;
  caches.reserve(count);
  std::vector<std::array<float, kHeads>> derivatives(count);
  std::vector<double> prediction(count), target(count);
  double minimum = std::numeric_limits<double>::infinity();
  double maximum = -std::numeric_limits<double>::infinity();
  int pairs = 0;
  for (std::size_t index = 0; index < count; ++index) {
    const PreparedAction& action = panel.actions[index];
    caches.push_back(network.forward(panel.state, action.action, action.features));
    prediction[index] = action.anchor + caches.back().output[0];
    target[index] = action.anchor + action.targets[0];
    minimum = std::min(minimum, action.score);
    maximum = std::max(maximum, action.score);
  }
  for (std::size_t first = 0; first < count; ++first) {
    for (std::size_t second = first + 1; second < count; ++second) {
      pairs += std::abs(panel.actions[first].score - panel.actions[second].score) >
               kTieTolerance;
    }
  }
  const double score_range = std::max(1.0, maximum - minimum);
  if (pairs) {
    for (std::size_t first = 0; first < count; ++first) {
      for (std::size_t second = first + 1; second < count; ++second) {
        const double difference =
            panel.actions[first].score - panel.actions[second].score;
        if (std::abs(difference) <= kTieTolerance) continue;
        const double sign = difference > 0.0 ? 1.0 : -1.0;
        const double margin = sign * (prediction[first] - prediction[second]);
        const double importance = 0.25 + 0.75 * std::abs(difference) / score_range;
        const double weight = kPairWeight * importance / pairs;
        loss += weight * softplus(-margin);
        const float derivative = static_cast<float>(-weight * sigmoid(-margin));
        derivatives[first][0] += static_cast<float>(sign) * derivative;
        derivatives[second][0] -= static_cast<float>(sign) * derivative;
      }
    }
  }
  double predicted_max = -std::numeric_limits<double>::infinity();
  double target_max = -std::numeric_limits<double>::infinity();
  for (std::size_t index = 0; index < count; ++index) {
    prediction[index] /= kListTemperature;
    target[index] /= kListTemperature;
    predicted_max = std::max(predicted_max, prediction[index]);
    target_max = std::max(target_max, target[index]);
  }
  double predicted_sum = 0.0, target_sum = 0.0;
  for (std::size_t index = 0; index < count; ++index) {
    prediction[index] = std::exp(prediction[index] - predicted_max);
    target[index] = std::exp(target[index] - target_max);
    predicted_sum += prediction[index]; target_sum += target[index];
  }
  for (std::size_t index = 0; index < count; ++index) {
    const double probability = prediction[index] / predicted_sum;
    const double truth = target[index] / target_sum;
    loss -= kListWeight * truth * std::log(std::max(1.0e-12, probability));
    derivatives[index][0] += static_cast<float>(
        kListWeight * (probability - truth) / kListTemperature);
  }
  for (std::size_t index = 0; index < count; ++index) {
    const PreparedAction& action = panel.actions[index];
    const float residual = caches[index].output[0];
    const float error = residual - action.targets[0];
    loss += 0.5 * kPointWeight * error * error / count;
    derivatives[index][0] += kPointWeight * error / count;
    loss += 0.5 * kResidualWeight * residual * residual / count;
    derivatives[index][0] += kResidualWeight * residual / count;
    for (int head = 1; head < kHeads; ++head) {
      const float auxiliary_error = caches[index].output[head] - action.targets[head];
      loss += 0.5 * kAuxiliaryWeight * auxiliary_error * auxiliary_error / count;
      derivatives[index][head] += kAuxiliaryWeight * auxiliary_error / count;
    }
  }
  for (std::size_t index = 0; index < count; ++index) {
    for (float& value : derivatives[index]) value *= inverse_batch;
    network.backpropagate(panel.actions[index], caches[index],
                          derivatives[index], gradient);
  }
}

struct TrainingResult {
  Network network;
  double first_loss = 0.0;
  double final_loss = 0.0;
  explicit TrainingResult(std::uint32_t seed) : network(seed) {}
};

TrainingResult train(const std::vector<PreparedPanel>& panels,
                     std::uint32_t seed, int epochs,
                     const Deadline& deadline, bool progress) {
  TrainingResult result(seed);
  std::vector<std::size_t> order(panels.size());
  std::iota(order.begin(), order.end(), 0u);
  for (int epoch = 0; epoch < epochs; ++epoch) {
    Mulberry32 random(mix32(kShuffleDomain ^ seed ^
                           static_cast<std::uint32_t>(epoch + 1)));
    for (std::size_t cursor = order.size(); cursor > 1; --cursor) {
      const std::size_t selected = static_cast<std::size_t>(
          (static_cast<std::uint64_t>(random.nextBits()) * cursor) >> 32u);
      std::swap(order[cursor - 1], order[selected]);
    }
    double loss = 0.0;
    for (std::size_t begin = 0; begin < order.size(); begin += kBatchRoots) {
      if ((begin & 255u) == 0u) deadline.check();
      const std::size_t end = std::min(order.size(), begin + kBatchRoots);
      std::vector<float> gradient = result.network.gradient();
      const float inverse = 1.0f / static_cast<float>(end - begin);
      for (std::size_t at = begin; at < end; ++at) {
        accumulatePanel(result.network, panels[order[at]], inverse, gradient, loss);
      }
      result.network.apply(gradient);
    }
    loss /= panels.size();
    if (epoch == 0) result.first_loss = loss;
    result.final_loss = loss;
    if (progress && ((epoch + 1) % 12 == 0 || epoch == 0)) {
      std::cerr << "D4_H200_NNUE_TRAIN {\"epoch\":" << epoch + 1
                << ",\"loss\":" << loss << ",\"rss\":"
                << audit::peakRssBytes() << "}\n";
    }
  }
  return result;
}

struct RankingAccumulator {
  int roots = 0;
  int pairs = 0;
  double top1 = 0.0;
  double top2 = 0.0;
  double pairwise = 0.0;
  double regret = 0.0;
  void merge(const RankingAccumulator& other) {
    roots += other.roots; pairs += other.pairs; top1 += other.top1;
    top2 += other.top2; pairwise += other.pairwise; regret += other.regret;
  }
};

struct RankingMetrics {
  int roots = 0;
  int pairs = 0;
  double top1 = 0.0;
  double top2 = 0.0;
  double pairwise = 0.0;
  double normalized_regret = 0.0;
};

void observeRanking(RankingAccumulator& result,
                    const std::vector<double>& prediction,
                    const std::vector<double>& target) {
  std::vector<int> order(prediction.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
    return prediction[left] > prediction[right];
  });
  const auto [minimum, maximum] = std::minmax_element(target.begin(), target.end());
  const auto best = [&](int index) {
    return target[index] + kTieTolerance >= *maximum;
  };
  result.top1 += best(order[0]);
  result.top2 += best(order[0]) || (order.size() > 1 && best(order[1]));
  const double range = *maximum - *minimum;
  if (range > kTieTolerance) {
    result.regret += (*maximum - target[order[0]]) / range;
  }
  for (std::size_t first = 0; first < target.size(); ++first) {
    for (std::size_t second = first + 1; second < target.size(); ++second) {
      const double truth = target[first] - target[second];
      const double guessed = prediction[first] - prediction[second];
      result.pairwise +=
          std::abs(truth) <= kTieTolerance || std::abs(guessed) <= 1.0e-12
              ? 0.5 : ((truth > 0.0) == (guessed > 0.0));
      ++result.pairs;
    }
  }
  ++result.roots;
}

RankingMetrics finish(const RankingAccumulator& source) {
  return {source.roots, source.pairs,
          source.top1 / std::max(1, source.roots),
          source.top2 / std::max(1, source.roots),
          source.pairwise / std::max(1, source.pairs),
          source.regret / std::max(1, source.roots)};
}

struct CalibrationAccumulator {
  int pairs = 0;
  double correct = 0.0;
  double baseline_correct = 0.0;
  double brier = 0.0;
  double confidence = 0.0;
  std::array<int, kCalibrationBins> counts{};
  std::array<double, kCalibrationBins> correct_by_bin{};
  std::array<double, kCalibrationBins> confidence_by_bin{};
  void merge(const CalibrationAccumulator& other) {
    pairs += other.pairs; correct += other.correct;
    baseline_correct += other.baseline_correct; brier += other.brier;
    confidence += other.confidence;
    for (int bin = 0; bin < kCalibrationBins; ++bin) {
      counts[bin] += other.counts[bin];
      correct_by_bin[bin] += other.correct_by_bin[bin];
      confidence_by_bin[bin] += other.confidence_by_bin[bin];
    }
  }
};

struct CalibrationMetrics {
  int pairs = 0;
  double accuracy = 0.0;
  double baseline_accuracy = 0.0;
  double brier = 0.0;
  double mean_confidence = 0.0;
  double ece = 0.0;
};

void observeCalibration(CalibrationAccumulator& result,
                        double first, double second,
                        double baseline_first, double baseline_second,
                        double truth_first, double truth_second) {
  const double truth_difference = truth_first - truth_second;
  if (std::abs(truth_difference) <= kTieTolerance) return;
  const double probability = sigmoid(first - second);
  const bool truth = truth_difference > 0.0;
  const bool choice = probability >= 0.5;
  const double correct = choice == truth;
  const double baseline_difference = baseline_first - baseline_second;
  const double baseline_correct = std::abs(baseline_difference) <= 1.0e-12
                                      ? 0.5
                                      : ((baseline_difference > 0.0) == truth);
  const double confidence = std::max(probability, 1.0 - probability);
  const int bin = std::clamp(static_cast<int>(
      (confidence - 0.5) * 2.0 * kCalibrationBins), 0, kCalibrationBins - 1);
  ++result.pairs; result.correct += correct;
  result.baseline_correct += baseline_correct;
  const double error = probability - static_cast<double>(truth);
  result.brier += error * error; result.confidence += confidence;
  ++result.counts[bin]; result.correct_by_bin[bin] += correct;
  result.confidence_by_bin[bin] += confidence;
}

CalibrationMetrics finish(const CalibrationAccumulator& source) {
  CalibrationMetrics result;
  result.pairs = source.pairs;
  if (!source.pairs) return result;
  result.accuracy = source.correct / source.pairs;
  result.baseline_accuracy = source.baseline_correct / source.pairs;
  result.brier = source.brier / source.pairs;
  result.mean_confidence = source.confidence / source.pairs;
  for (int bin = 0; bin < kCalibrationBins; ++bin) {
    if (!source.counts[bin]) continue;
    result.ece += static_cast<double>(source.counts[bin]) / source.pairs *
        std::abs(source.correct_by_bin[bin] / source.counts[bin] -
                 source.confidence_by_bin[bin] / source.counts[bin]);
  }
  return result;
}

struct EvaluationAccumulator {
  RankingAccumulator candidate;
  RankingAccumulator baseline;
  CalibrationAccumulator near_tie;
  CalibrationAccumulator decisive;
  void merge(const EvaluationAccumulator& other) {
    candidate.merge(other.candidate); baseline.merge(other.baseline);
    near_tie.merge(other.near_tie); decisive.merge(other.decisive);
  }
};

struct Evaluation {
  RankingMetrics candidate;
  RankingMetrics baseline;
  CalibrationMetrics near_tie;
  CalibrationMetrics decisive;
};

Evaluation finish(const EvaluationAccumulator& source) {
  return {finish(source.candidate), finish(source.baseline),
          finish(source.near_tie), finish(source.decisive)};
}

EvaluationAccumulator evaluate(const Network& network,
                               const Normalizer& normalizer,
                               const std::vector<RawPanel>& panels,
                               const std::vector<std::size_t>& indices,
                               const Deadline& deadline) {
  EvaluationAccumulator result;
  for (std::size_t offset = 0; offset < indices.size(); ++offset) {
    if ((offset & 63u) == 0u) deadline.check();
    const RawPanel& panel = panels.at(indices[offset]);
    std::vector<double> candidate, baseline, target;
    for (const RawAction& action : panel.actions) {
      const auto features = normalizer.features(action.features);
      baseline.push_back(action.anchor);
      candidate.push_back(action.anchor +
          network.predict(panel.state, action.action, features)[0]);
      target.push_back(action.label.mean_score);
    }
    observeRanking(result.candidate, candidate, target);
    observeRanking(result.baseline, baseline, target);
    const auto [minimum, maximum] = std::minmax_element(target.begin(), target.end());
    const double range = *maximum - *minimum;
    if (range <= kTieTolerance) continue;
    for (std::size_t first = 0; first < target.size(); ++first) {
      for (std::size_t second = first + 1; second < target.size(); ++second) {
        const double fraction = std::abs(target[first] - target[second]) / range;
        if (fraction <= kNearTieFraction) {
          observeCalibration(result.near_tie, candidate[first], candidate[second],
                             baseline[first], baseline[second], target[first], target[second]);
        }
        if (fraction >= kDecisiveFraction) {
          observeCalibration(result.decisive, candidate[first], candidate[second],
                             baseline[first], baseline[second], target[first], target[second]);
        }
      }
    }
  }
  return result;
}

std::vector<std::size_t> indicesFor(const std::vector<RawPanel>& panels,
                                    int heldout_game, bool training) {
  std::vector<std::size_t> result;
  for (std::size_t index = 0; index < panels.size(); ++index) {
    const int game = static_cast<int>(panels[index].origin_game - kGameStart);
    if ((game == heldout_game) != training) result.push_back(index);
  }
  return result;
}

std::vector<std::size_t> allIndices(std::size_t size) {
  std::vector<std::size_t> result(size);
  std::iota(result.begin(), result.end(), 0u);
  return result;
}

struct FoldResult {
  int heldout_game = 0;
  int training_roots = 0;
  int validation_roots = 0;
  double first_loss = 0.0;
  double final_loss = 0.0;
  Evaluation evaluation{};
  bool improves_pairwise_regret_without_top1_regression = false;
};

struct CrossValidation {
  std::array<FoldResult, kGames> folds{};
  Evaluation overall{};
  std::array<Evaluation, 2> halves{};
  int improved_folds = 0;
  bool passed = false;
};

bool pairwiseRegretImproves(const Evaluation& value) {
  return value.candidate.pairwise > value.baseline.pairwise + kTieTolerance &&
         value.candidate.normalized_regret + kTieTolerance <
             value.baseline.normalized_regret;
}

CrossValidation crossValidate(const std::vector<RawPanel>& panels,
                              const Deadline& deadline) {
  CrossValidation result;
  EvaluationAccumulator overall;
  std::array<EvaluationAccumulator, 2> halves{};
  for (int fold = 0; fold < kGames; ++fold) {
    const auto training_indices = indicesFor(panels, fold, true);
    const auto validation_indices = indicesFor(panels, fold, false);
    const Normalizer normalizer = fitNormalizer(panels, training_indices);
    const auto training_panels =
        preparePanels(panels, training_indices, normalizer);
    const TrainingResult trained = train(
        training_panels, mix32(kNetworkSeed ^ static_cast<std::uint32_t>(fold + 1)),
        kEpochs, deadline, false);
    const EvaluationAccumulator validation =
        evaluate(trained.network, normalizer, panels, validation_indices, deadline);
    overall.merge(validation);
    halves[fold / 4].merge(validation);
    FoldResult& record = result.folds[fold];
    record.heldout_game = fold;
    record.training_roots = training_indices.size();
    record.validation_roots = validation_indices.size();
    record.first_loss = trained.first_loss;
    record.final_loss = trained.final_loss;
    record.evaluation = finish(validation);
    record.improves_pairwise_regret_without_top1_regression =
        pairwiseRegretImproves(record.evaluation) &&
        record.evaluation.candidate.top1 + kTieTolerance >=
            record.evaluation.baseline.top1;
    result.improved_folds +=
        record.improves_pairwise_regret_without_top1_regression;
    std::cerr << std::setprecision(8)
              << "D4_H200_NNUE_FOLD {\"heldoutOrigin\":\""
              << audit::hex64(kGameStart + fold) << "\",\"candidateTop1\":"
              << record.evaluation.candidate.top1
              << ",\"baselineTop1\":" << record.evaluation.baseline.top1
              << ",\"candidatePairwise\":" << record.evaluation.candidate.pairwise
              << ",\"baselinePairwise\":" << record.evaluation.baseline.pairwise
              << ",\"candidateRegret\":"
              << record.evaluation.candidate.normalized_regret
              << ",\"baselineRegret\":"
              << record.evaluation.baseline.normalized_regret << "}\n";
  }
  result.overall = finish(overall);
  for (int half = 0; half < 2; ++half) result.halves[half] = finish(halves[half]);
  const bool overall_gate =
      result.overall.candidate.top1 >= result.overall.baseline.top1 + kTop1Gain &&
      result.overall.candidate.pairwise >=
          result.overall.baseline.pairwise + kPairwiseGain &&
      result.overall.candidate.normalized_regret <=
          kRegretRatio * result.overall.baseline.normalized_regret;
  result.passed = overall_gate && result.improved_folds >= kRequiredImprovedFolds &&
                  pairwiseRegretImproves(result.halves[0]) &&
                  pairwiseRegretImproves(result.halves[1]);
  return result;
}

void fingerprintFloat(std::uint64_t& hash, float value) {
  std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
  for (int byte = 0; byte < 4; ++byte) {
    hash ^= static_cast<std::uint8_t>(bits >> (byte * 8));
    hash *= 0x0000'0100'0000'01b3ull;
  }
}

std::uint64_t fingerprint(const Network& network, const Normalizer& normalizer) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (float value : network.parameters()) fingerprintFloat(hash, value);
  for (float value : normalizer.mean) fingerprintFloat(hash, value);
  for (float value : normalizer.scale) fingerprintFloat(hash, value);
  for (float value : normalizer.auxiliary_mean) fingerprintFloat(hash, value);
  for (float value : normalizer.auxiliary_scale) fingerprintFloat(hash, value);
  return hash;
}

std::uint64_t checkpointBytes(const Network& network) {
  return sizeof(kCheckpointMagic) + 5 * sizeof(std::uint32_t) + 64 +
         sizeof(std::uint64_t) + sizeof(Normalizer) +
         network.parameters().size() * sizeof(float);
}

void saveCheckpoint(const std::string& path, const Network& network,
                    const Normalizer& normalizer) {
  if (checkpointBytes(network) > kCheckpointLimitBytes) {
    throw std::runtime_error("NNUE checkpoint exceeds 512 KiB");
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("could not write NNUE checkpoint");
  const std::uint32_t feature_count = kFeatureCount, hidden = kHidden,
                      heads = kHeads, category_count = kCategoryCount,
                      parameter_count = network.parameterCount();
  std::array<char, 64> corpus{};
  std::copy(kCorpusSha256.begin(), kCorpusSha256.end(), corpus.begin());
  const std::uint64_t model_fingerprint = fingerprint(network, normalizer);
  writePod(output, kCheckpointMagic); writePod(output, kCheckpointVersion);
  writePod(output, feature_count); writePod(output, hidden); writePod(output, heads);
  writePod(output, category_count); writePod(output, parameter_count);
  writePod(output, corpus); writePod(output, model_fingerprint);
  writePod(output, normalizer);
  output.write(reinterpret_cast<const char*>(network.parameters().data()),
               network.parameters().size() * sizeof(float));
  if (!output) throw std::runtime_error("NNUE checkpoint write failed");
}

struct FrozenModel {
  Normalizer normalizer{};
  Network network{kNetworkSeed};
};

FrozenModel loadCheckpoint(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not read NNUE checkpoint");
  std::uint64_t magic = 0, expected_fingerprint = 0;
  std::uint32_t version = 0, feature_count = 0, hidden = 0, heads = 0,
                categories = 0, parameters = 0;
  std::array<char, 64> corpus{};
  readPod(input, magic); readPod(input, version); readPod(input, feature_count);
  readPod(input, hidden); readPod(input, heads); readPod(input, categories);
  readPod(input, parameters); readPod(input, corpus);
  readPod(input, expected_fingerprint);
  if (!input || magic != kCheckpointMagic || version != kCheckpointVersion ||
      feature_count != kFeatureCount || hidden != kHidden || heads != kHeads ||
      categories != kCategoryCount ||
      std::string_view(corpus.data(), 64) != kCorpusSha256) {
    throw std::runtime_error("invalid NNUE checkpoint metadata");
  }
  FrozenModel result;
  if (parameters != static_cast<std::uint32_t>(
                        result.network.parameterCount())) {
    throw std::runtime_error("invalid NNUE checkpoint parameter count");
  }
  readPod(input, result.normalizer);
  std::vector<float> values(parameters);
  input.read(reinterpret_cast<char*>(values.data()), values.size() * sizeof(float));
  char trailing = 0;
  if (!input || input.read(&trailing, 1)) {
    throw std::runtime_error("invalid NNUE checkpoint payload");
  }
  result.network.setParameters(values);
  if (fingerprint(result.network, result.normalizer) != expected_fingerprint ||
      std::filesystem::file_size(path) > kCheckpointLimitBytes) {
    throw std::runtime_error("NNUE checkpoint fingerprint/resource mismatch");
  }
  return result;
}

void writeRanking(std::ostream& output, const RankingMetrics& value) {
  output << "{\"roots\":" << value.roots << ",\"pairs\":" << value.pairs
         << ",\"top1\":" << value.top1 << ",\"top2\":" << value.top2
         << ",\"pairwise\":" << value.pairwise
         << ",\"normalizedRegret\":" << value.normalized_regret << '}';
}

void writeCalibration(std::ostream& output, const CalibrationMetrics& value) {
  output << "{\"pairs\":" << value.pairs << ",\"accuracy\":"
         << value.accuracy << ",\"alwaysD4Accuracy\":"
         << value.baseline_accuracy << ",\"brier\":" << value.brier
         << ",\"meanConfidence\":" << value.mean_confidence
         << ",\"ece\":" << value.ece << '}';
}

void writeEvaluation(std::ostream& output, const Evaluation& value) {
  output << "{\"candidate\":"; writeRanking(output, value.candidate);
  output << ",\"alwaysD4\":"; writeRanking(output, value.baseline);
  output << ",\"calibration\":{\"nearTie\":";
  writeCalibration(output, value.near_tie);
  output << ",\"decisive\":"; writeCalibration(output, value.decisive);
  output << "}}";
}

void writeGolden(const std::string& path, const FrozenModel& model,
                 const std::vector<RawPanel>& panels) {
  std::ofstream output(path, std::ios::trunc);
  if (!output) throw std::runtime_error("could not write NNUE golden file");
  output << std::setprecision(12)
         << "{\n  \"format\":\"drop7-d4-h200-sibling-nnue-golden-v1\","
         << "\n  \"modelFingerprint\":\"" << audit::hex64(
                fingerprint(model.network, model.normalizer))
         << "\",\n  \"cases\":[";
  for (int fixture = 0; fixture < 4; ++fixture) {
    const RawPanel& panel = panels[static_cast<std::size_t>(fixture) *
                                   panels.size() / 4];
    if (fixture) output << ',';
    output << "{\"publicHash\":\"" << audit::hex64(audit::publicHash(panel.state))
           << "\",\"actions\":[";
    for (std::size_t index = 0; index < panel.actions.size(); ++index) {
      if (index) output << ',';
      const RawAction& action = panel.actions[index];
      const auto features = model.normalizer.features(action.features);
      const double direct = action.anchor +
          model.network.predict(panel.state, action.action, features)[0];
      const double reflected = action.anchor + model.network.predict(
          audit::mirror(panel.state), kBoardSize - 1 - action.action,
          features)[0];
      if (direct != reflected) {
        throw std::runtime_error("golden reflection exactness failed");
      }
      output << "{\"action\":" << action.action << ",\"d4Anchor\":"
             << action.anchor << ",\"value\":" << direct
             << ",\"reflectedValue\":" << reflected << '}';
    }
    output << "]}";
  }
  output << "]\n}\n";
  if (!output) throw std::runtime_error("NNUE golden write failed");
}

void writeArtifact(const RunOptions& options, const ExactCorpus& exact,
                   const CrossValidation& cv, const TrainingResult& final,
                   const Normalizer& normalizer, double feature_seconds,
                   double cv_seconds, double final_seconds,
                   double wall_seconds) {
  const std::string checkpoint_sha = fileSha256(options.checkpoint);
  const std::string golden_sha = fileSha256(options.golden);
  const std::string cache_sha = fileSha256(options.audit_cache);
  std::ofstream output(options.output, std::ios::trunc);
  if (!output) throw std::runtime_error("could not write NNUE artifact");
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"exact-D4-anchored-h200-sibling-NNUE\","
         << "\n  \"scope\":{\"offlineOnly\":true,\"gameplaySeedsOpened\":0,"
            "\"newPanelsGenerated\":0,\"freshPolicyTestRun\":false,"
            "\"architectureEvidenceOnly\":true,\"records\":" << kRecords
         << ",\"wholeOriginGames\":" << kGames << "},"
         << "\n  \"hashes\":{\"sourceSha256\":\"" << options.source_sha256
         << "\",\"corpusSha256\":\"" << kCorpusSha256
         << "\",\"auditSourceSha256\":\"" << kAuditSourceSha256
         << "\",\"fairD4Sha256\":\"" << kD4SourceSha256
         << "\",\"fairD1Sha256\":\"" << kD1SourceSha256
         << "\",\"behaviorSha256\":\"" << kBehaviorSha256
         << "\",\"engineSha256\":\"" << kEngineSha256
         << "\",\"auditCacheSha256\":\"" << cache_sha
         << "\",\"checkpointSha256\":\"" << checkpoint_sha
         << "\",\"goldenSha256\":\"" << golden_sha << "\"},"
         << "\n  \"architecture\":{\"fixed\":true,\"reflection\":"
            "\"exact shared two-orientation average\","
            "\"publicCategoricalInputs\":[\"49 root cells\",\"next disc\","
            "\"rise phase\",\"candidate action\"],\"numericFeatures\":"
         << kFeatureCount << ",\"hidden\":" << kHidden
         << ",\"heads\":[\"D4-relative h200 score residual\","
            "\"mean survived moves\",\"mean numbered clears\","
            "\"mean covers revealed\",\"material downside upper99\"],"
            "\"anchor\":\"within-root centered/range-normalized exact D4 Q\","
            "\"D1QAuxiliaryInput\":true,\"commonImmediateSuccessors\":"
         << kSuccessorSamples << ",\"parameters\":"
         << final.network.parameterCount() << ",\"checkpointBytes\":"
         << std::filesystem::file_size(options.checkpoint) << "},"
         << "\n  \"definitions\":{"
            "\"top1\":\"selected action reaches the maximum stored meanScoreReturn within 1e-9\","
            "\"pairwise\":\"equal-weight concordance over every legal sibling pair; a target or prediction tie receives one-half credit\","
            "\"normalizedRegret\":\"(stored root maximum - selected stored value)/(stored root maximum - stored root minimum), zero on all-tie roots\","
            "\"nearTie\":\"absolute stored sibling difference at most 0.10 of root target range\","
            "\"decisive\":\"absolute stored sibling difference at least 0.50 of root target range\"},"
         << "\n  \"training\":{\"folding\":"
            "\"strict leave-one-entire-origin-game-out; every root scored once by a model trained on the other seven origins\","
            "\"target\":\"stored h200/255-scenario meanScoreReturn\","
            "\"objective\":\"grouped within-root pairwise plus listwise ranking, residual point and four auxiliary losses\","
            "\"epochs\":" << kEpochs << ",\"batchRoots\":" << kBatchRoots
         << ",\"learningRate\":" << kLearningRate
         << ",\"pairWeight\":" << kPairWeight
         << ",\"listWeight\":" << kListWeight
         << ",\"pointWeight\":" << kPointWeight
         << ",\"auxiliaryWeight\":" << kAuxiliaryWeight
         << ",\"residualWeight\":" << kResidualWeight << "},"
         << "\n  \"crossValidation\":{\"overall\":";
  writeEvaluation(output, cv.overall);
  output << ",\"folds\":[";
  for (int fold = 0; fold < kGames; ++fold) {
    if (fold) output << ',';
    const FoldResult& value = cv.folds[fold];
    output << "{\"heldoutOrigin\":\"" << audit::hex64(kGameStart + fold)
           << "\",\"trainingRoots\":" << value.training_roots
           << ",\"validationRoots\":" << value.validation_roots
           << ",\"firstLoss\":" << value.first_loss
           << ",\"finalLoss\":" << value.final_loss
           << ",\"improvesPairwiseAndRegretWithoutTop1Regression\":"
           << (value.improves_pairwise_regret_without_top1_regression ? "true" : "false")
           << ",\"evaluation\":";
    writeEvaluation(output, value.evaluation); output << '}';
  }
  output << "],\"fourGameHalves\":[";
  writeEvaluation(output, cv.halves[0]); output << ',';
  writeEvaluation(output, cv.halves[1]);
  output << "]},"
         << "\n  \"frozenPass\":{\"requiredTop1Gain\":" << kTop1Gain
         << ",\"requiredPairwiseGain\":" << kPairwiseGain
         << ",\"maximumRegretRatio\":" << kRegretRatio
         << ",\"requiredImprovedFolds\":" << kRequiredImprovedFolds
         << ",\"improvedFolds\":" << cv.improved_folds
         << ",\"bothFourGameHalvesImprovePairwiseAndRegret\":"
         << (pairwiseRegretImproves(cv.halves[0]) &&
                     pairwiseRegretImproves(cv.halves[1]) ? "true" : "false")
         << ",\"passed\":" << (cv.passed ? "true" : "false") << "},"
         << "\n  \"assessment\":{\"directH200ResidualLearnableBeyondD4\":"
         << (cv.passed ? "true" : "false")
         << ",\"basis\":\"the preregistered frozen whole-origin pass; no policy-performance inference\"},"
         << "\n  \"finalFit\":{\"firstLoss\":" << final.first_loss
         << ",\"finalLoss\":" << final.final_loss
         << ",\"fingerprint\":\"" << audit::hex64(
                fingerprint(final.network, normalizer))
         << "\",\"checkpoint\":\"" << audit::jsonEscape(options.checkpoint)
         << "\",\"golden\":\"" << audit::jsonEscape(options.golden) << "\"},"
         << "\n  \"exactD4Audit\":{\"cacheHit\":"
         << (exact.cache_hit ? "true" : "false")
         << ",\"work\":" << exact.work << ",\"nodes\":" << exact.nodes
         << ",\"cacheHits\":" << exact.cache_hits
         << ",\"maximumCacheEntries\":" << exact.maximum_cache_entries
         << ",\"seconds\":" << exact.seconds << "},"
         << "\n  \"tests\":{\"strictBuild\":true,\"sanitizerShadow\":true,"
            "\"checksumParser\":true,\"publicStateOnly\":true,"
            "\"metadataBlind\":true,\"reflectionExact\":true,"
            "\"checkpointRoundTrip\":true,\"golden\":true},"
         << "\n  \"resources\":{\"featureSeconds\":" << feature_seconds
         << ",\"crossValidationSeconds\":" << cv_seconds
         << ",\"finalTrainingSeconds\":" << final_seconds
         << ",\"wallSeconds\":" << wall_seconds
         << ",\"peakRssBytes\":" << audit::peakRssBytes()
         << ",\"rssLimitBytes\":" << kRssLimitBytes
         << ",\"checkpointLimitBytes\":" << kCheckpointLimitBytes << "}\n}\n";
  if (!output) throw std::runtime_error("NNUE artifact write failed");
}

void writeReadme(const RunOptions& options, const CrossValidation& cv) {
  std::ofstream output(options.readme, std::ios::trunc);
  if (!output) throw std::runtime_error("could not write offline README snippet");
  output << std::fixed << std::setprecision(6)
         << "## Exact-D4 anchored h200 sibling NNUE (offline only)\n\n"
         << "This architecture experiment used only the checksum-locked 477 h200 "
            "panels from eight already-burned origin games. It opened no gameplay "
            "seed, generated no panel, and ran no policy screen. Each prediction "
            "below came from strict leave-one-entire-origin-game-out training.\n\n"
         << "- Always-D4: top-1 " << cv.overall.baseline.top1
         << ", pairwise " << cv.overall.baseline.pairwise
         << ", normalized regret " << cv.overall.baseline.normalized_regret << ".\n"
         << "- D4 + residual NNUE: top-1 " << cv.overall.candidate.top1
         << ", pairwise " << cv.overall.candidate.pairwise
         << ", normalized regret " << cv.overall.candidate.normalized_regret << ".\n"
         << "- Whole-game folds satisfying pairwise/regret improvement with no "
            "top-1 regression: " << cv.improved_folds << "/8. Frozen pass: **"
         << (cv.passed ? "yes" : "no") << "**.\n"
         << "- Direct stored-h200 residual learnable beyond exact D4 under the "
            "preregistered gate: **" << (cv.passed ? "yes" : "no") << "**.\n\n"
         << "The checkpoint is " << std::filesystem::file_size(options.checkpoint)
         << " bytes. See `" << options.output
         << "` for all folds, two four-game halves, decisive/near-tie calibration, "
            "dependency hashes, and exact-D4 work. This is architecture evidence, "
            "not a gameplay-score claim.\n";
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

PublicState fixtureState() {
  PublicState fixture;
  constexpr std::string_view board =
      "0000000000000000000000000000000000000009003588488";
  for (int cell = 0; cell < kCellCount; ++cell) {
    fixture.board[cell] = static_cast<std::uint8_t>(board[cell] - '0');
  }
  fixture.next_disc = 6;
  fixture.moves_remaining = 3;
  return fixture;
}

bool selfTest(const RunOptions& options, std::ostream& output) {
  expect(sha256("abc") ==
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
         "SHA-256 golden failed");
  const PublicState fixture = fixtureState();
  const PublicState reflected = audit::mirror(fixture);
  expect(audit::publicHash(fixture) == audit::publicHash(reflected),
         "public reflection hash failed");

  std::ostringstream synthetic;
  synthetic << "{\"recordType\":\"deployment-panel-export-replay\","
            << "\"provenance\":{\"screenSeed\":" << kGameStart
            << ",\"moveIndex\":0,\"canonicalPublicHash\":\""
            << audit::hex64(audit::publicHash(fixture)) << "\",\"tapeSeed\":1},"
            << "\"modelInput\":{\"board\":\""
            << "0000000000000000000000000000000000000009003588488"
            << "\",\"nextDisc\":6,\"movesRemaining\":3,\"terminal\":false},"
            << "\"excludedFromModelInput\":[\"screenSeed\"],\"gate\":\"ultra\","
            << "\"fairD1Action\":3,\"selectedAction\":3,\"switched\":false,"
            << "\"actions\":[";
  for (int action = 0; action < kBoardSize; ++action) {
    if (action) synthetic << ',';
    synthetic << "{\"action\":" << action
              << ",\"meanScoreReturn\":" << 1000 + 20 * action
              << ",\"meanSurvivedMoves\":" << 100 + action
              << ",\"meanNumberedClears\":" << 10 + action
              << ",\"meanCoversRevealed\":" << 5 + action
              << ",\"survivingCutoffs\":0,\"pairedVsFairD1\":{"
                 "\"score\":{\"lowerOneSided99\":0},"
                 "\"moves\":{\"lowerOneSided99\":0},"
                 "\"materialDownsides\":0,\"materialDownsideUpper99\":0.1}}";
  }
  synthetic << "]}";
  const audit::PanelRecord parsed = audit::parsePanel(synthetic.str());
  expect(parsed.state == fixture && parsed.actions[6].mean_score == 1120,
         "locked panel parser golden failed");
  bool parser_rejected = false;
  try {
    std::string bad = synthetic.str();
    bad.replace(bad.find("deployment-panel-export-replay"), 30, "wrong-record");
    static_cast<void>(audit::parsePanel(bad));
  } catch (const std::exception&) { parser_rejected = true; }
  expect(parser_rejected, "parser accepted wrong metadata");

  audit::ExactSearch synthetic_search;
  synthetic_search.d1_q.fill(-std::numeric_limits<double>::infinity());
  synthetic_search.d4_q.fill(-std::numeric_limits<double>::infinity());
  synthetic_search.d4_immediate_score.fill(
      -std::numeric_limits<double>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    synthetic_search.d1_q[action] = 100.0 * action;
    synthetic_search.d4_q[action] = 120.0 * action;
    synthetic_search.d4_immediate_score[action] = 10.0 * action;
  }
  audit::PanelRecord reflected_panel = parsed;
  reflected_panel.state = reflected;
  reflected_panel.stored_public_hash = audit::publicHash(reflected);
  audit::ExactSearch reflected_search = synthetic_search;
  for (int action = 0; action < kBoardSize; ++action) {
    const int other = kBoardSize - 1 - action;
    reflected_panel.actions[other] = parsed.actions[action];
    reflected_search.d1_q[other] = synthetic_search.d1_q[action];
    reflected_search.d4_q[other] = synthetic_search.d4_q[action];
    reflected_search.d4_immediate_score[other] =
        synthetic_search.d4_immediate_score[action];
  }
  const RawPanel raw_feature_fixture =
      prepareRawPanel(parsed, synthetic_search);
  const RawPanel reflected_feature_fixture =
      prepareRawPanel(reflected_panel, reflected_search);
  for (int action = 0; action < kBoardSize; ++action) {
    const RawAction& direct_action = raw_feature_fixture.actions[action];
    const RawAction& mirror_action =
        reflected_feature_fixture.actions[kBoardSize - 1 - action];
    for (int feature = 0; feature < kFeatureCount; ++feature) {
      if (direct_action.features[feature] != mirror_action.features[feature]) {
        throw std::runtime_error(
            "common-successor numeric reflection failed at action " +
            std::to_string(action) + " feature " + std::to_string(feature));
      }
    }
    expect(direct_action.anchor == mirror_action.anchor,
           "D4 anchor reflection failed");
  }

  RawPanel raw;
  raw.origin_game = kGameStart;
  raw.state = fixture;
  for (int action = 0; action < kBoardSize; ++action) {
    RawAction value;
    value.action = action;
    value.anchor = static_cast<float>(action - 3) / 6.0f;
    value.label = parsed.actions[action];
    for (int feature = 0; feature < kFeatureCount; ++feature) {
      value.features[feature] = static_cast<float>((feature + action) % 11) / 11.0f;
    }
    raw.actions.push_back(value);
  }
  std::vector<RawPanel> raw_panels{raw, raw, raw, raw};
  const auto indices = allIndices(raw_panels.size());
  const Normalizer normalizer = fitNormalizer(raw_panels, indices);
  const auto prepared = preparePanels(raw_panels, indices, normalizer);
  Deadline deadline;
  const TrainingResult first = train(prepared, kNetworkSeed, 2, deadline, false);
  const TrainingResult repeated = train(prepared, kNetworkSeed, 2, deadline, false);
  expect(first.network.parameters() == repeated.network.parameters() &&
             first.final_loss == repeated.final_loss,
         "training determinism golden failed");
  for (const RawAction& action : raw.actions) {
    const auto features = normalizer.features(action.features);
    const auto direct = first.network.predict(fixture, action.action, features);
    const auto mirror = first.network.predict(
        reflected, kBoardSize - 1 - action.action, features);
    expect(direct == mirror, "reflection-exact NNUE test failed");
  }
  State metadata = audit::materialize(fixture);
  metadata.score = 99'999'999;
  metadata.level = 999;
  metadata.moves_played = 888;
  expect(audit::publicState(metadata) == fixture,
         "hidden metadata reached public model state");

  const std::string checkpoint = options.checkpoint + ".self-test";
  const std::string golden = options.golden + ".self-test";
  saveCheckpoint(checkpoint, first.network, normalizer);
  const FrozenModel loaded = loadCheckpoint(checkpoint);
  expect(loaded.network.parameters() == first.network.parameters() &&
             loaded.normalizer.mean == normalizer.mean &&
             std::filesystem::file_size(checkpoint) <= kCheckpointLimitBytes,
         "checkpoint round-trip test failed");
  const std::string corrupt_checkpoint = checkpoint + ".corrupt";
  std::string corrupt_payload = readWholeFile(checkpoint);
  corrupt_payload[8] ^= 0x01;
  {
    std::ofstream corrupt_output(corrupt_checkpoint, std::ios::binary |
                                                       std::ios::trunc);
    corrupt_output.write(corrupt_payload.data(), corrupt_payload.size());
  }
  bool corrupt_rejected = false;
  try {
    static_cast<void>(loadCheckpoint(corrupt_checkpoint));
  } catch (const std::exception&) { corrupt_rejected = true; }
  expect(corrupt_rejected, "corrupt checkpoint metadata was accepted");
  writeGolden(golden, loaded, raw_panels);
  expect(readWholeFile(golden).find("drop7-d4-h200-sibling-nnue-golden-v1") !=
             std::string::npos,
         "golden file test failed");
  deadline.check();
  output << "D4_H200_SIBLING_NNUE_SELF_TEST {\"passed\":true,"
            "\"offlineOnly\":true,\"gameplaySeedLanes\":0,"
            "\"sha256\":true,\"parser\":true,\"publicStateOnly\":true,"
            "\"metadataBlind\":true,\"reflectionExact\":true,"
            "\"deterministicTraining\":true,\"checkpoint\":true,"
            "\"golden\":true,\"checkpointBytes\":"
         << std::filesystem::file_size(checkpoint)
         << ",\"peakRssBytes\":" << audit::peakRssBytes() << "}\n";
  return true;
}

int run(const RunOptions& options, std::ostream& output) {
  const Deadline deadline;
  const std::vector<audit::PanelRecord> panels = loadLockedPanels(options);
  output << "D4_H200_SIBLING_NNUE_INPUT {\"records\":" << panels.size()
         << ",\"origins\":" << kGames << ",\"sha256\":\"" << kCorpusSha256
         << "\",\"gameplaySeedsOpened\":0}\n" << std::flush;
  const ExactCorpus exact = exactCorpus(options, panels, deadline);
  output << "D4_H200_SIBLING_NNUE_EXACT {\"cacheHit\":"
         << (exact.cache_hit ? "true" : "false") << ",\"work\":" << exact.work
         << ",\"seconds\":" << exact.seconds << "}\n" << std::flush;
  const Clock::time_point feature_started = Clock::now();
  const std::vector<RawPanel> raw =
      prepareRawPanels(panels, exact.searches, deadline);
  const double feature_seconds =
      std::chrono::duration<double>(Clock::now() - feature_started).count();
  const Clock::time_point cv_started = Clock::now();
  const CrossValidation cv = crossValidate(raw, deadline);
  const double cv_seconds =
      std::chrono::duration<double>(Clock::now() - cv_started).count();
  const auto indices = allIndices(raw.size());
  const Normalizer normalizer = fitNormalizer(raw, indices);
  const auto prepared = preparePanels(raw, indices, normalizer);
  const Clock::time_point final_started = Clock::now();
  const TrainingResult final = train(prepared, kNetworkSeed, kEpochs,
                                     deadline, true);
  const double final_seconds =
      std::chrono::duration<double>(Clock::now() - final_started).count();
  saveCheckpoint(options.checkpoint, final.network, normalizer);
  const FrozenModel loaded = loadCheckpoint(options.checkpoint);
  writeGolden(options.golden, loaded, raw);
  deadline.check();
  writeArtifact(options, exact, cv, final, normalizer, feature_seconds,
                cv_seconds, final_seconds, deadline.seconds());
  writeReadme(options, cv);
  output << std::setprecision(12)
         << "D4_H200_SIBLING_NNUE_RESULT {\"passed\":"
         << (cv.passed ? "true" : "false")
         << ",\"top1Gain\":"
         << cv.overall.candidate.top1 - cv.overall.baseline.top1
         << ",\"pairwiseGain\":"
         << cv.overall.candidate.pairwise - cv.overall.baseline.pairwise
         << ",\"regretRatio\":"
         << cv.overall.candidate.normalized_regret /
                cv.overall.baseline.normalized_regret
         << ",\"improvedFolds\":" << cv.improved_folds
         << ",\"directH200ResidualLearnableBeyondD4\":"
         << (cv.passed ? "true" : "false")
         << ",\"gameplaySeedsOpened\":0,\"newPanelsGenerated\":0,"
            "\"freshPolicyTestRun\":false,\"wallSeconds\":"
         << deadline.seconds() << ",\"peakRssBytes\":" << audit::peakRssBytes()
         << ",\"artifact\":\"" << audit::jsonEscape(options.output) << "\"}\n";
  return 0;
}

}  // namespace drop7::d4_h200_sibling_nnue

int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      drop7::d4_h200_sibling_nnue::RunOptions options;
      options.source_sha256 = std::string(64, '0');
      if (argc > 2) {
        options = drop7::d4_h200_sibling_nnue::parseOptions(argc, argv, 2);
      }
      return drop7::d4_h200_sibling_nnue::selfTest(options, std::cout)
                 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::d4_h200_sibling_nnue::parseOptions(argc, argv, 2);
      return drop7::d4_h200_sibling_nnue::run(options, std::cout);
    }
    std::cerr << "usage: drop7_d4_h200_sibling_nnue --self-test | --run "
                 "--source-sha256 HEX [--input PATH] [--output PATH] "
                 "[--checkpoint PATH] [--golden PATH] [--readme PATH] "
                 "[--audit-cache PATH] [--threads 1..4]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
