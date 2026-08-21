#!/usr/bin/env bash
# Builds the learned-leaf search family into a namespaced build directory.
#
# Everything here is new; no existing repository source is modified.  As in
# approaches/lifetime-objective/score-decomposition/build.sh, the frozen
# depth-4 reference ends with a real `int main` and internally does
# `#undef main`, so a byte-identical copy with only that one entry-point line
# renamed is generated into this build tree and its hash recorded.
#
# clang++ is named explicitly.  The Makefile's `CXX ?= clang++` loses to GNU
# make's builtin CXX=g++, and g++ trips a false-positive -Werror=array-bounds
# in src/core/native/public-behavior.hpp.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="${ROOT}/approaches/lifetime-objective/learned-leaf"
REFERENCE="${ROOT}/approaches/fair-expectimax/reference"
OUT="${ROOT}/build/lifetime-leaf"
CXX="${CXX_OVERRIDE:-clang++}"

mkdir -p "${OUT}"

sha256sum "${REFERENCE}/fair-only-depth4.cpp" \
          "${REFERENCE}/fair-only-horizon.cpp" \
          "${ROOT}/src/core/native/public-behavior.hpp" \
          "${ROOT}/src/core/native/engine.hpp" \
          "${ROOT}/approaches/lifetime-objective/common/harness.hpp" \
          > "${OUT}/reference-sources.sha256"

sed 's/^int main(int argc, char\*\* argv) {$/int drop7_depth4_unused_entrypoint(int argc, char** argv) {/' \
    "${REFERENCE}/fair-only-depth4.cpp" > "${OUT}/fair-only-depth4-noentry.cpp"

DIFFCOUNT="$(diff "${REFERENCE}/fair-only-depth4.cpp" "${OUT}/fair-only-depth4-noentry.cpp" | grep -c '^[<>]' || true)"
if [ "${DIFFCOUNT}" != "2" ]; then
  echo "refusing to build: expected exactly one changed line, got ${DIFFCOUNT}/2" >&2
  exit 1
fi

FLAGS=(-O3 -std=c++20 -pthread -Wall -Wextra -march=native -I "${REFERENCE}" -I "${OUT}" -I "${HERE}")

for target in leaf-probe net-check leaf-check search; do
  if [ -f "${HERE}/${target}.cpp" ]; then
    "${CXX}" "${FLAGS[@]}" -o "${OUT}/${target}" "${HERE}/${target}.cpp"
    echo "built ${OUT}/${target} with ${CXX}"
  fi
done
