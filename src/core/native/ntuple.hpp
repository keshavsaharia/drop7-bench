#pragma once

#include "engine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <vector>

namespace drop7::ntuple {

// Four horizontal groups, four vertical groups, and nine 2x2 groups. Every
// translated placement on the 7x7 board is active; placements share the exact
// dense table selected by their offset group. Each key also includes nextDisc.
constexpr int kTables = 17;
constexpr int kActiveFeatures = 28 + 28 + 36;
constexpr int kAbsoluteTables = kActiveFeatures;
constexpr int kPatterns = 10'000;
constexpr int kDiscValues = 7;
constexpr int kRisePhases = kMovesPerLevel;
constexpr int kVisibleTableEntries =
    kTables * kRisePhases * kDiscValues * kPatterns;
constexpr int kChanceTableEntries = kTables * kRisePhases * kPatterns;

struct CanonicalState {
  State state{};
  bool mirrored = false;
};

inline bool mirrorIsSmaller(const Board& board) {
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

inline CanonicalState canonicalize(const State& source) {
  CanonicalState result{source, mirrorIsSmaller(source.board)};
  if (!result.mirrored) return result;
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      result.state.board[indexOf(row, column)] =
          source.board[indexOf(row, kBoardSize - 1 - column)];
    }
  }
  return result;
}

inline int physicalAction(int canonical_action, bool mirrored) {
  return mirrored ? kBoardSize - 1 - canonical_action : canonical_action;
}

inline std::uint32_t observableHash(const State& canonical) {
  std::uint32_t hash = 0x811c'9dc5u;
  for (std::uint8_t cell : canonical.board) {
    hash ^= cell + 1u;
    hash *= 0x0100'0193u;
  }
  hash ^= canonical.next_disc;
  hash *= 0x0100'0193u;
  hash ^= static_cast<std::uint32_t>(canonical.moves_remaining);
  hash *= 0x0100'0193u;
  hash ^= static_cast<std::uint32_t>(canonical.level);
  return mix32(hash);
}

inline int patternCode(std::uint8_t first, std::uint8_t second,
                       std::uint8_t third, std::uint8_t fourth) {
  return ((first * 10 + second) * 10 + third) * 10 + fourth;
}

inline int featureIndex(int table, int moves_remaining,
                        std::uint8_t next_disc, int pattern,
                        bool disc_independent) {
  if (disc_independent) {
    return ((table * kRisePhases + moves_remaining - 1) * kPatterns) + pattern;
  }
  return ((((table * kRisePhases) + moves_remaining - 1) * kDiscValues +
           next_disc - 1) *
          kPatterns) +
         pattern;
}

inline std::array<int, kActiveFeatures> features(const State& source,
                                                 bool disc_independent,
                                                 bool absolute_position) {
  const State canonical = canonicalize(source).state;
  std::array<int, kActiveFeatures> result{};
  int count = 0;
  for (int row = 0; row < kBoardSize; ++row) {
    for (int start = 0; start <= kBoardSize - 4; ++start) {
      const int table = absolute_position ? count : start;
      result[count++] = featureIndex(
          table, canonical.moves_remaining, canonical.next_disc,
          patternCode(canonical.board[indexOf(row, start)],
                      canonical.board[indexOf(row, start + 1)],
                      canonical.board[indexOf(row, start + 2)],
                      canonical.board[indexOf(row, start + 3)]),
          disc_independent);
    }
  }
  for (int column = 0; column < kBoardSize; ++column) {
    for (int start = 0; start <= kBoardSize - 4; ++start) {
      const int table = absolute_position ? count : 4 + start;
      result[count++] = featureIndex(
          table, canonical.moves_remaining, canonical.next_disc,
          patternCode(canonical.board[indexOf(start, column)],
                      canonical.board[indexOf(start + 1, column)],
                      canonical.board[indexOf(start + 2, column)],
                      canonical.board[indexOf(start + 3, column)]),
          disc_independent);
    }
  }
  for (int row = 0; row < kBoardSize - 1; ++row) {
    for (int column = 0; column < kBoardSize - 1; ++column) {
      const int table = absolute_position
                            ? count
                            : 8 + (row % 3) * 3 + (column % 3);
      result[count++] = featureIndex(
          table, canonical.moves_remaining, canonical.next_disc,
          patternCode(canonical.board[indexOf(row, column)],
                      canonical.board[indexOf(row, column + 1)],
                      canonical.board[indexOf(row + 1, column)],
                      canonical.board[indexOf(row + 1, column + 1)]),
          disc_independent);
    }
  }
  if (count != kActiveFeatures) throw std::logic_error("bad tuple count");
  return result;
}

class Model {
 public:
  explicit Model(float optimistic_value = 0, bool disc_independent = false,
                 bool absolute_position = false, bool hierarchical = false)
      : disc_independent_(disc_independent),
        absolute_position_(absolute_position),
        hierarchical_(hierarchical),
        weights_((hierarchical ? kTables
                               : (absolute_position ? kAbsoluteTables : kTables)) *
                     kRisePhases * (disc_independent ? 1 : kDiscValues) *
                     kPatterns,
                 optimistic_value / static_cast<float>(kActiveFeatures)),
        residual_(hierarchical ? kAbsoluteTables * kRisePhases * kPatterns : 0,
                  0.0f) {
    if (hierarchical && (!disc_independent || absolute_position)) {
      throw std::invalid_argument(
          "hierarchical tuples require chance-state shared base");
    }
  }

  float value(const State& state) const {
    if (state.game_over) return 0;
    const auto active = features(state, disc_independent_, absolute_position_);
    float result = 0;
    for (int index : active) result += weights_[index];
    if (!residual_.empty()) {
      const auto residual_active = features(state, true, true);
      for (int index : residual_active) result += residual_[index];
    }
    return result;
  }

  void td0(const State& state, float delta, float learning_rate) {
    const float branches = residual_.empty() ? 1.0f : 2.0f;
    const float step = learning_rate * delta /
                       (branches * static_cast<float>(kActiveFeatures));
    const auto active = features(state, disc_independent_, absolute_position_);
    for (int index : active) weights_[index] += step;
    if (!residual_.empty()) {
      const auto residual_active = features(state, true, true);
      for (int index : residual_active) residual_[index] += step;
    }
  }

  std::size_t bytes() const {
    return (weights_.size() + residual_.size()) * sizeof(float);
  }
  std::size_t entries() const { return weights_.size() + residual_.size(); }
  bool discIndependent() const { return disc_independent_; }
  bool absolutePosition() const { return absolute_position_; }
  bool hierarchical() const { return hierarchical_; }

  void save(const std::string& path) const {
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("could not open n-tuple checkpoint");
    const char model_kind = hierarchical_
                                ? 'H'
                                : (absolute_position_
                                       ? 'A'
                                       : (disc_independent_ ? 'C' : 'V'));
    const std::array<char, 8> magic{{'D', '7', 'N', 'T', 'U', model_kind, '1', '\0'}};
    const std::uint32_t entries = static_cast<std::uint32_t>(weights_.size());
    const std::uint32_t residual_entries =
        static_cast<std::uint32_t>(residual_.size());
    output.write(magic.data(), magic.size());
    output.write(reinterpret_cast<const char*>(&entries), sizeof(entries));
    if (hierarchical_) {
      output.write(reinterpret_cast<const char*>(&residual_entries),
                   sizeof(residual_entries));
    }
    output.write(reinterpret_cast<const char*>(weights_.data()),
                 static_cast<std::streamsize>(weights_.size() * sizeof(float)));
    if (hierarchical_) {
      output.write(reinterpret_cast<const char*>(residual_.data()),
                   static_cast<std::streamsize>(residual_.size() * sizeof(float)));
    }
    if (!output) throw std::runtime_error("failed writing n-tuple checkpoint");
  }

  void load(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not open n-tuple checkpoint");
    std::array<char, 8> magic{};
    std::uint32_t entries = 0;
    std::uint32_t residual_entries = 0;
    input.read(magic.data(), magic.size());
    input.read(reinterpret_cast<char*>(&entries), sizeof(entries));
    const char model_kind = hierarchical_
                                ? 'H'
                                : (absolute_position_
                                       ? 'A'
                                       : (disc_independent_ ? 'C' : 'V'));
    const std::array<char, 8> expected{{'D', '7', 'N', 'T', 'U', model_kind, '1', '\0'}};
    if (hierarchical_) {
      input.read(reinterpret_cast<char*>(&residual_entries),
                 sizeof(residual_entries));
    }
    if (magic != expected || entries != weights_.size() ||
        residual_entries != residual_.size()) {
      throw std::runtime_error("incompatible n-tuple checkpoint");
    }
    input.read(reinterpret_cast<char*>(weights_.data()),
               static_cast<std::streamsize>(weights_.size() * sizeof(float)));
    if (hierarchical_) {
      input.read(reinterpret_cast<char*>(residual_.data()),
                 static_cast<std::streamsize>(residual_.size() * sizeof(float)));
    }
    if (!input) throw std::runtime_error("truncated n-tuple checkpoint");
  }

  void loadSharedBase(const std::string& path) {
    if (!hierarchical_) {
      throw std::logic_error("shared warm start requires hierarchical model");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not open shared checkpoint");
    std::array<char, 8> magic{};
    std::uint32_t entries = 0;
    input.read(magic.data(), magic.size());
    input.read(reinterpret_cast<char*>(&entries), sizeof(entries));
    constexpr std::array<char, 8> expected{{'D', '7', 'N', 'T', 'U', 'C', '1', '\0'}};
    if (magic != expected || entries != weights_.size()) {
      throw std::runtime_error("incompatible shared warm-start checkpoint");
    }
    input.read(reinterpret_cast<char*>(weights_.data()),
               static_cast<std::streamsize>(weights_.size() * sizeof(float)));
    if (!input) throw std::runtime_error("truncated shared warm-start checkpoint");
  }

 private:
  bool disc_independent_ = false;
  bool absolute_position_ = false;
  bool hierarchical_ = false;
  std::vector<float> weights_;
  std::vector<float> residual_;
};

struct Options {
  int training_games = 10'000;
  int probe_games = 64;
  int max_moves = 500;
  int chance_samples = 7;
  int report_every = 1'000;
  std::uint32_t training_seed_start = 0x3d70'0000u;
  std::uint32_t probe_seed_start = 0x4d70'0000u;
  float gamma = 0.995f;
  float lambda = 0;
  float learning_rate = 0.005f;
  float epsilon = 0.05f;
  float optimistic_value = 0;
  std::string checkpoint;
  std::string resume;
  bool disc_independent = false;
  bool direct_score_reward = false;
  bool absolute_position = false;
  bool hierarchical = false;
  std::string warm_start_shared;
};

struct ActionValue {
  int physical_column = -1;
  float value = -std::numeric_limits<float>::infinity();
};

inline float transitionReward(const MoveResult& move,
                              const Options& options) {
  return options.direct_score_reward
             ? static_cast<float>(move.score_delta) / 3'400.0f
             : 1.0f;
}

inline std::uint32_t seedWithFirstDisc(std::uint32_t base,
                                       std::uint8_t target_disc) {
  std::uint32_t candidate = base;
  for (std::uint32_t attempt = 0; attempt < 1'000; ++attempt) {
    Mulberry32 probe(candidate);
    if (probe.nextDisc() == target_disc) return candidate;
    candidate = mix32(candidate + 0x9e37'79b9u + attempt);
  }
  throw std::runtime_error("could not stratify first reveal disc");
}

inline std::array<float, kBoardSize> actionValues(const Model& model,
                                                  const State& source,
                                                  const Options& options) {
  const auto canonical = canonicalize(source);
  const State& state = canonical.state;
  const std::uint32_t hash = observableHash(state);
  std::array<float, kBoardSize> physical_values{};
  physical_values.fill(-std::numeric_limits<float>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    if (!isLegal(state.board, action)) continue;
    double total = 0;
    const int disc_offset = static_cast<int>(mix32(hash ^ 0x4e45'5854u) % 7u);
    for (int sample = 0; sample < options.chance_samples; ++sample) {
      const std::uint32_t base_seed = mix32(
          hash ^ (static_cast<std::uint32_t>(sample + 1) * 0xc2b2'ae35u) ^
          0x5245'564cu);
      const auto target_reveal =
          static_cast<std::uint8_t>(((disc_offset + sample) % 7) + 1);
      const std::uint32_t reveal_seed =
          seedWithFirstDisc(base_seed, target_reveal);
      Mulberry32 random(reveal_seed);
      MoveResult move;
      if (!playMove(state, action, random, move)) {
        throw std::runtime_error("tuple evaluator selected an illegal move");
      }
      // Upcoming discs are an independent uniform chance variable. Cycling
      // the seven values makes a seven-sample evaluation exactly stratified;
      // reveal randomness remains sampled and seed-blind.
      if (!model.discIndependent() && !move.state.game_over) {
        move.state.next_disc = static_cast<std::uint8_t>(
            ((disc_offset + sample) % kBoardSize) + 1);
      }
      total += transitionReward(move, options) +
               (move.state.game_over ? 0.0 : options.gamma * model.value(move.state));
    }
    const int physical = physicalAction(action, canonical.mirrored);
    physical_values[physical] =
        static_cast<float>(total / options.chance_samples);
  }
  return physical_values;
}

inline int greedyAction(const Model& model, const State& state,
                        const Options& options) {
  const auto values = actionValues(model, state, options);
  const bool mirrored = canonicalize(state).mirrored;
  constexpr std::array<int, kBoardSize> tie_order{{3, 2, 4, 1, 5, 0, 6}};
  int selected_canonical = -1;
  float best = -std::numeric_limits<float>::infinity();
  for (int canonical_column : tie_order) {
    const int physical_column = physicalAction(canonical_column, mirrored);
    if (values[physical_column] > best) {
      best = values[physical_column];
      selected_canonical = canonical_column;
    }
  }
  return physicalAction(selected_canonical, mirrored);
}

struct Evaluation {
  double mean_score = 0;
  double mean_moves = 0;
  std::int64_t minimum_score = 0;
  std::int64_t maximum_score = 0;
  int censored = 0;
};

inline Evaluation evaluate(const Model& model, const Options& options) {
  Evaluation result;
  result.minimum_score = std::numeric_limits<std::int64_t>::max();
  result.maximum_score = std::numeric_limits<std::int64_t>::min();
  for (int game = 0; game < options.probe_games; ++game) {
    const std::uint32_t seed =
        options.probe_seed_start + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < options.max_moves) {
      const int action = greedyAction(model, state, options);
      MoveResult move;
      if (!playHeadlessMove(state, seed, action, move)) {
        throw std::runtime_error("tuple policy selected an illegal probe action");
      }
    }
    result.mean_score += state.score;
    result.mean_moves += state.moves_played;
    result.minimum_score = std::min(result.minimum_score, state.score);
    result.maximum_score = std::max(result.maximum_score, state.score);
    if (!state.game_over) ++result.censored;
  }
  result.mean_score /= options.probe_games;
  result.mean_moves /= options.probe_games;
  return result;
}

inline void printProbe(const Evaluation& probe, int training_games) {
  std::cout << std::fixed << std::setprecision(3)
            << "NTUPLE_PROBE {\"trainingGames\":" << training_games
            << ",\"meanScore\":" << probe.mean_score
            << ",\"meanMoves\":" << probe.mean_moves
            << ",\"minimumScore\":" << probe.minimum_score
            << ",\"maximumScore\":" << probe.maximum_score
            << ",\"censored\":" << probe.censored
            << ",\"validationEligible\":"
            << (probe.mean_score >= 400'000 ? "true" : "false") << "}\n";
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

inline bool selfTest(std::ostream& output, bool disc_independent = true,
                     bool absolute_position = false,
                     bool hierarchical = true) {
  Options options;
  options.chance_samples = 7;
  Model model(37.0f, disc_independent, absolute_position, hierarchical);
  State state = initialHeadlessState(0x2d70'0042u);
  for (int action : {3, 1, 5, 2, 4, 0}) {
    MoveResult move;
    if (!playHeadlessMove(state, 0x2d70'0042u, action, move)) break;
  }
  State mirrored = state;
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      mirrored.board[indexOf(row, column)] =
          state.board[indexOf(row, kBoardSize - 1 - column)];
    }
  }
  const float first_value = model.value(state);
  const float mirrored_value = model.value(mirrored);
  const auto first_actions = actionValues(model, state, options);
  const auto mirrored_actions = actionValues(model, mirrored, options);
  bool mirror_safe = first_value == mirrored_value;
  for (int column = 0; column < kBoardSize; ++column) {
    const float left = first_actions[column];
    const float right = mirrored_actions[kBoardSize - 1 - column];
    if (std::isfinite(left) != std::isfinite(right) ||
        (std::isfinite(left) && std::abs(left - right) > 1e-5f)) {
      mirror_safe = false;
    }
  }
  const auto repeated = actionValues(model, state, options);
  const bool deterministic = repeated == first_actions;
  bool first_reveal_stratified = true;
  for (int disc = 1; disc <= 7; ++disc) {
    Mulberry32 probe(seedWithFirstDisc(0x1234'5678u,
                                      static_cast<std::uint8_t>(disc)));
    if (probe.nextDisc() != disc) first_reveal_stratified = false;
  }
  const bool action_mirror_safe =
      greedyAction(model, state, options) ==
      kBoardSize - 1 - greedyAction(model, mirrored, options);
  output << "NTUPLE_SELF_TEST {\"passed\":"
         << (mirror_safe && deterministic && action_mirror_safe &&
                     first_reveal_stratified
                 ? "true"
                 : "false")
         << ",\"mirrorSafe\":" << (mirror_safe ? "true" : "false")
         << ",\"actionMirrorSafe\":"
         << (action_mirror_safe ? "true" : "false")
         << ",\"seedBlindDeterministic\":"
         << (deterministic ? "true" : "false")
         << ",\"firstRevealStratified\":"
         << (first_reveal_stratified ? "true" : "false")
         << ",\"model\":\""
         << (disc_independent ? "chance-state" : "visible-disc")
         << (hierarchical ? "-hierarchical"
                          : (absolute_position ? "-absolute" : "-shared"))
         << "\",\"tables\":"
         << (hierarchical ? kTables + kAbsoluteTables
                          : (absolute_position ? kAbsoluteTables : kTables))
         << ",\"entries\":"
         << model.entries() << ",\"activeFeatures\":" << kActiveFeatures
         << ",\"bytes\":" << model.bytes() << "}\n";
  return mirror_safe && deterministic && action_mirror_safe &&
         first_reveal_stratified;
}

inline int train(const Options& options) {
  if (options.training_games < 1 || options.probe_games < 1 ||
      options.max_moves < 1 || options.chance_samples < 1 ||
      options.report_every < 1 || options.learning_rate <= 0 ||
      options.gamma <= 0 || options.gamma > 1 || options.lambda < 0 ||
      options.lambda > 1 || options.epsilon < 0 || options.epsilon > 1) {
    throw std::invalid_argument("invalid n-tuple training option");
  }
  Model model(options.optimistic_value, options.disc_independent,
              options.absolute_position, options.hierarchical);
  if (!options.resume.empty() && !options.warm_start_shared.empty()) {
    throw std::invalid_argument("use either --resume or --warm-start-shared");
  }
  if (!options.resume.empty()) {
    model.load(options.resume);
  } else if (!options.warm_start_shared.empty()) {
    model.loadSharedBase(options.warm_start_shared);
  }
  std::cout << "NTUPLE_CONFIG {\"model\":\""
            << (options.disc_independent ? "chance-state" : "visible-disc")
            << (options.hierarchical
                    ? "-hierarchical"
                    : (options.absolute_position ? "-absolute" : "-shared"))
            << "\",\"entries\":" << model.entries() << ",\"bytes\":"
            << model.bytes() << ",\"gamma\":" << options.gamma
            << ",\"lambda\":" << options.lambda
            << ",\"learningRate\":" << options.learning_rate
            << ",\"epsilon\":" << options.epsilon
            << ",\"chanceSamples\":" << options.chance_samples << "}\n";
  std::cout << "NTUPLE_OBJECTIVE {\"reward\":\""
            << (options.direct_score_reward ? "score-delta-over-3400"
                                            : "one-per-move")
            << "\"}\n";
  printProbe(evaluate(model, options), 0);
  const auto started = std::chrono::steady_clock::now();
  double interval_score = 0;
  double interval_moves = 0;
  int interval_games = 0;
  std::uint64_t total_moves = 0;
  struct TrajectoryStep {
    State state{};
    float old_value = 0;
    float reward = 1;
    bool terminal = false;
  };
  for (int game = 0; game < options.training_games; ++game) {
    const std::uint32_t seed =
        options.training_seed_start + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    Mulberry32 exploration(mix32(seed ^ 0x4558'504cu));
    std::vector<TrajectoryStep> trajectory;
    if (options.lambda > 0) trajectory.reserve(128);
    while (!state.game_over && state.moves_played < options.max_moves) {
      int action = greedyAction(model, state, options);
      if (exploration.nextUnit() < options.epsilon) {
        int legal_count = 0;
        const auto legal = legalColumns(state.board, legal_count);
        const int selected = static_cast<int>(
            (static_cast<std::uint64_t>(exploration.nextBits()) * legal_count) >>
            32);
        action = legal[selected];
      }
      const State previous = state;
      const float previous_value = model.value(previous);
      MoveResult move;
      if (!playHeadlessMove(state, seed, action, move)) {
        throw std::runtime_error("tuple policy selected an illegal train action");
      }
      const float reward = transitionReward(move, options);
      if (options.lambda == 0) {
        const float target = reward +
                             (state.game_over
                                  ? 0.0f
                                  : options.gamma * model.value(state));
        model.td0(previous, target - previous_value, options.learning_rate);
      } else {
        trajectory.push_back(
            {previous, previous_value, reward, state.game_over});
      }
    }
    if (options.lambda > 0 && !trajectory.empty()) {
      const float bootstrap = state.game_over ? 0.0f : model.value(state);
      float lambda_return = bootstrap;
      for (int index = static_cast<int>(trajectory.size()) - 1; index >= 0;
           --index) {
        const auto& step = trajectory[index];
        const float next_value =
            step.terminal
                ? 0.0f
                : (index + 1 < static_cast<int>(trajectory.size())
                       ? trajectory[index + 1].old_value
                       : bootstrap);
        if (step.terminal) lambda_return = 0;
        lambda_return = step.reward +
                        options.gamma *
                            ((1.0f - options.lambda) * next_value +
                             options.lambda * lambda_return);
        model.td0(step.state, lambda_return - step.old_value,
                  options.learning_rate);
      }
    }
    interval_score += state.score;
    interval_moves += state.moves_played;
    ++interval_games;
    total_moves += state.moves_played;
    if ((game + 1) % options.report_every == 0 ||
        game + 1 == options.training_games) {
      const double seconds = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
      std::cout << "NTUPLE_TRAIN {\"games\":" << game + 1
                << ",\"intervalMeanScore\":"
                << interval_score / interval_games
                << ",\"intervalMeanMoves\":"
                << interval_moves / interval_games << ",\"totalMoves\":"
                << total_moves << ",\"seconds\":" << seconds
                << ",\"movesPerSecond\":" << total_moves / seconds << "}\n";
      interval_score = 0;
      interval_moves = 0;
      interval_games = 0;
    }
  }
  const Evaluation probe = evaluate(model, options);
  printProbe(probe, options.training_games);
  if (!options.checkpoint.empty()) {
    model.save(options.checkpoint);
    std::cout << "NTUPLE_CHECKPOINT {\"path\":\"" << options.checkpoint
              << "\",\"bytes\":" << model.bytes()
              << ",\"peakResidentBytes\":" << peakResidentBytes()
              << "}\n";
  }
  return 0;
}

}  // namespace drop7::ntuple
