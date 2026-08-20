#!/usr/bin/env bash
# Builds the corrected-scoring (17,000-point) port of the 25-move / 7-scenario
# fair-D2 rollout veto.
#
# WHY A PORT AND NOT AN EDIT
# --------------------------
# approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto.cpp is a frozen
# historical source and is NOT modified.  It also cannot be compiled today:
#
#   $ clang++ -fsyntax-only -std=c++20 \
#         approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto.cpp
#   d4-d2-rollout-veto.cpp:90:15: error: static assertion failed due to
#       requirement 'kLevelBonus == 7000'
#      90 | static_assert(kLevelBonus == 7'000);
#   note: expression evaluates to '17000 == 7000'
#   1 error generated.
#
# COMPLETE LIST OF DIFFERENCES IN ../rollout-veto-17k/veto.cpp
# ------------------------------------------------------------
# Policy-affecting (2):
#   P1  static_assert(kLevelBonus == 7'000) -> == 17'000.
#       Justification: required to build at all; the engine defines 17,000 and
#       the assertion was the sole compile error.
#   P2  kMaximumRootQLoss (= "one canonical level bonus", 7,000 at the time)
#       becomes the --root-q-loss option, default 17,000.
#       Justification: the constant was written as static_cast<double>(
#       kLevelBonus); under corrected scoring the intended band is 17,000.
#       Both widths are measurable so the change is a parameter, not a silent
#       edit.
#
# Harness-only, policy-neutral (7):
#   H1  kMaximumMoves 1,000 -> --max-moves (default 2,000, the contract cap).
#   H2  kParallelism 4 -> --threads.
#   H3  hard-coded 0x3ded/0x3dee/0x3ebb/0x3ebc cohorts -> --seed-start /
#       --games, validated against lease SEEDLEASE-A51D-VETO.
#   H4  /tmp default output paths -> required --output; a /tmp path is refused.
#   H5  kRolloutHorizon 25 -> --horizon (default 25, max 25).  Scenario count
#       stays frozen at 7 because the paired t quantile t(0.975, df=6) is tied
#       to it.  The derived worst-case resource bounds are recomputed from the
#       runtime horizon instead of being constexpr.
#   H6  the four-stage fitting/heldout/screen/confirmation protocol, its gate
#       struct, its runtime-pause logic and its teacher-replay mode are
#       removed; this experiment is a single preregistered paired cohort.
#   H7  the bespoke game loop / summary / artifact writer is replaced by
#       approaches/lifetime-objective/common/harness.hpp (unmodified, included
#       as-is) so the score-decomposition identity is checked and the artifact
#       schema matches the other exploratory arms.
#
# Nothing else differs.  evaluateRollouts, playSyntheticMove, RevealTape,
# visibleDisc, fairDepthTwoAction, pairedReturnLower95, testAlternative and
# chooseAction are transcribed line for line.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
REFERENCE="${ROOT}/approaches/fair-expectimax/reference"
HERE="${ROOT}/approaches/lifetime-objective/rollout-veto-17k"
OUT="${ROOT}/build/lifetime/rollout-veto-17k"

# The Makefile's `CXX ?= clang++` is inert under GNU make (which predefines
# CXX=g++), and g++ rejects the reference sources with a false-positive
# -Werror=array-bounds.  clang++ is therefore explicit here.
CXX="${CXX_OVERRIDE:-clang++}"

mkdir -p "${OUT}"

sha256sum "${REFERENCE}/fair-only-depth4.cpp" \
          "${REFERENCE}/fair-only-horizon.cpp" \
          "${ROOT}/src/core/native/public-behavior.hpp" \
          "${ROOT}/src/core/native/engine.hpp" \
          "${ROOT}/approaches/lifetime-objective/common/harness.hpp" \
          "${ROOT}/approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto.cpp" \
          "${HERE}/veto.cpp" > "${OUT}/sources.sha256"

"${CXX}" -O3 -std=c++20 -pthread -Wall -Wextra \
  -I "${REFERENCE}" \
  -o "${OUT}/veto" \
  "${HERE}/veto.cpp"

echo "built ${OUT}/veto with ${CXX}"

# ---------------------------------------------------------------------------
# Differential parity reference.  Generates a build-tree copy of the frozen
# historical source in which EXACTLY ONE line differs (its 7,000-point
# assertion), refuses to continue if any other line changed, and builds a
# program that prints the same rollout digest as `veto --parity-dump`.
# The repository source itself is never written to.
# ---------------------------------------------------------------------------
ORIGINAL="${ROOT}/approaches/d4-long-outcome/rollout-veto/d4-d2-rollout-veto.cpp"
PATCHED="${OUT}/d4-d2-rollout-veto-17k-assert.cpp"

sed "s/^static_assert(kLevelBonus == 7'000);\$/static_assert(kLevelBonus == 17'000);/" \
    "${ORIGINAL}" > "${PATCHED}"

DIFFCOUNT="$(diff "${ORIGINAL}" "${PATCHED}" | grep -c '^[<>]' || true)"
if [ "${DIFFCOUNT}" != "2" ]; then
  echo "refusing to build parity reference: expected exactly one changed line, got ${DIFFCOUNT}/2" >&2
  exit 1
fi

"${CXX}" -O3 -std=c++20 -pthread -Wall -Wextra \
  -I "${REFERENCE}" -I "${OUT}" \
  -o "${OUT}/parity-original" \
  "${HERE}/parity-original.cpp"

echo "built ${OUT}/parity-original with ${CXX} (one line differs from the frozen source)"
