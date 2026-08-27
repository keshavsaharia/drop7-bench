#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
TEST_RUN_DIR="${ROOT}/runs/test-aws-run-cleanup-$$"
if [[ "${TEST_RUN_DIR}" != "${ROOT}/runs/"* ]]; then
  echo "unsafe test run directory" >&2
  exit 2
fi
FAKE_BIN="${TEST_RUN_DIR}/bin"
AWS_LOG="${TEST_RUN_DIR}/aws.log"
DESCRIBE_COUNT="${TEST_RUN_DIR}/describe-count"
mkdir -p "${FAKE_BIN}"
trap 'rm -rf "${TEST_RUN_DIR}"' EXIT

cat > "${FAKE_BIN}/aws" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >> "${DROP7_FAKE_AWS_LOG}"
case "${1:-} ${2:-}" in
  'ec2 describe-instances')
    count=0
    if [[ -f "${DROP7_FAKE_DESCRIBE_COUNT}" ]]; then
      count="$(<"${DROP7_FAKE_DESCRIBE_COUNT}")"
    fi
    count="$((count + 1))"
    printf '%s\n' "${count}" > "${DROP7_FAKE_DESCRIBE_COUNT}"
    if (( count >= 2 )); then
      printf '%s\n' 'i-0123456789abcdef0'
    fi
    ;;
  'ec2 describe-capacity-reservations')
    printf '%s\n' 'cr-0123456789abcdef0'
    ;;
  'ec2 terminate-instances'|'ec2 cancel-capacity-reservation')
    ;;
  *)
    echo "unexpected fake AWS command: $*" >&2
    exit 2
    ;;
esac
EOF
chmod +x "${FAKE_BIN}/aws"

# shellcheck source=aws-run-cleanup.sh
source "${ROOT}/approaches/fair-expectimax/rust-engine/cluster/aws-run-cleanup.sh"
PATH="${FAKE_BIN}:${PATH}" \
DROP7_FAKE_AWS_LOG="${AWS_LOG}" \
DROP7_FAKE_DESCRIBE_COUNT="${DESCRIBE_COUNT}" \
DROP7_CLEANUP_RETRY_DELAY_SECONDS=0 \
  drop7_cleanup_run_resources us-east-2 RUN-test-01234567 '' '' 1 1 3

grep -q 'describe-instances.*Name=client-token,Values=RUN-test-01234567' "${AWS_LOG}"
grep -q 'describe-instances.*Name=tag:RunId,Values=RUN-test-01234567' "${AWS_LOG}"
grep -q 'terminate-instances.*i-0123456789abcdef0' "${AWS_LOG}"
grep -q 'describe-capacity-reservations.*Name=tag:RunId,Values=RUN-test-01234567' "${AWS_LOG}"
grep -q 'cancel-capacity-reservation.*cr-0123456789abcdef0' "${AWS_LOG}"
if [[ "$(<"${DESCRIBE_COUNT}")" != "2" ]]; then
  echo "instance discovery did not retry exactly once" >&2
  exit 1
fi

"${ROOT}/approaches/fair-expectimax/rust-engine/cluster/package-source.sh" \
  --local "${TEST_RUN_DIR}/package" >/dev/null
if [[ ! -s "${TEST_RUN_DIR}/package/drop7-source.tar.gz" ]]; then
  echo "run-scoped source package was not created" >&2
  exit 1
fi

if rg -n '\$\(mktemp\)' "${ROOT}/approaches/fair-expectimax/rust-engine/cluster"/*.sh; then
  echo "cluster scripts contain a bare mktemp call" >&2
  exit 1
fi

echo "aws cleanup recovery test passed"
