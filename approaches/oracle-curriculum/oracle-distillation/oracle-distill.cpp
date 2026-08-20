#define main drop7_phase_student_embedded_main
#include "../../value-policy-learning/phase-distillation/phase-student.cpp"
#undef main

#include <sys/resource.h>

#include <chrono>
#include <fstream>
#include <unordered_map>

// Leakage-free policy distillation from the deliberately privileged future
// oracle in perfect-information-oracle/main.ts.  Only the teacher functions
// below receive a game seed.  Every example and student inference crosses the
// publicState() boundary, which retains only board, visible disc, and rise
// phase.  Protected probe/validation/final seed families are rejected.
namespace drop7::oracle_distill {

using Clock = std::chrono::steady_clock;
using phase_student::Example;
using phase_student::Network;
using phase_student::PolicyMetrics;

constexpr std::uint32_t kTrainingPartitionStart = 0x3d00'0000u;
constexpr std::uint32_t kTrainingPartitionEnd = 0x3e00'0000u;
constexpr std::uint32_t kOracleTrainStart = 0x3d7a'0000u;
constexpr std::uint32_t kBehaviorTrainStart = 0x3d7b'0000u;
constexpr std::uint32_t kOracleHoldoutStart = 0x3d7c'0000u;
constexpr std::uint32_t kBehaviorHoldoutStart = 0x3d7d'0000u;
constexpr std::uint32_t kScreenStart = 0x3d7e'0000u;
constexpr std::uint32_t kConfirmationStart = 0x3d7f'0000u;

constexpr int kOracleDepth = 4;
constexpr int kOracleBeam = 128;
constexpr double kTerminalPenalty = -1'000'000'000.0;
constexpr float kTeacherMass = 0.92f;

// These gates and the conservative deployment rule are fixed before the
// whole-game holdout or fresh policy screen is evaluated.
constexpr double kMinimumHoldoutTop1 = 0.30;
constexpr double kMinimumHoldoutTop2 = 0.55;
constexpr double kMaximumHoldoutCrossEntropy = 1.75;
constexpr float kMinimumStudentProbability = 0.40f;
constexpr float kMinimumProbabilityAdvantage = 0.12f;
constexpr double kMaximumExactQRangeLoss = 0.10;
constexpr double kMinimumExactQTolerance = 5'000.0;

constexpr std::array<int, kBoardSize> kCenterFirst{{3, 2, 4, 1, 5, 0, 6}};

enum class RollIn { kOracle, kBehavior };

struct Config {
  int oracle_train_games = 12;
  int behavior_train_games = 32;
  int oracle_holdout_games = 4;
  int behavior_holdout_games = 12;
  int label_moves = 200;
  int epochs = 30;
  int batch_size = 64;
  float learning_rate = 0.001f;
  int screen_games = 8;
  int confirmation_games = 16;
  int evaluation_moves = 500;
  std::string output = "/tmp/drop7-oracle-distill.json";
  std::string model = "/tmp/drop7-oracle-distill.bin";
};

struct OracleStats {
  std::uint64_t generated = 0;
  std::uint64_t deduplicated = 0;
  std::size_t peak_candidates = 0;
};

struct OraclePlan {
  int column = -1;
  OracleStats stats{};
};

struct BeamNode {
  State state{};
  int first_column = -1;
  std::string dynamic_key;
  double rank = -std::numeric_limits<double>::infinity();
};

struct Corpus {
  std::vector<Example> examples;
  std::uint64_t oracle_generated = 0;
  std::uint64_t oracle_deduplicated = 0;
  std::int64_t rollin_score = 0;
  int rollin_moves = 0;
  int games = 0;
  int censored = 0;
};

struct GameOutcome {
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  int switches = 0;
};

struct Summary {
  double baseline_mean_score = 0;
  double baseline_mean_moves = 0;
  double mean_score = 0;
  double mean_moves = 0;
  double score_delta = 0;
  double move_delta = 0;
  double score_lower95 = 0;
  double move_lower95 = 0;
  double switches_per_move = 0;
  int censored = 0;
};

State publicState(const State& source) {
  State result;
  result.board = source.board;
  result.next_disc = source.next_disc;
  result.moves_remaining = source.moves_remaining;
  // Score, level, move count, terminal metadata, seed, and all future random
  // values are deliberately absent from the student's effective input.
  result.score = 0;
  result.level = 1;
  result.moves_played = 0;
  result.game_over = false;
  return result;
}

void validateTrainingRange(std::uint32_t start, int games) {
  if (games < 1) throw std::invalid_argument("game count must be positive");
  const std::uint64_t end =
      static_cast<std::uint64_t>(start) + static_cast<std::uint64_t>(games);
  if (start < kTrainingPartitionStart || end > kTrainingPartitionEnd) {
    throw std::invalid_argument("seed range leaves the 0x3d training family");
  }
}

double originalCombinedUtility(const State& state) {
  if (state.game_over) return -250'000.0;
  const cfpi::detail::PhaseFeatures f =
      cfpi::detail::extractPhaseFeatures(state);
  // Implements the reference TypeScript `combined` leaf without phase-safety
  // additions.  The feature extractor uses one downward exposure edge.
  return 180.0 * f.open_columns - 10.0 * f.height_load -
         620.0 * f.solid_cells - 220.0 * f.cracked_cells -
         18.0 * f.numbered_cells - 90.0 * f.high_low_numbers +
         140.0 * f.direct_potential + 360.0 * f.latent_chain_potential +
         100.0 * f.cracked_exposure + 40.0 * f.solid_exposure -
         550.0 * f.adjacent_ones - 750.0 * f.triple_twos -
         120.0 * f.dead_low_numbers;
}

double rankState(const State& state) {
  return static_cast<double>(state.score) + originalCombinedUtility(state) +
         (state.game_over ? kTerminalPenalty : 0.0);
}

std::string dynamicKey(const State& state) {
  std::string key = serializeBoard(state.board);
  key.push_back('|');
  key += std::to_string(state.next_disc);
  key.push_back('|');
  key += std::to_string(state.level);
  key.push_back('|');
  key += std::to_string(state.moves_remaining);
  key.push_back('|');
  key += std::to_string(state.moves_played);
  key.push_back('|');
  key.push_back(state.game_over ? '1' : '0');
  return key;
}

bool betterBeamNode(const BeamNode& left, const BeamNode& right) {
  if (left.rank != right.rank) return left.rank > right.rank;
  if (left.state.score != right.state.score) {
    return left.state.score > right.state.score;
  }
  if (left.first_column != right.first_column) {
    return left.first_column < right.first_column;
  }
  return left.dynamic_key < right.dynamic_key;
}

bool dominatesEquivalent(const BeamNode& candidate, const BeamNode& prior) {
  if (candidate.state.score != prior.state.score) {
    return candidate.state.score > prior.state.score;
  }
  return candidate.first_column < prior.first_column;
}

void insertCandidate(std::unordered_map<std::string, BeamNode>& candidates,
                     BeamNode candidate, OracleStats& stats) {
  const auto found = candidates.find(candidate.dynamic_key);
  if (found == candidates.end()) {
    candidates.emplace(candidate.dynamic_key, std::move(candidate));
    return;
  }
  ++stats.deduplicated;
  if (dominatesEquivalent(candidate, found->second)) {
    found->second = std::move(candidate);
  }
}

OraclePlan planOracleMove(const State& root, std::uint32_t game_seed,
                          int depth = kOracleDepth,
                          int beam_width = kOracleBeam) {
  if (root.game_over) return {};
  if (depth < 1 || depth > 12 || beam_width < 1 || beam_width > 2'048) {
    throw std::invalid_argument("oracle work bounds are invalid");
  }
  BeamNode initial{root, -1, dynamicKey(root), rankState(root)};
  std::vector<BeamNode> beam{std::move(initial)};
  OracleStats stats;
  for (int ply = 0; ply < depth; ++ply) {
    std::unordered_map<std::string, BeamNode> candidates;
    candidates.reserve(static_cast<std::size_t>(beam_width * kBoardSize));
    for (const BeamNode& node : beam) {
      if (node.state.game_over) {
        insertCandidate(candidates, node, stats);
        continue;
      }
      int legal_count = 0;
      const auto legal = legalColumns(node.state.board, legal_count);
      for (int offset = 0; offset < legal_count; ++offset) {
        const int column = legal[offset];
        State next = node.state;
        MoveResult move;
        if (!playHeadlessMove(next, game_seed, column, move)) continue;
        ++stats.generated;
        BeamNode candidate;
        candidate.state = std::move(next);
        candidate.first_column =
            node.first_column < 0 ? column : node.first_column;
        candidate.dynamic_key = dynamicKey(candidate.state);
        insertCandidate(candidates, std::move(candidate), stats);
      }
    }
    if (candidates.empty()) break;
    stats.peak_candidates =
        std::max(stats.peak_candidates, candidates.size());
    std::vector<BeamNode> ranked;
    ranked.reserve(candidates.size());
    for (auto& entry : candidates) {
      entry.second.rank = rankState(entry.second.state);
      ranked.push_back(std::move(entry.second));
    }
    std::sort(ranked.begin(), ranked.end(), betterBeamNode);
    if (static_cast<int>(ranked.size()) > beam_width) {
      ranked.resize(static_cast<std::size_t>(beam_width));
    }
    beam = std::move(ranked);
  }
  std::sort(beam.begin(), beam.end(), betterBeamNode);
  for (const BeamNode& node : beam) {
    if (node.first_column >= 0) return {node.first_column, stats};
  }
  return {-1, stats};
}

Example oracleExample(const State& source, int teacher_column) {
  const phase_student::CanonicalState canonical =
      phase_student::canonicalize(publicState(source));
  const int label = canonical.mirrored
                        ? kBoardSize - 1 - teacher_column
                        : teacher_column;
  if (!isLegal(canonical.state.board, label)) {
    throw std::runtime_error("canonical oracle label is illegal");
  }
  std::array<float, kBoardSize> targets{};
  int legal_count = 0;
  legalColumns(canonical.state.board, legal_count);
  const float background = (1.0f - kTeacherMass) / legal_count;
  for (int column = 0; column < kBoardSize; ++column) {
    if (isLegal(canonical.state.board, column)) targets[column] = background;
  }
  targets[label] += kTeacherMass;
  return {canonical.state, targets, label, kOracleDepth};
}

cfpi::BehaviorOptions behaviorOptions() {
  cfpi::BehaviorOptions options;
  options.max_depth = 3;
  options.chance_samples = 5;
  options.max_work = 1'000'000;
  options.max_cache_entries = 40'000;
  options.terminal_utility = -1'000'000.0;
  return options;
}

Corpus collectCorpus(std::uint32_t seed_start, int games, int maximum_moves,
                     RollIn roll_in, std::string_view label) {
  validateTrainingRange(seed_start, games);
  Corpus result;
  result.games = games;
  result.examples.reserve(static_cast<std::size_t>(games * maximum_moves));
  const cfpi::BehaviorOptions behavior = behaviorOptions();
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed = seed_start + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    const std::size_t before = result.examples.size();
    while (!state.game_over && state.moves_played < maximum_moves) {
      const OraclePlan oracle = planOracleMove(state, seed);
      if (oracle.column < 0 || !isLegal(state.board, oracle.column)) {
        throw std::runtime_error("privileged oracle returned no legal move");
      }
      result.oracle_generated += oracle.stats.generated;
      result.oracle_deduplicated += oracle.stats.deduplicated;
      result.examples.push_back(oracleExample(state, oracle.column));
      const int action = roll_in == RollIn::kOracle
                             ? oracle.column
                             : cfpi::chooseBehaviorAction(state, behavior);
      MoveResult move;
      if (!playHeadlessMove(state, seed, action, move)) {
        throw std::runtime_error("roll-in transition rejected a legal action");
      }
    }
    result.rollin_score += state.score;
    result.rollin_moves += state.moves_played;
    result.censored += !state.game_over;
    std::cerr << label << ' ' << (game + 1) << '/' << games << " seed 0x"
              << std::hex << seed << std::dec << " labels "
              << result.examples.size() - before << " score " << state.score
              << " moves " << state.moves_played << '\n';
  }
  return result;
}

void appendCorpus(Corpus& target, Corpus source) {
  target.oracle_generated += source.oracle_generated;
  target.oracle_deduplicated += source.oracle_deduplicated;
  target.rollin_score += source.rollin_score;
  target.rollin_moves += source.rollin_moves;
  target.games += source.games;
  target.censored += source.censored;
  target.examples.insert(target.examples.end(),
                         std::make_move_iterator(source.examples.begin()),
                         std::make_move_iterator(source.examples.end()));
}

int chooseStudentAction(const Network& network, const State& state) {
  return network.chooseAction(publicState(state));
}

struct HybridDecision {
  int action = -1;
  int behavior_action = -1;
  bool switched = false;
};

HybridDecision chooseHybridAction(const Network& network, const State& source,
                                  const cfpi::BehaviorOptions& behavior) {
  const phase_student::TeacherLabel exact =
      phase_student::queryTeacher(publicState(source), behavior);
  const bool mirrored = phase_student::mirrorIsSmaller(source.board);
  const int behavior_actual = mirrored
                                  ? kBoardSize - 1 - exact.canonical_action
                                  : exact.canonical_action;
  const State observable = publicState(source);
  const phase_student::CanonicalState canonical =
      phase_student::canonicalize(observable);
  const auto logits = network.logitsCanonical(canonical.state);
  const auto probabilities =
      phase_student::legalProbabilities(canonical.state, logits);
  int student = -1;
  float best_probability = -1.0f;
  for (const int column : kCenterFirst) {
    if (!isLegal(canonical.state.board, column)) continue;
    if (probabilities[column] > best_probability) {
      best_probability = probabilities[column];
      student = column;
    }
  }
  int chosen = exact.canonical_action;
  if (student >= 0 && student != exact.canonical_action &&
      std::isfinite(exact.values[student]) &&
      std::isfinite(exact.values[exact.canonical_action])) {
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (int column = 0; column < kBoardSize; ++column) {
      if (!isLegal(canonical.state.board, column) ||
          !std::isfinite(exact.values[column])) {
        continue;
      }
      minimum = std::min(minimum, exact.values[column]);
      maximum = std::max(maximum, exact.values[column]);
    }
    const double tolerance = std::max(
        kMinimumExactQTolerance,
        kMaximumExactQRangeLoss * std::max(0.0, maximum - minimum));
    const float probability_advantage =
        probabilities[student] - probabilities[exact.canonical_action];
    if (probabilities[student] >= kMinimumStudentProbability &&
        probability_advantage >= kMinimumProbabilityAdvantage &&
        exact.values[student] >=
            exact.values[exact.canonical_action] - tolerance) {
      chosen = student;
    }
  }
  const int actual = mirrored ? kBoardSize - 1 - chosen : chosen;
  return {actual, behavior_actual, actual != behavior_actual};
}

GameOutcome playPolicyGame(std::uint32_t seed, int maximum_moves,
                           const Network* network, bool hybrid) {
  const cfpi::BehaviorOptions behavior = behaviorOptions();
  State state = initialHeadlessState(seed);
  int switches = 0;
  while (!state.game_over && state.moves_played < maximum_moves) {
    int action = -1;
    if (network == nullptr) {
      action = cfpi::chooseBehaviorAction(state, behavior);
    } else if (hybrid) {
      const HybridDecision decision =
          chooseHybridAction(*network, state, behavior);
      action = decision.action;
      switches += decision.switched;
    } else {
      action = chooseStudentAction(*network, state);
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("evaluated policy selected an illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("evaluated policy transition failed");
    }
  }
  return {state.score, state.moves_played, !state.game_over, switches};
}

double lower95(const std::vector<double>& values) {
  if (values.empty()) return 0;
  const double mean =
      std::accumulate(values.begin(), values.end(), 0.0) / values.size();
  if (values.size() < 2) return mean;
  double squared = 0;
  for (const double value : values) squared += (value - mean) * (value - mean);
  const double deviation =
      std::sqrt(squared / static_cast<double>(values.size() - 1));
  return mean - 1.96 * deviation / std::sqrt(static_cast<double>(values.size()));
}

Summary comparePolicies(const Network& network, std::uint32_t seed_start,
                        int games, int maximum_moves,
                        std::string_view label) {
  validateTrainingRange(seed_start, games);
  Summary summary;
  std::vector<double> score_deltas;
  std::vector<double> move_deltas;
  int total_switches = 0;
  int candidate_moves = 0;
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed = seed_start + static_cast<std::uint32_t>(game);
    const GameOutcome baseline =
        playPolicyGame(seed, maximum_moves, nullptr, false);
    const GameOutcome candidate =
        playPolicyGame(seed, maximum_moves, &network, true);
    summary.baseline_mean_score +=
        static_cast<double>(baseline.score) / games;
    summary.baseline_mean_moves +=
        static_cast<double>(baseline.moves) / games;
    summary.mean_score += static_cast<double>(candidate.score) / games;
    summary.mean_moves += static_cast<double>(candidate.moves) / games;
    score_deltas.push_back(static_cast<double>(candidate.score - baseline.score));
    move_deltas.push_back(static_cast<double>(candidate.moves - baseline.moves));
    total_switches += candidate.switches;
    candidate_moves += candidate.moves;
    summary.censored += candidate.censored;
    std::cerr << label << ' ' << (game + 1) << '/' << games << " seed 0x"
              << std::hex << seed << std::dec << " baseline "
              << baseline.score << '/' << baseline.moves << " hybrid "
              << candidate.score << '/' << candidate.moves << " switches "
              << candidate.switches << '\n';
  }
  summary.score_delta =
      std::accumulate(score_deltas.begin(), score_deltas.end(), 0.0) / games;
  summary.move_delta =
      std::accumulate(move_deltas.begin(), move_deltas.end(), 0.0) / games;
  summary.score_lower95 = lower95(score_deltas);
  summary.move_lower95 = lower95(move_deltas);
  summary.switches_per_move =
      candidate_moves > 0 ? static_cast<double>(total_switches) / candidate_moves
                          : 0.0;
  return summary;
}

int positiveInteger(const char* value, std::string_view flag) {
  std::size_t consumed = 0;
  const long long parsed = std::stoll(value, &consumed, 0);
  if (consumed != std::string(value).size() || parsed < 1 ||
      parsed > std::numeric_limits<int>::max()) {
    throw std::invalid_argument(std::string(flag) + " must be positive");
  }
  return static_cast<int>(parsed);
}

float positiveFloat(const char* value, std::string_view flag) {
  std::size_t consumed = 0;
  const float parsed = std::stof(value, &consumed);
  if (consumed != std::string(value).size() || !(parsed > 0) ||
      !std::isfinite(parsed)) {
    throw std::invalid_argument(std::string(flag) + " must be positive");
  }
  return parsed;
}

Config parseConfig(int argc, char** argv) {
  Config config;
  for (int index = 2; index < argc; ++index) {
    const std::string flag = argv[index];
    if (index + 1 >= argc) throw std::invalid_argument("missing value for " + flag);
    const char* value = argv[++index];
    if (flag == "--oracle-train-games") {
      config.oracle_train_games = positiveInteger(value, flag);
    } else if (flag == "--behavior-train-games") {
      config.behavior_train_games = positiveInteger(value, flag);
    } else if (flag == "--oracle-holdout-games") {
      config.oracle_holdout_games = positiveInteger(value, flag);
    } else if (flag == "--behavior-holdout-games") {
      config.behavior_holdout_games = positiveInteger(value, flag);
    } else if (flag == "--label-moves") {
      config.label_moves = positiveInteger(value, flag);
    } else if (flag == "--epochs") {
      config.epochs = positiveInteger(value, flag);
    } else if (flag == "--batch-size") {
      config.batch_size = positiveInteger(value, flag);
    } else if (flag == "--learning-rate") {
      config.learning_rate = positiveFloat(value, flag);
    } else if (flag == "--screen-games") {
      config.screen_games = positiveInteger(value, flag);
    } else if (flag == "--confirmation-games") {
      config.confirmation_games = positiveInteger(value, flag);
    } else if (flag == "--evaluation-moves") {
      config.evaluation_moves = positiveInteger(value, flag);
    } else if (flag == "--output") {
      config.output = value;
    } else if (flag == "--model") {
      config.model = value;
    } else {
      throw std::invalid_argument("unknown argument " + flag);
    }
  }
  return config;
}

void writeSummary(std::ostream& output, std::string_view name,
                  const Summary& summary) {
  output << '"' << name << "\":{\"baselineMeanScore\":"
         << summary.baseline_mean_score << ",\"baselineMeanMoves\":"
         << summary.baseline_mean_moves << ",\"meanScore\":"
         << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"pairedScoreDelta\":" << summary.score_delta
         << ",\"pairedMoveDelta\":" << summary.move_delta
         << ",\"scoreDeltaLower95\":" << summary.score_lower95
         << ",\"moveDeltaLower95\":" << summary.move_lower95
         << ",\"switchesPerMove\":" << summary.switches_per_move
         << ",\"censored\":" << summary.censored << '}';
}

void writeArtifact(const Config& config, const Corpus& training,
                   const Corpus& oracle_holdout,
                   const Corpus& behavior_holdout,
                   const PolicyMetrics& all_metrics,
                   const PolicyMetrics& oracle_metrics,
                   const PolicyMetrics& behavior_metrics,
                   bool label_gate, const Summary& screen,
                   bool screen_gate, const Summary& confirmation,
                   bool confirmation_ran, bool confirmation_gate,
                   double seconds) {
  std::ofstream output(config.output);
  if (!output) throw std::runtime_error("could not open result artifact");
  output << std::setprecision(10)
         << "{\n  \"format\":\"drop7-oracle-distill-v1\",\n"
         << "  \"teacher\":\"privileged-future-depth4-beam128\",\n"
         << "  \"studentInput\":[\"board\",\"nextDisc\",\"movesRemaining\"],\n"
         << "  \"forbiddenStudentInput\":[\"gameSeed\",\"futureDiscs\","
            "\"revealRng\",\"score\",\"level\",\"movesPlayed\"],\n"
         << "  \"reflection\":\"lexicographic-canonicalization\",\n"
         << "  \"network\":\"sparse-614x128x128x7\",\n"
         << "  \"parameterBytes\":384540,\n"
         << "  \"seeds\":{\"oracleTrain\":\"0x3d7a0000\","
            "\"behaviorTrain\":\"0x3d7b0000\","
            "\"oracleHoldout\":\"0x3d7c0000\","
            "\"behaviorHoldout\":\"0x3d7d0000\","
            "\"screen\":\"0x3d7e0000\","
            "\"confirmation\":\"0x3d7f0000\"},\n"
         << "  \"trainingExamples\":" << training.examples.size()
         << ",\"oracleHoldoutExamples\":" << oracle_holdout.examples.size()
         << ",\"behaviorHoldoutExamples\":"
         << behavior_holdout.examples.size() << ",\n"
         << "  \"heldout\":{\"all\":{\"top1\":" << all_metrics.top1
         << ",\"top2\":" << all_metrics.top2 << ",\"crossEntropy\":"
         << all_metrics.loss << "},\"oracleRollin\":{\"top1\":"
         << oracle_metrics.top1 << ",\"top2\":" << oracle_metrics.top2
         << ",\"crossEntropy\":" << oracle_metrics.loss
         << "},\"behaviorRollin\":{\"top1\":" << behavior_metrics.top1
         << ",\"top2\":" << behavior_metrics.top2
         << ",\"crossEntropy\":" << behavior_metrics.loss << "}},\n"
         << "  \"labelGates\":{\"top1\":0.30,\"top2\":0.55,"
            "\"crossEntropyMaximum\":1.75,\"passed\":"
         << (label_gate ? "true" : "false") << "},\n"
         << "  \"deploymentRule\":{\"studentProbability\":0.40,"
            "\"probabilityAdvantage\":0.12,\"exactQRangeLoss\":0.10,"
            "\"minimumExactQTolerance\":5000},\n  ";
  writeSummary(output, "screen", screen);
  output << ",\n  \"screenPassedBothMeans\":"
         << (screen_gate ? "true" : "false")
         << ",\n  \"confirmationRan\":"
         << (confirmation_ran ? "true" : "false")
         << ",\n  \"confirmationPassedBothMeans\":"
         << (confirmation_gate ? "true" : "false") << ",\n  ";
  writeSummary(output, "confirmation", confirmation);
  output << ",\n  \"decision\":\""
         << (label_gate && screen_gate && confirmation_gate ? "accept"
                                                             : "reject")
         << "\",\n"
         << "  \"protectedSeedsRead\":false,\n"
         << "  \"model\":\"" << config.model << "\",\n"
         << "  \"elapsedSeconds\":" << seconds << "\n}\n";
}

int run(const Config& config, std::ostream& output) {
  validateTrainingRange(kOracleTrainStart, config.oracle_train_games);
  validateTrainingRange(kBehaviorTrainStart, config.behavior_train_games);
  validateTrainingRange(kOracleHoldoutStart, config.oracle_holdout_games);
  validateTrainingRange(kBehaviorHoldoutStart, config.behavior_holdout_games);
  validateTrainingRange(kScreenStart, config.screen_games);
  validateTrainingRange(kConfirmationStart, config.confirmation_games);
  const auto started = Clock::now();

  Corpus training = collectCorpus(kOracleTrainStart,
                                  config.oracle_train_games,
                                  config.label_moves, RollIn::kOracle,
                                  "oracle-train");
  appendCorpus(training, collectCorpus(kBehaviorTrainStart,
                                       config.behavior_train_games,
                                       config.label_moves, RollIn::kBehavior,
                                       "behavior-train"));
  const Corpus oracle_holdout = collectCorpus(
      kOracleHoldoutStart, config.oracle_holdout_games, config.label_moves,
      RollIn::kOracle, "oracle-holdout");
  const Corpus behavior_holdout = collectCorpus(
      kBehaviorHoldoutStart, config.behavior_holdout_games,
      config.label_moves, RollIn::kBehavior, "behavior-holdout");

  Network network(0x3d7a'4e4eu);
  phase_student::train(network, training.examples, config.epochs,
                       config.batch_size, config.learning_rate,
                       "oracle-distill");
  std::vector<Example> all_holdout = oracle_holdout.examples;
  all_holdout.insert(all_holdout.end(), behavior_holdout.examples.begin(),
                     behavior_holdout.examples.end());
  const PolicyMetrics all_metrics =
      phase_student::evaluatePolicy(network, all_holdout);
  const PolicyMetrics oracle_metrics =
      phase_student::evaluatePolicy(network, oracle_holdout.examples);
  const PolicyMetrics behavior_metrics =
      phase_student::evaluatePolicy(network, behavior_holdout.examples);
  const bool label_gate = all_metrics.top1 >= kMinimumHoldoutTop1 &&
                          all_metrics.top2 >= kMinimumHoldoutTop2 &&
                          all_metrics.loss <= kMaximumHoldoutCrossEntropy;

  // This first policy comparison is always run.  The model and override rule
  // are locked before reading these whole-game evaluation seeds.
  const Summary screen = comparePolicies(network, kScreenStart,
                                         config.screen_games,
                                         config.evaluation_moves, "screen");
  const bool screen_gate = screen.score_delta > 0.0 && screen.move_delta > 0.0;
  Summary confirmation;
  bool confirmation_ran = false;
  if (screen_gate) {
    confirmation_ran = true;
    confirmation = comparePolicies(network, kConfirmationStart,
                                   config.confirmation_games,
                                   config.evaluation_moves, "confirmation");
  }
  const bool confirmation_gate =
      confirmation_ran && confirmation.score_delta > 0.0 &&
      confirmation.move_delta > 0.0;
  network.save(config.model);
  const double seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  writeArtifact(config, training, oracle_holdout, behavior_holdout,
                all_metrics, oracle_metrics, behavior_metrics, label_gate,
                screen, screen_gate, confirmation, confirmation_ran,
                confirmation_gate, seconds);
  output << std::fixed << std::setprecision(6)
         << "ORACLE_DISTILL_RESULT {\"trainingSeedOnly\":true"
         << ",\"trainingExamples\":" << training.examples.size()
         << ",\"heldoutExamples\":" << all_holdout.size()
         << ",\"heldoutTop1\":" << all_metrics.top1
         << ",\"heldoutTop2\":" << all_metrics.top2
         << ",\"heldoutCrossEntropy\":" << all_metrics.loss
         << ",\"labelGate\":" << (label_gate ? "true" : "false")
         << ",\"screenScoreDelta\":" << screen.score_delta
         << ",\"screenMoveDelta\":" << screen.move_delta
         << ",\"screenPassed\":" << (screen_gate ? "true" : "false")
         << ",\"confirmationRan\":"
         << (confirmation_ran ? "true" : "false")
         << ",\"confirmationPassed\":"
         << (confirmation_gate ? "true" : "false")
         << ",\"decision\":\""
         << (label_gate && screen_gate && confirmation_gate ? "accept"
                                                             : "reject")
         << "\",\"artifact\":\"" << config.output << "\"}\n";
  return 0;
}

bool selfTest(std::ostream& output) {
  Network network(0x3d7a'4e4eu);
  State state = initialHeadlessState(0x3d70'0000u);
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(4, 1)] = 2;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;
  const int action = chooseStudentAction(network, state);
  State reflected = state;
  reflected.board = phase_student::mirrorBoard(state.board);
  const int reflected_action = chooseStudentAction(network, reflected);
  const bool reflection_safe =
      reflected_action == kBoardSize - 1 - action;
  State forbidden_changed = state;
  forbidden_changed.score = 9'999'999;
  forbidden_changed.level = 77;
  forbidden_changed.moves_played = 381;
  forbidden_changed.game_over = true;
  const bool public_isolation =
      chooseStudentAction(network, forbidden_changed) == action;
  const bool legal = isLegal(state.board, action);
  const OraclePlan oracle_first =
      planOracleMove(initialHeadlessState(0x3d70'0000u), 0x3d70'0000u,
                     kOracleDepth, kOracleBeam);
  const OraclePlan oracle_second =
      planOracleMove(initialHeadlessState(0x3d70'0000u), 0x3d70'0000u,
                     kOracleDepth, kOracleBeam);
  const bool oracle_deterministic =
      oracle_first.column == oracle_second.column &&
      oracle_first.stats.generated == oracle_second.stats.generated;
  // The reference TypeScript oracle selects column zero for this fixture.  The
  // native leaf differs only in its downward-exposure edge handling.
  const bool oracle_reference = oracle_first.column == 0;
  const bool oracle_legal =
      isLegal(initialBoard(), oracle_first.column);
  const bool seed_guard = [&] {
    try {
      validateTrainingRange(0x4d70'0000u, 1);
      return false;
    } catch (const std::invalid_argument&) {
      return true;
    }
  }();
  const bool passed = reflection_safe && public_isolation && legal &&
                      oracle_deterministic && oracle_reference &&
                      oracle_legal && seed_guard;
  output << "ORACLE_DISTILL_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"publicStateIsolation\":"
         << (public_isolation ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe ? "true" : "false")
         << ",\"legalMask\":" << (legal ? "true" : "false")
         << ",\"oracleDeterministic\":"
         << (oracle_deterministic ? "true" : "false")
         << ",\"oracleReferenceAction\":" << oracle_first.column
         << ",\"oracleGenerated\":" << oracle_first.stats.generated
         << ",\"protectedSeedGuard\":"
         << (seed_guard ? "true" : "false") << "}\n";
  return passed;
}

}  // namespace drop7::oracle_distill

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
      return drop7::oracle_distill::selfTest(std::cout) ? 0 : 1;
    }
    if (argc >= 2 && std::string(argv[1]) == "--run") {
      return drop7::oracle_distill::run(
          drop7::oracle_distill::parseConfig(argc, argv), std::cout);
    }
    std::cerr
        << "usage: drop7_oracle_distill --self-test | --run "
           "[--oracle-train-games N] [--behavior-train-games N] "
           "[--oracle-holdout-games N] [--behavior-holdout-games N] "
           "[--label-moves N] [--epochs N] [--batch-size N] "
           "[--learning-rate X] [--screen-games N] "
           "[--confirmation-games N] [--evaluation-moves N] "
           "[--output PATH] [--model PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_oracle_distill: " << error.what() << '\n';
    return 1;
  }
}
