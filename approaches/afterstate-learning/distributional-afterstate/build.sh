#!/usr/bin/env bash
# Builds the afterstate-ranker pilot tools without modifying any existing
# repository source. Follows the same renamed-entrypoint pattern as
# approaches/lifetime-objective: the pinned fair-D4 reference is copied into
# the build tree with only its entry-point line renamed, the untouched
# original is hashed, and exactly one differing line is enforced.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="${ROOT}/approaches/afterstate-learning/distributional-afterstate"
REFERENCE="${ROOT}/approaches/fair-expectimax/reference"
OUT="${ROOT}/build/afterstate"
CXX="${CXX_OVERRIDE:-clang++}"

mkdir -p "${OUT}"

sha256sum "${REFERENCE}/fair-only-depth4.cpp" \
          "${REFERENCE}/fair-only-horizon.cpp" \
          "${ROOT}/src/core/native/public-behavior.hpp" \
          "${ROOT}/src/core/native/engine.hpp" \
          "${HERE}/common.hpp" \
          "${HERE}/generate-corpus.cpp" \
          "${HERE}/label-d4.cpp" \
          "${HERE}/self-test.cpp" > "${OUT}/sources.sha256"

sed 's/^int main(int argc, char\*\* argv) {$/int drop7_afterstate_unused_entrypoint(int argc, char** argv) {/' \
    "${REFERENCE}/fair-only-depth4.cpp" > "${OUT}/fair-only-depth4-noentry.cpp"

DIFFCOUNT="$(diff "${REFERENCE}/fair-only-depth4.cpp" "${OUT}/fair-only-depth4-noentry.cpp" | grep -c '^[<>]' || true)"
if [ "${DIFFCOUNT}" != "2" ]; then
  echo "refusing to build: expected exactly one changed line, got ${DIFFCOUNT}/2" >&2
  exit 1
fi

"${CXX}" -O3 -std=c++20 -pthread -Wall -Wextra \
  -o "${OUT}/self-test" \
  "${HERE}/self-test.cpp"

"${CXX}" -O3 -std=c++20 -pthread -Wall -Wextra \
  -o "${OUT}/generate-corpus" \
  "${HERE}/generate-corpus.cpp"

"${CXX}" -O3 -std=c++20 -pthread -Wall -Wextra \
  -I "${REFERENCE}" -I "${OUT}" \
  -o "${OUT}/label-d4" \
  "${HERE}/label-d4.cpp"

echo "built ${OUT}/{self-test,generate-corpus,label-d4} with ${CXX}"
