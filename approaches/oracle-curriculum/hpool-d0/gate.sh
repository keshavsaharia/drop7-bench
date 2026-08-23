#!/usr/bin/env bash
# CHECK gates for stage D0, run BEFORE the lease is opened.  Every seed read
# here lies inside the already-opened development probe range
# 0xa5278000-0xa52784ff (d0-generate --probe refuses any other seed).
#
#   privilege boundary  d0-relabel's symbol table contains no oracle planner
#                       or headless-tape symbol; the relabel path is a
#                       function of the public tuple only (static_assert in
#                       d0.cpp) and label fields edited in the input leave
#                       every relabel output byte-identical.
#   determinism         d0-generate at 16 and 4 threads writes identical
#                       records; d0-relabel at 1 and 8 threads, run twice,
#                       writes identical pools.
#   domain separation   static_asserts on the stream domains plus the
#                       per-state stream-separation and metadata checks of
#                       d0-relabel --self-test.
#   mirror invariance   d0-relabel --self-test: R_fair and the sibling
#                       vector of every probe state equal those of its mirror
#                       exactly (no tolerance is needed; the restart is
#                       played in the canonical frame).
#
# usage: gate.sh OUTDIR   (writes OUTDIR/gates.log and OUTDIR/gate-*.json*)
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="${ROOT}/approaches/oracle-curriculum/hpool-d0"
BIN="${ROOT}/build/hpool-d0"
OUT="${1:?usage: gate.sh OUTDIR}"
mkdir -p "${OUT}"
LOG="${OUT}/gates.log"
: > "${LOG}"
FAILURES=0

log() { echo "$*" | tee -a "${LOG}"; }
verdict() { # name ok
  if [ "$2" = "0" ]; then log "PASS $1"; else log "FAIL $1"; FAILURES=$((FAILURES + 1)); fi
}

log "hpool-d0 gates $(date -u +%Y-%m-%dT%H:%M:%SZ)"
log "probe seeds only: 0xa5278000-0xa52784ff (oracle from 0xa5278000, fair from 0xa5278100)"
"${HERE}/build.sh" >> "${LOG}" 2>&1 || { log "FAIL build"; exit 1; }
log "sources: $(tr '\n' ' ' < "${BIN}/sources.sha256" | cut -c1-400)..."

# --- privilege boundary: symbol table --------------------------------------
ORACLE_SYMS="$(nm -C "${BIN}/d0-relabel" | grep -c -E 'oracle_topology|planOracleMove|runOracleTrajectory|headlessDisc|playHeadlessMove' || true)"
GEN_SYMS="$(nm -C "${BIN}/d0-generate" | grep -c -E 'planOracleMove' || true)"
log "relabel oracle symbols: ${ORACLE_SYMS} (expect 0); generate planOracleMove symbols: ${GEN_SYMS} (expect >0)"
[ "${ORACLE_SYMS}" = "0" ] && [ "${GEN_SYMS}" != "0" ]; verdict "privilege-boundary-symbols" $?

# --- probe generation at 16 and 4 threads (determinism of the generator) ---
PROBE=(--probe --oracle-games 4 --max-moves 100 --fair-max-moves 60 --fair-seeds 16 --quota 8)
"${BIN}/d0-generate" --generate "${PROBE[@]}" --threads 16 \
  --output "${OUT}/gate-probe-t16.jsonl" --summary "${OUT}/gate-probe-t16.json" >> "${LOG}" 2>&1
verdict "probe-generate-t16" $?
"${BIN}/d0-generate" --generate "${PROBE[@]}" --threads 4 \
  --output "${OUT}/gate-probe-t4.jsonl" --summary "${OUT}/gate-probe-t4.json" >> "${LOG}" 2>&1
verdict "probe-generate-t4" $?
H16="$(sha256sum < "${OUT}/gate-probe-t16.jsonl" | cut -d' ' -f1)"
H4="$(sha256sum < "${OUT}/gate-probe-t4.jsonl" | cut -d' ' -f1)"
log "generate records sha256 t16=${H16} t4=${H4}"
[ "${H16}" = "${H4}" ]; verdict "generate-determinism-threads" $?
LEASED="$(grep -c '0xa52e' "${OUT}/gate-probe-t16.jsonl" || true)"
log "lease-range seeds in probe records: ${LEASED} (expect 0)"
[ "${LEASED}" = "0" ]; verdict "no-lease-seed-read" $?
log "probe records: $(wc -l < "${OUT}/gate-probe-t16.jsonl") (O+F), matching: $(python3 -c "import json;print(json.load(open('${OUT}/gate-probe-t16.json'))['matching'])")"

# --- relabel self-test: mirror, metadata, sibling consistency, streams -----
"${BIN}/d0-relabel" --relabel --self-test --input "${OUT}/gate-probe-t16.jsonl" --threads 16 2>&1 | tee -a "${LOG}"
[ "${PIPESTATUS[0]}" = "0" ]; verdict "relabel-self-test(mirror,metadata,sibling,domain)" $?

# --- relabel determinism across runs and thread counts ---------------------
"${BIN}/d0-relabel" --relabel --input "${OUT}/gate-probe-t16.jsonl" --threads 1 --output "${OUT}/gate-pools-t1.json" >> "${LOG}" 2>&1
"${BIN}/d0-relabel" --relabel --input "${OUT}/gate-probe-t16.jsonl" --threads 8 --output "${OUT}/gate-pools-t8a.json" >> "${LOG}" 2>&1
"${BIN}/d0-relabel" --relabel --input "${OUT}/gate-probe-t16.jsonl" --threads 8 --output "${OUT}/gate-pools-t8b.json" >> "${LOG}" 2>&1
strip_meta() { python3 -c "import json,sys;d=json.load(open(sys.argv[1]));json.dump(d['states'],sys.stdout,sort_keys=True)" "$1" | sha256sum | cut -d' ' -f1; }
R1="$(strip_meta "${OUT}/gate-pools-t1.json")"; R8A="$(strip_meta "${OUT}/gate-pools-t8a.json")"; R8B="$(strip_meta "${OUT}/gate-pools-t8b.json")"
log "relabel states sha256 t1=${R1} t8a=${R8A} t8b=${R8B}"
[ "${R1}" = "${R8A}" ] && [ "${R8A}" = "${R8B}" ]; verdict "relabel-determinism-runs-and-threads" $?

# --- label blindness: edit every label field, relabel output unchanged -----
sed -e 's/"column":[0-9-]*/"column":6/' -e 's/"d4Column":[0-9-]*/"d4Column":0/' \
    -e 's/"remainingRealised":[0-9]*/"remainingRealised":999/' -e 's/"seed":"0x[0-9a-f]*"/"seed":"0x00000000"/' \
    "${OUT}/gate-probe-t16.jsonl" > "${OUT}/gate-probe-relabelled-labels.jsonl"
"${BIN}/d0-relabel" --relabel --input "${OUT}/gate-probe-relabelled-labels.jsonl" --threads 8 --output "${OUT}/gate-pools-blind.json" >> "${LOG}" 2>&1
BLIND="$(python3 - "${OUT}/gate-pools-t8a.json" "${OUT}/gate-pools-blind.json" <<'PY'
import json, sys
a = json.load(open(sys.argv[1]))['states']; b = json.load(open(sys.argv[2]))['states']
ok = len(a) == len(b) and all(x['relabel'] == y['relabel'] and x['publicHash'] == y['publicHash'] for x, y in zip(a, b))
changed = sum(1 for x, y in zip(a, b) if x['column'] != y['column'])
print(f"{'0' if ok else '1'} labels_changed={changed} relabel_equal={ok}")
PY
)"
log "label-blindness: ${BLIND}"
[ "${BLIND%% *}" = "0" ]; verdict "privilege-boundary-label-blindness" $?

if [ "${FAILURES}" = "0" ]; then log "ALL GATES PASS"; else log "GATES FAILED: ${FAILURES}"; fi
exit "${FAILURES}"
