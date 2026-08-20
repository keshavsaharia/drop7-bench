#include "../../../src/core/native/public-behavior.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <future>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <list>
#include <numeric>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;
using drop7::MoveResult;
using drop7::State;

constexpr int kGroupCount = 7;
constexpr int kGenerations = 6;
constexpr int kPopulation = 14;
constexpr int kPairs = kPopulation / 2;
constexpr int kElite = 4;
constexpr int kGamesPerGeneration = 3;
constexpr int kScreenGames = 8;
constexpr int kConfirmationGames = 8;
constexpr int kMaximumMoves = 500;
constexpr double kLogBound = 0.6931471805599453;  // [0.5, 2.0].
constexpr double kInitialSigma = 0.22;
constexpr double kMinimumSigma = 0.04;
constexpr double kMaximumSigma = 0.50;
constexpr std::uint32_t kEvolutionSeedStart = 0x3d70'5000u;
constexpr std::uint32_t kScreenSeedStart = 0x3d70'5100u;
constexpr std::uint32_t kConfirmationSeedStart = 0x3d70'5200u;
constexpr std::uint32_t kOptimizerSeed = 0xa11c'e55eu;

constexpr std::array<std::string_view, kGroupCount> kGroupNames{{
    "boardLoad",
    "occupancyHeightRisk",
    "coverDebt",
    "quietBuildup",
    "releaseExposure",
    "lowNumberClog",
    "triggerRise",
}};

using Vector = std::array<double, kGroupCount>;

struct Options {
  drop7::cfpi::BehaviorOptions behavior;
};

struct SearchMetrics {
  int completed_depth = 0;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
};

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool terminal = false;
  std::uint64_t search_work = 0;
};

struct Evaluation {
  Vector log_multipliers{};
  std::vector<GameResult> games;
  double objective = -std::numeric_limits<double>::infinity();
  double mean_score = 0;
  double mean_moves = 0;
  double lower_half_log_score = 0;
  double lower_half_moves = 0;
  std::int64_t minimum_score = 0;
  int minimum_moves = 0;
  int censored = 0;
  std::uint64_t search_work = 0;
};

struct GenerationReport {
  int generation = 0;
  std::uint32_t seed_start = 0;
  double best_objective = 0;
  double mean_objective = 0;
  double best_mean_score = 0;
  double best_mean_moves = 0;
  Vector mean{};
  Vector sigma{};
};

struct PairedResult {
  Evaluation baseline;
  Evaluation candidate;
  double mean_score_difference = 0;
  double mean_move_difference = 0;
};

double parameterizedPotential(const State& state,
                              const Vector& log_multipliers) {
  if (state.game_over) return -250'000.0;
  const drop7::cfpi::detail::PhaseFeatures f =
      drop7::cfpi::detail::extractPhaseFeatures(state);
  std::array<double, kGroupCount> groups{};
  groups[0] =
      180.0 * f.open_columns - 10.0 * f.height_load -
      18.0 * f.numbered_cells;
  groups[1] =
      -240.0 * f.projected_occupancy_debt -
      1800.0 * f.peak_height_risk;
  groups[2] =
      -620.0 * f.solid_cells - 220.0 * f.cracked_cells -
      200.0 * f.residual_cover_debt -
      50.0 * f.cover_altitude_debt -
      70.0 * f.imminent_cover_altitude_debt;
  groups[3] =
      360.0 * f.direct_potential +
      300.0 * f.quiet_build_options +
      600.0 * f.quiet_direct_gain;
  groups[4] =
      800.0 * f.latent_chain_potential +
      540.0 * f.cracked_exposure +
      194.0 * f.solid_exposure;
  groups[5] =
      -90.0 * f.high_low_numbers -
      550.0 * f.adjacent_ones -
      750.0 * f.triple_twos -
      120.0 * f.dead_low_numbers -
      120.0 * f.low_cap_load -
      180.0 * f.adjacent_low_cap_load;
  groups[6] =
      600.0 * f.trigger_readiness +
      1200.0 * f.rise_trigger_readiness;

  double total = 0;
  for (int group = 0; group < kGroupCount; ++group) {
    total += groups[group] * std::exp(log_multipliers[group]);
  }
  return total;
}

class WorkLimitReached : public std::exception {};

struct CacheEntry {
  double value = 0;
  std::list<std::string>::iterator order;
};

struct SearchContext {
  SearchContext(const Options& run_options, const Vector& weights)
      : options(run_options), log_multipliers(weights) {}

  const Options& options;
  const Vector& log_multipliers;
  std::unordered_map<std::string, CacheEntry> cache;
  std::list<std::string> order;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
};

void checkBudget(const SearchContext& context) {
  if (context.work >= context.options.behavior.max_work) {
    throw WorkLimitReached{};
  }
}

void setCachedValue(SearchContext& context, std::string key, double value) {
  const auto prior = context.cache.find(key);
  if (prior != context.cache.end()) {
    context.order.erase(prior->second.order);
    context.cache.erase(prior);
  }
  while (context.cache.size() >=
         context.options.behavior.max_cache_entries) {
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

double evaluateAction(const State& state, int column, int depth,
                      SearchContext& context) {
  const int samples = context.options.behavior.chance_samples;
  const std::uint32_t state_seed =
      drop7::cfpi::detail::scenarioSeedForState(
          state, context.options.behavior.policy_seed, depth);
  double total = 0;
  for (int sample = 0; sample < samples; ++sample) {
    checkBudget(context);
    drop7::cfpi::detail::StratifiedRandom random{
        state_seed, sample, samples, 0,
    };
    MoveResult move;
    if (!drop7::cfpi::detail::playMoveSampled(
            state, column, random, move)) {
      total += context.options.behavior.terminal_utility;
      continue;
    }
    ++context.work;
    const double score_delta = static_cast<double>(move.score_delta);
    if (move.state.game_over) {
      total += score_delta + context.options.behavior.terminal_utility;
      continue;
    }
    move.state.score = 0;
    move.state.next_disc = drop7::cfpi::detail::sampledNextDisc(
        state_seed, sample, samples);
    bool ignored = false;
    const State next =
        drop7::cfpi::detail::canonicalState(move.state, ignored);
    total += score_delta + bestFutureValue(next, depth - 1, context);
  }
  return total / static_cast<double>(samples);
}

double evaluateLeaf(const State& state, SearchContext& context) {
  checkBudget(context);
  ++context.work;
  const double value =
      parameterizedPotential(state, context.log_multipliers);
  if (!std::isfinite(value)) {
    throw std::runtime_error(
        "phase-weight evaluator returned non-finite value");
  }
  return value;
}

double bestFutureValue(const State& state, int depth,
                       SearchContext& context) {
  ++context.nodes;
  checkBudget(context);
  if (state.game_over) {
    return context.options.behavior.terminal_utility;
  }
  if (depth == 0) return evaluateLeaf(state, context);
  const std::string key =
      drop7::cfpi::detail::dynamicStateKey(state, depth);
  const auto cached = context.cache.find(key);
  if (cached != context.cache.end()) {
    ++context.cache_hits;
    const double value = cached->second.value;
    context.order.splice(context.order.end(), context.order,
                         cached->second.order);
    return value;
  }
  double best = -std::numeric_limits<double>::infinity();
  for (int column : drop7::cfpi::detail::kColumnOrder) {
    if (!drop7::isLegal(state.board, column)) continue;
    best = std::max(best, evaluateAction(state, column, depth, context));
  }
  if (!std::isfinite(best)) {
    best = context.options.behavior.terminal_utility;
  }
  setCachedValue(context, key, best);
  return best;
}

std::pair<int, double> bestRootAction(
    const State& state, int depth, SearchContext& context) {
  int best_column = -1;
  double best_value = -std::numeric_limits<double>::infinity();
  for (int column : drop7::cfpi::detail::kColumnOrder) {
    if (!drop7::isLegal(state.board, column)) continue;
    const double value = evaluateAction(state, column, depth, context);
    if (value > best_value) {
      best_value = value;
      best_column = column;
    }
  }
  return {best_column, best_value};
}

int chooseAction(const State& input, const Vector& log_multipliers,
                 const Options& options,
                 SearchMetrics* metrics = nullptr) {
  if (input.game_over) return -1;
  bool mirrored = false;
  const State canonical =
      drop7::cfpi::detail::canonicalState(input, mirrored);
  SearchContext context(options, log_multipliers);
  int completed_column = -1;
  int completed_depth = 0;
  for (int depth = 1; depth <= options.behavior.max_depth; ++depth) {
    try {
      const auto [column, value] =
          bestRootAction(canonical, depth, context);
      (void)value;
      if (column < 0) break;
      completed_column = column;
      completed_depth = depth;
    } catch (const WorkLimitReached&) {
      break;
    }
  }
  if (completed_column < 0) {
    completed_column = drop7::centerFirstMove(canonical.board);
  }
  if (metrics != nullptr) {
    metrics->completed_depth = completed_depth;
    metrics->work = context.work;
    metrics->nodes = context.nodes;
    metrics->cache_hits = context.cache_hits;
  }
  return mirrored && completed_column >= 0
             ? drop7::kBoardSize - 1 - completed_column
             : completed_column;
}

GameResult runGame(std::uint32_t seed, const Vector& log_multipliers,
                   const Options& options) {
  State state = drop7::initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    SearchMetrics metrics;
    const int action =
        chooseAction(state, log_multipliers, options, &metrics);
    result.search_work += metrics.work;
    MoveResult move;
    if (!drop7::playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error(
          "phase-weight policy selected illegal action");
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.terminal = state.game_over;
  return result;
}

double lowerHalfMean(std::vector<double> values) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  const std::size_t count = (values.size() + 1) / 2;
  return std::accumulate(values.begin(), values.begin() + count, 0.0) /
         static_cast<double>(count);
}

Evaluation evaluateVector(const Vector& log_multipliers,
                          std::uint32_t seed_start, int games,
                          const Options& options) {
  Evaluation result;
  result.log_multipliers = log_multipliers;
  result.minimum_score = std::numeric_limits<std::int64_t>::max();
  result.minimum_moves = std::numeric_limits<int>::max();
  std::vector<double> log_scores;
  std::vector<double> moves;
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed =
        seed_start + static_cast<std::uint32_t>(game);
    GameResult outcome = runGame(seed, log_multipliers, options);
    result.mean_score += static_cast<double>(outcome.score);
    result.mean_moves += outcome.moves;
    result.minimum_score =
        std::min(result.minimum_score, outcome.score);
    result.minimum_moves =
        std::min(result.minimum_moves, outcome.moves);
    result.search_work += outcome.search_work;
    if (!outcome.terminal) ++result.censored;
    log_scores.push_back(
        std::log1p(std::max<std::int64_t>(0, outcome.score)));
    moves.push_back(static_cast<double>(outcome.moves));
    result.games.push_back(outcome);
  }
  result.mean_score /= games;
  result.mean_moves /= games;
  const double mean_log_score =
      std::accumulate(log_scores.begin(), log_scores.end(), 0.0) / games;
  result.lower_half_log_score = lowerHalfMean(log_scores);
  result.lower_half_moves = lowerHalfMean(moves);
  // Survival dominates; logarithmic score and lower-half terms prevent one
  // extraordinary chain from winning an optimizer generation.
  result.objective =
      0.50 * result.mean_moves +
      0.25 * result.lower_half_moves +
      4.0 * mean_log_score +
      2.0 * result.lower_half_log_score;
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
    const double first = std::max(1e-12, random_.nextUnit());
    const double second = random_.nextUnit();
    const double radius = std::sqrt(-2.0 * std::log(first));
    const double angle = 2.0 * std::acos(-1.0) * second;
    spare_ = radius * std::sin(angle);
    has_spare_ = true;
    return radius * std::cos(angle);
  }

 private:
  drop7::Mulberry32 random_;
  bool has_spare_ = false;
  double spare_ = 0;
};

Vector clamped(Vector vector) {
  for (double& value : vector) {
    value = std::clamp(value, -kLogBound, kLogBound);
  }
  return vector;
}

std::vector<Vector> antitheticPopulation(
    const Vector& mean, const Vector& sigma, NormalRandom& random) {
  std::vector<Vector> population;
  population.reserve(kPopulation);
  for (int pair = 0; pair < kPairs; ++pair) {
    Vector positive = mean;
    Vector negative = mean;
    for (int group = 0; group < kGroupCount; ++group) {
      const double perturbation = sigma[group] * random.next();
      positive[group] += perturbation;
      negative[group] -= perturbation;
    }
    population.push_back(clamped(positive));
    population.push_back(clamped(negative));
  }
  return population;
}

double maximumResidentMiB() {
#if defined(__APPLE__) || defined(__linux__)
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(__APPLE__)
    return usage.ru_maxrss / (1024.0 * 1024.0);
#else
    return usage.ru_maxrss / 1024.0;
#endif
  }
#endif
  return 0;
}

PairedResult pairedEvaluation(const Vector& candidate,
                              std::uint32_t seed_start, int games,
                              const Options& options) {
  Vector baseline{};
  auto baseline_future = std::async(std::launch::async, [&] {
    return evaluateVector(baseline, seed_start, games, options);
  });
  auto candidate_future = std::async(std::launch::async, [&] {
    return evaluateVector(candidate, seed_start, games, options);
  });
  PairedResult result;
  result.baseline = baseline_future.get();
  result.candidate = candidate_future.get();
  for (int game = 0; game < games; ++game) {
    result.mean_score_difference += static_cast<double>(
        result.candidate.games[game].score -
        result.baseline.games[game].score);
    result.mean_move_difference +=
        result.candidate.games[game].moves -
        result.baseline.games[game].moves;
  }
  result.mean_score_difference /= games;
  result.mean_move_difference /= games;
  return result;
}

void printVector(const Vector& vector, bool exponentiate) {
  std::cout << '{';
  for (int group = 0; group < kGroupCount; ++group) {
    if (group > 0) std::cout << ',';
    std::cout << '\"' << kGroupNames[group] << "\":"
              << (exponentiate ? std::exp(vector[group]) : vector[group]);
  }
  std::cout << '}';
}

void printEvaluation(const Evaluation& evaluation) {
  std::cout << "{\"objective\":" << evaluation.objective
            << ",\"mean_score\":" << evaluation.mean_score
            << ",\"mean_moves\":" << evaluation.mean_moves
            << ",\"lower_half_log_score\":"
            << evaluation.lower_half_log_score
            << ",\"lower_half_moves\":"
            << evaluation.lower_half_moves
            << ",\"minimum_score\":" << evaluation.minimum_score
            << ",\"minimum_moves\":" << evaluation.minimum_moves
            << ",\"censored\":" << evaluation.censored
            << ",\"search_work\":" << evaluation.search_work
            << ",\"scores\":[";
  for (std::size_t index = 0; index < evaluation.games.size(); ++index) {
    if (index > 0) std::cout << ',';
    std::cout << evaluation.games[index].score;
  }
  std::cout << "],\"moves\":[";
  for (std::size_t index = 0; index < evaluation.games.size(); ++index) {
    if (index > 0) std::cout << ',';
    std::cout << evaluation.games[index].moves;
  }
  std::cout << "],\"multipliers\":";
  printVector(evaluation.log_multipliers, true);
  std::cout << '}';
}

bool selfTest(std::ostream& output) {
  Options quick;
  quick.behavior.max_depth = 2;
  quick.behavior.chance_samples = 3;
  quick.behavior.max_work = 100'000;
  quick.behavior.max_cache_entries = 4'000;
  Vector baseline{};
  State state;
  state.board = drop7::initialBoard();
  state.board[drop7::indexOf(5, 0)] = 3;
  state.board[drop7::indexOf(5, 1)] = 5;
  state.board[drop7::indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;
  const bool potential_exact =
      std::abs(parameterizedPotential(state, baseline) -
               drop7::cfpi::phasePotential(state)) <= 1e-9;
  const int first = chooseAction(state, baseline, quick);
  const int second = chooseAction(state, baseline, quick);
  const int verified =
      drop7::cfpi::chooseBehaviorAction(state, quick.behavior);
  const bool action_exact = first == verified;
  const bool deterministic = first == second;
  const bool legal = drop7::isLegal(state.board, first);

  State mirrored = state;
  mirrored.board =
      drop7::cfpi::detail::mirrorBoard(state.board);
  const int reflected = chooseAction(mirrored, baseline, quick);
  const bool mirror_safe =
      reflected == drop7::kBoardSize - 1 - first;

  Vector mean{};
  Vector sigma{};
  sigma.fill(0.05);
  NormalRandom first_random(kOptimizerSeed);
  NormalRandom second_random(kOptimizerSeed);
  const auto first_population =
      antitheticPopulation(mean, sigma, first_random);
  const auto second_population =
      antitheticPopulation(mean, sigma, second_random);
  const bool optimizer_deterministic =
      first_population == second_population &&
      first_population.size() == kPopulation;
  bool antithetic = true;
  for (int pair = 0; pair < kPairs; ++pair) {
    for (int group = 0; group < kGroupCount; ++group) {
      antithetic =
          antithetic &&
          std::abs(first_population[2 * pair][group] +
                   first_population[2 * pair + 1][group]) <= 1e-12;
    }
  }
  const bool passed =
      potential_exact && action_exact && deterministic && legal &&
      mirror_safe && optimizer_deterministic && antithetic;
  output << "{\"potential_exact\":"
         << (potential_exact ? "true" : "false")
         << ",\"action_exact\":"
         << (action_exact ? "true" : "false")
         << ",\"deterministic\":"
         << (deterministic ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false")
         << ",\"mirror_safe\":"
         << (mirror_safe ? "true" : "false")
         << ",\"optimizer_deterministic\":"
         << (optimizer_deterministic ? "true" : "false")
         << ",\"antithetic\":"
         << (antithetic ? "true" : "false")
         << ",\"passed\":" << (passed ? "true" : "false")
         << "}\n";
  return passed;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    for (int index = 1; index < argc; ++index) {
      if (std::string_view(argv[index]) == "--self-test") {
        return selfTest(std::cout) ? 0 : 1;
      }
      throw std::invalid_argument("only --self-test is supported");
    }
    Options options;
    const auto started = Clock::now();
    Vector mean{};
    Vector sigma{};
    sigma.fill(kInitialSigma);
    NormalRandom random(kOptimizerSeed);
    std::vector<GenerationReport> reports;
    for (int generation = 0; generation < kGenerations; ++generation) {
      const std::uint32_t seed_start =
          kEvolutionSeedStart +
          static_cast<std::uint32_t>(generation * kGamesPerGeneration);
      const std::vector<Vector> population =
          antitheticPopulation(mean, sigma, random);
      std::vector<std::future<Evaluation>> pending;
      pending.reserve(population.size());
      for (const Vector& candidate : population) {
        pending.push_back(std::async(
            std::launch::async, [&, candidate, seed_start] {
              return evaluateVector(candidate, seed_start,
                                    kGamesPerGeneration, options);
            }));
      }
      std::vector<Evaluation> evaluations;
      evaluations.reserve(population.size());
      for (auto& future : pending) evaluations.push_back(future.get());
      std::vector<int> ranking(kPopulation);
      std::iota(ranking.begin(), ranking.end(), 0);
      std::stable_sort(
          ranking.begin(), ranking.end(), [&](int first, int second) {
            return evaluations[first].objective >
                   evaluations[second].objective;
          });

      Vector elite_mean{};
      for (int rank = 0; rank < kElite; ++rank) {
        const Vector& candidate =
            population[static_cast<std::size_t>(ranking[rank])];
        for (int group = 0; group < kGroupCount; ++group) {
          elite_mean[group] += candidate[group] / kElite;
        }
      }
      Vector elite_sigma{};
      for (int rank = 0; rank < kElite; ++rank) {
        const Vector& candidate =
            population[static_cast<std::size_t>(ranking[rank])];
        for (int group = 0; group < kGroupCount; ++group) {
          const double difference =
              candidate[group] - elite_mean[group];
          elite_sigma[group] +=
              difference * difference / kElite;
        }
      }
      for (int group = 0; group < kGroupCount; ++group) {
        elite_sigma[group] = std::sqrt(elite_sigma[group]);
        mean[group] = std::clamp(
            0.40 * mean[group] + 0.60 * elite_mean[group],
            -kLogBound, kLogBound);
        sigma[group] = std::clamp(
            0.55 * sigma[group] + 0.45 * elite_sigma[group],
            kMinimumSigma, kMaximumSigma);
      }
      const double mean_objective = std::accumulate(
          evaluations.begin(), evaluations.end(), 0.0,
          [](double total, const Evaluation& evaluation) {
            return total + evaluation.objective;
          }) / kPopulation;
      const Evaluation& best =
          evaluations[static_cast<std::size_t>(ranking.front())];
      reports.push_back({
          generation + 1,
          seed_start,
          best.objective,
          mean_objective,
          best.mean_score,
          best.mean_moves,
          mean,
          sigma,
      });
    }

    const PairedResult screen = pairedEvaluation(
        mean, kScreenSeedStart, kScreenGames, options);
    const bool screen_pass =
        screen.mean_score_difference > 0 &&
        screen.mean_move_difference > 0;
    PairedResult confirmation;
    bool confirmed = false;
    if (screen_pass) {
      confirmation = pairedEvaluation(
          mean, kConfirmationSeedStart,
          kConfirmationGames, options);
      confirmed =
          confirmation.mean_score_difference > 0 &&
          confirmation.mean_move_difference > 0;
    }
    const double elapsed_seconds = std::chrono::duration<double>(
        Clock::now() - started).count();
    std::cout << std::fixed << std::setprecision(6)
              << "{\"mode\":\"phase-weight-evolution\""
              << ",\"evolution_seed_start\":\"0x3d705000\""
              << ",\"screen_seed_start\":\"0x3d705100\""
              << ",\"confirmation_seed_start\":\"0x3d705200\""
              << ",\"generations\":" << kGenerations
              << ",\"population\":" << kPopulation
              << ",\"elite\":" << kElite
              << ",\"games_per_generation\":"
              << kGamesPerGeneration
              << ",\"max_moves\":" << kMaximumMoves
              << ",\"elapsed_seconds\":" << elapsed_seconds
              << ",\"max_rss_mib\":" << maximumResidentMiB()
              << ",\"screen_pass\":"
              << (screen_pass ? "true" : "false")
              << ",\"confirmed\":"
              << (confirmed ? "true" : "false")
              << ",\"final_log_multipliers\":";
    printVector(mean, false);
    std::cout << ",\"final_multipliers\":";
    printVector(mean, true);
    std::cout << ",\"final_sigma\":";
    printVector(sigma, false);
    std::cout << ",\"generation_reports\":[";
    for (std::size_t index = 0; index < reports.size(); ++index) {
      if (index > 0) std::cout << ',';
      const GenerationReport& report = reports[index];
      std::cout << "{\"generation\":" << report.generation
                << ",\"seed_start\":\"0x" << std::hex
                << report.seed_start << std::dec << "\""
                << ",\"best_objective\":"
                << report.best_objective
                << ",\"mean_objective\":"
                << report.mean_objective
                << ",\"best_mean_score\":"
                << report.best_mean_score
                << ",\"best_mean_moves\":"
                << report.best_mean_moves
                << ",\"mean\":";
      printVector(report.mean, false);
      std::cout << ",\"sigma\":";
      printVector(report.sigma, false);
      std::cout << '}';
    }
    std::cout << "],\"screen\":{\"mean_score_difference\":"
              << screen.mean_score_difference
              << ",\"mean_move_difference\":"
              << screen.mean_move_difference
              << ",\"baseline\":";
    printEvaluation(screen.baseline);
    std::cout << ",\"candidate\":";
    printEvaluation(screen.candidate);
    std::cout << "},\"confirmation\":";
    if (!screen_pass) {
      std::cout << "null";
    } else {
      std::cout << "{\"mean_score_difference\":"
                << confirmation.mean_score_difference
                << ",\"mean_move_difference\":"
                << confirmation.mean_move_difference
                << ",\"baseline\":";
      printEvaluation(confirmation.baseline);
      std::cout << ",\"candidate\":";
      printEvaluation(confirmation.candidate);
      std::cout << '}';
    }
    std::cout << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "drop7_phase_weight_evolution: "
              << error.what() << '\n';
    return 2;
  }
}
