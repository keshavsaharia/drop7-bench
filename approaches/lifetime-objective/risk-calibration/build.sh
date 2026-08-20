#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
"${ROOT}/approaches/lifetime-objective/score-decomposition/build.sh" >/dev/null
REFERENCE="${ROOT}/approaches/fair-expectimax/reference"
OUT="${ROOT}/build/lifetime"
CXX="${CXX_OVERRIDE:-clang++}"
"${CXX}" -O3 -std=c++20 -pthread -Wall -Wextra \
  -I "${REFERENCE}" -I "${OUT}" \
  -o "${OUT}/risk-calibration" \
  "${ROOT}/approaches/lifetime-objective/risk-calibration/search.cpp"
echo "built ${OUT}/risk-calibration with ${CXX}"
