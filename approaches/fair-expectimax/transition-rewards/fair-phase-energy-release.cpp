#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#include "../reference/fair-only-depth4.cpp"
#undef DROP7_FAIR_ONLY_DEPTH4_LIBRARY

#include <atomic>
#include <future>
#include <optional>
#include <sstream>

// Adds separate terms for stored potential near a covered-row rise and for
// releasing numbered discs.  Search topology, chance sampling, terminal
// utility, iterative deepening, and cache limits match the reference
// full-width D4 implementation.
namespace drop7::fair_phase_energy_release {

namespace stock = fair_only_depth4;
namespace frozen = fair_only_horizon;
using Clock = std::chrono::steady_clock;

constexpr int kDepth = stock::kCandidateDepth;
constexpr int kChanceSamples = stock::kChanceSamples;
constexpr std::uint64_t kMaximumWork = stock::kMaximumWork;
constexpr std::size_t kMaximumCacheEntries = stock::kMaximumCacheEntries;
constexpr std::uint64_t kWorstCaseWork = stock::kWorstCaseD4Work;
constexpr std::uint64_t kWorstCaseCacheEntries =
    stock::kWorstCaseD4CacheEntries;
constexpr int kMaximumMoves = 1'000;
constexpr int kDefaultThreads = 4;
constexpr std::uint32_t kFittingStart = 0x3de5'0000u;
constexpr int kFittingGames = 4;
constexpr std::uint32_t kHeldoutStart = 0x3de6'0000u;
constexpr int kHeldoutGames = 8;
constexpr std::uint32_t kScreenStart = 0x3eb3'0000u;
constexpr int kScreenGames = 8;
constexpr std::uint32_t kConfirmationStart = 0x3eb4'0000u;
constexpr int kConfirmationGames = 16;
constexpr double kMaterialFirstPairClearRatio = 0.95;
constexpr int kMinimumLooStableFolds = 3;
constexpr double kWallLimitSeconds = 35.0 * 60.0;
constexpr double kWallProjectionSafetyFactor = 1.50;
constexpr std::uint64_t kMaximumSelfTestRssBytes = 2ull * 1024u * 1024u * 1024u;

struct Config {
  const char* name;
  double clear_reward;
  double phase_strength;
  double reveal_reward = 0.0;
};

// Frozen before any assigned gameplay.  The phase vector is indexed by
// moves_remaining: it raises direct/latent energy value early in the cycle and
// lowers it as the next covered-row rise approaches.
constexpr std::array<double, kMovesPerLevel + 1> kPhaseEnergyDelta{{
    0.0, -0.40, -0.25, 0.0, 0.20, 0.35,
}};
constexpr std::array<Config, 5> kMenu{{
    {"stock", 0.0, 0.0},
    {"clear-only", 600.0, 0.0},
    {"phase-only", 0.0, 1.0},
    {"combined-moderate", 600.0, 1.0},
    {"combined-aggressive", 1'200.0, 1.5},
}};

static_assert(kDepth == 4 && kChanceSamples == 5);
static_assert(kMaximumWork > kWorstCaseWork);
static_assert(kMaximumCacheEntries > kWorstCaseCacheEntries);
static_assert(kWorstCaseWork == 3'134'950);
static_assert(kWorstCaseCacheEntries == 45'430);
static_assert(kLevelBonus == 7'000);
static_assert(frozen::kPolicySeed == 0xd707'5eedu);
static_assert(frozen::kDirectPotentialWeight == 1'600.0);
static_assert(frozen::kLatentChainPotentialWeight == 700.0);
static_assert(kFittingStart + kFittingGames < kHeldoutStart);
static_assert(kHeldoutStart + kHeldoutGames < kScreenStart);
static_assert(kScreenStart + kScreenGames < kConfirmationStart);
static_assert((kFittingStart >> 24u) == 0x3du &&
              (kHeldoutStart >> 24u) == 0x3du &&
              (kScreenStart >> 24u) == 0x3eu &&
              (kConfirmationStart >> 24u) == 0x3eu);
static_assert((kFittingStart >> 24u) != 0x7du &&
              (kFittingStart >> 24u) != 0xd7u);

std::mutex progress_mutex;

State publicState(const State& source) {
  State result;
  result.board = source.board;
  result.next_disc = source.next_disc;
  result.moves_remaining = source.moves_remaining;
  result.game_over = source.game_over;
  result.score = 0;
  result.level = 1;
  result.moves_played = 0;
  return result;
}

double phaseEnergyAdjustment(const State& state, const Config& config) {
  if (config.phase_strength == 0.0) return 0.0;
  if (state.moves_remaining < 1 || state.moves_remaining > kMovesPerLevel) {
    throw std::invalid_argument("invalid rise phase in energy evaluator");
  }
  const auto features = frozen::extractFairFeatures(state).heuristic;
  const double stored_energy =
      frozen::kDirectPotentialWeight * features.direct_potential +
      frozen::kLatentChainPotentialWeight * features.latent_chain_potential;
  return config.phase_strength * kPhaseEnergyDelta[state.moves_remaining] *
         stored_energy;
}

double candidateLeaf(const State& state, const Config& config) {
  const double stock_value = frozen::fairLeaf(state);
  if (config.phase_strength == 0.0) return stock_value;
  return stock_value + phaseEnergyAdjustment(state, config);
}

int numberedCleared(const MoveResult& move) {
  int result = 0;
  for (const Wave& wave : move.waves) result += wave.cleared;
  return result;
}

int coversRevealed(const MoveResult& move) {
  int result = 0;
  for (const Wave& wave : move.waves) result += wave.revealed;
  return result;
}

double transitionValue(const MoveResult& move, const Config& config) {
  const double score = static_cast<double>(move.score_delta);
  if (config.clear_reward == 0.0 && config.reveal_reward == 0.0) return score;
  return score + config.clear_reward * numberedCleared(move) +
         config.reveal_reward * coversRevealed(move);
}

class WorkLimitReached : public std::exception {};

struct CacheEntry {
  double value = 0.0;
  std::list<std::string>::iterator order;
};

struct SearchContext {
  std::unordered_map<std::string, CacheEntry> cache;
  std::list<std::string> order;
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
  std::uint64_t cache_hits = 0;
};

void checkBudget(const SearchContext& context) {
  if (context.work >= kMaximumWork) throw WorkLimitReached{};
}

void cacheValue(SearchContext& context, std::string key, double value) {
  const auto prior = context.cache.find(key);
  if (prior != context.cache.end()) {
    context.order.erase(prior->second.order);
    context.cache.erase(prior);
  }
  while (context.cache.size() >= kMaximumCacheEntries) {
    const std::string& oldest = context.order.front();
    context.cache.erase(oldest);
    context.order.pop_front();
  }
  context.order.push_back(key);
  const auto order = std::prev(context.order.end());
  context.cache.emplace(std::move(key), CacheEntry{value, order});
}

double bestFutureValue(const State& state, int depth, const Config& config,
                       SearchContext& context);

struct ActionValue {
  double value = 0.0;
  double expected_score = 0.0;
};

ActionValue evaluateAction(const State& state, int column, int depth,
                           const Config& config, SearchContext& context) {
  const std::uint32_t state_seed = cfpi::detail::scenarioSeedForState(
      state, frozen::kPolicySeed, depth);
  ActionValue result;
  for (int sample = 0; sample < kChanceSamples; ++sample) {
    checkBudget(context);
    cfpi::detail::StratifiedRandom random{
        state_seed, sample, kChanceSamples, 0};
    MoveResult move;
    const bool played =
        cfpi::detail::playMoveSampled(state, column, random, move);
    ++context.work;
    if (!played) {
      result.value += frozen::kTerminalUtility;
      continue;
    }
    const double score_delta = static_cast<double>(move.score_delta);
    const double transition = transitionValue(move, config);
    result.expected_score += score_delta;
    if (move.state.game_over) {
      result.value += transition + frozen::kTerminalUtility;
      continue;
    }
    move.state = publicState(move.state);
    move.state.next_disc = cfpi::detail::sampledNextDisc(
        state_seed, sample, kChanceSamples);
    bool ignored = false;
    const State next = cfpi::detail::canonicalState(move.state, ignored);
    result.value +=
        transition + bestFutureValue(next, depth - 1, config, context);
  }
  result.value /= kChanceSamples;
  result.expected_score /= kChanceSamples;
  return result;
}

double evaluateLeaf(const State& state, const Config& config,
                    SearchContext& context) {
  checkBudget(context);
  ++context.work;
  const double value = candidateLeaf(state, config);
  if (!std::isfinite(value)) {
    throw std::runtime_error("phase-energy leaf returned non-finite value");
  }
  return value;
}

double bestFutureValue(const State& state, int depth, const Config& config,
                       SearchContext& context) {
  ++context.nodes;
  checkBudget(context);
  if (state.game_over) return frozen::kTerminalUtility;
  if (depth == 0) return evaluateLeaf(state, config, context);
  const std::string key = cfpi::detail::dynamicStateKey(state, depth);
  const auto cached = context.cache.find(key);
  if (cached != context.cache.end()) {
    ++context.cache_hits;
    const double value = cached->second.value;
    context.order.splice(context.order.end(), context.order,
                         cached->second.order);
    return value;
  }
  double best = -std::numeric_limits<double>::infinity();
  for (const int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(state.board, column)) continue;
    best = std::max(
        best, evaluateAction(state, column, depth, config, context).value);
  }
  if (!std::isfinite(best)) best = frozen::kTerminalUtility;
  cacheValue(context, key, best);
  return best;
}

struct RootEvaluation {
  int action = -1;
  double value = -std::numeric_limits<double>::infinity();
  std::array<double, kBoardSize> values{};
  std::array<double, kBoardSize> expected_scores{};
};

RootEvaluation rootDecision(const State& state, int depth,
                            const Config& config, SearchContext& context) {
  RootEvaluation result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  result.expected_scores.fill(-std::numeric_limits<double>::infinity());
  for (const int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(state.board, column)) continue;
    const ActionValue value =
        evaluateAction(state, column, depth, config, context);
    result.values[column] = value.value;
    result.expected_scores[column] = value.expected_score;
    if (value.value > result.value) {
      result.value = value.value;
      result.action = column;
    }
  }
  return result;
}

struct SearchDecision {
  int action = -1;
  int completed_depth = 0;
  bool complete = false;
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
  std::uint64_t cache_hits = 0;
  std::size_t cache_entries = 0;
  std::array<double, kBoardSize> root_values{};
  std::array<double, kBoardSize> root_expected_scores{};
};

SearchDecision chooseAction(const State& source, const Config& config) {
  if (source.game_over) return {};
  bool mirrored = false;
  const State canonical =
      cfpi::detail::canonicalState(publicState(source), mirrored);
  SearchContext context;
  RootEvaluation completed;
  int completed_depth = 0;
  for (int depth = 1; depth <= kDepth; ++depth) {
    try {
      completed = rootDecision(canonical, depth, config, context);
      if (completed.action < 0) break;
      completed_depth = depth;
    } catch (const WorkLimitReached&) {
      break;
    }
  }
  int action = completed.action;
  if (action < 0) action = centerFirstMove(canonical.board);
  SearchDecision result;
  result.action = mirrored ? kBoardSize - 1 - action : action;
  result.completed_depth = completed_depth;
  result.complete = completed_depth == kDepth;
  result.nodes = context.nodes;
  result.work = context.work;
  result.cache_hits = context.cache_hits;
  result.cache_entries = context.cache.size();
  result.root_values.fill(-std::numeric_limits<double>::infinity());
  result.root_expected_scores.fill(-std::numeric_limits<double>::infinity());
  if (completed_depth > 0) {
    for (int column = 0; column < kBoardSize; ++column) {
      const int source_column =
          mirrored ? kBoardSize - 1 - column : column;
      result.root_values[source_column] = completed.values[column];
      result.root_expected_scores[source_column] =
          completed.expected_scores[column];
    }
  }
  return result;
}

struct GameResult {
  std::uint32_t seed = 0;
  int config = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t numbered_cleared = 0;
  std::uint64_t covers_revealed = 0;
  int maximum_chain = 0;
  int cleared_boards = 0;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::size_t peak_cache_entries = 0;
  double decision_seconds = 0.0;
};

GameResult runGame(std::uint32_t seed, int config_index) {
  const Config& config = kMenu.at(static_cast<std::size_t>(config_index));
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  result.config = config_index;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    const auto started = Clock::now();
    const SearchDecision decision = chooseAction(state, config);
    result.decision_seconds +=
        std::chrono::duration<double>(Clock::now() - started).count();
    if (!decision.complete || decision.completed_depth != kDepth) {
      throw std::runtime_error("phase-energy D4 did not complete");
    }
    if (decision.work > kMaximumWork ||
        decision.cache_entries > kMaximumCacheEntries) {
      throw std::runtime_error("phase-energy D4 exceeded resource bound");
    }
    result.work += decision.work;
    result.nodes += decision.nodes;
    result.cache_hits += decision.cache_hits;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, decision.cache_entries);
    if (!isLegal(state.board, decision.action)) {
      throw std::runtime_error("phase-energy D4 returned illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("phase-energy headless transition failed");
    }
    for (const Wave& wave : move.waves) {
      result.numbered_cleared += static_cast<std::uint64_t>(wave.cleared);
      result.covers_revealed += static_cast<std::uint64_t>(wave.revealed);
      result.maximum_chain = std::max(result.maximum_chain, wave.depth);
    }
    if (move.cleared_board) ++result.cleared_boards;
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  return result;
}

using MenuGames = std::array<std::vector<GameResult>, kMenu.size()>;

MenuGames runMenu(std::uint32_t start, int games,
                  const std::vector<int>& configs, int threads,
                  std::string_view label) {
  MenuGames result;
  struct Job {
    int config;
    int game;
  };
  std::vector<Job> jobs;
  for (const int config : configs) {
    result[static_cast<std::size_t>(config)].resize(
        static_cast<std::size_t>(games));
    for (int game = 0; game < games; ++game) jobs.push_back({config, game});
  }
  std::atomic<std::size_t> next{0};
  const int worker_count =
      std::max(1, std::min(threads, static_cast<int>(jobs.size())));
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [&, worker]() {
      static_cast<void>(worker);
      for (;;) {
        const std::size_t index = next.fetch_add(1);
        if (index >= jobs.size()) break;
        const Job job = jobs[index];
        const std::uint32_t seed =
            start + static_cast<std::uint32_t>(job.game);
        GameResult game = runGame(seed, job.config);
        result[static_cast<std::size_t>(job.config)]
              [static_cast<std::size_t>(job.game)] = game;
        const std::lock_guard<std::mutex> lock(progress_mutex);
        std::cerr << "phase-energy " << label << ' ' << kMenu[job.config].name
                  << " seed 0x" << std::hex << seed << std::dec << ' '
                  << game.score << " points/" << game.moves << " moves, "
                  << game.numbered_cleared << " clears\n";
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

void appendMenu(MenuGames& destination, MenuGames&& source,
                const std::vector<int>& configs) {
  for (const int config : configs) {
    auto& target = destination[static_cast<std::size_t>(config)];
    auto& values = source[static_cast<std::size_t>(config)];
    target.insert(target.end(), std::make_move_iterator(values.begin()),
                  std::make_move_iterator(values.end()));
  }
}

struct Summary {
  int games = 0;
  int censored = 0;
  double mean_score = 0.0;
  double median_score = 0.0;
  double standard_error = 0.0;
  double mean_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double mean_maximum_chain = 0.0;
  double mean_decision_ms = 0.0;
  double mean_work_per_move = 0.0;
  std::size_t peak_cache_entries = 0;
};

Summary summarize(const std::vector<GameResult>& games,
                  std::optional<int> omitted = std::nullopt) {
  Summary result;
  std::vector<double> scores;
  double score_square_sum = 0.0;
  std::uint64_t moves = 0;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
  std::uint64_t work = 0;
  double seconds = 0.0;
  for (std::size_t index = 0; index < games.size(); ++index) {
    if (omitted.has_value() && static_cast<int>(index) == *omitted) continue;
    const GameResult& game = games[index];
    ++result.games;
    result.censored += game.censored ? 1 : 0;
    result.mean_score += static_cast<double>(game.score);
    score_square_sum += static_cast<double>(game.score) * game.score;
    scores.push_back(static_cast<double>(game.score));
    moves += game.moves;
    clears += game.numbered_cleared;
    reveals += game.covers_revealed;
    work += game.work;
    seconds += game.decision_seconds;
    result.mean_maximum_chain += game.maximum_chain;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, game.peak_cache_entries);
  }
  if (result.games == 0) return result;
  result.mean_score /= result.games;
  result.mean_maximum_chain /= result.games;
  std::sort(scores.begin(), scores.end());
  if (scores.size() % 2 == 1) {
    result.median_score = scores[scores.size() / 2];
  } else {
    result.median_score =
        0.5 * (scores[scores.size() / 2 - 1] + scores[scores.size() / 2]);
  }
  if (result.games > 1) {
    const double variance =
        (score_square_sum - result.games * result.mean_score * result.mean_score) /
        static_cast<double>(result.games - 1);
    result.standard_error =
        std::sqrt(std::max(0.0, variance) / result.games);
  }
  result.mean_moves = static_cast<double>(moves) / result.games;
  if (moves > 0) {
    result.clears_per_move = static_cast<double>(clears) / moves;
    result.reveals_per_move = static_cast<double>(reveals) / moves;
    result.mean_decision_ms = 1'000.0 * seconds / moves;
    result.mean_work_per_move = static_cast<double>(work) / moves;
  }
  return result;
}

double projectedStageSeconds(const MenuGames& source,
                             const std::vector<int>& configs,
                             int source_games, int target_games,
                             int threads) {
  double decision_seconds = 0.0;
  for (const int config : configs) {
    for (const GameResult& game : source[static_cast<std::size_t>(config)]) {
      decision_seconds += game.decision_seconds;
    }
  }
  if (source_games <= 0) return kWallLimitSeconds;
  const double scaled_cpu = decision_seconds * target_games / source_games;
  return kWallProjectionSafetyFactor * scaled_cpu / std::max(1, threads);
}

bool stageFitsWallBudget(const Clock::time_point& experiment_started,
                         double projected_seconds) {
  const double elapsed = std::chrono::duration<double>(
                             Clock::now() - experiment_started)
                             .count();
  return elapsed + projected_seconds <= kWallLimitSeconds;
}

struct LeaveOneOut {
  std::array<int, kMenu.size()> winner_counts{};
  int triple_wins = 0;
  bool stable = false;
};

int selectCandidate(const MenuGames& games,
                    std::optional<int> omitted = std::nullopt) {
  int selected = 1;
  Summary best = summarize(games[1], omitted);
  for (std::size_t config = 2; config < kMenu.size(); ++config) {
    const Summary candidate = summarize(games[config], omitted);
    if (candidate.mean_score > best.mean_score ||
        (candidate.mean_score == best.mean_score &&
         candidate.mean_moves > best.mean_moves)) {
      selected = static_cast<int>(config);
      best = candidate;
    }
  }
  return selected;
}

LeaveOneOut leaveOneOut(const MenuGames& games, int champion) {
  LeaveOneOut result;
  for (int omitted = 0; omitted < kFittingGames; ++omitted) {
    const int winner = selectCandidate(games, omitted);
    ++result.winner_counts[static_cast<std::size_t>(winner)];
    const Summary baseline = summarize(games[0], omitted);
    const Summary candidate = summarize(games[champion], omitted);
    if (candidate.mean_score > baseline.mean_score &&
        candidate.mean_moves > baseline.mean_moves &&
        candidate.clears_per_move > baseline.clears_per_move) {
      ++result.triple_wins;
    }
  }
  result.stable =
      result.triple_wins >= kMinimumLooStableFolds &&
      result.winner_counts[static_cast<std::size_t>(champion)] >=
          kMinimumLooStableFolds;
  return result;
}

std::uint64_t peakRssBytes() { return stock::peakRssBytes(); }

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

void runSelfTests() {
  const State fixture = frozen::fixtureState(frozen::kTypeScriptFixtures[1]);
  const State public_fixture = publicState(fixture);
  const stock::SearchDecision stock_decision =
      stock::chooseDepth4Action(public_fixture);
  const SearchDecision zero = chooseAction(public_fixture, kMenu[0]);
  expect(zero.action == stock_decision.action &&
             zero.completed_depth == stock_decision.completed_depth &&
             zero.work == stock_decision.work &&
             zero.nodes == stock_decision.nodes &&
             zero.cache_hits == stock_decision.cache_hits &&
             zero.cache_entries == stock_decision.cache_entries &&
             zero.root_values == stock_decision.root_values &&
             zero.root_expected_scores == stock_decision.root_expected_scores,
         "zero coefficients did not exactly reproduce stock D4");

  const SearchDecision first = chooseAction(public_fixture, kMenu[3]);
  const SearchDecision repeat = chooseAction(public_fixture, kMenu[3]);
  expect(first.action == repeat.action && first.root_values == repeat.root_values &&
             first.work == repeat.work && first.nodes == repeat.nodes,
         "candidate search was not deterministic");
  State reflected = public_fixture;
  reflected.board = cfpi::detail::mirrorBoard(public_fixture.board);
  const SearchDecision mirrored = chooseAction(reflected, kMenu[3]);
  expect(mirrored.action == kBoardSize - 1 - first.action &&
             mirrored.work == first.work,
         "candidate search was not reflection safe");
  for (int column = 0; column < kBoardSize; ++column) {
    expect(first.root_values[column] ==
               mirrored.root_values[kBoardSize - 1 - column],
           "candidate root values were not reflection safe");
  }
  State metadata = public_fixture;
  metadata.score = 8'000'000;
  metadata.level = 73;
  metadata.moves_played = 412;
  const SearchDecision metadata_decision = chooseAction(metadata, kMenu[3]);
  expect(metadata_decision.action == first.action &&
             metadata_decision.root_values == first.root_values,
         "candidate search used non-public metadata");

  State phase_state = public_fixture;
  phase_state.moves_remaining = 5;
  const double early = phaseEnergyAdjustment(phase_state, kMenu[2]);
  phase_state.moves_remaining = 1;
  const double late = phaseEnergyAdjustment(phase_state, kMenu[2]);
  expect(early > 0.0 && late < 0.0,
         "phase schedule did not change stored energy in opposite directions");
  MoveResult move;
  move.score_delta = 14;
  move.waves.push_back({1, 2, 0, 14});
  expect(transitionValue(move, kMenu[1]) == 1'214.0 &&
             transitionValue(move, kMenu[2]) == 14.0,
         "isolated clear reward ablation failed");
  expect(first.complete && first.work <= kMaximumWork &&
             first.cache_entries <= kMaximumCacheEntries,
         "candidate resource/completion proof failed");
  expect(peakRssBytes() <= kMaximumSelfTestRssBytes,
         "candidate self-test exceeded RSS bound");
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"games\":" << summary.games
         << ",\"censored\":" << summary.censored
         << ",\"meanScore\":" << summary.mean_score
         << ",\"medianScore\":" << summary.median_score
         << ",\"standardError\":" << summary.standard_error
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"numberedClearsPerMove\":" << summary.clears_per_move
         << ",\"coversRevealedPerMove\":" << summary.reveals_per_move
         << ",\"meanMaximumChain\":" << summary.mean_maximum_chain
         << ",\"meanDecisionMs\":" << summary.mean_decision_ms
         << ",\"meanWorkPerMove\":" << summary.mean_work_per_move
         << ",\"peakCacheEntries\":" << summary.peak_cache_entries << '}';
}

void writeGames(std::ostream& output, const std::vector<GameResult>& games) {
  output << '[';
  for (std::size_t index = 0; index < games.size(); ++index) {
    if (index > 0) output << ',';
    const GameResult& game = games[index];
    output << "{\"seed\":" << game.seed << ",\"score\":" << game.score
           << ",\"moves\":" << game.moves
           << ",\"censored\":" << (game.censored ? "true" : "false")
           << ",\"numberedCleared\":" << game.numbered_cleared
           << ",\"coversRevealed\":" << game.covers_revealed
           << ",\"maximumChain\":" << game.maximum_chain
           << ",\"clearedBoards\":" << game.cleared_boards
           << ",\"work\":" << game.work << ",\"nodes\":" << game.nodes
           << ",\"cacheHits\":" << game.cache_hits
           << ",\"peakCacheEntries\":" << game.peak_cache_entries
           << ",\"decisionSeconds\":" << game.decision_seconds << '}';
  }
  output << ']';
}

void writeCohort(std::ostream& output, const MenuGames& games,
                 const std::vector<int>& configs) {
  output << '[';
  for (std::size_t index = 0; index < configs.size(); ++index) {
    if (index > 0) output << ',';
    const int config = configs[index];
    output << "{\"configIndex\":" << config << ",\"summary\":";
    writeSummary(output, summarize(games[static_cast<std::size_t>(config)]));
    output << ",\"games\":";
    writeGames(output, games[static_cast<std::size_t>(config)]);
    output << '}';
  }
  output << ']';
}

struct Options {
  std::string output = "/tmp/drop7-fair-phase-energy-release.json";
  int threads = kDefaultThreads;
  bool self_test_only = false;
};

Options parseOptions(int argc, char** argv) {
  Options result;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--self-test-only") {
      result.self_test_only = true;
      continue;
    }
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    if (argument == "--output") {
      result.output = argv[++index];
    } else if (argument == "--threads") {
      result.threads = std::stoi(argv[++index]);
      if (result.threads < 1 || result.threads > 32) {
        throw std::invalid_argument("threads must be in [1,32]");
      }
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  return result;
}

int run(const Options& options) {
  runSelfTests();
  std::cerr << "phase-energy self-tests passed (peak RSS " << peakRssBytes()
            << " bytes)\n";
  if (options.self_test_only) return 0;
  const auto started = Clock::now();
  const std::vector<int> all_configs{0, 1, 2, 3, 4};

  MenuGames fitting =
      runMenu(kFittingStart, 1, all_configs, options.threads, "fitting-first");
  const GameResult& first_baseline = fitting[0][0];
  bool early_diagnostic_stop = true;
  for (std::size_t config = 1; config < kMenu.size(); ++config) {
    const GameResult& candidate = fitting[config][0];
    const bool material_clear_loss =
        static_cast<double>(candidate.numbered_cleared) <
        static_cast<double>(first_baseline.numbered_cleared) *
            kMaterialFirstPairClearRatio;
    if (!(candidate.score < first_baseline.score && material_clear_loss)) {
      early_diagnostic_stop = false;
    }
  }
  if (!early_diagnostic_stop) {
    MenuGames remainder = runMenu(kFittingStart + 1, kFittingGames - 1,
                                  all_configs, options.threads,
                                  "fitting-remainder");
    appendMenu(fitting, std::move(remainder), all_configs);
  }

  int champion = -1;
  LeaveOneOut loo;
  bool fitting_gate = false;
  if (!early_diagnostic_stop) {
    champion = selectCandidate(fitting);
    loo = leaveOneOut(fitting, champion);
    const Summary baseline = summarize(fitting[0]);
    const Summary candidate = summarize(fitting[champion]);
    fitting_gate = baseline.censored == 0 && candidate.censored == 0 &&
                   candidate.mean_score > baseline.mean_score &&
                   candidate.mean_moves > baseline.mean_moves &&
                   candidate.clears_per_move > baseline.clears_per_move &&
                   loo.stable;
  }

  const std::vector<int> pair_configs = champion > 0
                                            ? std::vector<int>{0, champion}
                                            : std::vector<int>{0};
  MenuGames heldout;
  MenuGames screen;
  MenuGames confirmation;
  bool heldout_gate = false;
  bool screen_gate = false;
  bool confirmation_gate = false;
  std::string resource_stop_stage;
  const double heldout_projection =
      champion > 0
          ? projectedStageSeconds(fitting, pair_configs, kFittingGames,
                                  kHeldoutGames, options.threads)
          : kWallLimitSeconds;
  if (fitting_gate &&
      !stageFitsWallBudget(started, heldout_projection)) {
    resource_stop_stage = "heldout";
  }
  if (fitting_gate && resource_stop_stage.empty()) {
    heldout = runMenu(kHeldoutStart, kHeldoutGames, pair_configs,
                      options.threads, "heldout");
    const Summary baseline = summarize(heldout[0]);
    const Summary candidate = summarize(heldout[champion]);
    heldout_gate = baseline.censored == 0 && candidate.censored == 0 &&
                   candidate.mean_score > baseline.mean_score &&
                   candidate.mean_moves > baseline.mean_moves &&
                   candidate.clears_per_move >= baseline.clears_per_move &&
                   candidate.reveals_per_move >= baseline.reveals_per_move;
  }
  const double screen_projection =
      heldout_gate
          ? projectedStageSeconds(heldout, pair_configs, kHeldoutGames,
                                  kScreenGames, options.threads)
          : kWallLimitSeconds;
  if (fitting_gate && heldout_gate &&
      !stageFitsWallBudget(started, screen_projection)) {
    resource_stop_stage = "screen";
  }
  if (fitting_gate && heldout_gate && resource_stop_stage.empty()) {
    screen = runMenu(kScreenStart, kScreenGames, pair_configs,
                     options.threads, "fresh-screen");
    const Summary baseline = summarize(screen[0]);
    const Summary candidate = summarize(screen[champion]);
    screen_gate = baseline.censored == 0 && candidate.censored == 0 &&
                  candidate.mean_score > baseline.mean_score &&
                  candidate.mean_moves > baseline.mean_moves;
  }
  const double confirmation_projection =
      screen_gate
          ? projectedStageSeconds(screen, pair_configs, kScreenGames,
                                  kConfirmationGames, options.threads)
          : kWallLimitSeconds;
  if (screen_gate &&
      !stageFitsWallBudget(started, confirmation_projection)) {
    resource_stop_stage = "confirmation";
  }
  if (screen_gate && resource_stop_stage.empty()) {
    confirmation = runMenu(kConfirmationStart, kConfirmationGames,
                           pair_configs, options.threads,
                           "fresh-confirmation");
    const Summary baseline = summarize(confirmation[0]);
    const Summary candidate = summarize(confirmation[champion]);
    confirmation_gate =
        baseline.censored == 0 && candidate.censored == 0 &&
        candidate.mean_score > baseline.mean_score &&
        candidate.mean_moves > baseline.mean_moves &&
        candidate.clears_per_move >= baseline.clears_per_move &&
        candidate.reveals_per_move >= baseline.reveals_per_move;
  }

  const double wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open phase-energy artifact");
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"fair-d4-phase-energy-release\",\n"
         << "  \"preregistered\":true,\n"
         << "  \"publicStateOnly\":true,\n"
         << "  \"search\":{\"depth\":" << kDepth
         << ",\"chanceSamples\":" << kChanceSamples
         << ",\"fullWidth\":true,\"maximumMoves\":" << kMaximumMoves
         << ",\"maximumWork\":" << kMaximumWork
         << ",\"worstCaseWork\":" << kWorstCaseWork
         << ",\"maximumCacheEntries\":" << kMaximumCacheEntries
         << ",\"worstCaseCacheEntries\":" << kWorstCaseCacheEntries
         << ",\"wallLimitSeconds\":" << kWallLimitSeconds
         << ",\"wallProjectionSafetyFactor\":"
         << kWallProjectionSafetyFactor
         << "},\n  \"seedDiscipline\":{\"fittingStart\":"
         << kFittingStart << ",\"fittingGames\":" << kFittingGames
         << ",\"heldoutStart\":" << kHeldoutStart
         << ",\"heldoutGames\":" << kHeldoutGames
         << ",\"screenStart\":" << kScreenStart
         << ",\"screenGames\":" << kScreenGames
         << ",\"confirmationStart\":" << kConfirmationStart
         << ",\"confirmationGames\":" << kConfirmationGames << "},\n"
         << "  \"stockEnergyWeights\":{\"direct\":"
         << frozen::kDirectPotentialWeight << ",\"latent\":"
         << frozen::kLatentChainPotentialWeight
         << "},\n  \"phaseEnergyDeltaByMovesRemaining\":[";
  for (std::size_t index = 0; index < kPhaseEnergyDelta.size(); ++index) {
    if (index > 0) output << ',';
    output << kPhaseEnergyDelta[index];
  }
  output << "],\n  \"menu\":[";
  for (std::size_t index = 0; index < kMenu.size(); ++index) {
    if (index > 0) output << ',';
    output << "{\"index\":" << index << ",\"name\":\""
           << kMenu[index].name << "\",\"clearReward\":"
           << kMenu[index].clear_reward << ",\"phaseStrength\":"
           << kMenu[index].phase_strength << '}';
  }
  output << "],\n  \"earlyDiagnosticStop\":"
         << (early_diagnostic_stop ? "true" : "false")
         << ",\n  \"fitting\":";
  writeCohort(output, fitting, all_configs);
  output << ",\n  \"selection\":{\"championIndex\":" << champion
         << ",\"minimumStableFolds\":" << kMinimumLooStableFolds
         << ",\"tripleWinFolds\":" << loo.triple_wins
         << ",\"winnerCounts\":[";
  for (std::size_t index = 0; index < loo.winner_counts.size(); ++index) {
    if (index > 0) output << ',';
    output << loo.winner_counts[index];
  }
  output << "],\"leaveOneOutStable\":" << (loo.stable ? "true" : "false")
         << ",\"fittingGatePassed\":" << (fitting_gate ? "true" : "false")
         << "},\n  \"heldoutRan\":"
         << (fitting_gate && resource_stop_stage != "heldout" ? "true"
                                                               : "false")
         << ",\n  \"heldout\":";
  if (!fitting_gate || resource_stop_stage == "heldout") output << "null";
  else writeCohort(output, heldout, pair_configs);
  output << ",\n  \"heldoutGatePassed\":"
         << (heldout_gate ? "true" : "false")
         << ",\n  \"resourceStopStage\":";
  if (resource_stop_stage.empty()) output << "null";
  else output << '\"' << resource_stop_stage << '\"';
  output << ",\n  \"stageProjections\":{\"heldout\":"
         << heldout_projection << ",\"screen\":" << screen_projection
         << ",\"confirmation\":" << confirmation_projection
         << "},\n  \"screenRan\":"
         << (fitting_gate && heldout_gate && resource_stop_stage != "screen"
                 ? "true"
                 : "false")
         << ",\n  \"screen\":";
  if (!(fitting_gate && heldout_gate) || resource_stop_stage == "screen") {
    output << "null";
  }
  else writeCohort(output, screen, pair_configs);
  output << ",\n  \"screenGatePassed\":"
         << (screen_gate ? "true" : "false")
         << ",\n  \"confirmationRan\":"
         << (screen_gate && resource_stop_stage != "confirmation" ? "true"
                                                                    : "false")
         << ",\n  \"confirmation\":";
  if (!screen_gate || resource_stop_stage == "confirmation") output << "null";
  else writeCohort(output, confirmation, pair_configs);
  output << ",\n  \"confirmationGatePassed\":"
         << (confirmation_gate ? "true" : "false")
         << ",\n  \"qualified\":"
         << (screen_gate && confirmation_gate ? "true" : "false")
         << ",\n  \"wallSeconds\":" << wall_seconds
         << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
  output.close();

  std::cout << "early-stop=" << (early_diagnostic_stop ? "yes" : "no")
            << " champion=" << champion
            << " fitting=" << (fitting_gate ? "pass" : "fail")
            << " heldout=" << (heldout_gate ? "pass" : "fail")
            << " screen=" << (screen_gate ? "pass" : "not-pass")
            << " confirmation="
            << (confirmation_gate ? "pass" : "not-pass")
            << " artifact=" << options.output << '\n';
  return screen_gate && confirmation_gate ? 0 : 2;
}

}  // namespace drop7::fair_phase_energy_release

#ifndef DROP7_FAIR_PHASE_ENERGY_RELEASE_LIBRARY
int main(int argc, char** argv) {
  try {
    const auto options =
        drop7::fair_phase_energy_release::parseOptions(argc, argv);
    return drop7::fair_phase_energy_release::run(options);
  } catch (const std::exception& error) {
    std::cerr << "drop7_fair_phase_energy_release: " << error.what() << '\n';
    return 1;
  }
}
#endif
