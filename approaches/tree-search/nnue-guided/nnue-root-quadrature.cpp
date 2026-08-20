// Reuses the fixed public-state Q model, feature extractor, and K3+safety
// implementation without modifying their source.
#define main drop7_nnue_guided_frozen_entrypoint
#include "nnue-guided-search.cpp"
#undef main

namespace drop7::nnue_root_quadrature {

namespace frozen = drop7::nnue_guided;

constexpr std::uint32_t kScreenSeedStart = 0x3d70'a000u;
constexpr std::uint32_t kConfirmationSeedStart = 0x3d70'a100u;
constexpr std::uint32_t kAbsoluteScreenSeedStart = 0x3d70'c000u;
constexpr std::uint32_t kAbsoluteConfirmationSeedStart = 0x3d70'c100u;
constexpr int kScreenGames = 4;
constexpr int kConfirmationGames = 8;
constexpr int kMaximumMoves = 200;
constexpr int kParallelism = 4;
constexpr int kMaximumDepth = 5;
constexpr int kRootRevealSamples = 3;
constexpr int kInteriorSamples = 3;
constexpr std::uint64_t kMaximumWork = 250'000;
constexpr std::size_t kMaximumCacheEntries = 40'000;

struct ChanceConfiguration {
  int root_reveal_samples = kRootRevealSamples;
  int interior_samples = kInteriorSamples;
  bool enumerate_root_discs = true;
};

struct QuadratureContext {
  QuadratureContext(const frozen::SearchOptions& options,
                    const frozen::QModel* model,
                    ChanceConfiguration chance_configuration)
      : search(options, model), chance(chance_configuration) {}

  frozen::SearchContext search;
  ChanceConfiguration chance;
  std::uint64_t root_reveal_scenarios = 0;
  std::uint64_t root_next_disc_branches = 0;
  std::uint64_t interior_scenarios = 0;
};

template <typename Consumer>
void forEachExactRootOutcome(const State& state, int column,
                             int scenario_depth,
                             std::uint32_t policy_seed,
                             frozen::SearchContext* charged_search,
                             Consumer&& consume) {
  const std::uint32_t state_seed = cfpi::detail::scenarioSeedForState(
      state, policy_seed, scenario_depth);
  constexpr double weight =
      1.0 / static_cast<double>(kRootRevealSamples * kBoardSize);
  for (int reveal_sample = 0; reveal_sample < kRootRevealSamples;
       ++reveal_sample) {
    if (charged_search != nullptr) frozen::checkWork(*charged_search);
    cfpi::detail::StratifiedRandom random{
        state_seed, reveal_sample, kRootRevealSamples, 0,
    };
    MoveResult move;
    if (!cfpi::detail::playMoveSampled(state, column, random, move)) {
      throw std::runtime_error("root quadrature rejected a legal action");
    }
    if (charged_search != nullptr) ++charged_search->work;
    for (int next_disc = 1; next_disc <= kBoardSize; ++next_disc) {
      consume(move, reveal_sample, next_disc, weight);
    }
  }
}

double quadratureValue(const State& state, int depth,
                       QuadratureContext& context);

double jointActionValue(const State& state, int column, int depth,
                        int samples, QuadratureContext& context) {
  const std::uint32_t state_seed = cfpi::detail::scenarioSeedForState(
      state, context.search.options.policy_seed, depth);
  double value = 0.0;
  for (int sample = 0; sample < samples; ++sample) {
    frozen::checkWork(context.search);
    cfpi::detail::StratifiedRandom random{
        state_seed, sample, samples, 0,
    };
    MoveResult move;
    if (!cfpi::detail::playMoveSampled(state, column, random, move)) {
      value += context.search.options.terminal_utility;
      continue;
    }
    ++context.search.work;
    ++context.interior_scenarios;
    const double score_delta = static_cast<double>(move.score_delta);
    if (move.state.game_over) {
      value += score_delta + context.search.options.terminal_utility;
      continue;
    }
    move.state.score = 0;
    move.state.next_disc =
        cfpi::detail::sampledNextDisc(state_seed, sample, samples);
    bool ignored = false;
    const State next = cfpi::detail::canonicalState(move.state, ignored);
    value += score_delta + quadratureValue(next, depth - 1, context);
  }
  return value / static_cast<double>(samples);
}

double exactRootActionValue(const State& state, int column, int depth,
                            QuadratureContext& context) {
  double value = 0.0;
  forEachExactRootOutcome(
      state, column, depth, context.search.options.policy_seed,
      &context.search,
      [&](const MoveResult& sampled_move, int, int next_disc,
          double weight) {
        if (next_disc == 1) ++context.root_reveal_scenarios;
        ++context.root_next_disc_branches;
        const double score_delta =
            static_cast<double>(sampled_move.score_delta);
        if (sampled_move.state.game_over) {
          value += weight *
                   (score_delta + context.search.options.terminal_utility);
          return;
        }
        State branch = sampled_move.state;
        branch.score = 0;
        branch.next_disc = static_cast<std::uint8_t>(next_disc);
        bool ignored = false;
        const State next = cfpi::detail::canonicalState(branch, ignored);
        value += weight *
                 (score_delta + quadratureValue(next, depth - 1, context));
      });
  return value;
}

double quadratureValue(const State& state, int depth,
                       QuadratureContext& context) {
  ++context.search.nodes;
  frozen::checkWork(context.search);
  if (state.game_over) return context.search.options.terminal_utility;
  if (depth == 0) {
    ++context.search.work;
    const double value = cfpi::phasePotential(state);
    if (!std::isfinite(value)) {
      throw std::runtime_error("quadrature leaf returned non-finite value");
    }
    return value;
  }

  const std::string key = cfpi::detail::dynamicStateKey(state, depth);
  const auto cached = context.search.cache.find(key);
  if (cached != context.search.cache.end()) {
    ++context.search.cache_hits;
    context.search.order.splice(context.search.order.end(),
                                context.search.order,
                                cached->second.order);
    return cached->second.value;
  }
  const frozen::ActionRanking ranking =
      frozen::interiorActions(state, context.search);
  double best = -std::numeric_limits<double>::infinity();
  for (int index = 0; index < ranking.count; ++index) {
    best = std::max(best,
                    jointActionValue(state, ranking.actions[index], depth,
                                     context.chance.interior_samples,
                                     context));
  }
  if (!std::isfinite(best)) best = context.search.options.terminal_utility;
  frozen::cacheValue(context.search, key, best);
  return best;
}

frozen::RootEvaluation quadratureRootDecision(
    const State& canonical, int depth, QuadratureContext& context) {
  frozen::RootEvaluation result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  for (const int column : frozen::kColumnOrder) {
    if (!isLegal(canonical.board, column)) continue;
    const double value = context.chance.enumerate_root_discs
                             ? exactRootActionValue(canonical, column, depth,
                                                    context)
                             : jointActionValue(
                                   canonical, column, depth,
                                   context.chance.root_reveal_samples,
                                   context);
    result.values[column] = value;
    if (value > result.value) {
      result.value = value;
      result.action = column;
    }
  }
  return result;
}

struct QuadratureDecision {
  frozen::SearchDecision common;
  std::uint64_t root_reveal_scenarios = 0;
  std::uint64_t root_next_disc_branches = 0;
  std::uint64_t interior_scenarios = 0;
};

QuadratureDecision chooseQuadratureAction(
    const State& source, const frozen::SearchOptions& options,
    const frozen::QModel& model,
    ChanceConfiguration chance = ChanceConfiguration{}) {
  frozen::validateSearchOptions(options);
  if (!options.guided || options.top_k != 3 || !options.safety_union) {
    throw std::invalid_argument(
        "root quadrature requires frozen guided K3+safety options");
  }
  if (chance.root_reveal_samples < 1 || chance.interior_samples < 1) {
    throw std::invalid_argument("chance sample counts must be positive");
  }
  if (chance.enumerate_root_discs &&
      chance.root_reveal_samples != kRootRevealSamples) {
    throw std::invalid_argument(
        "exact root quadrature requires exactly three reveal strata");
  }
  if (source.game_over) return {};

  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  QuadratureContext context(options, &model, chance);
  int action = -1;
  int previous_action = -1;
  int completed_depth = 0;
  int switches = 0;
  std::array<double, kBoardSize> completed_values{};
  completed_values.fill(-std::numeric_limits<double>::infinity());
  for (int depth = 1; depth <= options.maximum_depth; ++depth) {
    try {
      const frozen::RootEvaluation candidate =
          quadratureRootDecision(canonical, depth, context);
      if (candidate.action < 0) break;
      if (previous_action >= 0 && candidate.action != previous_action) {
        ++switches;
      }
      previous_action = candidate.action;
      action = candidate.action;
      completed_values = candidate.values;
      completed_depth = depth;
    } catch (const frozen::WorkLimitReached&) {
      break;
    }
  }
  if (action < 0) action = centerFirstMove(canonical.board);

  QuadratureDecision result;
  result.common.action =
      mirrored && action >= 0 ? kBoardSize - 1 - action : action;
  result.common.canonical_action = action;
  result.common.completed_depth = completed_depth;
  result.common.depth_switches = switches;
  result.common.complete = completed_depth == options.maximum_depth;
  result.common.work = context.search.work;
  result.common.nodes = context.search.nodes;
  result.common.cache_hits = context.search.cache_hits;
  result.common.ranking_calls = context.search.ranking_calls;
  result.common.ranking_actions = context.search.ranking_actions;
  result.common.safety_additions = context.search.safety_additions;
  result.common.peak_cache_entries = context.search.peak_cache_entries;
  result.common.canonical_root_values = completed_values;
  result.common.root_values_complete = completed_depth > 0;
  result.root_reveal_scenarios = context.root_reveal_scenarios;
  result.root_next_disc_branches = context.root_next_disc_branches;
  result.interior_scenarios = context.interior_scenarios;
  return result;
}

frozen::SearchOptions searchOptions() {
  frozen::SearchOptions options;
  options.maximum_depth = kMaximumDepth;
  options.top_k = 3;
  options.guided = true;
  options.safety_union = true;
  options.maximum_work = kMaximumWork;
  options.maximum_cache_entries = kMaximumCacheEntries;
  return options;
}

frozen::SearchOptions exactOptions() {
  frozen::SearchOptions options;
  options.maximum_depth = 3;
  options.top_k = kBoardSize;
  options.guided = false;
  options.safety_union = false;
  options.maximum_work = kMaximumWork;
  options.maximum_cache_entries = kMaximumCacheEntries;
  return options;
}

struct QuadratureGameResult {
  frozen::GameResult common;
  std::uint64_t root_reveal_scenarios = 0;
  std::uint64_t root_next_disc_branches = 0;
  std::uint64_t interior_scenarios = 0;
};

QuadratureGameResult runQuadratureGame(
    std::uint32_t seed, const frozen::SearchOptions& options,
    const frozen::QModel& model, std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  QuadratureGameResult result;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    const QuadratureDecision decision =
        chooseQuadratureAction(state, options, model);
    if (!isLegal(state.board, decision.common.action)) {
      throw std::runtime_error("root quadrature selected an illegal action");
    }
    result.common.work += decision.common.work;
    result.common.nodes += decision.common.nodes;
    result.common.cache_hits += decision.common.cache_hits;
    result.common.ranking_calls += decision.common.ranking_calls;
    result.common.ranking_actions += decision.common.ranking_actions;
    result.common.safety_additions += decision.common.safety_additions;
    result.common.depth_switches += decision.common.depth_switches;
    result.common.complete_moves += decision.common.complete;
    result.common.depth_sum += decision.common.completed_depth;
    result.common.minimum_depth = std::min(
        result.common.minimum_depth, decision.common.completed_depth);
    result.common.maximum_depth = std::max(
        result.common.maximum_depth, decision.common.completed_depth);
    result.common.peak_cache_entries = std::max(
        result.common.peak_cache_entries,
        decision.common.peak_cache_entries);
    result.root_reveal_scenarios += decision.root_reveal_scenarios;
    result.root_next_disc_branches += decision.root_next_disc_branches;
    result.interior_scenarios += decision.interior_scenarios;
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.common.action, move)) {
      throw std::runtime_error("root quadrature transition failed");
    }
  }
  result.common.score = state.score;
  result.common.moves = state.moves_played;
  result.common.censored = !state.game_over;
  result.common.peak_rss_bytes = frozen::peakRssBytes();
  result.common.elapsed_seconds = std::chrono::duration<double>(
                                      std::chrono::steady_clock::now() -
                                      started)
                                      .count();
  {
    const std::lock_guard<std::mutex> lock(frozen::progress_mutex);
    std::cerr << label << " seed 0x" << std::hex << seed << std::dec << ' '
              << result.common.score << " (" << result.common.moves
              << " moves, depth " << result.common.minimum_depth << '-'
              << result.common.maximum_depth << ", work "
              << result.common.work << ", root branches "
              << result.root_next_disc_branches << ")\n";
  }
  return result;
}

struct Cohort {
  std::vector<frozen::GameResult> baseline;
  std::vector<QuadratureGameResult> candidate;
};

Cohort runCohort(std::uint32_t seed_start, int games,
                 const frozen::QModel& model, std::string_view phase,
                 const frozen::SearchOptions& baseline_options) {
  Cohort cohort;
  cohort.baseline.resize(static_cast<std::size_t>(games));
  cohort.candidate.resize(static_cast<std::size_t>(games));
  const frozen::SearchOptions candidate_options = searchOptions();
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  const int worker_count = std::min(kParallelism, games);
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= games) return;
        const std::uint32_t seed =
            seed_start + static_cast<std::uint32_t>(game);
        const std::string baseline_label = std::string(phase) +
            (baseline_options.guided ? "-frozen-k3-d5" : "-exact-d3");
        const std::string candidate_label =
            std::string(phase) + "-root-3x7-interior-3";
        cohort.baseline[static_cast<std::size_t>(game)] = frozen::runGame(
            seed, baseline_options,
            baseline_options.guided ? &model : nullptr, kMaximumMoves,
            baseline_label);
        cohort.candidate[static_cast<std::size_t>(game)] =
            runQuadratureGame(seed, candidate_options, model,
                              candidate_label);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return cohort;
}

std::vector<frozen::GameResult> commonGames(
    const std::vector<QuadratureGameResult>& games) {
  std::vector<frozen::GameResult> result;
  result.reserve(games.size());
  for (const QuadratureGameResult& game : games) {
    result.push_back(game.common);
  }
  return result;
}

struct ChanceSummary {
  double root_reveal_scenarios_per_move = 0.0;
  double root_next_disc_branches_per_move = 0.0;
  double interior_scenarios_per_move = 0.0;
};

ChanceSummary summarizeChance(
    const std::vector<QuadratureGameResult>& games) {
  std::uint64_t moves = 0;
  std::uint64_t reveals = 0;
  std::uint64_t branches = 0;
  std::uint64_t interior = 0;
  for (const QuadratureGameResult& game : games) {
    moves += game.common.moves;
    reveals += game.root_reveal_scenarios;
    branches += game.root_next_disc_branches;
    interior += game.interior_scenarios;
  }
  const double denominator =
      static_cast<double>(std::max<std::uint64_t>(1, moves));
  return {reveals / denominator, branches / denominator,
          interior / denominator};
}

struct PairedSummary {
  double mean_score_difference = 0.0;
  double mean_move_difference = 0.0;
  int wins = 0;
  int ties = 0;
  int losses = 0;
};

PairedSummary pairedSummary(const Cohort& cohort) {
  if (cohort.baseline.size() != cohort.candidate.size() ||
      cohort.baseline.empty()) {
    throw std::invalid_argument("root quadrature cohort is not paired");
  }
  PairedSummary result;
  for (std::size_t game = 0; game < cohort.baseline.size(); ++game) {
    const auto& baseline = cohort.baseline[game];
    const auto& candidate = cohort.candidate[game].common;
    result.mean_score_difference +=
        static_cast<double>(candidate.score - baseline.score) /
        cohort.baseline.size();
    result.mean_move_difference +=
        static_cast<double>(candidate.moves - baseline.moves) /
        cohort.baseline.size();
    if (candidate.score > baseline.score) {
      ++result.wins;
    } else if (candidate.score < baseline.score) {
      ++result.losses;
    } else {
      ++result.ties;
    }
  }
  return result;
}

void writePaired(std::ostream& output, const PairedSummary& result) {
  output << "{\"meanScoreDifference\":" << result.mean_score_difference
         << ",\"meanMoveDifference\":" << result.mean_move_difference
         << ",\"wins\":" << result.wins << ",\"ties\":"
         << result.ties << ",\"losses\":" << result.losses << '}';
}

void writeChance(std::ostream& output, const ChanceSummary& result) {
  output << "{\"rootRevealScenariosPerMove\":"
         << result.root_reveal_scenarios_per_move
         << ",\"rootNextDiscBranchesPerMove\":"
         << result.root_next_disc_branches_per_move
         << ",\"interiorScenariosPerMove\":"
         << result.interior_scenarios_per_move << '}';
}

void writeTrajectories(std::ostream& output,
                       const std::vector<frozen::GameResult>& games) {
  output << "{\"scores\":[";
  for (std::size_t game = 0; game < games.size(); ++game) {
    if (game > 0) output << ',';
    output << games[game].score;
  }
  output << "],\"moves\":[";
  for (std::size_t game = 0; game < games.size(); ++game) {
    if (game > 0) output << ',';
    output << games[game].moves;
  }
  output << "]}";
}

struct RootAudit {
  std::array<int, kBoardSize> disc_counts{};
  std::array<int, kRootRevealSamples> reveal_counts{};
  int revealed_cells = 0;
  double probability_mass = 0.0;
  bool reveal_disc_independent = true;
};

RootAudit auditRootOutcomes(const State& state, int column, int depth,
                            std::uint32_t policy_seed) {
  RootAudit audit;
  std::array<Board, kRootRevealSamples> reveal_boards{};
  std::array<bool, kRootRevealSamples> initialized{};
  forEachExactRootOutcome(
      state, column, depth, policy_seed, nullptr,
      [&](const MoveResult& move, int reveal_sample, int next_disc,
          double weight) {
        ++audit.disc_counts[next_disc - 1];
        ++audit.reveal_counts[reveal_sample];
        audit.probability_mass += weight;
        if (next_disc == 1) {
          for (const Wave& wave : move.waves) {
            audit.revealed_cells += wave.revealed;
          }
        }
        State branch = move.state;
        branch.next_disc = static_cast<std::uint8_t>(next_disc);
        if (!initialized[reveal_sample]) {
          reveal_boards[reveal_sample] = branch.board;
          initialized[reveal_sample] = true;
        } else if (reveal_boards[reveal_sample] != branch.board) {
          audit.reveal_disc_independent = false;
        }
        audit.reveal_disc_independent =
            audit.reveal_disc_independent &&
            branch.next_disc == static_cast<std::uint8_t>(next_disc);
      });
  return audit;
}

bool completeLegalRoot(const State& state,
                       const QuadratureDecision& decision) {
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(state, mirrored);
  for (const int column : frozen::kColumnOrder) {
    if (isLegal(canonical.board, column) &&
        !std::isfinite(decision.common.canonical_root_values[column])) {
      return false;
    }
  }
  return decision.common.root_values_complete;
}

bool selfTest(std::ostream& output) {
  frozen::QModel model;
  model.initialize();
  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(4, 1)] = kCracked;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;

  State reveal_state;
  reveal_state.board = initialBoard();
  reveal_state.board[indexOf(kBoardSize - 1, 0)] = kCracked;
  reveal_state.next_disc = 2;
  reveal_state.moves_remaining = 3;
  const RootAudit audit =
      auditRootOutcomes(reveal_state, 0, 3, 0xd707'5eedu);
  const bool all_discs_exact = std::all_of(
      audit.disc_counts.begin(), audit.disc_counts.end(),
      [](int count) { return count == kRootRevealSamples; });
  const bool three_reveal_strata = std::all_of(
      audit.reveal_counts.begin(), audit.reveal_counts.end(),
      [](int count) { return count == kBoardSize; });
  const bool normalized =
      std::abs(audit.probability_mass - 1.0) <= 1.0e-12;
  const bool exercised_reveal = audit.revealed_cells > 0;

  frozen::SearchOptions parity_options = searchOptions();
  parity_options.maximum_depth = 3;
  parity_options.maximum_cache_entries = 4'000;
  const frozen::SearchDecision frozen_decision =
      frozen::chooseAction(state, parity_options, &model);
  ChanceConfiguration compatibility;
  compatibility.root_reveal_samples = frozen::kChanceSamples;
  compatibility.interior_samples = frozen::kChanceSamples;
  compatibility.enumerate_root_discs = false;
  const QuadratureDecision compatible = chooseQuadratureAction(
      state, parity_options, model, compatibility);
  const bool frozen_compatible =
      compatible.common.action == frozen_decision.action &&
      compatible.common.completed_depth == frozen_decision.completed_depth &&
      compatible.common.work == frozen_decision.work &&
      compatible.common.nodes == frozen_decision.nodes &&
      compatible.common.canonical_root_values ==
          frozen_decision.canonical_root_values;

  frozen::SearchOptions exact_options = exactOptions();
  exact_options.maximum_cache_entries = 4'000;
  const frozen::SearchDecision exact_decision =
      frozen::chooseAction(state, exact_options, nullptr);
  cfpi::BehaviorOptions exact_reference_options;
  exact_reference_options.max_depth = exact_options.maximum_depth;
  exact_reference_options.chance_samples = frozen::kChanceSamples;
  exact_reference_options.max_work = exact_options.maximum_work;
  exact_reference_options.max_cache_entries =
      exact_options.maximum_cache_entries;
  const int exact_reference =
      cfpi::chooseBehaviorAction(state, exact_reference_options);
  const bool exact_baseline_compatible =
      exact_decision.action == exact_reference &&
      exact_decision.completed_depth == exact_options.maximum_depth;

  const QuadratureDecision first =
      chooseQuadratureAction(state, parity_options, model);
  const QuadratureDecision repeat =
      chooseQuadratureAction(state, parity_options, model);
  State mirrored = state;
  mirrored.board = cfpi::detail::mirrorBoard(state.board);
  const QuadratureDecision reflected =
      chooseQuadratureAction(mirrored, parity_options, model);
  State metadata = state;
  metadata.score = 999'999;
  metadata.level = 88;
  metadata.moves_played = 321;
  const QuadratureDecision metadata_decision =
      chooseQuadratureAction(metadata, parity_options, model);

  const bool deterministic =
      first.common.action == repeat.common.action &&
      first.common.work == repeat.common.work &&
      first.common.canonical_root_values ==
          repeat.common.canonical_root_values;
  const bool reflection_safe =
      reflected.common.action == kBoardSize - 1 - first.common.action;
  const bool public_state_only =
      metadata_decision.common.action == first.common.action &&
      metadata_decision.common.work == first.common.work;
  const bool bounded = first.common.work <= parity_options.maximum_work &&
                       first.common.peak_cache_entries <=
                           parity_options.maximum_cache_entries &&
                       first.common.completed_depth <=
                           parity_options.maximum_depth;
  const bool root_full_width = completeLegalRoot(state, first);
  const bool branch_ratio =
      first.root_next_disc_branches ==
      first.root_reveal_scenarios * kBoardSize;
  const bool legal = isLegal(state.board, first.common.action);
  const bool passed = all_discs_exact && three_reveal_strata && normalized &&
                      exercised_reveal && audit.reveal_disc_independent &&
                      frozen_compatible && exact_baseline_compatible &&
                      deterministic && reflection_safe && public_state_only &&
                      bounded && root_full_width && branch_ratio && legal;
  output << "NNUE_ROOT_QUADRATURE_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"allSevenRootDiscsExact\":"
         << (all_discs_exact ? "true" : "false")
         << ",\"threeRevealStrata\":"
         << (three_reveal_strata ? "true" : "false")
         << ",\"revealNextDiscIndependent\":"
         << (audit.reveal_disc_independent ? "true" : "false")
         << ",\"actualGrayRevealExercised\":"
         << (exercised_reveal ? "true" : "false")
         << ",\"probabilityNormalized\":"
         << (normalized ? "true" : "false")
         << ",\"frozenFiveStrataCompatible\":"
         << (frozen_compatible ? "true" : "false")
         << ",\"exactD3BaselineCompatible\":"
         << (exact_baseline_compatible ? "true" : "false")
         << ",\"deterministic\":"
         << (deterministic ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe ? "true" : "false")
         << ",\"publicStateOnly\":"
         << (public_state_only ? "true" : "false")
         << ",\"bounded\":" << (bounded ? "true" : "false")
         << ",\"rootFullWidth\":"
         << (root_full_width ? "true" : "false")
         << ",\"branchRatioExact\":"
         << (branch_ratio ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false") << "}\n";
  return passed;
}

struct ProgramOptions {
  std::string model = "/tmp/drop7-phase-q-student-scale.bin";
  std::string output = "/tmp/drop7-nnue-root-quadrature.json";
};

ProgramOptions parseOptions(int argc, char** argv, int first_argument) {
  ProgramOptions options;
  for (int index = first_argument; index < argc; ++index) {
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing option value");
    }
    const std::string argument = argv[index++];
    if (argument == "--model") {
      options.model = argv[index];
    } else if (argument == "--output") {
      options.output = argv[index];
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  return options;
}

int benchmark(const ProgramOptions& options, std::ostream& output) {
  const auto started = std::chrono::steady_clock::now();
  frozen::QModel model;
  model.load(options.model);
  const Cohort screen =
      runCohort(kScreenSeedStart, kScreenGames, model, "screen",
                searchOptions());
  const frozen::Summary screen_baseline = frozen::summarize(screen.baseline);
  const std::vector<frozen::GameResult> screen_candidate_games =
      commonGames(screen.candidate);
  const frozen::Summary screen_candidate =
      frozen::summarize(screen_candidate_games);
  const ChanceSummary screen_chance = summarizeChance(screen.candidate);
  const PairedSummary screen_paired = pairedSummary(screen);
  const bool screen_passed = screen_paired.mean_score_difference > 0.0 &&
                             screen_paired.mean_move_difference > 0.0;

  Cohort confirmation;
  frozen::Summary confirmation_baseline;
  frozen::Summary confirmation_candidate;
  ChanceSummary confirmation_chance;
  PairedSummary confirmation_paired;
  bool confirmed = false;
  if (screen_passed) {
    confirmation = runCohort(kConfirmationSeedStart, kConfirmationGames,
                             model, "confirmation", searchOptions());
    confirmation_baseline = frozen::summarize(confirmation.baseline);
    const std::vector<frozen::GameResult> confirmation_candidate_games =
        commonGames(confirmation.candidate);
    confirmation_candidate =
        frozen::summarize(confirmation_candidate_games);
    confirmation_chance = summarizeChance(confirmation.candidate);
    confirmation_paired = pairedSummary(confirmation);
    confirmed = confirmation_paired.mean_score_difference > 0.0 &&
                confirmation_paired.mean_move_difference > 0.0;
  }

  const double elapsed_seconds = std::chrono::duration<double>(
                                     std::chrono::steady_clock::now() -
                                     started)
                                     .count();
  std::ofstream artifact(options.output);
  if (!artifact) {
    throw std::runtime_error("could not open root quadrature artifact");
  }
  artifact << std::setprecision(10)
           << "{\n  \"format\": \"drop7-nnue-root-quadrature-v1\",\n"
           << "  \"trainingSeedOnly\": true,\n"
           << "  \"publicStateOnly\": true,\n"
           << "  \"rootCompleteness\": \"all-legal-actions\",\n"
           << "  \"rootChance\": \"three-reveal-strata-times-seven-exact-next-discs\",\n"
           << "  \"interiorChance\": \"three-joint-strata\",\n"
           << "  \"maximumDepth\": " << kMaximumDepth << ",\n"
           << "  \"maximumWork\": " << kMaximumWork << ",\n"
           << "  \"maximumCacheEntries\": " << kMaximumCacheEntries
           << ",\n  \"maximumMoves\": " << kMaximumMoves
           << ",\n  \"parallelism\": " << kParallelism
           << ",\n  \"screenSeedStart\": " << kScreenSeedStart
           << ",\n  \"screen\": {\"baseline\":";
  frozen::writeSummary(artifact, screen_baseline);
  artifact << ",\"candidate\":";
  frozen::writeSummary(artifact, screen_candidate);
  artifact << ",\"chance\":";
  writeChance(artifact, screen_chance);
  artifact << ",\"paired\":";
  writePaired(artifact, screen_paired);
  artifact << ",\"baselineTrajectories\":";
  writeTrajectories(artifact, screen.baseline);
  artifact << ",\"candidateTrajectories\":";
  writeTrajectories(artifact, screen_candidate_games);
  artifact << "},\n  \"screenPassed\": "
           << (screen_passed ? "true" : "false")
           << ",\n  \"confirmation\": ";
  if (!screen_passed) {
    artifact << "null";
  } else {
    const std::vector<frozen::GameResult> confirmation_candidate_games =
        commonGames(confirmation.candidate);
    artifact << "{\"seedStart\":" << kConfirmationSeedStart
             << ",\"baseline\":";
    frozen::writeSummary(artifact, confirmation_baseline);
    artifact << ",\"candidate\":";
    frozen::writeSummary(artifact, confirmation_candidate);
    artifact << ",\"chance\":";
    writeChance(artifact, confirmation_chance);
    artifact << ",\"paired\":";
    writePaired(artifact, confirmation_paired);
    artifact << ",\"baselineTrajectories\":";
    writeTrajectories(artifact, confirmation.baseline);
    artifact << ",\"candidateTrajectories\":";
    writeTrajectories(artifact, confirmation_candidate_games);
    artifact << '}';
  }
  artifact << ",\n  \"confirmed\": " << (confirmed ? "true" : "false")
           << ",\n  \"decision\": \""
           << (!screen_passed
                   ? "reject-screen"
                   : (confirmed ? "advance" : "reject-confirmation"))
           << "\",\n  \"model\": \"" << options.model
           << "\",\n  \"elapsedSeconds\": " << elapsed_seconds << "\n}\n";

  output << std::fixed << std::setprecision(3)
         << "NNUE_ROOT_QUADRATURE_RESULT {\"screenBaselineScore\":"
         << screen_baseline.mean_score
         << ",\"screenBaselineMoves\":" << screen_baseline.mean_moves
         << ",\"screenCandidateScore\":" << screen_candidate.mean_score
         << ",\"screenCandidateMoves\":" << screen_candidate.mean_moves
         << ",\"screenScoreDifference\":"
         << screen_paired.mean_score_difference
         << ",\"screenMoveDifference\":"
         << screen_paired.mean_move_difference
         << ",\"screenPassed\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmationRan\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmed\":" << (confirmed ? "true" : "false")
         << ",\"decision\":\""
         << (!screen_passed
                 ? "reject-screen"
                 : (confirmed ? "advance" : "reject-confirmation"))
         << "\",\"elapsedSeconds\":" << elapsed_seconds
         << ",\"peakRssBytes\":" << frozen::peakRssBytes()
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return 0;
}

int absoluteGate(const ProgramOptions& options, std::ostream& output) {
  const auto started = std::chrono::steady_clock::now();
  frozen::QModel model;
  model.load(options.model);
  const Cohort screen =
      runCohort(kAbsoluteScreenSeedStart, kScreenGames, model,
                "absolute-screen", exactOptions());
  const frozen::Summary screen_baseline = frozen::summarize(screen.baseline);
  const std::vector<frozen::GameResult> screen_candidate_games =
      commonGames(screen.candidate);
  const frozen::Summary screen_candidate =
      frozen::summarize(screen_candidate_games);
  const ChanceSummary screen_chance = summarizeChance(screen.candidate);
  const PairedSummary screen_paired = pairedSummary(screen);
  const bool screen_passed = screen_paired.mean_score_difference > 0.0 &&
                             screen_paired.mean_move_difference > 0.0;

  Cohort confirmation;
  frozen::Summary confirmation_baseline;
  frozen::Summary confirmation_candidate;
  ChanceSummary confirmation_chance;
  PairedSummary confirmation_paired;
  bool confirmed = false;
  if (screen_passed) {
    confirmation = runCohort(
        kAbsoluteConfirmationSeedStart, kConfirmationGames, model,
        "absolute-confirmation", exactOptions());
    confirmation_baseline = frozen::summarize(confirmation.baseline);
    const std::vector<frozen::GameResult> confirmation_candidate_games =
        commonGames(confirmation.candidate);
    confirmation_candidate =
        frozen::summarize(confirmation_candidate_games);
    confirmation_chance = summarizeChance(confirmation.candidate);
    confirmation_paired = pairedSummary(confirmation);
    confirmed = confirmation_paired.mean_score_difference > 0.0 &&
                confirmation_paired.mean_move_difference > 0.0;
  }

  const double elapsed_seconds = std::chrono::duration<double>(
                                     std::chrono::steady_clock::now() -
                                     started)
                                     .count();
  std::ofstream artifact(options.output);
  if (!artifact) {
    throw std::runtime_error("could not open absolute gate artifact");
  }
  artifact << std::setprecision(10)
           << "{\n  \"format\": \"drop7-nnue-root-quadrature-absolute-v1\",\n"
           << "  \"trainingSeedOnly\": true,\n"
           << "  \"comparator\": \"full-width-exact-d3-five-strata\",\n"
           << "  \"candidate\": \"root-3x7-interior-3-guided-k3-safety-d5\",\n"
           << "  \"maximumWork\": " << kMaximumWork << ",\n"
           << "  \"maximumCacheEntries\": " << kMaximumCacheEntries
           << ",\n  \"maximumMoves\": " << kMaximumMoves
           << ",\n  \"parallelism\": " << kParallelism
           << ",\n  \"screenSeedStart\": " << kAbsoluteScreenSeedStart
           << ",\n  \"screen\": {\"exactD3\":";
  frozen::writeSummary(artifact, screen_baseline);
  artifact << ",\"quadrature\":";
  frozen::writeSummary(artifact, screen_candidate);
  artifact << ",\"chance\":";
  writeChance(artifact, screen_chance);
  artifact << ",\"paired\":";
  writePaired(artifact, screen_paired);
  artifact << ",\"exactTrajectories\":";
  writeTrajectories(artifact, screen.baseline);
  artifact << ",\"quadratureTrajectories\":";
  writeTrajectories(artifact, screen_candidate_games);
  artifact << "},\n  \"screenPassed\": "
           << (screen_passed ? "true" : "false")
           << ",\n  \"confirmation\": ";
  if (!screen_passed) {
    artifact << "null";
  } else {
    const std::vector<frozen::GameResult> confirmation_candidate_games =
        commonGames(confirmation.candidate);
    artifact << "{\"seedStart\":" << kAbsoluteConfirmationSeedStart
             << ",\"exactD3\":";
    frozen::writeSummary(artifact, confirmation_baseline);
    artifact << ",\"quadrature\":";
    frozen::writeSummary(artifact, confirmation_candidate);
    artifact << ",\"chance\":";
    writeChance(artifact, confirmation_chance);
    artifact << ",\"paired\":";
    writePaired(artifact, confirmation_paired);
    artifact << ",\"exactTrajectories\":";
    writeTrajectories(artifact, confirmation.baseline);
    artifact << ",\"quadratureTrajectories\":";
    writeTrajectories(artifact, confirmation_candidate_games);
    artifact << '}';
  }
  artifact << ",\n  \"confirmed\": " << (confirmed ? "true" : "false")
           << ",\n  \"decision\": \""
           << (!screen_passed
                   ? "reject-screen"
                   : (confirmed ? "advance" : "reject-confirmation"))
           << "\",\n  \"model\": \"" << options.model
           << "\",\n  \"elapsedSeconds\": " << elapsed_seconds << "\n}\n";

  output << std::fixed << std::setprecision(3)
         << "NNUE_ROOT_QUADRATURE_ABSOLUTE_RESULT {\"screenExactScore\":"
         << screen_baseline.mean_score
         << ",\"screenExactMoves\":" << screen_baseline.mean_moves
         << ",\"screenQuadratureScore\":" << screen_candidate.mean_score
         << ",\"screenQuadratureMoves\":" << screen_candidate.mean_moves
         << ",\"screenScoreDifference\":"
         << screen_paired.mean_score_difference
         << ",\"screenMoveDifference\":"
         << screen_paired.mean_move_difference
         << ",\"screenPassed\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmationRan\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmed\":" << (confirmed ? "true" : "false")
         << ",\"decision\":\""
         << (!screen_passed
                 ? "reject-screen"
                 : (confirmed ? "advance" : "reject-confirmation"))
         << "\",\"elapsedSeconds\":" << elapsed_seconds
         << ",\"peakRssBytes\":" << frozen::peakRssBytes()
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return 0;
}

}  // namespace drop7::nnue_root_quadrature

int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string(argv[1]) == "--self-test") {
      return drop7::nnue_root_quadrature::selfTest(std::cout) ? 0 : 1;
    }
    if (argc >= 2 && std::string(argv[1]) == "--benchmark") {
      const auto options =
          drop7::nnue_root_quadrature::parseOptions(argc, argv, 2);
      return drop7::nnue_root_quadrature::benchmark(options, std::cout);
    }
    if (argc >= 2 && std::string(argv[1]) == "--absolute-gate") {
      auto options =
          drop7::nnue_root_quadrature::parseOptions(argc, argv, 2);
      if (options.output == "/tmp/drop7-nnue-root-quadrature.json") {
        options.output = "/tmp/drop7-nnue-root-quadrature-absolute.json";
      }
      return drop7::nnue_root_quadrature::absoluteGate(options, std::cout);
    }
    std::cerr << "usage: drop7_nnue_root_quadrature --self-test | "
                 "--benchmark [--model PATH] [--output PATH] | "
                 "--absolute-gate [--model PATH] [--output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_nnue_root_quadrature: " << error.what() << '\n';
    return 1;
  }
}
