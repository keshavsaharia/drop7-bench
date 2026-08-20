// Compares the reference fair-D4/s5 policy, a D4/s3 sampling control, and
// completed full-width fair-D5/s3 at a cycle boundary.
#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#include "../reference/fair-only-depth4.cpp"
#undef DROP7_FAIR_ONLY_DEPTH4_LIBRARY

#include <sstream>

namespace drop7::fair_depth5_s3 {

namespace d4 = drop7::fair_only_depth4;
namespace fair = drop7::fair_only_horizon;

constexpr int kControlDepth = 4;
constexpr int kCandidateDepth = 5;
constexpr int kStockSamples = 5;
constexpr int kCandidateSamples = 3;
constexpr std::uint64_t kMaximumWork = 9'000'000;
constexpr std::size_t kMaximumCacheEntries = 24'000;
constexpr std::uint64_t kMaximumRssBytes = 128ull * 1024ull * 1024ull;
constexpr int kMaximumMoves = 1'000;
constexpr int kParallelism = 2;
constexpr int kFittingGames = 8;
constexpr int kScreenGames = 8;
constexpr int kConfirmationGames = 16;
constexpr double kMaximumThroughputRegression = 0.02;
constexpr double kMaximumProjectedFittingSeconds = 45.0 * 60.0;
constexpr std::uint32_t kFittingStart = 0x3de4'0000u;
constexpr std::uint32_t kScreenStart = 0x3eb1'0000u;
constexpr std::uint32_t kConfirmationStart = 0x3eb2'0000u;

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

constexpr std::uint64_t kWorstCaseD5S3Work =
    worstCaseIterativeWork(kCandidateSamples, kCandidateDepth);
constexpr std::uint64_t kWorstCaseD5S3CacheEntries =
    worstCaseIterativeCacheEntries(kCandidateSamples, kCandidateDepth);

static_assert(kLevelBonus == 7'000);
static_assert(kControlDepth == d4::kCandidateDepth);
static_assert(kStockSamples == d4::kChanceSamples);
static_assert(kWorstCaseD5S3Work == 8'791'020);
static_assert(kWorstCaseD5S3CacheEntries == 214'410);
static_assert(kMaximumWork > kWorstCaseD5S3Work);
static_assert(kMaximumCacheEntries < kWorstCaseD5S3CacheEntries);
static_assert(kMaximumRssBytes == 134'217'728);
static_assert(kMaximumMoves >= 1'000 && kParallelism == 2);
static_assert(fair::kPolicySeed == 0xd707'5eedu);
static_assert(fair::kTerminalUtility == -1'000'000.0);
static_assert(kFittingStart + kFittingGames < kScreenStart);
static_assert(kScreenStart + kScreenGames < kConfirmationStart);
static_assert((kFittingStart >> 24) != 0x7du &&
              (kFittingStart >> 24) != 0xd7u);
static_assert((kScreenStart >> 24) != 0x7du &&
              (kScreenStart >> 24) != 0xd7u);
static_assert((kConfirmationStart >> 24) != 0x7du &&
              (kConfirmationStart >> 24) != 0xd7u);

class WorkLimitReached : public std::exception {};

struct CacheEntry {
  double value = 0.0;
  std::list<std::string>::iterator order;
};

template <int Depth, int Samples>
struct SearchContext {
  std::unordered_map<std::string, CacheEntry> cache;
  std::list<std::string> order;
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
  std::uint64_t cache_hits = 0;
};

template <int Depth, int Samples>
constexpr std::uint64_t maximumWorkFor() {
  if constexpr (Depth == kControlDepth && Samples == kStockSamples) {
    return d4::kMaximumWork;
  }
  return kMaximumWork;
}

template <int Depth, int Samples>
constexpr std::size_t maximumCacheEntriesFor() {
  if constexpr (Depth == kControlDepth && Samples == kStockSamples) {
    return d4::kMaximumCacheEntries;
  }
  return kMaximumCacheEntries;
}

template <int Depth, int Samples>
void checkBudget(const SearchContext<Depth, Samples>& context) {
  if (context.work >= maximumWorkFor<Depth, Samples>()) {
    throw WorkLimitReached{};
  }
}

template <int Depth, int Samples>
void cacheValue(SearchContext<Depth, Samples>& context, std::string key,
                double value) {
  const auto prior = context.cache.find(key);
  if (prior != context.cache.end()) {
    context.order.erase(prior->second.order);
    context.cache.erase(prior);
  }
  while (context.cache.size() >= maximumCacheEntriesFor<Depth, Samples>()) {
    const std::string& oldest = context.order.front();
    context.cache.erase(oldest);
    context.order.pop_front();
  }
  context.order.push_back(key);
  const auto order = std::prev(context.order.end());
  context.cache.emplace(std::move(key), CacheEntry{value, order});
}

template <int Depth, int Samples>
double bestFutureValue(const State& state, int remaining_depth,
                       SearchContext<Depth, Samples>& context);

struct ActionValue {
  double value = 0.0;
  double expected_score = 0.0;
};

template <int Depth, int Samples>
ActionValue evaluateAction(const State& state, int column,
                           int remaining_depth,
                           SearchContext<Depth, Samples>& context) {
  const std::uint32_t state_seed = cfpi::detail::scenarioSeedForState(
      state, fair::kPolicySeed, remaining_depth);
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
    result.value += score_delta +
                    bestFutureValue(next, remaining_depth - 1, context);
  }
  result.value /= Samples;
  result.expected_score /= Samples;
  return result;
}

template <int Depth, int Samples>
double evaluateLeaf(const State& state,
                    SearchContext<Depth, Samples>& context) {
  checkBudget(context);
  ++context.work;
  const double value = fair::fairLeaf(state);
  if (!std::isfinite(value)) {
    throw std::runtime_error("fair D5/s3 leaf was non-finite");
  }
  return value;
}

template <int Depth, int Samples>
double bestFutureValue(const State& state, int remaining_depth,
                       SearchContext<Depth, Samples>& context) {
  ++context.nodes;
  checkBudget(context);
  if (state.game_over) return fair::kTerminalUtility;
  if (remaining_depth == 0) return evaluateLeaf(state, context);
  const std::string key =
      cfpi::detail::dynamicStateKey(state, remaining_depth);
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
        best, evaluateAction(state, column, remaining_depth, context).value);
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

template <int Depth, int Samples>
RootEvaluation rootDecision(const State& canonical, int remaining_depth,
                            SearchContext<Depth, Samples>& context) {
  RootEvaluation result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  result.expected_scores.fill(-std::numeric_limits<double>::infinity());
  for (const int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(canonical.board, column)) continue;
    const ActionValue candidate =
        evaluateAction(canonical, column, remaining_depth, context);
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
  int prior_depth_action = -1;
  int completed_depth = 0;
  bool complete = false;
  bool switched_from_prior_depth = false;
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
  std::uint64_t cache_hits = 0;
  std::size_t cache_entries = 0;
  std::array<double, kBoardSize> root_values{};
  std::array<double, kBoardSize> root_expected_scores{};
};

template <int Depth, int Samples>
SearchDecision chooseAction(const State& source) {
  static_assert(Depth >= 2);
  if (source.game_over) return {};
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  SearchContext<Depth, Samples> context;
  RootEvaluation completed;
  int completed_depth = 0;
  int prior_depth_action = -1;
  for (int depth = 1; depth <= Depth; ++depth) {
    try {
      completed = rootDecision(canonical, depth, context);
      if (completed.action < 0) break;
      completed_depth = depth;
      if (depth == Depth - 1) prior_depth_action = completed.action;
    } catch (const WorkLimitReached&) {
      break;
    }
  }
  int action = completed.action;
  if (action < 0) action = centerFirstMove(canonical.board);
  if (prior_depth_action < 0) prior_depth_action = action;
  SearchDecision result;
  result.action = mirrored ? kBoardSize - 1 - action : action;
  result.prior_depth_action =
      mirrored ? kBoardSize - 1 - prior_depth_action : prior_depth_action;
  result.completed_depth = completed_depth;
  result.complete = completed_depth == Depth;
  result.switched_from_prior_depth =
      result.action != result.prior_depth_action;
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

template <int Depth, int Samples>
d4::GameResult runCustomGame(std::uint32_t seed, std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  d4::GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    const SearchDecision decision = chooseAction<Depth, Samples>(state);
    if (!decision.complete || decision.completed_depth != Depth) {
      throw std::runtime_error("fair custom search did not complete");
    }
    if (!isLegal(state.board, decision.action) ||
        !isLegal(state.board, decision.prior_depth_action)) {
      throw std::runtime_error("fair custom search decision was illegal");
    }
    if (d4::peakRssBytes() > kMaximumRssBytes) {
      throw std::runtime_error("fair D5/s3 exceeded RSS target");
    }
    result.depth_switches += decision.switched_from_prior_depth;
    ++result.action_counts[decision.action];
    result.work += decision.work;
    result.nodes += decision.nodes;
    result.cache_hits += decision.cache_hits;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, decision.cache_entries);
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("fair custom game transition failed");
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

struct TripleResult {
  d4::GameResult stock;
  d4::GameResult d4_s3;
  d4::GameResult d5_s3;
  double wall_seconds = 0.0;
};

TripleResult runTriple(std::uint32_t seed, std::string_view phase) {
  const auto started = std::chrono::steady_clock::now();
  TripleResult result;
  result.stock = d4::runDepth4Game(seed, std::string(phase) + "-fair-d4-s5");
  result.d4_s3 = runCustomGame<kControlDepth, kCandidateSamples>(
      seed, std::string(phase) + "-fair-d4-s3");
  result.d5_s3 = runCustomGame<kCandidateDepth, kCandidateSamples>(
      seed, std::string(phase) + "-fair-d5-s3");
  result.wall_seconds = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started)
                            .count();
  return result;
}

struct Cohort {
  std::vector<d4::GameResult> stock;
  std::vector<d4::GameResult> d4_s3;
  std::vector<d4::GameResult> d5_s3;
  double wall_seconds = 0.0;
};

void append(Cohort& cohort, TripleResult triple) {
  cohort.stock.push_back(std::move(triple.stock));
  cohort.d4_s3.push_back(std::move(triple.d4_s3));
  cohort.d5_s3.push_back(std::move(triple.d5_s3));
  cohort.wall_seconds += triple.wall_seconds;
}

Cohort runRemainingCohort(std::uint32_t seed_start, int begin_game,
                          int games, std::string_view phase) {
  const auto started = std::chrono::steady_clock::now();
  Cohort result;
  const int remaining = games - begin_game;
  result.stock.resize(static_cast<std::size_t>(remaining));
  result.d4_s3.resize(static_cast<std::size_t>(remaining));
  result.d5_s3.resize(static_cast<std::size_t>(remaining));
  std::atomic<int> next_game{begin_game};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, remaining); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= games) return;
        TripleResult triple = runTriple(
            seed_start + static_cast<std::uint32_t>(game), phase);
        const std::size_t destination =
            static_cast<std::size_t>(game - begin_game);
        result.stock[destination] = std::move(triple.stock);
        result.d4_s3[destination] = std::move(triple.d4_s3);
        result.d5_s3[destination] = std::move(triple.d5_s3);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.wall_seconds = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started)
                            .count();
  return result;
}

void append(Cohort& destination, Cohort source) {
  destination.stock.insert(destination.stock.end(),
                           std::make_move_iterator(source.stock.begin()),
                           std::make_move_iterator(source.stock.end()));
  destination.d4_s3.insert(destination.d4_s3.end(),
                           std::make_move_iterator(source.d4_s3.begin()),
                           std::make_move_iterator(source.d4_s3.end()));
  destination.d5_s3.insert(destination.d5_s3.end(),
                           std::make_move_iterator(source.d5_s3.begin()),
                           std::make_move_iterator(source.d5_s3.end()));
  destination.wall_seconds += source.wall_seconds;
}

d4::PairedSummary pairedSummary(const std::vector<d4::GameResult>& baseline,
                                const std::vector<d4::GameResult>& candidate) {
  if (baseline.size() != candidate.size() || baseline.empty()) {
    throw std::invalid_argument("invalid fair D5/s3 paired cohort");
  }
  std::vector<double> scores;
  std::vector<double> moves;
  std::vector<double> cleared;
  std::vector<double> revealed;
  for (std::size_t game = 0; game < baseline.size(); ++game) {
    scores.push_back(static_cast<double>(candidate[game].score) -
                     static_cast<double>(baseline[game].score));
    moves.push_back(static_cast<double>(candidate[game].moves) -
                    static_cast<double>(baseline[game].moves));
    cleared.push_back(
        static_cast<double>(candidate[game].numbered_cleared) -
        static_cast<double>(baseline[game].numbered_cleared));
    revealed.push_back(
        static_cast<double>(candidate[game].covers_revealed) -
        static_cast<double>(baseline[game].covers_revealed));
  }
  return {d4::differences(scores), d4::differences(moves),
          d4::differences(cleared), d4::differences(revealed)};
}

struct Analysis {
  d4::Summary stock;
  d4::Summary d4_s3;
  d4::Summary d5_s3;
  d4::PairedSummary d5_vs_stock;
  d4::PairedSummary d5_vs_d4_s3;
};

Analysis analyze(const Cohort& cohort) {
  if (cohort.stock.size() != cohort.d4_s3.size() ||
      cohort.stock.size() != cohort.d5_s3.size() ||
      cohort.stock.empty()) {
    throw std::invalid_argument("invalid fair D5/s3 cohort");
  }
  Analysis result;
  result.stock = d4::summarize(cohort.stock, kControlDepth);
  result.d4_s3 = d4::summarize(cohort.d4_s3, kControlDepth);
  result.d5_s3 = d4::summarize(cohort.d5_s3, kCandidateDepth);
  result.d5_vs_stock = pairedSummary(cohort.stock, cohort.d5_s3);
  result.d5_vs_d4_s3 = pairedSummary(cohort.d4_s3, cohort.d5_s3);
  return result;
}

bool meansImprove(const d4::Summary& baseline,
                  const d4::Summary& candidate) {
  return candidate.mean_score > baseline.mean_score &&
         candidate.mean_moves > baseline.mean_moves;
}

bool throughputWithinTwoPercent(const d4::Summary& baseline,
                                const d4::Summary& candidate) {
  const double minimum_fraction = 1.0 - kMaximumThroughputRegression;
  return candidate.clears_per_move >=
             minimum_fraction * baseline.clears_per_move &&
         candidate.reveals_per_move >=
             minimum_fraction * baseline.reveals_per_move;
}

bool fittingPasses(const Analysis& analysis) {
  return meansImprove(analysis.stock, analysis.d5_s3) &&
         meansImprove(analysis.d4_s3, analysis.d5_s3) &&
         throughputWithinTwoPercent(analysis.stock, analysis.d5_s3) &&
         throughputWithinTwoPercent(analysis.d4_s3, analysis.d5_s3);
}

bool downstreamPasses(const Analysis& analysis) {
  return meansImprove(analysis.stock, analysis.d5_s3);
}

void writePair(std::ostream& output, const Cohort& cohort,
               std::size_t game) {
  output << "{\"seed\":" << cohort.stock[game].seed
         << ",\"fairD4S5\":";
  d4::writeGame(output, cohort.stock[game]);
  output << ",\"fairD4S3\":";
  d4::writeGame(output, cohort.d4_s3[game]);
  output << ",\"fairD5S3\":";
  d4::writeGame(output, cohort.d5_s3[game]);
  output << '}';
}

void writeCohort(std::ostream& output, std::uint32_t seed_start,
                 const Cohort& cohort, const Analysis& analysis,
                 bool passed) {
  output << "{\"seedStart\":" << seed_start
         << ",\"games\":" << cohort.stock.size()
         << ",\"maximumMoves\":" << kMaximumMoves
         << ",\"fairD4S5\":";
  d4::writeSummary(output, analysis.stock);
  output << ",\"fairD4S3\":";
  d4::writeSummary(output, analysis.d4_s3);
  output << ",\"fairD5S3\":";
  d4::writeSummary(output, analysis.d5_s3);
  output << ",\"d5VsStock\":";
  d4::writePaired(output, analysis.d5_vs_stock);
  output << ",\"d5VsD4S3\":";
  d4::writePaired(output, analysis.d5_vs_d4_s3);
  output << ",\"wallSeconds\":" << cohort.wall_seconds
         << ",\"passed\":" << (passed ? "true" : "false")
         << ",\"triples\":[";
  for (std::size_t game = 0; game < cohort.stock.size(); ++game) {
    if (game != 0) output << ',';
    writePair(output, cohort, game);
  }
  output << "]}";
}

struct Options {
  std::string output = "/tmp/drop7-fair-depth5-s3.json";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing fair D5/s3 option value");
    }
    const std::string_view option(argv[index]);
    if (option == "--output") {
      result.output = argv[index + 1];
    } else {
      throw std::invalid_argument("unknown fair D5/s3 option");
    }
  }
  return result;
}

void writeProtocol(std::ostream& output) {
  output << "\"scoring\":{\"levelBonus\":7000},"
         << "\"frozenSemantics\":{\"evaluator\":\"confirmed-fair-only\","
         << "\"terminalUtility\":" << fair::kTerminalUtility
         << ",\"fairTerminalUtility\":" << fair::kFairTerminalUtility
         << ",\"actionOrder\":[3,2,4,1,5,0,6],\"policySeed\":"
         << fair::kPolicySeed << "},"
         << "\"search\":{\"stockDepth\":4,\"stockSamples\":5,"
         << "\"samplingControlDepth\":4,\"samplingControlSamples\":3,"
         << "\"candidateDepth\":5,\"candidateSamples\":3,"
         << "\"fullWidth\":true,\"iterativeDeepening\":true,"
         << "\"maximumWork\":" << kMaximumWork
         << ",\"worstCaseD5S3Work\":" << kWorstCaseD5S3Work
         << ",\"maximumCacheEntries\":" << kMaximumCacheEntries
         << ",\"worstCaseD5S3CacheEntries\":"
         << kWorstCaseD5S3CacheEntries
         << ",\"completionIndependentOfCache\":true,"
         << "\"maximumRssBytes\":" << kMaximumRssBytes
         << ",\"maximumMoves\":" << kMaximumMoves
         << ",\"parallelism\":" << kParallelism << "},"
         << "\"fittingGate\":{\"d5BothMeansBeatBothControls\":true,"
         << "\"maximumClearRateRegression\":"
         << kMaximumThroughputRegression
         << ",\"maximumRevealRateRegression\":"
         << kMaximumThroughputRegression << "},"
         << "\"runtimePilot\":{\"seed\":" << kFittingStart
         << ",\"maximumProjectedFittingSeconds\":"
         << kMaximumProjectedFittingSeconds << '}';
}

void writePausedArtifact(const Options& options, const Cohort& pilot,
                         double projected_seconds) {
  const Analysis analysis = analyze(pilot);
  std::ofstream output(options.output, std::ios::trunc);
  if (!output) throw std::runtime_error("could not open D5 pause artifact");
  output << std::setprecision(17)
         << "{\n  \"experiment\":\"fair-cycle-crossing-d5-s3\",\n"
         << "  \"preregistered\":true,\n  \"publicStateOnly\":true,\n"
         << "  \"status\":\"paused-after-runtime-pilot\",\n  ";
  writeProtocol(output);
  output << ",\n  \"pilot\":";
  writeCohort(output, kFittingStart, pilot, analysis, false);
  output << ",\n  \"projectedFittingSeconds\":" << projected_seconds
         << ",\n  \"additionalFittingSeedsRead\":false,\n"
         << "  \"fittingCompleted\":false,\n  \"screen\":null,\n"
         << "  \"confirmation\":null,\n  \"qualified\":false,\n"
         << "  \"peakRssBytes\":" << d4::peakRssBytes() << "\n}\n";
  if (!output) throw std::runtime_error("could not write D5 pause artifact");
}

void writeArtifact(const Options& options, const Cohort& fitting,
                   const Analysis& fitting_analysis, bool fitting_passed,
                   const Cohort* screen, const Analysis* screen_analysis,
                   bool screen_passed, const Cohort* confirmation,
                   const Analysis* confirmation_analysis,
                   bool confirmation_passed, double projected_seconds,
                   double total_wall_seconds) {
  std::ofstream output(options.output, std::ios::trunc);
  if (!output) throw std::runtime_error("could not open D5 artifact");
  output << std::setprecision(17)
         << "{\n  \"experiment\":\"fair-cycle-crossing-d5-s3\",\n"
         << "  \"preregistered\":true,\n  \"publicStateOnly\":true,\n"
         << "  \"status\":\"completed-fitting\",\n  ";
  writeProtocol(output);
  output << ",\n  \"projectedFittingSeconds\":" << projected_seconds
         << ",\n  \"fitting\":";
  writeCohort(output, kFittingStart, fitting, fitting_analysis,
              fitting_passed);
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
  output << ",\n  \"fittingPassed\":"
         << (fitting_passed ? "true" : "false")
         << ",\n  \"screenRan\":" << (screen != nullptr ? "true" : "false")
         << ",\n  \"screenPassed\":" << (screen_passed ? "true" : "false")
         << ",\n  \"confirmationRan\":"
         << (confirmation != nullptr ? "true" : "false")
         << ",\n  \"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\n  \"qualified\":"
         << (fitting_passed && screen_passed && confirmation_passed ? "true"
                                                                     : "false")
         << ",\n  \"totalWallSeconds\":" << total_wall_seconds
         << ",\n  \"peakRssBytes\":" << d4::peakRssBytes() << "\n}\n";
  if (!output) throw std::runtime_error("could not write D5 artifact");
}

bool nearlyEqual(double first, double second, double tolerance = 1.0e-9) {
  if (std::isinf(first) || std::isinf(second)) return first == second;
  return std::abs(first - second) <= tolerance;
}

bool exactThreeStrata(std::uint32_t seed, std::uint32_t domain, int event) {
  std::array<int, kCandidateSamples> counts{};
  for (int sample = 0; sample < kCandidateSamples; ++sample) {
    const double unit = cfpi::detail::stratifiedUnit(
        seed, sample, kCandidateSamples, domain, event);
    const int stratum = static_cast<int>(std::floor(unit * kCandidateSamples));
    if (stratum < 0 || stratum >= kCandidateSamples) return false;
    ++counts[static_cast<std::size_t>(stratum)];
  }
  return std::all_of(counts.begin(), counts.end(),
                     [](int count) { return count == 1; });
}

bool selfTest(std::ostream& output) {
  std::ostringstream inherited_output;
  const bool inherited = d4::selfTest(inherited_output);
  const State state = fair::fixtureState(fair::kTypeScriptFixtures[1]);
  const d4::SearchDecision stock = d4::chooseDepth4Action(state);
  const SearchDecision parity = chooseAction<kControlDepth, kStockSamples>(state);
  bool stock_parity = parity.action == stock.action &&
                      parity.prior_depth_action == stock.depth3_action &&
                      parity.completed_depth == stock.completed_depth &&
                      parity.complete == stock.complete &&
                      parity.work == stock.work &&
                      parity.nodes == stock.nodes &&
                      parity.cache_hits == stock.cache_hits &&
                      parity.cache_entries == stock.cache_entries;
  for (int column = 0; column < kBoardSize; ++column) {
    stock_parity =
        stock_parity &&
        nearlyEqual(parity.root_values[column], stock.root_values[column],
                    1.0e-10) &&
        nearlyEqual(parity.root_expected_scores[column],
                    stock.root_expected_scores[column], 1.0e-10);
  }

  const SearchDecision control =
      chooseAction<kControlDepth, kCandidateSamples>(state);
  const SearchDecision first =
      chooseAction<kCandidateDepth, kCandidateSamples>(state);
  const SearchDecision repeat =
      chooseAction<kCandidateDepth, kCandidateSamples>(state);
  State reflected = state;
  reflected.board = cfpi::detail::mirrorBoard(state.board);
  const SearchDecision mirrored =
      chooseAction<kCandidateDepth, kCandidateSamples>(reflected);
  State metadata = state;
  metadata.score = 7'777'777;
  metadata.level = 79;
  metadata.moves_played = 631;
  const SearchDecision metadata_decision =
      chooseAction<kCandidateDepth, kCandidateSamples>(metadata);
  const bool deterministic =
      repeat.action == first.action &&
      repeat.prior_depth_action == first.prior_depth_action &&
      repeat.work == first.work && repeat.nodes == first.nodes &&
      repeat.cache_hits == first.cache_hits &&
      repeat.cache_entries == first.cache_entries;
  const bool reflection_safe =
      mirrored.action == kBoardSize - 1 - first.action &&
      mirrored.prior_depth_action ==
          kBoardSize - 1 - first.prior_depth_action &&
      mirrored.work == first.work;
  const bool public_only = metadata_decision.action == first.action &&
                           metadata_decision.prior_depth_action ==
                               first.prior_depth_action &&
                           metadata_decision.work == first.work;
  bool full_root = first.complete && first.completed_depth == kCandidateDepth;
  for (int column = 0; column < kBoardSize; ++column) {
    if (isLegal(state.board, column)) {
      full_root = full_root && std::isfinite(first.root_values[column]);
    }
  }
  const bool legal = isLegal(state.board, first.action) &&
                     isLegal(state.board, first.prior_depth_action) &&
                     isLegal(state.board, control.action);
  const bool bounded = first.work <= kMaximumWork &&
                       first.cache_entries <= kMaximumCacheEntries;
  const bool completion_proven = kMaximumWork > kWorstCaseD5S3Work;
  const bool cache_independent =
      kMaximumCacheEntries < kWorstCaseD5S3CacheEntries;
  bool stratified = true;
  for (std::uint32_t seed = 0x2233'4400u; seed < 0x2233'4410u; ++seed) {
    for (int event = 0; event < 12; ++event) {
      stratified =
          stratified &&
          exactThreeStrata(seed, cfpi::detail::kDiscSampleDomain, event) &&
          exactThreeStrata(seed, cfpi::detail::kRevealSampleDomain, event);
    }
  }
  const bool frozen_semantics =
      fair::kPolicySeed == 0xd707'5eedu &&
      fair::kTerminalUtility == -1'000'000.0 &&
      cfpi::detail::kColumnOrder ==
          std::array<int, kBoardSize>{{3, 2, 4, 1, 5, 0, 6}};
  const bool protocol =
      kLevelBonus == 7'000 && kControlDepth == 4 &&
      kCandidateDepth == 5 && kStockSamples == 5 &&
      kCandidateSamples == 3 && kMaximumMoves == 1'000 &&
      kParallelism == 2 && kFittingGames == 8 && kScreenGames == 8 &&
      kConfirmationGames == 16 && kFittingStart == 0x3de4'0000u &&
      kScreenStart == 0x3eb1'0000u &&
      kConfirmationStart == 0x3eb2'0000u &&
      kMaximumRssBytes <= 128ull * 1024ull * 1024ull;
  const bool passed = inherited && stock_parity && deterministic &&
                      reflection_safe && public_only && full_root && legal &&
                      bounded && completion_proven && cache_independent &&
                      stratified && frozen_semantics && protocol;
  output << std::boolalpha << std::setprecision(12)
         << "FAIR_DEPTH5_S3_SELF_TEST {\"passed\":" << passed
         << ",\"inheritedD4Test\":" << inherited
         << ",\"exactStockD4S5Parity\":" << stock_parity
         << ",\"fullD5Root\":" << full_root
         << ",\"deterministicStratification\":" << stratified
         << ",\"deterministic\":" << deterministic
         << ",\"reflectionSafe\":" << reflection_safe
         << ",\"publicStateOnly\":" << public_only
         << ",\"legal\":" << legal << ",\"bounded\":" << bounded
         << ",\"completionProven\":" << completion_proven
         << ",\"completionIndependentOfCache\":" << cache_independent
         << ",\"frozenSemantics\":" << frozen_semantics
         << ",\"fixedProtocol\":" << protocol
         << ",\"d4s3Action\":" << control.action
         << ",\"d5s3Action\":" << first.action
         << ",\"work\":" << first.work
         << ",\"cacheEntries\":" << first.cache_entries
         << ",\"worstCaseWork\":" << kWorstCaseD5S3Work
         << ",\"worstCaseCache\":" << kWorstCaseD5S3CacheEntries
         << ",\"rssTargetBytes\":" << kMaximumRssBytes
         << ",\"levelBonus\":" << kLevelBonus << "}\n";
  return passed;
}

int run(const Options& options, std::ostream& output) {
  const auto started = std::chrono::steady_clock::now();
  Cohort fitting;
  const TripleResult pilot = runTriple(kFittingStart, "fitting-pilot");
  const double projected_seconds =
      pilot.wall_seconds *
      (1.0 + static_cast<double>(kFittingGames - 1) / kParallelism);
  append(fitting, pilot);
  if (projected_seconds > kMaximumProjectedFittingSeconds) {
    writePausedArtifact(options, fitting, projected_seconds);
    output << std::fixed << std::setprecision(3)
           << "FAIR_DEPTH5_S3_PAUSED {\"pilotWallSeconds\":"
           << pilot.wall_seconds << ",\"projectedFittingSeconds\":"
           << projected_seconds << ",\"maximumProjectedSeconds\":"
           << kMaximumProjectedFittingSeconds
           << ",\"additionalFittingSeedsRead\":false,\"peakRssBytes\":"
           << d4::peakRssBytes() << ",\"artifact\":\"" << options.output
           << "\"}\n";
    return 0;
  }

  append(fitting, runRemainingCohort(kFittingStart, 1, kFittingGames,
                                     "fitting"));
  const Analysis fitting_analysis = analyze(fitting);
  const bool fitting_passed = fittingPasses(fitting_analysis);

  Cohort screen;
  Analysis screen_analysis;
  bool screen_passed = false;
  if (fitting_passed) {
    screen = runRemainingCohort(kScreenStart, 0, kScreenGames, "screen");
    screen_analysis = analyze(screen);
    screen_passed = downstreamPasses(screen_analysis);
  }

  Cohort confirmation;
  Analysis confirmation_analysis;
  bool confirmation_passed = false;
  if (screen_passed) {
    confirmation = runRemainingCohort(
        kConfirmationStart, 0, kConfirmationGames, "confirmation");
    confirmation_analysis = analyze(confirmation);
    confirmation_passed = downstreamPasses(confirmation_analysis);
  }
  const double total_wall_seconds = std::chrono::duration<double>(
                                          std::chrono::steady_clock::now() -
                                          started)
                                          .count();
  writeArtifact(options, fitting, fitting_analysis, fitting_passed,
                fitting_passed ? &screen : nullptr,
                fitting_passed ? &screen_analysis : nullptr, screen_passed,
                screen_passed ? &confirmation : nullptr,
                screen_passed ? &confirmation_analysis : nullptr,
                confirmation_passed, projected_seconds, total_wall_seconds);
  output << std::fixed << std::setprecision(3)
         << "FAIR_DEPTH5_S3_RESULT {\"stockScore\":"
         << fitting_analysis.stock.mean_score << ",\"stockMoves\":"
         << fitting_analysis.stock.mean_moves << ",\"d4s3Score\":"
         << fitting_analysis.d4_s3.mean_score << ",\"d4s3Moves\":"
         << fitting_analysis.d4_s3.mean_moves << ",\"d5s3Score\":"
         << fitting_analysis.d5_s3.mean_score << ",\"d5s3Moves\":"
         << fitting_analysis.d5_s3.mean_moves << ",\"fittingPassed\":"
         << (fitting_passed ? "true" : "false")
         << ",\"screenRan\":" << (fitting_passed ? "true" : "false")
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

}  // namespace drop7::fair_depth5_s3

int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::fair_depth5_s3::selfTest(std::cout) ? EXIT_SUCCESS
                                                       : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::fair_depth5_s3::parseOptions(argc, argv, 2);
      return drop7::fair_depth5_s3::run(options, std::cout);
    }
    std::cerr << "usage: drop7_fair_depth5_s3 --self-test | --run "
                 "[--output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
