#define DROP7_ORACLE_TOPOLOGY_LIBRARY
#include "../topology/oracle-topology-audit.cpp"
#undef DROP7_ORACLE_TOPOLOGY_LIBRARY

#define DROP7_FAIR_ONLY_HORIZON_LIBRARY
#include "../../fair-expectimax/reference/fair-only-horizon.cpp"
#undef DROP7_FAIR_ONLY_HORIZON_LIBRARY

#include <bit>
#include <type_traits>

// Offline-only conversion of privileged oracle trajectories into a compact
// curriculum of public restart states.  Oracle privilege terminates at the
// PublicState constructor.  Exported records contain only a canonical board,
// visible next disc, five-drop rise phase, and diagnostics recomputed from
// independent public-state-derived stochastic streams.
namespace drop7::oracle_curriculum {

namespace oracle = drop7::oracle_topology;
namespace fair = drop7::fair_only_horizon;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kGenerationSeedStart = 0x3d66'0000u;
constexpr int kGenerationGames = 64;
constexpr int kMaximumMoves = 500;
constexpr int kFirstSampleMove = 50;
constexpr int kOracleDepth = 4;
constexpr int kOracleBeam = 128;
constexpr int kMaximumCurriculumStates = 4'096;
constexpr int kRestartScenarios = 7;
constexpr int kRestartHorizon = 25;
constexpr int kValidationHorizon = 8;
constexpr int kEventsPerStep = 64;
constexpr int kDefaultThreads = 8;
constexpr double kWallLimitSeconds = 30.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr std::uint64_t kRestartSeedDomain = 0x4355'5252'5345'4544ull;
constexpr std::uint32_t kRestartRevealDomain = 0x4352'5256u;  // "CRRV"
constexpr std::uint32_t kRestartVisibleDomain = 0x4352'5653u; // "CRVS"

static_assert(kLevelBonus == 17'000);
static_assert(kMovesPerLevel == 5);
static_assert(kGenerationGames == 64);
static_assert(kGenerationSeedStart + kGenerationGames == 0x3d66'0040u);
static_assert(kMaximumMoves == 500 && kFirstSampleMove == 50);
static_assert(kOracleDepth == oracle::kDefaultOracleDepth);
static_assert(kOracleBeam == oracle::kDefaultOracleBeam);
static_assert(kRestartScenarios == kBoardSize);
static_assert(kEventsPerStep > kCellCount);
static_assert((kGenerationSeedStart >> 24u) == 0x3du);
static_assert((kGenerationSeedStart >> 24u) != 0x7du &&
              (kGenerationSeedStart >> 24u) != 0xd7u);

std::mutex curriculum_progress_mutex;

struct Options {
  std::string output = "/tmp/drop7-oracle-curriculum.json";
  std::string states = "/tmp/drop7-oracle-curriculum-states.jsonl";
  int threads = kDefaultThreads;
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string argument = argv[index];
    if (argument == "--output") {
      result.output = argv[index + 1];
    } else if (argument == "--states") {
      result.states = argv[index + 1];
    } else if (argument == "--threads") {
      result.threads = std::stoi(argv[index + 1]);
      if (result.threads < 1 || result.threads > 16) {
        throw std::invalid_argument("threads must be in [1,16]");
      }
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  return result;
}

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
    throw std::invalid_argument("curriculum source is not restartable");
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

std::string publicKey(const PublicState& source) {
  std::string key;
  key.reserve(kCellCount + 2);
  for (const std::uint8_t cell : source.board) {
    key.push_back(static_cast<char>(cell));
  }
  key.push_back(static_cast<char>(source.next_disc));
  key.push_back(static_cast<char>(source.moves_remaining));
  return key;
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
    throw std::runtime_error("oracle curriculum exceeded 256 MiB RSS cap");
  }
}

struct Deadline {
  Clock::time_point started = Clock::now();

  double elapsedSeconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }

  void check() const {
    if (elapsedSeconds() > kWallLimitSeconds) {
      throw std::runtime_error("oracle curriculum exceeded 30 minute cap");
    }
  }
};

bool allowedGenerationSeed(std::uint32_t seed) {
  return seed >= kGenerationSeedStart &&
         seed < kGenerationSeedStart + kGenerationGames &&
         (seed >> 24u) != 0x7du && (seed >> 24u) != 0xd7u;
}

void requireGenerationSeed(std::uint32_t seed) {
  if (!allowedGenerationSeed(seed)) {
    throw std::invalid_argument("generation seed outside exact 3d66 allowlist");
  }
}

struct TrajectoryResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t oracle_generated = 0;
  std::uint64_t oracle_deduplicated = 0;
  std::size_t peak_candidates = 0;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
  std::uint64_t raw_samples = 0;
  double seconds = 0.0;
  std::vector<PublicState> samples;
};

TrajectoryResult generateTrajectory(std::uint32_t seed,
                                    const Deadline& deadline) {
  requireGenerationSeed(seed);
  const auto started = Clock::now();
  State state = initialHeadlessState(seed);
  TrajectoryResult result;
  result.seed = seed;
  result.samples.reserve(kMaximumMoves - kFirstSampleMove + 1);
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    deadline.check();
    if (state.next_disc != headlessDisc(seed, state.moves_played)) {
      throw std::runtime_error("oracle trajectory disc-stream guard failed");
    }
    const oracle::OraclePlan plan =
        oracle::planOracleMove(state, seed, kOracleDepth, kOracleBeam);
    if (!isLegal(state.board, plan.column)) {
      throw std::runtime_error("audited oracle selected illegal action");
    }
    result.oracle_generated += plan.stats.generated;
    result.oracle_deduplicated += plan.stats.deduplicated;
    result.peak_candidates =
        std::max(result.peak_candidates, plan.stats.peak_candidates);
    MoveResult move;
    if (!playHeadlessMove(state, seed, plan.column, move)) {
      throw std::runtime_error("oracle trajectory transition failed");
    }
    for (const Wave& wave : move.waves) {
      result.clears += static_cast<std::uint64_t>(wave.cleared);
      result.reveals += static_cast<std::uint64_t>(wave.revealed);
    }
    if (!state.game_over && state.moves_played >= kFirstSampleMove) {
      bool ignored = false;
      const PublicState canonical =
          canonicalPublic(stripToPublic(state), ignored);
      result.samples.push_back(canonical);
      ++result.raw_samples;
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  enforceRssLimit();
  return result;
}

std::vector<TrajectoryResult> generateTrajectories(int threads,
                                                   const Deadline& deadline) {
  std::vector<TrajectoryResult> result(kGenerationGames);
  std::atomic<int> next{0};
  std::vector<std::future<void>> workers;
  const int worker_count = std::max(1, std::min(threads, kGenerationGames));
  workers.reserve(static_cast<std::size_t>(worker_count));
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [&]() {
      for (;;) {
        const int game = next.fetch_add(1);
        if (game >= kGenerationGames) return;
        const std::uint32_t seed =
            kGenerationSeedStart + static_cast<std::uint32_t>(game);
        result[game] = generateTrajectory(seed, deadline);
        const std::lock_guard<std::mutex> lock(curriculum_progress_mutex);
        std::cerr << "oracle-curriculum " << game + 1 << '/'
                  << kGenerationGames << " seed 0x" << std::hex << seed
                  << std::dec << " score " << result[game].score << " moves "
                  << result[game].moves << " samples "
                  << result[game].raw_samples << '\n';
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

struct SelectedStates {
  std::vector<PublicState> states;
  std::uint64_t raw_samples = 0;
  std::uint64_t duplicate_samples = 0;
  std::uint64_t unique_before_cap = 0;
  std::uint64_t hash_collisions = 0;
};

SelectedStates selectStates(const std::vector<TrajectoryResult>& trajectories) {
  std::unordered_map<std::string, PublicState> unique;
  std::unordered_map<std::uint64_t, std::string> hashes;
  SelectedStates result;
  for (const TrajectoryResult& trajectory : trajectories) {
    result.raw_samples += trajectory.raw_samples;
    for (const PublicState& state : trajectory.samples) {
      const std::string key = publicKey(state);
      const auto [entry, inserted] = unique.emplace(key, state);
      static_cast<void>(entry);
      if (!inserted) {
        ++result.duplicate_samples;
        continue;
      }
      const std::uint64_t hash = publicHash(state);
      const auto found = hashes.find(hash);
      if (found != hashes.end() && found->second != key) {
        ++result.hash_collisions;
      } else {
        hashes.emplace(hash, key);
      }
    }
  }
  result.unique_before_cap = unique.size();
  result.states.reserve(unique.size());
  for (auto& [key, state] : unique) {
    static_cast<void>(key);
    result.states.push_back(std::move(state));
  }
  std::sort(result.states.begin(), result.states.end(),
            [](const PublicState& first, const PublicState& second) {
              const std::uint64_t first_hash = publicHash(first);
              const std::uint64_t second_hash = publicHash(second);
              if (first_hash != second_hash) return first_hash < second_hash;
              return publicKey(first) < publicKey(second);
            });
  if (result.states.size() > kMaximumCurriculumStates) {
    result.states.resize(kMaximumCurriculumStates);
  }
  return result;
}

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
        root_seed, scenario, kRestartScenarios, kRestartRevealDomain,
        event_index);
    return static_cast<std::uint8_t>(
        std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
  }
};

std::uint8_t restartVisibleDisc(std::uint32_t root_seed, int scenario,
                                int step) {
  const double unit = cfpi::detail::stratifiedUnit(
      root_seed, scenario, kRestartScenarios, kRestartVisibleDomain, step);
  return static_cast<std::uint8_t>(
      std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
}

bool playRestartMove(const PublicState& source, int action,
                     std::uint32_t root_seed, int scenario, int step,
                     MoveResult& result) {
  if (scenario < 0 || scenario >= kRestartScenarios || step < 0 ||
      step >= kRestartHorizon || !isLegal(source.board, action)) {
    return false;
  }
  State state = materialize(source);
  RestartRandom random{root_seed, scenario, step, 0};
  if (!cfpi::detail::playMoveSampled(state, action, random, result)) {
    return false;
  }
  result.state.score = 0;
  result.state.level = 1;
  result.state.moves_played = 0;
  if (!result.state.game_over) {
    result.state.next_disc =
        restartVisibleDisc(root_seed, scenario, step);
  }
  return true;
}

struct D1Decision {
  int action = -1;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  bool complete = false;
};

D1Decision chooseFairD1(const PublicState& source) {
  bool mirrored = false;
  const State canonical =
      cfpi::detail::canonicalState(materialize(source), mirrored);
  fair::SearchContext context;
  const fair::RootEvaluation root = fair::rootDecision(canonical, 1, context);
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
  return {mirrored ? kBoardSize - 1 - root.action : root.action,
          context.work, context.nodes, true};
}

using RestartPolicy = D1Decision (*)(const PublicState&);
static_assert(std::is_same_v<decltype(&chooseFairD1), RestartPolicy>);
static_assert(!std::is_invocable_v<RestartPolicy, const State&>);

struct RestartOutcome {
  int moves = 0;
  std::int64_t score_delta = 0;
  int clears = 0;
  int reveals = 0;
  int maximum_chain = 0;
  bool survived_horizon = false;
  std::uint64_t d1_work = 0;
  std::uint64_t d1_nodes = 0;

  bool operator==(const RestartOutcome&) const = default;
};

RestartOutcome runRestart(const PublicState& root, int scenario, int horizon,
                          const Deadline* deadline = nullptr) {
  if (scenario < 0 || scenario >= kRestartScenarios || horizon < 1 ||
      horizon > kRestartHorizon) {
    throw std::invalid_argument("invalid independent restart request");
  }
  PublicState state = root;
  const std::uint32_t root_seed = restartSeed(root);
  RestartOutcome result;
  for (int step = 0; step < horizon; ++step) {
    if (deadline != nullptr) deadline->check();
    const D1Decision decision = chooseFairD1(state);
    if (!decision.complete || !isLegal(state.board, decision.action)) {
      throw std::runtime_error("restart D1 chose illegal action");
    }
    result.d1_work += decision.work;
    result.d1_nodes += decision.nodes;
    MoveResult move;
    if (!playRestartMove(state, decision.action, root_seed, scenario, step,
                         move)) {
      throw std::runtime_error("independent restart transition failed");
    }
    ++result.moves;
    result.score_delta += move.score_delta;
    for (const Wave& wave : move.waves) {
      result.clears += wave.cleared;
      result.reveals += wave.revealed;
      result.maximum_chain = std::max(result.maximum_chain, wave.depth);
    }
    if (move.state.game_over) return result;
    state = stripToPublic(move.state);
  }
  result.survived_horizon = true;
  return result;
}

struct RecoveryDiagnostics {
  double mean_moves = 0.0;
  double survival_rate = 0.0;
  double mean_score_delta = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double mean_maximum_chain = 0.0;
  int survived = 0;
  int terminal = 0;
  std::uint64_t d1_work = 0;
  std::uint64_t d1_nodes = 0;
  std::array<RestartOutcome, kRestartScenarios> scenarios{};

  bool operator==(const RecoveryDiagnostics&) const = default;
};

RecoveryDiagnostics diagnoseRecovery(const PublicState& state,
                                       const Deadline* deadline = nullptr) {
  RecoveryDiagnostics result;
  int total_moves = 0;
  int total_clears = 0;
  int total_reveals = 0;
  for (int scenario = 0; scenario < kRestartScenarios; ++scenario) {
    RestartOutcome& outcome = result.scenarios[scenario];
    outcome = runRestart(state, scenario, kRestartHorizon, deadline);
    result.mean_moves +=
        static_cast<double>(outcome.moves) / kRestartScenarios;
    result.mean_score_delta +=
        static_cast<double>(outcome.score_delta) / kRestartScenarios;
    result.mean_maximum_chain +=
        static_cast<double>(outcome.maximum_chain) / kRestartScenarios;
    result.survived += outcome.survived_horizon;
    result.terminal += !outcome.survived_horizon;
    result.d1_work += outcome.d1_work;
    result.d1_nodes += outcome.d1_nodes;
    total_moves += outcome.moves;
    total_clears += outcome.clears;
    total_reveals += outcome.reveals;
  }
  result.survival_rate =
      static_cast<double>(result.survived) / kRestartScenarios;
  if (total_moves > 0) {
    result.clears_per_move =
        static_cast<double>(total_clears) / total_moves;
    result.reveals_per_move =
        static_cast<double>(total_reveals) / total_moves;
  }
  return result;
}

enum class FlowBand { kBlocked, kClosed, kRecovering, kFlowing };

FlowBand flowBand(const RecoveryDiagnostics& recovery) {
  if (recovery.survival_rate < 0.25) return FlowBand::kBlocked;
  if (recovery.reveals_per_move < 0.25) return FlowBand::kClosed;
  if (recovery.reveals_per_move < 0.60) return FlowBand::kRecovering;
  return FlowBand::kFlowing;
}

std::string_view flowBandName(FlowBand band) {
  switch (band) {
    case FlowBand::kBlocked:
      return "blocked";
    case FlowBand::kClosed:
      return "closed";
    case FlowBand::kRecovering:
      return "recovering";
    case FlowBand::kFlowing:
      return "flowing";
  }
  throw std::logic_error("unknown flow band");
}

struct StateShape {
  int occupancy = 0;
  int maximum_height = 0;
  int covers = 0;
  int legal_columns = 0;
};

StateShape stateShape(const PublicState& state) {
  StateShape result;
  const auto heights = cfpi::detail::columnHeights(state.board);
  for (int column = 0; column < kBoardSize; ++column) {
    result.occupancy += heights[column];
    result.maximum_height = std::max(result.maximum_height, heights[column]);
    result.legal_columns += heights[column] < kBoardSize;
  }
  for (const std::uint8_t cell : state.board) {
    result.covers += cell == kSolid || cell == kCracked;
  }
  return result;
}

struct CurriculumRecord {
  PublicState state{};
  StateShape shape{};
  RecoveryDiagnostics recovery{};
  FlowBand flow = FlowBand::kBlocked;
  std::uint64_t public_hash = 0;
  bool restart_independent = false;
};

bool validateRestartIndependence(const PublicState& state,
                                 const Deadline* deadline = nullptr) {
  State first_origin = materialize(state);
  first_origin.score = 9'876'543;
  first_origin.level = 91;
  first_origin.moves_played = 417;
  State second_origin = materialize(state);
  second_origin.score = 123;
  second_origin.level = 2;
  second_origin.moves_played = 1;
  const PublicState first_export = stripToPublic(first_origin);
  const PublicState second_export = stripToPublic(second_origin);
  if (first_export != second_export || restartSeed(first_export) !=
                                           restartSeed(second_export)) {
    return false;
  }
  const RestartOutcome first =
      runRestart(first_export, 0, kValidationHorizon, deadline);
  const RestartOutcome second =
      runRestart(second_export, 0, kValidationHorizon, deadline);
  return first == second;
}

std::vector<CurriculumRecord> diagnoseStates(
    const std::vector<PublicState>& states, int threads,
    const Deadline& deadline) {
  std::vector<CurriculumRecord> result(states.size());
  std::atomic<std::size_t> next{0};
  std::vector<std::future<void>> workers;
  const int worker_count =
      std::max(1, std::min(threads, static_cast<int>(states.size())));
  workers.reserve(static_cast<std::size_t>(worker_count));
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [&]() {
      for (;;) {
        const std::size_t index = next.fetch_add(1);
        if (index >= states.size()) return;
        CurriculumRecord record;
        record.state = states[index];
        record.public_hash = publicHash(record.state);
        record.shape = stateShape(record.state);
        record.recovery = diagnoseRecovery(record.state, &deadline);
        record.flow = flowBand(record.recovery);
        record.restart_independent =
            validateRestartIndependence(record.state, &deadline);
        if (!record.restart_independent) {
          throw std::runtime_error("source metadata changed restart outcome");
        }
        result[index] = std::move(record);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  enforceRssLimit();
  return result;
}

std::uint64_t streamFingerprint(const PublicState& state, int scenario) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  const std::uint32_t root_seed = restartSeed(state);
  for (int step = 0; step < kRestartHorizon; ++step) {
    hash ^= restartVisibleDisc(root_seed, scenario, step);
    hash *= 0x0000'0100'0000'01b3ull;
    RestartRandom random{root_seed, scenario, step, 0};
    hash ^= random.nextDisc();
    hash *= 0x0000'0100'0000'01b3ull;
  }
  return mix64(hash);
}

std::uint64_t datasetFingerprint(
    const std::vector<CurriculumRecord>& records) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const CurriculumRecord& record : records) {
    const std::string key = publicKey(record.state);
    for (const unsigned char byte : key) {
      hash ^= byte;
      hash *= 0x0000'0100'0000'01b3ull;
    }
    const std::uint64_t fields[] = {
        std::bit_cast<std::uint64_t>(record.recovery.mean_moves),
        std::bit_cast<std::uint64_t>(record.recovery.mean_score_delta),
        std::bit_cast<std::uint64_t>(record.recovery.reveals_per_move),
        static_cast<std::uint64_t>(record.recovery.survived),
    };
    for (const std::uint64_t field : fields) {
      hash ^= field;
      hash *= 0x0000'0100'0000'01b3ull;
    }
  }
  return mix64(hash);
}

std::string hex64(std::uint64_t value) {
  std::ostringstream output;
  output << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
  return output.str();
}

std::string jsonEscape(std::string_view source) {
  std::string result;
  for (const char character : source) {
    if (character == '\\' || character == '"') result.push_back('\\');
    result.push_back(character);
  }
  return result;
}

void writeRestartOutcome(std::ostream& output,
                         const RestartOutcome& outcome) {
  output << "{\"moves\":" << outcome.moves
         << ",\"scoreDelta\":" << outcome.score_delta
         << ",\"clears\":" << outcome.clears
         << ",\"reveals\":" << outcome.reveals
         << ",\"maximumChain\":" << outcome.maximum_chain
         << ",\"survivedHorizon\":"
         << (outcome.survived_horizon ? "true" : "false") << '}';
}

void writeStateLine(std::ostream& output, const CurriculumRecord& record) {
  output << std::setprecision(12)
         << "{\"format\":\"drop7-public-restart-v1\",\"state\":{"
         << "\"board\":\"" << serializeBoard(record.state.board)
         << "\",\"nextDisc\":" << static_cast<int>(record.state.next_disc)
         << ",\"movesRemaining\":"
         << static_cast<int>(record.state.moves_remaining)
         << "},\"publicHash\":\"" << hex64(record.public_hash)
         << "\",\"shape\":{\"occupancy\":" << record.shape.occupancy
         << ",\"maximumHeight\":" << record.shape.maximum_height
         << ",\"covers\":" << record.shape.covers
         << ",\"legalColumns\":" << record.shape.legal_columns
         << "},\"d1Recovery\":{\"streams\":" << kRestartScenarios
         << ",\"horizon\":" << kRestartHorizon
         << ",\"meanMoves\":" << record.recovery.mean_moves
         << ",\"survivalRate\":" << record.recovery.survival_rate
         << ",\"meanScoreDelta\":" << record.recovery.mean_score_delta
         << ",\"clearsPerMove\":" << record.recovery.clears_per_move
         << ",\"revealsPerMove\":" << record.recovery.reveals_per_move
         << ",\"meanMaximumChain\":"
         << record.recovery.mean_maximum_chain << ",\"flowBand\":\""
         << flowBandName(record.flow) << "\",\"scenarios\":[";
  for (int scenario = 0; scenario < kRestartScenarios; ++scenario) {
    if (scenario != 0) output << ',';
    writeRestartOutcome(output, record.recovery.scenarios[scenario]);
  }
  output << "]},\"independentRestartValidated\":"
         << (record.restart_independent ? "true" : "false") << "}\n";
}

void writeStates(const std::string& path,
                 const std::vector<CurriculumRecord>& records) {
  std::ofstream output(path, std::ios::trunc);
  if (!output) throw std::runtime_error("could not open curriculum JSONL");
  for (const CurriculumRecord& record : records) writeStateLine(output, record);
  output.close();
  if (!output) throw std::runtime_error("could not finish curriculum JSONL");
}

struct Distribution {
  std::array<std::uint64_t, kMovesPerLevel + 1> phase{};
  std::array<std::uint64_t, kBoardSize + 1> maximum_height{};
  std::array<std::uint64_t, kCellCount + 1> covers{};
  std::array<std::uint64_t, kCellCount + 1> occupancy{};
  std::array<std::uint64_t, kBoardSize + 1> legal_columns{};
  std::array<std::uint64_t, 4> flow{};
  std::array<std::uint64_t, 6> recovery_moves{};
  double mean_covers = 0.0;
  double mean_height = 0.0;
  double mean_recovery_moves = 0.0;
  double mean_survival_rate = 0.0;
  double mean_recovery_score = 0.0;
  double mean_reveals_per_move = 0.0;
  std::uint64_t d1_work = 0;
  std::uint64_t d1_nodes = 0;
  std::uint64_t independence_validated = 0;
};

Distribution distribution(const std::vector<CurriculumRecord>& records) {
  Distribution result;
  if (records.empty()) return result;
  for (const CurriculumRecord& record : records) {
    ++result.phase[record.state.moves_remaining];
    ++result.maximum_height[record.shape.maximum_height];
    ++result.covers[record.shape.covers];
    ++result.occupancy[record.shape.occupancy];
    ++result.legal_columns[record.shape.legal_columns];
    ++result.flow[static_cast<int>(record.flow)];
    const int recovery_bin =
        std::min(5, static_cast<int>(record.recovery.mean_moves / 5.0));
    ++result.recovery_moves[recovery_bin];
    result.mean_covers +=
        static_cast<double>(record.shape.covers) / records.size();
    result.mean_height +=
        static_cast<double>(record.shape.maximum_height) / records.size();
    result.mean_recovery_moves +=
        record.recovery.mean_moves / records.size();
    result.mean_survival_rate +=
        record.recovery.survival_rate / records.size();
    result.mean_recovery_score +=
        record.recovery.mean_score_delta / records.size();
    result.mean_reveals_per_move +=
        record.recovery.reveals_per_move / records.size();
    result.d1_work += record.recovery.d1_work;
    result.d1_nodes += record.recovery.d1_nodes;
    result.independence_validated += record.restart_independent;
  }
  return result;
}

template <std::size_t Size>
void writeHistogram(std::ostream& output,
                    const std::array<std::uint64_t, Size>& values,
                    int begin = 0) {
  output << '{';
  bool first = true;
  for (int index = begin; index < static_cast<int>(Size); ++index) {
    if (values[index] == 0) continue;
    if (!first) output << ',';
    first = false;
    output << '\"' << index << "\":" << values[index];
  }
  output << '}';
}

void writeArtifact(const Options& options,
                   const std::vector<TrajectoryResult>& trajectories,
                   const SelectedStates& selected,
                   const std::vector<CurriculumRecord>& records,
                   const Distribution& stats, double generation_seconds,
                   double recovery_seconds, double total_seconds) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open curriculum artifact");
  std::uint64_t total_generated = 0;
  std::uint64_t total_deduplicated = 0;
  std::uint64_t total_moves = 0;
  int natural = 0;
  int censored = 0;
  for (const TrajectoryResult& trajectory : trajectories) {
    total_generated += trajectory.oracle_generated;
    total_deduplicated += trajectory.oracle_deduplicated;
    total_moves += static_cast<std::uint64_t>(trajectory.moves);
    natural += !trajectory.censored;
    censored += trajectory.censored;
  }
  output << std::setprecision(12)
         << "{\n  \"format\":\"drop7-oracle-curriculum-v1\",\n"
         << "  \"purpose\":\"public-only restart curriculum prerequisite;"
            " no policy training or gameplay claim\",\n"
         << "  \"scoring\":{\"levelBonus\":" << kLevelBonus << "},\n"
         << "  \"generation\":{\"privilegedOracle\":true,"
            "\"implementation\":\"audited oracle_topology::planOracleMove\","
            "\"games\":" << kGenerationGames
         << ",\"maximumMoves\":" << kMaximumMoves
         << ",\"sampleAtOrAfterMove\":" << kFirstSampleMove
         << ",\"oracleDepth\":" << kOracleDepth
         << ",\"oracleBeam\":" << kOracleBeam
         << ",\"natural\":" << natural << ",\"censored\":" << censored
         << ",\"meanMoves\":"
         << static_cast<double>(total_moves) / trajectories.size()
         << ",\"generatedNodes\":" << total_generated
         << ",\"deduplicatedNodes\":" << total_deduplicated
         << ",\"seconds\":" << generation_seconds
         << "},\n  \"privilegeBoundary\":{"
            "\"exportedFields\":[\"board\",\"nextDisc\","
            "\"movesRemaining\"],"
            "\"strippedFields\":[\"sourceSeed\",\"futureTape\","
            "\"score\",\"level\",\"moveIndex\",\"history\"],"
            "\"canonicalReflection\":true,"
            "\"deduplicateKey\":\"board+nextDisc+movesRemaining\","
            "\"selectionAfterDedup\":\"lowest deterministic public hashes\","
            "\"sourcePrivilegeSerialized\":false},\n"
         << "  \"selection\":{\"rawSamples\":" << selected.raw_samples
         << ",\"duplicateSamples\":" << selected.duplicate_samples
         << ",\"uniqueBeforeCap\":" << selected.unique_before_cap
         << ",\"maximumStates\":" << kMaximumCurriculumStates
         << ",\"exportedStates\":" << records.size()
         << ",\"publicHashCollisions\":" << selected.hash_collisions
         << ",\"datasetFingerprint\":\""
         << hex64(datasetFingerprint(records)) << "\"},\n"
         << "  \"independentRestarts\":{\"policy\":\""
            "fresh exact public fair-D1/five-stratum\","
            "\"streamsPerState\":" << kRestartScenarios
         << ",\"horizon\":" << kRestartHorizon
         << ",\"eventIndexed\":true,"
            "\"restartTapeDerivedOnlyFromPublicState\":true,"
            "\"originSeedOrTapeAcceptedByRestartAPI\":false,"
            "\"poisonedSourceMetadataValidationHorizon\":"
         << kValidationHorizon << ",\"validatedStates\":"
         << stats.independence_validated << ",\"mismatches\":"
         << (records.size() - stats.independence_validated)
         << ",\"seconds\":" << recovery_seconds
         << ",\"d1Work\":" << stats.d1_work
         << ",\"d1Nodes\":" << stats.d1_nodes << "},\n"
         << "  \"distribution\":{\"phase\":";
  writeHistogram(output, stats.phase, 1);
  output << ",\"maximumHeight\":";
  writeHistogram(output, stats.maximum_height);
  output << ",\"covers\":";
  writeHistogram(output, stats.covers);
  output << ",\"occupancy\":";
  writeHistogram(output, stats.occupancy);
  output << ",\"legalColumns\":";
  writeHistogram(output, stats.legal_columns);
  output << ",\"flowBands\":{\"blocked\":" << stats.flow[0]
         << ",\"closed\":" << stats.flow[1]
         << ",\"recovering\":" << stats.flow[2]
         << ",\"flowing\":" << stats.flow[3]
         << "},\"flowDefinition\":{"
            "\"blocked\":\"survivalRate<0.25\","
            "\"closed\":\"survivalRate>=0.25 and revealsPerMove<0.25\","
            "\"recovering\":\"revealsPerMove in [0.25,0.60)\","
            "\"flowing\":\"revealsPerMove>=0.60\"},"
            "\"recoveryMeanMovesBins\":{\"0-4\":"
         << stats.recovery_moves[0] << ",\"5-9\":"
         << stats.recovery_moves[1] << ",\"10-14\":"
         << stats.recovery_moves[2] << ",\"15-19\":"
         << stats.recovery_moves[3] << ",\"20-24\":"
         << stats.recovery_moves[4] << ",\"25\":"
         << stats.recovery_moves[5] << "},\"means\":{\"covers\":"
         << stats.mean_covers << ",\"maximumHeight\":"
         << stats.mean_height << ",\"recoveryMoves\":"
         << stats.mean_recovery_moves << ",\"survivalRate\":"
         << stats.mean_survival_rate << ",\"recoveryScoreDelta\":"
         << stats.mean_recovery_score << ",\"revealsPerMove\":"
         << stats.mean_reveals_per_move << "}},\n"
         << "  \"statesFile\":\"" << jsonEscape(options.states)
         << "\",\n  \"policyTrainingPerformed\":false,\n"
         << "  \"gameplayClaim\":false,\n"
         << "  \"resourceCaps\":{\"wallSeconds\":" << kWallLimitSeconds
         << ",\"rssBytes\":" << kRssLimitBytes << "},\n"
         << "  \"totalSeconds\":" << total_seconds
         << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
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
  state.board[indexOf(6, 1)] = 4;
  state.board[indexOf(6, 2)] = 2;
  state.board[indexOf(5, 2)] = kCracked;
  state.board[indexOf(6, 4)] = 6;
  state.next_disc = 3;
  state.moves_remaining = 3;
  return state;
}

Board parseBoard(std::string_view serialized) {
  if (serialized.size() != kCellCount) {
    throw std::invalid_argument("serialized board length mismatch");
  }
  Board board{};
  for (int index = 0; index < kCellCount; ++index) {
    if (serialized[index] < '0' || serialized[index] > '9') {
      throw std::invalid_argument("serialized board token mismatch");
    }
    board[index] = static_cast<std::uint8_t>(serialized[index] - '0');
  }
  return board;
}

bool selfTest(std::ostream& output) {
  expect(kLevelBonus == 17'000, "corrected scoring regression");
  const PublicState fixture = fixtureState();
  bool mirrored = false;
  const PublicState canonical = canonicalPublic(fixture, mirrored);
  bool reflected_mirrored = false;
  const PublicState reflected_canonical =
      canonicalPublic(mirror(fixture), reflected_mirrored);
  expect(canonical == reflected_canonical &&
             publicHash(fixture) == publicHash(mirror(fixture)),
         "curriculum canonical reflection failed");
  expect(parseBoard(serializeBoard(canonical.board)) == canonical.board,
         "exact public board serialization failed");

  State metadata = materialize(fixture);
  metadata.score = 9'876'543;
  metadata.level = 91;
  metadata.moves_played = 417;
  expect(stripToPublic(metadata) == fixture &&
             restartSeed(stripToPublic(metadata)) == restartSeed(fixture),
         "source metadata crossed privilege boundary");

  const D1Decision d1 = chooseFairD1(fixture);
  const D1Decision reflected_d1 = chooseFairD1(mirror(fixture));
  expect(d1.complete && isLegal(fixture.board, d1.action) &&
             reflected_d1.action == kBoardSize - 1 - d1.action,
         "public fair-D1 restart policy failed");
  const RestartOutcome restart_first = runRestart(fixture, 0, 3);
  const RestartOutcome restart_repeat = runRestart(fixture, 0, 3);
  expect(restart_first == restart_repeat &&
             validateRestartIndependence(fixture),
         "independent restart determinism/source blindness failed");
  std::array<std::uint64_t, kRestartScenarios> fingerprints{};
  for (int scenario = 0; scenario < kRestartScenarios; ++scenario) {
    fingerprints[scenario] = streamFingerprint(fixture, scenario);
  }
  std::sort(fingerprints.begin(), fingerprints.end());
  expect(std::adjacent_find(fingerprints.begin(), fingerprints.end()) ==
             fingerprints.end(),
         "independent restart streams were not distinct");

  const std::uint32_t root_seed = restartSeed(fixture);
  for (const std::uint32_t domain : {kRestartRevealDomain,
                                     kRestartVisibleDomain}) {
    for (int event : {0, 1, 63, 64, 511}) {
      std::array<int, kRestartScenarios> strata{};
      for (int scenario = 0; scenario < kRestartScenarios; ++scenario) {
        const double unit = cfpi::detail::stratifiedUnit(
            root_seed, scenario, kRestartScenarios, domain, event);
        const int stratum =
            static_cast<int>(std::floor(unit * kRestartScenarios));
        expect(stratum >= 0 && stratum < kRestartScenarios,
               "restart stratum out of range");
        ++strata[stratum];
      }
      for (const int count : strata) {
        expect(count == 1, "restart event was not exactly stratified");
      }
    }
  }

  State oracle_fixture = initialHeadlessState(0x1266'0000u);
  const oracle::OraclePlan oracle_first =
      oracle::planOracleMove(oracle_fixture, 0x1266'0000u, 2, 16);
  const oracle::OraclePlan oracle_repeat =
      oracle::planOracleMove(oracle_fixture, 0x1266'0000u, 2, 16);
  expect(oracle_first.column == oracle_repeat.column &&
             oracle_first.stats.generated == oracle_repeat.stats.generated &&
             isLegal(oracle_fixture.board, oracle_first.column),
         "audited privileged oracle reuse failed");

  PublicState changed_next = fixture;
  changed_next.next_disc = 4;
  PublicState changed_phase = fixture;
  changed_phase.moves_remaining = 4;
  expect(publicKey(fixture) != publicKey(changed_next) &&
             publicKey(fixture) != publicKey(changed_phase),
         "deduplication key omitted visible disc or rise phase");
  expect(allowedGenerationSeed(kGenerationSeedStart) &&
             allowedGenerationSeed(kGenerationSeedStart +
                                   kGenerationGames - 1),
         "authorized generation seed rejected");
  expect(throwsInvalid([] {
           requireGenerationSeed(0x3d65'ffffu);
         }) &&
             throwsInvalid([] {
               requireGenerationSeed(0x3d66'0040u);
             }) &&
             throwsInvalid([] {
               requireGenerationSeed(0x3d67'0000u);
             }) &&
             throwsInvalid([] {
               requireGenerationSeed(0x7d66'0000u);
             }) &&
             throwsInvalid([] {
               requireGenerationSeed(0xd766'0000u);
             }),
         "generation seed guard failed");
  enforceRssLimit();
  output << std::setprecision(12)
         << "ORACLE_CURRICULUM_SELF_TEST {\"passed\":true,"
         << "\"levelBonus\":" << kLevelBonus
         << ",\"auditedOracleReused\":true,"
         << "\"canonicalReflection\":true,"
         << "\"exactSerialization\":true,"
         << "\"publicRestartOnly\":true,"
         << "\"sourceMetadataBlind\":true,"
         << "\"independentStreams\":true,"
         << "\"exactEventStratification\":true,"
         << "\"fairD1Recoverability\":true,"
         << "\"seedGuards\":true,\"peakRssBytes\":" << peakRssBytes()
         << "}\n";
  return true;
}

int run(const Options& options, std::ostream& output) {
  const Deadline deadline;
  const auto generation_started = Clock::now();
  const std::vector<TrajectoryResult> trajectories =
      generateTrajectories(options.threads, deadline);
  const double generation_seconds =
      std::chrono::duration<double>(Clock::now() - generation_started).count();
  SelectedStates selected = selectStates(trajectories);
  if (selected.states.empty()) {
    throw std::runtime_error("oracle trajectories yielded no move-50 restarts");
  }
  const auto recovery_started = Clock::now();
  const std::vector<CurriculumRecord> records =
      diagnoseStates(selected.states, options.threads, deadline);
  const double recovery_seconds =
      std::chrono::duration<double>(Clock::now() - recovery_started).count();
  const Distribution stats = distribution(records);
  if (stats.independence_validated != records.size()) {
    throw std::runtime_error("independent restart validation was incomplete");
  }
  writeStates(options.states, records);
  deadline.check();
  enforceRssLimit();
  const double total_seconds = deadline.elapsedSeconds();
  writeArtifact(options, trajectories, selected, records, stats,
                generation_seconds, recovery_seconds, total_seconds);
  output << std::fixed << std::setprecision(3)
         << "ORACLE_CURRICULUM_RESULT {\"generationGames\":"
         << kGenerationGames << ",\"rawSamples\":" << selected.raw_samples
         << ",\"uniqueBeforeCap\":" << selected.unique_before_cap
         << ",\"exportedStates\":" << records.size()
         << ",\"independenceValidated\":"
         << stats.independence_validated << ",\"meanRecoveryMoves\":"
         << stats.mean_recovery_moves << ",\"meanSurvivalRate\":"
         << stats.mean_survival_rate << ",\"meanRecoveryScore\":"
         << stats.mean_recovery_score << ",\"meanRevealsPerMove\":"
         << stats.mean_reveals_per_move << ",\"datasetFingerprint\":\""
         << hex64(datasetFingerprint(records))
         << "\",\"generationSeconds\":" << generation_seconds
         << ",\"recoverySeconds\":" << recovery_seconds
         << ",\"totalSeconds\":" << total_seconds
         << ",\"peakRssBytes\":" << peakRssBytes() << ",\"artifact\":\""
         << jsonEscape(options.output) << "\",\"states\":\""
         << jsonEscape(options.states) << "\",\"policyTraining\":false,"
         << "\"gameplayClaim\":false}\n";
  return 0;
}

}  // namespace drop7::oracle_curriculum

#ifndef DROP7_ORACLE_CURRICULUM_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::oracle_curriculum::selfTest(std::cout) ? EXIT_SUCCESS
                                                           : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::oracle_curriculum::parseOptions(argc, argv, 2);
      return drop7::oracle_curriculum::run(options, std::cout);
    }
    std::cerr << "usage: drop7_oracle_curriculum --self-test | --run "
                 "[--output PATH] [--states PATH] [--threads N]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_oracle_curriculum: " << error.what() << '\n';
    return 1;
  }
}
#endif
