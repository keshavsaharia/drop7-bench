#!/usr/bin/env bash
# Depth-4, seven-chance-strata evaluation of the leaf-reweight arms on the shared
# evaluation cohort 0xa51d1000-0xa51d103f (64 paired games).
#
# --max-work 16000000 is mandatory and is not a tuning choice.  finding-05 shows
# worst-case depth-4 work at seven strata is 11,892,398, so leaving the frozen
# 3,200,000 bound in place silently degrades this configuration to a completed
# depth 3 and has already produced one wrong conclusion in this programme.
#
# COMPARATOR.  The frozen-weight arm at this exact configuration is
# runs/RUN-A51D-s7confirm/fresh-s7.json, produced by
# approaches/lifetime-objective/risk-calibration on the same 64 ordered seeds
# with depth 4, seven strata and a 16,000,000 work bound.  This program's leaf is
# bit-identical to frozen::fairLeaf (936,612 boards, zero differing bits) and its
# driver spends identical work and picks identical columns at this exact
# configuration, so it produces that file's games exactly.  `frozen8` re-plays
# the first eight of them as a cross-binary check rather than paying ~4 hours to
# recompute all 64.
#
# Arms are ordered by information gain per unit of compute so that an interrupted
# run still answers the primary question.  The list and the weight values were
# fixed before any evaluation-cohort result was read, and every arm that runs is
# reported whether it wins or loses.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BIN="${ROOT}/build/lifetime/leaf-reweight"
W="${ROOT}/approaches/lifetime-objective/leaf-reweight"
OUT="${1:?output directory}"
THREADS="${2:-12}"
SEED="${3:-0xa51d1000}"
mkdir -p "${OUT}"

run() {
  local arm="$1"; local games="$2"; shift 2
  local file="${OUT}/${arm}.json"
  if [ -s "${file}" ]; then echo "skip ${arm}"; return; fi
  echo "=== ${arm} (${games} games) $(date +%H:%M:%S)"
  "${BIN}" --arm "${arm}" --depth 4 --chance-samples 7 --max-work 16000000 \
      --seed-start "${SEED}" --games "${games}" --max-moves 2000 \
      --threads "${THREADS}" --output "${file}" "$@" 2> "${OUT}/${arm}.log"
  echo "    done $(date +%H:%M:%S)"
}

# Cross-binary CHECK against fresh-s7.json on the first eight cohort games.
run frozen8 8

# Tier 1 bundled.  Values are the raw weight a same-scale fitted leaf would use
# for that term alone, fitted on fair-play positions only (tier1.py).
run t1-combined 64 --weight roughness=562 --weight solid_exposure=4934 \
                   --weight cracked_exposure=2868 --weight numbered_cells=1048

# Tier 2 - the whole fitted direction, sigma- and mean-matched to the frozen
# leaf, from the fair-play-only refit, half way and the whole way.
run t2-fair-a05 64 --weights "${W}/weights-refit-fair-achievableClears-a0p5.txt"
run t2-fair-a1  64 --weights "${W}/weights-refit-fair-achievableClears-a1.txt"

# Tier 1 attribution, cheapest-first by what the depth-3 map suggests matters.
run t1-exposures 64 --weight solid_exposure=4934 --weight cracked_exposure=2868
run t1-rough-560 64 --weight roughness=560
run t1-numbered  64 --weight numbered_cells=1048

echo "evaluation complete in ${OUT}"
