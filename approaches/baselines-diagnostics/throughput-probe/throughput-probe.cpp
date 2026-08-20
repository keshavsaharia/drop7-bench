// Measures how shallow policies trade immediate clears against gray-cover
// damage on a fixed, previously evaluated exploratory seed set. This file is
// not a validation benchmark.
#define main drop7_evolution_embedded_main
#include "../../heuristic-search/evolution/evolution.cpp"
#undef main

#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace drop7::throughput_probe {

namespace evo = drop7::evolution;

struct Profile {
  std::string_view name;
  evo::Weights weights;
};

evo::Weights throughputWeights(double intensity) {
  evo::Weights result = evo::kHandWeights;
  result[evo::kImmediateScore] = 1.0;
  result[evo::kImmediateClears] = 8.0 + 12.0 * intensity;
  result[evo::kImmediateReveals] = 20.0 + 80.0 * intensity;
  result[evo::kImmediateCrackProgress] = 10.0 + 50.0 * intensity;
  result[evo::kChainDepth] = 5.0;
  result[evo::kOccupancyReduction] = 20.0 + 80.0 * intensity;
  result[evo::kCoverReduction] = 25.0 + 95.0 * intensity;
  result[evo::kCoverLoad] = -4.0 - 20.0 * intensity;
  result[evo::kCrackedLoad] = 3.0 + 22.0 * intensity;
  result[evo::kCoverAltitude] = -32.0 - 48.0 * intensity;
  result[evo::kCrackedAltitude] = -12.0 - 20.0 * intensity;
  result[evo::kCoverExposure] = 13.0 + 47.0 * intensity;
  result[evo::kAccessibleCracked] = 18.0 + 62.0 * intensity;
  result[evo::kStoredHighNumbers] = 17.0 + 35.0 * intensity;
  result[evo::kDirectReadiness] = 18.0 + 30.0 * intensity;
  result[evo::kReleaseReadiness] = 12.0 + 40.0 * intensity;
  result[evo::kNextDropTriggers] = 20.0 + 45.0 * intensity;
  result[evo::kRiseTriggers] = 30.0 + 55.0 * intensity;
  result[evo::kRiseHighTriggers] = 34.0 + 45.0 * intensity;
  result[evo::kProjectedOccupancyDebt] = -32.0 - 35.0 * intensity;
  result[evo::kPhaseHeightRisk] = -86.0 - 50.0 * intensity;
  return result;
}

int run(std::ostream& output) {
  constexpr int games = 256;
  constexpr int maximum_moves = 1000;
  constexpr int chance_probes = 7;
  constexpr std::uint32_t seed_start = 0x3d70'0000u;
  const std::array<Profile, 5> profiles{{
      {"hand", evo::kHandWeights},
      {"cover-0.25", throughputWeights(0.25)},
      {"cover-0.50", throughputWeights(0.50)},
      {"cover-0.75", throughputWeights(0.75)},
      {"cover-1.00", throughputWeights(1.00)},
  }};
  const auto started = std::chrono::steady_clock::now();
  output << std::fixed << std::setprecision(3)
         << "{\"format\":\"drop7-throughput-historical-probe-v1\""
         << ",\"canonicalLevelBonus\":" << kLevelBonus
         << ",\"selectionEligible\":false"
         << ",\"seedStart\":\"0x3d700000\""
         << ",\"games\":" << games << ",\"profiles\":[";
  for (std::size_t index = 0; index < profiles.size(); ++index) {
    const evo::GameStats result = evo::evaluatePolicy(
        profiles[index].weights, seed_start, games, maximum_moves,
        chance_probes);
    if (index != 0) output << ',';
    output << "{\"name\":\"" << profiles[index].name
           << "\",\"meanScore\":" << result.mean_score
           << ",\"medianScore\":" << result.median_score
           << ",\"p10Score\":" << result.p10_score
           << ",\"p90Score\":" << result.p90_score
           << ",\"meanMoves\":" << result.mean_moves
           << ",\"minimumScore\":" << result.minimum_score
           << ",\"maximumScore\":" << result.maximum_score
           << ",\"censored\":" << result.censored << '}';
  }
  output << "],\"seconds\":"
         << std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started)
                .count()
         << "}\n";
  return 0;
}

}  // namespace drop7::throughput_probe

int main() {
  try {
    return drop7::throughput_probe::run(std::cout);
  } catch (const std::exception& error) {
    std::cerr << "drop7_throughput_probe: " << error.what() << '\n';
    return 1;
  }
}
