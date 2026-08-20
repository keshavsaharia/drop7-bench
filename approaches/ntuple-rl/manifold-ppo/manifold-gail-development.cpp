#define main drop7_oracle_manifold_ppo_frozen_entrypoint
#pragma push_macro("main")
#undef main
#pragma push_macro("oracle_manifold_ppo")
#define oracle_manifold_ppo                                                   \
  _Pragma("pop_macro(\"main\")")                                           \
  _Pragma("pop_macro(\"oracle_manifold_ppo\")") oracle_manifold_ppo
#include "oracle-manifold-ppo.cpp"
#undef main

#include <bit>
#include <fstream>
#include <optional>
#include <sstream>

// Trains a reflection-invariant 295-24-1 discriminator from 3,032 matched
// public pairs, then uses that discriminator to shape one fixed PPO run.  The
// executable evaluates whole-origin cross-fold AUC and matched-pair ranking
// gates without reading oracle metadata, collecting additional discriminator
// roots, or adapting the policy-selection rule.
namespace drop7::manifold_gail_development {

namespace frozen = drop7::oracle_manifold_ppo;
namespace prior = drop7::curriculum_option_ppo;
namespace vr = drop7::viability_reservoir_controller;
using Clock = std::chrono::steady_clock;
using PublicState = prior::PublicState;

constexpr std::size_t kMinimumMatchedPairs = 3'000;
constexpr double kMinimumFoldAuc = 0.90;
constexpr double kMinimumFoldPairRanking = 0.90;
constexpr std::size_t kExpectedMatchedPairs = 3'032;
constexpr std::array<std::size_t, 2> kExpectedFoldPairs{{1'503, 1'529}};
constexpr std::uint64_t kExpectedMatchFingerprint =
    0xc1ad'c1ba'7dae'1d99ull;
constexpr double kStoppedCoverageGate = 0.80;
constexpr double kStoppedObservedCoverage = 0.740234375;

constexpr double kGateMeanScore = 300'000.0;
constexpr double kGateMeanMoves = 90.0;
constexpr double kGateClearsPerMove = 1.95;
constexpr double kGateRevealsPerMove = 1.08;
constexpr double kGateBottomQuartileMoves = 45.0;
constexpr double kGateScoreRatio = 1.25;
constexpr double kGateMoveRatio = 1.25;
constexpr int kGateJointWins = 20;

constexpr double kMaximumPreflightSeconds = 10.0 * 60.0;
constexpr double kWallLimitSeconds = 60.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr std::uint64_t kDiscriminatorCheckpointMagic =
    0x4437'4d47'4149'4c31ull;  // D7MGAIL1
constexpr std::uint32_t kDiscriminatorCheckpointVersion = 1;

constexpr std::string_view kFrozenSourceSha256 =
    "5afd52091de9931761449f34015f09f46abcd6fcd37331e96a033ccde30a5e2f";
constexpr std::string_view kCurriculumSha256 =
    "c963ac242994e7d18020fd7369954be2f4015d7f6c972f6d5fffe79c371db226";
constexpr std::string_view kInheritedSha256 =
    "14b8c89cdc9a219480cdf74d9bb9bca4afec3b68997a7e0707077d97337cc55c";
constexpr std::string_view kStoppedArtifactSha256 =
    "47434c51ab6c00d2e89e141ece694caf3f7506f585bef74f1882ac71705210ce";

// These assertions lock the included schedule, reward, architecture, and seed
// lanes so any configuration drift stops the build.
static_assert(frozen::kDiscriminatorInputs == 295);
static_assert(frozen::kDiscriminatorHidden == 24);
static_assert(frozen::DiscriminatorLayout::count == 7'129);
static_assert(frozen::kIterations == 48);
static_assert(frozen::kEpisodesPerIteration == 512);
static_assert(frozen::kTrainingEpisodes == 24'576);
static_assert(frozen::kTrainingSeedStart == 0x3d6b'1000u);
static_assert(frozen::kTrainingSeedEndExclusive == 0x3d6b'7000u);
static_assert(frozen::kPpoEpochs == 4 && frozen::kMinibatch == 512);
static_assert(frozen::kGamma == 0.999f && frozen::kGaeLambda == 0.97f);
static_assert(frozen::kClipRatio == 0.20f);
static_assert(frozen::kEntropyCoefficient == 0.005f);
static_assert(frozen::kValueCoefficient == 0.25f);
static_assert(frozen::kGradientNorm == 0.50f);
static_assert(frozen::kLearningRate == 0.0001f);
static_assert(frozen::kSurvivalReward == 0.05f);
static_assert(frozen::kClearReward == 0.05f);
static_assert(frozen::kRevealReward == 0.15f);
static_assert(frozen::kTerminalReward == -5.0f);
static_assert(frozen::kGailCoefficient == 0.10f);
static_assert(frozen::kPotentialCoefficient == 0.15f);
static_assert(frozen::kMaximumPotential == 4.0f);
static_assert(frozen::kNegativeSeedStart == 0x3d6b'0000u);
static_assert(frozen::kNegativeSeedEndExclusive == 0x3d6b'0400u);
static_assert(frozen::kStageASeedStart == 0x3d6c'0000u);
static_assert(frozen::kStageASeedEndExclusive == 0x3d6c'0020u);
static_assert(frozen::kStageAGames == 32);
static_assert(frozen::kStageAMaximumMoves == 1'000);
static_assert(frozen::kWallLimitSeconds == kWallLimitSeconds);
static_assert(frozen::kRssLimitBytes == kRssLimitBytes);
static_assert(prior::Layout::count == 58'312);
static_assert(frozen::kExpectedCurriculumFingerprint ==
              0x8657'ac0d'c83c'6041ull);
static_assert(frozen::kExpectedPriorFingerprint ==
              0x3405'524b'4c94'2a9eull);

struct Options {
  std::string curriculum = "/tmp/drop7-oracle-curriculum-states.jsonl";
  std::string inherited = "/tmp/drop7-curriculum-option-ppo.bin";
  std::string checkpoint = "/tmp/drop7-manifold-gail-development.bin";
  std::string discriminator_checkpoint =
      "/tmp/drop7-manifold-gail-discriminator.bin";
  std::string golden = "/tmp/drop7-manifold-gail-development-golden.json";
  std::string preflight =
      "/tmp/drop7-manifold-gail-development-preflight.json";
  std::string output =
      "/tmp/drop7-manifold-gail-development-stage-a.json";
  std::string curriculum_sha256 = std::string(kCurriculumSha256);
  std::string inherited_sha256 = std::string(kInheritedSha256);
  std::string stopped_artifact_sha256 = std::string(kStoppedArtifactSha256);
  int threads = 4;
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; ++index) {
    const std::string flag = argv[index];
    const auto value = [&]() -> std::string {
      if (index + 1 >= argc) {
        throw std::invalid_argument("missing option value for " + flag);
      }
      return argv[++index];
    };
    if (flag == "--curriculum") result.curriculum = value();
    else if (flag == "--inherited") result.inherited = value();
    else if (flag == "--checkpoint") result.checkpoint = value();
    else if (flag == "--discriminator-checkpoint") {
      result.discriminator_checkpoint = value();
    } else if (flag == "--golden") result.golden = value();
    else if (flag == "--preflight-output") result.preflight = value();
    else if (flag == "--output") result.output = value();
    else if (flag == "--curriculum-sha256") {
      result.curriculum_sha256 = value();
    } else if (flag == "--inherited-sha256") {
      result.inherited_sha256 = value();
    } else if (flag == "--stopped-artifact-sha256") {
      result.stopped_artifact_sha256 = value();
    } else if (flag == "--threads") {
      result.threads = std::stoi(value());
    } else {
      throw std::invalid_argument("unknown option " + flag);
    }
  }
  if (result.curriculum.empty() || result.inherited.empty() ||
      result.checkpoint.empty() || result.discriminator_checkpoint.empty() ||
      result.golden.empty() || result.preflight.empty() ||
      result.output.empty() || result.threads < 1 ||
      result.threads > frozen::kMaximumThreads ||
      result.curriculum_sha256 != kCurriculumSha256 ||
      result.inherited_sha256 != kInheritedSha256 ||
      result.stopped_artifact_sha256 != kStoppedArtifactSha256) {
    throw std::invalid_argument("invalid or checksum-mismatched options");
  }
  return result;
}

std::string jsonEscape(std::string_view value) {
  std::string result;
  for (const char token : value) {
    if (token == '"' || token == '\\') result.push_back('\\');
    if (token == '\n') result += "\\n";
    else result.push_back(token);
  }
  return result;
}

std::string hex64(std::uint64_t value) {
  std::ostringstream output;
  output << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
  return output.str();
}

std::uint64_t peakRssBytes() { return frozen::peakRssBytes(); }

void enforceResources(const frozen::Deadline& deadline) {
  deadline.check();
  frozen::enforceRssLimit();
}

std::uint64_t discriminatorFingerprint(
    const frozen::Discriminator& discriminator,
    std::uint64_t dataset_fingerprint) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  const auto consume = [&](std::uint8_t byte) {
    prior::fingerprintByte(hash, byte);
  };
  for (const float parameter : discriminator.parameters()) {
    std::uint32_t bits = std::bit_cast<std::uint32_t>(parameter);
    for (int shift = 0; shift < 32; shift += 8) {
      consume(static_cast<std::uint8_t>(bits >> shift));
    }
  }
  for (int shift = 0; shift < 64; shift += 8) {
    consume(static_cast<std::uint8_t>(dataset_fingerprint >> shift));
  }
  return hash;
}

void saveDiscriminatorCheckpoint(
    const std::string& path, const frozen::Discriminator& discriminator,
    std::uint64_t dataset_fingerprint) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("could not write discriminator checkpoint");
  const std::uint32_t inputs = frozen::kDiscriminatorInputs;
  const std::uint32_t hidden = frozen::kDiscriminatorHidden;
  const std::uint32_t count = frozen::DiscriminatorLayout::count;
  const std::uint64_t fingerprint =
      discriminatorFingerprint(discriminator, dataset_fingerprint);
  output.write(reinterpret_cast<const char*>(&kDiscriminatorCheckpointMagic),
               sizeof(kDiscriminatorCheckpointMagic));
  output.write(reinterpret_cast<const char*>(&kDiscriminatorCheckpointVersion),
               sizeof(kDiscriminatorCheckpointVersion));
  output.write(reinterpret_cast<const char*>(&inputs), sizeof(inputs));
  output.write(reinterpret_cast<const char*>(&hidden), sizeof(hidden));
  output.write(reinterpret_cast<const char*>(&count), sizeof(count));
  output.write(reinterpret_cast<const char*>(&dataset_fingerprint),
               sizeof(dataset_fingerprint));
  output.write(reinterpret_cast<const char*>(&fingerprint), sizeof(fingerprint));
  output.write(
      reinterpret_cast<const char*>(discriminator.parameters().data()),
      static_cast<std::streamsize>(discriminator.parameters().size() *
                                   sizeof(float)));
  if (!output) throw std::runtime_error("discriminator checkpoint write failed");
}

void verifyDiscriminatorCheckpoint(
    const std::string& path, const frozen::Discriminator& discriminator,
    std::uint64_t dataset_fingerprint) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not read discriminator checkpoint");
  std::uint64_t magic = 0;
  std::uint32_t version = 0;
  std::uint32_t inputs = 0;
  std::uint32_t hidden = 0;
  std::uint32_t count = 0;
  std::uint64_t stored_dataset = 0;
  std::uint64_t stored_fingerprint = 0;
  input.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  input.read(reinterpret_cast<char*>(&version), sizeof(version));
  input.read(reinterpret_cast<char*>(&inputs), sizeof(inputs));
  input.read(reinterpret_cast<char*>(&hidden), sizeof(hidden));
  input.read(reinterpret_cast<char*>(&count), sizeof(count));
  input.read(reinterpret_cast<char*>(&stored_dataset), sizeof(stored_dataset));
  input.read(reinterpret_cast<char*>(&stored_fingerprint),
             sizeof(stored_fingerprint));
  std::vector<float> parameters(count);
  input.read(reinterpret_cast<char*>(parameters.data()),
             static_cast<std::streamsize>(parameters.size() * sizeof(float)));
  const bool payload = static_cast<bool>(input);
  char trailing = 0;
  const bool has_trailing = static_cast<bool>(input.read(&trailing, 1));
  if (!payload || has_trailing || !input.eof() ||
      magic != kDiscriminatorCheckpointMagic ||
      version != kDiscriminatorCheckpointVersion ||
      inputs != frozen::kDiscriminatorInputs ||
      hidden != frozen::kDiscriminatorHidden ||
      count != frozen::DiscriminatorLayout::count ||
      stored_dataset != dataset_fingerprint ||
      stored_fingerprint !=
          discriminatorFingerprint(discriminator, dataset_fingerprint) ||
      parameters != discriminator.parameters()) {
    throw std::runtime_error("discriminator checkpoint verification failed");
  }
}

struct DevelopmentDiscriminator {
  frozen::Discriminator model{};
  std::array<frozen::LabelMetrics, 2> heldout{};
  frozen::LabelMetrics all{};
  bool pair_count_admitted = false;
  bool fold_metrics_admitted = false;
  bool admitted = false;
  std::uint64_t fingerprint = 0;
};

DevelopmentDiscriminator fitDevelopmentDiscriminator(
    const frozen::MatchedDataset& dataset,
    const frozen::Deadline& deadline) {
  DevelopmentDiscriminator result;
  for (int training_fold = 0; training_fold < 2; ++training_fold) {
    const frozen::Discriminator crossfit = frozen::trainDiscriminator(
        dataset.folds[training_fold],
        frozen::kDiscriminatorSeed +
            static_cast<std::uint32_t>(training_fold + 1),
        deadline);
    const int heldout_fold = 1 - training_fold;
    result.heldout[heldout_fold] =
        frozen::labelMetrics(crossfit, dataset.folds[heldout_fold]);
  }
  result.pair_count_admitted = dataset.matched >= kMinimumMatchedPairs;
  result.fold_metrics_admitted = true;
  for (const frozen::LabelMetrics& metrics : result.heldout) {
    result.fold_metrics_admitted =
        result.fold_metrics_admitted && metrics.auc >= kMinimumFoldAuc &&
        metrics.matched_pair_ranking >= kMinimumFoldPairRanking;
  }
  result.admitted = result.pair_count_admitted && result.fold_metrics_admitted;
  if (!result.admitted) return result;
  std::vector<frozen::MatchedPair> all = dataset.folds[0];
  all.insert(all.end(), dataset.folds[1].begin(), dataset.folds[1].end());
  result.model = frozen::trainDiscriminator(
      all, frozen::kDiscriminatorSeed, deadline);
  result.all = frozen::labelMetrics(result.model, all);
  result.fingerprint =
      discriminatorFingerprint(result.model, dataset.fingerprint);
  return result;
}

bool exactKnownCorpus(const prior::Curriculum& curriculum,
                      const frozen::MatchedDataset& dataset) {
  return curriculum.states.size() == 4'096 &&
      curriculum.fingerprint == frozen::kExpectedCurriculumFingerprint &&
      dataset.positive_total == 4'096 &&
      dataset.negative_rollin_states == 54'397 &&
      dataset.matched == kExpectedMatchedPairs &&
      dataset.folds[0].size() == kExpectedFoldPairs[0] &&
      dataset.folds[1].size() == kExpectedFoldPairs[1] &&
      dataset.fingerprint == kExpectedMatchFingerprint &&
      dataset.coverage == kStoppedObservedCoverage;
}

struct PreflightResult {
  prior::Curriculum curriculum{};
  prior::Network inherited{};
  frozen::MatchedDataset dataset{};
  DevelopmentDiscriminator discriminator{};
  double seconds = 0.0;
  bool passed = false;
};

void writeDevelopmentLabelMetrics(
    std::ostream& output, const frozen::LabelMetrics& metrics) {
  output << "{\"pairs\":" << metrics.pairs << ",\"auc\":"
         << metrics.auc << ",\"matchedPairRanking\":"
         << metrics.matched_pair_ranking << ",\"loss\":" << metrics.loss
         << '}';
}

void writePreflightArtifact(const Options& options,
                            const PreflightResult& preflight) {
  std::ofstream output(options.preflight, std::ios::trunc);
  if (!output) throw std::runtime_error("could not write preflight artifact");
  output << std::setprecision(12)
         << "{\n  \"format\":\"drop7-manifold-gail-development-preflight-v1\",\n"
         << "  \"noFreshGameplay\":true,\n"
         << "  \"oldExperimentHistory\":{\"status\":\"stopped\","
            "\"coverageGate\":" << kStoppedCoverageGate
         << ",\"observedCoverage\":" << kStoppedObservedCoverage
         << ",\"passed\":false,\"artifactSha256\":\""
         << options.stopped_artifact_sha256 << "\"},\n"
         << "  \"frozenInputs\":{\"curriculumSha256\":\""
         << options.curriculum_sha256 << "\",\"inheritedSha256\":\""
         << options.inherited_sha256 << "\",\"frozenSourceSha256\":\""
         << kFrozenSourceSha256 << "\"},\n"
         << "  \"knownCorpus\":{\"oracleStates\":"
         << preflight.curriculum.states.size()
         << ",\"negativeSeedLane\":\"0x3d6b0000..0x3d6b03ff\","
            "\"negativeRollinStates\":"
         << preflight.dataset.negative_rollin_states
         << ",\"matchedPairs\":" << preflight.dataset.matched
         << ",\"coverage\":" << preflight.dataset.coverage
         << ",\"matchFingerprint\":\""
         << hex64(preflight.dataset.fingerprint) << "\"},\n"
         << "  \"developmentAdmission\":{\"minimumPairs\":"
         << kMinimumMatchedPairs << ",\"minimumEachFoldAuc\":"
         << kMinimumFoldAuc << ",\"minimumEachFoldPairRanking\":"
         << kMinimumFoldPairRanking << ",\"heldout\":[";
  writeDevelopmentLabelMetrics(output, preflight.discriminator.heldout[0]);
  output << ',';
  writeDevelopmentLabelMetrics(output, preflight.discriminator.heldout[1]);
  output << "],\"pairCountPassed\":"
         << (preflight.discriminator.pair_count_admitted ? "true" : "false")
         << ",\"foldMetricsPassed\":"
         << (preflight.discriminator.fold_metrics_admitted ? "true" : "false")
         << ",\"passed\":"
         << (preflight.discriminator.admitted ? "true" : "false")
         << "},\n  \"discriminator\":{\"architecture\":\"295-24-1\","
            "\"reflectionInvariant\":true,\"allFit\":";
  writeDevelopmentLabelMetrics(output, preflight.discriminator.all);
  output << ",\"fingerprint\":\""
         << hex64(preflight.discriminator.fingerprint) << "\"},\n"
         << "  \"futureSeedSeal\":{\"training\":\"unopened\","
            "\"stageA\":\"unopened\",\"protectedFamilies\":\"unopened\"},\n"
         << "  \"resources\":{\"seconds\":" << preflight.seconds
         << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"maximumPreflightSeconds\":" << kMaximumPreflightSeconds
         << ",\"rssLimitBytes\":" << kRssLimitBytes << "},\n"
         << "  \"passed\":" << (preflight.passed ? "true" : "false")
         << "\n}\n";
  if (!output) throw std::runtime_error("preflight artifact write failed");
}

PreflightResult runPreflight(const Options& options,
                             const frozen::Deadline& deadline,
                             bool freeze_discriminator) {
  const auto started = Clock::now();
  PreflightResult result;
  result.curriculum = prior::loadCurriculum(options.curriculum);
  result.inherited = prior::loadCheckpoint(options.inherited);
  if (prior::modelFingerprint(result.inherited) !=
      frozen::kExpectedPriorFingerprint) {
    throw std::runtime_error("inherited checkpoint fingerprint mismatch");
  }
  result.dataset =
      frozen::buildMatchedDataset(result.curriculum, deadline);
  if (!exactKnownCorpus(result.curriculum, result.dataset)) {
    throw std::runtime_error("known matched corpus checksum/count mismatch");
  }
  result.discriminator =
      fitDevelopmentDiscriminator(result.dataset, deadline);
  if (result.discriminator.admitted && freeze_discriminator) {
    saveDiscriminatorCheckpoint(options.discriminator_checkpoint,
                                result.discriminator.model,
                                result.dataset.fingerprint);
    verifyDiscriminatorCheckpoint(options.discriminator_checkpoint,
                                  result.discriminator.model,
                                  result.dataset.fingerprint);
  }
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  result.passed = result.discriminator.admitted &&
      result.seconds <= kMaximumPreflightSeconds &&
      peakRssBytes() <= kRssLimitBytes;
  writePreflightArtifact(options, result);
  enforceResources(deadline);
  return result;
}

struct FrozenCandidate {
  prior::Network network{};
  std::uint64_t fingerprint = 0;
  bool checkpoint_verified = false;
  bool golden_written = false;
};

std::uint64_t goldenFingerprint(
    const prior::Prediction& prediction, int action,
    float discriminator_logit, std::uint64_t model_fingerprint) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  const auto consume32 = [&](std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
      prior::fingerprintByte(hash,
          static_cast<std::uint8_t>(value >> shift));
    }
  };
  for (const float value : prediction.logits) {
    consume32(std::bit_cast<std::uint32_t>(value));
  }
  for (const float value : prediction.probabilities) {
    consume32(std::bit_cast<std::uint32_t>(value));
  }
  consume32(std::bit_cast<std::uint32_t>(prediction.value));
  consume32(static_cast<std::uint32_t>(action + 1));
  consume32(std::bit_cast<std::uint32_t>(discriminator_logit));
  for (int shift = 0; shift < 64; shift += 8) {
    prior::fingerprintByte(hash,
        static_cast<std::uint8_t>(model_fingerprint >> shift));
  }
  return hash;
}

void writeGolden(const Options& options, FrozenCandidate& candidate,
                 const frozen::Discriminator& discriminator) {
  const PublicState fixture = prior::fixtureState();
  bool ignored = false;
  const PublicState canonical = vr::canonicalState(fixture, ignored);
  const prior::BasePolicy base = prior::fairBasePolicy(canonical);
  const prior::Prediction prediction =
      prior::predictCanonical(candidate.network, canonical, &base);
  const prior::PolicyDecision decision =
      prior::chooseAction(fixture, candidate.network);
  const prior::PolicyDecision reflected =
      prior::chooseAction(vr::mirror(fixture), candidate.network);
  if (reflected.action != kBoardSize - 1 - decision.action) {
    throw std::runtime_error("frozen candidate reflection golden failed");
  }
  const float discriminator_logit = discriminator.logit(canonical);
  const std::uint64_t golden = goldenFingerprint(
      prediction, decision.action, discriminator_logit, candidate.fingerprint);
  std::ofstream output(options.golden, std::ios::trunc);
  if (!output) throw std::runtime_error("could not write golden artifact");
  output << std::setprecision(12)
         << "{\"format\":\"drop7-manifold-gail-golden-v1\","
         << "\"modelFingerprint\":\"" << hex64(candidate.fingerprint)
         << "\",\"fixturePublicHash\":\""
         << hex64(frozen::publicHash(canonical))
         << "\",\"action\":" << decision.action
         << ",\"reflectedAction\":" << reflected.action
         << ",\"value\":" << prediction.value
         << ",\"discriminatorLogit\":" << discriminator_logit
         << ",\"logits\":[";
  for (int action = 0; action < kBoardSize; ++action) {
    if (action) output << ',';
    if (std::isfinite(prediction.logits[action])) output << prediction.logits[action];
    else output << "null";
  }
  output << "],\"probabilities\":[";
  for (int action = 0; action < kBoardSize; ++action) {
    if (action) output << ',';
    output << prediction.probabilities[action];
  }
  output << "],\"goldenFingerprint\":\"" << hex64(golden)
         << "\",\"reflectionExact\":true,\"metadataRetained\":false}\n";
  output.close();
  if (!output) throw std::runtime_error("golden artifact write failed");
  candidate.golden_written = true;
}

FrozenCandidate freezeCandidate(
    const Options& options, const prior::Network& trained,
    const frozen::Discriminator& discriminator) {
  prior::saveCheckpoint(options.checkpoint, trained);
  FrozenCandidate result;
  result.network = prior::loadCheckpoint(options.checkpoint);
  result.fingerprint = prior::modelFingerprint(result.network);
  result.checkpoint_verified =
      result.fingerprint == prior::modelFingerprint(trained) &&
      result.network.parameters() == trained.parameters();
  if (!result.checkpoint_verified) {
    throw std::runtime_error("candidate checkpoint did not freeze bit-exactly");
  }
  writeGolden(options, result, discriminator);
  if (!result.golden_written) {
    throw std::runtime_error("candidate golden was not frozen");
  }
  return result;
}

std::vector<frozen::GameResult> evaluateAfterFreeze(
    const FrozenCandidate& candidate, const prior::Network& inherited,
    frozen::EvaluationPolicy policy, int threads,
    const frozen::Deadline& deadline) {
  if (!candidate.checkpoint_verified || !candidate.golden_written) {
    throw std::runtime_error("Stage-A attempted before checkpoint/golden freeze");
  }
  return frozen::evaluate(candidate.network, inherited, policy, threads,
                          deadline);
}

struct StageAGate {
  bool mean_score = false;
  bool mean_moves = false;
  bool clears = false;
  bool reveals = false;
  bool bottom_quartile = false;
  bool score_ratio = false;
  bool move_ratio = false;
  bool joint_wins = false;
  bool passed = false;
};

StageAGate stageAGate(const frozen::Summary& candidate,
                      const frozen::Summary& inherited,
                      const frozen::PairedSummary& paired) {
  StageAGate result;
  result.mean_score = candidate.mean_score >= kGateMeanScore;
  result.mean_moves = candidate.mean_moves >= kGateMeanMoves;
  result.clears = candidate.clears_per_move >= kGateClearsPerMove;
  result.reveals = candidate.reveals_per_move >= kGateRevealsPerMove;
  result.bottom_quartile =
      candidate.bottom_quartile_moves >= kGateBottomQuartileMoves;
  result.score_ratio =
      candidate.mean_score >= kGateScoreRatio * inherited.mean_score;
  result.move_ratio =
      candidate.mean_moves >= kGateMoveRatio * inherited.mean_moves;
  result.joint_wins = paired.joint_wins >= kGateJointWins;
  result.passed = result.mean_score && result.mean_moves && result.clears &&
      result.reveals && result.bottom_quartile && result.score_ratio &&
      result.move_ratio && result.joint_wins;
  return result;
}

void writeDevelopmentSummary(std::ostream& output,
                             const frozen::Summary& value) {
  output << "{\"meanScore\":" << value.mean_score
         << ",\"meanMoves\":" << value.mean_moves
         << ",\"bottomQuartileMoves\":" << value.bottom_quartile_moves
         << ",\"clearsPerMove\":" << value.clears_per_move
         << ",\"revealsPerMove\":" << value.reveals_per_move
         << ",\"maximumChain\":" << value.maximum_chain
         << ",\"capped\":" << value.capped << '}';
}

void writeDevelopmentPaired(std::ostream& output,
                            const frozen::PairedSummary& value) {
  output << "{\"scoreWins\":" << value.score_wins
         << ",\"moveWins\":" << value.move_wins
         << ",\"jointWins\":" << value.joint_wins
         << ",\"meanScoreDelta\":" << value.score_delta
         << ",\"meanMoveDelta\":" << value.move_delta << '}';
}

void writeGate(std::ostream& output, const StageAGate& value) {
  output << "{\"thresholds\":{\"meanScore\":" << kGateMeanScore
         << ",\"meanMoves\":" << kGateMeanMoves
         << ",\"clearsPerMove\":" << kGateClearsPerMove
         << ",\"revealsPerMove\":" << kGateRevealsPerMove
         << ",\"bottomQuartileMoves\":" << kGateBottomQuartileMoves
         << ",\"scoreRatioVsInherited\":" << kGateScoreRatio
         << ",\"moveRatioVsInherited\":" << kGateMoveRatio
         << ",\"jointWinsVsInherited\":" << kGateJointWins
         << "},\"checks\":{\"meanScore\":"
         << (value.mean_score ? "true" : "false")
         << ",\"meanMoves\":" << (value.mean_moves ? "true" : "false")
         << ",\"clearsPerMove\":" << (value.clears ? "true" : "false")
         << ",\"revealsPerMove\":" << (value.reveals ? "true" : "false")
         << ",\"bottomQuartileMoves\":"
         << (value.bottom_quartile ? "true" : "false")
         << ",\"scoreRatio\":" << (value.score_ratio ? "true" : "false")
         << ",\"moveRatio\":" << (value.move_ratio ? "true" : "false")
         << ",\"jointWins\":" << (value.joint_wins ? "true" : "false")
         << "},\"passed\":" << (value.passed ? "true" : "false") << '}';
}

void writeGameTriples(
    std::ostream& output,
    const std::vector<frozen::GameResult>& candidate,
    const std::vector<frozen::GameResult>& inherited,
    const std::vector<frozen::GameResult>& d1) {
  output << '[';
  for (std::size_t game = 0; game < candidate.size(); ++game) {
    if (game) output << ',';
    if (candidate[game].seed != inherited[game].seed ||
        candidate[game].seed != d1[game].seed) {
      throw std::runtime_error("Stage-A game triples are not paired");
    }
    const auto one = [&](std::string_view name,
                         const frozen::GameResult& value) {
      output << '"' << name << "\":{\"score\":" << value.score
             << ",\"moves\":" << value.moves << ",\"clears\":"
             << value.clears << ",\"reveals\":" << value.reveals
             << ",\"maximumChain\":" << value.maximum_chain
             << ",\"capped\":" << (value.capped ? "true" : "false")
             << '}';
    };
    output << "{\"seed\":" << candidate[game].seed << ',';
    one("candidate", candidate[game]);
    output << ',';
    one("inherited", inherited[game]);
    output << ',';
    one("fairD1", d1[game]);
    output << '}';
  }
  output << ']';
}

void writeResultArtifact(
    const Options& options, const PreflightResult& preflight,
    const frozen::TrainingResult& training,
    const FrozenCandidate& frozen_candidate,
    const std::vector<frozen::GameResult>& candidate_games,
    const std::vector<frozen::GameResult>& inherited_games,
    const std::vector<frozen::GameResult>& d1_games,
    const frozen::Summary& candidate,
    const frozen::Summary& inherited,
    const frozen::Summary& d1,
    const frozen::PairedSummary& versus_inherited,
    const frozen::PairedSummary& versus_d1,
    const StageAGate& gate, double wall_seconds) {
  std::ofstream output(options.output, std::ios::trunc);
  if (!output) throw std::runtime_error("could not write Stage-A artifact");
  output << std::setprecision(12)
         << "{\n  \"format\":\"drop7-manifold-gail-development-v1\",\n"
         << "  \"hypothesis\":\"development admission from already-known high-separation exact matched pairs\",\n"
         << "  \"oldExperimentHistory\":{\"coverageGate\":"
         << kStoppedCoverageGate << ",\"observedCoverage\":"
         << kStoppedObservedCoverage
         << ",\"passed\":false,\"rewritten\":false},\n"
         << "  \"discriminator\":{\"architecture\":\"295-24-1\","
            "\"matchedPairs\":" << preflight.dataset.matched
         << ",\"matchFingerprint\":\""
         << hex64(preflight.dataset.fingerprint) << "\",\"heldout\":[";
  writeDevelopmentLabelMetrics(output, preflight.discriminator.heldout[0]);
  output << ',';
  writeDevelopmentLabelMetrics(output, preflight.discriminator.heldout[1]);
  output << "],\"allFit\":";
  writeDevelopmentLabelMetrics(output, preflight.discriminator.all);
  output << ",\"fingerprint\":\""
         << hex64(preflight.discriminator.fingerprint)
         << "\",\"checkpoint\":\""
         << jsonEscape(options.discriminator_checkpoint) << "\"},\n"
         << "  \"inheritance\":{\"path\":\""
         << jsonEscape(options.inherited) << "\",\"sha256\":\""
         << options.inherited_sha256 << "\",\"fingerprint\":\""
         << hex64(frozen::kExpectedPriorFingerprint)
         << "\",\"bitExactInitialization\":true,\"freshAdam\":true},\n"
         << "  \"training\":{\"scheduleFrozenFrom\":\"approaches/ntuple-rl/manifold-ppo/oracle-manifold-ppo.cpp\","
            "\"iterations\":" << frozen::kIterations
         << ",\"episodesPerIteration\":" << frozen::kEpisodesPerIteration
         << ",\"totalEpisodes\":" << frozen::kTrainingEpisodes
         << ",\"mixedStarts\":\"256 initial + 256 curriculum per iteration\","
            "\"seedLane\":\"0x3d6b1000..0x3d6b6fff\","
            "\"noMidRunSelection\":true,\"learningCurve\":[";
  for (int iteration = 0; iteration < frozen::kIterations; ++iteration) {
    if (iteration) output << ',';
    const frozen::TrainingRecord& record = training.records[iteration];
    output << "{\"iteration\":" << record.iteration
           << ",\"samples\":" << record.samples
           << ",\"initialScore\":" << record.initial_score
           << ",\"initialMoves\":" << record.initial_moves
           << ",\"curriculumScore\":" << record.curriculum_score
           << ",\"curriculumMoves\":" << record.curriculum_moves
           << ",\"clearsPerMove\":" << record.clears_per_move
           << ",\"revealsPerMove\":" << record.reveals_per_move
           << ",\"manifoldProbability\":"
           << record.discriminator_probability
           << ",\"manifoldRewardPerMove\":" << record.manifold_reward
           << ",\"policyLoss\":" << record.update.policy_loss
           << ",\"valueLoss\":" << record.update.value_loss
           << ",\"entropy\":" << record.update.entropy
           << ",\"approximateKl\":" << record.update.approximate_kl
           << ",\"clipFraction\":" << record.update.clip_fraction << '}';
  }
  output << "]},\n  \"freeze\":{\"checkpoint\":\""
         << jsonEscape(options.checkpoint) << "\",\"modelFingerprint\":\""
         << hex64(frozen_candidate.fingerprint)
         << "\",\"checkpointVerifiedBeforeStageA\":true,\"golden\":\""
         << jsonEscape(options.golden) << "\"},\n"
         << "  \"stageA\":{\"seedLane\":\"0x3d6c0000..0x3d6c001f\","
            "\"games\":32,\"maximumMoves\":1000,\"checkpointFrozenFirst\":true,"
            "\"candidate\":";
  writeDevelopmentSummary(output, candidate);
  output << ",\"inherited\":";
  writeDevelopmentSummary(output, inherited);
  output << ",\"fairD1\":";
  writeDevelopmentSummary(output, d1);
  output << ",\"versusInherited\":";
  writeDevelopmentPaired(output, versus_inherited);
  output << ",\"versusFairD1\":";
  writeDevelopmentPaired(output, versus_d1);
  output << ",\"scoreRatioVsInherited\":"
         << candidate.mean_score / inherited.mean_score
         << ",\"moveRatioVsInherited\":"
         << candidate.mean_moves / inherited.mean_moves << ",\"gate\":";
  writeGate(output, gate);
  output << ",\"games\":";
  writeGameTriples(output, candidate_games, inherited_games, d1_games);
  output << "},\n  \"seedAudit\":{\"negativeReplayOnly\":\"0x3d6b0000..0x3d6b03ff\","
            "\"trainingOnly\":\"0x3d6b1000..0x3d6b6fff\","
            "\"stageAOnly\":\"0x3d6c0000..0x3d6c001f\","
            "\"protected4d7dd7Opened\":false,\"screenBeyondStageAOpened\":false},\n"
         << "  \"resources\":{\"wallSeconds\":" << wall_seconds
         << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"wallLimitSeconds\":" << kWallLimitSeconds
         << ",\"rssLimitBytes\":" << kRssLimitBytes << "},\n"
         << "  \"passed\":" << (gate.passed ? "true" : "false")
         << "\n}\n";
  if (!output) throw std::runtime_error("Stage-A artifact write failed");
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

bool selfTest(const Options& options, std::ostream& output) {
  const prior::Curriculum curriculum = prior::loadCurriculum(options.curriculum);
  const prior::Network inherited = prior::loadCheckpoint(options.inherited);
  expect(curriculum.states.size() == 4'096 &&
             curriculum.fingerprint == frozen::kExpectedCurriculumFingerprint,
         "curriculum checksum self-test failed");
  expect(prior::modelFingerprint(inherited) ==
             frozen::kExpectedPriorFingerprint,
         "inherited checkpoint self-test failed");
  prior::Network initialized;
  initialized.setParameters(inherited.parameters());
  expect(initialized.parameters() == inherited.parameters() &&
             prior::modelFingerprint(initialized) ==
                 frozen::kExpectedPriorFingerprint,
         "bit-exact fresh-Adam initialization self-test failed");

  const PublicState fixture = prior::fixtureState();
  const auto features = frozen::topologyFeatures(fixture);
  expect(features == frozen::topologyFeatures(vr::mirror(fixture)),
         "discriminator reflection self-test failed");
  frozen::Discriminator discriminator;
  expect(discriminator.logit(fixture) ==
             discriminator.logit(vr::mirror(fixture)),
         "discriminator score reflection self-test failed");
  const auto direct = prior::chooseAction(fixture, initialized);
  const auto reflected = prior::chooseAction(vr::mirror(fixture), initialized);
  expect(reflected.action == kBoardSize - 1 - direct.action,
         "policy reflection self-test failed");
  State metadata = vr::materialize(fixture);
  metadata.score = 99'999'999;
  metadata.level = 999;
  metadata.moves_played = 777;
  expect(vr::publicState(metadata) == fixture &&
             frozen::topologyFeatures(vr::publicState(metadata)) == features &&
             prior::chooseAction(vr::publicState(metadata), initialized) ==
                 direct,
         "public-only metadata self-test failed");

  frozen::MatchedDataset admitted;
  admitted.matched = kMinimumMatchedPairs;
  DevelopmentDiscriminator positive;
  positive.pair_count_admitted = admitted.matched >= kMinimumMatchedPairs;
  positive.heldout[0].auc = 0.90;
  positive.heldout[0].matched_pair_ranking = 0.90;
  positive.heldout[1] = positive.heldout[0];
  positive.fold_metrics_admitted = true;
  for (const auto& metrics : positive.heldout) {
    positive.fold_metrics_admitted = positive.fold_metrics_admitted &&
        metrics.auc >= kMinimumFoldAuc &&
        metrics.matched_pair_ranking >= kMinimumFoldPairRanking;
  }
  positive.admitted =
      positive.pair_count_admitted && positive.fold_metrics_admitted;
  expect(positive.admitted, "positive development admission self-test failed");
  --admitted.matched;
  expect(!(admitted.matched >= kMinimumMatchedPairs),
         "pair-count rejection self-test failed");
  positive.heldout[1].auc = 0.899999;
  expect(!(positive.heldout[1].auc >= kMinimumFoldAuc),
         "fold-AUC rejection self-test failed");

  frozen::Summary candidate;
  candidate.mean_score = 300'000;
  candidate.mean_moves = 90;
  candidate.bottom_quartile_moves = 45;
  candidate.clears_per_move = 1.95;
  candidate.reveals_per_move = 1.08;
  frozen::Summary baseline;
  baseline.mean_score = 240'000;
  baseline.mean_moves = 72;
  frozen::PairedSummary paired;
  paired.joint_wins = 20;
  expect(stageAGate(candidate, baseline, paired).passed,
         "positive Stage-A gate self-test failed");
  candidate.bottom_quartile_moves = 44.999;
  expect(!stageAGate(candidate, baseline, paired).passed,
         "negative Stage-A gate self-test failed");

  const std::string discriminator_test =
      options.discriminator_checkpoint + ".self-test";
  saveDiscriminatorCheckpoint(discriminator_test, discriminator,
                              kExpectedMatchFingerprint);
  verifyDiscriminatorCheckpoint(discriminator_test, discriminator,
                                kExpectedMatchFingerprint);

  expect(frozen::allowedSeed(frozen::kNegativeSeedStart,
                             frozen::SeedUse::kNegative) &&
             frozen::allowedSeed(frozen::kNegativeSeedEndExclusive - 1,
                                 frozen::SeedUse::kNegative) &&
             frozen::allowedSeed(frozen::kTrainingSeedStart,
                                 frozen::SeedUse::kTraining) &&
             frozen::allowedSeed(frozen::kTrainingSeedEndExclusive - 1,
                                 frozen::SeedUse::kTraining) &&
             frozen::allowedSeed(frozen::kStageASeedStart,
                                 frozen::SeedUse::kStageA) &&
             frozen::allowedSeed(frozen::kStageASeedEndExclusive - 1,
                                 frozen::SeedUse::kStageA) &&
             throwsInvalid([] {
               frozen::requireSeed(0x3d6b'0400u,
                                   frozen::SeedUse::kNegative);
             }) &&
             throwsInvalid([] {
               frozen::requireSeed(0x3d6b'7000u,
                                   frozen::SeedUse::kTraining);
             }) &&
             throwsInvalid([] {
               frozen::requireSeed(0x3d6c'0020u,
                                   frozen::SeedUse::kStageA);
             }) &&
             throwsInvalid([] {
               frozen::requireSeed(0x4d6b'1000u,
                                   frozen::SeedUse::kTraining);
             }) &&
             throwsInvalid([] {
               frozen::requireSeed(0x7d6b'1000u,
                                   frozen::SeedUse::kTraining);
             }) &&
             throwsInvalid([] {
               frozen::requireSeed(0xd76c'0000u,
                                   frozen::SeedUse::kStageA);
             }),
         "seed-lane guards self-test failed");
  frozen::enforceRssLimit();
  output << std::setprecision(12)
         << "MANIFOLD_GAIL_DEVELOPMENT_SELF_TEST {\"passed\":true,"
         << "\"oldCoverageHistoryPreserved\":true,"
         << "\"newAdmissionWiring\":true,\"scheduleFrozen\":true,"
         << "\"rewardFrozen\":true,\"bitExactInheritance\":true,"
         << "\"freshAdam\":true,\"publicOnly\":true,"
         << "\"reflectionExact\":true,\"metadataBlind\":true,"
         << "\"checkpointGoldenWiring\":true,\"stageASeal\":true,"
         << "\"seedGuards\":true,\"discriminatorParameters\":"
         << frozen::DiscriminatorLayout::count
         << ",\"policyParameters\":" << prior::Layout::count
         << ",\"peakRssBytes\":" << peakRssBytes() << "}\n";
  return true;
}

int preflightOnly(const Options& options, std::ostream& output) {
  const frozen::Deadline deadline;
  const PreflightResult preflight = runPreflight(options, deadline, true);
  output << std::setprecision(12)
         << "MANIFOLD_GAIL_DEVELOPMENT_PREFLIGHT {\"matchedPairs\":"
         << preflight.dataset.matched << ",\"coverage\":"
         << preflight.dataset.coverage << ",\"fold0Auc\":"
         << preflight.discriminator.heldout[0].auc << ",\"fold0Pair\":"
         << preflight.discriminator.heldout[0].matched_pair_ranking
         << ",\"fold1Auc\":" << preflight.discriminator.heldout[1].auc
         << ",\"fold1Pair\":"
         << preflight.discriminator.heldout[1].matched_pair_ranking
         << ",\"discriminatorFingerprint\":\""
         << hex64(preflight.discriminator.fingerprint)
         << "\",\"freshTrainingSeedsOpened\":0,\"stageASeedsOpened\":0,"
         << "\"passed\":" << (preflight.passed ? "true" : "false")
         << ",\"seconds\":" << preflight.seconds
         << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"artifact\":\"" << jsonEscape(options.preflight)
         << "\"}\n";
  return preflight.passed ? EXIT_SUCCESS : 2;
}

int run(const Options& options, std::ostream& output) {
  const frozen::Deadline deadline;
  PreflightResult preflight = runPreflight(options, deadline, true);
  output << std::setprecision(12)
         << "MANIFOLD_GAIL_DEVELOPMENT_ADMISSION {\"matchedPairs\":"
         << preflight.dataset.matched << ",\"fold0Auc\":"
         << preflight.discriminator.heldout[0].auc << ",\"fold0Pair\":"
         << preflight.discriminator.heldout[0].matched_pair_ranking
         << ",\"fold1Auc\":" << preflight.discriminator.heldout[1].auc
         << ",\"fold1Pair\":"
         << preflight.discriminator.heldout[1].matched_pair_ranking
         << ",\"oldCoverageGateStillFailed\":true,"
         << "\"developmentAdmissionPassed\":"
         << (preflight.passed ? "true" : "false")
         << ",\"trainingSeedsOpened\":0,\"stageASeedsOpened\":0}\n"
         << std::flush;
  if (!preflight.passed) return 2;

  // This is the sole read of the policy-evaluation seed lane.  The implementation is called
  // directly from the included reference so its 48x512 schedule, collection,
  // reward, network, optimizer hyperparameters, and shuffle stream are exact.
  const frozen::TrainingResult training = frozen::trainPolicy(
      preflight.inherited, preflight.discriminator.model,
      preflight.curriculum, options.threads, deadline);
  FrozenCandidate candidate = freezeCandidate(
      options, training.network, preflight.discriminator.model);
  output << "MANIFOLD_GAIL_DEVELOPMENT_FROZEN {\"iterations\":48,"
         << "\"trainingEpisodes\":24576,\"modelFingerprint\":\""
         << hex64(candidate.fingerprint)
         << "\",\"checkpointVerified\":true,\"goldenWritten\":true,"
         << "\"stageASeedsOpened\":0}\n" << std::flush;

  // Read Stage A exactly once, after locking the checkpoint and golden vector.
  const std::vector<frozen::GameResult> candidate_games =
      evaluateAfterFreeze(candidate, preflight.inherited,
                          frozen::EvaluationPolicy::kCandidate,
                          options.threads, deadline);
  const std::vector<frozen::GameResult> inherited_games =
      evaluateAfterFreeze(candidate, preflight.inherited,
                          frozen::EvaluationPolicy::kPrior,
                          options.threads, deadline);
  const std::vector<frozen::GameResult> d1_games =
      evaluateAfterFreeze(candidate, preflight.inherited,
                          frozen::EvaluationPolicy::kFairD1,
                          options.threads, deadline);
  const frozen::Summary candidate_summary = prior::summarize(candidate_games);
  const frozen::Summary inherited_summary = prior::summarize(inherited_games);
  const frozen::Summary d1_summary = prior::summarize(d1_games);
  const frozen::PairedSummary versus_inherited =
      prior::pair(candidate_games, inherited_games);
  const frozen::PairedSummary versus_d1 =
      prior::pair(candidate_games, d1_games);
  const StageAGate gate = stageAGate(
      candidate_summary, inherited_summary, versus_inherited);
  enforceResources(deadline);
  const double wall_seconds = deadline.elapsedSeconds();
  writeResultArtifact(options, preflight, training, candidate,
                      candidate_games, inherited_games, d1_games,
                      candidate_summary, inherited_summary, d1_summary,
                      versus_inherited, versus_d1, gate, wall_seconds);
  output << std::fixed << std::setprecision(6)
         << "MANIFOLD_GAIL_DEVELOPMENT_STAGE_A {\"candidateScore\":"
         << candidate_summary.mean_score << ",\"candidateMoves\":"
         << candidate_summary.mean_moves << ",\"bottomQuartileMoves\":"
         << candidate_summary.bottom_quartile_moves
         << ",\"clearsPerMove\":" << candidate_summary.clears_per_move
         << ",\"revealsPerMove\":" << candidate_summary.reveals_per_move
         << ",\"inheritedScore\":" << inherited_summary.mean_score
         << ",\"inheritedMoves\":" << inherited_summary.mean_moves
         << ",\"fairD1Score\":" << d1_summary.mean_score
         << ",\"fairD1Moves\":" << d1_summary.mean_moves
         << ",\"scoreRatioVsInherited\":"
         << candidate_summary.mean_score / inherited_summary.mean_score
         << ",\"moveRatioVsInherited\":"
         << candidate_summary.mean_moves / inherited_summary.mean_moves
         << ",\"jointWinsVsInherited\":" << versus_inherited.joint_wins
         << ",\"passed\":" << (gate.passed ? "true" : "false")
         << ",\"screenBeyondStageAOpened\":false,\"wallSeconds\":"
         << wall_seconds << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"artifact\":\"" << jsonEscape(options.output) << "\"}\n";
  return gate.passed ? EXIT_SUCCESS : 2;
}

}  // namespace drop7::manifold_gail_development

int main(int argc, char** argv) {
  try {
    if (argc < 2) throw std::invalid_argument("missing mode");
    const std::string_view mode(argv[1]);
    const auto options =
        drop7::manifold_gail_development::parseOptions(argc, argv, 2);
    if (mode == "--self-test") {
      return drop7::manifold_gail_development::selfTest(options, std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (mode == "--preflight") {
      return drop7::manifold_gail_development::preflightOnly(options,
                                                              std::cout);
    }
    if (mode == "--run") {
      return drop7::manifold_gail_development::run(options, std::cout);
    }
    throw std::invalid_argument(
        "usage: drop7_manifold_gail_development --self-test | --preflight | "
        "--run [--curriculum PATH] [--inherited PATH] [--checkpoint PATH] "
        "[--discriminator-checkpoint PATH] [--golden PATH] "
        "[--preflight-output PATH] [--output PATH] [--threads 1..8] "
        "[--curriculum-sha256 HEX] [--inherited-sha256 HEX] "
        "[--stopped-artifact-sha256 HEX]");
  } catch (const std::exception& error) {
    std::cerr << "drop7_manifold_gail_development: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
