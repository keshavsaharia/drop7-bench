#!/usr/bin/env bash
# Builds the fast-engine profile, gate and benchmark programs.
#
# Modifies no existing repository file.  The frozen reference is compiled as a
# library by generating a byte-identical copy in this approach's own namespaced
# build directory with only the entry-point line renamed, exactly as
# approaches/lifetime-objective/score-decomposition/build.sh does -- but into
# build/fast-engine/ so that no other agent's build tree is touched.
#
# clang++ only.  The repository Makefile's `CXX ?= clang++` is inert under GNU
# make (built-in CXX=g++ has origin `default`), and g++ fails a false-positive
# -Werror=array-bounds in public-behavior.hpp:442.  See audit-02 L1.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="${ROOT}/approaches/lifetime-objective/fast-engine"
REFERENCE="${ROOT}/approaches/fair-expectimax/reference"
OUT="${ROOT}/build/fast-engine"
CXX="${CXX_OVERRIDE:-clang++}"

mkdir -p "${OUT}"

sha256sum "${REFERENCE}/fair-only-depth4.cpp" \
          "${REFERENCE}/fair-only-horizon.cpp" \
          "${ROOT}/src/core/native/public-behavior.hpp" \
          "${ROOT}/src/core/native/engine.hpp" > "${OUT}/reference-sources.sha256"

sed 's/^int main(int argc, char\*\* argv) {$/int drop7_depth4_unused_entrypoint(int argc, char** argv) {/' \
    "${REFERENCE}/fair-only-depth4.cpp" > "${OUT}/fair-only-depth4-noentry.cpp"

DIFFCOUNT="$(diff "${REFERENCE}/fair-only-depth4.cpp" "${OUT}/fair-only-depth4-noentry.cpp" | grep -c '^[<>]' || true)"
if [ "${DIFFCOUNT}" != "2" ]; then
  echo "refusing to build: expected exactly one changed line, got ${DIFFCOUNT}/2" >&2
  exit 1
fi

FLAGS=(-O3 -std=c++20 -pthread -Wall -Wextra -Werror
       -I "${REFERENCE}" -I "${OUT}")

for program in profile leafprofile gate-leaf gate-trajectory gate-search bench cohort; do
  if [ "$#" -gt 0 ] && [ "$1" != "${program}" ]; then continue; fi
  echo "compiling ${program}..."
  "${CXX}" "${FLAGS[@]}" -o "${OUT}/${program}" "${HERE}/${program}.cpp"
done

echo "built into ${OUT} with ${CXX}"
