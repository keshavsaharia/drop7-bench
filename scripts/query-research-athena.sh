#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
usage: query-research-athena.sh (--query SQL | --file FILE) [options]

Run one bounded, read-only query against Drop7 analytics and print at most 500 rows.

Options:
  --stage STAGE      production or dev (default: production)
  --profile PROFILE  AWS profile (default: drop7-research)
  --output FORMAT    json, table, or text (default: json)
EOF
}

QUERY_SQL=""
QUERY_FILE=""
DEPLOYMENT_STAGE="production"
AWS_PROFILE_VALUE="${AWS_PROFILE:-drop7-research}"
OUTPUT_FORMAT="json"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --query)
      QUERY_SQL="${2:-}"
      shift 2
      ;;
    --file)
      QUERY_FILE="${2:-}"
      shift 2
      ;;
    --stage)
      DEPLOYMENT_STAGE="${2:-}"
      shift 2
      ;;
    --profile)
      AWS_PROFILE_VALUE="${2:-}"
      shift 2
      ;;
    --output)
      OUTPUT_FORMAT="${2:-}"
      shift 2
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

if [[ -n "${QUERY_SQL}" && -n "${QUERY_FILE}" ]] ||
   [[ -z "${QUERY_SQL}" && -z "${QUERY_FILE}" ]]; then
  echo "provide exactly one of --query or --file" >&2
  exit 2
fi
if [[ -n "${QUERY_FILE}" ]]; then
  if [[ ! -f "${QUERY_FILE}" ]]; then
    echo "--file must name an existing regular file" >&2
    exit 2
  fi
  QUERY_SQL="$(<"${QUERY_FILE}")"
fi
if [[ "${#QUERY_SQL}" -gt 10000 || ! "${QUERY_SQL}" =~ [^[:space:]] ]]; then
  echo "query must contain between 1 and 10,000 characters" >&2
  exit 2
fi
if [[ "${QUERY_SQL}" == *";"* || "${QUERY_SQL}" == *"--"* || "${QUERY_SQL}" == *"/*"* ]]; then
  echo "run one read-only statement without semicolons or SQL comments" >&2
  exit 2
fi

LOWER_QUERY="$(printf '%s' "${QUERY_SQL}" | tr '[:upper:]' '[:lower:]')"
FIRST_WORD="$(printf '%s' "${LOWER_QUERY}" | awk '{print $1; exit}')"
if [[ "${FIRST_WORD}" != "select" && "${FIRST_WORD}" != "with" ]]; then
  echo "only SELECT and WITH queries are allowed" >&2
  exit 2
fi
for keyword in insert update delete merge create drop alter truncate unload call optimize vacuum grant revoke prepare execute msck; do
  if [[ "${LOWER_QUERY}" =~ (^|[^a-z_])${keyword}([^a-z_]|$) ]]; then
    echo "query contains forbidden keyword: ${keyword}" >&2
    exit 2
  fi
done
if [[ "${OUTPUT_FORMAT}" != "json" && "${OUTPUT_FORMAT}" != "table" && "${OUTPUT_FORMAT}" != "text" ]]; then
  echo "--output must be json, table, or text" >&2
  exit 2
fi
if ! command -v aws >/dev/null 2>&1; then
  echo "aws is required" >&2
  exit 2
fi

case "${DEPLOYMENT_STAGE}" in
  production)
    DATABASE="drop7_production_analytics"
    WORKGROUP="drop7-production-analytics"
    ;;
  dev)
    DATABASE="drop7_dev_analytics"
    WORKGROUP="drop7-dev-analytics"
    ;;
  *)
    echo "--stage must be production or dev" >&2
    exit 2
    ;;
esac

WRAPPED_QUERY="SELECT * FROM (${QUERY_SQL}) AS agent_query LIMIT 500"
QUERY_EXECUTION_ID="$(aws athena start-query-execution \
  --query-string "${WRAPPED_QUERY}" \
  --query-execution-context "Catalog=AwsDataCatalog,Database=${DATABASE}" \
  --work-group "${WORKGROUP}" \
  --profile "${AWS_PROFILE_VALUE}" \
  --region us-east-1 \
  --query QueryExecutionId \
  --output text)"

stop_query() {
  aws athena stop-query-execution \
    --query-execution-id "${QUERY_EXECUTION_ID}" \
    --profile "${AWS_PROFILE_VALUE}" \
    --region us-east-1 >/dev/null 2>&1 || true
}
trap stop_query INT TERM

for _ in {1..120}; do
  read -r QUERY_STATE STATE_REASON < <(aws athena get-query-execution \
    --query-execution-id "${QUERY_EXECUTION_ID}" \
    --profile "${AWS_PROFILE_VALUE}" \
    --region us-east-1 \
    --query '[QueryExecution.Status.State, QueryExecution.Status.StateChangeReason]' \
    --output text)
  case "${QUERY_STATE}" in
    SUCCEEDED)
      trap - INT TERM
      read -r DATA_SCANNED ENGINE_MS < <(aws athena get-query-execution \
        --query-execution-id "${QUERY_EXECUTION_ID}" \
        --profile "${AWS_PROFILE_VALUE}" \
        --region us-east-1 \
        --query '[QueryExecution.Statistics.DataScannedInBytes, QueryExecution.Statistics.EngineExecutionTimeInMillis]' \
        --output text)
      printf 'query_execution_id=%s data_scanned_bytes=%s engine_execution_ms=%s\n' \
        "${QUERY_EXECUTION_ID}" "${DATA_SCANNED}" "${ENGINE_MS}" >&2
      aws athena get-query-results \
        --query-execution-id "${QUERY_EXECUTION_ID}" \
        --max-results 500 \
        --no-paginate \
        --profile "${AWS_PROFILE_VALUE}" \
        --region us-east-1 \
        --output "${OUTPUT_FORMAT}"
      exit 0
      ;;
    FAILED|CANCELLED)
      trap - INT TERM
      echo "Athena query ${QUERY_STATE,,}: ${STATE_REASON}" >&2
      exit 1
      ;;
  esac
  sleep 1
done

stop_query
trap - INT TERM
echo "Athena query exceeded the 120-second helper timeout" >&2
exit 1
