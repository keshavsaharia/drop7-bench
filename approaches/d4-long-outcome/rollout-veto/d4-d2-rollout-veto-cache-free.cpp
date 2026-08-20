#define DROP7_D4_D2_ROLLOUT_VETO_EXACT_COMPRESSED_LIBRARY
#include "d4-d2-rollout-veto-exact-compressed.cpp"
#undef DROP7_D4_D2_ROLLOUT_VETO_EXACT_COMPRESSED_LIBRARY

#include <bit>

// Evaluates the bounded rollout compressor's full-width fair-D2 tree directly,
// without its generic string/LRU transposition cache.  It preserves every
// value, action, tape, gate, and public-state input.
namespace drop7::d4_d2_rollout_veto_cache_free {

namespace compressed =
    drop7::d4_d2_rollout_veto_exact_compressed;
namespace original = drop7::d4_d2_rollout_veto;
namespace d4 = drop7::fair_only_depth4;
namespace fair = drop7::fair_only_horizon;

constexpr std::uint32_t kPilotSeed = compressed::kPilotSeed;
constexpr std::string_view kCompressedArtifact =
    "/tmp/drop7-d4-d2-rollout-veto-exact-compressed.json";
constexpr std::string_view kCompressedArtifactSha256 =
    "93873f5b5351311f3834a02dc676df86d74ded3db0166c6e3d719ee80f24fc45";
constexpr double kCompressedSeconds = 639.320592375;
constexpr std::uint64_t kCompressedD2Calls = 152'884;
constexpr std::uint64_t kCompressedD2Work = 310'321'943;

static_assert(original::kContinuationDepth == 2);
static_assert(original::kWorstD2Work == 2'485);
static_assert(kPilotSeed == 0x3ded'0000u);

struct Options {
  std::string output =
      "/tmp/drop7-d4-d2-rollout-veto-cache-free.json";
  std::string audit_output =
      "/tmp/drop7-d4-d2-rollout-veto-cache-free-audit.json";
  std::string trace_output =
      "/tmp/drop7-d4-d2-rollout-veto-cache-free-trace.jsonl";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string argument = argv[index];
    if (argument == "--output") result.output = argv[index + 1];
    else if (argument == "--audit-output") {
      result.audit_output = argv[index + 1];
    } else if (argument == "--trace-output") {
      result.trace_output = argv[index + 1];
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  return result;
}

struct DirectContext {
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
};

struct ActionValue {
  double value = 0.0;
  double expected_score = 0.0;
};

double directBestFuture(const State& state, int depth,
                        DirectContext& context);

ActionValue directEvaluateAction(const State& state, int column, int depth,
                                 DirectContext& context) {
  const std::uint32_t state_seed =
      cfpi::detail::scenarioSeedForState(state, fair::kPolicySeed, depth);
  ActionValue result;
  for (int sample = 0; sample < fair::kChanceSamples; ++sample) {
    cfpi::detail::StratifiedRandom random{
        state_seed, sample, fair::kChanceSamples, 0};
    MoveResult move;
    const bool played =
        cfpi::detail::playMoveSampled(state, column, random, move);
    ++context.work;
    if (!played) {
      result.value += fair::kTerminalUtility;
      continue;
    }
    const double score_delta = static_cast<double>(move.score_delta);
    result.expected_score += score_delta;
    if (move.state.game_over) {
      result.value += score_delta + fair::kTerminalUtility;
      continue;
    }
    move.state.score = 0;
    move.state.next_disc = cfpi::detail::sampledNextDisc(
        state_seed, sample, fair::kChanceSamples);
    bool ignored = false;
    const State next = cfpi::detail::canonicalState(move.state, ignored);
    result.value +=
        score_delta + directBestFuture(next, depth - 1, context);
  }
  result.value /= fair::kChanceSamples;
  result.expected_score /= fair::kChanceSamples;
  return result;
}

double directBestFuture(const State& state, int depth,
                        DirectContext& context) {
  ++context.nodes;
  if (state.game_over) return fair::kTerminalUtility;
  if (depth == 0) {
    ++context.work;
    const double value = fair::fairLeaf(state);
    if (!std::isfinite(value)) {
      throw std::runtime_error("cache-free D2 leaf was non-finite");
    }
    return value;
  }
  double best = -std::numeric_limits<double>::infinity();
  for (const int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(state.board, column)) continue;
    best = std::max(
        best, directEvaluateAction(state, column, depth, context).value);
  }
  return std::isfinite(best) ? best : fair::kTerminalUtility;
}

struct FullEvaluation {
  int action = -1;
  double value = -std::numeric_limits<double>::infinity();
  std::array<double, kBoardSize> values{};
  std::array<double, kBoardSize> expected_scores{};
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
  int root_actions = 0;
};

FullEvaluation cacheFreeFull(const original::ObservableState& source) {
  if (source.game_over) return {};
  bool mirrored = false;
  const State canonical =
      cfpi::detail::canonicalState(original::materialize(source), mirrored);
  FullEvaluation canonical_result;
  canonical_result.values.fill(-std::numeric_limits<double>::infinity());
  canonical_result.expected_scores.fill(
      -std::numeric_limits<double>::infinity());
  DirectContext context;
  for (const int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(canonical.board, column)) continue;
    ++canonical_result.root_actions;
    const ActionValue candidate = directEvaluateAction(
        canonical, column, original::kContinuationDepth, context);
    canonical_result.values[column] = candidate.value;
    canonical_result.expected_scores[column] = candidate.expected_score;
    if (candidate.value > canonical_result.value) {
      canonical_result.value = candidate.value;
      canonical_result.action = column;
    }
  }
  canonical_result.nodes = context.nodes;
  canonical_result.work = context.work;
  if (canonical_result.action < 0 ||
      canonical_result.work > original::kWorstD2Work) {
    throw std::runtime_error("cache-free D2 full-root proof failed");
  }
  if (!mirrored) return canonical_result;
  FullEvaluation reflected = canonical_result;
  reflected.action = kBoardSize - 1 - canonical_result.action;
  for (int column = 0; column < kBoardSize; ++column) {
    reflected.values[kBoardSize - 1 - column] =
        canonical_result.values[column];
    reflected.expected_scores[kBoardSize - 1 - column] =
        canonical_result.expected_scores[column];
  }
  return reflected;
}

int cacheFreeDepthTwoAction(const original::ObservableState& source,
                            original::D2Metrics* aggregate) {
  if (source.game_over) return -1;
  const FullEvaluation result = cacheFreeFull(source);
  if (aggregate != nullptr) {
    ++aggregate->calls;
    aggregate->work += result.work;
    aggregate->nodes += result.nodes;
    aggregate->root_actions +=
        static_cast<std::uint64_t>(result.root_actions);
    aggregate->full_root = aggregate->full_root && result.action >= 0;
  }
  return result.action;
}

FullEvaluation cachedFull(const original::ObservableState& source) {
  bool mirrored = false;
  const State canonical =
      cfpi::detail::canonicalState(original::materialize(source), mirrored);
  fair::SearchContext context;
  const fair::RootEvaluation root =
      fair::rootDecision(canonical, original::kContinuationDepth, context);
  FullEvaluation result;
  result.action = mirrored ? kBoardSize - 1 - root.action : root.action;
  result.value = root.value;
  result.nodes = context.nodes;
  result.work = context.work;
  for (int column = 0; column < kBoardSize; ++column) {
    result.root_actions += isLegal(canonical.board, column);
    const int destination = mirrored ? kBoardSize - 1 - column : column;
    result.values[destination] = root.values[column];
    result.expected_scores[destination] = root.expected_scores[column];
  }
  return result;
}

bool sameFull(const FullEvaluation& first, const FullEvaluation& second) {
  return first.action == second.action && first.value == second.value &&
         first.values == second.values &&
         first.expected_scores == second.expected_scores &&
         first.root_actions == second.root_actions;
}

bool reflectedFull(const FullEvaluation& first,
                   const FullEvaluation& reflected) {
  if (reflected.action != kBoardSize - 1 - first.action ||
      reflected.value != first.value ||
      reflected.root_actions != first.root_actions) {
    return false;
  }
  for (int column = 0; column < kBoardSize; ++column) {
    if (reflected.values[kBoardSize - 1 - column] != first.values[column] ||
        reflected.expected_scores[kBoardSize - 1 - column] !=
            first.expected_scores[column]) {
      return false;
    }
  }
  return true;
}

bool reflectedValues(const FullEvaluation& first,
                     const FullEvaluation& reflected) {
  if (reflected.value != first.value ||
      reflected.root_actions != first.root_actions) {
    return false;
  }
  for (int column = 0; column < kBoardSize; ++column) {
    if (reflected.values[kBoardSize - 1 - column] != first.values[column] ||
        reflected.expected_scores[kBoardSize - 1 - column] !=
            first.expected_scores[column]) {
      return false;
    }
  }
  return true;
}

struct AuditCounters {
  std::uint64_t calls = 0;
  std::uint64_t exact_forward = 0;
  std::uint64_t exact_reflections = 0;
  std::uint64_t reflection_equivariant_actions = 0;
  std::uint64_t symmetric_tie_exceptions = 0;
  std::uint64_t reflection_equivariant_values = 0;
  std::uint64_t symmetric_value_exceptions = 0;
  std::uint64_t direct_work = 0;
  std::uint64_t cached_work = 0;
  std::uint64_t direct_nodes = 0;
  std::uint64_t cached_nodes = 0;
  std::uint64_t digest = 0x4346'4432'4155'4449ull;
};

thread_local AuditCounters* active_audit = nullptr;

void updateDigest(AuditCounters& audit,
                  const original::ObservableState& state,
                  const FullEvaluation& value) {
  audit.digest = original::mix64(
      audit.digest ^ original::publicHash(state) ^
      static_cast<std::uint64_t>(value.action + 1));
  for (const double root : value.values) {
    audit.digest = original::mix64(
        audit.digest ^ std::bit_cast<std::uint64_t>(root));
  }
}

int auditedCacheFreeDepthTwoAction(
    const original::ObservableState& source,
    original::D2Metrics* aggregate) {
  if (active_audit == nullptr) {
    throw std::logic_error("cache-free audit evaluator outside audit");
  }
  const FullEvaluation direct = cacheFreeFull(source);
  const FullEvaluation cached = cachedFull(source);
  const original::ObservableState reflected_state = original::mirror(source);
  const FullEvaluation reflected = cacheFreeFull(reflected_state);
  const FullEvaluation reflected_cached = cachedFull(reflected_state);
  ++active_audit->calls;
  if (!sameFull(direct, cached)) {
    throw std::runtime_error("cache-free D2 diverged from cached D2");
  }
  ++active_audit->exact_forward;
  if (!sameFull(reflected, reflected_cached)) {
    throw std::runtime_error(
        "cache-free D2 diverged from cached D2 on reflection");
  }
  ++active_audit->exact_reflections;
  const bool symmetric =
      original::sameObservable(source, reflected_state);
  if (reflectedValues(direct, reflected)) {
    ++active_audit->reflection_equivariant_values;
  } else if (symmetric) {
    ++active_audit->symmetric_value_exceptions;
  } else {
    throw std::runtime_error(
        "asymmetric cache-free D2 reflected values diverged");
  }
  if (reflected.action == kBoardSize - 1 - direct.action) {
    ++active_audit->reflection_equivariant_actions;
  } else if (symmetric) {
    ++active_audit->symmetric_tie_exceptions;
  } else {
    throw std::runtime_error("asymmetric cache-free D2 action did not reflect");
  }
  active_audit->direct_work += direct.work;
  active_audit->cached_work += cached.work;
  active_audit->direct_nodes += direct.nodes;
  active_audit->cached_nodes += cached.nodes;
  updateDigest(*active_audit, source, direct);
  if (aggregate != nullptr) {
    ++aggregate->calls;
    aggregate->work += direct.work;
    aggregate->nodes += direct.nodes;
    aggregate->root_actions +=
        static_cast<std::uint64_t>(direct.root_actions);
    aggregate->full_root = aggregate->full_root && direct.action >= 0;
  }
  return direct.action;
}

struct ReplayResult {
  compressed::GameResult game{};
  std::vector<compressed::TraceRecord> records;
};

ReplayResult replayWith(compressed::D2ActionMemo::Evaluator evaluator,
                        std::string_view label) {
  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(kPilotSeed);
  ReplayResult result;
  result.game.seed = kPilotSeed;
  compressed::D2ActionMemo action_memo(compressed::kActionMemoCapacity,
                                       evaluator);
  while (!state.game_over && state.moves_played < original::kMaximumMoves) {
    compressed::Decision decision =
        compressed::chooseAction(state, action_memo);
    result.game.d4_work += decision.search.work;
    result.game.d4_nodes += decision.search.nodes;
    result.game.d4_cache_hits += decision.search.cache_hits;
    result.game.peak_d4_cache_entries =
        std::max(result.game.peak_d4_cache_entries,
                 decision.search.cache_entries);
    result.game.d4_seconds += decision.d4_seconds;
    result.game.rollout_seconds += decision.rollout_seconds;
    result.game.routed_decisions += decision.routed;
    result.game.switches += decision.switched;
    result.game.passing_alternatives += decision.passing_alternatives;
    result.game.root_q_rejections += decision.root_q_rejections;
    if (decision.routed) {
      compressed::addCompression(result.game.compression,
                                 decision.rollout.metrics);
      result.records.push_back(
          {original::observable(state), std::move(decision)});
    }
    MoveResult move;
    const int action = decision.action;
    if (!playHeadlessMove(state, kPilotSeed, action, move)) {
      throw std::runtime_error("cache-free replay transition failed");
    }
    result.game.maximum_chain =
        std::max(result.game.maximum_chain,
                 static_cast<int>(move.waves.size()));
    for (const Wave& wave : move.waves) {
      result.game.numbered_cleared +=
          static_cast<std::uint64_t>(wave.cleared);
      result.game.covers_revealed +=
          static_cast<std::uint64_t>(wave.revealed);
    }
    if (state.moves_played % 25 == 0) {
      std::cerr << label << " move " << state.moves_played << ", routes "
                << result.game.routed_decisions << ", switches "
                << result.game.switches << '\n';
    }
  }
  result.game.score = state.score;
  result.game.moves = state.moves_played;
  result.game.censored = !state.game_over;
  result.game.elapsed_seconds = std::chrono::duration<double>(
                                    std::chrono::steady_clock::now() - started)
                                    .count();
  result.game.peak_rss_bytes = d4::peakRssBytes();
  if (result.game.score != 404'047 || result.game.moves != 250 ||
      result.game.censored || result.game.numbered_cleared != 569 ||
      result.game.covers_revealed != 329 ||
      result.game.routed_decisions != 179 || result.game.switches != 12 ||
      result.game.passing_alternatives != 15 ||
      result.game.d4_work != 353'804'442 || result.records.size() != 179) {
    throw std::runtime_error("cache-free replay diverged from frozen pilot");
  }
  return result;
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

bool selfTest(std::ostream& output) {
  const bool dependency = compressed::selfTest(output);
  const original::ObservableState fixture = compressed::routedFixture();
  const FullEvaluation cached = cachedFull(fixture);
  const FullEvaluation direct = cacheFreeFull(fixture);
  const FullEvaluation reflected =
      cacheFreeFull(original::mirror(fixture));
  original::D2Metrics metrics;
  const int action = cacheFreeDepthTwoAction(fixture, &metrics);
  State state = original::materialize(fixture);
  compressed::D2ActionMemo memo(compressed::kActionMemoCapacity,
                                &cacheFreeDepthTwoAction);
  const compressed::Decision decision =
      compressed::chooseAction(state, memo, true, 3);
  compressed::D2ActionMemo reference_memo;
  const compressed::Decision reference =
      compressed::chooseAction(state, reference_memo, true, 3);
  const bool exact = sameFull(cached, direct) && action == cached.action &&
                     decision.action == reference.action &&
                     decision.rollout.actions == reference.rollout.actions &&
                     decision.rollout.evaluated ==
                         reference.rollout.evaluated;
  const bool reflection = reflectedFull(direct, reflected);
  const bool resources = metrics.calls == 1 && metrics.work <= 2'485 &&
                         metrics.peak_cache_entries == 0 &&
                         metrics.cache_hits == 0 && metrics.full_root;
  const bool passed = dependency && exact && reflection && resources;
  output << std::setprecision(12)
         << "D4_D2_CACHE_FREE_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"dependency\":" << (dependency ? "true" : "false")
         << ",\"bitExactValuesAndAction\":"
         << (exact ? "true" : "false")
         << ",\"reflectionExact\":"
         << (reflection ? "true" : "false")
         << ",\"fullRootBounded\":"
         << (resources ? "true" : "false")
         << ",\"cachedWork\":" << cached.work
         << ",\"directWork\":" << direct.work << "}\n";
  return passed;
}

void writeAuditArtifact(const Options& options,
                        const ReplayResult& replay,
                        const AuditCounters& audit) {
  std::ofstream output(options.audit_output);
  if (!output) throw std::runtime_error("could not open cache-free audit");
  output << std::setprecision(12)
         << "{\"experiment\":\"d4-d2-rollout-veto-cache-free-audit\""
         << ",\"formalInference\":false,\"sameOpenSeedOnly\":true"
         << ",\"seed\":\"0x3ded0000\",\"freshSeedsOpened\":false"
         << ",\"score\":" << replay.game.score
         << ",\"moves\":" << replay.game.moves
         << ",\"switches\":" << replay.game.switches
         << ",\"routedDecisions\":" << replay.game.routed_decisions
         << ",\"D2CallsAudited\":" << audit.calls
         << ",\"exactForwardRootValuesAndActions\":"
         << audit.exact_forward
         << ",\"exactReflectedRootValuesAndActions\":"
         << audit.exact_reflections
         << ",\"reflectionEquivariantActions\":"
         << audit.reflection_equivariant_actions
         << ",\"symmetricTieExceptions\":"
         << audit.symmetric_tie_exceptions
         << ",\"reflectionEquivariantValues\":"
         << audit.reflection_equivariant_values
         << ",\"symmetricValueExceptions\":"
         << audit.symmetric_value_exceptions
         << ",\"cachedWork\":" << audit.cached_work
         << ",\"directWork\":" << audit.direct_work
         << ",\"cachedNodes\":" << audit.cached_nodes
         << ",\"directNodes\":" << audit.direct_nodes
         << ",\"auditDigest\":" << audit.digest
         << ",\"elapsedSeconds\":" << replay.game.elapsed_seconds
         << ",\"peakRssBytes\":" << replay.game.peak_rss_bytes << "}\n";
}

int auditPilot(const Options& options, std::ostream& report) {
  AuditCounters audit;
  active_audit = &audit;
  ReplayResult replay;
  try {
    replay = replayWith(&auditedCacheFreeDepthTwoAction, "cache-free-audit");
  } catch (...) {
    active_audit = nullptr;
    throw;
  }
  active_audit = nullptr;
  if (audit.calls != replay.game.compression.continuation.calls ||
      audit.exact_forward != audit.calls ||
      audit.exact_reflections != audit.calls) {
    throw std::runtime_error("cache-free audit did not cover every D2 call");
  }
  writeAuditArtifact(options, replay, audit);
  report << std::fixed << std::setprecision(6)
         << "D4_D2_CACHE_FREE_AUDIT {\"calls\":" << audit.calls
         << ",\"exactForward\":" << audit.exact_forward
         << ",\"exactReflections\":" << audit.exact_reflections
         << ",\"seconds\":" << replay.game.elapsed_seconds
         << ",\"artifact\":\"" << options.audit_output << "\"}\n";
  return 0;
}

void writeFinalArtifact(const Options& options,
                        const compressed::GameResult& game,
                        std::size_t trace_records) {
  const double projected =
      std::max(compressed::kFrozenBaselineSeconds, game.elapsed_seconds) *
      original::kFullProtocolProjectionWaves;
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open cache-free artifact");
  output << std::setprecision(12)
         << "{\"experiment\":\"d4-d2-rollout-veto-cache-free\""
         << ",\"formalInference\":false,\"outcomePreserving\":true"
         << ",\"publicStateOnly\":true,\"seed\":\"0x3ded0000\""
         << ",\"uniqueSeedAlreadyOpen\":true,\"freshSeedsOpened\":false"
         << ",\"untouched\":[\"0x3ded0001...003\","
            "\"0x3dee0000...007\",\"0x3ebb...\",\"0x3ebc...\","
            "\"0x7d...\",\"0xd7...\"]"
         << ",\"compressedReference\":{\"path\":\""
         << kCompressedArtifact << "\",\"sha256\":\""
         << kCompressedArtifactSha256 << "\",\"seconds\":"
         << kCompressedSeconds << ",\"D2Calls\":" << kCompressedD2Calls
         << ",\"D2Work\":" << kCompressedD2Work << '}'
         << ",\"implementation\":{\"fullWidth\":true,"
            "\"continuationDepth\":2,\"genericInnerCache\":false,"
            "\"stringKeys\":false,\"LRU\":false,"
            "\"decisionActionMemoRetained\":true,"
            "\"exactSuffixReuseRetained\":true}"
         << ",\"frozenOutcomeParity\":{\"score\":" << game.score
         << ",\"moves\":" << game.moves
         << ",\"numberedCleared\":" << game.numbered_cleared
         << ",\"coversRevealed\":" << game.covers_revealed
         << ",\"routedDecisions\":" << game.routed_decisions
         << ",\"switches\":" << game.switches
         << ",\"passingAlternatives\":"
         << game.passing_alternatives
         << ",\"d4Work\":" << game.d4_work
         << ",\"matchedOriginal\":true}"
         << ",\"pilot\":{\"elapsedSeconds\":" << game.elapsed_seconds
         << ",\"d4Seconds\":" << game.d4_seconds
         << ",\"rolloutSeconds\":" << game.rollout_seconds
         << ",\"traceRecords\":" << trace_records
         << ",\"tracePath\":\"" << options.trace_output
         << "\",\"peakRssBytes\":" << game.peak_rss_bytes
         << ",\"compression\":";
  compressed::writeCompression(output, game.compression);
  output << "},\"runtimeProjection\":{\"firstPairSeconds\":"
         << std::max(compressed::kFrozenBaselineSeconds,
                     game.elapsed_seconds)
         << ",\"waves\":" << original::kFullProtocolProjectionWaves
         << ",\"projectedSeconds\":" << projected
         << ",\"limitSeconds\":"
         << original::kMaximumProjectedWallSeconds
         << ",\"fits\":"
         << (projected <= original::kMaximumProjectedWallSeconds
                 ? "true"
                 : "false")
         << "},\"remainingFittingAuthorized\":false"
         << ",\"approximationMenuStatus\":\"proposed-not-run\""
         << ",\"proposedApproximationMenu\":["
         << "{\"name\":\"h8-s7-d2\",\"horizon\":8,"
            "\"scenarios\":7,\"continuationDepth\":2},"
         << "{\"name\":\"h25-s7-d1\",\"horizon\":25,"
            "\"scenarios\":7,\"continuationDepth\":1}]}\n";
}

int replayPilot(const Options& options, std::ostream& report) {
  ReplayResult replay = replayWith(&cacheFreeDepthTwoAction,
                                   "cache-free-timing");
  std::ofstream trace(options.trace_output);
  if (!trace) throw std::runtime_error("could not open cache-free trace");
  trace << "{\"type\":\"metadata\","
        << "\"experiment\":\"d4-d2-rollout-veto-cache-free\","
        << "\"seed\":" << kPilotSeed
        << ",\"uniqueSeedAlreadyOpen\":true,\"records\":"
        << replay.records.size() << "}\n";
  for (std::size_t index = 0; index < replay.records.size(); ++index) {
    compressed::writeTraceRecord(trace, index, replay.records[index]);
  }
  trace << "{\"type\":\"summary\",\"score\":" << replay.game.score
        << ",\"moves\":" << replay.game.moves
        << ",\"switches\":" << replay.game.switches
        << ",\"elapsedSeconds\":" << replay.game.elapsed_seconds
        << ",\"compression\":";
  compressed::writeCompression(trace, replay.game.compression);
  trace << "}\n";
  trace.close();
  writeFinalArtifact(options, replay.game, replay.records.size());
  const double projected =
      std::max(compressed::kFrozenBaselineSeconds,
               replay.game.elapsed_seconds) *
      original::kFullProtocolProjectionWaves;
  report << std::fixed << std::setprecision(6)
         << "D4_D2_CACHE_FREE_REPLAY {\"score\":" << replay.game.score
         << ",\"moves\":" << replay.game.moves
         << ",\"switches\":" << replay.game.switches
         << ",\"seconds\":" << replay.game.elapsed_seconds
         << ",\"D2Calls\":"
         << replay.game.compression.continuation.calls
         << ",\"D2Work\":" << replay.game.compression.continuation.work
         << ",\"projectedSeconds\":" << projected
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return projected <= original::kMaximumProjectedWallSeconds ? 0 : 3;
}

}  // namespace drop7::d4_d2_rollout_veto_cache_free

#ifndef DROP7_D4_D2_ROLLOUT_VETO_CACHE_FREE_LIBRARY
int main(int argc, char** argv) {
  try {
    using namespace drop7::d4_d2_rollout_veto_cache_free;
    const Options options = parseOptions(argc, argv, 2);
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      return selfTest(std::cout) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--audit-pilot") {
      return auditPilot(options, std::cout);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--replay-pilot") {
      return replayPilot(options, std::cout);
    }
    std::cerr << "usage: drop7_d4_d2_rollout_veto_cache_free "
                 "--self-test | --audit-pilot | --replay-pilot "
                 "[--output PATH] [--audit-output PATH] "
                 "[--trace-output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
#endif
