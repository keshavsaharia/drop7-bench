#!/usr/bin/env bash
# Builds the survival-instinct gate and cohort runner without modifying any
# existing repository file.  The filtered search is GENERATED from the gated
# fast search (approaches/lifetime-objective/fast-engine/fast-search.hpp) by
# the substitutions below, and the build refuses to proceed if the diff is
# anything other than those lines: the only change is a root-column mask,
# mirrored with the canonicalised board, applied in rootDecision.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="${ROOT}/approaches/lifetime-objective/survival-instinct"
FAST="${ROOT}/approaches/lifetime-objective/fast-engine"
REFERENCE="${ROOT}/approaches/fair-expectimax/reference"
OUT="${ROOT}/build/survival-instinct"
CXX="${CXX_OVERRIDE:-clang++}"
mkdir -p "${OUT}"
"${FAST}/build.sh" gate-leaf >/dev/null
sha256sum "${FAST}/fast-engine.hpp" "${FAST}/fast-leaf.hpp" "${FAST}/fast-search.hpp" "${HERE}/filter.hpp" > "${OUT}/sources.sha256"

sed -e 's|^#include <vector>$|#include <vector>\n#include <array>|' \
    -e 's|^namespace drop7::fast {$|namespace drop7::fastf {\nusing namespace drop7::fast;|' \
    -e 's|^class FastSearch {$|class FilteredFastSearch {|' \
    -e 's|^  explicit FastSearch(FastSearchParameters parameters)$|  explicit FilteredFastSearch(FastSearchParameters parameters)|' \
    -e 's|^    const State canonical = canonicalStateFast(source, mirrored);$|    const State canonical = canonicalStateFast(source, mirrored);\n    for (int c = 0; c < kBoardSize; ++c) canonical_allowed_[static_cast<std::size_t>(c)] = root_allowed_[static_cast<std::size_t>(mirrored ? kBoardSize - 1 - c : c)];|' \
    -e 's|^  int requestedDepth() const { return parameters_.depth; }$|  int requestedDepth() const { return parameters_.depth; }\n  void setRootMask(const std::array<bool, kBoardSize>\& allowed) { root_allowed_ = allowed; }|' \
    -e 's|^      if (canonical.board\[static_cast<std::size_t>(column)\] != kEmpty) continue;$|      if (canonical.board[static_cast<std::size_t>(column)] != kEmpty \|\| !canonical_allowed_[static_cast<std::size_t>(column)]) continue;|' \
    -e 's|^  LeafScratch scratch_{};$|  LeafScratch scratch_{};\n  std::array<bool, kBoardSize> root_allowed_{{true, true, true, true, true, true, true}};\n  std::array<bool, kBoardSize> canonical_allowed_{{true, true, true, true, true, true, true}};|' \
    -e 's|^}  // namespace drop7::fast$|}  // namespace drop7::fastf|' \
    "${FAST}/fast-search.hpp" > "${OUT}/filtered-search.hpp"

DIFFCOUNT="$(diff "${FAST}/fast-search.hpp" "${OUT}/filtered-search.hpp" | grep -c '^[<>]' || true)"
if [ "${DIFFCOUNT}" != "16" ]; then
  echo "refusing to build: filtered-search.hpp differs from fast-search.hpp in ${DIFFCOUNT} lines, expected 16" >&2
  diff "${FAST}/fast-search.hpp" "${OUT}/filtered-search.hpp" >&2 || true
  exit 1
fi
for needle in "class FilteredFastSearch" "setRootMask" "canonical_allowed_[static_cast<std::size_t>(column)]) continue;" "namespace drop7::fastf"; do
  grep -q -F "${needle}" "${OUT}/filtered-search.hpp" || { echo "generated header lacks: ${needle}" >&2; exit 1; }
done

FLAGS=(-O3 -std=c++20 -pthread -Wall -Wextra -Werror -I "${FAST}" -I "${HERE}" -I "${OUT}" -I "${REFERENCE}" -I "${ROOT}/build/fast-engine")
for program in gate run; do
  if [ "$#" -gt 0 ] && [ "$1" != "${program}" ]; then continue; fi
  echo "compiling ${program}..."
  "${CXX}" "${FLAGS[@]}" -o "${OUT}/${program}" "${HERE}/${program}.cpp"
done
echo "built into ${OUT} with ${CXX}"
