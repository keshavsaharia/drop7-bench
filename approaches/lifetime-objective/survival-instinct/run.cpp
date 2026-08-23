// Paired cohort runner: the unchanged fast fair-D4 search ("none") against the
// same search with the survival-instinct root filter ("strict", "literal"),
// every arm on the same ordered seeds.  Writes the leaf-evolution population
// artifact format so compare.py applies unchanged, plus per-arm coverage:
//
//   triggeredDecisions  roots where the filter refused at least one legal column
//   overrides           roots where the unfiltered search's column was refused
//                       (measured by running the unfiltered search at those roots)
//   noColumnFallbacks   roots where the filter refused every legal column and
//                       the unfiltered search was used instead
//
// Work is bounded at the worst-case iterative bound; completed depth is
// audited for every decision; an artifact with incomplete decisions is void.

#include "filtered-search.hpp"
#include "filter.hpp"
#include "../common/harness.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace drop7;
using drop7::lifetime::GameRecord;
using drop7::lifetime::runGame;

std::uint64_t worstCaseWork(int depth, int strata) {
  const auto b = static_cast<std::uint64_t>(kBoardSize) * static_cast<std::uint64_t>(strata);
  std::uint64_t total = 0;
  for (int d = 1; d <= depth; ++d) { std::uint64_t p = 1; for (int l = 1; l <= d; ++l) { p *= b; total += p; } total += p; }
  return total;
}

struct Arm {
  survival::Filter filter;
  std::string name;
  std::vector<GameRecord> games;
  std::atomic<std::uint64_t> decisions{0}, incomplete{0}, illegal{0}, triggered{0}, overrides{0}, fallbacks{0}, extraWork{0}, maximumWork{0};
};

std::uint64_t fnv(std::uint64_t h, std::uint64_t v) { for (int i = 0; i < 8; ++i) { h ^= (v >> (8 * i)) & 0xffu; h *= 1099511628211ull; } return h; }
std::uint64_t checksum(const GameRecord& r) { std::uint64_t h = 1469598103934665603ull; h = fnv(h, r.seed); h = fnv(h, static_cast<std::uint64_t>(r.score)); h = fnv(h, static_cast<std::uint64_t>(r.moves)); for (int a : r.actions) h = fnv(h, static_cast<std::uint64_t>(a)); return h; }
}  // namespace

int main(int argc, char** argv) {
  try {
    std::vector<std::string> filters; std::string output, label; std::uint32_t seedStart = 0; bool seedGiven = false;
    int games = 64, maxMoves = 2000, threads = 30, depth = 4, strata = 5; std::size_t cache = 60'000; bool quiet = false;
    for (int i = 1; i < argc; ++i) {
      const std::string k = argv[i];
      if (k == "--quiet") { quiet = true; continue; }
      if (i + 1 >= argc) throw std::invalid_argument("missing value for " + k);
      const std::string v = argv[++i];
      if (k == "--filter") filters.push_back(v); else if (k == "--output") output = v; else if (k == "--label") label = v;
      else if (k == "--seed-start") { seedStart = static_cast<std::uint32_t>(std::stoul(v, nullptr, 0)); seedGiven = true; }
      else if (k == "--games") games = std::stoi(v); else if (k == "--max-moves") maxMoves = std::stoi(v); else if (k == "--threads") threads = std::stoi(v);
      else if (k == "--depth") depth = std::stoi(v); else if (k == "--chance-samples") strata = std::stoi(v); else if (k == "--cache") cache = std::stoull(v);
      else throw std::invalid_argument("unknown option " + k);
    }
    if (filters.empty() || output.empty() || !seedGiven) throw std::invalid_argument("--filter (repeatable), --output and --seed-start are required");
    fastf::FastSearchParameters parameters; parameters.depth = depth; parameters.chance_samples = strata; parameters.maximum_work = worstCaseWork(depth, strata) + 1; parameters.maximum_cache_entries = cache;
    std::vector<std::unique_ptr<Arm>> arms;
    for (const auto& f : filters) { auto a = std::make_unique<Arm>(); a->filter = survival::parseFilter(f); a->name = f; a->games.resize(static_cast<std::size_t>(games)); arms.push_back(std::move(a)); }
    std::cerr << "survival-instinct " << label << ": arms"; for (const auto& a : arms) std::cerr << " " << a->name; std::cerr << " x " << games << " games, seeds 0x" << std::hex << seedStart << std::dec << "+, depth " << depth << ", strata " << strata << ", " << threads << " threads\n";

    const int tasks = static_cast<int>(arms.size()) * games;
    std::atomic<int> next{0}, finished{0}; std::mutex logMutex;
    const auto started = std::chrono::steady_clock::now();
    std::vector<std::thread> pool;
    for (int w = 0; w < std::max(1, std::min(threads, tasks)); ++w) pool.emplace_back([&]() {
      for (;;) {
        const int task = next.fetch_add(1); if (task >= tasks) return;
        const int game = task / static_cast<int>(arms.size()); const int which = task % static_cast<int>(arms.size());
        Arm& arm = *arms[static_cast<std::size_t>(which)];
        const std::uint32_t seed = seedStart + static_cast<std::uint32_t>(game);
        fastf::FilteredFastSearch search{parameters};
        fastf::FilteredFastSearch unfiltered{parameters};
        std::array<bool, kBoardSize> all; all.fill(true);
        auto decide = [&](const State& s, std::uint64_t& work) {
          const auto m = survival::rootMask(s, arm.filter);
          const bool useMask = m.refused > 0 && m.refused < m.legal;
          if (m.refused > 0) arm.triggered.fetch_add(1, std::memory_order_relaxed);
          if (m.refused > 0 && m.refused >= m.legal) arm.fallbacks.fetch_add(1, std::memory_order_relaxed);
          search.setRootMask(useMask ? m.allowed : all);
          fastf::FastSearchMetrics metrics; const int action = search.chooseAction(s, metrics); work += metrics.work;
          arm.decisions.fetch_add(1, std::memory_order_relaxed);
          if (metrics.completed_depth != depth) arm.incomplete.fetch_add(1, std::memory_order_relaxed);
          if (action < 0 || !isLegal(s.board, action)) arm.illegal.fetch_add(1, std::memory_order_relaxed);
          std::uint64_t seen = arm.maximumWork.load(); while (metrics.work > seen && !arm.maximumWork.compare_exchange_weak(seen, metrics.work)) {}
          if (useMask) {  // coverage: would the unfiltered search have chosen a refused column?
            unfiltered.setRootMask(all); fastf::FastSearchMetrics um; const int ua = unfiltered.chooseAction(s, um);
            arm.extraWork.fetch_add(um.work, std::memory_order_relaxed);
            if (ua >= 0 && !m.allowed[static_cast<std::size_t>(ua)]) arm.overrides.fetch_add(1, std::memory_order_relaxed);
          }
          return action;
        };
        arm.games[static_cast<std::size_t>(game)] = runGame(seed, decide, maxMoves, true);
        const int done = finished.fetch_add(1) + 1;
        if (quiet) continue;
        const GameRecord& r = arm.games[static_cast<std::size_t>(game)];
        const std::lock_guard<std::mutex> lock(logMutex);
        std::cerr << "[" << done << "/" << tasks << "] " << arm.name << " seed 0x" << std::hex << seed << std::dec << " score " << r.score << " moves " << r.moves << " (" << std::fixed << std::setprecision(1) << r.wallSeconds << "s)\n";
      }
    });
    for (auto& t : pool) t.join();
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    std::ofstream out(output); if (!out) throw std::runtime_error("cannot open " + output);
    out << std::setprecision(12) << "{\n  \"format\": \"drop7-leaf-evolution-population-v1\",\n  \"label\": \"" << label << "\",\n  \"engine\": \"fast-engine fair leaf with survival-instinct root filter (approaches/lifetime-objective/survival-instinct)\",\n";
    out << "  \"config\": {\"depth\": " << depth << ", \"chanceSamples\": " << strata << ", \"terminalUtility\": " << parameters.terminal_utility << ", \"maximumWork\": " << parameters.maximum_work << ", \"worstCaseWork\": " << worstCaseWork(depth, strata) << ", \"maximumCacheEntries\": " << cache << ", \"policySeedHex\": \"0x" << std::hex << parameters.policy_seed << std::dec << "\"},\n";
    out << "  \"seedStartHex\": \"0x" << std::hex << seedStart << std::dec << "\",\n  \"games\": " << games << ",\n  \"maximumMoves\": " << maxMoves << ",\n  \"threads\": " << threads << ",\n  \"wallSeconds\": " << wall << ",\n  \"individuals\": [\n";
    std::uint64_t incompleteTotal = 0, illegalTotal = 0;
    for (std::size_t k = 0; k < arms.size(); ++k) {
      const Arm& a = *arms[k]; incompleteTotal += a.incomplete; illegalTotal += a.illegal;
      std::int64_t scoreTotal = 0; std::uint64_t moveTotal = 0, cleared = 0, revealed = 0, workTotal = 0; int censored = 0, identity = 0; std::vector<double> scores, moves;
      for (const auto& r : a.games) { scoreTotal += r.score; moveTotal += static_cast<std::uint64_t>(r.moves); cleared += r.numberedCleared; revealed += r.coversRevealed; workTotal += r.work; if (r.censored) ++censored; if (r.levelPoints + r.clearPoints + r.chainPoints != r.score) ++identity; scores.push_back(static_cast<double>(r.score)); moves.push_back(static_cast<double>(r.moves)); }
      const double n = static_cast<double>(a.games.size());
      out << (k ? ",\n" : "") << "    {\"name\": \"" << a.name << "\", \"frozen\": " << (a.filter == survival::Filter::kNone ? "true" : "false") << ", \"filter\": \"" << survival::filterName(a.filter) << "\",\n      \"decisions\": " << a.decisions << ", \"incompleteDecisions\": " << a.incomplete << ", \"illegalDecisions\": " << a.illegal
          << ", \"triggeredDecisions\": " << a.triggered << ", \"overrides\": " << a.overrides << ", \"noColumnFallbacks\": " << a.fallbacks << ", \"coverageWorkExtra\": " << a.extraWork
          << ", \"maximumWorkPerDecision\": " << a.maximumWork << ", \"scoreIdentityFailures\": " << identity << ", \"censoredGames\": " << censored
          << ",\n      \"score\": {\"mean\": " << static_cast<double>(scoreTotal) / n << ", \"median\": " << drop7::lifetime::quantile(scores, 0.5) << ", \"q25\": " << drop7::lifetime::quantile(scores, 0.25) << ", \"min\": " << drop7::lifetime::quantile(scores, 0.0) << ", \"max\": " << drop7::lifetime::quantile(scores, 1.0) << "}"
          << ", \"moves\": {\"mean\": " << static_cast<double>(moveTotal) / n << ", \"q25\": " << drop7::lifetime::quantile(moves, 0.25) << "}"
          << ", \"numberedClearsPerMove\": " << (moveTotal ? static_cast<double>(cleared) / static_cast<double>(moveTotal) : 0.0) << ", \"coverRevealsPerMove\": " << (moveTotal ? static_cast<double>(revealed) / static_cast<double>(moveTotal) : 0.0) << ", \"workPerMove\": " << (moveTotal ? static_cast<double>(workTotal) / static_cast<double>(moveTotal) : 0.0) << ",\n      \"games\": [\n";
      for (std::size_t g = 0; g < a.games.size(); ++g) { const auto& r = a.games[g];
        out << (g ? ",\n" : "") << "        {\"seedHex\": \"0x" << std::hex << r.seed << std::dec << "\", \"score\": " << r.score << ", \"moves\": " << r.moves << ", \"censored\": " << (r.censored ? "true" : "false") << ", \"rises\": " << r.rises << ", \"boardClears\": " << r.boardClears << ", \"chainPoints\": " << r.chainPoints << ", \"numberedCleared\": " << r.numberedCleared << ", \"coversRevealed\": " << r.coversRevealed << ", \"maxChainDepth\": " << r.maxChainDepth << ", \"meanOccupiedCells\": " << r.meanOccupancy << ", \"work\": " << r.work << ", \"wallSeconds\": " << r.wallSeconds << ", \"checksumHex\": \"" << std::hex << checksum(r) << std::dec << "\"}"; }
      out << "\n      ]}";
    }
    out << "\n  ],\n  \"incompleteDecisionsTotal\": " << incompleteTotal << ",\n  \"illegalDecisionsTotal\": " << illegalTotal << "\n}\n";
    std::cerr << "survival-instinct " << label << ": done in " << wall << "s";
    for (const auto& a : arms) std::cerr << " | " << a->name << " triggered " << a->triggered << "/" << a->decisions << " overrides " << a->overrides << " fallbacks " << a->fallbacks;
    std::cerr << "; incomplete " << incompleteTotal << ", illegal " << illegalTotal << "\n";
    if (incompleteTotal != 0) { std::cerr << "ARTIFACT VOID\n"; return 1; }
    return 0;
  } catch (const std::exception& e) { std::cerr << "run failed: " << e.what() << "\n"; return 2; }
}
