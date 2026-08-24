#!/usr/bin/env bash
# Builds the E-FAST-M6 gate without modifying any existing repository file.
#
# The equivalence comparator is the GENUINE native factored search
# (approaches/lifetime-objective/reveal-sampling/search.cpp).  As
# reveal-sampling/build.sh does, byte-identical copies are generated into this
# approach's namespaced build tree with only the entry-point line renamed; the
# reveal-sampling copy additionally gets `thread_local` on its five inline
# diagnostic atomics so gate.cpp can read a per-decision completed depth under
# game-level parallelism (each game runs wholly on one worker thread, and
# thread_local changes nothing single-threadedly).  The build refuses to
# proceed if any diff is anything other than exactly those lines.
#
# clang++ only (audit-02 L1: g++ trips a false-positive -Werror=array-bounds
# in public-behavior.hpp).  -ffp-contract=off pins bit-exactness of the leaf
# dot product (audit-06 section D); a no-op on baseline x86-64.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="${ROOT}/approaches/lifetime-objective/fast-reveal-sampling"
FAST="${ROOT}/approaches/lifetime-objective/fast-engine"
MEMO="${ROOT}/approaches/lifetime-objective/fast-engine-memo"
REVEAL="${ROOT}/approaches/lifetime-objective/reveal-sampling"
SINGLE="${ROOT}/approaches/lifetime-objective/risk-calibration"
REFERENCE="${ROOT}/approaches/fair-expectimax/reference"
OUT="${ROOT}/build/fast-reveal-sampling"
CXX="${CXX_OVERRIDE:-clang++}"

mkdir -p "${OUT}/oracle"

sha256sum "${REFERENCE}/fair-only-depth4.cpp" \
          "${REFERENCE}/fair-only-horizon.cpp" \
          "${SINGLE}/search.cpp" \
          "${REVEAL}/search.cpp" \
          "${FAST}/fast-engine.hpp" "${FAST}/fast-leaf.hpp" "${FAST}/fast-search.hpp" \
          "${MEMO}/memo-leaf.hpp" \
          "${HERE}/fast-factored-search.hpp" "${HERE}/gate.cpp" \
          "${ROOT}/approaches/lifetime-objective/common/harness.hpp" \
          "${ROOT}/src/core/native/public-behavior.hpp" \
          "${ROOT}/src/core/native/engine.hpp" > "${OUT}/sources.sha256"

rename_entry_point() {
  local source="$1" target="$2" name="$3"
  sed "s/^int main(int argc, char\*\* argv) {\$/int ${name}(int argc, char** argv) {/" \
      "${source}" > "${target}"
  local changed
  changed="$(diff "${source}" "${target}" | grep -c '^[<>]' || true)"
  if [ "${changed}" != "2" ]; then
    echo "refusing to build: expected exactly one changed line in ${source}, got ${changed}/2" >&2
    exit 1
  fi
}

rename_entry_point "${REFERENCE}/fair-only-depth4.cpp" \
                   "${OUT}/fair-only-depth4-noentry.cpp" \
                   "drop7_depth4_unused_entrypoint"
rename_entry_point "${SINGLE}/search.cpp" \
                   "${OUT}/oracle/risk-calibration-noentry.cpp" \
                   "drop7_risk_calibration_unused_entrypoint"

# reveal-sampling copy: entry-point rename plus thread_local on the five
# inline diagnostic atomics (gDecisions, gDecisionsBelowTarget,
# gWorkLimitEvents, gMinCompletedDepth, gMaxDecisionWork).  6 changed lines,
# so 12 diff lines.
sed -e 's/^int main(int argc, char\*\* argv) {$/int drop7_reveal_sampling_unused_entrypoint(int argc, char** argv) {/' \
    -e 's/^inline std::atomic</inline thread_local std::atomic</' \
    "${REVEAL}/search.cpp" > "${OUT}/reveal-sampling-noentry.cpp"
DIFFCOUNT="$(diff "${REVEAL}/search.cpp" "${OUT}/reveal-sampling-noentry.cpp" | grep -c '^[<>]' || true)"
if [ "${DIFFCOUNT}" != "12" ]; then
  echo "refusing to build: reveal-sampling-noentry.cpp differs in ${DIFFCOUNT} lines, expected 12" >&2
  diff "${REVEAL}/search.cpp" "${OUT}/reveal-sampling-noentry.cpp" >&2 || true
  exit 1
fi
if [ "$(grep -c '^inline thread_local std::atomic<' "${OUT}/reveal-sampling-noentry.cpp")" != "5" ]; then
  echo "refusing to build: expected exactly 5 thread_local diagnostic atomics" >&2
  exit 1
fi

FLAGS=(-O3 -std=c++20 -pthread -Wall -Wextra -Werror -ffp-contract=off
       -I "${REFERENCE}" -I "${OUT}")

echo "compiling gate..."
"${CXX}" "${FLAGS[@]}" -o "${OUT}/gate" "${HERE}/gate.cpp"

echo "built ${OUT}/gate with ${CXX}"
