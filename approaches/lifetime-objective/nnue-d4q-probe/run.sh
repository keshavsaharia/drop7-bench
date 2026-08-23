#!/usr/bin/env bash
# EX-20260823-nnue-d4q-ordering-probe-0ca09bb1: exact commands, in order.
# Usage: approaches/lifetime-objective/nnue-d4q-probe/run.sh [OUT_DIR]
# Default OUT_DIR is the preregistered artifact path.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT"
OUT="${1:-runs/RUN-20260823T191900Z-b9f8f80d/probe}"
HERE=approaches/lifetime-objective/nnue-d4q-probe
mkdir -p "$OUT"

# Environment (substrate-map section 2.8): ROCm venv if present, else the
# system python with torch; OPENBLAS_NUM_THREADS=1 is a correctness requirement.
# shellcheck disable=SC1091
source approaches/lifetime-objective/gpu/activate.sh >/dev/null 2>&1 || true
export OPENBLAS_NUM_THREADS=1
PY="${D7_PYTHON:-python}"
command -v "$PY" >/dev/null || PY=/usr/bin/python3

{
  echo "# run.sh started $(date -u +%Y-%m-%dT%H:%M:%SZ) git $(git rev-parse HEAD 2>/dev/null || echo unknown)"
  echo "# python: $(command -v "$PY")"
} | tee "$OUT/commands.log"

# 1. Train 5 seeds (validation only), select by validation top-1, read the
#    held-out panel once, write train.log, metrics.json, model-seed*.pt.
CMD="$PY $HERE/train_probe.py --out $OUT --epochs 30 --lr 1e-3 --batch-roots 256 --threads 8 --device cpu --wall-budget 5400"
echo "$CMD" | tee -a "$OUT/commands.log"
$CMD

# 2. Informational: export the selected seed with the existing leaf exporter
#    and time it with the existing native leaf-check (single thread).
SEL=$("$PY" -c "import json,sys;print(json.load(open('$OUT/metrics.json'))['selectedSeed'][2:])")
CKPT="$OUT/model-seed${SEL}.pt"
if [ -x build/lifetime-leaf/leaf-check ] && [ -f runs/RUN-A51D-corpus/mix-d3.states ]; then
  CMD="$PY approaches/lifetime-objective/learned-leaf/export_leaf.py --checkpoint $CKPT --out $OUT/model-seed${SEL}.d7leaf"
  echo "$CMD" | tee -a "$OUT/commands.log"
  $CMD 2>&1 | tee -a "$OUT/commands.log"
  CMD="build/lifetime-leaf/leaf-check --model $OUT/model-seed${SEL}.d7leaf --states runs/RUN-A51D-corpus/mix-d3.states --count 4096 --bench 400000 --threads 1"
  echo "$CMD" | tee -a "$OUT/commands.log"
  $CMD > "$OUT/leaf-check.log" 2>&1 || true
  cat "$OUT/leaf-check.log" | tee -a "$OUT/commands.log"
  "$PY" - "$OUT" <<'PYEOF'
import json, re, sys
out = sys.argv[1]
text = open(f"{out}/leaf-check.log").read()
m = re.search(r"([0-9.]+) us/state \(aggregate\)", text)
report = json.load(open(f"{out}/metrics.json"))
report["inferenceCost"] = {
    "tool": "build/lifetime-leaf/leaf-check --bench 400000 --threads 1",
    "microsecondsPerState": float(m.group(1)) if m else None,
    "rawLog": text.strip().splitlines(),
    "note": "informational; exported with hazardHorizon=0 so the single head lands in the lifetime slot",
}
json.dump(report, open(f"{out}/metrics.json", "w"), indent=1, sort_keys=True)
open(f"{out}/metrics.json", "a").write("\n")
PYEOF
else
  echo "inference cost: not measured (leaf-check or states file absent)" | tee -a "$OUT/commands.log"
fi
echo "# run.sh finished $(date -u +%Y-%m-%dT%H:%M:%SZ)" | tee -a "$OUT/commands.log"
