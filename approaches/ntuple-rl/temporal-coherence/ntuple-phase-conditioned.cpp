#define DROP7_NTUPLE_TC_LIBRARY
#include "ntuple-tc.cpp"

#include <cstdio>

// Adds a zero-initialized, phase-conditioned residual bank to the base n-tuple
// value function.  The residual conditions only local H4, V4, and 2x2
// interactions on moves_remaining.
// This captures the cyclic five-drop rise phase without pretending that Drop7
// has the monotone game stages used by multi-stage 2048 learners.
namespace drop7::ntuple_phase_conditioned {

namespace base = drop7::ntuple_tc;

constexpr int kPhaseCount = kMovesPerLevel;
constexpr int kPhaseTables = base::kAbsolute4Tables;
constexpr int kPhasePatterns = base::kAbsolute4Patterns;
constexpr int kPhaseActiveFeatures = kPhaseTables;
constexpr std::uint32_t kPhaseBase = base::kNodeCount;
constexpr std::uint32_t kPhaseNodeCount =
    kPhaseCount * kPhaseTables * kPhasePatterns;
constexpr std::uint32_t kNodeCount = kPhaseBase + kPhaseNodeCount;
constexpr int kActiveOccurrences =
    base::kActiveFeatures + kPhaseActiveFeatures;
constexpr std::size_t kMemoryLimit = 256ull * 1024 * 1024;

// The evaluation replays previously evaluated development seed ranges only.
constexpr int kTrainingGames = 10'000;
constexpr int kProbeGames = 64;
constexpr std::uint32_t kTrainingSeedStart = 0x3d10'0000u;
constexpr std::uint32_t kTrainingSeedEnd = 0x3d10'270fu;
constexpr std::uint32_t kProbeSeedStart = 0x3d20'0000u;
constexpr std::uint32_t kProbeSeedEnd = 0x3d20'003fu;
constexpr int kMaxMoves = 1'000;
constexpr int kChanceSamples = 7;
constexpr float kLearningRate = 0.1f;
constexpr float kOptimisticValue = 60.0f;
constexpr float kEpsilon = 0.01f;
constexpr float kGamma = 1.0f;
constexpr float kScoreScale = 1'000.0f;
constexpr double kCorrectedTdScore = 66'625.125;
constexpr double kCorrectedTdMoves = 49.469;
constexpr double kQualifiedD4Score = 176'925.25;
constexpr double kQualifiedD4Moves = 116.375;
constexpr double kScoreGate = 100'000.0;
constexpr double kMoveGate = 70.0;
constexpr double kMinimumScoreGain = 20'000.0;
constexpr double kMinimumMoveGain = 12.0;
constexpr const char* kCheckpointPath =
    "/tmp/drop7-ntuple-phase-conditioned-10k.bin";
constexpr const char* kArtifactPath =
    "/tmp/drop7-ntuple-phase-conditioned-audit.json";

static_assert(kTrainingSeedStart + kTrainingGames - 1 == kTrainingSeedEnd);
static_assert(kProbeSeedStart + kProbeGames - 1 == kProbeSeedEnd);
static_assert(kTrainingSeedEnd < kProbeSeedStart);
static_assert(kProbeSeedEnd < 0x3d21'0000u);
static_assert(kPhaseTables == 92);
static_assert(kPhaseNodeCount == 4'600'000u);
static_assert(kNodeCount == 13'921'107u);
static_assert(kActiveOccurrences == 430);
static_assert(static_cast<std::size_t>(kNodeCount) * sizeof(base::Node) ==
              167'053'284ull);
static_assert(static_cast<std::size_t>(kNodeCount) * sizeof(base::Node) <
              kMemoryLimit);

struct PhaseFeatureSet {
  std::array<std::uint32_t, kPhaseActiveFeatures> ids{};
  int count = 0;
};

PhaseFeatureSet phaseFeatures(const base::CompactState& original) {
  const base::CompactState state = base::canonicalize(original);
  const Board& board = state.board;
  const int phase =
      std::clamp<int>(state.moves_remaining, 1, kMovesPerLevel) - 1;
  PhaseFeatureSet result;
  int table = 0;
  const auto add = [&](int pattern) {
    result.ids[result.count++] =
        kPhaseBase +
        static_cast<std::uint32_t>((phase * kPhaseTables + table) *
                                   kPhasePatterns + pattern);
    ++table;
  };

  for (int row = 0; row < kBoardSize; ++row) {
    for (int start = 0; start <= kBoardSize - 4; ++start) {
      add(base::code4(board[indexOf(row, start)],
                      board[indexOf(row, start + 1)],
                      board[indexOf(row, start + 2)],
                      board[indexOf(row, start + 3)]));
    }
  }
  for (int column = 0; column < kBoardSize; ++column) {
    for (int start = 0; start <= kBoardSize - 4; ++start) {
      add(base::code4(board[indexOf(start, column)],
                      board[indexOf(start + 1, column)],
                      board[indexOf(start + 2, column)],
                      board[indexOf(start + 3, column)]));
    }
  }
  for (int row = 0; row < kBoardSize - 1; ++row) {
    for (int column = 0; column < kBoardSize - 1; ++column) {
      add(base::code4(board[indexOf(row, column)],
                      board[indexOf(row, column + 1)],
                      board[indexOf(row + 1, column)],
                      board[indexOf(row + 1, column + 1)]));
    }
  }
  if (table != kPhaseTables || result.count != kPhaseActiveFeatures) {
    throw std::logic_error("phase feature invariant failed");
  }
  return result;
}

struct CombinedGradientAudit {
  int occurrences = 0;
  int unique_parameters = 0;
  int squared_norm = 0;
  int maximum_multiplicity = 0;
  int phase_unique_parameters = 0;
  bool phase_collision_free = false;
};

CombinedGradientAudit combinedGradientAudit(
    const base::CompactState& state) {
  const base::GradientFeatures general =
      base::gradientFeatures(base::features(state));
  const PhaseFeatureSet phase = phaseFeatures(state);
  auto sorted = phase.ids;
  std::sort(sorted.begin(), sorted.end());
  const auto duplicate = std::adjacent_find(sorted.begin(), sorted.end());
  const int phase_unique =
      static_cast<int>(std::unique(sorted.begin(), sorted.end()) -
                       sorted.begin());
  return {general.occurrences + phase.count,
          general.unique_parameters + phase_unique,
          general.squared_norm + phase.count,
          general.maximum_multiplicity,
          phase_unique,
          duplicate == sorted.end()};
}

class Model {
 public:
  explicit Model(float optimistic_value = kOptimisticValue)
      : nodes_(kNodeCount) {
    // Initialize the base prior from optimistic_value and initialize every
    // phase-conditioned residual to zero.
    const float initial = optimistic_value / base::kActiveFeatures;
    for (std::uint32_t id = 0; id < base::kNodeCount; ++id) {
      nodes_[id].weight = initial;
    }
  }

  float value(const base::CompactState& state) const {
    if (state.terminal) return 0;
    const base::FeatureSet general = base::features(state);
    const PhaseFeatureSet phase = phaseFeatures(state);
    return valueFromFeatures(general, phase);
  }

  float value(const State& state) const { return value(base::compact(state)); }

  float update(const base::CompactState& state, float target,
               float learning_rate, base::UpdateStats& stats) {
    const base::FeatureSet general = base::features(state);
    const base::GradientFeatures gradient = base::gradientFeatures(general);
    const PhaseFeatureSet phase = phaseFeatures(state);
    const float prediction = valueFromFeatures(general, phase);
    const float delta = std::clamp(target - prediction, -200.0f, 200.0f);
    const int squared_norm = gradient.squared_norm + phase.count;
    const float normalized =
        learning_rate * delta / static_cast<float>(squared_norm);
    const auto update_node = [&](std::uint32_t id, int multiplicity) {
      base::Node& node = nodes_[id];
      // Calculate beta from history strictly preceding this sample, matching
      // the collision-corrected temporal-coherence implementation.
      const float beta = node.absolute_error > 0
                             ? std::abs(node.signed_error) / node.absolute_error
                             : 1.0f;
      node.weight += beta * normalized * multiplicity;
      const float parameter_error = delta * multiplicity;
      node.signed_error += parameter_error;
      node.absolute_error += std::abs(parameter_error);
      if (node.absolute_error > 1.0e7f) {
        node.signed_error *= 0.5f;
        node.absolute_error *= 0.5f;
      }
      stats.beta_sum += beta;
      ++stats.node_updates;
    };

    for (int index = 0; index < general.count; ++index) {
      const std::uint32_t id = general.ids[index];
      if (id < base::kShared5Base) update_node(id, 1);
    }
    for (int slot = 0; slot < base::kSharedGradientHashSlots; ++slot) {
      const int multiplicity = gradient.multiplicities[slot];
      if (multiplicity > 0) {
        update_node(gradient.shared_ids[slot], multiplicity);
      }
    }
    for (std::uint32_t id : phase.ids) update_node(id, 1);
    stats.absolute_delta_sum += std::abs(delta);
    ++stats.state_updates;
    return delta;
  }

  std::size_t bytes() const { return nodes_.size() * sizeof(base::Node); }

  const base::Node& node(std::uint32_t id) const { return nodes_.at(id); }

  void save(const std::string& path) const {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
      throw std::runtime_error("could not open phase-conditioned checkpoint");
    }
    constexpr std::array<char, 8> magic{{'D', '7', 'P', 'H', 'T', 'C', '1', 0}};
    const std::uint32_t count = kNodeCount;
    output.write(magic.data(), magic.size());
    output.write(reinterpret_cast<const char*>(&count), sizeof(count));
    output.write(reinterpret_cast<const char*>(nodes_.data()),
                 static_cast<std::streamsize>(nodes_.size() *
                                              sizeof(base::Node)));
    if (!output) {
      throw std::runtime_error("failed writing phase-conditioned checkpoint");
    }
  }

  void load(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      throw std::runtime_error("could not open phase-conditioned checkpoint");
    }
    std::array<char, 8> magic{};
    std::uint32_t count = 0;
    input.read(magic.data(), magic.size());
    input.read(reinterpret_cast<char*>(&count), sizeof(count));
    constexpr std::array<char, 8> expected{{'D', '7', 'P', 'H', 'T', 'C', '1', 0}};
    if (magic != expected || count != kNodeCount) {
      throw std::runtime_error("incompatible phase-conditioned checkpoint");
    }
    input.read(reinterpret_cast<char*>(nodes_.data()),
               static_cast<std::streamsize>(nodes_.size() *
                                            sizeof(base::Node)));
    if (!input) {
      throw std::runtime_error("truncated phase-conditioned checkpoint");
    }
  }

 private:
  float valueFromFeatures(const base::FeatureSet& general,
                          const PhaseFeatureSet& phase) const {
    double result = 0;
    for (std::uint32_t id : general.ids) result += nodes_[id].weight;
    for (std::uint32_t id : phase.ids) result += nodes_[id].weight;
    return static_cast<float>(result);
  }

  std::vector<base::Node> nodes_;
};

base::Options frozenOptions() {
  base::Options options;
  options.training_games = kTrainingGames;
  options.probe_games = kProbeGames;
  options.max_moves = kMaxMoves;
  options.chance_samples = kChanceSamples;
  options.report_every = 1'000;
  options.training_seed_start = kTrainingSeedStart;
  options.probe_seed_start = kProbeSeedStart;
  options.gamma = kGamma;
  options.learning_rate = kLearningRate;
  options.optimistic_value = kOptimisticValue;
  options.epsilon = kEpsilon;
  options.td_zero = true;
  options.score_reward = true;
  options.score_scale = kScoreScale;
  options.checkpoint = kCheckpointPath;
  return options;
}

std::array<float, kBoardSize> actionValues(const Model& model,
                                           const State& source,
                                           const base::Options& options) {
  const base::CanonicalState canonical = base::canonicalize(source);
  const State& state = canonical.state;
  const auto chance_seeds = base::stratifiedChanceSeeds(
      base::observableHash(state), options.chance_samples);
  std::array<float, kBoardSize> physical_values{};
  physical_values.fill(-std::numeric_limits<float>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    if (!isLegal(state.board, action)) continue;
    double total = 0;
    for (int sample = 0; sample < options.chance_samples; ++sample) {
      Mulberry32 chance(chance_seeds[sample]);
      MoveResult move;
      if (!playMove(state, action, chance, move)) {
        throw std::logic_error(
            "phase-conditioned evaluator chose an illegal move");
      }
      const double reward = base::transitionReward(move, options);
      const double sample_value =
          reward +
          (move.state.game_over
               ? 0.0
               : static_cast<double>(options.gamma) * model.value(move.state));
      total += sample_value;
      const bool consumed_reveal =
          std::any_of(move.waves.begin(), move.waves.end(),
                      [](const Wave& wave) { return wave.revealed > 0; });
      if (sample == 0 && !consumed_reveal) {
        total = sample_value * options.chance_samples;
        break;
      }
    }
    physical_values[base::physicalAction(action, canonical.mirrored)] =
        static_cast<float>(total / options.chance_samples);
  }
  return physical_values;
}

int greedyAction(const Model& model, const State& state,
                 const base::Options& options) {
  const auto values = actionValues(model, state, options);
  const bool mirrored = base::canonicalize(state).mirrored;
  constexpr std::array<int, kBoardSize> order{{3, 2, 4, 1, 5, 0, 6}};
  int selected_canonical = -1;
  float best = -std::numeric_limits<float>::infinity();
  for (int canonical_column : order) {
    const int physical = base::physicalAction(canonical_column, mirrored);
    if (!isLegal(state.board, physical)) continue;
    if (selected_canonical < 0 || values[physical] > best) {
      selected_canonical = canonical_column;
      best = values[physical];
    }
  }
  return selected_canonical < 0
             ? -1
             : base::physicalAction(selected_canonical, mirrored);
}

base::Evaluation evaluate(const Model& model, const base::Options& options) {
  base::Evaluation result;
  for (int game = 0; game < options.probe_games; ++game) {
    const std::uint32_t seed =
        options.probe_seed_start + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < options.max_moves) {
      const int action = greedyAction(model, state, options);
      MoveResult move;
      if (action < 0 || !playHeadlessMove(state, seed, action, move)) {
        throw std::logic_error(
            "phase-conditioned probe chose an illegal move");
      }
    }
    result.mean_score += state.score;
    result.mean_moves += state.moves_played;
    result.minimum_score = std::min(result.minimum_score, state.score);
    result.maximum_score = std::max(result.maximum_score, state.score);
    result.minimum_moves = std::min(result.minimum_moves, state.moves_played);
    result.maximum_moves = std::max(result.maximum_moves, state.moves_played);
    if (!state.game_over) ++result.censored;
  }
  result.mean_score /= options.probe_games;
  result.mean_moves /= options.probe_games;
  return result;
}

bool passesGate(const base::Evaluation& result) {
  return result.mean_score >= kScoreGate && result.mean_moves >= kMoveGate &&
         result.mean_score - kCorrectedTdScore >= kMinimumScoreGain &&
         result.mean_moves - kCorrectedTdMoves >= kMinimumMoveGain;
}

void printEvaluation(const char* label, const base::Evaluation& result,
                     double elapsed, std::uint64_t transitions,
                     const Model& model) {
  const double score_gap_fraction =
      (result.mean_score - kCorrectedTdScore) /
      (kQualifiedD4Score - kCorrectedTdScore);
  const double move_gap_fraction =
      (result.mean_moves - kCorrectedTdMoves) /
      (kQualifiedD4Moves - kCorrectedTdMoves);
  std::cout << std::fixed << std::setprecision(3)
            << "PHASE_NTUPLE_PROBE {\"label\":\"" << label
            << "\",\"meanScore\":" << result.mean_score
            << ",\"meanMoves\":" << result.mean_moves
            << ",\"minimumScore\":" << result.minimum_score
            << ",\"maximumScore\":" << result.maximum_score
            << ",\"minimumMoves\":" << result.minimum_moves
            << ",\"maximumMoves\":" << result.maximum_moves
            << ",\"censored\":" << result.censored
            << ",\"scoreGapClosed\":" << score_gap_fraction
            << ",\"moveGapClosed\":" << move_gap_fraction
            << ",\"transitions\":" << transitions
            << ",\"transitionsPerSecond\":"
            << (elapsed > 0 ? transitions / elapsed : 0)
            << ",\"parameterMiB\":" << model.bytes() / 1'048'576.0
            << ",\"peakRssMiB\":" << base::peakRssKiB() / 1024.0
            << ",\"passesFrozenGate\":"
            << (passesGate(result) ? "true" : "false") << "}\n";
}

std::uint64_t fileBytes(const std::string& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) return 0;
  return static_cast<std::uint64_t>(input.tellg());
}

void writeArtifact(const base::Evaluation& initial,
                   const base::Evaluation& result,
                   const CombinedGradientAudit& audit,
                   const base::UpdateStats& stats, std::uint64_t transitions,
                   double elapsed, std::uint64_t checkpoint_bytes,
                   long peak_rss_kib) {
  std::ofstream output(kArtifactPath);
  if (!output) throw std::runtime_error("could not open phase audit artifact");
  const bool passed = passesGate(result);
  output << std::fixed << std::setprecision(6)
         << "{\n"
         << "  \"experiment\": \"phase-conditioned-local-residual-10k\",\n"
         << "  \"status\": \"" << (passed ? "passed" : "rejected")
         << "\",\n"
         << "  \"decision\": \""
         << (passed
                 ? "stop at 10k and propose a frozen continuation"
                 : "reject phase-conditioned residual; do not continue")
         << "\",\n"
         << "  \"hypothesisFrozenBeforeReplay\": true,\n"
         << "  \"parameterSweep\": false,\n"
         << "  \"newSeedFamiliesOpened\": false,\n"
         << "  \"architecture\": {\n"
         << "    \"generalNodes\": " << base::kNodeCount << ",\n"
         << "    \"phaseResidualNodes\": " << kPhaseNodeCount << ",\n"
         << "    \"totalNodes\": " << kNodeCount << ",\n"
         << "    \"nodeBytes\": " << sizeof(base::Node) << ",\n"
         << "    \"parameterBytes\": "
         << static_cast<std::uint64_t>(kNodeCount) * sizeof(base::Node)
         << ",\n"
         << "    \"memoryLimitBytes\": " << kMemoryLimit << ",\n"
         << "    \"generalActiveOccurrences\": "
         << base::kActiveFeatures << ",\n"
         << "    \"phaseActiveOccurrences\": "
         << kPhaseActiveFeatures << ",\n"
         << "    \"totalActiveOccurrences\": " << kActiveOccurrences
         << ",\n"
         << "    \"phaseCount\": " << kPhaseCount << ",\n"
         << "    \"phaseTablesPerPhase\": " << kPhaseTables << ",\n"
         << "    \"phasePatternsPerTable\": " << kPhasePatterns << ",\n"
         << "    \"generalInitialization\": \"optimistic 60 / 338\",\n"
         << "    \"phaseResidualInitialization\": \"zero\",\n"
         << "    \"phasePromotion\": false\n"
         << "  },\n"
         << "  \"bellman\": {\n"
         << "    \"valueState\": \"U(board,movesRemaining), before future visible disc\",\n"
         << "    \"actionValue\": \"E_reveal[scoreDelta/1000 + gamma*U(successor)]\",\n"
         << "    \"tdError\": \"scoreDelta/1000 + gamma*U(successor) - U(state)\",\n"
         << "    \"terminalContinuation\": 0,\n"
         << "    \"gamma\": " << kGamma << ",\n"
         << "    \"temporalCoherenceUsesPriorHistory\": true,\n"
         << "    \"sharedGradientUsesMultiplicitySquaredNorm\": true\n"
         << "  },\n"
         << "  \"protocol\": {\n"
         << "    \"trainingGames\": " << kTrainingGames << ",\n"
         << "    \"trainingSeedStart\": \"0x3d100000\",\n"
         << "    \"trainingSeedEnd\": \"0x3d10270f\",\n"
         << "    \"probeGames\": " << kProbeGames << ",\n"
         << "    \"probeSeedStart\": \"0x3d200000\",\n"
         << "    \"probeSeedEnd\": \"0x3d20003f\",\n"
         << "    \"maxMoves\": " << kMaxMoves << ",\n"
         << "    \"chanceSamples\": " << kChanceSamples << ",\n"
         << "    \"learningRate\": " << kLearningRate << ",\n"
         << "    \"epsilon\": " << kEpsilon << ",\n"
         << "    \"scoreScale\": " << kScoreScale << "\n"
         << "  },\n"
         << "  \"activeCountAudit\": {\n"
         << "    \"occurrences\": " << audit.occurrences << ",\n"
         << "    \"uniqueParameters\": " << audit.unique_parameters
         << ",\n"
         << "    \"squaredGradientNorm\": " << audit.squared_norm
         << ",\n"
         << "    \"maximumMultiplicity\": "
         << audit.maximum_multiplicity << ",\n"
         << "    \"phaseUniqueParameters\": "
         << audit.phase_unique_parameters << ",\n"
         << "    \"phaseCollisionFree\": "
         << (audit.phase_collision_free ? "true" : "false") << "\n"
         << "  },\n"
         << "  \"references\": [\n"
         << "    \"https://www.cs.put.poznan.pl/wjaskowski/pub/papers/Szubert2014_2048.pdf\",\n"
         << "    \"https://arxiv.org/abs/1604.05085\"\n"
         << "  ],\n"
         << "  \"gate\": {\n"
         << "    \"correctedTdReferenceScore\": " << kCorrectedTdScore
         << ",\n"
         << "    \"correctedTdReferenceMoves\": " << kCorrectedTdMoves
         << ",\n"
         << "    \"qualifiedD4ReferenceScore\": " << kQualifiedD4Score
         << ",\n"
         << "    \"qualifiedD4ReferenceMoves\": " << kQualifiedD4Moves
         << ",\n"
         << "    \"minimumScore\": " << kScoreGate << ",\n"
         << "    \"minimumMoves\": " << kMoveGate << ",\n"
         << "    \"minimumGainOverCorrectedTdScore\": "
         << kMinimumScoreGain << ",\n"
         << "    \"minimumGainOverCorrectedTdMoves\": "
         << kMinimumMoveGain << "\n"
         << "  },\n"
         << "  \"initialProbe\": {\"meanScore\": " << initial.mean_score
         << ", \"meanMoves\": " << initial.mean_moves << "},\n"
         << "  \"result\": {\n"
         << "    \"meanScore\": " << result.mean_score << ",\n"
         << "    \"meanMoves\": " << result.mean_moves << ",\n"
         << "    \"minimumScore\": " << result.minimum_score << ",\n"
         << "    \"maximumScore\": " << result.maximum_score << ",\n"
         << "    \"minimumMoves\": " << result.minimum_moves << ",\n"
         << "    \"maximumMoves\": " << result.maximum_moves << ",\n"
         << "    \"censored\": " << result.censored << ",\n"
         << "    \"scoreGainOverCorrectedTd\": "
         << result.mean_score - kCorrectedTdScore << ",\n"
         << "    \"moveGainOverCorrectedTd\": "
         << result.mean_moves - kCorrectedTdMoves << ",\n"
         << "    \"scoreGapClosed\": "
         << (result.mean_score - kCorrectedTdScore) /
                (kQualifiedD4Score - kCorrectedTdScore)
         << ",\n"
         << "    \"moveGapClosed\": "
         << (result.mean_moves - kCorrectedTdMoves) /
                (kQualifiedD4Moves - kCorrectedTdMoves)
         << ",\n"
         << "    \"passesFrozenGate\": "
         << (passed ? "true" : "false") << "\n"
         << "  },\n"
         << "  \"training\": {\n"
         << "    \"transitions\": " << transitions << ",\n"
         << "    \"stateUpdates\": " << stats.state_updates << ",\n"
         << "    \"nodeUpdates\": " << stats.node_updates << ",\n"
         << "    \"meanAbsoluteClippedTdError\": "
         << (stats.state_updates
                 ? stats.absolute_delta_sum / stats.state_updates
                 : 0)
         << ",\n"
         << "    \"meanTemporalCoherenceBeta\": "
         << (stats.node_updates ? stats.beta_sum / stats.node_updates : 0)
         << ",\n"
         << "    \"elapsedSeconds\": " << elapsed << "\n"
         << "  },\n"
         << "  \"resources\": {\n"
         << "    \"peakRssBytes\": "
         << static_cast<std::uint64_t>(peak_rss_kib) * 1024 << ",\n"
         << "    \"checkpointBytes\": " << checkpoint_bytes << "\n"
         << "  },\n"
         << "  \"continuationExecuted\": false\n"
         << "}\n";
  if (!output) throw std::runtime_error("failed writing phase audit artifact");
}

bool exhaustivePhaseIndexTest() {
  std::uint64_t visited = 0;
  for (int phase = 0; phase < kPhaseCount; ++phase) {
    for (int table = 0; table < kPhaseTables; ++table) {
      for (int pattern = 0; pattern < kPhasePatterns; ++pattern) {
        const std::uint32_t id =
            kPhaseBase +
            static_cast<std::uint32_t>((phase * kPhaseTables + table) *
                                       kPhasePatterns + pattern);
        if (id < kPhaseBase || id >= kNodeCount) return false;
        ++visited;
      }
    }
  }
  return visited == kPhaseNodeCount;
}

bool selfTest(std::ostream& output) {
  const bool base_passed = base::selfTest(output);
  const bool exhaustive_indices = exhaustivePhaseIndexTest();
  const base::Options options = frozenOptions();
  State state = initialHeadlessState(0x2d70'0042u);
  for (int action : {3, 1, 5, 2, 4, 0}) {
    MoveResult move;
    if (!playHeadlessMove(state, 0x2d70'0042u, action, move)) break;
  }
  State mirrored = state;
  mirrored.board = base::mirrorBoard(state.board);
  Model model;
  const float value = model.value(state);
  const float mirror_value = model.value(mirrored);
  const bool mirror_value_ok = std::abs(value - mirror_value) < 1.0e-6f;
  const auto action_values = actionValues(model, state, options);
  const auto mirror_values = actionValues(model, mirrored, options);
  const auto repeated = actionValues(model, state, options);
  bool action_mirror = true;
  for (int column = 0; column < kBoardSize; ++column) {
    const float left = action_values[column];
    const float right = mirror_values[kBoardSize - 1 - column];
    if (std::isfinite(left) != std::isfinite(right) ||
        (std::isfinite(left) && std::abs(left - right) > 1.0e-5f)) {
      action_mirror = false;
    }
  }
  const bool deterministic = action_values == repeated;
  const bool mapped_tie = greedyAction(model, state, options) ==
                          kBoardSize - 1 -
                              greedyAction(model, mirrored, options);
  State other_disc = state;
  other_disc.next_disc = static_cast<std::uint8_t>(state.next_disc % 7 + 1);
  const bool disc_independent = model.value(state) == model.value(other_disc);

  base::CompactState phase_one = base::compact(state);
  phase_one.moves_remaining = 1;
  base::CompactState phase_two = phase_one;
  phase_two.moves_remaining = 2;
  const auto features_one = phaseFeatures(phase_one);
  const auto features_two = phaseFeatures(phase_two);
  bool phase_offset = true;
  for (int index = 0; index < kPhaseActiveFeatures; ++index) {
    phase_offset &= features_two.ids[index] - features_one.ids[index] ==
                    static_cast<std::uint32_t>(kPhaseTables * kPhasePatterns);
  }
  const CombinedGradientAudit audit = combinedGradientAudit(phase_one);
  const bool active_counts = audit.occurrences == kActiveOccurrences &&
                             audit.phase_unique_parameters ==
                                 kPhaseActiveFeatures &&
                             audit.unique_parameters >
                                 base::kNonSharedActiveFeatures &&
                             audit.squared_norm >= kActiveOccurrences &&
                             audit.phase_collision_free;
  const float expected_general =
      kOptimisticValue / static_cast<float>(base::kActiveFeatures);
  const bool initialization =
      std::abs(model.node(0).weight - expected_general) < 1.0e-7f &&
      model.node(kPhaseBase).weight == 0.0f &&
      std::abs(value - kOptimisticValue) < 1.0e-4f;
  base::CompactState terminal = phase_one;
  terminal.terminal = 1;
  const bool terminal_zero = model.value(terminal) == 0.0f;
  const bool memory_bounded = model.bytes() < kMemoryLimit;

  base::UpdateStats first_stats;
  base::UpdateStats second_stats;
  base::UpdateStats third_stats;
  const float before = model.value(phase_one);
  model.update(phase_one, before + 10.0f, 0.1f, first_stats);
  const float after_positive = model.value(phase_one);
  model.update(phase_one, after_positive - 10.0f, 0.1f, second_stats);
  const float after_reversal = model.value(phase_one);
  model.update(phase_one, after_reversal - 10.0f, 0.1f, third_stats);
  const float after_cancelled = model.value(phase_one);
  const double first_beta = first_stats.beta_sum / first_stats.node_updates;
  const double second_beta = second_stats.beta_sum / second_stats.node_updates;
  const double third_beta = third_stats.beta_sum / third_stats.node_updates;
  const bool normalized_gradient =
      std::abs((after_positive - before) - 1.0f) < 2.0e-3f &&
      std::abs((after_reversal - after_positive) + 1.0f) < 2.0e-3f &&
      after_cancelled == after_reversal &&
      first_stats.node_updates ==
          static_cast<std::uint64_t>(audit.unique_parameters) &&
      first_beta == 1.0 && second_beta == 1.0 && third_beta == 0.0;

  base::Evaluation pass_fixture;
  pass_fixture.mean_score = kScoreGate;
  pass_fixture.mean_moves = kMoveGate;
  base::Evaluation score_fail = pass_fixture;
  score_fail.mean_score = kScoreGate - 0.001;
  base::Evaluation move_fail = pass_fixture;
  move_fail.mean_moves = kMoveGate - 0.001;
  const bool gate_wiring = passesGate(pass_fixture) &&
                           !passesGate(score_fail) &&
                           !passesGate(move_fail);
  const bool seed_locks =
      options.training_seed_start == kTrainingSeedStart &&
      options.probe_seed_start == kProbeSeedStart &&
      options.training_games == kTrainingGames &&
      options.probe_games == kProbeGames;
  const bool passed =
      base_passed && exhaustive_indices && mirror_value_ok && action_mirror &&
      deterministic && mapped_tie && disc_independent && phase_offset &&
      active_counts && initialization && terminal_zero && memory_bounded &&
      normalized_gradient && gate_wiring && seed_locks;
  output << std::fixed << std::setprecision(6)
         << "PHASE_NTUPLE_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"basePassed\":" << (base_passed ? "true" : "false")
         << ",\"exhaustivePhaseIndices\":"
         << (exhaustive_indices ? "true" : "false")
         << ",\"mirrorValue\":" << (mirror_value_ok ? "true" : "false")
         << ",\"mirrorActions\":" << (action_mirror ? "true" : "false")
         << ",\"deterministic\":" << (deterministic ? "true" : "false")
         << ",\"mappedTieBehavior\":" << (mapped_tie ? "true" : "false")
         << ",\"discIndependent\":"
         << (disc_independent ? "true" : "false")
         << ",\"phaseOffset\":" << (phase_offset ? "true" : "false")
         << ",\"phaseCollisionFree\":"
         << (audit.phase_collision_free ? "true" : "false")
         << ",\"activeCountAudit\":"
         << (active_counts ? "true" : "false")
         << ",\"zeroResidualInitialization\":"
         << (initialization ? "true" : "false")
         << ",\"terminalZero\":" << (terminal_zero ? "true" : "false")
         << ",\"memoryBounded\":"
         << (memory_bounded ? "true" : "false")
         << ",\"normalizedGradient\":"
         << (normalized_gradient ? "true" : "false")
         << ",\"gateWiring\":" << (gate_wiring ? "true" : "false")
         << ",\"seedLocks\":" << (seed_locks ? "true" : "false")
         << ",\"activeOccurrences\":" << audit.occurrences
         << ",\"uniqueParameters\":" << audit.unique_parameters
         << ",\"squaredGradientNorm\":" << audit.squared_norm
         << ",\"maximumMultiplicity\":" << audit.maximum_multiplicity
         << ",\"phaseUniqueParameters\":"
         << audit.phase_unique_parameters << ",\"nodes\":" << kNodeCount
         << ",\"parameterMiB\":" << model.bytes() / 1'048'576.0
         << "}\n";
  return passed;
}

int run() {
  const base::Options options = frozenOptions();
  Model model(options.optimistic_value);
  const base::CompactState initial_state =
      base::compact(initialHeadlessState(kTrainingSeedStart));
  const CombinedGradientAudit audit = combinedGradientAudit(initial_state);
  if (audit.occurrences != kActiveOccurrences ||
      audit.phase_unique_parameters != kPhaseActiveFeatures ||
      !audit.phase_collision_free || model.bytes() >= kMemoryLimit) {
    throw std::logic_error("frozen phase architecture audit failed");
  }

  std::cout << "PHASE_NTUPLE_CONFIG {\"trainingSeedStart\":"
            << options.training_seed_start << ",\"trainingSeedEnd\":"
            << kTrainingSeedEnd << ",\"probeSeedStart\":"
            << options.probe_seed_start << ",\"probeSeedEnd\":"
            << kProbeSeedEnd << ",\"trainingGames\":"
            << options.training_games << ",\"probeGames\":"
            << options.probe_games << ",\"maxMoves\":" << options.max_moves
            << ",\"chanceSamples\":" << options.chance_samples
            << ",\"learningRate\":" << options.learning_rate
            << ",\"optimisticValue\":" << options.optimistic_value
            << ",\"phaseResidualInitialization\":0,\"epsilon\":"
            << options.epsilon
            << ",\"reward\":\"score-delta/1000\",\"nodes\":"
            << kNodeCount << ",\"activeOccurrences\":"
            << kActiveOccurrences << ",\"parameterBytes\":"
            << model.bytes()
            << ",\"frozenScoreGate\":" << kScoreGate
            << ",\"frozenMoveGate\":" << kMoveGate
            << ",\"parameterSweep\":false,\"newSeedFamilies\":false}\n";

  const base::Evaluation initial = evaluate(model, options);
  printEvaluation("initial", initial, 0, 0, model);
  base::UpdateStats stats;
  std::uint64_t transitions = 0;
  const auto started = std::chrono::steady_clock::now();
  for (int game = 0; game < options.training_games; ++game) {
    const std::uint32_t seed =
        options.training_seed_start + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    base::Rng exploration(mix32(seed ^ 0x4558'504cu));
    while (!state.game_over && state.moves_played < options.max_moves) {
      const base::CompactState previous = base::compact(state);
      int action = greedyAction(model, state, options);
      if (exploration.unit() < options.epsilon) {
        int legal_count = 0;
        const auto legal = legalColumns(state.board, legal_count);
        action = legal[exploration.bounded(legal_count)];
      }
      MoveResult move;
      if (action < 0 || !playHeadlessMove(state, seed, action, move)) {
        throw std::logic_error(
            "phase-conditioned training chose an illegal move");
      }
      ++transitions;
      const float reward = base::transitionReward(move, options);
      const float target =
          reward +
          (state.game_over
               ? 0.0f
               : options.gamma * model.value(base::compact(state)));
      model.update(previous, target, options.learning_rate, stats);
    }
    const int completed = game + 1;
    if (completed % options.report_every == 0 ||
        completed == options.training_games) {
      const double elapsed = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
      std::cout << std::fixed << std::setprecision(6)
                << "PHASE_NTUPLE_TRAIN {\"trainingGames\":" << completed
                << ",\"transitions\":" << transitions
                << ",\"meanAbsDelta\":"
                << (stats.state_updates
                        ? stats.absolute_delta_sum / stats.state_updates
                        : 0)
                << ",\"meanTcBeta\":"
                << (stats.node_updates ? stats.beta_sum / stats.node_updates
                                       : 0)
                << ",\"transitionsPerSecond\":"
                << (elapsed > 0 ? transitions / elapsed : 0)
                << ",\"peakRssMiB\":" << base::peakRssKiB() / 1024.0
                << "}\n";
    }
  }
  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();
  const base::Evaluation result = evaluate(model, options);
  printEvaluation("trained-10k", result, elapsed, transitions, model);
  model.save(kCheckpointPath);
  const std::uint64_t checkpoint_bytes = fileBytes(kCheckpointPath);
  const std::uint64_t expected_checkpoint_bytes =
      8 + sizeof(std::uint32_t) + model.bytes();
  if (checkpoint_bytes != expected_checkpoint_bytes) {
    throw std::runtime_error("phase-conditioned checkpoint size mismatch");
  }
  const long peak_rss_kib = base::peakRssKiB();
  writeArtifact(initial, result, audit, stats, transitions, elapsed,
                checkpoint_bytes, peak_rss_kib);
  std::cout << "PHASE_NTUPLE_DECISION {\"passesFrozenGate\":"
            << (passesGate(result) ? "true" : "false")
            << ",\"decision\":\""
            << (passesGate(result)
                    ? "stop-and-propose-frozen-continuation"
                    : "reject-and-do-not-continue")
            << "\",\"checkpoint\":\"" << kCheckpointPath
            << "\",\"artifact\":\"" << kArtifactPath << "\"}\n";
  return 0;
}

}  // namespace drop7::ntuple_phase_conditioned

#ifndef DROP7_NTUPLE_PHASE_CONDITIONED_LIBRARY
int main(int argc, char** argv) {
  try {
    std::cout.setf(std::ios::unitbuf);
    if (argc != 2) {
      std::cerr << "usage: drop7_ntuple_phase_conditioned --self-test | --run\n";
      return 2;
    }
    const std::string mode = argv[1];
    if (mode == "--self-test") {
      return drop7::ntuple_phase_conditioned::selfTest(std::cout) ? 0 : 1;
    }
    if (mode == "--run") {
      return drop7::ntuple_phase_conditioned::run();
    }
    throw std::invalid_argument("unknown mode: " + mode);
  } catch (const std::exception& error) {
    std::cerr << "drop7_ntuple_phase_conditioned: " << error.what() << '\n';
    return 1;
  }
}
#endif
