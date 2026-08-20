#define main drop7_constructive_spectrum_frozen_entrypoint
#include "constructive-spectrum.cpp"
#undef main

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
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <type_traits>
#include <utility>
#include <vector>

// Isolates only constructive rollout horizon length.  H7 delegates to the
// fixed policy above.  Longer variants reuse the exact fixed D3 shield,
// target, reward, seven stratified scenarios, one-step continuation, sampling
// domains, 2,500-unit top-two margin, and center-first tie order.
namespace drop7::constructive_horizon_scale {

namespace frozen = drop7::constructive_spectrum;
namespace fair = drop7::fair_only_horizon;
namespace detail = drop7::cfpi::detail;
using Clock = std::chrono::steady_clock;
using PublicState = frozen::PublicState;
using Decision = frozen::Decision;

constexpr std::uint32_t kFittingSeedStart = 0x3d6a'4000u;
constexpr std::uint32_t kFittingSeedEndExclusive = 0x3d6a'4020u;
constexpr int kFittingGames = 32;
constexpr std::uint32_t kScreenSeedStart = 0x3d6a'5000u;
constexpr std::uint32_t kScreenSeedEndExclusive = 0x3d6a'5040u;
constexpr int kScreenGames = 64;
constexpr int kMaximumMoves = 1'000;
constexpr int kDefaultThreads = 8;
constexpr double kWallLimitSeconds = 45.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr double kRequiredRatio = 1.10;
constexpr int kFitJointWins = 20;
constexpr int kScreenJointWins = 40;

static_assert(kLevelBonus == 17'000);
static_assert(kMovesPerLevel == 5);
static_assert(frozen::kChanceSamples == 7);
static_assert(frozen::kMinimumHorizon == 3 &&
              frozen::kMaximumHorizon == 7);
static_assert(frozen::kTacticalDepth == 3 &&
              frozen::kTacticalShortlist == 2 &&
              frozen::kTacticalNearTie == 2'500.0);
static_assert(frozen::kPolicySeed == 0x4353'5031u);
static_assert(frozen::kTerminalValue == -1.0e9);
static_assert(kFittingSeedEndExclusive - kFittingSeedStart == kFittingGames);
static_assert(kScreenSeedEndExclusive - kScreenSeedStart == kScreenGames);
static_assert(kFittingSeedEndExclusive <= kScreenSeedStart);
static_assert((kFittingSeedStart >> 16u) == 0x3d6au &&
              ((kFittingSeedEndExclusive - 1u) >> 16u) == 0x3d6au &&
              (kScreenSeedStart >> 16u) == 0x3d6au &&
              ((kScreenSeedEndExclusive - 1u) >> 16u) == 0x3d6au);
static_assert((kFittingSeedStart >> 24u) != 0x4du &&
              (kFittingSeedStart >> 24u) != 0x7du &&
              (kFittingSeedStart >> 24u) != 0xd7u &&
              (kScreenSeedStart >> 24u) != 0x4du &&
              (kScreenSeedStart >> 24u) != 0x7du &&
              (kScreenSeedStart >> 24u) != 0xd7u);

enum class Variant : std::uint8_t { kH7, kH12, kH17, kH27, kCount };

constexpr std::array<Variant, 4> kVariants{{
    Variant::kH7, Variant::kH12, Variant::kH17, Variant::kH27}};

std::string_view variantName(Variant variant) {
  switch (variant) {
    case Variant::kH7:
      return "H7";
    case Variant::kH12:
      return "H12";
    case Variant::kH17:
      return "H17";
    case Variant::kH27:
      return "H27";
    case Variant::kCount:
      break;
  }
  throw std::invalid_argument("invalid horizon variant");
}

Variant parseVariant(std::string_view name) {
  for (const Variant variant : kVariants) {
    if (variantName(variant) == name) return variant;
  }
  throw std::invalid_argument("invalid selected horizon variant");
}

int maximumHorizon(Variant variant) {
  switch (variant) {
    case Variant::kH7:
      return 7;
    case Variant::kH12:
      return 12;
    case Variant::kH17:
      return 17;
    case Variant::kH27:
      return 27;
    case Variant::kCount:
      break;
  }
  throw std::invalid_argument("invalid horizon variant");
}

int fullCycles(Variant variant) {
  switch (variant) {
    case Variant::kH7:
      return 1;
    case Variant::kH12:
      return 2;
    case Variant::kH17:
      return 3;
    case Variant::kH27:
      return 5;
    case Variant::kCount:
      break;
  }
  throw std::invalid_argument("invalid horizon variant");
}

int horizonFor(const PublicState& state, Variant variant) {
  return std::clamp(static_cast<int>(state.moves_remaining) +
                        fullCycles(variant) * kMovesPerLevel,
                    frozen::kMinimumHorizon, maximumHorizon(variant));
}

std::uint64_t peakRssBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ull;
#endif
}

void enforceRssLimit() {
  if (peakRssBytes() > kRssLimitBytes) {
    throw std::runtime_error("horizon scale exceeded 256 MiB RSS cap");
  }
}

struct Deadline {
  Clock::time_point started = Clock::now();

  double seconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }

  void check() const {
    if (seconds() > kWallLimitSeconds) {
      throw std::runtime_error("horizon scale exceeded 45 minute wall cap");
    }
  }
};

Decision chooseActionCanonical(const PublicState& source, Variant variant) {
  if (variant == Variant::kH7) return frozen::chooseActionCanonical(source);
  Decision result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  if (source.terminal) return result;
  const State root = frozen::materialize(source);
  fair::SearchContext tactical_context;
  const fair::RootEvaluation tactical =
      fair::rootDecision(root, frozen::kTacticalDepth, tactical_context);
  result.work += tactical_context.work;
  result.tactical_action = tactical.action;
  std::array<int, kBoardSize> tactical_rank{};
  tactical_rank.fill(kBoardSize);
  std::array<int, kBoardSize> ranked_columns{};
  int ranked_count = 0;
  for (const int column : frozen::kColumnOrder) {
    if (isLegal(root.board, column)) ranked_columns[ranked_count++] = column;
  }
  std::stable_sort(ranked_columns.begin(), ranked_columns.begin() + ranked_count,
                   [&](int left, int right) {
                     return tactical.values[left] > tactical.values[right];
                   });
  for (int rank = 0; rank < ranked_count; ++rank) {
    tactical_rank[ranked_columns[rank]] = rank;
  }
  result.horizon = horizonFor(source, variant);
  for (const int root_column : frozen::kColumnOrder) {
    if (!isLegal(root.board, root_column)) continue;
    if (tactical_rank[root_column] >= frozen::kTacticalShortlist) continue;
    if (tactical.values[root_column] <
        tactical.value - frozen::kTacticalNearTie) {
      continue;
    }
    ++result.shortlist;
    double root_total = 0.0;
    for (int root_sample = 0; root_sample < frozen::kChanceSamples;
         ++root_sample) {
      const frozen::SampledStep first = frozen::sampledStep(
          root, root_column, root_sample, result.horizon);
      ++result.work;
      if (!first.played || first.state.game_over) {
        root_total += frozen::kTerminalValue;
        continue;
      }
      State state = first.state;
      double trajectory = static_cast<double>(first.score_delta) +
                          5'000.0 * first.clears +
                          8'000.0 * first.reveals + 500.0 * first.waves;
      bool terminal = false;
      for (int step_index = 1; step_index < result.horizon; ++step_index) {
        const int depth_tag = result.horizon - step_index;
        const frozen::OneStepDecision continuation =
            frozen::constructiveContinuation(state, depth_tag);
        result.work += continuation.work;
        if (continuation.action < 0) {
          terminal = true;
          break;
        }
        const int sample =
            (root_sample + 2 * step_index) % frozen::kChanceSamples;
        const frozen::SampledStep next = frozen::sampledStep(
            state, continuation.action, sample, depth_tag);
        ++result.work;
        if (!next.played || next.state.game_over) {
          terminal = true;
          break;
        }
        trajectory += static_cast<double>(next.score_delta) +
                      5'000.0 * next.clears + 8'000.0 * next.reveals +
                      500.0 * next.waves;
        state = next.state;
      }
      root_total += terminal
                        ? frozen::kTerminalValue
                        : trajectory +
                              frozen::structuralValue(
                                  frozen::publicState(state));
    }
    result.values[root_column] = root_total / frozen::kChanceSamples;
    if (result.action < 0 ||
        result.values[root_column] > result.values[result.action]) {
      result.action = root_column;
    }
  }
  if (result.action < 0) result.action = centerFirstMove(root.board);
  return result;
}

Decision chooseAction(const PublicState& source, Variant variant) {
  if (variant == Variant::kH7) return frozen::chooseAction(source);
  if (source.terminal) return {};
  bool mirrored = false;
  const PublicState canonical = frozen::canonicalPublic(source, mirrored);
  Decision result = chooseActionCanonical(canonical, variant);
  if (!mirrored) return result;
  result.action = kBoardSize - 1 - result.action;
  result.tactical_action = kBoardSize - 1 - result.tactical_action;
  std::array<double, kBoardSize> values{};
  for (int column = 0; column < kBoardSize; ++column) {
    values[column] = result.values[kBoardSize - 1 - column];
  }
  result.values = values;
  return result;
}

using VariantPolicy = Decision (*)(const PublicState&, Variant);
static_assert(std::is_same_v<decltype(&chooseAction), VariantPolicy>);
static_assert(!std::is_invocable_v<VariantPolicy, const State&, Variant>);

bool allowedFittingSeed(std::uint32_t seed) {
  return seed >= kFittingSeedStart && seed < kFittingSeedEndExclusive;
}

bool allowedScreenSeed(std::uint32_t seed) {
  return seed >= kScreenSeedStart && seed < kScreenSeedEndExclusive;
}

void requireSeed(std::uint32_t seed, bool screen) {
  if (screen ? !allowedScreenSeed(seed) : !allowedFittingSeed(seed)) {
    throw std::invalid_argument(
        screen ? "seed outside exact 0x3d6a5000 screen bank"
               : "seed outside exact 0x3d6a4000 fitting bank");
  }
}

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  int clears = 0;
  int reveals = 0;
  int waves = 0;
  int maximum_chain = 0;
  bool natural_terminal = false;
  bool capped = false;
  std::uint64_t work = 0;
  std::uint64_t disc_hash = 0xcbf2'9ce4'8422'2325ull;
};

GameResult playGame(std::uint32_t seed, Variant variant,
                    const Deadline& deadline, bool screen) {
  requireSeed(seed, screen);
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    deadline.check();
    enforceRssLimit();
    if (state.next_disc != headlessDisc(seed, state.moves_played)) {
      throw std::runtime_error("headless disc stream guard failed");
    }
    result.disc_hash ^= state.next_disc;
    result.disc_hash *= 0x0000'0100'0000'01b3ull;
    const Decision decision = chooseAction(frozen::publicState(state), variant);
    if (!isLegal(state.board, decision.action)) {
      throw std::runtime_error("horizon policy selected illegal action");
    }
    result.work += decision.work;
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("headless transition failed");
    }
    result.waves += static_cast<int>(move.waves.size());
    for (const Wave& wave : move.waves) {
      result.clears += wave.cleared;
      result.reveals += wave.revealed;
      result.maximum_chain = std::max(result.maximum_chain, wave.depth);
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.natural_terminal = state.game_over;
  result.capped = !state.game_over && state.moves_played == kMaximumMoves;
  return result;
}

struct Summary {
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double bottom_quartile_score = 0.0;
  double bottom_quartile_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double waves_per_move = 0.0;
  double log_objective = 0.0;
  double geometric_objective = 0.0;
  int natural_terminals = 0;
  int capped = 0;
  int maximum_chain = 0;
  std::uint64_t work = 0;
};

Summary summarize(const std::vector<GameResult>& games) {
  if (games.empty()) throw std::invalid_argument("cannot summarize no games");
  Summary result;
  std::vector<std::int64_t> ordered_scores;
  std::vector<int> ordered_moves;
  std::int64_t scores = 0;
  std::int64_t moves = 0;
  std::int64_t clears = 0;
  std::int64_t reveals = 0;
  std::int64_t waves = 0;
  for (const GameResult& game : games) {
    scores += game.score;
    moves += game.moves;
    clears += game.clears;
    reveals += game.reveals;
    waves += game.waves;
    ordered_scores.push_back(game.score);
    ordered_moves.push_back(game.moves);
    result.natural_terminals += game.natural_terminal;
    result.capped += game.capped;
    result.maximum_chain = std::max(result.maximum_chain, game.maximum_chain);
    result.work += game.work;
  }
  std::sort(ordered_scores.begin(), ordered_scores.end());
  std::sort(ordered_moves.begin(), ordered_moves.end());
  const std::size_t quartile =
      std::max<std::size_t>(1, games.size() / 4);
  result.bottom_quartile_score = std::accumulate(
      ordered_scores.begin(), ordered_scores.begin() + quartile, 0.0) /
      quartile;
  result.bottom_quartile_moves = std::accumulate(
      ordered_moves.begin(), ordered_moves.begin() + quartile, 0.0) /
      quartile;
  result.mean_score = static_cast<double>(scores) / games.size();
  result.mean_moves = static_cast<double>(moves) / games.size();
  result.clears_per_move = static_cast<double>(clears) / moves;
  result.reveals_per_move = static_cast<double>(reveals) / moves;
  result.waves_per_move = static_cast<double>(waves) / moves;
  if (result.mean_score <= 0 || result.mean_moves <= 0 ||
      result.bottom_quartile_score <= 0 ||
      result.bottom_quartile_moves <= 0) {
    throw std::runtime_error("log objective received nonpositive statistic");
  }
  result.log_objective =
      (std::log(result.mean_score) + std::log(result.mean_moves) +
       std::log(result.bottom_quartile_score) +
       std::log(result.bottom_quartile_moves)) /
      4.0;
  result.geometric_objective = std::exp(result.log_objective);
  return result;
}

struct Paired {
  int score_wins = 0;
  int move_wins = 0;
  int joint_wins = 0;
  double mean_score_delta = 0.0;
  double mean_move_delta = 0.0;
};

Paired pair(const std::vector<GameResult>& candidate,
            const std::vector<GameResult>& baseline) {
  if (candidate.size() != baseline.size()) {
    throw std::invalid_argument("paired cohorts differ in size");
  }
  Paired result;
  for (std::size_t index = 0; index < candidate.size(); ++index) {
    if (candidate[index].seed != baseline[index].seed) {
      throw std::runtime_error("paired seed mismatch");
    }
    const bool score_win = candidate[index].score > baseline[index].score;
    const bool move_win = candidate[index].moves > baseline[index].moves;
    result.score_wins += score_win;
    result.move_wins += move_win;
    result.joint_wins += score_win && move_win;
    result.mean_score_delta += candidate[index].score - baseline[index].score;
    result.mean_move_delta += candidate[index].moves - baseline[index].moves;
  }
  result.mean_score_delta /= candidate.size();
  result.mean_move_delta /= candidate.size();
  return result;
}

std::vector<GameResult> evaluate(std::uint32_t seed_start, int games,
                                 Variant variant, int threads,
                                 const Deadline& deadline, bool screen) {
  std::vector<GameResult> result(games);
  std::atomic<int> next{0};
  std::mutex progress;
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(threads, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int index = next.fetch_add(1);
        if (index >= games) return;
        const std::uint32_t seed = seed_start + index;
        result[index] = playGame(seed, variant, deadline, screen);
        const std::lock_guard<std::mutex> lock(progress);
        std::cerr << variantName(variant) << " seed 0x" << std::hex << seed
                  << std::dec << ' ' << result[index].score << " ("
                  << result[index].moves << " moves, work "
                  << result[index].work << ")\n";
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

double projectedTournamentSeconds(double elapsed, double last_variant_seconds,
                                  int completed_variants) {
  if (completed_variants < 1 || completed_variants > 4 ||
      last_variant_seconds < 0 || elapsed < last_variant_seconds) {
    throw std::invalid_argument("invalid projection inputs");
  }
  if (completed_variants == 4) return elapsed;
  const int current_horizon =
      maximumHorizon(kVariants[completed_variants - 1]);
  double projected = elapsed;
  for (int index = completed_variants; index < 4; ++index) {
    projected += last_variant_seconds *
                 maximumHorizon(kVariants[index]) / current_horizon;
  }
  return projected;
}

struct Options {
  std::string output;
  std::string readme =
      "/tmp/drop7-constructive-horizon-scale-README.md";
  std::string qualification;
  std::string source_sha256;
  int threads = kDefaultThreads;
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string argument = argv[index];
    if (argument == "--output") {
      result.output = argv[index + 1];
    } else if (argument == "--readme") {
      result.readme = argv[index + 1];
    } else if (argument == "--qualification") {
      result.qualification = argv[index + 1];
    } else if (argument == "--source-sha256") {
      result.source_sha256 = argv[index + 1];
    } else if (argument == "--threads") {
      result.threads = std::stoi(argv[index + 1]);
      if (result.threads < 1 || result.threads > 8) {
        throw std::invalid_argument("threads must be in [1,8]");
      }
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  if (result.source_sha256.size() != 64) {
    throw std::invalid_argument("exact 64-character source SHA-256 required");
  }
  return result;
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"bottomQuartileScore\":"
         << summary.bottom_quartile_score
         << ",\"bottomQuartileMoves\":" << summary.bottom_quartile_moves
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"wavesPerMove\":" << summary.waves_per_move
         << ",\"logObjective\":" << summary.log_objective
         << ",\"geometricObjective\":" << summary.geometric_objective
         << ",\"naturalTerminals\":" << summary.natural_terminals
         << ",\"capped\":" << summary.capped
         << ",\"maximumChain\":" << summary.maximum_chain
         << ",\"work\":" << summary.work << '}';
}

void writePaired(std::ostream& output, const Paired& paired) {
  output << "{\"scoreWins\":" << paired.score_wins
         << ",\"moveWins\":" << paired.move_wins
         << ",\"jointWins\":" << paired.joint_wins
         << ",\"meanScoreDelta\":" << paired.mean_score_delta
         << ",\"meanMoveDelta\":" << paired.mean_move_delta << '}';
}

void writeGame(std::ostream& output, const GameResult& game) {
  output << "{\"seed\":\"0x" << std::hex << std::setw(8)
         << std::setfill('0') << game.seed << std::dec << std::setfill(' ')
         << "\",\"score\":" << game.score << ",\"moves\":" << game.moves
         << ",\"clears\":" << game.clears
         << ",\"reveals\":" << game.reveals << ",\"waves\":" << game.waves
         << ",\"maximumChain\":" << game.maximum_chain
         << ",\"naturalTerminal\":"
         << (game.natural_terminal ? "true" : "false")
         << ",\"capped\":" << (game.capped ? "true" : "false")
         << ",\"work\":" << game.work << ",\"discHash\":\"0x" << std::hex
         << game.disc_hash << std::dec << "\"}";
}

bool candidateGate(const Summary& candidate, const Summary& baseline,
                   const Paired& paired, int joint_wins) {
  return candidate.mean_score >= kRequiredRatio * baseline.mean_score &&
         candidate.mean_moves >= kRequiredRatio * baseline.mean_moves &&
         candidate.clears_per_move + 1.0e-12 >=
             baseline.clears_per_move &&
         candidate.reveals_per_move + 1.0e-12 >=
             baseline.reveals_per_move &&
         paired.joint_wins >= joint_wins;
}

struct VariantResult {
  Variant variant = Variant::kH7;
  std::vector<GameResult> games;
  Summary summary{};
  Paired versus_h7{};
  double seconds = 0.0;
  bool eligible = false;
};

void writeFitReadme(const Options& options,
                    const std::array<VariantResult, 4>& results,
                    std::optional<Variant> selected, bool passed,
                    double wall_seconds, double projection) {
  std::ofstream output(options.readme);
  if (!output) throw std::runtime_error("cannot write horizon README");
  output << "# Drop7 constructive horizon-scale tournament\n\n"
         << "Only rollout horizon changes. H7 is the exact frozen policy; "
            "H12, H17, and H27 retain its D3 shield, structural target, "
            "weights, rewards, seven scenarios, continuation, sampling, "
            "margin, and tie order.\n\n"
         << "- Phase: fitting\n"
         << "- Seeds: `0x3d6a4000..0x3d6a401f`\n"
         << "- Maximum moves: 1000\n"
         << "- Source SHA-256: `" << options.source_sha256 << "`\n"
         << "- Wall/projected seconds: " << wall_seconds << " / "
         << projection << "\n\n"
         << "| Variant | Mean score | Mean moves | Bottom-Q score | "
            "Bottom-Q moves | Clears | Reveals | Joint wins vs H7 | "
            "Eligible |\n"
         << "|---|---:|---:|---:|---:|---:|---:|---:|---|\n";
  for (const VariantResult& result : results) {
    output << "| " << variantName(result.variant) << " | "
           << result.summary.mean_score << " | " << result.summary.mean_moves
           << " | " << result.summary.bottom_quartile_score << " | "
           << result.summary.bottom_quartile_moves << " | "
           << result.summary.clears_per_move << " | "
           << result.summary.reveals_per_move << " | "
           << (result.variant == Variant::kH7
                   ? 0
                   : result.versus_h7.joint_wins)
           << " | " << (result.eligible ? "yes" : "no") << " |\n";
  }
  output << "\n- Selected: "
         << (selected ? std::string(variantName(*selected)) : "none") << "\n"
         << "- Passed: " << (passed ? "yes" : "no") << "\n\n"
         << "No screen or `0x4d`, `0x7d`, or `0xd7` seed is opened unless "
            "this fitting gate passes.\n";
}

int runFit(const Options& options, std::ostream& output) {
  const Deadline deadline;
  std::array<VariantResult, 4> results{};
  double projected_seconds = 0.0;
  for (int index = 0; index < 4; ++index) {
    const auto started = Clock::now();
    results[index].variant = kVariants[index];
    results[index].games =
        evaluate(kFittingSeedStart, kFittingGames, kVariants[index],
                 options.threads, deadline, false);
    results[index].seconds =
        std::chrono::duration<double>(Clock::now() - started).count();
    results[index].summary = summarize(results[index].games);
    projected_seconds = projectedTournamentSeconds(
        deadline.seconds(), results[index].seconds, index + 1);
    std::cerr << "projection after " << variantName(kVariants[index]) << ' '
              << projected_seconds << " seconds\n";
    if (projected_seconds > kWallLimitSeconds) {
      throw std::runtime_error(
          "measured fitting projection exceeded 45 minute cap");
    }
    deadline.check();
    enforceRssLimit();
  }
  const VariantResult& baseline = results[0];
  std::optional<Variant> selected;
  double selected_objective = -std::numeric_limits<double>::infinity();
  for (int index = 1; index < 4; ++index) {
    results[index].versus_h7 =
        pair(results[index].games, baseline.games);
    results[index].eligible =
        candidateGate(results[index].summary, baseline.summary,
                      results[index].versus_h7, kFitJointWins);
    if (results[index].eligible &&
        results[index].summary.log_objective > selected_objective) {
      selected = results[index].variant;
      selected_objective = results[index].summary.log_objective;
    }
  }
  const bool resource_gate = deadline.seconds() <= kWallLimitSeconds &&
                             projected_seconds <= kWallLimitSeconds &&
                             peakRssBytes() <= kRssLimitBytes;
  const bool passed = selected.has_value() && resource_gate;
  const std::string output_path =
      options.output.empty()
          ? "/tmp/drop7-constructive-horizon-scale-fit.json"
          : options.output;
  std::ofstream artifact(output_path);
  if (!artifact) throw std::runtime_error("cannot write horizon fit artifact");
  artifact << std::fixed << std::setprecision(9)
           << "{\n  \"format\":\"drop7-constructive-horizon-scale-v1\","
           << "\n  \"phase\":\"fitting\",\n  \"sourceSha256\":\""
           << options.source_sha256
           << "\",\n  \"publicOnly\":true,\n  \"causal\":true,"
           << "\n  \"isolatedVariable\":\"rolloutHorizon\","
           << "\n  \"horizons\":{\"H7\":\"min(remaining+5,7)\","
              "\"H12\":\"min(remaining+10,12)\","
              "\"H17\":\"min(remaining+15,17)\","
              "\"H27\":\"min(remaining+25,27)\"},"
           << "\n  \"seedBank\":{\"start\":\"0x3d6a4000\","
              "\"endExclusive\":\"0x3d6a4020\",\"games\":32,"
              "\"maximumMoves\":1000},"
           << "\n  \"selectionObjective\":\"equal mean of log meanScore, "
              "log meanMoves, log bottomQuartileScore, log "
              "bottomQuartileMoves\","
           << "\n  \"variants\":[";
  for (int index = 0; index < 4; ++index) {
    if (index) artifact << ',';
    const VariantResult& result = results[index];
    artifact << "{\"name\":\"" << variantName(result.variant)
             << "\",\"seconds\":" << result.seconds
             << ",\"summary\":";
    writeSummary(artifact, result.summary);
    artifact << ",\"versusH7\":";
    writePaired(artifact, result.versus_h7);
    artifact << ",\"eligible\":"
             << (result.eligible ? "true" : "false") << ",\"games\":[";
    for (std::size_t game = 0; game < result.games.size(); ++game) {
      if (game) artifact << ',';
      writeGame(artifact, result.games[game]);
    }
    artifact << "]}";
  }
  artifact << "],\n  \"gate\":{\"scoreRatio\":1.10,\"moveRatio\":1.10,"
              "\"clearNonregression\":true,"
              "\"revealNonregression\":true,\"jointWins\":20},"
           << "\n  \"selectedVariant\":"
           << (selected ? "\"" + std::string(variantName(*selected)) + "\""
                        : "null")
           << ",\n  \"resourceGate\":"
           << (resource_gate ? "true" : "false")
           << ",\n  \"projectedSeconds\":" << projected_seconds
           << ",\n  \"passed\":" << (passed ? "true" : "false")
           << ",\n  \"wallSeconds\":" << deadline.seconds()
           << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
  writeFitReadme(options, results, selected, passed, deadline.seconds(),
                 projected_seconds);
  output << std::fixed << std::setprecision(3)
         << "CONSTRUCTIVE_HORIZON_SCALE_FIT {\"H7Score\":"
         << results[0].summary.mean_score << ",\"H7Moves\":"
         << results[0].summary.mean_moves << ",\"H12Score\":"
         << results[1].summary.mean_score << ",\"H12Moves\":"
         << results[1].summary.mean_moves << ",\"H17Score\":"
         << results[2].summary.mean_score << ",\"H17Moves\":"
         << results[2].summary.mean_moves << ",\"H27Score\":"
         << results[3].summary.mean_score << ",\"H27Moves\":"
         << results[3].summary.mean_moves << ",\"selected\":"
         << (selected ? "\"" + std::string(variantName(*selected)) + "\""
                      : "null")
         << ",\"passed\":" << (passed ? "true" : "false")
         << ",\"wallSeconds\":" << deadline.seconds()
         << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"artifact\":\"" << output_path << "\"}\n";
  return passed ? EXIT_SUCCESS : 2;
}

Variant qualifiedVariant(const Options& options) {
  if (options.qualification.empty()) {
    throw std::invalid_argument("screen requires fitting qualification");
  }
  std::ifstream input(options.qualification);
  if (!input) throw std::invalid_argument("cannot open fitting qualification");
  const std::string contents((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
  if (contents.find("\"phase\":\"fitting\"") == std::string::npos ||
      contents.find("\"passed\":true") == std::string::npos ||
      contents.find("\"sourceSha256\":\"" + options.source_sha256 +
                    "\"") == std::string::npos) {
    throw std::invalid_argument("fitting qualification did not pass/match");
  }
  const std::string tag = "\"selectedVariant\":\"";
  const auto begin = contents.find(tag);
  if (begin == std::string::npos) {
    throw std::invalid_argument("qualification lacks selected variant");
  }
  const auto value_begin = begin + tag.size();
  const auto value_end = contents.find('"', value_begin);
  if (value_end == std::string::npos) {
    throw std::invalid_argument("malformed selected variant");
  }
  const Variant result =
      parseVariant(contents.substr(value_begin, value_end - value_begin));
  if (result == Variant::kH7) {
    throw std::invalid_argument("H7 cannot qualify as its own challenger");
  }
  return result;
}

void writeScreenReadme(const Options& options, Variant selected,
                       const Summary& candidate, const Summary& baseline,
                       const Paired& paired, bool passed, double wall_seconds) {
  std::ofstream output(options.readme);
  if (!output) throw std::runtime_error("cannot write horizon README");
  output << "# Drop7 constructive horizon-scale screen\n\n"
         << "The fitting-qualified horizon is compared once against exact "
            "frozen H7. Only rollout length differs.\n\n"
         << "- Selected: " << variantName(selected) << "\n"
         << "- Seeds: `0x3d6a5000..0x3d6a503f`\n"
         << "- Maximum moves: 1000\n"
         << "- Source SHA-256: `" << options.source_sha256 << "`\n"
         << "- Candidate mean score/moves: " << candidate.mean_score << " / "
         << candidate.mean_moves << "\n"
         << "- H7 mean score/moves: " << baseline.mean_score << " / "
         << baseline.mean_moves << "\n"
         << "- Candidate clear/reveal flow: " << candidate.clears_per_move
         << " / " << candidate.reveals_per_move << "\n"
         << "- H7 clear/reveal flow: " << baseline.clears_per_move << " / "
         << baseline.reveals_per_move << "\n"
         << "- Paired joint wins: " << paired.joint_wins << "\n"
         << "- Candidate natural/censored: " << candidate.natural_terminals
         << " / " << candidate.capped << "\n"
         << "- H7 natural/censored: " << baseline.natural_terminals << " / "
         << baseline.capped << "\n"
         << "- Wall seconds: " << wall_seconds << "\n"
         << "- Passed: " << (passed ? "yes" : "no") << "\n";
}

int runScreen(const Options& options, std::ostream& output) {
  const Variant selected = qualifiedVariant(options);
  const Deadline deadline;
  const auto candidate = evaluate(kScreenSeedStart, kScreenGames, selected,
                                  options.threads, deadline, true);
  const auto baseline = evaluate(kScreenSeedStart, kScreenGames, Variant::kH7,
                                 options.threads, deadline, true);
  const Summary candidate_summary = summarize(candidate);
  const Summary baseline_summary = summarize(baseline);
  const Paired paired = pair(candidate, baseline);
  const bool result_gate =
      candidateGate(candidate_summary, baseline_summary, paired,
                    kScreenJointWins);
  const bool resource_gate = deadline.seconds() <= kWallLimitSeconds &&
                             peakRssBytes() <= kRssLimitBytes;
  const bool passed = result_gate && resource_gate;
  const std::string output_path =
      options.output.empty()
          ? "/tmp/drop7-constructive-horizon-scale-screen.json"
          : options.output;
  std::ofstream artifact(output_path);
  if (!artifact) {
    throw std::runtime_error("cannot write horizon screen artifact");
  }
  artifact << std::fixed << std::setprecision(9)
           << "{\n  \"format\":\"drop7-constructive-horizon-scale-v1\","
           << "\n  \"phase\":\"screen\",\n  \"sourceSha256\":\""
           << options.source_sha256 << "\",\n  \"selectedVariant\":\""
           << variantName(selected)
           << "\",\n  \"publicOnly\":true,\n  \"causal\":true,"
           << "\n  \"seedBank\":{\"start\":\"0x3d6a5000\","
              "\"endExclusive\":\"0x3d6a5040\",\"games\":64,"
              "\"maximumMoves\":1000},"
           << "\n  \"candidate\":";
  writeSummary(artifact, candidate_summary);
  artifact << ",\n  \"H7\":";
  writeSummary(artifact, baseline_summary);
  artifact << ",\n  \"paired\":";
  writePaired(artifact, paired);
  artifact << ",\n  \"gate\":{\"scoreRatio\":1.10,\"moveRatio\":1.10,"
              "\"clearNonregression\":true,"
              "\"revealNonregression\":true,\"jointWins\":40},"
           << "\n  \"resultGate\":" << (result_gate ? "true" : "false")
           << ",\n  \"resourceGate\":"
           << (resource_gate ? "true" : "false")
           << ",\n  \"passed\":" << (passed ? "true" : "false")
           << ",\n  \"wallSeconds\":" << deadline.seconds()
           << ",\n  \"peakRssBytes\":" << peakRssBytes()
           << ",\n  \"candidateGames\":[";
  for (std::size_t index = 0; index < candidate.size(); ++index) {
    if (index) artifact << ',';
    writeGame(artifact, candidate[index]);
  }
  artifact << "],\n  \"H7Games\":[";
  for (std::size_t index = 0; index < baseline.size(); ++index) {
    if (index) artifact << ',';
    writeGame(artifact, baseline[index]);
  }
  artifact << "]\n}\n";
  writeScreenReadme(options, selected, candidate_summary, baseline_summary,
                    paired, passed, deadline.seconds());
  output << std::fixed << std::setprecision(3)
         << "CONSTRUCTIVE_HORIZON_SCALE_SCREEN {\"selected\":\""
         << variantName(selected) << "\",\"candidateScore\":"
         << candidate_summary.mean_score << ",\"candidateMoves\":"
         << candidate_summary.mean_moves << ",\"H7Score\":"
         << baseline_summary.mean_score << ",\"H7Moves\":"
         << baseline_summary.mean_moves << ",\"jointWins\":"
         << paired.joint_wins << ",\"passed\":"
         << (passed ? "true" : "false") << ",\"wallSeconds\":"
         << deadline.seconds() << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"artifact\":\"" << output_path << "\"}\n";
  return passed ? EXIT_SUCCESS : 2;
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

template <typename Function>
bool throwsInvalid(Function&& function) {
  try {
    function();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

bool selfTest(std::ostream& output) {
  expect(kLevelBonus == 17'000 && frozen::kChanceSamples == 7 &&
             frozen::kTacticalDepth == 3 &&
             frozen::kTacticalShortlist == 2 &&
             frozen::kTacticalNearTie == 2'500.0,
         "frozen controller constants changed");
  PublicState fixture;
  fixture.board.fill(kEmpty);
  fixture.board[indexOf(6, 0)] = kSolid;
  fixture.board[indexOf(5, 0)] = 6;
  fixture.board[indexOf(6, 1)] = kCracked;
  fixture.board[indexOf(6, 2)] = 5;
  fixture.board[indexOf(5, 2)] = 4;
  fixture.board[indexOf(6, 3)] = kSolid;
  fixture.board[indexOf(6, 4)] = 7;
  fixture.next_disc = 3;
  fixture.moves_remaining = 1;
  expect(horizonFor(fixture, Variant::kH7) == 6 &&
             horizonFor(fixture, Variant::kH12) == 11 &&
             horizonFor(fixture, Variant::kH17) == 16 &&
             horizonFor(fixture, Variant::kH27) == 26,
         "low-phase horizon formula failed");
  fixture.moves_remaining = 5;
  expect(horizonFor(fixture, Variant::kH7) == 7 &&
             horizonFor(fixture, Variant::kH12) == 12 &&
             horizonFor(fixture, Variant::kH17) == 17 &&
             horizonFor(fixture, Variant::kH27) == 27,
         "maximum horizon formula failed");

  const Decision frozen_h7 = frozen::chooseAction(fixture);
  const Decision scaled_h7 = chooseAction(fixture, Variant::kH7);
  const Decision generic_h7 = chooseActionCanonical(fixture, Variant::kH7);
  expect(scaled_h7 == frozen_h7 && generic_h7 ==
                                      frozen::chooseActionCanonical(fixture),
         "exact H7 action/value/work parity failed");
  const Decision h27 = chooseAction(fixture, Variant::kH27);
  const Decision h27_repeat = chooseAction(fixture, Variant::kH27);
  const Decision h27_reflected =
      chooseAction(frozen::mirror(fixture), Variant::kH27);
  expect(h27 == h27_repeat && isLegal(fixture.board, h27.action) &&
             h27.horizon == 27 && h27.shortlist >= 1 && h27.shortlist <= 2,
         "H27 determinism/legality failed");
  expect(h27_reflected.action == kBoardSize - 1 - h27.action &&
             h27_reflected.tactical_action ==
                 kBoardSize - 1 - h27.tactical_action &&
             h27_reflected.work == h27.work &&
             h27_reflected.shortlist == h27.shortlist,
         "H27 reflection failed");
  for (int column = 0; column < kBoardSize; ++column) {
    expect(h27.values[column] ==
               h27_reflected.values[kBoardSize - 1 - column],
           "H27 values failed reflection");
  }
  State metadata = frozen::materialize(fixture);
  metadata.score = 9'999'999;
  metadata.level = 777;
  metadata.moves_played = 888;
  expect(frozen::publicState(metadata) == fixture &&
             chooseAction(frozen::publicState(metadata), Variant::kH27) == h27,
         "horizon policy used hidden metadata");
  PublicState terminal = fixture;
  terminal.terminal = true;
  expect(chooseAction(terminal, Variant::kH7).action == -1 &&
             chooseAction(terminal, Variant::kH27).action == -1,
         "terminal horizon policy selected an action");
  expect(projectedTournamentSeconds(10.0, 10.0, 1) == 90.0 &&
             projectedTournamentSeconds(40.0, 10.0, 4) == 40.0 &&
             throwsInvalid([] {
               (void)projectedTournamentSeconds(1.0, 2.0, 1);
             }),
         "measured projection formula failed");
  expect(allowedFittingSeed(kFittingSeedStart) &&
             allowedFittingSeed(kFittingSeedEndExclusive - 1u) &&
             !allowedFittingSeed(kFittingSeedStart - 1u) &&
             !allowedFittingSeed(kFittingSeedEndExclusive) &&
             allowedScreenSeed(kScreenSeedStart) &&
             allowedScreenSeed(kScreenSeedEndExclusive - 1u) &&
             !allowedScreenSeed(kScreenSeedStart - 1u) &&
             !allowedScreenSeed(kScreenSeedEndExclusive) &&
             throwsInvalid([] { requireSeed(0x4d6a'4000u, false); }) &&
             throwsInvalid([] { requireSeed(0x7d6a'4000u, false); }) &&
             throwsInvalid([] { requireSeed(0xd76a'4000u, false); }) &&
             throwsInvalid([] { requireSeed(0x4d6a'5000u, true); }) &&
             throwsInvalid([] { requireSeed(0x7d6a'5000u, true); }) &&
             throwsInvalid([] { requireSeed(0xd76a'5000u, true); }),
         "horizon seed guards failed");
  enforceRssLimit();
  output << "CONSTRUCTIVE_HORIZON_SCALE_SELF_TEST {\"passed\":true,"
         << "\"publicOnly\":true,\"metadataBlind\":true,"
         << "\"deterministic\":true,\"reflection\":true,"
         << "\"legal\":true,\"exactH7Parity\":true,"
         << "\"horizons\":[7,12,17,27],\"projectionGuard\":true,"
         << "\"seedGuards\":true,\"H7Work\":" << scaled_h7.work
         << ",\"H27Work\":" << h27.work
         << ",\"peakRssBytes\":" << peakRssBytes() << "}\n";
  return true;
}

}  // namespace drop7::constructive_horizon_scale

int main(int argc, char** argv) {
  try {
    using namespace drop7::constructive_horizon_scale;
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return selfTest(std::cout) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--fit") {
      return runFit(parseOptions(argc, argv, 2), std::cout);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--screen") {
      return runScreen(parseOptions(argc, argv, 2), std::cout);
    }
    std::cerr << "usage: drop7_constructive_horizon_scale --self-test | "
                 "--fit --source-sha256 HASH [--output PATH] [--readme PATH] "
                 "[--threads N] | --screen --source-sha256 HASH "
                 "--qualification FIT_JSON [--output PATH] [--readme PATH] "
                 "[--threads N]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_constructive_horizon_scale: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
