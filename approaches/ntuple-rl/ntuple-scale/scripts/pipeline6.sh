#!/usr/bin/env bash
# Stage driver for the gentle-step TreeStrap experiment
# (EX-20260907-ntuple-treestrap-gentle-*).  Successor of scripts/pipeline5.sh,
# which stays as the frozen driver of the first TreeStrap experiment (alpha
# 1.0 with fresh accumulators, where both arms fell below the warm start at
# once); nothing here reads any earlier experiment's blocks.  Three arms at a
# gentle step size (alpha 0.05 and 0.2 for TreeStrap, 0.05 for the
# visited-states ablation), a 2,048-game depth-3 screen with the depth-4 arms
# on its first 512 seeds.
#
# Stages, in order:
#   gates   CHECK gates on the already-open probe block, on the frozen tables
#           (gate --weights: includes the training-search-vs-engine gate)
#   train   three warm-started search-actor arms in sequence under pilot/<arm>/:
#           treestrap05 and treestrap20 (every internal node of the search
#           tree trained toward its own backup, alpha 0.05 and 0.2) and
#           searchtd05 (visited afterstates only, alpha 0.05); each validated
#           on the 256-game training-role block at point 0 (the warm start)
#           and every 3e5 visited moves, with a window-of-three plateau rule
#           from the sixth training point, capped at ARM_MOVES / ARM_WALL
#   select  the TreeStrap arm with the larger best validation margin is the
#           candidate (ties to treestrap05); the other is screened at depth 3
#           as treestrapalt; searchtd05 is the ablation
#   freeze  SHA-256 of every table file the screen plays, and the CHECK gates
#           re-run on each of the three new files
#   screen  the one-shot held-out screen: depth-3 arms on 2,048 seeds, the
#           depth-4 arms on the first 512 of them
#   compare / analyze
#
#   probe (already open, SEEDLEASE-A52-FAST):  0xa5277000-0xa5278000, gates only
#   training lease:                            0xa5c00000-0xa5df0000, read in
#                                              order
#   validation lease (training role):          0xa52f2d80-0xa52f2e80, the
#                                              256-game validation block
#   screen lease (public-development):         0xa52f2e80-0xa52f3680, the
#                                              2,048-game held-out screen,
#                                              opened once
#
# DO NOT edit or replace this file while any stage is executing it.
#
# Usage: RUN_ID=RUN-... EXPERIMENT_ID=EX-... SCREEN_LEASE=research/seeds/leases/SL-....json \
#        scripts/pipeline6.sh <gates|train|select|freeze|screen|compare|analyze|chain>
# Environment: THREADS (32), ARM_MOVES (6000000), ARM_WALL (5400)
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
LAYOUT="rows,cols,win23,win32,phase=all"
PROBE_START=0xa5277000
TRAIN_START=0xa5c00000
TRAIN_COUNT=2031616
VALIDATE_START=0xa52f2d80
VALIDATE_GAMES=256
SCREEN_START=0xa52f2e80
SCREEN_GAMES=2048
SCREEN_GAMES_D4=512
SCREEN_LEASE="${SCREEN_LEASE:?set SCREEN_LEASE to research/seeds/leases/SL-....json}"
ARM_MOVES="${ARM_MOVES:-6000000}"
ARM_WALL="${ARM_WALL:-5400}"
# The first experiment's frozen candidate: the warm start of both arms and
# the comparator of the screen.
PRIOR="$ROOT/runs/RUN-20260905T193006Z-4fbeb4e5/ntuple-scale/main/best-weights.bin"
PRIOR_SHA256="0ade9d4e4080ebdd52a1474b1a13410dc8dfb77f5eba24b078aa7703c92ace0b"
# The fill-conditioned experiment's control continuation (one-ply TD from the
# same warm start, best validation point): the one-ply continuation arm.
CONTROL="$ROOT/runs/RUN-20260906T201104Z-a96ea6c8/ntuple-scale/pilot/control/best-weights.bin"
CONTROL_SHA256="92dd1cb2d2a74b026270606c18c5f0d6e4f4ccc74e64c3c4e0f0d2043cdddd90"
# The three training arms: name|targets|alpha
TRAIN_ARMS=(
  "treestrap05|tree|0.05"
  "treestrap20|tree|0.2"
  "searchtd05|visited|0.05"
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
    test -s "$PRIOR" || { echo "frozen tables missing: $PRIOR"; exit 2; }
    run gates "$BIN/gate" --probe-start "$PROBE_START" --weights "$PRIOR" > "$OUT/gates.log" 2>&1
    grep -q "ALL GATES PASSED" "$OUT/gates.log" || { log "gates failed"; exit 2; }
    log "gates: all passed on the frozen tables"
    ;;
  train)
    verify_prior
    for arm in "${TRAIN_ARMS[@]}"; do
      IFS='|' read -r name targets alpha <<< "$arm"
      dir="$OUT/pilot/$name"
      if [ -e "$dir/DONE" ]; then log "train $name: already done"; continue; fi
      mkdir -p "$dir"
      run "train-$name" "$BIN/train" --layout "$LAYOUT" --alpha "$alpha" --init-from "$PRIOR" \
        --actor search --targets "$targets" --search-depth 3 --validate-at-start \
        --seeds-start "$TRAIN_START" --seeds-count "$TRAIN_COUNT" \
        --moves "$ARM_MOVES" --chunk-moves 20000 --threads "$THREADS" \
        --validate-start "$VALIDATE_START" --validate-games "$VALIDATE_GAMES" \
        --validate-every 300000 --quick-every 150000 \
        --plateau-window 3 --plateau-min-points 6 --no-checkpoint \
        --wall-seconds "$ARM_WALL" --experiment-id "$EXPERIMENT_ID" --out "$dir" \
        > "$dir/train.log" 2> "$dir/train.err"
      rm -f "$dir/latest-weights.bin"
      log "train $name: done ($(cat "$dir/stop.json"))"
    done
    ;;
  select)
    "$PY" "$HERE/analyze.py" --run "$RUN_ID" --root "$ROOT" --select-gentle-arm | tee "$OUT/pilot/selection.json"
    log "select: $(tr -d '\n' < "$OUT/pilot/selection.json" | cut -c1-200)"
    ;;
  freeze)
    verify_prior
    test -s "$OUT/pilot/selection.json" || { echo "no selection.json"; exit 2; }
    test -s "$CONTROL" || { echo "control tables missing: $CONTROL"; exit 2; }
    sha256sum "$CONTROL" | tee "$OUT/main/control-weights.sha256"
    grep -q "^$CONTROL_SHA256 " "$OUT/main/control-weights.sha256" || { log "control tables hash mismatch"; exit 2; }
    candidate_arm="$("$PY" -c "import json,sys; print(json.load(open(sys.argv[1]))['candidateArm'])" "$OUT/pilot/selection.json")"
    alt_arm="$("$PY" -c "import json,sys; print(json.load(open(sys.argv[1]))['alternateArm'])" "$OUT/pilot/selection.json")"
    for pair in "candidate:$OUT/pilot/$candidate_arm/best-weights.bin" "treestrapalt:$OUT/pilot/$alt_arm/best-weights.bin" "searchtd:$OUT/pilot/searchtd05/best-weights.bin"; do
      IFS=':' read -r label file <<< "$pair"
      test -s "$file" || { echo "missing $file"; exit 2; }
      sha256sum "$file" | tee "$OUT/main/$label-weights.sha256"
      run "gates-$label" "$BIN/gate" --probe-start "$PROBE_START" --weights "$file" > "$OUT/main/gates-$label.log" 2>&1
      grep -q "ALL GATES PASSED" "$OUT/main/gates-$label.log" || { log "gates on $label failed"; exit 2; }
    done
    log "freeze: candidate ($candidate_arm) $(cut -c1-16 "$OUT/main/candidate-weights.sha256")..., alternate ($alt_arm) $(cut -c1-16 "$OUT/main/treestrapalt-weights.sha256")..., searchtd05 $(cut -c1-16 "$OUT/main/searchtd-weights.sha256")..., control verified, gates passed on all three new files"
    ;;
  screen)
    for f in candidate treestrapalt searchtd control prior; do
      test -s "$OUT/main/$f-weights.sha256" || { echo "$f hash not recorded"; exit 2; }
    done
    grep -q "^$PRIOR_SHA256 " "$OUT/main/prior-weights.sha256" || { echo "tables hash mismatch"; exit 2; }
    grep -q "^$CONTROL_SHA256 " "$OUT/main/control-weights.sha256" || { echo "control hash mismatch"; exit 2; }
    test ! -e "$OUT/screen/heldout.json" || { echo "screen artifact already exists; the block is one-shot"; exit 2; }
    candidate_arm="$("$PY" -c "import json,sys; print(json.load(open(sys.argv[1]))['candidateArm'])" "$OUT/pilot/selection.json")"
    alt_arm="$("$PY" -c "import json,sys; print(json.load(open(sys.argv[1]))['alternateArm'])" "$OUT/pilot/selection.json")"
    CANDIDATE="$OUT/pilot/$candidate_arm/best-weights.bin"
    ALTERNATE="$OUT/pilot/$alt_arm/best-weights.bin"
    SEARCHTD="$OUT/pilot/searchtd05/best-weights.bin"
    for f in "$CANDIDATE" "$ALTERNATE" "$SEARCHTD" "$CONTROL"; do
      test -s "$f" || { echo "missing $f"; exit 2; }
    done
    log "screen: opening the held-out lease"
    "$PY" "$HERE/open-screen-lease.py" --root "$ROOT" --run "$RUN_ID" \
      --hash-file "$OUT/main/candidate-weights.sha256" \
      --lease "$SCREEN_LEASE" --experiment "research/experiments/$EXPERIMENT_ID.json" \
      --seeds-start "$SCREEN_START" | tee -a "$OUT/pipeline.log"
    mkdir -p "$OUT/screen"
    run screen "$BIN/screen" \
      --arm "prior=$PRIOR" --arm "treestrap=$CANDIDATE" \
      --arm-d3 "treestrapalt=$ALTERNATE" --arm-d3 "searchtd=$SEARCHTD" --arm-d3 "control=$CONTROL" \
      --arm-d4 "prior=$PRIOR" --arm-d4 "treestrap=$CANDIDATE" --skip-d4 \
      --seeds-start "$SCREEN_START" --games "$SCREEN_GAMES" --games-d4 "$SCREEN_GAMES_D4" --threads "$THREADS" --move-cap 2000 \
      --out "$OUT/screen/heldout.json" --experiment-id "$EXPERIMENT_ID" \
      > "$OUT/screen.log" 2> "$OUT/screen.err"
    log "screen: done"
    ;;
  compare)
    CMP="$ROOT/approaches/lifetime-objective/leaf-evolution/compare.py"
    ART="$OUT/screen/heldout.json"
    for pair in "treestrap-d3s7:prior-d3s7" "treestrap-d3s7:searchtd-d3s7" "searchtd-d3s7:prior-d3s7" "treestrap-d3s7:control-d3s7" "control-d3s7:prior-d3s7" "treestrapalt-d3s7:prior-d3s7" "treestrap-d3s7:treestrapalt-d3s7" \
                "treestrap-d4s7:prior-d4s7" "treestrap-d4s7:treestrap-d3s7" "prior-d4s7:prior-d3s7" \
                "prior-d3s7:fair-d3s7" "treestrap-d3s7:fair-d3s7" "searchtd-d3s7:fair-d3s7" "control-d3s7:fair-d3s7" "treestrap-d4s7:fair-d3s7" \
                "treestrap-1ply:prior-1ply" "treestrap-1ply:fair-d3s7" "prior-1ply:fair-d3s7"; do
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
