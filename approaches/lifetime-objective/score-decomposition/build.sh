#!/usr/bin/env bash
# Builds the exploratory decomposition runner without modifying any existing
# repository source.
#
# approaches/fair-expectimax/reference/fair-only-depth4.cpp ends with a real
# int main and internally does "#undef main", so the usual include-with-renamed-
# entrypoint trick cannot suppress it.  Instead we generate a byte-identical
# copy in the build tree with only that one entry-point line renamed, record the
# hash of the untouched original, and compile against the generated copy.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
REFERENCE="${ROOT}/approaches/fair-expectimax/reference"
OUT="${ROOT}/build/lifetime"
CXX="${CXX_OVERRIDE:-clang++}"

mkdir -p "${OUT}"

sha256sum "${REFERENCE}/fair-only-depth4.cpp" \
          "${REFERENCE}/fair-only-horizon.cpp" \
          "${ROOT}/src/core/native/public-behavior.hpp" \
          "${ROOT}/src/core/native/engine.hpp" > "${OUT}/reference-sources.sha256"

sed 's/^int main(int argc, char\*\* argv) {$/int drop7_depth4_unused_entrypoint(int argc, char** argv) {/' \
    "${REFERENCE}/fair-only-depth4.cpp" > "${OUT}/fair-only-depth4-noentry.cpp"

# Exactly one line may differ, and it must be the entry point.
DIFFCOUNT="$(diff "${REFERENCE}/fair-only-depth4.cpp" "${OUT}/fair-only-depth4-noentry.cpp" | grep -c '^[<>]' || true)"
if [ "${DIFFCOUNT}" != "2" ]; then
  echo "refusing to build: expected exactly one changed line, got ${DIFFCOUNT}/2" >&2
  exit 1
fi

"${CXX}" -O3 -std=c++20 -pthread -Wall -Wextra \
  -I "${REFERENCE}" -I "${OUT}" \
  -o "${OUT}/decompose" \
  "${ROOT}/approaches/lifetime-objective/score-decomposition/decompose.cpp"

echo "built ${OUT}/decompose with ${CXX}"
