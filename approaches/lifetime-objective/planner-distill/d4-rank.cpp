// d4-rank - the fair depth-4 comparator's ranking of the SAME roots the fair
// planner labelled.
//
//   d4-rank --parity [--parity-games 3] [--parity-moves 40]
//   d4-rank --corpus runs/RID/corpus.bin --out runs/RID/d4-rank.bin
//           [--depth 4] [--chance-samples 5] [--max-work 3200000]
//           [--stride 1] [--limit N] [--threads 8]
//
// WHY
// ---
// `docs/benchmarks.md` requires a learned ranker to be compared against a named
// comparator on the same roots, not against chance.  The comparator here is the
// unmodified frozen fair depth-4 search, which is also the search the student is
// meant to improve.  Two questions need its root-value vector rather than only
// its column:
//
//   * how often does fair D4 already choose the planner's column?  That is the
//     floor a student has to clear before it is worth anything; and
//   * how well does fair D4 *rank* the planner's siblings pairwise?  A student
//     that ranks worse than the search it is being inserted into cannot help it.
//
// The parity gate is the same one `risk-calibration` publishes: at default
// parameters this driver must select exactly the reference column on every move
// of every probe game.  Roots are read from the corpus and are public states, so
// nothing here opens a seed.

#include "fair-search.hpp"

#include "corpus.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace drop7;
using namespace drop7::distill;
using drop7::distill::RootRecord;

#pragma pack(push, 1)
struct RankRecord {
  std::uint32_t row;             // index into the corpus file
  std::int8_t action;            // the comparator's chosen column
  std::uint8_t completed_depth;
  std::uint8_t legal_mask;
  std::uint8_t padding;
  float value[7];                // play orientation; -1e30 where illegal
  std::uint64_t work;
};
#pragma pack(pop)
static_assert(sizeof(RankRecord) == 44, "RankRecord layout");

struct Options {
  std::string corpus;
  std::string out;
  int depth = 4;
  int chance_samples = 5;
  std::uint64_t max_work = 3'200'000;
  int stride = 1;
  int limit = 0;
  std::string rows_file;   // explicit row list, one index per line
  int threads = 8;
  bool parity = false;
  int parity_games = 3;
  int parity_moves = 40;
};

State stateOf(const RootRecord& record) {
  State state;
  for (int index = 0; index < kCells; ++index) {
    state.board[static_cast<std::size_t>(index)] = record.board[index];
  }
  state.next_disc = record.next_disc;
  state.score = 0;
  state.level = 1;
  state.moves_remaining = record.moves_remaining;
  state.moves_played = 0;
  state.game_over = false;
  return state;
}

// CHECK-tier gate.  `cfpi::detail::scenarioSeedForState` and `dynamicStateKey`
// depend only on the board, the visible disc and the moves until the rise, so a
// state rebuilt from a corpus row decides identically to the state it came from;
// this gate proves the driver itself is the reference.
int runParity(const Options& options) {
  namespace refns = drop7::fair_only_depth4;
  SearchParameters parameters;
  ParameterizedSearch mine{parameters};
  std::uint64_t mismatches = 0;
  std::uint64_t compared = 0;
  for (int game = 0; game < options.parity_games; ++game) {
    const std::uint32_t seed = 0xa51d'1000u + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < options.parity_moves) {
      const refns::SearchDecision reference = refns::chooseDepth4Action(state);
      std::uint64_t work = 0;
      const int candidate = mine.chooseAction(state, work);
      ++compared;
      if (candidate != reference.action) {
        ++mismatches;
        std::printf("  mismatch seed 0x%08x move %d: reference %d mine %d\n",
                    seed, state.moves_played, reference.action, candidate);
      }
      MoveResult move;
      if (!playHeadlessMove(state, seed, reference.action, move)) break;
    }
  }
  std::printf("parity: %llu moves compared, %llu mismatches\n",
              static_cast<unsigned long long>(compared),
              static_cast<unsigned long long>(mismatches));
  std::printf("%s\n", mismatches == 0 ? "PARITY OK" : "PARITY FAILED");
  return mismatches == 0 ? 0 : 1;
}

int runRank(const Options& options) {
  std::ifstream file(options.corpus, std::ios::binary | std::ios::ate);
  if (!file) {
    std::cerr << "cannot open " << options.corpus << "\n";
    return 1;
  }
  const std::streamsize bytes = file.tellg();
  if (bytes % static_cast<std::streamsize>(sizeof(RootRecord)) != 0) {
    std::cerr << "corpus size is not a multiple of the record size\n";
    return 1;
  }
  const std::size_t rows =
      static_cast<std::size_t>(bytes) / sizeof(RootRecord);
  std::vector<RootRecord> records(rows);
  file.seekg(0);
  file.read(reinterpret_cast<char*>(records.data()), bytes);

  // An explicit row list is far cheaper than a stride when only the held-out
  // origins need a comparator: the gate is read on those roots and nowhere
  // else, and each decision costs a full depth-4 search.
  std::vector<std::uint32_t> wanted;
  if (!options.rows_file.empty()) {
    std::ifstream list(options.rows_file);
    if (!list) {
      std::cerr << "cannot open " << options.rows_file << "\n";
      return 1;
    }
    std::size_t row = 0;
    while (list >> row) {
      if (row < rows) wanted.push_back(static_cast<std::uint32_t>(row));
    }
  } else {
    for (std::size_t row = 0; row < rows; row += static_cast<std::size_t>(
                                                  std::max(1, options.stride))) {
      wanted.push_back(static_cast<std::uint32_t>(row));
      if (options.limit > 0 &&
          wanted.size() >= static_cast<std::size_t>(options.limit)) {
        break;
      }
    }
  }
  std::printf("corpus %zu rows, ranking %zu of them at depth %d, %d strata, "
              "work bound %llu\n",
              rows, wanted.size(), options.depth, options.chance_samples,
              static_cast<unsigned long long>(options.max_work));

  std::vector<RankRecord> out(wanted.size());
  std::atomic<std::size_t> next{0};
  std::atomic<std::size_t> done{0};
  const auto started = std::chrono::steady_clock::now();

  std::vector<std::thread> pool;
  const int threads = std::max(1, options.threads);
  for (int worker = 0; worker < threads; ++worker) {
    pool.emplace_back([&] {
      SearchParameters parameters;
      parameters.depth = options.depth;
      parameters.chanceSamples = options.chance_samples;
      parameters.maximumWork = options.max_work;
      ParameterizedSearch search{parameters};
      for (;;) {
        const std::size_t slot = next.fetch_add(1);
        if (slot >= wanted.size()) return;
        const std::uint32_t row = wanted[slot];
        const RootRecord& record = records[row];
        const State state = stateOf(record);
        const RootValues values = search.evaluateRoot(state);
        RankRecord rank{};
        rank.row = row;
        rank.action = static_cast<std::int8_t>(values.action);
        rank.completed_depth = static_cast<std::uint8_t>(values.completedDepth);
        rank.legal_mask = record.legal_mask;
        rank.work = values.work;
        for (int column = 0; column < kBoardSize; ++column) {
          const double value = values.value[static_cast<std::size_t>(column)];
          rank.value[column] =
              std::isfinite(value) ? static_cast<float>(value) : -1e30f;
        }
        out[slot] = rank;
        const std::size_t count = done.fetch_add(1) + 1;
        if (count % 200 == 0) {
          std::printf("  %zu / %zu\n", count, wanted.size());
          std::fflush(stdout);
        }
      }
    });
  }
  for (std::thread& thread : pool) thread.join();
  const double wall =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();

  std::size_t agree = 0;
  std::size_t shallow = 0;
  for (const RankRecord& rank : out) {
    const RootRecord& record = records[rank.row];
    if (rank.action == static_cast<std::int8_t>(record.chosen_column)) ++agree;
    if (rank.completed_depth < options.depth) ++shallow;
  }
  std::printf(
      "fair D4 chose the planner's column on %zu / %zu roots (%.4f)\n"
      "decisions that did not complete the requested depth: %zu\n"
      "%.1f s wall on %d threads, %.3f s per decision\n",
      agree, out.size(), out.empty() ? 0.0 : static_cast<double>(agree) / out.size(),
      shallow, wall, threads,
      out.empty() ? 0.0 : wall * threads / out.size());

  if (!options.out.empty()) {
    std::ofstream sink(options.out, std::ios::binary);
    if (!sink) {
      std::cerr << "cannot open " << options.out << "\n";
      return 1;
    }
    sink.write(reinterpret_cast<const char*>(out.data()),
               static_cast<std::streamsize>(out.size() * sizeof(RankRecord)));
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string flag = argv[index];
    if (flag == "--parity") {
      options.parity = true;
    } else if (flag == "--corpus" && index + 1 < argc) {
      options.corpus = argv[++index];
    } else if (flag == "--out" && index + 1 < argc) {
      options.out = argv[++index];
    } else if (flag == "--depth" && index + 1 < argc) {
      options.depth = std::atoi(argv[++index]);
    } else if (flag == "--chance-samples" && index + 1 < argc) {
      options.chance_samples = std::atoi(argv[++index]);
    } else if (flag == "--max-work" && index + 1 < argc) {
      options.max_work = std::strtoull(argv[++index], nullptr, 0);
    } else if (flag == "--rows" && index + 1 < argc) {
      options.rows_file = argv[++index];
    } else if (flag == "--stride" && index + 1 < argc) {
      options.stride = std::atoi(argv[++index]);
    } else if (flag == "--limit" && index + 1 < argc) {
      options.limit = std::atoi(argv[++index]);
    } else if (flag == "--threads" && index + 1 < argc) {
      options.threads = std::atoi(argv[++index]);
    } else if (flag == "--parity-games" && index + 1 < argc) {
      options.parity_games = std::atoi(argv[++index]);
    } else if (flag == "--parity-moves" && index + 1 < argc) {
      options.parity_moves = std::atoi(argv[++index]);
    } else {
      std::cerr << "unknown argument " << flag << "\n";
      return 2;
    }
  }
  if (options.parity) return runParity(options);
  if (options.corpus.empty()) {
    std::cerr << "need --corpus or --parity\n";
    return 2;
  }
  return runRank(options);
}
