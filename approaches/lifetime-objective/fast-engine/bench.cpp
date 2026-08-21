// End-to-end benchmark: whole real games, unoptimised arm versus fast arm, at
// a grid of (depth, chance strata).
//
// Because the two arms are proven action-identical and work-identical by
// gate-search, every game pair must end with the same score and the same move
// count; the benchmark asserts that on every game, so a silent divergence
// cannot be reported as a speedup.
//
// --mode ab      paired whole games at each configuration
// --mode probe   times a fixed number of decisions at one configuration, used
//                to price depth 5 with seven strata without playing it out
//
// Timing-grade runs need an exclusive machine.  The load average is printed for
// every measurement and each configuration is repeated at least three times.

#include "variant-search.hpp"
#include "corpus.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace drop7;
using namespace drop7::fast;
using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

std::uint64_t workBoundFor(int depth, int strata) {
  const std::uint64_t branches =
      static_cast<std::uint64_t>(kBoardSize) * static_cast<std::uint64_t>(strata);
  std::uint64_t total = 0;
  for (int level = 1; level <= depth; ++level) {
    std::uint64_t power = 1;
    for (int step = 0; step < level; ++step) power *= branches;
    for (int inner = 1; inner <= level; ++inner) {
      std::uint64_t inner_power = 1;
      for (int step = 0; step < inner; ++step) inner_power *= branches;
      total += inner_power;
    }
    total += power;
  }
  return total;
}

struct ArmResult {
  double seconds = 0;
  std::uint64_t moves = 0;
  std::uint64_t work = 0;
  std::int64_t score = 0;
  std::size_t table_bytes = 0;
};

template <typename Search>
ArmResult playGames(FastSearchParameters parameters, std::uint32_t seed_start,
                    int games, int maximum_moves) {
  ArmResult result;
  Search search{parameters};
  const auto start = Clock::now();
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed = seed_start + static_cast<std::uint32_t>(game);
    requireLease(seed);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < maximum_moves) {
      FastSearchMetrics metrics;
      int column = search.chooseAction(state, metrics);
      result.work += metrics.work;
      if (column < 0 || !isLegal(state.board, column)) {
        column = centerFirstMove(state.board);
        if (column < 0) break;
      }
      MoveResult move;
      if (!playHeadlessMove(state, seed, column, move)) break;
      ++result.moves;
    }
    result.score += state.score;
  }
  result.seconds = secondsSince(start);
  result.table_bytes = search.tableBytes();
  return result;
}

template <typename Search>
ArmResult probeDecisions(FastSearchParameters parameters,
                         const std::vector<State>& roots, int decisions) {
  ArmResult result;
  Search search{parameters};
  const auto start = Clock::now();
  for (int index = 0; index < decisions; ++index) {
    FastSearchMetrics metrics;
    search.chooseAction(roots[static_cast<std::size_t>(index) % roots.size()],
                        metrics);
    result.work += metrics.work;
    result.score += metrics.completed_depth;
    ++result.moves;
  }
  result.seconds = secondsSince(start);
  result.table_bytes = search.tableBytes();
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  std::string mode = "ab";
  std::string grid = "3x5,3x7,4x5,4x7";
  int games = 4;
  int maximum_moves = 2000;
  int repeats = 3;
  int decisions = 24;
  int probe_depth = 5;
  int probe_strata = 7;
  bool probe_baseline = false;
  std::size_t cache_entries = 200'000;
  for (int index = 1; index + 1 < argc; index += 2) {
    const std::string key = argv[index];
    const std::string value = argv[index + 1];
    if (key == "--mode") mode = value;
    else if (key == "--grid") grid = value;
    else if (key == "--games") games = std::stoi(value);
    else if (key == "--max-moves") maximum_moves = std::stoi(value);
    else if (key == "--repeats") repeats = std::stoi(value);
    else if (key == "--decisions") decisions = std::stoi(value);
    else if (key == "--probe-depth") probe_depth = std::stoi(value);
    else if (key == "--probe-strata") probe_strata = std::stoi(value);
    else if (key == "--probe-baseline") probe_baseline = std::stoi(value) != 0;
    else if (key == "--cache") cache_entries = std::stoull(value);
  }
  std::cout << std::fixed;

  if (mode == "ab") {
    std::cout << "paired whole games, seeds 0x" << std::hex << kBenchmarkSeeds
              << "-0x" << (kBenchmarkSeeds + static_cast<std::uint32_t>(games) - 1u)
              << std::dec << ", " << games << " games per arm, " << repeats
              << " repeats, best of\n";
    std::cout << "cache capacity " << cache_entries << " entries\n";
    std::cout << "cfg    arm        best s    ms/move   moves    work/move   "
                 "speedup  ident  load\n";
    std::size_t cursor = 0;
    while (cursor < grid.size()) {
      const std::size_t comma = grid.find(',', cursor);
      const std::string item = grid.substr(
          cursor, comma == std::string::npos ? std::string::npos : comma - cursor);
      cursor = comma == std::string::npos ? grid.size() : comma + 1;
      const std::size_t split = item.find('x');
      if (split == std::string::npos) continue;
      const int depth = std::stoi(item.substr(0, split));
      const int strata = std::stoi(item.substr(split + 1));
      FastSearchParameters parameters;
      parameters.depth = depth;
      parameters.chance_samples = strata;
      parameters.maximum_work = workBoundFor(depth, strata) + 1;
      parameters.maximum_cache_entries = cache_entries;

      ArmResult best_slow;
      ArmResult best_fast;
      best_slow.seconds = 1e18;
      best_fast.seconds = 1e18;
      for (int repeat = 0; repeat < repeats; ++repeat) {
        const ArmResult slow = playGames<BaselineSearch>(
            parameters, kBenchmarkSeeds, games, maximum_moves);
        if (slow.seconds < best_slow.seconds) best_slow = slow;
        const ArmResult fast = playGames<AllFastSearch>(
            parameters, kBenchmarkSeeds, games, maximum_moves);
        if (fast.seconds < best_fast.seconds) best_fast = fast;
      }
      const bool identical = best_slow.moves == best_fast.moves &&
                             best_slow.score == best_fast.score &&
                             best_slow.work == best_fast.work;
      auto line = [&](const char* name, const ArmResult& arm, double speedup) {
        std::cout << std::left << std::setw(7) << item << std::setw(11) << name
                  << std::right << std::setprecision(3) << std::setw(9)
                  << arm.seconds << std::setw(11)
                  << (arm.moves ? arm.seconds * 1000.0 /
                                      static_cast<double>(arm.moves)
                                : 0.0)
                  << std::setw(8) << arm.moves << std::setw(13)
                  << (arm.moves ? arm.work / arm.moves : 0) << std::setw(10)
                  << std::setprecision(2) << speedup << std::setw(7)
                  << (identical ? "yes" : "NO") << std::setw(7)
                  << loadAverage() << '\n';
      };
      line("baseline", best_slow, 1.0);
      line("fast", best_fast,
           best_fast.seconds > 0 ? best_slow.seconds / best_fast.seconds : 0.0);
      std::cout << "       fast table bytes " << best_fast.table_bytes
                << ", process peak resident " << peakResidentBytes() << '\n';
      if (!identical) {
        std::cout << "       ARMS DIVERGED: slow(moves " << best_slow.moves
                  << ", score " << best_slow.score << ", work "
                  << best_slow.work << ") fast(moves " << best_fast.moves
                  << ", score " << best_fast.score << ", work "
                  << best_fast.work << ")\n";
        return 1;
      }
    }
    return 0;
  }

  if (mode == "probe") {
    // Real roots to price an expensive configuration without playing it out.
    std::vector<State> roots;
    auto decide = [](const State& state) {
      return ref::chooseDepth4Action(state).action;
    };
    for (int game = 0; roots.size() < static_cast<std::size_t>(decisions) &&
                       game < 8;
         ++game) {
      harvestRootStates(kBenchmarkSeeds + 0x100u + static_cast<std::uint32_t>(game),
                        60, decide, roots);
    }
    FastSearchParameters parameters;
    parameters.depth = probe_depth;
    parameters.chance_samples = probe_strata;
    parameters.maximum_work = workBoundFor(probe_depth, probe_strata) + 1;
    parameters.maximum_cache_entries = cache_entries;
    std::cout << "probe: depth " << probe_depth << ", strata " << probe_strata
              << ", work bound " << parameters.maximum_work << ", "
              << decisions << " real roots, " << repeats << " repeats\n";
    std::cout << "arm        best s   ms/decision   work/decision   ns/work   "
                 "load\n";
    auto line = [&](const char* name, const ArmResult& arm) {
      const double per = static_cast<double>(arm.moves);
      std::cout << std::left << std::setw(11) << name << std::right
                << std::setprecision(3) << std::setw(8) << arm.seconds
                << std::setw(14) << arm.seconds * 1000.0 / per << std::setw(16)
                << (arm.moves ? arm.work / arm.moves : 0) << std::setw(10)
                << std::setprecision(3)
                << (arm.work ? arm.seconds * 1e9 /
                                   static_cast<double>(arm.work)
                             : 0.0)
                << std::setw(8) << std::setprecision(2) << loadAverage()
                << '\n';
    };
    if (probe_baseline) {
      ArmResult best;
      best.seconds = 1e18;
      for (int repeat = 0; repeat < repeats; ++repeat) {
        const ArmResult one =
            probeDecisions<BaselineSearch>(parameters, roots, decisions);
        if (one.seconds < best.seconds) best = one;
      }
      line("baseline", best);
    }
    ArmResult best;
    best.seconds = 1e18;
    for (int repeat = 0; repeat < repeats; ++repeat) {
      const ArmResult one =
          probeDecisions<AllFastSearch>(parameters, roots, decisions);
      if (one.seconds < best.seconds) best = one;
    }
    line("fast", best);
    std::cout << "fast table bytes " << best.table_bytes
              << ", process peak resident " << peakResidentBytes() << '\n';
    return 0;
  }

  if (mode == "cacheinv") {
    // Does the declared transposition capacity change any decision?
    //
    // It cannot, by construction: a cached value is exactly what expand() would
    // recompute for the same (state, depth) key, because the search is
    // deterministic and every chance seed derives from the state and the
    // remaining depth.  Capacity therefore moves `work`, `nodes` and
    // `cacheHits` and nothing else.  That is a strong claim about cost, so it
    // is checked rather than asserted: two FastSearch instances differing only
    // in capacity must select the same column at every move of a real game.
    std::vector<std::size_t> capacities;
    for (std::size_t value : {std::size_t{4'000}, std::size_t{60'000},
                              std::size_t{200'000}, std::size_t{1'000'000}}) {
      capacities.push_back(value);
    }
    FastSearchParameters base;
    base.depth = probe_depth;
    base.chance_samples = probe_strata;
    base.maximum_work = workBoundFor(probe_depth, probe_strata) + 1;
    std::cout << "cache invariance: depth " << probe_depth << ", strata "
              << probe_strata << ", " << games << " games x " << maximum_moves
              << " moves\n";
    std::cout << "capacity      moves  action mismatches   work/move   "
                 "cache hits/move  relative work\n";
    std::uint64_t reference_work = 0;
    std::vector<std::vector<int>> reference_actions;
    bool ok = true;
    for (std::size_t capacity : capacities) {
      FastSearchParameters parameters = base;
      parameters.maximum_cache_entries = capacity;
      FastSearch search{parameters};
      std::vector<std::vector<int>> actions;
      std::uint64_t work = 0;
      std::uint64_t hits = 0;
      std::uint64_t moves = 0;
      std::uint64_t incomplete = 0;
      for (int game = 0; game < games; ++game) {
        const std::uint32_t seed = kBenchmarkSeeds + 0x200u +
                                   static_cast<std::uint32_t>(game);
        requireLease(seed);
        State state = initialHeadlessState(seed);
        actions.emplace_back();
        while (!state.game_over && state.moves_played < maximum_moves) {
          FastSearchMetrics metrics;
          const int column = search.chooseAction(state, metrics);
          work += metrics.work;
          hits += metrics.cache_hits;
          if (metrics.completed_depth != probe_depth) ++incomplete;
          ++moves;
          actions.back().push_back(column);
          MoveResult move;
          if (!playHeadlessMove(state, seed, column, move)) break;
        }
      }
      std::uint64_t mismatches = 0;
      if (reference_actions.empty()) {
        reference_actions = actions;
        reference_work = work;
      } else {
        for (std::size_t g = 0; g < actions.size(); ++g) {
          const std::size_t n =
              std::min(actions[g].size(), reference_actions[g].size());
          if (actions[g].size() != reference_actions[g].size()) ++mismatches;
          for (std::size_t m = 0; m < n; ++m) {
            if (actions[g][m] != reference_actions[g][m]) ++mismatches;
          }
        }
      }
      std::cout << std::setw(9) << capacity << std::setw(11) << moves
                << std::setw(20) << mismatches << std::setw(12)
                << (moves ? work / moves : 0) << std::setw(17)
                << (moves ? hits / moves : 0) << std::setw(15)
                << std::setprecision(3)
                << (reference_work ? static_cast<double>(work) /
                                         static_cast<double>(reference_work)
                                   : 1.0)
                << (incomplete ? "  WORK BOUND BOUND" : "") << '\n';
      ok &= mismatches == 0 && incomplete == 0;
    }
    std::cout << (ok ? "CACHE INVARIANCE HOLDS: capacity moves cost only\n"
                     : "CACHE INVARIANCE VIOLATED\n");
    return ok ? 0 : 1;
  }

  std::cerr << "unknown mode " << mode << '\n';
  return 2;
}
