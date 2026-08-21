#!/usr/bin/env bash
# Builds the leaf-reweight arm without modifying any existing repository source.
# clang++ only: the Makefile's `CXX ?= clang++` loses to GNU make's builtin
# CXX=g++, and g++ trips a false-positive -Werror=array-bounds inside
# src/core/native/public-behavior.hpp.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
# regenerates build/lifetime/fair-only-depth4-noentry.cpp and the source hashes
"${ROOT}/approaches/lifetime-objective/score-decomposition/build.sh" >/dev/null
REFERENCE="${ROOT}/approaches/fair-expectimax/reference"
OUT="${ROOT}/build/lifetime"
CXX="${CXX_OVERRIDE:-clang++}"
"${CXX}" -O3 -std=c++20 -pthread -Wall -Wextra \
  -I "${REFERENCE}" -I "${OUT}" \
  -o "${OUT}/leaf-reweight" \
  "${ROOT}/approaches/lifetime-objective/leaf-reweight/search.cpp"
echo "built ${OUT}/leaf-reweight with ${CXX}"
