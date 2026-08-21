// Whole-game cohort runner on the fast search.
//
// Emits the standard exploratory artifact by including the existing
// approaches/lifetime-objective/common/harness.hpp -- which owns the game loop,
// the enforced score-decomposition identity (score == 17,000*rises +
// 70,000*clears + sum(waveDepthPoints)), the summary statistics and the
// per-game detail -- rather than writing a second one.  Gameplay therefore runs
// through the UNMODIFIED drop7::playHeadlessMove; only the decision comes from
// drop7::fast::FastSearch.  That is deliberate: it isolates the search, and the
// engine itself is gated separately (finding-13 section 4D).
//
// Three things this runner adds over a bare harness call:
//
//  1. COMPLETED-DEPTH AUDIT.  --max-work is passed explicitly from the
//     worst-case bound, never from measured work, and every single decision's
//     completed depth is checked against the requested depth.  A configuration
//     bound sized for one setting silently degraded a depth-4 search to depth 3
//     once already in this repository's history; `incompleteDecisions` must be
//     0 or the arm is void.
//  2. CHUNKED DURABILITY.  Games are run in chunks and the cumulative artifact
//     is rewritten after every chunk, so a runtime kill loses at most one chunk.
//  3. A declared cache capacity, because at these depths the transposition
//     cache evicts and eviction is part of the configuration.

#include "fast-search.hpp"
#include "../../../approaches/lifetime-objective/common/harness.hpp"

#include <atomic>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace drop7::lifetime::fastcohort {

using drop7::fast::FastSearch;
using drop7::fast::FastSearchMetrics;
using drop7::fast::FastSearchParameters;

// Worst-case iterative-deepening work, exactly the expression
// fair-only-depth4.cpp:worstCaseIterativeWork uses, generalised in the
// branching factor.  This is what --max-work must be sized from.
std::uint64_t worstCaseWork(int maximumDepth, int strata) {
  const auto branches =
      static_cast<std::uint64_t>(kBoardSize) * static_cast<std::uint64_t>(strata);
  std::uint64_t total = 0;
  for (int depth = 1; depth <= maximumDepth; ++depth) {
    std::uint64_t power = 1;
    for (int level = 1; level <= depth; ++level) {
      power *= branches;
      total += power;
    }
    total += power;
  }
  return total;
}

std::uint64_t worstCaseCacheEntries(int maximumDepth, int strata) {
  const auto branches =
      static_cast<std::uint64_t>(kBoardSize) * static_cast<std::uint64_t>(strata);
  std::uint64_t total = 0;
  for (int depth = 2; depth <= maximumDepth; ++depth) {
    std::uint64_t power = 1;
    for (int level = 1; level < depth; ++level) {
      power *= branches;
      total += power;
    }
  }
  return total;
}

struct Audit {
  std::atomic<std::uint64_t> decisions{0};
  std::atomic<std::uint64_t> incomplete{0};
  std::atomic<std::uint64_t> maximumWorkSeen{0};
  std::atomic<std::uint64_t> maximumCacheSeen{0};
  std::atomic<int> minimumCompletedDepth{99};
};

struct Options {
  CohortOptions cohort;
  int depth = 5;
  int strata = 7;
  std::size_t cacheEntries = 60'000;
  std::uint64_t maximumWork = 0;  // 0 => worst-case bound
  int chunk = 16;
  std::string output;
  std::string label;
};

Options parse(int argc, char** argv) {
  Options options;
  options.cohort.games = 64;
  options.cohort.maximumMoves = 2000;
  options.cohort.threads = 16;
  for (int index = 1; index + 1 < argc; index += 2) {
    const std::string key = argv[index];
    const std::string value = argv[index + 1];
    if (key == "--seed-start") {
      options.cohort.seedStart =
          static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
    } else if (key == "--games") options.cohort.games = std::stoi(value);
    else if (key == "--max-moves") options.cohort.maximumMoves = std::stoi(value);
    else if (key == "--threads") options.cohort.threads = std::stoi(value);
    else if (key == "--depth") options.depth = std::stoi(value);
    else if (key == "--chance-samples") options.strata = std::stoi(value);
    else if (key == "--cache") options.cacheEntries = std::stoull(value);
    else if (key == "--max-work") options.maximumWork = std::stoull(value);
    else if (key == "--chunk") options.chunk = std::stoi(value);
    else if (key == "--output") options.output = value;
    else if (key == "--label") options.label = value;
    else throw std::invalid_argument("unknown option " + key);
  }
  return options;
}

}  // namespace drop7::lifetime::fastcohort

int main(int argc, char** argv) {
  using namespace drop7;
  using namespace drop7::lifetime;
  using namespace drop7::lifetime::fastcohort;
  try {
    Options options = parse(argc, argv);
    const std::uint64_t bound = worstCaseWork(options.depth, options.strata);
    if (options.maximumWork == 0) options.maximumWork = bound + 1;
    if (options.maximumWork <= bound) {
      throw std::runtime_error(
          "--max-work must exceed the worst-case bound " +
          std::to_string(bound) +
          "; a bound that can bind silently degrades the completed depth");
    }
    if (options.output.empty()) throw std::runtime_error("--output is required");

    FastSearchParameters parameters;
    parameters.depth = options.depth;
    parameters.chance_samples = options.strata;
    parameters.maximum_work = options.maximumWork;
    parameters.maximum_cache_entries = options.cacheEntries;

    std::cerr << "arm " << options.label << ": depth " << options.depth
              << ", strata " << options.strata << ", terminalUtility "
              << parameters.terminal_utility << ", policySeed 0x" << std::hex
              << parameters.policy_seed << std::dec << "\n"
              << "  worst-case work " << bound << ", --max-work "
              << options.maximumWork << " (headroom "
              << (options.maximumWork - bound) << ")\n"
              << "  worst-case cache entries "
              << worstCaseCacheEntries(options.depth, options.strata)
              << ", declared capacity " << options.cacheEntries
              << (worstCaseCacheEntries(options.depth, options.strata) >
                          options.cacheEntries
                      ? "  => the cache EVICTS; eviction is part of this "
                        "configuration\n"
                      : "  => the cache never evicts\n")
              << "  seeds 0x" << std::hex << options.cohort.seedStart << "-0x"
              << (options.cohort.seedStart +
                  static_cast<std::uint32_t>(options.cohort.games) - 1u)
              << std::dec << ", " << options.cohort.games << " games, cap "
              << options.cohort.maximumMoves << " moves, "
              << options.cohort.threads << " threads\n";

    Audit audit;
    const auto started = std::chrono::steady_clock::now();
    std::vector<GameRecord> pooled;
    const int chunk = std::max(1, options.chunk);

    for (int done = 0; done < options.cohort.games; done += chunk) {
      CohortOptions slice = options.cohort;
      slice.seedStart =
          options.cohort.seedStart + static_cast<std::uint32_t>(done);
      slice.games = std::min(chunk, options.cohort.games - done);
      slice.threads = std::min(options.cohort.threads, slice.games);

      auto records = runCohort(slice, [&]() {
        return [search = FastSearch{parameters}, &audit](
                   const State& state, std::uint64_t& work) mutable {
          FastSearchMetrics metrics;
          const int action = search.chooseAction(state, metrics);
          work += metrics.work;
          audit.decisions.fetch_add(1, std::memory_order_relaxed);
          if (metrics.completed_depth != search.requestedDepth()) {
            audit.incomplete.fetch_add(1, std::memory_order_relaxed);
          }
          int previous = audit.minimumCompletedDepth.load();
          while (metrics.completed_depth < previous &&
                 !audit.minimumCompletedDepth.compare_exchange_weak(
                     previous, metrics.completed_depth)) {
          }
          std::uint64_t seen = audit.maximumWorkSeen.load();
          while (metrics.work > seen &&
                 !audit.maximumWorkSeen.compare_exchange_weak(seen,
                                                              metrics.work)) {
          }
          std::uint64_t entries = audit.maximumCacheSeen.load();
          const auto used = static_cast<std::uint64_t>(metrics.cache_entries);
          while (used > entries &&
                 !audit.maximumCacheSeen.compare_exchange_weak(entries, used)) {
          }
          return action;
        };
      });
      pooled.insert(pooled.end(), records.begin(), records.end());

      const double wall =
          std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
              .count();
      std::ostringstream config;
      config << std::setprecision(12) << "{\"engine\": \"fast-engine\""
             << ", \"depth\": " << options.depth
             << ", \"chanceSamples\": " << options.strata
             << ", \"terminalUtility\": " << parameters.terminal_utility
             << ", \"maximumWork\": " << options.maximumWork
             << ", \"worstCaseWork\": " << bound
             << ", \"maximumCacheEntries\": " << options.cacheEntries
             << ", \"worstCaseCacheEntries\": "
             << worstCaseCacheEntries(options.depth, options.strata)
             << ", \"decisions\": " << audit.decisions.load()
             << ", \"incompleteDecisions\": " << audit.incomplete.load()
             << ", \"minimumCompletedDepth\": "
             << audit.minimumCompletedDepth.load()
             << ", \"maximumWorkPerDecision\": " << audit.maximumWorkSeen.load()
             << ", \"maximumCacheEntriesUsed\": "
             << audit.maximumCacheSeen.load()
             << ", \"gamesComplete\": " << pooled.size() << "}";
      CohortOptions written = options.cohort;
      written.games = static_cast<int>(pooled.size());
      std::ofstream file(options.output);
      if (!file) throw std::runtime_error("cannot open " + options.output);
      writeArtifact(file, "fast-engine-parameterized-fair-search", config.str(),
                    written, pooled, wall);
      std::cerr << "  chunk done: " << pooled.size() << "/"
                << options.cohort.games << " games, " << wall
                << "s, artifact rewritten\n";
    }

    std::cerr << "AUDIT " << options.label << ": decisions "
              << audit.decisions.load() << ", incompleteDecisions "
              << audit.incomplete.load() << ", minimumCompletedDepth "
              << audit.minimumCompletedDepth.load() << " (requested "
              << options.depth << "), maximumWorkPerDecision "
              << audit.maximumWorkSeen.load() << " of " << options.maximumWork
              << ", maximumCacheEntriesUsed " << audit.maximumCacheSeen.load()
              << " of " << options.cacheEntries << "\n";
    if (audit.incomplete.load() != 0) {
      std::cerr << "ARM VOID: the work bound bound on "
                << audit.incomplete.load() << " decisions\n";
      return 1;
    }
    std::cerr << "ARM OK\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "cohort failed: " << error.what() << '\n';
    return 1;
  }
}
