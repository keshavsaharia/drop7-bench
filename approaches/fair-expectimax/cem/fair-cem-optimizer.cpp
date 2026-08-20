// Performs derivative-free complete-game optimization around the reference
// fair-only depth-three evaluator.  The search space is intentionally small and grouped;
// there are no action/column placement priors and no privileged inputs.
#define DROP7_FAIR_ONLY_HORIZON_LIBRARY
#include "../reference/fair-only-horizon.cpp"
#undef DROP7_FAIR_ONLY_HORIZON_LIBRARY

#include <fstream>
#include <sstream>
#include <unordered_set>

namespace drop7::fair_cem_optimizer {

namespace fair = drop7::fair_only_horizon;

constexpr int kCoefficientCount = 8;
constexpr int kGenerations = 8;
constexpr int kPopulation = 12;
constexpr int kElite = 3;
constexpr int kGamesPerGeneration = 3;
constexpr int kTournamentGames = 16;
constexpr int kHeldoutGames = 32;
constexpr int kScreenGames = 8;
constexpr int kConfirmationGames = 16;
constexpr int kMaximumMoves = 1'000;
constexpr int kParallelism = 4;
constexpr double kLogMultiplierBound = 0.6931471805599453;
constexpr double kInitialSigma = 0.35;
constexpr double kMinimumSigma = 0.05;
constexpr double kMaximumSigma = 0.70;
constexpr double kObjectiveMeanWeight = 0.60;
constexpr double kObjectiveTailWeight = 0.40;
constexpr double kObjectiveTailFraction = 0.25;
constexpr double kObjectiveScoreDivisor = 14'000.0;
constexpr std::uint32_t kFittingStart = 0x3dc0'0000u;
constexpr std::uint32_t kTournamentStart = 0x3dc0'0100u;
constexpr std::uint32_t kHeldoutStart = 0x3dc1'0000u;
constexpr std::uint32_t kScreenStart = 0x3ea3'0000u;
constexpr std::uint32_t kConfirmationStart = 0x3ea4'0000u;
constexpr std::uint32_t kOptimizerSeed = 0x4345'4d38u;

static_assert(kLevelBonus == 7'000);
static_assert(fair::kDepth == 3 && fair::kChanceSamples == 5);
static_assert(kPopulation >= 6 && kPopulation % 2 == 0);
static_assert(kElite >= 2 && kElite < kPopulation);
static_assert(kObjectiveMeanWeight + kObjectiveTailWeight == 1.0);
static_assert(kMaximumMoves == fair::kMaximumMoves);
static_assert(kGenerations * kPopulation * kGamesPerGeneration < 3'000);
static_assert((kFittingStart >> 24) != 0x7du &&
              (kFittingStart >> 24) != 0xd7u);
static_assert((kTournamentStart >> 24) != 0x7du &&
              (kTournamentStart >> 24) != 0xd7u);
static_assert((kHeldoutStart >> 24) != 0x7du &&
              (kHeldoutStart >> 24) != 0xd7u);
static_assert((kScreenStart >> 24) != 0x7du &&
              (kScreenStart >> 24) != 0xd7u);
static_assert((kConfirmationStart >> 24) != 0x7du &&
              (kConfirmationStart >> 24) != 0xd7u);

constexpr std::array<std::string_view, kCoefficientCount> kCoefficientNames{{
    "directTriggerMultiplier",
    "latentReleaseMultiplier",
    "coverDebtMultiplier",
    "altitudeDangerRiseMultiplier",
    "lowNumberClogMultiplier",
    "nextDiscQuietReadinessDelta",
    "revealedCoverReward",
    "additionalWaveReward",
}};

using Vector = std::array<double, kCoefficientCount>;

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

Vector clamped(Vector vector) {
  for (double& value : vector) value = std::clamp(value, -1.0, 1.0);
  return vector;
}

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
  // Preserve the reference accumulation order.  At the zero normalized vector,
  // every multiplier is exactly one and every delta is exactly zero, so this
  // is bit-for-bit identical to the reference fair leaf rather than merely
  // equivalent to it.
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
  if (context.work >= fair::kMaximumWork) throw WorkLimitReached{};
}

void cacheValue(SearchContext& context, std::string key, double value) {
  const auto prior = context.cache.find(key);
  if (prior != context.cache.end()) {
    context.order.erase(prior->second.order);
    context.cache.erase(prior);
  }
  while (context.cache.size() >= fair::kMaximumCacheEntries) {
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

double evaluateAction(const State& state, int action, int depth,
                      SearchContext& context) {
  const std::uint32_t state_seed = cfpi::detail::scenarioSeedForState(
      state, fair::kPolicySeed, depth);
  double result = 0.0;
  for (int sample = 0; sample < fair::kChanceSamples; ++sample) {
    checkBudget(context);
    cfpi::detail::StratifiedRandom random{
        state_seed, sample, fair::kChanceSamples, 0,
    };
    MoveResult move;
    const bool played =
        cfpi::detail::playMoveSampled(state, action, random, move);
    ++context.work;
    if (!played) {
      result += fair::kTerminalUtility;
      continue;
    }
    const double transition = static_cast<double>(move.score_delta) +
                              transitionBonus(move, context.coefficients);
    if (move.state.game_over) {
      result += transition + fair::kTerminalUtility;
      continue;
    }
    move.state.score = 0;
    move.state.next_disc = cfpi::detail::sampledNextDisc(
        state_seed, sample, fair::kChanceSamples);
    bool ignored = false;
    const State next = cfpi::detail::canonicalState(move.state, ignored);
    result += transition + bestFutureValue(next, depth - 1, context);
  }
  return result / fair::kChanceSamples;
}

double bestFutureValue(const State& state, int depth,
                       SearchContext& context) {
  ++context.nodes;
  checkBudget(context);
  if (state.game_over) return fair::kTerminalUtility;
  if (depth == 0) {
    ++context.work;
    const double leaf = parameterizedLeaf(state, context.coefficients);
    if (!std::isfinite(leaf)) {
      throw std::runtime_error("parameterized fair leaf is non-finite");
    }
    return leaf;
  }
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
  for (const int action : cfpi::detail::kColumnOrder) {
    if (!isLegal(state.board, action)) continue;
    best = std::max(best, evaluateAction(state, action, depth, context));
  }
  if (!std::isfinite(best)) best = fair::kTerminalUtility;
  cacheValue(context, key, best);
  return best;
}

struct SearchDecision {
  int action = -1;
  int completed_depth = 0;
  bool complete = false;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::size_t cache_entries = 0;
  std::array<double, kBoardSize> root_values{};
};

SearchDecision chooseAction(const State& source, const Vector& vector) {
  if (source.game_over) return {};
  const Coefficients coefficients = decode(vector);
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  SearchContext context(coefficients);
  int completed_depth = 0;
  int completed_action = -1;
  std::array<double, kBoardSize> completed_values{};
  completed_values.fill(-std::numeric_limits<double>::infinity());
  for (int depth = 1; depth <= fair::kDepth; ++depth) {
    try {
      int action = -1;
      double best = -std::numeric_limits<double>::infinity();
      std::array<double, kBoardSize> values{};
      values.fill(-std::numeric_limits<double>::infinity());
      for (const int candidate : cfpi::detail::kColumnOrder) {
        if (!isLegal(canonical.board, candidate)) continue;
        values[candidate] =
            evaluateAction(canonical, candidate, depth, context);
        if (values[candidate] > best) {
          best = values[candidate];
          action = candidate;
        }
      }
      if (action < 0) break;
      completed_action = action;
      completed_values = values;
      completed_depth = depth;
    } catch (const WorkLimitReached&) {
      break;
    }
  }
  if (completed_action < 0) completed_action = centerFirstMove(canonical.board);
  SearchDecision result;
  result.action = mirrored ? kBoardSize - 1 - completed_action
                           : completed_action;
  result.completed_depth = completed_depth;
  result.complete = completed_depth == fair::kDepth;
  result.work = context.work;
  result.nodes = context.nodes;
  result.cache_hits = context.cache_hits;
  result.cache_entries = context.cache.size();
  result.root_values.fill(-std::numeric_limits<double>::infinity());
  for (int canonical_action = 0; canonical_action < kBoardSize;
       ++canonical_action) {
    const int source_action = mirrored
                                  ? kBoardSize - 1 - canonical_action
                                  : canonical_action;
    result.root_values[source_action] = completed_values[canonical_action];
  }
  return result;
}

fair::GameResult runGame(const Vector& vector, std::uint32_t seed,
                         std::string_view label = {}) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  fair::GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    const SearchDecision decision = chooseAction(state, vector);
    if (!decision.complete || decision.completed_depth != fair::kDepth) {
      throw std::runtime_error("CEM fair search did not complete depth three");
    }
    if (!isLegal(state.board, decision.action)) {
      throw std::runtime_error("CEM fair search selected illegal action");
    }
    result.work += decision.work;
    result.nodes += decision.nodes;
    result.cache_hits += decision.cache_hits;
    result.maximum_cache_entries =
        std::max(result.maximum_cache_entries, decision.cache_entries);
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("CEM fair game transition failed");
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
  if (!label.empty()) fair::reportGame(label, result);
  return result;
}

double lowerTailMean(std::vector<double> values, double fraction) {
  if (values.empty() || fraction <= 0 || fraction > 1) {
    throw std::invalid_argument("invalid lower-tail request");
  }
  std::sort(values.begin(), values.end());
  const double mass = fraction * values.size();
  const int whole = static_cast<int>(std::floor(mass));
  const double fractional = mass - whole;
  double sum = 0;
  for (int index = 0; index < whole; ++index) {
    sum += values[static_cast<std::size_t>(index)];
  }
  if (fractional > 0) {
    sum += fractional * values[static_cast<std::size_t>(whole)];
  }
  return sum / mass;
}

struct Evaluation {
  Vector vector{};
  std::vector<fair::GameResult> games;
  double objective = -std::numeric_limits<double>::infinity();
  double mean_utility = 0.0;
  double tail_utility = 0.0;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double tail_score = 0.0;
  double tail_moves = 0.0;
  std::int64_t minimum_score = 0;
  int minimum_moves = 0;
  int censored = 0;
  std::uint64_t work = 0;
};

Evaluation evaluateVector(const Vector& vector, std::uint32_t seed_start,
                          int games) {
  if (games < 1) throw std::invalid_argument("empty CEM evaluation");
  Evaluation result;
  result.vector = vector;
  result.minimum_score = std::numeric_limits<std::int64_t>::max();
  result.minimum_moves = std::numeric_limits<int>::max();
  std::vector<double> utilities;
  std::vector<double> scores;
  std::vector<double> moves;
  utilities.reserve(static_cast<std::size_t>(games));
  scores.reserve(static_cast<std::size_t>(games));
  moves.reserve(static_cast<std::size_t>(games));
  for (int game = 0; game < games; ++game) {
    const fair::GameResult outcome = runGame(
        vector, seed_start + static_cast<std::uint32_t>(game));
    result.games.push_back(outcome);
    result.mean_score += static_cast<double>(outcome.score) / games;
    result.mean_moves += static_cast<double>(outcome.moves) / games;
    result.minimum_score = std::min(result.minimum_score, outcome.score);
    result.minimum_moves = std::min(result.minimum_moves, outcome.moves);
    result.censored += outcome.censored;
    result.work += outcome.work;
    const double utility = static_cast<double>(outcome.moves) +
                           static_cast<double>(outcome.score) /
                               kObjectiveScoreDivisor;
    utilities.push_back(utility);
    scores.push_back(static_cast<double>(outcome.score));
    moves.push_back(static_cast<double>(outcome.moves));
    result.mean_utility += utility / games;
  }
  result.tail_utility = lowerTailMean(utilities, kObjectiveTailFraction);
  result.tail_score = lowerTailMean(scores, kObjectiveTailFraction);
  result.tail_moves = lowerTailMean(moves, kObjectiveTailFraction);
  result.objective = kObjectiveMeanWeight * result.mean_utility +
                     kObjectiveTailWeight * result.tail_utility;
  return result;
}

class NormalRandom {
 public:
  explicit NormalRandom(std::uint32_t seed) : random_(seed) {}

  double next() {
    if (has_spare_) {
      has_spare_ = false;
      return spare_;
    }
    const double first = std::max(1.0e-12, random_.nextUnit());
    const double second = random_.nextUnit();
    const double radius = std::sqrt(-2.0 * std::log(first));
    const double angle = 2.0 * std::acos(-1.0) * second;
    spare_ = radius * std::sin(angle);
    has_spare_ = true;
    return radius * std::cos(angle);
  }

 private:
  Mulberry32 random_;
  bool has_spare_ = false;
  double spare_ = 0.0;
};

std::vector<Vector> population(const Vector& mean, const Vector& sigma,
                               NormalRandom& random) {
  std::vector<Vector> result;
  result.reserve(kPopulation);
  result.push_back(clamped(mean));
  result.push_back(Vector{});  // Confirmed fair baseline, every generation.
  while (static_cast<int>(result.size()) < kPopulation) {
    Vector positive = mean;
    Vector negative = mean;
    for (int coefficient = 0; coefficient < kCoefficientCount;
         ++coefficient) {
      const double perturbation = sigma[coefficient] * random.next();
      positive[coefficient] += perturbation;
      negative[coefficient] -= perturbation;
    }
    result.push_back(clamped(positive));
    result.push_back(clamped(negative));
  }
  return result;
}

std::vector<Evaluation> evaluatePopulation(
    const std::vector<Vector>& candidates, std::uint32_t seed_start,
    int games) {
  std::vector<Evaluation> result(candidates.size());
  std::atomic<int> next{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0;
       worker < std::min<int>(kParallelism, candidates.size()); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int candidate = next.fetch_add(1);
        if (candidate >= static_cast<int>(candidates.size())) return;
        result[static_cast<std::size_t>(candidate)] = evaluateVector(
            candidates[static_cast<std::size_t>(candidate)], seed_start,
            games);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

std::vector<int> ranked(const std::vector<Evaluation>& evaluations) {
  std::vector<int> result(evaluations.size());
  std::iota(result.begin(), result.end(), 0);
  std::stable_sort(result.begin(), result.end(), [&](int first, int second) {
    const Evaluation& lhs = evaluations[static_cast<std::size_t>(first)];
    const Evaluation& rhs = evaluations[static_cast<std::size_t>(second)];
    if (lhs.objective != rhs.objective) return lhs.objective > rhs.objective;
    if (lhs.tail_moves != rhs.tail_moves) return lhs.tail_moves > rhs.tail_moves;
    return lhs.mean_score > rhs.mean_score;
  });
  return result;
}

struct GenerationReport {
  int generation = 0;
  std::uint32_t seed_start = 0;
  Vector mean{};
  Vector sigma{};
  Vector winner{};
  double best_objective = 0.0;
  double population_mean_objective = 0.0;
  double best_mean_score = 0.0;
  double best_mean_moves = 0.0;
  double best_tail_score = 0.0;
  double best_tail_moves = 0.0;
  int best_censored = 0;
};

struct OptimizationResult {
  std::vector<GenerationReport> generations;
  std::vector<Vector> finalists;
  std::vector<Evaluation> tournament;
  Vector champion{};
  Evaluation champion_fitting;
  int candidate_games = 0;
};

bool sameVector(const Vector& first, const Vector& second) {
  return first == second;
}

void appendUnique(std::vector<Vector>& vectors, const Vector& candidate) {
  const bool present = std::any_of(
      vectors.begin(), vectors.end(), [&](const Vector& prior) {
        return sameVector(prior, candidate);
      });
  if (!present) vectors.push_back(candidate);
}

OptimizationResult optimize() {
  OptimizationResult result;
  Vector mean{};
  Vector sigma{};
  sigma.fill(kInitialSigma);
  NormalRandom random(kOptimizerSeed);
  for (int generation = 0; generation < kGenerations; ++generation) {
    const std::uint32_t seed_start =
        kFittingStart +
        static_cast<std::uint32_t>(generation * kGamesPerGeneration);
    const std::vector<Vector> candidates = population(mean, sigma, random);
    const std::vector<Evaluation> evaluations =
        evaluatePopulation(candidates, seed_start, kGamesPerGeneration);
    result.candidate_games += kPopulation * kGamesPerGeneration;
    const std::vector<int> ranking = ranked(evaluations);
    Vector elite_mean{};
    double rank_total = 0.0;
    for (int rank = 0; rank < kElite; ++rank) {
      const double weight = std::log(kElite + 0.5) - std::log(rank + 1.0);
      rank_total += weight;
      const Vector& elite =
          candidates[static_cast<std::size_t>(ranking[rank])];
      for (int coefficient = 0; coefficient < kCoefficientCount;
           ++coefficient) {
        elite_mean[coefficient] += weight * elite[coefficient];
      }
    }
    for (double& value : elite_mean) value /= rank_total;
    Vector elite_sigma{};
    for (int rank = 0; rank < kElite; ++rank) {
      const double weight = std::log(kElite + 0.5) - std::log(rank + 1.0);
      const Vector& elite =
          candidates[static_cast<std::size_t>(ranking[rank])];
      for (int coefficient = 0; coefficient < kCoefficientCount;
           ++coefficient) {
        const double difference = elite[coefficient] - elite_mean[coefficient];
        elite_sigma[coefficient] += weight * difference * difference;
      }
    }
    for (int coefficient = 0; coefficient < kCoefficientCount;
         ++coefficient) {
      elite_sigma[coefficient] =
          std::sqrt(elite_sigma[coefficient] / rank_total);
      mean[coefficient] = std::clamp(
          0.40 * mean[coefficient] + 0.60 * elite_mean[coefficient],
          -1.0, 1.0);
      sigma[coefficient] = std::clamp(
          0.55 * sigma[coefficient] + 0.45 * elite_sigma[coefficient],
          kMinimumSigma, kMaximumSigma);
    }
    const Evaluation& winner =
        evaluations[static_cast<std::size_t>(ranking.front())];
    appendUnique(result.finalists, winner.vector);
    const double population_mean = std::accumulate(
        evaluations.begin(), evaluations.end(), 0.0,
        [](double total, const Evaluation& evaluation) {
          return total + evaluation.objective;
        }) / evaluations.size();
    result.generations.push_back({
        generation + 1, seed_start, mean, sigma, winner.vector,
        winner.objective, population_mean, winner.mean_score,
        winner.mean_moves, winner.tail_score, winner.tail_moves,
        winner.censored,
    });
    std::cerr << "CEM generation " << generation + 1 << '/' << kGenerations
              << " best objective " << winner.objective << " score/moves "
              << winner.mean_score << '/' << winner.mean_moves
              << " tail " << winner.tail_score << '/' << winner.tail_moves
              << '\n';
  }
  appendUnique(result.finalists, mean);
  appendUnique(result.finalists, Vector{});
  result.tournament = evaluatePopulation(
      result.finalists, kTournamentStart, kTournamentGames);
  result.candidate_games +=
      static_cast<int>(result.finalists.size()) * kTournamentGames;
  const std::vector<int> tournament_ranking = ranked(result.tournament);
  result.champion = result.tournament[
      static_cast<std::size_t>(tournament_ranking.front())].vector;
  result.champion_fitting = result.tournament[
      static_cast<std::size_t>(tournament_ranking.front())];
  return result;
}

struct PairedCohort {
  std::vector<fair::GameResult> baseline;
  std::vector<fair::GameResult> candidate;
  double wall_seconds = 0.0;
};

PairedCohort runPairedCohort(const Vector& champion,
                             std::uint32_t seed_start, int games,
                             std::string_view phase) {
  if (games < 1) throw std::invalid_argument("empty paired cohort");
  const auto started = std::chrono::steady_clock::now();
  PairedCohort result;
  result.baseline.resize(static_cast<std::size_t>(games));
  result.candidate.resize(static_cast<std::size_t>(games));
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= games) return;
        const std::uint32_t seed =
            seed_start + static_cast<std::uint32_t>(game);
        result.baseline[static_cast<std::size_t>(game)] = fair::runFairGame(
            seed, std::string(phase) + "-confirmed-fair-d3");
        result.candidate[static_cast<std::size_t>(game)] = runGame(
            champion, seed, std::string(phase) + "-cem-candidate-d3");
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.wall_seconds = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started)
                            .count();
  return result;
}

fair::PairedSummary pairedSummary(const PairedCohort& cohort) {
  if (cohort.baseline.size() != cohort.candidate.size() ||
      cohort.baseline.empty()) {
    throw std::invalid_argument("invalid CEM paired cohort");
  }
  std::vector<double> scores;
  std::vector<double> moves;
  std::vector<double> cleared;
  std::vector<double> revealed;
  scores.reserve(cohort.baseline.size());
  moves.reserve(cohort.baseline.size());
  cleared.reserve(cohort.baseline.size());
  revealed.reserve(cohort.baseline.size());
  for (std::size_t game = 0; game < cohort.baseline.size(); ++game) {
    scores.push_back(static_cast<double>(cohort.candidate[game].score) -
                     static_cast<double>(cohort.baseline[game].score));
    moves.push_back(static_cast<double>(cohort.candidate[game].moves) -
                    static_cast<double>(cohort.baseline[game].moves));
    cleared.push_back(
        static_cast<double>(cohort.candidate[game].numbered_cleared) -
        static_cast<double>(cohort.baseline[game].numbered_cleared));
    revealed.push_back(
        static_cast<double>(cohort.candidate[game].covers_revealed) -
        static_cast<double>(cohort.baseline[game].covers_revealed));
  }
  return {fair::differences(scores), fair::differences(moves),
          fair::differences(cleared), fair::differences(revealed)};
}

struct CohortAnalysis {
  fair::Summary baseline;
  fair::Summary candidate;
  fair::PairedSummary paired;
  double baseline_tail_score = 0.0;
  double candidate_tail_score = 0.0;
  double baseline_tail_moves = 0.0;
  double candidate_tail_moves = 0.0;
};

CohortAnalysis analyze(const PairedCohort& cohort) {
  CohortAnalysis result;
  result.baseline = fair::summarize(cohort.baseline);
  result.candidate = fair::summarize(cohort.candidate);
  result.paired = pairedSummary(cohort);
  std::vector<double> baseline_scores;
  std::vector<double> candidate_scores;
  std::vector<double> baseline_moves;
  std::vector<double> candidate_moves;
  baseline_scores.reserve(cohort.baseline.size());
  candidate_scores.reserve(cohort.baseline.size());
  baseline_moves.reserve(cohort.baseline.size());
  candidate_moves.reserve(cohort.baseline.size());
  for (std::size_t game = 0; game < cohort.baseline.size(); ++game) {
    baseline_scores.push_back(
        static_cast<double>(cohort.baseline[game].score));
    candidate_scores.push_back(
        static_cast<double>(cohort.candidate[game].score));
    baseline_moves.push_back(
        static_cast<double>(cohort.baseline[game].moves));
    candidate_moves.push_back(
        static_cast<double>(cohort.candidate[game].moves));
  }
  result.baseline_tail_score =
      lowerTailMean(baseline_scores, kObjectiveTailFraction);
  result.candidate_tail_score =
      lowerTailMean(candidate_scores, kObjectiveTailFraction);
  result.baseline_tail_moves =
      lowerTailMean(baseline_moves, kObjectiveTailFraction);
  result.candidate_tail_moves =
      lowerTailMean(candidate_moves, kObjectiveTailFraction);
  return result;
}

bool heldoutGatePassed(const CohortAnalysis& result) {
  return result.candidate.mean_score > result.baseline.mean_score &&
         result.candidate.mean_moves > result.baseline.mean_moves &&
         result.candidate.mean_score >= 1.10 * result.baseline.mean_score &&
         result.candidate_tail_score >= result.baseline_tail_score &&
         result.candidate_tail_moves >= result.baseline_tail_moves;
}

bool pairedMeansPositive(const CohortAnalysis& result) {
  return result.paired.score.mean > 0.0 && result.paired.moves.mean > 0.0;
}

void writeVector(std::ostream& output, const Vector& vector) {
  output << '[';
  for (int index = 0; index < kCoefficientCount; ++index) {
    if (index != 0) output << ',';
    output << vector[static_cast<std::size_t>(index)];
  }
  output << ']';
}

void writeNamedVector(std::ostream& output, const Vector& vector) {
  output << '{';
  for (int index = 0; index < kCoefficientCount; ++index) {
    if (index != 0) output << ',';
    output << '"' << kCoefficientNames[static_cast<std::size_t>(index)]
           << "\":" << vector[static_cast<std::size_t>(index)];
  }
  output << '}';
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

void writeGames(std::ostream& output,
                const std::vector<fair::GameResult>& games) {
  output << '[';
  for (std::size_t game = 0; game < games.size(); ++game) {
    if (game != 0) output << ',';
    fair::writeGame(output, games[game]);
  }
  output << ']';
}

void writeEvaluation(std::ostream& output, const Evaluation& evaluation,
                     bool include_games) {
  output << "{\"normalized\":";
  writeVector(output, evaluation.vector);
  output << ",\"objective\":" << evaluation.objective
         << ",\"meanUtility\":" << evaluation.mean_utility
         << ",\"tailUtility\":" << evaluation.tail_utility
         << ",\"meanScore\":" << evaluation.mean_score
         << ",\"meanMoves\":" << evaluation.mean_moves
         << ",\"tailScore\":" << evaluation.tail_score
         << ",\"tailMoves\":" << evaluation.tail_moves
         << ",\"minimumScore\":" << evaluation.minimum_score
         << ",\"minimumMoves\":" << evaluation.minimum_moves
         << ",\"censored\":" << evaluation.censored
         << ",\"work\":" << evaluation.work;
  if (include_games) {
    output << ",\"games\":";
    writeGames(output, evaluation.games);
  }
  output << '}';
}

void writePairedCohort(std::ostream& output, std::uint32_t seed_start,
                       const PairedCohort& cohort,
                       const CohortAnalysis& analysis, bool passed) {
  output << "{\"seedStart\":" << seed_start
         << ",\"games\":" << cohort.baseline.size()
         << ",\"maximumMoves\":" << kMaximumMoves
         << ",\"confirmedFair\":";
  fair::writeSummary(output, analysis.baseline);
  output << ",\"candidate\":";
  fair::writeSummary(output, analysis.candidate);
  output << ",\"paired\":";
  fair::writePaired(output, analysis.paired);
  output << ",\"lowerTail25\":{\"confirmedFairScore\":"
         << analysis.baseline_tail_score
         << ",\"candidateScore\":" << analysis.candidate_tail_score
         << ",\"confirmedFairMoves\":" << analysis.baseline_tail_moves
         << ",\"candidateMoves\":" << analysis.candidate_tail_moves
         << "},\"wallSeconds\":" << cohort.wall_seconds
         << ",\"pairsPerWallSecond\":"
         << cohort.baseline.size() / std::max(1.0e-12, cohort.wall_seconds)
         << ",\"passed\":" << (passed ? "true" : "false")
         << ",\"pairs\":[";
  for (std::size_t game = 0; game < cohort.baseline.size(); ++game) {
    if (game != 0) output << ',';
    output << "{\"seed\":" << cohort.baseline[game].seed
           << ",\"confirmedFair\":";
    fair::writeGame(output, cohort.baseline[game]);
    output << ",\"candidate\":";
    fair::writeGame(output, cohort.candidate[game]);
    output << '}';
  }
  output << "]}";
}

void writeCheckpoint(const std::string& path, const Vector& champion) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("could not open CEM checkpoint");
  constexpr std::array<char, 8> magic{{'D', '7', 'F', 'C', 'E', 'M', '1', 0}};
  constexpr std::uint32_t version = 1;
  constexpr std::uint32_t count = kCoefficientCount;
  output.write(magic.data(), static_cast<std::streamsize>(magic.size()));
  output.write(reinterpret_cast<const char*>(&version), sizeof(version));
  output.write(reinterpret_cast<const char*>(&count), sizeof(count));
  output.write(reinterpret_cast<const char*>(champion.data()),
               static_cast<std::streamsize>(sizeof(double) * champion.size()));
  const Coefficients decoded = decode(champion);
  const std::array<double, kCoefficientCount> values{{
      decoded.direct_trigger_multiplier,
      decoded.latent_release_multiplier,
      decoded.cover_debt_multiplier,
      decoded.altitude_danger_rise_multiplier,
      decoded.low_number_clog_multiplier,
      decoded.next_disc_quiet_readiness_delta,
      decoded.revealed_cover_reward,
      decoded.additional_wave_reward,
  }};
  output.write(reinterpret_cast<const char*>(values.data()),
               static_cast<std::streamsize>(sizeof(double) * values.size()));
  if (!output) throw std::runtime_error("could not write CEM checkpoint");
}

struct Options {
  std::string output = "/tmp/drop7-fair-cem-optimizer.json";
  std::string checkpoint = "/tmp/drop7-fair-cem-optimizer.bin";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing CEM optimizer option value");
    }
    const std::string_view option(argv[index]);
    if (option == "--output") {
      result.output = argv[index + 1];
    } else if (option == "--checkpoint") {
      result.checkpoint = argv[index + 1];
    } else {
      throw std::invalid_argument("unknown CEM optimizer option");
    }
  }
  return result;
}

void writeArtifact(const Options& options,
                   const OptimizationResult& optimization,
                   const PairedCohort& heldout,
                   const CohortAnalysis& heldout_analysis,
                   bool heldout_passed, const PairedCohort* screen,
                   const CohortAnalysis* screen_analysis,
                   bool screen_passed, const PairedCohort* confirmation,
                   const CohortAnalysis* confirmation_analysis,
                   bool confirmation_passed, double total_wall_seconds) {
  std::ofstream output(options.output, std::ios::trunc);
  if (!output) throw std::runtime_error("could not open CEM artifact");
  output << std::setprecision(17)
         << "{\n  \"experiment\":\"fair-cem-complete-game-optimizer\",\n"
         << "  \"preregistered\":true,\n"
         << "  \"publicStateOnly\":true,\n"
         << "  \"historicalActionPlacementPriors\":false,\n"
         << "  \"scoring\":{\"levelBonus\":7000},\n"
         << "  \"search\":{\"depth\":" << fair::kDepth
         << ",\"chanceSamples\":" << fair::kChanceSamples
         << ",\"policySeed\":" << fair::kPolicySeed
         << ",\"maximumWork\":" << fair::kMaximumWork
         << ",\"maximumCacheEntries\":" << fair::kMaximumCacheEntries
         << ",\"maximumMoves\":" << kMaximumMoves
         << ",\"parallelism\":" << kParallelism << "},\n"
         << "  \"optimizer\":{\"kind\":\"cross-entropy-method\","
         << "\"optimizerSeed\":" << kOptimizerSeed
         << ",\"generations\":" << kGenerations
         << ",\"population\":" << kPopulation
         << ",\"elite\":" << kElite
         << ",\"gamesPerGeneration\":" << kGamesPerGeneration
         << ",\"tournamentGames\":" << kTournamentGames
         << ",\"candidateGames\":" << optimization.candidate_games
         << ",\"candidateGameLimit\":3000,\"objective\":{"
         << "\"perGame\":\"moves + score / 14000\","
         << "\"meanWeight\":" << kObjectiveMeanWeight
         << ",\"lowerTail25Weight\":" << kObjectiveTailWeight
         << "}},\n"
         << "  \"groups\":{"
         << "\"directTrigger\":[\"directPotential\"],"
         << "\"latentRelease\":[\"latentChainPotential\","
            "\"crackedExposure\",\"solidExposure\"],"
         << "\"coverDebt\":[\"solidCells\",\"crackedCells\"],"
         << "\"altitudeDangerRise\":[\"heightLoad\","
            "\"coveredHeightRisk\",\"lowNumberHeightRisk\","
            "\"dangerHeightSquared\",\"risePressure\"],"
         << "\"lowNumberClog\":[\"highLowNumbers\","
            "\"adjacentOnes\",\"tripleTwos\",\"deadLowNumbers\"],"
         << "\"nextDiscQuietReadiness\":[\"nextDiscVerticalOptions\","
            "\"quietBuildOptions\",\"quietDirectGain\","
            "\"triggerReadiness\",\"riseTriggerReadiness\"],"
         << "\"transitionOnly\":[\"revealedCover\","
            "\"additionalWave\"]},\n"
         << "  \"bounds\":{\"multipliers\":[0.5,2.0],"
            "\"readinessDelta\":[-1,1],\"revealedCoverReward\":[-600,600],"
            "\"additionalWaveReward\":[-1500,1500]},\n"
         << "  \"generations\":[";
  for (std::size_t index = 0; index < optimization.generations.size();
       ++index) {
    if (index != 0) output << ',';
    const GenerationReport& generation = optimization.generations[index];
    output << "{\"generation\":" << generation.generation
           << ",\"seedStart\":" << generation.seed_start
           << ",\"mean\":";
    writeVector(output, generation.mean);
    output << ",\"sigma\":";
    writeVector(output, generation.sigma);
    output << ",\"winner\":";
    writeVector(output, generation.winner);
    output << ",\"bestObjective\":" << generation.best_objective
           << ",\"populationMeanObjective\":"
           << generation.population_mean_objective
           << ",\"bestMeanScore\":" << generation.best_mean_score
           << ",\"bestMeanMoves\":" << generation.best_mean_moves
           << ",\"bestTailScore\":" << generation.best_tail_score
           << ",\"bestTailMoves\":" << generation.best_tail_moves
           << ",\"bestCensored\":" << generation.best_censored << '}';
  }
  output << "],\n  \"tournament\":[";
  for (std::size_t index = 0; index < optimization.tournament.size();
       ++index) {
    if (index != 0) output << ',';
    writeEvaluation(output, optimization.tournament[index], true);
  }
  output << "],\n  \"champion\":{\"normalizedByName\":";
  writeNamedVector(output, optimization.champion);
  output << ",\"normalized\":";
  writeVector(output, optimization.champion);
  output << ",\"decoded\":";
  writeCoefficients(output, decode(optimization.champion));
  output << ",\"fittingTournament\":";
  writeEvaluation(output, optimization.champion_fitting, false);
  output << ",\"checkpoint\":\"" << options.checkpoint << "\"},\n"
         << "  \"heldoutGateCriteria\":{"
            "\"bothMeansImprove\":true,\"minimumScoreGainFraction\":0.10,"
            "\"noLowerTail25ScoreCollapse\":true,"
            "\"noLowerTail25MovesCollapse\":true},\n"
         << "  \"heldout\":";
  writePairedCohort(output, kHeldoutStart, heldout, heldout_analysis,
                    heldout_passed);
  output << ",\n  \"screen\":";
  if (screen == nullptr) {
    output << "null";
  } else {
    writePairedCohort(output, kScreenStart, *screen, *screen_analysis,
                      screen_passed);
  }
  output << ",\n  \"confirmation\":";
  if (confirmation == nullptr) {
    output << "null";
  } else {
    writePairedCohort(output, kConfirmationStart, *confirmation,
                      *confirmation_analysis, confirmation_passed);
  }
  output << ",\n  \"heldoutPassed\":"
         << (heldout_passed ? "true" : "false")
         << ",\n  \"screenRan\":" << (screen != nullptr ? "true" : "false")
         << ",\n  \"screenPassed\":" << (screen_passed ? "true" : "false")
         << ",\n  \"confirmationRan\":"
         << (confirmation != nullptr ? "true" : "false")
         << ",\n  \"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\n  \"qualified\":"
         << (heldout_passed && screen_passed && confirmation_passed ? "true"
                                                                          : "false")
         << ",\n  \"totalWallSeconds\":" << total_wall_seconds
         << ",\n  \"peakRssBytes\":" << fair::peakRssBytes() << "\n}\n";
  if (!output) throw std::runtime_error("could not write CEM artifact");
}

bool nearlyEqual(double first, double second, double tolerance = 1.0e-9) {
  if (std::isinf(first) || std::isinf(second)) return first == second;
  return std::abs(first - second) <= tolerance;
}

bool selfTest(std::ostream& output) {
  std::ostringstream fair_output;
  const bool inherited = fair::selfTest(fair_output);
  const Vector zero{};
  const Coefficients baseline_coefficients = decode(zero);
  bool zero_leaf_parity = true;
  bool zero_search_parity = true;
  for (const fair::ParityFixture& fixture : fair::kTypeScriptFixtures) {
    const State state = fair::fixtureState(fixture);
    zero_leaf_parity = zero_leaf_parity &&
                       parameterizedLeaf(state, baseline_coefficients) ==
                           fair::fairLeaf(state);
    const SearchDecision candidate = chooseAction(state, zero);
    const fair::SearchDecision baseline = fair::chooseFairAction(state);
    zero_search_parity =
        zero_search_parity && candidate.action == baseline.action &&
        candidate.completed_depth == baseline.completed_depth &&
        candidate.complete == baseline.complete &&
        candidate.work == baseline.work && candidate.nodes == baseline.nodes &&
        candidate.cache_hits == baseline.cache_hits &&
        candidate.cache_entries == baseline.cache_entries;
    for (int column = 0; column < kBoardSize; ++column) {
      zero_search_parity =
          zero_search_parity &&
          nearlyEqual(candidate.root_values[column],
                      baseline.root_values[column], 1.0e-10);
    }
  }

  Vector probe{{0.20, -0.15, 0.30, -0.10, 0.25, 0.35, -0.20, 0.40}};
  const State source = fair::fixtureState(fair::kTypeScriptFixtures[1]);
  State reflected = source;
  reflected.board = cfpi::detail::mirrorBoard(source.board);
  const SearchDecision source_decision = chooseAction(source, probe);
  const SearchDecision reflected_decision = chooseAction(reflected, probe);
  const bool reflection_safe =
      nearlyEqual(parameterizedLeaf(source, decode(probe)),
                  parameterizedLeaf(reflected, decode(probe))) &&
      reflected_decision.action == kBoardSize - 1 - source_decision.action;
  State metadata = source;
  metadata.score = 9'876'543;
  metadata.level = 77;
  metadata.moves_played = 543;
  const SearchDecision metadata_decision = chooseAction(metadata, probe);
  const bool public_only =
      parameterizedLeaf(source, decode(probe)) ==
          parameterizedLeaf(metadata, decode(probe)) &&
      metadata_decision.action == source_decision.action &&
      metadata_decision.work == source_decision.work;

  MoveResult synthetic;
  synthetic.waves = {{1, 2, 3, 0}, {2, 4, 5, 0}, {3, 1, 7, 0}};
  Coefficients transition_coefficients;
  transition_coefficients.revealed_cover_reward = 11.0;
  transition_coefficients.additional_wave_reward = 101.0;
  const bool transition_exact =
      transitionBonus(synthetic, transition_coefficients) ==
      15.0 * 11.0 + 2.0 * 101.0;

  Vector mean{};
  Vector sigma{};
  sigma.fill(0.10);
  NormalRandom first_random(kOptimizerSeed);
  NormalRandom second_random(kOptimizerSeed);
  const std::vector<Vector> first_population =
      population(mean, sigma, first_random);
  const std::vector<Vector> second_population =
      population(mean, sigma, second_random);
  bool antithetic = first_population == second_population &&
                    first_population.size() == kPopulation &&
                    first_population[0] == mean &&
                    first_population[1] == Vector{};
  for (int candidate = 2; candidate < kPopulation; candidate += 2) {
    for (int coefficient = 0; coefficient < kCoefficientCount;
         ++coefficient) {
      antithetic = antithetic && nearlyEqual(
          first_population[static_cast<std::size_t>(candidate)][coefficient],
          -first_population[static_cast<std::size_t>(candidate + 1)]
                              [coefficient]);
    }
  }
  const bool tail_exact = nearlyEqual(
      lowerTailMean({1.0, 2.0, 10.0, 20.0, 30.0, 40.0}, 0.25),
      4.0 / 3.0);
  Vector lower{};
  Vector upper{};
  lower.fill(-1.0);
  upper.fill(1.0);
  const Coefficients lower_decoded = decode(lower);
  const Coefficients upper_decoded = decode(upper);
  const bool bounds_exact =
      nearlyEqual(lower_decoded.direct_trigger_multiplier, 0.5) &&
      nearlyEqual(upper_decoded.direct_trigger_multiplier, 2.0) &&
      lower_decoded.revealed_cover_reward == -600.0 &&
      upper_decoded.revealed_cover_reward == 600.0 &&
      lower_decoded.additional_wave_reward == -1'500.0 &&
      upper_decoded.additional_wave_reward == 1'500.0;
  const bool protocol =
      kLevelBonus == 7'000 && fair::kDepth == 3 &&
      fair::kChanceSamples == 5 && kHeldoutGames >= 32 &&
      kMaximumMoves == 1'000 &&
      kGenerations * kPopulation * kGamesPerGeneration +
              (kGenerations + 2) * kTournamentGames <=
          3'000 &&
      kFittingStart == 0x3dc0'0000u &&
      kHeldoutStart == 0x3dc1'0000u && kScreenStart == 0x3ea3'0000u &&
      kConfirmationStart == 0x3ea4'0000u;
  const bool action_priors_absent = reflection_safe && transition_exact;
  const bool passed = inherited && zero_leaf_parity && zero_search_parity &&
                      reflection_safe && public_only && transition_exact &&
                      antithetic && tail_exact && bounds_exact && protocol &&
                      action_priors_absent;
  output << std::boolalpha << std::setprecision(12)
         << "FAIR_CEM_OPTIMIZER_SELF_TEST {\"passed\":" << passed
         << ",\"inheritedFairSelfTest\":" << inherited
         << ",\"zeroLeafParity\":" << zero_leaf_parity
         << ",\"zeroSearchParity\":" << zero_search_parity
         << ",\"reflectionSafe\":" << reflection_safe
         << ",\"publicStateOnly\":" << public_only
         << ",\"transitionRewardsExact\":" << transition_exact
         << ",\"actionPlacementPriorsAbsent\":" << action_priors_absent
         << ",\"deterministicAntitheticPopulation\":" << antithetic
         << ",\"fractionalTailExact\":" << tail_exact
         << ",\"boundsExact\":" << bounds_exact
         << ",\"fixedProtocol\":" << protocol
         << ",\"levelBonus\":" << kLevelBonus << "}\n";
  return passed;
}

int run(const Options& options, std::ostream& output) {
  const auto started = std::chrono::steady_clock::now();
  const OptimizationResult optimization = optimize();
  writeCheckpoint(options.checkpoint, optimization.champion);

  const PairedCohort heldout = runPairedCohort(
      optimization.champion, kHeldoutStart, kHeldoutGames, "heldout");
  const CohortAnalysis heldout_analysis = analyze(heldout);
  const bool heldout_passed = heldoutGatePassed(heldout_analysis);

  PairedCohort screen;
  CohortAnalysis screen_analysis;
  bool screen_passed = false;
  if (heldout_passed) {
    screen = runPairedCohort(optimization.champion, kScreenStart,
                             kScreenGames, "screen");
    screen_analysis = analyze(screen);
    screen_passed = pairedMeansPositive(screen_analysis);
  }

  PairedCohort confirmation;
  CohortAnalysis confirmation_analysis;
  bool confirmation_passed = false;
  if (screen_passed) {
    confirmation = runPairedCohort(optimization.champion,
                                    kConfirmationStart,
                                    kConfirmationGames, "confirmation");
    confirmation_analysis = analyze(confirmation);
    confirmation_passed = pairedMeansPositive(confirmation_analysis);
  }

  const double total_wall_seconds = std::chrono::duration<double>(
                                          std::chrono::steady_clock::now() -
                                          started)
                                          .count();
  writeArtifact(options, optimization, heldout, heldout_analysis,
                heldout_passed, heldout_passed ? &screen : nullptr,
                heldout_passed ? &screen_analysis : nullptr, screen_passed,
                screen_passed ? &confirmation : nullptr,
                screen_passed ? &confirmation_analysis : nullptr,
                confirmation_passed, total_wall_seconds);
  output << std::fixed << std::setprecision(3)
         << "FAIR_CEM_OPTIMIZER_RESULT {\"championNormalized\":";
  writeVector(output, optimization.champion);
  output << ",\"heldoutFairScore\":"
         << heldout_analysis.baseline.mean_score
         << ",\"heldoutCandidateScore\":"
         << heldout_analysis.candidate.mean_score
         << ",\"heldoutScoreDelta\":" << heldout_analysis.paired.score.mean
         << ",\"heldoutFairMoves\":"
         << heldout_analysis.baseline.mean_moves
         << ",\"heldoutCandidateMoves\":"
         << heldout_analysis.candidate.mean_moves
         << ",\"heldoutMoveDelta\":" << heldout_analysis.paired.moves.mean
         << ",\"heldoutPassed\":"
         << (heldout_passed ? "true" : "false")
         << ",\"screenRan\":" << (heldout_passed ? "true" : "false")
         << ",\"screenPassed\":" << (screen_passed ? "true" : "false")
         << ",\"confirmationRan\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\"candidateGames\":" << optimization.candidate_games
         << ",\"peakRssBytes\":" << fair::peakRssBytes()
         << ",\"totalWallSeconds\":" << total_wall_seconds
         << ",\"artifact\":\"" << options.output
         << "\",\"checkpoint\":\"" << options.checkpoint << "\"}\n";
  return 0;
}

}  // namespace drop7::fair_cem_optimizer

int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::fair_cem_optimizer::selfTest(std::cout) ? EXIT_SUCCESS
                                                            : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::fair_cem_optimizer::parseOptions(argc, argv, 2);
      return drop7::fair_cem_optimizer::run(options, std::cout);
    }
    std::cerr << "usage: drop7_fair_cem_optimizer --self-test | --run "
                 "[--output PATH] [--checkpoint PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
