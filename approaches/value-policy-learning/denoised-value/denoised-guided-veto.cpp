#define main drop7_denoised_stochastic_value_frozen_main
#include "denoised-stochastic-value.cpp"
#undef main

#define main drop7_nnue_guided_frozen_main
#include "../../tree-search/nnue-guided/nnue-guided-search.cpp"
#undef main

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
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Combines the fixed denoised value model and guided K3 ensemble in isolation.
// The denoised value model cannot nominate an action: it may only veto the
// three-member guided K3 ensemble in favor of a fixed exact-d3
// fallback.  Both alternatives are compared on the same five root strata.
namespace drop7::denoised_guided_veto {

namespace denoised = drop7::denoised_stochastic_value;
namespace guided = drop7::nnue_guided;

constexpr std::uint32_t kScreenSeedStart = 0x3e84'0000u;
constexpr std::uint32_t kConfirmationSeedStart = 0x3e85'0000u;
constexpr int kScreenGames = 4;
constexpr int kConfirmationGames = 8;
constexpr int kMaximumMoves = 200;
constexpr int kParallelism = 4;
constexpr int kGuidedDepth = 5;
constexpr int kGuidedTopK = 3;
constexpr std::uint64_t kGuidedMaximumWork = 250'000;
constexpr std::size_t kGuidedMaximumCacheEntries = 40'000;
constexpr int kExactDepth = 3;
constexpr int kExactChanceSamples = 5;
constexpr std::uint64_t kExactMaximumWork = 1'000'000;
constexpr std::size_t kExactMaximumCacheEntries = 40'000;
constexpr double kVetoMargin = 2.744151;
constexpr std::uint64_t kMultiplyAddsPerInference =
    2u * (denoised::kActiveCategories * denoised::kHidden1 +
          denoised::kMetricCount * denoised::kHidden1 +
          denoised::kHidden1 * denoised::kHidden2 +
          denoised::kHeads * denoised::kHidden2);

static_assert(kMultiplyAddsPerInference == 12'864);
static_assert(kScreenSeedStart + kScreenGames < kConfirmationSeedStart);

std::mutex progress_mutex;

struct Options {
  std::string guided_model = "/tmp/drop7-phase-q-student-scale.bin";
  std::string value_model =
      "artifacts/models/denoised-value/v1.bin";
  std::string output = "/tmp/drop7-denoised-guided-veto.json";
};

guided::SearchOptions guidedOptions() {
  guided::SearchOptions result;
  result.maximum_depth = kGuidedDepth;
  result.top_k = kGuidedTopK;
  result.guided = true;
  result.safety_union = true;
  result.maximum_work = kGuidedMaximumWork;
  result.maximum_cache_entries = kGuidedMaximumCacheEntries;
  result.policy_seed = guided::kEnsemblePolicySeeds[0];
  return result;
}

cfpi::BehaviorOptions exactOptions() {
  cfpi::BehaviorOptions result;
  result.max_depth = kExactDepth;
  result.chance_samples = kExactChanceSamples;
  result.max_work = kExactMaximumWork;
  result.max_cache_entries = kExactMaximumCacheEntries;
  return result;
}

std::uint64_t fileBytes(const std::string& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) throw std::runtime_error("could not open model artifact " + path);
  const std::streamoff length = input.tellg();
  if (length < 0) throw std::runtime_error("could not size model artifact");
  return static_cast<std::uint64_t>(length);
}

struct ValueEstimate {
  double mean_lifetime = 0.0;
  std::uint64_t transitions = 0;
  std::uint64_t inferences = 0;
};

ValueEstimate estimateCanonicalAction(
    const State& canonical, int canonical_action,
    const denoised::ModelBundle& model) {
  if (!isLegal(canonical.board, canonical_action)) {
    throw std::invalid_argument("cannot value an illegal canonical action");
  }
  ValueEstimate result;
  for (int sample = 0; sample < denoised::kRootStrata; ++sample) {
    const denoised::RootSuccessor successor =
        denoised::rootSuccessor(canonical, canonical_action, sample);
    ++result.transitions;
    if (successor.terminal) continue;
    result.mean_lifetime +=
        denoised::predict(model.network, model.normalizer, model.calibrator,
                           successor.state)
            .lifetime /
        denoised::kRootStrata;
    ++result.inferences;
  }
  return result;
}

ValueEstimate estimateAction(const State& source, int action,
                             const denoised::ModelBundle& model) {
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  const int canonical_action = mirrored ? kBoardSize - 1 - action : action;
  return estimateCanonicalAction(canonical, canonical_action, model);
}

struct VetoSelection {
  int action = -1;
  bool vetoed = false;
};

VetoSelection selectVeto(int ensemble_action, int exact_action,
                         double exact_advantage) {
  const bool veto = ensemble_action != exact_action &&
                    exact_advantage >= kVetoMargin;
  return {veto ? exact_action : ensemble_action, veto};
}

struct MoveDecision {
  int action = -1;
  int ensemble_action = -1;
  int exact_action = -1;
  bool disagreed = false;
  bool vetoed = false;
  double exact_advantage = 0.0;
  std::uint64_t guided_work = 0;
  std::uint64_t exact_work = 0;
  std::uint64_t value_transitions = 0;
  std::uint64_t value_inferences = 0;
  std::size_t peak_cache_entries = 0;
};

MoveDecision chooseCandidateAction(const State& state,
                                   const guided::SearchOptions& options,
                                   const guided::QModel& q_model,
                                   const denoised::ModelBundle& value_model) {
  MoveDecision result;
  const guided::SearchDecision ensemble =
      guided::chooseEnsembleAction(state, options, q_model);
  result.ensemble_action = ensemble.action;
  result.guided_work = ensemble.work;
  result.peak_cache_entries = ensemble.peak_cache_entries;
  if (!isLegal(state.board, result.ensemble_action)) {
    throw std::runtime_error("guided ensemble produced an illegal action");
  }

  cfpi::BehaviorMetrics exact_metrics;
  result.exact_action =
      cfpi::chooseBehaviorAction(state, exactOptions(), &exact_metrics);
  result.exact_work = exact_metrics.work;
  if (!exact_metrics.complete || exact_metrics.completed_depth != kExactDepth) {
    throw std::runtime_error("exact fallback did not complete full depth three");
  }
  if (!isLegal(state.board, result.exact_action)) {
    throw std::runtime_error("exact fallback produced an illegal action");
  }

  result.disagreed = result.ensemble_action != result.exact_action;
  if (result.disagreed) {
    bool mirrored = false;
    const State canonical = cfpi::detail::canonicalState(state, mirrored);
    const int ensemble_canonical =
        mirrored ? kBoardSize - 1 - result.ensemble_action
                 : result.ensemble_action;
    const int exact_canonical =
        mirrored ? kBoardSize - 1 - result.exact_action : result.exact_action;
    const ValueEstimate ensemble_value =
        estimateCanonicalAction(canonical, ensemble_canonical, value_model);
    const ValueEstimate exact_value =
        estimateCanonicalAction(canonical, exact_canonical, value_model);
    result.exact_advantage =
        exact_value.mean_lifetime - ensemble_value.mean_lifetime;
    result.value_transitions =
        ensemble_value.transitions + exact_value.transitions;
    result.value_inferences =
        ensemble_value.inferences + exact_value.inferences;
  }
  const VetoSelection selection = selectVeto(
      result.ensemble_action, result.exact_action, result.exact_advantage);
  result.action = selection.action;
  result.vetoed = selection.vetoed;
  if (result.action != result.ensemble_action &&
      result.action != result.exact_action) {
    throw std::logic_error("veto policy nominated a third action");
  }
  return result;
}

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t decisions = 0;
  std::uint64_t disagreements = 0;
  std::uint64_t vetoes = 0;
  double disagreement_advantage_sum = 0.0;
  double veto_advantage_sum = 0.0;
  std::uint64_t guided_work = 0;
  std::uint64_t exact_work = 0;
  std::uint64_t value_transitions = 0;
  std::uint64_t value_inferences = 0;
  std::size_t peak_cache_entries = 0;
  std::uint64_t peak_rss_bytes = 0;
  double elapsed_seconds = 0.0;
};

void reportGame(std::string_view label, const GameResult& result) {
  const std::lock_guard<std::mutex> lock(progress_mutex);
  std::cerr << label << " seed 0x" << std::hex << result.seed << std::dec
            << ' ' << result.score << " (" << result.moves << " moves, "
            << result.vetoes << '/' << result.disagreements << " vetoes, "
            << "guided work " << result.guided_work << ", exact work "
            << result.exact_work << ")\n";
}

GameResult runBaselineGame(std::uint32_t seed,
                           const guided::SearchOptions& options,
                           const guided::QModel& model,
                           std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    const guided::SearchDecision decision =
        guided::chooseEnsembleAction(state, options, model);
    if (!isLegal(state.board, decision.action)) {
      throw std::runtime_error("baseline ensemble produced an illegal action");
    }
    ++result.decisions;
    result.guided_work += decision.work;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, decision.peak_cache_entries);
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("baseline headless transition failed");
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.peak_rss_bytes = guided::peakRssBytes();
  result.elapsed_seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
  reportGame(label, result);
  return result;
}

GameResult runCandidateGame(std::uint32_t seed,
                            const guided::SearchOptions& options,
                            const guided::QModel& q_model,
                            const denoised::ModelBundle& value_model,
                            std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    const MoveDecision decision =
        chooseCandidateAction(state, options, q_model, value_model);
    ++result.decisions;
    result.disagreements += decision.disagreed;
    result.vetoes += decision.vetoed;
    if (decision.disagreed) {
      result.disagreement_advantage_sum += decision.exact_advantage;
    }
    if (decision.vetoed) {
      result.veto_advantage_sum += decision.exact_advantage;
    }
    result.guided_work += decision.guided_work;
    result.exact_work += decision.exact_work;
    result.value_transitions += decision.value_transitions;
    result.value_inferences += decision.value_inferences;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, decision.peak_cache_entries);
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("candidate headless transition failed");
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.peak_rss_bytes = guided::peakRssBytes();
  result.elapsed_seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started)
                               .count();
  reportGame(label, result);
  return result;
}

struct Cohort {
  std::vector<GameResult> baseline;
  std::vector<GameResult> candidate;
  double wall_seconds = 0.0;
};

Cohort runCohort(std::uint32_t seed_start, int games,
                 const guided::QModel& q_model,
                 const denoised::ModelBundle& value_model,
                 std::string_view phase) {
  const auto started = std::chrono::steady_clock::now();
  Cohort result;
  result.baseline.resize(games);
  result.candidate.resize(games);
  const guided::SearchOptions options = guidedOptions();
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= games) return;
        const std::uint32_t seed =
            seed_start + static_cast<std::uint32_t>(game);
        result.baseline[game] = runBaselineGame(
            seed, options, q_model, std::string(phase) + "-ensemble");
        result.candidate[game] = runCandidateGame(
            seed, options, q_model, value_model,
            std::string(phase) + "-veto");
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
  std::uint64_t decisions = 0;
  std::uint64_t disagreements = 0;
  std::uint64_t vetoes = 0;
  double disagreement_rate = 0.0;
  double veto_rate = 0.0;
  double veto_rate_on_disagreements = 0.0;
  double mean_exact_advantage_on_disagreements = 0.0;
  double mean_exact_advantage_on_vetoes = 0.0;
  std::uint64_t guided_work = 0;
  std::uint64_t exact_work = 0;
  std::uint64_t value_transitions = 0;
  std::uint64_t value_inferences = 0;
  std::uint64_t value_multiply_adds = 0;
  double guided_work_per_move = 0.0;
  double exact_work_per_move = 0.0;
  double aggregate_game_seconds = 0.0;
  std::size_t peak_cache_entries = 0;
  std::uint64_t peak_rss_bytes = 0;
};

Summary summarize(const std::vector<GameResult>& games) {
  if (games.empty()) throw std::invalid_argument("cannot summarize no games");
  Summary result;
  result.games = static_cast<int>(games.size());
  double disagreement_advantage_sum = 0.0;
  double veto_advantage_sum = 0.0;
  std::uint64_t moves = 0;
  for (const GameResult& game : games) {
    result.mean_score += static_cast<double>(game.score) / games.size();
    result.mean_moves += static_cast<double>(game.moves) / games.size();
    result.censored += game.censored;
    result.decisions += game.decisions;
    result.disagreements += game.disagreements;
    result.vetoes += game.vetoes;
    disagreement_advantage_sum += game.disagreement_advantage_sum;
    veto_advantage_sum += game.veto_advantage_sum;
    result.guided_work += game.guided_work;
    result.exact_work += game.exact_work;
    result.value_transitions += game.value_transitions;
    result.value_inferences += game.value_inferences;
    result.aggregate_game_seconds += game.elapsed_seconds;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, game.peak_cache_entries);
    result.peak_rss_bytes =
        std::max(result.peak_rss_bytes, game.peak_rss_bytes);
    moves += static_cast<std::uint64_t>(game.moves);
  }
  const double decision_count =
      static_cast<double>(std::max<std::uint64_t>(1, result.decisions));
  const double disagreement_count =
      static_cast<double>(std::max<std::uint64_t>(1, result.disagreements));
  const double veto_count =
      static_cast<double>(std::max<std::uint64_t>(1, result.vetoes));
  const double move_count =
      static_cast<double>(std::max<std::uint64_t>(1, moves));
  result.disagreement_rate = result.disagreements / decision_count;
  result.veto_rate = result.vetoes / decision_count;
  result.veto_rate_on_disagreements = result.vetoes / disagreement_count;
  result.mean_exact_advantage_on_disagreements =
      disagreement_advantage_sum / disagreement_count;
  result.mean_exact_advantage_on_vetoes = veto_advantage_sum / veto_count;
  result.value_multiply_adds =
      result.value_inferences * kMultiplyAddsPerInference;
  result.guided_work_per_move = result.guided_work / move_count;
  result.exact_work_per_move = result.exact_work / move_count;
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
  if (values.empty()) throw std::invalid_argument("empty paired differences");
  DifferenceStats result;
  for (const double value : values) {
    result.mean += value / values.size();
    result.wins += value > 0.0;
    result.ties += value == 0.0;
    result.losses += value < 0.0;
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

struct PairedSummary {
  DifferenceStats score;
  DifferenceStats moves;
};

PairedSummary pairedSummary(const std::vector<GameResult>& baseline,
                            const std::vector<GameResult>& candidate) {
  if (baseline.size() != candidate.size() || baseline.empty()) {
    throw std::invalid_argument("invalid paired cohort");
  }
  std::vector<double> scores;
  std::vector<double> moves;
  scores.reserve(baseline.size());
  moves.reserve(baseline.size());
  for (std::size_t game = 0; game < baseline.size(); ++game) {
    scores.push_back(static_cast<double>(candidate[game].score -
                                         baseline[game].score));
    moves.push_back(static_cast<double>(candidate[game].moves -
                                        baseline[game].moves));
  }
  return {differences(scores), differences(moves)};
}

bool improvesBothMeans(const Summary& baseline, const Summary& candidate) {
  return candidate.mean_score > baseline.mean_score &&
         candidate.mean_moves > baseline.mean_moves;
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"games\":" << summary.games
         << ",\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"censored\":" << summary.censored
         << ",\"decisions\":" << summary.decisions
         << ",\"disagreements\":" << summary.disagreements
         << ",\"vetoes\":" << summary.vetoes
         << ",\"disagreementRate\":" << summary.disagreement_rate
         << ",\"vetoRate\":" << summary.veto_rate
         << ",\"vetoRateOnDisagreements\":"
         << summary.veto_rate_on_disagreements
         << ",\"meanExactAdvantageOnDisagreements\":"
         << summary.mean_exact_advantage_on_disagreements
         << ",\"meanExactAdvantageOnVetoes\":"
         << summary.mean_exact_advantage_on_vetoes
         << ",\"guidedWork\":" << summary.guided_work
         << ",\"exactWork\":" << summary.exact_work
         << ",\"guidedWorkPerMove\":" << summary.guided_work_per_move
         << ",\"exactWorkPerMove\":" << summary.exact_work_per_move
         << ",\"valueTransitions\":" << summary.value_transitions
         << ",\"valueInferences\":" << summary.value_inferences
         << ",\"valueMultiplyAdds\":" << summary.value_multiply_adds
         << ",\"aggregateGameSeconds\":"
         << summary.aggregate_game_seconds
         << ",\"peakCacheEntries\":" << summary.peak_cache_entries
         << ",\"peakRssBytes\":" << summary.peak_rss_bytes << '}';
}

void writeDifference(std::ostream& output, const DifferenceStats& summary) {
  output << "{\"mean\":" << summary.mean
         << ",\"lower95\":" << summary.lower_95
         << ",\"wins\":" << summary.wins
         << ",\"ties\":" << summary.ties
         << ",\"losses\":" << summary.losses << '}';
}

void writePaired(std::ostream& output, const PairedSummary& summary) {
  output << "{\"score\":";
  writeDifference(output, summary.score);
  output << ",\"moves\":";
  writeDifference(output, summary.moves);
  output << '}';
}

void writeGames(std::ostream& output, const std::vector<GameResult>& games) {
  output << '[';
  for (std::size_t index = 0; index < games.size(); ++index) {
    if (index != 0) output << ',';
    const GameResult& game = games[index];
    output << "{\"seed\":" << game.seed << ",\"score\":" << game.score
           << ",\"moves\":" << game.moves
           << ",\"censored\":" << (game.censored ? "true" : "false")
           << ",\"disagreements\":" << game.disagreements
           << ",\"vetoes\":" << game.vetoes
           << ",\"guidedWork\":" << game.guided_work
           << ",\"exactWork\":" << game.exact_work
           << ",\"valueTransitions\":" << game.value_transitions
           << ",\"valueInferences\":" << game.value_inferences << '}';
  }
  output << ']';
}

void writeCohort(std::ostream& output, std::uint32_t seed_start,
                 const Cohort& cohort, const Summary& baseline,
                 const Summary& candidate, const PairedSummary& paired,
                 bool passed) {
  output << "{\"seedStart\":" << seed_start << ",\"baseline\":";
  writeSummary(output, baseline);
  output << ",\"candidate\":";
  writeSummary(output, candidate);
  output << ",\"paired\":";
  writePaired(output, paired);
  output << ",\"wallSeconds\":" << cohort.wall_seconds
         << ",\"passed\":" << (passed ? "true" : "false")
         << ",\"baselineGames\":";
  writeGames(output, cohort.baseline);
  output << ",\"candidateGames\":";
  writeGames(output, cohort.candidate);
  output << '}';
}

void writeArtifact(const Options& options, std::uint64_t guided_bytes,
                   std::uint64_t value_bytes, const Cohort& screen,
                   const Summary& screen_baseline,
                   const Summary& screen_candidate,
                   const PairedSummary& screen_paired, bool screen_passed,
                   const Cohort* confirmation,
                   const Summary* confirmation_baseline,
                   const Summary* confirmation_candidate,
                   const PairedSummary* confirmation_paired,
                   bool confirmation_passed, double total_wall) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open veto result artifact");
  output << std::setprecision(10)
         << "{\n  \"experiment\":\"denoised-guided-exact-veto\",\n"
         << "  \"preregistered\":true,\n"
         << "  \"scoring\":{\"levelBonus\":7000},\n"
         << "  \"policy\":{\"baseline\":\"three-member-guided-K3-root-Q-ensemble\","
            "\"fallback\":\"verified-full-width-exact-d3-s5\","
            "\"valueUse\":\"exact-only-veto-common-five-strata\","
            "\"thirdActionAllowed\":false,\"vetoMarginMoves\":"
         << kVetoMargin << "},\n"
         << "  \"limits\":{\"maximumMoves\":" << kMaximumMoves
         << ",\"parallelism\":" << kParallelism
         << ",\"guidedDepth\":" << kGuidedDepth
         << ",\"guidedTopK\":" << kGuidedTopK
         << ",\"guidedMaximumWorkPerMember\":" << kGuidedMaximumWork
         << ",\"guidedMaximumCacheEntries\":"
         << kGuidedMaximumCacheEntries
         << ",\"exactDepth\":" << kExactDepth
         << ",\"exactChanceSamples\":" << kExactChanceSamples
         << ",\"exactMaximumWork\":" << kExactMaximumWork
         << ",\"exactMaximumCacheEntries\":"
         << kExactMaximumCacheEntries << "},\n"
         << "  \"models\":{\"guidedPath\":\"" << options.guided_model
         << "\",\"guidedBytes\":" << guided_bytes
         << ",\"denoisedPath\":\"" << options.value_model
         << "\",\"denoisedBytes\":" << value_bytes
         << ",\"combinedBytes\":" << guided_bytes + value_bytes
         << ",\"denoisedPayloadChecksum\":"
         << denoised::checkpointChecksum(
                denoised::readCheckpointBytes(options.value_model),
                denoised::kCheckpointHeaderBytes)
         << "},\n  \"screen\":";
  writeCohort(output, kScreenSeedStart, screen, screen_baseline,
              screen_candidate, screen_paired, screen_passed);
  output << ",\n  \"confirmation\":";
  if (confirmation == nullptr) {
    output << "null";
  } else {
    writeCohort(output, kConfirmationSeedStart, *confirmation,
                *confirmation_baseline, *confirmation_candidate,
                *confirmation_paired, confirmation_passed);
  }
  output << ",\n  \"screenPassed\":"
         << (screen_passed ? "true" : "false")
         << ",\n  \"confirmationRan\":"
         << (confirmation != nullptr ? "true" : "false")
         << ",\n  \"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\n  \"qualified\":"
         << (screen_passed && confirmation_passed ? "true" : "false")
         << ",\n  \"totalWallSeconds\":" << total_wall
         << ",\n  \"peakRssBytes\":" << guided::peakRssBytes() << "\n}\n";
}

bool selfTest(const Options& options, std::ostream& output) {
  const bool denoised_test = denoised::selfTest(output);
  const bool guided_test = guided::selfTest(options.guided_model, output);
  const denoised::ModelBundle value_model =
      denoised::loadModel(options.value_model);
  guided::QModel q_model;
  q_model.load(options.guided_model);

  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(4, 4)] = kCracked;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;
  State reflected = state;
  reflected.board = cfpi::detail::mirrorBoard(state.board);
  State metadata_changed = state;
  metadata_changed.score = 9'876'543;
  metadata_changed.level = 87;
  metadata_changed.moves_played = 321;

  const ValueEstimate first = estimateAction(state, 1, value_model);
  const ValueEstimate mirrored =
      estimateAction(reflected, kBoardSize - 1 - 1, value_model);
  const ValueEstimate metadata =
      estimateAction(metadata_changed, 1, value_model);
  const bool value_reflection_safe =
      first.mean_lifetime == mirrored.mean_lifetime &&
      first.transitions == mirrored.transitions &&
      first.inferences == mirrored.inferences;
  const bool public_state_only =
      first.mean_lifetime == metadata.mean_lifetime &&
      first.transitions == metadata.transitions &&
      first.inferences == metadata.inferences;
  const VetoSelection below = selectVeto(
      1, 4, std::nextafter(kVetoMargin, 0.0));
  const VetoSelection boundary = selectVeto(1, 4, kVetoMargin);
  const VetoSelection same =
      selectVeto(1, 1, std::numeric_limits<double>::infinity());
  const bool threshold_safe = below.action == 1 && !below.vetoed &&
                              boundary.action == 4 && boundary.vetoed &&
                              same.action == 1 && !same.vetoed;

  const guided::SearchDecision ensemble =
      guided::chooseEnsembleAction(state, guidedOptions(), q_model);
  cfpi::BehaviorMetrics exact_metrics;
  const int exact_action =
      cfpi::chooseBehaviorAction(state, exactOptions(), &exact_metrics);
  const bool exact_verified = exact_metrics.complete &&
                              exact_metrics.completed_depth == kExactDepth &&
                              isLegal(state.board, exact_action);
  const bool ensemble_legal = isLegal(state.board, ensemble.action);
  const MoveDecision combined = chooseCandidateAction(
      state, guidedOptions(), q_model, value_model);
  const bool no_third_action = combined.action == combined.ensemble_action ||
                               combined.action == combined.exact_action;
  const bool fixed_protocol =
      denoised::kRootStrata == 5 && kGuidedDepth == 5 &&
      kGuidedTopK == 3 && kExactDepth == 3 && kExactChanceSamples == 5 &&
      kVetoMargin == 2.744151 && kMaximumMoves == 200 &&
      kScreenSeedStart == 0x3e84'0000u &&
      kConfirmationSeedStart == 0x3e85'0000u;
  const bool model_sizes =
      fileBytes(options.guided_model) == 92'056 &&
      fileBytes(options.value_model) == 141'780;
  const bool passed = denoised_test && guided_test && value_reflection_safe &&
                      public_state_only && threshold_safe && exact_verified &&
                      ensemble_legal && no_third_action && fixed_protocol &&
                      model_sizes;
  output << std::setprecision(10)
         << "DENOISED_GUIDED_VETO_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"denoisedSelfTest\":"
         << (denoised_test ? "true" : "false")
         << ",\"guidedSelfTest\":" << (guided_test ? "true" : "false")
         << ",\"publicStateOnly\":"
         << (public_state_only ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (value_reflection_safe ? "true" : "false")
         << ",\"thresholdSafe\":"
         << (threshold_safe ? "true" : "false")
         << ",\"exactFallbackVerified\":"
         << (exact_verified ? "true" : "false")
         << ",\"ensembleLegal\":"
         << (ensemble_legal ? "true" : "false")
         << ",\"noThirdAction\":"
         << (no_third_action ? "true" : "false")
         << ",\"commonStrata\":" << denoised::kRootStrata
         << ",\"vetoMarginMoves\":" << kVetoMargin
         << ",\"guidedModelBytes\":" << fileBytes(options.guided_model)
         << ",\"denoisedModelBytes\":" << fileBytes(options.value_model)
         << ",\"combinedModelBytes\":"
         << fileBytes(options.guided_model) + fileBytes(options.value_model)
         << "}\n";
  return passed;
}

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing value for veto study argument");
    }
    const std::string argument = argv[index];
    const std::string value = argv[index + 1];
    if (argument == "--guided-model") {
      result.guided_model = value;
    } else if (argument == "--value-model") {
      result.value_model = value;
    } else if (argument == "--output") {
      result.output = value;
    } else {
      throw std::invalid_argument("unknown veto study argument " + argument);
    }
  }
  return result;
}

int run(const Options& options, std::ostream& output) {
  const auto started = std::chrono::steady_clock::now();
  guided::QModel q_model;
  q_model.load(options.guided_model);
  const denoised::ModelBundle value_model =
      denoised::loadModel(options.value_model);
  const std::uint64_t guided_bytes = fileBytes(options.guided_model);
  const std::uint64_t value_bytes = fileBytes(options.value_model);

  const Cohort screen = runCohort(kScreenSeedStart, kScreenGames, q_model,
                                  value_model, "screen");
  const Summary screen_baseline = summarize(screen.baseline);
  const Summary screen_candidate = summarize(screen.candidate);
  const PairedSummary screen_paired =
      pairedSummary(screen.baseline, screen.candidate);
  const bool screen_passed =
      improvesBothMeans(screen_baseline, screen_candidate);

  Cohort confirmation;
  Summary confirmation_baseline;
  Summary confirmation_candidate;
  PairedSummary confirmation_paired;
  bool confirmation_passed = false;
  if (screen_passed) {
    confirmation = runCohort(kConfirmationSeedStart, kConfirmationGames,
                             q_model, value_model, "confirmation");
    confirmation_baseline = summarize(confirmation.baseline);
    confirmation_candidate = summarize(confirmation.candidate);
    confirmation_paired =
        pairedSummary(confirmation.baseline, confirmation.candidate);
    confirmation_passed =
        improvesBothMeans(confirmation_baseline, confirmation_candidate);
  }
  const double total_wall = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - started)
                                .count();
  writeArtifact(
      options, guided_bytes, value_bytes, screen, screen_baseline,
      screen_candidate, screen_paired, screen_passed,
      screen_passed ? &confirmation : nullptr,
      screen_passed ? &confirmation_baseline : nullptr,
      screen_passed ? &confirmation_candidate : nullptr,
      screen_passed ? &confirmation_paired : nullptr, confirmation_passed,
      total_wall);
  output << std::fixed << std::setprecision(3)
         << "DENOISED_GUIDED_VETO_RESULT {\"levelBonus\":7000"
         << ",\"screenBaselineScore\":" << screen_baseline.mean_score
         << ",\"screenBaselineMoves\":" << screen_baseline.mean_moves
         << ",\"screenCandidateScore\":" << screen_candidate.mean_score
         << ",\"screenCandidateMoves\":" << screen_candidate.mean_moves
         << ",\"screenVetoRate\":" << screen_candidate.veto_rate
         << ",\"screenPassed\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmationRan\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\"combinedModelBytes\":" << guided_bytes + value_bytes
         << ",\"peakRssBytes\":" << guided::peakRssBytes()
         << ",\"totalWallSeconds\":" << total_wall
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return 0;
}

}  // namespace drop7::denoised_guided_veto

int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      const auto options =
          drop7::denoised_guided_veto::parseOptions(argc, argv, 2);
      return drop7::denoised_guided_veto::selfTest(options, std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::denoised_guided_veto::parseOptions(argc, argv, 2);
      return drop7::denoised_guided_veto::run(options, std::cout);
    }
    std::cerr << "usage: drop7_denoised_guided_veto --self-test|--run "
                 "[--guided-model PATH] [--value-model PATH] "
                 "[--output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
