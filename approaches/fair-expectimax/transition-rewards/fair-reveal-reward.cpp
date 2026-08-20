#define DROP7_FAIR_PHASE_ENERGY_RELEASE_LIBRARY
#include "fair-phase-energy-release.cpp"
#undef DROP7_FAIR_PHASE_ENERGY_RELEASE_LIBRARY

#include <atomic>
#include <future>
#include <optional>

// Tests an access-to-covered-number reward with coefficients fixed before
// evaluation.  It does not read the separate 0x3de5/0x3de6 clear-reward seed
// ranges.
namespace drop7::fair_reveal_reward {

namespace phase = fair_phase_energy_release;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kFittingStart = 0x3def'0000u;
constexpr int kFittingGames = 4;
constexpr std::uint32_t kHeldoutStart = 0x3df4'0000u;
constexpr int kHeldoutGames = 8;
constexpr std::uint32_t kScreenStart = 0x3ebd'0000u;
constexpr int kScreenGames = 8;
constexpr std::uint32_t kConfirmationStart = 0x3ebe'0000u;
constexpr int kConfirmationGames = 16;
constexpr int kMaximumMoves = 1'000;
constexpr int kDefaultThreads = 8;
constexpr int kMinimumLooTripleWins = 3;
constexpr double kMaterialRevealRatio = 0.95;
constexpr double kWallLimitSeconds = 35.0 * 60.0;
constexpr double kProjectionSafetyFactor = 1.50;
constexpr double kInitialSecondsPerGame = 5.0 * 60.0;
constexpr double kPriorFirstSeedWallSeconds = 386.055041583;
constexpr std::string_view kPriorFirstSeedArtifact =
    "/tmp/drop7-fair-reveal-reward-first-seed.json";
constexpr std::string_view kPriorFirstSeedArtifactSha256 =
    "1e3853efb5c4422fedf1c10db2756410ba1665f7859984f9e4a8897bfefeffac";

constexpr std::array<phase::Config, 3> kPolicies{{
    {"stock", 0.0, 0.0, 0.0},
    {"reveal-only", 0.0, 0.0, 600.0},
    {"balanced", 300.0, 0.0, 600.0},
}};

static_assert(phase::kDepth == 4 && phase::kChanceSamples == 5);
static_assert(phase::kMaximumMoves == kMaximumMoves);
static_assert(phase::kMaximumWork > phase::kWorstCaseWork);
static_assert(phase::kMaximumCacheEntries > phase::kWorstCaseCacheEntries);
static_assert(kPolicies[1].clear_reward == 0.0 &&
              kPolicies[1].reveal_reward == 600.0 &&
              kPolicies[1].phase_strength == 0.0);
static_assert(kPolicies[2].clear_reward == 300.0 &&
              kPolicies[2].reveal_reward == 600.0 &&
              kPolicies[2].phase_strength == 0.0);
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

phase::GameResult runGame(std::uint32_t seed, int policy_index) {
  const phase::Config& policy =
      kPolicies.at(static_cast<std::size_t>(policy_index));
  State state = initialHeadlessState(seed);
  phase::GameResult result;
  result.seed = seed;
  result.config = policy_index;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    const auto started = Clock::now();
    const phase::SearchDecision decision = phase::chooseAction(state, policy);
    result.decision_seconds +=
        std::chrono::duration<double>(Clock::now() - started).count();
    if (!decision.complete || decision.completed_depth != phase::kDepth) {
      throw std::runtime_error("reveal-reward D4 did not complete");
    }
    if (decision.work > phase::kMaximumWork ||
        decision.cache_entries > phase::kMaximumCacheEntries) {
      throw std::runtime_error("reveal-reward D4 exceeded resource bound");
    }
    result.work += decision.work;
    result.nodes += decision.nodes;
    result.cache_hits += decision.cache_hits;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, decision.cache_entries);
    if (!isLegal(state.board, decision.action)) {
      throw std::runtime_error("reveal-reward D4 returned illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("reveal-reward transition failed");
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

using PolicyGames = std::array<std::vector<phase::GameResult>, kPolicies.size()>;

// Cached first-fitting-seed records from kPriorFirstSeedArtifact.  Protocol
// continuation starts at the next fitting seed and does not reevaluate this
// cached seed.
PolicyGames priorFirstSeedGames() {
  PolicyGames result;
  result[0].push_back({kFittingStart, 0, 140'681, 90, false, 178, 98, 10,
                       0, 130'025'753, 65'916'482, 700'368, 34'145,
                       131.790472167});
  result[1].push_back({kFittingStart, 1, 171'147, 108, false, 213, 115, 10,
                       0, 150'872'391, 76'669'870, 879'422, 35'668,
                       160.51529054});
  result[2].push_back({kFittingStart, 2, 370'588, 225, false, 497, 282, 10,
                       0, 324'444'963, 164'563'155, 1'752'649, 33'951,
                       365.436862666});
  return result;
}

PolicyGames runPolicies(std::uint32_t start, int games,
                        const std::vector<int>& policies, int threads,
                        std::string_view label) {
  PolicyGames result;
  struct Job {
    int policy;
    int game;
  };
  std::vector<Job> jobs;
  for (const int policy : policies) {
    result[static_cast<std::size_t>(policy)].resize(
        static_cast<std::size_t>(games));
    for (int game = 0; game < games; ++game) jobs.push_back({policy, game});
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
        phase::GameResult game = runGame(seed, job.policy);
        result[static_cast<std::size_t>(job.policy)]
              [static_cast<std::size_t>(job.game)] = game;
        const std::lock_guard<std::mutex> lock(progress_mutex);
        std::cerr << "reveal-reward " << label << ' '
                  << kPolicies[job.policy].name << " seed 0x" << std::hex
                  << seed << std::dec << ' ' << game.score << " points/"
                  << game.moves << " moves, " << game.numbered_cleared
                  << " clears, " << game.covers_revealed << " reveals\n";
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

void appendPolicies(PolicyGames& destination, PolicyGames&& source,
                    const std::vector<int>& policies) {
  for (const int policy : policies) {
    auto& target = destination[static_cast<std::size_t>(policy)];
    auto& values = source[static_cast<std::size_t>(policy)];
    target.insert(target.end(), std::make_move_iterator(values.begin()),
                  std::make_move_iterator(values.end()));
  }
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
  phase::Summary base{};
  double lower_quartile_score = 0.0;
  double lower_quartile_moves = 0.0;
};

Summary summarizeExtended(const std::vector<phase::GameResult>& games,
                          std::optional<int> omitted = std::nullopt) {
  std::vector<phase::GameResult> included;
  std::vector<double> scores;
  std::vector<double> moves;
  included.reserve(games.size());
  for (std::size_t index = 0; index < games.size(); ++index) {
    if (omitted.has_value() && static_cast<int>(index) == *omitted) continue;
    included.push_back(games[index]);
    scores.push_back(static_cast<double>(games[index].score));
    moves.push_back(static_cast<double>(games[index].moves));
  }
  Summary result;
  result.base = phase::summarize(included);
  result.lower_quartile_score = quantile(std::move(scores), 0.25);
  result.lower_quartile_moves = quantile(std::move(moves), 0.25);
  return result;
}

bool fittingTripleWin(const Summary& stock, const Summary& candidate) {
  return candidate.base.mean_score > stock.base.mean_score &&
         candidate.base.mean_moves > stock.base.mean_moves &&
         candidate.base.reveals_per_move > stock.base.reveals_per_move;
}

bool laterMeanGate(const Summary& stock, const Summary& candidate) {
  return stock.base.censored == 0 && candidate.base.censored == 0 &&
         fittingTripleWin(stock, candidate);
}

bool lowerTailRetained(const Summary& stock, const Summary& candidate) {
  return candidate.lower_quartile_score >= stock.lower_quartile_score &&
         candidate.lower_quartile_moves >= stock.lower_quartile_moves;
}

bool strictGate(const Summary& stock, const Summary& candidate) {
  return laterMeanGate(stock, candidate) &&
         candidate.base.clears_per_move >= stock.base.clears_per_move &&
         lowerTailRetained(stock, candidate);
}

struct LeaveOneOut {
  int triple_wins = 0;
  std::array<int, kPolicies.size()> winner_counts{};
};

int selectCandidate(const PolicyGames& games,
                    std::optional<int> omitted = std::nullopt) {
  const Summary reveal = summarizeExtended(games[1], omitted);
  const Summary balanced = summarizeExtended(games[2], omitted);
  return balanced.base.mean_score > reveal.base.mean_score ? 2 : 1;
}

LeaveOneOut leaveOneOut(const PolicyGames& games, int champion) {
  LeaveOneOut result;
  for (int omitted = 0; omitted < kFittingGames; ++omitted) {
    ++result.winner_counts[static_cast<std::size_t>(
        selectCandidate(games, omitted))];
    if (fittingTripleWin(summarizeExtended(games[0], omitted),
                         summarizeExtended(games[champion], omitted))) {
      ++result.triple_wins;
    }
  }
  return result;
}

double projectedSeconds(const PolicyGames& source,
                        const std::vector<int>& policies, int source_games,
                        int target_games, int threads) {
  double cpu_seconds = 0.0;
  for (const int policy : policies) {
    for (const phase::GameResult& game : source[policy]) {
      cpu_seconds += game.decision_seconds;
    }
  }
  if (source_games <= 0) return kWallLimitSeconds;
  return kProjectionSafetyFactor * cpu_seconds * target_games /
         (source_games * std::max(1, threads));
}

double initialProjection(int policy_games, int threads) {
  return kProjectionSafetyFactor * kInitialSecondsPerGame * policy_games /
         std::max(1, threads);
}

bool stageFits(const Clock::time_point& started, double projection,
               double prior_seconds = 0.0) {
  const double elapsed =
      std::chrono::duration<double>(Clock::now() - started).count();
  return prior_seconds + elapsed + projection <= kWallLimitSeconds;
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

void runSelfTests() {
  phase::runSelfTests();
  const State fixture =
      phase::frozen::fixtureState(phase::frozen::kTypeScriptFixtures[1]);
  const phase::SearchDecision stock =
      phase::chooseAction(fixture, kPolicies[0]);
  const phase::SearchDecision qualified_stock =
      phase::chooseAction(fixture, phase::kMenu[0]);
  expect(stock.action == qualified_stock.action &&
             stock.root_values == qualified_stock.root_values &&
             stock.work == qualified_stock.work &&
             stock.nodes == qualified_stock.nodes &&
             stock.cache_hits == qualified_stock.cache_hits,
         "reveal stock policy did not preserve exact zero parity");
  const phase::SearchDecision candidate =
      phase::chooseAction(fixture, kPolicies[1]);
  const phase::SearchDecision repeat =
      phase::chooseAction(fixture, kPolicies[1]);
  expect(candidate.action == repeat.action &&
             candidate.root_values == repeat.root_values &&
             candidate.work == repeat.work,
         "reveal candidate was not deterministic");
  State reflected = phase::publicState(fixture);
  reflected.board = cfpi::detail::mirrorBoard(fixture.board);
  const phase::SearchDecision mirror =
      phase::chooseAction(reflected, kPolicies[1]);
  expect(mirror.action == kBoardSize - 1 - candidate.action,
         "reveal candidate was not reflection safe");
  State metadata = fixture;
  metadata.score = 9'000'000;
  metadata.level = 91;
  metadata.moves_played = 777;
  const phase::SearchDecision metadata_decision =
      phase::chooseAction(metadata, kPolicies[1]);
  expect(metadata_decision.action == candidate.action &&
             metadata_decision.root_values == candidate.root_values,
         "reveal candidate used non-public metadata");

  MoveResult row_rise_fixture;
  row_rise_fixture.score_delta = 21;
  // The engine appends post-rise cascade waves after the placement cascade.
  row_rise_fixture.waves.push_back({1, 2, 1, 14});
  row_rise_fixture.waves.push_back({2, 1, 3, 7});
  expect(phase::coversRevealed(row_rise_fixture) == 4 &&
             phase::transitionValue(row_rise_fixture, kPolicies[1]) == 2'421.0 &&
             phase::transitionValue(row_rise_fixture, kPolicies[2]) == 3'321.0,
         "reveal accounting omitted a row-rise cascade wave");
  expect(candidate.complete && candidate.work <= phase::kMaximumWork &&
             candidate.cache_entries <= phase::kMaximumCacheEntries,
         "reveal candidate resource proof failed");
  expect(initialProjection(3, kDefaultThreads) == 168.75,
         "initial first-seed projection changed");
  const PolicyGames prior = priorFirstSeedGames();
  expect(prior[0][0].seed == kFittingStart && prior[0][0].score == 140'681 &&
             prior[0][0].moves == 90 && prior[0][0].numbered_cleared == 178 &&
             prior[0][0].covers_revealed == 98 &&
             prior[0][0].work == 130'025'753 &&
             prior[1][0].score == 171'147 && prior[1][0].moves == 108 &&
             prior[1][0].covers_revealed == 115 &&
             prior[2][0].score == 370'588 && prior[2][0].moves == 225 &&
             prior[2][0].numbered_cleared == 497 &&
             prior[2][0].covers_revealed == 282 &&
             prior[2][0].work == 324'444'963,
         "frozen first-seed continuation fixture changed");
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"games\":" << summary.base.games
         << ",\"censored\":" << summary.base.censored
         << ",\"meanScore\":" << summary.base.mean_score
         << ",\"medianScore\":" << summary.base.median_score
         << ",\"lowerQuartileScore\":" << summary.lower_quartile_score
         << ",\"standardError\":" << summary.base.standard_error
         << ",\"meanMoves\":" << summary.base.mean_moves
         << ",\"lowerQuartileMoves\":" << summary.lower_quartile_moves
         << ",\"numberedClearsPerMove\":"
         << summary.base.clears_per_move
         << ",\"coversRevealedPerMove\":"
         << summary.base.reveals_per_move
         << ",\"meanMaximumChain\":" << summary.base.mean_maximum_chain
         << ",\"meanDecisionMs\":" << summary.base.mean_decision_ms
         << ",\"meanWorkPerMove\":" << summary.base.mean_work_per_move
         << ",\"peakCacheEntries\":" << summary.base.peak_cache_entries
         << '}';
}

void writeExtendedGames(std::ostream& output,
                        const std::vector<phase::GameResult>& games) {
  phase::writeGames(output, games);
}

void writeCohort(std::ostream& output, const PolicyGames& games,
                 const std::vector<int>& policies) {
  output << '[';
  for (std::size_t index = 0; index < policies.size(); ++index) {
    if (index > 0) output << ',';
    const int policy = policies[index];
    output << "{\"policyIndex\":" << policy << ",\"summary\":";
    writeSummary(output, summarizeExtended(games[policy]));
    output << ",\"games\":";
    writeExtendedGames(output, games[policy]);
    output << '}';
  }
  output << ']';
}

struct Options {
  std::string output = "/tmp/drop7-fair-reveal-reward.json";
  int threads = kDefaultThreads;
  bool self_test_only = false;
  bool first_seed_only = false;
};

Options parseOptions(int argc, char** argv) {
  Options result;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--self-test-only") {
      result.self_test_only = true;
      continue;
    }
    if (argument == "--first-seed-only") {
      result.first_seed_only = true;
      continue;
    }
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    if (argument == "--output") {
      result.output = argv[++index];
    } else if (argument == "--threads") {
      result.threads = std::stoi(argv[++index]);
      if (result.threads < 1 || result.threads > 16) {
        throw std::invalid_argument("threads must be in [1,16]");
      }
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  return result;
}

int run(const Options& options) {
  const auto started = Clock::now();
  runSelfTests();
  std::cerr << "reveal-reward self-tests passed (peak RSS "
            << phase::peakRssBytes() << " bytes)\n";
  if (options.self_test_only) return 0;
  const std::vector<int> all_policies{0, 1, 2};
  if (options.first_seed_only) {
    const double first_projection = initialProjection(3, options.threads);
    PolicyGames first = runPolicies(kFittingStart, 1, all_policies,
                                    options.threads, "fitting-first-only");
    bool adverse = true;
    const phase::GameResult& stock = first[0][0];
    for (int policy = 1; policy <= 2; ++policy) {
      const phase::GameResult& candidate = first[policy][0];
      const double stock_reveals =
          static_cast<double>(stock.covers_revealed) / stock.moves;
      const double candidate_reveals =
          static_cast<double>(candidate.covers_revealed) / candidate.moves;
      if (!(candidate.score < stock.score && candidate.moves < stock.moves &&
            candidate_reveals < stock_reveals * kMaterialRevealRatio)) {
        adverse = false;
      }
    }
    const double remainder_projection = projectedSeconds(
        first, all_policies, 1, kFittingGames - 1, options.threads);
    const bool remainder_fits = stageFits(started, remainder_projection);
    std::ofstream output(options.output);
    if (!output) throw std::runtime_error("could not open first-seed artifact");
    output << std::setprecision(12)
           << "{\n  \"experiment\":\"fair-d4-reveal-reward-first-seed\",\n"
           << "  \"formalGate\":false,\n  \"seed\":" << kFittingStart
           << ",\n  \"policies\":";
    writeCohort(output, first, all_policies);
    output << ",\n  \"adverseStop\":" << (adverse ? "true" : "false")
           << ",\n  \"firstProjectionSeconds\":" << first_projection
           << ",\n  \"remainderProjectionSeconds\":"
           << remainder_projection
           << ",\n  \"remainderFitsWall\":"
           << (remainder_fits ? "true" : "false")
           << ",\n  \"laterSeedOpened\":false,\n  \"wallSeconds\":"
           << std::chrono::duration<double>(Clock::now() - started).count()
           << ",\n  \"peakRssBytes\":" << phase::peakRssBytes() << "\n}\n";
    output.close();
    std::cout << "first-seed adverse=" << (adverse ? "yes" : "no")
              << " remainder-fits=" << (remainder_fits ? "yes" : "no")
              << " artifact=" << options.output << '\n';
    return 2;
  }
  PolicyGames fitting = priorFirstSeedGames();
  PolicyGames heldout;
  PolicyGames screen;
  PolicyGames confirmation;
  std::string resource_stop_stage;
  bool adverse_stop = false;
  bool fitting_complete = false;
  bool fitting_gate = false;
  bool heldout_ran = false;
  bool heldout_gate = false;
  bool screen_ran = false;
  bool screen_gate = false;
  bool confirmation_ran = false;
  bool confirmation_gate = false;
  int champion = -1;
  LeaveOneOut loo;

  const double first_projection = initialProjection(3, options.threads);
  const phase::GameResult& stock = fitting[0][0];
  adverse_stop = true;
  for (int policy = 1; policy <= 2; ++policy) {
    const phase::GameResult& candidate_game = fitting[policy][0];
    const double stock_reveals =
        static_cast<double>(stock.covers_revealed) / stock.moves;
    const double candidate_reveals =
        static_cast<double>(candidate_game.covers_revealed) /
        candidate_game.moves;
    const bool materially_adverse =
        candidate_game.score < stock.score &&
        candidate_game.moves < stock.moves &&
        candidate_reveals < stock_reveals * kMaterialRevealRatio;
    if (!materially_adverse) adverse_stop = false;
  }

  const double remainder_projection =
      fitting[0].empty()
          ? kWallLimitSeconds
          : projectedSeconds(fitting, all_policies, 1, kFittingGames - 1,
                             options.threads);
  if (!resource_stop_stage.empty() || adverse_stop) {
    // Diagnostic stop: never a formal fitting failure.
  } else if (!stageFits(started, remainder_projection,
                        kPriorFirstSeedWallSeconds)) {
    resource_stop_stage = "fitting-remainder";
  } else {
    PolicyGames remainder = runPolicies(
        kFittingStart + 1, kFittingGames - 1, all_policies, options.threads,
        "fitting-remainder");
    appendPolicies(fitting, std::move(remainder), all_policies);
    fitting_complete = true;
    champion = selectCandidate(fitting);
    loo = leaveOneOut(fitting, champion);
    const Summary stock = summarizeExtended(fitting[0]);
    const Summary candidate = summarizeExtended(fitting[champion]);
    fitting_gate = fittingTripleWin(stock, candidate) &&
                   stock.base.censored == 0 && candidate.base.censored == 0 &&
                   loo.triple_wins >= kMinimumLooTripleWins;
  }

  const std::vector<int> pair_policies =
      champion > 0 ? std::vector<int>{0, champion} : std::vector<int>{0};
  const double heldout_projection =
      fitting_complete
          ? projectedSeconds(fitting, pair_policies, kFittingGames,
                             kHeldoutGames, options.threads)
          : kWallLimitSeconds;
  if (fitting_gate) {
    if (!stageFits(started, heldout_projection,
                   kPriorFirstSeedWallSeconds)) {
      resource_stop_stage = "heldout";
    } else {
      heldout_ran = true;
      heldout = runPolicies(kHeldoutStart, kHeldoutGames, pair_policies,
                            options.threads, "heldout");
      heldout_gate = strictGate(summarizeExtended(heldout[0]),
                                summarizeExtended(heldout[champion]));
    }
  }

  const double screen_projection =
      heldout_ran
          ? projectedSeconds(heldout, pair_policies, kHeldoutGames,
                             kScreenGames, options.threads)
          : kWallLimitSeconds;
  if (heldout_gate) {
    if (!stageFits(started, screen_projection, kPriorFirstSeedWallSeconds)) {
      resource_stop_stage = "screen";
    } else {
      screen_ran = true;
      screen = runPolicies(kScreenStart, kScreenGames, pair_policies,
                           options.threads, "fresh-screen");
      screen_gate = laterMeanGate(summarizeExtended(screen[0]),
                                  summarizeExtended(screen[champion]));
    }
  }

  const double confirmation_projection =
      screen_ran
          ? projectedSeconds(screen, pair_policies, kScreenGames,
                             kConfirmationGames, options.threads)
          : kWallLimitSeconds;
  if (screen_gate) {
    if (!stageFits(started, confirmation_projection,
                   kPriorFirstSeedWallSeconds)) {
      resource_stop_stage = "confirmation";
    } else {
      confirmation_ran = true;
      confirmation = runPolicies(kConfirmationStart, kConfirmationGames,
                                 pair_policies, options.threads,
                                 "fresh-confirmation");
      confirmation_gate = strictGate(summarizeExtended(confirmation[0]),
                                     summarizeExtended(confirmation[champion]));
    }
  }

  const double wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open reveal artifact");
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"fair-d4-reveal-reward\",\n"
         << "  \"independentPreregisteredTest\":true,\n"
         << "  \"publicStateOnly\":true,\n"
         << "  \"search\":{\"depth\":4,\"chanceSamples\":5,"
            "\"fullWidth\":true,\"maximumMoves\":1000,"
            "\"maximumWork\":3200000,\"worstCaseWork\":3134950,"
            "\"maximumCacheEntries\":60000,"
            "\"worstCaseCacheEntries\":45430},\n"
         << "  \"menu\":[";
  for (std::size_t index = 0; index < kPolicies.size(); ++index) {
    if (index > 0) output << ',';
    output << "{\"index\":" << index << ",\"name\":\""
           << kPolicies[index].name << "\",\"clearReward\":"
           << kPolicies[index].clear_reward << ",\"revealReward\":"
           << kPolicies[index].reveal_reward << ",\"phaseStrength\":"
           << kPolicies[index].phase_strength << '}';
  }
  output << "],\n  \"seedDiscipline\":{\"fittingStart\":"
         << kFittingStart << ",\"heldoutStart\":" << kHeldoutStart
         << ",\"screenStart\":" << kScreenStart
         << ",\"confirmationStart\":" << kConfirmationStart
         << ",\"reusesDe5OrDe6\":false,\"forbiddenFamilies\":["
            "\"0x7d\",\"0xd7\"]},\n"
         << "  \"gates\":{\"minimumLooTripleWins\":"
         << kMinimumLooTripleWins
         << ",\"firstSeedMaterialRevealRatio\":" << kMaterialRevealRatio
         << ",\"lowerTailQuantile\":0.25,"
            "\"heldoutAndFinalRequireClearNonRegression\":true},\n"
         << "  \"resourceProtocol\":{\"wallLimitSeconds\":"
         << kWallLimitSeconds << ",\"projectionSafetyFactor\":"
         << kProjectionSafetyFactor << ",\"initialSecondsPerGame\":"
         << kInitialSecondsPerGame << ",\"threads\":" << options.threads
         << ",\"priorFirstSeedWallSeconds\":"
         << kPriorFirstSeedWallSeconds
         << ",\"resumedPriorFirstSeed\":true,\"priorFirstSeedArtifact\":\""
         << kPriorFirstSeedArtifact << "\",\"priorFirstSeedArtifactSha256\":\""
         << kPriorFirstSeedArtifactSha256 << '\"'
         << ",\"firstProjection\":" << first_projection
         << ",\"remainderProjection\":" << remainder_projection
         << ",\"heldoutProjection\":" << heldout_projection
         << ",\"screenProjection\":" << screen_projection
         << ",\"confirmationProjection\":" << confirmation_projection
         << ",\"stopStage\":";
  if (resource_stop_stage.empty()) output << "null";
  else output << '\"' << resource_stop_stage << '\"';
  output << "},\n  \"adverseFirstSeedStop\":"
         << (adverse_stop ? "true" : "false")
         << ",\n  \"fittingComplete\":"
         << (fitting_complete ? "true" : "false")
         << ",\n  \"fitting\":";
  writeCohort(output, fitting, all_policies);
  output << ",\n  \"selection\":{\"championIndex\":" << champion
         << ",\"leaveOneOutTripleWins\":" << loo.triple_wins
         << ",\"winnerCounts\":[";
  for (std::size_t index = 0; index < loo.winner_counts.size(); ++index) {
    if (index > 0) output << ',';
    output << loo.winner_counts[index];
  }
  output << "],\"fittingGatePassed\":"
         << (fitting_gate ? "true" : "false")
         << "},\n  \"heldoutRan\":" << (heldout_ran ? "true" : "false")
         << ",\n  \"heldout\":";
  if (!heldout_ran) output << "null";
  else writeCohort(output, heldout, pair_policies);
  output << ",\n  \"heldoutGatePassed\":"
         << (heldout_gate ? "true" : "false")
         << ",\n  \"screenRan\":" << (screen_ran ? "true" : "false")
         << ",\n  \"screen\":";
  if (!screen_ran) output << "null";
  else writeCohort(output, screen, pair_policies);
  output << ",\n  \"screenGatePassed\":"
         << (screen_gate ? "true" : "false")
         << ",\n  \"confirmationRan\":"
         << (confirmation_ran ? "true" : "false")
         << ",\n  \"confirmation\":";
  if (!confirmation_ran) output << "null";
  else writeCohort(output, confirmation, pair_policies);
  output << ",\n  \"confirmationGatePassed\":"
         << (confirmation_gate ? "true" : "false")
         << ",\n  \"qualified\":"
         << (confirmation_ran && confirmation_gate ? "true" : "false")
         << ",\n  \"wallSeconds\":" << wall_seconds
         << ",\n  \"totalExperimentWallSeconds\":"
         << (kPriorFirstSeedWallSeconds + wall_seconds)
         << ",\n  \"peakRssBytes\":" << phase::peakRssBytes() << "\n}\n";
  output.close();

  std::cout << "adverse-stop=" << (adverse_stop ? "yes" : "no")
            << " champion=" << champion
            << " fitting=" << (fitting_gate ? "pass" : "not-pass")
            << " heldout=" << (heldout_gate ? "pass" : "not-pass")
            << " screen=" << (screen_gate ? "pass" : "not-pass")
            << " confirmation="
            << (confirmation_gate ? "pass" : "not-pass")
            << " resource-stop="
            << (resource_stop_stage.empty() ? "none" : resource_stop_stage)
            << " artifact=" << options.output << '\n';
  return confirmation_ran && confirmation_gate ? 0 : 2;
}

}  // namespace drop7::fair_reveal_reward

#ifndef DROP7_FAIR_REVEAL_REWARD_LIBRARY
int main(int argc, char** argv) {
  try {
    const auto options = drop7::fair_reveal_reward::parseOptions(argc, argv);
    return drop7::fair_reveal_reward::run(options);
  } catch (const std::exception& error) {
    std::cerr << "drop7_fair_reveal_reward: " << error.what() << '\n';
    return 1;
  }
}
#endif
