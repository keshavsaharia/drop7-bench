#!/usr/bin/env bash
# Builds the flow-ceiling tools without modifying any existing repository file.
#
# clang++ explicitly: the Makefile's `CXX ?= clang++` loses to GNU make's
# built-in `CXX=g++`, and g++ raises a false-positive -Werror=array-bounds
# inside src/core/native/public-behavior.hpp.
#
# `approaches/fair-expectimax/reference/fair-only-depth4.cpp` ends in a real
# `int main` and internally does `#undef main`, so it cannot simply be included.
# As in `scenario/build.sh` and `score-decomposition/build.sh`, a byte-identical
# copy with only that one entry-point line renamed is generated into this
# approach's own build directory, the untouched original's hash is recorded, and
# the generated copy is what gets compiled.  Nothing under `approaches/` outside
# this directory is written.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="${ROOT}/approaches/lifetime-objective/flow-ceiling"
SCENARIO="${ROOT}/approaches/lifetime-objective/scenario"
REFERENCE="${ROOT}/approaches/fair-expectimax/reference"
OUT="${ROOT}/build/flow-ceiling"
CXX="${CXX_OVERRIDE:-clang++}"

mkdir -p "${OUT}"

sha256sum "${REFERENCE}/fair-only-depth4.cpp" \
          "${REFERENCE}/fair-only-horizon.cpp" \
          "${ROOT}/src/core/native/public-behavior.hpp" \
          "${ROOT}/src/core/native/engine.hpp" \
          "${SCENARIO}/scenario.hpp" \
          "${SCENARIO}/scenario-io.hpp" \
          "${SCENARIO}/solver.hpp" \
          "${HERE}/flow-common.hpp" \
          "${HERE}/flow-solver.hpp" \
          "${HERE}/flow-run.cpp" \
          "${HERE}/pv-replay.cpp" > "${OUT}/sources.sha256"

sed 's/^int main(int argc, char\*\* argv) {$/int drop7_depth4_unused_entrypoint(int argc, char** argv) {/' \
    "${REFERENCE}/fair-only-depth4.cpp" > "${OUT}/fair-only-depth4-noentry.cpp"

# Exactly one line may differ, and it must be the entry point.
DIFFCOUNT="$(diff "${REFERENCE}/fair-only-depth4.cpp" "${OUT}/fair-only-depth4-noentry.cpp" | grep -c '^[<>]' || true)"
if [ "${DIFFCOUNT}" != "2" ]; then
  echo "refusing to build: expected exactly one changed line, got ${DIFFCOUNT}/2" >&2
  exit 1
fi

FLAGS=(-O3 -std=c++20 -pthread -Wall -Wextra)

"${CXX}" "${FLAGS[@]}" -I "${REFERENCE}" -I "${OUT}" -o "${OUT}/flow-run" "${HERE}/flow-run.cpp"
"${CXX}" "${FLAGS[@]}" -I "${REFERENCE}" -I "${OUT}" -o "${OUT}/pv-replay" "${HERE}/pv-replay.cpp"

echo "built ${OUT}/{flow-run,pv-replay} with ${CXX}"
