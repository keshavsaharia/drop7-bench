#!/usr/bin/env bash
# Stage 1: the two reference arms of the 2x2 (which are also the validity gate
# against finding-05's published figures, because w = 0 is the frozen leaf
# bit-for-bit) followed by the tuning sweep on a separate leased cohort.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "${ROOT}"
B=./build/lifetime-leaf/search
M=runs/RUN-A52-LEAF/model/leafnet-h64.d7leaf
T="${THREADS:-12}"

# --- 2x2 reference arms on the fixed evaluation cohort ----------------------
$B --depth 4 --chance-samples 5 --max-work  3200000 \
   --seed-start 0xa51d1000 --games 64 --threads "$T" --w 0 \
   --output runs/RUN-A52-LEAF/eval/s5-w000.json
$B --depth 4 --chance-samples 7 --max-work 16000000 \
   --seed-start 0xa51d1000 --games 64 --threads "$T" --w 0 \
   --output runs/RUN-A52-LEAF/eval/s7-w000.json

# --- tuning, on SEEDLEASE-A52-LEAF, never on the evaluation cohort ----------
$B --depth 4 --chance-samples 5 --max-work 3200000 \
   --seed-start 0xa5241000 --games 32 --threads "$T" --w 0 \
   --output runs/RUN-A52-LEAF/tune/a-s5-w000.json
for W in 0.25 0.50 1.00; do
  $B --depth 4 --chance-samples 5 --max-work 3200000 \
     --seed-start 0xa5241000 --games 32 --threads "$T" --model "$M" \
     --w "$W" --leaf-value lifetime --scale 3400 \
     --output "runs/RUN-A52-LEAF/tune/a-s5-life-w${W/./}.json"
done
$B --depth 4 --chance-samples 5 --max-work 3200000 \
   --seed-start 0xa5241000 --games 32 --threads "$T" --model "$M" \
   --w 0.50 --leaf-value hazard --scale 17000 \
   --output runs/RUN-A52-LEAF/tune/a-s5-haz-w050.json
echo "stage1 complete"
