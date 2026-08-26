#!/usr/bin/env bash
# One-command, budget-bounded EC2 orchestrator for a fixed public-root matrix.
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
usage: run-ec2-matrix.sh ROUND --budget CENTS [options]

Required:
  ROUND                  scripted round id, for example gauntlet-01
  --budget CENTS         all-in operator budget in integer US cents

Options:
  --plan                 package and perform read-only AWS checks; do not upload or launch
  --depths LIST          completed depths (default: 4,5,6,7)
  --strata LIST          reveal strata (default: 7)
  --roots FILE           public `s ...` roots; default is ROUND's initial public position
  --root-limit N         maximum roots (default: 1)
  --region REGION        AWS region (default: us-east-2)
  --instance-type TYPE   EC2 instance type (default: hpc7a.96xlarge)
  --bucket BUCKET        S3 bucket (default: drop7-bench-data)
  --threads N            solver workers (default: 192)
  --no-capacity-reservation
EOF
}

if [[ $# -lt 1 ]]; then
  usage
  exit 2
fi
ROUND_ID="$1"
shift

BUDGET_CENTS=""
PLAN=0
DEPTHS="${DROP7_MATRIX_DEPTHS:-4,5,6,7}"
STRATA="${DROP7_MATRIX_STRATA:-7}"
ROOTS_SOURCE=""
ROOT_LIMIT="${DROP7_MATRIX_ROOT_LIMIT:-1}"
AWS_REGION_VALUE="${DROP7_AWS_REGION:-us-east-2}"
INSTANCE_TYPE="${DROP7_INSTANCE_TYPE:-hpc7a.96xlarge}"
S3_BUCKET="${DROP7_S3_BUCKET:-drop7-bench-data}"
THREADS="${DROP7_MATRIX_THREADS:-192}"
USE_CAPACITY_RESERVATION="${DROP7_USE_CAPACITY_RESERVATION:-1}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --budget)
      BUDGET_CENTS="${2:-}"
      shift 2
      ;;
    --plan)
      PLAN=1
      shift
      ;;
    --depths)
      DEPTHS="${2:-}"
      shift 2
      ;;
    --strata)
      STRATA="${2:-}"
      shift 2
      ;;
    --roots)
      ROOTS_SOURCE="${2:-}"
      shift 2
      ;;
    --root-limit)
      ROOT_LIMIT="${2:-}"
      shift 2
      ;;
    --region)
      AWS_REGION_VALUE="${2:-}"
      shift 2
      ;;
    --instance-type)
      INSTANCE_TYPE="${2:-}"
      shift 2
      ;;
    --bucket)
      S3_BUCKET="${2:-}"
      shift 2
      ;;
    --threads)
      THREADS="${2:-}"
      shift 2
      ;;
    --no-capacity-reservation)
      USE_CAPACITY_RESERVATION=0
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "unknown option: $1" >&2
      usage
      exit 2
      ;;
  esac
done

if [[ ! "${ROUND_ID}" =~ ^[a-z0-9][a-z0-9-]*$ ]]; then
  echo "ROUND must be a safe scripted-round id" >&2
  exit 2
fi
if [[ ! "${BUDGET_CENTS}" =~ ^[1-9][0-9]*$ ]]; then
  echo "--budget must be a positive integer number of US cents" >&2
  exit 2
fi
for integer_name in ROOT_LIMIT THREADS; do
  if [[ ! "${!integer_name}" =~ ^[1-9][0-9]*$ ]]; then
    echo "${integer_name} must be a positive integer" >&2
    exit 2
  fi
done
if [[ ! "${DEPTHS}" =~ ^[1-9][0-9]*(,[1-9][0-9]*)*$ \
   || ! "${STRATA}" =~ ^[1-9][0-9]*(,[1-9][0-9]*)*$ ]]; then
  echo "--depths and --strata must be comma-separated positive integers" >&2
  exit 2
fi
if [[ ! "${AWS_REGION_VALUE}" =~ ^[a-z0-9-]+$ \
   || ! "${INSTANCE_TYPE}" =~ ^[a-z0-9.]+$ \
   || ! "${S3_BUCKET}" =~ ^[a-z0-9][a-z0-9.-]+$ ]]; then
  echo "region, instance type, or bucket contains unsafe characters" >&2
  exit 2
fi
if [[ "${USE_CAPACITY_RESERVATION}" != "0" && "${USE_CAPACITY_RESERVATION}" != "1" ]]; then
  echo "DROP7_USE_CAPACITY_RESERVATION must be 0 or 1" >&2
  exit 2
fi
if [[ -z "${AWS_PROFILE:-}" ]]; then
  echo "AWS_PROFILE is required, for example AWS_PROFILE=personal-deploy" >&2
  exit 2
fi
for command_name in aws cargo git python3; do
  if ! command -v "${command_name}" >/dev/null 2>&1; then
    echo "${command_name} is required" >&2
    exit 2
  fi
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "${ROOT}"
ROUND_PATH="src/bench/rounds/${ROUND_ID}.json"
if [[ ! -f "${ROUND_PATH}" ]]; then
  echo "scripted round not found: ${ROUND_PATH}" >&2
  exit 2
fi
if [[ -n "${ROOTS_SOURCE}" && ! -f "${ROOTS_SOURCE}" ]]; then
  echo "roots file not found: ${ROOTS_SOURCE}" >&2
  exit 2
fi

CALLER_ACCOUNT="$(aws sts get-caller-identity --query Account --output text)"
echo "aws_profile=${AWS_PROFILE} account=${CALLER_ACCOUNT} region=${AWS_REGION_VALUE}"
aws s3api head-bucket --bucket "${S3_BUCKET}" >/dev/null
BUCKET_REGION="$(aws s3api get-bucket-location \
  --bucket "${S3_BUCKET}" --query LocationConstraint --output text)"
if [[ "${BUCKET_REGION}" == "None" ]]; then
  BUCKET_REGION="us-east-1"
fi
if [[ "${BUCKET_REGION}" != "${AWS_REGION_VALUE}" ]]; then
  echo "bucket ${S3_BUCKET} is in ${BUCKET_REGION}, not ${AWS_REGION_VALUE}" >&2
  exit 2
fi

query_hourly_price() {
  if [[ -n "${DROP7_HOURLY_PRICE_USD:-}" ]]; then
    printf '%s\n' "${DROP7_HOURLY_PRICE_USD}"
    return
  fi
  price_response="$(aws pricing get-products \
    --region us-east-1 \
    --service-code AmazonEC2 \
    --filters \
      "Type=TERM_MATCH,Field=instanceType,Value=${INSTANCE_TYPE}" \
      "Type=TERM_MATCH,Field=regionCode,Value=${AWS_REGION_VALUE}" \
      'Type=TERM_MATCH,Field=operatingSystem,Value=Linux' \
      'Type=TERM_MATCH,Field=tenancy,Value=Shared' \
      'Type=TERM_MATCH,Field=preInstalledSw,Value=NA' \
      'Type=TERM_MATCH,Field=capacitystatus,Value=Used' \
    --max-results 100 --output json)"
  PRICE_RESPONSE="${price_response}" python3 -c '
import json, os
payload = json.loads(os.environ["PRICE_RESPONSE"])
prices = set()
for encoded in payload.get("PriceList", []):
    offer = json.loads(encoded)
    for term in offer.get("terms", {}).get("OnDemand", {}).values():
        for dimension in term.get("priceDimensions", {}).values():
            if dimension.get("unit") == "Hrs" and dimension.get("beginRange") == "0":
                value = dimension.get("pricePerUnit", {}).get("USD")
                if value is not None and float(value) > 0:
                    prices.add(value)
if len(prices) != 1:
    raise SystemExit(f"expected exactly one positive hourly price, found {sorted(prices)}")
print(next(iter(prices)))
'
}

HOURLY_PRICE_USD="$(query_hourly_price)"
if [[ ! "${HOURLY_PRICE_USD}" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
  echo "could not resolve a positive hourly price: ${HOURLY_PRICE_USD}" >&2
  exit 2
fi

read -r SAFETY_CENTS COMPUTE_CAP_CENTS MAX_WALL_SECONDS COMPUTE_CAP_USD PROJECTED_USD <<< "$(
  python3 -c '
from decimal import Decimal, ROUND_CEILING, ROUND_FLOOR
import sys
budget = int(sys.argv[1])
hourly = Decimal(sys.argv[2])
if hourly <= 0:
    raise SystemExit("hourly price must be positive")
safety = max(500, int((Decimal(budget) * Decimal("0.10")).to_integral_value(rounding=ROUND_CEILING)))
compute = budget - safety
if compute <= 0:
    raise SystemExit("budget is too small after the $5/10% safety reserve")
seconds = int(((Decimal(compute) / Decimal(100)) / hourly * Decimal(3600)).to_integral_value(rounding=ROUND_FLOOR))
if seconds < 600:
    raise SystemExit("budget buys less than the 10-minute minimum wall bound")
projected = hourly * Decimal(seconds) / Decimal(3600)
print(safety, compute, seconds, f"{Decimal(compute) / Decimal(100):.2f}", f"{projected:.6f}")
' "${BUDGET_CENTS}" "${HOURLY_PRICE_USD}"
)"
echo "budget_cents=${BUDGET_CENTS} safety_cents=${SAFETY_CENTS} compute_cap_cents=${COMPUTE_CAP_CENTS}"
echo "hourly_usd=${HOURLY_PRICE_USD} wall_seconds=${MAX_WALL_SECONDS} projected_instance_usd=${PROJECTED_USD}"

read -r DEFAULT_VCPUS DEFAULT_CORES THREADS_PER_CORE ARCHITECTURE <<< "$(
  aws ec2 describe-instance-types --region "${AWS_REGION_VALUE}" \
    --instance-types "${INSTANCE_TYPE}" \
    --query 'InstanceTypes[0].[VCpuInfo.DefaultVCpus,VCpuInfo.DefaultCores,VCpuInfo.DefaultThreadsPerCore,ProcessorInfo.SupportedArchitectures[0]]' \
    --output text
)"
if [[ "${ARCHITECTURE}" != "x86_64" || "${DEFAULT_CORES}" -lt "${THREADS}" ]]; then
  echo "${INSTANCE_TYPE} exposes ${ARCHITECTURE} ${DEFAULT_CORES} cores; requested ${THREADS} x86 workers" >&2
  exit 2
fi
VCPU_QUOTA="$(aws service-quotas list-service-quotas \
  --region "${AWS_REGION_VALUE}" --service-code ec2 \
  --query "Quotas[?QuotaName=='Running On-Demand HPC instances'].Value | [0]" \
  --output json)"
if [[ ! "${VCPU_QUOTA}" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
  echo "cannot resolve the Running On-Demand HPC instances quota" >&2
  exit 2
fi
if ! awk -v quota="${VCPU_QUOTA}" -v need="${DEFAULT_VCPUS}" \
  'BEGIN { exit !(quota >= need) }'; then
  echo "HPC quota ${VCPU_QUOTA} vCPUs is below the ${DEFAULT_VCPUS} required" >&2
  exit 2
fi

RUN_SUFFIX="$(python3 -c 'import secrets; print(secrets.token_hex(4))')"
RUN_ID="RUN-$(date -u +%Y%m%dT%H%M%SZ)-${ROUND_ID}-${RUN_SUFFIX}"
INPUT_DIR="approaches/fair-expectimax/rust-engine/cluster-input/${RUN_ID}"
LOCAL_RUN_DIR="runs/${RUN_ID}"
PACKAGE_DIR="${LOCAL_RUN_DIR}/package"
DOWNLOAD_DIR="${LOCAL_RUN_DIR}/downloaded"
mkdir -p "${INPUT_DIR}" "${PACKAGE_DIR}" "${DOWNLOAD_DIR}"

ROOTS_PATH="${INPUT_DIR}/roots.txt"
SOURCE_GIT_COMMIT_PATH="${INPUT_DIR}/source-git-commit.txt"
SOURCE_GIT_STATUS_PATH="${INPUT_DIR}/source-git-status.txt"
if [[ -n "${ROOTS_SOURCE}" ]]; then
  cp "${ROOTS_SOURCE}" "${ROOTS_PATH}"
else
  python3 -c '
import json, sys
round_path, output_path = sys.argv[1:]
with open(round_path) as handle:
    round_data = json.load(handle)
if round_data.get("format") != "drop7-scripted-round-v1" or not round_data.get("discs"):
    raise SystemExit("malformed scripted round")
next_disc = int(round_data["discs"][0])
if not 1 <= next_disc <= 7:
    raise SystemExit("invalid initial visible disc")
board = "0" * 42 + "8" * 7
with open(output_path, "w") as handle:
    handle.write(f"s {board} {next_disc} 5 1 0\n")
' "${ROUND_PATH}" "${ROOTS_PATH}"
fi
python3 -c '
import sys
valid = 0
with open(sys.argv[1]) as handle:
    for line_number, raw in enumerate(handle, 1):
        if not raw.startswith("s "):
            continue
        fields = raw.split()
        if len(fields) != 6:
            raise SystemExit(f"{sys.argv[1]}:{line_number}: malformed root")
        _, board, next_disc, rise, level, terminal = fields
        if len(board) != 49 or any(cell not in "0123456789" for cell in board):
            raise SystemExit(f"{sys.argv[1]}:{line_number}: board must contain 49 serialized cells")
        if not 1 <= int(next_disc) <= 7 or not 0 <= int(rise) <= 5 or int(level) < 1:
            raise SystemExit(f"{sys.argv[1]}:{line_number}: root field out of range")
        if terminal not in ("0", "1"):
            raise SystemExit(f"{sys.argv[1]}:{line_number}: terminal flag must be 0 or 1")
        if terminal == "0":
            valid += 1
if valid == 0:
    raise SystemExit(f"{sys.argv[1]} contains no non-terminal public root")
' "${ROOTS_PATH}"

# The source archive deliberately excludes .git. Embed the exact revision and
# dirty-tree description beside the matrix inputs so the remote result can
# retain its source identity without needing repository metadata.
git rev-parse HEAD > "${SOURCE_GIT_COMMIT_PATH}"
git status --short > "${SOURCE_GIT_STATUS_PATH}"

MATRIX_CONFIG="${INPUT_DIR}/matrix.env"
cat > "${MATRIX_CONFIG}" <<EOF
MATRIX_ROOTS=${ROOTS_PATH}
MATRIX_SOURCE_GIT_COMMIT=${SOURCE_GIT_COMMIT_PATH}
MATRIX_SOURCE_GIT_STATUS=${SOURCE_GIT_STATUS_PATH}
MATRIX_OUTPUT=runs/${RUN_ID}/search-matrix/analytics.jsonl
MATRIX_DEPTHS=${DEPTHS}
MATRIX_STRATA=${STRATA}
MATRIX_LEAVES=fair
MATRIX_SCHEDULER=frontier
MATRIX_THREADS=${THREADS}
MATRIX_CACHE_ENTRIES_PER_WORKER=262144
MATRIX_SPLIT_PLIES=auto
MATRIX_ROOT_LIMIT=${ROOT_LIMIT}
MATRIX_MAX_FRONTIER_TASKS=1000000
MATRIX_MAX_HOST_BYTES=8589934592
EOF

MAX_DEPTH="$(tr ',' '\n' <<< "${DEPTHS}" | sort -n | tail -1)"
MAX_STRATA="$(tr ',' '\n' <<< "${STRATA}" | sort -n | tail -1)"
cargo build --release \
  --manifest-path approaches/fair-expectimax/rust-engine/Cargo.toml \
  --bin plan
approaches/fair-expectimax/rust-engine/target/release/plan \
  --depth "${MAX_DEPTH}" --strata "${MAX_STRATA}" \
  --threads "${THREADS}" --cache 262144 \
  --split-plies auto --max-host-bytes 8589934592 \
  > "${LOCAL_RUN_DIR}/resource-plan.json"

python3 -c '
import json, sys
keys = ["runId", "round", "budgetCents", "safetyCents", "computeCapCents", "hourlyPriceUsd", "maxWallSeconds", "instanceType", "region", "depths", "strata", "threads", "roots"]
values = sys.argv[2:]
payload = dict(zip(keys, values))
for key in ("budgetCents", "safetyCents", "computeCapCents", "maxWallSeconds", "threads", "roots"):
    payload[key] = int(payload[key])
with open(sys.argv[1], "w") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
' "${LOCAL_RUN_DIR}/request.json" "${RUN_ID}" "${ROUND_ID}" "${BUDGET_CENTS}" \
  "${SAFETY_CENTS}" "${COMPUTE_CAP_CENTS}" "${HOURLY_PRICE_USD}" \
  "${MAX_WALL_SECONDS}" "${INSTANCE_TYPE}" "${AWS_REGION_VALUE}" \
  "${DEPTHS}" "${STRATA}" "${THREADS}" "${ROOT_LIMIT}"

approaches/fair-expectimax/rust-engine/cluster/package-source.sh \
  --local "${PACKAGE_DIR}"
SOURCE_SHA256="$(awk '{print $1}' "${PACKAGE_DIR}/drop7-source.tar.gz.sha256")"
python3 -c '
import json, sys
with open(sys.argv[1]) as handle:
    payload = json.load(handle)
payload["sourceSha256"] = sys.argv[2]
with open(sys.argv[1], "w") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
' "${LOCAL_RUN_DIR}/request.json" "${SOURCE_SHA256}"
mkdir -p "${LOCAL_RUN_DIR}/input"
cp "${ROOTS_PATH}" "${LOCAL_RUN_DIR}/input/roots.txt"
cp "${MATRIX_CONFIG}" "${LOCAL_RUN_DIR}/input/matrix.env"
cp "${SOURCE_GIT_COMMIT_PATH}" "${LOCAL_RUN_DIR}/input/source-git-commit.txt"
cp "${SOURCE_GIT_STATUS_PATH}" "${LOCAL_RUN_DIR}/input/source-git-status.txt"
# The input must exist while packaging because it is embedded in the source
# archive. Retain a copy under ignored runs/, then remove the transient
# untracked source-tree directory so it cannot leak into a later package.
rm -f "${ROOTS_PATH}" "${MATRIX_CONFIG}" \
  "${SOURCE_GIT_COMMIT_PATH}" "${SOURCE_GIT_STATUS_PATH}"
rmdir "${INPUT_DIR}"

if (( PLAN == 1 )); then
  OFFERED_AZS="$(aws ec2 describe-instance-type-offerings \
    --region "${AWS_REGION_VALUE}" --location-type availability-zone \
    --filters "Name=instance-type,Values=${INSTANCE_TYPE}" \
    --query 'InstanceTypeOfferings[].Location' --output text)"
  echo "run_id=${RUN_ID}"
  echo "offered_azs=${OFFERED_AZS}"
  echo "source_sha256=${SOURCE_SHA256}"
  echo "plan only: local package created; no S3 object, reservation, or EC2 instance created"
  exit 0
fi

read -r AVAILABILITY_ZONE SUBNET_ID SECURITY_GROUP_ID INSTANCE_PROFILE <<< "$(
  approaches/fair-expectimax/rust-engine/cluster/ensure-aws-infrastructure.sh \
    --region "${AWS_REGION_VALUE}" --instance-type "${INSTANCE_TYPE}" \
    --bucket "${S3_BUCKET}"
)"

S3_RUN_PREFIX="s3://${S3_BUCKET}/ec2/${RUN_ID}"
SOURCE_S3_URI="${S3_RUN_PREFIX}/source/drop7-source.tar.gz"
aws s3 cp "${PACKAGE_DIR}/drop7-source.tar.gz" "${SOURCE_S3_URI}" \
  --only-show-errors
aws s3 cp "${PACKAGE_DIR}/drop7-source.tar.gz.sha256" \
  "${S3_RUN_PREFIX}/source/drop7-source.tar.gz.sha256" --only-show-errors
aws s3 cp "${LOCAL_RUN_DIR}/request.json" \
  "${S3_RUN_PREFIX}/request.json" --only-show-errors
aws s3 cp "${LOCAL_RUN_DIR}/resource-plan.json" \
  "${S3_RUN_PREFIX}/resource-plan.json" --only-show-errors
aws s3 cp "${LOCAL_RUN_DIR}/input" "${S3_RUN_PREFIX}/input/" \
  --recursive --only-show-errors
echo "source_s3_uri=${SOURCE_S3_URI}"
echo "source_sha256=${SOURCE_SHA256}"

EC2_CONFIG="${LOCAL_RUN_DIR}/ec2-run.env"
cat > "${EC2_CONFIG}" <<EOF
AWS_REGION=${AWS_REGION_VALUE}
AWS_AVAILABILITY_ZONE=${AVAILABILITY_ZONE}
AWS_INSTANCE_TYPE=${INSTANCE_TYPE}
AWS_SUBNET_ID=${SUBNET_ID}
AWS_SECURITY_GROUP_ID=${SECURITY_GROUP_ID}
AWS_IAM_INSTANCE_PROFILE=${INSTANCE_PROFILE}
AWS_SOURCE_S3_URI=${SOURCE_S3_URI}
AWS_SOURCE_SHA256=${SOURCE_SHA256}
AWS_RESULT_S3_PREFIX=${S3_RUN_PREFIX}/results
MATRIX_CONFIG_PATH=${MATRIX_CONFIG}
RUN_ID=${RUN_ID}
MAX_WALL_SECONDS=${MAX_WALL_SECONDS}
ASSERTED_HOURLY_PRICE_USD=${HOURLY_PRICE_USD}
MAX_INSTANCE_COST_USD=${COMPUTE_CAP_USD}
MIN_VCPUS=${THREADS}
MIN_PHYSICAL_CORES=${THREADS}
AWS_VCPU_QUOTA_NAME='Running On-Demand HPC instances'
ROOT_VOLUME_GIB=100
USE_CAPACITY_RESERVATION=${USE_CAPACITY_RESERVATION}
EOF

approaches/fair-expectimax/rust-engine/cluster/provision-ec2.sh \
  --plan "${EC2_CONFIG}" | tee "${LOCAL_RUN_DIR}/aws-plan.log"

INSTANCE_ID=""
CAPACITY_RESERVATION_ID=""
CLEANUP_NEEDED=1
cleanup_cloud() {
  status=$?
  if (( CLEANUP_NEEDED == 1 )); then
    set +e
    if [[ -z "${INSTANCE_ID}" && -f "${LOCAL_RUN_DIR}/launch.log" ]]; then
      INSTANCE_ID="$(awk -F= '$1 == "instance_id" {print $2}' "${LOCAL_RUN_DIR}/launch.log")"
    fi
    if [[ -z "${CAPACITY_RESERVATION_ID}" && -f "${LOCAL_RUN_DIR}/launch.log" ]]; then
      CAPACITY_RESERVATION_ID="$(awk -F= '$1 == "capacity_reservation_id" {print $2}' "${LOCAL_RUN_DIR}/launch.log")"
    fi
    if [[ "${INSTANCE_ID}" =~ ^i-[a-f0-9]+$ ]]; then
      aws ec2 terminate-instances --region "${AWS_REGION_VALUE}" \
        --instance-ids "${INSTANCE_ID}" >/dev/null 2>&1
    fi
    if [[ "${CAPACITY_RESERVATION_ID}" =~ ^cr-[a-f0-9]+$ ]]; then
      aws ec2 cancel-capacity-reservation --region "${AWS_REGION_VALUE}" \
        --capacity-reservation-id "${CAPACITY_RESERVATION_ID}" >/dev/null 2>&1
    fi
  fi
  exit "${status}"
}
trap cleanup_cloud EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

approaches/fair-expectimax/rust-engine/cluster/provision-ec2.sh \
  "${EC2_CONFIG}" | tee "${LOCAL_RUN_DIR}/launch.log"
INSTANCE_ID="$(awk -F= '$1 == "instance_id" {print $2}' "${LOCAL_RUN_DIR}/launch.log")"
CAPACITY_RESERVATION_ID="$(awk -F= '$1 == "capacity_reservation_id" {print $2}' "${LOCAL_RUN_DIR}/launch.log")"
if [[ ! "${INSTANCE_ID}" =~ ^i-[a-f0-9]+$ ]]; then
  echo "launcher returned no valid instance id" >&2
  exit 2
fi
echo "launched ${INSTANCE_ID}; waiting for automatic termination"

RESULT_RUN_URI="${S3_RUN_PREFIX}/results/${RUN_ID}"
DEADLINE_EPOCH="$(( $(date +%s) + MAX_WALL_SECONDS + 1800 ))"
POLL_COUNT=0
while true; do
  INSTANCE_STATE="$(aws ec2 describe-instances --region "${AWS_REGION_VALUE}" \
    --instance-ids "${INSTANCE_ID}" \
    --query 'Reservations[0].Instances[0].State.Name' --output text 2>/dev/null || true)"
  if [[ "${INSTANCE_STATE}" == "terminated" ]]; then
    break
  fi
  if (( $(date +%s) >= DEADLINE_EPOCH )); then
    echo "local deadline exceeded; terminating ${INSTANCE_ID}" >&2
    aws ec2 terminate-instances --region "${AWS_REGION_VALUE}" \
      --instance-ids "${INSTANCE_ID}" >/dev/null
    break
  fi
  if (( POLL_COUNT % 2 == 0 )); then
    if aws s3 cp "${RESULT_RUN_URI}/live/utilization.csv" \
      "${LOCAL_RUN_DIR}/live-utilization.csv" --only-show-errors 2>/dev/null; then
      LAST_SAMPLE="$(tail -n 1 "${LOCAL_RUN_DIR}/live-utilization.csv")"
      echo "instance=${INSTANCE_ID} state=${INSTANCE_STATE} utilization=${LAST_SAMPLE}"
    else
      echo "instance=${INSTANCE_ID} state=${INSTANCE_STATE} utilization=pending"
    fi
  fi
  POLL_COUNT="$((POLL_COUNT + 1))"
  sleep 30
done

while true; do
  INSTANCE_STATE="$(aws ec2 describe-instances --region "${AWS_REGION_VALUE}" \
    --instance-ids "${INSTANCE_ID}" \
    --query 'Reservations[0].Instances[0].State.Name' --output text 2>/dev/null || true)"
  [[ "${INSTANCE_STATE}" == "terminated" ]] && break
  sleep 10
done
if [[ -n "${CAPACITY_RESERVATION_ID}" && "${CAPACITY_RESERVATION_ID}" != "none" ]]; then
  aws ec2 cancel-capacity-reservation --region "${AWS_REGION_VALUE}" \
    --capacity-reservation-id "${CAPACITY_RESERVATION_ID}" >/dev/null 2>&1 || true
fi
CLEANUP_NEEDED=0

aws s3 sync "${RESULT_RUN_URI}/" "${DOWNLOAD_DIR}/" --only-show-errors
echo "instance_state=terminated"
echo "results_s3=${RESULT_RUN_URI}/"
echo "results_local=${DOWNLOAD_DIR}/"
if [[ -f "${DOWNLOAD_DIR}/utilization-summary.json" ]]; then
  echo "utilization_summary:"
  python3 -m json.tool "${DOWNLOAD_DIR}/utilization-summary.json"
fi
if [[ -f "${DOWNLOAD_DIR}/analytics.jsonl" ]]; then
  python3 -c '
import json, sys
groups = {}
invalid = 0
with open(sys.argv[1]) as handle:
    for raw in handle:
        try:
            row = json.loads(raw)
        except json.JSONDecodeError:
            invalid += 1
            continue
        if row.get("recordType") != "decision":
            continue
        key = (row["root"], row["leaf"], row["strata"])
        groups.setdefault(key, []).append(row)
summary = {
    "format": "drop7-downloaded-matrix-summary-v1",
    "invalidJsonLines": invalid,
    "groups": [],
}
for key in sorted(groups):
    rows = sorted(groups[key], key=lambda row: row["depth"])
    deepest = rows[-1]
    summary["groups"].append({
        "root": key[0],
        "leaf": key[1],
        "strata": key[2],
        "completedDepths": [row["depth"] for row in rows],
        "deepestCompletedDepth": deepest["depth"],
        "selectedActionAtDeepest": deepest["selectedAction"],
        "deepestWallSeconds": deepest["metrics"]["wallSeconds"],
        "deepestWorkerBusyFraction": deepest["metrics"]["workerBusyFraction"],
    })
with open(sys.argv[2], "w") as handle:
    json.dump(summary, handle, indent=2, sort_keys=True)
    handle.write("\n")
' "${DOWNLOAD_DIR}/analytics.jsonl" "${DOWNLOAD_DIR}/matrix-summary.json"
  aws s3 cp "${DOWNLOAD_DIR}/matrix-summary.json" \
    "${RESULT_RUN_URI}/matrix-summary.json" --only-show-errors
  echo "matrix_summary:"
  python3 -m json.tool "${DOWNLOAD_DIR}/matrix-summary.json"
fi

INSTANCE_EXIT_CODE=2
if [[ -f "${DOWNLOAD_DIR}/instance-exit-code.txt" ]]; then
  INSTANCE_EXIT_CODE="$(tr -d '[:space:]' < "${DOWNLOAD_DIR}/instance-exit-code.txt")"
fi
if [[ ! "${INSTANCE_EXIT_CODE}" =~ ^[0-9]+$ ]]; then
  INSTANCE_EXIT_CODE=2
fi
exit "${INSTANCE_EXIT_CODE}"
