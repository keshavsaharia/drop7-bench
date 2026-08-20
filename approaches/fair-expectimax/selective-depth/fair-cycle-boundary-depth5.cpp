#define DROP7_FAIR_SELECTIVE_DEPTH_NO_MAIN
#include "fair-selective-depth.cpp"
#undef DROP7_FAIR_SELECTIVE_DEPTH_NO_MAIN

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
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Uses completed fair-D5/w2 only when the visible phase is exactly one full
// five-move rise cycle away.  All other
// decisions use the reference full-width fair-D4 policy.  A {4,5} ablation is
// eligible only when the primary policy fails its fitting gate.
namespace drop7::fair_cycle_boundary_depth5 {

namespace selective = drop7::fair_selective_depth;
namespace d4 = drop7::fair_only_depth4;
namespace frozen = drop7::fair_only_horizon;

constexpr std::uint32_t kFittingSeedStart = 0x3de7'0000u;
constexpr std::uint32_t kHeldoutSeedStart = 0x3de8'0000u;
constexpr std::uint32_t kScreenSeedStart = 0x3eb5'0000u;
constexpr std::uint32_t kConfirmationSeedStart = 0x3eb6'0000u;
constexpr int kFittingGames = 4;
constexpr int kHeldoutGames = 8;
constexpr int kScreenGames = 8;
constexpr int kConfirmationGames = 16;
constexpr int kMaximumMoves = 1'000;
constexpr int kParallelism = 4;
constexpr double kMaximumThroughputRegression = 0.01;
constexpr double kMaximumProjectedWallSeconds = 40.0 * 60.0;
// Worst case includes the optional fitting ablation and every later gate.
constexpr double kFirstPairProjectionMultiplier = 10.5;

enum class CycleKind { kD4, kPhase5, kPhase45 };

struct CycleSpec {
  const char* name = "";
  CycleKind kind = CycleKind::kD4;
};

constexpr CycleSpec kBaseline{"fair-d4", CycleKind::kD4};
constexpr CycleSpec kPrimary{"cycle-boundary-d5w2-phase5",
                             CycleKind::kPhase5};
constexpr CycleSpec kAblation{"cycle-boundary-d5w2-phases45",
                              CycleKind::kPhase45};

constexpr selective::PolicySpec artifactSpec(const CycleSpec& spec) {
  if (spec.kind == CycleKind::kD4) return selective::kBaseline;
  return {spec.name, selective::PolicyKind::kPhaseAligned, 5, 2};
}

static_assert(selective::kWorstD5W2Work == 2'760'835);
static_assert(selective::kWorstD5W2Cache == 38'885);
static_assert(selective::kWorstD5W2Work <
              selective::kMaximumSelectiveWork);
static_assert(selective::kWorstD5W2Cache <
              selective::kMaximumCacheEntries);
static_assert(frozen::kChanceSamples == 5);
static_assert(kLevelBonus == 7'000);
static_assert(kFittingSeedStart + kFittingGames < kHeldoutSeedStart);
static_assert(kHeldoutSeedStart + kHeldoutGames < kScreenSeedStart);
static_assert(kScreenSeedStart + kScreenGames < kConfirmationSeedStart);
static_assert((kFittingSeedStart >> 24) != 0x7du &&
              (kFittingSeedStart >> 24) != 0xd7u);
static_assert((kHeldoutSeedStart >> 24) != 0x7du &&
              (kHeldoutSeedStart >> 24) != 0xd7u);
static_assert((kScreenSeedStart >> 24) != 0x7du &&
              (kScreenSeedStart >> 24) != 0xd7u);
static_assert((kConfirmationSeedStart >> 24) != 0x7du &&
              (kConfirmationSeedStart >> 24) != 0xd7u);

bool usesDepth5(const CycleSpec& spec, int moves_remaining) {
  if (spec.kind == CycleKind::kPhase5) return moves_remaining == 5;
  if (spec.kind == CycleKind::kPhase45) return moves_remaining >= 4;
  return false;
}

selective::SearchDecision chooseCycleAction(const State& source,
                                            const CycleSpec& spec) {
  if (!usesDepth5(spec, source.moves_remaining)) {
    return selective::wrapDepth4(d4::chooseDepth4Action(source));
  }
  const selective::SearchDecision result = selective::chooseUniformSelective(
      source, selective::kUniformMenu[0]);
  if (!result.complete || !result.selective_complete || result.used_fallback ||
      !result.full_root || result.requested_depth != 5 ||
      result.internal_width != 2) {
    throw std::runtime_error("cycle-boundary D5/w2 failed to complete");
  }
  return result;
}

selective::GameResult runGame(const CycleSpec& spec, std::uint32_t seed,
                              std::string_view phase_name) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  selective::GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    const int phase_index = state.moves_remaining - 1;
    if (phase_index < 0 || phase_index >= kMovesPerLevel) {
      throw std::runtime_error("invalid cycle-boundary phase");
    }
    const selective::SearchDecision decision = chooseCycleAction(state, spec);
    if (!decision.complete || !decision.full_root ||
        !isLegal(state.board, decision.action)) {
      throw std::runtime_error("cycle-boundary policy chose invalid action");
    }
    selective::PhaseStats& phase = result.phase[phase_index];
    ++phase.decisions;
    phase.policy_work += decision.work;
    result.work += decision.work;
    result.selective_work += decision.selective_work;
    result.fallback_work += decision.fallback_work;
    result.nodes += decision.nodes;
    result.cache_hits += decision.cache_hits;
    result.ordering_work += decision.ordering_work;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, decision.peak_cache_entries);
    result.root_width_violations += !decision.full_root;
    if (usesDepth5(spec, state.moves_remaining)) {
      if (!decision.selective_complete || decision.used_fallback) {
        throw std::runtime_error("proved D5/w2 unexpectedly fell back");
      }
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("cycle-boundary transition failed");
    }
    selective::observeMove(move, result, phase);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.peak_rss_bytes = selective::peakRssBytes();
  result.elapsed_seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
  selective::reportGame(phase_name, artifactSpec(spec), result);
  return result;
}

selective::Cohort runCohort(const CycleSpec& spec,
                            std::uint32_t seed_start, int games,
                            std::string_view phase_name) {
  const auto started = std::chrono::steady_clock::now();
  selective::Cohort result;
  result.spec = artifactSpec(spec);
  result.maximum_moves = kMaximumMoves;
  result.games.resize(games);
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= games) return;
        result.games[game] = runGame(
            spec, seed_start + static_cast<std::uint32_t>(game), phase_name);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.wall_seconds = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started)
                            .count();
  return result;
}

void appendCohort(selective::Cohort& target,
                  selective::Cohort source) {
  target.games.insert(target.games.end(),
                      std::make_move_iterator(source.games.begin()),
                      std::make_move_iterator(source.games.end()));
  target.wall_seconds += source.wall_seconds;
}

struct Gate {
  selective::Comparison comparison{};
  int leave_one_out_positive_both = 0;
  bool means_positive = false;
  bool throughput_ok = false;
  bool fitting_passed = false;
  bool evaluation_passed = false;
};

int positiveLeaveOneOut(const selective::Cohort& baseline,
                        const selective::Cohort& candidate) {
  if (baseline.games.size() != candidate.games.size() ||
      baseline.games.size() < 2) {
    throw std::invalid_argument("invalid leave-one-out cohorts");
  }
  double score_total = 0.0;
  double move_total = 0.0;
  for (std::size_t index = 0; index < baseline.games.size(); ++index) {
    score_total += candidate.games[index].score - baseline.games[index].score;
    move_total += candidate.games[index].moves - baseline.games[index].moves;
  }
  int positive = 0;
  for (std::size_t index = 0; index < baseline.games.size(); ++index) {
    const double score =
        candidate.games[index].score - baseline.games[index].score;
    const double moves =
        candidate.games[index].moves - baseline.games[index].moves;
    positive += score_total - score > 0.0 && move_total - moves > 0.0;
  }
  return positive;
}

Gate gate(const selective::Cohort& baseline,
          const selective::Cohort& candidate, bool fitting) {
  const selective::Summary baseline_summary = selective::summarize(baseline);
  const selective::Summary candidate_summary = selective::summarize(candidate);
  Gate result;
  result.comparison = selective::compare(baseline, candidate);
  result.leave_one_out_positive_both =
      positiveLeaveOneOut(baseline, candidate);
  result.means_positive = result.comparison.both_means_positive;
  result.throughput_ok =
      candidate_summary.clears_per_move >=
          (1.0 - kMaximumThroughputRegression) *
              baseline_summary.clears_per_move &&
      candidate_summary.reveals_per_move >=
          (1.0 - kMaximumThroughputRegression) *
              baseline_summary.reveals_per_move;
  result.fitting_passed = result.means_positive && result.throughput_ok &&
                          result.leave_one_out_positive_both >= 3;
  result.evaluation_passed = result.means_positive && result.throughput_ok;
  if (!fitting) result.fitting_passed = false;
  return result;
}

struct Stage {
  selective::Cohort baseline;
  selective::Cohort candidate;
  selective::Summary baseline_summary;
  selective::Summary candidate_summary;
  Gate result;
};

Stage makeStage(selective::Cohort baseline,
                selective::Cohort candidate, bool fitting) {
  Stage result;
  result.baseline = std::move(baseline);
  result.candidate = std::move(candidate);
  result.baseline_summary = selective::summarize(result.baseline);
  result.candidate_summary = selective::summarize(result.candidate);
  result.result = gate(result.baseline, result.candidate, fitting);
  return result;
}

Stage runStage(const CycleSpec& candidate, std::uint32_t seed_start,
               int games, std::string_view phase_name) {
  return makeStage(
      runCohort(kBaseline, seed_start, games,
                std::string(phase_name) + "-baseline"),
      runCohort(candidate, seed_start, games,
                std::string(phase_name) + "-candidate"),
      false);
}

void writeGate(std::ostream& output, const Gate& result) {
  output << "{\"comparison\":";
  selective::writeComparison(output, result.comparison);
  output << ",\"leaveOneOutPositiveBoth\":"
         << result.leave_one_out_positive_both
         << ",\"meansPositive\":"
         << (result.means_positive ? "true" : "false")
         << ",\"throughputOk\":"
         << (result.throughput_ok ? "true" : "false")
         << ",\"fittingPassed\":"
         << (result.fitting_passed ? "true" : "false")
         << ",\"evaluationPassed\":"
         << (result.evaluation_passed ? "true" : "false") << '}';
}

void writeStage(std::ostream& output, const Stage& stage) {
  output << "{\"baseline\":";
  selective::writeCohort(output, stage.baseline, stage.baseline_summary);
  output << ",\"candidate\":";
  selective::writeCohort(output, stage.candidate, stage.candidate_summary);
  output << ",\"gate\":";
  writeGate(output, stage.result);
  output << '}';
}

struct Options {
  std::string output = "/tmp/drop7-fair-cycle-boundary-depth5.json";
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

void writePausedArtifact(const Options& options,
                         const selective::Cohort& baseline,
                         const selective::Cohort& candidate,
                         double first_pair_wall, double projected_wall) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open paused artifact");
  const selective::Summary baseline_summary = selective::summarize(baseline);
  const selective::Summary candidate_summary = selective::summarize(candidate);
  output << std::setprecision(12)
         << "{\"experiment\":\"fair-cycle-boundary-depth5\""
         << ",\"pilotPairOnly\":true"
         << ",\"formalInference\":false"
         << ",\"conclusion\":\"rejected-paused-by-preregistered-runtime-gate\""
         << ",\"pausedForProjectedWall\":true"
         << ",\"readGameSeeds\":[\"0x3de70000\"]"
         << ",\"untouchedRanges\":[\"0x3de70001...003 fitting remainder\",\"0x3de70000...003 phase45 ablation\",\"0x3de80000...007 heldout\",\"0x3eb50000...007 screen\",\"0x3eb60000...00f confirmation\",\"0x7d... protected\",\"0xd7... protected\"]"
         << ",\"firstPairWallSeconds\":" << first_pair_wall
         << ",\"projectedMaximumWallSeconds\":" << projected_wall
         << ",\"firstPairBaseline\":";
  selective::writeCohort(output, baseline, baseline_summary);
  output << ",\"firstPairCandidate\":";
  selective::writeCohort(output, candidate, candidate_summary);
  output << "}\n";
}

bool selfTest(std::ostream& output) {
  const bool dependency_test = selective::selfTest(output);
  State source = frozen::fixtureState(frozen::kTypeScriptFixtures[1]);
  for (const int column : {0, 1, 5, 6}) {
    for (int row = 0; row < kBoardSize; ++row) {
      source.board[indexOf(row, column)] = kSolid;
    }
  }
  source.game_over = false;
  source.moves_remaining = 5;
  const selective::SearchDecision primary = chooseCycleAction(source, kPrimary);
  const selective::SearchDecision repeat = chooseCycleAction(source, kPrimary);
  State reflected = source;
  reflected.board = cfpi::detail::mirrorBoard(source.board);
  const selective::SearchDecision mirror = chooseCycleAction(reflected, kPrimary);
  State metadata = source;
  metadata.score = 7'777'777;
  metadata.level = 88;
  metadata.moves_played = 654;
  const selective::SearchDecision metadata_result =
      chooseCycleAction(metadata, kPrimary);

  State phase4 = source;
  phase4.moves_remaining = 4;
  const selective::SearchDecision primary_phase4 =
      chooseCycleAction(phase4, kPrimary);
  const d4::SearchDecision exact_phase4 = d4::chooseDepth4Action(phase4);
  const selective::SearchDecision ablation_phase4 =
      chooseCycleAction(phase4, kAblation);
  State phase3 = source;
  phase3.moves_remaining = 3;
  const selective::SearchDecision primary_phase3 =
      chooseCycleAction(phase3, kPrimary);

  const bool deterministic =
      primary.action == repeat.action && primary.work == repeat.work &&
      primary.cache_hits == repeat.cache_hits;
  const bool reflection_safe =
      mirror.action == kBoardSize - 1 - primary.action &&
      mirror.work == primary.work;
  const bool public_only = metadata_result.action == primary.action &&
                           metadata_result.work == primary.work;
  const bool exact_phase_routing =
      primary.selective_complete && !primary.used_fallback &&
      primary.requested_depth == 5 && primary.internal_width == 2 &&
      primary_phase4.action == exact_phase4.action &&
      primary_phase4.work == exact_phase4.work &&
      !primary_phase4.selective_complete &&
      ablation_phase4.selective_complete &&
      !ablation_phase4.used_fallback &&
      primary_phase3.requested_depth == 4 &&
      !primary_phase3.selective_complete;
  const bool legal = isLegal(source.board, primary.action) &&
                     isLegal(phase4.board, primary_phase4.action) &&
                     isLegal(phase4.board, ablation_phase4.action);
  const bool complete = primary.complete && primary.full_root &&
                        primary_phase4.complete && primary_phase4.full_root &&
                        ablation_phase4.complete && ablation_phase4.full_root;
  const bool bounded =
      primary.work <= selective::kMaximumSelectiveWork &&
      primary.peak_cache_entries <= selective::kMaximumCacheEntries &&
      ablation_phase4.work <= selective::kMaximumSelectiveWork &&
      ablation_phase4.peak_cache_entries <=
          selective::kMaximumCacheEntries;
  const bool protocol =
      kFittingSeedStart == 0x3de7'0000u &&
      kHeldoutSeedStart == 0x3de8'0000u &&
      kScreenSeedStart == 0x3eb5'0000u &&
      kConfirmationSeedStart == 0x3eb6'0000u && kFittingGames == 4 &&
      kHeldoutGames == 8 && kScreenGames == 8 &&
      kConfirmationGames == 16 && kMaximumMoves == 1'000;
  const bool passed = dependency_test && deterministic && reflection_safe &&
                      public_only && exact_phase_routing && legal && complete &&
                      bounded && protocol;
  output << std::setprecision(12)
         << "FAIR_CYCLE_BOUNDARY_DEPTH5_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"dependencyTest\":"
         << (dependency_test ? "true" : "false")
         << ",\"deterministic\":" << (deterministic ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe ? "true" : "false")
         << ",\"publicMetadataAndGameSeedBlind\":"
         << (public_only ? "true" : "false")
         << ",\"exactPhaseRouting\":"
         << (exact_phase_routing ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false")
         << ",\"completeFullRoot\":"
         << (complete ? "true" : "false")
         << ",\"bounded\":" << (bounded ? "true" : "false")
         << ",\"phase5Action\":" << primary.action
         << ",\"phase5Work\":" << primary.work
         << ",\"worstD5W2Work\":" << selective::kWorstD5W2Work
         << ",\"worstD5W2Cache\":" << selective::kWorstD5W2Cache
         << "}\n";
  return passed;
}

int run(const Options& options, std::ostream& report) {
  const auto all_started = std::chrono::steady_clock::now();
  selective::Cohort fitting_baseline =
      runCohort(kBaseline, kFittingSeedStart, 1, "fit-first-baseline");
  selective::Cohort fitting_primary =
      runCohort(kPrimary, kFittingSeedStart, 1, "fit-first-primary");
  const double first_pair_wall =
      fitting_baseline.wall_seconds + fitting_primary.wall_seconds;
  const double projected_wall =
      first_pair_wall * kFirstPairProjectionMultiplier;
  {
    const std::lock_guard<std::mutex> lock(selective::progress_mutex);
    std::cerr << "first-pair wall " << first_pair_wall
              << " seconds, projected gated maximum " << projected_wall
              << " seconds\n";
  }
  if (projected_wall > kMaximumProjectedWallSeconds) {
    writePausedArtifact(options, fitting_baseline, fitting_primary,
                        first_pair_wall, projected_wall);
    report << "FAIR_CYCLE_BOUNDARY_DEPTH5_PAUSED {\"firstPairWallSeconds\":"
           << first_pair_wall << ",\"projectedMaximumWallSeconds\":"
           << projected_wall << ",\"limitSeconds\":"
           << kMaximumProjectedWallSeconds << ",\"artifact\":\""
           << options.output << "\"}\n";
    return 3;
  }

  appendCohort(fitting_baseline,
               runCohort(kBaseline, kFittingSeedStart + 1,
                         kFittingGames - 1, "fit-baseline"));
  appendCohort(fitting_primary,
               runCohort(kPrimary, kFittingSeedStart + 1,
                         kFittingGames - 1, "fit-primary"));
  Stage primary_fit =
      makeStage(fitting_baseline, fitting_primary, true);
  std::optional<Stage> ablation_fit;
  const CycleSpec* selected = nullptr;
  if (primary_fit.result.fitting_passed) {
    selected = &kPrimary;
  } else {
    selective::Cohort ablation =
        runCohort(kAblation, kFittingSeedStart, kFittingGames,
                  "fit-ablation");
    ablation_fit = makeStage(primary_fit.baseline, std::move(ablation), true);
    if (ablation_fit->result.fitting_passed) selected = &kAblation;
  }

  std::optional<Stage> heldout;
  std::optional<Stage> screen;
  std::optional<Stage> confirmation;
  bool heldout_passed = false;
  bool screen_passed = false;
  bool confirmation_passed = false;
  if (selected != nullptr) {
    heldout = runStage(*selected, kHeldoutSeedStart, kHeldoutGames, "heldout");
    heldout_passed = heldout->result.evaluation_passed;
  }
  if (heldout_passed) {
    screen = runStage(*selected, kScreenSeedStart, kScreenGames, "screen");
    screen_passed = screen->result.means_positive;
  }
  if (screen_passed) {
    confirmation = runStage(*selected, kConfirmationSeedStart,
                            kConfirmationGames, "confirmation");
    confirmation_passed = confirmation->result.evaluation_passed;
  }
  const double total_wall = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - all_started)
                                .count();

  std::ofstream artifact(options.output);
  if (!artifact) throw std::runtime_error("could not open cycle artifact");
  artifact << std::setprecision(12)
           << "{\n  \"experiment\":\"fair-cycle-boundary-depth5\",\n"
           << "  \"preregistered\":true,\n"
           << "  \"hypothesis\":\"D5w2 only at movesRemaining 5 spans one covered-row rise cycle\",\n"
           << "  \"search\":{\"d4\":\"qualified-full-width\""
           << ",\"cycleSearch\":\"completed-selective-d5-w2-s5\""
           << ",\"worstD5W2Work\":" << selective::kWorstD5W2Work
           << ",\"worstD5W2Cache\":" << selective::kWorstD5W2Cache
           << ",\"maximumMoves\":" << kMaximumMoves
           << ",\"parallelism\":" << kParallelism << "},\n"
           << "  \"wallProjection\":{\"firstPairSeconds\":"
           << first_pair_wall << ",\"multiplier\":"
           << kFirstPairProjectionMultiplier << ",\"projectedSeconds\":"
           << projected_wall << ",\"limitSeconds\":"
           << kMaximumProjectedWallSeconds << ",\"paused\":false},\n"
           << "  \"fittingPrimary\":";
  writeStage(artifact, primary_fit);
  artifact << ",\n  \"fittingAblation\":";
  if (ablation_fit) writeStage(artifact, *ablation_fit);
  else artifact << "null";
  artifact << ",\n  \"selected\":";
  if (selected == nullptr) artifact << "null";
  else selective::writeSpec(artifact, artifactSpec(*selected));
  artifact << ",\n  \"heldout\":";
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
           << ",\n  \"peakRssBytes\":" << selective::peakRssBytes()
           << ",\n  \"totalWallSeconds\":" << total_wall << "\n}\n";
  artifact.close();

  report << std::fixed << std::setprecision(6)
         << "FAIR_CYCLE_BOUNDARY_DEPTH5_RESULT {\"selected\":";
  if (selected == nullptr) report << "null";
  else report << '\"' << selected->name << '\"';
  report << ",\"primaryFitPassed\":"
         << (primary_fit.result.fitting_passed ? "true" : "false")
         << ",\"ablationRan\":" << (ablation_fit ? "true" : "false")
         << ",\"heldoutPassed\":" << (heldout_passed ? "true" : "false")
         << ",\"screenRan\":" << (screen ? "true" : "false")
         << ",\"screenPassed\":" << (screen_passed ? "true" : "false")
         << ",\"confirmationRan\":"
         << (confirmation ? "true" : "false")
         << ",\"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\"peakRssBytes\":" << selective::peakRssBytes()
         << ",\"totalWallSeconds\":" << total_wall
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return 0;
}

}  // namespace drop7::fair_cycle_boundary_depth5

int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::fair_cycle_boundary_depth5::selfTest(std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::fair_cycle_boundary_depth5::parseOptions(argc, argv, 2);
      return drop7::fair_cycle_boundary_depth5::run(options, std::cout);
    }
    std::cerr << "usage: drop7_fair_cycle_boundary_depth5 --self-test | "
                 "--run [--output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
