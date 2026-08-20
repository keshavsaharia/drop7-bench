#define DROP7_SCALED_D4_DISTILL_LIBRARY
#include "../d4-distillation/scaled-d4-distill.cpp"

#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
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
#include <utility>
#include <vector>

// A ranking-only test of whether 25-move, closed-loop public D2 outcomes
// contain a learnable action-relative residual beyond exact D2's own root Q.
// Source roots come from the previously evaluated development-only
// 0x3df2/0x3df3 D4 corpus.  No
// gameplay seed or realized future is used by label generation or inference.
namespace drop7::d2_long_outcome_ranker {

namespace base = drop7::scaled_d4_distill;
namespace fair = drop7::fair_only_horizon;
using Clock = std::chrono::steady_clock;

constexpr int kRootsPerGame = 12;
constexpr int kHorizon = 25;
constexpr int kScenarios = 7;
constexpr int kEventsPerStep = 64;
constexpr int kParallelism = 4;
constexpr double kProjectionSafetyFactor = 1.5;
constexpr double kMaximumProjectedWallSeconds = 45.0 * 60.0;
constexpr std::uint32_t kTapeSeedDomain = 0x4c4f'4e47u;  // "LONG"
constexpr std::uint32_t kRevealDomain = 0x4c52'564cu;  // "LRVL"
constexpr std::uint32_t kVisibleDomain = 0x4c56'4953u;  // "LVIS"

constexpr double kMinimumTop1Improvement = 0.02;
constexpr double kMinimumTop2Improvement = 0.005;
constexpr double kMinimumPairwiseImprovement = 0.01;
constexpr double kMaximumRegretRatio = 0.90;
constexpr double kMinimumHalfPairwiseImprovement = 0.005;
constexpr double kMaximumHalfRegretRatio = 0.95;
constexpr std::uint64_t kMaximumCheckpointBytes = 32'768;
constexpr std::uint64_t kMaximumRssBytes = 160u * 1024u * 1024u;

constexpr std::uint64_t power(std::uint64_t base_value, int exponent) {
  std::uint64_t result = 1;
  for (int count = 0; count < exponent; ++count) result *= base_value;
  return result;
}

constexpr std::uint64_t kWorstD2Work =
    kBoardSize * fair::kChanceSamples +
    2u * power(kBoardSize * fair::kChanceSamples, 2);
constexpr std::size_t kWorstD2CacheEntries =
    kBoardSize * fair::kChanceSamples;
constexpr std::uint64_t kMaximumD2CallsPerRoot =
    kBoardSize * kScenarios * (kHorizon - 1);
constexpr std::uint64_t kMaximumSyntheticTransitionsPerRoot =
    kBoardSize * kScenarios * kHorizon;
constexpr int kTrainingRoots = base::kTrainingGames * kRootsPerGame;
constexpr int kHeldoutRoots = base::kHeldoutGames * kRootsPerGame;
constexpr int kTotalRoots = kTrainingRoots + kHeldoutRoots;

static_assert(kWorstD2Work == 2'485);
static_assert(kWorstD2CacheEntries == 35);
static_assert(kMaximumD2CallsPerRoot == 1'176);
static_assert(kMaximumSyntheticTransitionsPerRoot == 1'225);
static_assert(kTrainingRoots == 288 && kHeldoutRoots == 144);
static_assert(kHorizon == 25 && kScenarios == kBoardSize);
static_assert(kEventsPerStep > kCellCount);
static_assert(base::kTrainingSeedStart == 0x3df2'0000u);
static_assert(base::kHeldoutSeedStart == 0x3df3'0000u);

std::mutex progress_mutex;

std::uint64_t peakRssBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
#endif
}

struct Options {
  std::string source_labels =
      "/tmp/drop7-scaled-d4-distill-labels.jsonl";
  std::string output = "/tmp/drop7-d2-long-outcome-ranker.json";
  std::string checkpoint = "/tmp/drop7-d2-long-outcome-ranker.bin";
  std::string outcome_labels =
      "/tmp/drop7-d2-long-outcome-labels.jsonl";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string argument = argv[index];
    if (argument == "--source-labels") {
      result.source_labels = argv[index + 1];
    } else if (argument == "--output") {
      result.output = argv[index + 1];
    } else if (argument == "--checkpoint") {
      result.checkpoint = argv[index + 1];
    } else if (argument == "--outcome-labels") {
      result.outcome_labels = argv[index + 1];
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  return result;
}

struct ObservableState {
  Board board{};
  std::uint8_t next_disc = 1;
  int moves_remaining = kMovesPerLevel;
  bool game_over = false;
};

ObservableState observable(const State& source) {
  return {source.board, source.next_disc, source.moves_remaining,
          source.game_over};
}

State materialize(const ObservableState& source) {
  State result;
  result.board = source.board;
  result.next_disc = source.next_disc;
  result.moves_remaining = source.moves_remaining;
  result.game_over = source.game_over;
  result.score = 0;
  result.level = 1;
  result.moves_played = 0;
  return result;
}

ObservableState mirrored(const ObservableState& source) {
  ObservableState result = source;
  result.board = cfpi::detail::mirrorBoard(source.board);
  return result;
}

std::uint64_t mix64(std::uint64_t value) {
  value ^= value >> 30u;
  value *= 0xbf58'476d'1ce4'e5b9ull;
  value ^= value >> 27u;
  value *= 0x94d0'49bb'1331'11ebull;
  return value ^ (value >> 31u);
}

std::uint64_t publicHash(const ObservableState& source) {
  bool ignored = false;
  const State canonical =
      cfpi::detail::canonicalState(materialize(source), ignored);
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const std::uint8_t cell : canonical.board) {
    hash ^= static_cast<std::uint64_t>(cell + 1u);
    hash *= 0x0000'0100'0000'01b3ull;
  }
  hash ^= canonical.next_disc;
  hash *= 0x0000'0100'0000'01b3ull;
  hash ^= static_cast<std::uint64_t>(canonical.moves_remaining + 1);
  hash *= 0x0000'0100'0000'01b3ull;
  return mix64(hash);
}

std::uint32_t seed32(std::uint64_t value) {
  return mix32(static_cast<std::uint32_t>(value) ^
               static_cast<std::uint32_t>(value >> 32u));
}

struct RevealTape {
  std::uint32_t root_seed = 0;
  int scenario = 0;
  int step = 0;
  int event = 0;
  std::uint32_t domain = kRevealDomain;

  std::uint8_t nextDisc() {
    const int event_index = step * kEventsPerStep + event++;
    const double unit = cfpi::detail::stratifiedUnit(
        root_seed, scenario, kScenarios, domain, event_index);
    return static_cast<std::uint8_t>(
        std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
  }
};

std::uint8_t visibleDisc(std::uint32_t root_seed, int scenario, int step,
                         std::uint32_t domain = kVisibleDomain) {
  const double unit = cfpi::detail::stratifiedUnit(
      root_seed, scenario, kScenarios, domain, step);
  return static_cast<std::uint8_t>(
      std::floor(unit * static_cast<double>(kBoardSize)) + 1.0);
}

struct TapeDomains {
  std::uint32_t reveal = kRevealDomain;
  std::uint32_t visible = kVisibleDomain;
};

bool playSyntheticMove(const ObservableState& source, int action,
                       std::uint32_t root_seed, int scenario, int step,
                       MoveResult& result,
                       TapeDomains domains = TapeDomains{}) {
  if (source.game_over || !isLegal(source.board, action)) return false;
  Board board = source.board;
  if (!placeDisc(board, action, source.next_disc)) return false;
  RevealTape reveals{root_seed, scenario, step, 0, domains.reveal};
  result = MoveResult{};
  std::int64_t first_score = 0;
  cfpi::detail::resolveCascadeSampled(board, reveals, 1, first_score,
                                      result.waves);
  result.score_delta = first_score;
  result.cleared_board = isBoardEmpty(board);
  if (result.cleared_board) result.score_delta += kClearBonus;

  int moves_remaining = source.moves_remaining - 1;
  bool game_over = false;
  if (moves_remaining == 0) {
    Board raised{};
    if (!raiseCoveredRow(board, raised)) {
      game_over = true;
    } else {
      result.level_advanced = true;
      moves_remaining = kMovesPerLevel;
      result.score_delta += kLevelBonus;
      board = raised;
      std::int64_t level_score = 0;
      const int next_depth =
          result.waves.empty() ? 1 : result.waves.back().depth + 1;
      cfpi::detail::resolveCascadeSampled(board, reveals, next_depth,
                                          level_score, result.waves);
      result.score_delta += level_score;
      if (isBoardEmpty(board)) {
        result.score_delta += kClearBonus;
        result.cleared_board = true;
      }
    }
  }
  int legal_count = 0;
  legalColumns(board, legal_count);
  if (!game_over && legal_count == 0) game_over = true;

  result.state.board = board;
  result.state.next_disc =
      game_over ? source.next_disc
                : visibleDisc(root_seed, scenario, step, domains.visible);
  result.state.score = 0;
  result.state.level = 1;
  result.state.moves_remaining = moves_remaining;
  result.state.moves_played = 0;
  result.state.game_over = game_over;
  return true;
}

struct D2Metrics {
  std::uint64_t calls = 0;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::size_t peak_cache_entries = 0;
};

int d2Action(const ObservableState& source, D2Metrics& metrics) {
  bool was_mirrored = false;
  const State canonical =
      cfpi::detail::canonicalState(materialize(source), was_mirrored);
  base::fair::SearchContext context;
  const base::fair::RootEvaluation root =
      base::fair::rootDecision(canonical, 2, context);
  if (root.action < 0 || context.work > kWorstD2Work ||
      context.cache.size() > kWorstD2CacheEntries) {
    throw std::runtime_error("long-outcome D2 resource proof failed");
  }
  ++metrics.calls;
  metrics.work += context.work;
  metrics.nodes += context.nodes;
  metrics.cache_hits += context.cache_hits;
  metrics.peak_cache_entries =
      std::max(metrics.peak_cache_entries, context.cache.size());
  return was_mirrored ? kBoardSize - 1 - root.action : root.action;
}

struct ScenarioOutcome {
  double value = 0.0;
  int moves = 0;
  int clears = 0;
  bool survived_horizon = false;
};

struct ActionOutcome {
  std::array<ScenarioOutcome, kScenarios> scenarios{};
  double mean = 0.0;
};

struct OutcomeLabel {
  base::RootLabel label{};
  std::array<ActionOutcome, kBoardSize> actions{};
  std::uint64_t transitions = 0;
  D2Metrics d2{};
  double wall_seconds = 0.0;
};

OutcomeLabel evaluateRoot(const base::RootLabel& source,
                          int horizon = kHorizon) {
  if (horizon < 1 || horizon > kHorizon) {
    throw std::invalid_argument("invalid long-outcome horizon");
  }
  const auto started = Clock::now();
  bool was_mirrored = false;
  const State canonical_state =
      cfpi::detail::canonicalState(base::publicState(source), was_mirrored);
  const ObservableState root = observable(canonical_state);
  const std::uint32_t root_seed =
      seed32(publicHash(root) ^ static_cast<std::uint64_t>(kTapeSeedDomain));
  OutcomeLabel canonical;
  canonical.label = source;
  canonical.label.board = root.board;
  canonical.label.next_disc = root.next_disc;
  canonical.label.moves_remaining = root.moves_remaining;
  canonical.label.q.fill(-std::numeric_limits<double>::infinity());
  canonical.label.legal.fill(false);
  int best_action = -1;
  double best_value = -std::numeric_limits<double>::infinity();
  for (const int action : base::kActionOrder) {
    if (!isLegal(root.board, action)) continue;
    canonical.label.legal[action] = true;
    ActionOutcome& action_outcome = canonical.actions[action];
    for (int scenario = 0; scenario < kScenarios; ++scenario) {
      ObservableState state = root;
      ScenarioOutcome& outcome = action_outcome.scenarios[scenario];
      for (int step = 0; step < horizon; ++step) {
        const int selected =
            step == 0 ? action : d2Action(state, canonical.d2);
        if (!isLegal(state.board, selected)) {
          outcome.value += fair::kTerminalUtility;
          state.game_over = true;
          break;
        }
        MoveResult move;
        if (!playSyntheticMove(state, selected, root_seed, scenario, step,
                               move)) {
          outcome.value += fair::kTerminalUtility;
          state.game_over = true;
          break;
        }
        ++canonical.transitions;
        outcome.value += static_cast<double>(move.score_delta);
        ++outcome.moves;
        for (const Wave& wave : move.waves) outcome.clears += wave.cleared;
        state = observable(move.state);
        if (state.game_over) {
          outcome.value += fair::kTerminalUtility;
          break;
        }
      }
      if (!state.game_over) {
        outcome.survived_horizon = true;
        outcome.value += fair::fairLeaf(materialize(state));
      }
      action_outcome.mean += outcome.value / kScenarios;
    }
    canonical.label.q[action] = action_outcome.mean;
    if (best_action < 0 || action_outcome.mean > best_value) {
      best_action = action;
      best_value = action_outcome.mean;
    }
  }
  canonical.label.labeled_action = best_action;
  canonical.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  if (best_action < 0 ||
      canonical.transitions > kMaximumSyntheticTransitionsPerRoot ||
      canonical.d2.calls > kMaximumD2CallsPerRoot ||
      canonical.d2.work > kMaximumD2CallsPerRoot * kWorstD2Work ||
      canonical.d2.peak_cache_entries > kWorstD2CacheEntries) {
    throw std::runtime_error("long-outcome root exceeded resource bound");
  }
  // Persist the public canonical orientation used by live inference.  This
  // keeps orientation-dependent shallow chance sampling out of both splits;
  // source whole-game and move indices remain intact for grouped auditing.
  (void)was_mirrored;
  return canonical;
}

std::vector<base::RootLabel> selectEvenRoots(
    const std::vector<base::RootLabel>& source, int expected_games) {
  std::vector<std::vector<base::RootLabel>> by_game(expected_games);
  for (const base::RootLabel& label : source) {
    if (label.game < 0 || label.game >= expected_games) {
      throw std::runtime_error("source root has an unexpected game index");
    }
    by_game[label.game].push_back(label);
  }
  std::vector<base::RootLabel> selected;
  selected.reserve(static_cast<std::size_t>(expected_games) * kRootsPerGame);
  for (int game = 0; game < expected_games; ++game) {
    const auto& roots = by_game[game];
    if (roots.size() < static_cast<std::size_t>(kRootsPerGame + 1)) {
      throw std::runtime_error("source game is too short for fixed sampling");
    }
    std::size_t previous = roots.size();
    for (int slot = 0; slot < kRootsPerGame; ++slot) {
      const std::size_t index =
          (static_cast<std::size_t>(slot + 1) * roots.size()) /
          static_cast<std::size_t>(kRootsPerGame + 1);
      if (index >= roots.size() || (slot > 0 && index == previous)) {
        throw std::runtime_error("fixed root sampling produced a duplicate");
      }
      selected.push_back(roots[index]);
      previous = index;
    }
  }
  return selected;
}

std::vector<OutcomeLabel> generateLabels(
    const std::vector<base::RootLabel>& roots, std::string_view split,
    int completed_index = -1,
    const OutcomeLabel* completed_label = nullptr) {
  std::vector<OutcomeLabel> result(roots.size());
  if ((completed_index >= 0) != (completed_label != nullptr)) {
    throw std::invalid_argument("incomplete precomputed-root arguments");
  }
  if (completed_label != nullptr) {
    if (completed_index >= static_cast<int>(roots.size())) {
      throw std::invalid_argument("precomputed-root index is out of range");
    }
    result[completed_index] = *completed_label;
  }
  std::atomic<std::size_t> next{0};
  std::atomic<std::size_t> completed{completed_label == nullptr ? 0u : 1u};
  std::vector<std::future<void>> workers;
  workers.reserve(kParallelism);
  for (int worker = 0; worker < kParallelism; ++worker) {
    workers.push_back(std::async(std::launch::async, [&]() {
      while (true) {
        const std::size_t index = next.fetch_add(1);
        if (index >= roots.size()) return;
        if (static_cast<int>(index) == completed_index) continue;
        result[index] = evaluateRoot(roots[index]);
        const std::size_t count = completed.fetch_add(1) + 1;
        if (count % kRootsPerGame == 0 || count == roots.size()) {
          std::lock_guard<std::mutex> lock(progress_mutex);
          std::cerr << "long-outcome " << split << " roots " << count << '/'
                    << roots.size() << '\n';
        }
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

std::vector<base::RootLabel> outcomeRootLabels(
    const std::vector<OutcomeLabel>& outcomes) {
  std::vector<base::RootLabel> result;
  result.reserve(outcomes.size());
  for (const OutcomeLabel& outcome : outcomes) result.push_back(outcome.label);
  return result;
}

struct LabelAggregate {
  std::uint64_t roots = 0;
  std::uint64_t transitions = 0;
  std::uint64_t d2_calls = 0;
  std::uint64_t d2_work = 0;
  std::uint64_t d2_nodes = 0;
  std::uint64_t d2_cache_hits = 0;
  std::uint64_t scenarios = 0;
  std::uint64_t survived = 0;
  std::uint64_t clears = 0;
  std::size_t peak_cache_entries = 0;
  double cpu_seconds = 0.0;
  double maximum_root_seconds = 0.0;
};

LabelAggregate summarizeLabels(const std::vector<OutcomeLabel>& outcomes) {
  LabelAggregate result;
  result.roots = outcomes.size();
  for (const OutcomeLabel& outcome : outcomes) {
    result.transitions += outcome.transitions;
    result.d2_calls += outcome.d2.calls;
    result.d2_work += outcome.d2.work;
    result.d2_nodes += outcome.d2.nodes;
    result.d2_cache_hits += outcome.d2.cache_hits;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, outcome.d2.peak_cache_entries);
    result.cpu_seconds += outcome.wall_seconds;
    result.maximum_root_seconds =
        std::max(result.maximum_root_seconds, outcome.wall_seconds);
    for (int action = 0; action < kBoardSize; ++action) {
      if (!outcome.label.legal[action]) continue;
      for (const ScenarioOutcome& scenario :
           outcome.actions[action].scenarios) {
        ++result.scenarios;
        result.survived += scenario.survived_horizon;
        result.clears += static_cast<std::uint64_t>(scenario.clears);
      }
    }
  }
  return result;
}

void writeLabelAggregate(std::ostream& output,
                         const LabelAggregate& value) {
  output << std::setprecision(10) << "{\"roots\":" << value.roots
         << ",\"syntheticTransitions\":" << value.transitions
         << ",\"d2Calls\":" << value.d2_calls
         << ",\"d2Work\":" << value.d2_work
         << ",\"d2Nodes\":" << value.d2_nodes
         << ",\"d2CacheHits\":" << value.d2_cache_hits
         << ",\"peakD2CacheEntries\":" << value.peak_cache_entries
         << ",\"scenarioActions\":" << value.scenarios
         << ",\"survivedHorizon\":" << value.survived
         << ",\"numberedClears\":" << value.clears
         << ",\"summedRootSeconds\":" << value.cpu_seconds
         << ",\"maximumRootSeconds\":" << value.maximum_root_seconds << '}';
}

std::string encodedBoard(const Board& board) {
  std::string result;
  result.reserve(kCellCount);
  for (const std::uint8_t cell : board) {
    if (cell > 9) throw std::logic_error("board cell cannot be encoded");
    result.push_back(static_cast<char>('0' + cell));
  }
  return result;
}

void writeOutcomeRecord(std::ostream& output, std::string_view split,
                        const OutcomeLabel& outcome) {
  output << std::setprecision(17) << "{\"type\":\"root\",\"split\":\""
         << split << "\",\"game\":" << outcome.label.game
         << ",\"moveInSourceGame\":" << outcome.label.move_in_game
         << ",\"board\":\"" << encodedBoard(outcome.label.board)
         << "\",\"nextDisc\":" << static_cast<int>(outcome.label.next_disc)
         << ",\"movesRemaining\":" << outcome.label.moves_remaining
         << ",\"optimalAction\":" << outcome.label.labeled_action
         << ",\"rootQ\":[";
  for (int action = 0; action < kBoardSize; ++action) {
    if (action > 0) output << ',';
    if (outcome.label.legal[action]) {
      output << outcome.label.q[action];
    } else {
      output << "null";
    }
  }
  output << "],\"scenarioReturns\":[";
  for (int action = 0; action < kBoardSize; ++action) {
    if (action > 0) output << ',';
    if (!outcome.label.legal[action]) {
      output << "null";
      continue;
    }
    output << '[';
    for (int scenario = 0; scenario < kScenarios; ++scenario) {
      if (scenario > 0) output << ',';
      output << outcome.actions[action].scenarios[scenario].value;
    }
    output << ']';
  }
  output << "],\"syntheticTransitions\":" << outcome.transitions
         << ",\"d2Calls\":" << outcome.d2.calls
         << ",\"d2Work\":" << outcome.d2.work
         << ",\"wallSeconds\":" << outcome.wall_seconds << "}\n";
}

void writeOutcomeLabels(const Options& options,
                        const std::vector<OutcomeLabel>& training,
                        const std::vector<OutcomeLabel>& heldout) {
  std::ofstream output(options.outcome_labels);
  if (!output) throw std::runtime_error("could not write outcome labels");
  output << "{\"type\":\"metadata\","
            "\"format\":\"drop7-public-d2-closed-loop-outcomes-v1\","
            "\"sourceCorpusSha256\":"
            "\"e97f0a00dad76ce0e47bd60d5824e4e921e57b2cb47990b28b5bd4a562dd56bf\","
            "\"rootsPerGame\":12,\"horizon\":25,\"scenarios\":7,"
            "\"continuation\":\"full-width-public-fair-D2\","
            "\"rootOrientation\":\"public-canonical-reflection\","
            "\"trainingRoots\":288,\"heldoutRoots\":144,"
            "\"fresh3eSeedsRead\":0,\"validation7dSeedsRead\":0,"
            "\"finalD7SeedsRead\":0}\n";
  for (const OutcomeLabel& outcome : training) {
    writeOutcomeRecord(output, "fitting", outcome);
  }
  for (const OutcomeLabel& outcome : heldout) {
    writeOutcomeRecord(output, "heldout", outcome);
  }
}

base::RootLabel reflectedLabel(const base::RootLabel& source) {
  base::RootLabel result = source;
  result.board = cfpi::detail::mirrorBoard(source.board);
  for (int action = 0; action < kBoardSize; ++action) {
    const int mirror_action = kBoardSize - 1 - action;
    result.q[mirror_action] = source.q[action];
    result.legal[mirror_action] = source.legal[action];
  }
  result.labeled_action = source.labeled_action < 0
                              ? -1
                              : kBoardSize - 1 - source.labeled_action;
  return result;
}

base::RootLabel canonicalLabel(const base::RootLabel& source,
                               bool& was_mirrored) {
  const State canonical = cfpi::detail::canonicalState(
      base::publicState(source), was_mirrored);
  if (!was_mirrored) return source;
  base::RootLabel result = reflectedLabel(source);
  result.board = canonical.board;
  result.next_disc = canonical.next_disc;
  result.moves_remaining = canonical.moves_remaining;
  return result;
}

std::array<double, kBoardSize> inferenceScores(
    const base::LinearModel& model, const base::RootLabel& source) {
  bool was_mirrored = false;
  const base::RootLabel canonical = canonicalLabel(source, was_mirrored);
  const auto canonical_scores =
      base::modelScores(model, base::prepare(canonical));
  if (!was_mirrored) return canonical_scores;
  std::array<double, kBoardSize> result{};
  for (int action = 0; action < kBoardSize; ++action) {
    result[kBoardSize - 1 - action] = canonical_scores[action];
  }
  return result;
}

double reflectionGap(const base::LinearModel& model,
                     const std::vector<base::PreparedRoot>& roots) {
  double maximum_gap = 0.0;
  for (const base::PreparedRoot& root : roots) {
    const auto direct = inferenceScores(model, root.label);
    const auto mirror_scores =
        inferenceScores(model, reflectedLabel(root.label));
    for (int action = 0; action < kBoardSize; ++action) {
      if (!root.label.legal[action]) continue;
      maximum_gap =
          std::max(maximum_gap,
                   std::abs(direct[action] -
                            mirror_scores[kBoardSize - 1 - action]));
    }
  }
  return maximum_gap;
}

double sparseResidual(const base::LinearModel& model,
                      const base::PreparedRoot& root, int action) {
  return 0.5 * (base::dot(model, root.direct[action]) +
                base::dot(model, root.reflected[action]));
}

void updateAccumulator(double& accumulator, const base::LinearModel& model,
                       const base::FeatureVector& features, double scale) {
  for (const base::SparseFeature& feature : features) {
    accumulator += scale * model[feature.index] * feature.value;
  }
}

double incrementalAccumulatorGap(
    const base::LinearModel& model,
    const std::vector<base::PreparedRoot>& roots) {
  double maximum_gap = 0.0;
  for (const base::PreparedRoot& root : roots) {
    int previous = -1;
    double accumulator = 0.0;
    for (const int action : base::kActionOrder) {
      if (!root.label.legal[action]) continue;
      if (previous < 0) {
        updateAccumulator(accumulator, model, root.direct[action], 0.5);
        updateAccumulator(accumulator, model, root.reflected[action], 0.5);
      } else {
        updateAccumulator(accumulator, model, root.direct[previous], -0.5);
        updateAccumulator(accumulator, model, root.reflected[previous], -0.5);
        updateAccumulator(accumulator, model, root.direct[action], 0.5);
        updateAccumulator(accumulator, model, root.reflected[action], 0.5);
      }
      maximum_gap = std::max(
          maximum_gap,
          std::abs(accumulator - sparseResidual(model, root, action)));
      previous = action;
    }
  }
  return maximum_gap;
}

struct Throughput {
  std::uint64_t roots = 0;
  double seconds = 0.0;
  double roots_per_second = 0.0;
  double checksum = 0.0;
};

Throughput benchmarkPreparedInference(
    const base::LinearModel& model,
    const std::vector<base::PreparedRoot>& roots) {
  constexpr int kRepetitions = 500;
  const auto started = Clock::now();
  double checksum = 0.0;
  for (int repetition = 0; repetition < kRepetitions; ++repetition) {
    for (const base::PreparedRoot& root : roots) {
      const auto scores = base::modelScores(model, root);
      for (int action = 0; action < kBoardSize; ++action) {
        if (root.label.legal[action]) {
          checksum += scores[action] *
                      static_cast<double>(action + 1 + (repetition & 1));
        }
      }
    }
  }
  Throughput result;
  result.roots =
      static_cast<std::uint64_t>(roots.size()) * kRepetitions;
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  result.roots_per_second = result.roots / result.seconds;
  result.checksum = checksum;
  return result;
}

struct GateResult {
  bool overall_top1 = false;
  bool overall_top2 = false;
  bool overall_pairwise = false;
  bool overall_regret = false;
  std::array<bool, 2> half_top1{};
  std::array<bool, 2> half_pairwise{};
  std::array<bool, 2> half_regret{};
  bool passed = false;
};

GateResult rankingGate(const base::Ranking& d2_all,
                       const std::array<base::Ranking, 2>& d2_halves,
                       const base::Ranking& residual_all,
                       const std::array<base::Ranking, 2>& residual_halves) {
  GateResult result;
  result.overall_top1 =
      base::top1Rate(residual_all) >=
      base::top1Rate(d2_all) + kMinimumTop1Improvement;
  result.overall_top2 =
      base::top2Rate(residual_all) >=
      base::top2Rate(d2_all) + kMinimumTop2Improvement;
  result.overall_pairwise =
      base::pairwiseRate(residual_all) >=
      base::pairwiseRate(d2_all) + kMinimumPairwiseImprovement;
  result.overall_regret =
      base::regret(residual_all) <=
      kMaximumRegretRatio * base::regret(d2_all);
  for (int half = 0; half < 2; ++half) {
    result.half_top1[half] =
        base::top1Rate(residual_halves[half]) >=
        base::top1Rate(d2_halves[half]);
    result.half_pairwise[half] =
        base::pairwiseRate(residual_halves[half]) >=
        base::pairwiseRate(d2_halves[half]) +
            kMinimumHalfPairwiseImprovement;
    result.half_regret[half] =
        base::regret(residual_halves[half]) <=
        kMaximumHalfRegretRatio * base::regret(d2_halves[half]);
  }
  result.passed =
      result.overall_top1 && result.overall_top2 &&
      result.overall_pairwise && result.overall_regret &&
      result.half_top1[0] && result.half_top1[1] &&
      result.half_pairwise[0] && result.half_pairwise[1] &&
      result.half_regret[0] && result.half_regret[1];
  return result;
}

void writeGate(std::ostream& output, const GateResult& value) {
  output << "{\"passed\":" << (value.passed ? "true" : "false")
         << ",\"overallTop1\":"
         << (value.overall_top1 ? "true" : "false")
         << ",\"overallTop2\":"
         << (value.overall_top2 ? "true" : "false")
         << ",\"overallPairwise\":"
         << (value.overall_pairwise ? "true" : "false")
         << ",\"overallRegret\":"
         << (value.overall_regret ? "true" : "false")
         << ",\"firstHalfTop1NonRegression\":"
         << (value.half_top1[0] ? "true" : "false")
         << ",\"secondHalfTop1NonRegression\":"
         << (value.half_top1[1] ? "true" : "false")
         << ",\"firstHalfPairwise\":"
         << (value.half_pairwise[0] ? "true" : "false")
         << ",\"secondHalfPairwise\":"
         << (value.half_pairwise[1] ? "true" : "false")
         << ",\"firstHalfRegret\":"
         << (value.half_regret[0] ? "true" : "false")
         << ",\"secondHalfRegret\":"
         << (value.half_regret[1] ? "true" : "false") << '}';
}

void writePausedArtifact(const Options& options, const OutcomeLabel& pilot,
                         double projected_seconds) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not write pause artifact");
  output << std::setprecision(10)
         << "{\n  \"experiment\":\"d2-long-outcome-action-ranker\",\n"
            "  \"status\":\"paused-runtime-gate\",\n"
            "  \"reason\":\"one-root projection exceeded fixed 45-minute limit\",\n"
            "  \"pilot\":{\"fittingRootIndex\":11,\"seconds\":"
         << pilot.wall_seconds << ",\"projectedWallSeconds\":"
         << projected_seconds << ",\"limitSeconds\":"
         << kMaximumProjectedWallSeconds
         << "},\n  \"fittingRootsEvaluated\":1,\n"
            "  \"heldoutRootsEvaluated\":0,\n"
            "  \"newGameplaySeedsRead\":0,\n"
            "  \"peakRssBytes\":"
         << peakRssBytes() << "\n}\n";
}

void writeTrainingPauseArtifact(const Options& options,
                                const LabelAggregate& fitting,
                                double projected_seconds) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not write training pause");
  output << std::setprecision(10)
         << "{\n  \"experiment\":\"d2-long-outcome-action-ranker\",\n"
            "  \"status\":\"paused-before-heldout-runtime-gate\",\n"
            "  \"reason\":\"post-fitting projection exceeded fixed 45-minute limit\",\n"
            "  \"fitting\":";
  writeLabelAggregate(output, fitting);
  output << ",\n  \"projectedWallSeconds\":" << projected_seconds
         << ",\n  \"limitSeconds\":" << kMaximumProjectedWallSeconds
         << ",\n  \"heldoutRootsEvaluated\":0,\n"
            "  \"newGameplaySeedsRead\":0,\n"
            "  \"peakRssBytes\":"
         << peakRssBytes() << "\n}\n";
}

void writeArtifact(
    const Options& options, const LabelAggregate& fitting_labels,
    const LabelAggregate& heldout_labels, const base::Ranking& fitting_d2,
    const base::Ranking& fitting_residual, const base::Ranking& heldout_d2,
    const std::array<base::Ranking, 2>& heldout_d2_halves,
    const base::Ranking& heldout_residual,
    const std::array<base::Ranking, 2>& heldout_residual_halves,
    const GateResult& gate, const Throughput& throughput,
    std::uint64_t checkpoint_bytes, std::uint64_t model_fingerprint,
    double reflection_gap, double incremental_gap, double pilot_projection,
    double elapsed_seconds, bool resources_passed) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not write experiment artifact");
  output << std::setprecision(10)
         << "{\n  \"experiment\":\"d2-long-outcome-action-ranker\",\n"
            "  \"status\":\"complete\",\n"
            "  \"claimBoundary\":\"ranking-only long-outcome test; no gameplay evidence and no million-point claim\",\n"
            "  \"source\":{\"corpus\":\"frozen 0x3df2/0x3df3 public D4 roots\","
            "\"sha256\":\"e97f0a00dad76ce0e47bd60d5824e4e921e57b2cb47990b28b5bd4a562dd56bf\","
            "\"selection\":\"12 fixed evenly spaced roots per whole game\","
            "\"fittingGames\":24,\"heldoutGames\":12,"
            "\"heldoutContinuationsGeneratedAfterCheckpointFreeze\":true},\n"
            "  \"labelProtocol\":{\"horizon\":25,\"scenariosPerAction\":7,"
            "\"forcedSiblingFirstAction\":true,"
            "\"continuation\":\"fresh completed full-width public fair D2 each move\","
            "\"return\":\"accumulated canonical score plus terminal -1M or fair-leaf tail\","
            "\"commonRandomNumbers\":true,"
            "\"eventIndexedRevealTape\":true,"
            "\"separateVisibleDiscDomain\":true,"
            "\"gameplaySeedsOpened\":0,\"fresh3eSeedsRead\":0,"
            "\"validation7dSeedsRead\":0,\"finalD7SeedsRead\":0},\n"
            "  \"runtimeGate\":{\"pilotRootIndex\":11,"
            "\"pilotProjectedWallSeconds\":"
         << pilot_projection << ",\"limitSeconds\":"
         << kMaximumProjectedWallSeconds << ",\"passed\":true},\n"
            "  \"architecture\":{\"anchor\":\"exact public fair D2 root Q\","
            "\"candidate\":\"1647-weight action-relative reflection-averaged sparse residual\","
            "\"training\":\"deterministic grouped sibling ranking loss on fitting roots only\","
            "\"incrementalScoring\":\"subtract-old/add-new sparse accumulator\","
            "\"parameters\":"
         << base::kFeatureCount << ",\"checkpointBytes\":"
         << checkpoint_bytes << ",\"checkpointLimitBytes\":"
         << kMaximumCheckpointBytes << ",\"fingerprintFnv1a64\":\"0x"
         << std::hex << model_fingerprint << std::dec << "\"},\n"
            "  \"labelCost\":{\"fitting\":";
  writeLabelAggregate(output, fitting_labels);
  output << ",\"heldout\":";
  writeLabelAggregate(output, heldout_labels);
  output << "},\n  \"fittingRanking\":{";
  base::writeRanking(output, "d2", fitting_d2);
  output << ',';
  base::writeRanking(output, "residual", fitting_residual);
  output << "},\n  \"heldoutRanking\":{";
  base::writeRanking(output, "d2All", heldout_d2);
  output << ',';
  base::writeRanking(output, "d2FirstSixGames", heldout_d2_halves[0]);
  output << ',';
  base::writeRanking(output, "d2SecondSixGames", heldout_d2_halves[1]);
  output << ',';
  base::writeRanking(output, "residualAll", heldout_residual);
  output << ',';
  base::writeRanking(output, "residualFirstSixGames",
                     heldout_residual_halves[0]);
  output << ',';
  base::writeRanking(output, "residualSecondSixGames",
                     heldout_residual_halves[1]);
  output << "},\n  \"frozenGate\":";
  writeGate(output, gate);
  output << ",\n  \"implementationChecks\":{\"maximumReflectionScoreGap\":"
         << reflection_gap << ",\"maximumIncrementalAccumulatorGap\":"
         << incremental_gap << ",\"preparedSparseRootsPerSecond\":"
         << throughput.roots_per_second << ",\"benchmarkRoots\":"
         << throughput.roots << ",\"benchmarkSeconds\":"
         << throughput.seconds << ",\"benchmarkChecksum\":"
         << throughput.checksum << ",\"resourceBoundsPassed\":"
         << (resources_passed ? "true" : "false")
         << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"rssLimitBytes\":" << kMaximumRssBytes << "},\n"
            "  \"accepted\":"
         << (gate.passed && resources_passed ? "true" : "false")
         << ",\n  \"acceptedGameplayEvidence\":null,\n"
            "  \"conclusion\":\""
         << (gate.passed && resources_passed
                 ? "candidate passed the frozen disjoint long-outcome ranking gate; gameplay remains untested"
                 : "candidate rejected by the frozen disjoint long-outcome ranking/resource gate; exact D2 remains the anchor")
         << "\",\n  \"outcomeLabels\":\"" << options.outcome_labels
         << "\",\n  \"totalWallSeconds\":" << elapsed_seconds << "\n}\n";
}

constexpr int kPilotRootIndex = kRootsPerGame - 1;

int pilotOnly(const Options& options, std::ostream& output) {
  const std::vector<base::RootLabel> source =
      base::loadSplit(options.source_labels, "training");
  if (source.size() != 1'885) {
    throw std::runtime_error("unexpected frozen fitting source count");
  }
  const std::vector<base::RootLabel> roots =
      selectEvenRoots(source, base::kTrainingGames);
  const OutcomeLabel pilot = evaluateRoot(roots[kPilotRootIndex]);
  const double projection =
      pilot.wall_seconds * static_cast<double>(kTotalRoots) /
      static_cast<double>(kParallelism) * kProjectionSafetyFactor;
  output << std::fixed << std::setprecision(6)
         << "D2_LONG_OUTCOME_PILOT {\"rootIndex\":" << kPilotRootIndex
         << ",\"game\":" << roots[kPilotRootIndex].game
         << ",\"sourceMove\":" << roots[kPilotRootIndex].move_in_game
         << ",\"elapsedSeconds\":" << pilot.wall_seconds
         << ",\"projectedWallSeconds\":" << projection
         << ",\"limitSeconds\":" << kMaximumProjectedWallSeconds
         << ",\"passed\":"
         << (projection <= kMaximumProjectedWallSeconds ? "true" : "false")
         << ",\"heldoutContinuationsRead\":false}\n";
  return 0;
}

int run(const Options& options, std::ostream& output) {
  const auto started = Clock::now();
  const std::vector<base::RootLabel> fitting_source =
      base::loadSplit(options.source_labels, "training");
  if (fitting_source.size() != 1'885) {
    throw std::runtime_error("unexpected frozen fitting source count");
  }
  const std::vector<base::RootLabel> fitting_roots =
      selectEvenRoots(fitting_source, base::kTrainingGames);
  if (fitting_roots.size() != kTrainingRoots) {
    throw std::runtime_error("unexpected fitting root selection count");
  }
  const OutcomeLabel pilot = evaluateRoot(fitting_roots[kPilotRootIndex]);
  const double pilot_projection =
      pilot.wall_seconds * static_cast<double>(kTotalRoots) /
      static_cast<double>(kParallelism) * kProjectionSafetyFactor;
  output << std::fixed << std::setprecision(6)
         << "D2_LONG_OUTCOME_RUNTIME_GATE {\"pilotSeconds\":"
         << pilot.wall_seconds << ",\"projectedWallSeconds\":"
         << pilot_projection << ",\"limitSeconds\":"
         << kMaximumProjectedWallSeconds << ",\"passed\":"
         << (pilot_projection <= kMaximumProjectedWallSeconds ? "true"
                                                               : "false")
         << "}\n";
  if (pilot_projection > kMaximumProjectedWallSeconds) {
    writePausedArtifact(options, pilot, pilot_projection);
    return 0;
  }

  const std::vector<OutcomeLabel> fitting_outcomes = generateLabels(
      fitting_roots, "fitting", kPilotRootIndex, &pilot);
  const LabelAggregate fitting_cost = summarizeLabels(fitting_outcomes);
  const std::vector<base::RootLabel> fitting_labels =
      outcomeRootLabels(fitting_outcomes);
  const std::vector<base::PreparedRoot> prepared_fitting =
      base::prepareAll(fitting_labels);
  const base::LinearModel trained =
      base::trainLinear(prepared_fitting, 0.03);
  base::writeCheckpoint(options.checkpoint, trained);
  const base::LinearModel frozen = base::readCheckpoint(options.checkpoint);
  if (base::fingerprint(trained) != base::fingerprint(frozen)) {
    throw std::runtime_error("frozen checkpoint round trip failed");
  }

  const double elapsed_before_heldout =
      std::chrono::duration<double>(Clock::now() - started).count();
  const double heldout_projection =
      fitting_cost.cpu_seconds / static_cast<double>(kTrainingRoots) *
      static_cast<double>(kHeldoutRoots) / static_cast<double>(kParallelism) *
      kProjectionSafetyFactor;
  if (elapsed_before_heldout + heldout_projection >
      kMaximumProjectedWallSeconds) {
    writeTrainingPauseArtifact(options, fitting_cost,
                               elapsed_before_heldout + heldout_projection);
    output << "D2_LONG_OUTCOME_RESULT {\"status\":\"paused-before-heldout\","
              "\"heldoutContinuationsRead\":false}\n";
    return 0;
  }

  // Do not read the heldout source or its continuations until the
  // architecture and checkpoint above are locked.
  const std::vector<base::RootLabel> heldout_source =
      base::loadSplit(options.source_labels, "heldout");
  if (heldout_source.size() != 926) {
    throw std::runtime_error("unexpected frozen heldout source count");
  }
  const std::vector<base::RootLabel> heldout_roots =
      selectEvenRoots(heldout_source, base::kHeldoutGames);
  if (heldout_roots.size() != kHeldoutRoots) {
    throw std::runtime_error("unexpected heldout root selection count");
  }
  const std::vector<OutcomeLabel> heldout_outcomes =
      generateLabels(heldout_roots, "heldout");
  const LabelAggregate heldout_cost = summarizeLabels(heldout_outcomes);
  writeOutcomeLabels(options, fitting_outcomes, heldout_outcomes);

  const std::vector<base::RootLabel> heldout_labels =
      outcomeRootLabels(heldout_outcomes);
  const std::vector<base::PreparedRoot> prepared_heldout =
      base::prepareAll(heldout_labels);
  const base::Ranking fitting_d2 = base::evaluateRange(
      nullptr, prepared_fitting, 0, base::kTrainingGames);
  const base::Ranking fitting_residual = base::evaluateRange(
      &frozen, prepared_fitting, 0, base::kTrainingGames);
  const base::Ranking heldout_d2 =
      base::evaluateRange(nullptr, prepared_heldout, 0, base::kHeldoutGames);
  const std::array<base::Ranking, 2> heldout_d2_halves{{
      base::evaluateRange(nullptr, prepared_heldout, 0,
                          base::kHeldoutGames / 2),
      base::evaluateRange(nullptr, prepared_heldout,
                          base::kHeldoutGames / 2, base::kHeldoutGames),
  }};
  const base::Ranking heldout_residual =
      base::evaluateRange(&frozen, prepared_heldout, 0, base::kHeldoutGames);
  const std::array<base::Ranking, 2> heldout_residual_halves{{
      base::evaluateRange(&frozen, prepared_heldout, 0,
                          base::kHeldoutGames / 2),
      base::evaluateRange(&frozen, prepared_heldout,
                          base::kHeldoutGames / 2, base::kHeldoutGames),
  }};
  const GateResult gate = rankingGate(
      heldout_d2, heldout_d2_halves, heldout_residual,
      heldout_residual_halves);
  const double reflection_gap = reflectionGap(frozen, prepared_heldout);
  const double incremental_gap =
      incrementalAccumulatorGap(frozen, prepared_heldout);
  const Throughput throughput =
      benchmarkPreparedInference(frozen, prepared_heldout);
  const std::uint64_t checkpoint_bytes =
      base::fileBytes(options.checkpoint);
  const bool label_resources =
      fitting_cost.peak_cache_entries <= kWorstD2CacheEntries &&
      heldout_cost.peak_cache_entries <= kWorstD2CacheEntries &&
      fitting_cost.d2_work <= fitting_cost.d2_calls * kWorstD2Work &&
      heldout_cost.d2_work <= heldout_cost.d2_calls * kWorstD2Work;
  const bool resources_passed =
      label_resources && checkpoint_bytes <= kMaximumCheckpointBytes &&
      peakRssBytes() <= kMaximumRssBytes && reflection_gap <= 1.0e-9 &&
      incremental_gap <= 1.0e-9;
  const double elapsed_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  writeArtifact(options, fitting_cost, heldout_cost, fitting_d2,
                fitting_residual, heldout_d2, heldout_d2_halves,
                heldout_residual, heldout_residual_halves, gate, throughput,
                checkpoint_bytes, base::fingerprint(frozen), reflection_gap,
                incremental_gap, pilot_projection, elapsed_seconds,
                resources_passed);
  output << std::fixed << std::setprecision(6)
         << "D2_LONG_OUTCOME_RESULT {\"status\":\"complete\","
            "\"d2Top1\":"
         << base::top1Rate(heldout_d2) << ",\"residualTop1\":"
         << base::top1Rate(heldout_residual) << ",\"d2Pairwise\":"
         << base::pairwiseRate(heldout_d2) << ",\"residualPairwise\":"
         << base::pairwiseRate(heldout_residual) << ",\"d2Regret\":"
         << base::regret(heldout_d2) << ",\"residualRegret\":"
         << base::regret(heldout_residual) << ",\"gatePassed\":"
         << (gate.passed ? "true" : "false")
         << ",\"resourcesPassed\":"
         << (resources_passed ? "true" : "false")
         << ",\"gameplayRan\":false,\"artifact\":\"" << options.output
         << "\"}\n";
  return 0;
}

bool sameScenario(const ScenarioOutcome& first,
                  const ScenarioOutcome& second) {
  return first.value == second.value && first.moves == second.moves &&
         first.clears == second.clears &&
         first.survived_horizon == second.survived_horizon;
}

bool sameOutcome(const OutcomeLabel& first, const OutcomeLabel& second) {
  if (first.label.board != second.label.board ||
      first.label.next_disc != second.label.next_disc ||
      first.label.moves_remaining != second.label.moves_remaining ||
      first.label.labeled_action != second.label.labeled_action ||
      first.label.q != second.label.q ||
      first.label.legal != second.label.legal ||
      first.transitions != second.transitions ||
      first.d2.calls != second.d2.calls ||
      first.d2.work != second.d2.work ||
      first.d2.nodes != second.d2.nodes ||
      first.d2.cache_hits != second.d2.cache_hits ||
      first.d2.peak_cache_entries != second.d2.peak_cache_entries) {
    return false;
  }
  for (int action = 0; action < kBoardSize; ++action) {
    if (first.actions[action].mean != second.actions[action].mean) return false;
    for (int scenario = 0; scenario < kScenarios; ++scenario) {
      if (!sameScenario(first.actions[action].scenarios[scenario],
                        second.actions[action].scenarios[scenario])) {
        return false;
      }
    }
  }
  return true;
}

base::RootLabel rootLabel(const State& state) {
  base::RootLabel result;
  result.board = state.board;
  result.next_disc = state.next_disc;
  result.moves_remaining = state.moves_remaining;
  result.q.fill(-std::numeric_limits<double>::infinity());
  for (const int action : base::kActionOrder) {
    if (!isLegal(state.board, action)) continue;
    result.legal[action] = true;
    result.q[action] = 0.0;
    if (result.labeled_action < 0) result.labeled_action = action;
  }
  result.game = 0;
  result.move_in_game = 0;
  return result;
}

bool selfTest(const Options& options, std::ostream& output) {
  const bool inherited = base::fair::selfTest(output);
  const State fixture = base::fair::frozen::fixtureState(
      base::fair::frozen::kTypeScriptFixtures[1]);
  const base::RootLabel source = rootLabel(fixture);
  const OutcomeLabel first = evaluateRoot(source, 3);
  const OutcomeLabel repeat = evaluateRoot(source, 3);
  const OutcomeLabel mirror = evaluateRoot(reflectedLabel(source), 3);
  const bool deterministic = sameOutcome(first, repeat);
  const bool reflection = sameOutcome(first, mirror);

  std::array<int, kBoardSize> visible_counts{};
  std::array<int, kBoardSize> reveal_counts{};
  constexpr std::uint32_t tape_seed = 0x1234'5678u;
  for (int scenario = 0; scenario < kScenarios; ++scenario) {
    ++visible_counts[visibleDisc(tape_seed, scenario, 0) - 1];
    RevealTape tape{tape_seed, scenario, 0, 0, kRevealDomain};
    ++reveal_counts[tape.nextDisc() - 1];
  }
  bool exact_stratification = true;
  for (int value = 0; value < kBoardSize; ++value) {
    exact_stratification = exact_stratification &&
                           visible_counts[value] == 1 &&
                           reveal_counts[value] == 1;
  }
  RevealTape first_tape{tape_seed, 2, 3, 0, kRevealDomain};
  RevealTape second_tape{tape_seed, 2, 3, 0, kRevealDomain};
  bool deterministic_tape = true;
  for (int event = 0; event < 16; ++event) {
    deterministic_tape =
        deterministic_tape && first_tape.nextDisc() == second_tape.nextDisc();
  }

  ObservableState reveal_fixture;
  reveal_fixture.board.fill(kEmpty);
  reveal_fixture.board[indexOf(6, 1)] = kCracked;
  reveal_fixture.next_disc = 1;
  reveal_fixture.moves_remaining = 4;
  MoveResult standard;
  const bool standard_ok = playSyntheticMove(
      reveal_fixture, 0, tape_seed, 0, 0, standard);
  bool visible_isolated = false;
  bool reveal_isolated = false;
  for (std::uint32_t salt = 1; salt < 256; ++salt) {
    MoveResult changed_visible;
    if (!playSyntheticMove(
            reveal_fixture, 0, tape_seed, 0, 0, changed_visible,
            {kRevealDomain, kVisibleDomain ^ salt})) {
      continue;
    }
    if (changed_visible.state.next_disc != standard.state.next_disc &&
        changed_visible.state.board == standard.state.board &&
        changed_visible.score_delta == standard.score_delta) {
      visible_isolated = true;
    }
    MoveResult changed_reveal;
    if (!playSyntheticMove(
            reveal_fixture, 0, tape_seed, 0, 0, changed_reveal,
            {kRevealDomain ^ salt, kVisibleDomain})) {
      continue;
    }
    if (changed_reveal.state.board != standard.state.board &&
        changed_reveal.state.next_disc == standard.state.next_disc) {
      reveal_isolated = true;
    }
  }
  const bool domains_separate =
      standard_ok && visible_isolated && reveal_isolated;

  ObservableState terminal_fixture;
  terminal_fixture.board.fill(kSolid);
  terminal_fixture.board[indexOf(0, 3)] = kEmpty;
  terminal_fixture.next_disc = 2;
  terminal_fixture.moves_remaining = 1;
  const OutcomeLabel terminal =
      evaluateRoot(rootLabel(materialize(terminal_fixture)));
  bool terminal_semantics =
      terminal.transitions == kScenarios && terminal.d2.calls == 0 &&
      terminal.label.labeled_action == 3 &&
      terminal.label.q[3] == fair::kTerminalUtility;
  for (const ScenarioOutcome& scenario : terminal.actions[3].scenarios) {
    terminal_semantics = terminal_semantics && !scenario.survived_horizon &&
                         scenario.value == fair::kTerminalUtility;
  }

  const base::PreparedRoot prepared = base::prepare(first.label);
  const std::vector<base::PreparedRoot> tiny(32, prepared);
  const base::LinearModel trained = base::trainLinear(tiny, 0.03);
  const base::LinearModel retrained = base::trainLinear(tiny, 0.03);
  const bool deterministic_training =
      base::fingerprint(trained) == base::fingerprint(retrained);
  base::writeCheckpoint(options.checkpoint, trained);
  const base::LinearModel restored = base::readCheckpoint(options.checkpoint);
  const bool checkpoint =
      base::fingerprint(trained) == base::fingerprint(restored) &&
      base::fileBytes(options.checkpoint) <= kMaximumCheckpointBytes;
  const std::vector<base::PreparedRoot> singleton{prepared};
  const double model_reflection_gap = reflectionGap(restored, singleton);
  const double accumulator_gap =
      incrementalAccumulatorGap(restored, singleton);

  base::RootLabel metadata_label = first.label;
  metadata_label.game = 999;
  metadata_label.move_in_game = -777;
  const auto ordinary_scores = base::modelScores(restored, prepared);
  const auto metadata_scores =
      base::modelScores(restored, base::prepare(metadata_label));
  const bool metadata_blind = ordinary_scores == metadata_scores;

  base::LinearModel zero{};
  const auto zero_scores = base::modelScores(zero, prepared);
  int zero_action = -1;
  for (const int action : base::kActionOrder) {
    if (!prepared.label.legal[action]) continue;
    if (zero_action < 0 || zero_scores[action] > zero_scores[zero_action]) {
      zero_action = action;
    }
  }
  const bool exact_d2_anchor =
      zero_action == base::d2Action(base::publicState(first.label));
  const bool legal = first.label.labeled_action >= 0 &&
                     isLegal(first.label.board, first.label.labeled_action) &&
                     zero_action >= 0 &&
                     isLegal(first.label.board, zero_action);
  const bool resources =
      first.transitions <= kMaximumSyntheticTransitionsPerRoot &&
      first.d2.calls <= kMaximumD2CallsPerRoot &&
      first.d2.work <= first.d2.calls * kWorstD2Work &&
      first.d2.peak_cache_entries <= kWorstD2CacheEntries &&
      sizeof(base::LinearModel) ==
          static_cast<std::size_t>(base::kFeatureCount) * sizeof(double) &&
      base::fileBytes(options.checkpoint) <= kMaximumCheckpointBytes;
  const bool protocol =
      kRootsPerGame == 12 && kHorizon == 25 && kScenarios == 7 &&
      kTrainingRoots == 288 && kHeldoutRoots == 144 &&
      kPilotRootIndex == 11 && kRevealDomain != kVisibleDomain &&
      base::kTrainingSeedStart == 0x3df2'0000u &&
      base::kHeldoutSeedStart == 0x3df3'0000u;
  const bool passed =
      inherited && deterministic && reflection && exact_stratification &&
      deterministic_tape && domains_separate && terminal_semantics &&
      deterministic_training && checkpoint && model_reflection_gap <= 1.0e-9 &&
      accumulator_gap <= 1.0e-9 && metadata_blind && exact_d2_anchor && legal &&
      resources && protocol;
  output << std::setprecision(12)
         << "D2_LONG_OUTCOME_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"inheritedD4\":" << (inherited ? "true" : "false")
         << ",\"deterministicOutcome\":"
         << (deterministic ? "true" : "false")
         << ",\"reflection\":" << (reflection ? "true" : "false")
         << ",\"exactStratification\":"
         << (exact_stratification ? "true" : "false")
         << ",\"separateTapeDomains\":"
         << (domains_separate ? "true" : "false")
         << ",\"terminalSemantics\":"
         << (terminal_semantics ? "true" : "false")
         << ",\"deterministicTraining\":"
         << (deterministic_training ? "true" : "false")
         << ",\"checkpoint\":" << (checkpoint ? "true" : "false")
         << ",\"modelReflectionGap\":" << model_reflection_gap
         << ",\"incrementalAccumulatorGap\":" << accumulator_gap
         << ",\"metadataBlind\":"
         << (metadata_blind ? "true" : "false")
         << ",\"exactD2Anchor\":"
         << (exact_d2_anchor ? "true" : "false")
         << ",\"resources\":" << (resources ? "true" : "false")
         << ",\"protocol\":" << (protocol ? "true" : "false") << "}\n";
  return passed;
}

}  // namespace drop7::d2_long_outcome_ranker

#ifndef DROP7_D2_LONG_OUTCOME_RANKER_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      const auto options = drop7::d2_long_outcome_ranker::parseOptions(
          argc, argv, 2);
      return drop7::d2_long_outcome_ranker::selfTest(options, std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--pilot-only") {
      const auto options = drop7::d2_long_outcome_ranker::parseOptions(
          argc, argv, 2);
      return drop7::d2_long_outcome_ranker::pilotOnly(options, std::cout);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options = drop7::d2_long_outcome_ranker::parseOptions(
          argc, argv, 2);
      return drop7::d2_long_outcome_ranker::run(options, std::cout);
    }
    std::cerr << "usage: drop7_d2_long_outcome_ranker "
                 "--self-test | --pilot-only | --run "
                 "[--source-labels PATH --output PATH --checkpoint PATH "
                 "--outcome-labels PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_d2_long_outcome_ranker: " << error.what() << '\n';
    return 1;
  }
}
#endif
