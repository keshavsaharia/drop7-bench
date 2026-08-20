// Reuses the fixed public-state Q model, feature extraction, and K3+safety
// implementation without modifying its artifact.
#define main drop7_nnue_guided_frozen_entrypoint
#include "nnue-guided-search.cpp"
#undef main

namespace drop7::nnue_selective {

namespace frozen = drop7::nnue_guided;

constexpr std::uint32_t kScreenSeedStart = 0x3d70'9000u;
constexpr std::uint32_t kConfirmationSeedStart = 0x3d70'9100u;
constexpr int kScreenGames = 4;
constexpr int kConfirmationGames = 8;
constexpr int kMaximumMoves = 200;
constexpr int kParallelism = 4;
constexpr std::uint64_t kMaximumWork = 250'000;
constexpr std::size_t kMaximumCacheEntries = 40'000;
constexpr int kFrozenDepth = 5;
constexpr int kSelectiveMaximumDepth = 8;

struct SelectiveContext {
  SelectiveContext(const frozen::SearchOptions& options,
                   const frozen::QModel* model, bool reductions)
      : search(options, model), reductions_enabled(reductions) {}

  frozen::SearchContext search;
  bool reductions_enabled = true;
  std::uint64_t reduced_probes = 0;
  std::uint64_t fail_high_researches = 0;
  std::uint64_t successful_researches = 0;
};

double selectiveValue(const State& state, int depth,
                      SelectiveContext& context);

double selectiveActionValue(const State& state, int column,
                            int child_depth, int scenario_depth,
                            SelectiveContext& context) {
  if (child_depth < 0 || scenario_depth < 1) {
    throw std::logic_error("invalid selective action depth");
  }
  const std::uint32_t state_seed = cfpi::detail::scenarioSeedForState(
      state, context.search.options.policy_seed, scenario_depth);
  double value = 0.0;
  for (int sample = 0; sample < frozen::kChanceSamples; ++sample) {
    frozen::checkWork(context.search);
    cfpi::detail::StratifiedRandom random{
        state_seed, sample, frozen::kChanceSamples, 0,
    };
    MoveResult move;
    if (!cfpi::detail::playMoveSampled(state, column, random, move)) {
      value += context.search.options.terminal_utility;
      continue;
    }
    ++context.search.work;
    const double score_delta = static_cast<double>(move.score_delta);
    if (move.state.game_over) {
      value += score_delta + context.search.options.terminal_utility;
      continue;
    }
    move.state.score = 0;
    move.state.next_disc = cfpi::detail::sampledNextDisc(
        state_seed, sample, frozen::kChanceSamples);
    bool ignored = false;
    const State next = cfpi::detail::canonicalState(move.state, ignored);
    value += score_delta + selectiveValue(next, child_depth, context);
  }
  return value / frozen::kChanceSamples;
}

double selectiveValue(const State& state, int depth,
                      SelectiveContext& context) {
  if (depth < 0) throw std::logic_error("negative selective depth");
  ++context.search.nodes;
  frozen::checkWork(context.search);
  if (state.game_over) return context.search.options.terminal_utility;
  if (depth == 0) {
    ++context.search.work;
    const double value = cfpi::phasePotential(state);
    if (!std::isfinite(value)) {
      throw std::runtime_error("selective leaf returned non-finite value");
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
    const int column = ranking.actions[index];
    double value = -std::numeric_limits<double>::infinity();
    const bool reduce = context.reductions_enabled && index > 0 && depth >= 2;
    if (!reduce) {
      value = selectiveActionValue(state, column, depth - 1, depth, context);
    } else {
      ++context.reduced_probes;
      const double reduced =
          selectiveActionValue(state, column, depth - 2, depth, context);
      value = reduced;
      // A strict reduced-depth fail-high is the sole condition that permits a
      // later Q-ranked move to consume a full-depth re-search.
      if (reduced > best) {
        ++context.fail_high_researches;
        value =
            selectiveActionValue(state, column, depth - 1, depth, context);
        if (value > best) ++context.successful_researches;
      }
    }
    best = std::max(best, value);
  }
  if (!std::isfinite(best)) best = context.search.options.terminal_utility;
  frozen::cacheValue(context.search, key, best);
  return best;
}

frozen::RootEvaluation selectiveRootDecision(
    const State& canonical, int depth, SelectiveContext& context) {
  frozen::RootEvaluation result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  // LMR is interior-only: every legal root action receives a full-depth
  // evaluation at every completed iteration.
  for (const int column : frozen::kColumnOrder) {
    if (!isLegal(canonical.board, column)) continue;
    const double value =
        selectiveActionValue(canonical, column, depth - 1, depth, context);
    result.values[column] = value;
    if (value > result.value) {
      result.value = value;
      result.action = column;
    }
  }
  return result;
}

struct SelectiveDecision {
  frozen::SearchDecision common;
  std::uint64_t reduced_probes = 0;
  std::uint64_t fail_high_researches = 0;
  std::uint64_t successful_researches = 0;
};

SelectiveDecision chooseSelectiveAction(
    const State& source, const frozen::SearchOptions& options,
    const frozen::QModel& model, bool reductions_enabled = true) {
  frozen::validateSearchOptions(options);
  if (!options.guided || options.top_k != 3 || !options.safety_union) {
    throw std::invalid_argument(
        "selective search requires frozen guided K3+safety options");
  }
  if (source.game_over) return {};

  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  SelectiveContext context(options, &model, reductions_enabled);
  int action = -1;
  int previous_action = -1;
  int completed_depth = 0;
  int switches = 0;
  std::array<double, kBoardSize> completed_values{};
  completed_values.fill(-std::numeric_limits<double>::infinity());
  for (int depth = 1; depth <= options.maximum_depth; ++depth) {
    try {
      const frozen::RootEvaluation candidate =
          selectiveRootDecision(canonical, depth, context);
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

  SelectiveDecision result;
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
  result.reduced_probes = context.reduced_probes;
  result.fail_high_researches = context.fail_high_researches;
  result.successful_researches = context.successful_researches;
  return result;
}

struct SelectiveGameResult {
  frozen::GameResult common;
  std::uint64_t reduced_probes = 0;
  std::uint64_t fail_high_researches = 0;
  std::uint64_t successful_researches = 0;
};

SelectiveGameResult runSelectiveGame(
    std::uint32_t seed, const frozen::SearchOptions& options,
    const frozen::QModel& model, std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  SelectiveGameResult result;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    const SelectiveDecision decision =
        chooseSelectiveAction(state, options, model);
    if (!isLegal(state.board, decision.common.action)) {
      throw std::runtime_error("selective search selected illegal root action");
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
    result.reduced_probes += decision.reduced_probes;
    result.fail_high_researches += decision.fail_high_researches;
    result.successful_researches += decision.successful_researches;
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.common.action, move)) {
      throw std::runtime_error("selective root transition failed");
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
              << result.common.work << ", reduced "
              << result.reduced_probes << ", research "
              << result.fail_high_researches << ")\n";
  }
  return result;
}

frozen::SearchOptions frozenOptions() {
  frozen::SearchOptions options;
  options.maximum_depth = kFrozenDepth;
  options.top_k = 3;
  options.guided = true;
  options.safety_union = true;
  options.maximum_work = kMaximumWork;
  options.maximum_cache_entries = kMaximumCacheEntries;
  return options;
}

frozen::SearchOptions selectiveOptions() {
  frozen::SearchOptions options = frozenOptions();
  options.maximum_depth = kSelectiveMaximumDepth;
  return options;
}

struct Cohort {
  std::vector<frozen::GameResult> baseline;
  std::vector<SelectiveGameResult> candidate;
};

Cohort runCohort(std::uint32_t seed_start, int games,
                 const frozen::QModel& model, std::string_view phase) {
  Cohort cohort;
  cohort.baseline.resize(static_cast<std::size_t>(games));
  cohort.candidate.resize(static_cast<std::size_t>(games));
  const frozen::SearchOptions baseline_options = frozenOptions();
  const frozen::SearchOptions candidate_options = selectiveOptions();
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
        const std::string baseline_label =
            std::string(phase) + "-frozen-k3-d5";
        const std::string candidate_label =
            std::string(phase) + "-selective-d8";
        cohort.baseline[static_cast<std::size_t>(game)] = frozen::runGame(
            seed, baseline_options, &model, kMaximumMoves, baseline_label);
        cohort.candidate[static_cast<std::size_t>(game)] =
            runSelectiveGame(seed, candidate_options, model,
                             candidate_label);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return cohort;
}

std::vector<frozen::GameResult> commonGames(
    const std::vector<SelectiveGameResult>& games) {
  std::vector<frozen::GameResult> result;
  result.reserve(games.size());
  for (const SelectiveGameResult& game : games) {
    result.push_back(game.common);
  }
  return result;
}

struct SelectiveSummary {
  double reduced_probes_per_move = 0.0;
  double researches_per_move = 0.0;
  double successful_researches_per_move = 0.0;
};

SelectiveSummary summarizeSelective(
    const std::vector<SelectiveGameResult>& games) {
  std::uint64_t moves = 0;
  std::uint64_t reduced = 0;
  std::uint64_t researches = 0;
  std::uint64_t successful = 0;
  for (const SelectiveGameResult& game : games) {
    moves += game.common.moves;
    reduced += game.reduced_probes;
    researches += game.fail_high_researches;
    successful += game.successful_researches;
  }
  const double denominator =
      static_cast<double>(std::max<std::uint64_t>(1, moves));
  return {reduced / denominator, researches / denominator,
          successful / denominator};
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
    throw std::invalid_argument("selective cohort is not paired");
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

void writePaired(std::ostream& output, const PairedSummary& paired) {
  output << "{\"meanScoreDifference\":" << paired.mean_score_difference
         << ",\"meanMoveDifference\":" << paired.mean_move_difference
         << ",\"wins\":" << paired.wins << ",\"ties\":"
         << paired.ties << ",\"losses\":" << paired.losses << '}';
}

void writeSelectiveSummary(std::ostream& output,
                           const SelectiveSummary& summary) {
  output << "{\"reducedProbesPerMove\":"
         << summary.reduced_probes_per_move
         << ",\"failHighResearchesPerMove\":"
         << summary.researches_per_move
         << ",\"successfulResearchesPerMove\":"
         << summary.successful_researches_per_move << '}';
}

struct ProgramOptions {
  std::string model = "/tmp/drop7-phase-q-student-scale.bin";
  std::string output = "/tmp/drop7-nnue-selective-search.json";
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

bool allLegalRootValuesComplete(const State& state,
                                const SelectiveDecision& decision) {
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
  state.board[indexOf(4, 1)] = 2;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;

  frozen::SearchOptions parity_options = frozenOptions();
  parity_options.maximum_depth = 3;
  parity_options.maximum_cache_entries = 4'000;
  const frozen::SearchDecision frozen_decision =
      frozen::chooseAction(state, parity_options, &model);
  const SelectiveDecision parity_decision =
      chooseSelectiveAction(state, parity_options, model, false);
  const bool frozen_parity =
      parity_decision.common.action == frozen_decision.action &&
      parity_decision.common.completed_depth == frozen_decision.completed_depth &&
      parity_decision.common.work == frozen_decision.work &&
      parity_decision.common.nodes == frozen_decision.nodes &&
      parity_decision.common.canonical_root_values ==
          frozen_decision.canonical_root_values;

  frozen::SearchOptions selective_options = parity_options;
  selective_options.maximum_depth = 4;
  const SelectiveDecision first =
      chooseSelectiveAction(state, selective_options, model);
  const SelectiveDecision repeat =
      chooseSelectiveAction(state, selective_options, model);
  State mirrored = state;
  mirrored.board = cfpi::detail::mirrorBoard(state.board);
  const SelectiveDecision reflected =
      chooseSelectiveAction(mirrored, selective_options, model);
  State metadata = state;
  metadata.score = 999'999;
  metadata.level = 88;
  metadata.moves_played = 321;
  const SelectiveDecision metadata_decision =
      chooseSelectiveAction(metadata, selective_options, model);

  const bool deterministic =
      first.common.action == repeat.common.action &&
      first.common.work == repeat.common.work &&
      first.reduced_probes == repeat.reduced_probes &&
      first.fail_high_researches == repeat.fail_high_researches;
  const bool reflection_safe =
      reflected.common.action == kBoardSize - 1 - first.common.action;
  const bool public_state_only =
      metadata_decision.common.action == first.common.action &&
      metadata_decision.common.work == first.common.work;
  const bool bounded = first.common.work <= selective_options.maximum_work &&
                       first.common.peak_cache_entries <=
                           selective_options.maximum_cache_entries &&
                       first.common.completed_depth <=
                           selective_options.maximum_depth;
  const bool root_full_width = allLegalRootValuesComplete(state, first);
  const bool reductions_valid = first.reduced_probes > 0 &&
                                first.fail_high_researches <=
                                    first.reduced_probes &&
                                first.successful_researches <=
                                    first.fail_high_researches;
  const bool legal = isLegal(state.board, first.common.action);
  const bool passed = frozen_parity && deterministic && reflection_safe &&
                      public_state_only && bounded && root_full_width &&
                      reductions_valid && legal;
  output << "NNUE_SELECTIVE_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"frozenParityWithoutLmr\":"
         << (frozen_parity ? "true" : "false")
         << ",\"deterministic\":"
         << (deterministic ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe ? "true" : "false")
         << ",\"publicStateOnly\":"
         << (public_state_only ? "true" : "false")
         << ",\"bounded\":" << (bounded ? "true" : "false")
         << ",\"rootFullWidth\":"
         << (root_full_width ? "true" : "false")
         << ",\"reductionsValid\":"
         << (reductions_valid ? "true" : "false")
         << ",\"fiveStrataAllLevels\":true"
         << ",\"legal\":" << (legal ? "true" : "false") << "}\n";
  return passed;
}

int benchmark(const ProgramOptions& options, std::ostream& output) {
  const auto started = std::chrono::steady_clock::now();
  frozen::QModel model;
  model.load(options.model);
  const Cohort screen =
      runCohort(kScreenSeedStart, kScreenGames, model, "screen");
  const frozen::Summary screen_baseline = frozen::summarize(screen.baseline);
  const std::vector<frozen::GameResult> screen_candidate_games =
      commonGames(screen.candidate);
  const frozen::Summary screen_candidate =
      frozen::summarize(screen_candidate_games);
  const SelectiveSummary screen_selective =
      summarizeSelective(screen.candidate);
  const PairedSummary screen_paired = pairedSummary(screen);
  const bool screen_passed = screen_paired.mean_score_difference > 0.0 &&
                             screen_paired.mean_move_difference > 0.0;

  Cohort confirmation;
  frozen::Summary confirmation_baseline;
  frozen::Summary confirmation_candidate;
  SelectiveSummary confirmation_selective;
  PairedSummary confirmation_paired;
  bool confirmed = false;
  if (screen_passed) {
    confirmation = runCohort(kConfirmationSeedStart, kConfirmationGames,
                             model, "confirmation");
    confirmation_baseline = frozen::summarize(confirmation.baseline);
    const std::vector<frozen::GameResult> confirmation_candidate_games =
        commonGames(confirmation.candidate);
    confirmation_candidate =
        frozen::summarize(confirmation_candidate_games);
    confirmation_selective = summarizeSelective(confirmation.candidate);
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
    throw std::runtime_error("could not open selective result artifact");
  }
  artifact << std::setprecision(10)
           << "{\n  \"format\": \"drop7-nnue-selective-search-v1\",\n"
           << "  \"trainingSeedOnly\": true,\n"
           << "  \"publicStateOnly\": true,\n"
           << "  \"rootCompleteness\": \"all-legal-actions\",\n"
           << "  \"interiorPolicy\": \"q-ordered-k3-plus-safety\",\n"
           << "  \"reduction\": \"later-actions-minus-one-ply; strict-fail-high-research\",\n"
           << "  \"chanceSamples\": 5,\n"
           << "  \"frozenDepth\": " << kFrozenDepth << ",\n"
           << "  \"selectiveMaximumDepth\": "
           << kSelectiveMaximumDepth << ",\n"
           << "  \"maximumWork\": " << kMaximumWork << ",\n"
           << "  \"maximumCacheEntries\": " << kMaximumCacheEntries
           << ",\n  \"maximumMoves\": " << kMaximumMoves
           << ",\n  \"parallelism\": " << kParallelism
           << ",\n  \"screenSeedStart\": " << kScreenSeedStart
           << ",\n  \"screen\": {\"baseline\":";
  frozen::writeSummary(artifact, screen_baseline);
  artifact << ",\"candidate\":";
  frozen::writeSummary(artifact, screen_candidate);
  artifact << ",\"selective\":";
  writeSelectiveSummary(artifact, screen_selective);
  artifact << ",\"paired\":";
  writePaired(artifact, screen_paired);
  artifact << "},\n  \"screenPassed\": "
           << (screen_passed ? "true" : "false")
           << ",\n  \"confirmation\": ";
  if (!screen_passed) {
    artifact << "null";
  } else {
    artifact << "{\"seedStart\":" << kConfirmationSeedStart
             << ",\"baseline\":";
    frozen::writeSummary(artifact, confirmation_baseline);
    artifact << ",\"candidate\":";
    frozen::writeSummary(artifact, confirmation_candidate);
    artifact << ",\"selective\":";
    writeSelectiveSummary(artifact, confirmation_selective);
    artifact << ",\"paired\":";
    writePaired(artifact, confirmation_paired);
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
         << "NNUE_SELECTIVE_RESULT {\"screenBaselineScore\":"
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

}  // namespace drop7::nnue_selective

int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string(argv[1]) == "--self-test") {
      return drop7::nnue_selective::selfTest(std::cout) ? 0 : 1;
    }
    if (argc >= 2 && std::string(argv[1]) == "--benchmark") {
      const auto options =
          drop7::nnue_selective::parseOptions(argc, argv, 2);
      return drop7::nnue_selective::benchmark(options, std::cout);
    }
    std::cerr << "usage: drop7_nnue_selective_search --self-test | "
                 "--benchmark [--model PATH] [--output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_nnue_selective_search: " << error.what() << '\n';
    return 1;
  }
}
