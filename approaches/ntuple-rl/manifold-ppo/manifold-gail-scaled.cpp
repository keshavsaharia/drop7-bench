#define main drop7_manifold_gail_development_frozen_entrypoint
#pragma push_macro("main")
#undef main
#pragma push_macro("manifold_gail_development")
#define manifold_gail_development                                             \
  _Pragma("pop_macro(\"main\")")                                           \
  _Pragma("pop_macro(\"manifold_gail_development\")")                     \
      manifold_gail_development
#include "manifold-gail-development.cpp"
#undef main

#include <atomic>
#include <fstream>
#include <future>
#include <sstream>

// Runs a scale-only manifold-shaping configuration.  It keeps the supplied
// checkpoint, discriminator, policy architecture, 48x512 schedule, mixed
// starts, PPO update, ordinary reward, and checkpoint-selection rule fixed.
// Centered-GAIL changes from .10 to .75, potential shaping changes from .15 to
// .50, and evaluation uses a disjoint seed lane fixed before evaluation.
namespace drop7::manifold_gail_scaled {

namespace development = drop7::manifold_gail_development;
namespace weak = drop7::oracle_manifold_ppo;
namespace prior = drop7::curriculum_option_ppo;
namespace vr = drop7::viability_reservoir_controller;
using PublicState = prior::PublicState;

constexpr float kGailCoefficient = 0.75f;
constexpr float kPotentialCoefficient = 0.50f;

constexpr std::uint32_t kTrainingSeedStart = 0x3d73'0000u;
constexpr int kIterations = 48;
constexpr int kEpisodesPerIteration = 512;
constexpr int kTrainingEpisodes = kIterations * kEpisodesPerIteration;
constexpr std::uint32_t kTrainingSeedEndExclusive =
    kTrainingSeedStart + kTrainingEpisodes;
constexpr std::uint32_t kStageASeedStart = 0x3d73'8000u;
constexpr int kStageAGames = 32;
constexpr std::uint32_t kStageASeedEndExclusive =
    kStageASeedStart + kStageAGames;

constexpr std::uint64_t kExpectedDiscriminatorFingerprint =
    0xb8a2'ce14'c798'f083ull;
constexpr std::string_view kLockedDiscriminatorSha256 =
    "67e794c4c0dae4fe4587e3724a4430976e0f5a44aa43111975cc3a5221dda0dd";
constexpr std::string_view kInheritedSha256 =
    "14b8c89cdc9a219480cdf74d9bb9bca4afec3b68997a7e0707077d97337cc55c";
constexpr std::string_view kCurriculumSha256 =
    "c963ac242994e7d18020fd7369954be2f4015d7f6c972f6d5fffe79c371db226";
constexpr std::string_view kWeakFailureArtifactSha256 =
    "8af279f7bf577395282df3c064b4842dd8033860c25d11cdcce55fb698cb77cf";
constexpr std::string_view kDevelopmentSourceSha256 =
    "7986dd2f634f674176185409f87f3c645aaf2a495a4e8e4fcf2f731ce2d14e04";

static_assert(weak::kGailCoefficient == 0.10f);
static_assert(weak::kPotentialCoefficient == 0.15f);
static_assert(kGailCoefficient == 0.75f);
static_assert(kPotentialCoefficient == 0.50f);
static_assert(kIterations == weak::kIterations);
static_assert(kEpisodesPerIteration == weak::kEpisodesPerIteration);
static_assert(kTrainingEpisodes == 24'576);
static_assert(kTrainingSeedEndExclusive == 0x3d73'6000u);
static_assert(kStageASeedEndExclusive == 0x3d73'8020u);
static_assert(weak::kInitialEpisodesPerIteration == 256);
static_assert(weak::kCurriculumEpisodesPerIteration == 256);
static_assert(weak::kPpoEpochs == 4 && weak::kMinibatch == 512);
static_assert(weak::kGamma == 0.999f && weak::kGaeLambda == 0.97f);
static_assert(weak::kClipRatio == 0.20f);
static_assert(weak::kEntropyCoefficient == 0.005f);
static_assert(weak::kValueCoefficient == 0.25f);
static_assert(weak::kGradientNorm == 0.50f);
static_assert(weak::kLearningRate == 0.0001f);
static_assert(weak::kSurvivalReward == 0.05f);
static_assert(weak::kClearReward == 0.05f);
static_assert(weak::kRevealReward == 0.15f);
static_assert(weak::kTerminalReward == -5.0f);
static_assert(weak::kMaximumPotential == 4.0f);
static_assert(weak::kInitialMaximumMoves == 1'000);
static_assert(weak::kCurriculumHorizon == 100);
static_assert(weak::kStageAMaximumMoves == 1'000);
static_assert(weak::kWallLimitSeconds == 60.0 * 60.0);
static_assert(weak::kRssLimitBytes == 256ull * 1024ull * 1024ull);
static_assert(prior::Layout::count == 58'312);
static_assert(weak::DiscriminatorLayout::count == 7'129);

struct Options {
  std::string curriculum = "/tmp/drop7-oracle-curriculum-states.jsonl";
  std::string inherited = "/tmp/drop7-curriculum-option-ppo.bin";
  std::string discriminator = "/tmp/drop7-manifold-gail-discriminator.bin";
  std::string checkpoint = "/tmp/drop7-manifold-gail-scaled.bin";
  std::string golden = "/tmp/drop7-manifold-gail-scaled-golden.json";
  std::string preflight = "/tmp/drop7-manifold-gail-scaled-preflight.json";
  std::string output = "/tmp/drop7-manifold-gail-scaled-stage-a.json";
  std::string discriminator_sha256 =
      std::string(kLockedDiscriminatorSha256);
  std::string inherited_sha256 = std::string(kInheritedSha256);
  std::string curriculum_sha256 = std::string(kCurriculumSha256);
  std::string weak_failure_sha256 = std::string(kWeakFailureArtifactSha256);
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
    else if (flag == "--discriminator") result.discriminator = value();
    else if (flag == "--checkpoint") result.checkpoint = value();
    else if (flag == "--golden") result.golden = value();
    else if (flag == "--preflight-output") result.preflight = value();
    else if (flag == "--output") result.output = value();
    else if (flag == "--discriminator-sha256") {
      result.discriminator_sha256 = value();
    } else if (flag == "--inherited-sha256") {
      result.inherited_sha256 = value();
    } else if (flag == "--curriculum-sha256") {
      result.curriculum_sha256 = value();
    } else if (flag == "--weak-failure-sha256") {
      result.weak_failure_sha256 = value();
    } else if (flag == "--threads") {
      result.threads = std::stoi(value());
    } else {
      throw std::invalid_argument("unknown option " + flag);
    }
  }
  if (result.curriculum.empty() || result.inherited.empty() ||
      result.discriminator.empty() || result.checkpoint.empty() ||
      result.golden.empty() || result.preflight.empty() ||
      result.output.empty() || result.threads < 1 ||
      result.threads > weak::kMaximumThreads ||
      result.discriminator_sha256 != kLockedDiscriminatorSha256 ||
      result.inherited_sha256 != kInheritedSha256 ||
      result.curriculum_sha256 != kCurriculumSha256 ||
      result.weak_failure_sha256 != kWeakFailureArtifactSha256) {
    throw std::invalid_argument("invalid or checksum-mismatched options");
  }
  return result;
}

development::Options developmentOptions(const Options& source) {
  development::Options result;
  result.curriculum = source.curriculum;
  result.inherited = source.inherited;
  result.discriminator_checkpoint = source.discriminator;
  result.preflight = source.preflight;
  result.threads = source.threads;
  return result;
}

enum class SeedUse : std::uint8_t { kTraining, kStageA };

bool allowedSeed(std::uint32_t seed, SeedUse use) {
  const std::uint32_t begin =
      use == SeedUse::kTraining ? kTrainingSeedStart : kStageASeedStart;
  const std::uint32_t end = use == SeedUse::kTraining
                                ? kTrainingSeedEndExclusive
                                : kStageASeedEndExclusive;
  const std::uint8_t prefix = static_cast<std::uint8_t>(seed >> 24u);
  return seed >= begin && seed < end && prefix != 0x4d && prefix != 0x7d &&
      prefix != 0xd7;
}

void requireSeed(std::uint32_t seed, SeedUse use) {
  if (!allowedSeed(seed, use)) {
    throw std::invalid_argument("seed outside scaled manifold lane");
  }
}

std::string jsonEscape(std::string_view value) {
  return development::jsonEscape(value);
}

std::string hex64(std::uint64_t value) {
  return development::hex64(value);
}

float scaledManifoldReward(float current_logit, float next_logit,
                           bool terminal) {
  const float next_probability =
      terminal ? 0.0f : weak::Discriminator::sigmoid(next_logit);
  const float centered_gail = terminal
      ? 0.0f
      : -std::log(std::max(1.0e-5f, 1.0f - next_probability)) -
            std::log(2.0f);
  const float current_phi = weak::clippedPotential(current_logit);
  const float next_phi = terminal ? 0.0f : weak::clippedPotential(next_logit);
  return kGailCoefficient * std::clamp(centered_gail, -0.5f, 2.0f) +
      kPotentialCoefficient * (weak::kGamma * next_phi - current_phi);
}

weak::Trajectory collectScaledTrajectory(
    const prior::Network& network,
    const weak::Discriminator& discriminator,
    const prior::Curriculum& curriculum, std::uint32_t lane_seed,
    bool use_curriculum, const weak::Deadline& deadline) {
  requireSeed(lane_seed, SeedUse::kTraining);
  const std::size_t curriculum_index = static_cast<std::size_t>(
      mix32(lane_seed ^ weak::kCurriculumSelectDomain)) %
      curriculum.states.size();
  State state = use_curriculum
      ? vr::materialize(curriculum.states[curriculum_index])
      : initialHeadlessState(lane_seed);
  state.score = 0;
  state.level = 1;
  state.moves_played = 0;
  const std::uint32_t restart_seed =
      weak::restartBaseSeed(lane_seed, curriculum_index);
  Mulberry32 policy_random(mix32(lane_seed ^ weak::kPolicySampleDomain));
  const int horizon = use_curriculum ? weak::kCurriculumHorizon
                                     : weak::kInitialMaximumMoves;
  weak::Trajectory trajectory;
  trajectory.curriculum = use_curriculum;
  trajectory.samples.reserve(use_curriculum ? weak::kCurriculumHorizon : 128);
  for (int event = 0; !state.game_over && event < horizon; ++event) {
    if ((event & 31) == 0) deadline.check();
    bool mirrored = false;
    const PublicState canonical =
        vr::canonicalState(vr::publicState(state), mirrored);
    const prior::BasePolicy base = prior::fairBasePolicy(canonical);
    const prior::Prediction prediction =
        prior::predictCanonical(network, canonical, &base);
    weak::Sample sample;
    sample.state = canonical;
    sample.base_logits = base.logits;
    sample.action = prior::sampleCanonical(prediction, policy_random);
    if (sample.action < 0) {
      throw std::runtime_error("scaled PPO sampled no action");
    }
    sample.old_log_probability = std::log(std::max(
        1.0e-12f, prediction.probabilities[sample.action]));
    sample.old_value = prediction.value;
    const float current_logit = discriminator.logit(canonical);
    const int physical_action =
        mirrored ? kBoardSize - 1 - sample.action : sample.action;
    MoveResult move;
    const bool played = use_curriculum
        ? weak::playRestartMove(state, restart_seed, event, physical_action,
                                move)
        : playHeadlessMove(state, lane_seed, physical_action, move);
    if (!played) throw std::runtime_error("scaled PPO transition failed");
    int clears = 0;
    int reveals = 0;
    prior::accumulateMoveCounts(move, clears, reveals);
    sample.terminal = state.game_over;
    float next_logit = 0.0f;
    if (!sample.terminal) {
      bool ignored = false;
      const PublicState next =
          vr::canonicalState(vr::publicState(state), ignored);
      next_logit = discriminator.logit(next);
      trajectory.discriminator_probability +=
          weak::Discriminator::sigmoid(next_logit);
    }
    const float shaping =
        scaledManifoldReward(current_logit, next_logit, sample.terminal);
    sample.reward = static_cast<float>(move.score_delta) / 17'000.0f +
        (sample.terminal ? 0.0f : weak::kSurvivalReward) +
        weak::kClearReward * clears + weak::kRevealReward * reveals +
        (sample.terminal ? weak::kTerminalReward : 0.0f) + shaping;
    trajectory.manifold_reward += shaping;
    trajectory.clears += clears;
    trajectory.reveals += reveals;
    trajectory.samples.push_back(sample);
  }
  float bootstrap = 0.0f;
  if (!state.game_over) {
    bool ignored = false;
    const PublicState canonical =
        vr::canonicalState(vr::publicState(state), ignored);
    bootstrap = prior::predictCanonical(network, canonical).value;
  }
  weak::finishAdvantages(trajectory.samples, bootstrap);
  trajectory.score = state.score;
  trajectory.moves = static_cast<int>(trajectory.samples.size());
  return trajectory;
}

weak::Batch collectScaledBatch(
    const prior::Network& network,
    const weak::Discriminator& discriminator,
    const prior::Curriculum& curriculum, int iteration, int threads,
    const weak::Deadline& deadline) {
  weak::Batch batch;
  batch.trajectories.resize(kEpisodesPerIteration);
  std::atomic<int> next{0};
  std::vector<std::future<void>> futures;
  const int workers = std::min(threads, kEpisodesPerIteration);
  for (int worker = 0; worker < workers; ++worker) {
    futures.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int episode = next.fetch_add(1);
        if (episode >= kEpisodesPerIteration) return;
        const int global_episode =
            iteration * kEpisodesPerIteration + episode;
        const std::uint32_t lane_seed =
            kTrainingSeedStart + static_cast<std::uint32_t>(global_episode);
        const bool use_curriculum =
            episode >= weak::kInitialEpisodesPerIteration;
        batch.trajectories[episode] = collectScaledTrajectory(
            network, discriminator, curriculum, lane_seed, use_curriculum,
            deadline);
      }
    }));
  }
  for (auto& future : futures) future.get();
  std::int64_t total_moves = 0;
  std::int64_t total_clears = 0;
  std::int64_t total_reveals = 0;
  for (const weak::Trajectory& trajectory : batch.trajectories) {
    batch.samples += trajectory.samples.size();
    total_moves += trajectory.moves;
    total_clears += trajectory.clears;
    total_reveals += trajectory.reveals;
    batch.discriminator_probability += trajectory.discriminator_probability;
    batch.manifold_reward += trajectory.manifold_reward;
    if (trajectory.curriculum) {
      batch.curriculum_score += trajectory.score;
      batch.curriculum_moves += trajectory.moves;
    } else {
      batch.initial_score += trajectory.score;
      batch.initial_moves += trajectory.moves;
    }
  }
  if (batch.samples > weak::kMaximumBatchSamples || total_moves <= 0) {
    throw std::runtime_error("scaled PPO batch exceeded sample bound");
  }
  batch.initial_score /= weak::kInitialEpisodesPerIteration;
  batch.initial_moves /= weak::kInitialEpisodesPerIteration;
  batch.curriculum_score /= weak::kCurriculumEpisodesPerIteration;
  batch.curriculum_moves /= weak::kCurriculumEpisodesPerIteration;
  batch.clears_per_move = static_cast<double>(total_clears) / total_moves;
  batch.reveals_per_move = static_cast<double>(total_reveals) / total_moves;
  batch.discriminator_probability /= total_moves;
  batch.manifold_reward /= total_moves;
  weak::enforceRssLimit();
  return batch;
}

weak::TrainingResult trainScaledPolicy(
    const prior::Network& inherited,
    const weak::Discriminator& discriminator,
    const prior::Curriculum& curriculum, int threads,
    const weak::Deadline& deadline) {
  weak::TrainingResult result;
  result.network.setParameters(inherited.parameters());
  if (result.network.parameters() != inherited.parameters() ||
      prior::modelFingerprint(result.network) !=
          weak::kExpectedPriorFingerprint) {
    throw std::runtime_error("scaled PPO inheritance was not bit exact");
  }
  Mulberry32 shuffle_random(weak::kPolicyShuffleSeed);
  for (int iteration = 0; iteration < kIterations; ++iteration) {
    deadline.check();
    weak::Batch batch = collectScaledBatch(
        result.network, discriminator, curriculum, iteration, threads,
        deadline);
    weak::TrainingRecord record;
    record.iteration = iteration + 1;
    record.samples = batch.samples;
    record.initial_score = batch.initial_score;
    record.initial_moves = batch.initial_moves;
    record.curriculum_score = batch.curriculum_score;
    record.curriculum_moves = batch.curriculum_moves;
    record.clears_per_move = batch.clears_per_move;
    record.reveals_per_move = batch.reveals_per_move;
    record.discriminator_probability = batch.discriminator_probability;
    record.manifold_reward = batch.manifold_reward;
    record.update = weak::update(
        result.network, batch, shuffle_random, deadline);
    result.records[iteration] = record;
    result.moves += batch.samples;
    std::cerr << std::fixed << std::setprecision(3)
              << "scaled-manifold-ppo iteration " << record.iteration << '/'
              << kIterations << " samples " << record.samples << " initial "
              << record.initial_score << '/' << record.initial_moves
              << " curriculum " << record.curriculum_score << '/'
              << record.curriculum_moves << " flow "
              << record.clears_per_move << '/' << record.reveals_per_move
              << " manifold " << record.discriminator_probability << '/'
              << record.manifold_reward << " entropy "
              << record.update.entropy << " rss " << weak::peakRssBytes()
              << '\n';
  }
  return result;
}

weak::GameResult playStageAGame(
    const prior::Network& candidate, const prior::Network& inherited,
    std::uint32_t seed, weak::EvaluationPolicy policy,
    const weak::Deadline& deadline) {
  requireSeed(seed, SeedUse::kStageA);
  State state = initialHeadlessState(seed);
  weak::GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < weak::kStageAMaximumMoves) {
    if ((state.moves_played & 31) == 0) deadline.check();
    const PublicState public_state = vr::publicState(state);
    int action = -1;
    if (policy == weak::EvaluationPolicy::kCandidate) {
      action = prior::chooseAction(public_state, candidate).action;
    } else if (policy == weak::EvaluationPolicy::kPrior) {
      action = prior::chooseAction(public_state, inherited).action;
    } else {
      action = vr::chooseFairDepthOne(public_state).action;
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("scaled Stage-A selected illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("scaled Stage-A transition failed");
    }
    for (const Wave& wave : move.waves) {
      result.clears += wave.cleared;
      result.reveals += wave.revealed;
      result.maximum_chain = std::max(result.maximum_chain, wave.depth);
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.capped = !state.game_over;
  return result;
}

std::vector<weak::GameResult> evaluateStageA(
    const development::FrozenCandidate& candidate,
    const prior::Network& inherited, weak::EvaluationPolicy policy,
    int threads, const weak::Deadline& deadline) {
  if (!candidate.checkpoint_verified || !candidate.golden_written) {
    throw std::runtime_error("scaled Stage-A opened before freeze");
  }
  std::vector<weak::GameResult> games(kStageAGames);
  std::atomic<int> next{0};
  std::vector<std::future<void>> futures;
  const int workers = std::min(threads, kStageAGames);
  for (int worker = 0; worker < workers; ++worker) {
    futures.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next.fetch_add(1);
        if (game >= kStageAGames) return;
        games[game] = playStageAGame(
            candidate.network, inherited,
            kStageASeedStart + static_cast<std::uint32_t>(game), policy,
            deadline);
      }
    }));
  }
  for (auto& future : futures) future.get();
  weak::enforceRssLimit();
  return games;
}

void writeScaledPreflight(
    const Options& options,
    const development::PreflightResult& preflight) {
  std::ofstream output(options.preflight, std::ios::trunc);
  if (!output) throw std::runtime_error("could not write scaled preflight");
  output << std::setprecision(12)
         << "{\"format\":\"drop7-manifold-gail-scaled-preflight-v1\","
         << "\"weakShapingHistory\":{\"status\":\"stage-a-failed\","
            "\"artifactSha256\":\"" << options.weak_failure_sha256
         << "\",\"initializedFromFailedCandidate\":false},"
         << "\"replayOnly\":true,\"freshTrainingSeedsOpened\":0,"
            "\"stageASeedsOpened\":0,\"matchedPairs\":"
         << preflight.dataset.matched << ",\"matchFingerprint\":\""
         << hex64(preflight.dataset.fingerprint) << "\",\"heldout\":[";
  development::writeDevelopmentLabelMetrics(
      output, preflight.discriminator.heldout[0]);
  output << ',';
  development::writeDevelopmentLabelMetrics(
      output, preflight.discriminator.heldout[1]);
  output << "],\"lockedDiscriminator\":{\"path\":\""
         << jsonEscape(options.discriminator) << "\",\"sha256\":\""
         << options.discriminator_sha256 << "\",\"fingerprint\":\""
         << hex64(preflight.discriminator.fingerprint)
         << "\",\"refitMatchesLocked\":"
         << (preflight.discriminator.fingerprint ==
                     kExpectedDiscriminatorFingerprint
                 ? "true"
                 : "false")
         << "},\"inheritance\":{\"path\":\""
         << jsonEscape(options.inherited) << "\",\"sha256\":\""
         << options.inherited_sha256
         << "\",\"freshAdam\":true,\"failedCandidateUsed\":false},"
         << "\"changeControl\":{\"centeredGail\":{\"old\":0.10,"
            "\"new\":" << kGailCoefficient
         << "},\"potential\":{\"old\":0.15,\"new\":"
         << kPotentialCoefficient
         << "},\"allOtherArchitectureSchedulePpoAndRewardTermsFrozen\":true},"
         << "\"futureSeedSeal\":{\"training\":\"0x3d730000..0x3d735fff unopened\","
            "\"stageA\":\"0x3d738000..0x3d73801f unopened\","
            "\"protected\":\"unopened\"},\"seconds\":"
         << preflight.seconds << ",\"peakRssBytes\":"
         << weak::peakRssBytes() << ",\"passed\":"
         << (preflight.passed &&
                     preflight.discriminator.fingerprint ==
                         kExpectedDiscriminatorFingerprint
                 ? "true"
                 : "false")
         << "}\n";
  if (!output) throw std::runtime_error("scaled preflight write failed");
}

development::PreflightResult runScaledPreflight(
    const Options& options, const weak::Deadline& deadline) {
  development::Options base = developmentOptions(options);
  development::PreflightResult result =
      development::runPreflight(base, deadline, false);
  if (!result.passed ||
      result.discriminator.fingerprint !=
          kExpectedDiscriminatorFingerprint) {
    throw std::runtime_error("scaled replay-only admission failed");
  }
  development::verifyDiscriminatorCheckpoint(
      options.discriminator, result.discriminator.model,
      result.dataset.fingerprint);
  writeScaledPreflight(options, result);
  return result;
}

void writeArtifact(
    const Options& options,
    const development::PreflightResult& preflight,
    const weak::TrainingResult& training,
    const development::FrozenCandidate& candidate,
    const std::vector<weak::GameResult>& candidate_games,
    const std::vector<weak::GameResult>& inherited_games,
    const std::vector<weak::GameResult>& d1_games,
    const weak::Summary& candidate_summary,
    const weak::Summary& inherited_summary,
    const weak::Summary& d1_summary,
    const weak::PairedSummary& versus_inherited,
    const weak::PairedSummary& versus_d1,
    const development::StageAGate& gate, double wall_seconds) {
  std::ofstream output(options.output, std::ios::trunc);
  if (!output) throw std::runtime_error("could not write scaled artifact");
  output << std::setprecision(12)
         << "{\n  \"format\":\"drop7-manifold-gail-scaled-v1\",\n"
         << "  \"priorFailurePreserved\":{\"artifactSha256\":\""
         << options.weak_failure_sha256
         << "\",\"candidateCheckpointUsed\":false},\n"
         << "  \"lockedInputs\":{\"curriculumSha256\":\""
         << options.curriculum_sha256 << "\",\"inheritedSha256\":\""
         << options.inherited_sha256
         << "\",\"discriminatorSha256\":\""
         << options.discriminator_sha256
         << "\",\"developmentSourceSha256\":\""
         << kDevelopmentSourceSha256 << "\"},\n"
         << "  \"discriminator\":{\"matchedPairs\":"
         << preflight.dataset.matched << ",\"fingerprint\":\""
         << hex64(preflight.discriminator.fingerprint)
         << "\",\"architecture\":\"295-24-1\"},\n"
         << "  \"changeControl\":{\"centeredGailCoefficient\":{\"old\":0.10,"
            "\"new\":" << kGailCoefficient
         << "},\"potentialCoefficient\":{\"old\":0.15,\"new\":"
         << kPotentialCoefficient
         << "},\"allOtherRewardTermsFrozen\":true,"
            "\"architectureFrozen\":true,\"ppoFrozen\":true,"
            "\"scheduleFrozen\":true,\"noCheckpointSelection\":true},\n"
         << "  \"inheritance\":{\"fingerprint\":\""
         << hex64(weak::kExpectedPriorFingerprint)
         << "\",\"bitExact\":true,\"freshAdam\":true,"
            "\"failedCandidateUsed\":false},\n"
         << "  \"training\":{\"iterations\":48,\"episodesPerIteration\":512,"
            "\"totalEpisodes\":24576,\"mixedStarts\":\"256 initial + 256 curriculum\","
            "\"seedLane\":\"0x3d730000..0x3d735fff\","
            "\"learningCurve\":[";
  for (int iteration = 0; iteration < kIterations; ++iteration) {
    if (iteration) output << ',';
    const weak::TrainingRecord& record = training.records[iteration];
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
         << hex64(candidate.fingerprint)
         << "\",\"verifiedBeforeStageA\":true,\"golden\":\""
         << jsonEscape(options.golden) << "\"},\n"
         << "  \"stageA\":{\"seedLane\":\"0x3d738000..0x3d73801f\","
            "\"maximumMoves\":1000,\"candidate\":";
  development::writeDevelopmentSummary(output, candidate_summary);
  output << ",\"inherited\":";
  development::writeDevelopmentSummary(output, inherited_summary);
  output << ",\"fairD1\":";
  development::writeDevelopmentSummary(output, d1_summary);
  output << ",\"versusInherited\":";
  development::writeDevelopmentPaired(output, versus_inherited);
  output << ",\"versusFairD1\":";
  development::writeDevelopmentPaired(output, versus_d1);
  output << ",\"scoreRatioVsInherited\":"
         << candidate_summary.mean_score / inherited_summary.mean_score
         << ",\"moveRatioVsInherited\":"
         << candidate_summary.mean_moves / inherited_summary.mean_moves
         << ",\"gate\":";
  development::writeGate(output, gate);
  output << ",\"games\":";
  development::writeGameTriples(
      output, candidate_games, inherited_games, d1_games);
  output << "},\n  \"seedAudit\":{\"negativeReplayOnly\":\"0x3d6b0000..0x3d6b03ff\","
            "\"trainingOnly\":\"0x3d730000..0x3d735fff\","
            "\"stageAOnly\":\"0x3d738000..0x3d73801f\","
            "\"otherSeedsOpened\":false,\"protected4d7dd7Opened\":false,"
            "\"laterScreenOpened\":false},\n"
         << "  \"resources\":{\"wallSeconds\":" << wall_seconds
         << ",\"peakRssBytes\":" << weak::peakRssBytes()
         << ",\"wallLimitSeconds\":" << weak::kWallLimitSeconds
         << ",\"rssLimitBytes\":" << weak::kRssLimitBytes << "},\n"
         << "  \"passed\":" << (gate.passed ? "true" : "false")
         << "\n}\n";
  if (!output) throw std::runtime_error("scaled artifact write failed");
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
  expect(curriculum.fingerprint == weak::kExpectedCurriculumFingerprint &&
             curriculum.states.size() == 4'096,
         "scaled curriculum checksum self-test failed");
  prior::Network initialized;
  initialized.setParameters(inherited.parameters());
  expect(inherited.parameters() == initialized.parameters() &&
             prior::modelFingerprint(inherited) ==
                 weak::kExpectedPriorFingerprint &&
             prior::modelFingerprint(initialized) ==
                 weak::kExpectedPriorFingerprint,
         "scaled bit-exact fresh-Adam initialization self-test failed");

  const PublicState fixture = prior::fixtureState();
  expect(weak::topologyFeatures(fixture) ==
             weak::topologyFeatures(vr::mirror(fixture)),
         "scaled discriminator reflection self-test failed");
  const prior::PolicyDecision direct = prior::chooseAction(fixture, initialized);
  const prior::PolicyDecision reflected =
      prior::chooseAction(vr::mirror(fixture), initialized);
  expect(reflected.action == kBoardSize - 1 - direct.action,
         "scaled policy reflection self-test failed");
  State metadata = vr::materialize(fixture);
  metadata.score = 88'888'888;
  metadata.level = 888;
  metadata.moves_played = 888;
  expect(vr::publicState(metadata) == fixture &&
             prior::chooseAction(vr::publicState(metadata), initialized) ==
                 direct,
         "scaled public-only metadata self-test failed");

  const float current = 0.7f;
  const float next = 1.1f;
  const float centered = std::clamp(
      -std::log(1.0f - weak::Discriminator::sigmoid(next)) - std::log(2.0f),
      -0.5f, 2.0f);
  const float manual = kGailCoefficient * centered +
      kPotentialCoefficient *
          (weak::kGamma * weak::clippedPotential(next) -
           weak::clippedPotential(current));
  expect(std::abs(scaledManifoldReward(current, next, false) - manual) <
                 1.0e-7f &&
             scaledManifoldReward(current, next, false) !=
                 weak::manifoldReward(current, next, false),
         "scaled reward coefficient self-test failed");

  weak::Summary candidate;
  candidate.mean_score = 300'000;
  candidate.mean_moves = 90;
  candidate.bottom_quartile_moves = 45;
  candidate.clears_per_move = 1.95;
  candidate.reveals_per_move = 1.08;
  weak::Summary baseline;
  baseline.mean_score = 240'000;
  baseline.mean_moves = 72;
  weak::PairedSummary paired;
  paired.joint_wins = 20;
  expect(development::stageAGate(candidate, baseline, paired).passed,
         "scaled positive gate self-test failed");
  candidate.reveals_per_move = 1.0799;
  expect(!development::stageAGate(candidate, baseline, paired).passed,
         "scaled negative gate self-test failed");

  expect(allowedSeed(kTrainingSeedStart, SeedUse::kTraining) &&
             allowedSeed(kTrainingSeedEndExclusive - 1,
                         SeedUse::kTraining) &&
             allowedSeed(kStageASeedStart, SeedUse::kStageA) &&
             allowedSeed(kStageASeedEndExclusive - 1, SeedUse::kStageA) &&
             throwsInvalid([] {
               requireSeed(0x3d72'ffffu, SeedUse::kTraining);
             }) &&
             throwsInvalid([] {
               requireSeed(0x3d73'6000u, SeedUse::kTraining);
             }) &&
             throwsInvalid([] {
               requireSeed(0x3d73'7fffu, SeedUse::kStageA);
             }) &&
             throwsInvalid([] {
               requireSeed(0x3d73'8020u, SeedUse::kStageA);
             }) &&
             throwsInvalid([] {
               requireSeed(0x4d73'0000u, SeedUse::kTraining);
             }) &&
             throwsInvalid([] {
               requireSeed(0x7d73'0000u, SeedUse::kTraining);
             }) &&
             throwsInvalid([] {
               requireSeed(0xd773'8000u, SeedUse::kStageA);
             }),
         "scaled seed guards self-test failed");
  weak::enforceRssLimit();
  output << std::setprecision(12)
         << "MANIFOLD_GAIL_SCALED_SELF_TEST {\"passed\":true,"
         << "\"weakFailurePreserved\":true,\"failedCandidateUnused\":true,"
         << "\"onlyCoefficientChanges\":true,\"gailCoefficient\":"
         << kGailCoefficient << ",\"potentialCoefficient\":"
         << kPotentialCoefficient << ",\"bitExactInheritance\":true,"
         << "\"freshAdam\":true,\"scheduleFrozen\":true,"
         << "\"architectureFrozen\":true,\"ppoFrozen\":true,"
         << "\"ordinaryRewardFrozen\":true,\"publicOnly\":true,"
         << "\"reflectionExact\":true,\"metadataBlind\":true,"
         << "\"stageAGateFrozen\":true,\"seedGuards\":true,"
         << "\"peakRssBytes\":" << weak::peakRssBytes() << "}\n";
  return true;
}

int preflightOnly(const Options& options, std::ostream& output) {
  const weak::Deadline deadline;
  const development::PreflightResult preflight =
      runScaledPreflight(options, deadline);
  output << std::setprecision(12)
         << "MANIFOLD_GAIL_SCALED_PREFLIGHT {\"matchedPairs\":"
         << preflight.dataset.matched << ",\"fold0Auc\":"
         << preflight.discriminator.heldout[0].auc << ",\"fold0Pair\":"
         << preflight.discriminator.heldout[0].matched_pair_ranking
         << ",\"fold1Auc\":" << preflight.discriminator.heldout[1].auc
         << ",\"fold1Pair\":"
         << preflight.discriminator.heldout[1].matched_pair_ranking
         << ",\"discriminatorFingerprint\":\""
         << hex64(preflight.discriminator.fingerprint)
         << "\",\"freshTrainingSeedsOpened\":0,\"stageASeedsOpened\":0,"
         << "\"passed\":true,\"seconds\":" << preflight.seconds
         << ",\"peakRssBytes\":" << weak::peakRssBytes()
         << ",\"artifact\":\"" << jsonEscape(options.preflight)
         << "\"}\n";
  return 0;
}

int run(const Options& options, std::ostream& output) {
  const weak::Deadline deadline;
  development::PreflightResult preflight =
      runScaledPreflight(options, deadline);
  output << "MANIFOLD_GAIL_SCALED_ADMISSION {\"matchedPairs\":"
         << preflight.dataset.matched << ",\"discriminatorFingerprint\":\""
         << hex64(preflight.discriminator.fingerprint)
         << "\",\"weakFailurePreserved\":true,"
         << "\"failedCandidateUsed\":false,\"trainingSeedsOpened\":0,"
         << "\"stageASeedsOpened\":0,\"passed\":true}\n" << std::flush;

  const weak::TrainingResult training = trainScaledPolicy(
      preflight.inherited, preflight.discriminator.model,
      preflight.curriculum, options.threads, deadline);
  development::Options freeze_options = developmentOptions(options);
  freeze_options.checkpoint = options.checkpoint;
  freeze_options.golden = options.golden;
  development::FrozenCandidate candidate = development::freezeCandidate(
      freeze_options, training.network, preflight.discriminator.model);
  output << "MANIFOLD_GAIL_SCALED_FROZEN {\"iterations\":48,"
         << "\"trainingEpisodes\":24576,\"modelFingerprint\":\""
         << hex64(candidate.fingerprint)
         << "\",\"checkpointVerified\":true,\"goldenWritten\":true,"
         << "\"stageASeedsOpened\":0}\n" << std::flush;

  const std::vector<weak::GameResult> candidate_games = evaluateStageA(
      candidate, preflight.inherited, weak::EvaluationPolicy::kCandidate,
      options.threads, deadline);
  const std::vector<weak::GameResult> inherited_games = evaluateStageA(
      candidate, preflight.inherited, weak::EvaluationPolicy::kPrior,
      options.threads, deadline);
  const std::vector<weak::GameResult> d1_games = evaluateStageA(
      candidate, preflight.inherited, weak::EvaluationPolicy::kFairD1,
      options.threads, deadline);
  const weak::Summary candidate_summary = prior::summarize(candidate_games);
  const weak::Summary inherited_summary = prior::summarize(inherited_games);
  const weak::Summary d1_summary = prior::summarize(d1_games);
  const weak::PairedSummary versus_inherited =
      prior::pair(candidate_games, inherited_games);
  const weak::PairedSummary versus_d1 = prior::pair(candidate_games, d1_games);
  const development::StageAGate gate = development::stageAGate(
      candidate_summary, inherited_summary, versus_inherited);
  development::enforceResources(deadline);
  const double wall_seconds = deadline.elapsedSeconds();
  writeArtifact(options, preflight, training, candidate, candidate_games,
                inherited_games, d1_games, candidate_summary,
                inherited_summary, d1_summary, versus_inherited, versus_d1,
                gate, wall_seconds);
  output << std::fixed << std::setprecision(6)
         << "MANIFOLD_GAIL_SCALED_STAGE_A {\"candidateScore\":"
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
         << ",\"laterScreenOpened\":false,\"wallSeconds\":"
         << wall_seconds << ",\"peakRssBytes\":" << weak::peakRssBytes()
         << ",\"artifact\":\"" << jsonEscape(options.output) << "\"}\n";
  return gate.passed ? 0 : 2;
}

}  // namespace drop7::manifold_gail_scaled

int main(int argc, char** argv) {
  try {
    if (argc < 2) throw std::invalid_argument("missing mode");
    const std::string_view mode(argv[1]);
    const auto options =
        drop7::manifold_gail_scaled::parseOptions(argc, argv, 2);
    if (mode == "--self-test") {
      return drop7::manifold_gail_scaled::selfTest(options, std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (mode == "--preflight") {
      return drop7::manifold_gail_scaled::preflightOnly(options, std::cout);
    }
    if (mode == "--run") {
      return drop7::manifold_gail_scaled::run(options, std::cout);
    }
    throw std::invalid_argument(
        "usage: drop7_manifold_gail_scaled --self-test | --preflight | --run "
        "[--curriculum PATH] [--inherited PATH] [--discriminator PATH] "
        "[--checkpoint PATH] [--golden PATH] [--preflight-output PATH] "
        "[--output PATH] [--threads 1..8]");
  } catch (const std::exception& error) {
    std::cerr << "drop7_manifold_gail_scaled: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
