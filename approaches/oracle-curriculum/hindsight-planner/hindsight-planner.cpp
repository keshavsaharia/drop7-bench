// Performs bounded hindsight optimization from public root states. Each root
// action is evaluated on the same synthetic future tapes. The planner may optimize
// later actions with knowledge of one synthetic tape, but it never receives the
// real game seed or tape; only the root action averaged across tapes is played.
#define main drop7_oracle_topology_embedded_main
#include "../topology/oracle-topology-audit.cpp"
#undef main

#include <algorithm>
#include <array>
#include <atomic>
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
#include <unordered_map>
#include <vector>

namespace drop7::hindsight {

namespace oracle = drop7::oracle_topology;

constexpr std::uint32_t kScreenSeedStart = 0x3e8b'0000u;
constexpr std::uint32_t kConfirmationSeedStart = 0x3e8c'0000u;
constexpr int kScreenGames = 4;
constexpr int kConfirmationGames = 8;
constexpr int kMaximumMoves = 500;
constexpr int kDepth = 8;
constexpr int kBeamWidth = 64;
constexpr int kScenarios = 7;
constexpr double kLowerQuartileWeight = 0.20;
constexpr int kParallelism = 4;
constexpr std::uint32_t kScenarioDomain = 0x4849'4e44u;  // "HIND"
constexpr std::uint32_t kScenarioStride = 0x9e37'79b9u;
constexpr std::uint32_t kAttemptStride = 0x85eb'ca6bu;
constexpr std::uint64_t kMaximumGeneratedPerDecision =
    static_cast<std::uint64_t>(kBoardSize) * kScenarios *
    (1u + static_cast<std::uint64_t>(kDepth - 1) * kBeamWidth * kBoardSize);

static_assert(kLevelBonus == 7'000);
static_assert(kScenarios % kBoardSize == 0);

std::mutex progress_mutex;

struct PlannerMetrics {
  std::uint64_t generated = 0;
  std::uint64_t deduplicated = 0;
  std::size_t peak_candidates = 0;
  std::array<int, kBoardSize> first_next_disc_counts{};
};

struct ActionValue {
  int column = -1;
  double utility = -std::numeric_limits<double>::infinity();
  double mean = -std::numeric_limits<double>::infinity();
  double lower_quartile = -std::numeric_limits<double>::infinity();
  std::array<double, kScenarios> scenarios{};
};

struct Decision {
  int column = -1;
  std::array<ActionValue, kBoardSize> actions{};
  int action_count = 0;
  PlannerMetrics metrics{};
};

std::uint32_t publicHash(const State& canonical) {
  std::uint32_t hash = 0x811c'9dc5u;
  for (const std::uint8_t cell : canonical.board) {
    hash ^= static_cast<std::uint32_t>(cell + 1u);
    hash *= 0x0100'0193u;
  }
  hash ^= static_cast<std::uint32_t>(canonical.next_disc) * 0x9e37'79b9u;
  hash ^= static_cast<std::uint32_t>(canonical.moves_remaining) *
          0x85eb'ca6bu;
  return mix32(hash ^ kScenarioDomain);
}

State normalizedPublicState(const State& input, bool& mirrored) {
  State result = cfpi::detail::canonicalState(input, mirrored);
  // These fields do not change future mechanics. Removing them is also a hard
  // guard against accidentally constructing a synthetic tape from history.
  result.score = 0;
  result.level = 1;
  result.moves_played = 0;
  return result;
}

std::uint32_t scenarioSeed(std::uint32_t public_hash, int scenario) {
  const int target = scenario % kBoardSize + 1;
  const std::uint32_t batch = static_cast<std::uint32_t>(scenario / kBoardSize);
  for (std::uint32_t attempt = 0; attempt < 512; ++attempt) {
    const std::uint32_t candidate = mix32(
        public_hash ^ kScenarioDomain ^ ((batch + 1u) * kScenarioStride) ^
        ((attempt + 1u) * kAttemptStride));
    // The root disc is already public. This is the first unknown visible disc
    // after the root transition because normalized roots start at move zero.
    if (headlessDisc(candidate, 1) == target) return candidate;
  }
  throw std::runtime_error("could not construct stratified synthetic tape");
}

double bestSyntheticContinuation(State root, int first_column,
                                 std::uint32_t tape_seed,
                                 PlannerMetrics& metrics) {
  MoveResult root_move;
  if (!playHeadlessMove(root, tape_seed, first_column, root_move)) {
    return -std::numeric_limits<double>::infinity();
  }
  ++metrics.generated;
  if (root.game_over) return oracle::rankState(root);

  std::vector<oracle::BeamNode> beam{{
      root, first_column, oracle::oracleDynamicKey(root),
      oracle::rankState(root),
  }};
  for (int ply = 1; ply < kDepth; ++ply) {
    std::unordered_map<std::string, oracle::BeamNode> candidates;
    candidates.reserve(static_cast<std::size_t>(kBeamWidth * kBoardSize));
    for (const oracle::BeamNode& node : beam) {
      if (node.state.game_over) {
        const auto found = candidates.find(node.dynamic_key);
        if (found == candidates.end()) {
          candidates.emplace(node.dynamic_key, node);
        } else {
          ++metrics.deduplicated;
          if (node.state.score > found->second.state.score) {
            found->second = node;
          }
        }
        continue;
      }
      int legal_count = 0;
      const auto legal = legalColumns(node.state.board, legal_count);
      for (int offset = 0; offset < legal_count; ++offset) {
        State next = node.state;
        MoveResult move;
        if (!playHeadlessMove(next, tape_seed, legal[offset], move)) continue;
        ++metrics.generated;
        oracle::BeamNode candidate{
            next,
            first_column,
            oracle::oracleDynamicKey(next),
            oracle::rankState(next),
        };
        const auto found = candidates.find(candidate.dynamic_key);
        if (found == candidates.end()) {
          candidates.emplace(candidate.dynamic_key, std::move(candidate));
        } else {
          ++metrics.deduplicated;
          if (candidate.state.score > found->second.state.score) {
            found->second = std::move(candidate);
          }
        }
      }
    }
    if (candidates.empty()) break;
    metrics.peak_candidates =
        std::max(metrics.peak_candidates, candidates.size());
    std::vector<oracle::BeamNode> ranked;
    ranked.reserve(candidates.size());
    for (auto& [key, node] : candidates) {
      (void)key;
      node.rank = oracle::rankState(node.state);
      ranked.push_back(std::move(node));
    }
    std::sort(ranked.begin(), ranked.end(), oracle::betterBeamNode);
    if (static_cast<int>(ranked.size()) > kBeamWidth) {
      ranked.resize(kBeamWidth);
    }
    beam = std::move(ranked);
  }
  if (beam.empty()) return -std::numeric_limits<double>::infinity();
  return std::max_element(
             beam.begin(), beam.end(),
             [](const oracle::BeamNode& first,
                const oracle::BeamNode& second) {
               return oracle::betterBeamNode(second, first);
             })
      ->rank;
}

int tieRank(int column) {
  constexpr std::array<int, kBoardSize> order{{3, 2, 4, 1, 5, 0, 6}};
  for (int rank = 0; rank < kBoardSize; ++rank) {
    if (order[rank] == column) return rank;
  }
  return kBoardSize;
}

void finalize(ActionValue& action) {
  action.mean = std::accumulate(action.scenarios.begin(),
                                action.scenarios.end(), 0.0) /
                kScenarios;
  auto sorted = action.scenarios;
  std::sort(sorted.begin(), sorted.end());
  constexpr int lower_count = (kScenarios + 3) / 4;
  action.lower_quartile =
      std::accumulate(sorted.begin(), sorted.begin() + lower_count, 0.0) /
      lower_count;
  action.utility = (1.0 - kLowerQuartileWeight) * action.mean +
                   kLowerQuartileWeight * action.lower_quartile;
}

Decision chooseMove(const State& input) {
  if (input.game_over) return {};
  bool mirrored = false;
  const State canonical = normalizedPublicState(input, mirrored);
  const std::uint32_t hash = publicHash(canonical);
  Decision result;
  for (int column = 0; column < kBoardSize; ++column) {
    if (!isLegal(canonical.board, column)) continue;
    ActionValue& action = result.actions[result.action_count++];
    action.column = column;
    for (int scenario = 0; scenario < kScenarios; ++scenario) {
      const std::uint32_t tape_seed = scenarioSeed(hash, scenario);
      if (column == 0) {
        ++result.metrics.first_next_disc_counts[headlessDisc(tape_seed, 1) - 1];
      }
      action.scenarios[scenario] = bestSyntheticContinuation(
          canonical, column, tape_seed, result.metrics);
    }
    finalize(action);
  }
  if (result.metrics.generated > kMaximumGeneratedPerDecision) {
    throw std::runtime_error("hindsight planner exceeded its work bound");
  }
  const ActionValue* best = nullptr;
  for (int index = 0; index < result.action_count; ++index) {
    const ActionValue& action = result.actions[index];
    if (best == nullptr || action.utility > best->utility + 1e-9 ||
        (std::abs(action.utility - best->utility) <= 1e-9 &&
         tieRank(action.column) < tieRank(best->column))) {
      best = &action;
    }
  }
  if (best == nullptr) return result;
  result.column = mirrored ? kBoardSize - 1 - best->column : best->column;
  return result;
}

struct GameResult {
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t generated = 0;
  std::uint64_t deduplicated = 0;
  std::size_t peak_candidates = 0;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
  std::uint64_t waves = 0;
  double seconds = 0;
};

void addThroughput(GameResult& result, const MoveResult& move) {
  result.waves += move.waves.size();
  for (const Wave& wave : move.waves) {
    result.clears += wave.cleared;
    result.reveals += wave.revealed;
  }
}

GameResult runGame(std::uint32_t seed, bool candidate,
                   std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  GameResult result;
  cfpi::BehaviorOptions baseline;
  baseline.max_depth = 3;
  baseline.chance_samples = 5;
  baseline.max_work = 1'000'000;
  baseline.max_cache_entries = 40'000;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    int action = -1;
    if (candidate) {
      const Decision decision = chooseMove(state);
      action = decision.column;
      result.generated += decision.metrics.generated;
      result.deduplicated += decision.metrics.deduplicated;
      result.peak_candidates =
          std::max(result.peak_candidates, decision.metrics.peak_candidates);
    } else {
      action = cfpi::chooseBehaviorAction(state, baseline);
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("public planner selected an illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("paired environment transition failed");
    }
    addThroughput(result, move);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.seconds = std::chrono::duration<double>(
                       std::chrono::steady_clock::now() - started)
                       .count();
  {
    const std::lock_guard<std::mutex> lock(progress_mutex);
    std::cerr << label << " 0x" << std::hex << seed << std::dec << ' '
              << result.score << '/' << result.moves << '\n';
  }
  return result;
}

struct Cohort {
  std::vector<GameResult> baseline;
  std::vector<GameResult> candidate;
};

Cohort runCohort(std::uint32_t seed_start, int games,
                 std::string_view phase) {
  Cohort result;
  result.baseline.resize(games);
  result.candidate.resize(games);
  std::atomic<int> next{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int index = next.fetch_add(1);
        if (index >= games) return;
        const std::uint32_t seed = seed_start + index;
        result.baseline[index] =
            runGame(seed, false, std::string(phase) + "-d3");
        result.candidate[index] =
            runGame(seed, true, std::string(phase) + "-hindsight");
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

struct Summary {
  double mean_score = 0;
  double mean_moves = 0;
  double clears_per_move = 0;
  double reveals_per_move = 0;
  double waves_per_move = 0;
  double generated_per_move = 0;
  double seconds_per_move = 0;
  int censored = 0;
};

Summary summarize(const std::vector<GameResult>& games) {
  Summary result;
  std::uint64_t moves = 0;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
  std::uint64_t waves = 0;
  std::uint64_t generated = 0;
  double seconds = 0;
  for (const GameResult& game : games) {
    result.mean_score += game.score / static_cast<double>(games.size());
    result.mean_moves += game.moves / static_cast<double>(games.size());
    result.censored += game.censored;
    moves += game.moves;
    clears += game.clears;
    reveals += game.reveals;
    waves += game.waves;
    generated += game.generated;
    seconds += game.seconds;
  }
  const double count = std::max<std::uint64_t>(1, moves);
  result.clears_per_move = clears / count;
  result.reveals_per_move = reveals / count;
  result.waves_per_move = waves / count;
  result.generated_per_move = generated / count;
  result.seconds_per_move = seconds / count;
  return result;
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"wavesPerMove\":" << summary.waves_per_move
         << ",\"generatedPerMove\":" << summary.generated_per_move
         << ",\"secondsPerMove\":" << summary.seconds_per_move
         << ",\"censored\":" << summary.censored << '}';
}

bool passedScreen(const Cohort& cohort) {
  const Summary baseline = summarize(cohort.baseline);
  const Summary candidate = summarize(cohort.candidate);
  return candidate.mean_score > baseline.mean_score &&
         candidate.mean_moves > baseline.mean_moves;
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

bool selfTest(std::ostream& output) {
  State state = initialHeadlessState(0x2d70'8b01u);
  for (const int column : {3, 1, 5}) {
    MoveResult move;
    if (!playHeadlessMove(state, 0x2d70'8b01u, column, move)) return false;
  }
  const Decision first = chooseMove(state);
  const Decision repeated = chooseMove(state);
  State metadata = state;
  metadata.score = 999'999;
  metadata.level = 77;
  metadata.moves_played = 388;
  const Decision blind = chooseMove(metadata);
  State reflected = state;
  reflected.board = cfpi::detail::mirrorBoard(state.board);
  const Decision mirrored = chooseMove(reflected);
  bool strata = true;
  for (const int count : first.metrics.first_next_disc_counts) {
    strata = strata && count == 1;
  }
  const bool passed = first.column >= 0 && first.column == repeated.column &&
                      first.column == blind.column &&
                      mirrored.column == kBoardSize - 1 - first.column &&
                      first.metrics.generated == repeated.metrics.generated &&
                      first.metrics.generated <= kMaximumGeneratedPerDecision &&
                      strata;
  output << "{\"format\":\"drop7-hindsight-self-test-v1\""
         << ",\"canonicalLevelBonus\":" << kLevelBonus
         << ",\"deterministic\":"
         << (first.column == repeated.column ? "true" : "false")
         << ",\"metadataBlind\":"
         << (first.column == blind.column ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (mirrored.column == kBoardSize - 1 - first.column ? "true" : "false")
         << ",\"stratified\":" << (strata ? "true" : "false")
         << ",\"generated\":" << first.metrics.generated
         << ",\"workBound\":" << kMaximumGeneratedPerDecision
         << ",\"passed\":" << (passed ? "true" : "false") << "}\n";
  return passed;
}

int run(std::ostream& console) {
  const auto started = std::chrono::steady_clock::now();
  const Cohort screen = runCohort(kScreenSeedStart, kScreenGames, "screen");
  const bool gate = passedScreen(screen);
  Cohort confirmation;
  if (gate) {
    confirmation = runCohort(kConfirmationSeedStart, kConfirmationGames,
                             "confirmation");
  }
  const std::string path = "/tmp/drop7-hindsight-planner.json";
  std::ofstream output(path);
  if (!output) throw std::runtime_error("could not open hindsight artifact");
  output << std::setprecision(12)
         << "{\n  \"format\":\"drop7-hindsight-planner-v1\","
         << "\n  \"canonicalLevelBonus\":" << kLevelBonus << ','
         << "\n  \"publicStateOnly\":true,"
         << "\n  \"realFutureUsedByPlanner\":false,"
         << "\n  \"configuration\":{\"depth\":" << kDepth
         << ",\"beamWidth\":" << kBeamWidth
         << ",\"scenarios\":" << kScenarios
         << ",\"lowerQuartileWeight\":" << kLowerQuartileWeight
         << ",\"maximumMoves\":" << kMaximumMoves << "},"
         << "\n  \"screen\":{\"seedStart\":\"0x3e8b0000\",\"games\":"
         << kScreenGames << ",\"baseline\":";
  writeSummary(output, summarize(screen.baseline));
  output << ",\"candidate\":";
  writeSummary(output, summarize(screen.candidate));
  output << ",\"passed\":" << (gate ? "true" : "false") << "},"
         << "\n  \"confirmationRead\":" << (gate ? "true" : "false");
  if (gate) {
    output << ",\n  \"confirmation\":{\"seedStart\":\"0x3e8c0000\","
           << "\"games\":" << kConfirmationGames << ",\"baseline\":";
    writeSummary(output, summarize(confirmation.baseline));
    output << ",\"candidate\":";
    writeSummary(output, summarize(confirmation.candidate));
    output << '}';
  }
  output << ",\n  \"peakRssBytes\":" << peakRssBytes()
         << ",\n  \"wallSeconds\":"
         << std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started)
                .count()
         << "\n}\n";
  output.close();
  const Summary baseline = summarize(screen.baseline);
  const Summary candidate = summarize(screen.candidate);
  console << std::fixed << std::setprecision(3)
          << "HINDSIGHT {\"screenPassed\":" << (gate ? "true" : "false")
          << ",\"baselineScore\":" << baseline.mean_score
          << ",\"candidateScore\":" << candidate.mean_score
          << ",\"baselineMoves\":" << baseline.mean_moves
          << ",\"candidateMoves\":" << candidate.mean_moves
          << ",\"confirmationRead\":" << (gate ? "true" : "false")
          << ",\"artifact\":\"" << path << "\"}\n";
  return 0;
}

}  // namespace drop7::hindsight

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::hindsight::selfTest(std::cout) ? 0 : 1;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--run") {
      return drop7::hindsight::run(std::cout);
    }
    std::cerr << "usage: drop7_hindsight_planner --self-test | --run\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_hindsight_planner: " << error.what() << '\n';
    return 1;
  }
}
