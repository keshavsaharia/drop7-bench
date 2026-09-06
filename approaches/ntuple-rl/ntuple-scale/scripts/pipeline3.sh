#!/usr/bin/env bash
# Stage driver for the depth-4 screen of the frozen tables
# (EX-20260906-ntuple-scale-depth4-frozen-tables-*).  Successor of
# scripts/pipeline2.sh, which stays as the frozen driver of the replication;
# nothing here trains, and nothing here reads either earlier experiment's
# blocks.  The only gameplay is one held-out screen of four arms on identical
# seeds: the first experiment's frozen tables as the leaf of the reference
# depth-4 search (prior-d4s7, the candidate) and of the deployment depth-3
# search (prior-d3s7, the comparator), and the frozen fair leaf in both
# searches (fair-d3s7, fair-d4s7).
#
# Every stage is launched through this script so the exact command, its
# wall/CPU/peak-RSS usage (scripts/with-rusage.py) and its stdout/stderr are
# retained under runs/<RUN_ID>/ntuple-scale/.
#
#   probe (already open, SEEDLEASE-A52-FAST):  0xa5277000-0xa5278000, the CHECK
#                                              gates only
#   screen lease (public-development):         0xa52f2580-0xa52f2780, the
#                                              512-game held-out screen, opened
#                                              once
#
# DO NOT edit or replace this file while any stage is executing it.
#
# Usage: RUN_ID=RUN-... EXPERIMENT_ID=EX-... SCREEN_LEASE=research/seeds/leases/SL-....json \
#        scripts/pipeline3.sh <gates|freeze|screen|compare|analyze|chain>
# Environment: THREADS (32)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APPROACH="$(cd "$HERE/.." && pwd)"
ROOT="$(cd "$APPROACH/../../.." && pwd)"
BIN="$APPROACH/target/release"
RUN_ID="${RUN_ID:?set RUN_ID to the research run record id}"
OUT="$ROOT/runs/$RUN_ID/ntuple-scale"
THREADS="${THREADS:-32}"
PY="${PYTHON:-/usr/bin/python3}"
RUSAGE="$OUT/rusage.jsonl"
mkdir -p "$OUT"

EXPERIMENT_ID="${EXPERIMENT_ID:?set EXPERIMENT_ID to the frozen experiment record id}"
PROBE_START=0xa5277000
SCREEN_START=0xa52f2580
SCREEN_GAMES=512
SCREEN_LEASE="${SCREEN_LEASE:?set SCREEN_LEASE to research/seeds/leases/SL-....json}"
# The first experiment's frozen candidate: the only tables this experiment plays.
PRIOR="$ROOT/runs/RUN-20260905T193006Z-4fbeb4e5/ntuple-scale/main/best-weights.bin"
PRIOR_SHA256="0ade9d4e4080ebdd52a1474b1a13410dc8dfb77f5eba24b078aa7703c92ace0b"

log() { echo "[$(date -u +%FT%TZ)] $*" | tee -a "$OUT/pipeline.log"; }
run() {
  local label="$1"; shift
  log "$label: $*"
  "$PY" "$HERE/with-rusage.py" --record "$RUSAGE" --label "$label" -- "$@"
}

stage="${1:?stage required}"
case "$stage" in
  gates)
    test -s "$PRIOR" || { echo "frozen tables missing: $PRIOR"; exit 2; }
    run gates "$BIN/gate" --probe-start "$PROBE_START" --weights "$PRIOR" > "$OUT/gates.log" 2>&1
    grep -q "ALL GATES PASSED" "$OUT/gates.log" || { log "gates failed"; exit 2; }
    log "gates: all passed"
    ;;
  freeze)
    # Nothing is trained: the candidate and the prior are the same frozen
    # file, hashed and verified against the recorded value before the lease
    # opens.  Both names are written so the shared analysis and web tooling
    # find the hash where they expect it.
    mkdir -p "$OUT/main"
    test -s "$PRIOR" || { echo "frozen tables missing: $PRIOR"; exit 2; }
    sha256sum "$PRIOR" | tee "$OUT/main/prior-weights.sha256"
    grep -q "^$PRIOR_SHA256 " "$OUT/main/prior-weights.sha256" || { log "frozen tables hash mismatch"; exit 2; }
    cp "$OUT/main/prior-weights.sha256" "$OUT/main/candidate-weights.sha256"
    log "freeze: frozen tables verified ($PRIOR_SHA256)"
    ;;
  screen)
    test -s "$OUT/main/candidate-weights.sha256" || { echo "tables hash not recorded"; exit 2; }
    grep -q "^$PRIOR_SHA256 " "$OUT/main/prior-weights.sha256" || { echo "tables hash mismatch"; exit 2; }
    test ! -e "$OUT/screen/heldout.json" || { echo "screen artifact already exists; the block is one-shot"; exit 2; }
    log "screen: opening the held-out lease"
    "$PY" "$HERE/open-screen-lease.py" --root "$ROOT" --run "$RUN_ID" \
      --hash-file "$OUT/main/candidate-weights.sha256" \
      --lease "$SCREEN_LEASE" --experiment "research/experiments/$EXPERIMENT_ID.json" \
      --seeds-start "$SCREEN_START" | tee -a "$OUT/pipeline.log"
    mkdir -p "$OUT/screen"
    run screen "$BIN/screen" --arm "prior=$PRIOR" --arm-d4 "prior=$PRIOR" --skip-direct \
      --seeds-start "$SCREEN_START" --games "$SCREEN_GAMES" --threads "$THREADS" --move-cap 2000 \
      --out "$OUT/screen/heldout.json" --experiment-id "$EXPERIMENT_ID" \
      > "$OUT/screen.log" 2> "$OUT/screen.err"
    log "screen: done"
    ;;
  compare)
    CMP="$ROOT/approaches/lifetime-objective/leaf-evolution/compare.py"
    ART="$OUT/screen/heldout.json"
    "$PY" "$CMP" "$ART" --candidate prior-d4s7 --reference prior-d3s7 --out "$OUT/screen/compare-prior-d4s7-vs-prior-d3s7.json"
    "$PY" "$CMP" "$ART" --candidate prior-d4s7 --reference fair-d4s7 --out "$OUT/screen/compare-prior-d4s7-vs-fair-d4s7.json"
    "$PY" "$CMP" "$ART" --candidate prior-d4s7 --reference fair-d3s7 --out "$OUT/screen/compare-prior-d4s7-vs-fair-d3s7.json"
    "$PY" "$CMP" "$ART" --candidate fair-d4s7 --reference fair-d3s7 --out "$OUT/screen/compare-fair-d4s7-vs-fair-d3s7.json"
    "$PY" "$CMP" "$ART" --candidate prior-d3s7 --reference fair-d3s7 --out "$OUT/screen/compare-prior-d3s7-vs-fair-d3s7.json"
    "$PY" "$CMP" "$ART" --candidate prior-d3s7 --reference fair-d4s7 --out "$OUT/screen/compare-prior-d3s7-vs-fair-d4s7.json"
    ;;
  analyze)
    "$PY" "$HERE/analyze.py" --run "$RUN_ID" --root "$ROOT" > /dev/null
    log "analyze: written $OUT/analysis.md"
    ;;
  chain)
    "$0" screen
    "$0" compare
    "$0" analyze
    ;;
  *)
    echo "unknown stage $stage"; exit 2
    ;;
esac
