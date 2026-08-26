#!/usr/bin/env bash
# Package the exact tracked + non-ignored untracked checkout, optionally upload
# it to an operator-owned S3 URI.
set -euo pipefail

usage() {
  echo "usage: $0 [--local] <artifact-dir> [s3://bucket/prefix]" >&2
}

LOCAL_ONLY=0
if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
fi
if [[ "${1:-}" == "--local" ]]; then
  LOCAL_ONLY=1
  shift
fi
if (( LOCAL_ONLY == 1 )) && [[ $# -ne 1 ]]; then
  usage
  exit 2
fi
if (( LOCAL_ONLY == 0 )) && [[ $# -ne 2 ]]; then
  usage
  exit 2
fi

ARTIFACT_DIR="$1"
S3_PREFIX="${2:-}"
if (( LOCAL_ONLY == 0 )); then
  S3_PREFIX="${S3_PREFIX%/}"
  if [[ ! "${S3_PREFIX}" =~ ^s3://[A-Za-z0-9][A-Za-z0-9._/-]*$ ]]; then
    echo "invalid S3 prefix: ${S3_PREFIX}" >&2
    exit 2
  fi
  if ! command -v aws >/dev/null 2>&1; then
    echo "AWS CLI is required" >&2
    exit 2
  fi
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
mkdir -p "${ARTIFACT_DIR}"
ARCHIVE="${ARTIFACT_DIR}/drop7-source.tar.gz"
SOURCE_LIST="$(mktemp)"
trap 'rm -f "${SOURCE_LIST}"' EXIT

# `git ls-files` is the source boundary: committed files plus current
# non-ignored untracked files. Generated runs, targets, node_modules and web
# builds are excluded by the repository's ignore rules rather than an
# incomplete hand-maintained tar exclusion list. A tracked file deleted in the
# working tree is intentionally absent from the snapshot.
while IFS= read -r -d '' path; do
  if [[ -e "${ROOT}/${path}" || -L "${ROOT}/${path}" ]]; then
    printf '%s\0' "${path}" >> "${SOURCE_LIST}"
  fi
done < <(git -C "${ROOT}" ls-files --cached --others --exclude-standard -z)
tar -C "${ROOT}" --null -T "${SOURCE_LIST}" -czf "${ARCHIVE}"

if command -v sha256sum >/dev/null 2>&1; then
  SOURCE_SHA256="$(sha256sum "${ARCHIVE}" | awk '{print $1}')"
else
  SOURCE_SHA256="$(shasum -a 256 "${ARCHIVE}" | awk '{print $1}')"
fi
echo "${SOURCE_SHA256}  drop7-source.tar.gz" > "${ARTIFACT_DIR}/drop7-source.tar.gz.sha256"

if (( LOCAL_ONLY == 1 )); then
  echo "source_archive=${ARCHIVE}"
  echo "source_sha256=${SOURCE_SHA256}"
  exit 0
fi

aws s3 cp "${ARCHIVE}" "${S3_PREFIX}/drop7-source.tar.gz" --only-show-errors
aws s3 cp "${ARTIFACT_DIR}/drop7-source.tar.gz.sha256" \
  "${S3_PREFIX}/drop7-source.tar.gz.sha256" --only-show-errors
echo "source_s3_uri=${S3_PREFIX}/drop7-source.tar.gz"
echo "source_sha256=${SOURCE_SHA256}"
