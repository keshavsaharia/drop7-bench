// Runs tail-focused complete-game CEM for the fixed public phase evaluator.
//
// It preserves the 165-weight public policy and the exact D3 /
// internal-width-two / three-stratum selective expectimax mechanism, while
// ranking complete-game CRN batches with a survival-curve utility fixed before
// evaluation.  No proxy labels or private headless metadata enter a decision.

#pragma push_macro("main")
#undef main
#define main drop7_evo_public_policy_frozen_entrypoint
#include "../../heuristic-search/evolved-public-policy/evo-public-policy.cpp"
#pragma pop_macro("main")

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#endif

namespace drop7::tail_survival_cem {

namespace evo = drop7::evo_public_policy;

using Clock = std::chrono::steady_clock;
using GameResult = evo::GameResult;
using Weights = evo::Weights;

// Frozen inputs.
constexpr std::string_view kInputCheckpoint =
    "/tmp/drop7-evo-public-phase-v2.bin";
constexpr std::string_view kInputCheckpointSha256 =
    "d3de888162a02b658e8ebcbb10091022a7539a4d6917ac9218b45c53be1ae10a";
constexpr std::uint64_t kInputFingerprint = 0xbe99'd8ca'57c6'4ea2ull;
constexpr int kInputStoredSamples = 7;
constexpr std::string_view kInputSourceSha256 =
    "939298e1616f2ebbf76297439006db19f8e2c7d95cd2ab26c53cd30969bce1c3";
constexpr std::string_view kBehaviorSha256 =
    "e5e81fa103589a9a911b6019a15aa48339c78ae2460ea0b0df4b0f66d59f27df";
constexpr std::string_view kEngineSha256 =
    "b6dcde5f40dc39c6931b9a88e42bb351acd6fadaddd1e07691c41a82e44f3090";

// Frozen selective expectimax configuration.  chooseSearchAction evaluates
// every legal root; width applies only below the root.
constexpr int kSearchDepth = 3;
constexpr int kInternalWidth = 2;
constexpr int kStrata = 3;
constexpr int kMaximumMoves = 500;

// Frozen diagonal-CEM schedule and bounds.
constexpr int kGenerations = 32;
constexpr int kPopulation = 33;
constexpr int kElite = 8;
constexpr int kBatchGames = 32;
constexpr int kTournamentGames = 256;
constexpr int kThreads = 8;
constexpr double kMinimumWeight = -120.0;
constexpr double kMaximumWeight = 40.0;
constexpr double kMeanRetention = 0.25;
constexpr double kEliteMeanRate = 0.75;
constexpr double kSigmaRetention = 0.30;
constexpr double kEliteSigmaRate = 0.70;
constexpr double kMinimumSigma = 0.025;
constexpr double kMaximumSigma = 6.0;
constexpr std::uint64_t kOptimizerSeed = 0x7461'696c'2d63'656dull;

// Exact seed partitions.  Preflight can read only the documented,
// previously evaluated mechanism-ablation replay; it is not fitting evidence.
constexpr std::uint32_t kReplayStart = 0x3d51'0900u;
constexpr std::uint32_t kReplayEnd = 0x3d51'0907u;
constexpr std::uint32_t kFittingAllowStart = 0x3d74'0000u;
constexpr std::uint32_t kFittingAllowEnd = 0x3d74'ffffu;
constexpr std::uint32_t kTournamentStart = 0x3d75'0000u;
constexpr std::uint32_t kTournamentEnd = 0x3d75'00ffu;
constexpr std::uint32_t kStageAStart = 0x3d76'0000u;
constexpr std::uint32_t kStageAEnd = 0x3d76'001fu;

// Preregistered robust survival-curve utility.  A first isolated milestone
// hit receives only 35% of its ordinary rate; the remaining 65% is unlocked
// by repeated hits.  Thus a lucky outlier cannot dominate a 32-game batch.
constexpr std::array<int, 5> kMilestones{{75, 100, 150, 225, 300}};
constexpr std::array<double, 5> kMilestoneWeights{{24.0, 40.0, 80.0,
                                                    150.0, 240.0}};
constexpr double kFirstHitRateWeight = 0.35;
constexpr double kRepeatedHitRateWeight = 0.65;
constexpr double kMeanSurvivalWeight = 0.55;
constexpr double kLowerQuartileSurvivalWeight = 0.45;
constexpr double kMeanScoreWeight = 0.55;
constexpr double kLowerQuartileScoreWeight = 0.45;
constexpr double kSurvivalCoreWeight = 0.55;
constexpr double kCorrectedScoreCoreWeight = 0.15;

// Frozen tournament admission.  The first four checks are externally set;
// the last four define "material robust improvement" before Stage-A evaluation.
constexpr double kTournamentScoreGate = 500'000.0;
constexpr double kTournamentMovesGate = 150.0;
constexpr double kTournamentClearsGate = 2.15;
constexpr double kTournamentRevealsGate = 1.18;
constexpr double kTournamentObjectiveDeltaGate = 25.0;
constexpr double kTournamentMilestoneDeltaGate = 25.0;
constexpr double kTournamentMilestoneRateDelta = 0.02;
constexpr int kTournamentImprovedMilestonesGate = 3;
constexpr double kTournamentHighTailRateDelta = 0.03;

// Frozen Stage-A gate.
constexpr double kStageAScoreGate = 700'000.0;
constexpr double kStageAMovesGate = 200.0;
constexpr double kStageABottomQuartileMovesGate = 100.0;
constexpr double kStageAClearsGate = 2.20;
constexpr double kStageARevealsGate = 1.20;
constexpr int kStageAJointWinsGate = 20;

constexpr double kWallLimitSeconds = 75.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr double kProjectionSafetyFactor = 1.20;

constexpr std::string_view kOutputCheckpoint =
    "/tmp/drop7-tail-survival-cem.bin";
constexpr std::string_view kOutputGolden =
    "/tmp/drop7-tail-survival-cem-golden.json";
constexpr std::string_view kPreflightArtifact =
    "/tmp/drop7-tail-survival-cem-preflight-admitted.json";
constexpr std::string_view kOutputArtifact =
    "/tmp/drop7-tail-survival-cem.json";

static_assert(evo::kFeatureCount == 165);
static_assert(evo::kBaseFeatureCount == 33);
static_assert(evo::kPhaseCount == 5);
static_assert(evo::kCheckpointVersion == 2);
static_assert(kPopulation % 2 == 1 && kPopulation >= 5);
static_assert(kElite >= 2 && kElite < kPopulation);
static_assert(kGenerations >= 32 && kGenerations <= 48);
static_assert(kGenerations * kBatchGames <=
              static_cast<int>(kFittingAllowEnd - kFittingAllowStart + 1));
static_assert(kTournamentGames ==
              static_cast<int>(kTournamentEnd - kTournamentStart + 1));
static_assert(kStageAEnd - kStageAStart + 1 == 32);
static_assert((kFittingAllowStart >> 24) == 0x3du);
static_assert((kTournamentStart >> 24) == 0x3du);
static_assert((kStageAStart >> 24) == 0x3du);
static_assert(kSearchDepth == 3 && kInternalWidth == 2 && kStrata == 3);
static_assert(kMaximumMoves == 500 && kThreads == 8);

enum class SeedPurpose { kReplay, kFitting, kTournament, kStageA };

struct SeedAudit {
  std::atomic<std::uint64_t> replay_games{0};
  std::atomic<std::uint64_t> fitting_games{0};
  std::atomic<std::uint64_t> tournament_games{0};
  std::atomic<std::uint64_t> stage_a_games{0};
  std::atomic<bool> other_seed_opened{false};
  std::atomic<bool> forbidden_prefix_opened{false};
};

SeedAudit g_seed_audit;

void resetSeedAudit() {
  g_seed_audit.replay_games.store(0);
  g_seed_audit.fitting_games.store(0);
  g_seed_audit.tournament_games.store(0);
  g_seed_audit.stage_a_games.store(0);
  g_seed_audit.other_seed_opened.store(false);
  g_seed_audit.forbidden_prefix_opened.store(false);
}

bool seedAllowed(std::uint32_t seed, SeedPurpose purpose) {
  switch (purpose) {
    case SeedPurpose::kReplay:
      return seed >= kReplayStart && seed <= kReplayEnd;
    case SeedPurpose::kFitting:
      return seed >= kFittingAllowStart && seed <= kFittingAllowEnd;
    case SeedPurpose::kTournament:
      return seed >= kTournamentStart && seed <= kTournamentEnd;
    case SeedPurpose::kStageA:
      return seed >= kStageAStart && seed <= kStageAEnd;
  }
  return false;
}

void auditSeed(std::uint32_t seed, SeedPurpose purpose) {
  const std::uint32_t prefix = seed >> 24;
  if (prefix == 0x4du || prefix == 0x7du || prefix == 0xd7u) {
    g_seed_audit.forbidden_prefix_opened.store(true);
    throw std::runtime_error("protected Drop7 seed prefix requested");
  }
  if (!seedAllowed(seed, purpose)) {
    g_seed_audit.other_seed_opened.store(true);
    throw std::runtime_error("Drop7 seed outside preregistered lane");
  }
  switch (purpose) {
    case SeedPurpose::kReplay:
      ++g_seed_audit.replay_games;
      break;
    case SeedPurpose::kFitting:
      ++g_seed_audit.fitting_games;
      break;
    case SeedPurpose::kTournament:
      ++g_seed_audit.tournament_games;
      break;
    case SeedPurpose::kStageA:
      ++g_seed_audit.stage_a_games;
      break;
  }
}

std::uint64_t peakRssBytes() {
#if defined(__APPLE__) || defined(__linux__)
  struct rusage usage {};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ull;
#endif
#else
  return 0;
#endif
}

constexpr std::array<std::uint32_t, 64> kSha256Constants{{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
}};

std::string sha256(std::string_view source) {
  std::vector<std::uint8_t> message(source.begin(), source.end());
  const std::uint64_t bit_length =
      static_cast<std::uint64_t>(message.size()) * 8u;
  message.push_back(0x80u);
  while (message.size() % 64 != 56) message.push_back(0u);
  for (int byte = 7; byte >= 0; --byte) {
    message.push_back(
        static_cast<std::uint8_t>((bit_length >> (byte * 8)) & 0xffu));
  }
  std::array<std::uint32_t, 8> hash{{
      0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
  }};
  for (std::size_t offset = 0; offset < message.size(); offset += 64) {
    std::array<std::uint32_t, 64> words{};
    for (int word = 0; word < 16; ++word) {
      const std::size_t begin = offset + static_cast<std::size_t>(word * 4);
      words[word] = (static_cast<std::uint32_t>(message[begin]) << 24) |
                    (static_cast<std::uint32_t>(message[begin + 1]) << 16) |
                    (static_cast<std::uint32_t>(message[begin + 2]) << 8) |
                    static_cast<std::uint32_t>(message[begin + 3]);
    }
    for (int word = 16; word < 64; ++word) {
      const std::uint32_t s0 = std::rotr(words[word - 15], 7) ^
                               std::rotr(words[word - 15], 18) ^
                               (words[word - 15] >> 3);
      const std::uint32_t s1 = std::rotr(words[word - 2], 17) ^
                               std::rotr(words[word - 2], 19) ^
                               (words[word - 2] >> 10);
      words[word] = words[word - 16] + s0 + words[word - 7] + s1;
    }
    std::uint32_t a = hash[0];
    std::uint32_t b = hash[1];
    std::uint32_t c = hash[2];
    std::uint32_t d = hash[3];
    std::uint32_t e = hash[4];
    std::uint32_t f = hash[5];
    std::uint32_t g = hash[6];
    std::uint32_t h = hash[7];
    for (int round = 0; round < 64; ++round) {
      const std::uint32_t upper = std::rotr(e, 6) ^ std::rotr(e, 11) ^
                                  std::rotr(e, 25);
      const std::uint32_t choose = (e & f) ^ (~e & g);
      const std::uint32_t first =
          h + upper + choose + kSha256Constants[round] + words[round];
      const std::uint32_t lower = std::rotr(a, 2) ^ std::rotr(a, 13) ^
                                  std::rotr(a, 22);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t second = lower + majority;
      h = g;
      g = f;
      f = e;
      e = d + first;
      d = c;
      c = b;
      b = a;
      a = first + second;
    }
    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
    hash[5] += f;
    hash[6] += g;
    hash[7] += h;
  }
  constexpr char kDigits[] = "0123456789abcdef";
  std::string result;
  result.reserve(64);
  for (const std::uint32_t value : hash) {
    for (int nibble = 7; nibble >= 0; --nibble) {
      result.push_back(kDigits[(value >> (nibble * 4)) & 0x0fu]);
    }
  }
  return result;
}

std::string readWholeFile(std::string_view path) {
  std::ifstream input(std::string(path), std::ios::binary | std::ios::ate);
  if (!input) throw std::runtime_error("unable to read frozen input");
  const std::streampos end = input.tellg();
  if (end < 0) throw std::runtime_error("invalid frozen input size");
  std::string result(static_cast<std::size_t>(end), '\0');
  input.seekg(0);
  input.read(result.data(), static_cast<std::streamsize>(result.size()));
  if (!input) throw std::runtime_error("frozen input read failed");
  return result;
}

struct TailEvaluation {
  std::vector<GameResult> games;
  double objective = -std::numeric_limits<double>::infinity();
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double lower_quartile_score = 0.0;
  double lower_quartile_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double survival_core = 0.0;
  double corrected_score_core = 0.0;
  double milestone_utility = 0.0;
  std::array<int, kMilestones.size()> milestone_hits{};
  std::array<double, kMilestones.size()> milestone_rates{};
  std::array<double, kMilestones.size()> robust_milestone_rates{};
  int censored = 0;
  double seconds = 0.0;
};

double robustMilestoneRate(int hits, int games) {
  if (hits <= 0 || games <= 0) return 0.0;
  const double ordinary = static_cast<double>(hits) / games;
  const double repeated = hits > 1 && games > 1
                              ? static_cast<double>(hits - 1) / (games - 1)
                              : 0.0;
  return kFirstHitRateWeight * ordinary +
         kRepeatedHitRateWeight * repeated;
}

void summarize(TailEvaluation& evaluation) {
  if (evaluation.games.empty()) {
    throw std::invalid_argument("cannot summarize an empty game batch");
  }
  evaluation.censored = 0;
  evaluation.milestone_hits.fill(0);
  std::vector<double> scores;
  std::vector<double> moves;
  scores.reserve(evaluation.games.size());
  moves.reserve(evaluation.games.size());
  double total_score = 0.0;
  double total_moves = 0.0;
  double total_clears = 0.0;
  double total_reveals = 0.0;
  for (const GameResult& game : evaluation.games) {
    total_score += static_cast<double>(game.score);
    total_moves += static_cast<double>(game.moves);
    total_clears += static_cast<double>(game.clears);
    total_reveals += static_cast<double>(game.reveals);
    scores.push_back(static_cast<double>(game.score));
    moves.push_back(static_cast<double>(game.moves));
    if (!game.terminal) ++evaluation.censored;
    for (std::size_t index = 0; index < kMilestones.size(); ++index) {
      if (game.moves >= kMilestones[index]) {
        ++evaluation.milestone_hits[index];
      }
    }
  }
  const double count = static_cast<double>(evaluation.games.size());
  evaluation.mean_score = total_score / count;
  evaluation.mean_moves = total_moves / count;
  evaluation.lower_quartile_score = evo::lowerFractionMean(scores, 0.25);
  evaluation.lower_quartile_moves = evo::lowerFractionMean(moves, 0.25);
  evaluation.clears_per_move =
      total_moves > 0.0 ? total_clears / total_moves : 0.0;
  evaluation.reveals_per_move =
      total_moves > 0.0 ? total_reveals / total_moves : 0.0;
  evaluation.survival_core =
      kMeanSurvivalWeight * evaluation.mean_moves +
      kLowerQuartileSurvivalWeight * evaluation.lower_quartile_moves;
  evaluation.corrected_score_core =
      kMeanScoreWeight * (evaluation.mean_score / kLevelBonus) +
      kLowerQuartileScoreWeight *
          (evaluation.lower_quartile_score / kLevelBonus);
  evaluation.milestone_utility = 0.0;
  for (std::size_t index = 0; index < kMilestones.size(); ++index) {
    evaluation.milestone_rates[index] =
        static_cast<double>(evaluation.milestone_hits[index]) / count;
    evaluation.robust_milestone_rates[index] = robustMilestoneRate(
        evaluation.milestone_hits[index],
        static_cast<int>(evaluation.games.size()));
    evaluation.milestone_utility +=
        kMilestoneWeights[index] * evaluation.robust_milestone_rates[index];
  }
  evaluation.objective =
      kSurvivalCoreWeight * evaluation.survival_core +
      kCorrectedScoreCoreWeight * evaluation.corrected_score_core +
      evaluation.milestone_utility;
}

GameResult playTailGame(std::uint32_t seed, const Weights& weights,
                        SeedPurpose purpose) {
  auditSeed(seed, purpose);
  return evo::playSearchGame(seed, weights, kStrata, kMaximumMoves,
                             kSearchDepth, kInternalWidth);
}

TailEvaluation evaluate(const Weights& weights, std::uint32_t seed_start,
                        int games, SeedPurpose purpose) {
  if (games < 1) throw std::invalid_argument("game count must be positive");
  TailEvaluation result;
  result.games.reserve(static_cast<std::size_t>(games));
  const auto started = Clock::now();
  for (int game = 0; game < games; ++game) {
    result.games.push_back(playTailGame(
        seed_start + static_cast<std::uint32_t>(game), weights, purpose));
  }
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  summarize(result);
  return result;
}

struct Candidate {
  Weights weights{};
  TailEvaluation evaluation;
  std::string provenance;
  int generation = -1;
};

void evaluatePopulation(std::vector<Candidate>& candidates,
                        std::uint32_t seed_start, int games,
                        SeedPurpose purpose) {
  std::atomic<std::size_t> next{0};
  std::atomic<bool> stopped{false};
  std::exception_ptr failure;
  std::mutex failure_mutex;
  const int worker_count =
      std::min(kThreads, static_cast<int>(candidates.size()));
  std::vector<std::thread> workers;
  workers.reserve(static_cast<std::size_t>(worker_count));
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.emplace_back([&]() {
      while (!stopped.load()) {
        const std::size_t index = next.fetch_add(1);
        if (index >= candidates.size()) break;
        try {
          candidates[index].evaluation =
              evaluate(candidates[index].weights, seed_start, games, purpose);
        } catch (...) {
          stopped.store(true);
          std::lock_guard<std::mutex> lock(failure_mutex);
          if (!failure) failure = std::current_exception();
        }
      }
    });
  }
  for (std::thread& worker : workers) worker.join();
  if (failure) std::rethrow_exception(failure);
}

TailEvaluation runReplayBenchmark(const Weights& weights) {
  TailEvaluation aggregate;
  constexpr int replay_games =
      static_cast<int>(kReplayEnd - kReplayStart + 1);
  // Production workers evaluate long candidate batches.  Give every worker
  // the same complete, already-documented eight-game replay so thread startup
  // and cold caches cannot dominate the 50k-game resource projection.
  aggregate.games.resize(static_cast<std::size_t>(kThreads * replay_games));
  const auto started = Clock::now();
  std::exception_ptr failure;
  std::mutex failure_mutex;
  std::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int worker = 0; worker < kThreads; ++worker) {
    workers.emplace_back([&, worker]() {
      try {
        for (int game = 0; game < replay_games; ++game) {
          const std::size_t index = static_cast<std::size_t>(
              worker * replay_games + game);
          aggregate.games[index] = playTailGame(
              kReplayStart + static_cast<std::uint32_t>(game), weights,
              SeedPurpose::kReplay);
        }
      } catch (...) {
        std::lock_guard<std::mutex> lock(failure_mutex);
        if (!failure) failure = std::current_exception();
      }
    });
  }
  for (std::thread& worker : workers) worker.join();
  if (failure) std::rethrow_exception(failure);
  aggregate.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  summarize(aggregate);
  return aggregate;
}

struct TournamentGate {
  bool mean_score = false;
  bool mean_moves = false;
  bool clears = false;
  bool reveals = false;
  bool objective_delta = false;
  bool milestone_delta = false;
  bool improved_milestones = false;
  bool high_tail = false;
  int materially_improved_milestones = 0;
  double objective_improvement = 0.0;
  double milestone_improvement = 0.0;
  bool passed = false;
};

TournamentGate tournamentGate(const TailEvaluation& candidate,
                              const TailEvaluation& inherited) {
  TournamentGate result;
  result.mean_score = candidate.mean_score >= kTournamentScoreGate;
  result.mean_moves = candidate.mean_moves >= kTournamentMovesGate;
  result.clears = candidate.clears_per_move >= kTournamentClearsGate;
  result.reveals = candidate.reveals_per_move >= kTournamentRevealsGate;
  result.objective_improvement = candidate.objective - inherited.objective;
  result.milestone_improvement =
      candidate.milestone_utility - inherited.milestone_utility;
  result.objective_delta =
      result.objective_improvement >= kTournamentObjectiveDeltaGate;
  result.milestone_delta =
      result.milestone_improvement >= kTournamentMilestoneDeltaGate;
  for (std::size_t index = 0; index < kMilestones.size(); ++index) {
    if (candidate.milestone_rates[index] >=
        inherited.milestone_rates[index] + kTournamentMilestoneRateDelta) {
      ++result.materially_improved_milestones;
    }
  }
  result.improved_milestones =
      result.materially_improved_milestones >=
      kTournamentImprovedMilestonesGate;
  result.high_tail =
      candidate.milestone_rates[2] >=
      inherited.milestone_rates[2] + kTournamentHighTailRateDelta;
  result.passed = result.mean_score && result.mean_moves && result.clears &&
                  result.reveals && result.objective_delta &&
                  result.milestone_delta && result.improved_milestones &&
                  result.high_tail;
  return result;
}

struct StageAGate {
  bool mean_score = false;
  bool mean_moves = false;
  bool bottom_quartile_moves = false;
  bool clears = false;
  bool reveals = false;
  bool joint_wins = false;
  int joint_win_count = 0;
  bool passed = false;
};

StageAGate stageAGate(const TailEvaluation& candidate,
                      const TailEvaluation& inherited) {
  if (candidate.games.size() != inherited.games.size()) {
    throw std::invalid_argument("paired Stage-A batches differ in size");
  }
  StageAGate result;
  result.mean_score = candidate.mean_score >= kStageAScoreGate;
  result.mean_moves = candidate.mean_moves >= kStageAMovesGate;
  result.bottom_quartile_moves =
      candidate.lower_quartile_moves >= kStageABottomQuartileMovesGate;
  result.clears = candidate.clears_per_move >= kStageAClearsGate;
  result.reveals = candidate.reveals_per_move >= kStageARevealsGate;
  for (std::size_t game = 0; game < candidate.games.size(); ++game) {
    if (candidate.games[game].score > inherited.games[game].score &&
        candidate.games[game].moves > inherited.games[game].moves) {
      ++result.joint_win_count;
    }
  }
  result.joint_wins = result.joint_win_count >= kStageAJointWinsGate;
  result.passed = result.mean_score && result.mean_moves &&
                  result.bottom_quartile_moves && result.clears &&
                  result.reveals && result.joint_wins;
  return result;
}

struct Progress {
  int generation = 0;
  double best_objective = 0.0;
  double best_score = 0.0;
  double best_moves = 0.0;
  double best_lower_quartile_moves = 0.0;
  double best_milestone_utility = 0.0;
  std::array<int, kMilestones.size()> hits{};
  double elapsed_seconds = 0.0;
  double projected_seconds = 0.0;
  std::uint64_t peak_rss_bytes = 0;
};

void checkResources(double elapsed_seconds, double projected_seconds) {
  if (elapsed_seconds > kWallLimitSeconds ||
      projected_seconds > kWallLimitSeconds ||
      peakRssBytes() > kRssLimitBytes) {
    throw std::runtime_error("tail CEM resource projection failed");
  }
}

void writeMetric(std::ostream& output, const TailEvaluation& evaluation) {
  output << std::setprecision(12)
         << "{\"objective\":" << evaluation.objective
         << ",\"meanScore\":" << evaluation.mean_score
         << ",\"meanMoves\":" << evaluation.mean_moves
         << ",\"lowerQuartileScore\":"
         << evaluation.lower_quartile_score
         << ",\"lowerQuartileMoves\":"
         << evaluation.lower_quartile_moves
         << ",\"clearsPerMove\":" << evaluation.clears_per_move
         << ",\"revealsPerMove\":" << evaluation.reveals_per_move
         << ",\"survivalCore\":" << evaluation.survival_core
         << ",\"correctedScoreCore\":"
         << evaluation.corrected_score_core
         << ",\"milestoneUtility\":" << evaluation.milestone_utility
         << ",\"censored\":" << evaluation.censored
         << ",\"milestones\":[";
  for (std::size_t index = 0; index < kMilestones.size(); ++index) {
    if (index != 0) output << ',';
    output << "{\"moves\":" << kMilestones[index]
           << ",\"hits\":" << evaluation.milestone_hits[index]
           << ",\"rate\":" << evaluation.milestone_rates[index]
           << ",\"robustRate\":"
           << evaluation.robust_milestone_rates[index] << '}';
  }
  output << "]}";
}

void writeGames(std::ostream& output, const TailEvaluation& evaluation) {
  output << '[';
  for (std::size_t index = 0; index < evaluation.games.size(); ++index) {
    if (index != 0) output << ',';
    const GameResult& game = evaluation.games[index];
    output << "{\"seed\":" << game.seed << ",\"score\":" << game.score
           << ",\"moves\":" << game.moves << ",\"clears\":"
           << game.clears << ",\"reveals\":" << game.reveals
           << ",\"maximumChain\":" << game.maximum_chain
           << ",\"terminal\":" << (game.terminal ? "true" : "false")
           << '}';
  }
  output << ']';
}

void writePreflight(const TailEvaluation& benchmark,
                    double projected_seconds, bool passed) {
  std::ofstream output(std::string(kPreflightArtifact),
                       std::ios::trunc);
  if (!output) throw std::runtime_error("unable to write preflight artifact");
  output << std::setprecision(12)
         << "{\n  \"format\":\"drop7-tail-survival-cem-preflight-v2\",\n"
         << "  \"input\":{\"path\":\"" << kInputCheckpoint
         << "\",\"sha256\":\"" << kInputCheckpointSha256
         << "\",\"fingerprint\":\"0x" << std::hex << kInputFingerprint
         << std::dec << "\",\"storedSamples\":" << kInputStoredSamples
         << ",\"bitExact\":true},\n"
         << "  \"search\":{\"depth\":" << kSearchDepth
         << ",\"internalWidth\":" << kInternalWidth
         << ",\"strata\":" << kStrata
         << ",\"allLegalRootActions\":true},\n"
         << "  \"utility\":{\"formula\":\"0.55*(0.55*meanMoves+0.45*lowerQuartileMoves)+0.15*(0.55*meanScore/17000+0.45*lowerQuartileScore/17000)+sum(milestoneWeight*robustRate)\",\"robustRate\":\"0.35*hits/n+0.65*max(0,hits-1)/max(1,n-1)\",\"milestones\":[75,100,150,225,300],\"weights\":[24,40,80,150,240]},\n"
         << "  \"schedule\":{\"generations\":" << kGenerations
         << ",\"population\":" << kPopulation << ",\"elite\":"
         << kElite << ",\"gamesPerCandidate\":" << kBatchGames
         << ",\"threads\":" << kThreads << ",\"maximumMoves\":"
         << kMaximumMoves << ",\"tournamentGames\":"
         << kTournamentGames << ",\"archiveCandidates\":"
         << (1 + 2 * kGenerations) << "},\n"
         << "  \"bounds\":{\"weights\":[" << kMinimumWeight << ','
         << kMaximumWeight << "],\"sigma\":[" << kMinimumSigma << ','
         << kMaximumSigma << "],\"terminalWeightsFixed\":-100},\n"
         << "  \"seedSeal\":{\"replayOnly\":\"0x3d510900..0x3d510907\",\"fittingAllowlist\":\"0x3d740000..0x3d74ffff unopened\",\"tournament\":\"0x3d750000..0x3d7500ff unopened\",\"stageA\":\"0x3d760000..0x3d76001f unopened\",\"forbidden\":\"0x4d/0x7d/0xd7 unopened\"},\n"
         << "  \"benchmark\":";
  writeMetric(output, benchmark);
  output << ",\n  \"performance\":{\"replayWallSeconds\":"
         << benchmark.seconds << ",\"projectedWorstCaseWallSeconds\":"
         << projected_seconds << ",\"wallLimitSeconds\":"
         << kWallLimitSeconds << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"rssLimitBytes\":" << kRssLimitBytes << "},\n"
         << "  \"freshSeedCounts\":{\"fitting\":"
         << g_seed_audit.fitting_games.load() << ",\"tournament\":"
         << g_seed_audit.tournament_games.load() << ",\"stageA\":"
         << g_seed_audit.stage_a_games.load() << "},\n"
         << "  \"passed\":" << (passed ? "true" : "false") << "\n}\n";
  if (!output) throw std::runtime_error("preflight artifact write failed");
}

Weights loadFrozenInput() {
  const std::string contents = readWholeFile(kInputCheckpoint);
  if (contents.size() != 1'352 || sha256(contents) != kInputCheckpointSha256) {
    throw std::runtime_error("frozen phase-policy checkpoint changed");
  }
  auto [weights, stored_samples] =
      evo::loadCheckpoint(std::string(kInputCheckpoint));
  if (stored_samples != kInputStoredSamples ||
      evo::fingerprint(weights) != kInputFingerprint) {
    throw std::runtime_error("frozen phase-policy checkpoint identity mismatch");
  }
  return weights;
}

double projectedWorstCaseSeconds(double replay_seconds,
                                 std::size_t replay_game_executions) {
  constexpr std::uint64_t fitting_games =
      static_cast<std::uint64_t>(kGenerations) * kPopulation * kBatchGames;
  constexpr std::uint64_t tournament_games =
      static_cast<std::uint64_t>(1 + 2 * kGenerations) * kTournamentGames;
  constexpr std::uint64_t conditional_stage_games = 2u * 32u;
  constexpr std::uint64_t total_games =
      fitting_games + tournament_games + conditional_stage_games;
  return replay_seconds / static_cast<double>(replay_game_executions) *
         static_cast<double>(total_games) * kProjectionSafetyFactor;
}

TailEvaluation runPreflight() {
  resetSeedAudit();
  const Weights weights = loadFrozenInput();
  const TailEvaluation benchmark = runReplayBenchmark(weights);
  const double projection =
      projectedWorstCaseSeconds(benchmark.seconds, benchmark.games.size());
  bool exact_replay = benchmark.games.size() == 64;
  for (int worker = 0; worker < kThreads && exact_replay; ++worker) {
    const std::size_t first = static_cast<std::size_t>(worker * 8);
    exact_replay = benchmark.games[first].seed == kReplayStart &&
                   benchmark.games[first + 7].seed == kReplayEnd;
  }
  const bool fresh_sealed = g_seed_audit.fitting_games.load() == 0 &&
                            g_seed_audit.tournament_games.load() == 0 &&
                            g_seed_audit.stage_a_games.load() == 0;
  const bool resources = projection <= kWallLimitSeconds &&
                         peakRssBytes() <= kRssLimitBytes;
  const bool passed = exact_replay && fresh_sealed && resources &&
                      !g_seed_audit.other_seed_opened.load() &&
                      !g_seed_audit.forbidden_prefix_opened.load();
  writePreflight(benchmark, projection, passed);
  std::cout << std::fixed << std::setprecision(6)
            << "TAIL_SURVIVAL_CEM_PREFLIGHT {\"replayGames\":"
            << benchmark.games.size()
            << ",\"meanScore\":" << benchmark.mean_score
            << ",\"meanMoves\":" << benchmark.mean_moves
            << ",\"wallSeconds\":" << benchmark.seconds
            << ",\"projectedWorstCaseSeconds\":" << projection
            << ",\"freshFittingSeedsOpened\":"
            << g_seed_audit.fitting_games.load()
            << ",\"tournamentSeedsOpened\":"
            << g_seed_audit.tournament_games.load()
            << ",\"stageASeedsOpened\":"
            << g_seed_audit.stage_a_games.load() << ",\"peakRssBytes\":"
            << peakRssBytes() << ",\"passed\":"
            << (passed ? "true" : "false") << "}\n";
  if (!passed) throw std::runtime_error("tail CEM preflight failed");
  return benchmark;
}

void freezeCandidate(const Weights& candidate) {
  evo::saveCheckpoint(std::string(kOutputCheckpoint), candidate, kStrata);
  auto [reloaded, stored_samples] =
      evo::loadCheckpoint(std::string(kOutputCheckpoint));
  if (stored_samples != kStrata || reloaded != candidate ||
      evo::fingerprint(reloaded) != evo::fingerprint(candidate)) {
    throw std::runtime_error("frozen tail candidate failed round trip");
  }

  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;
  const int action = evo::chooseSearchAction(
      state, reloaded, kSearchDepth, kInternalWidth, kStrata);
  State mirrored = state;
  mirrored.board = evo::detail::mirrorBoard(state.board);
  const int reflected = evo::chooseSearchAction(
      mirrored, reloaded, kSearchDepth, kInternalWidth, kStrata);
  const bool reflection_exact = reflected == kBoardSize - 1 - action;
  if (!reflection_exact) throw std::runtime_error("golden reflection failed");

  std::ofstream output(std::string(kOutputGolden), std::ios::trunc);
  if (!output) throw std::runtime_error("unable to write golden artifact");
  output << "{\n  \"format\":\"drop7-tail-survival-cem-golden-v1\",\n"
         << "  \"modelFingerprint\":\"0x" << std::hex
         << evo::fingerprint(reloaded) << std::dec << "\",\n"
         << "  \"search\":{\"depth\":" << kSearchDepth
         << ",\"internalWidth\":" << kInternalWidth
         << ",\"strata\":" << kStrata << "},\n"
         << "  \"fixtureAction\":" << action
         << ",\"reflectedAction\":" << reflected
         << ",\"reflectionExact\":true\n}\n";
}

void writeGate(std::ostream& output, const TournamentGate& gate) {
  output << std::setprecision(12)
         << "{\"thresholds\":{\"meanScore\":" << kTournamentScoreGate
         << ",\"meanMoves\":" << kTournamentMovesGate
         << ",\"clearsPerMove\":" << kTournamentClearsGate
         << ",\"revealsPerMove\":" << kTournamentRevealsGate
         << ",\"objectiveDelta\":" << kTournamentObjectiveDeltaGate
         << ",\"milestoneUtilityDelta\":"
         << kTournamentMilestoneDeltaGate
         << ",\"improvedMilestones\":"
         << kTournamentImprovedMilestonesGate
         << ",\"highTail150RateDelta\":"
         << kTournamentHighTailRateDelta << "},\"observed\":{"
         << "\"objectiveDelta\":" << gate.objective_improvement
         << ",\"milestoneUtilityDelta\":" << gate.milestone_improvement
         << ",\"materiallyImprovedMilestones\":"
         << gate.materially_improved_milestones << "},\"checks\":{"
         << "\"meanScore\":" << (gate.mean_score ? "true" : "false")
         << ",\"meanMoves\":" << (gate.mean_moves ? "true" : "false")
         << ",\"clears\":" << (gate.clears ? "true" : "false")
         << ",\"reveals\":" << (gate.reveals ? "true" : "false")
         << ",\"objectiveDelta\":"
         << (gate.objective_delta ? "true" : "false")
         << ",\"milestoneDelta\":"
         << (gate.milestone_delta ? "true" : "false")
         << ",\"improvedMilestones\":"
         << (gate.improved_milestones ? "true" : "false")
         << ",\"highTail\":" << (gate.high_tail ? "true" : "false")
         << "},\"passed\":" << (gate.passed ? "true" : "false") << '}';
}

void writeGate(std::ostream& output, const StageAGate& gate) {
  output << std::setprecision(12)
         << "{\"thresholds\":{\"meanScore\":" << kStageAScoreGate
         << ",\"meanMoves\":" << kStageAMovesGate
         << ",\"bottomQuartileMoves\":"
         << kStageABottomQuartileMovesGate << ",\"clearsPerMove\":"
         << kStageAClearsGate << ",\"revealsPerMove\":"
         << kStageARevealsGate << ",\"jointWins\":"
         << kStageAJointWinsGate << "},\"observed\":{\"jointWins\":"
         << gate.joint_win_count << "},\"checks\":{\"meanScore\":"
         << (gate.mean_score ? "true" : "false") << ",\"meanMoves\":"
         << (gate.mean_moves ? "true" : "false")
         << ",\"bottomQuartileMoves\":"
         << (gate.bottom_quartile_moves ? "true" : "false")
         << ",\"clears\":" << (gate.clears ? "true" : "false")
         << ",\"reveals\":" << (gate.reveals ? "true" : "false")
         << ",\"jointWins\":" << (gate.joint_wins ? "true" : "false")
         << "},\"passed\":" << (gate.passed ? "true" : "false") << '}';
}

void writeArtifact(const std::vector<Candidate>& finalists,
                   const TailEvaluation& inherited,
                   const Candidate& champion,
                   const TournamentGate& tournament_gate,
                   const std::vector<Progress>& progress,
                   bool stage_opened,
                   const TailEvaluation* stage_candidate,
                   const TailEvaluation* stage_inherited,
                   const StageAGate* stage_gate,
                   double wall_seconds) {
  std::ofstream output(std::string(kOutputArtifact), std::ios::trunc);
  if (!output) throw std::runtime_error("unable to write tail CEM artifact");
  output << std::setprecision(12)
         << "{\n  \"format\":\"drop7-tail-survival-cem-v1\",\n"
         << "  \"lockedInputs\":{\"checkpoint\":\"" << kInputCheckpoint
         << "\",\"checkpointSha256\":\"" << kInputCheckpointSha256
         << "\",\"checkpointFingerprint\":\"0x" << std::hex
         << kInputFingerprint << std::dec << "\",\"sourceSha256\":\""
         << kInputSourceSha256 << "\",\"behaviorSha256\":\""
         << kBehaviorSha256 << "\",\"engineSha256\":\""
         << kEngineSha256 << "\"},\n"
         << "  \"search\":{\"depth\":" << kSearchDepth
         << ",\"internalWidth\":" << kInternalWidth
         << ",\"strata\":" << kStrata
         << ",\"allLegalRootActions\":true},\n"
         << "  \"objectivePreregistered\":true,\n"
         << "  \"training\":{\"generations\":" << kGenerations
         << ",\"population\":" << kPopulation << ",\"elite\":"
         << kElite << ",\"batchGames\":" << kBatchGames
         << ",\"maximumMoves\":" << kMaximumMoves
         << ",\"fittingAllowlist\":\"0x3d740000..0x3d74ffff\",\"usedDistinctSeeds\":\"0x3d740000..0x3d7403ff\",\"candidateGames\":"
         << static_cast<std::uint64_t>(kGenerations) * kPopulation *
                kBatchGames
         << ",\"progress\":[";
  for (std::size_t index = 0; index < progress.size(); ++index) {
    if (index != 0) output << ',';
    const Progress& item = progress[index];
    output << "{\"generation\":" << item.generation
           << ",\"bestObjective\":" << item.best_objective
           << ",\"bestScore\":" << item.best_score
           << ",\"bestMoves\":" << item.best_moves
           << ",\"bestLowerQuartileMoves\":"
           << item.best_lower_quartile_moves
           << ",\"bestMilestoneUtility\":"
           << item.best_milestone_utility << ",\"hits\":[";
    for (std::size_t milestone = 0; milestone < item.hits.size(); ++milestone) {
      if (milestone != 0) output << ',';
      output << item.hits[milestone];
    }
    output << "],\"elapsedSeconds\":" << item.elapsed_seconds
           << ",\"projectedSeconds\":" << item.projected_seconds
           << ",\"peakRssBytes\":" << item.peak_rss_bytes << '}';
  }
  output << "]},\n  \"tournament\":{\"seedLane\":\"0x3d750000..0x3d7500ff\",\"gamesPerFinalist\":"
         << kTournamentGames << ",\"finalistCount\":" << finalists.size()
         << ",\"allStartMeansAndChampionsReranked\":true,\"startingPolicy\":";
  writeMetric(output, inherited);
  output << ",\"champion\":{\"provenance\":\"" << champion.provenance
         << "\",\"generation\":" << champion.generation
         << ",\"fingerprint\":\"0x" << std::hex
         << evo::fingerprint(champion.weights) << std::dec
         << "\",\"metrics\":";
  writeMetric(output, champion.evaluation);
  output << "},\"admissionGate\":";
  writeGate(output, tournament_gate);
  output << ",\"finalists\":[";
  for (std::size_t index = 0; index < finalists.size(); ++index) {
    if (index != 0) output << ',';
    const Candidate& finalist = finalists[index];
    output << "{\"rank\":" << index + 1 << ",\"provenance\":\""
           << finalist.provenance << "\",\"generation\":"
           << finalist.generation << ",\"fingerprint\":\"0x" << std::hex
           << evo::fingerprint(finalist.weights) << std::dec
           << "\",\"metrics\":";
    writeMetric(output, finalist.evaluation);
    output << '}';
  }
  output << "]},\n  \"stageA\":{\"opened\":"
         << (stage_opened ? "true" : "false")
         << ",\"seedLane\":\"0x3d760000..0x3d76001f\"";
  if (stage_opened && stage_candidate != nullptr &&
      stage_inherited != nullptr && stage_gate != nullptr) {
    output << ",\"candidate\":";
    writeMetric(output, *stage_candidate);
    output << ",\"inherited\":";
    writeMetric(output, *stage_inherited);
    output << ",\"gate\":";
    writeGate(output, *stage_gate);
    output << ",\"candidateGames\":";
    writeGames(output, *stage_candidate);
    output << ",\"inheritedGames\":";
    writeGames(output, *stage_inherited);
  }
  output << "},\n  \"seedAudit\":{\"replayGames\":"
         << g_seed_audit.replay_games.load() << ",\"fittingGames\":"
         << g_seed_audit.fitting_games.load() << ",\"tournamentGames\":"
         << g_seed_audit.tournament_games.load() << ",\"stageAGames\":"
         << g_seed_audit.stage_a_games.load()
         << ",\"otherSeedOpened\":"
         << (g_seed_audit.other_seed_opened.load() ? "true" : "false")
         << ",\"forbiddenPrefixOpened\":"
         << (g_seed_audit.forbidden_prefix_opened.load() ? "true" : "false")
         << "},\n  \"resources\":{\"wallSeconds\":" << wall_seconds
         << ",\"wallLimitSeconds\":" << kWallLimitSeconds
         << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"rssLimitBytes\":" << kRssLimitBytes << "},\n"
         << "  \"passed\":"
         << (stage_opened && stage_gate != nullptr && stage_gate->passed
                 ? "true"
                 : "false")
         << "\n}\n";
  if (!output) throw std::runtime_error("tail CEM artifact write failed");
}

struct TrainingResult {
  std::vector<Candidate> finalists;
  TailEvaluation inherited_tournament;
  Candidate champion;
  TournamentGate gate;
  std::vector<Progress> progress;
};

TrainingResult trainAndTournament(const Weights& inherited,
                                  const Clock::time_point& run_started) {
  Weights mean = inherited;
  Weights sigma = evo::initialSigma();
  evo::SplitMix64 random(kOptimizerSeed);
  std::vector<Candidate> archive;
  archive.reserve(static_cast<std::size_t>(1 + 2 * kGenerations));
  archive.push_back(Candidate{mean, {}, "starting-policy", 0});
  std::vector<Progress> progress;
  progress.reserve(kGenerations / 4);

  for (int generation = 0; generation < kGenerations; ++generation) {
    std::vector<Candidate> population(static_cast<std::size_t>(kPopulation));
    population[0].weights = mean;
    population[0].provenance = "generation-mean";
    population[0].generation = generation + 1;
    for (int pair = 0; pair < (kPopulation - 1) / 2; ++pair) {
      Weights positive = mean;
      Weights negative = mean;
      for (int index = 0; index < evo::kFeatureCount; ++index) {
        const double perturbation = sigma[index] * random.normal();
        positive[index] = std::clamp(mean[index] + perturbation,
                                     kMinimumWeight, kMaximumWeight);
        negative[index] = std::clamp(mean[index] - perturbation,
                                     kMinimumWeight, kMaximumWeight);
      }
      for (int phase = 0; phase < evo::kPhaseCount; ++phase) {
        const int terminal = phase * evo::kBaseFeatureCount + 31;
        positive[terminal] = -100.0;
        negative[terminal] = -100.0;
      }
      Candidate& positive_candidate =
          population[static_cast<std::size_t>(1 + pair * 2)];
      Candidate& negative_candidate =
          population[static_cast<std::size_t>(2 + pair * 2)];
      positive_candidate.weights = positive;
      positive_candidate.provenance = "generation-sample";
      positive_candidate.generation = generation + 1;
      negative_candidate.weights = negative;
      negative_candidate.provenance = "generation-sample";
      negative_candidate.generation = generation + 1;
    }

    const std::uint32_t seed_start =
        kFittingAllowStart +
        static_cast<std::uint32_t>(generation * kBatchGames);
    evaluatePopulation(population, seed_start, kBatchGames,
                       SeedPurpose::kFitting);
    std::stable_sort(population.begin(), population.end(),
                     [](const Candidate& first, const Candidate& second) {
                       return first.evaluation.objective >
                              second.evaluation.objective;
                     });

    const Candidate generation_champion = population.front();
    Weights elite_mean{};
    double rank_mass = 0.0;
    for (int rank = 0; rank < kElite; ++rank) {
      const double rank_weight =
          std::log(static_cast<double>(kElite) + 0.5) -
          std::log(static_cast<double>(rank) + 1.0);
      rank_mass += rank_weight;
      for (int index = 0; index < evo::kFeatureCount; ++index) {
        elite_mean[index] += rank_weight * population[rank].weights[index];
      }
    }
    for (double& value : elite_mean) value /= rank_mass;

    Weights elite_variance{};
    for (int rank = 0; rank < kElite; ++rank) {
      const double rank_weight =
          std::log(static_cast<double>(kElite) + 0.5) -
          std::log(static_cast<double>(rank) + 1.0);
      for (int index = 0; index < evo::kFeatureCount; ++index) {
        const double delta = population[rank].weights[index] - elite_mean[index];
        elite_variance[index] += rank_weight * delta * delta;
      }
    }
    for (int index = 0; index < evo::kFeatureCount; ++index) {
      mean[index] = kMeanRetention * mean[index] +
                    kEliteMeanRate * elite_mean[index];
      const double selected_sigma =
          std::sqrt(elite_variance[index] / rank_mass + 1e-12);
      sigma[index] = std::clamp(kSigmaRetention * sigma[index] +
                                    kEliteSigmaRate * selected_sigma,
                                kMinimumSigma, kMaximumSigma);
    }
    for (int phase = 0; phase < evo::kPhaseCount; ++phase) {
      const int terminal = phase * evo::kBaseFeatureCount + 31;
      mean[terminal] = -100.0;
      sigma[terminal] = 0.0;
    }

    archive.push_back(Candidate{mean, {}, "post-update-mean", generation + 1});
    Candidate archived_champion = generation_champion;
    archived_champion.provenance = "generation-champion";
    archive.push_back(std::move(archived_champion));

    if ((generation + 1) % 4 == 0) {
      const double elapsed =
          std::chrono::duration<double>(Clock::now() - run_started).count();
      const double completed_candidate_games =
          static_cast<double>(generation + 1) * kPopulation * kBatchGames;
      const double total_candidate_games =
          static_cast<double>(kGenerations) * kPopulation * kBatchGames +
          static_cast<double>(1 + 2 * kGenerations) * kTournamentGames +
          64.0;
      const double projected = elapsed / completed_candidate_games *
                               total_candidate_games *
                               kProjectionSafetyFactor;
      Progress item;
      item.generation = generation + 1;
      item.best_objective = generation_champion.evaluation.objective;
      item.best_score = generation_champion.evaluation.mean_score;
      item.best_moves = generation_champion.evaluation.mean_moves;
      item.best_lower_quartile_moves =
          generation_champion.evaluation.lower_quartile_moves;
      item.best_milestone_utility =
          generation_champion.evaluation.milestone_utility;
      item.hits = generation_champion.evaluation.milestone_hits;
      item.elapsed_seconds = elapsed;
      item.projected_seconds = projected;
      item.peak_rss_bytes = peakRssBytes();
      progress.push_back(item);
      std::cout << std::fixed << std::setprecision(6)
                << "TAIL_SURVIVAL_CEM_PROGRESS {\"generation\":"
                << item.generation << ",\"bestObjective\":"
                << item.best_objective << ",\"bestScore\":"
                << item.best_score << ",\"bestMoves\":"
                << item.best_moves << ",\"bottomQuartileMoves\":"
                << item.best_lower_quartile_moves
                << ",\"milestoneUtility\":"
                << item.best_milestone_utility << ",\"hits\":[";
      for (std::size_t index = 0; index < item.hits.size(); ++index) {
        if (index != 0) std::cout << ',';
        std::cout << item.hits[index];
      }
      std::cout << "],\"elapsedSeconds\":" << item.elapsed_seconds
                << ",\"projectedSeconds\":" << item.projected_seconds
                << ",\"peakRssBytes\":" << item.peak_rss_bytes << "}\n";
      checkResources(elapsed, projected);
    }
  }

  if (archive.size() != static_cast<std::size_t>(1 + 2 * kGenerations)) {
    throw std::runtime_error("tail CEM finalist archive size changed");
  }
  evaluatePopulation(archive, kTournamentStart, kTournamentGames,
                     SeedPurpose::kTournament);
  const TailEvaluation inherited_tournament = archive.front().evaluation;
  std::stable_sort(archive.begin(), archive.end(),
                   [](const Candidate& first, const Candidate& second) {
                     return first.evaluation.objective >
                            second.evaluation.objective;
                   });
  const Candidate champion = archive.front();
  const TournamentGate gate =
      tournamentGate(champion.evaluation, inherited_tournament);
  freezeCandidate(champion.weights);
  checkResources(
      std::chrono::duration<double>(Clock::now() - run_started).count(),
      std::chrono::duration<double>(Clock::now() - run_started).count());
  return TrainingResult{std::move(archive), inherited_tournament, champion,
                        gate, std::move(progress)};
}

bool selfTest() {
  const bool sha =
      sha256("abc") ==
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  const Weights input = loadFrozenInput();
  const bool input_exact = evo::fingerprint(input) == kInputFingerprint;

  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;
  evo::SearchMetrics first_metrics;
  evo::SearchMetrics second_metrics;
  const int first = evo::chooseSearchAction(
      state, input, kSearchDepth, kInternalWidth, kStrata, &first_metrics);
  const int second = evo::chooseSearchAction(
      state, input, kSearchDepth, kInternalWidth, kStrata, &second_metrics);
  State mirrored = state;
  mirrored.board = evo::detail::mirrorBoard(state.board);
  const int reflected = evo::chooseSearchAction(
      mirrored, input, kSearchDepth, kInternalWidth, kStrata);
  State metadata = state;
  metadata.score = 999'999'999;
  metadata.level = 999;
  metadata.moves_played = 999;
  const int metadata_action = evo::chooseSearchAction(
      metadata, input, kSearchDepth, kInternalWidth, kStrata);
  const bool search_exact = first == second &&
                            first_metrics.transitions ==
                                second_metrics.transitions &&
                            reflected == kBoardSize - 1 - first &&
                            metadata_action == first &&
                            isLegal(state.board, first);

  TailEvaluation single;
  TailEvaluation repeated;
  for (int game = 0; game < 32; ++game) {
    GameResult first_game;
    first_game.seed = static_cast<std::uint32_t>(game);
    first_game.score = 17'000 * 20;
    first_game.moves = game == 0 ? 300 : 50;
    first_game.terminal = true;
    first_game.clears = 100;
    first_game.reveals = 50;
    single.games.push_back(first_game);
    GameResult repeated_game = first_game;
    repeated_game.moves = game < 8 ? 300 : 50;
    repeated.games.push_back(repeated_game);
  }
  summarize(single);
  summarize(repeated);
  const bool repeated_reward =
      repeated.milestone_utility > single.milestone_utility * 5.0 &&
      repeated.objective > single.objective;

  TailEvaluation inherited = repeated;
  TailEvaluation qualifying = repeated;
  qualifying.mean_score = 600'000.0;
  qualifying.mean_moves = 170.0;
  qualifying.clears_per_move = 2.2;
  qualifying.reveals_per_move = 1.2;
  qualifying.objective = inherited.objective + 30.0;
  qualifying.milestone_utility = inherited.milestone_utility + 30.0;
  for (std::size_t index = 0; index < kMilestones.size(); ++index) {
    qualifying.milestone_rates[index] =
        inherited.milestone_rates[index] + 0.04;
  }
  const bool tournament_gate_positive =
      tournamentGate(qualifying, inherited).passed;
  qualifying.mean_score = 100'000.0;
  const bool tournament_gate_negative =
      !tournamentGate(qualifying, inherited).passed;

  evo::saveCheckpoint("/tmp/drop7-tail-survival-cem-selftest.bin", input,
                      kStrata);
  const auto [round_trip, stored_samples] = evo::loadCheckpoint(
      "/tmp/drop7-tail-survival-cem-selftest.bin");
  const bool checkpoint = round_trip == input && stored_samples == kStrata;
  const bool seed_guards =
      seedAllowed(kFittingAllowStart, SeedPurpose::kFitting) &&
      seedAllowed(kFittingAllowEnd, SeedPurpose::kFitting) &&
      !seedAllowed(kFittingAllowStart - 1, SeedPurpose::kFitting) &&
      seedAllowed(kTournamentStart, SeedPurpose::kTournament) &&
      seedAllowed(kTournamentEnd, SeedPurpose::kTournament) &&
      seedAllowed(kStageAStart, SeedPurpose::kStageA) &&
      seedAllowed(kStageAEnd, SeedPurpose::kStageA) &&
      !seedAllowed(0x4d00'0000u, SeedPurpose::kFitting) &&
      !seedAllowed(0x7d00'0000u, SeedPurpose::kTournament) &&
      !seedAllowed(0xd700'0000u, SeedPurpose::kStageA);
  const bool passed = sha && input_exact && search_exact && repeated_reward &&
                      tournament_gate_positive && tournament_gate_negative &&
                      checkpoint && seed_guards &&
                      peakRssBytes() <= kRssLimitBytes;
  std::cout << "TAIL_SURVIVAL_CEM_SELF_TEST {\"passed\":"
            << (passed ? "true" : "false") << ",\"sha256\":"
            << (sha ? "true" : "false") << ",\"inputExact\":"
            << (input_exact ? "true" : "false")
            << ",\"publicSearchExact\":"
            << (search_exact ? "true" : "false")
            << ",\"repeatedMilestonesRewarded\":"
            << (repeated_reward ? "true" : "false")
            << ",\"gatePositiveNegative\":"
            << (tournament_gate_positive && tournament_gate_negative ? "true"
                                                                      : "false")
            << ",\"checkpointRoundTrip\":"
            << (checkpoint ? "true" : "false")
            << ",\"seedGuards\":" << (seed_guards ? "true" : "false")
            << ",\"freshSeedsOpened\":0,\"peakRssBytes\":"
            << peakRssBytes() << "}\n";
  return passed;
}

int run() {
  const auto run_started = Clock::now();
  runPreflight();
  const Weights inherited = loadFrozenInput();
  TrainingResult result = trainAndTournament(inherited, run_started);
  const double tournament_wall =
      std::chrono::duration<double>(Clock::now() - run_started).count();
  writeArtifact(result.finalists, result.inherited_tournament,
                result.champion, result.gate, result.progress, false, nullptr,
                nullptr, nullptr, tournament_wall);
  std::cout << std::fixed << std::setprecision(6)
            << "TAIL_SURVIVAL_CEM_TOURNAMENT {\"championScore\":"
            << result.champion.evaluation.mean_score
            << ",\"championMoves\":"
            << result.champion.evaluation.mean_moves
            << ",\"bottomQuartileMoves\":"
            << result.champion.evaluation.lower_quartile_moves
            << ",\"clearsPerMove\":"
            << result.champion.evaluation.clears_per_move
            << ",\"revealsPerMove\":"
            << result.champion.evaluation.reveals_per_move
            << ",\"milestoneUtility\":"
            << result.champion.evaluation.milestone_utility
            << ",\"startingScore\":"
            << result.inherited_tournament.mean_score
            << ",\"startingMoves\":"
            << result.inherited_tournament.mean_moves
            << ",\"objectiveDelta\":" << result.gate.objective_improvement
            << ",\"milestoneDelta\":"
            << result.gate.milestone_improvement
            << ",\"modelFingerprint\":\"0x" << std::hex
            << evo::fingerprint(result.champion.weights) << std::dec
            << "\",\"stageAOpened\":"
            << (result.gate.passed ? "true" : "false") << "}\n";
  if (!result.gate.passed) return 2;

  std::vector<Candidate> stage_candidates;
  stage_candidates.push_back(
      Candidate{result.champion.weights, {}, "frozen-candidate", -1});
  stage_candidates.push_back(
      Candidate{inherited, {}, "starting-policy", -1});
  evaluatePopulation(stage_candidates, kStageAStart, 32,
                     SeedPurpose::kStageA);
  const StageAGate stage_gate = stageAGate(stage_candidates[0].evaluation,
                                           stage_candidates[1].evaluation);
  const double wall_seconds =
      std::chrono::duration<double>(Clock::now() - run_started).count();
  checkResources(wall_seconds, wall_seconds);
  writeArtifact(result.finalists, result.inherited_tournament,
                result.champion, result.gate, result.progress, true,
                &stage_candidates[0].evaluation,
                &stage_candidates[1].evaluation, &stage_gate, wall_seconds);
  std::cout << std::fixed << std::setprecision(6)
            << "TAIL_SURVIVAL_CEM_STAGE_A {\"candidateScore\":"
            << stage_candidates[0].evaluation.mean_score
            << ",\"candidateMoves\":"
            << stage_candidates[0].evaluation.mean_moves
            << ",\"bottomQuartileMoves\":"
            << stage_candidates[0].evaluation.lower_quartile_moves
            << ",\"clearsPerMove\":"
            << stage_candidates[0].evaluation.clears_per_move
            << ",\"revealsPerMove\":"
            << stage_candidates[0].evaluation.reveals_per_move
            << ",\"startingScore\":"
            << stage_candidates[1].evaluation.mean_score
            << ",\"startingMoves\":"
            << stage_candidates[1].evaluation.mean_moves
            << ",\"jointWins\":" << stage_gate.joint_win_count
            << ",\"passed\":" << (stage_gate.passed ? "true" : "false")
            << ",\"wallSeconds\":" << wall_seconds
            << ",\"peakRssBytes\":" << peakRssBytes() << "}\n";
  return stage_gate.passed ? 0 : 3;
}

}  // namespace drop7::tail_survival_cem

int main(int argc, char** argv) {
  using namespace drop7::tail_survival_cem;
  try {
    if (argc != 2) {
      throw std::invalid_argument("usage: --self-test | --preflight | --run");
    }
    const std::string_view mode(argv[1]);
    if (mode == "--self-test") return selfTest() ? 0 : 1;
    if (mode == "--preflight") {
      runPreflight();
      return 0;
    }
    if (mode == "--run") return run();
    throw std::invalid_argument("unknown tail CEM mode");
  } catch (const std::exception& error) {
    std::cerr << "tail survival CEM failure: " << error.what() << '\n';
    return 1;
  }
}
