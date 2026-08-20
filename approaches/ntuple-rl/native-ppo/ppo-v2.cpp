#define main drop7_fair_only_horizon_for_ppo_v2_main
#include "../../fair-expectimax/reference/fair-only-horizon.cpp"
#undef main

#include "../../../src/core/native/ppo.hpp"

#include <atomic>
#include <bit>
#include <cstring>
#include <future>
#include <optional>
#include <sstream>
#include <sys/resource.h>

// Places the policy on the fair-D1 state distribution with behavioral cloning
// and two DAgger rounds.  Conservative on-policy PPO runs only when the
// resulting warm start satisfies the fixed fair-D1 behavior gate.
namespace drop7::ppo_v2 {

namespace actor = ppo;
namespace fair = fair_only_horizon;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kAuditTrainingStart = 0x3f00'0000u;
constexpr std::uint32_t kImitationStart = 0x3f01'0000u;
constexpr std::uint32_t kDaggerStart = 0x3f03'0000u;
constexpr std::uint32_t kPpoStart = 0x3f04'0000u;
constexpr std::uint32_t kProbeStart = 0x3f10'0000u;
constexpr std::uint32_t kHeldoutStart = 0x3f20'0000u;
constexpr std::uint32_t kNetworkSeed = 0x3f00'c0deu;
constexpr int kImitationGames = 512;
constexpr int kDaggerRounds = 2;
constexpr int kDaggerGames = 256;
constexpr int kBehaviorMaximumMoves = 300;
constexpr int kEvaluationMaximumMoves = 1'000;
constexpr int kProbeGames = 64;
constexpr int kHeldoutGames = 64;
constexpr int kImitationEpochs = 10;
constexpr int kDaggerEpochs = 5;
constexpr int kPpoIterations = 16;
constexpr int kPpoEpisodes = 256;
constexpr int kPpoEpochs = 3;
constexpr int kMinibatch = 512;
constexpr int kDefaultThreads = 4;
constexpr float kImitationLearningRate = 0.001f;
constexpr float kDaggerLearningRate = 0.0005f;
constexpr float kPpoLearningRate = 0.0001f;
constexpr float kGamma = 0.995f;
constexpr float kLambda = 0.95f;
constexpr float kClipRatio = 0.15f;
constexpr float kEntropyCoefficient = 0.002f;
constexpr float kValueCoefficient = 0.10f;
constexpr float kGradientNorm = 0.5f;
constexpr double kWarmRandomRatio = 1.10;
constexpr double kWarmTeacherRatio = 0.70;
constexpr double kWarmClearRatio = 1.02;
constexpr double kFinalTeacherScoreRatio = 1.02;
constexpr double kFinalTeacherMovesRatio = 1.00;
constexpr std::size_t kMaximumBehaviorExamples =
    static_cast<std::size_t>(kImitationGames +
                             kDaggerRounds * kDaggerGames) *
    kBehaviorMaximumMoves;
constexpr std::array<int, kBoardSize> kColumnOrder{{3, 2, 4, 1, 5, 0, 6}};

static_assert(kLevelBonus == 7'000);
static_assert(actor::Layout::count == 8'240);
static_assert(kMaximumBehaviorExamples == 307'200);
static_assert(kAuditTrainingStart < kImitationStart &&
              kImitationStart < kDaggerStart && kDaggerStart < kPpoStart &&
              kPpoStart < kProbeStart && kProbeStart < kHeldoutStart);
static_assert((kAuditTrainingStart >> 24u) == 0x3fu &&
              (kImitationStart >> 24u) == 0x3fu &&
              (kDaggerStart >> 24u) == 0x3fu &&
              (kPpoStart >> 24u) == 0x3fu &&
              (kProbeStart >> 24u) == 0x3fu &&
              (kHeldoutStart >> 24u) == 0x3fu);
static_assert((kAuditTrainingStart >> 24u) != 0x3eu &&
              (kAuditTrainingStart >> 24u) != 0x7du &&
              (kAuditTrainingStart >> 24u) != 0xd7u);

std::mutex progress_mutex;

State publicState(const State& source) {
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

actor::Observation observePublic(const State& source) {
  const State state = publicState(source);
  actor::Observation result;
  result.board = state.board;
  result.next_disc = state.next_disc;
  int occupied = 0;
  int covered = 0;
  int maximum_height = 0;
  int roughness = 0;
  std::array<int, kBoardSize> heights{};
  for (int column = 0; column < kBoardSize; ++column) {
    for (int row = 0; row < kBoardSize; ++row) {
      const std::uint8_t cell = state.board[indexOf(row, column)];
      if (cell == kEmpty) continue;
      ++heights[column];
      ++occupied;
      if (cell == kSolid || cell == kCracked) ++covered;
    }
    maximum_height = std::max(maximum_height, heights[column]);
    if (state.board[column] == kEmpty) {
      result.legal_mask |= static_cast<std::uint8_t>(1u << column);
    }
  }
  for (int column = 1; column < kBoardSize; ++column) {
    roughness += std::abs(heights[column] - heights[column - 1]);
  }
  // Every scalar is a function of board, visible next disc, or rise phase.
  result.scalars[0] =
      static_cast<float>(state.moves_remaining) / kMovesPerLevel;
  result.scalars[1] = occupied / static_cast<float>(kCellCount);
  result.scalars[2] = covered / static_cast<float>(kCellCount);
  for (int column = 0; column < kBoardSize; ++column) {
    result.scalars[3 + column] = heights[column] / 7.0f;
  }
  result.scalars[10] = maximum_height / 7.0f;
  result.scalars[11] = roughness / 42.0f;
  result.scalars[12] = state.next_disc / 7.0f;
  return result;
}

actor::Observation mirrorObservation(const actor::Observation& source) {
  actor::Observation result = source;
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      result.board[indexOf(row, column)] =
          source.board[indexOf(row, kBoardSize - 1 - column)];
    }
  }
  result.legal_mask = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    if ((source.legal_mask & (1u << column)) != 0) {
      result.legal_mask |=
          static_cast<std::uint8_t>(1u << (kBoardSize - 1 - column));
    }
    result.scalars[3 + column] = source.scalars[3 + kBoardSize - 1 - column];
  }
  return result;
}

bool isSymmetric(const actor::Observation& observation) {
  return mirrorObservation(observation).board == observation.board;
}

struct Prediction {
  actor::Observation observation{};
  actor::Observation mirrored_observation{};
  actor::ForwardCache direct{};
  actor::ForwardCache mirrored{};
  std::array<float, kBoardSize> probabilities{};
  float value = 0.0f;
};

Prediction predict(const actor::Network& network,
                   const actor::Observation& observation) {
  Prediction result;
  result.observation = observation;
  result.mirrored_observation = mirrorObservation(observation);
  result.direct = network.forward(result.observation);
  result.mirrored = network.forward(result.mirrored_observation);
  for (int action = 0; action < kBoardSize; ++action) {
    result.probabilities[action] =
        0.5f * (result.direct.probabilities[action] +
                result.mirrored.probabilities[kBoardSize - 1 - action]);
  }
  result.value = 0.5f * (result.direct.value + result.mirrored.value);
  return result;
}

int greedyAction(const Prediction& prediction) {
  // Selecting center on a symmetric state makes the deterministic action
  // equivariant whenever the invariant action is legal.  The probability
  // policy itself remains exactly equivariant for every state.
  if (isSymmetric(prediction.observation) &&
      prediction.probabilities[kBoardSize / 2] > 0.0f) {
    return kBoardSize / 2;
  }
  int selected = -1;
  float best = -1.0f;
  for (const int action : kColumnOrder) {
    if (prediction.probabilities[action] > best) {
      best = prediction.probabilities[action];
      selected = action;
    }
  }
  return selected;
}

int sampleAction(const Prediction& prediction, Mulberry32& random) {
  const double sample = random.nextUnit();
  double cumulative = 0.0;
  int fallback = -1;
  for (int action = 0; action < kBoardSize; ++action) {
    if (prediction.probabilities[action] <= 0.0f) continue;
    fallback = action;
    cumulative += prediction.probabilities[action];
    if (sample < cumulative) return action;
  }
  return fallback;
}

void accumulateEquivariantGradient(
    const actor::Network& network, const Prediction& prediction, int action,
    float policy_coefficient, float value_derivative,
    float entropy_coefficient, std::vector<float>& gradient) {
  const int mirrored_action = kBoardSize - 1 - action;
  const float direct_probability = prediction.direct.probabilities[action];
  const float mirrored_probability =
      prediction.mirrored.probabilities[mirrored_action];
  const float denominator = direct_probability + mirrored_probability;
  if (!(denominator > 0.0f)) {
    throw std::runtime_error("equivariant policy assigned zero legal mass");
  }
  const float direct_weight = direct_probability / denominator;
  const float mirrored_weight = mirrored_probability / denominator;
  // These branch weights are the exact derivative of log((p + p') / 2).
  // Entropy regularizes the two shared branches rather than approximating the
  // entropy gradient of their mixture.
  network.accumulateGradient(
      prediction.observation, prediction.direct, action,
      policy_coefficient * direct_weight, value_derivative * 0.5f,
      entropy_coefficient * 0.5f, gradient);
  network.accumulateGradient(
      prediction.mirrored_observation, prediction.mirrored, mirrored_action,
      policy_coefficient * mirrored_weight, value_derivative * 0.5f,
      entropy_coefficient * 0.5f, gradient);
}

int fairDepthOneAction(const State& source) {
  if (source.game_over) return -1;
  bool mirrored = false;
  const State state =
      cfpi::detail::canonicalState(publicState(source), mirrored);
  const std::uint32_t chance_seed = cfpi::detail::scenarioSeedForState(
      state, fair::kPolicySeed, 1);
  int selected = -1;
  double best = -std::numeric_limits<double>::infinity();
  for (const int action : kColumnOrder) {
    if (!isLegal(state.board, action)) continue;
    cfpi::detail::StratifiedRandom random{chance_seed, 0, 1, 0};
    MoveResult move;
    if (!cfpi::detail::playMoveSampled(state, action, random, move)) continue;
    double value = static_cast<double>(move.score_delta);
    if (move.state.game_over) {
      value += fair::kTerminalUtility;
    } else {
      move.state = publicState(move.state);
      move.state.next_disc =
          cfpi::detail::sampledNextDisc(chance_seed, 0, 1);
      value += fair::fairLeaf(move.state);
    }
    if (value > best) {
      best = value;
      selected = action;
    }
  }
  if (selected < 0) selected = centerFirstMove(state.board);
  return mirrored && selected >= 0 ? kBoardSize - 1 - selected : selected;
}

struct BehaviorExample {
  actor::Observation observation{};
  int teacher_action = -1;
};

struct BehaviorCollection {
  std::vector<BehaviorExample> examples;
  std::int64_t total_score = 0;
  std::int64_t total_moves = 0;
};

enum class CollectionPolicy { kTeacher, kStudent };

BehaviorCollection collectBehavior(const actor::Network& network,
                                   std::uint32_t start, int games,
                                   CollectionPolicy policy, int threads,
                                   std::string_view label) {
  const int thread_count = std::max(1, std::min(threads, games));
  std::vector<BehaviorCollection> partial(
      static_cast<std::size_t>(thread_count));
  std::vector<std::future<void>> workers;
  workers.reserve(static_cast<std::size_t>(thread_count));
  for (int thread = 0; thread < thread_count; ++thread) {
    workers.push_back(std::async(std::launch::async, [&, thread]() {
      BehaviorCollection& destination = partial[thread];
      for (int game = thread; game < games; game += thread_count) {
        const std::uint32_t seed = start + static_cast<std::uint32_t>(game);
        State state = initialHeadlessState(seed);
        while (!state.game_over && state.moves_played < kBehaviorMaximumMoves) {
          const actor::Observation observation = observePublic(state);
          const int teacher_action = fairDepthOneAction(state);
          if (!isLegal(state.board, teacher_action)) {
            throw std::runtime_error("fair D1 teacher returned illegal action");
          }
          destination.examples.push_back({observation, teacher_action});
          int action = teacher_action;
          if (policy == CollectionPolicy::kStudent) {
            action = greedyAction(predict(network, observation));
          }
          if (!isLegal(state.board, action)) {
            throw std::runtime_error("DAgger student returned illegal action");
          }
          MoveResult move;
          if (!playHeadlessMove(state, seed, action, move)) {
            throw std::runtime_error("behavior transition failed");
          }
        }
        destination.total_score += state.score;
        destination.total_moves += state.moves_played;
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  BehaviorCollection result;
  std::size_t count = 0;
  for (const auto& item : partial) count += item.examples.size();
  result.examples.reserve(count);
  for (auto& item : partial) {
    result.examples.insert(result.examples.end(),
                           std::make_move_iterator(item.examples.begin()),
                           std::make_move_iterator(item.examples.end()));
    result.total_score += item.total_score;
    result.total_moves += item.total_moves;
  }
  if (result.examples.size() > kMaximumBehaviorExamples) {
    throw std::runtime_error("behavior dataset exceeded static bound");
  }
  const std::lock_guard<std::mutex> lock(progress_mutex);
  std::cerr << "ppo-v2 " << label << " examples " << result.examples.size()
            << " mean score "
            << static_cast<double>(result.total_score) / games
            << " mean moves "
            << static_cast<double>(result.total_moves) / games << '\n';
  return result;
}

struct ImitationMetrics {
  double mean_loss = 0.0;
  double agreement = 0.0;
  int updates = 0;
};

void deterministicShuffle(std::vector<int>& order, Mulberry32& random) {
  for (std::size_t index = order.size(); index > 1; --index) {
    const std::size_t selected = static_cast<std::size_t>(
        (static_cast<std::uint64_t>(random.nextBits()) * index) >> 32);
    std::swap(order[index - 1], order[selected]);
  }
}

ImitationMetrics trainImitation(actor::Network& network,
                                const std::vector<BehaviorExample>& examples,
                                int epochs, float learning_rate,
                                Mulberry32& training_random) {
  if (examples.empty()) throw std::runtime_error("empty imitation dataset");
  std::vector<int> order(examples.size());
  std::iota(order.begin(), order.end(), 0);
  ImitationMetrics result;
  std::uint64_t metric_count = 0;
  for (int epoch = 0; epoch < epochs; ++epoch) {
    deterministicShuffle(order, training_random);
    for (std::size_t begin = 0; begin < order.size(); begin += kMinibatch) {
      const std::size_t end = std::min(order.size(), begin + kMinibatch);
      const float inverse = 1.0f / static_cast<float>(end - begin);
      std::vector<float> gradient = network.zeroGradient();
      for (std::size_t offset = begin; offset < end; ++offset) {
        const BehaviorExample& example = examples[order[offset]];
        const Prediction prediction = predict(network, example.observation);
        const float probability = std::max(
            1e-12f, prediction.probabilities[example.teacher_action]);
        result.mean_loss -= std::log(probability);
        result.agreement +=
            greedyAction(prediction) == example.teacher_action ? 1.0 : 0.0;
        ++metric_count;
        accumulateEquivariantGradient(network, prediction,
                                      example.teacher_action, -inverse, 0.0f,
                                      0.0f, gradient);
      }
      network.applyAdam(gradient, learning_rate, 1.0f);
      ++result.updates;
    }
  }
  result.mean_loss /= static_cast<double>(metric_count);
  result.agreement /= static_cast<double>(metric_count);
  return result;
}

ImitationMetrics evaluateImitation(
    const actor::Network& network,
    const std::vector<BehaviorExample>& examples) {
  ImitationMetrics result;
  for (const BehaviorExample& example : examples) {
    const Prediction prediction = predict(network, example.observation);
    result.mean_loss -= std::log(std::max(
        1e-12f, prediction.probabilities[example.teacher_action]));
    result.agreement +=
        greedyAction(prediction) == example.teacher_action ? 1.0 : 0.0;
  }
  result.mean_loss /= static_cast<double>(examples.size());
  result.agreement /= static_cast<double>(examples.size());
  return result;
}

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  std::int64_t numbered_cleared = 0;
  std::int64_t covers_revealed = 0;
  int maximum_chain = 0;
  bool censored = false;
};

enum class EvaluationPolicy { kRandom, kTeacher, kNetwork };

GameResult playEvaluationGame(const actor::Network& network,
                              std::uint32_t seed,
                              EvaluationPolicy policy) {
  State state = initialHeadlessState(seed);
  Mulberry32 random(mix32(seed ^ 0x5632'4556u));
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kEvaluationMaximumMoves) {
    int action = -1;
    if (policy == EvaluationPolicy::kRandom) {
      int legal_count = 0;
      const auto legal = legalColumns(state.board, legal_count);
      const int selected = static_cast<int>(
          (static_cast<std::uint64_t>(random.nextBits()) * legal_count) >> 32);
      action = legal[selected];
    } else if (policy == EvaluationPolicy::kTeacher) {
      action = fairDepthOneAction(state);
    } else {
      action = greedyAction(predict(network, observePublic(state)));
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("evaluation policy returned illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("evaluation transition failed");
    }
    for (const Wave& wave : move.waves) {
      result.numbered_cleared += wave.cleared;
      result.covers_revealed += wave.revealed;
      result.maximum_chain = std::max(result.maximum_chain, wave.depth);
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  return result;
}

std::vector<GameResult> evaluate(const actor::Network& network,
                                 std::uint32_t start, int games,
                                 EvaluationPolicy policy, int threads,
                                 std::string_view label) {
  std::vector<GameResult> result(static_cast<std::size_t>(games));
  std::atomic<int> next{0};
  const int thread_count = std::max(1, std::min(threads, games));
  std::vector<std::future<void>> workers;
  for (int thread = 0; thread < thread_count; ++thread) {
    workers.push_back(std::async(std::launch::async, [&, thread]() {
      static_cast<void>(thread);
      for (;;) {
        const int game = next.fetch_add(1);
        if (game >= games) break;
        result[static_cast<std::size_t>(game)] = playEvaluationGame(
            network, start + static_cast<std::uint32_t>(game), policy);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  const std::lock_guard<std::mutex> lock(progress_mutex);
  std::cerr << "ppo-v2 evaluated " << label << " " << games << " games\n";
  return result;
}

struct Summary {
  int games = 0;
  int censored = 0;
  double mean_score = 0.0;
  double median_score = 0.0;
  double mean_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double mean_maximum_chain = 0.0;
  double standard_error = 0.0;
  std::int64_t minimum_score = 0;
  std::int64_t maximum_score = 0;
};

double median(std::vector<std::int64_t> values) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const std::size_t middle = values.size() / 2;
  if (values.size() % 2 == 1) return static_cast<double>(values[middle]);
  return 0.5 * static_cast<double>(values[middle - 1] + values[middle]);
}

Summary summarize(const std::vector<GameResult>& games) {
  Summary result;
  result.games = static_cast<int>(games.size());
  if (games.empty()) return result;
  result.minimum_score = std::numeric_limits<std::int64_t>::max();
  result.maximum_score = std::numeric_limits<std::int64_t>::min();
  std::vector<std::int64_t> scores;
  double score_squares = 0.0;
  std::int64_t total_moves = 0;
  std::int64_t total_clears = 0;
  std::int64_t total_reveals = 0;
  for (const GameResult& game : games) {
    scores.push_back(game.score);
    result.mean_score += static_cast<double>(game.score);
    score_squares += static_cast<double>(game.score) * game.score;
    total_moves += game.moves;
    total_clears += game.numbered_cleared;
    total_reveals += game.covers_revealed;
    result.mean_maximum_chain += game.maximum_chain;
    result.minimum_score = std::min(result.minimum_score, game.score);
    result.maximum_score = std::max(result.maximum_score, game.score);
    result.censored += game.censored ? 1 : 0;
  }
  result.mean_score /= result.games;
  result.median_score = median(std::move(scores));
  result.mean_moves = static_cast<double>(total_moves) / result.games;
  result.mean_maximum_chain /= result.games;
  if (total_moves > 0) {
    result.clears_per_move = static_cast<double>(total_clears) / total_moves;
    result.reveals_per_move = static_cast<double>(total_reveals) / total_moves;
  }
  if (result.games > 1) {
    const double variance =
        (score_squares - result.games * result.mean_score * result.mean_score) /
        static_cast<double>(result.games - 1);
    result.standard_error =
        std::sqrt(std::max(0.0, variance) / result.games);
  }
  return result;
}

float transitionReward(const MoveResult& move) {
  int cleared = 0;
  int revealed = 0;
  for (const Wave& wave : move.waves) {
    cleared += wave.cleared;
    revealed += wave.revealed;
  }
  float reward = 0.10f + 0.020f * cleared + 0.015f * revealed +
                 static_cast<float>(move.score_delta) / 700'000.0f;
  if (move.state.game_over) reward -= 0.50f;
  return reward;
}

struct PpoSample {
  actor::Observation observation{};
  int action = -1;
  float old_log_probability = 0.0f;
  float old_value = 0.0f;
  float reward = 0.0f;
  bool terminal = false;
  float advantage = 0.0f;
  float return_value = 0.0f;
};

void finishAdvantages(std::vector<PpoSample>& samples, float bootstrap) {
  float next_value = bootstrap;
  float advantage = 0.0f;
  for (auto iterator = samples.rbegin(); iterator != samples.rend(); ++iterator) {
    const float nonterminal = iterator->terminal ? 0.0f : 1.0f;
    const float delta = iterator->reward + kGamma * next_value * nonterminal -
                        iterator->old_value;
    advantage = delta + kGamma * kLambda * nonterminal * advantage;
    iterator->advantage = advantage;
    iterator->return_value = advantage + iterator->old_value;
    next_value = iterator->old_value;
  }
}

struct PpoCollection {
  std::vector<PpoSample> samples;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  int censored = 0;
};

PpoCollection collectPpo(const actor::Network& network,
                         std::uint32_t start, int threads) {
  const int thread_count = std::max(1, std::min(threads, kPpoEpisodes));
  std::vector<PpoCollection> partial(static_cast<std::size_t>(thread_count));
  std::vector<std::future<void>> workers;
  for (int thread = 0; thread < thread_count; ++thread) {
    workers.push_back(std::async(std::launch::async, [&, thread]() {
      PpoCollection& destination = partial[thread];
      for (int episode = thread; episode < kPpoEpisodes;
           episode += thread_count) {
        const std::uint32_t seed = start + static_cast<std::uint32_t>(episode);
        State state = initialHeadlessState(seed);
        Mulberry32 policy_random(mix32(seed ^ 0x5032'504fu));
        std::vector<PpoSample> trajectory;
        while (!state.game_over && state.moves_played < kEvaluationMaximumMoves) {
          PpoSample sample;
          sample.observation = observePublic(state);
          const Prediction prediction = predict(network, sample.observation);
          sample.action = sampleAction(prediction, policy_random);
          sample.old_log_probability = std::log(std::max(
              1e-12f, prediction.probabilities[sample.action]));
          sample.old_value = prediction.value;
          MoveResult move;
          if (!playHeadlessMove(state, seed, sample.action, move)) {
            throw std::runtime_error("PPO transition failed");
          }
          sample.reward = transitionReward(move);
          sample.terminal = state.game_over;
          trajectory.push_back(sample);
        }
        const bool censored = !state.game_over;
        const float bootstrap =
            censored ? predict(network, observePublic(state)).value : 0.0f;
        finishAdvantages(trajectory, bootstrap);
        destination.samples.insert(
            destination.samples.end(),
            std::make_move_iterator(trajectory.begin()),
            std::make_move_iterator(trajectory.end()));
        destination.mean_score += static_cast<double>(state.score);
        destination.mean_moves += state.moves_played;
        destination.censored += censored ? 1 : 0;
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  PpoCollection result;
  std::size_t samples = 0;
  for (const auto& item : partial) samples += item.samples.size();
  result.samples.reserve(samples);
  for (auto& item : partial) {
    result.samples.insert(result.samples.end(),
                          std::make_move_iterator(item.samples.begin()),
                          std::make_move_iterator(item.samples.end()));
    result.mean_score += item.mean_score;
    result.mean_moves += item.mean_moves;
    result.censored += item.censored;
  }
  result.mean_score /= kPpoEpisodes;
  result.mean_moves /= kPpoEpisodes;
  return result;
}

struct PpoMetrics {
  double policy_loss = 0.0;
  double value_loss = 0.0;
  double entropy = 0.0;
  double approximate_kl = 0.0;
  double clip_fraction = 0.0;
  int updates = 0;
};

PpoMetrics updatePpo(actor::Network& network,
                     std::vector<PpoSample>& samples,
                     Mulberry32& training_random) {
  double advantage_mean = 0.0;
  for (const PpoSample& sample : samples) advantage_mean += sample.advantage;
  advantage_mean /= samples.size();
  double variance = 0.0;
  for (const PpoSample& sample : samples) {
    const double difference = sample.advantage - advantage_mean;
    variance += difference * difference;
  }
  const float scale = static_cast<float>(
      1.0 / std::sqrt(variance / samples.size() + 1e-8));
  for (PpoSample& sample : samples) {
    sample.advantage =
        static_cast<float>((sample.advantage - advantage_mean) * scale);
  }

  std::vector<int> order(samples.size());
  std::iota(order.begin(), order.end(), 0);
  PpoMetrics result;
  std::uint64_t metric_count = 0;
  for (int epoch = 0; epoch < kPpoEpochs; ++epoch) {
    deterministicShuffle(order, training_random);
    for (std::size_t begin = 0; begin < order.size(); begin += kMinibatch) {
      const std::size_t end = std::min(order.size(), begin + kMinibatch);
      const float inverse = 1.0f / static_cast<float>(end - begin);
      std::vector<float> gradient = network.zeroGradient();
      for (std::size_t offset = begin; offset < end; ++offset) {
        const PpoSample& sample = samples[order[offset]];
        const Prediction prediction = predict(network, sample.observation);
        const float probability =
            std::max(1e-12f, prediction.probabilities[sample.action]);
        const float log_probability = std::log(probability);
        const float ratio =
            std::exp(log_probability - sample.old_log_probability);
        const float clipped_ratio =
            std::clamp(ratio, 1.0f - kClipRatio, 1.0f + kClipRatio);
        const bool clipped =
            (sample.advantage >= 0.0f && ratio > 1.0f + kClipRatio) ||
            (sample.advantage < 0.0f && ratio < 1.0f - kClipRatio);
        const float policy_coefficient =
            clipped ? 0.0f : -sample.advantage * ratio * inverse;
        const float value_difference = prediction.value - sample.return_value;
        const float value_derivative =
            2.0f * kValueCoefficient * value_difference * inverse;
        accumulateEquivariantGradient(
            network, prediction, sample.action, policy_coefficient,
            value_derivative, kEntropyCoefficient * inverse, gradient);
        const float raw_objective = ratio * sample.advantage;
        const float clipped_objective = clipped_ratio * sample.advantage;
        result.policy_loss -= std::min(raw_objective, clipped_objective);
        result.value_loss += 0.5 * value_difference * value_difference;
        float entropy = 0.0f;
        for (const float candidate : prediction.probabilities) {
          if (candidate > 0.0f) entropy -= candidate * std::log(candidate);
        }
        result.entropy += entropy;
        result.approximate_kl += sample.old_log_probability - log_probability;
        result.clip_fraction += clipped ? 1.0 : 0.0;
        ++metric_count;
      }
      network.applyAdam(gradient, kPpoLearningRate, kGradientNorm);
      ++result.updates;
    }
  }
  const double inverse = 1.0 / static_cast<double>(metric_count);
  result.policy_loss *= inverse;
  result.value_loss *= inverse;
  result.entropy *= inverse;
  result.approximate_kl *= inverse;
  result.clip_fraction *= inverse;
  return result;
}

std::uint64_t parameterFingerprint(const actor::Network& network) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const float parameter : network.parameters()) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(parameter);
    for (int shift = 0; shift < 32; shift += 8) {
      hash ^= static_cast<std::uint8_t>(bits >> shift);
      hash *= 0x0000'0100'0000'01b3ull;
    }
  }
  return hash;
}

struct CheckpointHeader {
  std::array<char, 8> magic{{'D', '7', 'P', 'P', 'O', 'V', '2', '\0'}};
  std::uint32_t version = 2;
  std::uint32_t parameter_count = actor::Layout::count;
  std::uint32_t iteration = 0;
  std::uint32_t network_seed = kNetworkSeed;
  std::uint64_t fingerprint = 0;
};

void saveCheckpoint(const actor::Network& network, const std::string& path,
                    int iteration) {
  CheckpointHeader header;
  header.iteration = static_cast<std::uint32_t>(iteration);
  header.fingerprint = parameterFingerprint(network);
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("could not open PPO-v2 checkpoint");
  output.write(reinterpret_cast<const char*>(&header), sizeof(header));
  for (const float parameter : network.parameters()) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(parameter);
    output.write(reinterpret_cast<const char*>(&bits), sizeof(bits));
  }
  if (!output) throw std::runtime_error("failed to write PPO-v2 checkpoint");
}

actor::Network loadCheckpoint(const std::string& path,
                              std::uint32_t& iteration) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not read PPO-v2 checkpoint");
  CheckpointHeader header;
  input.read(reinterpret_cast<char*>(&header), sizeof(header));
  const CheckpointHeader expected;
  if (!input || header.magic != expected.magic || header.version != 2 ||
      header.parameter_count != actor::Layout::count ||
      header.network_seed != kNetworkSeed) {
    throw std::runtime_error("invalid PPO-v2 checkpoint header");
  }
  actor::Network network(kNetworkSeed);
  for (int index = 0; index < actor::Layout::count; ++index) {
    std::uint32_t bits = 0;
    input.read(reinterpret_cast<char*>(&bits), sizeof(bits));
    network.setParameter(index, std::bit_cast<float>(bits));
  }
  char trailing = 0;
  if (!input || input.read(&trailing, 1)) {
    throw std::runtime_error("invalid PPO-v2 checkpoint payload");
  }
  if (parameterFingerprint(network) != header.fingerprint) {
    throw std::runtime_error("PPO-v2 checkpoint fingerprint mismatch");
  }
  iteration = header.iteration;
  return network;
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

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

State asymmetricFixture() {
  State state;
  state.board.fill(kEmpty);
  state.board[indexOf(6, 0)] = kSolid;
  state.board[indexOf(6, 1)] = 4;
  state.board[indexOf(6, 2)] = 2;
  state.board[indexOf(5, 2)] = kCracked;
  state.board[indexOf(6, 4)] = 6;
  state.next_disc = 3;
  state.moves_remaining = 3;
  return state;
}

void runSelfTests(const std::string& checkpoint) {
  actor::Network network(kNetworkSeed);
  const State state = asymmetricFixture();
  const actor::Observation observation = observePublic(state);
  const Prediction first = predict(network, observation);
  const Prediction repeat = predict(network, observation);
  expect(first.probabilities == repeat.probabilities &&
             first.value == repeat.value,
         "equivariant prediction was not deterministic");

  State reflected = publicState(state);
  reflected.board = cfpi::detail::mirrorBoard(state.board);
  const Prediction mirrored = predict(network, observePublic(reflected));
  for (int action = 0; action < kBoardSize; ++action) {
    expect(first.probabilities[action] ==
               mirrored.probabilities[kBoardSize - 1 - action],
           "policy probabilities were not exactly reflection equivariant");
  }
  expect(first.value == mirrored.value,
         "critic was not exactly reflection invariant");

  State metadata = state;
  metadata.score = 9'999'999;
  metadata.level = 87;
  metadata.moves_played = 901;
  const actor::Observation metadata_observation = observePublic(metadata);
  expect(observation.board == metadata_observation.board &&
             observation.next_disc == metadata_observation.next_disc &&
             observation.scalars == metadata_observation.scalars &&
             observation.legal_mask == metadata_observation.legal_mask,
         "observation used non-public metadata");

  const actor::Observation symmetric =
      observePublic(initialHeadlessState(0x3f7f'0042u));
  const Prediction symmetric_prediction = predict(network, symmetric);
  for (int action = 0; action < kBoardSize; ++action) {
    expect(symmetric_prediction.probabilities[action] ==
               symmetric_prediction.probabilities[kBoardSize - 1 - action],
           "symmetric state did not have symmetric action probabilities");
  }

  std::vector<PpoSample> terminal(2);
  terminal[0].reward = 1.0f;
  terminal[0].old_value = 0.2f;
  terminal[1].reward = 2.0f;
  terminal[1].old_value = 0.3f;
  terminal[1].terminal = true;
  finishAdvantages(terminal, 99.0f);
  const float terminal_return = terminal[1].return_value;
  std::vector<PpoSample> truncated = terminal;
  truncated[1].terminal = false;
  finishAdvantages(truncated, 4.0f);
  expect(std::abs(terminal_return - 2.0f) < 1e-5f &&
             truncated[1].return_value > terminal[1].return_value,
         "terminal/truncation bootstrap semantics failed");

  MoveResult reward_move;
  reward_move.score_delta = 7'000;
  reward_move.state.game_over = false;
  reward_move.waves.push_back({1, 2, 1, 14});
  const float live_reward = transitionReward(reward_move);
  reward_move.state.game_over = true;
  expect(std::abs(live_reward - transitionReward(reward_move) - 0.5f) <
             1e-6f,
         "terminal reward penalty failed");

  const std::string test_path = checkpoint + ".self-test";
  saveCheckpoint(network, test_path, 7);
  std::uint32_t loaded_iteration = 0;
  const actor::Network loaded = loadCheckpoint(test_path, loaded_iteration);
  expect(loaded_iteration == 7 &&
             loaded.parameters() == network.parameters() &&
             parameterFingerprint(loaded) == parameterFingerprint(network),
         "deterministic checkpoint roundtrip failed");
  std::remove(test_path.c_str());
}

void writeSummary(std::ostream& output, const Summary& summary) {
  output << "{\"games\":" << summary.games
         << ",\"censored\":" << summary.censored
         << ",\"meanScore\":" << summary.mean_score
         << ",\"medianScore\":" << summary.median_score
         << ",\"standardError\":" << summary.standard_error
         << ",\"minimumScore\":" << summary.minimum_score
         << ",\"maximumScore\":" << summary.maximum_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"numberedClearsPerMove\":" << summary.clears_per_move
         << ",\"coversRevealedPerMove\":" << summary.reveals_per_move
         << ",\"meanMaximumChain\":" << summary.mean_maximum_chain << '}';
}

void writeGames(std::ostream& output, const std::vector<GameResult>& games) {
  output << '[';
  for (std::size_t index = 0; index < games.size(); ++index) {
    if (index > 0) output << ',';
    const GameResult& game = games[index];
    output << "{\"seed\":" << game.seed << ",\"score\":" << game.score
           << ",\"moves\":" << game.moves
           << ",\"numberedCleared\":" << game.numbered_cleared
           << ",\"coversRevealed\":" << game.covers_revealed
           << ",\"maximumChain\":" << game.maximum_chain
           << ",\"censored\":" << (game.censored ? "true" : "false")
           << '}';
  }
  output << ']';
}

struct IterationRecord {
  int iteration = 0;
  std::size_t samples = 0;
  double training_score = 0.0;
  double training_moves = 0.0;
  int censored = 0;
  PpoMetrics update{};
  Summary probe{};
};

struct Options {
  std::string output = "/tmp/drop7-ppo-v2.json";
  std::string checkpoint = "/tmp/drop7-ppo-v2.bin";
  int threads = kDefaultThreads;
  bool self_test_only = false;
};

Options parseOptions(int argc, char** argv) {
  Options result;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--self-test-only") {
      result.self_test_only = true;
      continue;
    }
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    if (argument == "--output") {
      result.output = argv[++index];
    } else if (argument == "--checkpoint") {
      result.checkpoint = argv[++index];
    } else if (argument == "--threads") {
      result.threads = std::stoi(argv[++index]);
      if (result.threads < 1 || result.threads > 64) {
        throw std::invalid_argument("threads must be in [1,64]");
      }
    } else {
      throw std::invalid_argument("unknown option " + argument);
    }
  }
  return result;
}

int run(const Options& options) {
  runSelfTests(options.checkpoint);
  std::cerr << "ppo-v2 self-tests passed\n";
  if (options.self_test_only) return 0;
  const auto started = Clock::now();
  actor::Network network(kNetworkSeed);
  Mulberry32 training_random(mix32(kNetworkSeed ^ 0x5632'5550u));

  const std::vector<GameResult> random_probe = evaluate(
      network, kProbeStart, kProbeGames, EvaluationPolicy::kRandom,
      options.threads, "probe-random");
  const std::vector<GameResult> teacher_probe = evaluate(
      network, kProbeStart, kProbeGames, EvaluationPolicy::kTeacher,
      options.threads, "probe-fair-d1");
  const std::vector<GameResult> initial_probe = evaluate(
      network, kProbeStart, kProbeGames, EvaluationPolicy::kNetwork,
      options.threads, "probe-untrained");
  const Summary random_summary = summarize(random_probe);
  const Summary teacher_summary = summarize(teacher_probe);
  const Summary initial_summary = summarize(initial_probe);

  BehaviorCollection behavior = collectBehavior(
      network, kImitationStart, kImitationGames, CollectionPolicy::kTeacher,
      options.threads, "teacher-collection");
  ImitationMetrics imitation = trainImitation(
      network, behavior.examples, kImitationEpochs,
      kImitationLearningRate, training_random);
  for (int round = 0; round < kDaggerRounds; ++round) {
    BehaviorCollection dagger = collectBehavior(
        network,
        kDaggerStart + static_cast<std::uint32_t>(round * kDaggerGames),
        kDaggerGames, CollectionPolicy::kStudent, options.threads,
        std::string("dagger-") + std::to_string(round + 1));
    behavior.examples.insert(
        behavior.examples.end(),
        std::make_move_iterator(dagger.examples.begin()),
        std::make_move_iterator(dagger.examples.end()));
    imitation = trainImitation(network, behavior.examples, kDaggerEpochs,
                               kDaggerLearningRate, training_random);
  }
  imitation = evaluateImitation(network, behavior.examples);
  const std::vector<GameResult> warm_probe = evaluate(
      network, kProbeStart, kProbeGames, EvaluationPolicy::kNetwork,
      options.threads, "probe-warm-start");
  const Summary warm_summary = summarize(warm_probe);
  const bool warm_gate =
      warm_summary.censored == 0 &&
      warm_summary.mean_score >=
          std::max(random_summary.mean_score * kWarmRandomRatio,
                   teacher_summary.mean_score * kWarmTeacherRatio) &&
      warm_summary.mean_moves >=
          std::max(random_summary.mean_moves * kWarmRandomRatio,
                   teacher_summary.mean_moves * kWarmTeacherRatio) &&
      warm_summary.clears_per_move >=
          random_summary.clears_per_move * kWarmClearRatio;

  actor::Network best_network = network;
  Summary best_probe = warm_summary;
  int best_iteration = 0;
  std::vector<IterationRecord> iterations;
  if (warm_gate) {
    for (int iteration = 1; iteration <= kPpoIterations; ++iteration) {
      const std::uint32_t training_start =
          kPpoStart +
          static_cast<std::uint32_t>((iteration - 1) * kPpoEpisodes);
      PpoCollection collection =
          collectPpo(network, training_start, options.threads);
      const PpoMetrics update =
          updatePpo(network, collection.samples, training_random);
      const std::vector<GameResult> probe = evaluate(
          network, kProbeStart, kProbeGames, EvaluationPolicy::kNetwork,
          options.threads,
          std::string("probe-ppo-") + std::to_string(iteration));
      const Summary summary = summarize(probe);
      iterations.push_back({iteration, collection.samples.size(),
                            collection.mean_score, collection.mean_moves,
                            collection.censored, update, summary});
      if (summary.mean_score > best_probe.mean_score) {
        best_probe = summary;
        best_network = network;
        best_iteration = iteration;
      }
      std::cerr << "ppo-v2 iteration " << iteration << '/' << kPpoIterations
                << " train " << collection.mean_score << '/'
                << collection.mean_moves << " probe " << summary.mean_score
                << '/' << summary.mean_moves << " KL "
                << update.approximate_kl << '\n';
    }
  }

  const bool final_gate =
      warm_gate && best_probe.censored == 0 &&
      best_probe.mean_score >=
          teacher_summary.mean_score * kFinalTeacherScoreRatio &&
      best_probe.mean_moves >=
          teacher_summary.mean_moves * kFinalTeacherMovesRatio &&
      best_probe.clears_per_move >= teacher_summary.clears_per_move;
  saveCheckpoint(best_network, options.checkpoint, best_iteration);
  std::uint32_t loaded_iteration = 0;
  const actor::Network loaded =
      loadCheckpoint(options.checkpoint, loaded_iteration);
  if (loaded_iteration != static_cast<std::uint32_t>(best_iteration) ||
      loaded.parameters() != best_network.parameters()) {
    throw std::runtime_error("final checkpoint did not roundtrip exactly");
  }

  std::vector<GameResult> heldout_random;
  std::vector<GameResult> heldout_teacher;
  std::vector<GameResult> heldout_candidate;
  Summary heldout_random_summary;
  Summary heldout_teacher_summary;
  Summary heldout_candidate_summary;
  bool heldout_passed = false;
  if (final_gate) {
    heldout_random = evaluate(best_network, kHeldoutStart, kHeldoutGames,
                              EvaluationPolicy::kRandom, options.threads,
                              "heldout-random");
    heldout_teacher = evaluate(best_network, kHeldoutStart, kHeldoutGames,
                               EvaluationPolicy::kTeacher, options.threads,
                               "heldout-fair-d1");
    heldout_candidate = evaluate(best_network, kHeldoutStart, kHeldoutGames,
                                 EvaluationPolicy::kNetwork, options.threads,
                                 "heldout-frozen-candidate");
    heldout_random_summary = summarize(heldout_random);
    heldout_teacher_summary = summarize(heldout_teacher);
    heldout_candidate_summary = summarize(heldout_candidate);
    heldout_passed =
        heldout_candidate_summary.censored == 0 &&
        heldout_candidate_summary.mean_score >
            heldout_teacher_summary.mean_score &&
        heldout_candidate_summary.mean_moves >=
            heldout_teacher_summary.mean_moves &&
        heldout_candidate_summary.clears_per_move >=
            heldout_teacher_summary.clears_per_move;
  }

  const double wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open PPO-v2 artifact");
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"drop7-ppo-v2-dagger-rescue\",\n"
         << "  \"preregistered\":true,\n"
         << "  \"publicStateOnly\":true,\n"
         << "  \"exactReflectionEquivariantDistribution\":true,\n"
         << "  \"v1Audit\":{\"trainingStart\":" << kAuditTrainingStart
         << ",\"probeStart\":" << kProbeStart
         << ",\"iterations\":8,\"episodesPerIteration\":128,"
            "\"bestMeanScore\":24503.344,\"bestMeanMoves\":22.109,"
            "\"randomMeanScore\":31835.25,"
            "\"randomMeanMoves\":26.9375,\"rejected\":true},\n"
         << "  \"seedDiscipline\":{\"imitationStart\":"
         << kImitationStart << ",\"daggerStart\":" << kDaggerStart
         << ",\"ppoStart\":" << kPpoStart
         << ",\"probeStart\":" << kProbeStart
         << ",\"heldoutStart\":" << kHeldoutStart
         << ",\"forbiddenFamilies\":[\"0x3e\",\"0x7d\",\"0xd7\"]},\n"
         << "  \"architecture\":{\"parameters\":"
         << actor::Layout::count
         << ",\"sharedTwoPassReflectionEnsemble\":true,"
            "\"observation\":[\"board\",\"nextDisc\","
            "\"movesRemaining\",\"derivedBoardScalars\"]},\n"
         << "  \"warmStart\":{\"teacherGames\":" << kImitationGames
         << ",\"daggerRounds\":" << kDaggerRounds
         << ",\"daggerGamesPerRound\":" << kDaggerGames
         << ",\"examples\":" << behavior.examples.size()
         << ",\"finalCrossEntropy\":" << imitation.mean_loss
         << ",\"trainingAgreement\":" << imitation.agreement
         << ",\"gatePassed\":" << (warm_gate ? "true" : "false")
         << "},\n  \"reward\":{\"survival\":0.1,"
            "\"numberedClear\":0.02,\"coverReveal\":0.015,"
            "\"scoreDeltaDivisor\":700000,\"terminalPenalty\":-0.5},\n"
         << "  \"probe\":{\"random\":";
  writeSummary(output, random_summary);
  output << ",\"fairD1\":";
  writeSummary(output, teacher_summary);
  output << ",\"untrained\":";
  writeSummary(output, initial_summary);
  output << ",\"warmStart\":";
  writeSummary(output, warm_summary);
  output << ",\"best\":";
  writeSummary(output, best_probe);
  output << ",\"bestIteration\":" << best_iteration << "},\n"
         << "  \"ppo\":{\"ran\":" << (warm_gate ? "true" : "false")
         << ",\"iterations\":[";
  for (std::size_t index = 0; index < iterations.size(); ++index) {
    if (index > 0) output << ',';
    const IterationRecord& record = iterations[index];
    output << "{\"iteration\":" << record.iteration
           << ",\"samples\":" << record.samples
           << ",\"trainingMeanScore\":" << record.training_score
           << ",\"trainingMeanMoves\":" << record.training_moves
           << ",\"censored\":" << record.censored
           << ",\"policyLoss\":" << record.update.policy_loss
           << ",\"valueLoss\":" << record.update.value_loss
           << ",\"entropy\":" << record.update.entropy
           << ",\"approximateKl\":" << record.update.approximate_kl
           << ",\"clipFraction\":" << record.update.clip_fraction
           << ",\"updates\":" << record.update.updates
           << ",\"probe\":";
    writeSummary(output, record.probe);
    output << '}';
  }
  output << "]},\n  \"gates\":{\"warmRandomRatio\":" << kWarmRandomRatio
         << ",\"warmTeacherRatio\":" << kWarmTeacherRatio
         << ",\"warmClearRatio\":" << kWarmClearRatio
         << ",\"finalTeacherScoreRatio\":" << kFinalTeacherScoreRatio
         << ",\"finalTeacherMovesRatio\":" << kFinalTeacherMovesRatio
         << ",\"warmPassed\":" << (warm_gate ? "true" : "false")
         << ",\"finalPassed\":" << (final_gate ? "true" : "false")
         << "},\n  \"heldoutRan\":"
         << (final_gate ? "true" : "false") << ",\n  \"heldout\":";
  if (!final_gate) {
    output << "null";
  } else {
    output << "{\"random\":";
    writeSummary(output, heldout_random_summary);
    output << ",\"fairD1\":";
    writeSummary(output, heldout_teacher_summary);
    output << ",\"candidate\":";
    writeSummary(output, heldout_candidate_summary);
    output << ",\"passed\":" << (heldout_passed ? "true" : "false")
           << ",\"randomGames\":";
    writeGames(output, heldout_random);
    output << ",\"teacherGames\":";
    writeGames(output, heldout_teacher);
    output << ",\"candidateGames\":";
    writeGames(output, heldout_candidate);
    output << '}';
  }
  output << ",\n  \"qualified\":"
         << (final_gate && heldout_passed ? "true" : "false")
         << ",\n  \"checkpoint\":{\"path\":\"" << options.checkpoint
         << "\",\"iteration\":" << best_iteration
         << ",\"bytes\":"
         << sizeof(CheckpointHeader) +
                actor::Layout::count * sizeof(std::uint32_t)
         << ",\"fingerprint\":" << parameterFingerprint(best_network)
         << "},\n  \"wallSeconds\":" << wall_seconds
         << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
  output.close();

  std::cout << std::setprecision(10)
            << "random=" << random_summary.mean_score << '/'
            << random_summary.mean_moves << " fairD1="
            << teacher_summary.mean_score << '/' << teacher_summary.mean_moves
            << " warm=" << warm_summary.mean_score << '/'
            << warm_summary.mean_moves << " best=" << best_probe.mean_score
            << '/' << best_probe.mean_moves << " iteration=" << best_iteration
            << '\n'
            << "warm gate=" << (warm_gate ? "pass" : "fail")
            << " final gate=" << (final_gate ? "pass" : "fail")
            << " heldout="
            << (final_gate ? (heldout_passed ? "pass" : "fail")
                           : "not-run")
            << " artifact=" << options.output << '\n';
  return final_gate && heldout_passed ? 0 : 2;
}

}  // namespace drop7::ppo_v2

#ifndef DROP7_PPO_V2_LIBRARY
int main(int argc, char** argv) {
  try {
    const auto options = drop7::ppo_v2::parseOptions(argc, argv);
    return drop7::ppo_v2::run(options);
  } catch (const std::exception& error) {
    std::cerr << "drop7_ppo_v2: " << error.what() << '\n';
    return 1;
  }
}
#endif
