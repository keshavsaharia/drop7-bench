#define DROP7_D4_D2_ROLLOUT_VETO_LIBRARY
#include "d4-d2-rollout-veto.cpp"
#undef DROP7_D4_D2_ROLLOUT_VETO_LIBRARY

#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

// Measures gameplay quality for the unchanged D4 + D2/s7/h25 veto without
// changing its runtime or deployment eligibility.  It reads the fixed pilot
// and the three fitting seeds immediately following it.
namespace drop7::d4_d2_rollout_veto_quality_extension {

namespace policy = drop7::d4_d2_rollout_veto;
namespace d4 = drop7::fair_only_depth4;

constexpr std::uint32_t kFrozenPilotSeed = 0x3ded'0000u;
constexpr std::array<std::uint32_t, 3> kNewSeeds{{
    0x3ded'0001u,
    0x3ded'0002u,
    0x3ded'0003u,
}};
constexpr int kNewGames = static_cast<int>(kNewSeeds.size());
constexpr int kCombinedGames = 4;
constexpr int kCandidateWorkers = 3;
constexpr int kBaselineWorkers = 3;
constexpr double kWallLimitSeconds = 90.0 * 60.0;
constexpr double kLowerHalfRetention = 0.90;
constexpr int kMinimumNewJointWins = 2;
constexpr int kMinimumLeaveOneOutWins = 3;
constexpr std::uintmax_t kFrozenPilotBytes = 3'670;
constexpr std::string_view kFrozenPilotSha256 =
    "5841c90412d21c0a42ee6adc7a2233b3b585087bcc04f54fab7ef3274d3a607e";

static_assert(kNewSeeds[0] == policy::kFittingSeedStart + 1u);
static_assert(kNewSeeds[2] == policy::kFittingSeedStart + 3u);
static_assert(kNewSeeds[2] < policy::kHeldoutSeedStart);
static_assert((kFrozenPilotSeed >> 24) != 0x7du &&
              (kFrozenPilotSeed >> 24) != 0xd7u);
static_assert((kNewSeeds[0] >> 24) != 0x7du &&
              (kNewSeeds[0] >> 24) != 0xd7u);
static_assert(policy::kRolloutHorizon == 25);
static_assert(policy::kScenarios == 7);
static_assert(policy::kContinuationDepth == 2);
static_assert(policy::kDangerHeight == 4);
static_assert(policy::kMaximumRootQLoss == 7'000.0);
static_assert(policy::kMaximumRssBytes == 128u * 1024u * 1024u);
static_assert(d4::kCandidateDepth == 4);

struct Options {
  std::string frozen_pilot = "/tmp/drop7-d4-d2-rollout-veto.json";
  std::string output =
      "/tmp/drop7-d4-d2-rollout-veto-quality-extension.json";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--frozen-pilot") {
      if (++index >= argc) {
        throw std::invalid_argument("missing --frozen-pilot value");
      }
      result.frozen_pilot = argv[index];
    } else if (argument == "--output") {
      if (++index >= argc) throw std::invalid_argument("missing --output value");
      result.output = argv[index];
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  return result;
}

[[noreturn]] void malformed(std::string_view field) {
  throw std::runtime_error("malformed frozen pilot artifact: " +
                           std::string(field));
}

std::size_t requireFind(std::string_view text, std::string_view marker,
                        std::size_t begin = 0) {
  const std::size_t position = text.find(marker, begin);
  if (position == std::string_view::npos) malformed(marker);
  return position + marker.size();
}

std::size_t matchingBrace(std::string_view text, std::size_t open) {
  if (open >= text.size() || text[open] != '{') malformed("object start");
  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (std::size_t index = open; index < text.size(); ++index) {
    const char value = text[index];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (value == '\\') {
        escaped = true;
      } else if (value == '"') {
        in_string = false;
      }
    } else if (value == '"') {
      in_string = true;
    } else if (value == '{') {
      ++depth;
    } else if (value == '}' && --depth == 0) {
      return index;
    }
  }
  malformed("unclosed object");
}

std::string_view objectAfter(std::string_view text, std::string_view marker,
                             std::size_t begin = 0) {
  const std::size_t content = requireFind(text, marker, begin);
  const std::size_t open = content - 1;
  const std::size_t close = matchingBrace(text, open);
  return text.substr(open, close - open + 1);
}

long long integerAfter(std::string_view text, std::string_view marker) {
  const std::size_t position = requireFind(text, marker);
  const std::string tail(text.substr(position));
  char* end = nullptr;
  const long long result = std::strtoll(tail.c_str(), &end, 10);
  if (end == tail.c_str()) malformed(marker);
  return result;
}

double doubleAfter(std::string_view text, std::string_view marker) {
  const std::size_t position = requireFind(text, marker);
  const std::string tail(text.substr(position));
  char* end = nullptr;
  const double result = std::strtod(tail.c_str(), &end);
  if (end == tail.c_str() || !std::isfinite(result)) malformed(marker);
  return result;
}

bool boolAfter(std::string_view text, std::string_view marker) {
  const std::size_t position = requireFind(text, marker);
  if (text.substr(position, 4) == "true") return true;
  if (text.substr(position, 5) == "false") return false;
  malformed(marker);
}

template <typename Target>
Target integerField(std::string_view object, std::string_view marker) {
  const long long value = integerAfter(object, marker);
  if (value < 0 ||
      static_cast<unsigned long long>(value) >
          std::numeric_limits<Target>::max()) {
    malformed(marker);
  }
  return static_cast<Target>(value);
}

policy::GameResult parseGame(std::string_view object) {
  policy::GameResult result;
  result.seed = integerField<std::uint32_t>(object, "\"seed\":");
  result.score = integerAfter(object, "\"score\":");
  result.moves = integerField<int>(object, "\"moves\":");
  result.censored = boolAfter(object, "\"censored\":");
  result.numbered_cleared =
      integerField<std::uint64_t>(object, "\"numberedCleared\":");
  result.covers_revealed =
      integerField<std::uint64_t>(object, "\"coversRevealed\":");
  result.maximum_chain = integerField<int>(object, "\"maximumChain\":");
  result.decisions =
      integerField<std::uint64_t>(object, "\"decisions\":");
  result.routed_decisions =
      integerField<std::uint64_t>(object, "\"routedDecisions\":");
  result.switches = integerField<std::uint64_t>(object, "\"switches\":");
  result.passing_alternatives =
      integerField<std::uint64_t>(object, "\"passingAlternatives\":");
  result.survivor_rejections =
      integerField<std::uint64_t>(object, "\"survivorRejections\":");
  result.clear_rejections =
      integerField<std::uint64_t>(object, "\"clearRejections\":");
  result.return_rejections =
      integerField<std::uint64_t>(object, "\"returnRejections\":");
  result.root_q_rejections =
      integerField<std::uint64_t>(object, "\"rootQRejections\":");
  result.d4_work = integerField<std::uint64_t>(object, "\"d4Work\":");
  result.d4_nodes = integerField<std::uint64_t>(object, "\"d4Nodes\":");
  result.d4_cache_hits =
      integerField<std::uint64_t>(object, "\"d4CacheHits\":");
  result.peak_d4_cache_entries =
      integerField<std::size_t>(object, "\"peakD4CacheEntries\":");
  result.synthetic_transitions =
      integerField<std::uint64_t>(object, "\"syntheticTransitions\":");
  result.continuation.calls =
      integerField<std::uint64_t>(object, "\"d2Calls\":");
  result.continuation.work =
      integerField<std::uint64_t>(object, "\"d2Work\":");
  result.continuation.nodes =
      integerField<std::uint64_t>(object, "\"d2Nodes\":");
  result.continuation.cache_hits =
      integerField<std::uint64_t>(object, "\"d2CacheHits\":");
  result.continuation.root_actions =
      integerField<std::uint64_t>(object, "\"d2RootActions\":");
  result.continuation.peak_cache_entries =
      integerField<std::size_t>(object, "\"peakD2CacheEntries\":");
  result.continuation.full_root = boolAfter(object, "\"d2FullRoot\":");
  result.elapsed_seconds = doubleAfter(object, "\"elapsedSeconds\":");
  result.peak_rss_bytes =
      integerField<std::uint64_t>(object, "\"peakRssBytes\":");
  return result;
}

struct FrozenPair {
  policy::GameResult baseline;
  policy::GameResult candidate;
};

FrozenPair loadFrozenPair(const std::string& path) {
  std::error_code error;
  if (std::filesystem::file_size(path, error) != kFrozenPilotBytes || error) {
    throw std::runtime_error("frozen pilot byte count mismatch");
  }
  std::ifstream input(path);
  if (!input) throw std::runtime_error("could not open frozen pilot artifact");
  const std::string document((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
  if (document.find("\"pilotPairOnly\":true") == std::string::npos ||
      document.find("\"runtimeStop\":true") == std::string::npos ||
      document.find("\"readGameSeeds\":[\"0x3ded0000\"]") ==
          std::string::npos) {
    malformed("pilot provenance");
  }
  const std::size_t pair_begin = requireFind(document, "\"pairs\":[{");
  const std::string_view baseline =
      objectAfter(document, "\"baseline\":{", pair_begin);
  const std::size_t baseline_offset =
      static_cast<std::size_t>(baseline.data() - document.data());
  const std::string_view candidate = objectAfter(
      document, "\"candidate\":{", baseline_offset + baseline.size());
  FrozenPair result{parseGame(baseline), parseGame(candidate)};

  const std::size_t pilot_begin = requireFind(document, "\"pilot\":{");
  const std::string_view candidate_summary =
      objectAfter(document, "\"candidate\":{", pilot_begin);
  result.candidate.switch_return_lower95_sum =
      doubleAfter(candidate_summary, "\"meanSwitchReturnLower95\":") *
      static_cast<double>(result.candidate.switches);
  result.candidate.switch_clear_advantage_sum =
      doubleAfter(candidate_summary, "\"meanSwitchClearAdvantage\":") *
      static_cast<double>(result.candidate.switches);
  result.candidate.switch_root_q_loss_sum =
      doubleAfter(candidate_summary, "\"meanSwitchRootQLoss\":") *
      static_cast<double>(result.candidate.switches);
  result.candidate.switch_survivor_advantage_sum = static_cast<std::int64_t>(
      std::llround(doubleAfter(
                       candidate_summary,
                       "\"meanSwitchSurvivorAdvantage\":") *
                   static_cast<double>(result.candidate.switches)));

  if (result.baseline.seed != kFrozenPilotSeed ||
      result.candidate.seed != kFrozenPilotSeed ||
      result.baseline.score != 159'616 || result.baseline.moves != 105 ||
      result.candidate.score != 404'047 || result.candidate.moves != 250 ||
      result.candidate.switches != 12 ||
      result.candidate.routed_decisions != 179 ||
      result.baseline.censored || result.candidate.censored) {
    malformed("frozen pair values");
  }
  return result;
}

using Clock = std::chrono::steady_clock;

class WallLimitReached : public std::runtime_error {
 public:
  WallLimitReached() : std::runtime_error("quality-audit wall limit reached") {}
};

bool deadlineReached(Clock::time_point deadline) {
  return Clock::now() >= deadline;
}

policy::GameResult runAuthorizedGame(std::uint32_t seed, bool candidate,
                                     std::string_view label,
                                     Clock::time_point deadline) {
  if (std::find(kNewSeeds.begin(), kNewSeeds.end(), seed) == kNewSeeds.end()) {
    throw std::invalid_argument("game seed is outside quality-audit allowlist");
  }
  const auto started = Clock::now();
  State state = initialHeadlessState(seed);
  policy::GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < policy::kMaximumMoves) {
    if (deadlineReached(deadline)) throw WallLimitReached{};
    d4::SearchDecision search;
    int action = -1;
    if (!candidate) {
      search = d4::chooseDepth4Action(state);
      action = search.action;
    } else {
      const policy::Decision decision = policy::chooseAction(state);
      search = decision.search;
      action = decision.action;
      result.danger_decisions += decision.danger;
      result.routed_decisions += decision.routed;
      result.switches += decision.switched;
      result.passing_alternatives += decision.passing_alternatives;
      result.survivor_rejections += decision.survivor_rejections;
      result.clear_rejections += decision.clear_rejections;
      result.return_rejections += decision.return_rejections;
      result.root_q_rejections += decision.root_q_rejections;
      if (decision.switched) {
        result.switch_return_lower95_sum +=
            decision.selected_return_lower95;
        result.switch_clear_advantage_sum +=
            decision.selected_clear_advantage;
        result.switch_root_q_loss_sum += decision.selected_root_q_loss;
        result.switch_survivor_advantage_sum +=
            decision.selected_survivor_advantage;
      }
      result.synthetic_transitions += decision.rollout.synthetic_transitions;
      result.continuation.calls += decision.rollout.continuation.calls;
      result.continuation.work += decision.rollout.continuation.work;
      result.continuation.nodes += decision.rollout.continuation.nodes;
      result.continuation.cache_hits +=
          decision.rollout.continuation.cache_hits;
      result.continuation.peak_cache_entries = std::max(
          result.continuation.peak_cache_entries,
          decision.rollout.continuation.peak_cache_entries);
      result.continuation.root_actions +=
          decision.rollout.continuation.root_actions;
      result.continuation.full_root =
          result.continuation.full_root &&
          decision.rollout.continuation.full_root;
    }
    if (deadlineReached(deadline)) throw WallLimitReached{};
    if (!search.complete || search.completed_depth != d4::kCandidateDepth ||
        !isLegal(state.board, action)) {
      throw std::runtime_error("quality-audit policy lost exact D4/legality");
    }
    ++result.decisions;
    result.d4_work += search.work;
    result.d4_nodes += search.nodes;
    result.d4_cache_hits += search.cache_hits;
    result.peak_d4_cache_entries =
        std::max(result.peak_d4_cache_entries, search.cache_entries);
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("quality-audit real transition failed");
    }
    policy::observeMove(move, result);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.peak_rss_bytes = d4::peakRssBytes();
  result.elapsed_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  if (result.peak_rss_bytes > policy::kMaximumRssBytes ||
      result.peak_d4_cache_entries > d4::kMaximumCacheEntries ||
      result.continuation.peak_cache_entries >
          policy::kWorstD2CacheEntries ||
      !result.continuation.full_root) {
    throw std::runtime_error("quality-audit game exceeded resource proof");
  }
  policy::reportGame(label, result);
  return result;
}

struct ArmResult {
  std::array<policy::GameResult, kNewGames> games{};
  double wall_seconds = 0.0;
};

ArmResult runArm(bool candidate, int workers, Clock::time_point deadline,
                 std::string_view label) {
  if (workers != kNewGames) {
    throw std::invalid_argument("quality audit requires one worker per new game");
  }
  const auto started = Clock::now();
  ArmResult result;
  std::vector<std::future<void>> futures;
  futures.reserve(kNewGames);
  for (int index = 0; index < kNewGames; ++index) {
    futures.push_back(std::async(std::launch::async, [&, index] {
      result.games[static_cast<std::size_t>(index)] = runAuthorizedGame(
          kNewSeeds[static_cast<std::size_t>(index)], candidate,
          std::string(label), deadline);
    }));
  }
  std::exception_ptr exception;
  for (auto& future : futures) {
    try {
      future.get();
    } catch (...) {
      if (exception == nullptr) exception = std::current_exception();
    }
  }
  if (exception != nullptr) std::rethrow_exception(exception);
  result.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

struct QualityGate {
  bool mean_score_improved = false;
  bool mean_moves_improved = false;
  int new_score_wins = 0;
  int new_move_wins = 0;
  int new_joint_wins = 0;
  bool new_wins_ok = false;
  bool clears_per_move_improved = false;
  bool reveals_per_move_improved = false;
  bool lower_half_score_retained = false;
  bool lower_half_moves_retained = false;
  int leave_one_out_positive_both = 0;
  bool leave_one_out_ok = false;
  bool passed = false;
};

QualityGate evaluateQualityGate(const policy::Cohort& combined) {
  if (combined.baseline.size() != kCombinedGames ||
      combined.candidate.size() != kCombinedGames) {
    throw std::invalid_argument("quality gate requires four pairs");
  }
  const policy::Summary baseline = policy::summarize(combined.baseline);
  const policy::Summary candidate = policy::summarize(combined.candidate);
  const policy::Paired comparison = policy::paired(combined);
  QualityGate result;
  result.mean_score_improved = candidate.mean_score > baseline.mean_score;
  result.mean_moves_improved = candidate.mean_moves > baseline.mean_moves;
  for (int index = 1; index < kCombinedGames; ++index) {
    const policy::GameResult& left =
        combined.baseline[static_cast<std::size_t>(index)];
    const policy::GameResult& right =
        combined.candidate[static_cast<std::size_t>(index)];
    result.new_score_wins += right.score > left.score;
    result.new_move_wins += right.moves > left.moves;
    result.new_joint_wins +=
        right.score > left.score && right.moves > left.moves;
  }
  result.new_wins_ok = result.new_joint_wins >= kMinimumNewJointWins;
  result.clears_per_move_improved =
      candidate.clears_per_move > baseline.clears_per_move;
  result.reveals_per_move_improved =
      candidate.reveals_per_move > baseline.reveals_per_move;
  result.lower_half_score_retained =
      candidate.lower_half_score >=
      kLowerHalfRetention * baseline.lower_half_score;
  result.lower_half_moves_retained =
      candidate.lower_half_moves >=
      kLowerHalfRetention * baseline.lower_half_moves;
  result.leave_one_out_positive_both =
      comparison.leave_one_out_positive_both;
  result.leave_one_out_ok =
      result.leave_one_out_positive_both >= kMinimumLeaveOneOutWins;
  result.passed =
      result.mean_score_improved && result.mean_moves_improved &&
      result.new_wins_ok && result.clears_per_move_improved &&
      result.reveals_per_move_improved &&
      result.lower_half_score_retained &&
      result.lower_half_moves_retained && result.leave_one_out_ok;
  return result;
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

bool selfTest(const Options& options, std::ostream& output) {
  const bool dependency = policy::selfTest(output);
  const FrozenPair frozen = loadFrozenPair(options.frozen_pilot);
  const policy::Summary baseline = policy::summarize({frozen.baseline});
  const policy::Summary candidate = policy::summarize({frozen.candidate});
  expect(baseline.mean_score == 159'616.0 && baseline.mean_moves == 105.0 &&
             candidate.mean_score == 404'047.0 &&
             candidate.mean_moves == 250.0 &&
             candidate.switches == 12,
         "frozen-pair reconstruction");
  expect(!deadlineReached(Clock::now() + std::chrono::seconds(1)) &&
             deadlineReached(Clock::now() - std::chrono::seconds(1)),
         "wall deadline wiring");
  expect(std::adjacent_find(kNewSeeds.begin(), kNewSeeds.end()) ==
                 kNewSeeds.end() &&
             std::all_of(kNewSeeds.begin(), kNewSeeds.end(), [](auto seed) {
               return seed >= 0x3ded'0001u && seed <= 0x3ded'0003u &&
                      (seed >> 24) != 0x7du && (seed >> 24) != 0xd7u;
             }),
         "seed allowlist wiring");

  policy::Cohort synthetic;
  for (int index = 0; index < kCombinedGames; ++index) {
    policy::GameResult left;
    left.seed = static_cast<std::uint32_t>(index);
    left.score = 100;
    left.moves = 10;
    left.numbered_cleared = 10;
    left.covers_revealed = 10;
    policy::GameResult right = left;
    right.score = 110;
    right.moves = 11;
    right.numbered_cleared = 13;
    right.covers_revealed = 13;
    synthetic.baseline.push_back(left);
    synthetic.candidate.push_back(right);
  }
  const QualityGate passing = evaluateQualityGate(synthetic);
  synthetic.candidate[2].score = 50;
  synthetic.candidate[2].moves = 5;
  synthetic.candidate[3].score = 50;
  synthetic.candidate[3].moves = 5;
  const QualityGate failing = evaluateQualityGate(synthetic);
  expect(passing.passed && passing.new_joint_wins == 3 &&
             passing.leave_one_out_positive_both == 4 && !failing.passed &&
             !failing.new_wins_ok,
         "quality gate boundaries");

  // Sanitizer shadow/quarantine memory is intentionally not compared with the
  // runtime 128 MiB cap.  Every optimized game enforces that cap in the game
  // runner, while the included self-test checks the per-search
  // D4/D2 cache and work bounds under either build.
  const bool passed = dependency;
  output << std::setprecision(12)
         << "D4_D2_ROLLOUT_VETO_QUALITY_EXTENSION_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"unchangedPolicyDependency\":"
         << (dependency ? "true" : "false")
         << ",\"frozenPilotLoadedNotReplayed\":true"
         << ",\"seedAllowlistExact\":true"
         << ",\"qualityGateWiring\":true"
         << ",\"wallLimitWiring\":true"
         << ",\"productionRssCapEnforcedAtGameBoundary\":true"
         << ",\"newSeeds\":[\"0x3ded0001\",\"0x3ded0002\",\"0x3ded0003\"]"
         << ",\"rolloutHorizon\":" << policy::kRolloutHorizon
         << ",\"scenarios\":" << policy::kScenarios
         << ",\"continuationDepth\":" << policy::kContinuationDepth
         << ",\"peakRssBytes\":" << d4::peakRssBytes() << "}\n";
  return passed;
}

void writeProtocol(std::ostream& output, std::string_view status) {
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"d4-d2-rollout-veto-quality-extension\",\n"
         << "  \"status\":\"" << status << "\",\n"
         << "  \"qualityOnly\":true,\n"
         << "  \"deployable\":false,\n"
         << "  \"runtimeGateStillRejected\":true,\n"
         << "  \"oldRuntimeGateIgnoredOnlyForThisAudit\":true,\n"
         << "  \"policyUnchanged\":{\"root\":\"fair-D4\",\"dangerHeight\":"
         << policy::kDangerHeight
         << ",\"rollout\":\"D2/s7/h25\",\"continuationDepth\":"
         << policy::kContinuationDepth
         << ",\"scenarios\":" << policy::kScenarios
         << ",\"horizon\":" << policy::kRolloutHorizon
         << ",\"rootQLossMaximum\":" << policy::kMaximumRootQLoss
         << ",\"policySourceSha256\":\"2f1018304d9cd2729bdf0c2ac552e2c8e9d0976374675f1be97257c81927f433\"},\n"
         << "  \"frozenPilot\":{\"seed\":\"0x3ded0000\",\"reexecuted\":false"
         << ",\"bytes\":" << kFrozenPilotBytes
         << ",\"sha256\":\"" << kFrozenPilotSha256 << "\"},\n"
         << "  \"newGameplaySeedsAuthorized\":[\"0x3ded0001\",\"0x3ded0002\",\"0x3ded0003\"],\n"
         << "  \"prohibitedUntouched\":[\"0x3dee... heldout\",\"0x3e... all\",\"0x7d... protected\",\"0xd7... protected\"],\n"
         << "  \"workers\":{\"baseline\":" << kBaselineWorkers
         << ",\"candidate\":" << kCandidateWorkers << "},\n"
         << "  \"wallLimitSeconds\":" << kWallLimitSeconds << ",\n"
         << "  \"frozenQualityGate\":{\"meanScoreImproved\":true"
         << ",\"meanMovesImproved\":true"
         << ",\"minimumNewJointScoreAndMoveWins\":"
         << kMinimumNewJointWins
         << ",\"newPairs\":" << kNewGames
         << ",\"clearsPerMoveImproved\":true"
         << ",\"revealsPerMoveImproved\":true"
         << ",\"lowerHalfScoreAndMovesRetention\":"
         << kLowerHalfRetention
         << ",\"minimumLeaveOneOutPositiveScoreAndMoves\":"
         << kMinimumLeaveOneOutWins << ",\"leaveOneOutSubsets\":"
         << kCombinedGames << "}";
}

void writeStatusArtifact(const Options& options, std::string_view status,
                         double wall_seconds) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not write quality status artifact");
  writeProtocol(output, status);
  output << ",\n  \"wallSeconds\":" << wall_seconds
         << ",\n  \"peakRssBytes\":" << d4::peakRssBytes() << "\n}\n";
}

void writeGate(std::ostream& output, const QualityGate& gate) {
  output << "{\"meanScoreImproved\":"
         << (gate.mean_score_improved ? "true" : "false")
         << ",\"meanMovesImproved\":"
         << (gate.mean_moves_improved ? "true" : "false")
         << ",\"newScoreWins\":" << gate.new_score_wins
         << ",\"newMoveWins\":" << gate.new_move_wins
         << ",\"newJointScoreAndMoveWins\":" << gate.new_joint_wins
         << ",\"newWinsOk\":" << (gate.new_wins_ok ? "true" : "false")
         << ",\"clearsPerMoveImproved\":"
         << (gate.clears_per_move_improved ? "true" : "false")
         << ",\"revealsPerMoveImproved\":"
         << (gate.reveals_per_move_improved ? "true" : "false")
         << ",\"lowerHalfScoreRetained\":"
         << (gate.lower_half_score_retained ? "true" : "false")
         << ",\"lowerHalfMovesRetained\":"
         << (gate.lower_half_moves_retained ? "true" : "false")
         << ",\"leaveOneOutPositiveScoreAndMoves\":"
         << gate.leave_one_out_positive_both
         << ",\"leaveOneOutOk\":"
         << (gate.leave_one_out_ok ? "true" : "false")
         << ",\"passed\":" << (gate.passed ? "true" : "false") << '}';
}

void writeCompleteArtifact(const Options& options,
                           const policy::Cohort& combined,
                           const ArmResult& baseline_arm,
                           const ArmResult& candidate_arm,
                           const QualityGate& gate, double total_wall) {
  const policy::Summary baseline = policy::summarize(combined.baseline);
  const policy::Summary candidate = policy::summarize(combined.candidate);
  const policy::Paired comparison = policy::paired(combined);
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not write quality artifact");
  writeProtocol(output, "complete");
  output << ",\n  \"newRun\":{\"baselineWallSeconds\":"
         << baseline_arm.wall_seconds
         << ",\"candidateWallSeconds\":" << candidate_arm.wall_seconds
         << ",\"newPairs\":[";
  for (int index = 0; index < kNewGames; ++index) {
    if (index != 0) output << ',';
    output << "{\"baseline\":";
    policy::writeGame(output,
                      baseline_arm.games[static_cast<std::size_t>(index)]);
    output << ",\"candidate\":";
    policy::writeGame(output,
                      candidate_arm.games[static_cast<std::size_t>(index)]);
    output << '}';
  }
  output << "]},\n  \"combinedFourGameFitting\":{\"baseline\":";
  policy::writeSummary(output, baseline);
  output << ",\"candidate\":";
  policy::writeSummary(output, candidate);
  output << ",\"paired\":{\"score\":";
  policy::writeDifference(output, comparison.score);
  output << ",\"moves\":";
  policy::writeDifference(output, comparison.moves);
  output << ",\"leaveOneOutPositiveBoth\":"
         << comparison.leave_one_out_positive_both << "},\"pairs\":[";
  for (int index = 0; index < kCombinedGames; ++index) {
    if (index != 0) output << ',';
    output << "{\"baseline\":";
    policy::writeGame(output,
                      combined.baseline[static_cast<std::size_t>(index)]);
    output << ",\"candidate\":";
    policy::writeGame(output,
                      combined.candidate[static_cast<std::size_t>(index)]);
    output << '}';
  }
  output << "]},\n  \"qualityGate\":";
  writeGate(output, gate);
  output << ",\n  \"qualityConclusion\":\""
         << (gate.passed ? "passed" : "failed") << "\""
         << ",\n  \"deploymentQualified\":false"
         << ",\n  \"totalWallSeconds\":" << total_wall
         << ",\n  \"peakRssBytes\":" << d4::peakRssBytes() << "\n}\n";
}

int run(const Options& options, std::ostream& report) {
  const auto all_started = Clock::now();
  if (!selfTest(options, report)) return EXIT_FAILURE;
  const FrozenPair frozen = loadFrozenPair(options.frozen_pilot);
  writeStatusArtifact(options, "preregistered-before-new-seeds", 0.0);
  const Clock::time_point deadline =
      all_started + std::chrono::duration_cast<Clock::duration>(
                        std::chrono::duration<double>(kWallLimitSeconds));
  try {
    // Baselines are short and are completed first; the three expensive
    // unchanged candidates then run concurrently, one allowlisted tape each.
    const ArmResult baseline_arm =
        runArm(false, kBaselineWorkers, deadline, "quality-baseline");
    if (deadlineReached(deadline)) throw WallLimitReached{};
    const ArmResult candidate_arm =
        runArm(true, kCandidateWorkers, deadline, "quality-candidate");
    const double total_wall =
        std::chrono::duration<double>(Clock::now() - all_started).count();
    if (total_wall > kWallLimitSeconds) throw WallLimitReached{};

    policy::Cohort combined;
    combined.baseline.push_back(frozen.baseline);
    combined.candidate.push_back(frozen.candidate);
    for (int index = 0; index < kNewGames; ++index) {
      combined.baseline.push_back(
          baseline_arm.games[static_cast<std::size_t>(index)]);
      combined.candidate.push_back(
          candidate_arm.games[static_cast<std::size_t>(index)]);
    }
    combined.wall_seconds = total_wall;
    const QualityGate gate = evaluateQualityGate(combined);
    writeCompleteArtifact(options, combined, baseline_arm, candidate_arm,
                          gate, total_wall);
    const policy::Summary baseline = policy::summarize(combined.baseline);
    const policy::Summary candidate = policy::summarize(combined.candidate);
    report << std::fixed << std::setprecision(6)
           << "D4_D2_ROLLOUT_VETO_QUALITY_EXTENSION_RESULT {\"qualityPassed\":"
           << (gate.passed ? "true" : "false")
           << ",\"deploymentQualified\":false"
           << ",\"baselineMeanScore\":" << baseline.mean_score
           << ",\"candidateMeanScore\":" << candidate.mean_score
           << ",\"baselineMeanMoves\":" << baseline.mean_moves
           << ",\"candidateMeanMoves\":" << candidate.mean_moves
           << ",\"newJointWins\":" << gate.new_joint_wins
           << ",\"leaveOneOutPositiveBoth\":"
           << gate.leave_one_out_positive_both
           << ",\"totalWallSeconds\":" << total_wall
           << ",\"peakRssBytes\":" << d4::peakRssBytes()
           << ",\"output\":\"" << options.output << "\"}\n";
    return EXIT_SUCCESS;
  } catch (const WallLimitReached&) {
    const double wall =
        std::chrono::duration<double>(Clock::now() - all_started).count();
    writeStatusArtifact(options, "stopped-at-90-minute-wall-limit", wall);
    report << "D4_D2_ROLLOUT_VETO_QUALITY_EXTENSION_STOP {\"wallLimit\":true,\"deploymentQualified\":false,\"wallSeconds\":"
           << wall << "}\n";
    return 2;
  }
}

}  // namespace drop7::d4_d2_rollout_veto_quality_extension

#ifndef DROP7_D4_D2_ROLLOUT_VETO_QUALITY_EXTENSION_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc < 2) throw std::invalid_argument("missing mode");
    const std::string mode = argv[1];
    const auto options =
        drop7::d4_d2_rollout_veto_quality_extension::parseOptions(argc, argv,
                                                                  2);
    if (mode == "--self-test") {
      return drop7::d4_d2_rollout_veto_quality_extension::selfTest(
                 options, std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (mode == "--run") {
      return drop7::d4_d2_rollout_veto_quality_extension::run(options,
                                                               std::cout);
    }
    throw std::invalid_argument(
        "usage: drop7_d4_d2_rollout_veto_quality_extension "
        "--self-test|--run [--frozen-pilot path] [--output path]");
  } catch (const std::exception& error) {
    std::cerr << "drop7 quality-extension error: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
#endif
