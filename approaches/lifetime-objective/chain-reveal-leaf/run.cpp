// Paired multi-arm cohort runner for the chain-reveal-leaf substrate.
//
// Every arm is the fast fair-D4 search with the one-entry leaf memo and a
// weight vector over the extra terms (extra-terms.hpp); the arm named
// "frozen" is the all-zero vector, which gate.cpp proves bit-identical to the
// unmodified search.  Every arm plays the same ordered seeds so the games are
// paired by seed.
//
//   --arm NAME[:name=value,...]   repeatable; NAME "frozen" must carry no
//                                 non-zero weight
//   --seed-lease ID               REQUIRED: the lease the seeds are read under;
//                                 written into every artifact (the shared
//                                 harness writer's fixed label is replaced)
//   --data-role ROLE              data role written into every artifact
//                                 (default public-development)
//   --shadow                      for each non-frozen arm, also run the
//                                 unmodified fast::FastSearch at every root and
//                                 count decisions whose column differs
//                                 (divergentDecisions): the coverage measure.
//                                 The shadow search never plays a move.
//
// Work is bounded at the worst-case iterative bound (+1) unless --max-work is
// given; completed depth is audited for every decision, and an artifact with
// incomplete decisions is void.
//
// Artifacts: <output> is an index (drop7-chain-reveal-leaf-arms-v1) with the
// per-arm weights, audits and shadow counts; next to it, one
// drop7-lifetime-cohort-v1 file per arm (<output-stem>.<arm>.json, written by
// common/harness.hpp writeArtifact) that fast-engine/analyze.py reads
// directly.  Everything is rewritten after every --chunk completed games, so
// an interrupted run leaves a readable partial file marked "complete": false.

#include "augmented-search.hpp"
#include "fast-search.hpp"
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
using drop7::lifetime::CohortOptions;
using drop7::lifetime::GameRecord;
using drop7::lifetime::runGame;

std::uint64_t worstCaseWork(int depth, int strata) {
  const auto b = static_cast<std::uint64_t>(kBoardSize) * static_cast<std::uint64_t>(strata);
  std::uint64_t total = 0;
  for (int d = 1; d <= depth; ++d) { std::uint64_t p = 1; for (int l = 1; l <= d; ++l) { p *= b; total += p; } total += p; }
  return total;
}

struct GameAudit {
  bool done = false;
  std::uint64_t decisions = 0, divergent = 0, shadowWork = 0, memoCalls = 0, memoHits = 0;
};

struct Arm {
  std::string name;
  std::string spec;  // the --arm argument verbatim
  fastx::ExtraWeights weights;
  bool frozen = false;
  std::vector<GameRecord> games;
  std::vector<GameAudit> audits;
  std::atomic<std::uint64_t> decisions{0}, incomplete{0}, illegal{0}, shadowDecisions{0}, divergent{0},
      shadowWork{0}, memoCalls{0}, memoHits{0}, maximumWork{0};
};

std::uint64_t fnv(std::uint64_t h, std::uint64_t v) { for (int i = 0; i < 8; ++i) { h ^= (v >> (8 * i)) & 0xffu; h *= 1099511628211ull; } return h; }
std::uint64_t checksum(const GameRecord& r) { std::uint64_t h = 1469598103934665603ull; h = fnv(h, r.seed); h = fnv(h, static_cast<std::uint64_t>(r.score)); h = fnv(h, static_cast<std::uint64_t>(r.moves)); for (int a : r.actions) h = fnv(h, static_cast<std::uint64_t>(a)); return h; }

std::string armFile(const std::string& output, const std::string& arm) {
  const std::string stem = output.size() > 5 && output.compare(output.size() - 5, 5, ".json") == 0 ? output.substr(0, output.size() - 5) : output;
  std::string safe;
  for (char ch : arm) safe += (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_') ? ch : '_';
  return stem + "." + safe + ".json";
}

// common/harness.hpp::writeArtifact is shared and frozen by other experiments;
// it labels every cohort "SEEDLEASE-A51D" / "exploratory-development-
// diagnostic".  Replace exactly those two lines with the run's own labels and
// refuse if either line is not where the writer puts it.
std::string relabel(std::string text, const std::string& seedLease, const std::string& dataRole) {
  const std::string leaseLine = "  \"seedLease\": \"SEEDLEASE-A51D\",\n";
  const std::string roleLine = "  \"dataRole\": \"exploratory-development-diagnostic\",\n";
  const auto l = text.find(leaseLine), r = text.find(roleLine);
  if (l == std::string::npos || r == std::string::npos || text.find(leaseLine, l + 1) != std::string::npos || text.find(roleLine, r + 1) != std::string::npos) {
    throw std::runtime_error("harness writeArtifact output changed; cannot relabel seedLease/dataRole");
  }
  text.replace(r, roleLine.size(), "  \"dataRole\": \"" + dataRole + "\",\n");
  text.replace(l, leaseLine.size(), "  \"seedLease\": \"" + seedLease + "\",\n");
  return text;
}

std::string jsonEscape(const std::string& s) { std::string out; for (char ch : s) { if (ch == '"' || ch == '\\') out += '\\'; out += ch; } return out; }
}  // namespace

int main(int argc, char** argv) {
  try {
    std::vector<std::string> armSpecs; std::string output, label, seedLease, dataRole = "public-development"; std::uint32_t seedStart = 0; bool seedGiven = false, shadow = false, quiet = false;
    int games = 64, maxMoves = 2000, threads = 30, depth = 4, strata = 7, chunk = 0; std::size_t cache = 60'000; std::uint64_t maxWork = 0;
    for (int i = 1; i < argc; ++i) {
      const std::string k = argv[i];
      if (k == "--quiet") { quiet = true; continue; }
      if (k == "--shadow") { shadow = true; continue; }
      if (i + 1 >= argc) throw std::invalid_argument("missing value for " + k);
      const std::string v = argv[++i];
      if (k == "--arm") armSpecs.push_back(v); else if (k == "--output") output = v; else if (k == "--label") label = v;
      else if (k == "--seed-lease") seedLease = v; else if (k == "--data-role") dataRole = v;
      else if (k == "--seed-start") { seedStart = static_cast<std::uint32_t>(std::stoul(v, nullptr, 0)); seedGiven = true; }
      else if (k == "--games") games = std::stoi(v); else if (k == "--max-moves") maxMoves = std::stoi(v); else if (k == "--threads") threads = std::stoi(v);
      else if (k == "--depth") depth = std::stoi(v); else if (k == "--chance-samples") strata = std::stoi(v); else if (k == "--cache") cache = std::stoull(v);
      else if (k == "--max-work") maxWork = std::stoull(v); else if (k == "--chunk") chunk = std::stoi(v);
      else throw std::invalid_argument("unknown option " + k);
    }
    if (armSpecs.empty() || output.empty() || !seedGiven) throw std::invalid_argument("--arm (repeatable), --output and --seed-start are required");
    if (seedLease.empty()) throw std::invalid_argument("--seed-lease ID is required so no cohort can be mislabelled by omission");
    if (dataRole.empty() || dataRole.find('"') != std::string::npos || seedLease.find('"') != std::string::npos) throw std::invalid_argument("bad --seed-lease / --data-role");
    if (games < 1) throw std::invalid_argument("--games must be positive");
    fastx::FastSearchParameters parameters; parameters.depth = depth; parameters.chance_samples = strata;
    parameters.maximum_work = maxWork ? maxWork : worstCaseWork(depth, strata) + 1; parameters.maximum_cache_entries = cache;
    fast::FastSearchParameters shadowParameters; shadowParameters.depth = depth; shadowParameters.chance_samples = strata;
    shadowParameters.maximum_work = parameters.maximum_work; shadowParameters.maximum_cache_entries = cache;

    std::vector<std::unique_ptr<Arm>> arms;
    for (const auto& spec : armSpecs) {
      auto a = std::make_unique<Arm>();
      const auto colon = spec.find(':');
      a->name = spec.substr(0, colon);
      a->spec = spec;
      a->weights = fastx::ExtraWeights::parse(colon == std::string::npos ? "" : spec.substr(colon + 1));
      a->frozen = a->weights.isFrozen();
      if (a->name.empty()) throw std::invalid_argument("empty arm name in " + spec);
      if (a->name == "frozen" && !a->frozen) throw std::invalid_argument("arm 'frozen' must carry no non-zero weight");
      for (const auto& other : arms) if (other->name == a->name) throw std::invalid_argument("duplicate arm name " + a->name);
      a->games.resize(static_cast<std::size_t>(games)); a->audits.resize(static_cast<std::size_t>(games));
      arms.push_back(std::move(a));
    }
    if (chunk < 1) chunk = static_cast<int>(arms.size());
    std::cerr << "chain-reveal-leaf " << label << ": arms"; for (const auto& a : arms) std::cerr << " " << a->name << "[" << a->weights.describe() << "]";
    std::cerr << " x " << games << " games, seeds 0x" << std::hex << seedStart << std::dec << "+, depth " << depth << ", strata " << strata << ", max work " << parameters.maximum_work << ", " << threads << " threads" << (shadow ? ", shadow" : "") << "\n";

    const auto started = std::chrono::steady_clock::now();
    std::mutex writeMutex;
    auto writeAll = [&](bool complete) {
      const std::lock_guard<std::mutex> lock(writeMutex);
      const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
      std::ostringstream armsJson;
      for (std::size_t k = 0; k < arms.size(); ++k) {
        const Arm& a = *arms[k];
        std::vector<GameRecord> done;
        for (std::size_t g = 0; g < a.games.size(); ++g) if (a.audits[g].done) done.push_back(a.games[g]);
        const std::string path = armFile(output, a.name);
        if (!done.empty()) {
          CohortOptions o; o.seedStart = seedStart; o.games = games; o.maximumMoves = maxMoves; o.threads = threads;
          std::ostringstream config;
          config << std::setprecision(12) << "{\"approach\": \"chain-reveal-leaf\", \"arm\": \"" << jsonEscape(a.name) << "\", \"extraWeights\": \"" << a.weights.describe() << "\", \"frozen\": " << (a.frozen ? "true" : "false")
                 << ", \"depth\": " << depth << ", \"chanceSamples\": " << strata << ", \"terminalUtility\": " << parameters.terminal_utility << ", \"maximumWork\": " << parameters.maximum_work
                 << ", \"worstCaseWork\": " << worstCaseWork(depth, strata) << ", \"maximumCacheEntries\": " << cache << ", \"policySeedHex\": \"0x" << std::hex << parameters.policy_seed << std::dec
                 << "\", \"completedGames\": " << done.size() << ", \"requestedGames\": " << games << ", \"complete\": " << (complete ? "true" : "false") << "}";
          const std::string tmp = path + ".tmp";
          std::ostringstream cohort;
          drop7::lifetime::writeArtifact(cohort, "fast fair-D4 + memo + extra terms [" + a.name + "]", config.str(), o, done, wall);
          { std::ofstream out(tmp); if (!out) throw std::runtime_error("cannot open " + tmp);
            out << relabel(cohort.str(), seedLease, dataRole); }
          if (std::rename(tmp.c_str(), path.c_str()) != 0) throw std::runtime_error("cannot rename " + tmp);
        }
        const std::uint64_t calls = a.memoCalls.load(), hits = a.memoHits.load();
        armsJson << (k ? ",\n" : "") << "    {\"name\": \"" << jsonEscape(a.name) << "\", \"weights\": \"" << a.weights.describe() << "\", \"armArg\": \"" << jsonEscape(a.spec) << "\", \"frozen\": " << (a.frozen ? "true" : "false")
                 << ", \"artifact\": \"" << jsonEscape(path) << "\", \"completedGames\": " << done.size()
                 << ",\n      \"decisions\": " << a.decisions << ", \"incompleteDecisions\": " << a.incomplete << ", \"illegalDecisions\": " << a.illegal
                 << ", \"shadowDecisions\": " << a.shadowDecisions << ", \"divergentDecisions\": " << a.divergent << ", \"shadowWork\": " << a.shadowWork
                 << ", \"memoCalls\": " << calls << ", \"memoHits\": " << hits << ", \"memoHitRate\": " << (calls ? static_cast<double>(hits) / static_cast<double>(calls) : 0.0)
                 << ", \"maximumWorkPerDecision\": " << a.maximumWork << ",\n      \"games\": [\n";
        bool first = true;
        for (std::size_t g = 0; g < a.games.size(); ++g) {
          if (!a.audits[g].done) continue;
          const GameRecord& r = a.games[g]; const GameAudit& u = a.audits[g];
          armsJson << (first ? "" : ",\n") << "        {\"seedHex\": \"0x" << std::hex << r.seed << std::dec << "\", \"score\": " << r.score << ", \"moves\": " << r.moves << ", \"censored\": " << (r.censored ? "true" : "false")
                   << ", \"decisions\": " << u.decisions << ", \"divergentDecisions\": " << u.divergent << ", \"memoCalls\": " << u.memoCalls << ", \"memoHits\": " << u.memoHits << ", \"work\": " << r.work
                   << ", \"wallSeconds\": " << r.wallSeconds << ", \"checksumHex\": \"" << std::hex << checksum(r) << std::dec << "\"}";
          first = false;
        }
        armsJson << "\n      ]}";
      }
      std::uint64_t incompleteTotal = 0, illegalTotal = 0;
      for (const auto& a : arms) { incompleteTotal += a->incomplete; illegalTotal += a->illegal; }
      const std::string tmp = output + ".tmp";
      { std::ofstream out(tmp); if (!out) throw std::runtime_error("cannot open " + tmp);
        out << std::setprecision(12) << "{\n  \"format\": \"drop7-chain-reveal-leaf-arms-v1\",\n  \"label\": \"" << jsonEscape(label) << "\",\n  \"engine\": \"fast-engine fair leaf + one-entry memo + weighted extra terms (approaches/lifetime-objective/chain-reveal-leaf)\",\n"
            << "  \"extraTerms\": ["; for (int i = 0; i < fastx::kExtraTerms; ++i) out << (i ? ", " : "") << "\"" << fastx::kExtraNames[i] << "\""; out << "],\n"
            << "  \"config\": {\"depth\": " << depth << ", \"chanceSamples\": " << strata << ", \"terminalUtility\": " << parameters.terminal_utility << ", \"maximumWork\": " << parameters.maximum_work << ", \"worstCaseWork\": " << worstCaseWork(depth, strata)
            << ", \"maximumCacheEntries\": " << cache << ", \"policySeedHex\": \"0x" << std::hex << parameters.policy_seed << std::dec << "\", \"shadow\": " << (shadow ? "true" : "false") << "},\n"
            << "  \"seedLease\": \"" << seedLease << "\",\n  \"dataRole\": \"" << dataRole << "\",\n"
            << "  \"seedStartHex\": \"0x" << std::hex << seedStart << std::dec << "\",\n  \"games\": " << games << ",\n  \"maximumMoves\": " << maxMoves << ",\n  \"threads\": " << threads
            << ",\n  \"complete\": " << (complete ? "true" : "false") << ",\n  \"wallSeconds\": " << wall << ",\n  \"incompleteDecisionsTotal\": " << incompleteTotal << ",\n  \"illegalDecisionsTotal\": " << illegalTotal
            << ",\n  \"arms\": [\n" << armsJson.str() << "\n  ]\n}\n"; }
      if (std::rename(tmp.c_str(), output.c_str()) != 0) throw std::runtime_error("cannot rename " + tmp);
    };

    const int tasks = static_cast<int>(arms.size()) * games;
    std::atomic<int> next{0}, finished{0}; std::mutex logMutex;
    std::vector<std::thread> pool;
    for (int w = 0; w < std::max(1, std::min(threads, tasks)); ++w) pool.emplace_back([&]() {
      for (;;) {
        const int task = next.fetch_add(1); if (task >= tasks) return;
        const int game = task / static_cast<int>(arms.size()); const int which = task % static_cast<int>(arms.size());
        Arm& arm = *arms[static_cast<std::size_t>(which)];
        const std::uint32_t seed = seedStart + static_cast<std::uint32_t>(game);
        fastx::AugmentedFastSearch search{parameters, arm.weights};
        std::unique_ptr<fast::FastSearch> shadowSearch;
        if (shadow && !arm.frozen) shadowSearch = std::make_unique<fast::FastSearch>(shadowParameters);
        GameAudit audit;
        auto decide = [&](const State& s, std::uint64_t& work) {
          fastx::FastSearchMetrics metrics; const int action = search.chooseAction(s, metrics); work += metrics.work;
          ++audit.decisions;
          if (metrics.completed_depth != depth) arm.incomplete.fetch_add(1, std::memory_order_relaxed);
          if (action < 0 || !isLegal(s.board, action)) arm.illegal.fetch_add(1, std::memory_order_relaxed);
          std::uint64_t seen = arm.maximumWork.load(); while (metrics.work > seen && !arm.maximumWork.compare_exchange_weak(seen, metrics.work)) {}
          if (shadowSearch) {
            fast::FastSearchMetrics sm; const int sa = shadowSearch->chooseAction(s, sm);
            audit.shadowWork += sm.work; if (sa != action) ++audit.divergent;
          }
          return action;
        };
        GameRecord record = runGame(seed, decide, maxMoves, true);
        audit.memoCalls = search.leafMemo().calls; audit.memoHits = search.leafMemo().hits; audit.done = true;
        arm.decisions.fetch_add(audit.decisions, std::memory_order_relaxed);
        if (shadowSearch) { arm.shadowDecisions.fetch_add(audit.decisions, std::memory_order_relaxed); arm.divergent.fetch_add(audit.divergent, std::memory_order_relaxed); arm.shadowWork.fetch_add(audit.shadowWork, std::memory_order_relaxed); }
        arm.memoCalls.fetch_add(audit.memoCalls, std::memory_order_relaxed); arm.memoHits.fetch_add(audit.memoHits, std::memory_order_relaxed);
        { const std::lock_guard<std::mutex> lock(writeMutex); arm.games[static_cast<std::size_t>(game)] = std::move(record); arm.audits[static_cast<std::size_t>(game)] = audit; }
        const int done = finished.fetch_add(1) + 1;
        if (!quiet) {
          const std::lock_guard<std::mutex> lock(logMutex);
          const GameRecord& r = arm.games[static_cast<std::size_t>(game)];
          std::cerr << "[" << done << "/" << tasks << "] " << arm.name << " seed 0x" << std::hex << seed << std::dec << " score " << r.score << " moves " << r.moves;
          if (shadowSearch) std::cerr << " divergent " << audit.divergent << "/" << audit.decisions;
          std::cerr << " (" << std::fixed << std::setprecision(1) << r.wallSeconds << "s)\n";
        }
        if (done % chunk == 0 && done < tasks) writeAll(false);
      }
    });
    for (auto& t : pool) t.join();
    writeAll(true);
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::uint64_t incompleteTotal = 0, illegalTotal = 0;
    std::cerr << "chain-reveal-leaf " << label << ": done in " << wall << "s";
    for (const auto& a : arms) {
      incompleteTotal += a->incomplete; illegalTotal += a->illegal;
      std::cerr << " | " << a->name << " decisions " << a->decisions;
      if (a->shadowDecisions) std::cerr << " divergent " << a->divergent << "/" << a->shadowDecisions;
      std::cerr << " memo " << (a->memoCalls ? 100.0 * static_cast<double>(a->memoHits) / static_cast<double>(a->memoCalls) : 0.0) << "%";
    }
    std::cerr << "; incomplete " << incompleteTotal << ", illegal " << illegalTotal << "\n";
    if (incompleteTotal != 0) { std::cerr << "ARTIFACT VOID\n"; return 1; }
    return 0;
  } catch (const std::exception& e) { std::cerr << "run failed: " << e.what() << "\n"; return 2; }
}
