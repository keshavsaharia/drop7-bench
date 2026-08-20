#define DROP7_FAIR_ONLY_HORIZON_LIBRARY
#include "../../fair-expectimax/reference/fair-only-horizon.cpp"
#undef DROP7_FAIR_ONLY_HORIZON_LIBRARY

#include <bit>
#include <deque>
#include <filesystem>
#include <fstream>
#include <optional>

// Trains a bounded, high-throughput off-policy action-value model for exact
// five-drop Hardcore scoring.  The deployed object is a sparse hashed n-tuple
// Q network.  Double-DQN targets, five-step returns and proportional replay
// provide the Rainbow-lite training pieces without putting a large dense
// network on the critical path.
namespace drop7::rainbow_ntuple_q {

namespace fair = drop7::fair_only_horizon;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kTrainingSeedStart = 0x3d40'0000u;
constexpr std::uint32_t kTrainingSeedEndExclusive = 0x3d42'0000u;
constexpr std::uint32_t kRandomProbeStart = 0x4d40'0000u;
constexpr std::uint32_t kFairProbeStart = 0x4d40'0020u;
constexpr std::uint32_t kFinalProbeStart = 0x4d40'0040u;
constexpr std::uint32_t kLearnerSeed = 0x3d40'c0deu;
constexpr std::uint32_t kEvaluationRandomDomain = 0x5241'494eu;

constexpr int kStageATransitions = 250'000;
constexpr int kStageBTransitions = 1'000'000;
constexpr int kStageCTransitions = 4'000'000;
constexpr int kTrainingMaximumMoves = 300;
constexpr int kEvaluationMaximumMoves = 1'000;
constexpr int kRandomProbeGames = 32;
constexpr int kFairProbeGames = 32;
constexpr int kFinalProbeGames = 64;
constexpr int kNstep = 5;
constexpr float kGamma = 0.997f;
constexpr int kReplayCapacity = 1 << 17;
constexpr int kReplayWarmup = 8'192;
constexpr int kBatchSize = 32;
constexpr int kTrainEvery = 16;
constexpr int kTargetSyncUpdates = 4'096;
constexpr float kLearningRate = 0.03f;
constexpr float kPriorityAlpha = 0.60f;
constexpr float kPriorityBetaStart = 0.40f;
constexpr float kPriorityEpsilon = 0.001f;
constexpr float kEpsilonStart = 1.0f;
constexpr float kEpsilonEnd = 0.05f;
constexpr int kEpsilonAnnealTransitions = 2'000'000;
constexpr int kPriorityBetaAnnealTransitions = kStageCTransitions;
constexpr int kHashBits = 23;
constexpr std::size_t kHashBuckets = std::size_t{1} << kHashBits;
constexpr std::size_t kDeployedModelLimit = 128ull * 1024 * 1024;
constexpr std::size_t kRuntimeRssLimit = 256ull * 1024 * 1024;
constexpr double kWallLimitSeconds = 30.0 * 60.0;
constexpr double kStageAScoreRatio = 1.10;
constexpr double kStageAMoveRatio = 1.05;
constexpr double kFinalScoreRatio = 1.05;
constexpr double kT975Df31 = 2.039513;
constexpr double kT975Df63 = 1.998341;
constexpr std::array<int, kBoardSize> kActionOrder{{3, 2, 4, 1, 5, 0, 6}};

constexpr int kTupleTables = 92;
constexpr int kAuxFeatures = 49 + 1 + 1 + 7 + 7 + 10;
constexpr int kActiveFeatures = kAuxFeatures + 2 * kTupleTables;

static_assert(kLevelBonus == 17'000);
static_assert(kMovesPerLevel == 5);
static_assert(kTupleTables == 28 + 28 + 36);
static_assert(kAuxFeatures == 75 && kActiveFeatures == 259);
static_assert((kHashBuckets & (kHashBuckets - 1)) == 0);
static_assert(kHashBuckets * sizeof(float) < kDeployedModelLimit);
static_assert(kReplayCapacity > kReplayWarmup);
static_assert(kTrainingSeedStart + 0x20'000u == kTrainingSeedEndExclusive);
static_assert(kRandomProbeStart + kRandomProbeGames <= kFairProbeStart);
static_assert(kFairProbeStart + kFairProbeGames <= kFinalProbeStart);
static_assert(kFinalProbeStart + kFinalProbeGames < 0x4d41'0000u);
static_assert((kTrainingSeedStart >> 24u) == 0x3du &&
              (kRandomProbeStart >> 24u) == 0x4du);
static_assert((kTrainingSeedStart >> 24u) != 0x7du &&
              (kTrainingSeedStart >> 24u) != 0xd7u &&
              (kRandomProbeStart >> 24u) != 0x7du &&
              (kRandomProbeStart >> 24u) != 0xd7u);

struct Options {
  std::string output = "/tmp/drop7-rainbow-ntuple-q.json";
  std::string checkpoint = "/tmp/drop7-rainbow-ntuple-q.bin";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string flag = argv[index];
    if (flag == "--output") result.output = argv[index + 1];
    else if (flag == "--checkpoint") result.checkpoint = argv[index + 1];
    else throw std::invalid_argument("unknown option " + flag);
  }
  return result;
}

struct Rng {
  explicit Rng(std::uint32_t seed) : random(seed) {}
  std::uint32_t bits() { return random.nextBits(); }
  float unit() { return static_cast<float>(random.nextUnit()); }
  int bounded(int bound) {
    return static_cast<int>(
        (static_cast<std::uint64_t>(bits()) *
         static_cast<std::uint32_t>(bound)) >>
        32u);
  }
  Mulberry32 random;
};

struct PublicState {
  Board board{};
  std::uint8_t next_disc = 1;
  std::uint8_t moves_remaining = kMovesPerLevel;

  bool operator==(const PublicState&) const = default;
};

PublicState publicState(const State& source) {
  return {source.board, source.next_disc,
          static_cast<std::uint8_t>(source.moves_remaining)};
}

State materialize(const PublicState& source) {
  State result;
  result.board = source.board;
  result.next_disc = source.next_disc;
  result.moves_remaining = source.moves_remaining;
  return result;
}

PublicState mirrorState(const PublicState& source) {
  PublicState result = source;
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      result.board[indexOf(row, column)] =
          source.board[indexOf(row, kBoardSize - 1 - column)];
    }
  }
  return result;
}

std::uint8_t legalMask(const PublicState& state) {
  std::uint8_t result = 0;
  for (int action = 0; action < kBoardSize; ++action) {
    if (isLegal(state.board, action)) {
      result |= static_cast<std::uint8_t>(1u << action);
    }
  }
  return result;
}

std::uint64_t mix64(std::uint64_t value) {
  value ^= value >> 30u;
  value *= 0xbf58'476d'1ce4'e5b9ull;
  value ^= value >> 27u;
  value *= 0x94d0'49bb'1331'11ebull;
  value ^= value >> 31u;
  return value;
}

std::uint32_t featureBucket(std::uint64_t family, std::uint64_t table,
                            std::uint64_t pattern, int action,
                            int next_disc = 0) {
  std::uint64_t key = family * 0x9e37'79b9'7f4a'7c15ull;
  key ^= (table + 1) * 0xd6e8'feb8'6659'fd93ull;
  key ^= (pattern + 1) * 0xa076'1d64'78bd'642full;
  key ^= static_cast<std::uint64_t>(action + 1) *
         0xe703'7ed1'a0b4'28dbull;
  key ^= static_cast<std::uint64_t>(next_disc + 1) *
         0x8ebc'6af0'9c88'c6e3ull;
  return static_cast<std::uint32_t>(mix64(key) & (kHashBuckets - 1));
}

int code4(std::uint8_t first, std::uint8_t second, std::uint8_t third,
          std::uint8_t fourth) {
  return ((first * 10 + second) * 10 + third) * 10 + fourth;
}

struct FeatureSet {
  std::array<std::uint32_t, kActiveFeatures> ids{};
  int count = 0;
};

FeatureSet features(const PublicState& state, int action) {
  if (state.next_disc < 1 || state.next_disc > kBoardSize ||
      state.moves_remaining < 1 || state.moves_remaining > kMovesPerLevel ||
      action < 0 || action >= kBoardSize) {
    throw std::invalid_argument("invalid public action-value input");
  }
  FeatureSet result;
  std::array<int, 10> token_counts{};
  for (int cell = 0; cell < kCellCount; ++cell) {
    const int token = state.board[cell];
    if (token < 0 || token > 9) {
      throw std::invalid_argument("invalid board token");
    }
    result.ids[result.count++] =
        featureBucket(1, static_cast<std::uint64_t>(cell), token, action);
    ++token_counts[token];
  }
  result.ids[result.count++] =
      featureBucket(2, 0, state.next_disc, action);
  result.ids[result.count++] =
      featureBucket(3, 0, state.moves_remaining, action);
  for (int column = 0; column < kBoardSize; ++column) {
    int height = 0;
    for (int row = 0; row < kBoardSize; ++row) {
      height += state.board[indexOf(row, column)] != kEmpty;
    }
    result.ids[result.count++] = featureBucket(4, column, height, action);
  }
  for (int row = 0; row < kBoardSize; ++row) {
    int occupied = 0;
    for (int column = 0; column < kBoardSize; ++column) {
      occupied += state.board[indexOf(row, column)] != kEmpty;
    }
    result.ids[result.count++] = featureBucket(5, row, occupied, action);
  }
  for (int token = 0; token < 10; ++token) {
    result.ids[result.count++] =
        featureBucket(6, token, token_counts[token], action);
  }

  int table = 0;
  auto add_tuple = [&](int pattern) {
    result.ids[result.count++] =
        featureBucket(7, table, pattern, action);
    result.ids[result.count++] =
        featureBucket(8, table, pattern, action, state.next_disc);
    ++table;
  };
  for (int row = 0; row < kBoardSize; ++row) {
    for (int start = 0; start <= kBoardSize - 4; ++start) {
      add_tuple(code4(state.board[indexOf(row, start)],
                      state.board[indexOf(row, start + 1)],
                      state.board[indexOf(row, start + 2)],
                      state.board[indexOf(row, start + 3)]));
    }
  }
  for (int column = 0; column < kBoardSize; ++column) {
    for (int start = 0; start <= kBoardSize - 4; ++start) {
      add_tuple(code4(state.board[indexOf(start, column)],
                      state.board[indexOf(start + 1, column)],
                      state.board[indexOf(start + 2, column)],
                      state.board[indexOf(start + 3, column)]));
    }
  }
  for (int row = 0; row < kBoardSize - 1; ++row) {
    for (int column = 0; column < kBoardSize - 1; ++column) {
      add_tuple(code4(state.board[indexOf(row, column)],
                      state.board[indexOf(row, column + 1)],
                      state.board[indexOf(row + 1, column)],
                      state.board[indexOf(row + 1, column + 1)]));
    }
  }
  if (table != kTupleTables || result.count != kActiveFeatures) {
    throw std::logic_error("rainbow n-tuple feature invariant failed");
  }
  return result;
}

struct Model {
  std::vector<float> weights;

  Model() : weights(kHashBuckets) {}
};

std::array<float, kBoardSize> singleValues(const Model& model,
                                            const PublicState& state) {
  std::array<float, kBoardSize> result{};
  for (int action = 0; action < kBoardSize; ++action) {
    const FeatureSet active = features(state, action);
    double value = 0.0;
    for (int index = 0; index < active.count; ++index) {
      value += model.weights[active.ids[index]];
    }
    result[action] = static_cast<float>(value);
  }
  return result;
}

std::array<float, kBoardSize> ensembleValues(const Model& model,
                                              const PublicState& state) {
  const auto direct = singleValues(model, state);
  const auto reflected = singleValues(model, mirrorState(state));
  std::array<float, kBoardSize> result{};
  const std::uint8_t mask = legalMask(state);
  for (int action = 0; action < kBoardSize; ++action) {
    result[action] =
        (mask & (1u << action)) != 0
            ? 0.5f * (direct[action] +
                      reflected[kBoardSize - 1 - action])
            : -std::numeric_limits<float>::infinity();
  }
  return result;
}

int greedyAction(const Model& model, const PublicState& state) {
  const auto q = ensembleValues(model, state);
  int result = -1;
  float best = -std::numeric_limits<float>::infinity();
  for (const int action : kActionOrder) {
    if (q[action] > best) {
      best = q[action];
      result = action;
    }
  }
  return result;
}

struct Transition {
  PublicState state{};
  PublicState next{};
  float reward = 0.0f;
  float discount = 0.0f;
  std::uint8_t action = 0;
  std::uint8_t terminal = 0;
};

static_assert(sizeof(PublicState) <= 51);
static_assert(sizeof(Transition) <= 116);
static_assert(static_cast<std::size_t>(kReplayCapacity) * sizeof(Transition) <
              16ull * 1024 * 1024);

struct OneStep {
  PublicState state{};
  PublicState next{};
  float reward = 0.0f;
  int action = -1;
  bool terminal = false;
};

class NstepAccumulator {
 public:
  std::vector<Transition> push(const OneStep& step) {
    pending_.push_back(step);
    std::vector<Transition> result;
    if (static_cast<int>(pending_.size()) >= kNstep) {
      result.push_back(make(kNstep));
      pending_.pop_front();
    }
    if (step.terminal) flush(result, true);
    return result;
  }

  std::vector<Transition> truncate() {
    std::vector<Transition> result;
    flush(result, false);
    return result;
  }

  bool empty() const { return pending_.empty(); }

 private:
  Transition make(int steps) const {
    if (steps <= 0 || steps > static_cast<int>(pending_.size())) {
      throw std::logic_error("invalid n-step span");
    }
    Transition result;
    result.state = pending_.front().state;
    result.action = static_cast<std::uint8_t>(pending_.front().action);
    float discount = 1.0f;
    for (int index = 0; index < steps; ++index) {
      result.reward += discount * pending_[index].reward;
      discount *= kGamma;
    }
    result.next = pending_[steps - 1].next;
    result.terminal = pending_[steps - 1].terminal ? 1 : 0;
    result.discount = result.terminal != 0 ? 0.0f : discount;
    return result;
  }

  void flush(std::vector<Transition>& output, bool terminal) {
    while (!pending_.empty()) {
      const int steps = static_cast<int>(pending_.size());
      Transition transition = make(steps);
      if (terminal) {
        transition.terminal = 1;
        transition.discount = 0.0f;
      }
      output.push_back(transition);
      pending_.pop_front();
    }
  }

  std::deque<OneStep> pending_;
};

class Replay {
 public:
  Replay() : transitions_(kReplayCapacity), tree_(2 * kReplayCapacity) {}

  void add(const Transition& transition) {
    transitions_[next_] = transition;
    setPriority(next_, maximum_priority_);
    next_ = (next_ + 1) & (kReplayCapacity - 1);
    size_ = std::min(size_ + 1, kReplayCapacity);
  }

  int size() const { return size_; }

  double totalPriority() const { return tree_[1]; }

  int sample(float unit) const {
    if (size_ == 0 || !(tree_[1] > 0.0f)) {
      throw std::logic_error("cannot sample empty prioritized replay");
    }
    double mass = static_cast<double>(std::clamp(
                      unit, 0.0f, std::nextafter(1.0f, 0.0f))) *
                  tree_[1];
    mass = std::min(mass, std::nextafter(tree_[1], 0.0));
    int node = 1;
    while (node < kReplayCapacity) {
      const int left = 2 * node;
      if (mass < tree_[left]) {
        node = left;
      } else {
        mass -= tree_[left];
        node = left + 1;
      }
    }
    const int index = node - kReplayCapacity;
    if (index < 0 || index >= size_) {
      throw std::logic_error("prioritized replay sampled unwritten slot");
    }
    return index;
  }

  const Transition& at(int index) const { return transitions_.at(index); }

  float probability(int index) const {
    return static_cast<float>(tree_[kReplayCapacity + index] / tree_[1]);
  }

  void update(int index, float absolute_td) {
    const float priority = std::pow(
        std::max(kPriorityEpsilon, absolute_td + kPriorityEpsilon),
        kPriorityAlpha);
    maximum_priority_ = std::max(maximum_priority_, priority);
    setPriority(index, priority);
  }

 private:
  void setPriority(int index, float priority) {
    int node = kReplayCapacity + index;
    const double delta = static_cast<double>(priority) - tree_[node];
    while (node >= 1) {
      tree_[node] += delta;
      node /= 2;
    }
  }

  std::vector<Transition> transitions_;
  std::vector<double> tree_;
  int next_ = 0;
  int size_ = 0;
  float maximum_priority_ = 1.0f;
};

float linearSchedule(float first, float last, std::uint64_t step,
                     std::uint64_t duration) {
  const float fraction = std::min(
      1.0f, static_cast<float>(step) / static_cast<float>(duration));
  return first + fraction * (last - first);
}

struct SparseGradient {
  std::array<std::pair<std::uint32_t, float>, 2 * kActiveFeatures> entries{};
  int count = 0;
};

SparseGradient ensembleGradient(const PublicState& state, int action) {
  SparseGradient result;
  const FeatureSet direct = features(state, action);
  const FeatureSet reflected =
      features(mirrorState(state), kBoardSize - 1 - action);
  for (int index = 0; index < direct.count; ++index) {
    result.entries[result.count++] = {direct.ids[index], 0.5f};
  }
  for (int index = 0; index < reflected.count; ++index) {
    result.entries[result.count++] = {reflected.ids[index], 0.5f};
  }
  std::sort(result.entries.begin(), result.entries.begin() + result.count,
            [](const auto& left, const auto& right) {
              return left.first < right.first;
            });
  return result;
}

double normalizedQUpdate(Model& model, const PublicState& state, int action,
                         float signal) {
  const SparseGradient gradient = ensembleGradient(state, action);
  std::array<std::pair<std::uint32_t, float>, 2 * kActiveFeatures> unique{};
  int unique_count = 0;
  for (int index = 0; index < gradient.count; ++index) {
    if (unique_count > 0 &&
        unique[unique_count - 1].first == gradient.entries[index].first) {
      unique[unique_count - 1].second += gradient.entries[index].second;
    } else {
      unique[unique_count++] = gradient.entries[index];
    }
  }
  double squared_norm = 0.0;
  for (int index = 0; index < unique_count; ++index) {
    squared_norm += unique[index].second * unique[index].second;
  }
  if (!(squared_norm > 0.0)) {
    throw std::logic_error("empty normalized Q gradient");
  }
  double maximum_change = 0.0;
  for (int index = 0; index < unique_count; ++index) {
    const double change = kLearningRate * signal * unique[index].second /
                          squared_norm;
    model.weights[unique[index].first] += static_cast<float>(change);
    maximum_change = std::max(maximum_change, std::abs(change));
  }
  return maximum_change;
}

int maskedArgmax(const std::array<float, kBoardSize>& values,
                 std::uint8_t mask) {
  int result = -1;
  float best = -std::numeric_limits<float>::infinity();
  for (const int action : kActionOrder) {
    if ((mask & (1u << action)) == 0) continue;
    if (values[action] > best) {
      best = values[action];
      result = action;
    }
  }
  return result;
}

float doubleDqnTarget(const Model& online, const Model& target,
                      const Transition& transition) {
  if (transition.terminal != 0 || transition.discount == 0.0f) {
    return transition.reward;
  }
  const auto online_q = ensembleValues(online, transition.next);
  const int selected = maskedArgmax(online_q, legalMask(transition.next));
  if (selected < 0) return transition.reward;
  const auto target_q = ensembleValues(target, transition.next);
  return transition.reward + transition.discount * target_q[selected];
}

struct LearnStats {
  std::uint64_t batch_updates = 0;
  std::uint64_t sampled_transitions = 0;
  std::uint64_t target_syncs = 0;
  double absolute_td_sum = 0.0;
  double huber_loss_sum = 0.0;
  double maximum_absolute_td = 0.0;
  double maximum_parameter_change = 0.0;
};

class Learner {
 public:
  Learner() : random_(kLearnerSeed) {}

  int behaviorAction(const PublicState& state) {
    const std::uint8_t mask = legalMask(state);
    int legal_count = 0;
    std::array<int, kBoardSize> legal{};
    for (int action = 0; action < kBoardSize; ++action) {
      if ((mask & (1u << action)) != 0) legal[legal_count++] = action;
    }
    if (legal_count == 0) return -1;
    const float epsilon = linearSchedule(
        kEpsilonStart, kEpsilonEnd, environment_steps_,
        kEpsilonAnnealTransitions);
    if (random_.unit() < epsilon) return legal[random_.bounded(legal_count)];
    return greedyAction(online_, state);
  }

  void add(const Transition& transition) { replay_.add(transition); }

  void finishEnvironmentStep() {
    ++environment_steps_;
    if (replay_.size() >= kReplayWarmup &&
        environment_steps_ % kTrainEvery == 0) {
      trainBatch();
    }
  }

  const Model& model() const { return online_; }
  Model& mutableModel() { return online_; }
  const LearnStats& stats() const { return stats_; }
  std::uint64_t environmentSteps() const { return environment_steps_; }
  int replaySize() const { return replay_.size(); }
  float epsilon() const {
    return linearSchedule(kEpsilonStart, kEpsilonEnd, environment_steps_,
                          kEpsilonAnnealTransitions);
  }
  float beta() const {
    return linearSchedule(kPriorityBetaStart, 1.0f, environment_steps_,
                          kPriorityBetaAnnealTransitions);
  }

 private:
  void trainBatch() {
    std::array<int, kBatchSize> indices{};
    std::array<float, kBatchSize> importance{};
    float maximum_importance = 0.0f;
    for (int sample = 0; sample < kBatchSize; ++sample) {
      const float stratified =
          (static_cast<float>(sample) + random_.unit()) / kBatchSize;
      indices[sample] = replay_.sample(stratified);
      const float probability = replay_.probability(indices[sample]);
      importance[sample] = std::pow(
          std::max(1.0e-12f, replay_.size() * probability), -beta());
      maximum_importance =
          std::max(maximum_importance, importance[sample]);
    }
    for (float& value : importance) value /= maximum_importance;

    for (int sample = 0; sample < kBatchSize; ++sample) {
      const Transition& transition = replay_.at(indices[sample]);
      const auto q = ensembleValues(online_, transition.state);
      const float target = doubleDqnTarget(online_, target_, transition);
      const float td = target - q[transition.action];
      const float huber_signal = std::clamp(td, -1.0f, 1.0f);
      const float weighted_signal = importance[sample] * huber_signal;
      stats_.maximum_parameter_change = std::max(
          stats_.maximum_parameter_change,
          normalizedQUpdate(online_, transition.state, transition.action,
                            weighted_signal));
      replay_.update(indices[sample], std::abs(td));
      stats_.absolute_td_sum += std::abs(td);
      stats_.maximum_absolute_td =
          std::max(stats_.maximum_absolute_td, static_cast<double>(std::abs(td)));
      stats_.huber_loss_sum +=
          std::abs(td) <= 1.0f ? 0.5 * td * td : std::abs(td) - 0.5;
      ++stats_.sampled_transitions;
    }
    ++stats_.batch_updates;
    if (stats_.batch_updates % kTargetSyncUpdates == 0) {
      target_.weights = online_.weights;
      ++stats_.target_syncs;
    }
  }

  Model online_{};
  Model target_{};
  Replay replay_{};
  Rng random_;
  LearnStats stats_{};
  std::uint64_t environment_steps_ = 0;
};

std::uint64_t modelFingerprint(const Model& model) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const float value : model.weights) {
    std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    for (int byte = 0; byte < 4; ++byte) {
      hash ^= bits & 0xffu;
      hash *= 0x0000'0100'0000'01b3ull;
      bits >>= 8u;
    }
  }
  return hash;
}

constexpr std::array<char, 8> kCheckpointMagic{{
    'D', '7', 'R', 'N', 'Q', '1', '7', '\0',
}};

struct CheckpointHeader {
  std::array<char, 8> magic{};
  std::uint32_t version = 1;
  std::uint32_t level_bonus = 0;
  std::uint64_t buckets = 0;
  std::uint64_t environment_steps = 0;
  std::uint64_t fingerprint = 0;
};

void writeCheckpoint(const std::string& path, const Model& model,
                     std::uint64_t environment_steps) {
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("could not write rainbow checkpoint");
  const CheckpointHeader header{
      kCheckpointMagic, 1, static_cast<std::uint32_t>(kLevelBonus),
      kHashBuckets, environment_steps, modelFingerprint(model)};
  output.write(reinterpret_cast<const char*>(&header), sizeof(header));
  output.write(reinterpret_cast<const char*>(model.weights.data()),
               static_cast<std::streamsize>(model.weights.size() *
                                            sizeof(float)));
  if (!output) throw std::runtime_error("rainbow checkpoint write failed");
}

std::pair<Model, std::uint64_t> readCheckpoint(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not read rainbow checkpoint");
  CheckpointHeader header;
  input.read(reinterpret_cast<char*>(&header), sizeof(header));
  if (!input || header.magic != kCheckpointMagic || header.version != 1 ||
      header.level_bonus != kLevelBonus || header.buckets != kHashBuckets) {
    throw std::runtime_error("invalid rainbow checkpoint header");
  }
  Model model;
  input.read(reinterpret_cast<char*>(model.weights.data()),
             static_cast<std::streamsize>(model.weights.size() *
                                          sizeof(float)));
  const bool payload_ok = static_cast<bool>(input);
  char trailing = 0;
  const bool has_trailing = static_cast<bool>(input.read(&trailing, 1));
  if (!payload_ok || !input.eof() || has_trailing ||
      header.fingerprint != modelFingerprint(model)) {
    throw std::runtime_error("invalid rainbow checkpoint payload");
  }
  return {std::move(model), header.environment_steps};
}

std::uint64_t fileBytes(const std::string& path) {
  std::error_code error;
  const std::uintmax_t bytes = std::filesystem::file_size(path, error);
  if (error || bytes > std::numeric_limits<std::uint64_t>::max()) {
    throw std::runtime_error("could not size rainbow file");
  }
  return static_cast<std::uint64_t>(bytes);
}

int randomAction(const PublicState& state, Rng& random) {
  std::array<int, kBoardSize> legal{};
  int count = 0;
  for (int action = 0; action < kBoardSize; ++action) {
    if (isLegal(state.board, action)) legal[count++] = action;
  }
  return count > 0 ? legal[random.bounded(count)] : -1;
}

int fairD1Action(const PublicState& observation) {
  State source = materialize(observation);
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  fair::SearchContext context;
  const fair::RootEvaluation root = fair::rootDecision(canonical, 1, context);
  int action = root.action;
  if (action < 0) action = centerFirstMove(canonical.board);
  return mirrored ? kBoardSize - 1 - action : action;
}

enum class PolicyKind { kRandom, kLearned, kFairD1 };

std::uint64_t futureDiscStreamHash(std::uint32_t seed, int moves) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (int move = 0; move < moves; ++move) {
    hash ^= headlessDisc(seed, move);
    hash *= 0x0000'0100'0000'01b3ull;
  }
  return hash;
}

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t future_disc_stream_hash = 0;
  std::uint64_t fair_work = 0;
};

GameResult runEvaluationGame(std::uint32_t seed, PolicyKind kind,
                             const Model* model, int policy_game_index) {
  State state = initialHeadlessState(seed);
  Rng random(mix32(kEvaluationRandomDomain ^
                   static_cast<std::uint32_t>(policy_game_index + 1)));
  std::uint64_t fair_work = 0;
  while (!state.game_over && state.moves_played < kEvaluationMaximumMoves) {
    if (state.next_disc != headlessDisc(seed, state.moves_played)) {
      throw std::runtime_error("paired future-disc stream mismatch");
    }
    const PublicState observation = publicState(state);
    int action = -1;
    if (kind == PolicyKind::kRandom) {
      action = randomAction(observation, random);
    } else if (kind == PolicyKind::kLearned) {
      if (model == nullptr) throw std::logic_error("missing learned model");
      action = greedyAction(*model, observation);
    } else {
      State source = materialize(observation);
      bool mirrored = false;
      const State canonical = cfpi::detail::canonicalState(source, mirrored);
      fair::SearchContext context;
      const fair::RootEvaluation root =
          fair::rootDecision(canonical, 1, context);
      action = root.action < 0 ? centerFirstMove(canonical.board) : root.action;
      action = mirrored ? kBoardSize - 1 - action : action;
      fair_work += context.work;
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("evaluation policy selected illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("evaluation move failed");
    }
  }
  return {seed, state.score, state.moves_played, !state.game_over,
          futureDiscStreamHash(seed, kEvaluationMaximumMoves), fair_work};
}

struct Cohort {
  std::vector<GameResult> games;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  int natural = 0;
  int censored = 0;
  double natural_mean_score = 0.0;
  double natural_mean_moves = 0.0;
  std::int64_t minimum_score = std::numeric_limits<std::int64_t>::max();
  std::int64_t maximum_score = std::numeric_limits<std::int64_t>::min();
  int minimum_moves = std::numeric_limits<int>::max();
  int maximum_moves = std::numeric_limits<int>::min();
  std::uint64_t fair_work = 0;
  double wall_seconds = 0.0;
};

Cohort evaluateCohort(std::uint32_t seed_start, int games, PolicyKind kind,
                      const Model* model) {
  const auto started = Clock::now();
  Cohort result;
  result.games.reserve(games);
  for (int game = 0; game < games; ++game) {
    const GameResult played =
        runEvaluationGame(seed_start + static_cast<std::uint32_t>(game), kind,
                          model, game);
    result.games.push_back(played);
    result.mean_score += static_cast<double>(played.score) / games;
    result.mean_moves += static_cast<double>(played.moves) / games;
    result.minimum_score = std::min(result.minimum_score, played.score);
    result.maximum_score = std::max(result.maximum_score, played.score);
    result.minimum_moves = std::min(result.minimum_moves, played.moves);
    result.maximum_moves = std::max(result.maximum_moves, played.moves);
    result.fair_work += played.fair_work;
    if (played.censored) {
      ++result.censored;
    } else {
      ++result.natural;
      result.natural_mean_score += played.score;
      result.natural_mean_moves += played.moves;
    }
  }
  if (result.natural > 0) {
    result.natural_mean_score /= result.natural;
    result.natural_mean_moves /= result.natural;
  }
  result.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

struct Difference {
  double mean_score = 0.0;
  double lower95_score = 0.0;
  double mean_moves = 0.0;
  double lower95_moves = 0.0;
  bool streams_identical = false;
};

Difference pairedDifference(const Cohort& candidate, const Cohort& baseline) {
  if (candidate.games.size() != baseline.games.size() ||
      candidate.games.size() < 2) {
    throw std::invalid_argument("invalid paired cohort sizes");
  }
  Difference result;
  std::vector<double> scores;
  std::vector<double> moves;
  scores.reserve(candidate.games.size());
  moves.reserve(candidate.games.size());
  result.streams_identical = true;
  for (std::size_t index = 0; index < candidate.games.size(); ++index) {
    const GameResult& first = candidate.games[index];
    const GameResult& second = baseline.games[index];
    result.streams_identical =
        result.streams_identical && first.seed == second.seed &&
        first.future_disc_stream_hash == second.future_disc_stream_hash;
    scores.push_back(static_cast<double>(first.score - second.score));
    moves.push_back(static_cast<double>(first.moves - second.moves));
  }
  const auto summarize = [](const std::vector<double>& values,
                            double critical) {
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) /
                        static_cast<double>(values.size());
    double squares = 0.0;
    for (const double value : values) {
      const double centered = value - mean;
      squares += centered * centered;
    }
    const double standard_error =
        std::sqrt(squares / static_cast<double>(values.size() - 1)) /
        std::sqrt(static_cast<double>(values.size()));
    return std::pair{mean, mean - critical * standard_error};
  };
  const double critical = candidate.games.size() == kFinalProbeGames
                              ? kT975Df63
                              : kT975Df31;
  const auto score = summarize(scores, critical);
  const auto move = summarize(moves, critical);
  result.mean_score = score.first;
  result.lower95_score = score.second;
  result.mean_moves = move.first;
  result.lower95_moves = move.second;
  if (!result.streams_identical) {
    throw std::runtime_error("paired evaluation did not share disc streams");
  }
  return result;
}

struct TrainingStats {
  std::uint64_t games = 0;
  std::uint64_t natural_games = 0;
  std::uint64_t censored_games = 0;
  std::int64_t score_sum = 0;
  std::uint64_t move_sum = 0;
  std::uint32_t next_seed = kTrainingSeedStart;
  double wall_seconds = 0.0;
};

void addTransitions(Learner& learner,
                    const std::vector<Transition>& transitions) {
  for (const Transition& transition : transitions) learner.add(transition);
}

class TrainingRun {
 public:
  explicit TrainingRun(Clock::time_point deadline) : deadline_(deadline) {}

  void trainTo(std::uint64_t target_transitions, std::ostream& progress) {
    const auto started = Clock::now();
    while (learner_.environmentSteps() < target_transitions) {
      if (Clock::now() >= deadline_) {
        throw std::runtime_error("rainbow training wall cap reached");
      }
      if (stats_.next_seed >= kTrainingSeedEndExclusive) {
        throw std::runtime_error("rainbow training seed family exhausted");
      }
      trainGame(stats_.next_seed++);
      ++stats_.games;
      if (stats_.games % 250 == 0 ||
          learner_.environmentSteps() >= target_transitions) {
        progress << "rainbow-train games=" << stats_.games
                 << " transitions=" << learner_.environmentSteps()
                 << " replay=" << learner_.replaySize()
                 << " epsilon=" << learner_.epsilon() << '\n';
      }
      if (fair::peakRssBytes() > kRuntimeRssLimit) {
        throw std::runtime_error("rainbow runtime RSS cap reached");
      }
    }
    stats_.wall_seconds +=
        std::chrono::duration<double>(Clock::now() - started).count();
  }

  const Learner& learner() const { return learner_; }
  Learner& learner() { return learner_; }
  const TrainingStats& stats() const { return stats_; }

 private:
  void trainGame(std::uint32_t seed) {
    State state = initialHeadlessState(seed);
    NstepAccumulator accumulator;
    while (!state.game_over && state.moves_played < kTrainingMaximumMoves) {
      if (state.next_disc != headlessDisc(seed, state.moves_played)) {
        throw std::runtime_error("training future-disc stream mismatch");
      }
      const PublicState observation = publicState(state);
      const int action = learner_.behaviorAction(observation);
      if (!isLegal(state.board, action)) {
        throw std::runtime_error("behavior policy selected illegal action");
      }
      MoveResult move;
      if (!playHeadlessMove(state, seed, action, move)) {
        throw std::runtime_error("training move failed");
      }
      const OneStep step{observation, publicState(state),
                         static_cast<float>(move.score_delta) /
                             static_cast<float>(kLevelBonus),
                         action, state.game_over};
      addTransitions(learner_, accumulator.push(step));
      learner_.finishEnvironmentStep();
    }
    if (!state.game_over) {
      addTransitions(learner_, accumulator.truncate());
      ++stats_.censored_games;
    } else {
      if (!accumulator.empty()) {
        throw std::logic_error("terminal n-step queue was not flushed");
      }
      ++stats_.natural_games;
    }
    stats_.score_sum += state.score;
    stats_.move_sum += state.moves_played;
  }

  Learner learner_{};
  TrainingStats stats_{};
  Clock::time_point deadline_;
};

struct Gate {
  bool score_mean = false;
  bool move_mean = false;
  bool score_lower95 = false;
  bool move_lower95 = false;
  bool streams = false;
  bool resources = false;
  bool passed = false;
};

Gate randomGate(const Cohort& candidate, const Cohort& random,
                const Difference& paired) {
  Gate result;
  result.score_mean =
      candidate.mean_score >= kStageAScoreRatio * random.mean_score;
  result.move_mean =
      candidate.mean_moves >= kStageAMoveRatio * random.mean_moves;
  result.score_lower95 = paired.lower95_score > 0.0;
  result.move_lower95 = paired.lower95_moves >= 0.0;
  result.streams = paired.streams_identical;
  result.resources = fair::peakRssBytes() <= kRuntimeRssLimit;
  result.passed = result.score_mean && result.move_mean &&
                  result.score_lower95 && result.move_lower95 &&
                  result.streams && result.resources;
  return result;
}

Gate fairGate(const Cohort& candidate, const Cohort& baseline,
              const Difference& paired, double score_ratio) {
  Gate result;
  result.score_mean = candidate.mean_score >= score_ratio * baseline.mean_score;
  result.move_mean = candidate.mean_moves >= baseline.mean_moves;
  result.score_lower95 = paired.lower95_score >= 0.0;
  result.move_lower95 = paired.lower95_moves >= 0.0;
  result.streams = paired.streams_identical;
  result.resources = fair::peakRssBytes() <= kRuntimeRssLimit;
  result.passed = result.score_mean && result.move_mean &&
                  result.score_lower95 && result.move_lower95 &&
                  result.streams && result.resources;
  return result;
}

struct CheckpointInfo {
  bool written = false;
  std::string stage;
  std::uint64_t environment_steps = 0;
  std::uint64_t bytes = 0;
  std::uint64_t fingerprint = 0;
};

CheckpointInfo preserveCheckpoint(const Options& options,
                                  const TrainingRun& training,
                                  std::string stage) {
  writeCheckpoint(options.checkpoint, training.learner().model(),
                  training.learner().environmentSteps());
  auto restored = readCheckpoint(options.checkpoint);
  const std::uint64_t fingerprint = modelFingerprint(restored.first);
  const std::uint64_t bytes = fileBytes(options.checkpoint);
  if (fingerprint != modelFingerprint(training.learner().model()) ||
      restored.second != training.learner().environmentSteps() ||
      bytes > kDeployedModelLimit) {
    throw std::runtime_error("rainbow deployed checkpoint resource failure");
  }
  return {true, std::move(stage), restored.second, bytes, fingerprint};
}

struct InferenceBenchmark {
  std::uint64_t state_evaluations = 0;
  std::uint64_t legal_action_values = 0;
  double seconds = 0.0;
  double state_evaluations_per_second = 0.0;
  double action_values_per_second = 0.0;
  double checksum = 0.0;
};

InferenceBenchmark benchmarkInference(const Model& model) {
  State fixture;
  fixture.board = initialBoard();
  fixture.next_disc = 4;
  fixture.moves_remaining = 3;
  const PublicState state = publicState(fixture);
  constexpr int repetitions = 20'000;
  const auto started = Clock::now();
  InferenceBenchmark result;
  for (int repetition = 0; repetition < repetitions; ++repetition) {
    const auto q = ensembleValues(model, state);
    for (int action = 0; action < kBoardSize; ++action) {
      if (!std::isfinite(q[action])) continue;
      result.checksum += q[action] *
                         static_cast<double>(action + 1 + (repetition & 1));
      ++result.legal_action_values;
    }
    ++result.state_evaluations;
  }
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  result.state_evaluations_per_second =
      result.state_evaluations / result.seconds;
  result.action_values_per_second =
      result.legal_action_values / result.seconds;
  return result;
}

void writeCohort(std::ostream& output, const Cohort& value) {
  output << "{\"games\":" << value.games.size()
         << ",\"meanScore\":" << value.mean_score
         << ",\"meanMoves\":" << value.mean_moves
         << ",\"naturalGames\":" << value.natural
         << ",\"censoredGames\":" << value.censored
         << ",\"naturalMeanScore\":";
  if (value.natural > 0) output << value.natural_mean_score;
  else output << "null";
  output << ",\"naturalMeanMoves\":";
  if (value.natural > 0) output << value.natural_mean_moves;
  else output << "null";
  output << ",\"scoreRange\":[" << value.minimum_score << ','
         << value.maximum_score << "],\"moveRange\":["
         << value.minimum_moves << ',' << value.maximum_moves
         << "],\"fairD1Work\":" << value.fair_work
         << ",\"wallSeconds\":" << value.wall_seconds << '}';
}

void writeDifference(std::ostream& output, const Difference& value) {
  output << "{\"meanScore\":" << value.mean_score
         << ",\"lower95Score\":" << value.lower95_score
         << ",\"meanMoves\":" << value.mean_moves
         << ",\"lower95Moves\":" << value.lower95_moves
         << ",\"identicalFutureDiscStreams\":"
         << (value.streams_identical ? "true" : "false") << '}';
}

void writeGate(std::ostream& output, const Gate& value) {
  output << "{\"passed\":" << (value.passed ? "true" : "false")
         << ",\"scoreMean\":" << (value.score_mean ? "true" : "false")
         << ",\"moveMean\":" << (value.move_mean ? "true" : "false")
         << ",\"scorePairedLower95\":"
         << (value.score_lower95 ? "true" : "false")
         << ",\"movePairedLower95\":"
         << (value.move_lower95 ? "true" : "false")
         << ",\"identicalFutureDiscStreams\":"
         << (value.streams ? "true" : "false")
         << ",\"resources\":" << (value.resources ? "true" : "false")
         << '}';
}

struct Audit {
  std::string status;
  std::optional<Cohort> random_a;
  std::optional<Cohort> candidate_a;
  std::optional<Difference> difference_a;
  std::optional<Gate> gate_a;
  std::optional<Cohort> fair_b;
  std::optional<Cohort> candidate_b;
  std::optional<Difference> difference_b;
  std::optional<Gate> gate_b;
  std::optional<Cohort> fair_c;
  std::optional<Cohort> candidate_c;
  std::optional<Difference> difference_c;
  std::optional<Gate> gate_c;
  CheckpointInfo checkpoint{};
  InferenceBenchmark inference{};
  double total_seconds = 0.0;
};

void writeOptionalStage(std::ostream& output, const char* name,
                        const std::optional<Cohort>& baseline,
                        const std::optional<Cohort>& candidate,
                        const std::optional<Difference>& difference,
                        const std::optional<Gate>& gate,
                        std::uint32_t seed_start, const char* baseline_name) {
  output << "  \"" << name << "\":";
  if (!candidate.has_value()) {
    output << "{\"opened\":false},\n";
    return;
  }
  output << "{\"opened\":true,\"seedStart\":" << seed_start
         << ",\"games\":" << candidate->games.size()
         << ",\"baseline\":\"" << baseline_name << "\",\"baselineMetrics\":";
  writeCohort(output, *baseline);
  output << ",\"candidateMetrics\":";
  writeCohort(output, *candidate);
  output << ",\"pairedCandidateMinusBaseline\":";
  writeDifference(output, *difference);
  output << ",\"gate\":";
  writeGate(output, *gate);
  output << "},\n";
}

void writeArtifact(const Options& options, const TrainingRun& training,
                   const Audit& audit) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not write rainbow artifact");
  const TrainingStats& trained = training.stats();
  const LearnStats& learned = training.learner().stats();
  const double training_throughput =
      trained.wall_seconds > 0.0
          ? training.learner().environmentSteps() / trained.wall_seconds
          : 0.0;
  const bool proposal = audit.gate_c.has_value() && audit.gate_c->passed;
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"corrected-17k-rainbow-ntuple-q\",\n"
            "  \"status\":\""
         << audit.status
         << "\",\n  \"evidenceClass\":\"staged-development-only\",\n"
            "  \"claimBoundary\":\"development probes only; no 0x7d or 0xd7 gameplay seed and no formal heldout/final claim\",\n"
            "  \"engine\":{\"mode\":\"five-drop numbered-only Hardcore/Blitz\",\"levelBonus\":"
         << kLevelBonus
         << ",\"reward\":\"unclipped score delta divided by 17000\",\"evaluationMaximumMoves\":"
         << kEvaluationMaximumMoves
         << ",\"trainingMaximumMoves\":" << kTrainingMaximumMoves
         << "},\n  \"observation\":{\"fields\":[\"public board\",\"visible next disc\",\"moves remaining in five-step rise phase\"],"
            "\"excluded\":[\"score\",\"level\",\"moves played\",\"game seed\",\"hidden cover values\"],"
            "\"actionMasking\":true,\"reflection\":\"exact mean of direct Q(a) and mirrored Q(6-a)\"},\n"
            "  \"model\":{\"kind\":\"hashed action-value n-tuple network\",\"hashBuckets\":"
         << kHashBuckets << ",\"tupleTables\":" << kTupleTables
         << ",\"activeFeaturesPerOrientationAction\":" << kActiveFeatures
         << ",\"tupleFamilies\":[\"28 horizontal length-4\",\"28 vertical length-4\",\"36 local 2x2\"],"
            "\"tupleContexts\":[\"action\",\"action plus visible next disc\"],"
            "\"parameterBytes\":" << kHashBuckets * sizeof(float)
         << ",\"deployedLimitBytes\":" << kDeployedModelLimit
         << "},\n  \"learner\":{\"algorithm\":\"Rainbow-lite sparse Double-DQN\",\"nStep\":"
         << kNstep << ",\"gamma\":" << kGamma
         << ",\"replayCapacity\":" << kReplayCapacity
         << ",\"replayWarmup\":" << kReplayWarmup
         << ",\"batchSize\":" << kBatchSize
         << ",\"trainEveryTransitions\":" << kTrainEvery
         << ",\"targetSyncBatchUpdates\":" << kTargetSyncUpdates
         << ",\"priorityAlpha\":" << kPriorityAlpha
         << ",\"priorityBetaStart\":" << kPriorityBetaStart
         << ",\"huberLoss\":true,\"normalizedSparseLearningRate\":"
         << kLearningRate << ",\"epsilon\":{\"start\":" << kEpsilonStart
         << ",\"end\":" << kEpsilonEnd
         << ",\"annealTransitions\":" << kEpsilonAnnealTransitions
         << "}},\n  \"seedProtocol\":{\"trainingStart\":"
         << kTrainingSeedStart << ",\"trainingEndExclusiveCap\":"
         << kTrainingSeedEndExclusive << ",\"nextUnopenedTrainingSeed\":"
         << trained.next_seed << ",\"randomProbeStart\":"
         << kRandomProbeStart << ",\"fairProbeStart\":" << kFairProbeStart
         << ",\"finalProbeStart\":" << kFinalProbeStart
         << ",\"opened7dGameplaySeeds\":0,\"openedD7GameplaySeeds\":0},\n"
            "  \"promotionProtocol\":{\"stageA\":{\"targetTransitions\":"
         << kStageATransitions << ",\"probeGames\":" << kRandomProbeGames
         << ",\"baseline\":\"random\",\"minimumScoreRatio\":"
         << kStageAScoreRatio << ",\"minimumMoveRatio\":"
         << kStageAMoveRatio
         << ",\"pairedLower95ScoreStrictlyPositive\":true,\"pairedLower95MovesNonnegative\":true},"
            "\"stageB\":{\"targetTransitions\":"
         << kStageBTransitions << ",\"probeGames\":" << kFairProbeGames
         << ",\"baseline\":\"exact corrected fair-D1\",\"scoreAndMovesNoninferior\":true,\"pairedLower95ScoreAndMovesNonnegative\":true},"
            "\"stageC\":{\"targetTransitions\":"
         << kStageCTransitions << ",\"probeGames\":" << kFinalProbeGames
         << ",\"baseline\":\"exact corrected fair-D1\",\"minimumScoreRatio\":"
         << kFinalScoreRatio
         << ",\"movesNoninferior\":true,\"pairedLower95ScoreAndMovesNonnegative\":true},"
            "\"stopAtFirstFailure\":true,\"checkpointOnlyAfterPassingGate\":true},\n"
            "  \"preDevelopmentImplementationAudit\":{\"failedAttemptBeforeAnyDevelopmentProbe\":true,"
            "\"lastReportedTrainingTransitions\":47146,\"developmentSeedsOpened\":0,"
            "\"fault\":\"float priority-tree accumulation selected an unwritten zero-priority tail slot\","
            "\"fix\":\"double-precision priority sum tree plus 60000-entry partial-buffer stress test\","
            "\"restart\":\"deterministic restart from the same approved training seed start\"},\n"
            "  \"training\":{\"environmentTransitions\":"
         << training.learner().environmentSteps() << ",\"games\":"
         << trained.games << ",\"naturalGames\":" << trained.natural_games
         << ",\"censoredGames\":" << trained.censored_games
         << ",\"meanScore\":"
         << (trained.games > 0
                 ? static_cast<double>(trained.score_sum) / trained.games
                 : 0.0)
         << ",\"meanMoves\":"
         << (trained.games > 0
                 ? static_cast<double>(trained.move_sum) / trained.games
                 : 0.0)
         << ",\"replaySize\":" << training.learner().replaySize()
         << ",\"finalEpsilon\":" << training.learner().epsilon()
         << ",\"finalPriorityBeta\":" << training.learner().beta()
         << ",\"batchUpdates\":" << learned.batch_updates
         << ",\"sampledReplayTransitions\":"
         << learned.sampled_transitions << ",\"targetSyncs\":"
         << learned.target_syncs << ",\"meanAbsoluteTd\":"
         << (learned.sampled_transitions > 0
                 ? learned.absolute_td_sum / learned.sampled_transitions
                 : 0.0)
         << ",\"meanHuberLoss\":"
         << (learned.sampled_transitions > 0
                 ? learned.huber_loss_sum / learned.sampled_transitions
                 : 0.0)
         << ",\"maximumAbsoluteTd\":" << learned.maximum_absolute_td
         << ",\"maximumParameterChange\":"
         << learned.maximum_parameter_change << ",\"wallSeconds\":"
         << trained.wall_seconds << ",\"environmentTransitionsPerSecond\":"
         << training_throughput << "},\n";
  writeOptionalStage(output, "stageARandomProbe", audit.random_a,
                     audit.candidate_a, audit.difference_a, audit.gate_a,
                     kRandomProbeStart, "random");
  writeOptionalStage(output, "stageBFairD1Probe", audit.fair_b,
                     audit.candidate_b, audit.difference_b, audit.gate_b,
                     kFairProbeStart, "exact corrected fair-D1");
  writeOptionalStage(output, "stageCDisjointFairD1Probe", audit.fair_c,
                     audit.candidate_c, audit.difference_c, audit.gate_c,
                     kFinalProbeStart, "exact corrected fair-D1");
  output << "  \"checkpoint\":{\"written\":"
         << (audit.checkpoint.written ? "true" : "false");
  if (audit.checkpoint.written) {
    output << ",\"lastPassingStage\":\"" << audit.checkpoint.stage
           << "\",\"path\":\"" << options.checkpoint
           << "\",\"environmentTransitions\":"
           << audit.checkpoint.environment_steps << ",\"bytes\":"
           << audit.checkpoint.bytes << ",\"fingerprintFnv1a64\":\"0x"
           << std::hex << audit.checkpoint.fingerprint << std::dec << "\"";
  }
  output << "},\n  \"implementation\":{\"inferenceStateEvaluationsPerSecond\":"
         << audit.inference.state_evaluations_per_second
         << ",\"inferenceActionValuesPerSecond\":"
         << audit.inference.action_values_per_second
         << ",\"inferenceBenchmarkStates\":"
         << audit.inference.state_evaluations
         << ",\"inferenceBenchmarkChecksum\":" << audit.inference.checksum
         << ",\"peakRssBytes\":" << fair::peakRssBytes()
         << ",\"runtimeRssLimitBytes\":" << kRuntimeRssLimit
         << ",\"totalSeconds\":" << audit.total_seconds
         << "},\n  \"nextExperimentProposal\":{\"recommended\":"
         << (proposal ? "true" : "false")
         << ",\"executed\":false,\"reason\":\""
         << (proposal
                 ? "candidate cleared all staged development gates; freeze a new formal protocol before any further seed"
                 : "candidate stopped at the first failed frozen promotion gate")
         << "\"},\n  \"conclusion\":\""
         << (proposal
                 ? "high-throughput learned Q policy merits a separately preregistered formal test; no such test was run"
                 : "high-throughput learned Q policy did not earn further scaling or deployment")
         << "\"\n}\n";
}

int run(const Options& options, std::ostream& report) {
  const auto started = Clock::now();
  const auto deadline = started + std::chrono::duration_cast<Clock::duration>(
      std::chrono::duration<double>(kWallLimitSeconds));
  TrainingRun training(deadline);
  Audit audit;

  training.trainTo(kStageATransitions, report);
  audit.random_a = evaluateCohort(kRandomProbeStart, kRandomProbeGames,
                                  PolicyKind::kRandom, nullptr);
  audit.candidate_a = evaluateCohort(
      kRandomProbeStart, kRandomProbeGames, PolicyKind::kLearned,
      &training.learner().model());
  audit.difference_a = pairedDifference(*audit.candidate_a, *audit.random_a);
  audit.gate_a =
      randomGate(*audit.candidate_a, *audit.random_a, *audit.difference_a);
  report << std::fixed << std::setprecision(3)
         << "RAINBOW_STAGE_A {\"candidateScore\":"
         << audit.candidate_a->mean_score << ",\"randomScore\":"
         << audit.random_a->mean_score << ",\"candidateMoves\":"
         << audit.candidate_a->mean_moves << ",\"randomMoves\":"
         << audit.random_a->mean_moves << ",\"scoreLower95\":"
         << audit.difference_a->lower95_score << ",\"moveLower95\":"
         << audit.difference_a->lower95_moves << ",\"passed\":"
         << (audit.gate_a->passed ? "true" : "false") << "}\n";
  if (!audit.gate_a->passed) {
    audit.status = "stopped-at-random-gate";
  } else {
    audit.checkpoint = preserveCheckpoint(options, training, "stage-a");
    training.trainTo(kStageBTransitions, report);
    audit.fair_b = evaluateCohort(kFairProbeStart, kFairProbeGames,
                                  PolicyKind::kFairD1, nullptr);
    audit.candidate_b = evaluateCohort(
        kFairProbeStart, kFairProbeGames, PolicyKind::kLearned,
        &training.learner().model());
    audit.difference_b = pairedDifference(*audit.candidate_b, *audit.fair_b);
    audit.gate_b = fairGate(*audit.candidate_b, *audit.fair_b,
                            *audit.difference_b, 1.0);
    report << "RAINBOW_STAGE_B {\"candidateScore\":"
           << audit.candidate_b->mean_score << ",\"fairD1Score\":"
           << audit.fair_b->mean_score << ",\"candidateMoves\":"
           << audit.candidate_b->mean_moves << ",\"fairD1Moves\":"
           << audit.fair_b->mean_moves << ",\"scoreLower95\":"
           << audit.difference_b->lower95_score << ",\"moveLower95\":"
           << audit.difference_b->lower95_moves << ",\"passed\":"
           << (audit.gate_b->passed ? "true" : "false") << "}\n";
    if (!audit.gate_b->passed) {
      audit.status = "stopped-at-fair-d1-gate";
    } else {
      audit.checkpoint = preserveCheckpoint(options, training, "stage-b");
      training.trainTo(kStageCTransitions, report);
      audit.fair_c = evaluateCohort(kFinalProbeStart, kFinalProbeGames,
                                    PolicyKind::kFairD1, nullptr);
      audit.candidate_c = evaluateCohort(
          kFinalProbeStart, kFinalProbeGames, PolicyKind::kLearned,
          &training.learner().model());
      audit.difference_c =
          pairedDifference(*audit.candidate_c, *audit.fair_c);
      audit.gate_c = fairGate(*audit.candidate_c, *audit.fair_c,
                              *audit.difference_c, kFinalScoreRatio);
      report << "RAINBOW_STAGE_C {\"candidateScore\":"
             << audit.candidate_c->mean_score << ",\"fairD1Score\":"
             << audit.fair_c->mean_score << ",\"candidateMoves\":"
             << audit.candidate_c->mean_moves << ",\"fairD1Moves\":"
             << audit.fair_c->mean_moves << ",\"scoreLower95\":"
             << audit.difference_c->lower95_score << ",\"moveLower95\":"
             << audit.difference_c->lower95_moves << ",\"passed\":"
             << (audit.gate_c->passed ? "true" : "false") << "}\n";
      if (audit.gate_c->passed) {
        audit.checkpoint = preserveCheckpoint(options, training, "stage-c");
        audit.status = "all-development-gates-passed-proposal-only";
      } else {
        audit.status = "stopped-at-disjoint-final-development-gate";
      }
    }
  }
  audit.inference = benchmarkInference(training.learner().model());
  audit.total_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  writeArtifact(options, training, audit);
  report << "RAINBOW_NTUPLE_Q_RESULT {\"status\":\"" << audit.status
         << "\",\"transitions\":"
         << training.learner().environmentSteps()
         << ",\"checkpointWritten\":"
         << (audit.checkpoint.written ? "true" : "false")
         << ",\"peakRssBytes\":" << fair::peakRssBytes()
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return 0;
}

bool selfTest(const Options& options, std::ostream& output) {
  const bool inherited = fair::selfTest(output);

  State level_state;
  level_state.board = initialBoard();
  level_state.next_disc = 7;
  level_state.moves_remaining = 1;
  Mulberry32 level_random(0x1234'5678u);
  MoveResult level_move;
  const bool level_played = playMove(level_state, 3, level_random, level_move);
  const bool corrected_scoring =
      level_played && level_move.level_advanced &&
      level_move.score_delta == kLevelBonus && kLevelBonus == 17'000;

  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 1)] = 3;
  state.next_disc = 4;
  state.moves_remaining = 3;
  const PublicState observation = publicState(state);
  const FeatureSet active = features(observation, 3);
  bool bounded_features = active.count == kActiveFeatures;
  for (int index = 0; index < active.count; ++index) {
    bounded_features = bounded_features && active.ids[index] < kHashBuckets;
  }

  State metadata = state;
  metadata.score = 9'999'999;
  metadata.level = 777;
  metadata.moves_played = 42'424;
  metadata.game_over = false;
  const bool metadata_blind = publicState(state) == publicState(metadata);

  Model online;
  normalizedQUpdate(online, observation, 1, 50.0f);
  normalizedQUpdate(online, observation, 5, -30.0f);
  const auto q = ensembleValues(online, observation);
  const auto mirrored_q = ensembleValues(online, mirrorState(observation));
  double reflection_gap = 0.0;
  for (int action = 0; action < kBoardSize; ++action) {
    reflection_gap = std::max(
        reflection_gap,
        std::abs(static_cast<double>(q[action] -
                                     mirrored_q[kBoardSize - 1 - action])));
  }
  PublicState masked = observation;
  for (int row = 0; row < kBoardSize; ++row) {
    masked.board[indexOf(row, 0)] = kSolid;
  }
  const auto masked_q = ensembleValues(online, masked);
  const bool masking = !std::isfinite(masked_q[0]) &&
                       isLegal(masked.board, greedyAction(online, masked));

  NstepAccumulator nstep;
  std::vector<Transition> produced;
  for (int step = 0; step < kNstep; ++step) {
    const OneStep item{observation, observation, 1.0f, 3, false};
    const auto batch = nstep.push(item);
    produced.insert(produced.end(), batch.begin(), batch.end());
  }
  double expected_reward = 0.0;
  double power = 1.0;
  for (int step = 0; step < kNstep; ++step) {
    expected_reward += power;
    power *= kGamma;
  }
  const bool nstep_exact =
      produced.size() == 1 &&
      std::abs(produced[0].reward - expected_reward) <= 1.0e-5 &&
      std::abs(produced[0].discount - power) <= 1.0e-5 &&
      produced[0].terminal == 0;
  const auto truncated = nstep.truncate();
  const bool truncation_bootstraps =
      truncated.size() == kNstep - 1 &&
      std::all_of(truncated.begin(), truncated.end(),
                  [](const Transition& value) {
                    return value.terminal == 0 && value.discount > 0.0f;
                  });
  NstepAccumulator terminal_nstep;
  terminal_nstep.push({observation, observation, 2.0f, 2, false});
  const auto terminal =
      terminal_nstep.push({observation, observation, 3.0f, 2, true});
  const bool terminal_flush =
      terminal.size() == 2 && terminal_nstep.empty() &&
      std::all_of(terminal.begin(), terminal.end(),
                  [](const Transition& value) {
                    return value.terminal != 0 && value.discount == 0.0f;
                  });

  Replay replay;
  for (int index = 0; index < 16; ++index) {
    Transition item;
    item.state = observation;
    item.next = observation;
    item.action = static_cast<std::uint8_t>(index % kBoardSize);
    replay.add(item);
    replay.update(index, static_cast<float>(index + 1));
  }
  for (int index = 16; index < 60'000; ++index) {
    Transition item;
    item.state = observation;
    item.next = observation;
    item.action = static_cast<std::uint8_t>(index % kBoardSize);
    replay.add(item);
    replay.update(index, static_cast<float>((index % 10'000) + 1));
  }
  bool replay_bounded =
      replay.size() == 60'000 && replay.totalPriority() > 0.0;
  for (int sample = 0; sample < 10'000; ++sample) {
    const float unit = static_cast<float>(
        static_cast<double>(mix32(static_cast<std::uint32_t>(sample))) /
        4'294'967'296.0);
    const int index = replay.sample(unit);
    replay_bounded = replay_bounded && index >= 0 && index < replay.size() &&
                     replay.probability(index) > 0.0f;
  }

  Model target;
  normalizedQUpdate(online, observation, 0, 100.0f);
  normalizedQUpdate(target, observation, 6, 1'000.0f);
  normalizedQUpdate(target, observation, 0, 20.0f);
  Transition double_transition;
  double_transition.state = observation;
  double_transition.next = observation;
  double_transition.action = 3;
  double_transition.reward = 2.0f;
  double_transition.discount = 0.5f;
  const auto online_next = ensembleValues(online, observation);
  const auto target_next = ensembleValues(target, observation);
  const int online_selected =
      maskedArgmax(online_next, legalMask(observation));
  const int target_selected =
      maskedArgmax(target_next, legalMask(observation));
  const float expected_double =
      double_transition.reward +
      double_transition.discount * target_next[online_selected];
  const bool double_dqn =
      online_selected != target_selected &&
      std::abs(doubleDqnTarget(online, target, double_transition) -
               expected_double) <= 1.0e-5f;

  const std::string selftest_checkpoint = options.checkpoint + ".selftest";
  writeCheckpoint(selftest_checkpoint, online, 123'456);
  auto restored = readCheckpoint(selftest_checkpoint);
  const bool checkpoint =
      restored.second == 123'456 &&
      modelFingerprint(restored.first) == modelFingerprint(online) &&
      fileBytes(selftest_checkpoint) <= kDeployedModelLimit;

  Cohort first;
  Cohort second;
  for (int game = 0; game < kRandomProbeGames; ++game) {
    const std::uint32_t synthetic_seed =
        0x1234'0000u + static_cast<std::uint32_t>(game);
    first.games.push_back(
        {synthetic_seed, 200, 100, false,
         futureDiscStreamHash(synthetic_seed, kEvaluationMaximumMoves), 0});
    second.games.push_back(
        {synthetic_seed, 100, 50, false,
         futureDiscStreamHash(synthetic_seed, kEvaluationMaximumMoves), 0});
  }
  first.mean_score = 200;
  first.mean_moves = 100;
  second.mean_score = 100;
  second.mean_moves = 50;
  const Difference paired = pairedDifference(first, second);
  const Gate positive = randomGate(first, second, paired);
  first.mean_score = 50;
  const Gate negative = randomGate(first, second, paired);
  const bool gate_wiring = positive.passed && !negative.passed &&
                           paired.streams_identical;

  const std::size_t runtime_estimate =
      2 * kHashBuckets * sizeof(float) +
      static_cast<std::size_t>(kReplayCapacity) * sizeof(Transition) +
      2 * static_cast<std::size_t>(kReplayCapacity) * sizeof(double);
  const bool resources =
      kHashBuckets * sizeof(float) < kDeployedModelLimit &&
      runtime_estimate < kRuntimeRssLimit;
  const bool seed_protocol =
      kTrainingSeedStart == 0x3d40'0000u &&
      kTrainingSeedEndExclusive == 0x3d42'0000u &&
      kRandomProbeStart == 0x4d40'0000u &&
      kFairProbeStart == 0x4d40'0020u &&
      kFinalProbeStart == 0x4d40'0040u;
  const bool protocol =
      kLevelBonus == 17'000 && kNstep == 5 && kGamma == 0.997f &&
      kStageATransitions == 250'000 && kStageBTransitions == 1'000'000 &&
      kStageCTransitions == 4'000'000 && kReplayCapacity == (1 << 17) &&
      kHashBits == 23 && seed_protocol;
  const bool passed =
      inherited && corrected_scoring && bounded_features && metadata_blind &&
      reflection_gap == 0.0 && masking && nstep_exact &&
      truncation_bootstraps && terminal_flush && replay_bounded && double_dqn &&
      checkpoint && gate_wiring && resources && protocol;
  output << std::setprecision(12)
         << "RAINBOW_NTUPLE_Q_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"inheritedFair\":" << (inherited ? "true" : "false")
         << ",\"corrected17kScoring\":"
         << (corrected_scoring ? "true" : "false")
         << ",\"boundedFeatures\":"
         << (bounded_features ? "true" : "false")
         << ",\"metadataBlind\":" << (metadata_blind ? "true" : "false")
         << ",\"reflectionGap\":" << reflection_gap
         << ",\"actionMasking\":" << (masking ? "true" : "false")
         << ",\"nStep\":" << (nstep_exact ? "true" : "false")
         << ",\"truncationBootstrap\":"
         << (truncation_bootstraps ? "true" : "false")
         << ",\"terminalFlush\":" << (terminal_flush ? "true" : "false")
         << ",\"prioritizedReplay\":"
         << (replay_bounded ? "true" : "false")
         << ",\"doubleDqn\":" << (double_dqn ? "true" : "false")
         << ",\"checkpoint\":" << (checkpoint ? "true" : "false")
         << ",\"gateWiring\":" << (gate_wiring ? "true" : "false")
         << ",\"resources\":" << (resources ? "true" : "false")
         << ",\"protocol\":" << (protocol ? "true" : "false") << "}\n";
  return passed;
}

}  // namespace drop7::rainbow_ntuple_q

#ifndef DROP7_RAINBOW_NTUPLE_Q_LIBRARY
int main(int argc, char** argv) {
  try {
    std::cout.setf(std::ios::unitbuf);
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      const auto options =
          drop7::rainbow_ntuple_q::parseOptions(argc, argv, 2);
      return drop7::rainbow_ntuple_q::selfTest(options, std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::rainbow_ntuple_q::parseOptions(argc, argv, 2);
      return drop7::rainbow_ntuple_q::run(options, std::cout);
    }
    std::cerr << "usage: drop7_rainbow_ntuple_q --self-test | --run "
                 "[--output PATH --checkpoint PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_rainbow_ntuple_q: " << error.what() << '\n';
    return 1;
  }
}
#endif
