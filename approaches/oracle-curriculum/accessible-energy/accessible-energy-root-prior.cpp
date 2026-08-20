#include "../../../src/core/native/public-behavior.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <thread>
#include <utility>
#include <vector>

// Uses the fixed accessible-energy residual only to rank statistically
// admissible root actions; it is never used at a search leaf.  Three
// otherwise-identical full-width exact-d3 searches estimate every root Q.  An
// alternative to the default-salt action is eligible only when a
// one-sided paired 95% lower confidence bound does not establish that it is
// worse.  Accessible energy breaks ties only inside that admissible set.
namespace drop7::accessible_energy_root_prior {

using Clock = std::chrono::steady_clock;

constexpr int kDepth = 3;
constexpr int kChanceSamples = 5;
constexpr std::uint64_t kMaximumWorkPerSalt = 1'000'000;
constexpr std::size_t kMaximumCacheEntries = 40'000;
constexpr int kMaximumMoves = 200;
constexpr int kScreenGames = 8;
constexpr int kConfirmationGames = 16;
constexpr int kDefaultThreads = 4;
constexpr std::uint32_t kScreenStart = 0x3e93'0000u;
constexpr std::uint32_t kConfirmationStart = 0x3e94'0000u;
constexpr std::array<std::uint32_t, 3> kPolicySalts{{
    0xd707'5eedu,
    0x91e1'0da5u,
    0x6a09'e667u,
}};
// t_(0.95, df=2): one-sided 95% lower confidence bound for three pairs.
constexpr double kOneSidedT95Df2 = 2.9199855803537256;
constexpr std::array<int, kBoardSize> kColumnOrder{{3, 2, 4, 1, 5, 0, 6}};

// These fixed standardized coefficients come from the public exact-d3 roll-in
// corpus 0x3d90f000..0x3d90f01f (first 24 train, last 8 held out).
constexpr std::uint32_t kFrozenCorpusStart = 0x3d90'f000u;
constexpr int kFrozenCorpusGames = 32;
constexpr int kFrozenTrainingGames = 24;
constexpr double kFrozenRidgeLambda = 1.0;

static_assert(kLevelBonus == 7'000);
static_assert(kPolicySalts.size() == 3);
static_assert((kScreenStart >> 24u) != 0x7du &&
              (kScreenStart >> 24u) != 0xd7u);
static_assert((kConfirmationStart >> 24u) != 0x7du &&
              (kConfirmationStart >> 24u) != 0xd7u);

std::mutex progress_mutex;

enum Feature : std::size_t {
  kDirectPotential,
  kLatentChainPotential,
  kTriggerReadiness,
  kRiseTriggerReadiness,
  kStoredHighNumbers,
  kCrackedExposure,
  kSolidCells,
  kSolidAltitude,
  kProjectedOccupancyDebt,
  kDeadLowNumbers,
  kLowCapLoad,
  kAdjacentLowCapLoad,
  kAdjacentOnes,
  kFeatureCount,
};

struct FrozenFeature {
  const char* name;
  double sign;
  double beta;
  double mean_signed_raw;
  double scale_signed_raw;
};

constexpr std::array<FrozenFeature, kFeatureCount> kFrozenFeatures{{
    {"directPotential", 1.0, 487.1271741, 2.548789234, 1.402080366},
    {"latentChainPotential", 1.0, 249.4035158, 0.4843543242, 0.7063806000},
    {"triggerReadiness", 1.0, 154.0850557, 5.125427873, 3.088196453},
    {"riseTriggerReadiness", 1.0, 460.1690947, 0.6477383863, 0.9043849076},
    {"storedHighNumbers", 1.0, 645.1574808, 1.328624026, 0.9209536686},
    {"crackedExposure", 1.0, 982.0400398, 1.093426602, 0.9611283387},
    {"solidCells", -1.0, 0.0, -13.64645477, 6.098995340},
    {"solidAltitude", -1.0, 0.0, -64.22444988, 66.02776321},
    {"projectedOccupancyDebt", -1.0, 0.0, -204.4596186, 218.6181777},
    {"deadLowNumbers", -1.0, 635.4718742, -2.006416548, 1.930066350},
    {"lowCapLoad", -1.0, 26.32354244, -43.28728606, 58.19274091},
    {"adjacentLowCapLoad", -1.0, 370.4575447, -12.50513447, 28.34514138},
    {"adjacentOnes", -1.0, 1093.809839, -0.2895314410, 0.6594060634},
}};

using RawFeatures = std::array<double, kFeatureCount>;

State publicState(const State& source) {
  State result;
  result.board = source.board;
  result.next_disc = source.next_disc;
  result.moves_remaining = source.moves_remaining;
  result.score = 0;
  result.level = 1;
  result.moves_played = 0;
  result.game_over = source.game_over;
  return result;
}

double readiness(int required) {
  return required >= 1 ? std::ldexp(1.0, 1 - required) : 0.0;
}

double unionReadiness(double first, double second) {
  return 1.0 - (1.0 - first) * (1.0 - second);
}

RawFeatures extractRawFeatures(const State& source) {
  const State state = publicState(source);
  const cfpi::detail::PhaseFeatures phase =
      cfpi::detail::extractPhaseFeatures(state);
  RawFeatures result{};
  result[kDirectPotential] = phase.direct_potential;
  result[kLatentChainPotential] = phase.latent_chain_potential;
  result[kTriggerReadiness] = phase.trigger_readiness;
  result[kRiseTriggerReadiness] = phase.rise_trigger_readiness;
  result[kCrackedExposure] = phase.cracked_exposure;
  result[kSolidCells] = phase.solid_cells;
  result[kProjectedOccupancyDebt] = phase.projected_occupancy_debt;
  result[kDeadLowNumbers] = phase.dead_low_numbers;
  result[kLowCapLoad] = phase.low_cap_load;
  result[kAdjacentLowCapLoad] = phase.adjacent_low_cap_load;
  result[kAdjacentOnes] = phase.adjacent_ones;

  for (int row = 0; row < kBoardSize; ++row) {
    const int elevation = kBoardSize - row;
    for (int column = 0; column < kBoardSize; ++column) {
      const std::uint8_t cell = state.board[indexOf(row, column)];
      if (cell == kSolid) result[kSolidAltitude] += elevation * elevation;
      if (cell < 5 || cell > 7) continue;
      const int horizontal = lineLength(state.board, row, column, false);
      const int vertical = lineLength(state.board, row, column, true);
      double ready = 0.0;
      if (horizontal < cell) ready = readiness(cell - horizontal);
      if (vertical < cell) {
        ready = unionReadiness(ready, readiness(cell - vertical));
      }
      if (ready > 0.0 && horizontal != cell && vertical != cell) {
        result[kStoredHighNumbers] += ready * (cell - 3) / 4.0;
      }
    }
  }
  for (std::size_t feature = 0; feature < kFeatureCount; ++feature) {
    result[feature] *= kFrozenFeatures[feature].sign;
    if (!std::isfinite(result[feature])) {
      throw std::runtime_error("accessible-energy feature is non-finite");
    }
  }
  return result;
}

// Intercept, clipping, and deployment scale are intentionally absent: all
// actions have the same number of root successor samples, so the intercept
// cancels, and a root rank prior needs no utility-unit conversion.  This uses
// the fixed model's unmodified standardized linear ordering, not a leaf value.
double accessibleEnergyPrior(const State& state) {
  const RawFeatures raw = extractRawFeatures(state);
  double result = 0.0;
  for (std::size_t feature = 0; feature < kFeatureCount; ++feature) {
    const FrozenFeature& frozen = kFrozenFeatures[feature];
    result += frozen.beta *
              (raw[feature] - frozen.mean_signed_raw) /
              frozen.scale_signed_raw;
  }
  if (!std::isfinite(result)) {
    throw std::runtime_error("accessible-energy prior is non-finite");
  }
  return result;
}

struct SaltRootValues {
  std::array<double, kBoardSize> q{};
  int winner = -1;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::size_t cache_entries = 0;
};

cfpi::BehaviorOptions exactOptions(std::uint32_t salt) {
  cfpi::BehaviorOptions options;
  options.max_depth = kDepth;
  options.chance_samples = kChanceSamples;
  options.max_work = kMaximumWorkPerSalt;
  options.max_cache_entries = kMaximumCacheEntries;
  options.policy_seed = salt;
  return options;
}

SaltRootValues evaluateSalt(const State& canonical, std::uint32_t salt) {
  const cfpi::BehaviorOptions options = exactOptions(salt);
  cfpi::detail::SearchContext context(options);
  SaltRootValues result;
  result.q.fill(-std::numeric_limits<double>::infinity());
  try {
    // Match the public policy exactly, including its iterative-deepening cache.
    for (int depth = 1; depth < kDepth; ++depth) {
      const auto completed =
          cfpi::detail::bestRootAction(canonical, depth, context);
      if (completed.first < 0) {
        throw std::runtime_error("exact root has no legal iterative action");
      }
    }
    for (const int column : kColumnOrder) {
      if (!isLegal(canonical.board, column)) continue;
      result.q[column] =
          cfpi::detail::evaluateAction(canonical, column, kDepth, context);
      if (!std::isfinite(result.q[column])) {
        throw std::runtime_error("exact root Q is non-finite");
      }
      if (result.winner < 0 ||
          result.q[column] > result.q[result.winner]) {
        result.winner = column;
      }
    }
  } catch (const cfpi::detail::WorkLimitReached&) {
    throw std::runtime_error("exact depth-three root exceeded work bound");
  }
  if (result.winner < 0) {
    throw std::runtime_error("exact root produced no legal winner");
  }
  result.work = context.work;
  result.nodes = context.nodes;
  result.cache_hits = context.cache_hits;
  result.cache_entries = context.cache.size();
  if (result.work > kMaximumWorkPerSalt ||
      result.cache_entries > kMaximumCacheEntries) {
    throw std::logic_error("exact root resource bound was violated");
  }
  return result;
}

double expectedAccessibleEnergy(const State& canonical, int action) {
  double sum = 0.0;
  int count = 0;
  for (const std::uint32_t salt : kPolicySalts) {
    const std::uint32_t state_seed =
        cfpi::detail::scenarioSeedForState(canonical, salt, kDepth);
    for (int sample = 0; sample < kChanceSamples; ++sample) {
      cfpi::detail::StratifiedRandom random{
          state_seed, sample, kChanceSamples, 0,
      };
      MoveResult move;
      if (!cfpi::detail::playMoveSampled(canonical, action, random, move)) {
        throw std::runtime_error("accessible-energy root sample failed");
      }
      move.state.score = 0;
      if (!move.state.game_over) {
        move.state.next_disc = cfpi::detail::sampledNextDisc(
            state_seed, sample, kChanceSamples);
      }
      bool ignored = false;
      const State successor =
          cfpi::detail::canonicalState(move.state, ignored);
      sum += accessibleEnergyPrior(successor);
      ++count;
    }
  }
  if (count != static_cast<int>(kPolicySalts.size()) * kChanceSamples) {
    throw std::logic_error("accessible-energy root sample count changed");
  }
  return sum / static_cast<double>(count);
}

struct GapEstimate {
  int action = -1;
  std::array<double, kPolicySalts.size()> paired_gaps{};
  double mean_gap = 0.0;
  double sample_sd = 0.0;
  double standard_error = 0.0;
  double margin = 0.0;
  double lower_bound = 0.0;
  double prior = -std::numeric_limits<double>::infinity();
  bool reference = false;
  bool admissible = false;
};

GapEstimate estimateGap(
    int reference, int action,
    const std::array<SaltRootValues, kPolicySalts.size()>& roots) {
  GapEstimate result;
  result.action = action;
  result.reference = action == reference;
  for (std::size_t member = 0; member < roots.size(); ++member) {
    result.paired_gaps[member] =
        roots[member].q[reference] - roots[member].q[action];
    result.mean_gap += result.paired_gaps[member] / roots.size();
  }
  double squared = 0.0;
  for (const double gap : result.paired_gaps) {
    const double delta = gap - result.mean_gap;
    squared += delta * delta;
  }
  result.sample_sd =
      std::sqrt(squared / static_cast<double>(roots.size() - 1));
  result.standard_error =
      result.sample_sd / std::sqrt(static_cast<double>(roots.size()));
  result.margin = kOneSidedT95Df2 * result.standard_error;
  result.lower_bound = result.mean_gap - result.margin;
  // Positive gap means the default action is better.  Reject an alternative
  // only when the one-sided lower bound is strictly positive.
  result.admissible = result.reference || result.lower_bound <= 0.0;
  return result;
}

struct Decision {
  int reference_action = -1;
  int action = -1;
  int legal_actions = 0;
  int admissible_actions = 0;
  bool switched = false;
  bool singleton_exact = false;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::size_t peak_cache_entries = 0;
  std::vector<GapEstimate> gaps;
};

Decision chooseAction(const State& source) {
  if (source.game_over) return {};
  bool mirrored = false;
  const State canonical =
      cfpi::detail::canonicalState(publicState(source), mirrored);
  std::array<SaltRootValues, kPolicySalts.size()> roots;
  Decision result;
  for (std::size_t member = 0; member < roots.size(); ++member) {
    roots[member] = evaluateSalt(canonical, kPolicySalts[member]);
    result.work += roots[member].work;
    result.nodes += roots[member].nodes;
    result.cache_hits += roots[member].cache_hits;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, roots[member].cache_entries);
  }
  const int canonical_reference = roots.front().winner;
  int canonical_choice = -1;
  double best_prior = -std::numeric_limits<double>::infinity();
  for (const int column : kColumnOrder) {
    if (!isLegal(canonical.board, column)) continue;
    ++result.legal_actions;
    GapEstimate gap = estimateGap(canonical_reference, column, roots);
    gap.prior = expectedAccessibleEnergy(canonical, column);
    if (gap.admissible) {
      ++result.admissible_actions;
      if (canonical_choice < 0 || gap.prior > best_prior) {
        canonical_choice = column;
        best_prior = gap.prior;
      }
    }
    result.gaps.push_back(gap);
  }
  if (canonical_choice < 0 || result.admissible_actions < 1) {
    throw std::logic_error("root confidence rule rejected every action");
  }
  if (result.admissible_actions == 1 &&
      canonical_choice != canonical_reference) {
    throw std::logic_error("singleton admissible set changed exact baseline");
  }
  result.singleton_exact = result.admissible_actions == 1;
  result.switched = canonical_choice != canonical_reference;
  result.reference_action = mirrored
                                ? kBoardSize - 1 - canonical_reference
                                : canonical_reference;
  result.action =
      mirrored ? kBoardSize - 1 - canonical_choice : canonical_choice;
  if (mirrored) {
    for (GapEstimate& gap : result.gaps) {
      gap.action = kBoardSize - 1 - gap.action;
    }
  }
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

struct PolicyTotals {
  std::uint64_t decisions = 0;
  std::uint64_t singleton_decisions = 0;
  std::uint64_t switches = 0;
  std::uint64_t legal_actions = 0;
  std::uint64_t admissible_actions = 0;
  std::uint64_t alternatives = 0;
  std::uint64_t admitted_alternatives = 0;
  double gap_sum = 0.0;
  double standard_error_sum = 0.0;
  double margin_sum = 0.0;
  double lower_bound_sum = 0.0;
  double maximum_margin = 0.0;
};

void addDecision(PolicyTotals& totals, const Decision& decision) {
  ++totals.decisions;
  totals.singleton_decisions += decision.singleton_exact;
  totals.switches += decision.switched;
  totals.legal_actions += decision.legal_actions;
  totals.admissible_actions += decision.admissible_actions;
  for (const GapEstimate& gap : decision.gaps) {
    if (gap.reference) continue;
    ++totals.alternatives;
    totals.admitted_alternatives += gap.admissible;
    totals.gap_sum += gap.mean_gap;
    totals.standard_error_sum += gap.standard_error;
    totals.margin_sum += gap.margin;
    totals.lower_bound_sum += gap.lower_bound;
    totals.maximum_margin = std::max(totals.maximum_margin, gap.margin);
  }
}

struct Game {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  int numbered_cleared = 0;
  int covers_revealed = 0;
  int waves = 0;
  bool censored = false;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::size_t peak_cache_entries = 0;
  double seconds = 0.0;
  PolicyTotals policy;
  std::vector<Decision> trace;
};

Game runGame(std::uint32_t seed, bool candidate, std::string_view label) {
  const auto started = Clock::now();
  State state = initialHeadlessState(seed);
  Game result;
  result.seed = seed;
  result.trace.reserve(kMaximumMoves);
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    int action = -1;
    if (!candidate) {
      cfpi::BehaviorMetrics metrics;
      action = cfpi::chooseBehaviorAction(
          publicState(state), exactOptions(kPolicySalts.front()), &metrics);
      if (metrics.completed_depth != kDepth || !metrics.complete) {
        throw std::runtime_error("baseline did not complete exact depth three");
      }
      result.work += metrics.work;
      result.nodes += metrics.nodes;
      result.cache_hits += metrics.cache_hits;
      result.peak_cache_entries =
          std::max(result.peak_cache_entries, metrics.cache_entries);
    } else {
      Decision decision = chooseAction(state);
      action = decision.action;
      result.work += decision.work;
      result.nodes += decision.nodes;
      result.cache_hits += decision.cache_hits;
      result.peak_cache_entries =
          std::max(result.peak_cache_entries, decision.peak_cache_entries);
      addDecision(result.policy, decision);
      result.trace.push_back(std::move(decision));
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("root policy selected an illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("headless transition failed");
    }
    for (const Wave& wave : move.waves) {
      result.numbered_cleared += wave.cleared;
      result.covers_revealed += wave.revealed;
      ++result.waves;
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  {
    const std::lock_guard<std::mutex> lock(progress_mutex);
    std::cerr << label << " seed 0x" << std::hex << seed << std::dec << ' '
              << result.score << " (" << result.moves << " moves, work "
              << result.work;
    if (candidate) {
      std::cerr << ", switches " << result.policy.switches << '/'
                << result.policy.decisions;
    }
    std::cerr << ")\n";
  }
  return result;
}

struct Cohort {
  std::vector<Game> baseline;
  std::vector<Game> candidate;
  double wall_seconds = 0.0;
};

Cohort runCohort(std::uint32_t start, int games, int threads,
                 std::string_view phase) {
  const auto started = Clock::now();
  Cohort result;
  result.baseline.resize(games);
  result.candidate.resize(games);
  std::atomic<int> next{0};
  std::atomic<bool> failed{false};
  std::mutex error_mutex;
  std::string error_message;
  std::vector<std::thread> workers;
  for (int worker = 0; worker < std::min(threads, games); ++worker) {
    workers.emplace_back([&] {
      while (!failed.load(std::memory_order_relaxed)) {
        const int index = next.fetch_add(1, std::memory_order_relaxed);
        if (index >= games) return;
        try {
          const std::uint32_t seed = start +
                                     static_cast<std::uint32_t>(index);
          result.baseline[index] = runGame(
              seed, false, std::string(phase) + "-exact-d3");
          result.candidate[index] = runGame(
              seed, true, std::string(phase) + "-energy-root-prior");
        } catch (const std::exception& error) {
          failed.store(true, std::memory_order_relaxed);
          const std::lock_guard<std::mutex> lock(error_mutex);
          if (error_message.empty()) error_message = error.what();
        }
      }
    });
  }
  for (std::thread& worker : workers) worker.join();
  if (failed.load()) {
    throw std::runtime_error("root-prior worker failed: " + error_message);
  }
  result.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

struct Summary {
  int games = 0;
  int censored = 0;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double score_per_move = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double waves_per_move = 0.0;
  double work_per_move = 0.0;
  double nodes_per_move = 0.0;
  double cache_hits_per_move = 0.0;
  double moves_per_cpu_second = 0.0;
  double aggregate_seconds = 0.0;
  std::size_t peak_cache_entries = 0;
  std::uint64_t peak_rss_bytes = 0;
  PolicyTotals policy;
};

Summary summarize(const std::vector<Game>& games) {
  if (games.empty()) throw std::invalid_argument("empty root-prior cohort");
  Summary result;
  result.games = static_cast<int>(games.size());
  double scores = 0.0;
  double moves = 0.0;
  double clears = 0.0;
  double reveals = 0.0;
  double waves = 0.0;
  double work = 0.0;
  double nodes = 0.0;
  double cache_hits = 0.0;
  for (const Game& game : games) {
    scores += game.score;
    moves += game.moves;
    clears += game.numbered_cleared;
    reveals += game.covers_revealed;
    waves += game.waves;
    work += static_cast<double>(game.work);
    nodes += static_cast<double>(game.nodes);
    cache_hits += static_cast<double>(game.cache_hits);
    result.aggregate_seconds += game.seconds;
    result.censored += game.censored;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, game.peak_cache_entries);
    result.policy.decisions += game.policy.decisions;
    result.policy.singleton_decisions += game.policy.singleton_decisions;
    result.policy.switches += game.policy.switches;
    result.policy.legal_actions += game.policy.legal_actions;
    result.policy.admissible_actions += game.policy.admissible_actions;
    result.policy.alternatives += game.policy.alternatives;
    result.policy.admitted_alternatives +=
        game.policy.admitted_alternatives;
    result.policy.gap_sum += game.policy.gap_sum;
    result.policy.standard_error_sum += game.policy.standard_error_sum;
    result.policy.margin_sum += game.policy.margin_sum;
    result.policy.lower_bound_sum += game.policy.lower_bound_sum;
    result.policy.maximum_margin =
        std::max(result.policy.maximum_margin,
                 game.policy.maximum_margin);
  }
  result.mean_score = scores / games.size();
  result.mean_moves = moves / games.size();
  result.score_per_move = scores / moves;
  result.clears_per_move = clears / moves;
  result.reveals_per_move = reveals / moves;
  result.waves_per_move = waves / moves;
  result.work_per_move = work / moves;
  result.nodes_per_move = nodes / moves;
  result.cache_hits_per_move = cache_hits / moves;
  result.moves_per_cpu_second = moves / result.aggregate_seconds;
  result.peak_rss_bytes = peakRssBytes();
  return result;
}

struct Paired {
  double mean_score_delta = 0.0;
  double mean_move_delta = 0.0;
  int score_wins = 0;
  int score_ties = 0;
  int score_losses = 0;
  int move_wins = 0;
  int move_ties = 0;
  int move_losses = 0;
};

Paired paired(const Cohort& cohort) {
  if (cohort.baseline.empty() ||
      cohort.baseline.size() != cohort.candidate.size()) {
    throw std::invalid_argument("root-prior cohort is not paired");
  }
  Paired result;
  for (std::size_t game = 0; game < cohort.baseline.size(); ++game) {
    const std::int64_t score_delta =
        cohort.candidate[game].score - cohort.baseline[game].score;
    const int move_delta =
        cohort.candidate[game].moves - cohort.baseline[game].moves;
    result.mean_score_delta += score_delta;
    result.mean_move_delta += move_delta;
    if (score_delta > 0) ++result.score_wins;
    else if (score_delta < 0) ++result.score_losses;
    else ++result.score_ties;
    if (move_delta > 0) ++result.move_wins;
    else if (move_delta < 0) ++result.move_losses;
    else ++result.move_ties;
  }
  result.mean_score_delta /= cohort.baseline.size();
  result.mean_move_delta /= cohort.baseline.size();
  return result;
}

void writePolicyTotals(std::ostream& output, const PolicyTotals& policy) {
  const double decisions = static_cast<double>(policy.decisions);
  const double alternatives = static_cast<double>(policy.alternatives);
  output << "{\"decisions\":" << policy.decisions
         << ",\"singletonDecisions\":" << policy.singleton_decisions
         << ",\"singletonRate\":"
         << (decisions > 0.0 ? policy.singleton_decisions / decisions : 0.0)
         << ",\"switches\":" << policy.switches
         << ",\"switchRate\":"
         << (decisions > 0.0 ? policy.switches / decisions : 0.0)
         << ",\"meanLegalSetSize\":"
         << (decisions > 0.0 ? policy.legal_actions / decisions : 0.0)
         << ",\"meanAdmissibleSetSize\":"
         << (decisions > 0.0 ? policy.admissible_actions / decisions : 0.0)
         << ",\"alternatives\":" << policy.alternatives
         << ",\"admittedAlternatives\":"
         << policy.admitted_alternatives
         << ",\"alternativeAdmissionRate\":"
         << (alternatives > 0.0
                 ? policy.admitted_alternatives / alternatives
                 : 0.0)
         << ",\"pairedGapMean\":"
         << (alternatives > 0.0 ? policy.gap_sum / alternatives : 0.0)
         << ",\"pairedStandardErrorMean\":"
         << (alternatives > 0.0
                 ? policy.standard_error_sum / alternatives
                 : 0.0)
         << ",\"oneSidedMarginMean\":"
         << (alternatives > 0.0 ? policy.margin_sum / alternatives : 0.0)
         << ",\"lowerBoundMean\":"
         << (alternatives > 0.0
                 ? policy.lower_bound_sum / alternatives
                 : 0.0)
         << ",\"maximumMargin\":" << policy.maximum_margin << '}';
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"games\":" << summary.games
         << ",\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"scorePerMove\":" << summary.score_per_move
         << ",\"numberedClearedPerMove\":" << summary.clears_per_move
         << ",\"coversRevealedPerMove\":" << summary.reveals_per_move
         << ",\"wavesPerMove\":" << summary.waves_per_move
         << ",\"workPerMove\":" << summary.work_per_move
         << ",\"nodesPerMove\":" << summary.nodes_per_move
         << ",\"cacheHitsPerMove\":" << summary.cache_hits_per_move
         << ",\"movesPerCpuSecond\":" << summary.moves_per_cpu_second
         << ",\"aggregateSeconds\":" << summary.aggregate_seconds
         << ",\"peakCacheEntries\":" << summary.peak_cache_entries
         << ",\"peakRssBytes\":" << summary.peak_rss_bytes
         << ",\"censored\":" << summary.censored << ",\"rootPolicy\":";
  writePolicyTotals(output, summary.policy);
  output << '}';
}

void writeGap(std::ostream& output, const GapEstimate& gap) {
  output << "{\"action\":" << gap.action << ",\"pairedQGaps\":["
         << gap.paired_gaps[0] << ',' << gap.paired_gaps[1] << ','
         << gap.paired_gaps[2] << "],\"meanGap\":" << gap.mean_gap
         << ",\"sampleSd\":" << gap.sample_sd
         << ",\"standardError\":" << gap.standard_error
         << ",\"oneSidedMargin\":" << gap.margin
         << ",\"lowerBound\":" << gap.lower_bound
         << ",\"accessibleEnergyPrior\":" << gap.prior
         << ",\"reference\":" << (gap.reference ? "true" : "false")
         << ",\"admissible\":" << (gap.admissible ? "true" : "false")
         << '}';
}

void writeTrace(std::ostream& output, const Game& game) {
  output << '[';
  for (std::size_t move = 0; move < game.trace.size(); ++move) {
    if (move > 0) output << ',';
    const Decision& decision = game.trace[move];
    output << "{\"move\":" << move
           << ",\"referenceAction\":" << decision.reference_action
           << ",\"chosenAction\":" << decision.action
           << ",\"legalSetSize\":" << decision.legal_actions
           << ",\"admissibleSetSize\":" << decision.admissible_actions
           << ",\"switched\":" << (decision.switched ? "true" : "false")
           << ",\"singletonExact\":"
           << (decision.singleton_exact ? "true" : "false")
           << ",\"actions\":[";
    for (std::size_t index = 0; index < decision.gaps.size(); ++index) {
      if (index > 0) output << ',';
      writeGap(output, decision.gaps[index]);
    }
    output << "]}";
  }
  output << ']';
}

void writeCohort(std::ostream& output, const Cohort& cohort) {
  const Summary baseline = summarize(cohort.baseline);
  const Summary candidate = summarize(cohort.candidate);
  const Paired comparison = paired(cohort);
  output << "{\"baseline\":";
  writeSummary(output, baseline);
  output << ",\"candidate\":";
  writeSummary(output, candidate);
  output << ",\"paired\":{\"meanScoreDelta\":"
         << comparison.mean_score_delta << ",\"meanMoveDelta\":"
         << comparison.mean_move_delta << ",\"scoreWins\":"
         << comparison.score_wins << ",\"scoreTies\":"
         << comparison.score_ties << ",\"scoreLosses\":"
         << comparison.score_losses << ",\"moveWins\":"
         << comparison.move_wins << ",\"moveTies\":"
         << comparison.move_ties << ",\"moveLosses\":"
         << comparison.move_losses << "},\"games\":[";
  for (std::size_t game = 0; game < cohort.baseline.size(); ++game) {
    if (game > 0) output << ',';
    const Game& baseline_game = cohort.baseline[game];
    const Game& candidate_game = cohort.candidate[game];
    output << "{\"seed\":" << baseline_game.seed
           << ",\"baselineScore\":" << baseline_game.score
           << ",\"candidateScore\":" << candidate_game.score
           << ",\"scoreDelta\":"
           << candidate_game.score - baseline_game.score
           << ",\"baselineMoves\":" << baseline_game.moves
           << ",\"candidateMoves\":" << candidate_game.moves
           << ",\"moveDelta\":"
           << candidate_game.moves - baseline_game.moves
           << ",\"candidatePolicy\":";
    writePolicyTotals(output, candidate_game.policy);
    output << ",\"decisionTrace\":";
    writeTrace(output, candidate_game);
    output << '}';
  }
  output << "],\"wallSeconds\":" << cohort.wall_seconds << '}';
}

bool improvesBoth(const Cohort& cohort) {
  const Summary baseline = summarize(cohort.baseline);
  const Summary candidate = summarize(cohort.candidate);
  return candidate.mean_score > baseline.mean_score &&
         candidate.mean_moves > baseline.mean_moves;
}

bool confidenceRuleSelfTest() {
  std::array<SaltRootValues, kPolicySalts.size()> roots;
  for (SaltRootValues& root : roots) root.q.fill(0.0);
  for (SaltRootValues& root : roots) {
    root.q[3] = 10.0;
    root.q[2] = 9.0;
  }
  const GapEstimate certain_worse = estimateGap(3, 2, roots);
  roots[0].q[2] = 9.0;
  roots[1].q[2] = 10.0;
  roots[2].q[2] = 11.0;
  const GapEstimate uncertain = estimateGap(3, 2, roots);
  return !certain_worse.admissible && certain_worse.lower_bound > 0.0 &&
         uncertain.admissible && uncertain.lower_bound <= 0.0;
}

bool selfTest(std::ostream& output) {
  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;

  const Decision first = chooseAction(state);
  const Decision repeat = chooseAction(state);
  cfpi::BehaviorMetrics baseline_metrics;
  const int baseline = cfpi::chooseBehaviorAction(
      publicState(state), exactOptions(kPolicySalts.front()),
      &baseline_metrics);

  State mirrored = state;
  mirrored.board = cfpi::detail::mirrorBoard(state.board);
  const Decision reflected = chooseAction(mirrored);
  State metadata = state;
  metadata.score = 987'654;
  metadata.level = 42;
  metadata.moves_played = 123;
  const Decision metadata_decision = chooseAction(metadata);

  State singleton;
  singleton.board = initialBoard();
  for (int column = 0; column < kBoardSize; ++column) {
    if (column != 3) singleton.board[indexOf(0, column)] = 4;
  }
  singleton.next_disc = 6;
  singleton.moves_remaining = 2;
  const Decision singleton_decision = chooseAction(singleton);
  const int singleton_baseline = cfpi::chooseBehaviorAction(
      publicState(singleton), exactOptions(kPolicySalts.front()));

  const double prior = accessibleEnergyPrior(state);
  const State prior_mirror = [&] {
    State result = state;
    result.board = cfpi::detail::mirrorBoard(state.board);
    return result;
  }();
  const bool deterministic = first.action == repeat.action &&
                             first.reference_action == repeat.reference_action &&
                             first.work == repeat.work;
  const bool reference_parity =
      first.reference_action == baseline && baseline_metrics.complete;
  const bool reflection_safe =
      reflected.action == kBoardSize - 1 - first.action &&
      reflected.reference_action ==
          kBoardSize - 1 - first.reference_action;
  const bool public_state_only = metadata_decision.action == first.action;
  const bool singleton_exact = singleton_decision.admissible_actions == 1 &&
                               singleton_decision.action == singleton_baseline &&
                               !singleton_decision.switched;
  const bool bounded =
      first.work <= kPolicySalts.size() * kMaximumWorkPerSalt &&
      first.peak_cache_entries <= kMaximumCacheEntries;
  const bool mirror_prior =
      std::abs(prior - accessibleEnergyPrior(prior_mirror)) <= 1.0e-9;
  const bool legal = isLegal(state.board, first.action);
  const bool confidence = confidenceRuleSelfTest();
  const bool passed = deterministic && reference_parity && reflection_safe &&
                      public_state_only && singleton_exact && bounded &&
                      mirror_prior && legal && confidence;
  output << std::setprecision(10)
         << "{\"passed\":" << (passed ? "true" : "false")
         << ",\"deterministic\":" << (deterministic ? "true" : "false")
         << ",\"referenceParity\":"
         << (reference_parity ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe ? "true" : "false")
         << ",\"publicStateOnly\":"
         << (public_state_only ? "true" : "false")
         << ",\"singletonExact\":"
         << (singleton_exact ? "true" : "false")
         << ",\"bounded\":" << (bounded ? "true" : "false")
         << ",\"mirrorPrior\":" << (mirror_prior ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false")
         << ",\"confidenceRule\":" << (confidence ? "true" : "false")
         << ",\"action\":" << first.action
         << ",\"referenceAction\":" << first.reference_action
         << ",\"admissibleSetSize\":" << first.admissible_actions
         << ",\"work\":" << first.work << "}\n";
  return passed;
}

struct RunOptions {
  int threads = kDefaultThreads;
  std::string output = "/tmp/drop7-accessible-energy-root-prior.json";
};

RunOptions parseOptions(int argc, char** argv) {
  RunOptions result;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--threads" && index + 1 < argc) {
      result.threads = std::stoi(argv[++index]);
    } else if (argument == "--output" && index + 1 < argc) {
      result.output = argv[++index];
    } else {
      throw std::invalid_argument("unknown accessible-energy root-prior option");
    }
  }
  if (result.threads < 1 || result.threads > 32) {
    throw std::invalid_argument("root-prior threads must be from 1 to 32");
  }
  return result;
}

int run(int argc, char** argv) {
  const auto started = Clock::now();
  const RunOptions options = parseOptions(argc, argv);
  const Cohort screen = runCohort(
      kScreenStart, kScreenGames, options.threads, "screen");
  const bool screen_passed = improvesBoth(screen);
  Cohort confirmation;
  if (screen_passed) {
    confirmation = runCohort(kConfirmationStart, kConfirmationGames,
                             options.threads, "confirmation");
  }
  const bool confirmation_passed =
      screen_passed && improvesBoth(confirmation);

  std::ofstream output(options.output);
  if (!output) {
    throw std::runtime_error("could not write root-prior artifact");
  }
  output << std::setprecision(10)
         << "{\n  \"format\":\"drop7-accessible-energy-root-prior-v1\",\n"
         << "  \"publicStateOnly\":true,\n"
         << "  \"leafEvaluator\":\"unchanged-exact-d3-phase-utility\",\n"
         << "  \"rootCompleteness\":\"all-legal-actions\",\n"
         << "  \"mechanics\":{\"levelBonus\":" << kLevelBonus << "},\n"
         << "  \"frozenAccessibleEnergy\":{\"source\":"
            "\"drop7-accessible-energy-lab-v1\",\"corpusStart\":"
         << kFrozenCorpusStart << ",\"corpusGames\":" << kFrozenCorpusGames
         << ",\"trainingGames\":" << kFrozenTrainingGames
         << ",\"ridgeLambda\":" << kFrozenRidgeLambda
         << ",\"use\":\"root-successor-ranking-only\","
            "\"postHocScale\":false,\"postHocPruning\":false,\"features\":[";
  for (std::size_t feature = 0; feature < kFeatureCount; ++feature) {
    if (feature > 0) output << ',';
    const FrozenFeature& frozen = kFrozenFeatures[feature];
    output << "{\"name\":\"" << frozen.name << "\",\"sign\":"
           << frozen.sign << ",\"beta\":" << frozen.beta
           << ",\"meanSignedRaw\":" << frozen.mean_signed_raw
           << ",\"scaleSignedRaw\":" << frozen.scale_signed_raw << '}';
  }
  output << "]},\n  \"rootRule\":{\"reference\":"
            "\"current-default-salt-exact-d3\",\"depth\":"
         << kDepth << ",\"chanceSamplesPerSalt\":" << kChanceSamples
         << ",\"policySalts\":[" << kPolicySalts[0] << ','
         << kPolicySalts[1] << ',' << kPolicySalts[2]
         << "],\"pairedReplicates\":" << kPolicySalts.size()
         << ",\"uncertainty\":\"one-sided-paired-95-percent-lower-bound\","
            "\"studentTCriticalDf2\":" << kOneSidedT95Df2
         << ",\"admission\":\"reference-minus-alternative-lower-bound-lte-zero\","
            "\"singletonReturnsExactBaseline\":true},\n"
         << "  \"evaluation\":{\"maximumMoves\":" << kMaximumMoves
         << ",\"screenStart\":" << kScreenStart
         << ",\"screenGames\":" << kScreenGames
         << ",\"confirmationStart\":" << kConfirmationStart
         << ",\"confirmationGames\":" << kConfirmationGames
         << ",\"forbiddenSeedFamiliesInspected\":false},\n"
         << "  \"screen\":";
  writeCohort(output, screen);
  output << ",\n  \"screenPassed\":"
         << (screen_passed ? "true" : "false")
         << ",\n  \"confirmation\":";
  if (screen_passed) writeCohort(output, confirmation);
  else output << "null";
  output << ",\n  \"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\n  \"decision\":\""
         << (!screen_passed
                 ? "reject-screen"
                 : (confirmation_passed ? "advance" : "reject-confirmation"))
         << "\",\n  \"totalWallSeconds\":"
         << std::chrono::duration<double>(Clock::now() - started).count()
         << "\n}\n";

  const Summary baseline = summarize(screen.baseline);
  const Summary candidate = summarize(screen.candidate);
  const Paired comparison = paired(screen);
  std::cout << std::fixed << std::setprecision(3)
            << "ACCESSIBLE_ENERGY_ROOT_PRIOR {\"baselineScore\":"
            << baseline.mean_score << ",\"candidateScore\":"
            << candidate.mean_score << ",\"scoreDelta\":"
            << comparison.mean_score_delta << ",\"baselineMoves\":"
            << baseline.mean_moves << ",\"candidateMoves\":"
            << candidate.mean_moves << ",\"moveDelta\":"
            << comparison.mean_move_delta << ",\"switchRate\":"
            << (candidate.policy.decisions > 0
                    ? static_cast<double>(candidate.policy.switches) /
                          candidate.policy.decisions
                    : 0.0)
            << ",\"meanAdmissibleSetSize\":"
            << (candidate.policy.decisions > 0
                    ? static_cast<double>(candidate.policy.admissible_actions) /
                          candidate.policy.decisions
                    : 0.0)
            << ",\"screenPassed\":"
            << (screen_passed ? "true" : "false")
            << ",\"confirmationPassed\":"
            << (confirmation_passed ? "true" : "false")
            << ",\"peakRssBytes\":" << peakRssBytes() << "}\n";
  return EXIT_SUCCESS;
}

}  // namespace drop7::accessible_energy_root_prior

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::accessible_energy_root_prior::selfTest(std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    return drop7::accessible_energy_root_prior::run(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "drop7_accessible_energy_root_prior: " << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
}
