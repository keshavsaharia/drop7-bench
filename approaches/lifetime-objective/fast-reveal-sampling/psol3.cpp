// P-SOL-3 stages T0 and S1 (EX-20260824-psol3-m6-ladder-corpus-gate-2d0167ad).
//
// All continuation arms are the E-FAST-M6 fast factored engine
// (drop7::fastr::FastFactoredSearch, trace-equivalent to the native
// FactoredSearch per RS-20260824T010000Z-8f3e9b4f):
//
//   d1m6    depth 1, N=7, M=6, worst-case work bound (never binds)
//   d2m6    depth 2, N=7, M=6, worst-case work bound (never binds)
//   d3n7m6  depth 3, N=7, M=6, exact C0 bounds (51,084,852 / 87,025)
//   d1ref   depth 1, N=5, M=1, frozen bounds -- writer-parity gate only:
//           bit-identical to the native d1 engine, so its panel2 file must be
//           BYTE-IDENTICAL to sibling-corpus/generate.cpp's d1 ladder output
//           on the same roots/K/H, proving this program's CRN tapes,
//           continuation semantics and serialization equal panel2's.
//
// Continuation semantics, CRN tape scheme (kTapeDomain "PSOL", kMoveDomain
// "CONT", publicRootHash over the canonical public root), root staging,
// and the 992-byte panel2 v2 record layout are line-faithful copies of
// approaches/lifetime-objective/sibling-corpus/generate.cpp (namespace
// panel2/ladder); the only difference is the engine behind chooseAction.
//
// Modes:
//   --mode t0      continuation-duty timing: R roots x 7 siblings x K x H,
//                  per-engine ms/move with per-root SD (thread CPU clock).
//   --mode ladder  runs the arms and writes <prefix>.<engine>.panel2 files
//                  for ladder.py; panel bytes carry no timing.

#include "fast-factored-search.hpp"

#include <time.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace drop7::psol3 {

using drop7::fastr::FastFactoredParameters;
using drop7::fastr::FastFactoredSearch;

inline double threadCpuSeconds() {
  timespec ts{};
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
  return static_cast<double>(ts.tv_sec) + 1e-9 * static_cast<double>(ts.tv_nsec);
}

// ---------------------------------------------------------------------------
// Engine specs
// ---------------------------------------------------------------------------

struct EngineSpec {
  std::uint8_t id = 0;
  int depth = 1;
  int discSamples = 7;
  int revealSamples = 6;
  std::uint64_t maximumWork = 0;
  std::size_t maximumCacheEntries = 60'000;
  std::string name;
};

inline std::uint64_t power(std::uint64_t base, int exponent) {
  std::uint64_t result = 1;
  for (int i = 0; i < exponent; ++i) result *= base;
  return result;
}

inline std::uint64_t worstCaseIterativeWork(std::uint64_t branches, int depth) {
  std::uint64_t result = 0;
  for (int d = 1; d <= depth; ++d) {
    for (int level = 1; level <= d; ++level) result += power(branches, level);
    result += power(branches, d);
  }
  return result;
}

inline EngineSpec engineSpecByName(const std::string& name) {
  // ids 0-4 are taken by generate.cpp's panel2 engines; d1ref must reuse id 0
  // (D1) so the writer-parity gate can be a byte comparison.
  if (name == "d1m6")
    return {5, 1, 7, 6, worstCaseIterativeWork(7ull * 7 * 6, 1), 60'000, "d1m6"};
  if (name == "d2m6")
    return {6, 2, 7, 6, worstCaseIterativeWork(7ull * 7 * 6, 2), 60'000, "d2m6"};
  if (name == "d3n7m6") return {7, 3, 7, 6, 51'084'852, 87'025, "d3n7m6"};
  if (name == "d1ref") return {0, 1, 5, 1, 3'200'000, 60'000, "d1ref"};
  throw std::invalid_argument("unknown engine " + name);
}

inline FastFactoredParameters parametersFor(const EngineSpec& spec) {
  FastFactoredParameters parameters;
  parameters.depth = spec.depth;
  parameters.chance_samples = spec.discSamples;
  parameters.reveal_samples = spec.revealSamples;
  parameters.maximum_work = spec.maximumWork;
  parameters.maximum_cache_entries = spec.maximumCacheEntries;
  parameters.use_leaf_memo = true;
  return parameters;
}

// ---------------------------------------------------------------------------
// Roots (jsonl, same layout as generate.cpp's ladder pool files)
// ---------------------------------------------------------------------------

struct PoolRoot {
  std::uint32_t tag = 0;
  std::uint16_t moveIndex = 0;
  State state{};
};

inline std::string jsonField(const std::string& line, const std::string& key) {
  const std::string needle = "\"" + key + "\": ";
  const std::size_t at = line.find(needle);
  if (at == std::string::npos)
    throw std::runtime_error("roots file missing field " + key);
  std::size_t start = at + needle.size();
  bool quoted = line[start] == '"';
  if (quoted) ++start;
  std::size_t end = start;
  while (end < line.size() &&
         (quoted ? line[end] != '"' : (line[end] != ',' && line[end] != '}')))
    ++end;
  return line.substr(start, end - start);
}

inline std::vector<PoolRoot> readRoots(const std::string& path) {
  std::ifstream file(path);
  if (!file) throw std::runtime_error("cannot open " + path);
  std::vector<PoolRoot> roots;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty()) continue;
    PoolRoot root;
    root.tag = static_cast<std::uint32_t>(std::stoul(jsonField(line, "tag")));
    root.moveIndex =
        static_cast<std::uint16_t>(std::stoul(jsonField(line, "moveIndex")));
    const std::string board = jsonField(line, "board");
    if (board.size() != static_cast<std::size_t>(kCellCount))
      throw std::runtime_error("bad board length in roots file");
    for (int cell = 0; cell < kCellCount; ++cell) {
      root.state.board[static_cast<std::size_t>(cell)] =
          static_cast<std::uint8_t>(board[static_cast<std::size_t>(cell)] - '0');
    }
    root.state.next_disc =
        static_cast<std::uint8_t>(std::stoi(jsonField(line, "nextDisc")));
    root.state.moves_remaining = std::stoi(jsonField(line, "movesRemaining"));
    roots.push_back(std::move(root));
  }
  return roots;
}

// ---------------------------------------------------------------------------
// panel2 constants, root staging, CRN tapes (generate.cpp line-faithful)
// ---------------------------------------------------------------------------

constexpr std::uint32_t kVersion = 0x0200;
constexpr int kHeaderBytes = 96;
constexpr int kSiblingBytes = 128;
constexpr int kRecordBytes = kHeaderBytes + kBoardSize * kSiblingBytes;
static_assert(kRecordBytes == 992, "PanelRecordV2 must stay 992 bytes");
constexpr std::uint32_t kTapeDomain = 0x50534f4cu;  // "PSOL"
constexpr std::uint32_t kMoveDomain = 0x434f4e54u;  // "CONT"

inline std::uint8_t legalMaskOf(const Board& board) {
  std::uint8_t mask = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    if (isLegal(board, column)) mask |= static_cast<std::uint8_t>(1u << column);
  }
  return mask;
}

inline State publicOnlyCanonicalRoot(const State& rootState, bool& mirrored) {
  State canonical = cfpi::detail::canonicalState(rootState, mirrored);
  canonical.score = 0;
  canonical.level = 1;
  canonical.moves_played = 0;
  canonical.game_over = false;
  return canonical;
}

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

struct RootStaging {
  State rootState{};
  State canonicalRoot{};
  std::uint32_t rootHash = 0;
  bool mirroredRoot = false;
  std::uint8_t legalMask = 0;
  std::uint16_t moveIndex = 0;
  std::uint32_t tag = 0;
};

inline RootStaging stagingFor(const PoolRoot& root) {
  RootStaging staging;
  staging.rootState = root.state;
  staging.legalMask = legalMaskOf(root.state.board);
  staging.moveIndex = root.moveIndex;
  staging.tag = root.tag;
  staging.canonicalRoot =
      publicOnlyCanonicalRoot(staging.rootState, staging.mirroredRoot);
  staging.rootHash = publicRootHash(staging.canonicalRoot);
  return staging;
}

struct ContinuationOutcome {
  std::uint8_t lifetime = 0;
  std::uint8_t deathRise = 0;
  std::uint32_t clears = 0;
  std::uint32_t reveals = 0;
  std::uint64_t movesPlayed = 0;
  double cpuSeconds = 0.0;  // never serialized
};

// generate.cpp panel2::runContinuation, with FastFactoredSearch behind
// chooseAction and a thread-CPU stopwatch around the whole continuation.
inline ContinuationOutcome runContinuation(const State& canonicalRoot,
                                           int canonicalColumn,
                                           std::uint32_t tapeSeed, int horizon,
                                           const EngineSpec& spec) {
  const double started = threadCpuSeconds();
  FastFactoredSearch engine{parametersFor(spec)};
  ContinuationOutcome outcome;
  State state = canonicalRoot;
  int column = canonicalColumn;
  int survivedMoves = 0;
  int rises = 0;
  for (int ordinal = 0;; ++ordinal) {
    Mulberry32 tape(moveSeedFor(tapeSeed, ordinal));
    MoveResult move;
    if (!cfpi::detail::playMoveSampled(state, column, tape, move)) break;
    ++outcome.movesPlayed;
    for (const Wave& wave : move.waves) {
      outcome.clears += static_cast<std::uint32_t>(wave.cleared);
      outcome.reveals += static_cast<std::uint32_t>(wave.revealed);
    }
    if (move.level_advanced) ++rises;
    state = move.state;
    if (state.game_over) break;
    ++survivedMoves;
    if (survivedMoves >= horizon) {
      outcome.lifetime = static_cast<std::uint8_t>(horizon);
      outcome.deathRise = 0;  // censored
      outcome.cpuSeconds = threadCpuSeconds() - started;
      return outcome;
    }
    std::uint64_t work = 0;
    column = engine.chooseAction(state, work);
    if (column < 0 || !isLegal(state.board, column)) {
      column = centerFirstMove(state.board);
    }
    if (column < 0) break;
  }
  outcome.lifetime = static_cast<std::uint8_t>(std::min(survivedMoves, horizon));
  outcome.deathRise = static_cast<std::uint8_t>(std::min(rises + 1, 12));
  outcome.cpuSeconds = threadCpuSeconds() - started;
  return outcome;
}

struct Plan {
  std::vector<RootStaging> roots;
  std::vector<EngineSpec> engines;
  int k = 8;
  int horizon = 40;
  std::vector<ContinuationOutcome> outcomes;  // [root][engine][7][K]

  std::size_t indexOf(std::size_t rootIndex, std::size_t engineIndex,
                      int sibling, int continuation) const {
    return ((rootIndex * engines.size() + engineIndex) * kBoardSize +
            static_cast<std::size_t>(sibling)) * static_cast<std::size_t>(k) +
           static_cast<std::size_t>(continuation);
  }
};

inline void runPlan(Plan& plan, int threads) {
  struct Task {
    std::uint32_t rootIndex;
    std::uint16_t engineIndex;
    std::uint8_t sibling;
    std::uint8_t continuation;
  };
  std::vector<Task> tasks;
  for (std::size_t rootIndex = 0; rootIndex < plan.roots.size(); ++rootIndex) {
    const RootStaging& root = plan.roots[rootIndex];
    for (std::size_t engineIndex = 0; engineIndex < plan.engines.size();
         ++engineIndex) {
      for (int sibling = 0; sibling < kBoardSize; ++sibling) {
        if (!((root.legalMask >> sibling) & 1u)) continue;
        for (int j = 0; j < plan.k; ++j) {
          tasks.push_back({static_cast<std::uint32_t>(rootIndex),
                           static_cast<std::uint16_t>(engineIndex),
                           static_cast<std::uint8_t>(sibling),
                           static_cast<std::uint8_t>(j)});
        }
      }
    }
  }
  plan.outcomes.assign(plan.roots.size() * plan.engines.size() * kBoardSize *
                           static_cast<std::size_t>(plan.k),
                       ContinuationOutcome{});
  std::atomic<std::size_t> nextTask{0};
  const int workerCount =
      std::max(1, std::min<int>(threads, static_cast<int>(tasks.size())));
  std::vector<std::thread> pool;
  for (int worker = 0; worker < workerCount; ++worker) {
    pool.emplace_back([&]() {
      for (;;) {
        const std::size_t taskIndex = nextTask.fetch_add(1);
        if (taskIndex >= tasks.size()) return;
        const Task& task = tasks[taskIndex];
        const RootStaging& root = plan.roots[task.rootIndex];
        const EngineSpec& spec = plan.engines[task.engineIndex];
        const int canonicalColumn =
            root.mirroredRoot ? kBoardSize - 1 - task.sibling : task.sibling;
        const std::uint32_t tapeSeed =
            tapeSeedFor(root.rootHash, task.continuation);
        plan.outcomes[plan.indexOf(task.rootIndex, task.engineIndex,
                                   task.sibling, task.continuation)] =
            runContinuation(root.canonicalRoot, canonicalColumn, tapeSeed,
                            plan.horizon, spec);
      }
    });
  }
  for (std::thread& thread : pool) thread.join();
}

template <typename Scalar>
inline void putScalar(std::uint8_t* out, Scalar value) {
  std::memcpy(out, &value, sizeof(Scalar));  // little-endian host assumed
}

// generate.cpp panel2::serializeRecord for ladder-mode roots: chosenColumn
// and referenceColumn 255, panelFlags 0, one-step after* fields zeroed
// (synthetic roots have no origin tape), slot[52] = legal.
inline void serializeRecord(std::uint8_t* out, const RootStaging& root,
                            std::uint32_t recordId, std::uint32_t originSeed,
                            const EngineSpec& spec, int k, int horizon,
                            const ContinuationOutcome* outcomes /* [7][K] */) {
  std::memset(out, 0, kRecordBytes);
  putScalar<std::uint32_t>(out + 0, kVersion);
  putScalar<std::uint32_t>(out + 4, recordId);
  putScalar<std::uint32_t>(out + 8, originSeed);
  putScalar<std::uint16_t>(out + 12, root.moveIndex);
  out[14] = root.rootState.next_disc;
  out[15] = static_cast<std::uint8_t>(root.rootState.moves_remaining);
  out[16] = root.legalMask;
  out[17] = 255;  // chosenColumn (ladder roots stage none)
  out[18] = 255;  // referenceColumn (not computed)
  out[19] = spec.id;
  out[20] = static_cast<std::uint8_t>(k);
  out[21] = static_cast<std::uint8_t>(horizon);
  out[22] = 0;  // panelFlags
  std::memcpy(out + 23, root.rootState.board.data(), kCellCount);
  for (int sibling = 0; sibling < kBoardSize; ++sibling) {
    std::uint8_t* slot = out + kHeaderBytes + sibling * kSiblingBytes;
    const bool legal = (root.legalMask >> sibling) & 1u;
    if (!legal) continue;
    slot[52] = 1;  // legal
    std::uint32_t clearsTotal = 0, revealsTotal = 0;
    for (int j = 0; j < k; ++j) {
      const ContinuationOutcome& outcome = outcomes[sibling * k + j];
      slot[60 + j] = outcome.lifetime;
      slot[60 + k + j] = outcome.deathRise;
      clearsTotal += outcome.clears;
      revealsTotal += outcome.reveals;
    }
    putScalar<std::uint32_t>(slot + 60 + 2 * k, clearsTotal);
    putScalar<std::uint32_t>(slot + 64 + 2 * k, revealsTotal);
  }
}

// ---------------------------------------------------------------------------
// Per-engine aggregation shared by both modes
// ---------------------------------------------------------------------------

struct EngineAggregate {
  std::uint64_t moves = 0;
  double cpu = 0.0;
  std::vector<double> perRootMs;  // per-root ms/move
  std::vector<std::uint64_t> perRootMoves;
};

inline std::vector<EngineAggregate> aggregate(const Plan& plan) {
  std::vector<EngineAggregate> aggregates(plan.engines.size());
  for (std::size_t engineIndex = 0; engineIndex < plan.engines.size();
       ++engineIndex) {
    EngineAggregate& agg = aggregates[engineIndex];
    for (std::size_t rootIndex = 0; rootIndex < plan.roots.size(); ++rootIndex) {
      std::uint64_t rootMoves = 0;
      double rootCpu = 0.0;
      for (int sibling = 0; sibling < kBoardSize; ++sibling) {
        if (!((plan.roots[rootIndex].legalMask >> sibling) & 1u)) continue;
        for (int j = 0; j < plan.k; ++j) {
          const ContinuationOutcome& outcome =
              plan.outcomes[plan.indexOf(rootIndex, engineIndex, sibling, j)];
          rootMoves += outcome.movesPlayed;
          rootCpu += outcome.cpuSeconds;
        }
      }
      agg.moves += rootMoves;
      agg.cpu += rootCpu;
      agg.perRootMoves.push_back(rootMoves);
      agg.perRootMs.push_back(
          rootMoves ? 1000.0 * rootCpu / static_cast<double>(rootMoves) : 0.0);
    }
  }
  return aggregates;
}

inline void printEngineJson(const Plan& plan,
                            const std::vector<EngineAggregate>& aggregates,
                            bool perRoot) {
  std::cout << "\"engines\": {";
  for (std::size_t engineIndex = 0; engineIndex < plan.engines.size();
       ++engineIndex) {
    const EngineSpec& spec = plan.engines[engineIndex];
    const EngineAggregate& agg = aggregates[engineIndex];
    double mean = 0.0, sd = 0.0;
    for (double v : agg.perRootMs) mean += v;
    if (!agg.perRootMs.empty()) mean /= static_cast<double>(agg.perRootMs.size());
    for (double v : agg.perRootMs) sd += (v - mean) * (v - mean);
    if (agg.perRootMs.size() > 1)
      sd = std::sqrt(sd / static_cast<double>(agg.perRootMs.size() - 1));
    std::cout << (engineIndex == 0 ? "" : ", ") << "\"" << spec.name
              << "\": {\"engineId\": " << static_cast<int>(spec.id)
              << ", \"depth\": " << spec.depth << ", \"discSamples\": "
              << spec.discSamples << ", \"revealSamples\": " << spec.revealSamples
              << ", \"maximumWork\": " << spec.maximumWork
              << ", \"maximumCacheEntries\": " << spec.maximumCacheEntries
              << ", \"moves\": " << agg.moves << ", \"cpuSeconds\": " << agg.cpu
              << ", \"msPerMove\": "
              << (agg.moves ? 1000.0 * agg.cpu / static_cast<double>(agg.moves)
                            : 0.0)
              << ", \"perRootMsPerMoveMean\": " << mean
              << ", \"perRootMsPerMoveSD\": " << sd;
    if (perRoot) {
      std::cout << ", \"perRootMsPerMove\": [";
      for (std::size_t i = 0; i < agg.perRootMs.size(); ++i)
        std::cout << (i == 0 ? "" : ", ") << agg.perRootMs[i];
      std::cout << "], \"perRootMoves\": [";
      for (std::size_t i = 0; i < agg.perRootMoves.size(); ++i)
        std::cout << (i == 0 ? "" : ", ") << agg.perRootMoves[i];
      std::cout << "]";
    }
    std::cout << "}";
  }
  std::cout << "}";
}

}  // namespace drop7::psol3

int main(int argc, char** argv) {
  using namespace drop7::psol3;
  std::string mode, rootsPath, prefix, engineList = "d3n7m6,d2m6,d1m6";
  int threads = 26, k = 8, horizon = 40, limit = 0, skip = 0;
  try {
    for (int index = 1; index + 1 < argc; index += 2) {
      const std::string key = argv[index];
      const std::string value = argv[index + 1];
      if (key == "--mode") mode = value;
      else if (key == "--roots") rootsPath = value;
      else if (key == "--engines") engineList = value;
      else if (key == "--threads") threads = std::stoi(value);
      else if (key == "--k") k = std::stoi(value);
      else if (key == "--horizon") horizon = std::stoi(value);
      else if (key == "--limit") limit = std::stoi(value);
      else if (key == "--skip") skip = std::stoi(value);
      else if (key == "--panel2") prefix = value;
      else throw std::invalid_argument("unknown option " + key);
    }
    if (mode != "t0" && mode != "ladder")
      throw std::invalid_argument("--mode t0|ladder required");
    if (rootsPath.empty()) throw std::invalid_argument("--roots required");
    if (k < 1 || k > 30) throw std::invalid_argument("--k out of range");

    Plan plan;
    plan.k = k;
    plan.horizon = horizon;
    {
      const auto pool = readRoots(rootsPath);
      for (std::size_t i = static_cast<std::size_t>(skip); i < pool.size(); ++i) {
        if (limit > 0 && plan.roots.size() >= static_cast<std::size_t>(limit))
          break;
        plan.roots.push_back(stagingFor(pool[i]));
      }
    }
    if (plan.roots.empty()) throw std::runtime_error("no roots selected");
    {
      std::stringstream stream(engineList);
      std::string name;
      while (std::getline(stream, name, ','))
        if (!name.empty()) plan.engines.push_back(engineSpecByName(name));
    }
    const auto started = std::chrono::steady_clock::now();
    runPlan(plan, threads);
    const double wall = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started)
                            .count();

    if (mode == "ladder") {
      if (prefix.empty()) throw std::invalid_argument("--panel2 required");
      std::vector<std::uint8_t> buffer(kRecordBytes);
      std::vector<ContinuationOutcome> slice(
          static_cast<std::size_t>(drop7::kBoardSize) * static_cast<std::size_t>(k));
      for (std::size_t engineIndex = 0; engineIndex < plan.engines.size();
           ++engineIndex) {
        const std::string path =
            prefix + "." + plan.engines[engineIndex].name + ".panel2";
        std::FILE* file = std::fopen(path.c_str(), "wb");
        if (file == nullptr) throw std::runtime_error("cannot open " + path);
        for (std::size_t rootIndex = 0; rootIndex < plan.roots.size();
             ++rootIndex) {
          for (int sibling = 0; sibling < drop7::kBoardSize; ++sibling)
            for (int j = 0; j < k; ++j)
              slice[static_cast<std::size_t>(sibling * k + j)] = plan.outcomes
                  [plan.indexOf(rootIndex, engineIndex, sibling, j)];
          serializeRecord(buffer.data(), plan.roots[rootIndex],
                          static_cast<std::uint32_t>(rootIndex),
                          plan.roots[rootIndex].tag, plan.engines[engineIndex],
                          k, horizon, slice.data());
          std::fwrite(buffer.data(), 1, buffer.size(), file);
        }
        std::fclose(file);
      }
    }

    const auto aggregates = aggregate(plan);
    std::cout << std::setprecision(10) << "{\"mode\": \"" << mode
              << "\", \"roots\": " << plan.roots.size() << ", \"k\": " << k
              << ", \"horizon\": " << horizon << ", \"threads\": " << threads
              << ", \"rootTags\": [";
    for (std::size_t i = 0; i < plan.roots.size(); ++i)
      std::cout << (i == 0 ? "" : ", ") << plan.roots[i].tag;
    std::cout << "], ";
    printEngineJson(plan, aggregates, mode == "t0");
    std::cout << ", \"wallSeconds\": " << wall << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "psol3 failed: " << error.what() << '\n';
    return 1;
  }
}
