#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
usage: publish-research-artifact.sh --run-id RUN-ID --file FILE --public [options]

Upload one public-safe research artifact to the immutable run namespace and print
the canonical data.drop7.dev reference.

Options:
  --key PATH         Object path below runs/<run-id>/ (default: file basename)
  --profile PROFILE  AWS profile (default: drop7-research)
  --bucket BUCKET    S3 bucket (default: drop7-bench-data)
  --public           Required acknowledgement that the file is safe to publish
EOF
}

RUN_ID=""
SOURCE_FILE=""
OBJECT_PATH=""
AWS_PROFILE_VALUE="${AWS_PROFILE:-drop7-research}"
BUCKET="${DROP7_S3_BUCKET:-drop7-bench-data}"
PUBLIC_ACK=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --run-id)
      RUN_ID="${2:-}"
      shift 2
      ;;
    --file)
      SOURCE_FILE="${2:-}"
      shift 2
      ;;
    --key)
      OBJECT_PATH="${2:-}"
      shift 2
      ;;
    --profile)
      AWS_PROFILE_VALUE="${2:-}"
      shift 2
      ;;
    --bucket)
      BUCKET="${2:-}"
      shift 2
      ;;
    --public)
      PUBLIC_ACK=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ ! "${RUN_ID}" =~ ^RUN-[0-9]{8}T[0-9]{6}Z-[a-f0-9]{8}$ ]]; then
  echo "--run-id must be a canonical RUN identifier" >&2
  exit 2
fi
if [[ -z "${SOURCE_FILE}" || ! -f "${SOURCE_FILE}" ]]; then
  echo "--file must name an existing regular file" >&2
  exit 2
fi
if [[ "${PUBLIC_ACK}" != "1" ]]; then
  echo "--public is required; the destination is world-readable" >&2
  exit 2
fi
if [[ -z "${OBJECT_PATH}" ]]; then
  OBJECT_PATH="$(basename "${SOURCE_FILE}")"
fi
if [[ "${OBJECT_PATH}" == /* \
   || "${OBJECT_PATH}" == *".."* \
   || ! "${OBJECT_PATH}" =~ ^[A-Za-z0-9._/-]+$ ]]; then
  echo "--key must be a safe relative path without '..'" >&2
  exit 2
fi
if [[ ! "${BUCKET}" =~ ^[a-z0-9][a-z0-9.-]+$ ]]; then
  echo "--bucket is invalid" >&2
  exit 2
fi
for command_name in aws awk wc tr; do
  if ! command -v "${command_name}" >/dev/null 2>&1; then
    echo "${command_name} is required" >&2
    exit 2
  fi
done

if command -v shasum >/dev/null 2>&1; then
  SHA256="$(shasum -a 256 "${SOURCE_FILE}" | awk '{print $1}')"
elif command -v sha256sum >/dev/null 2>&1; then
  SHA256="$(sha256sum "${SOURCE_FILE}" | awk '{print $1}')"
else
  echo "shasum or sha256sum is required" >&2
  exit 2
fi

OBJECT_KEY="runs/${RUN_ID}/${OBJECT_PATH}"
OBJECT_URI="s3://${BUCKET}/${OBJECT_KEY}"
PUBLIC_REF="https://data.drop7.dev/${OBJECT_KEY}#sha256=${SHA256}"
EXPECTED_BYTES="$(wc -c < "${SOURCE_FILE}" | tr -d ' ')"

if EXISTING_HEAD="$(aws s3api head-object \
  --bucket "${BUCKET}" \
  --key "${OBJECT_KEY}" \
  --profile "${AWS_PROFILE_VALUE}" \
  --region us-east-2 \
  --query '[Metadata."drop7-sha256", ContentLength]' \
  --output text 2>/dev/null)"; then
  read -r EXISTING_SHA EXISTING_BYTES <<< "${EXISTING_HEAD}"
  if [[ "${EXISTING_SHA}" == "${SHA256}" \
     && "${EXISTING_BYTES}" == "${EXPECTED_BYTES}" ]]; then
    printf '%s\n' "${PUBLIC_REF}"
    exit 0
  fi
  echo "refusing to overwrite existing ${OBJECT_URI}" >&2
  exit 1
fi

aws s3 cp "${SOURCE_FILE}" "${OBJECT_URI}" \
  --profile "${AWS_PROFILE_VALUE}" \
  --region us-east-2 \
  --checksum-algorithm SHA256 \
  --metadata "drop7-sha256=${SHA256}" \
  --only-show-errors

read -r STORED_SHA STORED_BYTES < <(aws s3api head-object \
  --bucket "${BUCKET}" \
  --key "${OBJECT_KEY}" \
  --profile "${AWS_PROFILE_VALUE}" \
  --region us-east-2 \
  --query '[Metadata."drop7-sha256", ContentLength]' \
  --output text)
if [[ "${STORED_SHA}" != "${SHA256}" || "${STORED_BYTES}" != "${EXPECTED_BYTES}" ]]; then
  echo "uploaded object metadata or byte count did not verify" >&2
  exit 1
fi

printf '%s\n' "${PUBLIC_REF}"
