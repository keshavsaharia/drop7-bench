// E-FAST-M6 equivalence, regression, determinism and timing gates
// (EX-20260823-fast-m6-reveal-sampling-port-be23e203).
//
// The comparator is the GENUINE native factored search: build.sh generates a
// copy of approaches/lifetime-objective/reveal-sampling/search.cpp that
// differs only in (a) the renamed entry point and (b) `thread_local` on its
// five inline diagnostic atomics, so that per-decision completed depth can be
// read back under game-level parallelism (each game runs wholly on one worker
// thread).  The diff is machine-checked to exactly those lines.
//
// Modes (one binary, disjoint probe/smoke seed blocks, no leased seed):
//   --mode grid     trace equivalence fast vs native on live probe games over
//                   d{3,4} x N{5,7} x M{1,2,6}: chosen column, work count and
//                   completed depth compared on every decision.
//   --mode replay   full replay of 3 retained C0 games (d3 N7 M6), fast
//                   engine driving; per-decision native comparison plus the
//                   retained final (score, moves) identity check; play-duty
//                   CPU/move for both engines.
//   --mode m1       M = 1 regression: FastFactoredSearch (memo on and off)
//                   vs the untouched fast::FastSearch, all six metric fields.
//   --mode subset   deterministic fast-only trace over all 12 grid points
//                   (smoke seeds) with per-decision memo-on/off identity and
//                   mirror-invariance checks; output contains no timing, so
//                   runs and thread counts must be byte-identical.
//   --mode cont     continuation duty (K=4, H=40, CRN tapes in the G0v2
//                   style) at d3 N7 M6: outcome identity fast vs native and
//                   realised CPU-s/move for both.

#include "reveal-sampling-noentry.cpp"

#include "../../../approaches/lifetime-objective/fast-reveal-sampling/fast-factored-search.hpp"

#include <sys/resource.h>
#include <time.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace drop7::fastm6gate {

namespace rs = drop7::lifetime::reveal;
using drop7::fastr::FastFactoredParameters;
using drop7::fastr::FastFactoredSearch;
using fast::FastSearchMetrics;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

inline double threadCpuSeconds() {
  timespec ts{};
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
  return static_cast<double>(ts.tv_sec) + 1e-9 * static_cast<double>(ts.tv_nsec);
}

inline double processCpuSeconds() {
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  return static_cast<double>(usage.ru_utime.tv_sec) +
         1e-6 * static_cast<double>(usage.ru_utime.tv_usec) +
         static_cast<double>(usage.ru_stime.tv_sec) +
         1e-6 * static_cast<double>(usage.ru_stime.tv_usec);
}

constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

inline void fnvAdd(std::uint64_t& hash, std::uint64_t value) {
  for (int byte = 0; byte < 8; ++byte) {
    hash ^= (value >> (8 * byte)) & 0xffu;
    hash *= kFnvPrime;
  }
}

inline bool anyLegal(const Board& board) {
  for (int column = 0; column < kBoardSize; ++column) {
    if (isLegal(board, column)) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// The configuration grid.  M = 1 bounds are the retained arm bounds; M > 1
// bounds follow the run-arms.sh convention (worst-case work, auto cache)
// except the two depth-4 M = 6 points, whose worst case (3.9e9 / 3.0e10
// work per decision) is infeasible inside the 4 h gate wall: they run under
// a disclosed budget cap, which deliberately exercises the work-limit
// degradation and LRU-eviction paths of both engines.
// ---------------------------------------------------------------------------

struct GridSpec {
  std::string tag;
  int depth = 3;
  int discSamples = 5;
  int revealSamples = 1;
  std::uint64_t maximumWork = 0;
  std::size_t maximumCacheEntries = 0;
  std::string boundOrigin;
};

inline std::uint64_t worstWork(int depth, int n, int m) {
  return rs::worstCaseIterativeWork(
      static_cast<std::uint64_t>(kBoardSize) * static_cast<std::uint64_t>(n) *
          static_cast<std::uint64_t>(m),
      depth);
}

inline std::size_t autoCache(int depth, int n, int m) {
  const std::uint64_t needed = rs::worstCaseIterativeCacheEntries(
      static_cast<std::uint64_t>(kBoardSize) * static_cast<std::uint64_t>(n) *
          static_cast<std::uint64_t>(m),
      depth);
  return static_cast<std::size_t>(std::max<std::uint64_t>(60'000, needed + 1));
}

inline std::vector<GridSpec> gridSpecs() {
  std::vector<GridSpec> specs;
  specs.push_back({"d3-n5-m1", 3, 5, 1, 3'200'000, 60'000,
                   "frozen reference bound (runs/RUN-A525-reveal/d3-n5-m1.json)"});
  specs.push_back({"d3-n5-m2", 3, 5, 2, worstWork(3, 5, 2), autoCache(3, 5, 2),
                   "worst-case work, auto cache (run-arms.sh convention)"});
  specs.push_back({"d3-n5-m6", 3, 5, 6, worstWork(3, 5, 6), autoCache(3, 5, 6),
                   "worst-case work, auto cache"});
  specs.push_back({"d3-n7-m1", 3, 7, 1, 16'000'000, 60'000,
                   "retained d3-n7-m1.json bound"});
  specs.push_back({"d3-n7-m2", 3, 7, 2, worstWork(3, 7, 2), autoCache(3, 7, 2),
                   "worst-case work, auto cache"});
  specs.push_back({"d3-n7-m6", 3, 7, 6, 51'084'852, 87'025,
                   "C0 exact (runs/RUN-A525-reveal/d3-n7-m6.json)"});
  specs.push_back({"d4-n5-m1", 4, 5, 1, 3'200'000, 60'000,
                   "frozen fair-D4 bound"});
  specs.push_back({"d4-n5-m2", 4, 5, 2, worstWork(4, 5, 2), autoCache(4, 5, 2),
                   "worst-case work, auto cache"});
  specs.push_back({"d4-n5-m6", 4, 5, 6, 51'084'852, 100'000,
                   "budget cap (worst case 3.87e9 infeasible in the gate wall); "
                   "exercises work-limit degradation and LRU eviction"});
  specs.push_back({"d4-n7-m1", 4, 7, 1, 16'000'000, 60'000,
                   "retained fresh-s7.json bound"});
  specs.push_back({"d4-n7-m2", 4, 7, 2, 187'336'114, autoCache(4, 7, 2),
                   "retained d4-n7-m2 arm bound (worst-case work, auto cache)"});
  specs.push_back({"d4-n7-m6", 4, 7, 6, 100'000'000, 150'000,
                   "budget cap (worst case 2.99e10 infeasible in the gate wall); "
                   "exercises work-limit degradation and LRU eviction"});
  return specs;
}

inline rs::SearchParameters nativeParameters(const GridSpec& spec) {
  rs::SearchParameters parameters;
  parameters.depth = spec.depth;
  parameters.discSamples = spec.discSamples;
  parameters.revealSamples = spec.revealSamples;
  parameters.maximumWork = spec.maximumWork;
  parameters.maximumCacheEntries = spec.maximumCacheEntries;
  return parameters;
}

inline FastFactoredParameters fastParameters(const GridSpec& spec, bool memo) {
  FastFactoredParameters parameters;
  parameters.depth = spec.depth;
  parameters.chance_samples = spec.discSamples;
  parameters.reveal_samples = spec.revealSamples;
  parameters.maximum_work = spec.maximumWork;
  parameters.maximum_cache_entries = spec.maximumCacheEntries;
  parameters.use_leaf_memo = memo;
  return parameters;
}

// Reads the native decision's completed depth from the (thread_local)
// diagnostics; the caller must have called rs::resetDiagnostics() first.
inline int nativeCompletedDepth() {
  const int depth = rs::gMinCompletedDepth.load();
  return depth >= (1 << 29) ? 0 : depth;
}

std::mutex gLogMutex;

// ---------------------------------------------------------------------------
// Mode: grid
// ---------------------------------------------------------------------------

constexpr std::uint32_t kProbeBase = 0xa527'8000u;  // grid blocks of 0x20
constexpr std::uint32_t kProbeM1Base = 0xa527'8200u;  // m1-regression blocks
constexpr std::uint32_t kSmokeBase = 0xa51d'8000u;  // subset mode, 12 of 16

struct GridGameResult {
  std::uint64_t decisions = 0;
  std::uint64_t columnMismatches = 0;
  std::uint64_t workMismatches = 0;
  std::uint64_t depthMismatches = 0;
  std::uint64_t nativeWork = 0;
  std::uint64_t fastWork = 0;
  std::uint64_t workLimitedNative = 0;
  double nativeCpu = 0.0;
  double fastCpu = 0.0;
  std::uint64_t nativeHash = kFnvOffset;
  std::uint64_t fastHash = kFnvOffset;
  int minCompletedDepth = INT_MAX;
};

GridGameResult runGridGame(const GridSpec& spec, std::uint32_t seed,
                           int maximumMoves, int mismatchLogBudget) {
  GridGameResult result;
  rs::FactoredSearch native{nativeParameters(spec)};
  FastFactoredSearch fastSearch{fastParameters(spec, true)};
  State state = initialHeadlessState(seed);
  while (!state.game_over && state.moves_played < maximumMoves) {
    if (!anyLegal(state.board)) break;
    rs::resetDiagnostics();
    std::uint64_t nativeWork = 0;
    const double nativeStart = threadCpuSeconds();
    const int nativeAction = native.chooseAction(state, nativeWork);
    result.nativeCpu += threadCpuSeconds() - nativeStart;
    const int nativeDepth = nativeCompletedDepth();
    result.workLimitedNative += rs::gWorkLimitEvents.load();
    FastSearchMetrics metrics;
    const double fastStart = threadCpuSeconds();
    fastSearch.chooseAction(state, metrics);
    result.fastCpu += threadCpuSeconds() - fastStart;
    ++result.decisions;
    result.nativeWork += nativeWork;
    result.fastWork += metrics.work;
    result.minCompletedDepth = std::min(result.minCompletedDepth, nativeDepth);
    fnvAdd(result.nativeHash, static_cast<std::uint64_t>(
                                  static_cast<std::int64_t>(nativeAction)));
    fnvAdd(result.nativeHash, nativeWork);
    fnvAdd(result.nativeHash, static_cast<std::uint64_t>(nativeDepth));
    fnvAdd(result.fastHash, static_cast<std::uint64_t>(
                                static_cast<std::int64_t>(metrics.action)));
    fnvAdd(result.fastHash, metrics.work);
    fnvAdd(result.fastHash, static_cast<std::uint64_t>(metrics.completed_depth));
    const bool columnBad = metrics.action != nativeAction;
    const bool workBad = metrics.work != nativeWork;
    const bool depthBad = metrics.completed_depth != nativeDepth;
    if (columnBad) ++result.columnMismatches;
    if (workBad) ++result.workMismatches;
    if (depthBad) ++result.depthMismatches;
    if ((columnBad || workBad || depthBad) &&
        static_cast<int>(result.columnMismatches + result.workMismatches +
                         result.depthMismatches) <= mismatchLogBudget) {
      std::lock_guard<std::mutex> lock(gLogMutex);
      std::cerr << "MISMATCH " << spec.tag << " seed 0x" << std::hex << seed
                << std::dec << " move " << state.moves_played << ": native ("
                << nativeAction << ", " << nativeWork << ", " << nativeDepth
                << ") fast (" << metrics.action << ", " << metrics.work << ", "
                << metrics.completed_depth << ")\n";
    }
    int column = nativeAction;
    if (column < 0 || !isLegal(state.board, column)) {
      column = centerFirstMove(state.board);
    }
    if (column < 0) break;
    MoveResult move;
    if (!playHeadlessMove(state, seed, column, move)) break;
  }
  return result;
}

int runGridMode(int threads, int games, int maximumMoves) {
  const auto specs = gridSpecs();
  struct Task {
    std::size_t specIndex;
    int game;
  };
  std::vector<Task> tasks;
  for (std::size_t specIndex = 0; specIndex < specs.size(); ++specIndex) {
    for (int game = 0; game < games; ++game) tasks.push_back({specIndex, game});
  }
  // Heavy points first so the tail of the schedule is short.
  std::stable_sort(tasks.begin(), tasks.end(), [&](const Task& a, const Task& b) {
    const auto cost = [&](const Task& task) {
      const GridSpec& spec = specs[task.specIndex];
      return spec.maximumWork * static_cast<std::uint64_t>(spec.depth);
    };
    return cost(a) > cost(b);
  });
  std::vector<GridGameResult> results(specs.size() * static_cast<std::size_t>(games));
  std::atomic<std::size_t> nextTask{0};
  const double wallStart =
      std::chrono::duration<double>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count();
  std::vector<std::thread> pool;
  for (int worker = 0; worker < threads; ++worker) {
    pool.emplace_back([&]() {
      for (;;) {
        const std::size_t index = nextTask.fetch_add(1);
        if (index >= tasks.size()) return;
        const Task& task = tasks[index];
        const GridSpec& spec = specs[task.specIndex];
        const std::uint32_t seed = kProbeBase +
                                   static_cast<std::uint32_t>(task.specIndex) * 0x20u +
                                   static_cast<std::uint32_t>(task.game);
        results[task.specIndex * static_cast<std::size_t>(games) +
                static_cast<std::size_t>(task.game)] =
            runGridGame(spec, seed, maximumMoves, 8);
      }
    });
  }
  for (std::thread& thread : pool) thread.join();
  const double wallSeconds =
      std::chrono::duration<double>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count() -
      wallStart;

  std::uint64_t totalDecisions = 0, totalMismatches = 0;
  std::cout << std::setprecision(10) << "{\"gate\": \"fastm6-grid-equivalence\", "
            << "\"probeSeedBaseHex\": \"0xa5278000\", \"gamesPerPoint\": "
            << games << ", \"maxMovesPerGame\": " << maximumMoves
            << ", \"points\": [";
  for (std::size_t specIndex = 0; specIndex < specs.size(); ++specIndex) {
    const GridSpec& spec = specs[specIndex];
    GridGameResult sum;
    sum.minCompletedDepth = INT_MAX;
    std::uint64_t pointNativeHash = kFnvOffset;
    std::uint64_t pointFastHash = kFnvOffset;
    for (int game = 0; game < games; ++game) {
      const GridGameResult& r =
          results[specIndex * static_cast<std::size_t>(games) +
                  static_cast<std::size_t>(game)];
      sum.decisions += r.decisions;
      sum.columnMismatches += r.columnMismatches;
      sum.workMismatches += r.workMismatches;
      sum.depthMismatches += r.depthMismatches;
      sum.nativeWork += r.nativeWork;
      sum.fastWork += r.fastWork;
      sum.workLimitedNative += r.workLimitedNative;
      sum.nativeCpu += r.nativeCpu;
      sum.fastCpu += r.fastCpu;
      sum.minCompletedDepth = std::min(sum.minCompletedDepth, r.minCompletedDepth);
      fnvAdd(pointNativeHash, r.nativeHash);
      fnvAdd(pointFastHash, r.fastHash);
    }
    totalDecisions += sum.decisions;
    totalMismatches +=
        sum.columnMismatches + sum.workMismatches + sum.depthMismatches;
    std::cout << (specIndex == 0 ? "" : ", ") << "{\"tag\": \"" << spec.tag
              << "\", \"depth\": " << spec.depth << ", \"discSamples\": "
              << spec.discSamples << ", \"revealSamples\": " << spec.revealSamples
              << ", \"maximumWork\": " << spec.maximumWork
              << ", \"maximumCacheEntries\": " << spec.maximumCacheEntries
              << ", \"boundOrigin\": \"" << spec.boundOrigin << "\""
              << ", \"decisions\": " << sum.decisions
              << ", \"columnMismatches\": " << sum.columnMismatches
              << ", \"workMismatches\": " << sum.workMismatches
              << ", \"depthMismatches\": " << sum.depthMismatches
              << ", \"nativeWorkTotal\": " << sum.nativeWork
              << ", \"fastWorkTotal\": " << sum.fastWork
              << ", \"workLimitedNativeDecisions\": " << sum.workLimitedNative
              << ", \"minCompletedDepth\": "
              << (sum.minCompletedDepth == INT_MAX ? 0 : sum.minCompletedDepth)
              << ", \"nativeCpuSeconds\": " << sum.nativeCpu
              << ", \"fastCpuSeconds\": " << sum.fastCpu
              << ", \"nativeCpuSecondsPerMove\": "
              << (sum.decisions ? sum.nativeCpu / static_cast<double>(sum.decisions) : 0.0)
              << ", \"fastCpuSecondsPerMove\": "
              << (sum.decisions ? sum.fastCpu / static_cast<double>(sum.decisions) : 0.0)
              << ", \"speedup\": "
              << (sum.fastCpu > 0.0 ? sum.nativeCpu / sum.fastCpu : 0.0)
              << ", \"nativeTraceHash\": \"" << std::hex << pointNativeHash
              << "\", \"fastTraceHash\": \"" << pointFastHash << std::dec
              << "\", \"decisionsAtLeast500\": "
              << (sum.decisions >= 500 ? "true" : "false") << "}";
  }
  std::cout << "], \"totalDecisions\": " << totalDecisions
            << ", \"totalMismatches\": " << totalMismatches
            << ", \"threads\": " << threads << ", \"wallSeconds\": "
            << wallSeconds << ", \"processCpuSeconds\": " << processCpuSeconds()
            << ", \"pass\": " << (totalMismatches == 0 ? "true" : "false")
            << "}\n";
  bool enough = true;
  for (std::size_t specIndex = 0; specIndex < specs.size(); ++specIndex) {
    GridGameResult sum;
    for (int game = 0; game < games; ++game) {
      sum.decisions += results[specIndex * static_cast<std::size_t>(games) +
                               static_cast<std::size_t>(game)].decisions;
    }
    if (sum.decisions < 500) enough = false;
  }
  return (totalMismatches == 0 && enough) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Mode: replay (retained C0 games; fast engine drives)
// ---------------------------------------------------------------------------

struct ReplayExpected {
  std::uint32_t seed;
  std::int64_t score;
  int moves;
};

// Retained finals from runs/RUN-A525-reveal/d3-n7-m6.json gamesDetail; the
// same three games G0v2 replayed exactly.
constexpr ReplayExpected kReplayExpected[] = {
    {0xa51d1005u, 320871, 95},
    {0xa51d1009u, 381355, 110},
    {0xa51d100au, 463094, 130},
};

int runReplayMode(int threads) {
  const GridSpec c0{"d3-n7-m6", 3, 7, 6, 51'084'852, 87'025, "C0 exact"};
  const int games = static_cast<int>(std::size(kReplayExpected));
  struct ReplayResult {
    std::uint64_t decisions = 0;
    std::uint64_t mismatches = 0;
    std::int64_t finalScore = 0;
    int finalMoves = 0;
    bool finalsMatch = false;
    double nativeCpu = 0.0;
    double fastCpu = 0.0;
    std::uint64_t work = 0;
  };
  std::vector<ReplayResult> results(static_cast<std::size_t>(games));
  std::atomic<int> nextGame{0};
  std::vector<std::thread> pool;
  const int workerCount = std::max(1, std::min(threads, games));
  for (int worker = 0; worker < workerCount; ++worker) {
    pool.emplace_back([&]() {
      for (;;) {
        const int gameIndex = nextGame.fetch_add(1);
        if (gameIndex >= games) return;
        const ReplayExpected& expected =
            kReplayExpected[static_cast<std::size_t>(gameIndex)];
        ReplayResult& result = results[static_cast<std::size_t>(gameIndex)];
        rs::FactoredSearch native{nativeParameters(c0)};
        FastFactoredSearch fastSearch{fastParameters(c0, true)};
        State state = initialHeadlessState(expected.seed);
        while (!state.game_over && state.moves_played < 2000) {
          if (!anyLegal(state.board)) break;
          rs::resetDiagnostics();
          std::uint64_t nativeWork = 0;
          const double nativeStart = threadCpuSeconds();
          const int nativeAction = native.chooseAction(state, nativeWork);
          result.nativeCpu += threadCpuSeconds() - nativeStart;
          const int nativeDepth = nativeCompletedDepth();
          FastSearchMetrics metrics;
          const double fastStart = threadCpuSeconds();
          fastSearch.chooseAction(state, metrics);
          result.fastCpu += threadCpuSeconds() - fastStart;
          ++result.decisions;
          result.work += metrics.work;
          if (metrics.action != nativeAction || metrics.work != nativeWork ||
              metrics.completed_depth != nativeDepth) {
            ++result.mismatches;
            std::lock_guard<std::mutex> lock(gLogMutex);
            std::cerr << "MISMATCH replay seed 0x" << std::hex << expected.seed
                      << std::dec << " move " << state.moves_played
                      << ": native (" << nativeAction << ", " << nativeWork
                      << ", " << nativeDepth << ") fast (" << metrics.action
                      << ", " << metrics.work << ", "
                      << metrics.completed_depth << ")\n";
          }
          // The FAST engine drives, so the finals identity below proves the
          // port reproduces the retained C0 trajectory end to end.
          int column = metrics.action;
          if (column < 0 || !isLegal(state.board, column)) {
            column = centerFirstMove(state.board);
          }
          if (column < 0) break;
          MoveResult move;
          if (!playHeadlessMove(state, expected.seed, column, move)) break;
        }
        result.finalScore = state.score;
        result.finalMoves = state.moves_played;
        result.finalsMatch = state.score == expected.score &&
                             state.moves_played == expected.moves;
      }
    });
  }
  for (std::thread& thread : pool) thread.join();
  std::uint64_t decisions = 0, mismatches = 0;
  double nativeCpu = 0.0, fastCpu = 0.0;
  bool finalsOk = true;
  std::cout << std::setprecision(10)
            << "{\"gate\": \"fastm6-c0-replay\", \"config\": {\"depth\": 3, "
               "\"discSamples\": 7, \"revealSamples\": 6, \"maximumWork\": "
               "51084852, \"maximumCacheEntries\": 87025}, \"games\": [";
  for (int gameIndex = 0; gameIndex < games; ++gameIndex) {
    const ReplayExpected& expected =
        kReplayExpected[static_cast<std::size_t>(gameIndex)];
    const ReplayResult& result = results[static_cast<std::size_t>(gameIndex)];
    decisions += result.decisions;
    mismatches += result.mismatches;
    nativeCpu += result.nativeCpu;
    fastCpu += result.fastCpu;
    finalsOk = finalsOk && result.finalsMatch;
    std::cout << (gameIndex == 0 ? "" : ", ") << "{\"seedHex\": \"0x" << std::hex
              << expected.seed << std::dec << "\", \"decisions\": "
              << result.decisions << ", \"mismatches\": " << result.mismatches
              << ", \"finalScore\": " << result.finalScore
              << ", \"finalMoves\": " << result.finalMoves
              << ", \"expectedScore\": " << expected.score
              << ", \"expectedMoves\": " << expected.moves
              << ", \"finalsMatch\": " << (result.finalsMatch ? "true" : "false")
              << "}";
  }
  std::cout << "], \"decisions\": " << decisions
            << ", \"mismatches\": " << mismatches
            << ", \"playDuty\": {\"nativeCpuSecondsPerMove\": "
            << (decisions ? nativeCpu / static_cast<double>(decisions) : 0.0)
            << ", \"fastCpuSecondsPerMove\": "
            << (decisions ? fastCpu / static_cast<double>(decisions) : 0.0)
            << ", \"speedup\": " << (fastCpu > 0.0 ? nativeCpu / fastCpu : 0.0)
            << "}, \"finalsMatch\": " << (finalsOk ? "true" : "false")
            << ", \"pass\": "
            << ((mismatches == 0 && finalsOk) ? "true" : "false") << "}\n";
  return (mismatches == 0 && finalsOk) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Mode: m1 (regression vs the untouched fast search)
// ---------------------------------------------------------------------------

int runM1Mode(int threads, int games, int maximumMoves) {
  std::vector<GridSpec> specs;
  for (const GridSpec& spec : gridSpecs()) {
    if (spec.revealSamples == 1) specs.push_back(spec);
  }
  struct M1Result {
    std::uint64_t decisions = 0;
    std::uint64_t memoOnMismatches = 0;
    std::uint64_t memoOffMismatches = 0;
  };
  std::vector<M1Result> results(specs.size() * static_cast<std::size_t>(games));
  struct Task {
    std::size_t specIndex;
    int game;
  };
  std::vector<Task> tasks;
  for (std::size_t specIndex = 0; specIndex < specs.size(); ++specIndex) {
    for (int game = 0; game < games; ++game) tasks.push_back({specIndex, game});
  }
  std::atomic<std::size_t> nextTask{0};
  std::vector<std::thread> pool;
  for (int worker = 0; worker < threads; ++worker) {
    pool.emplace_back([&]() {
      for (;;) {
        const std::size_t index = nextTask.fetch_add(1);
        if (index >= tasks.size()) return;
        const Task& task = tasks[index];
        const GridSpec& spec = specs[task.specIndex];
        M1Result& result =
            results[task.specIndex * static_cast<std::size_t>(games) +
                    static_cast<std::size_t>(task.game)];
        const std::uint32_t seed =
            kProbeM1Base + static_cast<std::uint32_t>(task.specIndex) * 0x20u +
            static_cast<std::uint32_t>(task.game);
        fast::FastSearchParameters baselineParameters;
        baselineParameters.depth = spec.depth;
        baselineParameters.chance_samples = spec.discSamples;
        baselineParameters.maximum_work = spec.maximumWork;
        baselineParameters.maximum_cache_entries = spec.maximumCacheEntries;
        fast::FastSearch baseline{baselineParameters};
        FastFactoredSearch memoOn{fastParameters(spec, true)};
        FastFactoredSearch memoOff{fastParameters(spec, false)};
        State state = initialHeadlessState(seed);
        while (!state.game_over && state.moves_played < maximumMoves) {
          if (!anyLegal(state.board)) break;
          FastSearchMetrics expected, onMetrics, offMetrics;
          baseline.chooseAction(state, expected);
          memoOn.chooseAction(state, onMetrics);
          memoOff.chooseAction(state, offMetrics);
          ++result.decisions;
          const auto identical = [](const FastSearchMetrics& a,
                                    const FastSearchMetrics& b) {
            return a.action == b.action && a.completed_depth == b.completed_depth &&
                   a.nodes == b.nodes && a.work == b.work &&
                   a.cache_hits == b.cache_hits &&
                   a.cache_entries == b.cache_entries;
          };
          if (!identical(expected, onMetrics)) ++result.memoOnMismatches;
          if (!identical(expected, offMetrics)) ++result.memoOffMismatches;
          int column = expected.action;
          if (column < 0 || !isLegal(state.board, column)) {
            column = centerFirstMove(state.board);
          }
          if (column < 0) break;
          MoveResult move;
          if (!playHeadlessMove(state, seed, column, move)) break;
        }
      }
    });
  }
  for (std::thread& thread : pool) thread.join();
  std::uint64_t totalDecisions = 0, totalMismatches = 0;
  std::cout << "{\"gate\": \"fastm6-m1-regression\", \"points\": [";
  for (std::size_t specIndex = 0; specIndex < specs.size(); ++specIndex) {
    M1Result sum;
    for (int game = 0; game < games; ++game) {
      const M1Result& r =
          results[specIndex * static_cast<std::size_t>(games) +
                  static_cast<std::size_t>(game)];
      sum.decisions += r.decisions;
      sum.memoOnMismatches += r.memoOnMismatches;
      sum.memoOffMismatches += r.memoOffMismatches;
    }
    totalDecisions += sum.decisions;
    totalMismatches += sum.memoOnMismatches + sum.memoOffMismatches;
    std::cout << (specIndex == 0 ? "" : ", ") << "{\"tag\": \""
              << specs[specIndex].tag << "\", \"decisions\": " << sum.decisions
              << ", \"memoOnMetricMismatches\": " << sum.memoOnMismatches
              << ", \"memoOffMetricMismatches\": " << sum.memoOffMismatches
              << "}";
  }
  std::cout << "], \"metricFieldsCompared\": \"action, completed_depth, nodes, "
               "work, cache_hits, cache_entries\", \"totalDecisions\": "
            << totalDecisions << ", \"totalMismatches\": " << totalMismatches
            << ", \"pass\": " << (totalMismatches == 0 ? "true" : "false")
            << "}\n";
  return totalMismatches == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Mode: subset (fast-only determinism + mirror + memo identity; no timing in
// the output, so byte identity across runs and thread counts is the check)
// ---------------------------------------------------------------------------

int runSubsetMode(int threads) {
  const auto specs = gridSpecs();
  struct SubsetResult {
    std::uint64_t decisions = 0;
    std::uint64_t memoMismatches = 0;
    std::uint64_t mirrorMismatches = 0;
    std::uint64_t mirrorSkippedSymmetric = 0;
    std::uint64_t traceHash = kFnvOffset;
  };
  std::vector<SubsetResult> results(specs.size());
  std::atomic<std::size_t> nextTask{0};
  std::vector<std::thread> pool;
  for (int worker = 0; worker < std::max(1, threads); ++worker) {
    pool.emplace_back([&]() {
      for (;;) {
        const std::size_t specIndex = nextTask.fetch_add(1);
        if (specIndex >= specs.size()) return;
        const GridSpec& spec = specs[specIndex];
        SubsetResult& result = results[specIndex];
        const std::uint32_t seed =
            kSmokeBase + static_cast<std::uint32_t>(specIndex);
        const int maximumMoves =
            (spec.depth == 4 && spec.revealSamples == 6) ? 6 : 12;
        FastFactoredSearch memoOn{fastParameters(spec, true)};
        FastFactoredSearch memoOff{fastParameters(spec, false)};
        FastFactoredSearch mirrorEngine{fastParameters(spec, true)};
        State state = initialHeadlessState(seed);
        while (!state.game_over && state.moves_played < maximumMoves) {
          if (!anyLegal(state.board)) break;
          FastSearchMetrics onMetrics, offMetrics, mirrorMetrics;
          memoOn.chooseAction(state, onMetrics);
          memoOff.chooseAction(state, offMetrics);
          State mirrored = state;
          mirrored.board = cfpi::detail::mirrorBoard(state.board);
          // A mirror-symmetric board maps to itself, so both orientations are
          // the same canonical state and the engine returns the same column,
          // not its mirror; finding-13's mirror gate (section 4C) excludes
          // symmetric boards for exactly this reason.
          const bool symmetric = mirrored.board == state.board;
          if (!symmetric) mirrorEngine.chooseAction(mirrored, mirrorMetrics);
          ++result.decisions;
          if (onMetrics.action != offMetrics.action ||
              onMetrics.work != offMetrics.work ||
              onMetrics.completed_depth != offMetrics.completed_depth ||
              onMetrics.nodes != offMetrics.nodes ||
              onMetrics.cache_hits != offMetrics.cache_hits ||
              onMetrics.cache_entries != offMetrics.cache_entries) {
            ++result.memoMismatches;
          }
          if (symmetric) {
            ++result.mirrorSkippedSymmetric;
          } else {
            const int expectedMirror =
                onMetrics.action < 0 ? onMetrics.action
                                     : kBoardSize - 1 - onMetrics.action;
            if (mirrorMetrics.action != expectedMirror ||
                mirrorMetrics.work != onMetrics.work ||
                mirrorMetrics.completed_depth != onMetrics.completed_depth) {
              ++result.mirrorMismatches;
            }
          }
          fnvAdd(result.traceHash, static_cast<std::uint64_t>(
                                       static_cast<std::int64_t>(onMetrics.action)));
          fnvAdd(result.traceHash, onMetrics.work);
          fnvAdd(result.traceHash,
                 static_cast<std::uint64_t>(onMetrics.completed_depth));
          int column = onMetrics.action;
          if (column < 0 || !isLegal(state.board, column)) {
            column = centerFirstMove(state.board);
          }
          if (column < 0) break;
          MoveResult move;
          if (!playHeadlessMove(state, seed, column, move)) break;
        }
      }
    });
  }
  for (std::thread& thread : pool) thread.join();
  std::uint64_t totalDecisions = 0, totalMismatches = 0;
  std::cout << "{\"gate\": \"fastm6-subset-determinism-mirror-memo\", "
               "\"smokeSeedBaseHex\": \"0xa51d8000\", \"points\": [";
  for (std::size_t specIndex = 0; specIndex < specs.size(); ++specIndex) {
    const SubsetResult& result = results[specIndex];
    totalDecisions += result.decisions;
    totalMismatches += result.memoMismatches + result.mirrorMismatches;
    std::cout << (specIndex == 0 ? "" : ", ") << "{\"tag\": \""
              << specs[specIndex].tag << "\", \"decisions\": "
              << result.decisions << ", \"memoOnOffMismatches\": "
              << result.memoMismatches << ", \"mirrorMismatches\": "
              << result.mirrorMismatches << ", \"mirrorSkippedSymmetric\": "
              << result.mirrorSkippedSymmetric << ", \"traceHash\": \""
              << std::hex << result.traceHash << std::dec << "\"}";
  }
  std::cout << "], \"totalDecisions\": " << totalDecisions
            << ", \"totalMismatches\": " << totalMismatches << ", \"pass\": "
            << (totalMismatches == 0 ? "true" : "false") << "}\n";
  return totalMismatches == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Mode: cont (continuation duty timing + outcome identity, G0v2 rung-2 style)
// ---------------------------------------------------------------------------

constexpr std::uint32_t kTapeDomain = 0x50534f4cu;  // "PSOL" (panel2 value)
constexpr std::uint32_t kMoveDomain = 0x434f4e54u;  // "CONT" (panel2 value)

inline std::uint32_t publicRootHash(const State& canonicalRoot) {
  std::uint32_t hash = 0x811c'9dc5u;
  for (std::uint8_t cell : canonicalRoot.board) {
    hash ^= static_cast<std::uint32_t>(cell + 1u);
    hash *= 0x0100'0193u;
  }
  hash ^= static_cast<std::uint32_t>(canonicalRoot.next_disc);
  hash *= 0x0100'0193u;
  hash ^= static_cast<std::uint32_t>(canonicalRoot.moves_remaining);
  hash *= 0x0100'0193u;
  return hash;
}

inline std::uint32_t tapeSeedFor(std::uint32_t rootHash, int continuation) {
  return mix32(rootHash ^ kTapeDomain ^
               ((static_cast<std::uint32_t>(continuation) + 1u) * 0x9e37'79b9u));
}

inline std::uint32_t moveSeedFor(std::uint32_t tapeSeed, int ordinal) {
  return mix32(tapeSeed ^ kMoveDomain ^
               ((static_cast<std::uint32_t>(ordinal) + 1u) * 0x85eb'ca6bu));
}

struct ContOutcome {
  int movesSurvived = 0;
  std::uint32_t clears = 0;
  std::uint32_t reveals = 0;
  int rises = 0;
  std::uint64_t columnHash = kFnvOffset;
  std::uint64_t work = 0;
  double cpuSeconds = 0.0;
};

template <typename Engine>
ContOutcome runContinuation(const State& canonicalRoot, std::uint32_t tapeSeed,
                            int horizon, Engine& engine) {
  ContOutcome outcome;
  State state = canonicalRoot;
  for (int ordinal = 0;; ++ordinal) {
    if (state.game_over || !anyLegal(state.board)) break;
    const double start = threadCpuSeconds();
    std::uint64_t work = 0;
    int column = engine.chooseAction(state, work);
    outcome.cpuSeconds += threadCpuSeconds() - start;
    outcome.work += work;
    if (column < 0 || !isLegal(state.board, column)) {
      column = centerFirstMove(state.board);
    }
    if (column < 0) break;
    fnvAdd(outcome.columnHash, static_cast<std::uint64_t>(column));
    Mulberry32 tape(moveSeedFor(tapeSeed, ordinal));
    MoveResult move;
    if (!cfpi::detail::playMoveSampled(state, column, tape, move)) break;
    for (const Wave& wave : move.waves) {
      outcome.clears += static_cast<std::uint32_t>(wave.cleared);
      outcome.reveals += static_cast<std::uint32_t>(wave.revealed);
    }
    if (move.level_advanced) ++outcome.rises;
    state = move.state;
    if (state.game_over) break;
    ++outcome.movesSurvived;
    if (outcome.movesSurvived >= horizon) break;
  }
  return outcome;
}

int runContMode(int threads) {
  const GridSpec c0{"d3-n7-m6", 3, 7, 6, 51'084'852, 87'025, "C0 exact"};
  // Roots: pre-move states at moves 40 and 80 of retained C0 game 0xa51d1005,
  // regenerated by the fast engine (whose replay-identity is proven by --mode
  // replay); already-read data, no new seed opened.
  const std::uint32_t rootSeed = 0xa51d1005u;
  const int captures[] = {40, 80};
  std::vector<State> roots;
  {
    FastFactoredSearch driver{fastParameters(c0, true)};
    State state = initialHeadlessState(rootSeed);
    while (!state.game_over && state.moves_played <= 80) {
      for (const int capture : captures) {
        if (state.moves_played == capture) {
          State root = state;
          root.score = 0;
          roots.push_back(root);
        }
      }
      if (!anyLegal(state.board)) break;
      std::uint64_t work = 0;
      int column = driver.chooseAction(state, work);
      if (column < 0 || !isLegal(state.board, column)) {
        column = centerFirstMove(state.board);
      }
      if (column < 0) break;
      MoveResult move;
      if (!playHeadlessMove(state, rootSeed, column, move)) break;
    }
  }
  if (roots.size() != std::size(captures)) {
    std::cerr << "cont: expected " << std::size(captures) << " roots, got "
              << roots.size() << "\n";
    return 1;
  }
  constexpr int kK = 4;
  constexpr int kHorizon = 40;
  struct Task {
    std::size_t root;
    int continuation;
    int engine;  // 0 fast, 1 native
  };
  std::vector<Task> tasks;
  for (std::size_t root = 0; root < roots.size(); ++root) {
    for (int continuation = 0; continuation < kK; ++continuation) {
      for (int engine = 0; engine < 2; ++engine) {
        tasks.push_back({root, continuation, engine});
      }
    }
  }
  std::vector<ContOutcome> outcomes(tasks.size());
  std::vector<State> canonicalRoots;
  std::vector<std::uint32_t> rootHashes;
  for (const State& root : roots) {
    bool mirrored = false;
    const State canonical = cfpi::detail::canonicalState(root, mirrored);
    canonicalRoots.push_back(canonical);
    rootHashes.push_back(publicRootHash(canonical));
  }
  std::atomic<std::size_t> nextTask{0};
  std::vector<std::thread> pool;
  for (int worker = 0; worker < std::max(1, threads); ++worker) {
    pool.emplace_back([&]() {
      for (;;) {
        const std::size_t index = nextTask.fetch_add(1);
        if (index >= tasks.size()) return;
        const Task& task = tasks[index];
        const std::uint32_t tapeSeed =
            tapeSeedFor(rootHashes[task.root], task.continuation);
        if (task.engine == 0) {
          FastFactoredSearch engine{fastParameters(c0, true)};
          outcomes[index] = runContinuation(canonicalRoots[task.root], tapeSeed,
                                            kHorizon, engine);
        } else {
          rs::FactoredSearch engine{nativeParameters(c0)};
          outcomes[index] = runContinuation(canonicalRoots[task.root], tapeSeed,
                                            kHorizon, engine);
        }
      }
    });
  }
  for (std::thread& thread : pool) thread.join();
  std::uint64_t mismatches = 0;
  std::uint64_t fastMoves = 0, nativeMoves = 0;
  double fastCpu = 0.0, nativeCpu = 0.0;
  std::cout << std::setprecision(10)
            << "{\"gate\": \"fastm6-continuation-duty\", \"rootGameSeedHex\": "
               "\"0xa51d1005\", \"rootMoves\": [40, 80], \"K\": " << kK
            << ", \"H\": " << kHorizon << ", \"continuations\": [";
  bool first = true;
  for (std::size_t index = 0; index < tasks.size(); index += 2) {
    const ContOutcome& fastOutcome = outcomes[index];
    const ContOutcome& nativeOutcome = outcomes[index + 1];
    const bool identical =
        fastOutcome.columnHash == nativeOutcome.columnHash &&
        fastOutcome.movesSurvived == nativeOutcome.movesSurvived &&
        fastOutcome.clears == nativeOutcome.clears &&
        fastOutcome.reveals == nativeOutcome.reveals &&
        fastOutcome.rises == nativeOutcome.rises &&
        fastOutcome.work == nativeOutcome.work;
    if (!identical) ++mismatches;
    const std::uint64_t moves =
        static_cast<std::uint64_t>(fastOutcome.movesSurvived) + 1;
    fastMoves += moves;
    nativeMoves += static_cast<std::uint64_t>(nativeOutcome.movesSurvived) + 1;
    fastCpu += fastOutcome.cpuSeconds;
    nativeCpu += nativeOutcome.cpuSeconds;
    std::cout << (first ? "" : ", ") << "{\"root\": " << tasks[index].root
              << ", \"continuation\": " << tasks[index].continuation
              << ", \"movesSurvived\": " << fastOutcome.movesSurvived
              << ", \"clears\": " << fastOutcome.clears << ", \"reveals\": "
              << fastOutcome.reveals << ", \"rises\": " << fastOutcome.rises
              << ", \"work\": " << fastOutcome.work
              << ", \"identicalToNative\": " << (identical ? "true" : "false")
              << "}";
    first = false;
  }
  std::cout << "], \"outcomeMismatches\": " << mismatches
            << ", \"continuationDuty\": {\"fastCpuSecondsPerMove\": "
            << (fastMoves ? fastCpu / static_cast<double>(fastMoves) : 0.0)
            << ", \"nativeCpuSecondsPerMove\": "
            << (nativeMoves ? nativeCpu / static_cast<double>(nativeMoves) : 0.0)
            << ", \"speedup\": " << (fastCpu > 0.0 ? nativeCpu / fastCpu : 0.0)
            << ", \"fastMoves\": " << fastMoves << ", \"nativeMoves\": "
            << nativeMoves << "}, \"pass\": "
            << (mismatches == 0 ? "true" : "false") << "}\n";
  return mismatches == 0 ? 0 : 1;
}

}  // namespace drop7::fastm6gate

int main(int argc, char** argv) {
  using namespace drop7::fastm6gate;
  std::string mode;
  int threads = 1;
  int games = 21;
  int maximumMoves = 25;
  try {
    for (int index = 1; index + 1 < argc; index += 2) {
      const std::string key = argv[index];
      const std::string value = argv[index + 1];
      if (key == "--mode") mode = value;
      else if (key == "--threads") threads = std::stoi(value);
      else if (key == "--games") games = std::stoi(value);
      else if (key == "--max-moves") maximumMoves = std::stoi(value);
      else throw std::invalid_argument("unknown option " + key);
    }
    if (mode == "grid") return runGridMode(threads, games, maximumMoves);
    if (mode == "replay") return runReplayMode(threads);
    if (mode == "m1") return runM1Mode(threads, games, maximumMoves);
    if (mode == "subset") return runSubsetMode(threads);
    if (mode == "cont") return runContMode(threads);
    std::cerr << "usage: gate --mode grid|replay|m1|subset|cont [--threads N]"
                 " [--games N] [--max-moves N]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "fastm6 gate failed: " << error.what() << '\n';
    return 1;
  }
}
