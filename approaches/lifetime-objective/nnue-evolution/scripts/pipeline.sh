#!/usr/bin/env bash
# Stage driver for the nnue-evolution experiments.
#
# Every gameplay stage is launched through this script so the exact command,
# its wall/CPU/peak-RSS usage (scripts/with-rusage.py) and its stdout/stderr
# are retained under runs/<RUN_ID>/nnue-evolution/.  The defaults below are
# the sub-blocks and constants of the first run's frozen protocol
# (EX-20260902-nnue-evolution-d3-v2-49c18bc2); the continuation experiment
# (EX-20260903-nnue-evolution-continuation-d3-f8ce9181) sets the environment
# documented in its record.  Only ranges named by a frozen protocol may be
# passed here.
#
#   First run, SL-20260825T063000Z-a52e0300 (training):
#     0xa52e0300-0xa52e0500  stage A teacher corpus, 512 games
#     0xa52e0500-0xa52e0c80  stage C fitness blocks, generation g plays
#                            [0xa52e0500 + 32 g, +32)
#     0xa52e0c80-0xa52e0d00  stage C elite re-selection, 128 games
#   First run, SL-20260825T063000Z-a52e1300 (public-development):
#     0xa52e1300-0xa52e1340  stage D held-out screen, 64 games, once
#
# DO NOT edit or replace this file while any stage is executing it: bash reads
# the script incrementally and a running stage will fail on its next line
# (this happened on 2026-09-02, see the first run's record).
#
# Usage: RUN_ID=RUN-... scripts/pipeline.sh <corpus|pretrain|evolve|select|screen|compare|chain|chain-evolve>
# Environment (defaults = the first run's protocol):
#   THREADS (32), CORPUS_WALL (46800), EVOLVE_WALL (21600)
#   EXPERIMENT_ID, LEASE_START (0xa52e0500), SCREEN_START (0xa52e1300),
#   SCREEN_LEASE (lease record path), GENERATIONS (60), EVOLVE_SEED (0x0e701e58)
#   INIT (pretrain/init.bin of this run), RESUME_POP (unset), BASELINE (unset),
#   SIGMA_REL (0.05), SIGMA_TAU (0 = constant), SIGMA_FLOOR (0),
#   PLATEAU_WINDOW (0 = off), PLATEAU_EVERY (50), PLATEAU_MIN (100),
#   EVOLVE_WALL (21,600; pinned on the first evolve invocation and cumulative
#   across resumes through evolve/wall-budget.json)
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

EXPERIMENT_ID="${EXPERIMENT_ID:-EX-20260902-nnue-evolution-d3-v2-49c18bc2}"
LEASE_START="${LEASE_START:-0xa52e0500}"
SCREEN_START="${SCREEN_START:-0xa52e1300}"
SCREEN_LEASE="${SCREEN_LEASE:-research/seeds/leases/SL-20260825T063000Z-a52e1300.json}"
GENERATIONS="${GENERATIONS:-60}"
EVOLVE_SEED="${EVOLVE_SEED:-0x0e701e58}"
INIT="${INIT:-$OUT/pretrain/init.bin}"
SIGMA_REL="${SIGMA_REL:-0.05}"
SIGMA_TAU="${SIGMA_TAU:-0}"
SIGMA_FLOOR="${SIGMA_FLOOR:-0}"
PLATEAU_WINDOW="${PLATEAU_WINDOW:-0}"
PLATEAU_EVERY="${PLATEAU_EVERY:-50}"
PLATEAU_MIN="${PLATEAU_MIN:-100}"
EVOLVE_WALL="${EVOLVE_WALL:-21600}"

log() { echo "[$(date -u +%FT%TZ)] $*" | tee -a "$OUT/pipeline.log"; }
run() {
  # run <label> <cmd...>: log the command verbatim, capture usage.
  local label="$1"; shift
  log "$label: $*"
  "$PY" "$HERE/with-rusage.py" --record "$RUSAGE" --label "$label" -- "$@"
}

# Optional continuation flags for evolve, assembled from the environment.
evolve_extra=()
if [ -n "${RESUME_POP:-}" ]; then evolve_extra+=(--resume-population "$RESUME_POP"); fi
if [ -n "${BASELINE:-}" ]; then evolve_extra+=(--baseline "$BASELINE"); fi
if [ "$SIGMA_TAU" != "0" ]; then evolve_extra+=(--sigma-decay-tau "$SIGMA_TAU" --sigma-floor "$SIGMA_FLOOR"); fi
if [ "$PLATEAU_WINDOW" != "0" ]; then
  evolve_extra+=(--plateau-window "$PLATEAU_WINDOW" --plateau-check-every "$PLATEAU_EVERY" --plateau-min-generations "$PLATEAU_MIN")
fi
screen_extra=()
if [ -n "${BASELINE:-}" ]; then screen_extra+=(--arm "baseline-run1=$BASELINE"); fi

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
    # Stage C.  GA constants from the frozen protocol in force.  Resumable
    # (checkpoint contract in evolve.rs); a PLATEAU marker ends the loop.
    # EVOLVE_WALL is the original total allowance on every invocation: evolve
    # pins one absolute deadline and refuses a changed value, so rerunning this
    # stage cannot reset or extend the budget.
    run evolve "$BIN/evolve" --init "$INIT" \
      --lease-start "$LEASE_START" --out "$OUT/evolve" \
      --population 32 --games 32 --generations "$GENERATIONS" --elites 4 --tournament 3 \
      --sigma-rel "$SIGMA_REL" --seed "$EVOLVE_SEED" --threads "$THREADS" \
      --wall-seconds "$EVOLVE_WALL" --move-cap 2000 \
      --experiment-id "$EXPERIMENT_ID" "${evolve_extra[@]}" \
      >> "$OUT/evolve.log" 2>> "$OUT/evolve.err"
    ;;
  select)
    # Stage C, elite re-selection: top 8 of the last completed generation on
    # the 128 fresh games immediately after the last fitness block played
    # (evolve.rs derives the block from the last completed generation).
    run select "$BIN/evolve" --select --lease-start "$LEASE_START" \
      --out "$OUT/evolve" --generations "$GENERATIONS" --games 32 --select-games 128 \
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
      --hash-file "$OUT/evolve/candidate-weights.sha256" \
      --lease "$SCREEN_LEASE" --experiment "research/experiments/$EXPERIMENT_ID.json" \
      --seeds-start "$SCREEN_START" | tee -a "$OUT/pipeline.log"
    mkdir -p "$OUT/screen"
    run screen "$BIN/screen" --candidate "$OUT/evolve/candidate-weights.bin" \
      --init "$INIT" --seeds-start "$SCREEN_START" --games 64 \
      --threads "$THREADS" --move-cap 2000 --out "$OUT/screen/heldout.json" \
      --experiment-id "$EXPERIMENT_ID" "${screen_extra[@]}" \
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
    if [ -n "${BASELINE:-}" ]; then
      "$PY" "$CMP" "$ART" --candidate candidate --reference baseline-run1 --out "$OUT/screen/compare-candidate-vs-baseline-run1.json"
      "$PY" "$CMP" "$ART" --candidate baseline-run1 --reference fair-d3s7 --out "$OUT/screen/compare-baseline-run1-vs-fair-d3s7.json"
    fi
    ;;
  chain)
    # Unattended continuation of the first run's protocol: wait for a
    # separately launched corpus stage to finish, then run every remaining
    # stage in protocol order.  Any stage failure stops the chain; nothing is
    # retried on a different block.  The screen stage performs the held-out
    # lease's state transition itself.
    log "chain: waiting for the corpus stage"
    # The liveness probe is a process-table scan, which can miss once (it did,
    # at 2026-09-02T22:09Z, with the corpus binary alive), so only three
    # consecutive misses ten seconds apart count as "the process is gone".
    misses=0
    while ! grep -q "corpus: done" "$OUT/pipeline.log"; do
      if pgrep -f "teacher_corpus --seeds-start 0xa52e0300" > /dev/null; then
        misses=0
        sleep 60
        continue
      fi
      misses=$((misses + 1))
      if [ "$misses" -ge 3 ]; then
        sleep 5
        grep -q "corpus: done" "$OUT/pipeline.log" || { log "chain: corpus process ended without its done marker; aborting"; exit 3; }
      fi
      sleep 10
    done
    "$0" pretrain
    "$0" evolve
    "$0" select
    "$0" screen
    "$0" compare
    "$PY" "$HERE/analyze.py" --run "$RUN_ID" --root "$ROOT" > /dev/null
    log "chain: analysis written to $OUT/analysis.md"
    ;;
  chain-evolve)
    # Unattended continuation experiment: evolve (resumed population) ->
    # re-selection -> one-shot screen -> compare -> analysis.
    "$0" evolve
    "$0" select
    "$0" screen
    "$0" compare
    "$PY" "$HERE/analyze.py" --run "$RUN_ID" --root "$ROOT" > /dev/null
    log "chain-evolve: analysis written to $OUT/analysis.md"
    ;;
  *)
    echo "unknown stage $stage" >&2; exit 2 ;;
esac
log "$stage: done"
