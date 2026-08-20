#include "../../../src/core/native/public-behavior.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using drop7::MoveResult;
using drop7::State;

std::string valueAfter(int argc, char** argv, std::string_view flag,
                       std::string fallback) {
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

int parseInt(const std::string& text, std::string_view flag, int minimum) {
  std::size_t consumed = 0;
  const long long value = std::stoll(text, &consumed, 0);
  if (consumed != text.size() || value < minimum ||
      value > std::numeric_limits<int>::max()) {
    throw std::invalid_argument(std::string(flag) + " is out of range");
  }
  return static_cast<int>(value);
}

double parseDouble(const std::string& text, std::string_view flag) {
  std::size_t consumed = 0;
  const double value = std::stod(text, &consumed);
  if (consumed != text.size() || !std::isfinite(value)) {
    throw std::invalid_argument(std::string(flag) + " must be finite");
  }
  return value;
}

std::uint32_t parseSeed(const std::string& text) {
  std::size_t consumed = 0;
  const unsigned long long value = std::stoull(text, &consumed, 0);
  if (consumed != text.size() || value > 0xffff'ffffull) {
    throw std::invalid_argument("--seed-start must be a uint32");
  }
  return static_cast<std::uint32_t>(value);
}

struct Result {
  std::int64_t score = 0;
  int moves = 0;
  int numbered_clears = 0;
  int reveals = 0;
  int maximum_height = 0;
  std::uint64_t work = 0;
  bool censored = false;
};

Result runGame(std::uint32_t seed,
               const drop7::cfpi::BehaviorOptions& options, int max_moves,
               bool trace) {
  State state = drop7::initialHeadlessState(seed);
  Result result;
  while (!state.game_over && state.moves_played < max_moves) {
    drop7::cfpi::BehaviorMetrics metrics;
    const int action =
        drop7::cfpi::chooseBehaviorAction(state, options, &metrics);
    if (action < 0) throw std::runtime_error("behavior returned no action");
    const int disc = state.next_disc;
    MoveResult move;
    if (!drop7::playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("behavior returned an illegal action");
    }
    result.work += metrics.work;
    for (const auto& wave : move.waves) {
      result.numbered_clears += wave.cleared;
      result.reveals += wave.revealed;
    }
    const auto phase = drop7::cfpi::evaluatePhaseMetrics(state);
    result.maximum_height = std::max(result.maximum_height,
                                     phase.maximum_height);
    if (trace) {
      std::cout << "TRACE {\"move\":" << state.moves_played
                << ",\"disc\":" << disc << ",\"column\":" << action
                << ",\"score\":" << state.score
                << ",\"height\":" << phase.maximum_height
                << ",\"occupied\":" << phase.occupied
                << ",\"covers\":" << phase.covers
                << ",\"potential\":" << phase.potential
                << ",\"board\":\"" << drop7::serializeBoard(state.board)
                << "\"}\n";
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  return result;
}

int run(int argc, char** argv) {
  const int games = parseInt(valueAfter(argc, argv, "--games", "8"),
                             "--games", 1);
  const int max_moves = parseInt(
      valueAfter(argc, argv, "--max-moves", "1000"), "--max-moves", 1);
  const std::uint32_t seed_start =
      parseSeed(valueAfter(argc, argv, "--seed-start", "0x3d700100"));
  drop7::cfpi::BehaviorOptions options;
  options.max_depth = parseInt(valueAfter(argc, argv, "--depth", "3"),
                               "--depth", 1);
  options.chance_samples = parseInt(
      valueAfter(argc, argv, "--samples", "5"), "--samples", 1);
  options.max_work = static_cast<std::uint64_t>(parseInt(
      valueAfter(argc, argv, "--max-work", "1000000"), "--max-work", 1));
  options.terminal_utility = parseDouble(
      valueAfter(argc, argv, "--terminal-utility", "-1000000"),
      "--terminal-utility");
  const bool trace = hasFlag(argc, argv, "--trace");

  std::vector<Result> results;
  results.reserve(games);
  const auto started = Clock::now();
  for (int game = 0; game < games; ++game) {
    const auto seed = seed_start + static_cast<std::uint32_t>(game);
    const Result result = runGame(seed, options, max_moves, trace);
    results.push_back(result);
    std::cerr << (game + 1) << '/' << games << " seed 0x" << std::hex
              << seed << std::dec << ' ' << result.score << " ("
              << result.moves << " moves)\n";
  }
  const double seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  const auto mean = [&](auto project) {
    return std::accumulate(results.begin(), results.end(), 0.0,
                           [&](double sum, const Result& result) {
                             return sum + project(result);
                           }) /
           results.size();
  };
  const double mean_moves = mean([](const Result& result) {
    return static_cast<double>(result.moves);
  });
  std::cout << std::fixed << std::setprecision(3)
            << "PHASE_BENCHMARK {\"games\":" << games
            << ",\"meanScore\":"
            << mean([](const Result& result) {
                 return static_cast<double>(result.score);
               })
            << ",\"meanMoves\":" << mean_moves
            << ",\"clearsPerMove\":"
            << mean([](const Result& result) {
                 return static_cast<double>(result.numbered_clears);
               }) /
                   mean_moves
            << ",\"revealsPerMove\":"
            << mean([](const Result& result) {
                 return static_cast<double>(result.reveals);
               }) /
                   mean_moves
            << ",\"meanMaximumHeight\":"
            << mean([](const Result& result) {
                 return static_cast<double>(result.maximum_height);
               })
            << ",\"workPerMove\":"
            << mean([](const Result& result) {
                 return static_cast<double>(result.work);
               }) /
                   mean_moves
            << ",\"seconds\":" << seconds << ",\"censored\":"
            << std::count_if(results.begin(), results.end(),
                             [](const Result& result) {
                               return result.censored;
                             })
            << "}\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "drop7_phase_benchmark: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
