#pragma once
// Shared helpers for the fast-engine gates, profile and benchmark: seed-lease
// bookkeeping, real-state harvesting and process memory readout.
//
// Seed lease SEEDLEASE-A52-FAST covers 0xa5270000-0xa5277fff.  Nothing in this
// approach touches a seed outside it.  Every program here is a CHECK-tier
// mechanical or timing operation and makes no strength claim, so no cohort role
// is consumed beyond marking the lease block as read.

#include "fast-engine.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace drop7::fast {

inline constexpr std::uint32_t kLeaseFirst = 0xa527'0000u;
inline constexpr std::uint32_t kLeaseLast = 0xa527'7fffu;

// Sub-blocks, fixed here so two programs never overlap.
inline constexpr std::uint32_t kTrajectoryDeterministicSeeds = 0xa527'0000u;
inline constexpr std::uint32_t kTrajectorySearchSeeds = 0xa527'1000u;
inline constexpr std::uint32_t kSearchParitySeeds = 0xa527'2000u;
inline constexpr std::uint32_t kLeafCorpusSeeds = 0xa527'3000u;
inline constexpr std::uint32_t kProfileCorpusSeeds = 0xa527'4000u;
inline constexpr std::uint32_t kBenchmarkSeeds = 0xa527'5000u;

inline void requireLease(std::uint32_t seed) {
  if (seed < kLeaseFirst || seed > kLeaseLast) {
    std::ostringstream message;
    message << "seed 0x" << std::hex << seed
            << " is outside SEEDLEASE-A52-FAST [0xa5270000, 0xa5277fff]";
    throw std::runtime_error(message.str());
  }
}

// Peak resident set of the whole process, in bytes.
inline std::uint64_t peakResidentBytes() {
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    if (line.rfind("VmHWM:", 0) == 0) {
      std::uint64_t kilobytes = 0;
      std::sscanf(line.c_str(), "VmHWM: %lu kB", &kilobytes);
      return kilobytes * 1024u;
    }
  }
  return 0;
}

inline std::uint64_t currentResidentBytes() {
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    if (line.rfind("VmRSS:", 0) == 0) {
      std::uint64_t kilobytes = 0;
      std::sscanf(line.c_str(), "VmRSS: %lu kB", &kilobytes);
      return kilobytes * 1024u;
    }
  }
  return 0;
}

inline double loadAverage() {
  std::ifstream loadavg("/proc/loadavg");
  double one = -1.0;
  loadavg >> one;
  return one;
}

// Collects the states a real game actually visits at the root, using the
// supplied decider to advance play through the unmodified frozen engine.
template <typename Decider>
inline void harvestRootStates(std::uint32_t seed, int maximum_moves,
                              Decider& decide, std::vector<State>& out) {
  requireLease(seed);
  State state = initialHeadlessState(seed);
  while (!state.game_over && state.moves_played < maximum_moves) {
    out.push_back(state);
    int column = decide(state);
    if (column < 0 || !isLegal(state.board, column)) {
      column = centerFirstMove(state.board);
      if (column < 0) break;
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, column, move)) break;
  }
}

// Expands a root exactly the way the search does -- same column order, same
// stratified chance samples, same canonicalisation -- and records every state
// the search would evaluate at the requested remaining depth.  With
// `record_depth == 0` this is precisely the distribution of leaf states.
inline void harvestSearchStates(const State& state, int depth, int strata,
                                int record_depth, std::uint32_t policy_seed,
                                std::vector<State>& out, std::size_t limit) {
  if (out.size() >= limit) return;
  if (depth == record_depth) {
    out.push_back(state);
    return;
  }
  if (depth == 0 || state.game_over) return;
  const std::uint32_t state_seed =
      cfpi::detail::scenarioSeedForState(state, policy_seed, depth);
  for (const int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(state.board, column)) continue;
    for (int sample = 0; sample < strata; ++sample) {
      if (out.size() >= limit) return;
      cfpi::detail::StratifiedRandom random{state_seed, sample, strata, 0};
      MoveResult move;
      if (!cfpi::detail::playMoveSampled(state, column, random, move)) continue;
      if (move.state.game_over) continue;
      move.state.score = 0;
      move.state.next_disc =
          cfpi::detail::sampledNextDisc(state_seed, sample, strata);
      bool ignored = false;
      const State next = cfpi::detail::canonicalState(move.state, ignored);
      harvestSearchStates(next, depth - 1, strata, record_depth, policy_seed,
                          out, limit);
    }
  }
}

}  // namespace drop7::fast
