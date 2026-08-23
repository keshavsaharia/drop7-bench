// Population evaluator: plays every individual of a population on the same
// ordered seed block and writes one JSON artifact with per-game rows.
//
// Common random numbers by construction: individual k and individual j play
// seed s from the same disc tape and the same reveal tape, so a paired
// difference between them shares every chance event up to their first
// divergent decision.  The frozen vector is supplied by the caller as an
// ordinary population member when a control arm is wanted; this program has no
// opinion about which line is the control.
//
// Work is bounded at the worst-case iterative bound so it can never bind, and
// every decision's completed depth is audited exactly as fast-engine/cohort.cpp
// does; an artifact with incompleteDecisions != 0 is void.

#include "weighted-search.hpp"
#include "../common/harness.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace drop7;
using drop7::lifetime::GameRecord;
using drop7::lifetime::runGame;

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

struct Individual {
  std::string name;
  fastw::LeafWeights weights;
  std::vector<GameRecord> games;
  std::atomic<std::uint64_t> decisions{0};
  std::atomic<std::uint64_t> incomplete{0};
  std::atomic<std::uint64_t> illegal{0};
  std::atomic<std::uint64_t> maximumWork{0};
};

struct Options {
  std::string population;
  std::string output;
  std::string label;
  std::uint32_t seedStart = 0;
  int games = 32;
  int maximumMoves = 2000;
  int threads = 30;
  int depth = 4;
  int strata = 7;
  std::size_t cache = 60'000;
  bool quiet = false;
};

Options parse(int argc, char** argv) {
  Options o;
  bool seedGiven = false;
  for (int i = 1; i < argc; ++i) {
    const std::string key = argv[i];
    if (key == "--quiet") { o.quiet = true; continue; }
    if (i + 1 >= argc) throw std::invalid_argument("missing value for " + key);
    const std::string value = argv[++i];
    if (key == "--population") o.population = value;
    else if (key == "--output") o.output = value;
    else if (key == "--label") o.label = value;
    else if (key == "--seed-start") { o.seedStart = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0)); seedGiven = true; }
    else if (key == "--games") o.games = std::stoi(value);
    else if (key == "--max-moves") o.maximumMoves = std::stoi(value);
    else if (key == "--threads") o.threads = std::stoi(value);
    else if (key == "--depth") o.depth = std::stoi(value);
    else if (key == "--chance-samples") o.strata = std::stoi(value);
    else if (key == "--cache") o.cache = std::stoull(value);
    else throw std::invalid_argument("unknown option " + key);
  }
  if (o.population.empty()) throw std::invalid_argument("--population is required");
  if (o.output.empty()) throw std::invalid_argument("--output is required");
  if (!seedGiven) throw std::invalid_argument("--seed-start is required");
  if (o.games < 1 || o.threads < 1) throw std::invalid_argument("bad games/threads");
  return o;
}

std::uint64_t fnv1a(std::uint64_t hash, std::uint64_t value) {
  for (int byte = 0; byte < 8; ++byte) {
    hash ^= (value >> (8 * byte)) & 0xffu;
    hash *= 1099511628211ull;
  }
  return hash;
}

std::uint64_t checksum(const GameRecord& r) {
  std::uint64_t h = 1469598103934665603ull;
  h = fnv1a(h, r.seed);
  h = fnv1a(h, static_cast<std::uint64_t>(r.score));
  h = fnv1a(h, static_cast<std::uint64_t>(r.moves));
  for (int a : r.actions) h = fnv1a(h, static_cast<std::uint64_t>(a));
  return h;
}

std::string jsonEscape(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options o = parse(argc, argv);
    const std::uint64_t bound = worstCaseWork(o.depth, o.strata);

    fastw::FastSearchParameters parameters;
    parameters.depth = o.depth;
    parameters.chance_samples = o.strata;
    parameters.maximum_work = bound + 1;
    parameters.maximum_cache_entries = o.cache;

    std::vector<std::unique_ptr<Individual>> population;
    {
      std::ifstream file(o.population);
      if (!file) throw std::runtime_error("cannot open " + o.population);
      std::string line;
      while (std::getline(file, line)) {
        std::string name;
        fastw::LeafWeights weights;
        if (!fastw::parsePopulationLine(line, name, weights)) continue;
        for (const auto& existing : population) {
          if (existing->name == name) throw std::runtime_error("duplicate individual " + name);
        }
        auto individual = std::make_unique<Individual>();
        individual->name = name;
        individual->weights = weights;
        individual->games.resize(static_cast<std::size_t>(o.games));
        population.push_back(std::move(individual));
      }
    }
    if (population.empty()) throw std::runtime_error("population is empty");

    std::cerr << "evaluate " << o.label << ": " << population.size()
              << " individuals x " << o.games << " games, seeds 0x" << std::hex
              << o.seedStart << "-0x" << (o.seedStart + static_cast<std::uint32_t>(o.games) - 1u)
              << std::dec << ", depth " << o.depth << ", strata " << o.strata
              << ", cache " << o.cache << ", max-work " << parameters.maximum_work
              << " (worst case " << bound << "), " << o.threads << " threads\n";

    const int tasks = static_cast<int>(population.size()) * o.games;
    std::atomic<int> next{0};
    std::atomic<int> finished{0};
    std::mutex logMutex;
    const auto started = std::chrono::steady_clock::now();
    const int threads = std::max(1, std::min(o.threads, tasks));
    std::vector<std::thread> pool;
    for (int worker = 0; worker < threads; ++worker) {
      pool.emplace_back([&]() {
        for (;;) {
          const int task = next.fetch_add(1);
          if (task >= tasks) return;
          // game-major order: all individuals on seed g before seed g+1, so a
          // partially finished artifact is still seed-aligned across the
          // population
          const int game = task / static_cast<int>(population.size());
          const int which = task % static_cast<int>(population.size());
          Individual& individual = *population[static_cast<std::size_t>(which)];
          const std::uint32_t seed = o.seedStart + static_cast<std::uint32_t>(game);
          fastw::WeightedFastSearch search{parameters, individual.weights};
          auto decide = [&](const State& state, std::uint64_t& work) {
            fastw::FastSearchMetrics metrics;
            const int action = search.chooseAction(state, metrics);
            work += metrics.work;
            individual.decisions.fetch_add(1, std::memory_order_relaxed);
            if (metrics.completed_depth != o.depth) {
              individual.incomplete.fetch_add(1, std::memory_order_relaxed);
            }
            if (action < 0 || !isLegal(state.board, action)) {
              individual.illegal.fetch_add(1, std::memory_order_relaxed);
            }
            std::uint64_t seen = individual.maximumWork.load();
            while (metrics.work > seen &&
                   !individual.maximumWork.compare_exchange_weak(seen, metrics.work)) {
            }
            return action;
          };
          individual.games[static_cast<std::size_t>(game)] =
              runGame(seed, decide, o.maximumMoves, true);
          const int done = finished.fetch_add(1) + 1;
          if (o.quiet) continue;
          const GameRecord& r = individual.games[static_cast<std::size_t>(game)];
          const std::lock_guard<std::mutex> lock(logMutex);
          std::cerr << "[" << done << "/" << tasks << "] " << individual.name
                    << " seed 0x" << std::hex << seed << std::dec << " score "
                    << r.score << " moves " << r.moves << " (" << std::fixed
                    << std::setprecision(1) << r.wallSeconds << "s)\n";
        }
      });
    }
    for (std::thread& t : pool) t.join();
    const double wall =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    std::uint64_t incompleteTotal = 0, illegalTotal = 0;
    std::ofstream out(o.output);
    if (!out) throw std::runtime_error("cannot open " + o.output);
    out << std::setprecision(12);
    out << "{\n  \"format\": \"drop7-leaf-evolution-population-v1\",\n";
    out << "  \"label\": \"" << jsonEscape(o.label) << "\",\n";
    out << "  \"engine\": \"fast-engine weighted leaf (approaches/lifetime-objective/leaf-evolution)\",\n";
    out << "  \"config\": {\"depth\": " << o.depth << ", \"chanceSamples\": " << o.strata
        << ", \"terminalUtility\": " << parameters.terminal_utility
        << ", \"maximumWork\": " << parameters.maximum_work
        << ", \"worstCaseWork\": " << bound
        << ", \"maximumCacheEntries\": " << o.cache
        << ", \"policySeedHex\": \"0x" << std::hex << parameters.policy_seed << std::dec << "\"},\n";
    out << "  \"seedStartHex\": \"0x" << std::hex << o.seedStart << std::dec << "\",\n";
    out << "  \"games\": " << o.games << ",\n";
    out << "  \"maximumMoves\": " << o.maximumMoves << ",\n";
    out << "  \"threads\": " << o.threads << ",\n";
    out << "  \"wallSeconds\": " << wall << ",\n";
    out << "  \"leafTerms\": [";
    for (int i = 0; i < fastw::kLeafTerms; ++i) out << (i ? ", " : "") << "\"" << fastw::kLeafNames[i] << "\"";
    out << "],\n";
    out << "  \"individuals\": [\n";
    for (std::size_t k = 0; k < population.size(); ++k) {
      const Individual& ind = *population[k];
      incompleteTotal += ind.incomplete.load();
      illegalTotal += ind.illegal.load();
      std::int64_t scoreTotal = 0;
      std::uint64_t moveTotal = 0, clearedTotal = 0, revealedTotal = 0, workTotal = 0;
      int censored = 0, identityFailures = 0;
      std::vector<double> scores, moves;
      for (const GameRecord& r : ind.games) {
        scoreTotal += r.score;
        moveTotal += static_cast<std::uint64_t>(r.moves);
        clearedTotal += r.numberedCleared;
        revealedTotal += r.coversRevealed;
        workTotal += r.work;
        if (r.censored) ++censored;
        if (r.levelPoints + r.clearPoints + r.chainPoints != r.score) ++identityFailures;
        scores.push_back(static_cast<double>(r.score));
        moves.push_back(static_cast<double>(r.moves));
      }
      const double n = static_cast<double>(ind.games.size());
      out << (k ? ",\n" : "") << "    {\"name\": \"" << jsonEscape(ind.name) << "\", \"frozen\": "
          << (ind.weights.isFrozen() ? "true" : "false") << ", \"weights\": [";
      for (int i = 0; i < fastw::kLeafTerms; ++i) out << (i ? ", " : "") << ind.weights.w[i];
      out << "],\n      \"decisions\": " << ind.decisions.load()
          << ", \"incompleteDecisions\": " << ind.incomplete.load()
          << ", \"illegalDecisions\": " << ind.illegal.load()
          << ", \"maximumWorkPerDecision\": " << ind.maximumWork.load()
          << ", \"scoreIdentityFailures\": " << identityFailures
          << ", \"censoredGames\": " << censored
          << ",\n      \"score\": {\"mean\": " << static_cast<double>(scoreTotal) / n
          << ", \"median\": " << drop7::lifetime::quantile(scores, 0.5)
          << ", \"q25\": " << drop7::lifetime::quantile(scores, 0.25)
          << ", \"min\": " << drop7::lifetime::quantile(scores, 0.0)
          << ", \"max\": " << drop7::lifetime::quantile(scores, 1.0) << "}"
          << ", \"moves\": {\"mean\": " << static_cast<double>(moveTotal) / n
          << ", \"q25\": " << drop7::lifetime::quantile(moves, 0.25) << "}"
          << ", \"numberedClearsPerMove\": " << (moveTotal ? static_cast<double>(clearedTotal) / static_cast<double>(moveTotal) : 0.0)
          << ", \"coverRevealsPerMove\": " << (moveTotal ? static_cast<double>(revealedTotal) / static_cast<double>(moveTotal) : 0.0)
          << ", \"workPerMove\": " << (moveTotal ? static_cast<double>(workTotal) / static_cast<double>(moveTotal) : 0.0)
          << ",\n      \"games\": [\n";
      for (std::size_t g = 0; g < ind.games.size(); ++g) {
        const GameRecord& r = ind.games[g];
        out << (g ? ",\n" : "") << "        {\"seedHex\": \"0x" << std::hex << r.seed << std::dec
            << "\", \"score\": " << r.score << ", \"moves\": " << r.moves
            << ", \"censored\": " << (r.censored ? "true" : "false")
            << ", \"rises\": " << r.rises << ", \"boardClears\": " << r.boardClears
            << ", \"chainPoints\": " << r.chainPoints
            << ", \"numberedCleared\": " << r.numberedCleared
            << ", \"coversRevealed\": " << r.coversRevealed
            << ", \"maxChainDepth\": " << r.maxChainDepth
            << ", \"meanOccupiedCells\": " << r.meanOccupancy
            << ", \"work\": " << r.work << ", \"wallSeconds\": " << r.wallSeconds
            << ", \"checksumHex\": \"" << std::hex << checksum(r) << std::dec << "\"}";
      }
      out << "\n      ]}";
    }
    out << "\n  ],\n  \"incompleteDecisionsTotal\": " << incompleteTotal
        << ",\n  \"illegalDecisionsTotal\": " << illegalTotal << "\n}\n";
    std::cerr << "evaluate " << o.label << ": done in " << wall << "s; incomplete "
              << incompleteTotal << ", illegal " << illegalTotal << "\n";
    if (incompleteTotal != 0) {
      std::cerr << "ARTIFACT VOID: incomplete decisions\n";
      return 1;
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "evaluate failed: " << error.what() << '\n';
    return 2;
  }
}
