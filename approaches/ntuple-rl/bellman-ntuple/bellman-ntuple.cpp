#include "../../../src/core/native/engine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <vector>

// Off-policy fitted Bellman value iteration over observable Drop7 decision
// states. Behavior actions are used only to collect replay states. Every
// update independently maximizes over all legal actions under common,
// state-hashed chance samples, so epsilon behavior cannot leak into targets.
namespace drop7::bellman_ntuple {

constexpr int kSharedTables = 3;
constexpr int kPatternsPerTable = 10'000;
constexpr int kAbsolute4Tables = 92;
constexpr int kPhaseFeatures = kMovesPerLevel;
constexpr int kDiscFeatures = kBoardSize;
constexpr int kMaximumHeightFeatures = kBoardSize + 1;
constexpr int kTokenCountFeatures = 10 * (kCellCount + 1);
constexpr int kAuxFeatures = kPhaseFeatures + kDiscFeatures +
                             kMaximumHeightFeatures + kTokenCountFeatures;
constexpr int kSharedBase = kAuxFeatures;
constexpr int kAbsolute4Base =
    kSharedBase + kSharedTables * kPatternsPerTable;
constexpr int kNodeCount =
    kAbsolute4Base + kAbsolute4Tables * kPatternsPerTable;
constexpr int kSharedActiveFeatures = 28 + 28 + 36 + 1 + 1 + 1 + 10;
constexpr int kMaximumActiveFeatures =
    kSharedActiveFeatures + kAbsolute4Tables;
constexpr std::size_t kMemoryLimit = 64ull * 1024 * 1024;

constexpr int kPhaseBase = 0;
constexpr int kDiscBase = kPhaseBase + kPhaseFeatures;
constexpr int kMaximumHeightBase = kDiscBase + kDiscFeatures;
constexpr int kTokenCountBase = kMaximumHeightBase + kMaximumHeightFeatures;
constexpr int kHorizontalBase = kSharedBase;
constexpr int kVerticalBase = kHorizontalBase + kPatternsPerTable;
constexpr int kSquareBase = kVerticalBase + kPatternsPerTable;
static_assert(kSquareBase + kPatternsPerTable == kAbsolute4Base);

struct CompactState {
  Board board{};
  std::uint8_t next_disc = 1;
  std::uint8_t moves_remaining = kMovesPerLevel;
};

static_assert(sizeof(CompactState) <= 52);

struct FeatureSet {
  std::array<std::uint32_t, kMaximumActiveFeatures> ids{};
  int count = 0;
};

struct Rng {
  explicit Rng(std::uint32_t seed) : random(seed) {}

  std::uint32_t bits() { return random.nextBits(); }
  double unit() { return random.nextUnit(); }
  int bounded(int bound) {
    return static_cast<int>(
        (static_cast<std::uint64_t>(bits()) * bound) >> 32);
  }

  Mulberry32 random;
};

struct Options {
  int training_games = 10'000;
  int probe_games = 64;
  int max_moves = 1'000;
  int chance_samples = 7;
  int replay_capacity = 100'000;
  int replay_warmup = 1'000;
  int updates_per_step = 1;
  int target_sync_updates = 10'000;
  int report_every = 10'000;
  float gamma = 0.997f;
  float learning_rate = 0.05f;
  float optimistic_value = 200.0f;
  float target_cap = 2'000.0f;
  float epsilon_start = 1.0f;
  float epsilon_end = 0.1f;
  std::uint32_t training_seed_start = 0x3d70'0000u;
  std::uint32_t probe_seed_start = 0x4d70'0000u;
  std::uint32_t learner_seed = 0xb311'4d7u;
  bool disc_independent = false;
  bool positional_residual = false;
};

struct TrainingStats {
  std::uint64_t transitions = 0;
  std::uint64_t updates = 0;
  std::uint64_t target_syncs = 0;
  std::uint64_t random_actions = 0;
  std::uint64_t greedy_actions = 0;
  std::uint64_t target_clamps = 0;
  double absolute_td_sum = 0;
  double target_sum = 0;
  float minimum_target = std::numeric_limits<float>::infinity();
  float maximum_target = -std::numeric_limits<float>::infinity();
};

struct Evaluation {
  double mean_score = 0;
  double mean_moves = 0;
  std::int64_t minimum_score = std::numeric_limits<std::int64_t>::max();
  std::int64_t maximum_score = std::numeric_limits<std::int64_t>::min();
  int minimum_moves = std::numeric_limits<int>::max();
  int maximum_moves = std::numeric_limits<int>::min();
  int censored = 0;
  std::vector<std::int64_t> scores;
  std::vector<int> moves;
};

CompactState compact(const State& state) {
  return {state.board, state.next_disc,
          static_cast<std::uint8_t>(state.moves_remaining)};
}

State expand(const CompactState& compact_state) {
  State state;
  state.board = compact_state.board;
  state.next_disc = compact_state.next_disc;
  state.moves_remaining = compact_state.moves_remaining;
  return state;
}

bool mirrorIsSmaller(const Board& board) {
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

Board mirrorBoard(const Board& board) {
  Board mirrored{};
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      mirrored[indexOf(row, column)] =
          board[indexOf(row, kBoardSize - 1 - column)];
    }
  }
  return mirrored;
}

CompactState canonicalize(const CompactState& source) {
  CompactState result = source;
  if (mirrorIsSmaller(source.board)) result.board = mirrorBoard(source.board);
  return result;
}

struct CanonicalState {
  State state{};
  bool mirrored = false;
};

CanonicalState canonicalize(const State& source) {
  CanonicalState result{source, mirrorIsSmaller(source.board)};
  if (result.mirrored) result.state.board = mirrorBoard(source.board);
  return result;
}

int physicalAction(int canonical_action, bool mirrored) {
  return mirrored ? kBoardSize - 1 - canonical_action : canonical_action;
}

std::uint32_t observableHash(const State& canonical) {
  std::uint32_t hash = 0x811c'9dc5u;
  for (std::uint8_t cell : canonical.board) {
    hash ^= static_cast<std::uint32_t>(cell + 1);
    hash *= 0x0100'0193u;
  }
  hash ^= canonical.next_disc;
  hash *= 0x0100'0193u;
  hash ^= static_cast<std::uint32_t>(canonical.moves_remaining);
  return mix32(hash ^ 0x4245'4c4cu);
}

std::vector<std::uint32_t> stratifiedChanceSeeds(std::uint32_t state_hash,
                                                  int samples) {
  std::vector<std::uint32_t> result(samples);
  const int rotation =
      static_cast<int>(mix32(state_hash ^ 0x5354'5241u) % 7u);
  for (int sample = 0; sample < samples; ++sample) {
    const int batch = sample / 7;
    const std::uint8_t desired = static_cast<std::uint8_t>(
        ((rotation + sample) % 7) + 1);
    bool found = false;
    for (std::uint32_t attempt = 0; attempt < 256; ++attempt) {
      const std::uint32_t candidate = mix32(
          state_hash ^
          (static_cast<std::uint32_t>(batch + 1) * 0x27d4'eb2du) ^
          (static_cast<std::uint32_t>(sample + 1) * 0xc2b2'ae35u) ^
          (attempt * 0x9e37'79b9u) ^ 0x5245'564cu);
      Mulberry32 probe(candidate);
      if (probe.nextDisc() == desired) {
        result[sample] = candidate;
        found = true;
        break;
      }
    }
    if (!found) throw std::runtime_error("chance stratification failed");
  }
  return result;
}

int code4(std::uint8_t first, std::uint8_t second, std::uint8_t third,
          std::uint8_t fourth) {
  return ((first * 10 + second) * 10 + third) * 10 + fourth;
}

FeatureSet features(const CompactState& original, bool disc_independent,
                    bool positional_residual) {
  const CompactState state = canonicalize(original);
  const Board& board = state.board;
  FeatureSet result;
  std::array<int, 10> token_counts{};
  int maximum_height = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    int height = 0;
    for (int row = 0; row < kBoardSize; ++row) {
      const std::uint8_t token = board[indexOf(row, column)];
      ++token_counts[token];
      height += token != kEmpty;
    }
    maximum_height = std::max(maximum_height, height);
  }

  result.ids[result.count++] =
      kPhaseBase +
      std::clamp<int>(state.moves_remaining, 1, kMovesPerLevel) - 1;
  result.ids[result.count++] =
      kDiscBase +
      (disc_independent
           ? 0
           : std::clamp<int>(state.next_disc, 1, kBoardSize) - 1);
  result.ids[result.count++] = kMaximumHeightBase + maximum_height;
  for (int token = 0; token < 10; ++token) {
    result.ids[result.count++] =
        kTokenCountBase + token * (kCellCount + 1) + token_counts[token];
  }

  for (int row = 0; row < kBoardSize; ++row) {
    for (int start = 0; start <= kBoardSize - 4; ++start) {
      const int pattern = code4(
          board[indexOf(row, start)], board[indexOf(row, start + 1)],
          board[indexOf(row, start + 2)], board[indexOf(row, start + 3)]);
      result.ids[result.count++] = kHorizontalBase + pattern;
    }
  }
  for (int column = 0; column < kBoardSize; ++column) {
    for (int start = 0; start <= kBoardSize - 4; ++start) {
      const int pattern = code4(
          board[indexOf(start, column)], board[indexOf(start + 1, column)],
          board[indexOf(start + 2, column)],
          board[indexOf(start + 3, column)]);
      result.ids[result.count++] = kVerticalBase + pattern;
    }
  }
  for (int row = 0; row < kBoardSize - 1; ++row) {
    for (int column = 0; column < kBoardSize - 1; ++column) {
      const int pattern = code4(
          board[indexOf(row, column)], board[indexOf(row, column + 1)],
          board[indexOf(row + 1, column)],
          board[indexOf(row + 1, column + 1)]);
      result.ids[result.count++] = kSquareBase + pattern;
    }
  }
  if (positional_residual) {
    int table = 0;
    for (int row = 0; row < kBoardSize; ++row) {
      for (int start = 0; start <= kBoardSize - 4; ++start, ++table) {
        const int pattern = code4(
            board[indexOf(row, start)], board[indexOf(row, start + 1)],
            board[indexOf(row, start + 2)], board[indexOf(row, start + 3)]);
        result.ids[result.count++] =
            kAbsolute4Base + table * kPatternsPerTable + pattern;
      }
    }
    for (int column = 0; column < kBoardSize; ++column) {
      for (int start = 0; start <= kBoardSize - 4; ++start, ++table) {
        const int pattern = code4(
            board[indexOf(start, column)], board[indexOf(start + 1, column)],
            board[indexOf(start + 2, column)],
            board[indexOf(start + 3, column)]);
        result.ids[result.count++] =
            kAbsolute4Base + table * kPatternsPerTable + pattern;
      }
    }
    for (int row = 0; row < kBoardSize - 1; ++row) {
      for (int column = 0; column < kBoardSize - 1; ++column, ++table) {
        const int pattern = code4(
            board[indexOf(row, column)], board[indexOf(row, column + 1)],
            board[indexOf(row + 1, column)],
            board[indexOf(row + 1, column + 1)]);
        result.ids[result.count++] =
            kAbsolute4Base + table * kPatternsPerTable + pattern;
      }
    }
    if (table != kAbsolute4Tables) {
      throw std::logic_error("absolute four-tuple table invariant failed");
    }
  }
  const int expected = kSharedActiveFeatures +
                       (positional_residual ? kAbsolute4Tables : 0);
  if (result.count != expected) {
    throw std::logic_error("shared four-tuple feature invariant failed");
  }
  return result;
}

class Model {
 public:
  explicit Model(float optimistic_value = 200.0f,
                 bool disc_independent = false,
                 bool positional_residual = false)
      : disc_independent_(disc_independent),
        positional_residual_(positional_residual),
        weights_(kNodeCount,
                 optimistic_value /
                     (kSharedActiveFeatures +
                      (positional_residual ? kAbsolute4Tables : 0))) {}

  float value(const CompactState& state) const {
    const FeatureSet active =
        features(state, disc_independent_, positional_residual_);
    double total = 0;
    for (int index = 0; index < active.count; ++index) {
      total += weights_[active.ids[index]];
    }
    return static_cast<float>(total);
  }

  float value(const State& state) const { return value(compact(state)); }

  float update(const CompactState& state, float target, float learning_rate) {
    const FeatureSet active =
        features(state, disc_independent_, positional_residual_);
    double prediction = 0;
    for (int index = 0; index < active.count; ++index) {
      prediction += weights_[active.ids[index]];
    }
    const float error = target - static_cast<float>(prediction);
    const float clipped_error = std::clamp(error, -100.0f, 100.0f);
    const float step = learning_rate * clipped_error / active.count;
    for (int index = 0; index < active.count; ++index) {
      weights_[active.ids[index]] += step;
    }
    return error;
  }

  void copyWeightsFrom(const Model& source) {
    if (disc_independent_ != source.disc_independent_ ||
        positional_residual_ != source.positional_residual_) {
      throw std::logic_error("copied incompatible n-tuple models");
    }
    weights_ = source.weights_;
  }

  void perturbForTest(std::size_t index, float amount) {
    weights_.at(index) += amount;
  }

  bool sameWeights(const Model& other) const {
    return weights_ == other.weights_;
  }

  std::size_t bytes() const { return weights_.size() * sizeof(float); }

 private:
  bool disc_independent_ = false;
  bool positional_residual_ = false;
  std::vector<float> weights_;
};

class Replay {
 public:
  explicit Replay(std::size_t capacity) : states_(capacity) {
    if (capacity == 0) throw std::invalid_argument("replay capacity is zero");
  }

  void push(const CompactState& state) {
    states_[next_] = canonicalize(state);
    next_ = (next_ + 1) % states_.size();
    size_ = std::min(size_ + 1, states_.size());
  }

  const CompactState& sample(Rng& random) const {
    if (size_ == 0) throw std::logic_error("sampled empty replay");
    return states_[static_cast<std::size_t>(random.bounded(
        static_cast<int>(size_)))];
  }

  std::size_t size() const { return size_; }
  std::size_t capacity() const { return states_.size(); }
  std::size_t bytes() const { return states_.size() * sizeof(CompactState); }

 private:
  std::vector<CompactState> states_;
  std::size_t next_ = 0;
  std::size_t size_ = 0;
};

std::array<float, kBoardSize> actionValues(const Model& model,
                                            const State& source,
                                            const Options& options) {
  const CanonicalState canonical = canonicalize(source);
  const State& state = canonical.state;
  const auto chance_seeds = stratifiedChanceSeeds(
      observableHash(state), options.chance_samples);
  std::array<float, kBoardSize> physical_values{};
  physical_values.fill(-std::numeric_limits<float>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    if (!isLegal(state.board, action)) continue;
    double total = 0;
    for (std::uint32_t seed : chance_seeds) {
      Mulberry32 chance(seed);
      MoveResult move;
      if (!playMove(state, action, chance, move)) {
        throw std::logic_error("Bellman evaluator chose illegal action");
      }
      total += 1.0 +
               (move.state.game_over
                    ? 0.0
                    : static_cast<double>(options.gamma) *
                          model.value(move.state));
    }
    physical_values[physicalAction(action, canonical.mirrored)] =
        static_cast<float>(total / chance_seeds.size());
  }
  return physical_values;
}

int greedyAction(const Model& model, const State& state,
                 const Options& options) {
  const auto values = actionValues(model, state, options);
  const bool mirrored = canonicalize(state).mirrored;
  constexpr std::array<int, kBoardSize> order{{3, 2, 4, 1, 5, 0, 6}};
  int selected_canonical = -1;
  float best = -std::numeric_limits<float>::infinity();
  for (int canonical_column : order) {
    const int physical = physicalAction(canonical_column, mirrored);
    if (!isLegal(state.board, physical)) continue;
    if (selected_canonical < 0 || values[physical] > best) {
      selected_canonical = canonical_column;
      best = values[physical];
    }
  }
  return selected_canonical < 0
             ? -1
             : physicalAction(selected_canonical, mirrored);
}

float bellmanTarget(const Model& target_model, const CompactState& replay_state,
                    const Options& options, TrainingStats* stats = nullptr) {
  const State state = expand(replay_state);
  const auto values = actionValues(target_model, state, options);
  float target = -std::numeric_limits<float>::infinity();
  for (float value : values) {
    if (std::isfinite(value)) target = std::max(target, value);
  }
  if (!std::isfinite(target)) throw std::logic_error("Bellman state had no action");
  const float clamped = std::clamp(target, 0.0f, options.target_cap);
  if (stats != nullptr) {
    if (clamped != target) ++stats->target_clamps;
    stats->target_sum += clamped;
    stats->minimum_target = std::min(stats->minimum_target, clamped);
    stats->maximum_target = std::max(stats->maximum_target, clamped);
  }
  return clamped;
}

int randomLegalAction(const State& state, Rng& random) {
  int legal_count = 0;
  const auto legal = legalColumns(state.board, legal_count);
  return legal_count == 0 ? -1 : legal[random.bounded(legal_count)];
}

double epsilonForGame(const Options& options, int game) {
  if (options.training_games <= 1) return options.epsilon_end;
  const double fraction = static_cast<double>(game) /
                          static_cast<double>(options.training_games - 1);
  return options.epsilon_start +
         (options.epsilon_end - options.epsilon_start) * fraction;
}

Evaluation evaluate(const Model& model, const Options& options) {
  Evaluation result;
  result.scores.reserve(options.probe_games);
  result.moves.reserve(options.probe_games);
  for (int game = 0; game < options.probe_games; ++game) {
    const std::uint32_t seed =
        options.probe_seed_start + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < options.max_moves) {
      const int action = greedyAction(model, state, options);
      MoveResult move;
      if (action < 0 || !playHeadlessMove(state, seed, action, move)) {
        throw std::logic_error("Bellman probe chose illegal action");
      }
    }
    result.mean_score += state.score;
    result.mean_moves += state.moves_played;
    result.minimum_score = std::min(result.minimum_score, state.score);
    result.maximum_score = std::max(result.maximum_score, state.score);
    result.minimum_moves = std::min(result.minimum_moves, state.moves_played);
    result.maximum_moves = std::max(result.maximum_moves, state.moves_played);
    if (!state.game_over) ++result.censored;
    result.scores.push_back(state.score);
    result.moves.push_back(state.moves_played);
  }
  result.mean_score /= options.probe_games;
  result.mean_moves /= options.probe_games;
  return result;
}

long peakRssKiB() {
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
#if defined(__APPLE__)
  return usage.ru_maxrss / 1024;
#else
  return usage.ru_maxrss;
#endif
}

template <typename Value>
void printArray(const std::vector<Value>& values) {
  std::cout << '[';
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) std::cout << ',';
    std::cout << values[index];
  }
  std::cout << ']';
}

void printProbe(const Evaluation& result, int games,
                const TrainingStats& stats, const Replay& replay,
                double elapsed, const Model& online,
                const Model& target) {
  std::cout << std::fixed << std::setprecision(3)
            << "BELLMAN_NTUPLE_PROBE {\"trainingGames\":" << games
            << ",\"transitions\":" << stats.transitions
            << ",\"updates\":" << stats.updates
            << ",\"targetSyncs\":" << stats.target_syncs
            << ",\"replaySize\":" << replay.size()
            << ",\"meanScore\":" << result.mean_score
            << ",\"meanMoves\":" << result.mean_moves
            << ",\"minimumScore\":" << result.minimum_score
            << ",\"maximumScore\":" << result.maximum_score
            << ",\"minimumMoves\":" << result.minimum_moves
            << ",\"maximumMoves\":" << result.maximum_moves
            << ",\"censored\":" << result.censored
            << ",\"meanAbsTd\":"
            << (stats.updates ? stats.absolute_td_sum / stats.updates : 0)
            << ",\"meanTarget\":"
            << (stats.updates ? stats.target_sum / stats.updates : 0)
            << ",\"minimumTarget\":"
            << (stats.updates ? stats.minimum_target : 0)
            << ",\"maximumTarget\":"
            << (stats.updates ? stats.maximum_target : 0)
            << ",\"targetClamps\":" << stats.target_clamps
            << ",\"randomActions\":" << stats.random_actions
            << ",\"greedyActions\":" << stats.greedy_actions
            << ",\"transitionsPerSecond\":"
            << (elapsed > 0 ? stats.transitions / elapsed : 0)
            << ",\"updatesPerSecond\":"
            << (elapsed > 0 ? stats.updates / elapsed : 0)
            << ",\"modelMiB\":"
            << (online.bytes() + target.bytes()) / 1'048'576.0
            << ",\"replayMiB\":" << replay.bytes() / 1'048'576.0
            << ",\"peakRssMiB\":" << peakRssKiB() / 1024.0
            << ",\"continueGate\":"
            << (result.mean_score >= 300'000 ? "true" : "false")
            << ",\"scores\":";
  printArray(result.scores);
  std::cout << ",\"moves\":";
  printArray(result.moves);
  std::cout << "}\n";
}

void validateOptions(const Options& options) {
  if (options.training_games < 1 || options.training_games > 100'000 ||
      options.probe_games < 1 || options.probe_games > 256 ||
      options.max_moves < 1 || options.max_moves > 5'000 ||
      options.chance_samples < 1 || options.chance_samples > 28 ||
      options.replay_capacity < 1 || options.replay_capacity > 1'000'000 ||
      options.replay_warmup < 1 ||
      options.replay_warmup > options.replay_capacity ||
      options.updates_per_step < 1 || options.updates_per_step > 16 ||
      options.target_sync_updates < 1 || options.report_every < 1 ||
      !(options.gamma > 0 && options.gamma <= 1) ||
      options.learning_rate <= 0 || options.learning_rate > 1 ||
      options.optimistic_value < 0 || options.target_cap < 1 ||
      options.epsilon_start < 0 || options.epsilon_start > 1 ||
      options.epsilon_end < 0 || options.epsilon_end > 1) {
    throw std::invalid_argument("invalid Bellman n-tuple options");
  }
  const std::uint64_t probe_end =
      static_cast<std::uint64_t>(options.probe_seed_start) +
      options.probe_games - 1;
  if ((options.training_seed_start & 0xffff'0000u) != 0x3d70'0000u ||
      (options.probe_seed_start & 0xffff'0000u) != 0x4d70'0000u ||
      probe_end >= 0x4d71'0000ull) {
    throw std::invalid_argument("seed range outside training/probe partitions");
  }
  const std::size_t total_bytes =
      2 * static_cast<std::size_t>(kNodeCount) * sizeof(float) +
      static_cast<std::size_t>(options.replay_capacity) *
          sizeof(CompactState);
  if (total_bytes >= kMemoryLimit) {
    throw std::invalid_argument("configured memory exceeds 64 MiB bound");
  }
}

int train(const Options& options) {
  validateOptions(options);
  Model online(options.optimistic_value, options.disc_independent,
               options.positional_residual);
  Model target(options.optimistic_value, options.disc_independent,
               options.positional_residual);
  Replay replay(options.replay_capacity);
  Rng learner(options.learner_seed);
  TrainingStats stats;
  const auto started = std::chrono::steady_clock::now();

  std::cout << "BELLMAN_NTUPLE_CONFIG {\"trainingSeedStart\":"
            << options.training_seed_start << ",\"probeSeedStart\":"
            << options.probe_seed_start << ",\"trainingGames\":"
            << options.training_games << ",\"chanceSamples\":"
            << options.chance_samples << ",\"replayCapacity\":"
            << options.replay_capacity << ",\"replayWarmup\":"
            << options.replay_warmup << ",\"updatesPerStep\":"
            << options.updates_per_step << ",\"targetSyncUpdates\":"
            << options.target_sync_updates << ",\"gamma\":"
            << options.gamma << ",\"learningRate\":"
            << options.learning_rate << ",\"optimisticValue\":"
            << options.optimistic_value << ",\"targetCap\":"
            << options.target_cap << ",\"epsilonStart\":"
            << options.epsilon_start << ",\"epsilonEnd\":"
            << options.epsilon_end << ",\"nodes\":" << kNodeCount
            << ",\"activeFeatures\":"
            << (kSharedActiveFeatures +
                (options.positional_residual ? kAbsolute4Tables : 0))
            << ",\"shared4Tables\":3,\"absolute4Tables\":"
            << (options.positional_residual ? kAbsolute4Tables : 0)
            << ",\"positionalResidual\":"
            << (options.positional_residual ? "true" : "false")
            << ",\"offPolicyMaxTarget\":true,\"targetNetwork\":true,"
               "\"visibleDiscEncoded\":"
            << (options.disc_independent ? "false" : "true")
            << ",\"discIndependentChanceState\":"
            << (options.disc_independent ? "true" : "false") << "}\n";
  const Evaluation initial_probe = evaluate(online, options);
  printProbe(initial_probe, 0, stats, replay, 0, online, target);
  double best_probe_score = initial_probe.mean_score;
  int nonimproving_probe_checkpoints = 0;

  for (int game = 0; game < options.training_games; ++game) {
    // The training partition contains 65,536 fixed environment tapes. At the
    // 100k gate tapes repeat, but the changing behavior policy reaches new
    // states; no probe or sealed seed is substituted.
    const std::uint32_t seed = options.training_seed_start +
        static_cast<std::uint32_t>(game & 0xffff);
    State state = initialHeadlessState(seed);
    const double epsilon = epsilonForGame(options, game);
    while (!state.game_over && state.moves_played < options.max_moves) {
      replay.push(compact(state));
      int action = -1;
      if (learner.unit() < epsilon) {
        action = randomLegalAction(state, learner);
        ++stats.random_actions;
      } else {
        action = greedyAction(online, state, options);
        ++stats.greedy_actions;
      }
      MoveResult actual;
      if (action < 0 || !playHeadlessMove(state, seed, action, actual)) {
        throw std::logic_error("behavior policy chose illegal action");
      }
      ++stats.transitions;

      if (replay.size() >= static_cast<std::size_t>(options.replay_warmup)) {
        for (int update = 0; update < options.updates_per_step; ++update) {
          const CompactState sampled = replay.sample(learner);
          const float fitted_target =
              bellmanTarget(target, sampled, options, &stats);
          const float td =
              online.update(sampled, fitted_target, options.learning_rate);
          stats.absolute_td_sum += std::abs(td);
          ++stats.updates;
          if (stats.updates % options.target_sync_updates == 0) {
            target.copyWeightsFrom(online);
            ++stats.target_syncs;
          }
        }
      }
    }

    const int completed = game + 1;
    if (completed % options.report_every == 0 ||
        completed == options.training_games) {
      const double elapsed = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
      const Evaluation probe = evaluate(online, options);
      printProbe(probe, completed, stats, replay, elapsed, online, target);
      if (probe.mean_score > best_probe_score) {
        best_probe_score = probe.mean_score;
        nonimproving_probe_checkpoints = 0;
      } else {
        ++nonimproving_probe_checkpoints;
      }
      if (nonimproving_probe_checkpoints >= 2 &&
          completed < options.training_games) {
        std::cout << "BELLMAN_NTUPLE_EARLY_STOP {\"trainingGames\":"
                  << completed << ",\"bestProbeMeanScore\":"
                  << best_probe_score
                  << ",\"nonimprovingCheckpoints\":"
                  << nonimproving_probe_checkpoints << "}\n";
        return 0;
      }
    }
  }
  return 0;
}

bool exhaustiveIndexTest() {
  for (int phase = 0; phase < kPhaseFeatures; ++phase) {
    if (kPhaseBase + phase >= kDiscBase) return false;
  }
  for (int disc = 0; disc < kDiscFeatures; ++disc) {
    if (kDiscBase + disc >= kMaximumHeightBase) return false;
  }
  for (int pattern = 0; pattern < kPatternsPerTable; ++pattern) {
    if (kHorizontalBase + pattern >= kVerticalBase ||
        kVerticalBase + pattern >= kSquareBase ||
        kSquareBase + pattern >= kAbsolute4Base) {
      return false;
    }
  }
  for (int table = 0; table < kAbsolute4Tables; ++table) {
    for (int pattern = 0; pattern < kPatternsPerTable; ++pattern) {
      const std::uint32_t id =
          kAbsolute4Base + table * kPatternsPerTable + pattern;
      if (id < static_cast<std::uint32_t>(kAbsolute4Base) ||
          id >= static_cast<std::uint32_t>(kNodeCount)) {
        return false;
      }
    }
  }
  return true;
}

bool selfTest(std::ostream& output) {
  Options options;
  options.chance_samples = 7;
  options.replay_capacity = 3;
  options.replay_warmup = 1;
  Model online(200);
  Model target(200);
  State state = initialHeadlessState(0x3d70'0042u);
  for (int action : {3, 1, 5, 2}) {
    MoveResult move;
    if (!playHeadlessMove(state, 0x3d70'0042u, action, move)) break;
  }
  State mirrored = state;
  mirrored.board = mirrorBoard(state.board);
  const float value = online.value(state);
  const float mirror_value = online.value(mirrored);
  const auto action_values = actionValues(online, state, options);
  const auto mirror_values = actionValues(online, mirrored, options);
  bool mirror_actions = true;
  for (int column = 0; column < kBoardSize; ++column) {
    const float first = action_values[column];
    const float second = mirror_values[kBoardSize - 1 - column];
    if (std::isfinite(first) != std::isfinite(second) ||
        (std::isfinite(first) && std::abs(first - second) > 1e-5f)) {
      mirror_actions = false;
    }
  }
  const bool deterministic =
      action_values == actionValues(online, state, options);

  State other_disc = state;
  other_disc.next_disc = static_cast<std::uint8_t>(state.next_disc % 7 + 1);
  const FeatureSet first_features = features(compact(state), false, false);
  const FeatureSet other_features =
      features(compact(other_disc), false, false);
  const bool disc_encoded = first_features.ids != other_features.ids;
  Model disc_independent_model(200, true, true);
  const FeatureSet independent_first = features(compact(state), true, true);
  const FeatureSet independent_other =
      features(compact(other_disc), true, true);
  const bool disc_independent = independent_first.ids == independent_other.ids &&
                                disc_independent_model.value(state) ==
                                    disc_independent_model.value(other_disc);

  bool ignored = false;
  const CanonicalState canonical = canonicalize(state);
  const auto chance_seeds = stratifiedChanceSeeds(
      observableHash(canonical.state), 7);
  std::array<int, 8> buckets{};
  for (std::uint32_t seed : chance_seeds) {
    Mulberry32 random(seed);
    ++buckets[random.nextDisc()];
  }
  for (int disc = 1; disc <= 7; ++disc) ignored |= buckets[disc] != 1;
  const bool stratified = !ignored;

  const float before_target = target.value(state);
  online.perturbForTest(0, 5);
  const bool target_lagged = target.value(state) == before_target &&
                             !online.sameWeights(target);
  target.copyWeightsFrom(online);
  const bool target_synced = online.sameWeights(target);

  Replay replay(3);
  replay.push(compact(state));
  replay.push(compact(other_disc));
  replay.push(compact(mirrored));
  replay.push(compact(state));
  const bool replay_bounded = replay.size() == 3 && replay.capacity() == 3;
  const bool memory_bounded =
      2 * online.bytes() + replay.bytes() < kMemoryLimit;
  const float fitted = bellmanTarget(target, compact(state), options);
  const bool finite_target = std::isfinite(fitted) && fitted >= 0 &&
                             fitted <= options.target_cap;
  const bool passed = exhaustiveIndexTest() &&
                      std::abs(value - mirror_value) < 1e-6f &&
                      mirror_actions && deterministic && disc_encoded &&
                      disc_independent &&
                      stratified && target_lagged && target_synced &&
                      replay_bounded && memory_bounded && finite_target;
  output << "BELLMAN_NTUPLE_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"exhaustiveIndices\":"
         << (exhaustiveIndexTest() ? "true" : "false")
         << ",\"mirrorValue\":"
         << (std::abs(value - mirror_value) < 1e-6f ? "true" : "false")
         << ",\"mirrorActions\":"
         << (mirror_actions ? "true" : "false")
         << ",\"seedBlindDeterministic\":"
         << (deterministic ? "true" : "false")
         << ",\"visibleDiscEncoded\":"
         << (disc_encoded ? "true" : "false")
         << ",\"discIndependentAblation\":"
         << (disc_independent ? "true" : "false")
         << ",\"firstChanceStratified\":"
         << (stratified ? "true" : "false")
         << ",\"targetLagged\":"
         << (target_lagged ? "true" : "false")
         << ",\"targetSynced\":"
         << (target_synced ? "true" : "false")
         << ",\"replayBounded\":"
         << (replay_bounded ? "true" : "false")
         << ",\"memoryBounded\":"
         << (memory_bounded ? "true" : "false")
         << ",\"finiteTarget\":"
         << (finite_target ? "true" : "false")
         << ",\"nodes\":" << kNodeCount
         << ",\"activeFeatures\":" << kSharedActiveFeatures << "}\n";
  return passed;
}

std::string valueAfter(int argc, char** argv, const std::string& name,
                       const std::string& fallback) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == name) return argv[index + 1];
  }
  return fallback;
}

int parseInt(const std::string& value, const char* name) {
  std::size_t consumed = 0;
  const long parsed = std::stol(value, &consumed, 0);
  if (consumed != value.size() || parsed < std::numeric_limits<int>::min() ||
      parsed > std::numeric_limits<int>::max()) {
    throw std::invalid_argument(std::string("invalid ") + name);
  }
  return static_cast<int>(parsed);
}

float parseFloat(const std::string& value, const char* name) {
  std::size_t consumed = 0;
  const float parsed = std::stof(value, &consumed);
  if (consumed != value.size() || !std::isfinite(parsed)) {
    throw std::invalid_argument(std::string("invalid ") + name);
  }
  return parsed;
}

std::uint32_t parseUint32(const std::string& value, const char* name) {
  std::size_t consumed = 0;
  const unsigned long parsed = std::stoul(value, &consumed, 0);
  if (consumed != value.size() ||
      parsed > std::numeric_limits<std::uint32_t>::max()) {
    throw std::invalid_argument(std::string("invalid ") + name);
  }
  return static_cast<std::uint32_t>(parsed);
}

Options parseOptions(int argc, char** argv) {
  Options options;
  options.training_games =
      parseInt(valueAfter(argc, argv, "--games", "10000"), "--games");
  options.probe_games = parseInt(
      valueAfter(argc, argv, "--probe-games", "64"), "--probe-games");
  options.max_moves = parseInt(
      valueAfter(argc, argv, "--max-moves", "1000"), "--max-moves");
  options.chance_samples = parseInt(
      valueAfter(argc, argv, "--chance-samples", "7"), "--chance-samples");
  options.replay_capacity = parseInt(
      valueAfter(argc, argv, "--replay-capacity", "100000"),
      "--replay-capacity");
  options.replay_warmup = parseInt(
      valueAfter(argc, argv, "--replay-warmup", "1000"),
      "--replay-warmup");
  options.updates_per_step = parseInt(
      valueAfter(argc, argv, "--updates-per-step", "1"),
      "--updates-per-step");
  options.target_sync_updates = parseInt(
      valueAfter(argc, argv, "--target-sync-updates", "10000"),
      "--target-sync-updates");
  options.report_every = parseInt(
      valueAfter(argc, argv, "--report-every", "10000"),
      "--report-every");
  options.gamma =
      parseFloat(valueAfter(argc, argv, "--gamma", "0.997"), "--gamma");
  options.learning_rate = parseFloat(
      valueAfter(argc, argv, "--learning-rate", "0.05"),
      "--learning-rate");
  options.optimistic_value = parseFloat(
      valueAfter(argc, argv, "--optimistic-value", "200"),
      "--optimistic-value");
  options.target_cap = parseFloat(
      valueAfter(argc, argv, "--target-cap", "2000"), "--target-cap");
  options.epsilon_start = parseFloat(
      valueAfter(argc, argv, "--epsilon-start", "1"), "--epsilon-start");
  options.epsilon_end = parseFloat(
      valueAfter(argc, argv, "--epsilon-end", "0.1"), "--epsilon-end");
  options.training_seed_start = parseUint32(
      valueAfter(argc, argv, "--training-seed-start", "0x3d700000"),
      "--training-seed-start");
  options.probe_seed_start = parseUint32(
      valueAfter(argc, argv, "--probe-seed-start", "0x4d700000"),
      "--probe-seed-start");
  options.learner_seed = parseUint32(
      valueAfter(argc, argv, "--learner-seed", "0x0b3114d7"),
      "--learner-seed");
  for (int index = 1; index < argc; ++index) {
    if (std::string(argv[index]) == "--disc-independent") {
      options.disc_independent = true;
    }
    if (std::string(argv[index]) == "--positional-residual") {
      options.positional_residual = true;
    }
  }
  return options;
}

}  // namespace drop7::bellman_ntuple

int main(int argc, char** argv) {
  try {
    std::cout.setf(std::ios::unitbuf);
    if (argc < 2) {
      std::cerr << "usage: drop7_bellman_ntuple --self-test | --train [options]\n";
      return 2;
    }
    const std::string mode = argv[1];
    if (mode == "--self-test") {
      return drop7::bellman_ntuple::selfTest(std::cout) ? 0 : 1;
    }
    if (mode == "--train") {
      return drop7::bellman_ntuple::train(
          drop7::bellman_ntuple::parseOptions(argc, argv));
    }
    throw std::invalid_argument("unknown mode: " + mode);
  } catch (const std::exception& error) {
    std::cerr << "drop7_bellman_ntuple: " << error.what() << '\n';
    return 1;
  }
}
