#!/usr/bin/env bash
# Overnight gate-corpora driver: generates the fresh held-out blocks for the
# full-training override gate (E), the D2-teacher ranking gate, and the
# D2-teacher override gate, plus their exact D4/D1 comparator labels.
# Runs at low thread count to stay out of the way of the main corpus job.
set -euo pipefail

RUNA_ID="$1"
RUNB_ID="$2"
GEN=build/afterstate/generate-corpus
LAB=build/afterstate/label-d4

for spec in "E 0x5da70400 $RUNA_ID/corpus-e" "Brank 0x5da70500 $RUNB_ID/corpus-rank" "Boverride 0x5da70600 $RUNB_ID/corpus-override"; do
  set -- $spec
  name="$1"; seeds="$2"; out="$3"
  if [ ! -f "runs/$out/corpus.ndjson" ]; then
    "$GEN" --seed-start "$seeds" --games 64 --roots 8000 --scenarios 256 \
      --threads 12 --fold-force heldout --out "runs/$out" --run-id "$RUNB_ID"
  fi
  if [ ! -f "runs/$out-labels/comparator-labels.tsv" ]; then
    "$LAB" --roots "runs/$out/roots.tsv" --out "runs/$out-labels/comparator-labels.tsv" \
      --threads 12
  fi
  echo "gate corpus $name done: $out"
done
echo "ALL GATE CORPORA DONE"
