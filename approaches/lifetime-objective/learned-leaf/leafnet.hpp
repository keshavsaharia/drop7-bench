#pragma once
// Dependency-free float32 inference for the leaf-affordable student network.
//
// Shape: EmbeddingBag(8902, H, sum) + bias -> ReLU -> Linear(H, M) -> ReLU ->
// Linear(M, 12 hazard logits + 1 log-lifetime + 2 flow).  Exactly 135 features
// are active per state, so the first layer is 135 gathered rows of H floats
// summed, not a dense matrix product.  That is the whole reason this model
// exists: it is the version of the survival evaluator that fits inside a
// depth-4 expectimax leaf.
//
// The feature space is defined once, in leaf_features.py, and mirrored here.
// leaf-check.cpp gates the two against each other on real corpus states.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace drop7::leaf {

namespace features {
constexpr int kBoard = 7;
constexpr int kCells = kBoard * kBoard;
constexpr int kValues = 10;
constexpr int kCellBase = 0;
constexpr int kNextBase = 490;
constexpr int kMovesBase = 497;
constexpr int kHPairBase = 502;
constexpr int kVPairBase = 4702;
constexpr int kFeatureCount = 8902;
constexpr int kActive = kCells + 1 + 1 + 42 + 42;  // 135

// Writes the kActive feature indices for one public state.
inline void build(const std::uint8_t* board, int nextDisc, int movesRemaining,
                  std::uint16_t* out) {
  int cursor = 0;
  for (int cell = 0; cell < kCells; ++cell) {
    out[cursor++] = static_cast<std::uint16_t>(kCellBase + cell * kValues + board[cell]);
  }
  out[cursor++] = static_cast<std::uint16_t>(kNextBase + nextDisc - 1);
  out[cursor++] = static_cast<std::uint16_t>(kMovesBase + movesRemaining - 1);
  int pair = 0;
  for (int row = 0; row < kBoard; ++row) {
    for (int column = 0; column + 1 < kBoard; ++column, ++pair) {
      const int a = board[row * kBoard + column];
      const int b = board[row * kBoard + column + 1];
      out[cursor++] = static_cast<std::uint16_t>(kHPairBase + pair * 100 + a * kValues + b);
    }
  }
  pair = 0;
  for (int row = 0; row + 1 < kBoard; ++row) {
    for (int column = 0; column < kBoard; ++column, ++pair) {
      const int a = board[row * kBoard + column];
      const int b = board[(row + 1) * kBoard + column];
      out[cursor++] = static_cast<std::uint16_t>(kVPairBase + pair * 100 + a * kValues + b);
    }
  }
}
}  // namespace features

struct LeafOutput {
  float hazardLogits[16] = {};
  float lifetimeLog = 0.0f;
  float clears = 0.0f;
  float reveals = 0.0f;
};

class LeafNet {
 public:
  explicit LeafNet(const std::string& path) { load(path); }

  int hidden() const { return hidden_; }
  int mid() const { return mid_; }
  int outputs() const { return outputs_; }
  int hazardHorizon() const { return hazardHorizon_; }
  std::uint64_t digest() const { return digest_; }
  std::size_t parameterCount() const { return parameters_; }

  void evaluate(const std::uint8_t* board, int nextDisc, int movesRemaining,
                LeafOutput& out, float* scratch) const {
    std::uint16_t index[features::kActive];
    features::build(board, nextDisc, movesRemaining, index);
    evaluateIndexed(index, out, scratch);
  }

  // scratch must hold at least hidden_ + mid_ floats.
  void evaluateIndexed(const std::uint16_t* index, LeafOutput& out,
                       float* __restrict scratch) const {
    float* __restrict accumulator = scratch;
    float* __restrict middle = scratch + hidden_;
    std::memcpy(accumulator, ftBias_.data(), sizeof(float) * static_cast<std::size_t>(hidden_));
    for (int slot = 0; slot < features::kActive; ++slot) {
      const float* __restrict row =
          ftWeight_.data() + static_cast<std::size_t>(index[slot]) * hidden_;
      for (int unit = 0; unit < hidden_; ++unit) accumulator[unit] += row[unit];
    }
    for (int unit = 0; unit < hidden_; ++unit) {
      if (accumulator[unit] < 0.0f) accumulator[unit] = 0.0f;
    }
    for (int unit = 0; unit < mid_; ++unit) {
      const float* __restrict row =
          l2Weight_.data() + static_cast<std::size_t>(unit) * hidden_;
      float total = l2Bias_[unit];
      for (int k = 0; k < hidden_; ++k) total += row[k] * accumulator[k];
      middle[unit] = total > 0.0f ? total : 0.0f;
    }
    for (int unit = 0; unit < outputs_; ++unit) {
      const float* __restrict row =
          outWeight_.data() + static_cast<std::size_t>(unit) * mid_;
      float total = outBias_[unit];
      for (int k = 0; k < mid_; ++k) total += row[k] * middle[k];
      if (unit < hazardHorizon_) out.hazardLogits[unit] = total;
      else if (unit == hazardHorizon_) out.lifetimeLog = total;
      else if (unit == hazardHorizon_ + 1) out.clears = total;
      else out.reveals = total;
    }
  }

 private:
  static std::uint64_t fnv1a(const std::uint8_t* data, std::size_t count) {
    std::uint64_t hash = 0xCBF2'9CE4'8422'2325ull;
    for (std::size_t index = 0; index < count; ++index) {
      hash ^= data[index];
      hash *= 0x0000'0100'0000'01B3ull;
    }
    return hash;
  }

  void load(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) throw std::runtime_error("cannot open leaf model " + path);
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(size));
    if (std::fread(raw.data(), 1, raw.size(), file) != raw.size()) {
      std::fclose(file);
      throw std::runtime_error("short read on leaf model");
    }
    std::fclose(file);
    if (raw.size() < 64) throw std::runtime_error("leaf model too small");
    std::uint64_t recorded = 0;
    std::memcpy(&recorded, raw.data() + raw.size() - 8, 8);
    if (fnv1a(raw.data(), raw.size() - 8) != recorded) {
      throw std::runtime_error("leaf model checksum mismatch");
    }
    digest_ = recorded;

    std::size_t offset = 0;
    auto take = [&](void* destination, std::size_t count) {
      if (offset + count > raw.size()) throw std::runtime_error("leaf model truncated");
      std::memcpy(destination, raw.data() + offset, count);
      offset += count;
    };
    auto takeU32 = [&]() { std::uint32_t v = 0; take(&v, 4); return v; };

    char magic[8];
    take(magic, 8);
    if (std::memcmp(magic, "D7LEAF\0\0", 8) != 0) {
      throw std::runtime_error("not a d7leaf model file");
    }
    if (takeU32() != 1) throw std::runtime_error("unsupported d7leaf version");
    const int featureCount = static_cast<int>(takeU32());
    const int active = static_cast<int>(takeU32());
    hidden_ = static_cast<int>(takeU32());
    mid_ = static_cast<int>(takeU32());
    outputs_ = static_cast<int>(takeU32());
    hazardHorizon_ = static_cast<int>(takeU32());
    const int boardSize = static_cast<int>(takeU32());
    if (featureCount != features::kFeatureCount || active != features::kActive ||
        boardSize != features::kBoard) {
      throw std::runtime_error("leaf model feature space does not match this build");
    }
    if (outputs_ > 16 + 3 || hazardHorizon_ > 16) {
      throw std::runtime_error("leaf model head too wide for this build");
    }

    const auto tensorCount = takeU32();
    std::unordered_map<std::string, std::vector<float>> tensors;
    parameters_ = 0;
    for (std::uint32_t index = 0; index < tensorCount; ++index) {
      const auto nameBytes = takeU32();
      std::string name(nameBytes, '\0');
      take(name.data(), nameBytes);
      const auto rank = takeU32();
      std::size_t count = 1;
      for (std::uint32_t d = 0; d < rank; ++d) count *= takeU32();
      std::vector<float> data(count);
      take(data.data(), count * sizeof(float));
      parameters_ += count;
      tensors.emplace(std::move(name), std::move(data));
    }
    auto fetch = [&](const std::string& name) {
      const auto found = tensors.find(name);
      if (found == tensors.end()) throw std::runtime_error("missing tensor " + name);
      return found->second;
    };
    ftWeight_ = fetch("ft.weight");
    ftBias_ = fetch("ft_bias");
    l2Weight_ = fetch("l2.weight");
    l2Bias_ = fetch("l2.bias");
    outWeight_ = fetch("out.weight");
    outBias_ = fetch("out.bias");
  }

  int hidden_ = 0, mid_ = 0, outputs_ = 0, hazardHorizon_ = 0;
  std::uint64_t digest_ = 0;
  std::size_t parameters_ = 0;
  std::vector<float> ftWeight_, ftBias_, l2Weight_, l2Bias_, outWeight_, outBias_;
};

}  // namespace drop7::leaf
