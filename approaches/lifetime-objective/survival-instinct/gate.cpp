// CHECK gates for the survival-instinct filtered search.  No gameplay
// evidence; probe seeds are previously opened development ranges.
//
//   --mask-units     hand-built boards with known answers for both filters
//   --parity         FilteredFastSearch with every column allowed selects the
//                    same column with the same work/nodes/cache/depth as
//                    FastSearch on every move of every probe game
//   --reflection     mask(mirror(state)) == mirror(mask(state)) and the
//                    filtered action mirrors, with identical work
//   --determinism    identical per-game results at 1 and N threads (strict)

#include "filtered-search.hpp"
#include "fast-search.hpp"
#include "filter.hpp"
#include "../common/harness.hpp"

#include <iostream>
#include <string>

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

State boardFrom(const std::string& cells, int next, int rise) {
  State s;
  for (int i = 0; i < kCellCount; ++i) s.board[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(cells[static_cast<std::size_t>(i)] - '0');
  s.next_disc = static_cast<std::uint8_t>(next); s.moves_remaining = rise; return s;
}

bool maskUnits() {
  int failures = 0;
  auto expect = [&](const char* name, const State& s, survival::Filter f, const std::string& allowed) {
    const auto m = survival::rootMask(s, f);
    std::string got;
    for (int c = 0; c < kBoardSize; ++c) got += m.allowed[static_cast<std::size_t>(c)] ? '1' : '0';
    const bool ok = got == allowed;
    if (!ok) ++failures;
    std::cout << "  " << name << " " << survival::filterName(f) << " expected " << allowed << " got " << got << (ok ? "" : "  MISMATCH") << "\n";
  };
  // Empty board: nothing is vertically dead.
  const State empty = boardFrom(std::string(49, '0'), 3, 5);
  expect("empty/3", empty, survival::Filter::kLiteral, "1111111");
  // Column 3 holds three discs (rows 4-6), next disc 3: it would land as the
  // 4th disc (vertically dead); landing row 3 is otherwise empty -> run 1.
  std::string b = std::string(49, '0');
  auto set = [&](int row, int col, char v) { b[static_cast<std::size_t>(row * kBoardSize + col)] = v; };
  set(4, 3, '7'); set(5, 3, '7'); set(6, 3, '7');
  expect("col3 h3, next 3, run 1", boardFrom(b, 3, 5), survival::Filter::kLiteral, "1110111");
  expect("col3 h3, next 3, run 1", boardFrom(b, 3, 5), survival::Filter::kStrict, "1111111");
  // Same board, next disc 4: lands as the 4th disc, 4 <= 4 -> allowed.
  expect("col3 h3, next 4", boardFrom(b, 4, 5), survival::Filter::kLiteral, "1111111");
  // Fill row 3 columns 0-2 so the landing row run becomes 4 (> 3): strict refuses.
  set(3, 0, '5'); set(3, 1, '5'); set(3, 2, '5');
  for (int r = 4; r < 7; ++r) for (int c = 0; c < 3; ++c) set(r, c, '6');  // keep columns packed
  // columns 0-2 (height 4) take the 3 as a fifth disc on an empty row: run 1 <= 3,
  // horizontal clearing remains possible, so strict allows them; column 3 lands
  // on row 3 beside three discs -> run 4 > 3, refused.
  expect("row run 4 over value 3", boardFrom(b, 3, 5), survival::Filter::kStrict, "1110111");
  expect("row run 4 over value 3", boardFrom(b, 3, 5), survival::Filter::kLiteral, "0000111");
  // Values 1 and 2 are exempt.
  expect("next 2 exempt", boardFrom(b, 2, 5), survival::Filter::kLiteral, "1111111");
  // A landing whose row run equals the value clears on arrival and is allowed
  // even though vertically dead: columns 0-2 have height 4, column 3 height 3;
  // dropping a 4 on column 3 lands at row 3 -> run through columns 0-3 = 4.
  expect("run equals value", boardFrom(b, 4, 5), survival::Filter::kLiteral, "0001111");
  std::cout << "mask-units: " << failures << " failures\n";
  return failures == 0;
}

bool parity(std::uint32_t seedStart, int games, int moves, int depth, int strata) {
  fast::FastSearch theirs{params<fast::FastSearchParameters>(depth, strata)};
  fastf::FilteredFastSearch mine{params<fastf::FastSearchParameters>(depth, strata)};
  std::uint64_t compared = 0, mismatch = 0;
  for (int g = 0; g < games; ++g) {
    const std::uint32_t seed = seedStart + static_cast<std::uint32_t>(g);
    State s = initialHeadlessState(seed);
    int played = 0;
    while (!s.game_over && played < moves) {
      fast::FastSearchMetrics a; fastf::FastSearchMetrics b;
      const int ca = theirs.chooseAction(s, a);
      const int cb = mine.chooseAction(s, b);
      ++compared;
      if (ca != cb || a.work != b.work || a.nodes != b.nodes || a.cache_hits != b.cache_hits || a.completed_depth != b.completed_depth) ++mismatch;
      int column = ca; if (column < 0 || !isLegal(s.board, column)) { column = centerFirstMove(s.board); if (column < 0) break; }
      MoveResult mv; if (!playHeadlessMove(s, seed, column, mv)) break; ++played;
    }
  }
  std::cout << "parity: " << compared << " moves compared, " << mismatch << " mismatches\n";
  return compared > 0 && mismatch == 0;
}

bool reflection(std::uint32_t seedStart, int games, int moves, int depth, int strata, survival::Filter filter) {
  fastf::FilteredFastSearch a{params<fastf::FastSearchParameters>(depth, strata)};
  fastf::FilteredFastSearch b{params<fastf::FastSearchParameters>(depth, strata)};
  std::uint64_t decisions = 0, maskMismatch = 0, actionMismatch = 0, workMismatch = 0, triggered = 0;
  for (int g = 0; g < games; ++g) {
    const std::uint32_t seed = seedStart + static_cast<std::uint32_t>(g);
    State s = initialHeadlessState(seed);
    int played = 0;
    while (!s.game_over && played < moves) {
      const auto m = survival::rootMask(s, filter);
      State mirrored = s; mirrored.board = cfpi::detail::mirrorBoard(s.board);
      const auto mm = survival::rootMask(mirrored, filter);
      for (int c = 0; c < kBoardSize; ++c) if (m.allowed[static_cast<std::size_t>(c)] != mm.allowed[static_cast<std::size_t>(kBoardSize - 1 - c)]) { ++maskMismatch; break; }
      const bool useMask = m.refused < m.legal;
      if (m.refused > 0) ++triggered;
      std::array<bool, kBoardSize> all; all.fill(true);
      a.setRootMask(useMask ? m.allowed : all);
      std::array<bool, kBoardSize> mirroredMask = all;
      if (useMask) for (int c = 0; c < kBoardSize; ++c) mirroredMask[static_cast<std::size_t>(c)] = m.allowed[static_cast<std::size_t>(kBoardSize - 1 - c)];
      b.setRootMask(mirroredMask);
      fastf::FastSearchMetrics ma, mb;
      const int ca = a.chooseAction(s, ma);
      const int cb = b.chooseAction(mirrored, mb);
      ++decisions;
      const bool symmetric = cfpi::detail::mirrorBoard(s.board) == s.board;
      if (symmetric ? (cb != ca) : (cb != kBoardSize - 1 - ca)) ++actionMismatch;
      if (ma.work != mb.work) ++workMismatch;
      int column = ca; if (column < 0 || !isLegal(s.board, column)) { column = centerFirstMove(s.board); if (column < 0) break; }
      MoveResult mv; if (!playHeadlessMove(s, seed, column, mv)) break; ++played;
    }
  }
  std::cout << "reflection(" << survival::filterName(filter) << "): " << decisions << " decisions, " << triggered << " with a refused column, "
            << maskMismatch << " mask, " << actionMismatch << " action, " << workMismatch << " work mismatches\n";
  return decisions > 0 && maskMismatch == 0 && actionMismatch == 0 && workMismatch == 0;
}

bool determinism(std::uint32_t seedStart, int games, int moves, int depth, int strata, int threads) {
  auto play = [&](int t) {
    CohortOptions o; o.seedStart = seedStart; o.games = games; o.maximumMoves = moves; o.threads = t; o.recordActions = true; o.quiet = true;
    return runCohort(o, [&]() {
      return [search = fastf::FilteredFastSearch{params<fastf::FastSearchParameters>(depth, strata)}](const State& s, std::uint64_t& work) mutable {
        const auto m = survival::rootMask(s, survival::Filter::kStrict);
        std::array<bool, kBoardSize> all; all.fill(true);
        search.setRootMask(m.refused < m.legal ? m.allowed : all);
        fastf::FastSearchMetrics metrics; const int a = search.chooseAction(s, metrics); work += metrics.work; return a;
      };
    });
  };
  const auto one = play(1), many = play(threads);
  std::uint64_t mismatch = 0;
  for (std::size_t i = 0; i < one.size(); ++i) if (one[i].score != many[i].score || one[i].moves != many[i].moves || one[i].work != many[i].work || one[i].actions != many[i].actions) ++mismatch;
  std::cout << "determinism: " << one.size() << " games at 1 and " << threads << " threads, " << mismatch << " mismatches\n";
  return !one.empty() && mismatch == 0;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    bool units = false, par = false, refl = false, det = false;
    std::uint32_t seed = 0xa527'8000u; int games = 2, moves = 30, depth = 4, strata = 5, threads = 4;
    for (int i = 1; i < argc; ++i) {
      const std::string k = argv[i];
      if (k == "--mask-units") units = true; else if (k == "--parity") par = true; else if (k == "--reflection") refl = true; else if (k == "--determinism") det = true;
      else { if (i + 1 >= argc) throw std::invalid_argument("missing value for " + k); const std::string v = argv[++i];
        if (k == "--seed-start") seed = static_cast<std::uint32_t>(std::stoul(v, nullptr, 0)); else if (k == "--games") games = std::stoi(v); else if (k == "--moves") moves = std::stoi(v);
        else if (k == "--depth") depth = std::stoi(v); else if (k == "--chance-samples") strata = std::stoi(v); else if (k == "--threads") threads = std::stoi(v); else throw std::invalid_argument("unknown option " + k); }
    }
    bool ok = true, any = false;
    if (units) { any = true; ok = maskUnits() && ok; }
    if (par) { any = true; ok = parity(seed, games, moves, depth, strata) && ok; }
    if (refl) { any = true; ok = reflection(seed, games, moves, depth, strata, survival::Filter::kStrict) && ok; ok = reflection(seed, games, moves, depth, strata, survival::Filter::kLiteral) && ok; }
    if (det) { any = true; ok = determinism(seed, games, moves, depth, strata, threads) && ok; }
    if (!any) throw std::invalid_argument("name at least one gate");
    std::cout << (ok ? "GATE PASS\n" : "GATE FAIL\n");
    return ok ? 0 : 1;
  } catch (const std::exception& e) { std::cerr << "gate failed: " << e.what() << "\n"; return 2; }
}
