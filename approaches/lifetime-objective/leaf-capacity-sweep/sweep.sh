#!/usr/bin/env bash
# Offline capacity/training sweep of the NNUE-shaped survival leaf
# (approaches/lifetime-objective/learned-leaf/train_leaf.py, unchanged).
# No gameplay, no seed is opened: every run reads the same training corpus
# and reports held-out metrics on the same whole-origin test split.
#
# Usage: sweep.sh <run-dir>
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "${ROOT}"
RUN="${1:?run dir}"
mkdir -p "${RUN}/models"
STATES=runs/RUN-A51D-corpus/all.states
test -s "${STATES}" || { echo "missing corpus ${STATES}" >&2; exit 2; }
sha256sum "${STATES}" approaches/lifetime-objective/learned-leaf/train_leaf.py \
          approaches/lifetime-objective/learned-leaf/leaf_features.py \
          approaches/lifetime-objective/afterstate-net/dataset.py > "${RUN}/inputs.sha256"
# shellcheck disable=SC1091
source approaches/lifetime-objective/gpu/activate.sh
export OMP_NUM_THREADS=2 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
python - <<'PY' | tee "${RUN}/environment.txt"
import torch, sys
print("python", sys.version.split()[0], "torch", torch.__version__, "hip", torch.version.hip, "device", torch.cuda.get_device_name(0) if torch.cuda.is_available() else "cpu")
PY

# tag hidden mid epochs lr seed  -- the baseline is h64-m32-e10-lr3e-3 seed 42282 = 0xA52A (finding-08)
GRID=$(cat <<'G'
h64-m32-e10-lr3e3-s0   64  32 10 3e-3 42282
h64-m32-e10-lr3e3-s1   64  32 10 3e-3 42283
h64-m32-e10-lr3e3-s2   64  32 10 3e-3 42284
h32-m32-e10-lr3e3-s0   32  32 10 3e-3 42282
h128-m32-e10-lr3e3-s0 128  32 10 3e-3 42282
h256-m32-e10-lr3e3-s0 256  32 10 3e-3 42282
h64-m16-e10-lr3e3-s0   64  16 10 3e-3 42282
h64-m64-e10-lr3e3-s0   64  64 10 3e-3 42282
h64-m32-e10-lr1e3-s0   64  32 10 1e-3 42282
h64-m32-e20-lr3e3-s0   64  32 20 3e-3 42282
h128-m32-e20-lr3e3-s0 128  32 20 3e-3 42282
h256-m64-e20-lr3e3-s0 256  64 20 3e-3 42282
h512-m64-e20-lr3e3-s0 512  64 20 3e-3 42282
G
)
echo "${GRID}" | while read -r tag hidden mid epochs lr seed; do
  [ -z "${tag}" ] && continue
  if [ -s "${RUN}/models/${tag}.json" ]; then echo "skip ${tag} (done)"; continue; fi
  echo "== ${tag} $(date -Is)"
  python approaches/lifetime-objective/learned-leaf/train_leaf.py \
    --states "${STATES}" --out "${RUN}/models/${tag}" \
    --hidden "${hidden}" --mid "${mid}" --epochs "${epochs}" --lr "${lr}" --seed "${seed}" \
    --batch 8192 --device cuda > "${RUN}/models/${tag}.log" 2>&1 || { echo "FAILED ${tag}"; cat "${RUN}/models/${tag}.log" | tail -5; }
  tail -c 600 "${RUN}/models/${tag}.log" | grep -o '"lifetimePearson": [0-9.]*' | head -1
done
echo "sweep complete $(date -Is)"
