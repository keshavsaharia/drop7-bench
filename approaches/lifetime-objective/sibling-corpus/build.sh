#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
"${ROOT}/approaches/lifetime-objective/score-decomposition/build.sh" >/dev/null
"${CXX_OVERRIDE:-clang++}" -O3 -std=c++20 -pthread -Wall -Wextra \
  -I "${ROOT}/approaches/fair-expectimax/reference" -I "${ROOT}/build/lifetime" \
  -o "${ROOT}/build/lifetime/generate" \
  "${ROOT}/approaches/lifetime-objective/sibling-corpus/generate.cpp"
echo "built ${ROOT}/build/lifetime/generate"
