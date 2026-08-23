// CHECK gates for the chain-reveal-leaf substrate.  No gameplay evidence is
// produced; every seed read is inside the already-opened development probe
// range 0xa5278000-0xa52784ff, and the program refuses any other seed.
//
//   --leaf-bits     on every board a real search visits (root states and every
//                   stratified one-ply successor of every legal drop, fed in
//                   the search's own order so the memo is exercised on hits):
//                     augmentedFairLeaf(zero weights) == fastFairLeaf, bit for
//                       bit;
//                     extra features on the board and on its horizontal
//                       mirror are equal, term by term;
//                     the memoised extra features equal freshly extracted ones
//                       (catches a term that reads next_disc but is memoised
//                       per (board, moves_remaining)).
//   --parity        AugmentedFastSearch(zero weights) selects the same column
//                   and spends the same work, nodes, cache hits and completed
//                   depth as FastSearch on every move of every probe game;
//                   prints the memo hit rate.
//   --determinism   with the live weight: identical per-game results at 1 and
//                   N threads.
//   --units         hand-built boards with known answers for the seven terms
//                   (support-disjoint rule, entombed_high, danger gate)
//   --live          with the live weights (--weights, default arm A):
//                   at least one decision differs from the frozen search over
//                   the probe games (the hook is reachable); score, level and
//                   move counter changes do not change the decision or its
//                   work (metadata blindness); mirrored input gives the
//                   mirrored action with identical work (reflection); every
//                   decision completes the requested depth and is legal.
//
// Exit status is non-zero if any requested gate fails.

#include "augmented-search.hpp"
#include "fast-search.hpp"
#include "../common/harness.hpp"

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace drop7;
using drop7::lifetime::CohortOptions;
using drop7::lifetime::GameRecord;
using drop7::lifetime::runCohort;

constexpr std::uint32_t kProbeFirst = 0xa527'8000u;
constexpr std::uint32_t kProbeLast = 0xa527'84ffu;

struct Config {
  int depth = 4;
  int strata = 5;
  std::size_t cache = 60'000;
  std::uint32_t seedStart = kProbeFirst;
  int games = 4;
  int moves = 40;
  int threads = 4;
  std::string weights = "aligned_double_hit=300";
  bool units = false;
  bool leafBits = false;
  bool parity = false;
  bool determinism = false;
  bool live = false;
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

template <typename P>
P parameters(const Config& c) {
  P p;
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

fastx::ExtraWeights liveWeights(const Config& c) {
  const fastx::ExtraWeights w = fastx::ExtraWeights::parse(c.weights);
  if (w.isFrozen()) throw std::runtime_error("live weights are all zero");
  return w;
}

// --------------------------------------------------------------------------
// Hand-built boards.  Seven rows top to bottom, '.' empty, '1'-'7' numbered,
// 'S' solid, 'C' cracked.  Boards are gravity-packed and contain no popping
// run, so the scratch the frozen extractor fills is the one real play sees.
State boardFrom(const char* const rows[7], int next = 3, int rise = 5) {
  State s;
  for (int r = 0; r < kBoardSize; ++r) {
    for (int c = 0; c < kBoardSize; ++c) {
      const char ch = rows[r][c];
      std::uint8_t cell = kEmpty;
      if (ch >= '1' && ch <= '7') cell = static_cast<std::uint8_t>(ch - '0');
      else if (ch == 'S') cell = kSolid;
      else if (ch == 'C') cell = kCracked;
      s.board[static_cast<std::size_t>(r * kBoardSize + c)] = cell;
    }
  }
  s.next_disc = static_cast<std::uint8_t>(next);
  s.moves_remaining = rise;
  return s;
}

fastx::ExtraFeatures extrasOf(const State& s, fast::LeafScratch& scratch) {
  fast::FastLeafFeatures ignored;
  fast::extractFastLeafFeatures<6>(s, scratch, ignored);
  fastx::ExtraFeatures out;
  fastx::extractExtraFeatures(s.board, s.moves_remaining, scratch, out);
  return out;
}

bool units() {
  int failures = 0;
  fast::LeafScratch scratch;
  auto expect = [&](const char* name, bool ok, double got) {
    if (!ok) ++failures;
    std::cout << "  " << name << ": " << got << (ok ? "" : "  MISMATCH") << "\n";
  };
  // U1: solid gray with two ready, support-disjoint (opposite-side, addition-
  // dominated) neighbours: both 2s complete vertically with one disc -> 1.0.
  const char* u1[7] = {".......", ".......", ".......", ".......", ".......", ".......", "..2S2.."};
  auto e1 = extrasOf(boardFrom(u1), scratch);
  expect("U1 aligned_double_hit (two ready neighbours) ~ 1", e1.v[0] > 0.99 && e1.v[0] <= 1.0, e1.v[0]);
  expect("U1 chain_to_crack_solid (release-only) > 0", e1.v[2] > 0.0, e1.v[2]);
  // U2: only one numbered neighbour -> no pair -> 0.
  const char* u2[7] = {".......", ".......", ".......", ".......", ".......", ".......", "..2S...."};
  auto e2 = extrasOf(boardFrom(u2), scratch);
  expect("U2 aligned_double_hit (one neighbour) == 0", e2.v[0] == 0.0, e2.v[0]);
  // U3: adjacent-side pair whose completion paths share cell (5,2): the 3 above
  // the gray needs (5,2) for its cheapest horizontal window and the 4 left of
  // the gray needs (5,2) for its vertical completion -> skipped -> 0.
  const char* u3[7] = {".......", ".......", ".......", ".......", ".......", "...3...", "..4S..."};
  {
    fast::FastLeafFeatures ignored;
    const State s = boardFrom(u3);
    fast::extractFastLeafFeatures<6>(s, scratch, ignored);
    const std::uint64_t pa = fastx::detail::completionPath(s.board, scratch, 5 * kBoardSize + 3);
    const std::uint64_t pb = fastx::detail::completionPath(s.board, scratch, 6 * kBoardSize + 2);
    const std::uint64_t shared = pa & pb;
    expect("U3 completion paths share (5,2)", (shared >> (5 * kBoardSize + 2)) & 1u, static_cast<double>(__builtin_popcountll(shared)));
    fastx::ExtraFeatures e3;
    fastx::extractExtraFeatures(s.board, s.moves_remaining, scratch, e3);
    expect("U3 aligned_double_hit (shared path) == 0", e3.v[0] == 0.0, e3.v[0]);
  }
  // U4: adjacent-side pair with empty completion paths (the 2 above the gray
  // is release-only; the 3 right of it completes vertically) -> counted, 1.0.
  const char* u4[7] = {".......", ".......", ".......", ".......", "..7....", ".425...", ".5S36.."};
  {
    fast::FastLeafFeatures ignored;
    const State s = boardFrom(u4);
    fast::extractFastLeafFeatures<6>(s, scratch, ignored);
    const std::uint64_t pa = fastx::detail::completionPath(s.board, scratch, 5 * kBoardSize + 2);
    const std::uint64_t pb = fastx::detail::completionPath(s.board, scratch, 6 * kBoardSize + 3);
    expect("U4 completion paths disjoint", (pa & pb) == 0, static_cast<double>(__builtin_popcountll(pa & pb)));
    fastx::ExtraFeatures e4;
    fastx::extractExtraFeatures(s.board, s.moves_remaining, scratch, e4);
    expect("U4 aligned_double_hit (disjoint adjacent pair) ~ 1", e4.v[0] > 0.99 && e4.v[0] <= 1.0, e4.v[0]);
  }
  // U5: entombed_high counts a 4 at column height 6 inside a row run of 5, and
  // not a 4 at column height 3 inside a row run of 5.
  const char* u5a[7] = {".......", "...7...", "...7...", "...7...", ".77477.", ".77777.", ".77777."};
  auto e5a = extrasOf(boardFrom(u5a), scratch);
  expect("U5a entombed_high (4 at height 6, run 5) > 0", e5a.v[3] > 0.0 && e5a.v[3] < 1.0, e5a.v[3]);
  const char* u5b[7] = {".......", ".......", ".......", ".......", "...7...", ".77477.", ".77777."};
  auto e5b = extrasOf(boardFrom(u5b), scratch);
  expect("U5b entombed_high (4 at height 3) == 0", e5b.v[3] == 0.0, e5b.v[3]);
  // U6: danger gate at max height 4 / 5 / 6 on the U1 board plus a column-0 stack.
  const char* u6a[7] = {".......", ".......", ".......", "7......", "7......", "7......", "7.2S2.."};
  const char* u6b[7] = {".......", ".......", "7......", "7......", "7......", "7......", "7.2S2.."};
  const char* u6c[7] = {".......", "7......", "7......", "7......", "7......", "7......", "7.2S2.."};
  auto e6a = extrasOf(boardFrom(u6a), scratch), e6b = extrasOf(boardFrom(u6b), scratch), e6c = extrasOf(boardFrom(u6c), scratch);
  expect("U6 gate at height 4 == 1.0 x term", e6a.v[0] > 0.99 && e6a.v[4] == e6a.v[0], e6a.v[4]);
  expect("U6 gate at height 5 == 0.5 x term", e6b.v[0] > 0.99 && e6b.v[4] == 0.5 * e6b.v[0], e6b.v[4]);
  expect("U6 gate at height 6 == 0", e6c.v[0] > 0.99 && e6c.v[4] == 0.0, e6c.v[4]);
  std::cout << "units: " << failures << " failures\n";
  return failures == 0;
}

// Plays the probe games with `search` deciding, calling `visit` on the root
// and (when wanted) on every stratified one-ply successor in the search's
// own order.  Shared by the leaf and live gates.
template <typename Search, typename Visit>
void walk(const Config& c, Search& search, bool successors, Visit visit) {
  const auto p = parameters<fastx::FastSearchParameters>(c);
  for (int game = 0; game < c.games; ++game) {
    const std::uint32_t seed = c.seedStart + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    int moves = 0;
    while (!state.game_over && moves < c.moves) {
      visit(state);
      if (successors) {
        const std::uint32_t stateSeed = cfpi::detail::scenarioSeedForState(
            state, p.policy_seed, c.depth);
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
            if (next.game_over) continue;
            visit(next);
          }
        }
      }
      fastx::FastSearchMetrics metrics;
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
}

// --------------------------------------------------------------------------
bool leafBits(const Config& c) {
  const fastx::ExtraWeights zero;
  fast::LeafScratch scratchMine, scratchTheirs, scratchMirror;
  fastx::AugmentedMemo memo;
  fastx::AugmentedFastSearch search{parameters<fastx::FastSearchParameters>(c), zero};
  std::uint64_t boards = 0, bitMismatch = 0, mirrorMismatch = 0,
                memoMismatch = 0, nonZeroTerms = 0;
  walk(c, search, true, [&](const State& probe) {
    ++boards;
    const double mine = fastx::augmentedFairLeaf(probe, scratchMine, memo, zero);
    const double theirs = fast::fastFairLeaf(probe, scratchTheirs);
    if (bitsOf(mine) != bitsOf(theirs)) {
      if (bitMismatch < 5) {
        std::cerr << "  leaf mismatch: mine " << bitsOf(mine) << " frozen "
                  << bitsOf(theirs) << "\n";
      }
      ++bitMismatch;
    }
    // Fresh extra features on the board and on its mirror; scratchTheirs holds
    // this board's scratch from the fastFairLeaf call above.
    fastx::ExtraFeatures fresh, mirrored;
    fastx::extractExtraFeatures(probe.board, probe.moves_remaining, scratchTheirs, fresh);
    State flipped = probe;
    flipped.board = cfpi::detail::mirrorBoard(probe.board);
    fast::FastLeafFeatures ignored;
    fast::extractFastLeafFeatures<6>(flipped, scratchMirror, ignored);
    fastx::extractExtraFeatures(flipped.board, flipped.moves_remaining, scratchMirror, mirrored);
    for (int i = 0; i < fastx::kExtraTerms; ++i) {
      if (bitsOf(fresh.v[i]) != bitsOf(mirrored.v[i])) ++mirrorMismatch;
      if (bitsOf(fresh.v[i]) != bitsOf(memo.extra.v[i])) ++memoMismatch;
      if (fresh.v[i] != 0.0) ++nonZeroTerms;
    }
  });
  std::cout << "leaf-bits d" << c.depth << "s" << c.strata << ": " << boards
            << " boards, " << bitMismatch << " bit mismatches, " << mirrorMismatch
            << " mirror mismatches, " << memoMismatch
            << " memo/fresh mismatches; extra terms non-zero on " << nonZeroTerms
            << " board-terms; memo hits " << memo.hits << "/" << memo.calls
            << " in feed order\n";
  return boards > 0 && bitMismatch == 0 && mirrorMismatch == 0 && memoMismatch == 0;
}

// --------------------------------------------------------------------------
bool parity(const Config& c) {
  const fastx::ExtraWeights zero;
  fast::FastSearch theirs{parameters<fast::FastSearchParameters>(c)};
  fastx::AugmentedFastSearch mine{parameters<fastx::FastSearchParameters>(c), zero};
  std::uint64_t compared = 0, actionMismatch = 0, workMismatch = 0,
                nodeMismatch = 0, hitMismatch = 0, depthMismatch = 0,
                totalWork = 0;
  const auto started = std::chrono::steady_clock::now();
  for (int game = 0; game < c.games; ++game) {
    const std::uint32_t seed = c.seedStart + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    int moves = 0;
    while (!state.game_over && moves < c.moves) {
      fast::FastSearchMetrics a;
      fastx::FastSearchMetrics b;
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
  const double wall =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  const auto& memo = mine.leafMemo();
  std::cout << "parity d" << c.depth << "s" << c.strata << ": " << compared
            << " moves compared over " << c.games << " games, " << actionMismatch
            << " action, " << workMismatch << " work, " << nodeMismatch << " node, "
            << hitMismatch << " cache-hit, " << depthMismatch
            << " completed-depth mismatches; total work " << totalWork
            << "; memo hit rate "
            << (memo.calls ? 100.0 * memo.hits / memo.calls : 0.0) << "% of "
            << memo.calls << " leaf calls; wall " << wall << "s (both searches)\n";
  return compared > 0 && actionMismatch == 0 && workMismatch == 0 &&
         nodeMismatch == 0 && hitMismatch == 0 && depthMismatch == 0;
}

// --------------------------------------------------------------------------
bool determinism(const Config& c) {
  const fastx::ExtraWeights weights = liveWeights(c);
  const auto p = parameters<fastx::FastSearchParameters>(c);
  auto play = [&](int threads) {
    CohortOptions options;
    options.seedStart = c.seedStart;
    options.games = c.games;
    options.maximumMoves = c.moves;
    options.threads = threads;
    options.recordActions = true;
    options.quiet = true;
    return runCohort(options, [&]() {
      return [search = fastx::AugmentedFastSearch{p, weights}](
                 const State& state, std::uint64_t& work) mutable {
        fastx::FastSearchMetrics metrics;
        const int action = search.chooseAction(state, metrics);
        work += metrics.work;
        return action;
      };
    });
  };
  const std::vector<GameRecord> one = play(1);
  const std::vector<GameRecord> many = play(c.threads);
  std::uint64_t mismatch = 0;
  for (std::size_t i = 0; i < one.size(); ++i) {
    if (one[i].score != many[i].score || one[i].moves != many[i].moves ||
        one[i].work != many[i].work || one[i].actions != many[i].actions) {
      ++mismatch;
    }
  }
  std::cout << "determinism d" << c.depth << "s" << c.strata << " (weights "
            << weights.describe() << "): " << one.size() << " games at 1 and "
            << c.threads << " threads, " << mismatch << " mismatches\n";
  return !one.empty() && mismatch == 0;
}

// --------------------------------------------------------------------------
bool live(const Config& c) {
  const fastx::ExtraWeights weights = liveWeights(c);
  const fastx::ExtraWeights zero;
  const auto p = parameters<fastx::FastSearchParameters>(c);
  fastx::AugmentedFastSearch search{p, weights};
  fastx::AugmentedFastSearch frozen{p, zero};
  fastx::AugmentedFastSearch mirrorSearch{p, weights};
  fastx::AugmentedFastSearch metaSearch{p, weights};
  std::uint64_t decisions = 0, divergent = 0, reflectionMismatch = 0,
                reflectionWork = 0, metadataMismatch = 0, incomplete = 0,
                illegal = 0, symmetricBoards = 0;
  walk(c, search, false, [&](const State& state) {
    fastx::FastSearchMetrics m;
    const int action = search.chooseAction(state, m);
    ++decisions;
    if (m.completed_depth != c.depth) ++incomplete;
    if (action < 0 || !isLegal(state.board, action)) ++illegal;

    fastx::FastSearchMetrics fm;
    if (frozen.chooseAction(state, fm) != action) ++divergent;

    State mirrored = state;
    mirrored.board = cfpi::detail::mirrorBoard(state.board);
    fastx::FastSearchMetrics mm;
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
    fastx::FastSearchMetrics mt;
    const int metaAction = metaSearch.chooseAction(meta, mt);
    if (metaAction != action || mt.work != m.work) ++metadataMismatch;
  });
  std::cout << "live d" << c.depth << "s" << c.strata << " (weights "
            << weights.describe() << "): " << decisions << " decisions, "
            << divergent << " differ from the frozen search (must be >= 1)\n"
            << "reflection: " << symmetricBoards << " symmetric boards, "
            << reflectionMismatch << " action mismatches, " << reflectionWork
            << " work mismatches\n"
            << "metadata-blindness: " << metadataMismatch << " mismatches\n"
            << "completed-depth: " << incomplete << " incomplete of " << decisions
            << "; illegal " << illegal << "\n";
  return decisions > 0 && divergent > 0 && reflectionMismatch == 0 &&
         reflectionWork == 0 && metadataMismatch == 0 && incomplete == 0 &&
         illegal == 0;
}

Config parse(int argc, char** argv) {
  Config c;
  for (int i = 1; i < argc; ++i) {
    const std::string key = argv[i];
    if (key == "--units") { c.units = true; continue; }
    if (key == "--leaf-bits") { c.leafBits = true; continue; }
    if (key == "--parity") { c.parity = true; continue; }
    if (key == "--determinism") { c.determinism = true; continue; }
    if (key == "--live") { c.live = true; continue; }
    if (i + 1 >= argc) throw std::invalid_argument("missing value for " + key);
    const std::string value = argv[++i];
    if (key == "--depth") c.depth = std::stoi(value);
    else if (key == "--chance-samples") c.strata = std::stoi(value);
    else if (key == "--cache") c.cache = std::stoull(value);
    else if (key == "--seed-start") c.seedStart = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
    else if (key == "--games") c.games = std::stoi(value);
    else if (key == "--moves") c.moves = std::stoi(value);
    else if (key == "--threads") c.threads = std::stoi(value);
    else if (key == "--weights") c.weights = value;
    else throw std::invalid_argument("unknown option " + key);
  }
  if (c.games < 1 || c.seedStart < kProbeFirst ||
      static_cast<std::uint64_t>(c.seedStart) + static_cast<std::uint64_t>(c.games) - 1 > kProbeLast) {
    throw std::invalid_argument("gate seeds must lie inside the opened probe range 0xa5278000-0xa52784ff");
  }
  return c;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Config c = parse(argc, argv);
    std::cout << "gate: depth " << c.depth << ", strata " << c.strata << ", cache "
              << c.cache << ", seeds 0x" << std::hex << c.seedStart << std::dec
              << "+" << c.games << ", " << c.moves << " moves, extra terms "
              << fastx::kExtraTerms << " (" << fastx::ExtraWeights{}.describe() << ")\n";
    bool ok = true;
    bool any = false;
    if (c.units) { any = true; ok = units() && ok; }
    if (c.leafBits) { any = true; ok = leafBits(c) && ok; }
    if (c.parity) { any = true; ok = parity(c) && ok; }
    if (c.determinism) { any = true; ok = determinism(c) && ok; }
    if (c.live) { any = true; ok = live(c) && ok; }
    if (!any) throw std::invalid_argument("name at least one gate");
    std::cout << (ok ? "GATE PASS\n" : "GATE FAIL\n");
    return ok ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "gate failed: " << error.what() << '\n';
    return 2;
  }
}
