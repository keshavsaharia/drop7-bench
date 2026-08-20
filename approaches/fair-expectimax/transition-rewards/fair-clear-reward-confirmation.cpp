#define DROP7_FAIR_PHASE_ENERGY_RELEASE_LIBRARY
#include "fair-phase-energy-release.cpp"
#undef DROP7_FAIR_PHASE_ENERGY_RELEASE_LIBRARY

// Fixed-candidate confirmation of the isolated +600 numbered-clear transition
// reward selected on 0x3de50000...003.  There is no phase term and no remaining
// candidate menu.  Each later range is read only after its preceding gate.
namespace drop7::fair_clear_reward_confirmation {

namespace phase = fair_phase_energy_release;
using Clock = std::chrono::steady_clock;

constexpr int kStockConfig = 0;
constexpr int kCandidateConfig = 1;
constexpr std::uint32_t kSelectionStart = 0x3de5'0000u;
constexpr int kSelectionGames = 4;
constexpr std::uint32_t kHeldoutStart = 0x3de6'0000u;
constexpr int kHeldoutGames = 8;
constexpr std::uint32_t kScreenStart = 0x3eb3'0000u;
constexpr int kScreenGames = 8;
constexpr std::uint32_t kConfirmationStart = 0x3eb4'0000u;
constexpr int kConfirmationGames = 16;
constexpr int kDefaultThreads = 8;
constexpr double kWallLimitSeconds = 35.0 * 60.0;
constexpr double kProjectionSafetyFactor = 1.50;
// Conservative upper bound inferred before this run from the resource-capped
// selection process: at most eight CPU-minutes per complete D4 game.
constexpr double kInitialSecondsPerGame = 8.0 * 60.0;

constexpr std::array<std::int64_t, kSelectionGames> kStockSelectionScores{{
    185'341, 51'625, 135'827, 200'405,
}};
constexpr std::array<int, kSelectionGames> kStockSelectionMoves{{
    105, 40, 85, 125,
}};
constexpr std::array<std::uint64_t, kSelectionGames> kStockSelectionClears{{
    204, 62, 158, 266,
}};
constexpr std::array<std::int64_t, kSelectionGames> kCandidateSelectionScores{{
    151'969, 154'863, 269'270, 400'644,
}};
constexpr std::array<int, kSelectionGames> kCandidateSelectionMoves{{
    100, 100, 175, 250,
}};
constexpr std::array<std::uint64_t, kSelectionGames>
    kCandidateSelectionClears{{195, 199, 376, 559}};

static_assert(phase::kMenu[kCandidateConfig].clear_reward == 600.0);
static_assert(phase::kMenu[kCandidateConfig].phase_strength == 0.0);
static_assert(phase::kDepth == 4 && phase::kChanceSamples == 5);
static_assert(phase::kMaximumMoves == 1'000);
static_assert(kSelectionStart + kSelectionGames < kHeldoutStart);
static_assert(kHeldoutStart + kHeldoutGames < kScreenStart);
static_assert(kScreenStart + kScreenGames < kConfirmationStart);
static_assert((kSelectionStart >> 24u) == 0x3du &&
              (kHeldoutStart >> 24u) == 0x3du &&
              (kScreenStart >> 24u) == 0x3eu &&
              (kConfirmationStart >> 24u) == 0x3eu);

struct PairedSummary {
  double mean_score_delta = 0.0;
  double mean_moves_delta = 0.0;
  int score_wins = 0;
  int move_wins = 0;
  int score_ties = 0;
  int move_ties = 0;
};

PairedSummary pairedSummary(const phase::MenuGames& games) {
  const auto& baseline = games[kStockConfig];
  const auto& candidate = games[kCandidateConfig];
  if (baseline.size() != candidate.size()) {
    throw std::runtime_error("unpaired clear-reward cohort");
  }
  PairedSummary result;
  for (std::size_t index = 0; index < baseline.size(); ++index) {
    const std::int64_t score_delta =
        candidate[index].score - baseline[index].score;
    const int move_delta = candidate[index].moves - baseline[index].moves;
    result.mean_score_delta += static_cast<double>(score_delta);
    result.mean_moves_delta += move_delta;
    if (score_delta > 0) ++result.score_wins;
    else if (score_delta == 0) ++result.score_ties;
    if (move_delta > 0) ++result.move_wins;
    else if (move_delta == 0) ++result.move_ties;
  }
  if (!baseline.empty()) {
    result.mean_score_delta /= baseline.size();
    result.mean_moves_delta /= baseline.size();
  }
  return result;
}

double measuredProjection(const phase::MenuGames& source, int source_games,
                          int target_games, int threads) {
  double cpu_seconds = 0.0;
  for (const int config : {kStockConfig, kCandidateConfig}) {
    for (const phase::GameResult& game : source[config]) {
      cpu_seconds += game.decision_seconds;
    }
  }
  if (source_games <= 0) return kWallLimitSeconds;
  return kProjectionSafetyFactor * cpu_seconds * target_games /
         (source_games * std::max(1, threads));
}

double initialHeldoutProjection(int threads) {
  return kProjectionSafetyFactor * kInitialSecondsPerGame *
         (2.0 * kHeldoutGames) / std::max(1, threads);
}

bool stageFits(const Clock::time_point& started, double projection) {
  const double elapsed =
      std::chrono::duration<double>(Clock::now() - started).count();
  return elapsed + projection <= kWallLimitSeconds;
}

bool noCensoring(const phase::Summary& baseline,
                 const phase::Summary& candidate) {
  return baseline.censored == 0 && candidate.censored == 0;
}

bool heldoutGate(const phase::MenuGames& games) {
  const phase::Summary baseline = phase::summarize(games[kStockConfig]);
  const phase::Summary candidate = phase::summarize(games[kCandidateConfig]);
  return noCensoring(baseline, candidate) &&
         candidate.mean_score > baseline.mean_score &&
         candidate.mean_moves > baseline.mean_moves &&
         candidate.clears_per_move >= baseline.clears_per_move &&
         candidate.reveals_per_move >= baseline.reveals_per_move;
}

bool screenGate(const phase::MenuGames& games) {
  const phase::Summary baseline = phase::summarize(games[kStockConfig]);
  const phase::Summary candidate = phase::summarize(games[kCandidateConfig]);
  return noCensoring(baseline, candidate) &&
         candidate.mean_score > baseline.mean_score &&
         candidate.mean_moves > baseline.mean_moves;
}

bool confirmationGate(const phase::MenuGames& games) {
  return heldoutGate(games);
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

void runSelfTests() {
  phase::runSelfTests();
  expect(phase::kMenu[kCandidateConfig].phase_strength == 0.0 &&
             phase::kMenu[kCandidateConfig].clear_reward == 600.0,
         "fixed candidate changed");
  for (int omitted = 0; omitted < kSelectionGames; ++omitted) {
    std::int64_t stock_score = 0;
    std::int64_t candidate_score = 0;
    int stock_moves = 0;
    int candidate_moves = 0;
    std::uint64_t stock_clears = 0;
    std::uint64_t candidate_clears = 0;
    for (int game = 0; game < kSelectionGames; ++game) {
      if (game == omitted) continue;
      stock_score += kStockSelectionScores[game];
      candidate_score += kCandidateSelectionScores[game];
      stock_moves += kStockSelectionMoves[game];
      candidate_moves += kCandidateSelectionMoves[game];
      stock_clears += kStockSelectionClears[game];
      candidate_clears += kCandidateSelectionClears[game];
    }
    expect(candidate_score > stock_score && candidate_moves > stock_moves &&
               static_cast<double>(candidate_clears) / candidate_moves >
                   static_cast<double>(stock_clears) / stock_moves,
           "selection evidence lost leave-one-out triple win");
  }
  expect(initialHeldoutProjection(kDefaultThreads) == 1'440.0,
         "initial resource projection changed");
}

void writePaired(std::ostream& output, const PairedSummary& summary) {
  output << "{\"meanScoreDelta\":" << summary.mean_score_delta
         << ",\"meanMovesDelta\":" << summary.mean_moves_delta
         << ",\"scoreWins\":" << summary.score_wins
         << ",\"moveWins\":" << summary.move_wins
         << ",\"scoreTies\":" << summary.score_ties
         << ",\"moveTies\":" << summary.move_ties << '}';
}

struct Options {
  std::string output = "/tmp/drop7-fair-clear-reward-confirmation.json";
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
  std::cerr << "clear-reward confirmation self-tests passed (peak RSS "
            << phase::peakRssBytes() << " bytes)\n";
  if (options.self_test_only) return 0;
  const std::vector<int> configs{kStockConfig, kCandidateConfig};
  phase::MenuGames heldout;
  phase::MenuGames screen;
  phase::MenuGames confirmation;
  bool heldout_ran = false;
  bool heldout_passed = false;
  bool screen_ran = false;
  bool screen_passed = false;
  bool confirmation_ran = false;
  bool confirmation_passed = false;
  std::string resource_stop_stage;

  const double heldout_projection = initialHeldoutProjection(options.threads);
  if (!stageFits(started, heldout_projection)) {
    resource_stop_stage = "heldout";
  } else {
    heldout_ran = true;
    heldout = phase::runMenu(kHeldoutStart, kHeldoutGames, configs,
                             options.threads, "clear-reward-heldout");
    heldout_passed = heldoutGate(heldout);
  }

  const double screen_projection =
      heldout_ran ? measuredProjection(heldout, kHeldoutGames, kScreenGames,
                                       options.threads)
                  : kWallLimitSeconds;
  if (heldout_passed) {
    if (!stageFits(started, screen_projection)) {
      resource_stop_stage = "screen";
    } else {
      screen_ran = true;
      screen = phase::runMenu(kScreenStart, kScreenGames, configs,
                              options.threads, "clear-reward-screen");
      screen_passed = screenGate(screen);
    }
  }

  const double confirmation_projection =
      screen_ran ? measuredProjection(screen, kScreenGames, kConfirmationGames,
                                      options.threads)
                 : kWallLimitSeconds;
  if (screen_passed) {
    if (!stageFits(started, confirmation_projection)) {
      resource_stop_stage = "confirmation";
    } else {
      confirmation_ran = true;
      confirmation = phase::runMenu(
          kConfirmationStart, kConfirmationGames, configs, options.threads,
          "clear-reward-confirmation");
      confirmation_passed = confirmationGate(confirmation);
    }
  }

  const double wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open confirmation artifact");
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"fair-d4-fixed-clear-reward-confirmation\",\n"
         << "  \"preregistered\":true,\n"
         << "  \"publicStateOnly\":true,\n"
         << "  \"candidate\":{\"clearReward\":600,\"phaseStrength\":0},\n"
         << "  \"selectionEvidence\":{\"seedStart\":" << kSelectionStart
         << ",\"games\":" << kSelectionGames
         << ",\"meanScoreDelta\":100887,\"meanMovesDelta\":67.5,"
            "\"clearThroughputDelta\":0.18273802817,"
            "\"leaveOneOutTripleWins\":4},\n"
         << "  \"search\":{\"depth\":4,\"chanceSamples\":5,"
            "\"fullWidth\":true,\"maximumMoves\":1000,"
            "\"maximumWork\":3200000,\"worstCaseWork\":3134950,"
            "\"maximumCacheEntries\":60000,"
            "\"worstCaseCacheEntries\":45430},\n"
         << "  \"resourceProtocol\":{\"wallLimitSeconds\":"
         << kWallLimitSeconds << ",\"projectionSafetyFactor\":"
         << kProjectionSafetyFactor << ",\"initialSecondsPerGame\":"
         << kInitialSecondsPerGame << ",\"threads\":" << options.threads
         << ",\"heldoutProjection\":" << heldout_projection
         << ",\"screenProjection\":" << screen_projection
         << ",\"confirmationProjection\":" << confirmation_projection
         << ",\"stopStage\":";
  if (resource_stop_stage.empty()) output << "null";
  else output << '\"' << resource_stop_stage << '\"';
  output << "},\n  \"heldoutRan\":" << (heldout_ran ? "true" : "false")
         << ",\n  \"heldout\":";
  if (!heldout_ran) output << "null";
  else {
    output << "{\"cohort\":";
    phase::writeCohort(output, heldout, configs);
    output << ",\"paired\":";
    writePaired(output, pairedSummary(heldout));
    output << ",\"passed\":" << (heldout_passed ? "true" : "false")
           << '}';
  }
  output << ",\n  \"screenRan\":" << (screen_ran ? "true" : "false")
         << ",\n  \"screen\":";
  if (!screen_ran) output << "null";
  else {
    output << "{\"cohort\":";
    phase::writeCohort(output, screen, configs);
    output << ",\"paired\":";
    writePaired(output, pairedSummary(screen));
    output << ",\"passed\":" << (screen_passed ? "true" : "false")
           << '}';
  }
  output << ",\n  \"confirmationRan\":"
         << (confirmation_ran ? "true" : "false")
         << ",\n  \"confirmation\":";
  if (!confirmation_ran) output << "null";
  else {
    output << "{\"cohort\":";
    phase::writeCohort(output, confirmation, configs);
    output << ",\"paired\":";
    writePaired(output, pairedSummary(confirmation));
    output << ",\"passed\":"
           << (confirmation_passed ? "true" : "false") << '}';
  }
  output << ",\n  \"qualified\":"
         << (confirmation_ran && confirmation_passed ? "true" : "false")
         << ",\n  \"wallSeconds\":" << wall_seconds
         << ",\n  \"peakRssBytes\":" << phase::peakRssBytes() << "\n}\n";
  output.close();

  std::cout << "heldout=" << (heldout_passed ? "pass" : "not-pass")
            << " screen=" << (screen_passed ? "pass" : "not-pass")
            << " confirmation="
            << (confirmation_passed ? "pass" : "not-pass")
            << " resource-stop="
            << (resource_stop_stage.empty() ? "none" : resource_stop_stage)
            << " artifact=" << options.output << '\n';
  return confirmation_ran && confirmation_passed ? 0 : 2;
}

}  // namespace drop7::fair_clear_reward_confirmation

#ifndef DROP7_FAIR_CLEAR_REWARD_CONFIRMATION_LIBRARY
int main(int argc, char** argv) {
  try {
    const auto options =
        drop7::fair_clear_reward_confirmation::parseOptions(argc, argv);
    return drop7::fair_clear_reward_confirmation::run(options);
  } catch (const std::exception& error) {
    std::cerr << "drop7_fair_clear_reward_confirmation: " << error.what()
              << '\n';
    return 1;
  }
}
#endif
