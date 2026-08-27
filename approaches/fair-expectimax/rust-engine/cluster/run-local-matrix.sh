#!/usr/bin/env bash
# Local depth-matrix runner: derives roots, freezes a matrix config into a
# unique run directory, and executes the same run-matrix.sh path the EC2
# instance uses. Creates no AWS resource and opens no research seed range.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
cd "${ROOT}"

usage() {
  cat >&2 <<'EOF'
usage: run-local-matrix.sh [ROUND] [options]

Positional:
  ROUND                  scripted round id (for example gauntlet-01); its
                         initial public position becomes the default root

Options (mirroring the analyze binary):
  --roots FILE           file of public `s ...` roots (replaces ROUND's root)
  --root-limit N         maximum roots read from the file (default: 1)
  --depths LIST          completed depths, comma list (default: 2,3,4,5)
  --strata LIST          reveal strata, comma list (default: 7)
  --leaf SPEC            fair or NAME=WEIGHTS_FILE; repeatable (default: fair)
  --scheduler NAME       frontier or root (default: frontier)
  --threads N            solver workers (default: all hardware threads)
  --cache N              transposition entries per worker (default: 262144)
  --split-plies SPEC     auto or a fixed frontier prefix depth (default: auto)
  --max-frontier-tasks N frontier task bound (default: 1000000)
  --max-host-bytes N     declared host memory budget (default: 8589934592)
  --run-id ID            run directory name (default: RUN-<utc>-local-<rand>)
  --plan                 build and print the per-cell resource plan; no search
EOF
}

ROUND_ID=""
if [[ $# -gt 0 && "$1" != --* ]]; then
  ROUND_ID="$1"
  shift
fi

ROOTS_SOURCE=""
ROOT_LIMIT=1
DEPTHS="2,3,4,5"
STRATA="7"
LEAVES=()
SCHEDULER="frontier"
THREADS="$(nproc)"
CACHE=262144
SPLIT_PLIES="auto"
MAX_FRONTIER_TASKS=1000000
MAX_HOST_BYTES=8589934592
RUN_ID=""
PLAN=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --roots) ROOTS_SOURCE="${2:?--roots needs a value}"; shift 2 ;;
    --root-limit) ROOT_LIMIT="${2:?--root-limit needs a value}"; shift 2 ;;
    --depths) DEPTHS="${2:?--depths needs a value}"; shift 2 ;;
    --strata) STRATA="${2:?--strata needs a value}"; shift 2 ;;
    --leaf) LEAVES+=("${2:?--leaf needs a value}"); shift 2 ;;
    --scheduler) SCHEDULER="${2:?--scheduler needs a value}"; shift 2 ;;
    --threads) THREADS="${2:?--threads needs a value}"; shift 2 ;;
    --cache) CACHE="${2:?--cache needs a value}"; shift 2 ;;
    --split-plies) SPLIT_PLIES="${2:?--split-plies needs a value}"; shift 2 ;;
    --max-frontier-tasks) MAX_FRONTIER_TASKS="${2:?--max-frontier-tasks needs a value}"; shift 2 ;;
    --max-host-bytes) MAX_HOST_BYTES="${2:?--max-host-bytes needs a value}"; shift 2 ;;
    --run-id) RUN_ID="${2:?--run-id needs a value}"; shift 2 ;;
    --plan) PLAN=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage; exit 2 ;;
  esac
done

if [[ ${#LEAVES[@]} -eq 0 ]]; then
  LEAVES=(fair)
fi

if [[ ${PLAN} -eq 1 ]]; then
  RUSTFLAGS="-C target-cpu=native" cargo build --release \
    --manifest-path approaches/fair-expectimax/rust-engine/Cargo.toml \
    --bin plan
  IFS=',' read -r -a DEPTH_LIST <<< "${DEPTHS}"
  IFS=',' read -r -a STRATA_LIST <<< "${STRATA}"
  for depth in "${DEPTH_LIST[@]}"; do
    for stratum in "${STRATA_LIST[@]}"; do
      approaches/fair-expectimax/rust-engine/target/release/plan \
        --depth "${depth}" --strata "${stratum}" --threads "${THREADS}" \
        --cache "${CACHE}" --split-plies "${SPLIT_PLIES}" \
        --max-frontier-tasks "${MAX_FRONTIER_TASKS}" \
        --max-host-bytes "${MAX_HOST_BYTES}"
    done
  done
  exit 0
fi

if [[ -z "${ROUND_ID}" && -z "${ROOTS_SOURCE}" ]]; then
  echo "a ROUND id or --roots FILE is required (see --help)" >&2
  exit 2
fi

if [[ -z "${RUN_ID}" ]]; then
  RUN_ID="RUN-$(date -u +%Y%m%dT%H%M%SZ)-local-$(python3 -c 'import secrets; print(secrets.token_hex(4))')"
fi
RUN_DIR="runs/${RUN_ID}"
if [[ -e "${RUN_DIR}" ]]; then
  echo "run directory already exists: ${RUN_DIR}" >&2
  exit 2
fi
mkdir -p "${RUN_DIR}"

ROOTS_PATH="${RUN_DIR}/roots.txt"
if [[ -n "${ROOTS_SOURCE}" ]]; then
  if [[ ! -f "${ROOTS_SOURCE}" ]]; then
    echo "roots file not found: ${ROOTS_SOURCE}" >&2
    exit 2
  fi
  cp "${ROOTS_SOURCE}" "${ROOTS_PATH}"
else
  ROUND_PATH="src/bench/rounds/${ROUND_ID}.json"
  if [[ ! -f "${ROUND_PATH}" ]]; then
    echo "scripted round not found: ${ROUND_PATH}" >&2
    exit 2
  fi
  python3 - "${ROUND_PATH}" "${ROOTS_PATH}" <<'EOF'
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
EOF
fi

LEAF_SPEC="$(IFS=','; echo "${LEAVES[*]}")"
CONFIG_PATH="${RUN_DIR}/matrix.env"
cat > "${CONFIG_PATH}" <<EOF
# Generated by run-local-matrix.sh; frozen beside its output before execution.
MATRIX_ROOTS=${ROOTS_PATH}
MATRIX_OUTPUT=${RUN_DIR}/analytics.jsonl
MATRIX_DEPTHS=${DEPTHS}
MATRIX_STRATA=${STRATA}
MATRIX_LEAVES=${LEAF_SPEC}
MATRIX_SCHEDULER=${SCHEDULER}
MATRIX_THREADS=${THREADS}
MATRIX_CACHE_ENTRIES_PER_WORKER=${CACHE}
MATRIX_SPLIT_PLIES=${SPLIT_PLIES}
MATRIX_ROOT_LIMIT=${ROOT_LIMIT}
MATRIX_MAX_FRONTIER_TASKS=${MAX_FRONTIER_TASKS}
MATRIX_MAX_HOST_BYTES=${MAX_HOST_BYTES}
EOF

echo "local matrix run: ${RUN_DIR}" >&2
exec "${SCRIPT_DIR}/run-matrix.sh" "${CONFIG_PATH}"
