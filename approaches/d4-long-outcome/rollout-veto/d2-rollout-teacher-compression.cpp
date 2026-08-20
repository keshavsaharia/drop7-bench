#define DROP7_D4_D2_ROLLOUT_VETO_LIBRARY
#include "d4-d2-rollout-veto.cpp"
#undef DROP7_D4_D2_ROLLOUT_VETO_LIBRARY

#include <atomic>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <unordered_set>

// Compresses the fixed 179-state D2/s7/h25 teacher export using replay only.
// This executable never creates or advances a real game and
// therefore cannot consume a gameplay seed.  It only materializes public
// states from the export and gives every legal root action the same synthetic
// tape that the teacher used.
namespace drop7::d2_rollout_teacher_compression {

namespace teacher = drop7::d4_d2_rollout_veto;
namespace fair = drop7::fair_only_horizon;
namespace d4 = drop7::fair_only_depth4;

constexpr int kTeacherRecords = 179;
constexpr int kTeacherSwitches = 12;
constexpr int kTeacherFallbacks = 167;
constexpr std::uintmax_t kTeacherBytes = 1'203'731;
constexpr std::uint32_t kTeacherGameSeed = 0x3ded'0000u;
constexpr int kMaximumScenarios = 7;
constexpr int kMaximumHorizon = 25;
constexpr int kEventsPerStep = teacher::kEventsPerStep;
constexpr double kPairedT975Df2 = 4.302653;
constexpr double kPairedT975Df4 = 2.776445;
constexpr double kPairedT975Df6 = 2.446912;
constexpr double kMaximumRootQLoss = teacher::kMaximumRootQLoss;
constexpr std::uint64_t kMaximumRssBytes = 128u * 1024u * 1024u;

constexpr std::uint64_t kWorstD1Work =
    2u * kBoardSize * fair::kChanceSamples;
constexpr std::uint64_t kWorstD2Work = teacher::kWorstD2Work;
constexpr std::size_t kWorstD1CacheEntries = 0;
constexpr std::size_t kWorstD2CacheEntries = teacher::kWorstD2CacheEntries;

static_assert(kTeacherRecords == kTeacherSwitches + kTeacherFallbacks);
static_assert(kWorstD1Work == 70);
static_assert(kWorstD2Work == 2'485);
static_assert(fair::kChanceSamples == 5);
static_assert(kMaximumRootQLoss == 7'000.0);
static_assert(teacher::kTapeSeedDomain == 0x4432'5254u);
static_assert(teacher::kRevealTapeDomain == 0x4432'5256u);
static_assert(teacher::kVisibleTapeDomain == 0x4432'5653u);
static_assert((kTeacherGameSeed >> 24) != 0x7du &&
              (kTeacherGameSeed >> 24) != 0xd7u);

using ObservableState = teacher::ObservableState;

enum class ContinuationPolicy { kD1, kD2, kD2AtOne, kD2AtOneOrFive };

struct Specification {
  char id = '?';
  const char* label = "";
  int scenarios = 0;
  int horizon = 0;
  ContinuationPolicy continuation = ContinuationPolicy::kD1;
};

// Preregistered before examining approximation outcomes.  `scenarios` is the
// number of paired root tapes; the fair continuation search itself always
// keeps its fixed five chance strata.
constexpr std::array<Specification, 6> kSpecifications{{
    {'A', "fair-D1/s7/h25", 7, 25, ContinuationPolicy::kD1},
    {'B', "fair-D2/s3/h25", 3, 25, ContinuationPolicy::kD2},
    {'C', "fair-D2/s5/h15", 5, 15, ContinuationPolicy::kD2},
    {'D', "fair-D2/s3/h15", 3, 15, ContinuationPolicy::kD2},
    {'E', "hybrid-D2@1/s7/h25", 7, 25,
     ContinuationPolicy::kD2AtOne},
    {'F', "hybrid-D2@1,5/s7/h25", 7, 25,
     ContinuationPolicy::kD2AtOneOrFive},
}};

constexpr double pairedTCutoff(int scenarios) {
  return scenarios == 3   ? kPairedT975Df2
         : scenarios == 5 ? kPairedT975Df4
         : scenarios == 7 ? kPairedT975Df6
                          : -1.0;
}

struct Options {
  std::string teacher =
      "/tmp/drop7-d4-d2-rollout-veto-teacher.jsonl";
  std::string output =
      "/tmp/drop7-d2-rollout-teacher-compression.json";
  int threads = 4;
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--teacher") {
      if (++index >= argc) throw std::invalid_argument("missing --teacher value");
      result.teacher = argv[index];
    } else if (argument == "--output") {
      if (++index >= argc) throw std::invalid_argument("missing --output value");
      result.output = argv[index];
    } else if (argument == "--threads") {
      if (++index >= argc) throw std::invalid_argument("missing --threads value");
      result.threads = std::stoi(argv[index]);
      if (result.threads < 1 || result.threads > 8) {
        throw std::invalid_argument("threads must be in [1,8]");
      }
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  return result;
}

[[noreturn]] void malformed(std::string_view detail) {
  throw std::runtime_error("malformed frozen teacher export: " +
                           std::string(detail));
}

std::size_t requireFind(std::string_view text, std::string_view marker,
                        std::size_t begin = 0) {
  const std::size_t position = text.find(marker, begin);
  if (position == std::string_view::npos) malformed(marker);
  return position + marker.size();
}

long long parseIntegerAt(std::string_view text, std::size_t position,
                         std::size_t* end = nullptr) {
  long long result = 0;
  const char* first = text.data() + position;
  const char* last = text.data() + text.size();
  const auto parsed = std::from_chars(first, last, result);
  if (parsed.ec != std::errc{} || parsed.ptr == first) malformed("integer");
  if (end != nullptr) *end = static_cast<std::size_t>(parsed.ptr - text.data());
  return result;
}

double parseDoubleAt(std::string_view text, std::size_t position,
                     std::size_t* end = nullptr) {
  const std::string tail(text.substr(position));
  char* parsed_end = nullptr;
  const double result = std::strtod(tail.c_str(), &parsed_end);
  if (parsed_end == tail.c_str() || !std::isfinite(result)) {
    malformed("finite double");
  }
  if (end != nullptr) {
    *end = position + static_cast<std::size_t>(parsed_end - tail.c_str());
  }
  return result;
}

bool parseBoolAt(std::string_view text, std::size_t position) {
  if (text.substr(position, 4) == "true") return true;
  if (text.substr(position, 5) == "false") return false;
  malformed("boolean");
}

std::size_t matchingDelimiter(std::string_view text, std::size_t open,
                              char left, char right) {
  if (open >= text.size() || text[open] != left) malformed("delimiter");
  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (std::size_t index = open; index < text.size(); ++index) {
    const char value = text[index];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (value == '\\') {
        escaped = true;
      } else if (value == '"') {
        in_string = false;
      }
      continue;
    }
    if (value == '"') {
      in_string = true;
    } else if (value == left) {
      ++depth;
    } else if (value == right && --depth == 0) {
      return index;
    }
  }
  malformed("unclosed delimiter");
}

template <std::size_t Size>
std::array<int, Size> parseIntegerArray(std::string_view line,
                                        std::string_view marker) {
  std::array<int, Size> result{};
  std::size_t position = requireFind(line, marker);
  for (std::size_t index = 0; index < Size; ++index) {
    std::size_t end = 0;
    const long long value = parseIntegerAt(line, position, &end);
    if (value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) {
      malformed("integer range");
    }
    result[index] = static_cast<int>(value);
    position = end;
    if (index + 1 < Size) {
      if (position >= line.size() || line[position] != ',') malformed("array comma");
      ++position;
    }
  }
  if (position >= line.size() || line[position] != ']') malformed("array length");
  return result;
}

std::array<double, kBoardSize> parseNullableDoubleArray(
    std::string_view line, std::string_view marker,
    std::array<bool, kBoardSize>& present) {
  std::array<double, kBoardSize> result{};
  std::size_t position = requireFind(line, marker);
  for (int index = 0; index < kBoardSize; ++index) {
    if (line.substr(position, 4) == "null") {
      present[index] = false;
      position += 4;
    } else {
      present[index] = true;
      result[index] = parseDoubleAt(line, position, &position);
    }
    if (index + 1 < kBoardSize) {
      if (position >= line.size() || line[position] != ',') malformed("array comma");
      ++position;
    }
  }
  if (position >= line.size() || line[position] != ']') malformed("array length");
  return result;
}

struct TeacherRecord {
  int index = -1;
  ObservableState state{};
  std::uint32_t tape_seed = 0;
  int d4_action = -1;
  std::array<double, kBoardSize> root_q{};
  std::array<bool, kBoardSize> legal{};
  int teacher_action = -1;
  bool teacher_switched = false;
  std::array<double, kBoardSize> teacher_mean_return{};
};

void parseTeacherActions(std::string_view line, TeacherRecord& record) {
  const std::size_t rollout = requireFind(line, "\"rollout\":{");
  std::size_t position = requireFind(line, "\"actions\":[", rollout);
  for (int slot = 0; slot < kBoardSize; ++slot) {
    if (line.substr(position, 4) == "null") {
      if (record.legal[slot]) malformed("legal action missing rollout");
      position += 4;
    } else {
      if (position >= line.size() || line[position] != '{') {
        malformed("action object");
      }
      const std::size_t close = matchingDelimiter(line, position, '{', '}');
      const std::string_view object = line.substr(position, close - position + 1);
      const int action = static_cast<int>(
          parseIntegerAt(object, requireFind(object, "\"action\":")));
      if (action != slot || !record.legal[slot]) malformed("action alignment");
      record.teacher_mean_return[slot] =
          parseDoubleAt(object, requireFind(object, "\"meanReturn\":"));
      position = close + 1;
    }
    if (slot + 1 < kBoardSize) {
      if (position >= line.size() || line[position] != ',') malformed("actions comma");
      ++position;
    }
  }
  if (position >= line.size() || line[position] != ']') malformed("actions length");
}

TeacherRecord parseTeacherRecord(std::string_view line, int expected_index) {
  TeacherRecord result;
  result.index = static_cast<int>(parseIntegerAt(
      line, requireFind(line, "\"type\":\"routed-decision\",\"index\":")));
  if (result.index != expected_index) malformed("record index sequence");
  const auto board = parseIntegerArray<kCellCount>(line, "\"board\":[");
  for (int index = 0; index < kCellCount; ++index) {
    if (board[index] < kEmpty || board[index] > kCracked) malformed("cell value");
    result.state.board[index] = static_cast<std::uint8_t>(board[index]);
  }
  const long long next_disc = parseIntegerAt(
      line, requireFind(line, "\"nextDisc\":"));
  if (next_disc < 1 || next_disc > kBoardSize) malformed("next disc");
  result.state.next_disc = static_cast<std::uint8_t>(next_disc);
  result.state.moves_remaining = static_cast<int>(parseIntegerAt(
      line, requireFind(line, "\"movesRemaining\":")));
  result.state.game_over =
      parseBoolAt(line, requireFind(line, "\"gameOver\":"));
  if (result.state.moves_remaining < 1 ||
      result.state.moves_remaining > kMovesPerLevel || result.state.game_over) {
    malformed("routed public state");
  }
  const long long tape_seed = parseIntegerAt(
      line, requireFind(line, "\"tapeSeed\":"));
  if (tape_seed < 0 ||
      static_cast<unsigned long long>(tape_seed) >
          std::numeric_limits<std::uint32_t>::max()) {
    malformed("tape seed");
  }
  result.tape_seed = static_cast<std::uint32_t>(tape_seed);
  result.d4_action = static_cast<int>(parseIntegerAt(
      line, requireFind(line, "\"stockD4\":{\"action\":")));
  result.root_q = parseNullableDoubleArray(line, "\"rootQ\":[", result.legal);
  const std::size_t veto = requireFind(line, "\"veto\":{");
  result.teacher_action = static_cast<int>(parseIntegerAt(
      line, requireFind(line, "\"action\":", veto)));
  result.teacher_switched =
      parseBoolAt(line, requireFind(line, "\"switched\":", veto));
  parseTeacherActions(line, result);

  int legal_count = 0;
  for (int action = 0; action < kBoardSize; ++action) {
    const bool board_legal = isLegal(result.state.board, action);
    if (board_legal != result.legal[action]) malformed("root legality mismatch");
    legal_count += board_legal;
  }
  if (legal_count == 0 || result.d4_action < 0 ||
      result.d4_action >= kBoardSize || !result.legal[result.d4_action] ||
      result.teacher_action < 0 || result.teacher_action >= kBoardSize ||
      !result.legal[result.teacher_action] ||
      result.teacher_switched != (result.teacher_action != result.d4_action)) {
    malformed("teacher action");
  }
  const std::uint32_t expected_tape = teacher::seed32(
      teacher::publicHash(result.state) ^
      static_cast<std::uint64_t>(teacher::kTapeSeedDomain));
  if (result.tape_seed != expected_tape) malformed("public tape hash");
  return result;
}

std::vector<TeacherRecord> loadTeacher(const std::string& path) {
  std::error_code error;
  const std::uintmax_t bytes = std::filesystem::file_size(path, error);
  if (error || bytes != kTeacherBytes) {
    throw std::runtime_error("frozen teacher byte count mismatch");
  }
  std::ifstream input(path);
  if (!input) throw std::runtime_error("could not open frozen teacher export");
  std::string line;
  if (!std::getline(input, line) ||
      line.find("\"type\":\"metadata\"") == std::string::npos ||
      parseIntegerAt(line, requireFind(line, "\"gameSeed\":")) !=
          kTeacherGameSeed ||
      parseIntegerAt(line, requireFind(line, "\"records\":")) !=
          kTeacherRecords ||
      parseIntegerAt(line, requireFind(line, "\"horizon\":")) != 25 ||
      parseIntegerAt(line, requireFind(line, "\"scenarios\":")) != 7) {
    malformed("metadata");
  }
  std::vector<TeacherRecord> records;
  records.reserve(kTeacherRecords);
  for (int index = 0; index < kTeacherRecords; ++index) {
    if (!std::getline(input, line)) malformed("missing record");
    records.push_back(parseTeacherRecord(line, index));
  }
  if (!std::getline(input, line) ||
      line.find("\"type\":\"summary\"") == std::string::npos ||
      parseIntegerAt(line, requireFind(line, "\"score\":")) != 404'047 ||
      parseIntegerAt(line, requireFind(line, "\"moves\":")) != 250 ||
      parseIntegerAt(line, requireFind(line, "\"routedDecisions\":")) !=
          kTeacherRecords ||
      parseIntegerAt(line, requireFind(line, "\"switches\":")) !=
          kTeacherSwitches || std::getline(input, line)) {
    malformed("summary/trailing data");
  }
  int switches = 0;
  for (const TeacherRecord& record : records) switches += record.teacher_switched;
  if (switches != kTeacherSwitches) malformed("teacher switch count");
  return records;
}

struct ContinuationMetrics {
  std::uint64_t calls = 0;
  std::uint64_t d1_calls = 0;
  std::uint64_t d2_calls = 0;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::uint64_t root_actions = 0;
  std::size_t peak_cache_entries = 0;
  bool full_root = true;

  void add(const ContinuationMetrics& other) {
    calls += other.calls;
    d1_calls += other.d1_calls;
    d2_calls += other.d2_calls;
    work += other.work;
    nodes += other.nodes;
    cache_hits += other.cache_hits;
    root_actions += other.root_actions;
    peak_cache_entries =
        std::max(peak_cache_entries, other.peak_cache_entries);
    full_root = full_root && other.full_root;
  }
};

int continuationDepth(const ObservableState& state,
                      ContinuationPolicy policy) {
  switch (policy) {
    case ContinuationPolicy::kD1:
      return 1;
    case ContinuationPolicy::kD2:
      return 2;
    case ContinuationPolicy::kD2AtOne:
      return state.moves_remaining == 1 ? 2 : 1;
    case ContinuationPolicy::kD2AtOneOrFive:
      return state.moves_remaining == 1 || state.moves_remaining == 5 ? 2 : 1;
  }
  throw std::logic_error("unknown continuation policy");
}

int fairAction(const ObservableState& source, ContinuationPolicy policy,
               ContinuationMetrics* aggregate = nullptr) {
  if (source.game_over) return -1;
  const int depth = continuationDepth(source, policy);
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(
      teacher::materialize(source), mirrored);
  fair::SearchContext context;
  const fair::RootEvaluation root = fair::rootDecision(canonical, depth, context);
  int legal_count = 0;
  int evaluated_count = 0;
  for (int action = 0; action < kBoardSize; ++action) {
    legal_count += isLegal(canonical.board, action);
    evaluated_count += std::isfinite(root.values[action]);
  }
  const std::uint64_t work_bound = depth == 1 ? kWorstD1Work : kWorstD2Work;
  const std::size_t cache_bound =
      depth == 1 ? kWorstD1CacheEntries : kWorstD2CacheEntries;
  if (root.action < 0 || evaluated_count != legal_count ||
      context.work > work_bound || context.cache.size() > cache_bound) {
    throw std::runtime_error("continuation failed full-width resource proof");
  }
  if (aggregate != nullptr) {
    ++aggregate->calls;
    aggregate->d1_calls += depth == 1;
    aggregate->d2_calls += depth == 2;
    aggregate->work += context.work;
    aggregate->nodes += context.nodes;
    aggregate->cache_hits += context.cache_hits;
    aggregate->root_actions += static_cast<std::uint64_t>(evaluated_count);
    aggregate->peak_cache_entries =
        std::max(aggregate->peak_cache_entries, context.cache.size());
    aggregate->full_root =
        aggregate->full_root && evaluated_count == legal_count;
  }
  return mirrored ? kBoardSize - 1 - root.action : root.action;
}

using PublicContinuation = int (*)(const ObservableState&, ContinuationPolicy,
                                    ContinuationMetrics*);
static_assert(std::is_same_v<decltype(&fairAction), PublicContinuation>);

struct RevealTape {
  std::uint32_t root_seed = 0;
  int scenario = 0;
  int scenario_count = 0;
  int step = 0;
  std::uint32_t domain = teacher::kRevealTapeDomain;
  int event = 0;

  std::uint8_t nextDisc() {
    const int event_index = step * kEventsPerStep + event++;
    const double unit = cfpi::detail::stratifiedUnit(
        root_seed, scenario, scenario_count, domain, event_index);
    return static_cast<std::uint8_t>(
        std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
  }
};

std::uint8_t visibleDisc(std::uint32_t root_seed, int scenario,
                         int scenario_count, int step,
                         std::uint32_t domain = teacher::kVisibleTapeDomain) {
  const double unit = cfpi::detail::stratifiedUnit(
      root_seed, scenario, scenario_count, domain, step);
  return static_cast<std::uint8_t>(
      std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
}

bool playSyntheticMove(const ObservableState& source, int action,
                       std::uint32_t root_seed, int scenario,
                       int scenario_count, int step, MoveResult& result,
                       std::uint64_t* transitions = nullptr,
                       teacher::TapeDomains domains = {}) {
  if (scenario_count < 1 || scenario_count > kMaximumScenarios ||
      scenario < 0 || scenario >= scenario_count || step < 0 ||
      source.game_over || !isLegal(source.board, action)) {
    return false;
  }
  Board board = source.board;
  if (!placeDisc(board, action, source.next_disc)) return false;

  RevealTape reveals{root_seed, scenario, scenario_count, step,
                     domains.reveal, 0};
  result = MoveResult{};
  std::int64_t first_score = 0;
  cfpi::detail::resolveCascadeSampled(board, reveals, 1, first_score,
                                      result.waves);
  result.score_delta = first_score;
  result.cleared_board = isBoardEmpty(board);
  if (result.cleared_board) result.score_delta += kClearBonus;

  int moves_remaining = source.moves_remaining - 1;
  bool game_over = false;
  if (moves_remaining == 0) {
    Board raised{};
    if (!raiseCoveredRow(board, raised)) {
      game_over = true;
    } else {
      result.level_advanced = true;
      moves_remaining = kMovesPerLevel;
      result.score_delta += kLevelBonus;
      board = raised;
      std::int64_t level_score = 0;
      const int next_depth =
          result.waves.empty() ? 1 : result.waves.back().depth + 1;
      cfpi::detail::resolveCascadeSampled(board, reveals, next_depth,
                                          level_score, result.waves);
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
  result.state.next_disc =
      game_over ? source.next_disc
                : visibleDisc(root_seed, scenario, scenario_count, step,
                              domains.visible);
  result.state.score = 0;
  result.state.level = 1;
  result.state.moves_remaining = moves_remaining;
  result.state.moves_played = 0;
  result.state.game_over = game_over;
  if (transitions != nullptr) ++*transitions;
  return true;
}

struct ScenarioOutcome {
  double value = 0.0;
  int numbered_clears = 0;
  bool survived_horizon = false;

  bool operator==(const ScenarioOutcome&) const = default;
};

struct ActionRollout {
  std::array<ScenarioOutcome, kMaximumScenarios> scenarios{};
  double mean_value = 0.0;
  double mean_numbered_clears = 0.0;
  int surviving_scenarios = 0;

  bool operator==(const ActionRollout&) const = default;
};

struct RolloutEvaluation {
  std::array<ActionRollout, kBoardSize> actions{};
  std::array<bool, kBoardSize> legal{};
  int legal_actions = 0;
  std::uint64_t synthetic_transitions = 0;
  ContinuationMetrics continuation{};
  bool full_root = true;
};

std::uint64_t maximumContinuationCalls(const Specification& specification) {
  return static_cast<std::uint64_t>(kBoardSize) * specification.scenarios *
         (specification.horizon - 1);
}

std::uint64_t maximumSyntheticTransitions(
    const Specification& specification) {
  return static_cast<std::uint64_t>(kBoardSize) * specification.scenarios *
         specification.horizon;
}

RolloutEvaluation evaluateRollouts(const ObservableState& source,
                                   const Specification& specification) {
  if (source.game_over || specification.scenarios < 2 ||
      specification.scenarios > kMaximumScenarios ||
      pairedTCutoff(specification.scenarios) <= 0.0 ||
      specification.horizon < 1 ||
      specification.horizon > kMaximumHorizon) {
    throw std::invalid_argument("invalid compression rollout root/specification");
  }
  RolloutEvaluation result;
  bool mirrored = false;
  const State canonical_state = cfpi::detail::canonicalState(
      teacher::materialize(source), mirrored);
  const ObservableState root = teacher::observable(canonical_state);
  const std::uint32_t tape_seed = teacher::seed32(
      teacher::publicHash(root) ^
      static_cast<std::uint64_t>(teacher::kTapeSeedDomain));

  for (const int action : cfpi::detail::kColumnOrder) {
    if (!isLegal(root.board, action)) continue;
    result.legal[action] = true;
    ++result.legal_actions;
    ActionRollout& action_result = result.actions[action];
    for (int scenario = 0; scenario < specification.scenarios; ++scenario) {
      ObservableState state = root;
      ScenarioOutcome& outcome = action_result.scenarios[scenario];
      for (int step = 0; step < specification.horizon; ++step) {
        const int selected =
            step == 0 ? action
                      : fairAction(state, specification.continuation,
                                   &result.continuation);
        if (!isLegal(state.board, selected)) {
          outcome.value += fair::kTerminalUtility;
          state.game_over = true;
          break;
        }
        MoveResult move;
        if (!playSyntheticMove(state, selected, tape_seed, scenario,
                               specification.scenarios, step, move,
                               &result.synthetic_transitions)) {
          outcome.value += fair::kTerminalUtility;
          state.game_over = true;
          break;
        }
        outcome.value += static_cast<double>(move.score_delta);
        for (const Wave& wave : move.waves) {
          outcome.numbered_clears += wave.cleared;
        }
        state = teacher::observable(move.state);
        if (state.game_over) {
          outcome.value += fair::kTerminalUtility;
          break;
        }
      }
      if (!state.game_over) {
        outcome.survived_horizon = true;
        outcome.value += fair::fairLeaf(teacher::materialize(state));
      }
      action_result.mean_value +=
          outcome.value / static_cast<double>(specification.scenarios);
      action_result.mean_numbered_clears +=
          static_cast<double>(outcome.numbered_clears) /
          static_cast<double>(specification.scenarios);
      action_result.surviving_scenarios += outcome.survived_horizon;
    }
  }
  result.full_root = result.legal_actions > 0 && result.continuation.full_root;
  const std::uint64_t worst_work =
      result.continuation.d1_calls * kWorstD1Work +
      result.continuation.d2_calls * kWorstD2Work;
  if (result.synthetic_transitions >
          maximumSyntheticTransitions(specification) ||
      result.continuation.calls > maximumContinuationCalls(specification) ||
      result.continuation.work > worst_work ||
      result.continuation.peak_cache_entries > kWorstD2CacheEntries ||
      result.continuation.calls !=
          result.continuation.d1_calls + result.continuation.d2_calls ||
      !result.full_root) {
    throw std::runtime_error("compression rollout exceeded resource/full-root bound");
  }
  if (!mirrored) return result;
  RolloutEvaluation reflected = result;
  for (int action = 0; action < kBoardSize; ++action) {
    reflected.actions[kBoardSize - 1 - action] = result.actions[action];
    reflected.legal[kBoardSize - 1 - action] = result.legal[action];
  }
  return reflected;
}

double pairedReturnLower95(const ActionRollout& candidate,
                           const ActionRollout& baseline, int scenarios) {
  if (scenarios < 2 || scenarios > kMaximumScenarios ||
      pairedTCutoff(scenarios) <= 0.0) {
    throw std::invalid_argument("unsupported paired scenario count");
  }
  double mean = 0.0;
  std::array<double, kMaximumScenarios> differences{};
  for (int scenario = 0; scenario < scenarios; ++scenario) {
    differences[scenario] = candidate.scenarios[scenario].value -
                            baseline.scenarios[scenario].value;
    mean += differences[scenario] / static_cast<double>(scenarios);
  }
  double squares = 0.0;
  for (int scenario = 0; scenario < scenarios; ++scenario) {
    const double centered = differences[scenario] - mean;
    squares += centered * centered;
  }
  const double deviation =
      std::sqrt(squares / static_cast<double>(scenarios - 1));
  return mean - pairedTCutoff(scenarios) * deviation /
                    std::sqrt(static_cast<double>(scenarios));
}

struct AlternativeTest {
  double return_lower95 = -std::numeric_limits<double>::infinity();
  double clear_advantage = -std::numeric_limits<double>::infinity();
  double root_q_loss = std::numeric_limits<double>::infinity();
  int survivor_advantage = std::numeric_limits<int>::min();
  bool survivors_ok = false;
  bool clears_ok = false;
  bool return_ok = false;
  bool root_q_ok = false;
  bool passed = false;
};

AlternativeTest testAlternative(const ActionRollout& candidate,
                                const ActionRollout& baseline,
                                double candidate_root_q,
                                double baseline_root_q, int scenarios) {
  AlternativeTest result;
  result.return_lower95 =
      pairedReturnLower95(candidate, baseline, scenarios);
  result.clear_advantage =
      candidate.mean_numbered_clears - baseline.mean_numbered_clears;
  result.root_q_loss = baseline_root_q - candidate_root_q;
  result.survivor_advantage =
      candidate.surviving_scenarios - baseline.surviving_scenarios;
  result.survivors_ok = result.survivor_advantage >= 0;
  result.clears_ok = result.clear_advantage >= 0.0;
  result.return_ok = result.return_lower95 > 0.0;
  result.root_q_ok = result.root_q_loss <= kMaximumRootQLoss + 1.0e-9;
  result.passed = result.survivors_ok && result.clears_ok &&
                  result.return_ok && result.root_q_ok;
  return result;
}

struct ApproximationDecision {
  int action = -1;
  bool switched = false;
  int alternatives_considered = 0;
  int passing_alternatives = 0;
  int survivor_rejections = 0;
  int clear_rejections = 0;
  int return_rejections = 0;
  int root_q_rejections = 0;
  double selected_return_lower95 = 0.0;
  RolloutEvaluation rollout{};
};

ApproximationDecision chooseApproximation(const TeacherRecord& record,
                                          const Specification& specification) {
  ApproximationDecision result;
  result.action = record.d4_action;
  result.rollout = evaluateRollouts(record.state, specification);
  for (int action = 0; action < kBoardSize; ++action) {
    if (result.rollout.legal[action] != record.legal[action]) {
      throw std::runtime_error("approximation root differs from teacher root");
    }
  }
  if (!result.rollout.legal[record.d4_action]) {
    throw std::runtime_error("D4 baseline missing from approximation root");
  }
  const ActionRollout& baseline = result.rollout.actions[record.d4_action];
  double best_lower = 0.0;
  for (const int action : cfpi::detail::kColumnOrder) {
    if (!result.rollout.legal[action] || action == record.d4_action) continue;
    ++result.alternatives_considered;
    const AlternativeTest test = testAlternative(
        result.rollout.actions[action], baseline, record.root_q[action],
        record.root_q[record.d4_action], specification.scenarios);
    result.survivor_rejections += !test.survivors_ok;
    result.clear_rejections += !test.clears_ok;
    result.return_rejections += !test.return_ok;
    result.root_q_rejections += !test.root_q_ok;
    if (!test.passed) continue;
    ++result.passing_alternatives;
    if (test.return_lower95 > best_lower) {
      best_lower = test.return_lower95;
      result.action = action;
      result.selected_return_lower95 = test.return_lower95;
    }
  }
  result.switched = result.action != record.d4_action;
  if (!isLegal(record.state.board, result.action)) {
    throw std::runtime_error("approximation selected illegal action");
  }
  return result;
}

struct RecordResult {
  int action = -1;
  bool switched = false;
  int alternatives_considered = 0;
  int passing_alternatives = 0;
  int survivor_rejections = 0;
  int clear_rejections = 0;
  int return_rejections = 0;
  int root_q_rejections = 0;
  double selected_return_lower95 = 0.0;
  std::array<double, kBoardSize> mean_return{};
  std::uint64_t synthetic_transitions = 0;
  ContinuationMetrics continuation{};
};

RecordResult compact(const ApproximationDecision& decision) {
  RecordResult result;
  result.action = decision.action;
  result.switched = decision.switched;
  result.alternatives_considered = decision.alternatives_considered;
  result.passing_alternatives = decision.passing_alternatives;
  result.survivor_rejections = decision.survivor_rejections;
  result.clear_rejections = decision.clear_rejections;
  result.return_rejections = decision.return_rejections;
  result.root_q_rejections = decision.root_q_rejections;
  result.selected_return_lower95 = decision.selected_return_lower95;
  result.synthetic_transitions = decision.rollout.synthetic_transitions;
  result.continuation = decision.rollout.continuation;
  for (int action = 0; action < kBoardSize; ++action) {
    result.mean_return[action] = decision.rollout.actions[action].mean_value;
  }
  return result;
}

struct SpecificationResult {
  Specification specification{};
  int records = 0;
  int exact_actions = 0;
  int teacher_switch_exact_actions = 0;
  int teacher_switch_any_switches = 0;
  int fallback_correct = 0;
  int false_switches = 0;
  int approximation_switches = 0;
  int alternatives_considered = 0;
  int passing_alternatives = 0;
  int survivor_rejections = 0;
  int clear_rejections = 0;
  int return_rejections = 0;
  int root_q_rejections = 0;
  std::uint64_t pairwise_agreements = 0;
  std::uint64_t pairwise_comparisons = 0;
  std::uint64_t synthetic_transitions = 0;
  ContinuationMetrics continuation{};
  double wall_seconds = 0.0;
  std::uint64_t peak_rss_bytes = 0;
  bool eligible = false;
  double overall_agreement = 0.0;
  double exact_switch_action_recall = 0.0;
  double any_switch_recall = 0.0;
  double fallback_specificity = 0.0;
  double balanced_agreement = 0.0;
  double pairwise_return_rank_agreement = 0.0;
};

SpecificationResult summarizeSpecification(
    const Specification& specification,
    const std::vector<TeacherRecord>& teacher_records,
    const std::vector<RecordResult>& results, double wall_seconds) {
  if (teacher_records.size() != results.size() || teacher_records.empty()) {
    throw std::invalid_argument("invalid compression result vectors");
  }
  SpecificationResult summary;
  summary.specification = specification;
  summary.records = static_cast<int>(results.size());
  summary.wall_seconds = wall_seconds;
  summary.peak_rss_bytes = d4::peakRssBytes();
  for (std::size_t index = 0; index < results.size(); ++index) {
    const TeacherRecord& label = teacher_records[index];
    const RecordResult& result = results[index];
    summary.exact_actions += result.action == label.teacher_action;
    summary.approximation_switches += result.switched;
    if (label.teacher_switched) {
      summary.teacher_switch_exact_actions +=
          result.action == label.teacher_action;
      summary.teacher_switch_any_switches += result.switched;
    } else {
      summary.fallback_correct += !result.switched;
      summary.false_switches += result.switched;
    }
    summary.alternatives_considered += result.alternatives_considered;
    summary.passing_alternatives += result.passing_alternatives;
    summary.survivor_rejections += result.survivor_rejections;
    summary.clear_rejections += result.clear_rejections;
    summary.return_rejections += result.return_rejections;
    summary.root_q_rejections += result.root_q_rejections;
    summary.synthetic_transitions += result.synthetic_transitions;
    summary.continuation.add(result.continuation);
    for (int first = 0; first < kBoardSize; ++first) {
      if (!label.legal[first]) continue;
      for (int second = first + 1; second < kBoardSize; ++second) {
        if (!label.legal[second]) continue;
        const double teacher_difference =
            label.teacher_mean_return[first] -
            label.teacher_mean_return[second];
        if (std::abs(teacher_difference) <= 1.0e-9) continue;
        const double approximation_difference =
            result.mean_return[first] - result.mean_return[second];
        ++summary.pairwise_comparisons;
        summary.pairwise_agreements +=
            (teacher_difference > 0.0) == (approximation_difference > 0.0) &&
            std::abs(approximation_difference) > 1.0e-9;
      }
    }
  }
  summary.overall_agreement =
      static_cast<double>(summary.exact_actions) / summary.records;
  summary.exact_switch_action_recall =
      static_cast<double>(summary.teacher_switch_exact_actions) /
      kTeacherSwitches;
  summary.any_switch_recall =
      static_cast<double>(summary.teacher_switch_any_switches) /
      kTeacherSwitches;
  summary.fallback_specificity =
      static_cast<double>(summary.fallback_correct) / kTeacherFallbacks;
  summary.balanced_agreement =
      0.5 * (summary.exact_switch_action_recall +
             summary.fallback_specificity);
  summary.pairwise_return_rank_agreement =
      static_cast<double>(summary.pairwise_agreements) /
      static_cast<double>(summary.pairwise_comparisons);
  summary.eligible = summary.teacher_switch_exact_actions >= 8 &&
                     summary.fallback_specificity >= 0.95;
  if (summary.records != kTeacherRecords ||
      summary.fallback_correct + summary.false_switches != kTeacherFallbacks ||
      !summary.continuation.full_root ||
      summary.peak_rss_bytes > kMaximumRssBytes) {
    throw std::runtime_error("compression summary invariant/resource failure");
  }
  return summary;
}

SpecificationResult evaluateSpecification(
    const Specification& specification,
    const std::vector<TeacherRecord>& records, int threads,
    std::ostream& progress) {
  const auto started = std::chrono::steady_clock::now();
  std::vector<RecordResult> results(records.size());
  std::atomic<std::size_t> next{0};
  std::atomic<int> completed{0};
  std::mutex exception_mutex;
  std::exception_ptr exception;
  const int worker_count =
      std::min(threads, static_cast<int>(records.size()));
  std::vector<std::thread> workers;
  workers.reserve(static_cast<std::size_t>(worker_count));
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.emplace_back([&] {
      try {
        while (true) {
          const std::size_t index = next.fetch_add(1);
          if (index >= records.size()) break;
          results[index] = compact(
              chooseApproximation(records[index], specification));
          const int count = completed.fetch_add(1) + 1;
          if (count % 20 == 0 || count == kTeacherRecords) {
            std::lock_guard<std::mutex> lock(teacher::progress_mutex);
            progress << "teacher-compression " << specification.id << ' '
                     << count << '/' << kTeacherRecords << '\n';
          }
        }
      } catch (...) {
        std::lock_guard<std::mutex> lock(exception_mutex);
        if (exception == nullptr) exception = std::current_exception();
        next.store(records.size());
      }
    });
  }
  for (std::thread& worker : workers) worker.join();
  if (exception != nullptr) std::rethrow_exception(exception);
  const double wall = std::chrono::duration<double>(
                          std::chrono::steady_clock::now() - started)
                          .count();
  return summarizeSpecification(specification, records, results, wall);
}

int selectSpecification(
    const std::array<SpecificationResult, kSpecifications.size()>& results) {
  int selected = -1;
  for (std::size_t index = 0; index < results.size(); ++index) {
    if (!results[index].eligible) continue;
    if (selected < 0 ||
        results[index].balanced_agreement >
            results[static_cast<std::size_t>(selected)].balanced_agreement ||
        (results[index].balanced_agreement ==
             results[static_cast<std::size_t>(selected)].balanced_agreement &&
         results[index].continuation.work <
             results[static_cast<std::size_t>(selected)].continuation.work)) {
      selected = static_cast<int>(index);
    }
  }
  return selected;
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

bool sameEvaluationUnderReflection(const RolloutEvaluation& first,
                                   const RolloutEvaluation& reflected,
                                   int scenarios) {
  for (int action = 0; action < kBoardSize; ++action) {
    const int mirror_action = kBoardSize - 1 - action;
    if (first.legal[action] != reflected.legal[mirror_action]) return false;
    if (!first.legal[action]) continue;
    const ActionRollout& left = first.actions[action];
    const ActionRollout& right = reflected.actions[mirror_action];
    if (left.mean_value != right.mean_value ||
        left.mean_numbered_clears != right.mean_numbered_clears ||
        left.surviving_scenarios != right.surviving_scenarios) {
      return false;
    }
    for (int scenario = 0; scenario < scenarios; ++scenario) {
      if (left.scenarios[scenario] != right.scenarios[scenario]) return false;
    }
  }
  return true;
}

bool selfTest(const Options& options, std::ostream& output) {
  const std::vector<TeacherRecord> records = loadTeacher(options.teacher);
  expect(records.size() == kTeacherRecords, "teacher parser record count");
  int switches = 0;
  for (const TeacherRecord& record : records) switches += record.teacher_switched;
  expect(switches == kTeacherSwitches, "teacher parser switch count");

  const ObservableState fixture = teacher::asymmetricFixture();
  ContinuationMetrics d1_first_metrics;
  ContinuationMetrics d1_repeat_metrics;
  ContinuationMetrics d1_mirror_metrics;
  const int d1_first = fairAction(fixture, ContinuationPolicy::kD1,
                                  &d1_first_metrics);
  const int d1_repeat = fairAction(fixture, ContinuationPolicy::kD1,
                                   &d1_repeat_metrics);
  const int d1_mirror = fairAction(teacher::mirror(fixture),
                                   ContinuationPolicy::kD1,
                                   &d1_mirror_metrics);
  ContinuationMetrics d2_first_metrics;
  ContinuationMetrics d2_mirror_metrics;
  const int d2_first = fairAction(fixture, ContinuationPolicy::kD2,
                                  &d2_first_metrics);
  const int d2_mirror = fairAction(teacher::mirror(fixture),
                                   ContinuationPolicy::kD2,
                                   &d2_mirror_metrics);
  expect(d1_first == d1_repeat &&
             d1_mirror == kBoardSize - 1 - d1_first &&
             d2_mirror == kBoardSize - 1 - d2_first,
         "continuation determinism/reflection");
  expect(d1_first_metrics.work == d1_repeat_metrics.work &&
             d1_first_metrics.work <= kWorstD1Work &&
             d1_first_metrics.peak_cache_entries == 0 &&
             d2_first_metrics.work <= kWorstD2Work &&
             d2_first_metrics.peak_cache_entries <=
                 kWorstD2CacheEntries &&
             d1_first_metrics.full_root && d2_first_metrics.full_root,
         "continuation resource/full-root bounds");

  ObservableState phase = fixture;
  phase.moves_remaining = 1;
  expect(continuationDepth(phase, ContinuationPolicy::kD2AtOne) == 2 &&
             continuationDepth(phase,
                               ContinuationPolicy::kD2AtOneOrFive) == 2,
         "hybrid phase one wiring");
  phase.moves_remaining = 5;
  expect(continuationDepth(phase, ContinuationPolicy::kD2AtOne) == 1 &&
             continuationDepth(phase,
                               ContinuationPolicy::kD2AtOneOrFive) == 2,
         "hybrid phase five wiring");
  phase.moves_remaining = 3;
  expect(continuationDepth(phase, ContinuationPolicy::kD2AtOne) == 1 &&
             continuationDepth(phase,
                               ContinuationPolicy::kD2AtOneOrFive) == 1,
         "hybrid D1 wiring");

  State hidden = teacher::materialize(fixture);
  hidden.score = 99'999'999;
  hidden.level = 991;
  hidden.moves_played = 777;
  const ObservableState stripped = teacher::observable(hidden);
  ContinuationMetrics metadata_metrics;
  const int metadata_action = fairAction(
      stripped, ContinuationPolicy::kD1, &metadata_metrics);
  expect(teacher::sameObservable(stripped, fixture) &&
             metadata_action == d1_first &&
             metadata_metrics.work == d1_first_metrics.work,
         "hidden metadata crossed public boundary");

  const Specification parity{'P', "parity-D2/s7/h3", 7, 3,
                             ContinuationPolicy::kD2};
  const RolloutEvaluation dynamic = evaluateRollouts(fixture, parity);
  const teacher::RolloutEvaluation frozen =
      teacher::evaluateRollouts(fixture, 3);
  bool teacher_parity =
      dynamic.legal == frozen.legal &&
      dynamic.synthetic_transitions == frozen.synthetic_transitions &&
      dynamic.continuation.calls == frozen.continuation.calls &&
      dynamic.continuation.work == frozen.continuation.work;
  for (int action = 0; action < kBoardSize; ++action) {
    if (!dynamic.legal[action]) continue;
    teacher_parity =
        teacher_parity &&
        dynamic.actions[action].mean_value ==
            frozen.actions[action].mean_value &&
        dynamic.actions[action].mean_numbered_clears ==
            frozen.actions[action].mean_numbered_clears &&
        dynamic.actions[action].surviving_scenarios ==
            frozen.actions[action].surviving_scenarios;
    for (int scenario = 0; scenario < parity.scenarios; ++scenario) {
      teacher_parity =
          teacher_parity &&
          dynamic.actions[action].scenarios[scenario].value ==
              frozen.actions[action].scenarios[scenario].value &&
          dynamic.actions[action].scenarios[scenario].numbered_clears ==
              frozen.actions[action].scenarios[scenario].numbered_clears &&
          dynamic.actions[action].scenarios[scenario].survived_horizon ==
              frozen.actions[action].scenarios[scenario].survived_horizon;
    }
  }
  expect(teacher_parity, "dynamic s7/h3 did not reproduce teacher kernel");

  const Specification d1_reflection{'R', "reflection-D1/s3/h3", 3, 3,
                                    ContinuationPolicy::kD1};
  const RolloutEvaluation d1_rollout =
      evaluateRollouts(fixture, d1_reflection);
  const RolloutEvaluation d1_reflected =
      evaluateRollouts(teacher::mirror(fixture), d1_reflection);
  expect(sameEvaluationUnderReflection(d1_rollout, d1_reflected, 3),
         "D1 rollout reflection");
  const Specification d2_reflection{'S', "reflection-D2/s3/h2", 3, 2,
                                    ContinuationPolicy::kD2};
  const RolloutEvaluation d2_rollout =
      evaluateRollouts(fixture, d2_reflection);
  const RolloutEvaluation d2_reflected =
      evaluateRollouts(teacher::mirror(fixture), d2_reflection);
  expect(sameEvaluationUnderReflection(d2_rollout, d2_reflected, 3),
         "D2 rollout reflection");

  bool stratification = true;
  constexpr std::uint32_t test_seed = 0x1234'5678u;
  for (const int count : std::array<int, 3>{3, 5, 7}) {
    std::vector<bool> reveal_strata(static_cast<std::size_t>(count));
    std::vector<bool> visible_strata(static_cast<std::size_t>(count));
    for (int scenario = 0; scenario < count; ++scenario) {
      const double reveal_unit = cfpi::detail::stratifiedUnit(
          test_seed, scenario, count, teacher::kRevealTapeDomain, 0);
      const double visible_unit = cfpi::detail::stratifiedUnit(
          test_seed, scenario, count, teacher::kVisibleTapeDomain, 0);
      const int reveal_stratum =
          static_cast<int>(std::floor(reveal_unit * count));
      const int visible_stratum =
          static_cast<int>(std::floor(visible_unit * count));
      stratification =
          stratification && reveal_stratum >= 0 && reveal_stratum < count &&
          visible_stratum >= 0 && visible_stratum < count &&
          !reveal_strata[static_cast<std::size_t>(reveal_stratum)] &&
          !visible_strata[static_cast<std::size_t>(visible_stratum)];
      reveal_strata[static_cast<std::size_t>(reveal_stratum)] = true;
      visible_strata[static_cast<std::size_t>(visible_stratum)] = true;
    }
  }
  expect(stratification, "scenario tapes are not exactly stratified");
  expect(pairedTCutoff(3) == kPairedT975Df2 &&
             pairedTCutoff(5) == kPairedT975Df4 &&
             pairedTCutoff(7) == kPairedT975Df6 &&
             pairedTCutoff(4) < 0.0,
         "paired-t cutoff wiring");

  ActionRollout baseline;
  ActionRollout challenger;
  for (int scenario = 0; scenario < kMaximumScenarios; ++scenario) {
    baseline.scenarios[scenario].value = 100.0;
    challenger.scenarios[scenario].value = 110.0;
    baseline.scenarios[scenario].survived_horizon = true;
    challenger.scenarios[scenario].survived_horizon = true;
  }
  baseline.mean_value = 100.0;
  challenger.mean_value = 110.0;
  baseline.mean_numbered_clears = 3.0;
  challenger.mean_numbered_clears = 3.0;
  baseline.surviving_scenarios = 3;
  challenger.surviving_scenarios = 3;
  const AlternativeTest gate_pass =
      testAlternative(challenger, baseline, 3'000.0, 10'000.0, 3);
  const AlternativeTest gate_q_fail =
      testAlternative(challenger, baseline, 2'999.0, 10'000.0, 3);
  expect(gate_pass.passed && !gate_q_fail.passed &&
             !gate_q_fail.root_q_ok,
         "gate wiring/boundary");

  std::array<SpecificationResult, kSpecifications.size()> synthetic{};
  synthetic[0].eligible = false;
  synthetic[0].balanced_agreement = 1.0;
  synthetic[1].eligible = true;
  synthetic[1].balanced_agreement = 0.9;
  synthetic[1].continuation.work = 100;
  synthetic[2].eligible = true;
  synthetic[2].balanced_agreement = 0.9;
  synthetic[2].continuation.work = 90;
  expect(selectSpecification(synthetic) == 2,
         "lexicographic selector/tie-work wiring");

  const bool passed = teacher_parity && stratification &&
                      d4::peakRssBytes() <= kMaximumRssBytes;
  output << std::setprecision(12)
         << "D2_ROLLOUT_TEACHER_COMPRESSION_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"teacherRecords\":" << records.size()
         << ",\"teacherSwitches\":" << switches
         << ",\"publicContinuationApi\":true"
         << ",\"hiddenMetadataBlind\":true"
         << ",\"teacherKernelParity\":"
         << (teacher_parity ? "true" : "false")
         << ",\"reflectionSafe\":true"
         << ",\"fullRootBounded\":true"
         << ",\"exactScenarioStratification\":"
         << (stratification ? "true" : "false")
         << ",\"pairedTCutoffs\":true"
         << ",\"gateWiring\":true"
         << ",\"selectionWiring\":true"
         << ",\"worstD1Work\":" << kWorstD1Work
         << ",\"worstD2Work\":" << kWorstD2Work
         << ",\"peakRssBytes\":" << d4::peakRssBytes() << "}\n";
  return passed;
}

void writeSpecification(std::ostream& output,
                        const SpecificationResult& result) {
  output << "{\"id\":\"" << result.specification.id
         << "\",\"label\":\"" << result.specification.label
         << "\",\"rootScenarios\":" << result.specification.scenarios
         << ",\"horizon\":" << result.specification.horizon
         << ",\"fairChanceSamples\":" << fair::kChanceSamples
         << ",\"pairedTCutoff\":"
         << pairedTCutoff(result.specification.scenarios)
         << ",\"records\":" << result.records
         << ",\"exactActions\":" << result.exact_actions
         << ",\"overallActionAgreement\":" << result.overall_agreement
         << ",\"teacherSwitchExactActions\":"
         << result.teacher_switch_exact_actions
         << ",\"exactBeneficialSwitchActionRecall\":"
         << result.exact_switch_action_recall
         << ",\"teacherSwitchAnySwitches\":"
         << result.teacher_switch_any_switches
         << ",\"anySwitchRecall\":" << result.any_switch_recall
         << ",\"fallbackCorrect\":" << result.fallback_correct
         << ",\"fallbackSpecificity\":" << result.fallback_specificity
         << ",\"falseSwitches\":" << result.false_switches
         << ",\"approximationSwitches\":"
         << result.approximation_switches
         << ",\"balancedAgreement\":" << result.balanced_agreement
         << ",\"pairwiseReturnRankAgreements\":"
         << result.pairwise_agreements
         << ",\"pairwiseReturnRankComparisons\":"
         << result.pairwise_comparisons
         << ",\"pairwiseReturnRankAgreement\":"
         << result.pairwise_return_rank_agreement
         << ",\"alternativesConsidered\":"
         << result.alternatives_considered
         << ",\"passingAlternatives\":"
         << result.passing_alternatives
         << ",\"survivorRejections\":" << result.survivor_rejections
         << ",\"clearRejections\":" << result.clear_rejections
         << ",\"returnRejections\":" << result.return_rejections
         << ",\"rootQRejections\":" << result.root_q_rejections
         << ",\"syntheticTransitions\":"
         << result.synthetic_transitions
         << ",\"continuationCalls\":" << result.continuation.calls
         << ",\"d1Calls\":" << result.continuation.d1_calls
         << ",\"d2Calls\":" << result.continuation.d2_calls
         << ",\"continuationWork\":" << result.continuation.work
         << ",\"continuationNodes\":" << result.continuation.nodes
         << ",\"continuationCacheHits\":"
         << result.continuation.cache_hits
         << ",\"continuationRootActions\":"
         << result.continuation.root_actions
         << ",\"peakContinuationCacheEntries\":"
         << result.continuation.peak_cache_entries
         << ",\"fullRoot\":"
         << (result.continuation.full_root ? "true" : "false")
         << ",\"wallSeconds\":" << result.wall_seconds
         << ",\"peakRssBytes\":" << result.peak_rss_bytes
         << ",\"eligible\":" << (result.eligible ? "true" : "false")
         << '}';
}

void writeArtifact(
    const Options& options,
    const std::array<SpecificationResult, kSpecifications.size()>& results,
    int selected, double total_wall) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open compression artifact");
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"d2-rollout-teacher-compression\",\n"
         << "  \"replayOnly\":true,\n"
         << "  \"newGameplaySeedsRead\":[],\n"
         << "  \"teacher\":{\"path\":\"" << options.teacher
         << "\",\"bytes\":" << kTeacherBytes
         << ",\"gameSeedAlreadyRead\":\"0x3ded0000\""
         << ",\"records\":" << kTeacherRecords
         << ",\"switches\":" << kTeacherSwitches
         << ",\"fallbacks\":" << kTeacherFallbacks << "},\n"
         << "  \"frozenProtocol\":{\"allRootActionsFullWidth\":true"
         << ",\"topQPruning\":false"
         << ",\"publicHashAndTapeDomainsUnchanged\":true"
         << ",\"fairChanceSamples\":" << fair::kChanceSamples
         << ",\"survivalGate\":true,\"meanClearGate\":true"
         << ",\"rootQLossMaximum\":" << kMaximumRootQLoss
         << ",\"pairedReturnLower95Positive\":true"
         << ",\"minimumExactBeneficialSwitchActions\":8"
         << ",\"minimumFallbackSpecificity\":0.95"
         << ",\"selectionOrder\":[\"eligibility\",\"maximum balanced agreement\",\"minimum continuation work\",\"menu order\"]},\n"
         << "  \"specifications\":[\n";
  for (std::size_t index = 0; index < results.size(); ++index) {
    output << "    ";
    writeSpecification(output, results[index]);
    output << (index + 1 == results.size() ? "\n" : ",\n");
  }
  output << "  ],\n  \"selection\":";
  if (selected < 0) {
    output << "null,\n  \"conclusion\":\"rejected-no-approximation-passed-frozen-gates\"";
  } else {
    const SpecificationResult& winner =
        results[static_cast<std::size_t>(selected)];
    output << "{\"id\":\"" << winner.specification.id
           << "\",\"label\":\"" << winner.specification.label
           << "\"},\n  \"conclusion\":\"selected-by-frozen-lexicographic-rule\"";
  }
  output << ",\n  \"threads\":" << options.threads
         << ",\n  \"totalWallSeconds\":" << total_wall
         << ",\n  \"peakRssBytes\":" << d4::peakRssBytes() << "\n}\n";
}

int run(const Options& options, std::ostream& report) {
  if (!selfTest(options, report)) return 1;
  const std::vector<TeacherRecord> records = loadTeacher(options.teacher);
  const auto started = std::chrono::steady_clock::now();
  std::array<SpecificationResult, kSpecifications.size()> results{};
  for (std::size_t index = 0; index < kSpecifications.size(); ++index) {
    results[index] = evaluateSpecification(kSpecifications[index], records,
                                           options.threads, report);
    report << std::fixed << std::setprecision(6)
           << "teacher-compression " << kSpecifications[index].id
           << " exact=" << results[index].exact_actions << '/'
           << kTeacherRecords << " switchExact="
           << results[index].teacher_switch_exact_actions << '/'
           << kTeacherSwitches << " specificity="
           << results[index].fallback_specificity << " pairRank="
           << results[index].pairwise_return_rank_agreement << " work="
           << results[index].continuation.work << " wall="
           << results[index].wall_seconds << "s eligible="
           << (results[index].eligible ? "true" : "false") << '\n';
  }
  const int selected = selectSpecification(results);
  const double total_wall = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - started)
                                .count();
  writeArtifact(options, results, selected, total_wall);
  report << "D2_ROLLOUT_TEACHER_COMPRESSION_RESULT {\"selection\":";
  if (selected < 0) {
    report << "null,\"conclusion\":\"rejected\"";
  } else {
    report << '\"' << kSpecifications[static_cast<std::size_t>(selected)].id
           << "\",\"conclusion\":\"selected\"";
  }
  report << ",\"totalWallSeconds\":" << total_wall
         << ",\"peakRssBytes\":" << d4::peakRssBytes()
         << ",\"output\":\"" << options.output << "\"}\n";
  return 0;
}

}  // namespace drop7::d2_rollout_teacher_compression

#ifndef DROP7_D2_ROLLOUT_TEACHER_COMPRESSION_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc < 2) throw std::invalid_argument("missing mode");
    const std::string mode = argv[1];
    const auto options =
        drop7::d2_rollout_teacher_compression::parseOptions(argc, argv, 2);
    if (mode == "--self-test") {
      return drop7::d2_rollout_teacher_compression::selfTest(options, std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (mode == "--run") {
      return drop7::d2_rollout_teacher_compression::run(options, std::cout);
    }
    throw std::invalid_argument(
        "usage: drop7_d2_rollout_teacher_compression --self-test|--run "
        "[--teacher path] [--output path] [--threads 1..8]");
  } catch (const std::exception& error) {
    std::cerr << "drop7 teacher-compression error: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
#endif
