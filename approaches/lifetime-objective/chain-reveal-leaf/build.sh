#!/usr/bin/env bash
# Builds the chain-reveal-leaf gate and paired runner without modifying any
# existing repository file.
#
# The augmented search is GENERATED from the gated fast search:
# build/chain-reveal-leaf/augmented-search.hpp is approaches/lifetime-objective/
# fast-engine/fast-search.hpp with exactly the substitutions listed below, and
# the script refuses to build if the diff is anything other than those lines.
# The only behavioural change is the leaf call site, which now goes through
# augmentedFairLeaf (frozen leaf + memo + weighted extra terms) BELOW the
# search's work increment; everything else is the fast search that finding-13
# proved action-, work- and completed-depth-identical to the frozen reference.
#
# clang++ only, for the reason every sibling build script gives (audit-02 L1).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="${ROOT}/approaches/lifetime-objective/chain-reveal-leaf"
FAST="${ROOT}/approaches/lifetime-objective/fast-engine"
REFERENCE="${ROOT}/approaches/fair-expectimax/reference"
OUT="${ROOT}/build/chain-reveal-leaf"
CXX="${CXX_OVERRIDE:-clang++}"

mkdir -p "${OUT}"

# The fast engine's own build regenerates the no-entry reference copy it
# includes; reuse it rather than duplicating that logic.
"${FAST}/build.sh" gate-leaf >/dev/null

sha256sum "${FAST}/fast-engine.hpp" "${FAST}/fast-leaf.hpp" "${FAST}/fast-search.hpp" \
          "${REFERENCE}/fair-only-depth4.cpp" "${REFERENCE}/fair-only-horizon.cpp" \
          "${ROOT}/src/core/native/public-behavior.hpp" "${ROOT}/src/core/native/engine.hpp" \
          "${ROOT}/approaches/lifetime-objective/common/harness.hpp" \
          "${HERE}/extra-terms.hpp" "${HERE}/augmented-leaf.hpp" \
          "${HERE}/gate.cpp" "${HERE}/run.cpp" "${HERE}/corpus-dump.cpp" "${HERE}/corpus-gate.py" "${HERE}/compare.py" "${HERE}/screen.sh" > "${OUT}/sources.sha256"

sed -e 's|^#include "fast-leaf.hpp"$|#include "augmented-leaf.hpp"|' \
    -e 's|^namespace drop7::fast {$|namespace drop7::fastx {\nusing namespace drop7::fast;|' \
    -e 's|^class FastSearch {$|class AugmentedFastSearch {|' \
    -e 's|^  explicit FastSearch(FastSearchParameters parameters)$|  explicit AugmentedFastSearch(FastSearchParameters parameters, const ExtraWeights\& weights)|' \
    -e 's|^      : parameters_(parameters), table_(parameters.maximum_cache_entries) {}$|      : parameters_(parameters), table_(parameters.maximum_cache_entries), weights_(weights) {}|' \
    -e 's|^    const double value = fastFairLeaf(state, scratch_);$|    const double value = augmentedFairLeaf(state, scratch_, memo_, weights_);|' \
    -e 's|^  std::size_t tableBytes() const { return table_.slotBytes(); }$|  std::size_t tableBytes() const { return table_.slotBytes(); }\n  const AugmentedMemo\& leafMemo() const { return memo_; }\n  const ExtraWeights\& extraWeights() const { return weights_; }|' \
    -e 's|^  LeafScratch scratch_{};$|  LeafScratch scratch_{};\n  AugmentedMemo memo_{};\n  ExtraWeights weights_{};|' \
    -e 's|^}  // namespace drop7::fast$|}  // namespace drop7::fastx|' \
    "${FAST}/fast-search.hpp" > "${OUT}/augmented-search.hpp"

# Nine substitutions.  Seven change a line (7 removed + 7 added; the namespace
# opener also adds one more line); the accessor and member substitutions only
# add two lines each after an unchanged anchor: 7 removed + 12 added = 19.
DIFFCOUNT="$(diff "${FAST}/fast-search.hpp" "${OUT}/augmented-search.hpp" | grep -c '^[<>]' || true)"
if [ "${DIFFCOUNT}" != "19" ]; then
  echo "refusing to build: augmented-search.hpp differs from fast-search.hpp in ${DIFFCOUNT} lines, expected 19" >&2
  diff "${FAST}/fast-search.hpp" "${OUT}/augmented-search.hpp" >&2 || true
  exit 1
fi
for needle in "class AugmentedFastSearch" "augmentedFairLeaf(state, scratch_, memo_, weights_)" \
              "AugmentedMemo memo_{};" "ExtraWeights weights_{};" "weights_(weights) {}" \
              "namespace drop7::fastx"; do
  grep -q -F "${needle}" "${OUT}/augmented-search.hpp" || { echo "generated header lacks: ${needle}" >&2; exit 1; }
done
# The leaf call must sit directly below the work increment (audit-06
# reconciliation): the memo and the extra terms may not change logical work.
if ! awk '/\+\+work_;/{w=NR} /augmentedFairLeaf\(state, scratch_, memo_, weights_\)/{if (w && NR>w && NR-w<4) ok=1} END{exit ok?0:1}' "${OUT}/augmented-search.hpp"; then
  echo "refusing to build: the augmented leaf call is not directly below ++work_" >&2; exit 1
fi

# -ffp-contract=off pins the bit-exactness of the leaf: clang contracts a*b+c
# into FMA when the target has it (audit-06), and the dot product must not be.
# A no-op on the baseline x86-64 target; load-bearing if -march is ever added.
FLAGS=(-O3 -std=c++20 -pthread -Wall -Wextra -Werror -ffp-contract=off
       -I "${FAST}" -I "${HERE}" -I "${OUT}" -I "${REFERENCE}" -I "${ROOT}/build/fast-engine")

for program in gate run corpus-dump; do
  if [ "$#" -gt 0 ] && [ "$1" != "${program}" ]; then continue; fi
  echo "compiling ${program}..."
  "${CXX}" "${FLAGS[@]}" -o "${OUT}/${program}" "${HERE}/${program}.cpp"
done

echo "built into ${OUT} with ${CXX}"
