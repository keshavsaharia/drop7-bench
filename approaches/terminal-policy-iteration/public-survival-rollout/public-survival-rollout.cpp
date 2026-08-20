// Evaluates each root action with a public-only 100-move survival rollout
// around the reference fair-D1 continuation policy.
//
// Every root action is evaluated on 31 aligned, event-indexed synthetic chance
// scenarios.  The root action is fixed; all later actions are freshly chosen
// by the same exact, completed fair-D1 function of board, visible next disc,
// rise phase, and terminal status.  Neither the real game seed nor synthetic
// scenario identity is available to that continuation policy.

#define DROP7_FAIR_ONLY_HORIZON_LIBRARY
#include "../../fair-expectimax/reference/fair-only-horizon.cpp"
#undef DROP7_FAIR_ONLY_HORIZON_LIBRARY

#include <atomic>
#include <cstdint>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace drop7::public_survival_rollout {

namespace fair = drop7::fair_only_horizon;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kScreenSeedStart = 0x3d61'0000u;
constexpr int kScreenGames = 4;
constexpr int kScreenMaximumMoves = 500;
constexpr std::uint32_t kDevelopmentSeedStart = 0x4d61'0000u;
constexpr int kDevelopmentGames = 8;
constexpr int kDevelopmentMaximumMoves = 1'000;
constexpr int kHorizon = 100;
constexpr int kScenarios = 31;
constexpr double kSurvivalReward = 3'400.0;
constexpr double kTerminalPenalty = -1'000'000.0;
constexpr int kDefaultThreads = 4;
constexpr std::uint64_t kMaximumRssBytes = 256ull * 1024ull * 1024ull;
constexpr double kMaximumWallSeconds = 30.0 * 60.0;
constexpr std::uint32_t kTapeSeedDomain = 0x5352'4f54u;     // "SROT"
constexpr std::uint32_t kRevealTapeDomain = 0x5352'564cu;   // "SRVL"
constexpr std::uint32_t kVisibleTapeDomain = 0x5356'4953u;  // "SVIS"
constexpr int kEventsPerStep = 64;
constexpr int kMaximumFairD1WorkPerCall =
    kBoardSize * fair::kChanceSamples * 2;
constexpr std::uint64_t kMaximumWorkPerDecision =
    static_cast<std::uint64_t>(kBoardSize) * kScenarios *
    (kHorizon + (kHorizon - 1) * kMaximumFairD1WorkPerCall);

static_assert(kLevelBonus == 17'000);
static_assert(fair::kChanceSamples == 5);
static_assert(kHorizon == 100 && kScenarios == 31);
static_assert(kEventsPerStep > kCellCount);
static_assert(kMaximumWorkPerDecision == 1'525'510);
static_assert((kScreenSeedStart >> 24u) == 0x3du);
static_assert((kDevelopmentSeedStart >> 24u) == 0x4du);
static_assert(kScreenSeedStart != 0x3d30'0000u &&
              kScreenSeedStart != 0x3d40'0000u &&
              kScreenSeedStart != 0x3d50'0000u &&
              kScreenSeedStart != 0x3d60'0000u);
static_assert((kScreenSeedStart >> 24u) != 0x7du &&
              (kScreenSeedStart >> 24u) != 0xd7u);
static_assert((kDevelopmentSeedStart >> 24u) != 0x7du &&
              (kDevelopmentSeedStart >> 24u) != 0xd7u);

std::mutex progress_mutex;

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

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

bool samePublicState(const State& first_source,
                     const State& second_source) {
  const State first = publicState(first_source);
  const State second = publicState(second_source);
  return first.board == second.board &&
         first.next_disc == second.next_disc &&
         first.moves_remaining == second.moves_remaining &&
         first.game_over == second.game_over;
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

bool assignedSeedRange(std::uint32_t start, int games, int maximum_moves) {
  return (start == kScreenSeedStart && games == kScreenGames &&
          maximum_moves == kScreenMaximumMoves) ||
         (start == kDevelopmentSeedStart && games == kDevelopmentGames &&
          maximum_moves == kDevelopmentMaximumMoves);
}

struct Work {
  std::uint64_t synthetic_transitions = 0;
  std::uint64_t fair_d1_calls = 0;
  std::uint64_t fair_d1_work = 0;

  std::uint64_t total() const {
    return synthetic_transitions + fair_d1_work;
  }
};

struct FairD1Decision {
  int action = -1;
  std::array<double, kBoardSize> values{};
  std::uint64_t work = 0;
};

FairD1Decision fairDepthOneDecision(const State& source) {
  FairD1Decision result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  if (source.game_over) return result;
  bool mirrored = false;
  const State state =
      cfpi::detail::canonicalState(publicState(source), mirrored);
  const std::uint32_t chance_seed = cfpi::detail::scenarioSeedForState(
      state, fair::kPolicySeed, 1);
  int selected = -1;
  double best = -std::numeric_limits<double>::infinity();
  std::array<double, kBoardSize> canonical_values{};
  canonical_values.fill(-std::numeric_limits<double>::infinity());

  for (const int action : cfpi::detail::kColumnOrder) {
    if (!isLegal(state.board, action)) continue;
    double sum = 0.0;
    for (int sample = 0; sample < fair::kChanceSamples; ++sample) {
      cfpi::detail::StratifiedRandom random{
          chance_seed, sample, fair::kChanceSamples, 0};
      MoveResult move;
      if (!cfpi::detail::playMoveSampled(state, action, random, move)) {
        sum += fair::kTerminalUtility;
        continue;
      }
      ++result.work;
      double value = static_cast<double>(move.score_delta);
      if (move.state.game_over) {
        value += fair::kTerminalUtility;
      } else {
        move.state = publicState(move.state);
        move.state.next_disc = cfpi::detail::sampledNextDisc(
            chance_seed, sample, fair::kChanceSamples);
        bool ignored = false;
        move.state = cfpi::detail::canonicalState(move.state, ignored);
        value += fair::fairLeaf(move.state);
        ++result.work;
      }
      sum += value;
    }
    const double value = sum / fair::kChanceSamples;
    canonical_values[action] = value;
    if (value > best) {
      best = value;
      selected = action;
    }
  }
  if (selected < 0) selected = centerFirstMove(state.board);
  for (int canonical_column = 0; canonical_column < kBoardSize;
       ++canonical_column) {
    const int source_column =
        mirrored ? kBoardSize - 1 - canonical_column : canonical_column;
    result.values[source_column] = canonical_values[canonical_column];
  }
  result.action = mirrored ? kBoardSize - 1 - selected : selected;
  return result;
}

int fairDepthOneAction(const State& source, Work* work = nullptr) {
  const FairD1Decision decision = fairDepthOneDecision(publicState(source));
  if (work != nullptr) {
    ++work->fair_d1_calls;
    work->fair_d1_work += decision.work;
  }
  return decision.action;
}

struct TapeDomains {
  std::uint32_t reveal = kRevealTapeDomain;
  std::uint32_t visible = kVisibleTapeDomain;
};

struct RevealTape {
  std::uint32_t root_seed = 0;
  int scenario = 0;
  int scenario_count = kScenarios;
  int step = 0;
  std::uint32_t domain = kRevealTapeDomain;
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
                         std::uint32_t domain = kVisibleTapeDomain) {
  const double unit = cfpi::detail::stratifiedUnit(
      root_seed, scenario, scenario_count, domain, step);
  return static_cast<std::uint8_t>(
      std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
}

bool playSyntheticMove(const State& source, int action,
                       std::uint32_t root_seed, int scenario,
                       int scenario_count, int step, MoveResult& result,
                       Work* work = nullptr, TapeDomains domains = {}) {
  const State state = publicState(source);
  if (state.game_over) return false;
  Board board = state.board;
  if (!placeDisc(board, action, state.next_disc)) return false;

  RevealTape reveals{
      root_seed, scenario, scenario_count, step, domains.reveal, 0};
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
                : visibleDisc(root_seed, scenario, scenario_count, step,
                              domains.visible);
  result.state.score = 0;
  result.state.level = 1;
  result.state.moves_remaining = moves_remaining;
  result.state.moves_played = 0;
  result.state.game_over = game_over;
  if (work != nullptr) ++work->synthetic_transitions;
  return true;
}

double scenarioReturn(std::int64_t score_delta, int survived_moves,
                      bool terminal_before_cutoff, double cutoff_leaf) {
  double value = static_cast<double>(score_delta) +
                 kSurvivalReward * survived_moves;
  value += terminal_before_cutoff ? kTerminalPenalty : cutoff_leaf;
  return value;
}

struct Decision {
  int action = -1;
  std::array<double, kBoardSize> values{};
  std::array<std::array<double, kScenarios>, kBoardSize> returns{};
  std::array<std::array<std::uint8_t, kScenarios>, kBoardSize> survived{};
  std::array<std::array<std::uint16_t, kScenarios>, kBoardSize> clears{};
  std::array<std::array<std::uint16_t, kScenarios>, kBoardSize> reveals{};
  Work work{};
};

class WallLimitReached : public std::exception {};

Decision chooseSurvivalAction(const State& source, int horizon = kHorizon,
                              int scenario_count = kScenarios,
                              const Clock::time_point* deadline = nullptr) {
  if (horizon < 1 || horizon > kHorizon || scenario_count < 1 ||
      scenario_count > kScenarios) {
    throw std::invalid_argument("invalid survival-rollout dimensions");
  }
  Decision result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  for (auto& action_returns : result.returns) {
    action_returns.fill(-std::numeric_limits<double>::infinity());
  }
  if (source.game_over) return result;

  bool mirrored = false;
  const State root =
      cfpi::detail::canonicalState(publicState(source), mirrored);
  const std::uint32_t tape_seed =
      seed32(publicHash(root) ^ static_cast<std::uint64_t>(kTapeSeedDomain));
  int selected = -1;
  double best = -std::numeric_limits<double>::infinity();

  for (const int root_action : cfpi::detail::kColumnOrder) {
    if (deadline != nullptr && Clock::now() >= *deadline) {
      throw WallLimitReached{};
    }
    if (!isLegal(root.board, root_action)) continue;
    double sum = 0.0;
    for (int scenario = 0; scenario < scenario_count; ++scenario) {
      if (deadline != nullptr && Clock::now() >= *deadline) {
        throw WallLimitReached{};
      }
      State state = root;
      std::int64_t score_delta = 0;
      int survived_moves = 0;
      int clears = 0;
      int reveals = 0;
      for (int step = 0; step < horizon && !state.game_over; ++step) {
        if (deadline != nullptr && Clock::now() >= *deadline) {
          throw WallLimitReached{};
        }
        // Crucially, scenario and tape identity are not arguments to this
        // continuation policy.  It is recomputed from the new public state.
        const int action =
            step == 0 ? root_action : fairDepthOneAction(state, &result.work);
        if (!isLegal(state.board, action)) {
          throw std::runtime_error("fair-D1 continuation returned illegal move");
        }
        MoveResult move;
        if (!playSyntheticMove(state, action, tape_seed, scenario,
                               scenario_count, step, move, &result.work)) {
          throw std::runtime_error("synthetic transition rejected legal move");
        }
        score_delta += move.score_delta;
        for (const Wave& wave : move.waves) {
          clears += wave.cleared;
          reveals += wave.revealed;
        }
        state = publicState(move.state);
        if (!state.game_over) ++survived_moves;
      }
      const bool terminal = state.game_over;
      const double cutoff_leaf = terminal ? 0.0 : fair::fairLeaf(state);
      const double value =
          scenarioReturn(score_delta, survived_moves, terminal, cutoff_leaf);
      result.returns[root_action][scenario] = value;
      result.survived[root_action][scenario] =
          static_cast<std::uint8_t>(survived_moves);
      result.clears[root_action][scenario] =
          static_cast<std::uint16_t>(clears);
      result.reveals[root_action][scenario] =
          static_cast<std::uint16_t>(reveals);
      sum += value;
    }
    const double value = sum / scenario_count;
    result.values[root_action] = value;
    if (value > best) {
      best = value;
      selected = root_action;
    }
  }
  if (selected < 0) selected = centerFirstMove(root.board);
  if (!mirrored) {
    result.action = selected;
  } else {
    std::array<double, kBoardSize> source_values{};
    std::array<std::array<double, kScenarios>, kBoardSize> source_returns{};
    std::array<std::array<std::uint8_t, kScenarios>, kBoardSize>
        source_survived{};
    std::array<std::array<std::uint16_t, kScenarios>, kBoardSize>
        source_clears{};
    std::array<std::array<std::uint16_t, kScenarios>, kBoardSize>
        source_reveals{};
    for (int canonical_column = 0; canonical_column < kBoardSize;
         ++canonical_column) {
      const int source_column = kBoardSize - 1 - canonical_column;
      source_values[source_column] = result.values[canonical_column];
      source_returns[source_column] = result.returns[canonical_column];
      source_survived[source_column] = result.survived[canonical_column];
      source_clears[source_column] = result.clears[canonical_column];
      source_reveals[source_column] = result.reveals[canonical_column];
    }
    result.values = source_values;
    result.returns = source_returns;
    result.survived = source_survived;
    result.clears = source_clears;
    result.reveals = source_reveals;
    result.action = kBoardSize - 1 - selected;
  }
  if (horizon == kHorizon && scenario_count == kScenarios &&
      result.work.total() > kMaximumWorkPerDecision) {
    throw std::runtime_error("survival rollout exceeded work proof");
  }
  return result;
}

struct RootTrace {
  std::uint32_t origin_seed = 0;
  int origin_move = 0;
  State state{};
  Decision decision{};
};

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool natural = false;
  bool capped = false;
  bool resource_limited = false;
  std::uint64_t numbered_cleared = 0;
  std::uint64_t covers_revealed = 0;
  int maximum_chain = 0;
  std::uint64_t work = 0;
  std::uint64_t fair_d1_calls = 0;
  double decision_seconds = 0.0;
};

enum class Policy { kFairD1, kSurvivalRollout };

GameResult runGame(std::uint32_t seed, int maximum_moves, Policy policy,
                   Clock::time_point deadline,
                   std::vector<RootTrace>* traces = nullptr) {
  GameResult result;
  result.seed = seed;
  State state = initialHeadlessState(seed);
  while (!state.game_over && state.moves_played < maximum_moves) {
    if (Clock::now() >= deadline) {
      result.resource_limited = true;
      break;
    }
    const State visible = publicState(state);
    const auto started = Clock::now();
    int action = -1;
    Work work;
    if (policy == Policy::kFairD1) {
      action = fairDepthOneAction(visible, &work);
    } else {
      Decision decision;
      try {
        decision = chooseSurvivalAction(visible, kHorizon, kScenarios,
                                        &deadline);
      } catch (const WallLimitReached&) {
        result.resource_limited = true;
        break;
      }
      action = decision.action;
      work = decision.work;
      if (traces != nullptr) {
        traces->push_back(
            RootTrace{seed, state.moves_played, visible, decision});
      }
    }
    result.decision_seconds +=
        std::chrono::duration<double>(Clock::now() - started).count();
    result.work += work.total();
    result.fair_d1_calls += work.fair_d1_calls;
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("game policy returned illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("headless transition rejected legal action");
    }
    for (const Wave& wave : move.waves) {
      result.numbered_cleared += static_cast<std::uint64_t>(wave.cleared);
      result.covers_revealed += static_cast<std::uint64_t>(wave.revealed);
      result.maximum_chain = std::max(result.maximum_chain, wave.depth);
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.natural = state.game_over;
  result.capped = !state.game_over && !result.resource_limited &&
                  state.moves_played >= maximum_moves;
  return result;
}

struct PairedCohort {
  std::vector<GameResult> baseline;
  std::vector<GameResult> candidate;
  std::vector<std::vector<RootTrace>> traces;
  double wall_seconds = 0.0;
};

PairedCohort runCohort(std::uint32_t seed_start, int games,
                       int maximum_moves, int threads,
                       Clock::time_point deadline,
                       std::string_view label) {
  if (!assignedSeedRange(seed_start, games, maximum_moves)) {
    throw std::invalid_argument("cohort escaped assigned 0x3d61/0x4d61 range");
  }
  const auto started = Clock::now();
  PairedCohort result;
  result.baseline.resize(static_cast<std::size_t>(games));
  result.candidate.resize(static_cast<std::size_t>(games));
  result.traces.resize(static_cast<std::size_t>(games));
  std::atomic<int> next{0};
  std::vector<std::future<void>> workers;
  const int worker_count = std::max(1, std::min(threads, games));
  workers.reserve(static_cast<std::size_t>(worker_count));
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [&, worker] {
      static_cast<void>(worker);
      for (;;) {
        const int game = next.fetch_add(1);
        if (game >= games) return;
        const std::uint32_t seed =
            seed_start + static_cast<std::uint32_t>(game);
        result.baseline[static_cast<std::size_t>(game)] = runGame(
            seed, maximum_moves, Policy::kFairD1, deadline);
        result.candidate[static_cast<std::size_t>(game)] = runGame(
            seed, maximum_moves, Policy::kSurvivalRollout, deadline,
            &result.traces[static_cast<std::size_t>(game)]);
        const std::lock_guard<std::mutex> lock(progress_mutex);
        const GameResult& baseline =
            result.baseline[static_cast<std::size_t>(game)];
        const GameResult& candidate =
            result.candidate[static_cast<std::size_t>(game)];
        std::cerr << "public-survival " << label << " seed 0x" << std::hex
                  << seed << std::dec << " baseline " << baseline.score << '/'
                  << baseline.moves << " candidate " << candidate.score << '/'
                  << candidate.moves
                  << (candidate.resource_limited ? " resource-limited" : "")
                  << '\n';
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.wall_seconds = std::chrono::duration<double>(Clock::now() - started)
                            .count();
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

struct Summary {
  int games = 0;
  int natural = 0;
  int capped = 0;
  int resource_limited = 0;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double mean_decision_ms = 0.0;
  double work_per_move = 0.0;
};

Summary summarize(const std::vector<GameResult>& games) {
  Summary result;
  result.games = static_cast<int>(games.size());
  double score = 0.0;
  double moves = 0.0;
  double clears = 0.0;
  double reveals = 0.0;
  double decision_seconds = 0.0;
  double work = 0.0;
  for (const GameResult& game : games) {
    result.natural += game.natural ? 1 : 0;
    result.capped += game.capped ? 1 : 0;
    result.resource_limited += game.resource_limited ? 1 : 0;
    score += static_cast<double>(game.score);
    moves += game.moves;
    clears += static_cast<double>(game.numbered_cleared);
    reveals += static_cast<double>(game.covers_revealed);
    decision_seconds += game.decision_seconds;
    work += static_cast<double>(game.work);
  }
  if (result.games > 0) {
    result.mean_score = score / result.games;
    result.mean_moves = moves / result.games;
  }
  if (moves > 0.0) {
    result.clears_per_move = clears / moves;
    result.reveals_per_move = reveals / moves;
    result.mean_decision_ms = 1'000.0 * decision_seconds / moves;
    result.work_per_move = work / moves;
  }
  return result;
}

struct PairedSummary {
  double mean_score_difference = 0.0;
  double mean_move_difference = 0.0;
  double score_lower_95 = -std::numeric_limits<double>::infinity();
  double move_lower_95 = -std::numeric_limits<double>::infinity();
  int joint_wins = 0;
};

double oneSidedCritical95(int games) {
  if (games == 4) return 2.3533634348018273;  // t(3), 95th percentile.
  if (games == 8) return 1.8945786050613050;  // t(7), 95th percentile.
  throw std::invalid_argument("unsupported paired cohort size");
}

std::pair<double, double> pairedMeanAndLower95(
    const std::vector<double>& differences) {
  if (differences.size() < 2) {
    return {0.0, -std::numeric_limits<double>::infinity()};
  }
  const double mean = std::accumulate(differences.begin(), differences.end(),
                                      0.0) /
                      differences.size();
  double square_sum = 0.0;
  for (const double difference : differences) {
    const double centered = difference - mean;
    square_sum += centered * centered;
  }
  const double standard_error =
      std::sqrt(square_sum /
                (static_cast<double>(differences.size() - 1) *
                 static_cast<double>(differences.size())));
  const double lower =
      mean - oneSidedCritical95(static_cast<int>(differences.size())) *
                 standard_error;
  return {mean, lower};
}

PairedSummary pairedSummary(const PairedCohort& cohort) {
  if (cohort.baseline.size() != cohort.candidate.size()) {
    throw std::invalid_argument("paired cohort length mismatch");
  }
  std::vector<double> score_differences;
  std::vector<double> move_differences;
  for (std::size_t index = 0; index < cohort.baseline.size(); ++index) {
    const GameResult& baseline = cohort.baseline[index];
    const GameResult& candidate = cohort.candidate[index];
    score_differences.push_back(
        static_cast<double>(candidate.score - baseline.score));
    move_differences.push_back(candidate.moves - baseline.moves);
  }
  const auto score = pairedMeanAndLower95(score_differences);
  const auto moves = pairedMeanAndLower95(move_differences);
  PairedSummary result;
  result.mean_score_difference = score.first;
  result.score_lower_95 = score.second;
  result.mean_move_difference = moves.first;
  result.move_lower_95 = moves.second;
  for (std::size_t index = 0; index < cohort.baseline.size(); ++index) {
    if (cohort.candidate[index].score > cohort.baseline[index].score &&
        cohort.candidate[index].moves > cohort.baseline[index].moves) {
      ++result.joint_wins;
    }
  }
  return result;
}

bool allNatural(const Summary& summary) {
  return summary.natural == summary.games && summary.capped == 0 &&
         summary.resource_limited == 0;
}

bool screenGate(const Summary& baseline, const Summary& candidate,
                const PairedSummary& paired) {
  return allNatural(baseline) && allNatural(candidate) &&
         candidate.mean_score >= 1.5 * baseline.mean_score &&
         candidate.mean_moves >= 1.5 * baseline.mean_moves &&
         candidate.mean_moves >= 150.0 && paired.joint_wins >= 3;
}

bool developmentGate(const Summary& baseline, const Summary& candidate,
                     const PairedSummary& paired) {
  return allNatural(baseline) && allNatural(candidate) &&
         candidate.mean_score >= 1.25 * baseline.mean_score &&
         candidate.mean_moves >= 1.25 * baseline.mean_moves &&
         paired.score_lower_95 >= 0.0 && paired.move_lower_95 >= 0.0;
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"games\":" << summary.games
         << ",\"natural\":" << summary.natural
         << ",\"capped\":" << summary.capped
         << ",\"resourceLimited\":" << summary.resource_limited
         << ",\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"meanDecisionMs\":" << summary.mean_decision_ms
         << ",\"workPerMove\":" << summary.work_per_move << '}';
}

void writePaired(std::ostream& output, const PairedSummary& paired) {
  output << "{\"meanScoreDifference\":"
         << paired.mean_score_difference
         << ",\"meanMoveDifference\":" << paired.mean_move_difference
         << ",\"scoreOneSided95Lower\":" << paired.score_lower_95
         << ",\"moveOneSided95Lower\":" << paired.move_lower_95
         << ",\"jointWins\":" << paired.joint_wins << '}';
}

void writeGames(std::ostream& output,
                const std::vector<GameResult>& games) {
  output << '[';
  for (std::size_t index = 0; index < games.size(); ++index) {
    if (index > 0) output << ',';
    const GameResult& game = games[index];
    output << "{\"seed\":" << game.seed << ",\"score\":" << game.score
           << ",\"moves\":" << game.moves
           << ",\"natural\":" << (game.natural ? "true" : "false")
           << ",\"capped\":" << (game.capped ? "true" : "false")
           << ",\"resourceLimited\":"
           << (game.resource_limited ? "true" : "false")
           << ",\"numberedCleared\":" << game.numbered_cleared
           << ",\"coversRevealed\":" << game.covers_revealed
           << ",\"maximumChain\":" << game.maximum_chain
           << ",\"work\":" << game.work
           << ",\"fairD1Calls\":" << game.fair_d1_calls
           << ",\"decisionSeconds\":" << game.decision_seconds << '}';
  }
  output << ']';
}

void writeCohort(std::ostream& output, const PairedCohort& cohort,
                 const Summary& baseline, const Summary& candidate,
                 const PairedSummary& paired, bool passed) {
  output << "{\"baselineSummary\":";
  writeSummary(output, baseline);
  output << ",\"candidateSummary\":";
  writeSummary(output, candidate);
  output << ",\"paired\":";
  writePaired(output, paired);
  output << ",\"passed\":" << (passed ? "true" : "false")
         << ",\"wallSeconds\":" << cohort.wall_seconds
         << ",\"baselineGames\":";
  writeGames(output, cohort.baseline);
  output << ",\"candidateGames\":";
  writeGames(output, cohort.candidate);
  output << '}';
}

template <typename Value>
void writeArray(std::ostream& output,
                const std::array<Value, kScenarios>& values) {
  output << '[';
  for (int scenario = 0; scenario < kScenarios; ++scenario) {
    if (scenario > 0) output << ',';
    output << +values[scenario];
  }
  output << ']';
}

void writeTraceJsonl(std::ostream& output, std::string_view cohort_name,
                     const PairedCohort& cohort) {
  output << std::setprecision(17);
  for (const auto& game_traces : cohort.traces) {
    for (const RootTrace& trace : game_traces) {
      for (int action = 0; action < kBoardSize; ++action) {
        if (!isLegal(trace.state.board, action)) continue;
        output << "{\"cohort\":\"" << cohort_name
               << "\",\"originSeed\":" << trace.origin_seed
               << ",\"originMove\":" << trace.origin_move
               << ",\"board\":\"" << serializeBoard(trace.state.board)
               << "\",\"nextDisc\":"
               << static_cast<int>(trace.state.next_disc)
               << ",\"movesRemaining\":" << trace.state.moves_remaining
               << ",\"action\":" << action
               << ",\"chosen\":"
               << (trace.decision.action == action ? "true" : "false")
               << ",\"meanReturn\":" << trace.decision.values[action]
               << ",\"scenarioReturns\":";
        writeArray(output, trace.decision.returns[action]);
        output << ",\"survivedMoves\":";
        writeArray(output, trace.decision.survived[action]);
        output << ",\"numberedClears\":";
        writeArray(output, trace.decision.clears[action]);
        output << ",\"coversRevealed\":";
        writeArray(output, trace.decision.reveals[action]);
        output << "}\n";
      }
    }
  }
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
  const State fixture = asymmetricFixture();
  const FairD1Decision d1 = fairDepthOneDecision(fixture);
  bool ignored = false;
  const State canonical =
      cfpi::detail::canonicalState(publicState(fixture), ignored);
  fair::SearchContext exact_context;
  const fair::RootEvaluation exact = fair::rootDecision(canonical, 1,
                                                        exact_context);
  std::array<double, kBoardSize> source_exact_values{};
  for (int canonical_column = 0; canonical_column < kBoardSize;
       ++canonical_column) {
    const int source_column =
        ignored ? kBoardSize - 1 - canonical_column : canonical_column;
    source_exact_values[source_column] = exact.values[canonical_column];
  }
  const int source_exact_action =
      ignored ? kBoardSize - 1 - exact.action : exact.action;
  expect(d1.action == source_exact_action &&
             d1.values == source_exact_values &&
             d1.work == exact_context.work,
         "fresh fair-D1 did not match exact public root decision");

  State metadata = fixture;
  metadata.score = 9'876'543;
  metadata.level = 73;
  metadata.moves_played = 812;
  const FairD1Decision metadata_d1 = fairDepthOneDecision(metadata);
  expect(d1.action == metadata_d1.action && d1.values == metadata_d1.values &&
             d1.work == metadata_d1.work,
         "fair-D1 continuation used hidden metadata");

  const Decision first = chooseSurvivalAction(fixture, 3, 5);
  const Decision second = chooseSurvivalAction(fixture, 3, 5);
  expect(first.action == second.action && first.values == second.values &&
             first.returns == second.returns &&
             first.survived == second.survived &&
             first.clears == second.clears &&
             first.reveals == second.reveals &&
             first.work.total() == second.work.total(),
         "survival rollout was not deterministic");
  const Decision metadata_rollout = chooseSurvivalAction(metadata, 3, 5);
  expect(first.action == metadata_rollout.action &&
             first.values == metadata_rollout.values &&
             first.returns == metadata_rollout.returns,
         "survival rollout used hidden metadata");
  expect(isLegal(fixture.board, first.action),
         "survival rollout returned illegal action");

  State mirrored = publicState(fixture);
  mirrored.board = cfpi::detail::mirrorBoard(fixture.board);
  const Decision reflected = chooseSurvivalAction(mirrored, 3, 5);
  expect(reflected.action == kBoardSize - 1 - first.action,
         "survival rollout action was not reflection safe");
  for (int action = 0; action < kBoardSize; ++action) {
    expect(reflected.values[kBoardSize - 1 - action] == first.values[action] &&
               reflected.returns[kBoardSize - 1 - action] ==
                   first.returns[action],
           "survival rollout values were not reflection safe");
  }

  constexpr std::uint32_t tape_seed = 0x1234'5678u;
  for (int event = 0; event < 12; ++event) {
    std::array<bool, kScenarios> strata{};
    for (int scenario = 0; scenario < kScenarios; ++scenario) {
      const double unit = cfpi::detail::stratifiedUnit(
          tape_seed, scenario, kScenarios, kRevealTapeDomain, event);
      const int stratum = static_cast<int>(std::floor(unit * kScenarios));
      expect(stratum >= 0 && stratum < kScenarios && !strata[stratum],
             "aligned chance repeated a stratum");
      strata[stratum] = true;
    }
    expect(std::all_of(strata.begin(), strata.end(),
                       [](bool present) { return present; }),
           "aligned chance omitted a stratum");
  }
  RevealTape aligned_first{tape_seed, 7, kScenarios, 2,
                           kRevealTapeDomain, 0};
  RevealTape aligned_second = aligned_first;
  for (int event = 0; event < 20; ++event) {
    expect(aligned_first.nextDisc() == aligned_second.nextDisc(),
           "event-indexed reveal tape was not deterministic");
  }

  expect(scenarioReturn(12'345, 17, true, 999'999.0) ==
             12'345.0 + 17.0 * 3'400.0 - 1'000'000.0,
         "terminal scenario score formula failed");
  expect(scenarioReturn(12'345, 100, false, -4'321.0) ==
             12'345.0 + 100.0 * 3'400.0 - 4'321.0,
         "cutoff scenario score formula failed");

  expect(assignedSeedRange(kScreenSeedStart, kScreenGames,
                           kScreenMaximumMoves) &&
             assignedSeedRange(kDevelopmentSeedStart, kDevelopmentGames,
                               kDevelopmentMaximumMoves),
         "assigned seed ranges rejected");
  for (const std::uint32_t forbidden : {
           0x3d30'0000u, 0x3d40'0000u, 0x3d50'0000u, 0x3d60'0000u,
           0x7d00'0000u, 0xd700'0000u, 0x4d60'0000u}) {
    expect(!assignedSeedRange(forbidden, kScreenGames,
                              kScreenMaximumMoves),
           "forbidden seed family passed guard");
  }
  expect(samePublicState(fixture, metadata),
         "public-state normalization failed");
  expect(kMaximumWorkPerDecision == 1'525'510 &&
             kMaximumRssBytes == 268'435'456 &&
             kMaximumWallSeconds == 1'800.0,
         "resource protocol drifted");
}

struct Options {
  bool self_test_only = false;
  int threads = kDefaultThreads;
  std::string output = "/tmp/drop7-public-survival-rollout.json";
  std::string roots = "/tmp/drop7-public-survival-rollout-roots.jsonl";
};

Options parseOptions(int argc, char** argv) {
  Options result;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--self-test") {
      result.self_test_only = true;
    } else if (argument == "--run") {
      result.self_test_only = false;
    } else if (argument == "--threads" || argument == "--output" ||
               argument == "--roots") {
      if (++index >= argc) {
        throw std::invalid_argument(std::string(argument) +
                                    " requires a value");
      }
      if (argument == "--threads") {
        result.threads = std::stoi(argv[index]);
        if (result.threads < 1 || result.threads > 16) {
          throw std::invalid_argument("threads must be from 1 to 16");
        }
      } else if (argument == "--output") {
        result.output = argv[index];
      } else {
        result.roots = argv[index];
      }
    } else {
      throw std::invalid_argument("unknown argument " +
                                  std::string(argument));
    }
  }
  return result;
}

int run(const Options& options) {
  runSelfTests();
  std::cerr << "public-survival self-tests passed\n";
  if (options.self_test_only) {
    std::cout << "PUBLIC_SURVIVAL_SELF_TEST {\"passed\":true,"
                 "\"levelBonus\":"
              << kLevelBonus << ",\"horizon\":" << kHorizon
              << ",\"scenarios\":" << kScenarios
              << ",\"maximumWorkPerDecision\":"
              << kMaximumWorkPerDecision << "}\n";
    return 0;
  }

  const auto total_started = Clock::now();
  const auto deadline =
      total_started + std::chrono::duration_cast<Clock::duration>(
                          std::chrono::duration<double>(kMaximumWallSeconds));
  const PairedCohort screen = runCohort(
      kScreenSeedStart, kScreenGames, kScreenMaximumMoves, options.threads,
      deadline, "screen");
  const Summary screen_baseline = summarize(screen.baseline);
  const Summary screen_candidate = summarize(screen.candidate);
  const PairedSummary screen_paired = pairedSummary(screen);
  const std::uint64_t screen_rss = peakRssBytes();
  const bool screen_resource_safe =
      screen_rss <= kMaximumRssBytes && Clock::now() < deadline;
  const bool screen_passed =
      screen_resource_safe &&
      screenGate(screen_baseline, screen_candidate, screen_paired);

  std::optional<PairedCohort> development;
  Summary development_baseline;
  Summary development_candidate;
  PairedSummary development_paired;
  bool development_passed = false;
  bool development_resource_projected = false;
  if (screen_passed) {
    const double elapsed = std::chrono::duration<double>(Clock::now() -
                                                         total_started)
                               .count();
    const double conservative_development_projection =
        screen.wall_seconds *
        (static_cast<double>(kDevelopmentGames) / kScreenGames) *
        (static_cast<double>(kDevelopmentMaximumMoves) /
         kScreenMaximumMoves) *
        1.10;
    development_resource_projected =
        elapsed + conservative_development_projection <=
        kMaximumWallSeconds;
    if (development_resource_projected) {
      development.emplace(runCohort(
          kDevelopmentSeedStart, kDevelopmentGames,
          kDevelopmentMaximumMoves, options.threads, deadline,
          "development"));
      development_baseline = summarize(development->baseline);
      development_candidate = summarize(development->candidate);
      development_paired = pairedSummary(*development);
      development_passed =
          peakRssBytes() <= kMaximumRssBytes && Clock::now() < deadline &&
          developmentGate(development_baseline, development_candidate,
                          development_paired);
    }
  }

  bool roots_written = false;
  if (screen_passed) {
    std::ofstream roots(options.roots, std::ios::trunc);
    if (!roots) throw std::runtime_error("could not create root JSONL");
    writeTraceJsonl(roots, "screen", screen);
    if (development.has_value()) {
      writeTraceJsonl(roots, "development", *development);
    }
    if (!roots) throw std::runtime_error("could not write root JSONL");
    roots_written = true;
  }

  const double total_wall =
      std::chrono::duration<double>(Clock::now() - total_started).count();
  const std::uint64_t peak_rss = peakRssBytes();
  std::ofstream output(options.output, std::ios::trunc);
  if (!output) throw std::runtime_error("could not create JSON artifact");
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"public-survival-rollout-h100\",\n"
         << "  \"publicStateOnly\":true,\n"
         << "  \"strategyFusionFree\":true,\n"
         << "  \"scoring\":{\"levelBonus\":" << kLevelBonus
         << ",\"survivalRewardPerMove\":" << kSurvivalReward
         << ",\"earlyTerminalPenalty\":" << kTerminalPenalty
         << "},\n  \"policy\":{\"rootActions\":\"all-legal\","
            "\"continuation\":\"fresh-completed-public-fair-d1\","
            "\"fairChanceStrata\":"
         << fair::kChanceSamples << ",\"horizon\":" << kHorizon
         << ",\"scenarios\":" << kScenarios
         << ",\"selection\":\"mean\","
            "\"tieBreak\":\"center-first\"},\n"
         << "  \"chance\":{\"alignedAcrossRootActions\":true,"
            "\"eventIndexed\":true,"
            "\"revealVisibleDomainsSeparate\":true,"
            "\"scenarioIdentityVisibleToContinuation\":false},\n"
         << "  \"seedProtocol\":{\"screenStart\":"
         << kScreenSeedStart << ",\"screenGames\":" << kScreenGames
         << ",\"developmentStart\":" << kDevelopmentSeedStart
         << ",\"developmentGames\":" << kDevelopmentGames
         << ",\"guardedAssignedRangesOnly\":true,"
            "\"openedProtectedSeeds\":false},\n"
         << "  \"gates\":{\"screen\":{"
            "\"minimumScoreRatio\":1.5,\"minimumMoveRatio\":1.5,"
            "\"minimumJointWins\":3,\"minimumMeanMoves\":150},"
            "\"development\":{\"minimumScoreRatio\":1.25,"
            "\"minimumMoveRatio\":1.25,"
            "\"nonnegativePairedOneSided95LowerBounds\":true}},\n"
         << "  \"resources\":{\"maximumWorkPerDecision\":"
         << kMaximumWorkPerDecision << ",\"maximumRssBytes\":"
         << kMaximumRssBytes << ",\"maximumWallSeconds\":"
         << kMaximumWallSeconds << ",\"peakRssBytes\":" << peak_rss
         << ",\"totalWallSeconds\":" << total_wall << "},\n"
         << "  \"screen\":";
  writeCohort(output, screen, screen_baseline, screen_candidate,
              screen_paired, screen_passed);
  output << ",\n  \"screenPassed\":"
         << (screen_passed ? "true" : "false")
         << ",\n  \"developmentResourceProjectionPassed\":"
         << (development_resource_projected ? "true" : "false")
         << ",\n  \"development\":";
  if (development.has_value()) {
    writeCohort(output, *development, development_baseline,
                development_candidate, development_paired,
                development_passed);
  } else {
    output << "null";
  }
  output << ",\n  \"developmentRan\":"
         << (development.has_value() ? "true" : "false")
         << ",\n  \"developmentPassed\":"
         << (development_passed ? "true" : "false")
         << ",\n  \"rootsJsonlWritten\":"
         << (roots_written ? "true" : "false")
         << ",\n  \"rootsJsonl\":"
         << (roots_written ? "\"" + options.roots + "\"" : "null")
         << "\n}\n";
  if (!output) throw std::runtime_error("could not write JSON artifact");

  std::cout << std::fixed << std::setprecision(3)
            << "PUBLIC_SURVIVAL_RESULT {\"screenBaselineScore\":"
            << screen_baseline.mean_score
            << ",\"screenBaselineMoves\":" << screen_baseline.mean_moves
            << ",\"screenCandidateScore\":"
            << screen_candidate.mean_score
            << ",\"screenCandidateMoves\":"
            << screen_candidate.mean_moves
            << ",\"screenJointWins\":" << screen_paired.joint_wins
            << ",\"screenPassed\":"
            << (screen_passed ? "true" : "false")
            << ",\"developmentRan\":"
            << (development.has_value() ? "true" : "false")
            << ",\"developmentPassed\":"
            << (development_passed ? "true" : "false")
            << ",\"peakRssBytes\":" << peak_rss
            << ",\"totalWallSeconds\":" << total_wall
            << ",\"artifact\":\"" << options.output << "\"}\n";
  return 0;
}

}  // namespace drop7::public_survival_rollout

#ifndef DROP7_PUBLIC_SURVIVAL_ROLLOUT_LIBRARY
int main(int argc, char** argv) {
  try {
    const auto options =
        drop7::public_survival_rollout::parseOptions(argc, argv);
    return drop7::public_survival_rollout::run(options);
  } catch (const std::exception& error) {
    std::cerr << "drop7_public_survival_rollout: " << error.what() << '\n';
    return 1;
  }
}
#endif
