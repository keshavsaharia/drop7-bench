#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#define DROP7_FAIR_ONLY_DEPTH4_NO_MAIN
#include "../../fair-expectimax/reference/fair-only-depth4.cpp"
#undef DROP7_FAIR_ONLY_DEPTH4_NO_MAIN
#undef DROP7_FAIR_ONLY_DEPTH4_LIBRARY

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <type_traits>
#include <utility>
#include <vector>

// Runs B0 information relaxation over the previously evaluated h200 sibling
// corpus.  The planner may see one synthetic tape while constructing a path,
// but every such advantage is charged by the one-step martingale difference
//
//   z = (reward + V(next)) - E[reward + V(next) | public state, action].
//
// V is the fixed public fair leaf used by fair-D4.  Planner realizations and
// their penalty expectations share one explicit seven-outcome conditional
// transition kernel; otherwise the martingale need not be mean-zero when a
// cascade consumes multiple coupled reveal draws.  Only the held-out
// evaluation panel uses separate event-keyed random domains.  Search and
// continuation APIs accept PublicState, so score, origin, history, scenario,
// and tape identity cannot enter a public decision.
namespace drop7::martingale_dual_b0 {

namespace d1 = drop7::fair_only_horizon;
namespace d4 = drop7::fair_only_depth4;
namespace detail = drop7::cfpi::detail;
using Clock = std::chrono::steady_clock;

constexpr int kExpectedRecords = 477;
constexpr int kExpectedGames = 8;
constexpr std::uint32_t kExpectedGameStart = 0x3d6d'0010u;
constexpr std::string_view kExpectedCorpusSha256 =
    "bfda8ae32fa0be3577c6b27f6413aba28f2854930e2f91a0dcf7674808f04196";
constexpr std::string_view kProtocol =
    "drop7-martingale-dual-b0/h12/b8/shared-kernel7/evaluation7/fair-d4-leaf/v2";

constexpr int kHorizon = 12;
constexpr int kBeamWidth = 8;
constexpr int kPlannerScenarios = 7;
constexpr int kPenaltySamples = 7;
constexpr int kEvaluationScenarios = 7;
constexpr int kEventsPerStep = 128;
constexpr double kWallLimitSeconds = 45.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr double kTieTolerance = 1.0e-9;

// The public kernel is deliberately unrelated to the engine's gameplay
// domains.  Planner and penalty lanes use these same domains and conditional
// seed: the distinction between those lanes is accounting, not probability.
constexpr std::uint32_t kKernelRevealDomain = 0x4230'4b52u;      // B0KR
constexpr std::uint32_t kKernelVisibleDomain = 0x4230'4b56u;     // B0KV
constexpr std::uint32_t kEvaluationRevealDomain = 0x4230'4552u;  // B0ER
constexpr std::uint32_t kEvaluationVisibleDomain = 0x4230'4556u; // B0EV
constexpr std::uint32_t kKernelSeedDomain = 0x4b45'524eu;
constexpr std::uint32_t kPlannerSelectorDomain = 0x5345'4c45u;
constexpr std::uint32_t kEvaluationSeedDomain = 0x4556'414cu;
constexpr std::array<int, kBoardSize> kActionOrder{{3, 2, 4, 1, 5, 0, 6}};

constexpr std::array<std::uint32_t, 5> kRandomDomains{{
    kKernelRevealDomain, kKernelVisibleDomain, kEvaluationRevealDomain,
    kEvaluationVisibleDomain, kPlannerSelectorDomain,
}};

consteval bool distinctRandomDomains() {
  for (std::size_t first = 0; first < kRandomDomains.size(); ++first) {
    for (std::size_t second = first + 1; second < kRandomDomains.size(); ++second) {
      if (kRandomDomains[first] == kRandomDomains[second]) return false;
    }
    if (kRandomDomains[first] == kNextDiscDomain ||
        kRandomDomains[first] == kRevealDomain ||
        kRandomDomains[first] == detail::kRevealSampleDomain ||
        kRandomDomains[first] == detail::kDiscSampleDomain) {
      return false;
    }
  }
  return true;
}

static_assert(distinctRandomDomains());
static_assert(kPlannerScenarios % kBoardSize == 0);
static_assert(kPenaltySamples % kBoardSize == 0);
static_assert(kEvaluationScenarios % kBoardSize == 0);
static_assert(kLevelBonus == 17'000);
static_assert(d4::kCandidateDepth == 4);

struct Config {
  int horizon = kHorizon;
  int beam_width = kBeamWidth;
  int planner_scenarios = kPlannerScenarios;
  int penalty_samples = kPenaltySamples;
  int evaluation_scenarios = kEvaluationScenarios;
};

void validateConfig(const Config& config) {
  if (config.horizon < 1 || config.horizon > kHorizon ||
      config.beam_width < 1 || config.beam_width > kBeamWidth ||
      config.planner_scenarios != kPlannerScenarios ||
      config.penalty_samples != kPenaltySamples ||
      config.evaluation_scenarios != kEvaluationScenarios) {
    throw std::invalid_argument("invalid bounded B0 configuration");
  }
}

struct PublicState {
  Board board{};
  std::uint8_t next_disc = 1;
  std::uint8_t moves_remaining = kMovesPerLevel;
  bool terminal = false;

  bool operator==(const PublicState&) const = default;
};

bool validMovesRemaining(int moves_remaining, bool terminal) {
  const int minimum = terminal ? 0 : 1;
  return moves_remaining >= minimum && moves_remaining <= kMovesPerLevel;
}

void validatePublicFields(int next_disc, int moves_remaining, bool terminal) {
  if (next_disc < 1 || next_disc > kBoardSize ||
      !validMovesRemaining(moves_remaining, terminal)) {
    std::ostringstream message;
    message << "invalid public-state fields: nextDisc=" << next_disc
            << ", movesRemaining=" << moves_remaining
            << ", terminal=" << (terminal ? "true" : "false");
    throw std::invalid_argument(message.str());
  }
}

State materialize(const PublicState& source) {
  validatePublicFields(source.next_disc, source.moves_remaining,
                       source.terminal);
  State result;
  result.board = source.board;
  result.next_disc = source.next_disc;
  result.score = 0;
  result.level = 1;
  result.moves_remaining = source.moves_remaining;
  result.moves_played = 0;
  result.game_over = source.terminal;
  return result;
}

PublicState publicState(const State& source) {
  validatePublicFields(source.next_disc, source.moves_remaining,
                       source.game_over);
  for (const std::uint8_t cell : source.board) {
    if (cell > kCracked) {
      throw std::invalid_argument("invalid public board token");
    }
  }
  return {source.board, source.next_disc,
          static_cast<std::uint8_t>(source.moves_remaining), source.game_over};
}

PublicState mirror(const PublicState& source) {
  PublicState result = source;
  result.board = detail::mirrorBoard(source.board);
  return result;
}

PublicState canonicalPublic(const PublicState& source, bool& mirrored) {
  return publicState(detail::canonicalState(materialize(source), mirrored));
}

PublicState canonicalPublic(const PublicState& source) {
  bool ignored = false;
  return canonicalPublic(source, ignored);
}

std::uint64_t mix64(std::uint64_t value) {
  value ^= value >> 30u;
  value *= 0xbf58'476d'1ce4'e5b9ull;
  value ^= value >> 27u;
  value *= 0x94d0'49bb'1331'11ebull;
  return value ^ (value >> 31u);
}

std::uint64_t publicHash(const PublicState& source) {
  const PublicState state = canonicalPublic(source);
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint64_t>(cell + 1u);
    hash *= 0x0000'0100'0000'01b3ull;
  }
  hash ^= state.next_disc;
  hash *= 0x0000'0100'0000'01b3ull;
  hash ^= static_cast<std::uint64_t>(state.moves_remaining + 1u);
  hash *= 0x0000'0100'0000'01b3ull;
  hash ^= static_cast<std::uint64_t>(state.terminal);
  return mix64(hash);
}

std::uint32_t foldSeed(std::uint64_t hash, std::uint32_t domain) {
  return mix32(static_cast<std::uint32_t>(hash) ^
               static_cast<std::uint32_t>(hash >> 32u) ^ domain);
}

double publicFairValue(const PublicState& source) {
  return d1::fairLeaf(materialize(canonicalPublic(source)));
}

enum class Lane : std::size_t { planner = 0, penalty = 1, evaluation = 2 };

struct ChanceAccounting {
  std::array<std::uint64_t, 3> transitions{};
  std::array<std::uint64_t, 3> reveal_draws{};
  std::array<std::uint64_t, 3> visible_draws{};
  std::uint64_t penalty_queries = 0;
  std::uint64_t public_d1_calls = 0;
  std::uint64_t public_d1_work = 0;

  ChanceAccounting& operator+=(const ChanceAccounting& other) {
    for (std::size_t lane = 0; lane < transitions.size(); ++lane) {
      transitions[lane] += other.transitions[lane];
      reveal_draws[lane] += other.reveal_draws[lane];
      visible_draws[lane] += other.visible_draws[lane];
    }
    penalty_queries += other.penalty_queries;
    public_d1_calls += other.public_d1_calls;
    public_d1_work += other.public_d1_work;
    return *this;
  }
};

struct TapeDomains {
  std::uint32_t reveal = 0;
  std::uint32_t visible = 0;
  Lane lane = Lane::planner;
};

constexpr TapeDomains kPlannerKernelDomains{
    kKernelRevealDomain, kKernelVisibleDomain, Lane::planner};
constexpr TapeDomains kPenaltyKernelDomains{
    kKernelRevealDomain, kKernelVisibleDomain, Lane::penalty};
constexpr TapeDomains kEvaluationDomains{
    kEvaluationRevealDomain, kEvaluationVisibleDomain, Lane::evaluation};

struct PublicTransition {
  PublicState next{};
  double reward = 0.0;
  int reveal_events = 0;
  bool played = false;
};

PublicTransition playPublicMove(const PublicState& source, int action,
                                std::uint32_t seed, int sample, int count,
                                int step, TapeDomains domains,
                                ChanceAccounting& accounting) {
  if (source.terminal || !isLegal(source.board, action) || sample < 0 ||
      sample >= count || count < 1 || step < 0 || step >= kHorizon) {
    throw std::invalid_argument("invalid public synthetic transition");
  }
  Board board = source.board;
  if (!placeDisc(board, action, source.next_disc)) {
    throw std::runtime_error("legal public move could not be placed");
  }

  const std::size_t lane = static_cast<std::size_t>(domains.lane);
  struct EventTape {
    std::uint32_t seed = 0;
    int sample = 0;
    int count = 1;
    std::uint32_t domain = 0;
    int step = 0;
    int event = 0;
    ChanceAccounting* accounting = nullptr;
    std::size_t lane = 0;

    std::uint8_t nextDisc() {
      if (event >= kEventsPerStep) {
        throw std::runtime_error("B0 reveal event slice exhausted");
      }
      ++accounting->reveal_draws[lane];
      const int event_key = step * kEventsPerStep + event++;
      const double unit = detail::stratifiedUnit(
          seed, sample, count, domain, event_key);
      return static_cast<std::uint8_t>(
          std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
    }
  } reveals{seed, sample, count, domains.reveal, step, 0, &accounting, lane};

  MoveResult move;
  std::int64_t score = 0;
  detail::resolveCascadeSampled(board, reveals, 1, score, move.waves);
  move.score_delta = score;
  move.cleared_board = isBoardEmpty(board);
  if (move.cleared_board) move.score_delta += kClearBonus;

  int moves_remaining = static_cast<int>(source.moves_remaining) - 1;
  bool terminal = false;
  if (moves_remaining == 0) {
    Board raised{};
    if (!raiseCoveredRow(board, raised)) {
      terminal = true;
    } else {
      move.level_advanced = true;
      moves_remaining = kMovesPerLevel;
      move.score_delta += kLevelBonus;
      board = raised;
      std::int64_t rise_score = 0;
      const int next_depth =
          move.waves.empty() ? 1 : move.waves.back().depth + 1;
      detail::resolveCascadeSampled(board, reveals, next_depth, rise_score,
                                    move.waves);
      move.score_delta += rise_score;
      if (isBoardEmpty(board)) {
        move.score_delta += kClearBonus;
        move.cleared_board = true;
      }
    }
  }
  int legal_count = 0;
  legalColumns(board, legal_count);
  if (!terminal && legal_count == 0) terminal = true;

  PublicTransition result;
  result.reward = static_cast<double>(move.score_delta);
  result.next.board = board;
  result.next.next_disc = source.next_disc;
  result.next.moves_remaining = static_cast<std::uint8_t>(moves_remaining);
  result.next.terminal = terminal;
  result.reveal_events = reveals.event;
  result.played = true;
  if (!terminal) {
    ++accounting.visible_draws[lane];
    const double unit = detail::stratifiedUnit(
        seed, sample, count, domains.visible, step);
    result.next.next_disc = static_cast<std::uint8_t>(
        std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
  }
  ++accounting.transitions[lane];
  return result;
}

double transitionPotential(const PublicTransition& transition) {
  return transition.reward + publicFairValue(transition.next);
}

struct CanonicalStateAction {
  PublicState state{};
  int action = -1;
};

CanonicalStateAction canonicalStateAction(const PublicState& source,
                                          int action) {
  bool mirrored = false;
  CanonicalStateAction result;
  result.state = canonicalPublic(source, mirrored);
  result.action = mirrored ? kBoardSize - 1 - action : action;
  if (!isLegal(result.state.board, result.action)) {
    throw std::invalid_argument("empirical kernel received illegal action");
  }
  return result;
}

std::uint32_t kernelSeed(const CanonicalStateAction& source) {
  return mix32(
      foldSeed(publicHash(source.state), kKernelSeedDomain) ^
      (static_cast<std::uint32_t>(source.action + 1) * 0x9e37'79b9u));
}

PublicTransition empiricalKernelTransition(const PublicState& source,
                                           int action, int support_index,
                                           Lane lane,
                                           ChanceAccounting& accounting) {
  if (support_index < 0 || support_index >= kPenaltySamples ||
      lane == Lane::evaluation) {
    throw std::invalid_argument("invalid empirical-kernel request");
  }
  const CanonicalStateAction canonical = canonicalStateAction(source, action);
  const TapeDomains domains =
      lane == Lane::planner ? kPlannerKernelDomains : kPenaltyKernelDomains;
  PublicTransition result = playPublicMove(
      canonical.state, canonical.action, kernelSeed(canonical), support_index,
      kPenaltySamples, 0, domains, accounting);
  result.next = canonicalPublic(result.next);
  return result;
}

double conditionalExpectedPotential(const PublicState& source, int action,
                                    const Config& config,
                                    ChanceAccounting& accounting) {
  ++accounting.penalty_queries;
  double result = 0.0;
  for (int support = 0; support < config.penalty_samples; ++support) {
    const PublicTransition transition = empiricalKernelTransition(
        source, action, support, Lane::penalty, accounting);
    result += transitionPotential(transition) /
              static_cast<double>(config.penalty_samples);
  }
  return result;
}

int plannerSupportIndex(const PublicState& canonical_root,
                        const PublicState& source, int action, int scenario,
                        int step) {
  if (scenario < 0 || scenario >= kPlannerScenarios || step < 0 ||
      step >= kHorizon) {
    throw std::invalid_argument("invalid planner support selector");
  }
  const CanonicalStateAction canonical = canonicalStateAction(source, action);
  const std::uint64_t event_key =
      publicHash(canonical_root) ^
      std::rotl(publicHash(canonical.state), 19) ^
      (static_cast<std::uint64_t>(canonical.action + 1) << 48u) ^
      (static_cast<std::uint64_t>(step + 1) * 0x9e37'79b9'7f4a'7c15ull);
  const int rotation = static_cast<int>(
      foldSeed(mix64(event_key), kPlannerSelectorDomain) % kPenaltySamples);
  return (scenario + rotation) % kPenaltySamples;
}

struct BeamNode {
  PublicState state{};
  double corrected_reward = 0.0;
  double raw_reward = 0.0;
  double penalty = 0.0;
  std::uint64_t path = 0;
};

double beamKey(const BeamNode& node) {
  return node.corrected_reward + publicFairValue(node.state);
}

bool betterNode(const BeamNode& first, const BeamNode& second) {
  const double first_key = beamKey(first);
  const double second_key = beamKey(second);
  if (first_key != second_key) return first_key > second_key;
  if (first.raw_reward != second.raw_reward) {
    return first.raw_reward > second.raw_reward;
  }
  return first.path < second.path;
}

double planScenario(const PublicState& canonical_root, int root_action,
                    int scenario, const Config& config,
                    ChanceAccounting& accounting) {
  if (!isLegal(canonical_root.board, root_action) || scenario < 0 ||
      scenario >= config.planner_scenarios) {
    throw std::invalid_argument("invalid planner scenario");
  }
  std::vector<BeamNode> beam{{canonical_root, 0.0, 0.0, 0.0, 0}};
  for (int step = 0; step < config.horizon; ++step) {
    std::vector<BeamNode> expanded;
    expanded.reserve(static_cast<std::size_t>(config.beam_width * kBoardSize));
    bool any_live = false;
    for (const BeamNode& node : beam) {
      if (node.state.terminal) {
        expanded.push_back(node);
        continue;
      }
      any_live = true;
      for (const int action : kActionOrder) {
        if (step == 0 && action != root_action) continue;
        if (!isLegal(node.state.board, action)) continue;
        const int support = plannerSupportIndex(
            canonical_root, node.state, action, scenario, step);
        const PublicTransition transition = empiricalKernelTransition(
            node.state, action, support, Lane::planner, accounting);
        const double expected = conditionalExpectedPotential(
            node.state, action, config, accounting);
        const double martingale = transitionPotential(transition) - expected;
        BeamNode candidate;
        candidate.state = canonicalPublic(transition.next);
        candidate.corrected_reward =
            node.corrected_reward + transition.reward - martingale;
        candidate.raw_reward = node.raw_reward + transition.reward;
        candidate.penalty = node.penalty + martingale;
        candidate.path = node.path * 8u + static_cast<std::uint64_t>(action + 1);
        expanded.push_back(candidate);
      }
    }
    if (!any_live) break;
    if (expanded.empty()) {
      throw std::runtime_error("planner exhausted a legal beam");
    }
    std::stable_sort(expanded.begin(), expanded.end(), betterNode);
    std::vector<BeamNode> unique;
    unique.reserve(static_cast<std::size_t>(config.beam_width));
    for (const BeamNode& candidate : expanded) {
      bool duplicate = false;
      for (const BeamNode& retained : unique) {
        if (candidate.state == retained.state) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) unique.push_back(candidate);
      if (static_cast<int>(unique.size()) == config.beam_width) break;
    }
    beam = std::move(unique);
  }
  if (beam.empty()) throw std::runtime_error("empty final planner beam");
  return beamKey(*std::max_element(
      beam.begin(), beam.end(),
      [](const BeamNode& first, const BeamNode& second) {
        return betterNode(second, first);
      }));
}

int publicD1Action(const PublicState& source, ChanceAccounting& accounting) {
  if (source.terminal) return -1;
  bool mirrored = false;
  const State canonical = detail::canonicalState(materialize(source), mirrored);
  d1::SearchContext context;
  const d1::RootEvaluation decision = d1::rootDecision(canonical, 1, context);
  ++accounting.public_d1_calls;
  accounting.public_d1_work += context.work;
  if (decision.action < 0) {
    throw std::runtime_error("public fair-D1 continuation did not complete");
  }
  return mirrored ? kBoardSize - 1 - decision.action : decision.action;
}

double evaluateScenario(const PublicState& canonical_root, int root_action,
                        int scenario, const Config& config,
                        ChanceAccounting& accounting) {
  const std::uint32_t seed =
      foldSeed(publicHash(canonical_root), kEvaluationSeedDomain);
  PublicState state = canonical_root;
  double result = 0.0;
  for (int step = 0; step < config.horizon && !state.terminal; ++step) {
    const int action = step == 0 ? root_action
                                 : publicD1Action(state, accounting);
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("public fair-D1 selected an illegal action");
    }
    const PublicTransition transition = playPublicMove(
        state, action, seed, scenario, config.evaluation_scenarios, step,
        kEvaluationDomains, accounting);
    result += transition.reward;
    state = canonicalPublic(transition.next);
  }
  return result + publicFairValue(state);
}

struct RankResult {
  int action = -1;
  std::array<int, 2> split_actions{{-1, -1}};
  std::array<bool, kBoardSize> legal{};
  std::array<double, kBoardSize> dual_values{};
  std::array<double, kBoardSize> evaluation_values{};
  ChanceAccounting accounting{};
};

int bestAction(const std::array<double, kBoardSize>& values,
               const std::array<bool, kBoardSize>& legal) {
  int result = -1;
  double best = -std::numeric_limits<double>::infinity();
  for (const int action : kActionOrder) {
    if (!legal[action]) continue;
    if (values[action] > best) {
      best = values[action];
      result = action;
    }
  }
  return result;
}

RankResult rankCanonical(const PublicState& root, const Config& config) {
  validateConfig(config);
  if (root.terminal) throw std::invalid_argument("terminal B0 root");
  RankResult result;
  result.dual_values.fill(-std::numeric_limits<double>::infinity());
  result.evaluation_values.fill(-std::numeric_limits<double>::infinity());
  std::array<std::array<double, kBoardSize>, 2> split_sums{};
  std::array<int, 2> split_counts{};
  for (int scenario = 0; scenario < config.planner_scenarios; ++scenario) {
    ++split_counts[scenario & 1];
  }
  for (int action = 0; action < kBoardSize; ++action) {
    result.legal[action] = isLegal(root.board, action);
    if (!result.legal[action]) continue;
    double planner_sum = 0.0;
    for (int scenario = 0; scenario < config.planner_scenarios; ++scenario) {
      const double value =
          planScenario(root, action, scenario, config, result.accounting);
      planner_sum += value;
      const int split = scenario & 1;
      split_sums[split][action] += value;
    }
    result.dual_values[action] =
        planner_sum / static_cast<double>(config.planner_scenarios);
    double evaluation_sum = 0.0;
    for (int scenario = 0; scenario < config.evaluation_scenarios; ++scenario) {
      evaluation_sum += evaluateScenario(root, action, scenario, config,
                                          result.accounting);
    }
    result.evaluation_values[action] =
        evaluation_sum / static_cast<double>(config.evaluation_scenarios);
  }
  result.action = bestAction(result.dual_values, result.legal);
  for (int split = 0; split < 2; ++split) {
    std::array<double, kBoardSize> values{};
    values.fill(-std::numeric_limits<double>::infinity());
    if (split_counts[split] == 0) {
      throw std::runtime_error("empty planner split");
    }
    for (int action = 0; action < kBoardSize; ++action) {
      if (result.legal[action]) {
        values[action] = split_sums[split][action] /
                         static_cast<double>(split_counts[split]);
      }
    }
    result.split_actions[split] = bestAction(values, result.legal);
  }
  if (result.action < 0) throw std::runtime_error("B0 ranked no legal action");
  const std::size_t planner = static_cast<std::size_t>(Lane::planner);
  const std::size_t penalty = static_cast<std::size_t>(Lane::penalty);
  if (result.accounting.penalty_queries !=
          result.accounting.transitions[planner] ||
      result.accounting.transitions[penalty] !=
          result.accounting.penalty_queries *
              static_cast<std::uint64_t>(config.penalty_samples)) {
    throw std::runtime_error("inexact martingale chance accounting");
  }
  return result;
}

RankResult rankRoot(const PublicState& source, const Config& config = {}) {
  bool mirrored = false;
  const PublicState canonical = canonicalPublic(source, mirrored);
  RankResult ranked = rankCanonical(canonical, config);
  if (!mirrored) return ranked;
  RankResult result;
  result.action = kBoardSize - 1 - ranked.action;
  result.split_actions = {{kBoardSize - 1 - ranked.split_actions[0],
                           kBoardSize - 1 - ranked.split_actions[1]}};
  result.accounting = ranked.accounting;
  result.dual_values.fill(-std::numeric_limits<double>::infinity());
  result.evaluation_values.fill(-std::numeric_limits<double>::infinity());
  for (int canonical_action = 0; canonical_action < kBoardSize;
       ++canonical_action) {
    const int source_action = kBoardSize - 1 - canonical_action;
    result.legal[source_action] = ranked.legal[canonical_action];
    result.dual_values[source_action] = ranked.dual_values[canonical_action];
    result.evaluation_values[source_action] =
        ranked.evaluation_values[canonical_action];
  }
  return result;
}

using PublicRanker = RankResult (*)(const PublicState&, const Config&);
static_assert(std::is_same_v<decltype(&rankRoot), PublicRanker>);
static_assert(!std::is_invocable_v<PublicRanker, const State&, const Config&>);

std::uint64_t peakRssBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ull;
#endif
}

void enforceRss() {
  if (peakRssBytes() > kRssLimitBytes) {
    throw std::runtime_error("B0 exceeded its 256 MiB RSS bound");
  }
}

struct Deadline {
  Clock::time_point started = Clock::now();

  double seconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }

  void check() const {
    if (seconds() > kWallLimitSeconds) {
      throw std::runtime_error("B0 exceeded its 45 minute wall bound");
    }
  }
};

std::string readWholeFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not read " + path);
  std::ostringstream output;
  output << input.rdbuf();
  if (!input.good() && !input.eof()) {
    throw std::runtime_error("failed reading " + path);
  }
  return output.str();
}

constexpr std::array<std::uint32_t, 64> kSha256Constants{{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
}};

std::string sha256(std::string_view source) {
  std::vector<std::uint8_t> message(source.begin(), source.end());
  const std::uint64_t bit_length =
      static_cast<std::uint64_t>(message.size()) * 8u;
  message.push_back(0x80u);
  while (message.size() % 64 != 56) message.push_back(0u);
  for (int byte = 7; byte >= 0; --byte) {
    message.push_back(static_cast<std::uint8_t>(bit_length >> (byte * 8)));
  }
  std::array<std::uint32_t, 8> hash{{
      0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
  }};
  for (std::size_t offset = 0; offset < message.size(); offset += 64) {
    std::array<std::uint32_t, 64> words{};
    for (int word = 0; word < 16; ++word) {
      const std::size_t at = offset + static_cast<std::size_t>(word * 4);
      words[word] = (static_cast<std::uint32_t>(message[at]) << 24) |
                    (static_cast<std::uint32_t>(message[at + 1]) << 16) |
                    (static_cast<std::uint32_t>(message[at + 2]) << 8) |
                    static_cast<std::uint32_t>(message[at + 3]);
    }
    for (int word = 16; word < 64; ++word) {
      const std::uint32_t s0 = std::rotr(words[word - 15], 7) ^
                               std::rotr(words[word - 15], 18) ^
                               (words[word - 15] >> 3);
      const std::uint32_t s1 = std::rotr(words[word - 2], 17) ^
                               std::rotr(words[word - 2], 19) ^
                               (words[word - 2] >> 10);
      words[word] = words[word - 16] + s0 + words[word - 7] + s1;
    }
    std::uint32_t a = hash[0], b = hash[1], c = hash[2], d = hash[3];
    std::uint32_t e = hash[4], f = hash[5], g = hash[6], h = hash[7];
    for (int round = 0; round < 64; ++round) {
      const std::uint32_t upper =
          std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
      const std::uint32_t choose = (e & f) ^ (~e & g);
      const std::uint32_t first =
          h + upper + choose + kSha256Constants[round] + words[round];
      const std::uint32_t lower =
          std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t second = lower + majority;
      h = g;
      g = f;
      f = e;
      e = d + first;
      d = c;
      c = b;
      b = a;
      a = first + second;
    }
    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
    hash[5] += f;
    hash[6] += g;
    hash[7] += h;
  }
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(64);
  for (const std::uint32_t value : hash) {
    for (int shift = 28; shift >= 0; shift -= 4) {
      result.push_back(digits[(value >> shift) & 0xfu]);
    }
  }
  return result;
}

std::size_t afterMarker(std::string_view text, std::string_view marker,
                        std::size_t begin = 0) {
  const std::size_t found = text.find(marker, begin);
  if (found == std::string_view::npos) {
    throw std::runtime_error("missing JSON marker " + std::string(marker));
  }
  return found + marker.size();
}

long long integerAfter(std::string_view text, std::string_view marker,
                       std::size_t begin = 0) {
  const std::size_t cursor = afterMarker(text, marker, begin);
  const std::string owned(text);
  char* end = nullptr;
  const char* first = owned.c_str() + cursor;
  const long long value = std::strtoll(first, &end, 10);
  if (end == first) throw std::runtime_error("invalid JSON integer");
  return value;
}

double numberAfter(std::string_view text, std::string_view marker,
                   std::size_t begin = 0) {
  const std::size_t cursor = afterMarker(text, marker, begin);
  const std::string owned(text);
  char* end = nullptr;
  const char* first = owned.c_str() + cursor;
  const double value = std::strtod(first, &end);
  if (end == first || !std::isfinite(value)) {
    throw std::runtime_error("invalid JSON number");
  }
  return value;
}

bool booleanAfter(std::string_view text, std::string_view marker,
                  std::size_t begin = 0) {
  const std::size_t cursor = afterMarker(text, marker, begin);
  if (text.substr(cursor, 4) == "true") return true;
  if (text.substr(cursor, 5) == "false") return false;
  throw std::runtime_error("invalid JSON boolean");
}

std::string stringAfter(std::string_view text, std::string_view marker,
                        std::size_t begin = 0) {
  const std::size_t cursor = afterMarker(text, marker, begin);
  const std::size_t end = text.find('"', cursor);
  if (end == std::string_view::npos) {
    throw std::runtime_error("unterminated JSON string");
  }
  return std::string(text.substr(cursor, end - cursor));
}

std::uint64_t parseHex64(std::string_view value) {
  const std::string owned(value);
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(owned.c_str(), &end, 0);
  if (end == owned.c_str() || *end != '\0') {
    throw std::runtime_error("invalid stored public hash");
  }
  return static_cast<std::uint64_t>(parsed);
}

std::size_t matchingDelimiter(std::string_view text, std::size_t begin,
                              char open, char close) {
  if (begin >= text.size() || text[begin] != open) {
    throw std::runtime_error("invalid JSON delimiter start");
  }
  int depth = 0;
  bool quoted = false;
  bool escaped = false;
  for (std::size_t cursor = begin; cursor < text.size(); ++cursor) {
    const char token = text[cursor];
    if (quoted) {
      if (escaped) escaped = false;
      else if (token == '\\') escaped = true;
      else if (token == '"') quoted = false;
      continue;
    }
    if (token == '"') quoted = true;
    else if (token == open) ++depth;
    else if (token == close && --depth == 0) return cursor;
  }
  throw std::runtime_error("unterminated JSON delimiter");
}

void skipSeparators(std::string_view text, std::size_t& cursor) {
  while (cursor < text.size() &&
         (text[cursor] == ' ' || text[cursor] == '\t' ||
          text[cursor] == ',')) {
    ++cursor;
  }
}

struct PanelRecord {
  std::uint32_t origin_game = 0;
  int move_index = -1;
  std::uint64_t stored_public_hash = 0;
  PublicState state{};
  std::array<bool, kBoardSize> legal{};
  std::array<double, kBoardSize> target{};
};

PanelRecord parsePanel(std::string_view line) {
  if (line.find("\"recordType\":\"deployment-panel-export-replay\"") ==
          std::string_view::npos ||
      line.find("\"gate\":\"ultra\"") == std::string_view::npos ||
      line.find("\"excludedFromModelInput\"") == std::string_view::npos) {
    throw std::runtime_error("unexpected panel record metadata");
  }
  PanelRecord result;
  const long long origin = integerAfter(line, "\"screenSeed\":");
  if (origin < 0 || origin > std::numeric_limits<std::uint32_t>::max()) {
    throw std::runtime_error("invalid panel origin");
  }
  result.origin_game = static_cast<std::uint32_t>(origin);
  result.move_index = static_cast<int>(integerAfter(line, "\"moveIndex\":"));
  result.stored_public_hash =
      parseHex64(stringAfter(line, "\"canonicalPublicHash\":\""));
  const std::string board = stringAfter(line, "\"board\":\"");
  if (board.size() != kCellCount) throw std::runtime_error("invalid board size");
  for (int cell = 0; cell < kCellCount; ++cell) {
    if (board[cell] < '0' || board[cell] > '9') {
      throw std::runtime_error("invalid board token");
    }
    result.state.board[cell] = static_cast<std::uint8_t>(board[cell] - '0');
    if (result.state.board[cell] > kCracked) {
      throw std::runtime_error("out-of-domain board token");
    }
  }
  result.state.next_disc =
      static_cast<std::uint8_t>(integerAfter(line, "\"nextDisc\":"));
  result.state.moves_remaining =
      static_cast<std::uint8_t>(integerAfter(line, "\"movesRemaining\":"));
  result.state.terminal = booleanAfter(line, "\"terminal\":");
  if (result.state.next_disc < 1 || result.state.next_disc > kBoardSize ||
      result.state.moves_remaining < 1 ||
      result.state.moves_remaining > kMovesPerLevel || result.state.terminal ||
      result.move_index < 0) {
    throw std::runtime_error("invalid panel public state");
  }

  std::size_t cursor = afterMarker(line, "\"actions\":[");
  for (int action = 0; action < kBoardSize; ++action) {
    skipSeparators(line, cursor);
    if (line.substr(cursor, 4) == "null") {
      cursor += 4;
      continue;
    }
    if (cursor >= line.size() || line[cursor] != '{') {
      throw std::runtime_error("invalid panel action array");
    }
    const std::size_t end = matchingDelimiter(line, cursor, '{', '}');
    const std::string_view object = line.substr(cursor, end - cursor + 1);
    if (integerAfter(object, "\"action\":") != action) {
      throw std::runtime_error("panel action index mismatch");
    }
    result.legal[action] = true;
    result.target[action] = numberAfter(object, "\"meanScoreReturn\":");
    cursor = end + 1;
  }
  skipSeparators(line, cursor);
  if (cursor >= line.size() || line[cursor] != ']') {
    throw std::runtime_error("unterminated panel action array");
  }
  for (int action = 0; action < kBoardSize; ++action) {
    if (result.legal[action] != isLegal(result.state.board, action)) {
      throw std::runtime_error("panel legal mask mismatch");
    }
  }
  if (publicHash(result.state) != result.stored_public_hash) {
    throw std::runtime_error("panel public hash mismatch");
  }
  return result;
}

std::vector<PanelRecord> loadLockedPanels(const std::string& path) {
  const std::string contents = readWholeFile(path);
  if (sha256(contents) != kExpectedCorpusSha256) {
    throw std::runtime_error("frozen h200 corpus SHA-256 mismatch");
  }
  std::vector<PanelRecord> result;
  std::istringstream input(contents);
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty()) result.push_back(parsePanel(line));
  }
  if (result.size() != kExpectedRecords) {
    throw std::runtime_error("frozen h200 record count mismatch");
  }
  std::map<std::uint32_t, std::set<int>> origins;
  for (const PanelRecord& panel : result) {
    origins[panel.origin_game].insert(panel.move_index);
  }
  if (origins.size() != kExpectedGames) {
    throw std::runtime_error("frozen h200 origin count mismatch");
  }
  for (int game = 0; game < kExpectedGames; ++game) {
    const std::uint32_t origin =
        kExpectedGameStart + static_cast<std::uint32_t>(game);
    const auto found = origins.find(origin);
    if (found == origins.end() || found->second.empty() ||
        *found->second.begin() != 0 ||
        *found->second.rbegin() + 1 != static_cast<int>(found->second.size())) {
      throw std::runtime_error("frozen h200 origin boundary mismatch");
    }
  }
  return result;
}

struct AuditRecord {
  std::uint64_t public_hash = 0;
  int action = -1;
  std::array<int, 2> split_actions{{-1, -1}};
  int d4_action = -1;
  std::uint8_t legal_mask = 0;
  std::array<double, kBoardSize> dual{};
  std::array<double, kBoardSize> evaluation{};
  std::array<double, kBoardSize> d4_q{};
  ChanceAccounting accounting{};
  std::uint64_t d4_work = 0;
};

std::uint8_t legalMask(const std::array<bool, kBoardSize>& legal) {
  std::uint8_t result = 0;
  for (int action = 0; action < kBoardSize; ++action) {
    if (legal[action]) result |= static_cast<std::uint8_t>(1u << action);
  }
  return result;
}

std::string serializeCheckpoint(const std::vector<AuditRecord>& records) {
  std::ostringstream output;
  output << "DROP7_MARTINGALE_DUAL_B0_CHECKPOINT 1\n"
         << kProtocol << '\n' << kExpectedCorpusSha256 << '\n'
         << records.size() << '\n' << std::setprecision(17);
  for (const AuditRecord& record : records) {
    output << "R " << record.public_hash << ' ' << record.action << ' '
           << record.split_actions[0] << ' ' << record.split_actions[1] << ' '
           << record.d4_action << ' ' << static_cast<int>(record.legal_mask);
    const auto writeValues = [&](const std::array<double, kBoardSize>& values) {
      for (int action = 0; action < kBoardSize; ++action) {
        output << ' ' << (((record.legal_mask >> action) & 1u) != 0
                              ? values[action]
                              : 0.0);
      }
    };
    writeValues(record.dual);
    writeValues(record.evaluation);
    writeValues(record.d4_q);
    for (const std::uint64_t value : record.accounting.transitions) {
      output << ' ' << value;
    }
    for (const std::uint64_t value : record.accounting.reveal_draws) {
      output << ' ' << value;
    }
    for (const std::uint64_t value : record.accounting.visible_draws) {
      output << ' ' << value;
    }
    output << ' ' << record.accounting.penalty_queries << ' '
           << record.accounting.public_d1_calls << ' '
           << record.accounting.public_d1_work << ' ' << record.d4_work
           << '\n';
  }
  return output.str();
}

std::vector<AuditRecord> parseCheckpoint(std::string_view source) {
  std::istringstream input{std::string(source)};
  std::string line;
  if (!std::getline(input, line) ||
      line != "DROP7_MARTINGALE_DUAL_B0_CHECKPOINT 1" ||
      !std::getline(input, line) || line != kProtocol ||
      !std::getline(input, line) || line != kExpectedCorpusSha256 ||
      !std::getline(input, line)) {
    throw std::runtime_error("checkpoint header/config mismatch");
  }
  const std::size_t count = static_cast<std::size_t>(std::stoull(line));
  if (count > kExpectedRecords) {
    throw std::runtime_error("checkpoint record count exceeds corpus");
  }
  std::vector<AuditRecord> result;
  result.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    if (!std::getline(input, line)) {
      throw std::runtime_error("truncated checkpoint");
    }
    std::istringstream row(line);
    char tag = '\0';
    int mask = 0;
    AuditRecord record;
    if (!(row >> tag >> record.public_hash >> record.action >>
          record.split_actions[0] >> record.split_actions[1] >>
          record.d4_action >> mask) || tag != 'R' || mask < 0 || mask >= 128) {
      throw std::runtime_error("invalid checkpoint record header");
    }
    record.legal_mask = static_cast<std::uint8_t>(mask);
    const auto readValues = [&](std::array<double, kBoardSize>& values) {
      values.fill(-std::numeric_limits<double>::infinity());
      for (int action = 0; action < kBoardSize; ++action) {
        double value = 0.0;
        if (!(row >> value) || !std::isfinite(value)) {
          throw std::runtime_error("invalid checkpoint value");
        }
        if (((record.legal_mask >> action) & 1u) != 0) values[action] = value;
      }
    };
    readValues(record.dual);
    readValues(record.evaluation);
    readValues(record.d4_q);
    for (std::uint64_t& value : record.accounting.transitions) row >> value;
    for (std::uint64_t& value : record.accounting.reveal_draws) row >> value;
    for (std::uint64_t& value : record.accounting.visible_draws) row >> value;
    if (!(row >> record.accounting.penalty_queries >>
          record.accounting.public_d1_calls >>
          record.accounting.public_d1_work >> record.d4_work)) {
      throw std::runtime_error("invalid checkpoint accounting");
    }
    std::string trailing;
    if (row >> trailing) throw std::runtime_error("checkpoint trailing data");
    result.push_back(record);
  }
  while (std::getline(input, line)) {
    if (!line.empty()) throw std::runtime_error("checkpoint extra records");
  }
  return result;
}

void saveCheckpoint(const std::string& path,
                    const std::vector<AuditRecord>& records) {
  const std::string temporary = path + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("could not write checkpoint temp");
    output << serializeCheckpoint(records);
    if (!output) throw std::runtime_error("checkpoint write failed");
  }
  if (std::rename(temporary.c_str(), path.c_str()) != 0) {
    throw std::runtime_error("atomic checkpoint rename failed");
  }
}

std::vector<AuditRecord> loadCheckpoint(const std::string& path) {
  return parseCheckpoint(readWholeFile(path));
}

struct Metric {
  int roots = 0;
  int top1_correct = 0;
  double top1_accuracy = 0.0;
  double pairwise_credit = 0.0;
  int pairwise_pairs = 0;
  double pairwise_accuracy = 0.0;
  double normalized_regret = 0.0;
};

Metric rankingMetric(const std::vector<PanelRecord>& panels,
                     const std::vector<AuditRecord>& records,
                     const std::vector<std::size_t>& indices, bool candidate) {
  Metric result;
  for (const std::size_t index : indices) {
    const PanelRecord& panel = panels.at(index);
    const AuditRecord& record = records.at(index);
    const auto& prediction = candidate ? record.dual : record.d4_q;
    const int selected = candidate ? record.action : record.d4_action;
    if (selected < 0 || selected >= kBoardSize || !panel.legal[selected]) {
      throw std::runtime_error("ranking selected an illegal action");
    }
    double target_best = -std::numeric_limits<double>::infinity();
    double target_worst = std::numeric_limits<double>::infinity();
    for (int action = 0; action < kBoardSize; ++action) {
      if (!panel.legal[action]) continue;
      target_best = std::max(target_best, panel.target[action]);
      target_worst = std::min(target_worst, panel.target[action]);
    }
    ++result.roots;
    result.top1_correct +=
        panel.target[selected] >= target_best - kTieTolerance;
    if (target_best > target_worst + kTieTolerance) {
      result.normalized_regret +=
          (target_best - panel.target[selected]) / (target_best - target_worst);
    }
    for (int first = 0; first < kBoardSize; ++first) {
      if (!panel.legal[first]) continue;
      for (int second = first + 1; second < kBoardSize; ++second) {
        if (!panel.legal[second]) continue;
        const double truth = panel.target[first] - panel.target[second];
        if (std::abs(truth) <= kTieTolerance) continue;
        const double estimate = prediction[first] - prediction[second];
        ++result.pairwise_pairs;
        if (estimate == 0.0) result.pairwise_credit += 0.5;
        else if ((estimate > 0.0) == (truth > 0.0)) {
          result.pairwise_credit += 1.0;
        }
      }
    }
  }
  if (result.roots == 0 || result.pairwise_pairs == 0) {
    throw std::runtime_error("empty ranking metric stratum");
  }
  result.top1_accuracy =
      static_cast<double>(result.top1_correct) / result.roots;
  result.pairwise_accuracy = result.pairwise_credit / result.pairwise_pairs;
  result.normalized_regret /= result.roots;
  return result;
}

bool nonRegresses(const Metric& candidate, const Metric& baseline) {
  return candidate.top1_accuracy + kTieTolerance >= baseline.top1_accuracy &&
         candidate.pairwise_accuracy + kTieTolerance >=
             baseline.pairwise_accuracy &&
         candidate.normalized_regret <=
             baseline.normalized_regret + kTieTolerance;
}

struct GateResult {
  Metric candidate{};
  Metric d4{};
  int passing_origins = 0;
  bool first_half = false;
  bool second_half = false;
  double planner_split_stability = 0.0;
  bool complete = false;
  bool passed = false;
};

GateResult auditGate(const std::vector<PanelRecord>& panels,
                     const std::vector<AuditRecord>& records) {
  if (panels.size() != records.size() || records.size() != kExpectedRecords) {
    throw std::runtime_error("gate requires complete frozen corpus");
  }
  std::vector<std::size_t> all(records.size());
  for (std::size_t index = 0; index < all.size(); ++index) all[index] = index;
  GateResult result;
  result.candidate = rankingMetric(panels, records, all, true);
  result.d4 = rankingMetric(panels, records, all, false);
  int stable = 0;
  result.complete = true;
  for (std::size_t index = 0; index < records.size(); ++index) {
    const AuditRecord& record = records[index];
    const PanelRecord& panel = panels[index];
    stable += record.split_actions[0] == record.split_actions[1];
    if (record.public_hash != panel.stored_public_hash ||
        record.legal_mask != legalMask(panel.legal) ||
        !panel.legal[record.action] || !panel.legal[record.d4_action]) {
      result.complete = false;
    }
    for (int action = 0; action < kBoardSize; ++action) {
      const bool finite = std::isfinite(record.dual[action]) &&
                          std::isfinite(record.evaluation[action]) &&
                          std::isfinite(record.d4_q[action]);
      if (finite != panel.legal[action]) result.complete = false;
    }
  }
  result.planner_split_stability =
      static_cast<double>(stable) / records.size();
  for (int game = 0; game < kExpectedGames; ++game) {
    std::vector<std::size_t> origin;
    for (std::size_t index = 0; index < panels.size(); ++index) {
      if (panels[index].origin_game ==
          kExpectedGameStart + static_cast<std::uint32_t>(game)) {
        origin.push_back(index);
      }
    }
    result.passing_origins +=
        nonRegresses(rankingMetric(panels, records, origin, true),
                     rankingMetric(panels, records, origin, false));
  }
  std::vector<std::size_t> first_half;
  std::vector<std::size_t> second_half;
  for (std::size_t index = 0; index < panels.size(); ++index) {
    (panels[index].origin_game < kExpectedGameStart + 4 ? first_half
                                                        : second_half)
        .push_back(index);
  }
  result.first_half =
      nonRegresses(rankingMetric(panels, records, first_half, true),
                   rankingMetric(panels, records, first_half, false));
  result.second_half =
      nonRegresses(rankingMetric(panels, records, second_half, true),
                   rankingMetric(panels, records, second_half, false));

  // The fixed gate requires aggregate ranking improvement, lower selected-
  // action regret, broad whole-origin support, and stable planner halves.
  result.passed =
      result.complete &&
      result.candidate.top1_accuracy + kTieTolerance >=
          result.d4.top1_accuracy &&
      result.candidate.pairwise_accuracy >=
          result.d4.pairwise_accuracy + 0.01 &&
      result.candidate.normalized_regret <=
          0.95 * result.d4.normalized_regret + kTieTolerance &&
      result.passing_origins >= 6 && result.first_half && result.second_half &&
      result.planner_split_stability >= 0.70;
  return result;
}

std::string jsonEscape(std::string_view value) {
  std::string result;
  for (const char token : value) {
    if (token == '"' || token == '\\') result.push_back('\\');
    if (token == '\n') result += "\\n";
    else result.push_back(token);
  }
  return result;
}

void writeArtifact(const std::string& path, const GateResult& gate,
                   const std::vector<AuditRecord>& records,
                   double wall_seconds) {
  ChanceAccounting accounting;
  std::uint64_t d4_work = 0;
  for (const AuditRecord& record : records) {
    accounting += record.accounting;
    d4_work += record.d4_work;
  }
  std::ofstream output(path, std::ios::trunc);
  if (!output) throw std::runtime_error("could not write B0 artifact");
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"martingale-dual-b0\",\n"
         << "  \"protocol\":\"" << kProtocol << "\",\n"
         << "  \"corpus\":{\"records\":" << records.size()
         << ",\"origins\":" << kExpectedGames << ",\"sha256\":\""
         << kExpectedCorpusSha256 << "\",\"newGameplaySeeds\":0},\n"
         << "  \"publicValue\":\"frozen fair leaf used by fair-D4\",\n"
         << "  \"empiricalTransitionLaw\":\"uniform exact seven-member "
            "support keyed by canonical public state and action; planner "
            "realizations and penalty mean share support\",\n"
         << "  \"evaluationRandomness\":\"independent event-keyed "
            "reveal and visible-disc domains\",\n"
         << "  \"config\":{\"horizon\":" << kHorizon
         << ",\"beamWidth\":" << kBeamWidth
         << ",\"plannerScenarios\":" << kPlannerScenarios
         << ",\"penaltySamples\":" << kPenaltySamples
         << ",\"evaluationScenarios\":" << kEvaluationScenarios << "},\n"
         << "  \"candidate\":{\"top1Accuracy\":"
         << gate.candidate.top1_accuracy << ",\"pairwiseAccuracy\":"
         << gate.candidate.pairwise_accuracy << ",\"normalizedRegret\":"
         << gate.candidate.normalized_regret << "},\n"
         << "  \"fairD4\":{\"top1Accuracy\":" << gate.d4.top1_accuracy
         << ",\"pairwiseAccuracy\":" << gate.d4.pairwise_accuracy
         << ",\"normalizedRegret\":" << gate.d4.normalized_regret << "},\n"
         << "  \"gate\":{\"complete\":"
         << (gate.complete ? "true" : "false")
         << ",\"passingOrigins\":" << gate.passing_origins
         << ",\"requiredOrigins\":6,\"firstHalfNonRegression\":"
         << (gate.first_half ? "true" : "false")
         << ",\"secondHalfNonRegression\":"
         << (gate.second_half ? "true" : "false")
         << ",\"plannerSplitStability\":"
         << gate.planner_split_stability << ",\"requiredStability\":0.70,"
         << "\"requiredPairwiseGain\":0.01,\"requiredRegretRatio\":0.95,"
         << "\"passed\":" << (gate.passed ? "true" : "false") << "},\n"
         << "  \"work\":{\"plannerTransitions\":"
         << accounting.transitions[static_cast<std::size_t>(Lane::planner)]
         << ",\"penaltyTransitions\":"
         << accounting.transitions[static_cast<std::size_t>(Lane::penalty)]
         << ",\"evaluationTransitions\":"
         << accounting.transitions[static_cast<std::size_t>(Lane::evaluation)]
         << ",\"publicD1Work\":" << accounting.public_d1_work
         << ",\"fairD4Work\":" << d4_work << "},\n"
         << "  \"wallSeconds\":" << wall_seconds
         << ",\"peakRssBytes\":" << peakRssBytes() << "\n}\n";
}

struct Options {
  std::string input = "/tmp/drop7-terminal-policy-deployment-panels.jsonl";
  std::string checkpoint = "/tmp/drop7-martingale-dual-b0.checkpoint";
  std::string output = "/tmp/drop7-martingale-dual-b0.json";
  bool resume = false;
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; ++index) {
    const std::string flag = argv[index];
    if (flag == "--resume") {
      result.resume = true;
      continue;
    }
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string value = argv[++index];
    if (flag == "--input") result.input = value;
    else if (flag == "--checkpoint") result.checkpoint = value;
    else if (flag == "--output") result.output = value;
    else throw std::invalid_argument("unknown option " + flag);
  }
  return result;
}

AuditRecord evaluateRecord(const PanelRecord& panel) {
  const RankResult rank = rankRoot(panel.state);
  const d4::SearchDecision baseline = d4::chooseDepth4Action(materialize(panel.state));
  if (!baseline.complete || baseline.completed_depth != d4::kCandidateDepth ||
      baseline.action < 0) {
    throw std::runtime_error("frozen fair-D4 baseline did not complete");
  }
  AuditRecord result;
  result.public_hash = panel.stored_public_hash;
  result.action = rank.action;
  result.split_actions = rank.split_actions;
  result.d4_action = baseline.action;
  result.legal_mask = legalMask(rank.legal);
  result.dual = rank.dual_values;
  result.evaluation = rank.evaluation_values;
  result.d4_q = baseline.root_values;
  result.accounting = rank.accounting;
  result.d4_work = baseline.work;
  return result;
}

int runAudit(const Options& options, std::ostream& output) {
  const Deadline deadline;
  const std::vector<PanelRecord> panels = loadLockedPanels(options.input);
  std::vector<AuditRecord> records;
  if (options.resume) records = loadCheckpoint(options.checkpoint);
  if (records.size() > panels.size()) {
    throw std::runtime_error("checkpoint is longer than frozen corpus");
  }
  for (std::size_t index = 0; index < records.size(); ++index) {
    if (records[index].public_hash != panels[index].stored_public_hash ||
        records[index].legal_mask != legalMask(panels[index].legal)) {
      throw std::runtime_error("checkpoint/corpus prefix mismatch");
    }
  }
  output << "MARTINGALE_DUAL_B0_INPUT {\"records\":" << panels.size()
         << ",\"resumedAt\":" << records.size() << ",\"sha256\":\""
         << kExpectedCorpusSha256 << "\",\"newGameplaySeeds\":0}\n"
         << std::flush;
  for (std::size_t index = records.size(); index < panels.size(); ++index) {
    deadline.check();
    records.push_back(evaluateRecord(panels[index]));
    saveCheckpoint(options.checkpoint, records);
    enforceRss();
    output << "MARTINGALE_DUAL_B0_PROGRESS {\"complete\":"
           << records.size() << ",\"total\":" << panels.size() << "}\n"
           << std::flush;
  }
  const GateResult gate = auditGate(panels, records);
  deadline.check();
  writeArtifact(options.output, gate, records, deadline.seconds());
  output << std::setprecision(12)
         << "MARTINGALE_DUAL_B0_RESULT {\"passed\":"
         << (gate.passed ? "true" : "false")
         << ",\"top1\":" << gate.candidate.top1_accuracy
         << ",\"d4Top1\":" << gate.d4.top1_accuracy
         << ",\"pairwise\":" << gate.candidate.pairwise_accuracy
         << ",\"d4Pairwise\":" << gate.d4.pairwise_accuracy
         << ",\"normalizedRegret\":" << gate.candidate.normalized_regret
         << ",\"d4NormalizedRegret\":" << gate.d4.normalized_regret
         << ",\"artifact\":\"" << jsonEscape(options.output) << "\"}\n";
  return 0;
}

PublicState asymmetricFixture() {
  PublicState state;
  state.board[indexOf(6, 0)] = kSolid;
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(6, 1)] = 6;
  state.board[indexOf(6, 2)] = kCracked;
  state.board[indexOf(6, 3)] = 7;
  state.board[indexOf(6, 5)] = 4;
  state.board[indexOf(6, 6)] = kSolid;
  state.next_disc = 5;
  state.moves_remaining = 3;
  return state;
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

bool selfTest(std::ostream& output) {
  expect(sha256("abc") ==
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
         "SHA-256 known vector failed");
  expect(distinctRandomDomains(), "random domains are not independent");

  // Seven strata map to each possible visible disc exactly once, irrespective
  // of domain-specific rotations and jitter.
  for (const std::uint32_t domain : kRandomDomains) {
    std::array<int, kBoardSize> counts{};
    for (int sample = 0; sample < kBoardSize; ++sample) {
      const double unit = detail::stratifiedUnit(
          0x1020'3040u, sample, kBoardSize, domain, 17);
      const int disc = static_cast<int>(std::floor(unit * kBoardSize));
      expect(disc >= 0 && disc < kBoardSize, "stratified disc out of range");
      ++counts[disc];
    }
    for (const int count : counts) {
      expect(count == 1, "chance strata did not account for every disc");
    }
  }

  // A fixed public action that reveals two cracked discs has an exactly
  // mean-zero penalty across the planner's seven realized scenarios.  The
  // outcome is nonlinear in the two coupled reveal draws, so matching only
  // their individual marginals would not be enough for this assertion.
  PublicState multi_reveal;
  multi_reveal.board[indexOf(6, 0)] = kCracked;
  multi_reveal.board[indexOf(6, 2)] = kCracked;
  multi_reveal.next_disc = 1;
  multi_reveal.moves_remaining = 4;
  Config one_step;
  one_step.horizon = 1;
  ChanceAccounting penalty_accounting;
  const double expected =
      conditionalExpectedPotential(multi_reveal, 1, one_step,
                                   penalty_accounting);
  ChanceAccounting planner_accounting;
  double mean_penalty = 0.0;
  double minimum_potential = std::numeric_limits<double>::infinity();
  double maximum_potential = -std::numeric_limits<double>::infinity();
  int reveal_events = 0;
  int minimum_reveal_events = std::numeric_limits<int>::max();
  std::array<int, kPenaltySamples> support_counts{};
  for (int scenario = 0; scenario < kPlannerScenarios; ++scenario) {
    const int support = plannerSupportIndex(
        multi_reveal, multi_reveal, 1, scenario, 0);
    ++support_counts[support];
    const PublicTransition transition = empiricalKernelTransition(
        multi_reveal, 1, support, Lane::planner, planner_accounting);
    const double potential = transitionPotential(transition);
    reveal_events += transition.reveal_events;
    minimum_reveal_events =
        std::min(minimum_reveal_events, transition.reveal_events);
    minimum_potential = std::min(minimum_potential, potential);
    maximum_potential = std::max(maximum_potential, potential);
    mean_penalty += (transitionPotential(transition) - expected) /
                    static_cast<double>(kPlannerScenarios);
  }
  for (const int count : support_counts) {
    expect(count == 1, "planner scenarios did not enumerate kernel support");
  }
  expect(std::abs(mean_penalty) < 1.0e-8,
         "multi-reveal planner penalty was not exactly mean-zero");
  expect(penalty_accounting.transitions[static_cast<std::size_t>(Lane::penalty)] ==
             kPenaltySamples &&
             planner_accounting.transitions[
                 static_cast<std::size_t>(Lane::planner)] ==
                 kPlannerScenarios &&
             reveal_events >= 2 * kPlannerScenarios &&
             minimum_reveal_events >= 2 &&
             maximum_potential > minimum_potential + kTieTolerance,
         "multi-reveal kernel support/accounting was not exact and nonlinear");

  // Two actions each win on one of two tapes.  Perfect foresight gains ten raw
  // points, while the exact martingale charges all ten and leaves value zero.
  constexpr std::array<std::array<double, 2>, 2> toy{{
      {{10.0, -10.0}}, {{-10.0, 10.0}},
  }};
  double raw_clairvoyant = 0.0;
  double penalized_clairvoyant = 0.0;
  for (int scenario = 0; scenario < 2; ++scenario) {
    double raw_best = -std::numeric_limits<double>::infinity();
    double penalized_best = -std::numeric_limits<double>::infinity();
    for (int action = 0; action < 2; ++action) {
      const double expectation = (toy[action][0] + toy[action][1]) / 2.0;
      const double penalty = toy[action][scenario] - expectation;
      raw_best = std::max(raw_best, toy[action][scenario]);
      penalized_best =
          std::max(penalized_best, toy[action][scenario] - penalty);
    }
    raw_clairvoyant += raw_best / 2.0;
    penalized_clairvoyant += penalized_best / 2.0;
  }
  expect(raw_clairvoyant == 10.0 && penalized_clairvoyant == 0.0,
         "martingale failed to charge toy clairvoyance");

  // Exercise held-out evaluation reveal and visible events separately.
  PublicState reveal;
  reveal.board[indexOf(6, 1)] = kCracked;
  reveal.next_disc = 1;
  reveal.moves_remaining = 4;
  ChanceAccounting reveal_accounting;
  int observed_reveals = 0;
  for (int sample = 0; sample < kBoardSize; ++sample) {
    observed_reveals += playPublicMove(
                            reveal, 0, 0x5566'7788u, sample, kBoardSize, 0,
                            kEvaluationDomains, reveal_accounting)
                            .reveal_events;
  }
  expect(observed_reveals >= kBoardSize &&
             reveal_accounting.transitions[
                 static_cast<std::size_t>(Lane::evaluation)] == kBoardSize &&
             reveal_accounting.reveal_draws[
                 static_cast<std::size_t>(Lane::evaluation)] ==
                 static_cast<std::uint64_t>(observed_reveals) &&
             reveal_accounting.visible_draws[
                 static_cast<std::size_t>(Lane::evaluation)] == kBoardSize,
         "reveal/visible event accounting failed");

  // A failed rise legitimately produces a terminal public state with zero
  // moves remaining.  Keep that state representable so the terminal utility
  // can charge it instead of throwing at the horizon boundary.
  PublicState failed_rise;
  for (int row = 0; row < kBoardSize; ++row) {
    failed_rise.board[indexOf(row, 0)] = kSolid;
  }
  failed_rise.next_disc = 7;
  failed_rise.moves_remaining = 1;
  ChanceAccounting terminal_accounting;
  const PublicTransition terminal = playPublicMove(
      failed_rise, 1, 0xaabb'ccddu, 0, kBoardSize, 0,
      kPlannerKernelDomains,
      terminal_accounting);
  expect(terminal.next.terminal && terminal.next.moves_remaining == 0 &&
             publicFairValue(terminal.next) == d1::kFairTerminalUtility,
         "failed-rise terminal state was not safely valued");

  Config terminal_search;
  terminal_search.horizon = 3;
  terminal_search.beam_width = 3;
  const RankResult failed_rise_rank = rankRoot(failed_rise, terminal_search);
  constexpr std::uint64_t kFailedRiseLegalActions = kBoardSize - 1;
  const std::size_t planner_lane = static_cast<std::size_t>(Lane::planner);
  const std::size_t penalty_lane = static_cast<std::size_t>(Lane::penalty);
  const std::size_t evaluation_lane =
      static_cast<std::size_t>(Lane::evaluation);
  expect(failed_rise_rank.action >= 1 &&
             failed_rise_rank.accounting.transitions[planner_lane] ==
                 kFailedRiseLegalActions * kPlannerScenarios &&
             failed_rise_rank.accounting.transitions[penalty_lane] ==
                 kFailedRiseLegalActions * kPlannerScenarios *
                     kPenaltySamples &&
             failed_rise_rank.accounting.transitions[evaluation_lane] ==
                 kFailedRiseLegalActions * kEvaluationScenarios &&
             failed_rise_rank.accounting.public_d1_calls == 0,
         "failed rise did not terminate safely inside planner/evaluation");

  Config small;
  small.horizon = 3;
  small.beam_width = 3;
  const PublicState asymmetric = asymmetricFixture();
  const RankResult ranked = rankRoot(asymmetric, small);
  const RankResult reflected = rankRoot(mirror(asymmetric), small);
  expect(reflected.action == kBoardSize - 1 - ranked.action &&
             reflected.split_actions[0] ==
                 kBoardSize - 1 - ranked.split_actions[0] &&
             reflected.split_actions[1] ==
                 kBoardSize - 1 - ranked.split_actions[1],
         "B0 reflection action equivariance failed");
  for (int action = 0; action < kBoardSize; ++action) {
    const int reflected_action = kBoardSize - 1 - action;
    expect(ranked.legal[action] == reflected.legal[reflected_action] &&
               ranked.dual_values[action] ==
                   reflected.dual_values[reflected_action] &&
               ranked.evaluation_values[action] ==
                   reflected.evaluation_values[reflected_action],
           "B0 reflection value equivariance failed");
  }
  const std::size_t planner = static_cast<std::size_t>(Lane::planner);
  const std::size_t penalty = static_cast<std::size_t>(Lane::penalty);
  expect(ranked.accounting.penalty_queries ==
             ranked.accounting.transitions[planner] &&
             ranked.accounting.transitions[penalty] ==
                 ranked.accounting.penalty_queries * kPenaltySamples,
         "ranker chance accounting failed");

  State metadata = materialize(asymmetric);
  metadata.score = 987'654'321;
  metadata.level = 999;
  metadata.moves_played = 123'456;
  expect(publicState(metadata) == asymmetric &&
             publicFairValue(publicState(metadata)) == publicFairValue(asymmetric),
         "score/history metadata leaked into public value");

  AuditRecord checkpoint_record;
  checkpoint_record.public_hash = publicHash(asymmetric);
  checkpoint_record.action = ranked.action;
  checkpoint_record.split_actions = ranked.split_actions;
  checkpoint_record.d4_action = ranked.action;
  checkpoint_record.legal_mask = legalMask(ranked.legal);
  checkpoint_record.dual = ranked.dual_values;
  checkpoint_record.evaluation = ranked.evaluation_values;
  checkpoint_record.d4_q = ranked.dual_values;
  checkpoint_record.accounting = ranked.accounting;
  checkpoint_record.d4_work = 42;
  const std::vector<AuditRecord> checkpoint_records{checkpoint_record};
  const std::string checkpoint_path =
      "/tmp/drop7-martingale-dual-b0-selftest.checkpoint";
  const std::string second_path = checkpoint_path + ".roundtrip";
  saveCheckpoint(checkpoint_path, checkpoint_records);
  const std::vector<AuditRecord> loaded = loadCheckpoint(checkpoint_path);
  saveCheckpoint(second_path, loaded);
  expect(readWholeFile(checkpoint_path) == readWholeFile(second_path) &&
             serializeCheckpoint(loaded) ==
                 serializeCheckpoint(checkpoint_records),
         "deterministic checkpoint/resume round trip failed");
  static_cast<void>(std::remove(checkpoint_path.c_str()));
  static_cast<void>(std::remove(second_path.c_str()));
  enforceRss();

  output << std::setprecision(12)
         << "MARTINGALE_DUAL_B0_SELF_TEST {\"passed\":true,"
         << "\"seedFree\":true,\"gameplaySeedsOpened\":0,"
         << "\"publicStateOnly\":true,\"metadataBlind\":true,"
         << "\"sharedPlannerPenaltyKernel\":true,"
         << "\"independentEvaluationDomain\":true,"
         << "\"multiRevealZeroMeanPenalty\":"
         << mean_penalty << ",\"toyRawClairvoyance\":" << raw_clairvoyant
         << ",\"toyPenalizedClairvoyance\":" << penalized_clairvoyant
         << ",\"reflectionEquivariance\":true,"
         << "\"exactChanceAccounting\":true,"
         << "\"deterministicCheckpointResume\":true,"
         << "\"peakRssBytes\":" << peakRssBytes() << "}\n";
  return true;
}

constexpr std::uint64_t worstPlannerTransitionsPerRoot() {
  const std::uint64_t per_scenario =
      1u + static_cast<std::uint64_t>(kHorizon - 1) * kBeamWidth * kBoardSize;
  return static_cast<std::uint64_t>(kBoardSize) * kPlannerScenarios *
         per_scenario;
}

int project(std::ostream& output) {
  const auto started = Clock::now();
  const RankResult rank = rankRoot(asymmetricFixture());
  const double rank_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  const auto d4_started = Clock::now();
  const d4::SearchDecision baseline =
      d4::chooseDepth4Action(materialize(asymmetricFixture()));
  const double d4_seconds =
      std::chrono::duration<double>(Clock::now() - d4_started).count();
  expect(baseline.complete, "seed-free projection fair-D4 did not complete");
  const double projected_seconds =
      (rank_seconds + d4_seconds) * static_cast<double>(kExpectedRecords);
  const std::uint64_t planner_upper = worstPlannerTransitionsPerRoot();
  const std::uint64_t penalty_upper = planner_upper * kPenaltySamples;
  const bool accounting_bounded =
      rank.accounting.transitions[static_cast<std::size_t>(Lane::planner)] <=
          planner_upper &&
      rank.accounting.transitions[static_cast<std::size_t>(Lane::penalty)] <=
          penalty_upper;
  const bool wall_projected = projected_seconds <= kWallLimitSeconds;
  const bool rss_bounded = peakRssBytes() <= kRssLimitBytes;
  output << std::setprecision(12)
         << "MARTINGALE_DUAL_B0_PROJECTION {\"seedFree\":true,"
         << "\"gameplaySeedsOpened\":0,\"fixtures\":1,"
         << "\"rankSecondsPerRoot\":" << rank_seconds
         << ",\"d4SecondsPerRoot\":" << d4_seconds
         << ",\"projected477Seconds\":" << projected_seconds
         << ",\"wallBudgetSeconds\":" << kWallLimitSeconds
         << ",\"wallProjected\":" << (wall_projected ? "true" : "false")
         << ",\"plannerTransitionsObserved\":"
         << rank.accounting.transitions[static_cast<std::size_t>(Lane::planner)]
         << ",\"plannerTransitionsUpperPerRoot\":" << planner_upper
         << ",\"penaltyTransitionsObserved\":"
         << rank.accounting.transitions[static_cast<std::size_t>(Lane::penalty)]
         << ",\"penaltyTransitionsUpperPerRoot\":" << penalty_upper
         << ",\"accountingBounded\":"
         << (accounting_bounded ? "true" : "false")
         << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"rssBudgetBytes\":" << kRssLimitBytes
         << ",\"rssBounded\":" << (rss_bounded ? "true" : "false")
         << ",\"projectionPassed\":"
         << (wall_projected && rss_bounded && accounting_bounded ? "true"
                                                                     : "false")
         << "}\n";
  return wall_projected && rss_bounded && accounting_bounded ? 0 : 1;
}

}  // namespace drop7::martingale_dual_b0

#ifndef DROP7_MARTINGALE_DUAL_B0_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::martingale_dual_b0::selfTest(std::cout) ? EXIT_SUCCESS
                                                            : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--project") {
      return drop7::martingale_dual_b0::project(std::cout);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--audit") {
      const auto options =
          drop7::martingale_dual_b0::parseOptions(argc, argv, 2);
      return drop7::martingale_dual_b0::runAudit(options, std::cout);
    }
    std::cerr << "usage: drop7_martingale_dual_b0 --self-test | --project | "
                 "--audit [--input PATH] [--checkpoint PATH] [--output PATH] "
                 "[--resume]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
#endif
