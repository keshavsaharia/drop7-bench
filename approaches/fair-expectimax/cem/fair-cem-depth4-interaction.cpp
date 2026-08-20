// Applies the fixed fair-CEM coefficients to the full-width depth-four search
// to measure their interaction without optimizing or modifying a coefficient.
#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#include "../reference/fair-only-depth4.cpp"
#undef DROP7_FAIR_ONLY_DEPTH4_LIBRARY

#include <sstream>

namespace drop7::fair_cem_depth4_interaction {

namespace d4 = drop7::fair_only_depth4;
namespace fair = drop7::fair_only_horizon;

constexpr int kCoefficientCount = 8;
constexpr int kDepth = 4;
constexpr int kChanceSamples = d4::kChanceSamples;
constexpr std::uint64_t kMaximumWork = d4::kMaximumWork;
constexpr std::size_t kMaximumCacheEntries = d4::kMaximumCacheEntries;
constexpr int kMaximumMoves = 1'000;
constexpr int kParallelism = 4;
constexpr int kTrainingGames = 16;
constexpr int kHeldoutGames = 16;
constexpr int kScreenGames = 8;
constexpr int kConfirmationGames = 16;
constexpr double kTailFraction = 0.25;
constexpr std::uint32_t kTrainingStart = 0x3da2'0000u;
constexpr std::uint32_t kHeldoutStart = 0x3da3'0000u;
constexpr std::uint32_t kScreenStart = 0x3eab'0000u;
constexpr std::uint32_t kConfirmationStart = 0x3eac'0000u;

using Vector = std::array<double, kCoefficientCount>;

// Exact normalized doubles stored by the fair-CEM optimizer's JSON artifact
// and checkpoint; this executable treats them as fixed inputs.
constexpr Vector kFrozenNormalized{{
    0.29781767106241563,
    0.095798932122743208,
    0.081211644067223879,
    0.28528175288153812,
    -0.18236227525958248,
    0.35842254554785136,
    0.12603672153429599,
    -0.051960417474261361,
}};
constexpr double kLogMultiplierBound = 0.6931471805599453;

static_assert(kLevelBonus == 7'000);
static_assert(kDepth == d4::kCandidateDepth && kChanceSamples == 5);
static_assert(kMaximumWork == 3'200'000);
static_assert(kMaximumCacheEntries == 60'000);
static_assert(kMaximumWork > d4::kWorstCaseD4Work);
static_assert(kMaximumCacheEntries > d4::kWorstCaseD4CacheEntries);
static_assert(kMaximumMoves >= 1'000);
static_assert(kTrainingStart + kTrainingGames < kHeldoutStart);
static_assert(kHeldoutStart + kHeldoutGames < kScreenStart);
static_assert(kScreenStart + kScreenGames < kConfirmationStart);
static_assert((kTrainingStart >> 24) != 0x7du &&
              (kTrainingStart >> 24) != 0xd7u);
static_assert((kHeldoutStart >> 24) != 0x7du &&
              (kHeldoutStart >> 24) != 0xd7u);
static_assert((kScreenStart >> 24) != 0x7du &&
              (kScreenStart >> 24) != 0xd7u);
static_assert((kConfirmationStart >> 24) != 0x7du &&
              (kConfirmationStart >> 24) != 0xd7u);

struct Coefficients {
  double direct_trigger_multiplier = 1.0;
  double latent_release_multiplier = 1.0;
  double cover_debt_multiplier = 1.0;
  double altitude_danger_rise_multiplier = 1.0;
  double low_number_clog_multiplier = 1.0;
  double next_disc_quiet_readiness_delta = 0.0;
  double revealed_cover_reward = 0.0;
  double additional_wave_reward = 0.0;
};

Coefficients decode(const Vector& vector) {
  Coefficients result;
  result.direct_trigger_multiplier =
      std::exp(vector[0] * kLogMultiplierBound);
  result.latent_release_multiplier =
      std::exp(vector[1] * kLogMultiplierBound);
  result.cover_debt_multiplier =
      std::exp(vector[2] * kLogMultiplierBound);
  result.altitude_danger_rise_multiplier =
      std::exp(vector[3] * kLogMultiplierBound);
  result.low_number_clog_multiplier =
      std::exp(vector[4] * kLogMultiplierBound);
  result.next_disc_quiet_readiness_delta = vector[5];
  result.revealed_cover_reward = 600.0 * vector[6];
  result.additional_wave_reward = 1'500.0 * vector[7];
  return result;
}

double parameterizedLeaf(const State& state,
                         const Coefficients& coefficients) {
  if (state.game_over) return fair::kFairTerminalUtility;
  const fair::FairFeatures features = fair::extractFairFeatures(state);
  const auto& f = features.heuristic;
  // Accumulation order matches fairLeaf. The all-zero normalized vector is
  // therefore exact stock fair D4, including floating-point tie behavior.
  double result = 0.0;
  result += fair::kOpenColumnsWeight * f.open_columns;
  result += coefficients.altitude_danger_rise_multiplier *
            fair::kHeightLoadWeight * f.height_load;
  result += coefficients.cover_debt_multiplier * fair::kSolidCellsWeight *
            f.solid_cells;
  result += coefficients.cover_debt_multiplier * fair::kCrackedCellsWeight *
            f.cracked_cells;
  result += fair::kNumberedCellsWeight * f.numbered_cells;
  result += coefficients.low_number_clog_multiplier *
            fair::kHighLowNumbersWeight * f.high_low_numbers;
  result += coefficients.direct_trigger_multiplier *
            fair::kDirectPotentialWeight * f.direct_potential;
  result += coefficients.latent_release_multiplier *
            fair::kLatentChainPotentialWeight * f.latent_chain_potential;
  result += coefficients.latent_release_multiplier *
            fair::kCrackedExposureWeight * f.cracked_exposure;
  result += coefficients.latent_release_multiplier *
            fair::kSolidExposureWeight * f.solid_exposure;
  result += coefficients.low_number_clog_multiplier *
            fair::kAdjacentOnesWeight * f.adjacent_ones;
  result += coefficients.low_number_clog_multiplier *
            fair::kTripleTwosWeight * f.triple_twos;
  result += coefficients.low_number_clog_multiplier *
            fair::kDeadLowNumbersWeight * f.dead_low_numbers;
  result += coefficients.altitude_danger_rise_multiplier *
            fair::kCoveredHeightRiskWeight * features.covered_height_risk;
  result += coefficients.altitude_danger_rise_multiplier *
            fair::kLowNumberHeightRiskWeight * features.low_number_height_risk;
  result += coefficients.altitude_danger_rise_multiplier *
            fair::kDangerHeightSquaredWeight *
            features.danger_height_squared;
  result += fair::kRoughnessWeight * features.roughness;
  result += coefficients.altitude_danger_rise_multiplier *
            fair::kRisePressureWeight * features.rise_pressure;
  result += fair::kNextDiscVerticalOptionsWeight *
            features.next_disc_vertical_options;
  const double extra_readiness =
      fair::kNextDiscVerticalOptionsWeight *
          features.next_disc_vertical_options +
      300.0 * f.quiet_build_options + 600.0 * f.quiet_direct_gain +
      600.0 * f.trigger_readiness + 1'200.0 * f.rise_trigger_readiness;
  result += coefficients.next_disc_quiet_readiness_delta * extra_readiness;
  return result;
}

double transitionBonus(const MoveResult& move,
                       const Coefficients& coefficients) {
  std::uint64_t revealed = 0;
  for (const Wave& wave : move.waves) {
    revealed += static_cast<std::uint64_t>(wave.revealed);
  }
  const int additional_waves =
      std::max(0, static_cast<int>(move.waves.size()) - 1);
  return coefficients.revealed_cover_reward * revealed +
         coefficients.additional_wave_reward * additional_waves;
}

class WorkLimitReached : public std::exception {};

struct CacheEntry {
  double value = 0.0;
  std::list<std::string>::iterator order;
};

struct SearchContext {
  explicit SearchContext(const Coefficients& run_coefficients)
      : coefficients(run_coefficients) {}

  const Coefficients& coefficients;
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

double bestFutureValue(const State& state, int depth,
                       SearchContext& context);

struct ActionValue {
  double value = 0.0;
  double expected_score = 0.0;
};

ActionValue evaluateAction(const State& state, int column, int depth,
                           SearchContext& context) {
  const std::uint32_t state_seed = cfpi::detail::scenarioSeedForState(
      state, fair::kPolicySeed, depth);
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
      result.value += fair::kTerminalUtility;
      continue;
    }
    const double score_delta = static_cast<double>(move.score_delta);
    result.expected_score += score_delta;
    const double transition =
        score_delta + transitionBonus(move, context.coefficients);
    if (move.state.game_over) {
      result.value += transition + fair::kTerminalUtility;
      continue;
    }
    move.state.score = 0;
    move.state.next_disc = cfpi::detail::sampledNextDisc(
        state_seed, sample, kChanceSamples);
    bool ignored = false;
    const State next = cfpi::detail::canonicalState(move.state, ignored);
    result.value += transition + bestFutureValue(next, depth - 1, context);
  }
  result.value /= kChanceSamples;
  result.expected_score /= kChanceSamples;
  return result;
}

double evaluateLeaf(const State& state, SearchContext& context) {
  checkBudget(context);
  ++context.work;
  const double value = parameterizedLeaf(state, context.coefficients);
  if (!std::isfinite(value)) {
    throw std::runtime_error("CEM D4 leaf returned non-finite value");
  }
  return value;
}

double bestFutureValue(const State& state, int depth,
                       SearchContext& context) {
  ++context.nodes;
  checkBudget(context);
  if (state.game_over) return fair::kTerminalUtility;
  if (depth == 0) return evaluateLeaf(state, context);
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
    best = std::max(best, evaluateAction(state, column, depth, context).value);
  }
  if (!std::isfinite(best)) best = fair::kTerminalUtility;
  cacheValue(context, key, best);
  return best;
}

struct RootEvaluation {
  int action = -1;
  double value = -std::numeric_limits<double>::infinity();
  std::array<double, kBoardSize> values{};
  std::array<double, kBoardSize> expected_scores{};
};

RootEvaluation rootDecision(const State& canonical, int depth,
                            SearchContext& context) {
  RootEvaluation result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  result.expected_scores.fill(-std::numeric_limits<double>::infinity());
  for (const int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(canonical.board, column)) continue;
    const ActionValue candidate = evaluateAction(canonical, column, depth,
                                                  context);
    result.values[column] = candidate.value;
    result.expected_scores[column] = candidate.expected_score;
    if (candidate.value > result.value) {
      result.value = candidate.value;
      result.action = column;
    }
  }
  return result;
}

struct SearchDecision {
  int action = -1;
  int depth3_action = -1;
  int completed_depth = 0;
  bool complete = false;
  bool switched_from_depth3 = false;
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
  std::uint64_t cache_hits = 0;
  std::size_t cache_entries = 0;
  std::array<double, kBoardSize> root_values{};
  std::array<double, kBoardSize> root_expected_scores{};
};

SearchDecision chooseAction(const State& source,
                            const Coefficients& coefficients) {
  if (source.game_over) return {};
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  SearchContext context(coefficients);
  RootEvaluation completed;
  int completed_depth = 0;
  int depth3_action = -1;
  for (int depth = 1; depth <= kDepth; ++depth) {
    try {
      completed = rootDecision(canonical, depth, context);
      if (completed.action < 0) break;
      completed_depth = depth;
      if (depth == d4::kBaselineDepth) depth3_action = completed.action;
    } catch (const WorkLimitReached&) {
      break;
    }
  }
  int action = completed.action;
  if (action < 0) action = centerFirstMove(canonical.board);
  if (depth3_action < 0) depth3_action = action;
  SearchDecision result;
  result.action = mirrored ? kBoardSize - 1 - action : action;
  result.depth3_action =
      mirrored ? kBoardSize - 1 - depth3_action : depth3_action;
  result.completed_depth = completed_depth;
  result.complete = completed_depth == kDepth;
  result.switched_from_depth3 = result.action != result.depth3_action;
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

d4::GameResult runCompositeGame(std::uint32_t seed, std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  const Coefficients coefficients = decode(kFrozenNormalized);
  State state = initialHeadlessState(seed);
  d4::GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    const SearchDecision decision = chooseAction(state, coefficients);
    if (!decision.complete || decision.completed_depth != kDepth) {
      throw std::runtime_error("frozen CEM D4 did not complete");
    }
    if (!isLegal(state.board, decision.action) ||
        !isLegal(state.board, decision.depth3_action)) {
      throw std::runtime_error("frozen CEM D4 decision was illegal");
    }
    result.depth_switches += decision.switched_from_depth3;
    ++result.action_counts[decision.action];
    result.work += decision.work;
    result.nodes += decision.nodes;
    result.cache_hits += decision.cache_hits;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, decision.cache_entries);
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("frozen CEM D4 transition failed");
    }
    d4::observeMove(move, result);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.peak_rss_bytes = d4::peakRssBytes();
  result.elapsed_seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
  d4::reportGame(label, result);
  return result;
}

struct Cohort {
  std::vector<d4::GameResult> stock;
  std::vector<d4::GameResult> composite;
  double wall_seconds = 0.0;
};

Cohort runCohort(std::uint32_t seed_start, int games,
                 std::string_view phase) {
  if (games < 1) throw std::invalid_argument("empty CEM D4 cohort");
  const auto started = std::chrono::steady_clock::now();
  Cohort result;
  result.stock.resize(static_cast<std::size_t>(games));
  result.composite.resize(static_cast<std::size_t>(games));
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= games) return;
        const std::uint32_t seed =
            seed_start + static_cast<std::uint32_t>(game);
        result.stock[static_cast<std::size_t>(game)] = d4::runDepth4Game(
            seed, std::string(phase) + "-stock-fair-d4");
        result.composite[static_cast<std::size_t>(game)] = runCompositeGame(
            seed, std::string(phase) + "-frozen-cem-d4");
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.wall_seconds = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started)
                            .count();
  return result;
}

double lowerTailMean(std::vector<double> values, double fraction) {
  if (values.empty() || fraction <= 0.0 || fraction > 1.0) {
    throw std::invalid_argument("invalid CEM D4 lower-tail request");
  }
  std::sort(values.begin(), values.end());
  const double mass = fraction * values.size();
  const int whole = static_cast<int>(std::floor(mass));
  const double fractional = mass - whole;
  double total = 0.0;
  for (int index = 0; index < whole; ++index) {
    total += values[static_cast<std::size_t>(index)];
  }
  if (fractional > 0.0) {
    total += fractional * values[static_cast<std::size_t>(whole)];
  }
  return total / mass;
}

d4::PairedSummary pairedSummary(const Cohort& cohort) {
  if (cohort.stock.size() != cohort.composite.size() ||
      cohort.stock.empty()) {
    throw std::invalid_argument("invalid CEM D4 paired cohort");
  }
  std::vector<double> scores;
  std::vector<double> moves;
  std::vector<double> cleared;
  std::vector<double> revealed;
  for (std::size_t game = 0; game < cohort.stock.size(); ++game) {
    scores.push_back(static_cast<double>(cohort.composite[game].score) -
                     static_cast<double>(cohort.stock[game].score));
    moves.push_back(static_cast<double>(cohort.composite[game].moves) -
                    static_cast<double>(cohort.stock[game].moves));
    cleared.push_back(
        static_cast<double>(cohort.composite[game].numbered_cleared) -
        static_cast<double>(cohort.stock[game].numbered_cleared));
    revealed.push_back(
        static_cast<double>(cohort.composite[game].covers_revealed) -
        static_cast<double>(cohort.stock[game].covers_revealed));
  }
  return {d4::differences(scores), d4::differences(moves),
          d4::differences(cleared), d4::differences(revealed)};
}

struct Analysis {
  d4::Summary stock;
  d4::Summary composite;
  d4::PairedSummary paired;
  double stock_tail_score = 0.0;
  double composite_tail_score = 0.0;
  double stock_tail_moves = 0.0;
  double composite_tail_moves = 0.0;
};

Analysis analyze(const Cohort& cohort) {
  Analysis result;
  result.stock = d4::summarize(cohort.stock, kDepth);
  result.composite = d4::summarize(cohort.composite, kDepth);
  result.paired = pairedSummary(cohort);
  std::vector<double> stock_scores;
  std::vector<double> composite_scores;
  std::vector<double> stock_moves;
  std::vector<double> composite_moves;
  for (std::size_t game = 0; game < cohort.stock.size(); ++game) {
    stock_scores.push_back(static_cast<double>(cohort.stock[game].score));
    composite_scores.push_back(
        static_cast<double>(cohort.composite[game].score));
    stock_moves.push_back(static_cast<double>(cohort.stock[game].moves));
    composite_moves.push_back(
        static_cast<double>(cohort.composite[game].moves));
  }
  result.stock_tail_score = lowerTailMean(stock_scores, kTailFraction);
  result.composite_tail_score =
      lowerTailMean(composite_scores, kTailFraction);
  result.stock_tail_moves = lowerTailMean(stock_moves, kTailFraction);
  result.composite_tail_moves =
      lowerTailMean(composite_moves, kTailFraction);
  return result;
}

bool passesGate(const Analysis& analysis) {
  return analysis.composite.mean_score > analysis.stock.mean_score &&
         analysis.composite.mean_moves > analysis.stock.mean_moves &&
         analysis.composite_tail_score >= analysis.stock_tail_score &&
         analysis.composite_tail_moves >= analysis.stock_tail_moves;
}

void writeVector(std::ostream& output, const Vector& vector) {
  output << '[';
  for (int index = 0; index < kCoefficientCount; ++index) {
    if (index != 0) output << ',';
    output << vector[static_cast<std::size_t>(index)];
  }
  output << ']';
}

void writeCoefficients(std::ostream& output,
                       const Coefficients& coefficients) {
  output << "{\"directTriggerMultiplier\":"
         << coefficients.direct_trigger_multiplier
         << ",\"latentReleaseMultiplier\":"
         << coefficients.latent_release_multiplier
         << ",\"coverDebtMultiplier\":"
         << coefficients.cover_debt_multiplier
         << ",\"altitudeDangerRiseMultiplier\":"
         << coefficients.altitude_danger_rise_multiplier
         << ",\"lowNumberClogMultiplier\":"
         << coefficients.low_number_clog_multiplier
         << ",\"nextDiscQuietReadinessDelta\":"
         << coefficients.next_disc_quiet_readiness_delta
         << ",\"revealedCoverReward\":"
         << coefficients.revealed_cover_reward
         << ",\"additionalWaveReward\":"
         << coefficients.additional_wave_reward << '}';
}

void writeCohort(std::ostream& output, std::uint32_t seed_start,
                 const Cohort& cohort, const Analysis& analysis,
                 bool passed) {
  output << "{\"seedStart\":" << seed_start
         << ",\"games\":" << cohort.stock.size()
         << ",\"maximumMoves\":" << kMaximumMoves
         << ",\"stockFairD4\":";
  d4::writeSummary(output, analysis.stock);
  output << ",\"frozenCemD4\":";
  d4::writeSummary(output, analysis.composite);
  output << ",\"paired\":";
  d4::writePaired(output, analysis.paired);
  output << ",\"lowerTail25\":{\"stockScore\":"
         << analysis.stock_tail_score << ",\"compositeScore\":"
         << analysis.composite_tail_score << ",\"stockMoves\":"
         << analysis.stock_tail_moves << ",\"compositeMoves\":"
         << analysis.composite_tail_moves << "},\"wallSeconds\":"
         << cohort.wall_seconds << ",\"passed\":"
         << (passed ? "true" : "false") << ",\"pairs\":[";
  for (std::size_t game = 0; game < cohort.stock.size(); ++game) {
    if (game != 0) output << ',';
    output << "{\"seed\":" << cohort.stock[game].seed
           << ",\"stockFairD4\":";
    d4::writeGame(output, cohort.stock[game]);
    output << ",\"frozenCemD4\":";
    d4::writeGame(output, cohort.composite[game]);
    output << '}';
  }
  output << "]}";
}

struct Options {
  std::string output = "/tmp/drop7-fair-cem-depth4-interaction.json";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing CEM D4 option value");
    }
    const std::string_view option(argv[index]);
    if (option == "--output") {
      result.output = argv[index + 1];
    } else {
      throw std::invalid_argument("unknown CEM D4 option");
    }
  }
  return result;
}

void writeArtifact(const Options& options, const Cohort& training,
                   const Analysis& training_analysis, bool training_passed,
                   const Cohort& heldout, const Analysis& heldout_analysis,
                   bool heldout_passed, const Cohort* screen,
                   const Analysis* screen_analysis, bool screen_passed,
                   const Cohort* confirmation,
                   const Analysis* confirmation_analysis,
                   bool confirmation_passed, double total_wall_seconds) {
  std::ofstream output(options.output, std::ios::trunc);
  if (!output) throw std::runtime_error("could not open CEM D4 artifact");
  output << std::setprecision(17)
         << "{\n  \"experiment\":\"frozen-fair-cem-d4-interaction\",\n"
         << "  \"preregistered\":true,\n"
         << "  \"rescueRetune\":false,\n"
         << "  \"d3ScreenFailureRemainsPrimary\":true,\n"
         << "  \"publicStateOnly\":true,\n"
         << "  \"historicalActionPlacementPriors\":false,\n"
         << "  \"sourceD3Result\":{\"artifactSha256\":"
            "\"524705e1bbe3ed409788864f94d70c5de998a94fa260b2a7ecddf677977d9a2a\","
            "\"checkpointSha256\":"
            "\"ec1356a8536077b412498c580aa5f38c261e14fa2b468fc3e9dfccc298c6fccf\","
            "\"screenScoreDelta\":-905.375,\"screenMoveDelta\":1.875,"
            "\"screenPassed\":false},\n"
         << "  \"frozenChampion\":{\"normalized\":";
  writeVector(output, kFrozenNormalized);
  output << ",\"decoded\":";
  writeCoefficients(output, decode(kFrozenNormalized));
  output << "},\n  \"scoring\":{\"levelBonus\":7000},\n"
         << "  \"search\":{\"depth\":" << kDepth
         << ",\"chanceSamples\":" << kChanceSamples
         << ",\"fullWidth\":true,\"iterativeDeepening\":true,"
         << "\"policySeed\":" << fair::kPolicySeed
         << ",\"maximumWork\":" << kMaximumWork
         << ",\"worstCaseWork\":" << d4::kWorstCaseD4Work
         << ",\"maximumCacheEntries\":" << kMaximumCacheEntries
         << ",\"worstCaseCacheEntries\":"
         << d4::kWorstCaseD4CacheEntries
         << ",\"maximumMoves\":" << kMaximumMoves
         << ",\"parallelism\":" << kParallelism << "},\n"
         << "  \"gateCriteria\":{\"trainingBothMeansImprove\":true,"
            "\"trainingLowerTail25DoesNotRegress\":true,"
            "\"heldoutBothMeansImprove\":true,"
            "\"heldoutLowerTail25DoesNotRegress\":true},\n"
         << "  \"training\":";
  writeCohort(output, kTrainingStart, training, training_analysis,
              training_passed);
  output << ",\n  \"heldout\":";
  writeCohort(output, kHeldoutStart, heldout, heldout_analysis,
              heldout_passed);
  output << ",\n  \"screen\":";
  if (screen == nullptr) {
    output << "null";
  } else {
    writeCohort(output, kScreenStart, *screen, *screen_analysis,
                screen_passed);
  }
  output << ",\n  \"confirmation\":";
  if (confirmation == nullptr) {
    output << "null";
  } else {
    writeCohort(output, kConfirmationStart, *confirmation,
                *confirmation_analysis, confirmation_passed);
  }
  const bool gate_passed = training_passed && heldout_passed;
  output << ",\n  \"trainingPassed\":"
         << (training_passed ? "true" : "false")
         << ",\n  \"heldoutPassed\":"
         << (heldout_passed ? "true" : "false")
         << ",\n  \"gatePassed\":" << (gate_passed ? "true" : "false")
         << ",\n  \"screenRan\":" << (screen != nullptr ? "true" : "false")
         << ",\n  \"screenPassed\":" << (screen_passed ? "true" : "false")
         << ",\n  \"confirmationRan\":"
         << (confirmation != nullptr ? "true" : "false")
         << ",\n  \"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\n  \"qualified\":"
         << (gate_passed && screen_passed && confirmation_passed ? "true"
                                                                  : "false")
         << ",\n  \"totalWallSeconds\":" << total_wall_seconds
         << ",\n  \"peakRssBytes\":" << d4::peakRssBytes() << "\n}\n";
  if (!output) throw std::runtime_error("could not write CEM D4 artifact");
}

bool nearlyEqual(double first, double second, double tolerance = 1.0e-9) {
  if (std::isinf(first) || std::isinf(second)) return first == second;
  return std::abs(first - second) <= tolerance;
}

bool selfTest(std::ostream& output) {
  std::ostringstream inherited_output;
  const bool inherited = d4::selfTest(inherited_output);
  const State state = fair::fixtureState(fair::kTypeScriptFixtures[1]);
  const Coefficients zero = decode(Vector{});
  const d4::SearchDecision stock = d4::chooseDepth4Action(state);
  const SearchDecision parity = chooseAction(state, zero);
  bool root_parity = parity.action == stock.action &&
                     parity.depth3_action == stock.depth3_action &&
                     parity.completed_depth == stock.completed_depth &&
                     parity.complete == stock.complete &&
                     parity.work == stock.work && parity.nodes == stock.nodes &&
                     parity.cache_hits == stock.cache_hits &&
                     parity.cache_entries == stock.cache_entries;
  for (int column = 0; column < kBoardSize; ++column) {
    root_parity =
        root_parity &&
        nearlyEqual(parity.root_values[column], stock.root_values[column],
                    1.0e-10) &&
        nearlyEqual(parity.root_expected_scores[column],
                    stock.root_expected_scores[column], 1.0e-10);
  }
  const bool leaf_parity = parameterizedLeaf(state, zero) == fair::fairLeaf(state);

  const Coefficients frozen = decode(kFrozenNormalized);
  const SearchDecision first = chooseAction(state, frozen);
  const SearchDecision repeat = chooseAction(state, frozen);
  State reflected = state;
  reflected.board = cfpi::detail::mirrorBoard(state.board);
  const SearchDecision mirrored = chooseAction(reflected, frozen);
  State metadata = state;
  metadata.score = 9'999'999;
  metadata.level = 88;
  metadata.moves_played = 654;
  const SearchDecision metadata_decision = chooseAction(metadata, frozen);
  const bool deterministic =
      repeat.action == first.action &&
      repeat.depth3_action == first.depth3_action &&
      repeat.work == first.work && repeat.nodes == first.nodes &&
      repeat.cache_hits == first.cache_hits &&
      repeat.cache_entries == first.cache_entries;
  const bool reflection_safe =
      mirrored.action == kBoardSize - 1 - first.action &&
      mirrored.depth3_action == kBoardSize - 1 - first.depth3_action &&
      mirrored.work == first.work &&
      parameterizedLeaf(state, frozen) ==
          parameterizedLeaf(reflected, frozen);
  const bool public_only = metadata_decision.action == first.action &&
                           metadata_decision.depth3_action ==
                               first.depth3_action &&
                           metadata_decision.work == first.work &&
                           parameterizedLeaf(state, frozen) ==
                               parameterizedLeaf(metadata, frozen);
  const bool legal = isLegal(state.board, first.action) &&
                     isLegal(state.board, first.depth3_action);
  const bool bounded = first.work <= kMaximumWork &&
                       first.cache_entries <= kMaximumCacheEntries;
  const bool completion_proven =
      kMaximumWork > d4::kWorstCaseD4Work &&
      kMaximumCacheEntries > d4::kWorstCaseD4CacheEntries;
  MoveResult synthetic;
  synthetic.waves = {{1, 2, 3, 0}, {2, 1, 5, 0}, {3, 4, 7, 0}};
  Coefficients transition;
  transition.revealed_cover_reward = 11.0;
  transition.additional_wave_reward = 101.0;
  const bool transition_exact =
      transitionBonus(synthetic, transition) ==
      15.0 * 11.0 + 2.0 * 101.0;
  const bool frozen_exact =
      nearlyEqual(frozen.direct_trigger_multiplier, 1.229283499618433) &&
      nearlyEqual(frozen.latent_release_multiplier, 1.0686570424928017) &&
      nearlyEqual(frozen.cover_debt_multiplier, 1.0579061475839819) &&
      nearlyEqual(frozen.altitude_danger_rise_multiplier,
                  1.218648237825525) &&
      nearlyEqual(frozen.low_number_clog_multiplier, 0.88125883714791231) &&
      frozen.next_disc_quiet_readiness_delta ==
          0.35842254554785136 &&
      nearlyEqual(frozen.revealed_cover_reward, 75.622032920577595) &&
      nearlyEqual(frozen.additional_wave_reward, -77.940626211392043);
  const bool tail_exact = nearlyEqual(
      lowerTailMean({1.0, 2.0, 10.0, 20.0, 30.0, 40.0}, 0.25),
      4.0 / 3.0);
  const bool protocol =
      kLevelBonus == 7'000 && kDepth == 4 && kChanceSamples == 5 &&
      kMaximumMoves >= 1'000 && kTrainingGames == 16 &&
      kHeldoutGames == 16 && kScreenGames == 8 &&
      kConfirmationGames == 16 && kTrainingStart == 0x3da2'0000u &&
      kHeldoutStart == 0x3da3'0000u && kScreenStart == 0x3eab'0000u &&
      kConfirmationStart == 0x3eac'0000u;
  const bool passed = inherited && leaf_parity && root_parity &&
                      deterministic && reflection_safe && public_only &&
                      legal && bounded && completion_proven &&
                      transition_exact && frozen_exact && tail_exact &&
                      protocol;
  output << std::boolalpha << std::setprecision(12)
         << "FAIR_CEM_DEPTH4_INTERACTION_SELF_TEST {\"passed\":" << passed
         << ",\"inheritedD4Test\":" << inherited
         << ",\"zeroLeafParity\":" << leaf_parity
         << ",\"zeroRootParity\":" << root_parity
         << ",\"deterministic\":" << deterministic
         << ",\"reflectionSafe\":" << reflection_safe
         << ",\"publicStateOnly\":" << public_only
         << ",\"legal\":" << legal << ",\"bounded\":" << bounded
         << ",\"completionProven\":" << completion_proven
         << ",\"transitionRewardsExact\":" << transition_exact
         << ",\"frozenCoefficientsExact\":" << frozen_exact
         << ",\"fractionalTailExact\":" << tail_exact
         << ",\"fixedProtocol\":" << protocol
         << ",\"work\":" << first.work
         << ",\"cacheEntries\":" << first.cache_entries
         << ",\"worstCaseWork\":" << d4::kWorstCaseD4Work
         << ",\"worstCaseCache\":" << d4::kWorstCaseD4CacheEntries
         << ",\"levelBonus\":" << kLevelBonus << "}\n";
  return passed;
}

int run(const Options& options, std::ostream& output) {
  const auto started = std::chrono::steady_clock::now();
  const Cohort training =
      runCohort(kTrainingStart, kTrainingGames, "training");
  const Analysis training_analysis = analyze(training);
  const bool training_passed = passesGate(training_analysis);

  // Run the entire heldout regardless of fitting metrics.  No choice or
  // retuning occurs between the two fixed cohorts.
  const Cohort heldout =
      runCohort(kHeldoutStart, kHeldoutGames, "heldout");
  const Analysis heldout_analysis = analyze(heldout);
  const bool heldout_passed = passesGate(heldout_analysis);
  const bool gate_passed = training_passed && heldout_passed;

  Cohort screen;
  Analysis screen_analysis;
  bool screen_passed = false;
  if (gate_passed) {
    screen = runCohort(kScreenStart, kScreenGames, "screen");
    screen_analysis = analyze(screen);
    screen_passed = passesGate(screen_analysis);
  }

  Cohort confirmation;
  Analysis confirmation_analysis;
  bool confirmation_passed = false;
  if (screen_passed) {
    confirmation = runCohort(kConfirmationStart, kConfirmationGames,
                             "confirmation");
    confirmation_analysis = analyze(confirmation);
    confirmation_passed = passesGate(confirmation_analysis);
  }

  const double total_wall_seconds = std::chrono::duration<double>(
                                          std::chrono::steady_clock::now() -
                                          started)
                                          .count();
  writeArtifact(options, training, training_analysis, training_passed,
                heldout, heldout_analysis, heldout_passed,
                gate_passed ? &screen : nullptr,
                gate_passed ? &screen_analysis : nullptr, screen_passed,
                screen_passed ? &confirmation : nullptr,
                screen_passed ? &confirmation_analysis : nullptr,
                confirmation_passed, total_wall_seconds);
  output << std::fixed << std::setprecision(3)
         << "FAIR_CEM_DEPTH4_INTERACTION_RESULT {\"trainingStockScore\":"
         << training_analysis.stock.mean_score
         << ",\"trainingCompositeScore\":"
         << training_analysis.composite.mean_score
         << ",\"trainingStockMoves\":"
         << training_analysis.stock.mean_moves
         << ",\"trainingCompositeMoves\":"
         << training_analysis.composite.mean_moves
         << ",\"trainingPassed\":"
         << (training_passed ? "true" : "false")
         << ",\"heldoutStockScore\":"
         << heldout_analysis.stock.mean_score
         << ",\"heldoutCompositeScore\":"
         << heldout_analysis.composite.mean_score
         << ",\"heldoutStockMoves\":"
         << heldout_analysis.stock.mean_moves
         << ",\"heldoutCompositeMoves\":"
         << heldout_analysis.composite.mean_moves
         << ",\"heldoutPassed\":"
         << (heldout_passed ? "true" : "false")
         << ",\"gatePassed\":" << (gate_passed ? "true" : "false")
         << ",\"screenRan\":" << (gate_passed ? "true" : "false")
         << ",\"screenPassed\":" << (screen_passed ? "true" : "false")
         << ",\"confirmationRan\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\"peakRssBytes\":" << d4::peakRssBytes()
         << ",\"totalWallSeconds\":" << total_wall_seconds
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return 0;
}

}  // namespace drop7::fair_cem_depth4_interaction

int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::fair_cem_depth4_interaction::selfTest(std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::fair_cem_depth4_interaction::parseOptions(argc, argv, 2);
      return drop7::fair_cem_depth4_interaction::run(options, std::cout);
    }
    std::cerr << "usage: drop7_fair_cem_depth4_interaction --self-test | "
                 "--run [--output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
