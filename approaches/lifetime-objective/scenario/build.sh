#!/usr/bin/env bash
# Builds the scenario benchmark tools without modifying any existing
# repository source.
#
# Build with clang++ explicitly.  The Makefile's `CXX ?= clang++` is defeated by
# GNU make's built-in `CXX=g++` default, and g++ raises a false-positive
# -Werror=array-bounds inside src/core/native/public-behavior.hpp.
#
# approaches/fair-expectimax/reference/fair-only-depth4.cpp ends with a real
# int main and internally does "#undef main", so the usual include-with-renamed-
# entrypoint trick cannot suppress it.  As in
# approaches/lifetime-objective/score-decomposition/build.sh we generate a
# byte-identical copy in the build tree with only that one entry-point line
# renamed, record the hash of the untouched original, and compile against the
# generated copy.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="${ROOT}/approaches/lifetime-objective/scenario"
REFERENCE="${ROOT}/approaches/fair-expectimax/reference"
OUT="${ROOT}/build/scenario"
CXX="${CXX_OVERRIDE:-clang++}"

mkdir -p "${OUT}"

sha256sum "${REFERENCE}/fair-only-depth4.cpp" \
          "${REFERENCE}/fair-only-horizon.cpp" \
          "${ROOT}/src/core/native/public-behavior.hpp" \
          "${ROOT}/src/core/native/engine.hpp" \
          "${HERE}/scenario.hpp" \
          "${HERE}/scenario-io.hpp" \
          "${HERE}/solver.hpp" \
          "${HERE}/generate.hpp" > "${OUT}/sources.sha256"

sed 's/^int main(int argc, char\*\* argv) {$/int drop7_depth4_unused_entrypoint(int argc, char** argv) {/' \
    "${REFERENCE}/fair-only-depth4.cpp" > "${OUT}/fair-only-depth4-noentry.cpp"

# Exactly one line may differ, and it must be the entry point.
DIFFCOUNT="$(diff "${REFERENCE}/fair-only-depth4.cpp" "${OUT}/fair-only-depth4-noentry.cpp" | grep -c '^[<>]' || true)"
if [ "${DIFFCOUNT}" != "2" ]; then
  echo "refusing to build: expected exactly one changed line, got ${DIFFCOUNT}/2" >&2
  exit 1
fi

FLAGS=(-O3 -std=c++20 -pthread -Wall -Wextra)

"${CXX}" "${FLAGS[@]}" -o "${OUT}/scenario-parity" "${HERE}/scenario-parity.cpp"
"${CXX}" "${FLAGS[@]}" -o "${OUT}/solve" "${HERE}/solve.cpp"
"${CXX}" "${FLAGS[@]}" -I "${REFERENCE}" -I "${OUT}" \
  -o "${OUT}/mint" "${HERE}/mint.cpp"

echo "built ${OUT}/{scenario-parity,solve,mint} with ${CXX}"
