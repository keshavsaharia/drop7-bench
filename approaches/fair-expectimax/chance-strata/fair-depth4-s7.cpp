// Compares five and seven stratified chance samples for the reference
// full-width fair-D4 policy. The sample count and the
// completion-independent resource caps are the only candidate differences.
#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#include "../reference/fair-only-depth4.cpp"
#undef DROP7_FAIR_ONLY_DEPTH4_LIBRARY

#include <sstream>

namespace drop7::fair_depth4_s7 {

namespace d4 = drop7::fair_only_depth4;
namespace fair = drop7::fair_only_horizon;

constexpr int kDepth = 4;
constexpr int kBaselineSamples = 5;
constexpr int kCandidateSamples = 7;
constexpr std::uint64_t kMaximumWork = 12'000'000;
constexpr std::size_t kMaximumCacheEntries = 24'000;
constexpr std::uint64_t kMaximumRssBytes = 64ull * 1024ull * 1024ull;
constexpr int kMaximumMoves = 1'000;
constexpr int kParallelism = 4;
constexpr int kFittingGames = 8;
constexpr int kHeldoutGames = 16;
constexpr int kScreenGames = 8;
constexpr int kConfirmationGames = 16;
constexpr double kTailFraction = 0.25;
constexpr std::uint32_t kFittingStart = 0x3de1'0000u;
constexpr std::uint32_t kHeldoutStart = 0x3de2'0000u;
constexpr std::uint32_t kScreenStart = 0x3ead'0000u;
constexpr std::uint32_t kConfirmationStart = 0x3eae'0000u;

constexpr std::uint64_t power(std::uint64_t base, int exponent) {
  std::uint64_t result = 1;
  for (int count = 0; count < exponent; ++count) result *= base;
  return result;
}

constexpr std::uint64_t worstCaseIterativeWork(int samples,
                                                int maximum_depth) {
  const std::uint64_t branches =
      static_cast<std::uint64_t>(kBoardSize * samples);
  std::uint64_t result = 0;
  for (int depth = 1; depth <= maximum_depth; ++depth) {
    for (int level = 1; level <= depth; ++level) {
      result += power(branches, level);
    }
    result += power(branches, depth);
  }
  return result;
}

constexpr std::uint64_t worstCaseIterativeCacheEntries(
    int samples, int maximum_depth) {
  const std::uint64_t branches =
      static_cast<std::uint64_t>(kBoardSize * samples);
  std::uint64_t result = 0;
  for (int depth = 2; depth <= maximum_depth; ++depth) {
    for (int level = 1; level < depth; ++level) {
      result += power(branches, level);
    }
  }
  return result;
}

constexpr std::uint64_t kWorstCaseS7Work =
    worstCaseIterativeWork(kCandidateSamples, kDepth);
constexpr std::uint64_t kWorstCaseS7CacheEntries =
    worstCaseIterativeCacheEntries(kCandidateSamples, kDepth);

static_assert(kLevelBonus == 7'000);
static_assert(kDepth == d4::kCandidateDepth);
static_assert(kBaselineSamples == d4::kChanceSamples);
static_assert(kWorstCaseS7Work == 11'892'398);
static_assert(kWorstCaseS7CacheEntries == 122'598);
static_assert(kMaximumWork > kWorstCaseS7Work);
static_assert(kMaximumCacheEntries < kWorstCaseS7CacheEntries);
static_assert(kMaximumRssBytes == 67'108'864);
static_assert(kMaximumMoves >= 1'000);
static_assert(fair::kPolicySeed == 0xd707'5eedu);
static_assert(fair::kTerminalUtility == -1'000'000.0);
static_assert(kFittingStart + kFittingGames < kHeldoutStart);
static_assert(kHeldoutStart + kHeldoutGames < kScreenStart);
static_assert(kScreenStart + kScreenGames < kConfirmationStart);
static_assert((kFittingStart >> 24) != 0x7du &&
              (kFittingStart >> 24) != 0xd7u);
static_assert((kHeldoutStart >> 24) != 0x7du &&
              (kHeldoutStart >> 24) != 0xd7u);
static_assert((kScreenStart >> 24) != 0x7du &&
              (kScreenStart >> 24) != 0xd7u);
static_assert((kConfirmationStart >> 24) != 0x7du &&
              (kConfirmationStart >> 24) != 0xd7u);

class WorkLimitReached : public std::exception {};

struct CacheEntry {
  double value = 0.0;
  std::list<std::string>::iterator order;
};

template <int Samples>
struct SearchContext {
  std::unordered_map<std::string, CacheEntry> cache;
  std::list<std::string> order;
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
  std::uint64_t cache_hits = 0;
};

template <int Samples>
constexpr std::uint64_t maximumWorkFor() {
  if constexpr (Samples == kBaselineSamples) return d4::kMaximumWork;
  return kMaximumWork;
}

template <int Samples>
constexpr std::size_t maximumCacheEntriesFor() {
  if constexpr (Samples == kBaselineSamples) {
    return d4::kMaximumCacheEntries;
  }
  return kMaximumCacheEntries;
}

template <int Samples>
void checkBudget(const SearchContext<Samples>& context) {
  if (context.work >= maximumWorkFor<Samples>()) throw WorkLimitReached{};
}

template <int Samples>
void cacheValue(SearchContext<Samples>& context, std::string key,
                double value) {
  const auto prior = context.cache.find(key);
  if (prior != context.cache.end()) {
    context.order.erase(prior->second.order);
    context.cache.erase(prior);
  }
  while (context.cache.size() >= maximumCacheEntriesFor<Samples>()) {
    const std::string& oldest = context.order.front();
    context.cache.erase(oldest);
    context.order.pop_front();
  }
  context.order.push_back(key);
  const auto order = std::prev(context.order.end());
  context.cache.emplace(std::move(key), CacheEntry{value, order});
}

template <int Samples>
double bestFutureValue(const State& state, int depth,
                       SearchContext<Samples>& context);

struct ActionValue {
  double value = 0.0;
  double expected_score = 0.0;
};

template <int Samples>
ActionValue evaluateAction(const State& state, int column, int depth,
                           SearchContext<Samples>& context) {
  const std::uint32_t state_seed = cfpi::detail::scenarioSeedForState(
      state, fair::kPolicySeed, depth);
  ActionValue result;
  for (int sample = 0; sample < Samples; ++sample) {
    checkBudget(context);
    cfpi::detail::StratifiedRandom random{state_seed, sample, Samples, 0};
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
    if (move.state.game_over) {
      result.value += score_delta + fair::kTerminalUtility;
      continue;
    }
    move.state.score = 0;
    move.state.next_disc =
        cfpi::detail::sampledNextDisc(state_seed, sample, Samples);
    bool ignored = false;
    const State next = cfpi::detail::canonicalState(move.state, ignored);
    result.value += score_delta + bestFutureValue(next, depth - 1, context);
  }
  result.value /= Samples;
  result.expected_score /= Samples;
  return result;
}

template <int Samples>
double evaluateLeaf(const State& state, SearchContext<Samples>& context) {
  checkBudget(context);
  ++context.work;
  const double value = fair::fairLeaf(state);
  if (!std::isfinite(value)) {
    throw std::runtime_error("fair D4 stratified leaf was non-finite");
  }
  return value;
}

template <int Samples>
double bestFutureValue(const State& state, int depth,
                       SearchContext<Samples>& context) {
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

template <int Samples>
RootEvaluation rootDecision(const State& canonical, int depth,
                            SearchContext<Samples>& context) {
  RootEvaluation result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  result.expected_scores.fill(-std::numeric_limits<double>::infinity());
  for (const int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(canonical.board, column)) continue;
    const ActionValue candidate =
        evaluateAction(canonical, column, depth, context);
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

template <int Samples>
SearchDecision chooseAction(const State& source) {
  if (source.game_over) return {};
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  SearchContext<Samples> context;
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

d4::GameResult runS7Game(std::uint32_t seed, std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  d4::GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    const SearchDecision decision = chooseAction<kCandidateSamples>(state);
    if (!decision.complete || decision.completed_depth != kDepth) {
      throw std::runtime_error("fair D4/s7 did not complete");
    }
    if (!isLegal(state.board, decision.action) ||
        !isLegal(state.board, decision.depth3_action)) {
      throw std::runtime_error("fair D4/s7 decision was illegal");
    }
    if (d4::peakRssBytes() > kMaximumRssBytes) {
      throw std::runtime_error("fair D4/s7 exceeded RSS target");
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
      throw std::runtime_error("fair D4/s7 transition failed");
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
  std::vector<d4::GameResult> s5;
  std::vector<d4::GameResult> s7;
  double wall_seconds = 0.0;
};

Cohort runCohort(std::uint32_t seed_start, int games,
                 std::string_view phase) {
  if (games < 1) throw std::invalid_argument("empty fair D4/s7 cohort");
  const auto started = std::chrono::steady_clock::now();
  Cohort result;
  result.s5.resize(static_cast<std::size_t>(games));
  result.s7.resize(static_cast<std::size_t>(games));
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= games) return;
        const std::uint32_t seed =
            seed_start + static_cast<std::uint32_t>(game);
        result.s5[static_cast<std::size_t>(game)] = d4::runDepth4Game(
            seed, std::string(phase) + "-fair-d4-s5");
        result.s7[static_cast<std::size_t>(game)] = runS7Game(
            seed, std::string(phase) + "-fair-d4-s7");
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
    throw std::invalid_argument("invalid fair D4/s7 lower-tail request");
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
  if (cohort.s5.size() != cohort.s7.size() || cohort.s5.empty()) {
    throw std::invalid_argument("invalid fair D4/s7 paired cohort");
  }
  std::vector<double> scores;
  std::vector<double> moves;
  std::vector<double> cleared;
  std::vector<double> revealed;
  for (std::size_t game = 0; game < cohort.s5.size(); ++game) {
    scores.push_back(static_cast<double>(cohort.s7[game].score) -
                     static_cast<double>(cohort.s5[game].score));
    moves.push_back(static_cast<double>(cohort.s7[game].moves) -
                    static_cast<double>(cohort.s5[game].moves));
    cleared.push_back(
        static_cast<double>(cohort.s7[game].numbered_cleared) -
        static_cast<double>(cohort.s5[game].numbered_cleared));
    revealed.push_back(
        static_cast<double>(cohort.s7[game].covers_revealed) -
        static_cast<double>(cohort.s5[game].covers_revealed));
  }
  return {d4::differences(scores), d4::differences(moves),
          d4::differences(cleared), d4::differences(revealed)};
}

struct Analysis {
  d4::Summary s5;
  d4::Summary s7;
  d4::PairedSummary paired;
  double s5_tail_score = 0.0;
  double s7_tail_score = 0.0;
  double s5_tail_moves = 0.0;
  double s7_tail_moves = 0.0;
};

Analysis analyze(const Cohort& cohort) {
  Analysis result;
  result.s5 = d4::summarize(cohort.s5, kDepth);
  result.s7 = d4::summarize(cohort.s7, kDepth);
  result.paired = pairedSummary(cohort);
  std::vector<double> s5_scores;
  std::vector<double> s7_scores;
  std::vector<double> s5_moves;
  std::vector<double> s7_moves;
  for (std::size_t game = 0; game < cohort.s5.size(); ++game) {
    s5_scores.push_back(static_cast<double>(cohort.s5[game].score));
    s7_scores.push_back(static_cast<double>(cohort.s7[game].score));
    s5_moves.push_back(static_cast<double>(cohort.s5[game].moves));
    s7_moves.push_back(static_cast<double>(cohort.s7[game].moves));
  }
  result.s5_tail_score = lowerTailMean(s5_scores, kTailFraction);
  result.s7_tail_score = lowerTailMean(s7_scores, kTailFraction);
  result.s5_tail_moves = lowerTailMean(s5_moves, kTailFraction);
  result.s7_tail_moves = lowerTailMean(s7_moves, kTailFraction);
  return result;
}

bool passesGate(const Analysis& analysis) {
  return analysis.s7.mean_score > analysis.s5.mean_score &&
         analysis.s7.mean_moves > analysis.s5.mean_moves &&
         analysis.s7_tail_score >= analysis.s5_tail_score &&
         analysis.s7_tail_moves >= analysis.s5_tail_moves;
}

void writeCohort(std::ostream& output, std::uint32_t seed_start,
                 const Cohort& cohort, const Analysis& analysis,
                 bool passed) {
  output << "{\"seedStart\":" << seed_start
         << ",\"games\":" << cohort.s5.size()
         << ",\"maximumMoves\":" << kMaximumMoves << ",\"fairD4S5\":";
  d4::writeSummary(output, analysis.s5);
  output << ",\"fairD4S7\":";
  d4::writeSummary(output, analysis.s7);
  output << ",\"paired\":";
  d4::writePaired(output, analysis.paired);
  output << ",\"lowerTail25\":{\"s5Score\":"
         << analysis.s5_tail_score << ",\"s7Score\":"
         << analysis.s7_tail_score << ",\"s5Moves\":"
         << analysis.s5_tail_moves << ",\"s7Moves\":"
         << analysis.s7_tail_moves << "},\"wallSeconds\":"
         << cohort.wall_seconds << ",\"passed\":"
         << (passed ? "true" : "false") << ",\"pairs\":[";
  for (std::size_t game = 0; game < cohort.s5.size(); ++game) {
    if (game != 0) output << ',';
    output << "{\"seed\":" << cohort.s5[game].seed << ",\"fairD4S5\":";
    d4::writeGame(output, cohort.s5[game]);
    output << ",\"fairD4S7\":";
    d4::writeGame(output, cohort.s7[game]);
    output << '}';
  }
  output << "]}";
}

struct Options {
  std::string output = "/tmp/drop7-fair-depth4-s7.json";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing fair D4/s7 option value");
    }
    const std::string_view option(argv[index]);
    if (option == "--output") {
      result.output = argv[index + 1];
    } else {
      throw std::invalid_argument("unknown fair D4/s7 option");
    }
  }
  return result;
}

void writeArtifact(const Options& options, const Cohort& fitting,
                   const Analysis& fitting_analysis, bool fitting_passed,
                   const Cohort& heldout, const Analysis& heldout_analysis,
                   bool heldout_passed, const Cohort* screen,
                   const Analysis* screen_analysis, bool screen_passed,
                   const Cohort* confirmation,
                   const Analysis* confirmation_analysis,
                   bool confirmation_passed, double total_wall_seconds) {
  std::ofstream output(options.output, std::ios::trunc);
  if (!output) throw std::runtime_error("could not open fair D4/s7 artifact");
  output << std::setprecision(17)
         << "{\n  \"experiment\":\"fair-full-width-d4-s7\",\n"
         << "  \"preregistered\":true,\n"
         << "  \"publicStateOnly\":true,\n"
         << "  \"onlySampleCountDiffers\":true,\n"
         << "  \"scoring\":{\"levelBonus\":7000},\n"
         << "  \"frozenSemantics\":{\"evaluator\":\"confirmed-fair-only\","
         << "\"terminalUtility\":" << fair::kTerminalUtility
         << ",\"fairTerminalUtility\":" << fair::kFairTerminalUtility
         << ",\"actionOrder\":[3,2,4,1,5,0,6],\"policySeed\":"
         << fair::kPolicySeed << "},\n"
         << "  \"search\":{\"depth\":" << kDepth
         << ",\"baselineSamples\":" << kBaselineSamples
         << ",\"candidateSamples\":" << kCandidateSamples
         << ",\"fullWidth\":true,\"iterativeDeepening\":true,"
         << "\"maximumWork\":" << kMaximumWork
         << ",\"worstCaseCandidateWork\":" << kWorstCaseS7Work
         << ",\"maximumCacheEntries\":" << kMaximumCacheEntries
         << ",\"worstCaseCandidateCacheEntries\":"
         << kWorstCaseS7CacheEntries
         << ",\"completionIndependentOfCache\":true"
         << ",\"maximumRssBytes\":" << kMaximumRssBytes
         << ",\"maximumMoves\":" << kMaximumMoves
         << ",\"parallelism\":" << kParallelism << "},\n"
         << "  \"motivation\":\"seven samples cover all seven next-visible-disc values at every chance node\",\n"
         << "  \"gateCriteria\":{\"fittingBothMeansImprove\":true,"
            "\"fittingLowerTail25DoesNotRegress\":true,"
            "\"heldoutBothMeansImprove\":true,"
            "\"heldoutLowerTail25DoesNotRegress\":true},\n"
         << "  \"fitting\":";
  writeCohort(output, kFittingStart, fitting, fitting_analysis,
              fitting_passed);
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
  const bool gate_passed = fitting_passed && heldout_passed;
  output << ",\n  \"fittingPassed\":"
         << (fitting_passed ? "true" : "false")
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
  if (!output) throw std::runtime_error("could not write fair D4/s7 artifact");
}

bool nearlyEqual(double first, double second, double tolerance = 1.0e-9) {
  if (std::isinf(first) || std::isinf(second)) return first == second;
  return std::abs(first - second) <= tolerance;
}

bool exactCoverage(std::uint32_t seed, bool next_visible) {
  for (int event = 0; event < 12; ++event) {
    std::array<int, kBoardSize> counts{};
    for (int sample = 0; sample < kCandidateSamples; ++sample) {
      std::uint8_t disc = 0;
      if (next_visible) {
        disc = cfpi::detail::sampledNextDisc(
            seed ^ static_cast<std::uint32_t>(event), sample,
            kCandidateSamples);
      } else {
        cfpi::detail::StratifiedRandom random{
            seed, sample, kCandidateSamples, event};
        disc = random.nextDisc();
      }
      if (disc < 1 || disc > kBoardSize) return false;
      ++counts[disc - 1];
    }
    if (!std::all_of(counts.begin(), counts.end(),
                     [](int count) { return count == 1; })) {
      return false;
    }
  }
  return true;
}

bool selfTest(std::ostream& output) {
  std::ostringstream inherited_output;
  const bool inherited = d4::selfTest(inherited_output);
  const State state = fair::fixtureState(fair::kTypeScriptFixtures[1]);
  const d4::SearchDecision stock = d4::chooseDepth4Action(state);
  const SearchDecision parity = chooseAction<kBaselineSamples>(state);
  bool s5_parity = parity.action == stock.action &&
                   parity.depth3_action == stock.depth3_action &&
                   parity.completed_depth == stock.completed_depth &&
                   parity.complete == stock.complete &&
                   parity.work == stock.work && parity.nodes == stock.nodes &&
                   parity.cache_hits == stock.cache_hits &&
                   parity.cache_entries == stock.cache_entries;
  for (int column = 0; column < kBoardSize; ++column) {
    s5_parity =
        s5_parity &&
        nearlyEqual(parity.root_values[column], stock.root_values[column],
                    1.0e-10) &&
        nearlyEqual(parity.root_expected_scores[column],
                    stock.root_expected_scores[column], 1.0e-10);
  }

  const SearchDecision first = chooseAction<kCandidateSamples>(state);
  const SearchDecision repeat = chooseAction<kCandidateSamples>(state);
  State reflected = state;
  reflected.board = cfpi::detail::mirrorBoard(state.board);
  const SearchDecision mirrored = chooseAction<kCandidateSamples>(reflected);
  State metadata = state;
  metadata.score = 8'888'888;
  metadata.level = 81;
  metadata.moves_played = 612;
  const SearchDecision metadata_decision =
      chooseAction<kCandidateSamples>(metadata);
  const bool deterministic =
      repeat.action == first.action &&
      repeat.depth3_action == first.depth3_action &&
      repeat.work == first.work && repeat.nodes == first.nodes &&
      repeat.cache_hits == first.cache_hits &&
      repeat.cache_entries == first.cache_entries;
  const bool reflection_safe =
      mirrored.action == kBoardSize - 1 - first.action &&
      mirrored.depth3_action == kBoardSize - 1 - first.depth3_action &&
      mirrored.work == first.work;
  const bool public_only = metadata_decision.action == first.action &&
                           metadata_decision.depth3_action ==
                               first.depth3_action &&
                           metadata_decision.work == first.work;
  const bool legal = isLegal(state.board, first.action) &&
                     isLegal(state.board, first.depth3_action);
  const bool bounded = first.work <= kMaximumWork &&
                       first.cache_entries <= kMaximumCacheEntries;
  const bool completion_proven =
      kMaximumWork > kWorstCaseS7Work;
  const bool cache_eviction_safe =
      kMaximumCacheEntries < kWorstCaseS7CacheEntries;
  const bool tight_rss_target =
      kMaximumRssBytes <= 64ull * 1024ull * 1024ull;
  bool next_disc_coverage = true;
  bool reveal_stratification = true;
  for (std::uint32_t seed = 0x1122'3300u; seed < 0x1122'3310u; ++seed) {
    next_disc_coverage = next_disc_coverage && exactCoverage(seed, true);
    reveal_stratification =
        reveal_stratification && exactCoverage(seed, false);
  }
  const bool tail_exact = nearlyEqual(
      lowerTailMean({1.0, 2.0, 10.0, 20.0, 30.0, 40.0}, 0.25),
      4.0 / 3.0);
  const bool frozen_semantics =
      fair::kPolicySeed == 0xd707'5eedu &&
      fair::kTerminalUtility == -1'000'000.0 &&
      cfpi::detail::kColumnOrder ==
          std::array<int, kBoardSize>{{3, 2, 4, 1, 5, 0, 6}};
  const bool protocol =
      kLevelBonus == 7'000 && kDepth == 4 && kBaselineSamples == 5 &&
      kCandidateSamples == 7 && kMaximumMoves >= 1'000 &&
      kFittingGames == 8 && kHeldoutGames == 16 && kScreenGames == 8 &&
      kConfirmationGames == 16 && kFittingStart == 0x3de1'0000u &&
      kHeldoutStart == 0x3de2'0000u && kScreenStart == 0x3ead'0000u &&
      kConfirmationStart == 0x3eae'0000u;
  const bool passed = inherited && s5_parity && deterministic &&
                      reflection_safe && public_only && legal && bounded &&
                      completion_proven && cache_eviction_safe &&
                      tight_rss_target && next_disc_coverage &&
                      reveal_stratification && tail_exact &&
                      frozen_semantics && protocol;
  output << std::boolalpha << std::setprecision(12)
         << "FAIR_DEPTH4_S7_SELF_TEST {\"passed\":" << passed
         << ",\"inheritedD4Test\":" << inherited
         << ",\"exactS5Parity\":" << s5_parity
         << ",\"s7NextDiscCoverage\":" << next_disc_coverage
         << ",\"s7RevealStratification\":" << reveal_stratification
         << ",\"deterministic\":" << deterministic
         << ",\"reflectionSafe\":" << reflection_safe
         << ",\"publicStateOnly\":" << public_only
         << ",\"legal\":" << legal << ",\"bounded\":" << bounded
         << ",\"completionProven\":" << completion_proven
         << ",\"completionIndependentOfCache\":"
         << cache_eviction_safe
         << ",\"tightRssTarget\":" << tight_rss_target
         << ",\"fractionalTailExact\":" << tail_exact
         << ",\"frozenSemantics\":" << frozen_semantics
         << ",\"fixedProtocol\":" << protocol
         << ",\"work\":" << first.work
         << ",\"cacheEntries\":" << first.cache_entries
         << ",\"worstCaseWork\":" << kWorstCaseS7Work
         << ",\"worstCaseCache\":" << kWorstCaseS7CacheEntries
         << ",\"rssTargetBytes\":" << kMaximumRssBytes
         << ",\"levelBonus\":" << kLevelBonus << "}\n";
  return passed;
}

d4::GameResult recoveredGame(std::uint32_t seed, std::int64_t score,
                             int moves, std::uint64_t switches,
                             std::uint64_t cleared, std::uint64_t revealed,
                             std::uint64_t work,
                             std::size_t peak_cache_entries) {
  d4::GameResult result;
  result.seed = seed;
  result.score = score;
  result.moves = moves;
  result.depth_switches = switches;
  result.numbered_cleared = cleared;
  result.covers_revealed = revealed;
  result.work = work;
  result.peak_cache_entries = peak_cache_entries;
  return result;
}

Cohort recoveredFittingCohort() {
  Cohort result;
  result.s5 = {
      recoveredGame(0x3de1'0000u, 195'484, 121, 51, 247, 138,
                    166'840'624, 35'266),
      recoveredGame(0x3de1'0001u, 116'292, 80, 31, 150, 77,
                    86'933'928, 33'526),
      recoveredGame(0x3de1'0002u, 59'572, 45, 19, 69, 37,
                    51'391'364, 36'438),
      recoveredGame(0x3de1'0003u, 129'505, 85, 28, 168, 94,
                    113'746'652, 36'122),
      recoveredGame(0x3de1'0004u, 114'901, 75, 28, 141, 79,
                    96'888'346, 33'435),
      recoveredGame(0x3de1'0005u, 59'235, 45, 22, 69, 35,
                    48'326'215, 35'787),
      recoveredGame(0x3de1'0006u, 223'810, 140, 55, 294, 167,
                    201'246'491, 36'619),
      recoveredGame(0x3de1'0007u, 50'609, 40, 10, 53, 26,
                    41'368'040, 35'359),
  };
  result.s7 = {
      recoveredGame(0x3de1'0000u, 150'058, 100, 26, 191, 103,
                    467'141'144, 24'000),
      recoveredGame(0x3de1'0001u, 124'798, 85, 19, 158, 83,
                    428'124'408, 24'000),
      recoveredGame(0x3de1'0002u, 155'825, 105, 25, 215, 120,
                    555'635'003, 24'000),
      recoveredGame(0x3de1'0003u, 73'873, 55, 11, 89, 45,
                    248'747'657, 24'000),
      recoveredGame(0x3de1'0004u, 94'343, 65, 14, 111, 55,
                    326'657'671, 24'000),
      recoveredGame(0x3de1'0005u, 52'587, 40, 10, 57, 30,
                    156'688'614, 24'000),
      recoveredGame(0x3de1'0006u, 179'672, 120, 20, 244, 134,
                    598'799'648, 24'000),
      recoveredGame(0x3de1'0007u, 116'943, 79, 18, 142, 74,
                    358'667'306, 24'000),
  };
  return result;
}

Cohort recoveredPartialHeldoutCohort() {
  Cohort result;
  result.s5 = {
      recoveredGame(0x3de2'0000u, 65'505, 45, 14, 66, 33,
                    55'487'276, 37'568),
      recoveredGame(0x3de2'0002u, 98'147, 70, 28, 121, 65,
                    85'797'605, 34'804),
      recoveredGame(0x3de2'0003u, 177'959, 115, 43, 238, 134,
                    164'060'475, 38'236),
      recoveredGame(0x3de2'0004u, 211'550, 135, 51, 279, 158,
                    185'938'398, 36'937),
  };
  result.s7 = {
      recoveredGame(0x3de2'0000u, 164'953, 110, 30, 220, 125,
                    634'989'349, 24'000),
      recoveredGame(0x3de2'0002u, 69'056, 50, 13, 82, 42,
                    212'829'699, 24'000),
      recoveredGame(0x3de2'0003u, 117'862, 80, 17, 146, 77,
                    393'610'691, 24'000),
      recoveredGame(0x3de2'0004u, 76'958, 55, 12, 89, 44,
                    243'165'012, 24'000),
  };
  return result;
}

void writeReliableGame(std::ostream& output, const d4::GameResult& game) {
  output << "{\"seed\":" << game.seed << ",\"score\":" << game.score
         << ",\"moves\":" << game.moves
         << ",\"censored\":false,\"numberedCleared\":"
         << game.numbered_cleared << ",\"coversRevealed\":"
         << game.covers_revealed << ",\"depthSwitches\":"
         << game.depth_switches << ",\"work\":" << game.work
         << ",\"peakCacheEntries\":" << game.peak_cache_entries << '}';
}

void writeReliableSummary(std::ostream& output, const d4::Summary& summary) {
  output << "{\"games\":" << summary.games << ",\"depth\":"
         << summary.depth << ",\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"censored\":" << summary.censored
         << ",\"meanNumberedCleared\":"
         << summary.mean_numbered_cleared
         << ",\"meanCoversRevealed\":"
         << summary.mean_covers_revealed
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"depthSwitches\":" << summary.depth_switches
         << ",\"switchRate\":" << summary.switch_rate
         << ",\"work\":" << summary.work
         << ",\"workPerMove\":" << summary.work_per_move
         << ",\"peakCacheEntries\":" << summary.peak_cache_entries << '}';
}

void writeRecoveredCohort(std::ostream& output, const Cohort& cohort,
                          const Analysis& analysis, bool formal,
                          bool passed) {
  output << "{\"formal\":" << (formal ? "true" : "false")
         << ",\"complete\":" << (formal ? "true" : "false")
         << ",\"completedPairs\":" << cohort.s5.size()
         << ",\"fairD4S5\":";
  writeReliableSummary(output, analysis.s5);
  output << ",\"fairD4S7\":";
  writeReliableSummary(output, analysis.s7);
  output << ",\"paired\":";
  d4::writePaired(output, analysis.paired);
  output << ",\"lowerTail25\":{\"s5Score\":"
         << analysis.s5_tail_score << ",\"s7Score\":"
         << analysis.s7_tail_score << ",\"s5Moves\":"
         << analysis.s5_tail_moves << ",\"s7Moves\":"
         << analysis.s7_tail_moves << "},\"passed\":"
         << (passed ? "true" : "false") << ",\"pairs\":[";
  for (std::size_t game = 0; game < cohort.s5.size(); ++game) {
    if (game != 0) output << ',';
    output << "{\"seed\":" << cohort.s5[game].seed
           << ",\"fairD4S5\":";
    writeReliableGame(output, cohort.s5[game]);
    output << ",\"fairD4S7\":";
    writeReliableGame(output, cohort.s7[game]);
    output << '}';
  }
  output << "]}";
}

int writeStoppedArtifact(const Options& options, std::ostream& result_output) {
  const Cohort fitting = recoveredFittingCohort();
  const Analysis fitting_analysis = analyze(fitting);
  const bool fitting_passed = passesGate(fitting_analysis);
  const Cohort partial = recoveredPartialHeldoutCohort();
  const Analysis partial_analysis = analyze(partial);
  std::ofstream output(options.output, std::ios::trunc);
  if (!output) {
    throw std::runtime_error("could not open stopped fair D4/s7 artifact");
  }
  output << std::setprecision(17)
         << "{\n  \"experiment\":\"fair-full-width-d4-s7\",\n"
         << "  \"preregistered\":true,\n"
         << "  \"status\":\"stopped-after-decisive-fitting-failure\",\n"
         << "  \"publicStateOnly\":true,\n"
         << "  \"onlySampleCountDiffers\":true,\n"
         << "  \"search\":{\"depth\":4,\"baselineSamples\":5,"
            "\"candidateSamples\":7,\"maximumWork\":12000000,"
            "\"worstCaseCandidateWork\":11892398,"
            "\"maximumCacheEntries\":24000,"
            "\"worstCaseCandidateCacheEntries\":122598,"
            "\"completionIndependentOfCache\":true,"
            "\"maximumRssBytes\":67108864,\"maximumMoves\":1000,"
            "\"parallelism\":4},\n"
         << "  \"fitting\":";
  writeRecoveredCohort(output, fitting, fitting_analysis, true,
                       fitting_passed);
  output << ",\n  \"heldoutExploratoryPartial\":";
  writeRecoveredCohort(output, partial, partial_analysis, false, false);
  output << ",\n  \"heldoutExploratoryWarning\":"
            "\"four completed pairs are incomplete, selection-stopped, and not a formal heldout result\",\n"
         << "  \"additionalCompletedS5Only\":[";
  writeReliableGame(output, recoveredGame(0x3de2'0001u, 85'996, 60, 18,
                                          103, 56, 61'241'241, 34'114));
  output << ',';
  writeReliableGame(output, recoveredGame(0x3de2'0005u, 181'358, 105, 38,
                                          204, 113, 134'331'962, 34'408));
  output << "],\n  \"interruption\":{"
            "\"requestedAfterFittingFailure\":true,"
            "\"processExitCode\":130,"
            "\"inFlightGamesExcluded\":true,"
            "\"exactWallThroughputUnavailable\":true,"
            "\"exactPeakRssUnavailable\":true,"
            "\"rssCeilingEnforcedBytes\":67108864},\n"
         << "  \"tests\":{\"optimizedWerror\":true,"
            "\"asanUbsanWerror\":true,\"exactS5Parity\":true,"
            "\"s7NextDiscCoverage\":true,"
            "\"s7RevealStratification\":true,"
            "\"reflectionSafe\":true,\"publicStateOnly\":true,"
            "\"deterministic\":true,\"legal\":true,"
            "\"completionProven\":true},\n"
         << "  \"fittingPassed\":"
         << (fitting_passed ? "true" : "false")
         << ",\n  \"formalHeldoutCompleted\":false,\n"
         << "  \"gatePassed\":false,\n  \"screen\":null,\n"
         << "  \"confirmation\":null,\n  \"screenRan\":false,\n"
         << "  \"confirmationRan\":false,\n  \"qualified\":false\n}\n";
  if (!output) throw std::runtime_error("could not write stopped artifact");
  result_output << "FAIR_DEPTH4_S7_STOPPED_ARTIFACT {\"fittingPassed\":"
                << (fitting_passed ? "true" : "false")
                << ",\"completedHeldoutPairs\":4,"
                << "\"formalHeldoutCompleted\":false,\"artifact\":\""
                << options.output << "\"}\n";
  return 0;
}

int run(const Options& options, std::ostream& output) {
  const auto started = std::chrono::steady_clock::now();
  const Cohort fitting =
      runCohort(kFittingStart, kFittingGames, "fitting");
  const Analysis fitting_analysis = analyze(fitting);
  const bool fitting_passed = passesGate(fitting_analysis);

  // Complete the whole heldout without changing anything after fitting.
  const Cohort heldout =
      runCohort(kHeldoutStart, kHeldoutGames, "heldout");
  const Analysis heldout_analysis = analyze(heldout);
  const bool heldout_passed = passesGate(heldout_analysis);
  const bool gate_passed = fitting_passed && heldout_passed;

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
  writeArtifact(options, fitting, fitting_analysis, fitting_passed,
                heldout, heldout_analysis, heldout_passed,
                gate_passed ? &screen : nullptr,
                gate_passed ? &screen_analysis : nullptr, screen_passed,
                screen_passed ? &confirmation : nullptr,
                screen_passed ? &confirmation_analysis : nullptr,
                confirmation_passed, total_wall_seconds);
  output << std::fixed << std::setprecision(3)
         << "FAIR_DEPTH4_S7_RESULT {\"fittingS5Score\":"
         << fitting_analysis.s5.mean_score << ",\"fittingS7Score\":"
         << fitting_analysis.s7.mean_score << ",\"fittingS5Moves\":"
         << fitting_analysis.s5.mean_moves << ",\"fittingS7Moves\":"
         << fitting_analysis.s7.mean_moves << ",\"fittingPassed\":"
         << (fitting_passed ? "true" : "false")
         << ",\"heldoutS5Score\":" << heldout_analysis.s5.mean_score
         << ",\"heldoutS7Score\":" << heldout_analysis.s7.mean_score
         << ",\"heldoutS5Moves\":" << heldout_analysis.s5.mean_moves
         << ",\"heldoutS7Moves\":" << heldout_analysis.s7.mean_moves
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
         << ",\"rssTargetBytes\":" << kMaximumRssBytes
         << ",\"totalWallSeconds\":" << total_wall_seconds
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return 0;
}

}  // namespace drop7::fair_depth4_s7

int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::fair_depth4_s7::selfTest(std::cout) ? EXIT_SUCCESS
                                                       : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::fair_depth4_s7::parseOptions(argc, argv, 2);
      return drop7::fair_depth4_s7::run(options, std::cout);
    }
    if (argc >= 2 &&
        std::string_view(argv[1]) == "--write-stopped-artifact") {
      const auto options =
          drop7::fair_depth4_s7::parseOptions(argc, argv, 2);
      return drop7::fair_depth4_s7::writeStoppedArtifact(options, std::cout);
    }
    std::cerr << "usage: drop7_fair_depth4_s7 --self-test | --run | "
                 "--write-stopped-artifact "
                 "[--output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
