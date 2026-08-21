#!/usr/bin/env bash
# Cheap wide sweep of leaf weight arms on the tuning lease.
#
# SEEDLEASE-A52-LEAFW = 0xa5278000-0xa527ffff.  Everything this script touches is
# inside that lease.  The shared evaluation cohort 0xa51d1000-0xa51d103f is never
# passed to this script.
#
# DEPTH IS AN ARGUMENT AND IS NOT A VALIDATED PROXY.  finding-05 shows the
# chance-strata effect is +101,171 at depth 4 and +7,276 at depth 3, so depth 3
# demonstrably fails to reproduce at least one depth-4 ordering.  Runs at depth 3
# here are a response-surface map, never a selection gate; selection happens at
# depth 4.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BIN="${ROOT}/build/lifetime/leaf-reweight"
OUT="${1:?output directory}"
DEPTH="${2:?depth}"
GAMES="${3:?games}"
THREADS="${4:?threads}"
SEED="${5:?seed start}"
mkdir -p "${OUT}"

run() {
  local arm="$1"; shift
  local file="${OUT}/${arm}.json"
  if [ -s "${file}" ]; then echo "skip ${arm}"; return; fi
  echo "=== ${arm}"
  "${BIN}" --arm "${arm}" --depth "${DEPTH}" --chance-samples 7 \
      --max-work 16000000 --seed-start "${SEED}" --games "${GAMES}" \
      --max-moves 2000 --threads "${THREADS}" --output "${file}" "$@" \
      2> "${OUT}/${arm}.log"
}

W="${ROOT}/approaches/lifetime-objective/leaf-reweight"

run frozen

# Tier 1, one constant each.  Anchors are the raw weight a same-scale fitted leaf
# would use (tier1.py), fitted on fair-play positions: roughness +562,
# solid_exposure +4,934, cracked_exposure +2,868, numbered_cells +1,048.
run t1-rough-140    --weight roughness=140
run t1-rough-280    --weight roughness=280
run t1-rough-560    --weight roughness=560
run t1-rough-1120   --weight roughness=1120
run t1-rough-2240   --weight roughness=2240
run t1-solidexp-1250  --weight solid_exposure=1250
run t1-solidexp-2500  --weight solid_exposure=2500
run t1-solidexp-4934  --weight solid_exposure=4934
run t1-solidexp-9868  --weight solid_exposure=9868
run t1-crackexp-1400  --weight cracked_exposure=1400
run t1-crackexp-2868  --weight cracked_exposure=2868
run t1-crackexp-5736  --weight cracked_exposure=5736
run t1-numbered-0     --weight numbered_cells=0
run t1-numbered-262   --weight numbered_cells=262
run t1-numbered-1048  --weight numbered_cells=1048
run t1-exposures      --weight solid_exposure=4934 --weight cracked_exposure=2868
run t1-combined-quarter --weight roughness=140 --weight solid_exposure=1234 \
                        --weight cracked_exposure=717 --weight numbered_cells=248
run t1-combined       --weight roughness=562 --weight solid_exposure=4934 \
                      --weight cracked_exposure=2868 --weight numbered_cells=1048

# Tier 2, the whole fitted direction, rescaled into leaf units and interpolated.
run t2-fair-a025  --weights "${W}/weights-refit-fair-achievableClears-a0p25.txt"
run t2-fair-a05   --weights "${W}/weights-refit-fair-achievableClears-a0p5.txt"
run t2-fair-a1    --weights "${W}/weights-refit-fair-achievableClears-a1.txt"
run t2-all-a025   --weights "${W}/weights-refit-all-achievableClears-a0p25.txt"
run t2-all-a05    --weights "${W}/weights-refit-all-achievableClears-a0p5.txt"
run t2-all-a1     --weights "${W}/weights-refit-all-achievableClears-a1.txt"

echo "sweep complete in ${OUT}"
