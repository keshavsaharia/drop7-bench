#!/usr/bin/env bash
# Runs inside the provisioned instance after the source archive is verified.
set -euo pipefail

if [[ $# -ne 4 ]]; then
  echo "usage: $0 <matrix.env> <s3://result-prefix> <wall-seconds> <run-id>" >&2
  exit 2
fi

MATRIX_CONFIG="$1"
RESULT_S3_PREFIX="${2%/}"
WALL_SECONDS="$3"
RUN_ID="$4"
if [[ ! "${RESULT_S3_PREFIX}" =~ ^s3://[A-Za-z0-9][A-Za-z0-9._/-]*$ ]]; then
  echo "invalid result S3 prefix" >&2
  exit 2
fi
if [[ ! "${WALL_SECONDS}" =~ ^[1-9][0-9]*$ ]]; then
  echo "wall seconds must be positive" >&2
  exit 2
fi

# shellcheck disable=SC1090
source "${MATRIX_CONFIG}"
: "${MATRIX_OUTPUT:?matrix config needs MATRIX_OUTPUT}"
OUT_DIR="$(dirname "${MATRIX_OUTPUT}")"
mkdir -p "${OUT_DIR}"
UTILIZATION_CSV="${OUT_DIR}/utilization.csv"
UTILIZATION_SUMMARY="${OUT_DIR}/utilization-summary.json"
LIVE_UTILIZATION_URI="${RESULT_S3_PREFIX}/${RUN_ID}/live/utilization.csv"
UTILIZATION_PID=""

sample_utilization() {
  echo 'timestamp,cpuBusyPercent,memoryUsedBytes,memoryAvailableBytes,memoryTotalBytes,load1,load5,load15,runnableProcesses,totalProcesses' > "${UTILIZATION_CSV}"
  previous_total=0
  previous_idle=0
  sample_count=0
  while true; do
    read -r _ cpu_user cpu_nice cpu_system cpu_idle cpu_iowait cpu_irq cpu_softirq cpu_steal _ < /proc/stat
    cpu_total="$((cpu_user + cpu_nice + cpu_system + cpu_idle + cpu_iowait + cpu_irq + cpu_softirq + cpu_steal))"
    cpu_idle_all="$((cpu_idle + cpu_iowait))"
    if (( previous_total == 0 || cpu_total <= previous_total )); then
      previous_total="${cpu_total}"
      previous_idle="${cpu_idle_all}"
      sleep 10
      continue
    fi
    delta_total="$((cpu_total - previous_total))"
    delta_idle="$((cpu_idle_all - previous_idle))"
    cpu_busy_percent="$(awk -v total="${delta_total}" -v idle="${delta_idle}" \
      'BEGIN { printf "%.3f", 100.0 * (total - idle) / total }')"
    previous_total="${cpu_total}"
    previous_idle="${cpu_idle_all}"

    memory_total_kib="$(awk '$1 == "MemTotal:" {print $2}' /proc/meminfo)"
    memory_available_kib="$(awk '$1 == "MemAvailable:" {print $2}' /proc/meminfo)"
    memory_total_bytes="$((memory_total_kib * 1024))"
    memory_available_bytes="$((memory_available_kib * 1024))"
    memory_used_bytes="$((memory_total_bytes - memory_available_bytes))"
    read -r load1 load5 load15 process_counts _ < /proc/loadavg
    runnable_processes="${process_counts%/*}"
    total_processes="${process_counts#*/}"
    timestamp="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
      "${timestamp}" "${cpu_busy_percent}" "${memory_used_bytes}" \
      "${memory_available_bytes}" "${memory_total_bytes}" \
      "${load1}" "${load5}" "${load15}" \
      "${runnable_processes}" "${total_processes}" >> "${UTILIZATION_CSV}"

    sample_count="$((sample_count + 1))"
    if (( sample_count % 6 == 0 )); then
      aws s3 cp "${UTILIZATION_CSV}" "${LIVE_UTILIZATION_URI}" \
        --only-show-errors >/dev/null 2>&1 || true
    fi
    sleep 10
  done
}

stop_utilization_sampler() {
  if [[ -n "${UTILIZATION_PID}" ]]; then
    kill "${UTILIZATION_PID}" >/dev/null 2>&1 || true
    wait "${UTILIZATION_PID}" >/dev/null 2>&1 || true
    UTILIZATION_PID=""
  fi
}

summarize_utilization() {
  python3 -c '
import csv, json, sys
rows = []
with open(sys.argv[1], newline="") as handle:
    for row in csv.DictReader(handle):
        rows.append(row)
def mean(key):
    return sum(float(row[key]) for row in rows) / len(rows) if rows else None
def maximum(key):
    return max(float(row[key]) for row in rows) if rows else None
def minimum(key):
    return min(float(row[key]) for row in rows) if rows else None
payload = {
    "format": "drop7-host-utilization-summary-v1",
    "samples": len(rows),
    "samplePeriodSeconds": 10,
    "startedAt": rows[0]["timestamp"] if rows else None,
    "endedAt": rows[-1]["timestamp"] if rows else None,
    "averageCpuBusyPercent": mean("cpuBusyPercent"),
    "maximumCpuBusyPercent": maximum("cpuBusyPercent"),
    "averageMemoryUsedBytes": mean("memoryUsedBytes"),
    "peakMemoryUsedBytes": maximum("memoryUsedBytes"),
    "minimumMemoryAvailableBytes": minimum("memoryAvailableBytes"),
    "memoryTotalBytes": float(rows[-1]["memoryTotalBytes"]) if rows else None,
    "averageLoad1": mean("load1"),
    "maximumLoad1": maximum("load1"),
}
with open(sys.argv[2], "w") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
' "${UTILIZATION_CSV}" "${UTILIZATION_SUMMARY}" || true
}

upload_and_terminate() {
  status=$?
  trap - EXIT INT TERM
  set +e
  stop_utilization_sampler
  summarize_utilization
  echo "${status}" > "${OUT_DIR}/instance-exit-code.txt"
  date -u +%Y-%m-%dT%H:%M:%SZ > "${OUT_DIR}/instance-ended-at.txt"
  lscpu > "${OUT_DIR}/lscpu.txt" 2>&1
  cat /proc/meminfo > "${OUT_DIR}/meminfo.txt" 2>&1
  uname -a > "${OUT_DIR}/uname.txt" 2>&1
  aws s3 cp "${OUT_DIR}" "${RESULT_S3_PREFIX}/${RUN_ID}/" \
    --recursive --only-show-errors
  shutdown -h now
  exit "${status}"
}
trap upload_and_terminate EXIT INT TERM

date -u +%Y-%m-%dT%H:%M:%SZ > "${OUT_DIR}/instance-started-at.txt"
sample_utilization &
UTILIZATION_PID="$!"
timeout --signal=TERM --kill-after=60 "${WALL_SECONDS}" \
  approaches/fair-expectimax/rust-engine/cluster/run-matrix.sh "${MATRIX_CONFIG}" \
  > "${OUT_DIR}/run-matrix.log" 2>&1
