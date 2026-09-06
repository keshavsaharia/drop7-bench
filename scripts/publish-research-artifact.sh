#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
usage: publish-research-artifact.sh --run-id RUN-ID (--file FILE | --dir DIR) --public [options]

Upload public-safe research artifacts to the immutable run namespace and print
one canonical data.drop7.dev reference per object.

Options:
  --file FILE        One regular file to publish
  --dir DIR          Publish every regular file below DIR, keeping its layout
                     (object path = --key prefix + path relative to DIR)
  --key PATH         Object path below runs/<run-id>/ (default: the file's
                     basename); with --dir, a prefix directory for the tree
  --exclude GLOB     Skip files whose path relative to DIR matches GLOB
                     (repeatable; with --dir only; e.g. --exclude '*.bin')
  --manifest FILE    Append one JSON line per object ({"key","bytes","sha256",
                     "ref","status"}) so a record can cite every reference
  --dry-run          Hash and list what would be uploaded; touch nothing remote
  --profile PROFILE  AWS profile (default: drop7-research)
  --bucket BUCKET    S3 bucket (default: drop7-bench-data)
  --public           Required acknowledgement that the files are safe to publish

An object whose key already holds the identical bytes (same SHA-256 metadata
and byte count) is reported as already published; a conflicting object is
never replaced.
EOF
}

RUN_ID=""
SOURCE_FILE=""
SOURCE_DIR=""
OBJECT_PATH=""
MANIFEST=""
DRY_RUN=0
EXCLUDES=()
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
    --dir)
      SOURCE_DIR="${2:-}"
      shift 2
      ;;
    --key)
      OBJECT_PATH="${2:-}"
      shift 2
      ;;
    --exclude)
      EXCLUDES+=("${2:-}")
      shift 2
      ;;
    --manifest)
      MANIFEST="${2:-}"
      shift 2
      ;;
    --dry-run)
      DRY_RUN=1
      shift
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
if [[ -n "${SOURCE_FILE}" && -n "${SOURCE_DIR}" ]]; then
  echo "use either --file or --dir, not both" >&2
  exit 2
fi
if [[ -n "${SOURCE_DIR}" ]]; then
  if [[ ! -d "${SOURCE_DIR}" ]]; then
    echo "--dir must name an existing directory" >&2
    exit 2
  fi
elif [[ -z "${SOURCE_FILE}" || ! -f "${SOURCE_FILE}" ]]; then
  echo "--file must name an existing regular file" >&2
  exit 2
fi
if [[ "${PUBLIC_ACK}" != "1" ]]; then
  echo "--public is required; the destination is world-readable" >&2
  exit 2
fi
if [[ -z "${OBJECT_PATH}" && -n "${SOURCE_FILE}" ]]; then
  OBJECT_PATH="$(basename "${SOURCE_FILE}")"
fi
if [[ -n "${OBJECT_PATH}" ]] && [[ "${OBJECT_PATH}" == /* \
   || "${OBJECT_PATH}" == *".."* \
   || ! "${OBJECT_PATH}" =~ ^[A-Za-z0-9._/-]+$ ]]; then
  echo "--key must be a safe relative path without '..'" >&2
  exit 2
fi
if [[ ${#EXCLUDES[@]} -gt 0 && -z "${SOURCE_DIR}" ]]; then
  echo "--exclude applies to --dir only" >&2
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

hash_file() {
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
  elif command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  else
    echo "shasum or sha256sum is required" >&2
    exit 2
  fi
}

record_manifest() {
  # $1 key, $2 bytes, $3 sha256, $4 ref, $5 status
  if [[ -n "${MANIFEST}" ]]; then
    printf '{"key":"%s","bytes":%s,"sha256":"%s","ref":"%s","status":"%s"}\n' \
      "$1" "$2" "$3" "$4" "$5" >> "${MANIFEST}"
  fi
}

# publish_one FILE OBJECT_PATH: hash, upload unless present, verify, print the reference.
publish_one() {
  local source_file="$1" object_path="$2"
  local sha256 object_key object_uri public_ref expected_bytes
  if [[ "${object_path}" == /* \
     || "${object_path}" == *".."* \
     || ! "${object_path}" =~ ^[A-Za-z0-9._/-]+$ ]]; then
    echo "object path ${object_path} is not a safe relative path" >&2
    exit 2
  fi
  sha256="$(hash_file "${source_file}")"
  object_key="runs/${RUN_ID}/${object_path}"
  object_uri="s3://${BUCKET}/${object_key}"
  public_ref="https://data.drop7.dev/${object_key}#sha256=${sha256}"
  expected_bytes="$(wc -c < "${source_file}" | tr -d ' ')"

  if [[ "${DRY_RUN}" == "1" ]]; then
    printf 'dry-run %s bytes %s -> %s\n' "${expected_bytes}" "${source_file}" "${public_ref}"
    record_manifest "${object_key}" "${expected_bytes}" "${sha256}" "${public_ref}" "dry-run"
    return 0
  fi

  local existing_head existing_sha existing_bytes
  if existing_head="$(aws s3api head-object \
    --bucket "${BUCKET}" \
    --key "${object_key}" \
    --profile "${AWS_PROFILE_VALUE}" \
    --region us-east-2 \
    --query '[Metadata."drop7-sha256", ContentLength]' \
    --output text 2>/dev/null)"; then
    read -r existing_sha existing_bytes <<< "${existing_head}"
    if [[ "${existing_sha}" == "${sha256}" \
       && "${existing_bytes}" == "${expected_bytes}" ]]; then
      printf '%s\n' "${public_ref}"
      record_manifest "${object_key}" "${expected_bytes}" "${sha256}" "${public_ref}" "already-published"
      return 0
    fi
    echo "refusing to overwrite existing ${object_uri}" >&2
    exit 1
  fi

  aws s3 cp "${source_file}" "${object_uri}" \
    --profile "${AWS_PROFILE_VALUE}" \
    --region us-east-2 \
    --checksum-algorithm SHA256 \
    --metadata "drop7-sha256=${sha256}" \
    --only-show-errors

  local stored_sha stored_bytes
  read -r stored_sha stored_bytes < <(aws s3api head-object \
    --bucket "${BUCKET}" \
    --key "${object_key}" \
    --profile "${AWS_PROFILE_VALUE}" \
    --region us-east-2 \
    --query '[Metadata."drop7-sha256", ContentLength]' \
    --output text)
  if [[ "${stored_sha}" != "${sha256}" || "${stored_bytes}" != "${expected_bytes}" ]]; then
    echo "uploaded object metadata or byte count did not verify" >&2
    exit 1
  fi

  printf '%s\n' "${public_ref}"
  record_manifest "${object_key}" "${expected_bytes}" "${sha256}" "${public_ref}" "uploaded"
}

excluded() {
  local relative="$1" pattern
  for pattern in "${EXCLUDES[@]+"${EXCLUDES[@]}"}"; do
    # shellcheck disable=SC2053
    if [[ "${relative}" == ${pattern} ]]; then
      return 0
    fi
  done
  return 1
}

if [[ -n "${SOURCE_DIR}" ]]; then
  prefix="${OBJECT_PATH:+${OBJECT_PATH%/}/}"
  count=0
  while IFS= read -r -d '' path; do
    relative="${path#"${SOURCE_DIR%/}"/}"
    if excluded "${relative}"; then
      continue
    fi
    publish_one "${path}" "${prefix}${relative}"
    count=$((count + 1))
  done < <(find "${SOURCE_DIR%/}" -type f -print0 | sort -z)
  if [[ "${count}" == "0" ]]; then
    echo "no regular files to publish under ${SOURCE_DIR}" >&2
    exit 2
  fi
else
  publish_one "${SOURCE_FILE}" "${OBJECT_PATH}"
fi
