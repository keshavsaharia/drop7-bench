#!/bin/sh
# EX-20260902-oneply-q-fit-and-pruned-search-panel-46c75cdf, stages after the
# panel: fit, depth-4 pruning on held-out roots, the 128-root depth-5
# sub-panel, depth-5 pruning, and the metric summaries.  Run from the
# repository root:  sh approaches/value-policy-learning/oneply-q-prune/scripts/run-ex4-stages.sh RUN_DIR CEM_WEIGHTS THREADS
set -eu
RUN="$1"
CEM="$2"
THREADS="${3:-12}"
BIN=approaches/value-policy-learning/oneply-q-prune/rust/target/release
AN=approaches/value-policy-learning/oneply-q-prune/analysis
stamp() { date -u +%Y-%m-%dT%H:%M:%SZ; }

echo "fit start $(stamp)" >> "$RUN/stages.log"
python3 "$AN/fit_q.py" --panel "$RUN/panel.ndjson" --train-games 32 --cem-weights "$CEM" \
  --out "$RUN/fit.json" --weights-out "$RUN/weights-lq.txt" --weights-raw-out "$RUN/weights-lq-raw.txt" 2>&1 | tee "$RUN/fit.log"
echo "fit end $(stamp)" >> "$RUN/stages.log"

LQ="$RUN/weights-lq.txt"
echo "prune-d4 start $(stamp)" >> "$RUN/stages.log"
"$BIN/prune_eval" --panel "$RUN/panel.ndjson" --game-min 32 --game-max 47 --threads "$THREADS" --out "$RUN/prune-d4.ndjson" \
  --config exact:4:7 \
  --config pruned:4:7:2,2:center --config pruned:4:7:3,3:center --config pruned:4:7:4,4:center --config pruned:4:7:3,2:center \
  --config "pruned:4:7:2,2:kf=$CEM" --config "pruned:4:7:3,3:kf=$CEM" --config "pruned:4:7:4,4:kf=$CEM" --config "pruned:4:7:3,2:kf=$CEM" \
  --config pruned:4:7:2,2:d1 --config pruned:4:7:3,3:d1 --config pruned:4:7:4,4:d1 --config pruned:4:7:3,2:d1 \
  --config "pruned:4:7:2,2:lq=$LQ" --config "pruned:4:7:3,3:lq=$LQ" --config "pruned:4:7:4,4:lq=$LQ" --config "pruned:4:7:3,2:lq=$LQ" \
  2>&1 | tee "$RUN/prune-d4.log"
echo "prune-d4 end $(stamp)" >> "$RUN/stages.log"
python3 "$AN/prune_metrics.py" --results "$RUN/prune-d4.ndjson" --truth-config exact:4:7 --reference-config exact:4:7 \
  --out "$RUN/prune-d4-summary.json" 2>&1 | tee "$RUN/prune-d4-summary.log"

HELD=$(python3 -c "import sys; print(sum(1 for l in open('$RUN/panel.ndjson') if l.strip() and __import__('json').loads(l)['game']>=32))")
EVERY=$((HELD / 128))
echo "d5 sub-panel: $HELD held-out roots, every $EVERY, limit 128" | tee -a "$RUN/stages.log"
echo "d5-panel start $(stamp)" >> "$RUN/stages.log"
"$BIN/prune_eval" --panel "$RUN/panel.ndjson" --game-min 32 --game-max 47 --every "$EVERY" --limit 128 --threads "$THREADS" \
  --out "$RUN/d5-panel.ndjson" --config exact:5:7 2>&1 | tee "$RUN/d5-panel.log"
echo "d5-panel end $(stamp)" >> "$RUN/stages.log"
echo "prune-d5 start $(stamp)" >> "$RUN/stages.log"
"$BIN/prune_eval" --panel "$RUN/panel.ndjson" --game-min 32 --game-max 47 --every "$EVERY" --limit 128 --threads "$THREADS" \
  --out "$RUN/prune-d5.ndjson" \
  --config exact:4:7 \
  --config pruned:5:7:2,2,2:d1 --config pruned:5:7:3,3,2:d1 --config pruned:5:7:3,3,3:d1 \
  --config "pruned:5:7:2,2,2:lq=$LQ" --config "pruned:5:7:3,3,2:lq=$LQ" --config "pruned:5:7:3,3,3:lq=$LQ" \
  2>&1 | tee "$RUN/prune-d5.log"
echo "prune-d5 end $(stamp)" >> "$RUN/stages.log"
python3 "$AN/prune_metrics.py" --results "$RUN/prune-d5.ndjson" --truth-results "$RUN/d5-panel.ndjson" --truth-config exact:5:7 \
  --reference-config exact:4:7 --paired-config exact:4:7 --out "$RUN/prune-d5-summary.json" 2>&1 | tee "$RUN/prune-d5-summary.log"
echo "all stages end $(stamp)" >> "$RUN/stages.log"
