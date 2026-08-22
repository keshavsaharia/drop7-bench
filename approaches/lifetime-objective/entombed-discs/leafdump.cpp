// Dumps the frozen fast leaf's eighteen features and its value for every
// record of a .states corpus (afterstate-net/dataset.py STATE_DTYPE, 72 bytes),
// so an offline analysis can ask what a new feature adds BEYOND the leaf the
// search already has.  Reads no seed; the corpus is already-played data.
//
//   leafdump <corpus.states> <out.f32>
//
// Output: float32[19] per record in corpus order: the 18 features in the
// fast leaf's accumulation order (fastw::kLeafNames), then the leaf value.
// A record whose public state is invalid for the leaf writes 19 NaNs.

#include "weighted-leaf.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
using namespace drop7;
constexpr std::size_t kRecordBytes = 72;
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: leafdump <corpus.states> <out.f32>\n";
    return 2;
  }
  std::FILE* in = std::fopen(argv[1], "rb");
  std::FILE* out = std::fopen(argv[2], "wb");
  if (!in || !out) {
    std::cerr << "cannot open input or output\n";
    return 2;
  }
  fast::LeafScratch scratch;
  std::vector<unsigned char> record(kRecordBytes);
  std::uint64_t count = 0, invalid = 0;
  while (std::fread(record.data(), 1, kRecordBytes, in) == kRecordBytes) {
    State state;
    std::memcpy(state.board.data(), record.data(), kCellCount);
    state.next_disc = record[49];
    state.moves_remaining = record[50];
    state.game_over = false;
    float row[19];
    bool ok = state.moves_remaining >= 1 && state.moves_remaining <= kMovesPerLevel &&
              state.next_disc >= 1 && state.next_disc <= kBoardSize;
    if (ok) {
      fast::FastLeafFeatures f;
      fast::extractFastLeafFeatures<6>(state, scratch, f);
      row[0] = static_cast<float>(f.open_columns);
      row[1] = static_cast<float>(f.height_load);
      row[2] = static_cast<float>(f.solid_cells);
      row[3] = static_cast<float>(f.cracked_cells);
      row[4] = static_cast<float>(f.numbered_cells);
      row[5] = static_cast<float>(f.high_low_numbers);
      row[6] = static_cast<float>(f.direct_potential);
      row[7] = static_cast<float>(f.latent_chain_potential);
      row[8] = static_cast<float>(f.cracked_exposure);
      row[9] = static_cast<float>(f.solid_exposure);
      row[10] = static_cast<float>(f.adjacent_ones);
      row[11] = static_cast<float>(f.triple_twos);
      row[12] = static_cast<float>(f.dead_low_numbers);
      row[13] = static_cast<float>(f.covered_height_risk);
      row[14] = static_cast<float>(f.low_number_height_risk);
      row[15] = static_cast<float>(f.danger_height_squared);
      row[16] = static_cast<float>(f.rise_pressure);
      row[17] = static_cast<float>(f.next_disc_vertical_options);
      row[18] = static_cast<float>(fast::fastFairLeaf(state, scratch));
    } else {
      for (float& v : row) v = std::nanf("");
      ++invalid;
    }
    std::fwrite(row, sizeof(float), 19, out);
    ++count;
  }
  std::fclose(in);
  std::fclose(out);
  std::cerr << "leafdump: " << count << " records, " << invalid << " invalid\n";
  return 0;
}
