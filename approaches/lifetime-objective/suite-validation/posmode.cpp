// Position-mode evaluation of the scenario suite, plus its CHECK gates.
//
// Usage
//   posmode --self-test
//   posmode --suite <suite.jsonl> --horizon 9 --tapes 16 --policies a,b,c \
//           --threads 8 --jsonl <out.jsonl>
//
// Output is one JSON object per (position, tape, policy).  Analysis lives in
// analyze.py so that the C++ side stays a measurement instrument.

#include "posmode.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <ostream>
#include <sstream>
#include <thread>

namespace {

using namespace drop7;
using namespace drop7::scenario;
using namespace drop7::suitevalidation;

struct Options {
  std::string suite;
  std::string output;
  int horizon = 9;
  int tapes = 16;
  int threads = 8;
  int limit = 0;
  std::uint32_t tape_offset = 0x0100u;
  std::vector<std::string> policies;
  bool self_test = false;
};

// A position stripped of its randomness: exactly what position mode holds fixed.
struct Position {
  Scenario base;            // the minted record, kept for its start position
  std::string suite_id;     // the minted id, the position's stable name
  std::string origin;
  int occupied = 0;
  int covered = 0;
  std::vector<Scenario> tapes;  // K independent futures for this position
};

std::string jsonField(const std::string& line, const std::string& key) {
  std::string value;
  if (!io_detail::parseString(line, key, value)) return "";
  return value;
}

// ---------------------------------------------------------------------------
// CHECK gates
// ---------------------------------------------------------------------------

// Gate 1: at (depth 4, 5 strata, work 3,200,000) the parameterized driver must
// select exactly the column the frozen reference selects.  This is
// `risk::parityCheck`, run on this work's own lease rather than on the seeds
// finding-05 used.
bool gateComparatorParity() {
  std::cout << "gate 1: comparator parity (d4s5 == frozen chooseDepth4Action)\n";
  return risk::parityCheck(leaseSeed(0x0000u), 3, 40, std::cout);
}

// Gate 2 is `build/scenario/scenario-parity`, not reimplemented here.  It proves
// `ScenarioEngine<StreamRevealSource>` trajectory-identical to
// `drop7::playHeadlessMove` over 8,192 game-plays and 218,470 moves, which is
// the reason a scenario score means anything at all.  `reproduce` runs it.

// Gate 3: the information boundary.  Position mode holds the start position and
// the visible next disc fixed and redraws everything else.  A policy that reads
// only public state must therefore choose the SAME first column on every one of
// the K tapes.  A policy that leaked the tape or the latent board could not.
bool gateInformationBoundary(const std::vector<Position>& positions) {
  std::cout << "gate 3: information boundary (first column invariant across tapes)\n";
  const std::vector<std::string> names{"d2s5", "d2s7", "d3s5", "d3s7", "d4s5",
                                       "d4s7", "center-first", "lowest-column"};
  std::uint64_t checked = 0;
  std::uint64_t violations = 0;
  for (const std::string& name : names) {
    Policy policy(*findPolicy(name));
    for (std::size_t index = 0; index < positions.size() && index < 24; ++index) {
      const Position& position = positions[index];
      int expected = -2;
      for (const Scenario& tape : position.tapes) {
        Mulberry32 rng(1u);
        std::uint64_t work = 0;
        auto engine = makeScenarioEngine(tape);
        const int column = policy.chooseColumn(engine.state(), rng, work);
        if (expected == -2) expected = column;
        ++checked;
        if (column != expected) ++violations;
      }
    }
  }
  std::cout << "  " << checked << " decisions checked, " << violations
            << " violations\n";
  return violations == 0;
}

// Gate 4: determinism and thread-count independence of the whole evaluation.
// Checked in `main` by comparing a 1-thread and an N-thread run of a small
// cohort; this helper hashes a set of records.
std::uint64_t hashRecords(const std::vector<std::string>& rows) {
  std::uint64_t hash = 1469598103934665603ull;
  for (const std::string& row : rows) {
    for (unsigned char byte : row) {
      hash ^= byte;
      hash *= 1099511628211ull;
    }
  }
  return hash;
}

// ---------------------------------------------------------------------------
// Loading and taping
// ---------------------------------------------------------------------------

bool loadPositions(const Options& options, std::vector<Position>& positions) {
  std::ifstream input(options.suite);
  if (!input) {
    std::cerr << "cannot read " << options.suite << "\n";
    return false;
  }
  std::string line;
  int accepted = 0;
  int rejected = 0;
  while (std::getline(input, line)) {
    if (line.empty()) continue;
    Scenario scenario;
    std::string reason;
    if (!deserializeScenario(line, scenario, reason)) {
      ++rejected;
      continue;
    }
    Position position;
    position.base = scenario;
    position.suite_id = scenario.id;
    position.origin = jsonField(line, "origin");
    if (position.origin.empty()) position.origin = "unknown";
    position.occupied = occupiedCells(scenario.board);
    position.covered = coveredCells(scenario.board);
    positions.push_back(std::move(position));
    ++accepted;
    if (options.limit > 0 && accepted >= options.limit) break;
  }
  if (rejected != 0) {
    std::cerr << "refusing to run: " << rejected
              << " suite lines failed self-verification\n";
    return false;
  }
  return !positions.empty();
}

// Tapes are drawn single-threaded and up front, exactly as `mint.cpp` builds its
// candidates, so seed consumption and every drawn future are identical
// regardless of the thread count used to play them.  One lease seed per tape
// index; the per-position stream is that seed spread by splitmix64 over the
// position index, so adding positions never perturbs an existing one.
void drawTapes(const Options& options, std::vector<Position>& positions) {
  for (std::size_t index = 0; index < positions.size(); ++index) {
    Position& position = positions[index];
    for (int tape = 0; tape < options.tapes; ++tape) {
      const std::uint32_t lease =
          leaseSeed(options.tape_offset + static_cast<std::uint32_t>(tape));
      Mulberry32 random(derivedSeed(lease, index));
      Scenario taped;
      if (!retapeAt(position.base, options.horizon, random, taped)) {
        std::cerr << "retape failed for position " << position.suite_id << "\n";
        std::exit(2);
      }
      std::string reason;
      if (!validateScenario(taped, reason)) {
        std::cerr << "retaped scenario invalid: " << reason << "\n";
        std::exit(2);
      }
      position.tapes.push_back(std::move(taped));
    }
  }
}

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------

// The order positions are *processed* in.  Positions are visited in an order
// determined only by the content hash of their id, so that a run stopped early
// leaves a prefix that is an unbiased subsample of the whole suite rather than
// "all the harvested ones".  The order is deterministic and independent of the
// thread count.
std::vector<std::size_t> processingOrder(const std::vector<Position>& positions) {
  std::vector<std::pair<std::uint64_t, std::size_t>> keyed;
  for (std::size_t index = 0; index < positions.size(); ++index) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char byte : positions[index].suite_id) {
      hash ^= byte;
      hash *= 1099511628211ull;
    }
    keyed.emplace_back(hash, index);
  }
  std::sort(keyed.begin(), keyed.end());
  std::vector<std::size_t> order;
  for (const auto& entry : keyed) order.push_back(entry.second);
  return order;
}

std::vector<std::string> evaluate(const Options& options,
                                  const std::vector<Position>& positions,
                                  const std::vector<PolicySpec>& specs,
                                  int threads, double& wall_seconds,
                                  std::ostream* stream = nullptr) {
  std::vector<std::vector<std::string>> rows(positions.size());
  const std::vector<std::size_t> order = processingOrder(positions);
  std::mutex stream_mutex;
  std::atomic<std::size_t> next{0};
  std::atomic<int> done{0};
  const auto started = std::chrono::steady_clock::now();

  const auto worker = [&]() {
    std::vector<Policy> pool;
    pool.reserve(specs.size());
    for (const PolicySpec& spec : specs) pool.emplace_back(spec);
    for (;;) {
      const std::size_t slot_index = next.fetch_add(1);
      if (slot_index >= positions.size()) return;
      const std::size_t index = order[slot_index];
      const Position& position = positions[index];
      for (std::size_t slot = 0; slot < specs.size(); ++slot) {
        for (int tape = 0; tape < options.tapes; ++tape) {
          // The policy's own randomization stream depends on the position, the
          // tape index and the policy name, never on the tape contents.
          Mulberry32 policy_rng(derivedSeed(
              leaseSeed(0x0080u),
              (static_cast<std::uint64_t>(index) << 20) ^
                  (static_cast<std::uint64_t>(tape) << 8) ^ slot));
          const PlayRecord record = playScenario(
              position.tapes[static_cast<std::size_t>(tape)], pool[slot],
              policy_rng);
          std::ostringstream out;
          out << "{\"position\":\"" << position.suite_id << "\""
              << ",\"origin\":\"" << position.origin << "\""
              << ",\"occupiedStart\":" << position.occupied
              << ",\"coveredStart\":" << position.covered
              << ",\"movesRemaining\":"
              << static_cast<int>(position.base.moves_remaining)
              << ",\"horizon\":" << options.horizon << ",\"tape\":" << tape
              << ",\"tapeId\":\""
              << position.tapes[static_cast<std::size_t>(tape)].id << "\""
              << ",\"policy\":\"" << specs[slot].name << "\""
              << ",\"points\":" << record.points
              << ",\"moves\":" << record.moves
              << ",\"clears\":" << record.clears
              << ",\"reveals\":" << record.reveals
              << ",\"rises\":" << record.rises
              << ",\"maxChainDepth\":" << record.max_chain_depth
              << ",\"died\":" << (record.died ? "true" : "false")
              << ",\"clearedBoard\":"
              << (record.cleared_board ? "true" : "false")
              << ",\"firstColumn\":" << record.first_column
              << ",\"occupiedEnd\":" << record.occupied_end
              << ",\"work\":" << record.work << "}";
          rows[index].push_back(out.str());
        }
      }
      const int completed = ++done;
      if (stream != nullptr) {
        // Stream each finished position immediately, so a run stopped by the
        // resource budget still yields a complete, analysable subsample.
        std::lock_guard<std::mutex> guard(stream_mutex);
        for (const std::string& row : rows[index]) *stream << row << "\n";
        stream->flush();
      }
      if (completed % 4 == 0) {
        std::printf("  %d/%zu positions evaluated (%.0f s)\n", completed,
                    positions.size(),
                    std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - started)
                        .count());
        std::fflush(stdout);
      }
    }
  };

  std::vector<std::thread> workers;
  for (int slot = 0; slot < std::max(1, threads); ++slot) {
    workers.emplace_back(worker);
  }
  for (std::thread& thread : workers) thread.join();
  wall_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();

  std::vector<std::string> flat;
  for (std::size_t slot_index = 0; slot_index < order.size(); ++slot_index) {
    for (const std::string& row : rows[order[slot_index]]) flat.push_back(row);
  }
  return flat;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string flag = argv[index];
    const auto value = [&]() -> std::string {
      if (index + 1 >= argc) {
        std::cerr << "missing value for " << flag << "\n";
        std::exit(2);
      }
      return argv[++index];
    };
    if (flag == "--suite") {
      options.suite = value();
    } else if (flag == "--jsonl") {
      options.output = value();
    } else if (flag == "--horizon") {
      options.horizon = std::stoi(value());
    } else if (flag == "--tapes") {
      options.tapes = std::stoi(value());
    } else if (flag == "--threads") {
      options.threads = std::stoi(value());
    } else if (flag == "--limit") {
      options.limit = std::stoi(value());
    } else if (flag == "--tape-offset") {
      options.tape_offset =
          static_cast<std::uint32_t>(std::stoul(value(), nullptr, 0));
    } else if (flag == "--policies") {
      std::stringstream stream(value());
      std::string name;
      while (std::getline(stream, name, ',')) options.policies.push_back(name);
    } else if (flag == "--self-test") {
      options.self_test = true;
    } else {
      std::cerr << "unknown option " << flag << "\n";
      return 2;
    }
  }
  if (options.threads > 8) {
    std::cerr << "refusing to use more than 8 threads (shared machine)\n";
    return 2;
  }
  if (options.policies.empty()) {
    for (const PolicySpec& spec : knownPolicies()) {
      options.policies.push_back(spec.name);
    }
  }
  std::vector<PolicySpec> specs;
  for (const std::string& name : options.policies) {
    const PolicySpec* spec = findPolicy(name);
    if (spec == nullptr) {
      std::cerr << "unknown policy " << name << "\n";
      return 2;
    }
    specs.push_back(*spec);
  }

  if (options.self_test) {
    bool ok = gateComparatorParity();
    if (!options.suite.empty()) {
      Options probe = options;
      probe.limit = 24;
      probe.tapes = std::min(options.tapes, 4);
      std::vector<Position> positions;
      if (!loadPositions(probe, positions)) return 2;
      drawTapes(probe, positions);
      ok = gateInformationBoundary(positions) && ok;

      std::cout << "gate 4: determinism and thread-count independence\n";
      std::vector<PolicySpec> small{*findPolicy("d2s5"), *findPolicy("d3s5"),
                                    *findPolicy("random-legal")};
      double wall = 0.0;
      Options tiny = probe;
      tiny.tapes = probe.tapes;
      const auto one = evaluate(tiny, positions, small, 1, wall);
      const auto many = evaluate(tiny, positions, small, 8, wall);
      const bool identical = one == many;
      std::cout << "  1-thread hash " << std::hex << hashRecords(one)
                << ", 8-thread hash " << hashRecords(many) << std::dec << ", "
                << (identical ? "identical" : "DIFFERENT") << "\n";
      ok = identical && ok;
    }
    std::cout << (ok ? "SELF-TEST OK\n" : "SELF-TEST FAILED\n");
    return ok ? 0 : 1;
  }

  if (options.suite.empty()) {
    std::cerr << "--suite <suite.jsonl> is required\n";
    return 2;
  }
  std::vector<Position> positions;
  if (!loadPositions(options, positions)) return 2;
  drawTapes(options, positions);
  std::printf(
      "position mode: %zu positions x %d tapes x %zu policies, horizon %d\n",
      positions.size(), options.tapes, specs.size(), options.horizon);
  std::printf(
      "seed lease SEEDLEASE-A52-SUITE, tape offsets 0x%x..0x%x of 0x%08x\n",
      options.tape_offset,
      options.tape_offset + static_cast<std::uint32_t>(options.tapes) - 1,
      kLeaseStart);

  double wall = 0.0;
  std::ofstream output;
  if (!options.output.empty()) {
    output.open(options.output);
    if (!output) {
      std::cerr << "cannot write " << options.output << "\n";
      return 2;
    }
  }
  const auto rows = evaluate(options, positions, specs, options.threads, wall,
                             options.output.empty() ? nullptr : &output);
  std::printf("evaluated %zu rows in %.1f s on %d threads\n", rows.size(), wall,
              options.threads);
  if (!options.output.empty()) std::printf("wrote %s\n", options.output.c_str());
  return 0;
}
