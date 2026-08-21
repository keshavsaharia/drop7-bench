#!/usr/bin/env bash
# Builds the suite-validation tools.  Creates no file outside
# approaches/lifetime-objective/suite-validation/ and build/suite-validation/.
#
# clang++ explicitly: the Makefile's `CXX ?= clang++` loses to GNU make's
# builtin CXX=g++, and g++ raises a false -Werror=array-bounds inside
# src/core/native/public-behavior.hpp.
#
# Two frozen sources end in a real `int main`, so they are copied into the build
# tree with only that one line renamed and the diff is enforced to be exactly
# one line each:
#   approaches/fair-expectimax/reference/fair-only-depth4.cpp   (the comparator)
#   approaches/lifetime-objective/risk-calibration/search.cpp   (ParameterizedSearch)
# The generated copies live in build/suite-validation/gen/, which is exactly
# three directories below the repository root, so every `../../../` relative
# include inside them still resolves to the untouched original tree.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="${ROOT}/approaches/lifetime-objective/suite-validation"
REFERENCE="${ROOT}/approaches/fair-expectimax/reference"
RISK="${ROOT}/approaches/lifetime-objective/risk-calibration"
SCEN="${ROOT}/approaches/lifetime-objective/scenario"
OUT="${ROOT}/build/suite-validation"
GEN="${OUT}/gen"
CXX="${CXX_OVERRIDE:-clang++}"

mkdir -p "${GEN}"

sha256sum "${REFERENCE}/fair-only-depth4.cpp" \
          "${REFERENCE}/fair-only-horizon.cpp" \
          "${RISK}/search.cpp" \
          "${ROOT}/src/core/native/public-behavior.hpp" \
          "${ROOT}/src/core/native/engine.hpp" \
          "${SCEN}/scenario.hpp" \
          "${SCEN}/scenario-io.hpp" \
          "${SCEN}/solver.hpp" \
          "${SCEN}/generate.hpp" \
          "${HERE}"/*.hpp "${HERE}"/*.cpp > "${OUT}/sources.sha256"

rename_main() {
  local src="$1" dst="$2" newname="$3"
  sed "s/^int main(int argc, char\*\* argv) {\$/int ${newname}(int argc, char** argv) {/" "${src}" > "${dst}"
  local n
  n="$(diff "${src}" "${dst}" | grep -c '^[<>]' || true)"
  if [ "${n}" != "2" ]; then
    echo "refusing to build: expected exactly one changed line in ${src}, got ${n}/2" >&2
    exit 1
  fi
}

rename_main "${REFERENCE}/fair-only-depth4.cpp" "${GEN}/fair-only-depth4-noentry.cpp" \
            drop7_depth4_unused_entrypoint
rename_main "${RISK}/search.cpp" "${GEN}/risk-search-noentry.cpp" \
            drop7_risk_calibration_unused_entrypoint

FLAGS=(-O3 -std=c++20 -pthread -Wall -Wextra)
INCLUDES=(-I "${REFERENCE}" -I "${GEN}")

for target in posmode structure; do
  "${CXX}" "${FLAGS[@]}" "${INCLUDES[@]}" -o "${OUT}/${target}" "${HERE}/${target}.cpp"
done

echo "built ${OUT}/{posmode,structure} with ${CXX}"
