#!/bin/sh
# EX-20260902-pruned-search-gameplay-pilot-bf465b1d: the 256-game pilot on the
# previously evaluated cohort 0xa5277000-0xa52770ff.  Run from the repository
# root:  sh .../run-ex5-pilot.sh RUN_DIR THREADS ARM_SPEC [ARM_SPEC ...]
# The full-width fair d4s7 arm is always played first; every ARM_SPEC is
# passed to `evaluate` as its own `--arm` argument, whatever it contains.
set -eu
RUN="$1"; shift
THREADS="$1"; shift
BIN="${BIN:-approaches/value-policy-learning/oneply-q-prune/rust/target/release}"
stamp() { date -u +%Y-%m-%dT%H:%M:%SZ; }
echo "pilot start $(stamp)" >> "$RUN/stages.log"

# Rebuild the positional parameters as `--arm SPEC` pairs.  Each spec is
# moved from the front of "$@" to the back with its flag, so argument
# boundaries survive whitespace and glob characters without any eval.
set -- fair:4:7 "$@"
count=$#
index=0
while [ "$index" -lt "$count" ]; do
  spec="$1"
  shift
  set -- "$@" --arm "$spec"
  index=$((index + 1))
done

"$BIN/evaluate" --seeds-start 0xa5277000 --games 256 --move-cap 2000 --threads "$THREADS" --table 65536 \
  --out "$RUN/evaluate-pilot.json" "$@" 2>&1 | tee "$RUN/evaluate-pilot.log"
echo "pilot end $(stamp)" >> "$RUN/stages.log"
