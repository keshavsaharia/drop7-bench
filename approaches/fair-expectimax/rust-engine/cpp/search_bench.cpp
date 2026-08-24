// Search-throughput benchmark for the two C++ arms on the SAME harvested
// roots the Rust bench reads, so decisions/second are comparable:
//   baseline  ConfigurableSearch<false,false,false> (reference storage:
//             string-keyed LRU table, reference engine, reference leaf)
//   fast      ConfigurableSearch<true,true,true> (packed LRU table, fast
//             engine, fast leaf) -- the proven AllFastSearch
//
//   search-bench --roots F --depth D --strata S --decisions N --repeats R
//
// Additive file; modifies no existing repository source.

#include "approaches/lifetime-objective/fast-engine/variant-search.hpp"
#include "approaches/lifetime-objective/fast-engine/corpus.hpp"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace drop7;
using namespace drop7::fast;
using Clock = std::chrono::steady_clock;

State parseState(const std::string& line) {
  State state;
  state.score = 0;
  state.moves_played = 0;
  std::string board;
  std::istringstream in(line.substr(2));
  in >> board;
  for (int index = 0; index < kCellCount; ++index) {
    state.board[static_cast<std::size_t>(index)] =
        static_cast<std::uint8_t>(board[static_cast<std::size_t>(index)] - '0');
  }
  int next = 0;
  int mr = 0;
  int level = 0;
  int over = 0;
  in >> next >> mr >> level >> over;
  state.next_disc = static_cast<std::uint8_t>(next);
  state.moves_remaining = mr;
  state.level = level;
  state.game_over = over != 0;
  return state;
}

std::uint64_t workBoundFor(int depth, int strata) {
  const std::uint64_t branches =
      static_cast<std::uint64_t>(kBoardSize) * static_cast<std::uint64_t>(strata);
  std::uint64_t total = 0;
  for (int level = 1; level <= depth; ++level) {
    std::uint64_t power = 1;
    for (int step = 0; step < level; ++step) power *= branches;
    for (int inner = 1; inner <= level; ++inner) {
      std::uint64_t inner_power = 1;
      for (int step = 0; step < inner; ++step) inner_power *= branches;
      total += inner_power;
    }
    total += power;
  }
  return total;
}

template <typename Search>
void bench(const std::string& name, const std::vector<State>& roots, int depth,
           int strata, int decisions, int repeats) {
  FastSearchParameters parameters;
  parameters.depth = depth;
  parameters.chance_samples = strata;
  parameters.maximum_work = workBoundFor(depth, strata) + 1;
  parameters.maximum_cache_entries = 200'000;
  double best_seconds = 1e18;
  std::uint64_t best_work = 0;
  std::uint64_t best_nodes = 0;
  std::uint64_t best_hits = 0;
  std::size_t table_bytes = 0;
  for (int repeat = 0; repeat < repeats; ++repeat) {
    Search search{parameters};
    const auto start = Clock::now();
    std::uint64_t work = 0;
    std::uint64_t nodes = 0;
    std::uint64_t hits = 0;
    for (int index = 0; index < decisions; ++index) {
      FastSearchMetrics metrics;
      search.chooseAction(roots[static_cast<std::size_t>(index) % roots.size()],
                          metrics);
      work += metrics.work;
      nodes += metrics.nodes;
      hits += metrics.cache_hits;
    }
    const double seconds =
        std::chrono::duration<double>(Clock::now() - start).count();
    if (seconds < best_seconds) {
      best_seconds = seconds;
      best_work = work;
      best_nodes = nodes;
      best_hits = hits;
      table_bytes = search.tableBytes();
    }
  }
  std::cout << name << ": " << best_seconds << " s, "
            << (best_seconds * 1000.0 / decisions) << " ms/decision, "
            << (decisions / best_seconds) << " decisions/s, "
            << (best_work / decisions) << " work/decision, "
            << (best_nodes / decisions) << " nodes/decision, "
            << (best_hits / decisions) << " hits/decision, "
            << (best_seconds * 1e9 / best_work) << " ns/work, table "
            << table_bytes << " bytes" << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  std::string roots_path;
  int depth = 4;
  int strata = 7;
  int decisions = 24;
  int repeats = 3;
  for (int index = 1; index + 1 < argc; index += 2) {
    const std::string key = argv[index];
    const std::string value = argv[index + 1];
    if (key == "--roots") roots_path = value;
    else if (key == "--depth") depth = std::stoi(value);
    else if (key == "--strata") strata = std::stoi(value);
    else if (key == "--decisions") decisions = std::stoi(value);
    else if (key == "--repeats") repeats = std::stoi(value);
  }
  std::ifstream in(roots_path);
  std::vector<State> roots;
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("s ", 0) == 0) roots.push_back(parseState(line));
  }
  if (roots.empty()) {
    std::cerr << "no roots" << '\n';
    return 2;
  }
  std::cout << "roots " << roots.size() << ", depth " << depth << ", strata "
            << strata << ", decisions " << decisions << ", best of " << repeats
            << '\n';
  bench<BaselineSearch>("baseline (reference storage)", roots, depth, strata,
                        decisions, repeats);
  bench<AllFastSearch>("fast (packed LRU table)     ", roots, depth, strata,
                       decisions, repeats);
  std::cout << "peak-rss " << peakResidentBytes() << '\n';
  return 0;
}
