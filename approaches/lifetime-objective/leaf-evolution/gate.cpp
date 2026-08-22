// CHECK-tier gates for the weighted fast search.  No gameplay evidence is
// produced here; every seed read is a previously opened development probe.
//
//   --leaf-bits       weightedFairLeaf(frozen) == fastFairLeaf, bit for bit,
//                     on every board a real search visits (root states, and
//                     every stratified one-ply successor of every legal drop)
//   --search-parity   WeightedFastSearch(frozen) selects the same column and
//                     spends the same work, nodes, cache hits and completed
//                     depth as FastSearch on every move of every probe game
//   --perturbed       with a non-frozen vector: identical per-game results at
//                     1 and N threads (determinism / worker independence);
//                     mirrored input -> mirrored action with identical work
//                     (reflection); score, level and move counter changes do
//                     not change the decision (metadata blindness); every
//                     decision completes the requested depth and is legal
//
// Exit status is non-zero if any requested gate fails.

#include "weighted-search.hpp"
#include "fast-search.hpp"
#include "../common/harness.hpp"

#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace drop7;
using drop7::lifetime::CohortOptions;
using drop7::lifetime::GameRecord;
using drop7::lifetime::runCohort;

struct Config {
  int depth = 4;
  int strata = 7;
  std::size_t cache = 60'000;
  std::uint32_t seedStart = 0xa527'8000u;
  int games = 2;
  int moves = 40;
  int threads = 4;
  bool leafBits = false;
  bool searchParity = false;
  bool perturbed = false;
};

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

fast::FastSearchParameters fastParameters(const Config& c) {
  fast::FastSearchParameters p;
  p.depth = c.depth;
  p.chance_samples = c.strata;
  p.maximum_work = worstCaseWork(c.depth, c.strata) + 1;
  p.maximum_cache_entries = c.cache;
  return p;
}

fastw::FastSearchParameters weightedParameters(const Config& c) {
  fastw::FastSearchParameters p;
  p.depth = c.depth;
  p.chance_samples = c.strata;
  p.maximum_work = worstCaseWork(c.depth, c.strata) + 1;
  p.maximum_cache_entries = c.cache;
  return p;
}

std::uint64_t bitsOf(double value) {
  std::uint64_t b = 0;
  std::memcpy(&b, &value, sizeof b);
  return b;
}

bool symmetric(const Board& board) {
  return cfpi::detail::mirrorBoard(board) == board;
}

// Deterministic non-frozen vector: every coordinate scaled by a factor in
// [0.7, 1.3], one coordinate sign-flipped, so the search is exercised away from
// the frozen point in both magnitude and sign.
fastw::LeafWeights perturbedWeights() {
  fastw::LeafWeights w;
  std::mt19937 rng(0x1eaf'e5e5u);
  std::uniform_real_distribution<double> u(-0.3, 0.3);
  for (int i = 0; i < fastw::kLeafTerms; ++i) w.w[i] *= 1.0 + u(rng);
  w.w[4] = -w.w[4];
  return w;
}

// --------------------------------------------------------------------------
bool leafBits(const Config& c) {
  const fastw::LeafWeights frozen;
  if (!frozen.isFrozen()) throw std::runtime_error("default weights moved");
  fast::LeafScratch scratchMine, scratchTheirs;
  const auto parameters = weightedParameters(c);
  fastw::WeightedFastSearch search{parameters, frozen};
  std::uint64_t boards = 0, mismatches = 0, terminal = 0;
  auto compare = [&](const State& probe) {
    const double mine = fastw::weightedFairLeaf(probe, scratchMine, frozen);
    const double theirs = fast::fastFairLeaf(probe, scratchTheirs);
    ++boards;
    if (probe.game_over) ++terminal;
    if (bitsOf(mine) != bitsOf(theirs)) {
      if (mismatches < 5) {
        std::cerr << "  leaf mismatch: mine " << bitsOf(mine) << " frozen "
                  << bitsOf(theirs) << "\n";
      }
      ++mismatches;
    }
  };
  for (int game = 0; game < c.games; ++game) {
    const std::uint32_t seed = c.seedStart + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    int moves = 0;
    while (!state.game_over && moves < c.moves) {
      compare(state);
      const std::uint32_t stateSeed = cfpi::detail::scenarioSeedForState(
          state, parameters.policy_seed, c.depth);
      for (int column = 0; column < kBoardSize; ++column) {
        if (!isLegal(state.board, column)) continue;
        for (int sample = 0; sample < c.strata; ++sample) {
          fast::FastStratifiedRandom random{stateSeed, sample, c.strata, 0};
          fast::MinimalWaveSink sink;
          fast::FastMoveResult move;
          if (!fast::playMoveFast(state, column, random, sink, move)) continue;
          move.state.score = 0;
          move.state.next_disc =
              fast::fastSampledNextDisc(stateSeed, sample, c.strata);
          bool ignored = false;
          const State next = fast::canonicalStateFast(move.state, ignored);
          compare(next);
        }
      }
      fastw::FastSearchMetrics metrics;
      int column = search.chooseAction(state, metrics);
      if (column < 0 || !isLegal(state.board, column)) {
        column = centerFirstMove(state.board);
        if (column < 0) break;
      }
      MoveResult move;
      if (!playHeadlessMove(state, seed, column, move)) break;
      ++moves;
    }
  }
  std::cout << "leaf-bits: " << boards << " boards compared (" << terminal
            << " terminal), " << mismatches << " mismatches\n";
  return mismatches == 0 && boards > 0;
}

// --------------------------------------------------------------------------
bool searchParity(const Config& c) {
  const fastw::LeafWeights frozen;
  fast::FastSearch theirs{fastParameters(c)};
  fastw::WeightedFastSearch mine{weightedParameters(c), frozen};
  std::uint64_t compared = 0, actionMismatch = 0, workMismatch = 0,
                nodeMismatch = 0, hitMismatch = 0, depthMismatch = 0,
                totalWork = 0;
  for (int game = 0; game < c.games; ++game) {
    const std::uint32_t seed = c.seedStart + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    int moves = 0;
    while (!state.game_over && moves < c.moves) {
      fast::FastSearchMetrics a;
      fastw::FastSearchMetrics b;
      const int ca = theirs.chooseAction(state, a);
      const int cb = mine.chooseAction(state, b);
      ++compared;
      totalWork += a.work;
      if (ca != cb) ++actionMismatch;
      if (a.work != b.work) ++workMismatch;
      if (a.nodes != b.nodes) ++nodeMismatch;
      if (a.cache_hits != b.cache_hits) ++hitMismatch;
      if (a.completed_depth != b.completed_depth) ++depthMismatch;
      int column = ca;
      if (column < 0 || !isLegal(state.board, column)) {
        column = centerFirstMove(state.board);
        if (column < 0) break;
      }
      MoveResult move;
      if (!playHeadlessMove(state, seed, column, move)) break;
      ++moves;
    }
  }
  std::cout << "search-parity: " << compared << " moves compared, "
            << actionMismatch << " action, " << workMismatch << " work, "
            << nodeMismatch << " node, " << hitMismatch << " cache-hit, "
            << depthMismatch << " completed-depth mismatches; total work "
            << totalWork << "\n";
  return compared > 0 && actionMismatch == 0 && workMismatch == 0 &&
         nodeMismatch == 0 && hitMismatch == 0 && depthMismatch == 0;
}

// --------------------------------------------------------------------------
bool perturbedGates(const Config& c) {
  const fastw::LeafWeights weights = perturbedWeights();
  if (weights.isFrozen()) throw std::runtime_error("perturbation is frozen");
  const auto parameters = weightedParameters(c);
  bool ok = true;

  // Determinism and worker-count independence.
  auto play = [&](int threads) {
    CohortOptions options;
    options.seedStart = c.seedStart;
    options.games = c.games;
    options.maximumMoves = c.moves;
    options.threads = threads;
    options.recordActions = true;
    options.quiet = true;
    return runCohort(options, [&]() {
      return [search = fastw::WeightedFastSearch{parameters, weights}](
                 const State& state, std::uint64_t& work) mutable {
        fastw::FastSearchMetrics metrics;
        const int action = search.chooseAction(state, metrics);
        work += metrics.work;
        return action;
      };
    });
  };
  const std::vector<GameRecord> one = play(1);
  const std::vector<GameRecord> many = play(c.threads);
  std::uint64_t gameMismatch = 0;
  for (std::size_t i = 0; i < one.size(); ++i) {
    if (one[i].score != many[i].score || one[i].moves != many[i].moves ||
        one[i].work != many[i].work || one[i].actions != many[i].actions) {
      ++gameMismatch;
    }
  }
  std::cout << "determinism: " << one.size() << " games at 1 and " << c.threads
            << " threads, " << gameMismatch << " mismatches\n";
  ok = ok && gameMismatch == 0 && !one.empty();

  // Reflection, metadata blindness, completed depth, legality along real play.
  fastw::WeightedFastSearch search{parameters, weights};
  fastw::WeightedFastSearch mirrorSearch{parameters, weights};
  fastw::WeightedFastSearch metaSearch{parameters, weights};
  std::uint64_t decisions = 0, reflectionMismatch = 0, reflectionWork = 0,
                metadataMismatch = 0, incomplete = 0, illegal = 0,
                symmetricBoards = 0;
  for (int game = 0; game < c.games; ++game) {
    const std::uint32_t seed = c.seedStart + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    int moves = 0;
    while (!state.game_over && moves < c.moves) {
      fastw::FastSearchMetrics m;
      const int action = search.chooseAction(state, m);
      ++decisions;
      if (m.completed_depth != c.depth) ++incomplete;
      if (action < 0 || !isLegal(state.board, action)) ++illegal;

      State mirrored = state;
      mirrored.board = cfpi::detail::mirrorBoard(state.board);
      fastw::FastSearchMetrics mm;
      const int mirroredAction = mirrorSearch.chooseAction(mirrored, mm);
      if (symmetric(state.board)) {
        ++symmetricBoards;
        if (mirroredAction != action) ++reflectionMismatch;
      } else if (mirroredAction != kBoardSize - 1 - action) {
        ++reflectionMismatch;
      }
      if (mm.work != m.work) ++reflectionWork;

      State meta = state;
      meta.score = 123'456'789;
      meta.level = 77;
      meta.moves_played = 999;
      fastw::FastSearchMetrics mt;
      const int metaAction = metaSearch.chooseAction(meta, mt);
      if (metaAction != action || mt.work != m.work) ++metadataMismatch;

      int column = action;
      if (column < 0 || !isLegal(state.board, column)) {
        column = centerFirstMove(state.board);
        if (column < 0) break;
      }
      MoveResult move;
      if (!playHeadlessMove(state, seed, column, move)) break;
      ++moves;
    }
  }
  std::cout << "reflection: " << decisions << " decisions (" << symmetricBoards
            << " symmetric boards), " << reflectionMismatch
            << " action mismatches, " << reflectionWork << " work mismatches\n"
            << "metadata-blindness: " << metadataMismatch << " mismatches\n"
            << "completed-depth: " << incomplete << " incomplete of "
            << decisions << "; illegal " << illegal << "\n";
  ok = ok && decisions > 0 && reflectionMismatch == 0 && reflectionWork == 0 &&
       metadataMismatch == 0 && incomplete == 0 && illegal == 0;
  return ok;
}

Config parse(int argc, char** argv) {
  Config c;
  for (int i = 1; i < argc; ++i) {
    const std::string key = argv[i];
    if (key == "--leaf-bits") { c.leafBits = true; continue; }
    if (key == "--search-parity") { c.searchParity = true; continue; }
    if (key == "--perturbed") { c.perturbed = true; continue; }
    if (i + 1 >= argc) throw std::invalid_argument("missing value for " + key);
    const std::string value = argv[++i];
    if (key == "--depth") c.depth = std::stoi(value);
    else if (key == "--chance-samples") c.strata = std::stoi(value);
    else if (key == "--cache") c.cache = std::stoull(value);
    else if (key == "--seed-start") c.seedStart = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
    else if (key == "--games") c.games = std::stoi(value);
    else if (key == "--moves") c.moves = std::stoi(value);
    else if (key == "--threads") c.threads = std::stoi(value);
    else throw std::invalid_argument("unknown option " + key);
  }
  return c;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Config c = parse(argc, argv);
    std::cout << "gate: depth " << c.depth << ", strata " << c.strata
              << ", cache " << c.cache << ", seeds 0x" << std::hex << c.seedStart
              << std::dec << "+" << c.games << ", " << c.moves << " moves\n";
    bool ok = true;
    bool any = false;
    if (c.leafBits) { any = true; ok = leafBits(c) && ok; }
    if (c.searchParity) { any = true; ok = searchParity(c) && ok; }
    if (c.perturbed) { any = true; ok = perturbedGates(c) && ok; }
    if (!any) throw std::invalid_argument("name at least one gate");
    std::cout << (ok ? "GATE PASS\n" : "GATE FAIL\n");
    return ok ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "gate failed: " << error.what() << '\n';
    return 2;
  }
}
