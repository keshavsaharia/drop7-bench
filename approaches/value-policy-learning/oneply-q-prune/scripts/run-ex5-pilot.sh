#!/bin/sh
# EX-20260902-pruned-search-gameplay-pilot-bf465b1d: the 256-game pilot on the
# previously evaluated cohort 0xa5277000-0xa52770ff.  Run from the repository
# root:  sh .../run-ex5-pilot.sh RUN_DIR THREADS ARM_SPEC [ARM_SPEC ...]
# (the full-width fair d4s7 arm is always first).
set -eu
RUN="$1"; shift
THREADS="$1"; shift
BIN=approaches/value-policy-learning/oneply-q-prune/rust/target/release
stamp() { date -u +%Y-%m-%dT%H:%M:%SZ; }
echo "pilot start $(stamp)" >> "$RUN/stages.log"
set -- --arm fair:4:7 "$@"
ARGS=""
for spec in "$@"; do
  if [ "$spec" = "--arm" ]; then continue; fi
  ARGS="$ARGS --arm $spec"
done
# shellcheck disable=SC2086
"$BIN/evaluate" --seeds-start 0xa5277000 --games 256 --move-cap 2000 --threads "$THREADS" --table 65536 \
  --out "$RUN/evaluate-pilot.json" $ARGS 2>&1 | tee "$RUN/evaluate-pilot.log"
echo "pilot end $(stamp)" >> "$RUN/stages.log"
