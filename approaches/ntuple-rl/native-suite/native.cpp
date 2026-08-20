#include "../../../src/core/native/engine.hpp"
#include "../../../src/core/native/ntuple.hpp"
#include "../../../src/core/native/ntuple-search.hpp"
#include "../../../src/core/native/ppo.hpp"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using Clock = std::chrono::steady_clock;
using drop7::MoveResult;
using drop7::State;

std::string valueAfter(int argc, char** argv, std::string_view flag,
                       std::string fallback = {}) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == flag) return argv[index + 1];
  }
  return fallback;
}

bool hasFlag(int argc, char** argv, std::string_view flag) {
  for (int index = 1; index < argc; ++index) {
    if (argv[index] == flag) return true;
  }
  return false;
}

std::uint32_t parseUint32(const std::string& value, std::string_view name) {
  if (value.empty()) throw std::invalid_argument(std::string(name) + " is required");
  std::size_t consumed = 0;
  const unsigned long long parsed = std::stoull(value, &consumed, 0);
  if (consumed != value.size() || parsed > 0xffff'ffffull) {
    throw std::invalid_argument(std::string(name) + " must be a uint32");
  }
  return static_cast<std::uint32_t>(parsed);
}

int parsePositive(const std::string& value, std::string_view name) {
  if (value.empty()) throw std::invalid_argument(std::string(name) + " is required");
  std::size_t consumed = 0;
  const long long parsed = std::stoll(value, &consumed, 10);
  if (consumed != value.size() || parsed < 1 ||
      parsed > std::numeric_limits<int>::max()) {
    throw std::invalid_argument(std::string(name) + " must be positive");
  }
  return static_cast<int>(parsed);
}

float parsePositiveFloat(const std::string& value, std::string_view name) {
  if (value.empty()) throw std::invalid_argument(std::string(name) + " is required");
  std::size_t consumed = 0;
  const float parsed = std::stof(value, &consumed);
  if (consumed != value.size() || !std::isfinite(parsed) || parsed <= 0) {
    throw std::invalid_argument(std::string(name) + " must be positive");
  }
  return parsed;
}

float parseNonnegativeFloat(const std::string& value, std::string_view name) {
  if (value.empty()) throw std::invalid_argument(std::string(name) + " is required");
  std::size_t consumed = 0;
  const float parsed = std::stof(value, &consumed);
  if (consumed != value.size() || !std::isfinite(parsed) || parsed < 0) {
    throw std::invalid_argument(std::string(name) + " must be nonnegative");
  }
  return parsed;
}

std::string traceLine(std::uint32_t seed, int disc, int column,
                      const MoveResult& move) {
  const State& state = move.state;
  std::ostringstream output;
  output << "{\"seed\":" << seed << ",\"move\":" << state.moves_played
         << ",\"disc\":" << disc << ",\"column\":" << column
         << ",\"scoreDelta\":" << move.score_delta << ",\"score\":"
         << state.score << ",\"level\":" << state.level
         << ",\"movesRemaining\":" << state.moves_remaining
         << ",\"gameOver\":" << (state.game_over ? "true" : "false")
         << ",\"clearedBoard\":"
         << (move.cleared_board ? "true" : "false")
         << ",\"levelAdvanced\":"
         << (move.level_advanced ? "true" : "false") << ",\"waves\":[";
  for (std::size_t index = 0; index < move.waves.size(); ++index) {
    if (index != 0) output << ',';
    const auto& wave = move.waves[index];
    output << "{\"depth\":" << wave.depth << ",\"cleared\":" << wave.cleared
           << ",\"revealed\":" << wave.revealed << ",\"points\":"
           << wave.points << '}';
  }
  output << "],\"board\":\"" << drop7::serializeBoard(state.board) << "\"}";
  return output.str();
}

int runTrace(int argc, char** argv) {
  const std::uint32_t seed =
      parseUint32(valueAfter(argc, argv, "--seed", "0"), "--seed");
  const int max_moves =
      parsePositive(valueAfter(argc, argv, "--moves", "500"), "--moves");
  State state = drop7::initialHeadlessState(seed);
  drop7::Mulberry32 action_random(drop7::mix32(seed ^ 0x5452'4143u));
  while (!state.game_over && state.moves_played < max_moves) {
    int legal_count = 0;
    const auto legal = drop7::legalColumns(state.board, legal_count);
    if (legal_count == 0) throw std::runtime_error("live state had no legal move");
    const int selected = static_cast<int>(
        (static_cast<std::uint64_t>(action_random.nextBits()) * legal_count) >>
        32);
    const int column = legal[selected];
    const int disc = state.next_disc;
    MoveResult move;
    if (!drop7::playHeadlessMove(state, seed, column, move)) {
      throw std::runtime_error("trace policy selected an illegal move");
    }
    std::cout << traceLine(seed, disc, column, move) << '\n';
  }
  return 0;
}

int runBenchmark(int argc, char** argv) {
  const int games =
      parsePositive(valueAfter(argc, argv, "--games", "10000"), "--games");
  const int max_moves =
      parsePositive(valueAfter(argc, argv, "--max-moves", "500"), "--max-moves");
  const std::uint32_t seed_start = parseUint32(
      valueAfter(argc, argv, "--seed-start", "0xa5700000"), "--seed-start");
  std::uint64_t total_moves = 0;
  std::uint64_t score_checksum = 0;
  std::uint64_t board_checksum = 0;
  const auto started = Clock::now();
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed = seed_start + static_cast<std::uint32_t>(game);
    State state = drop7::initialHeadlessState(seed);
    drop7::Mulberry32 action_random(drop7::mix32(seed ^ 0x4143'544eu));
    while (!state.game_over && state.moves_played < max_moves) {
      int legal_count = 0;
      const auto legal = drop7::legalColumns(state.board, legal_count);
      if (legal_count == 0) break;
      const int selected = static_cast<int>(
          (static_cast<std::uint64_t>(action_random.nextBits()) * legal_count) >>
          32);
      MoveResult move;
      if (!drop7::playHeadlessMove(state, seed, legal[selected], move)) {
        throw std::runtime_error("benchmark selected an illegal move");
      }
    }
    total_moves += static_cast<std::uint64_t>(state.moves_played);
    score_checksum ^= static_cast<std::uint64_t>(state.score) +
                      static_cast<std::uint64_t>(game) * 0x9e37'79b9u;
    for (std::uint8_t cell : state.board) {
      board_checksum = board_checksum * 131u + cell + 1u;
    }
  }
  const double seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  std::cout << std::fixed << std::setprecision(3)
            << "BENCHMARK {\"games\":" << games << ",\"moves\":"
            << total_moves << ",\"seconds\":" << seconds
            << ",\"movesPerSecond\":" << total_moves / seconds
            << ",\"scoreChecksum\":" << score_checksum
            << ",\"boardChecksum\":" << board_checksum << "}\n";
  return 0;
}

int runTrain(int argc, char** argv) {
  drop7::ppo::TrainingOptions options;
  options.iterations = parsePositive(
      valueAfter(argc, argv, "--iterations", std::to_string(options.iterations)),
      "--iterations");
  options.episodes_per_iteration = parsePositive(
      valueAfter(argc, argv, "--episodes",
                 std::to_string(options.episodes_per_iteration)),
      "--episodes");
  options.threads = parsePositive(
      valueAfter(argc, argv, "--threads", std::to_string(options.threads)),
      "--threads");
  options.max_moves = parsePositive(
      valueAfter(argc, argv, "--max-moves", std::to_string(options.max_moves)),
      "--max-moves");
  options.epochs = parsePositive(
      valueAfter(argc, argv, "--epochs", std::to_string(options.epochs)),
      "--epochs");
  options.minibatch_size = parsePositive(
      valueAfter(argc, argv, "--minibatch",
                 std::to_string(options.minibatch_size)),
      "--minibatch");
  options.probe_games = parsePositive(
      valueAfter(argc, argv, "--probe-games",
                 std::to_string(options.probe_games)),
      "--probe-games");
  options.probe_every = parsePositive(
      valueAfter(argc, argv, "--probe-every",
                 std::to_string(options.probe_every)),
      "--probe-every");
  options.training_seed_start = parseUint32(
      valueAfter(argc, argv, "--training-seed-start",
                 std::to_string(options.training_seed_start)),
      "--training-seed-start");
  options.probe_seed_start = parseUint32(
      valueAfter(argc, argv, "--probe-seed-start",
                 std::to_string(options.probe_seed_start)),
      "--probe-seed-start");
  options.network_seed = parseUint32(
      valueAfter(argc, argv, "--network-seed",
                 std::to_string(options.network_seed)),
      "--network-seed");
  options.gamma = parsePositiveFloat(
      valueAfter(argc, argv, "--gamma", std::to_string(options.gamma)),
      "--gamma");
  options.gae_lambda = parsePositiveFloat(
      valueAfter(argc, argv, "--gae-lambda",
                 std::to_string(options.gae_lambda)),
      "--gae-lambda");
  options.learning_rate = parsePositiveFloat(
      valueAfter(argc, argv, "--learning-rate",
                 std::to_string(options.learning_rate)),
      "--learning-rate");
  options.clip_ratio = parsePositiveFloat(
      valueAfter(argc, argv, "--clip-ratio",
                 std::to_string(options.clip_ratio)),
      "--clip-ratio");
  options.entropy_coefficient = parsePositiveFloat(
      valueAfter(argc, argv, "--entropy",
                 std::to_string(options.entropy_coefficient)),
      "--entropy");
  options.value_coefficient = parsePositiveFloat(
      valueAfter(argc, argv, "--value-coefficient",
                 std::to_string(options.value_coefficient)),
      "--value-coefficient");
  options.gradient_norm = parsePositiveFloat(
      valueAfter(argc, argv, "--gradient-norm",
                 std::to_string(options.gradient_norm)),
      "--gradient-norm");
  options.checkpoint =
      valueAfter(argc, argv, "--checkpoint", options.checkpoint);
  return drop7::ppo::train(options);
}

int runNtupleTrain(int argc, char** argv) {
  drop7::ntuple::Options options;
  options.training_games = parsePositive(
      valueAfter(argc, argv, "--games", std::to_string(options.training_games)),
      "--games");
  options.probe_games = parsePositive(
      valueAfter(argc, argv, "--probe-games",
                 std::to_string(options.probe_games)),
      "--probe-games");
  options.max_moves = parsePositive(
      valueAfter(argc, argv, "--max-moves", std::to_string(options.max_moves)),
      "--max-moves");
  options.chance_samples = parsePositive(
      valueAfter(argc, argv, "--chance-samples",
                 std::to_string(options.chance_samples)),
      "--chance-samples");
  options.report_every = parsePositive(
      valueAfter(argc, argv, "--report-every",
                 std::to_string(options.report_every)),
      "--report-every");
  options.training_seed_start = parseUint32(
      valueAfter(argc, argv, "--training-seed-start",
                 std::to_string(options.training_seed_start)),
      "--training-seed-start");
  options.probe_seed_start = parseUint32(
      valueAfter(argc, argv, "--probe-seed-start",
                 std::to_string(options.probe_seed_start)),
      "--probe-seed-start");
  options.gamma = parsePositiveFloat(
      valueAfter(argc, argv, "--gamma", std::to_string(options.gamma)),
      "--gamma");
  options.lambda = parseNonnegativeFloat(
      valueAfter(argc, argv, "--lambda", std::to_string(options.lambda)),
      "--lambda");
  options.learning_rate = parsePositiveFloat(
      valueAfter(argc, argv, "--learning-rate",
                 std::to_string(options.learning_rate)),
      "--learning-rate");
  options.epsilon = parseNonnegativeFloat(
      valueAfter(argc, argv, "--epsilon", std::to_string(options.epsilon)),
      "--epsilon");
  options.optimistic_value = parseNonnegativeFloat(
      valueAfter(argc, argv, "--optimistic-value",
                 std::to_string(options.optimistic_value)),
      "--optimistic-value");
  options.checkpoint = valueAfter(argc, argv, "--checkpoint", "");
  options.resume = valueAfter(argc, argv, "--resume", "");
  options.disc_independent = hasFlag(argc, argv, "--chance-state");
  options.direct_score_reward = hasFlag(argc, argv, "--score-reward");
  options.absolute_position = hasFlag(argc, argv, "--absolute-position");
  options.hierarchical = hasFlag(argc, argv, "--hierarchical");
  options.warm_start_shared =
      valueAfter(argc, argv, "--warm-start-shared", "");
  return drop7::ntuple::train(options);
}

int runNtupleSearch(int argc, char** argv) {
  drop7::ntuple::search::Options options;
  options.depth = parsePositive(
      valueAfter(argc, argv, "--depth", std::to_string(options.depth)),
      "--depth");
  options.reveal_samples = parsePositive(
      valueAfter(argc, argv, "--reveal-samples",
                 std::to_string(options.reveal_samples)),
      "--reveal-samples");
  options.internal_action_width = parsePositive(
      valueAfter(argc, argv, "--internal-action-width",
                 std::to_string(options.internal_action_width)),
      "--internal-action-width");
  options.max_work = static_cast<std::uint64_t>(parsePositive(
      valueAfter(argc, argv, "--max-work", std::to_string(options.max_work)),
      "--max-work"));
  options.games = parsePositive(
      valueAfter(argc, argv, "--games", std::to_string(options.games)),
      "--games");
  options.max_moves = parsePositive(
      valueAfter(argc, argv, "--max-moves", std::to_string(options.max_moves)),
      "--max-moves");
  options.seed_start = parseUint32(
      valueAfter(argc, argv, "--seed-start",
                 std::to_string(options.seed_start)),
      "--seed-start");
  options.checkpoint =
      valueAfter(argc, argv, "--checkpoint", options.checkpoint);
  return drop7::ntuple::search::benchmark(options);
}

void printUsage() {
  std::cerr
      << "Usage:\n"
      << "  drop7_native --trace [--seed N] [--moves N]\n"
      << "  drop7_native --benchmark [--games N] [--max-moves N] "
         "[--seed-start N]\n"
      << "  drop7_native --train [--iterations N] [--episodes N] "
         "[--threads N] [--checkpoint PATH]\n"
      << "  drop7_native --gradient-check\n";
  std::cerr
      << "  drop7_native --ntuple-self-test\n"
      << "  drop7_native --train-ntuple [--games N] [--chance-samples N] "
         "[--learning-rate X] [--epsilon X] [--optimistic-value X]\n";
  std::cerr
      << "  drop7_native --ntuple-search-self-test\n"
      << "  drop7_native --benchmark-ntuple-search [--depth N] "
         "[--reveal-samples N] [--internal-action-width N] [--max-work N]\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (hasFlag(argc, argv, "--trace")) return runTrace(argc, argv);
    if (hasFlag(argc, argv, "--benchmark")) return runBenchmark(argc, argv);
    if (hasFlag(argc, argv, "--gradient-check")) {
      return drop7::ppo::gradientCheck(std::cout) ? 0 : 1;
    }
    if (hasFlag(argc, argv, "--ntuple-self-test")) {
      return drop7::ntuple::selfTest(std::cout) ? 0 : 1;
    }
    if (hasFlag(argc, argv, "--ntuple-search-self-test")) {
      return drop7::ntuple::search::selfTest(std::cout) ? 0 : 1;
    }
    if (hasFlag(argc, argv, "--benchmark-ntuple-search")) {
      return runNtupleSearch(argc, argv);
    }
    if (hasFlag(argc, argv, "--train-ntuple")) {
      return runNtupleTrain(argc, argv);
    }
    if (hasFlag(argc, argv, "--train")) return runTrain(argc, argv);
    printUsage();
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
