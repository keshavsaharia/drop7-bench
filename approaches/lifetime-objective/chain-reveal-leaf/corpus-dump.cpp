// Seed-free corpus dump for the chain-reveal-leaf corpus gate.  Template:
// approaches/lifetime-objective/entombed-discs/leafdump.cpp (same 72-byte
// STATE_DTYPE record, same 18-feature order); reads no seed.
//
//   corpus-dump <corpus.states> <leaf.f32> <setups.f32> <setups-index.u32>
//
// leaf.f32          float32[26] per record in corpus order: the 18 frozen
//                   features (fastw::kLeafNames order), the frozen leaf value,
//                   then the 7 extra terms (fastx::kExtraNames order).  An
//                   invalid public state writes 26 NaNs.
// setups.f32        float32[98] per DEPTH-4 record (behaviorDepth == 4), in
//                   corpus order: for each of the 49 cells the best
//                   support-disjoint pair product r[a]*r[b] when the cell is
//                   kSolid (else 0), then the same with rel[a]*rel[b].  These
//                   feed the uncollected-setup rate.
// setups-index.u32  uint32 corpus record index of each setups.f32 row.

#include "extra-terms.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
using namespace drop7;
constexpr std::size_t kRecordBytes = 72;
}  // namespace

int main(int argc, char** argv) {
  if (argc != 5) {
    std::cerr << "usage: corpus-dump <corpus.states> <leaf.f32> <setups.f32> <setups-index.u32>\n";
    return 2;
  }
  std::FILE* in = std::fopen(argv[1], "rb");
  std::FILE* out = std::fopen(argv[2], "wb");
  std::FILE* setups = std::fopen(argv[3], "wb");
  std::FILE* index = std::fopen(argv[4], "wb");
  if (!in || !out || !setups || !index) {
    std::cerr << "cannot open input or output\n";
    return 2;
  }
  fast::LeafScratch scratch;
  std::vector<unsigned char> record(kRecordBytes);
  std::uint64_t count = 0, invalid = 0, depth4 = 0;
  while (std::fread(record.data(), 1, kRecordBytes, in) == kRecordBytes) {
    State state;
    std::memcpy(state.board.data(), record.data(), kCellCount);
    state.next_disc = record[49];
    state.moves_remaining = record[50];
    state.game_over = false;
    const int behaviorDepth = record[59];  // STATE_DTYPE offset of behaviorDepth
    float row[26];
    const bool ok = state.moves_remaining >= 1 && state.moves_remaining <= kMovesPerLevel &&
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
      // fastFairLeaf re-extracted into the same scratch: identical board, so
      // the scratch still describes this record.
      fastx::ExtraFeatures extra;
      fastx::extractExtraFeatures(state.board, state.moves_remaining, scratch, extra);
      for (int i = 0; i < fastx::kExtraTerms; ++i) row[19 + i] = static_cast<float>(extra.v[i]);
      if (behaviorDepth == 4) {
        float cells[98];
        auto r = [&](int x) {
          const auto s = static_cast<std::size_t>(x);
          return fast::unionReadinessFast(scratch.addition[s], scratch.release[s]);
        };
        auto rel = [&](int x) { return scratch.release[static_cast<std::size_t>(x)]; };
        for (int cell = 0; cell < kCellCount; ++cell) {
          const bool solid = state.board[static_cast<std::size_t>(cell)] == kSolid;
          cells[cell] = solid ? static_cast<float>(fastx::detail::bestPair(state.board, scratch, cell, r)) : 0.0f;
          cells[49 + cell] = solid ? static_cast<float>(fastx::detail::bestPair(state.board, scratch, cell, rel)) : 0.0f;
        }
        const auto idx = static_cast<std::uint32_t>(count);
        std::fwrite(cells, sizeof(float), 98, setups);
        std::fwrite(&idx, sizeof idx, 1, index);
        ++depth4;
      }
    } else {
      for (float& v : row) v = std::nanf("");
      ++invalid;
    }
    std::fwrite(row, sizeof(float), 26, out);
    ++count;
  }
  std::fclose(in);
  std::fclose(out);
  std::fclose(setups);
  std::fclose(index);
  std::cerr << "corpus-dump: " << count << " records, " << invalid << " invalid, " << depth4 << " depth-4 setup rows\n";
  return 0;
}
