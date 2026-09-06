#!/usr/bin/env bash
# Stage driver for the fresh-block replication at larger scale
# (EX-20260906-ntuple-scale-replication-wide-plateau-*).  The successor of
# scripts/pipeline.sh, which stays as the frozen driver of the first
# experiment; nothing here reads that experiment's blocks.
#
# Every stage is launched through this script so the exact command, its
# wall/CPU/peak-RSS usage (scripts/with-rusage.py) and its stdout/stderr are
# retained under runs/<RUN_ID>/ntuple-scale/.  The seed blocks below are the
# frozen protocol's leases; only they may be read here, plus the already-open
# probe block for the CHECK gates and the throughput smoke run.
#
#   probe (already open, SEEDLEASE-A52-FAST):  0xa5277000-0xa5278000, gates and
#                                              the smoke run only
#   training lease:                            0xa5500000-0xa56f0000, read in
#                                              order, wrapping
#   validation lease (training role):          0xa52f2280-0xa52f2380, the
#                                              256-game validation block
#   screen lease (public-development):         0xa52f2380-0xa52f2580, the
#                                              512-game held-out screen, opened
#                                              once
#
# DO NOT edit or replace this file while any stage is executing it.
#
# Usage: RUN_ID=RUN-... scripts/pipeline2.sh <gates|smoke|main|freeze|screen|compare|analyze|chain>
# Environment: THREADS (32), MAIN_MOVES (40000000000), MAIN_WALL (43200)
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
LAYOUT="rows,cols,win23,win32,win24,win42,phase=all"
ALPHA="1.0"
PROBE_START=0xa5277000
PROBE_COUNT=4096
TRAIN_START=0xa5500000
TRAIN_COUNT=2031616
VALIDATE_START=0xa52f2280
VALIDATE_GAMES=256
SCREEN_START=0xa52f2380
SCREEN_GAMES=512
SCREEN_LEASE="${SCREEN_LEASE:?set SCREEN_LEASE to research/seeds/leases/SL-....json}"
MAIN_MOVES="${MAIN_MOVES:-40000000000}"
MAIN_WALL="${MAIN_WALL:-43200}"
# The first experiment's frozen candidate, carried as the replication arm.
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
    run gates "$BIN/gate" --probe-start "$PROBE_START" --layout "$LAYOUT" > "$OUT/gates.log" 2>&1
    grep -q "ALL GATES PASSED" "$OUT/gates.log" || { log "gates failed"; exit 2; }
    log "gates: all passed"
    ;;
  smoke)
    # Throughput and memory of the wide layout on the already-open probe
    # block: no validation, no checkpoint, tables discarded.
    if [ -e "$OUT/smoke/DONE" ]; then log "smoke: already done"; exit 0; fi
    mkdir -p "$OUT/smoke"
    run smoke "$BIN/train" --layout "$LAYOUT" --alpha "$ALPHA" \
      --seeds-start "$PROBE_START" --seeds-count "$PROBE_COUNT" \
      --moves 40000000 --chunk-moves 10000000 --threads "$THREADS" \
      --validate-every 0 --quick-every 0 --no-checkpoint \
      --experiment-id "$EXPERIMENT_ID" --out "$OUT/smoke" \
      > "$OUT/smoke/train.log" 2> "$OUT/smoke/train.err"
    rm -f "$OUT/smoke/latest-weights.bin"
    log "smoke: done ($(tail -1 "$OUT/smoke/progress.jsonl" | cut -c1-160))"
    ;;
  main)
    mkdir -p "$OUT/main"
    resume=()
    if [ -e "$OUT/main/progress.jsonl" ]; then resume=(--resume); fi
    run main "$BIN/train" --layout "$LAYOUT" --alpha "$ALPHA" \
      --seeds-start "$TRAIN_START" --seeds-count "$TRAIN_COUNT" \
      --moves "$MAIN_MOVES" --chunk-moves 50000000 --threads "$THREADS" \
      --validate-start "$VALIDATE_START" --validate-games "$VALIDATE_GAMES" \
      --validate-every 500000000 --quick-every 100000000 \
      --plateau-window 4 --plateau-min-points 8 --checkpoint-every 4 \
      --wall-seconds "$MAIN_WALL" --experiment-id "$EXPERIMENT_ID" \
      --out "$OUT/main" "${resume[@]}" \
      >> "$OUT/main/train.log" 2>> "$OUT/main/train.err"
    log "main: done ($(cat "$OUT/main/stop.json"))"
    ;;
  freeze)
    test -s "$OUT/main/best-weights.bin" || { echo "no best-weights.bin"; exit 2; }
    sha256sum "$OUT/main/best-weights.bin" | tee "$OUT/main/candidate-weights.sha256"
    log "freeze: $(cat "$OUT/main/candidate-weights.sha256")"
    test -s "$PRIOR" || { echo "prior tables missing: $PRIOR"; exit 2; }
    sha256sum "$PRIOR" | tee "$OUT/main/prior-weights.sha256"
    grep -q "^$PRIOR_SHA256 " "$OUT/main/prior-weights.sha256" || { log "prior tables hash mismatch"; exit 2; }
    log "freeze: prior tables verified ($PRIOR_SHA256)"
    ;;
  screen)
    test -s "$OUT/main/candidate-weights.sha256" || { echo "candidate hash not recorded"; exit 2; }
    test -s "$OUT/main/prior-weights.sha256" || { echo "prior hash not recorded"; exit 2; }
    test ! -e "$OUT/screen/heldout.json" || { echo "screen artifact already exists; the block is one-shot"; exit 2; }
    log "screen: opening the held-out lease"
    "$PY" "$HERE/open-screen-lease.py" --root "$ROOT" --run "$RUN_ID" \
      --hash-file "$OUT/main/candidate-weights.sha256" \
      --lease "$SCREEN_LEASE" --experiment "research/experiments/$EXPERIMENT_ID.json" \
      --seeds-start "$SCREEN_START" | tee -a "$OUT/pipeline.log"
    mkdir -p "$OUT/screen"
    run screen "$BIN/screen" --candidate "$OUT/main/best-weights.bin" --arm "prior=$PRIOR" \
      --seeds-start "$SCREEN_START" --games "$SCREEN_GAMES" --threads "$THREADS" --move-cap 2000 \
      --out "$OUT/screen/heldout.json" --experiment-id "$EXPERIMENT_ID" \
      > "$OUT/screen.log" 2> "$OUT/screen.err"
    log "screen: done"
    ;;
  compare)
    CMP="$ROOT/approaches/lifetime-objective/leaf-evolution/compare.py"
    ART="$OUT/screen/heldout.json"
    "$PY" "$CMP" "$ART" --candidate candidate-d3s7 --reference fair-d3s7 --out "$OUT/screen/compare-candidate-d3s7-vs-fair-d3s7.json"
    "$PY" "$CMP" "$ART" --candidate prior-d3s7 --reference fair-d3s7 --out "$OUT/screen/compare-prior-d3s7-vs-fair-d3s7.json"
    "$PY" "$CMP" "$ART" --candidate candidate-d3s7 --reference prior-d3s7 --out "$OUT/screen/compare-candidate-d3s7-vs-prior-d3s7.json"
    "$PY" "$CMP" "$ART" --candidate candidate-d3s7 --reference fair-d4s7 --out "$OUT/screen/compare-candidate-d3s7-vs-fair-d4s7.json"
    "$PY" "$CMP" "$ART" --candidate prior-d3s7 --reference fair-d4s7 --out "$OUT/screen/compare-prior-d3s7-vs-fair-d4s7.json"
    "$PY" "$CMP" "$ART" --candidate fair-d4s7 --reference fair-d3s7 --out "$OUT/screen/compare-fair-d4s7-vs-fair-d3s7.json"
    "$PY" "$CMP" "$ART" --candidate candidate-1ply --reference fair-d3s7 --out "$OUT/screen/compare-candidate-1ply-vs-fair-d3s7.json"
    "$PY" "$CMP" "$ART" --candidate candidate-d3s7 --reference candidate-1ply --out "$OUT/screen/compare-candidate-d3s7-vs-candidate-1ply.json"
    ;;
  analyze)
    "$PY" "$HERE/analyze.py" --run "$RUN_ID" --root "$ROOT" > /dev/null
    log "analyze: written $OUT/analysis.md"
    ;;
  chain)
    "$0" main
    "$0" freeze
    "$0" screen
    "$0" compare
    "$0" analyze
    ;;
  *)
    echo "unknown stage $stage"; exit 2
    ;;
esac
