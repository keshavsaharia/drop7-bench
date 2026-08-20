#define DROP7_FAIR_ONLY_HORIZON_LIBRARY
#include "../reference/fair-only-horizon.cpp"
#undef DROP7_FAIR_ONLY_HORIZON_LIBRARY

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Evaluates the fixed fair-policy action and transition terms in depth-three
// search.  Action terms are charged once per decision and transition terms
// once per realized sampled move at every search ply.  Gameplay does not
// select any coefficient.
namespace drop7::full_fair_horizon {

namespace fair = drop7::fair_only_horizon;

#ifndef DROP7_FULL_FAIR_ACTION_PRIORS
#define DROP7_FULL_FAIR_ACTION_PRIORS 1
#endif
#ifndef DROP7_FULL_FAIR_SCREEN_SEED_START
#define DROP7_FULL_FAIR_SCREEN_SEED_START 0x3ea10000u
#endif
#ifndef DROP7_FULL_FAIR_CONFIRMATION_SEED_START
#define DROP7_FULL_FAIR_CONFIRMATION_SEED_START 0x3ea20000u
#endif

constexpr int kDepth = 3;
constexpr int kChanceSamples = 5;
constexpr std::uint64_t kMaximumWork = 1'000'000;
constexpr std::size_t kMaximumCacheEntries = 40'000;
constexpr double kTerminalUtility = -1'000'000.0;
constexpr std::uint32_t kPolicySeed = 0xd707'5eedu;
constexpr std::uint32_t kScreenSeedStart =
    DROP7_FULL_FAIR_SCREEN_SEED_START;
constexpr std::uint32_t kConfirmationSeedStart =
    DROP7_FULL_FAIR_CONFIRMATION_SEED_START;
constexpr int kScreenGames = 8;
constexpr int kConfirmationGames = 16;
constexpr int kMaximumMoves = 1'000;
constexpr int kParallelism = 4;

// Fixed initialFairPolicyWeights() action/transition coefficients with
// revealedCoverValue set to 300.  The fair search already adds
// scoreDelta with coefficient one, so it is not repeated in extraReward().
constexpr double kClearedDiscValue = 0.0;
constexpr double kRevealedCoverValue = 300.0;
constexpr double kChainDepthValue = 120.0;
constexpr double kLandingHeight = -80.0;
constexpr double kLandingHeightSquared = -20.0;
constexpr double kCenterDistance = -20.0;
constexpr double kNeighborHeightGap = -40.0;
constexpr double kCoveredInColumn = -100.0;
constexpr double kLowNumbersInColumn = -100.0;
constexpr double kVerticalBuildDistance = -40.0;
constexpr double kVerticalOvershoot = -200.0;
constexpr double kHorizontalBuildDistance = -40.0;
constexpr double kHorizontalOvershoot = -200.0;
constexpr double kOneMoveFromTrigger = 180.0;
constexpr double kImminentRiseHeight = -120.0;

static_assert(kLevelBonus == 7'000);
static_assert(kDepth == fair::kDepth);
static_assert(kChanceSamples == fair::kChanceSamples);
static_assert(kMaximumWork == fair::kMaximumWork);
static_assert(kMaximumCacheEntries == fair::kMaximumCacheEntries);
static_assert(kMaximumMoves == fair::kMaximumMoves);
static_assert(kScreenSeedStart + kScreenGames < kConfirmationSeedStart);

struct ActionTerms {
  int landing_height = 0;
  int neighbor_height_gap = 0;
  int covered_in_column = 0;
  int low_numbers_in_column = 0;
  int vertical_build_distance = 0;
  int vertical_overshoot = 0;
  int horizontal_build_distance = 0;
  int horizontal_overshoot = 0;
  int one_move_from_trigger = 0;
};

ActionTerms extractActionTerms(const State& state, int column) {
  if (!isLegal(state.board, column)) {
    throw std::invalid_argument("action terms require a legal column");
  }
  const auto heights = cfpi::detail::columnHeights(state.board);
  ActionTerms result;
  result.landing_height = heights[column] + 1;
  const int left_height =
      column > 0 ? heights[column - 1] : result.landing_height;
  const int right_height = column + 1 < kBoardSize
                               ? heights[column + 1]
                               : result.landing_height;
  result.neighbor_height_gap =
      std::abs(result.landing_height - left_height) +
      std::abs(result.landing_height - right_height);
  for (int row = 0; row < kBoardSize; ++row) {
    const std::uint8_t cell = state.board[indexOf(row, column)];
    result.covered_in_column += cell == kSolid || cell == kCracked;
    result.low_numbers_in_column += cell == 1 || cell == 2;
  }
  Board placed = state.board;
  if (!placeDisc(placed, column, state.next_disc)) {
    throw std::logic_error("legal action could not be placed");
  }
  const int landing_row = kBoardSize - result.landing_height;
  const int horizontal_length =
      lineLength(placed, landing_row, column, false);
  result.vertical_build_distance =
      std::max(0, static_cast<int>(state.next_disc) - result.landing_height);
  result.vertical_overshoot =
      std::max(0, result.landing_height - static_cast<int>(state.next_disc));
  result.horizontal_build_distance =
      std::max(0, static_cast<int>(state.next_disc) - horizontal_length);
  result.horizontal_overshoot =
      std::max(0, horizontal_length - static_cast<int>(state.next_disc));
  const int trigger_distance = std::min(
      std::abs(static_cast<int>(state.next_disc) - result.landing_height),
      std::abs(static_cast<int>(state.next_disc) - horizontal_length));
  result.one_move_from_trigger = trigger_distance == 1 ? 1 : 0;
  return result;
}

double actionPrior(const State& state, int column) {
  const ActionTerms terms = extractActionTerms(state, column);
  const double height = terms.landing_height;
  double result = 0.0;
  result += kLandingHeight * height;
  result += kLandingHeightSquared * height * height;
  result += kCenterDistance *
            std::abs(column - static_cast<int>(kBoardSize / 2));
  result += kNeighborHeightGap * terms.neighbor_height_gap;
  result += kCoveredInColumn * terms.covered_in_column;
  result += kLowNumbersInColumn * terms.low_numbers_in_column;
  result += kVerticalBuildDistance * terms.vertical_build_distance;
  result += kVerticalOvershoot * terms.vertical_overshoot;
  result += kHorizontalBuildDistance * terms.horizontal_build_distance;
  result += kHorizontalOvershoot * terms.horizontal_overshoot;
  result += kOneMoveFromTrigger * terms.one_move_from_trigger;
  if (state.moves_remaining == 1) {
    result += kImminentRiseHeight * height * height;
  }
  return result;
}

double transitionReward(const MoveResult& move) {
  int cleared = 0;
  int revealed = 0;
  for (const Wave& wave : move.waves) {
    cleared += wave.cleared;
    revealed += wave.revealed;
  }
  const int continuation_depth =
      std::max(0, static_cast<int>(move.waves.size()) - 1);
  return kClearedDiscValue * cleared + kRevealedCoverValue * revealed +
         kChainDepthValue * continuation_depth * continuation_depth;
}

double bestFutureValue(const State& state, int depth,
                       fair::SearchContext& context);

fair::ActionValue evaluateAction(const State& state, int column, int depth,
                                 fair::SearchContext& context) {
  const std::uint32_t state_seed =
      cfpi::detail::scenarioSeedForState(state, kPolicySeed, depth);
  const double prior = DROP7_FULL_FAIR_ACTION_PRIORS != 0
                           ? actionPrior(state, column)
                           : 0.0;
  fair::ActionValue result;
  for (int sample = 0; sample < kChanceSamples; ++sample) {
    fair::checkBudget(context);
    cfpi::detail::StratifiedRandom random{
        state_seed, sample, kChanceSamples, 0};
    MoveResult move;
    const bool played =
        cfpi::detail::playMoveSampled(state, column, random, move);
    ++context.work;
    if (!played) {
      result.value += kTerminalUtility;
      continue;
    }
    const double score_delta = static_cast<double>(move.score_delta);
    const double shaped_reward = score_delta + prior + transitionReward(move);
    result.expected_score += score_delta;
    if (move.state.game_over) {
      result.value += shaped_reward + kTerminalUtility;
      continue;
    }
    move.state.score = 0;
    move.state.next_disc = cfpi::detail::sampledNextDisc(
        state_seed, sample, kChanceSamples);
    bool ignored = false;
    const State next = cfpi::detail::canonicalState(move.state, ignored);
    result.value +=
        shaped_reward +
        full_fair_horizon::bestFutureValue(next, depth - 1, context);
  }
  result.value /= kChanceSamples;
  result.expected_score /= kChanceSamples;
  return result;
}

double bestFutureValue(const State& state, int depth,
                       fair::SearchContext& context) {
  ++context.nodes;
  fair::checkBudget(context);
  if (state.game_over) return kTerminalUtility;
  if (depth == 0) return fair::evaluateLeaf(state, context);
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
        best,
        full_fair_horizon::evaluateAction(state, column, depth, context).value);
  }
  if (!std::isfinite(best)) best = kTerminalUtility;
  fair::cacheValue(context, key, best);
  return best;
}

fair::RootEvaluation rootDecision(const State& canonical, int depth,
                                  fair::SearchContext& context) {
  fair::RootEvaluation result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  result.expected_scores.fill(-std::numeric_limits<double>::infinity());
  for (const int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(canonical.board, column)) continue;
    const fair::ActionValue candidate =
        full_fair_horizon::evaluateAction(canonical, column, depth, context);
    result.values[column] = candidate.value;
    result.expected_scores[column] = candidate.expected_score;
    if (candidate.value > result.value) {
      result.value = candidate.value;
      result.action = column;
    }
  }
  return result;
}

fair::SearchDecision chooseAction(const State& source) {
  if (source.game_over) return {};
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  fair::SearchContext context;
  fair::RootEvaluation completed;
  int completed_depth = 0;
  for (int depth = 1; depth <= kDepth; ++depth) {
    try {
      completed = full_fair_horizon::rootDecision(canonical, depth, context);
      if (completed.action < 0) break;
      completed_depth = depth;
    } catch (const fair::WorkLimitReached&) {
      break;
    }
  }
  int action = completed.action;
  if (action < 0) action = centerFirstMove(canonical.board);
  fair::SearchDecision result;
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
    for (int canonical_column = 0; canonical_column < kBoardSize;
         ++canonical_column) {
      const int source_column = mirrored
                                    ? kBoardSize - 1 - canonical_column
                                    : canonical_column;
      result.root_values[source_column] = completed.values[canonical_column];
      result.root_expected_scores[source_column] =
          completed.expected_scores[canonical_column];
    }
  }
  return result;
}

fair::GameResult runCandidateGame(std::uint32_t seed,
                                  std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  fair::GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    const fair::SearchDecision decision = chooseAction(state);
    if (!decision.complete || decision.completed_depth != kDepth) {
      throw std::runtime_error("full fair search did not complete depth three");
    }
    if (!isLegal(state.board, decision.action)) {
      throw std::runtime_error("full fair search chose an illegal action");
    }
    result.work += decision.work;
    result.nodes += decision.nodes;
    result.cache_hits += decision.cache_hits;
    result.maximum_cache_entries =
        std::max(result.maximum_cache_entries, decision.cache_entries);
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("full fair transition failed");
    }
    fair::observeMove(move, result);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.peak_rss_bytes = fair::peakRssBytes();
  result.elapsed_seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
  fair::reportGame(label, result);
  return result;
}

fair::Cohort runCohort(std::uint32_t seed_start, int games,
                       std::string_view phase) {
  const auto started = std::chrono::steady_clock::now();
  fair::Cohort result;
  result.baseline.resize(games);
  result.fair.resize(games);
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= games) return;
        const std::uint32_t seed =
            seed_start + static_cast<std::uint32_t>(game);
        result.baseline[game] = fair::runFairGame(
            seed, std::string(phase) + "-fair-leaf-d3");
        result.fair[game] = runCandidateGame(
            seed,
            std::string(phase) +
                (DROP7_FULL_FAIR_ACTION_PRIORS != 0
                     ? "-full-fair-d3"
                     : "-transition-reward-d3"));
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.wall_seconds = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started)
                            .count();
  return result;
}

void writePairs(std::ostream& output, const fair::Cohort& cohort) {
  output << '[';
  for (std::size_t game = 0; game < cohort.baseline.size(); ++game) {
    if (game != 0) output << ',';
    output << "{\"seed\":" << cohort.baseline[game].seed
           << ",\"fairLeaf\":";
    fair::writeGame(output, cohort.baseline[game]);
    output << ",\"fullFair\":";
    fair::writeGame(output, cohort.fair[game]);
    output << '}';
  }
  output << ']';
}

void writeCohort(std::ostream& output, std::uint32_t seed_start,
                 const fair::Cohort& cohort, const fair::Summary& baseline,
                 const fair::Summary& candidate,
                 const fair::PairedSummary& paired, bool passed) {
  output << "{\"seedStart\":" << seed_start << ",\"fairLeaf\":";
  fair::writeSummary(output, baseline);
  output << ",\"fullFair\":";
  fair::writeSummary(output, candidate);
  output << ",\"paired\":";
  fair::writePaired(output, paired);
  output << ",\"wallSeconds\":" << cohort.wall_seconds
         << ",\"passed\":" << (passed ? "true" : "false")
         << ",\"pairs\":";
  full_fair_horizon::writePairs(output, cohort);
  output << '}';
}

struct Options {
  std::string output = "/tmp/drop7-full-fair-horizon.json";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing full-fair option value");
    }
    if (std::string_view(argv[index]) == "--output") {
      result.output = argv[index + 1];
    } else {
      throw std::invalid_argument("unknown full-fair option");
    }
  }
  return result;
}

bool selfTest(std::ostream& output) {
  const State state = fair::fixtureState(fair::kTypeScriptFixtures[1]);
  State reflected = state;
  reflected.board = cfpi::detail::mirrorBoard(state.board);
  State metadata = state;
  metadata.score = 7'654'321;
  metadata.level = 73;
  metadata.moves_played = 812;
  const fair::SearchDecision first = chooseAction(state);
  const fair::SearchDecision second = chooseAction(state);
  const fair::SearchDecision mirror = chooseAction(reflected);
  const fair::SearchDecision hidden = chooseAction(metadata);
  const bool deterministic = first.action == second.action &&
                             first.work == second.work &&
                             first.root_values == second.root_values;
  const bool reflection_safe =
      mirror.action == kBoardSize - 1 - first.action;
  const bool public_only = hidden.action == first.action &&
                           hidden.work == first.work &&
                           hidden.root_values == first.root_values;
  const bool complete = first.complete && first.completed_depth == kDepth &&
                        isLegal(state.board, first.action);
  const bool fixed_protocol =
      kLevelBonus == 7'000 &&
      kScreenSeedStart + kScreenGames < kConfirmationSeedStart &&
      (kScreenSeedStart >> 24) != 0x7du &&
      (kScreenSeedStart >> 24) != 0xd7u &&
      (kConfirmationSeedStart >> 24) != 0x7du &&
      (kConfirmationSeedStart >> 24) != 0xd7u;
  const bool passed = deterministic && reflection_safe && public_only &&
                      complete && fixed_protocol;
  output << (DROP7_FULL_FAIR_ACTION_PRIORS != 0
                 ? "FULL_FAIR_HORIZON_SELF_TEST"
                 : "TRANSITION_REWARD_HORIZON_SELF_TEST")
         << " {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"deterministic\":" << (deterministic ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe ? "true" : "false")
         << ",\"publicStateOnly\":" << (public_only ? "true" : "false")
         << ",\"completedDepth\":" << first.completed_depth
         << ",\"action\":" << first.action << "}\n";
  return passed;
}

int run(const Options& options, std::ostream& output) {
  const auto started = std::chrono::steady_clock::now();
  const fair::Cohort screen =
      runCohort(kScreenSeedStart, kScreenGames, "screen");
  const fair::Summary screen_baseline = fair::summarize(screen.baseline);
  const fair::Summary screen_candidate = fair::summarize(screen.fair);
  const fair::PairedSummary screen_paired = fair::pairedSummary(screen);
  const bool screen_passed =
      fair::improvesBothMeans(screen_baseline, screen_candidate);

  fair::Cohort confirmation;
  fair::Summary confirmation_baseline;
  fair::Summary confirmation_candidate;
  fair::PairedSummary confirmation_paired;
  bool confirmation_passed = false;
  if (screen_passed) {
    confirmation = runCohort(kConfirmationSeedStart, kConfirmationGames,
                             "confirmation");
    confirmation_baseline = fair::summarize(confirmation.baseline);
    confirmation_candidate = fair::summarize(confirmation.fair);
    confirmation_paired = fair::pairedSummary(confirmation);
    confirmation_passed = fair::improvesBothMeans(
        confirmation_baseline, confirmation_candidate);
  }
  const double wall = std::chrono::duration<double>(
                          std::chrono::steady_clock::now() - started)
                          .count();
  std::ofstream artifact(options.output);
  if (!artifact) throw std::runtime_error("could not write full-fair artifact");
  artifact << std::setprecision(10) << "{\"experiment\":\""
           << (DROP7_FULL_FAIR_ACTION_PRIORS != 0
                   ? "full-historical-fair-horizon"
                   : "transition-reward-fair-horizon")
           << "\",\"preregistered\":true,\"publicStateOnly\":true,"
              "\"actionPriorsIncluded\":"
           << (DROP7_FULL_FAIR_ACTION_PRIORS != 0 ? "true" : "false")
           << ",\"levelBonus\":7000,\"screen\":";
  full_fair_horizon::writeCohort(
      artifact, kScreenSeedStart, screen, screen_baseline, screen_candidate,
      screen_paired, screen_passed);
  artifact << ",\"confirmation\":";
  if (screen_passed) {
    full_fair_horizon::writeCohort(
        artifact, kConfirmationSeedStart, confirmation, confirmation_baseline,
        confirmation_candidate, confirmation_paired, confirmation_passed);
  } else {
    artifact << "null";
  }
  artifact << ",\"qualified\":"
           << (screen_passed && confirmation_passed ? "true" : "false")
           << ",\"totalWallSeconds\":" << wall << "}\n";
  output << std::fixed << std::setprecision(3)
         << (DROP7_FULL_FAIR_ACTION_PRIORS != 0
                 ? "FULL_FAIR_HORIZON_RESULT"
                 : "TRANSITION_REWARD_HORIZON_RESULT")
         << " {\"screenFairScore\":"
         << screen_baseline.mean_score << ",\"screenFairMoves\":"
         << screen_baseline.mean_moves << ",\"screenCandidateScore\":"
         << screen_candidate.mean_score << ",\"screenCandidateMoves\":"
         << screen_candidate.mean_moves << ",\"scoreDelta\":"
         << screen_paired.score.mean << ",\"moveDelta\":"
         << screen_paired.moves.mean << ",\"screenPassed\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmationRan\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\"peakRssBytes\":" << fair::peakRssBytes()
         << ",\"totalWallSeconds\":" << wall << ",\"artifact\":\""
         << options.output << "\"}\n";
  return 0;
}

}  // namespace drop7::full_fair_horizon

#ifndef DROP7_FULL_FAIR_HORIZON_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::full_fair_horizon::selfTest(std::cout) ? EXIT_SUCCESS
                                                           : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::full_fair_horizon::parseOptions(argc, argv, 2);
      return drop7::full_fair_horizon::run(options, std::cout);
    }
    std::cerr << "usage: drop7_full_fair_horizon --self-test | --run "
                 "[--output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
#endif
