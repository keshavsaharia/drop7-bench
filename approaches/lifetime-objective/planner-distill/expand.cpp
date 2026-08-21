// expand - extra chance realisations of every logged sibling afterstate.
//
//   expand --corpus runs/RID/corpus.bin --out runs/RID/after.bin
//          [--draws 8] [--sampler-seed 0xa526c000] [--threads 8]
//
// WHY THIS EXISTS
// ---------------
// `corpus-gen` stores ONE realised afterstate per legal column: the state the
// game actually enters, resolved against the true master tape.  The teacher's
// label for that column is instead a mean over K sampled completions of the
// hidden board.  Scoring a student as `immediate + f(one realised afterstate)`
// therefore compares an average against a single draw, and the mismatch is not
// symmetric - it can only make the student look worse than it is.
//
// That matters for the honesty of a negative result.  At deployment the student
// is a leaf inside a fair expectimax whose chance nodes average the leaf over
// five or seven stratified reveal outcomes, so the quantity that actually
// governs the search's ranking is `E_reveal[immediate + f(afterstate)]`, not one
// sample of it.  This program supplies the draws that make the offline gate
// measure the same quantity the search will.
//
// LEGALITY
// --------
// A covered cell's hidden value is i.i.d. uniform on 1..7 and independent of
// everything public, and so is the next visible disc, so redrawing them from
// that marginal is the exact conditional distribution given the public state.
// It is the same draw `fair-planner.hpp::sampleWindow` makes.  No master tape,
// no game seed and no hidden value from the original run is read: the input is
// the corpus's public board, visible disc and moves-until-rise, and nothing
// else.  The sampler stream comes from the high half of the seed lease.

#include "pinned/flow-ceiling/flow-common.hpp"
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
using namespace drop7::scenario;
using namespace drop7::flowceiling;
using drop7::distill::RootRecord;

constexpr std::uint32_t kSamplerSeedStart = 0xa526'8000u;
constexpr std::uint32_t kLeaseEnd = 0xa526'ffffu;

#pragma pack(push, 1)
struct AfterRecord {
  std::uint32_t row;             // index into the corpus file
  std::uint8_t column;
  std::uint8_t draw;             // 0 is the corpus's own true-tape realisation
  std::uint8_t survived;
  std::uint8_t clears;
  std::uint8_t reveals;
  std::uint8_t next_disc;
  std::uint8_t moves_remaining;
  std::uint8_t occupied;
  std::uint8_t board[drop7::distill::kCells];
  std::uint8_t padding[3];
};
#pragma pack(pop)
static_assert(sizeof(AfterRecord) == 64, "AfterRecord must stay 64 bytes");

struct Options {
  std::string corpus;
  std::string out;
  int draws = 8;
  std::uint32_t sampler_seed = 0xa526'c000u;
  int threads = 8;
};

State stateOf(const RootRecord& record) {
  State state;
  for (int index = 0; index < drop7::distill::kCells; ++index) {
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

// One independent realisation of the reveal randomness for one column.
bool drawAfterstate(const State& state, int column, Mulberry32& random,
                    AfterRecord& out) {
  LatentBoard latent{};
  latent.fill(0);
  for (int index = 0; index < kCellCount; ++index) {
    const std::uint8_t cell = state.board[static_cast<std::size_t>(index)];
    if (cell == kSolid || cell == kCracked) {
      latent[static_cast<std::size_t>(index)] = random.nextDisc();
    }
  }
  // Two discs of tape are enough: the one in hand is already in `state`, and
  // the reveal source only needs the *next* visible disc after this move.
  std::array<std::uint8_t, 2> tape{state.next_disc, random.nextDisc()};
  RiseRow rise{};
  for (int index = 0; index < kBoardSize; ++index) rise[index] = random.nextDisc();

  LatentRevealSource source;
  source.tape = tape.data();
  source.tape_length = static_cast<int>(tape.size());
  source.tape_index = 1;
  source.rise_rows = &rise;
  source.rise_count = 1;
  source.rise_index = 0;

  MoveResult result;
  LatentBoard next_latent{};
  if (!playScenarioMove(state, latent, column, source, result, next_latent)) {
    return false;
  }
  int cleared = 0;
  int revealed = 0;
  for (const Wave& wave : result.waves) {
    cleared += wave.cleared;
    revealed += wave.revealed;
  }
  out.survived = result.state.game_over ? 0 : 1;
  out.clears = static_cast<std::uint8_t>(std::min(cleared, 255));
  out.reveals = static_cast<std::uint8_t>(std::min(revealed, 255));
  out.next_disc = result.state.next_disc;
  out.moves_remaining = static_cast<std::uint8_t>(result.state.moves_remaining);
  out.occupied = static_cast<std::uint8_t>(occupiedCellCount(result.state.board));
  std::memcpy(out.board, result.state.board.data(), drop7::distill::kCells);
  return true;
}

int run(const Options& options) {
  if (options.sampler_seed < kSamplerSeedStart ||
      options.sampler_seed > kLeaseEnd) {
    std::cerr << "sampler seed outside the SEEDLEASE-A52-DISTILL sampler half\n";
    return 2;
  }
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
  const std::size_t rows = static_cast<std::size_t>(bytes) / sizeof(RootRecord);
  std::vector<RootRecord> records(rows);
  file.seekg(0);
  file.read(reinterpret_cast<char*>(records.data()), bytes);

  // Per-row output blocks, so the file is deterministic in corpus order no
  // matter how the work is scheduled.
  std::vector<std::vector<AfterRecord>> blocks(rows);
  std::atomic<std::size_t> next{0};
  const auto started = std::chrono::steady_clock::now();
  std::vector<std::thread> pool;
  const int threads = std::max(1, options.threads);
  std::atomic<std::size_t> failures{0};
  for (int worker = 0; worker < threads; ++worker) {
    pool.emplace_back([&] {
      for (;;) {
        const std::size_t row = next.fetch_add(1);
        if (row >= rows) return;
        const RootRecord& record = records[row];
        const State state = stateOf(record);
        std::vector<AfterRecord> block;
        for (int column = 0; column < kBoardSize; ++column) {
          if (((record.legal_mask >> column) & 1u) == 0) continue;
          // Draw 0 is the corpus's own true-tape realisation, copied over so a
          // consumer needs only this one file.
          AfterRecord zero{};
          zero.row = static_cast<std::uint32_t>(row);
          zero.column = static_cast<std::uint8_t>(column);
          zero.draw = 0;
          zero.survived = record.after_survived[column];
          zero.clears = record.after_clears[column];
          zero.reveals = record.after_reveals[column];
          zero.next_disc = record.after_next_disc[column];
          zero.moves_remaining = record.after_moves_remaining[column];
          zero.occupied = record.after_occupied[column];
          std::memcpy(zero.board, record.after_board[column],
                      drop7::distill::kCells);
          block.push_back(zero);
          // Domain separation: the stream is a function of the row and the
          // column, so a rerun reproduces the file exactly and two columns of
          // one root never share a draw.
          Mulberry32 random(options.sampler_seed ^
                            (static_cast<std::uint32_t>(row) * 0x9e37'79b9u) ^
                            (static_cast<std::uint32_t>(column) * 0x85eb'ca6bu));
          for (int draw = 1; draw < options.draws; ++draw) {
            AfterRecord entry{};
            entry.row = static_cast<std::uint32_t>(row);
            entry.column = static_cast<std::uint8_t>(column);
            entry.draw = static_cast<std::uint8_t>(draw);
            if (!drawAfterstate(state, column, random, entry)) {
              failures.fetch_add(1);
              continue;
            }
            block.push_back(entry);
          }
        }
        blocks[row] = std::move(block);
      }
    });
  }
  for (std::thread& thread : pool) thread.join();
  const double wall =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();

  std::ofstream sink(options.out, std::ios::binary);
  if (!sink) {
    std::cerr << "cannot open " << options.out << "\n";
    return 1;
  }
  std::size_t written = 0;
  for (const std::vector<AfterRecord>& block : blocks) {
    sink.write(reinterpret_cast<const char*>(block.data()),
               static_cast<std::streamsize>(block.size() * sizeof(AfterRecord)));
    written += block.size();
  }
  std::printf(
      "%zu roots -> %zu afterstates (%d draws each), %zu failed, %.1f s, "
      "%.1f MiB\n",
      rows, written, options.draws, failures.load(), wall,
      written * sizeof(AfterRecord) / 1048576.0);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string flag = argv[index];
    if (flag == "--corpus" && index + 1 < argc) {
      options.corpus = argv[++index];
    } else if (flag == "--out" && index + 1 < argc) {
      options.out = argv[++index];
    } else if (flag == "--draws" && index + 1 < argc) {
      options.draws = std::atoi(argv[++index]);
    } else if (flag == "--sampler-seed" && index + 1 < argc) {
      options.sampler_seed = static_cast<std::uint32_t>(
          std::strtoul(argv[++index], nullptr, 0));
    } else if (flag == "--threads" && index + 1 < argc) {
      options.threads = std::atoi(argv[++index]);
    } else {
      std::cerr << "unknown argument " << flag << "\n";
      return 2;
    }
  }
  if (options.corpus.empty() || options.out.empty()) {
    std::cerr << "need --corpus and --out\n";
    return 2;
  }
  return run(options);
}
