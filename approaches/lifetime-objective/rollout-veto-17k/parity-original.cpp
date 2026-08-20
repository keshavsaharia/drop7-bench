// Differential parity reference.
//
// Includes the UNMODIFIED historical source
//   approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto.cpp
// through a build-tree copy in which exactly one line is changed:
//   static_assert(kLevelBonus == 7'000);  ->  static_assert(kLevelBonus == 17'000);
// (see build.sh, which refuses to proceed if more than that one line differs).
//
// It then prints the same canonical rollout digest that `veto --parity-dump`
// prints.  If the two digests are byte-identical, the port reproduces the
// original policy exactly at the rollout level and the only behavioural
// differences are the ones listed in build.sh.

#define DROP7_D4_D2_ROLLOUT_VETO_LIBRARY
#include "d4-d2-rollout-veto-17k-assert.cpp"

#include <iomanip>
#include <iostream>

namespace original = drop7::d4_d2_rollout_veto;

int main() {
  using namespace drop7;
  constexpr std::uint32_t kParitySeed = 0xa51e'3f20u;
  constexpr int kParityHorizon = 6;
  constexpr int kParityStates = 10;

  std::cout << std::setprecision(17);
  State state = initialHeadlessState(kParitySeed);
  int emitted = 0;
  for (int move = 0; move < 400 && !state.game_over && emitted < kParityStates;
       ++move) {
    if (move >= 6 && move % 4 == 0) {
      const original::RolloutEvaluation r =
          original::evaluateRollouts(original::observable(state),
                                     kParityHorizon);
      for (int column = 0; column < kBoardSize; ++column) {
        std::cout << "S" << emitted << " C" << column << ' '
                  << (r.legal[column] ? 1 : 0) << ' '
                  << r.actions[column].mean_value << ' '
                  << r.actions[column].surviving_scenarios << ' '
                  << r.actions[column].mean_numbered_clears << '\n';
      }
      std::cout << "S" << emitted << " T " << r.legal_actions << ' '
                << r.synthetic_transitions << ' ' << r.continuation.calls << ' '
                << r.continuation.work << ' ' << r.continuation.nodes << '\n';
      ++emitted;
    }
    MoveResult result;
    const int column = original::fairDepthTwoAction(original::observable(state), nullptr);
    if (column < 0 || !playHeadlessMove(state, kParitySeed, column, result)) {
      break;
    }
  }
  return 0;
}
