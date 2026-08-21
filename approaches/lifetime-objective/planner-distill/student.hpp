#pragma once

// Dependency-free float32 inference for the distilled afterstate evaluator.
//
// Shape: EmbeddingBag(8902, H, sum) + bias -> ReLU -> Linear(H, M) -> ReLU ->
// Linear(M, 2).  Head 0 is the planner residual (expected numbered discs the
// teacher clears over the rest of its window from this afterstate); head 1 is
// the auxiliary log remaining-lifetime head.
//
// Exactly 135 features are active per state, so the first layer is 135 gathered
// rows of H floats summed rather than a dense matrix product.  That is the whole
// reason this shape exists: a depth-4 fair expectimax evaluates 615,090 leaves
// per decision at five chance strata and 2,271,280 at seven
// (`learned-leaf/leaf-probe`), so a leaf evaluator has a budget of roughly one
// microsecond and the 4.12 ms residual CNN is ~2,900x over it.
//
// THE FEATURE SPACE IS NOT REDEFINED HERE.  It comes from
// `approaches/lifetime-objective/learned-leaf/leafnet.hpp`, which is already
// gated against `leaf_features.py` on real corpus states by that approach's
// `leaf-check`.  Only the weight file format and the two-output head are new.

#include "pinned/learned-leaf/leafnet.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace drop7::distill {

namespace features = drop7::leaf::features;

class Student {
 public:
  explicit Student(const std::string& path) { load(path); }

  int hidden() const { return hidden_; }
  int mid() const { return mid_; }
  std::uint64_t digest() const { return digest_; }
  std::size_t parameterCount() const { return parameters_; }

  // scratch must hold at least hidden_ + mid_ floats.
  void evaluate(const std::uint8_t* board, int nextDisc, int movesRemaining,
                float* __restrict scratch, float& residual,
                float& lifetimeLog) const {
    std::uint16_t index[features::kActive];
    features::build(board, nextDisc, movesRemaining, index);
    float* __restrict accumulator = scratch;
    float* __restrict middle = scratch + hidden_;
    std::memcpy(accumulator, ftBias_.data(),
                sizeof(float) * static_cast<std::size_t>(hidden_));
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
    float out[2] = {outBias_[0], outBias_[1]};
    for (int unit = 0; unit < 2; ++unit) {
      const float* __restrict row =
          outWeight_.data() + static_cast<std::size_t>(unit) * mid_;
      for (int k = 0; k < mid_; ++k) out[unit] += row[k] * middle[k];
    }
    residual = out[0];
    lifetimeLog = out[1];
  }

 private:
  static std::uint64_t fnv1a(const std::uint8_t* data, std::size_t length) {
    std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
    for (std::size_t index = 0; index < length; ++index) {
      hash ^= data[index];
      hash *= 0x0000'0100'0000'01b3ull;
    }
    return hash;
  }

  void load(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) throw std::runtime_error("cannot open " + path);
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    std::vector<std::uint8_t> blob(static_cast<std::size_t>(size));
    if (std::fread(blob.data(), 1, blob.size(), file) != blob.size()) {
      std::fclose(file);
      throw std::runtime_error("short read on " + path);
    }
    std::fclose(file);
    if (blob.size() < 8 + 4 + 4 * 4 + 4 + 8) {
      throw std::runtime_error("student file is too small");
    }
    if (std::memcmp(blob.data(), "D7PDST\0\0", 8) != 0) {
      throw std::runtime_error("student file has the wrong magic");
    }
    const std::size_t body = blob.size() - 8;
    std::memcpy(&digest_, blob.data() + body, 8);
    if (digest_ != fnv1a(blob.data(), body)) {
      throw std::runtime_error("student file failed its digest check");
    }
    std::size_t cursor = 8;
    const auto u32 = [&]() {
      std::uint32_t value = 0;
      std::memcpy(&value, blob.data() + cursor, 4);
      cursor += 4;
      return value;
    };
    if (u32() != 1u) throw std::runtime_error("unsupported student version");
    const int feature_count = static_cast<int>(u32());
    const int active = static_cast<int>(u32());
    hidden_ = static_cast<int>(u32());
    mid_ = static_cast<int>(u32());
    const int outputs = static_cast<int>(u32());
    if (feature_count != features::kFeatureCount || active != features::kActive) {
      throw std::runtime_error("student feature space differs from leafnet.hpp");
    }
    if (outputs != 2) throw std::runtime_error("student must have two outputs");

    const auto tensor = [&](std::vector<float>& out, std::size_t expected) {
      const std::uint32_t name_length = u32();
      cursor += name_length;
      const std::uint32_t rank = u32();
      std::size_t count = 1;
      for (std::uint32_t dimension = 0; dimension < rank; ++dimension) {
        count *= u32();
      }
      if (count != expected) {
        throw std::runtime_error("student tensor has an unexpected size");
      }
      out.resize(count);
      std::memcpy(out.data(), blob.data() + cursor, count * sizeof(float));
      cursor += count * sizeof(float);
    };
    const std::uint32_t tensor_count = u32();
    if (tensor_count != 6) throw std::runtime_error("student tensor count");
    tensor(ftWeight_, static_cast<std::size_t>(feature_count) * hidden_);
    tensor(ftBias_, static_cast<std::size_t>(hidden_));
    tensor(l2Weight_, static_cast<std::size_t>(mid_) * hidden_);
    tensor(l2Bias_, static_cast<std::size_t>(mid_));
    tensor(outWeight_, static_cast<std::size_t>(2) * mid_);
    tensor(outBias_, 2);
    parameters_ = ftWeight_.size() + ftBias_.size() + l2Weight_.size() +
                  l2Bias_.size() + outWeight_.size() + outBias_.size();
  }

  int hidden_ = 0;
  int mid_ = 0;
  std::uint64_t digest_ = 0;
  std::size_t parameters_ = 0;
  std::vector<float> ftWeight_, ftBias_, l2Weight_, l2Bias_, outWeight_, outBias_;
};

}  // namespace drop7::distill
