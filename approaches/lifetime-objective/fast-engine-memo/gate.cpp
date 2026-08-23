// CHECK gates for the one-entry leaf memo.  Probe seeds are previously opened
// development ranges; no gameplay evidence is produced.
//
//   --leaf-bits    fastFairLeafMemo == fastFairLeaf, bit for bit, on every
//                  board a real search visits, fed in the search's own order
//                  (root states and every stratified one-ply successor), so
//                  the memo is exercised on hits as well as misses
//   --parity       MemoSearch selects the same column with the same work,
//                  nodes, cache hits and completed depth as FastSearch on
//                  every move of every probe game; prints the memo hit rate
//   --determinism  identical per-game results at 1 and N threads
//   --timing       interleaved per-root timing ratio FastSearch / MemoSearch
//                  (indicative; the host may be loaded)

#include "memo-search.hpp"
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

std::uint64_t worstCaseWork(int depth, int strata) {
  const auto b = static_cast<std::uint64_t>(kBoardSize) * static_cast<std::uint64_t>(strata);
  std::uint64_t total = 0;
  for (int d = 1; d <= depth; ++d) { std::uint64_t p = 1; for (int l = 1; l <= d; ++l) { p *= b; total += p; } total += p; }
  return total;
}
template <typename P> P params(int depth, int strata) {
  P p; p.depth = depth; p.chance_samples = strata; p.maximum_work = worstCaseWork(depth, strata) + 1; p.maximum_cache_entries = 60'000; return p;
}
std::uint64_t bitsOf(double v) { std::uint64_t b; std::memcpy(&b, &v, 8); return b; }

bool leafBits(std::uint32_t seedStart, int games, int moves, int depth, int strata) {
  fast::LeafScratch s1, s2;
  fastm::LeafMemo memo;
  fastm::MemoSearch search{params<fastm::FastSearchParameters>(depth, strata)};
  const std::uint32_t policySeed = fastm::FastSearchParameters{}.policy_seed;
  std::uint64_t boards = 0, mismatches = 0;
  auto compare = [&](const State& probe) {
    const double mine = fastm::fastFairLeafMemo(probe, s1, memo);
    const double theirs = fast::fastFairLeaf(probe, s2);
    ++boards;
    if (bitsOf(mine) != bitsOf(theirs)) ++mismatches;
  };
  for (int g = 0; g < games; ++g) {
    const std::uint32_t seed = seedStart + static_cast<std::uint32_t>(g);
    State state = initialHeadlessState(seed);
    int played = 0;
    while (!state.game_over && played < moves) {
      compare(state);
      const std::uint32_t stateSeed = cfpi::detail::scenarioSeedForState(state, policySeed, depth);
      for (int column = 0; column < kBoardSize; ++column) {
        if (!isLegal(state.board, column)) continue;
        for (int sample = 0; sample < strata; ++sample) {  // the search's order: strata consecutively per column
          fast::FastStratifiedRandom random{stateSeed, sample, strata, 0};
          fast::MinimalWaveSink sink; fast::FastMoveResult move;
          if (!fast::playMoveFast(state, column, random, sink, move)) continue;
          move.state.score = 0;
          move.state.next_disc = fast::fastSampledNextDisc(stateSeed, sample, strata);
          bool ignored = false;
          const State next = fast::canonicalStateFast(move.state, ignored);
          if (next.game_over) continue;
          compare(next);
        }
      }
      fastm::FastSearchMetrics m; int column = search.chooseAction(state, m);
      if (column < 0 || !isLegal(state.board, column)) { column = centerFirstMove(state.board); if (column < 0) break; }
      MoveResult mv; if (!playHeadlessMove(state, seed, column, mv)) break; ++played;
    }
  }
  std::cout << "leaf-bits: " << boards << " boards compared, " << mismatches << " mismatches; memo hits "
            << memo.hits << "/" << memo.calls << " (" << (memo.calls ? 100.0 * memo.hits / memo.calls : 0.0) << "%) in feed order\n";
  return boards > 0 && mismatches == 0;
}

bool parity(std::uint32_t seedStart, int games, int moves, int depth, int strata) {
  fast::FastSearch theirs{params<fast::FastSearchParameters>(depth, strata)};
  fastm::MemoSearch mine{params<fastm::FastSearchParameters>(depth, strata)};
  std::uint64_t compared = 0, mismatch = 0, totalWork = 0;
  for (int g = 0; g < games; ++g) {
    const std::uint32_t seed = seedStart + static_cast<std::uint32_t>(g);
    State s = initialHeadlessState(seed);
    int played = 0;
    while (!s.game_over && played < moves) {
      fast::FastSearchMetrics a; fastm::FastSearchMetrics b;
      const int ca = theirs.chooseAction(s, a); const int cb = mine.chooseAction(s, b);
      ++compared; totalWork += a.work;
      if (ca != cb || a.work != b.work || a.nodes != b.nodes || a.cache_hits != b.cache_hits || a.completed_depth != b.completed_depth) ++mismatch;
      int column = ca; if (column < 0 || !isLegal(s.board, column)) { column = centerFirstMove(s.board); if (column < 0) break; }
      MoveResult mv; if (!playHeadlessMove(s, seed, column, mv)) break; ++played;
    }
  }
  const auto& memo = mine.leafMemo();
  std::cout << "parity d" << depth << "s" << strata << ": " << compared << " moves compared, " << mismatch
            << " action/work/node/hit/depth mismatches, total work " << totalWork << "; memo hit rate "
            << (memo.calls ? 100.0 * memo.hits / memo.calls : 0.0) << "% of " << memo.calls << " leaf calls\n";
  return compared > 0 && mismatch == 0;
}

bool determinism(std::uint32_t seedStart, int games, int moves, int depth, int strata, int threads) {
  auto play = [&](int t) {
    CohortOptions o; o.seedStart = seedStart; o.games = games; o.maximumMoves = moves; o.threads = t; o.recordActions = true; o.quiet = true;
    return runCohort(o, [&]() {
      return [search = fastm::MemoSearch{params<fastm::FastSearchParameters>(depth, strata)}](const State& s, std::uint64_t& work) mutable {
        fastm::FastSearchMetrics m; const int a = search.chooseAction(s, m); work += m.work; return a;
      };
    });
  };
  const auto one = play(1), many = play(threads);
  std::uint64_t mismatch = 0;
  for (std::size_t i = 0; i < one.size(); ++i) if (one[i].score != many[i].score || one[i].moves != many[i].moves || one[i].work != many[i].work || one[i].actions != many[i].actions) ++mismatch;
  std::cout << "determinism: " << one.size() << " games at 1 and " << threads << " threads, " << mismatch << " mismatches\n";
  return !one.empty() && mismatch == 0;
}

void timing(std::uint32_t seedStart, int games, int moves, int depth, int strata, int reps) {
  // roots from real play, then interleaved decisions; best-of-reps ratio (indicative under load)
  std::vector<State> roots;
  fast::FastSearch play{params<fast::FastSearchParameters>(2, 5)};
  for (int g = 0; g < games; ++g) {
    const std::uint32_t seed = seedStart + static_cast<std::uint32_t>(g);
    State s = initialHeadlessState(seed); int played = 0;
    while (!s.game_over && played < moves) {
      if (played % 10 == 5) roots.push_back(s);
      fast::FastSearchMetrics m; int c = play.chooseAction(s, m); if (c < 0 || !isLegal(s.board, c)) { c = centerFirstMove(s.board); if (c < 0) break; }
      MoveResult mv; if (!playHeadlessMove(s, seed, c, mv)) break; ++played;
    }
  }
  fast::FastSearch plain{params<fast::FastSearchParameters>(depth, strata)};
  fastm::MemoSearch memo{params<fastm::FastSearchParameters>(depth, strata)};
  double bestPlain = 1e18, bestMemo = 1e18; int mismatch = 0;
  for (int rep = 0; rep < reps; ++rep) {
    double tp = 0, tm = 0;
    for (const State& root : roots) {
      fast::FastSearchMetrics a; fastm::FastSearchMetrics b;
      const auto t0 = std::chrono::steady_clock::now(); const int ca = plain.chooseAction(root, a);
      const auto t1 = std::chrono::steady_clock::now(); const int cb = memo.chooseAction(root, b);
      const auto t2 = std::chrono::steady_clock::now();
      tp += std::chrono::duration<double>(t1 - t0).count(); tm += std::chrono::duration<double>(t2 - t1).count();
      if (ca != cb || a.work != b.work) ++mismatch;
    }
    bestPlain = std::min(bestPlain, tp); bestMemo = std::min(bestMemo, tm);
  }
  std::cout << "timing d" << depth << "s" << strata << ": " << roots.size() << " real roots x " << reps << " reps, plain " << bestPlain
            << "s memo " << bestMemo << "s, ratio " << (bestMemo > 0 ? bestPlain / bestMemo : 0.0) << " (indicative; host may be loaded), mismatches " << mismatch << "\n";
}
}  // namespace

int main(int argc, char** argv) {
  try {
    bool bits = false, par = false, det = false, tim = false;
    std::uint32_t seed = 0xa527'8000u; int games = 2, moves = 30, depth = 4, strata = 5, threads = 4, reps = 2;
    for (int i = 1; i < argc; ++i) {
      const std::string k = argv[i];
      if (k == "--leaf-bits") bits = true; else if (k == "--parity") par = true; else if (k == "--determinism") det = true; else if (k == "--timing") tim = true;
      else { if (i + 1 >= argc) throw std::invalid_argument("missing value for " + k); const std::string v = argv[++i];
        if (k == "--seed-start") seed = static_cast<std::uint32_t>(std::stoul(v, nullptr, 0)); else if (k == "--games") games = std::stoi(v); else if (k == "--moves") moves = std::stoi(v);
        else if (k == "--depth") depth = std::stoi(v); else if (k == "--chance-samples") strata = std::stoi(v); else if (k == "--threads") threads = std::stoi(v); else if (k == "--reps") reps = std::stoi(v);
        else throw std::invalid_argument("unknown option " + k); }
    }
    bool ok = true, any = false;
    if (bits) { any = true; ok = leafBits(seed, games, moves, depth, strata) && ok; }
    if (par) { any = true; ok = parity(seed, games, moves, depth, strata) && ok; }
    if (det) { any = true; ok = determinism(seed, games, moves, depth, strata, threads) && ok; }
    if (tim) { any = true; timing(seed, games, moves, depth, strata, reps); }
    if (!any) throw std::invalid_argument("name at least one gate");
    std::cout << (ok ? "GATE PASS\n" : "GATE FAIL\n");
    return ok ? 0 : 1;
  } catch (const std::exception& e) { std::cerr << "gate failed: " << e.what() << "\n"; return 2; }
}
