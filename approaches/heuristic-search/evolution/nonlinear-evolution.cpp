// Reuses the 40-feature action extractor from the linear policy.  Its CLI main
// is renamed here; this executable owns all seed selection and never calls the
// embedded training or probe entry points.
#define main drop7_evolution_embedded_cli
#include "evolution.cpp"
#undef main

#include "../../../src/core/native/public-behavior.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace drop7::nonlinear_evolution {

namespace evo = drop7::evolution;

constexpr int kHiddenUnits = 12;
constexpr int kInputs = static_cast<int>(evo::kFeatureCount);
constexpr int kParameterCount = kHiddenUnits * kInputs + kHiddenUnits +
                                kHiddenUnits + 1;

using Parameters = std::array<double, kParameterCount>;

struct Standardizer {
  evo::Features mean{};
  evo::Features scale{};

  Standardizer() { scale.fill(1); }

  evo::Features apply(const evo::Features& input) const {
    evo::Features result{};
    for (int index = 0; index < kInputs; ++index) {
      result[index] = (input[index] - mean[index]) / scale[index];
    }
    return result;
  }
};

inline std::uint32_t actionHash(const State& state, int column) {
  std::uint32_t hash = 0x811c'9dc5u;
  for (std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint32_t>(cell) + 1u;
    hash *= 0x0100'0193u;
  }
  hash ^= static_cast<std::uint32_t>(state.next_disc) * 0x9e37'79b9u;
  hash ^= static_cast<std::uint32_t>(state.moves_remaining) * 0x85eb'ca6bu;
  hash ^= static_cast<std::uint32_t>(column + 1) * 0x27d4'eb2fu;
  return mix32(hash);
}

evo::Features actionFeaturesCanonical(const State& canonical_state, int column,
                                      int chance_probes) {
  if (!isLegal(canonical_state.board, column)) {
    throw std::invalid_argument("cannot extract features for illegal action");
  }
  if (chance_probes < 1 || chance_probes > 7) {
    throw std::invalid_argument("chance probes must be in [1, 7]");
  }
  const evo::BoardAnalysis before = evo::analyzeBoard(canonical_state);
  evo::Features aggregate{};
  double top_sum = 0;
  double top_square_sum = 0;
  double worst_top = 0;
  const std::uint32_t base_hash = actionHash(canonical_state, column);
  for (int probe = 0; probe < chance_probes; ++probe) {
    const int stratum = std::min(
        6, static_cast<int>((probe + 0.5) * 7.0 / chance_probes));
    const int reveal_target = (stratum + 2 * column) % 7 + 1;
    const int next_target = (3 * stratum + column) % 7 + 1;
    const std::uint32_t chance_seed = evo::seedWithFirstDisc(
        mix32(base_hash ^
              (static_cast<std::uint32_t>(stratum + 1) * 0x9e37'79b9u)),
        reveal_target);
    Mulberry32 chance(chance_seed);
    MoveResult move;
    if (!playMove(canonical_state, column, chance, move)) {
      throw std::runtime_error("feature probe rejected legal action");
    }
    if (!move.state.game_over) {
      move.state.next_disc = static_cast<std::uint8_t>(next_target);
    }
    const evo::BoardAnalysis after = evo::analyzeBoard(move.state);
    evo::Features sample = after.features;
    int clears = 0;
    int reveals = 0;
    int maximum_depth = 0;
    for (const Wave& wave : move.waves) {
      clears += wave.cleared;
      reveals += wave.revealed;
      maximum_depth = std::max(maximum_depth, wave.depth);
    }
    const int raised_covers = move.level_advanced ? kBoardSize : 0;
    const int solid_progress = std::max(
        0, before.solids + raised_covers - after.solids - reveals);
    sample[evo::kImmediateScore] =
        std::min(8.0, move.score_delta / static_cast<double>(kLevelBonus));
    sample[evo::kImmediateClears] = clears / 7.0;
    sample[evo::kImmediateReveals] = reveals / 7.0;
    sample[evo::kImmediateCrackProgress] = solid_progress / 7.0;
    sample[evo::kChainDepth] = maximum_depth / 8.0;
    sample[evo::kBoardClear] = move.cleared_board ? 1.0 : 0.0;
    sample[evo::kDeath] = move.state.game_over ? 1.0 : 0.0;
    sample[evo::kLevelAdvance] = move.level_advanced ? 1.0 : 0.0;
    sample[evo::kOccupancyReduction] =
        (before.occupied + 1 + raised_covers - after.occupied) / 14.0;
    sample[evo::kCoverReduction] =
        (before.covers + raised_covers - after.covers) / 14.0;
    for (int index = 0; index < kInputs; ++index) {
      aggregate[index] += sample[index];
    }
    top_sum += after.top_load;
    top_square_sum += after.top_load * after.top_load;
    worst_top = std::max(worst_top, after.top_load);
  }
  const double inverse = 1.0 / chance_probes;
  for (double& value : aggregate) value *= inverse;
  const double top_mean = top_sum * inverse;
  aggregate[evo::kWorstTopLoad] = worst_top / 14.0;
  aggregate[evo::kOutcomeDispersion] =
      std::sqrt(std::max(0.0, top_square_sum * inverse -
                                  top_mean * top_mean)) /
      14.0;
  return aggregate;
}

evo::Features actionFeatures(const State& state, int physical_column,
                             int chance_probes) {
  const evo::CanonicalState canonical = evo::canonicalize(state);
  const int column = evo::canonicalColumn(canonical, physical_column);
  return actionFeaturesCanonical(canonical.state, column, chance_probes);
}

double nonlinearScore(const evo::Features& raw, const Standardizer& standardizer,
                      const Parameters& parameters) {
  const evo::Features input = standardizer.apply(raw);
  constexpr int hidden_weights = kHiddenUnits * kInputs;
  constexpr int hidden_biases = hidden_weights;
  constexpr int output_weights = hidden_biases + kHiddenUnits;
  constexpr int output_bias = output_weights + kHiddenUnits;
  double result = parameters[output_bias];
  for (int hidden = 0; hidden < kHiddenUnits; ++hidden) {
    double activation = parameters[hidden_biases + hidden];
    for (int feature = 0; feature < kInputs; ++feature) {
      activation += parameters[hidden * kInputs + feature] * input[feature];
    }
    result += parameters[output_weights + hidden] * std::tanh(activation);
  }
  return result;
}

double scoreAction(const State& state, int column, int chance_probes,
                   const Standardizer& standardizer,
                   const Parameters& parameters) {
  return nonlinearScore(actionFeatures(state, column, chance_probes),
                        standardizer, parameters);
}

int selectAction(const State& state, int chance_probes,
                 const Standardizer& standardizer,
                 const Parameters& parameters) {
  const evo::CanonicalState canonical = evo::canonicalize(state);
  constexpr std::array<int, kBoardSize> order{{3, 2, 4, 1, 5, 0, 6}};
  int selected = -1;
  double best = -std::numeric_limits<double>::infinity();
  for (int column : order) {
    if (!isLegal(canonical.state.board, column)) continue;
    const double value = nonlinearScore(
        actionFeaturesCanonical(canonical.state, column, chance_probes),
        standardizer, parameters);
    if (value > best + 1e-12) {
      best = value;
      selected = column;
    }
  }
  if (selected < 0) return -1;
  return canonical.reflected ? kBoardSize - 1 - selected : selected;
}

struct ActionSample {
  int column = -1;
  evo::Features features{};
};

struct CloneExample {
  std::array<ActionSample, kBoardSize> actions{};
  int action_count = 0;
  int teacher_index = -1;
};

struct CloneMetrics {
  double loss = 0;
  double accuracy = 0;
  int examples = 0;
};

struct PilotOptions {
  int clone_games = 4;
  int holdout_games = 2;
  int clone_epochs = 80;
  int clone_chance_probes = 3;
  int policy_chance_probes = 7;
  int screen_games = 4;
  int confirmation_games = 8;
  int max_moves = 500;
  int generations = 3;
  int antithetic_pairs = 6;
  int evolution_games = 4;
  double clone_learning_rate = 0.003;
  double sigma = 0.05;
  double nes_step = 0.03;
  double screen_ratio = 0.85;
  double minimum_screen_score = 250'000;
};

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  int clears = 0;
  int reveals = 0;
  bool censored = false;
  std::uint64_t teacher_work = 0;
};

struct GameSummary {
  double mean_score = 0;
  double median_score = 0;
  double mean_moves = 0;
  double clear_rate = 0;
  double reveal_rate = 0;
  std::vector<GameResult> games;

  double objective() const {
    return 0.7 * mean_score + 0.3 * median_score + 500.0 * mean_moves;
  }
};

struct ForwardPass {
  std::array<double, kHiddenUnits> hidden{};
  double score = 0;
};

ForwardPass forward(const evo::Features& raw, const Standardizer& standardizer,
                    const Parameters& parameters) {
  const evo::Features input = standardizer.apply(raw);
  constexpr int hidden_weights = kHiddenUnits * kInputs;
  constexpr int hidden_biases = hidden_weights;
  constexpr int output_weights = hidden_biases + kHiddenUnits;
  constexpr int output_bias = output_weights + kHiddenUnits;
  ForwardPass pass;
  pass.score = parameters[output_bias];
  for (int hidden = 0; hidden < kHiddenUnits; ++hidden) {
    double activation = parameters[hidden_biases + hidden];
    for (int feature = 0; feature < kInputs; ++feature) {
      activation += parameters[hidden * kInputs + feature] * input[feature];
    }
    pass.hidden[hidden] = std::tanh(activation);
    pass.score += parameters[output_weights + hidden] * pass.hidden[hidden];
  }
  return pass;
}

Parameters initializeParameters(std::uint32_t seed) {
  Parameters parameters{};
  Mulberry32 random(seed);
  const double hidden_scale = std::sqrt(6.0 / (kInputs + kHiddenUnits));
  const double output_scale = std::sqrt(6.0 / (kHiddenUnits + 1));
  constexpr int hidden_weights = kHiddenUnits * kInputs;
  constexpr int hidden_biases = hidden_weights;
  constexpr int output_weights = hidden_biases + kHiddenUnits;
  for (int index = 0; index < hidden_weights; ++index) {
    parameters[index] = (2.0 * random.nextUnit() - 1.0) * hidden_scale;
  }
  for (int hidden = 0; hidden < kHiddenUnits; ++hidden) {
    parameters[output_weights + hidden] =
        (2.0 * random.nextUnit() - 1.0) * output_scale;
  }
  return parameters;
}

void collectCloneGame(std::uint32_t seed, int max_moves, int chance_probes,
                      std::vector<CloneExample>& examples,
                      GameResult& result) {
  State state = initialHeadlessState(seed);
  result.seed = seed;
  while (!state.game_over && state.moves_played < max_moves) {
    cfpi::BehaviorMetrics teacher_metrics;
    const int teacher = cfpi::chooseBehaviorAction(state, {}, &teacher_metrics);
    result.teacher_work += teacher_metrics.work;
    CloneExample example;
    for (int column = 0; column < kBoardSize; ++column) {
      if (!isLegal(state.board, column)) continue;
      const int index = example.action_count++;
      example.actions[index] =
          {column, actionFeatures(state, column, chance_probes)};
      if (column == teacher) example.teacher_index = index;
    }
    if (example.teacher_index < 0) {
      throw std::runtime_error("teacher action missing from clone candidates");
    }
    examples.push_back(example);
    MoveResult move;
    if (!playHeadlessMove(state, seed, teacher, move)) {
      throw std::runtime_error("teacher selected illegal clone action");
    }
    for (const Wave& wave : move.waves) {
      result.clears += wave.cleared;
      result.reveals += wave.revealed;
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
}

Standardizer fitStandardizer(const std::vector<CloneExample>& examples) {
  if (examples.empty()) throw std::invalid_argument("empty clone dataset");
  Standardizer result;
  std::size_t count = 0;
  for (const CloneExample& example : examples) {
    for (int action = 0; action < example.action_count; ++action) {
      for (int feature = 0; feature < kInputs; ++feature) {
        result.mean[feature] += example.actions[action].features[feature];
      }
      ++count;
    }
  }
  for (double& mean : result.mean) mean /= count;
  evo::Features variance{};
  for (const CloneExample& example : examples) {
    for (int action = 0; action < example.action_count; ++action) {
      for (int feature = 0; feature < kInputs; ++feature) {
        const double difference =
            example.actions[action].features[feature] - result.mean[feature];
        variance[feature] += difference * difference;
      }
    }
  }
  for (int feature = 0; feature < kInputs; ++feature) {
    const double deviation = std::sqrt(variance[feature] / count);
    result.scale[feature] = deviation < 1e-6 ? 1.0 : deviation;
  }
  return result;
}

CloneMetrics cloneMetrics(const std::vector<CloneExample>& examples,
                          const Standardizer& standardizer,
                          const Parameters& parameters) {
  CloneMetrics metrics;
  metrics.examples = static_cast<int>(examples.size());
  for (const CloneExample& example : examples) {
    std::array<double, kBoardSize> scores{};
    double maximum = -std::numeric_limits<double>::infinity();
    int selected = 0;
    for (int action = 0; action < example.action_count; ++action) {
      scores[action] = forward(example.actions[action].features, standardizer,
                               parameters)
                           .score;
      if (scores[action] > maximum) {
        maximum = scores[action];
        selected = action;
      }
    }
    double denominator = 0;
    for (int action = 0; action < example.action_count; ++action) {
      denominator += std::exp(scores[action] - maximum);
    }
    const double probability =
        std::exp(scores[example.teacher_index] - maximum) / denominator;
    metrics.loss -= std::log(std::max(1e-12, probability));
    metrics.accuracy += selected == example.teacher_index ? 1.0 : 0.0;
  }
  metrics.loss /= examples.size();
  metrics.accuracy /= examples.size();
  return metrics;
}

void trainClone(const std::vector<CloneExample>& examples,
                const Standardizer& standardizer, int epochs,
                double learning_rate, Parameters& parameters) {
  Parameters first_moment{};
  Parameters second_moment{};
  std::uint64_t step = 0;
  constexpr int hidden_weights = kHiddenUnits * kInputs;
  constexpr int hidden_biases = hidden_weights;
  constexpr int output_weights = hidden_biases + kHiddenUnits;
  constexpr int output_bias = output_weights + kHiddenUnits;
  for (int epoch = 0; epoch < epochs; ++epoch) {
    for (std::size_t offset = 0; offset < examples.size(); ++offset) {
      const CloneExample& example =
          examples[(offset + static_cast<std::size_t>(epoch) * 7'919u) %
                   examples.size()];
      std::array<ForwardPass, kBoardSize> passes{};
      std::array<double, kBoardSize> scores{};
      double maximum = -std::numeric_limits<double>::infinity();
      for (int action = 0; action < example.action_count; ++action) {
        passes[action] = forward(example.actions[action].features, standardizer,
                                 parameters);
        scores[action] = passes[action].score;
        maximum = std::max(maximum, scores[action]);
      }
      double denominator = 0;
      for (int action = 0; action < example.action_count; ++action) {
        denominator += std::exp(scores[action] - maximum);
      }
      Parameters gradient{};
      for (int action = 0; action < example.action_count; ++action) {
        const double probability = std::exp(scores[action] - maximum) /
                                   denominator;
        const double score_gradient =
            probability - (action == example.teacher_index ? 1.0 : 0.0);
        gradient[output_bias] += score_gradient;
        const evo::Features input =
            standardizer.apply(example.actions[action].features);
        for (int hidden = 0; hidden < kHiddenUnits; ++hidden) {
          gradient[output_weights + hidden] +=
              score_gradient * passes[action].hidden[hidden];
          const double activation_gradient =
              score_gradient * parameters[output_weights + hidden] *
              (1.0 - passes[action].hidden[hidden] *
                         passes[action].hidden[hidden]);
          gradient[hidden_biases + hidden] += activation_gradient;
          for (int feature = 0; feature < kInputs; ++feature) {
            gradient[hidden * kInputs + feature] +=
                activation_gradient * input[feature];
          }
        }
      }
      ++step;
      const double first_decay = 1.0 - std::pow(0.9, step);
      const double second_decay = 1.0 - std::pow(0.999, step);
      for (int index = 0; index < kParameterCount; ++index) {
        first_moment[index] =
            0.9 * first_moment[index] + 0.1 * gradient[index];
        second_moment[index] =
            0.999 * second_moment[index] +
            0.001 * gradient[index] * gradient[index];
        parameters[index] -=
            learning_rate * (first_moment[index] / first_decay) /
            (std::sqrt(second_moment[index] / second_decay) + 1e-8);
      }
    }
  }
}

GameResult runNetworkGame(std::uint32_t seed, int max_moves, int chance_probes,
                          const Standardizer& standardizer,
                          const Parameters& parameters) {
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < max_moves) {
    const int action =
        selectAction(state, chance_probes, standardizer, parameters);
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("nonlinear policy selected illegal action");
    }
    for (const Wave& wave : move.waves) {
      result.clears += wave.cleared;
      result.reveals += wave.revealed;
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  return result;
}

GameResult runBehaviorGame(std::uint32_t seed, int max_moves) {
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < max_moves) {
    cfpi::BehaviorMetrics metrics;
    const int action = cfpi::chooseBehaviorAction(state, {}, &metrics);
    result.teacher_work += metrics.work;
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("behavior policy selected illegal action");
    }
    for (const Wave& wave : move.waves) {
      result.clears += wave.cleared;
      result.reveals += wave.revealed;
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  return result;
}

GameSummary summarize(std::vector<GameResult> games) {
  GameSummary result;
  result.games = std::move(games);
  std::vector<std::int64_t> scores;
  std::uint64_t moves = 0;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
  for (const GameResult& game : result.games) {
    result.mean_score += game.score;
    result.mean_moves += game.moves;
    scores.push_back(game.score);
    moves += static_cast<std::uint64_t>(game.moves);
    clears += static_cast<std::uint64_t>(game.clears);
    reveals += static_cast<std::uint64_t>(game.reveals);
  }
  result.mean_score /= result.games.size();
  result.mean_moves /= result.games.size();
  std::sort(scores.begin(), scores.end());
  const std::size_t middle = scores.size() / 2;
  result.median_score = scores.size() % 2 == 0
                            ? (scores[middle - 1] + scores[middle]) / 2.0
                            : scores[middle];
  result.clear_rate = static_cast<double>(clears) / std::max<std::uint64_t>(1, moves);
  result.reveal_rate =
      static_cast<double>(reveals) / std::max<std::uint64_t>(1, moves);
  return result;
}

void printGameSummary(std::string_view tag, const GameSummary& summary) {
  std::uint64_t teacher_work = 0;
  for (const GameResult& game : summary.games) teacher_work += game.teacher_work;
  std::cout << std::fixed << std::setprecision(3) << tag
            << " {\"games\":" << summary.games.size()
            << ",\"meanScore\":" << summary.mean_score
            << ",\"medianScore\":" << summary.median_score
            << ",\"meanMoves\":" << summary.mean_moves
            << ",\"clearRate\":" << summary.clear_rate
            << ",\"revealRate\":" << summary.reveal_rate
            << ",\"teacherWork\":" << teacher_work << "}\n";
}

void requireTrainingRange(std::uint32_t seed_start, int games) {
  const std::uint64_t end = static_cast<std::uint64_t>(seed_start) + games;
  const std::uint32_t prefix = seed_start >> 24;
  if ((prefix != 0x3du && prefix != 0x3eu) ||
      (static_cast<std::uint32_t>(end - 1) >> 24) != prefix) {
    throw std::invalid_argument(
        "nonlinear experiment is restricted to 0x3d/0x3e seeds");
  }
}

GameSummary evaluateNetwork(const Parameters& parameters,
                            const Standardizer& standardizer,
                            std::uint32_t seed_start, int games,
                            const PilotOptions& options) {
  requireTrainingRange(seed_start, games);
  std::vector<GameResult> results;
  results.reserve(games);
  for (int game = 0; game < games; ++game) {
    results.push_back(runNetworkGame(
        seed_start + static_cast<std::uint32_t>(game), options.max_moves,
        options.policy_chance_probes, standardizer, parameters));
  }
  return summarize(std::move(results));
}

struct PairedBounds {
  double score_lower_95 = 0;
  double moves_lower_95 = 0;
};

double pairedLower(const std::vector<double>& differences) {
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

PairedBounds pairedBounds(const GameSummary& behavior,
                          const GameSummary& candidate) {
  if (behavior.games.size() != candidate.games.size()) {
    throw std::invalid_argument("paired game summaries differ in size");
  }
  std::vector<double> scores;
  std::vector<double> moves;
  for (std::size_t index = 0; index < behavior.games.size(); ++index) {
    scores.push_back(candidate.games[index].score - behavior.games[index].score);
    moves.push_back(candidate.games[index].moves - behavior.games[index].moves);
  }
  return {pairedLower(scores), pairedLower(moves)};
}

Parameters evolve(Parameters current, const Standardizer& standardizer,
                  const PilotOptions& options) {
  evo::NormalRandom normal(0x6e45'5301u);
  for (int generation = 0; generation < options.generations; ++generation) {
    const std::uint32_t seed_start =
        0x3d82'0000u + static_cast<std::uint32_t>(generation * 0x1'0000);
    Parameters gradient{};
    double pair_scale = 0;
    for (int pair = 0; pair < options.antithetic_pairs; ++pair) {
      Parameters direction{};
      Parameters positive = current;
      Parameters negative = current;
      for (int index = 0; index < kParameterCount; ++index) {
        direction[index] = normal.next();
        positive[index] += options.sigma * direction[index];
        negative[index] -= options.sigma * direction[index];
      }
      const double positive_fitness =
          evaluateNetwork(positive, standardizer, seed_start,
                          options.evolution_games, options)
              .objective();
      const double negative_fitness =
          evaluateNetwork(negative, standardizer, seed_start,
                          options.evolution_games, options)
              .objective();
      const double difference = positive_fitness - negative_fitness;
      pair_scale += std::abs(difference);
      for (int index = 0; index < kParameterCount; ++index) {
        gradient[index] += difference * direction[index];
      }
    }
    const double normalization =
        std::max(10'000.0, pair_scale / options.antithetic_pairs);
    Parameters proposal = current;
    for (int index = 0; index < kParameterCount; ++index) {
      proposal[index] = std::clamp(
          proposal[index] +
              options.nes_step * gradient[index] /
                  (options.antithetic_pairs * normalization),
          -8.0, 8.0);
    }
    const GameSummary incumbent = evaluateNetwork(
        current, standardizer, seed_start, options.evolution_games, options);
    const GameSummary challenger = evaluateNetwork(
        proposal, standardizer, seed_start, options.evolution_games, options);
    const bool accepted = challenger.objective() > incumbent.objective();
    if (accepted) current = proposal;
    std::cout << std::fixed << std::setprecision(3)
              << "NONLINEAR_NES {\"generation\":" << generation
              << ",\"seedStart\":" << seed_start
              << ",\"incumbentScore\":" << incumbent.mean_score
              << ",\"incumbentMoves\":" << incumbent.mean_moves
              << ",\"challengerScore\":" << challenger.mean_score
              << ",\"challengerMoves\":" << challenger.mean_moves
              << ",\"accepted\":" << (accepted ? "true" : "false")
              << "}\n";
  }
  return current;
}

int runPilot(const PilotOptions& options) {
  if (options.clone_games < 1 || options.holdout_games < 1 ||
      options.clone_epochs < 1 || options.clone_chance_probes < 1 ||
      options.clone_chance_probes > 7 || options.policy_chance_probes < 1 ||
      options.policy_chance_probes > 7 || options.screen_games < 2 ||
      options.confirmation_games < 2 || options.max_moves < 1 ||
      options.generations < 1 || options.antithetic_pairs < 1 ||
      options.evolution_games < 1 || options.clone_learning_rate <= 0 ||
      options.sigma <= 0 || options.nes_step <= 0 ||
      options.screen_ratio <= 0 || options.screen_ratio > 1 ||
      options.minimum_screen_score < 0) {
    throw std::invalid_argument("invalid nonlinear pilot options");
  }
  const auto started = evo::Clock::now();
  std::vector<CloneExample> training;
  std::vector<CloneExample> holdout;
  std::vector<GameResult> collection_games;
  for (int game = 0; game < options.clone_games; ++game) {
    GameResult result;
    collectCloneGame(0x3d7f'0000u + static_cast<std::uint32_t>(game),
                     options.max_moves, options.clone_chance_probes, training,
                     result);
    collection_games.push_back(result);
  }
  for (int game = 0; game < options.holdout_games; ++game) {
    GameResult result;
    collectCloneGame(0x3d80'0000u + static_cast<std::uint32_t>(game),
                     options.max_moves, options.clone_chance_probes, holdout,
                     result);
    collection_games.push_back(result);
  }
  const Standardizer standardizer = fitStandardizer(training);
  Parameters parameters = initializeParameters(0x6e4c'4301u);
  const CloneMetrics initial =
      cloneMetrics(training, standardizer, parameters);
  trainClone(training, standardizer, options.clone_epochs,
             options.clone_learning_rate, parameters);
  const CloneMetrics fitted = cloneMetrics(training, standardizer, parameters);
  const CloneMetrics heldout = cloneMetrics(holdout, standardizer, parameters);
  std::cout << std::fixed << std::setprecision(6)
            << "NONLINEAR_CONFIG {\"features\":" << kInputs
            << ",\"hidden\":" << kHiddenUnits
            << ",\"parameters\":" << kParameterCount
            << ",\"cloneGames\":" << options.clone_games
            << ",\"holdoutGames\":" << options.holdout_games
            << ",\"cloneEpochs\":" << options.clone_epochs
            << ",\"cloneChanceProbes\":"
            << options.clone_chance_probes
            << ",\"policyChanceProbes\":"
            << options.policy_chance_probes
            << ",\"cloneLearningRate\":"
            << options.clone_learning_rate
            << ",\"screenGames\":" << options.screen_games
            << ",\"screenRatio\":" << options.screen_ratio
            << ",\"minimumScreenScore\":"
            << options.minimum_screen_score
            << ",\"generations\":" << options.generations
            << ",\"antitheticPairs\":" << options.antithetic_pairs
            << ",\"evolutionGames\":" << options.evolution_games
            << ",\"sigma\":" << options.sigma
            << ",\"nesStep\":" << options.nes_step
            << ",\"maxMoves\":" << options.max_moves
            << ",\"cloneSeedStart\":" << 0x3d7f'0000u
            << ",\"holdoutSeedStart\":" << 0x3d80'0000u
            << ",\"screenSeedStart\":" << 0x3e74'0000u
            << ",\"confirmationSeedStart\":" << 0x3e75'0000u
            << ",\"seedRanges\":[\"0x3d\",\"0x3e\"]}\n"
            << "NONLINEAR_CLONE {\"trainingExamples\":" << training.size()
            << ",\"holdoutExamples\":" << holdout.size()
            << ",\"initialLoss\":" << initial.loss
            << ",\"initialAccuracy\":" << initial.accuracy
            << ",\"trainLoss\":" << fitted.loss
            << ",\"trainAccuracy\":" << fitted.accuracy
            << ",\"holdoutLoss\":" << heldout.loss
            << ",\"holdoutAccuracy\":" << heldout.accuracy << "}\n";

  const std::uint32_t screen_start = 0x3e74'0000u;
  std::vector<GameResult> behavior_games;
  behavior_games.reserve(options.screen_games);
  for (int game = 0; game < options.screen_games; ++game) {
    behavior_games.push_back(runBehaviorGame(
        screen_start + static_cast<std::uint32_t>(game), options.max_moves));
  }
  const GameSummary behavior = summarize(std::move(behavior_games));
  const GameSummary candidate = evaluateNetwork(
      parameters, standardizer, screen_start, options.screen_games, options);
  const PairedBounds screen_bounds = pairedBounds(behavior, candidate);
  printGameSummary("NONLINEAR_SCREEN_BEHAVIOR", behavior);
  printGameSummary("NONLINEAR_SCREEN_CANDIDATE", candidate);
  const bool imitation_fit = fitted.accuracy >= 0.55 && heldout.accuracy >= 0.40;
  const bool screen_pass =
      imitation_fit &&
      candidate.mean_score >=
          std::max(options.minimum_screen_score,
                   options.screen_ratio * behavior.mean_score) &&
      candidate.mean_moves >= behavior.mean_moves;
  std::cout << "NONLINEAR_FUNNEL {\"imitationFit\":"
            << (imitation_fit ? "true" : "false")
            << ",\"screenPass\":" << (screen_pass ? "true" : "false")
            << ",\"scoreLower95\":" << screen_bounds.score_lower_95
            << ",\"movesLower95\":" << screen_bounds.moves_lower_95
            << "}\n";
  if (!screen_pass) {
    const double seconds = std::chrono::duration<double>(evo::Clock::now() -
                                                         started)
                               .count();
    std::cout << "NONLINEAR_RESULT {\"qualified\":false,"
                 "\"stoppedAt\":\"screen\",\"seconds\":"
              << seconds << ",\"maxRssMiB\":" << evo::maximumResidentMiB()
              << "}\n";
    return 3;
  }

  parameters = evolve(parameters, standardizer, options);
  const std::uint32_t confirmation_start = 0x3e75'0000u;
  std::vector<GameResult> confirmation_behavior_games;
  confirmation_behavior_games.reserve(options.confirmation_games);
  for (int game = 0; game < options.confirmation_games; ++game) {
    confirmation_behavior_games.push_back(runBehaviorGame(
        confirmation_start + static_cast<std::uint32_t>(game),
        options.max_moves));
  }
  const GameSummary confirmation_behavior =
      summarize(std::move(confirmation_behavior_games));
  const GameSummary confirmation_candidate = evaluateNetwork(
      parameters, standardizer, confirmation_start,
      options.confirmation_games, options);
  const PairedBounds confirmation_bounds =
      pairedBounds(confirmation_behavior, confirmation_candidate);
  const bool qualified =
      confirmation_candidate.mean_score > confirmation_behavior.mean_score &&
      confirmation_candidate.mean_moves >= confirmation_behavior.mean_moves &&
      confirmation_bounds.score_lower_95 > 0 &&
      confirmation_bounds.moves_lower_95 >= 0;
  printGameSummary("NONLINEAR_CONFIRM_BEHAVIOR", confirmation_behavior);
  printGameSummary("NONLINEAR_CONFIRM_CANDIDATE", confirmation_candidate);
  const double seconds =
      std::chrono::duration<double>(evo::Clock::now() - started).count();
  std::cout << "NONLINEAR_RESULT {\"qualified\":"
            << (qualified ? "true" : "false")
            << ",\"stoppedAt\":\"confirmation\",\"scoreLower95\":"
            << confirmation_bounds.score_lower_95
            << ",\"movesLower95\":" << confirmation_bounds.moves_lower_95
            << ",\"seconds\":" << seconds
            << ",\"maxRssMiB\":" << evo::maximumResidentMiB() << "}\n";
  return qualified ? 0 : 3;
}

bool selfTest(std::ostream& output) {
  const bool features = evo::selfTest(output);
  const bool behavior = cfpi::selfTest(output);
  State state = initialHeadlessState(0x3d7f'0001u);
  MoveResult move;
  if (!playHeadlessMove(state, 0x3d7f'0001u, 1, move) ||
      !playHeadlessMove(state, 0x3d7f'0001u, 4, move)) {
    throw std::runtime_error("failed to construct nonlinear fixture");
  }
  Parameters parameters{};
  for (int index = 0; index < kParameterCount; ++index) {
    parameters[index] =
        (static_cast<int>(mix32(static_cast<std::uint32_t>(index + 1))) % 2001 -
         1000) /
        20'000.0;
  }
  Standardizer standardizer;
  const int first = selectAction(state, 7, standardizer, parameters);
  const int repeat = selectAction(state, 7, standardizer, parameters);
  State mirrored = state;
  mirrored.board = evo::reflectedBoard(state.board);
  const int reflected = selectAction(mirrored, 7, standardizer, parameters);
  const bool deterministic = first == repeat;
  const bool reflection_safe = reflected == kBoardSize - 1 - first;
  bool score_reflection_safe = true;
  bool finite = true;
  for (int column = 0; column < kBoardSize; ++column) {
    if (!isLegal(state.board, column)) continue;
    const double original =
        scoreAction(state, column, 7, standardizer, parameters);
    const double mirror_score = scoreAction(
        mirrored, kBoardSize - 1 - column, 7, standardizer, parameters);
    finite = finite && std::isfinite(original);
    if (std::abs(original - mirror_score) > 1e-10) {
      score_reflection_safe = false;
    }
  }
  State irrelevant = state;
  irrelevant.score += 999'999;
  irrelevant.level += 50;
  irrelevant.moves_played += 271;
  const bool rule_state_only =
      selectAction(irrelevant, 7, standardizer, parameters) == first;
  const bool legal = isLegal(state.board, first);
  const bool passed = features && behavior && deterministic && reflection_safe &&
                      score_reflection_safe && finite && rule_state_only && legal;
  output << "NONLINEAR_EVOLUTION_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"deterministic\":" << (deterministic ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe && score_reflection_safe ? "true" : "false")
         << ",\"ruleStateOnly\":" << (rule_state_only ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false")
         << ",\"features\":" << kInputs
         << ",\"hidden\":" << kHiddenUnits
         << ",\"parameters\":" << kParameterCount << "}\n";
  return passed;
}

}  // namespace drop7::nonlinear_evolution

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::nonlinear_evolution::selfTest(std::cout) ? EXIT_SUCCESS
                                                              : EXIT_FAILURE;
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
      drop7::nonlinear_evolution::PilotOptions options;
      options.clone_games = std::stoi(value_after(
          "--clone-games", std::to_string(options.clone_games)));
      options.holdout_games = std::stoi(value_after(
          "--holdout-games", std::to_string(options.holdout_games)));
      options.clone_epochs = std::stoi(value_after(
          "--clone-epochs", std::to_string(options.clone_epochs)));
      options.clone_chance_probes = std::stoi(value_after(
          "--clone-chance-probes",
          std::to_string(options.clone_chance_probes)));
      options.policy_chance_probes = std::stoi(value_after(
          "--policy-chance-probes",
          std::to_string(options.policy_chance_probes)));
      options.screen_games = std::stoi(value_after(
          "--screen-games", std::to_string(options.screen_games)));
      options.confirmation_games = std::stoi(value_after(
          "--confirmation-games",
          std::to_string(options.confirmation_games)));
      options.max_moves = std::stoi(value_after(
          "--max-moves", std::to_string(options.max_moves)));
      options.generations = std::stoi(value_after(
          "--generations", std::to_string(options.generations)));
      options.antithetic_pairs = std::stoi(value_after(
          "--antithetic-pairs",
          std::to_string(options.antithetic_pairs)));
      options.evolution_games = std::stoi(value_after(
          "--evolution-games", std::to_string(options.evolution_games)));
      options.clone_learning_rate = std::stod(value_after(
          "--clone-learning-rate",
          std::to_string(options.clone_learning_rate)));
      options.sigma =
          std::stod(value_after("--sigma", std::to_string(options.sigma)));
      options.nes_step = std::stod(value_after(
          "--nes-step", std::to_string(options.nes_step)));
      options.screen_ratio = std::stod(value_after(
          "--screen-ratio", std::to_string(options.screen_ratio)));
      options.minimum_screen_score = std::stod(value_after(
          "--minimum-screen-score",
          std::to_string(options.minimum_screen_score)));
      return drop7::nonlinear_evolution::runPilot(options);
    }
    std::cerr
        << "Usage: drop7_nonlinear_evolution --self-test | --pilot [options]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
