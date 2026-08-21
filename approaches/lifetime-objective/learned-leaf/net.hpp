#pragma once
// Dependency-free float32 inference for the exported SurvivalNet.
//
// No libtorch, no BLAS, no dynamic library: the search that consumes this is
// called hundreds of thousands of times per decision and must remain a single
// self-contained native binary.
//
// Layout choice.  Everything is NHWC (pixel-major, channel-minor) with 49
// pixels, so a 3x3 convolution becomes 49 x 9 rank-1 updates of a C_out-wide
// accumulator and the innermost loop is a unit-stride FMA over channels that
// clang vectorises.  Weights are repacked once at load time into
// [tap][in][out] order so the inner loop reads them contiguously.
//
// Numerics.  Accumulation is float32 in a different order from PyTorch's
// im2col GEMM, so outputs agree to float32 rounding rather than bit-exactly.
// The parity gate in net-check.cpp states the achieved tolerance.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace drop7::leaf {

constexpr int kBoard = 7;
constexpr int kPixels = kBoard * kBoard;

struct Tensor {
  std::vector<std::uint32_t> dims;
  std::vector<float> data;
  std::size_t count() const { return data.size(); }
};

struct NetOutput {
  std::vector<float> hazardLogits;  // hazardHorizon logits
  float lifetimeLog = 0.0f;         // log1p(moves remaining)
  float clears = 0.0f;
  float reveals = 0.0f;
};

class Reader {
 public:
  explicit Reader(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) throw std::runtime_error("cannot open model " + path);
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    bytes_.resize(static_cast<std::size_t>(size));
    if (std::fread(bytes_.data(), 1, bytes_.size(), file) != bytes_.size()) {
      std::fclose(file);
      throw std::runtime_error("short read on model " + path);
    }
    std::fclose(file);
  }

  const std::vector<std::uint8_t>& bytes() const { return bytes_; }

  template <typename T>
  T take() {
    T value{};
    if (offset_ + sizeof(T) > bytes_.size()) throw std::runtime_error("model truncated");
    std::memcpy(&value, bytes_.data() + offset_, sizeof(T));
    offset_ += sizeof(T);
    return value;
  }

  void takeBytes(void* destination, std::size_t count) {
    if (offset_ + count > bytes_.size()) throw std::runtime_error("model truncated");
    std::memcpy(destination, bytes_.data() + offset_, count);
    offset_ += count;
  }

  std::size_t offset() const { return offset_; }

 private:
  std::vector<std::uint8_t> bytes_;
  std::size_t offset_ = 0;
};

inline std::uint64_t fnv1a(const std::uint8_t* data, std::size_t count) {
  std::uint64_t hash = 0xCBF2'9CE4'8422'2325ull;
  for (std::size_t index = 0; index < count; ++index) {
    hash ^= data[index];
    hash *= 0x0000'0100'0000'01B3ull;
  }
  return hash;
}

// ---------------------------------------------------------------------------
// Weights
// ---------------------------------------------------------------------------

struct ConvWeights {
  int inChannels = 0;
  int outChannels = 0;
  int kernel = 3;
  // [tap][in][out]
  std::vector<float> packed;
};

struct NormWeights {
  std::vector<float> gamma;
  std::vector<float> beta;
};

struct LinearWeights {
  int inFeatures = 0;
  int outFeatures = 0;
  std::vector<float> weight;  // [out][in]
  std::vector<float> bias;
};

struct HeadWeights {
  LinearWeights first;
  LinearWeights second;
};

class SurvivalNet {
 public:
  explicit SurvivalNet(const std::string& path) { load(path); }

  int planes() const { return planes_; }
  int channels() const { return channels_; }
  int blocks() const { return blocks_; }
  int hazardHorizon() const { return hazardHorizon_; }
  std::uint64_t digest() const { return digest_; }
  std::size_t parameterCount() const { return parameters_; }

  // Encodes a public state into the NHWC input buffer expected by forward().
  // Mirrors approaches/lifetime-objective/afterstate-net/dataset.py::encode
  // exactly, including plane order.
  void encode(const std::uint8_t* board, int nextDisc, int movesRemaining,
              float* out) const {
    std::memset(out, 0, sizeof(float) * kPixels * static_cast<std::size_t>(planes_));
    const float rise = (static_cast<float>(movesRemaining) - 1.0f) / 4.0f;
    for (int pixel = 0; pixel < kPixels; ++pixel) {
      float* cell = out + static_cast<std::size_t>(pixel) * planes_;
      const std::uint8_t value = board[pixel];
      if (value >= 1 && value <= 7) cell[value - 1] = 1.0f;
      else if (value == 8) cell[7] = 1.0f;
      else if (value == 9) cell[8] = 1.0f;
      else cell[9] = 1.0f;
      if (nextDisc >= 1 && nextDisc <= 7) cell[9 + nextDisc] = 1.0f;
      cell[17] = rise;
    }
  }

  const std::vector<int>& tapSource() const { return tapSource_; }

  const ConvWeights& stem() const { return stem_; }
  const NormWeights& stemNorm() const { return stemNorm_; }
  const std::vector<ConvWeights>& blockConvs() const { return blockConvs_; }
  const std::vector<NormWeights>& blockNorms() const { return blockNorms_; }
  const ConvWeights& pool() const { return pool_; }
  const NormWeights& poolNorm() const { return poolNorm_; }
  const HeadWeights& hazard() const { return hazard_; }
  const HeadWeights& lifetime() const { return lifetime_; }
  const HeadWeights& flow() const { return flow_; }
  int poolChannels() const { return poolChannels_; }
  int groups() const { return groups_; }
  float epsilon() const { return epsilon_; }

 private:
  void load(const std::string& path) {
    Reader reader(path);
    const std::vector<std::uint8_t>& raw = reader.bytes();
    if (raw.size() < 8 + 4 + 32 + 4 + 4 + 8) throw std::runtime_error("model too small");
    const std::uint64_t stored = fnv1a(raw.data(), raw.size() - 8);
    std::uint64_t recorded = 0;
    std::memcpy(&recorded, raw.data() + raw.size() - 8, 8);
    if (stored != recorded) throw std::runtime_error("model checksum mismatch");
    digest_ = recorded;

    char magic[8];
    reader.takeBytes(magic, 8);
    if (std::memcmp(magic, "D7NET\0\0\0", 8) != 0) {
      throw std::runtime_error("not a d7net model file");
    }
    const auto version = reader.take<std::uint32_t>();
    if (version != 1) throw std::runtime_error("unsupported d7net version");
    planes_ = static_cast<int>(reader.take<std::uint32_t>());
    channels_ = static_cast<int>(reader.take<std::uint32_t>());
    blocks_ = static_cast<int>(reader.take<std::uint32_t>());
    poolChannels_ = static_cast<int>(reader.take<std::uint32_t>());
    hazardHorizon_ = static_cast<int>(reader.take<std::uint32_t>());
    headHidden_ = static_cast<int>(reader.take<std::uint32_t>());
    const int boardSize = static_cast<int>(reader.take<std::uint32_t>());
    groups_ = static_cast<int>(reader.take<std::uint32_t>());
    if (boardSize != kBoard) throw std::runtime_error("unexpected board size");
    epsilon_ = reader.take<float>();

    const auto tensorCount = reader.take<std::uint32_t>();
    std::unordered_map<std::string, Tensor> tensors;
    parameters_ = 0;
    for (std::uint32_t index = 0; index < tensorCount; ++index) {
      const auto nameBytes = reader.take<std::uint32_t>();
      std::string name(nameBytes, '\0');
      reader.takeBytes(name.data(), nameBytes);
      const auto rank = reader.take<std::uint32_t>();
      Tensor tensor;
      std::size_t count = 1;
      for (std::uint32_t d = 0; d < rank; ++d) {
        const auto dim = reader.take<std::uint32_t>();
        tensor.dims.push_back(dim);
        count *= dim;
      }
      tensor.data.resize(count);
      reader.takeBytes(tensor.data.data(), count * sizeof(float));
      parameters_ += count;
      tensors.emplace(std::move(name), std::move(tensor));
    }

    auto fetch = [&](const std::string& name) -> const Tensor& {
      const auto found = tensors.find(name);
      if (found == tensors.end()) throw std::runtime_error("missing tensor " + name);
      return found->second;
    };

    stem_ = packConv(fetch("stem.0.weight"));
    stemNorm_ = packNorm(fetch("stem.1.weight"), fetch("stem.1.bias"));
    blockConvs_.clear();
    blockNorms_.clear();
    for (int block = 0; block < blocks_; ++block) {
      const std::string prefix = "tower." + std::to_string(block) + ".";
      blockConvs_.push_back(packConv(fetch(prefix + "a.weight")));
      blockNorms_.push_back(packNorm(fetch(prefix + "na.weight"), fetch(prefix + "na.bias")));
      blockConvs_.push_back(packConv(fetch(prefix + "b.weight")));
      blockNorms_.push_back(packNorm(fetch(prefix + "nb.weight"), fetch(prefix + "nb.bias")));
    }
    pool_ = packConv(fetch("pool.0.weight"));
    poolNorm_ = packNorm(fetch("pool.1.weight"), fetch("pool.1.bias"));
    hazard_ = packHead(tensors, "hazard");
    lifetime_ = packHead(tensors, "lifetime");
    flow_ = packHead(tensors, "flow");

    // Per output pixel and 3x3 tap, the source pixel index or -1 for the
    // zero-padded border.
    tapSource_.assign(9 * kPixels, -1);
    for (int tap = 0; tap < 9; ++tap) {
      const int dr = tap / 3 - 1;
      const int dc = tap % 3 - 1;
      for (int row = 0; row < kBoard; ++row) {
        for (int column = 0; column < kBoard; ++column) {
          const int sr = row + dr;
          const int sc = column + dc;
          const int pixel = row * kBoard + column;
          tapSource_[static_cast<std::size_t>(tap) * kPixels + pixel] =
              (sr >= 0 && sr < kBoard && sc >= 0 && sc < kBoard) ? sr * kBoard + sc : -1;
        }
      }
    }
  }

  static ConvWeights packConv(const Tensor& tensor) {
    if (tensor.dims.size() != 4) throw std::runtime_error("conv rank must be 4");
    ConvWeights conv;
    conv.outChannels = static_cast<int>(tensor.dims[0]);
    conv.inChannels = static_cast<int>(tensor.dims[1]);
    conv.kernel = static_cast<int>(tensor.dims[2]);
    const int taps = conv.kernel * conv.kernel;
    conv.packed.assign(static_cast<std::size_t>(taps) * conv.inChannels * conv.outChannels, 0.0f);
    for (int oc = 0; oc < conv.outChannels; ++oc) {
      for (int ic = 0; ic < conv.inChannels; ++ic) {
        for (int tap = 0; tap < taps; ++tap) {
          const std::size_t source =
              ((static_cast<std::size_t>(oc) * conv.inChannels + ic) * taps) + tap;
          const std::size_t destination =
              ((static_cast<std::size_t>(tap) * conv.inChannels + ic) * conv.outChannels) + oc;
          conv.packed[destination] = tensor.data[source];
        }
      }
    }
    return conv;
  }

  static NormWeights packNorm(const Tensor& gamma, const Tensor& beta) {
    return NormWeights{gamma.data, beta.data};
  }

  static LinearWeights packLinear(const Tensor& weight, const Tensor& bias) {
    LinearWeights linear;
    linear.outFeatures = static_cast<int>(weight.dims[0]);
    linear.inFeatures = static_cast<int>(weight.dims[1]);
    linear.weight = weight.data;
    linear.bias = bias.data;
    return linear;
  }

  static HeadWeights packHead(const std::unordered_map<std::string, Tensor>& tensors,
                              const std::string& name) {
    auto get = [&](const std::string& key) -> const Tensor& {
      const auto found = tensors.find(key);
      if (found == tensors.end()) throw std::runtime_error("missing tensor " + key);
      return found->second;
    };
    HeadWeights head;
    head.first = packLinear(get(name + ".0.weight"), get(name + ".0.bias"));
    head.second = packLinear(get(name + ".2.weight"), get(name + ".2.bias"));
    return head;
  }

  int planes_ = 0, channels_ = 0, blocks_ = 0, poolChannels_ = 0;
  int hazardHorizon_ = 0, headHidden_ = 0, groups_ = 0;
  float epsilon_ = 1e-5f;
  std::uint64_t digest_ = 0;
  std::size_t parameters_ = 0;
  ConvWeights stem_, pool_;
  NormWeights stemNorm_, poolNorm_;
  std::vector<ConvWeights> blockConvs_;
  std::vector<NormWeights> blockNorms_;
  HeadWeights hazard_, lifetime_, flow_;
  std::vector<int> tapSource_;
};

// ---------------------------------------------------------------------------
// Evaluator: one per thread, owns its scratch buffers.
// ---------------------------------------------------------------------------
class Evaluator {
 public:
  explicit Evaluator(const SurvivalNet& net) : net_(net) {
    const std::size_t wide = static_cast<std::size_t>(kPixels) * net.channels();
    inputBuffer_.assign(static_cast<std::size_t>(kPixels) * net.planes(), 0.0f);
    a_.assign(wide, 0.0f);
    b_.assign(wide, 0.0f);
    pooled_.assign(static_cast<std::size_t>(kPixels) * net.poolChannels(), 0.0f);
    body_.assign(static_cast<std::size_t>(kPixels) * net.poolChannels(), 0.0f);
    hidden_.assign(256, 0.0f);
    output_.hazardLogits.assign(static_cast<std::size_t>(net.hazardHorizon()), 0.0f);
  }

  float* input() { return inputBuffer_.data(); }

  const NetOutput& forward() {
    conv(net_.stem(), inputBuffer_.data(), a_.data());
    groupNorm(net_.stemNorm(), net_.channels(), net_.groups(), a_.data());
    relu(a_.data(), a_.size());

    const auto& convs = net_.blockConvs();
    const auto& norms = net_.blockNorms();
    for (int block = 0; block < net_.blocks(); ++block) {
      conv(convs[static_cast<std::size_t>(block) * 2], a_.data(), b_.data());
      groupNorm(norms[static_cast<std::size_t>(block) * 2], net_.channels(), net_.groups(), b_.data());
      relu(b_.data(), b_.size());
      conv(convs[static_cast<std::size_t>(block) * 2 + 1], b_.data(), c_scratch(b_.size()));
      groupNorm(norms[static_cast<std::size_t>(block) * 2 + 1], net_.channels(), net_.groups(), c_.data());
      for (std::size_t index = 0; index < a_.size(); ++index) {
        const float sum = a_[index] + c_[index];
        a_[index] = sum > 0.0f ? sum : 0.0f;
      }
    }

    conv(net_.pool(), a_.data(), pooled_.data());
    groupNorm(net_.poolNorm(), net_.poolChannels(), net_.poolChannels(), pooled_.data());
    relu(pooled_.data(), pooled_.size());

    // nn.Flatten on an NCHW tensor is channel-major; the working layout is
    // pixel-major, so transpose once here.
    const int pc = net_.poolChannels();
    for (int pixel = 0; pixel < kPixels; ++pixel) {
      for (int channel = 0; channel < pc; ++channel) {
        body_[static_cast<std::size_t>(channel) * kPixels + pixel] =
            pooled_[static_cast<std::size_t>(pixel) * pc + channel];
      }
    }

    head(net_.hazard(), output_.hazardLogits.data());
    float life = 0.0f;
    head(net_.lifetime(), &life);
    output_.lifetimeLog = life;
    float flow[2] = {0.0f, 0.0f};
    head(net_.flow(), flow);
    output_.clears = flow[0];
    output_.reveals = flow[1];
    return output_;
  }

 private:
  float* c_scratch(std::size_t size) {
    if (c_.size() < size) c_.assign(size, 0.0f);
    return c_.data();
  }

  void conv(const ConvWeights& weights, const float* __restrict source,
            float* __restrict destination) const {
    const int inChannels = weights.inChannels;
    const int outChannels = weights.outChannels;
    const int taps = weights.kernel * weights.kernel;
    const float* __restrict packed = weights.packed.data();
    if (taps == 1) {
      for (int pixel = 0; pixel < kPixels; ++pixel) {
        float* __restrict out = destination + static_cast<std::size_t>(pixel) * outChannels;
        for (int oc = 0; oc < outChannels; ++oc) out[oc] = 0.0f;
        const float* __restrict in = source + static_cast<std::size_t>(pixel) * inChannels;
        for (int ic = 0; ic < inChannels; ++ic) {
          const float value = in[ic];
          const float* __restrict row = packed + static_cast<std::size_t>(ic) * outChannels;
          for (int oc = 0; oc < outChannels; ++oc) out[oc] += value * row[oc];
        }
      }
      return;
    }
    const int* __restrict tapSource = net_.tapSource().data();
    for (int pixel = 0; pixel < kPixels; ++pixel) {
      float* __restrict out = destination + static_cast<std::size_t>(pixel) * outChannels;
      for (int oc = 0; oc < outChannels; ++oc) out[oc] = 0.0f;
      for (int tap = 0; tap < taps; ++tap) {
        const int neighbour = tapSource[static_cast<std::size_t>(tap) * kPixels + pixel];
        if (neighbour < 0) continue;
        const float* __restrict in = source + static_cast<std::size_t>(neighbour) * inChannels;
        const float* __restrict plane =
            packed + static_cast<std::size_t>(tap) * inChannels * outChannels;
        for (int ic = 0; ic < inChannels; ++ic) {
          const float value = in[ic];
          if (value == 0.0f) continue;
          const float* __restrict row = plane + static_cast<std::size_t>(ic) * outChannels;
          for (int oc = 0; oc < outChannels; ++oc) out[oc] += value * row[oc];
        }
      }
    }
  }

  void groupNorm(const NormWeights& weights, int channels, int groups,
                 float* __restrict data) const {
    const int perGroup = channels / groups;
    const float eps = net_.epsilon();
    for (int group = 0; group < groups; ++group) {
      const int first = group * perGroup;
      double sum = 0.0;
      double square = 0.0;
      for (int pixel = 0; pixel < kPixels; ++pixel) {
        const float* cell = data + static_cast<std::size_t>(pixel) * channels + first;
        for (int index = 0; index < perGroup; ++index) {
          const double value = cell[index];
          sum += value;
          square += value * value;
        }
      }
      const double count = static_cast<double>(kPixels) * perGroup;
      const double mean = sum / count;
      const double variance = square / count - mean * mean;
      const float scale = static_cast<float>(1.0 / std::sqrt(variance + eps));
      const float shift = static_cast<float>(mean);
      for (int pixel = 0; pixel < kPixels; ++pixel) {
        float* cell = data + static_cast<std::size_t>(pixel) * channels + first;
        for (int index = 0; index < perGroup; ++index) {
          cell[index] = (cell[index] - shift) * scale * weights.gamma[first + index] +
                        weights.beta[first + index];
        }
      }
    }
  }

  static void relu(float* data, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
      if (data[index] < 0.0f) data[index] = 0.0f;
    }
  }

  void head(const HeadWeights& weights, float* out) {
    const LinearWeights& first = weights.first;
    if (static_cast<int>(hidden_.size()) < first.outFeatures) {
      hidden_.assign(static_cast<std::size_t>(first.outFeatures), 0.0f);
    }
    const float* __restrict x = body_.data();
    for (int unit = 0; unit < first.outFeatures; ++unit) {
      const float* __restrict row =
          first.weight.data() + static_cast<std::size_t>(unit) * first.inFeatures;
      float total = first.bias[unit];
      for (int index = 0; index < first.inFeatures; ++index) total += row[index] * x[index];
      hidden_[unit] = total > 0.0f ? total : 0.0f;
    }
    const LinearWeights& second = weights.second;
    for (int unit = 0; unit < second.outFeatures; ++unit) {
      const float* __restrict row =
          second.weight.data() + static_cast<std::size_t>(unit) * second.inFeatures;
      float total = second.bias[unit];
      for (int index = 0; index < second.inFeatures; ++index) total += row[index] * hidden_[index];
      out[unit] = total;
    }
  }

  const SurvivalNet& net_;
  std::vector<float> inputBuffer_, a_, b_, c_, pooled_, body_, hidden_;
  NetOutput output_;
};

}  // namespace drop7::leaf
