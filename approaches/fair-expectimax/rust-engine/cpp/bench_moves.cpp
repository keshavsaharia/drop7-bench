// Engine move-throughput benchmark for the C++ reference and fast engines,
// mirroring the Rust bench's moves mode: whole center-policy games over the
// Rust benchmark sub-block seeds, best of --repeats, peak RSS printed.
//
//   bench-moves --games N --repeats R
//
// Seed discipline: Rust benchmark sub-block 0xa5277000-0xa5277fff of the
// already-opened SEEDLEASE-A52-FAST development lease only.
//
// Additive file; modifies no existing repository source.

#include "src/core/native/engine.hpp"
#include "approaches/lifetime-objective/fast-engine/fast-engine.hpp"
#include "approaches/lifetime-objective/fast-engine/corpus.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <vector>

namespace {

using namespace drop7;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kBenchSeeds = 0xa527'7000u;

struct Outcome {
  double seconds = 0;
  std::uint64_t moves = 0;
  std::uint64_t waves = 0;
  std::int64_t score = 0;
};

template <bool kFast>
void playOne(std::uint32_t seed, int maximum_moves, std::uint64_t& moves,
             std::uint64_t& waves, std::int64_t& score) {
  State state = initialHeadlessState(seed);
  while (!state.game_over && state.moves_played < maximum_moves) {
    const int column = centerFirstMove(state.board);
    if (column < 0) break;
    if constexpr (kFast) {
      drop7::fast::FullWaveSink sink;
      drop7::fast::FastMoveResult move;
      if (!drop7::fast::playHeadlessMoveFast(state, seed, column, sink, move))
        break;
      ++moves;
      waves += static_cast<std::uint64_t>(sink.count);
    } else {
      MoveResult move;
      if (!playHeadlessMove(state, seed, column, move)) break;
      ++moves;
      waves += move.waves.size();
    }
  }
  score = state.score;
}

template <bool kFast>
Outcome run(int games, int maximum_moves, int threads) {
  Outcome result;
  std::atomic<int> cursor{0};
  std::atomic<std::uint64_t> moves{0};
  std::atomic<std::uint64_t> waves{0};
  std::atomic<std::int64_t> score{0};
  const auto start = Clock::now();
  {
    std::vector<std::thread> workers;
    for (int worker = 0; worker < threads; ++worker) {
      workers.emplace_back([&] {
        while (true) {
          const int game = cursor.fetch_add(1, std::memory_order_relaxed);
          if (game >= games) break;
          std::uint64_t game_moves = 0;
          std::uint64_t game_waves = 0;
          std::int64_t game_score = 0;
          playOne<kFast>(kBenchSeeds + static_cast<std::uint32_t>(game),
                         maximum_moves, game_moves, game_waves, game_score);
          moves.fetch_add(game_moves, std::memory_order_relaxed);
          waves.fetch_add(game_waves, std::memory_order_relaxed);
          score.fetch_add(game_score, std::memory_order_relaxed);
        }
      });
    }
    for (auto& worker : workers) worker.join();
  }
  result.seconds =
      std::chrono::duration<double>(Clock::now() - start).count();
  result.moves = moves.load();
  result.waves = waves.load();
  result.score = score.load();
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  int games = 512;
  int repeats = 3;
  int maximum_moves = 2000;
  int threads = 1;
  for (int index = 1; index + 1 < argc; index += 2) {
    const std::string key = argv[index];
    const std::string value = argv[index + 1];
    if (key == "--games") games = std::stoi(value);
    else if (key == "--repeats") repeats = std::stoi(value);
    else if (key == "--max-moves") maximum_moves = std::stoi(value);
    else if (key == "--threads") threads = std::stoi(value);
  }
  for (int fast = 0; fast <= 1; ++fast) {
    Outcome best;
    best.seconds = 1e18;
    for (int repeat = 0; repeat < repeats; ++repeat) {
      const Outcome one = fast ? run<true>(games, maximum_moves, threads)
                               : run<false>(games, maximum_moves, threads);
      if (one.seconds < best.seconds) best = one;
    }
    std::cout << (fast ? "fast engine:      " : "reference engine: ") << games
              << " games, " << threads << " threads, " << best.moves
              << " moves, " << best.seconds << " s, "
              << (best.moves / best.seconds) << " moves/s, "
              << (best.seconds * 1e9 / best.moves) << " ns/move, mean score "
              << (best.score / games) << '\n';
  }
  std::cout << "peak-rss " << drop7::fast::peakResidentBytes() << '\n';
  return 0;
}
