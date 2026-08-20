#define DROP7_FAIR_ONLY_DEPTH4_NO_MAIN
#include "../reference/fair-only-depth4.cpp"
#undef DROP7_FAIR_ONLY_DEPTH4_NO_MAIN

#include <algorithm>
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
#include <list>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <unordered_map>
#include <utility>
#include <vector>

// A bounded, public-state-only selective extension of the reference fair-D4
// search.  Every root action remains full width.  At internal max nodes, legal
// actions are ordered by an exact one-ply fair evaluation using the same five
// stratified chance outcomes, then only the configured leading actions receive
// the deeper search.  The fit menu and every data gate are fixed below.
namespace drop7::fair_selective_depth {

namespace d4 = drop7::fair_only_depth4;
namespace frozen = drop7::fair_only_horizon;

constexpr int kChanceSamples = frozen::kChanceSamples;
constexpr std::uint64_t kMaximumSelectiveWork = 3'200'000;
constexpr std::size_t kMaximumCacheEntries = 45'000;
constexpr std::uint64_t kResidentTargetBytes = 64u * 1024u * 1024u;
constexpr std::uint32_t kTrainingSeedStart = 0x3dd0'0000u;
constexpr std::uint32_t kHeldoutSeedStart = 0x3dd1'0000u;
constexpr std::uint32_t kScreenSeedStart = 0x3ea7'0000u;
constexpr std::uint32_t kConfirmationSeedStart = 0x3ea8'0000u;
constexpr int kTrainingGames = 4;
constexpr int kHeldoutGames = 8;
constexpr int kScreenGames = 8;
constexpr int kConfirmationGames = 16;
constexpr int kTrainingMaximumMoves = 500;
constexpr int kEvaluationMaximumMoves = 1'000;
constexpr int kParallelism = 2;
constexpr double kLowerTailFloorRatio = 0.75;

enum class PolicyKind { kExactD4, kUniformSelective, kPhaseAligned };

struct PolicySpec {
  const char* name = "";
  PolicyKind kind = PolicyKind::kUniformSelective;
  int depth = 0;
  int internal_width = 0;
};

constexpr PolicySpec kBaseline{"fair-d4", PolicyKind::kExactD4, 4, 7};
constexpr std::array<PolicySpec, 3> kUniformMenu{{
    {"selective-d5-w2", PolicyKind::kUniformSelective, 5, 2},
    {"selective-d5-w3", PolicyKind::kUniformSelective, 5, 3},
    {"selective-d6-w2", PolicyKind::kUniformSelective, 6, 2},
}};
// This contingency is fitted on the same training seeds only when every
// uniform candidate is weak.  It is fixed before the disjoint heldout gate.
constexpr PolicySpec kPhaseAligned{"phase-d3-d4-d5w2",
                                    PolicyKind::kPhaseAligned, 5, 2};

constexpr std::uint64_t worstSelectiveNodeWork(int depth, int width) {
  constexpr std::uint64_t full_shallow =
      kBoardSize * kChanceSamples * 2u;  // transition plus fair leaf
  if (depth == 0) return 1;
  if (depth == 1) return full_shallow;
  return full_shallow + static_cast<std::uint64_t>(width) * kChanceSamples *
                            (1u + worstSelectiveNodeWork(depth - 1, width));
}

constexpr std::uint64_t worstSelectiveRootWork(int depth, int width) {
  return kBoardSize * kChanceSamples *
         (1u + worstSelectiveNodeWork(depth - 1, width));
}

constexpr std::uint64_t worstSelectiveCacheEntries(int depth, int width) {
  std::uint64_t result = 0;
  std::uint64_t states = kBoardSize * kChanceSamples;
  for (int remaining = depth - 1; remaining >= 1; --remaining) {
    result += states;
    if (remaining > 1) {
      states *= static_cast<std::uint64_t>(width) * kChanceSamples;
    }
  }
  return result;
}

constexpr std::uint64_t kWorstD5W2Work = worstSelectiveRootWork(5, 2);
constexpr std::uint64_t kWorstD5W2Cache =
    worstSelectiveCacheEntries(5, 2);
static_assert(kWorstD5W2Work == 2'760'835);
static_assert(kWorstD5W2Cache == 38'885);
static_assert(kWorstD5W2Work < kMaximumSelectiveWork);
static_assert(kWorstD5W2Cache < kMaximumCacheEntries);
static_assert(kChanceSamples == 5);
static_assert(kLevelBonus == 7'000);
static_assert(kTrainingSeedStart + kTrainingGames < kHeldoutSeedStart);
static_assert(kHeldoutSeedStart + kHeldoutGames < kScreenSeedStart);
static_assert(kScreenSeedStart + kScreenGames < kConfirmationSeedStart);
static_assert((kTrainingSeedStart >> 24) != 0x7du &&
              (kTrainingSeedStart >> 24) != 0xd7u);
static_assert((kHeldoutSeedStart >> 24) != 0x7du &&
              (kHeldoutSeedStart >> 24) != 0xd7u);
static_assert((kScreenSeedStart >> 24) != 0x7du &&
              (kScreenSeedStart >> 24) != 0xd7u);
static_assert((kConfirmationSeedStart >> 24) != 0x7du &&
              (kConfirmationSeedStart >> 24) != 0xd7u);

std::mutex progress_mutex;

class WorkLimitReached : public std::exception {};

struct CacheEntry {
  double value = 0.0;
  std::list<std::string>::iterator order;
};

struct SearchContext {
  explicit SearchContext(const PolicySpec& policy,
                         std::uint64_t work_limit = kMaximumSelectiveWork)
      : spec(policy), maximum_work(work_limit) {}

  const PolicySpec& spec;
  std::uint64_t maximum_work;
  std::unordered_map<std::string, CacheEntry> cache;
  std::list<std::string> order;
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
  std::uint64_t cache_hits = 0;
  std::uint64_t ordering_work = 0;
  std::size_t peak_cache_entries = 0;
};

void checkBudget(const SearchContext& context) {
  if (context.work >= context.maximum_work) throw WorkLimitReached{};
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
  context.peak_cache_entries =
      std::max(context.peak_cache_entries, context.cache.size());
}

double bestFutureValue(const State& state, int depth, SearchContext& context);

struct ActionValue {
  double value = 0.0;
  double expected_score = 0.0;
};

ActionValue evaluateAction(const State& state, int column, int depth,
                           SearchContext& context) {
  const std::uint32_t state_seed = cfpi::detail::scenarioSeedForState(
      state, frozen::kPolicySeed, depth);
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
      result.value += frozen::kTerminalUtility;
      continue;
    }
    const double score_delta = static_cast<double>(move.score_delta);
    result.expected_score += score_delta;
    if (move.state.game_over) {
      result.value += score_delta + frozen::kTerminalUtility;
      continue;
    }
    move.state.score = 0;
    move.state.next_disc = cfpi::detail::sampledNextDisc(
        state_seed, sample, kChanceSamples);
    bool ignored = false;
    const State next = cfpi::detail::canonicalState(move.state, ignored);
    result.value += score_delta + bestFutureValue(next, depth - 1, context);
  }
  result.value /= kChanceSamples;
  result.expected_score /= kChanceSamples;
  return result;
}

double evaluateLeaf(const State& state, SearchContext& context) {
  checkBudget(context);
  ++context.work;
  const double value = frozen::fairLeaf(state);
  if (!std::isfinite(value)) {
    throw std::runtime_error("selective fair leaf returned non-finite value");
  }
  return value;
}

std::vector<int> rankedInternalActions(const State& state, int depth,
                                       SearchContext& context) {
  std::vector<int> actions;
  for (const int column : cfpi::detail::kColumnOrder) {
    if (isLegal(state.board, column)) actions.push_back(column);
  }
  if (depth <= 1 ||
      actions.size() <= static_cast<std::size_t>(context.spec.internal_width)) {
    return actions;
  }
  struct RankedAction {
    int column = -1;
    double value = 0.0;
  };
  std::vector<RankedAction> ranked;
  ranked.reserve(actions.size());
  for (const int column : actions) {
    const std::uint64_t before = context.work;
    const ActionValue shallow = evaluateAction(state, column, 1, context);
    context.ordering_work += context.work - before;
    ranked.push_back({column, shallow.value});
  }
  std::stable_sort(ranked.begin(), ranked.end(),
                   [](const RankedAction& first,
                      const RankedAction& second) {
                     return first.value > second.value;
                   });
  actions.clear();
  const int retained = std::min(context.spec.internal_width,
                                static_cast<int>(ranked.size()));
  for (int index = 0; index < retained; ++index) {
    actions.push_back(ranked[index].column);
  }
  return actions;
}

double bestFutureValue(const State& state, int depth,
                       SearchContext& context) {
  ++context.nodes;
  checkBudget(context);
  if (state.game_over) return frozen::kTerminalUtility;
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
  const std::vector<int> actions =
      rankedInternalActions(state, depth, context);
  for (const int column : actions) {
    best = std::max(best, evaluateAction(state, column, depth, context).value);
  }
  if (!std::isfinite(best)) best = frozen::kTerminalUtility;
  cacheValue(context, key, best);
  return best;
}

struct RootEvaluation {
  int action = -1;
  double value = -std::numeric_limits<double>::infinity();
  int legal_actions = 0;
  int completed_actions = 0;
  std::array<double, kBoardSize> values{};
};

RootEvaluation rootDecision(const State& canonical, int depth,
                            SearchContext& context) {
  RootEvaluation result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  for (const int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(canonical.board, column)) continue;
    ++result.legal_actions;
    const ActionValue candidate =
        evaluateAction(canonical, column, depth, context);
    ++result.completed_actions;
    result.values[column] = candidate.value;
    if (candidate.value > result.value) {
      result.value = candidate.value;
      result.action = column;
    }
  }
  return result;
}

struct SearchDecision {
  int action = -1;
  int requested_depth = 0;
  int internal_width = 0;
  bool complete = false;
  bool selective_complete = false;
  bool used_fallback = false;
  bool full_root = false;
  std::uint64_t work = 0;
  std::uint64_t selective_work = 0;
  std::uint64_t fallback_work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::uint64_t ordering_work = 0;
  std::size_t peak_cache_entries = 0;
};

SearchDecision wrapDepth4(const d4::SearchDecision& decision) {
  return {decision.action,
          4,
          7,
          decision.complete && decision.completed_depth == 4,
          false,
          false,
          true,
          decision.work,
          0,
          0,
          decision.nodes,
          decision.cache_hits,
          0,
          decision.cache_entries};
}

SearchDecision wrapDepth3(const frozen::SearchDecision& decision) {
  return {decision.action,
          3,
          7,
          decision.complete && decision.completed_depth == 3,
          false,
          false,
          true,
          decision.work,
          0,
          0,
          decision.nodes,
          decision.cache_hits,
          0,
          decision.cache_entries};
}

SearchDecision chooseUniformSelective(
    const State& source, const PolicySpec& spec,
    const d4::SearchDecision* known_fallback = nullptr,
    std::uint64_t work_limit = kMaximumSelectiveWork) {
  if (source.game_over) return {};
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  RootEvaluation root;
  SearchDecision result;
  result.requested_depth = spec.depth;
  result.internal_width = spec.internal_width;
  {
    SearchContext context(spec, work_limit);
    try {
      root = rootDecision(canonical, spec.depth, context);
      result.selective_complete =
          root.action >= 0 && root.completed_actions == root.legal_actions;
    } catch (const WorkLimitReached&) {
      result.selective_complete = false;
    }
    result.selective_work = context.work;
    result.nodes = context.nodes;
    result.cache_hits = context.cache_hits;
    result.ordering_work = context.ordering_work;
    result.peak_cache_entries = context.peak_cache_entries;
  }
  if (result.selective_complete) {
    result.action = mirrored ? kBoardSize - 1 - root.action : root.action;
    result.complete = true;
    result.full_root = true;
    result.work = result.selective_work;
    return result;
  }

  const d4::SearchDecision fallback =
      known_fallback == nullptr ? d4::chooseDepth4Action(source)
                                : *known_fallback;
  if (!fallback.complete || fallback.completed_depth != 4 ||
      !isLegal(source.board, fallback.action)) {
    throw std::runtime_error("selective fallback did not complete fair D4");
  }
  result.action = fallback.action;
  result.complete = true;
  result.used_fallback = true;
  result.full_root = true;
  result.fallback_work = fallback.work;
  result.work += result.selective_work + result.fallback_work;
  result.nodes += fallback.nodes;
  result.cache_hits += fallback.cache_hits;
  result.peak_cache_entries =
      std::max(result.peak_cache_entries, fallback.cache_entries);
  return result;
}

SearchDecision choosePolicyAction(
    const State& source, const PolicySpec& spec,
    const d4::SearchDecision* known_d4 = nullptr,
    std::uint64_t work_limit = kMaximumSelectiveWork) {
  if (spec.kind == PolicyKind::kExactD4) {
    const d4::SearchDecision decision =
        known_d4 == nullptr ? d4::chooseDepth4Action(source) : *known_d4;
    return wrapDepth4(decision);
  }
  if (spec.kind == PolicyKind::kUniformSelective) {
    return chooseUniformSelective(source, spec, known_d4, work_limit);
  }
  if (source.moves_remaining <= 3) {
    return wrapDepth3(frozen::chooseFairAction(source));
  }
  if (source.moves_remaining == 4) {
    const d4::SearchDecision decision =
        known_d4 == nullptr ? d4::chooseDepth4Action(source) : *known_d4;
    return wrapDepth4(decision);
  }
  return chooseUniformSelective(source, kUniformMenu[0], known_d4,
                                work_limit);
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

struct PhaseStats {
  std::uint64_t decisions = 0;
  std::uint64_t switches_from_d4 = 0;
  std::uint64_t fallbacks = 0;
  std::int64_t score_delta = 0;
  std::uint64_t numbered_cleared = 0;
  std::uint64_t covers_revealed = 0;
  std::uint64_t policy_work = 0;
  std::uint64_t d4_reference_work = 0;
};

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t numbered_cleared = 0;
  std::uint64_t covers_revealed = 0;
  int maximum_chain = 0;
  std::uint64_t switches_from_d4 = 0;
  std::uint64_t fallbacks = 0;
  std::uint64_t incomplete_selective = 0;
  std::uint64_t root_width_violations = 0;
  std::uint64_t work = 0;
  std::uint64_t selective_work = 0;
  std::uint64_t fallback_work = 0;
  std::uint64_t d4_reference_work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::uint64_t ordering_work = 0;
  std::size_t peak_cache_entries = 0;
  std::array<PhaseStats, kMovesPerLevel> phase{};
  std::uint64_t peak_rss_bytes = 0;
  double elapsed_seconds = 0.0;
};

void observeMove(const MoveResult& move, GameResult& result,
                 PhaseStats& phase) {
  result.maximum_chain =
      std::max(result.maximum_chain, static_cast<int>(move.waves.size()));
  phase.score_delta += move.score_delta;
  for (const Wave& wave : move.waves) {
    result.numbered_cleared += static_cast<std::uint64_t>(wave.cleared);
    result.covers_revealed += static_cast<std::uint64_t>(wave.revealed);
    phase.numbered_cleared += static_cast<std::uint64_t>(wave.cleared);
    phase.covers_revealed += static_cast<std::uint64_t>(wave.revealed);
  }
}

void reportGame(std::string_view phase_name, const PolicySpec& spec,
                const GameResult& result) {
  const std::lock_guard<std::mutex> lock(progress_mutex);
  std::cerr << phase_name << ' ' << spec.name << " seed 0x" << std::hex
            << result.seed << std::dec << ' ' << result.score << " ("
            << result.moves << " moves"
            << (result.censored ? ", capped" : "") << ", switches "
            << result.switches_from_d4 << ", fallbacks " << result.fallbacks
            << ", work " << result.work << ", cache "
            << result.peak_cache_entries << ")\n";
}

GameResult runPolicyGame(const PolicySpec& spec, std::uint32_t seed,
                         int maximum_moves, bool compare_to_d4,
                         std::string_view phase_name) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < maximum_moves) {
    const int phase_index = state.moves_remaining - 1;
    if (phase_index < 0 || phase_index >= kMovesPerLevel) {
      throw std::runtime_error("invalid moves-remaining phase");
    }
    std::optional<d4::SearchDecision> reference;
    if (compare_to_d4) reference = d4::chooseDepth4Action(state);
    const SearchDecision decision = choosePolicyAction(
        state, spec, reference ? &*reference : nullptr);
    if (!decision.complete || !decision.full_root ||
        !isLegal(state.board, decision.action)) {
      throw std::runtime_error("selective policy returned incomplete action");
    }
    PhaseStats& phase = result.phase[phase_index];
    ++phase.decisions;
    phase.policy_work += decision.work;
    phase.fallbacks += decision.used_fallback;
    result.fallbacks += decision.used_fallback;
    result.incomplete_selective += !decision.selective_complete &&
                                   spec.kind == PolicyKind::kUniformSelective;
    result.root_width_violations += !decision.full_root;
    result.work += decision.work;
    result.selective_work += decision.selective_work;
    result.fallback_work += decision.fallback_work;
    result.nodes += decision.nodes;
    result.cache_hits += decision.cache_hits;
    result.ordering_work += decision.ordering_work;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, decision.peak_cache_entries);
    if (reference) {
      if (!reference->complete || reference->completed_depth != 4) {
        throw std::runtime_error("diagnostic D4 reference was incomplete");
      }
      phase.d4_reference_work += reference->work;
      result.d4_reference_work += reference->work;
      const bool switched = decision.action != reference->action;
      phase.switches_from_d4 += switched;
      result.switches_from_d4 += switched;
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("selective policy transition failed");
    }
    observeMove(move, result, phase);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.peak_rss_bytes = peakRssBytes();
  result.elapsed_seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
  reportGame(phase_name, spec, result);
  return result;
}

struct Cohort {
  PolicySpec spec{};
  int maximum_moves = 0;
  std::vector<GameResult> games;
  double wall_seconds = 0.0;
};

Cohort runCohort(const PolicySpec& spec, std::uint32_t seed_start, int games,
                 int maximum_moves, bool compare_to_d4,
                 std::string_view phase_name) {
  const auto started = std::chrono::steady_clock::now();
  Cohort result;
  result.spec = spec;
  result.maximum_moves = maximum_moves;
  result.games.resize(games);
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= games) return;
        result.games[game] = runPolicyGame(
            spec, seed_start + static_cast<std::uint32_t>(game),
            maximum_moves, compare_to_d4, phase_name);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.wall_seconds = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started)
                            .count();
  return result;
}

struct Summary {
  int games = 0;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  int censored = 0;
  double mean_numbered_cleared = 0.0;
  double mean_covers_revealed = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double mean_maximum_chain = 0.0;
  std::uint64_t switches_from_d4 = 0;
  double switch_rate = 0.0;
  std::uint64_t fallbacks = 0;
  double fallback_rate = 0.0;
  std::uint64_t incomplete_selective = 0;
  std::uint64_t root_width_violations = 0;
  std::uint64_t work = 0;
  double work_per_move = 0.0;
  std::uint64_t selective_work = 0;
  std::uint64_t fallback_work = 0;
  std::uint64_t d4_reference_work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::uint64_t ordering_work = 0;
  std::size_t peak_cache_entries = 0;
  std::array<PhaseStats, kMovesPerLevel> phase{};
  double aggregate_game_seconds = 0.0;
  double moves_per_game_second = 0.0;
  std::uint64_t peak_rss_bytes = 0;
};

Summary summarize(const Cohort& cohort) {
  if (cohort.games.empty()) throw std::invalid_argument("empty cohort");
  Summary result;
  result.games = static_cast<int>(cohort.games.size());
  std::uint64_t moves = 0;
  std::uint64_t cleared = 0;
  std::uint64_t revealed = 0;
  for (const GameResult& game : cohort.games) {
    result.mean_score += static_cast<double>(game.score) / result.games;
    result.mean_moves += static_cast<double>(game.moves) / result.games;
    result.censored += game.censored;
    result.mean_numbered_cleared +=
        static_cast<double>(game.numbered_cleared) / result.games;
    result.mean_covers_revealed +=
        static_cast<double>(game.covers_revealed) / result.games;
    result.mean_maximum_chain +=
        static_cast<double>(game.maximum_chain) / result.games;
    result.switches_from_d4 += game.switches_from_d4;
    result.fallbacks += game.fallbacks;
    result.incomplete_selective += game.incomplete_selective;
    result.root_width_violations += game.root_width_violations;
    result.work += game.work;
    result.selective_work += game.selective_work;
    result.fallback_work += game.fallback_work;
    result.d4_reference_work += game.d4_reference_work;
    result.nodes += game.nodes;
    result.cache_hits += game.cache_hits;
    result.ordering_work += game.ordering_work;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, game.peak_cache_entries);
    result.aggregate_game_seconds += game.elapsed_seconds;
    result.peak_rss_bytes =
        std::max(result.peak_rss_bytes, game.peak_rss_bytes);
    moves += static_cast<std::uint64_t>(game.moves);
    cleared += game.numbered_cleared;
    revealed += game.covers_revealed;
    for (int phase = 0; phase < kMovesPerLevel; ++phase) {
      PhaseStats& target = result.phase[phase];
      const PhaseStats& source = game.phase[phase];
      target.decisions += source.decisions;
      target.switches_from_d4 += source.switches_from_d4;
      target.fallbacks += source.fallbacks;
      target.score_delta += source.score_delta;
      target.numbered_cleared += source.numbered_cleared;
      target.covers_revealed += source.covers_revealed;
      target.policy_work += source.policy_work;
      target.d4_reference_work += source.d4_reference_work;
    }
  }
  const double move_count = static_cast<double>(std::max<std::uint64_t>(1, moves));
  result.clears_per_move = cleared / move_count;
  result.reveals_per_move = revealed / move_count;
  result.switch_rate = result.switches_from_d4 / move_count;
  result.fallback_rate = result.fallbacks / move_count;
  result.work_per_move = result.work / move_count;
  result.moves_per_game_second =
      move_count / std::max(1.0e-9, result.aggregate_game_seconds);
  return result;
}

struct DifferenceStats {
  double mean = 0.0;
  double lower_95 = 0.0;
  int wins = 0;
  int ties = 0;
  int losses = 0;
};

DifferenceStats differences(const std::vector<double>& values) {
  if (values.empty()) throw std::invalid_argument("empty differences");
  DifferenceStats result;
  for (const double value : values) {
    result.mean += value / values.size();
    result.wins += value > 0;
    result.ties += value == 0;
    result.losses += value < 0;
  }
  double squares = 0.0;
  for (const double value : values) {
    squares += (value - result.mean) * (value - result.mean);
  }
  const double deviation = values.size() > 1
                               ? std::sqrt(squares / (values.size() - 1))
                               : 0.0;
  result.lower_95 =
      result.mean - 1.96 * deviation / std::sqrt(values.size());
  return result;
}

double lowerTailMean(std::vector<double> values, std::size_t divisor) {
  if (values.empty()) throw std::invalid_argument("empty lower tail");
  std::sort(values.begin(), values.end());
  const std::size_t count = std::max<std::size_t>(1, values.size() / divisor);
  double result = 0.0;
  for (std::size_t index = 0; index < count; ++index) {
    result += values[index] / count;
  }
  return result;
}

struct Comparison {
  DifferenceStats score;
  DifferenceStats moves;
  double baseline_score_lower_quartile = 0.0;
  double candidate_score_lower_quartile = 0.0;
  double baseline_moves_lower_quartile = 0.0;
  double candidate_moves_lower_quartile = 0.0;
  double mean_log_utility = 0.0;
  double worst_half_log_utility = 0.0;
  double robust_training_utility = 0.0;
  bool both_means_positive = false;
  bool no_lower_tail_collapse = false;
};

Comparison compare(const Cohort& baseline, const Cohort& candidate) {
  if (baseline.games.size() != candidate.games.size() ||
      baseline.games.empty()) {
    throw std::invalid_argument("unpaired cohorts");
  }
  std::vector<double> score_deltas;
  std::vector<double> move_deltas;
  std::vector<double> baseline_scores;
  std::vector<double> candidate_scores;
  std::vector<double> baseline_moves;
  std::vector<double> candidate_moves;
  std::vector<double> utilities;
  for (std::size_t index = 0; index < baseline.games.size(); ++index) {
    const GameResult& first = baseline.games[index];
    const GameResult& second = candidate.games[index];
    if (first.seed != second.seed) throw std::invalid_argument("seed mismatch");
    score_deltas.push_back(static_cast<double>(second.score - first.score));
    move_deltas.push_back(static_cast<double>(second.moves - first.moves));
    baseline_scores.push_back(static_cast<double>(first.score));
    candidate_scores.push_back(static_cast<double>(second.score));
    baseline_moves.push_back(static_cast<double>(first.moves));
    candidate_moves.push_back(static_cast<double>(second.moves));
    const double score_ratio =
        std::log((second.score + static_cast<double>(kLevelBonus)) /
                 (first.score + static_cast<double>(kLevelBonus)));
    const double move_ratio =
        std::log((second.moves + 1.0) / (first.moves + 1.0));
    utilities.push_back(0.5 * score_ratio + 0.5 * move_ratio);
  }
  Comparison result;
  result.score = differences(score_deltas);
  result.moves = differences(move_deltas);
  result.baseline_score_lower_quartile = lowerTailMean(baseline_scores, 4);
  result.candidate_score_lower_quartile = lowerTailMean(candidate_scores, 4);
  result.baseline_moves_lower_quartile = lowerTailMean(baseline_moves, 4);
  result.candidate_moves_lower_quartile = lowerTailMean(candidate_moves, 4);
  for (const double utility : utilities) {
    result.mean_log_utility += utility / utilities.size();
  }
  result.worst_half_log_utility = lowerTailMean(utilities, 2);
  result.robust_training_utility =
      0.5 * result.mean_log_utility + 0.5 * result.worst_half_log_utility;
  result.both_means_positive = result.score.mean > 0 && result.moves.mean > 0;
  result.no_lower_tail_collapse =
      result.candidate_score_lower_quartile >=
          kLowerTailFloorRatio * result.baseline_score_lower_quartile &&
      result.candidate_moves_lower_quartile >=
          kLowerTailFloorRatio * result.baseline_moves_lower_quartile;
  return result;
}

struct MenuResult {
  Cohort cohort;
  Summary summary;
  Comparison comparison;
};

bool betterTrainingChoice(const MenuResult& first,
                          const MenuResult& second) {
  constexpr double tolerance = 1.0e-12;
  if (first.comparison.robust_training_utility >
      second.comparison.robust_training_utility + tolerance) {
    return true;
  }
  if (second.comparison.robust_training_utility >
      first.comparison.robust_training_utility + tolerance) {
    return false;
  }
  if (first.summary.work_per_move != second.summary.work_per_move) {
    return first.summary.work_per_move < second.summary.work_per_move;
  }
  return std::string_view(first.cohort.spec.name) <
         std::string_view(second.cohort.spec.name);
}

void writePhase(std::ostream& output, const PhaseStats& phase) {
  output << "{\"decisions\":" << phase.decisions
         << ",\"switchesFromD4\":" << phase.switches_from_d4
         << ",\"fallbacks\":" << phase.fallbacks
         << ",\"scoreDelta\":" << phase.score_delta
         << ",\"numberedCleared\":" << phase.numbered_cleared
         << ",\"coversRevealed\":" << phase.covers_revealed
         << ",\"policyWork\":" << phase.policy_work
         << ",\"d4ReferenceWork\":" << phase.d4_reference_work << '}';
}

void writePhases(std::ostream& output,
                 const std::array<PhaseStats, kMovesPerLevel>& phases) {
  output << '[';
  for (int index = 0; index < kMovesPerLevel; ++index) {
    if (index != 0) output << ',';
    output << "{\"movesRemaining\":" << index + 1 << ",\"stats\":";
    writePhase(output, phases[index]);
    output << '}';
  }
  output << ']';
}

void writeSpec(std::ostream& output, const PolicySpec& spec) {
  const char* kind = spec.kind == PolicyKind::kExactD4
                         ? "exact-d4"
                         : spec.kind == PolicyKind::kUniformSelective
                               ? "uniform-selective"
                               : "phase-aligned";
  output << "{\"name\":\"" << spec.name << "\",\"kind\":\"" << kind
         << "\",\"depth\":" << spec.depth
         << ",\"internalWidth\":" << spec.internal_width << '}';
}

void writeGame(std::ostream& output, const GameResult& game) {
  output << "{\"seed\":" << game.seed << ",\"score\":" << game.score
         << ",\"moves\":" << game.moves
         << ",\"censored\":" << (game.censored ? "true" : "false")
         << ",\"numberedCleared\":" << game.numbered_cleared
         << ",\"coversRevealed\":" << game.covers_revealed
         << ",\"maximumChain\":" << game.maximum_chain
         << ",\"switchesFromD4\":" << game.switches_from_d4
         << ",\"fallbacks\":" << game.fallbacks
         << ",\"incompleteSelective\":" << game.incomplete_selective
         << ",\"rootWidthViolations\":" << game.root_width_violations
         << ",\"work\":" << game.work
         << ",\"selectiveWork\":" << game.selective_work
         << ",\"fallbackWork\":" << game.fallback_work
         << ",\"d4ReferenceWork\":" << game.d4_reference_work
         << ",\"nodes\":" << game.nodes
         << ",\"cacheHits\":" << game.cache_hits
         << ",\"orderingWork\":" << game.ordering_work
         << ",\"peakCacheEntries\":" << game.peak_cache_entries
         << ",\"peakRssBytes\":" << game.peak_rss_bytes
         << ",\"elapsedSeconds\":" << game.elapsed_seconds
         << ",\"byMovesRemaining\":";
  writePhases(output, game.phase);
  output << '}';
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"games\":" << summary.games
         << ",\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"censored\":" << summary.censored
         << ",\"meanNumberedCleared\":" << summary.mean_numbered_cleared
         << ",\"meanCoversRevealed\":" << summary.mean_covers_revealed
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"meanMaximumChain\":" << summary.mean_maximum_chain
         << ",\"switchesFromD4\":" << summary.switches_from_d4
         << ",\"switchRate\":" << summary.switch_rate
         << ",\"fallbacks\":" << summary.fallbacks
         << ",\"fallbackRate\":" << summary.fallback_rate
         << ",\"incompleteSelective\":" << summary.incomplete_selective
         << ",\"rootWidthViolations\":" << summary.root_width_violations
         << ",\"work\":" << summary.work
         << ",\"workPerMove\":" << summary.work_per_move
         << ",\"selectiveWork\":" << summary.selective_work
         << ",\"fallbackWork\":" << summary.fallback_work
         << ",\"d4ReferenceWork\":" << summary.d4_reference_work
         << ",\"nodes\":" << summary.nodes
         << ",\"cacheHits\":" << summary.cache_hits
         << ",\"orderingWork\":" << summary.ordering_work
         << ",\"peakCacheEntries\":" << summary.peak_cache_entries
         << ",\"aggregateGameSeconds\":" << summary.aggregate_game_seconds
         << ",\"movesPerGameSecond\":" << summary.moves_per_game_second
         << ",\"peakRssBytes\":" << summary.peak_rss_bytes
         << ",\"byMovesRemaining\":";
  writePhases(output, summary.phase);
  output << '}';
}

void writeDifference(std::ostream& output, const DifferenceStats& stats) {
  output << "{\"mean\":" << stats.mean << ",\"lower95\":"
         << stats.lower_95 << ",\"wins\":" << stats.wins
         << ",\"ties\":" << stats.ties << ",\"losses\":"
         << stats.losses << '}';
}

void writeComparison(std::ostream& output, const Comparison& comparison) {
  output << "{\"score\":";
  writeDifference(output, comparison.score);
  output << ",\"moves\":";
  writeDifference(output, comparison.moves);
  output << ",\"baselineScoreLowerQuartile\":"
         << comparison.baseline_score_lower_quartile
         << ",\"candidateScoreLowerQuartile\":"
         << comparison.candidate_score_lower_quartile
         << ",\"baselineMovesLowerQuartile\":"
         << comparison.baseline_moves_lower_quartile
         << ",\"candidateMovesLowerQuartile\":"
         << comparison.candidate_moves_lower_quartile
         << ",\"meanLogUtility\":" << comparison.mean_log_utility
         << ",\"worstHalfLogUtility\":"
         << comparison.worst_half_log_utility
         << ",\"robustTrainingUtility\":"
         << comparison.robust_training_utility
         << ",\"bothMeansPositive\":"
         << (comparison.both_means_positive ? "true" : "false")
         << ",\"noLowerTailCollapse\":"
         << (comparison.no_lower_tail_collapse ? "true" : "false") << '}';
}

void writeCohort(std::ostream& output, const Cohort& cohort,
                 const Summary& summary) {
  output << "{\"policy\":";
  writeSpec(output, cohort.spec);
  output << ",\"maximumMoves\":" << cohort.maximum_moves
         << ",\"wallSeconds\":" << cohort.wall_seconds
         << ",\"summary\":";
  writeSummary(output, summary);
  output << ",\"games\":[";
  for (std::size_t index = 0; index < cohort.games.size(); ++index) {
    if (index != 0) output << ',';
    writeGame(output, cohort.games[index]);
  }
  output << "]}";
}

struct EvaluationStage {
  Cohort baseline;
  Cohort candidate;
  Summary baseline_summary;
  Summary candidate_summary;
  Comparison comparison;
};

void writeStage(std::ostream& output, const EvaluationStage& stage) {
  output << "{\"baseline\":";
  writeCohort(output, stage.baseline, stage.baseline_summary);
  output << ",\"candidate\":";
  writeCohort(output, stage.candidate, stage.candidate_summary);
  output << ",\"comparison\":";
  writeComparison(output, stage.comparison);
  output << '}';
}

EvaluationStage runEvaluationStage(const PolicySpec& selected,
                                   std::uint32_t seed_start, int games,
                                   int maximum_moves,
                                   std::string_view phase_name) {
  EvaluationStage result;
  result.baseline = runCohort(kBaseline, seed_start, games, maximum_moves,
                              false, std::string(phase_name) + "-baseline");
  result.candidate = runCohort(selected, seed_start, games, maximum_moves,
                               true, std::string(phase_name) + "-candidate");
  result.baseline_summary = summarize(result.baseline);
  result.candidate_summary = summarize(result.candidate);
  result.comparison = compare(result.baseline, result.candidate);
  return result;
}

struct Options {
  std::string output = "/tmp/drop7-fair-selective-depth.json";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string argument = argv[index];
    if (argument == "--output") {
      result.output = argv[index + 1];
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  return result;
}

bool selfTest(std::ostream& output) {
  const bool baseline_test = d4::selfTest(output);
  State source = frozen::fixtureState(frozen::kTypeScriptFixtures[1]);
  // Leave three legal columns so the expensive invariants remain sanitizer
  // friendly while still exercising ranking and a complete selective D5 tree.
  for (const int column : {0, 1, 5, 6}) {
    for (int row = 0; row < kBoardSize; ++row) {
      source.board[indexOf(row, column)] = kSolid;
    }
  }
  source.game_over = false;
  source.moves_remaining = 5;
  const SearchDecision first = choosePolicyAction(source, kUniformMenu[0]);
  const SearchDecision repeat = choosePolicyAction(source, kUniformMenu[0]);
  State reflected = source;
  reflected.board = cfpi::detail::mirrorBoard(source.board);
  const SearchDecision mirror = choosePolicyAction(reflected, kUniformMenu[0]);
  State metadata = source;
  metadata.score = 9'999'999;
  metadata.level = 91;
  metadata.moves_played = 777;
  const SearchDecision metadata_result =
      choosePolicyAction(metadata, kUniformMenu[0]);
  const d4::SearchDecision depth4 = d4::chooseDepth4Action(source);
  const SearchDecision forced_fallback =
      choosePolicyAction(source, kUniformMenu[0], &depth4, 1);

  int legal_count = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    legal_count += isLegal(source.board, column);
  }
  const bool deterministic =
      first.action == repeat.action && first.work == repeat.work &&
      first.ordering_work == repeat.ordering_work &&
      first.cache_hits == repeat.cache_hits;
  const bool reflection_safe =
      mirror.action == kBoardSize - 1 - first.action &&
      mirror.work == first.work && mirror.ordering_work == first.ordering_work;
  const bool public_only = metadata_result.action == first.action &&
                           metadata_result.work == first.work;
  const bool complete = first.complete && first.selective_complete &&
                        !first.used_fallback && first.full_root;
  const bool legal = legal_count == 3 && isLegal(source.board, first.action) &&
                     isLegal(source.board, forced_fallback.action);
  const bool bounded = first.selective_work <= kMaximumSelectiveWork &&
                       first.peak_cache_entries <= kMaximumCacheEntries;
  const bool fallback_safe =
      forced_fallback.complete && forced_fallback.used_fallback &&
      forced_fallback.action == depth4.action &&
      forced_fallback.fallback_work == depth4.work;
  const bool completion_proven =
      kWorstD5W2Work < kMaximumSelectiveWork &&
      kWorstD5W2Cache < kMaximumCacheEntries;
  const bool protocol =
      kTrainingSeedStart == 0x3dd0'0000u &&
      kHeldoutSeedStart == 0x3dd1'0000u &&
      kScreenSeedStart == 0x3ea7'0000u &&
      kConfirmationSeedStart == 0x3ea8'0000u && kTrainingGames == 4 &&
      kHeldoutGames == 8 && kScreenGames == 8 &&
      kConfirmationGames == 16 && kTrainingMaximumMoves == 500 &&
      kEvaluationMaximumMoves == 1'000;
  const bool passed = baseline_test && deterministic && reflection_safe &&
                      public_only && complete && legal && bounded &&
                      fallback_safe && completion_proven && protocol;
  output << std::setprecision(12)
         << "FAIR_SELECTIVE_DEPTH_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"baselineTest\":" << (baseline_test ? "true" : "false")
         << ",\"deterministic\":" << (deterministic ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe ? "true" : "false")
         << ",\"publicMetadataAndGameSeedBlind\":"
         << (public_only ? "true" : "false")
         << ",\"complete\":" << (complete ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false")
         << ",\"fullRoot\":" << (first.full_root ? "true" : "false")
         << ",\"bounded\":" << (bounded ? "true" : "false")
         << ",\"fallbackCompletedD4\":"
         << (fallback_safe ? "true" : "false")
         << ",\"fixtureAction\":" << first.action
         << ",\"fixtureWork\":" << first.work
         << ",\"fixtureOrderingWork\":" << first.ordering_work
         << ",\"fixtureCacheEntries\":" << first.peak_cache_entries
         << ",\"worstD5W2Work\":" << kWorstD5W2Work
         << ",\"worstD5W2Cache\":" << kWorstD5W2Cache
         << ",\"maximumWork\":" << kMaximumSelectiveWork
         << ",\"maximumCache\":" << kMaximumCacheEntries << "}\n";
  return passed;
}

int run(const Options& options, std::ostream& output) {
  const auto started = std::chrono::steady_clock::now();
  const Cohort training_baseline =
      runCohort(kBaseline, kTrainingSeedStart, kTrainingGames,
                kTrainingMaximumMoves, false, "training-baseline");
  const Summary training_baseline_summary = summarize(training_baseline);

  std::vector<MenuResult> menu;
  menu.reserve(kUniformMenu.size() + 1);
  for (const PolicySpec& spec : kUniformMenu) {
    MenuResult result;
    result.cohort = runCohort(spec, kTrainingSeedStart, kTrainingGames,
                              kTrainingMaximumMoves, false, "training-menu");
    result.summary = summarize(result.cohort);
    result.comparison = compare(training_baseline, result.cohort);
    {
      const std::lock_guard<std::mutex> lock(progress_mutex);
      std::cerr << "fit " << spec.name << " score-delta "
                << result.comparison.score.mean << ", move-delta "
                << result.comparison.moves.mean << ", robust-utility "
                << result.comparison.robust_training_utility
                << ", work/move " << result.summary.work_per_move << '\n';
    }
    menu.push_back(std::move(result));
  }
  auto best = std::min_element(
      menu.begin(), menu.end(),
      [](const MenuResult& first, const MenuResult& second) {
        return betterTrainingChoice(first, second);
      });
  if (best == menu.end()) throw std::runtime_error("empty fit menu");
  const bool uniform_weak =
      best->comparison.robust_training_utility <= 0.0 ||
      !best->comparison.both_means_positive;
  if (uniform_weak) {
    MenuResult contingency;
    contingency.cohort =
        runCohort(kPhaseAligned, kTrainingSeedStart, kTrainingGames,
                  kTrainingMaximumMoves, false, "training-contingency");
    contingency.summary = summarize(contingency.cohort);
    contingency.comparison = compare(training_baseline, contingency.cohort);
    {
      const std::lock_guard<std::mutex> lock(progress_mutex);
      std::cerr << "fit " << kPhaseAligned.name << " score-delta "
                << contingency.comparison.score.mean << ", move-delta "
                << contingency.comparison.moves.mean << ", robust-utility "
                << contingency.comparison.robust_training_utility
                << ", work/move " << contingency.summary.work_per_move
                << '\n';
    }
    menu.push_back(std::move(contingency));
    best = std::min_element(
        menu.begin(), menu.end(),
        [](const MenuResult& first, const MenuResult& second) {
          return betterTrainingChoice(first, second);
        });
  }
  const PolicySpec selected = best->cohort.spec;
  {
    const std::lock_guard<std::mutex> lock(progress_mutex);
    std::cerr << "frozen selection " << selected.name << " (uniform weak "
              << (uniform_weak ? "yes" : "no") << ")\n";
  }
  const bool training_memory_ok = peakRssBytes() <= kResidentTargetBytes;

  std::optional<EvaluationStage> heldout;
  std::optional<EvaluationStage> screen;
  std::optional<EvaluationStage> confirmation;
  bool heldout_passed = false;
  bool screen_passed = false;
  bool confirmation_passed = false;
  if (training_memory_ok) {
    heldout = runEvaluationStage(selected, kHeldoutSeedStart, kHeldoutGames,
                                 kTrainingMaximumMoves, "heldout");
    heldout_passed = heldout->comparison.both_means_positive &&
                     heldout->comparison.no_lower_tail_collapse &&
                     heldout->candidate_summary.root_width_violations == 0 &&
                     peakRssBytes() <= kResidentTargetBytes;
  }
  if (heldout_passed) {
    screen = runEvaluationStage(selected, kScreenSeedStart, kScreenGames,
                                kEvaluationMaximumMoves, "screen");
    screen_passed = screen->comparison.both_means_positive &&
                    peakRssBytes() <= kResidentTargetBytes;
  }
  if (screen_passed) {
    confirmation = runEvaluationStage(
        selected, kConfirmationSeedStart, kConfirmationGames,
        kEvaluationMaximumMoves, "confirmation");
    confirmation_passed =
        confirmation->comparison.both_means_positive &&
        confirmation->comparison.no_lower_tail_collapse &&
        peakRssBytes() <= kResidentTargetBytes;
  }
  const double total_wall = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - started)
                                .count();

  std::ofstream artifact(options.output);
  if (!artifact) throw std::runtime_error("could not open selective artifact");
  artifact << std::setprecision(12)
           << "{\n  \"experiment\":\"fair-public-selective-depth\",\n"
           << "  \"preregistered\":true,\n"
           << "  \"publicStateOnly\":true,\n"
           << "  \"gameSeedVisibleToSearch\":false,\n"
           << "  \"search\":{\"chanceSamples\":" << kChanceSamples
           << ",\"ordering\":\"exact-d1-fair-five-stratum\""
           << ",\"rootFullWidth\":true,\"maximumSelectiveWork\":"
           << kMaximumSelectiveWork << ",\"maximumCacheEntries\":"
           << kMaximumCacheEntries << ",\"residentTargetBytes\":"
           << kResidentTargetBytes << ",\"fallback\":\"completed-fair-d4\""
           << ",\"worstD5W2Work\":" << kWorstD5W2Work
           << ",\"worstD5W2Cache\":" << kWorstD5W2Cache << "},\n"
           << "  \"selection\":{\"metric\":\"half mean plus half worst-half paired equal-log-score-moves utility\""
           << ",\"lowerTailFloorRatio\":" << kLowerTailFloorRatio
           << ",\"uniformWeak\":" << (uniform_weak ? "true" : "false")
           << ",\"phaseContingencyRan\":"
           << (uniform_weak ? "true" : "false")
           << ",\"selected\":";
  writeSpec(artifact, selected);
  artifact << "},\n  \"training\":{\"seedStart\":" << kTrainingSeedStart
           << ",\"baseline\":";
  writeCohort(artifact, training_baseline, training_baseline_summary);
  artifact << ",\"menu\":[";
  for (std::size_t index = 0; index < menu.size(); ++index) {
    if (index != 0) artifact << ',';
    artifact << "{\"cohort\":";
    writeCohort(artifact, menu[index].cohort, menu[index].summary);
    artifact << ",\"comparison\":";
    writeComparison(artifact, menu[index].comparison);
    artifact << '}';
  }
  artifact << "]},\n  \"trainingMemoryOk\":"
           << (training_memory_ok ? "true" : "false")
           << ",\n  \"heldout\":";
  if (heldout) writeStage(artifact, *heldout); else artifact << "null";
  artifact << ",\n  \"heldoutPassed\":"
           << (heldout_passed ? "true" : "false")
           << ",\n  \"screen\":";
  if (screen) writeStage(artifact, *screen); else artifact << "null";
  artifact << ",\n  \"screenPassed\":"
           << (screen_passed ? "true" : "false")
           << ",\n  \"confirmation\":";
  if (confirmation) writeStage(artifact, *confirmation);
  else artifact << "null";
  artifact << ",\n  \"confirmationPassed\":"
           << (confirmation_passed ? "true" : "false")
           << ",\n  \"qualified\":"
           << (heldout_passed && screen_passed && confirmation_passed
                   ? "true"
                   : "false")
           << ",\n  \"peakRssBytes\":" << peakRssBytes()
           << ",\n  \"totalWallSeconds\":" << total_wall << "\n}\n";
  artifact.close();

  output << std::fixed << std::setprecision(6)
         << "FAIR_SELECTIVE_DEPTH_RESULT {\"selected\":\"" << selected.name
         << "\",\"uniformWeak\":" << (uniform_weak ? "true" : "false")
         << ",\"trainingUtility\":"
         << best->comparison.robust_training_utility
         << ",\"heldoutPassed\":" << (heldout_passed ? "true" : "false")
         << ",\"screenRan\":" << (screen ? "true" : "false")
         << ",\"screenPassed\":" << (screen_passed ? "true" : "false")
         << ",\"confirmationRan\":"
         << (confirmation ? "true" : "false")
         << ",\"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"totalWallSeconds\":" << total_wall
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return 0;
}

}  // namespace drop7::fair_selective_depth

#ifndef DROP7_FAIR_SELECTIVE_DEPTH_NO_MAIN
int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::fair_selective_depth::selfTest(std::cout) ? EXIT_SUCCESS
                                                              : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::fair_selective_depth::parseOptions(argc, argv, 2);
      return drop7::fair_selective_depth::run(options, std::cout);
    }
    std::cerr << "usage: drop7_fair_selective_depth --self-test | --run "
                 "[--output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
#endif
