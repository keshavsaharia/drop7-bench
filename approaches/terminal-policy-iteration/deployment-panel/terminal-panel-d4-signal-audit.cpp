#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#include "../../fair-expectimax/reference/fair-only-depth4.cpp"
#undef DROP7_FAIR_ONLY_DEPTH4_LIBRARY

#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <utility>
#include <vector>

// Offline-only audit of previously evaluated h200 development panels.  This file
// contains no gameplay-seed lane and never advances a real game.  It rebuilds
// each stored public root, evaluates the exact fixed fair-D1 and fair-D4 root
// action/Q functions, and measures their sibling rankings against the stored
// common-tape h200 mean-score-return teacher.  Provenance is used only for
// whole-game diagnostic splitting and stratification, never as model input.
namespace drop7::terminal_panel_d4_signal_audit {

namespace d1 = drop7::fair_only_horizon;
namespace d4 = drop7::fair_only_depth4;
using Clock = std::chrono::steady_clock;

constexpr int kExpectedRecords = 477;
constexpr int kExpectedGames = 8;
constexpr std::uint32_t kExpectedGameStart = 0x3d6d'0010u;
constexpr int kMaximumThreads = 4;
constexpr int kDefaultThreads = 2;
constexpr double kWallLimitSeconds = 45.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr double kTieTolerance = 1.0e-9;
constexpr std::string_view kExpectedInputSha256 =
    "bfda8ae32fa0be3577c6b27f6413aba28f2854930e2f91a0dcf7674808f04196";
constexpr std::string_view kEngineSha256 =
    "b6dcde5f40dc39c6931b9a88e42bb351acd6fadaddd1e07691c41a82e44f3090";
constexpr std::string_view kD1SourceSha256 =
    "f9d4ea210e282ce5cc22894c17b5be92efb12029242aa5c3c6dc6412b383f42b";
constexpr std::string_view kD4SourceSha256 =
    "1cb42629db07b17850045bf3e5678c1fed5b58c73ab38bcfb699c94ee34fe6aa";
constexpr std::array<int, kBoardSize> kActionOrder{{3, 2, 4, 1, 5, 0, 6}};

static_assert(kLevelBonus == 17'000);
static_assert(d1::kChanceSamples == 5);
static_assert(d4::kCandidateDepth == 4);
static_assert(kExpectedGameStart + kExpectedGames == 0x3d6d'0018u);

struct Options {
  std::string input = "/tmp/drop7-terminal-policy-deployment-panels.jsonl";
  std::string input_sha256 = std::string(kExpectedInputSha256);
  std::string output = "/tmp/drop7-terminal-panel-d4-signal-audit.json";
  std::string readme = "/tmp/drop7-terminal-panel-d4-signal-audit-README.md";
  int threads = kDefaultThreads;
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string flag = argv[index];
    if (flag == "--input") result.input = argv[index + 1];
    else if (flag == "--input-sha256") result.input_sha256 = argv[index + 1];
    else if (flag == "--output") result.output = argv[index + 1];
    else if (flag == "--readme") result.readme = argv[index + 1];
    else if (flag == "--threads") {
      result.threads = std::stoi(argv[index + 1]);
      if (result.threads < 1 || result.threads > kMaximumThreads) {
        throw std::invalid_argument("threads must be in [1,4]");
      }
    } else {
      throw std::invalid_argument("unknown option " + flag);
    }
  }
  if (result.input_sha256 != kExpectedInputSha256) {
    throw std::invalid_argument("panel SHA-256 does not match frozen corpus");
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

void enforceRss() {
  if (peakRssBytes() > kRssLimitBytes) {
    throw std::runtime_error("terminal-panel audit exceeded 256 MiB RSS");
  }
}

struct Deadline {
  Clock::time_point started = Clock::now();

  double seconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }

  void check() const {
    if (seconds() > kWallLimitSeconds) {
      throw std::runtime_error("terminal-panel audit exceeded 45 minute wall");
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
  result.board = cfpi::detail::mirrorBoard(source.board);
  return result;
}

PublicState canonicalPublic(const PublicState& source, bool& mirrored) {
  return publicState(cfpi::detail::canonicalState(materialize(source), mirrored));
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

std::string hex64(std::uint64_t value) {
  std::ostringstream output;
  output << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
  return output.str();
}

std::string jsonEscape(std::string_view value) {
  std::string result;
  for (const char character : value) {
    if (character == '"' || character == '\\') result.push_back('\\');
    if (character == '\n') {
      result += "\\n";
    } else {
      result.push_back(character);
    }
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

  const std::size_t paired_at =
      afterMarker(object, "\"pairedVsFairD1\":");
  const std::size_t score_at = afterMarker(object, "\"score\":", paired_at);
  const std::size_t score_end =
      matchingDelimiter(object, score_at, '{', '}');
  result.score_lcb99 = numberAfter(
      object.substr(score_at, score_end - score_at + 1),
      "\"lowerOneSided99\":");
  const std::size_t moves_at =
      afterMarker(object, "\"moves\":", score_end + 1);
  const std::size_t moves_end =
      matchingDelimiter(object, moves_at, '{', '}');
  result.move_lcb99 = numberAfter(
      object.substr(moves_at, moves_end - moves_at + 1),
      "\"lowerOneSided99\":");
  result.material_downsides = static_cast<int>(
      integerAfter(object, "\"materialDownsides\":", moves_end));
  result.material_downside_upper99 =
      numberAfter(object, "\"materialDownsideUpper99\":", moves_end);
  return result;
}

PanelRecord parsePanel(std::string_view line) {
  if (line.find("\"recordType\":\"deployment-panel-export-replay\"") ==
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
  result.move_index =
      static_cast<int>(integerAfter(line, "\"moveIndex\":"));
  result.stored_public_hash = parseHex64(
      stringAfter(line, "\"canonicalPublicHash\":\""));

  const std::string board = stringAfter(line, "\"board\":\"");
  if (board.size() != kCellCount) throw std::runtime_error("invalid board size");
  for (int cell = 0; cell < kCellCount; ++cell) {
    if (board[cell] < '0' || board[cell] > '9') {
      throw std::runtime_error("invalid board token");
    }
    result.state.board[cell] =
        static_cast<std::uint8_t>(board[cell] - '0');
    if (result.state.board[cell] > kCracked) {
      throw std::runtime_error("out-of-domain board token");
    }
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
    if (line.substr(cursor, 4) == "null") {
      cursor += 4;
      continue;
    }
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
  if (!result.actions[result.stored_d1_action].legal ||
      !result.actions[result.deployment_action].legal ||
      publicHash(result.state) != result.stored_public_hash) {
    throw std::runtime_error("panel public-state invariant failed");
  }
  return result;
}

std::vector<PanelRecord> loadPanels(const Options& options) {
  std::ifstream input(options.input);
  if (!input) throw std::runtime_error("could not open frozen panel corpus");
  std::vector<PanelRecord> result;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty()) result.push_back(parsePanel(line));
  }
  if (result.size() != kExpectedRecords) {
    throw std::runtime_error("frozen panel record count mismatch");
  }
  std::map<std::uint32_t, std::set<int>> moves;
  for (const PanelRecord& panel : result) {
    moves[panel.origin_game].insert(panel.move_index);
  }
  if (moves.size() != kExpectedGames) {
    throw std::runtime_error("frozen panel game count mismatch");
  }
  for (int game = 0; game < kExpectedGames; ++game) {
    const std::uint32_t seed =
        kExpectedGameStart + static_cast<std::uint32_t>(game);
    const auto found = moves.find(seed);
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
  if (source.terminal) throw std::invalid_argument("terminal search root");
  ExactSearch result;
  result.d1_q.fill(-std::numeric_limits<double>::infinity());
  bool mirrored = false;
  const State canonical =
      cfpi::detail::canonicalState(materialize(source), mirrored);
  d1::SearchContext d1_context;
  const d1::RootEvaluation d1_root =
      d1::rootDecision(canonical, 1, d1_context);
  if (d1_root.action < 0 || d1_context.work > 2 * kBoardSize * d1::kChanceSamples ||
      !d1_context.cache.empty()) {
    throw std::runtime_error("exact fair-D1 root did not complete");
  }
  result.d1_action = mirrored ? kBoardSize - 1 - d1_root.action
                              : d1_root.action;
  for (int canonical_action = 0; canonical_action < kBoardSize;
       ++canonical_action) {
    const int source_action = mirrored ? kBoardSize - 1 - canonical_action
                                       : canonical_action;
    result.d1_q[source_action] = d1_root.values[canonical_action];
  }
  result.d1_work = d1_context.work;

  const d4::SearchDecision d4_decision = d4::chooseDepth4Action(materialize(source));
  if (!d4_decision.complete ||
      d4_decision.completed_depth != d4::kCandidateDepth ||
      d4_decision.action < 0) {
    throw std::runtime_error("exact fair-D4 root did not complete");
  }
  result.d4_action = d4_decision.action;
  result.d4_q = d4_decision.root_values;
  result.d4_immediate_score = d4_decision.root_expected_scores;
  result.d4_work = d4_decision.work;
  result.d4_nodes = d4_decision.nodes;
  result.d4_cache_hits = d4_decision.cache_hits;
  result.d4_cache_entries = d4_decision.cache_entries;
  for (int action = 0; action < kBoardSize; ++action) {
    const bool legal = isLegal(source.board, action);
    if (legal != std::isfinite(result.d1_q[action]) ||
        legal != std::isfinite(result.d4_q[action])) {
      throw std::runtime_error("exact search legal/Q mismatch");
    }
  }
  if (!isLegal(source.board, result.d1_action) ||
      !isLegal(source.board, result.d4_action)) {
    throw std::runtime_error("exact search selected illegal action");
  }
  return result;
}

struct AuditRoot {
  PanelRecord panel{};
  ExactSearch search{};
  int occupied = 0;
  int maximum_height = 0;
  std::array<int, kBoardSize> heights{};
};

void deriveGeometry(AuditRoot& root) {
  root.heights = cfpi::detail::columnHeights(root.panel.state.board);
  for (const std::uint8_t cell : root.panel.state.board) {
    root.occupied += cell != kEmpty;
  }
  root.maximum_height =
      *std::max_element(root.heights.begin(), root.heights.end());
}

std::vector<AuditRoot> evaluateAll(const std::vector<PanelRecord>& panels,
                                   int threads, const Deadline& deadline) {
  std::vector<AuditRoot> result(panels.size());
  std::atomic<std::size_t> next{0};
  std::atomic<std::size_t> completed{0};
  std::mutex output_mutex;
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < threads; ++worker) {
    workers.push_back(std::async(std::launch::async, [&]() {
      while (true) {
        const std::size_t index = next.fetch_add(1);
        if (index >= panels.size()) return;
        deadline.check();
        AuditRoot root;
        root.panel = panels[index];
        root.search = exactSearch(root.panel.state);
        if (root.search.d1_action != root.panel.stored_d1_action) {
          throw std::runtime_error("stored and exact fair-D1 actions differ");
        }
        deriveGeometry(root);
        result[index] = std::move(root);
        enforceRss();
        const std::size_t done = completed.fetch_add(1) + 1;
        if (done % 20 == 0 || done == panels.size()) {
          const std::lock_guard<std::mutex> lock(output_mutex);
          std::cerr << "terminal-panel D4 audit " << done << '/'
                    << panels.size() << '\n';
        }
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

std::array<double, kBoardSize> teacherScores(const AuditRoot& root) {
  std::array<double, kBoardSize> result{};
  result.fill(-std::numeric_limits<double>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    if (root.panel.actions[action].legal) {
      result[action] = root.panel.actions[action].mean_score;
    }
  }
  return result;
}

std::array<double, kBoardSize> teacherMoves(const AuditRoot& root) {
  std::array<double, kBoardSize> result{};
  result.fill(-std::numeric_limits<double>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    if (root.panel.actions[action].legal) {
      result[action] = root.panel.actions[action].mean_moves;
    }
  }
  return result;
}

int bestAction(const std::array<double, kBoardSize>& values,
               const std::array<PanelAction, kBoardSize>& actions) {
  int best = -1;
  for (const int action : kActionOrder) {
    if (!actions[action].legal) continue;
    if (best < 0 || values[action] > values[best]) best = action;
  }
  return best;
}

struct Ranking {
  std::uint64_t roots = 0;
  double top1_credit = 0.0;
  double top2_credit = 0.0;
  std::uint64_t pairs = 0;
  double pairwise_credit = 0.0;
  double normalized_regret = 0.0;
  double sum_x = 0.0;
  double sum_y = 0.0;
  double sum_xx = 0.0;
  double sum_yy = 0.0;
  double sum_xy = 0.0;
};

void observeRanking(Ranking& result,
                    const std::array<double, kBoardSize>& prediction,
                    const std::array<double, kBoardSize>& target,
                    const std::array<PanelAction, kBoardSize>& actions,
                    int selected) {
  double target_minimum = std::numeric_limits<double>::infinity();
  double target_maximum = -std::numeric_limits<double>::infinity();
  std::vector<int> predicted_order;
  for (const int action : kActionOrder) {
    if (!actions[action].legal) continue;
    if (!std::isfinite(prediction[action]) || !std::isfinite(target[action])) {
      throw std::runtime_error("non-finite ranking value");
    }
    predicted_order.push_back(action);
    target_minimum = std::min(target_minimum, target[action]);
    target_maximum = std::max(target_maximum, target[action]);
  }
  if (predicted_order.empty() || !actions[selected].legal) {
    throw std::runtime_error("empty ranking root");
  }
  std::stable_sort(predicted_order.begin(), predicted_order.end(),
                   [&](int first, int second) {
                     return prediction[first] > prediction[second];
                   });
  result.top1_credit +=
      target[selected] + kTieTolerance >= target_maximum;
  const int top_count = std::min<int>(2, predicted_order.size());
  bool top_two = false;
  for (int rank = 0; rank < top_count; ++rank) {
    top_two = top_two ||
        target[predicted_order[rank]] + kTieTolerance >= target_maximum;
  }
  result.top2_credit += top_two;
  const double range = target_maximum - target_minimum;
  if (range > kTieTolerance) {
    result.normalized_regret +=
        (target_maximum - target[selected]) / range;
  }
  for (int first = 0; first < kBoardSize; ++first) {
    if (!actions[first].legal) continue;
    for (int second = first + 1; second < kBoardSize; ++second) {
      if (!actions[second].legal) continue;
      const double x = prediction[first] - prediction[second];
      const double y = target[first] - target[second];
      result.sum_x += x;
      result.sum_y += y;
      result.sum_xx += x * x;
      result.sum_yy += y * y;
      result.sum_xy += x * y;
      if (std::abs(x) <= kTieTolerance || std::abs(y) <= kTieTolerance) {
        result.pairwise_credit += 0.5;
      } else {
        result.pairwise_credit += (x > 0.0) == (y > 0.0);
      }
      ++result.pairs;
    }
  }
  ++result.roots;
}

double pearson(const Ranking& value) {
  if (value.pairs < 2) return 0.0;
  const double count = static_cast<double>(value.pairs);
  const double covariance = value.sum_xy - value.sum_x * value.sum_y / count;
  const double x_variance = value.sum_xx - value.sum_x * value.sum_x / count;
  const double y_variance = value.sum_yy - value.sum_y * value.sum_y / count;
  const double denominator =
      std::sqrt(std::max(0.0, x_variance) * std::max(0.0, y_variance));
  return denominator > 0.0 ? covariance / denominator : 0.0;
}

void writeRanking(std::ostream& output, const Ranking& value) {
  const double roots = static_cast<double>(std::max<std::uint64_t>(1, value.roots));
  const double pairs = static_cast<double>(std::max<std::uint64_t>(1, value.pairs));
  output << std::setprecision(12)
         << "{\"roots\":" << value.roots
         << ",\"top1WithTies\":" << value.top1_credit / roots
         << ",\"top2ContainsTeacherTop\":" << value.top2_credit / roots
         << ",\"pairwiseAccuracy\":" << value.pairwise_credit / pairs
         << ",\"normalizedRegret\":" << value.normalized_regret / roots
         << ",\"pairDifferencePearson\":" << pearson(value)
         << ",\"pairCount\":" << value.pairs << '}';
}

struct Summary {
  std::uint64_t roots = 0;
  Ranking d1_score{};
  Ranking d4_score{};
  Ranking d1_moves{};
  Ranking d4_moves{};
  Ranking d1_vs_d4{};
  std::uint64_t d1_d4_agreements = 0;
  std::uint64_t d4_differences = 0;
  std::uint64_t deployment_d1 = 0;
  std::uint64_t deployment_d4 = 0;
  std::uint64_t deployment_teacher_top = 0;
  std::uint64_t d4_score_better = 0;
  std::uint64_t d4_moves_nonworse = 0;
  std::uint64_t d4_mean_pareto_support = 0;
  std::uint64_t d4_confident99_support = 0;
  double d4_score_delta = 0.0;
  double d4_move_delta = 0.0;
  double d4_clear_delta = 0.0;
  double d4_reveal_delta = 0.0;
};

void observeSummary(Summary& result, const AuditRoot& root) {
  const auto scores = teacherScores(root);
  const auto moves = teacherMoves(root);
  observeRanking(result.d1_score, root.search.d1_q, scores,
                 root.panel.actions, root.search.d1_action);
  observeRanking(result.d4_score, root.search.d4_q, scores,
                 root.panel.actions, root.search.d4_action);
  observeRanking(result.d1_moves, root.search.d1_q, moves,
                 root.panel.actions, root.search.d1_action);
  observeRanking(result.d4_moves, root.search.d4_q, moves,
                 root.panel.actions, root.search.d4_action);
  observeRanking(result.d1_vs_d4, root.search.d1_q, root.search.d4_q,
                 root.panel.actions, root.search.d1_action);
  const int teacher_top = bestAction(scores, root.panel.actions);
  result.deployment_d1 +=
      root.panel.deployment_action == root.search.d1_action;
  result.deployment_d4 +=
      root.panel.deployment_action == root.search.d4_action;
  result.deployment_teacher_top +=
      root.panel.deployment_action == teacher_top;
  if (root.search.d1_action == root.search.d4_action) {
    ++result.d1_d4_agreements;
  } else {
    ++result.d4_differences;
    const PanelAction& first = root.panel.actions[root.search.d1_action];
    const PanelAction& fourth = root.panel.actions[root.search.d4_action];
    const double score_delta = fourth.mean_score - first.mean_score;
    const double move_delta = fourth.mean_moves - first.mean_moves;
    const bool score_better = score_delta > kTieTolerance;
    const bool moves_nonworse = move_delta >= -kTieTolerance;
    const bool cutoff_nonworse =
        fourth.surviving_cutoffs <= first.surviving_cutoffs;
    result.d4_score_better += score_better;
    result.d4_moves_nonworse += moves_nonworse;
    result.d4_mean_pareto_support +=
        score_better && moves_nonworse && cutoff_nonworse;
    result.d4_confident99_support +=
        fourth.score_lcb99 > 0.0 && fourth.move_lcb99 >= 0.0 &&
        cutoff_nonworse;
    result.d4_score_delta += score_delta;
    result.d4_move_delta += move_delta;
    result.d4_clear_delta += fourth.mean_clears - first.mean_clears;
    result.d4_reveal_delta += fourth.mean_reveals - first.mean_reveals;
  }
  ++result.roots;
}

void writeSummary(std::ostream& output, const Summary& value) {
  const double roots = static_cast<double>(std::max<std::uint64_t>(1, value.roots));
  const double differences =
      static_cast<double>(std::max<std::uint64_t>(1, value.d4_differences));
  output << "{\"roots\":" << value.roots << ",\"scoreTeacher\":{\"d1\":";
  writeRanking(output, value.d1_score);
  output << ",\"d4\":";
  writeRanking(output, value.d4_score);
  output << "},\"survivalTeacher\":{\"d1\":";
  writeRanking(output, value.d1_moves);
  output << ",\"d4\":";
  writeRanking(output, value.d4_moves);
  output << "},\"d1RankingVsD4Q\":";
  writeRanking(output, value.d1_vs_d4);
  output << ",\"actionRelationships\":{\"d1D4Agreements\":"
         << value.d1_d4_agreements << ",\"d4Differences\":"
         << value.d4_differences << ",\"d4DifferenceRate\":"
         << value.d4_differences / roots << ",\"deploymentMatchesD1\":"
         << value.deployment_d1 << ",\"deploymentMatchesD4\":"
         << value.deployment_d4 << ",\"deploymentMatchesTeacherTop\":"
         << value.deployment_teacher_top
         << "},\"d4AlternativeRisk\":{\"scoreBetter\":"
         << value.d4_score_better << ",\"movesNonworse\":"
         << value.d4_moves_nonworse << ",\"meanParetoSupport\":"
         << value.d4_mean_pareto_support
         << ",\"confident99Support\":" << value.d4_confident99_support
         << ",\"scoreBetterRate\":"
         << value.d4_score_better / differences
         << ",\"meanParetoSupportRate\":"
         << value.d4_mean_pareto_support / differences
         << ",\"meanScoreDelta\":" << value.d4_score_delta / differences
         << ",\"meanMoveDelta\":" << value.d4_move_delta / differences
         << ",\"meanClearDelta\":" << value.d4_clear_delta / differences
         << ",\"meanRevealDelta\":" << value.d4_reveal_delta / differences
         << "}}";
}

std::string occupancyBand(int occupied) {
  if (occupied <= 14) return "07-14";
  if (occupied <= 21) return "15-21";
  if (occupied <= 28) return "22-28";
  if (occupied <= 35) return "29-35";
  return "36-49";
}

std::string gamePhase(int move_index) {
  if (move_index < 20) return "early-00-19";
  if (move_index < 50) return "middle-20-49";
  return "late-50-plus";
}

using SummaryMap = std::map<std::string, Summary>;

struct Stratified {
  Summary overall{};
  SummaryMap origin_game;
  SummaryMap rise_phase;
  SummaryMap game_phase;
  SummaryMap occupancy;
  SummaryMap maximum_height;
  SummaryMap d4_differs;
};

Stratified stratify(const std::vector<AuditRoot>& roots) {
  Stratified result;
  for (const AuditRoot& root : roots) {
    const auto add = [&](Summary& value) { observeSummary(value, root); };
    add(result.overall);
    add(result.origin_game[hex64(root.panel.origin_game)]);
    add(result.rise_phase[std::to_string(root.panel.state.moves_remaining)]);
    add(result.game_phase[gamePhase(root.panel.move_index)]);
    add(result.occupancy[occupancyBand(root.occupied)]);
    add(result.maximum_height[std::to_string(root.maximum_height)]);
    add(result.d4_differs[root.search.d1_action == root.search.d4_action
                              ? "same"
                              : "different"]);
  }
  return result;
}

void writeSummaryMap(std::ostream& output, const SummaryMap& values) {
  output << '[';
  bool first = true;
  for (const auto& [name, summary] : values) {
    if (!first) output << ',';
    first = false;
    output << "{\"stratum\":\"" << jsonEscape(name) << "\",\"metrics\":";
    writeSummary(output, summary);
    output << '}';
  }
  output << ']';
}

constexpr int kGateFeatures = 7;
constexpr int kGateParameters = kGateFeatures + 1;
constexpr double kGateRidge = 0.5;
constexpr int kGateNewtonIterations = 30;
constexpr std::array<std::string_view, kGateFeatures> kGateFeatureNames{{
    "d4NormalizedAdvantageOverD1",
    "d1NormalizedPenaltyForD4",
    "occupancyFraction",
    "maximumHeightFraction",
    "riseMovesRemainingFraction",
    "d4MinusD1ColumnHeight",
    "d4MinusD1CenterDistance",
}};

double finiteRange(const std::array<double, kBoardSize>& values,
                   const std::array<PanelAction, kBoardSize>& actions) {
  double minimum = std::numeric_limits<double>::infinity();
  double maximum = -std::numeric_limits<double>::infinity();
  for (int action = 0; action < kBoardSize; ++action) {
    if (!actions[action].legal) continue;
    minimum = std::min(minimum, values[action]);
    maximum = std::max(maximum, values[action]);
  }
  return std::max(1.0, maximum - minimum);
}

struct GateExample {
  std::size_t root_index = 0;
  std::uint32_t game = 0;
  std::array<double, kGateFeatures> features{};
  bool pareto_support = false;
  bool confident99_support = false;
  bool score_support = false;
  double score_delta = 0.0;
  double move_delta = 0.0;
};

GateExample gateExample(const AuditRoot& root, std::size_t root_index) {
  if (root.search.d1_action == root.search.d4_action) {
    throw std::invalid_argument("gate example requires a D4 alternative");
  }
  GateExample result;
  result.root_index = root_index;
  result.game = root.panel.origin_game;
  const int first_action = root.search.d1_action;
  const int fourth_action = root.search.d4_action;
  const PanelAction& first = root.panel.actions[first_action];
  const PanelAction& fourth = root.panel.actions[fourth_action];
  const double d4_range = finiteRange(root.search.d4_q, root.panel.actions);
  const double d1_range = finiteRange(root.search.d1_q, root.panel.actions);
  result.features = {{
      (root.search.d4_q[fourth_action] - root.search.d4_q[first_action]) /
          d4_range,
      (root.search.d1_q[fourth_action] - root.search.d1_q[first_action]) /
          d1_range,
      static_cast<double>(root.occupied) / kCellCount,
      static_cast<double>(root.maximum_height) / kBoardSize,
      static_cast<double>(root.panel.state.moves_remaining) / kMovesPerLevel,
      static_cast<double>(root.heights[fourth_action] -
                          root.heights[first_action]) /
          kBoardSize,
      static_cast<double>(std::abs(fourth_action - kBoardSize / 2) -
                          std::abs(first_action - kBoardSize / 2)) /
          (kBoardSize / 2),
  }};
  result.score_delta = fourth.mean_score - first.mean_score;
  result.move_delta = fourth.mean_moves - first.mean_moves;
  const bool cutoff_nonworse =
      fourth.surviving_cutoffs <= first.surviving_cutoffs;
  result.score_support = result.score_delta > kTieTolerance;
  result.pareto_support = result.score_support &&
      result.move_delta >= -kTieTolerance && cutoff_nonworse;
  result.confident99_support = fourth.score_lcb99 > 0.0 &&
      fourth.move_lcb99 >= 0.0 && cutoff_nonworse;
  return result;
}

struct GateModel {
  std::array<double, kGateFeatures> mean{};
  std::array<double, kGateFeatures> scale{};
  std::array<double, kGateParameters> beta{};
};

double sigmoid(double value) {
  if (value >= 0.0) {
    const double exponential = std::exp(-value);
    return 1.0 / (1.0 + exponential);
  }
  const double exponential = std::exp(value);
  return exponential / (1.0 + exponential);
}

std::array<double, kGateParameters> designRow(
    const GateExample& example, const GateModel& model) {
  std::array<double, kGateParameters> result{};
  result[0] = 1.0;
  for (int feature = 0; feature < kGateFeatures; ++feature) {
    result[feature + 1] =
        (example.features[feature] - model.mean[feature]) /
        model.scale[feature];
  }
  return result;
}

std::array<double, kGateParameters> solveLinear(
    std::array<std::array<double, kGateParameters>, kGateParameters> matrix,
    std::array<double, kGateParameters> vector) {
  for (int pivot = 0; pivot < kGateParameters; ++pivot) {
    int best = pivot;
    for (int row = pivot + 1; row < kGateParameters; ++row) {
      if (std::abs(matrix[row][pivot]) > std::abs(matrix[best][pivot])) {
        best = row;
      }
    }
    if (std::abs(matrix[best][pivot]) < 1.0e-12) {
      throw std::runtime_error("singular gate fit");
    }
    std::swap(matrix[pivot], matrix[best]);
    std::swap(vector[pivot], vector[best]);
    const double denominator = matrix[pivot][pivot];
    for (int column = pivot; column < kGateParameters; ++column) {
      matrix[pivot][column] /= denominator;
    }
    vector[pivot] /= denominator;
    for (int row = 0; row < kGateParameters; ++row) {
      if (row == pivot) continue;
      const double factor = matrix[row][pivot];
      for (int column = pivot; column < kGateParameters; ++column) {
        matrix[row][column] -= factor * matrix[pivot][column];
      }
      vector[row] -= factor * vector[pivot];
    }
  }
  return vector;
}

GateModel fitGate(const std::vector<GateExample>& examples,
                  std::uint32_t excluded_game) {
  GateModel result;
  int count = 0;
  int positives = 0;
  for (const GateExample& example : examples) {
    if (example.game == excluded_game) continue;
    ++count;
    positives += example.pareto_support;
    for (int feature = 0; feature < kGateFeatures; ++feature) {
      result.mean[feature] += example.features[feature];
    }
  }
  if (count < kGateParameters || positives == 0 || positives == count) {
    throw std::runtime_error("degenerate whole-game gate training fold");
  }
  for (double& value : result.mean) value /= count;
  for (const GateExample& example : examples) {
    if (example.game == excluded_game) continue;
    for (int feature = 0; feature < kGateFeatures; ++feature) {
      const double centered = example.features[feature] - result.mean[feature];
      result.scale[feature] += centered * centered;
    }
  }
  for (double& value : result.scale) {
    value = std::max(1.0e-6, std::sqrt(value / count));
  }
  const double positive_weight = static_cast<double>(count) / (2.0 * positives);
  const double negative_weight =
      static_cast<double>(count) / (2.0 * (count - positives));
  for (int iteration = 0; iteration < kGateNewtonIterations; ++iteration) {
    std::array<double, kGateParameters> gradient{};
    std::array<std::array<double, kGateParameters>, kGateParameters> hessian{};
    for (const GateExample& example : examples) {
      if (example.game == excluded_game) continue;
      const auto row = designRow(example, result);
      const double score = std::inner_product(
          row.begin(), row.end(), result.beta.begin(), 0.0);
      const double probability = sigmoid(score);
      const double target = example.pareto_support ? 1.0 : 0.0;
      const double weight = example.pareto_support
                                ? positive_weight
                                : negative_weight;
      for (int first = 0; first < kGateParameters; ++first) {
        gradient[first] += weight * (probability - target) * row[first];
        for (int second = 0; second < kGateParameters; ++second) {
          hessian[first][second] += weight * probability *
              (1.0 - probability) * row[first] * row[second];
        }
      }
    }
    for (int parameter = 1; parameter < kGateParameters; ++parameter) {
      gradient[parameter] += kGateRidge * result.beta[parameter];
      hessian[parameter][parameter] += kGateRidge;
    }
    hessian[0][0] += 1.0e-8;
    const auto step = solveLinear(hessian, gradient);
    double maximum_step = 0.0;
    for (int parameter = 0; parameter < kGateParameters; ++parameter) {
      result.beta[parameter] -= step[parameter];
      maximum_step = std::max(maximum_step, std::abs(step[parameter]));
    }
    if (maximum_step < 1.0e-9) break;
  }
  return result;
}

double predictGate(const GateExample& example, const GateModel& model) {
  const auto row = designRow(example, model);
  const double score =
      std::inner_product(row.begin(), row.end(), model.beta.begin(), 0.0);
  return sigmoid(score);
}

struct BinaryMetrics {
  int examples = 0;
  int positives = 0;
  int predicted_positive = 0;
  int true_positive = 0;
  int true_negative = 0;
  int false_positive = 0;
  int false_negative = 0;
  double brier = 0.0;
  double auc = 0.0;
};

struct ChoiceMetrics {
  int roots = 0;
  double top1 = 0.0;
  double normalized_regret = 0.0;
  double score_delta_vs_d1 = 0.0;
  double move_delta_vs_d1 = 0.0;
  double clear_delta_vs_d1 = 0.0;
  double reveal_delta_vs_d1 = 0.0;
};

void observeChoice(ChoiceMetrics& result, const AuditRoot& root, int selected) {
  const auto targets = teacherScores(root);
  const int teacher = bestAction(targets, root.panel.actions);
  double minimum = std::numeric_limits<double>::infinity();
  double maximum = -std::numeric_limits<double>::infinity();
  for (int action = 0; action < kBoardSize; ++action) {
    if (!root.panel.actions[action].legal) continue;
    minimum = std::min(minimum, targets[action]);
    maximum = std::max(maximum, targets[action]);
  }
  result.top1 += targets[selected] + kTieTolerance >= targets[teacher];
  if (maximum - minimum > kTieTolerance) {
    result.normalized_regret +=
        (maximum - targets[selected]) / (maximum - minimum);
  }
  const PanelAction& baseline = root.panel.actions[root.search.d1_action];
  const PanelAction& choice = root.panel.actions[selected];
  result.score_delta_vs_d1 += choice.mean_score - baseline.mean_score;
  result.move_delta_vs_d1 += choice.mean_moves - baseline.mean_moves;
  result.clear_delta_vs_d1 += choice.mean_clears - baseline.mean_clears;
  result.reveal_delta_vs_d1 += choice.mean_reveals - baseline.mean_reveals;
  ++result.roots;
}

void writeChoice(std::ostream& output, const ChoiceMetrics& value) {
  const double count = static_cast<double>(std::max(1, value.roots));
  output << "{\"roots\":" << value.roots
         << ",\"teacherTop1\":" << value.top1 / count
         << ",\"normalizedRegret\":" << value.normalized_regret / count
         << ",\"meanScoreDeltaVsD1\":" << value.score_delta_vs_d1 / count
         << ",\"meanMoveDeltaVsD1\":" << value.move_delta_vs_d1 / count
         << ",\"meanClearDeltaVsD1\":" << value.clear_delta_vs_d1 / count
         << ",\"meanRevealDeltaVsD1\":" << value.reveal_delta_vs_d1 / count
         << '}';
}

struct FoldModel {
  std::uint32_t heldout_game = 0;
  GateModel model{};
};

struct GateAudit {
  std::vector<GateExample> examples;
  std::vector<double> probabilities;
  std::vector<FoldModel> folds;
  BinaryMetrics classification{};
  double score_pairwise_accuracy = 0.0;
  double always_d4_score_pairwise_accuracy = 0.0;
  ChoiceMetrics d1_choice{};
  ChoiceMetrics d4_choice{};
  ChoiceMetrics gate_choice{};
  bool reduces_d4_regret = false;
  bool adds_score_pairwise_signal = false;
};

GateAudit auditGate(const std::vector<AuditRoot>& roots) {
  GateAudit result;
  for (std::size_t index = 0; index < roots.size(); ++index) {
    if (roots[index].search.d1_action != roots[index].search.d4_action) {
      result.examples.push_back(gateExample(roots[index], index));
    }
  }
  result.probabilities.resize(result.examples.size());
  std::set<std::uint32_t> games;
  for (const GateExample& example : result.examples) games.insert(example.game);
  if (games.size() != kExpectedGames) {
    throw std::runtime_error("D4 alternatives did not span every origin game");
  }
  for (const std::uint32_t game : games) {
    FoldModel fold{game, fitGate(result.examples, game)};
    for (std::size_t index = 0; index < result.examples.size(); ++index) {
      if (result.examples[index].game == game) {
        result.probabilities[index] =
            predictGate(result.examples[index], fold.model);
      }
    }
    result.folds.push_back(std::move(fold));
  }

  std::vector<double> positive_probabilities;
  std::vector<double> negative_probabilities;
  double score_pairwise_credit = 0.0;
  double d4_score_pairwise_credit = 0.0;
  for (std::size_t index = 0; index < result.examples.size(); ++index) {
    const GateExample& example = result.examples[index];
    const AuditRoot& root = roots[example.root_index];
    const double probability = result.probabilities[index];
    const bool selected_d4 = probability >= 0.5;
    const bool target = example.pareto_support;
    ++result.classification.examples;
    result.classification.positives += target;
    result.classification.predicted_positive += selected_d4;
    result.classification.true_positive += selected_d4 && target;
    result.classification.true_negative += !selected_d4 && !target;
    result.classification.false_positive += selected_d4 && !target;
    result.classification.false_negative += !selected_d4 && target;
    result.classification.brier +=
        (probability - static_cast<double>(target)) *
        (probability - static_cast<double>(target));
    (target ? positive_probabilities : negative_probabilities)
        .push_back(probability);
    if (std::abs(example.score_delta) <= kTieTolerance) {
      score_pairwise_credit += 0.5;
      d4_score_pairwise_credit += 0.5;
    } else {
      score_pairwise_credit +=
          selected_d4 == (example.score_delta > 0.0);
      d4_score_pairwise_credit += example.score_delta > 0.0;
    }
    observeChoice(result.d1_choice, root, root.search.d1_action);
    observeChoice(result.d4_choice, root, root.search.d4_action);
    observeChoice(result.gate_choice, root,
                  selected_d4 ? root.search.d4_action
                              : root.search.d1_action);
  }
  double auc_credit = 0.0;
  for (const double positive : positive_probabilities) {
    for (const double negative : negative_probabilities) {
      if (positive > negative) auc_credit += 1.0;
      else if (positive == negative) auc_credit += 0.5;
    }
  }
  const double auc_pairs = static_cast<double>(positive_probabilities.size()) *
      negative_probabilities.size();
  result.classification.auc = auc_pairs > 0.0 ? auc_credit / auc_pairs : 0.0;
  result.classification.brier /=
      std::max(1, result.classification.examples);
  result.score_pairwise_accuracy =
      score_pairwise_credit / std::max<std::size_t>(1, result.examples.size());
  result.always_d4_score_pairwise_accuracy =
      d4_score_pairwise_credit /
      std::max<std::size_t>(1, result.examples.size());
  result.reduces_d4_regret =
      result.gate_choice.normalized_regret <
      result.d4_choice.normalized_regret - kTieTolerance;
  result.adds_score_pairwise_signal =
      result.score_pairwise_accuracy >
      result.always_d4_score_pairwise_accuracy + kTieTolerance;
  return result;
}

void writeGateModel(std::ostream& output, const FoldModel& fold) {
  output << "{\"heldoutOriginGame\":\"" << hex64(fold.heldout_game)
         << "\",\"standardizationMean\":[";
  for (int feature = 0; feature < kGateFeatures; ++feature) {
    if (feature) output << ',';
    output << fold.model.mean[feature];
  }
  output << "],\"standardizationScale\":[";
  for (int feature = 0; feature < kGateFeatures; ++feature) {
    if (feature) output << ',';
    output << fold.model.scale[feature];
  }
  output << "],\"standardizedCoefficients\":[";
  for (int parameter = 0; parameter < kGateParameters; ++parameter) {
    if (parameter) output << ',';
    output << fold.model.beta[parameter];
  }
  output << "]}";
}

void writeBinary(std::ostream& output, const BinaryMetrics& value) {
  const double positives = std::max(1, value.positives);
  const double negatives = std::max(1, value.examples - value.positives);
  const double predicted = std::max(1, value.predicted_positive);
  const double count = std::max(1, value.examples);
  const double sensitivity = value.true_positive / positives;
  const double specificity = value.true_negative / negatives;
  output << "{\"examples\":" << value.examples
         << ",\"positives\":" << value.positives
         << ",\"predictedPositive\":" << value.predicted_positive
         << ",\"truePositive\":" << value.true_positive
         << ",\"trueNegative\":" << value.true_negative
         << ",\"falsePositive\":" << value.false_positive
         << ",\"falseNegative\":" << value.false_negative
         << ",\"accuracy\":"
         << (value.true_positive + value.true_negative) / count
         << ",\"balancedAccuracy\":" << 0.5 * (sensitivity + specificity)
         << ",\"precision\":" << value.true_positive / predicted
         << ",\"recall\":" << sensitivity
         << ",\"specificity\":" << specificity
         << ",\"brier\":" << value.brier << ",\"auc\":" << value.auc
         << '}';
}

struct WorkSummary {
  std::uint64_t d1_work = 0;
  std::uint64_t d4_work = 0;
  std::uint64_t d4_nodes = 0;
  std::uint64_t d4_cache_hits = 0;
  std::size_t maximum_d4_cache_entries = 0;
};

WorkSummary summarizeWork(const std::vector<AuditRoot>& roots) {
  WorkSummary result;
  for (const AuditRoot& root : roots) {
    result.d1_work += root.search.d1_work;
    result.d4_work += root.search.d4_work;
    result.d4_nodes += root.search.d4_nodes;
    result.d4_cache_hits += root.search.d4_cache_hits;
    result.maximum_d4_cache_entries =
        std::max(result.maximum_d4_cache_entries,
                 root.search.d4_cache_entries);
  }
  return result;
}

bool rawSignalBeyondD4(const Stratified& stratified) {
  const Ranking& d4_score = stratified.overall.d4_score;
  return d4_score.roots > 0 &&
      d4_score.top1_credit / d4_score.roots < 0.95 &&
      d4_score.pairwise_credit / d4_score.pairs < 0.95 &&
      d4_score.normalized_regret / d4_score.roots > 0.01;
}

bool learnableGateSignal(const GateAudit& gate) {
  const BinaryMetrics& metrics = gate.classification;
  const double positive = std::max(1, metrics.positives);
  const double negative = std::max(1, metrics.examples - metrics.positives);
  const double balanced = 0.5 *
      (metrics.true_positive / positive + metrics.true_negative / negative);
  return balanced > 0.55 && gate.classification.auc > 0.55 &&
      gate.adds_score_pairwise_signal && gate.reduces_d4_regret;
}

void writeArtifact(const Options& options, const Stratified& stratified,
                   const GateAudit& gate, const WorkSummary& work,
                   double wall_seconds) {
  std::ofstream output(options.output, std::ios::trunc);
  if (!output) throw std::runtime_error("could not write audit artifact");
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"terminal-h200-vs-exact-fair-d4-offline-signal-audit\",\n"
         << "  \"scope\":{\"offlineOnly\":true,\"gameplaySeedsOpened\":0,"
            "\"newPanelsGenerated\":0,\"policyPerformanceClaimed\":false,"
            "\"inputRecords\":" << kExpectedRecords
         << ",\"originGames\":" << kExpectedGames << "},\n"
         << "  \"input\":{\"path\":\"" << jsonEscape(options.input)
         << "\",\"sha256\":\"" << options.input_sha256
         << "\",\"recordType\":\"deployment-panel-export-replay\","
            "\"horizon\":200,\"scenariosPerAction\":255,"
            "\"teacherTarget\":\"meanScoreReturn\","
            "\"riskTargets\":[\"meanSurvivedMoves\",\"survivingCutoffs\","
            "\"paired99LCB\"]},\n"
         << "  \"dependencies\":{\"engineSha256\":\"" << kEngineSha256
         << "\",\"fairD1Sha256\":\"" << kD1SourceSha256
         << "\",\"fairD4Sha256\":\"" << kD4SourceSha256 << "\"},\n"
         << "  \"definitions\":{"
            "\"top1WithTies\":\"selected action has maximum stored target within 1e-9\","
            "\"pairwiseAccuracy\":\"equal-weight legal sibling-pair sign concordance; ties score one half\","
            "\"normalizedRegret\":\"(teacher maximum - teacher value of selected action)/(teacher maximum - teacher minimum), averaged by root\","
            "\"meanParetoSupport\":\"D4 action has higher h200 mean score return, nonlower mean survival, and no additional surviving cutoff versus D1\","
            "\"confident99Support\":\"stored paired score LCB is positive, move LCB nonnegative, and no additional surviving cutoff\","
            "\"risePhase\":\"public movesRemaining in the five-drop rise cycle\","
            "\"gamePhase\":\"provenance-only diagnostic move bands; never a gate feature\"},\n"
         << "  \"overall\":";
  writeSummary(output, stratified.overall);
  output << ",\n  \"strata\":{\"originGame\":";
  writeSummaryMap(output, stratified.origin_game);
  output << ",\"risePhase\":";
  writeSummaryMap(output, stratified.rise_phase);
  output << ",\"gamePhase\":";
  writeSummaryMap(output, stratified.game_phase);
  output << ",\"occupancy\":";
  writeSummaryMap(output, stratified.occupancy);
  output << ",\"maximumHeight\":";
  writeSummaryMap(output, stratified.maximum_height);
  output << ",\"d4DiffersFromD1\":";
  writeSummaryMap(output, stratified.d4_differs);
  output << "},\n  \"wholeGameLeaveOneOutRiskGate\":{"
         << "\"diagnosticOnly\":true,\"target\":\"meanParetoSupport\","
            "\"threshold\":0.5,\"ridge\":" << kGateRidge
         << ",\"features\":[";
  for (int feature = 0; feature < kGateFeatures; ++feature) {
    if (feature) output << ',';
    output << '"' << kGateFeatureNames[feature] << '"';
  }
  output << "],\"classification\":";
  writeBinary(output, gate.classification);
  output << ",\"scorePairwiseAccuracy\":" << gate.score_pairwise_accuracy
         << ",\"alwaysD4ScorePairwiseAccuracy\":"
         << gate.always_d4_score_pairwise_accuracy
         << ",\"addsScorePairwiseSignal\":"
         << (gate.adds_score_pairwise_signal ? "true" : "false")
         << ",\"reducesD4Regret\":"
         << (gate.reduces_d4_regret ? "true" : "false")
         << ",\"choiceDiagnostics\":{\"d1\":";
  writeChoice(output, gate.d1_choice);
  output << ",\"alwaysD4\":";
  writeChoice(output, gate.d4_choice);
  output << ",\"gate\":";
  writeChoice(output, gate.gate_choice);
  output << "},\"foldModels\":[";
  for (std::size_t fold = 0; fold < gate.folds.size(); ++fold) {
    if (fold) output << ',';
    writeGateModel(output, gate.folds[fold]);
  }
  output << "]},\n  \"signalAssessment\":{\"rawLabelsDifferMateriallyFromD4\":"
         << (rawSignalBeyondD4(stratified) ? "true" : "false")
         << ",\"incrementalSignalLearnableByTinyWholeGameGate\":"
         << (learnableGateSignal(gate) ? "true" : "false")
         << ",\"interpretation\":\"Raw disagreement establishes information not present in D4 ranking; only leakage-free whole-game gate improvement establishes that this tiny public model can recover it.\"},\n"
         << "  \"work\":{\"d1Work\":" << work.d1_work
         << ",\"d4Work\":" << work.d4_work
         << ",\"d4Nodes\":" << work.d4_nodes
         << ",\"d4CacheHits\":" << work.d4_cache_hits
         << ",\"maximumD4CacheEntries\":"
         << work.maximum_d4_cache_entries << "},\n"
         << "  \"resources\":{\"wallSeconds\":" << wall_seconds
         << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"rssLimitBytes\":" << kRssLimitBytes << "}\n}\n";
  output.close();
  if (!output) throw std::runtime_error("audit artifact write failed");
}

void writeReadme(const Options& options, const Stratified& stratified,
                 const GateAudit& gate, const WorkSummary& work,
                 double wall_seconds) {
  std::ofstream output(options.readme, std::ios::trunc);
  if (!output) throw std::runtime_error("could not write audit README snippet");
  const Ranking& d1_score = stratified.overall.d1_score;
  const Ranking& d4_score = stratified.overall.d4_score;
  const auto rate = [](double numerator, std::uint64_t denominator) {
    return numerator / std::max<std::uint64_t>(1, denominator);
  };
  const BinaryMetrics& binary = gate.classification;
  const double balanced = 0.5 *
      (binary.true_positive / static_cast<double>(std::max(1, binary.positives)) +
       binary.true_negative /
           static_cast<double>(std::max(1, binary.examples - binary.positives)));
  output << std::fixed << std::setprecision(6)
         << "## Burned h200 panel vs exact fair-D4 audit\n\n"
         << "This is an offline diagnostic over all " << kExpectedRecords
         << " stored roots from " << kExpectedGames
         << " whole origin games. It opened no gameplay seeds, generated no new "
            "panels, and does not estimate policy score. The primary teacher is "
            "the stored h200/255-scenario `meanScoreReturn`; survived moves and "
            "paired risk statistics are reported separately.\n\n"
         << "- D1 vs h200: top-1 "
         << rate(d1_score.top1_credit, d1_score.roots) << ", pairwise "
         << rate(d1_score.pairwise_credit, d1_score.pairs) << ", regret "
         << rate(d1_score.normalized_regret, d1_score.roots) << ".\n"
         << "- D4 vs h200: top-1 "
         << rate(d4_score.top1_credit, d4_score.roots) << ", pairwise "
         << rate(d4_score.pairwise_credit, d4_score.pairs) << ", regret "
         << rate(d4_score.normalized_regret, d4_score.roots) << ".\n"
         << "- D4 differed from D1 at "
         << stratified.overall.d4_differences << " roots; h200 mean-Pareto "
            "supported the D4 alternative at "
         << stratified.overall.d4_mean_pareto_support << " of them, with "
         << stratified.overall.d4_confident99_support
         << " meeting the paired 99% support definition.\n"
         << "- The whole-origin-game leave-one-out tiny risk gate reached balanced "
            "accuracy " << balanced << ", AUC " << binary.auc
         << ", and D1-vs-D4 score-pair accuracy "
         << gate.score_pairwise_accuracy << " versus "
         << gate.always_d4_score_pairwise_accuracy << " for always taking D4.\n"
         << "- Raw incremental h200 signal beyond D4: **"
         << (rawSignalBeyondD4(stratified) ? "yes" : "no")
         << "**. Recoverable incremental signal with this tiny leakage-free gate: **"
         << (learnableGateSignal(gate) ? "yes" : "no") << "**.\n\n"
         << "See `" << options.output
         << "` for whole-game, rise-phase, game-phase, occupancy, maximum-height, "
            "and D4-vs-D1-disagreement strata. Exact-search work was "
         << work.d4_work << " D4 units; runtime was " << wall_seconds
         << " seconds and peak RSS was " << peakRssBytes() << " bytes.\n";
  output.close();
  if (!output) throw std::runtime_error("audit README write failed");
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

bool selfTest(std::ostream& output) {
  PublicState fixture;
  constexpr std::string_view board =
      "0000000000000000000000000000000000000009003588488";
  for (int cell = 0; cell < kCellCount; ++cell) {
    fixture.board[cell] = static_cast<std::uint8_t>(board[cell] - '0');
  }
  fixture.next_disc = 6;
  fixture.moves_remaining = 3;
  const PublicState reflected = mirror(fixture);
  expect(publicHash(fixture) == publicHash(reflected),
         "public hash reflection self-test failed");
  const ExactSearch direct = exactSearch(fixture);
  const ExactSearch mirrored = exactSearch(reflected);
  expect(direct.d1_action == kBoardSize - 1 - mirrored.d1_action &&
             direct.d4_action == kBoardSize - 1 - mirrored.d4_action,
         "exact action reflection self-test failed");
  for (int action = 0; action < kBoardSize; ++action) {
    const int other = kBoardSize - 1 - action;
    expect(direct.d1_q[action] == mirrored.d1_q[other] &&
               direct.d4_q[action] == mirrored.d4_q[other],
           "exact Q reflection self-test failed");
  }
  State metadata = materialize(fixture);
  metadata.score = 9'999'999;
  metadata.level = 777;
  metadata.moves_played = 888;
  expect(publicState(metadata) == fixture && materialize(fixture).score == 0 &&
             materialize(fixture).level == 1 &&
             materialize(fixture).moves_played == 0,
         "public-state/metadata self-test failed");

  std::ostringstream synthetic;
  synthetic << "{\"recordType\":\"deployment-panel-export-replay\","
            << "\"provenance\":{\"screenSeed\":" << kExpectedGameStart
            << ",\"moveIndex\":0,\"canonicalPublicHash\":\""
            << hex64(publicHash(fixture)) << "\",\"tapeSeed\":1},"
            << "\"modelInput\":{\"board\":\"" << board
            << "\",\"nextDisc\":6,\"movesRemaining\":3,"
               "\"terminal\":false},\"excludedFromModelInput\":[],"
               "\"gate\":\"ultra\",\"fairD1Action\":"
            << direct.d1_action << ",\"selectedAction\":"
            << direct.d1_action << ",\"switched\":false,\"actions\":[";
  for (int action = 0; action < kBoardSize; ++action) {
    if (action) synthetic << ',';
    synthetic << "{\"action\":" << action
              << ",\"meanScoreReturn\":" << 1000 + action
              << ",\"meanSurvivedMoves\":" << 10 + action
              << ",\"meanNumberedClears\":" << action
              << ",\"meanCoversRevealed\":" << action / 2.0
              << ",\"survivingCutoffs\":0,\"pairedVsFairD1\":{"
                 "\"score\":{\"lowerOneSided99\":"
              << action << "},\"moves\":{\"lowerOneSided99\":"
              << action << "},\"materialDownsides\":0,"
                 "\"materialDownsideUpper99\":0}}";
  }
  synthetic << "]}";
  const PanelRecord parsed = parsePanel(synthetic.str());
  expect(parsed.state == fixture && parsed.stored_d1_action == direct.d1_action &&
             parsed.actions[6].mean_score == 1006.0,
         "panel parser self-test failed");

  Ranking ranking;
  std::array<double, kBoardSize> prediction{{0, 1, 2, 3, 4, 5, 6}};
  std::array<double, kBoardSize> target{{0, 1, 2, 3, 4, 5, 6}};
  observeRanking(ranking, prediction, target, parsed.actions, 6);
  expect(ranking.roots == 1 && ranking.top1_credit == 1.0 &&
             ranking.pairwise_credit == ranking.pairs &&
             ranking.normalized_regret == 0.0,
         "ranking metric self-test failed");

  std::vector<GateExample> gate_examples;
  for (int index = 0; index < 12; ++index) {
    GateExample example;
    example.game = static_cast<std::uint32_t>(index % 3);
    example.features[0] = index / 11.0;
    example.features[1] = -example.features[0];
    example.features[2] = 0.25 + 0.01 * index;
    example.features[3] = 0.5;
    example.features[4] = (index % 5 + 1) / 5.0;
    example.features[5] = (index % 3 - 1) / 7.0;
    example.features[6] = (index % 2) / 3.0;
    example.pareto_support = index >= 6;
    gate_examples.push_back(example);
  }
  const GateModel first = fitGate(gate_examples, 99);
  const GateModel second = fitGate(gate_examples, 99);
  expect(first.beta == second.beta &&
             predictGate(gate_examples.front(), first) <
                 predictGate(gate_examples.back(), first),
         "deterministic risk-gate self-test failed");
  enforceRss();
  output << std::setprecision(12)
         << "TERMINAL_PANEL_D4_SIGNAL_AUDIT_SELF_TEST {\"passed\":true,"
         << "\"offlineOnly\":true,\"gameplaySeedLanes\":0,"
         << "\"parser\":true,\"publicStateOnly\":true,"
         << "\"metadataBlind\":true,\"reflection\":true,"
         << "\"exactD1\":true,\"exactD4\":true,"
         << "\"rankingMetrics\":true,\"wholeGameGate\":true,"
         << "\"peakRssBytes\":" << peakRssBytes() << "}\n";
  return true;
}

int run(const Options& options, std::ostream& output) {
  const Deadline deadline;
  const std::vector<PanelRecord> panels = loadPanels(options);
  output << "TERMINAL_PANEL_D4_SIGNAL_INPUT {\"records\":"
         << panels.size() << ",\"games\":" << kExpectedGames
         << ",\"sha256\":\"" << options.input_sha256
         << "\",\"gameplaySeedsOpened\":0}\n" << std::flush;
  const std::vector<AuditRoot> roots =
      evaluateAll(panels, options.threads, deadline);
  const Stratified stratified = stratify(roots);
  const GateAudit gate = auditGate(roots);
  const WorkSummary work = summarizeWork(roots);
  deadline.check();
  enforceRss();
  const double wall_seconds = deadline.seconds();
  writeArtifact(options, stratified, gate, work, wall_seconds);
  writeReadme(options, stratified, gate, work, wall_seconds);
  output << std::setprecision(12)
         << "TERMINAL_PANEL_D4_SIGNAL_RESULT {\"records\":" << roots.size()
         << ",\"d4Differences\":" << stratified.overall.d4_differences
         << ",\"d4MeanParetoSupport\":"
         << stratified.overall.d4_mean_pareto_support
         << ",\"rawSignalBeyondD4\":"
         << (rawSignalBeyondD4(stratified) ? "true" : "false")
         << ",\"tinyGateLearnableSignal\":"
         << (learnableGateSignal(gate) ? "true" : "false")
         << ",\"wallSeconds\":" << wall_seconds
         << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"artifact\":\"" << jsonEscape(options.output)
         << "\",\"readme\":\"" << jsonEscape(options.readme) << "\"}\n";
  return 0;
}

}  // namespace drop7::terminal_panel_d4_signal_audit

int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::terminal_panel_d4_signal_audit::selfTest(std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::terminal_panel_d4_signal_audit::parseOptions(argc, argv, 2);
      return drop7::terminal_panel_d4_signal_audit::run(options, std::cout);
    }
    std::cerr << "usage: drop7_terminal_panel_d4_signal_audit "
                 "--self-test | --run [--input PATH] [--input-sha256 HEX] "
                 "[--output PATH] [--readme PATH] [--threads 1..4]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
