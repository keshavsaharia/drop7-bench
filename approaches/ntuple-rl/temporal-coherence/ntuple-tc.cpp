#include "../../../src/core/native/engine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <vector>

// Learns a chance-state value with a standalone 2048-style Monte-Carlo n-tuple
// model. The value is the
// chance-state U(board, moves-until-rise), before the future numbered disc is
// known. The current visible disc still affects action selection because each
// candidate is played before U(successor) is compared.
namespace drop7::ntuple_tc {

constexpr int kAuxFeatures = 1'107;
constexpr int kAbsolute4Tables = 92;
constexpr int kAbsolute4Patterns = 10'000;
constexpr int kAbsolute5Tables = 42;
constexpr int kAbsolute5Patterns = 100'000;
constexpr int kShared5Tables = 2;
constexpr int kShared5Patterns = 100'000;
constexpr int kShared6Tables = 4;
constexpr int kShared6Patterns = 1'000'000;

constexpr std::uint32_t kAuxBase = 0;
constexpr std::uint32_t kAbsolute4Base = kAuxBase + kAuxFeatures;
constexpr std::uint32_t kAbsolute5Base =
    kAbsolute4Base + kAbsolute4Tables * kAbsolute4Patterns;
constexpr std::uint32_t kShared5Base =
    kAbsolute5Base + kAbsolute5Tables * kAbsolute5Patterns;
constexpr std::uint32_t kShared6Base =
    kShared5Base + kShared5Tables * kShared5Patterns;
constexpr std::uint32_t kNodeCount =
    kShared6Base + kShared6Tables * kShared6Patterns;
constexpr int kActiveFeatures = 338;
constexpr int kSharedActiveOccurrences = 130;
constexpr int kNonSharedActiveFeatures =
    kActiveFeatures - kSharedActiveOccurrences;
constexpr int kSharedGradientHashSlots = 256;
constexpr std::size_t kMemoryLimit = 256ull * 1024 * 1024;
// Fixed admission thresholds for extending the 10k pilot by 90k games, plus
// the exact-D4 score and move reference values used by the final comparison.
// This executable does not reread D4 validation seeds.
constexpr double kCorrectionPilotScoreGate = 100'000.0;
constexpr double kCorrectionPilotMoveGate = 70.0;
constexpr double kQualifiedD4ReferenceScore = 176'925.25;
constexpr double kQualifiedD4ReferenceMoves = 116.375;
static_assert((kSharedGradientHashSlots & (kSharedGradientHashSlots - 1)) == 0);
static_assert(kNonSharedActiveFeatures == 208);

// Auxiliary/factored feature layout.
constexpr int kCellBase = 0;
constexpr int kCellFeatureCount = kCellCount * 10;
constexpr int kPhaseBase = kCellBase + kCellFeatureCount;
constexpr int kPhaseFeatureCount = kMovesPerLevel;
constexpr int kColumnBase = kPhaseBase + kPhaseFeatureCount;
constexpr int kColumnFeatureCount = kBoardSize * (kBoardSize + 1);
constexpr int kRowBase = kColumnBase + kColumnFeatureCount;
constexpr int kRowFeatureCount = kBoardSize * (kBoardSize + 1);
constexpr int kTokenCountBase = kRowBase + kRowFeatureCount;
constexpr int kTokenCountFeatureCount = 10 * (kCellCount + 1);
static_assert(kTokenCountBase + kTokenCountFeatureCount == kAuxFeatures);

struct Node {
  float weight = 0;
  float signed_error = 0;
  float absolute_error = 0;
};
static_assert(sizeof(Node) == 12);
static_assert(static_cast<std::size_t>(kNodeCount) * sizeof(Node) <
              kMemoryLimit);

struct Rng {
  explicit Rng(std::uint32_t seed) : random(seed) {}
  std::uint32_t bits() { return random.nextBits(); }
  double unit() { return random.nextUnit(); }
  int bounded(int bound) {
    return static_cast<int>((static_cast<std::uint64_t>(bits()) * bound) >> 32);
  }
  Mulberry32 random;
};

struct CompactState {
  Board board{};
  std::uint8_t moves_remaining = kMovesPerLevel;
  std::uint8_t terminal = 0;
};

CompactState compact(const State& state) {
  return {state.board, static_cast<std::uint8_t>(state.moves_remaining),
          static_cast<std::uint8_t>(state.game_over)};
}

bool mirrorIsSmaller(const Board& board) {
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      const auto forward = board[indexOf(row, column)];
      const auto reflected = board[indexOf(row, kBoardSize - 1 - column)];
      if (reflected < forward) return true;
      if (reflected > forward) return false;
    }
  }
  return false;
}

Board mirrorBoard(const Board& source) {
  Board result{};
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      result[indexOf(row, column)] =
          source[indexOf(row, kBoardSize - 1 - column)];
    }
  }
  return result;
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

std::uint32_t observableHash(const State& state) {
  std::uint32_t hash = 0x811c'9dc5u;
  for (std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint32_t>(cell + 1);
    hash *= 0x0100'0193u;
  }
  hash ^= state.next_disc;
  hash *= 0x0100'0193u;
  hash ^= static_cast<std::uint32_t>(state.moves_remaining);
  return mix32(hash);
}

std::vector<std::uint32_t> stratifiedChanceSeeds(std::uint32_t hash,
                                                  int samples) {
  std::vector<std::uint32_t> result(samples);
  const int offset = static_cast<int>(mix32(hash ^ 0x5354'5241u) % 7u);
  for (int sample = 0; sample < samples; ++sample) {
    const std::uint8_t desired =
        static_cast<std::uint8_t>(((offset + sample) % 7) + 1);
    for (std::uint32_t attempt = 0;; ++attempt) {
      const std::uint32_t candidate = mix32(
          hash ^ (static_cast<std::uint32_t>(sample + 1) * 0xc2b2'ae35u) ^
          (attempt * 0x9e37'79b9u) ^ 0x5245'564cu);
      Mulberry32 probe(candidate);
      if (probe.nextDisc() == desired) {
        result[sample] = candidate;
        break;
      }
    }
  }
  return result;
}

int code4(std::uint8_t a, std::uint8_t b, std::uint8_t c,
          std::uint8_t d) {
  return ((a * 10 + b) * 10 + c) * 10 + d;
}

int code5(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d,
          std::uint8_t e) {
  return (((a * 10 + b) * 10 + c) * 10 + d) * 10 + e;
}

int code6(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d,
          std::uint8_t e, std::uint8_t f) {
  return ((((a * 10 + b) * 10 + c) * 10 + d) * 10 + e) * 10 + f;
}

struct FeatureSet {
  std::array<std::uint32_t, kActiveFeatures> ids{};
  int count = 0;
};

// Absolute tables use distinct parameters per translated tuple.  The shared
// tables intentionally do not: the same pattern can activate one parameter
// many times in a state.  For a linear value V = phi*w, the semi-gradient is
// that multiplicity and its normalized denominator is ||phi||^2, not the raw
// number of tuple occurrences.  The 2048 TC formula's divisor m assumes each
// active tuple refers to a distinct parameter.
struct GradientFeatures {
  std::array<std::uint32_t, kSharedGradientHashSlots> shared_ids{};
  std::array<std::uint16_t, kSharedGradientHashSlots> multiplicities{};
  int occurrences = 0;
  int unique_parameters = 0;
  int squared_norm = 0;
  int maximum_multiplicity = 0;
};

GradientFeatures gradientFeatures(const FeatureSet& active) {
  GradientFeatures result;
  result.shared_ids.fill(std::numeric_limits<std::uint32_t>::max());
  for (int index = 0; index < active.count; ++index) {
    const std::uint32_t id = active.ids[index];
    ++result.occurrences;
    if (id < kShared5Base) {
      ++result.unique_parameters;
      ++result.squared_norm;
      result.maximum_multiplicity =
          std::max(result.maximum_multiplicity, 1);
      continue;
    }
    std::uint32_t slot =
        mix32(id) & static_cast<std::uint32_t>(kSharedGradientHashSlots - 1);
    for (int probes = 0; probes < kSharedGradientHashSlots; ++probes) {
      if (result.shared_ids[slot] == id) {
        ++result.multiplicities[slot];
        break;
      }
      if (result.shared_ids[slot] ==
          std::numeric_limits<std::uint32_t>::max()) {
        result.shared_ids[slot] = id;
        result.multiplicities[slot] = 1;
        break;
      }
      slot = (slot + 1) & (kSharedGradientHashSlots - 1);
      if (probes + 1 == kSharedGradientHashSlots) {
        throw std::logic_error("shared gradient hash table is full");
      }
    }
  }
  for (int slot = 0; slot < kSharedGradientHashSlots; ++slot) {
    const int multiplicity = result.multiplicities[slot];
    if (multiplicity == 0) continue;
    ++result.unique_parameters;
    result.squared_norm += multiplicity * multiplicity;
    result.maximum_multiplicity =
        std::max(result.maximum_multiplicity, multiplicity);
  }
  if (result.occurrences != kActiveFeatures ||
      result.unique_parameters < kNonSharedActiveFeatures ||
      result.squared_norm < kActiveFeatures) {
    throw std::logic_error("shared gradient feature invariant failed");
  }
  return result;
}

FeatureSet features(const CompactState& original) {
  const CompactState state = canonicalize(original);
  const Board& board = state.board;
  FeatureSet result;
  std::array<int, 10> token_counts{};

  // Absolute token-square features plus low-dimensional row, column, phase,
  // and global-count factors.
  for (int index = 0; index < kCellCount; ++index) {
    const int token = board[index];
    result.ids[result.count++] = kAuxBase + kCellBase + index * 10 + token;
    ++token_counts[token];
  }
  result.ids[result.count++] =
      kAuxBase + kPhaseBase +
      std::clamp<int>(state.moves_remaining, 1, kMovesPerLevel) - 1;
  for (int column = 0; column < kBoardSize; ++column) {
    int occupied = 0;
    for (int row = 0; row < kBoardSize; ++row) {
      occupied += board[indexOf(row, column)] != kEmpty;
    }
    result.ids[result.count++] =
        kAuxBase + kColumnBase + column * (kBoardSize + 1) + occupied;
  }
  for (int row = 0; row < kBoardSize; ++row) {
    int occupied = 0;
    for (int column = 0; column < kBoardSize; ++column) {
      occupied += board[indexOf(row, column)] != kEmpty;
    }
    result.ids[result.count++] =
        kAuxBase + kRowBase + row * (kBoardSize + 1) + occupied;
  }
  for (int token = 0; token < 10; ++token) {
    result.ids[result.count++] = kAuxBase + kTokenCountBase +
                                 token * (kCellCount + 1) +
                                 token_counts[token];
  }

  // Every translated H4, V4, and 2x2 has its own absolute table.
  int table = 0;
  for (int row = 0; row < kBoardSize; ++row) {
    for (int start = 0; start <= kBoardSize - 4; ++start, ++table) {
      const int pattern = code4(board[indexOf(row, start)],
                                board[indexOf(row, start + 1)],
                                board[indexOf(row, start + 2)],
                                board[indexOf(row, start + 3)]);
      result.ids[result.count++] =
          kAbsolute4Base + table * kAbsolute4Patterns + pattern;
    }
  }
  for (int column = 0; column < kBoardSize; ++column) {
    for (int start = 0; start <= kBoardSize - 4; ++start, ++table) {
      const int pattern = code4(board[indexOf(start, column)],
                                board[indexOf(start + 1, column)],
                                board[indexOf(start + 2, column)],
                                board[indexOf(start + 3, column)]);
      result.ids[result.count++] =
          kAbsolute4Base + table * kAbsolute4Patterns + pattern;
    }
  }
  for (int row = 0; row < kBoardSize - 1; ++row) {
    for (int column = 0; column < kBoardSize - 1; ++column, ++table) {
      const int pattern = code4(board[indexOf(row, column)],
                                board[indexOf(row, column + 1)],
                                board[indexOf(row + 1, column)],
                                board[indexOf(row + 1, column + 1)]);
      result.ids[result.count++] =
          kAbsolute4Base + table * kAbsolute4Patterns + pattern;
    }
  }
  if (table != kAbsolute4Tables) {
    throw std::logic_error("absolute four-tuple table invariant failed");
  }

  // Straight five-tuples are represented both by absolute tables and by two
  // translation-shared tables. This lets frequently seen patterns generalize
  // while retaining the strong edge/height asymmetry of Drop7.
  table = 0;
  for (int row = 0; row < kBoardSize; ++row) {
    for (int start = 0; start <= kBoardSize - 5; ++start, ++table) {
      const int pattern = code5(
          board[indexOf(row, start)], board[indexOf(row, start + 1)],
          board[indexOf(row, start + 2)], board[indexOf(row, start + 3)],
          board[indexOf(row, start + 4)]);
      result.ids[result.count++] =
          kAbsolute5Base + table * kAbsolute5Patterns + pattern;
      result.ids[result.count++] = kShared5Base + pattern;
    }
  }
  for (int column = 0; column < kBoardSize; ++column) {
    for (int start = 0; start <= kBoardSize - 5; ++start, ++table) {
      const int pattern = code5(
          board[indexOf(start, column)], board[indexOf(start + 1, column)],
          board[indexOf(start + 2, column)], board[indexOf(start + 3, column)],
          board[indexOf(start + 4, column)]);
      result.ids[result.count++] =
          kAbsolute5Base + table * kAbsolute5Patterns + pattern;
      result.ids[result.count++] =
          kShared5Base + kShared5Patterns + pattern;
    }
  }
  if (table != kAbsolute5Tables) {
    throw std::logic_error("absolute five-tuple table invariant failed");
  }

  // Collision-free shared six-tuples cover long rows/columns and full local
  // 2x3 rectangles, adding long-range interactions beyond the four-tuples.
  for (int row = 0; row < kBoardSize; ++row) {
    for (int start = 0; start <= kBoardSize - 6; ++start) {
      const int pattern = code6(
          board[indexOf(row, start)], board[indexOf(row, start + 1)],
          board[indexOf(row, start + 2)], board[indexOf(row, start + 3)],
          board[indexOf(row, start + 4)], board[indexOf(row, start + 5)]);
      result.ids[result.count++] = kShared6Base + pattern;
    }
  }
  for (int column = 0; column < kBoardSize; ++column) {
    for (int start = 0; start <= kBoardSize - 6; ++start) {
      const int pattern = code6(
          board[indexOf(start, column)], board[indexOf(start + 1, column)],
          board[indexOf(start + 2, column)], board[indexOf(start + 3, column)],
          board[indexOf(start + 4, column)], board[indexOf(start + 5, column)]);
      result.ids[result.count++] =
          kShared6Base + kShared6Patterns + pattern;
    }
  }
  for (int row = 0; row <= kBoardSize - 2; ++row) {
    for (int column = 0; column <= kBoardSize - 3; ++column) {
      const int pattern = code6(
          board[indexOf(row, column)], board[indexOf(row, column + 1)],
          board[indexOf(row, column + 2)], board[indexOf(row + 1, column)],
          board[indexOf(row + 1, column + 1)],
          board[indexOf(row + 1, column + 2)]);
      result.ids[result.count++] =
          kShared6Base + 2 * kShared6Patterns + pattern;
    }
  }
  for (int row = 0; row <= kBoardSize - 3; ++row) {
    for (int column = 0; column <= kBoardSize - 2; ++column) {
      const int pattern = code6(
          board[indexOf(row, column)], board[indexOf(row, column + 1)],
          board[indexOf(row + 1, column)],
          board[indexOf(row + 1, column + 1)],
          board[indexOf(row + 2, column)],
          board[indexOf(row + 2, column + 1)]);
      result.ids[result.count++] =
          kShared6Base + 3 * kShared6Patterns + pattern;
    }
  }
  if (result.count != kActiveFeatures) {
    throw std::logic_error("active n-tuple feature invariant failed");
  }
  return result;
}

struct UpdateStats {
  double beta_sum = 0;
  std::uint64_t node_updates = 0;
  double absolute_delta_sum = 0;
  std::uint64_t state_updates = 0;
};

class Model {
 public:
  explicit Model(float optimistic_value = 60.0f)
      : nodes_(kNodeCount) {
    const float initial = optimistic_value / kActiveFeatures;
    for (Node& node : nodes_) node.weight = initial;
  }

  float value(const CompactState& state) const {
    if (state.terminal) return 0;
    const auto active = features(state);
    double result = 0;
    for (std::uint32_t id : active.ids) result += nodes_[id].weight;
    return static_cast<float>(result);
  }
  float value(const State& state) const { return value(compact(state)); }

  float update(const CompactState& state, float target, float learning_rate,
               UpdateStats& stats) {
    const auto active = features(state);
    const auto gradient = gradientFeatures(active);
    const float prediction = valueFromFeatures(active);
    const float delta = std::clamp(target - prediction, -200.0f, 200.0f);
    const float normalized =
        learning_rate * delta / static_cast<float>(gradient.squared_norm);
    const auto update_node = [&](std::uint32_t id, int multiplicity) {
      Node& node = nodes_[id];
      // TC computes its step size from the history that preceded this sample;
      // E and A are updated only after applying the current error.
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
    for (int index = 0; index < active.count; ++index) {
      const std::uint32_t id = active.ids[index];
      if (id < kShared5Base) update_node(id, 1);
    }
    for (int slot = 0; slot < kSharedGradientHashSlots; ++slot) {
      const int multiplicity = gradient.multiplicities[slot];
      if (multiplicity > 0) {
        update_node(gradient.shared_ids[slot], multiplicity);
      }
    }
    stats.absolute_delta_sum += std::abs(delta);
    ++stats.state_updates;
    return delta;
  }

  std::size_t bytes() const { return nodes_.size() * sizeof(Node); }

  void save(const std::string& path) const {
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("could not open TC checkpoint");
    constexpr std::array<char, 8> magic{{'D', '7', 'T', 'C', 'N', 'T', '1', 0}};
    const std::uint32_t count = kNodeCount;
    output.write(magic.data(), magic.size());
    output.write(reinterpret_cast<const char*>(&count), sizeof(count));
    output.write(reinterpret_cast<const char*>(nodes_.data()),
                 static_cast<std::streamsize>(nodes_.size() * sizeof(Node)));
    if (!output) throw std::runtime_error("failed writing TC checkpoint");
  }

  void load(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not open TC checkpoint");
    std::array<char, 8> magic{};
    std::uint32_t count = 0;
    input.read(magic.data(), magic.size());
    input.read(reinterpret_cast<char*>(&count), sizeof(count));
    constexpr std::array<char, 8> expected{{'D', '7', 'T', 'C', 'N', 'T', '1', 0}};
    if (magic != expected || count != kNodeCount) {
      throw std::runtime_error("incompatible TC checkpoint");
    }
    input.read(reinterpret_cast<char*>(nodes_.data()),
               static_cast<std::streamsize>(nodes_.size() * sizeof(Node)));
    if (!input) throw std::runtime_error("truncated TC checkpoint");
  }

 private:
  float valueFromFeatures(const FeatureSet& active) const {
    double result = 0;
    for (std::uint32_t id : active.ids) result += nodes_[id].weight;
    return static_cast<float>(result);
  }
  std::vector<Node> nodes_;
};

struct Options {
  int training_games = 10'000;
  int probe_games = 64;
  int max_moves = 1'000;
  int chance_samples = 7;
  int report_every = 1'000;
  int checkpoint_every = 0;
  std::uint32_t training_seed_start = 0x3d70'0000u;
  std::uint32_t probe_seed_start = 0x4d70'0000u;
  float gamma = 1.0f;
  float learning_rate = 0.1f;
  float optimistic_value = 60.0f;
  float epsilon = 0.0f;
  bool td_zero = false;
  bool score_reward = false;
  float score_scale = 1'000.0f;
  std::string checkpoint;
  std::string resume;
};

float transitionReward(const MoveResult& move, const Options& options) {
  return options.score_reward
             ? static_cast<float>(move.score_delta) / options.score_scale
             : 1.0f;
}

std::array<float, kBoardSize> actionValues(const Model& model,
                                            const State& source,
                                            const Options& options) {
  const auto canonical = canonicalize(source);
  const State& state = canonical.state;
  const auto chance_seeds =
      stratifiedChanceSeeds(observableHash(state), options.chance_samples);
  std::array<float, kBoardSize> physical_values{};
  physical_values.fill(-std::numeric_limits<float>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    if (!isLegal(state.board, action)) continue;
    double total = 0;
    for (int sample = 0; sample < options.chance_samples; ++sample) {
      Mulberry32 chance(chance_seeds[sample]);
      MoveResult move;
      if (!playMove(state, action, chance, move)) {
        throw std::logic_error("TC tuple evaluator chose an illegal move");
      }
      const double reward = transitionReward(move, options);
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
    physical_values[physicalAction(action, canonical.mirrored)] =
        static_cast<float>(total / options.chance_samples);
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

struct Evaluation {
  double mean_score = 0;
  double mean_moves = 0;
  std::int64_t minimum_score = std::numeric_limits<std::int64_t>::max();
  std::int64_t maximum_score = std::numeric_limits<std::int64_t>::min();
  int minimum_moves = std::numeric_limits<int>::max();
  int maximum_moves = std::numeric_limits<int>::min();
  int censored = 0;
};

Evaluation evaluate(const Model& model, const Options& options) {
  Evaluation result;
  for (int game = 0; game < options.probe_games; ++game) {
    const std::uint32_t seed =
        options.probe_seed_start + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < options.max_moves) {
      const int action = greedyAction(model, state, options);
      MoveResult move;
      if (action < 0 || !playHeadlessMove(state, seed, action, move)) {
        throw std::logic_error("TC tuple probe chose an illegal move");
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

long peakRssKiB() {
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
#if defined(__APPLE__)
  return usage.ru_maxrss / 1024;
#else
  return usage.ru_maxrss;
#endif
}

void printProbe(const Evaluation& result, int games, std::uint64_t transitions,
                std::uint64_t state_updates, double elapsed,
                const Model& model) {
  std::cout << std::fixed << std::setprecision(3)
            << "TC_NTUPLE_PROBE {\"trainingGames\":" << games
            << ",\"transitions\":" << transitions
            << ",\"meanScore\":" << result.mean_score
            << ",\"meanMoves\":" << result.mean_moves
            << ",\"minimumScore\":" << result.minimum_score
            << ",\"maximumScore\":" << result.maximum_score
            << ",\"minimumMoves\":" << result.minimum_moves
            << ",\"maximumMoves\":" << result.maximum_moves
            << ",\"censored\":" << result.censored
            << ",\"transitionsPerSecond\":"
            << (elapsed > 0 ? transitions / elapsed : 0)
            << ",\"updatesPerSecond\":"
            << (elapsed > 0 ? state_updates / elapsed : 0)
            << ",\"parameterMiB\":" << model.bytes() / 1'048'576.0
            << ",\"peakRssMiB\":" << peakRssKiB() / 1024.0
            << ",\"continueGate\":"
            << (result.mean_score >= 300'000 ? "true" : "false")
            << ",\"collisionCorrectionPilotGate\":"
            << (result.mean_score >= kCorrectionPilotScoreGate &&
                        result.mean_moves >= kCorrectionPilotMoveGate
                    ? "true"
                    : "false")
            << ",\"beatsQualifiedD4Reference\":"
            << (result.mean_score >= kQualifiedD4ReferenceScore &&
                        result.mean_moves >= kQualifiedD4ReferenceMoves
                    ? "true"
                    : "false")
            << "}\n";
}

float discountedReturn(int steps, float gamma) {
  if (std::abs(gamma - 1.0f) < 1e-7f) return static_cast<float>(steps);
  return (1.0f - std::pow(gamma, static_cast<float>(steps))) / (1.0f - gamma);
}

int train(const Options& options) {
  if (options.training_games < 1 || options.probe_games < 1 ||
      options.max_moves < 1 || options.chance_samples < 1 ||
      options.report_every < 1 || options.gamma <= 0 || options.gamma > 1 ||
      options.learning_rate <= 0 || options.optimistic_value < 0 ||
      options.epsilon < 0 || options.epsilon > 1 ||
      options.score_scale <= 0 || (options.score_reward && !options.td_zero)) {
    throw std::invalid_argument("invalid TC n-tuple options");
  }
  // Training remains in the development family. Previously evaluated 0x4d
  // probes remain readable for exact reproduction, while new probes stay in a
  // disjoint 0x3d subrange.
  const std::uint64_t training_end =
      static_cast<std::uint64_t>(options.training_seed_start) +
      options.training_games - 1;
  const std::uint64_t probe_end =
      static_cast<std::uint64_t>(options.probe_seed_start) +
      options.probe_games - 1;
  const bool training_family = options.training_seed_start >= 0x3d00'0000u &&
                               training_end < 0x3e00'0000ull;
  const bool new_probe_family = options.probe_seed_start >= 0x3d00'0000u &&
                                probe_end < 0x3e00'0000ull;
  const bool historical_probe = options.probe_seed_start >= 0x4d70'0000u &&
                                probe_end < 0x5d70'0000ull;
  const bool overlaps = new_probe_family &&
                        !(probe_end < options.training_seed_start ||
                          training_end < options.probe_seed_start);
  if (!training_family || (!new_probe_family && !historical_probe) ||
      overlaps) {
    throw std::invalid_argument(
        "seed ranges must be disjoint 0x3d development ranges or the "
        "historical 0x4d probe");
  }

  Model model(options.optimistic_value);
  if (!options.resume.empty()) model.load(options.resume);
  UpdateStats stats;
  std::uint64_t transitions = 0;
  int skipped_censored = 0;
  const auto started = std::chrono::steady_clock::now();
  std::cout << "TC_NTUPLE_CONFIG {\"trainingSeedStart\":"
            << options.training_seed_start << ",\"probeSeedStart\":"
            << options.probe_seed_start << ",\"games\":"
            << options.training_games << ",\"chanceSamples\":"
            << options.chance_samples << ",\"gamma\":" << options.gamma
            << ",\"learningRate\":" << options.learning_rate
            << ",\"optimisticValue\":" << options.optimistic_value
            << ",\"epsilon\":" << options.epsilon
            << ",\"updateTarget\":\""
            << (options.td_zero ? "td0" : "terminal-monte-carlo") << "\""
            << ",\"reward\":\""
            << (options.score_reward ? "score-delta" : "one-per-move")
            << "\",\"scoreScale\":" << options.score_scale
            << ",\"nodes\":" << kNodeCount
            << ",\"activeFeatures\":" << kActiveFeatures
            << ",\"absolute4Tables\":" << kAbsolute4Tables
            << ",\"absolute5Tables\":" << kAbsolute5Tables
            << ",\"shared5Tables\":" << kShared5Tables
            << ",\"shared6Tables\":" << kShared6Tables
            << ",\"collisionFree\":true,\"temporalCoherence\":true,"
               "\"discIndependentChanceState\":true}\n";
  printProbe(evaluate(model, options), 0, 0, 0, 0, model);

  for (int game = 0; game < options.training_games; ++game) {
    const std::uint32_t seed =
        options.training_seed_start + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    Rng exploration(mix32(seed ^ 0x4558'504cu));
    std::vector<CompactState> trajectory;
    if (!options.td_zero) trajectory.reserve(options.max_moves);
    while (!state.game_over && state.moves_played < options.max_moves) {
      const CompactState previous = compact(state);
      if (!options.td_zero) trajectory.push_back(previous);
      int action = greedyAction(model, state, options);
      if (exploration.unit() < options.epsilon) {
        int legal_count = 0;
        const auto legal = legalColumns(state.board, legal_count);
        action = legal[exploration.bounded(legal_count)];
      }
      MoveResult move;
      if (action < 0 || !playHeadlessMove(state, seed, action, move)) {
        throw std::logic_error("TC tuple training chose an illegal move");
      }
      ++transitions;
      if (options.td_zero) {
        const float reward = transitionReward(move, options);
        const float target =
            reward + (state.game_over
                          ? 0.0f
                          : options.gamma * model.value(compact(state)));
        model.update(previous, target, options.learning_rate, stats);
      }
    }
    if (!options.td_zero && state.game_over) {
      // Full terminal episodic returns. Updating backwards gives every state
      // the exact realized survival target without a bootstrap network.
      for (int index = static_cast<int>(trajectory.size()) - 1; index >= 0;
           --index) {
        const int remaining = static_cast<int>(trajectory.size()) - index;
        model.update(trajectory[index], discountedReturn(remaining, options.gamma),
                     options.learning_rate, stats);
      }
    } else if (!options.td_zero) {
      ++skipped_censored;
    }

    const int completed = game + 1;
    if (completed % options.report_every == 0 ||
        completed == options.training_games) {
      const double elapsed = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
      std::cout << "TC_NTUPLE_TRAIN {\"trainingGames\":" << completed
                << ",\"meanAbsDelta\":"
                << (stats.state_updates
                        ? stats.absolute_delta_sum / stats.state_updates
                        : 0)
                << ",\"meanTcBeta\":"
                << (stats.node_updates ? stats.beta_sum / stats.node_updates : 0)
                << ",\"stateUpdates\":" << stats.state_updates
                << ",\"skippedCensored\":" << skipped_censored << "}\n";
      printProbe(evaluate(model, options), completed, transitions,
                 stats.state_updates, elapsed, model);
      if (!options.checkpoint.empty() && options.checkpoint_every > 0 &&
          completed % options.checkpoint_every == 0) {
        model.save(options.checkpoint);
      }
    }
  }
  if (!options.checkpoint.empty()) model.save(options.checkpoint);
  return 0;
}

int evaluateCheckpoint(const Options& options) {
  if (options.resume.empty()) {
    throw std::invalid_argument("--evaluate requires --resume CHECKPOINT");
  }
  if (options.probe_games < 1 || options.max_moves < 1 ||
      options.chance_samples < 1) {
    throw std::invalid_argument("invalid TC n-tuple evaluation option");
  }
  const std::uint64_t probe_end =
      static_cast<std::uint64_t>(options.probe_seed_start) +
      options.probe_games - 1;
  if (options.probe_seed_start < 0x3d00'0000u ||
      probe_end >= 0x3e00'0000ull) {
    throw std::invalid_argument(
        "checkpoint evaluation is restricted to the 0x3d training family");
  }
  Model model(options.optimistic_value);
  model.load(options.resume);
  printProbe(evaluate(model, options), 0, 0, 0, 0, model);
  return 0;
}

bool exhaustiveIndexTest() {
  // Exhaust every legal table/pattern index, not merely observed boards.
  for (int index = 0; index < kAuxFeatures; ++index) {
    if (kAuxBase + index >= kAbsolute4Base) return false;
  }
  for (int table = 0; table < kAbsolute4Tables; ++table) {
    for (int pattern = 0; pattern < kAbsolute4Patterns; ++pattern) {
      const std::uint32_t id =
          kAbsolute4Base + table * kAbsolute4Patterns + pattern;
      if (id < kAbsolute4Base || id >= kAbsolute5Base) return false;
    }
  }
  for (int table = 0; table < kAbsolute5Tables; ++table) {
    for (int pattern = 0; pattern < kAbsolute5Patterns; ++pattern) {
      const std::uint32_t id =
          kAbsolute5Base + table * kAbsolute5Patterns + pattern;
      if (id < kAbsolute5Base || id >= kShared5Base) return false;
    }
  }
  for (int table = 0; table < kShared5Tables; ++table) {
    for (int pattern = 0; pattern < kShared5Patterns; ++pattern) {
      const std::uint32_t id =
          kShared5Base + table * kShared5Patterns + pattern;
      if (id < kShared5Base || id >= kShared6Base) return false;
    }
  }
  for (int table = 0; table < kShared6Tables; ++table) {
    for (int pattern = 0; pattern < kShared6Patterns; ++pattern) {
      const std::uint32_t id =
          kShared6Base + table * kShared6Patterns + pattern;
      if (id < kShared6Base || id >= kNodeCount) return false;
    }
  }
  return true;
}

bool selfTest(std::ostream& output) {
  const bool exhaustive_indices = exhaustiveIndexTest();
  Model model(60.0f);
  Options options;
  State state = initialHeadlessState(0x2d70'0042u);
  for (int action : {3, 1, 5, 2, 4, 0}) {
    MoveResult move;
    if (!playHeadlessMove(state, 0x2d70'0042u, action, move)) break;
  }
  State mirrored = state;
  mirrored.board = mirrorBoard(state.board);
  const float value = model.value(state);
  const float mirror_value = model.value(mirrored);
  const auto action_values = actionValues(model, state, options);
  const auto mirror_values = actionValues(model, mirrored, options);
  const auto repeated = actionValues(model, state, options);
  bool action_mirror = true;
  for (int column = 0; column < kBoardSize; ++column) {
    const float left = action_values[column];
    const float right = mirror_values[kBoardSize - 1 - column];
    if (std::isfinite(left) != std::isfinite(right) ||
        (std::isfinite(left) && std::abs(left - right) > 1e-5f)) {
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
  const auto seeds = stratifiedChanceSeeds(observableHash(state), 7);
  std::array<int, 8> reveal_counts{};
  for (std::uint32_t seed : seeds) {
    Mulberry32 random(seed);
    ++reveal_counts[random.nextDisc()];
  }
  bool stratified = true;
  for (int disc = 1; disc <= 7; ++disc) stratified &= reveal_counts[disc] == 1;
  const bool memory_bounded = model.bytes() < kMemoryLimit;
  MoveResult reward_fixture;
  reward_fixture.score_delta = 7'000;
  Options lifetime_options;
  Options score_options;
  score_options.score_reward = true;
  score_options.score_scale = 1'000.0f;
  const bool reward_modes =
      transitionReward(reward_fixture, lifetime_options) == 1.0f &&
      transitionReward(reward_fixture, score_options) == 7.0f;
  UpdateStats update_stats;
  const CompactState compact_state = compact(state);
  const float before_update = model.value(compact_state);
  model.update(compact_state, before_update + 1.0f, 0.1f, update_stats);
  const bool td_update = model.value(compact_state) > before_update &&
                         model.value(compact_state) == model.value(mirrored) &&
                         update_stats.state_updates == 1;
  const CompactState initial_compact =
      compact(initialHeadlessState(0x2d70'1234u));
  const GradientFeatures initial_gradient =
      gradientFeatures(features(initial_compact));
  const bool shared_gradient =
      initial_gradient.occurrences == kActiveFeatures &&
      initial_gradient.unique_parameters < kActiveFeatures &&
      initial_gradient.squared_norm > kActiveFeatures &&
      initial_gradient.maximum_multiplicity > 1;
  Model gradient_model(60.0f);
  UpdateStats first_gradient_stats;
  UpdateStats second_gradient_stats;
  UpdateStats third_gradient_stats;
  const float gradient_before = gradient_model.value(initial_compact);
  gradient_model.update(initial_compact, gradient_before + 10.0f, 0.1f,
                        first_gradient_stats);
  const float gradient_after_positive = gradient_model.value(initial_compact);
  gradient_model.update(initial_compact, gradient_after_positive - 10.0f,
                        0.1f, second_gradient_stats);
  const float gradient_after_reversal = gradient_model.value(initial_compact);
  gradient_model.update(initial_compact, gradient_after_reversal - 10.0f,
                        0.1f, third_gradient_stats);
  const float gradient_after_cancelled = gradient_model.value(initial_compact);
  const double first_beta =
      first_gradient_stats.beta_sum / first_gradient_stats.node_updates;
  const double second_beta =
      second_gradient_stats.beta_sum / second_gradient_stats.node_updates;
  const double third_beta =
      third_gradient_stats.beta_sum / third_gradient_stats.node_updates;
  const bool normalized_gradient =
      std::abs((gradient_after_positive - gradient_before) - 1.0f) < 1.0e-3f &&
      std::abs((gradient_after_reversal - gradient_after_positive) + 1.0f) <
          1.0e-3f &&
      gradient_after_cancelled == gradient_after_reversal &&
      first_gradient_stats.node_updates ==
          static_cast<std::uint64_t>(initial_gradient.unique_parameters) &&
      first_beta == 1.0 && second_beta == 1.0 && third_beta == 0.0;
  const bool passed = exhaustive_indices &&
                      std::abs(value - mirror_value) < 1e-6f && action_mirror &&
                      deterministic && mapped_tie && disc_independent &&
                      stratified && memory_bounded && reward_modes &&
                      td_update && shared_gradient && normalized_gradient;
  output << "TC_NTUPLE_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"exhaustiveIndices\":"
         << (exhaustive_indices ? "true" : "false")
         << ",\"mirrorValue\":"
         << (std::abs(value - mirror_value) < 1e-6f ? "true" : "false")
         << ",\"mirrorActions\":" << (action_mirror ? "true" : "false")
         << ",\"mappedTieBehavior\":" << (mapped_tie ? "true" : "false")
         << ",\"seedBlindDeterministic\":"
         << (deterministic ? "true" : "false")
         << ",\"discIndependent\":" << (disc_independent ? "true" : "false")
         << ",\"firstRevealStratified\":"
         << (stratified ? "true" : "false")
         << ",\"memoryBounded\":" << (memory_bounded ? "true" : "false")
         << ",\"rewardModes\":" << (reward_modes ? "true" : "false")
         << ",\"tdUpdate\":" << (td_update ? "true" : "false")
         << ",\"sharedGradient\":"
         << (shared_gradient ? "true" : "false")
         << ",\"normalizedGradient\":"
         << (normalized_gradient ? "true" : "false")
         << ",\"initialUniqueParameters\":"
         << initial_gradient.unique_parameters
         << ",\"initialSquaredNorm\":" << initial_gradient.squared_norm
         << ",\"initialMaxMultiplicity\":"
         << initial_gradient.maximum_multiplicity
         << ",\"legacyStepAmplification\":"
         << static_cast<double>(initial_gradient.squared_norm) /
                kActiveFeatures
         << ",\"nodes\":" << kNodeCount
         << ",\"activeFeatures\":" << kActiveFeatures
         << ",\"parameterMiB\":" << model.bytes() / 1'048'576.0 << "}\n";
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

std::uint32_t parseUint32(const std::string& value, const char* name) {
  std::size_t consumed = 0;
  const unsigned long parsed = std::stoul(value, &consumed, 0);
  if (consumed != value.size() ||
      parsed > std::numeric_limits<std::uint32_t>::max()) {
    throw std::invalid_argument(std::string("invalid ") + name);
  }
  return static_cast<std::uint32_t>(parsed);
}

float parseFloat(const std::string& value, const char* name) {
  std::size_t consumed = 0;
  const float parsed = std::stof(value, &consumed);
  if (consumed != value.size() || !std::isfinite(parsed)) {
    throw std::invalid_argument(std::string("invalid ") + name);
  }
  return parsed;
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
  options.report_every = parseInt(
      valueAfter(argc, argv, "--report-every", "1000"), "--report-every");
  options.checkpoint_every = parseInt(
      valueAfter(argc, argv, "--checkpoint-every", "0"),
      "--checkpoint-every");
  options.training_seed_start = parseUint32(
      valueAfter(argc, argv, "--training-seed-start", "0x3d700000"),
      "--training-seed-start");
  options.probe_seed_start = parseUint32(
      valueAfter(argc, argv, "--probe-seed-start", "0x4d700000"),
      "--probe-seed-start");
  options.gamma =
      parseFloat(valueAfter(argc, argv, "--gamma", "1"), "--gamma");
  options.learning_rate = parseFloat(
      valueAfter(argc, argv, "--learning-rate", "0.1"), "--learning-rate");
  options.optimistic_value = parseFloat(
      valueAfter(argc, argv, "--optimistic-value", "60"),
      "--optimistic-value");
  options.epsilon =
      parseFloat(valueAfter(argc, argv, "--epsilon", "0"), "--epsilon");
  const std::string update_target =
      valueAfter(argc, argv, "--update-target", "terminal-monte-carlo");
  if (update_target == "td0") options.td_zero = true;
  else if (update_target != "terminal-monte-carlo") {
    throw std::invalid_argument(
        "--update-target must be terminal-monte-carlo or td0");
  }
  const std::string reward =
      valueAfter(argc, argv, "--reward", "one-per-move");
  if (reward == "score-delta") options.score_reward = true;
  else if (reward != "one-per-move") {
    throw std::invalid_argument(
        "--reward must be one-per-move or score-delta");
  }
  options.score_scale = parseFloat(
      valueAfter(argc, argv, "--score-scale", "1000"), "--score-scale");
  options.checkpoint = valueAfter(argc, argv, "--checkpoint", "");
  options.resume = valueAfter(argc, argv, "--resume", "");
  return options;
}

}  // namespace drop7::ntuple_tc

#ifndef DROP7_NTUPLE_TC_LIBRARY
int main(int argc, char** argv) {
  try {
    std::cout.setf(std::ios::unitbuf);
    if (argc < 2) {
      std::cerr << "usage: drop7_ntuple_tc --self-test | --train [options] | "
                   "--evaluate --resume CHECKPOINT [options]\n";
      return 2;
    }
    const std::string mode = argv[1];
    if (mode == "--self-test") {
      return drop7::ntuple_tc::selfTest(std::cout) ? 0 : 1;
    }
    if (mode == "--train") {
      return drop7::ntuple_tc::train(
          drop7::ntuple_tc::parseOptions(argc, argv));
    }
    if (mode == "--evaluate") {
      return drop7::ntuple_tc::evaluateCheckpoint(
          drop7::ntuple_tc::parseOptions(argc, argv));
    }
    throw std::invalid_argument("unknown mode: " + mode);
  } catch (const std::exception& error) {
    std::cerr << "drop7_ntuple_tc: " << error.what() << '\n';
    return 1;
  }
}
#endif
