// Stage D0 of the H-pool program (EX-20260823-hpool-stage-d0-e0ad1c65):
// fair relabelling of oracle-visited public states against matched fair-D4
// public states.
//
// One source, two binaries:
//
//   d0-generate  (compiled with -DD0_GENERATE)  runs the privileged oracle
//                games and the public fair-D4 games, samples states from
//                move 50 onward, matches F to O on (rise phase, occupancy
//                bin, height bin) exactly as oracle-topology-audit.cpp bins
//                them, records the realised remaining moves on each state's
//                own trajectory (R_tape / R_real) and fair D4's column at
//                every O root, and writes public records.  Oracle privilege
//                terminates at stripToPublic(); nothing after it can read the
//                seed or the tape.
//
//   d0-relabel   (compiled without D0_GENERATE) links no oracle code at all
//                (gate.sh checks the symbol table).  It reads the public
//                tuple (board, next disc, moves until rise) from each record
//                and computes R_fair: the mean remaining moves over K = 32
//                public futures derived from the public-state hash through
//                the domain-separated restart streams of
//                oracle-curriculum.cpp ("CRRV" reveals, "CRVS" visible discs),
//                under fair depth-1 continuation at horizon 25, plus the same
//                quantity for every legal first column under the same 32
//                futures (common random numbers), the flow band, and the
//                per-scenario censor flags.  Label fields in the input
//                (columns, realised moves) are copied through untouched and
//                never parsed.
//
// The restart machinery is the one in
// approaches/oracle-curriculum/state-curriculum/oracle-curriculum.cpp with
// two deliberate differences, both recorded in README.mdx: K = 32 scenarios
// instead of 7, and the restart is played in the canonical orientation at
// every step so R_fair is exactly mirror-invariant (the original played in
// the source orientation, where row-major reveal order breaks exactness).
//
// Compile: see build.sh (clang++ -O3 -std=c++20 -pthread -Wall -Wextra
// -Werror -ffp-contract=off).

#ifdef D0_GENERATE
#define DROP7_ORACLE_TOPOLOGY_LIBRARY
#include "../topology/oracle-topology-audit.cpp"
#undef DROP7_ORACLE_TOPOLOGY_LIBRARY
#endif

#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#include "../../fair-expectimax/reference/fair-only-depth4.cpp"
#undef DROP7_FAIR_ONLY_DEPTH4_LIBRARY

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <type_traits>
#include <vector>

namespace drop7::hpool_d0 {

namespace fair = drop7::fair_only_horizon;
namespace d4 = drop7::fair_only_depth4;
using Clock = std::chrono::steady_clock;

// ---- frozen constants (EX-20260823-hpool-stage-d0-e0ad1c65) ----------------
constexpr std::uint32_t kOracleSeedStart = 0xa52e'0000u;
constexpr int kOracleGames = 64;
constexpr std::uint32_t kFairSeedStart = 0xa52e'0100u;
constexpr int kFairSeeds = 256;
[[maybe_unused]] constexpr std::uint32_t kProbeSeedStart = 0xa527'8000u;   // already opened
[[maybe_unused]] constexpr std::uint32_t kProbeSeedEnd = 0xa527'8500u;     // exclusive
[[maybe_unused]] constexpr int kOracleDepth = 4;
[[maybe_unused]] constexpr int kOracleBeam = 128;
[[maybe_unused]] constexpr int kMaximumMoves = 500;
[[maybe_unused]] constexpr int kFirstSampleMove = 50;
constexpr int kTargetStates = 2'000;
constexpr int kPerGameQuota = kTargetStates / kOracleGames;  // 31 -> 1,984
constexpr int kScenarios = 32;
constexpr int kHorizon = 25;
constexpr int kEventsPerStep = 64;
[[maybe_unused]] constexpr int kFairBatch = 16;          // seeds per matching round, fixed
[[maybe_unused]] constexpr int kPerGameBucketCap = 2;    // F states per bucket per F game
constexpr int kOccupancyBinWidth = 4;   // oracle-topology-audit.cpp
constexpr int kHeightBinWidth = 2;      // oracle-topology-audit.cpp
constexpr int kMaximumThreads = 16;
constexpr double kWallLimitSeconds = 7'200.0;
constexpr std::uint64_t kRssLimitBytes = 8ull * 1024ull * 1024ull * 1024ull;
constexpr std::uint64_t kRestartSeedDomain = 0x4355'5252'5345'4544ull;
constexpr std::uint32_t kRestartRevealDomain = 0x4352'5256u;   // "CRRV"
constexpr std::uint32_t kRestartVisibleDomain = 0x4352'5653u;  // "CRVS"

static_assert(kLevelBonus == 17'000 && kMovesPerLevel == 5);
static_assert(kOracleSeedStart + kOracleGames <= kFairSeedStart);
static_assert(kFairSeedStart + kFairSeeds == 0xa52e'0200u);
static_assert(kPerGameQuota * kOracleGames <= kTargetStates);
static_assert(kEventsPerStep > kCellCount);
static_assert(kRestartRevealDomain != kRestartVisibleDomain);
static_assert(kRestartRevealDomain != kRevealDomain &&
              kRestartRevealDomain != kNextDiscDomain &&
              kRestartRevealDomain != cfpi::detail::kRevealSampleDomain &&
              kRestartRevealDomain != cfpi::detail::kDiscSampleDomain);
static_assert(kRestartVisibleDomain != kRevealDomain &&
              kRestartVisibleDomain != kNextDiscDomain &&
              kRestartVisibleDomain != cfpi::detail::kRevealSampleDomain &&
              kRestartVisibleDomain != cfpi::detail::kDiscSampleDomain);
static_assert(fair::kChanceSamples == 5);
#ifdef D0_GENERATE
static_assert(kOracleDepth == drop7::oracle_topology::kDefaultOracleDepth);
static_assert(kOracleBeam == drop7::oracle_topology::kDefaultOracleBeam);
static_assert(kOccupancyBinWidth == drop7::oracle_topology::kOccupancyBinWidth);
static_assert(kHeightBinWidth == drop7::oracle_topology::kHeightBinWidth);
#endif

std::mutex progress_mutex;

// ---- public state: the privilege boundary ----------------------------------
struct PublicState {
  Board board{};
  std::uint8_t next_disc = 1;
  std::uint8_t moves_remaining = kMovesPerLevel;

  bool operator==(const PublicState&) const = default;
};

PublicState stripToPublic(const State& source) {
  if (source.game_over || source.next_disc < 1 ||
      source.next_disc > kBoardSize || source.moves_remaining < 1 ||
      source.moves_remaining > kMovesPerLevel) {
    throw std::invalid_argument("source is not restartable");
  }
  for (const std::uint8_t cell : source.board) {
    if (cell > kCracked) throw std::invalid_argument("invalid board token");
  }
  return {source.board, source.next_disc,
          static_cast<std::uint8_t>(source.moves_remaining)};
}

State materialize(const PublicState& source) {
  State result;
  result.board = source.board;
  result.next_disc = source.next_disc;
  result.score = 0;
  result.level = 1;
  result.moves_remaining = source.moves_remaining;
  result.moves_played = 0;
  result.game_over = false;
  return result;
}

PublicState canonicalPublic(const PublicState& source, bool& mirrored) {
  const State canonical =
      cfpi::detail::canonicalState(materialize(source), mirrored);
  return stripToPublic(canonical);
}

PublicState mirror(const PublicState& source) {
  PublicState result = source;
  result.board = cfpi::detail::mirrorBoard(source.board);
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
  const PublicState state = canonicalPublic(source, ignored);
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

std::uint32_t seed32(std::uint64_t value) {
  return mix32(static_cast<std::uint32_t>(value) ^
               static_cast<std::uint32_t>(value >> 32u));
}

std::uint32_t restartSeed(const PublicState& source) {
  return seed32(publicHash(source) ^ kRestartSeedDomain);
}

struct Bucket {
  int rise_phase = 0;
  int occupancy_bin = 0;
  int height_bin = 0;
  auto operator<=>(const Bucket&) const = default;
};

struct Shape {
  int occupancy = 0;
  int maximum_height = 0;
  int legal_columns = 0;
};

Shape shapeOf(const PublicState& state) {
  Shape result;
  const auto heights = cfpi::detail::columnHeights(state.board);
  for (int column = 0; column < kBoardSize; ++column) {
    result.occupancy += heights[column];
    result.maximum_height = std::max(result.maximum_height, heights[column]);
    result.legal_columns += heights[column] < kBoardSize;
  }
  return result;
}

Bucket bucketOf(const PublicState& state) {
  const Shape shape = shapeOf(state);
  return {state.moves_remaining, shape.occupancy / kOccupancyBinWidth,
          shape.maximum_height / kHeightBinWidth};
}

std::uint64_t peakRssBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ull;
}

void enforceRssLimit() {
  if (peakRssBytes() > kRssLimitBytes) {
    throw std::runtime_error("hpool-d0 exceeded the 8 GiB RSS cap");
  }
}

struct Deadline {
  Clock::time_point started = Clock::now();
  double limit = kWallLimitSeconds;
  double elapsedSeconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }
  bool expired() const { return elapsedSeconds() > limit; }
};

// ---- public restart streams (oracle-curriculum.cpp, K = 32) ----------------
struct RestartRandom {
  std::uint32_t root_seed = 0;
  int scenario = 0;
  int step = 0;
  int event = 0;

  std::uint8_t nextDisc() {
    if (event >= kEventsPerStep) {
      throw std::runtime_error("restart reveal event slice exhausted");
    }
    const int event_index = step * kEventsPerStep + event++;
    const double unit = cfpi::detail::stratifiedUnit(
        root_seed, scenario, kScenarios, kRestartRevealDomain, event_index);
    return static_cast<std::uint8_t>(
        std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
  }
};

std::uint8_t restartVisibleDisc(std::uint32_t root_seed, int scenario,
                                int step) {
  const double unit = cfpi::detail::stratifiedUnit(
      root_seed, scenario, kScenarios, kRestartVisibleDomain, step);
  return static_cast<std::uint8_t>(
      std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
}

bool playRestartMove(const PublicState& source, int action,
                     std::uint32_t root_seed, int scenario, int step,
                     MoveResult& result) {
  if (scenario < 0 || scenario >= kScenarios || step < 0 ||
      step >= kHorizon || !isLegal(source.board, action)) {
    return false;
  }
  const State state = materialize(source);
  RestartRandom random{root_seed, scenario, step, 0};
  if (!cfpi::detail::playMoveSampled(state, action, random, result)) {
    return false;
  }
  result.state.score = 0;
  result.state.level = 1;
  result.state.moves_played = 0;
  if (!result.state.game_over) {
    result.state.next_disc = restartVisibleDisc(root_seed, scenario, step);
  }
  return true;
}

// Fair depth-1 continuation on a canonical public state.  Returns the action
// in the canonical frame.
int chooseFairD1Canonical(const PublicState& canonical) {
  fair::SearchContext context;
  const fair::RootEvaluation root =
      fair::rootDecision(materialize(canonical), 1, context);
  int legal = 0;
  int evaluated = 0;
  for (int action = 0; action < kBoardSize; ++action) {
    legal += isLegal(canonical.board, action);
    evaluated += std::isfinite(root.values[action]);
  }
  if (root.action < 0 || legal != evaluated || context.work > 70 ||
      !context.cache.empty()) {
    throw std::runtime_error("restart fair D1 failed exact completion");
  }
  return root.action;
}

struct RestartOutcome {
  int moves = 0;
  int clears = 0;
  int reveals = 0;
  bool survived_horizon = false;
  bool operator==(const RestartOutcome&) const = default;
};

// One public future.  forced_first is a column in the ROOT's own frame
// (-1 = let D1 choose).  The restart is played in the canonical frame at
// every step, so the outcome of a state and of its mirror are identical.
RestartOutcome runRestart(const PublicState& root, int scenario,
                          int forced_first = -1) {
  if (scenario < 0 || scenario >= kScenarios) {
    throw std::invalid_argument("invalid restart scenario");
  }
  bool mirrored = false;
  PublicState state = canonicalPublic(root, mirrored);
  const std::uint32_t root_seed = restartSeed(root);
  RestartOutcome result;
  for (int step = 0; step < kHorizon; ++step) {
    int action = -1;
    if (step == 0 && forced_first >= 0) {
      action = mirrored ? kBoardSize - 1 - forced_first : forced_first;
    } else {
      action = chooseFairD1Canonical(state);
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("restart chose an illegal action");
    }
    MoveResult move;
    if (!playRestartMove(state, action, root_seed, scenario, step, move)) {
      throw std::runtime_error("restart transition failed");
    }
    ++result.moves;
    for (const Wave& wave : move.waves) {
      result.clears += wave.cleared;
      result.reveals += wave.revealed;
    }
    if (move.state.game_over) return result;
    bool ignored = false;
    state = canonicalPublic(stripToPublic(move.state), ignored);
  }
  result.survived_horizon = true;
  return result;
}

enum class FlowBand { kBlocked, kClosed, kRecovering, kFlowing };

std::string_view flowBandName(FlowBand band) {
  switch (band) {
    case FlowBand::kBlocked: return "blocked";
    case FlowBand::kClosed: return "closed";
    case FlowBand::kRecovering: return "recovering";
    case FlowBand::kFlowing: return "flowing";
  }
  throw std::logic_error("unknown flow band");
}

struct Relabel {
  double mean_moves = 0.0;
  int survived = 0;
  double reveals_per_move = 0.0;
  double clears_per_move = 0.0;
  FlowBand flow = FlowBand::kBlocked;
  std::array<int, kScenarios> scenario_moves{};
  std::array<double, kBoardSize> sibling_mean_moves{};  // NaN if illegal
  std::array<int, kBoardSize> sibling_survived{};
  bool operator==(const Relabel&) const = default;
};

// The relabel path: a function of the public tuple only.
Relabel relabel(const PublicState& state) {
  Relabel result;
  int total_moves = 0;
  int total_clears = 0;
  int total_reveals = 0;
  for (int scenario = 0; scenario < kScenarios; ++scenario) {
    const RestartOutcome outcome = runRestart(state, scenario);
    result.scenario_moves[scenario] = outcome.moves;
    result.survived += outcome.survived_horizon;
    total_moves += outcome.moves;
    total_clears += outcome.clears;
    total_reveals += outcome.reveals;
  }
  result.mean_moves = static_cast<double>(total_moves) / kScenarios;
  if (total_moves > 0) {
    result.reveals_per_move =
        static_cast<double>(total_reveals) / total_moves;
    result.clears_per_move = static_cast<double>(total_clears) / total_moves;
  }
  const double survival_rate =
      static_cast<double>(result.survived) / kScenarios;
  if (survival_rate < 0.25) {
    result.flow = FlowBand::kBlocked;
  } else if (result.reveals_per_move < 0.25) {
    result.flow = FlowBand::kClosed;
  } else if (result.reveals_per_move < 0.60) {
    result.flow = FlowBand::kRecovering;
  } else {
    result.flow = FlowBand::kFlowing;
  }
  for (int column = 0; column < kBoardSize; ++column) {
    if (!isLegal(state.board, column)) {
      result.sibling_mean_moves[column] =
          std::numeric_limits<double>::quiet_NaN();
      result.sibling_survived[column] = -1;
      continue;
    }
    int moves = 0;
    int survived = 0;
    for (int scenario = 0; scenario < kScenarios; ++scenario) {
      const RestartOutcome outcome = runRestart(state, scenario, column);
      moves += outcome.moves;
      survived += outcome.survived_horizon;
    }
    result.sibling_mean_moves[column] =
        static_cast<double>(moves) / kScenarios;
    result.sibling_survived[column] = survived;
  }
  return result;
}

using RelabelFunction = Relabel (*)(const PublicState&);
static_assert(std::is_same_v<decltype(&relabel), RelabelFunction>);
static_assert(!std::is_invocable_v<RelabelFunction, const State&>);
static_assert(!std::is_invocable_v<RelabelFunction, std::uint32_t>);

// ---- JSON helpers -----------------------------------------------------------
std::string hexSeed(std::uint32_t seed) {
  std::ostringstream output;
  output << "0x" << std::hex << std::setw(8) << std::setfill('0') << seed;
  return output.str();
}

std::string hex64(std::uint64_t value) {
  std::ostringstream output;
  output << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
  return output.str();
}

std::string numberText(double value) {
  if (std::isnan(value)) return "null";
  std::ostringstream output;
  output << std::setprecision(17) << value;
  return output.str();
}

void writePublicState(std::ostream& output, const PublicState& state) {
  output << "{\"board\":\"" << serializeBoard(state.board)
         << "\",\"nextDisc\":" << static_cast<int>(state.next_disc)
         << ",\"movesRemaining\":" << static_cast<int>(state.moves_remaining)
         << '}';
}

// Minimal extraction of the public tuple from one JSONL record.  Only these
// three fields are ever parsed by the relabel path.
PublicState parsePublicState(const std::string& line) {
  const auto field = [&](std::string_view name) -> std::size_t {
    const std::string needle = "\"" + std::string(name) + "\":";
    const std::size_t at = line.find(needle);
    if (at == std::string::npos) {
      throw std::runtime_error("record lacks " + std::string(name));
    }
    return at + needle.size();
  };
  PublicState state;
  const std::size_t board_at = field("board") + 1;  // skip the opening quote
  if (board_at + kCellCount > line.size()) {
    throw std::runtime_error("truncated board");
  }
  for (int cell = 0; cell < kCellCount; ++cell) {
    const char token = line[board_at + static_cast<std::size_t>(cell)];
    if (token < '0' || token > '9') throw std::runtime_error("bad board");
    state.board[static_cast<std::size_t>(cell)] =
        static_cast<std::uint8_t>(token - '0');
  }
  state.next_disc =
      static_cast<std::uint8_t>(std::stoi(line.substr(field("nextDisc"))));
  state.moves_remaining = static_cast<std::uint8_t>(
      std::stoi(line.substr(field("movesRemaining"))));
  return stripToPublic(materialize(state));
}

void writeRelabel(std::ostream& output, const Relabel& value) {
  output << "{\"scenarios\":" << kScenarios << ",\"horizon\":" << kHorizon
         << ",\"meanMoves\":" << numberText(value.mean_moves)
         << ",\"survived\":" << value.survived
         << ",\"revealsPerMove\":" << numberText(value.reveals_per_move)
         << ",\"clearsPerMove\":" << numberText(value.clears_per_move)
         << ",\"flowBand\":\"" << flowBandName(value.flow)
         << "\",\"scenarioMoves\":[";
  for (int scenario = 0; scenario < kScenarios; ++scenario) {
    if (scenario != 0) output << ',';
    output << value.scenario_moves[scenario];
  }
  output << "],\"siblingMeanMoves\":[";
  for (int column = 0; column < kBoardSize; ++column) {
    if (column != 0) output << ',';
    output << numberText(value.sibling_mean_moves[column]);
  }
  output << "],\"siblingSurvived\":[";
  for (int column = 0; column < kBoardSize; ++column) {
    if (column != 0) output << ',';
    if (value.sibling_survived[column] < 0) {
      output << "null";
    } else {
      output << value.sibling_survived[column];
    }
  }
  output << "]}";
}

// ---- relabel driver ---------------------------------------------------------
struct RelabelOptions {
  std::string input;
  std::string output;
  int threads = 8;
  bool self_test = false;
};

std::vector<std::string> readLines(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open " + path);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.front() == '{') lines.push_back(line);
  }
  return lines;
}

template <typename Work>
void parallelFor(std::size_t count, int threads, Work&& work) {
  std::atomic<std::size_t> next{0};
  std::vector<std::future<void>> workers;
  const int worker_count =
      std::max(1, std::min<int>(threads, static_cast<int>(count)));
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [&]() {
      for (;;) {
        const std::size_t index = next.fetch_add(1);
        if (index >= count) return;
        work(index);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
}

int runRelabel(const RelabelOptions& options) {
  const Deadline deadline;
  const std::vector<std::string> lines = readLines(options.input);
  std::vector<PublicState> states(lines.size());
  for (std::size_t index = 0; index < lines.size(); ++index) {
    states[index] = parsePublicState(lines[index]);
  }
  std::vector<Relabel> labels(states.size());
  std::atomic<std::size_t> done{0};
  parallelFor(states.size(), options.threads, [&](std::size_t index) {
    labels[index] = relabel(states[index]);
    const std::size_t finished = done.fetch_add(1) + 1;
    if (finished % 200 == 0 || finished == states.size()) {
      const std::lock_guard<std::mutex> lock(progress_mutex);
      std::cerr << "d0-relabel " << finished << '/' << states.size()
                << " states " << std::fixed << std::setprecision(1)
                << deadline.elapsedSeconds() << "s\n";
    }
  });
  enforceRssLimit();
  std::ofstream output(options.output, std::ios::trunc);
  if (!output) throw std::runtime_error("cannot write " + options.output);
  output << "{\"format\":\"drop7-hpool-d0-pools-v1\",\"relabel\":{"
         << "\"scenarios\":" << kScenarios << ",\"horizon\":" << kHorizon
         << ",\"continuation\":\"fair D1 (fair-only-horizon rootDecision "
            "depth 1, canonical frame each step)\",\"revealDomain\":\""
         << hexSeed(kRestartRevealDomain) << "\",\"visibleDomain\":\""
         << hexSeed(kRestartVisibleDomain) << "\",\"seedDomain\":\""
         << hex64(kRestartSeedDomain)
         << "\",\"threads\":" << options.threads
         << ",\"wallSeconds\":" << numberText(deadline.elapsedSeconds())
         << ",\"peakRssBytes\":" << peakRssBytes() << "},\"states\":[\n";
  for (std::size_t index = 0; index < lines.size(); ++index) {
    if (index != 0) output << ",\n";
    std::string line = lines[index];
    while (!line.empty() && line.back() != '}') line.pop_back();
    line.pop_back();
    output << line << ",\"publicHash\":\"" << hex64(publicHash(states[index]))
           << "\",\"relabel\":";
    writeRelabel(output, labels[index]);
    output << '}';
  }
  output << "\n]}\n";
  output.close();
  if (!output) throw std::runtime_error("could not finish " + options.output);
  std::cerr << "d0-relabel wrote " << lines.size() << " states to "
            << options.output << " in " << std::fixed << std::setprecision(1)
            << deadline.elapsedSeconds() << "s\n";
  return 0;
}

// Self-test on public records: mirror invariance (exact), metadata
// independence, domain separation, and sibling/root consistency.
int runRelabelSelfTest(const RelabelOptions& options) {
  const std::vector<std::string> lines = readLines(options.input);
  std::size_t tested = 0;
  std::size_t mirror_failures = 0;
  std::size_t metadata_failures = 0;
  std::size_t sibling_failures = 0;
  std::size_t stream_failures = 0;
  std::vector<PublicState> states;
  for (const std::string& line : lines) states.push_back(parsePublicState(line));
  const std::size_t limit = std::min<std::size_t>(states.size(), 64);
  std::vector<int> failures(limit * 4, 0);
  parallelFor(limit, options.threads, [&](std::size_t index) {
    const PublicState& state = states[index];
    const Relabel base = relabel(state);
    // (1) exact mirror invariance; siblings map to mirrored columns.
    const Relabel mirrored = relabel(mirror(state));
    bool mirror_ok = mirrored.mean_moves == base.mean_moves &&
                     mirrored.survived == base.survived &&
                     mirrored.scenario_moves == base.scenario_moves &&
                     mirrored.reveals_per_move == base.reveals_per_move;
    for (int column = 0; column < kBoardSize; ++column) {
      const double left = base.sibling_mean_moves[column];
      const double right = mirrored.sibling_mean_moves[kBoardSize - 1 - column];
      if (std::isnan(left) != std::isnan(right)) mirror_ok = false;
      if (!std::isnan(left) && left != right) mirror_ok = false;
    }
    failures[index * 4 + 0] = !mirror_ok;
    // (2) metadata independence: score / level / move counter are not in the
    // public tuple and cannot change anything.
    State origin = materialize(state);
    origin.score = 9'876'543;
    origin.level = 91;
    origin.moves_played = 417;
    const PublicState re_export = stripToPublic(origin);
    failures[index * 4 + 1] =
        !(re_export == state && restartSeed(re_export) == restartSeed(state) &&
          relabel(re_export) == base);
    // (3) the unforced restart equals the forced restart at D1's own column
    // in scenario 0 (the forced path is the same path).
    bool ignored = false;
    const PublicState canonical = canonicalPublic(state, ignored);
    const int d1 = chooseFairD1Canonical(canonical);
    const int d1_source = ignored ? kBoardSize - 1 - d1 : d1;
    failures[index * 4 + 2] =
        !(runRestart(state, 0, d1_source) == runRestart(state, 0));
    // (4) domain separation: for every scenario the visible-disc stream
    // ("CRVS") differs from the first reveal stream ("CRRV") of the same
    // scenario, and the scenarios are distinct as a set.  Adjacent strata of
    // the stratified visible stream legitimately coincide on a 25-step
    // window now and then (32 strata over 7 disc values), so pairwise
    // distinctness of the visible streams alone is not required; the
    // concatenated (visible, reveal) streams of the 32 scenarios must all
    // be distinct.
    const std::uint32_t root_seed = restartSeed(state);
    bool streams_ok = true;
    std::vector<std::string> fingerprints;
    for (int scenario = 0; scenario < kScenarios; ++scenario) {
      std::string visible;
      std::string reveal;
      for (int step = 0; step < kHorizon; ++step) {
        RestartRandom random{root_seed, scenario, step, 0};
        visible.push_back(static_cast<char>(
            '0' + restartVisibleDisc(root_seed, scenario, step)));
        reveal.push_back(static_cast<char>('0' + random.nextDisc()));
      }
      if (visible == reveal) streams_ok = false;
      fingerprints.push_back(visible + "|" + reveal);
    }
    std::sort(fingerprints.begin(), fingerprints.end());
    if (std::adjacent_find(fingerprints.begin(), fingerprints.end()) !=
        fingerprints.end()) {
      streams_ok = false;
    }
    failures[index * 4 + 3] = !streams_ok;
  });
  for (std::size_t index = 0; index < limit; ++index) {
    ++tested;
    mirror_failures += failures[index * 4 + 0];
    metadata_failures += failures[index * 4 + 1];
    sibling_failures += failures[index * 4 + 2];
    stream_failures += failures[index * 4 + 3];
  }
  std::cout << "relabel-self-test states=" << tested
            << " mirror_failures=" << mirror_failures
            << " metadata_failures=" << metadata_failures
            << " sibling_consistency_failures=" << sibling_failures
            << " stream_separation_failures=" << stream_failures << '\n';
  const bool ok = tested > 0 && mirror_failures == 0 &&
                  metadata_failures == 0 && sibling_failures == 0 &&
                  stream_failures == 0;
  std::cout << (ok ? "PASS" : "FAIL") << " relabel-self-test\n";
  return ok ? 0 : 1;
}

RelabelOptions parseRelabelOptions(int argc, char** argv, int begin) {
  RelabelOptions result;
  for (int index = begin; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--self-test") {
      result.self_test = true;
      continue;
    }
    if (index + 1 >= argc) throw std::invalid_argument("missing value");
    const std::string value = argv[++index];
    if (argument == "--input") {
      result.input = value;
    } else if (argument == "--output") {
      result.output = value;
    } else if (argument == "--threads") {
      result.threads = std::stoi(value);
      if (result.threads < 1 || result.threads > kMaximumThreads) {
        throw std::invalid_argument("threads must be in [1,16]");
      }
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  if (result.input.empty()) throw std::invalid_argument("--input required");
  if (!result.self_test && result.output.empty()) {
    throw std::invalid_argument("--output required");
  }
  return result;
}

#ifdef D0_GENERATE
// ---- generation (privileged oracle + public fair D4) ------------------------
namespace oracle = drop7::oracle_topology;

struct GenerateOptions {
  std::string output;          // JSONL of public records
  std::string summary;         // JSON summary
  int threads = 8;
  bool probe = false;          // probe seeds only (gate mode)
  int oracle_games = kOracleGames;
  int max_moves = kMaximumMoves;
  int fair_max_moves = kMaximumMoves;
  int fair_seeds = kFairSeeds;
  int quota = kPerGameQuota;
  double wall_limit = kWallLimitSeconds;
};

struct Visit {
  PublicState state;   // in the trajectory's own orientation
  int move = 0;        // moves_played at the state
  int column = -1;     // the column played from it (oracle or fair D4)
};

struct Trajectory {
  std::uint32_t seed = 0;
  int moves = 0;
  std::int64_t score = 0;
  bool censored = false;
  double seconds = 0.0;
  std::vector<Visit> visits;   // every non-terminal state from move 50 on
};

bool seedAllowed(const GenerateOptions& options, std::uint32_t seed) {
  if (options.probe) return seed >= kProbeSeedStart && seed < kProbeSeedEnd;
  return (seed >= kOracleSeedStart && seed < kOracleSeedStart + kOracleGames) ||
         (seed >= kFairSeedStart && seed < kFairSeedStart + kFairSeeds);
}

void requireSeed(const GenerateOptions& options, std::uint32_t seed) {
  if (!seedAllowed(options, seed)) {
    throw std::invalid_argument("seed " + hexSeed(seed) +
                                " is outside the allowed range");
  }
}

Trajectory runOracleTrajectory(const GenerateOptions& options,
                               std::uint32_t seed) {
  requireSeed(options, seed);
  const auto started = Clock::now();
  State state = initialHeadlessState(seed);
  Trajectory result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < options.max_moves) {
    if (state.next_disc != headlessDisc(seed, state.moves_played)) {
      throw std::runtime_error("oracle disc-stream guard failed");
    }
    const oracle::OraclePlan plan =
        oracle::planOracleMove(state, seed, kOracleDepth, kOracleBeam);
    if (!isLegal(state.board, plan.column)) {
      throw std::runtime_error("oracle selected an illegal action");
    }
    if (state.moves_played >= kFirstSampleMove) {
      result.visits.push_back(
          {stripToPublic(state), state.moves_played, plan.column});
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, plan.column, move)) {
      throw std::runtime_error("oracle transition failed");
    }
  }
  result.moves = state.moves_played;
  result.score = state.score;
  result.censored = !state.game_over;
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

Trajectory runFairTrajectory(const GenerateOptions& options,
                             std::uint32_t seed) {
  requireSeed(options, seed);
  const auto started = Clock::now();
  State state = initialHeadlessState(seed);
  Trajectory result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < options.fair_max_moves) {
    const d4::SearchDecision decision = d4::chooseDepth4Action(state);
    if (!decision.complete || !isLegal(state.board, decision.action)) {
      throw std::runtime_error("fair depth four did not complete");
    }
    if (state.moves_played >= kFirstSampleMove) {
      result.visits.push_back(
          {stripToPublic(state), state.moves_played, decision.action});
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("fair transition failed");
    }
  }
  result.moves = state.moves_played;
  result.score = state.score;
  result.censored = !state.game_over;
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

// Fair D4's column at a public root.  Public information only.
int fairD4ColumnAt(const PublicState& state) {
  const d4::SearchDecision decision =
      d4::chooseDepth4Action(materialize(state));
  if (!decision.complete || !isLegal(state.board, decision.action)) {
    throw std::runtime_error("fair depth four did not complete at O root");
  }
  return decision.action;
}

struct Record {
  char pool = 'O';
  int game = 0;
  std::uint32_t seed = 0;
  int move = 0;
  PublicState state;
  int column = -1;          // played column (oracle for O, fair D4 for F)
  int d4_column = -1;       // fair D4 at the root (O only; equals column for F)
  int remaining = 0;        // realised remaining moves, uncapped
  bool remaining_censored = false;  // origin game hit the move cap first
  Bucket bucket;
  int match = -1;           // index of the partner record
  int half = 0;             // origin-game half (O: game < games/2)
};

constexpr std::uint32_t kSampleDomain = 0x4430'5350u;  // "D0SP"

// One visit per stratum: the eligible visits are cut into `quota` equal
// strata and one index is drawn inside each with a seed-keyed hash.  A
// centred evenly-spaced rule aliases with the five-move rise cadence (a
// stride that is a multiple of five samples a single rise phase), so the
// within-stratum offset is jittered instead.
int stratifiedIndex(std::uint32_t seed, int slot, int quota, int count) {
  const int begin = static_cast<int>(static_cast<long long>(slot) * count / quota);
  const int end =
      static_cast<int>(static_cast<long long>(slot + 1) * count / quota);
  const int length = std::max(1, end - begin);
  const std::uint32_t draw = mix32(
      seed ^ (static_cast<std::uint32_t>(slot + 1) * 0x9e37'79b9u) ^
      kSampleDomain);
  return std::min(count - 1, begin + static_cast<int>(draw % static_cast<std::uint32_t>(length)));
}

void writeRecord(std::ostream& output, const Record& record, int index) {
  output << "{\"id\":" << index << ",\"pool\":\"" << record.pool
         << "\",\"game\":" << record.game << ",\"seed\":\""
         << hexSeed(record.seed) << "\",\"move\":" << record.move
         << ",\"state\":";
  writePublicState(output, record.state);
  output << ",\"column\":" << record.column
         << ",\"d4Column\":" << record.d4_column
         << ",\"remainingRealised\":" << record.remaining
         << ",\"remainingCapped\":" << std::min(record.remaining, kHorizon)
         << ",\"remainingCensored\":"
         << (record.remaining_censored ? "true" : "false")
         << ",\"bucket\":{\"risePhase\":" << record.bucket.rise_phase
         << ",\"occupancyBin\":" << record.bucket.occupancy_bin
         << ",\"heightBin\":" << record.bucket.height_bin
         << "},\"match\":" << record.match << ",\"half\":" << record.half
         << "}\n";
}

int runGenerate(const GenerateOptions& options) {
  Deadline deadline;
  deadline.limit = options.wall_limit;
  const std::uint32_t oracle_start =
      options.probe ? kProbeSeedStart : kOracleSeedStart;
  const std::uint32_t fair_start =
      options.probe ? kProbeSeedStart + 0x100u : kFairSeedStart;
  bool partial = false;
  std::string stop_reason = "completed";

  // --- pool O -------------------------------------------------------------
  std::vector<Trajectory> oracle_games(
      static_cast<std::size_t>(options.oracle_games));
  parallelFor(oracle_games.size(), options.threads, [&](std::size_t index) {
    const std::uint32_t seed = oracle_start + static_cast<std::uint32_t>(index);
    oracle_games[index] = runOracleTrajectory(options, seed);
    const std::lock_guard<std::mutex> lock(progress_mutex);
    std::cerr << "d0-generate oracle " << index + 1 << '/'
              << oracle_games.size() << " seed " << hexSeed(seed) << " moves "
              << oracle_games[index].moves << " score "
              << oracle_games[index].score << " visits "
              << oracle_games[index].visits.size() << '\n';
  });
  enforceRssLimit();

  std::vector<Record> records;
  std::uint64_t oracle_visits_total = 0;
  for (std::size_t game = 0; game < oracle_games.size(); ++game) {
    const Trajectory& trajectory = oracle_games[game];
    const int count = static_cast<int>(trajectory.visits.size());
    oracle_visits_total += static_cast<std::uint64_t>(count);
    const int take = std::min(count, options.quota);
    for (int slot = 0; slot < take; ++slot) {
      const int pick =
          count <= options.quota
              ? slot
              : stratifiedIndex(trajectory.seed, slot, options.quota, count);
      const Visit& visit = trajectory.visits[static_cast<std::size_t>(pick)];
      Record record;
      record.pool = 'O';
      record.game = static_cast<int>(game);
      record.seed = trajectory.seed;
      record.move = visit.move;
      record.state = visit.state;
      record.column = visit.column;
      record.remaining = trajectory.moves - visit.move;
      record.remaining_censored =
          trajectory.censored && record.remaining < kHorizon;
      record.bucket = bucketOf(visit.state);
      record.half = game < oracle_games.size() / 2 ? 0 : 1;
      records.push_back(record);
    }
  }
  const std::size_t o_count = records.size();
  std::cerr << "d0-generate pool O: " << o_count << " states from "
            << oracle_visits_total << " eligible visits\n";

  // fair D4's column at every O root (public computation).
  parallelFor(o_count, options.threads, [&](std::size_t index) {
    records[index].d4_column = fairD4ColumnAt(records[index].state);
    if ((index + 1) % 100 == 0) {
      const std::lock_guard<std::mutex> lock(progress_mutex);
      std::cerr << "d0-generate fair-D4 at O roots " << index + 1 << '/'
                << o_count << " " << std::fixed << std::setprecision(1)
                << deadline.elapsedSeconds() << "s\n";
    }
  });
  enforceRssLimit();

  // --- pool F, generated lazily in fixed batches of 16 seeds ---------------
  std::map<Bucket, std::vector<std::size_t>> demand;
  for (std::size_t index = 0; index < o_count; ++index) {
    demand[records[index].bucket].push_back(index);
  }
  std::size_t open_demand = o_count;
  int fair_games_played = 0;
  std::uint64_t fair_visits_total = 0;
  std::vector<Trajectory> fair_games;
  for (int batch_start = 0;
       batch_start < options.fair_seeds && open_demand > 0;
       batch_start += kFairBatch) {
    if (deadline.expired()) {
      partial = true;
      stop_reason = "wall limit reached before pool F was served";
      break;
    }
    const int batch_count =
        std::min(kFairBatch, options.fair_seeds - batch_start);
    std::vector<Trajectory> batch(static_cast<std::size_t>(batch_count));
    parallelFor(batch.size(), options.threads, [&](std::size_t index) {
      const std::uint32_t seed =
          fair_start + static_cast<std::uint32_t>(batch_start) +
          static_cast<std::uint32_t>(index);
      batch[index] = runFairTrajectory(options, seed);
      const std::lock_guard<std::mutex> lock(progress_mutex);
      std::cerr << "d0-generate fair " << batch_start + static_cast<int>(index) + 1
                << '/' << options.fair_seeds << " seed " << hexSeed(seed)
                << " moves " << batch[index].moves << " score "
                << batch[index].score << " visits " << batch[index].visits.size()
                << " " << std::fixed << std::setprecision(1)
                << deadline.elapsedSeconds() << "s\n";
    });
    enforceRssLimit();
    for (std::size_t local = 0; local < batch.size(); ++local) {
      const Trajectory& trajectory = batch[local];
      const int game = batch_start + static_cast<int>(local);
      ++fair_games_played;
      fair_visits_total += trajectory.visits.size();
      std::map<Bucket, int> used;
      for (const Visit& visit : trajectory.visits) {
        if (open_demand == 0) break;
        const Bucket bucket = bucketOf(visit.state);
        auto found = demand.find(bucket);
        if (found == demand.end() || found->second.empty()) continue;
        if (used[bucket] >= kPerGameBucketCap) continue;
        ++used[bucket];
        const std::size_t partner = found->second.front();
        found->second.erase(found->second.begin());
        --open_demand;
        Record record;
        record.pool = 'F';
        record.game = game;
        record.seed = trajectory.seed;
        record.move = visit.move;
        record.state = visit.state;
        record.column = visit.column;
        record.d4_column = visit.column;
        record.remaining = trajectory.moves - visit.move;
        record.remaining_censored =
            trajectory.censored && record.remaining < kHorizon;
        record.bucket = bucket;
        record.match = static_cast<int>(partner);
        record.half = records[partner].half;
        records[partner].match = static_cast<int>(records.size());
        records.push_back(record);
      }
      fair_games.push_back(trajectory);
    }
    std::cerr << "d0-generate pool F: " << records.size() - o_count
              << " matched after " << fair_games_played << " fair games; "
              << open_demand << " O states still unmatched\n";
  }
  const std::size_t matched = records.size() - o_count;
  const std::size_t unmatched = open_demand;

  // --- write ----------------------------------------------------------------
  {
    std::ofstream output(options.output, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write " + options.output);
    for (std::size_t index = 0; index < records.size(); ++index) {
      writeRecord(output, records[index], static_cast<int>(index));
    }
    output.close();
    if (!output) throw std::runtime_error("could not finish records");
  }
  {
    std::ofstream output(options.summary, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write " + options.summary);
    output << "{\"format\":\"drop7-hpool-d0-generate-v1\",\"probe\":"
           << (options.probe ? "true" : "false")
           << ",\"partial\":" << (partial ? "true" : "false")
           << ",\"stopReason\":\"" << stop_reason << "\""
           << ",\"oracle\":{\"seedStart\":\"" << hexSeed(oracle_start)
           << "\",\"games\":" << options.oracle_games
           << ",\"depth\":" << kOracleDepth << ",\"beam\":" << kOracleBeam
           << ",\"maxMoves\":" << options.max_moves
           << ",\"firstSampleMove\":" << kFirstSampleMove
           << ",\"quotaPerGame\":" << options.quota
           << ",\"samplingRule\":\"per game, take min(quota, eligible) "
              "visits; when eligible > quota pick index "
              "begin + mix32(seed ^ (slot+1)*0x9e3779b9 ^ 0x44305350) % (end-begin) "
              "with begin=floor(slot*eligible/quota), end=floor((slot+1)*"
              "eligible/quota) for slot in [0,quota) (one visit per equal "
              "stratum, seed-keyed offset to avoid aliasing with the five-"
              "move rise cadence); eligible = non-terminal states with "
              "moves_played >= 50 in move order\""
           << ",\"eligibleVisits\":" << oracle_visits_total
           << ",\"states\":" << o_count << ",\"games\":[";
    for (std::size_t game = 0; game < oracle_games.size(); ++game) {
      const Trajectory& trajectory = oracle_games[game];
      if (game != 0) output << ',';
      output << "{\"seed\":\"" << hexSeed(trajectory.seed) << "\",\"moves\":"
             << trajectory.moves << ",\"score\":" << trajectory.score
             << ",\"censored\":" << (trajectory.censored ? "true" : "false")
             << ",\"visits\":" << trajectory.visits.size()
             << ",\"seconds\":" << numberText(trajectory.seconds) << '}';
    }
    output << "]},\"fair\":{\"seedStart\":\"" << hexSeed(fair_start)
           << "\",\"seedsAvailable\":" << options.fair_seeds
           << ",\"gamesPlayed\":" << fair_games_played
           << ",\"batch\":" << kFairBatch
           << ",\"perGameBucketCap\":" << kPerGameBucketCap
           << ",\"maxMoves\":" << options.fair_max_moves
           << ",\"search\":\"fair-only-depth4 reference (depth 4, five "
              "strata, 3,200,000 work cap)\""
           << ",\"eligibleVisits\":" << fair_visits_total
           << ",\"matchingRule\":\"buckets (risePhase, occupancy/4, "
              "maxHeight/2) as oracle-topology-audit.cpp; F visits consumed "
              "in (seed, move) order, at most 2 per bucket per F game, each "
              "assigned to the earliest unmatched O state of its bucket; "
              "generation stops when every O state is matched or the seeds "
              "are exhausted\""
           << ",\"games\":[";
    for (std::size_t game = 0; game < fair_games.size(); ++game) {
      const Trajectory& trajectory = fair_games[game];
      if (game != 0) output << ',';
      output << "{\"seed\":\"" << hexSeed(trajectory.seed) << "\",\"moves\":"
             << trajectory.moves << ",\"score\":" << trajectory.score
             << ",\"censored\":" << (trajectory.censored ? "true" : "false")
             << ",\"visits\":" << trajectory.visits.size()
             << ",\"seconds\":" << numberText(trajectory.seconds) << '}';
    }
    output << "]},\"matching\":{\"oStates\":" << o_count
           << ",\"matched\":" << matched << ",\"unmatched\":" << unmatched
           << ",\"buckets\":" << demand.size() << ",\"unservedBuckets\":[";
    bool first = true;
    for (const auto& [bucket, waiting] : demand) {
      if (waiting.empty()) continue;
      if (!first) output << ',';
      first = false;
      output << "{\"risePhase\":" << bucket.rise_phase << ",\"occupancyBin\":"
             << bucket.occupancy_bin << ",\"heightBin\":" << bucket.height_bin
             << ",\"unmatched\":" << waiting.size() << '}';
    }
    output << "]},\"threads\":" << options.threads
           << ",\"wallSeconds\":" << numberText(deadline.elapsedSeconds())
           << ",\"peakRssBytes\":" << peakRssBytes() << "}\n";
    output.close();
    if (!output) throw std::runtime_error("could not finish summary");
  }
  std::cerr << "d0-generate done: O=" << o_count << " F=" << matched
            << " unmatched=" << unmatched << " fair games="
            << fair_games_played << " wall " << std::fixed
            << std::setprecision(1) << deadline.elapsedSeconds() << "s"
            << (partial ? " PARTIAL" : "") << '\n';
  return partial ? 3 : 0;
}

GenerateOptions parseGenerateOptions(int argc, char** argv, int begin) {
  GenerateOptions result;
  for (int index = begin; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--probe") {
      result.probe = true;
      continue;
    }
    if (index + 1 >= argc) throw std::invalid_argument("missing value");
    const std::string value = argv[++index];
    if (argument == "--output") {
      result.output = value;
    } else if (argument == "--summary") {
      result.summary = value;
    } else if (argument == "--threads") {
      result.threads = std::stoi(value);
      if (result.threads < 1 || result.threads > kMaximumThreads) {
        throw std::invalid_argument("threads must be in [1,16]");
      }
    } else if (argument == "--oracle-games") {
      result.oracle_games = std::stoi(value);
    } else if (argument == "--max-moves") {
      result.max_moves = std::stoi(value);
    } else if (argument == "--fair-max-moves") {
      result.fair_max_moves = std::stoi(value);
    } else if (argument == "--fair-seeds") {
      result.fair_seeds = std::stoi(value);
    } else if (argument == "--quota") {
      result.quota = std::stoi(value);
    } else if (argument == "--wall-limit") {
      result.wall_limit = std::stod(value);
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  if (result.output.empty() || result.summary.empty()) {
    throw std::invalid_argument("--output and --summary are required");
  }
  if (!result.probe) {
    // The frozen configuration may not be altered on the leased seeds.
    if (result.oracle_games != kOracleGames || result.max_moves != kMaximumMoves ||
        result.fair_max_moves != kMaximumMoves || result.fair_seeds != kFairSeeds ||
        result.quota != kPerGameQuota || result.wall_limit > kWallLimitSeconds) {
      throw std::invalid_argument(
          "non-probe runs use the frozen configuration only");
    }
  } else {
    if (result.oracle_games < 1 || result.oracle_games > 0x100 ||
        result.fair_seeds < 1 || result.fair_seeds > 0x400 ||
        result.quota < 1 || result.max_moves < kFirstSampleMove + 1 ||
        result.fair_max_moves < kFirstSampleMove + 1) {
      throw std::invalid_argument("probe configuration out of range");
    }
  }
  return result;
}
#endif  // D0_GENERATE

}  // namespace drop7::hpool_d0

int main(int argc, char** argv) {
  try {
#ifdef D0_GENERATE
    if (argc >= 2 && std::string_view(argv[1]) == "--generate") {
      return drop7::hpool_d0::runGenerate(
          drop7::hpool_d0::parseGenerateOptions(argc, argv, 2));
    }
    std::cerr << "usage: d0-generate --generate --output RECORDS.jsonl "
                 "--summary SUMMARY.json [--threads N] [--probe "
                 "--oracle-games N --max-moves N --fair-max-moves N "
                 "--fair-seeds N --quota N]\n";
    return 2;
#else
    if (argc >= 2 && std::string_view(argv[1]) == "--relabel") {
      const auto options = drop7::hpool_d0::parseRelabelOptions(argc, argv, 2);
      if (options.self_test) {
        return drop7::hpool_d0::runRelabelSelfTest(options);
      }
      return drop7::hpool_d0::runRelabel(options);
    }
    std::cerr << "usage: d0-relabel --relabel --input RECORDS.jsonl "
                 "(--output POOLS.json | --self-test) [--threads N]\n";
    return 2;
#endif
  } catch (const std::exception& error) {
    std::cerr << "hpool-d0: " << error.what() << '\n';
    return 1;
  }
}
