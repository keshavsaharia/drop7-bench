#!/bin/sh
# EX-20260902-treestrap-leaf-refit-panel-and-pilot-4024f360, stages 1 and 2:
# leaf terms of every panel root, the ridge refits, and the depth-5 sub-panel
# proxy.  Run from the repository root:
#   sh .../run-ex6-leaf.sh RUN_DIR CEM_WEIGHTS THREADS EVERY
set -eu
RUN="$1"; CEM="$2"; THREADS="${3:-12}"; EVERY="$4"
BIN=approaches/value-policy-learning/oneply-q-prune/rust/target/release
AN=approaches/value-policy-learning/oneply-q-prune/analysis
stamp() { date -u +%Y-%m-%dT%H:%M:%SZ; }
echo "leaf stage 1 start $(stamp)" >> "$RUN/stages.log"
"$BIN/leaf_terms" --panel "$RUN/panel.ndjson" --kf-weights "$CEM" --out "$RUN/root-terms.ndjson" 2>&1 | tee "$RUN/leaf-terms.log"
python3 "$AN/fit_leaf.py" --panel "$RUN/panel.ndjson" --terms "$RUN/root-terms.ndjson" --cem-weights "$CEM" \
  --train-games 32 --floor -50000 --out "$RUN/fit-leaf.json" \
  --weights18-out "$RUN/weights-leaf18.txt" --weights19-out "$RUN/weights-leaf19.txt" 2>&1 | tee "$RUN/fit-leaf.log"
echo "leaf stage 1 end $(stamp)" >> "$RUN/stages.log"
echo "leaf stage 2 start $(stamp)" >> "$RUN/stages.log"
"$BIN/prune_eval" --panel "$RUN/panel.ndjson" --game-min 32 --game-max 47 --every "$EVERY" --limit 128 --threads "$THREADS" \
  --out "$RUN/leaf-d5.ndjson" --config exact:4:7 --config "leaf:4:7:$RUN/weights-leaf18.txt" --config "leaf:4:7:$RUN/weights-leaf19.txt" \
  2>&1 | tee "$RUN/leaf-d5.log"
python3 "$AN/prune_metrics.py" --results "$RUN/leaf-d5.ndjson" --truth-results "$RUN/d5-panel.ndjson" --truth-config exact:5:7 \
  --reference-config exact:4:7 --paired-config exact:4:7 --out "$RUN/leaf-d5-summary.json" 2>&1 | tee "$RUN/leaf-d5-summary.log"
echo "leaf stage 2 end $(stamp)" >> "$RUN/stages.log"
