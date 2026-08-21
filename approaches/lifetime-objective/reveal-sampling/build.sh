#!/usr/bin/env bash
# Builds the factored-chance-node search without modifying any existing source.
#
# Two frozen sources end with a real int main and internally #undef main, so the
# usual include-with-renamed-entrypoint trick cannot suppress them.  As
# approaches/lifetime-objective/score-decomposition/build.sh already does, we
# generate byte-identical copies in a namespaced build tree with only that one
# entry-point line renamed, verify that exactly one line differs, and compile
# against the copies.  Nothing outside build/reveal-sampling/ is written.
#
# The oracle copy lives three directories below the repository root so that
# risk-calibration/search.cpp's relative include of
# ../../../approaches/lifetime-objective/common/harness.hpp still resolves.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
REFERENCE="${ROOT}/approaches/fair-expectimax/reference"
SINGLE="${ROOT}/approaches/lifetime-objective/risk-calibration"
OUT="${ROOT}/build/reveal-sampling"
CXX="${CXX_OVERRIDE:-clang++}"

mkdir -p "${OUT}/oracle"

sha256sum "${REFERENCE}/fair-only-depth4.cpp" \
          "${REFERENCE}/fair-only-horizon.cpp" \
          "${SINGLE}/search.cpp" \
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

"${CXX}" -O3 -std=c++20 -pthread -Wall -Wextra \
  -I "${REFERENCE}" -I "${OUT}" \
  -o "${OUT}/reveal-sampling" \
  "${ROOT}/approaches/lifetime-objective/reveal-sampling/search.cpp"

echo "built ${OUT}/reveal-sampling with ${CXX}"
