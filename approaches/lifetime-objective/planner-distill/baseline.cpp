// baseline - fair depth-4 (and any parameterisation of it, including a learned
// leaf) played on the SAME master tapes as the fair-planner teacher.
//
//   baseline --games 160 --seed-start 0xa5260000 [--depth 4]
//            [--chance-samples 5] [--max-work 3200000] [--max-moves 400]
//            [--threads 8] [--jsonl out.jsonl]
//
// WHY THIS IS NOT OPTIONAL
// -----------------------
// `docs/exploratory/finding-07-fair-planning-ceiling.md` reports every arm on
// eight master tapes, seeds 0xa5230000-07, and those eight are visibly easier
// than average: fair depth 4 survives 117.75 moves on them against 94.06 on the
// 64 fresh base-engine seeds of `finding-01`.  Any teacher-strength number taken
// from a different seed range therefore cannot be compared with finding-07's
// figures directly, and the only honest comparator is fair D4 on the same tapes
// this work's teacher played.
//
// The search is the parameterised driver in `fair-search.hpp`, whose `--parity`
// gate (in `d4-rank --parity`) proves it selects exactly the frozen reference
// column at default parameters.  Play, scoring, rises and termination come from
// the scenario engine that `scenario-parity.cpp` proves trajectory-identical to
// `drop7::playHeadlessMove`.

#include "fair-search.hpp"

#include "pinned/flow-ceiling/flow-common.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace drop7;
using namespace drop7::scenario;
using namespace drop7::flowceiling;
using drop7::distill::ParameterizedSearch;
using drop7::distill::SearchParameters;

constexpr std::uint32_t kLeaseStart = 0xa526'0000u;
constexpr std::uint32_t kGameSeedEnd = 0xa526'7fffu;

struct Options {
  int games = 8;
  std::uint32_t seed_start = kLeaseStart;
  int depth = 4;
  int chance_samples = 5;
  std::uint64_t max_work = 3'200'000;
  int max_moves = 400;
  int threads = 8;
  std::string jsonl;
  std::string label = "fair-d4";
};

GameStat playOne(const Options& options, std::uint32_t seed) {
  GameStat out;
  out.seed = seed;
  out.policy = options.label;
  const MasterTape tape = makeMasterTape(seed, options.max_moves);
  SearchParameters parameters;
  parameters.depth = options.depth;
  parameters.chanceSamples = options.chance_samples;
  parameters.maximumWork = options.max_work;
  ParameterizedSearch search{parameters};

  State state = masterStartState(tape);
  LatentBoard latent = tape.start_latent;
  int rises = 0;
  for (int move = 0; move < options.max_moves; ++move) {
    if (state.game_over) break;
    std::uint64_t work = 0;
    const int column = search.chooseAction(state, work);
    if (column < 0 || !isLegal(state.board, column)) {
      out.died = true;
      return out;
    }
    MoveResult result;
    LatentBoard next_latent{};
    if (!playMasterMove(tape, state, latent, move, rises, column, result,
                        next_latent)) {
      out.died = true;
      return out;
    }
    const MoveStat stat = describeMove(move + 1, column, state.next_disc, result);
    out.absorb(stat, result);
    if (result.level_advanced) ++rises;
    state = result.state;
    latent = next_latent;
    if (state.game_over) {
      out.died = true;
      return out;
    }
  }
  out.censored = true;
  return out;
}

int run(const Options& options) {
  if (options.seed_start < kLeaseStart ||
      options.seed_start + static_cast<std::uint32_t>(options.games) - 1u >
          kGameSeedEnd) {
    std::cerr << "game seeds outside SEEDLEASE-A52-DISTILL game half\n";
    return 2;
  }
  std::vector<GameStat> games(static_cast<std::size_t>(options.games));
  std::atomic<int> next{0};
  std::mutex log;
  const auto started = std::chrono::steady_clock::now();
  std::vector<std::thread> pool;
  const int threads = std::max(1, std::min(options.threads, options.games));
  for (int worker = 0; worker < threads; ++worker) {
    pool.emplace_back([&] {
      for (;;) {
        const int index = next.fetch_add(1);
        if (index >= options.games) return;
        const std::uint32_t seed =
            options.seed_start + static_cast<std::uint32_t>(index);
        GameStat game = playOne(options, seed);
        {
          std::lock_guard<std::mutex> guard(log);
          std::printf("game 0x%08x moves %4d score %9lld clears/move %.4f\n",
                      seed, game.moves, static_cast<long long>(game.score),
                      game.clearsPerMove());
          std::fflush(stdout);
        }
        games[static_cast<std::size_t>(index)] = std::move(game);
      }
    });
  }
  for (std::thread& thread : pool) thread.join();
  const double wall =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();

  std::int64_t moves = 0;
  std::int64_t cleared = 0;
  std::int64_t revealed = 0;
  std::int64_t score = 0;
  std::int64_t occupancy = 0;
  int censored = 0;
  int identity = 0;
  double slope_sum = 0.0;
  int slope_games = 0;
  std::int64_t steady_moves = 0;
  std::vector<int> lifetimes;
  for (const GameStat& game : games) {
    moves += game.moves;
    cleared += game.cleared;
    revealed += game.revealed;
    score += game.score;
    if (game.censored) ++censored;
    identity += game.identity_violations;
    for (int value : game.move_occupancy) occupancy += value;
    if (game.cycle_occupancy.size() >= 4) {
      slope_sum += occupancySlope(game.cycle_occupancy, 1);
      ++slope_games;
    }
    steady_moves += std::max<std::int64_t>(0, game.moves - 25);
    lifetimes.push_back(game.moves);
  }
  std::sort(lifetimes.begin(), lifetimes.end());
  const double move_total = static_cast<double>(std::max<std::int64_t>(moves, 1));
  std::printf(
      "\n=== %s, depth %d, %d strata, work %llu, %d games ===\n"
      "moves: mean %.2f median %d min %d max %d censored %d\n"
      "score: mean %.1f\n"
      "flow (pooled): clears/move %.4f (%.1f%% of 2.4), reveals/move %.4f "
      "(%.1f%% of 1.4)\n"
      "occupancy: mean %.2f cells, slope %.3f cells/cycle (%d games)\n"
      "checks: identity violations %d; %.1f s wall on %d threads\n",
      options.label.c_str(), options.depth, options.chance_samples,
      static_cast<unsigned long long>(options.max_work), options.games,
      moves / static_cast<double>(options.games),
      lifetimes[lifetimes.size() / 2], lifetimes.front(), lifetimes.back(),
      censored, score / static_cast<double>(options.games),
      cleared / move_total, 100.0 * (cleared / move_total) / 2.4,
      revealed / move_total, 100.0 * (revealed / move_total) / 1.4,
      occupancy / move_total, slope_games ? slope_sum / slope_games : 0.0,
      slope_games, identity, wall, threads);
  (void)steady_moves;

  if (!options.jsonl.empty()) {
    std::ofstream file(options.jsonl);
    if (!file) {
      std::cerr << "cannot open " << options.jsonl << "\n";
      return 1;
    }
    for (const GameStat& game : games) file << gameStatJson(game) << "\n";
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string flag = argv[index];
    if (flag == "--games" && index + 1 < argc) {
      options.games = std::atoi(argv[++index]);
    } else if (flag == "--seed-start" && index + 1 < argc) {
      options.seed_start = static_cast<std::uint32_t>(
          std::strtoul(argv[++index], nullptr, 0));
    } else if (flag == "--depth" && index + 1 < argc) {
      options.depth = std::atoi(argv[++index]);
    } else if (flag == "--chance-samples" && index + 1 < argc) {
      options.chance_samples = std::atoi(argv[++index]);
    } else if (flag == "--max-work" && index + 1 < argc) {
      options.max_work = std::strtoull(argv[++index], nullptr, 0);
    } else if (flag == "--max-moves" && index + 1 < argc) {
      options.max_moves = std::atoi(argv[++index]);
    } else if (flag == "--threads" && index + 1 < argc) {
      options.threads = std::atoi(argv[++index]);
    } else if (flag == "--jsonl" && index + 1 < argc) {
      options.jsonl = argv[++index];
    } else if (flag == "--label" && index + 1 < argc) {
      options.label = argv[++index];
    } else {
      std::cerr << "unknown argument " << flag << "\n";
      return 2;
    }
  }
  return run(options);
}
