#!/usr/bin/env bash
# Stage 2: the two learned arms of the 2x2 on the fixed evaluation cohort,
# using the blend frozen by stage 1's tuning sweep, then an optional 7-stratum
# tuning confirmation if budget remains.
#
# The blend is passed in so the script records what was actually run rather
# than hiding it in a default.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "${ROOT}"
B=./build/lifetime-leaf/search
M=runs/RUN-A52-LEAF/model/leafnet-h64.d7leaf
T="${THREADS:-12}"
W="${W:-0.50}"
SCALE="${SCALE:-3400}"
KIND="${KIND:-lifetime}"

$B --depth 4 --chance-samples 5 --max-work  3200000 \
   --seed-start 0xa51d1000 --games 64 --threads "$T" --model "$M" \
   --w "$W" --scale "$SCALE" --leaf-value "$KIND" \
   --output runs/RUN-A52-LEAF/eval/s5-learned.json

$B --depth 4 --chance-samples 7 --max-work 16000000 \
   --seed-start 0xa51d1000 --games 64 --threads "$T" --model "$M" \
   --w "$W" --scale "$SCALE" --leaf-value "$KIND" \
   --output runs/RUN-A52-LEAF/eval/s7-learned.json
echo "stage2 complete"
