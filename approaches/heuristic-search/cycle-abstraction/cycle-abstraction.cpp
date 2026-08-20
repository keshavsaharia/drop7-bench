#include "../../../src/core/native/engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace drop7::cycle_abstraction {

constexpr std::uint32_t kTrainingStart = 0x3d70'0000u;
constexpr std::uint32_t kTrainingEnd = 0x4d70'0000u;

enum Feature : int {
  kOccupancy,
  kCovers,
  kSolid,
  kCracked,
  kMovesUntilRise,
  kProjectedLoad,
  kMaximumHeight,
  kMeanHeight,
  kRoughness,
  kTopLoad,
  kCoverAltitude,
  kTriggerOneAway,
  kTriggerTwoAway,
  kTriggerFar,
  kTriggerOvershot,
  kCoverNumberContacts,
  kCrackedExposure,
  kSolidDoubleExposure,
  kStoredFive,
  kStoredSix,
  kStoredSeven,
  kStoredHighReady,
  kLowCapOnes,
  kLowCapTwos,
  kAdjacentLowCaps,
  kTripleTwos,
  kLowCapHeightLoad,
  kDiscOne,
  kDiscTwo,
  kDiscThree,
  kDiscFour,
  kDiscFive,
  kDiscSix,
  kDiscSeven,
  kHeightColumn0,
  kHeightColumn1,
  kHeightColumn2,
  kHeightColumn3,
  kHeightColumn4,
  kHeightColumn5,
  kHeightColumn6,
  kFeatureCount,
};

using Features = std::array<float, kFeatureCount>;

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

Board mirrorBoard(const Board& board) {
  Board result{};
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      result[indexOf(row, column)] =
          board[indexOf(row, kBoardSize - 1 - column)];
    }
  }
  return result;
}

State canonicalize(const State& source) {
  State result = source;
  if (mirrorIsSmaller(source.board)) result.board = mirrorBoard(source.board);
  return result;
}

Features extract(const State& original) {
  const State state = canonicalize(original);
  const Board& board = state.board;
  Features result{};
  std::array<int, kBoardSize> heights{};
  std::array<bool, kBoardSize> low_caps{};
  int occupied = 0;
  int covers = 0;
  int solids = 0;
  int cracked = 0;
  int cover_altitude = 0;

  for (int column = 0; column < kBoardSize; ++column) {
    for (int row = 0; row < kBoardSize; ++row) {
      const auto cell = board[indexOf(row, column)];
      if (cell == kEmpty) continue;
      ++occupied;
      ++heights[column];
      if (cell == kSolid || cell == kCracked) {
        ++covers;
        solids += cell == kSolid;
        cracked += cell == kCracked;
        cover_altitude += kBoardSize - row;
        int numbered_contacts = 0;
        for (const auto [row_step, column_step] :
             std::array<std::array<int, 2>, 4>{{
                 {{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}},
             }}) {
          const int next_row = row + row_step;
          const int next_column = column + column_step;
          if (inside(next_row, next_column) &&
              isNumbered(board[indexOf(next_row, next_column)])) {
            ++numbered_contacts;
          }
        }
        result[kCoverNumberContacts] += numbered_contacts;
        result[kCrackedExposure] += cell == kCracked && numbered_contacts > 0;
        result[kSolidDoubleExposure] +=
            cell == kSolid && numbered_contacts >= 2;
        continue;
      }
      if (!isNumbered(cell)) continue;
      const int line = std::max(lineLength(board, row, column, false),
                                lineLength(board, row, column, true));
      const int deficit = static_cast<int>(cell) - line;
      result[kTriggerOneAway] += deficit == 1;
      result[kTriggerTwoAway] += deficit == 2;
      result[kTriggerFar] += deficit >= 3;
      result[kTriggerOvershot] += deficit < 0;
      if (cell >= 5 && deficit > 0) {
        result[kStoredFive + cell - 5] += 1;
        result[kStoredHighReady] += deficit <= 2;
      }
    }
    if (heights[column] > 0) {
      const auto cap = board[indexOf(kBoardSize - heights[column], column)];
      if (cap == 1 || cap == 2) {
        low_caps[column] = true;
        result[cap == 1 ? kLowCapOnes : kLowCapTwos] += 1;
        result[kLowCapHeightLoad] += heights[column] * heights[column];
      }
    }
  }
  for (int column = 0; column + 1 < kBoardSize; ++column) {
    result[kAdjacentLowCaps] += low_caps[column] && low_caps[column + 1];
  }
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column + 2 < kBoardSize; ++column) {
      result[kTripleTwos] += board[indexOf(row, column)] == 2 &&
                            board[indexOf(row, column + 1)] == 2 &&
                            board[indexOf(row, column + 2)] == 2;
    }
  }
  int roughness = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    result[kHeightColumn0 + column] = heights[column] / 7.0f;
    if (column) roughness += std::abs(heights[column] - heights[column - 1]);
  }
  result[kOccupancy] = occupied / 49.0f;
  result[kCovers] = covers / 49.0f;
  result[kSolid] = solids / 49.0f;
  result[kCracked] = cracked / 49.0f;
  result[kMovesUntilRise] = state.moves_remaining / 5.0f;
  result[kProjectedLoad] =
      (occupied + 7 - 1.4f * state.moves_remaining) / 49.0f;
  result[kMaximumHeight] =
      *std::max_element(heights.begin(), heights.end()) / 7.0f;
  result[kMeanHeight] = occupied / 49.0f;
  result[kRoughness] = roughness / 42.0f;
  for (int column = 0; column < kBoardSize; ++column) {
    result[kTopLoad] += board[indexOf(0, column)] != kEmpty;
  }
  result[kTopLoad] /= 7.0f;
  result[kCoverAltitude] = cover_altitude / 343.0f;
  result[kDiscOne + state.next_disc - 1] = 1;
  return result;
}

constexpr std::array<const char*, kFeatureCount> kFeatureNames{{
    "occupancy",
    "covers",
    "solid",
    "cracked",
    "moves_until_rise",
    "projected_load",
    "maximum_height",
    "mean_height",
    "roughness",
    "top_load",
    "cover_altitude",
    "trigger_one_away",
    "trigger_two_away",
    "trigger_far",
    "trigger_overshot",
    "cover_number_contacts",
    "cracked_exposure",
    "solid_double_exposure",
    "stored_five",
    "stored_six",
    "stored_seven",
    "stored_high_ready",
    "low_cap_ones",
    "low_cap_twos",
    "adjacent_low_caps",
    "triple_twos",
    "low_cap_height_load",
    "disc_one",
    "disc_two",
    "disc_three",
    "disc_four",
    "disc_five",
    "disc_six",
    "disc_seven",
    "height_column_0",
    "height_column_1",
    "height_column_2",
    "height_column_3",
    "height_column_4",
    "height_column_5",
    "height_column_6",
}};

enum MacroOption : int {
  kClear,
  kBuild,
  kTunnel,
  kSafety,
  kBalanced,
  kOptionCount,
};

constexpr std::array<const char*, kOptionCount> kOptionNames{{
    "clear", "build", "tunnel", "safety", "balanced",
}};

std::uint32_t observableHash(const State& source) {
  const State state = canonicalize(source);
  std::uint32_t hash = 0x811c'9dc5u;
  for (const std::uint8_t cell : state.board) {
    hash = (hash ^ cell) * 0x0100'0193u;
  }
  hash = (hash ^ state.next_disc) * 0x0100'0193u;
  hash = (hash ^ static_cast<std::uint32_t>(state.moves_remaining)) *
         0x0100'0193u;
  hash = (hash ^ static_cast<std::uint32_t>(state.level)) * 0x0100'0193u;
  return mix32(hash);
}

int eventCount(const MoveResult& result, bool reveals) {
  int total = 0;
  for (const Wave& wave : result.waves) {
    total += reveals ? wave.revealed : wave.cleared;
  }
  return total;
}

float optionValue(MacroOption option, const Features& before,
                  const MoveResult& move) {
  if (move.state.game_over) return -1.0e9f;
  const Features after = extract(move.state);
  const float clears = static_cast<float>(eventCount(move, false));
  const float reveals = static_cast<float>(eventCount(move, true));
  const float potential =
      12.0f * after[kTriggerOneAway] + 7.0f * after[kTriggerTwoAway] +
      4.0f * after[kStoredHighReady] + 1.5f *
          (after[kStoredFive] + after[kStoredSix] + after[kStoredSeven]);
  const float clog = 12.0f * after[kLowCapOnes] +
                     9.0f * after[kLowCapTwos] +
                     18.0f * after[kAdjacentLowCaps] +
                     24.0f * after[kTripleTwos] +
                     1.5f * after[kLowCapHeightLoad];
  const float danger = 170.0f * after[kMaximumHeight] +
                       130.0f * after[kTopLoad] +
                       55.0f * after[kProjectedLoad] +
                       20.0f * after[kRoughness] + clog;
  const float tunnel_progress =
      18.0f * reveals +
      24.0f * std::max(0.0f, before[kCovers] - after[kCovers]) +
      5.0f * after[kCrackedExposure] +
      7.0f * after[kSolidDoubleExposure];
  const float score_signal =
      static_cast<float>(move.score_delta) / 2000.0f;
  switch (option) {
    case kClear:
      return -danger + 42.0f * clears + 14.0f * reveals + score_signal;
    case kBuild:
      return -0.62f * danger + 3.0f * potential - 18.0f * clears +
             2.0f * after[kCoverNumberContacts];
    case kTunnel:
      return -0.92f * danger + tunnel_progress + 8.0f * clears +
             0.25f * potential;
    case kSafety:
      return -2.1f * danger + 22.0f * clears + 10.0f * reveals +
             0.25f * potential;
    case kBalanced:
      return -danger + 19.0f * clears + tunnel_progress +
             1.15f * potential + 0.2f * score_signal;
    case kOptionCount:
      break;
  }
  return -1.0e9f;
}

int chooseOptionAction(const State& source, MacroOption option) {
  const bool mirrored = mirrorIsSmaller(source.board);
  const State state = canonicalize(source);
  const Features before = extract(state);
  constexpr std::array<int, kBoardSize> order{{3, 2, 4, 1, 5, 0, 6}};
  const std::uint32_t chance_seed =
      mix32(observableHash(state) ^ 0x504f'4c59u);
  int best_column = -1;
  float best_value = -std::numeric_limits<float>::infinity();
  for (const int column : order) {
    if (!isLegal(state.board, column)) continue;
    Mulberry32 random(chance_seed);
    MoveResult move;
    if (!playMove(state, column, random, move)) continue;
    const float value = optionValue(option, before, move);
    if (value > best_value + 1.0e-6f) {
      best_value = value;
      best_column = column;
    }
  }
  if (best_column < 0) return -1;
  return mirrored ? kBoardSize - 1 - best_column : best_column;
}

struct MacroSample {
  Features features{};
  int option = 0;
  int group = 0;
  int successes = 0;
  int trials = 0;
  float mean_moves = 0.0f;
};

struct RunConfig {
  int train_games = 24;
  int heldout_games = 12;
  int base_moves = 75;
  int rollouts = 3;
  int horizon = 25;
  int epochs = 500;
  std::string output = "/tmp/drop7-cycle-abstraction.json";
};

constexpr std::uint32_t kTrainSeedStart = 0x3d73'0000u;
constexpr std::uint32_t kHeldoutSeedStart = 0x3d74'0000u;

void validateSeedSpan(std::uint32_t start, int count) {
  if (count <= 0) throw std::invalid_argument("game count must be positive");
  const std::uint64_t end = static_cast<std::uint64_t>(start) +
                            static_cast<std::uint64_t>(count);
  if (start < kTrainingStart || end > kTrainingEnd) {
    throw std::invalid_argument("seed span leaves the training partition");
  }
}

std::vector<State> collectBaseStates(std::uint32_t seed_start, int games,
                                     int maximum_moves) {
  validateSeedSpan(seed_start, games);
  std::vector<State> states;
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed =
        seed_start + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < maximum_moves) {
      if (state.moves_played % kMovesPerLevel == 0) states.push_back(state);
      const int cycle = state.moves_played / kMovesPerLevel;
      const auto option =
          static_cast<MacroOption>((game + cycle) % kOptionCount);
      const int action = chooseOptionAction(state, option);
      if (action < 0) break;
      MoveResult move;
      if (!playHeadlessMove(state, seed, action, move)) break;
    }
  }
  return states;
}

bool playPairedSyntheticMove(State& state, int action,
                             std::uint32_t base_tape, int step,
                             MoveResult& move) {
  const auto step_bits = static_cast<std::uint32_t>(step + 1);
  const std::uint32_t reveal_seed =
      mix32(base_tape ^ (step_bits * 0x85eb'ca6bu) ^ kRevealDomain);
  Mulberry32 random(reveal_seed);
  if (!playMove(state, action, random, move)) return false;
  state = move.state;
  if (!state.game_over) {
    const std::uint32_t disc_bits =
        mix32(base_tape ^ (step_bits * 0x9e37'79b9u) ^ kNextDiscDomain);
    state.next_disc = static_cast<std::uint8_t>(
        ((static_cast<std::uint64_t>(disc_bits) * 7u) >> 32) + 1u);
  }
  return true;
}

std::vector<MacroSample> labelStates(const std::vector<State>& states,
                                     int rollouts, int horizon) {
  std::vector<MacroSample> samples;
  samples.reserve(states.size() * kOptionCount);
  for (std::size_t group = 0; group < states.size(); ++group) {
    const State& base = states[group];
    const std::uint32_t base_hash = observableHash(base);
    for (int option_index = 0; option_index < kOptionCount; ++option_index) {
      int successes = 0;
      int total_moves = 0;
      for (int rollout = 0; rollout < rollouts; ++rollout) {
        State state = base;
        const std::uint32_t tape = mix32(
            base_hash ^
            (static_cast<std::uint32_t>(rollout + 1) * 0x27d4'eb2du) ^
            0x4359'434cu);
        int survived = 0;
        for (int step = 0; step < horizon && !state.game_over; ++step) {
          const auto option = static_cast<MacroOption>(
              step < kMovesPerLevel ? option_index : kBalanced);
          const int action = chooseOptionAction(state, option);
          if (action < 0) break;
          MoveResult move;
          if (!playPairedSyntheticMove(state, action, tape, step, move)) break;
          ++survived;
        }
        total_moves += survived;
        successes += survived == horizon && !state.game_over;
      }
      MacroSample sample;
      sample.features = extract(base);
      sample.option = option_index;
      sample.group = static_cast<int>(group);
      sample.successes = successes;
      sample.trials = rollouts;
      sample.mean_moves =
          static_cast<float>(total_moves) / static_cast<float>(rollouts);
      samples.push_back(sample);
    }
  }
  return samples;
}

constexpr int kParameterCount = kFeatureCount + 1;
using Parameters = std::array<double, kParameterCount>;

struct LinearModels {
  Features mean{};
  Features scale{};
  std::array<Parameters, kOptionCount> logistic{};
  std::array<Parameters, kOptionCount> linear{};
};

double standardizedValue(const MacroSample& sample, const LinearModels& models,
                         int parameter) {
  if (parameter == 0) return 1.0;
  const int feature = parameter - 1;
  return (sample.features[feature] - models.mean[feature]) /
         models.scale[feature];
}

double dot(const Parameters& parameters, const MacroSample& sample,
           const LinearModels& models) {
  double result = parameters[0];
  for (int parameter = 1; parameter < kParameterCount; ++parameter) {
    result += parameters[parameter] *
              standardizedValue(sample, models, parameter);
  }
  return result;
}

double sigmoid(double value) {
  if (value >= 0.0) {
    const double exponential = std::exp(-value);
    return 1.0 / (1.0 + exponential);
  }
  const double exponential = std::exp(value);
  return exponential / (1.0 + exponential);
}

LinearModels fitModels(const std::vector<MacroSample>& samples, int horizon,
                       int epochs) {
  if (samples.empty()) throw std::invalid_argument("empty training samples");
  LinearModels models;
  for (int feature = 0; feature < kFeatureCount; ++feature) {
    double sum = 0.0;
    for (const MacroSample& sample : samples) sum += sample.features[feature];
    models.mean[feature] =
        static_cast<float>(sum / static_cast<double>(samples.size()));
    double squared = 0.0;
    for (const MacroSample& sample : samples) {
      const double delta = sample.features[feature] - models.mean[feature];
      squared += delta * delta;
    }
    const double variance = squared / static_cast<double>(samples.size());
    models.scale[feature] =
        static_cast<float>(std::max(1.0e-4, std::sqrt(variance)));
  }
  std::array<int, kOptionCount> counts{};
  std::array<double, kOptionCount> survival_sum{};
  std::array<double, kOptionCount> moves_sum{};
  for (const MacroSample& sample : samples) {
    ++counts[sample.option];
    survival_sum[sample.option] +=
        static_cast<double>(sample.successes) / sample.trials;
    moves_sum[sample.option] += sample.mean_moves / horizon;
  }
  for (int option = 0; option < kOptionCount; ++option) {
    const double survival = std::clamp(
        survival_sum[option] / counts[option], 1.0e-4, 1.0 - 1.0e-4);
    models.logistic[option][0] = std::log(survival / (1.0 - survival));
    models.linear[option][0] = moves_sum[option] / counts[option];
  }

  constexpr double ridge = 0.003;
  for (int epoch = 0; epoch < epochs; ++epoch) {
    std::array<Parameters, kOptionCount> logistic_gradient{};
    std::array<Parameters, kOptionCount> linear_gradient{};
    for (const MacroSample& sample : samples) {
      const int option = sample.option;
      const double survival_target =
          static_cast<double>(sample.successes) / sample.trials;
      const double move_target = sample.mean_moves / horizon;
      const double probability =
          sigmoid(dot(models.logistic[option], sample, models));
      const double move_prediction =
          dot(models.linear[option], sample, models);
      for (int parameter = 0; parameter < kParameterCount; ++parameter) {
        const double value = standardizedValue(sample, models, parameter);
        logistic_gradient[option][parameter] +=
            (probability - survival_target) * value;
        linear_gradient[option][parameter] +=
            (move_prediction - move_target) * value;
      }
    }
    const double learning_rate =
        0.045 / std::sqrt(1.0 + static_cast<double>(epoch) * 0.015);
    for (int option = 0; option < kOptionCount; ++option) {
      const double denominator = static_cast<double>(counts[option]);
      for (int parameter = 0; parameter < kParameterCount; ++parameter) {
        double logistic_gradient_value =
            logistic_gradient[option][parameter] / denominator;
        double linear_gradient_value =
            linear_gradient[option][parameter] / denominator;
        if (parameter != 0) {
          logistic_gradient_value +=
              ridge * models.logistic[option][parameter];
          linear_gradient_value += ridge * models.linear[option][parameter];
        }
        models.logistic[option][parameter] -=
            learning_rate * logistic_gradient_value;
        models.linear[option][parameter] -=
            learning_rate * linear_gradient_value;
      }
    }
  }
  return models;
}

double weightedAuc(const std::vector<MacroSample>& samples,
                   const LinearModels& models) {
  struct Point {
    double prediction;
    int positives;
    int negatives;
  };
  std::vector<Point> points;
  points.reserve(samples.size());
  double total_positives = 0.0;
  double total_negatives = 0.0;
  for (const MacroSample& sample : samples) {
    const double prediction =
        sigmoid(dot(models.logistic[sample.option], sample, models));
    points.push_back(
        {prediction, sample.successes, sample.trials - sample.successes});
    total_positives += sample.successes;
    total_negatives += sample.trials - sample.successes;
  }
  if (total_positives == 0.0 || total_negatives == 0.0) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  std::sort(points.begin(), points.end(), [](const Point& left,
                                              const Point& right) {
    return left.prediction < right.prediction;
  });
  double numerator = 0.0;
  double lower_negatives = 0.0;
  for (std::size_t begin = 0; begin < points.size();) {
    std::size_t end = begin + 1;
    while (end < points.size() &&
           std::abs(points[end].prediction - points[begin].prediction) <
               1.0e-12) {
      ++end;
    }
    double positives = 0.0;
    double negatives = 0.0;
    for (std::size_t index = begin; index < end; ++index) {
      positives += points[index].positives;
      negatives += points[index].negatives;
    }
    numerator += positives * (lower_negatives + 0.5 * negatives);
    lower_negatives += negatives;
    begin = end;
  }
  return numerator / (total_positives * total_negatives);
}

double optionKendall(const std::vector<MacroSample>& samples,
                     const LinearModels& models, int horizon) {
  double concordant = 0.0;
  double discordant = 0.0;
  for (std::size_t begin = 0; begin + kOptionCount <= samples.size();
       begin += kOptionCount) {
    for (int first = 0; first < kOptionCount; ++first) {
      for (int second = first + 1; second < kOptionCount; ++second) {
        const MacroSample& left = samples[begin + first];
        const MacroSample& right = samples[begin + second];
        const double actual = left.mean_moves - right.mean_moves;
        if (std::abs(actual) < 1.0e-9) continue;
        const double left_prediction =
            horizon * dot(models.linear[left.option], left, models);
        const double right_prediction =
            horizon * dot(models.linear[right.option], right, models);
        const double predicted = left_prediction - right_prediction;
        if (actual * predicted > 0.0) {
          ++concordant;
        } else if (actual * predicted < 0.0) {
          ++discordant;
        }
      }
    }
  }
  const double pairs = concordant + discordant;
  if (pairs == 0.0) return std::numeric_limits<double>::quiet_NaN();
  return (concordant - discordant) / pairs;
}

void writeResults(const RunConfig& config, std::size_t train_states,
                  std::size_t heldout_states, std::size_t train_samples,
                  std::size_t heldout_samples, double auc, double kendall,
                  bool passed) {
  std::ofstream output(config.output);
  if (!output) throw std::runtime_error("could not open output artifact");
  output << std::setprecision(10);
  output << "{\n"
         << "  \"format\": \"drop7-cycle-abstraction-v1\",\n"
         << "  \"trainingSeedOnly\": true,\n"
         << "  \"trainSeedStart\": " << kTrainSeedStart << ",\n"
         << "  \"heldoutSeedStart\": " << kHeldoutSeedStart << ",\n"
         << "  \"trainGames\": " << config.train_games << ",\n"
         << "  \"heldoutGames\": " << config.heldout_games << ",\n"
         << "  \"baseMoves\": " << config.base_moves << ",\n"
         << "  \"rolloutsPerOption\": " << config.rollouts << ",\n"
         << "  \"horizon\": " << config.horizon << ",\n"
         << "  \"trainStates\": " << train_states << ",\n"
         << "  \"heldoutStates\": " << heldout_states << ",\n"
         << "  \"trainSamples\": " << train_samples << ",\n"
         << "  \"heldoutSamples\": " << heldout_samples << ",\n"
         << "  \"features\": [";
  for (int feature = 0; feature < kFeatureCount; ++feature) {
    if (feature) output << ", ";
    output << '"' << kFeatureNames[feature] << '"';
  }
  output << "],\n  \"options\": [";
  for (int option = 0; option < kOptionCount; ++option) {
    if (option) output << ", ";
    output << '"' << kOptionNames[option] << '"';
  }
  output << "],\n"
         << "  \"survivalAuc\": " << auc << ",\n"
         << "  \"optionRankingKendall\": " << kendall << ",\n"
         << "  \"gates\": {\"survivalAuc\": 0.75, \"optionRankingKendall\": 0.6},\n"
         << "  \"qualified\": " << (passed ? "true" : "false") << ",\n"
         << "  \"decision\": \"" << (passed ? "advance" : "reject")
         << "\"\n}\n";
}

int runExperiment(const RunConfig& config, std::ostream& output) {
  validateSeedSpan(kTrainSeedStart, config.train_games);
  validateSeedSpan(kHeldoutSeedStart, config.heldout_games);
  if (config.base_moves <= 0 || config.rollouts <= 0 || config.horizon <= 0 ||
      config.epochs <= 0) {
    throw std::invalid_argument("run counts must all be positive");
  }
  const auto train_states = collectBaseStates(
      kTrainSeedStart, config.train_games, config.base_moves);
  const auto heldout_states = collectBaseStates(
      kHeldoutSeedStart, config.heldout_games, config.base_moves);
  const auto train_samples =
      labelStates(train_states, config.rollouts, config.horizon);
  const auto heldout_samples =
      labelStates(heldout_states, config.rollouts, config.horizon);
  const LinearModels models =
      fitModels(train_samples, config.horizon, config.epochs);
  const double auc = weightedAuc(heldout_samples, models);
  const double kendall =
      optionKendall(heldout_samples, models, config.horizon);
  const bool passed = std::isfinite(auc) && std::isfinite(kendall) &&
                      auc >= 0.75 && kendall >= 0.6;
  writeResults(config, train_states.size(), heldout_states.size(),
               train_samples.size(), heldout_samples.size(), auc, kendall,
               passed);
  output << std::setprecision(6)
         << "CYCLE_ABSTRACTION_RESULT {\"trainingSeedOnly\":true"
         << ",\"trainStates\":" << train_states.size()
         << ",\"heldoutStates\":" << heldout_states.size()
         << ",\"survivalAuc\":" << auc
         << ",\"optionRankingKendall\":" << kendall
         << ",\"qualified\":" << (passed ? "true" : "false")
         << ",\"decision\":\"" << (passed ? "advance" : "reject")
         << "\",\"artifact\":\"" << config.output << "\"}\n";
  return 0;
}

int positiveInteger(const char* value, const char* name) {
  const int parsed = std::stoi(value);
  if (parsed <= 0) throw std::invalid_argument(std::string(name) +
                                               " must be positive");
  return parsed;
}

RunConfig parseRunConfig(int argc, char** argv) {
  RunConfig config;
  for (int index = 2; index < argc; ++index) {
    const std::string argument = argv[index];
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing value for " + argument);
    }
    const char* value = argv[++index];
    if (argument == "--train-games") {
      config.train_games = positiveInteger(value, "train games");
    } else if (argument == "--heldout-games") {
      config.heldout_games = positiveInteger(value, "heldout games");
    } else if (argument == "--base-moves") {
      config.base_moves = positiveInteger(value, "base moves");
    } else if (argument == "--rollouts") {
      config.rollouts = positiveInteger(value, "rollouts");
    } else if (argument == "--horizon") {
      config.horizon = positiveInteger(value, "horizon");
    } else if (argument == "--epochs") {
      config.epochs = positiveInteger(value, "epochs");
    } else if (argument == "--output") {
      config.output = value;
    } else {
      throw std::invalid_argument("unknown argument " + argument);
    }
  }
  return config;
}

bool selfTest(std::ostream& output) {
  constexpr std::uint32_t seed = 0x3d70'0042u;
  State state = initialHeadlessState(seed);
  for (int action : {3, 1, 5, 2, 4, 0}) {
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) break;
  }
  State mirrored = state;
  mirrored.board = mirrorBoard(state.board);
  const auto first = extract(state);
  const auto second = extract(mirrored);
  const bool finite = std::all_of(first.begin(), first.end(),
                                  [](float value) { return std::isfinite(value); });
  const bool mirror_safe = first == second;
  const bool seed_partition = seed >= kTrainingStart && seed < kTrainingEnd;
  const bool passed = finite && mirror_safe && seed_partition;
  output << "CYCLE_ABSTRACTION_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"finite\":" << (finite ? "true" : "false")
         << ",\"mirrorSafe\":" << (mirror_safe ? "true" : "false")
         << ",\"trainingSeedOnly\":"
         << (seed_partition ? "true" : "false")
         << ",\"features\":" << kFeatureCount << "}\n";
  return passed;
}

}  // namespace drop7::cycle_abstraction

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
      return drop7::cycle_abstraction::selfTest(std::cout) ? 0 : 1;
    }
    if (argc >= 2 && std::string(argv[1]) == "--run") {
      const auto config = drop7::cycle_abstraction::parseRunConfig(argc, argv);
      return drop7::cycle_abstraction::runExperiment(config, std::cout);
    }
    std::cerr
        << "usage: drop7_cycle_abstraction --self-test | --run "
           "[--train-games N] [--heldout-games N] [--base-moves N] "
           "[--rollouts N] [--horizon N] [--epochs N] [--output PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_cycle_abstraction: " << error.what() << '\n';
    return 1;
  }
}
