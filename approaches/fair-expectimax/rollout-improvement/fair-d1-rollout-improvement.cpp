#define main drop7_fair_only_horizon_embedded_main
#include "../reference/fair-only-horizon.cpp"
#undef main

#include <atomic>
#include <bit>
#include <optional>
#include <sstream>

// A deliberately small, public-information rollout pilot.  Every legal root
// action is evaluated on the same seven deterministic, stratified scenario
// tapes.  The first action is fixed by the root candidate; every later action
// is selected by the fair depth-one policy without access to the tape.  Reveal
// draws and future visible discs occupy separate event-indexed domains.
namespace drop7::fair_d1_rollout_improvement {

namespace fair = fair_only_horizon;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kFittingStart = 0x3df0'0000u;
constexpr int kFittingGames = 12;
constexpr std::uint32_t kHeldoutStart = 0x3df1'0000u;
constexpr int kHeldoutGames = 16;
constexpr int kMaximumMoves = 1'000;
constexpr int kScenarios = 7;
constexpr int kDefaultThreads = 4;
constexpr double kTerminalUtility = -1'000'000.0;
constexpr double kMinimumFittingMean = 250'000.0;
constexpr double kMinimumClearThroughputRatio = 1.05;
constexpr int kMinimumLooScoreWins = 9;
constexpr std::uint32_t kTapeSeedDomain = 0x4652'5450u;  // "FRTP"
constexpr std::uint32_t kRevealTapeDomain = 0x4652'564cu;  // "FRVL"
constexpr std::uint32_t kVisibleTapeDomain = 0x4656'4953u; // "FVIS"
constexpr int kEventsPerStep = 64;

struct Config {
  int horizon;
  double tail_scale;
};

// Preregistered before running any fitting game.
constexpr std::array<Config, 6> kConfigs{{
    {8, 0.0}, {8, 0.25}, {16, 0.0},
    {16, 0.25}, {24, 0.0}, {24, 0.25},
}};

constexpr std::array<int, kBoardSize> kColumnOrder{{3, 2, 4, 1, 5, 0, 6}};
constexpr std::uint64_t kMaximumTransitionsPerDecision =
    static_cast<std::uint64_t>(kBoardSize) * kScenarios *
    kConfigs.back().horizon * (kBoardSize + 1);

static_assert(kLevelBonus == 7'000);
static_assert(kScenarios == kBoardSize);
static_assert(kEventsPerStep > kCellCount);
static_assert(kMaximumTransitionsPerDecision == 9'408);
static_assert((kFittingStart >> 24u) != 0x3eu &&
              (kFittingStart >> 24u) != 0x7du &&
              (kFittingStart >> 24u) != 0xd7u);
static_assert((kHeldoutStart >> 24u) != 0x3eu &&
              (kHeldoutStart >> 24u) != 0x7du &&
              (kHeldoutStart >> 24u) != 0xd7u);
static_assert(kFittingStart + kFittingGames <= kHeldoutStart);

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

bool samePublicState(const State& left_source, const State& right_source) {
  const State left = publicState(left_source);
  const State right = publicState(right_source);
  return left.board == right.board && left.next_disc == right.next_disc &&
         left.moves_remaining == right.moves_remaining &&
         left.game_over == right.game_over;
}

std::uint64_t mix64(std::uint64_t value) {
  value ^= value >> 30u;
  value *= 0xbf58'476d'1ce4'e5b9ull;
  value ^= value >> 27u;
  value *= 0x94d0'49bb'1331'11ebull;
  return value ^ (value >> 31u);
}

std::uint64_t publicHash(const State& source) {
  bool ignored = false;
  const State state =
      cfpi::detail::canonicalState(publicState(source), ignored);
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint64_t>(cell + 1u);
    hash *= 0x0000'0100'0000'01b3ull;
  }
  hash ^= state.next_disc;
  hash *= 0x0000'0100'0000'01b3ull;
  hash ^= static_cast<std::uint64_t>(state.moves_remaining + 1);
  hash *= 0x0000'0100'0000'01b3ull;
  hash ^= static_cast<std::uint64_t>(state.game_over);
  return mix64(hash);
}

std::uint32_t seed32(std::uint64_t value) {
  return mix32(static_cast<std::uint32_t>(value) ^
               static_cast<std::uint32_t>(value >> 32u));
}

struct Work {
  std::uint64_t transitions = 0;
  std::uint64_t fair_d1_calls = 0;
};

int fairDepthOneAction(const State& source, Work* work = nullptr) {
  if (source.game_over) return -1;
  bool mirrored = false;
  const State state = cfpi::detail::canonicalState(publicState(source), mirrored);
  const std::uint32_t chance_seed = cfpi::detail::scenarioSeedForState(
      state, fair::kPolicySeed, 1);
  int selected = -1;
  double best = -std::numeric_limits<double>::infinity();
  if (work != nullptr) ++work->fair_d1_calls;
  for (const int action : kColumnOrder) {
    if (!isLegal(state.board, action)) continue;
    cfpi::detail::StratifiedRandom random{chance_seed, 0, 1, 0};
    MoveResult move;
    if (!cfpi::detail::playMoveSampled(state, action, random, move)) continue;
    if (work != nullptr) ++work->transitions;
    double value = static_cast<double>(move.score_delta);
    if (move.state.game_over) {
      value += fair::kTerminalUtility;
    } else {
      move.state = publicState(move.state);
      move.state.next_disc =
          cfpi::detail::sampledNextDisc(chance_seed, 0, 1);
      value += fair::fairLeaf(move.state);
    }
    if (value > best) {
      best = value;
      selected = action;
    }
  }
  if (selected < 0) selected = centerFirstMove(state.board);
  return mirrored && selected >= 0 ? kBoardSize - 1 - selected : selected;
}

struct TapeDomains {
  std::uint32_t reveal = kRevealTapeDomain;
  std::uint32_t visible = kVisibleTapeDomain;
};

struct RevealTape {
  std::uint32_t root_seed = 0;
  int scenario = 0;
  int step = 0;
  std::uint32_t domain = kRevealTapeDomain;
  int event = 0;

  std::uint8_t nextDisc() {
    const int event_index = step * kEventsPerStep + event++;
    const double unit = cfpi::detail::stratifiedUnit(
        root_seed, scenario, kScenarios, domain, event_index);
    return static_cast<std::uint8_t>(
        std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
  }
};

std::uint8_t visibleDisc(std::uint32_t root_seed, int scenario, int step,
                         std::uint32_t domain = kVisibleTapeDomain) {
  const double unit = cfpi::detail::stratifiedUnit(
      root_seed, scenario, kScenarios, domain, step);
  return static_cast<std::uint8_t>(
      std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
}

bool playSyntheticMove(const State& source, int action,
                       std::uint32_t root_seed, int scenario, int step,
                       MoveResult& result, Work* work = nullptr,
                       TapeDomains domains = {}) {
  const State state = publicState(source);
  if (state.game_over) return false;
  Board board = state.board;
  if (!placeDisc(board, action, state.next_disc)) return false;

  RevealTape reveals{root_seed, scenario, step, domains.reveal, 0};
  result = MoveResult{};
  std::int64_t first_score = 0;
  cfpi::detail::resolveCascadeSampled(board, reveals, 1, first_score,
                                      result.waves);
  result.score_delta = first_score;
  result.cleared_board = isBoardEmpty(board);
  if (result.cleared_board) result.score_delta += kClearBonus;

  int moves_remaining = state.moves_remaining - 1;
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
      game_over ? state.next_disc
                : visibleDisc(root_seed, scenario, step, domains.visible);
  result.state.score = 0;
  result.state.level = 1;
  result.state.moves_remaining = moves_remaining;
  result.state.moves_played = 0;
  result.state.game_over = game_over;
  if (work != nullptr) ++work->transitions;
  return true;
}

struct Decision {
  int action = -1;
  std::array<double, kBoardSize> values{};
  std::array<std::array<double, kScenarios>, kBoardSize> scenario_returns{};
  Work work{};
};

Decision chooseRolloutAction(const State& source, const Config& config) {
  Decision result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  for (auto& returns : result.scenario_returns) {
    returns.fill(-std::numeric_limits<double>::infinity());
  }
  if (source.game_over) return result;

  bool mirrored = false;
  const State root = cfpi::detail::canonicalState(publicState(source), mirrored);
  const std::uint32_t tape_seed =
      seed32(publicHash(root) ^ static_cast<std::uint64_t>(kTapeSeedDomain));
  int selected = -1;
  double best = -std::numeric_limits<double>::infinity();

  for (const int action : kColumnOrder) {
    if (!isLegal(root.board, action)) continue;
    double sum = 0.0;
    for (int scenario = 0; scenario < kScenarios; ++scenario) {
      State state = root;
      double value = 0.0;
      for (int step = 0; step < config.horizon && !state.game_over; ++step) {
        const int next_action =
            step == 0 ? action : fairDepthOneAction(state, &result.work);
        if (!isLegal(state.board, next_action)) {
          value += kTerminalUtility;
          state.game_over = true;
          break;
        }
        MoveResult move;
        if (!playSyntheticMove(state, next_action, tape_seed, scenario, step,
                               move, &result.work)) {
          value += kTerminalUtility;
          state.game_over = true;
          break;
        }
        value += static_cast<double>(move.score_delta);
        state = publicState(move.state);
        if (state.game_over) value += kTerminalUtility;
      }
      if (!state.game_over) value += config.tail_scale * fair::fairLeaf(state);
      result.scenario_returns[action][scenario] = value;
      sum += value;
    }
    const double value = sum / static_cast<double>(kScenarios);
    result.values[action] = value;
    if (value > best) {
      best = value;
      selected = action;
    }
  }
  if (selected < 0) selected = centerFirstMove(root.board);

  if (!mirrored) {
    result.action = selected;
    return result;
  }
  std::array<double, kBoardSize> source_values{};
  std::array<std::array<double, kScenarios>, kBoardSize> source_returns{};
  for (int column = 0; column < kBoardSize; ++column) {
    source_values[kBoardSize - 1 - column] = result.values[column];
    source_returns[kBoardSize - 1 - column] = result.scenario_returns[column];
  }
  result.values = source_values;
  result.scenario_returns = source_returns;
  result.action = selected < 0 ? selected : kBoardSize - 1 - selected;
  return result;
}

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  std::int64_t numbered_cleared = 0;
  std::int64_t covers_revealed = 0;
  int clear_boards = 0;
  int maximum_chain = 0;
  bool censored = false;
  double decision_seconds = 0.0;
  std::uint64_t transitions = 0;
  std::uint64_t fair_d1_calls = 0;
};

enum class Policy { kFairD1, kRollout };

GameResult runGame(std::uint32_t seed, Policy policy,
                   const Config* config = nullptr) {
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    const auto started = Clock::now();
    int action = -1;
    Work work;
    if (policy == Policy::kFairD1) {
      action = fairDepthOneAction(publicState(state), &work);
    } else {
      if (config == nullptr) throw std::invalid_argument("missing rollout config");
      const Decision decision = chooseRolloutAction(publicState(state), *config);
      action = decision.action;
      work = decision.work;
      if (work.transitions > kMaximumTransitionsPerDecision) {
        throw std::runtime_error("rollout exceeded preregistered work bound");
      }
    }
    result.decision_seconds +=
        std::chrono::duration<double>(Clock::now() - started).count();
    result.transitions += work.transitions;
    result.fair_d1_calls += work.fair_d1_calls;
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("policy returned illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("headless transition failed");
    }
    for (const Wave& wave : move.waves) {
      result.numbered_cleared += wave.cleared;
      result.covers_revealed += wave.revealed;
      result.maximum_chain = std::max(result.maximum_chain, wave.depth);
    }
    if (move.cleared_board) ++result.clear_boards;
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  return result;
}

std::vector<GameResult> runCohort(std::uint32_t start, int games,
                                  Policy policy, const Config* config,
                                  int threads, std::string_view label) {
  std::vector<GameResult> result(static_cast<std::size_t>(games));
  std::atomic<int> next{0};
  std::atomic<int> completed{0};
  const int worker_count = std::max(1, std::min(threads, games));
  std::vector<std::future<void>> workers;
  workers.reserve(static_cast<std::size_t>(worker_count));
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [&, worker]() {
      static_cast<void>(worker);
      for (;;) {
        const int game = next.fetch_add(1);
        if (game >= games) break;
        const std::uint32_t seed = start + static_cast<std::uint32_t>(game);
        result[static_cast<std::size_t>(game)] = runGame(seed, policy, config);
        const int done = completed.fetch_add(1) + 1;
        const std::lock_guard<std::mutex> lock(progress_mutex);
        std::cerr << "fair-d1-rollout " << label << ' ' << done << '/'
                  << games << " seed 0x" << std::hex << seed << std::dec
                  << " score " << result[static_cast<std::size_t>(game)].score
                  << " moves " << result[static_cast<std::size_t>(game)].moves
                  << '\n';
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

double quantile(std::vector<double> values, double probability) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const double position = probability * static_cast<double>(values.size() - 1);
  const std::size_t lower = static_cast<std::size_t>(std::floor(position));
  const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
  const double fraction = position - static_cast<double>(lower);
  return values[lower] * (1.0 - fraction) + values[upper] * fraction;
}

struct Summary {
  int games = 0;
  int censored = 0;
  double mean_score = 0.0;
  double median_score = 0.0;
  double lower_quartile_score = 0.0;
  double minimum_score = 0.0;
  double maximum_score = 0.0;
  double standard_error = 0.0;
  double mean_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double mean_decision_ms = 0.0;
  double mean_transitions_per_decision = 0.0;
  std::int64_t total_clears = 0;
  std::int64_t total_reveals = 0;
  std::int64_t total_moves = 0;
};

Summary summarize(const std::vector<GameResult>& games,
                  std::optional<int> omitted = std::nullopt) {
  Summary result;
  std::vector<double> scores;
  double score_sum = 0.0;
  double score_square_sum = 0.0;
  double decision_seconds = 0.0;
  std::uint64_t transitions = 0;
  for (std::size_t index = 0; index < games.size(); ++index) {
    if (omitted.has_value() && static_cast<int>(index) == *omitted) continue;
    const GameResult& game = games[index];
    ++result.games;
    result.censored += game.censored ? 1 : 0;
    const double score = static_cast<double>(game.score);
    scores.push_back(score);
    score_sum += score;
    score_square_sum += score * score;
    result.total_moves += game.moves;
    result.total_clears += game.numbered_cleared;
    result.total_reveals += game.covers_revealed;
    decision_seconds += game.decision_seconds;
    transitions += game.transitions;
  }
  if (result.games == 0) return result;
  result.mean_score = score_sum / result.games;
  result.median_score = quantile(scores, 0.5);
  result.lower_quartile_score = quantile(scores, 0.25);
  result.minimum_score = *std::min_element(scores.begin(), scores.end());
  result.maximum_score = *std::max_element(scores.begin(), scores.end());
  if (result.games > 1) {
    const double variance = std::max(
        0.0, (score_square_sum - score_sum * score_sum / result.games) /
                 static_cast<double>(result.games - 1));
    result.standard_error = std::sqrt(variance / result.games);
  }
  result.mean_moves = static_cast<double>(result.total_moves) / result.games;
  if (result.total_moves > 0) {
    result.clears_per_move =
        static_cast<double>(result.total_clears) / result.total_moves;
    result.reveals_per_move =
        static_cast<double>(result.total_reveals) / result.total_moves;
    result.mean_decision_ms =
        1'000.0 * decision_seconds / result.total_moves;
    result.mean_transitions_per_decision =
        static_cast<double>(transitions) / result.total_moves;
  }
  return result;
}

struct LeaveOneOut {
  std::array<int, kConfigs.size()> winner_counts{};
  int global_score_wins = 0;
  int global_throughput_wins = 0;
  bool gains_survive = false;
  double global_mean_min = std::numeric_limits<double>::infinity();
  double global_mean_max = -std::numeric_limits<double>::infinity();
};

int selectConfig(const std::array<Summary, kConfigs.size()>& summaries) {
  int selected = 0;
  for (std::size_t index = 1; index < summaries.size(); ++index) {
    if (summaries[index].mean_score > summaries[selected].mean_score ||
        (summaries[index].mean_score == summaries[selected].mean_score &&
         summaries[index].clears_per_move >
             summaries[selected].clears_per_move)) {
      selected = static_cast<int>(index);
    }
  }
  return selected;
}

LeaveOneOut leaveOneOut(
    const std::vector<GameResult>& baseline,
    const std::array<std::vector<GameResult>, kConfigs.size()>& candidates,
    int global_champion) {
  LeaveOneOut result;
  for (int omitted = 0; omitted < kFittingGames; ++omitted) {
    std::array<Summary, kConfigs.size()> summaries{};
    for (std::size_t config = 0; config < kConfigs.size(); ++config) {
      summaries[config] = summarize(candidates[config], omitted);
    }
    const int winner = selectConfig(summaries);
    ++result.winner_counts[static_cast<std::size_t>(winner)];
    const Summary baseline_summary = summarize(baseline, omitted);
    const Summary champion_summary =
        summaries[static_cast<std::size_t>(global_champion)];
    result.global_mean_min =
        std::min(result.global_mean_min, champion_summary.mean_score);
    result.global_mean_max =
        std::max(result.global_mean_max, champion_summary.mean_score);
    if (champion_summary.mean_score > baseline_summary.mean_score) {
      ++result.global_score_wins;
    }
    if (champion_summary.clears_per_move >=
        baseline_summary.clears_per_move * kMinimumClearThroughputRatio) {
      ++result.global_throughput_wins;
    }
  }
  result.gains_survive =
      result.global_score_wins >= kMinimumLooScoreWins &&
      result.global_throughput_wins >= kMinimumLooScoreWins;
  return result;
}

std::uint64_t peakRssBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
#endif
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

State asymmetricFixture() {
  State state;
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

void runSelfTests() {
  const Config config{8, 0.25};
  const State fixture = asymmetricFixture();
  const Decision first = chooseRolloutAction(fixture, config);
  const Decision second = chooseRolloutAction(fixture, config);
  expect(first.action == second.action && first.values == second.values &&
             first.scenario_returns == second.scenario_returns &&
             first.work.transitions == second.work.transitions,
         "rollout must be deterministic");
  expect(first.work.transitions <= kMaximumTransitionsPerDecision,
         "rollout work bound failed");

  State metadata = fixture;
  metadata.score = 9'876'543;
  metadata.level = 73;
  metadata.moves_played = 812;
  const Decision metadata_decision = chooseRolloutAction(metadata, config);
  expect(first.action == metadata_decision.action &&
             first.values == metadata_decision.values &&
             first.scenario_returns == metadata_decision.scenario_returns,
         "rollout used hidden score/level/move metadata");
  expect(fairDepthOneAction(fixture) == fairDepthOneAction(metadata),
         "fair D1 continuation used hidden metadata");

  State mirrored = publicState(fixture);
  mirrored.board = cfpi::detail::mirrorBoard(fixture.board);
  const Decision mirrored_decision = chooseRolloutAction(mirrored, config);
  expect(mirrored_decision.action == kBoardSize - 1 - first.action,
         "root action was not reflection equivariant");
  for (int column = 0; column < kBoardSize; ++column) {
    expect(mirrored_decision.values[kBoardSize - 1 - column] ==
               first.values[column],
           "root values were not reflection equivariant");
    expect(mirrored_decision.scenario_returns[kBoardSize - 1 - column] ==
               first.scenario_returns[column],
           "aligned return vectors were not reflection equivariant");
  }

  std::array<int, kBoardSize> visible_counts{};
  constexpr std::uint32_t tape_seed = 0x1234'5678u;
  for (int scenario = 0; scenario < kScenarios; ++scenario) {
    ++visible_counts[visibleDisc(tape_seed, scenario, 0) - 1];
  }
  for (const int count : visible_counts) {
    expect(count == 1, "first future visible disc was not stratified");
  }
  RevealTape aligned_left{tape_seed, 2, 3, kRevealTapeDomain, 0};
  RevealTape aligned_right{tape_seed, 2, 3, kRevealTapeDomain, 0};
  for (int event = 0; event < 12; ++event) {
    expect(aligned_left.nextDisc() == aligned_right.nextDisc(),
           "sibling reveal tapes were not aligned");
  }

  State reveal_fixture;
  reveal_fixture.board.fill(kEmpty);
  reveal_fixture.board[indexOf(6, 1)] = kCracked;
  reveal_fixture.next_disc = 1;
  reveal_fixture.moves_remaining = 4;
  MoveResult standard;
  expect(playSyntheticMove(reveal_fixture, 0, tape_seed, 0, 0, standard),
         "domain fixture transition failed");
  bool found_visible_change = false;
  bool found_reveal_change = false;
  for (std::uint32_t salt = 1; salt < 256; ++salt) {
    MoveResult changed_visible;
    expect(playSyntheticMove(
               reveal_fixture, 0, tape_seed, 0, 0, changed_visible, nullptr,
               {kRevealTapeDomain, kVisibleTapeDomain ^ salt}),
           "visible-domain transition failed");
    if (changed_visible.state.next_disc != standard.state.next_disc) {
      expect(changed_visible.state.board == standard.state.board &&
                 changed_visible.score_delta == standard.score_delta &&
                 changed_visible.waves.size() == standard.waves.size(),
             "visible domain leaked into reveal mechanics");
      found_visible_change = true;
    }
    MoveResult changed_reveal;
    expect(playSyntheticMove(
               reveal_fixture, 0, tape_seed, 0, 0, changed_reveal, nullptr,
               {kRevealTapeDomain ^ salt, kVisibleTapeDomain}),
           "reveal-domain transition failed");
    if (changed_reveal.state.board != standard.state.board) {
      expect(changed_reveal.state.next_disc == standard.state.next_disc,
             "reveal domain leaked into visible-disc tape");
      found_reveal_change = true;
    }
  }
  expect(found_visible_change && found_reveal_change,
         "domain separation fixture was not discriminating");
  expect(samePublicState(publicState(metadata), fixture),
         "public-state normalization failed");
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"games\":" << summary.games
         << ",\"censored\":" << summary.censored
         << ",\"correctedMeanScore\":" << summary.mean_score
         << ",\"medianScore\":" << summary.median_score
         << ",\"lowerQuartileScore\":" << summary.lower_quartile_score
         << ",\"minimumScore\":" << summary.minimum_score
         << ",\"maximumScore\":" << summary.maximum_score
         << ",\"standardError\":" << summary.standard_error
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"numberedClearsPerMove\":" << summary.clears_per_move
         << ",\"coversRevealedPerMove\":" << summary.reveals_per_move
         << ",\"meanDecisionMs\":" << summary.mean_decision_ms
         << ",\"meanTransitionsPerDecision\":"
         << summary.mean_transitions_per_decision
         << ",\"totalMoves\":" << summary.total_moves
         << ",\"totalNumberedClears\":" << summary.total_clears
         << ",\"totalCoversRevealed\":" << summary.total_reveals << '}';
}

void writeGames(std::ostream& output, const std::vector<GameResult>& games) {
  output << '[';
  for (std::size_t index = 0; index < games.size(); ++index) {
    if (index > 0) output << ',';
    const GameResult& game = games[index];
    output << "{\"seed\":" << game.seed << ",\"score\":" << game.score
           << ",\"moves\":" << game.moves
           << ",\"numberedCleared\":" << game.numbered_cleared
           << ",\"coversRevealed\":" << game.covers_revealed
           << ",\"clearBoards\":" << game.clear_boards
           << ",\"maximumChain\":" << game.maximum_chain
           << ",\"censored\":" << (game.censored ? "true" : "false")
           << ",\"decisionSeconds\":" << game.decision_seconds
           << ",\"transitions\":" << game.transitions
           << ",\"fairD1Calls\":" << game.fair_d1_calls << '}';
  }
  output << ']';
}

struct Options {
  std::string output = "/tmp/drop7-fair-d1-rollout-improvement.json";
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
      if (result.threads < 1 || result.threads > 64) {
        throw std::invalid_argument("threads must be in [1,64]");
      }
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  return result;
}

int run(const Options& options) {
  runSelfTests();
  std::cerr << "fair-d1-rollout self-tests passed\n";
  if (options.self_test_only) return 0;
  const auto total_started = Clock::now();

  // Complete and report this baseline before fitting the rollout policy.
  const std::vector<GameResult> fitting_baseline = runCohort(
      kFittingStart, kFittingGames, Policy::kFairD1, nullptr,
      options.threads, "fitting-fair-d1");
  const Summary fitting_baseline_summary = summarize(fitting_baseline);
  std::cerr << "fair-d1 benchmark corrected mean "
            << fitting_baseline_summary.mean_score << " clears/move "
            << fitting_baseline_summary.clears_per_move << '\n';

  std::array<std::vector<GameResult>, kConfigs.size()> fitting_candidates;
  std::array<Summary, kConfigs.size()> fitting_summaries{};
  for (std::size_t index = 0; index < kConfigs.size(); ++index) {
    std::ostringstream label;
    label << "fitting-h" << kConfigs[index].horizon << "-tail"
          << kConfigs[index].tail_scale;
    fitting_candidates[index] = runCohort(
        kFittingStart, kFittingGames, Policy::kRollout, &kConfigs[index],
        options.threads, label.str());
    fitting_summaries[index] = summarize(fitting_candidates[index]);
  }

  const int champion = selectConfig(fitting_summaries);
  const Summary& champion_summary =
      fitting_summaries[static_cast<std::size_t>(champion)];
  const LeaveOneOut loo =
      leaveOneOut(fitting_baseline, fitting_candidates, champion);
  const bool complete = fitting_baseline_summary.censored == 0 &&
                        champion_summary.censored == 0;
  const bool score_gate = champion_summary.mean_score >= kMinimumFittingMean;
  const bool throughput_gate =
      champion_summary.clears_per_move >=
      fitting_baseline_summary.clears_per_move *
          kMinimumClearThroughputRatio;
  const bool fitting_gate =
      complete && score_gate && throughput_gate && loo.gains_survive;

  std::vector<GameResult> heldout_baseline;
  std::vector<GameResult> heldout_candidate;
  Summary heldout_baseline_summary;
  Summary heldout_candidate_summary;
  bool heldout_passed = false;
  if (fitting_gate) {
    heldout_baseline = runCohort(kHeldoutStart, kHeldoutGames,
                                 Policy::kFairD1, nullptr, options.threads,
                                 "heldout-fair-d1");
    heldout_candidate = runCohort(
        kHeldoutStart, kHeldoutGames, Policy::kRollout,
        &kConfigs[static_cast<std::size_t>(champion)], options.threads,
        "heldout-frozen-rollout");
    heldout_baseline_summary = summarize(heldout_baseline);
    heldout_candidate_summary = summarize(heldout_candidate);
    heldout_passed =
        heldout_baseline_summary.censored == 0 &&
        heldout_candidate_summary.censored == 0 &&
        heldout_candidate_summary.mean_score >
            heldout_baseline_summary.mean_score &&
        heldout_candidate_summary.clears_per_move >=
            heldout_baseline_summary.clears_per_move *
                kMinimumClearThroughputRatio;
  }

  const double total_wall =
      std::chrono::duration<double>(Clock::now() - total_started).count();
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open output artifact");
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"fair-d1-rollout-improvement\",\n"
         << "  \"preregistered\":true,\n"
         << "  \"publicStateOnly\":true,\n"
         << "  \"strategyFusionFree\":true,\n"
         << "  \"meanReturnSelection\":true,\n"
         << "  \"cvarUsed\":false,\n"
         << "  \"scoring\":{\"levelBonus\":7000},\n"
         << "  \"seedDiscipline\":{\"fittingStart\":" << kFittingStart
         << ",\"fittingGames\":" << kFittingGames
         << ",\"heldoutStart\":" << kHeldoutStart
         << ",\"heldoutGames\":" << kHeldoutGames
         << ",\"forbiddenFamilies\":[\"0x3e\",\"0x7d\",\"0xd7\"]},\n"
         << "  \"tapes\":{\"scenarios\":" << kScenarios
         << ",\"alignedAcrossRootActions\":true,"
            "\"eventIndexed\":true,\"revealVisibleDomainsSeparate\":true,"
            "\"futureTapeVisibleToContinuation\":false,"
            "\"firstFutureDiscExactlyStratified\":true},\n"
         << "  \"continuation\":{\"policy\":\"fair-depth-one\","
            "\"publicStateOnly\":true},\n"
         << "  \"grid\":[";
  for (std::size_t index = 0; index < kConfigs.size(); ++index) {
    if (index > 0) output << ',';
    output << "{\"horizon\":" << kConfigs[index].horizon
           << ",\"tailScale\":" << kConfigs[index].tail_scale << '}';
  }
  output << "],\n  \"resourceBound\":{\"maximumMoves\":" << kMaximumMoves
         << ",\"maximumTransitionsPerDecision\":"
         << kMaximumTransitionsPerDecision
         << ",\"threads\":" << options.threads << "},\n"
         << "  \"gate\":{\"minimumCorrectedFittingMean\":"
         << kMinimumFittingMean
         << ",\"minimumClearThroughputRatio\":"
         << kMinimumClearThroughputRatio
         << ",\"minimumLeaveOneOutWins\":" << kMinimumLooScoreWins
         << "},\n  \"fittingFairD1\":{\"summary\":";
  writeSummary(output, fitting_baseline_summary);
  output << ",\"games\":";
  writeGames(output, fitting_baseline);
  output << "},\n  \"fittingCandidates\":[";
  for (std::size_t index = 0; index < kConfigs.size(); ++index) {
    if (index > 0) output << ',';
    output << "{\"configIndex\":" << index << ",\"summary\":";
    writeSummary(output, fitting_summaries[index]);
    output << ",\"games\":";
    writeGames(output, fitting_candidates[index]);
    output << '}';
  }
  output << "],\n  \"selection\":{\"championIndex\":" << champion
         << ",\"horizon\":"
         << kConfigs[static_cast<std::size_t>(champion)].horizon
         << ",\"tailScale\":"
         << kConfigs[static_cast<std::size_t>(champion)].tail_scale
         << ",\"scoreGate\":" << (score_gate ? "true" : "false")
         << ",\"throughputGate\":"
         << (throughput_gate ? "true" : "false")
         << ",\"completeGameGate\":" << (complete ? "true" : "false")
         << ",\"fittingGatePassed\":"
         << (fitting_gate ? "true" : "false") << "},\n"
         << "  \"leaveOneOut\":{\"winnerCounts\":[";
  for (std::size_t index = 0; index < loo.winner_counts.size(); ++index) {
    if (index > 0) output << ',';
    output << loo.winner_counts[index];
  }
  output << "],\"globalChampionScoreWins\":" << loo.global_score_wins
         << ",\"globalChampionThroughputWins\":"
         << loo.global_throughput_wins
         << ",\"globalChampionMeanRange\":[" << loo.global_mean_min << ','
         << loo.global_mean_max << "],\"gainsSurvive\":"
         << (loo.gains_survive ? "true" : "false") << "},\n"
         << "  \"heldoutRan\":" << (fitting_gate ? "true" : "false")
         << ",\n  \"heldout\":";
  if (!fitting_gate) {
    output << "null";
  } else {
    output << "{\"baselineSummary\":";
    writeSummary(output, heldout_baseline_summary);
    output << ",\"candidateSummary\":";
    writeSummary(output, heldout_candidate_summary);
    output << ",\"passed\":" << (heldout_passed ? "true" : "false")
           << ",\"baselineGames\":";
    writeGames(output, heldout_baseline);
    output << ",\"candidateGames\":";
    writeGames(output, heldout_candidate);
    output << '}';
  }
  output << ",\n  \"qualified\":"
         << (fitting_gate && heldout_passed ? "true" : "false")
         << ",\n  \"totalWallSeconds\":" << total_wall
         << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
  output.close();

  std::cout << std::setprecision(10)
            << "fair D1 mean=" << fitting_baseline_summary.mean_score
            << " clears/move=" << fitting_baseline_summary.clears_per_move
            << '\n'
            << "champion=h"
            << kConfigs[static_cast<std::size_t>(champion)].horizon
            << " tail="
            << kConfigs[static_cast<std::size_t>(champion)].tail_scale
            << " mean=" << champion_summary.mean_score
            << " clears/move=" << champion_summary.clears_per_move
            << " loo=" << loo.global_score_wins << '/'
            << kFittingGames << " score, " << loo.global_throughput_wins
            << '/' << kFittingGames << " throughput\n"
            << "fitting gate=" << (fitting_gate ? "pass" : "fail")
            << " heldout="
            << (fitting_gate ? (heldout_passed ? "pass" : "fail")
                             : "not-run")
            << " artifact=" << options.output << '\n';
  return fitting_gate && heldout_passed ? 0 : 2;
}

}  // namespace drop7::fair_d1_rollout_improvement

#ifndef DROP7_FAIR_D1_ROLLOUT_IMPROVEMENT_LIBRARY
int main(int argc, char** argv) {
  try {
    const auto options =
        drop7::fair_d1_rollout_improvement::parseOptions(argc, argv);
    return drop7::fair_d1_rollout_improvement::run(options);
  } catch (const std::exception& error) {
    std::cerr << "drop7_fair_d1_rollout_improvement: " << error.what()
              << '\n';
    return 1;
  }
}
#endif
