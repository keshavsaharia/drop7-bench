#!/usr/bin/env bash
# Builds the leaf-evolution gate and evaluator without modifying any existing
# repository file.
#
# The weighted search is GENERATED from the gated fast search: build/leaf-
# evolution/weighted-search.hpp is approaches/lifetime-objective/fast-engine/
# fast-search.hpp with exactly the substitutions listed below, and the script
# refuses to build if the diff is anything other than those lines.  That keeps
# the equivalence argument auditable: everything about the search except the
# leaf's weight source is the fast search that finding-13 proved action-, work-
# and completed-depth-identical to the frozen reference.
#
# clang++ only, for the reason every sibling build script gives (audit-02 L1).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="${ROOT}/approaches/lifetime-objective/leaf-evolution"
FAST="${ROOT}/approaches/lifetime-objective/fast-engine"
REFERENCE="${ROOT}/approaches/fair-expectimax/reference"
OUT="${ROOT}/build/leaf-evolution"
CXX="${CXX_OVERRIDE:-clang++}"

mkdir -p "${OUT}"

# The fast engine's own build regenerates the no-entry reference copy it
# includes; reuse it rather than duplicating that logic.
"${FAST}/build.sh" gate-leaf >/dev/null

sha256sum "${FAST}/fast-engine.hpp" "${FAST}/fast-leaf.hpp" "${FAST}/fast-search.hpp" \
          "${REFERENCE}/fair-only-depth4.cpp" "${REFERENCE}/fair-only-horizon.cpp" \
          "${ROOT}/src/core/native/public-behavior.hpp" "${ROOT}/src/core/native/engine.hpp" \
          "${HERE}/weighted-leaf.hpp" > "${OUT}/sources.sha256"

sed -e 's|^#include "fast-leaf.hpp"$|#include "weighted-leaf.hpp"|' \
    -e 's|^namespace drop7::fast {$|namespace drop7::fastw {\nusing namespace drop7::fast;|' \
    -e 's|^class FastSearch {$|class WeightedFastSearch {|' \
    -e 's|^  explicit FastSearch(FastSearchParameters parameters)$|  explicit WeightedFastSearch(FastSearchParameters parameters, const LeafWeights\& weights)|' \
    -e 's|^      : parameters_(parameters), table_(parameters.maximum_cache_entries) {}$|      : parameters_(parameters), table_(parameters.maximum_cache_entries), weights_(weights) {}|' \
    -e 's|^    const double value = fastFairLeaf(state, scratch_);$|    const double value = weightedFairLeaf(state, scratch_, weights_);|' \
    -e 's|^  LeafScratch scratch_{};$|  LeafScratch scratch_{};\n  LeafWeights weights_{};|' \
    -e 's|^}  // namespace drop7::fast$|}  // namespace drop7::fastw|' \
    "${FAST}/fast-search.hpp" > "${OUT}/weighted-search.hpp"

# Eight substitutions; two only add a line after an unchanged anchor: 7 removed + 9 added.
DIFFCOUNT="$(diff "${FAST}/fast-search.hpp" "${OUT}/weighted-search.hpp" | grep -c '^[<>]' || true)"
if [ "${DIFFCOUNT}" != "16" ]; then
  echo "refusing to build: weighted-search.hpp differs from fast-search.hpp in ${DIFFCOUNT} lines, expected 16" >&2
  diff "${FAST}/fast-search.hpp" "${OUT}/weighted-search.hpp" >&2 || true
  exit 1
fi
for needle in "class WeightedFastSearch" "weightedFairLeaf(state, scratch_, weights_)" "LeafWeights weights_{};" "namespace drop7::fastw"; do
  grep -q -F "${needle}" "${OUT}/weighted-search.hpp" || { echo "generated header lacks: ${needle}" >&2; exit 1; }
done

FLAGS=(-O3 -std=c++20 -pthread -Wall -Wextra -Werror
       -I "${FAST}" -I "${HERE}" -I "${OUT}" -I "${REFERENCE}" -I "${ROOT}/build/fast-engine")

for program in gate evaluate decide; do
  if [ "$#" -gt 0 ] && [ "$1" != "${program}" ]; then continue; fi
  echo "compiling ${program}..."
  "${CXX}" "${FLAGS[@]}" -o "${OUT}/${program}" "${HERE}/${program}.cpp"
done

echo "built into ${OUT} with ${CXX}"
