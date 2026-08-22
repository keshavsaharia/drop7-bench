#pragma once
// The fast fair leaf with its weights as run-time data.
//
// EQUIVALENCE CONTRACT.  weightedFairLeaf is drop7::fast::fastFairLeaf with
// each `leafweights::kXWeight` constant replaced by `weights.w[i]`, in the same
// order, with the same operand types (double * int or double * double), the
// same accumulator and no re-association.  At the frozen vector the two
// functions therefore produce the same uint64 bit pattern on every board, and
// gate.cpp --leaf-bits checks that claim on the boards a real search visits.
//
// Roughness is absent for the reason fast-leaf.hpp states at L7: its frozen
// weight is exactly 0.0 and the fast leaf no longer computes the feature.  The
// search space here is therefore eighteen coordinates, not nineteen.  Anyone
// who wants to evolve a roughness term must reinstate the feature first.
//
// The terminal-board constant (kFairTerminalUtility) is not a coordinate: the
// terminal utility was shown saturated in finding-04 and changing it is a
// different experiment.
//
// This header modifies no existing repository file.

#include "../fast-engine/fast-leaf.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace drop7::fastw {

constexpr int kLeafTerms = 18;

// fast-leaf.hpp:fastFairLeaf accumulation order, minus roughness.
constexpr const char* kLeafNames[kLeafTerms] = {
    "open_columns",           "height_load",
    "solid_cells",            "cracked_cells",
    "numbered_cells",         "high_low_numbers",
    "direct_potential",       "latent_chain_potential",
    "cracked_exposure",       "solid_exposure",
    "adjacent_ones",          "triple_twos",
    "dead_low_numbers",       "covered_height_risk",
    "low_number_height_risk", "danger_height_squared",
    "rise_pressure",          "next_disc_vertical_options",
};

constexpr double kFrozenWeights[kLeafTerms] = {
    fast::leafweights::kOpenColumnsWeight,
    fast::leafweights::kHeightLoadWeight,
    fast::leafweights::kSolidCellsWeight,
    fast::leafweights::kCrackedCellsWeight,
    fast::leafweights::kNumberedCellsWeight,
    fast::leafweights::kHighLowNumbersWeight,
    fast::leafweights::kDirectPotentialWeight,
    fast::leafweights::kLatentChainPotentialWeight,
    fast::leafweights::kCrackedExposureWeight,
    fast::leafweights::kSolidExposureWeight,
    fast::leafweights::kAdjacentOnesWeight,
    fast::leafweights::kTripleTwosWeight,
    fast::leafweights::kDeadLowNumbersWeight,
    fast::leafweights::kCoveredHeightRiskWeight,
    fast::leafweights::kLowNumberHeightRiskWeight,
    fast::leafweights::kDangerHeightSquaredWeight,
    fast::leafweights::kRisePressureWeight,
    fast::leafweights::kNextDiscVerticalOptionsWeight,
};

// Guard against the frozen constants moving underneath this experiment.
static_assert(kFrozenWeights[0] == 180.0);
static_assert(kFrozenWeights[4] == -18.0);
static_assert(kFrozenWeights[6] == 1'600.0);
static_assert(kFrozenWeights[15] == -1'250.0);
static_assert(kFrozenWeights[16] == -35.0);
static_assert(kFrozenWeights[17] == 220.0);

struct LeafWeights {
  double w[kLeafTerms] = {
      kFrozenWeights[0],  kFrozenWeights[1],  kFrozenWeights[2],
      kFrozenWeights[3],  kFrozenWeights[4],  kFrozenWeights[5],
      kFrozenWeights[6],  kFrozenWeights[7],  kFrozenWeights[8],
      kFrozenWeights[9],  kFrozenWeights[10], kFrozenWeights[11],
      kFrozenWeights[12], kFrozenWeights[13], kFrozenWeights[14],
      kFrozenWeights[15], kFrozenWeights[16], kFrozenWeights[17]};

  bool isFrozen() const {
    for (int i = 0; i < kLeafTerms; ++i) {
      std::uint64_t a = 0, b = 0;
      std::memcpy(&a, &w[i], sizeof a);
      std::memcpy(&b, &kFrozenWeights[i], sizeof b);
      if (a != b) return false;
    }
    return true;
  }
};

inline int leafIndexOf(const std::string& name) {
  for (int i = 0; i < kLeafTerms; ++i) {
    if (name == kLeafNames[i]) return i;
  }
  return -1;
}

// Bit-identical to fast::fastFairLeaf when `weights.isFrozen()`.
inline double weightedFairLeaf(const State& state, fast::LeafScratch& scratch,
                               const LeafWeights& weights) {
  if (state.game_over) return fast::leafweights::kFairTerminalUtility;
  fast::FastLeafFeatures f;
  fast::extractFastLeafFeatures<6>(state, scratch, f);
  // Preserve the frozen dot-product order for parity.
  double result = 0.0;
  result += weights.w[0] * f.open_columns;
  result += weights.w[1] * f.height_load;
  result += weights.w[2] * f.solid_cells;
  result += weights.w[3] * f.cracked_cells;
  result += weights.w[4] * f.numbered_cells;
  result += weights.w[5] * f.high_low_numbers;
  result += weights.w[6] * f.direct_potential;
  result += weights.w[7] * f.latent_chain_potential;
  result += weights.w[8] * f.cracked_exposure;
  result += weights.w[9] * f.solid_exposure;
  result += weights.w[10] * f.adjacent_ones;
  result += weights.w[11] * f.triple_twos;
  result += weights.w[12] * f.dead_low_numbers;
  result += weights.w[13] * f.covered_height_risk;
  result += weights.w[14] * f.low_number_height_risk;
  result += weights.w[15] * f.danger_height_squared;
  // (roughness: absent, see the header comment and fast-leaf.hpp L7)
  result += weights.w[16] * f.rise_pressure;
  result += weights.w[17] * f.next_disc_vertical_options;
  return result;
}

// "name value" lines, '#' comments, in any order; every term must be present
// exactly once so a truncated file cannot silently mean "frozen elsewhere".
inline LeafWeights readWeightsFile(const std::string& path) {
  std::ifstream file(path);
  if (!file) throw std::runtime_error("cannot open weights file " + path);
  LeafWeights weights;
  bool seen[kLeafTerms] = {};
  std::string line;
  while (std::getline(file, line)) {
    const auto hash = line.find('#');
    if (hash != std::string::npos) line.erase(hash);
    std::istringstream in(line);
    std::string name;
    if (!(in >> name)) continue;
    double value = 0.0;
    if (!(in >> value) || !std::isfinite(value)) {
      throw std::runtime_error("weights file: bad value for " + name);
    }
    const int index = leafIndexOf(name);
    if (index < 0) throw std::runtime_error("unknown leaf term " + name);
    if (seen[index]) throw std::runtime_error("duplicate leaf term " + name);
    seen[index] = true;
    weights.w[index] = value;
  }
  for (int i = 0; i < kLeafTerms; ++i) {
    if (!seen[i]) {
      throw std::runtime_error(std::string("weights file missing term ") +
                               kLeafNames[i]);
    }
  }
  return weights;
}

// One line of a population file: "<name> w0 w1 ... w17".
inline bool parsePopulationLine(const std::string& line, std::string& name,
                                LeafWeights& weights) {
  std::istringstream in(line);
  if (!(in >> name)) return false;
  if (name[0] == '#') return false;
  for (int i = 0; i < kLeafTerms; ++i) {
    if (!(in >> weights.w[i]) || !std::isfinite(weights.w[i])) {
      throw std::runtime_error("population line for " + name +
                               " has fewer than 18 finite weights");
    }
  }
  double extra = 0.0;
  if (in >> extra) {
    throw std::runtime_error("population line for " + name +
                             " has more than 18 weights");
  }
  return true;
}

}  // namespace drop7::fastw
