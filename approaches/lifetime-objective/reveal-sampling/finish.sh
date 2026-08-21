#!/usr/bin/env bash
# Completes finding-16 as the two continuation arms progress.
#
# `run-arms.sh` runs each arm as four 16-game chunks over consecutive blocks of
# the shared evaluation cohort 0xa51d1000-0xa51d103f, so at any moment an arm
# holds a whole number of finished chunks.  This script re-pools whatever chunks
# exist, prints the completed-depth audit, the cohort rows and every paired
# comparison, and is idempotent: pooling is a deterministic function of the
# chunk files, so running it twice writes the same bytes.
#
# A partial arm is compared with `stats.py partial`, which pairs on the shared
# seeds and prints the paired n; it is never presented as a 64-game cohort.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
R="$ROOT/runs/RUN-A525-reveal"
T="$ROOT/approaches/lifetime-objective/reveal-sampling"
S="$ROOT/runs/RUN-A51D-s7confirm"

echo "===== arm progress ====="
for arm in d4-n7-m2 d3-n7-m12; do
  n=0
  for chunk in 0 1 2 3; do [ -s "$R/$arm-c$chunk.json" ] && n=$((n + 16)); done
  echo "  $arm: $n/64 games pooled"
done
echo
echo "===== re-pool the chunked arms (deterministic; safe to repeat) ====="
for arm in d4-n7-m2 d3-n7-m12; do
  chunks=()
  for chunk in 0 1 2 3; do [ -s "$R/$arm-c$chunk.json" ] && chunks+=("$R/$arm-c$chunk.json"); done
  [ ${#chunks[@]} -gt 0 ] && python3 "$T/pool.py" "$R/$arm-pooled.json" "${chunks[@]}"
done
echo
echo "===== pooling validity: four pooled chunks must reproduce a single run ====="
python3 "$T/compare.py" identity "$R/cont-pooltest-pooled.json" "$R/d3-n5-m1.json"
echo
echo "===== cohort rows ====="
python3 "$T/stats.py" rows \
  "d3-N7-M1=$R/d3-n7-m1.json" "d3-N7-M3=$R/d3-n7-m3.json" "d3-N7-M6=$R/d3-n7-m6.json" \
  "d3-N7-M12=$R/d3-n7-m12-pooled.json" "d4-N7-M1=$S/fresh-s7.json" \
  "d4-N7-M2=$R/d4-n7-m2-pooled.json" "d4-N5-M1=$S/fresh-s5.json"
echo
echo "===== paired deltas, complete arms (one-sided 95% bootstrap over whole games) ====="
python3 "$T/stats.py" delta \
  "d3-N7-M3=$R/d3-n7-m3.json"        "d3-N7-M1=$R/d3-n7-m1.json" \
  "d3-N7-M6=$R/d3-n7-m6.json"        "d3-N7-M1=$R/d3-n7-m1.json" \
  "d4-N7-M2=$R/d4-n7-m2-pooled.json" "d4-N7-M1=$S/fresh-s7.json" \
  "d4-N7-M2=$R/d4-n7-m2-pooled.json" "d4-N5-M1=$S/fresh-s5.json" \
  "d4-N7-M2=$R/d4-n7-m2-pooled.json" "d3-N7-M6=$R/d3-n7-m6.json" \
  "d3-N7-M6=$R/d3-n7-m6.json"        "d4-N7-M1=$S/fresh-s7.json"
echo
echo "===== paired deltas involving the incomplete M=12 arm (subset of seeds) ====="
python3 "$T/stats.py" partial \
  "d3-N7-M12=$R/d3-n7-m12-pooled.json" "d3-N7-M1=$R/d3-n7-m1.json" \
  "d3-N7-M12=$R/d3-n7-m12-pooled.json" "d3-N7-M6=$R/d3-n7-m6.json" \
  "d3-N7-M12=$R/d3-n7-m12-pooled.json" "d4-N7-M1=$S/fresh-s7.json"
