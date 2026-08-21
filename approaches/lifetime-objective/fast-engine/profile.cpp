// Profile of the ORIGINAL fair depth-4 hot path, plus per-optimisation
// attribution.
//
// `perf` is not installed on this host, so this program instruments instead:
//
//   1. CENSUS.  A counting copy of the unoptimised search driver reports, per
//      real decision, how many times each primitive is entered.
//   2. MICROBENCH.  Each primitive is timed on the input distribution the
//      census says it actually sees, harvested from real games.
//   3. ATTRIBUTION.  count x ns/call for each primitive, against the measured
//      whole-decision time, with the residual shown rather than hidden.
//   4. ABLATION.  The same search driver is instantiated with each storage
//      optimisation switched on independently and cumulatively, so every
//      claimed speedup is an A/B measurement on one driver.
//
// Timings on a contended host are indicative; the load average at each phase is
// printed alongside.  Every arm here is CHECK-tier: no strength claim.

#include "variant-search.hpp"
#include "corpus.hpp"

#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <list>
#include <string>
#include <unordered_map>
#include <functional>
#include <vector>

namespace {

using namespace drop7;
using namespace drop7::fast;

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

struct Corpus {
  std::vector<State> roots;      // real decision states
  std::vector<State> nodes;      // interior search states (depth >= 1)
  std::vector<State> leaves;     // depth-0 states the leaf actually sees
  std::vector<Board> cascades;   // boards findPoppers is actually called on
};

void buildCorpus(int games, int maximum_moves, std::size_t node_target,
                 std::size_t leaf_target, Corpus& corpus) {
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed =
        kProfileCorpusSeeds + static_cast<std::uint32_t>(game);
    requireLease(seed);
    auto decide = [](const State& state) {
      return ref::chooseDepth4Action(state).action;
    };
    harvestRootStates(seed, maximum_moves, decide, corpus.roots);
  }
  for (const State& root : corpus.roots) {
    if (corpus.leaves.size() >= leaf_target &&
        corpus.nodes.size() >= node_target) {
      break;
    }
    bool ignored = false;
    const State canonical = cfpi::detail::canonicalState(root, ignored);
    for (int ply = 1; ply <= 4; ++ply) {
      harvestSearchStates(
          canonical, ply, 5, 0, frozen::kPolicySeed, corpus.leaves,
          std::min(leaf_target, corpus.leaves.size() + 200));
    }
    for (int ply = 2; ply <= 4; ++ply) {
      harvestSearchStates(
          canonical, ply, 5, 1, frozen::kPolicySeed, corpus.nodes,
          std::min(node_target, corpus.nodes.size() + 200));
    }
  }
  // Boards as findPoppers sees them: every board state a cascade examines.
  for (const State& node : corpus.nodes) {
    if (corpus.cascades.size() >= node_target) break;
    Board board = node.board;
    placeDisc(board, centerFirstMove(board), node.next_disc);
    corpus.cascades.push_back(board);
  }
}

template <typename Function>
double timePerCall(Function&& function, std::size_t calls) {
  const auto start = Clock::now();
  function();
  return secondsSince(start) * 1e9 / static_cast<double>(calls);
}

}  // namespace

int main(int argc, char** argv) {
  int games = 3;
  int maximum_moves = 90;
  int decisions = 40;
  int repeats = 3;
  bool do_census = true;
  bool do_micro = true;
  bool do_ablation = true;
  for (int index = 1; index + 1 < argc; index += 2) {
    const std::string key = argv[index];
    const std::string value = argv[index + 1];
    if (key == "--games") games = std::stoi(value);
    else if (key == "--max-moves") maximum_moves = std::stoi(value);
    else if (key == "--decisions") decisions = std::stoi(value);
    else if (key == "--repeats") repeats = std::stoi(value);
    else if (key == "--census") do_census = std::stoi(value) != 0;
    else if (key == "--micro") do_micro = std::stoi(value) != 0;
    else if (key == "--ablation") do_ablation = std::stoi(value) != 0;
  }

  volatile double sink_double = 0;
  volatile std::int64_t sink_int = 0;
  std::cout << std::fixed << std::setprecision(1);
  std::cout << "load average at start: " << std::setprecision(2)
            << loadAverage() << std::setprecision(1) << '\n';

  Corpus corpus;
  buildCorpus(games, maximum_moves, 40'000, 40'000, corpus);
  std::cout << "corpus: " << corpus.roots.size() << " real roots, "
            << corpus.nodes.size() << " interior states, "
            << corpus.leaves.size() << " leaf states, "
            << corpus.cascades.size() << " cascade boards\n\n";

  if (corpus.roots.empty() || corpus.leaves.empty()) {
    std::cerr << "empty corpus\n";
    return 1;
  }
  const std::size_t root_count =
      std::min<std::size_t>(corpus.roots.size(), static_cast<std::size_t>(decisions));

  // -------------------------------------------------------------------------
  // 1. Census
  // -------------------------------------------------------------------------
  FastSearchParameters parameters;
  parameters.depth = 4;
  parameters.chance_samples = 5;
  parameters.maximum_work = 3'200'000;
  parameters.maximum_cache_entries = 60'000;

  const double per = static_cast<double>(root_count);
  if (do_census) {
  CensusSearch census_search{parameters};
  CensusSearch::Census totals;
  std::uint64_t total_work = 0;
  std::uint64_t total_nodes = 0;
  std::uint64_t total_hits = 0;
  for (std::size_t index = 0; index < root_count; ++index) {
    FastSearchMetrics metrics;
    census_search.chooseAction(corpus.roots[index], metrics);
    totals.leaf_calls += census_search.census.leaf_calls;
    totals.move_calls += census_search.census.move_calls;
    totals.action_nodes += census_search.census.action_nodes;
    totals.key_builds += census_search.census.key_builds;
    totals.key_inserts += census_search.census.key_inserts;
    totals.canonical_calls += census_search.census.canonical_calls;
    totals.leaf_repeats += census_search.census.leaf_repeats;
    total_work += metrics.work;
    total_nodes += metrics.nodes;
    total_hits += metrics.cache_hits;
  }
  std::cout << "1. CENSUS, unoptimised depth 4 / 5 strata, per decision, over "
            << root_count << " real decisions\n";
  std::cout << "   logical work            " << total_work / per << '\n';
  std::cout << "   bestFutureValue nodes   " << total_nodes / per << '\n';
  std::cout << "   fairLeaf calls          " << totals.leaf_calls / per << '\n';
  std::cout << "   playMoveSampled calls   " << totals.move_calls / per << '\n';
  std::cout << "   canonicalState calls    " << totals.canonical_calls / per
            << '\n';
  std::cout << "   dynamicStateKey builds  " << totals.key_builds / per << '\n';
  std::cout << "   cache inserts           " << totals.key_inserts / per << '\n';
  std::cout << "   cache hits              " << total_hits / per << '\n';
  std::cout << "   repeated leaf states    " << totals.leaf_repeats / per
            << "  ("
            << (totals.leaf_calls
                    ? 100.0 * static_cast<double>(totals.leaf_repeats) /
                          static_cast<double>(totals.leaf_calls)
                    : 0.0)
            << "% of leaf calls; headroom for a leaf memo)\n\n";

  // Cascade waves per applied move, on the real distribution.
  std::uint64_t waves = 0;
  std::uint64_t applied = 0;
  for (const State& node : corpus.nodes) {
    for (const int column : cfpi::detail::kColumnOrder) {
      if (!isLegal(node.board, column)) continue;
      for (int sample = 0; sample < 5; ++sample) {
        FastStratifiedRandom random{
            cfpi::detail::scenarioSeedForState(node, frozen::kPolicySeed, 2),
            sample, 5, 0};
        FullWaveSink sink;
        FastMoveResult move;
        if (!playMoveFast(node, column, random, sink, move)) continue;
        ++applied;
        // findPoppers runs once per wave plus once for the terminating scan,
        // and once more after a rise.
        waves += static_cast<std::uint64_t>(sink.count) + 1u;
      }
      break;
    }
  }
  const double popper_scans_per_move =
      applied ? static_cast<double>(waves) / static_cast<double>(applied) : 0.0;
  std::cout << "   findPoppers scans per applied move: " << std::setprecision(3)
            << popper_scans_per_move << std::setprecision(1) << " (over "
            << applied << " applied moves)\n\n";
  }

  // -------------------------------------------------------------------------
  // 2. Microbenchmarks
  // -------------------------------------------------------------------------
  if (do_micro) {
  std::cout << "2. MICROBENCH, ns per call, on the real input distribution "
               "(load "
            << std::setprecision(2) << loadAverage() << std::setprecision(1)
            << ")\n";
  std::cout << "   primitive                         original      fast     "
               "ratio\n";

  auto report = [&](const char* name, double original, double fast) {
    std::cout << "   " << std::left << std::setw(32) << name << std::right
              << std::setw(9) << std::setprecision(1) << original
              << std::setw(10) << fast << std::setw(10)
              << std::setprecision(2) << (fast > 0 ? original / fast : 0.0)
              << std::setprecision(1) << '\n';
  };

  {
    const std::size_t count = corpus.leaves.size();
    const int reps = 40;
    const double original = timePerCall(
        [&] {
          double total = 0;
          for (int rep = 0; rep < reps; ++rep) {
            for (const State& state : corpus.leaves) {
              total += frozen::fairLeaf(state);
            }
          }
          sink_double = total;
        },
        count * static_cast<std::size_t>(reps));
    LeafScratch scratch;
    const double fast = timePerCall(
        [&] {
          double total = 0;
          for (int rep = 0; rep < reps; ++rep) {
            for (const State& state : corpus.leaves) {
              total += fastFairLeaf(state, scratch);
            }
          }
          sink_double = total;
        },
        count * static_cast<std::size_t>(reps));
    report("fairLeaf", original, fast);
  }

  {
    const std::size_t count = corpus.nodes.size();
    const int reps = 8;
    const double original = timePerCall(
        [&] {
          std::int64_t total = 0;
          for (int rep = 0; rep < reps; ++rep) {
            for (const State& state : corpus.nodes) {
              for (int sample = 0; sample < 5; ++sample) {
                cfpi::detail::StratifiedRandom random{
                    cfpi::detail::scenarioSeedForState(state,
                                                       frozen::kPolicySeed, 2),
                    sample, 5, 0};
                MoveResult move;
                if (cfpi::detail::playMoveSampled(state, 3, random, move)) {
                  total += move.score_delta;
                }
              }
            }
          }
          sink_int = total;
        },
        count * static_cast<std::size_t>(reps) * 5u);
    const double fast = timePerCall(
        [&] {
          std::int64_t total = 0;
          for (int rep = 0; rep < reps; ++rep) {
            for (const State& state : corpus.nodes) {
              for (int sample = 0; sample < 5; ++sample) {
                FastStratifiedRandom random{
                    cfpi::detail::scenarioSeedForState(state,
                                                       frozen::kPolicySeed, 2),
                    sample, 5, 0};
                MinimalWaveSink wave_sink;
                FastMoveResult move;
                if (playMoveFast(state, 3, random, wave_sink, move)) {
                  total += move.score_delta;
                }
              }
            }
          }
          sink_int = total;
        },
        count * static_cast<std::size_t>(reps) * 5u);
    report("playMoveSampled (one move)", original, fast);
  }

  {
    const std::size_t count = corpus.cascades.size();
    const int reps = 40;
    const double original = timePerCall(
        [&] {
          std::int64_t total = 0;
          for (int rep = 0; rep < reps; ++rep) {
            for (const Board& board : corpus.cascades) {
              int popper_count = 0;
              findPoppers(board, popper_count);
              total += popper_count;
            }
          }
          sink_int = total;
        },
        count * static_cast<std::size_t>(reps));
    const double fast = timePerCall(
        [&] {
          std::int64_t total = 0;
          std::array<int, kCellCount> poppers{};
          for (int rep = 0; rep < reps; ++rep) {
            for (const Board& board : corpus.cascades) {
              total += findPoppersFast(board, poppers);
            }
          }
          sink_int = total;
        },
        count * static_cast<std::size_t>(reps));
    report("findPoppers", original, fast);
  }

  {
    const std::size_t count = corpus.cascades.size();
    const int reps = 200;
    const double original = timePerCall(
        [&] {
          std::int64_t total = 0;
          for (int rep = 0; rep < reps; ++rep) {
            for (const Board& board : corpus.cascades) {
              total += isBoardEmpty(board) ? 1 : 0;
            }
          }
          sink_int = total;
        },
        count * static_cast<std::size_t>(reps));
    const double fast = timePerCall(
        [&] {
          std::int64_t total = 0;
          for (int rep = 0; rep < reps; ++rep) {
            for (const Board& board : corpus.cascades) {
              total += isBoardEmptyFast(board) ? 1 : 0;
            }
          }
          sink_int = total;
        },
        count * static_cast<std::size_t>(reps));
    report("isBoardEmpty", original, fast);
  }

  {
    const std::size_t count = corpus.cascades.size();
    const int reps = 200;
    const double original = timePerCall(
        [&] {
          std::int64_t total = 0;
          std::array<int, kCellCount> poppers{};
          for (int rep = 0; rep < reps; ++rep) {
            for (const Board& board : corpus.cascades) {
              BoardScan scan;
              scanBoard(board, scan);
              total += findPoppersScanned(board, scan, poppers);
            }
          }
          sink_int = total;
        },
        count * static_cast<std::size_t>(reps));
    const double fast = timePerCall(
        [&] {
          std::int64_t total = 0;
          for (int rep = 0; rep < reps; ++rep) {
            for (const Board& board : corpus.cascades) {
              BoardScan scan;
              scanBoard(board, scan);
              total += static_cast<std::int64_t>(scan.numbered & 1u);
            }
          }
          sink_int = total;
        },
        count * static_cast<std::size_t>(reps));
    report("scanBoard(+poppers) vs scanBoard", original, fast);
  }

  {
    const std::size_t count = corpus.cascades.size();
    const int reps = 200;
    const double original = timePerCall(
        [&] {
          std::int64_t total = 0;
          for (int rep = 0; rep < reps; ++rep) {
            for (const Board& board : corpus.cascades) {
              const Board next = applyGravity(board);
              total += next[0];
            }
          }
          sink_int = total;
        },
        count * static_cast<std::size_t>(reps));
    const double fast = timePerCall(
        [&] {
          std::int64_t total = 0;
          for (int rep = 0; rep < reps; ++rep) {
            for (const Board& board : corpus.cascades) {
              Board next = board;
              applyGravityInPlace(next);
              total += next[0];
            }
          }
          sink_int = total;
        },
        count * static_cast<std::size_t>(reps));
    report("applyGravity", original, fast);
  }

  {
    const std::size_t count = corpus.nodes.size();
    const int reps = 200;
    const double original = timePerCall(
        [&] {
          std::int64_t total = 0;
          for (int rep = 0; rep < reps; ++rep) {
            for (const State& state : corpus.nodes) {
              bool mirrored = false;
              const State canonical =
                  cfpi::detail::canonicalState(state, mirrored);
              total += canonical.board[0] + (mirrored ? 1 : 0);
            }
          }
          sink_int = total;
        },
        count * static_cast<std::size_t>(reps));
    const double fast = timePerCall(
        [&] {
          std::int64_t total = 0;
          for (int rep = 0; rep < reps; ++rep) {
            for (const State& state : corpus.nodes) {
              bool mirrored = false;
              const State canonical = canonicalStateFast(state, mirrored);
              total += canonical.board[0] + (mirrored ? 1 : 0);
            }
          }
          sink_int = total;
        },
        count * static_cast<std::size_t>(reps));
    report("canonicalState", original, fast);
  }

  {
    const int reps = 200'000;
    const double original = timePerCall(
        [&] {
          std::int64_t total = 0;
          for (int rep = 0; rep < reps; ++rep) {
            for (int depth = 1; depth <= 8; ++depth) {
              total += scoreForWave(depth);
            }
          }
          sink_int = total;
        },
        static_cast<std::size_t>(reps) * 8u);
    const double fast = timePerCall(
        [&] {
          std::int64_t total = 0;
          for (int rep = 0; rep < reps; ++rep) {
            for (int depth = 1; depth <= 8; ++depth) {
              total += scoreForWaveFast(depth);
            }
          }
          sink_int = total;
        },
        static_cast<std::size_t>(reps) * 8u);
    report("scoreForWave (pow 2.5)", original, fast);
  }

  double key_original = 0;
  double key_fast = 0;
  {
    // The transposition operation as the search performs it: build a key,
    // probe, and on a miss insert with LRU bookkeeping.  Replayed over the
    // real interior-state stream at the real cache capacity.
    const std::size_t count = corpus.nodes.size();
    const int reps = 8;
    key_original = timePerCall(
        [&] {
          std::int64_t total = 0;
          for (int rep = 0; rep < reps; ++rep) {
            std::unordered_map<std::string, double> cache;
            std::list<std::string> order;
            for (const State& state : corpus.nodes) {
              for (int depth = 1; depth <= 4; ++depth) {
                std::string key = cfpi::detail::dynamicStateKey(state, depth);
                const auto found = cache.find(key);
                if (found != cache.end()) {
                  total += 1;
                  continue;
                }
                order.push_back(key);
                cache.emplace(std::move(key), 1.0);
              }
            }
            total += static_cast<std::int64_t>(cache.size());
          }
          sink_int = total;
        },
        count * static_cast<std::size_t>(reps) * 4u);
    key_fast = timePerCall(
        [&] {
          std::int64_t total = 0;
          for (int rep = 0; rep < reps; ++rep) {
            TranspositionTable table(200'000);
            for (const State& state : corpus.nodes) {
              for (int depth = 1; depth <= 4; ++depth) {
                const PackedKey key = packKey(state, depth);
                const std::uint64_t hash = hashKey(key);
                if (table.lookup(key, hash) != nullptr) {
                  total += 1;
                  continue;
                }
                table.store(key, hash, 1.0);
              }
            }
            total += static_cast<std::int64_t>(table.size());
          }
          sink_int = total;
        },
        count * static_cast<std::size_t>(reps) * 4u);
    report("transposition probe+insert", key_original, key_fast);
  }
  std::cout << '\n';

  }

  if (!do_ablation) {
    std::cout << "peak resident bytes: " << peakResidentBytes() << '\n';
    return 0;
  }
  // -------------------------------------------------------------------------
  // 3/4. Whole-decision ablation
  // -------------------------------------------------------------------------
  std::cout << "3. ABLATION, whole depth-4 / 5-strata decision, "
            << root_count << " real decisions, " << repeats
            << " repeats, variants INTERLEAVED within each repeat so that a\n"
               "   drifting background load cannot favour whichever arm ran "
               "last\n";

  struct Variant {
    const char* name;
    std::function<double()> run;
    double best = 1e18;
    double worst = 0.0;
    double load_at_best = 0.0;
  };

  auto timeOne = [&](auto&& make) {
    auto search = make();
    const auto start = Clock::now();
    std::int64_t total = 0;
    for (std::size_t index = 0; index < root_count; ++index) {
      FastSearchMetrics metrics;
      search.chooseAction(corpus.roots[index], metrics);
      total += metrics.action;
    }
    sink_int = total;
    return secondsSince(start) * 1000.0 / per;
  };

  std::vector<Variant> variants;
  variants.push_back({"baseline (all original)",
                      [&] { return timeOne([&] { return BaselineSearch{parameters}; }); },
                      1e18, 0.0, 0.0});
  variants.push_back({"O1 fast table only",
                      [&] { return timeOne([&] { return TableOnlySearch{parameters}; }); },
                      1e18, 0.0, 0.0});
  variants.push_back({"O3-O5 fast engine only",
                      [&] { return timeOne([&] { return EngineOnlySearch{parameters}; }); },
                      1e18, 0.0, 0.0});
  variants.push_back({"O2/O7 fast leaf only",
                      [&] { return timeOne([&] { return LeafOnlySearch{parameters}; }); },
                      1e18, 0.0, 0.0});
  variants.push_back({"O1 + engine",
                      [&] { return timeOne([&] { return TableEngineSearch{parameters}; }); },
                      1e18, 0.0, 0.0});
  variants.push_back({"O1 + engine + leaf (all)",
                      [&] { return timeOne([&] { return AllFastSearch{parameters}; }); },
                      1e18, 0.0, 0.0});

  for (int repeat = 0; repeat < repeats; ++repeat) {
    for (Variant& variant : variants) {
      const double milliseconds = variant.run();
      if (milliseconds < variant.best) {
        variant.best = milliseconds;
        variant.load_at_best = loadAverage();
      }
      variant.worst = std::max(variant.worst, milliseconds);
    }
  }

  std::cout << "   variant                        best ms   worst ms   spread"
               "   speedup   load\n";
  const double baseline_ms = variants.front().best;
  for (const Variant& variant : variants) {
    std::cout << "   " << std::left << std::setw(28) << variant.name
              << std::right << std::setw(10) << std::setprecision(2)
              << variant.best << std::setw(11) << variant.worst
              << std::setw(9) << std::setprecision(2)
              << variant.worst / variant.best << "x" << std::setw(9)
              << baseline_ms / variant.best << std::setw(8)
              << variant.load_at_best << '\n';
  }

  std::cout << "\n(sink " << sink_double << ' ' << sink_int << ")\n";
  std::cout << "peak resident bytes: " << peakResidentBytes() << '\n';
  return 0;
}
