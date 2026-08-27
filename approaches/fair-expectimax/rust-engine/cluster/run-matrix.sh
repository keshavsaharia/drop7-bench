#!/usr/bin/env bash
# Build and run one immutable Rust depth × strata × leaf matrix.
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <matrix.env>" >&2
  exit 2
fi

MATRIX_CONFIG="$1"
if [[ ! -f "${MATRIX_CONFIG}" ]]; then
  echo "matrix config not found: ${MATRIX_CONFIG}" >&2
  exit 2
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "${ROOT}"

# The config is an operator-authored, frozen shell environment file. It is
# copied and hashed beside the result before execution.
# shellcheck disable=SC1090
source "${MATRIX_CONFIG}"

: "${MATRIX_ROOTS:?MATRIX_ROOTS is required}"
: "${MATRIX_OUTPUT:?MATRIX_OUTPUT is required}"
: "${MATRIX_DEPTHS:?MATRIX_DEPTHS is required}"
: "${MATRIX_STRATA:?MATRIX_STRATA is required}"
: "${MATRIX_LEAVES:?MATRIX_LEAVES is required}"
: "${MATRIX_SCHEDULER:?MATRIX_SCHEDULER is required}"
: "${MATRIX_THREADS:?MATRIX_THREADS is required}"
: "${MATRIX_CACHE_ENTRIES_PER_WORKER:?MATRIX_CACHE_ENTRIES_PER_WORKER is required}"
: "${MATRIX_SPLIT_PLIES:?MATRIX_SPLIT_PLIES is required}"
: "${MATRIX_ROOT_LIMIT:?MATRIX_ROOT_LIMIT is required}"
: "${MATRIX_MAX_FRONTIER_TASKS:?MATRIX_MAX_FRONTIER_TASKS is required}"
: "${MATRIX_MAX_HOST_BYTES:?MATRIX_MAX_HOST_BYTES is required}"

if [[ ! -f "${MATRIX_ROOTS}" ]]; then
  echo "roots file not found: ${MATRIX_ROOTS}" >&2
  exit 2
fi
if [[ "${MATRIX_SCHEDULER}" != "frontier" && "${MATRIX_SCHEDULER}" != "root" ]]; then
  echo "MATRIX_SCHEDULER must be frontier or root" >&2
  exit 2
fi
for integer in MATRIX_THREADS MATRIX_CACHE_ENTRIES_PER_WORKER MATRIX_ROOT_LIMIT MATRIX_MAX_FRONTIER_TASKS MATRIX_MAX_HOST_BYTES; do
  value="${!integer}"
  if [[ ! "${value}" =~ ^[1-9][0-9]*$ ]]; then
    echo "${integer} must be a positive integer" >&2
    exit 2
  fi
done

OUT_DIR="$(dirname "${MATRIX_OUTPUT}")"
mkdir -p "${OUT_DIR}"
cp "${MATRIX_CONFIG}" "${OUT_DIR}/config.env"

if [[ -n "${MATRIX_SOURCE_GIT_COMMIT:-}" || -n "${MATRIX_SOURCE_GIT_STATUS:-}" ]]; then
  if [[ ! -f "${MATRIX_SOURCE_GIT_COMMIT:-}" || ! -f "${MATRIX_SOURCE_GIT_STATUS:-}" ]]; then
    echo "matrix source metadata files are missing" >&2
    exit 2
  fi
  cp "${MATRIX_SOURCE_GIT_COMMIT}" "${OUT_DIR}/git-commit.txt"
  cp "${MATRIX_SOURCE_GIT_STATUS}" "${OUT_DIR}/git-status.txt"
elif git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  git rev-parse HEAD > "${OUT_DIR}/git-commit.txt"
  git status --short > "${OUT_DIR}/git-status.txt"
else
  echo "matrix config must embed source Git metadata outside a Git checkout" >&2
  exit 2
fi

RUSTFLAGS="-C target-cpu=native" cargo build --release \
  --manifest-path approaches/fair-expectimax/rust-engine/Cargo.toml \
  --bin analyze

ANALYZE_ARGS=(
  --roots "${MATRIX_ROOTS}"
  --output "${MATRIX_OUTPUT}"
  --depths "${MATRIX_DEPTHS}"
  --strata "${MATRIX_STRATA}"
  --scheduler "${MATRIX_SCHEDULER}"
  --threads "${MATRIX_THREADS}"
  --cache "${MATRIX_CACHE_ENTRIES_PER_WORKER}"
  --split-plies "${MATRIX_SPLIT_PLIES}"
  --root-limit "${MATRIX_ROOT_LIMIT}"
  --max-frontier-tasks "${MATRIX_MAX_FRONTIER_TASKS}"
  --max-host-bytes "${MATRIX_MAX_HOST_BYTES}"
)
IFS=',' read -r -a LEAF_SPECS <<< "${MATRIX_LEAVES}"
for leaf in "${LEAF_SPECS[@]}"; do
  if [[ "${leaf}" != "fair" ]]; then
    leaf_path="${leaf#*=}"
    if [[ "${leaf}" == "${leaf_path}" || ! -f "${leaf_path}" ]]; then
      echo "leaf must be fair or NAME=existing-file: ${leaf}" >&2
      exit 2
    fi
  fi
  ANALYZE_ARGS+=(--leaf "${leaf}")
done

python3 .agents/skills/million-point-research/scripts/researchctl.py doctor \
  --output "${OUT_DIR}"

approaches/fair-expectimax/rust-engine/target/release/analyze "${ANALYZE_ARGS[@]}" \
  2> "${OUT_DIR}/analyze.log"

HASHER=()
if command -v sha256sum >/dev/null 2>&1; then
  HASHER=(sha256sum)
elif command -v shasum >/dev/null 2>&1; then
  HASHER=(shasum -a 256)
else
  echo "sha256sum or shasum is required" >&2
  exit 2
fi

HASH_INPUTS=(
  "${OUT_DIR}/config.env"
  "${MATRIX_ROOTS}"
  "${MATRIX_OUTPUT}"
  "${OUT_DIR}/git-commit.txt"
  "${OUT_DIR}/git-status.txt"
  "${OUT_DIR}/analyze.log"
)
for leaf in "${LEAF_SPECS[@]}"; do
  if [[ "${leaf}" != "fair" ]]; then
    HASH_INPUTS+=("${leaf#*=}")
  fi
done
"${HASHER[@]}" "${HASH_INPUTS[@]}" > "${OUT_DIR}/manifest.sha256"

echo "matrix complete: ${MATRIX_OUTPUT}"
echo "manifest: ${OUT_DIR}/manifest.sha256"
