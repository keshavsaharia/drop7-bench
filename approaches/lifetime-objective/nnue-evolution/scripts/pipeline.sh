#!/usr/bin/env bash
# Stage driver for EX-20260902-nnue-evolution-d3-v2-49c18bc2 (successor of EX-20260825-nnue-evolution-d3-bca7f330).
#
# Every gameplay stage of the experiment is launched through this script so
# the exact command, its wall/CPU/peak-RSS usage (scripts/with-rusage.py) and
# its stdout/stderr are retained under runs/<RUN_ID>/nnue-evolution/.  The
# seed origins below are the sub-blocks of the two leases registered with the
# experiment and are deliberately hard-coded: this script can only ever play
# the ranges the frozen protocol names.
#
#   SL-20260825T063000Z-a52e0300 (training):
#     0xa52e0300-0xa52e0500  stage A teacher corpus, 512 games
#     0xa52e0500-0xa52e0c80  stage C fitness blocks, generation g plays
#                            [0xa52e0500 + 32 g, +32)
#     0xa52e0c80-0xa52e0d00  stage C elite re-selection, 128 games
#   SL-20260825T063000Z-a52e1300 (public-development):
#     0xa52e1300-0xa52e1340  stage D held-out screen, 64 games, once
#
# Usage: RUN_ID=RUN-... scripts/pipeline.sh <corpus|pretrain|evolve|select|screen|compare|chain>
# Environment: THREADS (default 32), CORPUS_WALL (s, default 46800),
#              EVOLVE_WALL (s, default 21600)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APPROACH="$(cd "$HERE/.." && pwd)"
ROOT="$(cd "$APPROACH/../../.." && pwd)"
BIN="$APPROACH/target/release"
RUN_ID="${RUN_ID:?set RUN_ID to the research run record id}"
OUT="$ROOT/runs/$RUN_ID/nnue-evolution"
THREADS="${THREADS:-32}"
PY="${PYTHON:-/usr/bin/python3}"
RUSAGE="$OUT/rusage.jsonl"
mkdir -p "$OUT"

log() { echo "[$(date -u +%FT%TZ)] $*" | tee -a "$OUT/pipeline.log"; }
run() {
  # run <label> <cmd...>: log the command verbatim, capture usage.
  local label="$1"; shift
  log "$label: $*"
  "$PY" "$HERE/with-rusage.py" --record "$RUSAGE" --label "$label" -- "$@"
}

stage="${1:?stage required}"
case "$stage" in
  corpus)
    # Stage A.  --move-cap 500 is the disclosed operational cap the frozen
    # protocol leaves open (heavy-tailed teacher game lengths; labels are
    # search values, so the cap truncates the state distribution but does
    # not bias any label).  The wall budget stops new games; in-flight games
    # finish and are kept.  Resumable: rerun skips finished part files.
    run corpus "$BIN/teacher_corpus" --seeds-start 0xa52e0300 --games 512 \
      --threads "$THREADS" --depth 5 --move-cap 500 \
      --wall-seconds "${CORPUS_WALL:-46800}" --out "$OUT/corpus" \
      > "$OUT/corpus.log" 2> "$OUT/corpus.err"
    ;;
  pretrain)
    # Stage B.  Adam + Huber, whole-origin split, best epoch by validation
    # Huber (protocol).  16 epochs comfortably covers the epoch-5-to-10
    # validation optimum seen in the C0 probe; best-epoch selection means
    # extra epochs cannot hurt the init.  256 held-out probe roots.
    run pretrain "$BIN/pretrain" --corpus "$OUT/corpus/corpus.jsonl" \
      --out "$OUT/pretrain" --epochs 16 --batch 64 --lr 3e-4 \
      --seed 0x0e701e57 --probe-roots 256 --threads "$THREADS" \
      > "$OUT/pretrain.log" 2> "$OUT/pretrain.err"
    ;;
  evolve)
    # Stage C.  All GA constants are the frozen protocol's.  Resumable.
    run evolve "$BIN/evolve" --init "$OUT/pretrain/init.bin" \
      --lease-start 0xa52e0500 --out "$OUT/evolve" \
      --population 32 --games 32 --generations 60 --elites 4 --tournament 3 \
      --sigma-rel 0.05 --seed 0x0e701e58 --threads "$THREADS" \
      --wall-seconds "${EVOLVE_WALL:-21600}" --move-cap 2000 \
      >> "$OUT/evolve.log" 2>> "$OUT/evolve.err"
    ;;
  select)
    # Stage C, elite re-selection: top 8 of the last completed generation on
    # 128 fresh games at lease-start + 60*32 = 0xa52e0c80 (evolve.rs derives
    # the block from --generations and --games, which must match above).
    run select "$BIN/evolve" --select --lease-start 0xa52e0500 \
      --out "$OUT/evolve" --generations 60 --games 32 --select-games 128 \
      --threads "$THREADS" --move-cap 2000 \
      > "$OUT/select.log" 2> "$OUT/select.err"
    sha256sum "$OUT/evolve/candidate-weights.bin" | tee "$OUT/evolve/candidate-weights.sha256"
    ;;
  screen)
    # Stage D.  Opened exactly once.  The candidate hash must already be
    # recorded (select stage), and the held-out lease's state transition is
    # made HERE, immediately before the first held-out seed is read, by
    # whichever path invokes this stage: open-screen-lease.py refuses unless
    # the hash file exists and the lease is still reserved, so a second
    # invocation (or one after an interrupted screen) cannot re-read the
    # block; that needs a new experiment record, as the protocol says.
    test -s "$OUT/evolve/candidate-weights.sha256" || { echo "candidate hash not recorded"; exit 2; }
    test ! -e "$OUT/screen/heldout.json" || { echo "screen artifact already exists; the block is one-shot"; exit 2; }
    log "screen: opening the held-out lease"
    "$PY" "$HERE/open-screen-lease.py" --root "$ROOT" --run "$RUN_ID" \
      --hash-file "$OUT/evolve/candidate-weights.sha256" | tee -a "$OUT/pipeline.log"
    mkdir -p "$OUT/screen"
    run screen "$BIN/screen" --candidate "$OUT/evolve/candidate-weights.bin" \
      --init "$OUT/pretrain/init.bin" --seeds-start 0xa52e1300 --games 64 \
      --threads "$THREADS" --move-cap 2000 --out "$OUT/screen/heldout.json" \
      > "$OUT/screen.log" 2> "$OUT/screen.err"
    ;;
  compare)
    # Paired statistics with the unchanged compare.py of the prior
    # leaf-evolution experiment (bootstrap seed 0xb0071eaf, 20,000 resamples).
    CMP="$ROOT/approaches/lifetime-objective/leaf-evolution/compare.py"
    ART="$OUT/screen/heldout.json"
    "$PY" "$CMP" "$ART" --candidate candidate --reference fair-d3s7 --out "$OUT/screen/compare-candidate-vs-fair-d3s7.json"
    "$PY" "$CMP" "$ART" --candidate init-d3s7 --reference fair-d3s7 --out "$OUT/screen/compare-init-vs-fair-d3s7.json"
    "$PY" "$CMP" "$ART" --candidate candidate --reference init-d3s7 --out "$OUT/screen/compare-candidate-vs-init.json"
    "$PY" "$CMP" "$ART" --candidate fair-d4s7 --reference fair-d3s7 --out "$OUT/screen/compare-fair-d4s7-vs-fair-d3s7.json"
    ;;
  chain)
    # Unattended continuation: wait for a separately launched corpus stage to
    # finish, then run every remaining stage in protocol order.  Any stage
    # failure stops the chain; nothing is retried on a different block.  The
    # screen stage performs the held-out lease's state transition itself.
    log "chain: waiting for the corpus stage"
    while ! grep -q "corpus: done" "$OUT/pipeline.log"; do
      if ! ps -eo cmd | grep -v grep | grep -q "teacher_corpus --seeds-start 0xa52e0300"; then
        sleep 5
        grep -q "corpus: done" "$OUT/pipeline.log" || { log "chain: corpus process ended without its done marker; aborting"; exit 3; }
      fi
      sleep 60
    done
    "$0" pretrain
    "$0" evolve
    "$0" select
    "$0" screen
    "$0" compare
    "$PY" "$HERE/analyze.py" --run "$RUN_ID" --root "$ROOT" > /dev/null
    log "chain: analysis written to $OUT/analysis.md"
    ;;
  *)
    echo "unknown stage $stage" >&2; exit 2 ;;
esac
log "$stage: done"
