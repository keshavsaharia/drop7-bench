#!/usr/bin/env bash
# Builds the memo gate without modifying any existing repository file.
# memo-search.hpp is GENERATED from the gated fast search by the substitutions
# below; the build refuses to proceed if the diff is anything other than those
# lines.  The only behavioural change is the leaf call site, which now goes
# through the one-entry memo BELOW the search's work increment.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="${ROOT}/approaches/lifetime-objective/fast-engine-memo"
FAST="${ROOT}/approaches/lifetime-objective/fast-engine"
REFERENCE="${ROOT}/approaches/fair-expectimax/reference"
OUT="${ROOT}/build/fast-engine-memo"
CXX="${CXX_OVERRIDE:-clang++}"
mkdir -p "${OUT}"
"${FAST}/build.sh" gate-leaf >/dev/null
sha256sum "${FAST}/fast-engine.hpp" "${FAST}/fast-leaf.hpp" "${FAST}/fast-search.hpp" "${HERE}/memo-leaf.hpp" > "${OUT}/sources.sha256"

sed -e 's|^#include "fast-leaf.hpp"$|#include "memo-leaf.hpp"|' \
    -e 's|^namespace drop7::fast {$|namespace drop7::fastm {\nusing namespace drop7::fast;|' \
    -e 's|^class FastSearch {$|class MemoSearch {|' \
    -e 's|^  explicit FastSearch(FastSearchParameters parameters)$|  explicit MemoSearch(FastSearchParameters parameters)|' \
    -e 's|^    const double value = fastFairLeaf(state, scratch_);$|    const double value = fastFairLeafMemo(state, scratch_, memo_);|' \
    -e 's|^  std::size_t tableBytes() const { return table_.slotBytes(); }$|  std::size_t tableBytes() const { return table_.slotBytes(); }\n  const LeafMemo\& leafMemo() const { return memo_; }|' \
    -e 's|^  LeafScratch scratch_{};$|  LeafScratch scratch_{};\n  LeafMemo memo_{};|' \
    -e 's|^}  // namespace drop7::fast$|}  // namespace drop7::fastm|' \
    "${FAST}/fast-search.hpp" > "${OUT}/memo-search.hpp"

DIFFCOUNT="$(diff "${FAST}/fast-search.hpp" "${OUT}/memo-search.hpp" | grep -c '^[<>]' || true)"
if [ "${DIFFCOUNT}" != "15" ]; then
  echo "refusing to build: memo-search.hpp differs from fast-search.hpp in ${DIFFCOUNT} lines, expected 15" >&2
  diff "${FAST}/fast-search.hpp" "${OUT}/memo-search.hpp" >&2 || true
  exit 1
fi
for needle in "class MemoSearch" "fastFairLeafMemo(state, scratch_, memo_)" "LeafMemo memo_{};" "namespace drop7::fastm"; do
  grep -q -F "${needle}" "${OUT}/memo-search.hpp" || { echo "generated header lacks: ${needle}" >&2; exit 1; }
done
# The memo call must sit below the work increment (audit-06 reconciliation).
if ! awk '/\+\+work_;/{w=NR} /fastFairLeafMemo\(state, scratch_, memo_\)/{if (w && NR>w && NR-w<4) ok=1} END{exit ok?0:1}' "${OUT}/memo-search.hpp"; then
  echo "refusing to build: the memo call is not directly below ++work_" >&2; exit 1
fi

# -ffp-contract=off pins bit-exactness (audit-06 section D); a no-op on baseline x86-64.
FLAGS=(-O3 -std=c++20 -pthread -Wall -Wextra -Werror -ffp-contract=off -I "${FAST}" -I "${HERE}" -I "${OUT}" -I "${REFERENCE}" -I "${ROOT}/build/fast-engine")
for program in gate; do
  if [ "$#" -gt 0 ] && [ "$1" != "${program}" ]; then continue; fi
  echo "compiling ${program}..."
  "${CXX}" "${FLAGS[@]}" -o "${OUT}/${program}" "${HERE}/${program}.cpp"
done
echo "built into ${OUT} with ${CXX}"
