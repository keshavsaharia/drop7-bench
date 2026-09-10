#!/usr/bin/env bash
# Stage driver for the fill-conditioned leaf experiment
# (EX-20260906-ntuple-fill-conditioned-*).  Successor of scripts/pipeline3.sh,
# which stays as the frozen driver of the depth-4 screen; nothing here reads
# any earlier experiment's blocks.
#
# Stages, in order:
#   gates   CHECK gates on the already-open probe block, once per candidate
#           layout (occ5 and hgt5); no leased seed is read
#   edit    the two no-training edits of the first experiment's frozen tables
#           (zeroed, classmean; scripts/edit-tables.py), from the table file
#           and its checkpoint alone
#   train   three warm-started training arms in sequence under pilot/<arm>/:
#           control (the frozen tables' own layout, reloaded with fresh
#           coherence accumulators), occ5 and hgt5 (the frozen tables promoted
#           into the fill-conditioned layout); each validated on the 256-game
#           training-role block every 2e8 moves with a window-of-three plateau
#           rule from the sixth point, capped at ARM_MOVES / ARM_WALL
#   select  the fill arm with the larger best validation margin becomes the
#           candidate; the control arm's best point is the control candidate
#   freeze  SHA-256 of every table file the screen plays, and the CHECK gates
#           re-run on each of the four new files (gate --weights)
#   screen  the one-shot held-out screen, eleven arms on identical seeds
#   compare / analyze
#
#   probe (already open, SEEDLEASE-A52-FAST):  0xa5277000-0xa5278000, gates only
#   training lease:                            0xa5800000-0xa59f0000, read in
#                                              order, wrapping
#   validation lease (training role):          0xa52f2780-0xa52f2880, the
#                                              256-game validation block
#   screen lease (public-development):         0xa52f2880-0xa52f2a80, the
#                                              512-game held-out screen, opened
#                                              once
#
# DO NOT edit or replace this file while any stage is executing it.
#
# Usage: RUN_ID=RUN-... EXPERIMENT_ID=EX-... SCREEN_LEASE=research/seeds/leases/SL-....json \
#        scripts/pipeline4.sh <gates|edit|train|select|freeze|screen|compare|analyze|chain>
# Environment: THREADS (32), ARM_MOVES (2000000000), ARM_WALL (5400)
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
BASE_LAYOUT="rows,cols,win23,win32,phase=all"
ALPHA="1.0"
PROBE_START=0xa5277000
TRAIN_START=0xa5800000
TRAIN_COUNT=2031616
VALIDATE_START=0xa52f2780
VALIDATE_GAMES=256
SCREEN_START=0xa52f2880
SCREEN_GAMES=512
SCREEN_LEASE="${SCREEN_LEASE:?set SCREEN_LEASE to research/seeds/leases/SL-....json}"
ARM_MOVES="${ARM_MOVES:-2000000000}"
ARM_WALL="${ARM_WALL:-5400}"
# The first experiment's frozen candidate: the warm start of every training
# arm, the source of both edits, and the comparator of the screen.
PRIOR="$ROOT/runs/RUN-20260905T193006Z-4fbeb4e5/ntuple-scale/main/best-weights.bin"
PRIOR_SHA256="0ade9d4e4080ebdd52a1474b1a13410dc8dfb77f5eba24b078aa7703c92ace0b"
PRIOR_CHECKPOINT="$ROOT/runs/RUN-20260905T193006Z-4fbeb4e5/ntuple-scale/main/checkpoint.bin"
# The three training arms: name|layout
TRAIN_ARMS=(
  "control|$BASE_LAYOUT"
  "occ5|$BASE_LAYOUT,fill=occ5"
  "hgt5|$BASE_LAYOUT,fill=hgt5"
)

log() { echo "[$(date -u +%FT%TZ)] $*" | tee -a "$OUT/pipeline.log"; }
run() {
  local label="$1"; shift
  log "$label: $*"
  "$PY" "$HERE/with-rusage.py" --record "$RUSAGE" --label "$label" -- "$@"
}
verify_prior() {
  test -s "$PRIOR" || { echo "frozen tables missing: $PRIOR"; exit 2; }
  mkdir -p "$OUT/main"
  sha256sum "$PRIOR" | tee "$OUT/main/prior-weights.sha256"
  grep -q "^$PRIOR_SHA256 " "$OUT/main/prior-weights.sha256" || { log "frozen tables hash mismatch"; exit 2; }
}

stage="${1:?stage required}"
case "$stage" in
  gates)
    run gates-occ5 "$BIN/gate" --probe-start "$PROBE_START" --layout "$BASE_LAYOUT,fill=occ5" > "$OUT/gates.log" 2>&1
    grep -q "ALL GATES PASSED" "$OUT/gates.log" || { log "gates (occ5) failed"; exit 2; }
    run gates-hgt5 "$BIN/gate" --probe-start "$PROBE_START" --layout "$BASE_LAYOUT,fill=hgt5" > "$OUT/gates-hgt5.log" 2>&1
    grep -q "ALL GATES PASSED" "$OUT/gates-hgt5.log" || { log "gates (hgt5) failed"; exit 2; }
    log "gates: all passed for both fill layouts"
    ;;
  edit)
    if [ -e "$OUT/main/edits.json" ]; then log "edit: already done"; exit 0; fi
    verify_prior
    test -s "$PRIOR_CHECKPOINT" || { echo "checkpoint missing: $PRIOR_CHECKPOINT"; exit 2; }
    run edit "$PY" "$HERE/edit-tables.py" --source "$PRIOR" --checkpoint "$PRIOR_CHECKPOINT" --out-dir "$OUT/main" \
      > "$OUT/main/edit.log" 2> "$OUT/main/edit.err"
    log "edit: done ($(cat "$OUT/main/zeroed-weights.sha256" | cut -c1-16)..., $(cat "$OUT/main/classmean-weights.sha256" | cut -c1-16)...)"
    ;;
  train)
    verify_prior
    for arm in "${TRAIN_ARMS[@]}"; do
      IFS='|' read -r name layout <<< "$arm"
      dir="$OUT/pilot/$name"
      if [ -e "$dir/DONE" ]; then log "train $name: already done"; continue; fi
      mkdir -p "$dir"
      run "train-$name" "$BIN/train" --layout "$layout" --alpha "$ALPHA" --init-from "$PRIOR" \
        --seeds-start "$TRAIN_START" --seeds-count "$TRAIN_COUNT" \
        --moves "$ARM_MOVES" --chunk-moves 20000000 --threads "$THREADS" \
        --validate-start "$VALIDATE_START" --validate-games "$VALIDATE_GAMES" \
        --validate-every 200000000 --quick-every 100000000 \
        --plateau-window 3 --plateau-min-points 6 --no-checkpoint \
        --wall-seconds "$ARM_WALL" --experiment-id "$EXPERIMENT_ID" --out "$dir" \
        > "$dir/train.log" 2> "$dir/train.err"
      rm -f "$dir/latest-weights.bin"
      log "train $name: done ($(cat "$dir/stop.json"))"
    done
    ;;
  select)
    "$PY" "$HERE/analyze.py" --run "$RUN_ID" --root "$ROOT" --select-fill-arm | tee "$OUT/pilot/selection.json"
    log "select: $(tr -d '\n' < "$OUT/pilot/selection.json" | cut -c1-200)"
    ;;
  freeze)
    verify_prior
    test -s "$OUT/pilot/selection.json" || { echo "no selection.json"; exit 2; }
    candidate_arm="$("$PY" -c "import json,sys; print(json.load(open(sys.argv[1]))['candidateArm'])" "$OUT/pilot/selection.json")"
    for pair in "candidate:$OUT/pilot/$candidate_arm/best-weights.bin" "control:$OUT/pilot/control/best-weights.bin"; do
      IFS=':' read -r label file <<< "$pair"
      test -s "$file" || { echo "missing $file"; exit 2; }
      sha256sum "$file" | tee "$OUT/main/$label-weights.sha256"
    done
    test -s "$OUT/main/zeroed-weights.sha256" || { echo "edits not recorded"; exit 2; }
    test -s "$OUT/main/classmean-weights.sha256" || { echo "edits not recorded"; exit 2; }
    for pair in "candidate:$OUT/pilot/$candidate_arm/best-weights.bin" "control:$OUT/pilot/control/best-weights.bin" "zeroed:$OUT/main/zeroed-weights.bin" "classmean:$OUT/main/classmean-weights.bin"; do
      IFS=':' read -r label file <<< "$pair"
      run "gates-$label" "$BIN/gate" --probe-start "$PROBE_START" --weights "$file" > "$OUT/main/gates-$label.log" 2>&1
      grep -q "ALL GATES PASSED" "$OUT/main/gates-$label.log" || { log "gates on $label failed"; exit 2; }
    done
    log "freeze: candidate=$candidate_arm $(cut -c1-16 "$OUT/main/candidate-weights.sha256")..., control $(cut -c1-16 "$OUT/main/control-weights.sha256")..., gates passed on all four files"
    ;;
  screen)
    for f in candidate control zeroed classmean prior; do
      test -s "$OUT/main/$f-weights.sha256" || { echo "$f hash not recorded"; exit 2; }
    done
    grep -q "^$PRIOR_SHA256 " "$OUT/main/prior-weights.sha256" || { echo "tables hash mismatch"; exit 2; }
    test ! -e "$OUT/screen/heldout.json" || { echo "screen artifact already exists; the block is one-shot"; exit 2; }
    candidate_arm="$("$PY" -c "import json,sys; print(json.load(open(sys.argv[1]))['candidateArm'])" "$OUT/pilot/selection.json")"
    CANDIDATE="$OUT/pilot/$candidate_arm/best-weights.bin"
    CONTROL="$OUT/pilot/control/best-weights.bin"
    for f in "$CANDIDATE" "$CONTROL" "$OUT/main/zeroed-weights.bin" "$OUT/main/classmean-weights.bin"; do
      test -s "$f" || { echo "missing $f"; exit 2; }
    done
    log "screen: opening the held-out lease"
    "$PY" "$HERE/open-screen-lease.py" --root "$ROOT" --run "$RUN_ID" \
      --hash-file "$OUT/main/candidate-weights.sha256" \
      --lease "$SCREEN_LEASE" --experiment "research/experiments/$EXPERIMENT_ID.json" \
      --seeds-start "$SCREEN_START" | tee -a "$OUT/pipeline.log"
    mkdir -p "$OUT/screen"
    run screen "$BIN/screen" \
      --arm "prior=$PRIOR" --arm "fill=$CANDIDATE" \
      --arm-d3 "control=$CONTROL" --arm-d3 "zeroed=$OUT/main/zeroed-weights.bin" --arm-d3 "classmean=$OUT/main/classmean-weights.bin" \
      --arm-d4 "prior=$PRIOR" --arm-d4 "fill=$CANDIDATE" --arm-d4 "zeroed=$OUT/main/zeroed-weights.bin" --skip-d4 \
      --seeds-start "$SCREEN_START" --games "$SCREEN_GAMES" --threads "$THREADS" --move-cap 2000 \
      --out "$OUT/screen/heldout.json" --experiment-id "$EXPERIMENT_ID" \
      > "$OUT/screen.log" 2> "$OUT/screen.err"
    log "screen: done"
    ;;
  compare)
    CMP="$ROOT/approaches/lifetime-objective/leaf-evolution/compare.py"
    ART="$OUT/screen/heldout.json"
    for pair in "fill-d3s7:prior-d3s7" "fill-d3s7:control-d3s7" "control-d3s7:prior-d3s7" "zeroed-d3s7:prior-d3s7" "classmean-d3s7:prior-d3s7" \
                "fill-d4s7:prior-d4s7" "zeroed-d4s7:prior-d4s7" "fill-d4s7:fill-d3s7" "prior-d4s7:prior-d3s7" "zeroed-d4s7:zeroed-d3s7" \
                "prior-d3s7:fair-d3s7" "fill-d3s7:fair-d3s7" "control-d3s7:fair-d3s7" "zeroed-d3s7:fair-d3s7" "classmean-d3s7:fair-d3s7" \
                "fill-1ply:prior-1ply" "fill-1ply:fair-d3s7" "prior-1ply:fair-d3s7" "fill-d4s7:fair-d3s7"; do
      IFS=':' read -r cand ref <<< "$pair"
      "$PY" "$CMP" "$ART" --candidate "$cand" --reference "$ref" --out "$OUT/screen/compare-$cand-vs-$ref.json" || log "compare $cand vs $ref: skipped"
    done
    ;;
  analyze)
    "$PY" "$HERE/analyze.py" --run "$RUN_ID" --root "$ROOT" > /dev/null
    log "analyze: written $OUT/analysis.md"
    ;;
  chain)
    "$0" train
    "$0" select
    "$0" freeze
    "$0" screen
    "$0" compare
    "$0" analyze
    ;;
  *)
    echo "unknown stage $stage"; exit 2
    ;;
esac
