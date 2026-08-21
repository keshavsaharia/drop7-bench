#!/usr/bin/env bash
# Chunked, detached driver for the two arms finding-09 left unfinished.
#
# Each arm is run as four sequential 16-game chunks over consecutive blocks of
# the shared evaluation cohort 0xa51d1000-0xa51d103f.  Because runCohort maps
# game index i to seed seedStart+i, four chunks at seed starts +0x00, +0x10,
# +0x20, +0x30 play exactly the 64 cohort seeds, once each, and pooling them is
# statistically identical to one 64-game run while surviving an interruption.
#
# --max-work is passed explicitly per configuration (see --work-bound); a bound
# sized for a smaller branching factor silently completes a shallower depth.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
B="${ROOT}/build/reveal-sampling/reveal-sampling"
R="${ROOT}/runs/RUN-A525-reveal"
THREADS="${THREADS:-12}"
LOAD_CEILING="${LOAD_CEILING:-26}"

# The machine is shared with other agents.  Wait for the 1-minute load to fall
# to the ceiling, but only for a bounded time: an unbounded wait hands control
# of this arm to whoever else is running, and this arm has to finish.  After the
# grace period the chunk runs anyway at a reduced thread count, which is a
# smaller footprint than the one already agreed for an idle machine.
CHOSEN_THREADS="${THREADS}"

wait_for_load() {
  local waited=0
  CHOSEN_THREADS="${THREADS}"
  while :; do
    local one
    one="$(awk '{print int($1)}' /proc/loadavg)"
    if [ "${one}" -le "${LOAD_CEILING}" ]; then return 0; fi
    if [ "${waited}" -ge "${LOAD_GRACE:-900}" ]; then
      CHOSEN_THREADS="${CROWDED_THREADS:-8}"
      echo "$(date -Is) load ${one} > ${LOAD_CEILING} after ${waited}s, proceeding at ${CHOSEN_THREADS} threads" >&2
      return 0
    fi
    echo "$(date -Is) load ${one} > ${LOAD_CEILING}, backing off 120s" >&2
    sleep 120
    waited=$((waited + 120))
  done
}

run_arm() {
  local tag="$1" depth="$2" n="$3" m="$4" maxwork="$5"
  for chunk in 0 1 2 3; do
    local start out log
    printf -v start '0x%x' $((0xa51d1000 + chunk * 16))
    out="${R}/${tag}-c${chunk}.json"
    log="${R}/${tag}-c${chunk}.log"
    if [ -s "${out}" ]; then echo "$(date -Is) ${tag} chunk ${chunk} already present, skipping" >&2; continue; fi
    wait_for_load
    echo "$(date -Is) START ${tag} chunk ${chunk} seeds ${start}+16 threads ${CHOSEN_THREADS}" >&2
    "${B}" --depth "${depth}" --disc-samples "${n}" --reveal-samples "${m}" \
           --max-work "${maxwork}" --auto-cache \
           --seed-start "${start}" --games 16 --threads "${CHOSEN_THREADS}" \
           --output "${out}" > "${log}.stdout" 2> "${log}"
    echo "$(date -Is) DONE ${tag} chunk ${chunk} exit $?" >&2
  done
}

case "${1:-}" in
  arm1) run_arm d4-n7-m2 4 7 2 187336114 ;;
  arm2) run_arm d3-n7-m12 3 7 12 407634528 ;;
  *) echo "usage: run-arms.sh arm1|arm2" >&2; exit 2 ;;
esac
echo "$(date -Is) ARM COMPLETE ${1}" >&2
