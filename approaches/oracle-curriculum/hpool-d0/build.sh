#!/usr/bin/env bash
# Builds the stage-D0 binaries into build/hpool-d0 without modifying any
# existing repository file.  One source, two binaries:
#   d0-generate  (-DD0_GENERATE) links the privileged oracle planner;
#   d0-relabel   links no oracle code (gate.sh checks the symbol table).
# clang++ only, for the reason every sibling build script gives (audit-02 L1).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="${ROOT}/approaches/oracle-curriculum/hpool-d0"
OUT="${ROOT}/build/hpool-d0"
CXX="${CXX_OVERRIDE:-clang++}"

mkdir -p "${OUT}"

sha256sum "${HERE}/d0.cpp" "${HERE}/analyze.py" "${HERE}/gate.sh" "${HERE}/run.sh" \
          "${ROOT}/approaches/oracle-curriculum/topology/oracle-topology-audit.cpp" \
          "${ROOT}/approaches/fair-expectimax/reference/fair-only-depth4.cpp" \
          "${ROOT}/approaches/fair-expectimax/reference/fair-only-horizon.cpp" \
          "${ROOT}/src/core/native/public-behavior.hpp" \
          "${ROOT}/src/core/native/engine.hpp" > "${OUT}/sources.sha256"

FLAGS=(-O3 -std=c++20 -pthread -Wall -Wextra -Werror -ffp-contract=off)

echo "compiling d0-generate..."
"${CXX}" "${FLAGS[@]}" -DD0_GENERATE -o "${OUT}/d0-generate" "${HERE}/d0.cpp"
echo "compiling d0-relabel..."
"${CXX}" "${FLAGS[@]}" -o "${OUT}/d0-relabel" "${HERE}/d0.cpp"

echo "built into ${OUT} with ${CXX}"
