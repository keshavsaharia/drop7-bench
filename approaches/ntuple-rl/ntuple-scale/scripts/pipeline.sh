#!/usr/bin/env bash
# Stage driver for EX-20260905-ntuple-scale-tc-td-leaf-d3-535b2620.
#
# Every stage is launched through this script so the exact command, its
# wall/CPU/peak-RSS usage (scripts/with-rusage.py) and its stdout/stderr are
# retained under runs/<RUN_ID>/ntuple-scale/.  The seed blocks below are the
# frozen protocol's leases; only they may be read here.
#
#   SL-20260905T191317Z-09895ea2 (training):   0xa5300000-0xa54f0000, read in
#                                              order, wrapping (every training arm)
#   SL-20260905T191317Z-549265de (training):   0xa52f2240-0xa52f2280, the 64-game
#                                              validation block
#   SL-20260905T191317Z-c25f58cd (public-dev): 0xa52f2140-0xa52f2240, the 256-game
#                                              held-out screen, opened once
#
# DO NOT edit or replace this file while any stage is executing it.
#
# Usage: RUN_ID=RUN-... scripts/pipeline.sh <gates|pilot|select-arm|main|freeze|screen|compare|analyze|chain>
# Environment: THREADS (32), MAIN_MOVES (4000000000), MAIN_WALL (18000),
#              PILOT_MOVES (200000000), LAYOUT / ALPHA (main run; set by select-arm)
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

EXPERIMENT_ID="EX-20260905-ntuple-scale-tc-td-leaf-d3-535b2620"
TRAIN_START=0xa5300000
TRAIN_COUNT=2031616
VALIDATE_START=0xa52f2240
SCREEN_START=0xa52f2140
SCREEN_LEASE="research/seeds/leases/SL-20260905T191317Z-c25f58cd.json"
PILOT_MOVES="${PILOT_MOVES:-200000000}"
MAIN_MOVES="${MAIN_MOVES:-4000000000}"
MAIN_WALL="${MAIN_WALL:-18000}"

log() { echo "[$(date -u +%FT%TZ)] $*" | tee -a "$OUT/pipeline.log"; }
run() {
  local label="$1"; shift
  log "$label: $*"
  "$PY" "$HERE/with-rusage.py" --record "$RUSAGE" --label "$label" -- "$@"
}

# The six pilot arms: name|layout|alpha
PILOT_ARMS=(
  "A|rows,cols,win23,win32,phase=cols|1.0"
  "B|rows,cols,win23,win32,phase=none|1.0"
  "C|rows,cols,win23,win32,phase=all|1.0"
  "D|rows,cols,phase=cols|1.0"
  "E|win23,win32,phase=none|1.0"
  "F|rows,cols,win23,win32,phase=cols|0.25"
)

stage="${1:?stage required}"
case "$stage" in
  gates)
    run gates "$BIN/gate" --probe-start 0xa5277000 > "$OUT/gates.log" 2>&1
    grep -q "ALL GATES PASSED" "$OUT/gates.log" || { log "gates failed"; exit 2; }
    log "gates: all passed"
    ;;
  pilot)
    for arm in "${PILOT_ARMS[@]}"; do
      IFS='|' read -r name layout alpha <<< "$arm"
      dir="$OUT/pilot/$name"
      if [ -e "$dir/DONE" ]; then log "pilot $name: already done"; continue; fi
      mkdir -p "$dir"
      run "pilot-$name" "$BIN/train" --layout "$layout" --alpha "$alpha" \
        --seeds-start "$TRAIN_START" --seeds-count "$TRAIN_COUNT" \
        --moves "$PILOT_MOVES" --chunk-moves 10000000 --threads "$THREADS" \
        --validate-start "$VALIDATE_START" --validate-games 64 \
        --validate-every 50000000 --quick-every 25000000 \
        --no-checkpoint --experiment-id "$EXPERIMENT_ID" --out "$dir" \
        > "$dir/train.log" 2> "$dir/train.err"
      log "pilot $name: done ($(cat "$dir/best.json"))"
    done
    ;;
  select-arm)
    # Written through a temporary file: the analysis script reads the pilot
    # directory, and a redirection would create an empty selection.json
    # before the script runs (this aborted the first chain on 2026-09-05).
    "$PY" "$HERE/analyze.py" --run "$RUN_ID" --root "$ROOT" --select-arm > "$OUT/pilot/selection.json.tmp"
    mv "$OUT/pilot/selection.json.tmp" "$OUT/pilot/selection.json"
    log "select-arm: $(tr -d '\n' < "$OUT/pilot/selection.json" | cut -c1-200)"
    ;;
  main)
    sel="$OUT/pilot/selection.json"
    LAYOUT="${LAYOUT:-$("$PY" -c "import json;print(json.load(open('$sel'))['layout'])")}"
    ALPHA="${ALPHA:-$("$PY" -c "import json;print(json.load(open('$sel'))['alpha'])")}"
    mkdir -p "$OUT/main"
    resume=()
    if [ -e "$OUT/main/progress.jsonl" ]; then resume=(--resume); fi
    run main "$BIN/train" --layout "$LAYOUT" --alpha "$ALPHA" \
      --seeds-start "$TRAIN_START" --seeds-count "$TRAIN_COUNT" \
      --moves "$MAIN_MOVES" --chunk-moves 20000000 --threads "$THREADS" \
      --validate-start "$VALIDATE_START" --validate-games 64 \
      --validate-every 200000000 --quick-every 50000000 \
      --wall-seconds "$MAIN_WALL" --experiment-id "$EXPERIMENT_ID" \
      --out "$OUT/main" "${resume[@]}" \
      >> "$OUT/main/train.log" 2>> "$OUT/main/train.err"
    log "main: done ($(cat "$OUT/main/best.json"))"
    ;;
  freeze)
    test -s "$OUT/main/best-weights.bin" || { echo "no best-weights.bin"; exit 2; }
    sha256sum "$OUT/main/best-weights.bin" | tee "$OUT/main/candidate-weights.sha256"
    log "freeze: $(cat "$OUT/main/candidate-weights.sha256")"
    ;;
  screen)
    test -s "$OUT/main/candidate-weights.sha256" || { echo "candidate hash not recorded"; exit 2; }
    test ! -e "$OUT/screen/heldout.json" || { echo "screen artifact already exists; the block is one-shot"; exit 2; }
    log "screen: opening the held-out lease"
    "$PY" "$HERE/open-screen-lease.py" --root "$ROOT" --run "$RUN_ID" \
      --hash-file "$OUT/main/candidate-weights.sha256" \
      --lease "$SCREEN_LEASE" --experiment "research/experiments/$EXPERIMENT_ID.json" \
      --seeds-start "$SCREEN_START" | tee -a "$OUT/pipeline.log"
    mkdir -p "$OUT/screen"
    run screen "$BIN/screen" --candidate "$OUT/main/best-weights.bin" \
      --seeds-start "$SCREEN_START" --games 256 --threads "$THREADS" --move-cap 2000 \
      --out "$OUT/screen/heldout.json" --experiment-id "$EXPERIMENT_ID" \
      > "$OUT/screen.log" 2> "$OUT/screen.err"
    log "screen: done"
    ;;
  compare)
    CMP="$ROOT/approaches/lifetime-objective/leaf-evolution/compare.py"
    ART="$OUT/screen/heldout.json"
    "$PY" "$CMP" "$ART" --candidate candidate-d3s7 --reference fair-d3s7 --out "$OUT/screen/compare-candidate-vs-fair-d3s7.json"
    "$PY" "$CMP" "$ART" --candidate candidate-1ply --reference fair-d3s7 --out "$OUT/screen/compare-1ply-vs-fair-d3s7.json"
    "$PY" "$CMP" "$ART" --candidate candidate-d3s7 --reference fair-d4s7 --out "$OUT/screen/compare-candidate-vs-fair-d4s7.json"
    "$PY" "$CMP" "$ART" --candidate fair-d4s7 --reference fair-d3s7 --out "$OUT/screen/compare-fair-d4s7-vs-fair-d3s7.json"
    "$PY" "$CMP" "$ART" --candidate candidate-d3s7 --reference candidate-1ply --out "$OUT/screen/compare-candidate-vs-1ply.json"
    ;;
  analyze)
    "$PY" "$HERE/analyze.py" --run "$RUN_ID" --root "$ROOT" > /dev/null
    log "analyze: written $OUT/analysis.md"
    ;;
  chain)
    "$0" gates
    "$0" pilot
    "$0" select-arm
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
