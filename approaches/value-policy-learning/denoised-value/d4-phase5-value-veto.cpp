#define main drop7_denoised_value_frozen_entrypoint
#include "denoised-stochastic-value.cpp"
#undef main

#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#include "../../fair-expectimax/reference/fair-only-depth4.cpp"
#undef DROP7_FAIR_ONLY_DEPTH4_LIBRARY

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
#include <mutex>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Uses the fixed denoised lifetime model only as a phase-five veto.  The
// reference full-width fair-D4 search always completes first and remains the
// fallback. At the only phase that D4 cannot see through the next covered-row rise, the
// model may veto D4 for another D4-near-tied action under fixed confidence,
// survival, orientation, and root-Q constraints.
namespace drop7::d4_phase5_value_veto {

namespace d4 = drop7::fair_only_depth4;
namespace denoised = drop7::denoised_stochastic_value;

constexpr std::uint32_t kFittingSeedStart = 0x3de9'0000u;
constexpr std::uint32_t kHeldoutSeedStart = 0x3dea'0000u;
constexpr std::uint32_t kScreenSeedStart = 0x3eb7'0000u;
constexpr std::uint32_t kConfirmationSeedStart = 0x3eb8'0000u;
constexpr int kFittingGames = 4;
constexpr int kHeldoutGames = 8;
constexpr int kScreenGames = 8;
constexpr int kConfirmationGames = 16;
constexpr int kMaximumMoves = 1'000;
constexpr int kParallelism = 4;
constexpr int kDangerHeight = 4;
constexpr double kLifetimeMargin = 2.744151;
constexpr double kMaximumOrientationGap = 5.0;
// One canonical level bonus is the maximum fair-D4 root-Q concession.  This
// comes only from fixed game scoring; evaluation outcomes do not define it.
constexpr double kMaximumRootQLoss = static_cast<double>(kLevelBonus);
constexpr double kLowerTailRetention = 0.90;
constexpr double kMaximumProjectedWallSeconds = 45.0 * 60.0;
// There are 72 policy-games in the complete four-stage protocol and four
// workers.  One paired pilot occupies one wall-time wave, so 18 waves is a
// conservative projection when the machine is otherwise stable.
constexpr double kFullProtocolProjectionWaves = 18.0;
constexpr std::uint64_t kExpectedCheckpointBytes = 141'780;
constexpr std::uint32_t kExpectedPayloadChecksum = 1'239'007'257u;
constexpr std::uint64_t kMultiplyAddsPerInference =
    2u * (denoised::kActiveCategories * denoised::kHidden1 +
          denoised::kMetricCount * denoised::kHidden1 +
          denoised::kHidden1 * denoised::kHidden2 +
          denoised::kHeads * denoised::kHidden2);

static_assert(kMultiplyAddsPerInference == 12'864);
static_assert(d4::kCandidateDepth == 4);
static_assert(d4::kChanceSamples == denoised::kRootStrata);
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

std::mutex progress_mutex;

struct Options {
  std::string model = "artifacts/models/denoised-value/v1.bin";
  std::string output = "/tmp/drop7-d4-phase5-value-veto.json";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string argument = argv[index];
    if (argument == "--model") {
      result.model = argv[index + 1];
    } else if (argument == "--output") {
      result.output = argv[index + 1];
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  return result;
}

denoised::ModelBundle loadCheckedModel(const std::string& path) {
  const std::vector<std::uint8_t> bytes = denoised::readCheckpointBytes(path);
  if (bytes.size() != kExpectedCheckpointBytes) {
    throw std::runtime_error("unexpected denoised checkpoint byte count");
  }
  if (denoised::checkpointChecksum(bytes, denoised::kCheckpointHeaderBytes) !=
      kExpectedPayloadChecksum) {
    throw std::runtime_error("unexpected denoised checkpoint payload checksum");
  }
  return denoised::deserializeModel(bytes);
}

int maximumHeight(const Board& board) {
  const std::array<int, kBoardSize> heights = cfpi::detail::columnHeights(board);
  return *std::max_element(heights.begin(), heights.end());
}

bool isPhase5Danger(const State& state) {
  return state.moves_remaining == kMovesPerLevel &&
         maximumHeight(state.board) >= kDangerHeight;
}

struct ActionEstimate {
  std::array<double, denoised::kRootStrata> lifetime{};
  std::array<double, denoised::kRootStrata> survival25{};
  double mean_lifetime = 0.0;
  double mean_survival25 = 0.0;
  double maximum_orientation_gap = 0.0;
  std::uint64_t transitions = 0;
  std::uint64_t inferences = 0;
};

ActionEstimate estimateCanonicalAction(
    const State& canonical, int action,
    const denoised::ModelBundle& model) {
  if (!isLegal(canonical.board, action)) {
    throw std::invalid_argument("cannot estimate illegal canonical action");
  }
  ActionEstimate result;
  for (int sample = 0; sample < denoised::kRootStrata; ++sample) {
    const denoised::RootSuccessor successor =
        denoised::rootSuccessor(canonical, action, sample);
    ++result.transitions;
    if (successor.terminal) {
      // The legal root drop itself was survived, but there is no future state.
      result.lifetime[sample] = 1.0;
      result.survival25[sample] = 0.0;
    } else {
      const denoised::Prediction prediction = denoised::predict(
          model.network, model.normalizer, model.calibrator, successor.state);
      result.lifetime[sample] = 1.0 + prediction.lifetime;
      result.survival25[sample] = prediction.survival_25;
      result.maximum_orientation_gap = std::max(
          result.maximum_orientation_gap, prediction.orientation_gap);
      ++result.inferences;
    }
    result.mean_lifetime +=
        result.lifetime[sample] / denoised::kRootStrata;
    result.mean_survival25 +=
        result.survival25[sample] / denoised::kRootStrata;
  }
  return result;
}

template <typename Member>
double pairedLower95(const ActionEstimate& candidate,
                     const ActionEstimate& baseline, Member member) {
  std::array<double, denoised::kRootStrata> differences{};
  double mean = 0.0;
  for (int sample = 0; sample < denoised::kRootStrata; ++sample) {
    differences[sample] =
        (candidate.*member)[sample] - (baseline.*member)[sample];
    mean += differences[sample] / denoised::kRootStrata;
  }
  double squares = 0.0;
  for (const double value : differences) {
    squares += (value - mean) * (value - mean);
  }
  const double deviation = std::sqrt(
      squares / static_cast<double>(denoised::kRootStrata - 1));
  return mean - 1.96 * deviation / std::sqrt(denoised::kRootStrata);
}

struct AlternativeTest {
  double lifetime_lower95 = -std::numeric_limits<double>::infinity();
  double survival25_mean_advantage =
      -std::numeric_limits<double>::infinity();
  double root_q_loss = std::numeric_limits<double>::infinity();
  bool lifetime_ok = false;
  bool survival_ok = false;
  bool root_q_ok = false;
  bool orientation_ok = false;
  bool passed = false;
};

AlternativeTest testAlternative(const ActionEstimate& candidate,
                                const ActionEstimate& baseline,
                                double candidate_root_q,
                                double baseline_root_q) {
  AlternativeTest result;
  result.lifetime_lower95 = pairedLower95(
      candidate, baseline, &ActionEstimate::lifetime);
  result.survival25_mean_advantage =
      candidate.mean_survival25 - baseline.mean_survival25;
  result.root_q_loss = baseline_root_q - candidate_root_q;
  result.lifetime_ok = result.lifetime_lower95 >= kLifetimeMargin;
  result.survival_ok = result.survival25_mean_advantage >= 0.0;
  result.root_q_ok = result.root_q_loss <= kMaximumRootQLoss + 1.0e-9;
  result.orientation_ok =
      candidate.maximum_orientation_gap <= kMaximumOrientationGap;
  result.passed = result.lifetime_ok && result.survival_ok &&
                  result.root_q_ok && result.orientation_ok;
  return result;
}

struct Decision {
  int action = -1;
  int d4_action = -1;
  bool phase5 = false;
  bool danger = false;
  bool routed = false;
  bool switched = false;
  int legal_actions = 0;
  int passing_alternatives = 0;
  int lifetime_rejections = 0;
  int survival_rejections = 0;
  int root_q_rejections = 0;
  int orientation_rejections = 0;
  double selected_lifetime_lower95 = 0.0;
  double selected_survival25_advantage = 0.0;
  double selected_root_q_loss = 0.0;
  std::uint64_t model_transitions = 0;
  std::uint64_t model_inferences = 0;
  d4::SearchDecision search{};
};

Decision chooseAction(const State& source,
                      const denoised::ModelBundle& model,
                      bool switches_enabled = true) {
  Decision result;
  result.search = d4::chooseDepth4Action(source);
  result.action = result.search.action;
  result.d4_action = result.search.action;
  if (!result.search.complete ||
      result.search.completed_depth != d4::kCandidateDepth ||
      !isLegal(source.board, result.action)) {
    throw std::runtime_error("qualified fair D4 failed to complete");
  }
  result.phase5 = source.moves_remaining == kMovesPerLevel;
  result.danger = maximumHeight(source.board) >= kDangerHeight;
  result.routed = switches_enabled && result.phase5 && result.danger;
  if (!result.routed) return result;

  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  const int canonical_d4 =
      mirrored ? kBoardSize - 1 - result.d4_action : result.d4_action;
  const double baseline_root_q = result.search.root_values[result.d4_action];
  if (!std::isfinite(baseline_root_q)) {
    throw std::runtime_error("fair D4 baseline root-Q is not finite");
  }
  const ActionEstimate baseline =
      estimateCanonicalAction(canonical, canonical_d4, model);
  result.model_transitions += baseline.transitions;
  result.model_inferences += baseline.inferences;

  int selected = canonical_d4;
  double best_lower = kLifetimeMargin;
  AlternativeTest selected_test;
  for (const int canonical_action : cfpi::detail::kColumnOrder) {
    if (!isLegal(canonical.board, canonical_action)) continue;
    ++result.legal_actions;
    if (canonical_action == canonical_d4) continue;
    const int source_action = mirrored
                                  ? kBoardSize - 1 - canonical_action
                                  : canonical_action;
    const ActionEstimate candidate =
        estimateCanonicalAction(canonical, canonical_action, model);
    result.model_transitions += candidate.transitions;
    result.model_inferences += candidate.inferences;
    const AlternativeTest test = testAlternative(
        candidate, baseline, result.search.root_values[source_action],
        baseline_root_q);
    result.lifetime_rejections += !test.lifetime_ok;
    result.survival_rejections += !test.survival_ok;
    result.root_q_rejections += !test.root_q_ok;
    result.orientation_rejections += !test.orientation_ok;
    if (!test.passed) continue;
    ++result.passing_alternatives;
    if (test.lifetime_lower95 > best_lower) {
      best_lower = test.lifetime_lower95;
      selected = canonical_action;
      selected_test = test;
    }
  }
  result.action = mirrored ? kBoardSize - 1 - selected : selected;
  result.switched = result.action != result.d4_action;
  if (result.switched) {
    result.selected_lifetime_lower95 = selected_test.lifetime_lower95;
    result.selected_survival25_advantage =
        selected_test.survival25_mean_advantage;
    result.selected_root_q_loss = selected_test.root_q_loss;
  }
  if (!isLegal(source.board, result.action)) {
    throw std::runtime_error("phase-5 value veto selected illegal action");
  }
  return result;
}

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t numbered_cleared = 0;
  std::uint64_t covers_revealed = 0;
  int maximum_chain = 0;
  std::uint64_t decisions = 0;
  std::uint64_t phase5_decisions = 0;
  std::uint64_t danger_decisions = 0;
  std::uint64_t routed_decisions = 0;
  std::uint64_t switches = 0;
  std::uint64_t passing_alternatives = 0;
  std::uint64_t lifetime_rejections = 0;
  std::uint64_t survival_rejections = 0;
  std::uint64_t root_q_rejections = 0;
  std::uint64_t orientation_rejections = 0;
  double switch_lifetime_lower95_sum = 0.0;
  double switch_survival_advantage_sum = 0.0;
  double switch_root_q_loss_sum = 0.0;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::size_t peak_cache_entries = 0;
  std::uint64_t model_transitions = 0;
  std::uint64_t model_inferences = 0;
  std::uint64_t peak_rss_bytes = 0;
  double elapsed_seconds = 0.0;
};

void observeMove(const MoveResult& move, GameResult& result) {
  result.maximum_chain =
      std::max(result.maximum_chain, static_cast<int>(move.waves.size()));
  for (const Wave& wave : move.waves) {
    result.numbered_cleared += static_cast<std::uint64_t>(wave.cleared);
    result.covers_revealed += static_cast<std::uint64_t>(wave.revealed);
  }
}

void reportGame(std::string_view label, const GameResult& result) {
  const std::lock_guard<std::mutex> lock(progress_mutex);
  std::cerr << label << " seed 0x" << std::hex << result.seed << std::dec
            << ' ' << result.score << " (" << result.moves << " moves"
            << (result.censored ? ", capped" : "") << ", switches "
            << result.switches << '/' << result.routed_decisions
            << ", clears " << result.numbered_cleared << ", work "
            << result.work << ")\n";
}

GameResult runGame(std::uint32_t seed,
                   const denoised::ModelBundle* model,
                   std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    d4::SearchDecision search;
    int action = -1;
    if (model == nullptr) {
      search = d4::chooseDepth4Action(state);
      action = search.action;
    } else {
      const Decision decision = chooseAction(state, *model);
      search = decision.search;
      action = decision.action;
      result.phase5_decisions += decision.phase5;
      result.danger_decisions += decision.phase5 && decision.danger;
      result.routed_decisions += decision.routed;
      result.switches += decision.switched;
      result.passing_alternatives += decision.passing_alternatives;
      result.lifetime_rejections += decision.lifetime_rejections;
      result.survival_rejections += decision.survival_rejections;
      result.root_q_rejections += decision.root_q_rejections;
      result.orientation_rejections += decision.orientation_rejections;
      if (decision.switched) {
        result.switch_lifetime_lower95_sum +=
            decision.selected_lifetime_lower95;
        result.switch_survival_advantage_sum +=
            decision.selected_survival25_advantage;
        result.switch_root_q_loss_sum += decision.selected_root_q_loss;
      }
      result.model_transitions += decision.model_transitions;
      result.model_inferences += decision.model_inferences;
    }
    if (!search.complete || search.completed_depth != d4::kCandidateDepth ||
        !isLegal(state.board, action)) {
      throw std::runtime_error("game policy failed D4 completion or legality");
    }
    ++result.decisions;
    result.work += search.work;
    result.nodes += search.nodes;
    result.cache_hits += search.cache_hits;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, search.cache_entries);
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("headless phase-5 veto transition failed");
    }
    observeMove(move, result);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.peak_rss_bytes = d4::peakRssBytes();
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
                 const denoised::ModelBundle& model,
                 std::string_view phase) {
  const auto started = std::chrono::steady_clock::now();
  Cohort result;
  result.baseline.resize(games);
  result.candidate.resize(games);
  std::atomic<int> next_task{0};
  const int tasks = 2 * games;
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, tasks); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int task = next_task.fetch_add(1);
        if (task >= tasks) return;
        const int game = task / 2;
        const bool candidate = task % 2 != 0;
        const std::uint32_t seed =
            seed_start + static_cast<std::uint32_t>(game);
        if (candidate) {
          result.candidate[game] = runGame(
              seed, &model, std::string(phase) + "-candidate");
        } else {
          result.baseline[game] = runGame(
              seed, nullptr, std::string(phase) + "-baseline");
        }
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.wall_seconds = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started)
                            .count();
  return result;
}

void appendCohort(Cohort& target, Cohort source) {
  target.baseline.insert(target.baseline.end(),
                         std::make_move_iterator(source.baseline.begin()),
                         std::make_move_iterator(source.baseline.end()));
  target.candidate.insert(target.candidate.end(),
                          std::make_move_iterator(source.candidate.begin()),
                          std::make_move_iterator(source.candidate.end()));
  target.wall_seconds += source.wall_seconds;
}

struct Summary {
  int games = 0;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  int censored = 0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double lower_half_score = 0.0;
  double lower_half_moves = 0.0;
  std::uint64_t decisions = 0;
  std::uint64_t phase5_decisions = 0;
  std::uint64_t danger_decisions = 0;
  std::uint64_t routed_decisions = 0;
  std::uint64_t switches = 0;
  double switch_rate = 0.0;
  std::uint64_t passing_alternatives = 0;
  std::uint64_t lifetime_rejections = 0;
  std::uint64_t survival_rejections = 0;
  std::uint64_t root_q_rejections = 0;
  std::uint64_t orientation_rejections = 0;
  double mean_switch_lifetime_lower95 = 0.0;
  double mean_switch_survival_advantage = 0.0;
  double mean_switch_root_q_loss = 0.0;
  std::uint64_t work = 0;
  double work_per_move = 0.0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::size_t peak_cache_entries = 0;
  std::uint64_t model_transitions = 0;
  std::uint64_t model_inferences = 0;
  std::uint64_t model_multiply_adds = 0;
  double aggregate_game_seconds = 0.0;
  std::uint64_t peak_rss_bytes = 0;
};

double lowerHalfMean(std::vector<double> values) {
  if (values.empty()) throw std::invalid_argument("empty lower-half values");
  std::sort(values.begin(), values.end());
  const std::size_t count = (values.size() + 1) / 2;
  return std::accumulate(values.begin(), values.begin() + count, 0.0) /
         count;
}

Summary summarize(const std::vector<GameResult>& games) {
  if (games.empty()) throw std::invalid_argument("empty veto cohort");
  Summary result;
  result.games = static_cast<int>(games.size());
  std::vector<double> scores;
  std::vector<double> moves_vector;
  std::uint64_t moves = 0;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
  double switch_lifetime_sum = 0.0;
  double switch_survival_sum = 0.0;
  double switch_q_sum = 0.0;
  for (const GameResult& game : games) {
    scores.push_back(static_cast<double>(game.score));
    moves_vector.push_back(static_cast<double>(game.moves));
    result.mean_score += static_cast<double>(game.score) / games.size();
    result.mean_moves += static_cast<double>(game.moves) / games.size();
    result.censored += game.censored;
    result.decisions += game.decisions;
    result.phase5_decisions += game.phase5_decisions;
    result.danger_decisions += game.danger_decisions;
    result.routed_decisions += game.routed_decisions;
    result.switches += game.switches;
    result.passing_alternatives += game.passing_alternatives;
    result.lifetime_rejections += game.lifetime_rejections;
    result.survival_rejections += game.survival_rejections;
    result.root_q_rejections += game.root_q_rejections;
    result.orientation_rejections += game.orientation_rejections;
    switch_lifetime_sum += game.switch_lifetime_lower95_sum;
    switch_survival_sum += game.switch_survival_advantage_sum;
    switch_q_sum += game.switch_root_q_loss_sum;
    result.work += game.work;
    result.nodes += game.nodes;
    result.cache_hits += game.cache_hits;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, game.peak_cache_entries);
    result.model_transitions += game.model_transitions;
    result.model_inferences += game.model_inferences;
    result.aggregate_game_seconds += game.elapsed_seconds;
    result.peak_rss_bytes =
        std::max(result.peak_rss_bytes, game.peak_rss_bytes);
    moves += static_cast<std::uint64_t>(game.moves);
    clears += game.numbered_cleared;
    reveals += game.covers_revealed;
  }
  const double move_count =
      static_cast<double>(std::max<std::uint64_t>(1, moves));
  const double switch_count =
      static_cast<double>(std::max<std::uint64_t>(1, result.switches));
  result.clears_per_move = clears / move_count;
  result.reveals_per_move = reveals / move_count;
  result.lower_half_score = lowerHalfMean(scores);
  result.lower_half_moves = lowerHalfMean(moves_vector);
  result.switch_rate = result.switches / move_count;
  result.mean_switch_lifetime_lower95 = switch_lifetime_sum / switch_count;
  result.mean_switch_survival_advantage = switch_survival_sum / switch_count;
  result.mean_switch_root_q_loss = switch_q_sum / switch_count;
  result.work_per_move = result.work / move_count;
  result.model_multiply_adds =
      result.model_inferences * kMultiplyAddsPerInference;
  return result;
}

struct Difference {
  double mean = 0.0;
  double lower95 = 0.0;
  int wins = 0;
  int ties = 0;
  int losses = 0;
};

Difference difference(const std::vector<double>& values) {
  if (values.empty()) throw std::invalid_argument("empty paired differences");
  Difference result;
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
  result.lower95 =
      result.mean - 1.96 * deviation / std::sqrt(values.size());
  return result;
}

struct Paired {
  Difference score;
  Difference moves;
  int leave_one_out_positive_both = 0;
};

Paired paired(const Cohort& cohort) {
  if (cohort.baseline.size() != cohort.candidate.size() ||
      cohort.baseline.empty()) {
    throw std::invalid_argument("invalid paired cohort");
  }
  std::vector<double> scores;
  std::vector<double> moves;
  for (std::size_t index = 0; index < cohort.baseline.size(); ++index) {
    scores.push_back(static_cast<double>(cohort.candidate[index].score -
                                         cohort.baseline[index].score));
    moves.push_back(static_cast<double>(cohort.candidate[index].moves -
                                        cohort.baseline[index].moves));
  }
  Paired result{difference(scores), difference(moves), 0};
  if (scores.size() >= 2) {
    const double score_total =
        std::accumulate(scores.begin(), scores.end(), 0.0);
    const double move_total =
        std::accumulate(moves.begin(), moves.end(), 0.0);
    for (std::size_t index = 0; index < scores.size(); ++index) {
      result.leave_one_out_positive_both +=
          score_total - scores[index] > 0.0 &&
          move_total - moves[index] > 0.0;
    }
  }
  return result;
}

struct Gate {
  bool score_improved = false;
  bool moves_improved = false;
  bool clear_throughput_improved = false;
  bool lower_tail_retained = false;
  bool leave_one_out_ok = false;
  bool passed = false;
};

Gate evaluateGate(const Summary& baseline, const Summary& candidate,
                  const Paired& comparison, bool fitting) {
  Gate result;
  result.score_improved = candidate.mean_score > baseline.mean_score;
  result.moves_improved = candidate.mean_moves > baseline.mean_moves;
  result.clear_throughput_improved =
      candidate.clears_per_move > baseline.clears_per_move;
  result.lower_tail_retained =
      candidate.lower_half_score >=
          kLowerTailRetention * baseline.lower_half_score &&
      candidate.lower_half_moves >=
          kLowerTailRetention * baseline.lower_half_moves;
  result.leave_one_out_ok =
      !fitting || comparison.leave_one_out_positive_both >= 3;
  result.passed = result.score_improved && result.moves_improved &&
                  result.clear_throughput_improved &&
                  result.lower_tail_retained && result.leave_one_out_ok;
  return result;
}

struct Stage {
  std::uint32_t seed_start = 0;
  Cohort cohort;
  Summary baseline;
  Summary candidate;
  Paired comparison;
  Gate gate;
};

Stage makeStage(std::uint32_t seed_start, Cohort cohort, bool fitting) {
  Stage result;
  result.seed_start = seed_start;
  result.cohort = std::move(cohort);
  result.baseline = summarize(result.cohort.baseline);
  result.candidate = summarize(result.cohort.candidate);
  result.comparison = paired(result.cohort);
  result.gate = evaluateGate(result.baseline, result.candidate,
                             result.comparison, fitting);
  return result;
}

void writeGame(std::ostream& output, const GameResult& game) {
  output << "{\"seed\":" << game.seed << ",\"score\":" << game.score
         << ",\"moves\":" << game.moves
         << ",\"censored\":" << (game.censored ? "true" : "false")
         << ",\"numberedCleared\":" << game.numbered_cleared
         << ",\"coversRevealed\":" << game.covers_revealed
         << ",\"maximumChain\":" << game.maximum_chain
         << ",\"decisions\":" << game.decisions
         << ",\"phase5Decisions\":" << game.phase5_decisions
         << ",\"dangerDecisions\":" << game.danger_decisions
         << ",\"routedDecisions\":" << game.routed_decisions
         << ",\"switches\":" << game.switches
         << ",\"passingAlternatives\":" << game.passing_alternatives
         << ",\"lifetimeRejections\":" << game.lifetime_rejections
         << ",\"survivalRejections\":" << game.survival_rejections
         << ",\"rootQRejections\":" << game.root_q_rejections
         << ",\"orientationRejections\":"
         << game.orientation_rejections << ",\"work\":" << game.work
         << ",\"nodes\":" << game.nodes
         << ",\"cacheHits\":" << game.cache_hits
         << ",\"peakCacheEntries\":" << game.peak_cache_entries
         << ",\"modelTransitions\":" << game.model_transitions
         << ",\"modelInferences\":" << game.model_inferences
         << ",\"elapsedSeconds\":" << game.elapsed_seconds
         << ",\"peakRssBytes\":" << game.peak_rss_bytes << '}';
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"games\":" << summary.games
         << ",\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"censored\":" << summary.censored
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"lowerHalfScore\":" << summary.lower_half_score
         << ",\"lowerHalfMoves\":" << summary.lower_half_moves
         << ",\"decisions\":" << summary.decisions
         << ",\"phase5Decisions\":" << summary.phase5_decisions
         << ",\"dangerDecisions\":" << summary.danger_decisions
         << ",\"routedDecisions\":" << summary.routed_decisions
         << ",\"switches\":" << summary.switches
         << ",\"switchRate\":" << summary.switch_rate
         << ",\"passingAlternatives\":"
         << summary.passing_alternatives
         << ",\"lifetimeRejections\":" << summary.lifetime_rejections
         << ",\"survivalRejections\":" << summary.survival_rejections
         << ",\"rootQRejections\":" << summary.root_q_rejections
         << ",\"orientationRejections\":"
         << summary.orientation_rejections
         << ",\"meanSwitchLifetimeLower95\":"
         << summary.mean_switch_lifetime_lower95
         << ",\"meanSwitchSurvivalAdvantage\":"
         << summary.mean_switch_survival_advantage
         << ",\"meanSwitchRootQLoss\":"
         << summary.mean_switch_root_q_loss << ",\"work\":" << summary.work
         << ",\"workPerMove\":" << summary.work_per_move
         << ",\"nodes\":" << summary.nodes
         << ",\"cacheHits\":" << summary.cache_hits
         << ",\"peakCacheEntries\":" << summary.peak_cache_entries
         << ",\"modelTransitions\":" << summary.model_transitions
         << ",\"modelInferences\":" << summary.model_inferences
         << ",\"modelMultiplyAdds\":" << summary.model_multiply_adds
         << ",\"aggregateGameSeconds\":"
         << summary.aggregate_game_seconds
         << ",\"peakRssBytes\":" << summary.peak_rss_bytes << '}';
}

void writeDifference(std::ostream& output, const Difference& value) {
  output << "{\"mean\":" << value.mean << ",\"lower95\":"
         << value.lower95 << ",\"wins\":" << value.wins
         << ",\"ties\":" << value.ties << ",\"losses\":"
         << value.losses << '}';
}

void writeStage(std::ostream& output, const Stage& stage) {
  output << "{\"seedStart\":" << stage.seed_start
         << ",\"maximumMoves\":" << kMaximumMoves
         << ",\"wallSeconds\":" << stage.cohort.wall_seconds
         << ",\"baseline\":";
  writeSummary(output, stage.baseline);
  output << ",\"candidate\":";
  writeSummary(output, stage.candidate);
  output << ",\"paired\":{\"score\":";
  writeDifference(output, stage.comparison.score);
  output << ",\"moves\":";
  writeDifference(output, stage.comparison.moves);
  output << ",\"leaveOneOutPositiveBoth\":"
         << stage.comparison.leave_one_out_positive_both
         << "},\"gate\":{\"scoreImproved\":"
         << (stage.gate.score_improved ? "true" : "false")
         << ",\"movesImproved\":"
         << (stage.gate.moves_improved ? "true" : "false")
         << ",\"clearThroughputImproved\":"
         << (stage.gate.clear_throughput_improved ? "true" : "false")
         << ",\"lowerTailRetained\":"
         << (stage.gate.lower_tail_retained ? "true" : "false")
         << ",\"leaveOneOutOk\":"
         << (stage.gate.leave_one_out_ok ? "true" : "false")
         << ",\"passed\":" << (stage.gate.passed ? "true" : "false")
         << "},\"pairs\":[";
  for (std::size_t index = 0; index < stage.cohort.baseline.size(); ++index) {
    if (index != 0) output << ',';
    output << "{\"baseline\":";
    writeGame(output, stage.cohort.baseline[index]);
    output << ",\"candidate\":";
    writeGame(output, stage.cohort.candidate[index]);
    output << '}';
  }
  output << "]}";
}

void writePausedArtifact(const Options& options, const Cohort& pilot,
                         double projected_wall) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open paused artifact");
  const Stage stage = makeStage(kFittingSeedStart, pilot, false);
  output << std::setprecision(12)
         << "{\"experiment\":\"d4-phase5-denoised-value-veto\""
         << ",\"pilotPairOnly\":true,\"formalInference\":false"
         << ",\"conclusion\":\"paused-by-preregistered-runtime-gate\""
         << ",\"readGameSeeds\":[\"0x3de90000\"]"
         << ",\"untouchedRanges\":[\"0x3de90001...003 fitting remainder\",\"0x3dea0000...007 heldout\",\"0x3eb70000...007 screen\",\"0x3eb80000...00f confirmation\",\"0x7d... protected\",\"0xd7... protected\"]"
         << ",\"firstPairWallSeconds\":" << pilot.wall_seconds
         << ",\"projectionWaves\":" << kFullProtocolProjectionWaves
         << ",\"projectedFullProtocolWallSeconds\":" << projected_wall
         << ",\"limitSeconds\":" << kMaximumProjectedWallSeconds
         << ",\"pilot\":";
  writeStage(output, stage);
  output << "}\n";
}

bool selfTest(const Options& options, std::ostream& output) {
  const bool d4_test = d4::selfTest(output);
  const bool denoised_test = denoised::selfTest(output);
  const denoised::ModelBundle model = loadCheckedModel(options.model);

  State source = fair_only_horizon::fixtureState(
      fair_only_horizon::kTypeScriptFixtures[1]);
  source.moves_remaining = kMovesPerLevel;
  // Guarantee the fixed physics predicate without filling any legal column.
  for (int row = 3; row < kBoardSize; ++row) {
    source.board[indexOf(row, 0)] = kSolid;
  }
  source.board[indexOf(2, 0)] = kEmpty;
  const Decision first = chooseAction(source, model);
  const Decision repeat = chooseAction(source, model);
  State reflected = source;
  reflected.board = cfpi::detail::mirrorBoard(source.board);
  const Decision mirror = chooseAction(reflected, model);
  State metadata = source;
  metadata.score = 8'888'888;
  metadata.level = 97;
  metadata.moves_played = 777;
  const Decision metadata_result = chooseAction(metadata, model);

  State phase4 = source;
  phase4.moves_remaining = 4;
  const Decision phase4_result = chooseAction(phase4, model);
  const d4::SearchDecision phase4_d4 = d4::chooseDepth4Action(phase4);
  State safe = source;
  safe.board = initialBoard();
  safe.moves_remaining = kMovesPerLevel;
  const Decision safe_result = chooseAction(safe, model);
  const d4::SearchDecision safe_d4 = d4::chooseDepth4Action(safe);
  const Decision disabled = chooseAction(source, model, false);

  ActionEstimate baseline;
  ActionEstimate challenger;
  baseline.lifetime.fill(10.0);
  challenger.lifetime.fill(10.0 + kLifetimeMargin + 0.25);
  baseline.survival25.fill(0.5);
  challenger.survival25.fill(0.51);
  baseline.mean_lifetime = 10.0;
  challenger.mean_lifetime = 10.0 + kLifetimeMargin + 0.25;
  baseline.mean_survival25 = 0.5;
  challenger.mean_survival25 = 0.51;
  const AlternativeTest gate_pass =
      testAlternative(challenger, baseline, 3'000.0, 10'000.0);
  const AlternativeTest q_fail =
      testAlternative(challenger, baseline, 2'999.0, 10'000.0);
  ActionEstimate survival_bad = challenger;
  survival_bad.mean_survival25 = 0.49;
  const AlternativeTest survival_fail =
      testAlternative(survival_bad, baseline, 3'000.0, 10'000.0);
  ActionEstimate orientation_bad = challenger;
  orientation_bad.maximum_orientation_gap = kMaximumOrientationGap + 0.01;
  const AlternativeTest orientation_fail =
      testAlternative(orientation_bad, baseline, 3'000.0, 10'000.0);

  const bool deterministic =
      first.action == repeat.action && first.d4_action == repeat.d4_action &&
      first.search.work == repeat.search.work &&
      first.model_transitions == repeat.model_transitions;
  const bool reflection_safe =
      mirror.action == kBoardSize - 1 - first.action &&
      mirror.d4_action == kBoardSize - 1 - first.d4_action &&
      mirror.model_transitions == first.model_transitions &&
      mirror.model_inferences == first.model_inferences;
  const bool public_only = metadata_result.action == first.action &&
                           metadata_result.d4_action == first.d4_action &&
                           metadata_result.search.work == first.search.work &&
                           metadata_result.model_transitions ==
                               first.model_transitions;
  const bool routing = first.phase5 && first.danger && first.routed &&
                       !phase4_result.routed &&
                       phase4_result.model_transitions == 0 &&
                       phase4_result.action == phase4_d4.action &&
                       !safe_result.danger && !safe_result.routed &&
                       safe_result.model_transitions == 0 &&
                       safe_result.action == safe_d4.action;
  const bool zero_switch_parity = !disabled.routed && !disabled.switched &&
                                  disabled.action == disabled.d4_action &&
                                  disabled.model_transitions == 0;
  const bool gates = gate_pass.passed &&
                     std::abs(gate_pass.root_q_loss - kMaximumRootQLoss) <
                         1.0e-9 &&
                     !q_fail.passed && !q_fail.root_q_ok &&
                     !survival_fail.passed && !survival_fail.survival_ok &&
                     !orientation_fail.passed &&
                     !orientation_fail.orientation_ok;
  const bool legal = isLegal(source.board, first.action) &&
                     isLegal(source.board, first.d4_action) &&
                     isLegal(reflected.board, mirror.action);
  const bool protocol =
      kFittingSeedStart == 0x3de9'0000u &&
      kHeldoutSeedStart == 0x3dea'0000u &&
      kScreenSeedStart == 0x3eb7'0000u &&
      kConfirmationSeedStart == 0x3eb8'0000u && kMaximumMoves == 1'000 &&
      kFittingGames == 4 && kHeldoutGames == 8 && kScreenGames == 8 &&
      kConfirmationGames == 16;
  const bool passed = d4_test && denoised_test && deterministic &&
                      reflection_safe && public_only && routing &&
                      zero_switch_parity && gates && legal && protocol;
  output << std::setprecision(12)
         << "D4_PHASE5_VALUE_VETO_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"d4Dependency\":" << (d4_test ? "true" : "false")
         << ",\"denoisedDependency\":"
         << (denoised_test ? "true" : "false")
         << ",\"checkpointBytes\":" << kExpectedCheckpointBytes
         << ",\"payloadChecksum\":" << kExpectedPayloadChecksum
         << ",\"deterministic\":"
         << (deterministic ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe ? "true" : "false")
         << ",\"publicMetadataBlind\":"
         << (public_only ? "true" : "false")
         << ",\"phaseDangerRouting\":"
         << (routing ? "true" : "false")
         << ",\"zeroSwitchParity\":"
         << (zero_switch_parity ? "true" : "false")
         << ",\"gateWiring\":" << (gates ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false")
         << ",\"sourceD4Action\":" << first.d4_action
         << ",\"sourceCandidateAction\":" << first.action
         << ",\"sourceModelTransitions\":" << first.model_transitions
         << ",\"lifetimeMargin\":" << kLifetimeMargin
         << ",\"maximumRootQLoss\":" << kMaximumRootQLoss
         << "}\n";
  return passed;
}

int run(const Options& options, std::ostream& report) {
  const auto all_started = std::chrono::steady_clock::now();
  const denoised::ModelBundle model = loadCheckedModel(options.model);
  Cohort fitting = runCohort(kFittingSeedStart, 1, model, "fit-first");
  const double first_pair_wall = fitting.wall_seconds;
  const double projected_wall =
      first_pair_wall * kFullProtocolProjectionWaves;
  {
    const std::lock_guard<std::mutex> lock(progress_mutex);
    std::cerr << "first paired wall " << first_pair_wall
              << " seconds; projected complete protocol " << projected_wall
              << " seconds\n";
  }
  if (projected_wall > kMaximumProjectedWallSeconds) {
    writePausedArtifact(options, fitting, projected_wall);
    report << std::fixed << std::setprecision(6)
           << "D4_PHASE5_VALUE_VETO_PAUSED {\"firstPairWallSeconds\":"
           << first_pair_wall
           << ",\"projectedFullProtocolWallSeconds\":" << projected_wall
           << ",\"limitSeconds\":" << kMaximumProjectedWallSeconds
           << ",\"artifact\":\"" << options.output << "\"}\n";
    return 3;
  }

  appendCohort(fitting, runCohort(kFittingSeedStart + 1,
                                  kFittingGames - 1, model, "fitting"));
  Stage fitting_stage = makeStage(kFittingSeedStart, std::move(fitting), true);
  std::optional<Stage> heldout;
  std::optional<Stage> screen;
  std::optional<Stage> confirmation;
  if (fitting_stage.gate.passed) {
    heldout = makeStage(
        kHeldoutSeedStart,
        runCohort(kHeldoutSeedStart, kHeldoutGames, model, "heldout"), false);
  }
  if (heldout && heldout->gate.passed) {
    screen = makeStage(
        kScreenSeedStart,
        runCohort(kScreenSeedStart, kScreenGames, model, "screen"), false);
  }
  if (screen && screen->gate.passed) {
    confirmation = makeStage(
        kConfirmationSeedStart,
        runCohort(kConfirmationSeedStart, kConfirmationGames, model,
                  "confirmation"),
        false);
  }
  const bool qualified = confirmation && confirmation->gate.passed;
  const double total_wall = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - all_started)
                                .count();

  std::ofstream artifact(options.output);
  if (!artifact) throw std::runtime_error("could not open veto artifact");
  artifact << std::setprecision(12)
           << "{\n  \"experiment\":\"d4-phase5-denoised-value-veto\",\n"
           << "  \"preregistered\":true,\n"
           << "  \"publicStateOnly\":true,\n"
           << "  \"checkpoint\":{\"path\":\"" << options.model
           << "\",\"bytes\":" << kExpectedCheckpointBytes
           << ",\"payloadChecksum\":" << kExpectedPayloadChecksum
           << "},\n  \"policy\":{\"baseline\":\"qualified-full-width-fair-d4-s5\""
           << ",\"route\":\"movesRemaining==5 && maxHeight>=4\""
           << ",\"rootStrata\":" << denoised::kRootStrata
           << ",\"lifetimePairedLower95Margin\":" << kLifetimeMargin
           << ",\"survival25MeanAdvantageMinimum\":0"
           << ",\"maximumOrientationGap\":" << kMaximumOrientationGap
           << ",\"maximumD4RootQLoss\":" << kMaximumRootQLoss
           << ",\"maximumMoves\":" << kMaximumMoves << "},\n"
           << "  \"gate\":{\"strictScoreAndMovesAndClearThroughput\":true"
           << ",\"lowerHalfRetention\":" << kLowerTailRetention
           << ",\"fittingLeaveOneOutPositiveBothMinimum\":3},\n"
           << "  \"runtimeProjection\":{\"firstPairSeconds\":"
           << first_pair_wall
           << ",\"fullProtocolWaves\":" << kFullProtocolProjectionWaves
           << ",\"projectedSecondsFromFirstPair\":" << projected_wall
           << ",\"limitSeconds\":" << kMaximumProjectedWallSeconds
           << ",\"paused\":false},\n  \"fitting\":";
  writeStage(artifact, fitting_stage);
  artifact << ",\n  \"heldout\":";
  if (heldout) writeStage(artifact, *heldout); else artifact << "null";
  artifact << ",\n  \"screen\":";
  if (screen) writeStage(artifact, *screen); else artifact << "null";
  artifact << ",\n  \"confirmation\":";
  if (confirmation) writeStage(artifact, *confirmation);
  else artifact << "null";
  artifact << ",\n  \"qualified\":" << (qualified ? "true" : "false")
           << ",\n  \"protectedRangesRead\":false"
           << ",\n  \"totalWallSeconds\":" << total_wall
           << ",\n  \"peakRssBytes\":" << d4::peakRssBytes() << "\n}\n";
  artifact.close();

  report << std::fixed << std::setprecision(6)
         << "D4_PHASE5_VALUE_VETO_RESULT {\"fitScoreDelta\":"
         << fitting_stage.comparison.score.mean
         << ",\"fitMoveDelta\":" << fitting_stage.comparison.moves.mean
         << ",\"fitSwitches\":" << fitting_stage.candidate.switches
         << ",\"fitPassed\":"
         << (fitting_stage.gate.passed ? "true" : "false")
         << ",\"heldoutRan\":" << (heldout ? "true" : "false")
         << ",\"heldoutPassed\":"
         << (heldout && heldout->gate.passed ? "true" : "false")
         << ",\"screenRan\":" << (screen ? "true" : "false")
         << ",\"screenPassed\":"
         << (screen && screen->gate.passed ? "true" : "false")
         << ",\"confirmationRan\":"
         << (confirmation ? "true" : "false")
         << ",\"qualified\":" << (qualified ? "true" : "false")
         << ",\"totalWallSeconds\":" << total_wall
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return 0;
}

}  // namespace drop7::d4_phase5_value_veto

#ifndef DROP7_D4_PHASE5_VALUE_VETO_LIBRARY
int main(int argc, char** argv) {
  try {
    const auto options = drop7::d4_phase5_value_veto::parseOptions(
        argc, argv, 2);
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::d4_phase5_value_veto::selfTest(options, std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      return drop7::d4_phase5_value_veto::run(options, std::cout);
    }
    std::cerr << "usage: drop7_d4_phase5_value_veto --self-test | --run "
                 "[--model PATH] [--output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
#endif
