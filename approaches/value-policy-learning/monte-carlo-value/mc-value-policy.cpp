#include "../../../src/core/native/public-behavior.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <vector>

namespace drop7::mc_value {

constexpr int kChanceStrata = 7;
constexpr int kEnsembleSize = 4;
constexpr int kHeads = 3;
constexpr int kBuckets = 32'768;
constexpr int kMaximumLifetime = 500;

struct ObservableState {
  Board board{};
  std::uint8_t next_disc = 1;
  std::uint8_t moves_remaining = kMovesPerLevel;
};

inline ObservableState observable(const State& state) {
  return {state.board, state.next_disc,
          static_cast<std::uint8_t>(state.moves_remaining)};
}

inline State materialize(const ObservableState& state) {
  State result;
  result.board = state.board;
  result.next_disc = state.next_disc;
  result.moves_remaining = state.moves_remaining;
  return result;
}

inline bool mirrorIsSmaller(const Board& board) {
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      const std::uint8_t forward = board[indexOf(row, column)];
      const std::uint8_t reflected =
          board[indexOf(row, kBoardSize - 1 - column)];
      if (reflected < forward) return true;
      if (reflected > forward) return false;
    }
  }
  return false;
}

inline ObservableState canonicalize(const ObservableState& source) {
  if (!mirrorIsSmaller(source.board)) return source;
  ObservableState result = source;
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      result.board[indexOf(row, column)] =
          source.board[indexOf(row, kBoardSize - 1 - column)];
    }
  }
  return result;
}

inline std::uint32_t observableHash(const ObservableState& source) {
  const ObservableState state = canonicalize(source);
  std::uint32_t hash = 0x811c'9dc5u;
  for (std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint32_t>(cell) + 1u;
    hash *= 0x0100'0193u;
  }
  hash ^= state.next_disc;
  hash *= 0x0100'0193u;
  hash ^= state.moves_remaining;
  return mix32(hash);
}

inline int tupleCode(std::uint8_t first, std::uint8_t second,
                     std::uint8_t third, std::uint8_t fourth) {
  return ((first * 10 + second) * 10 + third) * 10 + fourth;
}

inline std::vector<std::uint16_t> features(const ObservableState& source,
                                           std::uint32_t model_seed) {
  const ObservableState state = canonicalize(source);
  const auto emit = [&](std::uint32_t code) {
    return static_cast<std::uint16_t>(
        mix32(code ^ model_seed) % static_cast<std::uint32_t>(kBuckets));
  };
  std::vector<std::uint16_t> result;
  result.reserve(160);
  result.push_back(emit(0x4249'4153u));
  result.push_back(emit(
      0x5048'4153u ^
      (static_cast<std::uint32_t>(state.moves_remaining) << 8) ^
      (static_cast<std::uint32_t>(state.next_disc) << 16)));
  int occupied = 0;
  int covers = 0;
  int maximum_height = 0;
  for (int index = 0; index < kCellCount; ++index) {
    const std::uint8_t cell = state.board[index];
    occupied += cell != kEmpty;
    covers += cell == kSolid || cell == kCracked;
    result.push_back(emit(
        0x4345'4c4cu ^ (static_cast<std::uint32_t>(index) * 0x9e37'79b9u) ^
        (static_cast<std::uint32_t>(cell) << 20)));
  }
  for (int row = 0; row < kBoardSize; ++row) {
    for (int start = 0; start <= kBoardSize - 4; ++start) {
      const int pattern = tupleCode(
          state.board[indexOf(row, start)],
          state.board[indexOf(row, start + 1)],
          state.board[indexOf(row, start + 2)],
          state.board[indexOf(row, start + 3)]);
      result.push_back(emit(
          0x524f'5734u ^ (static_cast<std::uint32_t>(row * 4 + start) << 16) ^
          static_cast<std::uint32_t>(pattern)));
    }
  }
  for (int column = 0; column < kBoardSize; ++column) {
    int height = 0;
    for (int row = 0; row < kBoardSize; ++row) {
      height += state.board[indexOf(row, column)] != kEmpty;
    }
    maximum_height = std::max(maximum_height, height);
    result.push_back(emit(
        0x4845'4947u ^ (static_cast<std::uint32_t>(column) << 8) ^
        (static_cast<std::uint32_t>(height) << 16)));
    for (int start = 0; start <= kBoardSize - 4; ++start) {
      const int pattern = tupleCode(
          state.board[indexOf(start, column)],
          state.board[indexOf(start + 1, column)],
          state.board[indexOf(start + 2, column)],
          state.board[indexOf(start + 3, column)]);
      result.push_back(emit(
          0x434f'4c34u ^
          (static_cast<std::uint32_t>(column * 4 + start) << 16) ^
          static_cast<std::uint32_t>(pattern)));
    }
  }
  for (int row = 0; row < kBoardSize - 1; ++row) {
    for (int column = 0; column < kBoardSize - 1; ++column) {
      const int pattern = tupleCode(
          state.board[indexOf(row, column)],
          state.board[indexOf(row, column + 1)],
          state.board[indexOf(row + 1, column)],
          state.board[indexOf(row + 1, column + 1)]);
      result.push_back(emit(
          0x5351'5232u ^
          (static_cast<std::uint32_t>(row * 6 + column) << 16) ^
          static_cast<std::uint32_t>(pattern)));
    }
  }
  result.push_back(emit(
      0x474c'4f42u ^ (static_cast<std::uint32_t>(occupied) << 8) ^
      (static_cast<std::uint32_t>(covers) << 16) ^
      (static_cast<std::uint32_t>(maximum_height) << 24)));
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

inline float sigmoid(float value) {
  if (value >= 0) return 1.0f / (1.0f + std::exp(-value));
  const float exponential = std::exp(value);
  return exponential / (1.0f + exponential);
}

struct Prediction {
  double lifetime = 100;
  double survival_25 = 0;
  double survival_50 = 0;
  double support = 0;
};

struct Label {
  ObservableState state{};
  float lifetime = 0;
  float survival_25 = 0;
  float survival_50 = 0;
  std::uint32_t identifier = 0;
};

class ValueModel {
 public:
  explicit ValueModel(std::uint32_t seed = 1)
      : seed_(seed),
        weights_(static_cast<std::size_t>(kBuckets) * kHeads),
        squared_gradients_(weights_.size(), 1e-3f),
        support_(kBuckets) {}

  Prediction predict(const ObservableState& state) const {
    const auto active = features(state, seed_);
    const float scale = 1.0f / std::sqrt(static_cast<float>(active.size()));
    std::array<float, kHeads> output{{100.0f / kMaximumLifetime, 0, 0}};
    double support = 0;
    for (std::uint16_t feature : active) {
      const std::size_t base = static_cast<std::size_t>(feature) * kHeads;
      for (int head = 0; head < kHeads; ++head) {
        output[head] += scale * weights_[base + head];
      }
      support += support_[feature];
    }
    return {
        std::clamp(static_cast<double>(output[0] * kMaximumLifetime),
                   0.0, static_cast<double>(kMaximumLifetime)),
        sigmoid(output[1]),
        sigmoid(output[2]),
        support / active.size(),
    };
  }

  void train(const Label& label, float learning_rate) {
    const auto active = features(label.state, seed_);
    if ((mix32(label.identifier ^ seed_) & 7u) == 0u) return;
    const Prediction prediction = predict(label.state);
    std::array<float, kHeads> gradient{};
    const float residual = static_cast<float>(
        (prediction.lifetime - label.lifetime) / kMaximumLifetime);
    gradient[0] = std::clamp(residual, -0.1f, 0.1f);
    gradient[1] = static_cast<float>(prediction.survival_25 -
                                     label.survival_25);
    gradient[2] = static_cast<float>(prediction.survival_50 -
                                     label.survival_50);
    const float scale = 1.0f / std::sqrt(static_cast<float>(active.size()));
    for (std::uint16_t feature : active) {
      const std::size_t base = static_cast<std::size_t>(feature) * kHeads;
      for (int head = 0; head < kHeads; ++head) {
        const float local = gradient[head] * scale;
        float& accumulator = squared_gradients_[base + head];
        accumulator += local * local;
        weights_[base + head] -=
            learning_rate * local / std::sqrt(accumulator);
      }
    }
  }

  void observe(const ObservableState& state) {
    for (std::uint16_t feature : features(state, seed_)) {
      if (support_[feature] != std::numeric_limits<std::uint16_t>::max()) {
        ++support_[feature];
      }
    }
  }

 private:
  std::uint32_t seed_ = 1;
  std::vector<float> weights_;
  std::vector<float> squared_gradients_;
  std::vector<std::uint16_t> support_;
};

using Ensemble = std::array<ValueModel, kEnsembleSize>;

inline Ensemble createEnsemble(std::uint32_t seed) {
  return {{ValueModel(mix32(seed ^ 0x1111'1111u)),
           ValueModel(mix32(seed ^ 0x2222'2222u)),
           ValueModel(mix32(seed ^ 0x3333'3333u)),
           ValueModel(mix32(seed ^ 0x4444'4444u))}};
}

inline std::uint32_t seedWithFirstDisc(std::uint32_t base,
                                       std::uint8_t target) {
  std::uint32_t candidate = base;
  for (std::uint32_t attempt = 0; attempt < 1'000; ++attempt) {
    Mulberry32 probe(candidate);
    if (probe.nextDisc() == target) return candidate;
    candidate = mix32(candidate + 0x9e37'79b9u + attempt);
  }
  throw std::runtime_error("could not construct reveal stratum");
}

struct Successor {
  ObservableState state{};
  bool terminal = false;
  std::uint8_t reveal_stratum = 1;
  std::uint8_t disc_stratum = 1;
};

Successor successor(const ObservableState& source, int action, int stratum) {
  if (stratum < 0 || stratum >= kChanceStrata ||
      !isLegal(source.board, action)) {
    throw std::invalid_argument("invalid successor request");
  }
  const std::uint32_t hash = observableHash(source);
  const int reveal_offset = static_cast<int>(mix32(hash ^ 0x5245'564cu) % 7u);
  const int disc_offset = static_cast<int>(mix32(hash ^ 0x4449'5343u) % 7u);
  const auto reveal = static_cast<std::uint8_t>(
      ((reveal_offset + stratum) % kChanceStrata) + 1);
  const auto next_disc = static_cast<std::uint8_t>(
      ((disc_offset + 3 * stratum) % kChanceStrata) + 1);
  const std::uint32_t base = mix32(
      hash ^ (static_cast<std::uint32_t>(stratum + 1) * 0xc2b2'ae35u) ^
      1'296'258'640u);  // Fixed counterfactual-domain tag, not a game seed.
  Mulberry32 random(seedWithFirstDisc(base, reveal));
  MoveResult move;
  if (!playMove(materialize(source), action, random, move)) {
    throw std::runtime_error("legal successor action failed");
  }
  if (!move.state.game_over) move.state.next_disc = next_disc;
  return {observable(move.state), move.state.game_over, reveal, next_disc};
}

struct ActionEstimate {
  int action = -1;
  std::array<double, kEnsembleSize> member_values{};
  std::array<double, kEnsembleSize * kChanceStrata> paired_values{};
  double mean_support = 0;
  int live_successors = 0;
};

ActionEstimate evaluateAction(const ObservableState& state, int action,
                              const Ensemble& ensemble) {
  ActionEstimate result;
  result.action = action;
  for (int stratum = 0; stratum < kChanceStrata; ++stratum) {
    const Successor next = successor(state, action, stratum);
    for (int member = 0; member < kEnsembleSize; ++member) {
      if (next.terminal) {
        result.member_values[member] += 1.0 / kChanceStrata;
        result.paired_values[member * kChanceStrata + stratum] = 1.0;
      } else {
        const Prediction prediction = ensemble[member].predict(next.state);
        const double value = 1.0 + prediction.lifetime;
        result.member_values[member] += value / kChanceStrata;
        result.paired_values[member * kChanceStrata + stratum] = value;
        result.mean_support += prediction.support;
        ++result.live_successors;
      }
    }
  }
  if (result.live_successors > 0) {
    result.mean_support /= result.live_successors;
  }
  return result;
}

inline double lowerConfidenceMargin(const ActionEstimate& candidate,
                                    const ActionEstimate& behavior,
                                    double z = 1.96) {
  std::array<double, kEnsembleSize * kChanceStrata> differences{};
  for (int index = 0; index < static_cast<int>(differences.size()); ++index) {
    differences[index] = candidate.paired_values[index] -
                         behavior.paired_values[index];
  }
  const double mean =
      std::accumulate(differences.begin(), differences.end(), 0.0) /
      differences.size();
  double squares = 0;
  for (double difference : differences) {
    squares += (difference - mean) * (difference - mean);
  }
  const double deviation = std::sqrt(squares / (differences.size() - 1));
  return mean - z * deviation / std::sqrt(differences.size());
}

struct PilotOptions {
  int training_games = 8;
  int stage_games = 8;
  int epochs = 25;
  int max_moves = 500;
  float learning_rate = 0.04f;
  double confidence_z = 1.96;
  double minimum_margin = 3.0;
  double minimum_support = 2.0;
  double support_ratio = 0.8;
  double maximum_disagreement = 35.0;
};

struct PolicyMetrics {
  std::uint64_t modeled_transitions = 0;
  std::uint64_t behavior_search_work = 0;
  int switches = 0;
  int ood_rejections = 0;
  int confidence_rejections = 0;
};

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  int clears = 0;
  int reveals = 0;
  bool censored = false;
  PolicyMetrics policy{};
};

struct Summary {
  double mean_score = 0;
  double mean_moves = 0;
  double clear_rate = 0;
  double reveal_rate = 0;
  double mean_switches = 0;
  std::uint64_t modeled_transitions = 0;
  std::uint64_t behavior_search_work = 0;
  int ood_rejections = 0;
  int confidence_rejections = 0;
  std::vector<GameResult> games;
};

inline std::pair<int, int> throughput(const MoveResult& move) {
  int clears = 0;
  int reveals = 0;
  for (const Wave& wave : move.waves) {
    clears += wave.cleared;
    reveals += wave.revealed;
  }
  return {clears, reveals};
}

inline double disagreement(const ActionEstimate& estimate) {
  const double mean = std::accumulate(estimate.member_values.begin(),
                                      estimate.member_values.end(), 0.0) /
                      estimate.member_values.size();
  double squares = 0;
  for (double value : estimate.member_values) {
    squares += (value - mean) * (value - mean);
  }
  return std::sqrt(squares / (estimate.member_values.size() - 1));
}

int chooseImprovedAction(const State& state, int behavior_action,
                         const Ensemble& ensemble,
                         const PilotOptions& options,
                         PolicyMetrics& metrics) {
  const ObservableState input = observable(state);
  const ActionEstimate behavior =
      evaluateAction(input, behavior_action, ensemble);
  metrics.modeled_transitions += kChanceStrata;
  int selected = behavior_action;
  double best_lower = 0;
  for (int action = 0; action < kBoardSize; ++action) {
    if (action == behavior_action || !isLegal(state.board, action)) continue;
    const ActionEstimate candidate = evaluateAction(input, action, ensemble);
    metrics.modeled_transitions += kChanceStrata;
    const bool supported =
        candidate.mean_support >= options.minimum_support &&
        candidate.mean_support >= options.support_ratio * behavior.mean_support &&
        disagreement(candidate) <= options.maximum_disagreement;
    if (!supported) {
      ++metrics.ood_rejections;
      continue;
    }
    const double lower =
        lowerConfidenceMargin(candidate, behavior, options.confidence_z) -
        options.minimum_margin;
    if (lower <= 0) {
      ++metrics.confidence_rejections;
      continue;
    }
    if (lower > best_lower) {
      best_lower = lower;
      selected = action;
    }
  }
  if (selected != behavior_action) ++metrics.switches;
  return selected;
}

GameResult runPolicyGame(std::uint32_t seed, const Ensemble* ensemble,
                         const PilotOptions& options) {
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < options.max_moves) {
    cfpi::BehaviorMetrics behavior_metrics;
    const int behavior_action =
        cfpi::chooseBehaviorAction(state, {}, &behavior_metrics);
    result.policy.behavior_search_work += behavior_metrics.work;
    int action = behavior_action;
    if (ensemble != nullptr) {
      action = chooseImprovedAction(state, behavior_action, *ensemble, options,
                                    result.policy);
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("MC value policy selected illegal action");
    }
    const auto [clears, reveals] = throughput(move);
    result.clears += clears;
    result.reveals += reveals;
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  return result;
}

GameResult collectBehaviorGame(std::uint32_t seed, const PilotOptions& options,
                               std::vector<Label>& labels,
                               std::uint32_t& identifier) {
  State state = initialHeadlessState(seed);
  struct Pending {
    ObservableState state{};
    int move_index = 0;
  };
  std::vector<Pending> trajectory;
  trajectory.reserve(160);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < options.max_moves) {
    trajectory.push_back({observable(state), state.moves_played});
    cfpi::BehaviorMetrics behavior_metrics;
    const int action = cfpi::chooseBehaviorAction(state, {}, &behavior_metrics);
    result.policy.behavior_search_work += behavior_metrics.work;
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("behavior collection selected illegal action");
    }
    const auto [clears, reveals] = throughput(move);
    result.clears += clears;
    result.reveals += reveals;
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  if (!result.censored) {
    for (const Pending& pending : trajectory) {
      const int remaining = state.moves_played - pending.move_index;
      labels.push_back({
          pending.state,
          static_cast<float>(remaining),
          remaining >= 25 ? 1.0f : 0.0f,
          remaining >= 50 ? 1.0f : 0.0f,
          identifier++,
      });
    }
  }
  return result;
}

Summary summarize(std::vector<GameResult> games) {
  Summary result;
  result.games = std::move(games);
  std::uint64_t moves = 0;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
  for (const GameResult& game : result.games) {
    result.mean_score += game.score;
    result.mean_moves += game.moves;
    result.mean_switches += game.policy.switches;
    result.modeled_transitions += game.policy.modeled_transitions;
    result.behavior_search_work += game.policy.behavior_search_work;
    result.ood_rejections += game.policy.ood_rejections;
    result.confidence_rejections += game.policy.confidence_rejections;
    moves += static_cast<std::uint64_t>(game.moves);
    clears += static_cast<std::uint64_t>(game.clears);
    reveals += static_cast<std::uint64_t>(game.reveals);
  }
  const double count = static_cast<double>(result.games.size());
  result.mean_score /= count;
  result.mean_moves /= count;
  result.mean_switches /= count;
  result.clear_rate = static_cast<double>(clears) / std::max<std::uint64_t>(1, moves);
  result.reveal_rate =
      static_cast<double>(reveals) / std::max<std::uint64_t>(1, moves);
  return result;
}

inline std::uint64_t peakResidentBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
#endif
}

double pairedLower95(const Summary& behavior, const Summary& improved,
                     bool moves) {
  if (behavior.games.size() != improved.games.size()) {
    throw std::invalid_argument("paired summaries differ in size");
  }
  std::vector<double> differences;
  differences.reserve(behavior.games.size());
  for (std::size_t index = 0; index < behavior.games.size(); ++index) {
    differences.push_back(
        moves ? improved.games[index].moves - behavior.games[index].moves
              : improved.games[index].score - behavior.games[index].score);
  }
  const double mean =
      std::accumulate(differences.begin(), differences.end(), 0.0) /
      differences.size();
  if (differences.size() < 2) {
    return -std::numeric_limits<double>::infinity();
  }
  double squares = 0;
  for (double difference : differences) {
    squares += (difference - mean) * (difference - mean);
  }
  const double deviation =
      std::sqrt(squares / (differences.size() - 1));
  return mean - 1.96 * deviation / std::sqrt(differences.size());
}

void printSummary(std::string_view tag, const Summary& summary) {
  std::cout << std::fixed << std::setprecision(3) << tag
            << " {\"games\":" << summary.games.size()
            << ",\"meanScore\":" << summary.mean_score
            << ",\"meanMoves\":" << summary.mean_moves
            << ",\"clearRate\":" << summary.clear_rate
            << ",\"revealRate\":" << summary.reveal_rate
            << ",\"meanSwitches\":" << summary.mean_switches
            << ",\"modeledTransitions\":" << summary.modeled_transitions
            << ",\"oodRejections\":" << summary.ood_rejections
            << ",\"confidenceRejections\":"
            << summary.confidence_rejections
            << ",\"behaviorSearchWork\":"
            << summary.behavior_search_work << "}\n";
}

int runPilot(const PilotOptions& options) {
  if (options.training_games < 1 || options.stage_games < 8 ||
      options.epochs < 1 || options.max_moves < 1 ||
      options.learning_rate <= 0 || options.confidence_z < 0 ||
      options.minimum_margin < 0 || options.minimum_support < 0 ||
      options.support_ratio < 0 || options.support_ratio > 1 ||
      options.maximum_disagreement <= 0) {
    throw std::invalid_argument("invalid MC value pilot options");
  }
  const auto started = std::chrono::steady_clock::now();
  std::vector<Label> labels;
  std::vector<GameResult> training_games;
  std::uint32_t identifier = 0;
  for (int game = 0; game < options.training_games; ++game) {
    training_games.push_back(collectBehaviorGame(
        0x3d7e'0000u + static_cast<std::uint32_t>(game), options, labels,
        identifier));
  }
  Ensemble ensemble = createEnsemble(0x6c43'5601u);
  for (ValueModel& member : ensemble) {
    for (const Label& label : labels) member.observe(label.state);
  }
  for (int epoch = 0; epoch < options.epochs; ++epoch) {
    for (std::size_t offset = 0; offset < labels.size(); ++offset) {
      const std::size_t index =
          (offset + static_cast<std::size_t>(epoch) * 7'919u) % labels.size();
      for (ValueModel& member : ensemble) {
        member.train(labels[index], options.learning_rate);
      }
    }
  }
  const Summary training = summarize(std::move(training_games));
  printSummary("MC_VALUE_TRAINING_BEHAVIOR", training);

  std::vector<GameResult> behavior_games;
  std::vector<GameResult> improved_games;
  behavior_games.reserve(options.stage_games);
  improved_games.reserve(options.stage_games);
  for (int game = 0; game < options.stage_games; ++game) {
    const std::uint32_t seed =
        0x3e73'0000u + static_cast<std::uint32_t>(game);
    behavior_games.push_back(runPolicyGame(seed, nullptr, options));
    improved_games.push_back(runPolicyGame(seed, &ensemble, options));
  }
  const Summary behavior = summarize(std::move(behavior_games));
  const Summary improved = summarize(std::move(improved_games));
  const double score_lower = pairedLower95(behavior, improved, false);
  const double moves_lower = pairedLower95(behavior, improved, true);
  const bool qualified = improved.mean_score > behavior.mean_score &&
                         improved.mean_moves > behavior.mean_moves &&
                         score_lower > 0 && moves_lower > 0;
  printSummary("MC_VALUE_STAGE_BEHAVIOR", behavior);
  printSummary("MC_VALUE_STAGE_IMPROVED", improved);
  const double seconds = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - started)
                             .count();
  std::cout << "MC_VALUE_RESULT {\"labels\":" << labels.size()
            << ",\"scoreLower95\":" << score_lower
            << ",\"movesLower95\":" << moves_lower
            << ",\"qualified\":" << (qualified ? "true" : "false")
            << ",\"seconds\":" << seconds
            << ",\"peakResidentBytes\":" << peakResidentBytes()
            << ",\"seedRanges\":[\"0x3d\",\"0x3e\"]}\n";
  return qualified ? 0 : 3;
}

bool selfTest(std::ostream& output) {
  const bool behavior = cfpi::selfTest(output);
  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;
  State irrelevant = state;
  irrelevant.score = 123'456;
  irrelevant.level = 42;
  irrelevant.moves_played = 271;
  const bool observable_only =
      observableHash(observable(state)) == observableHash(observable(irrelevant));

  ObservableState reflected = observable(state);
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      reflected.board[indexOf(row, kBoardSize - 1 - column)] =
          state.board[indexOf(row, column)];
    }
  }
  ValueModel model(0x51f7'0001u);
  const Prediction forward = model.predict(observable(state));
  const Prediction mirror = model.predict(reflected);
  const bool reflection_safe =
      std::abs(forward.lifetime - mirror.lifetime) < 1e-9;

  Label label{observable(state), 180, 1, 1, 0};
  model.observe(label.state);
  const double before = model.predict(label.state).lifetime;
  for (std::uint32_t step = 0; step < 64; ++step) {
    label.identifier = step;
    model.train(label, 0.05f);
  }
  const Prediction learned = model.predict(label.state);
  const bool learner = learned.lifetime > before && learned.support > 0;

  Ensemble ensemble = createEnsemble(0x6c43'0001u);
  int legal_count = 0;
  const auto legal = legalColumns(state.board, legal_count);
  std::array<bool, 8> reveals{};
  std::array<bool, 8> discs{};
  bool all_actions = legal_count > 0;
  for (int offset = 0; offset < legal_count; ++offset) {
    const ActionEstimate estimate =
        evaluateAction(observable(state), legal[offset], ensemble);
    all_actions = all_actions && std::isfinite(estimate.member_values[0]);
    for (int stratum = 0; stratum < kChanceStrata; ++stratum) {
      const Successor next = successor(observable(state), legal[offset], stratum);
      reveals[next.reveal_stratum] = true;
      discs[next.disc_stratum] = true;
    }
  }
  const bool exact_strata =
      std::all_of(reveals.begin() + 1, reveals.end(), [](bool value) {
        return value;
      }) &&
      std::all_of(discs.begin() + 1, discs.end(), [](bool value) {
        return value;
      });
  const ActionEstimate equal = evaluateAction(observable(state), legal[0], ensemble);
  const bool confidence = std::abs(lowerConfidenceMargin(equal, equal)) < 1e-12;
  const bool passed = behavior && observable_only && reflection_safe && learner &&
                      all_actions && exact_strata && confidence;
  output << "MC_VALUE_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"observableOnly\":" << (observable_only ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe ? "true" : "false")
         << ",\"learnerWired\":" << (learner ? "true" : "false")
         << ",\"legalActions\":" << legal_count
         << ",\"counterfactuals\":" << legal_count * kChanceStrata
         << ",\"exactStrata\":" << (exact_strata ? "true" : "false")
         << ",\"confidencePaired\":" << (confidence ? "true" : "false")
         << "}\n";
  return passed;
}

}  // namespace drop7::mc_value

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::mc_value::selfTest(std::cout) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    const auto value_after = [&](std::string_view flag,
                                 std::string fallback) {
      for (int index = 1; index + 1 < argc; ++index) {
        if (std::string_view(argv[index]) == flag) {
          return std::string(argv[index + 1]);
        }
      }
      return fallback;
    };
    const auto has_flag = [&](std::string_view flag) {
      for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == flag) return true;
      }
      return false;
    };
    if (has_flag("--pilot")) {
      drop7::mc_value::PilotOptions options;
      options.training_games = std::stoi(value_after(
          "--training-games", std::to_string(options.training_games)));
      options.stage_games = std::stoi(
          value_after("--stage-games", std::to_string(options.stage_games)));
      options.epochs =
          std::stoi(value_after("--epochs", std::to_string(options.epochs)));
      options.max_moves = std::stoi(
          value_after("--max-moves", std::to_string(options.max_moves)));
      options.learning_rate = std::stof(value_after(
          "--learning-rate", std::to_string(options.learning_rate)));
      options.confidence_z = std::stod(value_after(
          "--confidence-z", std::to_string(options.confidence_z)));
      options.minimum_margin = std::stod(value_after(
          "--minimum-margin", std::to_string(options.minimum_margin)));
      options.minimum_support = std::stod(value_after(
          "--minimum-support", std::to_string(options.minimum_support)));
      options.support_ratio = std::stod(value_after(
          "--support-ratio", std::to_string(options.support_ratio)));
      options.maximum_disagreement = std::stod(value_after(
          "--maximum-disagreement",
          std::to_string(options.maximum_disagreement)));
      return drop7::mc_value::runPilot(options);
    }
    std::cerr << "Usage: drop7_mc_value_policy --self-test | --pilot [options]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
