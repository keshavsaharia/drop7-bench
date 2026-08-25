#!/usr/bin/env bash
# Stage D0 main run on the leased seeds (SL-20260823T200000Z-a52e0000).
# Refuses to start unless OUTDIR/gates.log ends with "ALL GATES PASS".
# usage: run.sh OUTDIR [THREADS]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="${ROOT}/approaches/oracle-curriculum/hpool-d0"
BIN="${ROOT}/build/hpool-d0"
OUT="${1:?usage: run.sh OUTDIR [THREADS]}"
THREADS="${2:-16}"
LOG="${OUT}/run.log"

grep -q '^ALL GATES PASS$' "${OUT}/gates.log" || { echo "gates have not passed; refusing to open the lease" >&2; exit 1; }
"${HERE}/build.sh" > /dev/null
CURRENT="$(sha256sum "${HERE}/d0.cpp" | cut -d' ' -f1)"
grep -q "^${CURRENT}  " "${BIN}/sources.sha256" || { echo "d0.cpp changed since build" >&2; exit 1; }
grep -q "${CURRENT}" "${OUT}/gates.log" || { echo "gated d0.cpp hash differs from the one about to run" >&2; exit 1; }

{
  echo "hpool-d0 run start $(date -u +%Y-%m-%dT%H:%M:%SZ) threads=${THREADS}"
  echo "d0.cpp sha256 ${CURRENT}"
  START=$(date +%s)
  set +e
  "${BIN}/d0-generate" --generate --threads "${THREADS}" \
      --output "${OUT}/records.jsonl" --summary "${OUT}/generate-summary.json"
  GEN=$?
  echo "d0-generate exit ${GEN} at $(( $(date +%s) - START ))s"
  "${BIN}/d0-relabel" --relabel --threads "${THREADS}" \
      --input "${OUT}/records.jsonl" --output "${OUT}/pools.json"
  REL=$?
  echo "d0-relabel exit ${REL} at $(( $(date +%s) - START ))s"
  python3 "${HERE}/analyze.py" "${OUT}/pools.json" "${OUT}/generate-summary.json" "${OUT}/d0-result.json"
  ANA=$?
  echo "analyze exit ${ANA} at $(( $(date +%s) - START ))s"
  echo "hpool-d0 run end $(date -u +%Y-%m-%dT%H:%M:%SZ) wall $(( $(date +%s) - START ))s generate=${GEN} relabel=${REL} analyze=${ANA}"
} 2>&1 | tee "${LOG}"
