#pragma once

// The policy set used by the suite-validation work, and the seed-lease guard.
//
// Nothing here re-derives a search.  The six fair arms are
// `drop7::lifetime::risk::ParameterizedSearch` from
// `approaches/lifetime-objective/risk-calibration/search.cpp`, consumed
// unmodified through a build-tree copy whose only changed line is the entry
// point (see build.sh).  That class is the driver `finding-05` measured, and
// its CHECK gate proves that at (depth 4, 5 strata, work 3,200,000) it selects
// exactly the same column as the frozen reference `chooseDepth4Action` on every
// move.  The three weak baselines are the repository's own
// `drop7::centerFirstMove`, `drop7::scenario::lowestColumnPolicy`, and a
// uniform draw over the legal columns.
//
// Every policy here reads only `drop7::State` - the visible board, the visible
// next disc, moves until the next rise, and the terminal flag.  None of them can
// see `latent[]`, the disc tape, the scenario id, or the seed.  `posmode.cpp`
// asserts that structurally with an information-boundary gate rather than
// relying on this comment.

#include "risk-search-noentry.cpp"

#include "../scenario/generate.hpp"
#include "../scenario/scenario.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace drop7::suitevalidation {

using namespace drop7;
using namespace drop7::scenario;
namespace risk = drop7::lifetime::risk;

// ---------------------------------------------------------------------------
// Seed lease
// ---------------------------------------------------------------------------

// SEEDLEASE-A52-SUITE, a sub-range of the SEEDLEASE-A52 reserve recorded in
// docs/exploratory/lease-map.md.  Role: exploratory development diagnostic.
// Offsets are partitioned so concurrent tools inside this work never collide:
//   0x0000-0x007f  CHECK gates (parity, reflection, solver agreement)
//   0x0080         policy randomization stream for `random-legal`
//   0x0081-0x00ff  reserved for further gates
//   0x0100-0x01ff  position-mode tape streams, one lease seed per tape index
//   0x1000-0x1fff  structure-probe position generation
//   0x2000-0x2fff  structure-probe completion sampling (latent + tape)
//   0x3000-0x3fff  reserve
constexpr std::uint32_t kLeaseStart = 0xa525'8000u;
constexpr std::uint32_t kLeaseEnd = 0xa525'bfffu;

inline std::uint32_t leaseSeed(std::uint32_t offset) {
  const std::uint32_t seed = kLeaseStart + offset;
  if (seed > kLeaseEnd) {
    std::cerr << "seed lease SEEDLEASE-A52-SUITE exhausted at offset " << offset
              << "\n";
    std::exit(2);
  }
  return seed;
}

// splitmix64, used only to spread one lease seed deterministically over the
// positions of a cohort.  It is not a gameplay RNG: its output seeds a
// `Mulberry32`, which is the engine's own generator.
inline std::uint64_t splitmix64(std::uint64_t x) {
  x += 0x9e37'79b9'7f4a'7c15ull;
  x = (x ^ (x >> 30)) * 0xbf58'476d'1ce4'e5b9ull;
  x = (x ^ (x >> 27)) * 0x94d0'49bb'1331'11ebull;
  return x ^ (x >> 31);
}

inline std::uint32_t derivedSeed(std::uint32_t lease, std::uint64_t index) {
  return static_cast<std::uint32_t>(
      splitmix64((static_cast<std::uint64_t>(lease) << 32) ^ index));
}

// ---------------------------------------------------------------------------
// Policies
// ---------------------------------------------------------------------------

enum class PolicyKind { kFairSearch, kCenterFirst, kLowestColumn, kRandomLegal };

struct PolicySpec {
  std::string name;
  PolicyKind kind = PolicyKind::kFairSearch;
  int depth = 4;
  int chance_samples = 5;
  std::uint64_t maximum_work = 3'200'000;
};

// The six fair arms are exactly the configurations `finding-05` reports on the
// shared cohort 0xa51d1000-0xa51d103f, including its declared work bounds: the
// frozen 3,200,000 for the five-stratum arms and 16,000,000 for the seven-
// stratum arms, which `finding-05` shows is required or a seven-stratum depth-4
// run silently degrades to a completed depth 3.
inline std::vector<PolicySpec> knownPolicies() {
  return {
      {"d2s5", PolicyKind::kFairSearch, 2, 5, 3'200'000},
      {"d2s7", PolicyKind::kFairSearch, 2, 7, 16'000'000},
      {"d3s5", PolicyKind::kFairSearch, 3, 5, 3'200'000},
      {"d3s7", PolicyKind::kFairSearch, 3, 7, 16'000'000},
      {"d4s5", PolicyKind::kFairSearch, 4, 5, 3'200'000},
      {"d4s7", PolicyKind::kFairSearch, 4, 7, 16'000'000},
      {"center-first", PolicyKind::kCenterFirst},
      {"random-legal", PolicyKind::kRandomLegal},
      {"lowest-column", PolicyKind::kLowestColumn},
  };
}

inline const PolicySpec* findPolicy(const std::string& name) {
  static const std::vector<PolicySpec> all = knownPolicies();
  for (const PolicySpec& spec : all) {
    if (spec.name == name) return &spec;
  }
  return nullptr;
}

// One policy instance.  `chooseColumn` takes only a public `State`.
class Policy {
 public:
  explicit Policy(const PolicySpec& spec)
      : spec_(spec),
        search_(risk::SearchParameters{spec.depth, spec.chance_samples,
                                       drop7::fair_only_horizon::kTerminalUtility,
                                       spec.maximum_work, 60'000}) {}

  const PolicySpec& spec() const { return spec_; }

  // `rng` is the policy's own randomization stream and is used only by
  // `random-legal`.  It is seeded from the lease, never from the tape.
  int chooseColumn(const State& state, Mulberry32& rng, std::uint64_t& work) {
    if (state.game_over) return -1;
    switch (spec_.kind) {
      case PolicyKind::kCenterFirst:
        ++work;
        return centerFirstMove(state.board);
      case PolicyKind::kLowestColumn:
        ++work;
        return lowestColumnPolicy(state.board);
      case PolicyKind::kRandomLegal: {
        ++work;
        int legal_count = 0;
        const auto columns = legalColumns(state.board, legal_count);
        if (legal_count == 0) return -1;
        return columns[rng.nextBits() % static_cast<std::uint32_t>(legal_count)];
      }
      case PolicyKind::kFairSearch:
      default:
        return search_.chooseAction(state, work);
    }
  }

 private:
  PolicySpec spec_;
  risk::ParameterizedSearch search_;
};

}  // namespace drop7::suitevalidation
