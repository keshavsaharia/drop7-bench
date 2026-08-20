// Runs one bounded risk-sensitive ablation over the reference fair
// depth-three evaluator.  Seven fixed public root scenarios differ only in the
// immediate chance transition; every observed successor then uses the exact
// non-clairvoyant fair continuation search for two more action plies.
#define DROP7_FAIR_ONLY_HORIZON_LIBRARY
#include "../reference/fair-only-horizon.cpp"
#undef DROP7_FAIR_ONLY_HORIZON_LIBRARY

#include <unordered_set>

namespace drop7::fair_root_risk {

namespace fair = drop7::fair_only_horizon;

constexpr int kRootScenarios = 7;
constexpr int kContinuationDepth = 2;
constexpr double kMeanWeight = 0.75;
constexpr double kCvarWeight = 0.25;
constexpr double kCvarFraction = 0.25;
constexpr std::uint32_t kRootScenarioDomain = 0x5249'534bu;
constexpr std::uint32_t kRootRevealDomain = 0x5245'564cu;
constexpr std::uint32_t kRootDiscDomain = 0x4449'5343u;
constexpr std::uint32_t kScreenStart = 0x3e9d'0000u;
constexpr int kScreenGames = 8;
constexpr std::uint32_t kConfirmationStart = 0x3e9e'0000u;
constexpr int kConfirmationGames = 16;
constexpr int kMaximumMoves = 1'000;
constexpr int kParallelism = 4;

static_assert(kLevelBonus == 7'000);
static_assert(fair::kDepth == kContinuationDepth + 1);
static_assert(fair::kChanceSamples == 5);
static_assert(kRootScenarios == 7);
static_assert(kMeanWeight == 0.75 && kCvarWeight == 0.25);
static_assert(kMeanWeight + kCvarWeight == 1.0);
static_assert(kCvarFraction == 0.25);
static_assert(kMaximumMoves == fair::kMaximumMoves);
static_assert((kScreenStart >> 24) != 0x7du &&
              (kScreenStart >> 24) != 0xd7u);
static_assert((kConfirmationStart >> 24) != 0x7du &&
              (kConfirmationStart >> 24) != 0xd7u);
static_assert(kScreenStart + kScreenGames < kConfirmationStart);

std::mutex risk_progress_mutex;

std::uint32_t publicHash(const State& source) {
  bool ignored = false;
  const State state = cfpi::detail::canonicalState(source, ignored);
  std::uint32_t hash = 0x811c'9dc5u;
  for (const std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint32_t>(cell + 1u);
    hash *= 0x0100'0193u;
  }
  hash ^= state.next_disc;
  hash *= 0x0100'0193u;
  hash ^= static_cast<std::uint32_t>(state.moves_remaining);
  return mix32(hash);
}

std::uint32_t safePublicSeed(std::uint32_t seed) {
  const std::uint32_t family = seed >> 24;
  return family == 0x7du || family == 0xd7u ? seed ^ 0x4000'0000u : seed;
}

std::uint32_t rootScenarioSeed(const State& canonical, int scenario) {
  if (scenario < 0 || scenario >= kRootScenarios) {
    throw std::invalid_argument("invalid fair root scenario");
  }
  return safePublicSeed(mix32(
      publicHash(canonical) ^ kRootScenarioDomain ^
      (static_cast<std::uint32_t>(scenario + 1) * 0x9e37'79b9u)));
}

std::uint32_t eventBits(std::uint32_t seed, std::uint32_t domain,
                        int event) {
  return mix32(seed ^ domain ^
               (static_cast<std::uint32_t>(event + 1) * 0x85eb'ca6bu));
}

std::uint8_t bitsToDisc(std::uint32_t bits) {
  return static_cast<std::uint8_t>(
      ((static_cast<std::uint64_t>(bits) * kBoardSize) >> 32) + 1u);
}

struct RootScenarioRandom {
  std::uint32_t seed = 0;
  int event = 0;

  std::uint8_t nextDisc() {
    return bitsToDisc(eventBits(seed, kRootRevealDomain, event++));
  }
};

std::uint8_t scenarioNextDisc(std::uint32_t seed) {
  return bitsToDisc(eventBits(seed, kRootDiscDomain, 0));
}

double empiricalLowerCvar25(
    const std::array<double, kRootScenarios>& scenarios) {
  std::array<double, kRootScenarios> sorted = scenarios;
  std::sort(sorted.begin(), sorted.end());
  constexpr double tail_mass = kCvarFraction * kRootScenarios;
  constexpr int whole = static_cast<int>(tail_mass);
  constexpr double fractional = tail_mass - whole;
  static_assert(whole == 1);
  static_assert(fractional == 0.75);
  double total = 0;
  for (int index = 0; index < whole; ++index) total += sorted[index];
  total += fractional * sorted[whole];
  return total / tail_mass;
}

struct RootActionRisk {
  std::array<double, kRootScenarios> scenarios{};
  double mean = -std::numeric_limits<double>::infinity();
  double cvar25 = -std::numeric_limits<double>::infinity();
  double robust = -std::numeric_limits<double>::infinity();
};

RootActionRisk evaluateRootAction(const State& canonical, int action,
                                  fair::SearchContext& context) {
  if (!isLegal(canonical.board, action)) {
    throw std::invalid_argument("illegal fair root-risk action");
  }
  RootActionRisk result;
  for (int scenario = 0; scenario < kRootScenarios; ++scenario) {
    fair::checkBudget(context);
    const std::uint32_t seed = rootScenarioSeed(canonical, scenario);
    RootScenarioRandom random{seed, 0};
    MoveResult move;
    const bool played =
        cfpi::detail::playMoveSampled(canonical, action, random, move);
    ++context.work;
    if (!played) {
      result.scenarios[scenario] = fair::kTerminalUtility;
      continue;
    }
    const double immediate = static_cast<double>(move.score_delta);
    if (move.state.game_over) {
      result.scenarios[scenario] = immediate + fair::kTerminalUtility;
      continue;
    }
    move.state.score = 0;
    // The next visible disc is indexed only by the public root and scenario,
    // not by how many covers this sibling happened to reveal.
    move.state.next_disc = scenarioNextDisc(seed);
    bool ignored = false;
    const State next = cfpi::detail::canonicalState(move.state, ignored);
    result.scenarios[scenario] =
        immediate + fair::bestFutureValue(next, kContinuationDepth, context);
  }
  result.mean = std::accumulate(result.scenarios.begin(),
                                result.scenarios.end(), 0.0) /
                kRootScenarios;
  result.cvar25 = empiricalLowerCvar25(result.scenarios);
  result.robust = kMeanWeight * result.mean +
                  kCvarWeight * result.cvar25;
  return result;
}

struct RiskDecision {
  int action = -1;
  bool complete = false;
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
  std::uint64_t cache_hits = 0;
  std::size_t cache_entries = 0;
  std::array<RootActionRisk, kBoardSize> actions{};
};

// Deliberately accepts no game seed.  All chance scenarios are derived from
// the canonical public board, visible next disc, and moves-until-rise only.
RiskDecision chooseRiskAction(const State& source) {
  if (source.game_over) return {};
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  fair::SearchContext context;
  RiskDecision result;
  int canonical_action = -1;
  double best = -std::numeric_limits<double>::infinity();
  try {
    for (const int action : cfpi::detail::kColumnOrder) {
      if (!isLegal(canonical.board, action)) continue;
      result.actions[action] = evaluateRootAction(canonical, action, context);
      if (result.actions[action].robust > best) {
        best = result.actions[action].robust;
        canonical_action = action;
      }
    }
    result.complete = canonical_action >= 0;
  } catch (const fair::WorkLimitReached&) {
    result.complete = false;
  }
  if (canonical_action < 0) canonical_action = centerFirstMove(canonical.board);
  result.action = mirrored && canonical_action >= 0
                      ? kBoardSize - 1 - canonical_action
                      : canonical_action;
  result.nodes = context.nodes;
  result.work = context.work;
  result.cache_hits = context.cache_hits;
  result.cache_entries = context.cache.size();
  if (mirrored) {
    std::array<RootActionRisk, kBoardSize> reflected{};
    for (int action = 0; action < kBoardSize; ++action) {
      reflected[kBoardSize - 1 - action] = result.actions[action];
    }
    result.actions = reflected;
  }
  return result;
}

struct RiskGameResult {
  fair::GameResult game;
  std::uint64_t switches = 0;
  std::uint64_t root_scenarios = 0;
  std::uint64_t switch_audit_work = 0;
  std::uint64_t switch_audit_nodes = 0;
  double selected_mean_total = 0.0;
  double selected_cvar_total = 0.0;
  double selected_tail_gap_total = 0.0;
};

void reportRiskGame(std::string_view label, const RiskGameResult& result) {
  const std::lock_guard<std::mutex> lock(risk_progress_mutex);
  std::cerr << label << " seed 0x" << std::hex << result.game.seed
            << std::dec << ' ' << result.game.score << " ("
            << result.game.moves << " moves"
            << (result.game.censored ? ", capped" : "")
            << ", switches " << result.switches << ", clears "
            << result.game.numbered_cleared << ", reveals "
            << result.game.covers_revealed << ", policy work "
            << result.game.work << ", audit work "
            << result.switch_audit_work << ")\n";
}

RiskGameResult runRiskGame(std::uint32_t seed, std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  RiskGameResult result;
  result.game.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    const RiskDecision risk = chooseRiskAction(state);
    if (!risk.complete || !isLegal(state.board, risk.action)) {
      throw std::runtime_error("fair root-risk search did not complete");
    }
    const fair::SearchDecision fair_decision = fair::chooseFairAction(state);
    if (!fair_decision.complete ||
        fair_decision.completed_depth != fair::kDepth ||
        !isLegal(state.board, fair_decision.action)) {
      throw std::runtime_error("fair switch audit did not complete");
    }
    result.switches += risk.action != fair_decision.action;
    result.switch_audit_work += fair_decision.work;
    result.switch_audit_nodes += fair_decision.nodes;
    result.game.work += risk.work;
    result.game.nodes += risk.nodes;
    result.game.cache_hits += risk.cache_hits;
    result.game.maximum_cache_entries =
        std::max(result.game.maximum_cache_entries, risk.cache_entries);
    int legal_count = 0;
    legalColumns(state.board, legal_count);
    result.root_scenarios +=
        static_cast<std::uint64_t>(legal_count * kRootScenarios);
    const RootActionRisk& selected = risk.actions[risk.action];
    result.selected_mean_total += selected.mean;
    result.selected_cvar_total += selected.cvar25;
    result.selected_tail_gap_total += selected.mean - selected.cvar25;
    MoveResult move;
    if (!playHeadlessMove(state, seed, risk.action, move)) {
      throw std::runtime_error("fair root-risk transition failed");
    }
    fair::observeMove(move, result.game);
  }
  result.game.score = state.score;
  result.game.moves = state.moves_played;
  result.game.censored = !state.game_over;
  result.game.peak_rss_bytes = fair::peakRssBytes();
  result.game.elapsed_seconds = std::chrono::duration<double>(
                                    std::chrono::steady_clock::now() - started)
                                    .count();
  reportRiskGame(label, result);
  return result;
}

struct Cohort {
  std::vector<fair::GameResult> fair;
  std::vector<RiskGameResult> risk;
  double wall_seconds = 0.0;
};

Cohort runCohort(std::uint32_t seed_start, int games,
                 std::string_view phase) {
  const auto started = std::chrono::steady_clock::now();
  Cohort result;
  result.fair.resize(static_cast<std::size_t>(games));
  result.risk.resize(static_cast<std::size_t>(games));
  std::atomic<int> next{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next.fetch_add(1);
        if (game >= games) return;
        const std::uint32_t seed =
            seed_start + static_cast<std::uint32_t>(game);
        result.fair[static_cast<std::size_t>(game)] = fair::runFairGame(
            seed, std::string(phase) + "-fair-only-d3-s5");
        result.risk[static_cast<std::size_t>(game)] = runRiskGame(
            seed, std::string(phase) + "-fair-root-risk-s7");
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.wall_seconds = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started)
                            .count();
  return result;
}

struct RiskSummary {
  fair::Summary game;
  double mean_switches = 0.0;
  double switch_rate = 0.0;
  std::uint64_t root_scenarios = 0;
  double scenarios_per_move = 0.0;
  std::uint64_t switch_audit_work = 0;
  std::uint64_t switch_audit_nodes = 0;
  double audit_work_per_move = 0.0;
  double observed_work_per_move = 0.0;
  double mean_selected_value = 0.0;
  double mean_selected_cvar25 = 0.0;
  double mean_selected_tail_gap = 0.0;
};

RiskSummary summarizeRisk(const std::vector<RiskGameResult>& games) {
  if (games.empty()) throw std::invalid_argument("empty risk cohort");
  std::vector<fair::GameResult> common;
  common.reserve(games.size());
  RiskSummary result;
  std::uint64_t moves = 0;
  std::uint64_t switches = 0;
  for (const RiskGameResult& game : games) {
    common.push_back(game.game);
    moves += static_cast<std::uint64_t>(game.game.moves);
    switches += game.switches;
    result.mean_switches +=
        static_cast<double>(game.switches) / games.size();
    result.root_scenarios += game.root_scenarios;
    result.switch_audit_work += game.switch_audit_work;
    result.switch_audit_nodes += game.switch_audit_nodes;
    result.mean_selected_value +=
        game.selected_mean_total / games.size();
    result.mean_selected_cvar25 +=
        game.selected_cvar_total / games.size();
    result.mean_selected_tail_gap +=
        game.selected_tail_gap_total / games.size();
  }
  result.game = fair::summarize(common);
  const double move_count =
      static_cast<double>(std::max<std::uint64_t>(1, moves));
  result.switch_rate = switches / move_count;
  result.scenarios_per_move = result.root_scenarios / move_count;
  result.audit_work_per_move = result.switch_audit_work / move_count;
  result.observed_work_per_move =
      (result.game.work + result.switch_audit_work) / move_count;
  result.mean_selected_value /= move_count / games.size();
  result.mean_selected_cvar25 /= move_count / games.size();
  result.mean_selected_tail_gap /= move_count / games.size();
  return result;
}

struct PairedSummary {
  fair::DifferenceStats score;
  fair::DifferenceStats moves;
  fair::DifferenceStats numbered_cleared;
  fair::DifferenceStats covers_revealed;
  fair::DifferenceStats maximum_chain;
};

PairedSummary pairedSummary(const Cohort& cohort) {
  if (cohort.fair.size() != cohort.risk.size() || cohort.fair.empty()) {
    throw std::invalid_argument("invalid fair root-risk paired cohort");
  }
  std::vector<double> scores;
  std::vector<double> moves;
  std::vector<double> cleared;
  std::vector<double> revealed;
  std::vector<double> chains;
  for (std::size_t game = 0; game < cohort.fair.size(); ++game) {
    const fair::GameResult& baseline = cohort.fair[game];
    const fair::GameResult& candidate = cohort.risk[game].game;
    scores.push_back(static_cast<double>(candidate.score - baseline.score));
    moves.push_back(static_cast<double>(candidate.moves - baseline.moves));
    cleared.push_back(static_cast<double>(candidate.numbered_cleared) -
                      baseline.numbered_cleared);
    revealed.push_back(static_cast<double>(candidate.covers_revealed) -
                       baseline.covers_revealed);
    chains.push_back(
        static_cast<double>(candidate.maximum_chain - baseline.maximum_chain));
  }
  return {
      fair::differences(scores), fair::differences(moves),
      fair::differences(cleared), fair::differences(revealed),
      fair::differences(chains),
  };
}

bool improvesBothMeans(const fair::Summary& baseline,
                       const RiskSummary& candidate) {
  return candidate.game.mean_score > baseline.mean_score &&
         candidate.game.mean_moves > baseline.mean_moves;
}

void writeRiskGame(std::ostream& output, const RiskGameResult& result) {
  output << "{\"game\":";
  fair::writeGame(output, result.game);
  output << ",\"switches\":" << result.switches
         << ",\"rootScenarios\":" << result.root_scenarios
         << ",\"switchAuditWork\":" << result.switch_audit_work
         << ",\"switchAuditNodes\":" << result.switch_audit_nodes
         << ",\"selectedMeanTotal\":" << result.selected_mean_total
         << ",\"selectedCvar25Total\":" << result.selected_cvar_total
         << ",\"selectedTailGapTotal\":"
         << result.selected_tail_gap_total << '}';
}

void writeRiskSummary(std::ostream& output, const RiskSummary& result) {
  output << "{\"game\":";
  fair::writeSummary(output, result.game);
  output << ",\"meanSwitches\":" << result.mean_switches
         << ",\"switchRate\":" << result.switch_rate
         << ",\"rootScenarios\":" << result.root_scenarios
         << ",\"scenariosPerMove\":" << result.scenarios_per_move
         << ",\"switchAuditWork\":" << result.switch_audit_work
         << ",\"switchAuditNodes\":" << result.switch_audit_nodes
         << ",\"auditWorkPerMove\":" << result.audit_work_per_move
         << ",\"observedWorkPerMove\":"
         << result.observed_work_per_move
         << ",\"meanSelectedValue\":" << result.mean_selected_value
         << ",\"meanSelectedCvar25\":"
         << result.mean_selected_cvar25
         << ",\"meanSelectedTailGap\":"
         << result.mean_selected_tail_gap << '}';
}

void writePaired(std::ostream& output, const PairedSummary& result) {
  output << "{\"score\":";
  fair::writeDifference(output, result.score);
  output << ",\"moves\":";
  fair::writeDifference(output, result.moves);
  output << ",\"numberedCleared\":";
  fair::writeDifference(output, result.numbered_cleared);
  output << ",\"coversRevealed\":";
  fair::writeDifference(output, result.covers_revealed);
  output << ",\"maximumChain\":";
  fair::writeDifference(output, result.maximum_chain);
  output << '}';
}

void writePairs(std::ostream& output, const Cohort& cohort) {
  output << '[';
  for (std::size_t game = 0; game < cohort.fair.size(); ++game) {
    if (game > 0) output << ',';
    output << "{\"seed\":" << cohort.fair[game].seed
           << ",\"fairOnlyD3S5\":";
    fair::writeGame(output, cohort.fair[game]);
    output << ",\"rootRisk\":";
    writeRiskGame(output, cohort.risk[game]);
    output << '}';
  }
  output << ']';
}

void writeCohort(std::ostream& output, std::uint32_t seed_start,
                 const Cohort& cohort, const fair::Summary& baseline,
                 const RiskSummary& candidate,
                 const PairedSummary& paired, bool passed) {
  output << "{\"seedStart\":" << seed_start
         << ",\"maximumMoves\":" << kMaximumMoves
         << ",\"fairOnlyD3S5\":";
  fair::writeSummary(output, baseline);
  output << ",\"rootRisk\":";
  writeRiskSummary(output, candidate);
  output << ",\"paired\":";
  writePaired(output, paired);
  output << ",\"wallSeconds\":" << cohort.wall_seconds
         << ",\"passed\":" << (passed ? "true" : "false")
         << ",\"pairs\":";
  writePairs(output, cohort);
  output << '}';
}

bool selfTest(std::ostream& output) {
  std::ostringstream fair_output;
  const bool fair_test = fair::selfTest(fair_output);
  const State source = fair::fixtureState(fair::kTypeScriptFixtures[1]);
  const RiskDecision first = chooseRiskAction(source);
  const RiskDecision repeat = chooseRiskAction(source);
  bool deterministic = first.complete && repeat.complete &&
                       first.action == repeat.action &&
                       first.work == repeat.work &&
                       first.nodes == repeat.nodes &&
                       first.cache_hits == repeat.cache_hits &&
                       first.cache_entries == repeat.cache_entries;
  for (int action = 0; action < kBoardSize; ++action) {
    deterministic = deterministic &&
                    first.actions[action].scenarios ==
                        repeat.actions[action].scenarios &&
                    first.actions[action].robust ==
                        repeat.actions[action].robust;
  }

  State metadata = source;
  metadata.score = 9'876'543;
  metadata.level = 417;
  metadata.moves_played = 991;
  const RiskDecision metadata_decision = chooseRiskAction(metadata);
  bool metadata_blind = metadata_decision.action == first.action &&
                        metadata_decision.work == first.work;
  for (int action = 0; action < kBoardSize; ++action) {
    metadata_blind = metadata_blind &&
                     metadata_decision.actions[action].scenarios ==
                         first.actions[action].scenarios;
  }

  State reflected = source;
  reflected.board = cfpi::detail::mirrorBoard(source.board);
  const RiskDecision reflected_decision = chooseRiskAction(reflected);
  bool reflection_safe =
      reflected_decision.action == kBoardSize - 1 - first.action &&
      reflected_decision.work == first.work;
  for (int action = 0; action < kBoardSize; ++action) {
    reflection_safe = reflection_safe &&
        reflected_decision.actions[kBoardSize - 1 - action].scenarios ==
            first.actions[action].scenarios;
  }

  std::unordered_set<std::uint32_t> scenario_seeds;
  bool public_scenarios = true;
  for (int scenario = 0; scenario < kRootScenarios; ++scenario) {
    const std::uint32_t seed = rootScenarioSeed(source, scenario);
    scenario_seeds.insert(seed);
    public_scenarios = public_scenarios &&
                       seed == rootScenarioSeed(metadata, scenario) &&
                       seed == rootScenarioSeed(reflected, scenario) &&
                       (seed >> 24) != 0x7du &&
                       (seed >> 24) != 0xd7u;
  }
  public_scenarios = public_scenarios &&
                     scenario_seeds.size() == kRootScenarios;

  const std::array<double, kRootScenarios> cvar_fixture{{
      6.0, 0.0, 5.0, 1.0, 4.0, 2.0, 3.0,
  }};
  const double cvar = empiricalLowerCvar25(cvar_fixture);
  const double robust = kMeanWeight * 3.0 + kCvarWeight * cvar;
  const bool aggregator_exact = std::abs(cvar - 3.0 / 7.0) < 1.0e-12 &&
                                std::abs(robust - 33.0 / 14.0) < 1.0e-12;
  const bool legal = isLegal(source.board, first.action);
  const bool bounded = first.work <= fair::kMaximumWork &&
                       first.cache_entries <= fair::kMaximumCacheEntries;
  const bool fixed_protocol =
      kLevelBonus == 7'000 && kRootScenarios == 7 &&
      kContinuationDepth == 2 && kMeanWeight == 0.75 &&
      kCvarWeight == 0.25 && kCvarFraction == 0.25 &&
      kMaximumMoves == 1'000 && kScreenStart == 0x3e9d'0000u &&
      kConfirmationStart == 0x3e9e'0000u &&
      (kScreenStart >> 24) != 0x7du &&
      (kScreenStart >> 24) != 0xd7u &&
      (kConfirmationStart >> 24) != 0x7du &&
      (kConfirmationStart >> 24) != 0xd7u;
  // Game seed blindness is structural: chooseRiskAction has no seed argument.
  constexpr bool game_seed_parameter_absent = true;
  const bool passed = fair_test && deterministic && metadata_blind &&
                      reflection_safe && public_scenarios &&
                      aggregator_exact && legal && bounded &&
                      fixed_protocol && game_seed_parameter_absent;
  output << fair_output.str();
  output << std::setprecision(12)
         << "FAIR_ROOT_RISK_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"fairSearchVerified\":"
         << (fair_test ? "true" : "false")
         << ",\"deterministic\":"
         << (deterministic ? "true" : "false")
         << ",\"metadataBlind\":"
         << (metadata_blind ? "true" : "false")
         << ",\"gameSeedParameterAbsent\":true"
         << ",\"reflectionSafe\":"
         << (reflection_safe ? "true" : "false")
         << ",\"independentPublicScenarios\":"
         << (public_scenarios ? "true" : "false")
         << ",\"aggregatorExact\":"
         << (aggregator_exact ? "true" : "false")
         << ",\"complete\":" << (first.complete ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false")
         << ",\"bounded\":" << (bounded ? "true" : "false")
         << ",\"work\":" << first.work
         << ",\"cacheEntries\":" << first.cache_entries
         << ",\"levelBonus\":" << kLevelBonus << "}\n";
  return passed;
}

struct Options {
  std::string output = "/tmp/drop7-fair-root-risk.json";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing fair root-risk option value");
    }
    const std::string argument = argv[index];
    if (argument == "--output") {
      result.output = argv[index + 1];
    } else {
      throw std::invalid_argument("unknown fair root-risk option " + argument);
    }
  }
  return result;
}

void writeArtifact(const Options& options, const Cohort& screen,
                   const fair::Summary& screen_baseline,
                   const RiskSummary& screen_candidate,
                   const PairedSummary& screen_paired,
                   bool screen_passed,
                   const Cohort* confirmation,
                   const fair::Summary* confirmation_baseline,
                   const RiskSummary* confirmation_candidate,
                   const PairedSummary* confirmation_paired,
                   bool confirmation_passed, double total_wall) {
  std::ofstream output(options.output);
  if (!output) {
    throw std::runtime_error("could not open fair root-risk artifact");
  }
  output << std::setprecision(10)
         << "{\n  \"experiment\":\"fair-root-risk-v1\",\n"
         << "  \"preregistered\":true,\n"
         << "  \"publicStateOnly\":true,\n"
         << "  \"gameSeedPolicyInput\":false,\n"
         << "  \"metadataInputs\":{\"score\":false,\"level\":false,"
            "\"movesPlayed\":false},\n"
         << "  \"scoring\":{\"levelBonus\":7000},\n"
         << "  \"baseline\":{\"name\":\"confirmed-fair-only-d3-s5\","
            "\"depth\":"
         << fair::kDepth << ",\"chanceSamples\":"
         << fair::kChanceSamples << ",\"maximumWork\":"
         << fair::kMaximumWork << ",\"maximumCacheEntries\":"
         << fair::kMaximumCacheEntries << "},\n"
         << "  \"candidate\":{\"rootScenarios\":" << kRootScenarios
         << ",\"continuationDepth\":" << kContinuationDepth
         << ",\"totalActionPlies\":" << kContinuationDepth + 1
         << ",\"scenarioKind\":"
            "\"independent-fixed-public-immediate-transition\","
            "\"futureScenarioTapeUsed\":false,"
            "\"aggregator\":{\"meanWeight\":"
         << kMeanWeight << ",\"cvar25Weight\":" << kCvarWeight
         << ",\"empiricalTailMass\":1.75,"
            "\"fractionalOrderWeights\":[1.0,0.75]}},\n"
         << "  \"protocol\":{\"screenSeedStart\":" << kScreenStart
         << ",\"screenGames\":" << kScreenGames
         << ",\"confirmationSeedStart\":" << kConfirmationStart
         << ",\"confirmationGames\":" << kConfirmationGames
         << ",\"maximumMoves\":" << kMaximumMoves
         << ",\"parallelism\":" << kParallelism << "},\n"
         << "  \"screen\":";
  writeCohort(output, kScreenStart, screen, screen_baseline,
              screen_candidate, screen_paired, screen_passed);
  output << ",\n  \"confirmation\":";
  if (confirmation == nullptr) {
    output << "null";
  } else {
    writeCohort(output, kConfirmationStart, *confirmation,
                *confirmation_baseline, *confirmation_candidate,
                *confirmation_paired, confirmation_passed);
  }
  output << ",\n  \"screenPassed\":"
         << (screen_passed ? "true" : "false")
         << ",\n  \"confirmationRan\":"
         << (confirmation != nullptr ? "true" : "false")
         << ",\n  \"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\n  \"qualified\":"
         << (screen_passed && confirmation_passed ? "true" : "false")
         << ",\n  \"totalWallSeconds\":" << total_wall
         << ",\n  \"peakRssBytes\":" << fair::peakRssBytes() << "\n}\n";
  if (!output) {
    throw std::runtime_error("could not write fair root-risk artifact");
  }
}

int run(const Options& options, std::ostream& output) {
  const auto started = std::chrono::steady_clock::now();
  const Cohort screen = runCohort(kScreenStart, kScreenGames, "screen");
  const fair::Summary screen_baseline = fair::summarize(screen.fair);
  const RiskSummary screen_candidate = summarizeRisk(screen.risk);
  const PairedSummary screen_paired = pairedSummary(screen);
  const bool screen_passed =
      improvesBothMeans(screen_baseline, screen_candidate);

  Cohort confirmation;
  fair::Summary confirmation_baseline;
  RiskSummary confirmation_candidate;
  PairedSummary confirmation_paired;
  bool confirmation_passed = false;
  if (screen_passed) {
    confirmation = runCohort(kConfirmationStart, kConfirmationGames,
                             "confirmation");
    confirmation_baseline = fair::summarize(confirmation.fair);
    confirmation_candidate = summarizeRisk(confirmation.risk);
    confirmation_paired = pairedSummary(confirmation);
    confirmation_passed =
        improvesBothMeans(confirmation_baseline, confirmation_candidate);
  }
  const double total_wall = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - started)
                                .count();
  writeArtifact(
      options, screen, screen_baseline, screen_candidate, screen_paired,
      screen_passed, screen_passed ? &confirmation : nullptr,
      screen_passed ? &confirmation_baseline : nullptr,
      screen_passed ? &confirmation_candidate : nullptr,
      screen_passed ? &confirmation_paired : nullptr,
      confirmation_passed, total_wall);
  output << std::fixed << std::setprecision(3)
         << "FAIR_ROOT_RISK_RESULT {\"levelBonus\":7000"
         << ",\"screenFairScore\":" << screen_baseline.mean_score
         << ",\"screenFairMoves\":" << screen_baseline.mean_moves
         << ",\"screenRiskScore\":"
         << screen_candidate.game.mean_score
         << ",\"screenRiskMoves\":"
         << screen_candidate.game.mean_moves
         << ",\"screenScoreDelta\":" << screen_paired.score.mean
         << ",\"screenMoveDelta\":" << screen_paired.moves.mean
         << ",\"screenSwitchRate\":" << screen_candidate.switch_rate
         << ",\"screenPassed\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmationRan\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\"peakRssBytes\":" << fair::peakRssBytes()
         << ",\"totalWallSeconds\":" << total_wall
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return 0;
}

}  // namespace drop7::fair_root_risk

int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::fair_root_risk::selfTest(std::cout) ? EXIT_SUCCESS
                                                        : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::fair_root_risk::parseOptions(argc, argv, 2);
      return drop7::fair_root_risk::run(options, std::cout);
    }
    std::cerr << "usage: drop7_fair_root_risk --self-test | --run "
                 "[--output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_fair_root_risk: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
