#!/usr/bin/env bash
# Builds the planner-distillation tools without modifying any existing file.
#
# clang++ explicitly: the Makefile's `CXX ?= clang++` loses to GNU make's
# built-in `CXX=g++`, and g++ raises a false-positive -Werror=array-bounds
# inside src/core/native/public-behavior.hpp.
#
# `approaches/fair-expectimax/reference/fair-only-depth4.cpp` ends in a real
# `int main` and internally does `#undef main`, so it cannot simply be included.
# As in `scenario/build.sh`, `flow-ceiling/build.sh` and
# `score-decomposition/build.sh`, a byte-identical copy with only that one
# entry-point line renamed is generated into this approach's own build
# directory, the untouched original's hash is recorded, and the generated copy
# is what gets compiled.  Nothing under `approaches/` outside this directory is
# written.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="${ROOT}/approaches/lifetime-objective/planner-distill"
SCENARIO="${ROOT}/approaches/lifetime-objective/scenario"
FLOW="${ROOT}/approaches/lifetime-objective/flow-ceiling"
REFERENCE="${ROOT}/approaches/fair-expectimax/reference"
OUT="${ROOT}/build/planner-distill"
CXX="${CXX_OVERRIDE:-clang++}"

mkdir -p "${OUT}"

sha256sum "${REFERENCE}/fair-only-depth4.cpp" \
          "${REFERENCE}/fair-only-horizon.cpp" \
          "${ROOT}/src/core/native/public-behavior.hpp" \
          "${ROOT}/src/core/native/engine.hpp" \
          "${SCENARIO}/scenario.hpp" \
          "${SCENARIO}/scenario-io.hpp" \
          "${SCENARIO}/solver.hpp" \
          "${FLOW}/flow-common.hpp" \
          "${FLOW}/flow-solver.hpp" \
          "${FLOW}/fair-planner.hpp" \
          "${HERE}/corpus.hpp" \
          "${HERE}/corpus-gen.cpp" \
          "${HERE}/d4-rank.cpp" \
          "${HERE}/expand.cpp" \
          "${HERE}/fair-search.hpp" \
          "${HERE}/baseline.cpp" \
          "${HERE}/student.hpp" \
          "${HERE}/play.cpp" \
          "${HERE}/student-probe.cpp" \
          "${ROOT}/approaches/lifetime-objective/learned-leaf/leafnet.hpp" > "${OUT}/sources.sha256"

# Another contributor is editing `flow-ceiling/` and `scenario/` concurrently.
# A multi-hour corpus run must not be silently invalidated halfway through by an
# edit to a header it was compiled against, so the shared headers are SNAPSHOT
# into this approach's own build tree and compiled from there.  Their upstream
# hashes are recorded above; `sources.sha256` is the record of exactly which
# revision produced a given corpus.  Nothing upstream is written.
mkdir -p "${OUT}/pinned/flow-ceiling" "${OUT}/pinned/scenario" "${OUT}/pinned/learned-leaf"
cp "${ROOT}/approaches/lifetime-objective/learned-leaf/leafnet.hpp" "${OUT}/pinned/learned-leaf/"
cp "${FLOW}/flow-common.hpp" "${FLOW}/flow-solver.hpp" "${FLOW}/fair-planner.hpp" \
   "${OUT}/pinned/flow-ceiling/"
cp "${SCENARIO}/scenario.hpp" "${SCENARIO}/scenario-io.hpp" "${SCENARIO}/solver.hpp" \
   "${SCENARIO}/generate.hpp" "${OUT}/pinned/scenario/"
# The snapshot sits at a different depth from the original, so the one relative
# include that escapes the approach tree is rewritten to an absolute path.  The
# engine itself is never copied: the pinned headers point at the live,
# unmodified `src/core/native/`.
sed -i "s|\"../../../src/core/native/|\"${ROOT}/src/core/native/|" \
    "${OUT}/pinned/scenario/scenario.hpp"

sed 's/^int main(int argc, char\*\* argv) {$/int drop7_depth4_unused_entrypoint(int argc, char** argv) {/' \
    "${REFERENCE}/fair-only-depth4.cpp" > "${OUT}/fair-only-depth4-noentry.cpp"

# Exactly one line may differ, and it must be the entry point.
DIFFCOUNT="$(diff "${REFERENCE}/fair-only-depth4.cpp" "${OUT}/fair-only-depth4-noentry.cpp" | grep -c '^[<>]' || true)"
if [ "${DIFFCOUNT}" != "2" ]; then
  echo "refusing to build: expected exactly one changed line, got ${DIFFCOUNT}/2" >&2
  exit 1
fi

FLAGS=(-O3 -std=c++20 -pthread -Wall -Wextra)

"${CXX}" "${FLAGS[@]}" -I "${REFERENCE}" -I "${OUT}" \
    -o "${OUT}/corpus-gen" "${HERE}/corpus-gen.cpp"
"${CXX}" "${FLAGS[@]}" -I "${REFERENCE}" -I "${OUT}" \
    -o "${OUT}/d4-rank" "${HERE}/d4-rank.cpp"
"${CXX}" "${FLAGS[@]}" -I "${OUT}" -o "${OUT}/expand" "${HERE}/expand.cpp"
"${CXX}" "${FLAGS[@]}" -I "${REFERENCE}" -I "${OUT}" \
    -o "${OUT}/baseline" "${HERE}/baseline.cpp"
"${CXX}" "${FLAGS[@]}" -I "${REFERENCE}" -I "${OUT}" \
    -o "${OUT}/play" "${HERE}/play.cpp"
"${CXX}" "${FLAGS[@]}" -I "${OUT}" \
    -o "${OUT}/student-probe" "${HERE}/student-probe.cpp"

echo "built ${OUT}/{corpus-gen,d4-rank,expand,baseline,play,student-probe} with ${CXX}"
